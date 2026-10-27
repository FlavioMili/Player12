// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "database/world/world_io.h"
#include "database/world/world_schema.h"
#include "database/world/world_validate.h"

using json = nlohmann::json;
namespace fs = std::filesystem;
using World::IssueCode;
using World::Kind;
using World::Severity;

namespace
{
bool hasIssue(const std::vector<World::Issue>& issues, IssueCode code,
              const std::string& field = {})
{
  return std::ranges::any_of(issues,
                             [&](const World::Issue& issue)
                             {
                               return issue.code == code &&
                                      (field.empty() || issue.field == field);
                             });
}

json playerJson(std::int64_t id, std::int64_t team)
{
  return {{"id", id},
          {"team_id", team},
          {"first_name", "Player"},
          {"last_name", std::to_string(id)},
          {"age", 20 + (id % 15)},
          {"nationality", "Italian"},
          {"role", "CM"},
          {"preferred_foot", "Right"},
          {"height", 180},
          {"wage", 1000},
          {"contract_years", 2},
          {"stats",
           {{"Pace", 60},
            {"Shooting", 60},
            {"Passing", 60},
            {"Dribbling", 60},
            {"Defending", 60},
            {"Physicality", 60},
            {"Stamina", 60},
            {"Vision", 60},
            {"Goalkeeping", 10}}}};
}

/** A world of @p players spread over 20-club divisions. */
World::Document bigWorld(int players)
{
  World::Document document = World::templateDocument();
  document.setSection(Kind::Player, json::array());
  document.setSection(Kind::Club, json::array());
  for (int club = 1; club <= 500; ++club)
  {
    document.add(Kind::Club, {{"id", 1000 + club},
                              {"league_id", 1},
                              {"name", "Club " + std::to_string(club)},
                              {"short_name", "C" + std::to_string(club % 99)},
                              {"colours", {{"primary", "#112233"}}}});
  }
  for (int id = 1; id <= players; ++id)
  {
    document.add(Kind::Player, playerJson(id, 1001 + (id % 500)));
  }
  return document;
}

fs::path scratchFile(const std::string& name)
{
  const fs::path dir = fs::temp_directory_path() / "fm_world_document_tests";
  fs::create_directories(dir);
  return dir / name;
}
}  // namespace

TEST(WorldDocument, TemplateIsValidAndPlayable)
{
  const World::Document document = World::templateDocument();
  EXPECT_TRUE(World::validate(document).empty());
  EXPECT_TRUE(World::playability(document).empty());
  EXPECT_EQ(document.count(Kind::Club), 4U);
}

TEST(WorldDocument, FindAddRemoveAndFreeIds)
{
  World::Document document = World::templateDocument();
  ASSERT_NE(document.find(Kind::Club, 101), nullptr);
  EXPECT_EQ(document.highestId(Kind::Club), 104);
  EXPECT_EQ(document.freeId(Kind::Club), 105);
  document.add(Kind::League, {{"id", 249}, {"name", "Top"}});
  // The highest league id is used: the first gap is returned instead.
  EXPECT_EQ(document.freeId(Kind::League), 2);
  EXPECT_TRUE(document.remove(Kind::Club, 101));
  EXPECT_EQ(document.find(Kind::Club, 101), nullptr);
  EXPECT_NE(document.find(Kind::Club, 102), nullptr);
}

TEST(WorldDocument, DottedPathsReadWriteAndRemove)
{
  json club = {{"id", 1}};
  World::set(club, "colours.home.shirt", "#FF0000");
  ASSERT_NE(World::get(club, "colours.home.shirt"), nullptr);
  EXPECT_EQ(*World::get(club, "colours.home.shirt"), "#FF0000");
  World::set(club, "colours.home.shirt", nullptr);
  EXPECT_FALSE(club.contains("colours"));
  EXPECT_EQ(World::differingPaths({{"id", 1}, {"a", 1}},
                                  {{"id", 2}, {"a", 1.0}, {"b", {{"c", 2}}}}),
            (std::vector<std::string>{"b.c"}));
}

TEST(WorldDocument, NormaliseFoldsAccentsCaseAndPunctuation)
{
  EXPECT_EQ(World::normalise("  Réal   Madrid C.F. "), "real madrid cf");
  EXPECT_EQ(World::normalise("Borussia-Mönchengladbach"),
            "borussia monchengladbach");
}

