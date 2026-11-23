// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <tuple>
#include <vector>

#include "controller/game_controller.h"
#include "global/logger.h"
#include "model/holiday.h"
#include "model/match.h"
#include "model/season_agenda.h"
#include "model/transfer_negotiation.h"
#include "model/transfer_windows.h"

namespace
{
// League ids of the built-in world (assets/world/default.fmdb).
constexpr LeagueID ITALY = 1;
constexpr LeagueID SPAIN = 2;
constexpr LeagueID ENGLAND = 3;
constexpr LeagueID GERMANY = 4;
constexpr LeagueID FRANCE = 5;
constexpr LeagueID ITALY_SECOND = 6;
constexpr LeagueID USA = 7;
constexpr LeagueID RUSSIA = 8;
constexpr LeagueID MEXICO = 9;
constexpr LeagueID ARGENTINA = 10;
constexpr LeagueID BRAZIL = 11;
constexpr LeagueID PORTUGAL = 12;
constexpr LeagueID ENGLAND_SECOND = 14;

GameDateValue day(int year, int month, int date)
{
  return GameDateValue(static_cast<std::uint16_t>(year),
                       static_cast<std::uint8_t>(month),
                       static_cast<std::uint8_t>(date));
}

/** Open on the first and last day of a window, shut the days around it. */
void expectWindow(LeagueID league, GameDateValue opens, GameDateValue closes)
{
  EXPECT_FALSE(TransferWindows::isOpen(league, opens - 1))
      << int{league} << " " << opens.toString();
  EXPECT_TRUE(TransferWindows::isOpen(league, opens))
      << int{league} << " " << opens.toString();
  EXPECT_TRUE(TransferWindows::isOpen(league, closes))
      << int{league} << " " << closes.toString();
  EXPECT_FALSE(TransferWindows::isOpen(league, closes + 1))
      << int{league} << " " << closes.toString();
  EXPECT_TRUE(TransferWindows::isDeadlineDay(league, closes));
  EXPECT_FALSE(TransferWindows::isDeadlineDay(league, closes - 1));
  EXPECT_EQ(TransferWindows::windowEnd(league, opens), closes);
  EXPECT_EQ(TransferWindows::nextOpening(league, opens - 1), opens);
}

std::size_t count(const std::vector<AgendaEvent>& agenda, AgendaKind kind,
                  std::vector<GameDateValue>* dates = nullptr)
{
  std::size_t found = 0;
  for (const AgendaEvent& event : agenda)
  {
    if (event.kind != kind) continue;
    ++found;
    if (dates) dates->push_back(event.date);
  }
  return found;
}
}  // namespace

TEST(TransferWindows, EveryCountryOpensAndShutsOnItsOwnDays)
{
  expectWindow(ENGLAND, day(2025, 6, 15), day(2025, 9, 1));
  expectWindow(ENGLAND, day(2026, 1, 1), day(2026, 2, 2));
  expectWindow(ITALY, day(2025, 6, 29), day(2025, 9, 1));
  expectWindow(ITALY, day(2026, 1, 2), day(2026, 2, 2));
  expectWindow(SPAIN, day(2025, 7, 1), day(2025, 9, 1));
  expectWindow(GERMANY, day(2025, 7, 1), day(2025, 8, 31));
  expectWindow(FRANCE, day(2025, 6, 15), day(2025, 9, 1));
  expectWindow(PORTUGAL, day(2025, 7, 1), day(2025, 9, 4));
  expectWindow(BRAZIL, day(2025, 7, 20), day(2025, 9, 11));
  expectWindow(BRAZIL, day(2026, 1, 5), day(2026, 3, 3));
  expectWindow(ARGENTINA, day(2025, 7, 9), day(2025, 7, 31));
  expectWindow(ARGENTINA, day(2026, 1, 2), day(2026, 1, 27));
  expectWindow(USA, day(2025, 7, 13), day(2025, 9, 2));
  expectWindow(USA, day(2026, 1, 26), day(2026, 3, 26));
  expectWindow(MEXICO, day(2025, 7, 2), day(2025, 9, 11));
  expectWindow(MEXICO, day(2026, 1, 1), day(2026, 2, 9));
  expectWindow(RUSSIA, day(2025, 6, 19), day(2025, 9, 11));
  expectWindow(RUSSIA, day(2026, 1, 23), day(2026, 2, 19));

  // On 2 September only the late countries still sign players.
  const GameDateValue late = day(2025, 9, 2);
  for (const LeagueID shut :
       {ENGLAND, ITALY, SPAIN, GERMANY, FRANCE, ARGENTINA})
    EXPECT_FALSE(TransferWindows::isOpen(shut, late)) << int{shut};
  for (const LeagueID open : {PORTUGAL, BRAZIL, USA, MEXICO, RUSSIA})
    EXPECT_TRUE(TransferWindows::isOpen(open, late)) << int{open};
  EXPECT_TRUE(TransferWindows::isOpenAnywhere(late));
  EXPECT_FALSE(TransferWindows::isOpenAnywhere(day(2025, 11, 15)));
}

