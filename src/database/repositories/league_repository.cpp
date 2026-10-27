// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "league_repository.h"

#include <sqlite3.h>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>

#include "database/SQLLoader.h"

LeagueRepository::LeagueRepository(std::shared_ptr<DatabaseConnection> conn)
    : db_conn(conn)
{
}

std::vector<League> LeagueRepository::loadAllLeagues() const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "SELECT id, name, parent_league_id, tiebreak FROM Leagues;");
  std::vector<League> leagues;

  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    int id = sqlite3_column_int(stmt, 0);
    const unsigned char* name_text = sqlite3_column_text(stmt, 1);
    std::string name =
        name_text ? reinterpret_cast<const char*>(name_text) : "";
    std::optional<LeagueID> parent;
    if (sqlite3_column_type(stmt, 2) != SQLITE_NULL)
      parent = static_cast<LeagueID>(sqlite3_column_int(stmt, 2));
    leagues.emplace_back(
        static_cast<LeagueID>(id), name, std::vector<TeamID>{}, parent,
        static_cast<TieBreakRule>(sqlite3_column_int(stmt, 3)));
  }

  sqlite3_finalize(stmt);

  for (auto& league : leagues)
  {
    loadTeamsForLeague(league);
    loadLeaguePoints(league);
  }

  return leagues;
}

void LeagueRepository::loadTeamsForLeague(League& league) const
{
  sqlite3_stmt* stmt =
      db_conn->prepareStatement("SELECT id FROM Teams WHERE league_id = ?");

  sqlite3_bind_int(stmt, 1, league.getId());

  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    int team_id = sqlite3_column_int(stmt, 0);
    league.addTeamID(static_cast<uint16_t>(team_id));
  }

  sqlite3_finalize(stmt);
}

void LeagueRepository::insertLeague(const League& league) const
{
  sqlite3_stmt* stmt =
      db_conn->prepareStatement(SQLLoader::getQuery(Query::INSERT_LEAGUE));

  sqlite3_bind_text(stmt, 1, league.getName().data(), -1, SQLITE_TRANSIENT);

  db_conn->executeStep(stmt);
  sqlite3_finalize(stmt);
}

void LeagueRepository::insertLeagueWithId(const League& league) const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      SQLLoader::getQuery(Query::INSERT_LEAGUE_WITH_ID));

  sqlite3_bind_int(stmt, 1, league.getId());
  sqlite3_bind_text(stmt, 2, league.getName().c_str(), -1, SQLITE_TRANSIENT);

  if (league.getParentLeagueID().has_value())
  {
    LeagueID parentID = *league.getParentLeagueID();
    sqlite3_bind_int(stmt, 3, parentID);
  }
  else
  {
    sqlite3_bind_null(stmt, 3);
  }

  db_conn->executeStep(stmt);
  sqlite3_finalize(stmt);

  sqlite3_stmt* rule_stmt = db_conn->prepareStatement(
      "UPDATE Leagues SET tiebreak = ? WHERE id = ?;");
  sqlite3_bind_int(rule_stmt, 1, std::to_underlying(league.getTieBreakRule()));
  sqlite3_bind_int(rule_stmt, 2, league.getId());
  db_conn->executeStep(rule_stmt);
  sqlite3_finalize(rule_stmt);
}

void LeagueRepository::saveLeaguePoints(const League& league) const
{
  // Teams move between leagues at season end: drop rows of former members.
  sqlite3_stmt* stmt_delete = db_conn->prepareStatement(
      "DELETE FROM LeaguePoints WHERE league_id = ?;");
  sqlite3_bind_int(stmt_delete, 1, league.getId());
  db_conn->executeStep(stmt_delete);
  sqlite3_finalize(stmt_delete);

  sqlite3_stmt* stmt = db_conn->prepareStatement(
      SQLLoader::getQuery(Query::UPSERT_LEAGUE_POINTS));

  for (const auto& [teamId, points] : league.getLeaderboard())
  {
    sqlite3_bind_int(stmt, 1, league.getId());
    sqlite3_bind_int(stmt, 2, teamId);
    sqlite3_bind_int(stmt, 3, points);

    db_conn->executeStep(stmt);

    sqlite3_clear_bindings(stmt);
    sqlite3_reset(stmt);
  }

  sqlite3_finalize(stmt);
}

void LeagueRepository::loadLeaguePoints(League& league) const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      SQLLoader::getQuery(Query::SELECT_LEAGUE_POINTS));

  sqlite3_bind_int(stmt, 1, league.getId());

  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    auto team_id = static_cast<uint16_t>(sqlite3_column_int(stmt, 0));
    auto points = static_cast<uint16_t>(sqlite3_column_int(stmt, 1));
    if (std::ranges::contains(league.getTeamIDs(), team_id))
      league.setPoints(team_id, points);
  }

  sqlite3_finalize(stmt);
}

void LeagueRepository::saveTeamMemberships(const League& league) const
{
  sqlite3_stmt* stmt =
      db_conn->prepareStatement("UPDATE Teams SET league_id = ? WHERE id = ?;");
  for (const TeamID team_id : league.getTeamIDs())
  {
    sqlite3_bind_int(stmt, 1, league.getId());
    sqlite3_bind_int(stmt, 2, team_id);
    db_conn->executeStep(stmt);
    sqlite3_clear_bindings(stmt);
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
}

void LeagueRepository::resetAllLeaguePoints() const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      SQLLoader::getQuery(Query::RESET_ALL_LEAGUE_POINTS));

  db_conn->executeStep(stmt);
  sqlite3_finalize(stmt);
}
