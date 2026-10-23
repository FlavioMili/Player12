// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "audio/audio_mixer.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Audio
{
namespace
{
constexpr float LIMIT_KNEE = 0.8f;
/** Bus gains glide to new values over roughly this many samples. */
constexpr float GAIN_GLIDE = 1.0f / 2400.0f;
}  // namespace

float softLimit(float sample)
{
  const float magnitude = std::abs(sample);
  if (magnitude <= LIMIT_KNEE) return sample;
  constexpr float HEADROOM = 1.0f - LIMIT_KNEE;
  const float over = (magnitude - LIMIT_KNEE) / HEADROOM;
  const float limited = LIMIT_KNEE + HEADROOM * std::tanh(over);
  return std::copysign(std::min(limited, 0.999f), sample);
}

Mixer::Mixer() = default;

bool Mixer::trigger(const SoundCommand& command)
{
  const std::uint32_t head = commandHead.load(std::memory_order_relaxed);
  if (const std::uint32_t tail = commandTail.load(std::memory_order_acquire);
      head - tail >= COMMAND_CAPACITY)
    return false;
  commands[head % COMMAND_CAPACITY] = command;
  commandHead.store(head + 1, std::memory_order_release);
  ++triggered[static_cast<std::size_t>(std::to_underlying(command.sound))];
  return true;
}

void Mixer::setLevels(const BusLevels& levels)
{
  masterGain.store(std::clamp(levels.master, 0.0f, 1.0f));
  crowdGain.store(std::clamp(levels.crowd, 0.0f, 1.0f));
  effectsGain.store(std::clamp(levels.effects, 0.0f, 1.0f));
  muted.store(levels.muted);
}

void Mixer::setCrowd(float intensity, float level)
{
  crowdIntensity.store(std::clamp(intensity, 0.0f, 1.0f));
  crowdLevel.store(std::clamp(level, 0.0f, 1.0f));
}

void Mixer::startVoice(const SoundCommand& command)
{
  // A free voice, else steal the oldest one of the same bus.
  Voice* chosen = nullptr;
  float oldest = -1.0f;
  const bool crowd = isCrowdSound(command.sound);
  for (Voice& voice : voices)
  {
    if (!voice.active())
    {
      chosen = &voice;
      break;
    }
    if (isCrowdSound(voice.sound()) == crowd && voice.age() > oldest)
    {
      oldest = voice.age();
      chosen = &voice;
    }
  }
  if (chosen == nullptr) return;
  voiceSeed = voiceSeed * 1664525U + 1013904223U;
  chosen->start(command, voiceSeed);
}

void Mixer::render(float* out, int frames)
{
  std::uint32_t tail = commandTail.load(std::memory_order_relaxed);
  const std::uint32_t head = commandHead.load(std::memory_order_acquire);
  const bool silent = muted.load();
  while (tail != head)
  {
    if (!silent) startVoice(commands[tail % COMMAND_CAPACITY]);
    ++tail;
  }
  commandTail.store(tail, std::memory_order_release);

  std::fill(out, out + 2 * frames, 0.0f);
  if (silent)
  {
    // Muted: nothing is synthesised and pending sounds are dropped.
    for (Voice& voice : voices) voice.stop();
    activeVoiceCount.store(0);
    appliedMaster = 0.0f;
    return;
  }

  const float targetMaster = masterGain.load();
  const float targetCrowd = crowdGain.load();
  const float targetEffects = effectsGain.load();
  bed.setTargets(crowdIntensity.load(), crowdLevel.load());

  int active = 0;
  for (int offset = 0; offset < frames;)
  {
    const int block =
        std::min(frames - offset, static_cast<int>(scratch.size() / 2));
    float* destination = out + 2 * offset;
    // Glide the bus gains per block so volume changes never click.
    const float glide = std::min(1.0f, GAIN_GLIDE * static_cast<float>(block));
    appliedMaster += (targetMaster - appliedMaster) * glide;
    appliedCrowd += (targetCrowd - appliedCrowd) * glide;
    appliedEffects += (targetEffects - appliedEffects) * glide;

    bed.render(destination, block, appliedCrowd);
    active = 0;
    for (Voice& voice : voices)
    {
      if (!voice.active()) continue;
      std::fill(scratch.begin(), scratch.begin() + 2 * block, 0.0f);
      if (voice.render(scratch.data(), block)) ++active;
      const float bus =
          isCrowdSound(voice.sound()) ? appliedCrowd : appliedEffects;
      for (int index = 0; index < 2 * block; ++index)
        destination[index] += scratch[static_cast<std::size_t>(index)] * bus;
    }
    for (int index = 0; index < 2 * block; ++index)
      destination[index] = softLimit(destination[index] * appliedMaster);
    offset += block;
  }
  activeVoiceCount.store(active);
}
}  // namespace Audio
