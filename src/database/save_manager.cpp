// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "database/save_manager.h"

#include <sqlite3.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

#include "database/database_connection.h"
#include "database/migrations/migrations.h"
#include "global/logger.h"
#include "model/world_rng.h"

namespace fs = std::filesystem;

namespace
{
SaveManager::FaultHook& faultHook()
{
  static SaveManager::FaultHook hook;
  return hook;
}

constexpr int MAX_BACKUPS = 9;

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start)
{
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
      .count();
}

std::string utcNow(std::string_view format)
{
  const auto now = std::chrono::floor<std::chrono::seconds>(
      std::chrono::system_clock::now());
  return std::vformat(format, std::make_format_args(now));
}

SaveErrorKind kindFromErrno(int error)
{
  using enum SaveErrorKind;
  switch (error)
  {
    case ENOSPC:
#if defined(EDQUOT)
    case EDQUOT:
#endif
      return DiskFull;
    case EACCES:
    case EPERM:
    case EROFS:
      return PermissionDenied;
    default:
      return Io;
  }
}

/** Classifies a failed SQLite call on @p db. */
SaveFailure sqliteFailure(sqlite3* db, int rc, std::string_view what)
{
  using enum SaveErrorKind;
  SaveError error;
  const int system_error = db ? sqlite3_system_errno(db) : 0;
  if (const int primary = rc & 0xFF; primary == SQLITE_FULL)
    error.kind = DiskFull;
  else if (system_error != 0 && kindFromErrno(system_error) != Io)
    error.kind = kindFromErrno(system_error);
  else if (primary == SQLITE_READONLY || primary == SQLITE_PERM ||
           primary == SQLITE_AUTH)
    error.kind = PermissionDenied;
  else if (primary == SQLITE_CORRUPT || primary == SQLITE_NOTADB)
    error.kind = Corrupt;
  else
    error.kind = Io;
  error.detail =
      std::format("{}: {}", what, db ? sqlite3_errmsg(db) : sqlite3_errstr(rc));
  return SaveFailure(std::move(error));
}

SaveFailure fileFailure(const std::error_code& code, std::string_view what)
{
  SaveError error;
  error.kind = code.category() == std::generic_category() ||
                       code.category() == std::system_category()
                   ? kindFromErrno(code.value())
                   : SaveErrorKind::Io;
  error.detail = std::format("{}: {}", what, code.message());
  return SaveFailure(std::move(error));
}

/** Owns a raw sqlite3 handle. */
class Handle
{
 public:
  Handle() = default;
  ~Handle() { close(); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;

  int open(const fs::path& path, int flags)
  {
    close();
    const int rc = sqlite3_open_v2(path.string().c_str(), &db, flags, nullptr);
    if (rc == SQLITE_OK) sqlite3_extended_result_codes(db, 1);
    return rc;
  }
  void close()
  {
    if (db) sqlite3_close(db);
    db = nullptr;
  }
  sqlite3* get() const { return db; }

 private:
  sqlite3* db = nullptr;
};

void removeDatabaseFiles(const fs::path& path)
{
  std::error_code ignored;
  for (const char* suffix : {"", "-wal", "-shm", "-journal"})
    fs::remove(path.string() + suffix, ignored);
}

/** First row of an integrity pragma is "ok" (and prepared at all). */
bool checkIntegrity(sqlite3* db, const char* pragma, std::string& detail)
{
  sqlite3_stmt* stmt = nullptr;
  if (const int rc = sqlite3_prepare_v2(db, pragma, -1, &stmt, nullptr);
      rc != SQLITE_OK)
  {
    detail = sqlite3_errmsg(db);
    sqlite3_finalize(stmt);
    return false;
  }
  bool ok = false;
  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    const auto* text =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    ok = text != nullptr && std::string_view(text) == "ok";
    if (!ok) detail = text ? text : sqlite3_errmsg(db);
  }
  else
  {
    detail = sqlite3_errmsg(db);
  }
  sqlite3_finalize(stmt);
  return ok;
}

