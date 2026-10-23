// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Browser-style screen history and the sideways swipe recogniser that
// drives it, without a window.

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

#include "gui/nav_history.h"
#include "gui/swipe_gesture.h"

namespace
{
const auto ANY = [](const NavEntry&) { return true; };

NavEntry section(NavSection value) { return NavEntry::ofSection(value); }

std::optional<NavEntry> some(const NavEntry& entry) { return entry; }

constexpr std::uint64_t MS = 1'000'000;

/**
 * Feeds @p samples wheel samples 8 ms apart starting at @p start and counts
 * the steps they complete in each direction.
 */
struct Steps
{
  int back = 0;
  int forward = 0;
};

Steps swipe(SwipeGesture& gesture, float dx, float dy, int samples,
            std::uint64_t start)
{
  Steps steps;
  for (int index = 0; index < samples; ++index)
  {
    const SwipeGesture::Step step = gesture.feed(
        dx, dy, start + static_cast<std::uint64_t>(index) * 8 * MS);
    steps.back += step == SwipeGesture::Step::BACK ? 1 : 0;
    steps.forward += step == SwipeGesture::Step::FORWARD ? 1 : 0;
  }
  return steps;
}
}  // namespace

TEST(NavHistoryTest, BackAndForwardWalkTheVisits)
{
  NavHistory history;
  EXPECT_FALSE(history.current().has_value());
  EXPECT_FALSE(history.back(ANY).has_value());
  EXPECT_FALSE(history.forward(ANY).has_value());

  history.visit(section(NavSection::HOME));
  history.visit(section(NavSection::SQUAD));
  history.visit(section(NavSection::TRANSFERS));
  EXPECT_EQ(history.size(), 3U);
  EXPECT_EQ(history.current(), some(section(NavSection::TRANSFERS)));

  EXPECT_EQ(history.back(ANY), some(section(NavSection::SQUAD)));
  EXPECT_EQ(history.back(ANY), some(section(NavSection::HOME)));
  EXPECT_FALSE(history.back(ANY).has_value());
  EXPECT_EQ(history.current(), some(section(NavSection::HOME)));

  EXPECT_EQ(history.forward(ANY), some(section(NavSection::SQUAD)));
  EXPECT_EQ(history.forward(ANY), some(section(NavSection::TRANSFERS)));
  EXPECT_FALSE(history.forward(ANY).has_value());
  EXPECT_EQ(history.current(), some(section(NavSection::TRANSFERS)));
}

TEST(NavHistoryTest, PeekingDoesNotMove)
{
  NavHistory history;
  history.visit(section(NavSection::HOME));
  history.visit(section(NavSection::INBOX));
  EXPECT_EQ(history.peekBack(ANY), some(section(NavSection::HOME)));
  EXPECT_FALSE(history.peekForward(ANY).has_value());
  EXPECT_EQ(history.current(), some(section(NavSection::INBOX)));
  history.back(ANY);
  EXPECT_EQ(history.peekForward(ANY), some(section(NavSection::INBOX)));
  EXPECT_FALSE(history.peekBack(ANY).has_value());
  EXPECT_EQ(history.current(), some(section(NavSection::HOME)));
}

TEST(NavHistoryTest, NewVisitAfterBackDropsTheForwardList)
{
  using enum NavSection;
  NavHistory history;
  history.visit(section(HOME));
  history.visit(section(SQUAD));
  history.visit(section(TRANSFERS));
  history.visit(NavEntry::ofPlayer(42));
  history.back(ANY);
  history.back(ANY);
  ASSERT_EQ(history.current(), some(section(SQUAD)));

  history.visit(section(FIXTURES));
  EXPECT_EQ(history.size(), 3U);
  EXPECT_FALSE(history.forward(ANY).has_value());
  EXPECT_EQ(history.back(ANY), some(section(SQUAD)));
  EXPECT_EQ(history.back(ANY), some(section(HOME)));
}

TEST(NavHistoryTest, RevisitingTheCurrentScreenIsNotANewEntry)
{
  using enum NavSection;
  NavHistory history;
  history.visit(section(HOME));
  history.visit(section(HOME));
  EXPECT_EQ(history.size(), 1U);
  history.visit(NavEntry::ofPlayer(7));
  history.visit(NavEntry::ofPlayer(7));
  EXPECT_EQ(history.size(), 2U);
  // Revisiting the current entry after going back keeps the forward list.
  history.back(ANY);
  history.visit(section(NavSection::HOME));
  EXPECT_EQ(history.forward(ANY), some(NavEntry::ofPlayer(7)));
}

