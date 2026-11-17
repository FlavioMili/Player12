// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "global/stats_config.h"
#include "model/match_engine.h"
#include "model/match_pitch.h"
#include "model/match_scenario.h"
#include "model/player.h"
#include "model/team.h"

namespace
{
// Tests play on a match pitch, placed by fractions of it.
const MatchPitch& PITCH = MatchPitch::matchSize();
}  // namespace

namespace
{
StatsConfig createStatsConfig()
{
  StatsConfig config;
  config.possible_stats = {"Pace",      "Shooting",  "Passing",
                           "Dribbling", "Defending", "Physicality",
                           "Stamina",   "Vision",    "Goalkeeping"};
  config.role_focus["Goalkeeper"] = {
      .stats = {"Goalkeeping", "Vision", "Physicality"},
      .weights = {0.7, 0.2, 0.1}};
  config.role_focus["Defender"] = {
      .stats = {"Defending", "Physicality", "Pace", "Vision"},
      .weights = {0.4, 0.3, 0.15, 0.15}};
  config.role_focus["Midfielder"] = {
      .stats = {"Passing", "Vision", "Stamina", "Dribbling"},
      .weights = {0.3, 0.3, 0.2, 0.2}};
  config.role_focus["Striker"] = {
      .stats = {"Shooting", "Pace", "Dribbling", "Physicality"},
      .weights = {0.4, 0.2, 0.2, 0.2}};
  return config;
}

Team createDummyTeam(TeamID id, const std::string& name, int rating,
                     std::vector<std::unique_ptr<Player>>& players)
{
  using enum PlayerRole;
  Team team(id, 1, name, 50'000'000, {}, Strategy{}, Lineup{});
  static constexpr std::array ROLES = {GK, LB, CB, CB, RB, CM,
                                       CM, LW, RW, ST, ST};
  static constexpr std::array<Vector2F, 11> POSITIONS = {
      {{.x = 0.04F, .y = 0.50F},
       {.x = 0.18F, .y = 0.12F},
       {.x = 0.18F, .y = 0.38F},
       {.x = 0.18F, .y = 0.62F},
       {.x = 0.18F, .y = 0.88F},
       {.x = 0.43F, .y = 0.35F},
       {.x = 0.43F, .y = 0.65F},
       {.x = 0.68F, .y = 0.16F},
       {.x = 0.68F, .y = 0.84F},
       {.x = 0.78F, .y = 0.38F},
       {.x = 0.78F, .y = 0.62F}}};

  for (uint32_t index = 0; index < 11; ++index)
  {
    const auto value = static_cast<float>(rating);
    const std::map<std::string, float> stats = {
        {"Pace", value},      {"Shooting", value},  {"Passing", value},
        {"Dribbling", value}, {"Defending", value}, {"Physicality", value},
        {"Stamina", value},   {"Vision", value},    {"Goalkeeping", value}};
    auto player = std::make_unique<Player>(
        (static_cast<PlayerID>(id) * 100U) + index, id, "First",
        std::to_string(index), ROLES[index], Language::EN, 100'000, 0, 25, 3,
        180, Foot::Right, stats);
    if (ROLES[index] == GK)
    {
      team.getLineup().setGoalkeeper(player.get());
    }
    else
    {
      team.getLineup().addOutfieldPlayer(player.get(), POSITIONS[index]);
    }
    players.push_back(std::move(player));
  }
  return team;
}

struct Ids
{
  std::vector<std::uint32_t> home;
  std::vector<std::uint32_t> away;
};

Ids collectIds(const std::vector<std::unique_ptr<Player>>& pool)
{
  Ids ids;
  for (int index = 0; index < 11; ++index)
  {
    ids.home.push_back(pool[static_cast<size_t>(index)]->getId());
    ids.away.push_back(pool[static_cast<size_t>(index + 11)]->getId());
  }
  return ids;
}

std::map<std::uint32_t, Vector2F> standardHomeAttackLayout(const Ids& ids)
{
  // Home (left to right) building out of a settled block; away parked deep.
  const std::array<std::pair<std::uint32_t, Vector2F>, 11> homeSlots = {
      {{ids.home[0], {.x = 0.02F, .y = 0.50F}},
       {ids.home[1], {.x = 0.16F, .y = 0.12F}},
       {ids.home[2], {.x = 0.17F, .y = 0.35F}},
       {ids.home[3], {.x = 0.17F, .y = 0.65F}},
       {ids.home[4], {.x = 0.16F, .y = 0.88F}},
       {ids.home[5], {.x = 0.42F, .y = 0.45F}},
       {ids.home[6], {.x = 0.42F, .y = 0.55F}},
       {ids.home[7], {.x = 0.62F, .y = 0.12F}},
       {ids.home[8], {.x = 0.62F, .y = 0.88F}},
       {ids.home[9], {.x = 0.68F, .y = 0.42F}},
       {ids.home[10], {.x = 0.68F, .y = 0.58F}}}};
  const std::array<std::pair<std::uint32_t, Vector2F>, 11> awaySlots = {
      {{ids.away[0], {.x = 0.965F, .y = 0.50F}},
       {ids.away[1], {.x = 0.88F, .y = 0.12F}},
       {ids.away[2], {.x = 0.88F, .y = 0.38F}},
       {ids.away[3], {.x = 0.88F, .y = 0.62F}},
       {ids.away[4], {.x = 0.88F, .y = 0.88F}},
       {ids.away[5], {.x = 0.74F, .y = 0.42F}},
       {ids.away[6], {.x = 0.74F, .y = 0.58F}},
       {ids.away[7], {.x = 0.60F, .y = 0.12F}},
       {ids.away[8], {.x = 0.60F, .y = 0.88F}},
       {ids.away[9], {.x = 0.45F, .y = 0.42F}},
       {ids.away[10], {.x = 0.45F, .y = 0.58F}}}};
  std::map<std::uint32_t, Vector2F> layout;
  for (const auto& [playerId, position] : homeSlots)
  {
    layout[playerId] = position;
  }
  for (const auto& [playerId, position] : awaySlots)
  {
    layout[playerId] = position;
  }
  return layout;
}

std::uint32_t slotId(const Ids& ids, uint32_t index)
{
  return index < 11 ? ids.home[index] : ids.away[index - 11];
}

struct ScenarioSpec
{
  std::map<std::uint32_t, Vector2F> layout;
  std::map<std::uint32_t, bool> makeRuns;
  std::uint32_t carrierId = 0;
};

MatchScenario buildScenario(const ScenarioSpec& spec)
{
  MatchScenario scenario;
  for (const auto& [id, position] : spec.layout)
  {
    MatchScenarioPlayer placement;
    placement.playerId = id;
    placement.position = PITCH.at(position.x, position.y);
    placement.makingRun = spec.makeRuns.contains(id) && spec.makeRuns.at(id);
    scenario.players.push_back(placement);
  }
  scenario.carrierId = spec.carrierId;
  const Vector2F ball = spec.layout.at(spec.carrierId);
  scenario.ballPosition = PITCH.at(ball.x, ball.y);
  return scenario;
}

MatchEngine buildEngine(const Team& home, const Team& away,
                        const StatsConfig& config)
{
  return MatchEngine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 111'777);
}
}  // namespace

