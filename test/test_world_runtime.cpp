// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <algorithm>
#include <memory>
#include <string>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "database/world/world_geography.h"
#include "database/world/world_io.h"
#include "database/world/world_runtime.h"
#include "database/world/world_schema.h"
#include "global/logger.h"
#include "global/nationality.h"
#include "global/runtime_paths.h"
#include "model/continental.h"
#include "model/national_teams.h"
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
  country["demonym"] = "Scottish";
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

/** A fantasy world: two confederations with a cup each, two regions, a
 * language of its own, two peoples (one speaking it) and 12-club leagues. */
std::shared_ptr<const World::Document> fantasyWorld()
{
  World::Document document = World::Document::empty("Fantasy");
  json first = json::array();
  json last = json::array();
  for (int i = 0; i < 40; ++i)
  {
    first.push_back("Aegon" + std::to_string(i));
    last.push_back("Velar" + std::to_string(i));
  }
  document.add(Kind::Language, {{"id", 1},
                                {"name", "Valyrian"},
                                {"first_names", first},
                                {"last_names", last}});
  document.add(Kind::Confederation, {{"id", 1},
                                     {"name", "Seven Kingdoms"},
                                     {"cups", {{{"name", "Iron Cup"}}}}});
  document.add(Kind::Confederation, {{"id", 2},
                                     {"name", "Free Cities"},
                                     {"cups", {{{"name", "Coast Cup"}}}}});
  document.add(Kind::Region,
               {{"id", 1}, {"name", "The Reach"}, {"confederation_id", 1}});
  document.add(
      Kind::Region,
      {{"id", 2}, {"name", "The Narrow Sea"}, {"confederation_id", 2}});
  document.add(Kind::Country, {{"id", 1},
                               {"name", "Westeros"},
                               {"demonym", "Westerosi"},
                               {"language", 1},
                               {"region_id", 1},
                               {"reputation", 60}});
  document.add(Kind::Country, {{"id", 2},
                               {"name", "Braavos"},
                               {"demonym", "Braavosi"},
                               {"language", "Italian"},
                               {"region_id", 2},
                               {"reputation", 55}});
  for (int country = 1; country <= 2; ++country)
  {
    document.add(Kind::League, {{"id", country},
                                {"name", "League " + std::to_string(country)},
                                {"country_id", country},
                                {"tiebreak", "goal_difference"}});
    for (int i = 1; i <= 12; ++i)
    {
      const int id = country * 100 + i;
      const std::string code =
          std::string(1, static_cast<char>('A' + country)) +
          static_cast<char>('A' + i / 10) + static_cast<char>('A' + i % 10);
      document.add(Kind::Club, {{"id", id},
                                {"league_id", country},
                                {"name", "Club " + std::to_string(id)},
                                {"short_name", code},
                                {"colours", {{"primary", "#334455"}}}});
    }
  }
  EXPECT_TRUE(World::validate(document).empty());
  EXPECT_TRUE(World::playability(document).empty());
  return std::make_shared<const World::Document>(std::move(document));
}

/** Ten confederations with a cup each, 300 peoples (one country each) and a
 * 12-club league in each of the first ten countries. */