TEST(NavHistoryTest, CapacityDropsTheOldestEntries)
{
  NavHistory history;
  EXPECT_EQ(history.capacity(), NavHistory::DEFAULT_CAPACITY);
  EXPECT_EQ(NavHistory::DEFAULT_CAPACITY, 50U);
  for (PlayerID player = 1; player <= 60; ++player)
    history.visit(NavEntry::ofPlayer(player));
  EXPECT_EQ(history.size(), 50U);
  EXPECT_EQ(history.position(), 49U);
  EXPECT_EQ(history.current(), some(NavEntry::ofPlayer(60)));
  std::optional<NavEntry> oldest;
  int steps = 0;
  while (const std::optional<NavEntry> entry = history.back(ANY))
  {
    oldest = entry;
    ++steps;
  }
  EXPECT_EQ(steps, 49);
  EXPECT_EQ(oldest, some(NavEntry::ofPlayer(11)));

  NavHistory tiny(2);
  tiny.visit(section(NavSection::HOME));
  tiny.visit(section(NavSection::SQUAD));
  tiny.visit(section(NavSection::LINEUP));
  EXPECT_EQ(tiny.size(), 2U);
  EXPECT_EQ(tiny.back(ANY), some(section(NavSection::SQUAD)));
  EXPECT_FALSE(tiny.back(ANY).has_value());
}

TEST(NavHistoryTest, EntriesThatCanNoLongerOpenAreSkipped)
{
  NavHistory history;
  history.visit(section(NavSection::HOME));
  history.visit(NavEntry::ofPlayer(99));
  history.visit(section(NavSection::SQUAD));
  history.visit(section(NavSection::TRANSFERS));
  // Player 99 left the game; the squad screen closed (out of work).
  const auto valid = [](const NavEntry& entry)
  {
    return !(entry.kind == NavEntry::Kind::PLAYER && entry.player == 99) &&
           !(entry.kind == NavEntry::Kind::SECTION &&
             entry.section == NavSection::SQUAD);
  };
  EXPECT_EQ(history.peekBack(valid), some(section(NavSection::HOME)));
  EXPECT_EQ(history.back(valid), some(section(NavSection::HOME)));
  EXPECT_FALSE(history.back(valid).has_value());
  EXPECT_EQ(history.forward(valid), some(section(NavSection::TRANSFERS)));
  EXPECT_FALSE(history.forward(valid).has_value());
  // Everything is kept: the entries open again once valid.
  EXPECT_EQ(history.back(ANY), some(section(NavSection::SQUAD)));
}

TEST(NavHistoryTest, StepsNeverLandOnACopyOfTheCurrentScreen)
{
  NavHistory history;
  history.visit(section(NavSection::HOME));
  history.visit(NavEntry::ofPlayer(5));
  history.visit(section(NavSection::HOME));
  const auto noPlayers = [](const NavEntry& entry)
  { return entry.kind != NavEntry::Kind::PLAYER; };
  // The only earlier screen left is Home, which is shown already.
  EXPECT_FALSE(history.back(noPlayers).has_value());
  EXPECT_EQ(history.back(ANY), some(NavEntry::ofPlayer(5)));
}

TEST(NavHistoryTest, ClearForgetsTheCareer)
{
  using enum NavSection;
  NavHistory history;
  history.visit(section(HOME));
  history.visit(section(SQUAD));
  history.back(ANY);
  history.clear();
  EXPECT_EQ(history.size(), 0U);
  EXPECT_FALSE(history.current().has_value());
  EXPECT_FALSE(history.back(ANY).has_value());
  EXPECT_FALSE(history.forward(ANY).has_value());
  history.visit(section(INBOX));
  EXPECT_EQ(history.current(), some(section(INBOX)));
  EXPECT_EQ(history.position(), 0U);
}

