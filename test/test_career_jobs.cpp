// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// U21 squads (age rules, over-age places, the cheap U21 leagues, computer
// clubs, persistence and older saves) and national-team jobs (rules,
// applications and offers, call-up validity, results and reputation,
// persistence), plus the cost of a busy day with all of it.

#include <gtest/gtest.h>
#include <sqlite3.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "controller/game_controller.h"
#include "database/database_connection.h"
#include "database/gamedata.h"
#include "database/migrations/migrations.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/calendar.h"
#include "model/inbox.h"
#include "model/national_job.h"
#include "model/world_rng.h"
#include "model/world_simulation.h"
#include "model/youth_academy.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 820'000 + (getpid() % 10'000) * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

std::unique_ptr<GameController> makeWorld(int slot)
{
  Logger::init();
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot, WORLD_SEED);
  return controller;
}

/** A new world after its first day: the national teams form when the
 * season's international calendar is planned. */
std::unique_ptr<GameController> makeNationsWorld(int slot)
{
  auto controller = makeWorld(slot);
  controller->advanceDay();
  return controller;
}

/** The most reputable club of a league. */
TeamID topClub(const GameController& controller, LeagueID league_id)
{
  TeamID best = 0;
  for (const TeamID id :
       controller.getLeagueById(league_id)->get().getTeamIDs())
  {
    if (best == 0 || controller.getTeamById(id)->get().getReputation() >
                         controller.getTeamById(best)->get().getReputation())
      best = id;
  }
  return best;
}

void runDays(YouthAcademy& academy, GameDateValue from, GameDateValue to,
             TeamID managed, Inbox& inbox)
{
  while (from < to)
  {
    from = SeasonCalendar::addDays(from, 1);
    academy.onDayAdvanced(from, managed, inbox);
  }
}

bool hasMessage(const std::vector<InboxMessage>& messages,
                const std::string& title_key)
{
  return std::ranges::any_of(messages, [&](const InboxMessage& message)
                             { return message.title_key == title_key; });
}

bool listed(const std::vector<std::reference_wrapper<const Player>>& players,
            PlayerID player_id)
{
  return std::ranges::any_of(players, [player_id](const Player& player)
                             { return player.getId() == player_id; });
}

/** Players of @p team in its senior squad within an age band, by id. */
std::vector<PlayerID> seniorsAged(const GameData& gamedata, TeamID team,
                                  int min_age, int max_age, bool keepers)
{
  std::vector<PlayerID> ids;
  for (const Player& player : gamedata.getPlayersForTeam(team))
  {
    if (player.getAge() < min_age || player.getAge() > max_age) continue;
    if ((player.getRole() == PlayerRole::GK) != keepers) continue;
    ids.push_back(player.getId());
  }
  std::ranges::sort(ids);
  return ids;
}

/** A manager out of work with a national reputation. */
void createInternationalManager(GameController& controller,
                                Language nationality)
{
  ManagerSetup setup;
  setup.first_name = "Livio";
  setup.last_name = "Castellani";
  setup.nationality = nationality;
  setup.age = 48;
  setup.background = ManagerBackground::FormerInternational;
  controller.createManager(setup);
}

/** The lowest-rated nations (at most five) whose pool is deep enough for
 * any squad, weakest first. */
std::vector<Language> smallNations(GameController& controller)
{
  const NationalTeams& teams = controller.getGame()->getNationalTeams();
  const std::vector<Language> ranking = teams.ranking();
  std::vector<Language> nations;
  for (auto it = ranking.rbegin(); it != ranking.rend() && nations.size() < 5;
       ++it)
    if (teams.eligiblePool(*it, controller.getCurrentDate()).size() >= 40)
      nations.push_back(*it);
  return nations;
}

/** Opens jobs at those nations and applies until an offer arrives; returns
 * the offer id (0 when none came). */
std::uint32_t obtainNationalOffer(GameController& controller)
{
  Game* game = controller.getGame();
  for (const Language nation : smallNations(controller))
  {
    game->getNationalJob().openVacancy(nation, controller.getCurrentDate());
    const NationalApplyResult result = controller.applyForNationalJob(nation);
    EXPECT_TRUE(result == NationalApplyResult::Ok ||
                result == NationalApplyResult::AlreadyApplied)
        << NationalJobModel::applyResultKey(result);
  }
  for (int day = 0; day < 12 && controller.getNationalJobOffers().empty();
       ++day)
    controller.advanceDay();
  return controller.getNationalJobOffers().empty()
             ? 0
             : controller.getNationalJobOffers().front().id;
}
}  // namespace

// ---------------------------------------------------------------------------
// U21 squads
// ---------------------------------------------------------------------------

