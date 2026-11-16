// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <unistd.h>

#include <cmath>
#include <memory>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/mentoring.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 4'000'000 + getpid() % 100'000 * 10 + offset;
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

Player& mutablePlayer(GameController& controller, PlayerID id)
{
  return controller.getGameData()->getPlayers().at(id);
}

struct Pair
{
  PlayerID mentor = 0;
  PlayerID mentee = 0;
  PlayerID second_mentee = 0;
};

Pair pickPair(const GameController& controller)
{
  Pair pair;
  const TeamID managed = controller.getManagedTeam()->get().getId();
  for (const auto& player : controller.getPlayersForTeam(managed))
  {
    const int age = player.get().getAge();
    if (age >= Mentoring::MENTOR_MIN_AGE + 4 && pair.mentor == 0)
      pair.mentor = player.get().getId();
    else if (age <= Mentoring::MENTEE_MAX_AGE - 1)
    {
      if (pair.mentee == 0)
        pair.mentee = player.get().getId();
      else if (pair.second_mentee == 0)
        pair.second_mentee = player.get().getId();
    }
  }
  return pair;
}
}  // namespace

TEST(Mentoring, WeeklyShiftIsSlowAndBounded)
{
  float mentee = 20.0f;
  float shifted = 0.0f;
  float largest_step = 0.0f;
  for (int week = 0; week < 52 * 5; ++week)
  {
    const float step =
        Mentoring::weeklyShift(95.0f, mentee, 1.0f, 1.0f, shifted);
    largest_step = std::max(largest_step, std::abs(step));
    shifted += step;
    mentee += step;
  }
  EXPECT_LE(largest_step, Mentoring::MAX_WEEKLY_STEP + 1e-5f);
  EXPECT_NEAR(shifted, Mentoring::MAX_TOTAL_SHIFT, 1e-3f);
  // One season moves a receptive youngster only a few points.
  float season = 0.0f;
  for (int week = 0; week < 52; ++week)
    season += Mentoring::weeklyShift(95.0f, 20.0f + season, 1.0f, 1.0f, season);
  EXPECT_GT(season, 3.0f);
  EXPECT_LT(season, Mentoring::MAX_TOTAL_SHIFT);
  // A bad influence pulls down, just as slowly.
  EXPECT_LT(Mentoring::weeklyShift(20.0f, 80.0f, 1.0f, 1.0f, 0.0f), 0.0f);
  // Older youngsters and younger mentors carry less.
  EXPECT_GT(Mentoring::receptiveness(17), Mentoring::receptiveness(23));
  EXPECT_GT(Mentoring::influence(33), Mentoring::influence(25));
  EXPECT_FLOAT_EQ(Mentoring::developmentMultiplier(40), 1.0f);
  EXPECT_FLOAT_EQ(Mentoring::developmentMultiplier(100),
                  1.0f + Mentoring::MAX_DEVELOPMENT_BONUS);
}

TEST(Mentoring, GroupRulesAndWeeklyEffect)
{
  const SlotCleanup slot{uniqueSlot(0)};
  auto controller = makeCareer(slot.slot);
  const Pair pair = pickPair(*controller);
  ASSERT_NE(pair.mentor, 0u);
  ASSERT_NE(pair.mentee, 0u);
  ASSERT_NE(pair.second_mentee, 0u);

  // Rules: a youngster cannot lead, a veteran cannot be mentored.
  EXPECT_EQ(controller->createMentoringGroup(pair.mentee),
            MentoringError::MentorTooYoung);
  uint32_t group = 0;
  ASSERT_EQ(controller->createMentoringGroup(pair.mentor, &group),
            MentoringError::None);
  EXPECT_EQ(controller->createMentoringGroup(pair.mentor),
            MentoringError::AlreadyMentor);
  EXPECT_EQ(controller->addMentee(group, pair.mentor),
            MentoringError::SamePlayer);
  ASSERT_EQ(controller->addMentee(group, pair.mentee), MentoringError::None);
  EXPECT_EQ(controller->addMentee(group, pair.mentee),
            MentoringError::AlreadyMentee);
  // Another club's player cannot join.
  const TeamID other = controller->getTeams()[1].get().getId();
  const PlayerID outsider =
      controller->getPlayersForTeam(other).front().get().getId();
  EXPECT_EQ(controller->addMentee(group, outsider),
            MentoringError::NotSameClub);

  Player& mentor = mutablePlayer(*controller, pair.mentor);
  Player& mentee = mutablePlayer(*controller, pair.mentee);
  PlayerTraits traits = mentor.getTraits();
  traits.professionalism = 95;
  traits.temperament = 90;
  mentor.setTraits(traits);
  traits = mentee.getTraits();
  traits.professionalism = 30;
  traits.temperament = 30;
  mentee.setTraits(traits);
  mentor.mutableDynamics().morale = 70.0f;
  EXPECT_GT(controller->getMentoringMultiplier(pair.mentee), 1.03f);
  EXPECT_FLOAT_EQ(controller->getMentoringMultiplier(pair.second_mentee), 1.0f);

  MentoringSystem& system = controller->getGame()->getWorld().getMentoring();
  for (int week = 0; week < 26; ++week)
    system.onWeek(*controller->getGameData());
  const int after_half = mentee.getTraits().professionalism;
  EXPECT_GT(after_half, 30);
  EXPECT_LE(after_half, 30 + static_cast<int>(Mentoring::MAX_TOTAL_SHIFT));
  EXPECT_GT(mentee.getTraits().temperament, 30);

  // Round trip keeps the group and the partial progress.
  ASSERT_TRUE(controller->saveGame());
  {
    GameController reloaded;
    ASSERT_TRUE(reloaded.loadGame(slot.slot));
    const auto groups = reloaded.getMentoringGroups();
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].mentor_id, pair.mentor);
    ASSERT_EQ(groups[0].mentees.size(), 1u);
    EXPECT_NEAR(
        groups[0].mentees[0].professionalism_shift,
        controller->getMentoringGroups()[0].mentees[0].professionalism_shift,
        1e-4f);
  }

  // Leaving the group stops the effect.
  ASSERT_EQ(controller->removeMentee(group, pair.mentee), MentoringError::None);
  const int frozen = mentee.getTraits().professionalism;
  for (int week = 0; week < 26; ++week)
    system.onWeek(*controller->getGameData());
  EXPECT_EQ(mentee.getTraits().professionalism, frozen);

  // An unhappy mentor spreads his mood.
  ASSERT_EQ(controller->addMentee(group, pair.second_mentee),
            MentoringError::None);
  mentor.mutableDynamics().morale = 20.0f;
  Player& second = mutablePlayer(*controller, pair.second_mentee);
  second.mutableDynamics().morale = 60.0f;
  system.onWeek(*controller->getGameData());
  EXPECT_LT(second.getDynamics().morale, 60.0f);
  EXPECT_EQ(controller->dissolveMentoringGroup(group), MentoringError::None);
  EXPECT_TRUE(controller->getMentoringGroups().empty());
}