void syncFile(const fs::path& path, bool directory)
{
#if defined(_WIN32)
  // SQLite already flushed the file when committing the backup; Windows
  // has no directory fsync.
  (void)path;
  (void)directory;
#else
  const int fd =
      ::open(path.c_str(), directory ? O_RDONLY | O_DIRECTORY : O_RDONLY);
  if (fd < 0) return;
  ::fsync(fd);
  ::close(fd);
#endif
}

/**
 * Online backup of @p source into a new file @p target in rollback-journal
 * mode (self-contained: no -wal sidecar), then quick_check and fsync.
 */
void writeSnapshot(sqlite3* source, const fs::path& target,
                   SaveTimings* timings)
{
  const auto started = Clock::now();
  removeDatabaseFiles(target);
  // SQLite reports a failed open without the OS error: probe it first so a
  // read-only folder or a full disk is reported as such.
  errno = 0;
  if (std::FILE* probe = std::fopen(target.string().c_str(), "wb"))
    std::fclose(probe);
  else
    throw fileFailure(std::error_code(errno, std::generic_category()),
                      "create temporary save");
  Handle destination;
  if (const int rc =
          destination.open(target, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
      rc != SQLITE_OK)
    throw sqliteFailure(destination.get(), rc, "create temporary save");
  SaveManager::fault(SaveManager::FaultPoint::TempOpened, destination.get());

  sqlite3_backup* backup =
      sqlite3_backup_init(destination.get(), "main", source, "main");
  if (!backup)
    throw sqliteFailure(destination.get(), sqlite3_errcode(destination.get()),
                        "start snapshot");
  const int step = sqlite3_backup_step(backup, -1);
  sqlite3_backup_finish(backup);
  if (step != SQLITE_DONE)
    throw sqliteFailure(destination.get(), step, "write snapshot");
  if (const int rc =
          sqlite3_exec(destination.get(), "PRAGMA journal_mode=DELETE;",
                       nullptr, nullptr, nullptr);
      rc != SQLITE_OK)
    throw sqliteFailure(destination.get(), rc, "finish snapshot");
  destination.close();
  SaveManager::fault(SaveManager::FaultPoint::SnapshotWritten);
  if (timings) timings->snapshot_ms = millisecondsSince(started);

  const auto verifying = Clock::now();
  Handle check;
  if (std::string detail;
      check.open(target, SQLITE_OPEN_READONLY) != SQLITE_OK ||
      !checkIntegrity(check.get(), "PRAGMA quick_check;", detail))
  {
    SaveError error;
    error.kind = SaveErrorKind::Io;
    error.detail = "snapshot verification failed: " + detail;
    throw SaveFailure(std::move(error));
  }
  check.close();
  syncFile(target, false);
  if (timings) timings->verify_ms = millisecondsSince(verifying);
}

void renameOrThrow(const fs::path& from, const fs::path& to,
                   std::string_view what)
{
  std::error_code code;
  fs::rename(from, to, code);
  if (code) throw fileFailure(code, what);
}

/**
 * Saves written while the slot file was the live database may still have
 * a -wal file; fold it in so the file can be linked or copied safely.
 */
void consolidate(const fs::path& file)
{
  if (!fs::exists(file.string() + "-wal")) return;
  Handle db;
  if (const int rc = db.open(file, SQLITE_OPEN_READWRITE); rc != SQLITE_OK)
    throw sqliteFailure(db.get(), rc, "open previous save");
  if (const int rc = sqlite3_exec(db.get(),
                                  "PRAGMA wal_checkpoint(TRUNCATE);"
                                  "PRAGMA journal_mode=DELETE;",
                                  nullptr, nullptr, nullptr);
      rc != SQLITE_OK)
    throw sqliteFailure(db.get(), rc, "checkpoint previous save");
  db.close();
  std::error_code ignored;
  fs::remove(file.string() + "-shm", ignored);
}

/** Shifts <file>.bak.N; the current file becomes .bak.1. */
void rotateBackups(const fs::path& file, int keep)
{
  std::error_code code;
  for (int index = std::max(keep, 1); index <= MAX_BACKUPS; ++index)
  {
    fs::remove(SaveManager::backupPath(file, index), code);
    if (code) throw fileFailure(code, "remove old backup");
  }
  if (keep <= 0 || !fs::exists(file)) return;
  consolidate(file);
  for (int index = keep - 1; index >= 1; --index)
  {
    const fs::path from = SaveManager::backupPath(file, index);
    if (fs::exists(from))
      renameOrThrow(from, SaveManager::backupPath(file, index + 1),
                    "rotate backup");
  }
  // A hard link keeps the slot file in place until the rename replaces it.
  const fs::path newest = SaveManager::backupPath(file, 1);
  fs::create_hard_link(file, newest, code);
  if (code)
  {
    fs::copy_file(file, newest, fs::copy_options::overwrite_existing, code);
    if (code) throw fileFailure(code, "copy previous save");
  }
}

/** Copies a closed save into @p target through a verified temporary file. */
void copyDatabase(const fs::path& source_path, const fs::path& target)
{
  Handle source;
  if (const int rc = source.open(source_path, SQLITE_OPEN_READONLY);
      rc != SQLITE_OK)
    throw sqliteFailure(source.get(), rc, "open backup source");
  const fs::path temp = SaveManager::tempPath(target);
  try
  {
    writeSnapshot(source.get(), temp, nullptr);
    renameOrThrow(temp, target, "install copy");
  }
  catch (...)
  {
    removeDatabaseFiles(temp);
    throw;
  }
  syncFile(target.parent_path(), true);
}

std::string columnText(sqlite3_stmt* stmt, int column)
{
  const auto* text =
      reinterpret_cast<const char*>(sqlite3_column_text(stmt, column));
  return text ? text : "";
}

void readMetadata(sqlite3* db, SaveInspection& inspection)
{
  if (!Migrations::tableExists(db, "save_meta")) return;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(
          db,
          "SELECT schema_version, game_version, engine_version, "
          "rules_edition, rng_version, sim_version, world_seed, seed_unknown, "
          "created_at_utc, updated_at_utc, last_saved_game_date, "
          "playtime_seconds FROM save_meta WHERE id = 1;",
          -1, &stmt, nullptr) == SQLITE_OK &&
      sqlite3_step(stmt) == SQLITE_ROW)
  {
    SaveMetadata& meta = inspection.metadata;
    meta.schema_version = sqlite3_column_int(stmt, 0);
    meta.game_version = columnText(stmt, 1);
    meta.engine_version = columnText(stmt, 2);
    meta.rules_edition = columnText(stmt, 3);
    meta.rng_version = sqlite3_column_int(stmt, 4);
    meta.sim_version = sqlite3_column_int(stmt, 5);
    meta.world_seed = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 6));
    meta.seed_unknown = sqlite3_column_int(stmt, 7) != 0;
    meta.created_at_utc = columnText(stmt, 8);
    meta.updated_at_utc = columnText(stmt, 9);
    meta.last_saved_game_date = columnText(stmt, 10);
    meta.playtime_seconds = sqlite3_column_int64(stmt, 11);
    inspection.has_metadata = true;
  }
  sqlite3_finalize(stmt);
}