TEST(ReserveSquadTest, AgeRulesAndOverAgePlaces)
{
  ReserveQuota quota;
  EXPECT_EQ(YouthModel::reserveEligibility(17, PlayerRole::CM, quota),
            YouthActionResult::Ok);
  EXPECT_EQ(YouthModel::reserveEligibility(21, PlayerRole::GK, quota),
            YouthActionResult::Ok);
  EXPECT_EQ(YouthModel::reserveEligibility(24, PlayerRole::ST, quota),
            YouthActionResult::Ok);
  quota.overage_outfield = YouthModel::U21_OVERAGE_OUTFIELD;
  EXPECT_EQ(YouthModel::reserveEligibility(22, PlayerRole::ST, quota),
            YouthActionResult::OverageFull);
  EXPECT_EQ(YouthModel::reserveEligibility(21, PlayerRole::ST, quota),
            YouthActionResult::Ok)
      << "players of U21 age never use an over-age place";
  EXPECT_EQ(YouthModel::reserveEligibility(30, PlayerRole::GK, quota),
            YouthActionResult::Ok)
      << "the goalkeeper's place is separate";
  quota.overage_goalkeepers = YouthModel::U21_OVERAGE_GOALKEEPERS;
  EXPECT_EQ(YouthModel::reserveEligibility(23, PlayerRole::GK, quota),
            YouthActionResult::OverageFull);
  EXPECT_STREQ(YouthModel::actionResultKey(YouthActionResult::OverageFull),
               "YOUTH_ACTION_OVERAGE_FULL");
}

TEST(ReserveSquadTest, PlayersMoveBetweenFirstTeamU21AndU18)
{
  const SlotCleanup slot{uniqueSlot(0)};
  auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID managed = topClub(*controller, 1);
  controller->selectManagedTeam(managed);
  YouthAcademy& academy = controller->getGame()->getWorld().getYouth();
  academy.ensureReady();

  // A young first-team player joins the U21s and leaves the senior squad.
  const std::vector<PlayerID> young =
      seniorsAged(*gamedata, managed, 18, YouthModel::U21_MAX_AGE, false);
  ASSERT_FALSE(young.empty());
  const PlayerID prospect = young.front();
  ASSERT_EQ(controller->moveToReserves(prospect), YouthActionResult::Ok);
  EXPECT_EQ(academy.record(prospect)->status, YouthStatus::Reserve);
  EXPECT_TRUE(controller->isAcademyPlayer(prospect));
  EXPECT_FALSE(listed(gamedata->getPlayersForTeam(managed), prospect));
  EXPECT_TRUE(listed(gamedata->getAcademyForTeam(managed), prospect));
  EXPECT_EQ(controller->moveToReserves(prospect),
            YouthActionResult::NotAllowed);
  ASSERT_EQ(controller->getYouthPlayers(YouthStatus::Reserve).size(), 1u);

  // Over-age places: three outfield players and one goalkeeper.
  const std::vector<PlayerID> outfield =
      seniorsAged(*gamedata, managed, YouthModel::U21_MAX_AGE + 1, 40, false);
  ASSERT_GE(outfield.size(), YouthModel::U21_OVERAGE_OUTFIELD + 1);
  for (std::size_t i = 0; i < YouthModel::U21_OVERAGE_OUTFIELD; ++i)
    EXPECT_EQ(controller->moveToReserves(outfield[i]), YouthActionResult::Ok);
  EXPECT_EQ(
      controller->moveToReserves(outfield[YouthModel::U21_OVERAGE_OUTFIELD]),
      YouthActionResult::OverageFull);
  if (const std::vector<PlayerID> keepers = seniorsAged(
          *gamedata, managed, YouthModel::U21_MAX_AGE + 1, 40, true);
      keepers.size() >= 2)
  {
    EXPECT_EQ(controller->moveToReserves(keepers[0]), YouthActionResult::Ok);
    EXPECT_EQ(controller->moveToReserves(keepers[1]),
              YouthActionResult::OverageFull);
  }
  const GameController::ReserveOverview overview =
      controller->getReserveOverview();
  EXPECT_EQ(overview.quota.overage_outfield, YouthModel::U21_OVERAGE_OUTFIELD);
  EXPECT_GT(overview.league_size, 20) << "one U21 league per country";

  // Back to the first team: the over-age place is free again.
  ASSERT_EQ(controller->promoteReservePlayer(outfield[0]),
            YouthActionResult::Ok);
  EXPECT_TRUE(listed(gamedata->getPlayersForTeam(managed), outfield[0]));
  EXPECT_FALSE(controller->isAcademyPlayer(outfield[0]));
  EXPECT_EQ(
      controller->moveToReserves(outfield[YouthModel::U21_OVERAGE_OUTFIELD]),
      YouthActionResult::Ok);

  // U18 and U21: up from 17, back down only while young enough.
  const auto u18 = academy.members(managed, YouthStatus::Squad);
  ASSERT_FALSE(u18.empty());
  const PlayerID junior = u18.front()->player_id;
  ASSERT_EQ(controller->moveToReserves(junior), YouthActionResult::Ok);
  EXPECT_EQ(academy.record(junior)->status, YouthStatus::Reserve);
  EXPECT_EQ(controller->moveReserveToU18(junior), YouthActionResult::Ok);
  EXPECT_EQ(academy.record(junior)->status, YouthStatus::Squad);
  if (gamedata->getPlayer(prospect)->get().getAge() > YouthModel::U18_MAX_AGE)
    EXPECT_EQ(controller->moveReserveToU18(prospect),
              YouthActionResult::TooOld);

  // Another club's players are not ours to move.
  const TeamID other =
      controller->getLeagueById(1)->get().getTeamIDs().front() == managed
          ? controller->getLeagueById(1)->get().getTeamIDs()[1]
          : controller->getLeagueById(1)->get().getTeamIDs()[0];
  EXPECT_EQ(controller->moveToReserves(
                gamedata->getPlayersForTeam(other).front().get().getId()),
            YouthActionResult::NotAllowed);
}

