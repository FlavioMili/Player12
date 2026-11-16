// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "controller/game_controller.h"
#include "database/database_connection.h"
#include "database/gamedata.h"
#include "database/migrations/migrations.h"
#include "database/repositories/fixture_repository.h"
#include "database/repositories/player_repository.h"
#include "database/repositories/world_state_repository.h"
#include "database/save_manager.h"
#include "global/global.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/board.h"
#include "model/calendar.h"
#include "model/data_hub.h"
#include "model/guidance.h"
#include "model/match.h"
#include "model/player.h"
#include "model/world_rng.h"
#include "model/youth_academy.h"

namespace fs = std::filesystem;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

/** Deletes a slot with its backups when the test ends. */
struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { SaveManager::deleteSave(RuntimePaths::savePath(slot)); }
};

/** Resets the fault hook even when an assertion fails. */
struct FaultHookGuard
{
  explicit FaultHookGuard(SaveManager::FaultHook hook)
  {
    SaveManager::setFaultHook(std::move(hook));
  }
  FaultHookGuard(const FaultHookGuard&) = delete;
  FaultHookGuard& operator=(const FaultHookGuard&) = delete;

  ~FaultHookGuard() { SaveManager::setFaultHook(nullptr); }
};

std::unique_ptr<GameController> makeCareer(int slot)
{
  Logger::init();
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot, WORLD_SEED);
  controller->selectManagedTeam(controller->getTeams().front().get().getId());
  return controller;
}

void advance(GameController& controller, int days)
{
  for (int day = 0; day < days; ++day) controller.advanceDay();
}

std::string fileBytes(const fs::path& path)
{
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void execSql(sqlite3* db, const std::string& sql)
{
  char* error = nullptr;
  ASSERT_EQ(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error), SQLITE_OK)
      << sql << ": " << (error ? error : "");
  sqlite3_free(error);
}

int queryInt(sqlite3* db, const std::string& sql)
{
  sqlite3_stmt* stmt = nullptr;
  int value = -1;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK &&
      sqlite3_step(stmt) == SQLITE_ROW)
    value = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  return value;
}

/** Schema plus a content digest: equal before and after an idempotent run. */
std::string databaseDigest(sqlite3* db)
{
  std::string digest;
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db,
                     "SELECT name, sql FROM sqlite_master WHERE type = 'table' "
                     "AND name NOT LIKE 'sqlite_%' ORDER BY name;",
                     -1, &stmt, nullptr);
  std::vector<std::string> tables;
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    tables.emplace_back(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    digest += reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
  }
  sqlite3_finalize(stmt);
  for (const std::string& table : tables)
  {
    sqlite3_prepare_v2(db, std::format("SELECT * FROM \"{}\";", table).c_str(),
                       -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      for (int column = 0; column < sqlite3_column_count(stmt); ++column)
      {
        const auto* text =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, column));
        digest += text ? text : "<null>";
        digest += '|';
      }
    }
    sqlite3_finalize(stmt);
  }
  return digest;
}

/** Opens a closed save file directly, like a hand-edited save. */
class RawDb
{
 public:
  explicit RawDb(const fs::path& path)
  {
    if (sqlite3_open(path.string().c_str(), &db) != SQLITE_OK)
      throw std::runtime_error("cannot open " + path.string());
  }
  ~RawDb() { sqlite3_close(db); }
  RawDb(const RawDb&) = delete;
  RawDb& operator=(const RawDb&) = delete;
  sqlite3* get() const { return db; }

 private:
  sqlite3* db = nullptr;
};

// Layout of the first public saves (before competitions, the world
// simulation and versioning), reduced to what the loader reads.
constexpr const char* LEGACY_V0_SCHEMA = R"sql(
CREATE TABLE Leagues (id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL UNIQUE, parent_league_id INTEGER NULL,
  FOREIGN KEY(parent_league_id) REFERENCES Leagues(id));
CREATE TABLE Teams (id INTEGER PRIMARY KEY AUTOINCREMENT,
  league_id INTEGER NOT NULL, name TEXT NOT NULL UNIQUE,
  balance INTEGER NOT NULL DEFAULT 0, strategy TEXT DEFAULT '{}',
  lineup TEXT DEFAULT '{}', FOREIGN KEY(league_id) REFERENCES Leagues(id));
CREATE TABLE Players (id INTEGER PRIMARY KEY AUTOINCREMENT,
  team_id INTEGER NOT NULL, first_name TEXT NOT NULL, last_name TEXT NOT NULL,
  age INTEGER NOT NULL DEFAULT 18, role TEXT NOT NULL,
  nationality TEXT NOT NULL, wage INTEGER NOT NULL DEFAULT 0,
  contract_years INTEGER NOT NULL DEFAULT 1, height INTEGER NOT NULL DEFAULT 175,
  foot TEXT NOT NULL DEFAULT 'Right', stats TEXT NOT NULL,
  status INTEGER DEFAULT 0, FOREIGN KEY(team_id) REFERENCES Teams(id));
CREATE TABLE Fixtures (id INTEGER PRIMARY KEY AUTOINCREMENT,
  game_date TEXT NOT NULL, home_team_id INTEGER NOT NULL,
  away_team_id INTEGER NOT NULL, match_type INTEGER NOT NULL DEFAULT 0,
  home_goals INTEGER, away_goals INTEGER, played INTEGER NOT NULL DEFAULT 0,
  UNIQUE(game_date, home_team_id, away_team_id));
CREATE TABLE GameState (id INTEGER PRIMARY KEY, managed_team_id INTEGER,
  game_date TEXT, current_season INTEGER);
CREATE TABLE LeaguePoints (league_id INTEGER NOT NULL,
  team_id INTEGER NOT NULL, points INTEGER NOT NULL DEFAULT 0,
  goal_difference INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(league_id, team_id));
CREATE TABLE TransferList (player_id INTEGER PRIMARY KEY,
  asking_price INTEGER NOT NULL DEFAULT 0, listing_date TEXT NOT NULL);
INSERT INTO Leagues (id, name) VALUES (1, 'Old League');
INSERT INTO Teams (id, league_id, name, balance) VALUES (1, 1, 'Old Town', 5000);
INSERT INTO Players (id, team_id, first_name, last_name, role, nationality,
  stats) VALUES (1, 1, 'Ada', 'Legacy', 'ST', 'EN', '{}');
INSERT INTO Fixtures (game_date, home_team_id, away_team_id)
  VALUES ('2025-08-10', 1, 1);
INSERT INTO GameState VALUES (1, 1, '2025-07-20', 1);
INSERT INTO TransferList VALUES (1, 900, '2025-07-10');
)sql";

fs::path scratchDatabase(const char* name)
{
  const fs::path path = RuntimePaths::saveDirectory() / name;
  SaveManager::deleteSave(path);
  return path;
}

// Columns added by migrations 10-14 (U21 squads, match detail, board
// targets, squad numbers): a version 9 save has none of them.
constexpr std::array<std::pair<std::string_view, std::string_view>, 15>
    COLUMNS_AFTER_VERSION_NINE = {{
        {"YouthAcademies", "reserve_played"},
        {"YouthAcademies", "reserve_won"},
        {"YouthAcademies", "reserve_drawn"},
        {"YouthAcademies", "reserve_lost"},
        {"YouthAcademies", "reserve_goals_for"},
        {"YouthAcademies", "reserve_goals_against"},
        {"YouthAcademies", "reserves_ready"},
        {"YouthResults", "squad"},
        {"ManagedMatchAnalytics", "detail"},
        {"BoardState", "cup_objective"},
        {"BoardState", "finance_objective"},
        {"BoardState", "youth_target"},
        {"BoardState", "start_balance"},
        {"BoardState", "targets_set"},
        {"Players", "squad_number"},
    }};

/** Drops the columns of COLUMNS_AFTER_VERSION_NINE (but those of
 * @p skip_table). */
void dropColumnsAfterVersionNine(sqlite3* db, std::string_view skip_table = {})
{
  for (const auto& [table, column] : COLUMNS_AFTER_VERSION_NINE)
    if (table != skip_table)
      execSql(db, std::format("ALTER TABLE {} DROP COLUMN {};", table, column));
}

/** Turns a current database into the layout of a version 9 save. */
void downgradeToVersionNine(sqlite3* db)
{
  dropColumnsAfterVersionNine(db);
  execSql(db, "DELETE FROM schema_migrations WHERE number >= 10;");
  execSql(db, "UPDATE save_meta SET schema_version = 9;");
}

