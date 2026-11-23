// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_checks.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

#include "controller/game_controller.h"
#include "global/runtime_paths.h"
#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/match_scenario.h"
#include "model/team.h"
#include "tools/match_sandbox_detail.h"
#include "tools/match_sandbox_drill.h"
#include "tools/match_sandbox_measure.h"
#include "tools/match_sandbox_possession_trace.h"
#include "tools/match_sandbox_recorder.h"
#include "tools/match_sandbox_recording.h"
#include "tools/match_sandbox_rewind.h"
#include "tools/match_sandbox_scenario.h"
#include "tools/match_sandbox_setup.h"
#include "tools/match_sandbox_shot_map.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// psapi.h requires the Windows types to be declared first.
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#endif

namespace
{
// Checks play on a match pitch, placed by fractions of it.
const MatchPitch& PITCH = MatchPitch::matchSize();
using Clock = std::chrono::steady_clock;

constexpr double DEGREES_PER_RADIAN = 180.0 / std::numbers::pi;
constexpr float DEGREES_PER_RADIAN_F = 180.0F / std::numbers::pi_v<float>;
constexpr double BYTES_PER_KIB = 1024.0;
constexpr double MS_PER_SECOND = 1000.0;
/** Seeds: the matches and traces from 1000, the possession pairings from
 * 2000, the first-touch and restart matches from 3000. */
constexpr std::uint32_t MATCH_SEED = 1000;
constexpr std::uint32_t POSSESSION_SEED = 2000;
constexpr std::uint32_t TOUCH_SEED = 3000;
/** A match that runs longer than this many steps has hung. */
constexpr int MAX_MATCH_STEPS = 2'000'000;
/** Live-style play: at most this many one-second steps, with a highlight
 * look-ahead every 30. */
constexpr int MAX_LIVE_SECONDS = 20000;
constexpr int LOOK_AHEAD_EVERY_SECONDS = 30;
/** The longest possession trace: a whole hour of play. */
constexpr double MAX_TRACE_SECONDS = 3600.0;
/** Engine copies are taken after play advanced in 10 s steps. */
constexpr float COPY_ADVANCE_SECONDS = 10.0F;
/** The rewind match's manager changes: tactics, a sub and a shout. */
constexpr float TACTICS_MINUTE = 30.0F;
constexpr float SUBSTITUTION_MINUTE = 60.0F;
constexpr float SHOUT_MINUTE = 75.0F;
constexpr float ATTACKING_PRESSING = 0.9F;
constexpr float ATTACKING_RISK = 0.8F;
/** A scenario export places every player of both sides. */
constexpr std::size_t EXPORTED_PLAYERS = 22;
/** The tampered recording's decision: its carry score moved this much. */
constexpr float TAMPERED_CARRY = 0.5F;
/** Drills: the sprint (seed, pitch fractions, at most 300 ticks); the forced
 * shot (100 seeds, 80 ticks); the measure (Pace 50 and 70, 8 runs); the shot
 * map (a 3 x 3 grid of 10 runs). */
constexpr std::uint32_t SPRINT_SEED = 7;
constexpr float SPRINT_START_X = 0.30F;
constexpr float SPRINT_TARGET_X = 0.60F;
constexpr float MIDDLE_Y = 0.50F;
constexpr float BALL_CLEAR = 0.95F;
constexpr int SPRINT_TICKS = 300;
constexpr float SHOOTER_X = 0.84F;
constexpr float SHOOTER_Y = 0.55F;
constexpr float KEEPER_X = 0.985F;
constexpr int SHOT_TICKS = 80;
constexpr std::uint32_t SHOT_SEEDS = 100;
constexpr float SLOW_PACE = 50.0F;
constexpr float FAST_PACE = 70.0F;
constexpr std::size_t MEASURE_RUNS = 8;
constexpr double NOT_MEASURED = -2.0;
constexpr float SHOT_MAP_NEAREST = 6.0F;
constexpr float SHOT_MAP_FURTHEST = 18.0F;
constexpr float SHOT_MAP_SIDE = 8.0F;
constexpr int SHOT_MAP_REPEATS = 10;
constexpr std::size_t SHOT_MAP_CELLS = 9;
/** Exercise recipes: future weights add up to 1, spread at most 0.25. */
constexpr float WEIGHT_SUM_TOLERANCE = 1e-4F;
constexpr float MAX_RECIPE_SPREAD = 0.25F;
/** The stumble trace: at his spot within 2 m; history every 0.1 s; intent
 * shown in 7 letters. */
constexpr float AT_SPOT_METRES = 2.0F;
constexpr double HISTORY_STEP_SECONDS = 0.1;
constexpr std::size_t INTENT_LETTERS = 7;

/** Private memory of the process in bytes (empty where unsupported). */
std::optional<std::size_t> privateBytes()
{
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS_EX counters{};
  if (K32GetProcessMemoryInfo(
          GetCurrentProcess(),
          // The API takes the base struct; the _EX one extends it.
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
          sizeof(counters)) != 0)
  {
    return counters.PrivateUsage;
  }
#endif
  return std::nullopt;
}

/** FNV-1a over everything a match produced that must not change. */
class Fingerprint
{
 public:
  template <typename T>
  void add(const T& value)
  {
    const auto bytes =
        std::bit_cast<std::array<unsigned char, sizeof(T)>>(value);
    for (const unsigned char byte : bytes)
    {
      hash = (hash ^ byte) * FNV_PRIME;
    }
  }
  [[nodiscard]] std::uint64_t value() const { return hash; }

