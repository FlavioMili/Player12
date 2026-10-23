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
#include <memory>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/awards.h"
#include "model/board.h"
#include "model/inbox.h"
#include "model/match_report.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 2'000'000 + getpid() % 100'000 * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

std::unique_ptr<GameController> makeCareer(int slot)
{
  Logger::init();
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot, WORLD_SEED);
  controller->selectManagedTeam(controller->getTeams().front().get().getId());
  return controller;
}

AwardCandidate candidate(PlayerID id, PlayerRole role, int age,
                         std::uint16_t minutes, float rating,
                         std::uint16_t goals = 0, std::uint16_t clean = 0)
{
  AwardCandidate result;
  result.role = role;
  result.age = age;
  result.tally.player_id = id;
  result.tally.minutes = minutes;
  result.tally.appearances = static_cast<std::uint16_t>(minutes / 90);
  result.tally.rated = result.tally.appearances;
  result.tally.rating_total = rating * static_cast<float>(result.tally.rated);
  result.tally.goals = goals;
  result.tally.clean_sheets = clean;
  return result;
}

/** A league match between two clubs with full-match lines. */
MatchReport leagueMatch(const GameDateValue& date, TeamID home, TeamID away,
                        LeagueID league, std::uint8_t home_goals,
                        std::uint8_t away_goals)
{
  MatchReport report;
  report.date = date;
  report.home_team_id = home;
  report.away_team_id = away;
  report.match_type = MatchType::LEAGUE;
  report.competition_id = league;
  report.home_goals = home_goals;
  report.away_goals = away_goals;
  return report;
}

PlayerMatchLine line(PlayerID id, TeamID team, std::uint8_t minutes,
                     float rating, std::uint8_t goals = 0)
{
  PlayerMatchLine result;
  result.player_id = id;
  result.team_id = team;
  result.minutes = minutes;
  result.rating = rating;
  result.goals = goals;
  return result;
}

/** First player of a club matching the predicate. */
template <typename Pred>
PlayerID findPlayer(const GameController& controller, TeamID team, Pred pred)
{
  for (const auto& player : controller.getPlayersForTeam(team))
    if (pred(player.get())) return player.get().getId();
  return 0;
}
}  // namespace

TEST(Awards, PlayerOfMonthNeedsMinimumMinutes)
{
  using enum PlayerRole;
  const std::vector pool = {
      candidate(1, CM, 27, 180, 9.5f),  // two matches only
      candidate(2, CM, 27, 270, 7.4f), candidate(3, ST, 27, 360, 7.1f)};
  const auto best = Awards::bestPlayer(pool, Awards::MONTH_MIN_MINUTES);
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(pool[*best].tally.player_id, 2u);
}

TEST(Awards, YoungPlayerRespectsAgeAndTiesGoToLowerId)
{
  using enum PlayerRole;
  const std::vector pool = {candidate(9, CM, 22, 360, 8.0f),
                            candidate(7, CM, 21, 360, 7.0f),
                            candidate(5, CM, 20, 360, 7.0f)};
  const auto young = Awards::bestPlayer(pool, Awards::MONTH_MIN_MINUTES,
                                        Awards::YOUNG_MAX_AGE);
  ASSERT_TRUE(young.has_value());
  EXPECT_EQ(pool[*young].tally.player_id, 5u);
  EXPECT_EQ(pool[*Awards::bestPlayer(pool, 0)].tally.player_id, 9u);
}

TEST(Awards, GoldenBootAndGloveTieBreaks)
{
  using enum PlayerRole;
  const std::vector pool = {
      candidate(1, ST, 25, 2700, 7.0f, 20),
      candidate(2, ST, 25, 2400, 7.0f, 20),  // fewer minutes
      candidate(3, GK, 25, 3000, 6.8f, 0, 12),
      candidate(4, GK, 25, 900, 6.8f, 0, 14),    // too few minutes
      candidate(5, CB, 25, 3000, 6.8f, 0, 15)};  // not a keeper
  EXPECT_EQ(pool[*Awards::goldenBoot(pool)].tally.player_id, 2u);
  const auto glove = Awards::goldenGlove(pool, Awards::seasonMinMinutes(pool));
  ASSERT_TRUE(glove.has_value());
  EXPECT_EQ(pool[*glove].tally.player_id, 3u);
  EXPECT_FALSE(Awards::goldenBoot(std::vector{candidate(8, ST, 25, 900, 7.0f)})
                   .has_value());
}

