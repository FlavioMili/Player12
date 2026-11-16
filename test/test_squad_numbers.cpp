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
#include <bitset>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "controller/game_controller.h"
#include "database/database_connection.h"
#include "database/gamedata.h"
#include "database/migrations/migrations.h"
#include "database/save_manager.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/render/match_shirt_numbers.h"
#include "model/squad_numbers.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

using SquadNumbers::Entry;

Entry entry(PlayerID id, PlayerRole role, double overall, int age = 26,
            std::uint8_t number = 0)
{
  Entry result;
  result.id = id;
  result.role = role;
  result.overall = overall;
  result.age = age;
  result.number = number;
  return result;
}

int numberOf(const std::vector<Entry>& squad, PlayerID id)
{
  const auto found = std::ranges::find(squad, id, &Entry::id);
  return found == squad.end() ? -1 : found->number;
}

/** Every number valid and worn once. */
void expectUnique(const std::vector<Entry>& squad)
{
  std::bitset<SquadNumbers::MAX_NUMBER + 1> worn;
  for (const Entry& player : squad)
  {
    ASSERT_TRUE(SquadNumbers::isValid(player.number)) << player.id;
    EXPECT_FALSE(worn.test(player.number)) << "number " << int{player.number};
    worn.set(player.number);
  }
}

/** A usual senior squad: 25 players, best first within each position. */
std::vector<Entry> typicalSquad()
{
  return {entry(1, PlayerRole::GK, 75),      entry(2, PlayerRole::GK, 68),
          entry(3, PlayerRole::GK, 55, 19),  entry(4, PlayerRole::RB, 72),
          entry(5, PlayerRole::RB, 64),      entry(6, PlayerRole::LB, 71),
          entry(7, PlayerRole::LB, 60),      entry(8, PlayerRole::CB, 78),
          entry(9, PlayerRole::CB, 74),      entry(10, PlayerRole::CB, 66),
          entry(11, PlayerRole::CB, 58, 18), entry(12, PlayerRole::CDM, 73),
          entry(13, PlayerRole::CDM, 63),    entry(14, PlayerRole::CM, 76),
          entry(15, PlayerRole::CM, 69),     entry(16, PlayerRole::CM, 57, 19),
          entry(17, PlayerRole::CAM, 79),    entry(18, PlayerRole::CAM, 65),
          entry(19, PlayerRole::RW, 77),     entry(20, PlayerRole::RM, 62),
          entry(21, PlayerRole::LW, 74),     entry(22, PlayerRole::LM, 61),
          entry(23, PlayerRole::ST, 81),     entry(24, PlayerRole::ST, 70),
          entry(25, PlayerRole::ST, 59, 17)};
}

/** Senior squad of a club as numbered entries. */
std::vector<Entry> clubNumbers(const GameData& data, TeamID team_id)
{
  std::vector<Entry> squad;
  for (const auto& ref : data.getPlayersForTeam(team_id))
  {
    const Player& player = ref.get();
    squad.push_back(entry(player.getId(), player.getRole(),
                          player.getOverall(data.getStatsConfig()),
                          player.getAge(), player.getSquadNumber()));
  }
  return squad;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { SaveManager::deleteSave(RuntimePaths::savePath(slot)); }
};

std::unique_ptr<GameController> makeCareer(int slot)
{
  Logger::init();
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot, WORLD_SEED);
  controller->selectManagedTeam(controller->getTeams().front().get().getId());
  return controller;
}

/** Number of every player of every club (free agents left out). */
std::map<PlayerID, int> allNumbers(const GameData& data)
{
  std::map<PlayerID, int> numbers;
  for (const auto& [id, player] : data.getPlayers())
    if (player.getTeamId() != FREE_AGENTS_TEAM_ID)
      numbers[id] = player.getSquadNumber();
  return numbers;
}

/** Every club's senior squad wears unique, valid numbers. */
void expectEveryClubNumbered(const GameData& data)
{
  for (const auto& team : data.getTeamsVector())
  {
    const TeamID id = team.get().getId();
    if (id == FREE_AGENTS_TEAM_ID) continue;
    SCOPED_TRACE(team.get().getName());
    expectUnique(clubNumbers(data, id));
  }
}
}  // namespace