TEST(MatchScenarioTest, BlockedCentralLaneChoosesTheOpenFlankOption)
{
  std::vector<std::unique_ptr<Player>> pool;
  Team home = createDummyTeam(1, "Home", 70, pool);
  Team away = createDummyTeam(2, "Away", 70, pool);
  const Ids ids = collectIds(pool);
  const StatsConfig config = createStatsConfig();

  ScenarioSpec spec;
  spec.layout = standardHomeAttackLayout(ids);
  // Carrier is a central midfielder; the central lane onward is blocked.
  spec.layout[slotId(ids, 5)] = {.x = 0.50F, .y = 0.50F};
  spec.layout[slotId(ids, 9)] = {.x = 0.66F, .y = 0.50F};
  spec.layout[slotId(ids, 7)] = {.x = 0.72F, .y = 0.12F};
  spec.layout[slotId(ids, 7 + 11)] = {.x = 0.58F, .y = 0.50F};
  spec.carrierId = slotId(ids, 5);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  if (!decision.best)
  {
    FAIL() << "expected a pass option";
  }
  EXPECT_EQ(decision.best->receiverId, slotId(ids, 7))
      << "The blocked central lane must be avoided in favour of the open "
         "flank option";
  EXPECT_NE(decision.best->receiverId, slotId(ids, 9));
  EXPECT_LT(decision.best->completionProbability, 0.99F);
  EXPECT_FALSE(decision.reason.empty());

  const std::string json = engine.getDebugSnapshotJson();
  EXPECT_NE(json.find("\"decision\":{\"reason\":"), std::string::npos);
  EXPECT_NE(json.find("\"best\":{\"receiver\":"), std::string::npos);
}

