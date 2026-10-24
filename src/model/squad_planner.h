// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "global/types.h"

/**
 * @enum PlannerGroup
 * @brief Position groups of the depth chart (the groups computer-managed
 * clubs count when they look for missing or surplus players).
 */
enum class PlannerGroup : std::uint8_t
{
  Goalkeeper,
  CentreBack,
  LeftBack,
  RightBack,
  Midfield, /*!< Defensive, central and attacking midfielders. */
  Wide,     /*!< Wide midfielders and wingers. */
  Striker,
  COUNT
};

inline constexpr std::size_t PLANNER_GROUP_COUNT =
    static_cast<std::size_t>(std::to_underlying(PlannerGroup::COUNT));

/** @brief Place of a player in his group's depth chart. */
enum class DepthTier : std::uint8_t
{
  FirstChoice,
  Backup,
  Prospect /*!< Young player behind the first choices. */
};

/** @brief What the planner knows about one player of the squad. */
struct PlannerPlayer
{
  PlayerID id = 0;
  PlayerRole role = PlayerRole::UNKNOWN;
  int age = 0;
  float overall = 0.0f;
  /** Midpoint of the club's potential estimate (never the hidden value). */
  float potential = 0.0f;
  /** Remaining seasons including the current one; <= 1 expires in June. */
  int contract_years = 0;
  bool in_xi = false; /*!< Selected in the current starting XI. */
  bool injured = false;
  bool listed = false; /*!< Transfer listed: not counted as depth. */
};

/** @brief One player's line in the depth chart of the viewed season. */
struct DepthEntry
{
  PlannerPlayer player; /*!< Projected age, overall and contract. */
  DepthTier tier = DepthTier::Backup;
  /** Contract ends at the close of the viewed season. */
  bool expiring = false;
  /** 31 or older in the viewed season: expected to decline. */
  bool ageing = false;
};

/** @brief Depth of one position group. */
struct GroupDepth
{
  PlannerGroup group = PlannerGroup::Goalkeeper;
  int target = 0;   /*!< Healthy squad depth for the group. */
  int starters = 0; /*!< Starters the formation fields in the group. */
  std::vector<DepthEntry> players; /*!< First choices first, then by ability. */
  int missing = 0;
  int surplus = 0;
  float first_choice_average = 0.0f; /*!< 0 without players. */
};

/** @brief Why the planner suggests recruitment (or a sale) for a group. */
enum class NeedKind : std::uint8_t
{
  Missing,    /*!< Fewer players than the healthy depth. */
  Upgrade,    /*!< First choices clearly weaker than the rest of the XI. */
  Succession, /*!< A first choice is ageing or his contract runs out. */
  Surplus     /*!< Two or more players more than needed. */
};

struct PlannerNeed
{
  PlannerGroup group = PlannerGroup::Goalkeeper;
  NeedKind kind = NeedKind::Missing;
  int count = 0;          /*!< Players missing / surplus / concerned. */
  PlayerID player_id = 0; /*!< Succession: the first choice concerned. */
};

/** @brief Age bands of the age profile. */
inline constexpr std::array<int, 4> AGE_BAND_LIMITS = {21, 25, 29, 32};
inline constexpr std::size_t AGE_BAND_COUNT = AGE_BAND_LIMITS.size() + 1;

/** @brief Players whose contract ends in a given season. */
struct ContractExpiry
{
  int years_left = 0; /*!< 1 = end of the viewed season. */
  std::vector<PlayerID> players;
};

/**
 * @struct SquadPlan
 * @brief Depth chart, needs, age profile and contract calendar of a squad
 * for the current season (offset 0) or a projected later one.
 */
struct SquadPlan
{
  int season_offset = 0;
  std::array<GroupDepth, PLANNER_GROUP_COUNT> groups{};
  std::vector<PlannerNeed> needs; /*!< Most pressing first. */
  std::array<int, AGE_BAND_COUNT> age_bands{};
  float average_age = 0.0f;
  std::vector<ContractExpiry> expiries; /*!< Soonest first, no empty years. */
  /** Players who leave before the viewed season (expired contracts). */
  std::vector<PlayerID> departures;
  int squad_size = 0;
};

namespace SquadPlanner
{
/** Group of a playing role (UNKNOWN counts as midfield). */
PlannerGroup groupOf(PlayerRole role);

/** Language key naming @p group (e.g. "PLANNER_GROUP_GOALKEEPERS"). */
const char* groupKey(PlannerGroup group);

/** Healthy squad depth per group (3 GK, 5 CB, 2+2 FB, 6 MID, 4 wide, 3 ST). */
int targetDepth(PlannerGroup group);

/** Starters of a typical back-four formation (1, 2, 1, 1, 3, 2, 1). */
int defaultStarters(PlannerGroup group);

/**
 * The player @p seasons seasons later: older, with a shorter contract and
 * a projected overall that grows towards the potential estimate while
 * young and declines from 30 (rates of WorldTuning::Development, an
 * average playing time assumed). contract_years may reach 0 (left).
 */
PlannerPlayer project(const PlannerPlayer& player, int seasons);

/**
 * Builds the plan of @p squad for the season @p season_offset seasons from
 * now (0 = current). Later seasons drop players whose contract has run out
 * and project the others. @p starters gives the XI's players per group
 * (all zero: defaultStarters()).
 */
SquadPlan build(const std::vector<PlannerPlayer>& squad, int season_offset,
                const std::array<int, PLANNER_GROUP_COUNT>& starters = {});
}  // namespace SquadPlanner
