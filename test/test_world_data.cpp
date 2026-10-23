// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "controller/game_controller.h"
#include "database/datagenerator.h"
#include "database/gamedata.h"
#include "global/global.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/competition.h"
#include "model/standings.h"
#include "model/world_tuning.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;
constexpr LeagueID ITALIAN_LOWER_LEAGUE = 6;
constexpr std::size_t COUNTRIES = 11;
constexpr std::size_t CLUBS_PER_LEAGUE = 20;
constexpr std::size_t CLUBS_MOVING = 3;

int uniqueSlot(int offset)
{
  return 400'000 + getpid() % 100'000 * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

/** The data pack's leagues filled with its clubs, without generating players.
 */
void fillPackWorld(GameData& gamedata)
{
  std::map<LeagueID, std::vector<TeamID>> members;
  for (const Team& team : DataGenerator::generateTeams())
  {
    gamedata.addTeam(team.getId(), team);
    members[team.getLeagueId()].push_back(team.getId());
  }
  for (const League& league : DataGenerator::generateLeagues())
  {
    gamedata.addLeague(
        league.getId(),
        League(league.getId(), league.getName(), members[league.getId()],
               league.getParentLeagueID(), league.getTieBreakRule()));
  }
}
}  // namespace

TEST(WorldDataTest, LeaguesKeepTheirStructure)
{
  const auto leagues = DataGenerator::generateLeagues();
  ASSERT_EQ(leagues.size(), 2 * COUNTRIES);
  const auto lower =
      std::ranges::find(leagues, ITALIAN_LOWER_LEAGUE, &League::getId);
  ASSERT_NE(lower, leagues.end());
  EXPECT_EQ(lower->getParentLeagueID(), std::optional<LeagueID>{1});
  EXPECT_EQ(lower->getTieBreakRule(), TieBreakRule::HEAD_TO_HEAD);
}

TEST(WorldDataTest, EveryCountryHasATwoTierPyramid)
{
  GameData gamedata;
  fillPackWorld(gamedata);
  const auto roots = Competitions::countryRoots(gamedata);
  ASSERT_EQ(roots.size(), COUNTRIES);
  for (const LeagueID root : roots)
  {
    const auto tiers = Competitions::countryLeagues(gamedata, root);
    ASSERT_EQ(tiers.size(), 2u) << int(root);
    const League& top = gamedata.getLeague(tiers[0])->get();
    const League& lower = gamedata.getLeague(tiers[1])->get();
    EXPECT_EQ(tiers[0], root);
    EXPECT_EQ(Competitions::leagueTier(gamedata, tiers[1]), 2);
    EXPECT_EQ(lower.getParentLeagueID(), std::optional<LeagueID>{root});
    EXPECT_EQ(lower.getTieBreakRule(), top.getTieBreakRule())
        << lower.getName();
    EXPECT_EQ(top.getTeamIDs().size(), CLUBS_PER_LEAGUE) << top.getName();
    EXPECT_EQ(lower.getTeamIDs().size(), CLUBS_PER_LEAGUE) << lower.getName();
    EXPECT_EQ(Competitions::cupEntrants(gamedata, root).size(),
              2 * CLUBS_PER_LEAGUE);

    // Every division has its own economy row; the second tier plays in the
    // same country (nationality, region) at a lower level.
    const LeagueProfile& top_profile = leagueProfile(top.getId());
    const LeagueProfile& lower_profile = leagueProfile(lower.getId());
    EXPECT_EQ(top_profile.league_id, top.getId()) << top.getName();
    EXPECT_EQ(lower_profile.league_id, lower.getId()) << lower.getName();
    EXPECT_EQ(lower_profile.domestic_nationality,
              top_profile.domestic_nationality)
        << lower.getName();
    EXPECT_EQ(lower_profile.region, top_profile.region) << lower.getName();
    EXPECT_LT(lower_profile.reputation + 15, top_profile.reputation)
        << lower.getName();
    EXPECT_LT(lower_profile.average_revenue_eur,
              top_profile.average_revenue_eur)
        << lower.getName();
    EXPECT_FLOAT_EQ(lower_profile.continental_share, 0.0f) << lower.getName();
    EXPECT_NEAR(lower_profile.tv_share + lower_profile.gate_share +
                    lower_profile.commercial_share,
                1.0f, 1e-4f)
        << lower.getName();
  }
}

