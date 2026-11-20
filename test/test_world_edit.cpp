// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include "database/world/world_edit.h"
#include "database/world/world_io.h"
#include "database/world/world_validate.h"

using namespace World;

namespace
{
std::int64_t integer(const nlohmann::json& entity, const char* key)
{
  return entity.value(key, std::int64_t{0});
}
}  // namespace

TEST(WorldEdit, CreatedEntitiesAreValid)
{
  Document document = templateDocument();
  const auto country = create(document, Kind::Country);
  const auto city = create(document, Kind::City, country);
  const auto league = create(document, Kind::League, country);
  const auto club = create(document, Kind::Club, league);
  const auto player = create(document, Kind::Player, club);
  const auto travel = create(document, Kind::Travel, city);
  ASSERT_GT(country, 0);
  ASSERT_GT(city, 0);
  ASSERT_GT(league, 0);
  ASSERT_GT(club, 0);
  ASSERT_GT(player, 0);
  ASSERT_GE(travel, 0);
  EXPECT_EQ(integer(*document.find(Kind::City, city), "country_id"), country);
  EXPECT_EQ(integer(*document.find(Kind::Club, club), "league_id"), league);
  EXPECT_EQ(integer(*document.find(Kind::Player, player), "team_id"), club);
  EXPECT_EQ(document.find(Kind::Player, player)->at("stats").size(), 9U);
  EXPECT_FALSE(hasErrors(validate(document)));
}

TEST(WorldEdit, CreatedClubsGetUniqueNamesAndShortNames)
{
  Document document = templateDocument();
  const auto first = create(document, Kind::Club, 1);
  const auto second = create(document, Kind::Club, 1);
  const auto& a = *document.find(Kind::Club, first);
  const auto& b = *document.find(Kind::Club, second);
  EXPECT_NE(a.at("name"), b.at("name"));
  EXPECT_NE(a.at("short_name"), b.at("short_name"));
  EXPECT_EQ(a.at("short_name").get<std::string>().size(), 3U);
  for (const auto& issue : playability(document))
  {
    EXPECT_NE(issue.code, IssueCode::DuplicateName);
  }
}

TEST(WorldEdit, TravelNeedsTwoCities)
{
  Document document = Document::empty("Empty");
  EXPECT_EQ(create(document, Kind::Travel), -1);
  create(document, Kind::City);
  EXPECT_EQ(create(document, Kind::Travel), -1);
  create(document, Kind::City);
  const auto row = create(document, Kind::Travel);
  ASSERT_GE(row, 0);
  EXPECT_NE(document.section(Kind::Travel).at(0).at("from"),
            document.section(Kind::Travel).at(0).at("to"));
}

TEST(WorldEdit, DuplicateKeepsPlayerNamesAndSuffixesOthers)
{
  Document document = templateDocument();
  const auto player = duplicate(document, Kind::Player, 1);
  ASSERT_GT(player, 1);
  EXPECT_EQ(document.find(Kind::Player, player)->at("last_name"), "Example");
  const auto club = duplicate(document, Kind::Club, 101);
  ASSERT_GT(club, 0);
  EXPECT_EQ(document.find(Kind::Club, club)->at("name"), "Northport (2)");
  EXPECT_NE(document.find(Kind::Club, club)->at("short_name"),
            document.find(Kind::Club, 101)->at("short_name"));
  const auto again = duplicate(document, Kind::Club, 101);
  EXPECT_EQ(document.find(Kind::Club, again)->at("name"), "Northport (3)");
  const auto row = duplicate(document, Kind::Travel, 0);
  EXPECT_EQ(row, 1);
  EXPECT_EQ(duplicate(document, Kind::Club, 9999), -1);
}

TEST(WorldEdit, RemovingAClubFreesItsPlayers)
{
  Document document = templateDocument();
  const auto effect = impact(document, Kind::Club, 101);
  EXPECT_EQ(effect.players, 1U);
  removeEntity(document, Kind::Club, 101);
  EXPECT_EQ(document.find(Kind::Club, 101), nullptr);
  EXPECT_EQ(integer(*document.find(Kind::Player, 1), "team_id"), 0);
  EXPECT_TRUE(document.find(Kind::Player, 1)->contains("id"));
}

TEST(WorldEdit, RemovingALeagueLeavesClubsUnassigned)
{
  Document document = templateDocument();
  const auto child = create(document, Kind::League, 1);
  document.find(Kind::League, child)->operator[]("parent_league") = 1;
  const auto effect = impact(document, Kind::League, 1);
  EXPECT_EQ(effect.clubs, 4U);
  EXPECT_EQ(effect.leagues, 1U);
  removeEntity(document, Kind::League, 1);
  EXPECT_FALSE(document.find(Kind::League, child)->contains("parent_league"));
  const auto issues = playability(document);
  EXPECT_TRUE(
      std::ranges::any_of(issues, [](const Issue& issue)
                          { return issue.code == IssueCode::Unassigned; }));
}

TEST(WorldEdit, RemovingACountryAndCityDetachChildren)
{
  Document document = templateDocument();
  EXPECT_EQ(impact(document, Kind::City, 1).travel, 1U);
  removeEntity(document, Kind::City, 1);
  EXPECT_EQ(document.count(Kind::Travel), 0U);
  EXPECT_FALSE(document.find(Kind::Club, 101)->contains("city_id"));
  EXPECT_EQ(impact(document, Kind::Country, 1).cities, 3U);
  removeEntity(document, Kind::Country, 1);
  EXPECT_FALSE(document.find(Kind::City, 2)->contains("country_id"));
  EXPECT_FALSE(document.find(Kind::League, 1)->contains("country_id"));
  EXPECT_FALSE(hasErrors(validate(document)));
}

TEST(WorldEdit, RemovingPlayersAndTravelRowsByPosition)
{
  Document document = templateDocument();
  removeEntity(document, Kind::Player, 1);
  EXPECT_EQ(document.count(Kind::Player), 0U);
  removeEntity(document, Kind::Travel, 0);
  EXPECT_EQ(document.count(Kind::Travel), 0U);
  removeEntity(document, Kind::Travel, 5);
  EXPECT_FALSE(impact(document, Kind::Player, 1).any());
}
