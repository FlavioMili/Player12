// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <sqlite3.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <print>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/global.h"
#include "global/language_manager.h"
#include "global/logger.h"
#include "global/paths.h"
#include "global/runtime_paths.h"
#include "model/injury.h"
#include "model/match.h"
#include "model/match_report.h"
#include "model/world_generation.h"
#include "model/world_rng.h"
#include "model/world_simulation.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

/**
 * Save slot unique to this process: ctest runs tests (and the unit_tests and
 * core_unit_tests copies of this file) in parallel, and saves are files.
 */
int uniqueSlot(int offset)
{
  return 100'000 + (getpid() % 100'000) * 10 + offset;
}

/** Deletes the save of a slot when the test ends. */
struct SlotCleanup
{
  int slot;
  explicit SlotCleanup(int slotValue) : slot(slotValue) {}
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;
};

std::unique_ptr<GameController> makeWorld(int slot,
                                          std::uint64_t seed = WORLD_SEED)
{
  Logger::init();
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot, seed);
  return controller;
}

double overallOf(const GameController& controller, const Player& player)
{
  return player.getOverall(controller.getStatsConfig());
}

float statOf(const Player& player, const char* stat)
{
  const auto found = player.getStats().find(stat);
  return found == player.getStats().end() ? 0.0f : found->second;
}

bool isWinger(PlayerRole role)
{
  return role == PlayerRole::LW || role == PlayerRole::RW;
}

void execSql(sqlite3* db, const char* sql)
{
  char* error = nullptr;
  ASSERT_EQ(sqlite3_exec(db, sql, nullptr, nullptr, &error), SQLITE_OK)
      << (error ? error : "") << " in " << sql;
}
}  // namespace

// ---------------------------------------------------------------------------
// Deterministic RNG
// ---------------------------------------------------------------------------

TEST(WorldRngTest, SameSeedSameSequenceAndIndependentStreams)
{
  using enum RngDomain;
  WorldRng a(99);
  WorldRng b(99);
  for (int i = 0; i < 100; ++i) ASSERT_EQ(a.next(), b.next());

  WorldRng injuries = WorldRng::stream(7, MatchInjury, 1, 2);
  WorldRng injuries_again = WorldRng::stream(7, MatchInjury, 1, 2);
  WorldRng youth = WorldRng::stream(7, YouthIntake, 1, 2);
  const std::uint64_t first = injuries.next();
  EXPECT_EQ(first, injuries_again.next());
  EXPECT_NE(first, youth.next());
  EXPECT_EQ(WorldRng::hashUniform(7, Retirement, 3, 4),
            WorldRng::hashUniform(7, Retirement, 3, 4));
  EXPECT_NE(WorldRng::hashUniform(7, RngDomain::Retirement, 3, 4),
            WorldRng::hashUniform(8, RngDomain::Retirement, 3, 4));
}

TEST(WorldRngTest, DistributionsStayInRange)
{
  WorldRng rng(1234);
  bool saw_low = false;
  bool saw_high = false;
  double normal_total = 0.0;
  constexpr int DRAWS = 20'000;
  for (int i = 0; i < DRAWS; ++i)
  {
    const int value = rng.uniformInt(3, 8);
    ASSERT_GE(value, 3);
    ASSERT_LE(value, 8);
    saw_low |= value == 3;
    saw_high |= value == 8;
    const double unit = rng.uniform01();
    ASSERT_GE(unit, 0.0);
    ASSERT_LT(unit, 1.0);
    normal_total += static_cast<double>(rng.normal(10.0f, 2.0f));
  }
  EXPECT_TRUE(saw_low && saw_high);
  EXPECT_NEAR(normal_total / DRAWS, 10.0, 0.1);

  const std::array<float, 3> weights = {0.0f, 1.0f, 3.0f};
  std::array<int, 3> picks{};
  for (int i = 0; i < DRAWS; ++i) ++picks[rng.weightedIndex(weights)];
  EXPECT_EQ(picks[0], 0);
  EXPECT_NEAR(static_cast<double>(picks[2]) / picks[1], 3.0, 0.3);
}

