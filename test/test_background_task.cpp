// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <stdexcept>

#include "global/background_task.h"

TEST(BackgroundTask, ReturnsAResult)
{
  std::jthread worker;
  auto result = launchBackgroundTask(worker, [] { return 42; });
  EXPECT_EQ(result.get(), 42);
}

TEST(BackgroundTask, PropagatesWorkerExceptionsThroughTheFuture)
{
  std::jthread worker;
  auto result = launchBackgroundTask(
      worker, []() -> int { throw std::runtime_error("worker failed"); });
  EXPECT_THROW(result.get(), std::runtime_error);
}

TEST(BackgroundTask, SupportsMoveOnlyCapturesAndJoinsAtDestruction)
{
  std::atomic<int> value{0};
  {
    std::jthread worker;
    auto result =
        launchBackgroundTask(worker, [owned = std::make_unique<int>(7), &value]
                             { value.store(*owned); });
    EXPECT_TRUE(result.valid());
    // A packaged-task future does not join the worker; its owner must do so.
  }
  EXPECT_EQ(value.load(), 7);
}

TEST(BackgroundTask, ReusesTheWorkerWithoutLosingThePreviousResult)
{
  std::jthread worker;
  auto first = launchBackgroundTask(worker, [] { return 11; });
  auto second = launchBackgroundTask(worker, [] { return 22; });
  EXPECT_EQ(first.get(), 11);
  EXPECT_EQ(second.get(), 22);
}