TEST(Awards, TeamOfSeasonFillsTheFormationWithFallbacks)
{
  using enum PlayerRole;
  std::vector<AwardCandidate> pool;
  PlayerID id = 1;
  for (const PlayerRole role : {GK, RB, CB, CB, CB, CM, CDM, CAM, RW, ST, LM})
    pool.push_back(candidate(id++, role, 26, 2500, 7.0f + 0.01f * id));
  // No natural left back: the spare centre back covers it.
  const auto team = Awards::teamOfSeason(pool, 1000);
  for (const auto& slot : team) ASSERT_TRUE(slot.has_value());
  EXPECT_EQ(pool[*team[0]].role, GK);
  EXPECT_EQ(pool[*team[4]].role, CB);
  EXPECT_EQ(pool[*team[10]].role, LM);
  std::vector<std::size_t> picked;
  for (const auto& slot : team) picked.push_back(*slot);
  std::ranges::sort(picked);
  EXPECT_EQ(std::ranges::unique(picked).begin(), picked.end());
  // Below the minimum nobody makes it.
  const auto empty = Awards::teamOfSeason(pool, 5000);
  EXPECT_TRUE(std::ranges::none_of(
      empty, [](const auto& slot) { return slot.has_value(); }));
}

TEST(Awards, ManagerAwardComparesPointsWithExpectation)
{
  std::vector<AwardClubTally> clubs(3);
  clubs[0] = {1, 1, 4, 12.0f, 11.0f, 10, 2};  // won everything, as expected
  clubs[1] = {2, 1, 4, 8.0f, 4.0f, 6, 4};     // far above expectation
  clubs[2] = {3, 1, 2, 6.0f, 1.0f, 4, 0};     // too few matches
  const auto best = Awards::bestManager(clubs, Awards::MONTH_MIN_MATCHES);
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(clubs[*best].team_id, 2u);
}

TEST(Awards, GoalScoreRewardsLateWinners)
{
  const float late_winner = Awards::goalScore(88, 1, 1, true, 0, false);
  const float early_opener = Awards::goalScore(10, 0, 0, false, 0, true);
  const float equaliser = Awards::goalScore(60, 0, 1, false, 10, true);
  EXPECT_GT(late_winner, equaliser);
  EXPECT_GT(equaliser, early_opener);
}

TEST(Awards, ValuePremiumIsRecentAndCapped)
{
  using enum AwardType;
  const GameDateValue today(2027, 3, 10);
  std::vector<AwardRecord> honours;
  AwardRecord month;
  month.type = AwardType::PlayerOfMonth;
  month.season_year = 2026;
  month.month = 11;
  honours.push_back(month);
  EXPECT_NEAR(Awards::valueMultiplier(honours, today), 1.03f, 1e-4f);
  month.season_year = 2024;  // too old
  honours.push_back(month);
  EXPECT_NEAR(Awards::valueMultiplier(honours, today), 1.03f, 1e-4f);
  AwardRecord season;
  season.season_year = 2026;
  for (const AwardType type :
       {PlayerOfSeason, GoldenBoot, YoungPlayerOfSeason, GoldenGlove})
  {
    season.type = type;
    honours.push_back(season);
  }
  EXPECT_NEAR(Awards::valueMultiplier(honours, today),
              1.0f + Awards::MAX_VALUE_PREMIUM, 1e-4f);
}