/** Club, date and season from GameState; false when there is none. */
bool readGameState(sqlite3* db, SaveInspection& inspection)
{
  sqlite3_stmt* stmt = nullptr;
  bool found = false;
  if (sqlite3_prepare_v2(db,
                         "SELECT managed_team_id, game_date, current_season "
                         "FROM GameState WHERE id = 1;",
                         -1, &stmt, nullptr) == SQLITE_OK &&
      sqlite3_step(stmt) == SQLITE_ROW)
  {
    inspection.managed_team_id = sqlite3_column_int(stmt, 0);
    inspection.game_date = columnText(stmt, 1);
    inspection.season = sqlite3_column_int(stmt, 2);
    found = true;
  }
  sqlite3_finalize(stmt);
  if (!found || inspection.managed_team_id <= 0) return found;

  if (sqlite3_prepare_v2(db, "SELECT name FROM Teams WHERE id = ?;", -1, &stmt,
                         nullptr) == SQLITE_OK)
  {
    sqlite3_bind_int(stmt, 1, inspection.managed_team_id);
    if (sqlite3_step(stmt) == SQLITE_ROW)
      inspection.club_name = columnText(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return true;
}
}  // namespace

const char* SaveError::langKey() const
{
  using enum SaveErrorKind;
  switch (kind)
  {
    case None:
      return "SAVE_STATUS_OK";
    case Missing:
      return "SAVE_ERROR_MISSING";
    case Incomplete:
      return "SAVE_ERROR_INCOMPLETE";
    case Corrupt:
      return "SAVE_ERROR_CORRUPT";
    case FutureVersion:
      return "SAVE_ERROR_FUTURE_VERSION";
    case DiskFull:
      return "SAVE_ERROR_DISK_FULL";
    case PermissionDenied:
      return "SAVE_ERROR_PERMISSION";
    case Io:
      break;
  }
  return "SAVE_ERROR_IO";
}

SaveFailure::SaveFailure(SaveError failure)
    : DatabaseException(failure.detail), error(std::move(failure))
{
}

void SaveManager::setFaultHook(FaultHook hook)
{
  faultHook() = std::move(hook);
}

void SaveManager::fault(FaultPoint point, sqlite3* temp_db)
{
  if (const FaultHook& hook = faultHook()) hook(point, temp_db);
}

fs::path SaveManager::backupPath(const fs::path& file, int index)
{
  return fs::path(std::format("{}.bak.{}", file.string(), index));
}

fs::path SaveManager::tempPath(const fs::path& file)
{
  return fs::path(file.string() + ".tmp");
}

SaveInspection SaveManager::inspect(const fs::path& file, SaveCheck check)
{
  SaveInspection inspection;
  inspection.supported_version = Migrations::currentSchemaVersion();
  if (!fs::exists(file))
  {
    inspection.status = SaveStatus::Missing;
    return inspection;
  }
  inspection.status = SaveStatus::Corrupt;
  Handle db;
  if (db.open(file, SQLITE_OPEN_READONLY) != SQLITE_OK)
  {
    inspection.detail = db.get() ? sqlite3_errmsg(db.get()) : "cannot open";
    return inspection;
  }
  if (!checkIntegrity(db.get(),
                      check == SaveCheck::Full ? "PRAGMA integrity_check;"
                                               : "PRAGMA quick_check;",
                      inspection.detail))
    return inspection;

  inspection.schema_version = Migrations::readSchemaVersion(db.get());
  readMetadata(db.get(), inspection);
  const bool has_state = readGameState(db.get(), inspection);
  if (inspection.schema_version > inspection.supported_version)
  {
    inspection.status = SaveStatus::FutureVersion;
    inspection.detail =
        std::format("schema version {} > supported {}",
                    inspection.schema_version, inspection.supported_version);
    return inspection;
  }
  if (!has_state)
  {
    inspection.status = SaveStatus::Incomplete;
    inspection.detail = "no game state";
    return inspection;
  }
  inspection.status = SaveStatus::Ok;
  inspection.detail.clear();
  if (check == SaveCheck::Full)
    inspection.foreign_key_issues = Migrations::foreignKeyIssues(db.get());
  return inspection;
}

std::shared_ptr<DatabaseConnection> SaveManager::createWorkingCopy()
{
  return std::make_shared<DatabaseConnection>(":memory:");
}

std::shared_ptr<DatabaseConnection> SaveManager::openWorkingCopy(
    const fs::path& file)
{
  Handle source;
  if (const int rc = source.open(file, SQLITE_OPEN_READONLY); rc != SQLITE_OK)
    throw sqliteFailure(source.get(), rc, "open save");
  auto working = createWorkingCopy();
  sqlite3_backup* backup =
      sqlite3_backup_init(working->getRaw(), "main", source.get(), "main");
  if (!backup)
    throw sqliteFailure(working->getRaw(), sqlite3_errcode(working->getRaw()),
                        "read save");
  const int step = sqlite3_backup_step(backup, -1);
  sqlite3_backup_finish(backup);
  if (step != SQLITE_DONE) throw sqliteFailure(source.get(), step, "read save");
  return working;
}

void SaveManager::stampMetadata(const DatabaseConnection& working,
                                std::uint64_t world_seed,
                                const GameDateValue& game_date,
                                std::int64_t playtime_seconds)
{
  // The versions are those of the build writing the save: a save upgraded
  // from an older build continues under the current engine and RNG.
  sqlite3_stmt* stmt = working.prepareStatement(
      "UPDATE save_meta SET game_version = ?, engine_version = ?, "
      "rng_version = ?, sim_version = ?, world_seed = ?, updated_at_utc = ?, "
      "last_saved_game_date = ?, playtime_seconds = ? WHERE id = 1;");
  const std::string updated = utcNow("{:%FT%TZ}");
  const std::string date = game_date.toString();
  sqlite3_bind_text(stmt, 1, SaveFormat::GAME_VERSION.data(),
                    static_cast<int>(SaveFormat::GAME_VERSION.size()),
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, SaveFormat::ENGINE_VERSION.data(),
                    static_cast<int>(SaveFormat::ENGINE_VERSION.size()),
                    SQLITE_STATIC);
  sqlite3_bind_int(stmt, 3, SaveFormat::RNG_VERSION);
  sqlite3_bind_int(stmt, 4, SaveFormat::SIM_VERSION);
  sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(world_seed));
  sqlite3_bind_text(stmt, 6, updated.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, date.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 8, playtime_seconds);
  working.executeStep(stmt);
  sqlite3_finalize(stmt);
}

