// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "database/world/world_combine.h"

using namespace World;
using json = nlohmann::json;

namespace
{
constexpr std::size_t slot(Kind kind) { return static_cast<std::size_t>(kind); }

Document makeDocument(const std::string& name,
                      std::optional<int> season = std::nullopt)
{
  Document document = Document::empty(name);
  Manifest manifest = document.manifest();
  manifest.season = season;
  document.setManifest(manifest);
  return document;
}

void addCountry(Document& d, int id, const std::string& name)
{
  d.add(Kind::Country, {{"id", id}, {"name", name}});
}

void addLeague(Document& d, int id, const std::string& name, int country)
{
  d.add(Kind::League, {{"id", id}, {"name", name}, {"country_id", country}});
}

void addClub(Document& d, int id, const std::string& name, int league,
             int balance = 1000)
{
  d.add(Kind::Club, {{"id", id},
                     {"name", name},
                     {"league_id", league},
                     {"balance", balance}});
}

void addPlayer(Document& d, int id, const std::string& first,
               const std::string& last, int age, int team)
{
  d.add(Kind::Player, {{"id", id},
                       {"first_name", first},
                       {"last_name", last},
                       {"age", age},
                       {"nationality", "Italy"},
                       {"team_id", team}});
}

Document spain(const std::string& name = "Base", int balance = 1000)
{
  Document d = makeDocument(name);
  addCountry(d, 1, "Spain");
  addLeague(d, 1, "La Liga", 1);
  addClub(d, 1, "Real Madrid", 1, balance);
  addPlayer(d, 1, "Luka", "Modric", 30, 1);
  return d;
}
}  // namespace

TEST(CombineDisjoint, ShiftsIdsAndRewritesReferences)
{
  Document base = spain();
  Document incoming = makeDocument("Other");
  addCountry(incoming, 1, "England");
  addLeague(incoming, 1, "Premier League", 1);
  addClub(incoming, 1, "Arsenal", 1);
  addPlayer(incoming, 1, "Bukayo", "Saka", 22, 1);

  const CombineResult result = combine(base, incoming);
  EXPECT_TRUE(result.conflicts.empty());
  EXPECT_EQ(result.document.count(Kind::Country), 2U);
  EXPECT_EQ(result.document.count(Kind::Club), 2U);
  EXPECT_EQ(result.added[slot(Kind::Player)], 1U);
  const std::int64_t country = result.id_map[slot(Kind::Country)].at(1);
  const std::int64_t league = result.id_map[slot(Kind::League)].at(1);
  const std::int64_t club = result.id_map[slot(Kind::Club)].at(1);
  const std::int64_t player = result.id_map[slot(Kind::Player)].at(1);
  EXPECT_EQ(country, 2);
  EXPECT_EQ(result.document.find(Kind::League, league)->at("country_id"),
            country);
  EXPECT_EQ(result.document.find(Kind::Club, club)->at("league_id"), league);
  EXPECT_EQ(result.document.find(Kind::Player, player)->at("team_id"), club);
}

TEST(CombineDisjoint, IdOverflowFallsBackToFreeId)
{
  Document base = makeDocument("Base");
  addCountry(base, 1, "Spain");
  addLeague(base, 248, "Old League", 1);
  Document incoming = makeDocument("Other");
  addCountry(incoming, 1, "England");
  addLeague(incoming, 5, "Premier League", 1);

  const CombineResult result = combine(base, incoming);
  const std::int64_t id = result.id_map[slot(Kind::League)].at(5);
  EXPECT_LE(id, maxId(Kind::League));
  EXPECT_NE(id, 248);
  EXPECT_NE(result.document.find(Kind::League, id), nullptr);
  EXPECT_EQ(result.document.count(Kind::League), 2U);
}

TEST(CombineMerge, IdenticalDuplicateMergesSilently)
{
  const Document base = spain();
  const Document incoming = spain("Other");
  const CombineResult result = combine(base, incoming);
  EXPECT_TRUE(result.conflicts.empty());
  EXPECT_EQ(result.document.count(Kind::Club), 1U);
  EXPECT_EQ(result.document.count(Kind::Player), 1U);
  EXPECT_EQ(result.merged[slot(Kind::Club)], 1U);
  EXPECT_EQ(result.merged[slot(Kind::Country)], 1U);
  EXPECT_EQ(result.added[slot(Kind::Club)], 0U);
}

TEST(CombineConflict, DifferingClubIsOneConflictAndChildrenFollowBase)
{
  const Document base = spain("Base", 1000);
  Document incoming = spain("Other", 5000);
  addPlayer(incoming, 2, "Vini", "Junior", 22, 1);

  const CombineResult result = combine(base, incoming);
  ASSERT_EQ(result.conflicts.size(), 1U);
  const Conflict& conflict = result.conflicts.front();
  EXPECT_EQ(conflict.kind, Kind::Club);
  EXPECT_EQ(conflict.fields, std::vector<std::string>{"balance"});
  EXPECT_EQ(conflict.base_source, "Base");
  EXPECT_EQ(conflict.incoming_source, "Other");
  EXPECT_EQ(conflict.incoming.at("balance"), 5000);
  EXPECT_EQ(result.document.find(Kind::Club, 1)->at("balance"), 1000);
  const std::int64_t player = result.id_map[slot(Kind::Player)].at(2);
  EXPECT_EQ(result.document.find(Kind::Player, player)->at("team_id"), 1);
  EXPECT_EQ(result.document.count(Kind::Club), 1U);
}

