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
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <latch>
#include <limits>
#include <memory>
#include <thread>
#include <utility>

#include "controller/game_controller.h"
#include "database/database_connection.h"
#include "database/gamedata.h"
#include "database/repositories/competition_repository.h"
#include "global/logger.h"
#include "model/buyer_negotiation.h"
#include "model/calendar.h"
#include "model/competition_manager.h"
#include "model/interactions.h"
#include "model/season_history.h"
#include "model/transfer_listing.h"
#include "model/transfer_market.h"
#include "model/transfer_tuning.h"
#include "model/transfer_windows.h"
#include "model/world_rng.h"
#include "model/world_simulation.h"
#include "model/world_tuning.h"

class TransferMarketTest : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    Logger::init();
    controller = std::make_unique<GameController>();
    controller->newGame(0);
    controller->selectManagedTeam(1);  // Select a dummy team for testing
  }

  void TearDown() override { controller.reset(); }

  std::unique_ptr<GameController> controller;
};

TEST_F(TransferMarketTest, BidOnPlayer)
{
  auto all_teams = controller->getTeams();
  ASSERT_FALSE(all_teams.empty());

  TeamID managed_team_id = all_teams.front().get().getId();
  controller->selectManagedTeam(managed_team_id);

  TeamID test_team_id = all_teams.back().get().getId();
  if (test_team_id == managed_team_id)
  {
    test_team_id = all_teams[1].get().getId();
  }

  auto team_players = controller->getPlayersForTeam(test_team_id);
  ASSERT_FALSE(team_players.empty());
  PlayerID test_player_id = team_players.front().get().getId();

  controller->listPlayerForTransfer(test_player_id, 10);

  auto listings = controller->getAllListings();
  ASSERT_FALSE(listings.empty());

  auto first_listing = listings.begin()->second;
  auto player_id = first_listing.player_id;

  int manager_team_id = controller->getManagedTeam()->get().getId();

  auto gamedata = controller->getGameData();
  gamedata->getTeams()
      .at(manager_team_id)
      .getFinances()
      .addBalance(1000000000LL);

  // Make a bid
  long long bid_amount = first_listing.asking_price + 1000;
  bool success = controller->submitBid(player_id, manager_team_id, bid_amount);

  EXPECT_TRUE(success);

  const auto otherBuyer =
      std::ranges::find_if(all_teams,
                           [manager_team_id, test_team_id](const auto& team)
                           {
                             return team.get().getId() != manager_team_id &&
                                    team.get().getId() != test_team_id;
                           });
  ASSERT_NE(otherBuyer, all_teams.end());
  gamedata->getTeams()
      .at(otherBuyer->get().getId())
      .getFinances()
      .addBalance(1000000000LL);
  EXPECT_FALSE(controller->buyPlayer(player_id, otherBuyer->get().getId(),
                                     static_cast<uint32_t>(bid_amount)))
      << "A club must not reuse another club's winning bid";

  ASSERT_TRUE(controller->counterOffer(
      player_id, static_cast<uint32_t>(bid_amount + 500)));
  const auto countered = controller->getAllListings().find(player_id);
  ASSERT_NE(countered, controller->getAllListings().end());
  EXPECT_FALSE(countered->second.highest_bidder_id.has_value());
  EXPECT_EQ(countered->second.highest_bid, 0u);
}

TEST_F(TransferMarketTest, AIEvaluatesAndAcceptsBid)
{
  auto all_teams = controller->getTeams();
  ASSERT_GE(all_teams.size(), 2u);

  TeamID buyer_team_id = all_teams[0].get().getId();
  TeamID seller_team_id = all_teams[1].get().getId();

  auto seller_players = controller->getPlayersForTeam(seller_team_id);
  ASSERT_FALSE(seller_players.empty());
  // An outfield player: computer-managed clubs keep their goalkeepers.
  const auto outfield = std::ranges::find_if(
      seller_players, [](const auto& player)
      { return player.get().getRole() != PlayerRole::GK; });
  ASSERT_NE(outfield, seller_players.end());
  PlayerID test_player_id = outfield->get().getId();

  controller->listPlayerForTransfer(test_player_id, 10000);

  auto gamedata = controller->getGameData();
  gamedata->getTeams().at(buyer_team_id).getFinances().addBalance(10000000LL);

  bool bid_submitted =
      controller->submitBid(test_player_id, buyer_team_id, 15000);
  EXPECT_TRUE(bid_submitted);

  // Advance day to trigger AI activity and bid processing
  controller->advanceDay();

  // The transfer should have been completed by the AI accepting the bid
  EXPECT_FALSE(controller->isPlayerListed(test_player_id));
}

TEST_F(TransferMarketTest, BidSurvivesSaveAndReload)
{
  const auto teams = controller->getTeams();
  ASSERT_GE(teams.size(), 2u);
  const TeamID buyerId = teams[0].get().getId();
  const TeamID sellerId = teams[1].get().getId();

  const auto sellerPlayers = controller->getPlayersForTeam(sellerId);
  const auto target = std::ranges::find_if(
      sellerPlayers, [this](const auto& player)
      { return !controller->isPlayerListed(player.get().getId()); });
  ASSERT_NE(target, sellerPlayers.end());
  const PlayerID playerId = target->get().getId();

  controller->getGameData()->getTeams().at(buyerId).getFinances().addBalance(
      1'000'000'000LL);
  controller->listPlayerForTransfer(playerId, 2'000'000);
  ASSERT_TRUE(controller->submitBid(playerId, buyerId, 2'100'000));
  controller->saveGame();

  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(0));
  const auto listing = controller->getAllListings().find(playerId);
  ASSERT_NE(listing, controller->getAllListings().end());
  ASSERT_TRUE(listing->second.highest_bidder_id.has_value());
  EXPECT_EQ(*listing->second.highest_bidder_id, buyerId);
  EXPECT_EQ(listing->second.highest_bid, 2'100'000u);
}

TEST_F(TransferMarketTest, AcceptedTransferAndBalancesSurviveReload)
{
  const auto teams = controller->getTeams();
  ASSERT_GE(teams.size(), 2u);
  const TeamID buyerId = teams[0].get().getId();
  const TeamID sellerId = teams[1].get().getId();

  const auto sellerPlayers = controller->getPlayersForTeam(sellerId);
  const auto target = std::ranges::find_if(
      sellerPlayers, [this](const auto& player)
      { return !controller->isPlayerListed(player.get().getId()); });
  ASSERT_NE(target, sellerPlayers.end());
  const PlayerID playerId = target->get().getId();

  auto gameData = controller->getGameData();
  gameData->getTeams().at(buyerId).getFinances().addBalance(1'000'000'000LL);
  controller->listPlayerForTransfer(playerId, 1'500'000);
  ASSERT_TRUE(controller->submitBid(playerId, buyerId, 1'600'000));
  ASSERT_TRUE(controller->acceptBid(playerId));
  const int64_t buyerBalance =
      gameData->getTeams().at(buyerId).getFinances().getBalance();
  const int64_t sellerBalance =
      gameData->getTeams().at(sellerId).getFinances().getBalance();
  controller->saveGame();

  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(0));
  gameData = controller->getGameData();
  const auto player = gameData->getPlayer(playerId);
  ASSERT_TRUE(player.has_value());
  EXPECT_EQ(player->get().getTeamId(), buyerId);
  EXPECT_EQ(gameData->getTeams().at(buyerId).getFinances().getBalance(),
            buyerBalance);
  EXPECT_EQ(gameData->getTeams().at(sellerId).getFinances().getBalance(),
            sellerBalance);
  EXPECT_FALSE(controller->isPlayerListed(playerId));
}

TEST_F(TransferMarketTest, ContractTermsAreNegotiatedAndPersisted)
{
  const auto teams = controller->getTeams();
  ASSERT_GE(teams.size(), 2u);
  const TeamID buyerId = teams[0].get().getId();
  const TeamID sellerId = teams[1].get().getId();
  const auto sellerPlayers = controller->getPlayersForTeam(sellerId);
  ASSERT_FALSE(sellerPlayers.empty());
  const PlayerID playerId = sellerPlayers.front().get().getId();
  constexpr uint32_t ASKING_PRICE = 1'000'000;

  auto gameData = controller->getGameData();
  gameData->getTeams().at(buyerId).getFinances().addBalance(1'000'000'000LL);
  controller->listPlayerForTransfer(playerId, ASKING_PRICE);

  const GameController::ContractTerms demand =
      controller->getContractDemand(playerId, false);
  ASSERT_GT(demand.weekly_wage, 0u);
  GameController::ContractTerms insufficient = demand;
  --insufficient.weekly_wage;
  EXPECT_FALSE(controller->buyPlayerWithContract(playerId, buyerId,
                                                 ASKING_PRICE, insufficient));
  EXPECT_EQ(gameData->getPlayer(playerId)->get().getTeamId(), sellerId);

  ASSERT_TRUE(controller->buyPlayerWithContract(playerId, buyerId, ASKING_PRICE,
                                                demand));
  EXPECT_EQ(gameData->getPlayer(playerId)->get().getWage(), demand.weekly_wage);
  EXPECT_EQ(gameData->getPlayer(playerId)->get().getContractYears(),
            demand.years);
  controller->saveGame();

  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(0));
  const auto reloadedPlayer = controller->getGameData()->getPlayer(playerId);
  ASSERT_TRUE(reloadedPlayer.has_value());
  EXPECT_EQ(reloadedPlayer->get().getTeamId(), buyerId);
  EXPECT_EQ(reloadedPlayer->get().getWage(), demand.weekly_wage);
  EXPECT_EQ(reloadedPlayer->get().getContractYears(), demand.years);
}

// ---------------------------------------------------------------------------
// Structured deals, loans, pre-contracts and the AI market
// ---------------------------------------------------------------------------

namespace
{
using TransferNegotiation::ClubResponse;
using TransferNegotiation::ContractKind;

const TransferMarket& marketOf(const GameController& controller)
{
  return controller.getGame()->getTransfers();
}

int64_t categoryTotal(const Finances& finances, FinanceCategory category)
{
  int64_t total = 0;
  for (const FinanceTransaction& transaction : finances.getLedger())
    if (transaction.category == category) total += transaction.amount;
  return total;
}

/** A player of another club who would join @p club and is not a key
 * player there (so the seller is willing to negotiate). */
PlayerID findWillingTarget(const GameController& controller, TeamID club,
                           SquadRole min_role = SquadRole::FirstTeam)
{
  const TransferMarket& market = marketOf(controller);
  for (const auto& team : controller.getTeams())
  {
    if (team.get().getId() == club) continue;
    for (const auto& player : controller.getPlayersForTeam(team.get().getId()))
    {
      const PlayerID id = player.get().getId();
      if (player.get().getAge() > 21 &&
          player.get().getRole() != PlayerRole::GK && market.canBeTraded(id) &&
          static_cast<int>(std::to_underlying(controller.getSquadRole(id))) >=
              static_cast<int>(std::to_underlying(min_role)) &&
          market.wouldJoin(id, club, ContractKind::Transfer))
        return id;
    }
  }
  return 0;
}

/** Manages the first generated club (team ids depend on the leagues). */
TeamID manageFirstClub(GameController& controller)
{
  const TeamID club = controller.getTeams().front().get().getId();
  controller.selectManagedTeam(club);
  return club;
}

/** Standalone market over the controller's world (state loaded from the
 * save) so tests can run its calendar without simulating matches. */
struct MarketHarness
{
  explicit MarketHarness(const GameController& controller)
      : world(controller.getGameData()),
        competitions(controller.getGameData(), controller.getDbConn()),
        market(controller.getGameData(), world, competitions)
  {
    market.load(controller.getDbConn());
  }
  WorldSimulation world;
  CompetitionManager competitions;
  TransferMarket market;
};
}  // namespace