TEST(MatchScenarioTest, OpenProgressiveMidfielderIsChosenFirst)
{
  std::vector<std::unique_ptr<Player>> pool;
  Team home = createDummyTeam(1, "Home", 70, pool);
  Team away = createDummyTeam(2, "Away", 70, pool);
  const Ids ids = collectIds(pool);
  const StatsConfig config = createStatsConfig();

  ScenarioSpec spec;
  spec.layout = standardHomeAttackLayout(ids);
  spec.layout[slotId(ids, 5)] = {.x = 0.48F, .y = 0.50F};
  spec.layout[slotId(ids, 6)] = {.x = 0.63F, .y = 0.55F};
  // Keep the forwards behind the ball so the midfielder option dominates.
  spec.layout[slotId(ids, 9)] = {.x = 0.32F, .y = 0.38F};
  spec.layout[slotId(ids, 10)] = {.x = 0.32F, .y = 0.62F};
  spec.carrierId = slotId(ids, 5);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  if (!decision.best)
  {
    FAIL() << "expected a pass option";
  }
  EXPECT_EQ(decision.best->receiverId, slotId(ids, 6));
  EXPECT_EQ(decision.best->intent, PassIntent::PROGRESSIVE);
  EXPECT_GT(decision.best->progression, 0.0F);
  EXPECT_GT(decision.best->completionProbability, 0.60F);
}

TEST(MatchScenarioTest, OnsideRunnerBehindTheLineIsSelectable)
{
  std::vector<std::unique_ptr<Player>> pool;
  Team home = createDummyTeam(1, "Home", 70, pool);
  Team away = createDummyTeam(2, "Away", 70, pool);
  const Ids ids = collectIds(pool);
  const StatsConfig config = createStatsConfig();

  ScenarioSpec spec;
  spec.layout = standardHomeAttackLayout(ids);
  spec.layout[slotId(ids, 5)] = {.x = 0.55F, .y = 0.50F};
  spec.layout[slotId(ids, 9)] = {.x = 0.80F, .y = 0.50F};
  spec.layout[slotId(ids, 10)] = {.x = 0.80F, .y = 0.74F};
  // High defensive line: away back four on the edge of the box.
  spec.layout[slotId(ids, 1 + 11)] = {.x = 0.87F, .y = 0.12F};
  spec.layout[slotId(ids, 2 + 11)] = {.x = 0.87F, .y = 0.40F};
  spec.layout[slotId(ids, 3 + 11)] = {.x = 0.87F, .y = 0.60F};
  spec.layout[slotId(ids, 4 + 11)] = {.x = 0.87F, .y = 0.88F};
  spec.layout[slotId(ids, 9 + 11)] = {.x = 0.45F, .y = 0.42F};
  spec.layout[slotId(ids, 10 + 11)] = {.x = 0.45F, .y = 0.58F};
  spec.makeRuns[slotId(ids, 9)] = true;
  spec.carrierId = slotId(ids, 5);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  if (!decision.best)
  {
    FAIL() << "expected a pass option";
  }
  EXPECT_EQ(decision.best->receiverId, slotId(ids, 9));
  EXPECT_GT(decision.best->progression, 0.09F);
}