TEST(ReserveSquadTest, U21LeaguesArePlayedWeeklyAndMinutesSpeedUpGrowth)
{
  const SlotCleanup slot{uniqueSlot(1)};
  auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID managed = topClub(*controller, 1);
  YouthAcademy academy(gamedata);
  academy.ensureReady();
  for (const PlayerID player_id :
       seniorsAged(*gamedata, managed, 18, YouthModel::U21_MAX_AGE, false))
    academy.moveToReserves(managed, player_id);
  ASSERT_GE(academy.reserveQuota(managed).squad, 1u);
  Inbox inbox;
  runDays(academy, GameDateValue(2025, 8, 1), GameDateValue(2025, 11, 30),
          managed, inbox);

  // Every club of the country plays once a week, in one table.
  const std::vector<YouthTableRow> table = academy.reserveTable(managed);
  ASSERT_GT(table.size(), 20u);
  int played_total = 0;
  int goals_for = 0;
  int goals_against = 0;
  for (const YouthTableRow& row : table)
  {
    EXPECT_GE(row.played, 14) << row.team_id;
    EXPECT_LE(row.played, 18) << row.team_id;
    played_total += row.played;
    goals_for += row.goals_for;
    goals_against += row.goals_against;
  }
  EXPECT_EQ(goals_for, goals_against);
  EXPECT_GT(played_total, 0);
  EXPECT_EQ(academy.reserveResults().size(),
            academy.club(managed)->reserve_table.played);
  // The U18 results stay separate.
  EXPECT_NE(academy.results().size(), 0u);

  // Minutes are recorded and lift development above a senior who does not
  // play.
  float best = 0.0f;
  for (const YouthRecord* youth :
       academy.members(managed, YouthStatus::Reserve))
  {
    EXPECT_GT(youth->minutes, 0u);
    best = std::max(best, academy.developmentMultiplier(
                              gamedata->getPlayer(youth->player_id)->get()));
  }
  EXPECT_GT(best, 1.0f);
}

TEST(ReserveSquadTest, ComputerClubsRunTheirU21sAndAgeRulesHoldAtSeasonStart)
{
  const SlotCleanup slot{uniqueSlot(2)};
  auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID managed = topClub(*controller, 1);
  YouthAcademy academy(gamedata);
  academy.ensureReady();
  // The managed U18 squad gets an 18-year-old who will be over age.
  const auto u18 = academy.members(managed, YouthStatus::Squad);
  ASSERT_FALSE(u18.empty());
  const PlayerID graduate = u18.front()->player_id;
  gamedata->getPlayers().at(graduate).setAge(18);
  const std::size_t managed_seniors = academy.firstTeamSize(managed);
  Inbox inbox;
  runDays(academy, GameDateValue(2025, 7, 2), GameDateValue(2025, 7, 10),
          managed, inbox);

  // Day one: computer clubs move young players outside their plans down,
  // keeping their senior squads at the transfer target; the manager's squad
  // is left alone.
  EXPECT_EQ(academy.firstTeamSize(managed), managed_seniors);
  EXPECT_EQ(academy.reserveQuota(managed).squad, 0u);
  std::size_t clubs_with_u21 = 0;
  for (const auto& team_ref : controller->getTeams())
  {
    const TeamID team_id = team_ref.get().getId();
    if (team_id == managed) continue;
    const ReserveQuota quota = academy.reserveQuota(team_id);
    EXPECT_LE(quota.squad, YouthModel::U21_SQUAD_LIMIT);
    if (quota.squad > 0)
    {
      ++clubs_with_u21;
      EXPECT_GE(academy.firstTeamSize(team_id), 26u)
          << team_ref.get().getName();
    }
    for (const YouthRecord* youth :
         academy.members(team_id, YouthStatus::Reserve))
      EXPECT_LE(gamedata->getPlayer(youth->player_id)->get().getAge(),
                YouthModel::U21_MAX_AGE);
  }
  std::cout << "[u21] computer clubs with a U21 squad on day one: "
            << clubs_with_u21 << "\n";

  // Season rollover: ageing, then the age rules.
  academy.onSeasonEnd(GameDateValue(2026, 7, 1), managed);
  gamedata->ageAllPlayers();
  academy.onSeasonStart(GameDateValue(2026, 7, 1), managed, inbox);
  ASSERT_NE(academy.record(graduate), nullptr);
  EXPECT_EQ(academy.record(graduate)->status, YouthStatus::Reserve)
      << "over-age U18 players move up to the U21s";
  EXPECT_TRUE(hasMessage(inbox.getMessages(), "INBOX_YOUTH_TO_U21_TITLE"));
  for (const auto& team_ref : controller->getTeams())
  {
    const TeamID team_id = team_ref.get().getId();
    for (const YouthRecord* youth :
         academy.members(team_id, YouthStatus::Squad))
      EXPECT_LE(gamedata->getPlayer(youth->player_id)->get().getAge(),
                YouthModel::U18_MAX_AGE);
    const ReserveQuota quota = academy.reserveQuota(team_id);
    EXPECT_LE(quota.overage_outfield, YouthModel::U21_OVERAGE_OUTFIELD);
    EXPECT_LE(quota.overage_goalkeepers, YouthModel::U21_OVERAGE_GOALKEEPERS);
    if (team_id != managed)
    {
      EXPECT_EQ(quota.overage_outfield + quota.overage_goalkeepers, 0u)
          << "computer clubs never use the over-age places";
      EXPECT_LE(quota.squad, YouthModel::U21_SQUAD_LIMIT);
      clubs_with_u21 += quota.squad > 0 ? 1 : 0;
    }
  }
  EXPECT_GT(clubs_with_u21, 0u) << "U18 graduates feed computer U21 squads";
}

