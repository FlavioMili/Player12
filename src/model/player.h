// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "global/global.h"
#include "global/languages.h"
#include "global/stats_config.h"
#include "global/types.h"
#include "model/injury.h"

/**
 * @enum Foot
 * @brief Represents a player's preferred foot.
 */
enum class Foot : bool
{
  Left = false,
  Right = true
};

/**
 * @enum TransferStatus
 * @brief Represents a player's transfer listing status.
 */
enum class TransferStatus
{
  Listed,
  NotListed
};

/**
 * @struct PlayerTraits
 * @brief Hidden personality and physical traits on a 1-100 scale.
 *
 * Professionalism scales training effort and slows decline, ambition drives
 * expectations about playing time and wages, temperament damps morale swings
 * (high = composed), loyalty softens reactions to transfer interest and
 * injury proneness scales the injury hazard (50 = average).
 */
struct PlayerTraits
{
  std::uint8_t professionalism = 50;
  std::uint8_t ambition = 50;
  std::uint8_t temperament = 50;
  std::uint8_t loyalty = 50;
  std::uint8_t injury_proneness = 50;
};

/**
 * @struct PlayerDynamics
 * @brief Day-to-day state that changes between matches.
 *
 * Day stamps are day ordinals (see dayOrdinal() in world_rng.h); 0 means
 * "never".
 */
struct PlayerDynamics
{
  static constexpr std::size_t FORM_WINDOW = 5;

  float condition = 100.0f;   /*!< 0-100 physical freshness. */
  float sharpness = 60.0f;    /*!< 0-100 match fitness. */
  float morale = 60.0f;       /*!< 0-100. */
  float playing_share = 0.0f; /*!< Smoothed share of minutes played. */
  InjuryType injury = InjuryType::None;
  std::uint16_t injury_days = 0; /*!< Days until fit again. */
  InjuryType last_injury = InjuryType::None;
  std::int32_t last_injury_day = 0;
  std::int32_t last_match_day = 0;
  std::uint16_t season_appearances = 0;
  std::uint16_t season_minutes = 0;
  std::uint16_t week_minutes = 0;
  std::uint8_t transfer_interest_weeks = 0; /*!< Unsettled by a bid. */
  std::uint8_t rating_count = 0;
  std::array<float, FORM_WINDOW> recent_ratings{}; /*!< Newest first. */
};

/**
 * @class Player
 * @brief Persistent career identity, attributes, traits and day-to-day state.
 *
 * On-pitch positions, intentions and live fatigue belong to MatchPlayer in
 * match_engine.h. The engine borrows this Player as read-only input and the
 * career later applies its match consequences. PlayerAgent models contract
 * negotiations, not on-pitch AI. See docs/development/player-behavior.md.
 * Concurrent const queries may refresh the price cache. Attribute mutations
 * require exclusive career ownership, as with other persistent model state.
 */
class Player
{
 public:
  /**
   * @brief Constructs a Player object.
   * @param new_id The unique ID for the player.
   * @param new_team_id The ID of the team the player belongs to.
   * @param new_first_name The player's first name.
   * @param new_last_name The player's last name.
   * @param new_role The player's role (e.g., ST, CB).
   * @param new_nationality The player's nationality.
   * @param new_wage The player's wage.
   * @param new_status The player's status.
   * @param new_age The player's age.
   * @param new_contract_years Years remaining on the contract.
   * @param new_height The player's height in cm.
   * @param new_foot The player's preferred foot.
   * @param new_stats A map of the player's attributes/stats.
   */
  Player(PlayerID new_id, TeamID new_team_id, std::string_view new_first_name,
         std::string_view new_last_name, PlayerRole new_role,
         Language new_nationality, uint32_t new_wage, uint32_t new_status,
         uint8_t new_age, uint8_t new_contract_years, uint8_t new_height,
         Foot new_foot, std::map<std::string, float> new_stats);

  /** @brief Gets the player's ID. */
  PlayerID getId() const;

  /** @brief Gets the ID of the player's team. */
  TeamID getTeamId() const;

  /** @brief Sets the ID of the player's team. */
  void setTeamId(TeamID id);

  /** @brief Gets the player's full name. */
  std::string getName() const;

  /** @brief Gets the player's first name. */
  const std::string& getFirstName() const;

  /** @brief Gets the player's last name. */
  const std::string& getLastName() const;

  /** @brief Gets the player's age. */
  int getAge() const;

  /** @brief Sets the player's age. */
  void setAge(uint8_t new_age);

  /** @brief Gets the player's role. */
  PlayerRole getRole() const;

  /** @brief Gets the player's nationality. */
  Language getNationality() const;

  /** @brief Gets the player's wage. */
  uint32_t getWage() const;

  /** Updates the weekly wage after a successful contract negotiation. */
  void setWage(uint32_t wage);

  /** @brief Gets the player's remaining contract years. */
  uint8_t getContractYears() const;

  /** Updates the remaining contract duration. */
  void setContractYears(uint8_t years);

  /** Decrements a non-zero contract and returns true when it expires. */
  bool advanceContractYear();

  /** @brief Gets the player's height. */
  uint8_t getHeight() const;

  /** @brief Gets the player's preferred foot. */
  Foot getFoot() const;

