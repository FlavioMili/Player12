// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "database/database_connection.h"
#include "database/gamedata.h"
#include "database/migrations/migrations.h"
#include "database/repositories/competition_repository.h"
#include "database/repositories/fixture_repository.h"
#include "database/repositories/league_repository.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/calendar.h"
#include "model/competition.h"
#include "model/competition_manager.h"
#include "model/discipline.h"
#include "model/game.h"
#include "model/league.h"
#include "model/match_report.h"
#include "model/player.h"
#include "model/season_history.h"
#include "model/standings.h"
#include "model/team.h"

namespace
{
constexpr LeagueID TOP_LEAGUE = 1;
constexpr LeagueID SECOND_LEAGUE = 6;
constexpr LeagueID OTHER_COUNTRY = 2;
constexpr size_t DIVISION_SIZE = 20;

void addDivision(GameData& gamedata, LeagueID league_id,
                 std::optional<LeagueID> parent, TeamID first_team)
{
  std::vector<TeamID> team_ids;
  for (size_t i = 0; i < DIVISION_SIZE; ++i)
  {
    const auto team_id = static_cast<TeamID>(first_team + i);
    gamedata.addTeam(
        team_id, Team(team_id, league_id, std::format("Club {}", team_id), 0));
    team_ids.push_back(team_id);
  }
  gamedata.addLeague(
      league_id, League(league_id, std::format("Country {} League", league_id),
                        team_ids, parent));
}

/** Two linked divisions (40 clubs) plus a single-division country. */
void buildWorld(GameData& gamedata)
{
  addDivision(gamedata, TOP_LEAGUE, std::nullopt, 101);
  addDivision(gamedata, SECOND_LEAGUE, TOP_LEAGUE, 601);
  addDivision(gamedata, OTHER_COUNTRY, std::nullopt, 201);
}

Match played(TeamID home, TeamID away, GameDateValue date, uint8_t home_goals,
             uint8_t away_goals, LeagueID league = TOP_LEAGUE)
{
  Match match(home, away, date, MatchType::LEAGUE, league);
  match.setPlayedResult(home_goals, away_goals);
  return match;
}

bool consecutiveOrSame(const GameDateValue& earlier, const GameDateValue& later)
{
  return earlier == later || SeasonCalendar::addDays(earlier, 1) == later;
}

/** Settles every unplayed cup tie on @p date without running the engine. */
void settleCupTies(Calendar& calendar, const GameDateValue& date)
{
  for (Match& match : calendar.getMatchesForDateMutable(date))
  {
    if (match.isPlayed() || match.getMatchType() != MatchType::CUP) continue;
    const unsigned selector =
        (match.getHomeTeamId() + match.getAwayTeamId()) % 3U;
    if (selector == 0)
      match.setPlayedResult(2, 1);
    else if (selector == 1)
      match.setKnockoutResult(1, 1, true,
                              std::make_pair(uint8_t{4}, uint8_t{3}));
    else
      match.setPlayedResult(0, 1);
  }
}
}  // namespace

// ---------------- Season calendar rules ----------------

TEST(SeasonCalendarTest, WeekdaysAndDayArithmetic)
{
  EXPECT_EQ(SeasonCalendar::dayOfWeek(GameDateValue(2025, 9, 27)),
            SeasonCalendar::SATURDAY);
  EXPECT_EQ(SeasonCalendar::dayOfWeek(GameDateValue(2026, 1, 1)), 3);
  EXPECT_EQ(SeasonCalendar::addDays(GameDateValue(2025, 12, 31), 1),
            GameDateValue(2026, 1, 1));
  EXPECT_EQ(SeasonCalendar::addDays(GameDateValue(2024, 3, 1), -1),
            GameDateValue(2024, 2, 29));
  EXPECT_EQ(SeasonCalendar::seasonStartYear(GameDateValue(2026, 5, 10)), 2025);
  EXPECT_EQ(SeasonCalendar::seasonStartYear(GameDateValue(2025, 7, 1)), 2025);
}

TEST(SeasonCalendarTest, LeagueRoundsUseWeekendsMidweeksAndBreaks)
{
  const auto dates = SeasonCalendar::leagueRoundDates(2025, 38);
  ASSERT_EQ(dates.size(), 38u);
  const auto cup_dates =
      SeasonCalendar::cupRoundDates(2025, SeasonCalendar::MAX_CUP_ROUNDS + 1);
  size_t midweeks = 0;
  for (size_t i = 0; i < dates.size(); ++i)
  {
    const uint8_t weekday = SeasonCalendar::dayOfWeek(dates[i]);
    ASSERT_TRUE(weekday == SeasonCalendar::SATURDAY ||
                weekday == SeasonCalendar::WEDNESDAY)
        << dates[i].toString();
    if (weekday == SeasonCalendar::WEDNESDAY) ++midweeks;
    EXPECT_FALSE(SeasonCalendar::isBlackout(dates[i])) << dates[i].toString();
    if (weekday == SeasonCalendar::SATURDAY)
      EXPECT_FALSE(
          SeasonCalendar::isBlackout(SeasonCalendar::addDays(dates[i], 1)));
    EXPECT_FALSE(std::ranges::contains(cup_dates, dates[i]))
        << "League round on a cup date " << dates[i].toString();
    if (i > 0) EXPECT_TRUE(dates[i - 1] < dates[i]);
  }
  EXPECT_GE(midweeks, 1u) << "20-team leagues need a few midweek rounds";
  EXPECT_LE(midweeks, 8u);
  EXPECT_FALSE(dates.front() < GameDateValue(2025, 8, 15));
  EXPECT_FALSE(GameDateValue(2026, 5, 24) < dates.back());

  EXPECT_TRUE(SeasonCalendar::isInternationalBreak(GameDateValue(2025, 9, 10)));
  EXPECT_TRUE(SeasonCalendar::isInternationalBreak(GameDateValue(2026, 3, 28)));
  EXPECT_FALSE(
      SeasonCalendar::isInternationalBreak(GameDateValue(2025, 9, 20)));
  EXPECT_TRUE(SeasonCalendar::isWinterBreak(GameDateValue(2025, 12, 28)));
  EXPECT_FALSE(SeasonCalendar::isWinterBreak(GameDateValue(2026, 1, 17)));

  // Four international windows: each leaves a gap of roughly two weeks.
  size_t long_gaps = 0;
  for (size_t i = 1; i < dates.size(); ++i)
    if (SeasonCalendar::addDays(dates[i - 1], 12) < dates[i]) ++long_gaps;
  EXPECT_GE(long_gaps, 4u);
}

TEST(SeasonCalendarTest, CupRoundsAreMidweekWithWeekendFinal)
{
  const auto dates = SeasonCalendar::cupRoundDates(2025, 6);
  ASSERT_EQ(dates.size(), 6u);
  for (size_t i = 0; i + 1 < dates.size(); ++i)
  {
    EXPECT_EQ(SeasonCalendar::dayOfWeek(dates[i]), SeasonCalendar::WEDNESDAY);
    EXPECT_FALSE(SeasonCalendar::isBlackout(dates[i]));
    EXPECT_TRUE(dates[i] < dates[i + 1]);
  }
  EXPECT_EQ(dates.back(),
            SeasonCalendar::addDays(SeasonCalendar::leagueEnd(2025), 7));
  EXPECT_EQ(SeasonCalendar::dayOfWeek(dates.back()), SeasonCalendar::SATURDAY);
}

