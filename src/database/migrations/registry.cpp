// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <string>

#include "database/migrations/migrations.h"

namespace
{
std::string utcNow()
{
  return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(
                                      std::chrono::system_clock::now()));
}

// 0001: save_meta gets the provenance of the save. Saves from before
// versioning have no creation date; a seed is only recorded when the world
// state stored one (never invented).
void versionMetadata(const Migrations::MigrationContext& context)
{
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(context.db,
                     "UPDATE save_meta SET game_version = ?, engine_version = "
                     "?, rules_edition = ?, rng_version = ?, sim_version = ?, "
                     "created_at_utc = ? WHERE id = 1;",
                     -1, &stmt, nullptr);
  const std::string created = context.fresh ? utcNow() : std::string();
  sqlite3_bind_text(stmt, 1, SaveFormat::GAME_VERSION.data(),
                    static_cast<int>(SaveFormat::GAME_VERSION.size()),
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, SaveFormat::ENGINE_VERSION.data(),
                    static_cast<int>(SaveFormat::ENGINE_VERSION.size()),
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, SaveFormat::RULES_EDITION.data(),
                    static_cast<int>(SaveFormat::RULES_EDITION.size()),
                    SQLITE_STATIC);
  sqlite3_bind_int(stmt, 4, SaveFormat::RNG_VERSION);
  sqlite3_bind_int(stmt, 5, SaveFormat::SIM_VERSION);
  sqlite3_bind_text(stmt, 6, created.c_str(), -1, SQLITE_TRANSIENT);
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE)
    throw DatabaseException(std::string("save_meta: ") +
                            sqlite3_errmsg(context.db));

  if (context.fresh) return;
  const bool seeded =
      Migrations::tableExists(context.db, "WorldState") &&
      sqlite3_exec(context.db,
                   "UPDATE save_meta SET world_seed = (SELECT seed FROM "
                   "WorldState WHERE id = 1) WHERE id = 1 AND EXISTS (SELECT "
                   "1 FROM WorldState WHERE id = 1);",
                   nullptr, nullptr, nullptr) == SQLITE_OK &&
      sqlite3_changes(context.db) > 0;
  if (!seeded)
    sqlite3_exec(context.db, "UPDATE save_meta SET seed_unknown = 1;", nullptr,
                 nullptr, nullptr);
}

// Columns that earlier builds added with "ALTER TABLE ... fails harmlessly".
constexpr std::array LEAGUE_TIEBREAK = {
    Migrations::ColumnSpec{"Leagues", "tiebreak", "INTEGER NOT NULL DEFAULT 0"},
};

