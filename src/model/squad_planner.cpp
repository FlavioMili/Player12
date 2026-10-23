// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/squad_planner.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include "model/world_tuning.h"

namespace
{
using Development = WorldTuning::Development;

constexpr std::array<int, PLANNER_GROUP_COUNT> TARGET_DEPTH = {3, 5, 2, 2,
                                                               6, 4, 3};
constexpr std::array<int, PLANNER_GROUP_COUNT> DEFAULT_STARTERS = {1, 2, 1, 1,
                                                                   3, 2, 1};
/** Age from which a first choice needs a successor. */
constexpr int SUCCESSION_AGE = 32;
/** Age from which a player is flagged as declining. */
constexpr int AGEING_AGE = 31;
/** First choices this far below the XI's average ask for an upgrade. */
constexpr float UPGRADE_GAP = 4.0f;
/** Players above the healthy depth before a sale is suggested. */
constexpr int SURPLUS_MARGIN = 2;
/** Share of overall carried by the physical attributes (roughly). */
constexpr float PHYSICAL_SHARE = 0.4f;
/** Growth factor for an average season of minutes (0.6 + 0.4 * 0.5). */
constexpr float AVERAGE_MINUTES_FACTOR = 0.8f;

float growthRate(int age)
{
  if (age <= 19) return Development::GROWTH_RATE_TEEN;
  if (age <= 21) return Development::GROWTH_RATE_20_21;
  if (age <= 23) return Development::GROWTH_RATE_22_23;
  if (age <= 25) return Development::GROWTH_RATE_24_25;
  if (age <= 28) return Development::GROWTH_RATE_26_28;
  return 0.0f;
}

/** Yearly decline of overall at @p age (after the birthday). */
float declineRate(int age)
{
  return PHYSICAL_SHARE * Development::physicalDecline(age) +
         (1.0f - PHYSICAL_SHARE) * Development::technicalDecline(age);
}

std::size_t ageBand(int age)
{
  std::size_t band = 0;
  while (band < AGE_BAND_LIMITS.size() && age > AGE_BAND_LIMITS[band]) ++band;
  return band;
}

std::size_t index(PlannerGroup group)
{
  return static_cast<std::size_t>(std::to_underlying(group));
}
}  // namespace

PlannerGroup SquadPlanner::groupOf(PlayerRole role)
{
  using enum PlayerRole;
  switch (role)
  {
    case GK:
      return PlannerGroup::Goalkeeper;
    case CB:
      return PlannerGroup::CentreBack;
    case LB:
      return PlannerGroup::LeftBack;
    case RB:
      return PlannerGroup::RightBack;
    case LM:
    case RM:
    case LW:
    case RW:
      return PlannerGroup::Wide;
    case ST:
      return PlannerGroup::Striker;
    case CDM:
    case CM:
    case CAM:
    case UNKNOWN:
      break;
  }
  return PlannerGroup::Midfield;
}

const char* SquadPlanner::groupKey(PlannerGroup group)
{
  using enum PlannerGroup;
  switch (group)
  {
    case Goalkeeper:
      return "PLANNER_GROUP_GOALKEEPERS";
    case CentreBack:
      return "PLANNER_GROUP_CENTRE_BACKS";
    case LeftBack:
      return "PLANNER_GROUP_LEFT_BACKS";
    case RightBack:
      return "PLANNER_GROUP_RIGHT_BACKS";
    case Midfield:
      return "PLANNER_GROUP_MIDFIELD";
    case Wide:
      return "PLANNER_GROUP_WIDE";
    case Striker:
    case COUNT:
      break;
  }
  return "PLANNER_GROUP_STRIKERS";
}

int SquadPlanner::targetDepth(PlannerGroup group)
{
  return group < PlannerGroup::COUNT ? TARGET_DEPTH[index(group)] : 0;
}

int SquadPlanner::defaultStarters(PlannerGroup group)
{
  return group < PlannerGroup::COUNT ? DEFAULT_STARTERS[index(group)] : 0;
}

PlannerPlayer SquadPlanner::project(const PlannerPlayer& player, int seasons)
{
  PlannerPlayer projected = player;
  for (int season = 0; season < seasons; ++season)
  {
    const float gap = std::max(0.0f, projected.potential - projected.overall);
    projected.overall +=
        gap * growthRate(projected.age) * AVERAGE_MINUTES_FACTOR;
    ++projected.age;
    projected.overall *= 1.0f - declineRate(projected.age);
    projected.contract_years = std::max(0, projected.contract_years - 1);
  }
  return projected;
}

