// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

/**
 * @brief Steps of the first-week checklist of a new career.
 *
 * Bit positions are persisted (GuidanceState.onboarding_done); append new
 * steps at the end.
 */
enum class OnboardingTask : std::uint8_t
{
  ReviewTactics = 0, /*!< Tactics screen opened. */
  CheckSquad,        /*!< Squad screen opened. */
  BoardObjective,    /*!< Club screen (board objective) opened. */
  SetTraining,       /*!< Training screen opened. */
  ExploreMarket,     /*!< Transfers or scouting opened. */
  PlayFirstMatch,    /*!< First match of the managed club played. */
  COUNT
};

inline constexpr std::size_t ONBOARDING_TASK_COUNT =
    static_cast<std::size_t>(std::to_underlying(OnboardingTask::COUNT));

/**
 * @brief Progress of the first-week checklist.
 *
 * Steps tick themselves when the manager does them; the checklist disappears
 * for good once every step is done or the manager dismisses it.
 */
class OnboardingState
{
 public:
  /** Marks a step as done; true when it was not done before. */
  bool complete(OnboardingTask task);
  bool isDone(OnboardingTask task) const;
  std::size_t doneCount() const;
  bool allDone() const { return doneCount() == ONBOARDING_TASK_COUNT; }

  void dismiss() { dismissed = true; }
  /** Brings a dismissed checklist back (steps already done stay done). */
  void undismiss() { dismissed = false; }
  bool isDismissed() const { return dismissed; }
  /** Still worth showing: neither finished nor dismissed. */
  bool isVisible() const { return !dismissed && !allDone(); }

  std::uint32_t doneMask() const { return done_mask; }
  /** Restores persisted progress (unknown bits are dropped). */
  void restore(std::uint32_t mask, bool was_dismissed);

 private:
  std::uint32_t done_mask = 0;
  bool dismissed = false;
};

/** Language key of a checklist step's label. */
const char* onboardingTaskKey(OnboardingTask task);
/** Language key of the one-line explanation of a step. */
const char* onboardingTaskHelpKey(OnboardingTask task);
