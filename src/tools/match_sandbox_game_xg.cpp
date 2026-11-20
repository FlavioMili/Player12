// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_game_xg.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <ctime>
#include <format>
#include <iostream>
#include <numbers>
#include <string_view>

namespace
{
using Clock = std::chrono::steady_clock;

// The Still-ball shot drill's settings the sweep holds or moves.
constexpr const char* LINE = "Out from goal line (m)";
constexpr const char* SIDE = "Off centre (m)";
constexpr const char* GOALS = "Goals";
constexpr const char* SHOOTING = "Shooting";
constexpr const char* KEEPER = "Keeper";
constexpr const char* GOALKEEPING = "Goalkeeping";
/** Where the keeper starts: 0, his set position. */
constexpr const char* KEEPER_STARTS = "Keeper starts";
/** The shooter's foot: right (0), as most players; the left foot's map is
 * this one mirrored. */
constexpr const char* FOOT = "Foot";
/** The keeper factor's floor under the empty goal's chance: where the empty
 * goal is nearly never scored (the narrowest angles), the factor isn't
 * noise over nothing (my choice). */
constexpr float MIN_EMPTY_CHANCE = 0.02F;
/** A flag metric (shot, goal) counts as set above half. */
constexpr double FLAG_SET = 0.5;
/** Two-sided 95%: a mirrored pair differs beyond chance past this. */
constexpr double Z_95 = 1.96;
/** Smoothing: the bandwidth is this share of the distance to the 4th-nearest
 * measured spot (on a square lattice, the spacing), never under the floor
 * (m). */
constexpr std::size_t BANDWIDTH_NEIGHBOUR = 4;
constexpr float BANDWIDTH_SHARE = 0.75F;
constexpr float MIN_BANDWIDTH_METRES = 0.5F;
/** Smoothing: the width over the goal's mouth angle (radians; 10 degrees, my
 * estimate): far less than the jump either side of a post on the goal line
 * (0 to 180 degrees), far more than the angle's change between neighbouring
 * spots out in the field. */
constexpr double ANGLE_BANDWIDTH_RADIANS = 10.0 * std::numbers::pi / 180.0;
constexpr std::size_t TIMESTAMP_BYTES = 32;

std::string timestamp()
{
  const std::time_t now = std::time(nullptr);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  std::array<char, TIMESTAMP_BYTES> text{};
  std::strftime(text.data(), text.size(), "%Y-%m-%d %H%M%S", &local);
  return text.data();
}

double metricOf(const std::vector<DrillMetric>& metrics, std::string_view name)
{
  for (const DrillMetric& metric : metrics)
  {
    if (metric.name == name)
    {
      return metric.value;
    }
  }
  return 0.0;
}

}  // namespace

GameXgSweep::GameXgSweep(Drill& shotDrill, const StatsConfig& config_ref,
                         GameXgSweepSpec spec)
    : drill(shotDrill),
      config(config_ref),
      setup(std::move(spec)),
      savedSeed(shotDrill.seed)
{
  for (const DrillParameter& parameter : drill.parameters())
  {
    if (parameter.value != nullptr)
    {
      saved[parameter.name] = *parameter.value;
    }
  }
  setup.repeats = std::max(1, setup.repeats);
  if (setup.shooting.empty())
  {
    setup.shooting = GameXgSweepSpec{}.shooting;
  }
  if (setup.bands.empty())
  {
    setup.bands = GameXgSweepSpec{}.bands;
  }
  std::ranges::sort(setup.shooting);
  if (keeperLayer())
  {
    if (setup.goalkeeping.empty())
    {
      setup.goalkeeping = GameXgSweepSpec{}.goalkeeping;
    }
    std::ranges::sort(setup.goalkeeping);
    // A copy, so a re-run of the empty goal meanwhile doesn't move it.
    if (const GameXgTable* empty =
            GameXg::table(GameXgTable::EMPTY_GOAL, setup.goal))
    {
      emptyGoal = *empty;
    }
    else
    {
      trouble = std::format(
          "the keeper layer needs the empty goal's map for the {} goal: "
          "sweep that first",
          goalKindName(setup.goal));
      halted = true;
      stage = Stage::DONE;
      return;
    }
  }
  else
  {
    setup.goalkeeping.clear();
  }
  // The lattice: the finest band's step; each band takes every n-th spot of
  // it inside its own box (n = its step over the finest), both sides.
  fine = setup.bands.front().step;
  for (const GameXgBand& band : setup.bands)
  {
    fine = std::min(fine, band.step);
  }
  const GameXgBand& outer = setup.bands.back();
  lineCells =
      static_cast<int>(std::lround((outer.lineTo - setup.lineFrom) / fine));
  sideCells = static_cast<int>(std::lround(outer.sideMax / fine));
  for (const GameXgBand& band : setup.bands)
  {
    const int every =
        std::max(1, static_cast<int>(std::lround(band.step / fine)));
    const int lines = static_cast<int>(
        std::floor((band.lineTo - setup.lineFrom) / fine + 1e-3F));
    const int sides = static_cast<int>(std::floor(band.sideMax / fine + 1e-3F));
    for (int line = 0; line <= std::min(lines, lineCells); line += every)
    {
      for (int side = -(sides / every) * every; side <= sides; side += every)
      {
        queue(line, side);
      }
    }
  }
}

