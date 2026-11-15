// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_session.h"

#include <algorithm>
#include <format>
#include <set>

#include "model/match_engine.h"
#include "model/player.h"

namespace
{
/** The squad's ids, apart from the drill runs' reference players. */
constexpr PlayerID FIRST_SQUAD_ID = 800'000;

/** The example session (plan section 23, point 9; our own layout): 16
 * players, two 4 v 4 small-sided games side by side, then four 2 v 2, then
 * one 6 v 6 with four resting in the changing room. */
SessionPlan smallSidedRotation()
{
  SessionPlan plan;
  plan.name = "Small-sided rotation";
  plan.summary =
      "Sixteen players: two 4 v 4 small-sided games side by side, then four "
      "2 v 2 games, then one 6 v 6 with four resting. Two minutes each "
      "(training ground T7).";
  plan.squadSize = 16;
  const auto game = [](std::string name, int rung, int nASide, PitchPoint centre,
                       std::vector<std::size_t> players)
  {
    SessionZone zone;
    zone.name = std::move(name);
    zone.drill = "Small-sided game";
    zone.settings = {{"Rung", static_cast<float>(rung)},
                     {"N a side (N v N)", static_cast<float>(nASide)},
                     {"Seconds", 120.0f}};
    zone.centre = centre;
    zone.players = std::move(players);
    return zone;
  };
  // The drill names its players home, away, home, away...: a zone's list
  // alternates the sides.
  SessionExercise fours{"Two 4 v 4", {}};
  fours.zones.push_back(game("Left 4 v 4", 4, 4, PitchPoint{27.0f, 34.0f}, {0, 1, 2, 3, 4, 5, 6, 7}));
  fours.zones.push_back(
      game("Right 4 v 4", 4, 4, PitchPoint{78.0f, 34.0f}, {8, 9, 10, 11, 12, 13, 14, 15}));
  SessionExercise twos{"Four 2 v 2", {}};
  twos.zones.push_back(game("Top left 2 v 2", 2, 0, PitchPoint{26.0f, 17.0f}, {0, 8, 1, 9}));
  twos.zones.push_back(game("Top right 2 v 2", 2, 0, PitchPoint{79.0f, 17.0f}, {2, 10, 3, 11}));
  twos.zones.push_back(game("Bottom left 2 v 2", 2, 0, PitchPoint{26.0f, 51.0f}, {4, 12, 5, 13}));
  twos.zones.push_back(game("Bottom right 2 v 2", 2, 0, PitchPoint{79.0f, 51.0f}, {6, 14, 7, 15}));
  SessionExercise sixes{"6 v 6", {}};
  sixes.zones.push_back(game("6 v 6", 4, 6, PitchPoint{52.5f, 34.0f},
                             {0, 2, 1, 3, 4, 6, 5, 7, 8, 10, 9, 11}));
  plan.exercises = {std::move(fours), std::move(twos), std::move(sixes)};
  return plan;
}
}  // namespace

const std::vector<SessionPlan>& sessionPlans()
{
  static const std::vector<SessionPlan> plans{smallSidedRotation()};
  return plans;
}

std::unique_ptr<Drill> makeSessionDrill(const SessionZone& zone)
{
  for (std::unique_ptr<Drill>& drill : makeDrills())
  {
    if (zone.drill != drill->name()) continue;
    for (const DrillParameter& parameter : drill->parameters())
      if (parameter.value)
        if (const auto found = zone.settings.find(parameter.name);
            found != zone.settings.end())
          *parameter.value = found->second;
    return std::move(drill);
  }
  return nullptr;
}

std::optional<ZoneFootprint> zoneFootprint(const SessionZone& zone)
{
  const std::unique_ptr<Drill> drill = makeSessionDrill(zone);
  if (!drill) return std::nullopt;
  const MatchPitch pitch = drill->pitch();
  const float along = zone.turned ? pitch.width : pitch.length;
  const float across = zone.turned ? pitch.length : pitch.width;
  return ZoneFootprint{zone.centre.x - 0.5f * along, zone.centre.y - 0.5f * across,
                       zone.centre.x + 0.5f * along, zone.centre.y + 0.5f * across};
}

