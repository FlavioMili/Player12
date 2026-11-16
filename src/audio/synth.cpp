// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "audio/synth.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace Audio
{
namespace
{
constexpr float PI = std::numbers::pi_v<float>;
constexpr float TWO_PI = 2.0f * PI;
/** Envelopes and filter sweeps are updated every CONTROL_BLOCK samples. */
constexpr int CONTROL_BLOCK = 32;
constexpr float CONTROL_SECONDS =
    static_cast<float>(CONTROL_BLOCK) / SAMPLE_RATE_F;

/** Per-sample multiplier that decays by 1/e over `seconds`. */
float decayCoefficient(float seconds)
{
  return std::exp(-1.0f / (std::max(seconds, 1e-4f) * SAMPLE_RATE_F));
}

float smoothstep(float x)
{
  const float t = std::clamp(x, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float advancePhase(float phase, float hz)
{
  phase += hz * SAMPLE_PERIOD;
  return phase - std::floor(phase);
}

/** Whistle segments: {start, length} in seconds (length 0 ends the list). */
struct Segment
{
  float start;
  float length;
};
using SegmentList = std::array<Segment, 3>;
constexpr std::array<SegmentList, 4> WHISTLE_SEGMENTS = {{
    {{{0.0f, 0.26f}, {0.0f, 0.0f}, {0.0f, 0.0f}}},
    {{{0.0f, 0.62f}, {0.0f, 0.0f}, {0.0f, 0.0f}}},
    {{{0.0f, 0.42f}, {0.62f, 0.95f}, {0.0f, 0.0f}}},
    {{{0.0f, 0.38f}, {0.58f, 0.38f}, {1.16f, 1.35f}}},
}};

const SegmentList& whistleSegments(std::uint8_t variant)
{
  return WHISTLE_SEGMENTS[std::min<std::size_t>(variant,
                                                WHISTLE_SEGMENTS.size() - 1)];
}

/** Base pitch of the chanting voices behind crowd vowels (Hz). */
constexpr std::array<float, 6> CROWD_VOICE_PITCHES = {112.0f, 131.0f, 149.0f,
                                                      176.0f, 203.0f, 238.0f};
/**
 * Output trim per Sound so that each one peaks at a sensible level at unit
 * gain and full intensity (a roar ~0.75, a hard shot ~0.4, the bed quieter).
 */
constexpr std::array<float, SOUND_COUNT> SOUND_LEVELS = {
    0.2f,    // KICK
    0.3f,    // NET
    0.31f,   // WOODWORK
    0.6f,    // WHISTLE
    0.345f,  // CROWD_OOH
    0.4f,    // CROWD_CHEER
    0.3f,    // CROWD_GROAN
    0.33f,   // CROWD_BOO
    0.41f,   // CROWD_WHISTLES
    0.3f,    // CROWD_APPLAUSE
};
/** Crowd bed trim (the bed has no per-command gain). */
constexpr float BED_LEVEL = 2.0f;

/** Free-free bar partial ratios: a metallic, inharmonic ring. */
constexpr std::array<float, 6> BAR_RATIOS = {1.0f,   2.756f,  5.404f,
                                             8.933f, 13.344f, 18.638f};
}  // namespace

void OnePole::set(float cutoffHz)
{
  const float cutoff = std::clamp(cutoffHz, 10.0f, SAMPLE_RATE_F * 0.45f);
  coefficient = 1.0f - std::exp(-2.0f * PI * cutoff / SAMPLE_RATE_F);
}

void Svf::set(float cutoffHz, float q)
{
  const float cutoff = std::clamp(cutoffHz, 20.0f, SAMPLE_RATE_F * 0.45f);
  const float g = std::tan(PI * cutoff / SAMPLE_RATE_F);
  k = 1.0f / std::max(q, 0.05f);
  a1 = 1.0f / (1.0f + g * (g + k));
  a2 = g * a1;
  a3 = g * a2;
}

float Drift::step(float seconds)
{
  if (std::abs(target - current) < 0.04f) target = noise.unit();
  current += (target - current) * std::min(1.0f, rate * seconds);
  return current;
}

bool isCrowdSound(Sound sound)
{
  using enum Sound;
  switch (sound)
  {
    case CROWD_OOH:
    case CROWD_CHEER:
    case CROWD_GROAN:
    case CROWD_BOO:
    case CROWD_WHISTLES:
    case CROWD_APPLAUSE:
      return true;
    default:
      return false;
  }
}

void Voice::start(const SoundCommand& soundCommand, std::uint32_t seed)
{
  command = soundCommand;
  command.intensity = std::clamp(command.intensity, 0.0f, 1.0f);
  playing = true;
  time = 0.0f;
  delay = std::max(0.0f, command.delay);
  controlCounter = 0;
  noise.reseed(seed);
  noiseRight.reseed(seed * 2654435761U + 17U);
  filterA.reset();
  filterB.reset();
  filterC.reset();
  filterD.reset();
  toneLeft.reset();
  toneRight.reset();
  toneLeft.set(2600.0f);
  toneRight.set(2600.0f);
  phase.fill(0.0f);
  amplitude.fill(0.0f);
  lfoPhase = 0.0f;
  glide = 1.0f;
  excitation = 0.0f;
  excitationRight = 0.0f;
  envelope = 0.0f;

  const float pan = std::clamp(command.pan, -1.0f, 1.0f);
  const float angle = (pan + 1.0f) * PI * 0.25f;
  const float gain =
      command.gain * (command.sound < Sound::COUNT
                          ? SOUND_LEVELS[static_cast<std::size_t>(
                                std::to_underlying(command.sound))]
                          : 0.0f);
  leftGain = std::cos(angle) * std::numbers::sqrt2_v<float> * gain;
  rightGain = std::sin(angle) * std::numbers::sqrt2_v<float> * gain;

  const float intensity = command.intensity;
  switch (command.sound)
  {
    case Sound::KICK:
      duration = 0.22f;
      // Body: a low thud falling in pitch; click: leather on boot.
      amplitude[0] = 1.0f;
      decay[0] = decayCoefficient(0.045f + 0.035f * intensity);
      amplitude[1] = 0.35f + 0.65f * intensity;
      decay[1] = decayCoefficient(0.0035f + 0.002f * intensity);
      amplitude[2] = 0.5f;
      decay[2] = decayCoefficient(0.018f);
      frequency[0] = 85.0f + 75.0f * intensity;
      filterA.set(1900.0f + 1800.0f * intensity, 1.1f);
      filterB.set(420.0f + 120.0f * intensity, 1.8f);
      break;
    case Sound::NET:
      duration = 0.55f + 0.25f * intensity;
      amplitude[0] = 1.0f;
      decay[0] = decayCoefficient(0.11f + 0.06f * intensity);
      amplitude[1] = 0.35f;
      decay[1] = decayCoefficient(0.32f);
      amplitude[2] = 0.6f;
      decay[2] = decayCoefficient(0.05f);
      filterA.set(3600.0f, 0.8f);
      filterB.set(2200.0f, 0.7f);
      filterC.set(35.0f, 0.7f);
      toneLeft.set(6500.0f);
      filterD.set(170.0f, 1.5f);
      break;
    case Sound::WOODWORK:
    {
      duration = 1.7f;
      const float base = 455.0f + 60.0f * noise.unit();
      for (std::size_t index = 0; index < PARTIALS; ++index)
      {
        frequency[index] = base * BAR_RATIOS[index];
        amplitude[index] = (0.35f + 0.65f * intensity) /
                           (1.0f + 0.9f * static_cast<float>(index));
        decay[index] =
            decayCoefficient((1.25f - 0.17f * static_cast<float>(index)) *
                             (0.7f + 0.3f * intensity));
      }
      excitation = 1.0f;
      filterA.set(3200.0f, 1.2f);
      break;
    }
    case Sound::WHISTLE:
    {
      const SegmentList& segments = whistleSegments(command.variant);
      duration = 0.0f;
      for (const Segment& segment : segments)
        if (segment.length > 0.0f)
          duration = std::max(duration, segment.start + segment.length);
      duration += 0.06f;
      frequency[0] = 2780.0f + 140.0f * (noise.unit() - 0.5f);
      filterA.set(frequency[0], 3.0f);
      filterB.set(6500.0f, 0.7f);
      break;
    }
    case Sound::CROWD_OOH:
      attack = 0.22f;
      hold = 0.2f + 0.35f * intensity;
      release = 1.0f + 0.5f * intensity;
      break;
    case Sound::CROWD_CHEER:
      toneLeft.set(3300.0f);
      toneRight.set(3300.0f);
      attack = 0.12f;
      hold = 0.8f + 4.2f * intensity;
      release = 1.8f + 1.4f * intensity;
      break;
    case Sound::CROWD_GROAN:
      attack = 0.08f;
      hold = 0.15f + 0.25f * intensity;
      release = 1.1f + 0.4f * intensity;
      break;
    case Sound::CROWD_BOO:
      attack = 0.3f;
      hold = 0.7f + 1.6f * intensity;
      release = 0.8f;
      break;
    case Sound::CROWD_WHISTLES:
      attack = 0.05f;
      hold = 1.2f + 0.8f * intensity;
      release = 0.4f;
      for (std::size_t index = 0; index < PARTIALS; ++index)
      {
        frequency[index] = 1900.0f + 1700.0f * noise.unit();
        onset[index] = 0.7f * noise.unit();
        decay[index] = 0.5f + 0.8f * noise.unit();  // tone length (s)
        amplitude[index] = 0.12f + 0.1f * noise.unit();
        phase[index] = noise.unit();
      }
      break;
    case Sound::CROWD_APPLAUSE:
      attack = 0.3f;
      hold = 0.8f + 2.2f * intensity;
      release = 1.4f;
      filterA.set(1500.0f, 0.9f);
      filterC.set(1750.0f, 0.9f);
      toneLeft.set(4500.0f);
      toneRight.set(4500.0f);
      filterB.set(420.0f, 0.7f);
      break;
    case Sound::COUNT:
      playing = false;
      break;
  }
  if (isCrowdSound(command.sound))
  {
    duration = attack + hold + release;
    if (command.sound != Sound::CROWD_WHISTLES &&
        command.sound != Sound::CROWD_APPLAUSE)
    {
      // Voiced part: a spread of chanting voices, slightly detuned per voice.
      for (std::size_t index = 0; index < PARTIALS; ++index)
      {
        frequency[index] =
            CROWD_VOICE_PITCHES[index] * (0.94f + 0.12f * noise.unit());
        phase[index] = noise.unit();
      }
    }
  }
  updateControl();
}

float Voice::crowdEnvelope() const
{
  if (time < attack) return smoothstep(time / attack);
  const float afterHold = time - attack - hold;
  if (afterHold <= 0.0f) return 1.0f;
  const float remaining = 1.0f - afterHold / release;
  return remaining <= 0.0f ? 0.0f : remaining * remaining;
}

float Voice::whistleEnvelope() const
{
  for (const Segment& segment : whistleSegments(command.variant))
  {
    if (segment.length <= 0.0f) break;
    const float local = time - segment.start;
    if (local < 0.0f || local > segment.length) continue;
    return std::min(1.0f, local / 0.012f) *
           std::min(1.0f, (segment.length - local) / 0.045f);
  }
  return 0.0f;
}

void Voice::updateControl()
{
  const float progress = duration > 0.0f ? time / duration : 1.0f;
  switch (command.sound)
  {
    case Sound::CROWD_OOH:
      // "Oooh": pitch and first formant rise with the gasp, then sink.
      glide = 1.0f + 0.22f * std::sin(PI * std::min(1.0f, progress * 1.4f)) -
              0.08f * progress;
      filterA.set(310.0f + 90.0f * (glide - 1.0f) * 4.0f, 4.0f);
      filterB.set(760.0f, 4.5f);
      filterC.set(330.0f + 80.0f * (glide - 1.0f) * 4.0f, 4.0f);
      filterD.set(800.0f, 4.5f);
      break;
    case Sound::CROWD_CHEER:
      glide = 1.12f + 0.1f * std::sin(PI * std::min(1.0f, progress * 2.0f)) -
              0.12f * progress;
      filterA.set(720.0f + 180.0f * command.intensity, 1.6f);
      filterB.set(1350.0f + 450.0f * command.intensity, 1.8f);
      filterC.set(760.0f + 180.0f * command.intensity, 1.6f);
      filterD.set(1500.0f + 450.0f * command.intensity, 1.8f);
      break;
    case Sound::CROWD_GROAN:
      glide = 1.05f - 0.3f * std::min(1.0f, progress * 1.2f);
      filterA.set(560.0f - 170.0f * progress, 3.2f);
      filterB.set(1050.0f - 200.0f * progress, 3.5f);
      filterC.set(590.0f - 170.0f * progress, 3.2f);
      filterD.set(1000.0f - 200.0f * progress, 3.5f);
      break;
    case Sound::CROWD_BOO:
      glide = 0.82f + 0.03f * std::sin(TWO_PI * 5.5f * time);
      filterA.set(290.0f, 4.0f);
      filterB.set(680.0f, 4.0f);
      filterC.set(305.0f, 4.0f);
      filterD.set(650.0f, 4.0f);
      break;
    default:
      break;
  }
}

float Voice::renderKick()
{
  const float bodyHz = 52.0f + frequency[0] * std::exp(-time * (1.0f / 0.022f));
  phase[0] = advancePhase(phase[0], bodyHz);
  const float body = std::sin(TWO_PI * phase[0]) * amplitude[0];
  amplitude[0] *= decay[0];
  const float n = noise.next();
  const float click = filterA.process(n).band * amplitude[1] * 2.2f;
  amplitude[1] *= decay[1];
  const float thump = filterB.process(n).band * amplitude[2];
  amplitude[2] *= decay[2];
  return (body * 0.85f + click + thump) * (0.35f + 0.65f * command.intensity);
}

float Voice::renderNet()
{
  const float n = toneLeft.process(noise.next()) * 1.6f;
  const float attackGain = std::min(1.0f, time / 0.006f);
  // Rustle: the mesh flaps, so the hiss is amplitude-modulated by a slow,
  // irregular signal.
  const float rustle =
      0.55f + std::min(0.45f, std::abs(filterC.process(n).low) * 18.0f);
  const float hiss =
      (filterA.process(n).band * 1.4f + filterB.process(n).high * 0.2f) *
      (amplitude[0] + amplitude[1]) * rustle;
  amplitude[0] *= decay[0];
  amplitude[1] *= decay[1];
  const float bulge = filterD.process(n).band * amplitude[2] * 1.3f;
  amplitude[2] *= decay[2];
  return (hiss + bulge) * attackGain * (0.5f + 0.5f * command.intensity);
}

float Voice::renderWoodwork()
{
  float ring = 0.0f;
  for (std::size_t index = 0; index < PARTIALS; ++index)
  {
    phase[index] = advancePhase(phase[index], frequency[index]);
    ring += std::sin(TWO_PI * phase[index]) * amplitude[index];
    amplitude[index] *= decay[index];
  }
  const float impact = filterA.process(noise.next()).band * excitation * 2.0f;
  excitation *= 0.9925f;
  return ring * 0.55f + impact;
}

float Voice::renderWhistle()
{
  // A pea whistle: a bright tone warbled at ~30 Hz by the rattling pea,
  // with a little breath noise around the pitch.
  lfoPhase = advancePhase(lfoPhase, 31.0f);
  const float trill = std::sin(TWO_PI * lfoPhase);
  const float hz = frequency[0] * (1.0f + 0.03f * trill);
  phase[0] = advancePhase(phase[0], hz);
  const float angle = TWO_PI * phase[0];
  const float tone = std::sin(angle) + 0.17f * std::sin(2.0f * angle) +
                     0.05f * std::sin(3.0f * angle);
  const float n = noise.next();
  const float breath =
      filterA.process(n).band * 0.35f + filterB.process(n).high * 0.012f;
  const float warble = 0.78f + 0.22f * std::sin(TWO_PI * lfoPhase + 1.3f);
  return (tone * 0.42f * warble + breath) * excitation;
}

float Voice::renderCrowdVowel(float firstFormant, float secondFormant)
{
  // Voiced excitation: a few detuned sawtooth "voices" following the pitch
  // contour, blended with breath noise, shaped by two formant filters.
  float voiced = 0.0f;
  for (std::size_t index = 0; index < PARTIALS; ++index)
  {
    phase[index] = advancePhase(phase[index], frequency[index] * glide);
    voiced += 2.0f * phase[index] - 1.0f;
  }
  voiced *= 1.0f / static_cast<float>(PARTIALS);
  const float left = toneLeft.process(noise.next()) * 1.6f + voiced * 0.9f;
  const float right =
      toneRight.process(noiseRight.next()) * 1.6f + voiced * 0.9f;
  excitationRight = filterC.process(right).band * firstFormant +
                    filterD.process(right).band * secondFormant;
  return filterA.process(left).band * firstFormant +
         filterB.process(left).band * secondFormant;
}

float Voice::renderCrowdWhistles()
{
  float sum = 0.0f;
  for (std::size_t index = 0; index < PARTIALS; ++index)
  {
    const float local = time - onset[index];
    if (local < 0.0f || local > decay[index]) continue;
    const float shape = std::sin(PI * local / decay[index]);
    const float vibrato =
        1.0f +
        0.025f * std::sin(TWO_PI * (5.0f + static_cast<float>(index)) * time);
    // Whistled pitches slide up a little as each one starts.
    const float slide = 0.94f + 0.06f * std::min(1.0f, local * 6.0f);
    phase[index] =
        advancePhase(phase[index], frequency[index] * vibrato * slide);
    sum += std::sin(TWO_PI * phase[index]) * amplitude[index] * shape;
  }
  return sum;
}

float Voice::renderApplause(float& right)
{
  // Claps are short noise bursts; their rate grows with the intensity until
  // they merge into the familiar dense patter.
  const float rate = 140.0f + 520.0f * command.intensity;
  const float chance = rate * SAMPLE_PERIOD;
  constexpr float CLAP_DECAY = 0.9935f;
  if (noise.unit() < chance)
    excitation = std::max(excitation, 0.35f + 0.65f * noise.unit());
  if (noiseRight.unit() < chance)
    excitationRight =
        std::max(excitationRight, 0.35f + 0.65f * noiseRight.unit());
  const float leftNoise = toneLeft.process(noise.next()) * 1.5f;
  const float rightNoise = toneRight.process(noiseRight.next()) * 1.5f;
  const float body = filterB.process(leftNoise + rightNoise).band * 0.25f;
  const float left = filterA.process(leftNoise * excitation).band * 2.2f;
  right = filterC.process(rightNoise * excitationRight).band * 2.2f + body;
  excitation *= CLAP_DECAY;
  excitationRight *= CLAP_DECAY;
  return left + body;
}

bool Voice::render(float* out, int frames)
{
  using enum Sound;
  if (!playing) return false;
  for (int frame = 0; frame < frames; ++frame)
  {
    if (delay > 0.0f)
    {
      delay -= SAMPLE_PERIOD;
      continue;
    }
    if (controlCounter == 0)
    {
      updateControl();
      if (isCrowdSound(command.sound))
        envelope = crowdEnvelope();
      else if (command.sound == WHISTLE)
        excitation = whistleEnvelope();
    }
    controlCounter = (controlCounter + 1) % CONTROL_BLOCK;

    float left = 0.0f;
    float right = 0.0f;
    switch (command.sound)
    {
      case KICK:
        right = renderKick();
        left = right;
        break;
      case NET:
        right = renderNet();
        left = right;
        break;
      case WOODWORK:
        right = renderWoodwork();
        left = right;
        break;
      case WHISTLE:
        right = renderWhistle();
        left = right;
        break;
      case CROWD_OOH:
        left = renderCrowdVowel(1.0f, 0.55f) * 0.9f * envelope;
        right = excitationRight * 0.9f * envelope;
        break;
      case CROWD_CHEER:
        left = renderCrowdVowel(0.9f, 0.7f) * 1.1f * envelope;
        right = excitationRight * 1.1f * envelope;
        break;
      case CROWD_GROAN:
        left = renderCrowdVowel(1.0f, 0.5f) * 0.85f * envelope;
        right = excitationRight * 0.85f * envelope;
        break;
      case CROWD_BOO:
        left = renderCrowdVowel(1.0f, 0.4f) * 0.8f * envelope;
        right = excitationRight * 0.8f * envelope;
        break;
      case CROWD_WHISTLES:
      {
        const float mono = renderCrowdWhistles() * envelope;
        left = mono;
        right = mono;
        break;
      }
      case CROWD_APPLAUSE:
        left = renderApplause(right) * envelope;
        right *= envelope;
        break;
      case COUNT:
        break;
    }
    out[2 * frame] += left * leftGain;
    out[2 * frame + 1] += right * rightGain;
    time += SAMPLE_PERIOD;
    if (time >= duration)
    {
      playing = false;
      return false;
    }
  }
  return true;
}

CrowdBed::Channel::Channel(std::uint32_t seed)
    : noise(seed), swell(seed * 3U + 1U, 0.12f), babble(seed * 7U + 5U, 2.4f)
{
  tone.set(2300.0f);
  rumble.set(210.0f, 0.6f);
  voices.set(700.0f, 0.8f);
  voicesHigh.set(1900.0f, 1.1f);
  air.set(5200.0f, 0.6f);
}

CrowdBed::CrowdBed() = default;

void CrowdBed::setTargets(float intensityValue, float levelValue)
{
  intensityTarget = std::clamp(intensityValue, 0.0f, 1.0f);
  levelTarget = std::clamp(levelValue, 0.0f, 1.0f);
}

void CrowdBed::updateControl()
{
  // Intensity follows in about a second; the level (pause fade) in ~0.4 s.
  intensity += (intensityTarget - intensity) * (CONTROL_SECONDS / 1.1f);
  level += (levelTarget - level) * (CONTROL_SECONDS / 0.4f);
  for (Channel* channel : {&left, &right})
  {
    channel->swell.step(CONTROL_SECONDS);
    channel->babble.step(CONTROL_SECONDS);
    channel->voices.set(620.0f + 420.0f * intensity, 0.8f);
    channel->voicesHigh.set(1700.0f + 900.0f * intensity, 1.1f);
  }
}

void CrowdBed::render(float* out, int frames, float busGain)
{
  for (int frame = 0; frame < frames; ++frame)
  {
    if (controlCounter == 0) updateControl();
    controlCounter = (controlCounter + 1) % CONTROL_BLOCK;
    const float gain = busGain * level * (0.45f + 0.55f * intensity);
    std::array<float, 2> sample{};
    std::size_t side = 0;
    for (Channel* channel : {&left, &right})
    {
      const float raw = channel->noise.next();
      const float n = channel->tone.process(raw) * 1.6f;
      const float rumble = channel->rumble.process(raw).low * 0.9f;
      const float swell = 0.55f + 0.9f * channel->swell.value();
      const float babble = 0.8f + 0.4f * channel->babble.value();
      const float voices =
          (channel->voices.process(n).band +
           channel->voicesHigh.process(n).band * (0.25f + 0.5f * intensity)) *
          swell * babble * (0.7f + 0.5f * intensity);
      const float air = channel->air.process(raw).high * 0.012f * intensity;
      sample[side++] = (rumble + voices + air) * gain * 0.32f * BED_LEVEL;
    }
    out[2 * frame] += sample[0];
    out[2 * frame + 1] += sample[1];
  }
}
}  // namespace Audio