GameXgSweep::~GameXgSweep() { restore(); }

void GameXgSweep::restore()
{
  if (restored)
  {
    return;
  }
  restored = true;
  for (const DrillParameter& parameter : drill.parameters())
  {
    if ((parameter.value != nullptr) && saved.contains(parameter.name))
    {
      *parameter.value = saved.at(parameter.name);
    }
  }
  drill.seed = savedSeed;
}

float GameXgSweep::lineMetres(int line) const
{
  return setup.lineFrom + (static_cast<float>(line) * fine);
}

float GameXgSweep::sideMetres(int side) const
{
  return static_cast<float>(side) * fine;
}

std::size_t GameXgSweep::levels() const
{
  return std::max<std::size_t>(1, setup.goalkeeping.size()) *
         setup.shooting.size();
}

float GameXgSweep::shootingOf(std::size_t level) const
{
  return setup.shooting[level % setup.shooting.size()];
}

float GameXgSweep::goalkeepingOf(std::size_t level) const
{
  return setup.goalkeeping.empty()
             ? 0.0F
             : setup.goalkeeping[level / setup.shooting.size()];
}

void GameXgSweep::queue(int line, int side)
{
  for (std::size_t level = 0; level < levels(); ++level)
  {
    const Key key{line, side, level};
    if (!index.contains(key))
    {
      index.emplace(key, points.size());
      points.push_back({.line = line, .side = side, .level = level});
    }
  }
}

std::vector<GameXgSweep::SweepSpot> GameXgSweep::spots() const
{
  // A spot's levels are queued together, so they sit side by side in
  // `points`: measured before nextPoint, waiting after it.
  std::vector<SweepSpot> out;
  for (std::size_t at = 0; at < points.size(); at += levels())
  {
    const std::size_t last = at + levels() - 1;
    SpotState state = SpotState::MEASURING;
    if ((stage == Stage::DONE && !halted) || last < nextPoint)
    {
      state = SpotState::MEASURED;
    }
    else if (at > nextPoint || (at == nextPoint && nextRepeat == 0))
    {
      state = SpotState::WAITING;
    }
    out.push_back({.line = lineMetres(points[at].line),
                   .side = sideMetres(points[at].side),
                   .state = state});
  }
  return out;
}

long long GameXgSweep::runsQueued() const
{
  return static_cast<long long>(points.size()) * setup.repeats;
}

std::string GameXgSweep::phase() const
{
  switch (stage)
  {
    case Stage::MEASURE:
      return std::format("measuring {} spots, both sides",
                         points.size() / levels());
    case Stage::DONE:
      if (!trouble.empty())
      {
        return trouble;
      }
      return halted ? "stopped" : "done";
  }
  return "";
}

