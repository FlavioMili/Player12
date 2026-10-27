// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <string>

#include "benchmark_environment.h"
#include "database/database_connection.h"
#include "database/gamedata.h"
#include "database/repositories/game_state_repository.h"
#include "database/repositories/player_repository.h"
#include "global/runtime_paths.h"

namespace
{
std::filesystem::path benchmarkDatabasePath()
{
  const auto directory = RuntimePaths::root() / "benchmark";
  std::filesystem::create_directories(directory);
  return directory / "database.db";
}
constexpr std::uint8_t BENCHMARK_SEASON = 1;
constexpr TeamID BENCHMARK_MANAGED_TEAM = 0;
constexpr const char* BENCHMARK_DATE = "2025-07-01";

void markDatabaseInitialized(
    const std::shared_ptr<DatabaseConnection>& connection)
{
  GameStateRepository(connection)
      .updateGameState(BENCHMARK_SEASON, BENCHMARK_MANAGED_TEAM,
                       BENCHMARK_DATE);
}

PlayerID nextPlayerId(const GameData& gameData)
{
  PlayerID nextId = 1;
  for (const auto& [id, player] : gameData.getPlayers())
  {
    (void)player;
    nextId = std::max(nextId, static_cast<PlayerID>(id + 1));
  }
  return nextId;
}

Player makeBenchmarkPlayer(PlayerID id, TeamID teamId, int sequence)
{
  return Player(id, teamId, "Test", std::format("Player {}", sequence),
                PlayerRole::ST, Language::EN, 1'000, 0, 20, 2, 180, Foot::Right,
                {});
}
}  // namespace

// Set up the database environment for benchmarks
class DatabaseFixture : public benchmark::Fixture
{
 public:
  void SetUp(::benchmark::State& state) override
  {
    (void)state;
    // Benchmarks must never touch a player's real save database.
    const auto databasePath = benchmarkDatabasePath();
    std::filesystem::remove(databasePath);
    std::filesystem::remove(databasePath.string() + "-wal");
    std::filesystem::remove(databasePath.string() + "-shm");
    db_conn = std::make_shared<DatabaseConnection>(databasePath.string());
    db_conn->initialize();
  }

  void TearDown(::benchmark::State& state) override
  {
    (void)state;
    db_conn.reset();
    const auto databasePath = benchmarkDatabasePath();
    std::filesystem::remove(databasePath);
    std::filesystem::remove(databasePath.string() + "-wal");
    std::filesystem::remove(databasePath.string() + "-shm");
  }

  std::shared_ptr<DatabaseConnection> db_conn;
  GameData gamedata;
};

BENCHMARK_DEFINE_F(DatabaseFixture, BM_LoadFromDB)(benchmark::State& state)
{
  // Pre-load initial data
  gamedata.loadFromDB(db_conn);
  markDatabaseInitialized(db_conn);

  auto target_players = state.range(0);
  auto current_players = static_cast<int>(gamedata.getPlayers().size());
  PlayerID next_id = nextPlayerId(gamedata);

  // Inject extra players directly into the DB
  db_conn->beginTransaction();
  for (int i = current_players; i < target_players; ++i)
  {
    const Player player = makeBenchmarkPlayer(next_id++, 1, i);
    PlayerRepository(db_conn).insertPlayerWithId(player);
  }
  db_conn->commitTransaction();
  for (auto _ : state)
  {
    (void)_;
    gamedata.loadFromDB(db_conn);
  }
}

BENCHMARK_DEFINE_F(DatabaseFixture, BM_SaveToDB)(benchmark::State& state)
{
  // Pre-load data
  gamedata.loadFromDB(db_conn);
  markDatabaseInitialized(db_conn);

  auto target_players = state.range(0);
  auto current_players = static_cast<int>(gamedata.getPlayers().size());
  PlayerID next_id = nextPlayerId(gamedata);

  db_conn->beginTransaction();
  for (int i = current_players; i < target_players; ++i)
  {
    const Player player = makeBenchmarkPlayer(next_id++, 1, i);
    PlayerRepository(db_conn).insertPlayerWithId(player);
    gamedata.addPlayer(player.getId(), player);
  }
  db_conn->commitTransaction();
  for (auto _ : state)
  {
    (void)_;
    gamedata.saveToDB();
  }
}

// The generated database already has more than 7,000 players, so ranges must
// remain above that baseline to measure actual scaling.
BENCHMARK_REGISTER_F(DatabaseFixture, BM_LoadFromDB)
    ->RangeMultiplier(2)
    ->Range(8192, 16384)
    ->Unit(benchmark::kMillisecond);
BENCHMARK_REGISTER_F(DatabaseFixture, BM_SaveToDB)
    ->RangeMultiplier(2)
    ->Range(8192, 16384)
    ->Unit(benchmark::kMillisecond);

BENCHMARK_DEFINE_F(DatabaseFixture,
                   BM_GetPlayersForTeam)(benchmark::State& state)
{
  // Pre-load data
  gamedata.loadFromDB(db_conn);
  markDatabaseInitialized(db_conn);

  auto target_players = state.range(0);
  auto current_players = static_cast<int>(gamedata.getPlayers().size());
  PlayerID next_id = nextPlayerId(gamedata);

  constexpr TeamID TEAM_ID_TO_QUERY = 1;
  constexpr TeamID ALTERNATE_TEAM_ID = 2;
  // Use a transaction for fast inserts
  db_conn->beginTransaction();
  for (int i = current_players; i < target_players; ++i)
  {
    const TeamID team_id = i % 2 == 0 ? TEAM_ID_TO_QUERY : ALTERNATE_TEAM_ID;
    const Player player = makeBenchmarkPlayer(next_id++, team_id, i);
    PlayerRepository(db_conn).insertPlayerWithId(player);
    gamedata.addPlayer(player.getId(), player);
  }
  db_conn->commitTransaction();

  for (auto _ : state)
  {
    const auto& players = gamedata.getPlayersForTeam(TEAM_ID_TO_QUERY);
    benchmark::DoNotOptimize(players.data());
    benchmark::DoNotOptimize(players.size());
  }
}

BENCHMARK_REGISTER_F(DatabaseFixture, BM_GetPlayersForTeam)
    ->RangeMultiplier(2)
    ->Range(8192, 16384)
    ->Unit(benchmark::kMicrosecond);

int main(int argc, char** argv)
{
  return BenchmarkEnvironment::run(argc, argv);
}