 private:
  std::uint64_t hash = FNV_OFFSET_BASIS;
};

std::uint64_t fingerprint(const MatchEngine& engine)
{
  Fingerprint print;
  print.add(engine.getSimulatedSteps());
  print.add(engine.getHomeScore());
  print.add(engine.getAwayScore());
  for (const MatchEvent& event : engine.getEvents())
  {
    print.add(event.type);
    print.add(event.period);
    print.add(event.timeMinute);
    print.add(event.primaryPlayerId);
    print.add(event.secondaryPlayerId);
    print.add(event.position.x);
    print.add(event.position.y);
    print.add(event.xg);
  }
  for (const PlayerMatchStats& stats : engine.getPlayerStats())
  {
    print.add(stats.playerId);
    print.add(stats.passesAttempted);
    print.add(stats.passesCompleted);
    print.add(stats.touches);
    print.add(stats.tacklesAttempted);
    print.add(stats.distanceMetres);
    print.add(stats.rating);
  }
  for (const MatchPlayer& player : engine.getPlayers())
  {
    print.add(player.position.x);
    print.add(player.position.y);
  }
  return print.value();
}

std::unique_ptr<MatchEngine> makeEngine(const Team& home, const Team& away,
                                        const StatsConfig& config,
                                        std::uint32_t seed)
{
  auto engine = std::make_unique<MatchEngine>(
      home.getLineup(), away.getLineup(), home.getStrategy(),
      away.getStrategy(), config, seed, sandboxHandshake(),
      sandboxPossession());
  applyHomeAdvantage(*engine, sandboxHomeAdvantage());
  engine->setTeamNames(home.getName(), away.getName());
  engine->setBrain(true, sandboxBrains()[0]);
  engine->setBrain(false, sandboxBrains()[1]);
  engine->setBody(true, sandboxBodies()[0]);
  engine->setBody(false, sandboxBodies()[1]);
  engine->setReferee(sandboxReferee());
  engine->setManager(true, sandboxManagers()[0]);
  engine->setManager(false, sandboxManagers()[1]);
  engine->setBallPhysics(sandboxBallPhysics());
  engine->setOutOfPlay(sandboxOutOfPlay());
  return engine;
}

double secondsSince(Clock::time_point start)
{
  return std::chrono::duration<double>(Clock::now() - start).count();
}

/** A drill engine: everyone removed but `keep`, ball loose or carried. */
std::unique_ptr<MatchEngine> drillEngine(const Team& home, const Team& away,
                                         const StatsConfig& config,
                                         std::uint32_t seed,
                                         const std::vector<PlayerID>& keep)
{
  auto engine = makeEngine(home, away, config, seed);
  engine->setDrillRules({.offside = false, .minimumPlayers = false});
  for (const MatchPlayer& slot : engine->getPlayers())
  {
    if ((slot.player != nullptr) &&
        std::ranges::find(keep, slot.player->getId()) == keep.end())
    {
      (void)engine->removeBeforeKickOff(slot.player->getId());
    }
  }
  return engine;
}

/** Sets a drill's parameter by name; false if the drill has none of it. */
bool setParameter(Drill& drill, std::string_view name, float value)
{
  bool found = false;
  for (const DrillParameter& parameter : drill.parameters())
  {
    if (std::string_view(parameter.name) == name)
    {
      *parameter.value = value;
      found = true;
    }
  }
  return found;
}

/** Every parameter's value, in order. */
std::vector<float> parameterValues(Drill& drill)
{
  std::vector<float> values;
  for (const DrillParameter& parameter : drill.parameters())
  {
    values.push_back(*parameter.value);
  }
  return values;
}

/** The last metric of that name, or `fallback`. */
double metricValue(const std::vector<DrillMetric>& metrics,
                   std::string_view name, double fallback)
{
  double value = fallback;
  for (const DrillMetric& metric : metrics)
  {
    if (metric.name == name)
    {
      value = metric.value;
    }
  }
  return value;
}

/** One sprint drill run: the step he arrived and the final checksum. */
std::pair<std::uint64_t, std::uint64_t> sprintRun(const Team& home,
                                                  const Team& away,
                                                  const StatsConfig& config,
                                                  const Player* runner,
                                                  SandboxRecorder* recorder)
{
  auto engine = drillEngine(home, away, config, SPRINT_SEED, {runner->getId()});
  engine->setRecorder(recorder);
  MatchScenario scenario;
  scenario.players.push_back({.playerId = runner->getId(),
                              .position = PITCH.at(SPRINT_START_X, MIDDLE_Y),
                              .makingRun = false,
                              .facing = std::nullopt});
  scenario.ballPosition = PITCH.at(BALL_CLEAR, BALL_CLEAR);
  const bool started = engine->startDrill(scenario);
  const PitchPoint target = PITCH.at(SPRINT_TARGET_X, MIDDLE_Y);
  engine->setDrillTarget(runner->getId(), target, true);
  std::uint64_t arrived = 0;
  for (int tick = 0; started && tick < SPRINT_TICKS && arrived == 0; ++tick)
  {
    engine->advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    for (const MatchPlayer& slot : engine->getPlayers())
    {
      const bool there = std::hypot(slot.position.x - target.x,
                                    slot.position.y - target.y) < 1.0F;
      if (slot.player == runner && there)
      {
        arrived = engine->getSimulatedSteps();
      }
    }
  }
  return std::pair{arrived, SandboxRecorder::stateChecksum(*engine)};
}

/** Sprint: one AI runner, held to a target ~31 m away, flat out. */
bool checkDrillSprint(const Team& home, const Team& away,
                      const StatsConfig& config)
{
  const Player* runner = home.getLineup().getOutfieldPlayers().front().player;
  SandboxRecorder recorded;
  const auto first = sprintRun(home, away, config, runner, &recorded);
  const auto second = sprintRun(home, away, config, runner, nullptr);
  bool rewound = false;
  if (first.first > 2)
  {
    const auto rebuilt = MatchRewind::rebuild(recorded, first.first / 2);
    rewound = rebuilt.engine && !rebuilt.divergedAt;
  }
  std::cout << std::format(
      "\nDrill sprint: 31.5 m in {:.1f} s ({}), same twice: {}, rewinds "
      "exactly: {}\n",
      static_cast<double>(first.first) *
          MatchTuning::Timing::FIXED_STEP_SECONDS,
      runner->getName(), first == second ? "yes" : "NO",
      rewound ? "yes" : "NO");
  return first.first > 0 && first == second && rewound;
}

/** How a forced shot ended, from one step's events; none while it's live. */
std::optional<std::string> shotEnding(const MatchEngine& engine,
                                      const Player* keeper)
{
  for (const MatchEvent& event : engine.getEvents())
  {
    if (event.type == MatchEventType::GOAL ||
        event.type == MatchEventType::SAVE ||
        event.type == MatchEventType::WOODWORK ||
        event.type == MatchEventType::SHOT_BLOCKED)
    {
      return std::string(matchEventTypeName(event.type));
    }
    // The engine logs no "off target" event: a miss goes out of play.
    if (event.type == MatchEventType::GOAL_KICK ||
        event.type == MatchEventType::CORNER ||
        event.type == MatchEventType::THROW_IN)
    {
      return std::string("off target");
    }
  }
  // A weak miss the keeper simply picks up is logged as nothing either.
  if (engine.getPossessionAttachment() == keeper &&
      !engine.getBall().possessionState().isShot)
  {
    return std::string("off target (gathered)");
  }
  return std::nullopt;
}

/** One forced shot from the edge of the box: how it ended. */
std::string forcedShot(const Team& home, const Team& away,
                       const StatsConfig& config, std::uint32_t seed)
{
  const Player* shooter = home.getLineup().getOutfieldPlayers().back().player;
  const Player* keeper = away.getLineup().getGoalkeeper();
  auto engine = drillEngine(home, away, config, seed,
                            {shooter->getId(), keeper->getId()});
  MatchScenario scenario;
  scenario.players.push_back({.playerId = shooter->getId(),
                              .position = PITCH.at(SHOOTER_X, SHOOTER_Y),
                              .makingRun = false,
                              .facing = std::nullopt});
  scenario.players.push_back({.playerId = keeper->getId(),
                              .position = PITCH.at(KEEPER_X, MIDDLE_Y),
                              .makingRun = false,
                              .facing = std::nullopt});
  scenario.carrierId = shooter->getId();
  scenario.ballPosition = PITCH.at(SHOOTER_X, SHOOTER_Y);
  if (!engine->startDrill(scenario))
  {
    return "not started";
  }
  engine->forceShot(shooter->getId());
  // The run ends at the first stoppage or once the shot is dealt with.
  for (int tick = 0; tick < SHOT_TICKS; ++tick)
  {
    engine->advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    if (auto ending = shotEnding(*engine, keeper))
    {
      return *ending;
    }
  }
  return "no outcome";
}

/** Forced shot: a striker on the edge of the box against the keeper. */
bool checkDrillForcedShot(const Team& home, const Team& away,
                          const StatsConfig& config)
{
  std::map<std::string, int> outcomes;
  for (std::uint32_t seed = 1; seed <= SHOT_SEEDS; ++seed)
  {
    ++outcomes[forcedShot(home, away, config, seed)];
  }
  const bool repeatable =
      forcedShot(home, away, config, 42) == forcedShot(home, away, config, 42);
  std::string summary;
  for (const auto& [outcome, count] : outcomes)
  {
    summary += std::format("{} {}  ", outcome, count);
  }
  std::cout << std::format(
      "Drill forced shot (100 seeds, {} v the keeper): {}· same twice: {}\n",
      home.getLineup().getOutfieldPlayers().back().player->getName(), summary,
      repeatable ? "yes" : "NO");
  return repeatable && !outcomes.contains("not started") &&
         !outcomes.contains("no outcome");
}

/** A drill run to its end: its report (running tallies left out) and, when
 * asked, whether its recording rewinds exactly from the middle. */
std::pair<std::vector<std::string>, bool> drillToEnd(Drill& drill,
                                                     const StatsConfig& config,
                                                     bool rewind)
{
  DrillRun run(drill, config);
  while (run.step())
  {
  }
  bool exact = true;
  if (rewind)
  {
    const std::uint64_t middle = run.engine().getSimulatedSteps() / 2;
    const auto rebuilt = MatchRewind::rebuild(run.recorder(), middle);
    exact = rebuilt.engine && !rebuilt.divergedAt;
  }
  // Running tallies over several runs differ by design: left out.
  std::vector<std::string> report = drill.report();
  std::erase_if(report, [](const std::string& line)
                { return line.starts_with("These settings so far"); });
  return std::pair{report, exact};
}

/** One drill variant run twice: same report, exact rewind, and finished.
 * None when the drill has no such variant. */
std::optional<bool> checkDrillVariant(Drill& drill, const StatsConfig& config,
                                      float withBall, float path)
{
  const bool hasPath = setParameter(drill, "Who runs", path);
  const bool hasBall = setParameter(drill, "With the ball", withBall);
  if ((!hasPath && path > 0.0F) || (!hasBall && withBall > 0.0F))
  {
    return std::nullopt;
  }
  const auto first = drillToEnd(drill, config, true);
  const auto second = drillToEnd(drill, config, false);
  const bool same = first.first == second.first && first.second;
  // The headline result: the total or finish line (the run completed), else
  // the last line.
  std::string result = first.first.empty() ? "" : first.first.back();
  bool completed = false;
  for (const std::string& line : first.first)
  {
    if (line.starts_with("TOTAL") || line.starts_with("FINISH") ||
        line.starts_with("Decrement") || line.starts_with("OUTCOME"))
    {
      result = line;
      completed = true;
    }
  }
  std::cout << std::format(
      "Drill {}{}{}: {}  ·  same twice and rewinds: {}{}\n", drill.name(),
      hasPath ? " (AI)" : "", withBall > 0.0F ? " with the ball" : "", result,
      same ? "yes" : "NO", completed ? "" : "  ·  DID NOT FINISH");
  if (hasBall)
  {
    setParameter(drill, "With the ball", 0.0F);
  }
  return same && completed;
}

/** The drill framework: every drill, run twice to its end through DrillRun,
 * reports the same and its run rewinds exactly, by the AI (the headless
 * movement path), and the movement drills again with the ball. */
bool checkDrillFramework(const StatsConfig& config)
{
  bool framework = true;
  for (const auto& drill : makeDrills())
  {
    for (const float withBall : {0.0F, 1.0F})
    {
      for (const float path : {0.0F})
      {
        const std::optional<bool> ok =
            checkDrillVariant(*drill, config, withBall, path);
        framework = framework && ok.value_or(true);
      }
    }
  }
  return framework;
}

/** Measure mode: a sweep gives the same results as running the drill
 * directly with those settings, and leaves the drill's settings as found. */
bool checkDrillMeasure(const StatsConfig& config)
{
  auto drills = makeDrills();
  Drill& sprint = *drills.front();
  const std::vector<float> before = parameterValues(sprint);
  MeasureSpec spec;
  spec.sweep = {.parameter = "Pace", .values = {SLOW_PACE, FAST_PACE}};
  spec.by = MeasureAxis{.parameter = "With the ball", .values = {0.0F, 1.0F}};
  spec.repeats = 2;
  DrillMeasure measure(sprint, config, spec);
  while (measure.work(1.0))
  {
  }
  const std::vector<float> after = parameterValues(sprint);
  // The same point run directly: Pace 70 with the ball, seed 2.
  setParameter(sprint, "Pace", FAST_PACE);
  setParameter(sprint, "With the ball", 1.0F);
  sprint.seed = 2;
  double direct = -1.0;
  {
    DrillRun run(sprint, config, false);
    while (run.step())
    {
    }
    direct = metricValue(sprint.metrics(), "Finish (s)", direct);
  }
  double measured = NOT_MEASURED;
  for (const MeasureRun& run : measure.runs())
  {
    if (run.x == FAST_PACE && run.by == 1.0F && run.seed == 2)
    {
      measured = metricValue(run.metrics, "Finish (s)", measured);
    }
  }
  std::cout << std::format(
      "Drill measure (sprint, Pace × with the ball × 2 seeds): {} runs, "
      "matches a direct run: {}, settings restored: {}\n",
      measure.runs().size(), direct == measured ? "yes" : "NO",
      before == after ? "yes" : "NO");
  return measure.runs().size() == MEASURE_RUNS && before == after &&
         direct == measured;
}

/** A shot map run to its end: shots, goals and xG per spot. */
std::vector<std::tuple<int, int, double>> shotMapCells(
    Drill& drill, const StatsConfig& config, const ShotMapSpec& spec)
{
  ShotMap map(drill, config, spec);
  while (map.work(1.0))
  {
  }
  std::vector<std::tuple<int, int, double>> cells;
  for (const ShotMapCell& cell : map.cells())
  {
    cells.emplace_back(cell.shots, cell.goals, cell.xg);
  }
  return cells;
}

/** Shot map: a small grid twice gives the same goals and xG per spot. */
bool checkDrillShotMap(const StatsConfig& config)
{
  auto drills = makeDrills();
  Drill* shotDrill = nullptr;
  for (const auto& drill : drills)
  {
    if (drill->offersShotMap())
    {
      shotDrill = drill.get();
    }
  }
  if (shotDrill == nullptr)
  {
    return false;
  }
  ShotMapSpec spec;
  spec.lineFrom = SHOT_MAP_NEAREST;
  spec.lineTo = SHOT_MAP_FURTHEST;
  spec.lineCount = 3;
  spec.sideFrom = -SHOT_MAP_SIDE;
  spec.sideTo = SHOT_MAP_SIDE;
  spec.sideCount = 3;
  spec.repeats = SHOT_MAP_REPEATS;
  const auto first = shotMapCells(*shotDrill, config, spec);
  const bool ok = first.size() == SHOT_MAP_CELLS &&
                  first == shotMapCells(*shotDrill, config, spec);
  int shots = 0;
  int goals = 0;
  for (const auto& [cellShots, cellGoals, xg] : first)
  {
    shots += cellShots;
    goals += cellGoals;
  }
  std::cout << std::format(
      "Drill shot map (3 × 3 spots × 10): {} shots, {} goals, same twice: "
      "{}\n",
      shots, goals, ok ? "yes" : "NO");
  return ok;
}

/** Results of the drill checks (engine drill support). Every check runs and
 * reports, whatever the ones before it found. */
bool runDrillChecks(const Team& home, const Team& away,
                    const StatsConfig& config)
{
  const bool sprintOk = checkDrillSprint(home, away, config);
  const bool shotOk = checkDrillForcedShot(home, away, config);
  const bool framework = checkDrillFramework(config);
  const bool measureOk = checkDrillMeasure(config);
  const bool shotMapOk = checkDrillShotMap(config);
  return sprintOk && shotOk && framework && measureOk && shotMapOk;
}

/** One registry row complete (a physical range holding ratings 0 to 1) and
 * its default recipe summing to 1. */
bool derivedRowOk(std::size_t index)
{
  const DerivedStatInfo& info = DERIVED_STATS.at(index);
  bool ok = !info.name.empty() && info.label.size() == 3 &&
            !info.description.empty() &&
            (info.unit.empty() || info.minValue < info.maxValue);
  // The safe range holds the whole line from rating 0 to 1, so the clamp
  // never flattens a real rating.
  if (!info.unit.empty())
  {
    const float slope =
        (info.highValue - info.lowValue) / (info.highRating - info.lowRating);
    const float atZero = info.lowValue - (slope * info.lowRating);
    const float atOne = atZero + slope;
    ok = ok && std::min(atZero, atOne) >= info.minValue &&
         std::max(atZero, atOne) <= info.maxValue;
  }
  const DerivedRecipe& recipe = DEFAULT_DERIVED_RECIPES.at(index);
  float total = 0.0F;
  for (const float weight : recipe.weights)
  {
    total += weight;
  }
  for (const float weight : recipe.futureWeights)
  {
    total += weight;
  }
  return ok && std::abs(total - 1.0F) < WEIGHT_SUM_TOLERANCE &&
         recipe.spread >= 0.0F && recipe.spread <= MAX_RECIPE_SPREAD;
}

/** The layers: every future game stat before any match-engine-only stat (so
 * deriving in order works); a future stat reads current stats only; a
 * match-engine-only stat reads current and future stats, never another
 * match-engine-only stat. Counts each layer's stats into `layerCounts`. */
bool derivedLayersOk(std::array<int, 2>& layerCounts)
{
  bool layersOk = true;
  for (std::size_t index = 0; index < DERIVED_STAT_COUNT; ++index)
  {
    const DerivedLayer layer = DERIVED_STATS.at(index).layer;
    ++layerCounts.at(static_cast<std::size_t>(layer));
    if (index > 0 && layer == DerivedLayer::FUTURE_GAME_STAT &&
        DERIVED_STATS.at(index - 1).layer != DerivedLayer::FUTURE_GAME_STAT)
    {
      layersOk = false;
    }
    for (std::size_t read = 0; read < DERIVED_STAT_COUNT; ++read)
    {
      const bool reads =
          DEFAULT_DERIVED_RECIPES.at(index).futureWeights.at(read) != 0.0F;
      layersOk =
          layersOk && (!reads || (layer == DerivedLayer::MATCH_ENGINE_ONLY &&
                                  DERIVED_STATS.at(read).layer ==
                                      DerivedLayer::FUTURE_GAME_STAT));
    }
  }
  return layersOk;
}

/** One handshake's derived stats over the checked players. */
struct HandshakeDerivedStats
{
  std::array<double, DERIVED_STAT_COUNT> sums{};
  std::size_t players = 0;
  // Jumping reach's spread in metres, and how many are held at the ends of
  // its scale.
  float reachLow = std::numeric_limits<float>::max();
  float reachHigh = 0.0F;
  std::size_t reachHeld = 0;
  bool valuesOk = true;