SaveTimings SaveManager::persist(const DatabaseConnection& working,
                                 const fs::path& file, int backups)
{
  SaveTimings timings;
  const fs::path temp = tempPath(file);
  try
  {
    writeSnapshot(working.getRaw(), temp, &timings);
    const auto syncing = Clock::now();
    rotateBackups(file, std::clamp(backups, 0, MAX_BACKUPS));
    fault(FaultPoint::Rotated);
    renameOrThrow(temp, file, "replace save");
    syncFile(file.parent_path(), true);
    timings.sync_ms = millisecondsSince(syncing);
  }
  catch (...)
  {
    removeDatabaseFiles(temp);
    throw;
  }
  timings.total_ms = timings.snapshot_ms + timings.verify_ms + timings.sync_ms;
  return timings;
}

void SaveManager::preserveBeforeMigration(const fs::path& file,
                                          int schema_version)
{
  const fs::path target(
      std::format("{}.pre-v{}.bak", file.string(), schema_version));
  if (fs::exists(target)) return;
  copyDatabase(file, target);
  Logger::info("Kept a copy of the save before upgrading it: " +
               target.string());
}

int SaveManager::countBackups(const fs::path& file)
{
  int count = 0;
  for (int index = 1; index <= MAX_BACKUPS; ++index)
    count += fs::exists(backupPath(file, index)) ? 1 : 0;
  return count;
}