TEST(MatchScenarioTest, OffsideRunnerIsNeverSelected)
{
  std::vector<std::unique_ptr<Player>> pool;
  Team home = createDummyTeam(1, "Home", 70, pool);
  Team away = createDummyTeam(2, "Away", 70, pool);
  const Ids ids = collectIds(pool);
  const StatsConfig config = createStatsConfig();

  ScenarioSpec spec;
  spec.layout = standardHomeAttackLayout(ids);
  spec.layout[slotId(ids, 5)] = {.x = 0.55F, .y = 0.50F};
  // The striker is well beyond the high defensive line: clearly offside.
  spec.layout[slotId(ids, 9)] = {.x = 0.93F, .y = 0.50F};
  spec.layout[slotId(ids, 10)] = {.x = 0.80F, .y = 0.74F};
  spec.layout[slotId(ids, 1 + 11)] = {.x = 0.87F, .y = 0.12F};
  spec.layout[slotId(ids, 2 + 11)] = {.x = 0.87F, .y = 0.40F};
  spec.layout[slotId(ids, 3 + 11)] = {.x = 0.87F, .y = 0.60F};
  spec.layout[slotId(ids, 4 + 11)] = {.x = 0.87F, .y = 0.88F};
  spec.layout[slotId(ids, 9 + 11)] = {.x = 0.45F, .y = 0.42F};
  spec.layout[slotId(ids, 10 + 11)] = {.x = 0.45F, .y = 0.58F};
  spec.makeRuns[slotId(ids, 9)] = true;
  spec.carrierId = slotId(ids, 5);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  if (!decision.best)
  {
    FAIL() << "expected a pass option";
  }
  EXPECT_NE(decision.best->receiverId, slotId(ids, 9))
      << "An offside runner must never be the selected receiver";
  if (decision.runnerUp)
  {
    EXPECT_NE(decision.runnerUp->receiverId, slotId(ids, 9))
        << "An offside runner must not appear among the candidates either";
  }
  EXPECT_EQ(decision.best->receiverId, slotId(ids, 10))
      << "The onside run in the half-space should be chosen instead";

  const std::string json = engine.getDebugSnapshotJson();
  EXPECT_NE(json.find("\"rejected\":"), std::string::npos);
}

TEST(MatchScenarioTest, PressuredCarrierRecyclesToTheSafeOutlet)
{
  std::vector<std::unique_ptr<Player>> pool;
  Team home = createDummyTeam(1, "Home", 70, pool);
  Team away = createDummyTeam(2, "Away", 70, pool);
  const Ids ids = collectIds(pool);
  const StatsConfig config = createStatsConfig();

  ScenarioSpec spec;
  spec.layout = standardHomeAttackLayout(ids);
  spec.layout[slotId(ids, 5)] = {.x = 0.50F, .y = 0.50F};
  // Immediate presser hugging the carrier.
  spec.layout[slotId(ids, 6 + 11)] = {.x = 0.505F, .y = 0.50F};
  // Safe backward outlet, fully open.
  spec.layout[slotId(ids, 6)] = {.x = 0.40F, .y = 0.50F};
  spec.layout[slotId(ids, 9 + 11)] = {.x = 0.70F, .y = 0.42F};
  spec.layout[slotId(ids, 10 + 11)] = {.x = 0.70F, .y = 0.58F};
  // Every forward lane is blocked or marked so the outlet dominates.
  spec.layout[slotId(ids, 9)] = {.x = 0.62F, .y = 0.50F};
  spec.layout[slotId(ids, 2 + 11)] = {.x = 0.56F, .y = 0.50F};
  spec.layout[slotId(ids, 10)] = {.x = 0.68F, .y = 0.58F};
  spec.layout[slotId(ids, 5 + 11)] = {.x = 0.64F, .y = 0.56F};
  spec.carrierId = slotId(ids, 5);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  if (!decision.best)
  {
    FAIL() << "expected a pass option";
  }
  EXPECT_EQ(decision.best->intent, PassIntent::PRESSURE_RELEASE)
      << "Under direct pressure the safe outlet must win";
  EXPECT_EQ(decision.best->receiverId, slotId(ids, 6));
  EXPECT_LT(decision.best->progression, 0.0F);
}