void GameXgSweep::runOne()
{
  Point& point = points[nextPoint];
  restored = false;
  std::map<std::string, float> values = saved;
  values[LINE] = lineMetres(point.line);
  values[SIDE] = sideMetres(point.side);
  values[GOALS] = static_cast<float>(static_cast<int>(setup.goal) - 1);
  values[SHOOTING] = shootingOf(point.level);
  values[KEEPER] = keeperLayer() ? 1.0F : 0.0F;
  if (keeperLayer())
  {
    values[GOALKEEPING] = goalkeepingOf(point.level);
    values[KEEPER_STARTS] = 0.0F;
  }
  values[FOOT] = 0.0F;
  for (const DrillParameter& parameter : drill.parameters())
  {
    if ((parameter.value != nullptr) && values.contains(parameter.name))
    {
      *parameter.value = values.at(parameter.name);
    }
  }
  // A seed of its own for every run, so neighbouring spots don't share luck.
  drill.seed = setup.firstSeed +
               static_cast<std::uint32_t>(
                   (nextPoint * static_cast<std::size_t>(setup.repeats)) +
                   static_cast<std::size_t>(nextRepeat));
  std::vector<DrillMetric> metrics;
  {
    DrillRun run(drill, config, false);
    while (run.step())
    {
    }
    metrics = drill.metrics();
  }
  if (metricOf(metrics, "Shot") > FLAG_SET)
  {
    ++point.shots;
    if (metricOf(metrics, "Goal") > FLAG_SET)
    {
      ++point.goals;
    }
  }
  ++finished;
  if (++nextRepeat >= setup.repeats)
  {
    nextRepeat = 0;
    ++nextPoint;
  }
}

bool GameXgSweep::work(double budgetSeconds)
{
  const auto start = Clock::now();
  while (stage != Stage::DONE)
  {
    if (nextPoint < points.size())
    {
      runOne();
    }
    else
    {
      testSymmetry();
      finish();
    }
    if (std::chrono::duration<double>(Clock::now() - start).count() >=
        budgetSeconds)
    {
      break;
    }
  }
  elapsed += std::chrono::duration<double>(Clock::now() - start).count();
  return stage != Stage::DONE;
}

void GameXgSweep::stop()
{
  halted = true;
  stage = Stage::DONE;
  restore();
}

void GameXgSweep::testSymmetry()
{
  int pairs = 0;
  int differ = 0;
  double signedSum = 0.0;
  double varianceSum = 0.0;
  for (const Point& point : points)
  {
    if (point.side <= 0)
    {
      continue;
    }
    const auto other = index.find(Key{point.line, -point.side, point.level});
    if (other == index.end())
    {
      continue;
    }
    const Point& mirrored = points[other->second];
    const int shots = point.shots + mirrored.shots;
    if (point.shots == 0 || mirrored.shots == 0)
    {
      continue;
    }
    const double pooled =
        static_cast<double>(point.goals + mirrored.goals) / shots;
    if (pooled <= 0.0 || pooled >= 1.0)
    {
      continue;
    }
    const double spread =
        std::sqrt(pooled * (1.0 - pooled) *
                  ((1.0 / point.shots) + (1.0 / mirrored.shots)));
    const double z = ((static_cast<double>(point.goals) / point.shots) -
                      (static_cast<double>(mirrored.goals) / mirrored.shots)) /
                     spread;
    ++pairs;
    if (std::abs(z) > Z_95)
    {
      ++differ;
    }
    // The pooled test: each pair's difference weighted by its precision
    // (1 / (1/n1 + 1/n2)), summed with its variance.
    const double weight = 1.0 / ((1.0 / point.shots) + (1.0 / mirrored.shots));
    signedSum += weight * z * spread;
    varianceSum += weight * pooled * (1.0 - pooled);
  }
  asymmetry = pairs > 0 ? static_cast<double>(differ) / pairs : 0.0;
  pooledZ = varianceSum > 0.0 ? signedSum / std::sqrt(varianceSum) : 0.0;
  // Folded only if neither test finds a difference: the pair-by-pair share
  // (patchy differences) nor the pooled one (a steady one-sided lean,
  // which single pairs miss at a few dozen shots).
  mirror = asymmetry <= setup.asymmetricShare && std::abs(pooledZ) <= Z_95;
}

std::optional<std::pair<int, int>> GameXgSweep::folded(int line, int side,
                                                       std::size_t level) const
{
  std::optional<std::pair<int, int>> out;
  const auto add = [&](int at)
  {
    if (const auto found = index.find(Key{line, at, level});
        found != index.end())
    {
      const Point& point = points[found->second];
      if (!out)
      {
        out = std::pair{0, 0};
      }
      out->first += point.shots;
      out->second += point.goals;
    }
  };
  add(side);
  if (mirror.value_or(false) && side != 0)
  {
    add(-side);
  }
  return out;
}