TEST(ReserveSquadTest, TwoSeasonsKeepComputerSeniorSquadsFull)
{
  const SlotCleanup slot{uniqueSlot(8)};
  auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID managed = topClub(*controller, 1);
  WorldSimulation world(gamedata);
  // The world between matches for two seasons with Game::endSeason's
  // rollover (ageing, expiring contracts) and no transfer market: squads
  // only lose players, so the academies alone must keep them full.
  GameDateValue date = controller->getCurrentDate();
  for (int day = 0; day < 732; ++day)
  {
    date = SeasonCalendar::addDays(date, 1);
    world.onDayAdvanced(date, managed);
    if (date.month == 7 && date.day == 1)
    {
      world.onSeasonEnd(date, managed);
      gamedata->ageAllPlayers();
      gamedata->advanceContractsAndReleasePlayers();
      world.onSeasonStart(date, managed);
    }
  }
  const YouthAcademy& academy = world.getYouth();
  std::size_t clubs = 0;
  std::size_t in_band = 0;
  double seniors_total = 0.0;
  double reserves_total = 0.0;
  for (const auto& team_ref : controller->getTeams())
  {
    const TeamID team_id = team_ref.get().getId();
    if (team_id == managed) continue;
    ++clubs;
    const std::size_t seniors = academy.firstTeamSize(team_id);
    seniors_total += static_cast<double>(seniors);
    reserves_total += static_cast<double>(academy.reserveQuota(team_id).squad);
    in_band += seniors >= 24 && seniors <= 28 ? 1 : 0;
  }
  const double mean = seniors_total / static_cast<double>(clubs);
  std::cout << "[u21] after two seasons: senior squads mean " << mean << " ("
            << in_band << "/" << clubs << " within 24-28), U21 squads mean "
            << reserves_total / static_cast<double>(clubs) << "\n";
  EXPECT_GE(mean, 24.0);
  EXPECT_LE(mean, 28.5);
  EXPECT_GE(in_band * 10, clubs * 7);
}

TEST(ReservePersistenceTest, U21StateRoundTripsAndOlderSavesSetItUp)
{
  const SlotCleanup slot{uniqueSlot(3)};
  auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID managed = topClub(*controller, 1);
  YouthAcademy academy(gamedata);
  academy.ensureReady();
  const std::vector<PlayerID> young =
      seniorsAged(*gamedata, managed, 18, YouthModel::U21_MAX_AGE, false);
  ASSERT_FALSE(young.empty());
  ASSERT_EQ(academy.moveToReserves(managed, young.front()),
            YouthActionResult::Ok);
  Inbox inbox;
  runDays(academy, GameDateValue(2025, 8, 1), GameDateValue(2025, 9, 15),
          managed, inbox);
  ASSERT_FALSE(academy.reserveResults().empty());

  const auto db = controller->getDbConn();
  academy.save(db);
  YouthAcademy restored(gamedata);
  restored.load(db);
  EXPECT_EQ(restored.record(young.front())->status, YouthStatus::Reserve);
  EXPECT_EQ(restored.reserveResults().size(), academy.reserveResults().size());
  EXPECT_EQ(restored.results().size(), academy.results().size());
  EXPECT_EQ(restored.club(managed)->reserve_table.points(),
            academy.club(managed)->reserve_table.points());
  EXPECT_TRUE(restored.club(managed)->reserves_ready);
  std::size_t reserves = 0;
  for (const auto& team_ref : controller->getTeams())
  {
    const TeamID team_id = team_ref.get().getId();
    EXPECT_EQ(restored.reserveQuota(team_id).squad,
              academy.reserveQuota(team_id).squad);
    reserves += restored.reserveQuota(team_id).squad;
  }
  EXPECT_GT(reserves, 1u);
  EXPECT_TRUE(gamedata->getPlayer(young.front())->get().isAcademyPlayer());

  // A save from before the U21s: no U21 players, flags unset. Computer
  // clubs set their squads up on the next day, the manager's is untouched.
  sqlite3_exec(db->getRaw(),
               "DELETE FROM YouthPlayers WHERE status = 3; UPDATE "
               "YouthAcademies SET reserves_ready = 0, reserve_played = 0, "
               "reserve_won = 0, reserve_drawn = 0, reserve_lost = 0, "
               "reserve_goals_for = 0, reserve_goals_against = 0; DELETE FROM "
               "YouthResults WHERE squad = 1;",
               nullptr, nullptr, nullptr);
  YouthAcademy legacy(gamedata);
  legacy.load(db);
  EXPECT_EQ(legacy.reserveQuota(managed).squad, 0u);
  EXPECT_FALSE(gamedata->getPlayer(young.front())->get().isAcademyPlayer());
  EXPECT_TRUE(legacy.reserveResults().empty());
  legacy.onDayAdvanced(GameDateValue(2025, 9, 16), managed, inbox);
  std::size_t rebuilt = 0;
  for (const auto& team_ref : controller->getTeams())
    rebuilt += legacy.reserveQuota(team_ref.get().getId()).squad;
  EXPECT_GT(rebuilt, 0u);
  EXPECT_EQ(legacy.reserveQuota(managed).squad, 0u);
  EXPECT_TRUE(legacy.club(managed)->reserves_ready);
}

