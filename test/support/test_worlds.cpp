// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "test_worlds.h"

#include <sqlite3.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <thread>

#include "global/logger.h"
#include "global/paths.h"
#include "global/runtime_paths.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace TestWorlds
{
namespace
{
fs::path executablePath()
{
#if defined(_WIN32)
  char buffer[MAX_PATH] = {};
  const DWORD size = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
  return size > 0 ? fs::path(std::string(buffer, size)) : fs::path();
#elif defined(__APPLE__)
  char buffer[4096] = {};
  std::uint32_t size = sizeof(buffer);
  return _NSGetExecutablePath(buffer, &size) == 0 ? fs::path(buffer)
                                                  : fs::path();
#else
  std::error_code error;
  return fs::read_symlink("/proc/self/exe", error);
#endif
}

/**
 * Identifies this build of the test executable: a checkpoint made by another
 * build (different simulation code) must never be reused. Empty when the
 * executable cannot be inspected: the disk cache is then off.
 */
std::string buildStamp()
{
  static const std::string stamp = []
  {
    std::error_code error;
    const fs::path exe = executablePath();
    if (exe.empty()) return std::string();
    const auto size = fs::file_size(exe, error);
    if (error) return std::string();
    const auto time = fs::last_write_time(exe, error);
    if (error) return std::string();
    // The built-in world is read at run time: editing it must not reuse
    // checkpoints generated from the old one.
    const fs::path world = AssetPaths::defaultWorld();
    const auto world_size = fs::file_size(world, error);
    if (error) return std::string();
    const auto world_time = fs::last_write_time(world, error);
    if (error) return std::string();
    return std::format("{}-{}-{}-{}-{}", exe.filename().string(), size,
                       time.time_since_epoch().count(), world_size,
                       world_time.time_since_epoch().count());
  }();
  return stamp;
}

long processId()
{
#if defined(_WIN32)
  return static_cast<long>(GetCurrentProcessId());
#else
  return static_cast<long>(getpid());
#endif
}

/**
 * Where checkpoints live: next to the test executable (inside the build
 * tree, so a clean build starts clean and nothing leaks into the system
 * temporary folder); the temporary folder when that is not writable.
 */
fs::path cacheDirectory()
{
  std::error_code error;
  fs::path directory = executablePath().parent_path() / "fm-test-fixtures";
  fs::create_directories(directory, error);
  if (!error) return directory;
  directory = fs::temp_directory_path(error);
  if (error) return {};
  directory /= "fm-test-fixtures";
  fs::create_directories(directory, error);
  return error ? fs::path() : directory;
}

std::string safeName(const std::string& text)
{
  std::string result;
  for (const char c : text)
    result +=
        (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.')
            ? c
            : '_';
  return result;
}

/** Records the producing process in its lock, so a killed one is noticed. */
void writeOwner(const fs::path& lock)
{
  std::ofstream(lock / "owner") << processId();
}

/** True when the process that took @p lock is known to be gone (POSIX). */
bool ownerGone(const fs::path& lock)
{
#if defined(_WIN32)
  (void)lock;
  return false;
#else
  std::ifstream in(lock / "owner");
  long pid = 0;
  if (!(in >> pid) || pid <= 0) return false;
  return kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH;
#endif
}

/** Deletes the checkpoints of earlier builds of this executable. */
void removeOtherBuilds(const fs::path& directory, const std::string& stamp)
{
  const std::string own = safeName(stamp);
  const std::string name = safeName(executablePath().filename().string()) + "-";
  std::error_code error;
  for (const auto& entry : fs::directory_iterator(directory, error))
  {
    const std::string file = entry.path().filename().string();
    if (file.starts_with(name) && !file.starts_with(own + "."))
      fs::remove_all(entry.path(), error);
  }
}

/**
 * Path of the cached checkpoint for @p key, produced by @p produce(path) when
 * missing. Production is serialised across processes by a lock directory and
 * published by an atomic rename, so a reader never sees a partial file.
 * Returns an empty path when the disk cache is unavailable.
 */
fs::path diskCheckpoint(const std::string& key,
                        const std::function<void(const fs::path&)>& produce)
{
  const std::string stamp = buildStamp();
  const fs::path directory = cacheDirectory();
  if (stamp.empty() || directory.empty()) return {};
  const fs::path file =
      directory / (safeName(stamp) + "." + safeName(key) + ".sqlite");
  std::error_code error;
  if (fs::exists(file, error)) return file;

  const fs::path lock = fs::path(file).concat(".lock");
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(300);
  while (true)
  {
    if (fs::create_directory(lock, error))
    {
      // We own production. A stale lock of a crashed producer expires below.
      struct Unlock
      {
        fs::path path;
        ~Unlock()
        {
          std::error_code ignored;
          fs::remove(path, ignored);
        }
      } unlock{lock};
      writeOwner(lock);
      removeOtherBuilds(directory, stamp);
      if (fs::exists(file, error)) return file;
      const fs::path temp = fs::path(file).concat(std::format(
          ".tmp{}",
          std::chrono::steady_clock::now().time_since_epoch().count()));
      produce(temp);
      fs::rename(temp, file, error);
      if (error)
      {
        fs::remove(temp, error);
        return {};
      }
      return file;
    }
    if (fs::exists(file, error)) return file;
    if (ownerGone(lock) || std::chrono::steady_clock::now() > deadline)
    {
      // The producer died or is hopelessly slow: take the lock over.
      fs::remove(lock, error);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

void backup(sqlite3* from, sqlite3* to)
{
  sqlite3_backup* copy = sqlite3_backup_init(to, "main", from, "main");
  if (!copy) throw std::runtime_error("TestWorlds: cannot start SQLite backup");
  const int step = sqlite3_backup_step(copy, -1);
  sqlite3_backup_finish(copy);
  if (step != SQLITE_DONE)
    throw std::runtime_error("TestWorlds: SQLite backup failed");
}

struct WorldTemplate
{
  std::shared_ptr<DatabaseConnection> connection;
};

}  // namespace

World defaultWorld(std::uint64_t seed)
{
  static std::mutex mutex;
  static std::map<std::uint64_t, WorldTemplate> templates;
  const std::scoped_lock lock(mutex);
  auto it = templates.find(seed);
  if (it == templates.end())
  {
    Logger::init();
    auto connection = std::make_shared<DatabaseConnection>(":memory:");
    GameData generated;
    generated.setWorldSeed(seed);
    if (!generated.loadFromDB(connection))
      throw std::runtime_error("TestWorlds: world generation failed");
    it = templates.emplace(seed, WorldTemplate{std::move(connection)}).first;
  }
  World world;
  world.connection = std::make_shared<DatabaseConnection>(":memory:");
  backup(it->second.connection->getRaw(), world.connection->getRaw());
  world.data = std::make_shared<GameData>();
  world.data->setWorldSeed(seed);
  if (!world.data->loadFromDB(world.connection))
    throw std::runtime_error("TestWorlds: world load failed");
  return world;
}

int uniqueSlot(int offset)
{
#if defined(_WIN32)
  const int pid = static_cast<int>(GetCurrentProcessId());
#else
  const int pid = static_cast<int>(getpid());
#endif
  return 7'000'000 + pid % 100'000 * 10 + offset;
}

namespace
{
std::unique_ptr<GameController> loadOrBuild(int slot, const std::string& key,
                                            const CareerPrepare& prepare,
                                            std::uint64_t seed, bool managed)
{
  Logger::init();
  const std::string full_key =
      std::format("{}.{}{}", seed, managed ? "" : "unmanaged.", key);
  const auto build = [&](int build_slot)
  {
    GameController controller;
    controller.newGame(build_slot, seed);
    if (managed)
      controller.selectManagedTeam(controller.getTeams().front().get().getId());
    if (prepare) prepare(controller);
    if (!controller.saveGame())
      throw std::runtime_error("TestWorlds: cannot save checkpoint " + key);
  };

  const fs::path cached = diskCheckpoint(
      full_key,
      [&](const fs::path& target)
      {
        const int build_slot = uniqueSlot(8);
        RuntimePaths::removeSave(build_slot);
        build(build_slot);
        std::error_code error;
        fs::copy_file(RuntimePaths::savePath(build_slot), target,
                      fs::copy_options::overwrite_existing, error);
        RuntimePaths::removeSave(build_slot);
        if (error)
          throw std::runtime_error("TestWorlds: cannot store checkpoint");
      });

  RuntimePaths::removeSave(slot);
  if (cached.empty())
  {
    // No usable disk cache: build straight into the caller's slot.
    build(slot);
  }
  else
  {
    std::error_code error;
    fs::create_directories(RuntimePaths::saveDirectory(), error);
    fs::copy_file(cached, RuntimePaths::savePath(slot),
                  fs::copy_options::overwrite_existing, error);
    if (error)
      throw std::runtime_error("TestWorlds: cannot copy checkpoint " + key);
  }
  auto controller = std::make_unique<GameController>();
  if (!controller->loadGame(slot))
    throw std::runtime_error("TestWorlds: cannot load checkpoint " + key);
  return controller;
}
}  // namespace

std::unique_ptr<GameController> career(int slot, const std::string& key,
                                       const CareerPrepare& prepare,
                                       std::uint64_t seed)
{
  return loadOrBuild(slot, key, prepare, seed, true);
}

std::unique_ptr<GameController> unmanagedWorld(int slot, const std::string& key,
                                               const CareerPrepare& prepare,
                                               std::uint64_t seed)
{
  return loadOrBuild(slot, key, prepare, seed, false);
}

std::unique_ptr<GameController> newCareer(int slot, std::uint64_t seed)
{
  return career(slot, "new", {}, seed);
}

std::unique_ptr<GameController> careerAt(int slot, const GameDateValue& date,
                                         std::uint64_t seed)
{
  return career(
      slot, "date-" + date.toString(),
      [date](GameController& controller)
      {
        while (controller.getCurrentDate() < date) controller.advanceDay();
      },
      seed);
}

std::unique_ptr<GameController> careerAtManagedFixture(int slot, int n,
                                                       std::uint64_t seed)
{
  return career(
      slot, std::format("fixture-{}", n),
      [n](GameController& controller)
      {
        for (int i = 0; i < n; ++i)
        {
          controller.advanceToNextManagedFixture();
          if (i + 1 < n) controller.advanceDay();
        }
      },
      seed);
}

}  // namespace TestWorlds