void GameXgSweep::finish()
{
  stage = Stage::DONE;
  restore();
  const bool folding = mirror.value_or(false);
  GameXgTable& table = result;
  table = GameXgTable{};
  table.layer = setup.layer;
  table.goal = setup.goal;
  table.measured = timestamp();
  table.repeats = setup.repeats;
  table.firstSeed = setup.firstSeed;
  table.mirrored = folding;
  table.shooting = setup.shooting;
  table.goalkeeping = setup.goalkeeping;
  const float step = setup.gridStep;
  table.line = {.from = setup.lineFrom,
                .step = step,
                .count = static_cast<int>(std::lround(
                             (lineMetres(lineCells) - setup.lineFrom) / step)) +
                         1};
  const float sideMax = sideMetres(sideCells);
  const int sideSteps = static_cast<int>(std::lround(sideMax / step));
  table.side = folding ? GameXgTable::Axis{.from = 0.0F,
                                           .step = step,
                                           .count = sideSteps + 1}
                       : GameXgTable::Axis{.from = -sideMax,
                                           .step = step,
                                           .count = (2 * sideSteps) + 1};
  table.samples.clear();
  for (const Point& point : points)
  {
    table.samples.push_back({.line = lineMetres(point.line),
                             .side = sideMetres(point.side),
                             .shooting = shootingOf(point.level),
                             .goalkeeping = goalkeepingOf(point.level),
                             .shots = point.shots,
                             .goals = point.goals});
  }

  // Smoothing, per level, onto the regular grid. The keeper layer stores
  // its smoothed chance over the empty goal's there: the keeper's factor.
  table.values.clear();
  table.values.reserve(levels() * static_cast<std::size_t>(table.line.count) *
                       static_cast<std::size_t>(table.side.count));
  for (std::size_t level = 0; level < levels(); ++level)
  {
    const std::vector<SmoothingDatum> data = levelData(level);
    for (int li = 0; li < table.line.count; ++li)
    {
      for (int si = 0; si < table.side.count; ++si)
      {
        const float line = table.line.at(li);
        const float side = table.side.at(si);
        const float chance = smoothedAt(data, line, side, halfWidth());
        if (!emptyGoal)
        {
          table.values.push_back(chance);
          continue;
        }
        const float open = std::max(
            MIN_EMPTY_CHANCE, emptyGoal->value(line, side, shootingOf(level)));
        table.values.push_back(std::clamp(chance / open, 0.0F, 1.0F));
      }
    }
  }
}

std::vector<SmoothingDatum> GameXgSweep::levelData(std::size_t level) const
{
  // Each measured spot (folded if mirrored): where, shots and goals.
  const bool folding = mirror.value_or(false);
  std::vector<SmoothingDatum> data;
  for (const Point& point : points)
  {
    if (point.level != level || (folding && point.side < 0))
    {
      continue;
    }
    const auto measured = folded(point.line, point.side, level);
    if (!measured || measured->first == 0)
    {
      continue;
    }
    data.push_back({.x = lineMetres(point.line),
                    .y = sideMetres(point.side),
                    .angle = mouthAngle(lineMetres(point.line),
                                        sideMetres(point.side), halfWidth()),
                    .shots = static_cast<double>(measured->first),
                    .goals = static_cast<double>(measured->second)});
  }
  return data;
}

float GameXgSweep::halfWidth() const
{
  return PitchGoal::ofKind(setup.goal).halfWidth;
}

float mouthAngle(float line, float side, float halfWidth)
{
  // Between the lines to the two posts. On the goal line itself: the whole
  // half-turn between the posts, nothing outside them.
  if (line <= 0.0F)
  {
    return std::abs(side) < halfWidth ? std::numbers::pi_v<float> : 0.0F;
  }
  return std::abs(std::atan2(halfWidth - side, line) -
                  std::atan2(-halfWidth - side, line));
}

