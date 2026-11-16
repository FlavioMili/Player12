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

#include "controller/game_controller.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/delegation.h"
#include "model/onboarding.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 320'000 + getpid() % 100'000 * 10 + offset;
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

size_t activeAssignments(const GameController& controller)
{
  return static_cast<size_t>(std::ranges::count_if(
      controller.getScoutAssignments(),
      [](const ScoutAssignment& assignment) { return !assignment.finished; }));
}
}  // namespace

TEST(DelegationTest, PresetsDifferAndAreRecognised)
{
  const DelegationPolicy hands_on =
      DelegationPolicy::preset(DelegationPreset::HandsOn);
  const DelegationPolicy balanced =
      DelegationPolicy::preset(DelegationPreset::Balanced);
  const DelegationPolicy assistant =
      DelegationPolicy::preset(DelegationPreset::AssistantRuns);
  EXPECT_EQ(DelegationPolicy(), balanced);
  EXPECT_EQ(hands_on.matchingPreset(), DelegationPreset::HandsOn);
  EXPECT_EQ(assistant.matchingPreset(), DelegationPreset::AssistantRuns);
  for (std::size_t index = 0; index < DUTY_COUNT; ++index)
  {
    const auto duty = static_cast<Duty>(index);
    EXPECT_TRUE(assistant.delegated(duty));
    EXPECT_FALSE(hands_on.delegated(duty));
    EXPECT_STRNE(dutyKey(duty), "");
    EXPECT_STRNE(dutyHelpKey(duty), "");
  }
  // The matchday safety net is on by default, as before this screen.
  EXPECT_TRUE(balanced.delegated(Duty::LineupFixes));
}

TEST(DelegationTest, CustomChangesLeaveThePreset)
{
  DelegationPolicy policy;
  EXPECT_TRUE(policy.set(Duty::Substitutions, DutyOwner::Assistant));
  EXPECT_FALSE(policy.matchingPreset().has_value());
  EXPECT_TRUE(policy.set(Duty::Friendlies, DutyOwner::Manager));
  EXPECT_FALSE(policy.delegated(Duty::Friendlies));
  EXPECT_FALSE(policy.set(Duty::COUNT, DutyOwner::Manager));
  policy.apply(DelegationPreset::HandsOn);
  EXPECT_EQ(policy.matchingPreset(), DelegationPreset::HandsOn);
}

TEST(DelegationTest, OnboardingTicksOnceAndHidesWhenDoneOrDismissed)
{
  OnboardingState state;
  EXPECT_TRUE(state.isVisible());
  EXPECT_TRUE(state.complete(OnboardingTask::CheckSquad));
  EXPECT_FALSE(state.complete(OnboardingTask::CheckSquad));
  EXPECT_EQ(state.doneCount(), 1U);
  for (std::size_t index = 0; index < ONBOARDING_TASK_COUNT; ++index)
    state.complete(static_cast<OnboardingTask>(index));
  EXPECT_TRUE(state.allDone());
  EXPECT_FALSE(state.isVisible());

  OnboardingState dismissed;
  dismissed.dismiss();
  EXPECT_FALSE(dismissed.isVisible());
  dismissed.restore(0xFFFFFFFFU, false);
  EXPECT_EQ(dismissed.doneCount(), ONBOARDING_TASK_COUNT);
}

TEST(DelegationTest, GuidanceStateSurvivesSaveAndLoad)
{
  SlotCleanup slot{uniqueSlot(1)};
  auto controller = makeCareer(slot.slot);
  controller->applyDelegationPreset(DelegationPreset::AssistantRuns);
  ASSERT_TRUE(
      controller->setDutyOwner(Duty::Substitutions, DutyOwner::Manager));
  controller->completeOnboardingTask(OnboardingTask::ReviewTactics);
  controller->completeOnboardingTask(OnboardingTask::SetTraining);
  const TeamID opponent = controller->getTeams().back().get().getId();
  const PlayerID target =
      controller->getPlayersForTeam(opponent).front().get().getId();
  ASSERT_TRUE(controller->setOppositionInstruction(
      opponent, target, OppositionInstruction::ShowWeakFoot));
  // Instructions only apply to that club's players.
  EXPECT_FALSE(controller->setOppositionInstruction(
      opponent + 1000, target, OppositionInstruction::Press));
  const DelegationPolicy policy = controller->getDelegation();
  ASSERT_TRUE(controller->saveGame());

  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  EXPECT_EQ(reloaded.getDelegation(), policy);
  EXPECT_FALSE(reloaded.isDelegated(Duty::Substitutions));
  EXPECT_TRUE(reloaded.getAssistantFixesLineup());
  EXPECT_TRUE(reloaded.getOnboarding().isDone(OnboardingTask::ReviewTactics));
  EXPECT_TRUE(reloaded.getOnboarding().isDone(OnboardingTask::SetTraining));
  EXPECT_FALSE(reloaded.getOnboarding().isDone(OnboardingTask::CheckSquad));
  EXPECT_TRUE(reloaded.getOnboarding().isVisible());
  EXPECT_EQ(reloaded.getOppositionInstruction(opponent, target),
            OppositionInstruction::ShowWeakFoot);

  reloaded.dismissOnboarding();
  ASSERT_TRUE(reloaded.saveGame());
  GameController again;
  ASSERT_TRUE(again.loadGame(slot.slot));
  EXPECT_FALSE(again.getOnboarding().isVisible());
}