/** Turns a current save into a save from before versioning ("version 0"). */
void downgradeToVersionZero(const fs::path& save)
{
  RawDb db(save);
  // BoardState is dropped as a whole below.
  dropColumnsAfterVersionNine(db.get(), "BoardState");
  for (const char* sql :
       {"DROP TABLE schema_migrations;",
        "DROP TABLE save_meta;",
        "DROP TABLE FinanceLedger;",
        "DROP TABLE InboxMessages;",
        "DROP TABLE BoardState;",
        "DROP TABLE WorldState;",
        "DROP TABLE Staff;",
        "DROP TABLE TeamTraining;",
        "DROP TABLE PlayerTraining;",
        "ALTER TABLE Players DROP COLUMN potential;",
        "ALTER TABLE Players DROP COLUMN traits;",
        "ALTER TABLE Players DROP COLUMN dynamics;",
        "ALTER TABLE Teams DROP COLUMN reputation;",
        "ALTER TABLE Teams DROP COLUMN stadium_capacity;",
        "ALTER TABLE Teams DROP COLUMN recent_form;",
        "ALTER TABLE TransferList DROP COLUMN highest_bid;",
        "ALTER TABLE TransferOffers DROP COLUMN status;",
        "ALTER TABLE TransferOffers DROP COLUMN respond_on;",
        "ALTER TABLE PlayerMarketFlags DROP COLUMN not_for_sale_until;",
        "ALTER TABLE Leagues DROP COLUMN tiebreak;"})
    execSql(db.get(), sql);
}
}  // namespace

// ---- Migration runner
// --------------------------------------------------------

TEST(SaveMigrations, FreshDatabaseAppliesEveryMigrationOnce)
{
  Logger::init();
  DatabaseConnection connection(":memory:");
  const auto first = Migrations::migrate(connection);
  EXPECT_TRUE(first.fresh);
  EXPECT_EQ(first.from_version, 0);
  EXPECT_EQ(first.to_version, Migrations::currentSchemaVersion());
  EXPECT_EQ(first.applied.size(), Migrations::registry().size());
  sqlite3* db = connection.getRaw();
  EXPECT_EQ(queryInt(db, "SELECT COUNT(*) FROM schema_migrations;"),
            static_cast<int>(Migrations::registry().size()));
  EXPECT_EQ(queryInt(db, "SELECT schema_version FROM save_meta;"),
            Migrations::currentSchemaVersion());
  EXPECT_EQ(queryInt(db, "SELECT rng_version FROM save_meta;"),
            SaveFormat::RNG_VERSION);
  EXPECT_EQ(queryInt(db, "SELECT length(created_at_utc) > 0 FROM save_meta;"),
            1);
  EXPECT_EQ(queryInt(db, "SELECT seed_unknown FROM save_meta;"), 0);

  const std::string before = databaseDigest(db);
  const auto second = Migrations::migrate(connection);
  EXPECT_TRUE(second.applied.empty());
  EXPECT_TRUE(second.repaired.empty());
  EXPECT_EQ(second.from_version, Migrations::currentSchemaVersion());
  EXPECT_EQ(databaseDigest(db), before);
}

TEST(SaveMigrations, LegacyVersionZeroLayoutUpgradesIdempotently)
{
  Logger::init();
  const fs::path path = scratchDatabase("legacy_v0.db");
  {
    RawDb raw(path);
    execSql(raw.get(), LEGACY_V0_SCHEMA);
  }
  {
    DatabaseConnection connection(path.string());
    const auto report = Migrations::migrate(connection);
    EXPECT_FALSE(report.fresh);
    EXPECT_EQ(report.from_version, 0);
    EXPECT_EQ(report.applied.size(), Migrations::registry().size());
    sqlite3* db = connection.getRaw();
    for (const auto& [table, column] :
         {std::pair{"Leagues", "tiebreak"},
          {"Fixtures", "stage"},
          {"Fixtures", "home_penalties"},
          {"TransferList", "highest_bid"},
          {"Players", "potential"},
          {"Teams", "recent_form"},
          {"WorldState", "next_staff_id"},
          {"TransferOffers", "status"},
          {"TransferOffers", "respond_on"},
          {"PlayerMarketFlags", "not_for_sale_until"},
          {"YouthAcademies", "reserve_played"},
          {"YouthAcademies", "reserves_ready"},
          {"YouthResults", "squad"},
          {"ManagedMatchAnalytics", "detail"},
          {"BoardState", "finance_objective"},
          {"BoardState", "targets_set"},
          {"Players", "squad_number"}})
      EXPECT_TRUE(Migrations::columnExists(db, table, column))
          << table << "." << column;
    for (const char* table : {"FinanceLedger", "WorldState", "Staff",
                              "save_meta", "schema_migrations"})
      EXPECT_TRUE(Migrations::tableExists(db, table)) << table;
    // Data survives, defaults fill the new columns, no seed is invented.
    EXPECT_EQ(queryInt(db, "SELECT balance FROM Teams WHERE id = 1;"), 5000);
    EXPECT_EQ(queryInt(db, "SELECT asking_price FROM TransferList;"), 900);
    EXPECT_EQ(queryInt(db, "SELECT highest_bid FROM TransferList;"), 0);
    EXPECT_EQ(queryInt(db, "SELECT seed_unknown FROM save_meta;"), 1);
    EXPECT_EQ(queryInt(db, "SELECT length(created_at_utc) FROM save_meta;"), 0);
    EXPECT_EQ(Migrations::foreignKeyIssues(db), 0);
    EXPECT_EQ(queryInt(db,
                       "SELECT COUNT(*) FROM pragma_integrity_check "
                       "WHERE integrity_check <> 'ok';"),
              0);

    const std::string upgraded = databaseDigest(db);
    const auto again = Migrations::migrate(connection);
    EXPECT_TRUE(again.applied.empty());
    EXPECT_TRUE(again.repaired.empty());
    EXPECT_EQ(databaseDigest(db), upgraded);
  }
  SaveManager::deleteSave(path);
}

TEST(SaveMigrations, OfferNegotiationsUpgradeASaveFromBeforeThem)
{
  Logger::init();
  DatabaseConnection connection(":memory:");
  Migrations::migrate(connection);
  sqlite3* db = connection.getRaw();
  // The layout of a version 8 save: no talks state, no not-for-sale flag,
  // no rounds table, and an offer waiting for the club.
  for (const char* sql :
       {"ALTER TABLE TransferOffers DROP COLUMN status;",
        "ALTER TABLE TransferOffers DROP COLUMN respond_on;",
        "ALTER TABLE PlayerMarketFlags DROP COLUMN not_for_sale_until;",
        "DROP TABLE TransferOfferRounds;",
        "DELETE FROM schema_migrations WHERE number >= 9;",
        "UPDATE save_meta SET schema_version = 8;",
        "INSERT INTO TransferOffers (id, kind, player_id, club_id, created, "
        "expires, rounds, terms) VALUES (7, 0, 1, 2, 20250710, 20250715, 0, "
        "'{\"offer\":{\"fee\":1000000},\"max_fee\":1200000}');",
        "INSERT INTO PlayerMarketFlags (player_id, loan_listed) VALUES (1, "
        "1);"})
    execSql(db, sql);

  const auto report = Migrations::migrate(connection);
  EXPECT_EQ(report.from_version, 8);
  // Migration 9 and every later one run again.
  ASSERT_FALSE(report.applied.empty());
  EXPECT_EQ(report.applied.front(), 9);
  for (const auto& [table, column] :
       {std::pair{"TransferOffers", "status"},
        {"TransferOffers", "respond_on"},
        {"PlayerMarketFlags", "not_for_sale_until"}})
    EXPECT_TRUE(Migrations::columnExists(db, table, column))
        << table << "." << column;
  EXPECT_TRUE(Migrations::tableExists(db, "TransferOfferRounds"));
  // The old offer waits for the club and nobody is declared not for sale.
  EXPECT_EQ(queryInt(db, "SELECT status FROM TransferOffers WHERE id = 7;"), 0);
  EXPECT_EQ(queryInt(db, "SELECT respond_on FROM TransferOffers WHERE id = 7;"),
            0);
  EXPECT_EQ(queryInt(db,
                     "SELECT not_for_sale_until FROM PlayerMarketFlags "
                     "WHERE player_id = 1;"),
            0);
  EXPECT_EQ(queryInt(db, "SELECT loan_listed FROM PlayerMarketFlags;"), 1);

  const std::string upgraded = databaseDigest(db);
  const auto again = Migrations::migrate(connection);
  EXPECT_TRUE(again.applied.empty());
  EXPECT_TRUE(again.repaired.empty());
  EXPECT_EQ(databaseDigest(db), upgraded);
}

