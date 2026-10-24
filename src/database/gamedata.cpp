// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "database/gamedata.h"

#include <algorithm>
#include <bitset>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

#include "database/SQLLoader.h"
#include "database/database_connection.h"
#include "database/datagenerator.h"
#include "database/migrations/migrations.h"
#include "database/repositories/finance_repository.h"
#include "database/repositories/game_state_repository.h"
#include "database/repositories/league_repository.h"
#include "database/repositories/player_repository.h"
#include "database/repositories/staff_repository.h"
#include "database/repositories/team_repository.h"
#include "database/repositories/training_repository.h"
#include "database/repositories/world_state_repository.h"
#include "global/logger.h"
#include "global/paths.h"
#include "global/queries.h"
#include "model/club_economy.h"
#include "model/squad_numbers.h"
#include "model/transfer_listing.h"
#include "model/world_generation.h"
#include "model/world_rng.h"

namespace
{
std::uint64_t defaultWorldSeed()
{
  if (const char* configured = std::getenv("FM_WORLD_SEED"))
  {
    try
    {
      return std::stoull(configured);
    }
    catch (const std::exception&)
    {
      Logger::warn("Ignoring invalid FM_WORLD_SEED");
    }
  }
  const auto now = static_cast<std::uint64_t>(
      std::chrono::system_clock::now().time_since_epoch().count());
  return mixHash(now, 0x5EEDULL);
}

bool restoreLineup(Team& team, const StoredLineup& stored,
                   const std::unordered_map<PlayerID, Player>& players)
{
  if (!stored.persisted) return false;
  Lineup& lineup = team.getLineup();
  lineup.clear();

  const auto resolvePlayer = [&](PlayerID playerId) -> const Player*
  {
    const auto found = players.find(playerId);
    if (found == players.end() || found->second.getTeamId() != team.getId())
      return nullptr;
    return &found->second;
  };

  if (stored.goalkeeperId.has_value())
  {
    const Player* goalkeeper = resolvePlayer(*stored.goalkeeperId);
    if (!goalkeeper) return false;
    lineup.setGoalkeeper(goalkeeper);
  }
  for (const StoredPositionedPlayer& positioned : stored.outfield)
  {
    const Player* player = resolvePlayer(positioned.playerId);
    if (!player) return false;
    lineup.addOutfieldPlayer(player, positioned.position);
  }
  std::vector<const Player*> reserves;
  reserves.reserve(stored.reserves.size());
  for (PlayerID playerId : stored.reserves)
  {
    const Player* player = resolvePlayer(playerId);
    if (!player) return false;
    reserves.push_back(player);
  }
  lineup.setReserves(reserves);
  // Designations of players who left the club fall back to automatic.
  SetPieceDesignations designations = stored.designations;
  for (PlayerID& designated : designations)
    if (designated != PlayerID{} && !resolvePlayer(designated))
      designated = PlayerID{};
  lineup.setDesignations(designations);
  std::vector<Lineup::StandIn> standIns;
  for (const Lineup::StandIn& entry : stored.standIns)
    if (resolvePlayer(entry.regular) && resolvePlayer(entry.stand_in))
      standIns.push_back(entry);
  lineup.setStandIns(std::move(standIns));
  return true;
}
}  // namespace

GameData::GameData() = default;

// ---------------- DB ----------------
bool GameData::loadFromDB(std::shared_ptr<DatabaseConnection> database_ptr)
{
  _leagues.clear();
  _teams.clear();
  _players.clear();
  _leaguesVec.clear();
  _teamsVec.clear();
  _playersVec.clear();
  _teamPlayers.clear();
  _teamAcademies.clear();
  removed_player_ids.clear();

  loadStatsConfig();
  db_conn = database_ptr;

  GameStateRepository gameStateRepo(db_conn);

  bool is_first_run = gameStateRepo.isFirstRun();

  Logger::debug("GameData::loadFromDB called. is_first_run: " +
                std::to_string(is_first_run));

  if (is_first_run)
  {
    generateAndSaveInitialData();
  }
  else
  {
    loadExistingData();
  }

  // Build vectors from the maps
  _leaguesVec.clear();
  _leaguesVec.reserve(_leagues.size());
  for (auto& [id, league] : _leagues) _leaguesVec.push_back(league);

  _teamsVec.clear();
  _teamsVec.reserve(_teams.size());
  for (auto& [id, team] : _teams)
  {
    if (id != FREE_AGENTS_TEAM_ID) _teamsVec.push_back(team);
  }

  _playersVec.clear();
  _playersVec.reserve(_players.size());
  for (auto& [id, player] : _players) _playersVec.push_back(player);

  restoreStaffAndTraining();
  return true;
}

