// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <unordered_map>
#include <vector>

#include "gamedate.h"
#include "global/global.h"
#include "global/stats_config.h"
#include "global/types.h"
#include "model/league.h"
#include "model/player.h"
#include "model/squad_numbers.h"
#include "model/staff.h"
#include "model/team.h"
#include "model/training.h"

struct TransferListing;

class DatabaseConnection;

/**
 * @class GameData
 * @brief Singleton class that acts as an in-memory cache for the database
 * state.
 *
 * GameData is responsible for loading the initial game state from SQLite,
 * holding all core entities (Leagues, Teams, Players), and facilitating
 * high-performance read/write access during gameplay without hitting the disk.
 */
class GameData
{
 public:
  /**
   * @brief Constructs a new GameData instance.
   */
  GameData();

  GameData(const GameData&) = delete;
  GameData& operator=(const GameData&) = delete;

  // ---------------- DB ----------------
  /**
   * @brief Loads the initial game state from the database.
   * @param database_ptr Shared pointer to the active DatabaseConnection.
   * @return True if successful, false otherwise.
   */
  bool loadFromDB(std::shared_ptr<DatabaseConnection> database_ptr,
                  std::stop_token stop = {});

  /**
   * @brief Saves the current in-memory state back to the database.
   * @return True if successful, false otherwise.
   */
  bool saveToDB() const;

  // ---------------- StatsConfig ----------------
  const StatsConfig& getStatsConfig() const;

  // ---------------- League ----------------
  void addLeague(LeagueID id, const League& league);

  std::optional<std::reference_wrapper<const League>> getLeague(
      LeagueID id) const;

  const std::unordered_map<LeagueID, League>& getLeagues() const;

  std::unordered_map<LeagueID, League>& getLeagues();

  const std::vector<std::reference_wrapper<const League>>& getLeaguesVector()
      const;

  // ---------------- Team ----------------
  void addTeam(TeamID id, const Team& team);

  std::optional<std::reference_wrapper<Team>> getTeam(TeamID id);

  std::optional<std::reference_wrapper<const Team>> getTeam(TeamID id) const;

  const std::unordered_map<TeamID, Team>& getTeams() const;

  std::unordered_map<TeamID, Team>& getTeams();

  const std::vector<std::reference_wrapper<const Team>>& getTeamsVector() const;

  void ageAllPlayers();

  /**
   * Advances contracts and releases newly out-of-contract players. Clubs
   * that lose a player get a new line-up, except @p keep_selection (the
   * manager's club), whose gaps the caller fills.
   */
  std::vector<PlayerID> advanceContractsAndReleasePlayers(
      TeamID keep_selection = FREE_AGENTS_TEAM_ID);

  void addPlayer(PlayerID id, const Player& player);
  std::optional<std::reference_wrapper<const Player>> getPlayer(
      PlayerID id) const;
  const std::unordered_map<PlayerID, Player>& getPlayers() const;
  std::unordered_map<PlayerID, Player>& getPlayers();
  const std::vector<std::reference_wrapper<const Player>>& getPlayersVector()
      const;
  /** Senior squad of a club: academy players (U18 squad and intake
   * trialists) are listed by getAcademyForTeam(). */
  const std::vector<std::reference_wrapper<const Player>>& getPlayersForTeam(
      TeamID team_id) const;
  /** Academy players of a club (still club players for contracts and
   * wages, but not part of the senior squad). */
  const std::vector<std::reference_wrapper<const Player>>& getAcademyForTeam(
      TeamID team_id) const;
  /**
   * Moves a player of his club between the senior squad and the academy:
   * sets Player::isAcademyPlayer() and keeps the club's lists in sync.
   */
  void setAcademyMember(PlayerID id, bool academy);
  bool removePlayer(PlayerID id);
  /** Moves a player to another club's senior squad (academy membership
   * does not travel with him). He keeps his squad number when it is free
   * at the new club, otherwise he gets a free one (SquadNumbers). */
  void transferPlayer(PlayerID id, TeamID new_team_id);

  /** Gives @p id squad number @p number (1-99) at his club; a teammate
   * wearing it takes his old number. */
  SquadNumbers::Change setSquadNumber(PlayerID id, int number);
  /**
   * Numbers the club's senior players who have none or share one. The
   * number of @p newcomer (just arrived) only counts as his wish.
   */
  void assignSquadNumbers(TeamID team_id, PlayerID newcomer = 0);

  // ---------------- World state ----------------
  /**
   * @brief Seeds the generation of a new world (call before loadFromDB).
   *
   * Without an explicit seed a new world uses FM_WORLD_SEED from the
   * environment or, failing that, the system clock. Loaded saves use their
   * stored seed.
   */
  void setWorldSeed(std::uint64_t seed);

  /** @brief Seed of every random stream of the world simulation. */
  std::uint64_t getWorldSeed() const;

  /** @brief Returns a new, never reused player id. */
  PlayerID allocatePlayerId();

  /** @brief The id the next allocatePlayerId() call will return. */
  PlayerID peekNextPlayerId() const;

  /** @brief Players removed (retired) since the last save. */
  const std::vector<PlayerID>& getRemovedPlayerIds() const;

  /** @brief Forgets removed players once their deletion is persisted. */
  void clearRemovedPlayerIds();

  // ---------------- Staff & training ----------------
  /** @brief Coaches, medics and scouts of every club plus the staff market. */
  StaffRoster& getStaff() { return staff; }
  const StaffRoster& getStaff() const { return staff; }

  /** @brief Training plans, familiarity and player workloads. */
  TrainingRegistry& getTraining() { return training; }
  const TrainingRegistry& getTraining() const { return training; }

  // ---------------- Transfer Market ----------------
  /**
   * @brief Saves a transfer listing to the database (UPSERT).
   */
  void saveTransferListing(const TransferListing& listing) const;

  /**
   * @brief Deletes a transfer listing from the database.
   */
  void deleteTransferListing(PlayerID player_id) const;

  /**
   * @brief Loads all transfer listings from the database into a map.
   */
  std::unordered_map<PlayerID, TransferListing> loadAllTransferListings() const;

 private:
  std::unordered_map<LeagueID, League> _leagues;
  std::vector<std::reference_wrapper<const League>> _leaguesVec;
  std::unordered_map<TeamID, Team> _teams;
  std::vector<std::reference_wrapper<const Team>> _teamsVec;
  std::unordered_map<PlayerID, Player> _players;
  std::vector<std::reference_wrapper<const Player>> _playersVec;
  std::unordered_map<TeamID, std::vector<std::reference_wrapper<const Player>>>
      _teamPlayers;
  std::unordered_map<TeamID, std::vector<std::reference_wrapper<const Player>>>
      _teamAcademies;
  StatsConfig stats_config;
  std::shared_ptr<DatabaseConnection> db_conn;
  std::uint64_t world_seed = 0;
  bool world_seed_set = false;
  PlayerID next_player_id = 1;
  std::vector<PlayerID> removed_player_ids;
  StaffRoster staff;
  TrainingRegistry training;

  void loadStatsConfig();
  void generateAndSaveInitialData(std::stop_token stop);
  void loadExistingData();
  void migrateSchema() const;
  void restoreWorldState();
  void restoreStaffAndTraining();
};
