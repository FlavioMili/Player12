// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <format>
#include <memory>
#include <vector>

#include "controller/game_controller.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/opposition_report.h"

namespace
{
constexpr TeamID OPPONENT = 7;
constexpr std::uint64_t WORLD_SEED = 20250702;

MatchReport report(int day, TeamID home, TeamID away, int home_goals,
                   int away_goals, float home_xg, float away_xg,
                   float home_possession = 50.0f, int home_completed = 400,
                   int away_completed = 400)
{
  MatchReport result;
  result.date = GameDateValue(2025, 9, static_cast<uint8_t>(day));
  result.home_team_id = home;
  result.away_team_id = away;
  result.match_type = MatchType::LEAGUE;
  result.home_goals = static_cast<uint8_t>(home_goals);
  result.away_goals = static_cast<uint8_t>(away_goals);
  result.home_stats.shots = 12;
  result.away_stats.shots = 12;
  result.home_stats.expected_goals = home_xg;
  result.away_stats.expected_goals = away_xg;
  result.home_stats.possession = home_possession;
  result.away_stats.possession = 100.0f - home_possession;
  result.home_stats.passes_attempted = 500;
  result.home_stats.passes_completed = static_cast<uint16_t>(home_completed);
  result.away_stats.passes_attempted = 500;
  result.away_stats.passes_completed = static_cast<uint16_t>(away_completed);
  return result;
}

/** League of evenly matched sides: 1.3 xG each, 80% passing. */
std::vector<MatchReport> league()
{
  std::vector<MatchReport> reports;
  for (int day = 1; day <= 20; ++day)
    reports.push_back(report(day, 100 + day, 200 + day, 1, 1, 1.3f, 1.3f));
  return reports;
}

OppositionPlayer player(PlayerID id, PlayerRole role, bool starter, int goals,
                        int assists = 0)
{
  OppositionPlayer entry;
  entry.player = id;
  entry.name = std::format("Player {}", id);
  entry.role = role;
  entry.likely_starter = starter;
  entry.appearances = 6;
  entry.goals = goals;
  entry.assists = assists;
  entry.knowledge = 40.0f;
  return entry;
}

std::vector<OppositionPlayer> squad()
{
  using enum PlayerRole;
  return {player(1, GK, true, 0),    player(2, CB, true, 0),
          player(3, CB, true, 0),    player(4, LB, true, 0),
          player(5, RB, true, 0),    player(6, CM, true, 1),
          player(7, CM, true, 0, 3), player(8, CAM, true, 1),
          player(9, LW, true, 0),    player(10, RW, true, 1),
          player(11, ST, true, 6),   player(12, ST, false, 0)};
}
}  // namespace

TEST(OppositionReportTest, FormationFromTheLikelyEleven)
{
  using enum PlayerRole;
  const std::vector roles = {GK, CB, CB, LB, RB, CDM, CM, LM, RM, ST, ST};
  EXPECT_EQ(formationLabel(roles), "4-4-2");
  EXPECT_EQ(formationLabel({}), "");
}

