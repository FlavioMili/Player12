// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/buyer_negotiation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include "model/transfer_tuning.h"

namespace BuyerNegotiation
{
namespace
{
using B = TransferTuning::Buyer;
using Offer = TransferTuning::Offer;

constexpr std::uint32_t FEE_ROUNDING = 10'000;
constexpr double MAX_FEE = 4.0e9;
constexpr int FEE_SEARCH_STEPS = 48;
/** Most of the fee an opening bid ever reaches. */
constexpr double MAX_OPENING_SHARE = 0.97;
/** Plan draws: below the first a longer spread, below the second add-ons
 * instead of cash, otherwise the club's own structure. */
constexpr double SPREAD_ROLL = 0.35;
constexpr double ADD_ONS_ROLL = 0.65;
/** Structures tried when cash is short: upfront share and years. */
constexpr std::array<std::pair<std::uint8_t, std::uint8_t>, 4> CASH_PLANS = {
    {{50, 3}, {40, 3}, {30, 4}, {20, 4}}};
constexpr std::uint8_t SPREAD_UPFRONT_STEP = 20;
constexpr std::uint8_t SPREAD_MIN_UPFRONT = 40;
constexpr std::uint8_t SPREAD_FIRST_UPFRONT = 60;
constexpr std::uint8_t SPREAD_FIRST_YEARS = 2;
constexpr std::uint8_t SELL_ON_CUT = 5;
constexpr std::uint8_t SELL_ON_CUT_FROM = 10;

constexpr std::array<const char*,
                     static_cast<std::size_t>(std::to_underlying(Move::COUNT))>
    MOVE_KEYS = {"OFFER_MOVE_BID",      "OFFER_MOVE_IMPROVED",
                 "OFFER_MOVE_FINAL",    "OFFER_MOVE_RESTATED",
                 "OFFER_MOVE_COUNTER",  "OFFER_MOVE_ASKING_PRICE",
                 "OFFER_MOVE_ACCEPTED", "OFFER_MOVE_WALKED_AWAY"};

constexpr std::array<const char*,
                     static_cast<std::size_t>(std::to_underlying(Why::COUNT))>
    WHY_KEYS = {"OFFER_WHY_WITHIN_BUDGET",    "OFFER_WHY_IMPROVED",
                "OFFER_WHY_MORE_INSTALMENTS", "OFFER_WHY_ADD_ONS",
                "OFFER_WHY_ADD_ONS_TRIMMED",  "OFFER_WHY_SELL_ON_CUT",
                "OFFER_WHY_CASH_LIMITED",     "OFFER_WHY_FINAL_OFFER",
                "OFFER_WHY_UNREALISTIC",      "OFFER_WHY_INSULTED",
                "OFFER_WHY_OUT_OF_PATIENCE",  "OFFER_WHY_RIVAL_BIDS",
                "OFFER_WHY_DEADLINE",         "OFFER_WHY_WAGE_BUDGET"};

std::uint32_t roundDown(double amount)
{
  const double rounded =
      std::floor(std::clamp(amount, 0.0, MAX_FEE) / FEE_ROUNDING) *
      FEE_ROUNDING;
  return static_cast<std::uint32_t>(rounded);
}

/**
 * The shape of a structure independent of its size: add-ons are shares of
 * the fee, so the same deal can be scaled up or down.
 */
struct Shape
{
  std::uint8_t upfront_percent = 100;
  std::uint8_t instalment_years = 0;
  double appearance_share = 0.0;
  std::uint16_t appearance_target = 0;
  double goal_share = 0.0;
  std::uint16_t goal_target = 0;
  std::uint8_t sell_on_percent = 0;

  OfferTerms build(std::uint32_t fee) const
  {
    OfferTerms terms;
    terms.fee = fee;
    terms.upfront_percent = instalment_years > 0 ? upfront_percent : 100;
    terms.instalment_years = upfront_percent < 100 ? instalment_years : 0;
    terms.appearance_bonus =
        appearance_target > 0 ? roundDown(appearance_share * fee) : 0;
    terms.appearance_target =
        terms.appearance_bonus > 0 ? appearance_target : 0;
    terms.goal_bonus = goal_target > 0 ? roundDown(goal_share * fee) : 0;
    terms.goal_target = terms.goal_bonus > 0 ? goal_target : 0;
    terms.sell_on_percent = sell_on_percent;
    return terms;
  }

