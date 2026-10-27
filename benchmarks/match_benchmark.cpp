// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <benchmark/benchmark.h>

#include <array>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "benchmark_environment.h"
#include "model/match_engine.h"
#include "model/player.h"
#include "model/team.h"

namespace
{
constexpr int BENCHMARK_PLAYER_RATING = 70;
constexpr int LIVE_VIEW_FRAME_COUNT = 600;
constexpr float LIVE_VIEW_FRAME_SECONDS = 0.05f;
constexpr float FULL_MATCH_FRAME_SECONDS = 0.1f;
constexpr std::uint32_t BENCHMARK_SEED = 42;
constexpr TeamID HOME_TEAM_ID = 1;
constexpr TeamID AWAY_TEAM_ID = 2;
constexpr std::uint8_t BENCHMARK_LEAGUE_ID = 1;
constexpr std::uint32_t BENCHMARK_TEAM_BUDGET = 50'000'000;
constexpr std::uint32_t BENCHMARK_PLAYER_WAGE = 1'000;
constexpr std::uint8_t BENCHMARK_PLAYER_AGE = 24;
constexpr std::uint8_t BENCHMARK_CONTRACT_YEARS = 3;
constexpr std::uint8_t BENCHMARK_PLAYER_HEIGHT = 180;

StatsConfig createStatsConfig()
{
  StatsConfig config;
  config.possible_stats = {"Pace",      "Shooting",  "Passing",
                           "Dribbling", "Defending", "Physicality",
                           "Stamina",   "Vision",    "Goalkeeping"};
  config.role_focus["Goalkeeper"] = {{"Goalkeeping", "Vision", "Physicality"},
                                     {0.7, 0.2, 0.1}};
  config.role_focus["Defender"] = {
      {"Defending", "Physicality", "Pace", "Vision"}, {0.4, 0.3, 0.15, 0.15}};
  config.role_focus["Midfielder"] = {
      {"Passing", "Vision", "Stamina", "Dribbling"}, {0.3, 0.3, 0.2, 0.2}};
  config.role_focus["Striker"] = {
      {"Shooting", "Pace", "Dribbling", "Physicality"}, {0.4, 0.2, 0.2, 0.2}};
  return config;
}

Team createBenchmarkTeam(TeamID id, const std::string& name,
                         std::vector<std::unique_ptr<Player>>& players)
{
  Team team(id, BENCHMARK_LEAGUE_ID, std::move(name), BENCHMARK_TEAM_BUDGET, {},
            Strategy{}, Lineup{});
  using enum PlayerRole;
  static constexpr std::array<PlayerRole, 11> ROLES = {GK, LB, CB, CB, RB, CM,
                                                       CM, LW, RW, ST, ST};
  static constexpr std::array<Vector2F, 11> POSITIONS = {
      Vector2F{0.04f, 0.50f}, Vector2F{0.18f, 0.12f}, Vector2F{0.18f, 0.38f},
      Vector2F{0.18f, 0.62f}, Vector2F{0.18f, 0.88f}, Vector2F{0.43f, 0.35f},
      Vector2F{0.43f, 0.65f}, Vector2F{0.68f, 0.16f}, Vector2F{0.68f, 0.84f},
      Vector2F{0.78f, 0.38f}, Vector2F{0.78f, 0.62f}};

  for (std::size_t index = 0; index < ROLES.size(); ++index)
  {
    const auto rating = static_cast<float>(BENCHMARK_PLAYER_RATING);
    std::map<std::string, float> stats = {
        {"Pace", rating},      {"Shooting", rating},  {"Passing", rating},
        {"Dribbling", rating}, {"Defending", rating}, {"Physicality", rating},
        {"Stamina", rating},   {"Vision", rating},    {"Goalkeeping", rating}};
    const auto playerId = static_cast<PlayerID>(players.size() + 1);
    players.push_back(std::make_unique<Player>(
        playerId, id, "Benchmark", std::format("Player {}", index),
        ROLES[index], Language::EN, BENCHMARK_PLAYER_WAGE, 0,
        BENCHMARK_PLAYER_AGE, BENCHMARK_CONTRACT_YEARS, BENCHMARK_PLAYER_HEIGHT,
        Foot::Right, std::move(stats)));
    if (ROLES[index] == GK)
      team.getLineup().setGoalkeeper(players.back().get());
    else
      team.getLineup().addOutfieldPlayer(players.back().get(),
                                         POSITIONS[index]);
  }
  return team;
}

void simulateToFullTime(MatchEngine& engine)
{
  while (engine.getState() != MatchState::FULL_TIME)
  {
    engine.update(FULL_MATCH_FRAME_SECONDS);
  }
}

void BM_LiveMatchSimulation(benchmark::State& state)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createBenchmarkTeam(HOME_TEAM_ID, "Home", players);
  Team away = createBenchmarkTeam(AWAY_TEAM_ID, "Away", players);
  const StatsConfig statsConfig = createStatsConfig();
  for (auto _ : state)
  {
    (void)_;
    MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                       away.getStrategy(), statsConfig, BENCHMARK_SEED);
    for (int frame = 0; frame < LIVE_VIEW_FRAME_COUNT; ++frame)
      engine.update(LIVE_VIEW_FRAME_SECONDS);
    benchmark::DoNotOptimize(engine.getPlayers().data());
    int passesAttempted = engine.getStats().homePassesAttempted;
    benchmark::DoNotOptimize(passesAttempted);
  }
  state.SetItemsProcessed(state.iterations() * LIVE_VIEW_FRAME_COUNT);
}

void BM_FullMatchSimulation(benchmark::State& state)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createBenchmarkTeam(HOME_TEAM_ID, "Home", players);
  Team away = createBenchmarkTeam(AWAY_TEAM_ID, "Away", players);
  const StatsConfig statsConfig = createStatsConfig();
  for (auto _ : state)
  {
    (void)_;
    MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                       away.getStrategy(), statsConfig, BENCHMARK_SEED);
    simulateToFullTime(engine);
    int homeScore = engine.getHomeScore();
    int awayScore = engine.getAwayScore();
    benchmark::DoNotOptimize(homeScore);
    benchmark::DoNotOptimize(awayScore);
  }
  state.SetItemsProcessed(state.iterations());
}
}  // namespace

BENCHMARK(BM_LiveMatchSimulation)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_FullMatchSimulation)->Unit(benchmark::kMillisecond);

int main(int argc, char** argv)
{
  return BenchmarkEnvironment::run(argc, argv);
}