TEST(SaveMigrations, BoardObjectivesUpgradeASaveFromBeforeThem)
{
  Logger::init();
  DatabaseConnection connection(":memory:");
  Migrations::migrate(connection);
  sqlite3* db = connection.getRaw();
  // A version 11 save: a board with a league objective only.
  for (const char* sql :
       {"ALTER TABLE BoardState DROP COLUMN cup_objective;",
        "ALTER TABLE BoardState DROP COLUMN finance_objective;",
        "ALTER TABLE BoardState DROP COLUMN youth_target;",
        "ALTER TABLE BoardState DROP COLUMN start_balance;",
        "DELETE FROM schema_migrations WHERE number >= 12;",
        "UPDATE save_meta SET schema_version = 11;",
        "INSERT INTO BoardState (id, team_id, season_year, objective, "
        "expected_position, target_position, confidence) VALUES (1, 4, 2025, "
        "2, 8, 10, 61.5);"})
    execSql(db, sql);

  const auto report = Migrations::migrate(connection);
  EXPECT_EQ(report.from_version, 11);
  ASSERT_FALSE(report.applied.empty());
  EXPECT_EQ(report.applied.front(), 12);
  for (const char* column :
       {"cup_objective", "finance_objective", "youth_target", "start_balance"})
    EXPECT_TRUE(Migrations::columnExists(db, "BoardState", column)) << column;
  // The old board keeps its league objective; the new targets start empty
  // and are set at the next season start.
  EXPECT_EQ(queryInt(db, "SELECT objective FROM BoardState WHERE id = 1;"), 2);
  EXPECT_EQ(queryInt(db, "SELECT cup_objective FROM BoardState;"), 0);
  EXPECT_EQ(queryInt(db, "SELECT youth_target FROM BoardState;"), 0);
  EXPECT_EQ(queryInt(db, "SELECT targets_set FROM BoardState;"), 0)
      << "the board never announced them";
  EXPECT_TRUE(Migrations::tableExists(db, "SeasonTables"));
  EXPECT_TRUE(Migrations::tableExists(db, "SeasonReviews"));

  const std::string upgraded = databaseDigest(db);
  const auto again = Migrations::migrate(connection);
  EXPECT_TRUE(again.applied.empty());
  EXPECT_TRUE(again.repaired.empty());
  EXPECT_EQ(databaseDigest(db), upgraded);
}

TEST(SaveMigrations, VersionNineSaveUpgradesWithDefaults)
{
  Logger::init();
  auto connection = std::make_shared<DatabaseConnection>(":memory:");
  Migrations::migrate(*connection);
  sqlite3* db = connection->getRaw();
  // A version 9 save: an academy with a U18 record and one result, a
  // managed match without detail, a board with a league objective only
  // and a player without a squad number.
  downgradeToVersionNine(db);
  for (const char* sql :
       {"INSERT INTO Leagues (id, name) VALUES (1, 'Old League');",
        "INSERT INTO Teams (id, league_id, name, balance) VALUES (4, 1, "
        "'Old Town', -250000);",
        "INSERT INTO Players (id, team_id, first_name, last_name, role, "
        "nationality, stats) VALUES (1, 4, 'Ada', 'Legacy', 'ST', 'EN', '{}');",
        "INSERT INTO YouthAcademies (team_id, recruitment, project, "
        "project_start_day, project_done_day, project_target, "
        "last_request_day, played, won, drawn, lost, goals_for, goals_against) "
        "VALUES (4, 60, 0, 0, 0, 0, 0, 3, 2, 1, 0, 7, 2);",
        "INSERT INTO YouthResults (seq, date, opponent_id, home, goals_for, "
        "goals_against) VALUES (1, 20250823, 5, 1, 3, 1);",
        "INSERT INTO BoardState (id, team_id, season_year, objective, "
        "expected_position, target_position, confidence) VALUES (1, 4, 2025, "
        "3, 12, 15, 58.0);"})
    execSql(db, sql);
  ManagedMatchSnapshot snapshot;
  snapshot.date = GameDateValue(2025, 8, 23);
  snapshot.home_id = 4;
  snapshot.away_id = 5;
  snapshot.corners = {6, 2};
  execSql(db, std::format("INSERT INTO ManagedMatchAnalytics (game_date, "
                          "home_id, away_id, data) VALUES (20250823, 4, 5, "
                          "'{}');",
                          snapshot.toJson()));

  const auto report = Migrations::migrate(*connection);
  EXPECT_EQ(report.from_version, 9);
  ASSERT_FALSE(report.applied.empty());
  EXPECT_EQ(report.applied.front(), 10);
  EXPECT_EQ(static_cast<int>(report.applied.size()),
            Migrations::currentSchemaVersion() - 9);
  for (const auto& [table, column] : COLUMNS_AFTER_VERSION_NINE)
    EXPECT_TRUE(Migrations::columnExists(db, table, column))
        << table << "." << column;
  // Old data survives; the new columns hold their defaults.
  EXPECT_EQ(queryInt(db, "SELECT played FROM YouthAcademies;"), 3);
  EXPECT_EQ(queryInt(db, "SELECT reserve_played FROM YouthAcademies;"), 0);
  EXPECT_EQ(queryInt(db, "SELECT reserves_ready FROM YouthAcademies;"), 0);
  EXPECT_EQ(queryInt(db, "SELECT squad FROM YouthResults;"), 0);
  EXPECT_EQ(queryInt(db, "SELECT detail IS NULL FROM ManagedMatchAnalytics;"),
            1);
  EXPECT_EQ(queryInt(db, "SELECT objective FROM BoardState;"), 3);
  EXPECT_EQ(queryInt(db, "SELECT finance_objective FROM BoardState;"), 0);
  EXPECT_EQ(queryInt(db, "SELECT start_balance FROM BoardState;"), 0);
  EXPECT_EQ(queryInt(db, "SELECT targets_set FROM BoardState;"), 0);
  EXPECT_EQ(queryInt(db, "SELECT squad_number FROM Players;"), 0);
  EXPECT_EQ(queryInt(db, "SELECT balance FROM Teams WHERE id = 4;"), -250000);

  const std::string upgraded = databaseDigest(db);
  const auto again = Migrations::migrate(*connection);
  EXPECT_TRUE(again.applied.empty());
  EXPECT_TRUE(again.repaired.empty());
  EXPECT_EQ(databaseDigest(db), upgraded);

  // The loaders read the upgraded rows.
  YouthAcademy academy(std::make_shared<GameData>());
  academy.load(connection);
  const AcademyClub* club = academy.club(4);
  ASSERT_NE(club, nullptr);
  EXPECT_EQ(club->table.played, 3);
  EXPECT_EQ(club->reserve_table.played, 0);
  EXPECT_FALSE(club->reserves_ready) << "set up on the next day";
  ASSERT_EQ(academy.results().size(), 1u) << "old results are U18 results";
  EXPECT_EQ(academy.results().front().goals_for, 3);
  EXPECT_TRUE(academy.reserveResults().empty());

  CareerGuidance guidance;
  guidance.load(connection);
  ASSERT_EQ(guidance.getSnapshots().size(), 1u);
  EXPECT_EQ(guidance.getSnapshots().front().corners[0], 6);
  EXPECT_TRUE(guidance.getSnapshots().front().detail.empty());

  BoardState board;
  ASSERT_TRUE(WorldStateRepository(connection).loadBoard(board));
  EXPECT_EQ(board.objective, BoardObjective::MidTable);
  EXPECT_FALSE(board.targets_set);
}

TEST(SaveMigrations, BoardTargetsSetKeepsTargetsTheBoardAnnounced)
{
  Logger::init();
  for (const bool announced : {true, false})
  {
    SCOPED_TRACE(announced ? "announced" : "upgraded by 0012");
    DatabaseConnection connection(":memory:");
    Migrations::migrate(connection);
    sqlite3* db = connection.getRaw();
    // A version 13 save whose board did or did not set its targets yet.
    for (const char* sql : {"ALTER TABLE BoardState DROP COLUMN targets_set;",
                            "DELETE FROM schema_migrations WHERE number >= 14;",
                            "UPDATE save_meta SET schema_version = 13;"})
      execSql(db, sql);
    execSql(db, std::format("INSERT INTO BoardState (id, team_id, "
                            "season_year, objective, expected_position, "
                            "target_position, confidence, cup_objective, "
                            "start_balance) VALUES (1, 4, 2025, 1, 3, 4, "
                            "60.0, {}, {});",
                            announced ? 2 : 0, announced ? 8'000'000 : 0));
    const auto report = Migrations::migrate(connection);
    EXPECT_EQ(report.from_version, 13);
    ASSERT_FALSE(report.applied.empty());
    EXPECT_EQ(report.applied.front(), 14);
    EXPECT_EQ(queryInt(db, "SELECT targets_set FROM BoardState;"),
              announced ? 1 : 0);
  }
}

