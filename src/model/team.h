// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "finances.h"
#include "global/types.h"
#include "lineup.h"
#include "strategy.h"

/**
 * @struct ClubProfile
 * @brief Persistent club attributes that drive revenue and development.
 */
struct ClubProfile
{
  std::uint8_t reputation = 50; /*!< 1-100 club stature. */
  std::uint32_t stadium_capacity = 10'000;
  std::uint32_t ticket_price = 20;       /*!< Average yield per spectator. */
  std::uint8_t training_facilities = 50; /*!< 1-100. */
  std::uint8_t youth_facilities = 50;    /*!< 1-100. */
};

/** @brief Result letters stored in the recent-form string. */
enum class MatchOutcome : char
{
  Win = 'W',
  Draw = 'D',
  Loss = 'L'
};

/**
 * @class Team
 * @brief Represents a football team.
 */
class Team
{
 public:
  /**
   * @brief Constructs a Team object.
   * @param team_id The unique ID for the team.
   * @param team_league_id The ID of the league the team belongs to.
   * @param team_name The name of the team.
   * @param initial_balance The initial financial balance.
   * @param initial_player_ids Optional list of initial player IDs.
   * @param strategy Optional strategy for the team.
   * @param lineup_data Optional lineup data for the team.
   */
  Team(TeamID team_id, LeagueID team_league_id, std::string_view team_name,
       int64_t initial_balance,
       const std::vector<PlayerID>& initial_player_ids = {},
       const Strategy& strategy = Strategy{},
       const Lineup& lineup_data = Lineup{});

  // Accessors

  /** @brief Gets the team's ID. */
  TeamID getId() const;

  /** @brief Gets the ID of the team's league. */
  LeagueID getLeagueId() const;

  /** @brief Moves the team to another league (promotion/relegation). */
  void setLeagueId(LeagueID new_league_id);

  /** @brief Gets the name of the team. */
  const std::string& getName() const;

  /** @brief Player IDs of the senior squad (academy players excluded). */
  const std::vector<PlayerID>& getPlayerIDs() const;

  /** @brief Player IDs of the club's academy (U18 squad and trialists). */
  const std::vector<PlayerID>& getAcademyIDs() const;

  // Manage roster

  /**
   * @brief Adds a player to the team.
   * @param id The player's ID.
   */
  void addPlayerID(PlayerID id);

  /** @brief Adds a player to the club's academy. */
  void addAcademyID(PlayerID id);

  /**
   * @brief Moves a club player between the senior squad and the academy
   * (use GameData::setAcademyMember, which keeps the player's flag too).
   */
  void setAcademyMember(PlayerID id, bool academy);

  /**
   * @brief Removes a player from the team (senior squad or academy).
   * @param id The player's ID.
   * @return True if successful, false otherwise.
   */
  bool removePlayerID(PlayerID id);

  // Lineup access

  /** @brief Gets the team's lineup (non-const). */
  Lineup& getLineup();

  /** @brief Gets the team's lineup (const). */
  const Lineup& getLineup() const;

  // Strategy access

  /** @brief Gets the team's strategy (non-const). */
  Strategy& getStrategy();

  /** @brief Gets the team's strategy (const). */
  const Strategy& getStrategy() const;

  /** @brief Sets the team's strategy. */
  void setStrategy(const Strategy& strategy);

  /**
   * @brief Generates the best starting XI automatically based on stats.
   *
   * Injured players are left out.
   * @param stats_config The configuration used to evaluate players.
   */
  void generateStartingXI(const class GameData& gamedata,
                          const StatsConfig& stats_config);

  // Finances access

  /** @brief Gets the team's finances (non-const). */
  Finances& getFinances() noexcept;

  /** @brief Gets the team's finances (const). */
  const Finances& getFinances() const noexcept;

  // Club profile

  /** @brief Reputation, stadium, ticket price and facilities. */
  const ClubProfile& getProfile() const noexcept;

  /** @brief Replaces the club profile. */
  void setProfile(const ClubProfile& profile);

  /** @brief Club reputation (1-100). */
  std::uint8_t getReputation() const noexcept;

  /** @brief Stadium capacity in seats. */
  std::uint32_t getStadiumCapacity() const noexcept;

  /** @brief Last results, newest first ("WDL..."), at most 5 letters. */
  const std::string& getRecentForm() const noexcept;

  /** @brief Restores the recent-form string (persistence). */
  void setRecentForm(std::string_view form);

  /** @brief Prepends a result to the recent form. */
  void pushResult(MatchOutcome outcome);

 private:
  TeamID id;
  LeagueID league_id;
  std::string name;

  /*
   * the main idea is to have a players map *map[player_id]-- > player;
   * and here we just iterate on the ids to get *all players.
   */
  std::vector<PlayerID> player_ids;
  std::vector<PlayerID> academy_ids;

  Strategy team_strategy;
  Lineup lineup;
  Finances finances;
  ClubProfile profile;
  std::string recent_form;
};
