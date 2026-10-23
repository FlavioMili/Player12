// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "global/types.h"

/**
 * @enum TieBreakRule
 * @brief How teams level on points are ordered (varies by country).
 */
enum class TieBreakRule : uint8_t
{
  /** Points, goal difference, goals scored, then head-to-head. */
  GOAL_DIFFERENCE = 0,
  /** Points, head-to-head points and goal difference, then overall goal
   * difference and goals scored. */
  HEAD_TO_HEAD = 1
};

/**
 * @class League
 * @brief Represents a football league containing teams and a leaderboard.
 *
 * Full tables (goal difference, form, tie-breakers) are computed from the
 * played fixtures by Standings::compute; the leaderboard here only mirrors
 * the points. Leagues form pyramids through the parent league ID.
 */
class League
{
 public:
  /**
   * @brief Constructs a League object.
   * @param league_id The unique ID for the league.
   * @param league_name The name of the league.
   * @param initial_team_ids Optional list of initial team IDs in the league.
   * @param parent Optional parent league ID.
   * @param tie_break_rule How teams level on points are ordered.
   */
  League(LeagueID league_id, const std::string& league_name,
         const std::vector<TeamID>& initial_team_ids = {},
         std::optional<LeagueID> parent = std::nullopt,
         TieBreakRule tie_break_rule = TieBreakRule::GOAL_DIFFERENCE);

  // Accessors

  /**
   * @brief Gets the league ID.
   * @return The ID of the league.
   */
  uint8_t getId() const;

  /**
   * @brief Gets the name of the league.
   * @return The name of the league.
   */
  std::string getName() const;

  /**
   * @brief Gets the IDs of the teams in the league.
   * @return A vector containing the team IDs.
   */
  const std::vector<TeamID>& getTeamIDs() const;

  /**
   * @brief Adds a team to the league.
   * @param team_id The ID of the team to add.
   */
  void addTeamID(TeamID team_id);

  /**
   * @brief Removes a team from the league.
   * @param team_id The ID of the team to remove.
   */
  void removeTeamID(TeamID team_id);

  /**
   * @brief Gets the points of a specific team.
   * @param team_id The ID of the team.
   * @return The points of the team.
   */
  uint16_t getPoints(TeamID team_id) const;

  /**
   * @brief Adds points to a specific team.
   * @param team_id The ID of the team.
   * @param points The points to add.
   */
  void addPoints(TeamID team_id, uint16_t points);

  /**
   * @brief Sets the points of a specific team.
   * @param team_id The ID of the team.
   * @param points The points to set.
   */
  void setPoints(TeamID team_id, uint16_t points);

  /**
   * @brief Resets the points of all teams in the league to zero.
   */
  void resetPoints();

  /**
   * @brief Gets the league leaderboard.
   *
   * Points mirror of the fixture-derived table (see Standings::compute),
   * refreshed by Game after every result.
   * @return A map of team IDs to their respective points.
   */
  const std::map<TeamID, uint16_t>& getLeaderboard() const;

  /**
   * @brief Gets the parent league ID, if any.
   * @return An optional containing the parent league ID.
   */
  std::optional<LeagueID> getParentLeagueID() const;

  /** @brief Ordering rule for teams level on points. */
  TieBreakRule getTieBreakRule() const { return tie_break; }

 private:
  const LeagueID id;
  std::string name;
  const std::optional<LeagueID> parent_league_id;
  TieBreakRule tie_break;
  std::vector<TeamID> team_ids;
  std::map<TeamID, uint16_t> leaderboard;  // team_id -> points
};
