// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

/**
 * @file test_state_guard.cpp
 * Keeps tests independent when many of them share one process.
 *
 * The default lanes run each test executable as a few sharded processes, so
 * a test starts where the previous one left the process-global state. This
 * listener is added to every test executable that links fm_test_support. It
 * takes a cheap fingerprint of that state before each test and compares it
 * after:
 *
 *  - the installed world, its league profiles and the nationality registry
 *    (World::install): a difference fails the test and the built-in world is
 *    installed again;
 * A test that installs another world must put the built-in world back
 * (World::install(nullptr)) before it ends.
 */

#include <gtest/gtest.h>

#include <format>
#include <string>

#include "database/world/world_runtime.h"
#include "global/nationality.h"
#include "model/world_tuning.h"

namespace
{
std::string worldFingerprint()
{
  // By content, not identity: loading a save installs a fresh copy of the
  // world stored in it, which is the same world.
  const auto world = World::current();
  std::string counts;
  for (const World::Kind kind : World::KINDS)
    counts += std::format(" {}", world->count(kind));
  return std::format("world='{}' v{} counts{} profiles={} peoples={}",
                     world->manifest().name, world->manifest().version, counts,
                     leagueProfiles().size(), allNationalities().size());
}

class StateGuard final : public testing::EmptyTestEventListener
{
 public:
  void OnTestStart(const testing::TestInfo&) override
  {
    world = worldFingerprint();
  }

  void OnTestEnd(const testing::TestInfo&) override
  {
    if (const std::string now = worldFingerprint(); now != world)
    {
      ADD_FAILURE() << "test leaked process-global state (installed world, "
                       "league profiles or nationalities): put the built-in "
                       "world back with World::install(nullptr)\n  before: "
                    << world << "\n  after:  " << now;
      World::install(nullptr);
    }
  }

 private:
  std::string world;
};

// Registered when the executable starts, before the tests are listed.
const bool installed = []
{
  testing::UnitTest::GetInstance()->listeners().Append(new StateGuard);
  return true;
}();
}  // namespace