  /** Every derived stat finite and in range, summed for the means. */
  void add(const MatchPlayer& player)
  {
    ++players;
    for (std::size_t index = 0; index < DERIVED_STAT_COUNT; ++index)
    {
      const auto stat = static_cast<DerivedStat>(index);
      const float rating = player.derivedStat(stat);
      const DerivedStatInfo& info = DERIVED_STATS.at(index);
      valuesOk =
          valuesOk && std::isfinite(rating) && rating >= 0.0F && rating <= 1.0F;
      if (!info.unit.empty())
      {
        const float value = derivedPhysicalValue(stat, rating);
        valuesOk = valuesOk && value >= info.minValue && value <= info.maxValue;
      }
      sums.at(index) += rating;
    }
    const float reach = player.derivedStat(DerivedStat::JUMPING_REACH);
    const float metres =
        derivedPhysicalValue(DerivedStat::JUMPING_REACH, reach);
    reachLow = std::min(reachLow, metres);
    reachHigh = std::max(reachHigh, metres);
    if (reach <= 0.0F || reach >= 1.0F)
    {
      ++reachHeld;
    }
  }

  /** The mean ratings line for this handshake. */
  [[nodiscard]] std::string means(HandshakeId handshake) const
  {
    std::string line = std::format("\n  {:<9}", handshakeName(handshake));
    for (std::size_t index = 0; index < DERIVED_STAT_COUNT; ++index)
    {
      line += std::format(" {} {:.0f}", DERIVED_STATS.at(index).label,
                          (players != 0U) ? 100.0 * sums.at(index) /
                                                static_cast<double>(players)
                                          : 0.0);
    }
    if (players != 0U)
    {
      line += std::format(" (jumping reach {:.2f}-{:.2f} m, {} at an end)",
                          reachLow, reachHigh, reachHeld);
    }
    return line;
  }
};

/** Every player of the checked clubs under one handshake. */
HandshakeDerivedStats derivedStatsUnder(const std::vector<const Team*>& clubs,
                                        const StatsConfig& config,
                                        HandshakeId handshake)
{
  HandshakeDerivedStats stats;
  for (std::size_t pair = 0; pair + 1 < clubs.size(); pair += 2)
  {
    const MatchEngine engine(
        clubs[pair]->getLineup(), clubs[pair + 1]->getLineup(),
        clubs[pair]->getStrategy(), clubs[pair + 1]->getStrategy(), config, 1U,
        handshake);
    for (const MatchPlayer& player : engine.getPlayers())
    {
      stats.add(player);
    }
  }
  return stats;
}

/** Derived stats: every registry row complete (a physical range holding
 * ratings 0 to 1), every recipe summing to 1, and every player of the checked
 * clubs finite and in range under each handshake. */
bool runDerivedStatChecks(const std::vector<const Team*>& clubs,
                          const StatsConfig& config)
{
  bool registryOk = true;
  for (std::size_t index = 0; index < DERIVED_STAT_COUNT; ++index)
  {
    registryOk = derivedRowOk(index) && registryOk;
  }
  std::array<int, 2> layerCounts{};
  const bool layersOk = derivedLayersOk(layerCounts);
  registryOk = registryOk && layersOk;

  bool valuesOk = true;
  std::size_t checked = 0;
  std::string means;
  for (const HandshakeId handshake : ALL_HANDSHAKES)
  {
    const HandshakeDerivedStats stats =
        derivedStatsUnder(clubs, config, handshake);
    valuesOk = valuesOk && stats.valuesOk;
    checked += stats.players;
    means += stats.means(handshake);
  }
  std::cout << std::format(
      "\nDerived stats: {} kinds ({} future game stats, {} match engine "
      "only; layers kept: {}), registry complete: {}, {} player loads in "
      "range: {}; mean ratings:{}\n",
      DERIVED_STAT_COUNT, layerCounts[0], layerCounts[1],
      layersOk ? "yes" : "NO", registryOk ? "yes" : "NO", checked,
      valuesOk ? "yes" : "NO", means);
  return registryOk && valuesOk;
}

/** Possession: one match for every pairing of bodies, stepped one fixed step
 * at a time, with the ball's ownership state coherent after every step (a
 * ball in a first touch has an owner, in open play). */
bool runPossessionChecks(const std::vector<const Team*>& clubs,
                         const StatsConfig& config)
{
  bool consistent = true;
  std::size_t steps = 0;
  std::size_t pairing = 0;
  for (const BodyId homeBody : ALL_BODIES)
  {
    for (const BodyId awayBody : ALL_BODIES)
    {
      const std::size_t pair = (pairing++ * 2) % clubs.size();
      auto engine =
          makeEngine(*clubs[pair], *clubs[(pair + 1) % clubs.size()], config,
                     static_cast<std::uint32_t>(POSSESSION_SEED + pair));
      engine->setBody(true, homeBody);
      engine->setBody(false, awayBody);
      while (engine->getState() != MatchState::FULL_TIME &&
             steps < MAX_MATCH_STEPS)
      {
        (void)engine->advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
        ++steps;
        consistent = consistent && engine->possessionConsistent();
      }
    }
  }
  std::cout << std::format(
      "\nPossession: {} body pairings, {} steps, ownership coherent every "
      "step: {}\n",
      pairing, steps, consistent ? "yes" : "NO");
  return consistent;
}

// First touch checks: matches per set-up, the step, and the budgets.
constexpr int TOUCH_MATCHES = 3;
constexpr float TOUCH_STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
// Rounding slack for one step's travel, in metres.
constexpr float TOUCH_SLACK_METRES = 0.02F;
// A touch that let the ball this far from him counts as heavy.
constexpr float HEAVY_TOUCH_GAP_METRES = 2.0F;

/** Distance between two pitch points, in metres. */
float pointGap(PitchPoint from, PitchPoint to)
{
  const float x = to.x - from.x;
  const float y = to.y - from.y;
  return std::sqrt((x * x) + (y * y));
}

/** The same for the change between two steps (ball from holder). */
float offsetGap(Vector2F from, Vector2F to)
{
  const float x = to.x - from.x;
  const float y = to.y - from.y;
  return std::sqrt((x * x) + (y * y));
}

float speedOf(Vector2F velocity)
{
  return std::sqrt((velocity.x * velocity.x) + (velocity.y * velocity.y));
}

/** How the touches ended: one for all touches, one for pass receptions. */
struct TouchOutcomes
{
  std::size_t started = 0;
  std::size_t collected = 0;
  std::size_t released = 0;
  std::size_t taken = 0;
  /** Someone else played it, then the same player touched it again. */
  std::size_t replayed = 0;
  std::size_t stopped = 0;
};

/** One brain and body pairing's first touch totals. */
struct TouchSetup
{
  const char* name = "";
  BrainId brain = BrainId::AARDVARK;
  BodyId body = BodyId::AARDVARK;
  std::array<TouchOutcomes, 2> outcomes{};
  double collectSeconds = 0.0;
  double contactGapSum = 0.0;
  std::size_t intoStride = 0;
  /** Touches whose ball got more than HEAVY_TOUCH_GAP_METRES from him. */
  std::size_t heavy = 0;
  /** Sums of the touches' absolute errors (radians, share). */
  double angleErrorSum = 0.0;
  double weightErrorSum = 0.0;
  int angleFailed = 0;
  int weightFailed = 0;
  std::uint32_t waitsMissed = 0;
  MatchEngine::FirstTouchWaitLosses losses;
  std::uint64_t dribbleTouches = 0;
  double dribbleContactGapSum = 0.0;
  std::uint64_t dribbleFailed = 0;
  float worstExcess = 0.0F;
  float worstCollectJump = 0.0F;
  /** Attempted, completed, intercepted. */
  std::array<int, 3> passes{};
};

/** That player's slot in the engine, if he's on. */
const MatchPlayer* slotOfPlayer(const MatchEngine& engine, const Player* player)
{
  for (const MatchPlayer& slot : engine.getPlayers())
  {
    if (player != nullptr && slot.player == player)
    {
      return &slot;
    }
  }
  return nullptr;
}

/** His touches so far this match; 0 if he isn't on. */
int touchesOf(const MatchEngine& engine, const Player* player)
{
  const MatchPlayer* holder = slotOfPlayer(engine, player);
  return (holder != nullptr)
             ? engine.getPlayerStats()[holder->statsIndex].touches
             : 0;
}

/** Adds one match's lost-wait split to a running total. */
void addWaitLosses(MatchEngine::FirstTouchWaitLosses& sum,
                   const MatchEngine::FirstTouchWaitLosses& split)
{
  sum.deadZone += split.deadZone;
  sum.outOfReach += split.outOfReach;
  sum.sameStep += split.sameStep;
  sum.intentChanged += split.intentChanged;
  sum.bumped += split.bumped;
  sum.facingErrorSum += split.facingErrorSum;
  sum.facingErrorMax = std::max(sum.facingErrorMax, split.facingErrorMax);
  sum.facingSamples += split.facingSamples;
  sum.unchanged += split.unchanged;
  sum.unchangedBumped += split.unchangedBumped;
  sum.unchangedFacingErrorSum += split.unchangedFacingErrorSum;
  sum.unchangedFacingErrorMax =
      std::max(sum.unchangedFacingErrorMax, split.unchangedFacingErrorMax);
  sum.unchangedFacingSamples += split.unchangedFacingSamples;
  sum.carriedOn += split.carriedOn;
  sum.carriedOnStillLost += split.carriedOnStillLost;
}

/** Follows one match's touches step by step into a set-up's totals. */
class FirstTouchTracker
{
 public:
  FirstTouchTracker(const MatchEngine& match, TouchSetup& totals,
                    std::uint32_t matchSeed)
      : engine(&match),
        setup(&totals),
        seed(matchSeed),
        lastPosition(match.getBall().position),
        lastLaunchSpeed(match.getBall().touchLaunchSpeed),
        lastPathSince(match.getPossessionState().pathSince)
  {
  }

  /** After a call to advance() that ran `stepCount` steps (one or more). */
  void step(float stepCount)
  {
    const MatchBall& ball = engine->getBall();
    // A first touch runs from the touch to collection (then he dribbles the
    // same claimed ball).
    const bool inTouch = engine->hasTouchClaim() && !engine->isTouchCollected();
    const bool claimed = engine->hasTouchClaim();
    // The same touch only while its owner has not re-claimed the ball:
    // someone else can play it and he touch it again within one step.
    const bool sameTouch = wasInTouch && inTouch &&
                           engine->getPossessionAttachment() == owner &&
                           touchesOf(*engine, owner) == ownerTouches;
    if (wasClaimed && claimed &&
        engine->getPossessionAttachment() == claimOwner &&
        touchesOf(*engine, claimOwner) == claimTouches)
    {
      checkClaimMotion(ball, stepCount);
    }
    if (!sameTouch && wasInTouch)
    {
      touchEnded(ball, stepCount);
    }
    if (inTouch && !sameTouch)
    {
      touchStarted(ball);
    }
    remember(ball, inTouch, claimed);
  }

 private:
  /** Rolling on to him, or dribbled: nothing but the ball's own physics
   * moved it (a touch changes its velocity, not where it is). Left out: a
   * step in which someone else played it and he won it back (several touches
   * in one step). A touch during the step may have sent it faster than
   * either end shows (a hop off it then bounces pace away): the pace it left
   * the touch at counts too. Rolling resistance takes up to this much off by
   * the step's end. */
  void checkClaimMotion(const MatchBall& ball, float stepCount)
  {
    const std::uint64_t steps = engine->getSimulatedSteps();
    using B = MatchTuning::Ball;
    // A touch resets his cooldown (its length is by his Dribbling): it is
    // above where counting down would have left it.
    const bool touchedThisStep =
        ball.ownTouchCooldown >
        std::max(0.0F, lastCooldown - (TOUCH_STEP * stepCount)) + 1e-4F;
    const float fastest =
        std::max({lastSpeed, speedOf(ball.velocity),
                  touchedThisStep ? ball.touchLaunchSpeed : 0.0F});
    const float seconds = TOUCH_STEP * stepCount;
    const float rollingLoss =
        (B::ROLLING_DECELERATION + B::ROLLING_VISCOUS_PER_SECOND * fastest) *
        seconds;
    const float allowed =
        ((fastest + rollingLoss) * seconds) + TOUCH_SLACK_METRES;
    const float excess = pointGap(lastPosition, ball.position) - allowed;
    if (excess > setup->worstExcess && excess > 0.0F)
    {
      std::cerr << std::format(
          "Motion budget diagnostic: seed {} brain {} body {} tick {} "
          "owner {} "
          "moved {:.6f} allowed {:.6f} speeds {:.6f}/{:.6f} launch "
          "{:.6f}/{:.6f} "
          "cooldown {:.6f}/{:.6f} touched {} path {:.6f}/{:.6f} "
          "position {:.9f},{:.9f}/{:.9f},{:.9f} sent {:.9f},{:.9f}\n",
          seed, static_cast<unsigned>(setup->brain),
          static_cast<unsigned>(setup->body), steps,
          (claimOwner != nullptr) ? claimOwner->getId() : 0,
          pointGap(lastPosition, ball.position), allowed, lastSpeed,
          speedOf(ball.velocity), lastLaunchSpeed, ball.touchLaunchSpeed,
          lastCooldown, ball.ownTouchCooldown, touchedThisStep, lastPathSince,
          engine->getPossessionState().pathSince, lastPosition.x,
          lastPosition.y, ball.position.x, ball.position.y, ball.sentFrom.x,
          ball.sentFrom.y);
    }
    setup->worstExcess = std::max(setup->worstExcess, excess);
  }