float smoothedAt(const std::vector<SmoothingDatum>& data, float x, float y,
                 float halfWidth)
{
  // Goals over shots, pooled under a Gaussian kernel: each spot counts by
  // its shots, whatever its rate (a spot near certainty isn't discounted).
  // The bandwidth follows the sampling: a share of the distance to the
  // 4th-nearest spot, so about 0.75 m on a 1 m band, 3 m on a 4 m one.
  if (data.empty())
  {
    return 0.0F;
  }
  std::vector<float> distances(data.size());
  for (std::size_t d = 0; d < data.size(); ++d)
  {
    const float dx = data[d].x - x;
    const float dy = data[d].y - y;
    distances[d] = (dx * dx) + (dy * dy);
  }
  std::vector<float> sorted = distances;
  const std::size_t nth = std::min(BANDWIDTH_NEIGHBOUR, sorted.size() - 1);
  std::ranges::nth_element(sorted,
                           sorted.begin() + static_cast<std::ptrdiff_t>(nth));
  const float bandwidth =
      std::max(MIN_BANDWIDTH_METRES, BANDWIDTH_SHARE * std::sqrt(sorted[nth]));
  const double twoSigmaSquared =
      2.0 * static_cast<double>(bandwidth) * bandwidth;
  // And by how differently each spot sees the goal: across a post on the
  // goal line the chance jumps, and the spots either side mustn't pool.
  const double angle = mouthAngle(x, y, halfWidth);
  constexpr double TWO_ANGLE_SIGMA_SQUARED =
      2.0 * ANGLE_BANDWIDTH_RADIANS * ANGLE_BANDWIDTH_RADIANS;
  double goals = 0.0;
  double shots = 0.0;
  double nearGoals = 0.0;
  double nearShots = 0.0;
  for (std::size_t d = 0; d < data.size(); ++d)
  {
    const double near = std::exp(-distances[d] / twoSigmaSquared);
    const double apart = static_cast<double>(data[d].angle) - angle;
    const double w =
        near * std::exp(-(apart * apart) / TWO_ANGLE_SIGMA_SQUARED);
    goals += w * data[d].goals;
    shots += w * data[d].shots;
    nearGoals += near * data[d].goals;
    nearShots += near * data[d].shots;
  }
  // No spot near enough in both (a point between rows where the angle
  // changes fastest): by distance alone.
  if (shots < 1.0)
  {
    return nearShots > 0.0 ? static_cast<float>(nearGoals / nearShots) : 0.0F;
  }
  return static_cast<float>(goals / shots);
}

// --- Command line ------------------------------------------------------------

namespace
{
std::optional<float> number(std::string_view text)
{
  float value = 0.0F;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc() || end != text.data() + text.size())
  {
    return std::nullopt;
  }
  return value;
}

std::optional<GoalKind> goalNamed(std::string_view name)
{
  for (int kind = 1; kind <= static_cast<int>(GoalKind::POP_UP); ++kind)
  {
    if (name == goalKindName(static_cast<GoalKind>(kind)))
    {
      return static_cast<GoalKind>(kind);
    }
  }
  return std::nullopt;
}

void printTable(const GameXgTable& table)
{
  constexpr std::array<float, 12> LINES{0,  2,  6,  11, 16, 20,
                                        25, 30, 35, 40, 45, 52};
  constexpr std::array<float, 9> HALF{0, 4, 8, 12, 16, 20, 24, 28, 32};
  constexpr std::array<float, 17> BOTH{-32, -28, -24, -20, -16, -12, -8, -4, 0,
                                       4,   8,   12,  16,  20,  24,  28, 32};
  const std::vector<float> SIDES =
      table.mirrored ? std::vector<float>(HALF.begin(), HALF.end())
                     : std::vector<float>(BOTH.begin(), BOTH.end());
  constexpr double PERCENT = 100.0;
  const std::vector<float> keepers =
      table.goalkeeping.empty() ? std::vector<float>{0.0F} : table.goalkeeping;
  for (const float goalkeeping : keepers)
  {
    for (const float shooting : table.shooting)
    {
      std::cout << std::format(
          "\nShooting {:g}{}: {} (%) by metres out (rows) and off centre\n",
          shooting,
          table.goalkeeping.empty()
              ? std::string()
              : std::format(", Goalkeeping {:g}", goalkeeping),
          table.goalkeeping.empty() ? "chance"
                                    : "keeper factor on the empty goal");
      std::string header = "        ";
      for (const float side : SIDES)
      {
        header += std::format("{:>6g}", side);
      }
      std::cout << header << '\n';
      for (const float line : LINES)
      {
        std::string row = std::format("{:>6g}  ", line);
        for (const float side : SIDES)
        {
          row += std::format(
              "{:>6.0f}",
              PERCENT * table.value(line, side, shooting, goalkeeping));
        }
        std::cout << row << '\n';
      }
    }
  }
}
/** fm_lab game-xg's options. */
struct GameXgOptions
{
  GameXgSweepSpec base;
  std::vector<GoalKind> goals{GoalKind::FULL};
  bool save = true;
  bool print = false;
  /** Saved here instead of the game's own file (a check's run); with more
   * than one goal, the goal's name is added before the extension. */
  std::string out;
};