TEST(OppositionReportTest, AveragesAndFormComeFromTheLatestFiveMatches)
{
  // Seven matches: an old 0-5 defeat falls out of the window.
  std::vector reports = {report(1, OPPONENT, 50, 0, 5, 0.2f, 3.0f),
                         report(2, OPPONENT, 51, 0, 5, 0.2f, 3.0f),
                         report(3, OPPONENT, 52, 2, 0, 2.0f, 0.5f),
                         report(4, 53, OPPONENT, 0, 2, 0.5f, 2.0f),
                         report(5, OPPONENT, 54, 1, 1, 2.0f, 0.5f),
                         report(6, 55, OPPONENT, 1, 3, 0.5f, 2.0f),
                         report(7, OPPONENT, 56, 2, 1, 2.0f, 0.5f)};
  const std::vector<MatchReport> leagueReports = league();
  OppositionInput input;
  input.opponent = OPPONENT;
  input.reports = reports;
  input.league_reports = leagueReports;
  input.squad = squad();
  const OppositionReport result = buildOppositionReport(input);
  ASSERT_TRUE(result.enough_data);
  EXPECT_EQ(result.recent.matches, 5);
  EXPECT_EQ(result.form, (std::vector<int>{1, 1, 0, 1, 1}));
  EXPECT_NEAR(result.recent.xg_for, 2.0f, 1e-4f);
  EXPECT_NEAR(result.recent.xg_against, 0.5f, 1e-4f);
  EXPECT_NEAR(result.league.xg_for, 1.3f, 1e-4f);
  EXPECT_EQ(result.formation, "4-3-3");
  EXPECT_EQ(result.likely_xi.size(), 11U);
  EXPECT_NEAR(result.confidence, 40.0f, 1e-4f);
  // Strong attack, strong defence and in form.
  const auto keys = [](const std::vector<AnalysisLine>& lines)
  {
    std::vector<std::string> out;
    for (const AnalysisLine& line : lines) out.push_back(line.key);
    return out;
  };
  const auto strengths = keys(result.strengths);
  EXPECT_TRUE(std::ranges::contains(strengths, "OPPOSITION_STRENGTH_ATTACK"));
  EXPECT_TRUE(std::ranges::contains(strengths, "OPPOSITION_STRENGTH_DEFENCE"));
  EXPECT_TRUE(std::ranges::contains(strengths, "OPPOSITION_STRENGTH_FORM"));
  EXPECT_TRUE(result.weaknesses.empty());
}

TEST(OppositionReportTest, KeyPlayersAndTheMainThreatIsMarked)
{
  std::vector<MatchReport> reports;
  for (int day = 1; day <= 4; ++day)
    reports.push_back(report(day, OPPONENT, 60 + day, 2, 1, 1.9f, 1.2f));
  const std::vector<MatchReport> leagueReports = league();
  OppositionInput input;
  input.opponent = OPPONENT;
  input.reports = reports;
  input.league_reports = leagueReports;
  input.squad = squad();
  const OppositionReport result = buildOppositionReport(input);
  ASSERT_EQ(result.key_players.size(), OppositionRules::KEY_PLAYERS);
  EXPECT_EQ(result.key_players.front().player.player, 11U);
  EXPECT_EQ(result.key_players.front().reason.args.front(), "6");
  ASSERT_FALSE(result.counters.empty());
  // 6 of 9 goals: mark him, and the counter names him.
  EXPECT_EQ(result.counters.front().mark, 11U);
  EXPECT_EQ(result.counters.front().action.key, "OPPOSITION_COUNTER_MARK");
  EXPECT_LE(result.counters.size(), OppositionRules::MAX_COUNTERS);
}

TEST(OppositionReportTest, PossessionSidesAreCounteredCompactly)
{
  std::vector<MatchReport> reports;
  for (int day = 1; day <= 5; ++day)
    reports.push_back(
        report(day, OPPONENT, 70 + day, 1, 1, 1.3f, 1.3f, 62.0f, 330, 400));
  const std::vector<MatchReport> leagueReports = league();
  OppositionInput input;
  input.opponent = OPPONENT;
  input.reports = reports;
  input.league_reports = leagueReports;
  const OppositionReport result = buildOppositionReport(input);
  const auto counter = [&](const char* key)
  {
    return std::ranges::find(result.counters, std::string(key),
                             [](const CounterTactic& c)
                             { return c.action.key; });
  };
  // 62% possession but only 66% passing: press them and stay compact.
  ASSERT_NE(counter("OPPOSITION_COUNTER_PRESS"), result.counters.end());
  ASSERT_NE(counter("OPPOSITION_COUNTER_COMPACT"), result.counters.end());
  EXPECT_GT(counter("OPPOSITION_COUNTER_PRESS")->shift.pressing, 0.0f);
  EXPECT_LT(counter("OPPOSITION_COUNTER_COMPACT")->shift.compactness, 0.0f);

  Strategy strategy;
  const Strategy countered =
      applyCounter(strategy, *counter("OPPOSITION_COUNTER_PRESS"));
  EXPECT_GT(countered.getSliders().pressing, strategy.getSliders().pressing);
  EXPECT_LE(countered.getSliders().pressing, 1.0f);
}