TEST(SaveMigrations, FailingMigrationLeavesThePreviousVersion)
{
  Logger::init();
  DatabaseConnection connection(":memory:");
  Migrations::migrate(connection);
  const int version = Migrations::currentSchemaVersion();

  static constexpr std::array<Migrations::ColumnSpec, 1> NEW_COLUMN = {
      {{"Players", "doomed_column", "INTEGER NOT NULL DEFAULT 7"}}};
  std::vector<Migrations::Migration> migrations(Migrations::registry().begin(),
                                                Migrations::registry().end());
  migrations.emplace_back(version + 1, "9999_throws", NEW_COLUMN,
                          [](const Migrations::MigrationContext&)
                          { throw std::runtime_error("injected failure"); });

  EXPECT_THROW(Migrations::migrate(connection, migrations), DatabaseException);
  sqlite3* db = connection.getRaw();
  EXPECT_EQ(Migrations::readSchemaVersion(db), version);
  EXPECT_FALSE(Migrations::columnExists(db, "Players", "doomed_column"));
  EXPECT_EQ(queryInt(db, std::format("SELECT COUNT(*) FROM schema_migrations "
                                     "WHERE number = {};",
                                     version + 1)),
            0);
  // The database stays usable at its version.
  EXPECT_TRUE(Migrations::migrate(connection).applied.empty());
}

TEST(SaveMigrations, RecordedMigrationWithMissingColumnsIsRepaired)
{
  Logger::init();
  DatabaseConnection connection(":memory:");
  Migrations::migrate(connection);
  execSql(connection.getRaw(), "ALTER TABLE Players DROP COLUMN potential;");
  const auto report = Migrations::migrate(connection);
  EXPECT_TRUE(report.applied.empty());
  ASSERT_EQ(report.repaired.size(), 1u);
  EXPECT_TRUE(
      Migrations::columnExists(connection.getRaw(), "Players", "potential"));
}

TEST(SaveMigrations, FutureVersionIsRefusedBeforeAnyWrite)
{
  Logger::init();
  DatabaseConnection connection(":memory:");
  Migrations::migrate(connection);
  const int future = Migrations::currentSchemaVersion() + 7;
  execSql(connection.getRaw(),
          std::format("UPDATE save_meta SET schema_version = {};", future));
  const std::string before = databaseDigest(connection.getRaw());
  try
  {
    Migrations::migrate(connection);
    FAIL() << "future version accepted";
  }
  catch (const Migrations::FutureVersionError& error)
  {
    EXPECT_EQ(error.found, future);
    EXPECT_EQ(error.supported, Migrations::currentSchemaVersion());
  }
  EXPECT_EQ(databaseDigest(connection.getRaw()), before);
}

// ---- Careers: save, load, metadata
// --------------------------------------------

TEST(SaveSafety, SaveIsSelfContainedWithMetadata)
{
  const SlotCleanup slot{11};
  auto controller = makeCareer(slot.slot);
  advance(*controller, 3);
  ASSERT_TRUE(controller->saveGame());
  const fs::path path = RuntimePaths::savePath(slot.slot);
  EXPECT_FALSE(fs::exists(path.string() + "-wal"));
  EXPECT_FALSE(fs::exists(SaveManager::tempPath(path)));
  const std::string bytes = fileBytes(path);
  ASSERT_GT(bytes.size(), 100u);
  EXPECT_EQ(bytes[18], 1) << "snapshot must use a rollback journal";

  // Readable from a read-only file without loading the world.
  fs::permissions(path, fs::perms::owner_read, fs::perm_options::replace);
  const SaveInspection inspection = SaveManager::inspect(path);
  fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write,
                  fs::perm_options::replace);
  ASSERT_EQ(inspection.status, SaveStatus::Ok) << inspection.detail;
  EXPECT_TRUE(inspection.has_metadata);
  EXPECT_EQ(inspection.schema_version, Migrations::currentSchemaVersion());
  EXPECT_EQ(inspection.metadata.world_seed, WORLD_SEED);
  EXPECT_FALSE(inspection.metadata.seed_unknown);
  EXPECT_EQ(inspection.metadata.game_version, SaveFormat::GAME_VERSION);
  EXPECT_EQ(inspection.metadata.rules_edition, SaveFormat::RULES_EDITION);
  EXPECT_EQ(inspection.metadata.last_saved_game_date,
            controller->getCurrentDate().toString());
  EXPECT_FALSE(inspection.metadata.created_at_utc.empty());
  EXPECT_FALSE(inspection.metadata.updated_at_utc.empty());
  EXPECT_GE(inspection.metadata.playtime_seconds, 0);
  EXPECT_EQ(inspection.club_name,
            controller->getManagedTeam()->get().getName());
  EXPECT_EQ(inspection.game_date, controller->getCurrentDate().toString());
  EXPECT_EQ(inspection.foreign_key_issues, 0);

  const auto metadata = controller->getSaveSlotMetadata(slot.slot);
  EXPECT_TRUE(metadata.exists);
  EXPECT_EQ(metadata.status, SaveStatus::Ok);
  EXPECT_EQ(metadata.team_name, inspection.club_name);
  EXPECT_EQ(metadata.season, 1);
  EXPECT_EQ(metadata.schema_version, Migrations::currentSchemaVersion());
  EXPECT_EQ(metadata.last_saved_game_date,
            controller->getCurrentDate().toString());
  EXPECT_EQ(controller->getCurrentSlot(), slot.slot);
}

TEST(SaveSafety, UnsavedProgressNeverReachesTheSlot)
{
  const SlotCleanup slot{12};
  auto controller = makeCareer(slot.slot);
  ASSERT_TRUE(controller->saveGame());
  const fs::path path = RuntimePaths::savePath(slot.slot);
  const std::string saved = fileBytes(path);
  const std::string date = controller->getCurrentDate().toString();
  controller->setAutosavePolicy({AutosaveFrequency::Off, 3});
  advance(*controller, 10);
  EXPECT_EQ(fileBytes(path), saved);

  // Loading twice (without saving) leaves the file byte-identical and gives
  // the same world.
  std::string first_digest;
  for (int round = 0; round < 2; ++round)
  {
    GameController reloaded;
    ASSERT_TRUE(reloaded.loadGame(slot.slot));
    EXPECT_EQ(reloaded.getCurrentDate().toString(), date);
    std::string digest = databaseDigest(reloaded.getDbConn()->getRaw());
    if (round == 0)
      first_digest = std::move(digest);
    else
      EXPECT_EQ(digest, first_digest);
  }
  EXPECT_EQ(fileBytes(path), saved);
}

TEST(SaveSafety, LegacyCareerUpgradesAndKeepsAPreUpgradeCopy)
{
  const SlotCleanup slot{13};
  const fs::path path = RuntimePaths::savePath(slot.slot);
  std::string date;
  TeamID team_id = 0;
  {
    auto controller = makeCareer(slot.slot);
    advance(*controller, 5);
    ASSERT_TRUE(controller->saveGame());
    date = controller->getCurrentDate().toString();
    team_id = controller->getManagedTeam()->get().getId();
  }
  downgradeToVersionZero(path);
  EXPECT_EQ(SaveManager::inspect(path).schema_version, 0);
  const std::string legacy = fileBytes(path);

  for (int round = 0; round < 2; ++round)
  {
    GameController controller;
    ASSERT_TRUE(controller.loadGame(slot.slot))
        << controller.getLastLoadError().value_or(SaveError{}).detail;
    EXPECT_EQ(controller.getCurrentDate().toString(), date);
    EXPECT_EQ(controller.getManagedTeam()->get().getId(), team_id);
    EXPECT_GT(controller.getManagedTeam()->get().getReputation(), 0);
    // Loading upgrades the working copy only.
    EXPECT_EQ(fileBytes(path), legacy);
  }
  const fs::path copy = path.string() + ".pre-v0.bak";
  ASSERT_TRUE(fs::exists(copy));
  EXPECT_EQ(SaveManager::inspect(copy).schema_version, 0);
  const auto backups = SaveManager::listBackups(path);
  ASSERT_FALSE(backups.empty());
  EXPECT_EQ(backups.back().kind, SaveBackup::Kind::PreMigration);

  GameController controller;
  ASSERT_TRUE(controller.loadGame(slot.slot));
  advance(controller, 1);
  ASSERT_TRUE(controller.saveGame());
  const SaveInspection upgraded = SaveManager::inspect(path);
  ASSERT_EQ(upgraded.status, SaveStatus::Ok) << upgraded.detail;
  EXPECT_EQ(upgraded.schema_version, Migrations::currentSchemaVersion());
  EXPECT_TRUE(upgraded.metadata.seed_unknown) << "legacy seed must be flagged";
  EXPECT_EQ(upgraded.foreign_key_issues, 0);
  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  EXPECT_EQ(reloaded.getCurrentDate(), controller.getCurrentDate());
  EXPECT_EQ(reloaded.getWorldSeed(), controller.getWorldSeed());
}

