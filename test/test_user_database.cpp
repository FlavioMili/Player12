// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "database/user_database.h"

namespace fs = std::filesystem;

namespace
{
fs::path scratchDir()
{
  const fs::path dir =
      fs::temp_directory_path() / "player12-fmdb-tests";
  fs::create_directories(dir);
  return dir;
}

void writeFile(const fs::path& path, const std::string& text)
{
  std::ofstream out(path);
  out << text;
}

constexpr const char* GOOD_DB = R"({
  "manifest": {"name": "Test DB", "author": "Tests", "version": "1.0",
               "schema": 1, "min_game_version": "1.0.0"},
  "leagues": [{"id": 1, "name": "Test League", "tiebreak": "head_to_head"}],
  "clubs": [{"id": 101, "league_id": 1, "name": "Test FC",
             "short_name": "TFC", "nickname": "Testers",
             "colours": {"primary": "#FF0000", "secondary": "#FFFFFF",
                         "home": {"shirt": "#FF0000", "shorts": "#FFFFFF",
                                  "socks": "#FF0000", "trim": "#000000"}},
             "stadium": {"name": "Test Ground", "capacity": 10000},
             "founded": 1900}],
  "players": [{"id": 1001, "team_id": 101, "first_name": "Test",
               "last_name": "Player", "nickname": "Tester", "age": 25,
               "nationality": "English", "role": "ST",
               "stats": {"Pace": 70, "Shooting": 75}, "contract_years": 2,
               "wage": 10000, "height": 180, "preferred_foot": "Both",
               "skin": 3}],
  "first_names": {"names": ["Test"]},
  "last_names": {"names": ["Player"]}
})";

constexpr const char* BAD_DB = R"({
  "manifest": {"name": "", "schema": 99},
  "leagues": [{"id": 1}, {"id": 1, "name": "Dup"}],
  "clubs": [{"id": 101, "name": "", "league_id": 999,
             "colours": {"primary": "red"}}],
  "players": [{"id": 1001, "team_id": 999, "first_name": "", "last_name": "X",
               "age": 5, "role": "Nope", "stats": [],
               "preferred_foot": "Middle", "skin": 42}]
})";
}  // namespace

TEST(UserDatabaseTest, AcceptsValidDatabase)
{
  const fs::path file = scratchDir() / "good.fmdb";
  writeFile(file, GOOD_DB);
  const UserDatabase::LoadResult result = UserDatabase::loadFile(file);
  EXPECT_TRUE(result.ok) << result.issues.front().message;
  EXPECT_EQ(result.content.manifest.name, "Test DB");
  EXPECT_EQ(result.content.leagues.size(), 1);
  EXPECT_EQ(result.content.clubs.size(), 1);
  EXPECT_EQ(result.content.players.size(), 1);
  EXPECT_TRUE(result.content.has_names);
}

TEST(UserDatabaseTest, RejectsInvalidDatabase)
{
  const fs::path file = scratchDir() / "bad.fmdb";
  writeFile(file, BAD_DB);
  const UserDatabase::LoadResult result = UserDatabase::loadFile(file);
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.issues.empty());
  bool saw_manifest = false;
  bool saw_leagues = false;
  bool saw_clubs = false;
  bool saw_players = false;
  for (const auto& issue : result.issues)
  {
    EXPECT_TRUE(issue.is_error);
    saw_manifest |= issue.section == "manifest";
    saw_leagues |= issue.section == "leagues";
    saw_clubs |= issue.section == "clubs";
    saw_players |= issue.section == "players";
  }
  EXPECT_TRUE(saw_manifest);
  EXPECT_TRUE(saw_leagues);
  EXPECT_TRUE(saw_clubs);
  EXPECT_TRUE(saw_players);
}

TEST(UserDatabaseTest, ImportActivateAndClearRoundTrip)
{
  const fs::path file = scratchDir() / "roundtrip.fmdb";
  writeFile(file, GOOD_DB);
  UserDatabase::clearActive();
  std::vector<UserDatabase::Issue> issues;
  std::string imported;
  ASSERT_TRUE(UserDatabase::importFile(file, issues, imported));
  EXPECT_TRUE(issues.empty());
  const auto active = UserDatabase::activeName();
  ASSERT_TRUE(active.has_value());
  EXPECT_EQ(*active, imported);
  const auto content = UserDatabase::activeContent();
  ASSERT_TRUE(content.has_value());
  EXPECT_EQ(content->manifest.name, "Test DB");
  // Clean up so no state leaks into other tests or runs.
  UserDatabase::clearActive();
  std::error_code error;
  fs::remove(UserDatabase::databasesDir() / imported, error);
  EXPECT_FALSE(UserDatabase::activeName().has_value());
}

// Loads a real packaged database when FM_TEST_USER_DATABASE points at one.
// Skipped otherwise (the file is too large to commit as a fixture).
TEST(UserDatabaseTest, RealWorldDatabaseLoads)
{
  const char* path = std::getenv("FM_TEST_USER_DATABASE");
  if (path == nullptr || *path == '\0') GTEST_SKIP();
  const UserDatabase::LoadResult result = UserDatabase::loadFile(path);
  for (const auto& issue : result.issues)
    printf("[%s #%lld] %s: %s\n", issue.section.c_str(), issue.index,
           issue.is_error ? "error" : "warning", issue.message.c_str());
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.content.leagues.size(), 22);
  EXPECT_EQ(result.content.clubs.size(), 440);
  EXPECT_GT(result.content.players.size(), 12000);
}