void GameData::restoreStaffAndTraining()
{
  staff.restore(StaffRepository(db_conn).loadAll());
  // New worlds and saves from before staff existed get a deterministic
  // staff generated from the world seed.
  if (staff.empty()) StaffModel::generateWorld(*this);
  // Ids of departed staff are not handed out again (scouting data is keyed
  // by staff id).
  staff.restoreNextId(WorldStateRepository(db_conn).loadNextStaffId());
  const TrainingRepository training_repo(db_conn);
  training.restore(training_repo.loadPlans(), training_repo.loadPlayers());
  for (const auto& [id, team] : _teams)
  {
    if (id != FREE_AGENTS_TEAM_ID) training.plan(id);
  }
}

void GameData::generateAndSaveInitialData()
{
  TeamRepository teamRepo(db_conn);
  LeagueRepository leagueRepo(db_conn);
  PlayerRepository playerRepo(db_conn);

  migrateSchema();
  sqlite3_exec(
      db_conn->getRaw(),
      "DELETE FROM Players; DELETE FROM Teams; DELETE FROM Leagues; DELETE "
      "FROM GameState; DELETE FROM LeaguePoints; DELETE FROM Fixtures; "
      "DELETE FROM FinanceLedger; DELETE FROM InboxMessages; DELETE FROM "
      "BoardState; DELETE FROM WorldState; DELETE FROM Staff; DELETE FROM "
      "TeamTraining; DELETE FROM PlayerTraining;",
      nullptr, nullptr, nullptr);
  sqlite3_exec(db_conn->getRaw(),
               "INSERT OR IGNORE INTO Teams (id, league_id, name, balance) "
               "VALUES (0, -1, 'Free agents', -1);",
               nullptr, nullptr, nullptr);
  Logger::debug("Database initialized. Generating data.");

  if (!world_seed_set) world_seed = defaultWorldSeed();
  auto leagues_data = DataGenerator::generateLeagues();
  auto all_teams = DataGenerator::generateTeams();

  db_conn->beginTransaction();
  try
  {
    _teams.try_emplace(FREE_AGENTS_TEAM_ID, FREE_AGENTS_TEAM_ID,
                       std::numeric_limits<LeagueID>::max(), "Free agents", -1);
    for (const auto& team : all_teams)
    {
      _teams.try_emplace(team.getId(), team);
    }
    WorldGeneration::generateClubProfiles(_teams, world_seed, true);

    // Populate _teamsVec so we can use insertTeamsWithId
    _teamsVec.clear();
    _teamsVec.reserve(_teams.size());
    for (auto& [id, team] : _teams) _teamsVec.push_back(team);

    teamRepo.insertTeamsWithId(_teamsVec);

    std::map<uint8_t, std::vector<TeamID>> league_teams_map;
    for (const auto& team : all_teams)
    {
      league_teams_map[team.getLeagueId()].push_back(team.getId());
    }

    for (const auto& league_data : leagues_data)
    {
      leagueRepo.insertLeagueWithId(league_data);
      _leagues.try_emplace(
          league_data.getId(), league_data.getId(), league_data.getName(),
          league_teams_map[league_data.getId()],
          league_data.getParentLeagueID(), league_data.getTieBreakRule());
    }

    // DataGenerator::generatePlayers depends on GameData::getTeamsVector()
    // being populated!
    _teamsVec.clear();
    _teamsVec.reserve(_teams.size());
    for (auto& [id, team] : _teams) _teamsVec.push_back(team);

    auto players = DataGenerator::generatePlayers(*this);
    for (const auto& player : players)
    {
      auto it = _players.try_emplace(player.getId(), player).first;
      _teamPlayers[player.getTeamId()].push_back(it->second);

      // Add to Team's player list
      auto team_it = _teams.find(player.getTeamId());
      if (team_it != _teams.end())
      {
        team_it->second.addPlayerID(player.getId());
      }
    }

    _playersVec.clear();
    _playersVec.reserve(_players.size());
    for (auto& [id, player] : _players) _playersVec.push_back(player);
    for (const auto& [id, team] : _teams) assignSquadNumbers(id);

    playerRepo.insertPlayers(_playersVec);

    for (const auto& [id, player] : _players)
      next_player_id = std::max(next_player_id, id + 1);
    const auto economies = buildLeagueEconomies(*this);
    for (auto& [id, team] : _teams)
    {
      const auto economy = economies.find(team.getLeagueId());
      if (id == FREE_AGENTS_TEAM_ID || economy == economies.end()) continue;
      WorldGeneration::applyOpeningBudgets(
          team, economy->second,
          team.getFinances().getCurrentWageSpending(*this, team));
    }

    for (auto& [id, team] : _teams)
    {
      team.generateStartingXI(*this, stats_config);
    }
    teamRepo.updateTeamsState(_teamsVec);
    WorldStateRepository(db_conn).saveWorldState(world_seed, next_player_id,
                                                 staff.peekNextId());

    db_conn->commitTransaction();
  }
  catch (...)
  {
    db_conn->rollbackTransaction();
    throw;
  }
  for (auto& [id, team] : _teams) team.getFinances().markPersisted();
}

