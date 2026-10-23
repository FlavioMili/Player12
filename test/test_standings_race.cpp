// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Run-in maths of league tables: clinched and lost races on constructed
// tables (last matchday, level points, both tie-break rules, three-way
// head-to-heads, play-off places), the "what you need" counts, and a
// brute-force cross-check against the real table code.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "database/gamedata.h"
#include "model/calendar.h"
#include "model/league.h"
#include "model/match.h"
#include "model/standings.h"
#include "model/standings_race.h"
#include "model/team.h"

namespace
{
using Standings::Clinch;
using Standings::Race;
using Standings::RaceNeed;
using Standings::RacePlaces;
using Standings::RaceState;

constexpr TeamID A = 1;
constexpr TeamID B = 2;
constexpr TeamID C = 3;
constexpr TeamID D = 4;
constexpr TeamID E = 5;
constexpr TeamID F = 6;

/** A league table built fixture by fixture. */
struct Table
{
  Standings::RaceInput input;

  explicit Table(size_t clubs,
                 TieBreakRule rule = TieBreakRule::GOAL_DIFFERENCE)
  {
    static constexpr std::array<std::string_view, 6> NAMES = {
        "Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot"};
    input.rule = rule;
    for (size_t index = 0; index < clubs; ++index)
    {
      input.teams.push_back(static_cast<TeamID>(index + 1));
      input.names.emplace_back(NAMES[index]);
    }
  }

  Table& played(TeamID home, TeamID away, int home_goals, int away_goals)
  {
    input.fixtures.push_back({home, away, true,
                              static_cast<uint8_t>(home_goals),
                              static_cast<uint8_t>(away_goals)});
    return *this;
  }

  Table& upcoming(TeamID home, TeamID away)
  {
    input.fixtures.push_back({home, away, false, 0, 0});
    return *this;
  }

  [[nodiscard]] RaceState race(TeamID team, Race which,
                               const RacePlaces& places = {}) const
  {
    return Standings::clinchReport(input, places, team)
        .races[static_cast<size_t>(std::to_underlying(which))];
  }

  [[nodiscard]] Clinch status(TeamID team, const RacePlaces& places = {}) const
  {
    return Standings::clinchStatus(input, places, team);
  }

  [[nodiscard]] std::vector<RaceNeed> needs(TeamID team,
                                            const RacePlaces& places = {}) const
  {
    return Standings::whatYouNeed(input, places, team);
  }
};

/** Title table: Alpha 10, Bravo 7 after two meetings (Alpha W, D). */
Table titleRace(TieBreakRule rule)
{
  Table league(4, rule);
  league.played(A, B, 1, 0)
      .played(B, A, 1, 1)
      .played(A, C, 2, 0)
      .played(A, D, 2, 0)
      .played(B, C, 1, 0)
      .played(B, D, 1, 0)
      .played(C, D, 0, 0)
      .played(D, C, 0, 0)
      .upcoming(C, A)
      .upcoming(B, D);
  return league;
}
}  // namespace

TEST(StandingsRaceTest, ThreePointLeadWithOneGameLeftDependsOnTheTieBreak)
{
  // Bravo can draw level by winning while Alpha loses. On goal difference
  // that tie is open (both still play, margins are free); on head-to-head
  // Alpha's win and draw against Bravo already settle it.
  const Table goals = titleRace(TieBreakRule::GOAL_DIFFERENCE);
  EXPECT_EQ(goals.race(A, Race::TITLE), RaceState::OPEN);
  EXPECT_EQ(goals.status(A), Clinch::OPEN);
  EXPECT_EQ(goals.race(B, Race::TITLE), RaceState::OPEN);
  EXPECT_EQ(goals.race(C, Race::TITLE), RaceState::LOST);

  const Table h2h = titleRace(TieBreakRule::HEAD_TO_HEAD);
  EXPECT_EQ(h2h.race(A, Race::TITLE), RaceState::SECURED);
  EXPECT_EQ(h2h.status(A), Clinch::CHAMPION);
  EXPECT_EQ(h2h.race(B, Race::TITLE), RaceState::LOST);
}