std::vector<SaveBackup> SaveManager::listBackups(const fs::path& file)
{
  std::vector<SaveBackup> backups;
  for (int index = 1; index <= MAX_BACKUPS; ++index)
  {
    const fs::path path = backupPath(file, index);
    if (!fs::exists(path)) continue;
    backups.emplace_back(SaveBackup::Kind::Rotation, index, path,
                         inspect(path, SaveCheck::Quick));
  }
  std::error_code code;
  const std::string prefix = file.filename().string() + ".pre-v";
  for (const auto& entry : fs::directory_iterator(file.parent_path(), code))
  {
    const std::string name = entry.path().filename().string();
    if (!name.starts_with(prefix) || !name.ends_with(".bak")) continue;
    const std::string version =
        name.substr(prefix.size(), name.size() - prefix.size() - 4);
    int schema_version = 0;
    try
    {
      schema_version = std::stoi(version);
    }
    catch (const std::invalid_argument&)
    {
      continue;
    }
    catch (const std::out_of_range&)
    {
      continue;
    }
    backups.emplace_back(SaveBackup::Kind::PreMigration, schema_version,
                         entry.path(), inspect(entry.path(), SaveCheck::Quick));
  }
  return backups;
}

const SaveBackup* SaveManager::newestUsable(
    const std::vector<SaveBackup>& backups)
{
  const auto usable = std::ranges::find_if(
      backups, [](const SaveBackup& backup)
      { return backup.inspection.status == SaveStatus::Ok; });
  return usable == backups.end() ? nullptr : std::to_address(usable);
}

