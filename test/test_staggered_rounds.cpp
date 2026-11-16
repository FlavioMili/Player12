// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// League rounds spread over Friday-Monday (and Tuesday-Wednesday midweek)
// on the stock 22-league data: every club plays once per round with enough
// rest, no day carries a large share of a round, kick-offs persist.

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "database/database_connection.h"
#include "database/gamedata.h"
#include "database/migrations/migrations.h"
#include "database/repositories/fixture_repository.h"
#include "global/logger.h"
#include "global/paths.h"
#include "model/calendar.h"
#include "model/competition.h"
#include "model/league.h"
#include "model/team.h"

namespace
{
constexpr size_t CLUBS_PER_LEAGUE = 20;
constexpr size_t ROUND_MATCHES = CLUBS_PER_LEAGUE / 2;

/** The stock leagues (ids, pyramid) with 20 placeholder clubs each. */
void buildStockWorld(GameData& gamedata)
{
  std::ifstream file(AssetPaths::leagues());
  const nlohmann::json data = nlohmann::json::parse(file);
  for (const auto& item : data)
  {
    const auto league_id = item.at("id").get<LeagueID>();
    std::optional<LeagueID> parent;
    if (item.contains("parent_league"))
      parent = item.at("parent_league").get<LeagueID>();
    std::vector<TeamID> team_ids;
    for (size_t i = 1; i <= CLUBS_PER_LEAGUE; ++i)
    {
      const auto team_id = static_cast<TeamID>(league_id * 100 + i);
      gamedata.addTeam(team_id, Team(team_id, league_id,
                                     std::format("Club {}", team_id), 0));
      team_ids.push_back(team_id);
    }
    gamedata.addLeague(league_id,
                       League(league_id, item.at("name").get<std::string>(),
                              team_ids, parent));
  }
}

int ordinal(const GameDateValue& date)
{
  return static_cast<int>(date.year) * 400 + date.month * 32 + date.day;
}

int daysBetween(const GameDateValue& earlier, const GameDateValue& later)
{
  int days = 0;
  for (GameDateValue day = earlier; day < later;
       day = SeasonCalendar::addDays(day, 1))
    ++days;
  return days;
}

/** Saturday (weekend rounds) or Wednesday (midweek rounds) of a day. */
GameDateValue anchorOf(const GameDateValue& date)
{
  switch (SeasonCalendar::dayOfWeek(date))
  {
    case SeasonCalendar::FRIDAY:
      return SeasonCalendar::addDays(date, 1);
    case SeasonCalendar::SUNDAY:
      return SeasonCalendar::addDays(date, -1);
    case SeasonCalendar::MONDAY:
      return SeasonCalendar::addDays(date, -2);
    case SeasonCalendar::TUESDAY:
      return SeasonCalendar::addDays(date, 1);
    case SeasonCalendar::THURSDAY:
      return SeasonCalendar::addDays(date, -1);
    default:
      return date;
  }
}

struct Fixture
{
  GameDateValue date;
  MatchType type;
};

std::map<TeamID, std::vector<Fixture>> clubFixtures(const Calendar& calendar)
{
  std::map<TeamID, std::vector<Fixture>> fixtures;
  for (const auto& [date, matches] : calendar.getFullCalendar())
    for (const Match& match : matches)
    {
      fixtures[match.getHomeTeamId()].push_back({date, match.getMatchType()});
      fixtures[match.getAwayTeamId()].push_back({date, match.getMatchType()});
    }
  return fixtures;
}

/** Rest rules for every match, friendlies included: two days between
 * matches, three before a cup or continental tie that follows a league
 * match. */
void expectRested(const Calendar& calendar)
{
  for (const auto& [team_id, fixtures] : clubFixtures(calendar))
  {
    for (size_t i = 1; i < fixtures.size(); ++i)
    {
      const Fixture& earlier = fixtures[i - 1];
      const Fixture& later = fixtures[i];
      const int required = SeasonCalendar::restDays(earlier.type, later.type);
      EXPECT_GE(daysBetween(earlier.date, later.date), required)
          << "club " << team_id << " plays " << earlier.date.toString()
          << " and " << later.date.toString();
    }
  }
}

/** No day carries much of a round (all 22 leagues together). */
void expectSpreadDays(const Calendar& calendar, const GameData& gamedata,
                      uint16_t season_year)
{
  // League matches of all 22 leagues per day and per round window (a
  // round's window is anchored on its last day: a weekend match brought
  // forward to Thursday belongs to the weekend).
  std::map<std::pair<LeagueID, uint8_t>, GameDateValue> round_end;
  for (const auto& [date, matches] : calendar.getFullCalendar())
    for (const Match& match : matches)
      if (match.getMatchType() == MatchType::LEAGUE)
        round_end[{match.getCompetitionId(), match.getStage()}] = date;
  std::map<int, size_t> per_day;
  std::map<int, size_t> per_window;
  std::map<int, std::set<int>> window_days;
  std::map<int, GameDateValue> dates;
  std::map<int, GameDateValue> anchor_of_day;
  for (const auto& [date, matches] : calendar.getFullCalendar())
    for (const Match& match : matches)
    {
      if (match.getMatchType() != MatchType::LEAGUE) continue;
      const GameDateValue anchor =
          anchorOf(round_end[{match.getCompetitionId(), match.getStage()}]);
      ++per_day[ordinal(date)];
      ++per_window[ordinal(anchor)];
      window_days[ordinal(anchor)].insert(ordinal(date));
      dates[ordinal(date)] = date;
      anchor_of_day[ordinal(date)] = anchor;
    }
  const size_t world_round = gamedata.getLeagues().size() * ROUND_MATCHES;
  size_t busiest = 0;
  size_t total = 0;
  for (const auto& [day, count] : per_day)
  {
    busiest = std::max(busiest, count);
    total += count;
    const GameDateValue anchor = anchor_of_day[day];
    // Some weekends lose a day for everybody (the Monday before an
    // international window or a cup midweek most clubs play in, the Friday
    // at the end of the winter break, the Monday before a midweek round of
    // both tiers): the round then has three days.
    const bool short_window = window_days[ordinal(anchor)].size() <= 3;
    const size_t limit = world_round * (short_window ? 34 : 30) / 100;
    EXPECT_LE(count, limit)
        << dates[day].toString() << " carries " << count << " of "
        << per_window[ordinal(anchor)] << " league matches";
  }
  std::cout << "[staggered] " << season_year << ": league days "
            << per_day.size() << ", busiest " << busiest << ", mean "
            << total / per_day.size() << " matches per day (" << world_round
            << " per round)\n";
  std::map<size_t, size_t> histogram;  // Upper bound of 10 -> days.
  for (const auto& [day, count] : per_day) ++histogram[(count + 9) / 10 * 10];
  for (const auto& [bucket, count] : histogram)
    std::cout << "[staggered]   " << bucket - 9 << "-" << bucket
              << " league matches: " << count << " days\n";
  size_t short_weekends = 0;
  for (const auto& [anchor, count] : per_window)
  {
    EXPECT_GE(window_days[anchor].size(), 2U);
    const bool midweek =
        SeasonCalendar::dayOfWeek(dates[*window_days[anchor].rbegin()]) <=
            SeasonCalendar::THURSDAY &&
        SeasonCalendar::dayOfWeek(dates[*window_days[anchor].rbegin()]) !=
            SeasonCalendar::MONDAY;
    if (window_days[anchor].size() <= 3 && !midweek) ++short_weekends;
  }
  std::cout << "[staggered]   weekends with three days: " << short_weekends
            << "\n";
  EXPECT_LE(short_weekends, 8U) << "season " << season_year;

  // Every competition scheduled at the start of the season: friendlies, the
  // cup's first round and the league.
  size_t busiest_any = 0;
  GameDateValue busiest_date;
  for (const auto& [date, matches] : calendar.getFullCalendar())
    if (matches.size() > busiest_any)
    {
      busiest_any = matches.size();
      busiest_date = date;
    }
  std::cout << "[staggered] busiest day of any competition: "
            << busiest_date.toString() << " with " << busiest_any
            << " matches\n";
}

class StaggeredRoundsTest : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    Logger::init();
    buildStockWorld(gamedata);
    calendar.generate(gamedata, GameDateValue(2025, 7, 2));
  }

  GameData gamedata;
  Calendar calendar;
};
}  // namespace