SquadPlan SquadPlanner::build(
    const std::vector<PlannerPlayer>& squad, int season_offset,
    const std::array<int, PLANNER_GROUP_COUNT>& starters)
{
  SquadPlan plan;
  plan.season_offset = std::max(0, season_offset);
  const bool from_xi =
      std::ranges::any_of(starters, [](int n) { return n > 0; });
  for (std::size_t group = 0; group < PLANNER_GROUP_COUNT; ++group)
  {
    plan.groups[group].group = static_cast<PlannerGroup>(group);
    plan.groups[group].target = TARGET_DEPTH[group];
    plan.groups[group].starters =
        from_xi ? starters[group] : DEFAULT_STARTERS[group];
  }

  int age_total = 0;
  for (const PlannerPlayer& player : squad)
  {
    // A contract of n seasons covers n - 1 seasons after the current one.
    if (plan.season_offset > 0 && player.contract_years <= plan.season_offset)
    {
      plan.departures.push_back(player.id);
      continue;
    }
    DepthEntry entry;
    entry.player = project(player, plan.season_offset);
    entry.expiring = entry.player.contract_years <= 1;
    entry.ageing = entry.player.age >= AGEING_AGE;
    ++plan.age_bands[ageBand(entry.player.age)];
    age_total += entry.player.age;
    ++plan.squad_size;
    const auto years = std::max(1, entry.player.contract_years);
    auto expiry =
        std::ranges::find(plan.expiries, years, &ContractExpiry::years_left);
    if (expiry == plan.expiries.end())
      expiry =
          plan.expiries.insert(plan.expiries.end(), ContractExpiry{years, {}});
    expiry->players.push_back(player.id);
    plan.groups[index(groupOf(player.role))].players.push_back(
        std::move(entry));
  }
  plan.average_age =
      plan.squad_size > 0
          ? static_cast<float>(age_total) / static_cast<float>(plan.squad_size)
          : 0.0f;
  std::ranges::sort(plan.expiries, {}, &ContractExpiry::years_left);

  float xi_total = 0.0f;
  int xi_count = 0;
  for (GroupDepth& depth : plan.groups)
  {
    // Selected players first (the manager's choice), then by ability.
    std::ranges::sort(
        depth.players,
        [](const DepthEntry& a, const DepthEntry& b)
        {
          return std::tuple(!a.player.in_xi, -a.player.overall, a.player.id) <
                 std::tuple(!b.player.in_xi, -b.player.overall, b.player.id);
        });
    int counted = 0;
    float first_total = 0.0f;
    int first_count = 0;
    for (std::size_t rank = 0; rank < depth.players.size(); ++rank)
    {
      DepthEntry& entry = depth.players[rank];
      if (!entry.player.listed) ++counted;
      if (static_cast<int>(rank) < depth.starters)
      {
        entry.tier = DepthTier::FirstChoice;
        first_total += entry.player.overall;
        ++first_count;
      }
      else
      {
        entry.tier =
            entry.player.age <= 21 ? DepthTier::Prospect : DepthTier::Backup;
      }
    }
    depth.missing = std::max(0, depth.target - counted);
    depth.surplus = std::max(0, counted - depth.target);
    depth.first_choice_average =
        first_count > 0 ? first_total / static_cast<float>(first_count) : 0.0f;
    xi_total += first_total;
    xi_count += first_count;
  }

  // Needs: missing depth first, then weak first choices, successions and
  // finally surplus players.
  const float xi_average =
      xi_count > 0 ? xi_total / static_cast<float>(xi_count) : 0.0f;
  for (const GroupDepth& depth : plan.groups)
    if (depth.missing > 0)
      plan.needs.push_back({depth.group, NeedKind::Missing, depth.missing, 0});
  for (const GroupDepth& depth : plan.groups)
  {
    if (!depth.players.empty() &&
        depth.first_choice_average < xi_average - UPGRADE_GAP)
      plan.needs.push_back({depth.group, NeedKind::Upgrade, 1, 0});
  }
  for (const GroupDepth& depth : plan.groups)
  {
    for (const DepthEntry& entry : depth.players)
    {
      if (entry.tier != DepthTier::FirstChoice) break;
      if (entry.player.age >= SUCCESSION_AGE || entry.expiring)
        plan.needs.push_back(
            {depth.group, NeedKind::Succession, 1, entry.player.id});
    }
  }
  for (const GroupDepth& depth : plan.groups)
    if (depth.surplus >= SURPLUS_MARGIN)
      plan.needs.push_back({depth.group, NeedKind::Surplus, depth.surplus, 0});
  return plan;
}
