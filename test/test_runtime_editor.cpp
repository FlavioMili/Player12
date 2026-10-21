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
#include <nlohmann/json.hpp>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "database/runtime_editor.h"
#include "global/logger.h"

namespace
{
using namespace RuntimeEditor;
using Handle = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
Handle database(const char* sql)
{
  sqlite3* raw = nullptr;
  EXPECT_EQ(sqlite3_open(":memory:", &raw), SQLITE_OK);
  Handle db(raw, &sqlite3_close);
  EXPECT_EQ(sqlite3_exec(raw, sql, nullptr, nullptr, nullptr), SQLITE_OK);
  return db;
}
Change update(const Page& page, std::size_t row,
              std::map<std::string, Value, std::less<>> fields)
{
  return {page.table.name, page.schema, Operation::Update, page.rows.at(row),
          std::move(fields)};
}
}  // namespace

TEST(RuntimeEditor, DiscoversUnrelatedSchemasAndEvolvesWithoutAnAllowlist)
{
  auto db = database(
      "CREATE TABLE gadgets (key TEXT PRIMARY KEY, score REAL CHECK(score>=0), "
      "bytes BLOB, note TEXT DEFAULT 'untitled', doubled REAL GENERATED ALWAYS "
      "AS (score*2) STORED) STRICT; INSERT INTO gadgets(key,score,bytes) "
      "VALUES('a',2,x'00ff'); CREATE VIEW summaries AS SELECT key,score FROM "
      "gadgets;");
  auto page = read(db.get(), "gadgets");
  ASSERT_EQ(page.table.columns.size(), 5U);
  EXPECT_NE(page.table.columns.back().hidden, 0);
  EXPECT_EQ(std::get<Blob>(page.rows[0].values[2]), (Blob{0, 255}));
  EXPECT_THROW(apply(db.get(), {update(page, 0, {{"doubled", 10.0}})}),
               std::runtime_error);
  apply(db.get(),
        {update(page, 0, {{"score", 3.0}, {"note", std::string("custom")}})});
  EXPECT_EQ(std::get<double>(read(db.get(), "gadgets").rows[0].values[4]), 6);
  EXPECT_FALSE(read(db.get(), "summaries").table.writable);
  EXPECT_EQ(
      sqlite3_exec(
          db.get(),
          "ALTER TABLE gadgets ADD COLUMN new_stat INTEGER DEFAULT 7; CREATE "
          "TABLE future_feature(id INTEGER PRIMARY KEY, extra TEXT);",
          nullptr, nullptr, nullptr),
      SQLITE_OK);
  EXPECT_THROW(apply(db.get(), {update(page, 0, {{"score", 4.0}})}),
               std::runtime_error);
  auto evolved = read(db.get(), "gadgets");
  EXPECT_EQ(evolved.table.columns.back().name, "new_stat");
  apply(db.get(), {update(evolved, 0, {{"new_stat", std::int64_t{99}}})});
  EXPECT_EQ(
      std::get<std::int64_t>(read(db.get(), "gadgets").rows[0].values.back()),
      99);
  Change insert{"future_feature",
                schema(db.get()),
                Operation::Insert,
                {},
                {{"extra", std::string("works")}}};
  apply(db.get(), {insert});
  EXPECT_EQ(read(db.get(), "future_feature").rows.size(), 1U);
}

