// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <sqlite3.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "database/database_exception.h"
#include "global/build_info.h"

class DatabaseConnection;

/** Versions recorded in save_meta by every save (contracts/save-format.md). */
namespace SaveFormat
{
inline constexpr std::string_view FORMAT_ID = "open-football-career";
inline const std::string_view GAME_VERSION = BuildInfo::version();
inline constexpr std::string_view ENGINE_VERSION = "1.0.0-player12.1";
inline constexpr std::string_view RULES_EDITION = "2026-27";
inline constexpr int RNG_VERSION = 2;  // 2: platform-independent distributions
inline constexpr int SIM_VERSION = 1;
}  // namespace SaveFormat

/**
 * Ordered, numbered and transactional schema migrations of a career save.
 *
 * Every save records the migrations applied to it in `schema_migrations` and
 * its schema version in `save_meta`. Opening a save applies the pending
 * migrations in number order, each in one transaction together with its
 * `schema_migrations` row. See src/database/migrations/README.md for the
 * procedure to add one.
 */
namespace Migrations
{
/** A column added to an existing table when missing. */
struct ColumnSpec
{
  std::string_view table;
  std::string_view column;
  std::string_view definition;  ///< Type and constraints, e.g. "INTEGER".
};

/** What a migration may need to know about the database it upgrades. */
struct MigrationContext
{
  sqlite3* db = nullptr;
  /** The database had no tables at all: a new career is being created. */
  bool fresh = false;
};

struct Migration
{
  int number = 0;
  std::string_view name;
  /** Added only to tables that exist and lack them (explicit checks). */
  std::span<const ColumnSpec> columns;
  /** Optional data step, run after the columns; must be idempotent. */
  void (*apply)(const MigrationContext&) = nullptr;
};

/** Every migration of the game, ordered by number. */
std::span<const Migration> registry();

/** Highest migration number the game knows (the supported save version). */
int currentSchemaVersion();

/** Checksum of a migration's declarative content (stored per save). */
std::string checksum(const Migration& migration);

/**
 * Schema version recorded in a database: save_meta.schema_version, or the
 * highest schema_migrations number, or 0 for saves from before versioning.
 * Read-only; works on connections opened with SQLITE_OPEN_READONLY.
 */
int readSchemaVersion(sqlite3* db);

/** A save written by a newer game than this one. Nothing was modified. */
class FutureVersionError : public DatabaseException
{
 public:
  FutureVersionError(int found, int supported);
  int found;
  int supported;
};

struct MigrationReport
{
  int from_version = 0;
  int to_version = 0;
  bool fresh = false;
  /** Migrations applied now, in order. */
  std::vector<int> applied;
  /**
   * Already recorded migrations whose columns were missing and re-added
   * (hand-edited or partially copied saves).
   */
  std::vector<int> repaired;
};

/**
 * Brings a database to the current schema: rejects future versions before
 * any write, creates missing tables from schema.sql, applies pending
 * migrations in order (one transaction each) and repairs recorded ones whose
 * columns are missing. A failing migration rolls back, leaves the database
 * at the previous version and throws DatabaseException.
 */
MigrationReport migrate(const DatabaseConnection& connection,
                        std::span<const Migration> migrations = registry());

/** True when @p table exists. */
bool tableExists(sqlite3* db, std::string_view table);

/** True when @p table exists and has @p column. */
bool columnExists(sqlite3* db, std::string_view table, std::string_view column);

/**
 * Foreign key violations, ignoring the free-agents pseudo club (league -1),
 * which is a known sentinel until the sentinel repair migration lands.
 */
int foreignKeyIssues(sqlite3* db);
}  // namespace Migrations