TEST(SquadNumbers, NewSquadIsNumberedTheTraditionalWay)
{
  std::vector<Entry> squad = typicalSquad();
  SquadNumbers::assign(squad);
  expectUnique(squad);
  EXPECT_EQ(numberOf(squad, 1), 1);  // first goalkeeper
  EXPECT_EQ(numberOf(squad, 4), 2);  // right back
  EXPECT_EQ(numberOf(squad, 6), 3);  // left back
  EXPECT_EQ(numberOf(squad, 8), 4);  // centre backs
  EXPECT_EQ(numberOf(squad, 9), 5);
  EXPECT_EQ(numberOf(squad, 12), 6);   // holding midfielder
  EXPECT_EQ(numberOf(squad, 19), 7);   // right wing
  EXPECT_EQ(numberOf(squad, 14), 8);   // centre midfielder
  EXPECT_EQ(numberOf(squad, 23), 9);   // striker
  EXPECT_EQ(numberOf(squad, 17), 10);  // playmaker
  EXPECT_EQ(numberOf(squad, 21), 11);  // left wing
  EXPECT_EQ(numberOf(squad, 2), 12);   // back-up goalkeepers
  EXPECT_EQ(numberOf(squad, 3), 13);
  for (const Entry& player : squad)
  {
    if (player.number <= 13) continue;
    // Other seniors from 14, youngsters from 30.
    if (player.age <= SquadNumbers::YOUNG_AGE)
      EXPECT_GE(player.number, 30) << player.id;
    else
      EXPECT_LT(player.number, 30) << player.id;
  }

  // The order of the players does not matter.
  std::vector<Entry> shuffled = typicalSquad();
  std::mt19937 engine(3);
  std::ranges::shuffle(shuffled, engine);
  SquadNumbers::assign(shuffled);
  for (const Entry& player : squad)
    EXPECT_EQ(numberOf(shuffled, player.id), player.number) << player.id;
}

TEST(SquadNumbers, NewcomersKeepTheirNumberWhenFree)
{
  std::vector<Entry> squad = typicalSquad();
  SquadNumbers::assign(squad);
  // A striker who wore 99 keeps it; one who wore 9 (taken) does not take
  // the captain's shirt but a free number.
  Entry keeps = entry(30, PlayerRole::ST, 80);
  keeps.preferred = 99;
  Entry clashes = entry(31, PlayerRole::ST, 82);
  clashes.preferred = 9;
  squad.push_back(keeps);
  squad.push_back(clashes);
  SquadNumbers::assign(squad);
  expectUnique(squad);
  EXPECT_EQ(numberOf(squad, 30), 99);
  EXPECT_EQ(numberOf(squad, 23), 9);
  EXPECT_NE(numberOf(squad, 31), 9);
  // A free traditional number goes to a newcomer of that position.
  squad.erase(std::ranges::find(squad, 17, &Entry::id));  // the 10 leaves
  squad.push_back(entry(32, PlayerRole::CAM, 70));
  SquadNumbers::assign(squad);
  EXPECT_EQ(numberOf(squad, 32), 10);
  // Two players sharing a number: the better one keeps it.
  std::vector clash = {entry(1, PlayerRole::ST, 60, 26, 9),
                       entry(2, PlayerRole::ST, 70, 26, 9)};
  SquadNumbers::assign(clash);
  EXPECT_EQ(numberOf(clash, 2), 9);
  EXPECT_NE(numberOf(clash, 1), 9);
  expectUnique(clash);
}

TEST(SquadNumbers, MatchShirtsShowSquadNumbers)
{
  const std::map<std::string, float> stats;
  Player keeper(1, 1, "A", "Keeper", PlayerRole::GK, Language::EN, 0, 0, 25, 2,
                185, Foot::Right, stats);
  Player striker(2, 1, "B", "Striker", PlayerRole::ST, Language::EN, 0, 0, 25,
                 2, 180, Foot::Right, stats);
  Player youngster(3, 1, "C", "Youngster", PlayerRole::CM, Language::EN, 0, 0,
                   17, 1, 175, Foot::Right, stats);
  Player visitor(4, 2, "D", "Visitor", PlayerRole::ST, Language::EN, 0, 0, 25,
                 2, 180, Foot::Right, stats);
  keeper.setSquadNumber(1);
  striker.setSquadNumber(23);
  visitor.setSquadNumber(23);
  MatchShirtNumbers numbers;
  const auto shirt = [&numbers](const Player& player, bool home)
  {
    MatchRenderPlayer view;
    view.player = &player;
    view.isHomeTeam = home;
    return numbers.numberFor(view, nullptr);
  };
  EXPECT_EQ(shirt(striker, true), 23);
  EXPECT_EQ(shirt(keeper, true), 1);
  EXPECT_EQ(shirt(visitor, false), 23);  // each side has its own numbers
  // Without a squad number: a spare one that squad numbers rarely use.
  EXPECT_EQ(shirt(youngster, true), 50);
  EXPECT_EQ(shirt(striker, true), 23);  // stable within the match
  numbers.reset();
  EXPECT_EQ(shirt(youngster, true), 50);

  // An academy keeper called up takes a free back-up keeper number; the
  // squad numbers of the players in the match are reserved first, so the
  // teammate who wears 12 keeps it even when asked for later.
  Player academyKeeper(5, 1, "E", "Academy", PlayerRole::GK, Language::EN, 0, 0,
                       17, 1, 186, Foot::Right, stats);
  Player backup(6, 1, "F", "Backup", PlayerRole::GK, Language::EN, 0, 0, 29, 2,
                188, Foot::Right, stats);
  backup.setSquadNumber(12);
  MatchRenderSnapshot snapshot;
  for (const Player* player : {&keeper, &backup, &academyKeeper})
  {
    MatchRenderPlayer& view = snapshot.players.emplace_back();
    view.player = player;
    view.isHomeTeam = true;
  }
  numbers.reset(snapshot);
  EXPECT_EQ(shirt(academyKeeper, true), 13);
  EXPECT_EQ(shirt(backup, true), 12);
  EXPECT_EQ(shirt(keeper, true), 1);
}

