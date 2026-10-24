// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "global/languages.h"
#include "global/types.h"

class GameData;
class Team;
struct LeagueEconomy;

/** @brief Type alias for staff identifiers. */
using StaffID = std::uint32_t;

/** @brief Staff roles (values are persisted). */
enum class StaffRole : std::uint8_t
{
  AssistantManager = 0,
  AttackingCoach,
  DefendingCoach,
  FitnessCoach,
  GoalkeepingCoach,
  YouthCoach,
  Physio,
  SportsScientist, /*!< Club doctor / sports science. */
  Scout,
  HeadOfYouth,
  COUNT
};

inline constexpr std::size_t STAFF_ROLE_COUNT =
    static_cast<std::size_t>(std::to_underlying(StaffRole::COUNT));

/** @brief Staff attributes on a 1-100 scale (order is persisted). */
enum class StaffAttribute : std::uint8_t
{
  Attacking = 0,    /*!< Coaching attacking play. */
  Defending,        /*!< Coaching defending. */
  Fitness,          /*!< Strength and conditioning. */
  Goalkeeping,      /*!< Coaching goalkeepers. */
  Tactical,         /*!< Tactical knowledge and match preparation. */
  YouthDevelopment, /*!< Working with young players. */
  Physiotherapy,    /*!< Injury treatment and rehabilitation. */
  SportsScience,    /*!< Load management and injury prevention. */
  JudgingAbility,   /*!< Assessing current ability. */
  JudgingPotential, /*!< Assessing potential. */
  COUNT
};

inline constexpr std::size_t STAFF_ATTRIBUTE_COUNT =
    static_cast<std::size_t>(std::to_underlying(StaffAttribute::COUNT));

/**
 * @struct StaffMember
 * @brief A coach, medic, scout or academy manager.
 *
 * team_id 0 means unattached (on the staff market). contract_years counts
 * the remaining seasons including the current one, like player contracts.
 */
struct StaffMember
{
  StaffID id = 0;
  TeamID team_id = 0;
  std::string first_name;
  std::string last_name;
  Language nationality = Language::EN;
  std::uint8_t age = 40;
  StaffRole role = StaffRole::AssistantManager;
  std::array<std::uint8_t, STAFF_ATTRIBUTE_COUNT> attributes{};
  std::uint32_t wage = 0; /*!< Weekly. */
  std::uint8_t contract_years = 0;

  std::string name() const { return first_name + " " + last_name; }
  std::uint8_t attribute(StaffAttribute which) const
  {
    return attributes[static_cast<std::size_t>(std::to_underlying(which))];
  }
};

/**
 * @struct StaffEffects
 * @brief What a club's staff does for the squad (all bounded).
 *
 * Coaching qualities are 0-1 per area (the best specialist, else 80% of the
 * assistant manager, else an untrained baseline of 0.25).
 */
struct StaffEffects
{
  float coaching_fitness = 0.25f;
  float coaching_attacking = 0.25f;
  float coaching_defending = 0.25f;
  float coaching_tactical = 0.25f;
  float coaching_goalkeeping = 0.25f;
  float coaching_youth = 0.25f;
  /** Mean coaching quality (0-1), shown as training effectiveness. */
  float training_quality = 0.25f;
  /** Injury hazard multiplier (0.88-1.08). */
  float injury_prevention = 1.08f;
  /** Layoff length multiplier applied to new injuries (0.85-1.12). */
  float layoff_multiplier = 1.12f;
  /** Potential bonus of academy graduates in overall points (-2..+4). */
  float youth_potential_bonus = -2.0f;
  /**
   * Scouting accuracy (0-1). Maps to discrimination d' = 0.8 + 1.3 * accuracy
   * (0.8 single weak source, 1.3 good scout, 2.1 elite department;
   * world-realism 1.4/9.1); see StaffModel::potentialErrorSd().
   */
  float scouting_accuracy = 0.0f;
  /** Multiplier of tactical familiarity growth (0.8-1.2). */
  float familiarity_rate = 0.8f;
  std::int64_t weekly_payroll = 0;
};

/**
 * @class StaffRoster
 * @brief Every staff member of the world with per-club effect caches.
 * Concurrent effects queries return independent snapshots. Roster mutations
 * and access through borrowed member pointers require exclusive career access.
 */
class StaffRoster
{
 public:
  bool empty() const { return members.empty(); }
  void clear();