TEST(StandingsRaceTest, HeadToHeadStillToPlayKeepsTheRaceOpen)
{
  // Alpha 7, Bravo 4; Alpha won the first meeting 1-0 and the return game is
  // the last one. A Bravo win by two levels points, head-to-head points and
  // turns the head-to-head goal difference.
  Table league(4, TieBreakRule::HEAD_TO_HEAD);
  league.played(A, B, 1, 0)
      .played(A, C, 1, 0)
      .played(A, D, 0, 0)
      .played(B, C, 1, 0)
      .played(B, D, 0, 0)
      .upcoming(B, A)
      .upcoming(C, D);
  EXPECT_EQ(league.race(A, Race::TITLE), RaceState::OPEN);
  EXPECT_EQ(league.race(B, Race::TITLE), RaceState::OPEN);
}

TEST(StandingsRaceTest, ChasersPlayingEachOtherCannotBothPass)
{
  // Alpha has finished on 7; Bravo and Charlie (5 each) meet in the last
  // game. Each could pass Alpha alone, but not both: Alpha is sure of the
  // top two even though the title is still open.
  Table league(4);
  league.played(A, D, 1, 0)
      .played(A, D, 1, 0)
      .played(A, B, 0, 0)
      .played(B, D, 1, 0)
      .played(B, C, 0, 0)
      .played(C, D, 1, 0)
      .played(C, D, 1, 1)
      .upcoming(B, C);
  RacePlaces places;
  places.promotion = 2;
  EXPECT_EQ(league.race(A, Race::PROMOTION, places), RaceState::SECURED);
  EXPECT_EQ(league.race(A, Race::TITLE, places), RaceState::OPEN);
  EXPECT_EQ(league.status(A, places), Clinch::PROMOTED);
  EXPECT_EQ(league.race(B, Race::PROMOTION, places), RaceState::OPEN);
  EXPECT_EQ(league.race(D, Race::PROMOTION, places), RaceState::LOST);
}

TEST(StandingsRaceTest, FinishedSeasonFollowsEachTieBreakRule)
{
  // Level on 7 points: Alpha has the far better goal difference, Bravo won
  // the head-to-head.
  for (const TieBreakRule rule :
       {TieBreakRule::GOAL_DIFFERENCE, TieBreakRule::HEAD_TO_HEAD})
  {
    Table league(3, rule);
    league.played(A, B, 0, 1)
        .played(B, A, 0, 0)
        .played(A, C, 6, 0)
        .played(C, A, 0, 6)
        .played(B, C, 1, 0)
        .played(C, B, 1, 0);
    const bool goals = rule == TieBreakRule::GOAL_DIFFERENCE;
    EXPECT_EQ(league.status(A), goals ? Clinch::CHAMPION : Clinch::OPEN);
    EXPECT_EQ(league.race(A, Race::TITLE),
              goals ? RaceState::SECURED : RaceState::LOST);
    EXPECT_EQ(league.race(B, Race::TITLE),
              goals ? RaceState::LOST : RaceState::SECURED);
  }
}

TEST(StandingsRaceTest, CompletelyLevelClubsFallBackToTheName)
{
  // Same points, goal difference and goals, a drawn head-to-head: the name
  // decides ("Alpha" before "Bravo"), exactly as the table orders them.
  Table league(3);
  league.played(A, B, 1, 1).played(A, C, 2, 1).played(B, C, 2, 1);
  EXPECT_EQ(league.status(A), Clinch::CHAMPION);
  EXPECT_EQ(league.race(B, Race::TITLE), RaceState::LOST);
}

TEST(StandingsRaceTest, SafeAndRelegatedMirrorEachOther)
{
  // Last round: Alpha-Delta and Bravo-Charlie. Charlie (4) can only be
  // caught by Delta (1) if Delta wins and Charlie loses. Charlie won and
  // drew against Delta, so head-to-head keeps Charlie up; on goal
  // difference both clubs still play and the race stays open.
  for (const TieBreakRule rule :
       {TieBreakRule::GOAL_DIFFERENCE, TieBreakRule::HEAD_TO_HEAD})
  {
    Table league(4, rule);
    league.played(A, B, 1, 0)
        .played(B, A, 1, 1)
        .played(A, C, 1, 0)
        .played(C, A, 0, 1)
        .played(A, D, 1, 0)
        .played(B, C, 1, 0)
        .played(B, D, 1, 0)
        .played(D, B, 0, 1)
        .played(C, D, 1, 0)
        .played(D, C, 0, 0)
        .upcoming(A, D)
        .upcoming(B, C);
    RacePlaces places;
    places.relegation = 1;
    const bool h2h = rule == TieBreakRule::HEAD_TO_HEAD;
    // Alpha (13) is also champion on head-to-head against Bravo (10).
    EXPECT_EQ(league.status(A, places), h2h ? Clinch::CHAMPION : Clinch::SAFE);
    EXPECT_EQ(league.status(B, places), Clinch::SAFE);
    EXPECT_EQ(league.status(C, places), h2h ? Clinch::SAFE : Clinch::OPEN);
    EXPECT_EQ(league.status(D, places), h2h ? Clinch::RELEGATED : Clinch::OPEN);
  }
}

