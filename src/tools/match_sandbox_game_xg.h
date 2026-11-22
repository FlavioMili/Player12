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
#include <map>
#include <optional>
#include <string>
#include <string_view>
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
 * The game xG sweep (game xG plan, section 8): the Still-ball shot drill run
 * over spots and Shooting ratings, then smoothed into a GameXgTable.
 *
 * Layer 1, the empty goal: the chance of scoring with no keeper.
 * Layer 2, the keeper: the same against a keeper at his set position, over
 * Goalkeeping ratings too, stored as a factor on layer 1 (the keeper's
 * chance over the empty goal's), so it needs layer 1's map for the goal.
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
  /** GameXgTable::EMPTY_GOAL or GameXgTable::KEEPER. */
  std::string layer{GameXgTable::EMPTY_GOAL};
  GoalKind goal = GoalKind::FULL;
  std::vector<float> shooting{1.0F, 25.0F, 50.0F, 75.0F, 100.0F};
  /** The keeper layer's Goalkeeping ratings (ignored for the empty goal). */
  std::vector<float> goalkeeping{1.0F, 50.0F, 100.0F};
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
  /** Why there is no result (the keeper layer without an empty-goal map);
   * empty if none. */
  [[nodiscard]] const std::string& problem() const { return trouble; }
  [[nodiscard]] bool keeperLayer() const
  {
    return setup.layer == GameXgTable::KEEPER;
  }

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
  /** A spot on the finest band's lattice and a level: a Shooting rating,
   * or for the keeper layer a Goalkeeping and Shooting pair (Goalkeeping
   * outer, as the table stores them). */
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
  /** How many levels each spot is measured at, and a level's ratings. */
  [[nodiscard]] std::size_t levels() const;
  [[nodiscard]] float shootingOf(std::size_t level) const;
  [[nodiscard]] float goalkeepingOf(std::size_t level) const;
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
  std::string trouble;
  /** The keeper layer: the empty-goal map its factor divides by. */
  std::optional<GameXgTable> emptyGoal;
  std::map<std::string, float> saved;
  std::uint32_t savedSeed = 1;
  bool restored = false;
};

/**
 * Game xG layer 3's leg chance (game xG plan, step 3): the Shot block drill
 * with one body near the shot's line, over his distance from the shooter,
 * how far the line passes from him, his blocking rating (Defending and
 * Tackling together), how he moves and whose he is. The chance a shot
 * through his leg-space meets a leg: leg meetings over the runs whose ball
 * didn't meet his torso or head. The offset is how far his path (where he
 * would have been had he not reacted) ran from the ball's actual path, not
 * from the line aimed along: aims run over the offsets and each run counts
 * at the offset nearest its measured one. The shooter's own error is
 * measured apart (the scatter phase: Shooting levels, the defender well off
 * the line) for the estimate to spread its aim lines by. The shooter
 * stands GAP metres further out than him, aiming low, so the ball is low
 * as it reaches him. Team-mates (no leg put out, so no rating) are measured at
 * the middle rating and stored at every one.
 */
struct GameXgLegSweepSpec
{
  std::vector<float> distances{1.0F, 2.0F,  3.0F,  4.0F,  5.0F,  6.0F,
                               8.0F, 10.0F, 12.0F, 15.0F, 20.0F, 25.0F};
  std::vector<float> offsets{0.0F, 0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F,
                             0.8F, 0.9F, 1.0F, 1.1F, 1.2F, 1.3F, 1.4F, 1.5F};
  std::vector<float> ratings{1.0F, 50.0F, 100.0F};
  int repeats = 60;
  std::uint32_t firstSeed = 1;
  /** The shooter's distance behind him (m), the aim's height (m), the
   * shot's power share and Shooting: my choices (see above). */
  float gap = 3.0F;
  float aimHeight = 0.2F;
  float power = 0.9F;
  /** A sprinter's run-up (m): long enough to be at full speed as it is
   * struck, so his speed then carries him on (a sidestepper is at his pace
   * within the drill's own). */
  float sprintRunUp = 25.0F;
  /** Fewest shots a cell's chance rests on: a thinner one borrows its
   * neighbouring distances' (my choice). */
  int minimumShots = 10;
  float shooting = 50.0F;
  /** The scatter phase: Shooting levels, shots at each, the shooter's
   * distance out from the defender (m), the defender's distance off the
   * line (m, out of the way) and the aim's height (m): my choices. */
  std::vector<float> scatterShooting{1.0F, 50.0F, 100.0F};
  int scatterRepeats = 300;
  float scatterDistance = 10.0F;
  float scatterOffLine = 4.0F;
  float scatterAimHeight = 1.0F;
};

class GameXgLegSweep
{
 public:
  GameXgLegSweep(Drill& blockDrill, const StatsConfig& config,
                 GameXgLegSweepSpec spec);
  ~GameXgLegSweep();
  GameXgLegSweep(const GameXgLegSweep&) = delete;
  GameXgLegSweep& operator=(const GameXgLegSweep&) = delete;

  /** Runs for about `budgetSeconds`; false once done. */
  bool work(double budgetSeconds);
  void stop();
  [[nodiscard]] bool done() const { return finishedAll; }
  [[nodiscard]] bool stopped() const { return halted; }
  [[nodiscard]] long long runsDone() const { return finished; }
  [[nodiscard]] long long runsQueued() const;
  [[nodiscard]] double seconds() const { return elapsed; }
  [[nodiscard]] const GameXgLegTable& table() const { return result; }
  [[nodiscard]] const GameXgLegSweepSpec& spec() const { return setup; }