TEST(ReserveSaveMigration, YouthTablesOfOlderSavesGetTheU21Columns)
{
  Logger::init();
  DatabaseConnection connection(":memory:");
  Migrations::migrate(connection);
  sqlite3* db = connection.getRaw();
  // The layout of a version 9 save with one academy and one result.
  for (const char* sql :
       {"DROP TABLE YouthAcademies;", "DROP TABLE YouthResults;",
        "CREATE TABLE YouthAcademies (team_id INTEGER PRIMARY KEY, recruitment "
        "INTEGER NOT NULL, project INTEGER NOT NULL, project_start_day INTEGER "
        "NOT NULL, project_done_day INTEGER NOT NULL, project_target INTEGER "
        "NOT NULL, last_request_day INTEGER NOT NULL, played INTEGER NOT NULL, "
        "won INTEGER NOT NULL, drawn INTEGER NOT NULL, lost INTEGER NOT NULL, "
        "goals_for INTEGER NOT NULL, goals_against INTEGER NOT NULL);",
        "CREATE TABLE YouthResults (seq INTEGER PRIMARY KEY, date INTEGER NOT "
        "NULL, opponent_id INTEGER NOT NULL, home INTEGER NOT NULL, goals_for "
        "INTEGER NOT NULL, goals_against INTEGER NOT NULL);",
        "INSERT INTO YouthAcademies VALUES (4, 60, 0, 0, 0, 0, 0, 3, 2, 1, 0, "
        "7, 2);",
        "INSERT INTO YouthResults VALUES (0, 20251004, 9, 1, 3, 1);",
        "DELETE FROM schema_migrations WHERE number >= 10;",
        "UPDATE save_meta SET schema_version = 9;"})
    ASSERT_EQ(sqlite3_exec(db, sql, nullptr, nullptr, nullptr), SQLITE_OK)
        << sql;

  const auto report = Migrations::migrate(connection);
  EXPECT_EQ(report.from_version, 9);
  ASSERT_FALSE(report.applied.empty());
  EXPECT_EQ(report.applied.front(), 10);
  for (const char* column :
       {"reserve_played", "reserve_won", "reserve_drawn", "reserve_lost",
        "reserve_goals_for", "reserve_goals_against", "reserves_ready"})
    EXPECT_TRUE(Migrations::columnExists(db, "YouthAcademies", column))
        << column;
  EXPECT_TRUE(Migrations::columnExists(db, "YouthResults", "squad"));
  // Existing rows keep their values; the U21 columns start empty.
  sqlite3_stmt* stmt = nullptr;
  ASSERT_EQ(sqlite3_prepare_v2(db,
                               "SELECT played, reserve_played, reserves_ready "
                               "FROM YouthAcademies WHERE team_id = 4;",
                               -1, &stmt, nullptr),
            SQLITE_OK);
  ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
  EXPECT_EQ(sqlite3_column_int(stmt, 0), 3);
  EXPECT_EQ(sqlite3_column_int(stmt, 1), 0);
  EXPECT_EQ(sqlite3_column_int(stmt, 2), 0);
  sqlite3_finalize(stmt);
  ASSERT_EQ(sqlite3_prepare_v2(db, "SELECT squad FROM YouthResults;", -1, &stmt,
                               nullptr),
            SQLITE_OK);
  ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
  EXPECT_EQ(sqlite3_column_int(stmt, 0), 0) << "old results are U18 results";
  sqlite3_finalize(stmt);
  // National-job tables exist (created by the schema).
  for (const char* table :
       {"NationalJobState", "NationalJobHistory", "NationalVacancies",
        "NationalApplications", "NationalJobOffers"})
    EXPECT_TRUE(Migrations::tableExists(db, table)) << table;
  // Running it again changes nothing.
  const auto again = Migrations::migrate(connection);
  EXPECT_TRUE(again.applied.empty());
  EXPECT_TRUE(again.repaired.empty());
}

// ---------------------------------------------------------------------------
// National-team jobs
// ---------------------------------------------------------------------------

