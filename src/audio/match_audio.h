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
#include <memory>
#include <span>
#include <vector>

#include "audio/audio_mixer.h"
#include "model/match_events.h"

class MatchEngine;
struct Settings;

namespace Audio
{
class AudioDevice;
}

/**
 * What the live view shows this frame, copied from the engine on the UI
 * thread. The audio never sees the engine itself.
 */
struct MatchAudioFrame
{
  float ballX = 0.5f;
  /** Ball speed in m/s. */
  float ballSpeed = 0.0f;
  bool ballIsShot = false;
  /** Nobody has the ball at their feet. */
  bool ballLoose = false;
  /** Last player to kick or deflect the ball (0: none). */
  PlayerID kickerId = 0;
  bool homeInPossession = false;
  bool awayInPossession = false;
  float matchMinute = 0.0f;
  int period = 1;
  int homeScore = 0;
  int awayScore = 0;
  /** Break or full time: the crowd settles. */
  bool stopped = false;
  /** Simulated seconds per wall second. */
  float playbackSpeed = 1.0f;
  bool paused = false;
  /**
   * The match jumped ahead this frame (highlight skip, quick result):
   * events that arrived with the jump are consumed silently.
   */
  bool skipped = false;
};

/** Copies the frame state from a live engine (UI thread only). */
MatchAudioFrame captureMatchAudioFrame(const MatchEngine& engine,
                                       float playbackSpeed, bool paused,
                                       bool skipped);

/**
 * Remembers which match events were already heard. Events carry no id and
 * the engine's log drops its oldest entries once full, so an event is known
 * by its place in match time (period, clock) plus a fingerprint of its
 * content. That identity survives log eviction, a replayed or re-created
 * engine and seeking: an event is reported once, and never again.
 */
class MatchEventCursor
{
 public:
  /** Appends events not heard yet (in log order) and marks them heard. */
  void collect(std::span<const MatchEvent> events,
               std::vector<const MatchEvent*>& fresh);
  /** Marks every event in `events` heard without reporting it. */
  void skipAll(std::span<const MatchEvent> events);

 private:
  static constexpr std::size_t SAME_TIME_CAPACITY = 16;

  /** Strictly earlier in match time than the last heard event. */
  bool isBefore(const MatchEvent& event) const;
  bool isNew(const MatchEvent& event, std::uint64_t fingerprint) const;
  void mark(const MatchEvent& event, std::uint64_t fingerprint);

  int period = 0;
  float minute = -1.0f;
  /** Fingerprints of heard events at exactly (period, minute). */
  std::array<std::uint64_t, SAME_TIME_CAPACITY> sameTime{};
  std::size_t sameTimeCount = 0;
};

/**
 * Match audio: turns the live match into sound. Each frame the scene passes
 * a MatchAudioFrame and the engine's event log; new events become crowd
 * reactions, whistles and ball sounds (from the home crowd's point of view),
 * ball launches become kicks panned by the ball's position, and the crowd
 * bed follows the danger on the pitch. Above 4x playback the minor sounds
 * are thinned out; a pause fades the crowd down. Purely presentational: it
 * only reads copies and never feeds anything back into the simulation.
 */
class MatchAudio
{
 public:
  /** `openDevice` false keeps the mixer silent-to-the-world (tests). */
  explicit MatchAudio(bool openDevice = true);
  ~MatchAudio();
  MatchAudio(const MatchAudio&) = delete;
  MatchAudio& operator=(const MatchAudio&) = delete;

  /** Master/crowd/effects volumes and mute from the settings. */
  static Audio::BusLevels levelsFrom(const Settings& settings);
  void setLevels(const Audio::BusLevels& levels);

  void update(float wallSeconds, const MatchAudioFrame& frame,
              std::span<const MatchEvent> events);

  Audio::Mixer& mixer() { return *mixerInstance; }
  /** Waits for the output device to open; true when it plays. */
  bool deviceOpen();

 private:
  /** Which sounds survive fast playback. */
  enum class Priority : std::uint8_t
  {
    MINOR,
    MAJOR,
    KEY
  };

  struct PendingShot
  {
    bool active = false;
    bool home = false;
    float xg = 0.0f;
    int period = 1;
    float minute = 0.0f;
  };

  void react(const MatchEvent& event);
  void reactToGoal(const MatchEvent& event);
  void reactToMiss(float x);
  /** `replaying`: a moment already heard (seek back); history only. */
  void detectKicks(const MatchAudioFrame& frame, bool replaying);
  void updateCrowd(float wallSeconds, const MatchAudioFrame& frame);
  bool allowed(Priority priority) const;
  void play(Priority priority, Audio::SoundCommand command);
  void whistle(Priority priority, Audio::WhistlePattern pattern, float x);

  std::unique_ptr<Audio::Mixer> mixerInstance =
      std::make_unique<Audio::Mixer>();
  /** Declared after the mixer so it stops pulling audio first. */
  std::unique_ptr<Audio::AudioDevice> device;
  MatchEventCursor cursor;
  std::vector<const MatchEvent*> freshEvents;

  float playbackSpeed = 1.0f;
  float previousBallSpeed = 0.0f;
  bool previousShot = false;
  bool previousLoose = false;
  PlayerID previousKicker = 0;
  bool haveBallHistory = false;
  float kickCooldown = 0.0f;
  /** Seconds since the last crowd reaction and its priority. */
  float sinceReaction = 10.0f;
  Priority lastReaction = Priority::MINOR;
  /** Short-lived excitement on top of the crowd bed (can dip below 0). */
  float tension = 0.0f;
  PendingShot pendingShot;
  /** Latest match moment heard, so a replay of it stays quiet. */
  int latestPeriod = 0;
  float latestMinute = -1.0f;
};