std::optional<std::vector<GoalKind>> goalsNamed(const std::string& value)
{
  if (value != "all")
  {
    const auto kind = goalNamed(value);
    return kind ? std::optional(std::vector<GoalKind>{*kind}) : std::nullopt;
  }
  std::vector<GoalKind> goals;
  for (int kind = 1; kind <= static_cast<int>(GoalKind::POP_UP); ++kind)
  {
    goals.push_back(static_cast<GoalKind>(kind));
  }
  return goals;
}

std::optional<std::vector<float>> numbers(const std::string& value)
{
  std::vector<float> out;
  std::size_t start = 0;
  while (start <= value.size())
  {
    const std::size_t end = std::min(value.find(',', start), value.size());
    const auto parsed =
        number(std::string_view(value).substr(start, end - start));
    if (!parsed)
    {
      return std::nullopt;
    }
    out.push_back(*parsed);
    start = end + 1;
  }
  return out;
}

/** A numeric option onto the spec; false if it isn't one or doesn't parse. */
bool numericOption(const std::string& argument, const std::string& value,
                   GameXgSweepSpec& spec)
{
  const auto parsed = number(value);
  if (!parsed)
  {
    return false;
  }
  const float given = *parsed;
  if (argument == "--repeats")
  {
    spec.repeats = static_cast<int>(given);
  }
  else if (argument == "--seed")
  {
    spec.firstSeed = static_cast<std::uint32_t>(given);
  }
  else
  {
    return false;
  }
  return true;
}

/** "18:20:1,30:28:2,52:32:4": each band's line-to, side-max and step (m),
 * finest first. */
std::optional<std::vector<GameXgBand>> bandsNamed(std::string value)
{
  constexpr std::size_t PARTS = 3;
  std::ranges::replace(value, ':', ',');
  const auto parts = numbers(value);
  if (!parts || parts->empty() || parts->size() % PARTS != 0)
  {
    return std::nullopt;
  }
  std::vector<GameXgBand> bands;
  for (std::size_t at = 0; at < parts->size(); at += PARTS)
  {
    const GameXgBand band{.lineTo = (*parts)[at],
                          .sideMax = (*parts)[at + 1],
                          .step = (*parts)[at + 2]};
    if (band.step <= 0.0F || band.lineTo <= 0.0F || band.sideMax < 0.0F)
    {
      return std::nullopt;
    }
    bands.push_back(band);
  }
  return bands;
}

