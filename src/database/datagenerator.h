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
#include <unordered_map>
#include <vector>

#include "league.h"
#include "player.h"
#include "team.h"

/**
 * @struct ClubIdentity
 * @brief Descriptive club data of the installed world database.
 *
 * Read from the world, which saves keep a copy of. The stadium capacity used by
 * the simulation lives in ClubProfile and is derived from reputation when the
 * world is generated; the capacity here is the pack's nominal figure.
 */
/** One full strip (shirt, shorts, socks and a trim), 0xRRGGBB each. */
struct KitPalette
{
  std::uint32_t shirt = 0;
  std::uint32_t shorts = 0;
  std::uint32_t socks = 0;
  std::uint32_t trim = 0;
  bool valid = false; /*!< True when the pack described this strip. */
};

struct ClubIdentity
{
  std::string short_name;                    /*!< Three-letter code. */
  std::uint32_t primary_colour = 0xFFFFFF;   /*!< Kit colour, 0xRRGGBB. */
  std::uint32_t secondary_colour = 0x1A1A1A; /*!< Kit colour, 0xRRGGBB. */
  /** Optional real strips; when absent the club wears primary/secondary. */
  KitPalette home_kit;
  KitPalette away_kit;
  KitPalette third_kit;
  std::string stadium_name;
  std::uint32_t stadium_capacity = 0; /*!< Nominal seats; 0 if unknown. */
  std::uint16_t founded = 0;          /*!< Founding year; 0 if unknown. */
  std::string nickname;
  /** Stature the world gives the club (1-100); 0 when the world leaves it
   * to generation. New careers rank and rate the club by it. */
  std::uint8_t reputation = 0;
};

/**
 * @class DataGenerator
 * @brief Class responsible for generating initial data for leagues, teams, and
 * players.
 */
class DataGenerator
{
 public:
  /**
   * @brief Generate a collection of leagues.
   * @return A vector of generated League objects.
   */
  static std::vector<League> generateLeagues();

  /**
   * @brief Generate a collection of teams.
   * @return A vector of generated Team objects.
   */
  static std::vector<Team> generateTeams();

  /**
   * @brief Reads short name, kit colours, stadium, founding year and
   * nickname of every club in the data pack. Missing fields keep the
   * ClubIdentity defaults, so minimal packs still load.
   * @return Identities keyed by team id.
   */
  static std::unordered_map<TeamID, ClubIdentity> loadClubIdentities();

  /** loadClubIdentities() of the installed world, parsed once per world. */
  static const std::unordered_map<TeamID, ClubIdentity>& clubIdentities();

  /**
   * @brief Optional Italian article ("article_it": "la", "il", "lo" or "l'")
   * of the clubs of the installed world that set one, for ClubArticle.
   * @return Articles keyed by club name, parsed once per world.
   */
  static const std::unordered_map<std::string, std::string>& clubArticles();

  /**
   * @brief Skin tone (0 lightest .. 7 darkest) of every authored player of
   * the installed world: the "skin" field, else a plausible tone for the
   * nationality. Generated players get a deterministic tone from their id.
   * @return Skin tones keyed by player id, built once per world.
   */
  static const std::unordered_map<PlayerID, std::uint8_t>& playerSkinTones();

  /**
   * @brief Loads the predefined players and completes every club's squad
   * with generated players (see WorldGeneration). Club profiles must be
   * assigned first; generation is deterministic for the world seed.
   * @return A vector of generated Player objects.
   */
  static std::vector<Player> generatePlayers(const class GameData& gamedata);
};
