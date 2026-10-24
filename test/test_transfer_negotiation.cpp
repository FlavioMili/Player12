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

#include "model/transfer_negotiation.h"
#include "model/transfer_tuning.h"
#include "model/world_simulation.h"

using namespace TransferNegotiation;

namespace
{
bool has(const std::vector<Reason>& reasons, Reason reason)
{
  return std::ranges::find(reasons, reason) != reasons.end();
}

SaleContext rotationPlayer()
{
  SaleContext context;
  context.market_value = 10'000'000;
  context.age = 25;
  context.role = SquadRole::Rotation;
  context.contract_years = 3;
  context.seller_reputation = 60;
  context.buyer_reputation = 60;
  return context;
}

PlayerContext transferTarget()
{
  PlayerContext context;
  context.kind = ContractKind::Transfer;
  context.age = 26;
  context.current_wage = 20'000;
  context.market_value = 8'000'000;
  context.ambition = 50;
  context.loyalty = 50;
  context.current_club_reputation = 60;
  context.new_club_reputation = 60;
  context.current_role = SquadRole::FirstTeam;
  context.projected_role = SquadRole::FirstTeam;
  return context;
}
}  // namespace

TEST(TransferNegotiationTest, AskingFeeReflectsRoleRivalryAndTiming)
{
  SaleContext context = rotationPlayer();
  const Valuation rotation = valueForSale(context);
  const std::uint32_t base = rotation.asking_fee;
  EXPECT_GT(base, 11'000'000u) << "nobody sells a squad player at value";
  EXPECT_LT(base, 14'000'000u);
  EXPECT_TRUE(has(rotation.reasons, Reason::LongContract));

  context.role = SquadRole::KeyPlayer;
  const Valuation key = valueForSale(context);
  EXPECT_GT(key.asking_fee, base);
  EXPECT_TRUE(has(key.reasons, Reason::KeyPlayer));

  context.same_league = true;
  const Valuation rival = valueForSale(context);
  EXPECT_GT(rival.asking_fee, key.asking_fee);
  EXPECT_TRUE(has(rival.reasons, Reason::SellingToRival));

  context.winter_window = true;
  EXPECT_GT(valueForSale(context).asking_fee, rival.asking_fee)
      << "regular starters cost more mid-season";

  context = rotationPlayer();
  context.role = SquadRole::Fringe;
  EXPECT_LT(valueForSale(context).asking_fee, base);

  context = rotationPlayer();
  context.buyer_reputation = 85;
  const Valuation richer = valueForSale(context);
  EXPECT_GT(richer.asking_fee, base);
  EXPECT_TRUE(has(richer.reasons, Reason::RicherBuyer));
}

TEST(TransferNegotiationTest, ListedPriceClauseAndNotForSale)
{
  SaleContext context = rotationPlayer();
  context.listed = true;
  context.listing_price = 4'000'000;
  EXPECT_EQ(valueForSale(context).asking_fee, 4'000'000u);

  context = rotationPlayer();
  context.role = SquadRole::KeyPlayer;
  context.seller_reputation = 80;
  context.buyer_reputation = 60;
  const Valuation protected_star = valueForSale(context);
  EXPECT_TRUE(protected_star.not_for_sale);

  OfferTerms huge;
  huge.fee = 200'000'000;
  EXPECT_EQ(evaluateOffer(context, huge, 0).decision,
            ClubResponse::Decision::Reject);

  // A release clause cannot be refused when paid in full up front.
  context.release_clause = 30'000'000;
  OfferTerms clause;
  clause.fee = 30'000'000;
  const ClubResponse forced = evaluateOffer(context, clause, 0);
  EXPECT_EQ(forced.decision, ClubResponse::Decision::Accept);
  EXPECT_TRUE(has(forced.reasons, Reason::ReleaseClauseMet));
  clause.upfront_percent = 50;
  clause.instalment_years = 2;
  EXPECT_NE(evaluateOffer(context, clause, 0).decision,
            ClubResponse::Decision::Accept)
      << "clauses are paid up front";
}

