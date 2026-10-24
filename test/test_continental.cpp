// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <sqlite3.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <numeric>
#include <set>
#include <vector>

#include "controller/game_controller.h"
#include "database/database_connection.h"
#include "database/gamedata.h"
#include "database/migrations/migrations.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/calendar.h"
#include "model/competition.h"
#include "model/continental.h"
#include "model/match.h"
#include "model/match_engine.h"
#include "model/match_report.h"
#include "model/match_scheduler.h"
#include "model/team.h"

namespace
{
using Continental::Round;

constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 300'000 + (getpid() % 100'000) * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

/** Associations of a league phase: sizes per association, pots by index. */
std::vector<Continental::DrawTeam> drawTeams(const std::vector<int>& sizes,
                                             uint8_t pots)
{
  std::vector<Continental::DrawTeam> teams;
  const int total = std::accumulate(sizes.begin(), sizes.end(), 0);
  TeamID id = 1;
  // Interleave associations so every pot mixes them (as coefficients do).
  std::vector<int> left = sizes;
  while (static_cast<int>(teams.size()) < total)
  {
    for (size_t a = 0; a < left.size(); ++a)
    {
      if (left[a] == 0) continue;
      --left[a];
      Continental::DrawTeam team;
      team.team_id = id++;
      team.association = static_cast<uint32_t>(a + 1);
      teams.push_back(team);
    }
  }
  const size_t pot_size = teams.size() / pots;
  for (size_t i = 0; i < teams.size(); ++i)
    teams[i].pot = static_cast<uint8_t>(i / pot_size);
  return teams;
}

struct DrawCheck
{
  bool own_association = false;
  bool over_two = false;
};

DrawCheck checkDraw(const std::vector<Continental::DrawTeam>& teams,
                    const std::vector<Continental::LeagueFixture>& fixtures,
                    uint8_t pots)
{
  DrawCheck check;
  std::map<TeamID, Continental::DrawTeam> by_id;
  for (const auto& team : teams) by_id[team.team_id] = team;
  const size_t matches = size_t{2} * pots;
  EXPECT_EQ(fixtures.size(), teams.size() * pots);
  std::map<TeamID, std::set<TeamID>> opponents;
  std::map<TeamID, int> home;
  std::map<TeamID, std::map<uint8_t, int>> home_by_pot;
  std::map<TeamID, std::map<uint8_t, int>> away_by_pot;
  std::map<TeamID, std::map<uint32_t, int>> by_association;
  std::map<std::pair<TeamID, uint8_t>, int> per_matchday;
  for (const auto& fixture : fixtures)
  {
    const auto& h = by_id.at(fixture.home_id);
    const auto& a = by_id.at(fixture.away_id);
    EXPECT_NE(fixture.home_id, fixture.away_id);
    EXPECT_TRUE(opponents[h.team_id].insert(a.team_id).second);
    EXPECT_TRUE(opponents[a.team_id].insert(h.team_id).second);
    ++home[h.team_id];
    ++home_by_pot[h.team_id][a.pot];
    ++away_by_pot[a.team_id][h.pot];
    if (h.association == a.association) check.own_association = true;
    if (++by_association[h.team_id][a.association] > 2) check.over_two = true;
    if (++by_association[a.team_id][h.association] > 2) check.over_two = true;
    EXPECT_GE(fixture.matchday, 1);
    EXPECT_LE(fixture.matchday, matches);
    const int home_count =
        ++per_matchday[std::pair{h.team_id, fixture.matchday}];
    const int away_count =
        ++per_matchday[std::pair{a.team_id, fixture.matchday}];
    EXPECT_EQ(home_count, 1);
    EXPECT_EQ(away_count, 1);
  }
  for (const auto& team : teams)
  {
    EXPECT_EQ(opponents[team.team_id].size(), matches);
    EXPECT_EQ(home[team.team_id], pots);
    for (uint8_t pot = 0; pot < pots; ++pot)
    {
      EXPECT_EQ(home_by_pot[team.team_id][pot], 1);
      EXPECT_EQ(away_by_pot[team.team_id][pot], 1);
    }
  }
  return check;
}

std::unique_ptr<GameController> makeWorld(int slot)
{
  Logger::init();
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot, WORLD_SEED);
  return controller;
}

/** Plays every unplayed continental fixture on or before @p date. */
void playUntil(Calendar& calendar, ContinentalCompetitions& continental,
               const GameDateValue& date, uint32_t salt)
{
  // Day by day: knockout rounds are added to the calendar as they are drawn.
  if (calendar.getFullCalendar().empty()) return;
  for (GameDateValue day = calendar.getFullCalendar().begin()->first;
       !(date < day); day = SeasonCalendar::addDays(day, 1))
  {
    for (Match& match : calendar.getMatchesForDateMutable(day))
    {
      if (match.isPlayed() || match.getMatchType() != MatchType::CONTINENTAL)
        continue;
      const uint32_t hash = Competitions::mixSeed(match.getHomeTeamId(),
                                                  match.getAwayTeamId(), salt);
      match.setPlayedResult(static_cast<uint8_t>(hash % 4),
                            static_cast<uint8_t>((hash / 4) % 3));
      continental.onResult(match);
      continental.resolveDecider(calendar, match);
    }
    continental.afterMatchday(calendar, day);
  }
}
}  // namespace