TEST(CalendarTest, GeneratesBalancedRealisticSeason)
{
  GameData gamedata;
  buildWorld(gamedata);
  Calendar calendar;
  calendar.generate(gamedata, GameDateValue(2025, 7, 2));

  const GameDateValue league_start = SeasonCalendar::leagueStart(2025);
  for (const auto& [team_id, team] : gamedata.getTeams())
  {
    const auto fixtures = calendar.getTeamFixtures(team_id);
    size_t league_matches = 0;
    size_t home_matches = 0;
    std::map<TeamID, int> meetings;
    size_t friendlies = 0;
    for (size_t i = 0; i < fixtures.size(); ++i)
    {
      const Match& match = fixtures[i];
      if (i > 0)
        EXPECT_FALSE(
            consecutiveOrSame(fixtures[i - 1].getDate(), match.getDate()))
            << "Team " << team_id << " plays on consecutive days "
            << match.getDate().toString();
      if (match.getMatchType() == MatchType::FRIENDLY)
      {
        // Pre-season weeks: Tuesday to Sunday, at most one a week.
        ++friendlies;
        EXPECT_NE(SeasonCalendar::dayOfWeek(match.getDate()),
                  SeasonCalendar::MONDAY);
        EXPECT_TRUE(SeasonCalendar::addDays(match.getDate(), 4) < league_start);
        if (i > 0 && fixtures[i - 1].getMatchType() == MatchType::FRIENDLY)
          EXPECT_TRUE(SeasonCalendar::addDays(fixtures[i - 1].getDate(), 1) <
                      match.getDate());
        continue;
      }
      if (match.getMatchType() != MatchType::LEAGUE) continue;
      ++league_matches;
      EXPECT_EQ(match.getCompetitionId(), team.getLeagueId());
      // Weekend rounds run Friday to Monday, midweek rounds Tuesday and
      // Wednesday (Thursday when Tuesday is still in a break); every fixture
      // gets a kick-off time.
      if (SeasonCalendar::dayOfWeek(match.getDate()) ==
          SeasonCalendar::THURSDAY)
        EXPECT_TRUE(SeasonCalendar::isBlackout(
            SeasonCalendar::addDays(match.getDate(), -2)))
            << match.getDate().toString();
      EXPECT_GT(match.getScheduledKickoff(), 0) << match.getDate().toString();
      EXPECT_FALSE(SeasonCalendar::isBlackout(match.getDate()));
      EXPECT_FALSE(SeasonCalendar::isContinentalWeek(match.getDate()));
      if (match.getHomeTeamId() == team_id) ++home_matches;
      ++meetings[match.getHomeTeamId() == team_id ? match.getAwayTeamId()
                                                  : match.getHomeTeamId()];
    }
    EXPECT_EQ(league_matches, 38u) << "Team " << team_id;
    EXPECT_EQ(home_matches, 19u) << "Team " << team_id;
    EXPECT_EQ(meetings.size(), 19u);
    for (const auto& [opponent, count] : meetings) EXPECT_EQ(count, 2);
    EXPECT_LE(friendlies, SeasonCalendar::PRESEASON_FRIENDLIES);
    EXPECT_GE(friendlies, 3u);
  }

  // Cup round 1: 40 clubs -> 8 preliminary ties among second-tier clubs.
  const auto italy = Competitions::cupStatus(calendar, gamedata, TOP_LEAGUE);
  EXPECT_EQ(italy.total_rounds, 6);
  ASSERT_EQ(italy.rounds.size(), 1u);
  EXPECT_EQ(italy.rounds.front().ties.size(), 8u);
  const auto first_cup_date = SeasonCalendar::cupRoundDates(2025, 6).front();
  for (const Match& tie : italy.rounds.front().ties)
  {
    // Spread from Tuesday to Thursday around the round's Wednesday.
    EXPECT_FALSE(tie.getDate() < SeasonCalendar::addDays(first_cup_date, -1));
    EXPECT_FALSE(SeasonCalendar::addDays(first_cup_date, 1) < tie.getDate());
    EXPECT_GE(tie.getHomeTeamId(), 601);
    EXPECT_GE(tie.getAwayTeamId(), 601);
  }
  const auto other = Competitions::cupStatus(calendar, gamedata, OTHER_COUNTRY);
  EXPECT_EQ(other.total_rounds, 5);
  ASSERT_EQ(other.rounds.size(), 1u);
  EXPECT_EQ(other.rounds.front().ties.size(), 4u);
  EXPECT_EQ(Competitions::countryRoots(gamedata),
            (std::vector<LeagueID>{TOP_LEAGUE, OTHER_COUNTRY}));

  Calendar again;
  again.generate(gamedata, GameDateValue(2025, 7, 2));
  ASSERT_EQ(again.getFullCalendar().size(), calendar.getFullCalendar().size());
  for (const auto& [date, matches] : calendar.getFullCalendar())
  {
    const auto& other_matches = again.getMatchesForDate(date);
    ASSERT_EQ(other_matches.size(), matches.size());
    for (size_t i = 0; i < matches.size(); ++i)
    {
      EXPECT_EQ(other_matches[i].getHomeTeamId(), matches[i].getHomeTeamId());
      EXPECT_EQ(other_matches[i].getAwayTeamId(), matches[i].getAwayTeamId());
      EXPECT_EQ(other_matches[i].getScheduledKickoff(),
                matches[i].getScheduledKickoff());
    }
  }
}

// ---------------- Standings ----------------

class StandingsTest : public ::testing::Test
{
 protected:
  void addTeams(const std::vector<std::pair<TeamID, std::string>>& teams,
                TieBreakRule rule = TieBreakRule::GOAL_DIFFERENCE)
  {
    std::vector<TeamID> ids;
    for (const auto& [id, name] : teams)
    {
      gamedata.addTeam(id, Team(id, TOP_LEAGUE, name, 0));
      ids.push_back(id);
    }
    gamedata.addLeague(
        TOP_LEAGUE, League(TOP_LEAGUE, "Test League", ids, std::nullopt, rule));
  }

  std::vector<StandingRow> table() const
  {
    return Standings::compute(gamedata.getLeagues().at(TOP_LEAGUE), calendar,
                              gamedata);
  }

  static std::vector<TeamID> order(const std::vector<StandingRow>& rows)
  {
    std::vector<TeamID> ids;
    for (const StandingRow& row : rows) ids.push_back(row.team_id);
    return ids;
  }

  GameData gamedata;
  Calendar calendar;
  GameDateValue day{2025, 9, 6};
  GameDateValue nextDay()
  {
    day = SeasonCalendar::addDays(day, 7);
    return day;
  }
};

