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
#include "model/match_scenario.h"
#include "model/player.h"
#include "model/team.h"

namespace
{
StatsConfig createStatsConfig()
{
  StatsConfig config;
  config.possible_stats = {"Pace",      "Shooting",  "Passing",
                           "Dribbling", "Defending", "Physicality",
                           "Stamina",   "Vision",    "Goalkeeping"};
  config.role_focus["Goalkeeper"] = {{"Goalkeeping", "Vision", "Physicality"},
                                     {0.7, 0.2, 0.1}};
  config.role_focus["Defender"] = {
      {"Defending", "Physicality", "Pace", "Vision"}, {0.4, 0.3, 0.15, 0.15}};
  config.role_focus["Midfielder"] = {
      {"Passing", "Vision", "Stamina", "Dribbling"}, {0.3, 0.3, 0.2, 0.2}};
  config.role_focus["Striker"] = {
      {"Shooting", "Pace", "Dribbling", "Physicality"}, {0.4, 0.2, 0.2, 0.2}};
  return config;
}

Team createDummyTeam(TeamID id, const std::string& name, int rating,
                     std::vector<std::unique_ptr<Player>>& players)
{
  using enum PlayerRole;
  Team team(id, 1, name, 50'000'000, {}, Strategy{}, Lineup{});
  static constexpr std::array ROLES = {GK, LB, CB, CB, RB, CM,
                                       CM, LW, RW, ST, ST};
  static constexpr std::array<Vector2F, 11> POSITIONS = {{{0.04f, 0.50f},
                                                          {0.18f, 0.12f},
                                                          {0.18f, 0.38f},
                                                          {0.18f, 0.62f},
                                                          {0.18f, 0.88f},
                                                          {0.43f, 0.35f},
                                                          {0.43f, 0.65f},
                                                          {0.68f, 0.16f},
                                                          {0.68f, 0.84f},
                                                          {0.78f, 0.38f},
                                                          {0.78f, 0.62f}}};

  for (uint32_t index = 0; index < 11; ++index)
  {
    const auto value = static_cast<float>(rating);
    const std::map<std::string, float> stats = {
        {"Pace", value},      {"Shooting", value},  {"Passing", value},
        {"Dribbling", value}, {"Defending", value}, {"Physicality", value},
        {"Stamina", value},   {"Vision", value},    {"Goalkeeping", value}};
    auto player = std::make_unique<Player>(
        static_cast<PlayerID>(id) * 100U + index, id, "First",
        std::to_string(index), ROLES[index], Language::EN, 100'000, 0, 25, 3,
        180, Foot::Right, stats);
    if (ROLES[index] == GK)
      team.getLineup().setGoalkeeper(player.get());
    else
      team.getLineup().addOutfieldPlayer(player.get(), POSITIONS[index]);
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
      {{ids.home[0], {0.02f, 0.50f}},
       {ids.home[1], {0.16f, 0.12f}},
       {ids.home[2], {0.17f, 0.35f}},
       {ids.home[3], {0.17f, 0.65f}},
       {ids.home[4], {0.16f, 0.88f}},
       {ids.home[5], {0.42f, 0.45f}},
       {ids.home[6], {0.42f, 0.55f}},
       {ids.home[7], {0.62f, 0.12f}},
       {ids.home[8], {0.62f, 0.88f}},
       {ids.home[9], {0.68f, 0.42f}},
       {ids.home[10], {0.68f, 0.58f}}}};
  const std::array<std::pair<std::uint32_t, Vector2F>, 11> awaySlots = {
      {{ids.away[0], {0.965f, 0.50f}},
       {ids.away[1], {0.88f, 0.12f}},
       {ids.away[2], {0.88f, 0.38f}},
       {ids.away[3], {0.88f, 0.62f}},
       {ids.away[4], {0.88f, 0.88f}},
       {ids.away[5], {0.74f, 0.42f}},
       {ids.away[6], {0.74f, 0.58f}},
       {ids.away[7], {0.60f, 0.12f}},
       {ids.away[8], {0.60f, 0.88f}},
       {ids.away[9], {0.45f, 0.42f}},
       {ids.away[10], {0.45f, 0.58f}}}};
  std::map<std::uint32_t, Vector2F> layout;
  for (const auto& [playerId, position] : homeSlots)
    layout[playerId] = position;
  for (const auto& [playerId, position] : awaySlots)
    layout[playerId] = position;
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
    placement.position = position;
    placement.makingRun = spec.makeRuns.contains(id) && spec.makeRuns.at(id);
    scenario.players.push_back(placement);
  }
  scenario.carrierId = spec.carrierId;
  scenario.ballPosition = spec.layout.at(spec.carrierId);
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
  spec.layout[slotId(ids, 5)] = {0.50f, 0.50f};
  spec.layout[slotId(ids, 9)] = {0.66f, 0.50f};
  spec.layout[slotId(ids, 7)] = {0.72f, 0.12f};
  spec.layout[slotId(ids, 7 + 11)] = {0.58f, 0.50f};
  spec.carrierId = slotId(ids, 5);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  ASSERT_TRUE(decision.best.has_value());
  EXPECT_EQ(decision.best->receiverId, slotId(ids, 7))
      << "The blocked central lane must be avoided in favour of the open "
         "flank option";
  EXPECT_NE(decision.best->receiverId, slotId(ids, 9));
  EXPECT_LT(decision.best->completionProbability, 0.99f);
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
  spec.layout[slotId(ids, 5)] = {0.48f, 0.50f};
  spec.layout[slotId(ids, 6)] = {0.63f, 0.55f};
  // Keep the forwards behind the ball so the midfielder option dominates.
  spec.layout[slotId(ids, 9)] = {0.32f, 0.38f};
  spec.layout[slotId(ids, 10)] = {0.32f, 0.62f};
  spec.carrierId = slotId(ids, 5);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  ASSERT_TRUE(decision.best.has_value());
  EXPECT_EQ(decision.best->receiverId, slotId(ids, 6));
  EXPECT_EQ(decision.best->intent, PassIntent::PROGRESSIVE);
  EXPECT_GT(decision.best->progression, 0.0f);
  EXPECT_GT(decision.best->completionProbability, 0.60f);
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
  spec.layout[slotId(ids, 5)] = {0.55f, 0.50f};
  spec.layout[slotId(ids, 9)] = {0.80f, 0.50f};
  spec.layout[slotId(ids, 10)] = {0.80f, 0.74f};
  // High defensive line: away back four on the edge of the box.
  spec.layout[slotId(ids, 1 + 11)] = {0.87f, 0.12f};
  spec.layout[slotId(ids, 2 + 11)] = {0.87f, 0.40f};
  spec.layout[slotId(ids, 3 + 11)] = {0.87f, 0.60f};
  spec.layout[slotId(ids, 4 + 11)] = {0.87f, 0.88f};
  spec.layout[slotId(ids, 9 + 11)] = {0.45f, 0.42f};
  spec.layout[slotId(ids, 10 + 11)] = {0.45f, 0.58f};
  spec.makeRuns[slotId(ids, 9)] = true;
  spec.carrierId = slotId(ids, 5);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  ASSERT_TRUE(decision.best.has_value());
  EXPECT_EQ(decision.best->receiverId, slotId(ids, 9));
  EXPECT_GT(decision.best->progression, 0.09f);
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
  spec.layout[slotId(ids, 5)] = {0.55f, 0.50f};
  // The striker is well beyond the high defensive line: clearly offside.
  spec.layout[slotId(ids, 9)] = {0.93f, 0.50f};
  spec.layout[slotId(ids, 10)] = {0.80f, 0.74f};
  spec.layout[slotId(ids, 1 + 11)] = {0.87f, 0.12f};
  spec.layout[slotId(ids, 2 + 11)] = {0.87f, 0.40f};
  spec.layout[slotId(ids, 3 + 11)] = {0.87f, 0.60f};
  spec.layout[slotId(ids, 4 + 11)] = {0.87f, 0.88f};
  spec.layout[slotId(ids, 9 + 11)] = {0.45f, 0.42f};
  spec.layout[slotId(ids, 10 + 11)] = {0.45f, 0.58f};
  spec.makeRuns[slotId(ids, 9)] = true;
  spec.carrierId = slotId(ids, 5);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  ASSERT_TRUE(decision.best.has_value());
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
  spec.layout[slotId(ids, 5)] = {0.50f, 0.50f};
  // Immediate presser hugging the carrier.
  spec.layout[slotId(ids, 6 + 11)] = {0.505f, 0.50f};
  // Safe backward outlet, fully open.
  spec.layout[slotId(ids, 6)] = {0.40f, 0.50f};
  spec.layout[slotId(ids, 9 + 11)] = {0.70f, 0.42f};
  spec.layout[slotId(ids, 10 + 11)] = {0.70f, 0.58f};
  // Every forward lane is blocked or marked so the outlet dominates.
  spec.layout[slotId(ids, 9)] = {0.62f, 0.50f};
  spec.layout[slotId(ids, 2 + 11)] = {0.56f, 0.50f};
  spec.layout[slotId(ids, 10)] = {0.68f, 0.58f};
  spec.layout[slotId(ids, 5 + 11)] = {0.64f, 0.56f};
  spec.carrierId = slotId(ids, 5);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  ASSERT_TRUE(decision.best.has_value());
  EXPECT_EQ(decision.best->intent, PassIntent::PRESSURE_RELEASE)
      << "Under direct pressure the safe outlet must win";
  EXPECT_EQ(decision.best->receiverId, slotId(ids, 6));
  EXPECT_LT(decision.best->progression, 0.0f);
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
  spec.layout[slotId(ids, 5)] = {0.50f, 0.30f};
  spec.layout[slotId(ids, 8)] = {0.57f, 0.80f};
  // Block the alternative forward lanes so the far winger dominates.
  spec.layout[slotId(ids, 9)] = {0.62f, 0.50f};
  spec.layout[slotId(ids, 2 + 11)] = {0.58f, 0.50f};
  spec.layout[slotId(ids, 10)] = {0.42f, 0.62f};
  spec.carrierId = slotId(ids, 5);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  ASSERT_TRUE(decision.best.has_value());
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
  spec.layout[slotId(ids, 7)] = {0.58f, 0.14f};
  // The left back runs beyond the winger (overlap).
  spec.layout[slotId(ids, 1)] = {0.68f, 0.14f};
  spec.makeRuns[slotId(ids, 1)] = true;
  // All other forwards stay behind the carrier so the overlap dominates.
  spec.layout[slotId(ids, 9)] = {0.42f, 0.38f};
  spec.layout[slotId(ids, 10)] = {0.42f, 0.62f};
  spec.layout[slotId(ids, 8)] = {0.44f, 0.84f};
  spec.carrierId = slotId(ids, 7);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  ASSERT_TRUE(decision.best.has_value());
  EXPECT_EQ(decision.best->receiverId, slotId(ids, 1));
  EXPECT_GT(decision.best->progression, 0.0f)
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
  spec.layout[slotId(ids, 8)] = {0.72f, 0.86f};
  spec.layout[slotId(ids, 9)] = {0.75f, 0.50f};
  spec.layout[slotId(ids, 10)] = {0.76f, 0.42f};
  spec.layout[slotId(ids, 7)] = {0.30f, 0.14f};
  spec.layout[slotId(ids, 5)] = {0.42f, 0.45f};
  spec.layout[slotId(ids, 6)] = {0.42f, 0.55f};
  spec.carrierId = slotId(ids, 8);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  ASSERT_TRUE(decision.best.has_value());
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
  spec.layout[slotId(ids, 8)] = {0.90f, 0.86f};
  spec.layout[slotId(ids, 10)] = {0.86f, 0.50f};
  spec.layout[slotId(ids, 9)] = {0.30f, 0.38f};
  spec.layout[slotId(ids, 7)] = {0.30f, 0.14f};
  spec.layout[slotId(ids, 5)] = {0.42f, 0.45f};
  spec.layout[slotId(ids, 6)] = {0.42f, 0.55f};
  spec.carrierId = slotId(ids, 8);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  ASSERT_TRUE(decision.best.has_value());
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
  spec.layout[slotId(ids, 0)] = {0.04f, 0.50f};
  spec.layout[slotId(ids, 2)] = {0.26f, 0.50f};
  spec.carrierId = slotId(ids, 0);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  ASSERT_TRUE(decision.best.has_value());
  EXPECT_EQ(decision.best->receiverId, slotId(ids, 2))
      << "The goalkeeper should pick the open central outlet";
  EXPECT_GT(decision.best->progression, 0.0f);
  EXPECT_GT(decision.best->completionProbability, 0.55f);
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
    spec.layout[slotId(ids, 5)] = {0.50f, 0.50f};
    spec.layout[slotId(ids, 9)] = {0.62f, 0.50f};
    spec.carrierId = slotId(ids, 5);
    return spec;
  };

  ScenarioSpec openSpec = makeSharedSpec();
  MatchEngine open = buildEngine(home, away, config);
  ASSERT_TRUE(open.applyScenario(buildScenario(openSpec)));
  ASSERT_TRUE(open.getLastScenarioDecision().best.has_value());
  const float openCompletion =
      open.getLastScenarioDecision().best->completionProbability;

  ScenarioSpec blockedSpec = makeSharedSpec();
  blockedSpec.layout[slotId(ids, 2 + 11)] = {0.56f, 0.50f};
  MatchEngine blocked = buildEngine(home, away, config);
  ASSERT_TRUE(blocked.applyScenario(buildScenario(blockedSpec)));
  ASSERT_TRUE(blocked.getLastScenarioDecision().best.has_value());
  const float blockedCompletion =
      blocked.getLastScenarioDecision().best->completionProbability;

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
  spec.layout[slotId(ids, 5)] = {0.50f, 0.50f};
  spec.layout[slotId(ids, 9)] = {0.66f, 0.50f};
  spec.layout[slotId(ids, 7)] = {0.72f, 0.12f};
  spec.layout[slotId(ids, 7 + 11)] = {0.58f, 0.50f};
  spec.carrierId = slotId(ids, 5);
  const MatchScenario scenario = buildScenario(spec);

  MatchEngine first = buildEngine(home, away, config);
  MatchEngine second = buildEngine(home, away, config);
  ASSERT_TRUE(first.applyScenario(scenario));
  ASSERT_TRUE(second.applyScenario(scenario));

  const ScenarioDecision& a = first.getLastScenarioDecision();
  const ScenarioDecision& b = second.getLastScenarioDecision();
  ASSERT_TRUE(a.best.has_value());
  ASSERT_TRUE(b.best.has_value());
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
  spec.layout[slotId(ids, 9)] = {0.905f, 0.50f};
  spec.layout[slotId(ids, 0 + 11)] = {0.965f, 0.50f};
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
  spec.layout[slotId(ids, 9)] = {0.905f, 0.50f};
  spec.layout[slotId(ids, 0 + 11)] = {0.50f, 0.50f};
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
  spec.layout[slotId(ids, 9)] = {0.885f, 0.50f};
  spec.layout[slotId(ids, 0 + 11)] = {0.965f, 0.50f};
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
  spec.layout[slotId(ids, 8)] = {0.90f, 0.86f};
  spec.layout[slotId(ids, 9)] = {0.86f, 0.50f};
  spec.carrierId = slotId(ids, 8);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  EXPECT_EQ(decision.action, ScenarioAction::PASS) << decision.reason;
  ASSERT_TRUE(decision.best.has_value());
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
  spec.layout[slotId(ids, 6)] = {0.56f, 0.55f};
  spec.layout[slotId(ids, 6 + 11)] = {0.58f, 0.56f};
  spec.layout[slotId(ids, 5 + 11)] = {0.72f, 0.78f};
  spec.layout[slotId(ids, 7 + 11)] = {0.70f, 0.25f};
  spec.layout[slotId(ids, 9 + 11)] = {0.66f, 0.55f};
  spec.layout[slotId(ids, 10 + 11)] = {0.70f, 0.50f};
  // A safe central outlet sits well behind the press.
  spec.layout[slotId(ids, 5)] = {0.44f, 0.52f};
  spec.carrierId = slotId(ids, 6);

  MatchEngine engine = buildEngine(home, away, config);
  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));

  const ScenarioDecision& decision = engine.getLastScenarioDecision();
  EXPECT_EQ(decision.action, ScenarioAction::PASS) << decision.reason;
  ASSERT_TRUE(decision.best.has_value());
}
