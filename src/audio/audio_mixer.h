// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <utility>

#include "audio/synth.h"

namespace Audio
{
/** Volume buses; every gain is linear in [0, 1]. */
struct BusLevels
{
  float master = 0.8f;
  float crowd = 0.8f;
  float effects = 0.8f;
  bool muted = false;
};

/**
 * Real-time stereo mixer (48 kHz, interleaved float): the crowd bed plus a
 * fixed pool of one-shot voices, routed through crowd and effects buses into
 * the master bus and a soft limiter, so output never leaves [-1, 1].
 *
 * Threading: the control methods (trigger, setLevels, setCrowd) are called
 * from one producer thread (the UI thread) and render() from the audio
 * thread. They only exchange plain data through atomics and a single-
 * producer/single-consumer command ring; render() never allocates or locks.
 */
class Mixer
{
 public:
  static constexpr int MAX_VOICES = 24;
  static constexpr std::size_t COMMAND_CAPACITY = 64;

  Mixer();

  /** Queues a sound; false when the command ring is full. */
  bool trigger(const SoundCommand& command);
  void setLevels(const BusLevels& levels);
  /** Crowd bed intensity (0..1) and level (0..1, lowered while paused). */
  void setCrowd(float intensity, float level);

  /** Mixes `frames` stereo frames into `out` (overwrites it). */
  void render(float* out, int frames);

  /** Voices playing after the last render() (any thread). */
  int activeVoices() const { return activeVoiceCount.load(); }
  /** Sounds accepted by trigger() so far, per kind (producer thread). */
  std::uint64_t triggeredCount(Sound sound) const
  {
    return triggered[static_cast<std::size_t>(std::to_underlying(sound))];
  }

 private:
  void startVoice(const SoundCommand& command);

  // Producer -> audio thread.
  std::array<SoundCommand, COMMAND_CAPACITY> commands{};
  std::atomic<std::uint32_t> commandHead{0};
  std::atomic<std::uint32_t> commandTail{0};
  std::atomic<float> masterGain{0.8f};
  std::atomic<float> crowdGain{0.8f};
  std::atomic<float> effectsGain{0.8f};
  std::atomic<bool> muted{false};
  std::atomic<float> crowdIntensity{0.3f};
  std::atomic<float> crowdLevel{1.0f};
  std::atomic<int> activeVoiceCount{0};
  std::array<std::uint64_t, SOUND_COUNT> triggered{};

  // Audio thread only.
  std::array<Voice, MAX_VOICES> voices{};
  CrowdBed bed;
  std::array<float, 2 * 256> scratch{};
  std::uint32_t voiceSeed = 0x5EED1234U;
  float appliedMaster = 0.0f;
  float appliedCrowd = 0.0f;
  float appliedEffects = 0.0f;
};

/** Soft limiter: linear below 0.8, then a smooth knee that never reaches 1. */
float softLimit(float sample);
}  // namespace Audio