TEST(WorldDataTest, SeasonRolloverSwapsThreeClubsInEveryCountry)
{
  GameData gamedata;
  fillPackWorld(gamedata);
  // Final tables in team id order: the first ids finish on top.
  std::unordered_map<LeagueID, std::vector<StandingRow>> tables;
  for (const auto& [id, league] : gamedata.getLeagues())
  {
    std::vector<TeamID> order = league.getTeamIDs();
    std::ranges::sort(order);
    auto& rows = tables[id];
    for (const TeamID team_id : order)
    {
      StandingRow row;
      row.team_id = team_id;
      row.position = static_cast<uint16_t>(rows.size() + 1);
      rows.push_back(row);
    }
  }

  const auto movements = Competitions::computeLeagueMovements(gamedata, tables);
  ASSERT_EQ(movements.size(), 2 * CLUBS_MOVING * COUNTRIES);
  for (const LeagueID root : Competitions::countryRoots(gamedata))
  {
    const LeagueID lower = Competitions::countryLeagues(gamedata, root)[1];
    std::set<TeamID> promoted;
    std::set<TeamID> relegated;
    for (const auto& movement : movements)
    {
      if (movement.from == lower && movement.to == root)
        promoted.insert(movement.team_id);
      if (movement.from == root && movement.to == lower)
        relegated.insert(movement.team_id);
    }
    const auto& lower_table = tables.at(lower);
    const auto& top_table = tables.at(root);
    EXPECT_EQ(promoted,
              (std::set<TeamID>{lower_table[0].team_id, lower_table[1].team_id,
                                lower_table[2].team_id}))
        << int(root);
    EXPECT_EQ(relegated,
              (std::set<TeamID>{top_table[CLUBS_PER_LEAGUE - 1].team_id,
                                top_table[CLUBS_PER_LEAGUE - 2].team_id,
                                top_table[CLUBS_PER_LEAGUE - 3].team_id}))
        << int(root);
  }

  Competitions::applyLeagueMovements(gamedata, movements);
  for (const auto& [id, league] : gamedata.getLeagues())
  {
    EXPECT_EQ(league.getTeamIDs().size(), CLUBS_PER_LEAGUE) << league.getName();
    for (const TeamID team_id : league.getTeamIDs())
      EXPECT_EQ(gamedata.getTeam(team_id)->get().getLeagueId(), id);
  }
  for (const auto& movement : movements)
    EXPECT_EQ(gamedata.getTeam(movement.team_id)->get().getLeagueId(),
              movement.to);
}