TEST(Awards, MonthlyAwardsFromMatchReports)
{
  const SlotCleanup slot{uniqueSlot(0)};
  auto controller = makeCareer(slot.slot);
  const auto gamedata = controller->getGameData();
  const LeagueID league = controller->getManagedTeam()->get().getLeagueId();
  const auto& clubs = controller->getLeagueById(league)->get().getTeamIDs();
  ASSERT_GE(clubs.size(), 2u);
  const TeamID a = clubs[0];
  const TeamID b = clubs[1];
  const PlayerID star =
      findPlayer(*controller, a, [](const Player& p)
                 { return p.getAge() > 23 && p.getRole() != PlayerRole::GK; });
  const PlayerID cameo =
      findPlayer(*controller, b, [](const Player& p)
                 { return p.getAge() > 23 && p.getRole() != PlayerRole::GK; });
  const PlayerID youngster = findPlayer(
      *controller, b, [](const Player& p) { return p.getAge() <= 21; });
  const PlayerID keeper = findPlayer(*controller, a, [](const Player& p)
                                     { return p.getRole() == PlayerRole::GK; });
  ASSERT_NE(star, 0u);
  ASSERT_NE(cameo, 0u);
  ASSERT_NE(youngster, 0u);
  ASSERT_NE(keeper, 0u);

  AwardSystem awards;
  for (int round = 0; round < 4; ++round)
  {
    const bool a_home = round % 2 == 0;
    MatchReport report = leagueMatch(
        GameDateValue(2025, 8, static_cast<std::uint8_t>(9 + 7 * round)),
        a_home ? a : b, a_home ? b : a, league, a_home ? 2 : 0, a_home ? 0 : 2);
    report.players.push_back(line(star, a, 90, 7.6f, 1));
    report.players.push_back(line(keeper, a, 90, 7.0f));
    report.players.push_back(line(youngster, b, 90, 7.4f));
    // A brilliant substitute who never reaches the minimum minutes.
    report.players.push_back(line(cameo, b, 30, 9.6f));
    MatchReportEvent late;
    late.minute = static_cast<std::uint8_t>(round == 2 ? 89 : 20);
    late.home = a_home;
    late.player = star;
    report.events.push_back(late);
    awards.onMatchPlayed(*gamedata, report, 1.2f, 1.2f);
  }
  ASSERT_EQ(awards.monthTallies().size(), 4u);
  const std::vector<AwardRecord> given = awards.awardMonth(*gamedata, 2025, 8);
  const auto find = [&](AwardType type)
  {
    const auto it = std::ranges::find(given, type, &AwardRecord::type);
    return it == given.end() ? nullptr : std::to_address(it);
  };
  ASSERT_NE(find(AwardType::PlayerOfMonth), nullptr);
  EXPECT_EQ(find(AwardType::PlayerOfMonth)->player_id, star);
  ASSERT_NE(find(AwardType::YoungPlayerOfMonth), nullptr);
  EXPECT_EQ(find(AwardType::YoungPlayerOfMonth)->player_id, youngster);
  ASSERT_NE(find(AwardType::ManagerOfMonth), nullptr);
  EXPECT_EQ(find(AwardType::ManagerOfMonth)->team_id, a);
  EXPECT_NEAR(find(AwardType::ManagerOfMonth)->value, 4.0f * (3.0f - 1.2f),
              0.05f);
  ASSERT_NE(find(AwardType::GoalOfMonth), nullptr);
  EXPECT_EQ(find(AwardType::GoalOfMonth)->player_id, star);
  EXPECT_EQ(find(AwardType::GoalOfMonth)->count, 89u);
  EXPECT_TRUE(awards.monthTallies().empty());
  // Season tallies keep running: clean sheets for the keeper.
  const auto& season = awards.seasonTallies();
  EXPECT_EQ(season.at({league, keeper}).clean_sheets, 4u);

  const std::vector<AwardRecord> end = awards.awardSeason(*gamedata, 2025);
  const auto boot =
      std::ranges::find(end, AwardType::GoldenBoot, &AwardRecord::type);
  ASSERT_NE(boot, end.end());
  EXPECT_EQ(boot->player_id, star);
  EXPECT_EQ(boot->value, 4.0f);
  const auto glove =
      std::ranges::find(end, AwardType::GoldenGlove, &AwardRecord::type);
  ASSERT_NE(glove, end.end());
  EXPECT_EQ(glove->player_id, keeper);
  EXPECT_TRUE(std::ranges::contains(awards.honoursFor(star),
                                    AwardType::GoldenBoot, &AwardRecord::type));
}

TEST(Awards, CareerAwardsAugustAndSurvivesSaveLoad)
{
  const SlotCleanup slot{uniqueSlot(1)};
  auto controller = makeCareer(slot.slot);
  const LeagueID league = controller->getManagedTeam()->get().getLeagueId();
  while (controller->getCurrentDate() < GameDateValue(2025, 9, 1))
    controller->advanceDay();
  const std::vector<AwardRecord> august =
      controller->getLeagueAwards(league, 2025);
  ASSERT_FALSE(august.empty());
  const auto player_of_month =
      std::ranges::find(august, AwardType::PlayerOfMonth, &AwardRecord::type);
  ASSERT_NE(player_of_month, august.end());
  EXPECT_EQ(player_of_month->month, 8u);
  EXPECT_GE(player_of_month->count, 3u);
  EXPECT_FALSE(player_of_month->name.empty());
  EXPECT_TRUE(std::ranges::any_of(
      controller->getInbox(), [](const InboxMessage& message)
      { return message.title_key == "INBOX_AWARDS_MONTH_TITLE"; }));
  // Every league of the world has its own winners.
  EXPECT_GT(controller->getAwardHistory().size(), august.size());
  const auto race = controller->getAwardRace(league, false, 5);
  EXPECT_EQ(race.size(), 5u);

  // The honour raises the winner's price.
  const PlayerID winner = player_of_month->player_id;
  const auto player = controller->getGameData()->getPlayer(winner);
  ASSERT_TRUE(player.has_value());
  player->get().updateMarketValue(controller->getStatsConfig());
  EXPECT_GT(controller->getPlayerMarketValue(winner),
            player->get().getMarketValue());

  const size_t history = controller->getAwardHistory().size();
  const size_t tallies =
      controller->getGame()->getWorld().getAwards().seasonTallies().size();
  ASSERT_TRUE(controller->saveGame());
  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  ASSERT_EQ(reloaded.getAwardHistory().size(), history);
  EXPECT_EQ(reloaded.getAwardHistory().front().name,
            controller->getAwardHistory().front().name);
  EXPECT_EQ(reloaded.getGame()->getWorld().getAwards().seasonTallies().size(),
            tallies);
  EXPECT_EQ(reloaded.getPlayerHonours(winner).size(),
            controller->getPlayerHonours(winner).size());
}