TEST_F(TransferMarketTest, StructuredOfferContractTalksAndLedger)
{
  const TeamID managed = manageFirstClub(*controller);
  auto gamedata = controller->getGameData();
  gamedata->getTeams().at(managed).getFinances().addBalance(2'000'000'000LL);
  const PlayerID target = findWillingTarget(*controller, managed);
  ASSERT_NE(target, 0u);
  const TeamID seller = gamedata->getPlayer(target)->get().getTeamId();
  const uint32_t value = controller->getPlayerMarketValue(target);

  TransferNegotiation::OfferTerms terms;
  terms.fee = std::max(value * 3, 1'000'000u);
  terms.upfront_percent = 40;
  terms.instalment_years = 3;
  terms.appearance_bonus = 500'000;
  terms.appearance_target = 20;
  terms.sell_on_percent = 15;
  const ClubResponse club = controller->makeTransferOffer(target, terms);
  ASSERT_EQ(club.decision, ClubResponse::Decision::Accept);
  ASSERT_EQ(controller->getContractTalkKind(target), ContractKind::Transfer);

  const auto demand =
      controller->getPlayerDemand(target, ContractKind::Transfer);
  TransferNegotiation::ContractOffer lowball =
      TransferNegotiation::demandedOffer(demand);
  lowball.weekly_wage /= 3;
  const auto refused = controller->proposeContract(target, lowball);
  EXPECT_FALSE(refused.completed);
  EXPECT_FALSE(refused.response.accepted);
  EXPECT_EQ(refused.rounds_left,
            TransferTuning::Negotiation::MAX_PLAYER_ROUNDS - 1);

  const int64_t seller_before =
      gamedata->getTeams().at(seller).getFinances().getBalance();
  const auto offer = TransferNegotiation::demandedOffer(demand);
  const auto agreed = controller->proposeContract(target, offer);
  ASSERT_TRUE(agreed.completed);
  EXPECT_EQ(gamedata->getPlayer(target)->get().getTeamId(), managed);
  EXPECT_EQ(gamedata->getPlayer(target)->get().getWage(), offer.weekly_wage);

  const uint32_t upfront = TransferNegotiation::upfrontAmount(terms);
  EXPECT_EQ(gamedata->getTeams().at(seller).getFinances().getBalance() -
                seller_before,
            static_cast<int64_t>(upfront));
  const auto& ledger =
      gamedata->getTeams().at(managed).getFinances().getLedger();
  const bool paid_upfront = std::ranges::any_of(
      ledger,
      [&](const FinanceTransaction& t)
      {
        return t.category == FinanceCategory::TransferFeeOut &&
               t.amount == -static_cast<int64_t>(upfront);
      });
  const bool paid_agent = std::ranges::any_of(
      ledger,
      [&](const FinanceTransaction& t)
      {
        return t.category == FinanceCategory::TransferFeeOut &&
               t.amount == -static_cast<int64_t>(terms.fee / 10);
      });
  EXPECT_TRUE(paid_upfront);
  EXPECT_TRUE(paid_agent) << "agent fee is ~10% of the fee";

  const auto count_kind = [&](const GameController& c, ObligationKind kind)
  {
    return std::ranges::count_if(
        marketOf(c).obligations(), [&](const TransferObligation& o)
        { return o.player_id == target && o.kind == kind; });
  };
  EXPECT_EQ(count_kind(*controller, ObligationKind::Instalment), 3);
  EXPECT_EQ(count_kind(*controller, ObligationKind::AppearanceBonus), 1);
  EXPECT_EQ(count_kind(*controller, ObligationKind::SellOn), 1);
  const auto history = marketOf(*controller).historyFor(target);
  ASSERT_FALSE(history.empty());
  EXPECT_EQ(history.back().kind, TransferKind::Permanent);
  EXPECT_EQ(history.back().fee, terms.fee);

  controller->saveGame();
  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(0));
  EXPECT_EQ(count_kind(*controller, ObligationKind::Instalment), 3);
  EXPECT_EQ(count_kind(*controller, ObligationKind::SellOn), 1);
  EXPECT_EQ(marketOf(*controller).historyFor(target).size(), history.size());
  const auto instalments = TransferNegotiation::instalmentAmounts(terms);
  EXPECT_EQ(marketOf(*controller)
                .committedPayables(managed, GameDateValue(2026, 7, 3)),
            static_cast<int64_t>(instalments.front()))
      << "next season's instalment is reserved from next season's budget";

  // Queries for different seasons must not overwrite one another's totals.
  const auto& market = marketOf(*controller);
  const std::array dates = {GameDateValue(2026, 7, 3),
                            GameDateValue(2027, 7, 3)};
  const std::array expected = {market.committedPayables(managed, dates[0]),
                               market.committedPayables(managed, dates[1])};
  std::atomic<bool> consistent{true};
  std::latch start(8);
  std::vector<std::jthread> readers;
  for (std::size_t thread = 0; thread < 8; ++thread)
    readers.emplace_back(
        [&, index = thread % dates.size()]
        {
          start.arrive_and_wait();
          for (int attempt = 0; attempt < 100; ++attempt)
            if (market.committedPayables(managed, dates[index]) !=
                expected[index])
              consistent.store(false);
        });
  readers.clear();
  EXPECT_TRUE(consistent.load());
}

TEST_F(TransferMarketTest, InstalmentsAddOnsAndSellOnsArePaid)
{
  controller->saveGame();
  MarketHarness harness(*controller);
  TransferMarket& market = harness.market;
  auto gamedata = controller->getGameData();
  const auto& teams = controller->getTeams();
  const TeamID a = teams[2].get().getId();
  const TeamID b = teams[3].get().getId();
  const TeamID c = teams[4].get().getId();
  const PlayerID player = controller->getPlayersForTeam(a).back().get().getId();
  const GameDateValue signed_on(2025, 7, 10);

  TransferMarket::Deal deal;
  deal.player_id = player;
  deal.buyer_id = b;
  deal.terms.fee = 9'000'000;
  deal.terms.upfront_percent = 40;
  deal.terms.instalment_years = 2;
  deal.terms.goal_bonus = 300'000;
  deal.terms.goal_target = 10;
  deal.terms.sell_on_percent = 20;
  deal.contract.weekly_wage = 5'000;
  deal.contract.years = 3;
  ASSERT_TRUE(market.completeTransfer(deal, signed_on, FREE_AGENTS_TEAM_ID));

  const int64_t a_before =
      gamedata->getTeams().at(a).getFinances().getBalance();
  const int64_t b_before =
      gamedata->getTeams().at(b).getFinances().getBalance();
  market.onDayAdvanced(signed_on + 365, FREE_AGENTS_TEAM_ID);
  EXPECT_EQ(gamedata->getTeams().at(a).getFinances().getBalance() - a_before,
            2'700'000);
  EXPECT_EQ(b_before - gamedata->getTeams().at(b).getFinances().getBalance(),
            2'700'000);

  // The goal add-on lapses once the player leaves the buyer; the sell-on
  // pays 20% of the next fee to the first club.
  TransferMarket::Deal resale = deal;
  resale.buyer_id = c;
  resale.terms = TransferNegotiation::OfferTerms{};
  resale.terms.fee = 5'000'000;
  const int64_t a_mid = gamedata->getTeams().at(a).getFinances().getBalance();
  const int64_t b_mid = gamedata->getTeams().at(b).getFinances().getBalance();
  ASSERT_TRUE(market.completeTransfer(resale, GameDateValue(2026, 8, 1),
                                      FREE_AGENTS_TEAM_ID));
  EXPECT_EQ(gamedata->getTeams().at(a).getFinances().getBalance() - a_mid,
            1'000'000);
  EXPECT_EQ(gamedata->getTeams().at(b).getFinances().getBalance() - b_mid,
            4'000'000);
  EXPECT_FALSE(std::ranges::any_of(
      market.obligations(), [&](const TransferObligation& o)
      { return o.kind == ObligationKind::SellOn && o.payee == a; }));
  market.onDayAdvanced(GameDateValue(2026, 8, 2), FREE_AGENTS_TEAM_ID);
  for (const auto& team : controller->getTeams())
    EXPECT_EQ(team.get().getFinances().getBalance(),
              team.get().getFinances().ledgerTotal());
}

TEST_F(TransferMarketTest, LoanOfferLifecycleSurvivesReload)
{
  const TeamID managed = manageFirstClub(*controller);
  auto gamedata = controller->getGameData();
  gamedata->getTeams().at(managed).getFinances().addBalance(100'000'000LL);
  const TransferMarket& market = marketOf(*controller);

  PlayerID target = 0;
  for (const auto& team : controller->getTeams())
  {
    if (team.get().getId() == managed || target != 0) continue;
    for (const auto& player : controller->getPlayersForTeam(team.get().getId()))
    {
      const PlayerID id = player.get().getId();
      std::vector<TransferNegotiation::Reason> reasons;
      const SquadRole role = controller->getSquadRole(id);
      if (static_cast<int>(std::to_underlying(role)) >=
              static_cast<int>(std::to_underlying(SquadRole::Backup)) &&
          market.canBeTraded(id) &&
          TransferNegotiation::playerAcceptsLoan(
              role, market.projectedRole(id, managed),
              player.get().getTraits().ambition,
              team.get().getReputation() -
                  controller->getManagedTeam()->get().getReputation(),
              reasons))
      {
        target = id;
        break;
      }
    }
  }
  ASSERT_NE(target, 0u);
  const TeamID parent = gamedata->getPlayer(target)->get().getTeamId();
  const uint32_t full_wage = gamedata->getPlayer(target)->get().getWage();

  TransferNegotiation::LoanTerms terms;
  terms.wage_share = 20;
  const ClubResponse counter = controller->makeLoanOffer(target, terms);
  ASSERT_EQ(counter.decision, ClubResponse::Decision::Counter)
      << (counter.reasons.empty()
              ? "none"
              : TransferNegotiation::reasonKey(counter.reasons.front()));
  terms.wage_share = counter.counter_wage_share;
  terms.recall_clause = true;
  const ClubResponse accepted = controller->makeLoanOffer(target, terms);
  ASSERT_EQ(accepted.decision, ClubResponse::Decision::Accept);
  EXPECT_EQ(gamedata->getPlayer(target)->get().getTeamId(), managed);
  EXPECT_EQ(gamedata->getPlayer(target)->get().getWage(),
            full_wage * terms.wage_share / 100);
  ASSERT_NE(market.findLoan(target), nullptr);
  EXPECT_FALSE(market.canBeTraded(target)) << "no sub-loans or resale";
  controller->listPlayerForTransfer(target, 1'000'000);
  EXPECT_FALSE(controller->isPlayerListed(target));
  EXPECT_FALSE(controller->releasePlayer(target));

  controller->saveGame();
  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(0));
  const LoanDeal* loan = marketOf(*controller).findLoan(target);
  ASSERT_NE(loan, nullptr);
  EXPECT_EQ(loan->parent, parent);
  EXPECT_EQ(loan->end, GameDateValue(2026, 6, 30));
  EXPECT_TRUE(loan->recall_clause);

  // The parent pays its wage share weekly; the player returns on 30 June.
  MarketHarness harness(*controller);
  gamedata = controller->getGameData();
  const int64_t parent_wages = categoryTotal(
      gamedata->getTeams().at(parent).getFinances(), FinanceCategory::Wages);
  GameDateValue date(2025, 7, 3);
  while (dayOrdinal(date) % 7 != 0) date = date + 1;
  harness.market.onDayAdvanced(date, managed);
  EXPECT_EQ(parent_wages -
                categoryTotal(gamedata->getTeams().at(parent).getFinances(),
                              FinanceCategory::Wages),
            static_cast<int64_t>(full_wage) * (100 - terms.wage_share) / 100);
  harness.market.onDayAdvanced(GameDateValue(2026, 6, 30), managed);
  EXPECT_EQ(gamedata->getPlayer(target)->get().getTeamId(), parent);
  EXPECT_EQ(gamedata->getPlayer(target)->get().getWage(), full_wage);
  EXPECT_EQ(harness.market.findLoan(target), nullptr);
  EXPECT_EQ(harness.market.historyFor(target).back().kind,
            TransferKind::LoanReturn);
}

TEST_F(TransferMarketTest, LoanCapsLimitLoansBetweenClubs)
{
  controller->saveGame();
  MarketHarness harness(*controller);
  const auto& teams = controller->getTeams();
  const TeamID parent = teams[5].get().getId();
  const TeamID borrower = teams[6].get().getId();
  std::vector<PlayerID> seniors;
  // Outfield players: the parent keeps its goalkeepers.
  for (const auto& player : controller->getPlayersForTeam(parent))
    if (player.get().getAge() > TransferTuning::Loan::EXEMPT_MAX_AGE &&
        player.get().getRole() != PlayerRole::GK)
      seniors.push_back(player.get().getId());
  ASSERT_GE(seniors.size(), 4u);
  TransferNegotiation::LoanTerms terms;
  const GameDateValue date(2025, 7, 20);
  for (int i = 0; i < TransferTuning::Loan::MAX_BETWEEN_CLUBS; ++i)
    ASSERT_TRUE(harness.market.startLoan(seniors[static_cast<size_t>(i)],
                                         borrower, terms, date,
                                         FREE_AGENTS_TEAM_ID));
  EXPECT_FALSE(harness.market.loanWithinLimits(seniors[3], parent, borrower));
  EXPECT_FALSE(harness.market.startLoan(seniors[3], borrower, terms, date,
                                        FREE_AGENTS_TEAM_ID));
  EXPECT_FALSE(harness.market.startLoan(seniors[0], teams[7].get().getId(),
                                        terms, date, FREE_AGENTS_TEAM_ID))
      << "a loanee cannot be loaned on";
}