TEST(TransferNegotiationTest, StructuredOffersAreValuedAndCountered)
{
  OfferTerms cash;
  cash.fee = 10'000'000;
  OfferTerms deferred = cash;
  deferred.upfront_percent = 40;
  deferred.instalment_years = 3;
  EXPECT_LT(sellerValue(deferred, 25), sellerValue(cash, 25));

  OfferTerms add_ons = cash;
  add_ons.appearance_bonus = 50'000'000;
  add_ons.appearance_target = 30;
  EXPECT_DOUBLE_EQ(
      sellerValue(add_ons, 25),
      10'000'000.0 * (1.0 + TransferTuning::Offer::ADD_ON_CREDIT_CAP))
      << "add-on credit is capped";

  OfferTerms sell_on = cash;
  sell_on.sell_on_percent = 20;
  EXPECT_GT(sellerValue(sell_on, 20), sellerValue(sell_on, 30))
      << "sell-on clauses are worth more for young players";

  const SaleContext context = rotationPlayer();
  const ClubResponse opening = evaluateOffer(context, cash, 0);
  ASSERT_EQ(opening.decision, ClubResponse::Decision::Counter)
      << "a first bid at market value is countered, not accepted";
  EXPECT_TRUE(has(opening.reasons, Reason::OpeningBidLow));
  EXPECT_EQ(opening.counter_fee, valueForSale(context).asking_fee);

  OfferTerms low = cash;
  low.fee = 8'000'000;
  const ClubResponse counter = evaluateOffer(context, low, 0);
  ASSERT_EQ(counter.decision, ClubResponse::Decision::Counter);
  EXPECT_GE(counter.counter_fee, 10'000'000u);
  low.fee = counter.counter_fee;
  EXPECT_EQ(evaluateOffer(context, low, 1).decision,
            ClubResponse::Decision::Accept)
      << "meeting the counter closes the deal";
  OfferTerms asking = cash;
  asking.fee = valueForSale(context).asking_fee;
  EXPECT_EQ(evaluateOffer(context, asking, 0).decision,
            ClubResponse::Decision::Accept)
      << "an opening bid at the valuation is accepted";

  deferred.fee = 10'000'000;
  const ClubResponse deferred_counter = evaluateOffer(context, deferred, 0);
  ASSERT_EQ(deferred_counter.decision, ClubResponse::Decision::Counter);
  EXPECT_GT(deferred_counter.counter_fee, 10'000'000u)
      << "instalments raise the asked fee";

  OfferTerms insulting = cash;
  insulting.fee = 3'000'000;
  const ClubResponse rejected = evaluateOffer(context, insulting, 0);
  EXPECT_EQ(rejected.decision, ClubResponse::Decision::Reject);
  EXPECT_TRUE(has(rejected.reasons, Reason::OfferTooLow));

  const ClubResponse broken =
      evaluateOffer(context, OfferTerms{.fee = 8'000'000},
                    TransferTuning::Offer::MAX_CLUB_ROUNDS - 1);
  EXPECT_EQ(broken.decision, ClubResponse::Decision::Reject);
  EXPECT_TRUE(has(broken.reasons, Reason::TalksBroken));
}

TEST(TransferNegotiationTest, InstalmentScheduleAndValidity)
{
  OfferTerms terms;
  terms.fee = 10'000'001;
  terms.upfront_percent = 40;
  terms.instalment_years = 3;
  ASSERT_TRUE(isValid(terms));
  const auto instalments = instalmentAmounts(terms);
  ASSERT_EQ(instalments.size(), 3u);
  EXPECT_EQ(upfrontAmount(terms) +
                std::accumulate(instalments.begin(), instalments.end(), 0u),
            terms.fee);

  OfferTerms invalid = terms;
  invalid.instalment_years = 0;
  EXPECT_FALSE(isValid(invalid)) << "deferred money needs instalments";
  invalid = terms;
  invalid.upfront_percent = 5;
  EXPECT_FALSE(isValid(invalid));
  invalid = terms;
  invalid.goal_bonus = 100'000;
  EXPECT_FALSE(isValid(invalid)) << "a bonus needs a target";

  EXPECT_EQ(aiOfferTerms(1'000'000).instalment_years, 0);
  EXPECT_GT(aiOfferTerms(30'000'000).instalment_years, 0);
}