TEST(StandingsRaceTest, ThirdClubJoiningTheTieReordersTheMiniLeague)
{
  // Alpha (12) beat Bravo twice, so a two-club tie would always go to
  // Alpha. But if Charlie beats Alpha in the last game while Bravo wins,
  // all three finish on 12 and the three-club mini-league is level on
  // points, with Alpha's defeat (any margin) dropping it to third.
  Table league(4, TieBreakRule::HEAD_TO_HEAD);
  league.played(A, B, 1, 0)
      .played(B, A, 0, 1)
      .played(A, C, 0, 1)
      .played(B, C, 1, 0)
      .played(C, B, 0, 1)
      .played(A, D, 1, 0)
      .played(D, A, 0, 1)
      .played(B, D, 1, 0)
      .played(C, D, 1, 0)
      .played(D, C, 0, 1)
      .upcoming(C, A)
      .upcoming(B, D);
  EXPECT_EQ(league.race(A, Race::TITLE), RaceState::OPEN);
  RacePlaces places;
  places.promotion = 2;
  EXPECT_EQ(league.race(A, Race::PROMOTION, places), RaceState::OPEN);
}

TEST(StandingsRaceTest, PlayOffPlacesCanBeSecuredBeforePromotion)
{
  // Promotion for the top club, play-offs for the next two. With one game
  // left Alpha (15), Bravo (14) and Charlie (13) are out of Delta's reach
  // (4 + 3), so all three have at least a play-off place.
  Table league(6);
  for (int game = 0; game < 5; ++game) league.played(A, F, 1, 0);
  for (int game = 0; game < 4; ++game) league.played(B, F, 1, 0);
  league.played(B, E, 0, 0).played(B, E, 0, 0);
  for (int game = 0; game < 4; ++game) league.played(C, E, 1, 0);
  league.played(C, D, 0, 0).played(D, F, 1, 0);
  league.upcoming(A, D).upcoming(B, E).upcoming(C, F);
  RacePlaces places;
  places.promotion = 1;
  places.play_off = 2;
  for (const TeamID club : {A, B, C})
  {
    EXPECT_EQ(league.race(club, Race::PLAY_OFF, places), RaceState::SECURED)
        << club;
    EXPECT_EQ(league.race(club, Race::PROMOTION, places), RaceState::OPEN)
        << club;
    EXPECT_EQ(league.status(club, places), Clinch::PLAY_OFF) << club;
  }
  EXPECT_EQ(league.race(D, Race::PLAY_OFF, places), RaceState::LOST);
}

TEST(StandingsRaceTest, ContinentalPlacesAreSeparateRaces)
{
  // One game left: Alpha 12, Bravo 11, Charlie 6, Delta 2, Echo 0. Only
  // Bravo can pass Alpha, nobody can catch Bravo, and Charlie is out of
  // reach of fourth but cannot reach second. Top tier = 2 places, any
  // continental place = 3.
  Table league(5);
  for (int game = 0; game < 4; ++game) league.played(A, E, 1, 0);
  for (int game = 0; game < 3; ++game) league.played(B, E, 1, 0);
  league.played(B, D, 0, 0).played(B, D, 0, 0);
  league.played(C, E, 1, 0).played(C, E, 1, 0);
  league.upcoming(A, D).upcoming(B, C);
  RacePlaces places;
  places.continental_top = 2;
  places.continental = 3;
  places.relegation = 1;
  EXPECT_EQ(league.race(A, Race::TITLE, places), RaceState::OPEN);
  EXPECT_EQ(league.status(A, places), Clinch::CONTINENTAL_TOP);
  EXPECT_EQ(league.status(B, places), Clinch::CONTINENTAL_TOP);
  EXPECT_EQ(league.status(C, places), Clinch::CONTINENTAL);
  EXPECT_EQ(league.race(C, Race::CONTINENTAL_TOP, places), RaceState::LOST);
  EXPECT_EQ(league.race(E, Race::CONTINENTAL, places), RaceState::LOST);
  EXPECT_EQ(league.status(E, places), Clinch::RELEGATED);
  EXPECT_EQ(Standings::raceCutoff(Race::CONTINENTAL, places, 5), 3);
  places.continental = 2;
  EXPECT_EQ(Standings::raceCutoff(Race::CONTINENTAL, places, 5), 0)
      << "no separate race when every continental place is top tier";
}