TEST_F(TransferMarketTest, PreContractSignedInJanuaryCompletesOnFirstJuly)
{
  controller->saveGame();
  const TeamID managed = manageFirstClub(*controller);
  auto gamedata = controller->getGameData();
  const TeamID owner = controller->getTeams()[8].get().getId();
  const PlayerID player =
      controller->getPlayersForTeam(owner).front().get().getId();
  gamedata->getPlayers().at(player).setContractYears(2);

  TransferNegotiation::ContractOffer terms;
  terms.weekly_wage = 12'345;
  terms.years = 3;
  terms.signing_bonus = 50'000;
  terms.promised_role = SquadRole::FirstTeam;
  {
    MarketHarness harness(*controller);
    EXPECT_FALSE(harness.market.agreePreContract(
        player, managed, terms, GameDateValue(2026, 1, 5), managed))
        << "only players in their final six months";
    gamedata->getPlayers().at(player).setContractYears(1);
    EXPECT_FALSE(harness.market.agreePreContract(
        player, managed, terms, GameDateValue(2025, 12, 20), managed));
    ASSERT_TRUE(harness.market.agreePreContract(
        player, managed, terms, GameDateValue(2026, 1, 5), managed));
    EXPECT_FALSE(harness.market.canBeTraded(player));
    harness.market.save(controller->getDbConn());
  }
  MarketHarness reloaded(*controller);
  ASSERT_NE(reloaded.market.findPreContract(player), nullptr);
  reloaded.market.onDayAdvanced(GameDateValue(2026, 6, 30), managed);
  EXPECT_EQ(gamedata->getPlayer(player)->get().getTeamId(), owner);
  const int64_t balance =
      gamedata->getTeams().at(managed).getFinances().getBalance();
  reloaded.market.onDayAdvanced(GameDateValue(2026, 7, 1), managed);
  const Player& moved = gamedata->getPlayer(player)->get();
  EXPECT_EQ(moved.getTeamId(), managed);
  EXPECT_EQ(moved.getWage(), terms.weekly_wage);
  EXPECT_EQ(moved.getContractYears(), terms.years);
  EXPECT_EQ(reloaded.market.historyFor(player).back().kind,
            TransferKind::PreContract);
  // Signing bonus plus the agent's fee (weeks of the new wage), no fee.
  EXPECT_EQ(
      balance - gamedata->getTeams().at(managed).getFinances().getBalance(),
      50'000 + 12'345 * TransferTuning::Offer::FREE_AGENT_FEE_WEEKS);
  ASSERT_NE(reloaded.market.flags(player), nullptr);
  EXPECT_EQ(reloaded.market.flags(player)->promised_role, SquadRole::FirstTeam);
}

TEST_F(TransferMarketTest, ReleasedPlayerIsPaidOffAndBecomesFreeAgent)
{
  const TeamID managed = manageFirstClub(*controller);
  auto gamedata = controller->getGameData();
  const auto squad = controller->getPlayersForTeam(managed);
  const auto contracted = std::ranges::find_if(
      squad, [](const auto& p) { return p.get().getContractYears() >= 2; });
  ASSERT_NE(contracted, squad.end());
  const PlayerID player = contracted->get().getId();
  const int64_t cost = controller->getReleaseCost(player);
  EXPECT_EQ(cost, TransferNegotiation::severancePay(
                      contracted->get().getWage(),
                      contracted->get().getContractYears(),
                      controller->getCurrentDate()));
  ASSERT_GT(cost, 0);
  const int64_t balance =
      gamedata->getTeams().at(managed).getFinances().getBalance();
  ASSERT_TRUE(controller->releasePlayer(player));
  EXPECT_EQ(
      balance - gamedata->getTeams().at(managed).getFinances().getBalance(),
      cost);
  EXPECT_EQ(gamedata->getPlayer(player)->get().getTeamId(),
            FREE_AGENTS_TEAM_ID);
  EXPECT_EQ(gamedata->getPlayer(player)->get().getContractYears(), 0);
  EXPECT_EQ(marketOf(*controller).historyFor(player).back().kind,
            TransferKind::Release);

  // Free agents can be signed back; the player's patience is limited.
  ASSERT_EQ(controller->getContractTalkKind(player), ContractKind::FreeAgent);
  TransferNegotiation::ContractOffer insulting;
  insulting.weekly_wage = 1;
  insulting.years = 1;
  for (int round = 0; round < TransferTuning::Negotiation::MAX_PLAYER_ROUNDS;
       ++round)
    EXPECT_FALSE(controller->proposeContract(player, insulting).completed);
  EXPECT_EQ(controller->getContractRoundsLeft(player), 0);
  const auto ended = controller->proposeContract(
      player, TransferNegotiation::demandedOffer(controller->getPlayerDemand(
                  player, ContractKind::FreeAgent)));
  EXPECT_FALSE(ended.completed);
}

// ---------------------------------------------------------------------------
// Talks over the AI clubs' bids for the managed club's players
// ---------------------------------------------------------------------------

namespace
{
using OfferOutcome = GameController::OfferOutcome;

/** Manages the club with the lowest reputation, so bigger buyers exist. */
TeamID manageSmallClub(GameController& controller)
{
  // Among the clubs whose country's window is open today (early July some
  // countries, e.g. Brazil, have not opened theirs yet).
  TeamID club = 0;
  int lowest = std::numeric_limits<int>::max();
  for (const auto& team : controller.getTeams())
  {
    const TeamID id = team.get().getId();
    // Its country's transfer window must be open today.
    if (id != FREE_AGENTS_TEAM_ID && team.get().getReputation() < lowest &&
        controller.isTransferWindowOpenFor(id) &&
        !controller.getPlayersForTeam(id).empty() &&
        controller.isTransferWindowOpenFor(id))
    {
      lowest = team.get().getReputation();
      club = id;
    }
  }
  controller.selectManagedTeam(club);
  return club;
}

struct KeenOffer
{
  PlayerID player = 0;
  TeamID buyer = 0;
  std::uint32_t offer_id = 0;
};

/**
 * Stands in for an AI club's approach: a bid of @p fee (hidden ceiling
 * @p ceiling) from a clearly bigger, well funded club for a managed player
 * who wants the move, so personal terms never fail. The player is made
 * ambitious for the test.
 */
KeenOffer openKeenOffer(GameController& controller, TeamID managed,
                        std::uint32_t fee, std::uint32_t ceiling)
{
  auto data = controller.getGameData();
  TransferMarket& market = controller.getGame()->getTransfers();
  std::vector<TeamID> buyers;
  for (const auto& team : controller.getTeams())
    if (team.get().getId() != managed &&
        team.get().getId() != FREE_AGENTS_TEAM_ID &&
        controller.isTransferWindowOpenFor(team.get().getId()))
      buyers.push_back(team.get().getId());
  std::ranges::sort(buyers,
                    [&](TeamID a, TeamID b)
                    {
                      return controller.getTeamById(a)->get().getReputation() >
                             controller.getTeamById(b)->get().getReputation();
                    });
  const GameDateValue today = controller.getCurrentDate();
  // The best players are likeliest to be first-teamers at a bigger club.
  std::vector<PlayerID> candidates;
  for (const auto& reference : controller.getPlayersForTeam(managed))
    candidates.push_back(reference.get().getId());
  const StatsConfig& config = data->getStatsConfig();
  std::ranges::sort(candidates,
                    [&](PlayerID a, PlayerID b)
                    {
                      return data->getPlayer(a)->get().getOverall(config) >
                             data->getPlayer(b)->get().getOverall(config);
                    });
  for (const PlayerID candidate : candidates)
  {
    Player& player = data->getPlayers().at(candidate);
    if (!market.canBeTraded(player.getId())) continue;
    PlayerTraits traits = player.getTraits();
    traits.ambition = 90;
    traits.loyalty = 20;
    player.setTraits(traits);
    for (const TeamID buyer : buyers)
    {
      IncomingOffer offer;
      offer.player_id = player.getId();
      offer.buyer = buyer;
      offer.terms.fee = fee;
      offer.max_fee = ceiling;
      offer.patience = 3;
      offer.created = today;
      offer.expires = today + 5;
      const std::uint32_t id = market.addIncomingOffer(offer);
      if (const auto view = controller.getIncomingOfferView(id);
          view &&
          (view->stance == BuyerNegotiation::PlayerStance::WantsBiggerClub ||
           view->stance == BuyerNegotiation::PlayerStance::AskedToLeave))
      {
        data->getTeams().at(buyer).getFinances().addBalance(500'000'000LL);
        return {player.getId(), buyer, id};
      }
      market.removeIncomingOffer(id);
    }
  }
  return {};
}

/** Days until the buyer has answered the club's counter (at most three). */
void waitForAnswer(GameController& controller, std::uint32_t offer_id)
{
  for (int day = 0; day < 3; ++day)
  {
    if (const IncomingOffer* offer =
            controller.getGame()->getTransfers().findIncomingOffer(offer_id);
        offer == nullptr || offer->status != OfferStatus::AwaitingBuyer)
      return;
    controller.advanceDay();
  }
}
}  // namespace

TEST_F(TransferMarketTest, IncomingOfferIsNegotiatedOverDaysAndPaidUpfront)
{
  const TeamID managed = manageSmallClub(*controller);
  ASSERT_TRUE(controller->isTransferWindowOpen());
  const KeenOffer keen =
      openKeenOffer(*controller, managed, 1'000'000, 1'500'000);
  ASSERT_NE(keen.offer_id, 0u) << "no managed player keen on a bigger club";
  auto gamedata = controller->getGameData();
  const int age = gamedata->getPlayer(keen.player)->get().getAge();
  const double opening = BuyerNegotiation::buyerCost(
      controller->getIncomingOfferView(keen.offer_id)->terms, age);

  // Nearly 1.5x the ceiling: not insulting, but out of reach even with
  // rivals and the deadline, so the buyer comes back with its own offer.
  TransferNegotiation::OfferTerms asked;
  asked.fee = 2'200'000;
  asked.upfront_percent = 60;
  asked.instalment_years = 2;
  ASSERT_EQ(controller->counterIncomingOffer(keen.offer_id, asked),
            OfferOutcome::AwaitingReply);
  auto view = controller->getIncomingOfferView(keen.offer_id);
  ASSERT_TRUE(view);
  EXPECT_EQ(view->status, OfferStatus::AwaitingBuyer);
  EXPECT_LT(controller->getCurrentDate(), view->respond_on)
      << "the answer comes on a later day";
  EXPECT_EQ(controller->settleIncomingOffer(keen.offer_id),
            OfferOutcome::Failed)
      << "nothing to accept while the buyer thinks it over";

  waitForAnswer(*controller, keen.offer_id);
  view = controller->getIncomingOfferView(keen.offer_id);
  ASSERT_TRUE(view) << "the buyer answered with an offer";
  EXPECT_EQ(view->status, OfferStatus::AwaitingClub);
  ASSERT_GE(view->history.size(), 3u);
  EXPECT_EQ(view->history[0].move, BuyerNegotiation::Move::Bid);
  EXPECT_EQ(view->history[1].move, BuyerNegotiation::Move::Counter);
  EXPECT_EQ(view->history[1].terms.fee, asked.fee);
  const BuyerNegotiation::Move answer = view->history.back().move;
  EXPECT_TRUE(answer == BuyerNegotiation::Move::Improved ||
              answer == BuyerNegotiation::Move::FinalOffer)
      << BuyerNegotiation::moveKey(answer);
  const double improved = BuyerNegotiation::buyerCost(view->terms, age);
  EXPECT_GT(improved, opening);
  EXPECT_LE(improved, 1'500'000.0 * 1.2 * 1.06) << "within its ceiling";
  EXPECT_TRUE(std::ranges::any_of(
      controller->getInbox(), [](const InboxMessage& message)
      { return message.title_key == "INBOX_OFFER_REPLY_TITLE"; }));

  // Accepting completes the sale: the upfront part reaches the ledger now,
  // the rest is owed by the buyer as yearly instalments.
  const TransferNegotiation::OfferTerms agreed = view->terms;
  const Finances& finances = gamedata->getTeams().at(managed).getFinances();
  const int64_t before =
      categoryTotal(finances, FinanceCategory::TransferFeeIn);
  ASSERT_EQ(controller->settleIncomingOffer(keen.offer_id), OfferOutcome::Sold);
  EXPECT_EQ(gamedata->getPlayer(keen.player)->get().getTeamId(), keen.buyer);
  EXPECT_EQ(categoryTotal(finances, FinanceCategory::TransferFeeIn) - before,
            static_cast<int64_t>(TransferNegotiation::upfrontAmount(agreed)));
  int64_t owed = 0;
  for (const TransferObligation& obligation :
       marketOf(*controller).obligations())
    if (obligation.player_id == keen.player &&
        obligation.kind == ObligationKind::Instalment)
    {
      EXPECT_EQ(obligation.payer, keen.buyer);
      EXPECT_EQ(obligation.payee, managed);
      owed += obligation.amount;
    }
  EXPECT_EQ(owed, static_cast<int64_t>(agreed.fee) -
                      TransferNegotiation::upfrontAmount(agreed));
  EXPECT_TRUE(controller->getIncomingOffers().empty() ||
              std::ranges::none_of(controller->getIncomingOffers(),
                                   [&](const IncomingOffer& offer)
                                   { return offer.player_id == keen.player; }));
}

TEST_F(TransferMarketTest, BuyerAcceptsACounterWithinItsCeiling)
{
  const TeamID managed = manageSmallClub(*controller);
  const KeenOffer keen =
      openKeenOffer(*controller, managed, 1'000'000, 1'500'000);
  ASSERT_NE(keen.offer_id, 0u);
  TransferNegotiation::OfferTerms asked;
  asked.fee = 1'400'000;
  ASSERT_EQ(controller->counterIncomingOffer(keen.offer_id, asked),
            OfferOutcome::AwaitingReply);
  auto gamedata = controller->getGameData();
  waitForAnswer(*controller, keen.offer_id);
  EXPECT_EQ(gamedata->getPlayer(keen.player)->get().getTeamId(), keen.buyer)
      << "the buyer accepted the club's terms and the sale went through";
  EXPECT_FALSE(controller->getIncomingOfferView(keen.offer_id).has_value());
  const auto moves = marketOf(*controller).historyFor(keen.player);
  ASSERT_FALSE(moves.empty());
  EXPECT_EQ(moves.back().fee, asked.fee);
}