  /** Returns a new, never reused id. */
  StaffID allocateId() { return next_id++; }

  /** The id allocateId() hands out next (persisted with the world). */
  StaffID peekNextId() const { return next_id; }

  /** Restores a saved id counter; never goes below the live ids. */
  void restoreNextId(StaffID id) { next_id = std::max(next_id, id); }

  /** Adds or replaces a member. */
  void add(StaffMember member);

  /** Removes a member (retirement). */
  bool remove(StaffID id);

  const StaffMember* find(StaffID id) const;

  /** Mutable access; call invalidate() after changing club, role or
   * attributes. */
  StaffMember* find(StaffID id);

  const std::unordered_map<StaffID, StaffMember>& all() const
  {
    return members;
  }

  /** Staff of a club by role, best first. */
  std::vector<const StaffMember*> clubStaff(TeamID team_id) const;

  /** Unattached staff, best first. */
  std::vector<const StaffMember*> market() const;

  /** Members of a club in a role. */
  std::size_t roleCount(TeamID team_id, StaffRole role) const;

  /** Cached effects of a club's staff (clubs without staff get baselines). */
  StaffEffects effects(TeamID team_id) const;

  /** Marks the effect caches stale after a change. */
  void invalidate()
  {
    const std::scoped_lock lock(cache_mutex);
    cache_valid = false;
  }

  /** Replaces the roster with persisted members. */
  void restore(std::vector<StaffMember> loaded);

 private:
  void rebuildCache() const;

  std::unordered_map<StaffID, StaffMember> members;
  mutable std::mutex cache_mutex;
  mutable std::unordered_map<TeamID, StaffEffects> effects_cache;
  mutable bool cache_valid = false;
  StaffID next_id = 1;
};

/**
 * @namespace StaffModel
 * @brief Staff ratings, effects, generation, wages and contracts.
 */
namespace StaffModel
{
/** Most members a club may employ per role. */
inline constexpr std::array<std::uint8_t, STAFF_ROLE_COUNT> ROLE_LIMITS = {
    1, 2, 2, 2, 1, 2, 4, 2, 6, 1};

/** Target size of the staff market. */
inline constexpr std::size_t MARKET_SIZE = 70;

/** Share of non-player wages paid to football staff. [P] */
inline constexpr double FOOTBALL_STAFF_SHARE = 0.35;

/** Role rating (1-100) from the role's key attributes. */
std::uint8_t rating(const StaffMember& member);

/** The two attributes that matter most for a role. */
std::array<StaffAttribute, 2> keyAttributes(StaffRole role);

/** Effects of a club's staff. */
StaffEffects computeEffects(std::span<const StaffMember* const> staff);

/** Weekly wage a member asks for on the market. */
std::uint32_t marketWage(const StaffMember& member);

/** Weekly wage asked to extend a contract. */
std::uint32_t renewalWage(const StaffMember& member);

/** Compensation for terminating a contract (up to a year's wages). */
std::int64_t severance(const StaffMember& member);

/** Staff members a club employs per role at a reputation. */
std::array<std::uint8_t, STAFF_ROLE_COUNT> roleCounts(std::uint8_t reputation);

/**
 * Generates a club's staff: numbers and quality follow reputation, wages
 * add up to the football-staff share of the club's non-player wages.
 */
std::vector<StaffMember> generateClubStaff(const Team& team,
                                           const LeagueEconomy& economy,
                                           std::uint64_t world_seed,
                                           StaffRoster& roster);

/** Generates staff for every club plus the market (new worlds and saves
 * from before staff existed). */
void generateWorld(GameData& gamedata);

/** Replaces part of the market with new candidates (monthly). */
void refreshMarket(GameData& gamedata, std::int32_t ordinal);

/**
 * Season end: ageing, retirements, contract expiry (AI clubs mostly renew
 * and refill vacancies from the market).
 * @return Names of the managed club's staff whose contracts ran out.
 */
std::vector<std::string> seasonEnd(GameData& gamedata, TeamID managed_team_id,
                                   std::uint16_t year);

/**
 * Standard deviation of a scout's potential estimate in overall points:
 * the youth potential spread (9) divided by d' = 0.8 + 1.3 * accuracy.
 */
float potentialErrorSd(float scouting_accuracy);

/** Language keys. */
const char* roleKey(StaffRole role);
const char* roleDescriptionKey(StaffRole role);
const char* attributeKey(StaffAttribute attribute);
}  // namespace StaffModel
