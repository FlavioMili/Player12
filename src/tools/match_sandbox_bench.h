// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "tools/match_sandbox_drill.h"
#include "tools/match_sandbox_tuning.h"

struct StatsConfig;

/**
 * Technique bench (Notes and plans/match_engine_technique_bench_plan.md): a
 * catalogue of pass and shot tests, each a drill with fixed settings and
 * tags, swept over the skill that matters (Passing, Shooting) with many
 * seeds, on worker threads, with the sandbox's technique tuning. Results are
 * success rates by test and skill, kept per run for CSV.
 */

/** A pass on the strike alone counts as on target within this many metres
 * of the target: 1 m at 10 m and closer, 3 m at 60 m and beyond, in a
 * straight line between. */
inline constexpr float BENCH_NEAR_METRES = 10.0F;
inline constexpr float BENCH_NEAR_TOLERANCE = 1.0F;
inline constexpr float BENCH_FAR_METRES = 60.0F;
inline constexpr float BENCH_FAR_TOLERANCE = 3.0F;
/** The tolerance (m) for a pass struck from `distance` metres. */
float passTolerance(float distance);

/** What a test counts as a success. */
struct BenchSuccess
{
  enum class Kind : std::uint8_t
  {
    /** A 0/1 metric at 1 ("On target"). */
    FLAG,
    /** A metric at most `threshold`. */
    AT_MOST,
    /** A metric at least `threshold`. */
    AT_LEAST,
    /** "Strike miss (m)" within passTolerance of "Strike to target (m)". */
    STRIKE,
  };
  Kind kind = Kind::FLAG;
  std::string metric;
  float threshold = 0.0F;

  /** Whether a finished run's metrics pass; `tolerance` gets the strike
   * tolerance used (STRIKE only). */
  [[nodiscard]] bool judge(const std::vector<DrillMetric>& metrics,
                           float* tolerance = nullptr) const;
  /** In words, e.g. "lands or runs within 1.4 m of the target". `range`: the
   * test's nominal distance, for the strike tolerance. */
  [[nodiscard]] std::string describe(std::optional<float> range) const;
};

struct BenchTest
{
  std::string id;
  std::string name;
  std::string section;
  std::string drill;
  /** By the drill's setting names (choices as their index). */
  std::map<std::string, float> settings;
  /** Filter tags, e.g. family=pass, type=ground, range=short. */
  std::map<std::string, std::string> tags;
  /** The setting swept as the skill ("Passing", "Shooting"). */
  std::string skill;
  BenchSuccess success;
  /** Nominal distance (m): the pass's length, for showing the tolerance. */
  std::optional<float> range;

  [[nodiscard]] std::string tag(const std::string& key) const;
};

struct BenchCatalogue
{
  std::filesystem::path path;
  std::vector<BenchTest> tests;
  /** Skill values swept by default. */
  std::vector<float> skills;
  /** Anything wrong in the file (the test is left out). */
  std::vector<std::string> problems;
};

/** assets/tools/technique_tests.json under the asset root. */
std::filesystem::path benchCataloguePath();
BenchCatalogue loadBenchCatalogue(const std::filesystem::path& path);
/** 1, 10, 20 ... 100. */
std::vector<float> defaultBenchSkills();

/** Mean and spread of one metric over a cell's finished runs. */
struct BenchStat
{
  int count = 0;
  double sum = 0.0;
  double squares = 0.0;
  void add(double value)
  {
    ++count;
    sum += value;
    squares += value * value;
  }
  [[nodiscard]] double mean() const { return count > 0 ? sum / count : 0.0; }
  [[nodiscard]] double sd() const;
};

/** One test at one skill. */
struct BenchCell
{
  int runs = 0;
  int completed = 0;
  int successes = 0;
  /** Strike tolerances used, for their mean. */
  BenchStat tolerance;
  std::map<std::string, BenchStat> metrics;

  /** Share of the finished runs that succeeded (0-1); none if none
   * finished. */
  [[nodiscard]] std::optional<double> rate() const;
};

