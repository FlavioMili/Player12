// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "fixture_repository.h"

#include <sqlite3.h>

#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "global/logger.h"
#include "model/match_report.h"
#include "model/world_rng.h"

namespace
{
constexpr const char* INSERT_FIXTURE_SQL =
    "INSERT OR REPLACE INTO Fixtures (game_date, home_team_id, away_team_id, "
    "match_type, home_goals, away_goals, played, competition_id, stage, "
    "extra_time, home_penalties, away_penalties, kickoff) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

constexpr const char* REPORT_COLUMNS =
    "game_date, home_team_id, away_team_id, season, match_type, "
    "competition_id, stage, home_goals, away_goals, extra_time, "
    "home_penalties, away_penalties, attendance, stats, events, players";

std::string columnText(sqlite3_stmt* stmt, int column)
{
  const unsigned char* text = sqlite3_column_text(stmt, column);
  return text ? reinterpret_cast<const char*>(text) : std::string();
}

void bindFixture(sqlite3_stmt* stmt, const Match& match)
{
  sqlite3_bind_text(stmt, 1, match.getDate().toString().c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, match.getHomeTeamId());
  sqlite3_bind_int(stmt, 3, match.getAwayTeamId());
  sqlite3_bind_int(stmt, 4, std::to_underlying(match.getMatchType()));
  sqlite3_bind_int(stmt, 5, match.getHomeScore());
  sqlite3_bind_int(stmt, 6, match.getAwayScore());
  sqlite3_bind_int(stmt, 7, match.isPlayed() ? 1 : 0);
  sqlite3_bind_int(stmt, 8, match.getCompetitionId());
  sqlite3_bind_int(stmt, 9, match.getStage());
  sqlite3_bind_int(stmt, 10, match.wentToExtraTime() ? 1 : 0);
  if (match.wentToPenalties())
  {
    sqlite3_bind_int(stmt, 11, match.getHomePenalties());
    sqlite3_bind_int(stmt, 12, match.getAwayPenalties());
  }
  else
  {
    sqlite3_bind_null(stmt, 11);
    sqlite3_bind_null(stmt, 12);
  }
  sqlite3_bind_int(stmt, 13, match.getScheduledKickoff());
}

/** Columns of a fixture that change after it was scheduled (the kick-off
 * never does: a fixture moved to another day is a new row). */
struct FixtureState
{
  int match_type = 0;
  int home_goals = 0;
  int away_goals = 0;
  int played = 0;
  int competition_id = 0;
  int stage = 0;
  int extra_time = 0;
  int home_penalties = -1;  ///< -1: NULL (no shoot-out).
  int away_penalties = -1;

  bool operator==(const FixtureState&) const = default;
};

FixtureState stateOf(const Match& match)
{
  FixtureState state;
  state.match_type = std::to_underlying(match.getMatchType());
  state.home_goals = match.getHomeScore();
  state.away_goals = match.getAwayScore();
  state.played = match.isPlayed() ? 1 : 0;
  state.competition_id = match.getCompetitionId();
  state.stage = match.getStage();
  state.extra_time = match.wentToExtraTime() ? 1 : 0;
  if (match.wentToPenalties())
  {
    state.home_penalties = match.getHomePenalties();
    state.away_penalties = match.getAwayPenalties();
  }
  return state;
}

/** Natural key of a fixture (UNIQUE(game_date, home, away)). */
std::uint64_t fixtureKey(const GameDateValue& date, int home, int away)
{
  return (static_cast<std::uint64_t>(
              static_cast<std::uint32_t>(dayOrdinal(date)))
          << 32) |
         (static_cast<std::uint64_t>(home & 0xFFFF) << 16) |
         static_cast<std::uint64_t>(away & 0xFFFF);
}

MatchReport readReport(sqlite3_stmt* stmt)
{
  MatchReport report;
  report.date = GameDateValue::fromString(columnText(stmt, 0));
  report.home_team_id = static_cast<TeamID>(sqlite3_column_int(stmt, 1));
  report.away_team_id = static_cast<TeamID>(sqlite3_column_int(stmt, 2));
  report.season = static_cast<uint16_t>(sqlite3_column_int(stmt, 3));
  report.match_type = static_cast<MatchType>(sqlite3_column_int(stmt, 4));
  report.competition_id = static_cast<LeagueID>(sqlite3_column_int(stmt, 5));
  report.stage = static_cast<uint8_t>(sqlite3_column_int(stmt, 6));
  report.home_goals = static_cast<uint8_t>(sqlite3_column_int(stmt, 7));
  report.away_goals = static_cast<uint8_t>(sqlite3_column_int(stmt, 8));
  report.extra_time = sqlite3_column_int(stmt, 9) != 0;
  report.penalties = sqlite3_column_type(stmt, 10) != SQLITE_NULL;
  report.home_penalties = static_cast<uint8_t>(sqlite3_column_int(stmt, 10));
  report.away_penalties = static_cast<uint8_t>(sqlite3_column_int(stmt, 11));
  report.attendance = static_cast<uint32_t>(sqlite3_column_int64(stmt, 12));
  report.statsFromJson(columnText(stmt, 13));
  report.eventsFromJson(columnText(stmt, 14));
  report.playersFromJson(columnText(stmt, 15));
  return report;
}
}  // namespace

