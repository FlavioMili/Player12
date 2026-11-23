// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <atomic>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "database/world/world_runtime.h"
#include "model/world_tuning.h"

namespace
{
struct Table
{
  std::vector<LeagueProfile> profiles;
  /** Position + 1 of each league id's profile; 0 when absent. */
  std::unordered_map<LeagueID, std::uint16_t> index;
};

struct Registry
{
  std::mutex mutex;
  /** Every table ever installed: code may keep references to profiles (club
   * economies do), so tables are never freed. */
  std::deque<Table> tables;
  std::atomic<const Table*> current{nullptr};
};

Registry& registry()
{
  static Registry instance;
  return instance;
}

const Table& table()
{
  const Table* installed = registry().current.load(std::memory_order_acquire);
  if (installed == nullptr)
  {
    // First use before any career: the built-in world provides them.
    (void)World::current();
    installed = registry().current.load(std::memory_order_acquire);
  }
  return *installed;
}
}  // namespace

void installLeagueProfiles(std::vector<LeagueProfile> profiles)
{
  Registry& shared = registry();
  const std::scoped_lock lock(shared.mutex);
  Table& added = shared.tables.emplace_back();
  added.profiles = std::move(profiles);
  for (std::size_t i = 0; i < added.profiles.size(); ++i)
  {
    added.index[added.profiles[i].league_id] =
        static_cast<std::uint16_t>(i + 1);
  }
  shared.current.store(&added, std::memory_order_release);
}

const LeagueProfile& leagueProfile(LeagueID league_id)
{
  const Table& installed = table();
  const auto found = installed.index.find(league_id);
  return found == installed.index.end() ? DEFAULT_LEAGUE_PROFILE
                                        : installed.profiles[found->second - 1];
}

std::span<const LeagueProfile> leagueProfiles() { return table().profiles; }
