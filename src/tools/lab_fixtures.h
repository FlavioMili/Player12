// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "global/stats_config.h"
#include "global/types.h"
#include "model/lineup.h"
#include "model/strategy.h"

class Player;

/** Synthetic squads and tactic presets for isolated lab matches. */
namespace Lab
{
/**
 * The game's stats configuration (assets/config/stats_config.json), so lab
 * players rate exactly like generated ones; the calibration-test constants
 * when the file cannot be read.
 */
StatsConfig loadStatsConfig();

/** One outfield slot of a lab formation: its role (which shapes the
 * player's attributes) and its Lineup position. */
struct FormationSlot
{
  PlayerRole role = PlayerRole::UNKNOWN;
  Vector2F position;
};
/** The ten outfield slots of a formation (the keeper is always added). */
using FormationSlots = std::array<FormationSlot, 10>;

/**
 * 4-4-2 matchday squad (or the outfield slots of `formation`, if given) (eleven
 * starters, seven reserves) whose attributes are role-shaped offsets around
 * `rating` with a small deterministic per-player spread. Same construction as
 * test/test_match_calibration.cpp (createCalibrationTeam), duplicated so the
 * lab does not depend on tests, except that the spread depends on the squad
 * slot only: two squads of the same rating are identical (the test's ID-based
 * spread makes team 1 about 2.3 attribute points stronger than team 2). Players
 * are owned by `pool` and get IDs team_id * 100 + index.
 */
Lineup buildLabLineup(TeamID team_id, float rating,
                      std::vector<std::unique_ptr<Player>>& pool,
                      const FormationSlots* formation = nullptr);

/**
 * The same 4-4-2 squad with every stat of `config` at `rating` for every
 * player, keepers and outfielders alike, all 26, 180 cm and right-footed:
 * the match sandbox's "Team N" (match_sandbox_test_teams.cpp). Same ids as
 * buildLabLineup().
 */
Lineup buildUniformLineup(TeamID team_id, float rating,
                          const StatsConfig& config,
                          std::vector<std::unique_ptr<Player>>& pool,
                          const FormationSlots* formation = nullptr);

/** Mean overall of the eleven starters (the contract's team rating). */
float lineupRating(const Lineup& lineup, const StatsConfig& config);

struct TacticPreset
{
  std::string_view name;
  StrategySliders sliders;
};

/**
 * The four presets of the tactics screen (gui/scenes/strategy_scene.cpp,
 * PRESETS). Duplicated because the GUI table lives in an anonymous
 * namespace of fm_ui; keep both in sync.
 */
inline constexpr std::array<TacticPreset, 4> TACTIC_PRESETS{{
    {.name = "Balanced",
     .sliders = {.pressing = 0.50F,
                 .riskTaking = 0.50F,
                 .offensiveBias = 0.50F,
                 .widthUsage = 0.50F,
                 .compactness = 0.50F}},
    {.name = "Front foot",
     .sliders = {.pressing = 0.82F,
                 .riskTaking = 0.72F,
                 .offensiveBias = 0.76F,
                 .widthUsage = 0.68F,
                 .compactness = 0.64F}},
    {.name = "Counter",
     .sliders = {.pressing = 0.38F,
                 .riskTaking = 0.68F,
                 .offensiveBias = 0.62F,
                 .widthUsage = 0.58F,
                 .compactness = 0.42F}},
    {.name = "Control",
     .sliders = {.pressing = 0.62F,
                 .riskTaking = 0.34F,
                 .offensiveBias = 0.44F,
                 .widthUsage = 0.72F,
                 .compactness = 0.72F}},
}};
}  // namespace Lab