  /** @brief Gets the player's status. */
  uint32_t getStatus() const;

  /**
   * @brief Calculates the player's overall rating based on stats and
   * configuration.
   * @param stats_config Configuration weights for the stats.
   * @return The overall rating.
   */
  double getOverall(const StatsConfig& stats_config) const;

  /** @brief Gets the player's stats map. */
  const std::map<std::string, float>& getStats() const;

  /** @brief Sets the player's stats map. */
  void setStats(const std::map<std::string, float>& new_stats);

  /**
   * @brief Increases the player's age by 1 and applies yearly ageing.
   *
   * Physical attributes decline first (-1%/year at 30-32, -3%/year after 32,
   * stamina at half rate), technical attributes from 32 and vision from 35.
   * Professional players decline more slowly.
   */
  void agePlayer();

  /**
   * @brief Improves every listed stat by @p amount (clamped to the maximum).
   *
   * Because role weights sum to one, training all of a role's focus stats by
   * x raises the overall rating by x.
   */
  void train(const std::vector<std::string>& focus_stats,
             float amount = PLAYER_STAT_INCREASE_BASE);

  // Hidden attributes & dynamic state

  /** @brief Hidden potential on the overall-rating scale (never shown raw). */
  float getPotential() const;

  /** @brief Sets the hidden potential. */
  void setPotential(float potential);

  /** @brief Hidden personality traits. */
  const PlayerTraits& getTraits() const;

  /** @brief Replaces the hidden personality traits. */
  void setTraits(const PlayerTraits& traits);

  /** @brief Condition, sharpness, morale, injury and form state. */
  const PlayerDynamics& getDynamics() const;

  /** @brief Mutable dynamic state for the world simulation. */
  PlayerDynamics& mutableDynamics();

  /** @brief True when the player is fit enough to be selected. */
  bool isAvailable() const;

  /** @brief Average of the recent match ratings, 0 when none. */
  float getForm() const;

  /** @brief Records a match rating (1-10) in the recent form window. */
  void pushMatchRating(float rating);

  // Market Value & Transfer Logic

  /** @brief Gets the player's market value. */
  uint32_t getMarketValue() const;

  /**
   * @brief Updates the player's market value.
   * @param stats_config The stats configuration to use for the calculation.
   */
  void updateMarketValue(const StatsConfig& stats_config) const;

  /**
   * @brief Market value model shared by own players and scouting estimates:
   * ability, an inverted-U age curve, a potential premium that fades out
   * between 22 and 25, and the remaining contract length.
   * @return Value in euros, within EUR 10K - 250M.
   */
  static double valueFor(double overall, double potential, int age,
                         double contract_years);

  /** @brief Bitmask bit for transfer-listed status. */
  static constexpr uint32_t TRANSFER_LISTED_BIT = 0x01U;

  /** @brief Sets the player's transfer status. */
  void setTransferStatus(TransferStatus status);

  /** @brief Gets the player's transfer status. */
  TransferStatus getTransferStatus() const;

  /** @brief Bitmask bit for players of a youth academy (U18 or trialist). */
  static constexpr uint32_t ACADEMY_BIT = 0x02U;

  /**
   * @brief Marks the player as part of his club's academy: he is not a
   * first-team player (squad counts, automatic line-ups).
   */
  void setAcademyPlayer(bool academy);

  /** @brief True for U18 players and intake trialists. */
  bool isAcademyPlayer() const;

  /** @brief Squad number (1-99), 0 when he has none (academy players). */
  std::uint8_t getSquadNumber() const { return _squad_number; }

  /** @brief Sets the squad number; uniqueness is kept by GameData. */
  void setSquadNumber(std::uint8_t number) { _squad_number = number; }

 private:
  // 32-bit fields first
  PlayerID _id;
  TeamID _team_id;
  uint32_t _wage;
  uint32_t _status;
  /** Independently copied cache; concurrent const price queries may refresh it.
   */
  struct MarketValueCache
  {
    MarketValueCache() = default;
    ~MarketValueCache() = default;
    MarketValueCache(const MarketValueCache& other) noexcept
        : value(other.load())
    {
    }
    MarketValueCache& operator=(const MarketValueCache& other) noexcept
    {
      store(other.load());
      return *this;
    }
    MarketValueCache(MarketValueCache&& other) noexcept
        : MarketValueCache(other)
    {
    }
    MarketValueCache& operator=(MarketValueCache&& other) noexcept
    {
      return *this = other;
    }
    uint32_t load() const noexcept { return value.load(); }
    void store(uint32_t next) const noexcept { value.store(next); }

   private:
    mutable std::atomic<uint32_t> value{0};
  };
  MarketValueCache _cached_market_value;

  // strings (non-POD, heap allocated, alignment not a problem)
  std::string _first_name;
  std::string _last_name;

  // Enums and small ints grouped together
  PlayerRole _role;
  Language _nationality;
  TransferStatus _transfer_status = TransferStatus::NotListed;
  uint8_t _age;
  uint8_t _contract_years;
  uint8_t _height;
  Foot _foot;
  std::uint8_t _squad_number = 0;

  float _potential = 0.0f;
  PlayerTraits _traits;
  PlayerDynamics _dynamics;

  // stats container
  std::map<std::string, float> _stats;
};