TEST(WorldRngTest, DayOrdinalMatchesTheCivilCalendar)
{
  EXPECT_EQ(dayOrdinal(GameDateValue(1970, 1, 1)), 0);
  EXPECT_EQ(dayOrdinal(GameDateValue(2025, 7, 2)), 20271);
  EXPECT_EQ(dayOrdinal(GameDateValue(2000, 2, 29)), 11016);
  EXPECT_EQ(dayOrdinal(GameDateValue(2026, 3, 1)) -
                dayOrdinal(GameDateValue(2026, 2, 28)),
            1);
  const GameDateValue date(2031, 11, 5);
  EXPECT_EQ(dateFromInt(dateToInt(date)), date);
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

TEST(WorldGenerationTest, SameSeedGeneratesTheSameWorld)
{
  const SlotCleanup slot_a{uniqueSlot(1)};
  const SlotCleanup slot_b{uniqueSlot(2)};
  const SlotCleanup slot_c{uniqueSlot(3)};
  const auto first = makeWorld(slot_a.slot);
  const auto second = makeWorld(slot_b.slot);
  const auto other = makeWorld(slot_c.slot, WORLD_SEED + 1);

  const auto& players_a = first->getGameData()->getPlayers();
  const auto& players_b = second->getGameData()->getPlayers();
  ASSERT_EQ(players_a.size(), players_b.size());
  for (const auto& [id, player] : players_a)
  {
    const auto twin = players_b.find(id);
    ASSERT_NE(twin, players_b.end());
    ASSERT_EQ(player.getName(), twin->second.getName());
    ASSERT_EQ(player.getWage(), twin->second.getWage());
    ASSERT_EQ(player.getStats(), twin->second.getStats());
    ASSERT_FLOAT_EQ(player.getPotential(), twin->second.getPotential());
    ASSERT_EQ(player.getTraits().professionalism,
              twin->second.getTraits().professionalism);
  }
  for (const auto& team : first->getTeams())
  {
    const Team& twin = second->getTeamById(team.get().getId())->get();
    EXPECT_EQ(team.get().getReputation(), twin.getReputation());
    EXPECT_EQ(team.get().getStadiumCapacity(), twin.getStadiumCapacity());
    EXPECT_EQ(team.get().getFinances().getBalance(),
              twin.getFinances().getBalance());
  }

  const auto& players_other = other->getGameData()->getPlayers();
  std::size_t differences = 0;
  for (const auto& [id, player] : players_a)
  {
    const auto found = players_other.find(id);
    if (found == players_other.end() ||
        found->second.getStats() != player.getStats())
      ++differences;
  }
  EXPECT_GT(differences, players_a.size() / 2);
}

TEST(WorldGenerationTest, RolesLeaguesAndReputationShapeTheWorld)
{
  const SlotCleanup slot{uniqueSlot(0)};
  const auto controller = makeWorld(slot.slot);
  const auto gamedata = controller->getGameData();

  double cb_defending = 0.0;
  double cb_shooting = 0.0;
  double cb_pace = 0.0;
  double winger_pace = 0.0;
  double gk_keeping = 0.0;
  double gk_shooting = 0.0;
  int cbs = 0;
  int wingers = 0;
  int keepers = 0;
  std::set<Language> nationalities;
  for (const auto& [id, player] : gamedata->getPlayers())
  {
    // Club players are under contract; the summer's free agents (their
    // contracts ran out on 30 June) are not.
    if (player.getTeamId() == FREE_AGENTS_TEAM_ID)
      ASSERT_EQ(player.getContractYears(), 0);
    else
      ASSERT_GE(player.getContractYears(), 1);
    ASSERT_LE(player.getContractYears(), 5);
    ASSERT_GE(player.getAge(), 16);
    ASSERT_LE(player.getAge(), 37);
    ASSERT_GE(player.getPotential() + 0.01,
              static_cast<float>(overallOf(*controller, player)));
    nationalities.insert(player.getNationality());
    if (player.getRole() == PlayerRole::CB)
    {
      cb_defending += statOf(player, "Defending");
      cb_shooting += statOf(player, "Shooting");
      cb_pace += statOf(player, "Pace");
      ++cbs;
    }
    else if (isWinger(player.getRole()))
    {
      winger_pace += statOf(player, "Pace");
      ++wingers;
    }
    else if (player.getRole() == PlayerRole::GK)
    {
      gk_keeping += statOf(player, "Goalkeeping");
      gk_shooting += statOf(player, "Shooting");
      ++keepers;
    }
  }
  ASSERT_GT(cbs, 0);
  ASSERT_GT(wingers, 0);
  ASSERT_GT(keepers, 0);
  EXPECT_GT(cb_defending / cbs, cb_shooting / cbs + 15.0);
  EXPECT_GT(winger_pace / wingers, cb_pace / cbs + 8.0);
  EXPECT_GT(gk_keeping / keepers, gk_shooting / keepers + 30.0);
  EXPECT_GE(nationalities.size(), 10u);

  // Reputation drives quality, stadiums and payroll; top leagues pay more.
  std::map<LeagueID, std::vector<const Team*>> by_league;
  for (const auto& team : controller->getTeams())
    by_league[team.get().getLeagueId()].push_back(&team.get());
  const auto payroll = [&](const Team* team)
  { return controller->getWeeklyWageBill(team->getId()); };
  const auto mean_payroll = [&](LeagueID league)
  {
    double total = 0.0;
    for (const Team* team : by_league[league])
      total += static_cast<double>(payroll(team));
    return total / static_cast<double>(by_league[league].size());
  };
  EXPECT_GT(mean_payroll(3), 5.0 * mean_payroll(6))
      << "English top flight must out-pay the Italian second tier";

  const WorldSimulation& world = controller->getGame()->getWorld();
  for (auto& [league_id, teams] : by_league)
  {
    std::ranges::sort(teams, [](const Team* a, const Team* b)
                      { return a->getReputation() > b->getReputation(); });
    const Team* giant = teams.front();
    const Team* minnow = teams.back();
    EXPECT_GT(giant->getReputation(), minnow->getReputation());
    EXPECT_GT(world.lineupStrength(giant->getId()),
              world.lineupStrength(minnow->getId()));
    EXPECT_GT(payroll(giant), payroll(minnow));
    EXPECT_GT(giant->getStadiumCapacity(), minnow->getStadiumCapacity());
    for (const Team* team : teams)
    {
      const Finances& finances = team->getFinances();
      EXPECT_GE(finances.getWageBudget(), payroll(team));
      EXPECT_LE(finances.getTransferBudget(), finances.getBalance());
      EXPECT_GE(team->getStadiumCapacity(), 2'500u);
      EXPECT_GT(team->getProfile().ticket_price, 0u);
    }
  }
}

// ---------------------------------------------------------------------------
// Injuries
// ---------------------------------------------------------------------------

TEST(WorldGenerationTest, NamesFollowNationalityAndAreUnique)
{
  const SlotCleanup slot{uniqueSlot(0)};
  const auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const NamePool& pool = NamePool::instance();

  std::map<std::string, int, std::less<>> full_names;
  std::map<LeagueID, std::pair<int, int>> domestic;  // domestic, total
  int matching_first_names = 0;
  for (const auto& [id, player] : gamedata->getPlayers())
  {
    ++full_names[player.getName()];
    EXPECT_FALSE(pool.isExcluded(player.getName()));
    if (const auto& firsts = pool.firstNames(player.getNationality());
        std::ranges::contains(firsts, player.getFirstName()))
      ++matching_first_names;
    const auto team = gamedata->getTeam(player.getTeamId());
    if (!team || player.getTeamId() == FREE_AGENTS_TEAM_ID) continue;
    auto& [home_grown, total] = domestic[team->get().getLeagueId()];
    ++total;
    if (player.getNationality() ==
        leagueProfile(team->get().getLeagueId()).domestic_nationality)
      ++home_grown;
  }
  int duplicates = 0;
  for (const auto& [name, count] : full_names) duplicates += count - 1;
  EXPECT_EQ(duplicates, 0);
  EXPECT_GT(matching_first_names,
            static_cast<int>(0.98 * gamedata->getPlayers().size()));

  // Each league's domestic share follows its profile (CIES: about 40% in
  // England, Italy and the MLS, 90% in Brazil and Argentina).
  for (const auto& [league_id, counts] : domestic)
  {
    const double share = static_cast<double>(counts.first) / counts.second;
    EXPECT_NEAR(share, leagueProfile(league_id).domestic_share, 0.08)
        << "league " << league_id;
  }
  EXPECT_LT(static_cast<double>(domestic[3].first) / domestic[3].second, 0.55)
      << "most Premier League players are foreign";
  EXPECT_GT(static_cast<double>(domestic[11].first) / domestic[11].second, 0.8)
      << "Brazil's top flight is mostly Brazilian";

  // At most one surname appears twice in a squad, none three times.
  for (const auto& team : controller->getTeams())
  {
    std::map<std::string, int, std::less<>> surnames;
    for (const auto& player : controller->getPlayersForTeam(team.get().getId()))
      ++surnames[player.get().getLastName()];
    int repeated = 0;
    for (const auto& [surname, count] : surnames)
    {
      EXPECT_LE(count, 2) << team.get().getName() << " " << surname;
      if (count > 1) ++repeated;
    }
    EXPECT_LE(repeated, 1) << team.get().getName();
  }
}

// Top divisions are far more stratified than second tiers (points SD per
// game 0.43-0.54 against 0.28-0.36), Germany has a single hegemon and Spain
// three giants, and the best second-tier clubs are about as strong as the
// weakest top-flight ones, so promoted clubs usually finish near the bottom.
TEST(WorldGenerationTest, LeagueShapesStratifyTopDivisions)
{
  // Shape arithmetic: zero mean, the league's spread, the elite pulled clear.
  for (const LeagueProfile& profile : LEAGUE_PROFILES)
  {
    const std::vector<float> offsets =
        WorldGeneration::levelOffsets(profile.shape, 20);
    double mean = 0.0;
    for (const float offset : offsets) mean += offset;
    mean /= 20.0;
    double squares = 0.0;
    for (const float offset : offsets)
      squares += (offset - mean) * (offset - mean);
    EXPECT_NEAR(mean, 0.0, 1e-3) << int(profile.league_id);
    EXPECT_NEAR(std::sqrt(squares / 20.0), profile.shape.level_sd, 1e-3)
        << int(profile.league_id);
    EXPECT_TRUE(std::ranges::is_sorted(offsets, std::greater<>{}));
    EXPECT_LE(WorldGeneration::reputationCentre(profile, 20) +
                  WorldTuning::Generation::REPUTATION_PER_LEVEL * offsets[0],
              99.0f - WorldTuning::Generation::REPUTATION_CAP_MARGIN + 1e-3f);
  }
  const std::vector<float> germany =
      WorldGeneration::levelOffsets(leagueProfile(4).shape, 20);
  EXPECT_GT(germany[0] - germany[1], 2.0f * (germany[1] - germany[2]))
      << "one hegemon";
  const std::vector<float> spain =
      WorldGeneration::levelOffsets(leagueProfile(2).shape, 20);
  EXPECT_GT(spain[2] - spain[3], 2.0f * (spain[3] - spain[4]))
      << "three giants";

  const SlotCleanup slot{uniqueSlot(8)};
  const auto controller = makeWorld(slot.slot);
  const WorldSimulation& world = controller->getGame()->getWorld();
  const auto strengths = [&](LeagueID league_id)
  {
    std::vector<double> values;
    for (const auto& team : controller->getTeams())
      if (team.get().getLeagueId() == league_id)
        values.push_back(world.lineupStrength(team.get().getId()));
    std::ranges::sort(values, std::greater<>{});
    return values;
  };
  const auto sd = [](const std::vector<double>& values)
  {
    double mean = 0.0;
    for (const double value : values) mean += value;
    mean /= static_cast<double>(values.size());
    double squares = 0.0;
    for (const double value : values)
      squares += (value - mean) * (value - mean);
    return std::sqrt(squares / static_cast<double>(values.size()));
  };
  // Every country with its second tier; the first six (England, Spain,
  // Italy, Germany, France, Portugal) have strongly stratified top flights.
  constexpr std::array<std::pair<LeagueID, LeagueID>, 11> COUNTRIES = {
      {{3, 14},
       {2, 13},
       {1, 6},
       {4, 15},
       {5, 16},
       {12, 22},
       {11, 21},
       {10, 20},
       {9, 19},
       {7, 17},
       {8, 18}}};
  for (std::size_t country = 0; country < COUNTRIES.size(); ++country)
  {
    const auto [top, second] = COUNTRIES[country];
    const std::vector<double> upper = strengths(top);
    const std::vector<double> lower = strengths(second);
    ASSERT_EQ(upper.size(), 20u);
    ASSERT_EQ(lower.size(), 20u);
    const double promoted = (lower[0] + lower[1] + lower[2]) / 3.0;
    std::print(
        "[shape] league {} strength SD {:.2f} (1st {:.1f}, 11th {:.1f}, "
        "16th {:.1f}, 20th {:.1f}); league {} SD {:.2f} (top three {:.1f})\n",
        int(top), sd(upper), upper[0], upper[10], upper[15], upper[19],
        int(second), sd(lower), promoted);
    if (country < 6)
    {
      EXPECT_GT(sd(upper), 1.4 * sd(lower)) << "league " << int(top);
      EXPECT_GT(sd(upper), 5.5) << "league " << int(top);
    }
    EXPECT_GT(sd(upper), sd(lower)) << "league " << int(top);
    // The best second-tier clubs sit among the weakest top-flight ones.
    EXPECT_LT(promoted, upper[10]) << "league " << int(second);
    EXPECT_GT(promoted, upper[19] - 3.0) << "league " << int(second);
  }
}

namespace
{
/** Players whose club does not list them (senior squad or academy). */
std::vector<std::string> rosterProblems(const GameController& controller)
{
  const auto data = controller.getGameData();
  std::unordered_map<PlayerID, TeamID> listed;
  std::vector<std::string> problems;
  for (const auto& [team_id, team] : data->getTeams())
  {
    if (team_id == FREE_AGENTS_TEAM_ID) continue;
    for (const auto* ids : {&team.getPlayerIDs(), &team.getAcademyIDs()})
      for (const PlayerID player_id : *ids)
        if (!listed.emplace(player_id, team_id).second)
          problems.push_back(std::format("player {} listed twice", player_id));
  }
  for (const auto& [player_id, player] : data->getPlayers())
  {
    if (player.getTeamId() == FREE_AGENTS_TEAM_ID) continue;
    const auto found = listed.find(player_id);
    if (found == listed.end() || found->second != player.getTeamId())
      problems.push_back(
          std::format("player {} of club {} is not on its roster", player_id,
                      player.getTeamId()));
  }
  if (problems.size() > 8) problems.resize(8);
  return problems;
}

std::string joinProblems(const std::vector<std::string>& problems)
{
  std::string text;
  for (const std::string& problem : problems) text += "\n  " + problem;
  return text;
}
}  // namespace

// Every club player is on his club's roster (senior squad or academy), in a
// new world, after saving and loading, and after weeks of play.
TEST(WorldGenerationTest, EveryClubPlayerIsOnHisClubsRoster)
{
  const SlotCleanup slot{uniqueSlot(10)};
  auto controller = makeWorld(slot.slot);
  EXPECT_TRUE(rosterProblems(*controller).empty())
      << "new world:" << joinProblems(rosterProblems(*controller));
  controller->selectManagedTeam(controller->getTeams().front().get().getId());
  controller->saveGame();
  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(slot.slot));
  EXPECT_TRUE(rosterProblems(*controller).empty())
      << "after loading:" << joinProblems(rosterProblems(*controller));
  for (int day = 0; day < 45; ++day) controller->advanceDay();
  EXPECT_TRUE(rosterProblems(*controller).empty())
      << "after 45 days:" << joinProblems(rosterProblems(*controller));
}

TEST(WorldGenerationTest, VeteransHaveNoHiddenGrowth)
{
  const SlotCleanup slot{uniqueSlot(1)};
  const auto controller = makeWorld(slot.slot);
  int veterans = 0;
  for (const auto& [id, player] : controller->getGameData()->getPlayers())
  {
    if (player.getAge() < WorldTuning::Generation::VETERAN_AGE) continue;
    ++veterans;
    EXPECT_LE(player.getPotential() - overallOf(*controller, player),
              WorldTuning::Generation::VETERAN_HEADROOM + 0.01)
        << player.getName() << " age " << player.getAge();
  }
  EXPECT_GT(veterans, 500);
  EXPECT_FLOAT_EQ(WorldGeneration::maxPotential(34, 70.0f),
                  70.0f + WorldTuning::Generation::VETERAN_HEADROOM);
  EXPECT_GT(WorldGeneration::maxPotential(19, 60.0f), 90.0f);
}

// Ageing by attribute group: pace and strength fall from about 28, technique
// from 31, passing and vision still improve into the early 30s, and
// goalkeepers and centre-backs hold their peak longer. Overall change per
// year: about -0.5 at 29-30, -1.5 at 31-32 and -3 from 33. [S: realism
// research 2, section 4]
TEST(PlayerAgeingTest, AttributeGroupsAgeAtDifferentRates)
{
  const SlotCleanup slot{uniqueSlot(9)};
  const auto controller = makeWorld(slot.slot);
  const StatsConfig& config = controller->getStatsConfig();
  const auto yearly = [&](PlayerRole role, int age)
  {
    std::map<std::string, float> stats;
    for (const std::string& name : config.possible_stats) stats[name] = 70.0f;
    Player player(1, 1, "Test", "Veteran", role, Language::EN, 1000, 0,
                  static_cast<std::uint8_t>(age - 1), 2, 180, Foot::Right,
                  stats);
    const double before = player.getOverall(config);
    player.agePlayer();
    return player.getOverall(config) - before;
  };
  constexpr std::array<PlayerRole, 6> OUTFIELD = {
      PlayerRole::CB,  PlayerRole::LB, PlayerRole::CM,
      PlayerRole::CAM, PlayerRole::LW, PlayerRole::ST};
  const auto outfield = [&](int age)
  {
    double total = 0.0;
    for (const PlayerRole role : OUTFIELD) total += yearly(role, age);
    return total / static_cast<double>(OUTFIELD.size());
  };
  for (const int age : {26, 27, 29, 30, 31, 32, 33, 34, 35})
    std::print(
        "[ageing] age {} outfield {:.2f} GK {:.2f} CB {:.2f} ST {:.2f}\n", age,
        outfield(age), yearly(PlayerRole::GK, age), yearly(PlayerRole::CB, age),
        yearly(PlayerRole::ST, age));

  EXPECT_NEAR(outfield(26), 0.0, 0.05) << "no ageing before 27";
  EXPECT_GE(outfield(27), 0.0) << "passing and vision still improve";
  EXPECT_LT(outfield(29), -0.1);
  EXPECT_GT(outfield(29), -0.9);
  EXPECT_LT(outfield(31), -0.9);
  EXPECT_GT(outfield(31), -2.2);
  EXPECT_LT(outfield(34), -2.0);
  EXPECT_GT(outfield(34), -4.5);
  // Goalkeepers and centre-backs keep their level longer.
  EXPECT_GT(yearly(PlayerRole::GK, 31), 0.5 * outfield(31));
  EXPECT_GT(yearly(PlayerRole::CB, 31), yearly(PlayerRole::ST, 31));
  EXPECT_LT(yearly(PlayerRole::GK, 36), -1.0) << "but not forever";
}

TEST(WorldGenerationTest, PreseasonFriendliesStayInTheRegion)
{
  const SlotCleanup slot{uniqueSlot(2)};
  const auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  int friendlies = 0;
  for (const auto& [date, matches] :
       controller->getGame()->getCalendar().getFullCalendar())
  {
    for (const Match& match : matches)
    {
      if (match.getMatchType() != MatchType::FRIENDLY) continue;
      ++friendlies;
      const LeagueID home =
          gamedata->getTeam(match.getHomeTeamId())->get().getLeagueId();
      const LeagueID away =
          gamedata->getTeam(match.getAwayTeamId())->get().getLeagueId();
      EXPECT_EQ(leagueProfile(home).region, leagueProfile(away).region)
          << static_cast<int>(home) << " vs " << static_cast<int>(away);
    }
  }
  EXPECT_GT(friendlies, 100);
}

TEST(InjuryModelTest, DiagnosisMixAndLayoffsFollowTheStudies)
{
  using enum InjuryType;
  WorldRng rng(77);
  constexpr int DRAWS = 40'000;
  int hamstrings = 0;
  int minor = 0;
  int match_acl = 0;
  std::vector<int> acl_days;
  for (int i = 0; i < DRAWS; ++i)
  {
    const Injury injury =
        InjuryModel::draw(rng, InjuryContext::Match, None, false);
    ASSERT_NE(injury.type, None);
    ASSERT_GE(injury.days, 1);
    if (injury.type == HamstringStrain || injury.type == HamstringTightness)
      ++hamstrings;
    if (InjuryModel::severity(injury.days) == InjurySeverity::Minor) ++minor;
    if (injury.type == KneeAcl)
    {
      ++match_acl;
      acl_days.push_back(injury.days);
    }
  }
  const double hamstring_share = static_cast<double>(hamstrings) / DRAWS;
  EXPECT_GT(hamstring_share, 0.18);
  EXPECT_LT(hamstring_share, 0.30);
  const double minor_share = static_cast<double>(minor) / DRAWS;
  EXPECT_GT(minor_share, 0.30);
  EXPECT_LT(minor_share, 0.65);
  ASSERT_GT(acl_days.size(), 100u);
  std::ranges::nth_element(
      acl_days,
      acl_days.begin() + static_cast<std::ptrdiff_t>(acl_days.size() / 2));
  const int acl_median = acl_days[acl_days.size() / 2];
  EXPECT_GT(acl_median, 170);
  EXPECT_LT(acl_median, 240);

  int training_acl = 0;
  for (int i = 0; i < DRAWS; ++i)
  {
    if (InjuryModel::draw(rng, InjuryContext::Training, None, false).type ==
        KneeAcl)
      ++training_acl;
  }
  EXPECT_LT(training_acl * 5, match_acl);
}

TEST(WorldSimulationTest, MatchConsequencesDrainRecoverAndInjure)
{
  const SlotCleanup slot{uniqueSlot(0)};
  const auto controller = makeWorld(slot.slot);
  const auto gamedata = controller->getGameData();
  const TeamID managed = controller->getTeams().front().get().getId();
  controller->selectManagedTeam(managed);
  const PlayerID player_id =
      controller->getPlayersForTeam(managed).front().get().getId();
  const Player& player = gamedata->getPlayers().at(player_id);

  ASSERT_TRUE(controller->applyMatchConsequences(player_id, 90, 68.0f, false));
  EXPECT_FLOAT_EQ(player.getDynamics().condition, 68.0f);
  EXPECT_EQ(player.getDynamics().season_minutes, 90);
  EXPECT_FALSE(controller->applyMatchConsequences(player_id, 90, 50.0f, false))
      << "consequences apply once per player and day";

  // Recovery follows a 48 h time constant: ~94% after three days.
  WorldSimulation world(gamedata);
  GameDateValue date = controller->getCurrentDate();
  for (int day = 0; day < 3; ++day)
  {
    date = date + 1;
    world.onDayAdvanced(date, managed);
  }
  if (player.isAvailable())
  {
    EXPECT_GT(player.getDynamics().condition, 88.0f);
    EXPECT_LT(player.getDynamics().condition, 100.0f);
  }

  // An engine-reported injury makes the player unavailable.
  const PlayerID injured_id =
      controller->getPlayersForTeam(managed).back().get().getId();
  ASSERT_TRUE(controller->applyMatchConsequences(injured_id, 30, 80.0f, true));
  EXPECT_FALSE(controller->isPlayerAvailable(injured_id));
  const auto injured = controller->getInjuredPlayers(managed);
  EXPECT_TRUE(std::ranges::contains(injured, injured_id));
  // Serious injuries are reported at once, knocks in the weekly medical
  // report.
  const std::string name =
      controller->getGameData()->getPlayer(injured_id)->get().getName();
  const auto reported = [&]
  {
    return std::ranges::any_of(
        controller->getInbox(),
        [&](const InboxMessage& message)
        {
          return message.category == InboxCategory::Injury &&
                 (message.player_id == injured_id ||
                  (message.title_key == "INBOX_MEDICAL_TITLE" &&
                   message.args.size() > 1 && message.args[1].contains(name)));
        });
  };
  for (int day = 0; day < 7 && !reported(); ++day) controller->advanceDay();
  EXPECT_TRUE(reported());
  EXPECT_GT(controller->getUnreadInboxCount(), 0u);
}

TEST(WorldSimulationTest, InjuredPlayersAreLeftOutOfAiLineups)
{
  const SlotCleanup slot{uniqueSlot(0)};
  const auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  Team& team =
      gamedata->getTeams().at(controller->getTeams().back().get().getId());
  const Player* goalkeeper = team.getLineup().getGoalkeeper();
  ASSERT_NE(goalkeeper, nullptr);
  const PlayerID keeper_id = goalkeeper->getId();

  PlayerDynamics& dynamics =
      gamedata->getPlayers().at(keeper_id).mutableDynamics();
  dynamics.injury = InjuryType::KneeMcl;
  dynamics.injury_days = 20;
  team.generateStartingXI(*gamedata, gamedata->getStatsConfig());

  ASSERT_NE(team.getLineup().getGoalkeeper(), nullptr);
  EXPECT_NE(team.getLineup().getGoalkeeper()->getId(), keeper_id);
  for (const auto& positioned : team.getLineup().getOutfieldPlayers())
    EXPECT_NE(positioned.player->getId(), keeper_id);
  for (const Player* reserve : team.getLineup().getReserves())
    EXPECT_NE(reserve->getId(), keeper_id);
}

// ---------------------------------------------------------------------------
// A year of world events (synthetic results, no match engine)
// ---------------------------------------------------------------------------

namespace
{
struct YearDigest
{
  std::int64_t balances = 0;
  double overall = 0.0;
  std::size_t injured_players = 0;
  std::size_t players = 0;
};

/** Prints revenue, wage ratio and net result per club of each league over
 * the first season (only visible with ctest -V). */
void printLeagueFinances(const GameController& controller)
{
  struct LeagueTotals
  {
    double income = 0.0;
    double wages = 0.0;
    double net = 0.0;
    double transfers = 0.0;
    std::vector<double> net_ratios;
    std::size_t squad_players = 0;
    int negative_cash = 0;
    int clubs = 0;
  };
  std::map<LeagueID, LeagueTotals> leagues;
  for (const auto& team : controller.getTeams())
  {
    const FinanceSummary season = team.get().getFinances().summarize(
        GameDateValue(2025, 7, 3), GameDateValue(2026, 7, 2));
    const auto category = [&](FinanceCategory value)
    {
      return static_cast<double>(season.by_category[static_cast<std::size_t>(
          std::to_underlying(value))]);
    };
    LeagueTotals& totals = leagues[team.get().getLeagueId()];
    const double transfers = category(FinanceCategory::TransferFeeIn) +
                             category(FinanceCategory::TransferFeeOut);
    const double income = static_cast<double>(season.income) -
                          category(FinanceCategory::TransferFeeIn);
    const double net = static_cast<double>(season.net()) - transfers;
    totals.income += income;
    totals.wages -= category(FinanceCategory::Wages);
    totals.net += net;
    totals.transfers += transfers;
    totals.net_ratios.push_back(income > 0.0 ? net / income : 0.0);
    totals.squad_players += team.get().getPlayerIDs().size();
    if (team.get().getFinances().getBalance() < 0) ++totals.negative_cash;
    ++totals.clubs;
  }
  for (auto& [league_id, totals] : leagues)
  {
    std::ranges::sort(totals.net_ratios);
    std::cout << "[world-calibration] league " << static_cast<int>(league_id)
              << " revenue/club=" << totals.income / totals.clubs / 1e6
              << "M wages/revenue=" << totals.wages / totals.income
              << " operating net/club=" << totals.net / totals.clubs / 1e6
              << "M median net="
              << totals.net_ratios[totals.net_ratios.size() / 2]
              << " transfers/club=" << totals.transfers / totals.clubs / 1e6
              << "M negative cash=" << totals.negative_cash << "/"
              << totals.clubs
              << " squad=" << totals.squad_players / totals.clubs << "\n";
  }
}

/** Plays a full year: daily world processing plus weekly synthetic results
 * for the managed club's league. */
YearDigest playYear(GameController& controller, WorldSimulation& world,
                    TeamID managed, int days)
{
  auto gamedata = controller.getGameData();
  const LeagueID league_id = gamedata->getTeam(managed)->get().getLeagueId();
  std::vector<TeamID> league_teams =
      gamedata->getLeague(league_id)->get().getTeamIDs();
  std::ranges::sort(league_teams);
  WorldRng scores(99);
  GameDateValue date = controller.getCurrentDate();
  int round = 0;
  for (int day = 0; day < days; ++day)
  {
    date = date + 1;
    world.onDayAdvanced(date, managed);
    if (date.month == 7 && date.day == 1)
    {
      world.onSeasonEnd(date, managed);
      world.onSeasonStart(date, managed);
    }
    if (const bool in_season = date.month >= 9 || date.month <= 5;
        !in_season || dayOrdinal(date) % 7 != 3)
      continue;
    // Round robin pairing by rotation.
    ++round;
    const std::size_t size = league_teams.size();
    for (std::size_t i = 0; i < size / 2; ++i)
    {
      const TeamID home =
          league_teams[(i + static_cast<std::size_t>(round)) % size];
      const TeamID away =
          league_teams[(size - 1 - i + static_cast<std::size_t>(round)) % size];
      if (home == away) continue;
      Match match(home, away, date, MatchType::LEAGUE, league_id);
      const auto home_goals =
          static_cast<std::uint8_t>(scores.uniformInt(0, 3));
      const auto away_goals =
          static_cast<std::uint8_t>(scores.uniformInt(0, 2));
      match.setPlayedResult(home_goals, away_goals);
      auto& league = gamedata->getLeagues().at(league_id);
      if (home_goals > away_goals)
        league.addPoints(home, 3);
      else if (home_goals < away_goals)
        league.addPoints(away, 3);
      else
      {
        league.addPoints(home, 1);
        league.addPoints(away, 1);
      }
      MatchReport report;
      world.onMatchPlayed(match, report, managed);
    }
  }
  YearDigest digest;
  for (const auto& team : controller.getTeams())
    digest.balances += team.get().getFinances().getBalance();
  for (const auto& [id, player] : gamedata->getPlayers())
  {
    digest.overall += overallOf(controller, player);
    if (player.getDynamics().last_injury != InjuryType::None)
      ++digest.injured_players;
  }
  digest.players = gamedata->getPlayers().size();
  return digest;
}
}  // namespace

TEST(WorldSimulationTest, AYearOfDevelopmentInjuriesYouthFinanceAndNews)
{
  const SlotCleanup slot{uniqueSlot(0)};
  const auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID managed = controller->getTeams().front().get().getId();
  const PlayerID first_new_id = gamedata->peekNextPlayerId();

  // Young, high-potential players should improve over a year.
  std::map<PlayerID, double> prospects;
  for (const auto& [id, player] : gamedata->getPlayers())
  {
    if (player.getAge() <= 20 &&
        player.getPotential() > overallOf(*controller, player) + 8.0)
      prospects.emplace(id, overallOf(*controller, player));
  }
  ASSERT_GT(prospects.size(), 50u);

  WorldSimulation world(gamedata);
  const auto started = std::chrono::steady_clock::now();
  const YearDigest digest = playYear(*controller, world, managed, 365);
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();
  std::cout << "[world] 365 days of world processing for "
            << controller->getTeams().size() << " clubs: " << seconds << " s\n";
  EXPECT_LT(seconds, 20.0);

  // Development: prospects still at the club grew on average.
  double growth = 0.0;
  int tracked = 0;
  for (const auto& [id, start] : prospects)
  {
    const auto player = gamedata->getPlayer(id);
    if (!player) continue;
    growth += overallOf(*controller, player->get()) - start;
    ++tracked;
  }
  ASSERT_GT(tracked, 0);
  EXPECT_GT(growth / tracked, 2.0);

  // Injuries: most players pick up at least one knock over a year.
  const double injured_share = static_cast<double>(digest.injured_players) /
                               static_cast<double>(digest.players);
  EXPECT_GT(injured_share, 0.30);
  EXPECT_LT(injured_share, 0.98);

  // Youth intake in March and retirements at the season end.
  std::size_t youth = 0;
  for (const auto& [id, player] : gamedata->getPlayers())
  {
    if (id < first_new_id) continue;
    ++youth;
    EXPECT_GE(player.getAge(), 15);
    EXPECT_LE(player.getAge(), 18);
    EXPECT_NE(player.getTeamId(), FREE_AGENTS_TEAM_ID);
  }
  EXPECT_GE(youth, 3u * controller->getTeams().size());
  EXPECT_FALSE(gamedata->getRemovedPlayerIds().empty()) << "no retirements";

  // Graduates get names nobody else carries.
  std::set<std::string, std::less<>> names;
  for (const auto& [id, player] : gamedata->getPlayers())
    EXPECT_TRUE(names.insert(player.getName()).second) << player.getName();

  // Finances: ledgers reconcile and every revenue stream was paid.
  for (const auto& team : controller->getTeams())
  {
    const Finances& finances = team.get().getFinances();
    ASSERT_EQ(finances.getBalance(), finances.ledgerTotal());
  }
  printLeagueFinances(*controller);
  std::cout << "[world-calibration] injured share=" << injured_share
            << " youth=" << youth
            << " retired=" << gamedata->getRemovedPlayerIds().size()
            << " prospect growth=" << growth / tracked << "\n";
  const Finances& managed_finances =
      gamedata->getTeam(managed)->get().getFinances();
  const FinanceSummary year = managed_finances.summarize(
      controller->getCurrentDate(), GameDateValue(2026, 7, 2));
  const auto total = [&](FinanceCategory category)
  {
    return year
        .by_category[static_cast<std::size_t>(std::to_underlying(category))];
  };
  EXPECT_LT(total(FinanceCategory::Wages), 0);
  EXPECT_GT(total(FinanceCategory::Matchday), 0);
  EXPECT_GT(total(FinanceCategory::Broadcasting), 0);
  EXPECT_GT(total(FinanceCategory::Sponsorship), 0);
  EXPECT_GE(total(FinanceCategory::PrizeMoney), 0);
  EXPECT_LT(total(FinanceCategory::Staff), 0);
  EXPECT_LT(total(FinanceCategory::Facilities), 0);

  // Bounded dynamic state.
  for (const auto& [id, player] : gamedata->getPlayers())
  {
    const PlayerDynamics& dynamics = player.getDynamics();
    ASSERT_GE(dynamics.morale, 0.0f);
    ASSERT_LE(dynamics.morale, 100.0f);
    ASSERT_GE(dynamics.condition, 0.0f);
    ASSERT_LE(dynamics.condition, 100.0f);
  }

  // News for the managed club.
  std::set<InboxCategory> categories;
  for (const InboxMessage& message : world.getInbox().getMessages())
  {
    categories.insert(message.category);
    EXPECT_FALSE(message.formatTitle().empty());
  }
  for (const InboxCategory expected :
       {InboxCategory::Match, InboxCategory::Finance, InboxCategory::Board,
        InboxCategory::Youth, InboxCategory::Injury})
    EXPECT_TRUE(categories.contains(expected)) << inboxCategoryKey(expected);
  EXPECT_EQ(world.getBoardState().team_id, managed);
}

TEST(WorldSimulationTest, TakingChargeFillsTheDayOneInbox)
{
  const SlotCleanup slot{uniqueSlot(3)};
  ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::EN));
  auto controller = makeWorld(slot.slot);
  const TeamID managed = controller->getTeams().front().get().getId();
  controller->selectManagedTeam(managed);

  // Objective and budgets are known before the first day is played.
  const BoardState board = controller->getBoardState();
  EXPECT_EQ(board.team_id, managed);
  EXPECT_GT(board.expected_position, 0);
  EXPECT_GT(board.target_position, 0);

  std::set<std::string, std::less<>> titles;
  for (const InboxMessage& message : controller->getInbox())
  {
    titles.insert(message.title_key);
    EXPECT_EQ(message.formatBody().find('{'), std::string::npos)
        << message.body_key;
    EXPECT_EQ(message.formatBody().find("#0"), std::string::npos);
  }
  for (const char* expected :
       {"INBOX_BOARD_WELCOME_TITLE", "INBOX_SQUAD_REPORT_TITLE",
        "INBOX_PRESEASON_TITLE", "INBOX_SCOUT_SUGGESTION_TITLE"})
    EXPECT_TRUE(titles.contains(expected)) << expected;
  EXPECT_GE(controller->getUnreadInboxCount(), 4u);

  // Re-selecting the same club does not repeat the news.
  const std::size_t messages = controller->getInbox().size();
  controller->selectManagedTeam(managed);
  EXPECT_EQ(controller->getInbox().size(), messages);

  // The board survives a reload.
  controller->saveGame();
  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(slot.slot));
  EXPECT_EQ(controller->getBoardState().expected_position,
            board.expected_position);
  EXPECT_EQ(controller->getInbox().size(), messages);
}

