// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "league.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

League::League(LeagueID league_id, const std::string& league_name,
               const std::vector<TeamID>& initial_team_ids,
               std::optional<LeagueID> parent, TieBreakRule tie_break_rule)
    : id(league_id),
      name(league_name),
      parent_league_id(parent),
      tie_break(tie_break_rule)
{
  for (auto tid : initial_team_ids)
  {
    team_ids.push_back(tid);
    leaderboard[tid] = 0;
  }
}

uint8_t League::getId() const { return id; }

std::string League::getName() const { return name; }

const std::vector<TeamID>& League::getTeamIDs() const { return team_ids; }

void League::addTeamID(TeamID team_id)
{
  if (std::ranges::find(team_ids, team_id) == team_ids.end())
  {
    team_ids.push_back(team_id);
    leaderboard[team_id] = 0;
  }
}

void League::removeTeamID(TeamID team_id)
{
  if (std::erase(team_ids, team_id) > 0)
  {
    leaderboard.erase(team_id);
  }
}

// Points management
uint16_t League::getPoints(TeamID team_id) const
{
  auto it = leaderboard.find(team_id);
  return it != leaderboard.end() ? it->second : 0;
}

void League::addPoints(TeamID team_id, uint16_t points)
{
  leaderboard[team_id] = static_cast<uint16_t>(leaderboard[team_id] + points);
}

void League::setPoints(TeamID team_id, uint16_t points)
{
  leaderboard[team_id] = points;
}

void League::resetPoints()
{
  for (auto& [_, pts] : leaderboard)
  {
    pts = 0;
  }
}

const std::map<TeamID, uint16_t>& League::getLeaderboard() const
{
  return leaderboard;
}

std::optional<LeagueID> League::getParentLeagueID() const
{
  return parent_league_id;
}
