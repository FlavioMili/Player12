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
#include <memory>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/match_report.h"
#include "model/records.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 3'000'000 + getpid() % 100'000 * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

std::unique_ptr<GameController> makeCareer(int slot)
{
  Logger::init();
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot, WORLD_SEED);
  controller->selectManagedTeam(controller->getTeams().front().get().getId());
  return controller;
}

MatchReport match(const GameDateValue& date, TeamID home, TeamID away,
                  LeagueID league, std::uint8_t home_goals,
                  std::uint8_t away_goals, std::uint32_t attendance,
                  MatchType type = MatchType::LEAGUE)
{
  MatchReport report;
  report.date = date;
  report.home_team_id = home;
  report.away_team_id = away;
  report.match_type = type;
  report.competition_id = type == MatchType::LEAGUE ? league : 0;
  report.home_goals = home_goals;
  report.away_goals = away_goals;
  report.attendance = attendance;
  return report;
}

const RecordEntry* find(const std::vector<RecordEntry>& records,
                        RecordKind kind)
{
  const auto it = std::ranges::find(records, kind, &RecordEntry::kind);
  return it == records.end() ? nullptr : std::to_address(it);
}
}  // namespace

TEST(Records, TiesKeepTheEarlierHolder)
{
  RecordEntry holder;
  holder.kind = RecordKind::BiggestWin;
  holder.value = 4;
  holder.value2 = 4;
  RecordEntry same = holder;
  EXPECT_FALSE(Records::beats(same, holder));
  same.value2 = 5;  // 5-1 beats 4-0 on goals scored
  EXPECT_TRUE(Records::beats(same, holder));
  RecordEntry crowd;
  crowd.kind = RecordKind::HighestAttendance;
  crowd.value = 30'000;
  RecordEntry equal = crowd;
  equal.value2 = 99;
  EXPECT_FALSE(Records::beats(equal, crowd));
}

TEST(Records, LegendThresholds)
{
  ClubPlayerTotal totals;
  totals.appearances = 149;
  EXPECT_FALSE(Records::isLegend(totals, 0));
  totals.appearances = 150;
  EXPECT_TRUE(Records::isLegend(totals, 0));
  totals.appearances = 60;
  EXPECT_FALSE(Records::isLegend(totals, 1));
  EXPECT_TRUE(Records::isLegend(totals, 2));
  totals.appearances = 10;
  totals.goals = 60;
  EXPECT_TRUE(Records::isLegend(totals, 0));
  EXPECT_EQ(Records::legendScore(totals, 1), 10 + 120 + 30);
}

