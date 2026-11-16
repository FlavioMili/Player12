// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/onboarding.h"

#include <array>
#include <bit>
#include <utility>

namespace
{
constexpr std::uint32_t ALL_TASKS = (1U << ONBOARDING_TASK_COUNT) - 1U;

constexpr std::array<const char*, ONBOARDING_TASK_COUNT> TASK_KEYS = {
    "ONBOARDING_TASK_TACTICS", "ONBOARDING_TASK_SQUAD",
    "ONBOARDING_TASK_BOARD",   "ONBOARDING_TASK_TRAINING",
    "ONBOARDING_TASK_MARKET",  "ONBOARDING_TASK_MATCH"};

constexpr std::array<const char*, ONBOARDING_TASK_COUNT> TASK_HELP_KEYS = {
    "ONBOARDING_HELP_TACTICS", "ONBOARDING_HELP_SQUAD",
    "ONBOARDING_HELP_BOARD",   "ONBOARDING_HELP_TRAINING",
    "ONBOARDING_HELP_MARKET",  "ONBOARDING_HELP_MATCH"};

std::uint32_t bitOf(OnboardingTask task)
{
  const auto index = static_cast<std::size_t>(std::to_underlying(task));
  return index < ONBOARDING_TASK_COUNT ? 1U << index : 0U;
}
}  // namespace

bool OnboardingState::complete(OnboardingTask task)
{
  const std::uint32_t bit = bitOf(task);
  if (bit == 0U || (done_mask & bit) != 0U) return false;
  done_mask |= bit;
  return true;
}

bool OnboardingState::isDone(OnboardingTask task) const
{
  const std::uint32_t bit = bitOf(task);
  return bit != 0U && (done_mask & bit) != 0U;
}

std::size_t OnboardingState::doneCount() const
{
  return static_cast<std::size_t>(std::popcount(done_mask));
}

void OnboardingState::restore(std::uint32_t mask, bool was_dismissed)
{
  done_mask = mask & ALL_TASKS;
  dismissed = was_dismissed;
}

const char* onboardingTaskKey(OnboardingTask task)
{
  const auto index = static_cast<std::size_t>(std::to_underlying(task));
  return index < ONBOARDING_TASK_COUNT ? TASK_KEYS[index] : "";
}

const char* onboardingTaskHelpKey(OnboardingTask task)
{
  const auto index = static_cast<std::size_t>(std::to_underlying(task));
  return index < ONBOARDING_TASK_COUNT ? TASK_HELP_KEYS[index] : "";
}
