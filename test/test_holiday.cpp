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
#include <format>
#include <memory>

#include "controller/game_controller.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/holiday.h"
#include "model/transfer_negotiation.h"
#include "model/world_tuning.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 6'000'000 + getpid() % 100'000 * 10 + offset;
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

HolidayPlan plan(HolidayMode mode)
{
  HolidayPlan result;
  result.mode = mode;
  return result;
}
/** Clubs of the Italian league follow Italy's transfer windows. */
constexpr LeagueID ITALY = 1;
}  // namespace

TEST(Holiday, TargetsPerMode)
{
  const GameDateValue today(2025, 7, 2);
  HolidayPlan until = plan(HolidayMode::UntilDate);
  until.until = GameDateValue(2025, 8, 20);
  EXPECT_EQ(Holiday::targetDate(until, today, std::nullopt, ITALY),
            until.until);
  until.until = today;
  EXPECT_FALSE(
      Holiday::targetDate(until, today, std::nullopt, ITALY).has_value());
  const GameDateValue match(2025, 7, 12);
  EXPECT_EQ(
      Holiday::targetDate(plan(HolidayMode::NextMatch), today, match, ITALY),
      match);
  EXPECT_FALSE(Holiday::targetDate(plan(HolidayMode::NextMatch), today,
                                   std::nullopt, ITALY));
  EXPECT_FALSE(Holiday::targetDate(plan(HolidayMode::NextDecision), today,
                                   match, ITALY));
  // The day after the summer deadline, in the open window...
  const GameDateValue end = Holiday::windowEndDate(ITALY, today);
  const auto window = TransferNegotiation::windowInfo(ITALY, today);
  ASSERT_TRUE(window.open);
  EXPECT_EQ(end, SeasonCalendar::addDays(today, window.days_to_deadline + 1));
  EXPECT_FALSE(TransferNegotiation::windowInfo(ITALY, end).open);
  // ... and from a closed window, the next one's.
  const GameDateValue winter = Holiday::windowEndDate(ITALY, end);
  EXPECT_TRUE(end < winter);
  EXPECT_TRUE(TransferNegotiation::windowInfo(
                  ITALY, SeasonCalendar::addDays(winter, -1))
                  .open);
}

TEST(Holiday, StopRulesInOrderOfSeriousness)
{
  HolidayPlan until = plan(HolidayMode::UntilDate);
  HolidayDay quiet;
  EXPECT_FALSE(Holiday::checkStop(until, quiet).has_value());

  HolidayDay bid;
  bid.new_offers.push_back({1, 30'000'000, false, true});
  EXPECT_EQ(Holiday::checkStop(until, bid), HolidayStop::BigBid);
  bid.new_offers[0].key_player = false;
  EXPECT_FALSE(Holiday::checkStop(until, bid).has_value());
  until.preferences.big_bid_threshold = 20'000'000;
  EXPECT_EQ(Holiday::checkStop(until, bid), HolidayStop::BigBid);
  bid.new_offers[0].loan = true;
  EXPECT_FALSE(Holiday::checkStop(until, bid).has_value());
  until.preferences.stop_big_bid = false;
  bid.new_offers[0].loan = false;
  EXPECT_FALSE(Holiday::checkStop(until, bid).has_value());

  HolidayDay injury;
  injury.new_injuries.emplace_back(2, "A", "INJURY", 10, true);
  EXPECT_FALSE(Holiday::checkStop(until, injury).has_value());
  injury.new_injuries[0].days = Holiday::KEY_INJURY_DAYS;
  EXPECT_EQ(Holiday::checkStop(until, injury), HolidayStop::KeyPlayerInjured);

  HolidayDay crisis;
  crisis.injured = 5;
  crisis.injured_at_start = 5;  // already a crisis when leaving
  EXPECT_FALSE(Holiday::checkStop(until, crisis).has_value());
  crisis.injured = 6;
  EXPECT_EQ(Holiday::checkStop(until, crisis), HolidayStop::InjuryCrisis);
  crisis.injured_at_start = 1;
  crisis.injured = 4;
  EXPECT_FALSE(Holiday::checkStop(until, crisis).has_value());

  HolidayDay decision;
  decision.new_decision = true;
  EXPECT_FALSE(Holiday::checkStop(until, decision).has_value());
  EXPECT_EQ(Holiday::checkStop(plan(HolidayMode::NextDecision), decision),
            HolidayStop::Decision);

  HolidayDay worst = crisis;
  worst.injured = 9;
  worst.board_warning = true;
  EXPECT_EQ(Holiday::checkStop(until, worst), HolidayStop::SackingWarning);
  worst.dismissed = true;
  until.preferences.stop_sacking_warning = false;
  EXPECT_EQ(Holiday::checkStop(until, worst), HolidayStop::Dismissed);
}