std::optional<GameXgOptions> parseOptions(
    const std::vector<std::string>& arguments, std::string& error)
{
  GameXgOptions options;
  bool shootingGiven = false;
  for (std::size_t index = 0; index < arguments.size(); ++index)
  {
    const std::string& argument = arguments[index];
    if (argument == "--no-save")
    {
      options.save = false;
      continue;
    }
    if (argument == "--print")
    {
      options.print = true;
      continue;
    }
    if (index + 1 >= arguments.size())
    {
      error = std::format("\"{}\" needs a value", argument);
      return std::nullopt;
    }
    const std::string& value = arguments[++index];
    if (argument == "--goal")
    {
      const auto goals = goalsNamed(value);
      if (!goals)
      {
        error =
            "--goal: full, 9v9, 7v7, futsal, 5-a-side, small, pop-up or all";
        return std::nullopt;
      }
      options.goals = *goals;
    }
    else if (argument == "--shooting")
    {
      const auto levels = numbers(value);
      if (!levels)
      {
        error = "--shooting expects numbers, e.g. 1,25,50,75,100";
        return std::nullopt;
      }
      options.base.shooting = *levels;
      shootingGiven = true;
    }
    else if (argument == "--goalkeeping")
    {
      const auto levels = numbers(value);
      if (!levels)
      {
        error = "--goalkeeping expects numbers, e.g. 1,50,100";
        return std::nullopt;
      }
      options.base.goalkeeping = *levels;
    }
    else if (argument == "--layer")
    {
      if (value == "empty")
      {
        options.base.layer = GameXgTable::EMPTY_GOAL;
      }
      else if (value == "keeper")
      {
        options.base.layer = GameXgTable::KEEPER;
        if (!shootingGiven)
        {
          options.base.shooting = gameXgSpec(GameXgTable::KEEPER).shooting;
        }
      }
      else
      {
        error = "--layer: empty or keeper";
        return std::nullopt;
      }
    }
    else if (argument == "--out")
    {
      options.out = value;
    }
    else if (argument == "--bands")
    {
      const auto bands = bandsNamed(value);
      if (!bands)
      {
        error =
            "--bands expects LINE:SIDE:STEP,... finest first, e.g. "
            "18:20:1,30:28:2,52:32:4";
        return std::nullopt;
      }
      options.base.bands = *bands;
    }
    else if (!numericOption(argument, value, options.base))
    {
      error =
          std::format("unknown option or bad value \"{} {}\"", argument, value);
      return std::nullopt;
    }
  }
  return options;
}
}  // namespace

GameXgSweepSpec gameXgSpec(std::string_view layer)
{
  GameXgSweepSpec spec;
  spec.layer = std::string(layer);
  if (layer == GameXgTable::KEEPER)
  {
    // Three Shooting ratings, as its three Goalkeeping ones, to keep the
    // run near the empty goal's length (my choice).
    spec.shooting = spec.goalkeeping;
  }
  return spec;
}

Drill* gameXgDrill(const std::vector<std::unique_ptr<Drill>>& drills)
{
  for (const auto& drill : drills)
  {
    if (std::string_view(drill->name()) == "Still-ball shot")
    {
      return drill.get();
    }
  }
  return nullptr;
}

int runGameXgCommand(const StatsConfig& config,
                     const std::vector<std::string>& arguments)
{
  auto drills = makeDrills();
  Drill* shotDrill = gameXgDrill(drills);
  if (shotDrill == nullptr)
  {
    std::cerr << "game-xg: no Shot drill\n";
    return 2;
  }
  std::string error;
  const auto options = parseOptions(arguments, error);
  if (!options)
  {
    std::cerr << "game-xg: " << error << '\n';
    return 2;
  }
  const bool save = options->save;
  const bool print = options->print;
  for (const GoalKind goal : options->goals)
  {
    GameXgSweepSpec spec = options->base;
    spec.goal = goal;
    GameXgSweep sweep(*shotDrill, config, spec);
    std::cout << std::format("Game xG, {} layer, {} goal: {} shots a point\n",
                             spec.layer, goalKindName(goal), spec.repeats);
    if (!sweep.problem().empty())
    {
      std::cerr << "game-xg: " << sweep.problem() << '\n';
      return 1;
    }
    while (sweep.work(1.0))
    {
      std::cerr << std::format("  {}: {} / {} runs\r", sweep.phase(),
                               sweep.runsDone(), sweep.runsQueued());
    }
    const GameXgTable& table = sweep.table();
    std::cout << std::format(
        "Done in {:.1f} s: {} runs at {} points. Left and right {} ({:.0f}% "
        "of mirrored pairs differed; pooled z {:+.1f}).\n",
        sweep.seconds(), sweep.runsDone(), table.samples.size(),
        table.mirrored ? "agree, folded together" : "differ, both kept",
        100.0 * sweep.asymmetricShare(), sweep.asymmetricZ());
    if (print)
    {
      printTable(table);
    }
    if (save)
    {
      std::filesystem::path path = GameXg::file(table.layer, goal);
      if (!options->out.empty())
      {
        path = options->out;
        if (options->goals.size() > 1)
        {
          path.replace_filename(path.stem().string() + "_" +
                                goalKindName(goal) + path.extension().string());
        }
      }
      std::string problem;
      if (!table.save(path, problem))
      {
        std::cerr << "game-xg: " << problem << '\n';
        return 1;
      }
      std::cout << "Saved " << path.string() << '\n';
      GameXg::reload();
    }
  }
  return 0;
}
