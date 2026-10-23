// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "audio/audio_device.h"

#include <algorithm>
#include <format>

#include "global/logger.h"

namespace Audio
{
AudioDevice::AudioDevice(Mixer& mixerToPlay) : mixer(mixerToPlay)
{
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
  {
    Logger::info(std::format("Match audio disabled: {}", SDL_GetError()));
    return;
  }
  subsystemStarted = true;
  opener = std::jthread([this] { open(); });
}

void AudioDevice::open()
{
  const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, SAMPLE_RATE};
  SDL_AudioStream* opened = SDL_OpenAudioDeviceStream(
      SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioDevice::feed, this);
  if (opened == nullptr)
  {
    Logger::info(std::format("Match audio disabled: {}", SDL_GetError()));
    return;
  }
  // Streams open paused; start pulling from the mixer.
  SDL_ResumeAudioStreamDevice(opened);
  stream.store(opened);
}

bool AudioDevice::isOpen()
{
  if (opener.joinable()) opener.join();
  return stream.load() != nullptr;
}

AudioDevice::~AudioDevice()
{
  if (opener.joinable()) opener.join();
  // Destroying the stream waits for a running callback, so the mixer is not
  // touched afterwards.
  if (SDL_AudioStream* opened = stream.load()) SDL_DestroyAudioStream(opened);
  if (subsystemStarted) SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SDLCALL AudioDevice::feed(void* userdata, SDL_AudioStream* stream,
                               int additionalAmount, int /*totalAmount*/)
{
  auto* device = static_cast<AudioDevice*>(userdata);
  constexpr int FRAME_BYTES = 2 * static_cast<int>(sizeof(float));
  int frames = (additionalAmount + FRAME_BYTES - 1) / FRAME_BYTES;
  while (frames > 0)
  {
    const int block = std::min(frames, CHUNK_FRAMES);
    device->mixer.render(device->chunk.data(), block);
    SDL_PutAudioStreamData(stream, device->chunk.data(), block * FRAME_BYTES);
    frames -= block;
  }
}
}  // namespace Audio