TEST(StandingsRaceTest, WhatYouNeedCountsThePointsFromOwnGames)
{
  // Alpha 9 (beat Bravo), Bravo 6; two games each against clubs on zero.
  for (const TieBreakRule rule :
       {TieBreakRule::GOAL_DIFFERENCE, TieBreakRule::HEAD_TO_HEAD})
  {
    Table league(6, rule);
    league.played(A, B, 1, 0).played(A, C, 1, 0).played(A, C, 1, 0);
    league.played(B, E, 1, 0).played(B, E, 1, 0);
    league.upcoming(A, C).upcoming(D, A).upcoming(B, E).upcoming(F, B);
    const auto needs = league.needs(A);
    ASSERT_EQ(needs.size(), 1u);
    EXPECT_EQ(needs[0].race, Race::TITLE);
    EXPECT_EQ(needs[0].kind, RaceNeed::Kind::IN_HANDS);
    EXPECT_EQ(needs[0].games_left, 2);
    EXPECT_EQ(needs[0].next_opponent, C);
    EXPECT_TRUE(needs[0].next_at_home);
    // Head-to-head: a win and a defeat (12, level with a perfect Bravo)
    // is enough. Goal difference: the defeat leaves the tie open, so it
    // takes a win and a draw.
    EXPECT_EQ(needs[0].points, rule == TieBreakRule::HEAD_TO_HEAD ? 3 : 4);
  }
}

TEST(StandingsRaceTest, WhatYouNeedOnTheLastMatchday)
{
  // Alpha 9 (beat Bravo), Bravo 7, one game each, away at Charlie and at
  // home to Delta.
  for (const TieBreakRule rule :
       {TieBreakRule::GOAL_DIFFERENCE, TieBreakRule::HEAD_TO_HEAD})
  {
    Table league(4, rule);
    league.played(A, B, 1, 0).played(A, D, 1, 0).played(A, D, 1, 0);
    league.played(B, D, 1, 0).played(B, C, 1, 0).played(B, C, 0, 0);
    league.upcoming(C, A).upcoming(B, D);
    const auto needs = league.needs(A);
    ASSERT_EQ(needs.size(), 1u);
    EXPECT_EQ(needs[0].games_left, 1);
    EXPECT_EQ(needs[0].next_opponent, C);
    EXPECT_FALSE(needs[0].next_at_home);
    // A draw takes Alpha to 10, which a Bravo win matches.
    EXPECT_EQ(needs[0].points, rule == TieBreakRule::HEAD_TO_HEAD ? 1 : 3);
  }
}

TEST(StandingsRaceTest, OutOfYourHandsWhenOnlyOtherResultsCanHelp)
{
  // Alpha 6, Bravo 9 with one game each: Alpha can only draw level, and
  // only if Bravo loses.
  Table league(4);
  league.played(A, C, 1, 0).played(A, D, 1, 0);
  league.played(B, C, 1, 0).played(B, D, 1, 0).played(B, C, 2, 0);
  league.upcoming(A, C).upcoming(B, D);
  EXPECT_EQ(league.race(A, Race::TITLE), RaceState::OPEN);
  const auto needs = league.needs(A);
  ASSERT_EQ(needs.size(), 1u);
  EXPECT_EQ(needs[0].kind, RaceNeed::Kind::NEEDS_HELP);
  // Decided races are left out.
  EXPECT_TRUE(titleRace(TieBreakRule::HEAD_TO_HEAD).needs(A).empty());
}