TEST(ContinentalTest, StageCodesRoundTrip)
{
  for (const Round round : {Round::Playoff, Round::RoundOf16,
                            Round::QuarterFinal, Round::SemiFinal})
  {
    for (uint8_t leg = 1; leg <= 2; ++leg)
    {
      const uint8_t stage = Continental::stageCode(round, leg);
      EXPECT_GE(stage, Continental::KNOCKOUT_STAGE_BASE);
      EXPECT_EQ(Continental::roundOf(stage), round);
      EXPECT_EQ(Continental::legOf(stage), leg);
    }
  }
  EXPECT_EQ(Continental::roundOf(Continental::stageCode(Round::Final, 1)),
            Round::Final);
  for (uint8_t matchday = 1; matchday <= 8; ++matchday)
    EXPECT_EQ(Continental::roundOf(matchday), Round::LeaguePhase);
}

TEST(ContinentalTest, AccessListFollowsAssociationRank)
{
  const auto& top = *Continental::rules(Continental::CHAMPIONS_CUP_ID);
  const std::vector<uint8_t> capacity(7, 20);
  const std::vector<uint8_t> places =
      Continental::allocatePlaces(top, 7, capacity);
  EXPECT_EQ(std::accumulate(places.begin(), places.end(), 0), 36);
  EXPECT_TRUE(std::ranges::is_sorted(places, std::greater<>{}));
  EXPECT_EQ(places.front(), 6);
  EXPECT_EQ(places.back(), 3);

  // An association with fewer free clubs passes its places on.
  const std::vector<uint8_t> tight = {20, 2, 20, 20, 20, 20, 20};
  const std::vector<uint8_t> limited =
      Continental::allocatePlaces(top, 7, tight);
  EXPECT_EQ(limited[1], 2);
  EXPECT_EQ(std::accumulate(limited.begin(), limited.end(), 0), 36);
}

TEST(ContinentalTest, SwissDrawIsValidForManySeeds)
{
  // Seven associations as in the default world.
  const auto teams = drawTeams({6, 6, 5, 5, 5, 5, 4}, 4);
  int relaxed = 0;
  for (uint32_t seed = 1; seed <= 1000; ++seed)
  {
    const auto fixtures = Continental::drawLeaguePhase(teams, 4, seed);
    const DrawCheck check = checkDraw(teams, fixtures, 4);
    EXPECT_FALSE(check.own_association) << "seed " << seed;
    if (check.over_two) ++relaxed;
  }
  EXPECT_EQ(relaxed, 0);
}

TEST(ContinentalTest, SmallerFormatsDrawToo)
{
  const auto shield = drawTeams({4, 4, 4, 3, 3, 3, 3}, 3);
  const auto americas = drawTeams({6, 6, 6, 6}, 3);
  for (uint32_t seed = 1; seed <= 100; ++seed)
  {
    EXPECT_FALSE(
        checkDraw(shield, Continental::drawLeaguePhase(shield, 3, seed), 3)
            .own_association);
    EXPECT_FALSE(
        checkDraw(americas, Continental::drawLeaguePhase(americas, 3, seed), 3)
            .own_association);
  }
}

TEST(ContinentalTest, DrawIsDeterministic)
{
  const auto teams = drawTeams({6, 6, 5, 5, 5, 5, 4}, 4);
  const auto first = Continental::drawLeaguePhase(teams, 4, 77);
  const auto second = Continental::drawLeaguePhase(teams, 4, 77);
  ASSERT_EQ(first.size(), second.size());
  for (size_t i = 0; i < first.size(); ++i)
  {
    EXPECT_EQ(first[i].home_id, second[i].home_id);
    EXPECT_EQ(first[i].away_id, second[i].away_id);
    EXPECT_EQ(first[i].matchday, second[i].matchday);
  }
}

TEST(ContinentalTest, LeaguePhaseTableUsesAwayGoalsAsTieBreaker)
{
  const GameDateValue day(2025, 9, 16);
  // 1 and 2 both win 2-1 and lose 0-1: 1 scored its goals away.
  Match a(3, 1, day, MatchType::CONTINENTAL, Continental::CHAMPIONS_CUP_ID, 1);
  a.setPlayedResult(1, 2);
  Match b(1, 4, day, MatchType::CONTINENTAL, Continental::CHAMPIONS_CUP_ID, 2);
  b.setPlayedResult(0, 1);
  Match c(2, 3, day, MatchType::CONTINENTAL, Continental::CHAMPIONS_CUP_ID, 1);
  c.setPlayedResult(2, 1);
  Match d(4, 2, day, MatchType::CONTINENTAL, Continental::CHAMPIONS_CUP_ID, 2);
  d.setPlayedResult(1, 0);
  const auto table = Continental::leaguePhaseTable(
      {1, 2, 3, 4}, {&a, &b, &c, &d}, [](TeamID) { return 0.0; });
  ASSERT_EQ(table.size(), 4u);
  EXPECT_EQ(table[0].team_id, 4);  // Two wins.
  EXPECT_EQ(table[1].team_id, 1);  // Level with 2 on points, GD, goals.
  EXPECT_EQ(table[2].team_id, 2);
  EXPECT_EQ(table[1].points, table[2].points);
}

TEST(ContinentalTest, CalendarKeepsContinentalWeeksFree)
{
  const auto weeks = SeasonCalendar::continentalWeeks(2025);
  ASSERT_EQ(weeks.size(), SeasonCalendar::CONTINENTAL_WEEKS);
  for (size_t i = 0; i < weeks.size(); ++i)
  {
    EXPECT_EQ(SeasonCalendar::dayOfWeek(weeks[i]), 1);  // Tuesday
    for (int offset = 0; offset < 3; ++offset)
      EXPECT_FALSE(SeasonCalendar::isBlackout(
          SeasonCalendar::addDays(weeks[i], offset)));
    if (i > 0) EXPECT_TRUE(weeks[i - 1] < weeks[i]);
  }
  // Domestic midweek rounds and cup ties avoid those weeks.
  for (const GameDateValue& date : SeasonCalendar::cupRoundDates(2025, 8))
    EXPECT_FALSE(SeasonCalendar::isContinentalWeek(date));
  for (const GameDateValue& date : SeasonCalendar::leagueRoundDates(2025, 38))
    EXPECT_FALSE(SeasonCalendar::isContinentalWeek(date));
}