TEST(MatchScenarioTest, WideSwitchIsChosenWhenTheFarSideIsOpen)
{
  std::vector<std::unique_ptr<Player>> pool;
  Team home = createDummyTeam(1, "Home", 70, pool);
  Team away = createDummyTeam(2, "Away", 70, pool);
  const Ids ids = collectIds(pool);
  const StatsConfig config = createStatsConfig();

  ScenarioSpec spec;
  spec.layout = standardHomeAttackLayout(ids);
  spec.layout[slotId(ids, 5)] = {.x = 0.50F, .y = 0.30F};
  spec.layout[slotId(ids, 8)] = {.x = 0.57F, .y = 0.80F};
  // Block the alternative forward lanes so the far winger dominates.
  spec.layout[slotId(ids, 9)] = {.x = 0.62F, .y = 0.50F};
  spec.layout[slotId(ids, 2 + 11)] = {.x = 0.58F, .y = 0.50F};
  spec.layout[slotId(ids, 10)] = {.x = 0.42F, .y = 0.62F};
  spec.carrierId = slotId(ids, 5);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  if (!decision.best)
  {
    FAIL() << "expected a pass option";
  }
  EXPECT_EQ(decision.best->intent, PassIntent::SWITCH_PLAY);
  EXPECT_EQ(decision.best->receiverId, slotId(ids, 8));
}

TEST(MatchScenarioTest, OverlappingFullbackIsReachableAheadOfTheCarrier)
{
  std::vector<std::unique_ptr<Player>> pool;
  Team home = createDummyTeam(1, "Home", 70, pool);
  Team away = createDummyTeam(2, "Away", 70, pool);
  const Ids ids = collectIds(pool);
  const StatsConfig config = createStatsConfig();

  ScenarioSpec spec;
  spec.layout = standardHomeAttackLayout(ids);
  spec.layout[slotId(ids, 7)] = {.x = 0.58F, .y = 0.14F};
  // The left back runs beyond the winger (overlap).
  spec.layout[slotId(ids, 1)] = {.x = 0.68F, .y = 0.14F};
  spec.makeRuns[slotId(ids, 1)] = true;
  // All other forwards stay behind the carrier so the overlap dominates.
  spec.layout[slotId(ids, 9)] = {.x = 0.42F, .y = 0.38F};
  spec.layout[slotId(ids, 10)] = {.x = 0.42F, .y = 0.62F};
  spec.layout[slotId(ids, 8)] = {.x = 0.44F, .y = 0.84F};
  spec.carrierId = slotId(ids, 7);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  if (!decision.best)
  {
    FAIL() << "expected a pass option";
  }
  EXPECT_EQ(decision.best->receiverId, slotId(ids, 1));
  EXPECT_GT(decision.best->progression, 0.0F)
      << "The overlapping fullback has overtaken the carrier";
}

TEST(MatchScenarioTest, CrossChosenFromWideDeepPosition)
{
  std::vector<std::unique_ptr<Player>> pool;
  Team home = createDummyTeam(1, "Home", 70, pool);
  Team away = createDummyTeam(2, "Away", 70, pool);
  const Ids ids = collectIds(pool);
  const StatsConfig config = createStatsConfig();

  ScenarioSpec spec;
  spec.layout = standardHomeAttackLayout(ids);
  spec.layout[slotId(ids, 8)] = {.x = 0.72F, .y = 0.86F};
  spec.layout[slotId(ids, 9)] = {.x = 0.75F, .y = 0.50F};
  spec.layout[slotId(ids, 10)] = {.x = 0.76F, .y = 0.42F};
  spec.layout[slotId(ids, 7)] = {.x = 0.30F, .y = 0.14F};
  spec.layout[slotId(ids, 5)] = {.x = 0.42F, .y = 0.45F};
  spec.layout[slotId(ids, 6)] = {.x = 0.42F, .y = 0.55F};
  spec.carrierId = slotId(ids, 8);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  if (!decision.best)
  {
    FAIL() << "expected a pass option";
  }
  EXPECT_EQ(decision.best->intent, PassIntent::CROSS);
}

