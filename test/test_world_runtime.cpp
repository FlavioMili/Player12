// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <memory>
#include <string>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "database/world/world_io.h"
#include "database/world/world_runtime.h"
#include "database/world/world_schema.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/world_generation.h"
#include "model/world_tuning.h"

using json = nlohmann::json;
using World::Kind;

namespace
{
constexpr std::uint64_t SEED = 77;

struct Slot
{
  explicit Slot(int number) : slot(number) {}
  Slot(const Slot&) = delete;
  Slot& operator=(const Slot&) = delete;
  ~Slot() { RuntimePaths::removeSave(slot); }
  int slot;
};

/** Template world plus a second division of four clubs. */
std::shared_ptr<const World::Document> customWorld()
{
  World::Document document = World::templateDocument();
  json& country = document.section(Kind::Country)[0];
  country["nationality"] = "Scottish";
  country["economy"] = 30;
  document.add(Kind::League, {{"id", 2},
                              {"name", "Freeland Second"},
                              {"country_id", 1},
                              {"parent_league", 1}});
  for (int i = 1; i <= 4; ++i)
  {
    document.add(Kind::Club, {{"id", 200 + i},
                              {"league_id", 2},
                              {"name", "Lowtown " + std::to_string(i)},
                              {"short_name", "LO" + std::to_string(i)},
                              {"colours", {{"primary", "#334455"}}}});
  }
  json first = {{"names", {"Aldo", "Bram"}}};
  document.setNames(true, first);
  EXPECT_TRUE(World::validate(document).empty());
  EXPECT_TRUE(World::playability(document).empty());
  return std::make_shared<const World::Document>(std::move(document));
}

std::unique_ptr<GameController> career(
    int slot, std::shared_ptr<const World::Document> world)
{
  Logger::init();
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot, SEED, std::move(world));
  return controller;
}
}  // namespace

TEST(WorldRuntime, BuiltInWorldProvidesTheLeagueProfiles)
{
  World::install(nullptr);
  EXPECT_EQ(leagueProfiles().size(), 22U);
  EXPECT_EQ(leagueProfile(3).reputation, 92);
  EXPECT_EQ(leagueProfile(3).domestic_nationality, Language::EN);
  EXPECT_EQ(leagueProfile(200).reputation, DEFAULT_LEAGUE_PROFILE.reputation);
}

TEST(WorldRuntime, ProfilesRoundTripThroughJson)
{
  World::install(nullptr);
  for (const LeagueProfile& profile : leagueProfiles())
  {
    const LeagueProfile back = World::profileFromJson(
        World::profileToJson(profile), DEFAULT_LEAGUE_PROFILE);
    EXPECT_EQ(back.average_revenue_eur, profile.average_revenue_eur);
    EXPECT_EQ(back.shape.level_offset, profile.shape.level_offset);
    EXPECT_EQ(back.match_style.home_edge, profile.match_style.home_edge);
    EXPECT_EQ(back.region, profile.region);
    EXPECT_EQ(back.domestic_nationality, profile.domestic_nationality);
  }
}

TEST(WorldRuntime, CustomLeaguesFollowTheirCountry)
{
  const auto world = customWorld();
  const auto profiles = World::buildProfiles(*world);
  ASSERT_EQ(profiles.size(), 2U);
  const LeagueProfile& top = profiles[0];
  const LeagueProfile& second = profiles[1];
  EXPECT_EQ(top.domestic_nationality, Language::SC);
  EXPECT_EQ(top.reputation, 60);
  EXPECT_LT(top.average_revenue_eur,
            DEFAULT_LEAGUE_PROFILE.average_revenue_eur);
  EXPECT_LT(second.reputation, top.reputation);
  EXPECT_LT(second.average_revenue_eur, top.average_revenue_eur);
  // Authored values win over the synthesis.
  World::Document edited = *world;
  World::set(*edited.find(Kind::League, 1), "profile.reputation", 99);
  EXPECT_EQ(World::buildProfiles(edited)[0].reputation, 99);
}