/** One run's outcome. */
struct BenchRow
{
  std::size_t test = 0;
  float skill = 0.0F;
  std::uint32_t seed = 0;
  bool completed = false;
  bool success = false;
  float tolerance = 0.0F;
  std::vector<DrillMetric> metrics;
};

/** A bench run's results (or one loaded back from its CSV). */
class BenchResults
{
 public:
  std::string label;
  std::vector<BenchTest> tests;
  std::vector<float> skills;
  int repeats = 0;
  std::vector<std::string> tuningChanges;

  void add(BenchRow row);
  [[nodiscard]] const std::vector<BenchRow>& rows() const { return all; }
  /** The test's cell at a skill, by test id (so runs of different
   * selections compare); null if it was not run. */
  [[nodiscard]] const BenchCell* cell(const std::string& testId,
                                      float skill) const;
  [[nodiscard]] const std::vector<std::string>& metricNames() const
  {
    return names;
  }
  [[nodiscard]] std::string csv() const;
  /** Writes the CSV and, beside it, the tuning used ("<name> tuning.txt"). */
  bool save(const std::filesystem::path& path) const;
  static std::optional<BenchResults> load(const std::filesystem::path& path,
                                          std::string& error);

 private:
  std::vector<BenchRow> all;
  std::vector<std::string> names;
  /** By test id, then skill. */
  std::map<std::string, std::map<float, BenchCell>> cells;
};

struct BenchSpec
{
  /** Indexes into the catalogue's tests. */
  std::vector<std::size_t> tests;
  std::vector<float> skills;
  int repeats = 50;
  std::uint32_t firstSeed = 1;
  /** Worker threads; 0: ThreadPool::defaultThreadCount(). */
  unsigned threads = 0;
  /** Sweep this setting instead of each test's own skill (empty: theirs). */
  std::string skillOverride;
  TechniqueTuning tuning;
};

/**
 * A bench run in progress: worker threads, each with its own drills, take
 * runs in an order that gives a rough picture early (every test at the
 * middle skill, then the ends, then the rest). collect() moves finished runs
 * into the results; stop() or destruction stops and joins the workers.
 */
class BenchRunner
{
 public:
  BenchRunner(const BenchCatalogue& catalogue, const StatsConfig& config,
              BenchSpec spec);
  ~BenchRunner();
  BenchRunner(const BenchRunner&) = delete;
  BenchRunner& operator=(const BenchRunner&) = delete;

  void stop();
  [[nodiscard]] bool done() const { return finished.load() >= jobs.size(); }
  /** Still working: not done and not stopped. */
  [[nodiscard]] bool running() const { return !done() && !stopping.load(); }
  [[nodiscard]] std::size_t runsDone() const { return finished.load(); }
  [[nodiscard]] std::size_t runsTotal() const { return jobs.size(); }
  [[nodiscard]] double seconds() const;
  [[nodiscard]] const std::vector<std::string>& warnings() const
  {
    return notes;
  }
  /** Moves the runs finished since the last call into `results` (set up
   * here on the first call: tests, skills, tuning). */
  void collect(BenchResults& results);

 private:
  struct Job
  {
    std::size_t test = 0;
    float skill = 0.0F;
    std::uint32_t seed = 0;
  };
  void work();

  const StatsConfig& config;
  BenchSpec setup;
  std::vector<BenchTest> tests;
  std::vector<Job> jobs;
  std::vector<std::string> notes;
  std::atomic<std::size_t> nextJob{0};
  std::atomic<std::size_t> finished{0};
  std::atomic<bool> stopping{false};
  std::mutex mutex;
  std::vector<BenchRow> pending;
  bool initialised = false;
  std::chrono::steady_clock::time_point started;
  std::atomic<double> finishedAfter{0.0};
  std::vector<std::jthread> workers;
};

/** A new results file: Documents/Player12 drill results/bench/<time>.csv. */
std::filesystem::path benchResultsPath();

/** Headless bench from the command line (see match_sandbox_main.cpp). */
int runBenchCommand(const StatsConfig& config,
                    const std::vector<std::string>& arguments);
