// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <vector>

struct sqlite3;

#include "database/world/world_document.h"
#include "global/nationality.h"
#include "model/world_tuning.h"

/**
 * The world the running (or starting) career is built from. A new career
 * installs the world the player chose; loading a career installs the world
 * stored in its save (the built-in world for saves made before world
 * databases). Everything that reads world content (generation, club
 * identities, name pools, league profiles, calendars) reads it from here.
 */
namespace World
{
/** The installed world; installs the built-in world on first use. */
std::shared_ptr<const Document> current();
/** Installs @p world (null: the built-in world) and its league profiles. */
void install(std::shared_ptr<const Document> world);
/** Changes on every install; caches of derived data key on it. */
std::uint64_t generation();
/** The built-in world (assets/world/default.fmdb), parsed once. Throws when
 * the asset is missing or invalid: the installation is broken. */
std::shared_ptr<const Document> defaultWorld();

/** Keeps @p world in the save (table WorldDocument), so a career always
 * reloads the world it was created from. */
void storeInSave(sqlite3* db, const Document& world);
/** The world stored in a save; null for saves made before world databases
 * (they use the built-in world) or when the stored copy cannot be read. */
std::shared_ptr<const Document> fromSave(sqlite3* db);

/** Peoples (demonyms) of the world's countries that are not built-in
 * nationalities, in country order; install() hands them to the nationality
 * registry. */
std::vector<CustomNationality> peoplesOf(const Document& world);

/** League profile <-> JSON ("profile" object of a league). */
nlohmann::json profileToJson(const LeagueProfile& profile);
/** @p base with every field present in @p json applied. */
LeagueProfile profileFromJson(const nlohmann::json& json,
                              const LeagueProfile& base);
/** Profiles of every league: the authored values over a profile synthesised
 * from the league's country and division tier. */
std::vector<LeagueProfile> buildProfiles(const Document& world);

/**
 * Data derived from the installed world, rebuilt when another world is
 * installed. Values stay alive for the whole process, so references handed
 * out earlier remain valid while a career is switched.
 */
template <typename T>
class PerWorld
{
 public:
  template <typename Loader>
  const T& get(Loader&& load)
  {
    const std::uint64_t now = generation();
    const std::scoped_lock lock(mutex);
    if (values.empty() || built_for != now)
    {
      values.push_back(std::make_unique<const T>(load()));
      built_for = now;
    }
    return *values.back();
  }

 private:
  std::mutex mutex;
  std::uint64_t built_for = 0;
  std::deque<std::unique_ptr<const T>> values;
};
}  // namespace World
