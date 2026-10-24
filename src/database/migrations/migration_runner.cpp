// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>

#include "database/database_connection.h"
#include "database/migrations/migrations.h"
#include "global/logger.h"

namespace
{
// Tables owned by the migration runner itself (also in assets/db/schema.sql).
constexpr const char* VERSION_TABLES_SQL =
    "CREATE TABLE IF NOT EXISTS schema_migrations ("
    "number INTEGER PRIMARY KEY, name TEXT NOT NULL, "
    "applied_at_game_version TEXT NOT NULL, checksum TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS save_meta ("
    "id INTEGER PRIMARY KEY CHECK (id = 1), format_id TEXT NOT NULL, "
    "schema_version INTEGER NOT NULL, min_reader_version INTEGER NOT NULL, "
    "game_version TEXT NOT NULL DEFAULT '', "
    "engine_version TEXT NOT NULL DEFAULT '', "
    "rules_edition TEXT NOT NULL DEFAULT '', "
    "rng_version INTEGER NOT NULL DEFAULT 0, "
    "sim_version INTEGER NOT NULL DEFAULT 0, "
    "world_seed INTEGER NOT NULL DEFAULT 0, "
    "seed_unknown INTEGER NOT NULL DEFAULT 0, "
    "tuning_profiles TEXT NOT NULL DEFAULT '[]', "
    "packs TEXT NOT NULL DEFAULT '[]', "
    "created_at_utc TEXT NOT NULL DEFAULT '', "
    "updated_at_utc TEXT NOT NULL DEFAULT '', "
    "edited INTEGER NOT NULL DEFAULT 0, "
    "last_saved_game_date TEXT NOT NULL DEFAULT '', "
    "playtime_seconds INTEGER NOT NULL DEFAULT 0);";

void exec(sqlite3* db, const std::string& sql)
{
  char* error = nullptr;
  if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK)
  {
    std::string message = error ? error : sqlite3_errmsg(db);
    sqlite3_free(error);
    throw DatabaseException(message);
  }
}

int queryInt(sqlite3* db, const char* sql, int fallback)
{
  sqlite3_stmt* stmt = nullptr;
  int value = fallback;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK &&
      sqlite3_step(stmt) == SQLITE_ROW &&
      sqlite3_column_type(stmt, 0) != SQLITE_NULL)
    value = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  return value;
}

bool hasUserTables(sqlite3* db)
{
  return queryInt(db,
                  "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND "
                  "name NOT LIKE 'sqlite_%';",
                  0) > 0;
}

/** Adds the missing columns; returns how many were added. */
int addMissingColumns(sqlite3* db, const Migrations::Migration& migration)
{
  int added = 0;
  for (const Migrations::ColumnSpec& spec : migration.columns)
  {
    if (!Migrations::tableExists(db, spec.table) ||
        Migrations::columnExists(db, spec.table, spec.column))
      continue;
    exec(db, std::format(R"(ALTER TABLE "{}" ADD COLUMN "{}" {};)", spec.table,
                         spec.column, spec.definition));
    ++added;
  }
  return added;
}

bool columnsMissing(sqlite3* db, const Migrations::Migration& migration)
{
  return std::ranges::any_of(migration.columns,
                             [db](const Migrations::ColumnSpec& spec)
                             {
                               return Migrations::tableExists(db, spec.table) &&
                                      !Migrations::columnExists(db, spec.table,
                                                                spec.column);
                             });
}

/** Runs @p body in BEGIN IMMEDIATE ... COMMIT, rolling back on failure. */
template <typename Body>
void inTransaction(sqlite3* db, Body&& body)
{
  exec(db, "BEGIN IMMEDIATE;");
  try
  {
    body();
    exec(db, "COMMIT;");
  }
  catch (...)
  {
    sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
    throw;
  }
}

void recordMigration(sqlite3* db, const Migrations::Migration& migration)
{
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db,
                         "INSERT OR REPLACE INTO schema_migrations (number, "
                         "name, applied_at_game_version, checksum) VALUES (?, "
                         "?, ?, ?);",
                         -1, &stmt, nullptr) != SQLITE_OK)
    throw DatabaseException(sqlite3_errmsg(db));
  const std::string sum = Migrations::checksum(migration);
  sqlite3_bind_int(stmt, 1, migration.number);
  sqlite3_bind_text(stmt, 2, migration.name.data(),
                    static_cast<int>(migration.name.size()), SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, SaveFormat::GAME_VERSION.data(),
                    static_cast<int>(SaveFormat::GAME_VERSION.size()),
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 4, sum.c_str(), -1, SQLITE_TRANSIENT);
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) throw DatabaseException(sqlite3_errmsg(db));
  exec(db, std::format("UPDATE save_meta SET schema_version = {0}, "
                       "min_reader_version = {0} WHERE id = 1;",
                       migration.number));
}
}  // namespace

Migrations::FutureVersionError::FutureVersionError(int found_version,
                                                   int supported_version)
    : DatabaseException(std::format(
          "Save schema version {} is newer than the supported version {}; "
          "update the game to load it. The save was not modified.",
          found_version, supported_version)),
      found(found_version),
      supported(supported_version)
{
}

