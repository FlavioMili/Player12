// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "competition_repository.h"

#include <sqlite3.h>

#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace
{
constexpr const char* PLAYER_STATS_COLUMNS =
    "season, player_id, team_id, competition_type, appearances, starts, "
    "minutes, goals, assists, yellow_cards, red_cards, rating_total, "
    "rated_matches";

std::string columnText(sqlite3_stmt* stmt, int column)
{
  const unsigned char* text = sqlite3_column_text(stmt, column);
  return text ? reinterpret_cast<const char*>(text) : std::string();
}

std::vector<TeamID> teamList(const std::string& text)
{
  const auto json = nlohmann::json::parse(text, nullptr, false);
  if (!json.is_array()) return {};
  return json.get<std::vector<TeamID>>();
}

void bindOptionalId(sqlite3_stmt* stmt, int index, uint32_t id)
{
  if (id == 0)
    sqlite3_bind_null(stmt, index);
  else
    sqlite3_bind_int64(stmt, index, id);
}

PlayerSeasonStats readPlayerStats(sqlite3_stmt* stmt)
{
  PlayerSeasonStats stats;
  const auto column = [stmt](int index)
  { return static_cast<uint16_t>(sqlite3_column_int(stmt, index)); };
  stats.season = column(0);
  stats.player_id = static_cast<PlayerID>(sqlite3_column_int64(stmt, 1));
  stats.team_id = column(2);
  stats.competition_type = static_cast<MatchType>(sqlite3_column_int(stmt, 3));
  stats.appearances = column(4);
  stats.starts = column(5);
  stats.minutes = column(6);
  stats.goals = column(7);
  stats.assists = column(8);
  stats.yellow_cards = column(9);
  stats.red_cards = column(10);
  stats.rating_total = static_cast<float>(sqlite3_column_double(stmt, 11));
  stats.rated_matches = column(12);
  return stats;
}
}  // namespace

CompetitionRepository::CompetitionRepository(
    std::shared_ptr<DatabaseConnection> conn)
    : db_conn(std::move(conn))
{
}

void CompetitionRepository::saveSeasonHistory(
    const std::vector<SeasonHistoryEntry>& entries) const
{
  if (entries.empty()) return;
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "INSERT OR REPLACE INTO SeasonHistory (season, start_year, "
      "competition_type, competition_id, competition_name, champion_id, "
      "runner_up_id, promoted, relegated, top_scorer_id, top_scorer_goals) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
  for (const SeasonHistoryEntry& entry : entries)
  {
    sqlite3_bind_int(stmt, 1, entry.season);
    sqlite3_bind_int(stmt, 2, entry.start_year);
    sqlite3_bind_int(stmt, 3, std::to_underlying(entry.competition_type));
    sqlite3_bind_int(stmt, 4, entry.competition_id);
    sqlite3_bind_text(stmt, 5, entry.competition_name.c_str(), -1,
                      SQLITE_TRANSIENT);
    bindOptionalId(stmt, 6, entry.champion_id);
    bindOptionalId(stmt, 7, entry.runner_up_id);
    sqlite3_bind_text(stmt, 8, nlohmann::json(entry.promoted).dump().c_str(),
                      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, nlohmann::json(entry.relegated).dump().c_str(),
                      -1, SQLITE_TRANSIENT);
    bindOptionalId(stmt, 10, entry.top_scorer_id);
    sqlite3_bind_int(stmt, 11, entry.top_scorer_goals);
    db_conn->executeStep(stmt);
    sqlite3_clear_bindings(stmt);
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
}

std::vector<SeasonHistoryEntry> CompetitionRepository::loadSeasonHistory() const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "SELECT season, start_year, competition_type, competition_id, "
      "competition_name, champion_id, runner_up_id, promoted, relegated, "
      "top_scorer_id, top_scorer_goals FROM SeasonHistory "
      "ORDER BY season, competition_type, competition_id;");
  std::vector<SeasonHistoryEntry> entries;
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    SeasonHistoryEntry entry;
    entry.season = static_cast<uint16_t>(sqlite3_column_int(stmt, 0));
    entry.start_year = static_cast<uint16_t>(sqlite3_column_int(stmt, 1));
    entry.competition_type =
        static_cast<MatchType>(sqlite3_column_int(stmt, 2));
    entry.competition_id = static_cast<LeagueID>(sqlite3_column_int(stmt, 3));
    entry.competition_name = columnText(stmt, 4);
    entry.champion_id = static_cast<TeamID>(sqlite3_column_int(stmt, 5));
    entry.runner_up_id = static_cast<TeamID>(sqlite3_column_int(stmt, 6));
    entry.promoted = teamList(columnText(stmt, 7));
    entry.relegated = teamList(columnText(stmt, 8));
    entry.top_scorer_id = static_cast<PlayerID>(sqlite3_column_int64(stmt, 9));
    entry.top_scorer_goals =
        static_cast<uint16_t>(sqlite3_column_int(stmt, 10));
    entries.push_back(std::move(entry));
  }
  sqlite3_finalize(stmt);
  return entries;
}