TEST_F(StaggeredRoundsTest, EveryClubPlaysOncePerRoundWithRest)
{
  ASSERT_EQ(gamedata.getLeagues().size(), 22U);
  // (league, round) -> clubs and days used.
  std::map<std::pair<LeagueID, uint8_t>, std::multiset<TeamID>> clubs;
  std::map<std::pair<LeagueID, uint8_t>, std::set<int>> days;
  for (const auto& [date, matches] : calendar.getFullCalendar())
    for (const Match& match : matches)
    {
      if (match.getMatchType() != MatchType::LEAGUE) continue;
      const std::pair key{match.getCompetitionId(), match.getStage()};
      clubs[key].insert(match.getHomeTeamId());
      clubs[key].insert(match.getAwayTeamId());
      days[key].insert(ordinal(date));
      EXPECT_FALSE(SeasonCalendar::isBlackout(date)) << date.toString();
      // Top flights keep continental weeks free (lower divisions may bring
      // a weekend match forward to that Thursday).
      if (Competitions::leagueTier(gamedata, match.getCompetitionId()) == 1)
        EXPECT_FALSE(SeasonCalendar::isContinentalWeek(date))
            << date.toString();
    }
  EXPECT_EQ(clubs.size(), 22U * 38U);
  for (const auto& [key, round_clubs] : clubs)
  {
    EXPECT_EQ(round_clubs.size(), CLUBS_PER_LEAGUE)
        << "league " << int(key.first) << " round " << int(key.second);
    EXPECT_EQ(std::set<TeamID>(round_clubs.begin(), round_clubs.end()).size(),
              CLUBS_PER_LEAGUE)
        << "a club plays twice in league " << int(key.first) << " round "
        << int(key.second);
  }
  // Rounds are spread over several days (a league whose pattern has only
  // Saturday and Sunday still uses both).
  size_t spread_rounds = 0;
  std::string single_days;
  for (const auto& [key, used] : days)
  {
    if (used.size() >= 2)
      ++spread_rounds;
    else
      single_days += std::format(" {}/{}", key.first, key.second);
  }
  EXPECT_GE(spread_rounds, days.size() * 95 / 100)
      << "league/round on one day:" << single_days;
  expectRested(calendar);
}