TEST(SaveSafety, VersionNineCareerUpgradesAndRecordsTheCurrentVersions)
{
  const SlotCleanup slot{29};
  const fs::path path = RuntimePaths::savePath(slot.slot);
  std::string date;
  TeamID team_id = 0;
  {
    auto controller = makeCareer(slot.slot);
    advance(*controller, 5);
    ASSERT_TRUE(controller->saveGame());
    date = controller->getCurrentDate().toString();
    team_id = controller->getManagedTeam()->get().getId();
  }
  {
    // Written by an older build: older engine and random number streams.
    RawDb db(path);
    downgradeToVersionNine(db.get());
    execSql(db.get(),
            "UPDATE save_meta SET engine_version = '0.9.0', "
            "rng_version = 1, sim_version = 0;");
  }
  const SaveInspection legacy = SaveManager::inspect(path);
  ASSERT_EQ(legacy.status, SaveStatus::Ok) << legacy.detail;
  EXPECT_EQ(legacy.schema_version, 9);
  EXPECT_EQ(legacy.metadata.rng_version, 1);

  GameController controller;
  ASSERT_TRUE(controller.loadGame(slot.slot))
      << controller.getLastLoadError().value_or(SaveError{}).detail;
  EXPECT_EQ(controller.getCurrentDate().toString(), date);
  EXPECT_EQ(controller.getManagedTeam()->get().getId(), team_id);
  // The board of the old layout announced no cup, finance or youth target:
  // they are not judged before the next season start.
  EXPECT_FALSE(controller.getBoardState().targets_set);
  const auto targets = controller.getBoardTargets();
  ASSERT_TRUE(targets.has_value());
  EXPECT_EQ(targets->finance_grade, ObjectiveGrade::Met);
  // Players are numbered when the save is loaded.
  bool numbered = false;
  for (const auto& [id, player] : controller.getGameData()->getPlayers())
    numbered |= player.getTeamId() == team_id && player.getSquadNumber() != 0;
  EXPECT_TRUE(numbered);

  advance(controller, 1);
  ASSERT_TRUE(controller.saveGame());
  // The save now records the build that wrote it.
  const SaveInspection upgraded = SaveManager::inspect(path);
  ASSERT_EQ(upgraded.status, SaveStatus::Ok) << upgraded.detail;
  EXPECT_EQ(upgraded.schema_version, Migrations::currentSchemaVersion());
  EXPECT_EQ(upgraded.metadata.game_version, SaveFormat::GAME_VERSION);
  EXPECT_EQ(upgraded.metadata.engine_version, SaveFormat::ENGINE_VERSION);
  EXPECT_EQ(upgraded.metadata.rng_version, SaveFormat::RNG_VERSION);
  EXPECT_EQ(upgraded.metadata.sim_version, SaveFormat::SIM_VERSION);
  EXPECT_EQ(upgraded.foreign_key_issues, 0);

  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  EXPECT_EQ(reloaded.getCurrentDate(), controller.getCurrentDate());
  EXPECT_FALSE(reloaded.getBoardState().targets_set);
}

TEST(SaveSafety, FutureVersionSaveIsRefusedAndLeftUntouched)
{
  const SlotCleanup slot{14};
  const fs::path path = RuntimePaths::savePath(slot.slot);
  std::string club;
  {
    auto controller = makeCareer(slot.slot);
    ASSERT_TRUE(controller->saveGame());
    club = controller->getManagedTeam()->get().getName();
  }
  const int future = Migrations::currentSchemaVersion() + 3;
  {
    RawDb db(path);
    execSql(db.get(),
            std::format("UPDATE save_meta SET schema_version = {};", future));
  }
  const std::string before = fileBytes(path);

  GameController controller;
  EXPECT_FALSE(controller.loadGame(slot.slot));
  ASSERT_TRUE(controller.getLastLoadError().has_value());
  EXPECT_EQ(controller.getLastLoadError()->kind, SaveErrorKind::FutureVersion);
  EXPECT_EQ(controller.getLastLoadError()->found_version, future);
  EXPECT_STREQ(controller.getLastLoadError()->langKey(),
               "SAVE_ERROR_FUTURE_VERSION");
  EXPECT_FALSE(controller.isGameLoaded());
  EXPECT_EQ(fileBytes(path), before);

  const auto metadata = controller.getSaveSlotMetadata(slot.slot);
  EXPECT_EQ(metadata.status, SaveStatus::FutureVersion);
  EXPECT_EQ(metadata.team_name, club) << "read-only preview";
  EXPECT_EQ(fileBytes(path), before);

  // A working copy is refused by the runner as well.
  auto working = SaveManager::openWorkingCopy(path);
  EXPECT_THROW(Migrations::migrate(*working), Migrations::FutureVersionError);
  EXPECT_EQ(fileBytes(path), before);
}

TEST(SaveSafety, IncompleteSaveIsReported)
{
  const SlotCleanup slot{15};
  const fs::path path = RuntimePaths::savePath(slot.slot);
  {
    DatabaseConnection connection(path.string());
    Migrations::migrate(connection);
  }
  EXPECT_EQ(SaveManager::inspect(path).status, SaveStatus::Incomplete);
  GameController controller;
  EXPECT_FALSE(controller.loadGame(slot.slot));
  ASSERT_TRUE(controller.getLastLoadError().has_value());
  EXPECT_EQ(controller.getLastLoadError()->kind, SaveErrorKind::Incomplete);
}

// ---- Fault injection
// -----------------------------------------------------------

TEST(SaveSafety, InterruptedSaveKeepsThePreviousSave)
{
  const SlotCleanup slot{16};
  const fs::path path = RuntimePaths::savePath(slot.slot);
  auto controller = makeCareer(slot.slot);
  controller->setAutosavePolicy({AutosaveFrequency::Off, 3});
  ASSERT_TRUE(controller->saveGame());
  const std::string saved_date = controller->getCurrentDate().toString();

  for (const auto point :
       {SaveManager::FaultPoint::MidFlush, SaveManager::FaultPoint::TempOpened,
        SaveManager::FaultPoint::SnapshotWritten,
        SaveManager::FaultPoint::Rotated})
  {
    SCOPED_TRACE(static_cast<int>(point));
    advance(*controller, 2);
    const std::string saved = fileBytes(path);
    {
      const FaultHookGuard hook(
          [point](SaveManager::FaultPoint reached, sqlite3*)
          {
            if (reached == point) throw std::runtime_error("power cut");
          });
      EXPECT_FALSE(controller->saveGame());
    }
    const auto status = controller->getSaveStatus();
    EXPECT_FALSE(status.ok);
    EXPECT_EQ(status.error.kind, SaveErrorKind::Io);
    EXPECT_EQ(fileBytes(path), saved);
    EXPECT_FALSE(fs::exists(SaveManager::tempPath(path)));
    GameController reloaded;
    ASSERT_TRUE(reloaded.loadGame(slot.slot));
    EXPECT_EQ(reloaded.getCurrentDate().toString(), saved_date);
  }
  // The game in memory is intact and saves once the fault is gone.
  ASSERT_TRUE(controller->saveGame());
  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  EXPECT_EQ(reloaded.getCurrentDate(), controller->getCurrentDate());
}

TEST(SaveSafety, ProcessKilledMidSaveKeepsThePreviousSave)
{
  const SlotCleanup slot{17};
  const fs::path path = RuntimePaths::savePath(slot.slot);
  auto controller = makeCareer(slot.slot);
  controller->setAutosavePolicy({AutosaveFrequency::Off, 3});
  ASSERT_TRUE(controller->saveGame());
  const std::string saved_date = controller->getCurrentDate().toString();
  advance(*controller, 3);

  for (const auto point : {SaveManager::FaultPoint::SnapshotWritten,
                           SaveManager::FaultPoint::Rotated})
  {
    SCOPED_TRACE(static_cast<int>(point));
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0)
    {
      SaveManager::setFaultHook(
          [point](SaveManager::FaultPoint reached, sqlite3*)
          {
            if (reached == point) _exit(9);
          });
      controller->saveGame();
      _exit(0);
    }
    int status = 0;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (waitpid(child, &status, WNOHANG) == 0)
    {
      if (std::chrono::steady_clock::now() > deadline)
      {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
        FAIL() << "child did not finish";
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 9);
    EXPECT_EQ(SaveManager::inspect(path).status, SaveStatus::Ok);
    GameController reloaded;
    ASSERT_TRUE(reloaded.loadGame(slot.slot));
    EXPECT_EQ(reloaded.getCurrentDate().toString(), saved_date);
  }
  // The leftover temporary file does not disturb the next save.
  ASSERT_TRUE(controller->saveGame());
  EXPECT_FALSE(fs::exists(SaveManager::tempPath(path)));
  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  EXPECT_EQ(reloaded.getCurrentDate(), controller->getCurrentDate());
}