TEST(InboxTest, WorkloadWarningsDigestWithoutSuppressingInjuries)
{
  Inbox inbox;
  const auto post =
      [&inbox](const char* title, GameDateValue date, PlayerID player)
  {
    InboxMessage message;
    message.title_key = title;
    message.date = date;
    message.player_id = player;
    inbox.add(std::move(message));
  };
  post("MEDICAL_WARN_LOAD_TITLE", GameDateValue(2025, 9, 1), 1);
  post("MEDICAL_WARN_LOAD_TITLE", GameDateValue(2025, 9, 2), 2);
  post("INBOX_INJURY_TITLE", GameDateValue(2025, 9, 3), 1);
  post("INBOX_INJURY_TITLE", GameDateValue(2025, 9, 3), 2);
  post("MEDICAL_WARN_LOAD_TITLE", GameDateValue(2025, 9, 8), 3);
  const auto& messages = inbox.getMessages();
  ASSERT_EQ(messages.size(), 5u);  // Keep every advisory in the history.
  EXPECT_FALSE(messages[0].read);
  EXPECT_TRUE(messages[1].read);
  EXPECT_FALSE(messages[2].read);
  EXPECT_FALSE(messages[3].read);
  EXPECT_FALSE(messages[4].read);  // A new week's warning is unread again.
}