FixtureRepository::FixtureRepository(std::shared_ptr<DatabaseConnection> conn)
    : db_conn(conn)
{
}

void FixtureRepository::insertFixture(const Match& match) const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(INSERT_FIXTURE_SQL);
  bindFixture(stmt, match);
  db_conn->executeStep(stmt);
  sqlite3_finalize(stmt);
}

std::vector<Match> FixtureRepository::loadAllMatches() const
{
  std::vector<Match> matches;
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "SELECT home_team_id, away_team_id, game_date, match_type, home_goals, "
      "away_goals, played, competition_id, stage, extra_time, home_penalties, "
      "away_penalties, kickoff FROM Fixtures;");

  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    auto home_id = static_cast<uint16_t>(sqlite3_column_int(stmt, 0));
    auto away_id = static_cast<uint16_t>(sqlite3_column_int(stmt, 1));
    std::string date_str = columnText(stmt, 2);
    auto match_type = static_cast<MatchType>(sqlite3_column_int(stmt, 3));

    Match match(home_id, away_id, GameDateValue::fromString(date_str),
                match_type, static_cast<LeagueID>(sqlite3_column_int(stmt, 7)),
                static_cast<uint8_t>(sqlite3_column_int(stmt, 8)));
    match.setKickoff(static_cast<uint16_t>(sqlite3_column_int(stmt, 12)));
    if (sqlite3_column_int(stmt, 6) != 0)
    {
      std::optional<std::pair<uint8_t, uint8_t>> penalties;
      if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
        penalties.emplace(static_cast<uint8_t>(sqlite3_column_int(stmt, 10)),
                          static_cast<uint8_t>(sqlite3_column_int(stmt, 11)));
      match.setKnockoutResult(static_cast<uint8_t>(sqlite3_column_int(stmt, 4)),
                              static_cast<uint8_t>(sqlite3_column_int(stmt, 5)),
                              sqlite3_column_int(stmt, 9) != 0, penalties);
    }
    matches.push_back(std::move(match));
  }

  sqlite3_finalize(stmt);
  return matches;
}

void FixtureRepository::saveCalendar(const Calendar& calendar) const
{
  // Only fixtures added, changed (results, cup draws) or dropped since the
  // last save are written: a season holds thousands of fixtures.
  struct Stored
  {
    sqlite3_int64 id = 0;
    FixtureState state;
    bool seen = false;
  };
  std::unordered_map<std::uint64_t, Stored> stored;
  sqlite3_stmt* select = db_conn->prepareStatement(
      "SELECT id, game_date, home_team_id, away_team_id, match_type, "
      "home_goals, away_goals, played, competition_id, stage, extra_time, "
      "home_penalties, away_penalties FROM Fixtures;");
  while (sqlite3_step(select) == SQLITE_ROW)
  {
    Stored row;
    row.id = sqlite3_column_int64(select, 0);
    FixtureState& state = row.state;
    state.match_type = sqlite3_column_int(select, 4);
    state.home_goals = sqlite3_column_int(select, 5);
    state.away_goals = sqlite3_column_int(select, 6);
    state.played = sqlite3_column_int(select, 7);
    state.competition_id = sqlite3_column_int(select, 8);
    state.stage = sqlite3_column_int(select, 9);
    state.extra_time = sqlite3_column_int(select, 10);
    if (sqlite3_column_type(select, 11) != SQLITE_NULL)
    {
      state.home_penalties = sqlite3_column_int(select, 11);
      state.away_penalties = sqlite3_column_int(select, 12);
    }
    stored.try_emplace(
        fixtureKey(GameDateValue::fromString(columnText(select, 1)),
                   sqlite3_column_int(select, 2),
                   sqlite3_column_int(select, 3)),
        row);
  }
  sqlite3_finalize(select);

  sqlite3_stmt* insert = db_conn->prepareStatement(INSERT_FIXTURE_SQL);
  sqlite3_stmt* update = db_conn->prepareStatement(
      "UPDATE Fixtures SET match_type = ?, home_goals = ?, away_goals = ?, "
      "played = ?, competition_id = ?, stage = ?, extra_time = ?, "
      "home_penalties = ?, away_penalties = ? WHERE id = ?;");
  for (const auto& [day, matches] : calendar.getFullCalendar())
  {
    for (const Match& match : matches)
    {
      const auto found = stored.find(
          fixtureKey(day, match.getHomeTeamId(), match.getAwayTeamId()));
      if (found == stored.end())
      {
        bindFixture(insert, match);
        db_conn->executeStep(insert);
        sqlite3_reset(insert);
        continue;
      }
      found->second.seen = true;
      const FixtureState state = stateOf(match);
      if (state == found->second.state) continue;
      sqlite3_bind_int(update, 1, state.match_type);
      sqlite3_bind_int(update, 2, state.home_goals);
      sqlite3_bind_int(update, 3, state.away_goals);
      sqlite3_bind_int(update, 4, state.played);
      sqlite3_bind_int(update, 5, state.competition_id);
      sqlite3_bind_int(update, 6, state.stage);
      sqlite3_bind_int(update, 7, state.extra_time);
      if (state.home_penalties >= 0)
      {
        sqlite3_bind_int(update, 8, state.home_penalties);
        sqlite3_bind_int(update, 9, state.away_penalties);
      }
      else
      {
        sqlite3_bind_null(update, 8);
        sqlite3_bind_null(update, 9);
      }
      sqlite3_bind_int64(update, 10, found->second.id);
      db_conn->executeStep(update);
      sqlite3_reset(update);
      found->second.state = state;
    }
  }
  sqlite3_finalize(insert);
  sqlite3_finalize(update);

  sqlite3_stmt* remove =
      db_conn->prepareStatement("DELETE FROM Fixtures WHERE id = ?;");
  for (const auto& [key, row] : stored)
  {
    if (row.seen) continue;
    sqlite3_bind_int64(remove, 1, row.id);
    db_conn->executeStep(remove);
    sqlite3_reset(remove);
  }
  sqlite3_finalize(remove);
}