TEST(RuntimeEditor, CompositeKeysNullsBlobsAndQuotedIdentifiersRoundtrip)
{
  auto db = database(
      R"(CREATE TABLE "odd""table"(a TEXT,b INTEGER,payload BLOB,note TEXT,PRIMARY KEY(a,b)) WITHOUT ROWID; INSERT INTO "odd""table" VALUES('x',2,x'',NULL),('y',3,x'ff','other');)");
  auto page = read(db.get(), "odd\"table");
  ASSERT_EQ(page.table.identity.size(), 2U);
  EXPECT_TRUE(std::holds_alternative<std::monostate>(page.rows[0].values[3]));
  EXPECT_TRUE(std::holds_alternative<Blob>(page.rows[0].values[2]));
  apply(db.get(), {update(page, 0,
                          {{"a", std::string("'; DROP TABLE anything;--")},
                           {"payload", Blob{0, 1, 255}},
                           {"note", std::string("hello")}})});
  EXPECT_EQ(read(db.get(), "odd\"table", 0, "DROP TABLE").rows.size(), 1U);
  auto after = read(db.get(), "odd\"table");
  auto deletion = update(after, 0, {});
  deletion.operation = Operation::Delete;
  apply(db.get(), {deletion});
  EXPECT_EQ(read(db.get(), "odd\"table").rows.size(), 1U);
  EXPECT_EQ(parse(SQLITE_INTEGER, "9223372036854775807"),
            Value{std::numeric_limits<std::int64_t>::max()});
  EXPECT_THROW(parse(SQLITE_INTEGER, "9223372036854775808"),
               std::runtime_error);
  EXPECT_THROW(parse(SQLITE_FLOAT, "nan"), std::runtime_error);
  EXPECT_THROW(parse(SQLITE_FLOAT, "1.0oops"), std::runtime_error);
  EXPECT_THROW(parse(SQLITE_BLOB, "0z"), std::runtime_error);
  EXPECT_EQ(parse(SQLITE_BLOB, "00ff"), Value(Blob{0, 255}));
}

TEST(RuntimeEditor, ConstraintsStaleDraftsValidatorsAndTriggersAreAtomic)
{
  auto db = database(
      "CREATE TABLE parents(id INTEGER PRIMARY KEY); CREATE TABLE children(id "
      "INTEGER PRIMARY KEY, parent INTEGER REFERENCES parents(id), value "
      "INTEGER CHECK(value>=0)); INSERT INTO parents VALUES(1); INSERT INTO "
      "children VALUES(1,1,10),(2,1,20); CREATE TABLE audit(value INTEGER); "
      "CREATE TRIGGER changed AFTER UPDATE ON children BEGIN INSERT INTO audit "
      "VALUES(new.value); END;");
  auto page = read(db.get(), "children");
  EXPECT_THROW(
      apply(db.get(), {update(page, 0, {{"value", std::int64_t{11}}}),
                       update(page, 1, {{"value", std::int64_t{-1}}})}),
      std::runtime_error);
  EXPECT_EQ(read(db.get(), "audit").rows.size(), 0U);
  EXPECT_EQ(
      std::get<std::int64_t>(read(db.get(), "children").rows[0].values[2]), 10);
  EXPECT_THROW(
      apply(db.get(), {update(page, 0, {{"parent", std::int64_t{999}}})}),
      std::runtime_error);
  EXPECT_THROW(apply(db.get(), {update(page, 0, {{"value", std::int64_t{11}}})},
                     {[](sqlite3*, const std::vector<Change>&)
                      { throw std::runtime_error("domain failure"); }}),
               std::runtime_error);
  apply(db.get(), {update(page, 0, {{"value", std::int64_t{12}}})});
  EXPECT_THROW(
      apply(db.get(), {update(page, 0, {{"value", std::int64_t{13}}})}),
      std::runtime_error);
  EXPECT_EQ(read(db.get(), "audit").rows.size(), 1U);
}

TEST(RuntimeEditor, AdditionalWithoutRowidForeignKeyViolationsAreRejected)
{
  auto db = database(
      "CREATE TABLE parents(id INTEGER PRIMARY KEY); "
      "CREATE TABLE children(id TEXT PRIMARY KEY, parent INTEGER "
      "REFERENCES parents(id)) WITHOUT ROWID; "
      "INSERT INTO children VALUES('legacy',999);");
  const Change insert{
      "children",
      schema(db.get()),
      Operation::Insert,
      {},
      {{"id", std::string("new")}, {"parent", std::int64_t{999}}}};
  EXPECT_THROW(apply(db.get(), {insert}), Error);
  const auto page = read(db.get(), "children");
  ASSERT_EQ(page.rows.size(), 1U);
  EXPECT_EQ(std::get<std::string>(page.rows.front().values.front()), "legacy");

  // Keeping an existing violation is permitted; resolving one is permitted too.
  apply(db.get(), {update(page, 0, {{"id", std::string("renamed")}})});
  ASSERT_EQ(sqlite3_exec(db.get(), "INSERT INTO parents VALUES(999)", nullptr,
                         nullptr, nullptr),
            SQLITE_OK);
  EXPECT_NO_THROW(apply(db.get(), {}));
}