TEST(WorldDataTest, EveryClubHasACompleteIdentity)
{
  const auto teams = DataGenerator::generateTeams();
  const auto identities = DataGenerator::loadClubIdentities();
  ASSERT_EQ(teams.size(), 2 * COUNTRIES * CLUBS_PER_LEAGUE);
  ASSERT_EQ(identities.size(), teams.size());

  std::set<std::string, std::less<>> names;
  std::set<std::string, std::less<>> codes;
  for (const Team& team : teams)
  {
    EXPECT_TRUE(names.insert(team.getName()).second) << team.getName();
    const auto found = identities.find(team.getId());
    ASSERT_NE(found, identities.end()) << team.getName();
    const ClubIdentity& identity = found->second;
    EXPECT_EQ(identity.short_name.size(), 3u) << team.getName();
    EXPECT_TRUE(std::ranges::all_of(
        identity.short_name, [](char c) { return c >= 'A' && c <= 'Z'; }))
        << identity.short_name;
    EXPECT_TRUE(codes.insert(identity.short_name).second)
        << identity.short_name;
    EXPECT_NE(identity.primary_colour, identity.secondary_colour);
    EXPECT_LE(identity.primary_colour, 0xFFFFFFu);
    EXPECT_FALSE(identity.stadium_name.empty()) << team.getName();
    EXPECT_FALSE(identity.nickname.empty()) << team.getName();
    EXPECT_GE(identity.stadium_capacity, 2'500u);
    EXPECT_LE(identity.stadium_capacity, 99'000u);
    EXPECT_GE(identity.founded, 1850);
    EXPECT_LE(identity.founded, 2025);
  }
}

TEST(WorldDataTest, NewGameLoadsNamesAndIdentities)
{
  Logger::init();
  const SlotCleanup cleanup{uniqueSlot(0)};
  auto controller = std::make_unique<GameController>();
  controller->newGame(cleanup.slot, WORLD_SEED);
  const GameData& data = *controller->getGameData();

  const auto league = data.getLeague(1);
  ASSERT_TRUE(league.has_value());
  EXPECT_EQ(league->get().getName(), "Italian League");
  EXPECT_EQ(Competitions::cupName(data, 1), "Italian Cup");

  const auto lecce = data.getTeam(103);
  ASSERT_TRUE(lecce.has_value());
  EXPECT_EQ(lecce->get().getName(), "Lecce");
  const auto predefined = data.getPlayer(1001);
  ASSERT_TRUE(predefined.has_value());
  EXPECT_EQ(predefined->get().getTeamId(), 103);

  // The Apulian second division, including Acaya.
  std::set<std::string, std::less<>> lower_names;
  for (const auto& [id, team] : data.getTeams())
  {
    if (team.getLeagueId() == ITALIAN_LOWER_LEAGUE)
      lower_names.insert(team.getName());
  }
  EXPECT_EQ(lower_names.size(), 20u);
  for (const char* town :
       {"Acaya", "Foggia", "Cerignola", "Taranto", "Otranto"})
    EXPECT_TRUE(lower_names.contains(town)) << town;

  // Every country's second division is generated below its top division,
  // with a mostly domestic squad.
  for (const LeagueID root : Competitions::countryRoots(data))
  {
    const auto tiers = Competitions::countryLeagues(data, root);
    ASSERT_EQ(tiers.size(), 2u) << int(root);
    const auto mean_reputation = [&data](LeagueID league_id)
    {
      const auto& ids = data.getLeague(league_id)->get().getTeamIDs();
      double total = 0.0;
      for (const TeamID id : ids)
        total += data.getTeam(id)->get().getReputation();
      return ids.empty() ? 0.0 : total / static_cast<double>(ids.size());
    };
    EXPECT_LT(mean_reputation(tiers[1]) + 10.0, mean_reputation(tiers[0]))
        << int(root);

    std::size_t players = 0;
    std::size_t domestic = 0;
    const Language country = leagueProfile(root).domestic_nationality;
    for (const TeamID id : data.getLeague(tiers[1])->get().getTeamIDs())
    {
      for (const Player& player : data.getPlayersForTeam(id))
      {
        ++players;
        if (player.getNationality() == country) ++domestic;
      }
    }
    ASSERT_GT(players, 0u) << int(root);
    EXPECT_GT(2 * domestic, players) << int(root);
  }

  const auto identities = DataGenerator::loadClubIdentities();
  const auto identity = identities.find(103);
  ASSERT_NE(identity, identities.end());
  EXPECT_EQ(identity->second.short_name, "LEC");
  EXPECT_EQ(identity->second.stadium_name, "Stadio Comunale di Lecce");
  EXPECT_NE(identity->second.primary_colour, identity->second.secondary_colour);
  for (const auto& [id, club] : data.getTeams())
  {
    if (id == FREE_AGENTS_TEAM_ID) continue;
    EXPECT_TRUE(identities.contains(id)) << club.getName();
  }
}
