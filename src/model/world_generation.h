// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "global/languages.h"
#include "global/stats_config.h"
#include "global/types.h"
#include "model/club_economy.h"
#include "model/player.h"
#include "model/team.h"

class WorldRng;

/**
 * @struct NamePool
 * @brief First and last names of the installed world (or the built-in pools).
 *
 * Besides a generic list, every nationality has its own fictional but
 * culture-appropriate names. Full names listed (as hashes) under
 * "excluded_name_hashes" are never generated.
 */
struct NamePool
{
  std::vector<std::string> first_names;
  std::vector<std::string> last_names;
  std::unordered_map<Language, std::vector<std::string>> first_by_nationality;
  std::unordered_map<Language, std::vector<std::string>> last_by_nationality;
  /** nameHash() of every full name that must never be generated. */
  std::unordered_set<std::uint64_t> excluded_name_hashes;

  /**
   * Comparable form of a name: lower case ASCII, Latin accents and
   * ligatures folded (e.g. "Ø" -> "o", "ß" -> "ss"), whitespace collapsed
   * to single spaces and trimmed.
   */
  static std::string normalizeName(std::string_view name);

  /** Salted 64-bit FNV-1a hash of normalizeName(@p name). */
  static std::uint64_t nameHash(std::string_view name);

  /** True when @p full_name is on the excluded list. */
  bool isExcluded(std::string_view full_name) const;

  /** First names of @p nationality (the generic list if it has none). */
  const std::vector<std::string>& firstNames(Language nationality) const;

  /** Last names of @p nationality (the generic list if it has none). */
  const std::vector<std::string>& lastNames(Language nationality) const;

  /** Loads the name files once and returns the shared pool. */
  static const NamePool& instance();
};

/**
 * @class NameRegistry
 * @brief Full names in use in a world, so that no two players share one.
 */
class NameRegistry
{
 public:
  /** Not taken yet and not a reserved real name. */
  bool isAvailable(const std::string& full_name) const;
  void claim(const std::string& full_name);
  void clear() { names.clear(); }
  std::size_t size() const { return names.size(); }

 private:
  std::unordered_set<std::string> names;
};

/**
 * @class SquadSurnames
 * @brief Surnames of one squad: at most one surname may appear twice.
 */
class SquadSurnames
{
 public:
  bool allows(const std::string& last_name) const;
  void add(const std::string& last_name);

 private:
  std::unordered_map<std::string, int> counts;
  bool repeated = false;
};

/**
 * @namespace WorldGeneration
 * @brief Creation of clubs and players with realistic structure.
 *
 * Club reputations are tiered inside each league around the league's level;
 * squad quality, stadiums, revenue and payroll all follow from reputation,
 * so wage bills predict league strength. Players get role-dependent
 * attribute profiles, an age curve, hidden potential and personality.
 */
namespace WorldGeneration
{
/** Mean overall rating of a first-team regular at a club. */
float teamLevel(std::uint8_t reputation);

/**
 * First-team level of each club relative to its league's mean, by rank
 * (0 = strongest): normal scores with the elite pulled clear, rescaled to
 * the shape's standard deviation.
 */
std::vector<float> levelOffsets(const LeagueShape& shape, std::size_t clubs);

/**
 * Reputation the clubs of @p league spread around: the league's own
 * reputation, lowered when needed so that its strongest club stays below
 * the cap of 99.
 */
float reputationCentre(const LeagueProfile& league, std::size_t clubs);

/**
 * Mean first-team level a club of @p reputation is generated at: the
 * league's level (teamLevel of its reputation plus the shape's offset) and
 * the club's place in the league's hierarchy.
 */
float clubLevel(const LeagueEconomy& economy, std::uint8_t reputation);

/** Overall rating of @p stats for @p role (same formula as Player). */
double overallFor(PlayerRole role, const std::map<std::string, float>& stats,
                  const StatsConfig& stats_config);

/**
 * Assigns reputation, facilities, stadium and ticket price to every club
 * (except free agents), league by league. With @p assign_opening_balance the
 * clubs also get opening cash scaled to their expected income (new worlds)
 * and the registry of generated names starts afresh; legacy saves keep
 * their balance.
 *
 * With @p stature (new worlds) the pecking order comes from the world, not
 * from the seed: a club with a stated stature keeps it and takes the free
 * rank of the league's shape nearest to it; the other clubs fill the free
 * ranks in club-id order. Every career of the same world therefore has the
 * same strongest clubs. Without it (legacy saves) the clubs are ranked by a
 * shuffle of @p world_seed, as before.
 */
void generateClubProfiles(
    std::unordered_map<TeamID, Team>& teams, std::uint64_t world_seed,
    bool assign_opening_balance,
    const std::unordered_map<TeamID, std::uint8_t>* stature = nullptr);

/**
 * Draws a name of @p nationality that is free in @p registry and keeps the
 * squad's surnames distinct (one repeat allowed), then claims it. After many
 * clashes a double surname is used.
 */
std::pair<std::string, std::string> drawName(WorldRng& rng,
                                             Language nationality,
                                             NameRegistry& registry,
                                             SquadSurnames& squad);

/**
 * Highest believable potential for a player of @p age and current
 * @p overall: veterans (Generation::VETERAN_AGE and older) have at most
 * Generation::VETERAN_HEADROOM left.
 */
float maxPotential(int age, float overall);

/**
 * Generates the missing players of a 30-man squad. Wages are scaled so the
 * club's payroll (including @p existing_weekly_wages) matches the league's
 * player-wage share of the club's expected income. Names are unique among
 * the squads generated since the last generateClubProfiles() of a new world
 * and follow each player's nationality.
 */
std::vector<Player> generateSquad(const Team& team,
                                  const LeagueEconomy& economy,
                                  std::size_t existing_players,
                                  std::int64_t existing_weekly_wages,
                                  PlayerID& next_player_id,
                                  std::uint64_t world_seed,
                                  const StatsConfig& stats_config);

/**
 * Draws potential and personality for a player that has none yet (a
 * predefined or legacy player) and reserves his name for the world being
 * generated.
 */
void initializeHiddenAttributes(Player& player, WorldRng& rng,
                                const StatsConfig& stats_config);

/** Sets the opening transfer and wage budgets from cash and payroll. */
void applyOpeningBudgets(Team& team, const LeagueEconomy& economy,
                         std::int64_t weekly_payroll);
}  // namespace WorldGeneration