TEST(NationalJobModelTest, StatureChanceWageAndContract)
{
  EXPECT_FLOAT_EQ(NationalJobModel::stature(0, 31), 92.0f);
  EXPECT_FLOAT_EQ(NationalJobModel::stature(30, 31), 42.0f);
  EXPECT_GT(NationalJobModel::stature(3, 31),
            NationalJobModel::stature(10, 31));
  EXPECT_EQ(NationalJobModel::requiredLicence(90.0f), CoachingLicence::Pro);
  EXPECT_EQ(NationalJobModel::requiredLicence(50.0f), CoachingLicence::A);

  // Reputation, licence and nationality decide.
  EXPECT_EQ(NationalJobModel::applicationChance(40.0f, CoachingLicence::Pro,
                                                45.0f, true),
            0.0f)
      << "below a national reputation federations do not answer";
  const float local = NationalJobModel::applicationChance(
      60.0f, CoachingLicence::A, 60.0f, true);
  const float foreign = NationalJobModel::applicationChance(
      60.0f, CoachingLicence::A, 60.0f, false);
  EXPECT_GT(local, foreign);
  EXPECT_GT(NationalJobModel::applicationChance(80.0f, CoachingLicence::Pro,
                                                90.0f, false),
            NationalJobModel::applicationChance(60.0f, CoachingLicence::Pro,
                                                90.0f, false));
  EXPECT_GT(NationalJobModel::applicationChance(80.0f, CoachingLicence::Pro,
                                                85.0f, false),
            NationalJobModel::applicationChance(80.0f, CoachingLicence::B,
                                                85.0f, false));
  EXPECT_GT(NationalJobModel::weeklyWage(90.0f),
            10 * NationalJobModel::weeklyWage(45.0f));
  EXPECT_FALSE(NationalJobModel::canCombineWithClub(69.0f));
  EXPECT_TRUE(NationalJobModel::canCombineWithClub(70.0f));

  // Contracts end the summer after the next finals, at least ten months on.
  EXPECT_EQ(NationalJobModel::contractEnd(GameDateValue(2025, 9, 1)),
            GameDateValue(2026, 7, 31));
  EXPECT_EQ(NationalJobModel::contractEnd(GameDateValue(2025, 12, 1)),
            GameDateValue(2028, 7, 31));
  EXPECT_EQ(NationalJobModel::contractEnd(GameDateValue(2026, 8, 10)),
            GameDateValue(2028, 7, 31));

  // Results against expectation move the reputation, finals weigh most.
  using International::Competition;
  EXPECT_GT(
      NationalJobModel::resultReputation(Competition::WorldQualifier, 0.3, 1.0),
      0.0f);
  EXPECT_LT(NationalJobModel::resultReputation(Competition::Friendly, 0.7, 0.0),
            0.0f);
  EXPECT_GT(
      NationalJobModel::resultReputation(Competition::WorldFinals, 0.5, 1.0),
      NationalJobModel::resultReputation(Competition::Friendly, 0.5, 1.0));
}