TEST(MatchScenarioTest, CutbackChosenFromTheByline)
{
  std::vector<std::unique_ptr<Player>> pool;
  Team home = createDummyTeam(1, "Home", 70, pool);
  Team away = createDummyTeam(2, "Away", 70, pool);
  const Ids ids = collectIds(pool);
  const StatsConfig config = createStatsConfig();

  ScenarioSpec spec;
  spec.layout = standardHomeAttackLayout(ids);
  spec.layout[slotId(ids, 8)] = {.x = 0.90F, .y = 0.86F};
  spec.layout[slotId(ids, 10)] = {.x = 0.86F, .y = 0.50F};
  spec.layout[slotId(ids, 9)] = {.x = 0.30F, .y = 0.38F};
  spec.layout[slotId(ids, 7)] = {.x = 0.30F, .y = 0.14F};
  spec.layout[slotId(ids, 5)] = {.x = 0.42F, .y = 0.45F};
  spec.layout[slotId(ids, 6)] = {.x = 0.42F, .y = 0.55F};
  spec.carrierId = slotId(ids, 8);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  if (!decision.best)
  {
    FAIL() << "expected a pass option";
  }
  EXPECT_EQ(decision.best->intent, PassIntent::CUTBACK);
}

TEST(MatchScenarioTest, GoalkeeperPlaysTheOpenOutlet)
{
  std::vector<std::unique_ptr<Player>> pool;
  Team home = createDummyTeam(1, "Home", 70, pool);
  Team away = createDummyTeam(2, "Away", 70, pool);
  const Ids ids = collectIds(pool);
  const StatsConfig config = createStatsConfig();

  ScenarioSpec spec;
  spec.layout = standardHomeAttackLayout(ids);
  spec.layout[slotId(ids, 0)] = {.x = 0.04F, .y = 0.50F};
  spec.layout[slotId(ids, 2)] = {.x = 0.26F, .y = 0.50F};
  spec.carrierId = slotId(ids, 0);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  if (!decision.best)
  {
    FAIL() << "expected a pass option";
  }
  EXPECT_EQ(decision.best->receiverId, slotId(ids, 2))
      << "The goalkeeper should pick the open central outlet";
  EXPECT_GT(decision.best->progression, 0.0F);
  EXPECT_GT(decision.best->completionProbability, 0.55F);
}

TEST(MatchScenarioTest, CompletionProbabilityImprovesWhenTheOptionIsOpen)
{
  std::vector<std::unique_ptr<Player>> pool;
  Team home = createDummyTeam(1, "Home", 70, pool);
  Team away = createDummyTeam(2, "Away", 70, pool);
  const Ids ids = collectIds(pool);
  const StatsConfig config = createStatsConfig();

  auto makeSharedSpec = [&ids]()
  {
    ScenarioSpec spec;
    spec.layout = standardHomeAttackLayout(ids);
    spec.layout[slotId(ids, 5)] = {.x = 0.50F, .y = 0.50F};
    spec.layout[slotId(ids, 9)] = {.x = 0.62F, .y = 0.50F};
    spec.carrierId = slotId(ids, 5);
    return spec;
  };

  ScenarioSpec openSpec = makeSharedSpec();
  MatchEngine open = buildEngine(home, away, config);
  ASSERT_TRUE(open.applyScenario(buildScenario(openSpec)));
  const auto& openBest = open.getLastScenarioDecision().best;
  if (!openBest)
  {
    FAIL() << "expected an open-lane pass option";
  }
  const float openCompletion = openBest->completionProbability;

  ScenarioSpec blockedSpec = makeSharedSpec();
  blockedSpec.layout[slotId(ids, 2 + 11)] = {.x = 0.56F, .y = 0.50F};
  MatchEngine blocked = buildEngine(home, away, config);
  ASSERT_TRUE(blocked.applyScenario(buildScenario(blockedSpec)));
  const auto& blockedBest = blocked.getLastScenarioDecision().best;
  if (!blockedBest)
  {
    FAIL() << "expected a blocked-lane pass option";
  }
  const float blockedCompletion = blockedBest->completionProbability;

  EXPECT_GT(openCompletion, blockedCompletion)
      << "An open lane must raise the completion probability compared with a "
         "blocked lane";
}

