// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/squad_status.h"

#include <sqlite3.h>

#include <algorithm>
#include <utility>

#include "database/database_connection.h"
#include "model/world_simulation.h"

namespace
{
/** Status levels a player accepts below his standing without complaint. */
constexpr int TOLERATED_LEVELS = 1;
constexpr float MORALE_PER_LEVEL = 4.0f;

int level(SquadStatus status)
{
  // Prospects and backups both sit at the bottom of the pecking order.
  return status == SquadStatus::Prospect
             ? static_cast<int>(std::to_underlying(SquadStatus::Backup))
             : static_cast<int>(std::to_underlying(status));
}
}  // namespace

const char* SquadStatusModel::nameKey(SquadStatus status)
{
  using enum SquadStatus;
  switch (status)
  {
    case Star:
      return "SQUAD_STATUS_STAR";
    case Important:
      return "SQUAD_STATUS_IMPORTANT";
    case Regular:
      return "SQUAD_STATUS_REGULAR";
    case Rotation:
      return "SQUAD_STATUS_ROTATION";
    case Backup:
      return "SQUAD_STATUS_BACKUP";
    case Prospect:
      return "SQUAD_STATUS_PROSPECT";
    case COUNT:
      break;
  }
  return "SQUAD_STATUS_REGULAR";
}

const char* SquadStatusModel::expectationKey(SquadStatus status)
{
  using enum SquadStatus;
  switch (status)
  {
    case Star:
      return "SQUAD_STATUS_STAR_HELP";
    case Important:
      return "SQUAD_STATUS_IMPORTANT_HELP";
    case Regular:
      return "SQUAD_STATUS_REGULAR_HELP";
    case Rotation:
      return "SQUAD_STATUS_ROTATION_HELP";
    case Backup:
      return "SQUAD_STATUS_BACKUP_HELP";
    case Prospect:
      return "SQUAD_STATUS_PROSPECT_HELP";
    case COUNT:
      break;
  }
  return "SQUAD_STATUS_REGULAR_HELP";
}

SquadRole SquadStatusModel::toSquadRole(SquadStatus status)
{
  using enum SquadStatus;
  switch (status)
  {
    case Star:
      return SquadRole::KeyPlayer;
    case Important:
    case Regular:
      return SquadRole::FirstTeam;
    case Rotation:
      return SquadRole::Rotation;
    case Backup:
      return SquadRole::Backup;
    case Prospect:
    case COUNT:
      break;
  }
  return SquadRole::Fringe;
}

SquadStatus SquadStatusModel::expectation(SquadStatus assigned,
                                          SquadStatus deserved)
{
  const int floor = level(deserved) + TOLERATED_LEVELS;
  if (level(assigned) <= floor) return assigned;
  // A prospect who deserves little keeps his own, modest expectation.
  return static_cast<SquadStatus>(std::min(
      floor, static_cast<int>(std::to_underlying(SquadStatus::Backup))));
}

SquadStatus SquadStatusModel::deserved(std::size_t rank, int age)
{
  if (rank < 3) return SquadStatus::Star;
  if (rank < 6) return SquadStatus::Important;
  if (rank < 11) return SquadStatus::Regular;
  if (rank < 16) return SquadStatus::Rotation;
  return age <= PROSPECT_MAX_AGE ? SquadStatus::Prospect : SquadStatus::Backup;
}

float SquadStatusModel::moraleOffset(SquadStatus assigned, SquadStatus deserved,
                                     float ambition)
{
  const int gap = level(assigned) - level(deserved) - TOLERATED_LEVELS;
  if (gap <= 0) return 0.0f;
  return -MORALE_PER_LEVEL * static_cast<float>(gap) *
         std::clamp(ambition, 0.75f, 1.5f);
}

void SquadStatusBook::set(PlayerID player_id, TeamID team_id,
                          std::optional<SquadStatus> status)
{
  if (!status || *status >= SquadStatus::COUNT)
  {
    entries.erase(player_id);
    return;
  }
  entries[player_id] = Entry{team_id, *status};
}

std::optional<SquadStatus> SquadStatusBook::get(PlayerID player_id,
                                                TeamID current_team_id) const
{
  const auto found = entries.find(player_id);
  if (found == entries.end() || found->second.team_id != current_team_id)
    return std::nullopt;
  return found->second.status;
}

void SquadStatusBook::load(const DatabaseConnection& db_conn)
{
  entries.clear();
  sqlite3_stmt* stmt = db_conn.prepareStatement(
      "SELECT player_id, team_id, status FROM SquadStatuses;");
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    const auto status = sqlite3_column_int(stmt, 2);
    if (status < 0 ||
        status >= static_cast<int>(std::to_underlying(SquadStatus::COUNT)))
      continue;
    entries[static_cast<PlayerID>(sqlite3_column_int64(stmt, 0))] =
        Entry{static_cast<TeamID>(sqlite3_column_int(stmt, 1)),
              static_cast<SquadStatus>(status)};
  }
  sqlite3_finalize(stmt);
}

void SquadStatusBook::save(const DatabaseConnection& db_conn) const
{
  sqlite3_exec(db_conn.getRaw(), "DELETE FROM SquadStatuses;", nullptr, nullptr,
               nullptr);
  sqlite3_stmt* stmt = db_conn.prepareStatement(
      "INSERT INTO SquadStatuses (player_id, team_id, status) VALUES (?, ?, "
      "?);");
  for (const auto& [player_id, entry] : entries)
  {
    sqlite3_bind_int64(stmt, 1, player_id);
    sqlite3_bind_int(stmt, 2, entry.team_id);
    sqlite3_bind_int(stmt, 3,
                     static_cast<int>(std::to_underlying(entry.status)));
    db_conn.executeStep(stmt);
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
}