std::optional<std::string> checkSessionPlan(const SessionPlan& plan)
{
  using namespace TrainingGround;
  constexpr float M = ZONE_MARGIN_METRES;
  for (const SessionExercise& exercise : plan.exercises)
  {
    std::vector<ZoneFootprint> placed;
    std::set<std::size_t> taken;
    for (const SessionZone& zone : exercise.zones)
    {
      const std::unique_ptr<Drill> drill = makeSessionDrill(zone);
      if (!drill) return std::format("{}: no drill called \"{}\"", zone.name, zone.drill);
      for (const auto& [name, value] : zone.settings)
      {
        const std::vector<DrillParameter> parameters = drill->parameters();
        if (std::ranges::none_of(parameters, [&](const DrillParameter& parameter)
                                 { return name == parameter.name; }))
          return std::format("{}: {} has no setting \"{}\"", zone.name, zone.drill, name);
      }
      if (drill->players().size() != zone.players.size())
        return std::format("{}: {} names {} players, the zone has {}", zone.name,
                           zone.drill, drill->players().size(), zone.players.size());
      for (const std::size_t player : zone.players)
      {
        if (player >= plan.squadSize)
          return std::format("{}: player {} is not in the squad of {}", zone.name, player,
                             plan.squadSize);
        if (!taken.insert(player).second)
          return std::format("{}: player {} is already in another zone", zone.name, player);
      }
      const ZoneFootprint box = *zoneFootprint(zone);
      if (box.minX < M || box.minY < M || box.maxX > LENGTH_METRES - M ||
          box.maxY > WIDTH_METRES - M)
        return std::format("{} ({}): off the ground", zone.name, exercise.name);
      for (const ZoneFootprint& other : placed)
        if (box.minX < other.maxX + 2.0f * M && other.minX < box.maxX + 2.0f * M &&
            box.minY < other.maxY + 2.0f * M && other.minY < box.maxY + 2.0f * M)
          return std::format("{} ({}): too close to another zone", zone.name, exercise.name);
      placed.push_back(box);
    }
  }
  return std::nullopt;
}

SessionRun::SessionRun(const SessionPlan& plan, const StatsConfig& config_ref,
                       std::uint32_t seed_value, bool record_value)
    : sessionPlan(plan), config(config_ref), seed(seed_value), record(record_value)
{
  // The squad: reference players, as the drill runs' own.
  for (std::size_t index = 0; index < plan.squadSize; ++index)
  {
    std::map<std::string, float> stats;
    for (const std::string& stat : config.possible_stats)
      stats[stat] = DrillRun::REFERENCE_RATING;
    players.push_back(std::make_unique<Player>(
        FIRST_SQUAD_ID + static_cast<PlayerID>(index), static_cast<TeamID>(1), "",
        std::format("Player {}", index + 1), PlayerRole::CM, Language::EN, 0, 0, 25, 1,
        static_cast<std::uint8_t>(180), Foot::Right, stats));
  }
  if (plan.exercises.empty())
    over = true;
  else
    startExercise(0);
}

SessionRun::~SessionRun()
{
  // The zones' engines borrow the squad's players: they go first.
  current.clear();
}

void SessionRun::startExercise(std::size_t index)
{
  exerciseIndex = index;
  current.clear();
  const SessionExercise& exercise = sessionPlan.exercises[index];
  for (std::size_t zoneIndex = 0; zoneIndex < exercise.zones.size(); ++zoneIndex)
  {
    const SessionZone& plan = exercise.zones[zoneIndex];
    Zone zone;
    zone.plan = &plan;
    zone.drill = makeSessionDrill(plan);
    if (!zone.drill) continue;
    // Each zone on its own seed from the session's, so the session repeats.
    zone.drill->seed = seed + static_cast<std::uint32_t>(index * 100 + zoneIndex);
    std::vector<Player*> squadPlayers;
    for (const std::size_t player : plan.players)
      squadPlayers.push_back(players[player].get());
    zone.run = std::make_unique<DrillRun>(*zone.drill, config, record,
                                          std::function<void(MatchEngine&)>{}, squadPlayers);
    current.push_back(std::move(zone));
  }
}

void SessionRun::finishExercise()
{
  std::vector<std::string> reports;
  for (const Zone& zone : current)
    for (const std::string& line : zone.report)
      reports.push_back(std::format("{}: {}", zone.plan->name, line));
  finishedReports.push_back(std::move(reports));
}

bool SessionRun::step()
{
  if (over) return false;
  bool running = false;
  for (Zone& zone : current)
  {
    if (!zone.run || zone.run->finished()) continue;
    if (zone.run->step())
      running = true;
    else
      zone.report = zone.drill->report();
  }
  elapsed += MatchTuning::Timing::FIXED_STEP_SECONDS;
  if (running) return true;
  finishExercise();
  if (exerciseIndex + 1 < sessionPlan.exercises.size())
  {
    startExercise(exerciseIndex + 1);
    return true;
  }
  over = true;
  return false;
}