TEST(ContinentalTest, InternationalWindowsFollowTheFifaCalendar)
{
  const auto before = SeasonCalendar::internationalWindows(2025);
  ASSERT_EQ(before.size(), 5u);  // Sep, Oct, Nov, Mar, Jun
  const auto after = SeasonCalendar::internationalWindows(2026);
  ASSERT_EQ(after.size(), 4u);  // Sep-Oct double window, Nov, Mar, Jun
  EXPECT_EQ(after[0].start, GameDateValue(2026, 9, 21));
  EXPECT_EQ(after[0].end, GameDateValue(2026, 10, 6));
  EXPECT_EQ(after[0].match_days.size(), 4u);
  EXPECT_EQ(after[1].start, GameDateValue(2026, 11, 9));
  EXPECT_EQ(after[2].start, GameDateValue(2027, 3, 22));
  EXPECT_EQ(after[3].start, GameDateValue(2027, 6, 7));
  EXPECT_TRUE(after[3].summer);
  for (const auto& window : after)
  {
    EXPECT_EQ(SeasonCalendar::dayOfWeek(window.start), 0);  // Monday
    for (const GameDateValue& day : window.match_days)
      EXPECT_TRUE(SeasonCalendar::isInternationalBreak(day));
  }
  // The summer window starts after the top continental final.
  EXPECT_TRUE(SeasonCalendar::addDays(SeasonCalendar::leagueEnd(2026), 14) <
              after[3].start);
}

TEST(ContinentalTest, FullCompetitionRunsToAWinnerAndPays)
{
  const SlotCleanup slot{uniqueSlot(1)};
  const auto controller = makeWorld(slot.slot);
  const auto gamedata = controller->getGameData();
  Calendar calendar;
  ContinentalCompetitions continental(gamedata);
  std::vector<std::pair<std::vector<TeamID>, bool>> news;
  continental.setNewsSink(
      [&news](InboxMessage, const std::vector<TeamID>& clubs, bool headline)
      { news.emplace_back(clubs, headline); });
  continental.startSeason(2025, GameDateValue(2025, 7, 2));
  ASSERT_EQ(continental.getSeasons().size(), 3u);
  const auto* top = continental.getSeason(Continental::CHAMPIONS_CUP_ID);
  ASSERT_NE(top, nullptr);
  EXPECT_EQ(top->entrants.size(), 36u);
  EXPECT_EQ(continental.getSeason(Continental::CONTINENTAL_SHIELD_ID)
                ->entrants.size(),
            24u);
  EXPECT_EQ(
      continental.getSeason(Continental::AMERICAS_CUP_ID)->entrants.size(),
      24u);
  // No club enters two competitions; pots follow the club coefficient.
  std::set<TeamID> seen;
  for (const auto& season : continental.getSeasons())
    for (const auto& entrant : season.entrants)
      EXPECT_TRUE(seen.insert(entrant.team_id).second);
  // Pots in order, coefficient order inside a pot, and at most half a pot
  // from one association (so the own-association ban stays satisfiable).
  std::map<std::pair<uint8_t, LeagueID>, int> per_pot;
  for (size_t i = 0; i < top->entrants.size(); ++i)
  {
    const auto& entrant = top->entrants[i];
    const int same = ++per_pot[std::pair{entrant.pot, entrant.association}];
    EXPECT_LE(same, 4);
    if (i == 0) continue;
    const auto& previous = top->entrants[i - 1];
    EXPECT_LE(previous.pot, entrant.pot);
    if (previous.pot == entrant.pot)
      EXPECT_GE(previous.coefficient, entrant.coefficient);
  }

  const TeamID champion_club = top->entrants.front().team_id;
  const int64_t balance_before =
      gamedata->getTeam(champion_club)->get().getFinances().getBalance();

  // Draw day: fixtures on Tuesday-Thursday continental weeks.
  continental.afterMatchday(calendar, top->draw_date);
  ASSERT_TRUE(continental.getSeason(Continental::CHAMPIONS_CUP_ID)->drawn);
  size_t fixtures = 0;
  for (const auto& [date, matches] : calendar.getFullCalendar())
    for (const Match& match : matches)
    {
      ++fixtures;
      EXPECT_TRUE(SeasonCalendar::isContinentalWeek(date)) << date.toString();
      EXPECT_EQ(match.getMatchType(), MatchType::CONTINENTAL);
    }
  EXPECT_EQ(fixtures, 36u * 4 + 24u * 3 + 24u * 3);
  // Never a club of the same association in the league phase.
  for (const auto& [date, matches] : calendar.getFullCalendar())
    for (const Match& match : matches)
      EXPECT_NE(
          Competitions::rootLeague(
              *gamedata,
              gamedata->getTeam(match.getHomeTeamId())->get().getLeagueId()),
          Competitions::rootLeague(
              *gamedata,
              gamedata->getTeam(match.getAwayTeamId())->get().getLeagueId()));
  EXPECT_GT(gamedata->getTeam(champion_club)->get().getFinances().getBalance(),
            balance_before);
  EXPECT_FALSE(news.empty());

  // Play the whole season: league phase, play-offs, knockouts, finals.
  playUntil(calendar, continental, GameDateValue(2026, 6, 30), 11);
  for (const auto& season : continental.getSeasons())
  {
    EXPECT_TRUE(season.league_phase_complete);
    EXPECT_NE(season.winner_id, 0) << int(season.competition_id);
    EXPECT_NE(season.runner_up_id, 0);
    EXPECT_EQ(season.ties.back().round, Round::Final);
    // Every knockout tie has a winner and exactly one team goes through.
    for (const auto& tie : season.ties)
    {
      EXPECT_TRUE(tie.winner_id == tie.seeded_id ||
                  tie.winner_id == tie.unseeded_id);
      const auto score = continental.tieScore(calendar, season, tie);
      if (score.seeded_goals != score.unseeded_goals)
        EXPECT_EQ(tie.winner_id, score.seeded_goals > score.unseeded_goals
                                     ? tie.seeded_id
                                     : tie.unseeded_id);
      else
      {
        EXPECT_TRUE(score.second_leg->wentToExtraTime());
        if (score.second_leg->wentToPenalties())
          EXPECT_EQ(tie.winner_id, score.second_leg->getHomePenalties() >
                                           score.second_leg->getAwayPenalties()
                                       ? tie.seeded_id
                                       : tie.unseeded_id);
      }
    }
  }
  const auto* finished = continental.getSeason(Continental::CHAMPIONS_CUP_ID);
  EXPECT_EQ(std::ranges::count(finished->ties, Round::Playoff,
                               &ContinentalCompetitions::Tie::round),
            8);
  EXPECT_EQ(std::ranges::count(finished->ties, Round::RoundOf16,
                               &ContinentalCompetitions::Tie::round),
            8);
  EXPECT_TRUE(
      std::ranges::any_of(news, [](const auto& item) { return item.second; }));

  // Season end: coefficients move and next season's clubs are qualified.
  std::unordered_map<LeagueID, std::vector<StandingRow>> tables;
  for (const LeagueID root : Competitions::countryRoots(*gamedata))
  {
    auto& rows = tables[root];
    for (const TeamID team_id : gamedata->getLeague(root)->get().getTeamIDs())
    {
      StandingRow row;
      row.team_id = team_id;
      rows.push_back(row);
    }
    std::ranges::sort(rows, {}, &StandingRow::team_id);
  }
  const auto england = Competitions::countryRoots(*gamedata).front();
  const TeamID cup_winner = tables[england].back().team_id;
  const double before = continental.clubCoefficient(finished->winner_id);
  continental.closeSeason(calendar, 2025, tables, {{england, cup_winner}});
  continental.closeSeason(calendar, 2025, tables,
                          {{england, cup_winner}});  // no-op
  EXPECT_NE(continental.clubCoefficient(finished->winner_id), before);
  const auto& qualified = continental.getQualified();
  ASSERT_EQ(qualified.size(), 3u);
  EXPECT_EQ(qualified.at(Continental::CHAMPIONS_CUP_ID).size(), 36u);
  // The cup winner (last in its table) takes a place in the second tier.
  const auto& shield = qualified.at(Continental::CONTINENTAL_SHIELD_ID);
  EXPECT_TRUE(std::ranges::any_of(
      shield, [&](const auto& entrant)
      { return entrant.team_id == cup_winner && entrant.cup_winner; }));
  const auto ranking =
      continental.associationRanking(Continental::Continent::Europe);
  EXPECT_EQ(ranking.size(), 7u);

  // The next season starts from that qualification.
  continental.startSeason(2026, GameDateValue(2026, 7, 1));
  EXPECT_EQ(continental.getSeasons().size(), 3u);
  EXPECT_EQ(continental.getSeasons().front().season_year, 2026);
}