void GameData::migrateSchema() const
{
  // Rejects saves from newer versions before any write, creates tables
  // introduced after the save was written and applies pending migrations.
  Migrations::migrate(*db_conn);
}

void GameData::restoreWorldState()
{
  PlayerID max_player_id = 0;
  for (const auto& [id, player] : _players)
    max_player_id = std::max(max_player_id, id);
  if (!WorldStateRepository(db_conn).loadWorldState(world_seed, next_player_id))
  {
    // Saves from before the world simulation get a stable derived seed.
    world_seed = mixHash(0x1E6AC75EEDULL, max_player_id);
  }
  next_player_id = std::max(next_player_id, max_player_id + 1);

  const auto ledgers = FinanceRepository(db_conn).loadAll();
  for (auto& [id, team] : _teams)
  {
    const auto ledger = ledgers.find(id);
    if (ledger == ledgers.end()) continue;  // Legacy: opening entry pending.
    Finances& finances = team.getFinances();
    const std::int64_t stored_balance = finances.getBalance();
    finances.restoreLedger(ledger->second);
    if (finances.getBalance() != stored_balance &&
        !finances.getLedger().empty())
    {
      finances.record(finances.getLedger().back().date,
                      FinanceCategory::Adjustment,
                      stored_balance - finances.getBalance());
    }
  }

  // Saves from before the world simulation: derive club profiles, budgets
  // and hidden player attributes deterministically.
  if (const bool legacy_clubs =
          std::ranges::any_of(_teams,
                              [](const auto& entry)
                              {
                                return entry.first != FREE_AGENTS_TEAM_ID &&
                                       entry.second.getReputation() == 0;
                              });
      legacy_clubs)
  {
    WorldGeneration::generateClubProfiles(_teams, world_seed, false);
    const auto economies = buildLeagueEconomies(*this);
    for (auto& [id, team] : _teams)
    {
      const auto economy = economies.find(team.getLeagueId());
      if (id == FREE_AGENTS_TEAM_ID || economy == economies.end()) continue;
      WorldGeneration::applyOpeningBudgets(
          team, economy->second,
          team.getFinances().getCurrentWageSpending(*this, team));
    }
  }
  for (auto& [id, player] : _players)
  {
    if (player.getPotential() > 0.0f) continue;
    WorldRng rng = WorldRng::stream(world_seed, RngDomain::Migration, id);
    WorldGeneration::initializeHiddenAttributes(player, rng, stats_config);
  }
}

