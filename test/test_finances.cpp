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
#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <utility>
#include <vector>

#include "controller/game_controller.h"
#include "database/database_connection.h"
#include "database/gamedata.h"
#include "database/repositories/finance_repository.h"
#include "database/repositories/team_repository.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/club_economy.h"
#include "model/finances.h"
#include "model/manager_career.h"
#include "model/match.h"
#include "model/match_report.h"
#include "model/team.h"
#include "model/world_rng.h"
#include "model/world_simulation.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 424242;

/** Save slot unique to this process (tests run in parallel processes). */
int financeSlot() { return 100'000 + getpid() % 100'000 * 10 + 5; }

const GameDateValue DAY_ONE(2025, 7, 2);
const GameDateValue DAY_TWO(2025, 7, 9);
const GameDateValue DAY_THREE(2025, 8, 1);

std::int64_t categoryTotal(const Finances& finances, FinanceCategory category)
{
  std::int64_t total = 0;
  for (const FinanceTransaction& transaction : finances.getLedger())
  {
    if (transaction.category == category) total += transaction.amount;
  }
  return total;
}
}  // namespace

TEST(FinancesTest, BalanceAlwaysEqualsTheLedger)
{
  using enum FinanceCategory;
  Finances finances(1'000'000, DAY_ONE);
  finances.record(DAY_TWO, Wages, -250'000);
  finances.record(DAY_TWO, Matchday, 400'000);
  finances.record(DAY_THREE, Broadcasting, 90'000);

  EXPECT_EQ(finances.getBalance(), 1'240'000);
  EXPECT_EQ(finances.ledgerTotal(), finances.getBalance());
  ASSERT_EQ(finances.getLedger().size(), 4u);
  EXPECT_EQ(finances.getLedger().front().category, OpeningBalance);

  EXPECT_TRUE(finances.revertLastTransaction());
  EXPECT_EQ(finances.getBalance(), 1'150'000);
  EXPECT_EQ(finances.ledgerTotal(), finances.getBalance());
}

TEST(FinancesTest, SummaryTotalsOnlyTheRequestedDates)
{
  using enum FinanceCategory;
  Finances finances(0, DAY_ONE);
  finances.record(DAY_TWO, Wages, -100);
  finances.record(DAY_TWO, Sponsorship, 300);
  finances.record(DAY_THREE, Staff, -50);

  const FinanceSummary july =
      finances.summarize(GameDateValue(2025, 7, 3), GameDateValue(2025, 7, 31));
  EXPECT_EQ(july.income, 300);
  EXPECT_EQ(july.expenses, 100);
  EXPECT_EQ(july.net(), 200);
  EXPECT_EQ(july.by_category[static_cast<std::size_t>(Wages)], -100);
  EXPECT_EQ(july.by_category[static_cast<std::size_t>(Staff)], 0);
}

TEST(FinancesTest, TransferAndWageBudgetsAreSeparate)
{
  Finances finances(50'000'000, DAY_ONE);
  finances.setTransferBudget(10'000'000);
  finances.setWageBudget(200'000);

  finances.record(DAY_TWO, FinanceCategory::TransferFeeOut, -4'000'000);
  EXPECT_EQ(finances.getTransferBudget(), 6'000'000);
  EXPECT_EQ(finances.getWageBudget(), 200'000);

  // Half of a sale is reinvested in the transfer budget.
  finances.record(DAY_TWO, FinanceCategory::TransferFeeIn, 2'000'000);
  EXPECT_EQ(finances.getTransferBudget(), 7'000'000);

  // Wages do not touch the transfer budget.
  finances.record(DAY_TWO, FinanceCategory::Wages, -150'000);
  EXPECT_EQ(finances.getTransferBudget(), 7'000'000);

  // 5.2M of transfer budget buys 100k of weekly wage room.
  EXPECT_TRUE(finances.moveTransferToWageBudget(5'200'000, 150'000));
  EXPECT_EQ(finances.getTransferBudget(), 1'800'000);
  EXPECT_EQ(finances.getWageBudget(), 300'000);
  // Cannot overdraw the transfer budget or undercut the payroll.
  EXPECT_FALSE(finances.moveTransferToWageBudget(5'200'000, 150'000));
  EXPECT_FALSE(finances.moveTransferToWageBudget(-10'400'000, 150'000));
  EXPECT_EQ(finances.ledgerTotal(), finances.getBalance());
}

TEST(FinancesTest, LedgerRoundTripsThroughTheDatabase)
{
  Logger::init();
  auto db = std::make_shared<DatabaseConnection>(":memory:");
  db->initialize();

  Team team(7, 1, "Ledger FC", 5'000'000);
  team.getFinances().record(DAY_TWO, FinanceCategory::Wages, -120'000);
  team.getFinances().record(DAY_THREE, FinanceCategory::PrizeMoney, 800'000);
  TeamRepository team_repo(db);
  team_repo.insertTeamWithId(team);
  team_repo.updateTeamState(team);
  // Flushing twice before markPersisted() must not duplicate rows.
  team_repo.updateTeamState(team);

  const auto ledgers = FinanceRepository(db).loadAll();
  const auto stored = ledgers.find(team.getId());
  ASSERT_NE(stored, ledgers.end());
  ASSERT_EQ(stored->second.size(), team.getFinances().getLedger().size());
  for (std::size_t i = 0; i < stored->second.size(); ++i)
  {
    EXPECT_EQ(stored->second[i].amount,
              team.getFinances().getLedger()[i].amount);
    EXPECT_EQ(stored->second[i].category,
              team.getFinances().getLedger()[i].category);
    EXPECT_EQ(stored->second[i].date, team.getFinances().getLedger()[i].date);
  }

  Finances restored(0);
  restored.restoreLedger(stored->second);
  EXPECT_EQ(restored.getBalance(), team.getFinances().getBalance());
  EXPECT_TRUE(restored.pendingTransactions().empty());
}

TEST(ClubEconomyTest, PrizeMoneyRewardsHigherPlaces)
{
  const LeagueEconomy economy =
      makeLeagueEconomy(3, std::vector<std::uint8_t>(20, 90));
  const auto prizes = ClubEconomy::prizeMoney(economy, 20);
  ASSERT_EQ(prizes.size(), 20u);
  EXPECT_TRUE(std::ranges::is_sorted(prizes, std::greater<>{}));
  EXPECT_GT(prizes.back(), 0);

  const auto revenue =
      static_cast<double>(economy.profile->average_revenue_eur);
  // Only the league's merit pool: continental money is paid by the
  // competitions as it is earned.
  const double expected_pool =
      revenue * 20.0 * static_cast<double>(economy.profile->tv_share) *
      (1.0 - static_cast<double>(WorldTuning::Finance::TV_EQUAL_SHARE));
  const auto total = static_cast<double>(
      std::accumulate(prizes.begin(), prizes.end(), std::int64_t{0}));
  EXPECT_NEAR(total, expected_pool, expected_pool * 0.001);
}

TEST(ClubEconomyTest, ContinentalPrizeMoneyIsPaidOnlyByTheCompetitions)
{
  Logger::init();
  const int slot = financeSlot() + 2;
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot, WORLD_SEED);
  auto gamedata = controller->getGameData();
  WorldSimulation world(gamedata);

  std::map<TeamID, std::size_t> ledger_sizes;
  for (const auto& [team_id, team] : gamedata->getTeams())
    ledger_sizes[team_id] = team.getFinances().getLedger().size();
  const GameDateValue season_end(2026, 7, 1);
  world.onSeasonEnd(season_end, FREE_AGENTS_TEAM_ID);

  // The season-end award pays each league's merit pool as broadcasting
  // money and never an estimate of continental prizes on top of what the
  // continental competitions pay.
  const auto economies = buildLeagueEconomies(*gamedata);
  std::map<LeagueID, std::int64_t> merit_paid;
  for (const auto& [team_id, team] : gamedata->getTeams())
  {
    const auto& ledger = team.getFinances().getLedger();
    for (std::size_t i = ledger_sizes[team_id]; i < ledger.size(); ++i)
    {
      EXPECT_NE(ledger[i].category, FinanceCategory::PrizeMoney)
          << team.getName();
      if (ledger[i].category == FinanceCategory::Broadcasting)
        merit_paid[team.getLeagueId()] += ledger[i].amount;
    }
  }
  ASSERT_FALSE(merit_paid.empty());
  for (const auto& [league_id, paid] : merit_paid)
  {
    const auto economy = economies.find(league_id);
    ASSERT_NE(economy, economies.end());
    const auto prizes = ClubEconomy::prizeMoney(
        economy->second,
        gamedata->getLeagues().at(league_id).getTeamIDs().size());
    EXPECT_EQ(paid,
              std::accumulate(prizes.begin(), prizes.end(), std::int64_t{0}));
  }
  controller.reset();
  RuntimePaths::removeSave(slot);
}

TEST(ClubEconomyTest, AttendanceFollowsSuccessPriceAndCapacity)
{
  const LeagueEconomy economy = makeLeagueEconomy(1, {70, 75, 80, 85, 90, 95});
  ClubProfile profile;
  profile.reputation = 80;
  profile.stadium_capacity = 40'000;
  profile.ticket_price = static_cast<std::uint32_t>(
      ClubEconomy::fairTicketPrice(economy, profile));

  const auto neutral =
      ClubEconomy::attendance(economy, profile, 0.0, 80, MatchType::LEAGUE);
  const auto winning =
      ClubEconomy::attendance(economy, profile, 1.0, 80, MatchType::LEAGUE);
  const auto losing =
      ClubEconomy::attendance(economy, profile, -1.0, 80, MatchType::LEAGUE);
  EXPECT_GE(winning, neutral);
  EXPECT_LT(losing, neutral);
  EXPECT_LE(winning, profile.stadium_capacity);

  ClubProfile expensive = profile;
  expensive.ticket_price *= 3;
  EXPECT_LT(
      ClubEconomy::attendance(economy, expensive, -1.0, 80, MatchType::LEAGUE),
      losing);
  EXPECT_LT(
      ClubEconomy::attendance(economy, profile, 0.0, 80, MatchType::FRIENDLY),
      neutral);

  // Gate income peaks a little above the reference price; far above it the
  // ground empties.
  const auto gate = [&](double ratio)
  {
    ClubProfile priced = profile;
    priced.ticket_price = static_cast<std::uint32_t>(
        std::lround(ratio * static_cast<double>(profile.ticket_price)));
    return static_cast<double>(ClubEconomy::attendance(economy, priced, 0.0, 80,
                                                       MatchType::LEAGUE)) *
           static_cast<double>(priced.ticket_price);
  };
  double best_ratio = 0.0;
  double best_gate = 0.0;
  for (int sampleIndex = 0; sampleIndex <= 70; ++sampleIndex)
  {
    const double ratio = 0.5 + static_cast<double>(sampleIndex) * 0.05;
    if (gate(ratio) > best_gate)
    {
      best_gate = gate(ratio);
      best_ratio = ratio;
    }
  }
  EXPECT_GT(best_ratio, 1.0);
  EXPECT_LT(best_ratio, 1.8);
  EXPECT_LT(gate(3.0), 0.5 * gate(1.0));
  EXPECT_LT(gate(10.0), 0.01 * gate(1.0));

  // At the reference price a season of home games meets the gate target.
  const double season_gate = static_cast<double>(neutral) *
                             static_cast<double>(profile.ticket_price) *
                             WorldTuning::Finance::HOME_MATCHES_PER_SEASON;
  const double target =
      ClubEconomy::expectedRevenue(economy, profile.reputation) *
      static_cast<double>(economy.profile->gate_share);
  EXPECT_NEAR(season_gate, target, target * 0.05);
}

class FinanceWorldTest : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    Logger::init();
    controller = std::make_unique<GameController>();
    controller->newGame(financeSlot(), WORLD_SEED);
  }

  void TearDown() override
  {
    controller.reset();
    RuntimePaths::removeSave(financeSlot());
  }

  std::unique_ptr<GameController> controller;
};

TEST_F(FinanceWorldTest, LedgersReconcileThroughMonthsOfPlayAndReload)
{
  const TeamID managed = controller->getTeams().front().get().getId();
  controller->selectManagedTeam(managed);
  for (int day = 0; day < 40; ++day) controller->advanceDay();

  auto gamedata = controller->getGameData();
  std::size_t clubs_with_gate = 0;
  for (const auto& team_ref : controller->getTeams())
  {
    const Finances& finances = team_ref.get().getFinances();
    ASSERT_EQ(finances.getBalance(), finances.ledgerTotal())
        << team_ref.get().getName();
    EXPECT_LT(categoryTotal(finances, FinanceCategory::Wages), 0);
    EXPECT_GT(categoryTotal(finances, FinanceCategory::Broadcasting), 0);
    EXPECT_GT(categoryTotal(finances, FinanceCategory::Sponsorship), 0);
    EXPECT_LT(categoryTotal(finances, FinanceCategory::Staff), 0);
    if (categoryTotal(finances, FinanceCategory::Matchday) > 0)
      ++clubs_with_gate;
  }
  EXPECT_GT(clubs_with_gate, 0u) << "home friendlies must earn gate receipts";

  const Finances& managed_finances =
      controller->getManagedTeam()->get().getFinances();
  const auto ledger_size = managed_finances.getLedger().size();
  const auto balance = managed_finances.getBalance();
  const auto transfer_budget = managed_finances.getTransferBudget();
  const auto wage_budget = managed_finances.getWageBudget();
  controller->saveGame();

  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(financeSlot()));
  const Finances& reloaded = controller->getManagedTeam()->get().getFinances();
  EXPECT_EQ(reloaded.getLedger().size(), ledger_size);
  EXPECT_EQ(reloaded.getBalance(), balance);
  EXPECT_EQ(reloaded.ledgerTotal(), balance);
  EXPECT_EQ(reloaded.getTransferBudget(), transfer_budget);
  EXPECT_EQ(reloaded.getWageBudget(), wage_budget);
}

// Relegated clubs keep a falling share of the top division's equal TV money
// for two seasons, then nothing.
TEST(ClubEconomyTest, ParachutesRunForTwoSeasons)
{
  using Finance = WorldTuning::Finance;
  const LeagueEconomy top = makeLeagueEconomy(3, {60, 70, 80, 90});
  const double equal_share = ClubEconomy::monthlyBroadcasting(top) * 12.0;
  ASSERT_GT(equal_share, 0.0);
  const double first = ClubEconomy::parachutePayment(top, 0);
  const double second = ClubEconomy::parachutePayment(top, 1);
  EXPECT_NEAR(first,
              equal_share * static_cast<double>(Finance::PARACHUTE_SHARES[0]),
              1.0);
  EXPECT_GT(first, second);
  EXPECT_GT(second, 0.0);
  EXPECT_EQ(ClubEconomy::parachutePayment(top, 2), 0.0);
  EXPECT_EQ(ClubEconomy::parachutePayment(top, -1), 0.0);
}

TEST(ClubEconomyTest, OwnerRescueRestoresACushion)
{
  using Finance = WorldTuning::Finance;
  constexpr std::int64_t PAYROLL = 100'000;
  // Small overdrafts are the club's own business.
  EXPECT_EQ(ClubEconomy::ownerRescue(1'000'000, PAYROLL), 0);
  EXPECT_EQ(ClubEconomy::ownerRescue(
                -Finance::OWNER_RESCUE_TRIGGER_WEEKS * PAYROLL, PAYROLL),
            0);
  // Deep in the red the owner restores the cushion.
  const std::int64_t balance = -10 * PAYROLL;
  EXPECT_EQ(balance + ClubEconomy::ownerRescue(balance, PAYROLL),
            Finance::OWNER_RESCUE_CUSHION_WEEKS * PAYROLL);
  EXPECT_EQ(ClubEconomy::ownerRescue(balance, 0), 0);
}

// Benefactors recapitalise their AI clubs at the next monthly review;
// member-owned clubs have nobody to call.
TEST_F(FinanceWorldTest, AiOwnersRescueClubsDeepInTheRed)
{
  auto gamedata = controller->getGameData();
  TeamID benefactor = FREE_AGENTS_TEAM_ID;
  TeamID members = FREE_AGENTS_TEAM_ID;
  for (const auto& team_ref : controller->getTeams())
  {
    const Team& team = team_ref.get();
    const OwnerType owner =
        ManagerMarketModel::clubVision(gamedata->getWorldSeed(), team.getId(),
                                       team.getReputation(),
                                       team.getProfile().youth_facilities, 0, 0)
            .owner;
    if (owner == OwnerType::ImpatientBenefactor &&
        benefactor == FREE_AGENTS_TEAM_ID)
      benefactor = team.getId();
    if (owner == OwnerType::MemberOwned && members == FREE_AGENTS_TEAM_ID)
      members = team.getId();
  }
  ASSERT_NE(benefactor, FREE_AGENTS_TEAM_ID);
  ASSERT_NE(members, FREE_AGENTS_TEAM_ID);
  for (const TeamID team_id : {benefactor, members})
  {
    Finances& finances = gamedata->getTeams().at(team_id).getFinances();
    finances.record(controller->getCurrentDate(),
                    FinanceCategory::TransferFeeOut,
                    -finances.getBalance() - 500'000'000);
  }
  while (!(controller->getCurrentDate().day == 2 &&
           controller->getCurrentDate().month == 8))
    controller->advanceDay();

  const Finances& rescued = gamedata->getTeams().at(benefactor).getFinances();
  // Revenues before the review can reduce the original debt. The owner
  // injects enough to rescue the balance at review time, not the old debt.
  EXPECT_GT(categoryTotal(rescued, FinanceCategory::Investment), 0);
  EXPECT_GT(rescued.getBalance(), 0);
  const Finances& unrescued = gamedata->getTeams().at(members).getFinances();
  EXPECT_EQ(categoryTotal(unrescued, FinanceCategory::Investment), 0);
  EXPECT_LT(unrescued.getBalance(), 0);
}

TEST_F(FinanceWorldTest, TransferFeesAreRecordedForBothClubs)
{
  const auto& teams = controller->getTeams();
  ASSERT_GE(teams.size(), 2u);
  const TeamID buyer_id = teams[0].get().getId();
  const TeamID seller_id = teams[1].get().getId();
  const PlayerID player_id =
      controller->getPlayersForTeam(seller_id).front().get().getId();
  auto gamedata = controller->getGameData();
  gamedata->getTeams().at(buyer_id).getFinances().addBalance(100'000'000);

  const std::int64_t buyer_budget =
      gamedata->getTeams().at(buyer_id).getFinances().getTransferBudget();
  controller->listPlayerForTransfer(player_id, 1'000'000);
  ASSERT_TRUE(controller->buyPlayer(player_id, buyer_id, 1'000'000));

  const Finances& buyer = gamedata->getTeams().at(buyer_id).getFinances();
  const Finances& seller = gamedata->getTeams().at(seller_id).getFinances();
  EXPECT_EQ(buyer.getLedger().back().category, FinanceCategory::TransferFeeOut);
  EXPECT_EQ(buyer.getLedger().back().amount, -1'000'000);
  EXPECT_EQ(seller.getLedger().back().category, FinanceCategory::TransferFeeIn);
  EXPECT_EQ(seller.getLedger().back().amount, 1'000'000);
  EXPECT_EQ(buyer.getTransferBudget(), buyer_budget - 1'000'000);
  EXPECT_EQ(buyer.getBalance(), buyer.ledgerTotal());
  EXPECT_EQ(seller.getBalance(), seller.ledgerTotal());
}

// ---------------------------------------------------------------------------
// Economy calibration: a season of every league without transfers
// ---------------------------------------------------------------------------

namespace
{
int poissonGoals(WorldRng& rng, double lambda)
{
  const double limit = std::exp(-lambda);
  double product = rng.uniform01();
  int goals = 0;
  while (product > limit && goals < 9)
  {
    product *= rng.uniform01();
    ++goals;
  }
  return goals;
}

/** Plays one synthetic double round robin per league (weekly from August)
 * through the world simulation, with strength-driven scores. */
void playWorldSeason(const GameController& controller, WorldSimulation& world)
{
  auto gamedata = controller.getGameData();
  std::map<LeagueID, std::vector<TeamID>> leagues;
  for (const auto& [league_id, league] : gamedata->getLeagues())
  {
    std::vector<TeamID> teams = league.getTeamIDs();
    std::ranges::sort(teams);
    if (teams.size() % 2 == 1) teams.push_back(FREE_AGENTS_TEAM_ID);
    leagues.try_emplace(league_id, std::move(teams));
  }
  WorldRng scores(7);
  GameDateValue date = controller.getCurrentDate();
  int round = -1;
  for (int day = 0; day < 365; ++day)
  {
    date = date + 1;
    world.onDayAdvanced(date, FREE_AGENTS_TEAM_ID);
    if (date.month == 7 && date.day == 1)
    {
      world.onSeasonEnd(date, FREE_AGENTS_TEAM_ID);
      world.onSeasonStart(date, FREE_AGENTS_TEAM_ID);
    }
    if (const bool in_season = date.month >= 8 || date.month <= 5;
        !in_season || dayOrdinal(date) % 7 != 5 ||
        (date.month == 8 && date.day < 8))
      continue;
    ++round;
    for (auto& [league_id, teams] : leagues)
    {
      const std::size_t size = teams.size();
      if (round >= static_cast<int>(2 * (size - 1))) continue;
      // Circle method: the first club stays, the others rotate.
      std::vector<TeamID> order(teams.begin(), teams.end());
      std::rotate(order.begin() + 1,
                  order.begin() + 1 +
                      static_cast<std::ptrdiff_t>(
                          static_cast<std::size_t>(round) % (size - 1)),
                  order.end());
      for (std::size_t i = 0; i < size / 2; ++i)
      {
        TeamID home = order[i];
        TeamID away = order[size - 1 - i];
        if (home == FREE_AGENTS_TEAM_ID || away == FREE_AGENTS_TEAM_ID)
          continue;
        if ((round + static_cast<int>(i)) % 2 == 1) std::swap(home, away);
        const double diff =
            (world.lineupStrength(home) - world.lineupStrength(away)) / 12.0;
        const auto home_goals = static_cast<std::uint8_t>(
            poissonGoals(scores, 1.5 * std::exp(0.5 * diff)));
        const auto away_goals = static_cast<std::uint8_t>(
            poissonGoals(scores, 1.15 * std::exp(-0.5 * diff)));
        Match match(home, away, date, MatchType::LEAGUE, league_id);
        match.setPlayedResult(home_goals, away_goals);
        League& league = gamedata->getLeagues().at(league_id);
        if (home_goals > away_goals)
          league.addPoints(home, 3);
        else if (home_goals < away_goals)
          league.addPoints(away, 3);
        else
        {
          league.addPoints(home, 1);
          league.addPoints(away, 1);
        }
        MatchReport report;
        world.onMatchPlayed(match, report, FREE_AGENTS_TEAM_ID);
      }
    }
  }
}

double median(std::vector<double> values)
{
  std::ranges::sort(values);
  return values.empty() ? 0.0 : values[values.size() / 2];
}
}  // namespace

// UEFA ECFIL: wages 57-73% of revenue by league (lower tiers overshoot),
// about half of the clubs make a profit, insolvency is rare. Without transfer
// activity a median club should roughly break even, rich clubs can profit and
// few clubs run out of cash within a season.
TEST(EconomyCalibrationTest, MedianClubsBreakEvenInEveryLeague)
{
  Logger::init();
  const int slot = financeSlot() + 1;
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot, WORLD_SEED);
  WorldSimulation world(controller->getGameData());
  playWorldSeason(*controller, world);

  struct LeagueResult
  {
    std::vector<double> revenue;
    std::vector<double> net_ratio;
    std::vector<double> wage_ratio;
    int negative_cash = 0;
  };
  std::map<LeagueID, LeagueResult> leagues;
  for (const auto& team : controller->getTeams())
  {
    const Finances& finances = team.get().getFinances();
    const FinanceSummary season = finances.summarize(GameDateValue(2025, 7, 3),
                                                     GameDateValue(2026, 7, 2));
    const auto category = [&](FinanceCategory value)
    {
      return static_cast<double>(season.by_category[static_cast<std::size_t>(
          std::to_underlying(value))]);
    };
    const double revenue = static_cast<double>(season.income) -
                           category(FinanceCategory::TransferFeeIn) -
                           category(FinanceCategory::Investment);
    const double net = static_cast<double>(season.net()) -
                       category(FinanceCategory::TransferFeeIn) -
                       category(FinanceCategory::TransferFeeOut) -
                       category(FinanceCategory::Investment);
    ASSERT_GT(revenue, 0.0) << team.get().getName();
    LeagueResult& result = leagues[team.get().getLeagueId()];
    result.revenue.push_back(revenue);
    result.net_ratio.push_back(net / revenue);
    result.wage_ratio.push_back(-category(FinanceCategory::Wages) / revenue);
    if (finances.getBalance() < 0) ++result.negative_cash;
  }
  for (const auto& [league_id, result] : leagues)
  {
    const auto clubs = static_cast<double>(result.revenue.size());
    const double median_net = median(result.net_ratio);
    std::cout << "[economy] league " << static_cast<int>(league_id)
              << " median revenue " << median(result.revenue) / 1e6
              << "M median net " << 100.0 * median_net << "% best net "
              << 100.0 * std::ranges::max(result.net_ratio) << "% worst net "
              << 100.0 * std::ranges::min(result.net_ratio)
              << "% median player wages " << 100.0 * median(result.wage_ratio)
              << "% negative cash " << result.negative_cash << "/" << clubs
              << "\n";
    EXPECT_LT(std::abs(median_net), 0.15) << "league " << league_id;
    EXPECT_GE(median(result.wage_ratio), 0.45) << "league " << league_id;
    EXPECT_LE(median(result.wage_ratio), 0.78) << "league " << league_id;
    EXPECT_LT(static_cast<double>(result.negative_cash), 0.10 * clubs)
        << "league " << league_id;
  }
  controller.reset();
  RuntimePaths::removeSave(slot);
}