TEST(WorldSimulationTest, InboxStaysQuietThroughTheFirstMonths)
{
  const SlotCleanup slot{uniqueSlot(4)};
  const auto controller = makeWorld(slot.slot);
  const TeamID managed = controller->getTeams().front().get().getId();
  controller->selectManagedTeam(managed);
  while (controller->getCurrentDate() < GameDateValue(2025, 10, 1))
    controller->advanceDay();

  std::map<std::string, std::pair<int, int>, std::less<>> by_title;
  std::map<int, int> unread_by_month;
  for (const InboxMessage& message : controller->getInbox())
  {
    auto& [unread, read] = by_title[message.title_key];
    (message.read ? read : unread) += 1;
    if (!message.read) ++unread_by_month[message.date.month];
  }
  for (const auto& [title, counts] : by_title)
    std::cout << "[inbox] " << title << " unread=" << counts.first
              << " read=" << counts.second << "\n";
  for (const auto& [month, unread] : unread_by_month)
  {
    std::cout << "[inbox] month " << month << " unread " << unread << "\n";
    // July also holds the four day-one messages.
    EXPECT_LE(unread, month == 7 ? 19 : 15) << "month " << month;
  }
}

TEST(WorldSimulationTest, NegativeCashWarnsThenFreezesTransfers)
{
  const SlotCleanup slot{uniqueSlot(5)};
  auto controller = makeWorld(slot.slot);
  const TeamID managed = controller->getTeams().front().get().getId();
  controller->selectManagedTeam(managed);
  Finances& finances =
      controller->getGameData()->getTeams().at(managed).getFinances();
  EXPECT_GT(controller->transferBudgetForTeam(managed), 0u);
  // The spendable budget keeps twelve weeks of payroll in the bank.
  const std::int64_t payroll = controller->getWeeklyWageBill(managed);
  EXPECT_LE(
      static_cast<std::int64_t>(controller->transferBudgetForTeam(managed)),
      finances.getBalance() - 12 * payroll);

  const float confidence = controller->getBoardState().confidence;
  finances.addBalance(-finances.getBalance() - 300'000'000);
  EXPECT_EQ(controller->transferBudgetForTeam(managed), 0u);

  // The first weekly review warns; the next monthly review freezes.
  const auto count = [&](const char* title)
  {
    return std::ranges::count(controller->getInbox(), std::string(title),
                              &InboxMessage::title_key);
  };
  while (count("INBOX_BOARD_CASH_WARNING_TITLE") == 0) controller->advanceDay();
  EXPECT_FALSE(controller->isTransferEmbargoed());
  EXPECT_LT(controller->getBoardState().confidence, confidence);
  while (!controller->isTransferEmbargoed()) controller->advanceDay();
  EXPECT_EQ(controller->getCurrentDate().day, 1);
  EXPECT_EQ(count("INBOX_BOARD_EMBARGO_TITLE"), 1);

  // The embargo is rebuilt from the ledger after a reload.
  controller->saveGame();
  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(slot.slot));
  EXPECT_TRUE(controller->isTransferEmbargoed());

  // New money lifts it at the next weekly review.
  controller->getGameData()->getTeams().at(managed).getFinances().addBalance(
      600'000'000);
  EXPECT_FALSE(controller->isTransferEmbargoed());
  for (int day = 0; day < 7; ++day) controller->advanceDay();
  EXPECT_EQ(count("INBOX_BOARD_EMBARGO_LIFTED_TITLE"), 1);
}