TEST(TransferWindows, SecondDivisionsAndUnknownLeagues)
{
  for (const GameDateValue date :
       {day(2025, 6, 29), day(2025, 9, 1), day(2025, 9, 2), day(2026, 1, 2)})
  {
    EXPECT_EQ(TransferWindows::isOpen(ITALY_SECOND, date),
              TransferWindows::isOpen(ITALY, date));
    EXPECT_EQ(TransferWindows::isOpen(ENGLAND_SECOND, date),
              TransferWindows::isOpen(ENGLAND, date));
  }
  // A league added by a data pack follows the common European calendar.
  constexpr LeagueID UNKNOWN = 99;
  expectWindow(UNKNOWN, day(2025, 7, 1), day(2025, 9, 1));
  expectWindow(UNKNOWN, day(2026, 1, 1), day(2026, 2, 2));
  // Every country of the table is distinct and lists both divisions.
  for (const TransferWindows::CountryRules& rules :
       TransferWindows::countries())
    for (const LeagueID league : rules.leagues)
      EXPECT_EQ(&TransferWindows::rulesFor(league), &rules) << int{league};
}

TEST(TransferWindows, WindowInfoCountsToTheCountrysDeadline)
{
  using TransferNegotiation::windowInfo;
  const GameDateValue date = day(2025, 8, 28);
  EXPECT_EQ(windowInfo(GERMANY, date).days_to_deadline, 3);
  EXPECT_EQ(windowInfo(ENGLAND, date).days_to_deadline, 4);
  EXPECT_EQ(windowInfo(PORTUGAL, date).days_to_deadline, 7);
  EXPECT_EQ(windowInfo(BRAZIL, date).days_to_deadline, 14);
  EXPECT_FALSE(windowInfo(ARGENTINA, date).open);
  EXPECT_EQ(windowInfo(ARGENTINA, date).days_to_deadline, -1);
  // Deadline day weighs most, in each country on its own day.
  EXPECT_GT(TransferNegotiation::activityWeight(
                windowInfo(PORTUGAL, day(2025, 9, 4))),
            TransferNegotiation::activityWeight(windowInfo(PORTUGAL, date)));
  // The start-of-year window is the "winter" one everywhere.
  EXPECT_TRUE(windowInfo(USA, day(2026, 3, 1)).winter);
  EXPECT_TRUE(windowInfo(BRAZIL, day(2026, 2, 20)).winter);
  EXPECT_FALSE(windowInfo(BRAZIL, day(2025, 8, 20)).winter);
}

TEST(TransferWindows, FreeAgentsFollowTheCountrysRule)
{
  // Most countries register players without a club on any day.
  for (const LeagueID league : {ENGLAND, ITALY, SPAIN, BRAZIL, USA})
    EXPECT_TRUE(TransferWindows::canSignFreeAgent(league, day(2025, 11, 20)))
        << int{league};
  // Russia: 14 more days after each close, then not until the next window.
  EXPECT_TRUE(TransferWindows::canSignFreeAgent(RUSSIA, day(2025, 9, 25)));
  EXPECT_FALSE(TransferWindows::canSignFreeAgent(RUSSIA, day(2025, 9, 26)));
  EXPECT_FALSE(TransferWindows::canSignFreeAgent(RUSSIA, day(2026, 1, 10)));
  EXPECT_TRUE(TransferWindows::canSignFreeAgent(RUSSIA, day(2026, 3, 5)));
  EXPECT_FALSE(TransferWindows::canSignFreeAgent(RUSSIA, day(2026, 3, 6)));
  // Mexico: until early March after the January window.
  EXPECT_TRUE(TransferWindows::canSignFreeAgent(MEXICO, day(2026, 3, 6)));
  EXPECT_FALSE(TransferWindows::canSignFreeAgent(MEXICO, day(2026, 3, 7)));
  EXPECT_FALSE(TransferWindows::canSignFreeAgent(MEXICO, day(2026, 5, 1)));
}