TEST(ContinentalTest, LevelAggregateGoesToExtraTime)
{
  const SlotCleanup slot{uniqueSlot(2)};
  const auto controller = makeWorld(slot.slot);
  const auto gamedata = controller->getGameData();
  const auto league = gamedata->getLeagues().begin()->second.getTeamIDs();
  ASSERT_GE(league.size(), 2u);
  const TeamID seeded = league[0];
  const TeamID unseeded = league[1];
  Calendar calendar;
  ContinentalCompetitions continental(gamedata);
  const LeagueID id = Continental::CHAMPIONS_CUP_ID;
  Match first(unseeded, seeded, GameDateValue(2026, 3, 3),
              MatchType::CONTINENTAL, id,
              Continental::stageCode(Round::RoundOf16, 1));
  first.setPlayedResult(2, 1);
  calendar.addMatch(first);
  Match second(seeded, unseeded, GameDateValue(2026, 3, 10),
               MatchType::CONTINENTAL, id,
               Continental::stageCode(Round::RoundOf16, 2));
  second.setPlayedResult(1, 0);  // Not level on the day, level on aggregate.
  calendar.addMatch(second);
  Match* stored =
      calendar.findMatch(GameDateValue(2026, 3, 10), seeded, unseeded);
  ASSERT_NE(stored, nullptr);
  EXPECT_TRUE(continental.needsExtraTime(calendar, *stored));
  EXPECT_TRUE(continental.resolveDecider(calendar, *stored));
  EXPECT_TRUE(stored->wentToExtraTime());
  EXPECT_FALSE(continental.resolveDecider(calendar, *stored));  // Once.
  // No away goals: a 2-1 / 1-0 tie is never decided on away goals alone.
  const int seeded_total = stored->getHomeScore() + 1;
  const int unseeded_total = stored->getAwayScore() + 2;
  if (seeded_total == unseeded_total) EXPECT_TRUE(stored->wentToPenalties());
}

