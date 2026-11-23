// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "global/runtime_paths.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "global/logger.h"
#include "global/paths.h"

#if defined(_WIN32)
#include <process.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
std::string environmentValue(const char* name)
{
#if defined(_WIN32)
  char* value = nullptr;
  if (std::size_t size = 0; _dupenv_s(&value, &size, name) != 0) return {};
  const auto free_value = [](char* allocation) { std::free(allocation); };
  const std::unique_ptr<char, decltype(free_value)> owned(value, free_value);
  return owned ? std::string(owned.get()) : std::string{};
#else
  const char* value = std::getenv(name);
  return value != nullptr ? std::string(value) : std::string{};
#endif
}

bool keepTestArtifacts()
{
  const auto keep = environmentValue("FM_KEEP_TEST_ARTIFACTS");
  return !keep.empty() && keep != "0";
}

std::filesystem::path ensureDirectory(std::filesystem::path path)
{
  std::filesystem::create_directories(path);
  return path;
}

std::filesystem::path ensureTestDirectory(std::filesystem::path path)
{
  std::filesystem::create_directories(path);
  if (std::filesystem::is_symlink(std::filesystem::symlink_status(path)))
    throw std::filesystem::filesystem_error(
        "Test storage must not be a symbolic link", path,
        std::make_error_code(std::errc::operation_not_permitted));
#if !defined(_WIN32)
  // A shared /tmp parent may already exist. Do not trust a directory planted
  // by another account, and keep process files private even with a loose umask.
  struct stat status{};
  if (lstat(path.c_str(), &status) != 0 || !S_ISDIR(status.st_mode) ||
      status.st_uid != geteuid())
    throw std::filesystem::filesystem_error(
        "Test storage must be owned by the current user", path,
        std::make_error_code(std::errc::permission_denied));
  std::filesystem::permissions(path, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace);
#endif
  return path;
}

void removeDatabaseFiles(const std::filesystem::path& database)
{
  std::error_code error;
  std::filesystem::remove(database, error);
  std::filesystem::remove(database.string() + "-wal", error);
  std::filesystem::remove(database.string() + "-shm", error);
}

unsigned long processId()
{
#if defined(_WIN32)
  return static_cast<unsigned long>(_getpid());
#else
  return static_cast<unsigned long>(getpid());
#endif
}

/**
 * Per-process test root, removed at process exit so test runs never
 * accumulate files. FM_KEEP_TEST_ARTIFACTS=1 keeps it (captures, saves) for
 * inspection.
 */
class TestRuntimeRoot
{
 public:
  explicit TestRuntimeRoot(std::filesystem::path directory)
      : path(ensureTestDirectory(std::move(directory)))
  {
  }
  ~TestRuntimeRoot()
  {
    // A forked child (e.g. a death test) must not delete its parent's root.
    if (processId() != owner) return;
    if (keep_artifacts) return;
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
  TestRuntimeRoot(const TestRuntimeRoot&) = delete;
  TestRuntimeRoot& operator=(const TestRuntimeRoot&) = delete;

  const std::filesystem::path path;

 private:
  const unsigned long owner = processId();
  // Read configuration while construction can still report allocation errors.
  // Destruction uses the filesystem cleanup overload that reports error codes.
  const bool keep_artifacts = keepTestArtifacts();
};
}  // namespace

std::filesystem::path RuntimePaths::root()
{
  if (const auto configured = environmentValue("FM_RUNTIME_ROOT");
      !configured.empty())
  {
    return ensureDirectory(std::filesystem::absolute(configured));
  }

  if (const auto testRoot = environmentValue("FM_TEST_RUNTIME_ROOT");
      !testRoot.empty())
  {
    // Created once per process; later calls only re-create the directory
    // if a test removed it.
    static const TestRuntimeRoot processRoot(
        ensureTestDirectory(std::filesystem::absolute(testRoot)) /
        std::to_string(processId()));
    return ensureTestDirectory(processRoot.path);
  }

  // Keep the historical storage identity through the Player12 rebrand so
  // existing careers and settings remain discoverable on every platform.
  if (const std::unique_ptr<char, decltype(&SDL_free)> prefPath(
          SDL_GetPrefPath("FlavioMili", "FootballManagement"), &SDL_free);
      prefPath != nullptr)
  {
    return ensureDirectory(prefPath.get());
  }

  // A predictable directory in the system temporary area can be planted by
  // another user. Never store careers there when the preference path fails.
  throw std::filesystem::filesystem_error(
      std::string("Cannot locate private game storage: ") + SDL_GetError(),
      std::make_error_code(std::errc::io_error));
}

std::filesystem::path RuntimePaths::saveDirectory()
{
  return ensureDirectory(root() / "saves");
}

std::filesystem::path RuntimePaths::savePath(int slot)
{
  if (slot < 0) throw std::invalid_argument("Save slot cannot be negative");
  return saveDirectory() / std::format("save_{}.db", slot);
}

std::filesystem::path RuntimePaths::settingsPath()
{
  return root() / "settings.json";
}

std::filesystem::path RuntimePaths::logPath()
{
  return ensureDirectory(root() / "logs") / "log.txt";
}

std::filesystem::path RuntimePaths::previousLogPath()
{
  return ensureDirectory(root() / "logs") / "log.prev.txt";
}

std::filesystem::path RuntimePaths::capturePath(const char* filename)
{
  return ensureDirectory(root() / "captures") / filename;
}

std::filesystem::path RuntimePaths::imguiIniPath()
{
  return root() / "imgui.ini";
}

namespace
{
bool hasAssets(const std::filesystem::path& candidate)
{
  std::error_code error;
  return std::filesystem::is_regular_file(
      candidate / "assets" / "db" / "schema.sql", error);
}

std::filesystem::path resolveAssetRoot()
{
  std::filesystem::path chosen = FM_SOURCE_DIR;
  if (const auto configured = environmentValue("FM_ASSET_ROOT");
      !configured.empty())
  {
    chosen = std::filesystem::absolute(configured);
  }
  else if (const char* base = SDL_GetBasePath(); base != nullptr)
  {
    // SDL owns the returned string; on macOS bundles it already points at
    // Contents/Resources.
    const std::filesystem::path executableDir(base);
    for (const auto& candidate :
         {executableDir / ".." / "share" / "footballmanagement",
          executableDir / ".." / "Resources", executableDir})
    {
      if (hasAssets(candidate))
      {
        chosen = candidate.lexically_normal();
        break;
      }
    }
  }
  Logger::info("Game data root: " + chosen.string());
  return chosen;
}
}  // namespace

const std::filesystem::path& RuntimePaths::assetRoot()
{
  static const std::filesystem::path resolved = resolveAssetRoot();
  return resolved;
}

void RuntimePaths::removeSave(int slot) { removeDatabaseFiles(savePath(slot)); }

void RuntimePaths::removeAllSaves()
{
  std::error_code error;
  std::filesystem::remove_all(saveDirectory(), error);
}
