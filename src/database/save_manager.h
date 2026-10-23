// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "database/database_exception.h"
#include "model/gamedate.h"

class DatabaseConnection;

/** Health of a save file as seen by a load screen. */
enum class SaveStatus : std::uint8_t
{
  Ok,
  Missing,
  Incomplete,     ///< No game state: the game stopped while creating it.
  Corrupt,        ///< Damaged file (integrity check failed / not a database).
  FutureVersion,  ///< Written by a newer game; left untouched.
};

/** Why a save or load failed. */
enum class SaveErrorKind : std::uint8_t
{
  None,
  Missing,
  Incomplete,
  Corrupt,
  FutureVersion,
  DiskFull,
  PermissionDenied,
  Io,
};

struct SaveError
{
  SaveErrorKind kind = SaveErrorKind::None;
  std::string detail;  ///< Technical detail for logs (not localized).
  /** For FutureVersion: the version found and the supported one. */
  int found_version = 0;
  int supported_version = 0;

  /** Localized message key (both lang files), e.g. "SAVE_ERROR_CORRUPT". */
  const char* langKey() const;
};

/** Thrown by SaveManager when a file operation fails. */
class SaveFailure : public DatabaseException
{
 public:
  explicit SaveFailure(SaveError failure);
  SaveError error;
};

/** Contents of save_meta (contracts/save-format.md). */
struct SaveMetadata
{
  int schema_version = 0;
  std::string game_version;
  std::string engine_version;
  std::string rules_edition;
  int rng_version = 0;
  int sim_version = 0;
  std::uint64_t world_seed = 0;
  bool seed_unknown = false;
  std::string created_at_utc;  ///< ISO-8601, empty for legacy saves.
  std::string updated_at_utc;  ///< ISO-8601 of the last save.
  std::string last_saved_game_date;
  std::int64_t playtime_seconds = 0;
};

/** How thoroughly SaveManager::inspect checks a file. */
enum class SaveCheck : std::uint8_t
{
  Quick,  ///< PRAGMA quick_check (load screens).
  Full,   ///< PRAGMA integrity_check + foreign_key_check (before loading).
};

/** Everything a load screen shows about a save file, read-only. */
struct SaveInspection
{
  SaveStatus status = SaveStatus::Missing;
  std::string detail;  ///< Why it is not Ok (technical).
  int schema_version = 0;
  int supported_version = 0;
  bool has_metadata = false;  ///< False for saves from before versioning.
  SaveMetadata metadata;
  int managed_team_id = -1;
  std::string club_name;  ///< Empty while no club is managed.
  std::string game_date;
  int season = 0;
  /** Foreign key violations (diagnostic, not a load blocker). */
  int foreign_key_issues = 0;
};

/** A restorable copy of a slot. */
struct SaveBackup
{
  enum class Kind : std::uint8_t
  {
    Rotation,     ///< Previous save; index 1 is the newest.
    PreMigration  ///< The file as it was before a schema upgrade.
  };
  Kind kind = Kind::Rotation;
  int index = 0;  ///< Rotation index, or the schema version for PreMigration.
  std::filesystem::path path;
  SaveInspection inspection;
};

/** Durations of one save, in milliseconds. */
struct SaveTimings
{
  double flush_ms = 0;     ///< In-memory state written to the working DB.
  double snapshot_ms = 0;  ///< Online backup to the temporary file.
  double verify_ms = 0;    ///< quick_check of the temporary file.
  double sync_ms = 0;      ///< fsync, backup rotation and rename.
  double total_ms = 0;
};

/** When the game saves on its own. */
enum class AutosaveFrequency : std::uint8_t
{
  Off,
  Daily,
  Weekly,
  Monthly,
  Matchday,   ///< The day after a match of the managed club.
  SeasonEnd,  ///< When a new season starts.
};

struct AutosavePolicy
{
  AutosaveFrequency frequency = AutosaveFrequency::Weekly;
  /** Previous saves kept per slot (<slot>.bak.1 .. .bak.N), 0-9. */
  int backups = 3;
};