TEST(Holiday, NextMatchStopsOnTheFixtureDay)
{
  const SlotCleanup slot{uniqueSlot(0)};
  auto controller = makeCareer(slot.slot);
  HolidayPlan next = plan(HolidayMode::NextMatch);
  // This checks the fixture target; early-stop preferences have their own
  // tests and may fire when the assistant plays today's match.
  next.preferences.stop_big_bid = false;
  next.preferences.stop_sacking_warning = false;
  next.preferences.stop_injury_crisis = false;
  next.preferences.stop_key_injury = false;
  const auto target = controller->getHolidayTarget(next);
  ASSERT_TRUE(target.has_value());
  const int days = controller->goOnHoliday(next);
  EXPECT_EQ(controller->getCurrentDate(), *target);
  const HolidaySummary& summary = controller->getHolidaySummary();
  ASSERT_TRUE(summary.valid);
  EXPECT_EQ(summary.days, days);
  EXPECT_EQ(summary.reason, HolidayStop::Completed);
  EXPECT_TRUE(summary.results.empty());
  // Leaving on a match day hands that match to the assistant.
  const auto following = controller->getHolidayTarget(next);
  ASSERT_TRUE(following.has_value());
  EXPECT_TRUE(*target < *following);
  EXPECT_GT(controller->goOnHoliday(next), 0);
  EXPECT_EQ(controller->getCurrentDate(), *following);
  ASSERT_EQ(controller->getHolidaySummary().results.size(), 1u);
  EXPECT_EQ(controller->getHolidaySummary().results[0].date, *target);
}

TEST(Holiday, AssistantRunsTheClubUntilADate)
{
  const SlotCleanup slot{uniqueSlot(1)};
  auto controller = makeCareer(slot.slot);
  controller->setAssistantFixesLineup(false);
  HolidayPlan until = plan(HolidayMode::UntilDate);
  until.until = GameDateValue(2025, 8, 19);
  until.preferences.stop_big_bid = false;
  until.preferences.stop_injury_crisis = false;
  until.preferences.stop_key_injury = false;
  until.preferences.stop_sacking_warning = false;
  const int days = controller->goOnHoliday(until);
  const HolidaySummary& summary = controller->getHolidaySummary();
  ASSERT_TRUE(summary.valid);
  EXPECT_EQ(summary.reason, HolidayStop::Completed);
  EXPECT_EQ(controller->getCurrentDate(), until.until);
  EXPECT_EQ(summary.days, days);
  // Friendlies and the first league rounds were played by the assistant.
  EXPECT_GE(summary.results.size(), 5u);
  EXPECT_TRUE(
      std::ranges::any_of(summary.results, [](const HolidayResult& result)
                          { return result.type == MatchType::LEAGUE; }));
  EXPECT_GT(summary.position_after, 0);
  EXPECT_GT(summary.points_after + summary.results.size(), 0u);
  EXPECT_GT(summary.income, 0);
  EXPECT_GT(summary.expenses, 0);
  EXPECT_GT(summary.messages_filed, 0);
  // The manager's own settings are back.
  EXPECT_FALSE(controller->getAssistantFixesLineup());
  EXPECT_EQ(controller->getHolidayPreferences().stop_big_bid, false);

  // Preferences travel with the save.
  ASSERT_TRUE(controller->saveGame());
  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  EXPECT_FALSE(reloaded.getHolidayPreferences().stop_big_bid);
  EXPECT_FALSE(reloaded.getHolidayPreferences().stop_injury_crisis);
}

