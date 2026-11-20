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

#include "model/match_body.h"
#include "model/match_brain.h"
#include "model/match_engine.h"
#include "model/match_point.h"
#include "model/match_tuning.h"
#include "tools/match_sandbox_drill.h"
#include "tools/match_sandbox_training.h"

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
/** The ground the zones are laid out on (metres): match-sized, and it must
 * stay so, as the views still frame and mirror everything by the match
 * pitch's size. */
inline constexpr float LENGTH_METRES = MatchTuning::Pitch::MATCH_LENGTH_METRES;
inline constexpr float WIDTH_METRES = MatchTuning::Pitch::MATCH_WIDTH_METRES;
/** Kept clear round each zone's walls: its coach stands 1 m out. */
inline constexpr float ZONE_MARGIN_METRES = 2.0F;
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
  /** The brain and body both its sides use (T7e); none: the sandbox's
   * Set-up. */
  std::optional<BrainId> brain;
  std::optional<BodyId> body;
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
  float minX = 0.0F;
  float minY = 0.0F;
  float maxX = 0.0F;
  float maxY = 0.0F;
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
    /** Each player's last carried-in state (none: he started where the
     * drill put him): a saved zone starts from these (T7f). */
    std::map<PlayerID, MatchEngine::CarriedPlayer> startStates;
    /** Its last report, once its run is over. */
    std::vector<std::string> report;
  };

  /** `picks`: the players to train (the first of them make up the squad;
   * reference players fill any gap). `seed`: the session's; each zone's drill
   * runs on its own seed from it, so the session repeats. `record`: attach each
   * zone's recorder. Throws std::invalid_argument, with checkSessionPlan's
   * message, if the plan has a problem; so each exercise's zones are the
   * plan's, one for one and in order. */
  SessionRun(const SessionPlan& plan, const StatsConfig& config,
             std::uint32_t seed, bool record = false,
             const std::vector<TrainingPlayer>& picks = {});
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
  [[nodiscard]] const std::vector<std::unique_ptr<Player>>& squad() const
  {
    return players;
  }
  /** Simulated seconds since the session began. */
  [[nodiscard]] double seconds() const { return elapsed; }
  /** A squad player leaves his zone for the changing room now (taken off
   * at once; the zone plays on a man short). He plays again in the next
   * exercise that names him. False if he isn't playing now. */
  bool sendToChangingRoom(std::size_t squadIndex);
  /** Squad players in the changing room this exercise: sent there, or in
   * no zone. */
  [[nodiscard]] std::vector<std::size_t> changingRoom() const;
  /** Each finished exercise's zones' reports, in order. */
  [[nodiscard]] const std::vector<std::vector<std::string>>& history() const
  {
    return finishedReports;
  }
  /** Between exercises (T7d): everyone who was playing in one engine over
   * the whole ground, walking to his place in the next exercise. */
  [[nodiscard]] bool changing() const { return regroup != nullptr; }
  /** The change's engine, while changing. */
  [[nodiscard]] const DrillRun* regroupRun() const { return regroup.get(); }

  /** Change of exercise (plan section 23, T7d): the change ends when
   * everyone is this close to his place, or after the time limit. Our own
   * estimates. */
  static constexpr float ARRIVED_METRES = 1.0F;
  static constexpr double CHANGE_LIMIT_SECONDS = 60.0;

 private:
  /** Someone walking to his next place in a change: his squad index, the
   * zone he comes from (none: he joins from the changing room) and where
   * he is going on the ground, and the zone he is going to (none: the
   * changing room). */
  struct Mover
  {
    std::size_t squadIndex = 0;
    PitchPoint target{};
    std::optional<std::size_t> toZone;
  };

  /** The zones of an exercise, their runs started (players at the drill's
   * places, with their carried condition). */
  std::vector<Zone> buildExercise(std::size_t index);
  void finishExercise();
  /** Tidy up and merge: the next exercise's zones are made ready, and the
   * players who were playing walk to their places in one engine. */
  void beginChange(std::size_t next);
  /** The next zone's starting place, or the next changing-room spot. */
  [[nodiscard]] Mover nextPlace(std::size_t squadIndex,
                                const PitchPoint& benchPlace) const;
  /** Split: the next exercise's zones take their players as they arrived. */
  void finishChange();

  std::vector<Zone> upcoming;
  std::size_t upcomingIndex = 0;
  std::vector<Mover> movers;
  std::unique_ptr<Drill> regroupDrill;
  std::unique_ptr<DrillRun> regroup;
  double changeStart = 0.0;
  /** How the last change went, reported with the exercise it led into. */
  std::string changeReport;
  /** The largest jump of place and condition at the last merge. */
  float mergeJump = 0.0F;
  float mergeConditionJump = 0.0F;

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
  /** Each squad player's running state when he last left a zone (an
   * exercise's end, or the changing room) or a change: his condition
   * carries into the next (T7c). Always on the ground (its metres, its
   * facing), never a zone's own. */
  std::map<PlayerID, MatchEngine::CarriedPlayer> carried;
  /** Squad players sent to the changing room this exercise. */
  std::vector<std::size_t> resting;
};

/** A zone's footprint (its pitch from its drill and settings), turned. */
std::optional<ZoneFootprint> zoneFootprint(const SessionZone& zone);

/** A new drill by its name, its settings applied (none if unknown). */
std::unique_ptr<Drill> makeSessionDrill(const SessionZone& zone);

struct MatchRenderSnapshot;

/** The view of a session's ground (plan section 23, T7b): the ground as the
 * pitch, each running zone's players, coach, kit and ball placed on it, the
 * zones' walls and goals, and the other zones' balls; `watchedZone`'s ball is
 * the main one (the 3D cameras follow it). The caller sets the
 * interpolation. */
void fillSessionRenderSnapshot(const SessionRun& run,
                               MatchRenderSnapshot& snapshot,
                               std::size_t watchedZone = 0);

/** The carry-over check (plan section 23, T7c): a small-sided game runs 30
 * s; a second engine starts from its players' running states; the lines
 * say how exactly the states carried and how the first step after compares
 * with the original's. */
std::vector<std::string> runCarryCheck(const StatsConfig& config);