TEST(SaveSafety, DiskFullKeepsThePreviousSave)
{
  const SlotCleanup slot{18};
  const fs::path path = RuntimePaths::savePath(slot.slot);
  auto controller = makeCareer(slot.slot);
  ASSERT_TRUE(controller->saveGame());
  const std::string saved = fileBytes(path);
  advance(*controller, 2);
  {
    // The temporary file may not grow beyond a few pages: SQLITE_FULL.
    const FaultHookGuard hook(
        [](SaveManager::FaultPoint reached, sqlite3* temp)
        {
          if (reached == SaveManager::FaultPoint::TempOpened)
            sqlite3_exec(temp, "PRAGMA max_page_count = 4;", nullptr, nullptr,
                         nullptr);
        });
    EXPECT_FALSE(controller->saveGame());
  }
  const auto status = controller->getSaveStatus();
  EXPECT_EQ(status.error.kind, SaveErrorKind::DiskFull);
  EXPECT_STREQ(status.error.langKey(), "SAVE_ERROR_DISK_FULL");
  EXPECT_EQ(fileBytes(path), saved);
  EXPECT_FALSE(fs::exists(SaveManager::tempPath(path)));
}

TEST(SaveSafety, ReadOnlyFolderKeepsThePreviousSave)
{
  if (geteuid() == 0) GTEST_SKIP() << "root ignores file permissions";
  const SlotCleanup slot{19};
  const fs::path path = RuntimePaths::savePath(slot.slot);
  auto controller = makeCareer(slot.slot);
  ASSERT_TRUE(controller->saveGame());
  const std::string saved = fileBytes(path);
  advance(*controller, 2);
  const fs::path folder = path.parent_path();
  const auto permissions = fs::status(folder).permissions();
  fs::permissions(folder, fs::perms::owner_read | fs::perms::owner_exec,
                  fs::perm_options::replace);
  const bool ok = controller->saveGame();
  fs::permissions(folder, permissions, fs::perm_options::replace);
  EXPECT_FALSE(ok);
  EXPECT_EQ(controller->getSaveStatus().error.kind,
            SaveErrorKind::PermissionDenied);
  EXPECT_EQ(fileBytes(path), saved);
  ASSERT_TRUE(controller->saveGame()) << "saves again once writable";
}

TEST(SaveSafety, CorruptSaveIsDetectedAndABackupRestores)
{
  const SlotCleanup slot{20};
  const fs::path path = RuntimePaths::savePath(slot.slot);
  auto controller = makeCareer(slot.slot);
  controller->setAutosavePolicy({AutosaveFrequency::Off, 3});
  ASSERT_TRUE(controller->saveGame());
  const std::string backup_date = controller->getCurrentDate().toString();
  advance(*controller, 2);
  ASSERT_TRUE(controller->saveGame());
  controller.reset();

  // Damage the middle of the file (table pages) and, separately, the header.
  for (const bool header : {false, true})
  {
    SCOPED_TRACE(header ? "header" : "pages");
    std::string bytes = fileBytes(path);
    const std::size_t start = header ? 0 : (bytes.size() / 2) & ~4095ULL;
    for (std::size_t i = start; i < std::min(bytes.size(), start + 16384); ++i)
      bytes[i] = static_cast<char>(0xA5);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << bytes;

    GameController loader;
    EXPECT_FALSE(loader.loadGame(slot.slot));
    ASSERT_TRUE(loader.getLastLoadError().has_value());
    EXPECT_EQ(loader.getLastLoadError()->kind, SaveErrorKind::Corrupt);
    EXPECT_EQ(loader.getSaveSlotMetadata(slot.slot).status,
              SaveStatus::Corrupt);

    const auto backups = loader.getSaveBackups(slot.slot);
    ASSERT_FALSE(backups.empty());
    EXPECT_EQ(backups.front().index, 1);
    ASSERT_EQ(backups.front().inspection.status, SaveStatus::Ok);
    EXPECT_EQ(backups.front().inspection.game_date, backup_date);
    ASSERT_TRUE(loader.restoreBackup(slot.slot, backups.front().path));
    EXPECT_EQ(loader.getCurrentDate().toString(), backup_date);
    ASSERT_TRUE(loader.saveGame());  // Rotates: the slot has a backup again.
    advance(loader, 2);
    ASSERT_TRUE(loader.saveGame());
  }
  int kept_aside = 0;
  for (const auto& entry : fs::directory_iterator(path.parent_path()))
    if (entry.path().filename().string().starts_with(path.filename().string() +
                                                     ".corrupt-"))
      ++kept_aside;
  EXPECT_GE(kept_aside, 1) << "a damaged save is never deleted";
}

// ---- Backups and autosave
// -------------------------------------------------------

TEST(SaveSafety, BackupsRotateAndKeepN)
{
  const SlotCleanup slot{21};
  const fs::path path = RuntimePaths::savePath(slot.slot);
  auto controller = makeCareer(slot.slot);
  controller->setAutosavePolicy({AutosaveFrequency::Off, 2});
  std::vector<std::string> dates;
  for (int save = 0; save < 4; ++save)
  {
    advance(*controller, 1);
    ASSERT_TRUE(controller->saveGame());
    dates.push_back(controller->getCurrentDate().toString());
  }
  EXPECT_EQ(SaveManager::countBackups(path), 2);
  EXPECT_EQ(SaveManager::inspect(SaveManager::backupPath(path, 1)).game_date,
            dates[2]);
  EXPECT_EQ(SaveManager::inspect(SaveManager::backupPath(path, 2)).game_date,
            dates[1]);
  EXPECT_EQ(controller->getSaveSlotMetadata(slot.slot).backups, 2);

  controller->setAutosavePolicy({AutosaveFrequency::Off, 0});
  ASSERT_TRUE(controller->saveGame());
  EXPECT_EQ(SaveManager::countBackups(path), 0);
}

TEST(SaveSafety, RecoveryOffersTheNewestBackupThatLoads)
{
  const SlotCleanup slot{28};
  const fs::path path = RuntimePaths::savePath(slot.slot);
  auto controller = makeCareer(slot.slot);
  controller->setAutosavePolicy({AutosaveFrequency::Off, 3});
  std::vector<std::string> dates;
  for (int save = 0; save < 5; ++save)
  {
    advance(*controller, 1);
    ASSERT_TRUE(controller->saveGame());
    dates.push_back(controller->getCurrentDate().toString());
  }
  controller.reset();
  // Rotation: the slot holds save 5, .bak.1..3 saves 4, 3 and 2.
  ASSERT_EQ(SaveManager::countBackups(path), 3);
  EXPECT_EQ(SaveManager::inspect(SaveManager::backupPath(path, 3)).game_date,
            dates[1]);

  // The slot is cut short (half the file) and the newest backup has a
  // damaged header.
  fs::resize_file(path, fs::file_size(path) / 2);
  {
    const fs::path newest = SaveManager::backupPath(path, 1);
    std::string bytes = fileBytes(newest);
    std::fill_n(bytes.begin(), 100, 'Z');
    std::ofstream(newest, std::ios::binary | std::ios::trunc) << bytes;
  }

  GameController loader;
  EXPECT_EQ(loader.getSaveSlotMetadata(slot.slot).status, SaveStatus::Corrupt);
  EXPECT_FALSE(loader.loadGame(slot.slot));
  ASSERT_TRUE(loader.getLastLoadError().has_value());
  EXPECT_EQ(loader.getLastLoadError()->kind, SaveErrorKind::Corrupt);

  std::vector<SaveBackup> backups = loader.getSaveBackups(slot.slot);
  const SaveBackup* offered = SaveManager::newestUsable(backups);
  ASSERT_NE(offered, nullptr);
  EXPECT_EQ(offered->index, 2) << "the damaged newest backup is skipped";
  EXPECT_EQ(offered->inspection.game_date, dates[2]);
  ASSERT_TRUE(loader.restoreBackup(slot.slot, offered->path));
  EXPECT_EQ(loader.getCurrentDate().toString(), dates[2]);
  EXPECT_EQ(SaveManager::inspect(path).status, SaveStatus::Ok);

  for (SaveBackup& backup : backups)
    backup.inspection.status = SaveStatus::Corrupt;
  EXPECT_EQ(SaveManager::newestUsable(backups), nullptr) << "nothing to offer";
}