TEST(TransferNegotiationTest, PlayerWeighsWageStatureAndPlayingTime)
{
  PlayerContext context = transferTarget();
  const ContractDemand demand = contractDemand(context);
  EXPECT_GT(demand.weekly_wage, context.current_wage);
  EXPECT_EQ(demand.min_years,
            TransferTuning::Contract::PRIME_PLAYER_MINIMUM_YEARS);

  ContractOffer offer = demandedOffer(demand);
  EXPECT_TRUE(evaluateContract(context, offer, 0).accepted);

  ContractOffer cheap = offer;
  cheap.weekly_wage = offer.weekly_wage / 2;
  const ContractResponse refused = evaluateContract(context, cheap, 0);
  EXPECT_FALSE(refused.accepted);
  EXPECT_TRUE(has(refused.reasons, Reason::WageTooLow));

  // A signing bonus can make up for a lower wage.
  ContractOffer bonus = offer;
  bonus.weekly_wage = offer.weekly_wage - 1'000;
  bonus.signing_bonus = 1'000U * 52U * offer.years;
  EXPECT_TRUE(evaluateContract(context, bonus, 0).accepted);

  // Stepping down: ambitious players ask more, or refuse a much smaller club.
  PlayerContext stepping_down = context;
  stepping_down.ambition = 80;
  stepping_down.current_club_reputation = 80;
  stepping_down.new_club_reputation = 65;
  EXPECT_GT(contractDemand(stepping_down).weekly_wage,
            contractDemand(context).weekly_wage);
  stepping_down.new_club_reputation = 50;
  const ContractResponse too_small = evaluateContract(
      stepping_down, demandedOffer(contractDemand(stepping_down)), 0);
  EXPECT_FALSE(too_small.accepted);
  EXPECT_TRUE(has(too_small.reasons, Reason::ClubTooSmall));

  // Playing time: two levels below the desired role is refused; a credible
  // promise bridges one level, an incredible one is noted.
  PlayerContext bench = context;
  bench.projected_role = SquadRole::Backup;
  const ContractOffer bench_offer = demandedOffer(contractDemand(bench));
  const ContractResponse benched = evaluateContract(bench, bench_offer, 0);
  EXPECT_FALSE(benched.accepted);
  EXPECT_TRUE(has(benched.reasons, Reason::PlayingTime));
  ContractOffer promised = bench_offer;
  promised.promised_role = SquadRole::Rotation;
  EXPECT_TRUE(evaluateContract(bench, promised, 0).accepted);
  promised.promised_role = SquadRole::KeyPlayer;
  const ContractResponse doubtful = evaluateContract(bench, promised, 0);
  EXPECT_TRUE(doubtful.accepted);
  EXPECT_TRUE(has(doubtful.reasons, Reason::PromiseNotCredible));
}