void GameData::loadExistingData()
{
  migrateSchema();
  TeamRepository teamRepo(db_conn);
  LeagueRepository leagueRepo(db_conn);
  PlayerRepository playerRepo(db_conn);

  auto leagues_from_db = leagueRepo.loadAllLeagues();
  auto all_teams = teamRepo.loadAllTeams();
  const auto storedLineups = teamRepo.loadAllLineups();

  std::map<uint8_t, std::vector<TeamID>> league_teams_map;
  for (const auto& team : all_teams)
  {
    league_teams_map[team.getLeagueId()].push_back(team.getId());
  }

  _teams.reserve(all_teams.size());
  _teamPlayers.reserve(all_teams.size());
  for (auto& team : all_teams)
  {
    const TeamID teamId = team.getId();
    _teams.try_emplace(teamId, std::move(team));
  }

  for (auto& league_from_db : leagues_from_db)
  {
    for (TeamID tid : league_teams_map[league_from_db.getId()])
    {
      league_from_db.addTeamID(tid);
    }
    const LeagueID leagueId = league_from_db.getId();
    _leagues.try_emplace(leagueId, std::move(league_from_db));
  }

  auto players = playerRepo.loadAllPlayers();
  _players.reserve(players.size());
  for (auto& player : players)
  {
    const PlayerID playerId = player.getId();
    const TeamID teamId = player.getTeamId();
    auto it = _players.try_emplace(playerId, std::move(player)).first;
    // The academy flag is stored with the player's status: academy players
    // stay out of the senior squad.
    const bool academy = it->second.isAcademyPlayer();
    (academy ? _teamAcademies : _teamPlayers)[teamId].push_back(it->second);

    // Add to Team's player list
    auto team_it = _teams.find(teamId);
    if (team_it != _teams.end())
    {
      if (academy)
        team_it->second.addAcademyID(playerId);
      else
        team_it->second.addPlayerID(playerId);
    }
  }

  restoreWorldState();
  // Saves from before squad numbers (and any club whose numbers clash).
  for (const auto& [id, team] : _teams) assignSquadNumbers(id);

  _teamsVec.clear();
  _teamsVec.reserve(_teams.size());
  for (auto& [id, team] : _teams) _teamsVec.push_back(team);

  _playersVec.clear();
  _playersVec.reserve(_players.size());
  for (auto& [id, player] : _players) _playersVec.push_back(player);

  for (auto& [id, team] : _teams)
  {
    const auto stored = storedLineups.find(id);
    if (stored == storedLineups.end() ||
        !restoreLineup(team, stored->second, _players))
    {
      team.generateStartingXI(*this, stats_config);
    }
  }

  Logger::debug("Loaded all data from database.");
}

bool GameData::saveToDB() const
{
  if (!db_conn) return false;

  db_conn->beginTransaction();
  try
  {
    PlayerRepository playerRepo(db_conn);
    TeamRepository teamRepo(db_conn);
    playerRepo.updatePlayers(_playersVec);
    teamRepo.updateTeamsState(_teamsVec);
    db_conn->commitTransaction();
    return true;
  }
  catch (const std::exception& exception)
  {
    db_conn->rollbackTransaction();
    Logger::error("Failed to save game data: " + std::string(exception.what()));
    return false;
  }
}

// ---------------- League ----------------
void GameData::addLeague(LeagueID id, const League& league)
{
  _leagues.try_emplace(id, league);
  if (!_leaguesVec.empty()) _leaguesVec.push_back(_leagues.at(id));
}

std::optional<std::reference_wrapper<const League>> GameData::getLeague(
    LeagueID id) const
{
  if (auto it = _leagues.find(id); it != _leagues.end()) return it->second;
  return std::nullopt;
}

const std::unordered_map<LeagueID, League>& GameData::getLeagues() const
{
  return _leagues;
}

std::unordered_map<LeagueID, League>& GameData::getLeagues()
{
  return _leagues;
}

const std::vector<std::reference_wrapper<const League>>&
GameData::getLeaguesVector() const
{
  return _leaguesVec;
}

// ---------------- Team ----------------
void GameData::addTeam(TeamID id, const Team& team)
{
  _teams.try_emplace(id, team);
  if (!_teamsVec.empty()) _teamsVec.push_back(_teams.at(id));
}

std::optional<std::reference_wrapper<Team>> GameData::getTeam(TeamID id)
{
  if (auto it = _teams.find(id); it != _teams.end()) return it->second;
  return std::nullopt;
}

std::optional<std::reference_wrapper<const Team>> GameData::getTeam(
    TeamID id) const
{
  if (auto it = _teams.find(id); it != _teams.end()) return it->second;
  return std::nullopt;
}

const std::unordered_map<TeamID, Team>& GameData::getTeams() const
{
  return _teams;
}