TEST_F(StandingsTest, CountsResultsFormAndIgnoresOtherMatches)
{
  addTeams({{1, "Alpha"}, {2, "Bravo"}, {3, "Charlie"}});
  calendar.addMatch(played(1, 2, nextDay(), 2, 0));
  calendar.addMatch(played(2, 1, nextDay(), 1, 1));
  calendar.addMatch(played(1, 3, nextDay(), 0, 3));
  calendar.addMatch(played(3, 1, nextDay(), 0, 1));
  calendar.addMatch(played(1, 2, nextDay(), 4, 1));
  calendar.addMatch(played(2, 1, nextDay(), 0, 2));
  calendar.addMatch(Match(1, 3, nextDay(), MatchType::LEAGUE, TOP_LEAGUE));
  Match cup(1, 2, nextDay(), MatchType::CUP, TOP_LEAGUE);
  cup.setPlayedResult(0, 5);
  calendar.addMatch(cup);
  calendar.addMatch(played(1, 99, nextDay(), 0, 9));

  const auto rows = table();
  ASSERT_EQ(rows.size(), 3u);
  const StandingRow& alpha = rows.front();
  EXPECT_EQ(alpha.team_id, 1);
  EXPECT_EQ(alpha.position, 1);
  EXPECT_EQ(alpha.played, 6);
  EXPECT_EQ(alpha.won, 4);
  EXPECT_EQ(alpha.drawn, 1);
  EXPECT_EQ(alpha.lost, 1);
  EXPECT_EQ(alpha.goals_for, 10);
  EXPECT_EQ(alpha.goals_against, 5);
  EXPECT_EQ(alpha.goal_difference, 5);
  EXPECT_EQ(alpha.points, 13);
  EXPECT_EQ(alpha.form, "DLWWW") << "last five, oldest first";
}

TEST_F(StandingsTest, GoalDifferenceThenGoalsScored)
{
  addTeams({{1, "Alpha"},
            {2, "Bravo"},
            {3, "Charlie"},
            {4, "Delta"},
            {5, "Echo"},
            {6, "Foxtrot"}});
  calendar.addMatch(played(1, 4, nextDay(), 1, 0));  // 1: GD +1
  calendar.addMatch(played(2, 5, nextDay(), 3, 1));  // 2: GD +2, GF 3
  calendar.addMatch(played(3, 6, nextDay(), 2, 0));  // 3: GD +2, GF 2
  const auto ids = order(table());
  EXPECT_EQ(std::vector<TeamID>(ids.begin(), ids.begin() + 3),
            (std::vector<TeamID>{2, 3, 1}));
}

TEST_F(StandingsTest, HeadToHeadBreaksTiesBeforeNames)
{
  addTeams({{1, "Zulu"}, {2, "Alpha"}, {3, "Mike"}, {4, "Whiskey"}});
  calendar.addMatch(played(1, 2, nextDay(), 1, 0));
  calendar.addMatch(played(3, 1, nextDay(), 1, 0));
  calendar.addMatch(played(2, 3, nextDay(), 1, 0));
  calendar.addMatch(played(4, 3, nextDay(), 5, 0));
  // 1 and 2 are level on points, goal difference and goals scored; 1 won
  // the head-to-head although "Alpha" sorts first.
  EXPECT_EQ(order(table()), (std::vector<TeamID>{4, 1, 2, 3}));
}

TEST_F(StandingsTest, HeadToHeadRuleComesBeforeGoalDifference)
{
  addTeams({{1, "Alpha"}, {2, "Bravo"}, {3, "Charlie"}, {4, "Delta"}},
           TieBreakRule::HEAD_TO_HEAD);
  calendar.addMatch(played(1, 2, nextDay(), 2, 1));
  calendar.addMatch(played(2, 3, nextDay(), 6, 0));
  calendar.addMatch(played(3, 1, nextDay(), 1, 0));
  calendar.addMatch(played(3, 4, nextDay(), 0, 0));
  // 1 and 2 have 3 points; 2 has the far better goal difference but lost
  // the head-to-head.
  EXPECT_EQ(order(table()), (std::vector<TeamID>{3, 1, 2, 4}));
}

TEST_F(StandingsTest, GoalDifferenceRuleIgnoresHeadToHeadWhenGoalsDiffer)
{
  addTeams({{1, "Alpha"}, {2, "Bravo"}, {3, "Charlie"}, {4, "Delta"}});
  calendar.addMatch(played(1, 2, nextDay(), 2, 1));
  calendar.addMatch(played(2, 3, nextDay(), 6, 0));
  calendar.addMatch(played(3, 1, nextDay(), 1, 0));
  calendar.addMatch(played(3, 4, nextDay(), 0, 0));
  EXPECT_EQ(order(table()), (std::vector<TeamID>{3, 2, 1, 4}));
}

TEST_F(StandingsTest, NameThenIdWhenNothingSeparatesTeams)
{
  addTeams({{9, "Bravo"}, {3, "Charlie"}, {7, "Alpha"}, {4, "Alpha"}});
  EXPECT_EQ(order(table()), (std::vector<TeamID>{4, 7, 9, 3}));
}

TEST_F(StandingsTest, PointsDoNotOverflowPastByteRange)
{
  addTeams({{1, "Alpha"}, {2, "Bravo"}});
  for (int i = 0; i < 90; ++i) calendar.addMatch(played(1, 2, nextDay(), 1, 0));
  EXPECT_EQ(table().front().points, 270);

  League league(1, "Big", {1});
  league.setPoints(1, 300);
  EXPECT_EQ(league.getPoints(1), 300);
}

// ---------------- Knockout resolution ----------------

TEST(KnockoutTest, PenaltyConversionIsRealistic)
{
  EXPECT_FLOAT_EQ(Competitions::penaltyConversionProbability(50.0f, 50.0f),
                  0.75f);
  EXPECT_GT(Competitions::penaltyConversionProbability(85.0f, 50.0f), 0.75f);
  EXPECT_LT(Competitions::penaltyConversionProbability(50.0f, 85.0f), 0.75f);
  EXPECT_LE(Competitions::penaltyConversionProbability(100.0f, 1.0f), 0.92f);
  EXPECT_GE(Competitions::penaltyConversionProbability(1.0f, 100.0f), 0.55f);

  std::mt19937 rng(1234);
  const std::vector takers(11, 0.75f);
  int home_wins = 0;
  int sudden_death = 0;
  constexpr int SHOOTOUTS = 20'000;
  for (int i = 0; i < SHOOTOUTS; ++i)
  {
    const auto result = Competitions::simulateShootout(takers, takers, rng);
    ASSERT_NE(result.home, result.away);
    const int margin = std::abs(result.home - result.away);
    if (std::max(result.home, result.away) > 5) EXPECT_EQ(margin, 1);
    EXPECT_LE(margin, 3) << "best-of-five ends as soon as it is decided";
    if (result.home > 5 || result.away > 5) ++sudden_death;
    if (result.home > result.away) ++home_wins;
  }
  EXPECT_NEAR(static_cast<double>(home_wins) / SHOOTOUTS, 0.5, 0.03);
  EXPECT_GT(sudden_death, 0);

  std::mt19937 first(77);
  std::mt19937 second(77);
  const auto a = Competitions::simulateShootout(takers, takers, first);
  const auto b = Competitions::simulateShootout(takers, takers, second);
  EXPECT_EQ(a.home, b.home);
  EXPECT_EQ(a.away, b.away);
}

