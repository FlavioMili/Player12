// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "world_state_repository.h"

#include <sqlite3.h>

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

WorldStateRepository::WorldStateRepository(
    std::shared_ptr<DatabaseConnection> conn)
    : db_conn(std::move(conn))
{
}

bool WorldStateRepository::loadWorldState(std::uint64_t& seed,
                                          PlayerID& next_player_id) const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "SELECT seed, next_player_id FROM WorldState WHERE id = 1;");
  bool found = false;
  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    seed = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 0));
    next_player_id = static_cast<PlayerID>(sqlite3_column_int64(stmt, 1));
    found = true;
  }
  sqlite3_finalize(stmt);
  return found;
}

void WorldStateRepository::saveWorldState(std::uint64_t seed,
                                          PlayerID next_player_id,
                                          std::uint32_t next_staff_id) const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "INSERT OR REPLACE INTO WorldState (id, seed, next_player_id, "
      "next_staff_id) VALUES (1, ?, ?, ?);");
  sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(seed));
  sqlite3_bind_int64(stmt, 2, next_player_id);
  sqlite3_bind_int64(stmt, 3, next_staff_id);
  db_conn->executeStep(stmt);
  sqlite3_finalize(stmt);
}

std::uint32_t WorldStateRepository::loadNextStaffId() const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "SELECT next_staff_id FROM WorldState WHERE id = 1;");
  std::uint32_t next_staff_id = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    next_staff_id = static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 0));
  sqlite3_finalize(stmt);
  return next_staff_id;
}

bool WorldStateRepository::loadBoard(BoardState& board) const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "SELECT team_id, season_year, objective, expected_position, "
      "target_position, confidence, low_reviews, league_matches, dismissed, "
      "recent_deltas, cup_objective, finance_objective, youth_target, "
      "start_balance, targets_set FROM BoardState WHERE id = 1;");
  bool found = false;
  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    BoardState loaded;
    loaded.team_id = static_cast<TeamID>(sqlite3_column_int(stmt, 0));
    loaded.season_year =
        static_cast<std::uint16_t>(sqlite3_column_int(stmt, 1));
    loaded.objective = static_cast<BoardObjective>(sqlite3_column_int(stmt, 2));
    loaded.expected_position =
        static_cast<std::uint8_t>(sqlite3_column_int(stmt, 3));
    loaded.target_position =
        static_cast<std::uint8_t>(sqlite3_column_int(stmt, 4));
    loaded.confidence = static_cast<float>(sqlite3_column_double(stmt, 5));
    loaded.low_reviews = static_cast<std::uint8_t>(sqlite3_column_int(stmt, 6));
    loaded.league_matches =
        static_cast<std::uint8_t>(sqlite3_column_int(stmt, 7));
    loaded.dismissed = sqlite3_column_int(stmt, 8) != 0;
    const unsigned char* text = sqlite3_column_text(stmt, 9);
    if (const auto deltas = nlohmann::json::parse(
            text ? reinterpret_cast<const char*>(text) : "[]", nullptr, false);
        deltas.is_array())
    {
      for (const auto& delta : deltas)
      {
        if (!delta.is_number() ||
            loaded.result_count >= loaded.recent_deltas.size())
          continue;
        loaded.recent_deltas[loaded.result_count++] = delta.get<float>();
      }
    }
    loaded.cup_objective = static_cast<CupObjective>(
        std::clamp(sqlite3_column_int(stmt, 10), 0, 4));
    loaded.finance_objective = static_cast<FinanceObjective>(
        std::clamp(sqlite3_column_int(stmt, 11), 0, 1));
    loaded.youth_target =
        static_cast<std::uint8_t>(sqlite3_column_int(stmt, 12));
    loaded.start_balance = sqlite3_column_int64(stmt, 13);
    loaded.targets_set = sqlite3_column_int(stmt, 14) != 0;
    board = loaded;
    found = true;
  }
  sqlite3_finalize(stmt);
  return found;
}

void WorldStateRepository::saveBoard(const BoardState& board) const
{
  nlohmann::json deltas = nlohmann::json::array();
  for (std::size_t i = 0; i < board.result_count; ++i)
    deltas.push_back(board.recent_deltas[i]);
  const std::string deltas_text = deltas.dump();

  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "INSERT OR REPLACE INTO BoardState (id, team_id, season_year, "
      "objective, expected_position, target_position, confidence, "
      "low_reviews, league_matches, dismissed, recent_deltas, "
      "cup_objective, finance_objective, youth_target, start_balance, "
      "targets_set) VALUES (1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
  sqlite3_bind_int(stmt, 1, board.team_id);
  sqlite3_bind_int(stmt, 2, board.season_year);
  sqlite3_bind_int(stmt, 3,
                   static_cast<int>(std::to_underlying(board.objective)));
  sqlite3_bind_int(stmt, 4, board.expected_position);
  sqlite3_bind_int(stmt, 5, board.target_position);
  sqlite3_bind_double(stmt, 6, static_cast<double>(board.confidence));
  sqlite3_bind_int(stmt, 7, board.low_reviews);
  sqlite3_bind_int(stmt, 8, board.league_matches);
  sqlite3_bind_int(stmt, 9, board.dismissed ? 1 : 0);
  sqlite3_bind_text(stmt, 10, deltas_text.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 11,
                   static_cast<int>(std::to_underlying(board.cup_objective)));
  sqlite3_bind_int(
      stmt, 12, static_cast<int>(std::to_underlying(board.finance_objective)));
  sqlite3_bind_int(stmt, 13, board.youth_target);
  sqlite3_bind_int64(stmt, 14, board.start_balance);
  sqlite3_bind_int(stmt, 15, board.targets_set ? 1 : 0);
  db_conn->executeStep(stmt);
  sqlite3_finalize(stmt);
}
