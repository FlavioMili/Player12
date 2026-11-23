// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

/**
 * Same-seed fingerprint of a new default career: every table of the first
 * save (fixtures, finances, players, staff...), the club identities and the
 * league profiles. Floating point results differ between compilers and
 * platforms, so the expected value is supplied by the developer through
 * FM_EXPECT_WORLD_FINGERPRINT (recorded on the base revision with the same
 * build) rather than hard-coded; without it the test prints the value.
 */

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include "controller/game_controller.h"
#include "database/datagenerator.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/world_tuning.h"

namespace
{
constexpr std::uint64_t SEED = 20251119;
constexpr int SLOT = 731'119;

struct Hash
{
  std::uint64_t value = 0xCBF29CE484222325ULL;
  void feed(std::string_view text)
  {
    for (const char c : text)
    {
      value ^= static_cast<unsigned char>(c);
      value *= 0x100000001B3ULL;
    }
    value ^= 0xFFU;
    value *= 0x100000001B3ULL;
  }
};

std::vector<std::string> query(sqlite3* db, const std::string& sql)
{
  std::vector<std::string> rows;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
  {
    return rows;
  }
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    std::string row;
    for (int column = 0; column < sqlite3_column_count(stmt); ++column)
    {
      const auto* text = sqlite3_column_text(stmt, column);
      row += text ? reinterpret_cast<const char*>(text) : "<null>";
      row += '|';
    }
    rows.push_back(std::move(row));
  }
  sqlite3_finalize(stmt);
  return rows;
}

std::uint64_t saveFingerprint(const std::filesystem::path& path)
{
  sqlite3* db = nullptr;
  sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  Hash hash;
  for (const std::string& table :
       query(db,
             "SELECT name FROM sqlite_master WHERE type = 'table' AND name "
             "NOT IN ('save_meta', 'schema_migrations', 'WorldDocument') "
             "ORDER BY name;"))
  {
    const std::string name = table.substr(0, table.size() - 1);
    hash.feed(name);
    std::vector<std::string> rows =
        query(db, "SELECT * FROM \"" + name + "\";");
    std::ranges::sort(rows);
    for (const auto& row : rows)
    {
      hash.feed(row);
    }
  }
  sqlite3_close(db);
  return hash.value;
}
}  // namespace

TEST(WorldFingerprint, DefaultCareerMatchesRecordedFingerprint)
{
  Logger::init();
  auto controller = std::make_unique<GameController>();
  controller->newGame(SLOT, SEED);
  Hash hash;
  hash.feed(std::to_string(saveFingerprint(RuntimePaths::savePath(SLOT))));
  for (const auto& [league_id, league] :
       controller->getGameData()->getLeagues())
  {
    const LeagueProfile& profile = leagueProfile(league_id);
    hash.feed(std::format("{}:{}:{}:{}:{}:{}:{}", league.getName(),
                          profile.reputation, profile.average_revenue_eur,
                          profile.wage_ratio, profile.shape.level_sd,
                          profile.match_style.goals,
                          static_cast<int>(profile.domestic_nationality)));
  }
  for (const auto& team : controller->getGameData()->getTeamsVector())
  {
    if (const ClubIdentity* identity =
            controller->getClubIdentity(team.get().getId()))
    {
      hash.feed(std::format("{}:{}:{}:{}:{}", team.get().getId(),
                            identity->short_name, identity->primary_colour,
                            identity->stadium_name, identity->nickname));
    }
  }
  controller.reset();
  RuntimePaths::removeSave(SLOT);
  const std::string value = std::format("{:016x}", hash.value);
  std::cout << "World fingerprint: " << value << '\n';
  if (const char* expected = std::getenv("FM_EXPECT_WORLD_FINGERPRINT"))
  {
    EXPECT_EQ(value, expected);
  }
}