void FixtureRepository::loadCalendar(Calendar& calendar) const
{
  auto matches = loadAllMatches();
  for (const auto& match : matches)
  {
    calendar.addMatch(match);
  }
  calendar.markRestChecked();
}

void FixtureRepository::saveMatchReports(
    const std::vector<MatchReport>& reports) const
{
  if (reports.empty()) return;
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      std::string("INSERT OR REPLACE INTO MatchReports (") + REPORT_COLUMNS +
      ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
  for (const MatchReport& report : reports)
  {
    sqlite3_bind_text(stmt, 1, report.date.toString().c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, report.home_team_id);
    sqlite3_bind_int(stmt, 3, report.away_team_id);
    sqlite3_bind_int(stmt, 4, report.season);
    sqlite3_bind_int(stmt, 5, std::to_underlying(report.match_type));
    sqlite3_bind_int(stmt, 6, report.competition_id);
    sqlite3_bind_int(stmt, 7, report.stage);
    sqlite3_bind_int(stmt, 8, report.home_goals);
    sqlite3_bind_int(stmt, 9, report.away_goals);
    sqlite3_bind_int(stmt, 10, report.extra_time ? 1 : 0);
    if (report.penalties)
    {
      sqlite3_bind_int(stmt, 11, report.home_penalties);
      sqlite3_bind_int(stmt, 12, report.away_penalties);
    }
    else
    {
      sqlite3_bind_null(stmt, 11);
      sqlite3_bind_null(stmt, 12);
    }
    sqlite3_bind_int64(stmt, 13, report.attendance);
    sqlite3_bind_text(stmt, 14, report.statsToJson().c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 15, report.eventsToJson().c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 16, report.playersToJson().c_str(), -1,
                      SQLITE_TRANSIENT);
    db_conn->executeStep(stmt);
    sqlite3_clear_bindings(stmt);
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
}

std::optional<MatchReport> FixtureRepository::loadMatchReport(
    const GameDateValue& date, TeamID home_id, TeamID away_id) const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      std::string("SELECT ") + REPORT_COLUMNS +
      " FROM MatchReports WHERE game_date = ? AND home_team_id = ? AND "
      "away_team_id = ?;");
  sqlite3_bind_text(stmt, 1, date.toString().c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, home_id);
  sqlite3_bind_int(stmt, 3, away_id);
  std::optional<MatchReport> report;
  if (sqlite3_step(stmt) == SQLITE_ROW) report = readReport(stmt);
  sqlite3_finalize(stmt);
  return report;
}

std::vector<MatchReport> FixtureRepository::loadTeamMatchReports(
    uint16_t season, TeamID team_id) const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      std::string("SELECT ") + REPORT_COLUMNS +
      " FROM MatchReports WHERE season = ? AND (home_team_id = ? OR "
      "away_team_id = ?) ORDER BY game_date;");
  sqlite3_bind_int(stmt, 1, season);
  sqlite3_bind_int(stmt, 2, team_id);
  sqlite3_bind_int(stmt, 3, team_id);
  std::vector<MatchReport> reports;
  while (sqlite3_step(stmt) == SQLITE_ROW) reports.push_back(readReport(stmt));
  sqlite3_finalize(stmt);
  return reports;
}