TEST(WorldSimulationTest, AiClubsTrimSurplusPlayersAtTheSeasonEnd)
{
  const SlotCleanup slot{uniqueSlot(6)};
  const auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID managed = controller->getTeams()[0].get().getId();
  const TeamID ai_club = controller->getTeams()[1].get().getId();
  Team& team = gamedata->getTeams().at(ai_club);

  // Six weak extra players, one of them on loan from another club.
  auto stats =
      gamedata->getPlayer(team.getPlayerIDs().front())->get().getStats();
  for (auto& [name, value] : stats) value *= 0.4f;
  std::vector<PlayerID> extras;
  for (int i = 0; i < 6; ++i)
  {
    const PlayerID id = gamedata->allocatePlayerId();
    gamedata->addPlayer(
        id, Player(id, ai_club, "Extra", std::to_string(i), PlayerRole::CM,
                   Language::EN, 1000, 0, 25, 3, 180, Foot::Right, stats));
    team.addPlayerID(id);
    extras.push_back(id);
  }
  const PlayerID loanee = extras.back();

  WorldSimulation world(gamedata);
  world.setLoanCheck([loanee](PlayerID id) { return id == loanee; });
  world.onSeasonEnd(GameDateValue(2026, 7, 1), managed);

  for (const PlayerID id : extras)
  {
    const auto player = gamedata->getPlayer(id);
    ASSERT_TRUE(player);
    EXPECT_EQ(player->get().getContractYears(), id == loanee ? 3 : 1)
        << player->get().getName();
  }
}