std::unordered_map<TeamID, Team>& GameData::getTeams() { return _teams; }

const std::vector<std::reference_wrapper<const Team>>&
GameData::getTeamsVector() const
{
  return _teamsVec;
}

// ---------------- Player ----------------
void GameData::addPlayer(PlayerID id, const Player& player)
{
  auto it = _players.try_emplace(id, player).first;
  if (!_playersVec.empty())
  {
    _playersVec.push_back(it->second);
  }
  (player.isAcademyPlayer() ? _teamAcademies : _teamPlayers)[player.getTeamId()]
      .push_back(it->second);
  if (!player.isAcademyPlayer() && player.getSquadNumber() == 0)
    assignSquadNumbers(player.getTeamId());
}

std::optional<std::reference_wrapper<const Player>> GameData::getPlayer(
    PlayerID id) const
{
  if (auto it = _players.find(id); it != _players.end()) return it->second;
  return std::nullopt;
}

const std::unordered_map<PlayerID, Player>& GameData::getPlayers() const
{
  return _players;
}

std::unordered_map<PlayerID, Player>& GameData::getPlayers()
{
  return _players;
}
const std::vector<std::reference_wrapper<const Player>>&
GameData::getPlayersVector() const
{
  return _playersVec;
}

void GameData::ageAllPlayers()
{
  for (auto& [id, player] : _players)
  {
    player.agePlayer();
  }
}

std::vector<PlayerID> GameData::advanceContractsAndReleasePlayers(
    TeamID keep_selection)
{
  std::vector<std::pair<PlayerID, TeamID>> expiringPlayers;
  expiringPlayers.reserve(_players.size());
  for (auto& [id, player] : _players)
  {
    if (player.getTeamId() != FREE_AGENTS_TEAM_ID &&
        player.advanceContractYear())
    {
      expiringPlayers.emplace_back(id, player.getTeamId());
    }
  }

  std::vector<PlayerID> releasedPlayers;
  releasedPlayers.reserve(expiringPlayers.size());
  std::vector<TeamID> affectedTeams;
  affectedTeams.reserve(expiringPlayers.size());
  for (const auto& [playerId, oldTeamId] : expiringPlayers)
  {
    if (auto team = getTeam(oldTeamId)) team->get().removePlayerID(playerId);
    if (auto freeAgents = getTeam(FREE_AGENTS_TEAM_ID))
      freeAgents->get().addPlayerID(playerId);
    transferPlayer(playerId, FREE_AGENTS_TEAM_ID);
    _players.at(playerId).setTransferStatus(TransferStatus::NotListed);
    releasedPlayers.push_back(playerId);
    if (!std::ranges::contains(affectedTeams, oldTeamId))
      affectedTeams.push_back(oldTeamId);
  }

  for (TeamID teamId : affectedTeams)
  {
    if (teamId == keep_selection && teamId != FREE_AGENTS_TEAM_ID) continue;
    if (auto team = getTeam(teamId))
      team->get().generateStartingXI(*this, stats_config);
  }
  return releasedPlayers;
}

const std::vector<std::reference_wrapper<const Player>>&
GameData::getPlayersForTeam(TeamID team_id) const
{
  static const std::vector<std::reference_wrapper<const Player>> empty_vec;
  if (const auto it = _teamPlayers.find(team_id); it != _teamPlayers.end())
  {
    return it->second;
  }
  return empty_vec;
}

const std::vector<std::reference_wrapper<const Player>>&
GameData::getAcademyForTeam(TeamID team_id) const
{
  static const std::vector<std::reference_wrapper<const Player>> empty_vec;
  if (const auto it = _teamAcademies.find(team_id); it != _teamAcademies.end())
    return it->second;
  return empty_vec;
}

void GameData::setAcademyMember(PlayerID id, bool academy)
{
  const auto it = _players.find(id);
  if (it == _players.end()) return;
  Player& player = it->second;
  player.setAcademyPlayer(academy);
  const TeamID team_id = player.getTeamId();
  if (auto& from = (academy ? _teamPlayers : _teamAcademies)[team_id];
      std::erase_if(
          from, [id](const auto& ref) { return ref.get().getId() == id; }) > 0)
    (academy ? _teamAcademies : _teamPlayers)[team_id].push_back(player);
  if (const auto team = _teams.find(team_id); team != _teams.end())
    team->second.setAcademyMember(id, academy);
  // Academy players wear no senior number; a promoted one gets his own.
  if (academy)
    player.setSquadNumber(0);
  else if (player.getSquadNumber() == 0)
    assignSquadNumbers(team_id);
}

