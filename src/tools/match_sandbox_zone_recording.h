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
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "model/match_engine.h"
#include "tools/match_sandbox_drill.h"

class Player;
class SessionRun;
struct StatsConfig;

/**
 * One zone of a training session, saved (training ground plan section 23,
 * T7f): what it was built from (its drill, settings and seed, the engine
 * versions it ran, its players), where its players started (as carried in
 * from the change), the commands given after its start, and a fingerprint
 * of every tick. Replaying it rebuilds the zone alone on its own pitch on
 * the current engine, and shows where a changed engine first plays it
 * differently. The parallel session itself is not saved.
 */
struct ZoneRecording
{
  static constexpr int VERSION = 1;

  struct PlayerSpec
  {
    PlayerID id = 0;
    std::string name;
    PlayerRole role = PlayerRole::CM;
    std::map<std::string, float> stats;
    int height = 180;
    Foot foot = Foot::Right;
  };

  std::string created;
  std::string build;
  std::string session;
  std::string exercise;
  std::string zone;
  std::string drill;
  std::map<std::string, float> settings;
  std::uint32_t seed = 1;
  // The engine versions the zone ran (both sides, home then away).
  std::array<BrainId, 2> brains{BrainId::AARDVARK, BrainId::AARDVARK};
  std::array<BodyId, 2> bodies{BodyId::AARDVARK, BodyId::AARDVARK};
  HandshakeId handshake = HandshakeId::AARDVARK;
  RefereeId referee = RefereeId::AARDVARK;
  std::array<ManagerId, 2> managers{ManagerId::AARDVARK, ManagerId::AARDVARK};
  BallPhysicsId ball = BallPhysicsId::AARDVARK;
  OutOfPlayId outOfPlay = OutOfPlayId::AARDVARK;
  PossessionId possession = PossessionId::AARDVARK;
  float homeAdvantage = 0.0f;
  std::vector<PlayerSpec> players;
  std::vector<MatchEngine::CarriedPlayer> startStates;
  std::vector<MatchCommandRecord> commands;
  /** Fingerprint per tick (index = tick; 0 when not recorded). */
  std::vector<std::uint64_t> checksums;
  std::uint64_t lastTick = 0;
  int homeScore = 0;
  int awayScore = 0;
};

namespace ZoneRecordings
{
/** The zone `zoneIndex` of the session's current exercise, as far as it
 * has played (none, with `error` set, if it can't be saved). */
std::optional<ZoneRecording> capture(const SessionRun& run, std::size_t zoneIndex,
                                     std::string& error);
/** "2026-11-16 10-32 Small-sided rotation, Left 4 v 4 zone.json" */
std::string fileName(const ZoneRecording& recording);
bool save(const ZoneRecording& recording, const std::filesystem::path& path,
          std::string& error);
std::optional<ZoneRecording> load(const std::filesystem::path& path, std::string& error);
/** The zone recordings in the recordings folder, newest first. */
std::vector<std::filesystem::path> list();

/** A zone recording played again on the current engine: the zone alone,
 * its run recorded so it can be reviewed like a drill. */
struct Replay
{
  std::unique_ptr<Drill> drill;
  std::vector<std::unique_ptr<Player>> players;
  std::unique_ptr<DrillRun> run;
  /** First tick whose state differs from the recording, if any. */
  std::optional<std::uint64_t> divergedAt;
  std::string error;
  ~Replay();
};
/** Builds the zone and plays it to the recording's last tick. */
std::unique_ptr<Replay> replay(const ZoneRecording& recording, const StatsConfig& config);
}  // namespace ZoneRecordings

/** match_sandbox --zone-round-trip (T7f's check): runs the example session
 * headless, saves a zone of its first exercise (no carried states) and of
 * its second (carried in from the change), loads each and replays it; the
 * lines say whether each replays identically. 0 if all do. */
int runZoneRoundTrip(const StatsConfig& config);
