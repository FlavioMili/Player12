// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Version reporting and local crash reports.

#include <gtest/gtest.h>
#include <stdlib.h>

#include <csignal>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>

#include "database/migrations/migrations.h"
#include "global/build_info.h"
#include "global/crash_report.h"
#include "global/logger.h"
#include "global/runtime_paths.h"

namespace fs = std::filesystem;

namespace
{
std::string readFile(const fs::path& path)
{
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
}

bool contains(std::string_view text, std::string_view part)
{
  return text.contains(part);
}

/** Points the runtime root of this process (and its children) elsewhere. */
class RuntimeRootOverride
{
 public:
  explicit RuntimeRootOverride(fs::path directory) : path(std::move(directory))
  {
    fs::remove_all(path);
    fs::create_directories(path);
    setenv("FM_RUNTIME_ROOT", path.c_str(), 1);
  }
  ~RuntimeRootOverride() { unsetenv("FM_RUNTIME_ROOT"); }
  RuntimeRootOverride(const RuntimeRootOverride&) = delete;
  RuntimeRootOverride& operator=(const RuntimeRootOverride&) = delete;

  const fs::path path;
};

/** The single report a crashed child process left under @p root. */
fs::path onlyReport(const fs::path& root)
{
  fs::path found;
  int count = 0;
  for (const auto& entry : fs::directory_iterator(root / "logs"))
  {
    const std::string name = entry.path().filename().string();
    if (name.starts_with("crash-") && name.ends_with(".txt"))
    {
      found = entry.path();
      ++count;
    }
  }
  EXPECT_EQ(count, 1);
  return found;
}
}  // namespace

TEST(BuildInfo, VersionIsTheProjectVersionEverywhere)
{
  const std::string_view version = BuildInfo::version();
  // A release tag may add a suffix (1.0.0-rc1); the numbers are the
  // project version from CMakeLists.txt.
  EXPECT_TRUE(version.starts_with(FM_PROJECT_VERSION)) << version;
  EXPECT_TRUE(std::regex_match(std::string(version),
                               std::regex(R"(\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?)")))
      << version;
  EXPECT_EQ(SaveFormat::GAME_VERSION, version) << "saves record this version";

  const std::string summary = BuildInfo::summary();
  EXPECT_TRUE(contains(summary, version)) << summary;
  EXPECT_TRUE(contains(summary, BuildInfo::commit())) << summary;
  EXPECT_FALSE(BuildInfo::commit().empty());
  EXPECT_FALSE(BuildInfo::configuration().empty());
  EXPECT_FALSE(BuildInfo::compiler().empty());
  EXPECT_FALSE(BuildInfo::platform().empty());
}

TEST(CrashReport, ReportHasVersionAndBuildAndIsShownOnce)
{
  Logger::init();
  const fs::path report = CrashReport::write("test: simulated failure");
  ASSERT_TRUE(fs::is_regular_file(report));
  EXPECT_EQ(report.parent_path(), CrashReport::directory());
  const std::string text = readFile(report);
  EXPECT_TRUE(contains(text, std::format("Version: {}", BuildInfo::version())))
      << text;
  EXPECT_TRUE(contains(text, BuildInfo::commit())) << text;
  EXPECT_TRUE(contains(text, BuildInfo::platform())) << text;
  EXPECT_TRUE(contains(text, "Reason: test: simulated failure")) << text;
  EXPECT_TRUE(contains(text, "nothing was sent anywhere")) << text;

  // Only the first fatal error of a process is reported.
  EXPECT_EQ(CrashReport::write("a second failure"), report);
  EXPECT_EQ(readFile(report), text);

  // The next start keeps that session's log next to the report.
  std::ofstream(RuntimePaths::previousLogPath())
      << "last lines of the crashed session\n";
  CrashReport::install();
  const auto pending = CrashReport::pending();
  ASSERT_TRUE(pending.has_value());
  EXPECT_EQ(pending->report, report);
  ASSERT_FALSE(pending->log.empty());
  EXPECT_EQ(readFile(pending->log), "last lines of the crashed session\n");

  CrashReport::acknowledge();
  EXPECT_FALSE(CrashReport::pending().has_value()) << "shown once";
  EXPECT_TRUE(fs::is_regular_file(report)) << "the report itself is kept";
  std::signal(SIGSEGV, SIG_DFL);
  std::signal(SIGABRT, SIG_DFL);
}

TEST(CrashReport, AcknowledgingKeepsTheNewestTenReports)
{
  const fs::path folder = CrashReport::directory();
  for (int day = 10; day < 22; ++day)
  {
    const std::string stamp = std::format("crash-209909{:02}T120000Z", day);
    std::ofstream(folder / (stamp + ".txt")) << "report\n";
    std::ofstream(folder / (stamp + ".log")) << "log\n";
  }
  CrashReport::acknowledge();
  EXPECT_FALSE(fs::exists(folder / "crash-20990910T120000Z.txt"));
  EXPECT_FALSE(fs::exists(folder / "crash-20990911T120000Z.log"));
  EXPECT_TRUE(fs::exists(folder / "crash-20990912T120000Z.txt"));
  EXPECT_TRUE(fs::exists(folder / "crash-20990921T120000Z.log"));
}

TEST(CrashReportDeathTest, UncaughtExceptionWritesAReport)
{
  GTEST_FLAG_SET(death_test_style, "fast");
  const RuntimeRootOverride root(RuntimePaths::root() / "uncaught");
  EXPECT_DEATH(
      {
        CrashReport::install();
        std::thread([] { throw std::runtime_error("boom in a worker"); })
            .join();
      },
      "");
  const fs::path report = onlyReport(root.path);
  const std::string text = readFile(report);
  EXPECT_TRUE(contains(text, "Reason: unhandled exception: boom in a worker"))
      << text;
  EXPECT_TRUE(contains(text, std::format("Version: {}", BuildInfo::version())));
  EXPECT_EQ(readFile(root.path / "logs" / "crash.pending"),
            report.filename().string());
}

TEST(CrashReportDeathTest, FatalSignalWritesAReport)
{
  GTEST_FLAG_SET(death_test_style, "fast");
  const RuntimeRootOverride root(RuntimePaths::root() / "signal");
  EXPECT_DEATH(
      {
        CrashReport::install();
        std::raise(SIGSEGV);
      },
      "");
  const std::string text = readFile(onlyReport(root.path));
  EXPECT_TRUE(contains(text, "Reason: fatal signal SIGSEGV")) << text;
  EXPECT_TRUE(contains(text, std::format("Version: {}", BuildInfo::version())));
}