  double addOnShare() const { return appearance_share + goal_share; }
};

Shape shapeOf(const OfferTerms& terms)
{
  Shape shape;
  shape.upfront_percent = terms.upfront_percent;
  shape.instalment_years = terms.instalment_years;
  if (terms.fee > 0)
  {
    const auto fee = static_cast<double>(terms.fee);
    if (terms.appearance_target > 0)
    {
      shape.appearance_share = terms.appearance_bonus / fee;
      shape.appearance_target = terms.appearance_target;
    }
    if (terms.goal_target > 0)
    {
      shape.goal_share = terms.goal_bonus / fee;
      shape.goal_target = terms.goal_target;
    }
  }
  shape.sell_on_percent = terms.sell_on_percent;
  return shape;
}

/** Largest rounded fee whose structure costs the buyer at most @p target
 * (the cost grows with the fee for a fixed shape). 0 if none. */
template <typename Build>
std::uint32_t largestFee(const Build& build, double target, int age)
{
  if (buyerCost(build(FEE_ROUNDING), age) > target) return 0;
  double low = FEE_ROUNDING;
  double high = std::clamp(target * 4.0, 2.0 * FEE_ROUNDING, MAX_FEE);
  for (int step = 0; step < FEE_SEARCH_STEPS; ++step)
  {
    const double mid = 0.5 * (low + high);
    if (buyerCost(build(roundDown(mid)), age) <= target)
      low = mid;
    else
      high = mid;
  }
  return roundDown(low);
}

/** A longer spread: less upfront, one more year of instalments. */
Shape spread(Shape shape)
{
  if (shape.upfront_percent >= 100 || shape.instalment_years == 0)
  {
    shape.upfront_percent = SPREAD_FIRST_UPFRONT;
    shape.instalment_years = SPREAD_FIRST_YEARS;
    return shape;
  }
  // Never more upfront than before (a 30% share stays at 30%).
  shape.upfront_percent = static_cast<std::uint8_t>(std::min<int>(
      shape.upfront_percent,
      std::max<int>(SPREAD_MIN_UPFRONT,
                    shape.upfront_percent - SPREAD_UPFRONT_STEP)));
  shape.instalment_years = static_cast<std::uint8_t>(
      std::min<int>(Offer::MAX_INSTALMENT_YEARS, shape.instalment_years + 1));
  return shape;
}

/** Part of the money moved into an appearance add-on. */
Shape withAddOns(Shape shape)
{
  const double missing =
      static_cast<double>(B::ADD_ON_SHARE) - shape.addOnShare();
  if (missing <= 0.0) return shape;
  shape.appearance_share += missing;
  if (shape.appearance_target == 0)
    shape.appearance_target = B::ADD_ON_APPEARANCES;
  return shape;
}
}  // namespace

bool byBuyer(Move move)
{
  using enum Move;
  switch (move)
  {
    case Bid:
    case Improved:
    case FinalOffer:
    case Restated:
    case Accepted:
    case WalkedAway:
      return true;
    case Counter:
    case AskingPrice:
    case COUNT:
      break;
  }
  return false;
}

const char* moveKey(Move move)
{
  const auto index = static_cast<std::size_t>(std::to_underlying(move));
  return index < MOVE_KEYS.size() ? MOVE_KEYS[index] : "";
}

const char* whyKey(Why why)
{
  const auto index = static_cast<std::size_t>(std::to_underlying(why));
  return index < WHY_KEYS.size() ? WHY_KEYS[index] : "";
}

double appearanceOdds(std::uint16_t target)
{
  return std::clamp(
      static_cast<double>(B::APPEARANCE_ODDS_BASE) -
          static_cast<double>(B::APPEARANCE_ODDS_PER_MATCH) * target,
      static_cast<double>(B::MIN_ADD_ON_ODDS),
      static_cast<double>(B::MAX_ADD_ON_ODDS));
}

double goalOdds(std::uint16_t target)
{
  return std::clamp(static_cast<double>(B::GOAL_ODDS_BASE) -
                        static_cast<double>(B::GOAL_ODDS_PER_GOAL) * target,
                    static_cast<double>(B::MIN_ADD_ON_ODDS),
                    static_cast<double>(B::MAX_ADD_ON_ODDS));
}

double sellOnCostPerPercent(int age)
{
  if (age <= B::SELL_ON_YOUNG_AGE)
    return static_cast<double>(B::SELL_ON_COST_YOUNG) / 100.0;
  if (age <= B::SELL_ON_PRIME_AGE)
    return static_cast<double>(B::SELL_ON_COST_PRIME) / 100.0;
  if (age <= B::SELL_ON_SETTLED_AGE)
    return static_cast<double>(B::SELL_ON_COST_SETTLED) / 100.0;
  return static_cast<double>(B::SELL_ON_COST_VETERAN) / 100.0;
}

double buyerCost(const OfferTerms& terms, int age)
{
  const auto fee = static_cast<double>(terms.fee);
  const auto upfront =
      static_cast<double>(TransferNegotiation::upfrontAmount(terms));
  const double average_delay =
      0.5 * static_cast<double>(terms.instalment_years + 1);
  const double deferred_factor =
      std::max(0.0, 1.0 - static_cast<double>(B::DEFERRED_DISCOUNT_PER_YEAR) *
                              average_delay);
  double add_ons = 0.0;
  if (terms.appearance_target > 0)
    add_ons += terms.appearance_bonus * appearanceOdds(terms.appearance_target);
  if (terms.goal_target > 0)
    add_ons += terms.goal_bonus * goalOdds(terms.goal_target);
  const double sell_on =
      fee * terms.sell_on_percent * sellOnCostPerPercent(age);
  return std::lerp(upfront, fee, deferred_factor) + add_ons + sell_on;
}

std::uint32_t agentFee(std::uint32_t fee)
{
  return static_cast<std::uint32_t>(static_cast<std::uint64_t>(fee) *
                                    Offer::AGENT_FEE_PERCENT / 100U);
}

std::int64_t signingCash(const OfferTerms& terms)
{
  return static_cast<std::int64_t>(TransferNegotiation::upfrontAmount(terms)) +
         static_cast<std::int64_t>(agentFee(terms.fee));
}

bool deadlinePressure(int days_to_deadline)
{
  return days_to_deadline >= 0 && days_to_deadline <= B::DEADLINE_DAYS;
}

double effectiveCeiling(const BuyerContext& context)
{
  const double rivals =
      std::min(static_cast<double>(B::RIVAL_CEILING_BONUS) * context.rivals,
               static_cast<double>(B::MAX_RIVAL_CEILING_BONUS));
  const double deadline = deadlinePressure(context.days_to_deadline)
                              ? static_cast<double>(B::DEADLINE_CEILING_BONUS)
                              : 0.0;
  return static_cast<double>(context.ceiling) * (1.0 + rivals) *
         (1.0 + deadline);
}

BuyerReply respond(const BuyerContext& context, const OfferTerms& current,
                   const OfferTerms& asked, bool firm, double roll)
{
  BuyerReply reply;
  const double ceiling = effectiveCeiling(context);
  const bool deadline = deadlinePressure(context.days_to_deadline);
  const bool valid = TransferNegotiation::isValid(asked) && asked.fee > 0;
  const double cost = valid ? buyerCost(asked, context.age)
                            : std::numeric_limits<double>::infinity();
  const auto pressure = [&]
  {
    if (context.rivals > 0) reply.reasons.push_back(Why::RivalBids);
    if (deadline) reply.reasons.push_back(Why::DeadlineDay);
  };

  if (!context.wage_fits)
  {
    reply.reasons.push_back(Why::WageBudget);
    return reply;
  }
  if (valid && cost <= ceiling && signingCash(asked) <= context.cash)
  {
    reply.decision = Decision::Accept;
    reply.move = Move::Accepted;
    reply.terms = asked;
    reply.reasons.push_back(Why::WithinBudget);
    pressure();
    return reply;
  }
  if (context.answered >= context.patience)
  {
    reply.reasons.push_back(Why::OutOfPatience);
    return reply;
  }
  if (cost > ceiling * static_cast<double>(B::INSULT_MULTIPLE))
  {
    reply.insulted = true;
    if (cost > ceiling * static_cast<double>(B::WALK_OUT_MULTIPLE) ||
        context.insults + 1 >= B::MAX_INSULTS)
    {
      reply.reasons.push_back(Why::Insulted);
      return reply;
    }
    reply.decision = Decision::Counter;
    reply.move = Move::Restated;
    reply.terms = current;
    reply.reasons.push_back(Why::Unrealistic);
    return reply;
  }

  const bool final_offer = firm || context.answered + 1 >= context.patience;
  const double previous = buyerCost(current, context.age);
  const double wanted = std::min(cost, ceiling);
  double share = 1.0;
  if (!final_offer)
    share = std::min(
        1.0,
        static_cast<double>(B::FIRST_CONCESSION) +
            static_cast<double>(B::CONCESSION_STEP) * context.answered +
            static_cast<double>(B::RIVAL_CONCESSION_BONUS) * context.rivals +
            (deadline ? static_cast<double>(B::DEADLINE_CONCESSION_BONUS)
                      : 0.0));
  const double target = std::max(previous, std::lerp(previous, wanted, share));

  // Start from the club's own structure, within what the buyer tolerates.
  Shape base = shapeOf(asked);
  std::vector<Why> notes;
  if (base.addOnShare() > static_cast<double>(B::MAX_ADD_ON_SHARE))
  {
    const double scale =
        static_cast<double>(B::MAX_ADD_ON_SHARE) / base.addOnShare();
    base.appearance_share *= scale;
    base.goal_share *= scale;
    notes.push_back(Why::AddOnsTrimmed);
  }
  if (base.sell_on_percent >= SELL_ON_CUT_FROM &&
      context.age <= B::SELL_ON_PRIME_AGE)
  {
    base.sell_on_percent =
        static_cast<std::uint8_t>(base.sell_on_percent - SELL_ON_CUT);
    notes.push_back(Why::SellOnCut);
  }

  const auto feeFor = [target, age = context.age](const Shape& shape)
  {
    return largestFee([&shape](std::uint32_t fee) { return shape.build(fee); },
                      target, age);
  };
  const auto fitsCash =
      [cash = context.cash](const Shape& shape, std::uint32_t fee)
  { return signingCash(shape.build(fee)) <= cash; };

  Shape shape = base;
  Why plan = Why::Improved;
  if (!fitsCash(base, feeFor(base)))
  {
    // Short of cash: spread the money until the signing-day part fits,
    // else offer what the cash allows.
    notes.push_back(Why::CashLimited);
    plan = Why::MoreInstalments;
    shape = spread(base);
    for (const auto& [upfront, years] : CASH_PLANS)
    {
      if (fitsCash(shape, feeFor(shape))) break;
      shape.upfront_percent = std::min(shape.upfront_percent, upfront);
      shape.instalment_years = std::max(shape.instalment_years, years);
    }
  }
  else if (roll < SPREAD_ROLL)
  {
    plan = Why::MoreInstalments;
    shape = spread(base);
  }
  else if (roll < ADD_ONS_ROLL)
  {
    plan = Why::AddOnsInsteadOfCash;
    shape = withAddOns(base);
  }

  std::uint32_t fee = feeFor(shape);
  if (!fitsCash(shape, fee))
  {
    const double per_fee = shape.build(1'000'000).upfront_percent / 100.0 +
                           Offer::AGENT_FEE_PERCENT / 100.0;
    fee = std::min(fee, roundDown(static_cast<double>(context.cash) / per_fee));
  }
  if (fee < current.fee && plan != Why::MoreInstalments)
  {
    // Never a lower headline than before: keep the club's shape instead.
    plan = Why::Improved;
    shape = base;
    fee = feeFor(shape);
  }
  OfferTerms proposal = shape.build(fee);
  const bool improves =
      TransferNegotiation::isValid(proposal) && proposal.fee > 0 &&
      buyerCost(proposal, context.age) > previous + 0.5 * FEE_ROUNDING;

  reply.decision = Decision::Counter;
  if (!improves)
  {
    // It cannot go further: its bid stands as the final offer.
    reply.move = Move::FinalOffer;
    reply.terms = current;
    reply.reasons.push_back(Why::FinalOffer);
    pressure();
    return reply;
  }
  reply.move = final_offer ? Move::FinalOffer : Move::Improved;
  reply.terms = proposal;
  reply.reasons.push_back(final_offer ? Why::FinalOffer : Why::Improved);
  if (plan != Why::Improved) reply.reasons.push_back(plan);
  reply.reasons.insert(reply.reasons.end(), notes.begin(), notes.end());
  pressure();
  return reply;
}

OfferTerms openingBid(std::uint32_t ceiling, int age, std::uint8_t rivals,
                      double share_roll, double add_on_roll)
{
  double share =
      static_cast<double>(B::OPENING_SHARE_MIN) +
      static_cast<double>(B::OPENING_SHARE_MAX - B::OPENING_SHARE_MIN) *
          share_roll;
  if (rivals > 0) share += static_cast<double>(B::OPENING_RIVAL_BONUS);
  share = std::min(share, MAX_OPENING_SHARE);
  const bool add_on =
      add_on_roll < static_cast<double>(B::OPENING_ADD_ON_CHANCE);
  const auto build = [add_on](std::uint32_t fee)
  {
    OfferTerms terms = TransferNegotiation::aiOfferTerms(fee);
    if (add_on)
    {
      terms.appearance_bonus =
          roundDown(static_cast<double>(B::ADD_ON_SHARE) * fee);
      terms.appearance_target =
          terms.appearance_bonus > 0 ? B::ADD_ON_APPEARANCES : 0;
    }
    return terms;
  };
  const std::uint32_t fee =
      largestFee(build, static_cast<double>(ceiling) * share, age);
  return build(std::max(fee, FEE_ROUNDING));
}

std::uint8_t drawPatience(double roll, int days_to_deadline)
{
  const int span = B::MAX_PATIENCE - B::MIN_PATIENCE + 1;
  const int patience = std::min<int>(
      B::MAX_PATIENCE,
      B::MIN_PATIENCE + static_cast<int>(std::clamp(roll, 0.0, 1.0) * span));
  if (deadlinePressure(days_to_deadline))
    return std::min<std::uint8_t>(static_cast<std::uint8_t>(patience),
                                  B::MIN_PATIENCE);
  return static_cast<std::uint8_t>(patience);
}

int replyDelay(double roll, int days_to_deadline)
{
  if (deadlinePressure(days_to_deadline)) return 0;
  const int span = B::MAX_REPLY_DAYS - B::MIN_REPLY_DAYS + 1;
  int delay = std::min(
      B::MAX_REPLY_DAYS,
      B::MIN_REPLY_DAYS + static_cast<int>(std::clamp(roll, 0.0, 1.0) * span));
  if (days_to_deadline >= 0) delay = std::min(delay, days_to_deadline);
  return delay;
}

// ---------------------------------------------------------------------------
// The player
// ---------------------------------------------------------------------------

const char* stanceKey(PlayerStance stance)
{
  using enum PlayerStance;
  switch (stance)
  {
    case AskedToLeave:
      return "OFFER_STANCE_ASKED_TO_LEAVE";
    case WantsBiggerClub:
      return "OFFER_STANCE_BIGGER_CLUB";
    case Open:
      return "OFFER_STANCE_OPEN";
    case HappyHere:
      return "OFFER_STANCE_HAPPY";
    case Reluctant:
      return "OFFER_STANCE_RELUCTANT";
  }
  return "";
}

PlayerStance stanceFor(const StanceFacts& facts)
{
  using enum PlayerStance;
  if (facts.transfer_request) return AskedToLeave;
  if (!facts.would_join) return Reluctant;
  if (facts.reputation_gap >= B::KEEN_REPUTATION_GAP &&
      facts.ambition >= B::KEEN_AMBITION)
    return WantsBiggerClub;
  if (facts.loyalty >= B::HAPPY_LOYALTY ||
      (facts.reputation_gap <= 0 && facts.morale >= B::HAPPY_MORALE))
    return HappyHere;
  return Open;
}

double termsRefusalChance(PlayerStance stance)
{
  using enum PlayerStance;
  switch (stance)
  {
    case AskedToLeave:
    case WantsBiggerClub:
      return 0.0;
    case Open:
      return B::TERMS_REFUSAL_OPEN;
    case HappyHere:
      return B::TERMS_REFUSAL_HAPPY;
    case Reluctant:
      return B::TERMS_REFUSAL_RELUCTANT;
  }
  return 0.0;
}

bool isBigBid(const OfferTerms& terms, int /*age*/, std::uint32_t market_value)
{
  const double headline = static_cast<double>(terms.fee) +
                          static_cast<double>(terms.appearance_bonus) +
                          static_cast<double>(terms.goal_bonus);
  return market_value > 0 &&
         headline >= static_cast<double>(market_value) *
                         static_cast<double>(B::BIG_BID_VALUE_SHARE);
}

RejectionEffect rejectionEffect(PlayerStance stance, bool big_bid,
                                std::uint8_t ambition)
{
  using enum PlayerStance;
  RejectionEffect effect;
  switch (stance)
  {
    case AskedToLeave:
      effect.morale_delta =
          -(big_bid ? B::REJECTED_ASKED_MORALE : B::REJECTED_SMALL_BID_MORALE);
      if (big_bid) effect.trust_delta = -B::REJECTED_TRUST;
      break;
    case WantsBiggerClub:
      effect.morale_delta =
          -(big_bid ? B::REJECTED_KEEN_MORALE : B::REJECTED_SMALL_BID_MORALE);
      if (big_bid)
      {
        effect.trust_delta = -B::REJECTED_TRUST;
        effect.transfer_request = ambition >= B::REQUEST_AMBITION;
      }
      break;
    case Open:
      if (big_bid) effect.morale_delta = -B::REJECTED_OPEN_MORALE;
      break;
    case HappyHere:
    case Reluctant:
      break;
  }
  return effect;
}

}  // namespace BuyerNegotiation
