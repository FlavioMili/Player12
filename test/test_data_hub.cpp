// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <utility>
#include <vector>

#include "model/data_hub.h"
#include "model/guidance.h"

namespace
{
constexpr TeamID CLUB = 3;

MatchReport report(int day, TeamID home, TeamID away, int home_goals,
                   int away_goals, float home_xg, float away_xg)
{
  MatchReport result;
  result.date = GameDateValue(2025, 10, static_cast<uint8_t>(day));
  result.home_team_id = home;
  result.away_team_id = away;
  result.match_type = MatchType::LEAGUE;
  result.home_goals = static_cast<uint8_t>(home_goals);
  result.away_goals = static_cast<uint8_t>(away_goals);
  result.home_stats.shots = static_cast<uint16_t>(home_xg * 10.0f);
  result.away_stats.shots = static_cast<uint16_t>(away_xg * 10.0f);
  result.home_stats.expected_goals = home_xg;
  result.away_stats.expected_goals = away_xg;
  result.home_stats.passes_attempted = 400;
  result.home_stats.passes_completed = 320;
  result.away_stats.passes_attempted = 400;
  result.away_stats.passes_completed = 300;
  return result;
}

void addLine(MatchReport& match, PlayerID player, TeamID team, int minutes,
             float rating, int goals = 0)
{
  PlayerMatchLine line;
  line.player_id = player;
  line.team_id = team;
  line.minutes = static_cast<uint8_t>(minutes);
  line.rating = rating;
  line.goals = static_cast<uint8_t>(goals);
  match.players.push_back(line);
}

ManagedMatchSnapshot snapshot(int day, TeamID home, TeamID away)
{
  ManagedMatchSnapshot result;
  result.date = GameDateValue(2025, 10, static_cast<uint8_t>(day));
  result.home_id = home;
  result.away_id = away;
  result.managed_home = home == CLUB;
  result.set_piece_shots = {3, 1};
  result.set_piece_goals = {1, 0};
  result.corners = {6, 2};
  ShotRecord own;
  own.home = result.managed_home;
  own.x = 0.9f;
  own.y = 0.4f;
  own.xg = 0.35f;
  own.outcome = ShotOutcome::Goal;
  ShotRecord other = own;
  other.home = !result.managed_home;
  other.outcome = ShotOutcome::Saved;
  result.shots = {own, own, other};
  result.players = {{10, 90, 50, 45, 2, 1, 0.2f, 1, 2},
                    {11, 90, 30, 15, 0, 2, 0.5f, 0, 0}};
  return result;
}
}  // namespace

TEST(DataHubTest, TrendAndRollingAverageReconcileWithReports)
{
  std::vector reports = {report(9, CLUB, 20, 2, 0, 2.0f, 0.5f),
                         report(2, 21, CLUB, 1, 1, 1.0f, 1.0f),
                         report(16, CLUB, 22, 0, 1, 0.6f, 1.4f),
                         report(23, 23, CLUB, 0, 3, 0.4f, 3.0f)};
  DataHubInput input;
  input.team_id = CLUB;
  input.team_reports = reports;
  const TeamAnalytics analytics = DataHub::buildTeamAnalytics(input);
  ASSERT_EQ(analytics.trend.size(), 4U);
  EXPECT_TRUE(analytics.hasEnoughMatches());
  // Oldest first, seen from the club's side.
  EXPECT_EQ(analytics.trend[0].opponent, 21);
  EXPECT_FALSE(analytics.trend[0].home);
  EXPECT_FLOAT_EQ(analytics.trend[3].xg_for, 3.0f);
  EXPECT_FLOAT_EQ(analytics.trend[3].xg_against, 0.4f);
  EXPECT_EQ(analytics.trend[3].goals_for, 3);
  const float sum_for = 1.0f + 2.0f + 0.6f + 3.0f;
  EXPECT_NEAR(analytics.rolling_xg_for.back(), sum_for / 4.0f, 1e-4f);
  EXPECT_NEAR(analytics.rolling_xg_for.front(), 1.0f, 1e-4f);
  EXPECT_NEAR(
      analytics
          .metrics[static_cast<size_t>(std::to_underlying(HubMetric::XgFor))]
          .team,
      sum_for / 4.0f, 1e-4f);
}

