// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>

// ID Types
/** @brief Type alias for League identifiers. */
using LeagueID = uint16_t;

/** @brief Type alias for nationality identifiers (underlying type of Language). */
using NationalityID = uint16_t;

/** @brief Type aliases for world entity identifiers. */
using CountryID = uint16_t;
using RegionID = uint16_t;
using ConfederationID = uint16_t;
using LanguageID = uint16_t;

/** @brief Type alias for Team identifiers. */
using TeamID = uint16_t;

/** @brief Type alias for Player identifiers. */
using PlayerID = uint32_t;

/**
 * @enum SeasonPhase
 * @brief Represents the different phases of a season.
 */
enum class SeasonPhase : uint8_t
{
  PRE_SEASON,     /*!< Phase before the regular season begins */
  REGULAR_SEASON, /*!< The main competitive phase of the season */
  POST_SEASON,    /*!< Phase after the regular season (e.g., playoffs) */
  OFF_SEASON      /*!< Period between seasons with no matches */
};

/**
 * @enum MatchType
 * @brief Represents the type or category of a match.
 */
enum class MatchType : uint8_t
{
  LEAGUE = 0,     /*!< A regular league match */
  FRIENDLY = 1,   /*!< A non-competitive friendly match */
  CUP = 2,        /*!< A cup tournament match */
  CONTINENTAL = 3 /*!< A continental club competition match */
};

/**
 * @struct Matchup
 * @brief Represents a single match between two teams.
 */
struct Matchup
{
  TeamID home_team_id; /*!< ID of the home team */
  TeamID away_team_id; /*!< ID of the away team */
  MatchType type;      /*!< Type of the match */
};

/**
 * @enum PlayerRole
 * @brief Represents a specific tactical role on the pitch.
 */
enum class PlayerRole : uint8_t
{
  GK,  /*!< Goalkeeper */
  CB,  /*!< Center Back */
  LB,  /*!< Left Back */
  RB,  /*!< Right Back */
  CDM, /*!< Central Defensive Midfielder */
  CM,  /*!< Central Midfielder */
  CAM, /*!< Central Attacking Midfielder */
  LM,  /*!< Left Midfielder */
  RM,  /*!< Right Midfielder */
  LW,  /*!< Left Winger */
  RW,  /*!< Right Winger */
  ST,  /*!< Striker */
  UNKNOWN
};

/**
 * @struct Vector2F
 * @brief A simple 2D float vector for pitch coordinates.
 */
struct Vector2F
{
  float x;
  float y;
};