namespace
{
/** Goals of one side as the report tells them: its scorers' lines plus the
 * opponents' own goals, and its goal events plus the opponents' own goals. */
std::pair<int, int> reportedGoals(const MatchReport& report, TeamID team_id,
                                  bool home)
{
  int own_goals_for = 0;
  int events = 0;
  for (const MatchReportEvent& event : report.events)
  {
    if (event.kind == MatchEventKind::GOAL && event.home == home) ++events;
    if (event.kind == MatchEventKind::OWN_GOAL && event.home != home)
    {
      ++events;
      ++own_goals_for;
    }
  }
  int lines = own_goals_for;
  for (const PlayerMatchLine& line : report.players)
    if (line.team_id == team_id) lines += line.goals;
  return {lines, events};
}
}  // namespace

TEST(ContinentalTest, AggregateDecidesExtraTimeInTheSimulation)
{
  const SlotCleanup slot{uniqueSlot(5)};
  const auto controller = makeWorld(slot.slot);
  const auto gamedata = controller->getGameData();
  const auto league = gamedata->getLeagues().begin()->second.getTeamIDs();
  ASSERT_GE(league.size(), 2u);
  const Match second(league[0], league[1], GameDateValue(2026, 3, 10),
                     MatchType::CONTINENTAL, Continental::CHAMPIONS_CUP_ID,
                     Continental::stageCode(Round::RoundOf16, 2));
  std::optional<MatchSimulationInput> input =
      second.prepareSimulation(*gamedata);
  ASSERT_TRUE(input.has_value());
  const StatsConfig& config = gamedata->getStatsConfig();
  const MatchSimulationResult plain = MatchSimulation::run(*input, config);
  ASSERT_FALSE(plain.extra_time);
  EXPECT_EQ(reportedGoals(plain.report, league[0], true),
            std::pair(int{plain.home_goals}, int{plain.home_goals}));

  // Level on aggregate after 90 minutes (whatever the score on the day):
  // the engine plays extra time, then penalties if still level. The rules
  // only matter at full time, so the first 90 minutes are the plain ones.
  input->knockout = {.required = true,
                     .homeAggregate = plain.away_goals,
                     .awayAggregate = plain.home_goals};
  const MatchSimulationResult decided = MatchSimulation::run(*input, config);
  EXPECT_TRUE(decided.extra_time);
  EXPECT_GE(decided.home_goals, plain.home_goals);
  EXPECT_GE(decided.away_goals, plain.away_goals);
  const int home_goals = decided.home_goals;
  const int away_goals = decided.away_goals;
  EXPECT_EQ(reportedGoals(decided.report, league[0], true),
            std::pair(home_goals, home_goals));
  EXPECT_EQ(reportedGoals(decided.report, league[1], false),
            std::pair(away_goals, away_goals));
  // Every goal, extra time included, was scored by a player on the pitch
  // by the end of the 120 minutes (stoppage time is 120+n).
  for (const MatchReportEvent& event : decided.report.events)
  {
    if (event.kind != MatchEventKind::GOAL) continue;
    EXPECT_NE(event.player, 0u);
    EXPECT_LE(event.minute - event.added_minute, 120);
  }
  // The winner the engine names follows the aggregate, then the shootout.
  const int home_total = home_goals + plain.away_goals;
  const int away_total = away_goals + plain.home_goals;
  ASSERT_TRUE(decided.tie_winner_home.has_value());
  if (home_total != away_total)
  {
    EXPECT_FALSE(decided.penalties.has_value());
    EXPECT_EQ(*decided.tie_winner_home, home_total > away_total);
  }
  else
  {
    ASSERT_TRUE(decided.penalties.has_value());
    EXPECT_NE(decided.penalties->first, decided.penalties->second);
    EXPECT_EQ(*decided.tie_winner_home,
              decided.penalties->first > decided.penalties->second);
  }
  EXPECT_EQ(decided.report.extra_time, decided.extra_time);
  EXPECT_EQ(decided.report.penalties, decided.penalties.has_value());
  // Whoever played the whole match is reported with 120 minutes.
  bool full_match = false;
  for (const PlayerMatchLine& line : decided.report.players)
  {
    EXPECT_LE(line.minutes, 120);
    full_match |= line.minutes == 120;
  }
  EXPECT_TRUE(full_match);

  // Not level on aggregate: no extra time, the plain 90 minutes stand.
  input->knockout.homeAggregate += 1;
  const MatchSimulationResult settled = MatchSimulation::run(*input, config);
  EXPECT_FALSE(settled.extra_time);
  EXPECT_FALSE(settled.penalties.has_value());
  EXPECT_EQ(settled.home_goals, plain.home_goals);
  EXPECT_EQ(settled.away_goals, plain.away_goals);
  EXPECT_EQ(settled.tie_winner_home, std::optional<bool>(true));
}