TEST(OppositionReportTest, TooFewMatchesGiveNoTacticalAdvice)
{
  const std::vector reports = {report(1, OPPONENT, 80, 3, 0, 2.5f, 0.2f)};
  const std::vector<MatchReport> leagueReports = league();
  OppositionInput input;
  input.opponent = OPPONENT;
  input.reports = reports;
  input.league_reports = leagueReports;
  const OppositionReport result = buildOppositionReport(input);
  EXPECT_FALSE(result.enough_data);
  EXPECT_TRUE(result.strengths.empty());
  ASSERT_EQ(result.counters.size(), 1U);
  EXPECT_EQ(result.counters.front().action.key, "OPPOSITION_COUNTER_NO_DATA");
  const StrategySliders shift = result.counters.front().shift;
  EXPECT_FLOAT_EQ(shift.pressing + shift.riskTaking + shift.compactness, 0.0f);
}

TEST(OppositionReportTest, PlanStoresOneInstructionPerPlayer)
{
  using enum OppositionInstruction;
  OppositionPlan plan;
  plan.set(OPPONENT, 11, TightMark);
  plan.set(OPPONENT, 11, Press);
  plan.set(OPPONENT, 9, ShowWeakFoot);
  plan.set(8, 3, Press);
  EXPECT_EQ(plan.get(OPPONENT, 11), Press);
  EXPECT_EQ(plan.forOpponent(OPPONENT).size(), 2U);
  plan.set(OPPONENT, 9, None);
  EXPECT_EQ(plan.forOpponent(OPPONENT).size(), 1U);
  plan.clear(OPPONENT);
  EXPECT_TRUE(plan.forOpponent(OPPONENT).empty());
  EXPECT_EQ(plan.all().size(), 1U);
}

TEST(OppositionReportTest, ControllerReportUsesScoutedEstimatesOnly)
{
  Logger::init();
  const int slot = 330'000 + getpid() % 100'000 * 10;
  struct Cleanup
  {
    explicit Cleanup(int saveSlot) : slot(saveSlot) {}
    Cleanup(const Cleanup&) = delete;
    Cleanup& operator=(const Cleanup&) = delete;

    int slot;
    ~Cleanup() { RuntimePaths::removeSave(slot); }
  };
  const Cleanup cleanup{slot};
  GameController controller;
  controller.newGame(slot, WORLD_SEED);
  controller.selectManagedTeam(controller.getTeams().front().get().getId());
  const auto fixture = controller.getNextManagedFixture();
  ASSERT_TRUE(fixture.has_value());
  const OppositionReport result =
      controller.getOppositionReport(fixture->opponent);
  EXPECT_EQ(result.opponent, fixture->opponent);
  EXPECT_FALSE(result.likely_xi.empty());
  for (const OppositionPlayer& entry : result.likely_xi)
  {
    const auto row = controller.getScoutedRow(entry.player);
    ASSERT_TRUE(row.has_value());
    if (entry.estimate >= 0.0f) EXPECT_FLOAT_EQ(entry.estimate, row->overall);
  }
  EXPECT_FALSE(controller.wasOppositionReportViewed(fixture->opponent));
  controller.markOppositionReportViewed(fixture->opponent);
  EXPECT_TRUE(controller.wasOppositionReportViewed(fixture->opponent));

  const StrategySliders before =
      controller.getManagedTeam()->get().getStrategy().getSliders();
  CounterTactic counter;
  counter.shift.pressing = 0.1f;
  counter.mark = result.likely_xi.front().player;
  ASSERT_TRUE(controller.applyCounterTactic(counter));
  EXPECT_NEAR(
      controller.getManagedTeam()->get().getStrategy().getSliders().pressing,
      std::min(1.0f, before.pressing + 0.1f), 1e-4f);
  EXPECT_EQ(
      controller.getOppositionInstruction(fixture->opponent, counter.mark),
      OppositionInstruction::TightMark);
}
