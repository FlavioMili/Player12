// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/club_economy.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

#include "database/gamedata.h"
#include "global/global.h"
#include "model/team.h"

namespace
{
using Finance = WorldTuning::Finance;

double revenueIndex(const LeagueEconomy& economy, std::uint8_t reputation)
{
  return std::exp(static_cast<double>(Finance::REVENUE_REPUTATION_SLOPE) *
                  (static_cast<double>(reputation) -
                   static_cast<double>(economy.mean_reputation)));
}

double operatingShare(const LeagueEconomy& economy)
{
  return std::max(static_cast<double>(Finance::MIN_OPERATING_SHARE),
                  static_cast<double>(Finance::TARGET_COST_RATIO) -
                      ClubEconomy::playerWageShare(economy) -
                      static_cast<double>(Finance::STAFF_SHARE));
}

double meritPrize(const LeagueEconomy& economy, std::size_t position,
                  std::size_t league_size)
{
  const auto clubs = static_cast<double>(league_size);
  const double merit_pool =
      static_cast<double>(economy.profile->average_revenue_eur) *
      static_cast<double>(economy.profile->tv_share) *
      (1.0 - static_cast<double>(Finance::TV_EQUAL_SHARE)) * clubs;
  // [S] Linear merit payments by place (Premier League model).
  return merit_pool * (clubs - static_cast<double>(position)) /
         (clubs * (clubs + 1.0) / 2.0);
}

double continentalPrize(const LeagueEconomy& economy, std::size_t position,
                        std::size_t league_size)
{
  if (position >= Finance::CONTINENTAL_WEIGHTS.size()) return 0.0;
  return static_cast<double>(economy.profile->average_revenue_eur) *
         static_cast<double>(economy.profile->continental_share) *
         static_cast<double>(league_size) *
         static_cast<double>(Finance::CONTINENTAL_WEIGHTS[position]);
}
}  // namespace

LeagueEconomy makeLeagueEconomy(LeagueID league_id,
                                const std::vector<std::uint8_t>& reputations)
{
  LeagueEconomy economy;
  economy.profile = &leagueProfile(league_id);
  economy.clubs = reputations.size();
  if (reputations.empty()) return economy;

  double total = 0.0;
  for (const std::uint8_t reputation : reputations) total += reputation;
  economy.mean_reputation =
      static_cast<float>(total / static_cast<double>(reputations.size()));

  double index_total = 0.0;
  for (const std::uint8_t reputation : reputations)
    index_total += revenueIndex(economy, reputation);
  economy.revenue_normalizer =
      index_total / static_cast<double>(reputations.size());
  economy.reputations_desc = reputations;
  std::ranges::sort(economy.reputations_desc, std::greater<>{});
  return economy;
}

std::unordered_map<LeagueID, LeagueEconomy> buildLeagueEconomies(
    const GameData& gamedata)
{
  std::map<LeagueID, std::vector<std::uint8_t>> reputations;
  for (const auto& [team_id, team] : gamedata.getTeams())
  {
    if (team_id == FREE_AGENTS_TEAM_ID) continue;
    reputations[team.getLeagueId()].push_back(team.getReputation());
  }
  std::unordered_map<LeagueID, LeagueEconomy> economies;
  economies.reserve(reputations.size());
  for (const auto& [league_id, values] : reputations)
    economies.try_emplace(league_id, makeLeagueEconomy(league_id, values));
  return economies;
}