std::shared_ptr<const World::Document> wideWorld()
{
  constexpr int CONFEDERATIONS = 10;
  constexpr int COUNTRIES = 300;
  World::Document document = World::Document::empty("Wide");
  json first = json::array();
  json last = json::array();
  for (int i = 0; i < 40; ++i)
  {
    first.push_back("Aldo" + std::to_string(i));
    last.push_back("Brun" + std::to_string(i));
  }
  document.add(Kind::Language, {{"id", 1},
                                {"name", "Common"},
                                {"first_names", first},
                                {"last_names", last}});
  for (int i = 1; i <= CONFEDERATIONS; ++i)
  {
    document.add(Kind::Confederation,
                 {{"id", i},
                  {"name", "Union " + std::to_string(i)},
                  {"cups", {{{"name", "Cup " + std::to_string(i)}}}}});
    document.add(Kind::Region, {{"id", 1000 + i},
                                {"name", "Region " + std::to_string(i)},
                                {"confederation_id", i}});
  }
  for (int i = 1; i <= COUNTRIES; ++i)
  {
    document.add(Kind::Country,
                 {{"id", i},
                  {"name", "Land " + std::to_string(i)},
                  {"demonym", "People " + std::to_string(i)},
                  {"language", 1},
                  {"region_id", 1000 + (i - 1) % CONFEDERATIONS + 1},
                  {"reputation", 50}});
  }
  for (int country = 1; country <= CONFEDERATIONS; ++country)
  {
    document.add(Kind::League, {{"id", country},
                                {"name", "League " + std::to_string(country)},
                                {"country_id", country}});
    for (int i = 1; i <= 12; ++i)
    {
      const int id = country * 100 + i;
      const std::string code =
          std::string(1, static_cast<char>('A' + country)) +
          static_cast<char>('A' + i / 10) + static_cast<char>('A' + i % 10);
      document.add(Kind::Club, {{"id", id},
                                {"league_id", country},
                                {"name", "Club " + std::to_string(id)},
                                {"short_name", code},
                                {"colours", {{"primary", "#334455"}}}});
    }
  }
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

TEST(WorldPeoples, WorldPeoplesTakeFreeNumbersAndBuiltInOnesKeepTheirs)
{
  World::install(fantasyWorld());
  const auto westerosi = nationalityFromName("Westerosi");
  const auto braavosi = nationalityFromName("Braavosi");
  ASSERT_TRUE(westerosi && braavosi);
  EXPECT_EQ(std::to_underlying(*westerosi), FIRST_CUSTOM_NATIONALITY);
  EXPECT_EQ(std::to_underlying(*braavosi), FIRST_CUSTOM_NATIONALITY + 1);
  EXPECT_EQ(nationalityName(*westerosi), "Westerosi");
  EXPECT_EQ(nationalityFromName("Italian"), Language::IT);
  EXPECT_EQ(nationalityName(Language::IT), "Italian");
  EXPECT_TRUE(customNationality(*braavosi).has_value());
  EXPECT_FALSE(customNationality(Language::IT).has_value());
  EXPECT_EQ(std::ranges::count(allNationalities(), "Westerosi",
                               &std::pair<Language, std::string>::second),
            1);
  // The league follows its country: people, region and confederation.
  EXPECT_EQ(leagueProfile(1).domestic_nationality, *westerosi);
  EXPECT_EQ(leagueProfile(1).confederation, 1);
  EXPECT_EQ(leagueProfile(2).confederation, 2);
  EXPECT_NE(leagueProfile(1).region_id, leagueProfile(2).region_id);
  EXPECT_EQ(International::confederationOf(*westerosi), 1);
  EXPECT_EQ(International::confederationOf(*braavosi), 2);
  const auto& cups = Continental::competitions();
  ASSERT_EQ(cups.size(), 2U);
  EXPECT_EQ(cups[0].id, 250);
  EXPECT_EQ(cups[0].continent, 1);
  EXPECT_EQ(std::string(cups[1].name_key), "Coast Cup");
  EXPECT_EQ(cups[1].continent, 2);
  World::install(nullptr);
  EXPECT_FALSE(nationalityFromName("Westerosi").has_value());
  EXPECT_EQ(Continental::competitions().size(), 3U);
  EXPECT_EQ(World::geography().regions.size(), 4U);
}

TEST(WorldPeoples, OlderWorldsKeepTheBuiltInGeography)
{
  // A schema 2 world without peoples or regions: the nationality field still
  // names the people, the region text still picks the region.
  World::Document document = World::templateDocument();
  json& country = document.section(Kind::Country)[0];
  country.erase("demonym");
  country.erase("language");
  country["nationality"] = "Scottish";
  country["region"] = "SouthAmerica";
  EXPECT_TRUE(World::validate(document).empty());
  World::install(std::make_shared<const World::Document>(document));
  EXPECT_EQ(leagueProfile(1).domestic_nationality, Language::SC);
  EXPECT_EQ(leagueProfile(1).confederation, 2);  // The Americas.
  EXPECT_EQ(World::geography().regions.size(), 4U);
  World::install(nullptr);
}

TEST(WorldPeoples, FantasyWorldPlaysASeasonWithItsOwnPeoplesAndCups)
{
  const Slot slot{731'205};
  auto controller = career(slot.slot, fantasyWorld());
  const auto westerosi = *nationalityFromName("Westerosi");
  const GameData& data = *controller->getGameData();
  std::size_t of_people = 0;
  std::size_t from_pool = 0;
  for (const TeamID id : controller->getLeagueById(1)->get().getTeamIDs())
  {
    for (const PlayerID player_id : data.getTeam(id)->get().getPlayerIDs())
    {
      const Player& player = data.getPlayer(player_id)->get();
      if (player.getNationality() != westerosi) continue;
      ++of_people;
      from_pool += player.getFirstName().starts_with("Aegon") &&
                           player.getLastName().starts_with("Velar")
                       ? 1
                       : 0;
    }
  }
  EXPECT_GT(of_people, 150U);
  EXPECT_EQ(from_pool, of_people);
  controller->selectManagedTeam(101);
  const GameDateValue start = controller->getCurrentDate();
  const GameDateValue next_season(static_cast<std::uint16_t>(start.year + 1), 7,
                                  15);
  for (int days = 0; controller->getCurrentDate() < next_season && days < 500;
       ++days)
  {
    controller->advanceDay();
  }
  // Both cups were played, each with the clubs of its own confederation.
  const auto* iron = controller->getContinental()->getSeason(250);
  const auto* coast = controller->getContinental()->getSeason(251);
  ASSERT_TRUE(iron != nullptr && coast != nullptr);
  EXPECT_TRUE(std::ranges::all_of(iron->entrants, [](const auto& entrant)
                                  { return entrant.team_id / 100 == 1; }));
  EXPECT_TRUE(std::ranges::all_of(coast->entrants, [](const auto& entrant)
                                  { return entrant.team_id / 100 == 2; }));
  // The invented people has a national team that played.
  const NationalTeams* nations = controller->getNationalTeams();
  ASSERT_NE(nations, nullptr);
  ASSERT_NE(nations->getTeam(westerosi), nullptr);
  EXPECT_TRUE(std::ranges::any_of(nations->getFixtures(),
                                  [&](const International::Fixture& fixture)
                                  {
                                    return fixture.played &&
                                           (fixture.home == westerosi ||
                                            fixture.away == westerosi);
                                  }));
  // Saving keeps the people: another world in between, then a reload.
  controller->saveGame();
  const PlayerID sample = data.getTeam(101)->get().getPlayerIDs().front();
  const Language before = data.getPlayer(sample)->get().getNationality();
  World::install(nullptr);
  auto loaded = std::make_unique<GameController>();
  ASSERT_TRUE(loaded->loadGame(slot.slot));
  EXPECT_EQ(
      nationalityName(before),
      nationalityName(
          loaded->getGameData()->getPlayer(sample)->get().getNationality()));
  EXPECT_EQ(nationalityFromName("Westerosi"), westerosi);
}

TEST(WorldPeoples, SixteenBitIdsHoldHundredsOfPeoplesAndManyCups)
{
  const Slot slot{731'206};
  auto controller = career(slot.slot, wideWorld());
  EXPECT_EQ(allNationalities().size(), 159U + 300U);
  const auto last = nationalityFromName("People 300");
  ASSERT_TRUE(last.has_value());
  EXPECT_EQ(std::to_underlying(*last), FIRST_CUSTOM_NATIONALITY + 299);
  const auto& cups = Continental::competitions();
  ASSERT_EQ(cups.size(), 10U);
  EXPECT_EQ(cups.back().id, 259);
  EXPECT_EQ(cups.back().continent, 10);
  controller->selectManagedTeam(101);
  bool played = false;
  for (int days = 0; days < 220 && !played; ++days)
  {
    controller->advanceDay();
    for (int league = 1; league <= 10 && !played; ++league)
    {
      const TeamID club = static_cast<TeamID>(league * 100 + 1);
      played = std::ranges::any_of(
          controller->getTeamFixtures(club),
          [](const Match& match)
          {
            return match.getMatchType() == MatchType::CONTINENTAL &&
                   match.isPlayed();
          });
    }
  }
  EXPECT_TRUE(played);
  for (const auto& cup : cups)
  {
    const auto* season = controller->getContinental()->getSeason(cup.id);
    ASSERT_NE(season, nullptr);
    EXPECT_TRUE(season->drawn);
  }
}