TEST(WorldDocument, ValidationReportsEveryFieldWithLocation)
{
  World::Document document = World::templateDocument();
  json& club = *document.find(Kind::Club, 101);
  club["colours"]["primary"] = "red";
  club["league_id"] = 77;
  json& player = document.section(Kind::Player)[0];
  player["age"] = 70;
  player["role"] = "Wizard";
  player["nationality"] = "Westerosi";
  player["stats"]["Pace"] = 140;
  document.add(Kind::Club, {{"id", 101}, {"name", "Copy"}, {"league_id", 1}});

  const auto issues = World::validate(document);
  EXPECT_TRUE(hasIssue(issues, IssueCode::BadColour, "colours.primary"));
  EXPECT_TRUE(hasIssue(issues, IssueCode::BadReference, "league_id"));
  EXPECT_TRUE(hasIssue(issues, IssueCode::OutOfRange, "age"));
  EXPECT_TRUE(hasIssue(issues, IssueCode::BadChoice, "role"));
  EXPECT_TRUE(hasIssue(issues, IssueCode::UnknownNationality, "nationality"));
  EXPECT_TRUE(hasIssue(issues, IssueCode::OutOfRange, "stats.Pace"));
  EXPECT_TRUE(hasIssue(issues, IssueCode::DuplicateId, "id"));
  const auto bad =
      std::ranges::find(issues, IssueCode::BadColour, &World::Issue::code);
  ASSERT_NE(bad, issues.end());
  EXPECT_EQ(bad->name, "Northport");
  EXPECT_EQ(bad->kind, Kind::Club);
  EXPECT_EQ(World::countOf(issues, Severity::Warning), 2U);
  EXPECT_TRUE(World::hasErrors(issues));
}

TEST(WorldDocument, PlayabilityBlocksUnfinishedStructures)
{
  World::Document document = World::templateDocument();
  EXPECT_TRUE(World::playability(document).empty());
  document.remove(Kind::Club, 104);
  auto issues = World::playability(document);
  EXPECT_TRUE(hasIssue(issues, IssueCode::TooFewClubs));
  EXPECT_TRUE(hasIssue(issues, IssueCode::NoPlayableLeague));

  document = World::templateDocument();
  document.add(Kind::League,
               {{"id", 2}, {"name", "Second"}, {"parent_league", 3}});
  document.add(Kind::League,
               {{"id", 3}, {"name", "Third"}, {"parent_league", 2}});
  (*document.find(Kind::Club, 102))["short_name"] = "NOR";
  issues = World::playability(document);
  EXPECT_TRUE(hasIssue(issues, IssueCode::ParentCycle));
  EXPECT_TRUE(hasIssue(issues, IssueCode::DuplicateShortName));
  EXPECT_EQ(World::countOf(issues, Severity::Blocking), issues.size());
}

TEST(WorldDocument, PartialWorldsAreValidDocuments)
{
  World::Document document = World::Document::empty("Legends");
  document.add(Kind::Player, playerJson(7, 0));
  EXPECT_TRUE(World::validate(document).empty());
  // Not playable on its own, but importable and combinable.
  EXPECT_TRUE(
      hasIssue(World::playability(document), IssueCode::NoPlayableLeague));
}

TEST(WorldDocument, WriterIsDeterministicAndRoundTrips)
{
  World::Document document = World::templateDocument();
  // Entities out of order and an unknown mod key must survive.
  document.add(Kind::Club, {{"id", 50},
                            {"league_id", 1},
                            {"name", "Early"},
                            {"mod_data", {{"anthem", "la la"}}}});
  json root = document.root();
  root["mod_extra"] = {{"x", 1}};
  const World::Document with_extra(root);
  const std::string first = World::write(with_extra);
  const auto loaded = World::readText(first);
  ASSERT_TRUE(loaded.ok()) << World::describe(loaded.issues.front());
  EXPECT_EQ(World::write(*loaded.document), first);
  EXPECT_EQ(loaded.document->count(Kind::Club), 5U);
  EXPECT_EQ(loaded.document->root()["mod_extra"],
            with_extra.root()["mod_extra"]);
  // One entity per line, sorted by id.
  const auto clubs = first.find("\"clubs\"");
  EXPECT_LT(first.find("\"id\":50", clubs), first.find("\"id\":101", clubs));
  EXPECT_NE(first.find("{\"anthem\":\"la la\"}"), std::string::npos);
}