void CompetitionRepository::savePlayerSeasonStats(
    const PlayerSeasonTable& table) const
{
  if (table.empty()) return;
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      std::string("INSERT OR REPLACE INTO PlayerSeasonStats (") +
      PLAYER_STATS_COLUMNS +
      ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
  for (const auto& [key, stats] : table)
  {
    sqlite3_bind_int(stmt, 1, stats.season);
    sqlite3_bind_int64(stmt, 2, stats.player_id);
    sqlite3_bind_int(stmt, 3, stats.team_id);
    sqlite3_bind_int(stmt, 4, std::to_underlying(stats.competition_type));
    sqlite3_bind_int(stmt, 5, stats.appearances);
    sqlite3_bind_int(stmt, 6, stats.starts);
    sqlite3_bind_int(stmt, 7, stats.minutes);
    sqlite3_bind_int(stmt, 8, stats.goals);
    sqlite3_bind_int(stmt, 9, stats.assists);
    sqlite3_bind_int(stmt, 10, stats.yellow_cards);
    sqlite3_bind_int(stmt, 11, stats.red_cards);
    sqlite3_bind_double(stmt, 12, static_cast<double>(stats.rating_total));
    sqlite3_bind_int(stmt, 13, stats.rated_matches);
    db_conn->executeStep(stmt);
    sqlite3_clear_bindings(stmt);
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
}

PlayerSeasonTable CompetitionRepository::loadPlayerSeasonStats(
    uint16_t season) const
{
  sqlite3_stmt* stmt =
      db_conn->prepareStatement(std::string("SELECT ") + PLAYER_STATS_COLUMNS +
                                " FROM PlayerSeasonStats WHERE season = ?;");
  sqlite3_bind_int(stmt, 1, season);
  PlayerSeasonTable table;
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    const PlayerSeasonStats stats = readPlayerStats(stmt);
    table.try_emplace(PlayerSeasonKey{stats.season, stats.player_id,
                                      stats.team_id, stats.competition_type},
                      stats);
  }
  sqlite3_finalize(stmt);
  return table;
}

std::vector<PlayerSeasonStats> CompetitionRepository::loadPlayerCareer(
    PlayerID player_id) const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      std::string("SELECT ") + PLAYER_STATS_COLUMNS +
      " FROM PlayerSeasonStats WHERE player_id = ? ORDER BY season;");
  sqlite3_bind_int64(stmt, 1, player_id);
  std::vector<PlayerSeasonStats> career;
  while (sqlite3_step(stmt) == SQLITE_ROW)
    career.push_back(readPlayerStats(stmt));
  sqlite3_finalize(stmt);
  return career;
}

void CompetitionRepository::saveDiscipline(
    const std::vector<DisciplinaryRecord>& records) const
{
  sqlite3_stmt* clear =
      db_conn->prepareStatement("DELETE FROM PlayerDiscipline;");
  db_conn->executeStep(clear);
  sqlite3_finalize(clear);
  if (records.empty()) return;

  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "INSERT INTO PlayerDiscipline (player_id, competition_type, "
      "season_yellows, season_reds, ban_matches) VALUES (?, ?, ?, ?, ?);");
  for (const DisciplinaryRecord& record : records)
  {
    sqlite3_bind_int64(stmt, 1, record.player_id);
    sqlite3_bind_int(stmt, 2, std::to_underlying(record.scope));
    sqlite3_bind_int(stmt, 3, record.season_yellows);
    sqlite3_bind_int(stmt, 4, record.season_reds);
    sqlite3_bind_int(stmt, 5, record.ban_matches);
    db_conn->executeStep(stmt);
    sqlite3_clear_bindings(stmt);
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
}

std::vector<DisciplinaryRecord> CompetitionRepository::loadDiscipline() const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "SELECT player_id, competition_type, season_yellows, season_reds, "
      "ban_matches FROM PlayerDiscipline;");
  std::vector<DisciplinaryRecord> records;
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    DisciplinaryRecord record;
    record.player_id = static_cast<PlayerID>(sqlite3_column_int64(stmt, 0));
    record.scope = static_cast<MatchType>(sqlite3_column_int(stmt, 1));
    record.season_yellows = static_cast<uint16_t>(sqlite3_column_int(stmt, 2));
    record.season_reds = static_cast<uint8_t>(sqlite3_column_int(stmt, 3));
    record.ban_matches = static_cast<uint8_t>(sqlite3_column_int(stmt, 4));
    records.push_back(record);
  }
  sqlite3_finalize(stmt);
  return records;
}