TEST_F(TransferMarketTest, InsultingDemandsEndTheTalks)
{
  const TeamID managed = manageSmallClub(*controller);
  const KeenOffer keen =
      openKeenOffer(*controller, managed, 1'000'000, 1'500'000);
  ASSERT_NE(keen.offer_id, 0u);
  TransferNegotiation::OfferTerms asked;
  asked.fee = 10'000'000;  // Beyond any stretch of its ceiling.
  ASSERT_EQ(controller->counterIncomingOffer(keen.offer_id, asked),
            OfferOutcome::AwaitingReply);
  waitForAnswer(*controller, keen.offer_id);
  EXPECT_FALSE(controller->getIncomingOfferView(keen.offer_id).has_value());
  EXPECT_EQ(
      controller->getGameData()->getPlayer(keen.player)->get().getTeamId(),
      managed);
  EXPECT_TRUE(std::ranges::any_of(
      controller->getInbox(), [](const InboxMessage& message)
      { return message.body_key == "INBOX_OFFER_WITHDRAWN_INSULT_BODY"; }));
}

TEST_F(TransferMarketTest, RejectingABigBidUpsetsAPlayerWhoWantsToGo)
{
  const TeamID managed = manageSmallClub(*controller);
  const KeenOffer probe = openKeenOffer(*controller, managed, 10'000, 20'000);
  ASSERT_NE(probe.offer_id, 0u);
  TransferMarket& market = controller->getGame()->getTransfers();
  // The same pairing with a bid above his market value.
  IncomingOffer offer = *market.findIncomingOffer(probe.offer_id);
  const uint32_t value = controller->getPlayerMarketValue(probe.player);
  offer.terms.fee = std::max<uint32_t>(value, 100'000);
  offer.max_fee = offer.terms.fee * 2;
  ASSERT_TRUE(market.updateIncomingOffer(offer));

  const float morale = controller->getGameData()
                           ->getPlayer(probe.player)
                           ->get()
                           .getDynamics()
                           .morale;
  // A player who has not asked to leave yet does so now.
  const bool asked_before =
      controller->getIncomingOfferView(probe.offer_id)->stance ==
      BuyerNegotiation::PlayerStance::AskedToLeave;
  ASSERT_TRUE(controller->rejectIncomingOffer(probe.offer_id));
  EXPECT_LT(controller->getGameData()
                ->getPlayer(probe.player)
                ->get()
                .getDynamics()
                .morale,
            morale);
  const PlayerRelation* relation = controller->getPlayerRelation(probe.player);
  ASSERT_NE(relation, nullptr);
  EXPECT_EQ(relation->request, TalkRequest::Transfer)
      << "an ambitious player asks to leave";
  EXPECT_TRUE(std::ranges::any_of(
      controller->getInbox(),
      [&](const InboxMessage& message)
      {
        return message.title_key == (asked_before
                                         ? "OFFER_REJECTED_UPSET_TITLE"
                                         : "OFFER_REJECTED_REQUEST_TITLE") &&
               message.player_id == probe.player;
      }));
}

TEST_F(TransferMarketTest, NotForSaleRejectsEveryBidAndKeepsClubsAway)
{
  const TeamID managed = manageSmallClub(*controller);
  const KeenOffer keen =
      openKeenOffer(*controller, managed, 1'000'000, 1'500'000);
  ASSERT_NE(keen.offer_id, 0u);
  TransferMarket& market = controller->getGame()->getTransfers();
  // A rival bid for the same player.
  IncomingOffer rival = *market.findIncomingOffer(keen.offer_id);
  rival.history.clear();
  rival.buyer = 0;
  for (const auto& team : controller->getTeams())
  {
    const TeamID id = team.get().getId();
    if (id != managed && id != keen.buyer && id != FREE_AGENTS_TEAM_ID)
      rival.buyer = id;
  }
  ASSERT_NE(rival.buyer, 0);
  const std::uint32_t rival_id = market.addIncomingOffer(rival);
  EXPECT_EQ(controller->getIncomingOfferView(keen.offer_id)->rivals, 1);

  ASSERT_TRUE(controller->declareNotForSale(keen.offer_id));
  EXPECT_EQ(market.findIncomingOffer(keen.offer_id), nullptr);
  EXPECT_EQ(market.findIncomingOffer(rival_id), nullptr);
  EXPECT_TRUE(market.isNotForSale(keen.player, controller->getCurrentDate()));
  // Bids on the transfer list are refused as well.
  EXPECT_FALSE(controller->isPlayerListed(keen.player));
}

TEST_F(TransferMarketTest, ListingBidsForManagedPlayersBecomeOffers)
{
  const TeamID managed = manageSmallClub(*controller);
  auto gamedata = controller->getGameData();
  const PlayerID player =
      controller->getPlayersForTeam(managed).front().get().getId();
  controller->listPlayerForTransfer(player, 1'000'000);
  ASSERT_TRUE(controller->isPlayerListed(player));
  TeamID bidder = 0;
  for (const auto& team : controller->getTeams())
    if (team.get().getId() != managed &&
        team.get().getId() != FREE_AGENTS_TEAM_ID)
      bidder = team.get().getId();
  gamedata->getTeams().at(bidder).getFinances().addBalance(500'000'000LL);

  ASSERT_TRUE(controller->submitBid(player, bidder, 900'000));
  const auto listing = controller->getAllListings().find(player);
  ASSERT_NE(listing, controller->getAllListings().end());
  EXPECT_FALSE(listing->second.highest_bidder_id.has_value());
  const auto offer = std::ranges::find_if(
      controller->getIncomingOffers(), [&](const IncomingOffer& candidate)
      { return candidate.player_id == player && candidate.buyer == bidder; });
  ASSERT_NE(offer, controller->getIncomingOffers().end());
  EXPECT_EQ(offer->terms.fee, 900'000u);
  EXPECT_GE(offer->max_fee, 900'000u);
  const auto view = controller->getIncomingOfferView(offer->id);
  ASSERT_TRUE(view);
  EXPECT_EQ(view->asking_price, 1'000'000u);
  ASSERT_EQ(view->history.size(), 1u);
  EXPECT_EQ(view->history.front().move, BuyerNegotiation::Move::Bid);
  EXPECT_FALSE(controller->submitBid(player, bidder, 950'000))
      << "one set of talks per club";
}

TEST_F(TransferMarketTest, ASpurnedClubDoesNotComeStraightBack)
{
  const TeamID managed = manageSmallClub(*controller);
  const KeenOffer keen =
      openKeenOffer(*controller, managed, 1'000'000, 1'500'000);
  ASSERT_NE(keen.offer_id, 0u);
  TransferMarket& market = controller->getGame()->getTransfers();
  const GameDateValue today = controller->getCurrentDate();
  ASSERT_TRUE(controller->rejectIncomingOffer(keen.offer_id));
  EXPECT_TRUE(market.talksClosed(keen.player, keen.buyer, today));
  EXPECT_TRUE(market.talksClosed(keen.player, keen.buyer, today + 10));
  EXPECT_FALSE(market.talksClosed(
      keen.player, keen.buyer,
      today + static_cast<std::size_t>(
                  TransferTuning::Buyer::TALKS_COOLDOWN_DAYS + 1)));

  // Listed later, the same club's bid is refused; another club may bid.
  controller->listPlayerForTransfer(keen.player, 2'000'000);
  ASSERT_TRUE(controller->isPlayerListed(keen.player));
  EXPECT_FALSE(controller->submitBid(keen.player, keen.buyer, 1'800'000));
  TeamID other = 0;
  for (const auto& team : controller->getTeams())
  {
    const TeamID id = team.get().getId();
    if (id != managed && id != keen.buyer && id != FREE_AGENTS_TEAM_ID)
      other = id;
  }
  controller->getGameData()->getTeams().at(other).getFinances().addBalance(
      500'000'000LL);
  EXPECT_TRUE(controller->submitBid(keen.player, other, 1'800'000));

  // An offer left to expire counts as turned down too.
  IncomingOffer ignored;
  ignored.player_id = keen.player;
  for (const auto& team : controller->getTeams())
  {
    const TeamID id = team.get().getId();
    if (ignored.buyer == 0 && id != managed && id != keen.buyer &&
        id != other && id != FREE_AGENTS_TEAM_ID)
      ignored.buyer = id;
  }
  ASSERT_NE(ignored.buyer, 0);
  ignored.terms.fee = 500'000;
  ignored.created = today;
  ignored.expires = today;
  market.addIncomingOffer(ignored);
  controller->advanceDay();
  EXPECT_TRUE(market.talksClosed(keen.player, ignored.buyer,
                                 controller->getCurrentDate()));
}

TEST_F(TransferMarketTest, OffersForARetiredPlayerAreDropped)
{
  const TeamID managed = manageSmallClub(*controller);
  const KeenOffer keen =
      openKeenOffer(*controller, managed, 1'000'000, 1'500'000);
  ASSERT_NE(keen.offer_id, 0u);
  TransferMarket& market = controller->getGame()->getTransfers();
  market.closeTalks(keen.player, keen.buyer, controller->getCurrentDate());
  market.setNotForSale(keen.player, controller->getCurrentDate() + 10);

  // What WorldSimulation::retirePlayers does to a retiring player.
  auto gamedata = controller->getGameData();
  Team& club = gamedata->getTeams().at(managed);
  club.removePlayerID(keen.player);
  club.generateStartingXI(*gamedata, gamedata->getStatsConfig());
  ASSERT_TRUE(gamedata->removePlayer(keen.player));

  // The market's day starts with this sweep (retirements happen on the
  // same day, before it).
  market.forgetRemovedPlayers();
  EXPECT_EQ(market.findIncomingOffer(keen.offer_id), nullptr);
  EXPECT_TRUE(std::ranges::none_of(market.incomingOffers(),
                                   [&](const IncomingOffer& offer)
                                   { return offer.player_id == keen.player; }));
  EXPECT_TRUE(std::ranges::none_of(
      market.closedTalks(), [&](const TalksCooldown& cooldown)
      { return cooldown.player_id == keen.player; }));
  EXPECT_EQ(market.flags(keen.player), nullptr);
  EXPECT_FALSE(controller->getIncomingOfferView(keen.offer_id).has_value());
}

TEST_F(TransferMarketTest, NegotiationStateSurvivesSaveAndReload)
{
  const TeamID managed = manageSmallClub(*controller);
  const KeenOffer keen =
      openKeenOffer(*controller, managed, 1'000'000, 1'500'000);
  ASSERT_NE(keen.offer_id, 0u);
  TransferNegotiation::OfferTerms asked;
  asked.fee = 2'000'000;
  asked.upfront_percent = 40;
  asked.instalment_years = 3;
  asked.appearance_bonus = 250'000;
  asked.appearance_target = 20;
  asked.goal_bonus = 100'000;
  asked.goal_target = 10;
  asked.sell_on_percent = 15;
  ASSERT_EQ(controller->counterIncomingOffer(keen.offer_id, asked),
            OfferOutcome::AwaitingReply);
  TransferMarket& market = controller->getGame()->getTransfers();
  // A second player declared not for sale.
  PlayerID protected_player = 0;
  for (const auto& player : controller->getPlayersForTeam(managed))
    if (player.get().getId() != keen.player)
      protected_player = player.get().getId();
  market.setNotForSale(protected_player, controller->getCurrentDate() + 20);
  // A club that walked away from talks for him.
  const TeamID spurned =
      keen.buyer == controller->getTeams().front().get().getId()
          ? controller->getTeams().back().get().getId()
          : controller->getTeams().front().get().getId();
  market.closeTalks(protected_player, spurned, controller->getCurrentDate());
  const bool spurned_before = market.talksClosed(
      protected_player, spurned, controller->getCurrentDate() + 5);
  const IncomingOffer before = *market.findIncomingOffer(keen.offer_id);
  controller->saveGame();

  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(0));
  const TransferMarket& reloaded = marketOf(*controller);
  const IncomingOffer* after = reloaded.findIncomingOffer(keen.offer_id);
  ASSERT_NE(after, nullptr);
  EXPECT_EQ(after->status, OfferStatus::AwaitingBuyer);
  EXPECT_EQ(after->respond_on, before.respond_on);
  EXPECT_EQ(after->expires, before.expires);
  EXPECT_EQ(after->max_fee, before.max_fee);
  EXPECT_EQ(after->patience, before.patience);
  EXPECT_EQ(after->insults, before.insults);
  EXPECT_EQ(after->round, before.round);
  EXPECT_EQ(after->firm, before.firm);
  EXPECT_EQ(after->asked.fee, asked.fee);
  EXPECT_EQ(after->asked.upfront_percent, asked.upfront_percent);
  EXPECT_EQ(after->asked.instalment_years, asked.instalment_years);
  EXPECT_EQ(after->asked.appearance_bonus, asked.appearance_bonus);
  EXPECT_EQ(after->asked.appearance_target, asked.appearance_target);
  EXPECT_EQ(after->asked.goal_bonus, asked.goal_bonus);
  EXPECT_EQ(after->asked.goal_target, asked.goal_target);
  EXPECT_EQ(after->asked.sell_on_percent, asked.sell_on_percent);
  ASSERT_EQ(after->history.size(), before.history.size());
  for (size_t index = 0; index < before.history.size(); ++index)
  {
    EXPECT_EQ(after->history[index].move, before.history[index].move);
    EXPECT_EQ(after->history[index].date, before.history[index].date);
    EXPECT_EQ(after->history[index].terms.fee, before.history[index].terms.fee);
    EXPECT_EQ(after->history[index].terms.sell_on_percent,
              before.history[index].terms.sell_on_percent);
  }
  EXPECT_TRUE(reloaded.isNotForSale(protected_player,
                                    controller->getCurrentDate() + 20));
  EXPECT_FALSE(reloaded.isNotForSale(protected_player,
                                     controller->getCurrentDate() + 21));
  EXPECT_EQ(reloaded.talksClosed(protected_player, spurned,
                                 controller->getCurrentDate() + 5),
            spurned_before);
  EXPECT_TRUE(reloaded.talksClosed(protected_player, spurned,
                                   controller->getCurrentDate()));

  // The reloaded talks carry on: the buyer still answers.
  waitForAnswer(*controller, keen.offer_id);
  const IncomingOffer* answered = reloaded.findIncomingOffer(keen.offer_id);
  EXPECT_TRUE(answered == nullptr ||
              answered->status == OfferStatus::AwaitingClub);
}

TEST_F(TransferMarketTest, SummerMarketIsMostlyFreeAndLoanMoves)
{
  using enum TransferKind;
  manageFirstClub(*controller);
  const auto started = std::chrono::steady_clock::now();
  for (int day = 0; day < 40; ++day) controller->advanceDay();
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();

  int fee_moves = 0;
  int free_moves = 0;
  int loans = 0;
  for (const TransferRecord& record : marketOf(*controller).history())
  {
    if (record.kind == Permanent) ++fee_moves;
    if (record.kind == Free) ++free_moves;
    if (record.kind == Loan) ++loans;
  }
  const int moves = fee_moves + free_moves + loans;
  const double fee_share =
      moves > 0 ? static_cast<double>(fee_moves) / moves : 0.0;
  std::cout << "[transfer-calibration] moves=" << moves << " fee=" << fee_moves
            << " free=" << free_moves << " loans=" << loans
            << " fee_share=" << fee_share << " per_club="
            << static_cast<double>(moves) /
                   static_cast<double>(controller->getTeams().size())
            << " seconds=" << seconds << "\n";
  // About two moves per club in the first 40 days of the summer window.
  EXPECT_GT(moves, 800);
  EXPECT_GT(fee_moves, 0);
  EXPECT_GT(loans, 0);
  EXPECT_GT(free_moves, 0);
  // FIFA 2025: 17.7% of men's professional moves carried a fee.
  EXPECT_GT(fee_share, 0.08);
  EXPECT_LT(fee_share, 0.25);
  for (const auto& team : controller->getTeams())
  {
    EXPECT_EQ(team.get().getFinances().getBalance(),
              team.get().getFinances().ledgerTotal());
    EXPECT_GE(team.get().getPlayerIDs().size(), 18u) << team.get().getName();
  }
}

TEST_F(TransferMarketTest, FreeAgentsNeedCashNotTransferBudget)
{
  auto gamedata = controller->getGameData();
  // The club with the most wage room, so only the fee budget is at stake:
  // many lower-league clubs start at their wage cap.
  const auto wageRoom = [this](const Team& team)
  {
    return team.getFinances().getWageBudget() -
           controller->getWeeklyWageBill(team.getId());
  };
  const auto roomiest = std::ranges::max_element(
      controller->getTeams(), {},
      [&wageRoom](const auto& team) { return wageRoom(team.get()); });
  ASSERT_NE(roomiest, controller->getTeams().end());
  const TeamID managed = roomiest->get().getId();
  controller->selectManagedTeam(managed);
  const auto& squad = controller->getPlayersForTeam(managed);
  const auto best = std::ranges::max_element(
      squad, {}, [&gamedata](const auto& player)
      { return player.get().getOverall(gamedata->getStatsConfig()); });
  ASSERT_NE(best, squad.end());
  const PlayerID player = best->get().getId();
  ASSERT_TRUE(controller->releasePlayer(player));

  Finances& finances = gamedata->getTeams().at(managed).getFinances();
  finances.setTransferBudget(0);
  ASSERT_EQ(controller->getContractTalkKind(player), ContractKind::FreeAgent);
  const auto offer = TransferNegotiation::demandedOffer(
      controller->getPlayerDemand(player, ContractKind::FreeAgent));
  ASSERT_GE(wageRoom(gamedata->getTeams().at(managed)),
            static_cast<int64_t>(offer.weekly_wage))
      << "the scenario needs a club with room for the wage";
  const auto result = controller->proposeContract(player, offer);
  EXPECT_TRUE(result.response.accepted);
  ASSERT_TRUE(result.completed)
      << "a free signing needs cash, not a fee budget (over budget: "
      << result.over_budget << ")";
  EXPECT_EQ(gamedata->getPlayer(player)->get().getTeamId(), managed);
  EXPECT_EQ(finances.getBalance(), finances.ledgerTotal());
}

TEST_F(TransferMarketTest, SellOnPayoutNeverLeavesANegativeBudget)
{
  controller->saveGame();
  MarketHarness harness(*controller);
  auto gamedata = controller->getGameData();
  const auto& teams = controller->getTeams();
  const TeamID first = teams[10].get().getId();
  const TeamID second = teams[11].get().getId();
  const TeamID third = teams[12].get().getId();
  const PlayerID player =
      controller->getPlayersForTeam(first).front().get().getId();

  TransferMarket::Deal deal;
  deal.player_id = player;
  deal.buyer_id = second;
  deal.terms.fee = 1'000'000;
  deal.terms.sell_on_percent = TransferTuning::Offer::MAX_SELL_ON_PERCENT;
  deal.contract.weekly_wage = 2'000;
  deal.contract.years = 2;
  ASSERT_TRUE(harness.market.completeTransfer(deal, GameDateValue(2025, 7, 5),
                                              FREE_AGENTS_TEAM_ID));
  // Resold with a small upfront part: the 30% sell-on exceeds it.
  Finances& seller = gamedata->getTeams().at(second).getFinances();
  seller.setTransferBudget(0);
  TransferMarket::Deal resale = deal;
  resale.buyer_id = third;
  resale.terms = TransferNegotiation::OfferTerms{};
  resale.terms.fee = 10'000'000;
  resale.terms.upfront_percent = TransferTuning::Offer::MIN_UPFRONT_PERCENT;
  resale.terms.instalment_years = 2;
  ASSERT_TRUE(harness.market.completeTransfer(resale, GameDateValue(2025, 8, 5),
                                              FREE_AGENTS_TEAM_ID));
  EXPECT_GE(seller.getTransferBudget(), 0);
  EXPECT_EQ(seller.getBalance(), seller.ledgerTotal());
}

TEST_F(TransferMarketTest, OpeningBidAtValueIsCounteredThenAgreed)
{
  const TeamID managed = manageFirstClub(*controller);
  auto gamedata = controller->getGameData();
  Finances& finances = gamedata->getTeams().at(managed).getFinances();
  finances.addBalance(500'000'000LL);
  finances.setTransferBudget(500'000'000LL);
  const PlayerID target = findWillingTarget(*controller, managed);
  ASSERT_NE(target, 0u);
  gamedata->getPlayers().at(target).setContractYears(3);

  TransferNegotiation::OfferTerms terms;
  terms.fee = controller->getPlayerMarketValue(target);
  const ClubResponse opening = controller->makeTransferOffer(target, terms);
  ASSERT_EQ(opening.decision, ClubResponse::Decision::Counter)
      << "sellers haggle over a bid at market value";
  EXPECT_GT(opening.counter_fee, terms.fee);
  EXPECT_FALSE(opening.reasons.empty());
  EXPECT_FALSE(controller->getContractTalkKind(target).has_value());

  terms.fee = opening.counter_fee;
  const ClubResponse agreed = controller->makeTransferOffer(target, terms);
  EXPECT_EQ(agreed.decision, ClubResponse::Decision::Accept);
  EXPECT_EQ(controller->getContractTalkKind(target), ContractKind::Transfer);

  // The agent opens above the real demand and softens after a refusal.
  const auto demand =
      controller->getPlayerDemand(target, ContractKind::Transfer);
  EXPECT_GT(demand.asking_wage, demand.weekly_wage);
  TransferNegotiation::ContractOffer lowball =
      TransferNegotiation::demandedOffer(demand);
  lowball.weekly_wage = demand.weekly_wage / 2;
  EXPECT_FALSE(controller->proposeContract(target, lowball).completed);
  EXPECT_LT(
      controller->getPlayerDemand(target, ContractKind::Transfer).asking_wage,
      demand.asking_wage);
}

TEST_F(TransferMarketTest, TakingChargeLeavesListingsToTheManager)
{
  // The seeded market lists surplus players of every club; once a manager
  // takes charge those decisions are his, so no unsolicited loan offers.
  auto fresh = std::make_unique<GameController>();
  fresh->newGame(1);
  const TeamID club = manageFirstClub(*fresh);
  for (const auto& player : fresh->getPlayersForTeam(club))
  {
    EXPECT_FALSE(marketOf(*fresh).isLoanListed(player.get().getId()));
    EXPECT_FALSE(fresh->isPlayerListed(player.get().getId()));
  }
  for (int day = 0; day < 20; ++day) fresh->advanceDay();
  EXPECT_TRUE(std::ranges::none_of(fresh->getIncomingOffers(),
                                   [](const IncomingOffer& offer)
                                   { return offer.loan; }));
}

TEST_F(TransferMarketTest, EveryClubBudgetKeepsThePayrollReserve)
{
  const TeamID managed = manageFirstClub(*controller);
  auto gamedata = controller->getGameData();
  const TransferMarket& market = marketOf(*controller);
  const GameDateValue today = controller->getCurrentDate();
  for (const TeamID club : {managed, controller->getTeams()[7].get().getId()})
  {
    Team& team = gamedata->getTeams().at(club);
    Finances& finances = team.getFinances();
    const std::int64_t payroll =
        finances.getCurrentWageSpending(*gamedata, team);
    ASSERT_GT(payroll, 0);
    const std::int64_t reserve =
        WorldTuning::Finance::CASH_RESERVE_WEEKS * payroll;
    finances.setTransferBudget(500'000'000);

    // A generous board allowance buys nothing while the cash only covers
    // the payroll reserve.
    finances.record(today, FinanceCategory::Investment,
                    reserve - 1 - finances.getBalance());
    EXPECT_EQ(market.spendableBudget(club, today), 0) << team.getName();
    EXPECT_EQ(controller->transferBudgetForTeam(club), 0u) << team.getName();

    // Above the reserve, the spare cash is the limit for AI clubs and the
    // manager alike.
    finances.record(today, FinanceCategory::Investment, 5'000'001);
    const std::int64_t expected =
        5'000'000 - market.committedPayables(club, today);
    EXPECT_EQ(market.spendableBudget(club, today),
              std::max<std::int64_t>(0, expected))
        << team.getName();
    EXPECT_EQ(
        static_cast<std::int64_t>(controller->transferBudgetForTeam(club)),
        market.spendableBudget(club, today));
  }
}

TEST_F(TransferMarketTest, ReleasingAStarterOnlyFillsHisPlace)
{
  const TeamID managed = manageFirstClub(*controller);
  auto gamedata = controller->getGameData();
  const Lineup& lineup = gamedata->getTeams().at(managed).getLineup();
  ASSERT_GE(lineup.getOutfieldPlayers().size(), 2u);
  ASSERT_FALSE(lineup.getReserves().empty());
  const Lineup::PositionedPlayer leaving = lineup.getOutfieldPlayers()[1];
  const PlayerID released = leaving.player->getId();
  std::vector<PlayerID> others;
  for (const auto& positioned : lineup.getOutfieldPlayers())
    if (positioned.player->getId() != released)
      others.push_back(positioned.player->getId());
  const PlayerID keeper = lineup.getGoalkeeper()->getId();

  ASSERT_TRUE(controller->releasePlayer(released));
  EXPECT_FALSE(lineup.isStarter(released));
  EXPECT_EQ(lineup.getGoalkeeper()->getId(), keeper);
  ASSERT_EQ(lineup.getOutfieldPlayers().size(), others.size() + 1);
  for (const PlayerID id : others) EXPECT_TRUE(lineup.isStarter(id));
  // The replacement takes the departed player's spot on the pitch.
  const auto replacement = std::ranges::find_if(
      lineup.getOutfieldPlayers(), [&](const auto& positioned)
      { return !std::ranges::contains(others, positioned.player->getId()); });
  ASSERT_NE(replacement, lineup.getOutfieldPlayers().end());
  EXPECT_EQ(replacement->position.x, leaving.position.x);
  EXPECT_EQ(replacement->position.y, leaving.position.y);
  EXPECT_EQ(replacement->player->getTeamId(), managed);
  for (const Player* reserve : lineup.getReserves())
  {
    EXPECT_NE(reserve->getId(), released);
    EXPECT_NE(reserve->getId(), replacement->player->getId());
  }
}

TEST_F(TransferMarketTest, TakingChargeWithdrawsTheClubsUnmanagedBids)
{
  // While the market is seeded no club has a manager yet: a bid placed by
  // the club the user then picks must not complete without him.
  auto gamedata = controller->getGameData();
  const auto& teams = controller->getTeams();
  const TeamID chosen = teams[5].get().getId();
  const TeamID seller = teams[6].get().getId();
  gamedata->getTeams().at(chosen).getFinances().addBalance(1'000'000'000LL);
  const PlayerID target =
      controller->getPlayersForTeam(seller).back().get().getId();
  controller->listPlayerForTransfer(target, 1'000'000);
  ASSERT_TRUE(controller->submitBid(target, chosen, 1'200'000));

  controller->selectManagedTeam(chosen);
  const GameDateValue taken_over = controller->getCurrentDate();
  for (const auto& [player_id, listing] : controller->getAllListings())
    EXPECT_NE(listing.highest_bidder_id, std::optional<TeamID>(chosen))
        << "player " << player_id;
  controller->advanceDay();
  EXPECT_NE(gamedata->getPlayer(target)->get().getTeamId(), chosen);
  for (const TransferRecord& record : marketOf(*controller).history())
    if (taken_over < record.date)
      EXPECT_NE(record.to_team, chosen) << "player " << record.player_id;
}

TEST_F(TransferMarketTest, LoansNeverOutliveTheContract)
{
  EXPECT_EQ(TransferNegotiation::contractEndDate(GameDateValue(2026, 6, 10), 1),
            GameDateValue(2026, 6, 30));
  EXPECT_EQ(TransferNegotiation::contractEndDate(GameDateValue(2026, 6, 10), 2),
            GameDateValue(2027, 6, 30));
  EXPECT_EQ(TransferNegotiation::contractEndDate(GameDateValue(2026, 8, 1), 1),
            GameDateValue(2027, 6, 30));

  controller->saveGame();
  MarketHarness harness(*controller);
  auto gamedata = controller->getGameData();
  const auto& teams = controller->getTeams();
  const TeamID parent = teams[5].get().getId();
  const TeamID borrower = teams[6].get().getId();
  const auto& squad = controller->getPlayersForTeam(parent);
  ASSERT_GE(squad.size(), 2u);
  const PlayerID expiring = squad[0].get().getId();
  const PlayerID contracted = squad[1].get().getId();
  gamedata->getPlayers().at(expiring).setContractYears(1);
  gamedata->getPlayers().at(contracted).setContractYears(2);

  // A June loan covers next season: only a player still under contract then
  // may go.
  const TransferNegotiation::LoanTerms terms;
  const GameDateValue june(2026, 6, 10);
  EXPECT_FALSE(harness.market.startLoan(expiring, borrower, terms, june,
                                        FREE_AGENTS_TEAM_ID));
  EXPECT_EQ(gamedata->getPlayer(expiring)->get().getTeamId(), parent);
  ASSERT_TRUE(harness.market.startLoan(contracted, borrower, terms, june,
                                       FREE_AGENTS_TEAM_ID));
  const LoanDeal* loan = harness.market.findLoan(contracted);
  ASSERT_NE(loan, nullptr);
  EXPECT_FALSE(TransferNegotiation::contractEndDate(june, 2) < loan->end);
}

TEST_F(TransferMarketTest, AMoveEndsTheLoanForGood)
{
  controller->saveGame();
  MarketHarness harness(*controller);
  auto gamedata = controller->getGameData();
  const auto& teams = controller->getTeams();
  const TeamID parent = teams[5].get().getId();
  const TeamID borrower = teams[6].get().getId();
  const TeamID next_club = teams[7].get().getId();
  // Two outfield players: the parent keeps its goalkeepers.
  std::vector<PlayerID> squad;
  for (const auto& player : controller->getPlayersForTeam(parent))
    if (player.get().getRole() != PlayerRole::GK)
      squad.push_back(player.get().getId());
  ASSERT_GE(squad.size(), 2u);
  const PlayerID signed_elsewhere = squad[0];
  const PlayerID stranded = squad[1];
  TransferNegotiation::LoanTerms terms;
  terms.wage_share = 50;
  const GameDateValue start(2025, 8, 20);
  for (const PlayerID id : {signed_elsewhere, stranded})
  {
    gamedata->getPlayers().at(id).setContractYears(2);
    ASSERT_TRUE(harness.market.startLoan(id, borrower, terms, start,
                                         FREE_AGENTS_TEAM_ID));
  }

  // A pre-contract completing on a loanee ends the loan: its end date must
  // not take him back to the old club.
  TransferMarket::Deal deal;
  deal.player_id = signed_elsewhere;
  deal.buyer_id = next_club;
  deal.kind = TransferKind::PreContract;
  deal.contract.weekly_wage = 10'000;
  deal.contract.years = 2;
  ASSERT_TRUE(
      harness.market.completeTransfer(deal, start + 30, FREE_AGENTS_TEAM_ID));
  EXPECT_EQ(harness.market.findLoan(signed_elsewhere), nullptr);

  // A loanee who left the borrower another way (an old save released him
  // while on loan) no longer costs the parent club a wage share.
  gamedata->getTeams().at(borrower).removePlayerID(stranded);
  gamedata->getTeams().at(FREE_AGENTS_TEAM_ID).addPlayerID(stranded);
  gamedata->transferPlayer(stranded, FREE_AGENTS_TEAM_ID);
  const Finances& parent_finances =
      gamedata->getTeams().at(parent).getFinances();
  GameDateValue day = start + 31;
  while (dayOrdinal(day) % 7 != 0) day = day + 1;
  const std::size_t entries = parent_finances.getLedger().size();
  harness.market.onDayAdvanced(day, FREE_AGENTS_TEAM_ID);
  EXPECT_EQ(harness.market.findLoan(stranded), nullptr);
  EXPECT_TRUE(harness.market.canBeTraded(stranded));
  for (std::size_t i = entries; i < parent_finances.getLedger().size(); ++i)
    EXPECT_NE(parent_finances.getLedger()[i].category, FinanceCategory::Wages);

  // The season ends: nobody is pulled back.
  harness.market.onDayAdvanced(GameDateValue(2026, 6, 30), FREE_AGENTS_TEAM_ID);
  EXPECT_EQ(gamedata->getPlayer(signed_elsewhere)->get().getTeamId(),
            next_club);
  EXPECT_EQ(gamedata->getPlayer(stranded)->get().getTeamId(),
            FREE_AGENTS_TEAM_ID);
}

namespace
{
/** Wages a club is committed to next season: contracts that run beyond
 * 30 June and the pre-contracts it has agreed. */
int64_t committedNextSeason(const GameController& controller,
                            const TransferMarket& market, TeamID club)
{
  int64_t wages = 0;
  for (const auto& player : controller.getPlayersForTeam(club))
  {
    if (player.get().getContractYears() > 1 &&
        market.findLoan(player.get().getId()) == nullptr)
      wages += player.get().getWage();
  }
  for (const auto& [id, loan] : market.loans())
  {
    if (loan.parent == club &&
        controller.getGameData()->getPlayer(id)->get().getContractYears() > 1)
      wages += loan.full_wage;
  }
  for (const auto& [id, deal] : market.preContracts())
  {
    if (deal.to_team == club) wages += deal.terms.weekly_wage;
  }
  return wages;
}
}  // namespace

TEST_F(TransferMarketTest, AiPreContractsFitTheBuyersBudgets)
{
  const TeamID managed = manageFirstClub(*controller);
  controller->saveGame();
  MarketHarness harness(*controller);
  auto gamedata = controller->getGameData();
  const std::size_t clubs = controller->getTeams().size();
  const int attempts = TransferTuning::Market::perDay(
      clubs, TransferTuning::Market::DAILY_PRE_CONTRACT_SHARE);
  const GameDateValue first(2026, 1, 1);
  const GameDateValue last(2026, 6, 30);
  WorldRng rng =
      WorldRng::stream(gamedata->getWorldSeed(), RngDomain::Transfers, 11, 7);
  for (GameDateValue day = first; !(last < day); day = day + 1)
    harness.market.runAiPreContracts(day, managed, rng, attempts);

  const auto& deals = harness.market.preContracts();
  ASSERT_GT(deals.size(), 50u) << "the market still works";
  std::unordered_map<TeamID, int64_t> costs;
  for (const auto& [player_id, deal] : deals)
  {
    const Team& buyer = gamedata->getTeams().at(deal.to_team);
    const int64_t budget = buyer.getFinances().getWageBudget();
    EXPECT_LE(static_cast<double>(deal.terms.weekly_wage),
              static_cast<double>(budget) *
                  TransferTuning::Market::MAX_SINGLE_WAGE_SHARE)
        << buyer.getName() << " pays one player out of its league";
    costs[deal.to_team] += deal.terms.signing_bonus +
                           static_cast<int64_t>(deal.terms.weekly_wage) *
                               TransferTuning::Offer::FREE_AGENT_FEE_WEEKS;
  }
  for (const auto& [club, cost] : costs)
  {
    const Team& buyer = gamedata->getTeams().at(club);
    EXPECT_LE(committedNextSeason(*controller, harness.market, club),
              buyer.getFinances().getWageBudget())
        << buyer.getName() << " breaks its wage budget";
    EXPECT_LE(cost, harness.market.spendableBudget(club, last))
        << buyer.getName() << " cannot fund the bonuses and agent fees";
  }
}

TEST_F(TransferMarketTest, FreeAgentsLowerTheirDemandsAndSignWithinMeans)
{
  const TeamID managed = manageFirstClub(*controller);
  controller->saveGame();
  MarketHarness harness(*controller);
  auto gamedata = controller->getGameData();

  // A big club lets its best-paid seniors go.
  const auto richest = std::ranges::max_element(
      controller->getTeams(), {}, [](const auto& team)
      { return team.get().getFinances().getWageBudget(); });
  const TeamID big_club = richest->get().getId();
  ASSERT_NE(big_club, managed);
  std::vector<PlayerID> released;
  for (const auto& player : controller->getPlayersForTeam(big_club))
  {
    // Outfield players: the club keeps its goalkeepers.
    if (player.get().getAge() >= 24 && released.size() < 12 &&
        player.get().getRole() != PlayerRole::GK)
      released.push_back(player.get().getId());
  }
  const GameDateValue august(2025, 8, 20);
  for (const PlayerID id : released)
    ASSERT_TRUE(harness.market.releasePlayer(id, august, managed));

  const PlayerID star = released.front();
  const uint32_t wage = gamedata->getPlayer(star)->get().getWage();
  const TeamID other = controller->getTeams()[3].get().getId();
  const uint32_t demand =
      TransferNegotiation::contractDemand(
          harness.market.playerContext(star, other, ContractKind::FreeAgent))
          .weekly_wage;
  // Two months without a club.
  harness.market.onDayAdvanced(GameDateValue(2025, 9, 1), managed);
  harness.market.onDayAdvanced(GameDateValue(2025, 10, 1), managed);
  const uint32_t lowered = gamedata->getPlayer(star)->get().getWage();
  EXPECT_LT(lowered, wage);
  EXPECT_NEAR(static_cast<double>(lowered),
              wage * TransferTuning::Market::FREE_AGENT_MONTHLY_WAGE_FACTOR *
                  TransferTuning::Market::FREE_AGENT_MONTHLY_WAGE_FACTOR,
              2.0);
  EXPECT_LT(
      TransferNegotiation::contractDemand(
          harness.market.playerContext(star, other, ContractKind::FreeAgent))
          .weekly_wage,
      demand);
  EXPECT_GE(lowered, TransferTuning::Contract::MINIMUM_WEEKLY_WAGE);

  // Clubs sign the free agents they can pay, and pay them within their
  // wage budget and the single-player share of it.
  std::unordered_map<TeamID, int64_t> room;
  for (const auto& team : controller->getTeams())
    room[team.get().getId()] =
        team.get().getFinances().getWageBudget() -
        controller->getWeeklyWageBill(team.get().getId());
  WorldRng rng =
      WorldRng::stream(gamedata->getWorldSeed(), RngDomain::Transfers, 12, 7);
  const GameDateValue october(2025, 10, 10);
  for (int round = 0; round < 3; ++round)
  {
    for (const auto& team : controller->getTeams())
      harness.market.runAiClub(team.get().getId(), {}, october, managed, rng,
                               true);
  }
  int signed_players = 0;
  std::unordered_map<TeamID, int64_t> added;
  for (const TransferRecord& record : harness.market.history())
  {
    if (record.kind != TransferKind::Free || !(record.date == october))
      continue;
    ++signed_players;
    const Team& club = gamedata->getTeams().at(record.to_team);
    const uint32_t paid =
        gamedata->getPlayer(record.player_id)->get().getWage();
    added[record.to_team] += paid;
    EXPECT_LE(added[record.to_team], room[record.to_team]) << club.getName();
    EXPECT_LE(static_cast<double>(paid),
              static_cast<double>(club.getFinances().getWageBudget()) *
                  TransferTuning::Market::MAX_SINGLE_WAGE_SHARE)
        << club.getName();
  }
  EXPECT_GT(signed_players, 0);
}

TEST_F(TransferMarketTest, RelegationClausesLetPlayersLeaveRelegatedClubs)
{
  const TeamID managed = manageFirstClub(*controller);
  auto gamedata = controller->getGameData();
  // One whole division goes down (the managed club included).
  const LeagueID league = gamedata->getTeams().at(managed).getLeagueId();
  SeasonHistoryEntry entry;
  entry.season = 1;
  entry.start_year = 2025;
  entry.competition_type = MatchType::LEAGUE;
  entry.competition_id = league;
  entry.competition_name = "League";
  std::vector<TeamID> relegated;
  for (const auto& team : controller->getTeams())
  {
    if (team.get().getLeagueId() == league)
      relegated.push_back(team.get().getId());
  }
  entry.relegated = relegated;
  controller->saveGame();
  CompetitionRepository(controller->getDbConn()).saveSeasonHistory({entry});
  MarketHarness harness(*controller);
  Calendar calendar;
  harness.competitions.load(calendar, 2);

  std::unordered_map<TeamID, int64_t> balances;
  std::unordered_map<TeamID, std::vector<PlayerID>> first_team;
  const StatsConfig& config = gamedata->getStatsConfig();
  for (const auto& team : controller->getTeams())
  {
    const TeamID id = team.get().getId();
    balances[id] = team.get().getFinances().getBalance();
    std::vector<std::pair<double, PlayerID>> ranked;
    for (const auto& player : controller->getPlayersForTeam(id))
    {
      if (!player.get().isAcademyPlayer())
        ranked.emplace_back(player.get().getOverall(config),
                            player.get().getId());
    }
    std::ranges::sort(ranked, std::greater<>{});
    for (std::size_t i = 0; i < ranked.size() && i < 16; ++i)
      first_team[id].push_back(ranked[i].second);
  }

  const GameDateValue july(2026, 7, 1);
  harness.market.onDayAdvanced(july, managed);
  std::unordered_map<TeamID, int> exits;
  for (const TransferRecord& record : harness.market.history())
  {
    if (record.kind != TransferKind::Release || !(record.date == july))
      continue;
    ++exits[record.from_team];
    EXPECT_TRUE(std::ranges::contains(relegated, record.from_team))
        << "only relegated clubs lose players this way";
    EXPECT_TRUE(
        std::ranges::contains(first_team[record.from_team], record.player_id))
        << "the clause is for first-team players";
    EXPECT_EQ(record.fee, 0u) << "the clause frees the player at no cost";
    const Player& player = gamedata->getPlayer(record.player_id)->get();
    EXPECT_EQ(player.getTeamId(), FREE_AGENTS_TEAM_ID);
    EXPECT_LE(player.getAge(),
              TransferTuning::Market::RELEGATION_CLAUSE_MAX_AGE);
  }
  EXPECT_EQ(exits[managed], 0) << "the manager's contracts carry no clause";
  int total = 0;
  for (const auto& [club, count] : exits)
  {
    total += count;
    EXPECT_LE(count, TransferTuning::Market::RELEGATION_CLAUSE_MAX_EXITS);
    EXPECT_EQ(gamedata->getTeams().at(club).getFinances().getBalance(),
              balances[club])
        << "no severance is due";
  }
  // ~25% of 16 first-team players at 19 clubs, capped at three a club.
  EXPECT_GT(total, 19);
}

// ---------------------------------------------------------------------------
// Loan talks, wage budgets, release clauses, agents, renewals, squad floor
// and the deadline
// ---------------------------------------------------------------------------

namespace
{
/** A managed outfield player who may be lent or sold without breaking the
 * squad floor, and the richest other club, given room for his wage. */
struct LoanCase
{
  PlayerID player = 0;
  TeamID borrower = 0;
  std::uint32_t wage = 0;
};

LoanCase loanCase(const GameController& controller, TeamID managed)
{
  auto data = controller.getGameData();
  const TransferMarket& market = marketOf(controller);
  LoanCase found;
  for (const auto& reference : controller.getPlayersForTeam(managed))
  {
    const Player& player = reference.get();
    if (player.getRole() != PlayerRole::GK &&
        market.canBeTraded(player.getId()) && player.getContractYears() > 1 &&
        player.getWage() > 0 &&
        controller.getSaleBlock(player.getId()) ==
            GameController::PlayerActionBlock::None)
    {
      found.player = player.getId();
      found.wage = player.getWage();
      break;
    }
  }
  for (const auto& team : controller.getTeams())
    if (team.get().getId() != managed &&
        team.get().getId() != FREE_AGENTS_TEAM_ID &&
        controller.isTransferWindowOpenFor(team.get().getId()))
      found.borrower = team.get().getId();
  Finances& finances = data->getTeams().at(found.borrower).getFinances();
  finances.addBalance(200'000'000LL);
  finances.setWageBudget(controller.getWeeklyWageBill(found.borrower) +
                         10 * found.wage + 1'000'000);
  return found;
}

std::uint32_t addLoanOffer(GameController& controller, const LoanCase& loan,
                           std::uint8_t share, std::uint32_t ceiling)
{
  IncomingOffer offer;
  offer.player_id = loan.player;
  offer.buyer = loan.borrower;
  offer.loan = true;
  offer.loan_terms.wage_share = share;
  offer.max_fee = ceiling;
  offer.patience = 3;
  offer.created = controller.getCurrentDate();
  offer.expires = controller.getCurrentDate() + 5;
  return controller.getGame()->getTransfers().addIncomingOffer(offer);
}

/** Manages the smallest club whose transfer window is open today. */
TeamID manageOpenClub(GameController& controller)
{
  TeamID club = 0;
  int lowest = std::numeric_limits<int>::max();
  for (const auto& team : controller.getTeams())
  {
    const TeamID id = team.get().getId();
    if (id != FREE_AGENTS_TEAM_ID && controller.isTransferWindowOpenFor(id) &&
        team.get().getReputation() < lowest &&
        controller.getPlayersForTeam(id).size() > 16)
    {
      lowest = team.get().getReputation();
      club = id;
    }
  }
  controller.selectManagedTeam(club);
  return club;
}

bool inboxHas(const GameController& controller, const std::string& key)
{
  return std::ranges::any_of(
      controller.getInbox(), [&](const InboxMessage& message)
      { return message.title_key == key || message.body_key == key; });
}

/** Deadline day of the window open today for @p club, and the day after. */
std::pair<GameDateValue, GameDateValue> windowClose(
    const GameController& controller, TeamID club)
{
  const LeagueID league = controller.getTeamById(club)->get().getLeagueId();
  const GameDateValue last =
      TransferWindows::windowEnd(league, controller.getCurrentDate())
          .value_or(controller.getCurrentDate());
  return {last, last + 1};
}
}  // namespace

TEST_F(TransferMarketTest, LoanOfferIsNegotiatedAndItsTermsSurviveReload)
{
  const TeamID managed = manageOpenClub(*controller);
  ASSERT_TRUE(controller->isTransferWindowOpen());
  const LoanCase loan = loanCase(*controller, managed);
  ASSERT_NE(loan.player, 0u);
  const int weeks = TransferNegotiation::weeksBetween(
      controller->getCurrentDate(),
      TransferNegotiation::loanEndDate(
          controller->getCurrentDate(),
          TransferNegotiation::LoanDuration::SeasonEnd));
  // It would pay all of his wage over the season and a little more.
  const auto ceiling =
      static_cast<std::uint32_t>(loan.wage * 1.2 * std::max(1, weeks));
  // The fee for unplayed games is a few weeks of his wage.
  const std::uint32_t unplayed = (loan.wage * 4 / 1'000 + 1) * 1'000;
  const std::uint32_t offer_id = addLoanOffer(*controller, loan, 40, ceiling);
  const auto view = controller->getIncomingOfferView(offer_id);
  ASSERT_TRUE(view.has_value()) << "loan offers are negotiated in the talks";
  EXPECT_TRUE(view->loan);
  ASSERT_EQ(view->history.size(), 1u);
  EXPECT_EQ(view->history.front().loan_terms.wage_share, 40);

  TransferNegotiation::LoanTerms asked;
  asked.wage_share = 70;
  asked.recall_clause = true;
  asked.min_appearances = 5;
  asked.unplayed_fee = unplayed;
  asked.option_fee = 9'000'000;
  const OfferOutcome sent = controller->counterLoanOffer(offer_id, asked);
  ASSERT_TRUE(sent == OfferOutcome::AwaitingReply ||
              sent == OfferOutcome::Sold);
  if (sent == OfferOutcome::AwaitingReply)
  {
    const IncomingOffer before =
        *marketOf(*controller).findIncomingOffer(offer_id);
    EXPECT_EQ(before.status, OfferStatus::AwaitingBuyer);
    EXPECT_EQ(before.history.back().move, BuyerNegotiation::Move::Counter);
    controller->saveGame();
    controller = std::make_unique<GameController>();
    ASSERT_TRUE(controller->loadGame(0));
    const IncomingOffer* after =
        marketOf(*controller).findIncomingOffer(offer_id);
    ASSERT_NE(after, nullptr);
    EXPECT_TRUE(after->loan);
    EXPECT_EQ(after->max_fee, before.max_fee);
    EXPECT_EQ(after->patience, before.patience);
    EXPECT_EQ(after->respond_on, before.respond_on);
    EXPECT_EQ(after->asked_loan.wage_share, 70);
    EXPECT_TRUE(after->asked_loan.recall_clause);
    EXPECT_EQ(after->asked_loan.min_appearances, 5);
    EXPECT_EQ(after->asked_loan.unplayed_fee, unplayed);
    EXPECT_EQ(after->asked_loan.option_fee, 9'000'000u);
    ASSERT_EQ(after->history.size(), before.history.size());
    EXPECT_EQ(after->history.front().loan_terms.wage_share, 40);
    EXPECT_EQ(after->history.back().loan_terms.min_appearances, 5);
    waitForAnswer(*controller, offer_id);
  }
  // Within its ceiling and wage room: it agreed and he left on loan.
  auto data = controller->getGameData();
  EXPECT_EQ(data->getPlayer(loan.player)->get().getTeamId(), loan.borrower);
  const LoanDeal* found = marketOf(*controller).findLoan(loan.player);
  ASSERT_NE(found, nullptr);
  // A copy: the controller (and its market) is replaced on reload below.
  const LoanDeal deal = *found;
  EXPECT_EQ(deal.wage_share, 70);
  EXPECT_TRUE(deal.recall_clause);
  EXPECT_EQ(deal.option_fee, 9'000'000u);
  const auto clause = std::ranges::find(marketOf(*controller).obligations(),
                                        ObligationKind::LoanUnplayedFee,
                                        &TransferObligation::kind);
  ASSERT_NE(clause, marketOf(*controller).obligations().end());
  EXPECT_EQ(clause->target, 5);
  EXPECT_EQ(clause->amount, unplayed);

  // The clause survives a reload and is paid when he played too little.
  controller->saveGame();
  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(0));
  MarketHarness harness(*controller);
  data = controller->getGameData();
  const int64_t received =
      categoryTotal(data->getTeams().at(managed).getFinances(),
                    FinanceCategory::TransferFeeIn);
  harness.market.onDayAdvanced(deal.end, managed);
  EXPECT_EQ(data->getPlayer(loan.player)->get().getTeamId(), managed);
  EXPECT_EQ(categoryTotal(data->getTeams().at(managed).getFinances(),
                          FinanceCategory::TransferFeeIn) -
                received,
            unplayed);
  EXPECT_TRUE(std::ranges::any_of(
      harness.world.getInbox().getMessages(), [](const InboxMessage& message)
      { return message.title_key == "INBOX_LOAN_UNPLAYED_FEE_TITLE"; }));
}

TEST_F(TransferMarketTest, BorrowersAndBuyersStayWithinTheirWageBudget)
{
  const TeamID managed = manageOpenClub(*controller);
  const LoanCase loan = loanCase(*controller, managed);
  ASSERT_NE(loan.player, 0u);
  auto data = controller->getGameData();
  // Room for only 30% of his wage: the full wage is never agreed.
  data->getTeams()
      .at(loan.borrower)
      .getFinances()
      .setWageBudget(controller->getWeeklyWageBill(loan.borrower) +
                     loan.wage * 3 / 10);
  const std::uint32_t offer_id =
      addLoanOffer(*controller, loan, 20, 1'000'000'000);
  TransferNegotiation::LoanTerms asked;
  asked.wage_share = 100;
  if (const OfferOutcome sent = controller->counterLoanOffer(offer_id, asked);
      sent == OfferOutcome::AwaitingReply)
    waitForAnswer(*controller, offer_id);
  EXPECT_NE(data->getPlayer(loan.player)->get().getTeamId(), loan.borrower);
  const IncomingOffer* countered =
      marketOf(*controller).findIncomingOffer(offer_id);
  ASSERT_NE(countered, nullptr);
  EXPECT_EQ(countered->status, OfferStatus::AwaitingClub);
  EXPECT_LE(countered->loan_terms.wage_share, 30);

  // A buyer whose wage budget no longer fits his wage walks away.
  const KeenOffer keen =
      openKeenOffer(*controller, managed, 1'000'000, 1'500'000);
  ASSERT_NE(keen.offer_id, 0u);
  data->getTeams().at(keen.buyer).getFinances().setWageBudget(0);
  TransferNegotiation::OfferTerms counter;
  counter.fee = 1'100'000;
  if (const OfferOutcome answer =
          controller->counterIncomingOffer(keen.offer_id, counter);
      answer == OfferOutcome::AwaitingReply)
    waitForAnswer(*controller, keen.offer_id);
  EXPECT_EQ(marketOf(*controller).findIncomingOffer(keen.offer_id), nullptr);
  EXPECT_NE(data->getPlayer(keen.player)->get().getTeamId(), keen.buyer);
  EXPECT_TRUE(inboxHas(*controller, "INBOX_OFFER_WITHDRAWN_WAGES_BODY"));
}

TEST_F(TransferMarketTest, ReleaseClausesArePaidByTheClubAndByAiClubs)
{
  const TeamID managed = manageOpenClub(*controller);
  auto data = controller->getGameData();
  TransferMarket& market = controller->getGame()->getTransfers();
  data->getTeams().at(managed).getFinances().addBalance(500'000'000LL);
  data->getTeams().at(managed).getFinances().setWageBudget(
      controller->getWeeklyWageBill(managed) + 1'000'000);

  // The managed club pays a target's clause: no talks with his club.
  const PlayerID target = findWillingTarget(*controller, managed);
  ASSERT_NE(target, 0u);
  const uint32_t clause = controller->getPlayerMarketValue(target) * 2;
  market.setReleaseClause(target, clause);
  EXPECT_EQ(controller->getReleaseClause(target), clause);
  const ClubResponse paid = controller->payReleaseClause(target);
  ASSERT_EQ(paid.decision, ClubResponse::Decision::Accept);
  EXPECT_TRUE(std::ranges::contains(
      paid.reasons, TransferNegotiation::Reason::ReleaseClauseMet));
  ASSERT_EQ(controller->getContractTalkKind(target), ContractKind::Transfer);
  const auto demands =
      controller->getAgentDemands(target, ContractKind::Transfer, 3);
  EXPECT_EQ(demands.standard_agent_fee, clause / 10)
      << "the agent's usual fee follows the clause paid";
  TransferNegotiation::ContractOffer offer = TransferNegotiation::demandedOffer(
      controller->getPlayerDemand(target, ContractKind::Transfer));
  const auto signed_up = controller->proposeContract(target, offer);
  ASSERT_TRUE(signed_up.completed)
      << (signed_up.response.reasons.empty()
              ? "no reason"
              : TransferNegotiation::reasonKey(
                    signed_up.response.reasons.front()));
  EXPECT_EQ(data->getPlayer(target)->get().getTeamId(), managed);
  EXPECT_EQ(market.history().back().fee, clause);

  // An AI club pays a managed player's clause: the club cannot refuse.
  const LoanCase sale = loanCase(*controller, managed);
  ASSERT_NE(sale.player, 0u);
  market.setReleaseClause(sale.player, 1'000'000);
  ASSERT_TRUE(market.payReleaseClause(sale.borrower, sale.player,
                                      controller->getCurrentDate(), managed));
  EXPECT_EQ(data->getPlayer(sale.player)->get().getTeamId(), sale.borrower);
  EXPECT_EQ(market.history().back().fee, 1'000'000u);
  EXPECT_TRUE(inboxHas(*controller, "INBOX_RELEASE_CLAUSE_PAID_TITLE"));
  EXPECT_EQ(market.releaseClause(sale.player), 0u)
      << "a new contract, no clause";
}

TEST_F(TransferMarketTest, RenewalsExtendTheContractAndPayItsExtras)
{
  const TeamID managed = manageFirstClub(*controller);
  auto data = controller->getGameData();
  Finances& finances = data->getTeams().at(managed).getFinances();
  finances.addBalance(100'000'000LL);
  finances.setWageBudget(controller->getWeeklyWageBill(managed) + 1'000'000);
  // His club's best player whose contract can still be extended.
  PlayerID own = 0;
  double best = -1.0;
  const StatsConfig& config = data->getStatsConfig();
  for (const auto& reference : controller->getPlayersForTeam(managed))
    if (controller->getRenewalBlock(reference.get().getId()) ==
            GameController::PlayerActionBlock::None &&
        reference.get().getOverall(config) > best)
    {
      best = reference.get().getOverall(config);
      own = reference.get().getId();
    }
  ASSERT_NE(own, 0u);
  Player& player = data->getPlayers().at(own);
  ASSERT_EQ(controller->getContractTalkKind(own), ContractKind::Renewal);
  const std::uint8_t before = player.getContractYears();

  // A renewal is priced on what players of his level earn at the club.
  const std::uint32_t deserved =
      marketOf(*controller).deservedWage(own, managed);
  ASSERT_GT(deserved, 1'000u);
  player.setWage(deserved / 4);
  const auto demand = controller->getPlayerDemand(own, ContractKind::Renewal);
  EXPECT_GE(demand.weekly_wage, deserved) << "not his old wage x 1.10";

  TransferNegotiation::ContractOffer offer =
      TransferNegotiation::demandedOffer(demand);
  offer.years = before;
  EXPECT_EQ(controller->proposeContract(own, offer).block,
            GameController::PlayerActionBlock::NotLonger)
      << "the same length would not extend it";
  offer.years = static_cast<std::uint8_t>(
      TransferNegotiation::maxContractYears(player.getAge()) + 1);
  EXPECT_EQ(controller->proposeContract(own, offer).block,
            GameController::PlayerActionBlock::TooLong);
  EXPECT_EQ(controller->getContractRoundsLeft(own),
            TransferTuning::Negotiation::MAX_PLAYER_ROUNDS)
      << "invalid proposals use no round";

  offer.years = static_cast<std::uint8_t>(before + 1);
  offer.weekly_wage = demand.weekly_wage + 1'000;
  offer.yearly_rise = 5;
  offer.appearance_bonus = 1'000;
  const auto renewed = controller->proposeContract(own, offer);
  ASSERT_TRUE(renewed.completed);
  EXPECT_STREQ(renewed.agent_line, "AGENT_LINE_AGREED");
  EXPECT_EQ(player.getContractYears(), before + 1) << "one more season";
  EXPECT_EQ(player.getWage(), offer.weekly_wage);
  const auto& obligations = marketOf(*controller).obligations();
  EXPECT_TRUE(std::ranges::any_of(obligations,
                                  [&](const TransferObligation& o)
                                  {
                                    return o.kind == ObligationKind::WageRise &&
                                           o.player_id == own && o.amount == 5;
                                  }));
  EXPECT_TRUE(std::ranges::any_of(obligations,
                                  [&](const TransferObligation& o)
                                  {
                                    return o.kind ==
                                               ObligationKind::AppearanceFee &&
                                           o.player_id == own &&
                                           o.amount == 1'000;
                                  }));

  // The extras survive a reload and the rise comes a year later.
  const GameDateValue today = controller->getCurrentDate();
  controller->saveGame();
  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(0));
  MarketHarness harness(*controller);
  data = controller->getGameData();
  harness.market.onDayAdvanced(today + 364, managed);
  EXPECT_EQ(data->getPlayer(own)->get().getWage(), offer.weekly_wage);
  harness.market.onDayAdvanced(today + 365, managed);
  EXPECT_EQ(data->getPlayer(own)->get().getWage(),
            offer.weekly_wage * 105 / 100);
}

TEST_F(TransferMarketTest, TheSquadNeverDropsBelowElevenOrLosesItsGoalkeeper)
{
  const TeamID managed = manageOpenClub(*controller);
  auto data = controller->getGameData();
  const auto seniors = [&]
  {
    int count = 0;
    for (const PlayerID id : data->getTeams().at(managed).getPlayerIDs())
      if (!data->getPlayer(id)->get().isAcademyPlayer()) ++count;
    return count;
  };
  // Release every goalkeeper but one, then outfield players down to eleven.
  std::vector<PlayerID> keepers;
  std::vector<PlayerID> outfield;
  for (const PlayerID id : data->getTeams().at(managed).getPlayerIDs())
  {
    const Player& player = data->getPlayer(id)->get();
    if (player.isAcademyPlayer()) continue;
    (player.getRole() == PlayerRole::GK ? keepers : outfield).push_back(id);
  }
  ASSERT_FALSE(keepers.empty());
  for (std::size_t index = 1; index < keepers.size(); ++index)
    controller->releasePlayer(keepers[index]);
  EXPECT_EQ(controller->getReleaseBlock(keepers.front()),
            GameController::PlayerActionBlock::LastGoalkeeper);
  EXPECT_FALSE(controller->releasePlayer(keepers.front()));
  for (const PlayerID id : outfield)
    if (seniors() > 11) controller->releasePlayer(id);
  ASSERT_EQ(seniors(), 11);
  const PlayerID last = std::ranges::find_if(
      outfield, [&](PlayerID id)
      { return data->getPlayer(id)->get().getTeamId() == managed; })[0];
  EXPECT_EQ(controller->getReleaseBlock(last),
            GameController::PlayerActionBlock::SquadFloor);
  EXPECT_FALSE(controller->releasePlayer(last));
  EXPECT_EQ(seniors(), 11);

  // A bid for him cannot be accepted either; it stays on the table.
  IncomingOffer bid;
  bid.player_id = last;
  for (const auto& team : controller->getTeams())
    if (team.get().getId() != managed &&
        team.get().getId() != FREE_AGENTS_TEAM_ID)
      bid.buyer = team.get().getId();
  bid.terms.fee = 50'000'000;
  bid.created = controller->getCurrentDate();
  bid.expires = controller->getCurrentDate() + 5;
  const std::uint32_t id =
      controller->getGame()->getTransfers().addIncomingOffer(bid);
  EXPECT_EQ(controller->getIncomingOfferView(id)->sale_block,
            GameController::PlayerActionBlock::SquadFloor);
  EXPECT_EQ(controller->settleIncomingOffer(id), OfferOutcome::SquadTooSmall);
  EXPECT_EQ(data->getPlayer(last)->get().getTeamId(), managed);
  EXPECT_NE(marketOf(*controller).findIncomingOffer(id), nullptr);
}

TEST_F(TransferMarketTest, DeadlineDaysAreBusierAndTheCloseIsSummedUp)
{
  using TransferNegotiation::WindowInfo;
  using M = TransferTuning::Market;
  EXPECT_FLOAT_EQ(
      TransferNegotiation::activityWeight(WindowInfo{true, false, 0}),
      M::DEADLINE_DAY_WEIGHT);
  EXPECT_FLOAT_EQ(
      TransferNegotiation::activityWeight(WindowInfo{true, false, 1}),
      M::DEADLINE_EVE_WEIGHT);
  EXPECT_LT(TransferNegotiation::activityWeight(WindowInfo{true, false, 2}),
            M::DEADLINE_EVE_WEIGHT);
  EXPECT_EQ(BuyerNegotiation::replyDelay(0.99, 1), 0) << "same-day answers";

  const TeamID managed = manageOpenClub(*controller);
  const LoanCase loan = loanCase(*controller, managed);
  ASSERT_NE(loan.player, 0u);
  controller->saveGame();
  MarketHarness harness(*controller);
  const auto [last_day, closed] = windowClose(*controller, managed);
  TransferNegotiation::LoanTerms terms;
  terms.wage_share = 50;
  ASSERT_TRUE(harness.market.startLoan(loan.player, loan.borrower, terms,
                                       last_day, managed));
  harness.market.onDayAdvanced(closed, managed);
  const auto& messages = harness.world.getInbox().getMessages();
  const auto summary = std::ranges::find(
      messages, "INBOX_DEADLINE_SUMMARY_TITLE", &InboxMessage::title_key);
  ASSERT_NE(summary, messages.end());
  ASSERT_GE(summary->args.size(), 6u);
  EXPECT_NE(
      summary->args[5].find(
          controller->getGameData()->getPlayer(loan.player)->get().getName()),
      std::string::npos)
      << "the club's own deadline business is listed";
}

TEST_F(TransferMarketTest, PreContractExtrasAndAgentFeeWaitForFirstJuly)
{
  controller->saveGame();
  const TeamID managed = manageFirstClub(*controller);
  auto gamedata = controller->getGameData();
  const TeamID owner = controller->getTeams()[8].get().getId();
  const PlayerID player =
      controller->getPlayersForTeam(owner).front().get().getId();
  gamedata->getPlayers().at(player).setContractYears(1);
  TransferNegotiation::ContractOffer terms;
  terms.weekly_wage = 10'000;
  terms.years = 3;
  terms.signing_bonus = 40'000;
  terms.yearly_rise = 8;
  terms.appearance_bonus = 500;
  terms.agent_fee = 25'000;  // Below the usual eight weeks of wage.
  {
    MarketHarness harness(*controller);
    ASSERT_TRUE(harness.market.agreePreContract(
        player, managed, terms, GameDateValue(2026, 1, 5), managed));
    harness.market.save(controller->getDbConn());
  }
  MarketHarness reloaded(*controller);
  const auto& obligations = reloaded.market.obligations();
  const auto kinds = [&](ObligationKind kind)
  {
    return std::ranges::count_if(
        obligations, [&](const TransferObligation& o)
        { return o.kind == kind && o.player_id == player; });
  };
  EXPECT_EQ(kinds(ObligationKind::AgentFee), 1);
  EXPECT_EQ(kinds(ObligationKind::WageRise), 1);
  EXPECT_EQ(kinds(ObligationKind::AppearanceFee), 1);
  const int64_t balance =
      gamedata->getTeams().at(managed).getFinances().getBalance();
  reloaded.market.onDayAdvanced(GameDateValue(2026, 7, 1), managed);
  ASSERT_EQ(gamedata->getPlayer(player)->get().getTeamId(), managed);
  // The bonus and the agreed agent fee, not the standard one.
  EXPECT_EQ(
      balance - gamedata->getTeams().at(managed).getFinances().getBalance(),
      40'000 + 25'000);
  EXPECT_EQ(kinds(ObligationKind::AgentFee), 0);
  EXPECT_EQ(kinds(ObligationKind::WageRise), 1) << "his contract's rise stays";
  reloaded.market.onDayAdvanced(GameDateValue(2027, 7, 1), managed);
  EXPECT_EQ(gamedata->getPlayer(player)->get().getWage(), 10'800u)
      << "8% a year after joining";
}