TEST(RuntimeEditor, PaginationAndSearchAreBoundAndLiteral)
{
  auto db = database(
      "CREATE TABLE items(text TEXT); INSERT INTO items "
      "VALUES('a%'),('b_'),('c'),('d');");
  auto page = read(db.get(), "items", 0, "", 2);
  EXPECT_EQ(page.rows.size(), 2U);
  EXPECT_TRUE(page.more);
  EXPECT_FALSE(read(db.get(), "items", 2, "", 2).more);
  EXPECT_EQ(read(db.get(), "items", 0, "%").rows.size(), 1U);
  EXPECT_EQ(read(db.get(), "items", 0, "' OR 1=1 --").rows.size(), 0U);
  EXPECT_THROW(read(db.get(), "items", -1), std::runtime_error);
}

TEST(RuntimeEditor, ExtraColumnsSurviveReplacementAndDoNotResurrectDeletedRows)
{
  auto canonical = database(
      "CREATE TABLE events(id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT "
      "UNIQUE NOT NULL,value INTEGER);");
  auto active = database(
      "CREATE TABLE events(id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT "
      "UNIQUE NOT NULL,value INTEGER,custom TEXT DEFAULT 'default'); INSERT "
      "INTO events(name,value,custom) "
      "VALUES('keep',1,'extension'),('remove',2,'gone'); CREATE TABLE "
      "unrelated(custom TEXT); INSERT INTO unrelated VALUES('preserved');");
  Extensions extras(active.get(), canonical.get());
  ASSERT_FALSE(extras.empty());
  extras.capture(active.get());
  EXPECT_EQ(sqlite3_exec(active.get(),
                         "DELETE FROM events; INSERT INTO events(name,value) "
                         "VALUES('keep',3);",
                         nullptr, nullptr, nullptr),
            SQLITE_OK);
  extras.restore(active.get());
  auto page = read(active.get(), "events");
  ASSERT_EQ(page.rows.size(), 1U);
  EXPECT_EQ(std::get<std::string>(page.rows[0].values[3]), "extension");
  EXPECT_EQ(std::get<std::int64_t>(page.rows[0].values[2]), 3);
  EXPECT_EQ(read(active.get(), "unrelated").rows.size(), 1U);
}

TEST(RuntimeEditor, NullableUniqueKeysDoNotMixExtensionData)
{
  auto canonical =
      database("CREATE TABLE items(id INTEGER PRIMARY KEY,name TEXT UNIQUE);");
  auto active = database(
      "CREATE TABLE items(id INTEGER PRIMARY KEY,name TEXT UNIQUE,custom TEXT);"
      "INSERT INTO items VALUES(1,NULL,'first'),(2,NULL,'second');");
  Extensions extras(active.get(), canonical.get());
  extras.capture(active.get());
  ASSERT_EQ(sqlite3_exec(active.get(), "UPDATE items SET custom=NULL", nullptr,
                         nullptr, nullptr),
            SQLITE_OK);
  extras.restore(active.get());
  const auto page = read(active.get(), "items");
  ASSERT_EQ(page.rows.size(), 2U);
  EXPECT_EQ(std::get<std::string>(page.rows[0].values[2]), "first");
  EXPECT_EQ(std::get<std::string>(page.rows[1].values[2]), "second");
}