  /** Counts how a touch ended, for all touches and, if it was one, for
   * pass receptions. */
  void recordEnd(std::size_t TouchOutcomes::* field)
  {
    ++(setup->outcomes[0].*field);
    if (reception)
    {
      ++(setup->outcomes[1].*field);
    }
  }

  /** The touch followed until last step is over: how it ended. */
  void touchEnded(const MatchBall& ball, float stepCount)
  {
    if (lastPeakGap > HEAVY_TOUCH_GAP_METRES)
    {
      ++setup->heavy;
    }
    const Player* attached = engine->getPossessionAttachment();
    if (engine->getState() != MatchState::PLAYING)
    {
      recordEnd(&TouchOutcomes::stopped);
    }
    else if (attached == owner && touchesOf(*engine, owner) != ownerTouches)
    {
      recordEnd(&TouchOutcomes::replayed);
    }
    else if (attached == owner)
    {
      recordEnd(&TouchOutcomes::collected);
      setup->collectSeconds += engine->getSimulatedSeconds() - touchStart;
      collectJump(ball, stepCount);
    }
    else if (attached == nullptr)
    {
      recordEnd(&TouchOutcomes::released);
    }
    else
    {
      recordEnd(&TouchOutcomes::taken);
    }
  }

  /** How far the ball jumped relative to him as the dribble took over,
   * beyond what its own speed relative to him explains. */
  void collectJump(const MatchBall& ball, float stepCount)
  {
    const MatchPlayer* holder = slotOfPlayer(*engine, owner);
    if ((holder == nullptr) || stepCount != 1.0F)
    {
      return;
    }
    const Vector2F offset = ball.position - holder->position;
    setup->worstCollectJump =
        std::max(setup->worstCollectJump,
                 offsetGap(lastOffset, offset) -
                     ((lastSpeed + speedOf(holder->velocity)) * TOUCH_STEP));
  }

  /** A new touch: who, whether it's a pass reception, and its quality. */
  void touchStarted(const MatchBall& ball)
  {
    owner = engine->getPossessionAttachment();
    ownerTouches = touchesOf(*engine, owner);
    reception = engine->getPendingPasser() != nullptr;
    touchStart = engine->getSimulatedSeconds();
    ++setup->outcomes[0].started;
    if (reception)
    {
      ++setup->outcomes[1].started;
    }
    setup->contactGapSum += ball.touchContactGap;
    setup->angleErrorSum += std::abs(ball.touchAngleError);
    setup->weightErrorSum += std::abs(ball.touchWeightError);
    setup->angleFailed += ball.touchAngleFailed ? 1 : 0;
    setup->weightFailed += ball.touchWeightFailed ? 1 : 0;
    if (ball.touchKind == TouchIntent::Kind::INTO_STRIDE)
    {
      ++setup->intoStride;
    }
  }

  /** What the next step compares against. */
  void remember(const MatchBall& ball, bool inTouch, bool claimed)
  {
    wasInTouch = inTouch;
    wasClaimed = claimed;
    claimOwner = claimed ? engine->getPossessionAttachment() : nullptr;
    claimTouches = touchesOf(*engine, claimOwner);
    lastPeakGap = inTouch ? ball.touchPeakGap : 0.0F;
    lastPosition = ball.position;
    lastSpeed = speedOf(ball.velocity);
    lastCooldown = ball.ownTouchCooldown;
    lastLaunchSpeed = ball.touchLaunchSpeed;
    lastPathSince = engine->getPossessionState().pathSince;
    if (const MatchPlayer* holder =
            inTouch ? slotOfPlayer(*engine, owner) : nullptr)
    {
      lastOffset = ball.position - holder->position;
    }
  }

  const MatchEngine* engine;
  TouchSetup* setup;
  std::uint32_t seed;
  bool wasInTouch = false;
  // The whole claim (first touch, then the dribble) for the physics test.
  bool wasClaimed = false;
  const Player* claimOwner = nullptr;
  int claimTouches = 0;
  bool reception = false;
  int ownerTouches = 0;
  float lastPeakGap = 0.0F;
  const Player* owner = nullptr;
  double touchStart = 0.0;
  PitchPoint lastPosition;
  Vector2F lastOffset{.x = 0.0F, .y = 0.0F};
  float lastSpeed = 0.0F;
  float lastCooldown = 0.0F;
  float lastLaunchSpeed;
  double lastPathSince;
};

/** One first touch match, played to full time into the set-up's totals. */
void runFirstTouchMatch(const std::vector<const Team*>& clubs,
                        const StatsConfig& config, TouchSetup& setup, int match)
{
  const std::size_t pair = (static_cast<std::size_t>(match) * 2) % clubs.size();
  const auto seed = static_cast<std::uint32_t>(TOUCH_SEED + match);
  auto engine =
      makeEngine(*clubs[pair], *clubs[(pair + 1) % clubs.size()], config, seed);
  for (const bool home : {true, false})
  {
    engine->setBrain(home, setup.brain);
    engine->setBody(home, setup.body);
  }
  FirstTouchTracker tracker(*engine, setup, seed);
  std::uint64_t lastSteps = engine->getSimulatedSteps();
  while (engine->getState() != MatchState::FULL_TIME)
  {
    (void)engine->advance(TOUCH_STEP);
    // advance() keeps exact time, so one call can run zero or two steps.
    const std::uint64_t steps = engine->getSimulatedSteps();
    if (steps == lastSteps)
    {
      continue;
    }
    const auto stepCount = static_cast<float>(steps - lastSteps);
    lastSteps = steps;
    tracker.step(stepCount);
  }
  setup.waitsMissed += engine->getFirstTouchWaitsMissed();
  addWaitLosses(setup.losses, engine->getFirstTouchWaitLosses());
  setup.dribbleTouches += engine->getDribbleTouches();
  setup.dribbleContactGapSum += engine->getDribbleContactGapSum();
  setup.dribbleFailed += engine->getDribbleFailedTouches();
  const MatchStats& stats = engine->getStats();
  setup.passes[0] += stats.homePassesAttempted + stats.awayPassesAttempted;
  setup.passes[1] += stats.homePassesCompleted + stats.awayPassesCompleted;
  for (const PlayerMatchStats& line : engine->getPlayerStats())
  {
    setup.passes[2] += line.interceptions;
  }
}

/** How a set of touches ended, as shares of those started. */
std::string describeTouches(const TouchOutcomes& counts)
{
  const auto share = [&counts](std::size_t part)
  {
    return counts.started ? 100.0 * static_cast<double>(part) /
                                static_cast<double>(counts.started)
                          : 0.0;
  };
  return std::format(
      "{} touches: collected {:.0f}%, released {:.0f}%, taken by an "
      "opponent {:.0f}%, played by another then touched again {:.1f}%, "
      "play stopped {:.0f}%",
      counts.started, share(counts.collected), share(counts.released),
      share(counts.taken), share(counts.replayed), share(counts.stopped));
}

/** A share of `whole` as a percentage; 0 when there is none. */
double percentOf(double part, double whole)
{
  return whole != 0.0 ? 100.0 * part / whole : 0.0;
}

/** The lost waits, split by why (printed only when some were lost). */
void reportLostWaits(const MatchEngine::FirstTouchWaitLosses& split)
{
  std::cout << std::format(
      "    lost waits: dead zone {}, out of reach {}; in the step he chose "
      "to wait {}; "
      "intent changed that step {}; bumped {}; facing vs the wait read's "
      "after a move: "
      "mean {:.0f}°, worst {:.0f}° ({} lost)\n",
      split.deadZone, split.outOfReach, split.sameStep, split.intentChanged,
      split.bumped,
      (split.facingSamples != 0U)
          ? split.facingErrorSum / split.facingSamples * DEGREES_PER_RADIAN
          : 0.0,
      split.facingErrorMax * DEGREES_PER_RADIAN_F, split.facingSamples);
  if (split.unchanged > 0)
  {
    std::cout << std::format(
        "    of which intent unchanged: {} (bumped {}; facing vs the "
        "read's mean {:.0f}°, "
        "worst {:.0f}°)\n",
        split.unchanged, split.unchangedBumped,
        (split.unchangedFacingSamples != 0U)
            ? split.unchangedFacingErrorSum / split.unchangedFacingSamples *
                  DEGREES_PER_RADIAN
            : 0.0,
        split.unchangedFacingErrorMax * DEGREES_PER_RADIAN_F);
  }
}

/** A Binturong-body set-up's touch report; true if it passes. */
bool reportTouchSetup(const TouchSetup& setup)
{
  const TouchOutcomes& all = setup.outcomes[0];
  const bool physical = setup.worstExcess <= 0.0F;
  const double started = std::max(1.0, static_cast<double>(all.started));
  const auto dribbles = static_cast<double>(setup.dribbleTouches);
  std::cout << std::format(
      "    all: {}\n    pass receptions: {}\n    met the ball {:.2f} m from "
      "his centre on average; into stride {:.0f}%; mean error: angle "
      "{:.1f}°, weight {:.2f} m/s; failed: angle {:.1f}%, weight {:.1f}%; "
      "heavy (ball over 2 m away) {:.1f}%; mean "
      "time to collect {:.2f} s\n    dribble touches {} (met {:.2f} m from "
      "his "
      "centre on average, {:.1f}% went wrong); claimed "
      "ball (touched, then dribbled) moved only under physics: {} (worst "
      "excess {:.3f} m); largest jump relative to him at collection {:.2f} "
      "m; waited and lost it: {}\n",
      describeTouches(all), describeTouches(setup.outcomes[1]),
      setup.contactGapSum / started,
      100.0 * static_cast<double>(setup.intoStride) / started,
      setup.angleErrorSum / started * DEGREES_PER_RADIAN,
      setup.weightErrorSum / started,
      100.0 * static_cast<double>(setup.angleFailed) / started,
      100.0 * static_cast<double>(setup.weightFailed) / started,
      100.0 * static_cast<double>(setup.heavy) / started,
      (all.collected != 0U)
          ? setup.collectSeconds / static_cast<double>(all.collected)
          : 0.0,
      setup.dribbleTouches,
      (setup.dribbleTouches != 0U) ? setup.dribbleContactGapSum / dribbles
                                   : 0.0,
      percentOf(static_cast<double>(setup.dribbleFailed), dribbles),
      physical ? "yes" : "NO", std::max(setup.worstExcess, 0.0F),
      std::max(setup.worstCollectJump, 0.0F), setup.waitsMissed);
  std::cout << std::format(
      "    waits carried on as read (the brain's move would have lost it): "
      "{}, of which "
      "still lost {}\n",
      setup.losses.carriedOn, setup.losses.carriedOnStillLost);
  if (setup.waitsMissed > 0)
  {
    reportLostWaits(setup.losses);
  }
  return physical && all.collected > 0 && setup.waitsMissed == 0;
}

/** First touch: every pairing of brain and body on the same fixtures. For
 * the Binturong bodies: how touches end and how long they take, that a touched
 * ball only ever moves under physics (never further in a step than its
 * speed carries it), how close to his centre he meets the ball, and that a
 * player who waits for it never loses it. Passes for all four, for
 * reference. */
bool runFirstTouchChecks(const std::vector<const Team*>& clubs,
                         const StatsConfig& config)
{
  std::array<TouchSetup, 4> setups{{
      {.name = "Aardvark brain + Aardvark body",
       .brain = BrainId::AARDVARK,
       .body = BodyId::AARDVARK,
       .losses = {}},
      {.name = "Aardvark brain + Binturong body",
       .brain = BrainId::AARDVARK,
       .body = BodyId::BINTURONG,
       .losses = {}},
      {.name = "Binturong brain + Aardvark body",
       .brain = BrainId::BINTURONG,
       .body = BodyId::AARDVARK,
       .losses = {}},
      {.name = "Binturong brain + Binturong body",
       .brain = BrainId::BINTURONG,
       .body = BodyId::BINTURONG,
       .losses = {}},
  }};
  for (TouchSetup& setup : setups)
  {
    for (int match = 0; match < TOUCH_MATCHES; ++match)
    {
      runFirstTouchMatch(clubs, config, setup, match);
    }
  }

  bool ok = true;
  std::cout << std::format("\nFirst touch ({} matches per set-up)\n",
                           TOUCH_MATCHES);
  for (const TouchSetup& setup : setups)
  {
    const double completion = (setup.passes[0] != 0)
                                  ? 100.0 * setup.passes[1] / setup.passes[0]
                                  : 0.0;
    std::cout << std::format(
        "  {}: passes {} attempted, {:.1f}% completed, "
        "{} intercepted\n",
        setup.name, setup.passes[0], completion, setup.passes[2]);
    if (setup.body == BodyId::BINTURONG)
    {
      ok = reportTouchSetup(setup) && ok;
    }
  }
  return ok;
}

/** One line on how a side-by-side replay went against its recording. */
std::string describeReplay(const Recordings::Replay& replay)
{
  if (!replay.match)
  {
    return "failed: " + replay.error;
  }
  if (!replay.divergedAt && !replay.firstDifferentDecision)
  {
    return "same as the recording";
  }
  return std::format("differs from tick {}", replay.divergedAt.value_or(0));
}
}  // namespace