constexpr std::array FIXTURE_COMPETITIONS = {
    Migrations::ColumnSpec{"Fixtures", "competition_id",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"Fixtures", "stage", "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"Fixtures", "extra_time",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"Fixtures", "home_penalties", "INTEGER"},
    Migrations::ColumnSpec{"Fixtures", "away_penalties", "INTEGER"},
};

constexpr std::array TRANSFER_BIDS = {
    Migrations::ColumnSpec{"TransferList", "highest_bidder_id", "INTEGER"},
    Migrations::ColumnSpec{"TransferList", "highest_bid",
                           "INTEGER NOT NULL DEFAULT 0"},
};

constexpr std::array WORLD_SIMULATION = {
    Migrations::ColumnSpec{"Players", "potential", "REAL NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"Players", "traits", "TEXT NOT NULL DEFAULT ''"},
    Migrations::ColumnSpec{"Players", "dynamics", "TEXT NOT NULL DEFAULT ''"},
    Migrations::ColumnSpec{"Teams", "reputation", "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"Teams", "stadium_capacity",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"Teams", "ticket_price",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"Teams", "training_facilities",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"Teams", "youth_facilities",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"Teams", "transfer_budget",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"Teams", "wage_budget",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"Teams", "recent_form", "TEXT NOT NULL DEFAULT ''"},
};

// Scout reports record the ability range and whether they were opened;
// reports of older saves count as already read.
constexpr std::array SCOUT_REPORT_DETAILS = {
    Migrations::ColumnSpec{"ScoutReports", "overall_low",
                           "REAL NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"ScoutReports", "overall_high",
                           "REAL NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"ScoutReports", "seen",
                           "INTEGER NOT NULL DEFAULT 1"},
};

// Staff ids are never reused: older saves derive the counter from the
// stored staff (0 = unknown).
constexpr std::array STAFF_ID_COUNTER = {
    Migrations::ColumnSpec{"WorldState", "next_staff_id",
                           "INTEGER NOT NULL DEFAULT 0"},
};

// League rounds are spread over several days with a kick-off time per
// fixture; fixtures of older saves keep the usual time of their competition.
constexpr std::array FIXTURE_KICKOFF = {
    Migrations::ColumnSpec{"Fixtures", "kickoff", "INTEGER NOT NULL DEFAULT 0"},
};

// Offers for the managed club's players are negotiated: whose move it is
// and when the buyer answers, and players declared not for sale. Offers of
// older saves wait for the club (their bid opens the rounds on load).
constexpr std::array OFFER_NEGOTIATIONS = {
    Migrations::ColumnSpec{"TransferOffers", "status",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"TransferOffers", "respond_on",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"PlayerMarketFlags", "not_for_sale_until",
                           "INTEGER NOT NULL DEFAULT 0"},
};

// U21 squads: every academy gets a row in its country's U21 league and a
// flag telling whether its U21 squad was set up (older saves set it up on
// the next day), and managed results tell U18 from U21 matches.
constexpr std::array RESERVE_SQUADS = {
    Migrations::ColumnSpec{"YouthAcademies", "reserve_played",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"YouthAcademies", "reserve_won",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"YouthAcademies", "reserve_drawn",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"YouthAcademies", "reserve_lost",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"YouthAcademies", "reserve_goals_for",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"YouthAcademies", "reserve_goals_against",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"YouthAcademies", "reserves_ready",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"YouthResults", "squad",
                           "INTEGER NOT NULL DEFAULT 0"},
};

// Managed matches keep their touch maps, pass network and pressing numbers
// as a compact blob; matches of older saves have none (NULL).
constexpr std::array MATCH_DETAIL = {
    Migrations::ColumnSpec{"ManagedMatchAnalytics", "detail", "BLOB"},
};

// The board sets cup, finance and youth targets next to the league one;
// boards of older saves get them at the next season start.
constexpr std::array BOARD_OBJECTIVES = {
    Migrations::ColumnSpec{"BoardState", "cup_objective",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"BoardState", "finance_objective",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"BoardState", "youth_target",
                           "INTEGER NOT NULL DEFAULT 0"},
    Migrations::ColumnSpec{"BoardState", "start_balance",
                           "INTEGER NOT NULL DEFAULT 0"},
};

// Players keep a squad number; rows of older saves have none (0) and are
// numbered when the save is loaded (GameData::assignSquadNumbers).
constexpr std::array SQUAD_NUMBERS = {
    Migrations::ColumnSpec{"Players", "squad_number",
                           "INTEGER NOT NULL DEFAULT 0"},
};

// Boards record whether the cup, finance and youth targets were set for
// the season. Boards that already announced them (a value differs from the
// column defaults) keep being judged on them; boards of saves upgraded by
// 0012 get them at the next season start and are judged on the league
// until then.
constexpr std::array BOARD_TARGETS_SET = {
    Migrations::ColumnSpec{"BoardState", "targets_set",
                           "INTEGER NOT NULL DEFAULT 0"},
};

void boardTargetsSet(const Migrations::MigrationContext& context)
{
  if (context.fresh) return;
  char* error = nullptr;
  const int rc = sqlite3_exec(
      context.db,
      "UPDATE BoardState SET targets_set = 1 WHERE cup_objective <> 0 OR "
      "finance_objective <> 0 OR youth_target <> 0 OR start_balance <> 0;",
      nullptr, nullptr, &error);
  const std::string message = error ? error : "";
  sqlite3_free(error);
  if (rc != SQLITE_OK) throw DatabaseException("BoardState: " + message);
}

constexpr std::array PLAYER_NICKNAME = {
    Migrations::ColumnSpec{"Players", "nickname", "TEXT NOT NULL DEFAULT ''"},
};

// Append new migrations at the end with the next number; never renumber,
// edit or remove a released one (see README.md).
constexpr std::array<Migrations::Migration, 15> REGISTRY = {{
    {1, "0001_version_metadata", {}, &versionMetadata},
    {2, "0002_league_tiebreak", LEAGUE_TIEBREAK, nullptr},
    {3, "0003_fixture_competitions", FIXTURE_COMPETITIONS, nullptr},
    {4, "0004_transfer_bids", TRANSFER_BIDS, nullptr},
    {5, "0005_world_simulation", WORLD_SIMULATION, nullptr},
    {6, "0006_scout_report_details", SCOUT_REPORT_DETAILS, nullptr},
    {7, "0007_staff_id_counter", STAFF_ID_COUNTER, nullptr},
    {8, "0008_fixture_kickoff", FIXTURE_KICKOFF, nullptr},
    {9, "0009_offer_negotiations", OFFER_NEGOTIATIONS, nullptr},
    {10, "0010_reserve_squads", RESERVE_SQUADS, nullptr},
    {11, "0011_match_detail", MATCH_DETAIL, nullptr},
    {12, "0012_board_objectives", BOARD_OBJECTIVES, nullptr},
    {13, "0013_squad_numbers", SQUAD_NUMBERS, nullptr},
    {14, "0014_board_targets_set", BOARD_TARGETS_SET, &boardTargetsSet},
    {15, "0015_player_nickname", PLAYER_NICKNAME, nullptr},
}};

static_assert(std::ranges::is_sorted(REGISTRY, {},
                                     &Migrations::Migration::number),
              "migrations must be ordered by number");
}  // namespace

std::span<const Migrations::Migration> Migrations::registry()
{
  return REGISTRY;
}

int Migrations::currentSchemaVersion() { return REGISTRY.back().number; }
