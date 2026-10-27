// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// World database IO: parse + validate, write and combine for worlds of
// 1,000 to 16,000 players (NFR-001: 15,000 players validated in < 2 s).

#include <benchmark/benchmark.h>

#include <string>

#include "database/world/world_combine.h"
#include "database/world/world_io.h"
#include "database/world/world_validate.h"

using json = nlohmann::json;
using World::Kind;

namespace
{
World::Document world(std::int64_t players)
{
  World::Document document = World::templateDocument();
  document.setSection(Kind::Club, json::array());
  document.setSection(Kind::Player, json::array());
  const std::int64_t clubs = std::max<std::int64_t>(4, players / 30);
  json& club_section = document.section(Kind::Club);
  for (std::int64_t id = 1; id <= clubs; ++id)
  {
    club_section.push_back(
        {{"id", id},
         {"league_id", 1},
         {"name", "Club " + std::to_string(id)},
         {"colours", {{"primary", "#204060"}}},
         {"stadium", {{"name", "Park"}, {"capacity", 9000}}}});
  }
  json& player_section = document.section(Kind::Player);
  for (std::int64_t id = 1; id <= players; ++id)
  {
    player_section.push_back({{"id", id},
                              {"team_id", 1 + (id % clubs)},
                              {"first_name", "Name" + std::to_string(id % 997)},
                              {"last_name", "Surname" + std::to_string(id)},
                              {"age", 18 + (id % 18)},
                              {"nationality", "Italian"},
                              {"role", "CM"},
                              {"preferred_foot", "Right"},
                              {"height", 180},
                              {"wage", 2500},
                              {"contract_years", 2},
                              {"stats",
                               {{"Pace", 60},
                                {"Shooting", 55},
                                {"Passing", 62},
                                {"Dribbling", 58},
                                {"Defending", 50},
                                {"Physicality", 61},
                                {"Stamina", 64},
                                {"Vision", 57},
                                {"Goalkeeping", 10}}}});
  }
  document.invalidate();
  return document;
}

void BM_WorldReadValidate(benchmark::State& state)
{
  const std::string text = World::write(world(state.range(0)));
  for (auto _ : state)
  {
    World::LoadResult loaded = World::readText(text);
    benchmark::DoNotOptimize(loaded);
  }
  state.counters["bytes"] = static_cast<double>(text.size());
}

void BM_WorldWrite(benchmark::State& state)
{
  const World::Document document = world(state.range(0));
  for (auto _ : state)
  {
    std::string text = World::write(document);
    benchmark::DoNotOptimize(text);
  }
}

void BM_WorldCombine(benchmark::State& state)
{
  const World::Document base = world(state.range(0));
  const World::Document incoming = world(state.range(0));
  for (auto _ : state)
  {
    World::CombineResult result = World::combine(base, incoming);
    benchmark::DoNotOptimize(result);
  }
}
}  // namespace

BENCHMARK(BM_WorldReadValidate)
    ->Arg(1'000)
    ->Arg(16'000)
    ->Unit(benchmark::kMillisecond);
BENCHMARK(BM_WorldWrite)
    ->Arg(1'000)
    ->Arg(16'000)
    ->Unit(benchmark::kMillisecond);
BENCHMARK(BM_WorldCombine)
    ->Arg(1'000)
    ->Arg(16'000)
    ->Unit(benchmark::kMillisecond);