TEST(TransferWindows, AgendaAndHolidayUseTheClubsOwnDeadline)
{
  const std::vector<Match> none;
  for (const auto& [league, summer, winter] :
       {std::tuple{GERMANY, day(2025, 8, 31), day(2026, 2, 2)},
        std::tuple{PORTUGAL, day(2025, 9, 4), day(2026, 2, 2)},
        std::tuple{BRAZIL, day(2025, 9, 11), day(2026, 3, 3)},
        std::tuple{ARGENTINA, day(2025, 7, 31), day(2026, 1, 27)}})
  {
    std::vector<GameDateValue> deadlines;
    EXPECT_EQ(count(SeasonAgenda::build(2025, none, false, league),
                    AgendaKind::TransferDeadline, &deadlines),
              2u)
        << int{league};
    ASSERT_EQ(deadlines.size(), 2u);
    EXPECT_EQ(deadlines[0], summer) << int{league};
    EXPECT_EQ(deadlines[1], winter) << int{league};
    // Holiday "until the window shuts" ends the day after that deadline.
    EXPECT_EQ(Holiday::windowEndDate(league, day(2025, 7, 25)), summer + 1);
    EXPECT_EQ(Holiday::windowEndDate(league, day(2025, 10, 1)), winter + 1);
  }
}

TEST(TransferWindows, OnlyClubsWhoseWindowIsOpenCanBuy)
{
  Logger::init();
  GameController controller;
  controller.newGame(0, 20250702);
  // Early July: Italy and Spain are open, Brazil opens on 20 July.
  const auto italians = controller.getTeamsInLeague(ITALY);
  const auto spaniards = controller.getTeamsInLeague(SPAIN);
  const auto brazilians = controller.getTeamsInLeague(BRAZIL);
  ASSERT_GE(italians.size(), 2u);
  ASSERT_FALSE(spaniards.empty());
  ASSERT_FALSE(brazilians.empty());
  controller.selectManagedTeam(italians.front().get().getId());
  // A new career starts early in July, before Brazil's window opens.
  ASSERT_TRUE(controller.getCurrentDate() < day(2025, 7, 20));
  EXPECT_TRUE(controller.isTransferWindowOpen());
  EXPECT_TRUE(controller.getTransferWindow().open);
  const TeamID brazil_club = brazilians.front().get().getId();
  const TeamID spain_club = spaniards.front().get().getId();
  EXPECT_FALSE(controller.isTransferWindowOpenFor(brazil_club));
  EXPECT_FALSE(controller.getTransferWindowFor(brazil_club).open);
  EXPECT_TRUE(controller.isTransferWindowOpenFor(spain_club));
  // Brazilian clubs still sign free agents, any day.
  EXPECT_TRUE(controller.canSignFreeAgentFor(brazil_club));

  // A player listed by another Italian club: the Brazilian bid is refused
  // because Brazil's window is shut, the Spanish one goes through.
  const TeamID seller = italians[1].get().getId();
  const auto& squad = controller.getPlayersForTeam(seller);
  ASSERT_FALSE(squad.empty());
  const auto cheapest = std::ranges::min_element(
      squad, {}, [](const auto& player) { return player.get().getWage(); });
  const PlayerID listed = cheapest->get().getId();
  controller.listPlayerForTransfer(listed, 1'000'000);
  ASSERT_TRUE(controller.getAllListings().contains(listed));
  std::uint32_t bid = 1'000;
  for (const auto& club : brazilians)
    EXPECT_FALSE(controller.submitBid(listed, club.get().getId(), ++bid));
  bool spanish_bid = false;
  for (const auto& club : spaniards)
    spanish_bid =
        controller.submitBid(listed, club.get().getId(), ++bid) || spanish_bid;
  EXPECT_TRUE(spanish_bid);
}