namespace
{
constexpr int RESTART_MATCHES = 4;
// A line-up running this long has stuck.
constexpr float RESTART_STUCK_SECONDS = 10.0F;
constexpr std::array<std::string_view, 7> RESTART_NAMES{
    "kick-off", "", "throw-in", "goal kick", "corner", "free kick", "penalty"};

/** One out-of-play version's restarts over its matches. */
struct RestartTotals
{
  std::array<std::uint32_t, RESTART_NAMES.size()> taken{};
  MatchEngine::RestartLineUpStats all;
  float longest = 0.0F;
  int finished = 0;
};

/** The taker's state when a line-up has run on too long. */
void reportRestartStall(const MatchEngine& engine, std::uint32_t seed,
                        OutOfPlayId version)
{
  const auto& b = engine.getBall();
  for (const auto& player : engine.getPlayers())
  {
    if (player.player != engine.getPossessionAttachment())
    {
      continue;
    }
    std::cerr << std::format(
        "Restart stall diagnostic: seed {} out-of-play {} tick {} "
        "state {} "
        "ball {:.9f},{:.9f} taker {} at {:.9f},{:.9f} target "
        "{:.9f},{:.9f} "
        "speed {:.4f},{:.4f} fallen {} injured {} facing {:.5f} "
        "requested {} {:.5f} "
        "feet {:.4f} stamina {:.4f} reserve {:.4f}\n",
        seed, static_cast<unsigned>(version), engine.getSimulatedSteps(),
        static_cast<unsigned>(engine.getState()), b.position.x, b.position.y,
        player.player->getId(), player.position.x, player.position.y,
        player.movementTarget.x, player.movementTarget.y, player.velocity.x,
        player.velocity.y, player.fallen, player.isInjured, player.facingAngle,
        player.facingRequested, player.facingRequest, player.feetHeight,
        player.stamina, player.sprintReserve);
  }
}

/** Adds one match's line-up stats to a running total. */
void addRestartStats(MatchEngine::RestartLineUpStats& all,
                     const MatchEngine::RestartLineUpStats& stats)
{
  for (std::size_t index = 0; index < all.byRestart.size(); ++index)
  {
    all.byRestart.at(index) += stats.byRestart.at(index);
  }
  all.kicked += stats.kicked;
  all.turned += stats.turned;
  all.outOfTime += stats.outOfTime;
  all.outsideComfort += stats.outsideComfort;
  all.inDeadZone += stats.inDeadZone;
  all.secondsSum += stats.secondsSum;
  all.secondsMax = std::max(all.secondsMax, stats.secondsMax);
  all.gapSum += stats.gapSum;
  all.gapMax = std::max(all.gapMax, stats.gapMax);
}

/** One match with Binturong brains and bodies under that version, into its
 * totals: every restart taken, the longest line-up, and whether it ended. */
void runRestartMatch(const std::vector<const Team*>& clubs,
                     const StatsConfig& config, OutOfPlayId version, int match,
                     RestartTotals& totals)
{
  const std::size_t pair = (static_cast<std::size_t>(match) * 2) % clubs.size();
  const auto seed = static_cast<std::uint32_t>(TOUCH_SEED + match);
  auto engine =
      makeEngine(*clubs[pair], *clubs[(pair + 1) % clubs.size()], config, seed);
  for (const bool home : {true, false})
  {
    engine->setBrain(home, BrainId::BINTURONG);
    engine->setBody(home, BodyId::BINTURONG);
  }
  engine->setOutOfPlay(version);
  MatchState previous = engine->getState();
  std::size_t steps = 0;
  bool reportedStall = false;
  while (engine->getState() != MatchState::FULL_TIME && steps < MAX_MATCH_STEPS)
  {
    (void)engine->advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    ++steps;
    totals.longest =
        std::max(totals.longest, engine->getRestartLineUpSeconds());
    if (!reportedStall &&
        engine->getRestartLineUpSeconds() >= RESTART_STUCK_SECONDS)
    {
      reportedStall = true;
      reportRestartStall(*engine, seed, version);
    }
    // A restart taken: play resumes from it.
    const MatchState now = engine->getState();
    const auto index = static_cast<std::size_t>(previous);
    if (now == MatchState::PLAYING && previous != MatchState::PLAYING &&
        index < totals.taken.size())
    {
      ++totals.taken.at(index);
    }
    previous = now;
  }
  if (engine->getState() == MatchState::FULL_TIME)
  {
    ++totals.finished;
  }
  addRestartStats(totals.all, engine->getRestartLineUpStats());
}

/** One version's report line; true if it passes. */
bool reportRestartVersion(OutOfPlayId version, const RestartTotals& totals)
{
  const MatchEngine::RestartLineUpStats& all = totals.all;
  const double kicked = std::max(1.0, static_cast<double>(all.kicked));
  std::string byRestart;
  for (std::size_t index = 0; index < RESTART_NAMES.size(); ++index)
  {
    if (!RESTART_NAMES.at(index).empty())
    {
      byRestart += std::format("{}{} {}/{}", byRestart.empty() ? "" : ", ",
                               RESTART_NAMES.at(index), all.byRestart.at(index),
                               totals.taken.at(index));
    }
  }
  const bool good = totals.finished == RESTART_MATCHES &&
                    all.outsideComfort == 0 && all.inDeadZone == 0 &&
                    totals.longest < RESTART_STUCK_SECONDS;
  std::cout << std::format(
      "  {} out of play: finished {}/{}; lined up/taken: {}\n"
      "    {} kicked: turned to his line {:.0f}%, out of time {:.0f}%; "
      "line-up {:.2f} s on average, longest {:.2f} s; ball {:.2f} m from his "
      "centre on average, furthest {:.2f} m; outside his comfort gap {}, in "
      "his dead "
      "zone {}{}\n",
      version == OutOfPlayId::AARDVARK ? "Aardvark" : "Binturong",
      totals.finished, RESTART_MATCHES, byRestart, all.kicked,
      100.0 * all.turned / kicked, 100.0 * all.outOfTime / kicked,
      all.secondsSum / kicked, totals.longest, all.gapSum / kicked, all.gapMax,
      all.outsideComfort, all.inDeadZone, good ? "" : "  <- FAIL");
  return good;
}
}  // namespace

/** Restart line-ups: Binturong brains and bodies on both sides, under both
 * out-of-play versions. Every restart is taken (no line-up runs on), and a
 * lined-up ball is kicked inside his comfort gap, out of his dead zone. Reports
 * how many restarts of each kind were lined up, how long it took and how
 * far the ball was as he kicked. */
bool runRestartChecks(const std::vector<const Team*>& clubs,
                      const StatsConfig& config)
{
  bool ok = true;
  std::cout << std::format(
      "\nRestart line-ups (Binturong brains and bodies, "
      "{} matches per version)\n",
      RESTART_MATCHES);
  for (const OutOfPlayId version :
       {OutOfPlayId::AARDVARK, OutOfPlayId::BINTURONG})
  {
    RestartTotals totals;
    for (int match = 0; match < RESTART_MATCHES; ++match)
    {
      runRestartMatch(clubs, config, version, match, totals);
    }
    ok = reportRestartVersion(version, totals) && ok;
  }
  return ok;
}

int runPossessionTrace(GameController& controller, double seconds,
                       const std::string& outputPath)
{
  if (!std::isfinite(seconds) || seconds <= 0.0 || seconds > MAX_TRACE_SECONDS)
  {
    return 2;
  }
  std::vector<const Team*> clubs;
  const auto& leagues = controller.getLeagues();
  for (const auto& team : controller.getTeams())
  {
    if (!leagues.empty() &&
        team.get().getLeagueId() == leagues.front().get().getId())
    {
      clubs.push_back(&team.get());
    }
  }
  if (clubs.size() < 2)
  {
    return 2;
  }
  std::ofstream output(outputPath);
  if (!output)
  {
    std::cerr << "Cannot write possession trace: " << outputPath << '\n';
    return 2;
  }
  auto observed =
      makeEngine(*clubs[0], *clubs[1], controller.getStatsConfig(), MATCH_SEED);
  auto reference =
      makeEngine(*clubs[0], *clubs[1], controller.getStatsConfig(), MATCH_SEED);
  const int result =
      tracePossessionPair(*observed, *reference, seconds, output);
  const auto observedFingerprint = fingerprint(*observed);
  const bool same = observedFingerprint == fingerprint(*reference);
  output << std::format(
      "{{\"type\":\"final-fingerprint\",\"seed\":1000,"
      "\"fingerprint\":\"{:016x}\",\"matches_reference\":{}}}\n",
      observedFingerprint, same ? "true" : "false");
  output.flush();
  std::cout << "Possession trace: " << outputPath << "; observed/reference "
            << (same && result == 0 ? "identical" : "FAILED") << '\n';
  if (!output)
  {
    return 2;
  }
  if (result != 0)
  {
    return result;
  }
  return same ? 0 : 1;
}