/**
 * Crash-safe persistence of career saves.
 *
 * A career is played on an in-memory working database; the slot file is
 * only replaced by a complete, verified snapshot (online backup to
 * <slot>.tmp, quick_check, fsync, rotate backups, atomic rename, fsync of the
 * directory). An interrupted save therefore never damages the previous one.
 * See src/database/migrations/README.md, "Save protocol".
 */
class SaveManager
{
 public:
  /** Places where tests inject faults (kill, disk full, I/O errors). */
  enum class FaultPoint : std::uint8_t
  {
    MidFlush,         ///< Inside Game::saveGame's transaction.
    TempOpened,       ///< Temporary file opened, before the backup.
    SnapshotWritten,  ///< Backup done, before verification.
    Rotated,          ///< Backups rotated, before the rename.
  };
  using FaultHook = std::function<void(FaultPoint, sqlite3* temp_db)>;

  /** Test hook, called at every FaultPoint (nullptr to disable). */
  static void setFaultHook(FaultHook hook);
  static void fault(FaultPoint point, sqlite3* temp_db = nullptr);

  /** Read-only inspection; never modifies the file. */
  static SaveInspection inspect(const std::filesystem::path& file,
                                SaveCheck check = SaveCheck::Full);

  /**
   * Copies a save into a new in-memory working database (online backup;
   * content of a leftover -wal file from a crash is included).
   * @throws SaveFailure when the file cannot be read.
   */
  static std::shared_ptr<DatabaseConnection> openWorkingCopy(
      const std::filesystem::path& file);

  /** A new, empty in-memory working database. */
  static std::shared_ptr<DatabaseConnection> createWorkingCopy();

  /**
   * Writes the provenance of this save into save_meta of @p working,
   * including the game, engine, RNG and simulation versions of this build.
   */
  static void stampMetadata(const DatabaseConnection& working,
                            std::uint64_t world_seed,
                            const GameDateValue& game_date,
                            std::int64_t playtime_seconds);

  /**
   * Atomically replaces @p file with a snapshot of @p working, keeping
   * @p backups previous versions. On failure the previous file and its
   * backups are unchanged.
   * @throws SaveFailure (DiskFull, PermissionDenied, Io).
   */
  static SaveTimings persist(const DatabaseConnection& working,
                             const std::filesystem::path& file, int backups);

  /** Keeps a copy of a save before its schema is upgraded (once). */
  static void preserveBeforeMigration(const std::filesystem::path& file,
                                      int schema_version);

  /** Number of rotation backups of a slot (no file is opened). */
  static int countBackups(const std::filesystem::path& file);

  /** Rotation and pre-migration backups of a slot, newest first. */
  static std::vector<SaveBackup> listBackups(const std::filesystem::path& file);

  /**
   * The newest backup of @p backups (as listed by listBackups) that loads,
   * or nullptr: what a recovery dialog offers first.
   */
  static const SaveBackup* newestUsable(const std::vector<SaveBackup>& backups);

  /**
   * Replaces @p file with @p backup (verified first). The replaced file is
   * kept as <file>.corrupt-<utc> or <file>.replaced-<utc>, never deleted.
   * @throws SaveFailure.
   */
  static void restoreBackup(const std::filesystem::path& file,
                            const std::filesystem::path& backup);

  /** Removes a save with its sidecars, temporary file and backups. */
  static void deleteSave(const std::filesystem::path& file);

  /** Whether an autosave is due today. */
  static bool isAutosaveDue(AutosaveFrequency frequency,
                            const GameDateValue& last_autosave,
                            int last_autosave_season,
                            const GameDateValue& today, int season,
                            bool managed_match_yesterday);

  static std::filesystem::path backupPath(const std::filesystem::path& file,
                                          int index);
  static std::filesystem::path tempPath(const std::filesystem::path& file);
};