TEST(TransferNegotiationTest, ContractLengthClausesAndPatience)
{
  PlayerContext minor = transferTarget();
  minor.age = 17;
  EXPECT_EQ(maxContractYears(17), 3);
  ContractOffer offer = demandedOffer(contractDemand(minor));
  offer.years = 4;
  EXPECT_TRUE(
      has(evaluateContract(minor, offer, 0).reasons, Reason::ContractTooLong));
  offer.years = 1;
  EXPECT_TRUE(
      has(evaluateContract(minor, offer, 0).reasons, Reason::ContractTooShort));

  PlayerContext ambitious = transferTarget();
  ambitious.ambition = 85;
  ambitious.current_club_reputation = 60;
  ambitious.new_club_reputation = 62;
  const ContractDemand demand = contractDemand(ambitious);
  ASSERT_TRUE(demand.wants_release_clause);
  ContractOffer no_clause = demandedOffer(demand);
  no_clause.release_clause = 0;
  const ContractResponse refused = evaluateContract(ambitious, no_clause, 0);
  EXPECT_FALSE(refused.accepted);
  EXPECT_TRUE(has(refused.reasons, Reason::WantsReleaseClause));
  EXPECT_TRUE(evaluateContract(ambitious, demandedOffer(demand), 0).accepted);

  const ContractResponse last = evaluateContract(
      ambitious, no_clause, TransferTuning::Negotiation::MAX_PLAYER_ROUNDS - 1);
  EXPECT_TRUE(has(last.reasons, Reason::TalksEnded));

  PlayerContext free_agent = transferTarget();
  free_agent.kind = ContractKind::FreeAgent;
  free_agent.current_club_reputation = 0;
  EXPECT_GT(contractDemand(free_agent).signing_bonus, 0u)
      << "free agents ask for a signing bonus instead of a fee";
}

TEST(TransferNegotiationTest, LoanTermsAndPlayerAgreement)
{
  LoanContext context;
  context.market_value = 2'000'000;
  context.weekly_wage = 10'000;
  context.age = 20;
  context.role = SquadRole::FirstTeam;
  context.weeks = 40;

  LoanTerms terms;
  terms.wage_share = 100;
  const ClubResponse key = evaluateLoan(context, terms);
  EXPECT_EQ(key.decision, ClubResponse::Decision::Reject);
  EXPECT_TRUE(has(key.reasons, Reason::NotForLoan));

  context.role = SquadRole::Fringe;
  terms.wage_share = 30;
  const ClubResponse counter = evaluateLoan(context, terms);
  ASSERT_EQ(counter.decision, ClubResponse::Decision::Counter);
  EXPECT_EQ(counter.counter_wage_share,
            TransferTuning::Loan::UNLISTED_WAGE_SHARE);
  terms.wage_share = counter.counter_wage_share;
  EXPECT_EQ(evaluateLoan(context, terms).decision,
            ClubResponse::Decision::Accept);

  context.loan_listed = true;
  terms.wage_share = TransferTuning::Loan::LISTED_WAGE_SHARE;
  terms.option_fee = 1'000'000;
  const ClubResponse option = evaluateLoan(context, terms);
  ASSERT_EQ(option.decision, ClubResponse::Decision::Counter);
  EXPECT_TRUE(has(option.reasons, Reason::OptionTooLow));
  EXPECT_GE(option.counter_option_fee, 2'200'000u);

  context.within_limits = false;
  EXPECT_TRUE(has(evaluateLoan(context, terms).reasons, Reason::LoanLimit));

  std::vector<Reason> reasons;
  EXPECT_TRUE(playerAcceptsLoan(SquadRole::Fringe, SquadRole::Rotation, 50, 10,
                                reasons));
  EXPECT_FALSE(playerAcceptsLoan(SquadRole::Rotation, SquadRole::Backup, 50, 0,
                                 reasons));
  EXPECT_TRUE(has(reasons, Reason::NoPlayingTimeOnLoan));
}