TEST(WorldSimulationTest, SameSeedSameHistory)
{
  const SlotCleanup slot_a{uniqueSlot(1)};
  const SlotCleanup slot_b{uniqueSlot(2)};
  const auto first = makeWorld(slot_a.slot);
  const auto second = makeWorld(slot_b.slot);
  const TeamID managed = first->getTeams().front().get().getId();
  WorldSimulation world_a(first->getGameData());
  WorldSimulation world_b(second->getGameData());
  const YearDigest a = playYear(*first, world_a, managed, 90);
  const YearDigest b = playYear(*second, world_b, managed, 90);
  EXPECT_EQ(a.balances, b.balances);
  EXPECT_DOUBLE_EQ(a.overall, b.overall);
  EXPECT_EQ(a.injured_players, b.injured_players);
  EXPECT_EQ(world_a.getInbox().getMessages().size(),
            world_b.getInbox().getMessages().size());
}

TEST(WorldSimulationTest, BoardSetsObjectivesAndLosesPatienceOnlySlowly)
{
  BoardState board;
  board.objective = BoardModel::objectiveFor(3, 20);
  EXPECT_EQ(board.objective, BoardObjective::TopFour);
  board.target_position = BoardModel::targetPosition(board.objective, 20);
  EXPECT_EQ(board.target_position, 4);
  EXPECT_EQ(BoardModel::objectiveFor(1, 20), BoardObjective::WinLeague);
  EXPECT_EQ(BoardModel::objectiveFor(19, 20), BoardObjective::AvoidRelegation);

  // Favourites are expected to take more points at home.
  EXPECT_GT(BoardModel::expectedPoints(75.0f, 65.0f, true),
            BoardModel::expectedPoints(65.0f, 75.0f, true));
  EXPECT_GT(BoardModel::expectedPoints(70.0f, 70.0f, true),
            BoardModel::expectedPoints(70.0f, 70.0f, false));

  // A few bad months only warn; a sustained collapse ends in dismissal.
  int reviews_until_dismissal = 0;
  bool warned = false;
  for (int month = 0; month < 12 && !board.dismissed; ++month)
  {
    for (int match = 0; match < 4; ++match)
      BoardModel::recordMatch(board, 0.0f, 2.0f);
    const BoardReviewOutcome outcome =
        BoardModel::monthlyReview(board, 20, 20, true, true);
    warned |= outcome == BoardReviewOutcome::Warning;
    ++reviews_until_dismissal;
  }
  EXPECT_TRUE(warned);
  EXPECT_TRUE(board.dismissed);
  EXPECT_GE(reviews_until_dismissal, 4);

  BoardState happy;
  happy.target_position = 10;
  for (int month = 0; month < 10; ++month)
  {
    for (int match = 0; match < 4; ++match)
      BoardModel::recordMatch(happy, 3.0f, 1.3f);
    EXPECT_EQ(BoardModel::monthlyReview(happy, 3, 20, false, false),
              BoardReviewOutcome::Satisfied);
  }
  EXPECT_GT(happy.confidence, 70.0f);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

TEST(WorldPersistenceTest, WorldStateSurvivesSaveAndLoad)
{
  const SlotCleanup slot{uniqueSlot(0)};
  auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID managed = controller->getTeams().front().get().getId();
  controller->selectManagedTeam(managed);
  controller->advanceDay();
  ASSERT_FALSE(controller->getInbox().empty()) << "board objective expected";
  const std::uint32_t read_id = controller->getInbox().front().id;
  ASSERT_TRUE(controller->markInboxMessageRead(read_id));

  const PlayerID player_id =
      controller->getPlayersForTeam(managed).front().get().getId();
  Player& player = gamedata->getPlayers().at(player_id);
  PlayerDynamics& dynamics = player.mutableDynamics();
  dynamics.injury = InjuryType::HamstringStrain;
  dynamics.injury_days = 12;
  dynamics.morale = 33.5f;
  player.pushMatchRating(7.4f);
  player.pushMatchRating(6.1f);
  ASSERT_TRUE(controller->setTicketPrice(77));

  const Player before = player;
  const Team& team = controller->getManagedTeam()->get();
  const ClubProfile profile = team.getProfile();
  const std::string form = team.getRecentForm();
  const auto balance = team.getFinances().getBalance();
  const auto ledger = team.getFinances().getLedger().size();
  const auto inbox_size = controller->getInbox().size();
  const auto unread = controller->getUnreadInboxCount();
  const BoardState board = controller->getBoardState();
  const auto seed = controller->getWorldSeed();
  const auto next_id = gamedata->peekNextPlayerId();
  controller->saveGame();

  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(slot.slot));
  gamedata = controller->getGameData();
  const Player& after = gamedata->getPlayers().at(player_id);
  EXPECT_FLOAT_EQ(after.getPotential(), before.getPotential());
  EXPECT_EQ(after.getTraits().ambition, before.getTraits().ambition);
  EXPECT_EQ(after.getTraits().injury_proneness,
            before.getTraits().injury_proneness);
  EXPECT_EQ(after.getDynamics().injury, InjuryType::HamstringStrain);
  EXPECT_EQ(after.getDynamics().injury_days, 12);
  EXPECT_NEAR(after.getDynamics().morale, 33.5f, 0.01f);
  EXPECT_NEAR(after.getDynamics().condition, before.getDynamics().condition,
              0.01f);
  EXPECT_EQ(after.getDynamics().rating_count, 2);
  EXPECT_NEAR(after.getForm(), before.getForm(), 0.01f);
  EXPECT_FALSE(controller->isPlayerAvailable(player_id));

  const Team& reloaded = controller->getManagedTeam()->get();
  EXPECT_EQ(reloaded.getProfile().ticket_price, 77u);
  EXPECT_EQ(reloaded.getReputation(), profile.reputation);
  EXPECT_EQ(reloaded.getStadiumCapacity(), profile.stadium_capacity);
  EXPECT_EQ(reloaded.getProfile().youth_facilities, profile.youth_facilities);
  EXPECT_EQ(reloaded.getRecentForm(), form);
  EXPECT_EQ(reloaded.getFinances().getBalance(), balance);
  EXPECT_EQ(reloaded.getFinances().getLedger().size(), ledger);

  EXPECT_EQ(controller->getInbox().size(), inbox_size);
  EXPECT_EQ(controller->getUnreadInboxCount(), unread);
  EXPECT_TRUE(controller->getInbox().front().read);
  EXPECT_EQ(controller->getBoardState().team_id, board.team_id);
  EXPECT_EQ(controller->getBoardState().objective, board.objective);
  EXPECT_NEAR(controller->getBoardState().confidence, board.confidence, 0.01f);
  EXPECT_EQ(controller->getWorldSeed(), seed);
  EXPECT_EQ(controller->getGameData()->peekNextPlayerId(), next_id);
}