bool GameData::removePlayer(PlayerID id)
{
  auto player_it = _players.find(id);
  if (player_it == _players.end())
  {
    return false;
  }

  // Line-ups hold raw pointers; any club (including the free agents, whose
  // line-up is not rebuilt when one of them signs elsewhere) may still
  // reference the player.
  for (auto& [team_key, team] : _teams) team.getLineup().removePlayer(id);

  const TeamID team_id = player_it->second.getTeamId();
  const auto same = [id](const auto& ref) { return ref.get().getId() == id; };
  std::erase_if(_teamPlayers[team_id], same);
  std::erase_if(_teamAcademies[team_id], same);
  std::erase_if(_playersVec,
                [id](const auto& ref) { return ref.get().getId() == id; });
  _players.erase(player_it);
  removed_player_ids.push_back(id);
  return true;
}

void GameData::transferPlayer(PlayerID id, TeamID new_team_id)
{
  auto it = _players.find(id);
  if (it == _players.end()) return;

  TeamID old_team_id = it->second.getTeamId();
  if (old_team_id == new_team_id) return;

  it->second.setTeamId(new_team_id);
  it->second.setAcademyPlayer(false);

  const auto same = [id](const auto& ref) { return ref.get().getId() == id; };
  std::erase_if(_teamPlayers[old_team_id], same);
  std::erase_if(_teamAcademies[old_team_id], same);

  _teamPlayers[new_team_id].push_back(it->second);
  if (new_team_id == FREE_AGENTS_TEAM_ID) return;
  // The number he wore stays his wish; a teammate may already wear it.
  const auto& squad = _teamPlayers[new_team_id];
  const std::uint8_t number = it->second.getSquadNumber();
  if (number == 0 || std::ranges::any_of(squad,
                                         [&](const auto& ref)
                                         {
                                           const Player& other = ref.get();
                                           return other.getId() != id &&
                                                  other.getSquadNumber() ==
                                                      number;
                                         }))
    assignSquadNumbers(new_team_id, id);
}

void GameData::assignSquadNumbers(TeamID team_id, PlayerID newcomer)
{
  if (team_id == FREE_AGENTS_TEAM_ID) return;
  const auto found = _teamPlayers.find(team_id);
  if (found == _teamPlayers.end()) return;
  const auto& squad = found->second;
  std::bitset<SquadNumbers::MAX_NUMBER + 1> worn;
  bool complete = newcomer == 0;
  for (const auto& ref : squad)
  {
    if (!complete) break;
    const std::uint8_t number = ref.get().getSquadNumber();
    complete = SquadNumbers::isValid(number) && !worn.test(number);
    if (complete) worn.set(number);
  }
  if (complete) return;

  std::vector<SquadNumbers::Entry> entries;
  entries.reserve(squad.size());
  for (const auto& ref : squad)
  {
    const Player& player = ref.get();
    SquadNumbers::Entry entry;
    entry.id = player.getId();
    entry.role = player.getRole();
    entry.overall = player.getOverall(stats_config);
    entry.age = player.getAge();
    entry.number = player.getSquadNumber();
    if (entry.id == newcomer)
    {
      // He arrives with the number of his old club as a wish only.
      entry.preferred = entry.number;
      entry.number = 0;
    }
    entries.push_back(entry);
  }
  SquadNumbers::assign(entries);
  for (const SquadNumbers::Entry& entry : entries)
    _players.at(entry.id).setSquadNumber(entry.number);
}

