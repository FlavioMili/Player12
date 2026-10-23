// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/guidance.h"

#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "database/database_connection.h"
#include "model/world_rng.h"

namespace
{
template <typename Read>
void forEachRow(const DatabaseConnection& db, const char* sql, Read read)
{
  sqlite3_stmt* stmt = db.prepareStatement(sql);
  while (sqlite3_step(stmt) == SQLITE_ROW) read(stmt);
  sqlite3_finalize(stmt);
}

void execute(const DatabaseConnection& db, const char* sql)
{
  sqlite3_exec(db.getRaw(), sql, nullptr, nullptr, nullptr);
}

bool sameFixture(const ManagedMatchSnapshot& a, const ManagedMatchSnapshot& b)
{
  return a.date == b.date && a.home_id == b.home_id && a.away_id == b.away_id;
}
}  // namespace

void CareerGuidance::addSnapshot(ManagedMatchSnapshot snapshot)
{
  if (const auto found = std::ranges::find_if(
          snapshots, [&snapshot](const ManagedMatchSnapshot& existing)
          { return sameFixture(existing, snapshot); });
      found != snapshots.end())
  {
    *found = std::move(snapshot);
    unsaved_from = std::min(
        unsaved_from, static_cast<std::size_t>(found - snapshots.begin()));
    return;
  }
  snapshots.push_back(std::move(snapshot));
  if (snapshots.size() > MAX_SNAPSHOTS)
  {
    const std::size_t dropped = snapshots.size() - MAX_SNAPSHOTS;
    snapshots.erase(snapshots.begin(),
                    snapshots.begin() + static_cast<std::ptrdiff_t>(dropped));
    unsaved_from = unsaved_from > dropped ? unsaved_from - dropped : 0;
    pruned = true;
  }
}

void CareerGuidance::load(const std::shared_ptr<DatabaseConnection>& db_conn)
{
  *this = CareerGuidance();
  forEachRow(*db_conn,
             "SELECT onboarding_done, onboarding_dismissed FROM GuidanceState "
             "WHERE id = 1;",
             [this](sqlite3_stmt* stmt)
             {
               onboarding.restore(
                   static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 0)),
                   sqlite3_column_int(stmt, 1) != 0);
               stored = true;
             });
  if (!stored)
  {
    // A career saved before the checklist existed is already under way.
    forEachRow(*db_conn, "SELECT EXISTS (SELECT 1 FROM MatchReports);",
               [this](sqlite3_stmt* stmt)
               {
                 if (sqlite3_column_int(stmt, 0) != 0) onboarding.dismiss();
               });
  }
  forEachRow(
      *db_conn, "SELECT duty, owner FROM DelegatedDuties;",
      [this](sqlite3_stmt* stmt)
      {
        const int duty = sqlite3_column_int(stmt, 0);
        const int owner = sqlite3_column_int(stmt, 1);
        if (duty < 0 || duty >= static_cast<int>(DUTY_COUNT) || owner < 0 ||
            owner > static_cast<int>(std::to_underlying(DutyOwner::Assistant)))
          return;  // Written by a newer version.
        delegation.set(static_cast<Duty>(duty), static_cast<DutyOwner>(owner));
      });
  std::vector<OppositionOrder> orders;
  forEachRow(
      *db_conn,
      "SELECT opponent_id, player_id, instruction FROM OppositionInstructions;",
      [&orders](sqlite3_stmt* stmt)
      {
        const int instruction = sqlite3_column_int(stmt, 2);
        if (instruction <= 0 ||
            instruction >= static_cast<int>(std::to_underlying(
                               OppositionInstruction::COUNT)))
          return;
        orders.push_back({static_cast<TeamID>(sqlite3_column_int(stmt, 0)),
                          static_cast<PlayerID>(sqlite3_column_int64(stmt, 1)),
                          static_cast<OppositionInstruction>(instruction)});
      });
  opposition.restore(std::move(orders));
  forEachRow(
      *db_conn,
      "SELECT data, detail FROM ManagedMatchAnalytics ORDER BY "
      "game_date, home_id, away_id;",
      [this](sqlite3_stmt* stmt)
      {
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        if (text == nullptr) return;
        auto snapshot =
            ManagedMatchSnapshot::fromJson(reinterpret_cast<const char*>(text));
        if (!snapshot) return;
        const auto* blob =
            static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt, 1));
        if (const int bytes = sqlite3_column_bytes(stmt, 1);
            blob != nullptr && bytes > 0)
        {
          // A damaged blob only loses the detail, not the snapshot.
          if (auto detail = MatchDetail::decode(
                  std::span(blob, static_cast<std::size_t>(bytes))))
            snapshot->detail = std::move(*detail);
        }
        snapshots.push_back(std::move(*snapshot));
      });
  if (snapshots.size() > MAX_SNAPSHOTS)
  {
    snapshots.erase(
        snapshots.begin(),
        snapshots.end() - static_cast<std::ptrdiff_t>(MAX_SNAPSHOTS));
    pruned = true;
  }
  unsaved_from = snapshots.size();
}

