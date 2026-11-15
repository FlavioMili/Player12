// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "model/match_point.h"
#include "tools/match_sandbox_drill.h"

class Player;
struct StatsConfig;

/**
 * Training sessions (training ground plan section 23, T7): a squad and a
 * list of exercises; each exercise a set of zones on one 105 x 68 training
 * ground, each zone a drill on its own walled pitch with its own engine and
 * ball, placed on the ground (its centre, turned 0 or 90 degrees), with its
 * players from the squad. Players in no zone wait in the changing room.
 * The zones of an exercise step together at the fixed step.
 */
namespace TrainingGround
{
/** The ground the zones are laid out on (metres). */
inline constexpr float LENGTH_METRES = 105.0f;
inline constexpr float WIDTH_METRES = 68.0f;
/** Kept clear round each zone's walls: its coach stands 1 m out. */
inline constexpr float ZONE_MARGIN_METRES = 2.0f;
}  // namespace TrainingGround

/** One zone of an exercise. */
struct SessionZone
{
  std::string name;
  /** The drill, by its name (makeDrills), and settings by their names. */
  std::string drill;
  std::map<std::string, float> settings;
  /** Its pitch's centre on the ground (metres), and turned 90 degrees (its
   * length across the ground). */
  PitchPoint centre{};
  bool turned = false;
  /** Its players: squad indices, one per player the drill names, in the
   * drill's order. */
  std::vector<std::size_t> players;
};

struct SessionExercise
{
  std::string name;
  std::vector<SessionZone> zones;
};

struct SessionPlan
{
  std::string name;
  std::string summary;
  std::size_t squadSize = 0;
  std::vector<SessionExercise> exercises;
};

/** The sessions on offer. */
const std::vector<SessionPlan>& sessionPlans();

/** What is wrong with a plan, if anything: an unknown drill or setting, a
 * zone off the ground or overlapping another (with its margin), the wrong
 * number of players for its drill, a player in two zones or not in the
 * squad. */
std::optional<std::string> checkSessionPlan(const SessionPlan& plan);

/** A zone's footprint on the ground: its pitch, turned as placed. */
struct ZoneFootprint
{
  float minX = 0.0f;
  float minY = 0.0f;
  float maxX = 0.0f;
  float maxY = 0.0f;
};

/** A session being run: the squad's players, and the current exercise's
 * zones, each a drill run on the squad's own players. */
class SessionRun
{
 public:
  struct Zone
  {
    const SessionZone* plan = nullptr;
    std::unique_ptr<Drill> drill;
    std::unique_ptr<DrillRun> run;
    /** Its last report, once its run is over. */
    std::vector<std::string> report;
  };

  /** `seed`: the session's; each zone's drill runs on its own seed from it,
   * so the session repeats. `record`: attach each zone's recorder. */
  SessionRun(const SessionPlan& plan, const StatsConfig& config, std::uint32_t seed,
             bool record = false);
  ~SessionRun();
  SessionRun(const SessionRun&) = delete;
  SessionRun& operator=(const SessionRun&) = delete;

  /** One fixed step of every zone still running; when they are all over,
   * the next exercise starts. False once the last exercise is over. */
  bool step();
  [[nodiscard]] bool finished() const { return over; }
  [[nodiscard]] std::size_t exercise() const { return exerciseIndex; }
  [[nodiscard]] const SessionPlan& plan() const { return sessionPlan; }
  [[nodiscard]] const std::vector<Zone>& zones() const { return current; }
  [[nodiscard]] std::vector<Zone>& zones() { return current; }
  /** The squad's players (owned by the session). */
  [[nodiscard]] const std::vector<std::unique_ptr<Player>>& squad() const { return players; }
  /** Simulated seconds since the session began. */
  [[nodiscard]] double seconds() const { return elapsed; }
  /** Each finished exercise's zones' reports, in order. */
  [[nodiscard]] const std::vector<std::vector<std::string>>& history() const
  {
    return finishedReports;
  }

 private:
  void startExercise(std::size_t index);
  void finishExercise();

  SessionPlan sessionPlan;
  const StatsConfig& config;
  std::uint32_t seed = 0;
  bool record = false;
  std::vector<std::unique_ptr<Player>> players;
  std::vector<Zone> current;
  std::size_t exerciseIndex = 0;
  double elapsed = 0.0;
  bool over = false;
  std::vector<std::vector<std::string>> finishedReports;
};

/** A zone's footprint (its pitch from its drill and settings), turned. */
std::optional<ZoneFootprint> zoneFootprint(const SessionZone& zone);

/** A new drill by its name, its settings applied (none if unknown). */
std::unique_ptr<Drill> makeSessionDrill(const SessionZone& zone);

struct MatchRenderSnapshot;

/** The view of a session's ground (plan section 23, T7b): the ground as the
 * pitch, each running zone's players, coach, kit and ball placed on it, the
 * zones' walls and goals, and the other zones' balls. The caller sets the
 * interpolation. */
void fillSessionRenderSnapshot(const SessionRun& run, MatchRenderSnapshot& snapshot);