TEST(WorldDocument, FileRoundTripWithUnicodePath)
{
  const World::Document document = World::templateDocument();
  const fs::path path = scratchFile("") / World::pathFromUtf8("Città ñ.fmdb");
  std::string error;
  ASSERT_TRUE(World::writeFile(document, path, &error)) << error;
  const auto loaded = World::readFile(path);
  ASSERT_TRUE(loaded.ok());
  EXPECT_EQ(loaded.document->root(), document.root());
  EXPECT_FALSE(fs::exists(fs::path(path) += ".tmp"));
  fs::remove(path);
}

TEST(WorldDocument, RejectsBrokenInputWithoutThrowing)
{
  const std::vector<std::pair<std::string, IssueCode>> cases = {
      {"", IssueCode::NotJson},
      {"{\"manifest\": {\"name\": \"x\"", IssueCode::NotJson},
      {"[1, 2, 3]", IssueCode::NotJson},
      {"{\"clubs\": []}", IssueCode::NoManifest},
      {"{\"manifest\": 5}", IssueCode::NoManifest},
      {"{\"manifest\": {\"name\": \"x\", \"schema\": \"two\"}}",
       IssueCode::NoManifest},
      {"{\"manifest\": {\"name\": \"x\", \"schema\": 99, "
       "\"min_game_version\": \"9.0\"}}",
       IssueCode::NewerSchema},
      {"{\"manifest\": {\"name\": \"x\"}, \"clubs\": 7}", IssueCode::NotAList},
      {"{\"manifest\": {\"name\": \"x\"}, \"clubs\": [7]}",
       IssueCode::NotAnObject},
      {"{\"manifest\": {\"name\": \"x\"}, \"clubs\": [{\"id\": -3}]}",
       IssueCode::BadId},
      {"{\"manifest\": {\"name\": \"x\"}, \"first_names\": {\"names\": []}}",
       IssueCode::WrongType},
      {"\xff\xfe garbage", IssueCode::NotJson},
  };
  for (const auto& [text, code] : cases)
  {
    World::LoadResult result;
    ASSERT_NO_THROW(result = World::readText(text)) << text;
    EXPECT_FALSE(result.ok()) << text;
    EXPECT_TRUE(hasIssue(result.issues, code)) << text;
  }
  EXPECT_TRUE(hasIssue(World::readFile(scratchFile("missing.fmdb")).issues,
                       IssueCode::CannotRead));
}

TEST(WorldDocument, TruncatedFilesNeverCrash)
{
  const std::string text = World::write(World::templateDocument());
  for (std::size_t size = 0; size < text.size(); size += 7)
  {
    ASSERT_NO_THROW((void)World::readText(text.substr(0, size)));
  }
}

TEST(WorldDocument, SchemaOneDatabasesUpgrade)
{
  const json old = {
      {"manifest", {{"name", "Old"}, {"schema", 1}, {"version", "1.0"}}},
      {"leagues",
       {{{"id", 1}, {"name", "Serie Uno"}, {"tiebreak", "head_to_head"}},
        {{"id", 2}, {"name", "Serie Due"}, {"parent_league", 1}}}},
      {"clubs",
       {{{"id", 101}, {"league_id", 2}, {"name", "Lecce"}},
        {{"id", 102}, {"league_id", 1}, {"name", "Bari"}}}},
      {"players", {playerJson(1, 101), playerJson(2, 102)}},
  };
  const auto loaded = World::readText(old.dump());
  ASSERT_TRUE(loaded.ok());
  const World::Document& document = *loaded.document;
  EXPECT_EQ(document.manifest().schema, World::SCHEMA);
  ASSERT_EQ(document.count(Kind::Country), 1U);
  EXPECT_EQ(document.section(Kind::Country)[0]["nationality"], "Italian");
  EXPECT_EQ(document.find(Kind::League, 2)->at("country_id"), 1);
  EXPECT_EQ(document.find(Kind::League, 1)->at("country_id"), 1);
}

TEST(WorldDocument, LargeWorldIsFastAndCompact)
{
  const World::Document document = bigWorld(15'000);
  const auto start = std::chrono::steady_clock::now();
  const std::string text = World::write(document);
  const auto loaded = World::readText(text);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  ASSERT_TRUE(loaded.ok());
  EXPECT_LT(text.size(), 10U * 1024U * 1024U);
  // Generous bound for slow CI machines; the benchmark tracks the real time.
  EXPECT_LT(std::chrono::duration<double>(elapsed).count(), 10.0);
}