namespace
{
/** The first league's clubs, in order: pairs 1 v 2, 3 v 4, ... */
std::vector<const Team*> firstLeagueClubs(const GameController& controller)
{
  std::vector<const Team*> clubs;
  const auto& leagues = controller.getLeagues();
  for (const auto& team : controller.getTeams())
  {
    if (!leagues.empty() &&
        team.get().getLeagueId() == leagues.front().get().getId())
    {
      clubs.push_back(&team.get());
    }
  }
  return clubs;
}

/** Totals over the recorded-match table. */
struct RecordedMatchTotals
{
  bool allIdentical = true;
  double plainTotal = 0.0;
  double recordedTotal = 0.0;
  std::size_t entriesTotal = 0;
  std::size_t bytesTotal = 0;
  std::array<std::size_t, std::variant_size_v<SandboxRecorder::Payload>>
      kinds{};
};

/** A recorder's entry and byte counts (and its entries by kind, if asked). */
std::pair<std::size_t, std::size_t> recorderSize(
    SandboxRecorder& recorder, RecordedMatchTotals* totals = nullptr)
{
  const auto guard = recorder.lock();
  if (totals != nullptr)
  {
    for (const auto& entry : recorder.entries())
    {
      ++totals->kinds.at(entry.payload.index());
    }
  }
  return {recorder.entries().size(), recorder.approximateBytes()};
}

/** One match three ways (plain, recorded, and recorded live with look-ahead
 * copies): all must end the same. One table row. */
void checkRecordedMatch(const Team& home, const Team& away,
                        const StatsConfig& config, int match,
                        RecordedMatchTotals& totals)
{
  const auto seed = MATCH_SEED + static_cast<std::uint32_t>(match);

  // A: no recorder.
  auto plain = makeEngine(home, away, config, seed);
  auto started = Clock::now();
  plain->simulateToEnd();
  const double plainSeconds = secondsSince(started);

  // B: recorded.
  SandboxRecorder recorder;
  auto recorded = makeEngine(home, away, config, seed);
  recorded->setRecorder(&recorder);
  started = Clock::now();
  recorded->simulateToEnd();
  const double recordedSeconds = secondsSince(started);

  // C: recorded in one-second steps with highlight look-ahead copies, as
  // the live view plays it: the copies must neither record nor diverge.
  SandboxRecorder liveRecorder;
  auto live = makeEngine(home, away, config, seed);
  live->setRecorder(&liveRecorder);
  for (int second = 0;
       live->getState() != MatchState::FULL_TIME && second < MAX_LIVE_SECONDS;
       ++second)
  {
    if (second % LOOK_AHEAD_EVERY_SECONDS == 0)
    {
      (void)live->predictNextHighlight();
    }
    live->advance(1.0F);
  }

  const std::uint64_t expected = fingerprint(*plain);
  const bool same =
      fingerprint(*recorded) == expected && fingerprint(*live) == expected;
  const auto [entries, bytes] = recorderSize(recorder, &totals);
  const std::size_t liveEntries = recorderSize(liveRecorder).first;
  const bool sameLog = liveEntries == entries;
  totals.allIdentical = totals.allIdentical && same && sameLog;
  totals.plainTotal += plainSeconds;
  totals.recordedTotal += recordedSeconds;
  totals.entriesTotal += entries;
  totals.bytesTotal += bytes;

  std::cout << std::format(
      "{:>3}  {:<32}  {:>2}-{:<2}  {:>7.2f}  {:>5.2f}  {:>10}  {:>7}  {:>6}  "
      "{}"
      "  {:016x}\n",
      match + 1, home.getName() + " v " + away.getName(), plain->getHomeScore(),
      plain->getAwayScore(), plainSeconds, recordedSeconds,
      sameLog ? "same log" : "LOG DIFFERS", entries,
      bytes / static_cast<std::size_t>(BYTES_PER_KIB), same ? "yes" : "NO",
      expected);
}

/** Means per match over the recorded-match table. */
void reportRecordedMatches(const RecordedMatchTotals& totals, int matches)
{
  const auto count = static_cast<double>(matches);
  std::cout << std::format(
      "\nFull-fidelity match: {:.2f} s plain, {:.2f} s recorded ({:+.1f}%)\n",
      totals.plainTotal / count, totals.recordedTotal / count,
      totals.plainTotal > 0.0
          ? (totals.recordedTotal / totals.plainTotal - 1.0) * 100.0
          : 0.0);
  std::cout << std::format(
      "Log per match: {:.0f} entries, ~{:.0f} KB\n",
      static_cast<double>(totals.entriesTotal) / count,
      static_cast<double>(totals.bytesTotal) / count / BYTES_PER_KIB);
  constexpr std::array<const char*, 8> KIND_NAMES{
      "phase",  "job",        "intent", "decision",
      "action", "possession", "keeper", "event"};
  std::cout << "  by kind:";
  for (std::size_t kind = 0;
       kind < totals.kinds.size() && kind < KIND_NAMES.size(); ++kind)
  {
    std::cout << std::format(
        " {} {:.0f}", KIND_NAMES.at(kind),
        static_cast<double>(totals.kinds.at(kind)) / count);
  }
  std::cout << "\n";
}

/** Engine copies (saved states for rewind): time and memory per copy, at
 * half time and at full time (events and tracking grow during a match). */
void reportEngineCopies(const std::vector<const Team*>& clubs,
                        const StatsConfig& config)
{
  for (const float minutes : {45.0F, 95.0F})
  {
    auto engine = makeEngine(*clubs[0], *clubs[1], config, MATCH_SEED);
    while (engine->getState() != MatchState::FULL_TIME &&
           engine->getMatchTimeMinutes() < minutes)
    {
      engine->advance(COPY_ADVANCE_SECONDS);
    }
    constexpr int COPIES = 100;
    std::vector<std::unique_ptr<MatchEngine>> copies;
    copies.reserve(COPIES);
    const auto before = privateBytes();
    const auto started = Clock::now();
    for (int copy = 0; copy < COPIES; ++copy)
    {
      copies.push_back(std::make_unique<MatchEngine>(*engine));
    }
    const double seconds = secondsSince(started);
    const auto after = privateBytes();
    std::cout << std::format("Engine copy at {:>4.1f}': {:.3f} ms",
                             engine->getMatchTimeMinutes(),
                             seconds * MS_PER_SECOND / COPIES);
    if (before && after)
    {
      std::cout << std::format(
          ", ~{:.0f} KB each",
          static_cast<double>(*after - *before) / COPIES / BYTES_PER_KIB);
    }
    std::cout << "\n";
  }
}

/** Which manager changes the rewind match made. */
struct ManagerChanges
{
  bool tactics = false;
  bool substitution = false;
  bool shout = false;
};

/** Plays the rewind match to its end, the home manager changing tactics at
 * 30', making a substitution at 60' and shouting at 75'. */
ManagerChanges playManagedMatch(MatchEngine& engine, const Team& home)
{
  ManagerChanges made;
  for (int second = 0;
       engine.getState() != MatchState::FULL_TIME && second < MAX_LIVE_SECONDS;
       ++second)
  {
    const float minute = engine.getMatchTimeMinutes();
    if (!made.tactics && minute >= TACTICS_MINUTE)
    {
      Strategy attacking = home.getStrategy();
      attacking.setPressing(ATTACKING_PRESSING);
      attacking.setRiskTaking(ATTACKING_RISK);
      engine.setStrategy(true, attacking);
      made.tactics = true;
    }
    if (!made.substitution && minute >= SUBSTITUTION_MINUTE &&
        !home.getLineup().getReserves().empty() &&
        !home.getLineup().getOutfieldPlayers().empty())
    {
      const Player* outgoing = home.getLineup().getOutfieldPlayers()[0].player;
      made.substitution = engine.substitutePlayer(
          outgoing->getId(), home.getLineup().getReserves()[0]);
    }
    if (!made.shout && minute >= SHOUT_MINUTE)
    {
      engine.applyShout(true, MatchShout::ENCOURAGE);
      made.shout = true;
    }
    engine.advance(1.0F);
  }
  return made;
}

constexpr int REWIND_SEEKS = 40;
constexpr int DETAIL_STRETCHES = 12;

/** Rebuilds at ticks across the whole match: how many differed from the
 * recording, and the seconds spent seeking. */
std::pair<int, double> checkSeeks(SandboxRecorder& recorder,
                                  std::uint64_t lastTick)
{
  double seekSeconds = 0.0;
  int diverged = 0;
  for (int seek = 0; seek <= REWIND_SEEKS; ++seek)
  {
    const std::uint64_t tick =
        1 + ((lastTick - 1) * static_cast<std::uint64_t>(seek) / REWIND_SEEKS);
    const auto started = Clock::now();
    const MatchRewind::Result rebuilt = MatchRewind::rebuild(recorder, tick);
    seekSeconds += secondsSince(started);
    const auto guard = recorder.lock();
    const bool exact = rebuilt.engine && !rebuilt.divergedAt &&
                       rebuilt.engine->getSimulatedSteps() == tick &&
                       recorder.checksum(tick) ==
                           SandboxRecorder::stateChecksum(*rebuilt.engine);
    if (!exact)
    {
      ++diverged;
    }
  }
  return {diverged, seekSeconds};
}

/** Counts over the full-detail stretches, and the largest residual found. */
struct DetailTotals
{
  double seconds = 0.0;
  int diverged = 0;
  std::size_t decisions = 0;
  std::size_t candidates = 0;
  std::size_t rankings = 0;
  std::size_t duels = 0;
  std::size_t breakdowns = 0;
  float worst = 0.0F;
  const char* worstName = "";

  /** A breakdown must add up to its score. */
  void check(const ScoreBreakdown& score, const char* name)
  {
    if (!std::isfinite(score.total))
    {
      return;
    }
    ++breakdowns;
    const float residual = std::abs(score.residual());
    if (residual > worst)
    {
      worst = residual;
      worstName = name;
    }
  }

  void addDecisions(const DetailRecorder& detail)
  {
    for (const auto& at : detail.decisions)
    {
      ++decisions;
      for (const auto& option : at.detail.options)
      {
        check(option, "decision option");
      }
      check(at.detail.noiseScale, "noise scale");
      for (const PassCandidateDetail& candidate : at.detail.candidates)
      {
        if (candidate.excluded != PassExclusion::NONE)
        {
          continue;
        }
        ++candidates;
        check(candidate.utility, "pass utility");
        check(candidate.completion, "pass completion");
      }
    }
  }

  void addRankings(const DetailRecorder& detail)
  {
    for (const auto& at : detail.rankings)
    {
      ++rankings;
      const char* name = at.detail.kind == MatchRankingDetail::Kind::TO_BALL
                             ? "to-ball arrival"
                             : "run priority";
      for (const auto& candidate : at.detail.candidates)
      {
        check(candidate.score, name);
      }
    }
  }

