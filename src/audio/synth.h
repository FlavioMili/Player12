// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>
#include <utility>

/**
 * Procedural sound synthesis for the match audio. Every sound is built from
 * noise, oscillators and filters at run time: there are no sample files.
 *
 * Nothing here allocates or locks, so it is safe on the audio thread. The
 * noise generators are private xorshift states, independent of the game's
 * seeded RNG, so audio can never influence a simulation.
 */
namespace Audio
{
inline constexpr int SAMPLE_RATE = 48000;
inline constexpr float SAMPLE_RATE_F = 48000.0f;
inline constexpr float SAMPLE_PERIOD = 1.0f / SAMPLE_RATE_F;

/** Tiny deterministic white-noise source (xorshift32) in [-1, 1). */
class Noise
{
 public:
  explicit Noise(std::uint32_t seed = 0x9E3779B9U) : state(seed | 1U) {}
  float next()
  {
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return static_cast<float>(static_cast<std::int32_t>(state)) *
           (1.0f / 2147483648.0f);
  }
  /** Uniform value in [0, 1). */
  float unit() { return 0.5f * (next() + 1.0f); }
  void reseed(std::uint32_t seed) { state = seed | 1U; }

 private:
  std::uint32_t state;
};

/** Topology-preserving state-variable filter (low/band/high outputs). */
class Svf
{
 public:
  struct Output
  {
    float low = 0.0f;
    float band = 0.0f;
    float high = 0.0f;
  };

  void set(float cutoffHz, float q);
  Output process(float input)
  {
    const float v3 = input - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    return {v2, v1, input - k * v1 - v2};
  }
  void reset()
  {
    ic2 = 0.0f;
    ic1 = 0.0f;
  }

 private:
  float ic1 = 0.0f;
  float ic2 = 0.0f;
  float k = 1.0f;
  float a1 = 0.0f;
  float a2 = 0.0f;
  float a3 = 0.0f;
};

/** One-pole low-pass: darkens noise before it is shaped. */
class OnePole
{
 public:
  void set(float cutoffHz);
  float process(float input)
  {
    state += coefficient * (input - state);
    return state;
  }
  void reset() { state = 0.0f; }

 private:
  float state = 0.0f;
  float coefficient = 1.0f;
};

/** Two cascaded one-pole low-passes (12 dB per octave). */
class Darken
{
 public:
  void set(float cutoffHz)
  {
    first.set(cutoffHz);
    second.set(cutoffHz);
  }
  float process(float input) { return second.process(first.process(input)); }
  void reset()
  {
    first.reset();
    second.reset();
  }

 private:
  OnePole first;
  OnePole second;
};

/** Slow bounded random walk in [0, 1] used for swells and murmurs. */
class Drift
{
 public:
  explicit Drift(std::uint32_t seed, float ratePerSecond)
      : noise(seed), rate(ratePerSecond)
  {
  }
  /** Advances by `seconds` (call at control rate). */
  float step(float seconds);
  float value() const { return current; }

 private:
  Noise noise;
  float rate;
  float current = 0.5f;
  float target = 0.5f;
};

/** Every one-shot sound the match audio can play. */
enum class Sound : std::uint8_t
{
  KICK,
  NET,
  WOODWORK,
  WHISTLE,
  CROWD_OOH,
  CROWD_CHEER,
  CROWD_GROAN,
  CROWD_BOO,
  CROWD_WHISTLES,
  CROWD_APPLAUSE,
  COUNT
};
inline constexpr std::size_t SOUND_COUNT =
    static_cast<std::size_t>(std::to_underlying(Sound::COUNT));

/** Referee whistle patterns (Voice variant for Sound::WHISTLE). */
enum class WhistlePattern : std::uint8_t
{
  SHORT,
  KICK_OFF,
  HALF_TIME,
  FULL_TIME
};

/** True for crowd sounds (crowd bus); the rest go to the effects bus. */
bool isCrowdSound(Sound sound);

/** A request to play a sound; plain data, copied to the audio thread. */
struct SoundCommand
{
  Sound sound = Sound::KICK;
  /** Linear gain before the bus gains. */
  float gain = 1.0f;
  /** -1 left .. +1 right. */
  float pan = 0.0f;
  /** 0..1: strength (kick power, reaction size); shapes timbre and length. */
  float intensity = 0.5f;
  /** Sound-specific variant (WhistlePattern for the whistle). */
  std::uint8_t variant = 0;
  /** Seconds to wait before the sound starts. */
  float delay = 0.0f;
};

/**
 * One playing sound. start() prepares it from a command; render() adds its
 * stereo output to `out` and returns false once finished. All state lives
 * inline so a fixed pool of voices never allocates.
 */
class Voice
{
 public:
  void start(const SoundCommand& command, std::uint32_t seed);
  bool render(float* out, int frames);
  void stop() { playing = false; }
  bool active() const { return playing; }
  Sound sound() const { return command.sound; }
  /** Seconds played so far (for voice stealing). */
  float age() const { return time; }

 private:
  static constexpr int PARTIALS = 6;

  float renderKick();
  float renderNet();
  float renderWoodwork();
  float renderWhistle();
  float renderCrowdVowel(float firstFormant, float secondFormant);
  float renderCrowdWhistles();
  float renderApplause(float& right);
  /** Crowd envelope: attack, hold scaled by intensity, release. */
  float crowdEnvelope() const;
  /** 0..1 envelope of the whistle pattern at the current time. */
  float whistleEnvelope() const;
  void updateControl();

  SoundCommand command;
  bool playing = false;
  float time = 0.0f;
  float duration = 0.0f;
  float delay = 0.0f;
  float leftGain = 0.0f;
  float rightGain = 0.0f;
  int controlCounter = 0;

  Noise noise;
  Noise noiseRight{0x1234567U};
  Svf filterA;
  Svf filterB;
  Svf filterC;
  Svf filterD;
  Darken toneLeft;
  Darken toneRight;
  std::array<float, PARTIALS> phase{};
  std::array<float, PARTIALS> frequency{};
  std::array<float, PARTIALS> amplitude{};
  std::array<float, PARTIALS> decay{};
  std::array<float, PARTIALS> onset{};
  float lfoPhase = 0.0f;
  float glide = 0.0f;
  float excitation = 0.0f;
  float excitationRight = 0.0f;
  /** Crowd envelope, refreshed at control rate. */
  float envelope = 0.0f;
  float attack = 0.1f;
  float hold = 0.5f;
  float release = 1.0f;
};

/**
 * The continuous stadium ambience: layered filtered noise (rumble, voices,
 * air) with slow independent swells per channel. `intensity` (0..1) raises
 * level and brightness; `level` is the overall gain (fades on pause).
 */
class CrowdBed
{
 public:
  CrowdBed();
  void setTargets(float intensity, float level);
  /** Adds stereo output to `out`, already scaled by `busGain`. */
  void render(float* out, int frames, float busGain);

 private:
  struct Channel
  {
    Noise noise;
    Darken tone;
    Svf rumble;
    Svf voices;
    Svf voicesHigh;
    Svf air;
    Drift swell;
    Drift babble;
    explicit Channel(std::uint32_t seed);
  };
  void updateControl();

  Channel left{0xC0FFEEU};
  Channel right{0xBADA55U};
  float intensity = 0.3f;
  float intensityTarget = 0.3f;
  float level = 0.0f;
  float levelTarget = 1.0f;
  int controlCounter = 0;
};
}  // namespace Audio
