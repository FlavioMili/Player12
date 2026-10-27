// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

/**
 * @file test_worlds.h
 * Cached, ready-made worlds and careers for tests.
 *
 * Generating a world or simulating weeks of a career costs seconds; most tests
 * only need the resulting state. This library builds each state once and hands
 * out independent copies:
 *
 *  - in memory, once per process (a SQLite backup of the generated world);
 *  - on disk, once per build of the test executable (a save file in the system
 *    temporary folder keyed by the executable's size and timestamp, because
 *    CTest runs every test case in its own process).
 *
 * The calendar is not jumped: the model advances players, finances, the
 * market and competitions day by day, so a date reached by a shortcut would be
 * a state the game can never produce. A career "at a date" is instead produced
 * by one real simulation (the first process that needs it) and stored as a
 * checkpoint save; later tests load the checkpoint. Loading is a file copy and
 * a load, a few hundred milliseconds instead of seconds of simulation.
 *
 * Every function returns a fresh object; mutating it never affects other tests.
 */

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "controller/game_controller.h"
#include "database/database_connection.h"
#include "database/gamedata.h"
#include "model/gamedate.h"

namespace TestWorlds
{
/** World seed used by the test suite (the same as FM_WORLD_SEED in CTest). */
inline constexpr std::uint64_t DEFAULT_SEED = 20250702;

/** A generated world: its data and the database connection backing it. */
struct World
{
  std::shared_ptr<GameData> data;
  std::shared_ptr<DatabaseConnection> connection;
};

/**
 * A copy of the generated world of @p seed (default league set, no career).
 * The world is generated once per process per seed; each call backs the
 * database up to a new in-memory connection and loads a new GameData on it.
 */
World defaultWorld(std::uint64_t seed = DEFAULT_SEED);

/** Steps that bring a new career (managed club: first team) to a state. */
using CareerPrepare = std::function<void(GameController&)>;

/**
 * A started career in @p slot, loaded from the checkpoint @p key. The first
 * caller (per build of the executable, across processes) runs newGame with
 * @p seed, selects the first club, runs @p prepare and saves the checkpoint;
 * everyone else loads it. @p key must identify what @p prepare does: two
 * different preparations under one key would share the first one's state.
 * The slot is the caller's: remove it with RuntimePaths::removeSave.
 */
std::unique_ptr<GameController> career(
    int slot, const std::string& key, const CareerPrepare& prepare = {},
    std::uint64_t seed = DEFAULT_SEED);

/** A career on its first day (the state newGame leaves), club chosen. */
std::unique_ptr<GameController> newCareer(int slot,
                                          std::uint64_t seed = DEFAULT_SEED);

/**
 * A career advanced day by day (the real simulation, once) until the game date
 * reaches @p date; the checkpoint is shared by all tests asking for the date.
 */
std::unique_ptr<GameController> careerAt(int slot, const GameDateValue& date,
                                         std::uint64_t seed = DEFAULT_SEED);

/**
 * A career advanced to the day of its @p n-th managed fixture (1 = the first),
 * through advanceToNextManagedFixture, so the next Continue plays that match.
 */
std::unique_ptr<GameController> careerAtManagedFixture(
    int slot, int n, std::uint64_t seed = DEFAULT_SEED);

/** A slot number that cannot collide with other processes of the same run. */
int uniqueSlot(int offset = 0);

}  // namespace TestWorlds