TEST(NationalJobTest, OfferAcceptanceCallUpsAndResults)
{
  const SlotCleanup slot{uniqueSlot(4)};
  auto controller = makeNationsWorld(slot.slot);
  Game* game = controller->getGame();
  const std::vector<Language> ranking = game->getNationalTeams().ranking();
  ASSERT_GE(ranking.size(), 8u);
  const std::vector<Language> small = smallNations(*controller);
  ASSERT_FALSE(small.empty());
  const Language home_nation = small.front();
  createInternationalManager(*controller, home_nation);
  ASSERT_TRUE(controller->isUnemployed());
  EXPECT_FALSE(controller->hasNationalJob());
  EXPECT_FALSE(controller->getCallUpView().announced);

  // Apply, then the federation answers within days.
  game->getNationalJob().openVacancy(home_nation, controller->getCurrentDate());
  const auto vacancies = controller->getNationalVacancies();
  ASSERT_FALSE(vacancies.empty());
  EXPECT_TRUE(std::ranges::any_of(
      vacancies, [&](const auto& view)
      { return view.nation == home_nation && view.compatriot; }));
  EXPECT_EQ(controller->applyForNationalJob(home_nation),
            NationalApplyResult::Ok);
  EXPECT_EQ(controller->applyForNationalJob(home_nation),
            NationalApplyResult::AlreadyApplied);
  const std::uint32_t offer_id = obtainNationalOffer(*controller);
  ASSERT_NE(offer_id, 0u) << "a well-known compatriot gets an offer";
  const Language nation = controller->getNationalJobOffers().front().nation;
  ASSERT_EQ(controller->acceptNationalJobOffer(offer_id),
            NationalApplyResult::Ok);
  ASSERT_TRUE(controller->hasNationalJob());
  EXPECT_EQ(controller->getNationalJob()->nation, nation);
  EXPECT_TRUE(controller->getNationalJobOffers().empty());
  EXPECT_TRUE(game->getCareer().hasInternationalDuty());
  EXPECT_EQ(game->getNationalTeams().getTeam(nation)->coach,
            controller->getManagerProfile()->name());
  EXPECT_TRUE(hasMessage(controller->getInbox(), "INBOX_NT_APPOINTED_TITLE"));
  EXPECT_EQ(controller->applyForNationalJob(ranking.front()),
            NationalApplyResult::AlreadyInCharge);

  // The next window: the squad is announced a week before and Continue
  // stops there for the head coach.
  bool announced = false;
  for (int day = 0; day < 120 && !announced; ++day)
  {
    controller->advanceDay();
    announced = game->getLastNationalEvents().squad_to_pick;
  }
  ASSERT_TRUE(announced);
  EXPECT_TRUE(hasMessage(controller->getInbox(), "INBOX_NT_CALLUP_TITLE"));
  GameController::CallUpView view = controller->getCallUpView();
  ASSERT_TRUE(view.announced);
  EXPECT_FALSE(view.locked);
  const auto selected =
      std::ranges::count_if(view.candidates, [](const auto& candidate)
                            { return candidate.selected; });
  EXPECT_GE(static_cast<std::size_t>(selected), International::MIN_CALL_UPS);
  EXPECT_LE(static_cast<std::size_t>(selected), view.limit);

  // Call-up validity.
  std::vector<PlayerID> keepers;
  std::vector<PlayerID> outfield;
  for (const auto& candidate : view.candidates)
  {
    if (!candidate.available) continue;
    (candidate.role == PlayerRole::GK ? keepers : outfield)
        .push_back(candidate.id);
  }
  ASSERT_GE(keepers.size(), 2u);
  ASSERT_GE(outfield.size(), view.limit);
  std::vector<PlayerID> squad = {keepers[0], keepers[1]};
  squad.insert(squad.end(), outfield.begin(), outfield.begin() + 18);
  std::vector<PlayerID> too_many = squad;
  too_many.insert(too_many.end(), outfield.begin() + 18,
                  outfield.begin() + static_cast<std::ptrdiff_t>(view.limit));
  EXPECT_EQ(controller->setNationalSquad(too_many),
            International::CallUpResult::TooMany);
  EXPECT_EQ(controller->setNationalSquad({keepers[0], keepers[1], outfield[0]}),
            International::CallUpResult::TooFew);
  std::vector<PlayerID> one_keeper(outfield.begin(), outfield.begin() + 19);
  one_keeper.push_back(keepers[0]);
  EXPECT_EQ(controller->setNationalSquad(one_keeper),
            International::CallUpResult::NeedGoalkeepers);
  std::vector<PlayerID> duplicate = squad;
  duplicate.back() = duplicate.front();
  EXPECT_EQ(controller->setNationalSquad(duplicate),
            International::CallUpResult::Duplicate);
  PlayerID foreigner = 0;
  for (const auto& [id, player] : controller->getGameData()->getPlayers())
    if (player.getNationality() != nation && player.getAge() >= 20)
    {
      foreigner = id;
      break;
    }
  std::vector<PlayerID> wrong = squad;
  wrong.back() = foreigner;
  EXPECT_EQ(controller->setNationalSquad(wrong),
            International::CallUpResult::Ineligible);
  ASSERT_EQ(controller->setNationalSquad(squad),
            International::CallUpResult::Ok);
  view = controller->getCallUpView();
  EXPECT_EQ(static_cast<std::size_t>(std::ranges::count_if(
                view.candidates, [](const auto& c) { return c.selected; })),
            squad.size());

  // Once the players report the squad is locked; the matches are played
  // with it and move the reputation.
  const float reputation = controller->getManagerProfile()->reputation;
  while (controller->getCurrentDate() < view.start) controller->advanceDay();
  EXPECT_EQ(controller->setNationalSquad(squad),
            International::CallUpResult::Locked);
  EXPECT_TRUE(controller->getCallUpView().locked);
  while (!(view.until < controller->getCurrentDate())) controller->advanceDay();
  const NationalJob* job = controller->getNationalJob();
  ASSERT_NE(job, nullptr);
  EXPECT_GE(job->played, 1);
  EXPECT_EQ(job->played, job->won + job->drawn + job->lost);
  EXPECT_TRUE(hasMessage(controller->getInbox(), "INBOX_NT_RESULT_TITLE"));
  EXPECT_NE(controller->getManagerProfile()->reputation, reputation);
  EXPECT_FALSE(controller->getNationalJobHistory().size() > 0);

  // Resigning hands the team to an interim coach.
  ASSERT_TRUE(controller->resignNationalJob());
  EXPECT_FALSE(controller->hasNationalJob());
  EXPECT_FALSE(game->getCareer().hasInternationalDuty());
  ASSERT_EQ(controller->getNationalJobHistory().size(), 1u);
  EXPECT_EQ(controller->getNationalJobHistory().front().reason,
            DepartureReason::Resigned);
  EXPECT_NE(game->getNationalTeams().getTeam(nation)->coach,
            controller->getManagerProfile()->name());
  EXPECT_EQ(controller->applyForNationalJob(nation),
            NationalApplyResult::RecentlyLeft);
}

TEST(NationalJobTest, ClubManagersNeedAContinentalNameForBothJobs)
{
  const SlotCleanup slot{uniqueSlot(5)};
  auto controller = makeNationsWorld(slot.slot);
  Game* game = controller->getGame();
  const std::vector<Language> small = smallNations(*controller);
  ASSERT_FALSE(small.empty());
  createInternationalManager(*controller, small.front());
  controller->selectManagedTeam(topClub(*controller, 3));
  ASSERT_FALSE(controller->isUnemployed());
  ASSERT_LT(controller->getManagerProfile()->reputation,
            NationalJobModel::DUAL_ROLE_REPUTATION);
  EXPECT_FALSE(controller->canCombineClubAndNation());
  game->getNationalJob().openVacancy(small.front(),
                                     controller->getCurrentDate());
  EXPECT_EQ(controller->applyForNationalJob(small.front()),
            NationalApplyResult::ClubConflict);

  // A famous manager may do both; taking a club later keeps the nation.
  game->getCareer().adjustReputation(20.0f);
  ASSERT_TRUE(controller->canCombineClubAndNation());
  const std::uint32_t offer_id = obtainNationalOffer(*controller);
  ASSERT_NE(offer_id, 0u);
  ASSERT_EQ(controller->acceptNationalJobOffer(offer_id),
            NationalApplyResult::Ok);
  EXPECT_TRUE(controller->hasNationalJob());
  EXPECT_FALSE(controller->isUnemployed());

  // Without the name, a new club ends the national job.
  game->getCareer().adjustReputation(-30.0f);
  ASSERT_TRUE(controller->resignFromClub());
  EXPECT_TRUE(controller->hasNationalJob())
      << "leaving the club keeps the nation";
  ManagerContract contract;
  contract.weekly_wage = 5'000;
  contract.start = controller->getCurrentDate();
  contract.expires = GameDateValue(2027, 6, 30);
  game->takeJob(topClub(*controller, 2), contract);
  EXPECT_FALSE(controller->hasNationalJob());
  ASSERT_EQ(controller->getNationalJobHistory().size(), 1u);
  EXPECT_EQ(controller->getNationalJobHistory().front().reason,
            DepartureReason::Moved);
}