TEST(CombineResolve, KeepBaseKeepIncomingKeepBoth)
{
  const Document base = spain("Base", 1000);
  const Document incoming = spain("Other", 5000);
  const CombineResult result = combine(base, incoming);
  ASSERT_EQ(result.conflicts.size(), 1U);
  const Conflict& conflict = result.conflicts.front();

  Document keep_base = result.document;
  resolve(keep_base, conflict, Choice::KeepBase);
  EXPECT_EQ(keep_base.find(Kind::Club, 1)->at("balance"), 1000);

  Document keep_incoming = result.document;
  resolve(keep_incoming, conflict, Choice::KeepIncoming);
  EXPECT_EQ(keep_incoming.find(Kind::Club, 1)->at("balance"), 5000);
  EXPECT_EQ(keep_incoming.find(Kind::Club, 1)->at("id"), 1);
  EXPECT_EQ(keep_incoming.count(Kind::Club), 1U);

  Document keep_both = result.document;
  resolve(keep_both, conflict, Choice::KeepBoth);
  ASSERT_EQ(keep_both.count(Kind::Club), 2U);
  EXPECT_EQ(keep_both.find(Kind::Club, 1)->at("balance"), 1000);
  EXPECT_EQ(keep_both.find(Kind::Club, 2)->at("balance"), 5000);
}

TEST(CombineResolve, ResolveFieldsTakesOnlyListedPaths)
{
  Document base = spain("Base", 1000);
  base.find(Kind::Club, 1)->operator[]("nickname") = "Los Blancos";
  Document incoming = spain("Other", 5000);
  incoming.find(Kind::Club, 1)->operator[]("nickname") = "Merengues";
  CombineResult result = combine(base, incoming);
  ASSERT_EQ(result.conflicts.size(), 1U);
  resolveFields(result.document, result.conflicts.front(), {"balance"});
  EXPECT_EQ(result.document.find(Kind::Club, 1)->at("balance"), 5000);
  EXPECT_EQ(result.document.find(Kind::Club, 1)->at("nickname"), "Los Blancos");
}

TEST(CombineResolve, ResolveAllBySource)
{
  const Document base = spain("Base", 1000);
  const Document incoming = spain("Other", 5000);

  CombineResult first = combine(base, incoming);
  resolveAll(first.document, first.conflicts, "Other");
  EXPECT_TRUE(first.conflicts.empty());
  EXPECT_EQ(first.document.find(Kind::Club, 1)->at("balance"), 5000);

  CombineResult second = combine(base, incoming);
  resolveAll(second.document, second.conflicts, "Base");
  EXPECT_TRUE(second.conflicts.empty());
  EXPECT_EQ(second.document.find(Kind::Club, 1)->at("balance"), 1000);
}

TEST(CombineLookAlike, ClubFormTokensAndTypos)
{
  EXPECT_TRUE(lookAlike("real madrid", "real madrid cf"));
  EXPECT_FALSE(lookAlike("real madrid", "real madrid"));
  EXPECT_TRUE(lookAlike("borussia dortmund", "borusia dortmund"));
  EXPECT_FALSE(lookAlike("roma", "rome"));
  EXPECT_FALSE(lookAlike("real madrid", "real betis"));
}

TEST(CombineLookAlike, FlaggedNotMerged)
{
  const Document base = spain();
  Document incoming = makeDocument("Other");
  addCountry(incoming, 1, "Spain");
  addLeague(incoming, 1, "La Liga", 1);
  addClub(incoming, 1, "Real Madrid CF", 1);

  const CombineResult result = combine(base, incoming);
  EXPECT_EQ(result.document.count(Kind::Club), 2U);
  ASSERT_EQ(result.possible_duplicates.size(), 1U);
  const PossibleDuplicate& duplicate = result.possible_duplicates.front();
  EXPECT_EQ(duplicate.kind, Kind::Club);
  EXPECT_EQ(duplicate.base_id, 1);
  EXPECT_EQ(duplicate.incoming_id, result.id_map[slot(Kind::Club)].at(1));
}