  /** What came of a run, for the view. */
  enum class RunResult : std::uint8_t
  {
    LEG,       /*!< Met a leg: a block, counted. */
    PAST_LOW,  /*!< Past him below his hip: counted, no block. */
    TORSO,     /*!< Met his torso or head: not leg-space. */
    PAST_HIGH, /*!< Past him at his hip or over: not leg-space. */
    OFF_GRID,  /*!< Its path ran wider than the last offset. */
    SCATTER,   /*!< A shooter's-error run (nobody in the way). */
  };
  /** One run as the sandbox's scan view draws it, in the shot's frame:
   * along its path from where it left, across it (+ to its left). */
  struct RunView
  {
    std::size_t kind = 0;
    /** Defending and Tackling; Shooting for a scatter run. */
    float rating = 0.0F;
    /** The set distance out and off the line (m). */
    float distance = 0.0F;
    float offset = 0.0F;
    /** Him as it was struck, and his velocity then. */
    float himAlong = 0.0F;
    float himAcross = 0.0F;
    float paceAlong = 0.0F;
    float paceAcross = 0.0F;
    /** Where it crosses the goal line, and the spot aimed at. */
    float goalAlong = 0.0F;
    float aimAlong = 0.0F;
    float aimAcross = 0.0F;
    /** Where he'd have been as it passed him (no reaction). */
    float projectedAlong = 0.0F;
    float projectedAcross = 0.0F;
    /** As it passed level with him: its gap from where he'd have been
     * (m), and its height (m); below 0 if it never did. */
    float gap = -1.0F;
    float height = -1.0F;
    /** His hip's height (m). */
    float hip = 0.0F;
    RunResult result = RunResult::OFF_GRID;
    /** The distance index, and the offset index it counted at. */
    std::size_t distanceIndex = 0;
    std::optional<std::size_t> bin;
  };
  /** The latest runs, newest last (a few dozen). */
  [[nodiscard]] const std::deque<RunView>& recentRuns() const { return recent; }
  /** The runs through his leg-space so far and the leg meetings among
   * them, at a kind, rating index, distance index and offset index. */
  [[nodiscard]] std::pair<int, int> tally(std::size_t kind, std::size_t rating,
                                          std::size_t distance,
                                          std::size_t offset) const;
  /** The kind and rating index being measured now (or last). */
  [[nodiscard]] std::size_t currentKind() const;
  [[nodiscard]] std::size_t currentRating() const;
  [[nodiscard]] bool scattering() const;

 private:
  /** One cell of runs: a kind, a rating index, a distance and the aimed
   * offset's index; or (scatter) a Shooting level's index in `rating`. */
  struct Cell
  {
    std::size_t kind = 0;
    std::size_t rating = 0;
    std::size_t distance = 0;
    std::size_t offset = 0;
    bool scatter = false;
  };
  void runOne();
  void finish();
  void restore();
  [[nodiscard]] int repeatsOf(const Cell& cell) const;
  /** The drill's settings for a run of `cell`. */
  [[nodiscard]] std::map<std::string, float> runValues(const Cell& cell) const;
  /** The offset index nearest a measured gap (m); none off the grid. */
  [[nodiscard]] std::optional<std::size_t> offsetBin(double gap) const;
  /** Keeps a run for the view (his hip's height in m). */
  void noteView(const Cell& cell, const std::vector<DrillMetric>& metrics,
                double hip, RunResult outcome, std::optional<std::size_t> bin);
  /** How many runs the view keeps. */
  static constexpr std::size_t RECENT_RUNS = 40;
  /** Counts a run's result (his height in cm, for his hip). */
  void tallyRun(const Cell& cell, const std::vector<DrillMetric>& metrics,
                float heightCentimetres);
  /** A measured cell's chance, a thin one borrowing nearby distances'. */
  [[nodiscard]] float pooledChance(std::size_t kind, std::size_t rating,
                                   std::size_t distance,
                                   std::size_t offset) const;
  /** The table's shooter's-error points from the scatter runs. */
  void fillScatter(GameXgLegTable& table) const;
  [[nodiscard]] std::size_t binIndex(std::size_t kind, std::size_t rating,
                                     std::size_t distance,
                                     std::size_t offset) const;

  Drill& drill;
  const StatsConfig& config;
  GameXgLegSweepSpec setup;
  std::vector<Cell> cells;
  /** Runs through his leg-space and leg meetings, by kind, rating, distance
   * and measured offset (the table's order). */
  std::vector<int> binShots;
  std::vector<int> binBlocks;
  /** The shooter's errors (rad) per Shooting level: over the ground
   * (unsigned) and upward (+ higher). */
  std::vector<std::vector<float>> acrossErrors;
  std::vector<std::vector<float>> upErrors;
  /** Runs started before the current cell (for seeds). */
  std::size_t runsBefore = 0;
  std::size_t nextCell = 0;
  int nextRepeat = 0;
  long long finished = 0;
  double elapsed = 0.0;
  bool finishedAll = false;
  bool halted = false;
  GameXgLegTable result;
  std::map<std::string, float> saved;
  std::uint32_t savedSeed = 1;
  bool restored = false;
  std::deque<RunView> recent;
};

/** The drill the leg sweep measures with (Shot block); nullptr if absent. */
[[nodiscard]] Drill* gameXgLegDrill(
    const std::vector<std::unique_ptr<Drill>>& drills);

/** A layer's default sweep (GameXgTable::EMPTY_GOAL or KEEPER). */
[[nodiscard]] GameXgSweepSpec gameXgSpec(std::string_view layer);

/** The drill the sweep measures with (Still-ball shot), from a set of
 * drills; nullptr if it isn't there. */
[[nodiscard]] Drill* gameXgDrill(
    const std::vector<std::unique_ptr<Drill>>& drills);

/** fm_lab game-xg: the sweep headless; saves the table (see lab_main.cpp). */
int runGameXgCommand(const StatsConfig& config,
                     const std::vector<std::string>& arguments);
