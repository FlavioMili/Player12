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

/** A measured spot for smoothing: where (metres out from the goal line,
 * off its middle), the goal's mouth angle seen from there, its shots and its
 * goals. */
struct SmoothingDatum
{
  float x = 0.0F;
  float y = 0.0F;
  float angle = 0.0F;
  double shots = 0.0;
  double goals = 0.0;
};

/** The angle (radians) a goal `halfWidth` either side of its middle shows
 * from `line` metres out and `side` metres off its middle: 0 from the line
 * outside the posts, pi from the line between them. */
[[nodiscard]] float mouthAngle(float line, float side, float halfWidth);

/** The smoothed chance at (x, y) metres from `data` (game xG plan section
 * 8): goals over shots, pooled under a Gaussian kernel over the pitch (width
 * 0.75 of the distance to the 4th-nearest spot, the local spacing, at least
 * 0.5 m) times one over the mouth angle (width 10 degrees), so spots that see
 * the goal very differently (either side of a post on the goal line) don't
 * pool. */
[[nodiscard]] float smoothedAt(const std::vector<SmoothingDatum>& data, float x,
                               float y, float halfWidth);

/** A band of spots: every `step` metres out to `lineTo` metres from the goal
 * line and `sideMax` either side of its middle. */
struct GameXgBand
{
  float lineTo = 0.0F;
  float sideMax = 0.0F;
  float step = 1.0F;
};

/**
 * The game xG sweep, layer 1 (game xG plan, section 8): the Shot drill run
 * at an empty goal over spots and Shooting ratings, then smoothed into a
 * GameXgTable.
 *
 * 1. Measure: spots in fixed bands, finest near the goal (each band's step
 *    a multiple of the finest), on both sides of the goal.
 * 2. Symmetry: left and right compared spot by spot; if they agree within
 *    noise they are folded together (twice the shots per spot).
 * 3. Smooth: goals over shots pooled under a Gaussian kernel whose width
 *    follows the spacing, onto a regular grid.
 *
 * The numbers below are our own choices (plan section 8), not research.
 */
struct GameXgSweepSpec
{
  GoalKind goal = GoalKind::FULL;
  std::vector<float> shooting{1.0F, 25.0F, 50.0F, 75.0F, 100.0F};
  /** The first row: the goal line itself (the ball touching it), the
   * baseline for the narrowest angles. */
  float lineFrom = 0.0F;
  /** Finest first: 1 m in and around the box, 2 m to 30 m, 4 m beyond. The
   * last band is the sweep's extent: the match pitch's attacking half (to
   * halfway, 52.5 m out) and as near the touchlines as the drill places the
   * ball, so the map has no flat strips beyond what was measured. */
  std::vector<GameXgBand> bands{
      {.lineTo = 18.0F, .sideMax = 20.0F, .step = 1.0F},
      {.lineTo = 30.0F, .sideMax = 28.0F, .step = 2.0F},
      {.lineTo = 52.0F, .sideMax = 32.0F, .step = 4.0F}};
  int repeats = 60;
  std::uint32_t firstSeed = 1;
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
  /** What it is doing ("measuring", "done"...). */
  [[nodiscard]] std::string phase() const;
  [[nodiscard]] long long runsDone() const { return finished; }
  [[nodiscard]] long long runsQueued() const;
  [[nodiscard]] double seconds() const { return elapsed; }
  /** Set once measured: whether left and right were folded. */
  [[nodiscard]] std::optional<bool> symmetric() const { return mirror; }
  /** Share of mirrored pairs that differed, and how many pairs. */
  [[nodiscard]] double asymmetricShare() const { return asymmetry; }
  /** The pooled left-minus-right difference over all pairs, in standard
   * errors (+: the positive side, towards the bottom touchline, scores
   * more). */
  [[nodiscard]] double asymmetricZ() const { return pooledZ; }
  /** The result, once done. */
  [[nodiscard]] const GameXgTable& table() const { return result; }

  /** A spot's progress: every Shooting level still to come, some measured,
   * or all measured. */
  enum class SpotState : std::uint8_t
  {
    WAITING,
    MEASURING,
    MEASURED
  };
  /** A spot (metres out from the goal line, off its middle) and its
   * progress, for drawing the sweep as it runs. */
  struct SweepSpot
  {
    float line = 0.0F;
    float side = 0.0F;
    SpotState state = SpotState::WAITING;
  };
  /** Every spot the sweep measures, in the order it measures them. */
  [[nodiscard]] std::vector<SweepSpot> spots() const;

 private:
  enum class Stage : std::uint8_t
  {
    MEASURE,
    DONE
  };
  /** A spot on the finest band's lattice and a Shooting level. */
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
  /** Compares left and right, and folds them if they agree. */
  void testSymmetry();
  /** Half the measured goal's width (m), for its mouth angle. */
  [[nodiscard]] float halfWidth() const;
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
  /** The lattice: its spacing (the finest band's step, m) and extent. */
  float fine = 1.0F;
  int lineCells = 0;
  int sideCells = 0;
  Stage stage = Stage::MEASURE;
  std::vector<Point> points;
  std::map<Key, std::size_t> index;
  std::size_t nextPoint = 0;
  int nextRepeat = 0;
  long long finished = 0;
  double elapsed = 0.0;
  std::optional<bool> mirror;
  double asymmetry = 0.0;
  double pooledZ = 0.0;
  bool halted = false;
  GameXgTable result;
  std::map<std::string, float> saved;
  std::uint32_t savedSeed = 1;
  bool restored = false;
};

/** The drill the sweep measures with (Still-ball shot), from a set of
 * drills; nullptr if it isn't there. */
[[nodiscard]] Drill* gameXgDrill(
    const std::vector<std::unique_ptr<Drill>>& drills);

/** fm_lab game-xg: the sweep headless; saves the table (see lab_main.cpp). */
int runGameXgCommand(const StatsConfig& config,
                     const std::vector<std::string>& arguments);