  void addDuels(const DetailRecorder& detail)
  {
    for (const auto& at : detail.duels)
    {
      ++duels;
      check(at.detail.chance, "duel chance");
      if (at.detail.kind == MatchDuelDetail::Kind::TACKLE)
      {
        check(at.detail.foul, "foul propensity");
      }
      if (at.detail.kind == MatchDuelDetail::Kind::FIRST_TOUCH)
      {
        check(at.detail.foul, "first-touch weight difficulty");
      }
      if (at.detail.kind == MatchDuelDetail::Kind::STRIKE)
      {
        check(at.detail.foul, "strike weight difficulty");
      }
    }
  }
};

/** Full detail: stretches rebuilt with every score broken down must still
 * reproduce the match, and every breakdown must add up to its score. */
DetailTotals checkFullDetail(SandboxRecorder& recorder, std::uint64_t lastTick)
{
  DetailTotals totals;
  for (int stretch = 1; stretch <= DETAIL_STRETCHES; ++stretch)
  {
    const std::uint64_t tick =
        lastTick * static_cast<std::uint64_t>(stretch) / (DETAIL_STRETCHES + 1);
    DetailRecorder detail;
    const auto started = Clock::now();
    const MatchRewind::Result rebuilt =
        MatchRewind::rebuild(recorder, tick, &detail);
    totals.seconds += secondsSince(started);
    if (!rebuilt.engine || rebuilt.divergedAt)
    {
      ++totals.diverged;
    }
    totals.addDecisions(detail);
    totals.addRankings(detail);
    totals.addDuels(detail);
  }
  return totals;
}

/** Scenario export: a decision becomes a test case with all 22 players. */
bool checkScenarioExport(SandboxRecorder& recorder, std::uint64_t lastTick)
{
  DetailRecorder detail;
  const std::uint64_t tick = lastTick / 3;
  (void)MatchRewind::rebuild(recorder, tick, &detail,
                             SandboxRecorder::KEYFRAME_TICKS);
  if (detail.decisions.empty())
  {
    return false;
  }
  const auto& at = detail.decisions.front();
  const MatchRewind::Result before =
      MatchRewind::rebuild(recorder, at.tick - 1);
  const std::string text = ScenarioExport::testCase(
      *before.engine, recorder, at.detail, at.tick, at.minute);
  std::size_t placements = 0;
  for (std::size_t found = text.find("spec.layout[");
       found != std::string::npos; found = text.find("spec.layout[", found + 1))
  {
    ++placements;
  }
  const bool exported = placements == EXPORTED_PLAYERS &&
                        text.find("spec.carrierId") != std::string::npos;
  const std::filesystem::path sample =
      RuntimePaths::createToolOutputDirectory("scenario-export") /
      "player12-sandbox-check-scenario.cpp.txt";
  std::ofstream(sample) << text;
  std::cout << std::format(
      "Scenario export: {} placements, carrier set: {} (sample: {})\n",
      placements, exported ? "yes" : "NO", sample.string());
  return exported;
}

/** A file's size in bytes, read from its end; 0 if it cannot be opened. */
std::uintmax_t fileBytes(const std::filesystem::path& path)
{
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  return file ? static_cast<std::uintmax_t>(file.tellg()) : 0;
}

/** Whether a replay ran with that possession version. */
bool possessionSelected(const Recordings::Replay& result, PossessionId version)
{
  if (!result.match)
  {
    return false;
  }
  const auto guard = result.match->lock();
  const auto& frames = result.match->keyframes();
  return !frames.empty() &&
         frames.front().engine->getPossessionVersion() == version;
}

/** What replaying a reopened recording found. */
struct ReplayFindings
{
  bool exact = false;
  bool tickFound = false;
  bool decisionFound = false;
  std::string newBrainReplay = "not run";
  std::string newBodyReplay = "not run";
  std::string newHandshakeReplay = "not run";
  bool possessionReplay = false;
  std::string possessionOverride = "not run";
};

/** Replays a reopened recording: as recorded, with the other possession
 * version, with the versions being developed (information only), and with a
 * fingerprint and a decision tampered with (both must be found). */
ReplayFindings replayRecording(const MatchRecording& loaded,
                               GameController& controller)
{
  ReplayFindings found;
  const Recordings::Replay replay = Recordings::replay(loaded, controller);
  found.exact =
      replay.match && !replay.divergedAt && !replay.firstDifferentDecision;
  const auto other = loaded.setup.possession == PossessionId::AARDVARK
                         ? PossessionId::BINTURONG
                         : PossessionId::AARDVARK;
  const auto overridden = Recordings::replay(loaded, controller, std::nullopt,
                                             std::nullopt, std::nullopt, other);
  found.possessionReplay =
      possessionSelected(replay, loaded.setup.possession) &&
      possessionSelected(overridden, other);
  found.possessionOverride =
      std::format("{} selected: {}; {}", possessionName(other),
                  possessionSelected(overridden, other) ? "yes" : "NO",
                  overridden.match && !overridden.divergedAt &&
                          !overridden.firstDifferentDecision
                      ? "same as recording"
                      : "different/failed");
  // Side by side with the brain being developed (information only:
  // identical while Binturong is still a copy of the Aardvark brain).
  found.newBrainReplay = describeReplay(Recordings::replay(
      loaded, controller, std::array{BrainId::BINTURONG, BrainId::BINTURONG}));
  // The same with the body being developed (information only too).
  found.newBodyReplay = describeReplay(
      Recordings::replay(loaded, controller, std::nullopt,
                         std::array{BodyId::BINTURONG, BodyId::BINTURONG}));
  // And with the handshake being developed (information only too).
  found.newHandshakeReplay = describeReplay(Recordings::replay(
      loaded, controller, std::nullopt, std::nullopt, HandshakeId::BINTURONG));
  // Tampered copies: one fingerprint, one decision's score.
  MatchRecording tampered = loaded;
  const std::uint64_t at = tampered.lastTick / 2;
  tampered.checksums[at] ^= 1U;
  found.tickFound = Recordings::replay(tampered, controller).divergedAt == at;
  tampered = loaded;
  const std::size_t which = tampered.decisions.size() / 2;
  tampered.decisions[which].record.carry += TAMPERED_CARRY;
  found.decisionFound =
      Recordings::replay(tampered, controller).firstDifferentDecision == which;
  return found;
}

/** Recordings: saved with full detail, opened again and replayed, the match
 * must replay exactly; a changed fingerprint or decision in the recording
 * must be found where it was changed. */
bool checkRecordings(GameController& controller, const MatchSetup& setup,
                     const SandboxRecorder& recorder)
{
  const auto started = Clock::now();
  const MatchRecording recording = Recordings::capture(setup, recorder, true);
  const double captureSeconds = secondsSince(started);
  const ScratchDirectory scratch("recording-check");
  const auto path = scratch.path / "player12-sandbox-check-recording.json";
  std::string error;
  bool roundTrip = Recordings::save(recording, path, error);
  const std::uintmax_t bytes = roundTrip ? fileBytes(path) : 0;
  std::optional<MatchRecording> loaded =
      roundTrip ? Recordings::load(path, error) : std::nullopt;
  roundTrip = loaded && loaded->setup.possession == recording.setup.possession;
  const ReplayFindings found =
      loaded ? replayRecording(*loaded, controller) : ReplayFindings{};
  std::cout << std::format(
      "Recording: {} decisions with detail, {:.0f} KB, captured in {:.2f} "
      "s; reopened {}, replays exactly {}, changed tick found {}, changed "
      "decision found {}{}\n",
      recording.decisions.size(), static_cast<double>(bytes) / BYTES_PER_KIB,
      captureSeconds, roundTrip ? "yes" : "NO", found.exact ? "yes" : "NO",
      found.tickFound ? "yes" : "NO", found.decisionFound ? "yes" : "NO",
      error.empty() ? "" : "  (" + error + ")");
  std::cout << std::format(
      "Replay with brains Binturong v Binturong (recorded with {}): {}\n",
      brainPairName(recording.setup.brains), found.newBrainReplay);
  std::cout << std::format(
      "Replay with bodies Binturong v Binturong (recorded with {}): {}\n",
      bodyPairName(recording.setup.bodies), found.newBodyReplay);
  std::cout << std::format(
      "Replay with handshake Binturong (recorded with {}): {}\n",
      handshakeName(recording.setup.handshake), found.newHandshakeReplay);
  std::cout << std::format("Possession {} retained: {}; replay override {}\n",
                           possessionName(recording.setup.possession),
                           found.possessionReplay ? "yes" : "NO",
                           found.possessionOverride);
  return roundTrip && found.exact && found.tickFound && found.decisionFound &&
         found.possessionReplay;
}

/** Rewind: a recorded match with manager changes (tactics at 30', a
 * substitution at 60', a shout at 75') is rebuilt at ticks across the whole
 * match; every rebuilt tick must match the recording. Then full detail,
 * scenario export and recordings, from the same match. None if the match
 * could not be built. */
std::optional<bool> checkRewind(GameController& controller,
                                const std::vector<const Team*>& clubs)
{
  const Team& home = *clubs[0];
  const Team& away = *clubs[1];
  SandboxRecorder recorder;
  // Built from a setup, as the sandbox builds its matches.
  const MatchSetup setup =
      MatchSetup::capture(controller, {home.getId(), away.getId()}, 4242, true);
  std::string buildError;
  auto engine = setup.build(controller, buildError);
  if (!engine)
  {
    std::cerr << "Could not build the match: " << buildError << '\n';
    return std::nullopt;
  }
  engine->setRecorder(&recorder);
  const ManagerChanges made = playManagedMatch(*engine, home);
  std::uint64_t lastTick = 0;
  std::size_t keyframes = 0;
  std::size_t commands = 0;
  {
    const auto guard = recorder.lock();
    lastTick = recorder.lastTick();
    keyframes = recorder.keyframes().size();
    commands = recorder.commands().size();
  }
  const auto [seekDiverged, seekSeconds] = checkSeeks(recorder, lastTick);
  const DetailTotals detail = checkFullDetail(recorder, lastTick);
  const int diverged = seekDiverged + detail.diverged;
  bool rewindExact = diverged == 0;
  const bool exported = checkScenarioExport(recorder, lastTick);
  rewindExact = rewindExact && exported;
  const bool addsUp = detail.worst < 1e-3F;
  rewindExact = rewindExact && addsUp;
  const bool recordingsWork = checkRecordings(controller, setup, recorder);
  rewindExact = rewindExact && recordingsWork;
  std::cout << std::format(
      "\nFull detail: {} stretches, {:.1f} ms each; {} decisions, {} pass "
      "candidates, {} rankings, {} duels; {} breakdowns, largest residual "
      "{:.2e}{}{}\n",
      DETAIL_STRETCHES, detail.seconds * MS_PER_SECOND / DETAIL_STRETCHES,
      detail.decisions, detail.candidates, detail.rankings, detail.duels,
      detail.breakdowns, detail.worst, detail.worst > 0.0F ? " in " : "",
      detail.worst > 0.0F ? detail.worstName : "");
  std::cout << std::format(
      "\nRewind: {} seeks across {} ticks ({} saved copies, {} manager "
      "changes: tactics {}, sub {}, shout {}): {} exact, {:.2f} ms per "
      "seek\n",
      REWIND_SEEKS + 1, lastTick, keyframes, commands,
      made.tactics ? "yes" : "no", made.substitution ? "yes" : "no",
      made.shout ? "yes" : "no", REWIND_SEEKS + 1 - diverged,
      seekSeconds * MS_PER_SECOND / (REWIND_SEEKS + 1));
  return rewindExact;
}

/** One check group's result line; returns whether it passed. */
bool reportCheck(bool passed, const char* pass, const char* fail)
{
  std::cout << (passed ? pass : fail);
  return passed;
}

/** The drills play with the sandbox's brains and bodies. Routes, turns and
 * the new touch and strike are the Binturong body's, so the drills are
 * checked with the Binturong brain and body; the matches keep theirs. */
bool runDrillChecksAsBinturong(const std::vector<const Team*>& clubs,
                               const StatsConfig& config)
{
  const std::array<BrainId, 2> matchBrains = sandboxBrains();
  const std::array<BodyId, 2> matchBodies = sandboxBodies();
  sandboxBrains() = {BrainId::BINTURONG, BrainId::BINTURONG};
  sandboxBodies() = {BodyId::BINTURONG, BodyId::BINTURONG};
  const bool drillsWork = runDrillChecks(*clubs[0], *clubs[1], config);
  sandboxBrains() = matchBrains;
  sandboxBodies() = matchBodies;
  return drillsWork;
}

/** The checks after the matches: drills, derived stats, possession, first
 * touch and restarts. Each runs and reports whatever the others found. */
bool runEngineChecks(const std::vector<const Team*>& clubs,
                     const StatsConfig& config)
{
  const bool drillsWork = reportCheck(runDrillChecksAsBinturong(clubs, config),
                                      "PASS: drills run, repeat and rewind.\n",
                                      "FAIL: a drill check failed.\n");
  const bool derivedWork =
      reportCheck(runDerivedStatChecks(clubs, config),
                  "PASS: derived stats complete and in range.\n",
                  "FAIL: a derived stat check failed.\n");
  const bool possessionWorks =
      reportCheck(runPossessionChecks(clubs, config),
                  "PASS: ball ownership coherent throughout.\n",
                  "FAIL: the ball's ownership state was incoherent.\n");
  const bool firstTouchWorks =
      reportCheck(runFirstTouchChecks(clubs, config),
                  "PASS: first touches roll under physics and are collected.\n",
                  "FAIL: a first-touch check failed.\n");
  const bool restartsWork =
      reportCheck(runRestartChecks(clubs, config),
                  "PASS: restarts are lined up and taken, the ball inside "
                  "his comfort gap.\n",
                  "FAIL: a restart check failed.\n");
  return drillsWork && derivedWork && possessionWorks && firstTouchWorks &&
         restartsWork;
}
}  // namespace

int runSandboxChecks(GameController& controller, int matches,
                     SandboxCheckGroup group)
{
  // Pairs of clubs from the first league: 1 v 2, 3 v 4, ...
  const std::vector<const Team*> clubs = firstLeagueClubs(controller);
  if (clubs.size() < 2)
  {
    std::cerr << "Not enough clubs for the checks.\n";
    return 1;
  }
  const StatsConfig& config = controller.getStatsConfig();

  if (group == SandboxCheckGroup::FIRST_TOUCH)
  {
    const bool good = reportCheck(
        runFirstTouchChecks(clubs, config),
        "PASS: first touches roll under physics and are collected.\n",
        "FAIL: a first-touch check failed.\n");
    return good ? 0 : 1;
  }
  if (group == SandboxCheckGroup::RESTARTS)
  {
    const bool good = reportCheck(runRestartChecks(clubs, config),
                                  "PASS: restarts are lined up and taken, the "
                                  "ball inside his comfort gap.\n",
                                  "FAIL: a restart check failed.\n");
    return good ? 0 : 1;
  }

  std::cout << "Match sandbox checks: " << matches << " matches\n\n";
  std::cout << "  #  match                              score  plain s  rec s  "
               "look-ahead  entries      KB  identical\n";
  RecordedMatchTotals totals;
  for (int match = 0; match < matches; ++match)
  {
    const auto pair = static_cast<std::size_t>(match) * 2;
    checkRecordedMatch(*clubs[pair % clubs.size()],
                       *clubs[(pair + 1) % clubs.size()], config, match,
                       totals);
  }
  reportRecordedMatches(totals, matches);
  reportEngineCopies(clubs, config);

  const std::optional<bool> rewindExact = checkRewind(controller, clubs);
  if (!rewindExact)
  {
    return 1;
  }
  std::cout << (totals.allIdentical
                    ? "\nPASS: recording never changed a match.\n"
                    : "\nFAIL: a recorded match differed.\n");
  std::cout << (*rewindExact
                    ? "PASS: every rewind and recording replay reproduced the "
                      "match.\n"
                    : "FAIL: a rewind or recording replay differed from the "
                      "match.\n");
  const bool engineWorks = runEngineChecks(clubs, config);
  return totals.allIdentical && *rewindExact && engineWorks ? 0 : 1;
}

