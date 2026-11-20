// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>

#include "global/types.h"

class GameData;

/**
 * Synthetic clubs for controlled sandbox matches, in a "Test teams" league
 * added to the generated world:
 *
 *  - Team N (N = 6, 16, ..., 96): an 18-man 4-4-2 squad (two keepers) whose
 *    every stat is N, outfield and goalkeeping alike.
 *  - Team Tiered: one outfield player and one keeper at each of those
 *    levels, named by it ("Outfield 56", "Keeper 56"). The ten outfielders
 *    start in a 4-4-2, weakest at the back; Keeper 66 starts, the other nine
 *    keepers are the bench.
 *
 * A club cannot play itself, so each is added twice: "Team 66" and
 * "Team 66 (copy)". Everyone is 26, 180 cm, right-footed, with default
 * traits and fresh condition, so only the stats tell players apart.
 *
 * Call right after the world is generated (before anything else allocates
 * player ids): the same world seed then gives the same ids, so recordings
 * of matches between test teams replay.
 */
void addSandboxTestTeams(GameData& data);

/**
 * The clubs the match setup opens with (home, away): Team 66 and its copy
 * once addSandboxTestTeams() ran, else 0 (the setup picks its own).
 */
std::array<TeamID, 2>& sandboxDefaultClubs();