namespace
{
/** Every position a club reaches over all scores 0-2 of the open games. */
struct PositionSpan
{
  int best = 99;
  int worst = 0;
};

constexpr LeagueID LEAGUE_ID = 1;

std::vector<StandingRow> realTable(GameData& gamedata,
                                   const Standings::RaceInput& input,
                                   const std::vector<uint8_t>& goals)
{
  Calendar calendar;
  GameDateValue day(2025, 8, 2);
  size_t open = 0;
  for (const Standings::RaceFixture& fixture : input.fixtures)
  {
    Match match(fixture.home_id, fixture.away_id, day, MatchType::LEAGUE,
                LEAGUE_ID);
    if (fixture.played)
      match.setPlayedResult(fixture.home_goals, fixture.away_goals);
    else
    {
      match.setPlayedResult(goals[open * 2], goals[open * 2 + 1]);
      ++open;
    }
    calendar.addMatch(match);
    day = SeasonCalendar::addDays(day, 1);
  }
  return Standings::compute(gamedata.getLeagues().at(LEAGUE_ID), calendar,
                            gamedata);
}
}  // namespace

TEST(StandingsRaceTest, NeverContradictsTheRealTableOverAllSmallScores)
{
  // Random small leagues, a few games still open: whatever the solver calls
  // decided must hold for every score 0-2 of the open games in the real
  // table code, and a finished season must match the table exactly.
  std::mt19937 rng(20251002);
  int decided = 0;
  int finished_checks = 0;
  for (int round = 0; round < 140; ++round)
  {
    const auto clubs = 4 + static_cast<size_t>(round % 3);
    const TieBreakRule rule = round % 2 == 0 ? TieBreakRule::GOAL_DIFFERENCE
                                             : TieBreakRule::HEAD_TO_HEAD;
    Table league(clubs, rule);
    std::vector<std::pair<TeamID, TeamID>> pairs;
    for (TeamID home = 1; home <= clubs; ++home)
      for (TeamID away = 1; away <= clubs; ++away)
        if (home != away) pairs.emplace_back(home, away);
    std::ranges::shuffle(pairs, rng);
    const size_t open = static_cast<size_t>(round % 4);
    std::uniform_int_distribution score(0, 2);
    for (size_t index = 0; index < pairs.size(); ++index)
    {
      if (index < open)
        league.upcoming(pairs[index].first, pairs[index].second);
      else
        league.played(pairs[index].first, pairs[index].second, score(rng),
                      score(rng));
    }

    GameData gamedata;
    for (size_t index = 0; index < clubs; ++index)
      gamedata.addTeam(league.input.teams[index],
                       Team(league.input.teams[index], LEAGUE_ID,
                            league.input.names[index], 0));
    gamedata.addLeague(
        LEAGUE_ID,
        ::League(LEAGUE_ID, "Test", league.input.teams, std::nullopt, rule));

    std::vector<PositionSpan> spans(clubs + 1);
    std::vector<uint8_t> goals(open * 2, 0);
    for (;;)
    {
      const auto rows = realTable(gamedata, league.input, goals);
      for (const StandingRow& row : rows)
      {
        spans[row.team_id].best =
            std::min<int>(spans[row.team_id].best, row.position);
        spans[row.team_id].worst =
            std::max<int>(spans[row.team_id].worst, row.position);
      }
      size_t digit = 0;
      while (digit < goals.size() && goals[digit] == 2) goals[digit++] = 0;
      if (digit == goals.size()) break;
      ++goals[digit];
    }

    RacePlaces places;
    places.continental_top = 2;
    places.relegation = 1;
    const auto reports = Standings::clinchReports(league.input, places);
    std::string fixtures;
    for (const Standings::RaceFixture& fixture : league.input.fixtures)
    {
      const auto result =
          fixture.played
              ? std::format(" {}:{}", fixture.home_goals, fixture.away_goals)
              : std::string(" open");
      fixtures +=
          std::format("{}-{}{}; ", fixture.home_id, fixture.away_id, result);
    }
    for (const Standings::ClinchReport& report : reports)
    {
      const PositionSpan& span = spans[report.team_id];
      for (const Race race :
           {Race::TITLE, Race::CONTINENTAL_TOP, Race::SURVIVAL})
      {
        const int cutoff = Standings::raceCutoff(race, places, clubs);
        const RaceState state =
            report.races[static_cast<size_t>(std::to_underlying(race))];
        if (state == RaceState::SECURED)
        {
          ++decided;
          EXPECT_LE(span.worst, cutoff) << "round " << round << " club "
                                        << report.team_id << ": " << fixtures;
        }
        if (state == RaceState::LOST)
        {
          ++decided;
          EXPECT_GT(span.best, cutoff) << "round " << round << " club "
                                       << report.team_id << ": " << fixtures;
        }
        if (open == 0)
        {
          ++finished_checks;
          EXPECT_EQ(state,
                    span.worst <= cutoff ? RaceState::SECURED : RaceState::LOST)
              << "finished season, round " << round << " club "
              << report.team_id;
        }
      }
    }
  }
  EXPECT_GT(decided, 200) << "the cross-check must exercise decided races";
  EXPECT_GT(finished_checks, 100);
}

