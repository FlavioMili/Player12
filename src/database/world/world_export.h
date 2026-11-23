// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <string_view>

#include "database/world/world_document.h"

class GameData;

namespace World
{
/**
 * The world of a running career as a world database: every competition, club
 * and player (generated ones and free agents included) as they are now.
 * Countries, cities, travel times, name pools and unknown keys come from
 * @p source (the world the career was created from); club identity (colours,
 * stadium, nickname) comes from @p source, or from the installed world.
 * The career is not changed.
 * @param label Manifest name prefix (the club); empty uses the source's name.
 * @param date Text appended to the name ("Lecce - 2027-07-01").
 * @param season Reference season stored in the manifest (0: none).
 */
Document exportCareer(const GameData& data, const Document& source,
                      std::string_view label = {}, std::string_view date = {},
                      int season = 0);
}  // namespace World
