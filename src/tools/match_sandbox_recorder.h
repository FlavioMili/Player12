// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "model/match_engine.h"
#include "model/match_recorder.h"

/**
 * The match sandbox's record of one match (debugger phase 1, the "Tier A"
 * log): what the planner, the players and the referee decided and what came
 * of it, in tick order. Only changes are kept: a player holding the same
 * intent for a minute is one entry.
 *
 * Hooks may run on the quick-result worker, so every access goes through
 * the recorder's lock (see lock()).
 */
class SandboxRecorder : public MatchRecorder
{
 public:
  /** A team-wide job of the off-ball planner. */
  enum class Job : std::uint8_t
  {
    NONE,
    /** The two players nearest the ball by arrival time, on either side:
     * pressers out of possession, receiver or ball-winner in it. */
    PRESSER_1,
    PRESSER_2,
    RUNNER_1,
    RUNNER_2,
    RUNNER_3,
    MIDFIELD_ARRIVAL,
    FAR_POST,
    OVERLAP,
    SUPPORTER_1,
    SUPPORTER_2,
    SUPPORTER_3,
    COVER_OUTLET
  };
  static constexpr std::size_t JOB_COUNT =
      static_cast<std::size_t>(Job::COVER_OUTLET) + 1;

  struct PhaseChange
  {
    bool homeTeam = true;
    TeamPhase from = TeamPhase::STOPPAGE;
    TeamPhase to = TeamPhase::STOPPAGE;
  };
  /** A job given to a different player (or left empty). */
  struct JobChange
  {
    bool homeTeam = true;
    Job job = Job::NONE;
    PlayerID player = 0;
    PlayerID previous = 0;
    /** Press arrival seconds or run priority; 0 for the other jobs. */
    float score = 0.0f;
  };
  struct IntentChange
  {
    PlayerID player = 0;
    bool homeTeam = true;
    PlayerIntent from = PlayerIntent::HOLD_SHAPE;
    PlayerIntent to = PlayerIntent::HOLD_SHAPE;
    PitchPoint target;
    /** His team-wide job when the intent changed, if any. */
    Job job = Job::NONE;
  };
  /** The ball changed hands (or became loose). */
  struct PossessionChange
  {
    PlayerID from = 0;
    PlayerID to = 0;
    bool fromHome = true;
    bool toHome = true;
    /** Log entry of the last action before the change, if any. */
    std::optional<std::size_t> afterAction;
  };
  struct KeeperChange
  {
    bool homeTeam = true;
    GoalkeeperState from = GoalkeeperState::SET_POSITION;
    GoalkeeperState to = GoalkeeperState::SET_POSITION;
  };
  /** A match event (shot, foul, card, offside, substitution…). */
  struct EventEntry
  {
    std::size_t index = 0;
  };

  using Payload =
      std::variant<PhaseChange, JobChange, IntentChange, MatchDecisionRecord,
                   MatchActionRecord, PossessionChange, KeeperChange,
                   EventEntry>;

  struct Entry
  {
    std::uint64_t tick = 0;
    /** Match clock in minutes when it was recorded. */
    float minute = 0.0f;
    Payload payload;
  };

  void onTeamPlan(const MatchEngine& engine,
                  const MatchTeamPlan& plan) override;
  void onDecision(const MatchEngine& engine,
                  const MatchDecisionRecord& decision) override;
  void onAction(const MatchEngine& engine,
                const MatchActionRecord& action) override;
  void onStepEnd(const MatchEngine& engine) override;

  /** Held while reading entries() and the other accessors. */
  [[nodiscard]] std::unique_lock<std::mutex> lock() const
  {
    return std::unique_lock(mutex);
  }
  [[nodiscard]] const std::vector<Entry>& entries() const { return log; }
  [[nodiscard]] const std::vector<MatchEvent>& events() const
  {
    return matchEvents;
  }
  /** Name of a player seen in the match. */
  [[nodiscard]] std::string playerName(PlayerID id) const;
  [[nodiscard]] const std::unordered_map<PlayerID, std::string>& knownPlayers()
      const
  {
    return names;
  }
  [[nodiscard]] std::uint64_t lastTick() const { return tick; }
  /** Rough memory held by the log, in bytes. */
  [[nodiscard]] std::size_t approximateBytes() const;

  // --- Rewind (debugger phase 2) -------------------------------------------

  /** A saved copy of the match, taken at the end of `tick`. */
  struct Keyframe
  {
    std::uint64_t tick = 0;
    std::shared_ptr<const MatchEngine> engine;
  };
  /** Ticks between saved copies (60 s of play). */
  static constexpr std::uint64_t KEYFRAME_TICKS = 600;

  /** Saved copies, oldest first (the first one after the first tick). */
  [[nodiscard]] const std::vector<Keyframe>& keyframes() const
  {
    return savedCopies;
  }
  /** The match's command and input logs so far (for continueReplay). */
  [[nodiscard]] const std::vector<MatchCommandRecord>& commands() const
  {
    return commandLog;
  }
  [[nodiscard]] const std::vector<MatchInputRecord>& inputs() const
  {
    return inputLog;
  }
  /** Fingerprint of the state at the end of `tick`, when it was recorded. */
  [[nodiscard]] std::optional<std::uint64_t> checksum(std::uint64_t at) const;
  /** Fingerprint of an engine's state: positions, ball, possession, score. */
  [[nodiscard]] static std::uint64_t stateChecksum(const MatchEngine& engine);
  /** Whether the match reached full time. */
  [[nodiscard]] bool finished() const { return fullTime; }

 private:

  void append(const MatchEngine& engine, Payload payload);
  void learnPlayers(const MatchEngine& engine);
  [[nodiscard]] Job jobOf(PlayerID player, bool homeTeam) const;

  mutable std::mutex mutex;
  std::vector<Entry> log;
  std::vector<MatchEvent> matchEvents;
  std::unordered_map<PlayerID, std::string> names;
  std::uint64_t tick = 0;

  // The last state seen, for change detection.
  bool started = false;
  std::array<TeamPhase, 2> phases{};
  std::array<GoalkeeperState, 2> keepers{};
  std::unordered_map<PlayerID, PlayerIntent> intents;
  /** Per side: who holds each job (index Job), from the latest plan. */
  std::array<std::array<PlayerID, JOB_COUNT>, 2> jobs{};
  const Player* possessor = nullptr;
  bool possessorHome = true;
  std::optional<std::size_t> lastAction;

  // Rewind.
  std::vector<Keyframe> savedCopies;
  std::vector<MatchCommandRecord> commandLog;
  std::vector<MatchInputRecord> inputLog;
  /** Per tick (index = tick); 0 for a tick not recorded. */
  std::vector<std::uint64_t> checksums;
  bool fullTime = false;
};