void CareerGuidance::save(
    const std::shared_ptr<DatabaseConnection>& db_conn) const
{
  const DatabaseConnection& db = *db_conn;
  execute(db, "DELETE FROM GuidanceState;");
  sqlite3_stmt* stmt = db.prepareStatement(
      "INSERT INTO GuidanceState (id, onboarding_done, onboarding_dismissed) "
      "VALUES (1, ?, ?);");
  sqlite3_bind_int64(stmt, 1, onboarding.doneMask());
  sqlite3_bind_int(stmt, 2, onboarding.isDismissed() ? 1 : 0);
  db.executeStep(stmt);
  sqlite3_finalize(stmt);

  execute(db, "DELETE FROM DelegatedDuties;");
  stmt = db.prepareStatement(
      "INSERT INTO DelegatedDuties (duty, owner) VALUES (?, ?);");
  for (std::size_t duty = 0; duty < DUTY_COUNT; ++duty)
  {
    sqlite3_bind_int(stmt, 1, static_cast<int>(duty));
    sqlite3_bind_int(stmt, 2,
                     static_cast<int>(std::to_underlying(
                         delegation.owner(static_cast<Duty>(duty)))));
    db.executeStep(stmt);
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);

  execute(db, "DELETE FROM OppositionInstructions;");
  stmt = db.prepareStatement(
      "INSERT INTO OppositionInstructions (opponent_id, player_id, "
      "instruction) VALUES (?, ?, ?);");
  for (const OppositionOrder& order : opposition.all())
  {
    sqlite3_bind_int(stmt, 1, order.opponent);
    sqlite3_bind_int64(stmt, 2, order.player);
    sqlite3_bind_int(stmt, 3,
                     static_cast<int>(std::to_underlying(order.instruction)));
    db.executeStep(stmt);
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);

  if (pruned && !snapshots.empty())
  {
    stmt = db.prepareStatement(
        "DELETE FROM ManagedMatchAnalytics WHERE game_date < ?;");
    sqlite3_bind_int(stmt, 1, dateToInt(snapshots.front().date));
    db.executeStep(stmt);
    sqlite3_finalize(stmt);
  }
  stmt = db.prepareStatement(
      "INSERT OR REPLACE INTO ManagedMatchAnalytics (game_date, home_id, "
      "away_id, data, detail) VALUES (?, ?, ?, ?, ?);");
  for (std::size_t index = unsaved_from; index < snapshots.size(); ++index)
  {
    const ManagedMatchSnapshot& snapshot = snapshots[index];
    const std::string data = snapshot.toJson();
    sqlite3_bind_int(stmt, 1, dateToInt(snapshot.date));
    sqlite3_bind_int(stmt, 2, snapshot.home_id);
    sqlite3_bind_int(stmt, 3, snapshot.away_id);
    sqlite3_bind_text(stmt, 4, data.c_str(), -1, SQLITE_TRANSIENT);
    if (snapshot.detail.empty())
    {
      sqlite3_bind_null(stmt, 5);
    }
    else
    {
      const std::vector<std::uint8_t> detail = snapshot.detail.encode();
      sqlite3_bind_blob(stmt, 5, detail.data(), static_cast<int>(detail.size()),
                        SQLITE_TRANSIENT);
    }
    db.executeStep(stmt);
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
}

void CareerGuidance::onSaved()
{
  unsaved_from = snapshots.size();
  pruned = false;
  stored = true;
}
