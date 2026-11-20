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
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "model/match_engine.h"
#include "model/match_recorder.h"
#include "tools/match_sandbox_detail.h"
#include "tools/match_sandbox_setup.h"

class GameController;
class SandboxRecorder;

/**
 * A saved sandbox match (debugger phase 5): what it was built from, the
 * manager's and controller's inputs, a fingerprint of every tick, every AI
 * decision and (optionally) every decision's full breakdown. Replaying it
 * on the current engine rebuilds the match for review, and shows where a
 * changed engine or tuning first plays it differently.
 *
 * A recording replays exactly only on the build that recorded it: a change
 * to the engine or its tuning makes the match diverge, which is what the
 * comparison is for.
 */
struct MatchRecording
{
  // 3: points and the ball height in metres (training ground T1 stage 2b);
  // 4: facing angles are true angles (stage 3b). Older recordings are refused.
  static constexpr int VERSION = 4;

  struct Decision
  {
    std::uint64_t tick = 0;
    MatchDecisionRecord record;
  };

  std::string created;
  std::string build;
  MatchSetup setup;
  std::vector<MatchCommandRecord> commands;
  std::vector<MatchInputRecord> inputs;
  /** Fingerprint per tick (index = tick; 0 when not recorded). */
  std::vector<std::uint64_t> checksums;
  std::uint64_t lastTick = 0;
  int homeScore = 0;
  int awayScore = 0;
  std::vector<Decision> decisions;
  /** Every decision's breakdown, when saved with full detail. */
  std::vector<DetailRecorder::At<MatchDecisionDetail>> details;
  /** Owns the term names of loaded details (ScoreTerm keeps pointers). */
  std::shared_ptr<std::unordered_set<std::string>> names =
      std::make_shared<std::unordered_set<std::string>>();
};

namespace Recordings
{
/** Where recordings are kept: Documents/Player12 match recordings. */
std::filesystem::path folder();

/** A finished (or stopped) match as a recording. With `withDetail` the
 * whole match is rebuilt once to break every decision down. */
MatchRecording capture(const MatchSetup& setup, const SandboxRecorder& match,
                       bool withDetail);

/** "2026-10-09 14-32 Milan v Verona (seed 123).json" */
std::string fileName(const MatchRecording& recording);

bool save(const MatchRecording& recording, const std::filesystem::path& path,
          std::string& error);
std::optional<MatchRecording> load(const std::filesystem::path& path,
                                   std::string& error);

/** A recording played again on the current engine. */
struct Replay
{
  /** The replayed match, reviewable like a live one (null on error). */
  std::unique_ptr<SandboxRecorder> match;
  /** First tick whose state differs from the recording, if any. */
  std::optional<std::uint64_t> divergedAt;
  /** First recorded decision the replay does not make identically (index
   * into the recording's decisions), and what the replay did instead. */
  std::optional<std::size_t> firstDifferentDecision;
  std::optional<MatchRecording::Decision> replayedDecision;
  int homeScore = 0;
  int awayScore = 0;
  std::string error;
};
/** @param brains Play it with these brains (home, away) instead of the
 * recorded ones; the comparison then shows where the brains differ.
 * @param bodies Likewise for the players' bodies.
 * @param handshake Likewise for the match's handshake. */
Replay replay(const MatchRecording& recording, const GameController& controller,
              std::optional<std::array<BrainId, 2>> brains = {},
              std::optional<std::array<BodyId, 2>> bodies = {},
              std::optional<HandshakeId> handshake = {},
              std::optional<PossessionId> possession = {});
/** A command as a match recording saves it, and back (a session zone's
 * recording saves its commands the same way; training ground T7f). */
nlohmann::json commandToJson(const MatchCommandRecord& command);
MatchCommandRecord commandFromJsonValue(const nlohmann::json& value);
}  // namespace Recordings