TEST(DelegationTest, LineupToggleAndDelegationAreOneSetting)
{
  SlotCleanup slot{uniqueSlot(2)};
  auto controller = makeCareer(slot.slot);
  controller->setAssistantFixesLineup(false);
  EXPECT_FALSE(controller->isDelegated(Duty::LineupFixes));
  ASSERT_TRUE(
      controller->setDutyOwner(Duty::LineupFixes, DutyOwner::Assistant));
  EXPECT_TRUE(controller->getAssistantFixesLineup());
}

TEST(DelegationTest, AssistantActsOnlyOnDelegatedDuties)
{
  SlotCleanup slot{uniqueSlot(3)};
  auto controller = makeCareer(slot.slot);
  controller->applyDelegationPreset(DelegationPreset::HandsOn);
  ASSERT_TRUE(controller->setTrainingIntensity(TrainingIntensity::High));
  ASSERT_TRUE(controller->setCongestionAutoAdjust(false));
  const size_t before = activeAssignments(*controller);
  for (int day = 0; day < 7; ++day) controller->advanceDay();
  // Nobody touched the manager's choices.
  EXPECT_EQ(activeAssignments(*controller), before);
  EXPECT_EQ(controller->getTrainingPlan()->intensity, TrainingIntensity::High);
  EXPECT_FALSE(controller->getTrainingPlan()->auto_congestion);

  controller->applyDelegationPreset(DelegationPreset::AssistantRuns);
  EXPECT_TRUE(controller->getTrainingPlan()->auto_congestion);
  for (int day = 0; day < 7; ++day) controller->advanceDay();
  EXPECT_NE(controller->getTrainingPlan()->intensity, TrainingIntensity::High);
  if (!controller->getScouts().empty())
    EXPECT_GT(activeAssignments(*controller), before);
}

TEST(DelegationTest, CareersFromBeforeTheChecklistSkipIt)
{
  SlotCleanup slot{uniqueSlot(4)};
  auto controller = makeCareer(slot.slot);
  for (int day = 0; day < 10; ++day) controller->advanceDay();
  ASSERT_TRUE(controller->saveGame());
  {
    sqlite3* db = nullptr;
    ASSERT_EQ(
        sqlite3_open(RuntimePaths::savePath(slot.slot).string().c_str(), &db),
        SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db,
                           "DELETE FROM GuidanceState; DELETE FROM "
                           "DelegatedDuties;",
                           nullptr, nullptr, nullptr),
              SQLITE_OK);
    sqlite3_close(db);
  }
  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  EXPECT_FALSE(reloaded.getOnboarding().isVisible());
  EXPECT_EQ(reloaded.getDelegation(), DelegationPolicy());
}

TEST(DelegationTest, ManualChangeTakesTheDutyBackWithUndo)
{
  SlotCleanup slot{uniqueSlot(5)};
  auto controller = makeCareer(slot.slot);
  ASSERT_TRUE(controller->isDelegated(Duty::TrainingSchedule));
  // The assistant's own adjustments never hand the duty back.
  for (int day = 0; day < 3; ++day) controller->advanceDay();
  EXPECT_TRUE(controller->isDelegated(Duty::TrainingSchedule));
  EXPECT_FALSE(controller->getReclaimedDuty().has_value());

  // The manager sets the intensity by hand: he now owns training and the
  // assistant stops overriding it.
  ASSERT_TRUE(controller->setTrainingIntensity(TrainingIntensity::High));
  EXPECT_FALSE(controller->isDelegated(Duty::TrainingSchedule));
  EXPECT_EQ(controller->getReclaimedDuty(), Duty::TrainingSchedule);
  for (int day = 0; day < 3; ++day) controller->advanceDay();
  EXPECT_EQ(controller->getTrainingPlan()->intensity, TrainingIntensity::High);

  // Undo hands it back.
  controller->undoReclaimedDuty();
  EXPECT_TRUE(controller->isDelegated(Duty::TrainingSchedule));
  EXPECT_FALSE(controller->getReclaimedDuty().has_value());

  // Setting the same value is no change and keeps the delegation.
  const TrainingIntensity current = controller->getTrainingPlan()->intensity;
  ASSERT_TRUE(controller->setTrainingIntensity(current));
  EXPECT_TRUE(controller->isDelegated(Duty::TrainingSchedule));

  // Cancelling one of the assistant's scouting trips takes scouting back.
  ASSERT_TRUE(controller->setDutyOwner(Duty::ScoutingAssignments,
                                       DutyOwner::Assistant));
  for (int day = 0; day < 7; ++day) controller->advanceDay();
  if (const auto active =
          std::ranges::find_if(controller->getScoutAssignments(),
                               [](const ScoutAssignment& assignment)
                               { return !assignment.finished; });
      active != controller->getScoutAssignments().end())
  {
    ASSERT_TRUE(controller->cancelScoutAssignment(active->id));
    EXPECT_FALSE(controller->isDelegated(Duty::ScoutingAssignments));
    EXPECT_EQ(controller->getReclaimedDuty(), Duty::ScoutingAssignments);
    controller->dismissReclaimedDuty();
    EXPECT_FALSE(controller->getReclaimedDuty().has_value());
    EXPECT_FALSE(controller->isDelegated(Duty::ScoutingAssignments));
  }

  // A duty the manager already owns is not "taken back".
  controller->applyDelegationPreset(DelegationPreset::HandsOn);
  ASSERT_TRUE(controller->setTrainingIntensity(TrainingIntensity::Low));
  EXPECT_FALSE(controller->getReclaimedDuty().has_value());
}