TEST(CombinePlayers, IdentityWithAndWithoutSeason)
{
  Document base = makeDocument("Base", 2026);
  Document incoming = makeDocument("Other", 2028);
  addPlayer(base, 1, "Mario", "Rossi", 20, 0);
  addPlayer(incoming, 1, "Mario", "Rossi", 22, 0);
  const CombineResult with_season = combine(base, incoming);
  EXPECT_EQ(with_season.document.count(Kind::Player), 1U);
  // Same person, matched; only the age (relative to each season) differs.
  ASSERT_EQ(with_season.conflicts.size(), 1U);
  EXPECT_EQ(with_season.conflicts.front().fields,
            std::vector<std::string>{"age"});

  Document base_no = makeDocument("Base");
  Document incoming_no = makeDocument("Other", 2028);
  addPlayer(base_no, 1, "Mario", "Rossi", 20, 0);
  addPlayer(incoming_no, 1, "Mario", "Rossi", 22, 0);
  const CombineResult without = combine(base_no, incoming_no);
  EXPECT_EQ(without.document.count(Kind::Player), 2U);

  EXPECT_EQ(identityKey(Kind::Player, base.section(Kind::Player)[0], base),
            "mario rossi|Italy|2006");
}

TEST(CombineTravel, ReversedPairMatches)
{
  Document base = makeDocument("Base");
  Document incoming = makeDocument("Other");
  for (Document* d : {&base, &incoming})
  {
    addCountry(*d, 1, "Italy");
    d->add(Kind::City, {{"id", 1}, {"name", "Roma"}, {"country_id", 1}});
    d->add(Kind::City, {{"id", 2}, {"name", "Milano"}, {"country_id", 1}});
  }
  base.add(Kind::Travel, {{"from", 1}, {"to", 2}, {"minutes", 200}});
  incoming.add(Kind::Travel, {{"from", 2}, {"to", 1}, {"minutes", 200}});
  const CombineResult same = combine(base, incoming);
  EXPECT_EQ(same.document.count(Kind::Travel), 1U);
  EXPECT_EQ(same.merged[slot(Kind::Travel)], 1U);
  EXPECT_TRUE(same.conflicts.empty());

  incoming.section(Kind::Travel)[0]["minutes"] = 250;
  const CombineResult different = combine(base, incoming);
  ASSERT_EQ(different.conflicts.size(), 1U);
  EXPECT_EQ(different.conflicts.front().fields,
            std::vector<std::string>{"minutes"});
  Document resolved = different.document;
  resolve(resolved, different.conflicts.front(), Choice::KeepIncoming);
  EXPECT_EQ(resolved.section(Kind::Travel)[0].at("minutes"), 250);
}

TEST(CombineNames, PoolsUnion)
{
  Document base = spain();
  Document incoming = spain("Other");
  base.setNames(true, {{"names", {"A", "B"}},
                       {"by_nationality", {{"Italy", {"X"}}}},
                       {"excluded_name_hashes", {1}}});
  incoming.setNames(true,
                    {{"names", {"B", "C"}},
                     {"by_nationality", {{"Italy", {"Y"}}, {"Spain", {"Z"}}}},
                     {"excluded_name_hashes", {1, 2}}});
  const CombineResult result = combine(base, incoming);
  const json& pool = result.document.names(true);
  EXPECT_EQ(pool.at("names"), json::array({"A", "B", "C"}));
  EXPECT_EQ(pool.at("by_nationality").at("Italy"), json::array({"X", "Y"}));
  EXPECT_EQ(pool.at("by_nationality").at("Spain"), json::array({"Z"}));
  EXPECT_EQ(pool.at("excluded_name_hashes"), json::array({1, 2}));
}

TEST(CombineMergeInto, MovesReferencesAndRemoves)
{
  Document d = spain();
  addClub(d, 2, "Real Madrid CF", 1);
  addPlayer(d, 2, "Vini", "Junior", 22, 2);
  mergeInto(d, Kind::Club, 1, 2);
  EXPECT_EQ(d.count(Kind::Club), 1U);
  EXPECT_EQ(d.find(Kind::Player, 2)->at("team_id"), 1);
  EXPECT_EQ(d.find(Kind::Club, 2), nullptr);
}

TEST(CombinePurity, InputsUnchanged)
{
  const Document base = spain("Base", 1000);
  Document incoming = spain("Other", 5000);
  addClub(incoming, 2, "Barcelona", 1);
  const json base_before = base.root();
  const json incoming_before = incoming.root();
  const CombineResult result = combine(base, incoming);
  EXPECT_FALSE(result.conflicts.empty());
  EXPECT_EQ(base.root(), base_before);
  EXPECT_EQ(incoming.root(), incoming_before);
}

TEST(CombinePerformance, FifteenThousandPlayersUnderOneSecond)
{
  Document base = makeDocument("Base");
  Document incoming = makeDocument("Other");
  addCountry(base, 1, "Spain");
  addLeague(base, 1, "La Liga", 1);
  addClub(base, 1, "Real Madrid", 1);
  addCountry(incoming, 1, "Spain");
  addLeague(incoming, 1, "La Liga", 1);
  addClub(incoming, 1, "Real Madrid", 1);
  for (int i = 1; i <= 15000; ++i)
  {
    addPlayer(base, i, "First" + std::to_string(i), "Last", 25, 1);
    addPlayer(incoming, i, "Other" + std::to_string(i), "Last", 25, 1);
  }
  const auto start = std::chrono::steady_clock::now();
  const CombineResult result = combine(base, incoming);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_EQ(result.document.count(Kind::Player), 30000U);
  EXPECT_LT(elapsed, std::chrono::seconds(1));
}