TEST(NationalJobPersistenceTest, JobMarketAndCallUpsSurviveSaveAndLoad)
{
  const SlotCleanup slot{uniqueSlot(6)};
  auto controller = makeNationsWorld(slot.slot);
  Game* game = controller->getGame();
  const std::vector<Language> ranking = game->getNationalTeams().ranking();
  const std::vector<Language> small = smallNations(*controller);
  ASSERT_FALSE(small.empty());
  createInternationalManager(*controller, small.front());
  const std::uint32_t offer_id = obtainNationalOffer(*controller);
  ASSERT_NE(offer_id, 0u);
  ASSERT_EQ(controller->acceptNationalJobOffer(offer_id),
            NationalApplyResult::Ok);
  const NationalJob job = *controller->getNationalJob();
  game->getNationalJob().openVacancy(ranking.front(),
                                     controller->getCurrentDate());
  const std::size_t vacancies = game->getNationalJob().getVacancies().size();
  ASSERT_GE(vacancies, 1u);
  ASSERT_TRUE(controller->saveGame());

  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(slot.slot));
  game = controller->getGame();
  ASSERT_TRUE(controller->hasNationalJob());
  const NationalJob* loaded = controller->getNationalJob();
  EXPECT_EQ(loaded->nation, job.nation);
  EXPECT_EQ(loaded->start, job.start);
  EXPECT_EQ(loaded->expires, job.expires);
  EXPECT_EQ(loaded->weekly_wage, job.weekly_wage);
  EXPECT_EQ(game->getNationalJob().getVacancies().size(), vacancies);
  EXPECT_TRUE(game->getCareer().hasInternationalDuty())
      << "the duty flag follows the loaded job";
  EXPECT_EQ(game->getNationalTeams().getTeam(job.nation)->coach,
            controller->getManagerProfile()->name());

  // Older saves have no national job at all and keep working.
  sqlite3_exec(controller->getDbConn()->getRaw(),
               "DELETE FROM NationalJobState; DELETE FROM NationalVacancies;",
               nullptr, nullptr, nullptr);
  NationalManagement fresh(controller->getGameData());
  fresh.load(controller->getDbConn());
  EXPECT_FALSE(fresh.hasJob());
  EXPECT_TRUE(fresh.getVacancies().empty());
}

// ---------------------------------------------------------------------------
// Cost
// ---------------------------------------------------------------------------

TEST(ReservePerformanceTest, BusyDayWithU21MatchesStaysCheap)
{
  const SlotCleanup slot{uniqueSlot(7)};
  auto controller = makeWorld(slot.slot);
  const TeamID managed = topClub(*controller, 1);
  controller->selectManagedTeam(managed);
  // Into the season: the U18 and U21 leagues are running.
  while (controller->getCurrentDate() < GameDateValue(2025, 9, 20))
    controller->advanceDay();
  YouthAcademy& academy = controller->getGame()->getWorld().getYouth();
  const auto timeDay = [&]
  {
    const auto started = std::chrono::steady_clock::now();
    controller->advanceDay();
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - started)
        .count();
  };
  // A week of days: the U21 matchday, the U18 matchday and the busiest
  // league day among them.
  double total = 0.0;
  double busiest = 0.0;
  double reserve_day = 0.0;
  for (int day = 0; day < 7; ++day)
  {
    const std::uint16_t before =
        academy.club(managed) ? academy.club(managed)->reserve_table.played : 0;
    const double ms = timeDay();
    total += ms;
    busiest = std::max(busiest, ms);
    if (academy.club(managed) &&
        academy.club(managed)->reserve_table.played != before)
      reserve_day = ms;
  }
  // The U21 round alone, for every country.
  YouthAcademy alone(controller->getGameData());
  alone.ensureReady();
  Inbox inbox;
  GameDateValue date = controller->getCurrentDate();
  // The first day sets the computer clubs' U21 squads up once: untimed.
  date = SeasonCalendar::addDays(date, 1);
  alone.onDayAdvanced(date, managed, inbox);
  double round_ms = 0.0;
  for (int day = 0; day < 7; ++day)
  {
    date = SeasonCalendar::addDays(date, 1);
    const auto started = std::chrono::steady_clock::now();
    alone.onDayAdvanced(date, managed, inbox);
    round_ms =
        std::max(round_ms, std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - started)
                               .count());
  }
  std::cout << "[u21] week of days: " << total << " ms total, busiest day "
            << busiest << " ms, U21 matchday " << reserve_day
            << " ms; slowest academy day alone (U18 or U21 round, all "
               "countries) "
            << round_ms << " ms\n";
  EXPECT_GT(reserve_day, 0.0) << "a U21 matchday was played during the week";
  EXPECT_LT(round_ms, 60.0);
}
