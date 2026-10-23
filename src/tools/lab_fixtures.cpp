// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/lab_fixtures.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

#include "global/paths.h"
#include "global/types.h"
#include "model/player.h"

namespace Lab
{
namespace
{
constexpr std::uint32_t RESERVES = 7;
constexpr std::uint32_t STARTERS = 11;
constexpr std::uint32_t PLAYER_WAGE = 100'000;
constexpr std::uint8_t PLAYER_AGE = 26;
constexpr std::uint8_t CONTRACT_YEARS = 3;
constexpr std::uint8_t TALL_HEIGHT_CM = 188;
constexpr std::uint8_t HEIGHT_CM = 178;

StatsConfig builtInStatsConfig()
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

/** Role-shaped attribute offsets so lab squads resemble real ones. */
std::map<std::string, float> roleStats(PlayerRole role, float base,
                                       std::uint32_t salt)
{
  // Small deterministic per-player spread keeps squads from being clones.
  const float jitter = static_cast<float>((salt * 2654435761U) % 11U) - 5.0f;
  const auto value = [&](float offset)
  { return std::clamp(base + offset + jitter, 1.0f, 99.0f); };
  std::map<std::string, float> stats = {
      {"Pace", value(0.0f)},      {"Shooting", value(-8.0f)},
      {"Passing", value(0.0f)},   {"Dribbling", value(-4.0f)},
      {"Defending", value(0.0f)}, {"Physicality", value(0.0f)},
      {"Stamina", value(0.0f)},   {"Vision", value(0.0f)},
      {"Goalkeeping", 15.0f}};
  using enum PlayerRole;
  switch (role)
  {
    case GK:
      stats["Goalkeeping"] = value(4.0f);
      stats["Shooting"] = value(-40.0f);
      stats["Dribbling"] = value(-30.0f);
      stats["Pace"] = value(-20.0f);
      break;
    case CB:
      stats["Defending"] = value(8.0f);
      stats["Physicality"] = value(8.0f);
      stats["Shooting"] = value(-25.0f);
      stats["Dribbling"] = value(-15.0f);
      break;
    case LB:
    case RB:
      stats["Defending"] = value(4.0f);
      stats["Pace"] = value(4.0f);
      stats["Shooting"] = value(-20.0f);
      break;
    case LM:
    case RM:
    case LW:
    case RW:
      stats["Pace"] = value(6.0f);
      stats["Dribbling"] = value(4.0f);
      stats["Defending"] = value(-15.0f);
      stats["Shooting"] = value(-4.0f);
      break;
    case CM:
    case CDM:
    case CAM:
      stats["Passing"] = value(6.0f);
      stats["Vision"] = value(6.0f);
      stats["Stamina"] = value(6.0f);
      stats["Defending"] = value(-6.0f);
      break;
    case ST:
      stats["Shooting"] = value(8.0f);
      stats["Defending"] = value(-30.0f);
      stats["Dribbling"] = value(2.0f);
      break;
    case UNKNOWN:
      break;
  }
  return stats;
}
}  // namespace

StatsConfig loadStatsConfig()
{
  std::ifstream file(AssetPaths::statsConfig());
  if (!file.is_open()) return builtInStatsConfig();
  try
  {
    // Same fields as GameData::loadStatsConfig().
    const nlohmann::json json = nlohmann::json::parse(file);
    StatsConfig config;
    json.at("possible_stats").get_to(config.possible_stats);
    for (const auto& [role, focus] : json.at("role_focus").items())
    {
      RoleFocus& entry = config.role_focus[role];
      focus.at("stats").get_to(entry.stats);
      focus.at("weights").get_to(entry.weights);
    }
    return config;
  }
  catch (const nlohmann::json::exception&)
  {
    return builtInStatsConfig();
  }
}

Lineup buildLabLineup(TeamID team_id, float rating,
                      std::vector<std::unique_ptr<Player>>& pool)
{
  static constexpr std::array<PlayerRole, STARTERS> ROLES = {
      PlayerRole::GK, PlayerRole::LB, PlayerRole::CB, PlayerRole::CB,
      PlayerRole::RB, PlayerRole::LM, PlayerRole::CM, PlayerRole::CM,
      PlayerRole::RM, PlayerRole::ST, PlayerRole::ST};
  static constexpr std::array<Vector2F, STARTERS> POSITIONS = {
      Vector2F{0.04f, 0.50f}, Vector2F{0.20f, 0.12f}, Vector2F{0.20f, 0.38f},
      Vector2F{0.20f, 0.62f}, Vector2F{0.20f, 0.88f}, Vector2F{0.43f, 0.12f},
      Vector2F{0.43f, 0.38f}, Vector2F{0.43f, 0.62f}, Vector2F{0.43f, 0.88f},
      Vector2F{0.78f, 0.38f}, Vector2F{0.78f, 0.62f}};
  static constexpr std::array<PlayerRole, RESERVES> BENCH = {
      PlayerRole::GK, PlayerRole::CB, PlayerRole::RB, PlayerRole::CM,
      PlayerRole::LM, PlayerRole::ST, PlayerRole::ST};

  const auto makePlayer = [&](std::uint32_t index, PlayerRole role)
  {
    const auto playerId = static_cast<PlayerID>(team_id) * 100U + index;
    const std::uint8_t height = role == PlayerRole::GK || role == PlayerRole::CB
                                    ? TALL_HEIGHT_CM
                                    : HEIGHT_CM;
    pool.push_back(std::make_unique<Player>(
        playerId, team_id, "Lab", std::to_string(playerId), role, Language::EN,
        PLAYER_WAGE, 0, PLAYER_AGE, CONTRACT_YEARS, height, Foot::Right,
        roleStats(role, rating, index + 1)));
    return static_cast<const Player*>(pool.back().get());
  };

  Lineup lineup;
  for (std::uint32_t index = 0; index < STARTERS; ++index)
  {
    const Player* player = makePlayer(index, ROLES[index]);
    if (ROLES[index] == PlayerRole::GK)
      lineup.setGoalkeeper(player);
    else
      lineup.addOutfieldPlayer(player, POSITIONS[index]);
  }
  std::vector<const Player*> reserves;
  for (std::uint32_t index = 0; index < RESERVES; ++index)
    reserves.push_back(makePlayer(STARTERS + index, BENCH[index]));
  lineup.setReserves(reserves);
  return lineup;
}

float lineupRating(const Lineup& lineup, const StatsConfig& config)
{
  double total = 0.0;
  int count = 0;
  if (const Player* keeper = lineup.getGoalkeeper())
  {
    total += keeper->getOverall(config);
    ++count;
  }
  for (const auto& slot : lineup.getOutfieldPlayers())
  {
    if (slot.player == nullptr) continue;
    total += slot.player->getOverall(config);
    ++count;
  }
  return count == 0 ? 0.0f : static_cast<float>(total / count);
}
}  // namespace Lab