TEST(SaveSafety, FailedNewGameKeepsTheOldCareer)
{
  const SlotCleanup slot{27};
  const fs::path path = RuntimePaths::savePath(slot.slot);
  const AutosavePolicy policy{AutosaveFrequency::Off, 2};
  auto controller = makeCareer(slot.slot);
  controller->setAutosavePolicy(policy);
  for (int save = 0; save < 3; ++save)
  {
    advance(*controller, 1);
    ASSERT_TRUE(controller->saveGame());
  }
  SaveManager::preserveBeforeMigration(path, 1);
  const fs::path upgrade_copy(path.string() + ".pre-v1.bak");
  ASSERT_TRUE(fs::exists(upgrade_copy));
  const SaveInspection before = SaveManager::inspect(path);
  ASSERT_EQ(before.status, SaveStatus::Ok);
  ASSERT_GT(before.managed_team_id, 0);
  const std::string backup_date =
      SaveManager::inspect(SaveManager::backupPath(path, 1)).game_date;
  ASSERT_EQ(SaveManager::countBackups(path), 2);
  controller.reset();

  const auto oldCareerIntact = [&]
  {
    const SaveInspection after = SaveManager::inspect(path);
    EXPECT_EQ(after.status, SaveStatus::Ok);
    EXPECT_EQ(after.game_date, before.game_date);
    EXPECT_EQ(after.managed_team_id, before.managed_team_id);
    EXPECT_EQ(SaveManager::countBackups(path), 2);
    EXPECT_EQ(SaveManager::inspect(SaveManager::backupPath(path, 1)).game_date,
              backup_date);
    EXPECT_TRUE(fs::exists(upgrade_copy));
    GameController reloaded;
    ASSERT_TRUE(reloaded.loadGame(slot.slot));
    EXPECT_EQ(reloaded.getCurrentDate().toString(), before.game_date);
  };

  // A new career in the same slot fails while its world is being created
  // (the first flush of the new world) and then, on a second attempt, at
  // its first save: the old career, its backups and its upgrade copy are
  // still there both times.
  int flushes_to_survive = 0;
  const FaultHookGuard guard(
      [&flushes_to_survive](SaveManager::FaultPoint point, sqlite3*)
      {
        if (point == SaveManager::FaultPoint::MidFlush)
        {
          if (flushes_to_survive-- == 0)
            throw std::runtime_error("disk unplugged");
        }
      });
  {
    GameController fresh;
    fresh.setAutosavePolicy(policy);
    EXPECT_THROW(fresh.newGame(slot.slot, WORLD_SEED + 1), std::runtime_error);
    EXPECT_EQ(fresh.getGame(), nullptr) << "no half-built career is kept";
  }
  oldCareerIntact();
  flushes_to_survive = 1;
  {
    GameController fresh;
    fresh.setAutosavePolicy(policy);
    fresh.newGame(slot.slot, WORLD_SEED + 1);
    EXPECT_FALSE(fresh.getSaveStatus().ok);
  }
  oldCareerIntact();
  SaveManager::setFaultHook(nullptr);

  // Once the new world is saved it replaces the slot, and the old career is
  // the newest rotation backup.
  GameController fresh;
  fresh.setAutosavePolicy(policy);
  fresh.newGame(slot.slot, WORLD_SEED + 1);
  ASSERT_TRUE(fresh.getSaveStatus().ok);
  const SaveInspection replaced = SaveManager::inspect(path);
  EXPECT_EQ(replaced.status, SaveStatus::Ok);
  EXPECT_EQ(replaced.game_date, fresh.getCurrentDate().toString());
  EXPECT_NE(replaced.game_date, before.game_date);
  EXPECT_EQ(SaveManager::countBackups(path), 2);
  EXPECT_EQ(SaveManager::inspect(SaveManager::backupPath(path, 1)).game_date,
            before.game_date);
}

TEST(SaveSafety, AutosaveFrequencies)
{
  const GameDateValue monday(2025, 8, 4);
  const auto due = [&](AutosaveFrequency frequency, GameDateValue today,
                       int season = 1, bool matchday = false)
  {
    return SaveManager::isAutosaveDue(frequency, monday, 1, today, season,
                                      matchday);
  };
  EXPECT_FALSE(due(AutosaveFrequency::Off, GameDateValue(2026, 1, 1), 2, true));
  EXPECT_FALSE(due(AutosaveFrequency::Daily, monday));
  EXPECT_TRUE(due(AutosaveFrequency::Daily, GameDateValue(2025, 8, 5)));
  EXPECT_FALSE(due(AutosaveFrequency::Weekly, GameDateValue(2025, 8, 10)));
  EXPECT_TRUE(due(AutosaveFrequency::Weekly, GameDateValue(2025, 8, 11)));
  EXPECT_FALSE(due(AutosaveFrequency::Monthly, GameDateValue(2025, 8, 31)));
  EXPECT_TRUE(due(AutosaveFrequency::Monthly, GameDateValue(2025, 9, 1)));
  EXPECT_FALSE(due(AutosaveFrequency::Matchday, GameDateValue(2025, 8, 5)));
  EXPECT_TRUE(
      due(AutosaveFrequency::Matchday, GameDateValue(2025, 8, 5), 1, true));
  EXPECT_FALSE(due(AutosaveFrequency::SeasonEnd, GameDateValue(2026, 6, 30)));
  EXPECT_TRUE(due(AutosaveFrequency::SeasonEnd, GameDateValue(2026, 7, 1), 2));

  const SlotCleanup slot{22};
  const fs::path path = RuntimePaths::savePath(slot.slot);
  auto controller = makeCareer(slot.slot);
  controller->setAutosavePolicy({AutosaveFrequency::Weekly, 3});
  const int baseline = controller->getSaveStatus().successful_saves;
  advance(*controller, 6);
  EXPECT_EQ(controller->getSaveStatus().successful_saves, baseline);
  advance(*controller, 1);
  const auto status = controller->getSaveStatus();
  EXPECT_TRUE(status.ok);
  EXPECT_TRUE(status.autosave);
  EXPECT_EQ(status.successful_saves, baseline + 1);
  EXPECT_EQ(SaveManager::inspect(path).game_date,
            controller->getCurrentDate().toString());
  std::cout << std::format(
      "[ SAVE     ] autosave {:.1f} ms (flush {:.1f}, "
      "snapshot {:.1f}, verify {:.1f}, sync {:.1f})\n",
      status.timings.total_ms, status.timings.flush_ms,
      status.timings.snapshot_ms, status.timings.verify_ms,
      status.timings.sync_ms);
}

TEST(SaveSafety, SaveAndLoadTimesAtStandardWorldSize)
{
  const SlotCleanup slot{23};
  auto controller = makeCareer(slot.slot);
  controller->setAutosavePolicy({AutosaveFrequency::Off, 3});
  advance(*controller, 14);
  double best_save = 1e9;
  for (int round = 0; round < 3; ++round)
  {
    ASSERT_TRUE(controller->saveGame());
    best_save =
        std::min(best_save, controller->getSaveStatus().timings.total_ms);
  }
  const auto timings = controller->getSaveStatus().timings;
  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  const auto bytes = fs::file_size(RuntimePaths::savePath(slot.slot));
  std::cout << std::format(
      "[ SAVE     ] {} players, {:.1f} MB: save best {:.1f} ms (last: flush "
      "{:.1f}, snapshot {:.1f}, verify {:.1f}, sync {:.1f}); load {:.1f} ms\n",
      controller->getGameData()->getPlayers().size(),
      static_cast<double>(bytes) / 1e6, best_save, timings.flush_ms,
      timings.snapshot_ms, timings.verify_ms, timings.sync_ms,
      reloaded.getLastInitializationMilliseconds());
  RecordProperty("save_ms", std::to_string(best_save));
  RecordProperty("load_ms",
                 std::to_string(reloaded.getLastInitializationMilliseconds()));
  // Generous bound: catches pathological regressions, not machine noise.
  EXPECT_LT(best_save, 2000.0);
}

// ---- Incremental writes
// ---------------------------------------------------------