TEST(WorldRuntime, CareerFromCustomWorldPersistsItsWorld)
{
  const Slot slot{731'201};
  {
    auto controller = career(slot.slot, customWorld());
    const GameData& data = *controller->getGameData();
    EXPECT_EQ(data.getLeagues().size(), 2U);
    ASSERT_TRUE(data.getTeam(101).has_value());
    EXPECT_EQ(data.getTeam(101)->get().getName(), "Northport");
    // Squads are generated around the authored player.
    ASSERT_TRUE(data.getPlayer(1).has_value());
    EXPECT_EQ(data.getPlayer(1)->get().getLastName(), "Example");
    EXPECT_GE(data.getTeam(101)->get().getPlayerIDs().size(), 20U);
    // The world's own name pool and nationality are used.
    std::size_t scottish = 0;
    for (const auto& player : data.getPlayersVector())
    {
      scottish += player.get().getNationality() == Language::SC ? 1 : 0;
    }
    EXPECT_GT(scottish, data.getPlayersVector().size() / 2);
    const ClubIdentity* identity = controller->getClubIdentity(101);
    ASSERT_NE(identity, nullptr);
    EXPECT_EQ(identity->short_name, "NOR");
    controller->selectManagedTeam(101);
    controller->saveGame();
  }
  // Another career in between installs the built-in world.
  World::install(nullptr);
  auto loaded = std::make_unique<GameController>();
  ASSERT_TRUE(loaded->loadGame(slot.slot));
  const ClubIdentity* identity = loaded->getClubIdentity(201);
  ASSERT_NE(identity, nullptr);
  EXPECT_EQ(identity->short_name, "LO1");
  EXPECT_EQ(World::current()->manifest().name, "My World");
  EXPECT_EQ(leagueProfile(1).domestic_nationality, Language::SC);
}

TEST(WorldRuntime, CustomWorldPlaysAWholeSeason)
{
  const Slot slot{731'202};
  auto controller = career(slot.slot, customWorld());
  controller->selectManagedTeam(201);
  const GameDateValue start = controller->getCurrentDate();
  const GameDateValue next_season(static_cast<std::uint16_t>(start.year + 1), 7,
                                  15);
  int days = 0;
  while (controller->getCurrentDate() < next_season && days < 500)
  {
    controller->advanceDay();
    ++days;
  }
  EXPECT_LT(days, 500);
  // Promotion and relegation swapped clubs between the two divisions.
  const auto& top = controller->getLeagueById(1)->get().getTeamIDs();
  EXPECT_EQ(top.size(), 4U);
  EXPECT_TRUE(std::ranges::any_of(top, [](TeamID id) { return id > 200; }));
}

TEST(WorldRuntime, SavesWithoutAWorldUseTheBuiltInWorld)
{
  const Slot slot{731'203};
  {
    auto controller = career(slot.slot, nullptr);
    controller->selectManagedTeam(103);
    controller->saveGame();
  }
  {
    sqlite3* db = nullptr;
    ASSERT_EQ(
        sqlite3_open(RuntimePaths::savePath(slot.slot).string().c_str(), &db),
        SQLITE_OK);
    sqlite3_exec(db, "DROP TABLE WorldDocument;", nullptr, nullptr, nullptr);
    sqlite3_close(db);
  }
  World::install(customWorld());
  auto loaded = std::make_unique<GameController>();
  ASSERT_TRUE(loaded->loadGame(slot.slot));
  EXPECT_EQ(World::current(), World::defaultWorld());
  EXPECT_EQ(leagueProfiles().size(), 22U);
}

TEST(WorldRuntime, FailedLoadKeepsTheRunningWorld)
{
  World::install(customWorld());
  const auto before = World::current();
  auto controller = std::make_unique<GameController>();
  EXPECT_FALSE(controller->loadGame(731'204));
  EXPECT_EQ(World::current(), before);
  World::install(nullptr);
}