TEST(KnockoutTest, DrawnTiesAlwaysProduceAWinner)
{
  const Team home(1, 1, "Home", 0);
  const Team away(2, 1, "Away", 0);
  const StatsConfig config{};
  int shootouts = 0;
  constexpr int TIES = 2'000;
  for (uint32_t seed = 0; seed < TIES; ++seed)
  {
    const auto resolution =
        Competitions::resolveDrawnKnockout(home, away, config, seed);
    if (resolution.penalties)
    {
      ++shootouts;
      EXPECT_EQ(resolution.home_extra_goals, resolution.away_extra_goals);
      EXPECT_NE(resolution.home_penalties, resolution.away_penalties);
    }
    else
    {
      EXPECT_NE(resolution.home_extra_goals, resolution.away_extra_goals);
    }
  }
  EXPECT_GT(shootouts, TIES * 35 / 100);
  EXPECT_LT(shootouts, TIES * 75 / 100);

  Match tie(1, 2, GameDateValue(2025, 9, 24), MatchType::CUP, 1, 1);
  tie.setKnockoutResult(1, 1, true, std::make_pair(uint8_t{3}, uint8_t{5}));
  EXPECT_EQ(tie.getWinnerId(), TeamID{2});
  EXPECT_TRUE(tie.wentToExtraTime());
  EXPECT_TRUE(tie.wentToPenalties());
  Match league(1, 2, GameDateValue(2025, 9, 20), MatchType::LEAGUE, 1, 1);
  league.setPlayedResult(2, 2);
  EXPECT_FALSE(league.getWinnerId().has_value());
}

// ---------------- Domestic cup ----------------

TEST(CupTest, BracketRunsToASingleWinner)
{
  GameData gamedata;
  buildWorld(gamedata);
  Calendar calendar;
  calendar.generate(gamedata, GameDateValue(2025, 7, 2));
  EXPECT_EQ(Competitions::cupRoundCount(40), 6);
  EXPECT_EQ(Competitions::cupRoundCount(20), 5);
  EXPECT_EQ(Competitions::cupRoundCount(16), 4);
  EXPECT_EQ(Competitions::cupRoundCount(1), 0);

  for (int guard = 0; guard < 16; ++guard)
  {
    const auto status = Competitions::cupStatus(calendar, gamedata, TOP_LEAGUE);
    if (status.winner.has_value()) break;
    // A round's ties are spread over several days: settle all of them.
    GameDateValue round_date = status.rounds.back().ties.front().getDate();
    for (const LeagueID cup : {TOP_LEAGUE, OTHER_COUNTRY})
    {
      // Keep the owning status alive while iterating its ties (GCC 14
      // does not extend this intermediate temporary's lifetime).
      const auto cup_status = Competitions::cupStatus(calendar, gamedata, cup);
      ASSERT_FALSE(cup_status.rounds.empty());
      for (const Match& tie : cup_status.rounds.back().ties)
      {
        settleCupTies(calendar, tie.getDate());
        if (round_date < tie.getDate()) round_date = tie.getDate();
      }
    }
    Competitions::drawPendingCupRounds(calendar, gamedata, 2025, round_date);
  }

  for (const LeagueID cup : {TOP_LEAGUE, OTHER_COUNTRY})
  {
    const auto status = Competitions::cupStatus(calendar, gamedata, cup);
    ASSERT_TRUE(status.winner.has_value()) << "cup " << int{cup};
    ASSERT_TRUE(status.runner_up.has_value());
    EXPECT_EQ(status.rounds.size(), status.total_rounds);
    EXPECT_EQ(status.remaining, std::vector<TeamID>{*status.winner});
    EXPECT_EQ(status.rounds.back().ties.size(), 1u);
    std::set<TeamID> eliminated;
    for (size_t r = 0; r < status.rounds.size(); ++r)
    {
      const auto& round = status.rounds[r];
      EXPECT_EQ(round.stage, r + 1);
      EXPECT_TRUE(round.complete);
      std::set<TeamID> seen;
      for (const Match& tie : round.ties)
      {
        EXPECT_TRUE(seen.insert(tie.getHomeTeamId()).second);
        EXPECT_TRUE(seen.insert(tie.getAwayTeamId()).second);
        EXPECT_FALSE(eliminated.contains(tie.getHomeTeamId()));
        EXPECT_FALSE(eliminated.contains(tie.getAwayTeamId()));
        if (r > 0)
          EXPECT_TRUE(status.rounds[r - 1].ties.front().getDate() <
                      tie.getDate());
      }
      for (const Match& tie : round.ties)
      {
        const TeamID winner = *tie.getWinnerId();
        eliminated.insert(winner == tie.getHomeTeamId() ? tie.getAwayTeamId()
                                                        : tie.getHomeTeamId());
      }
    }
    EXPECT_EQ(Competitions::cupRoundLabelKey(status.total_rounds,
                                             status.total_rounds),
              std::string("CUP_ROUND_FINAL"));
  }
  const auto italy = Competitions::cupStatus(calendar, gamedata, TOP_LEAGUE);
  EXPECT_EQ(italy.rounds[1].ties.size(), 16u) << "8 winners + 24 byes";
  EXPECT_EQ(Competitions::cupName(gamedata, TOP_LEAGUE), "Country 1 Cup");
}

// ---------------- Promotion / relegation ----------------

TEST(PromotionTest, TopThreeUpBottomThreeDown)
{
  GameData gamedata;
  buildWorld(gamedata);
  EXPECT_EQ(Competitions::leagueTier(gamedata, SECOND_LEAGUE), 2);
  EXPECT_EQ(Competitions::rootLeague(gamedata, SECOND_LEAGUE), TOP_LEAGUE);

  std::unordered_map<LeagueID, std::vector<StandingRow>> tables;
  for (const LeagueID league_id : {TOP_LEAGUE, SECOND_LEAGUE, OTHER_COUNTRY})
  {
    for (const TeamID team_id :
         gamedata.getLeagues().at(league_id).getTeamIDs())
    {
      StandingRow row;
      row.team_id = team_id;
      tables[league_id].push_back(row);
    }
  }
  const auto movements = Competitions::computeLeagueMovements(gamedata, tables);
  ASSERT_EQ(movements.size(), 6u);
  std::set<TeamID> up;
  std::set<TeamID> down;
  for (const auto& movement : movements)
  {
    if (movement.to == TOP_LEAGUE)
      up.insert(movement.team_id);
    else
      down.insert(movement.team_id);
  }
  EXPECT_EQ(up, (std::set<TeamID>{601, 602, 603}));
  EXPECT_EQ(down, (std::set<TeamID>{118, 119, 120}));

  Competitions::applyLeagueMovements(gamedata, movements);
  EXPECT_EQ(gamedata.getTeam(601)->get().getLeagueId(), TOP_LEAGUE);
  EXPECT_EQ(gamedata.getTeam(120)->get().getLeagueId(), SECOND_LEAGUE);
  EXPECT_EQ(gamedata.getLeagues().at(TOP_LEAGUE).getTeamIDs().size(),
            DIVISION_SIZE);
  EXPECT_EQ(gamedata.getLeagues().at(SECOND_LEAGUE).getTeamIDs().size(),
            DIVISION_SIZE);
  EXPECT_TRUE(std::ranges::contains(
      gamedata.getLeagues().at(TOP_LEAGUE).getTeamIDs(), TeamID{602}));
}