TEST(Holiday, DayLimitAndOpenEndedHolidays)
{
  const SlotCleanup slot{uniqueSlot(2)};
  auto controller = makeCareer(slot.slot);
  HolidayPlan until = plan(HolidayMode::UntilDate);
  until.until = GameDateValue(2025, 12, 1);
  until.max_days = 5;
  EXPECT_EQ(controller->goOnHoliday(until), 5);
  EXPECT_EQ(controller->getHolidaySummary().reason, HolidayStop::DayLimit);

  const int days = controller->goOnHoliday(plan(HolidayMode::NextDecision));
  const HolidaySummary& summary = controller->getHolidaySummary();
  EXPECT_LE(days, 90);
  EXPECT_GT(days, 0);
  // Open-ended: it ends on a decision, another stop or the day limit.
  EXPECT_NE(summary.reason, HolidayStop::Completed);
}

TEST(Holiday, SackingEndsTheHoliday)
{
  const SlotCleanup slot{uniqueSlot(3)};
  auto controller = makeCareer(slot.slot);
  HolidayPlan until = plan(HolidayMode::UntilDate);
  until.preferences.stop_big_bid = false;
  until.preferences.stop_injury_crisis = false;
  until.preferences.stop_key_injury = false;
  until.preferences.stop_sacking_warning = false;
  until.until = GameDateValue(2025, 9, 30);
  controller->goOnHoliday(until);
  ASSERT_EQ(controller->getCurrentDate(), until.until);
  ASSERT_FALSE(controller->isUnemployed());
  ASSERT_TRUE(controller->saveGame());

  // The board has run out of patience: the October review dismisses him.
  using Tuning = WorldTuning::Board;
  sqlite3* db = nullptr;
  ASSERT_EQ(
      sqlite3_open(RuntimePaths::savePath(slot.slot).string().c_str(), &db),
      SQLITE_OK);
  const std::string update = std::format(
      "UPDATE BoardState SET confidence = 0, low_reviews = {}, league_matches "
      "= {};",
      Tuning::DISMISSAL_REVIEWS - 1, Tuning::MIN_MATCHES_FOR_DISMISSAL);
  ASSERT_EQ(sqlite3_exec(db, update.c_str(), nullptr, nullptr, nullptr),
            SQLITE_OK);
  sqlite3_close(db);
  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(slot.slot));
  ASSERT_FALSE(controller->isUnemployed());

  until.until = GameDateValue(2025, 10, 20);
  EXPECT_EQ(controller->goOnHoliday(until), 1);
  const HolidaySummary& summary = controller->getHolidaySummary();
  EXPECT_EQ(summary.reason, HolidayStop::Dismissed);
  EXPECT_EQ(controller->getCurrentDate(), GameDateValue(2025, 10, 1));
  EXPECT_TRUE(controller->isUnemployed());
  ASSERT_FALSE(controller->getManagerStints().empty());
  EXPECT_EQ(controller->getManagerStints().back().reason,
            DepartureReason::Sacked);
}

TEST(Holiday, AutosavesKeepTheManagersOwnDuties)
{
  const SlotCleanup slot{uniqueSlot(4)};
  auto controller = makeCareer(slot.slot);
  controller->setAssistantFixesLineup(false);
  controller->setAutosavePolicy({AutosaveFrequency::Daily, 0});
  HolidayPlan until = plan(HolidayMode::UntilDate);
  until.until = SeasonCalendar::addDays(controller->getCurrentDate(), 3);
  until.preferences.assistant_lineup = true;
  until.preferences.stop_big_bid = false;
  until.preferences.stop_injury_crisis = false;
  until.preferences.stop_key_injury = false;
  until.preferences.stop_sacking_warning = false;
  EXPECT_EQ(controller->goOnHoliday(until), 3);
  EXPECT_FALSE(controller->getAssistantFixesLineup());
  // The last holiday day was autosaved while the assistant stood in.
  EXPECT_EQ(controller->getSaveStatus().game_date,
            controller->getCurrentDate().toString());
  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  EXPECT_EQ(reloaded.getCurrentDate(), controller->getCurrentDate());
  EXPECT_FALSE(reloaded.getAssistantFixesLineup());
}