TEST(MatchScenarioTest, EvaluationIsDeterministicAcrossEngines)
{
  std::vector<std::unique_ptr<Player>> pool;
  Team home = createDummyTeam(1, "Home", 70, pool);
  Team away = createDummyTeam(2, "Away", 70, pool);
  const Ids ids = collectIds(pool);
  const StatsConfig config = createStatsConfig();

  ScenarioSpec spec;
  spec.layout = standardHomeAttackLayout(ids);
  spec.layout[slotId(ids, 5)] = {.x = 0.50F, .y = 0.50F};
  spec.layout[slotId(ids, 9)] = {.x = 0.66F, .y = 0.50F};
  spec.layout[slotId(ids, 7)] = {.x = 0.72F, .y = 0.12F};
  spec.layout[slotId(ids, 7 + 11)] = {.x = 0.58F, .y = 0.50F};
  spec.carrierId = slotId(ids, 5);
  const MatchScenario scenario = buildScenario(spec);

  MatchEngine first = buildEngine(home, away, config);
  MatchEngine second = buildEngine(home, away, config);
  ASSERT_TRUE(first.applyScenario(scenario));
  ASSERT_TRUE(second.applyScenario(scenario));

  const ScenarioDecision& a = first.getLastScenarioDecision();
  const ScenarioDecision& b = second.getLastScenarioDecision();
  if (!a.best)
  {
    FAIL() << "expected a pass option";
  }
  if (!b.best)
  {
    FAIL() << "expected a pass option";
  }
  EXPECT_EQ(a.best->receiverId, b.best->receiverId);
  EXPECT_EQ(a.best->intent, b.best->intent);
  EXPECT_FLOAT_EQ(a.best->utility, b.best->utility);
  EXPECT_EQ(a.reason, b.reason);
  EXPECT_EQ(first.getDebugSnapshotJson(), second.getDebugSnapshotJson());

  const std::string json = first.getDebugSnapshotJson();
  EXPECT_NE(json.find("\"decision\":{\"reason\":"), std::string::npos);
  EXPECT_NE(json.find("\"best\":{\"receiver\":"), std::string::npos);
  EXPECT_NE(json.find("\"rejected\":"), std::string::npos);
}

TEST(MatchScenarioTest, CentralOneOnOnePrefersTheShot)
{
  std::vector<std::unique_ptr<Player>> pool;
  Team home = createDummyTeam(1, "Home", 70, pool);
  Team away = createDummyTeam(2, "Away", 70, pool);
  const Ids ids = collectIds(pool);
  const StatsConfig config = createStatsConfig();

  ScenarioSpec spec;
  spec.layout = standardHomeAttackLayout(ids);
  spec.layout[slotId(ids, 9)] = {.x = 0.905F, .y = 0.50F};
  spec.layout[slotId(ids, 0 + 11)] = {.x = 0.965F, .y = 0.50F};
  spec.carrierId = slotId(ids, 9);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  EXPECT_EQ(decision.action, ScenarioAction::SHOT) << decision.reason;
  EXPECT_FALSE(decision.reason.empty());
}