TEST(Records, BookUpdatesFromReportsAndSettlesSeasons)
{
  const SlotCleanup slot{uniqueSlot(0)};
  auto controller = makeCareer(slot.slot);
  const auto gamedata = controller->getGameData();
  const LeagueID league = controller->getManagedTeam()->get().getLeagueId();
  const auto& clubs = controller->getLeagueById(league)->get().getTeamIDs();
  const TeamID a = clubs[0];
  const TeamID b = clubs[1];
  const PlayerID scorer =
      controller->getPlayersForTeam(a).front().get().getId();

  RecordBook book;
  MatchReport opener =
      match(GameDateValue(2025, 8, 16), a, b, league, 3, 0, 21'000);
  opener.players.push_back({scorer, a, true, 90, 2, 0, 0, 0, 7.5f});
  book.onMatchPlayed(*gamedata, opener);
  // Same margin later: the earlier win stays the record.
  book.onMatchPlayed(
      *gamedata, match(GameDateValue(2025, 8, 23), b, a, league, 0, 3, 19'000));
  book.onMatchPlayed(
      *gamedata, match(GameDateValue(2025, 9, 2), a, b, league, 2, 2, 25'000));
  // Friendlies never count.
  book.onMatchPlayed(*gamedata, match(GameDateValue(2025, 7, 20), a, b, league,
                                      9, 0, 40'000, MatchType::FRIENDLY));

  const auto records_a = book.clubRecords(a);
  const RecordEntry* win = find(records_a, RecordKind::BiggestWin);
  ASSERT_NE(win, nullptr);
  EXPECT_EQ(win->value, 3);
  EXPECT_EQ(win->date, GameDateValue(2025, 8, 16));
  EXPECT_EQ(find(records_a, RecordKind::HighestScoringMatch)->value, 4);
  EXPECT_EQ(find(records_a, RecordKind::HighestAttendance)->value, 25'000);
  const auto records_b = book.clubRecords(b);
  const RecordEntry* defeat = find(records_b, RecordKind::BiggestDefeat);
  ASSERT_NE(defeat, nullptr);
  EXPECT_EQ(defeat->value, 3);
  EXPECT_EQ(find(book.leagueRecords(league), RecordKind::BiggestWin)->team_id,
            a);

  const auto table = book.allTimeTable(league);
  ASSERT_EQ(table.size(), 2u);
  EXPECT_EQ(table[0].team_id, a);
  EXPECT_EQ(table[0].points, 7u);
  EXPECT_EQ(table[0].won, 2u);
  EXPECT_EQ(table[0].drawn, 1u);
  EXPECT_EQ(table[1].lost, 2u);
  EXPECT_EQ(table[0].goals_for, 8u);

  const auto scorers = book.topScorers(a, 5);
  ASSERT_EQ(scorers.size(), 1u);
  EXPECT_EQ(scorers[0].player_id, scorer);
  EXPECT_EQ(scorers[0].goals, 2u);
  EXPECT_EQ(book.mostAppearances(a, 5).front().appearances, 1u);

  book.onTransfer(*gamedata, GameDateValue(2025, 8, 30), scorer, b, a,
                  12'000'000);
  book.onTransfer(*gamedata, GameDateValue(2025, 8, 31), scorer, b, a,
                  9'000'000);
  EXPECT_EQ(find(book.clubRecords(a), RecordKind::RecordSigning)->value,
            12'000'000);
  EXPECT_EQ(find(book.clubRecords(b), RecordKind::RecordSale)->value,
            12'000'000);

  book.closeSeason(*gamedata, 2025);
  EXPECT_EQ(find(book.clubRecords(a), RecordKind::MostGoalsSeason)->value, 8);
  EXPECT_EQ(find(book.clubRecords(a), RecordKind::MostPointsSeason)->value, 7);
  const auto season_records = book.clubRecords(a);
  const RecordEntry* top = find(season_records, RecordKind::TopScorerSeason);
  ASSERT_NE(top, nullptr);
  EXPECT_EQ(top->player_id, scorer);
  EXPECT_EQ(top->value, 2);
  EXPECT_FALSE(top->name.empty());
}

TEST(Records, CareerBookRoundTripsAndRebuildsOldSaves)
{
  const SlotCleanup slot{uniqueSlot(1)};
  auto controller = makeCareer(slot.slot);
  const TeamID managed = controller->getManagedTeam()->get().getId();
  const LeagueID league = controller->getManagedTeam()->get().getLeagueId();
  while (controller->getCurrentDate() < GameDateValue(2025, 8, 26))
    controller->advanceDay();
  const auto records = controller->getClubRecords(managed);
  ASSERT_FALSE(records.empty());
  EXPECT_NE(
      find(controller->getLeagueRecords(league), RecordKind::HighestAttendance),
      nullptr);
  const auto table = controller->getAllTimeTable(league);
  ASSERT_EQ(table.size(),
            controller->getLeagueById(league)->get().getTeamIDs().size());
  EXPECT_GT(table.front().played, 0u);
  const auto apps = controller->getClubMostAppearances(managed, 3);
  ASSERT_FALSE(apps.empty());

  ASSERT_TRUE(controller->saveGame());
  {
    GameController reloaded;
    ASSERT_TRUE(reloaded.loadGame(slot.slot));
    const auto again = reloaded.getClubRecords(managed);
    ASSERT_EQ(again.size(), records.size());
    for (size_t index = 0; index < records.size(); ++index)
    {
      EXPECT_EQ(again[index].kind, records[index].kind);
      EXPECT_EQ(again[index].value, records[index].value);
      EXPECT_EQ(again[index].date, records[index].date);
    }
    EXPECT_EQ(reloaded.getAllTimeTable(league).front().points,
              table.front().points);
    EXPECT_EQ(reloaded.getClubMostAppearances(managed, 3).front().appearances,
              apps.front().appearances);
  }

  // A save from before the records book: rebuilt from the stored reports.
  sqlite3* db = nullptr;
  ASSERT_EQ(
      sqlite3_open(RuntimePaths::savePath(slot.slot).string().c_str(), &db),
      SQLITE_OK);
  for (const char* table_name :
       {"RecordMeta", "RecordEntries", "ClubPlayerTotals", "AllTimeTable",
        "RecordSeason", "RecordSeasonScorers"})
    sqlite3_exec(db, (std::string("DELETE FROM ") + table_name + ";").c_str(),
                 nullptr, nullptr, nullptr);
  sqlite3_close(db);
  GameController legacy;
  ASSERT_TRUE(legacy.loadGame(slot.slot));
  const auto rebuilt = legacy.getClubRecords(managed);
  ASSERT_EQ(rebuilt.size(), records.size());
  for (size_t index = 0; index < records.size(); ++index)
  {
    EXPECT_EQ(rebuilt[index].kind, records[index].kind);
    EXPECT_EQ(rebuilt[index].value, records[index].value);
  }
  EXPECT_EQ(legacy.getAllTimeTable(league).front().points,
            table.front().points);
}
