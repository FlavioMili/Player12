// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_test_teams.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "database/gamedata.h"
#include "gui/view_models/formation.h"
#include "model/league.h"
#include "model/lineup.h"
#include "model/player.h"
#include "model/role_utils.h"
#include "model/team.h"

namespace
{
constexpr int LOWEST_LEVEL = 6;
constexpr int HIGHEST_LEVEL = 96;
constexpr int LEVEL_STEP = 10;
constexpr int DEFAULT_LEVEL = 66;
constexpr int TIERED_STARTING_KEEPER = 66;
constexpr std::uint8_t AGE = 26;
constexpr std::uint8_t HEIGHT_CM = 180;
constexpr std::uint8_t CONTRACT_YEARS = 3;
/** The 4-4-2 of the formation screen, so the setup shows it as such. */
const Formation::Preset& shape() { return Formation::PRESETS[0]; }
/** Bench of the uniform squads: a keeper and one of each line. */
constexpr std::array<PlayerRole, 7> BENCH{
    PlayerRole::GK, PlayerRole::CB, PlayerRole::RB, PlayerRole::CM,
    PlayerRole::LM, PlayerRole::ST, PlayerRole::ST};

/** One player to create: who, and whether he starts (and where). */
struct Member
{
  std::string name;
  PlayerRole role;
  int level;
  /** Index into shape().slots for an outfield starter, -1 the starting keeper,
   * -2 the bench. */
  int slot;
};

constexpr int STARTING_KEEPER = -1;
constexpr int BENCHED = -2;

std::vector<int> levels()
{
  std::vector<int> result;
  for (int level = LOWEST_LEVEL; level <= HIGHEST_LEVEL; level += LEVEL_STEP)
  {
    result.push_back(level);
  }
  return result;
}

/** Every stat of the configuration at @p level. */
std::map<std::string, float> statsAt(const StatsConfig& config, int level)
{
  std::map<std::string, float> stats;
  for (const std::string& stat : config.possible_stats)
  {
    stats[stat] = static_cast<float>(level);
  }
  return stats;
}

std::vector<Member> uniformSquad(int level)
{
  std::vector<Member> squad;
  // Names repeat a role's number only when the squad has several ("CB 2").
  std::map<PlayerRole, int> seen;
  const auto add = [&](PlayerRole role, int slot)
  {
    const int count = ++seen[role];
    const std::string suffix = count > 1 ? std::format(" {}", count) : "";
    squad.push_back({.name = std::format("{} {}{}", level,
                                         RoleUtils::toString(role), suffix),
                     .role = role,
                     .level = level,
                     .slot = slot});
  };
  add(PlayerRole::GK, STARTING_KEEPER);
  for (std::size_t slot = 0; slot < shape().slots.size(); ++slot)
  {
    add(shape().slots.at(slot).role, static_cast<int>(slot));
  }
  for (const PlayerRole role : BENCH)
  {
    add(role, BENCHED);
  }
  return squad;
}

std::vector<Member> tieredSquad()
{
  std::vector<Member> squad;
  const std::vector<int> all = levels();
  squad.reserve(2 * all.size());
  for (std::size_t index = 0; index < all.size(); ++index)
  {
    squad.push_back({.name = std::format("Outfield {}", all[index]),
                     .role = shape().slots.at(index).role,
                     .level = all[index],
                     .slot = static_cast<int>(index)});
  }
  for (const int level : all)
  {
    squad.push_back(
        {.name = std::format("Keeper {}", level),
         .role = PlayerRole::GK,
         .level = level,
         .slot = level == TIERED_STARTING_KEEPER ? STARTING_KEEPER : BENCHED});
  }
  return squad;
}

/** Adds the club, its players and its selection; returns its id. */
TeamID addClub(GameData& data, TeamID id, LeagueID league,
               const std::string& name, const std::vector<Member>& squad)
{
  const StatsConfig& config = data.getStatsConfig();
  std::vector<PlayerID> ids;
  ids.reserve(squad.size());
  for (std::size_t index = 0; index < squad.size(); ++index)
  {
    ids.push_back(data.allocatePlayerId());
  }
  data.addTeam(id, Team(id, league, name, 0, ids));
  Lineup lineup;
  std::vector<const Player*> reserves;
  for (std::size_t index = 0; index < squad.size(); ++index)
  {
    const Member& member = squad[index];
    data.addPlayer(ids[index],
                   Player(ids[index], id, "", member.name, member.role,
                          Language::EN, 0, 0, AGE, CONTRACT_YEARS, HEIGHT_CM,
                          Foot::Right, statsAt(config, member.level)));
    const auto added = data.getPlayer(ids[index]);
    if (!added.has_value())
    {
      throw std::runtime_error("Could not create the sandbox player");
    }
    const Player* player = &added->get();
    if (member.slot == STARTING_KEEPER)
    {
      lineup.setGoalkeeper(player);
    }
    else if (member.slot == BENCHED)
    {
      reserves.push_back(player);
    }
    else
    {
      lineup.addOutfieldPlayer(
          player,
          shape().slots.at(static_cast<std::size_t>(member.slot)).position);
    }
  }
  lineup.setReserves(reserves);
  const auto team = data.getTeam(id);
  if (!team.has_value())
  {
    throw std::runtime_error("Could not create the sandbox club");
  }
  team->get().getLineup() = lineup;
  return id;
}
}  // namespace

std::array<TeamID, 2>& sandboxDefaultClubs()
{
  static std::array<TeamID, 2> clubs{0, 0};
  return clubs;
}

void addSandboxTestTeams(GameData& data)
{
  LeagueID league = 0;
  for (const auto& [id, existing] : data.getLeagues())
  {
    league = std::max<LeagueID>(league, id);
  }
  ++league;
  TeamID next = 0;
  for (const auto& [id, existing] : data.getTeams())
  {
    next = std::max<TeamID>(next, id);
  }
  ++next;

  std::vector<TeamID> clubs;
  const auto addPair =
      [&](const std::string& name, const std::vector<Member>& squad)
  {
    std::array<TeamID, 2> pair{};
    pair[0] = addClub(data, next++, league, name, squad);
    pair[1] = addClub(data, next++, league, name + " (copy)", squad);
    clubs.insert(clubs.end(), pair.begin(), pair.end());
    return pair;
  };
  for (const int level : levels())
  {
    const auto pair =
        addPair(std::format("Team {}", level), uniformSquad(level));
    if (level == DEFAULT_LEVEL)
    {
      sandboxDefaultClubs() = pair;
    }
  }
  addPair("Team Tiered", tieredSquad());
  data.addLeague(league, League(league, "Test teams", clubs));
}
