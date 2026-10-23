// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <thread>

#include "audio/audio_mixer.h"

namespace Audio
{
/**
 * Plays a Mixer on the default output through an SDL audio stream (48 kHz
 * stereo float). SDL pulls audio from its own thread through the stream
 * callback, which renders straight from the mixer. Without an audio device
 * (or with SDL_AUDIODRIVER=dummy) everything still works; nothing is heard.
 * The mixer must outlive the device.
 *
 * The audio subsystem starts on the constructing (main) thread, as SDL asks;
 * the stream itself opens on a short-lived helper thread because connecting
 * to the sound server can take tens of milliseconds.
 */
class AudioDevice
{
 public:
  explicit AudioDevice(Mixer& mixer);
  ~AudioDevice();
  AudioDevice(const AudioDevice&) = delete;
  AudioDevice& operator=(const AudioDevice&) = delete;

  /** Waits for the device to finish opening; true when it plays. */
  bool isOpen();

 private:
  static void SDLCALL feed(void* userdata, SDL_AudioStream* stream,
                           int additionalAmount, int totalAmount);

  static constexpr int CHUNK_FRAMES = 512;

  void open();

  Mixer& mixer;
  std::atomic<SDL_AudioStream*> stream{nullptr};
  bool subsystemStarted = false;
  std::jthread opener;
  /** Callback scratch (audio thread only). */
  std::array<float, 2 * CHUNK_FRAMES> chunk{};
};
}  // namespace Audio