// ---------------- Player season stats ----------------

TEST(SeasonStatsTest, AccumulatesLinesAndFallsBackToEvents)
{
  PlayerSeasonTable table;
  MatchReport with_lines;
  with_lines.season = 1;
  with_lines.home_team_id = 10;
  with_lines.away_team_id = 20;
  with_lines.players = {{1, 10, true, 90, 2, 0, 1, 0, 7.5f},
                        {2, 10, false, 20, 0, 1, 0, 0, 6.5f},
                        {3, 20, true, 90, 0, 0, 0, 1, 5.0f}};
  SeasonStats::accumulate(table, with_lines);

  MatchReport events_only;
  events_only.season = 1;
  events_only.home_team_id = 10;
  events_only.away_team_id = 20;
  events_only.players = {{1, 10, true, 90, 0, 0, 0, 0, 0.0f}};
  events_only.events = {{12, 0, MatchEventKind::GOAL, true, 1, 2},
                        {80, 0, MatchEventKind::YELLOW_CARD, false, 3, 0}};
  SeasonStats::accumulate(table, events_only);

  MatchReport friendly = with_lines;
  friendly.match_type = MatchType::FRIENDLY;
  SeasonStats::accumulate(table, friendly);

  const auto& scorer = table.at({1, 1, 10, MatchType::LEAGUE});
  EXPECT_EQ(scorer.appearances, 2);
  EXPECT_EQ(scorer.starts, 2);
  EXPECT_EQ(scorer.minutes, 180);
  EXPECT_EQ(scorer.goals, 3);
  EXPECT_EQ(scorer.yellow_cards, 1);
  EXPECT_FLOAT_EQ(scorer.averageRating(), 7.5f);
  EXPECT_EQ(table.at({1, 2, 10, MatchType::LEAGUE}).assists, 2);
  EXPECT_EQ(table.at({1, 3, 20, MatchType::LEAGUE}).yellow_cards, 1);
  EXPECT_EQ(table.at({1, 3, 20, MatchType::LEAGUE}).red_cards, 1);

  const auto scorers =
      SeasonStats::topScorers(table, 1, MatchType::LEAGUE, {10, 20}, 5);
  ASSERT_EQ(scorers.size(), 1u);
  EXPECT_EQ(scorers.front().player_id, 1u);
  EXPECT_TRUE(
      SeasonStats::topScorers(table, 1, MatchType::LEAGUE, {20}, 5).empty());
  EXPECT_TRUE(
      SeasonStats::topScorers(table, 1, MatchType::CUP, {10}, 5).empty());
}

// ---------------- Suspensions ----------------

namespace
{
MatchReport cardReport(MatchType type, TeamID home, TeamID away,
                       std::vector<PlayerMatchLine> lines)
{
  MatchReport report;
  report.match_type = type;
  report.home_team_id = home;
  report.away_team_id = away;
  report.players = std::move(lines);
  return report;
}

PlayerMatchLine cards(PlayerID player, TeamID team, uint8_t yellows,
                      uint8_t reds)
{
  return {player, team, true, 90, 0, 0, yellows, reds, 0.0f};
}

Player squadPlayer(PlayerID id, TeamID team, PlayerRole role)
{
  return Player(id, team, "Test", std::format("Player {}", id), role,
                Language::EN, 1000, 0, 25, 2, 180, Foot::Right, {});
}
}  // namespace

class DisciplineTest : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    using enum PlayerRole;
    gamedata.addTeam(10, Team(10, 1, "Home", 0));
    gamedata.addTeam(20, Team(20, 1, "Away", 0));
    gamedata.addPlayer(1, squadPlayer(1, 10, CM));
    gamedata.addPlayer(2, squadPlayer(2, 10, ST));
    gamedata.addPlayer(3, squadPlayer(3, 20, CB));
  }

  void play(MatchType type, std::vector<PlayerMatchLine> lines = {})
  {
    discipline.processMatch(cardReport(type, 10, 20, std::move(lines)),
                            gamedata);
  }

  GameData gamedata;
  Discipline discipline;
};

TEST_F(DisciplineTest, FifthYellowBansForOneMatchInTheSameCompetition)
{
  using enum MatchType;
  for (int match = 0; match < 4; ++match) play(LEAGUE, {cards(1, 10, 1, 0)});
  play(CUP, {cards(1, 10, 1, 0)});
  EXPECT_FALSE(discipline.isSuspended(1, LEAGUE));
  play(LEAGUE, {cards(1, 10, 1, 0)});
  EXPECT_EQ(discipline.banMatches(1, LEAGUE), 1);
  EXPECT_FALSE(discipline.isSuspended(1, CUP));

  play(CUP);  // Cup matches do not serve league bans.
  play(FRIENDLY);
  EXPECT_EQ(discipline.banMatches(1, LEAGUE), 1);
  play(LEAGUE);
  EXPECT_FALSE(discipline.isSuspended(1, LEAGUE));
}

TEST_F(DisciplineTest, RedCardsBanAndRepeatOffencesGrow)
{
  using enum MatchType;
  play(LEAGUE, {cards(2, 10, 2, 1)});  // Second caution.
  EXPECT_EQ(discipline.banMatches(2, LEAGUE), 1);
  play(LEAGUE);
  EXPECT_EQ(discipline.banMatches(2, LEAGUE), 0);

  play(LEAGUE, {cards(3, 20, 0, 1)});  // Straight red.
  EXPECT_EQ(discipline.banMatches(3, LEAGUE), 1);
  play(LEAGUE);
  play(LEAGUE, {cards(3, 20, 0, 1)});  // Second red this season.
  EXPECT_EQ(discipline.banMatches(3, LEAGUE), 2);

  const auto suspended = discipline.suspendedPlayers(20, gamedata);
  ASSERT_EQ(suspended.size(), 1u);
  EXPECT_EQ(suspended.front().player_id, 3u);

  const auto records = discipline.records();
  const auto second_booked =
      std::ranges::find_if(records, [](const DisciplinaryRecord& record)
                           { return record.player_id == 2; });
  ASSERT_NE(second_booked, records.end());
  EXPECT_EQ(second_booked->season_yellows, 0)
      << "cautions of a second-yellow dismissal do not accumulate";

  discipline.resetSeason();
  EXPECT_EQ(discipline.banMatches(3, LEAGUE), 2) << "bans carry over";
  EXPECT_EQ(discipline.records().size(), 1u);
}

TEST_F(DisciplineTest, CardEventsAreUsedWithoutPlayerLines)
{
  MatchReport report = cardReport(MatchType::CUP, 10, 20, {});
  report.events = {{30, 0, MatchEventKind::YELLOW_CARD, true, 1, 0},
                   {70, 0, MatchEventKind::SECOND_YELLOW, true, 1, 0}};
  discipline.processMatch(report, gamedata);
  EXPECT_EQ(discipline.banMatches(1, MatchType::CUP), 1);
}