TEST(ContinentalTest, ExtraTimeResultCountsForTheManager)
{
  // Plays a second leg of the managed club once without a first leg to
  // learn its 90-minute score, then again (same world, same days) after a
  // first leg that levels the aggregate, so the engine plays extra time.
  // Candidates are tried until one tie is won or lost in extra time; ties
  // that go to penalties are checked on the way.
  const SlotCleanup slot_a{uniqueSlot(6)};
  const SlotCleanup slot_b{uniqueSlot(7)};
  const auto busy = [](const GameController& controller, TeamID team_id,
                       const GameDateValue& date)
  {
    for (const Match& match : controller.getTeamFixtures(team_id))
      for (int offset = -1; offset <= 1; ++offset)
        if (match.getDate() == SeasonCalendar::addDays(date, offset))
          return true;
    return false;
  };
  const LeagueID competition = Continental::CHAMPIONS_CUP_ID;
  const uint8_t stage = Continental::stageCode(Round::RoundOf16, 2);
  TeamID managed = 0;
  // Opponents from another league on a free day for both.
  std::vector<std::pair<TeamID, GameDateValue>> candidates;
  {
    const auto world = makeWorld(slot_a.slot);
    managed = world->getTeams().front().get().getId();
    ASSERT_NE(managed, FREE_AGENTS_TEAM_ID);
    const LeagueID own_league =
        world->getTeamById(managed)->get().getLeagueId();
    GameDateValue day = SeasonCalendar::addDays(world->getCurrentDate(), 2);
    // About half of the ties still level after extra time go to penalties,
    // so eight candidates make an extra-time decision all but certain.
    for (int tries = 0; tries < 40 && candidates.size() < 8; ++tries)
    {
      day = SeasonCalendar::addDays(day, 1);
      if (busy(*world, managed, day)) continue;
      for (const auto& team : world->getTeams())
      {
        const Team& other = team.get();
        if (other.getId() == FREE_AGENTS_TEAM_ID ||
            other.getLeagueId() == own_league ||
            busy(*world, other.getId(), day) ||
            std::ranges::contains(candidates, other.getId(),
                                  &std::pair<TeamID, GameDateValue>::first))
          continue;
        candidates.emplace_back(other.getId(), day);
        break;
      }
    }
  }
  ASSERT_FALSE(candidates.empty());

  bool decided_in_extra_time = false;
  for (const auto& [opponent, day] : candidates)
  {
    const Match second_leg(managed, opponent, day, MatchType::CONTINENTAL,
                           competition, stage);
    // The assistant plays a managed fixture left unplayed the next day.
    const GameDateValue after = SeasonCalendar::addDays(day, 1);
    int home_90 = 0;
    int away_90 = 0;
    {
      auto first_run = makeWorld(slot_a.slot);
      first_run->selectManagedTeam(managed);
      first_run->getGame()->getCalendar().addMatch(second_leg);
      while (first_run->getCurrentDate() < after) first_run->advanceDay();
      const Match* played =
          first_run->getGame()->getCalendar().findMatch(day, managed, opponent);
      ASSERT_NE(played, nullptr);
      ASSERT_TRUE(played->isPlayed());
      ASSERT_FALSE(played->wentToExtraTime());
      home_90 = played->getHomeScore();
      away_90 = played->getAwayScore();
    }

    auto controller = makeWorld(slot_b.slot);
    controller->selectManagedTeam(managed);
    // First leg at the opponent's ground: managed club scored y, conceded x.
    const int x = std::max(0, home_90 - away_90);
    const int y = std::max(0, away_90 - home_90);
    Match first_leg(opponent, managed, GameDateValue(2025, 6, 25),
                    MatchType::CONTINENTAL, competition,
                    Continental::stageCode(Round::RoundOf16, 1));
    first_leg.setPlayedResult(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
    controller->getGame()->getCalendar().addMatch(first_leg);
    controller->getGame()->getCalendar().addMatch(second_leg);
    while (controller->getCurrentDate() < after) controller->advanceDay();

    const Match* decider =
        controller->getGame()->getCalendar().findMatch(day, managed, opponent);
    ASSERT_NE(decider, nullptr);
    ASSERT_TRUE(decider->isPlayed());
    EXPECT_TRUE(decider->wentToExtraTime());
    const int home_goals = decider->getHomeScore();
    const int away_goals = decider->getAwayScore();
    // The first 90 minutes were the same: level on aggregate.
    EXPECT_GE(home_goals, home_90);
    EXPECT_GE(away_goals, away_90);

    // The stored report adds up to the final score.
    const auto report = controller->getMatchReport(day, managed, opponent);
    ASSERT_TRUE(report.has_value());
    EXPECT_TRUE(report->extra_time);
    EXPECT_EQ(report->penalties, decider->wentToPenalties());
    EXPECT_EQ(reportedGoals(*report, managed, true),
              std::pair(home_goals, home_goals));
    EXPECT_EQ(reportedGoals(*report, opponent, false),
              std::pair(away_goals, away_goals));

    // Form and the manager's record count the match's score after extra
    // time (a second leg can be drawn on the day and still decide the tie).
    char outcome = 'D';
    if (home_goals > away_goals)
      outcome = 'W';
    else if (home_goals < away_goals)
      outcome = 'L';
    const std::string& form =
        controller->getTeamById(managed)->get().getRecentForm();
    ASSERT_FALSE(form.empty());
    EXPECT_EQ(form.front(), outcome) << form;  // Newest first.
    ASSERT_FALSE(controller->getManagerStints().empty());
    const ManagerStint& stint = controller->getManagerStints().back();
    EXPECT_EQ(stint.won, outcome == 'W' ? 1 : 0);
    EXPECT_EQ(stint.lost, outcome == 'L' ? 1 : 0);
    EXPECT_EQ(stint.drawn, outcome == 'D' ? 1 : 0);
    if (decider->wentToPenalties())
    {
      // Level on aggregate after 120 minutes: the shootout names the winner.
      EXPECT_EQ(home_goals - away_goals, x - y);
      EXPECT_NE(decider->getHomePenalties(), decider->getAwayPenalties());
      continue;
    }
    EXPECT_NE(home_goals + y, away_goals + x) << "a winner after extra time";
    // Decided in extra time: its goals are in the recorded score.
    EXPECT_GT(home_goals + away_goals, home_90 + away_90);
    decided_in_extra_time = true;
    break;
  }
  EXPECT_TRUE(decided_in_extra_time)
      << "no candidate tie was decided in extra time";
}

TEST(ContinentalTest, UnevenPotsFallBackToCoefficientPots)
{
  const SlotCleanup slot{uniqueSlot(8)};
  const auto controller = makeWorld(slot.slot);
  const auto gamedata = controller->getGameData();
  ContinentalCompetitions original(gamedata);
  original.startSeason(2025, GameDateValue(2025, 7, 2));
  // Every club of the top competition in one pot: the draw cannot use them.
  DatabaseConnection db(":memory:");
  Migrations::migrate(db);
  original.save(db);
  sqlite3_stmt* select =
      db.prepareStatement("SELECT data FROM ContinentalState WHERE id = 1;");
  ASSERT_EQ(sqlite3_step(select), SQLITE_ROW);
  auto state = nlohmann::json::parse(
      reinterpret_cast<const char*>(sqlite3_column_text(select, 0)));
  sqlite3_finalize(select);
  for (auto& season : state["seasons"])
    if (season["competition"] == Continental::CHAMPIONS_CUP_ID)
      for (auto& entrant : season["entrants"]) entrant["pot"] = 0;
  sqlite3_stmt* update =
      db.prepareStatement("UPDATE ContinentalState SET data = ? WHERE id = 1;");
  const std::string text = state.dump();
  sqlite3_bind_text(update, 1, text.c_str(), -1, SQLITE_TRANSIENT);
  db.executeStep(update);
  sqlite3_finalize(update);

  ContinentalCompetitions continental(gamedata);
  continental.load(db);
  const auto* season = continental.getSeason(Continental::CHAMPIONS_CUP_ID);
  ASSERT_NE(season, nullptr);
  Calendar calendar;
  for (GameDateValue day(2025, 7, 2); !(season->draw_date < day);
       day = SeasonCalendar::addDays(day, 1))
    continental.afterMatchday(calendar, day);
  season = continental.getSeason(Continental::CHAMPIONS_CUP_ID);
  ASSERT_TRUE(season->drawn);
  std::map<TeamID, int> matches;
  for (const auto& [date, day] : calendar.getFullCalendar())
    for (const Match& match : day)
      if (match.getCompetitionId() == Continental::CHAMPIONS_CUP_ID)
      {
        ++matches[match.getHomeTeamId()];
        ++matches[match.getAwayTeamId()];
      }
  EXPECT_EQ(matches.size(), season->entrants.size());
  for (const auto& [team_id, count] : matches)
    EXPECT_EQ(count, season->matches) << team_id;
}

TEST(ContinentalTest, StateRoundTripsThroughSave)
{
  const SlotCleanup slot{uniqueSlot(3)};
  const auto controller = makeWorld(slot.slot);
  const auto gamedata = controller->getGameData();
  Calendar calendar;
  ContinentalCompetitions continental(gamedata);
  continental.startSeason(2025, GameDateValue(2025, 7, 2));
  continental.afterMatchday(calendar, GameDateValue(2025, 8, 29));
  const std::string saved = continental.serialize();
  ContinentalCompetitions restored(gamedata);
  restored.deserialize(saved);
  EXPECT_EQ(restored.serialize(), saved);
  ASSERT_EQ(restored.getSeasons().size(), continental.getSeasons().size());
  EXPECT_EQ(restored.getSeasons().front().entrants.size(),
            continental.getSeasons().front().entrants.size());
}

TEST(KnockoutPipelineTest, CupTiesArePlayedToAWinnerByTheEngine)
{
  // One-off cup ties between clubs with a free day, simulated by the
  // matchday pipeline: level after 90 minutes they go to extra time and
  // then to penalties, and every tie has a winner.
  const SlotCleanup slot{uniqueSlot(4)};
  const auto controller = makeWorld(slot.slot);
  const TeamID managed = controller->getTeams().front().get().getId();
  controller->selectManagedTeam(managed);
  const GameDateValue day =
      SeasonCalendar::addDays(controller->getCurrentDate(), 3);
  const auto busy = [&](TeamID team_id)
  {
    for (const Match& match : controller->getTeamFixtures(team_id))
      for (int offset = -1; offset <= 1; ++offset)
        if (match.getDate() == SeasonCalendar::addDays(day, offset))
          return true;
    return false;
  };
  std::vector<TeamID> free_clubs;
  for (const auto& team : controller->getTeams())
  {
    if (const TeamID id = team.get().getId();
        id != FREE_AGENTS_TEAM_ID && id != managed && !busy(id))
      free_clubs.push_back(id);
    if (free_clubs.size() == 80) break;
  }
  ASSERT_GE(free_clubs.size(), 40u);
  std::vector<std::pair<TeamID, TeamID>> ties;
  for (std::size_t index = 0; index + 1 < free_clubs.size(); index += 2)
  {
    ties.emplace_back(free_clubs[index], free_clubs[index + 1]);
    controller->getGame()->getCalendar().addMatch(
        Match(free_clubs[index], free_clubs[index + 1], day, MatchType::CUP));
  }
  const GameDateValue after = SeasonCalendar::addDays(day, 1);
  while (controller->getCurrentDate() < after) controller->advanceDay();

  int extra_time = 0;
  int shootouts = 0;
  for (const auto& [home, away] : ties)
  {
    const Match* tie =
        controller->getGame()->getCalendar().findMatch(day, home, away);
    ASSERT_NE(tie, nullptr);
    ASSERT_TRUE(tie->isPlayed());
    EXPECT_TRUE(tie->getWinnerId().has_value());
    const bool level = tie->getHomeScore() == tie->getAwayScore();
    EXPECT_EQ(level, tie->wentToPenalties());
    if (tie->wentToPenalties())
    {
      EXPECT_TRUE(tie->wentToExtraTime()) << "penalties come after extra time";
      EXPECT_NE(tie->getHomePenalties(), tie->getAwayPenalties());
    }
    extra_time += tie->wentToExtraTime() ? 1 : 0;
    shootouts += tie->wentToPenalties() ? 1 : 0;

    const auto report = controller->getMatchReport(day, home, away);
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->extra_time, tie->wentToExtraTime());
    EXPECT_EQ(report->penalties, tie->wentToPenalties());
    EXPECT_EQ(report->home_penalties, tie->getHomePenalties());
    EXPECT_EQ(report->away_penalties, tie->getAwayPenalties());
    // The engine's goals (extra time included) and nothing else: shootout
    // kicks are not goals.
    const int home_goals = tie->getHomeScore();
    const int away_goals = tie->getAwayScore();
    EXPECT_EQ(reportedGoals(*report, home, true),
              std::pair(home_goals, home_goals));
    EXPECT_EQ(reportedGoals(*report, away, false),
              std::pair(away_goals, away_goals));
    for (const PlayerMatchLine& line : report->players)
      EXPECT_LE(line.minutes, tie->wentToExtraTime() ? 120 : 90);
  }
  EXPECT_GT(extra_time, 0);
  EXPECT_GT(shootouts, 0);
  EXPECT_LT(extra_time, static_cast<int>(ties.size()));
}