TEST(StandingsRaceTest, FullSizeLeagueRunInStaysFast)
{
  // 20 clubs, double round robin with seeded results, checked from 30
  // rounds to go (nothing decided, the search must stay cheap) to the last.
  constexpr size_t CLUBS = 20;
  std::vector<std::pair<TeamID, TeamID>> rounds;
  std::vector<TeamID> circle;
  for (TeamID id = 1; id <= CLUBS; ++id) circle.push_back(id);
  for (int leg = 0; leg < 2; ++leg)
  {
    std::vector<TeamID> order = circle;
    for (size_t round = 0; round + 1 < CLUBS; ++round)
    {
      for (size_t pair = 0; pair < CLUBS / 2; ++pair)
      {
        const TeamID home = order[pair];
        const TeamID away = order[CLUBS - 1 - pair];
        rounds.emplace_back(leg == 0 ? home : away, leg == 0 ? away : home);
      }
      std::rotate(order.begin() + 1, order.end() - 1, order.end());
    }
  }
  for (const auto& [left, rule] :
       {std::pair{30u, TieBreakRule::GOAL_DIFFERENCE},
        std::pair{15u, TieBreakRule::GOAL_DIFFERENCE},
        std::pair{6u, TieBreakRule::GOAL_DIFFERENCE},
        std::pair{2u, TieBreakRule::GOAL_DIFFERENCE},
        std::pair{1u, TieBreakRule::GOAL_DIFFERENCE},
        std::pair{30u, TieBreakRule::HEAD_TO_HEAD},
        std::pair{15u, TieBreakRule::HEAD_TO_HEAD},
        std::pair{6u, TieBreakRule::HEAD_TO_HEAD},
        std::pair{2u, TieBreakRule::HEAD_TO_HEAD},
        std::pair{1u, TieBreakRule::HEAD_TO_HEAD}})
  {
    std::mt19937 rng(77);
    std::poisson_distribution<int> goals(1.3);
    Standings::RaceInput input;
    input.rule = rule;
    for (TeamID id = 1; id <= CLUBS; ++id)
    {
      input.teams.push_back(id);
      input.names.push_back(std::format("Club {}", id));
    }
    const size_t open_from = rounds.size() - left * (CLUBS / 2);
    for (size_t index = 0; index < rounds.size(); ++index)
    {
      // Stronger clubs (low ids) score a little more.
      const auto [home, away] = rounds[index];
      const int home_goals = std::min(9, goals(rng) + (home < 6 ? 1 : 0));
      const int away_goals = std::min(9, goals(rng) + (away < 6 ? 1 : 0));
      input.fixtures.push_back({home, away, index < open_from,
                                static_cast<uint8_t>(home_goals),
                                static_cast<uint8_t>(away_goals)});
    }
    RacePlaces places;
    places.continental_top = 4;
    places.continental = 6;
    places.relegation = 3;
    const auto start = std::chrono::steady_clock::now();
    const auto reports = Standings::clinchReports(input, places);
    const auto needs = Standings::whatYouNeed(input, places, 10);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    ASSERT_EQ(reports.size(), CLUBS);
    int decided = 0;
    for (const auto& report : reports)
      for (const RaceState state : report.races)
        if (state == RaceState::SECURED || state == RaceState::LOST) ++decided;
    std::cout << left << " rounds left ("
              << (rule == TieBreakRule::HEAD_TO_HEAD ? "head-to-head"
                                                     : "goal difference")
              << "): " << decided << " decided races, " << needs.size()
              << " needs for club 10, " << elapsed.count() << " ms\n";
    EXPECT_LT(elapsed.count(), 4000) << left << " rounds left";
    if (left <= 2) EXPECT_GT(decided, 0) << left << " rounds left";
  }
}
