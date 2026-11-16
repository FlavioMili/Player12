// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/transfer_negotiation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>
#include <ranges>
#include <utility>

#include "model/transfer_tuning.h"
#include "model/transfer_windows.h"
#include "model/world_rng.h"
#include "model/world_simulation.h"

namespace TransferNegotiation
{
namespace
{
constexpr std::array<const char*, static_cast<std::size_t>(
                                      std::to_underlying(Reason::COUNT))>
    REASON_KEYS = {
        "NEG_REASON_KEY_PLAYER",
        "NEG_REASON_FIRST_TEAM",
        "NEG_REASON_RIVAL",
        "NEG_REASON_MID_SEASON",
        "NEG_REASON_LATE_WINDOW",
        "NEG_REASON_RICHER_BUYER",
        "NEG_REASON_LISTED",
        "NEG_REASON_NOT_FOR_SALE",
        "NEG_REASON_RELEASE_CLAUSE_MET",
        "NEG_REASON_OFFER_TOO_LOW",
        "NEG_REASON_TOO_LITTLE_UPFRONT",
        "NEG_REASON_COUNTER",
        "NEG_REASON_TALKS_BROKEN",
        "NEG_REASON_OFFER_ACCEPTED",
        "NEG_REASON_LONG_CONTRACT",
        "NEG_REASON_EXPIRING_CONTRACT",
        "NEG_REASON_OPENING_BID_LOW",
        "NEG_REASON_KEY_PLAYER_NOT_FOR_SALE",
        "NEG_REASON_WAGE_TOO_LOW",
        "NEG_REASON_CONTRACT_TOO_SHORT",
        "NEG_REASON_CONTRACT_TOO_LONG",
        "NEG_REASON_CLUB_TOO_SMALL",
        "NEG_REASON_PLAYING_TIME",
        "NEG_REASON_PROMISE_NOT_CREDIBLE",
        "NEG_REASON_WANTS_RELEASE_CLAUSE",
        "NEG_REASON_LOYAL",
        "NEG_REASON_BIGGER_CLUB",
        "NEG_REASON_TERMS_ACCEPTED",
        "NEG_REASON_TALKS_ENDED",
        "NEG_REASON_AGENT_PUSHBACK",
        "NEG_REASON_NOT_FOR_LOAN",
        "NEG_REASON_WAGE_SHARE_LOW",
        "NEG_REASON_OPTION_TOO_LOW",
        "NEG_REASON_LOAN_LIMIT",
        "NEG_REASON_NO_PLAYING_TIME_LOAN",
        "NEG_REASON_WINDOW_CLOSED",
        "NEG_REASON_UNAVAILABLE",
        "NEG_REASON_OVER_BUDGET",
        "NEG_REASON_EMBARGO",
        "NEG_REASON_AGENT_FEE_LOW",
        "NEG_REASON_SQUAD_FULL",
};

constexpr std::uint32_t FEE_ROUNDING = 10'000;
constexpr int MONTH_JUNE = 6;
constexpr int SEASON_END_DAY = 30;

int level(SquadRole role) { return static_cast<int>(std::to_underlying(role)); }

std::uint32_t roundFeeUp(double fee)
{
  const double rounded = std::ceil(std::max(fee, 0.0) / FEE_ROUNDING) *
                         static_cast<double>(FEE_ROUNDING);
  return static_cast<std::uint32_t>(std::min(
      rounded, static_cast<double>(std::numeric_limits<std::uint32_t>::max())));
}

float rolePremium(SquadRole role)
{
  using enum SquadRole;
  using Valuation = TransferTuning::Valuation;
  switch (role)
  {
    case KeyPlayer:
      return Valuation::KEY_PLAYER_PREMIUM;
    case FirstTeam:
      return Valuation::FIRST_TEAM_PREMIUM;
    case Rotation:
      return Valuation::ROTATION_PREMIUM;
    case Backup:
      return Valuation::BACKUP_PREMIUM;
    case Fringe:
      return Valuation::FRINGE_PREMIUM;
  }
  return 1.0f;
}

bool isRefusal(Reason reason)
{
  using enum Reason;
  switch (reason)
  {
    case WageTooLow:
    case ContractTooShort:
    case ContractTooLong:
    case ClubTooSmall:
    case PlayingTime:
    case WantsReleaseClause:
    case TalksEnded:
    case AgentPushback:
    case AgentFeeTooLow:
      return true;
    default:
      return false;
  }
}

/** Smallest fee with the same structure that the seller values at
 * @p target (the value is monotonic in the fee). */
std::uint32_t feeForValue(const OfferTerms& terms, double target, int age)
{
  OfferTerms probe = terms;
  double low = 0.0;
  double high = std::max(target * 4.0, 1.0);
  for (int iteration = 0; iteration < 48; ++iteration)
  {
    const double mid = 0.5 * (low + high);
    probe.fee = static_cast<std::uint32_t>(std::min(mid, 4.0e9));
    if (sellerValue(probe, age) >= target)
      high = mid;
    else
      low = mid;
  }
  return roundFeeUp(high);
}
}  // namespace

const char* reasonKey(Reason reason)
{
  const auto index = static_cast<std::size_t>(std::to_underlying(reason));
  return index < REASON_KEYS.size() ? REASON_KEYS[index] : "";
}

bool isValid(const OfferTerms& terms)
{
  using Offer = TransferTuning::Offer;
  if (terms.upfront_percent < Offer::MIN_UPFRONT_PERCENT ||
      terms.upfront_percent > 100 ||
      terms.instalment_years > Offer::MAX_INSTALMENT_YEARS ||
      terms.sell_on_percent > Offer::MAX_SELL_ON_PERCENT)
    return false;
  if (terms.upfront_percent < 100 && terms.instalment_years == 0) return false;
  if (terms.appearance_bonus > 0 && terms.appearance_target == 0) return false;
  if (terms.goal_bonus > 0 && terms.goal_target == 0) return false;
  return true;
}

std::uint32_t upfrontAmount(const OfferTerms& terms)
{
  if (terms.instalment_years == 0) return terms.fee;
  return static_cast<std::uint32_t>(static_cast<std::uint64_t>(terms.fee) *
                                    terms.upfront_percent / 100U);
}

std::vector<std::uint32_t> instalmentAmounts(const OfferTerms& terms)
{
  std::vector<std::uint32_t> amounts;
  const std::uint32_t deferred = terms.fee - upfrontAmount(terms);
  if (terms.instalment_years == 0 || deferred == 0) return amounts;
  const std::uint32_t share = deferred / terms.instalment_years;
  amounts.assign(terms.instalment_years, share);
  amounts.back() += deferred - share * terms.instalment_years;
  return amounts;
}

OfferTerms aiOfferTerms(std::uint32_t fee)
{
  using Offer = TransferTuning::Offer;
  OfferTerms terms;
  terms.fee = fee;
  if (fee >= Offer::AI_INSTALMENT_THRESHOLD)
  {
    terms.upfront_percent = Offer::AI_UPFRONT_PERCENT;
    terms.instalment_years = fee >= 4 * Offer::AI_INSTALMENT_THRESHOLD ? 3 : 2;
  }
  return terms;
}

OfferTerms aiBidFor(std::uint32_t asking_fee, int age)
{
  OfferTerms terms = aiOfferTerms(asking_fee);
  if (terms.instalment_years > 0)
    terms.fee = std::max(
        terms.fee, feeForValue(terms, static_cast<double>(asking_fee), age));
  return terms;
}

Valuation valueForSale(const SaleContext& context)
{
  using V = TransferTuning::Valuation;
  Valuation valuation;
  if (context.listed && context.listing_price > 0)
  {
    valuation.asking_fee = context.listing_price;
    valuation.reasons.push_back(Reason::Listed);
  }
  else
  {
    double fee = static_cast<double>(context.market_value) *
                 static_cast<double>(rolePremium(context.role));
    if (context.role == SquadRole::KeyPlayer)
      valuation.reasons.push_back(Reason::KeyPlayer);
    else if (context.role == SquadRole::FirstTeam)
      valuation.reasons.push_back(Reason::FirstTeamPlayer);

    if (context.contract_years > V::LONG_CONTRACT_BASE_YEARS)
    {
      fee *= 1.0 + static_cast<double>(V::LONG_CONTRACT_PREMIUM_PER_YEAR) *
                       (context.contract_years - V::LONG_CONTRACT_BASE_YEARS);
      valuation.reasons.push_back(Reason::LongContract);
    }
    else if (context.contract_years <= 1)
    {
      fee *= static_cast<double>(V::EXPIRING_CONTRACT_FACTOR);
      valuation.reasons.push_back(Reason::ExpiringContract);
    }

    const int raw_gap = static_cast<int>(context.buyer_reputation) -
                        static_cast<int>(context.seller_reputation);
    if (const int gap = std::clamp(raw_gap, V::BUYER_REPUTATION_GAP_MIN,
                                   V::BUYER_REPUTATION_GAP_MAX);
        gap > 0)
    {
      fee *= 1.0 + static_cast<double>(V::BUYER_REPUTATION_SLOPE) * gap;
      if (gap >= 5) valuation.reasons.push_back(Reason::RicherBuyer);
    }
    if (context.same_league && std::abs(raw_gap) <= V::RIVAL_REPUTATION_GAP)
    {
      fee *= static_cast<double>(V::RIVAL_PREMIUM);
      valuation.reasons.push_back(Reason::SellingToRival);
    }
    if (context.winter_window &&
        level(context.role) <= level(SquadRole::FirstTeam))
    {
      fee *= static_cast<double>(V::MID_SEASON_PREMIUM);
      valuation.reasons.push_back(Reason::MidSeason);
    }
    if (context.days_to_deadline >= 0 &&
        context.days_to_deadline <= V::LATE_WINDOW_DAYS &&
        level(context.role) <= level(SquadRole::Rotation))
    {
      fee *= static_cast<double>(V::LATE_WINDOW_PREMIUM);
      valuation.reasons.push_back(Reason::LateWindow);
    }
    valuation.not_for_sale =
        context.role == SquadRole::KeyPlayer &&
        context.contract_years >= V::NOT_FOR_SALE_MIN_CONTRACT_YEARS &&
        raw_gap <= -V::NOT_FOR_SALE_REPUTATION_GAP;
    if (valuation.not_for_sale)
    {
      valuation.reasons.push_back(Reason::NotForSale);
    }
    else if (context.role == SquadRole::KeyPlayer &&
             context.contract_years >= V::KEY_PLAYER_REFUSAL_MIN_YEARS &&
             raw_gap < V::KEY_PLAYER_REFUSAL_BUYER_GAP &&
             context.resolve >= 1.0f - V::KEY_PLAYER_REFUSAL_SHARE)
    {
      valuation.not_for_sale = true;
      valuation.reasons.push_back(Reason::KeyPlayerNotForSale);
    }
    valuation.asking_fee = std::max(roundFeeUp(fee), FEE_ROUNDING);
  }
  if (context.release_clause > 0)
    valuation.asking_fee =
        std::min(valuation.asking_fee, context.release_clause);
  return valuation;
}

double sellerValue(const OfferTerms& terms, int age)
{
  using Offer = TransferTuning::Offer;
  const auto fee = static_cast<double>(terms.fee);
  const auto upfront = static_cast<double>(upfrontAmount(terms));
  const double average_delay =
      0.5 * static_cast<double>(terms.instalment_years + 1);
  const double deferred_factor = std::max(
      0.0, 1.0 - static_cast<double>(Offer::DEFERRED_DISCOUNT_PER_YEAR) *
                     average_delay);
  double add_ons = 0.0;
  if (terms.appearance_target > 0) add_ons += terms.appearance_bonus;
  if (terms.goal_target > 0) add_ons += terms.goal_bonus;
  add_ons = std::min(add_ons * static_cast<double>(Offer::ADD_ON_WEIGHT),
                     fee * static_cast<double>(Offer::ADD_ON_CREDIT_CAP));
  const float sell_on_weight = age <= Offer::SELL_ON_YOUNG_MAX_AGE
                                   ? Offer::SELL_ON_WEIGHT_YOUNG
                                   : Offer::SELL_ON_WEIGHT_SENIOR;
  const double sell_on = std::min(
      fee * terms.sell_on_percent / 100.0 * static_cast<double>(sell_on_weight),
      fee * static_cast<double>(Offer::SELL_ON_CREDIT_CAP));
  return std::lerp(upfront, fee, deferred_factor) + add_ons + sell_on;
}

ClubResponse evaluateOffer(const SaleContext& context, const OfferTerms& terms,
                           std::uint8_t round)
{
  using Offer = TransferTuning::Offer;
  ClubResponse response;
  if (!isValid(terms))
  {
    response.reasons.push_back(terms.upfront_percent <
                                       Offer::MIN_UPFRONT_PERCENT
                                   ? Reason::TooLittleUpfront
                                   : Reason::OfferTooLow);
    return response;
  }
  if (context.release_clause > 0 && terms.fee >= context.release_clause &&
      terms.instalment_years == 0)
  {
    response.decision = ClubResponse::Decision::Accept;
    response.reasons.push_back(Reason::ReleaseClauseMet);
    return response;
  }

  const Valuation valuation = valueForSale(context);
  response.reasons = valuation.reasons;
  if (valuation.not_for_sale) return response;

  const auto asking = static_cast<double>(valuation.asking_fee);
  const double value = sellerValue(terms, context.age);
  if (const float accept_share =
          round == 0 ? Offer::OPENING_ACCEPT_SHARE : Offer::ACCEPT_SHARE;
      value >= asking * static_cast<double>(accept_share))
  {
    response.decision = ClubResponse::Decision::Accept;
    response.reasons.push_back(Reason::OfferAccepted);
    return response;
  }
  response.counter_fee = feeForValue(terms, asking, context.age);
  if (round + 1 >= Offer::MAX_CLUB_ROUNDS)
  {
    response.reasons.push_back(Reason::TalksBroken);
    return response;
  }
  if (value < asking * static_cast<double>(Offer::REJECT_SHARE))
  {
    response.reasons.push_back(Reason::OfferTooLow);
    return response;
  }
  response.decision = ClubResponse::Decision::Counter;
  response.reasons.push_back(round == 0 ? Reason::OpeningBidLow
                                        : Reason::CounterOffer);
  return response;
}

// ---------------------------------------------------------------------------
// Player contracts
// ---------------------------------------------------------------------------

std::uint8_t maxContractYears(int age)
{
  using Contract = TransferTuning::Contract;
  return age < Contract::MINOR_AGE ? Contract::MINOR_MAXIMUM_YEARS
                                   : Contract::MAXIMUM_YEARS;
}

ContractDemand contractDemand(const PlayerContext& context)
{
  using Contract = TransferTuning::Contract;
  using N = TransferTuning::Negotiation;
  ContractDemand demand;
  const double ambition = context.ambition / 100.0;

  auto raise = static_cast<double>(Contract::FREE_AGENT_WAGE_RAISE);
  if (context.kind == ContractKind::Transfer ||
      context.kind == ContractKind::PreContract)
    raise = static_cast<double>(N::BASE_TRANSFER_RAISE) +
            static_cast<double>(N::AMBITION_RAISE) * ambition;
  else if (context.kind == ContractKind::Renewal)
    raise =
        static_cast<double>(Contract::FREE_AGENT_WAGE_RAISE) + 0.10 * ambition;
  double wage = static_cast<double>(context.current_wage) * raise;

  if (context.current_club_reputation > 0 &&
      context.kind != ContractKind::Renewal)
  {
    const int gap = static_cast<int>(context.current_club_reputation) -
                    static_cast<int>(context.new_club_reputation);
    if (gap > 0)
      wage *= 1.0 + static_cast<double>(N::STATURE_WAGE_SLOPE) * gap * ambition;
    else
      wage *= 1.0 - 0.005 * std::min(20, -gap);
  }
  if (const bool leaving = context.kind == ContractKind::Transfer ||
                           context.kind == ContractKind::PreContract;
      leaving && context.loyalty >= N::LOYALTY_THRESHOLD)
    wage *= static_cast<double>(N::LOYALTY_PREMIUM);
  if (context.unsettled && context.kind != ContractKind::Renewal)
    wage *= static_cast<double>(N::UNSETTLED_DISCOUNT);
  demand.weekly_wage = std::max(Contract::MINIMUM_WEEKLY_WAGE,
                                static_cast<std::uint32_t>(std::ceil(wage)));

  demand.max_years = maxContractYears(context.age);
  std::uint8_t min_years = Contract::VETERAN_MINIMUM_YEARS;
  if (context.age <= Contract::YOUNG_PLAYER_MAXIMUM_AGE)
    min_years = Contract::YOUNG_PLAYER_MINIMUM_YEARS;
  else if (context.age <= Contract::PRIME_PLAYER_MAXIMUM_AGE)
    min_years = Contract::PRIME_PLAYER_MINIMUM_YEARS;
  demand.min_years = std::min(min_years, demand.max_years);

  if (context.kind == ContractKind::FreeAgent ||
      context.kind == ContractKind::PreContract)
    demand.signing_bonus = demand.weekly_wage * N::FREE_SIGNING_BONUS_WEEKS;

  int desired = level(context.current_role);
  if (context.ambition >= N::STATURE_AMBITION && desired > 0) --desired;
  demand.desired_role = static_cast<SquadRole>(desired);

  demand.wants_release_clause =
      context.kind != ContractKind::Renewal &&
      context.ambition >= N::RELEASE_CLAUSE_AMBITION &&
      context.new_club_reputation < N::RELEASE_CLAUSE_CLUB_REPUTATION;
  if (demand.wants_release_clause)
    demand.max_release_clause =
        roundFeeUp(static_cast<double>(context.market_value) *
                   static_cast<double>(N::RELEASE_CLAUSE_VALUE_MULTIPLE));
  demand.asking_wage = agentAsk(context, demand, 0);
  return demand;
}

std::uint32_t agentAsk(const PlayerContext& context,
                       const ContractDemand& demand, std::uint8_t round)
{
  using N = TransferTuning::Negotiation;
  double margin = static_cast<double>(N::AGENT_BASE_MARGIN) +
                  static_cast<double>(N::AGENT_AMBITION_MARGIN) *
                      (context.ambition / 100.0);
  if (context.current_club_reputation > context.new_club_reputation &&
      context.kind != ContractKind::Renewal)
    margin += std::min(
        static_cast<double>(N::AGENT_STATURE_MARGIN_CAP),
        static_cast<double>(N::AGENT_STATURE_MARGIN_PER_POINT) *
            (context.current_club_reputation - context.new_club_reputation));
  const int last_round = std::max(1, N::MAX_PLAYER_ROUNDS - 1);
  if (round >= last_round) return demand.weekly_wage;
  const double remaining = 1.0 - static_cast<double>(round) / last_round;
  const double ask =
      static_cast<double>(demand.weekly_wage) * (1.0 + margin * remaining);
  // Small wages round to tens, so every softer ask is visibly lower.
  const double step = demand.weekly_wage < 100 * N::AGENT_ASK_ROUNDING
                          ? N::AGENT_ASK_ROUNDING / 10.0
                          : N::AGENT_ASK_ROUNDING;
  return std::max(demand.weekly_wage,
                  static_cast<std::uint32_t>(std::ceil(ask / step) * step));
}

ContractResponse evaluateContract(const PlayerContext& context,
                                  const ContractOffer& offer,
                                  std::uint8_t round)
{
  using N = TransferTuning::Negotiation;
  ContractResponse response;
  response.demand = contractDemand(context);
  const ContractDemand& demand = response.demand;
  auto& reasons = response.reasons;

  if (offer.years == 0)
    reasons.push_back(Reason::ContractTooShort);
  else if (offer.years > demand.max_years)
    reasons.push_back(Reason::ContractTooLong);
  else if (offer.years < demand.min_years)
    reasons.push_back(Reason::ContractTooShort);

  if (context.current_club_reputation > 0 &&
      context.kind != ContractKind::Renewal)
  {
    const int gap = static_cast<int>(context.current_club_reputation) -
                    static_cast<int>(context.new_club_reputation);
    if (context.ambition >= N::STATURE_AMBITION &&
        gap >= N::STATURE_REFUSAL_GAP)
      reasons.push_back(Reason::ClubTooSmall);
    else if (gap < 0)
      reasons.push_back(Reason::BiggerClub);
  }
  if ((context.kind == ContractKind::Transfer ||
       context.kind == ContractKind::PreContract) &&
      context.loyalty >= N::LOYALTY_THRESHOLD)
    reasons.push_back(Reason::LoyalToClub);

  const int projected = level(context.projected_role);
  int effective = projected;
  if (offer.promised_role)
  {
    const int promised = level(*offer.promised_role);
    const int credible = std::max(0, projected - N::PROMISE_CREDIBILITY_LEVELS);
    if (promised < credible) reasons.push_back(Reason::PromiseNotCredible);
    effective = std::min(projected, std::max(promised, credible));
  }
  if (effective > level(demand.desired_role) + 1)
    reasons.push_back(Reason::PlayingTime);

  if (demand.wants_release_clause &&
      (offer.release_clause == 0 ||
       offer.release_clause > demand.max_release_clause))
    reasons.push_back(Reason::WantsReleaseClause);

  if (offer.years > 0)
  {
    const double bonus_delta = static_cast<double>(offer.signing_bonus) -
                               static_cast<double>(demand.signing_bonus);
    // A yearly rise and appearance money count at what they are worth to
    // him per week: the average uplift and the games he expects to play.
    const double effective_wage =
        static_cast<double>(offer.weekly_wage) +
        bonus_delta / (TransferTuning::Contract::WEEKS_PER_YEAR * offer.years) +
        yearlyRiseWorth(offer.weekly_wage, offer.yearly_rise, offer.years) +
        static_cast<double>(offer.appearance_bonus) *
            static_cast<double>(
                appearanceRate(static_cast<SquadRole>(effective)));
    const auto wanted = static_cast<double>(demand.weekly_wage);
    if (effective_wage + 0.5 < wanted)
      reasons.push_back(
          effective_wage >=
                  wanted * (1.0 - static_cast<double>(N::AGENT_PUSHBACK_BAND))
              ? Reason::AgentPushback
              : Reason::WageTooLow);
  }

  response.accepted = std::ranges::none_of(reasons, isRefusal);
  if (!response.accepted)
  {
    if (round + 1 >= N::MAX_PLAYER_ROUNDS)
      reasons.push_back(Reason::TalksEnded);
    response.demand.asking_wage = agentAsk(
        context, demand, static_cast<std::uint8_t>(std::min(round + 1, 255)));
  }
  if (response.accepted) reasons.push_back(Reason::TermsAccepted);
  return response;
}

float appearanceRate(SquadRole role)
{
  using enum SquadRole;
  using N = TransferTuning::Negotiation;
  switch (role)
  {
    case KeyPlayer:
      return N::KEY_APPS_PER_WEEK;
    case FirstTeam:
      return N::FIRST_TEAM_APPS_PER_WEEK;
    case Rotation:
      return N::ROTATION_APPS_PER_WEEK;
    case Backup:
      return N::BACKUP_APPS_PER_WEEK;
    case Fringe:
      break;
  }
  return N::FRINGE_APPS_PER_WEEK;
}

double yearlyRiseWorth(std::uint32_t wage, std::uint8_t percent,
                       std::uint8_t years)
{
  if (percent == 0 || years < 2) return 0.0;
  const double rise = 1.0 + percent / 100.0;
  double total = 0.0;
  double factor = 1.0;
  for (int year = 0; year < years; ++year)
  {
    total += factor;
    factor *= rise;
  }
  return static_cast<double>(wage) * (total / years - 1.0);
}

ContractOffer demandedOffer(const ContractDemand& demand)
{
  ContractOffer offer;
  offer.weekly_wage = demand.weekly_wage;
  offer.years = demand.min_years;
  offer.signing_bonus = demand.signing_bonus;
  offer.release_clause =
      demand.wants_release_clause ? demand.max_release_clause : 0;
  return offer;
}

// ---------------------------------------------------------------------------
// Loans
// ---------------------------------------------------------------------------

ClubResponse evaluateLoan(const LoanContext& context, const LoanTerms& terms)
{
  using L = TransferTuning::Loan;
  ClubResponse response;
  if (!context.within_limits)
  {
    response.reasons.push_back(Reason::LoanLimit);
    return response;
  }
  if (!context.loan_listed &&
      level(context.role) <= level(SquadRole::FirstTeam))
  {
    response.reasons.push_back(Reason::NotForLoan);
    return response;
  }

  const int required =
      context.loan_listed ? L::LISTED_WAGE_SHARE : L::UNLISTED_WAGE_SHARE;
  const double weekly_total =
      static_cast<double>(context.weekly_wage) * std::max(1, context.weeks);
  const double shortfall =
      weekly_total * std::max(0, required - terms.wage_share) / 100.0;
  const bool wages_covered = static_cast<double>(terms.loan_fee) >= shortfall;

  const double minimum_option =
      static_cast<double>(context.market_value) *
      static_cast<double>(terms.obligation ? L::OBLIGATION_VALUE_MULTIPLE
                                           : L::OPTION_VALUE_MULTIPLE);
  const bool wants_option = terms.option_fee > 0 || terms.obligation;
  const bool option_ok =
      !wants_option || static_cast<double>(terms.option_fee) >= minimum_option;

  if (wages_covered && option_ok && terms.wage_share <= 100)
  {
    response.decision = ClubResponse::Decision::Accept;
    response.reasons.push_back(Reason::OfferAccepted);
    return response;
  }
  response.decision = ClubResponse::Decision::Counter;
  if (!wages_covered)
  {
    const double fee_share =
        weekly_total > 0.0 ? terms.loan_fee * 100.0 / weekly_total : 0.0;
    response.counter_wage_share = static_cast<std::uint8_t>(
        std::clamp(static_cast<int>(std::ceil(required - fee_share)), 0, 100));
    response.reasons.push_back(Reason::WageShareTooLow);
  }
  else
  {
    response.counter_wage_share = terms.wage_share;
  }
  if (!option_ok)
  {
    response.counter_option_fee = roundFeeUp(minimum_option);
    response.reasons.push_back(Reason::OptionTooLow);
  }
  else
  {
    response.counter_option_fee = terms.option_fee;
  }
  return response;
}

bool playerAcceptsLoan(SquadRole at_parent, SquadRole at_borrower,
                       std::uint8_t ambition, int reputation_gap,
                       std::vector<Reason>& reasons)
{
  using N = TransferTuning::Negotiation;
  bool accepted = true;
  if (level(at_borrower) > level(SquadRole::Rotation) ||
      (at_parent != SquadRole::Fringe && level(at_borrower) > level(at_parent)))
  {
    reasons.push_back(Reason::NoPlayingTimeOnLoan);
    accepted = false;
  }
  if (ambition >= N::STATURE_AMBITION &&
      reputation_gap >= N::STATURE_REFUSAL_GAP + 10)
  {
    reasons.push_back(Reason::ClubTooSmall);
    accepted = false;
  }
  return accepted;
}

// ---------------------------------------------------------------------------
// Calendar helpers
// ---------------------------------------------------------------------------

WindowInfo windowInfo(LeagueID league, const GameDateValue& date)
{
  WindowInfo window;
  const std::optional<GameDateValue> end =
      TransferWindows::windowEnd(league, date);
  window.open = end.has_value();
  if (!end)
  {
    window.days_to_deadline = -1;
    return window;
  }
  window.winter = TransferWindows::isStartOfYearWindow(league, date);
  window.days_to_deadline = dayOrdinal(*end) - dayOrdinal(date);
  return window;
}

float activityWeight(const WindowInfo& window)
{
  using M = TransferTuning::Market;
  if (!window.open) return 0.0f;
  if (window.days_to_deadline == 0) return M::DEADLINE_DAY_WEIGHT;
  if (window.days_to_deadline < TransferTuning::Buyer::DEADLINE_DAYS)
    return M::DEADLINE_EVE_WEIGHT;
  if (window.days_to_deadline <= M::LATE_WINDOW_DAYS)
    return M::LATE_WINDOW_WEIGHT;
  return 1.0f;
}

bool canSignPreContract(std::uint8_t contract_years, const GameDateValue& date)
{
  return contract_years == 1 && date.month >= 1 && date.month <= MONTH_JUNE;
}

GameDateValue seasonEndDate(const GameDateValue& date)
{
  const auto year = static_cast<std::uint16_t>(
      date.month > MONTH_JUNE ? date.year + 1 : date.year);
  return GameDateValue(year, MONTH_JUNE, SEASON_END_DAY);
}

GameDateValue contractEndDate(const GameDateValue& date,
                              std::uint8_t contract_years)
{
  const GameDateValue end = seasonEndDate(date);
  return GameDateValue(
      static_cast<std::uint16_t>(
          end.year + std::max(1, static_cast<int>(contract_years)) - 1),
      MONTH_JUNE, SEASON_END_DAY);
}

GameDateValue loanEndDate(const GameDateValue& start, LoanDuration duration)
{
  // A loan agreed in the June part of the summer window covers the
  // season that starts on 1 July.
  const GameDateValue end =
      start.month == MONTH_JUNE
          ? GameDateValue(static_cast<std::uint16_t>(start.year + 1),
                          MONTH_JUNE, SEASON_END_DAY)
          : seasonEndDate(start);
  if (duration == LoanDuration::SeasonEnd) return end;
  const GameDateValue half =
      start + static_cast<std::size_t>(TransferTuning::Loan::SIX_MONTH_DAYS);
  return half < end ? half : end;
}

int weeksBetween(const GameDateValue& from, const GameDateValue& to)
{
  return std::max(0, (dayOrdinal(to) - dayOrdinal(from)) / 7);
}

std::int64_t severancePay(std::uint32_t weekly_wage,
                          std::uint8_t contract_years,
                          const GameDateValue& date)
{
  if (contract_years == 0) return 0;
  const int weeks = weeksBetween(date, seasonEndDate(date)) +
                    static_cast<int>(TransferTuning::Contract::WEEKS_PER_YEAR) *
                        (contract_years - 1);
  return static_cast<std::int64_t>(weekly_wage) * weeks;
}

SquadRole roleForRank(std::size_t rank)
{
  using enum SquadRole;
  if (rank < 5) return KeyPlayer;
  if (rank < 11) return FirstTeam;
  if (rank < 16) return Rotation;
  if (rank < 22) return Backup;
  return Fringe;
}

// ---------------------------------------------------------------------------
// Squad needs
// ---------------------------------------------------------------------------

PlayerRole positionGroup(PlayerRole role)
{
  using enum PlayerRole;
  switch (role)
  {
    case CDM:
    case CM:
    case CAM:
      return CM;
    case LM:
    case RM:
    case LW:
    case RW:
      return LW;
    default:
      return role;
  }
}

const PositionNeed* SquadNeeds::find(PlayerRole role) const
{
  const PlayerRole group = positionGroup(role);
  const auto found = std::ranges::find(positions, group, &PositionNeed::group);
  return found == positions.end() ? nullptr : std::to_address(found);
}

SquadNeeds squadNeeds(std::span<const std::pair<PlayerRole, float>> squad)
{
  struct Shape
  {
    PlayerRole group;
    std::uint8_t starters;
    std::uint8_t wanted;
  };
  // A typical XI and the depth a club keeps behind it (as the AI clubs).
  static constexpr std::array<Shape, SquadNeeds::GROUPS> SHAPE = {
      {{PlayerRole::GK, 1, 3},
       {PlayerRole::CB, 2, 4},
       {PlayerRole::LB, 1, 2},
       {PlayerRole::RB, 1, 2},
       {PlayerRole::CM, 3, 5},
       {PlayerRole::LW, 2, 3},
       {PlayerRole::ST, 1, 3}}};
  constexpr std::size_t FIRST_TEAM_SIZE = 16;

  std::vector<float> overalls;
  overalls.reserve(squad.size());
  for (const auto& [role, overall] : squad) overalls.push_back(overall);
  std::ranges::sort(overalls, std::greater<>{});
  SquadNeeds needs;
  if (const std::size_t top = std::min(overalls.size(), FIRST_TEAM_SIZE);
      top > 0)
    needs.squad_level =
        std::accumulate(overalls.begin(),
                        overalls.begin() + static_cast<std::ptrdiff_t>(top),
                        0.0f) /
        static_cast<float>(top);

  std::vector<float> group_overalls;
  for (std::size_t index = 0; index < SHAPE.size(); ++index)
  {
    const Shape& shape = SHAPE[index];
    group_overalls.clear();
    for (const auto& [role, overall] : squad)
    {
      if (positionGroup(role) == shape.group) group_overalls.push_back(overall);
    }
    std::ranges::sort(group_overalls, std::greater<>{});
    PositionNeed& need = needs.positions[index];
    need.group = shape.group;
    need.count = static_cast<std::uint8_t>(
        std::min<std::size_t>(group_overalls.size(), UINT8_MAX));
    need.starters = shape.starters;
    need.wanted = shape.wanted;
    if (group_overalls.size() >= shape.starters)
      need.weakest_starter = group_overalls[shape.starters - 1];
  }
  return needs;
}

SquadFit squadFit(const SquadNeeds& needs, PlayerRole role, float overall)
{
  using F = TransferTuning::Fit;
  SquadFit fit;
  const PositionNeed* need = needs.find(role);
  if (need == nullptr) return fit;
  const bool missing_starter = need->count < need->starters;
  // Without enough starters the bar is what the AI asks of a shortage
  // signing: a little below the squad level.
  const float reference =
      missing_starter
          ? needs.squad_level - TransferTuning::Market::SHORTAGE_LEVEL_MARGIN
          : need->weakest_starter;
  fit.gain = overall - reference;
  fit.score =
      F::UPGRADE_WEIGHT * std::clamp(fit.gain, F::MIN_GAIN, F::MAX_GAIN);
  if (missing_starter)
  {
    fit.score += F::MISSING_STARTER_BONUS;
    fit.kind = FitKind::Starter;
  }
  else if (fit.gain >= F::UPGRADE_MARGIN)
  {
    fit.kind = FitKind::Upgrade;
  }
  if (!missing_starter && need->count < need->wanted)
  {
    fit.score += F::MISSING_DEPTH_BONUS;
    if (fit.kind == FitKind::None) fit.kind = FitKind::Depth;
  }
  return fit;
}

}  // namespace TransferNegotiation