bool Migrations::tableExists(sqlite3* db, std::string_view table)
{
  sqlite3_stmt* stmt = nullptr;
  bool found = false;
  if (sqlite3_prepare_v2(db,
                         "SELECT 1 FROM sqlite_master WHERE type = 'table' AND "
                         "name = ?;",
                         -1, &stmt, nullptr) == SQLITE_OK)
  {
    sqlite3_bind_text(stmt, 1, table.data(), static_cast<int>(table.size()),
                      SQLITE_STATIC);
    found = sqlite3_step(stmt) == SQLITE_ROW;
  }
  sqlite3_finalize(stmt);
  return found;
}

bool Migrations::columnExists(sqlite3* db, std::string_view table,
                              std::string_view column)
{
  sqlite3_stmt* stmt = nullptr;
  bool found = false;
  if (sqlite3_prepare_v2(db,
                         "SELECT 1 FROM pragma_table_info(?) WHERE name = ?;",
                         -1, &stmt, nullptr) == SQLITE_OK)
  {
    sqlite3_bind_text(stmt, 1, table.data(), static_cast<int>(table.size()),
                      SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, column.data(), static_cast<int>(column.size()),
                      SQLITE_STATIC);
    found = sqlite3_step(stmt) == SQLITE_ROW;
  }
  sqlite3_finalize(stmt);
  return found;
}

std::string Migrations::checksum(const Migration& migration)
{
  // FNV-1a over the migration's declarative content.
  std::uint64_t hash = 0xCBF29CE484222325ULL;
  const auto mix = [&hash](std::string_view text)
  {
    for (const char c : text)
    {
      hash ^= static_cast<unsigned char>(c);
      hash *= 0x100000001B3ULL;
    }
    hash ^= 0xFF;
    hash *= 0x100000001B3ULL;
  };
  mix(migration.name);
  for (const ColumnSpec& spec : migration.columns)
  {
    mix(spec.table);
    mix(spec.column);
    mix(spec.definition);
  }
  return std::format("fnv1a:{:016x}", hash);
}

int Migrations::readSchemaVersion(sqlite3* db)
{
  int version = 0;
  if (tableExists(db, "save_meta"))
    version =
        queryInt(db, "SELECT schema_version FROM save_meta WHERE id = 1;", 0);
  if (tableExists(db, "schema_migrations"))
    version = std::max(
        version, queryInt(db, "SELECT MAX(number) FROM schema_migrations;", 0));
  return version;
}

int Migrations::foreignKeyIssues(sqlite3* db)
{
  sqlite3_stmt* stmt = nullptr;
  int issues = 0;
  if (sqlite3_prepare_v2(db, "PRAGMA foreign_key_check;", -1, &stmt, nullptr) !=
      SQLITE_OK)
    return 0;
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    const auto* table =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const bool free_agents_sentinel = table != nullptr &&
                                      std::string_view(table) == "Teams" &&
                                      sqlite3_column_int64(stmt, 1) == 0;
    if (!free_agents_sentinel) ++issues;
  }
  sqlite3_finalize(stmt);
  return issues;
}

Migrations::MigrationReport Migrations::migrate(
    const DatabaseConnection& connection, std::span<const Migration> migrations)
{
  sqlite3* db = connection.getRaw();
  MigrationReport report;
  const int supported = migrations.empty() ? 0 : migrations.back().number;
  report.from_version = readSchemaVersion(db);
  if (report.from_version > supported)
    throw FutureVersionError(report.from_version, supported);

  report.fresh = !hasUserTables(db);
  // Tables introduced after the save was written are created with their
  // full definition (schema.sql is idempotent and must run outside a
  // transaction: it sets the journal mode).
  connection.initialize();
  inTransaction(
      db,
      [db, &report]
      {
        exec(db, VERSION_TABLES_SQL);
        exec(db, std::format("INSERT OR IGNORE INTO save_meta (id, format_id, "
                             "schema_version, min_reader_version) VALUES "
                             "(1, '{}', {}, {});",
                             SaveFormat::FORMAT_ID, report.from_version,
                             report.from_version));
      });

  const MigrationContext context{db, report.fresh};
  for (const Migration& migration : migrations)
  {
    if (migration.number <= report.from_version)
    {
      // Recorded as applied but columns are missing (hand-edited saves):
      // columns are re-added, data steps are not repeated.
      if (!columnsMissing(db, migration)) continue;
      inTransaction(db, [db, &migration] { addMissingColumns(db, migration); });
      report.repaired.push_back(migration.number);
      Logger::warn(
          std::format("Save schema repaired: migration {} re-added "
                      "missing columns",
                      migration.name));
      continue;
    }
    try
    {
      inTransaction(db,
                    [db, &migration, &context]
                    {
                      addMissingColumns(db, migration);
                      if (migration.apply) migration.apply(context);
                      recordMigration(db, migration);
                    });
    }
    catch (const std::exception& error)
    {
      throw DatabaseException(
          std::format("Migration {} failed: {}", migration.name, error.what()));
    }
    report.applied.push_back(migration.number);
  }
  report.to_version = std::max(report.from_version, supported);
  if (!report.applied.empty())
    Logger::info(std::format("Save schema migrated from version {} to {}",
                             report.from_version, report.to_version));
  return report;
}
