// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "model/match_game_xg.h"
#include "tools/match_sandbox_drill.h"

struct StatsConfig;

/** A measured spot for smoothing: where, its logit and its precision. */
struct SmoothingDatum
{
  float x = 0.0F;
  float y = 0.0F;
  double logit = 0.0;
  double weight = 0.0;
};

/** The smoothed chance at (x, y) metres from `data` (game xG plan section
 * 8: a Gaussian kernel in logit space, bandwidth from the 4th-nearest spot,
 * at least 0.5 m). */
[[nodiscard]] float smoothedAt(const std::vector<SmoothingDatum>& data, float x,
                               float y);

/**
 * The game xG sweep, layer 1 (game xG plan, section 8): the Shot drill run
 * at an empty goal over spots and Shooting ratings, then smoothed into a
 * GameXgTable.
 *
 * 1. Coarse: spots every `coarseStep` metres on both sides of the goal.
 * 2. Symmetry: left and right compared spot by spot. If they agree within
 *    noise they are folded together and the rest is measured on one side.
 * 3. Refine: a cell whose corners differ by more than `refineDifference`
 *    (at any Shooting) is split in half, for `refinePasses` passes.
 * 4. Smooth: a Gaussian kernel in logit space onto a regular grid.
 *
 * The numbers below are our own choices (plan section 8), not research.
 */
struct GameXgSweepSpec
{
  GoalKind goal = GoalKind::FULL;
  std::vector<float> shooting{1.0F, 25.0F, 50.0F, 75.0F, 100.0F};
  float lineFrom = 1.0F;
  float lineTo = 41.0F;
  float sideMax = 28.0F;
  float coarseStep = 4.0F;
  int refinePasses = 2;
  int repeats = 200;
  std::uint32_t firstSeed = 1;
  /** A cell is split if its corners differ by more than this. */
  double refineDifference = 0.08;
  /** Left and right count as mirror images if no more than this share of
   * mirrored pairs differ at |z| > 1.96. */
  double asymmetricShare = 0.10;
  /** The smoothed grid's spacing (m). */
  float gridStep = 0.5F;
};

class GameXgSweep
{
 public:
  GameXgSweep(Drill& shotDrill, const StatsConfig& config,
              GameXgSweepSpec spec);
  ~GameXgSweep();
  GameXgSweep(const GameXgSweep&) = delete;
  GameXgSweep& operator=(const GameXgSweep&) = delete;

  /** Runs for about `budgetSeconds`; false once the sweep is done. */
  bool work(double budgetSeconds);
  /** Stops early: no table. */
  void stop();
  [[nodiscard]] bool done() const { return stage == Stage::DONE; }
  [[nodiscard]] bool stopped() const { return halted; }
  [[nodiscard]] const GameXgSweepSpec& spec() const { return setup; }
  /** What it is doing ("coarse grid", "refining (pass 1)"...). */
  [[nodiscard]] std::string phase() const;
  [[nodiscard]] long long runsDone() const { return finished; }
  /** Runs queued so far (refinement adds more). */
  [[nodiscard]] long long runsQueued() const;
  [[nodiscard]] double seconds() const { return elapsed; }
  /** Set after the coarse grid: whether left and right were folded. */
  [[nodiscard]] std::optional<bool> symmetric() const { return mirror; }
  /** Share of mirrored pairs that differed, and how many pairs. */
  [[nodiscard]] double asymmetricShare() const { return asymmetry; }
  /** The result, once done. */
  [[nodiscard]] const GameXgTable& table() const { return result; }

 private:
  enum class Stage : std::uint8_t
  {
    COARSE,
    REFINE,
    DONE
  };
  /** A spot on the fine lattice (1 m at the defaults) and a Shooting level. */
  struct Point
  {
    int line = 0;
    int side = 0;
    std::size_t level = 0;
    int shots = 0;
    int goals = 0;
  };
  using Key = std::tuple<int, int, std::size_t>;

  [[nodiscard]] float lineMetres(int line) const;
  [[nodiscard]] float sideMetres(int side) const;
  void queue(int line, int side);
  void runOne();
  void afterCoarse();
  /** Queues the next refinement pass; false if it added nothing. */
  bool refine();
  /** Whether a cell (corner at `line`, `side`, `cell` lattice steps a
   * side) changes enough to split: at any Shooting, its corners differ by
   * more than the threshold and by more than their noise. */
  [[nodiscard]] bool cellChanges(int line, int side, int cell) const;
  /** One Shooting level's measured spots, ready to smooth. */
  [[nodiscard]] std::vector<SmoothingDatum> levelData(std::size_t level) const;
  /** Measured shots and goals at a lattice spot (folded if mirrored). */
  [[nodiscard]] std::optional<std::pair<int, int>> folded(
      int line, int side, std::size_t level) const;
  void finish();
  void restore();

  Drill& drill;
  const StatsConfig& config;
  GameXgSweepSpec setup;
  /** Fine lattice cells per coarse step, and the lattice's extent. */
  int coarseCells = 1;
  int lineCells = 0;
  int sideCells = 0;
  Stage stage = Stage::COARSE;
  int pass = 0;
  std::vector<Point> points;
  std::map<Key, std::size_t> index;
  std::size_t nextPoint = 0;
  int nextRepeat = 0;
  long long finished = 0;
  double elapsed = 0.0;
  std::optional<bool> mirror;
  double asymmetry = 0.0;
  bool halted = false;
  GameXgTable result;
  std::map<std::string, float> saved;
  std::uint32_t savedSeed = 1;
  bool restored = false;
};

/** fm_lab game-xg: the sweep headless; saves the table (see lab_main.cpp). */
int runGameXgCommand(const StatsConfig& config,
                     const std::vector<std::string>& arguments);