SquadNumbers::Change GameData::setSquadNumber(PlayerID id, int number)
{
  using enum SquadNumbers::Change;
  const auto it = _players.find(id);
  if (it == _players.end() || !SquadNumbers::isValid(number)) return Invalid;
  Player& player = it->second;
  const TeamID team_id = player.getTeamId();
  if (team_id == FREE_AGENTS_TEAM_ID || player.isAcademyPlayer())
    return Invalid;
  const auto wanted = static_cast<std::uint8_t>(number);
  const std::uint8_t previous = player.getSquadNumber();
  if (previous == wanted) return Changed;
  SquadNumbers::Change change = Changed;
  for (const auto& ref : _teamPlayers[team_id])
  {
    const Player& other = ref.get();
    if (other.getId() == id || other.getSquadNumber() != wanted) continue;
    _players.at(other.getId()).setSquadNumber(previous);
    change = Swapped;
    break;
  }
  player.setSquadNumber(wanted);
  // A swap with a player who had no number leaves the teammate without one.
  if (change == Swapped && previous == 0) assignSquadNumbers(team_id);
  return change;
}

// ---------------- World state ----------------
void GameData::setWorldSeed(std::uint64_t seed)
{
  world_seed = seed;
  world_seed_set = true;
}

std::uint64_t GameData::getWorldSeed() const { return world_seed; }

PlayerID GameData::allocatePlayerId() { return next_player_id++; }

PlayerID GameData::peekNextPlayerId() const { return next_player_id; }

const std::vector<PlayerID>& GameData::getRemovedPlayerIds() const
{
  return removed_player_ids;
}

void GameData::clearRemovedPlayerIds() { removed_player_ids.clear(); }

// ---------------- Transfer Market ----------------
void GameData::saveTransferListing(const TransferListing& listing) const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      SQLLoader::getQuery(Query::UPSERT_TRANSFER_LISTING));

  sqlite3_bind_int(stmt, 1, static_cast<int>(listing.player_id));
  sqlite3_bind_int(stmt, 2, static_cast<int>(listing.asking_price));
  std::string date_str = listing.listing_date.toString();
  sqlite3_bind_text(stmt, 3, date_str.c_str(), -1, SQLITE_TRANSIENT);
  if (listing.highest_bidder_id.has_value())
  {
    sqlite3_bind_int(stmt, 4, static_cast<int>(*listing.highest_bidder_id));
  }
  else
  {
    sqlite3_bind_null(stmt, 4);
  }
  sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(listing.highest_bid));

  db_conn->executeStep(stmt);
  sqlite3_finalize(stmt);
}

void GameData::deleteTransferListing(PlayerID player_id) const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      SQLLoader::getQuery(Query::DELETE_TRANSFER_LISTING));

  sqlite3_bind_int(stmt, 1, static_cast<int>(player_id));

  db_conn->executeStep(stmt);
  sqlite3_finalize(stmt);
}

std::unordered_map<PlayerID, TransferListing>
GameData::loadAllTransferListings() const
{
  std::unordered_map<PlayerID, TransferListing> listings;

  sqlite3_stmt* stmt = db_conn->prepareStatement(
      SQLLoader::getQuery(Query::LOAD_ALL_TRANSFER_LISTINGS));

  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    auto pid = static_cast<PlayerID>(sqlite3_column_int(stmt, 0));
    auto price = static_cast<uint32_t>(sqlite3_column_int(stmt, 1));
    auto* date_str =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

    TransferListing listing;
    listing.player_id = pid;
    listing.asking_price = price;
    listing.listing_date =
        GameDateValue::fromString(date_str ? date_str : "2025-07-01");
    if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
    {
      listing.highest_bidder_id =
          static_cast<TeamID>(sqlite3_column_int(stmt, 3));
    }
    listing.highest_bid = static_cast<uint32_t>(sqlite3_column_int64(stmt, 4));

    listings[pid] = listing;
  }

  sqlite3_finalize(stmt);
  return listings;
}

void from_json(const nlohmann::json& j, RoleFocus& rf)
{
  j.at("stats").get_to(rf.stats);
  j.at("weights").get_to(rf.weights);
}

void from_json(const nlohmann::json& j, StatsConfig& sc)
{
  j.at("role_focus").get_to(sc.role_focus);
  j.at("possible_stats").get_to(sc.possible_stats);
}

void GameData::loadStatsConfig()
{
  std::ifstream f(AssetPaths::statsConfig());
  if (!f.is_open())
  {
    throw std::runtime_error("FATAL: Could not open stats config file");
  }
  nlohmann::json stats_config_json = nlohmann::json::parse(f);
  stats_config = stats_config_json.get<StatsConfig>();
}

const StatsConfig& GameData::getStatsConfig() const { return stats_config; }