TEST(RuntimeEditor, OpaqueJsonMembersSurviveWhileOwnedMembersCanBeRemoved)
{
  auto original = database(
      R"(CREATE TABLE data(id INTEGER PRIMARY KEY, payload TEXT); INSERT INTO data VALUES(1,'{"owned":1,"removed_later":2,"future":{"x":3},"nested":{"known":1,"custom":2}}');)");
  auto normalized = database(
      R"(CREATE TABLE data(id INTEGER PRIMARY KEY, payload TEXT); INSERT INTO data VALUES(1,'{"owned":1,"removed_later":2,"nested":{"known":1}}');)");
  Payloads opaque(original.get(), normalized.get());
  EXPECT_EQ(
      sqlite3_exec(
          normalized.get(),
          R"(UPDATE data SET payload='{"owned":5,"nested":{"known":9}}';)",
          nullptr, nullptr, nullptr),
      SQLITE_OK);
  opaque.restore(normalized.get());
  auto json = nlohmann::json::parse(
      std::get<std::string>(read(normalized.get(), "data").rows[0].values[1]));
  EXPECT_EQ(json["owned"], 5);
  EXPECT_EQ(json["nested"]["known"], 9);
  EXPECT_EQ(json["nested"]["custom"], 2);
  EXPECT_EQ(json["future"]["x"], 3);
  EXPECT_FALSE(json.contains("removed_later"));
}

TEST(RuntimeEditorCareer, EditsBecomeLivePersistAndRecoverWithCustomData)
{
  Logger::init();
  GameController controller;
  controller.newGame(91, 20250702);
  const auto team_id =
      controller.getGameData()->getTeamsVector().front().get().getId();
  const auto player_id = controller.getGameData()
                             ->getPlayersForTeam(team_id)
                             .front()
                             .get()
                             .getId();
  const auto db = controller.getDbConn();
  ASSERT_EQ(
      sqlite3_exec(db->getRaw(),
                   "CREATE TABLE CustomData(key TEXT PRIMARY KEY, value TEXT); "
                   "INSERT INTO CustomData VALUES('extra','preserve'); ALTER "
                   "TABLE Fixtures ADD COLUMN custom_note TEXT DEFAULT '';",
                   nullptr, nullptr, nullptr),
      SQLITE_OK);
  auto session = controller.beginEditor();
  auto player = read(session->database->getRaw(), "Players", 0, "", 50,
                     {{"id", static_cast<std::int64_t>(player_id)}});
  ASSERT_EQ(player.rows.size(), 1U);
  const auto stats_column = std::ranges::find(
      player.table.columns, std::string("stats"), &Column::name);
  const auto stats_index =
      static_cast<std::size_t>(stats_column - player.table.columns.begin());
  auto stats = nlohmann::json::parse(
      std::get<std::string>(player.rows[0].values[stats_index]));
  stats["future_stat"] = 77;
  auto team = read(session->database->getRaw(), "Teams", 0, "", 50,
                   {{"id", static_cast<std::int64_t>(team_id)}});
  auto fixture = read(session->database->getRaw(), "Fixtures", 0, "", 1);
  ASSERT_FALSE(fixture.rows.empty());
  auto custom = read(session->database->getRaw(), "CustomData");
  const auto date = controller.getCurrentDate();
  controller.advanceDay();
  EXPECT_EQ(controller.getCurrentDate(), date);  // Session pauses advancement.
  std::string error;
  ASSERT_TRUE(controller.applyEditor(
      session,
      {update(player, 0, {{"stats", stats.dump()}}),
       update(team, 0, {{"name", std::string("Edited club")}}),
       update(fixture, 0, {{"custom_note", std::string("keep me")}}),
       update(custom, 0, {{"value", std::string("changed")}})},
      error))
      << error;
  EXPECT_EQ(controller.getGameData()->getPlayer(player_id)->get().getStats().at(
                "future_stat"),
            77);
  EXPECT_EQ(controller.getGameData()->getTeam(team_id)->get().getName(),
            "Edited club");
  ASSERT_TRUE(controller.saveGame());
  EXPECT_EQ(read(controller.getDbConn()->getRaw(), "Fixtures", 0, "keep me")
                .rows.size(),
            1U);
  EXPECT_EQ(
      std::get<std::string>(read(controller.getDbConn()->getRaw(), "CustomData")
                                .rows[0]
                                .values[1]),
      "changed");
  ASSERT_TRUE(controller.undoEditor(error)) << error;
  EXPECT_NE(controller.getGameData()->getTeam(team_id)->get().getName(),
            "Edited club");
  EXPECT_FALSE(
      controller.getGameData()->getPlayer(player_id)->get().getStats().contains(
          "future_stat"));
  ASSERT_TRUE(controller.loadGame(91));
  EXPECT_EQ(controller.getGameData()->getPlayer(player_id)->get().getStats().at(
                "future_stat"),
            77);
  EXPECT_EQ(read(controller.getDbConn()->getRaw(), "Fixtures", 0, "keep me")
                .rows.size(),
            1U);
  auto invalid_session = controller.beginEditor();
  auto invalid_player =
      read(invalid_session->database->getRaw(), "Players", 0, "", 50,
           {{"id", static_cast<std::int64_t>(player_id)}});
  EXPECT_FALSE(controller.applyEditor(
      invalid_session,
      {update(invalid_player, 0, {{"age", std::int64_t{999}}})}, error));
  EXPECT_FALSE(error.empty());
  controller.cancelEditor();
}

