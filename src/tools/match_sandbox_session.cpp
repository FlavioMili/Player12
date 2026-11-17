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
#include <span>
#include <stdexcept>
#include <utility>

#include "gui/render/match_render_snapshot.h"
#include "model/match_engine.h"
#include "model/player.h"
#include "tools/match_sandbox_setup.h"
#include "tools/match_sandbox_training.h"

namespace
{
/** The squad's ids, apart from the drill runs' reference players. */
constexpr PlayerID FIRST_SQUAD_ID = 800'000;
constexpr int REFERENCE_PLAYER_AGE = 25;
constexpr std::uint8_t REFERENCE_PLAYER_HEIGHT_CM = 180;
constexpr std::size_t EXERCISE_SEED_STRIDE = 100;
constexpr std::size_t REGROUP_SEED_OFFSET = EXERCISE_SEED_STRIDE - 1;
constexpr float CENTRE_TO_EDGE_SCALE = 0.5F;

[[nodiscard]] std::optional<std::string> checkPlayerAssignments(
    const SessionZone& zone, std::size_t squadSize,
    std::set<std::size_t>& taken)
{
  for (const std::size_t player : zone.players)
  {
    if (player >= squadSize)
    {
      return std::format("{}: player {} is not in the squad of {}", zone.name,
                         player, squadSize);
    }
    if (!taken.insert(player).second)
    {
      return std::format("{}: player {} is already in another zone", zone.name,
                         player);
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> checkZoneDrill(const SessionZone& zone)
{
  const std::unique_ptr<Drill> drill = makeSessionDrill(zone);
  if (!drill)
  {
    return std::format("{}: no drill called \"{}\"", zone.name, zone.drill);
  }
  for (const auto& [name, value] : zone.settings)
  {
    const std::vector<DrillParameter> parameters = drill->parameters();
    if (std::ranges::none_of(parameters, [&](const DrillParameter& parameter)
                             { return name == parameter.name; }))
    {
      return std::format("{}: {} has no setting \"{}\"", zone.name, zone.drill,
                         name);
    }
  }
  if (drill->players().size() != zone.players.size())
  {
    return std::format("{}: {} names {} players, the zone has {}", zone.name,
                       zone.drill, drill->players().size(),
                       zone.players.size());
  }
  return std::nullopt;
}

/** The example session (plan section 23, point 9; our own layout): 16
 * players, two 4 v 4 small-sided games side by side, then four 2 v 2, then
 * one 6 v 6 with four resting in the changing room. */
SessionPlan smallSidedRotation()
{
  constexpr std::size_t SQUAD_SIZE = 16;
  constexpr PitchPoint LEFT_FOURS_CENTRE{27.0F, 34.0F};
  constexpr PitchPoint RIGHT_FOURS_CENTRE{78.0F, 34.0F};
  constexpr PitchPoint TOP_LEFT_CENTRE{26.0F, 17.0F};
  constexpr PitchPoint TOP_RIGHT_CENTRE{79.0F, 17.0F};
  constexpr PitchPoint BOTTOM_LEFT_CENTRE{26.0F, 51.0F};
  constexpr PitchPoint BOTTOM_RIGHT_CENTRE{79.0F, 51.0F};
  constexpr PitchPoint SIXES_CENTRE{52.5F, 34.0F};
  constexpr auto LEFT_FOURS =
      std::to_array<std::size_t>({0, 1, 2, 3, 4, 5, 6, 7});
  constexpr auto RIGHT_FOURS =
      std::to_array<std::size_t>({8, 9, 10, 11, 12, 13, 14, 15});
  constexpr auto TOP_LEFT = std::to_array<std::size_t>({0, 8, 1, 9});
  constexpr auto TOP_RIGHT = std::to_array<std::size_t>({2, 10, 3, 11});
  constexpr auto BOTTOM_LEFT = std::to_array<std::size_t>({4, 12, 5, 13});
  constexpr auto BOTTOM_RIGHT = std::to_array<std::size_t>({6, 14, 7, 15});
  constexpr auto SIXES =
      std::to_array<std::size_t>({0, 2, 1, 3, 4, 6, 5, 7, 8, 10, 9, 11});
  constexpr int SIX_A_SIDE = 6;
  SessionPlan plan;
  plan.name = "Small-sided rotation";
  plan.summary =
      "Sixteen players: two 4 v 4 small-sided games side by side, then four "
      "2 v 2 games, then one 6 v 6 with four resting. Two minutes each "
      "(training ground T7).";
  plan.squadSize = SQUAD_SIZE;
  const auto game = [](std::string name, int rung, int nASide,
                       PitchPoint centre, std::span<const std::size_t> players)
  {
    constexpr float EXERCISE_SECONDS = 120.0F;
    SessionZone zone;
    zone.name = std::move(name);
    zone.drill = "Small-sided game";
    zone.settings = {{"Rung", static_cast<float>(rung)},
                     {"N a side (N v N)", static_cast<float>(nASide)},
                     {"Seconds", EXERCISE_SECONDS}};
    zone.centre = centre;
    zone.players.assign(players.begin(), players.end());
    return zone;
  };
  // The drill names its players home, away, home, away...: a zone's list
  // alternates the sides.
  SessionExercise fours{.name = "Two 4 v 4", .zones = {}};
  fours.zones.push_back(
      game("Left 4 v 4", 4, 4, LEFT_FOURS_CENTRE, LEFT_FOURS));
  fours.zones.push_back(
      game("Right 4 v 4", 4, 4, RIGHT_FOURS_CENTRE, RIGHT_FOURS));
  SessionExercise twos{.name = "Four 2 v 2", .zones = {}};
  twos.zones.push_back(game("Top left 2 v 2", 2, 0, TOP_LEFT_CENTRE, TOP_LEFT));
  twos.zones.push_back(
      game("Top right 2 v 2", 2, 0, TOP_RIGHT_CENTRE, TOP_RIGHT));
  twos.zones.push_back(
      game("Bottom left 2 v 2", 2, 0, BOTTOM_LEFT_CENTRE, BOTTOM_LEFT));
  twos.zones.push_back(
      game("Bottom right 2 v 2", 2, 0, BOTTOM_RIGHT_CENTRE, BOTTOM_RIGHT));
  SessionExercise sixes{.name = "6 v 6", .zones = {}};
  sixes.zones.push_back(game("6 v 6", 4, SIX_A_SIDE, SIXES_CENTRE, SIXES));
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
    if (zone.drill != drill->name())
    {
      continue;
    }
    for (const DrillParameter& parameter : drill->parameters())
    {
      if (parameter.value != nullptr)
      {
        if (const auto found = zone.settings.find(parameter.name);
            found != zone.settings.end())
        {
          *parameter.value = found->second;
        }
      }
    }
    return std::move(drill);
  }
  return nullptr;
}

std::optional<ZoneFootprint> zoneFootprint(const SessionZone& zone)
{
  const std::unique_ptr<Drill> drill = makeSessionDrill(zone);
  if (!drill)
  {
    return std::nullopt;
  }
  const MatchPitch pitch = drill->pitch();
  const float along = zone.turned ? pitch.width : pitch.length;
  const float across = zone.turned ? pitch.length : pitch.width;
  return ZoneFootprint{.minX = zone.centre.x - (CENTRE_TO_EDGE_SCALE * along),
                       .minY = zone.centre.y - (CENTRE_TO_EDGE_SCALE * across),
                       .maxX = zone.centre.x + (CENTRE_TO_EDGE_SCALE * along),
                       .maxY = zone.centre.y + (CENTRE_TO_EDGE_SCALE * across)};
}

std::optional<std::string> checkSessionPlan(const SessionPlan& plan)
{
  using namespace TrainingGround;
  constexpr float M = ZONE_MARGIN_METRES;
  constexpr float INTER_ZONE_GAP = M + M;
  for (const SessionExercise& exercise : plan.exercises)
  {
    std::vector<ZoneFootprint> placed;
    std::set<std::size_t> taken;
    for (const SessionZone& zone : exercise.zones)
    {
      if (const auto problem = checkZoneDrill(zone))
      {
        return problem;
      }
      if (const auto problem =
              checkPlayerAssignments(zone, plan.squadSize, taken))
      {
        return problem;
      }
      const ZoneFootprint box = *zoneFootprint(zone);
      if (box.minX < M || box.minY < M || box.maxX > LENGTH_METRES - M ||
          box.maxY > WIDTH_METRES - M)
      {
        return std::format("{} ({}): off the ground", zone.name, exercise.name);
      }
      for (const ZoneFootprint& other : placed)
      {
        if (box.minX < other.maxX + INTER_ZONE_GAP &&
            other.minX < box.maxX + INTER_ZONE_GAP &&
            box.minY < other.maxY + INTER_ZONE_GAP &&
            other.minY < box.maxY + INTER_ZONE_GAP)
        {
          return std::format("{} ({}): too close to another zone", zone.name,
                             exercise.name);
        }
      }
      placed.push_back(box);
    }
  }
  return std::nullopt;
}

SessionRun::SessionRun(const SessionPlan& plan, const StatsConfig& config_ref,
                       std::uint32_t seed_value, bool record_value,
                       const std::vector<TrainingPlayer>& picks)
    : sessionPlan(plan),
      config(config_ref),
      seed(seed_value),
      record(record_value)
{
  for (const SessionExercise& exercise : sessionPlan.exercises)
  {
    std::set<std::size_t> taken;
    for (const SessionZone& zone : exercise.zones)
    {
      if (const auto problem =
              checkPlayerAssignments(zone, sessionPlan.squadSize, taken))
      {
        throw std::invalid_argument(*problem);
      }
    }
  }
  // The squad: the first picks, then reference players, as the drill runs'
  // own, for any gap.
  for (std::size_t index = 0; index < plan.squadSize; ++index)
  {
    const PlayerID id = FIRST_SQUAD_ID + static_cast<PlayerID>(index);
    if (index < picks.size())
    {
      const TrainingPlayer& pick = picks[index];
      players.push_back(std::make_unique<Player>(
          id, static_cast<TeamID>(1), "", pick.name, pick.role, Language::EN, 0,
          0, REFERENCE_PLAYER_AGE, 1, static_cast<std::uint8_t>(pick.height),
          pick.foot, pick.stats));
      continue;
    }
    std::map<std::string, float> stats;
    for (const std::string& stat : config.possible_stats)
    {
      stats[stat] = DrillRun::REFERENCE_RATING;
    }
    players.push_back(std::make_unique<Player>(
        id, static_cast<TeamID>(1), "", std::format("Player {}", index + 1),
        PlayerRole::CM, Language::EN, 0, 0, REFERENCE_PLAYER_AGE, 1,
        REFERENCE_PLAYER_HEIGHT_CM, Foot::Right, stats));
  }
  if (plan.exercises.empty())
  {
    over = true;
  }
  else
  {
    current = buildExercise(0);
  }
}

SessionRun::~SessionRun()
{
  // The engines borrow the squad's players: they go first.
  regroup.reset();
  upcoming.clear();
  current.clear();
}

std::vector<SessionRun::Zone> SessionRun::buildExercise(std::size_t index)
{
  std::vector<Zone> built;
  const SessionExercise& exercise = sessionPlan.exercises[index];
  for (std::size_t zoneIndex = 0; zoneIndex < exercise.zones.size();
       ++zoneIndex)
  {
    const SessionZone& plan = exercise.zones[zoneIndex];
    Zone zone;
    zone.plan = &plan;
    zone.drill = makeSessionDrill(plan);
    if (!zone.drill)
    {
      continue;
    }
    // Each zone on its own seed from the session's, so the session repeats.
    zone.drill->seed = seed + static_cast<std::uint32_t>(
                                  (index * EXERCISE_SEED_STRIDE) + zoneIndex);
    std::vector<Player*> squadPlayers;
    squadPlayers.reserve(plan.players.size());
    for (const std::size_t player : plan.players)
    {
      squadPlayers.push_back(players[player].get());
    }
    // A zone's own brain and body (T7e), for its engine only: the run takes
    // the sandbox's when it is made, so they stand in for that.
    const std::array<BrainId, 2> setUpBrains = sandboxBrains();
    const std::array<BodyId, 2> setUpBodies = sandboxBodies();
    if (plan.brain)
    {
      sandboxBrains() = {*plan.brain, *plan.brain};
    }
    if (plan.body)
    {
      sandboxBodies() = {*plan.body, *plan.body};
    }
    zone.run = std::make_unique<DrillRun>(*zone.drill, config, record,
                                          std::function<void(MatchEngine&)>{},
                                          squadPlayers);
    sandboxBrains() = setUpBrains;
    sandboxBodies() = setUpBodies;
    // Each player brings his condition from when he last played (T7c);
    // where he starts is still the drill's (the change itself is T7d).
    MatchEngine& engine = zone.run->engine();
    for (const Player* player : squadPlayers)
    {
      const auto saved = carried.find(player->getId());
      if (saved == carried.end())
      {
        continue;
      }
      if (std::optional<MatchEngine::CarriedPlayer> now =
              engine.carriedPlayer(player->getId()))
      {
        now->stamina = saved->second.stamina;
        now->sprintReserve = saved->second.sprintReserve;
        now->fastBurstDebt = saved->second.fastBurstDebt;
        now->loadSpeed = saved->second.loadSpeed;
        engine.carryPlayerIn(*now);
        zone.startStates[player->getId()] = *now;
      }
    }
    built.push_back(std::move(zone));
  }
  return built;
}

bool SessionRun::sendToChangingRoom(std::size_t squadIndex)
{
  if (over || squadIndex >= players.size())
  {
    return false;
  }
  const PlayerID id = players[squadIndex]->getId();
  for (Zone& zone : current)
  {
    if (!zone.run || zone.run->finished())
    {
      continue;
    }
    MatchEngine& engine = zone.run->engine();
    const std::optional<MatchEngine::CarriedPlayer> now =
        engine.carriedPlayer(id);
    if (!now || !engine.takeOffForDrill(id))
    {
      continue;
    }
    carried[id] = *now;
    resting.push_back(squadIndex);
    return true;
  }
  return false;
}

std::vector<std::size_t> SessionRun::changingRoom() const
{
  std::vector<std::size_t> room = resting;
  if (over)
  {
    return room;
  }
  if (regroup)
  {
    // In a change: everyone not walking to a place.
    room.clear();
    for (std::size_t index = 0; index < players.size(); ++index)
    {
      if (std::ranges::none_of(movers, [index](const Mover& mover)
                               { return mover.squadIndex == index; }))
      {
        room.push_back(index);
      }
    }
    return room;
  }
  for (std::size_t index = 0; index < players.size(); ++index)
  {
    bool inZone = false;
    for (const Zone& zone : current)
    {
      if (std::ranges::find(zone.plan->players, index) !=
          zone.plan->players.end())
      {
        inZone = true;
      }
    }
    if (!inZone)
    {
      room.push_back(index);
    }
  }
  return room;
}

void SessionRun::finishExercise()
{
  // Everyone still playing leaves with his running state (T7c).
  float conditionSum = 0.0F;
  int counted = 0;
  for (const Zone& zone : current)
  {
    if (!zone.run)
    {
      continue;
    }
    for (const std::size_t index : zone.plan->players)
    {
      const PlayerID id = players[index]->getId();
      if (std::ranges::find(resting, index) == resting.end())
      {
        if (const auto now = zone.run->engine().carriedPlayer(id))
        {
          carried[id] = *now;
        }
      }
      if (const auto saved = carried.find(id); saved != carried.end())
      {
        conditionSum += saved->second.stamina;
        ++counted;
      }
    }
  }
  std::vector<std::string> reports;
  if (!changeReport.empty())
  {
    reports.push_back(std::exchange(changeReport, {}));
  }
  if (counted > 0)
  {
    reports.push_back(
        std::format("Squad: average condition {:.0f}% at the end",
                    100.0F * conditionSum / static_cast<float>(counted)));
  }
  for (const Zone& zone : current)
  {
    for (const std::string& line : zone.report)
    {
      reports.push_back(std::format("{}: {}", zone.plan->name, line));
    }
  }
  finishedReports.push_back(std::move(reports));
}

namespace
{
/** A zone's point, velocity and facing on the ground, and back (T7d). */
PitchPoint groundPoint(const SessionZone& zone, const MatchPitch& pitch,
                       PitchPoint local)
{
  const float dx = local.x - (CENTRE_TO_EDGE_SCALE * pitch.length);
  const float dy = local.y - (CENTRE_TO_EDGE_SCALE * pitch.width);
  return zone.turned ? PitchPoint{zone.centre.x - dy, zone.centre.y + dx}
                     : PitchPoint{zone.centre.x + dx, zone.centre.y + dy};
}
PitchPoint localPoint(const SessionZone& zone, const MatchPitch& pitch,
                      PitchPoint ground)
{
  const float gx = ground.x - zone.centre.x;
  const float gy = ground.y - zone.centre.y;
  return zone.turned ? PitchPoint{(CENTRE_TO_EDGE_SCALE * pitch.length) + gy,
                                  (CENTRE_TO_EDGE_SCALE * pitch.width) - gx}
                     : PitchPoint{(CENTRE_TO_EDGE_SCALE * pitch.length) + gx,
                                  (CENTRE_TO_EDGE_SCALE * pitch.width) + gy};
}
Vector2F groundVector(const SessionZone& zone, Vector2F local)
{
  return zone.turned ? Vector2F{.x = -local.y, .y = local.x} : local;
}
Vector2F localVector(const SessionZone& zone, Vector2F ground)
{
  return zone.turned ? Vector2F{.x = ground.y, .y = -ground.x} : ground;
}
constexpr float QUARTER_TURN = 1.5707963F;

[[nodiscard]] PitchPoint changingRoomPlace(int benchSpot)
{
  constexpr float FIRST_BENCH_SPOT_METRES = 8.0F;
  constexpr float BENCH_SPACING_METRES = 3.0F;
  constexpr float BENCH_FENCE_MARGIN_METRES = 2.0F;
  return PitchPoint{FIRST_BENCH_SPOT_METRES +
                        (BENCH_SPACING_METRES * static_cast<float>(benchSpot)),
                    TrainingGround::WIDTH_METRES - BENCH_FENCE_MARGIN_METRES};
}

/** The change of exercise's engine (T7d): the whole ground, walled at its
 * fence, no goals; everyone who was playing, carried over where he was;
 * the ball with a coach; each walking to his next place. Next zones are
 * marked out as areas. */
class RegroupDrill final : public Drill
{
 public:
  struct Walker
  {
    MatchEngine::CarriedPlayer state;
    PitchPoint target;
  };
  RegroupDrill(std::vector<Walker> walkers, std::vector<ZoneFootprint> next)
      : walkers_(std::move(walkers)), next_(std::move(next))
  {
  }
  [[nodiscard]] const char* name() const override { return "Regroup"; }
  [[nodiscard]] const char* summary() const override
  {
    return "Between exercises.";
  }
  std::vector<DrillParameter> parameters() override { return {}; }
  [[nodiscard]] MatchPitch pitch() const override
  {
    return MatchPitch::training(TrainingGround::LENGTH_METRES,
                                TrainingGround::WIDTH_METRES);
  }
  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    std::vector<DrillPlayerSpec> list;
    list.reserve(walkers_.size());
    for (std::size_t index = 0; index < walkers_.size(); ++index)
    {
      list.push_back({.name = std::format("Walker {}", index + 1),
                      .role = PlayerRole::CM,
                      .homeTeam = index % 2 == 0,
                      .stats = {},
                      .derived = {}});
    }
    return list;
  }
  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    constexpr std::uint32_t NEXT_ZONE_COLOUR = 0xC0FFFFFFU;
    MatchScenario scenario;
    for (std::size_t index = 0; index < ids.size() && index < walkers_.size();
         ++index)
    {
      scenario.players.push_back({.playerId = ids[index],
                                  .position = walkers_[index].state.position,
                                  .makingRun = false});
    }
    scenario.ballPosition =
        PitchPoint{CENTRE_TO_EDGE_SCALE * TrainingGround::LENGTH_METRES,
                   CENTRE_TO_EDGE_SCALE * TrainingGround::WIDTH_METRES};
    engine.startDrill(scenario);
    engine.holdPlayForRegroup();
    PitchKit::Layout kit;
    for (std::size_t zone = 0; zone < next_.size(); ++zone)
    {
      kit.areas.push_back(
          {.name = std::format("Next {}", zone + 1),
           .min = PitchPoint{next_[zone].minX, next_[zone].minY},
           .max = PitchPoint{next_[zone].maxX, next_[zone].maxY},
           .colour = NEXT_ZONE_COLOUR});
    }
    engine.setKit(std::move(kit));
    for (std::size_t index = 0; index < ids.size() && index < walkers_.size();
         ++index)
    {
      MatchEngine::CarriedPlayer state = walkers_[index].state;
      state.id = ids[index];
      engine.carryPlayerIn(state);
      engine.setDrillTarget(ids[index], walkers_[index].target, false);
    }
  }
  bool afterStep(MatchEngine& /*engine*/) override { return true; }
  [[nodiscard]] std::vector<std::string> report() const override { return {}; }
  [[nodiscard]] std::vector<DrillMetric> metrics() const override { return {}; }

 private:
  std::vector<Walker> walkers_;
  std::vector<ZoneFootprint> next_;
};
}  // namespace

void SessionRun::beginChange(std::size_t next)
{
  // The next exercise's zones made ready (players at the drill's places,
  // with their condition), not yet stepped.
  upcomingIndex = next;
  upcoming = buildExercise(next);
  // Who walks: everyone who was playing at the end, from where he was, to
  // his place in the next exercise (or the changing room's spots by the
  // bottom fence).
  movers.clear();
  std::vector<RegroupDrill::Walker> walkers;
  std::vector<Player*> walkerPlayers;
  int benchSpot = 0;
  for (const Zone& zone : current)
  {
    if (!zone.run)
    {
      continue;
    }
    const MatchPitch& from = zone.run->engine().getPitch();
    for (const std::size_t index : zone.plan->players)
    {
      if (std::ranges::find(resting, index) != resting.end())
      {
        continue;
      }
      const PlayerID id = players[index]->getId();
      const auto saved = carried.find(id);
      if (saved == carried.end())
      {
        continue;
      }
      const Mover mover = nextPlace(index, changingRoomPlace(benchSpot));
      if (!mover.toZone)
      {
        ++benchSpot;
      }
      MatchEngine::CarriedPlayer state = saved->second;
      state.position = groundPoint(*zone.plan, from, state.position);
      state.velocity = groundVector(*zone.plan, state.velocity);
      if (zone.plan->turned)
      {
        state.facing += QUARTER_TURN;
      }
      walkers.push_back({.state = state, .target = mover.target});
      walkerPlayers.push_back(players[index].get());
      movers.push_back(mover);
    }
  }
  std::vector<ZoneFootprint> footprints;
  for (const Zone& zone : upcoming)
  {
    if (const auto box = zoneFootprint(*zone.plan))
    {
      footprints.push_back(*box);
    }
  }
  // Merge: the zones go; one engine over the whole ground takes everyone.
  current.clear();
  resting.clear();
  const std::vector<RegroupDrill::Walker> leaving = walkers;
  regroupDrill =
      std::make_unique<RegroupDrill>(std::move(walkers), std::move(footprints));
  regroupDrill->seed =
      seed + static_cast<std::uint32_t>((next * EXERCISE_SEED_STRIDE) +
                                        REGROUP_SEED_OFFSET);
  regroup = std::make_unique<DrillRun>(*regroupDrill, config, record,
                                       std::function<void(MatchEngine&)>{},
                                       walkerPlayers);
  changeStart = elapsed;
  // How far anyone jumped, or his condition, at the merge (should be none).
  mergeJump = 0.0F;
  mergeConditionJump = 0.0F;
  for (std::size_t index = 0;
       index < leaving.size() && index < walkerPlayers.size(); ++index)
  {
    if (const auto now =
            regroup->engine().carriedPlayer(walkerPlayers[index]->getId()))
    {
      mergeJump = std::max(
          mergeJump,
          std::hypot(now->position.x - leaving[index].state.position.x,
                     now->position.y - leaving[index].state.position.y));
      mergeConditionJump =
          std::max(mergeConditionJump,
                   std::abs(now->stamina - leaving[index].state.stamina));
    }
  }
}

SessionRun::Mover SessionRun::nextPlace(std::size_t squadIndex,
                                        const PitchPoint& benchPlace) const
{
  Mover mover;
  mover.squadIndex = squadIndex;
  mover.target = benchPlace;
  const PlayerID id = players[squadIndex]->getId();
  for (std::size_t to = 0; to < upcoming.size(); ++to)
  {
    const Zone& zone = upcoming[to];
    if (std::ranges::find(zone.plan->players, squadIndex) ==
        zone.plan->players.end())
    {
      continue;
    }
    if (const auto start = zone.run->engine().carriedPlayer(id))
    {
      mover.target = groundPoint(*zone.plan, zone.run->engine().getPitch(),
                                 start->position);
    }
    mover.toZone = to;
  }
  return mover;
}

void SessionRun::finishChange()
{
  // Split: each mover goes into his next zone as he arrived (where he is,
  // how he moves, his condition); the rest keep their state for later.
  const MatchEngine& merged = regroup->engine();
  int arrived = 0;
  float splitJump = 0.0F;
  float splitConditionJump = 0.0F;
  for (const Mover& mover : movers)
  {
    const PlayerID id = players[mover.squadIndex]->getId();
    const std::optional<MatchEngine::CarriedPlayer> state =
        merged.carriedPlayer(id);
    if (!state)
    {
      continue;
    }
    carried[id] = *state;
    if (std::hypot(state->position.x - mover.target.x,
                   state->position.y - mover.target.y) <= ARRIVED_METRES)
    {
      ++arrived;
    }
    if (!mover.toZone)
    {
      continue;
    }
    Zone& zone = upcoming[*mover.toZone];
    MatchEngine& engine = zone.run->engine();
    MatchEngine::CarriedPlayer local = *state;
    local.position = localPoint(*zone.plan, engine.getPitch(), state->position);
    local.velocity = localVector(*zone.plan, state->velocity);
    if (zone.plan->turned)
    {
      local.facing -= QUARTER_TURN;
    }
    engine.carryPlayerIn(local);
    zone.startStates[id] = local;
    // How far he jumped, or his condition, at the split (should be none).
    if (const auto now = engine.carriedPlayer(id))
    {
      const PitchPoint placed =
          groundPoint(*zone.plan, engine.getPitch(), now->position);
      splitJump = std::max(splitJump, std::hypot(placed.x - state->position.x,
                                                 placed.y - state->position.y));
      splitConditionJump =
          std::max(splitConditionJump, std::abs(now->stamina - state->stamina));
    }
  }
  // Reported with the exercise it led into.
  changeReport = std::format(
      "Change: {} of {} at their places after {:.1f} s; largest jump at the "
      "merge {:.3f} m "
      "(condition {:.4f}), at the split {:.3f} m (condition {:.4f})",
      arrived, movers.size(), elapsed - changeStart, mergeJump,
      mergeConditionJump, splitJump, splitConditionJump);
  regroup.reset();
  regroupDrill.reset();
  current = std::move(upcoming);
  upcoming.clear();
  exerciseIndex = upcomingIndex;
}

bool SessionRun::step()
{
  if (over)
  {
    return false;
  }
  if (regroup)
  {
    // The change: everyone walks; it ends when all are at their places or
    // at the time limit.
    regroup->step();
    elapsed += MatchTuning::Timing::FIXED_STEP_SECONDS;
    bool allThere = true;
    for (const Mover& mover : movers)
    {
      if (const auto state = regroup->engine().carriedPlayer(
              players[mover.squadIndex]->getId()))
      {
        if (std::hypot(state->position.x - mover.target.x,
                       state->position.y - mover.target.y) > ARRIVED_METRES)
        {
          allThere = false;
        }
      }
    }
    if (allThere || elapsed - changeStart >= CHANGE_LIMIT_SECONDS)
    {
      finishChange();
    }
    return true;
  }
  bool running = false;
  for (Zone& zone : current)
  {
    if (!zone.run || zone.run->finished())
    {
      continue;
    }
    if (zone.run->step())
    {
      running = true;
    }
    else
    {
      zone.report = zone.drill->report();
    }
  }
  elapsed += MatchTuning::Timing::FIXED_STEP_SECONDS;
  if (running)
  {
    return true;
  }
  finishExercise();
  if (exerciseIndex + 1 < sessionPlan.exercises.size())
  {
    beginChange(exerciseIndex + 1);
    return true;
  }
  over = true;
  return false;
}

void fillSessionRenderSnapshot(const SessionRun& run,
                               MatchRenderSnapshot& snapshot,
                               std::size_t watchedZone)
{
  // The ground is the pitch: plain training grass, its fence the walls, no
  // goals of its own (each zone has its own).
  snapshot.pitchLengthMetres = TrainingGround::LENGTH_METRES;
  snapshot.pitchWidthMetres = TrainingGround::WIDTH_METRES;
  snapshot.pitchSurface = PitchSurface::TRAINING;
  snapshot.pitchEdge = PitchEdge::WALLS;
  snapshot.pitchGoal = PitchGoal::ofKind(GoalKind::NONE);
  snapshot.kit.clear();
  snapshot.players.clear();
  snapshot.touchline.clear();
  snapshot.zones.clear();
  snapshot.extraBalls.clear();
  snapshot.state = MatchState::PLAYING;
  // Bibs: each zone's home side in the first colours, away in the second.
  snapshot.homeTeam = 1;
  snapshot.awayTeam = 2;
  snapshot.events = nullptr;
  snapshot.stats = nullptr;
  snapshot.playerStats = nullptr;
  // A change of exercise (T7d): the one engine over the whole ground, its
  // walkers and the next zones marked out.
  if (const DrillRun* change = run.regroupRun())
  {
    fillMatchRenderSnapshot(change->engine(), snapshot);
    std::erase_if(snapshot.players, [](const MatchRenderPlayer& player)
                  { return !player.onPitch; });
    snapshot.homeTeam = 1;
    snapshot.awayTeam = 2;
    snapshot.events = nullptr;
    snapshot.stats = nullptr;
    snapshot.playerStats = nullptr;
    snapshot.state = MatchState::PLAYING;
    return;
  }
  std::vector<MatchRenderBall> balls;
  MatchRenderSnapshot zoneView;
  for (const SessionRun::Zone& zone : run.zones())
  {
    if (!zone.run)
    {
      continue;
    }
    const MatchEngine& engine = zone.run->engine();
    fillMatchRenderSnapshot(engine, zoneView);
    MatchRenderZone placed;
    placed.centre = zone.plan->centre;
    placed.turned = zone.plan->turned;
    placed.lengthMetres = engine.getPitch().length;
    placed.widthMetres = engine.getPitch().width;
    placed.goal = engine.getPitch().goal;
    snapshot.zones.push_back(placed);
    const auto point = [&](PitchPoint local) { return placed.toGround(local); };
    const auto angle = [&](float local) { return placed.toGroundAngle(local); };
    for (MatchRenderPlayer player : zoneView.players)
    {
      if (!player.onPitch)
      {
        continue;
      }
      player.previousPosition = point(player.previousPosition);
      player.currentPosition = point(player.currentPosition);
      player.movementTarget = point(player.movementTarget);
      player.previousFacingAngle = angle(player.previousFacingAngle);
      player.currentFacingAngle = angle(player.currentFacingAngle);
      player.lyingAngle = angle(player.lyingAngle);
      player.upFacingAngle = angle(player.upFacingAngle);
      snapshot.players.push_back(player);
    }
    for (MatchRenderTouchlinePerson person : zoneView.touchline)
    {
      person.previousPosition = point(person.previousPosition);
      person.currentPosition = point(person.currentPosition);
      person.previousFacingAngle = angle(person.previousFacingAngle);
      person.currentFacingAngle = angle(person.currentFacingAngle);
      person.lyingAngle = angle(person.lyingAngle);
      snapshot.touchline.push_back(person);
    }
    for (PitchKit::Cone cone : zoneView.kit.cones)
    {
      cone.at = point(cone.at);
      snapshot.kit.cones.push_back(std::move(cone));
    }
    for (PitchKit::Line line : zoneView.kit.lines)
    {
      line.from = point(line.from);
      line.to = point(line.to);
      snapshot.kit.lines.push_back(std::move(line));
    }
    for (PitchKit::Area area : zoneView.kit.areas)
    {
      const PitchPoint a = point(area.min);
      const PitchPoint b = point(area.max);
      area.min = PitchPoint{std::min(a.x, b.x), std::min(a.y, b.y)};
      area.max = PitchPoint{std::max(a.x, b.x), std::max(a.y, b.y)};
      snapshot.kit.areas.push_back(std::move(area));
    }
    for (PitchKit::Dummy dummy : zoneView.kit.dummies)
    {
      dummy.at = point(dummy.at);
      dummy.facing = angle(dummy.facing);
      snapshot.kit.dummies.push_back(std::move(dummy));
    }
    MatchRenderBall ball = zoneView.ball;
    ball.previousPosition = point(ball.previousPosition);
    ball.currentPosition = point(ball.currentPosition);
    balls.push_back(ball);
  }
  // The watched zone's ball is the main one (the 3D cameras follow it);
  // the rest are extra balls.
  if (balls.empty())
  {
    snapshot.ball.outOfSight = true;
    return;
  }
  const std::size_t watched = std::min(watchedZone, balls.size() - 1);
  snapshot.ball = balls[watched];
  for (std::size_t index = 0; index < balls.size(); ++index)
  {
    if (index != watched)
    {
      snapshot.extraBalls.push_back(balls[index]);
    }
  }
}

std::vector<std::string> runCarryCheck(const StatsConfig& config)
{
  constexpr float DRILL_RUNG = 3.0F;
  constexpr float DRILL_SECONDS = 60.0F;
  constexpr std::size_t SQUAD_SIZE = 6;
  constexpr std::uint32_t ORIGINAL_SEED = 7;
  constexpr std::uint32_t CARRIED_SEED = 8;
  constexpr int STEPS_AFTER_TAKE_OFF = 100;
  // A 3 v 3 small-sided game on reference players, run 30 s.
  SessionZone zone;
  zone.drill = "Small-sided game";
  zone.settings = {{"Rung", DRILL_RUNG}, {"Seconds", DRILL_SECONDS}};
  std::vector<std::unique_ptr<Player>> squad;
  std::vector<Player*> picks;
  for (std::size_t index = 0; index < SQUAD_SIZE; ++index)
  {
    std::map<std::string, float> stats;
    for (const std::string& stat : config.possible_stats)
    {
      stats[stat] = DrillRun::REFERENCE_RATING;
    }
    squad.push_back(std::make_unique<Player>(
        FIRST_SQUAD_ID + static_cast<PlayerID>(index), static_cast<TeamID>(1),
        "", std::format("Player {}", index + 1), PlayerRole::CM, Language::EN,
        0, 0, REFERENCE_PLAYER_AGE, 1, REFERENCE_PLAYER_HEIGHT_CM, Foot::Right,
        stats));
    picks.push_back(squad.back().get());
  }
  const std::unique_ptr<Drill> first = makeSessionDrill(zone);
  const std::unique_ptr<Drill> second = makeSessionDrill(zone);
  first->seed = ORIGINAL_SEED;
  second->seed = CARRIED_SEED;
  DrillRun original(*first, config, false, {}, picks);
  constexpr double STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
  constexpr double CARRY_CHECK_SECONDS = 30.0;
  for (std::size_t tick = 0;
       static_cast<double>(tick) * STEP < CARRY_CHECK_SECONDS; ++tick)
  {
    original.step();
  }
  // The second engine takes up every player's running state.
  DrillRun carried(*second, config, false, {}, picks);
  std::vector<MatchEngine::CarriedPlayer> states;
  for (const Player* player : picks)
  {
    if (const auto state = original.engine().carriedPlayer(player->getId()))
    {
      states.push_back(*state);
      carried.engine().carryPlayerIn(*state);
    }
  }
  // How exactly it carried.
  float worstPlace = 0.0F;
  float worstSpeed = 0.0F;
  float worstCondition = 0.0F;
  for (const MatchEngine::CarriedPlayer& state : states)
  {
    if (const auto now = carried.engine().carriedPlayer(state.id))
    {
      worstPlace =
          std::max(worstPlace, std::hypot(now->position.x - state.position.x,
                                          now->position.y - state.position.y));
      worstSpeed =
          std::max(worstSpeed, std::hypot(now->velocity.x - state.velocity.x,
                                          now->velocity.y - state.velocity.y));
      worstCondition =
          std::max(worstCondition, std::abs(now->stamina - state.stamina));
    }
  }
  // One step on in each: the change of speed, carried against the original.
  const auto speedChange = [&](DrillRun& run)
  {
    std::vector<float> before;
    before.reserve(states.size());
    for (const MatchEngine::CarriedPlayer& state : states)
    {
      before.push_back(std::hypot(state.velocity.x, state.velocity.y));
    }
    run.step();
    float worst = 0.0F;
    for (std::size_t index = 0; index < states.size(); ++index)
    {
      if (const auto now = run.engine().carriedPlayer(states[index].id))
      {
        worst = std::max(worst,
                         std::abs(std::hypot(now->velocity.x, now->velocity.y) -
                                  before[index]));
      }
    }
    return worst;
  };
  const float originalChange = speedChange(original);
  const float carriedChange = speedChange(carried);
  // The changing room: one player off at once; the game plays on without
  // him.
  const PlayerID resting = picks.front()->getId();
  const bool takenOff = carried.engine().takeOffForDrill(resting);
  int stepsAfter = 0;
  for (; stepsAfter < STEPS_AFTER_TAKE_OFF && carried.step(); ++stepsAfter)
  {
  }
  const bool stillOff = !carried.engine().carriedPlayer(resting).has_value();
  return {
      std::format("Changing room: taken off {}, still off {}, the game played "
                  "on {:.1f} s",
                  takenOff ? "yes" : "no", stillOff ? "yes" : "no",
                  stepsAfter * STEP),
      std::format("Carried {} players after 30 s of a 3 v 3", states.size()),
      std::format(
          "Carried exactly: place within {:.4f} m, velocity within {:.4f} m/s, "
          "condition within {:.4f}",
          worstPlace, worstSpeed, worstCondition),
      std::format("Largest change of speed in the first step after: original "
                  "{:.2f} m/s, "
                  "carried {:.2f} m/s",
                  originalChange, carriedChange)};
}