TEST(MatchScenarioTest, EmptyGoalPromptsTheShot)
{
  std::vector<std::unique_ptr<Player>> pool;
  Team home = createDummyTeam(1, "Home", 70, pool);
  Team away = createDummyTeam(2, "Away", 70, pool);
  const Ids ids = collectIds(pool);
  const StatsConfig config = createStatsConfig();

  ScenarioSpec spec;
  spec.layout = standardHomeAttackLayout(ids);
  // The away keeper is stranded at the centre circle: the goal mouth is open.
  spec.layout[slotId(ids, 9)] = {.x = 0.905F, .y = 0.50F};
  spec.layout[slotId(ids, 0 + 11)] = {.x = 0.50F, .y = 0.50F};
  spec.carrierId = slotId(ids, 9);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  EXPECT_EQ(decision.action, ScenarioAction::SHOT) << decision.reason;
}

TEST(MatchScenarioTest, PenaltySpotProducesAimedShot)
{
  std::vector<std::unique_ptr<Player>> pool;
  Team home = createDummyTeam(1, "Home", 70, pool);
  Team away = createDummyTeam(2, "Away", 70, pool);
  const Ids ids = collectIds(pool);
  const StatsConfig config = createStatsConfig();

  ScenarioSpec spec;
  spec.layout = standardHomeAttackLayout(ids);
  spec.layout[slotId(ids, 9)] = {.x = 0.885F, .y = 0.50F};
  spec.layout[slotId(ids, 0 + 11)] = {.x = 0.965F, .y = 0.50F};
  spec.carrierId = slotId(ids, 9);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec), MatchState::PENALTY));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  EXPECT_EQ(decision.action, ScenarioAction::SHOT) << decision.reason;
}

TEST(MatchScenarioTest, WideLowXgShotYieldsToTheOpenCutback)
{
  std::vector<std::unique_ptr<Player>> pool;
  Team home = createDummyTeam(1, "Home", 70, pool);
  Team away = createDummyTeam(2, "Away", 70, pool);
  const Ids ids = collectIds(pool);
  const StatsConfig config = createStatsConfig();

  ScenarioSpec spec;
  spec.layout = standardHomeAttackLayout(ids);
  // Carrier is wide on the byline with a hopeless angle; a striker is open in
  // the middle for an easy cutback.
  spec.layout[slotId(ids, 8)] = {.x = 0.90F, .y = 0.86F};
  spec.layout[slotId(ids, 9)] = {.x = 0.86F, .y = 0.50F};
  spec.carrierId = slotId(ids, 8);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  EXPECT_EQ(decision.action, ScenarioAction::PASS) << decision.reason;
  if (!decision.best)
  {
    FAIL() << "expected a pass option";
  }
  EXPECT_EQ(decision.best->intent, PassIntent::CUTBACK);
}

TEST(MatchScenarioTest, PressuredLongShotRecyclesToTheSafeOutlet)
{
  std::vector<std::unique_ptr<Player>> pool;
  Team home = createDummyTeam(1, "Home", 70, pool);
  Team away = createDummyTeam(2, "Away", 70, pool);
  const Ids ids = collectIds(pool);
  const StatsConfig config = createStatsConfig();

  ScenarioSpec spec;
  spec.layout = standardHomeAttackLayout(ids);
  // Carrier is well outside the final third and immediately pressed, with the
  // forward carrying channels bottled up.
  spec.layout[slotId(ids, 6)] = {.x = 0.56F, .y = 0.55F};
  spec.layout[slotId(ids, 6 + 11)] = {.x = 0.58F, .y = 0.56F};
  spec.layout[slotId(ids, 5 + 11)] = {.x = 0.72F, .y = 0.78F};
  spec.layout[slotId(ids, 7 + 11)] = {.x = 0.70F, .y = 0.25F};
  spec.layout[slotId(ids, 9 + 11)] = {.x = 0.66F, .y = 0.55F};
  spec.layout[slotId(ids, 10 + 11)] = {.x = 0.70F, .y = 0.50F};
  // A safe central outlet sits well behind the press.
  spec.layout[slotId(ids, 5)] = {.x = 0.44F, .y = 0.52F};
  spec.carrierId = slotId(ids, 6);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  EXPECT_EQ(decision.action, ScenarioAction::PASS) << decision.reason;
  if (!decision.best)
  {
    FAIL() << "expected a pass option";
  }
}