TEST(WorldPersistenceTest, RetiredAndNewPlayersArePersisted)
{
  const SlotCleanup slot{uniqueSlot(0)};
  auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID team_id = controller->getTeams().front().get().getId();
  Team& team = gamedata->getTeams().at(team_id);

  // A youth player created after generation is inserted on save.
  Player youth(gamedata->allocatePlayerId(), team_id, "New", "Talent",
               PlayerRole::CM, Language::IT, 500, 0, 16, 3, 175, Foot::Right,
               controller->getPlayersForTeam(team_id).front().get().getStats());
  youth.setPotential(80.0f);
  const PlayerID youth_id = youth.getId();
  gamedata->addPlayer(youth_id, youth);
  team.addPlayerID(youth_id);

  // A retired player is deleted on save.
  const PlayerID retired_id =
      controller->getPlayersForTeam(team_id).front().get().getId();
  ASSERT_NE(retired_id, youth_id);
  team.removePlayerID(retired_id);
  team.generateStartingXI(*gamedata, gamedata->getStatsConfig());
  ASSERT_TRUE(gamedata->removePlayer(retired_id));
  controller->saveGame();

  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(slot.slot));
  EXPECT_TRUE(controller->getGameData()->getPlayer(youth_id).has_value());
  EXPECT_FALSE(controller->getGameData()->getPlayer(retired_id).has_value());
  EXPECT_GT(controller->getGameData()->peekNextPlayerId(), youth_id);
}