TEST_F(StaggeredRoundsTest, NoDayCarriesMuchOfARound)
{
  expectSpreadDays(calendar, gamedata, 2025);
}

TEST_F(StaggeredRoundsTest, KickoffsComeFromTheDayPatterns)
{
  std::set<uint16_t> italian_sunday;
  for (const auto& [date, matches] : calendar.getFullCalendar())
    for (const Match& match : matches)
    {
      if (match.getMatchType() != MatchType::LEAGUE) continue;
      EXPECT_GT(match.getScheduledKickoff(), 9 * 60) << date.toString();
      EXPECT_LT(match.getScheduledKickoff(), 23 * 60) << date.toString();
      EXPECT_EQ(match.getKickoff(), match.getScheduledKickoff());
      if (match.getCompetitionId() == 1 &&
          SeasonCalendar::dayOfWeek(date) == SeasonCalendar::SUNDAY)
        italian_sunday.insert(match.getScheduledKickoff());
    }
  // leagues.json: Italian Sundays go from lunchtime to the evening match.
  EXPECT_TRUE(italian_sunday.contains(12 * 60 + 30));
  EXPECT_TRUE(italian_sunday.contains(20 * 60 + 45));

  // Fixtures without a scheduled time use the competition's usual one.
  const Match cup(1, 2, GameDateValue(2025, 10, 29), MatchType::CUP, 1, 2);
  EXPECT_EQ(cup.getKickoff(), 20 * 60 + 45);
  const Match old_league(1, 2, GameDateValue(2025, 9, 27), MatchType::LEAGUE,
                         1);
  EXPECT_EQ(old_league.getKickoff(), 15 * 60);
}