TEST(TransferNegotiationTest, WindowsDeadlinesAndContractCalendar)
{
  // An Italian club: the summer window shuts on 1 September.
  constexpr LeagueID ITALY = 1;
  const WindowInfo summer = windowInfo(ITALY, GameDateValue(2025, 8, 25));
  EXPECT_TRUE(summer.open);
  EXPECT_FALSE(summer.winter);
  EXPECT_EQ(summer.days_to_deadline, 7);
  const WindowInfo deadline = windowInfo(ITALY, GameDateValue(2025, 9, 1));
  EXPECT_TRUE(deadline.open);
  EXPECT_EQ(deadline.days_to_deadline, 0);
  EXPECT_GT(activityWeight(deadline), activityWeight(summer));
  EXPECT_GT(activityWeight(summer),
            activityWeight(windowInfo(ITALY, GameDateValue(2025, 7, 10))));
  EXPECT_TRUE(windowInfo(ITALY, GameDateValue(2026, 1, 15)).winter);
  const WindowInfo closed = windowInfo(ITALY, GameDateValue(2025, 10, 1));
  EXPECT_FALSE(closed.open);
  EXPECT_EQ(activityWeight(closed), 0.0f);

  EXPECT_TRUE(canSignPreContract(1, GameDateValue(2026, 1, 1)));
  EXPECT_FALSE(canSignPreContract(1, GameDateValue(2025, 12, 31)));
  EXPECT_FALSE(canSignPreContract(2, GameDateValue(2026, 3, 1)));

  EXPECT_EQ(loanEndDate(GameDateValue(2025, 7, 10), LoanDuration::SeasonEnd),
            GameDateValue(2026, 6, 30));
  EXPECT_EQ(loanEndDate(GameDateValue(2025, 6, 10), LoanDuration::SeasonEnd),
            GameDateValue(2026, 6, 30));
  EXPECT_EQ(loanEndDate(GameDateValue(2025, 8, 1), LoanDuration::SixMonths),
            GameDateValue(2026, 1, 30));
  EXPECT_EQ(loanEndDate(GameDateValue(2026, 1, 20), LoanDuration::SixMonths),
            GameDateValue(2026, 6, 30));

  EXPECT_EQ(severancePay(1'000, 0, GameDateValue(2025, 9, 1)), 0);
  EXPECT_EQ(severancePay(1'000, 1, GameDateValue(2026, 6, 2)), 4'000);
  EXPECT_EQ(severancePay(1'000, 3, GameDateValue(2026, 6, 2)),
            4'000 + 2 * 52 * 1'000);
}

TEST(TransferNegotiationTest, OpeningBidsAtValueAreCounteredForEveryRole)
{
  using enum SquadRole;
  for (const SquadRole role : {KeyPlayer, FirstTeam, Rotation, Backup, Fringe})
  {
    SaleContext context = rotationPlayer();
    context.role = role;
    context.contract_years = 2;
    OfferTerms at_value;
    at_value.fee = context.market_value;
    const ClubResponse response = evaluateOffer(context, at_value, 0);
    ASSERT_EQ(response.decision, ClubResponse::Decision::Counter)
        << static_cast<int>(std::to_underlying(role));
    const double premium = static_cast<double>(response.counter_fee) /
                           static_cast<double>(context.market_value);
    EXPECT_GE(premium, 1.07) << static_cast<int>(std::to_underlying(role));
    EXPECT_LE(premium, 1.41) << static_cast<int>(std::to_underlying(role));
    EXPECT_FALSE(response.reasons.empty());

    // AI buyers bid the valuation in cash and must still be accepted.
    context.market_value = 3'000'000;
    const OfferTerms ai = aiOfferTerms(valueForSale(context).asking_fee);
    ASSERT_EQ(ai.instalment_years, 0);
    EXPECT_EQ(evaluateOffer(context, ai, 0).decision,
              ClubResponse::Decision::Accept)
        << static_cast<int>(std::to_underlying(role));
  }
}

TEST(TransferNegotiationTest, ContractLengthMovesTheValuation)
{
  SaleContext context = rotationPlayer();
  context.contract_years = 1;
  const Valuation expiring = valueForSale(context);
  EXPECT_TRUE(has(expiring.reasons, Reason::ExpiringContract));
  context.contract_years = 2;
  const Valuation normal = valueForSale(context);
  context.contract_years = 4;
  const Valuation tied = valueForSale(context);
  EXPECT_TRUE(has(tied.reasons, Reason::LongContract));
  EXPECT_LT(expiring.asking_fee, normal.asking_fee);
  EXPECT_LT(normal.asking_fee, tied.asking_fee);
}

TEST(TransferNegotiationTest, StubbornClubsRefuseToSellKeyPlayers)
{
  SaleContext context = rotationPlayer();
  context.role = SquadRole::KeyPlayer;
  context.resolve = 0.95f;
  const Valuation refused = valueForSale(context);
  EXPECT_TRUE(refused.not_for_sale);
  EXPECT_TRUE(has(refused.reasons, Reason::KeyPlayerNotForSale));
  OfferTerms huge;
  huge.fee = 100'000'000;
  EXPECT_EQ(evaluateOffer(context, huge, 0).decision,
            ClubResponse::Decision::Reject);

  SaleContext calm = context;
  calm.resolve = 0.2f;
  EXPECT_FALSE(valueForSale(calm).not_for_sale);
  SaleContext bigger_buyer = context;
  bigger_buyer.buyer_reputation = 75;
  EXPECT_FALSE(valueForSale(bigger_buyer).not_for_sale)
      << "a clearly bigger club can still prise him away";
  SaleContext expiring = context;
  expiring.contract_years = 1;
  EXPECT_FALSE(valueForSale(expiring).not_for_sale);
  SaleContext squad_player = context;
  squad_player.role = SquadRole::Rotation;
  EXPECT_FALSE(valueForSale(squad_player).not_for_sale);
}

TEST(TransferNegotiationTest, AgentOpensHighAndSoftensEachRound)
{
  PlayerContext context = transferTarget();
  const ContractDemand demand = contractDemand(context);
  EXPECT_GT(demand.asking_wage, demand.weekly_wage);
  EXPECT_EQ(demand.asking_wage, agentAsk(context, demand, 0));
  std::uint32_t previous = demand.asking_wage;
  for (std::uint8_t round = 1;
       round < TransferTuning::Negotiation::MAX_PLAYER_ROUNDS; ++round)
  {
    const std::uint32_t ask = agentAsk(context, demand, round);
    EXPECT_LE(ask, previous);
    previous = ask;
  }
  EXPECT_EQ(previous, demand.weekly_wage) << "the last ask is the real demand";

  PlayerContext ambitious = context;
  ambitious.ambition = 90;
  const ContractDemand ambitious_demand = contractDemand(ambitious);
  EXPECT_GT(static_cast<double>(ambitious_demand.asking_wage) /
                ambitious_demand.weekly_wage,
            static_cast<double>(demand.asking_wage) / demand.weekly_wage);
  PlayerContext stepping_down = context;
  stepping_down.current_club_reputation = 80;
  stepping_down.new_club_reputation = 60;
  const ContractDemand down = contractDemand(stepping_down);
  EXPECT_GT(down.weekly_wage, demand.weekly_wage)
      << "a step down in stature costs wages";
  EXPECT_GT(static_cast<double>(down.asking_wage) / down.weekly_wage,
            static_cast<double>(demand.asking_wage) / demand.weekly_wage);
}

TEST(TransferNegotiationTest, AgentPushesBackOnNearMisses)
{
  const PlayerContext context = transferTarget();
  const ContractDemand demand = contractDemand(context);
  ContractOffer offer = demandedOffer(demand);
  EXPECT_TRUE(evaluateContract(context, offer, 0).accepted)
      << "the real demand is accepted (AI clubs rely on it)";

  offer.weekly_wage = demand.weekly_wage * 95 / 100;
  const ContractResponse close = evaluateContract(context, offer, 0);
  EXPECT_FALSE(close.accepted);
  EXPECT_TRUE(has(close.reasons, Reason::AgentPushback));
  EXPECT_FALSE(has(close.reasons, Reason::WageTooLow));
  EXPECT_EQ(close.demand.asking_wage, agentAsk(context, demand, 1));
  EXPECT_LT(close.demand.asking_wage, demand.asking_wage);
  EXPECT_GE(close.demand.asking_wage, demand.weekly_wage);

  offer.weekly_wage = demand.weekly_wage / 2;
  const ContractResponse far = evaluateContract(context, offer, 0);
  EXPECT_TRUE(has(far.reasons, Reason::WageTooLow));
  EXPECT_FALSE(has(far.reasons, Reason::AgentPushback));
}

TEST(TransferNegotiationTest, SquadFitRanksNeedsAboveMarginalUpgrades)
{
  using enum PlayerRole;
  const std::vector<std::pair<PlayerRole, float>> squad = {
      {GK, 70.0f},  {GK, 62.0f}, {GK, 50.0f}, {CB, 70.0f},  {CB, 68.0f},
      {CB, 60.0f},  {CB, 55.0f}, {LB, 66.0f}, {CDM, 72.0f}, {CM, 70.0f},
      {CAM, 68.0f}, {CM, 60.0f}, {CM, 58.0f}, {LW, 69.0f},  {RW, 67.0f},
      {LM, 55.0f},  {ST, 71.0f}, {ST, 60.0f}, {ST, 55.0f}};
  const SquadNeeds needs = squadNeeds(squad);
  EXPECT_GT(needs.squad_level, 60.0f);
  ASSERT_NE(needs.find(RB), nullptr);
  EXPECT_EQ(needs.find(RB)->count, 0);
  EXPECT_EQ(needs.find(CAM)->group, CM);
  EXPECT_EQ(needs.find(CM)->count, 5);
  EXPECT_FLOAT_EQ(needs.find(CB)->weakest_starter, 68.0f);
  EXPECT_EQ(positionGroup(RW), LW);

  const SquadFit missing = squadFit(needs, RB, 62.0f);
  EXPECT_EQ(missing.kind, FitKind::Starter);
  const SquadFit upgrade = squadFit(needs, CB, 72.0f);
  EXPECT_EQ(upgrade.kind, FitKind::Upgrade);
  EXPECT_FLOAT_EQ(upgrade.gain, 4.0f);
  EXPECT_GT(missing.score, upgrade.score)
      << "an empty starting place outranks a marginal upgrade";
  const SquadFit cover = squadFit(needs, LB, 63.0f);
  EXPECT_EQ(cover.kind, FitKind::Depth);
  EXPECT_GT(cover.score, 0.0f);
  const SquadFit surplus = squadFit(needs, CB, 60.0f);
  EXPECT_EQ(surplus.kind, FitKind::None);
  EXPECT_LT(surplus.score, 0.0f);
  EXPECT_GT(squadFit(needs, ST, 80.0f).score, squadFit(needs, ST, 74.0f).score);
}

TEST(TransferNegotiationTest, AiBidsAboveTheInstalmentThresholdAreAccepted)
{
  // Big AI deals are paid in instalments; the headline fee rises so the
  // seller still receives his price in value and accepts the opening bid.
  for (const std::uint32_t value : {2'000'000u, 8'000'000u, 40'000'000u})
  {
    SaleContext context = rotationPlayer();
    context.market_value = value;
    const std::uint32_t asking = valueForSale(context).asking_fee;
    const OfferTerms bid = aiBidFor(asking, context.age);
    EXPECT_TRUE(isValid(bid));
    EXPECT_GE(bid.fee, asking);
    EXPECT_EQ(bid.instalment_years > 0,
              asking >= TransferTuning::Offer::AI_INSTALMENT_THRESHOLD);
    EXPECT_EQ(evaluateOffer(context, bid, 0).decision,
              ClubResponse::Decision::Accept)
        << value;
    // ...but not by more than the discount on the deferred money.
    EXPECT_LE(static_cast<double>(bid.fee), 1.2 * static_cast<double>(asking));
  }
}