TEST(WorldPersistenceTest, LegacySavesAreMigrated)
{
  const SlotCleanup slot{uniqueSlot(0)};
  auto controller = makeWorld(slot.slot);
  const TeamID team_id = controller->getTeams().front().get().getId();
  const PlayerID player_id =
      controller->getPlayersForTeam(team_id).front().get().getId();
  controller->saveGame();
  const std::int64_t balance =
      controller->getTeamById(team_id)->get().getFinances().getBalance();
  controller.reset();

  // Strip everything the world simulation added to the schema.
  sqlite3* db = nullptr;
  ASSERT_EQ(sqlite3_open(RuntimePaths::savePath(slot.slot).c_str(), &db),
            SQLITE_OK);
  for (const char* sql :
       {"DROP TABLE FinanceLedger;", "DROP TABLE InboxMessages;",
        "DROP TABLE BoardState;", "DROP TABLE WorldState;",
        "ALTER TABLE Players DROP COLUMN potential;",
        "ALTER TABLE Players DROP COLUMN traits;",
        "ALTER TABLE Players DROP COLUMN dynamics;",
        "ALTER TABLE Teams DROP COLUMN reputation;",
        "ALTER TABLE Teams DROP COLUMN stadium_capacity;",
        "ALTER TABLE Teams DROP COLUMN ticket_price;",
        "ALTER TABLE Teams DROP COLUMN training_facilities;",
        "ALTER TABLE Teams DROP COLUMN youth_facilities;",
        "ALTER TABLE Teams DROP COLUMN transfer_budget;",
        "ALTER TABLE Teams DROP COLUMN wage_budget;",
        "ALTER TABLE Teams DROP COLUMN recent_form;"})
    execSql(db, sql);
  sqlite3_close(db);

  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(slot.slot));
  const Team& team = controller->getTeamById(team_id)->get();
  EXPECT_GT(team.getReputation(), 0);
  EXPECT_GT(team.getStadiumCapacity(), 0u);
  EXPECT_GT(team.getFinances().getWageBudget(), 0);
  EXPECT_EQ(team.getFinances().getBalance(), balance);
  EXPECT_EQ(team.getFinances().ledgerTotal(), balance);
  EXPECT_GT(
      controller->getGameData()->getPlayer(player_id)->get().getPotential(),
      0.0f);
  const std::uint64_t migrated_seed = controller->getWorldSeed();
  controller->advanceDay();
  controller->saveGame();

  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(slot.slot));
  EXPECT_EQ(controller->getWorldSeed(), migrated_seed);
  const Finances& finances =
      controller->getTeamById(team_id)->get().getFinances();
  EXPECT_EQ(finances.getBalance(), finances.ledgerTotal());
}

// ---------------------------------------------------------------------------
// Opt-in timing of a complete headless season (match engine included).
// Run with FM_SEASON_TIMING=1 ctest --test-dir build -R FullSeasonTiming
// ---------------------------------------------------------------------------

TEST(WorldSimulationTest, FullSeasonTiming)
{
  const SlotCleanup slot{uniqueSlot(0)};
  if (!std::getenv("FM_SEASON_TIMING"))
    GTEST_SKIP() << "set FM_SEASON_TIMING=1 to time a full headless season";
  const auto controller = makeWorld(slot.slot);
  const auto started = std::chrono::steady_clock::now();
  for (int day = 0; day < 365; ++day) controller->advanceDay();
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();
  std::cout << "[season] 365 days, all leagues, AI only: " << seconds << " s\n";
  printLeagueFinances(*controller);
  std::size_t injured = 0;
  for (const auto& [id, player] : controller->getGameData()->getPlayers())
  {
    if (player.getDynamics().last_injury != InjuryType::None) ++injured;
  }
  std::cout << "[season] players injured at least once: " << injured << " of "
            << controller->getGameData()->getPlayers().size() << "\n";
  for (const auto& team : controller->getTeams())
  {
    const Finances& finances = team.get().getFinances();
    EXPECT_EQ(finances.getBalance(), finances.ledgerTotal());
  }
}

TEST(WorldGenerationTest, ExcludedNamesAreHashedAndNeverGenerated)
{
  // The pack stores only hashes of normalised full names ("Zzz Testname" is
  // a fictional entry kept on the list for this test).
  EXPECT_EQ(NamePool::normalizeName("  Zzz   TESTNAME "), "zzz testname");
  EXPECT_EQ(NamePool::normalizeName("Zzz Téstnâme"), "zzz testname");
  EXPECT_EQ(NamePool::normalizeName("Øyvind Łukasz Straße"),
            "oyvind lukasz strasse");
  EXPECT_EQ(NamePool::nameHash("Zzz Testname"),
            NamePool::nameHash("zzz  téstname"));
  EXPECT_NE(NamePool::nameHash("Zzz Testname"),
            NamePool::nameHash("Zzz Testnam"));

  const NamePool& pool = NamePool::instance();
  EXPECT_TRUE(pool.isExcluded("Zzz Testname"));
  EXPECT_TRUE(pool.isExcluded("ZZZ TÉSTNAME"));
  EXPECT_FALSE(pool.isExcluded("Zzz Testnam"));
  NameRegistry registry;
  EXPECT_FALSE(registry.isAvailable("Zzz Testname"));
  EXPECT_TRUE(registry.isAvailable("Zzz Testnam"));

  // The asset holds no plain list of names to avoid.
  std::ifstream file(AssetPaths::firstNames());
  const auto json = nlohmann::json::parse(file);
  EXPECT_FALSE(json.contains("excluded_full_names"));
  ASSERT_TRUE(json.contains("excluded_name_hashes"));
  EXPECT_EQ(json.at("excluded_name_hashes").size(),
            pool.excluded_name_hashes.size());
  for (const auto& hash : json.at("excluded_name_hashes"))
    EXPECT_EQ(hash.get<std::string>().size(), 16u);

  // Draws never hand out an excluded name, even with a tiny pool.
  WorldRng rng(99);
  NameRegistry names;
  for (int i = 0; i < 3000; ++i)
  {
    SquadSurnames squad;
    const auto [first, last] =
        WorldGeneration::drawName(rng, Language::IT, names, squad);
    EXPECT_FALSE(pool.isExcluded(first + " " + last));
  }
}

TEST(WorldPersistenceTest, FixtureLeftOn30JuneCountsBeforeTheSeasonEnds)
{
  const SlotCleanup slot{uniqueSlot(7)};
  {
    auto controller = makeWorld(slot.slot);
    controller->selectManagedTeam(controller->getTeams().front().get().getId());
    ASSERT_TRUE(controller->saveGame());
  }
  // The last days of the season.
  sqlite3* db = nullptr;
  ASSERT_EQ(sqlite3_open(RuntimePaths::savePath(slot.slot).c_str(), &db),
            SQLITE_OK);
  ASSERT_EQ(sqlite3_exec(db, "UPDATE GameState SET game_date = '2026-06-29';",
                         nullptr, nullptr, nullptr),
            SQLITE_OK);
  sqlite3_close(db);
  GameController controller;
  ASSERT_TRUE(controller.loadGame(slot.slot));
  ASSERT_EQ(controller.getCurrentDate(), GameDateValue(2026, 6, 29));
  const TeamID managed = controller.getManagedTeam()->get().getId();
  TeamID opponent = 0;
  for (const auto& team : controller.getTeams())
    if (team.get().getId() != managed &&
        team.get().getId() != FREE_AGENTS_TEAM_ID)
    {
      opponent = team.get().getId();
      break;
    }
  ASSERT_NE(opponent, 0);
  const GameDateValue last_day(2026, 6, 30);
  controller.getGame()->getCalendar().addMatch(
      Match(managed, opponent, last_day, MatchType::FRIENDLY));

  // The manager leaves it unplayed on its day; the assistant plays it the
  // next morning, before the new season replaces the calendar.
  controller.advanceDay();
  EXPECT_FALSE(controller.getMatchReport(last_day, managed, opponent));
  controller.advanceDay();
  ASSERT_EQ(controller.getCurrentDate(), GameDateValue(2026, 7, 1));
  const auto report = controller.getMatchReport(last_day, managed, opponent);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->home_team_id, managed);
  EXPECT_FALSE(report->players.empty());
}
