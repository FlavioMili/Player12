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
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "tools/match_sandbox_drill.h"

struct StatsConfig;

/**
 * Measure mode: a drill run headless many times over a sweep of one setting
 * (optionally for each value of a second, e.g. with and without the ball), each
 * point repeated over consecutive seeds. Results are the drill's metrics,
 * per run and as mean and spread per point.
 */

/** Values a setting takes in a sweep. */
struct MeasureAxis
{
  /** The drill's parameter name (DrillParameter::name). */
  std::string parameter;
  std::vector<float> values;
};

struct MeasureSpec
{
  MeasureAxis sweep;
  /** Optional second setting: one curve per value. */
  std::optional<MeasureAxis> by;
  /** Runs per point, on seeds firstSeed, firstSeed + 1, ... */
  int repeats = 1;
  std::uint32_t firstSeed = 1;
  /** Settings held at a value for the whole measure (others keep theirs). */
  std::map<std::string, float> fixed;
};

/** One run's results. */
struct MeasureRun
{
  float x = 0.0F;
  float by = 0.0F;
  std::uint32_t seed = 0;
  /** Empty if the run did not complete. */
  std::vector<DrillMetric> metrics;
};

/** Spread of one metric over a point's runs. */
struct MeasureStat
{
  int count = 0;
  double mean = 0.0;
  double sd = 0.0;
  double min = 0.0;
  double max = 0.0;
};

/** One point of the sweep (for one value of `by`). */
struct MeasurePoint
{
  float x = 0.0F;
  float by = 0.0F;
  int runs = 0;
  int completed = 0;
  std::map<std::string, MeasureStat> stats;
};

/**
 * A measure in progress or done. Changes the drill's settings while it
 * runs and puts them back when it finishes, stops or is destroyed.
 */
class DrillMeasure
{
 public:
  DrillMeasure(Drill& drill, const StatsConfig& config, MeasureSpec spec);
  ~DrillMeasure();
  DrillMeasure(const DrillMeasure&) = delete;
  DrillMeasure& operator=(const DrillMeasure&) = delete;

  /** Runs for about `budgetSeconds` of real time; false once all are done. */
  bool work(double budgetSeconds);
  /** Stops early (the results so far stay). */
  void stop();
  [[nodiscard]] bool done() const { return next >= total; }
  [[nodiscard]] std::size_t runsDone() const { return next; }
  [[nodiscard]] std::size_t runsTotal() const { return total; }
  [[nodiscard]] double seconds() const { return elapsed; }

  [[nodiscard]] const MeasureSpec& spec() const { return setup; }
  [[nodiscard]] const std::vector<MeasureRun>& runs() const { return results; }
  /** Metric names in the order the drill reports them. */
  [[nodiscard]] const std::vector<std::string>& metricNames() const
  {
    return names;
  }
  /** Mean and spread per point, by `by` value then sweep value. */
  [[nodiscard]] std::vector<MeasurePoint> points() const;
  /** Problems with the spec (unknown setting...), found at the start. */
  [[nodiscard]] const std::vector<std::string>& warnings() const
  {
    return notes;
  }

  /** Every run as a CSV row (settings, seed, metrics). */
  [[nodiscard]] std::string csv() const;
  /** Display text of a value of a setting (a choice's name, or the number). */
  [[nodiscard]] std::string valueText(const std::string& parameter,
                                      float value) const;

 private:
  /** A headless run has no "You": the AI runs instead. */
  void keepHeadless();
  void apply(std::size_t index);
  void restore();

  Drill& drill;
  const StatsConfig& config;
  MeasureSpec setup;
  std::size_t total = 0;
  std::size_t next = 0;
  double elapsed = 0.0;
  std::vector<MeasureRun> results;
  std::vector<std::string> names;
  std::vector<std::string> notes;
  /** The drill's own settings and seed, put back at the end. */
  std::map<std::string, float> saved;
  std::uint32_t savedSeed = 1;
  /** Choice names of each choice setting, for display and CSV. */
  std::map<std::string, std::vector<std::string>> choiceNames;
  bool restored = false;
};

/** Parses an axis: "Pace=40:95:12" (from:to:count), "Pace=40,60,80", or a
 * choice setting's name alone ("Who runs": every choice that can run
 * headless). Empty on error, with the reason in `error`. */
std::optional<MeasureAxis> parseMeasureAxis(Drill& drill,
                                            const std::string& text,
                                            std::string& error);
/** Every value of a choice setting that can run headless (not "You"). */
std::vector<float> headlessChoices(const DrillParameter& parameter);
/** `count` values evenly from `from` to `to` (rounded if whole numbers). */
std::vector<float> spreadValues(float from, float to, int count, bool integer);

/** Folder for exported results: Documents/Player12 drill results. */
std::filesystem::path measureFolder();
/** A new results file in that folder, named by drill, sweep and time. */
std::filesystem::path measurePath(const std::string& drill,
                                  const std::string& sweep);

/** Headless measure from the command line (see match_sandbox_main.cpp). */
int runMeasureCommand(const StatsConfig& config, const std::string& drillName,
                      const std::vector<std::string>& arguments);