namespace
{
/** One player at one step, for the collision trace. */
struct TraceMoment
{
  PitchPoint position;
  Vector2F velocity{};
  float facing = 0.0F;
  PlayerIntent intent = PlayerIntent::HOLD_SHAPE;
  PitchPoint target;
  PitchPoint heading;
  int waypoints = 0;
  int roundOf = -1;
  int roundSide = 0;
  int blockedBy = -1;
  int givingWayTo = -1;
  int rightOver = -1;
  float giveWaySpeed = -1.0F;
  bool steers = false;
  bool ballOwned = false;
  bool setPiece = false;
};

/** Getting into position: holding shape, offering support, recovering. */
bool isPositioning(PlayerIntent intent)
{
  return intent == PlayerIntent::HOLD_SHAPE ||
         intent == PlayerIntent::OFFER_SUPPORT ||
         intent == PlayerIntent::RECOVER_SHAPE;
}

std::string intentText(PlayerIntent intent)
{
  return std::string(MatchEngineInternal::intentName(intent));
}

/** Follows a match's collisions between players getting into position,
 * printing each wanted one with both players' last steps. */
class CollisionTrace
{
 public:
  CollisionTrace(const MatchEngine& match, const std::string& wantedKind)
      : engine(&match),
        players(&match.getPlayers()),
        history(match.getPlayers().size()),
        kind(&wantedKind)
  {
  }

  /** One step's collisions, then the step remembered; returns how many
   * cases were shown. */
  int step(double now)
  {
    int shown = 0;
    for (const MatchEngine::CollisionHit& hit : engine->getLastCollisions())
    {
      shown += consider(hit, now) ? 1 : 0;
    }
    remember();
    return shown;
  }

 private:
  static constexpr std::size_t HISTORY = 20;
  using Slots = std::array<std::size_t, 2>;

  [[nodiscard]] const MatchPlayer& at(std::size_t slot) const
  {
    return (*players)[slot];
  }

  [[nodiscard]] std::string nameOf(std::size_t slot) const
  {
    return (at(slot).player != nullptr) ? at(slot).player->getName()
                                        : std::string("?");
  }

  /** Each side's slot; none unless both are on. */
  [[nodiscard]] std::optional<Slots> slotsOf(
      const MatchEngine::CollisionHit& hit) const
  {
    Slots slots{players->size(), players->size()};
    for (std::size_t side = 0; side < 2; ++side)
    {
      for (std::size_t slot = 0; slot < players->size(); ++slot)
      {
        if ((at(slot).player != nullptr) &&
            at(slot).player->getId() == hit.sides.at(side).player)
        {
          slots.at(side) = slot;
        }
      }
    }
    if (slots[0] >= players->size() || slots[1] >= players->size())
    {
      return std::nullopt;
    }
    return slots;
  }

  [[nodiscard]] bool atSpot(const TraceMoment& moment, std::size_t slot) const
  {
    const Vector2F here = asVector(at(slot).position);
    const Vector2F target = asVector(moment.target);
    return std::hypot(target.x - here.x, target.y - here.y) <= AT_SPOT_METRES;
  }

  /** By the trace's kind: "all", "spot" (one at his spot), "giveway" (one
   * giving way), else "neither saw it" (nobody at his spot, giving way or
   * going round). */
  [[nodiscard]] bool wanted(const Slots& slots) const
  {
    const TraceMoment& lastA = history[slots[0]].back();
    const TraceMoment& lastB = history[slots[1]].back();
    const bool spot = atSpot(lastA, slots[0]) || atSpot(lastB, slots[1]);
    const auto a8 = static_cast<int>(slots[0]);
    const auto b8 = static_cast<int>(slots[1]);
    const bool givingWay = lastA.givingWayTo == b8 || lastB.givingWayTo == a8 ||
                           lastA.rightOver == b8 || lastB.rightOver == a8;
    const bool sawIt = lastA.roundOf >= 0 || lastB.roundOf >= 0 ||
                       lastA.blockedBy >= 0 || lastB.blockedBy >= 0;
    if (*kind == "all")
    {
      return true;
    }
    if (*kind == "spot")
    {
      return spot;
    }
    if (*kind == "giveway")
    {
      return !spot && givingWay;
    }
    return !spot && !givingWay && !sawIt && lastA.givingWayTo < 0 &&
           lastB.givingWayTo < 0 && lastA.rightOver < 0 && lastB.rightOver < 0;
  }

  /** Prints the collision if it's a wanted one; returns whether it was. */
  bool consider(const MatchEngine::CollisionHit& hit, double now)
  {
    const std::optional<Slots> slots = slotsOf(hit);
    if (!slots)
    {
      return false;
    }
    if (hit.sides[0].outcome == 0 && hit.sides[1].outcome == 0)
    {
      return false;
    }
    if (history[(*slots)[0]].empty() || history[(*slots)[1]].empty())
    {
      return false;
    }
    if (!isPositioning(history[(*slots)[0]].back().intent) ||
        !isPositioning(history[(*slots)[1]].back().intent) || !wanted(*slots))
    {
      return false;
    }
    // Why the planner never had them: over the last 2 s, for each.
    printWhy(0, history[(*slots)[0]]);
    printWhy(1, history[(*slots)[1]]);
    printHeader(hit, *slots, now);
    printMoments(*slots);
    return true;
  }

  static void printWhy(std::size_t side, const std::deque<TraceMoment>& h)
  {
    int owned = 0;
    int setPiece = 0;
    int notSteering = 0;
    int intentChanges = 0;
    int targetJumps = 0;
    float biggestJump = 0.0F;
    for (std::size_t i = 0; i < h.size(); ++i)
    {
      owned += h[i].ballOwned ? 1 : 0;
      setPiece += h[i].setPiece ? 1 : 0;
      notSteering += h[i].steers ? 0 : 1;
      if (i == 0)
      {
        continue;
      }
      intentChanges += h[i].intent != h[i - 1].intent ? 1 : 0;
      const Vector2F t0 = asVector(h[i - 1].target);
      const Vector2F t1 = asVector(h[i].target);
      const float jump = std::hypot(t1.x - t0.x, t1.y - t0.y);
      targetJumps += jump > 1.0F ? 1 : 0;
      biggestJump = std::max(biggestJump, jump);
    }
    std::println(
        "  WHY {}: ball owned {}/{} steps, set piece {}, not steering {}, "
        "intent changes {}, target jumps >1 m {} (biggest {:.1f} m)",
        side == 0 ? 'A' : 'B', owned, h.size(), setPiece, notSteering,
        intentChanges, targetJumps, biggestJump);
  }

  void printHeader(const MatchEngine::CollisionHit& hit, const Slots& slots,
                   double now) const
  {
    const TraceMoment& lastA = history[slots[0]].back();
    const TraceMoment& lastB = history[slots[1]].back();
    const bool mates = at(slots[0]).isHomeTeam == at(slots[1]).isHomeTeam;
    std::println(
        "\n=== {:.1f} s  {} ({}) / {} ({}), {}; outcome {} / {}, jolt {:.2f} "
        "/ {:.2f}",
        now, nameOf(slots[0]), intentText(lastA.intent), nameOf(slots[1]),
        intentText(lastB.intent), mates ? "team-mates" : "opponents",
        hit.sides[0].outcome, hit.sides[1].outcome,
        std::hypot(hit.sides[0].jolt.x, hit.sides[0].jolt.y),
        std::hypot(hit.sides[1].jolt.x, hit.sides[1].jolt.y));
  }

  /** How he moved, where to, and whom he was going round or giving way to. */
  [[nodiscard]] std::string describe(const TraceMoment& m, Vector2F here) const
  {
    const Vector2F target = asVector(m.target);
    const Vector2F heading = asVector(m.heading);
    const float speed = std::hypot(m.velocity.x, m.velocity.y);
    const float course =
        std::atan2(m.velocity.y, m.velocity.x) * DEGREES_PER_RADIAN_F;
    std::string yielding;
    if (m.givingWayTo >= 0)
    {
      yielding = std::format(" · GIVING WAY to {} (held to {:.1f} m/s)",
                             nameOf(m.givingWayTo), m.giveWaySpeed);
    }
    else if (m.rightOver >= 0)
    {
      yielding = std::format(" · right of way over {}", nameOf(m.rightOver));
    }
    return std::format(
        "{:.1f} m/s course {:.0f} · {} · target {:.1f} m{} · {}{}{}", speed,
        course, intentText(m.intent).substr(0, INTENT_LETTERS),
        std::hypot(target.x - here.x, target.y - here.y),
        m.waypoints > 0
            ? std::format(" (via waypoint {:.1f} m)",
                          std::hypot(heading.x - here.x, heading.y - here.y))
            : std::string(),
        m.roundOf >= 0
            ? std::format("going round {} on his {}", nameOf(m.roundOf),
                          m.roundSide > 0 ? "left" : "right")
            : std::string("not going round"),
        m.blockedBy >= 0 ? std::format(" (blocked by {})", nameOf(m.blockedBy))
                         : std::string(),
        yielding);
  }

  /** Both players every other step back from the collision. */
  void printMoments(const Slots& slots) const
  {
    const std::deque<TraceMoment>& historyA = history[slots[0]];
    const std::deque<TraceMoment>& historyB = history[slots[1]];
    const std::size_t count = std::min(historyA.size(), historyB.size());
    for (std::size_t back = count; back-- > 0;)
    {
      const bool shownStep = back % 2 == 1 || back == 0;
      if (!shownStep || back >= historyA.size() || back >= historyB.size())
      {
        continue;
      }
      const TraceMoment& a = historyA[historyA.size() - 1 - back];
      const TraceMoment& b = historyB[historyB.size() - 1 - back];
      const Vector2F pa = asVector(a.position);
      const Vector2F pb = asVector(b.position);
      std::println("  -{:.1f} s  gap {:.2f} m{}\n     A: {}{}\n     B: {}{}",
                   HISTORY_STEP_SECONDS * static_cast<double>(back),
                   std::hypot(pb.x - pa.x, pb.y - pa.y),
                   a.ballOwned ? "  (ball owned)" : "", describe(a, pa),
                   a.steers ? "" : " · NOT STEERING", describe(b, pb),
                   b.steers ? "" : " · NOT STEERING");
    }
  }

  /** Every player's moment this step, the oldest dropped past HISTORY. */
  void remember()
  {
    const bool ballOwned = engine->getPossessionAttachment() != nullptr;
    const bool setPiece = engine->getSetPiecePhaseRemaining() > 0.0F ||
                          engine->getState() != MatchState::PLAYING;
    for (std::size_t slot = 0; slot < players->size(); ++slot)
    {
      const MatchPlayer& player = at(slot);
      history[slot].push_back({.position = player.position,
                               .velocity = player.velocity,
                               .facing = player.facingAngle,
                               .intent = player.intent,
                               .target = player.tacticalTarget,
                               .heading = player.movementTarget,
                               .waypoints = player.routeWaypoints,
                               .roundOf = player.wayRoundOf,
                               .roundSide = player.wayRoundSide,
                               .blockedBy = player.wayRoundBlockedBy,
                               .givingWayTo = player.givingWayTo,
                               .rightOver = player.rightOfWayOver,
                               .giveWaySpeed = player.giveWaySpeed,
                               .steers = player.steersRound,
                               .ballOwned = ballOwned,
                               .setPiece = setPiece});
      if (history[slot].size() > HISTORY)
      {
        history[slot].pop_front();
      }
    }
  }

  const MatchEngine* engine;
  const std::vector<MatchPlayer>* players;
  std::vector<std::deque<TraceMoment>> history;
  const std::string* kind;
};
}  // namespace

int runMatchTrace(GameController& controller, double seconds, int cases,
                  const std::string& kind)
{
  // The first two clubs of the first league, a match from kick-off: each
  // stumble or fall between two players getting into position (holding
  // shape, offering support, recovering shape), with both players' last
  // 1.2 s before it (way-round plan diagnostics).
  const std::vector<const Team*> clubs = firstLeagueClubs(controller);
  if (clubs.size() < 2)
  {
    return 1;
  }
  auto engine =
      makeEngine(*clubs[0], *clubs[1], controller.getStatsConfig(), MATCH_SEED);
  CollisionTrace trace(*engine, kind);
  int shown = 0;
  double now = 0.0;
  while (now < seconds && shown < cases &&
         engine->getState() != MatchState::FULL_TIME)
  {
    engine->advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    now += MatchTuning::Timing::FIXED_STEP_SECONDS;
    shown += trace.step(now);
  }
  std::println("\n{} cases in {:.0f} s", shown, now);
  return 0;
}