TEST(KnockoutPipelineTest, LiveCupTieKeepsTheEnginesExtraTimeAndShootout)
{
  // A cup tie watched live: the engine gets the knockout rules from the
  // controller and the recorded result is exactly what it played (no
  // statistical extra time on top).
  const SlotCleanup slot{uniqueSlot(9)};
  const auto controller = makeWorld(slot.slot);
  const TeamID managed = controller->getTeams().front().get().getId();
  controller->selectManagedTeam(managed);
  const GameDateValue today = controller->getCurrentDate();
  for (const Match& match : controller->getTeamFixtures(managed))
    ASSERT_FALSE(match.getDate() == today) << "the managed club is busy today";
  const LeagueID own_league =
      controller->getTeamById(managed)->get().getLeagueId();
  const StatsConfig& config = controller->getStatsConfig();

  // The first opponent whose tie is level after 90 minutes.
  TeamID opponent = 0;
  std::unique_ptr<MatchEngine> engine;
  for (const auto& team : controller->getTeams())
  {
    const Team& other = team.get();
    if (other.getId() == FREE_AGENTS_TEAM_ID || other.getId() == managed ||
        other.getLeagueId() == own_league)
      continue;
    if (const bool free_today = std::ranges::none_of(
            controller->getTeamFixtures(other.getId()),
            [&](const Match& match) { return match.getDate() == today; });
        !free_today)
      continue;
    const Match probe(managed, other.getId(), today, MatchType::CUP);
    const auto rules = controller->getGame()->getCompetitions().knockoutRules(
        controller->getGame()->getCalendar(), probe);
    ASSERT_TRUE(rules.has_value());
    const Team& home = controller->getTeamById(managed)->get();
    auto candidate = std::make_unique<MatchEngine>(
        home.getLineup(), other.getLineup(), home.getStrategy(),
        other.getStrategy(), config, probe.getSeed());
    candidate->setKnockout(*rules);
    candidate->simulateToEnd();
    if (candidate->wentToExtraTime())
    {
      opponent = other.getId();
      engine = std::move(candidate);
      controller->getGame()->getCalendar().addMatch(probe);
      break;
    }
  }
  ASSERT_NE(opponent, 0) << "no tie went to extra time";
  ASSERT_TRUE(engine->getTieWinnerHome().has_value());
  // What the live match screen asks for before kick-off.
  const auto rules = controller->getKnockoutRules(today, managed, opponent);
  ASSERT_TRUE(rules.has_value());
  EXPECT_TRUE(rules->required);
  EXPECT_TRUE(rules->extraTime);
  EXPECT_EQ(rules->homeAggregate, 0);
  EXPECT_EQ(rules->awayAggregate, 0);
  EXPECT_FALSE(controller->getKnockoutRules(today, opponent, managed))
      << "no such fixture";

  ASSERT_TRUE(controller->setMatchResult(today, managed, opponent, *engine));
  const Match* tie =
      controller->getGame()->getCalendar().findMatch(today, managed, opponent);
  ASSERT_NE(tie, nullptr);
  ASSERT_TRUE(tie->isPlayed());
  EXPECT_TRUE(tie->wentToExtraTime());
  EXPECT_EQ(tie->getHomeScore(), engine->getHomeScore());
  EXPECT_EQ(tie->getAwayScore(), engine->getAwayScore());
  EXPECT_EQ(tie->wentToPenalties(), engine->hasShootout());
  EXPECT_EQ(tie->getHomePenalties(), engine->getShootoutScore(true));
  EXPECT_EQ(tie->getAwayPenalties(), engine->getShootoutScore(false));
  ASSERT_TRUE(tie->getWinnerId().has_value());
  EXPECT_EQ(*tie->getWinnerId() == managed, *engine->getTieWinnerHome());
  const auto report = controller->getMatchReport(today, managed, opponent);
  ASSERT_TRUE(report.has_value());
  EXPECT_TRUE(report->extra_time);
  EXPECT_EQ(reportedGoals(*report, managed, true),
            std::pair(engine->getHomeScore(), engine->getHomeScore()));
  EXPECT_EQ(reportedGoals(*report, opponent, false),
            std::pair(engine->getAwayScore(), engine->getAwayScore()));
}