TEST_F(StaggeredRoundsTest, KickoffsSurviveSaveAndLoad)
{
  auto db_conn = std::make_shared<DatabaseConnection>(":memory:");
  db_conn->initialize();
  FixtureRepository repository(db_conn);
  repository.saveCalendar(calendar);
  Calendar loaded;
  repository.loadCalendar(loaded);
  size_t compared = 0;
  for (const auto& [date, matches] : calendar.getFullCalendar())
    for (const Match& match : matches)
    {
      const Match* stored =
          loaded.findMatch(date, match.getHomeTeamId(), match.getAwayTeamId());
      ASSERT_NE(stored, nullptr);
      EXPECT_EQ(stored->getScheduledKickoff(), match.getScheduledKickoff());
      ++compared;
    }
  EXPECT_GT(compared, 22U * 380U);
  // Loaded fixtures keep their days.
  EXPECT_EQ(loaded.protectRest(GameDateValue(2025, 7, 2)), 0U);
}

TEST(StaggeredRoundsMigrationTest, OlderFixturesGetTheUsualKickoff)
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
               "match_type, played) VALUES ('2025-09-27', 11, 12, 0, 0), "
               "('2025-10-01', 13, 14, 3, 0);",
               nullptr, nullptr, nullptr);
  Migrations::migrate(*db_conn);
  Migrations::migrate(*db_conn);  // Idempotent.
  Calendar calendar;
  FixtureRepository(db_conn).loadCalendar(calendar);
  const Match* league = calendar.findMatch(GameDateValue(2025, 9, 27), 11, 12);
  const Match* continental =
      calendar.findMatch(GameDateValue(2025, 10, 1), 13, 14);
  ASSERT_NE(league, nullptr);
  ASSERT_NE(continental, nullptr);
  EXPECT_EQ(league->getScheduledKickoff(), 0);
  EXPECT_EQ(league->getKickoff(), 15 * 60);
  EXPECT_EQ(continental->getKickoff(), 21 * 60);
}

TEST_F(StaggeredRoundsTest, CupRoundsSpreadOverMidweek)
{
  const auto cup_dates = SeasonCalendar::cupRoundDates(2025, 6);
  std::map<int, size_t> per_day;
  size_t ties = 0;
  for (const auto& [date, matches] : calendar.getFullCalendar())
    for (const Match& match : matches)
    {
      if (match.getMatchType() != MatchType::CUP) continue;
      // Tuesday to Thursday around the round's Wednesday.
      EXPECT_FALSE(date < SeasonCalendar::addDays(cup_dates.front(), -1))
          << date.toString();
      EXPECT_FALSE(SeasonCalendar::addDays(cup_dates.front(), 1) < date)
          << date.toString();
      ++per_day[ordinal(date)];
      ++ties;
    }
  EXPECT_EQ(ties, 11U * 8U);  // Preliminary round of every country.
  EXPECT_GE(per_day.size(), 2U);
  for (const auto& [day, count] : per_day) EXPECT_LE(count, ties / 2);
  expectRested(calendar);
}

TEST_F(StaggeredRoundsTest, FriendliesSpreadOverTheWeek)
{
  std::map<TeamID, std::set<int>> weeks;  // Monday of each friendly's week.
  std::map<int, size_t> per_day;
  for (const auto& [date, matches] : calendar.getFullCalendar())
    for (const Match& match : matches)
    {
      if (match.getMatchType() != MatchType::FRIENDLY) continue;
      EXPECT_NE(SeasonCalendar::dayOfWeek(date), SeasonCalendar::MONDAY);
      const int week = ordinal(
          SeasonCalendar::addDays(date, -SeasonCalendar::dayOfWeek(date)));
      for (const TeamID team : {match.getHomeTeamId(), match.getAwayTeamId()})
        EXPECT_TRUE(weeks[team].insert(week).second)
            << "club " << team << " plays twice in the week of "
            << date.toString();
      ++per_day[ordinal(date)];
    }
  EXPECT_EQ(weeks.size(), gamedata.getTeams().size());
  EXPECT_GE(per_day.size(), 4U * 6U);
  for (const auto& [day, count] : per_day) EXPECT_LE(count, 45U);
}

TEST_F(StaggeredRoundsTest, NoDayIsCrowdedAcrossCompetitions)
{
  // Friendlies, the cup's first round and the league together.
  std::map<size_t, size_t> histogram;
  size_t busiest = 0;
  for (const auto& [date, matches] : calendar.getFullCalendar())
  {
    busiest = std::max(busiest, matches.size());
    ++histogram[(matches.size() + 9) / 10 * 10];
    EXPECT_LE(matches.size(), 62U) << date.toString();
  }
  std::cout << "[staggered] all competitions: busiest day " << busiest
            << " matches\n";
  for (const auto& [bucket, count] : histogram)
    std::cout << "[staggered]   " << bucket - 9 << "-" << bucket
              << " matches: " << count << " days\n";
}