TEST_F(DisciplineTest, SuspendedStartersSitOutSimulatedMatches)
{
  auto db_conn = std::make_shared<DatabaseConnection>(":memory:");
  gamedata.addPlayer(4, squadPlayer(4, 10, PlayerRole::GK));
  gamedata.addPlayer(5, squadPlayer(5, 10, PlayerRole::ST));
  auto shared = std::shared_ptr<GameData>(&gamedata,
                                          [](GameData*)
                                          {
                                            // The fixture owns this object; the
                                            // shared pointer only borrows it.
                                          });
  Lineup& lineup = shared->getTeam(10)->get().getLineup();
  lineup.setGoalkeeper(&shared->getPlayers().at(4));
  lineup.addOutfieldPlayer(&shared->getPlayers().at(2), Vector2F{0.8f, 0.5f});
  lineup.setReserves({&shared->getPlayers().at(5)});

  CompetitionManager manager(shared, db_conn);
  MatchReport red = cardReport(MatchType::LEAGUE, 10, 20, {cards(2, 10, 0, 1)});
  manager.recordResult(
      Match(10, 20, GameDateValue(2025, 9, 6), MatchType::LEAGUE, 1, 1), red);
  ASSERT_TRUE(manager.getDiscipline().isSuspended(2, MatchType::LEAGUE));

  const Match next(20, 10, GameDateValue(2025, 9, 13), MatchType::LEAGUE, 1, 2);
  const auto swaps = manager.benchSuspendedPlayers(next);
  ASSERT_EQ(swaps.size(), 1u);
  EXPECT_EQ(lineup.getOutfieldPlayers().front().player->getId(), 5u);
  manager.restoreLineups(swaps);
  EXPECT_EQ(lineup.getOutfieldPlayers().front().player->getId(), 2u);
  EXPECT_TRUE(manager
                  .benchSuspendedPlayers(Match(
                      20, 10, GameDateValue(2025, 9, 17), MatchType::CUP, 1, 1))
                  .empty())
      << "league bans do not apply to the cup";
}

// ---------------- Persistence ----------------

class CompetitionPersistenceTest : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    Logger::init();
    db_conn = std::make_shared<DatabaseConnection>(":memory:");
    db_conn->initialize();
  }
  std::shared_ptr<DatabaseConnection> db_conn;
};

TEST_F(CompetitionPersistenceTest, KnockoutFixtureRoundTrips)
{
  FixtureRepository repository(db_conn);
  Calendar calendar;
  Match tie(5, 6, GameDateValue(2025, 10, 29), MatchType::CUP, 3, 2);
  tie.setKnockoutResult(2, 2, true, std::make_pair(uint8_t{5}, uint8_t{4}));
  calendar.addMatch(tie);
  calendar.addMatch(
      Match(7, 8, GameDateValue(2025, 11, 1), MatchType::LEAGUE, 4, 11));
  repository.saveCalendar(calendar);

  Calendar loaded;
  repository.loadCalendar(loaded);
  const Match* stored = loaded.findMatch(GameDateValue(2025, 10, 29), 5, 6);
  ASSERT_NE(stored, nullptr);
  EXPECT_EQ(stored->getCompetitionId(), 3);
  EXPECT_EQ(stored->getStage(), 2);
  EXPECT_TRUE(stored->wentToExtraTime());
  EXPECT_TRUE(stored->wentToPenalties());
  EXPECT_EQ(stored->getHomePenalties(), 5);
  EXPECT_EQ(stored->getWinnerId(), TeamID{5});
  const Match* league = loaded.findMatch(GameDateValue(2025, 11, 1), 7, 8);
  ASSERT_NE(league, nullptr);
  EXPECT_FALSE(league->isPlayed());
  EXPECT_EQ(league->getStage(), 11);
}

TEST_F(CompetitionPersistenceTest, MatchReportRoundTrips)
{
  MatchReport report;
  report.date = GameDateValue(2025, 9, 13);
  report.home_team_id = 101;
  report.away_team_id = 102;
  report.season = 1;
  report.competition_id = 1;
  report.stage = 3;
  report.home_goals = 2;
  report.away_goals = 1;
  report.home_stats.shots = 14;
  report.away_stats.possession = 41.5f;
  report.events = {{23, 0, MatchEventKind::GOAL, true, 5001, 5002},
                   {47, 2, MatchEventKind::YELLOW_CARD, false, 6001, 0}};
  report.players = {{5001, 101, true, 90, 1, 0, 0, 0, 7.8f}};
  FixtureRepository repository(db_conn);
  repository.saveMatchReports({report});

  const auto loaded = repository.loadMatchReport(report.date, 101, 102);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->stage, 3);
  EXPECT_EQ(loaded->home_goals, 2);
  EXPECT_FALSE(loaded->penalties);
  EXPECT_EQ(loaded->home_stats.shots, 14);
  EXPECT_FLOAT_EQ(loaded->away_stats.possession, 41.5f);
  ASSERT_EQ(loaded->events.size(), 2u);
  EXPECT_EQ(loaded->events[0].player, 5001u);
  EXPECT_EQ(loaded->events[0].assist, 5002u);
  EXPECT_EQ(loaded->events[1].added_minute, 2);
  EXPECT_EQ(loaded->events[1].kind, MatchEventKind::YELLOW_CARD);
  ASSERT_EQ(loaded->players.size(), 1u);
  EXPECT_FLOAT_EQ(loaded->players[0].rating, 7.8f);
  EXPECT_EQ(repository.loadTeamMatchReports(1, 102).size(), 1u);
  EXPECT_FALSE(repository.loadMatchReport(report.date, 102, 101).has_value());
}

