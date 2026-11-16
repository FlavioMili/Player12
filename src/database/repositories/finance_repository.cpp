// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "finance_repository.h"

#include <sqlite3.h>

#include <utility>

#include "model/team.h"
#include "model/world_rng.h"

FinanceRepository::FinanceRepository(std::shared_ptr<DatabaseConnection> conn)
    : db_conn(std::move(conn))
{
}

std::unordered_map<TeamID, std::vector<FinanceTransaction>>
FinanceRepository::loadAll() const
{
  std::unordered_map<TeamID, std::vector<FinanceTransaction>> ledgers;
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "SELECT team_id, game_date, category, amount FROM FinanceLedger "
      "ORDER BY team_id, seq;");
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    const auto team_id = static_cast<TeamID>(sqlite3_column_int(stmt, 0));
    ledgers[team_id].push_back(
        {dateFromInt(sqlite3_column_int(stmt, 1)),
         static_cast<FinanceCategory>(sqlite3_column_int(stmt, 2)),
         sqlite3_column_int64(stmt, 3)});
  }
  sqlite3_finalize(stmt);
  return ledgers;
}

void FinanceRepository::insertPending(
    const std::vector<std::reference_wrapper<const Team>>& teams) const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "INSERT OR IGNORE INTO FinanceLedger (team_id, seq, game_date, "
      "category, amount) VALUES (?, ?, ?, ?, ?);");
  for (const auto& team_ref : teams)
  {
    const Team& team = team_ref.get();
    const Finances& finances = team.getFinances();
    std::size_t seq = finances.persistedCount();
    for (const FinanceTransaction& transaction : finances.pendingTransactions())
    {
      sqlite3_bind_int(stmt, 1, team.getId());
      sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(seq++));
      sqlite3_bind_int(stmt, 3, dateToInt(transaction.date));
      sqlite3_bind_int(
          stmt, 4, static_cast<int>(std::to_underlying(transaction.category)));
      sqlite3_bind_int64(stmt, 5, transaction.amount);
      db_conn->executeStep(stmt);
      sqlite3_reset(stmt);
      sqlite3_clear_bindings(stmt);
    }
  }
  sqlite3_finalize(stmt);
}
