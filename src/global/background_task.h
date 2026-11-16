// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <future>
#include <thread>
#include <type_traits>
#include <utility>

/** Starts work on an explicitly owned thread and returns its result channel.
 * The owner must outlive captured objects. Reusing or destroying the worker
 * joins its previous task; exceptions travel through the returned future.
 */
template <class Function>
auto launchBackgroundTask(std::jthread& worker, Function function)
{
  std::packaged_task<std::invoke_result_t<Function&>()> task(
      std::move(function));
  auto result = task.get_future();
  worker = std::jthread(std::move(task));
  return result;
}