TEST_F(StaggeredRoundsTest, LeagueFixturesMakeRoomForNewTies)
{
  // Top-flight clubs drawn into a continental Tuesday and lower-division
  // clubs into a Wednesday cup tie: their league matches just before move.
  const GameDateValue tuesday = SeasonCalendar::continentalWeeks(2025).at(2);
  const GameDateValue sunday = SeasonCalendar::addDays(tuesday, -2);
  const auto cup_dates = SeasonCalendar::cupRoundDates(2025, 6);
  const GameDateValue wednesday = cup_dates.at(2);
  const GameDateValue monday = SeasonCalendar::addDays(wednesday, -2);
  const auto pickPlaying = [&](const GameDateValue& date, bool top_flight)
  {
    std::optional<std::pair<TeamID, TeamID>> pair;
    for (const Match& match : calendar.getMatchesForDate(date))
      if (match.getMatchType() == MatchType::LEAGUE &&
          (Competitions::leagueTier(gamedata, match.getCompetitionId()) == 1) ==
              top_flight)
        pair = {match.getHomeTeamId(), match.getAwayTeamId()};
    return pair;
  };
  const auto top = pickPlaying(sunday, true);
  const auto lower = pickPlaying(monday, false);
  ASSERT_TRUE(top) << "a top-flight match on " << sunday.toString();
  ASSERT_TRUE(lower) << "a lower-division match on " << monday.toString();

  // Opponents from another country, so no league fixture is shared.
  const TeamID continental_opponent = 1201;
  const TeamID cup_opponent = lower->first == 601 ? 602 : 601;
  calendar.addMatch(Match(top->first, continental_opponent, tuesday,
                          MatchType::CONTINENTAL, 1, 1));
  calendar.addMatch(
      Match(lower->first, cup_opponent, wednesday, MatchType::CUP, 1, 3));
  EXPECT_GE(calendar.protectRest(GameDateValue(2025, 8, 1)), 1U);
  EXPECT_EQ(calendar.protectRest(GameDateValue(2025, 8, 1)), 0U)
      << "nothing new to check";

  for (const auto& [home, away] : {*top, *lower})
  {
    const auto fixtures = calendar.getTeamFixtures(home);
    EXPECT_EQ(std::ranges::count_if(fixtures,
                                    [&](const Match& match)
                                    {
                                      return match.getAwayTeamId() == away ||
                                             match.getHomeTeamId() == away;
                                    }),
              2)
        << "the league meeting still exists (home and away)";
  }
  EXPECT_EQ(calendar.findMatch(sunday, top->first, top->second), nullptr);
  expectRested(calendar);
}

TEST(StaggeredRoundsSeasonTest, NextSeasonsAndDeterminism)
{
  Logger::init();
  GameData gamedata;
  buildStockWorld(gamedata);
  for (const uint16_t year : {2025, 2026, 2027, 2028})
  {
    Calendar first;
    first.generate(gamedata, GameDateValue(year, 7, 1));
    Calendar second;
    second.generate(gamedata, GameDateValue(year, 7, 1));
    ASSERT_EQ(first.getFullCalendar().size(), second.getFullCalendar().size());
    size_t league_matches = 0;
    for (const auto& [date, matches] : first.getFullCalendar())
    {
      const auto& again = second.getMatchesForDate(date);
      ASSERT_EQ(again.size(), matches.size()) << date.toString();
      for (size_t i = 0; i < matches.size(); ++i)
      {
        EXPECT_EQ(again[i].getHomeTeamId(), matches[i].getHomeTeamId());
        EXPECT_EQ(again[i].getAwayTeamId(), matches[i].getAwayTeamId());
        EXPECT_EQ(again[i].getScheduledKickoff(),
                  matches[i].getScheduledKickoff());
        if (matches[i].getMatchType() == MatchType::LEAGUE) ++league_matches;
      }
    }
    EXPECT_EQ(league_matches, 22U * 380U) << "season " << year;
    expectRested(first);
    expectSpreadDays(first, gamedata, year);
  }
}
