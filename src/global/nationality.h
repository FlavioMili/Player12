// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "global/languages.h"

/**
 * @file nationality.h
 * @brief Nationalities of the running world.
 *
 * The 159 built-in nationalities (the Language enum, table in languages.h)
 * keep their numbers: saves store them as integers. A world can add peoples
 * of its own; each takes a free 16-bit value after the built-in ones when the
 * world is installed (World::install), in the order of its countries, so a
 * career reloads the same numbers from the world stored in its save.
 * Game code reads names through these functions, never the tables.
 */

/** Smallest and largest value a world-defined nationality can take. */
inline constexpr unsigned FIRST_CUSTOM_NATIONALITY =
    std::to_underlying(Language::MU) + 1;
inline constexpr unsigned LAST_CUSTOM_NATIONALITY =
    std::numeric_limits<NationalityID>::max();

/** A people defined by the installed world. */
struct CustomNationality
{
  std::string name;            /*!< Demonym ("Westerosi"). */
  RegionID region = 0;         /*!< Id of its country's region. */
  std::int64_t country_id = 0; /*!< World country it belongs to. */
};

/** Replaces the world-defined nationalities (extras beyond the free values
 * are dropped). Called by World::install. */
void setCustomNationalities(std::vector<CustomNationality> peoples);

/** Name of a built-in or world-defined nationality; empty when unknown. */
std::string nationalityName(Language nationality);
/** Nationality called @p name; nullopt when no such people exists. */
std::optional<Language> nationalityFromName(std::string_view name);
/** The world-defined people behind @p nationality, if it is one. */
std::optional<CustomNationality> customNationality(Language nationality);
/** Every nationality (built-in and world-defined) with its name, by name. */
std::vector<std::pair<Language, std::string>> allNationalities();
/** Name for display: translated for built-in peoples, as written for others. */
std::string nationalityLabel(Language nationality);