TEST_F(CompetitionPersistenceTest, HistoryAndPlayerStatsRoundTrip)
{
  CompetitionRepository repository(db_conn);
  SeasonHistoryEntry entry;
  entry.season = 1;
  entry.start_year = 2025;
  entry.competition_id = 6;
  entry.competition_name = "Lower League";
  entry.champion_id = 601;
  entry.runner_up_id = 602;
  entry.promoted = {601, 602, 603};
  entry.top_scorer_id = 50'123;
  entry.top_scorer_goals = 21;
  SeasonHistoryEntry cup;
  cup.season = 1;
  cup.start_year = 2025;
  cup.competition_type = MatchType::CUP;
  cup.competition_id = 1;
  cup.competition_name = "Cup";
  cup.champion_id = 105;
  repository.saveSeasonHistory({entry, cup});

  const auto history = repository.loadSeasonHistory();
  ASSERT_EQ(history.size(), 2u);
  EXPECT_EQ(history[0].promoted, (std::vector<TeamID>{601, 602, 603}));
  EXPECT_TRUE(history[0].relegated.empty());
  EXPECT_EQ(history[0].top_scorer_goals, 21);
  EXPECT_EQ(history[1].competition_type, MatchType::CUP);
  EXPECT_EQ(history[1].champion_id, 105);
  EXPECT_EQ(history[1].runner_up_id, 0);

  PlayerSeasonTable table;
  PlayerSeasonStats stats;
  stats.season = 1;
  stats.player_id = 50'123;
  stats.team_id = 601;
  stats.appearances = 30;
  stats.goals = 21;
  stats.rating_total = 210.0f;
  stats.rated_matches = 30;
  table.try_emplace(PlayerSeasonKey{1, 50'123, 601, MatchType::LEAGUE}, stats);
  repository.savePlayerSeasonStats(table);
  const auto loaded = repository.loadPlayerSeasonStats(1);
  ASSERT_EQ(loaded.size(), 1u);
  EXPECT_EQ(loaded.begin()->second.goals, 21);
  EXPECT_FLOAT_EQ(loaded.begin()->second.averageRating(), 7.0f);
  EXPECT_EQ(repository.loadPlayerCareer(50'123).size(), 1u);
  EXPECT_TRUE(repository.loadPlayerSeasonStats(2).empty());

  repository.saveDiscipline({{50'123, MatchType::CUP, 4, 1, 2}});
  const auto discipline = repository.loadDiscipline();
  ASSERT_EQ(discipline.size(), 1u);
  EXPECT_EQ(discipline.front().scope, MatchType::CUP);
  EXPECT_EQ(discipline.front().season_yellows, 4);
  EXPECT_EQ(discipline.front().ban_matches, 2);
}

TEST_F(CompetitionPersistenceTest, LeagueParentAndMembershipsPersist)
{
  LeagueRepository repository(db_conn);
  repository.insertLeagueWithId(League(1, "Top", {}));
  repository.insertLeagueWithId(
      League(6, "Lower", {}, LeagueID{1}, TieBreakRule::HEAD_TO_HEAD));
  sqlite3_exec(db_conn->getRaw(),
               "INSERT INTO Teams (id, league_id, name) VALUES (601, 1, 'Up');",
               nullptr, nullptr, nullptr);
  repository.saveTeamMemberships(League(6, "Lower", {601}, LeagueID{1}));

  const auto leagues = repository.loadAllLeagues();
  const auto lower = std::ranges::find_if(
      leagues, [](const League& league) { return league.getId() == 6; });
  ASSERT_NE(lower, leagues.end());
  EXPECT_EQ(lower->getParentLeagueID(), LeagueID{1});
  EXPECT_EQ(lower->getTieBreakRule(), TieBreakRule::HEAD_TO_HEAD);
  EXPECT_EQ(lower->getTeamIDs(), std::vector<TeamID>{601});
}

TEST(CompetitionMigrationTest, LegacyFixturesTableLoads)
{
  Logger::init();
  auto db_conn = std::make_shared<DatabaseConnection>(":memory:");
  sqlite3_exec(db_conn->getRaw(),
               "CREATE TABLE Fixtures (id INTEGER PRIMARY KEY AUTOINCREMENT, "
               "game_date TEXT NOT NULL, home_team_id INTEGER NOT NULL, "
               "away_team_id INTEGER NOT NULL, match_type INTEGER NOT NULL "
               "DEFAULT 0, home_goals INTEGER, away_goals INTEGER, played "
               "INTEGER NOT NULL DEFAULT 0, UNIQUE(game_date, home_team_id, "
               "away_team_id));"
               "INSERT INTO Fixtures (game_date, home_team_id, away_team_id, "
               "match_type, home_goals, away_goals, played) VALUES "
               "('2025-09-14', 11, 12, 0, 3, 2, 1);",
               nullptr, nullptr, nullptr);
  Migrations::migrate(*db_conn);  // What opening the save does.
  const auto matches = FixtureRepository(db_conn).loadAllMatches();
  ASSERT_EQ(matches.size(), 1u);
  EXPECT_EQ(matches[0].getHomeScore(), 3);
  EXPECT_EQ(matches[0].getCompetitionId(), 0);
  EXPECT_FALSE(matches[0].wentToPenalties());
}

// ---------------- Full season through Game ----------------

TEST(SeasonRolloverTest, SeasonEndRecordsHistoryPromotesAndRegenerates)
{
  Logger::init();
  // Per-process runtime root: unit_tests and core_unit_tests run in parallel.
  const auto path = RuntimePaths::root() / "competitions_rollover.db";
  for (const auto& file :
       {path.string(), path.string() + "-wal", path.string() + "-shm"})
    std::filesystem::remove(file);

  std::vector<StandingRow> final_top;
  std::vector<StandingRow> final_second;
  size_t reports_checked = 0;
  {
    auto gamedata = std::make_shared<GameData>();
    auto db_conn = std::make_shared<DatabaseConnection>(path.string());
    Game game(gamedata, db_conn);
    ASSERT_EQ(Competitions::leagueTier(*gamedata, SECOND_LEAGUE), 2)
        << "leagues.json links the Italian divisions";

    // One real engine match through the full pipeline.
    const Match* first_league = nullptr;
    for (const auto& [date, matches] : game.getCalendar().getFullCalendar())
    {
      for (const Match& match : matches)
        if (match.getMatchType() == MatchType::LEAGUE) first_league = &match;
      if (first_league) break;
    }
    ASSERT_NE(first_league, nullptr);
    const GameDateValue engine_date = first_league->getDate();
    const TeamID engine_home = first_league->getHomeTeamId();
    const TeamID engine_away = first_league->getAwayTeamId();
    const size_t top_size =
        gamedata->getLeagues().at(TOP_LEAGUE).getTeamIDs().size();
    const size_t second_size =
        gamedata->getLeagues().at(SECOND_LEAGUE).getTeamIDs().size();

    while (game.getCurrentSeason() == 1)
    {
      const GameDateValue next =
          SeasonCalendar::addDays(game.getCurrentDate(), 1);
      {
        for (Match& match : game.getCalendar().getMatchesForDateMutable(next))
        {
          if (const bool engine_fixture =
                  next == engine_date && match.getHomeTeamId() == engine_home &&
                  match.getAwayTeamId() == engine_away;
              match.isPlayed() || engine_fixture)
            continue;
          const auto home_goals = static_cast<uint8_t>(
              (match.getHomeTeamId() * 7U + next.day) % 4U);
          const auto away_goals = static_cast<uint8_t>(
              (match.getAwayTeamId() * 3U + next.month) % 3U);
          if (match.isKnockout() && home_goals == away_goals)
            match.setKnockoutResult(home_goals, away_goals, true,
                                    std::make_pair(uint8_t{5}, uint8_t{4}));
          else
            match.setPlayedResult(home_goals, away_goals);
        }
      }
      if (next == GameDateValue(2026, 6, 30))
      {
        // Cup and continental ties drawn during the season still left every
        // club its rest (league fixtures moved within their round).
        std::map<TeamID, std::vector<const Match*>> by_club;
        for (const auto& [date, matches] : game.getCalendar().getFullCalendar())
          for (const Match& match : matches)
            if (match.getMatchType() != MatchType::FRIENDLY)
              for (const TeamID team :
                   {match.getHomeTeamId(), match.getAwayTeamId()})
                by_club[team].push_back(&match);
        size_t tired = 0;
        for (const auto& [team, fixtures] : by_club)
          for (size_t i = 1; i < fixtures.size(); ++i)
          {
            const Match& earlier = *fixtures[i - 1];
            const Match& later = *fixtures[i];
            if (const int required =
                    earlier.getMatchType() == MatchType::LEAGUE &&
                            later.getMatchType() != MatchType::LEAGUE
                        ? SeasonCalendar::MIN_REST_BEFORE_TIE
                        : SeasonCalendar::MIN_REST_DAYS;
                SeasonCalendar::addDays(earlier.getDate(), required - 1) <
                later.getDate())
              continue;
            if (++tired <= 5)
              ADD_FAILURE() << "club " << team << " plays "
                            << earlier.getDate().toString() << " and "
                            << later.getDate().toString();
          }
        EXPECT_EQ(tired, 0u);
        // Matches of every competition per day over the whole season.
        std::map<size_t, size_t> histogram;
        size_t busiest = 0;
        GameDateValue busiest_date;
        for (const auto& [date, matches] : game.getCalendar().getFullCalendar())
        {
          if (matches.empty()) continue;
          ++histogram[(matches.size() + 9) / 10 * 10];
          if (matches.size() > busiest)
          {
            busiest = matches.size();
            busiest_date = date;
          }
        }
        std::cout << "[season] busiest day " << busiest_date.toString() << ": "
                  << busiest << " matches\n";
        for (const auto& [bucket, days] : histogram)
          std::cout << "[season]   " << bucket - 9 << "-" << bucket
                    << " matches: " << days << " days\n";
        // League rounds, friendlies and cup rounds are spread over several
        // days: no Continue simulates much more than a quarter of a round.
        EXPECT_LE(busiest, 66u) << busiest_date.toString();
        final_top =
            game.getCompetitions().getStandings(game.getCalendar(), TOP_LEAGUE);
        final_second = game.getCompetitions().getStandings(game.getCalendar(),
                                                           SECOND_LEAGUE);
      }
      game.advanceDay();
      if (game.getCurrentDate() == engine_date)
      {
        const Match* match =
            game.getCalendar().findMatch(engine_date, engine_home, engine_away);
        ASSERT_NE(match, nullptr);
        ASSERT_TRUE(match->isPlayed());
        const auto report = game.getCompetitions().getMatchReport(
            engine_date, engine_home, engine_away);
        ASSERT_TRUE(report.has_value());
        EXPECT_EQ(report->home_goals, match->getHomeScore());
        EXPECT_EQ(report->away_goals, match->getAwayScore());
        EXPECT_EQ(report->competition_id,
                  gamedata->getTeam(engine_home)->get().getLeagueId());
        EXPECT_EQ(report->season, 1);
        EXPECT_GE(report->players.size(), 22u);
        ++reports_checked;
      }
    }
    EXPECT_EQ(reports_checked, 1u);
    ASSERT_EQ(final_top.size(), top_size);
    ASSERT_EQ(final_second.size(), second_size);
    EXPECT_EQ(game.getCurrentDate(), GameDateValue(2026, 7, 1));

    for (size_t i = 0; i < Competitions::PROMOTION_SLOTS; ++i)
    {
      EXPECT_EQ(gamedata->getTeam(final_second[i].team_id)->get().getLeagueId(),
                TOP_LEAGUE);
      EXPECT_EQ(gamedata->getTeam(final_top[top_size - 1 - i].team_id)
                    ->get()
                    .getLeagueId(),
                SECOND_LEAGUE);
    }

    const auto& history = game.getCompetitions().getSeasonHistory();
    const auto top_entry = std::ranges::find_if(
        history,
        [](const SeasonHistoryEntry& entry)
        {
          return entry.competition_type == MatchType::LEAGUE &&
                 entry.competition_id == TOP_LEAGUE;
        });
    ASSERT_NE(top_entry, history.end());
    EXPECT_EQ(top_entry->season, 1);
    EXPECT_EQ(top_entry->start_year, 2025);
    EXPECT_EQ(top_entry->champion_id, final_top.front().team_id);
    EXPECT_EQ(top_entry->runner_up_id, final_top[1].team_id);
    EXPECT_EQ(top_entry->relegated.size(), Competitions::PROMOTION_SLOTS);
    const auto cups = std::ranges::count_if(
        history,
        [](const SeasonHistoryEntry& entry)
        {
          return entry.competition_type == MatchType::CUP && entry.champion_id;
        });
    EXPECT_EQ(cups, Competitions::countryRoots(*gamedata).size());

    // The new season is scheduled with the promoted clubs in the top flight.
    size_t new_top_fixtures = 0;
    for (const Match& match :
         game.getCalendar().getTeamFixtures(final_second.front().team_id))
    {
      EXPECT_FALSE(match.getDate() < GameDateValue(2026, 7, 1));
      if (match.getMatchType() == MatchType::LEAGUE)
      {
        EXPECT_EQ(match.getCompetitionId(), TOP_LEAGUE);
        ++new_top_fixtures;
      }
    }
    EXPECT_EQ(new_top_fixtures, 2 * (top_size - 1));
    EXPECT_EQ(gamedata->getLeagues().at(TOP_LEAGUE).getTeamIDs().size(),
              top_size);
    game.saveGame();
  }

  auto gamedata = std::make_shared<GameData>();
  auto db_conn = std::make_shared<DatabaseConnection>(path.string());
  Game reloaded(gamedata, db_conn);
  EXPECT_EQ(reloaded.getCurrentSeason(), 2);
  EXPECT_EQ(
      gamedata->getTeam(final_second.front().team_id)->get().getLeagueId(),
      TOP_LEAGUE);
  EXPECT_EQ(Competitions::leagueTier(*gamedata, SECOND_LEAGUE), 2);
  EXPECT_FALSE(reloaded.getCompetitions().getSeasonHistory().empty());
  EXPECT_TRUE(
      std::ranges::contains(gamedata->getLeagues().at(TOP_LEAGUE).getTeamIDs(),
                            final_second.front().team_id));
}

TEST(CompetitionMigrationTest, SaveWithoutCompetitionTablesLoads)
{
  Logger::init();
  const auto path = RuntimePaths::root() / "competitions_legacy.db";
  for (const auto& file :
       {path.string(), path.string() + "-wal", path.string() + "-shm"})
    std::filesystem::remove(file);
  {
    auto gamedata = std::make_shared<GameData>();
    auto db_conn = std::make_shared<DatabaseConnection>(path.string());
    Game game(gamedata, db_conn);
    sqlite3_exec(db_conn->getRaw(),
                 "DROP TABLE MatchReports; DROP TABLE PlayerSeasonStats; "
                 "DROP TABLE SeasonHistory; DROP TABLE PlayerDiscipline;",
                 nullptr, nullptr, nullptr);
  }
  auto gamedata = std::make_shared<GameData>();
  auto db_conn = std::make_shared<DatabaseConnection>(path.string());
  Game game(gamedata, db_conn);
  EXPECT_TRUE(game.getCompetitions().getSeasonHistory().empty());
  EXPECT_FALSE(game.getCompetitions()
                   .getStandings(game.getCalendar(), TOP_LEAGUE)
                   .empty());
  EXPECT_NO_THROW(game.saveGame());
  EXPECT_FALSE(game.getCompetitions()
                   .getMatchReport(GameDateValue(2025, 8, 16), 1, 2)
                   .has_value());
}