TEST(SquadNumbers, GeneratedWorldNumbersEveryClub)
{
  auto controller = makeCareer(0);
  const GameData& data = *controller->getGameData();
  expectEveryClubNumbered(data);
  // The best goalkeeper of each club wears 1.
  int clubs_checked = 0;
  for (const auto& team : data.getTeamsVector())
  {
    const TeamID id = team.get().getId();
    if (id == FREE_AGENTS_TEAM_ID) continue;
    const std::vector<Entry> squad = clubNumbers(data, id);
    const Entry* best_keeper = nullptr;
    for (const Entry& player : squad)
      if (player.role == PlayerRole::GK &&
          (!best_keeper || player.overall > best_keeper->overall))
        best_keeper = &player;
    if (!best_keeper) continue;
    EXPECT_EQ(best_keeper->number, 1) << team.get().getName();
    ++clubs_checked;
  }
  EXPECT_GT(clubs_checked, 100);
}

TEST(SquadNumbers, TransfersAndManagerChangesKeepNumbersUnique)
{
  auto controller = makeCareer(0);
  GameData& data = *controller->getGameData();
  const TeamID managed = controller->getManagedTeam()->get().getId();
  const auto& teams = data.getTeamsVector();
  const auto other =
      std::ranges::find_if(teams,
                           [managed](const auto& team)
                           {
                             return team.get().getId() != managed &&
                                    team.get().getId() != FREE_AGENTS_TEAM_ID;
                           });
  ASSERT_NE(other, teams.end());
  const TeamID seller = other->get().getId();

  // The seller's number 1 joins the managed club, where 1 is taken.
  PlayerID keeper = 0;
  for (const auto& ref : data.getPlayersForTeam(seller))
    if (ref.get().getSquadNumber() == 1) keeper = ref.get().getId();
  ASSERT_NE(keeper, 0u);
  data.transferPlayer(keeper, managed);
  EXPECT_NE(data.getPlayer(keeper)->get().getSquadNumber(), 1);
  expectUnique(clubNumbers(data, managed));

  // A player whose number is free at his new club keeps it.
  std::bitset<SquadNumbers::MAX_NUMBER + 1> worn;
  for (const auto& ref : data.getPlayersForTeam(managed))
    worn.set(ref.get().getSquadNumber());
  PlayerID free_number = 0;
  for (const auto& ref : data.getPlayersForTeam(seller))
    if (!worn.test(ref.get().getSquadNumber())) free_number = ref.get().getId();
  ASSERT_NE(free_number, 0u);
  const int kept = data.getPlayer(free_number)->get().getSquadNumber();
  data.transferPlayer(free_number, managed);
  EXPECT_EQ(data.getPlayer(free_number)->get().getSquadNumber(), kept);
  expectUnique(clubNumbers(data, managed));
  expectUnique(clubNumbers(data, seller));

  // The manager picks numbers: a free one, then one a teammate wears.
  const auto& squad = data.getPlayersForTeam(managed);
  const PlayerID first = squad[0].get().getId();
  const PlayerID second = squad[1].get().getId();
  int spare = 99;
  while (worn.test(static_cast<std::size_t>(spare)) || spare == kept) --spare;
  EXPECT_EQ(controller->setSquadNumber(first, spare),
            SquadNumbers::Change::Changed);
  EXPECT_EQ(data.getPlayer(first)->get().getSquadNumber(), spare);
  const int theirs = data.getPlayer(second)->get().getSquadNumber();
  EXPECT_EQ(controller->setSquadNumber(first, theirs),
            SquadNumbers::Change::Swapped);
  EXPECT_EQ(data.getPlayer(first)->get().getSquadNumber(), theirs);
  EXPECT_EQ(data.getPlayer(second)->get().getSquadNumber(), spare);
  expectUnique(clubNumbers(data, managed));
  // Out of range, or another club's player: refused.
  EXPECT_EQ(controller->setSquadNumber(first, 0),
            SquadNumbers::Change::Invalid);
  EXPECT_EQ(controller->setSquadNumber(first, 100),
            SquadNumbers::Change::Invalid);
  const PlayerID foreign = data.getPlayersForTeam(seller).front().get().getId();
  EXPECT_EQ(controller->setSquadNumber(foreign, 77),
            SquadNumbers::Change::Invalid);
}