TEST(DataHubTest, FewMatchesAndScoreOnlyReportsGiveAnEmptyState)
{
  MatchReport bare = report(5, CLUB, 20, 1, 0, 0.0f, 0.0f);
  bare.home_stats = {};
  bare.away_stats = {};
  std::vector<MatchReport> reports = {bare,
                                      report(6, CLUB, 21, 1, 0, 1.0f, 0.5f)};
  DataHubInput input;
  input.team_id = CLUB;
  input.team_reports = reports;
  const TeamAnalytics analytics = DataHub::buildTeamAnalytics(input);
  EXPECT_EQ(analytics.trend.size(), 1U);
  EXPECT_FALSE(analytics.hasEnoughMatches());
  EXPECT_FALSE(DataHub::hasStatistics(bare));
}

TEST(DataHubTest, LeagueAveragesAndRanks)
{
  // Club creates 2.0 xG a match, the others 1.0: best in the league.
  std::vector league = {report(1, CLUB, 20, 2, 0, 2.0f, 1.0f),
                        report(2, 21, CLUB, 0, 1, 1.0f, 2.0f),
                        report(3, 20, 21, 1, 1, 1.0f, 1.0f),
                        report(4, 22, 20, 1, 1, 1.0f, 1.0f)};
  DataHubInput input;
  input.team_id = CLUB;
  input.team_reports = std::span(league).first(2);
  input.league_reports = league;
  const TeamAnalytics analytics = DataHub::buildTeamAnalytics(input);
  EXPECT_EQ(analytics.league_matches, 4);
  const MetricComparison& xg_for =
      analytics
          .metrics[static_cast<size_t>(std::to_underlying(HubMetric::XgFor))];
  EXPECT_NEAR(xg_for.team, 2.0f, 1e-4f);
  EXPECT_NEAR(xg_for.league, 10.0f / 8.0f, 1e-4f);
  EXPECT_EQ(xg_for.rank, 1);
  EXPECT_EQ(xg_for.ranked_teams, 4);
  const MetricComparison& conceded = analytics.metrics[static_cast<size_t>(
      std::to_underlying(HubMetric::XgAgainst))];
  EXPECT_TRUE(conceded.lower_is_better);
  EXPECT_NEAR(conceded.team, 1.0f, 1e-4f);
  // Pass completion: 80% at home, 75% away.
  EXPECT_NEAR(
      analytics.metrics[static_cast<size_t>(HubMetric::PassCompletion)].team,
      77.5f, 1e-3f);
}

TEST(DataHubTest, SetPiecesAndShotMapComeFromSnapshots)
{
  std::vector reports = {report(1, CLUB, 20, 2, 0, 2.0f, 0.5f),
                         report(8, 21, CLUB, 0, 1, 0.5f, 1.0f)};
  const std::vector snapshots = {snapshot(1, CLUB, 20), snapshot(8, 21, CLUB)};
  DataHubInput input;
  input.team_id = CLUB;
  input.team_reports = reports;
  input.snapshots = snapshots;
  const TeamAnalytics analytics = DataHub::buildTeamAnalytics(input);
  EXPECT_EQ(analytics.tracked_matches, 2);
  EXPECT_EQ(analytics.set_pieces.shots_for, 6);
  EXPECT_EQ(analytics.set_pieces.goals_for, 2);
  EXPECT_EQ(analytics.set_pieces.corners_for, 12);
  EXPECT_EQ(analytics.shots_for.size(), 4U);
  EXPECT_EQ(analytics.shots_against.size(), 2U);
  EXPECT_EQ(analytics.set_pieces.all_shots_for, 4);
  const float plotted = std::accumulate(
      analytics.shots_for.begin(), analytics.shots_for.end(), 0.0f,
      [](float sum, const ShotRecord& shot) { return sum + shot.xg; });
  EXPECT_NEAR(plotted, 1.4f, 1e-4f);
}

