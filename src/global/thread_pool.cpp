// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "global/thread_pool.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string_view>
#include <system_error>

#if defined(__linux__)
#include <sys/resource.h>
#include <unistd.h>

#include <cerrno>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifdef _MSC_VER
#include <Windows.h>
#else
#include <windows.h>
#endif
#endif

namespace
{
/** Nice offset of the workers relative to the thread that built the pool. */
[[maybe_unused]] constexpr int WORKER_NICE_OFFSET = 5;
[[maybe_unused]] constexpr int LOWEST_NICE = 19;

void lowerCurrentThreadPriority()
{
#if defined(__linux__)
  // Linux nice values are per thread; failures just keep the priority.
  const auto thread_id = static_cast<id_t>(gettid());
  errno = 0;
  const int current = getpriority(PRIO_PROCESS, thread_id);
  if (errno == 0)
    setpriority(PRIO_PROCESS, thread_id,
                std::min(current + WORKER_NICE_OFFSET, LOWEST_NICE));
#elif defined(_WIN32)
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
}

/** Shared by the caller and the helper jobs of one parallelFor() call. */
struct Batch
{
  Batch(std::size_t total, const std::function<void(std::size_t)>& work)
      : count(total), task(&work)
  {
  }

  const std::size_t count;
  // Only dereferenced after claiming an index below count, which keeps the
  // caller (and so the task) alive until that index finished.
  const std::function<void(std::size_t)>* task;
  std::atomic<std::size_t> next{0};
  std::atomic<std::size_t> finished{0};
  std::mutex error_mutex;
  std::exception_ptr error;
};

void drain(Batch& batch)
{
  for (std::size_t index = batch.next.fetch_add(1); index < batch.count;
       index = batch.next.fetch_add(1))
  {
    try
    {
      (*batch.task)(index);
    }
    catch (...)
    {
      const std::scoped_lock lock(batch.error_mutex);
      if (!batch.error) batch.error = std::current_exception();
    }
    if (batch.finished.fetch_add(1) + 1 == batch.count)
      batch.finished.notify_all();
  }
}
}  // namespace

ThreadPool::ThreadPool(unsigned worker_count)
{
  const unsigned count = std::min(worker_count, MAX_THREADS);
  workers.reserve(count);
  for (unsigned i = 0; i < count; ++i)
    workers.emplace_back([this](const std::stop_token& stop)
                         { workerLoop(stop); });
}

ThreadPool::~ThreadPool()
{
  // Stop every worker before the first join (the stop request wakes a
  // waiting worker), so shutdown waits for one wake-up, not one per worker.
  // A worker inside a job finishes it first; batch callers only return once
  // their batch completed, so no batch needs the pool any more.
  for (std::jthread& worker : workers) worker.request_stop();
  workers.clear();
}

void ThreadPool::workerLoop(const std::stop_token& stop)
{
  lowerCurrentThreadPriority();
  while (true)
  {
    std::function<void()> job;
    {
      std::unique_lock lock(mutex);
      if (!wake.wait(lock, stop, [this] { return !jobs.empty(); })) return;
      job = std::move(jobs.front());
      jobs.pop_front();
    }
    job();
  }
}

void ThreadPool::parallelFor(std::size_t count,
                             const std::function<void(std::size_t)>& task)
{
  if (count == 0) return;
  const auto batch = std::make_shared<Batch>(count, task);
  if (const std::size_t helpers =
          std::min<std::size_t>(workers.size(), count - 1);
      helpers > 0)
  {
    {
      const std::scoped_lock lock(mutex);
      for (std::size_t i = 0; i < helpers; ++i)
        jobs.emplace_back([batch] { drain(*batch); });
    }
    wake.notify_all();
  }
  drain(*batch);
  for (std::size_t done = batch->finished.load(); done < count;
       done = batch->finished.load())
    batch->finished.wait(done);
  if (batch->error) std::rethrow_exception(batch->error);
}

unsigned ThreadPool::defaultThreadCount()
{
#if defined(_WIN32)
  char* environment = nullptr;
  if (std::size_t size = 0;
      _dupenv_s(&environment, &size, "FM_SIM_THREADS") != 0)
    environment = nullptr;
  const auto free_value = [](char* allocation) { std::free(allocation); };
  const std::unique_ptr<char, decltype(free_value)> owned(environment,
                                                          free_value);
#endif
  if (const char* configured =
#if defined(_WIN32)
          owned.get();
#else
          std::getenv("FM_SIM_THREADS");
#endif
      configured != nullptr)
  {
    unsigned value = 0;
    const std::string_view text(configured);
    const char* end = text.data() + text.size();
    if (const auto [ptr, error] = std::from_chars(configured, end, value);
        error == std::errc{} && ptr == end && value > 0)
      return std::min(value, MAX_THREADS);
  }
  const unsigned hardware = std::thread::hardware_concurrency();
  return std::clamp(hardware > 1 ? hardware - 1 : 1U, 1U, MAX_THREADS);
}