TEST(SaveWrites, PlayerRowsRoundTripExactly)
{
  Logger::init();
  auto connection = std::make_shared<DatabaseConnection>(":memory:");
  Migrations::migrate(*connection);
  Player player(7, 3, "Ada", "Quote\"Back\\slash", PlayerRole::ST, Language::IT,
                1500, 0, 24, 3, 181, Foot::Left,
                {{"Pace", 78.00740814208984f},
                 {"Shooting", 1e-7f},
                 {"Vision", 99.99999f}});
  player.setPotential(83.25f);
  player.setSquadNumber(9);
  PlayerDynamics& dynamics = player.mutableDynamics();
  dynamics.condition = 71.234f;
  dynamics.injury_days = 0;
  dynamics.last_match_day = 20281;
  dynamics.rating_count = 2;
  dynamics.recent_ratings = {6.7f, 7.26f, 0.0f, 0.0f, 0.0f};
  PlayerRepository repository(connection);
  repository.updatePlayers({std::cref(player)});  // Inserted.
  player.mutableDynamics().morale = 12.5f;
  repository.updatePlayers({std::cref(player)});  // Updated.

  const auto loaded = repository.loadAllPlayers();
  ASSERT_EQ(loaded.size(), 1u);
  const Player& copy = loaded.front();
  EXPECT_EQ(copy.getStats(), player.getStats()) << "stats must be exact";
  EXPECT_EQ(copy.getLastName(), player.getLastName());
  EXPECT_EQ(copy.getNationality(), Language::IT);
  EXPECT_EQ(copy.getFoot(), Foot::Left);
  EXPECT_FLOAT_EQ(copy.getPotential(), 83.25f);
  EXPECT_EQ(copy.getSquadNumber(), 9);
  EXPECT_NEAR(copy.getDynamics().condition, 71.23f, 1e-4f);
  EXPECT_FLOAT_EQ(copy.getDynamics().morale, 12.5f);
  EXPECT_EQ(copy.getDynamics().last_match_day, 20281);
  EXPECT_FLOAT_EQ(copy.getDynamics().recent_ratings[1], 7.3f);
}

TEST(SaveWrites, CalendarWritesOnlyChangedFixtures)
{
  Logger::init();
  auto connection = std::make_shared<DatabaseConnection>(":memory:");
  Migrations::migrate(*connection);
  FixtureRepository repository(connection);
  const GameDateValue day(2025, 9, 6);
  Calendar calendar;
  calendar.addMatch(Match(1, 2, day, MatchType::LEAGUE, 1, 1));
  calendar.addMatch(Match(3, 4, day, MatchType::LEAGUE, 1, 1));
  calendar.addMatch(
      Match(5, 6, GameDateValue(2025, 9, 13), MatchType::CUP, 9, 1));
  repository.saveCalendar(calendar);
  sqlite3* db = connection->getRaw();
  const int first_id =
      queryInt(db, "SELECT id FROM Fixtures WHERE home_team_id = 3;");

  // A result, a shoot-out and a new fixture; nothing else is rewritten.
  calendar.findMatch(day, 1, 2)->setPlayedResult(2, 1);
  calendar.findMatch(GameDateValue(2025, 9, 13), 5, 6)
      ->setKnockoutResult(1, 1, true, std::make_pair(uint8_t{4}, uint8_t{3}));
  calendar.addMatch(
      Match(7, 8, GameDateValue(2025, 9, 20), MatchType::LEAGUE, 1, 2));
  sqlite3_exec(db,
               "CREATE TEMP TABLE writes (n INTEGER);"
               "CREATE TEMP TRIGGER count_updates AFTER UPDATE ON Fixtures "
               "BEGIN INSERT INTO writes VALUES (1); END;",
               nullptr, nullptr, nullptr);
  repository.saveCalendar(calendar);
  EXPECT_EQ(queryInt(db, "SELECT COUNT(*) FROM writes;"), 2);
  EXPECT_EQ(queryInt(db, "SELECT id FROM Fixtures WHERE home_team_id = 3;"),
            first_id);
  EXPECT_EQ(queryInt(db, "SELECT COUNT(*) FROM Fixtures;"), 4);

  // Fixtures gone from the calendar (a new season) are removed.
  Calendar next;
  next.addMatch(Match(1, 2, day, MatchType::LEAGUE, 1, 1));
  next.findMatch(day, 1, 2)->setPlayedResult(2, 1);
  repository.saveCalendar(next);
  EXPECT_EQ(queryInt(db, "SELECT COUNT(*) FROM Fixtures;"), 1);

  Calendar loaded;
  repository.loadCalendar(loaded);
  const Match* stored = loaded.findMatch(day, 1, 2);
  ASSERT_NE(stored, nullptr);
  EXPECT_TRUE(stored->isPlayed());
  EXPECT_EQ(stored->getHomeScore(), 2);
}

TEST(SaveWrites, CareerStateRoundTripsExactly)
{
  const SlotCleanup slot{26};
  auto controller = makeCareer(slot.slot);
  controller->setAutosavePolicy({AutosaveFrequency::Off, 3});
  advance(*controller, 9);
  const TeamID managed = controller->getManagedTeam()->get().getId();
  ASSERT_TRUE(
      controller->setDutyOwner(Duty::TrainingSchedule, DutyOwner::Manager));
  ASSERT_TRUE(controller->setTrainingIntensity(TrainingIntensity::High));
  ASSERT_TRUE(controller->saveGame());
  // A second save with (almost) nothing changed writes only differences.
  advance(*controller, 1);
  ASSERT_TRUE(controller->saveGame());

  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  const auto gd = controller->getGameData();
  const auto copy = reloaded.getGameData();
  ASSERT_EQ(copy->getPlayers().size(), gd->getPlayers().size());
  for (const auto& [id, player] : gd->getPlayers())
  {
    const auto other = copy->getPlayer(id);
    ASSERT_TRUE(other.has_value()) << id;
    EXPECT_EQ(other->get().getStats(), player.getStats()) << id;
    EXPECT_EQ(other->get().getTeamId(), player.getTeamId()) << id;
  }
  for (const auto& [id, team] : gd->getTeams())
  {
    if (id == FREE_AGENTS_TEAM_ID) continue;  // Its lineup is never saved.
    const Team& other = copy->getTeam(id)->get();
    const Lineup& a = team.getLineup();
    const Lineup& b = other.getLineup();
    ASSERT_EQ(a.getOutfieldPlayers().size(), b.getOutfieldPlayers().size())
        << id;
    for (std::size_t i = 0; i < a.getOutfieldPlayers().size(); ++i)
    {
      EXPECT_EQ(a.getOutfieldPlayers()[i].player->getId(),
                b.getOutfieldPlayers()[i].player->getId());
      EXPECT_EQ(a.getOutfieldPlayers()[i].position.x,
                b.getOutfieldPlayers()[i].position.x);
      EXPECT_EQ(a.getOutfieldPlayers()[i].position.y,
                b.getOutfieldPlayers()[i].position.y);
    }
    EXPECT_EQ(a.getDesignations(), b.getDesignations()) << id;
    const StrategySliders sa = team.getStrategy().getSliders();
    const StrategySliders sb = other.getStrategy().getSliders();
    EXPECT_EQ(sa.pressing, sb.pressing);
    EXPECT_EQ(sa.compactness, sb.compactness);
    EXPECT_EQ(sa.widthUsage, sb.widthUsage);
  }
  EXPECT_TRUE(reloaded.getBoardState().targets_set);
  EXPECT_EQ(reloaded.getBoardState().start_balance,
            controller->getBoardState().start_balance);
  EXPECT_EQ(copy->getStaff().all().size(), gd->getStaff().all().size());
  for (const auto& [id, member] : gd->getStaff().all())
  {
    const auto found = copy->getStaff().all().find(id);
    ASSERT_NE(found, copy->getStaff().all().end());
    EXPECT_EQ(found->second.team_id, member.team_id);
    EXPECT_EQ(found->second.attributes, member.attributes);
    EXPECT_EQ(found->second.wage, member.wage);
  }
  const TeamTrainingPlan* plan = copy->getTraining().findPlan(managed);
  ASSERT_NE(plan, nullptr);
  EXPECT_EQ(plan->intensity, TrainingIntensity::High);
  EXPECT_EQ(plan->familiarity,
            gd->getTraining().findPlan(managed)->familiarity);
  for (const auto& [id, state] : gd->getTraining().players())
  {
    const auto* other = copy->getTraining().findPlayer(id);
    ASSERT_NE(other, nullptr) << id;
    EXPECT_EQ(other->acute, state.acute);
    EXPECT_EQ(other->chronic, state.chronic);
  }
}