TEST(NavHistoryTest, StepBackToAScreenTheHistoryMissed)
{
  using enum NavSection;
  NavHistory history;
  history.stepBackTo(section(HOME));
  EXPECT_EQ(history.current(), some(section(HOME)));

  // Unknown: inserted before the current entry, which stays ahead.
  history.clear();
  history.visit(NavEntry::ofPlayer(3));
  history.stepBackTo(section(SQUAD));
  EXPECT_EQ(history.size(), 2U);
  EXPECT_EQ(history.current(), some(section(SQUAD)));
  EXPECT_EQ(history.forward(ANY), some(NavEntry::ofPlayer(3)));

  // Known but skipped (invalid): the nearest earlier copy becomes current.
  history.clear();
  history.visit(section(NavSection::HOME));
  history.visit(section(NavSection::SQUAD));
  history.visit(NavEntry::ofPlayer(3));
  history.stepBackTo(section(NavSection::SQUAD));
  EXPECT_EQ(history.size(), 3U);
  EXPECT_EQ(history.position(), 1U);
  EXPECT_EQ(history.forward(ANY), some(NavEntry::ofPlayer(3)));

  // The current screen itself: nothing changes.
  history.stepBackTo(NavEntry::ofPlayer(3));
  EXPECT_EQ(history.position(), 2U);

  // Full: the oldest entry makes room.
  NavHistory full(3);
  full.visit(section(NavSection::HOME));
  full.visit(section(NavSection::INBOX));
  full.visit(NavEntry::ofPlayer(3));
  full.stepBackTo(section(NavSection::SQUAD));
  EXPECT_EQ(full.size(), 3U);
  EXPECT_EQ(full.current(), some(section(NavSection::SQUAD)));
  EXPECT_EQ(full.peekBack(ANY), some(section(NavSection::INBOX)));
  EXPECT_EQ(full.peekForward(ANY), some(NavEntry::ofPlayer(3)));
}

TEST(NavHistoryTest, EntriesDescribeTheScreenToReopen)
{
  const GameDateValue day(2025, 9, 14);
  const NavEntry report = NavEntry::ofMatchReport(day, 3, 8);
  EXPECT_EQ(report.kind, NavEntry::Kind::MATCH_REPORT);
  EXPECT_EQ(report.team, 3);
  EXPECT_EQ(report.away_team, 8);
  EXPECT_EQ(report.date, day);
  EXPECT_NE(report, NavEntry::ofMatchReport(day, 8, 3));
  EXPECT_NE(NavEntry::ofClub(4), NavEntry::ofClub(5));
  EXPECT_NE(NavEntry::ofPlayer(4), NavEntry::ofCompare(4, 0));

  EXPECT_TRUE(NavEntry::ofPlayer(1).isDetail());
  EXPECT_TRUE(report.isDetail());
  EXPECT_TRUE(NavEntry::ofCompare(1, 2).isDetail());
  // The empty comparison is the sidebar screen.
  EXPECT_FALSE(NavEntry::ofCompare(0, 0).isDetail());
  EXPECT_FALSE(NavEntry::ofClub(4).isDetail());
  EXPECT_FALSE(section(NavSection::SQUAD).isDetail());
}

