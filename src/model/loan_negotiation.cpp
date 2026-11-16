// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/loan_negotiation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include "model/transfer_tuning.h"

namespace LoanNegotiation
{
namespace
{
using B = TransferTuning::Buyer;
using L = TransferTuning::Borrower;

constexpr double MONEY_ROUNDING = 1'000.0;
constexpr double OPTION_ROUNDING = 10'000.0;
constexpr int SIX_MONTH_WEEKS = 26;
constexpr int BLEND_STEPS = 24;
/** Spread of the appearances he gets around the expectation. */
constexpr double MIN_APPEARANCE_SPREAD = 2.0;
constexpr double APPEARANCE_SPREAD_SHARE = 0.35;
constexpr double MIN_UNPLAYED_ODDS = 0.02;
constexpr double MAX_UNPLAYED_ODDS = 0.98;
/** Smallest change in cost that counts as a better proposal. */
constexpr double MIN_IMPROVEMENT = 1.0;
/** Share of a full wage over the loan that sets the smallest ceiling. */
constexpr double MIN_CEILING_WAGE_SHARE = 0.10;

constexpr std::array<const char*,
                     static_cast<std::size_t>(std::to_underlying(Why::COUNT))>
    WHY_KEYS = {"LOAN_WHY_WITHIN_BUDGET", "LOAN_WHY_IMPROVED",
                "LOAN_WHY_WAGE_ROOM",     "LOAN_WHY_FEE_INSTEAD",
                "LOAN_WHY_NO_OBLIGATION", "LOAN_WHY_APPEARANCES_CUT",
                "LOAN_WHY_FINAL_OFFER",   "LOAN_WHY_UNREALISTIC",
                "LOAN_WHY_INSULTED",      "LOAN_WHY_OUT_OF_PATIENCE",
                "LOAN_WHY_NO_WAGE_ROOM",  "LOAN_WHY_DEADLINE"};

std::uint32_t roundDown(double amount, double step = MONEY_ROUNDING)
{
  const double clamped = std::clamp(
      amount, 0.0,
      static_cast<double>(std::numeric_limits<std::uint32_t>::max()));
  return static_cast<std::uint32_t>(std::floor(clamped / step) * step);
}

std::uint8_t stepDown(double value, std::uint8_t step)
{
  const double clamped = std::clamp(value, 0.0, 255.0);
  return static_cast<std::uint8_t>(std::floor(clamped / step) * step);
}

double blend(double from, double to, double t)
{
  return std::lerp(from, to, t);
}

bool sameTerms(const LoanTerms& a, const LoanTerms& b)
{
  return a.duration == b.duration && a.wage_share == b.wage_share &&
         a.loan_fee == b.loan_fee && a.option_fee == b.option_fee &&
         a.obligation == b.obligation && a.recall_clause == b.recall_clause &&
         a.min_appearances == b.min_appearances &&
         a.unplayed_fee == b.unplayed_fee;
}
}  // namespace

const char* whyKey(Why why)
{
  const auto index = static_cast<std::size_t>(std::to_underlying(why));
  return index < WHY_KEYS.size() ? WHY_KEYS[index] : "";
}

bool isValid(const LoanTerms& terms)
{
  return terms.wage_share <= 100 &&
         (!terms.obligation || terms.option_fee > 0) &&
         terms.min_appearances <= L::MAX_MIN_APPEARANCES &&
         (terms.min_appearances == 0) == (terms.unplayed_fee == 0);
}

int loanWeeks(const BorrowerContext& context, LoanDuration duration)
{
  const int season = std::max(1, context.season_weeks);
  return duration == LoanDuration::SixMonths ? std::min(SIX_MONTH_WEEKS, season)
                                             : season;
}

double expectedAppearances(const BorrowerContext& context,
                           LoanDuration duration)
{
  return static_cast<double>(context.apps_per_week) *
         loanWeeks(context, duration);
}

double unplayedOdds(double expected, std::uint8_t minimum)
{
  if (minimum == 0) return 0.0;
  const double spread =
      std::max(MIN_APPEARANCE_SPREAD, APPEARANCE_SPREAD_SHARE * expected);
  const double odds = 1.0 / (1.0 + std::exp((expected - minimum) / spread));
  return std::clamp(odds, MIN_UNPLAYED_ODDS, MAX_UNPLAYED_ODDS);
}

std::uint8_t maxWageShare(const BorrowerContext& context)
{
  if (context.wage_room <= 0) return 0;
  if (context.weekly_wage == 0) return 100;
  const double share = std::floor(static_cast<double>(context.wage_room) *
                                  100.0 / context.weekly_wage);
  return static_cast<std::uint8_t>(std::clamp(share, 0.0, 100.0));
}

double borrowerCost(const BorrowerContext& context, const LoanTerms& terms)
{
  const auto wage = static_cast<double>(context.weekly_wage);
  const int weeks = loanWeeks(context, terms.duration);
  double cost = wage * terms.wage_share / 100.0 * weeks +
                static_cast<double>(terms.loan_fee);
  if (terms.recall_clause) cost += wage * L::RECALL_COST_WEEKS;
  if (terms.min_appearances > 0)
    cost += static_cast<double>(terms.unplayed_fee) *
            unplayedOdds(expectedAppearances(context, terms.duration),
                         terms.min_appearances);
  return cost;
}

double effectiveCeiling(const BorrowerContext& context)
{
  const double deadline =
      BuyerNegotiation::deadlinePressure(context.days_to_deadline)
          ? static_cast<double>(B::DEADLINE_CEILING_BONUS)
          : 0.0;
  return static_cast<double>(context.ceiling) * (1.0 + deadline);
}

bool affordable(const BorrowerContext& context, const LoanTerms& terms)
{
  const std::int64_t cash = std::max<std::int64_t>(context.cash, 0);
  return terms.wage_share <= maxWageShare(context) &&
         static_cast<std::int64_t>(terms.loan_fee) <= cash &&
         (!terms.obligation ||
          (static_cast<std::int64_t>(terms.option_fee) <= cash &&
           static_cast<double>(terms.option_fee) <=
               static_cast<double>(L::MAX_OBLIGATION_VALUE) *
                   context.market_value));
}

BorrowerReply respond(const BorrowerContext& context, const LoanTerms& current,
                      const LoanTerms& asked, double roll)
{
  BorrowerReply reply;
  const double ceiling = effectiveCeiling(context);
  const bool deadline =
      BuyerNegotiation::deadlinePressure(context.days_to_deadline);
  const std::uint8_t max_share = maxWageShare(context);
  const auto pressure = [&]
  {
    if (deadline) reply.reasons.push_back(Why::DeadlineDay);
  };
  // Its budget changed since it made its offer: it cannot keep it.
  if (current.wage_share > max_share)
  {
    reply.reasons.push_back(Why::NoWageRoom);
    return reply;
  }
  const bool valid = isValid(asked);
  const double cost = valid ? borrowerCost(context, asked)
                            : std::numeric_limits<double>::infinity();
  if (valid && cost <= ceiling && affordable(context, asked))
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

  const bool final_offer = context.answered + 1 >= context.patience;
  const double previous = borrowerCost(context, current);
  double share = 1.0;
  if (!final_offer)
    share = std::min(
        1.0, static_cast<double>(B::FIRST_CONCESSION) +
                 static_cast<double>(B::CONCESSION_STEP) * context.answered +
                 (deadline ? static_cast<double>(B::DEADLINE_CONCESSION_BONUS)
                           : 0.0));
  const double target = std::max(
      previous, previous + share * (std::min(cost, ceiling) - previous));

  // The club's structure, within what the borrower can carry.
  LoanTerms base = asked;
  std::vector<Why> notes;
  const std::int64_t cash = std::max<std::int64_t>(context.cash, 0);
  bool share_capped = false;
  if (base.wage_share > max_share)
  {
    base.wage_share = stepDown(max_share, L::WAGE_SHARE_STEP);
    share_capped = true;
    notes.push_back(Why::WageRoom);
  }
  base.loan_fee =
      static_cast<std::uint32_t>(std::min<std::int64_t>(base.loan_fee, cash));
  if (base.obligation &&
      (static_cast<std::int64_t>(base.option_fee) > cash ||
       static_cast<double>(base.option_fee) >
           static_cast<double>(L::MAX_OBLIGATION_VALUE) * context.market_value))
  {
    base.obligation = false;
    notes.push_back(Why::ObligationDropped);
  }
  if (base.min_appearances > 0)
  {
    const double expected = expectedAppearances(context, base.duration);
    if (base.min_appearances > expected)
    {
      base.min_appearances = stepDown(expected, L::APPEARANCES_STEP);
      if (base.min_appearances == 0) base.unplayed_fee = 0;
      notes.push_back(Why::AppearancesCut);
    }
  }

  // Moves from its own offer towards that structure as far as the target
  // allows: every amount is a straight line between the two, so the cost
  // grows (or falls) steadily along the way.
  const auto build = [&](const LoanTerms& shape, double t)
  {
    LoanTerms terms = shape;
    terms.wage_share = std::min<std::uint8_t>(
        stepDown(blend(current.wage_share, shape.wage_share, t),
                 L::WAGE_SHARE_STEP),
        max_share);
    terms.loan_fee = roundDown(blend(current.loan_fee, shape.loan_fee, t));
    terms.min_appearances =
        stepDown(blend(current.min_appearances, shape.min_appearances, t),
                 L::APPEARANCES_STEP);
    terms.unplayed_fee =
        terms.min_appearances > 0
            ? roundDown(blend(current.unplayed_fee, shape.unplayed_fee, t))
            : 0;
    if (terms.min_appearances > 0 && terms.unplayed_fee == 0)
      terms.min_appearances = 0;
    return terms;
  };
  const auto fits = [&](const LoanTerms& terms)
  {
    return borrowerCost(context, terms) <= target && affordable(context, terms);
  };
  const auto furthest = [&](const LoanTerms& shape) -> std::optional<LoanTerms>
  {
    if (fits(build(shape, 1.0))) return build(shape, 1.0);
    if (!fits(build(shape, 0.0))) return std::nullopt;
    double low = 0.0;
    double high = 1.0;
    for (int step = 0; step < BLEND_STEPS; ++step)
    {
      const double mid = 0.5 * (low + high);
      if (fits(build(shape, mid)))
        low = mid;
      else
        high = mid;
    }
    return build(shape, low);
  };

  std::optional<LoanTerms> proposal = furthest(base);
  if (!proposal && base.obligation)
  {
    // The obligation is what it cannot stretch to: an option instead.
    base.obligation = false;
    notes.push_back(Why::ObligationDropped);
    proposal = furthest(base);
  }
  if (!proposal && base.recall_clause && !current.recall_clause)
  {
    base.recall_clause = false;
    proposal = furthest(base);
  }

  if (proposal && share_capped &&
      roll < static_cast<double>(L::FEE_INSTEAD_OF_WAGES_CHANCE))
  {
    // Short of wage room: what it would have paid in wages comes as a fee.
    const double spare = target - borrowerCost(context, *proposal);
    const auto extra = static_cast<std::int64_t>(roundDown(spare));
    const std::int64_t fee = std::min<std::int64_t>(
        static_cast<std::int64_t>(proposal->loan_fee) + extra, cash);
    if (fee > static_cast<std::int64_t>(proposal->loan_fee))
    {
      proposal->loan_fee = static_cast<std::uint32_t>(fee);
      notes.push_back(Why::FeeInsteadOfWages);
    }
  }

  reply.decision = Decision::Counter;
  // Better for the club: it pays more, or it takes on part of the club's
  // terms that cost it nothing (an option to buy).
  if (const bool improves =
          proposal && isValid(*proposal) &&
          (borrowerCost(context, *proposal) > previous + MIN_IMPROVEMENT ||
           (!sameTerms(*proposal, current) &&
            borrowerCost(context, *proposal) >= previous - MIN_IMPROVEMENT));
      !improves)
  {
    reply.move = Move::FinalOffer;
    reply.terms = current;
    reply.reasons.push_back(Why::FinalOffer);
    pressure();
    return reply;
  }
  reply.move = final_offer ? Move::FinalOffer : Move::Improved;
  reply.terms = *proposal;
  reply.reasons.push_back(final_offer ? Why::FinalOffer : Why::Improved);
  for (const Why note : notes)
    if (!std::ranges::contains(reply.reasons, note))
      reply.reasons.push_back(note);
  pressure();
  return reply;
}

LoanTerms openingOffer(const BorrowerContext& context, LoanDuration duration,
                       std::uint8_t share, int age, double option_roll)
{
  LoanTerms terms;
  terms.duration = duration;
  terms.wage_share = std::min<std::uint8_t>(
      share, stepDown(maxWageShare(context), L::WAGE_SHARE_STEP));
  if (age <= L::OPTION_MAX_AGE &&
      option_roll < static_cast<double>(L::OPENING_OPTION_CHANCE) &&
      context.market_value > 0)
    terms.option_fee = static_cast<std::uint32_t>(
        std::ceil(
            static_cast<double>(context.market_value) *
            static_cast<double>(TransferTuning::Loan::OPTION_VALUE_MULTIPLE) /
            OPTION_ROUNDING) *
        OPTION_ROUNDING);
  return terms;
}

std::uint32_t ceilingFor(const BorrowerContext& context,
                         const LoanTerms& opening, double roll)
{
  const double floor_cost = static_cast<double>(context.weekly_wage) *
                            loanWeeks(context, opening.duration) *
                            MIN_CEILING_WAGE_SHARE;
  const double base = std::max(borrowerCost(context, opening), floor_cost);
  const double headroom =
      static_cast<double>(L::CEILING_HEADROOM_MIN) +
      static_cast<double>(L::CEILING_HEADROOM_MAX - L::CEILING_HEADROOM_MIN) *
          std::clamp(roll, 0.0, 1.0);
  return roundDown(base * (1.0 + headroom));
}

}  // namespace LoanNegotiation
