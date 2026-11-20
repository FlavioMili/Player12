// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/match_pitch.h"

/**
 * Game xG (game xG plan, `Notes and plans/match_engine_game_xg_plan.md`):
 * the engine's own conversion, measured by running shots in the sandbox's
 * Shot drill and saved per layer and goal kind.
 *
 * Layer 1, the empty goal: the chance a shot from a spot goes in with no
 * keeper and nobody in the way, by the spot (metres out from the goal line,
 * metres off the middle) and the shooter's Shooting (the 1-100 rating).
 * A table holds a smoothed grid for lookups and the raw measured samples
 * (so the smoothing can be redone).
 */

/** One measured point: shots taken from a spot at a Shooting (and, for the
 * keeper layer, against a Goalkeeping; 0 without a keeper), and goals. */
struct GameXgSample
{
  float line = 0.0F;
  float side = 0.0F;
  float shooting = 0.0F;
  float goalkeeping = 0.0F;
  int shots = 0;
  int goals = 0;
};

class GameXgTable
{
 public:
  /** Evenly spaced values: from, from + step, ... (count of them). */
  struct Axis
  {
    float from = 0.0F;
    float step = 1.0F;
    int count = 0;
    [[nodiscard]] float at(int index) const
    {
      return from + (step * static_cast<float>(index));
    }
  };

  /** The layers' names, as stored and in the file names. Layer 1: the
   * chance into an empty goal. Layer 2: the factor a keeper in his usual
   * (set) position puts on it, the chance with him over the chance without
   * (game xG plan, step 2). */
  static constexpr std::string_view EMPTY_GOAL = "empty goal";
  static constexpr std::string_view KEEPER = "keeper";

  std::string layer{EMPTY_GOAL};
  GoalKind goal = GoalKind::FULL;
  /** Local time the sweep finished ("2026-11-20 153012"). */
  std::string measured;
  /** Shots per measured point, and the first seed (seeds run on from it). */
  int repeats = 0;
  std::uint32_t firstSeed = 1;
  /** The measured shooter's stronger foot ("right" or "left"). The engine's
   * feet mirror each other, so the other foot's map is this one with the
   * sides swapped. */
  std::string foot = "right";
  /** The side axis holds the distance off the middle (left and right were
   * found to agree and were folded together); else signed. */
  bool mirrored = true;
  /** Shooting ratings (1-100) the grid is held at, ascending. */
  std::vector<float> shooting;
  /** The keeper layer's Goalkeeping ratings (1-100), ascending; empty for
   * the empty-goal layer. */
  std::vector<float> goalkeeping;
  Axis line;
  Axis side;
  /** Smoothed value per Goalkeeping (one block without), then Shooting,
   * then line, then side: a chance (empty goal) or a factor (keeper). */
  std::vector<float> values;
  std::vector<GameXgSample> samples;

  [[nodiscard]] bool empty() const { return values.empty(); }
  /** The value at a spot for a Shooting rating (and Goalkeeping rating, for
   * the keeper layer; 1-100), interpolated over the grid and the ratings;
   * outside them, the value at their edge. `side` signed (+ towards the
   * bottom touchline, attacking the right-hand goal); folded when the table
   * is mirrored. */
  [[nodiscard]] float value(float lineMetres, float sideMetres,
                            float shootingRating,
                            float goalkeepingRating = 0.0F) const;
  /** Total shots measured. */
  [[nodiscard]] long long shots() const;

  [[nodiscard]] std::string toJson() const;
  [[nodiscard]] static std::optional<GameXgTable> fromJson(
      const std::string& text, std::string& error);
  bool save(const std::filesystem::path& path, std::string& error) const;
  [[nodiscard]] static std::optional<GameXgTable> load(
      const std::filesystem::path& path, std::string& error);

 private:
  [[nodiscard]] float at(std::size_t level, int lineIndex, int sideIndex) const;
  [[nodiscard]] float plane(std::size_t level, float lineMetres,
                            float sideMetres) const;
  /** Over the Shooting levels of one Goalkeeping block. */
  [[nodiscard]] float overShooting(std::size_t block, float lineMetres,
                                   float sideMetres,
                                   float shootingRating) const;
};

namespace GameXg
{
/** Where the tables live: assets/config/game_xg/ (the source checkout in
 * development, so a sweep's result can be committed). */
[[nodiscard]] std::filesystem::path folder();
/** A layer's file for a goal kind: "empty_goal_full.json". */
[[nodiscard]] std::filesystem::path file(std::string_view layer, GoalKind goal);
/** The saved table for a layer and goal kind, loaded once and kept (null
 * if there is none). Safe from several threads. */
[[nodiscard]] const GameXgTable* table(std::string_view layer, GoalKind goal);
/** Forgets the loaded tables (after a sweep saves a new one). */
void reload();

/** The share (0-1) of a goal's mouth, as seen from `from`, that a keeper at
 * `keeper` leaves uncovered: his cover is `coverMetres` either side of him,
 * as an angle from the shooter. `goalLine` and `goalMiddle` place the goal
 * (x of its line, y of its middle); `halfWidth` its half-width. */
[[nodiscard]] float openShare(PitchPoint from, PitchPoint keeper,
                              float goalLine, float goalMiddle, float halfWidth,
                              float coverMetres);
}  // namespace GameXg