TEST(SquadNumbers, NumbersSurviveSaveAndLoad)
{
  const SlotCleanup slot{61};
  std::map<PlayerID, int> before;
  PlayerID changed = 0;
  {
    auto controller = makeCareer(slot.slot);
    const TeamID managed = controller->getManagedTeam()->get().getId();
    changed = controller->getPlayersForTeam(managed).front().get().getId();
    const auto& squad = controller->getPlayersForTeam(managed);
    const int target = squad.back().get().getSquadNumber();
    ASSERT_NE(controller->setSquadNumber(changed, target),
              SquadNumbers::Change::Invalid);
    controller->advanceDay();
    ASSERT_TRUE(controller->saveGame());
    before = allNumbers(*controller->getGameData());
  }
  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  EXPECT_EQ(allNumbers(*reloaded.getGameData()), before);
  EXPECT_NE(before.at(changed), 0);
}

TEST(SquadNumbers, MigrationAddsTheColumnOnce)
{
  Logger::init();
  DatabaseConnection connection(":memory:");
  Migrations::migrate(connection);
  sqlite3* db = connection.getRaw();
  // A version 12 save: players without squad numbers.
  for (const char* sql : {"ALTER TABLE Players DROP COLUMN squad_number;",
                          "DELETE FROM schema_migrations WHERE number >= 13;",
                          "UPDATE save_meta SET schema_version = 12;"})
    ASSERT_EQ(sqlite3_exec(db, sql, nullptr, nullptr, nullptr), SQLITE_OK)
        << sql << ": " << sqlite3_errmsg(db);
  ASSERT_FALSE(Migrations::columnExists(db, "Players", "squad_number"));
  const auto report = Migrations::migrate(connection);
  EXPECT_EQ(report.from_version, 12);
  EXPECT_TRUE(std::ranges::contains(report.applied, 13));
  EXPECT_TRUE(Migrations::columnExists(db, "Players", "squad_number"));
  const auto again = Migrations::migrate(connection);
  EXPECT_TRUE(again.applied.empty());
  EXPECT_TRUE(again.repaired.empty());
}

TEST(SquadNumbers, SaveFromBeforeSquadNumbersLoadsNumbered)
{
  const SlotCleanup slot{62};
  {
    auto controller = makeCareer(slot.slot);
    controller->advanceDay();
    ASSERT_TRUE(controller->saveGame());
  }
  // Turn it into a version 12 save: no squad_number column.
  {
    sqlite3* db = nullptr;
    ASSERT_EQ(
        sqlite3_open(RuntimePaths::savePath(slot.slot).string().c_str(), &db),
        SQLITE_OK);
    for (const char* sql : {"ALTER TABLE Players DROP COLUMN squad_number;",
                            "DELETE FROM schema_migrations WHERE number >= 13;",
                            "UPDATE save_meta SET schema_version = 12;"})
      EXPECT_EQ(sqlite3_exec(db, sql, nullptr, nullptr, nullptr), SQLITE_OK)
          << sql << ": " << sqlite3_errmsg(db);
    sqlite3_close(db);
  }
  GameController loaded;
  ASSERT_TRUE(loaded.loadGame(slot.slot))
      << loaded.getLastLoadError().value_or(SaveError{}).detail;
  expectEveryClubNumbered(*loaded.getGameData());
  // Saving writes the numbers; the next load reads them back unchanged.
  const std::map<PlayerID, int> numbered = allNumbers(*loaded.getGameData());
  ASSERT_TRUE(loaded.saveGame());
  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  EXPECT_EQ(allNumbers(*reloaded.getGameData()), numbered);
}