TEST(SwipeGestureTest, OneSwipeIsOneStep)
{
  SwipeGesture gesture;
  // 40 units of sideways travel: four times the threshold.
  const Steps steps = swipe(gesture, 2.0f, 0.1f, 20, 1'000 * MS);
  EXPECT_EQ(steps.back, 1);
  EXPECT_EQ(steps.forward, 0);
}

TEST(SwipeGestureTest, DirectionPicksBackOrForward)
{
  SwipeGesture right;
  EXPECT_EQ(swipe(right, 1.5f, 0.0f, 10, 0).back, 1);
  SwipeGesture left;
  const Steps steps = swipe(left, -1.5f, 0.0f, 10, 0);
  EXPECT_EQ(steps.forward, 1);
  EXPECT_EQ(steps.back, 0);
}

TEST(SwipeGestureTest, ShortSwipeStaysBelowTheThreshold)
{
  SwipeGesture gesture;
  const float threshold = gesture.getTuning().threshold;
  const Steps steps =
      swipe(gesture, 1.0f, 0.0f, static_cast<int>(threshold) - 2, 5'000 * MS);
  EXPECT_EQ(steps.back + steps.forward, 0);
  const std::uint64_t last =
      5'000 * MS + static_cast<std::uint64_t>(threshold - 3.0f) * 8 * MS;
  const float progress = gesture.progress(last);
  EXPECT_GT(progress, 0.5f);
  EXPECT_LT(progress, 1.0f);
  // Passing the threshold completes the step at once.
  EXPECT_EQ(gesture.feed(2.5f, 0.0f, last + 8 * MS), SwipeGesture::Step::BACK);
  EXPECT_FLOAT_EQ(gesture.progress(last + 8 * MS), 1.0f);
}

TEST(SwipeGestureTest, CustomThreshold)
{
  SwipeTuning tuning;
  tuning.threshold = 4.0f;
  SwipeGesture gesture(tuning);
  EXPECT_EQ(gesture.feed(2.0f, 0.0f, 0), SwipeGesture::Step::NONE);
  EXPECT_EQ(gesture.feed(2.5f, 0.0f, 8 * MS), SwipeGesture::Step::BACK);
}

TEST(SwipeGestureTest, VerticalScrollingNeverNavigates)
{
  SwipeGesture gesture;
  // A page scroll with a little sideways wobble.
  Steps steps = swipe(gesture, 0.6f, 3.0f, 30, 0);
  EXPECT_EQ(steps.back + steps.forward, 0);
  EXPECT_FLOAT_EQ(gesture.progress(29 * 8 * MS), 0.0f);
  // Going sideways in the same gesture is still part of the scroll.
  steps = swipe(gesture, 3.0f, 0.0f, 20, 30 * 8 * MS);
  EXPECT_EQ(steps.back + steps.forward, 0);
}

TEST(SwipeGestureTest, SwipeDriftingIntoAScrollIsDropped)
{
  SwipeGesture gesture;
  Steps steps = swipe(gesture, 1.0f, 0.0f, 4, 0);
  EXPECT_EQ(steps.back, 0);
  EXPECT_GT(gesture.progress(3 * 8 * MS), 0.0f);
  steps = swipe(gesture, 0.0f, 4.0f, 4, 4 * 8 * MS);
  EXPECT_FLOAT_EQ(gesture.progress(7 * 8 * MS), 0.0f);
  steps = swipe(gesture, 3.0f, 0.0f, 10, 8 * 8 * MS);
  EXPECT_EQ(steps.back + steps.forward, 0);
}

TEST(SwipeGestureTest, PauseStartsANewGesture)
{
  SwipeGesture gesture;
  const std::uint64_t idle = gesture.getTuning().idle_ns;
  EXPECT_EQ(swipe(gesture, 2.0f, 0.0f, 20, 0).back, 1);
  const std::uint64_t end = 19 * 8 * MS;
  // Fingers still moving (no pause): the same gesture, no second step.
  EXPECT_EQ(swipe(gesture, 2.0f, 0.0f, 20, end + 8 * MS).back, 0);
  const std::uint64_t later = end + 20 * 8 * MS + idle + MS;
  // The step shows as complete until the gesture ends, then disappears.
  EXPECT_FLOAT_EQ(gesture.progress(end + 20 * 8 * MS), 1.0f);
  EXPECT_FLOAT_EQ(gesture.progress(later), 0.0f);
  // After a pause, the next swipe is a new step.
  EXPECT_EQ(swipe(gesture, -2.0f, 0.0f, 20, later).forward, 1);
  // So is one after a vertical scroll has ended.
  swipe(gesture, 0.0f, 3.0f, 10, later + 2 * idle);
  EXPECT_EQ(swipe(gesture, 2.0f, 0.0f, 20, later + 4 * idle).back, 1);
}

TEST(SwipeGestureTest, SuppressedGestureIsIgnoredUntilItEnds)
{
  SwipeGesture gesture;
  const std::uint64_t idle = gesture.getTuning().idle_ns;
  swipe(gesture, 1.0f, 0.0f, 5, 0);
  gesture.suppress(5 * 8 * MS);
  EXPECT_FLOAT_EQ(gesture.progress(5 * 8 * MS), 0.0f);
  EXPECT_EQ(swipe(gesture, 2.0f, 0.0f, 20, 6 * 8 * MS).back, 0);
  EXPECT_EQ(swipe(gesture, 2.0f, 0.0f, 20, 26 * 8 * MS + idle + MS).back, 1);

  gesture.reset();
  EXPECT_FLOAT_EQ(gesture.progress(0), 0.0f);
}