TEST(RuntimeEditorCareer, RejectsDeletionsRecreatedByRuntimeLoaders)
{
  GameController controller;
  controller.newGame(92, 20250702);
  controller.selectManagedTeam(
      controller.getGameData()->getTeamsVector().front().get().getId());
  ASSERT_EQ(sqlite3_exec(controller.getDbConn()->getRaw(),
                         "CREATE TABLE CustomData(id INTEGER PRIMARY KEY); "
                         "INSERT INTO CustomData VALUES(1)",
                         nullptr, nullptr, nullptr),
            SQLITE_OK);
  auto session = controller.beginEditor();
  const auto profile = read(session->database->getRaw(), "ManagerProfile");
  ASSERT_EQ(profile.rows.size(), 1U);
  auto deletion = update(profile, 0, {});
  deletion.operation = Operation::Delete;
  std::string error;
  EXPECT_FALSE(controller.applyEditor(session, {deletion}, error));
  EXPECT_NE(error.find("recreate"), std::string::npos) << error;
  EXPECT_EQ(controller.getEditorRevision(), 0U);
  EXPECT_EQ(
      read(controller.getDbConn()->getRaw(), "ManagerProfile").rows.size(), 1U);

  const auto custom = read(session->database->getRaw(), "CustomData");
  deletion = update(custom, 0, {});
  deletion.operation = Operation::Delete;
  const Change replacement{"CustomData",
                           custom.schema,
                           Operation::Insert,
                           {},
                           {{"id", std::int64_t{1}}}};
  ASSERT_TRUE(controller.applyEditor(session, {deletion, replacement}, error))
      << error;
  session = controller.beginEditor();
  const auto retained = read(session->database->getRaw(), "CustomData");
  ASSERT_EQ(retained.rows.size(), 1U);
  deletion = update(retained, 0, {});
  deletion.operation = Operation::Delete;
  ASSERT_TRUE(controller.applyEditor(session, {deletion}, error)) << error;
  EXPECT_TRUE(
      read(controller.getDbConn()->getRaw(), "CustomData").rows.empty());
}

TEST(RuntimeEditorCareer, EditingPausesUnemployedAdvancementAndResumesAutosave)
{
  GameController controller;
  controller.newGame(93, 20250702);
  controller.selectManagedTeam(
      controller.getGameData()->getTeamsVector().front().get().getId());
  ASSERT_TRUE(controller.resignFromClub());
  ASSERT_TRUE(controller.isUnemployed());
  controller.setAutosavePolicy(
      {.frequency = AutosaveFrequency::Daily, .backups = 0});
  const auto session = controller.beginEditor();
  const auto before = controller.getCurrentDate();
  EXPECT_EQ(controller.advanceWhileUnemployed(1), 0);
  EXPECT_EQ(controller.getCurrentDate(), before);
  EXPECT_FALSE(controller.saveGame());
  controller.cancelEditor();
  ASSERT_TRUE(controller.saveGame());
  EXPECT_EQ(controller.advanceWhileUnemployed(1), 1);
  EXPECT_NE(controller.getCurrentDate(), before);
  EXPECT_TRUE(controller.getSaveStatus().ok);
  EXPECT_TRUE(controller.getSaveStatus().autosave);
  controller.advanceDay();
  EXPECT_TRUE(controller.getSaveStatus().ok);
  EXPECT_TRUE(controller.getSaveStatus().autosave);
}