namespace ClubEconomy
{
double expectedRevenue(const LeagueEconomy& economy, std::uint8_t reputation)
{
  return static_cast<double>(economy.profile->average_revenue_eur) *
         revenueIndex(economy, reputation) / economy.revenue_normalizer;
}

std::size_t expectedPosition(const LeagueEconomy& economy,
                             std::uint8_t reputation)
{
  // Ties share the middle of their places.
  const auto& reps = economy.reputations_desc;
  const auto first =
      std::ranges::lower_bound(reps, reputation, std::greater<>{});
  const auto last =
      std::ranges::upper_bound(reps, reputation, std::greater<>{});
  const auto better = static_cast<std::size_t>(first - reps.begin());
  const auto tied = static_cast<std::size_t>(last - first);
  return better + (tied > 0 ? (tied - 1) / 2 : 0);
}

double expectedIncome(const LeagueEconomy& economy, std::uint8_t reputation)
{
  const double revenue = expectedRevenue(economy, reputation);
  const auto clubs = std::max<std::size_t>(economy.clubs, 1);
  const std::size_t position =
      std::min(expectedPosition(economy, reputation), clubs - 1);
  return monthlyBroadcasting(economy) * 12.0 +
         meritPrize(economy, position, clubs) +
         static_cast<double>(Finance::BUDGETED_CONTINENTAL_SHARE) *
             continentalPrize(economy, position, clubs) +
         revenue * static_cast<double>(economy.profile->gate_share +
                                       economy.profile->commercial_share);
}

double baseDemand(const LeagueEconomy& economy, std::uint8_t reputation)
{
  // Mid-table clubs fill the league's typical share of their ground (97% in
  // Germany, 34% in Russia) [S]; bigger clubs draw proportionally more (a
  // 20-point reputation edge fills about 30% more). [P]
  const auto fill = static_cast<double>(economy.profile->stadium_fill);
  return std::clamp(
      fill * (1.0 + 0.3 *
                        (static_cast<double>(reputation) -
                         static_cast<double>(economy.mean_reputation)) /
                        20.0),
      0.4 * fill, 1.15);
}

double fairTicketPrice(const LeagueEconomy& economy, const ClubProfile& profile)
{
  const double gate_target =
      expectedRevenue(economy, profile.reputation) *
      static_cast<double>(economy.profile->gate_share) /
      static_cast<double>(Finance::HOME_MATCHES_PER_SEASON);
  const double expected_crowd =
      static_cast<double>(
          std::max<std::uint32_t>(profile.stadium_capacity, 1)) *
      std::min(1.0, baseDemand(economy, profile.reputation));
  return std::max(1.0, gate_target / expected_crowd);
}

std::uint32_t attendance(const LeagueEconomy& economy,
                         const ClubProfile& profile, double success,
                         std::uint8_t opponent_reputation, MatchType type)
{
  const double reference = fairTicketPrice(economy, profile);
  const double price = std::max(1.0, static_cast<double>(profile.ticket_price));
  // [P] Bigger visitors draw up to 10% more, smaller ones up to 10% fewer.
  const double opponent =
      1.0 + 0.1 * std::clamp((static_cast<double>(opponent_reputation) -
                              static_cast<double>(profile.reputation)) /
                                 20.0,
                             -1.0, 1.0);
  double type_factor = 1.0;
  if (type == MatchType::FRIENDLY) type_factor = 0.3;  // [P]
  const double interest = std::max(
      static_cast<double>(Finance::CORE_SUPPORT_SHARE),
      baseDemand(economy, profile.reputation) *
          std::exp(static_cast<double>(Finance::ATTENDANCE_SUCCESS_WEIGHT) *
                   std::clamp(success, -1.0, 1.0)) *
          opponent * type_factor);
  // Near the usual price demand is inelastic; far above it the crowd goes.
  const double ratio = price / reference;
  const double overpriced = std::max(0.0, ratio - 1.0);
  const double price_factor = std::exp(
      -static_cast<double>(Finance::TICKET_PRICE_ELASTICITY) * std::log(ratio) -
      static_cast<double>(Finance::TICKET_OVERPRICING_DECAY) * overpriced *
          overpriced);
  const double capacity = static_cast<double>(profile.stadium_capacity);
  return static_cast<std::uint32_t>(std::lround(
      std::clamp(capacity * interest * price_factor, 0.0, capacity)));
}

double monthlyBroadcasting(const LeagueEconomy& economy)
{
  return static_cast<double>(economy.profile->average_revenue_eur) *
         static_cast<double>(economy.profile->tv_share) *
         static_cast<double>(Finance::TV_EQUAL_SHARE) / 12.0;
}

double monthlySponsorship(const LeagueEconomy& economy, std::uint8_t reputation)
{
  return expectedRevenue(economy, reputation) *
         static_cast<double>(economy.profile->commercial_share) / 12.0;
}

double monthlyStaffCosts(const LeagueEconomy& economy, std::uint8_t reputation)
{
  return expectedIncome(economy, reputation) *
         static_cast<double>(Finance::STAFF_SHARE) / 12.0;
}

double monthlyFacilityCosts(const LeagueEconomy& economy,
                            const ClubProfile& profile)
{
  // [P] Facilities above the club's stature cost more to run, and less when
  // below it (+-0.4% per point, 0.8x-1.2x).
  const double facility_gap =
      (static_cast<double>(profile.training_facilities) +
       static_cast<double>(profile.youth_facilities)) /
          2.0 -
      static_cast<double>(profile.reputation);
  const double facility_factor =
      std::clamp(1.0 + 0.004 * facility_gap, 0.8, 1.2);
  return expectedIncome(economy, profile.reputation) * operatingShare(economy) *
         facility_factor / 12.0;
}

double playerWageShare(const LeagueEconomy& economy)
{
  // [S] ECFIL: wages are 57-73% of revenue by league; leagues with a high
  // total wage ratio spend more on players, from ~48% in the 2. Bundesliga
  // to 68% in second tiers that overspend (Championship, Ligue 2); with
  // staff that puts their total wages above 80% of revenue.
  return std::clamp(
      static_cast<double>(Finance::PLAYER_WAGE_SHARE_BASE) +
          static_cast<double>(Finance::PLAYER_WAGE_SHARE_SLOPE) *
              (static_cast<double>(economy.profile->wage_ratio) - 0.65),
      static_cast<double>(Finance::PLAYER_WAGE_SHARE_MIN),
      static_cast<double>(Finance::PLAYER_WAGE_SHARE_MAX));
}

std::vector<std::int64_t> prizeMoney(const LeagueEconomy& economy,
                                     std::size_t league_size)
{
  std::vector<std::int64_t> merit(league_size, 0);
  for (std::size_t position = 0; position < league_size; ++position)
  {
    merit[position] = static_cast<std::int64_t>(
        std::llround(meritPrize(economy, position, league_size)));
  }
  return merit;
}

std::int64_t seasonTransferBudget(std::int64_t balance, double revenue)
{
  if (balance <= 0) return 0;
  const double budget =
      static_cast<double>(Finance::TRANSFER_BUDGET_CASH_SHARE) *
          static_cast<double>(balance) +
      static_cast<double>(Finance::TRANSFER_BUDGET_REVENUE_SHARE) * revenue;
  return std::min(balance, static_cast<std::int64_t>(std::llround(budget)));
}

std::int64_t availableTransferBudget(std::int64_t board_budget,
                                     std::int64_t balance,
                                     std::int64_t weekly_payroll,
                                     std::int64_t committed)
{
  const std::int64_t spare_cash =
      balance -
      Finance::CASH_RESERVE_WEEKS * std::max<std::int64_t>(0, weekly_payroll) -
      std::max<std::int64_t>(0, committed);
  return std::max<std::int64_t>(0, std::min(board_budget, spare_cash));
}

std::int64_t ownerRescue(std::int64_t balance, std::int64_t weekly_payroll)
{
  const std::int64_t payroll = std::max<std::int64_t>(0, weekly_payroll);
  if (payroll == 0 || balance >= -Finance::OWNER_RESCUE_TRIGGER_WEEKS * payroll)
    return 0;
  return Finance::OWNER_RESCUE_CUSHION_WEEKS * payroll - balance;
}

double parachutePayment(const LeagueEconomy& top_division, int seasons_ago)
{
  if (seasons_ago < 0 ||
      seasons_ago >= static_cast<int>(Finance::PARACHUTE_SHARES.size()))
    return 0.0;
  return monthlyBroadcasting(top_division) * 12.0 *
         static_cast<double>(
             Finance::PARACHUTE_SHARES[static_cast<std::size_t>(seasons_ago)]);
}

std::int64_t seasonWageBudget(const LeagueEconomy& economy, double revenue,
                              std::int64_t weekly_payroll)
{
  // The board funds the league's typical player wage share of the expected
  // income with a small margin, but never below the payroll it has already
  // committed to.
  const double affordable =
      revenue * playerWageShare(economy) * Finance::WAGE_BUDGET_MARGIN / 52.0;
  return std::max(static_cast<std::int64_t>(std::llround(affordable)),
                  weekly_payroll);
}

double wageIndex(double overall, int age)
{
  double age_factor = 1.0;
  if (age < 19)
    age_factor = 0.35;
  else if (age < 21)
    age_factor = 0.55;
  else if (age < 23)
    age_factor = 0.8;
  else if (age > 32)
    age_factor = 0.8;
  else if (age > 30)
    age_factor = 0.9;
  return std::pow(std::max(overall, 1.0) / 65.0,
                  static_cast<double>(
                      WorldTuning::Generation::WAGE_ABILITY_EXPONENT)) *
         age_factor;
}
}  // namespace ClubEconomy