TEST(DataHubTest, PlayerRowsPer90RatingTrendAndPassShare)
{
  std::vector<MatchReport> reports;
  for (int day = 1; day <= 6; ++day)
  {
    MatchReport match = report(day, CLUB, 20 + day, 1, 0, 1.0f, 0.5f);
    addLine(match, 10, CLUB, 90, day <= 3 ? 6.0f : 8.0f, day == 6 ? 1 : 0);
    addLine(match, 11, CLUB, 45, 6.5f);
    addLine(match, 99, 20 + day, 90, 7.0f);  // Opponent.
    reports.push_back(match);
  }
  const std::vector snapshots = {snapshot(1, CLUB, 21)};
  DataHubInput input;
  input.team_id = CLUB;
  input.team_reports = reports;
  input.snapshots = snapshots;
  const auto rows = DataHub::buildPlayerAnalytics(input);
  ASSERT_EQ(rows.size(), 2U);
  const PlayerAnalyticsRow& star = rows.front();
  EXPECT_EQ(star.player, 10U);
  EXPECT_EQ(star.appearances, 6);
  EXPECT_EQ(star.minutes, 540);
  EXPECT_EQ(star.goals, 1);
  EXPECT_NEAR(star.average_rating, 7.0f, 1e-4f);
  EXPECT_NEAR(star.rating_trend, 1.0f, 1e-4f);
  EXPECT_EQ(star.ratings.size(), 6U);
  EXPECT_NEAR(star.pass_share, 45.0f / 60.0f, 1e-4f);
  EXPECT_NEAR(PlayerAnalyticsRow::per90(1.0f, star.minutes), 1.0f / 6.0f,
              1e-4f);
  EXPECT_FLOAT_EQ(PlayerAnalyticsRow::per90(3.0f, 0), 0.0f);
}

TEST(DataHubTest, SnapshotJsonRoundTrip)
{
  const ManagedMatchSnapshot original = snapshot(4, CLUB, 20);
  const auto restored = ManagedMatchSnapshot::fromJson(original.toJson());
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->date, original.date);
  EXPECT_EQ(restored->home_id, original.home_id);
  EXPECT_EQ(restored->set_piece_shots, original.set_piece_shots);
  EXPECT_EQ(restored->corners, original.corners);
  ASSERT_EQ(restored->shots.size(), original.shots.size());
  EXPECT_EQ(restored->shots[0].outcome, ShotOutcome::Goal);
  EXPECT_FLOAT_EQ(restored->shots[2].xg, 0.35f);
  ASSERT_EQ(restored->players.size(), 2U);
  EXPECT_EQ(restored->players[0].passes_completed, 45);
  EXPECT_FALSE(ManagedMatchSnapshot::fromJson("not json").has_value());
  EXPECT_FALSE(ManagedMatchSnapshot::fromJson("{}").has_value());
}

TEST(DataHubTest, GuidanceKeepsTheNewestSnapshotsAndReplacesReplays)
{
  CareerGuidance guidance;
  for (std::size_t index = 0; index < CareerGuidance::MAX_SNAPSHOTS + 5;
       ++index)
  {
    ManagedMatchSnapshot entry = snapshot(1, CLUB, 20);
    entry.date = GameDateValue(2025, 7, 1) + index;
    guidance.addSnapshot(entry);
  }
  EXPECT_EQ(guidance.getSnapshots().size(), CareerGuidance::MAX_SNAPSHOTS);
  EXPECT_EQ(guidance.getSnapshots().front().date,
            GameDateValue(2025, 7, 1) + std::size_t{5});
  ManagedMatchSnapshot replay = guidance.getSnapshots().back();
  replay.corners = {1, 1};
  guidance.addSnapshot(replay);
  EXPECT_EQ(guidance.getSnapshots().size(), CareerGuidance::MAX_SNAPSHOTS);
  EXPECT_EQ(guidance.getSnapshots().back().corners[0], 1);
}