void SaveManager::restoreBackup(const fs::path& file, const fs::path& backup)
{
  if (const SaveInspection source = inspect(backup, SaveCheck::Full);
      source.status != SaveStatus::Ok)
  {
    SaveError error;
    error.kind = source.status == SaveStatus::FutureVersion
                     ? SaveErrorKind::FutureVersion
                     : SaveErrorKind::Corrupt;
    error.detail = "backup unusable: " + source.detail;
    throw SaveFailure(std::move(error));
  }
  const fs::path temp = tempPath(file);
  Handle source_db;
  if (const int rc = source_db.open(backup, SQLITE_OPEN_READONLY);
      rc != SQLITE_OK)
    throw sqliteFailure(source_db.get(), rc, "open backup");
  try
  {
    writeSnapshot(source_db.get(), temp, nullptr);
    source_db.close();
    if (fs::exists(file))
    {
      // The replaced file is never deleted: a damaged one may still hold
      // recoverable data.
      const bool intact =
          inspect(file, SaveCheck::Quick).status == SaveStatus::Ok;
      const std::string aside = std::format("{}.{}-{}", file.string(),
                                            intact ? "replaced" : "corrupt",
                                            utcNow("{:%Y%m%dT%H%M%SZ}"));
      for (const char* suffix : {"-wal", "-shm"})
      {
        if (fs::exists(file.string() + suffix))
          renameOrThrow(file.string() + suffix, aside + suffix,
                        "keep replaced save");
      }
      renameOrThrow(file, aside, "keep replaced save");
    }
    renameOrThrow(temp, file, "restore backup");
  }
  catch (...)
  {
    removeDatabaseFiles(temp);
    throw;
  }
  syncFile(file.parent_path(), true);
}

void SaveManager::deleteSave(const fs::path& file)
{
  std::error_code code;
  const std::string name = file.filename().string();
  std::vector<fs::path> doomed;
  for (const auto& entry : fs::directory_iterator(file.parent_path(), code))
  {
    const std::string entry_name = entry.path().filename().string();
    if (entry_name == name || entry_name.starts_with(name + ".") ||
        entry_name.starts_with(name + "-"))
      doomed.push_back(entry.path());
  }
  for (const fs::path& path : doomed) fs::remove(path, code);
}

bool SaveManager::isAutosaveDue(AutosaveFrequency frequency,
                                const GameDateValue& last_autosave,
                                int last_autosave_season,
                                const GameDateValue& today, int season,
                                bool managed_match_yesterday)
{
  using enum AutosaveFrequency;
  switch (frequency)
  {
    case Off:
      return false;
    case Daily:
      return !(today == last_autosave);
    case Weekly:
      return dayOrdinal(today) - dayOrdinal(last_autosave) >= 7;
    case Monthly:
      return today.year != last_autosave.year ||
             today.month != last_autosave.month;
    case Matchday:
      return managed_match_yesterday;
    case SeasonEnd:
      return season != last_autosave_season;
  }
  return false;
}
