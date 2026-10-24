// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// fm_lab: headless balance lab. Runs seeded batches of isolated matches,
// tactic round robins or whole-world seasons and compares the results with
// the spec's calibration targets (Markdown + JSON report, CI exit codes).

#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "global/logger.h"
#include "global/thread_pool.h"
#include "tools/lab_fixtures.h"
#include "tools/lab_report.h"
#include "tools/lab_runner.h"
#include "tools/lab_season.h"

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace
{
class ScratchDirectoryError : public std::runtime_error
{
 public:
  using std::runtime_error::runtime_error;
};

constexpr unsigned DEFAULT_THREADS = 4;
constexpr int DEFAULT_MATCHES = 2000;
constexpr int DEFAULT_TACTIC_SEEDS = 200;
constexpr float DEFAULT_RATING = 65.0f;
constexpr float SPREAD_MIN_RATING = 55.0f;
constexpr float SPREAD_MAX_RATING = 80.0f;
constexpr int PRIORITY_INCREMENT = 10;
constexpr double TWO_POW_32 = 4294967296.0;
constexpr std::uint64_t SPREAD_SALT = 0x5EED5EEDULL;

constexpr std::string_view USAGE = R"(fm_lab - headless balance lab

Usage:
  fm_lab matches [--n N] [--seed S] [--home-rating X --away-rating Y | --spread
                 [--rating-min A --rating-max B]] [--fidelity full|background]
                 [--threads T] [--out-dir DIR]
  fm_lab season  [--leagues all|top|ID,ID...] [--seasons N] [--seed S]
                 [--threads T] [--max-days D] [--reload] [--out-dir DIR]
  fm_lab tactics [--n SEEDS_PER_PAIR] [--seed S] [--rating X] [--threads T]
                 [--fidelity full|background] [--out-dir DIR]

matches  isolated matches between synthetic 4-4-2 squads (default 2000 at
         rating 65 v 65; --spread draws both ratings uniformly per match)
season   whole AI-only world via GameController in a scratch directory
         ("seasons" is accepted too); the soak trend table tracks
         population, ability, finances by tier, competitions, save size and
         day timings season by season; --reload saves and reloads the career
         after every season
tactics  round robin of the tactic presets between equal teams; each seed is
         played twice with home and away swapped

Fidelity: full (default, the live engine's 10 Hz step) or background (the
cheaper step unwatched fixtures use); compare both to check equivalence.

Threads: --threads, else FM_SIM_THREADS, else 4 (max 8); the process lowers
its own priority. Output: report.md and metrics.json in --out-dir (default a
fresh /tmp/fm-lab-* directory). Saves go to a scratch runtime root under /tmp
that is removed at exit; the user's saves are never touched.

Exit codes: 0 all Gate targets pass, 1 a Gate target FAILED, 2 Monitor pass
rate below 90%, 3 invalid arguments, 4 simulation error.
)";

struct Arguments
{
  std::string mode;
  std::map<std::string, std::string, std::less<>> values;
  std::vector<std::string> flags;

  bool has(std::string_view flag) const
  {
    return std::ranges::find(flags, flag) != flags.end();
  }
  std::optional<std::string> value(const std::string& key) const
  {
    const auto found = values.find(key);
    if (found == values.end()) return std::nullopt;
    return found->second;
  }
};

class UsageError : public std::runtime_error
{
 public:
  using std::runtime_error::runtime_error;
};

Arguments parseArguments(int argc, char** argv)
{
  static constexpr std::array<std::string_view, 4> FLAGS = {
      "--spread", "--reload", "--help", "-h"};
  static constexpr std::array<std::string_view, 13> OPTIONS = {
      "--n",       "--seed",       "--home-rating", "--away-rating",
      "--rating",  "--rating-min", "--rating-max",  "--threads",
      "--out-dir", "--leagues",    "--seasons",     "--max-days",
      "--fidelity"};
  Arguments arguments;
  if (argc < 2) throw UsageError("missing mode");
  arguments.mode = argv[1];
  if (arguments.mode == "seasons") arguments.mode = "season";
  for (int i = 2; i < argc; ++i)
  {
    const std::string_view arg = argv[i];
    if (std::ranges::find(FLAGS, arg) != FLAGS.end())
    {
      arguments.flags.emplace_back(arg);
      continue;
    }
    if (std::ranges::find(OPTIONS, arg) == OPTIONS.end())
      throw UsageError(std::format("unknown option '{}'", arg));
    if (i + 1 >= argc)
      throw UsageError(std::format("option '{}' needs a value", arg));
    arguments.values[std::string(arg)] = argv[++i];
  }
  return arguments;
}

template <typename Number>
Number parseNumber(const Arguments& arguments, const std::string& key,
                   Number fallback, Number minimum, Number maximum)
{
  const std::optional<std::string> text = arguments.value(key);
  if (!text) return fallback;
  try
  {
    std::size_t used = 0;
    Number value{};
    if constexpr (std::is_floating_point_v<Number>)
      value = static_cast<Number>(std::stod(*text, &used));
    else
      value = static_cast<Number>(std::stoll(*text, &used));
    if (used != text->size() || value < minimum || value > maximum)
      throw UsageError("");
    return value;
  }
  catch (const std::invalid_argument&)
  {
    throw UsageError(std::format("invalid value '{}' for {}", *text, key));
  }
  catch (const std::out_of_range&)
  {
    throw UsageError(std::format("invalid value '{}' for {}", *text, key));
  }
}

unsigned threadCount(const Arguments& arguments)
{
  unsigned fallback = DEFAULT_THREADS;
  if (const char* env = std::getenv("FM_SIM_THREADS"); env && *env)
  {
    try
    {
      fallback = static_cast<unsigned>(std::stoul(env));
    }
    catch (const std::invalid_argument&)
    {
      fallback = DEFAULT_THREADS;
    }
    catch (const std::out_of_range&)
    {
      fallback = DEFAULT_THREADS;
    }
  }
  fallback = std::clamp(fallback, 1U, ThreadPool::MAX_THREADS);
  return parseNumber<unsigned>(arguments, "--threads", fallback, 1U,
                               ThreadPool::MAX_THREADS);
}

std::filesystem::path makeTempDirectory(const std::string& prefix)
{
#if !defined(_WIN32)
  std::string pattern =
      (std::filesystem::temp_directory_path() / (prefix + "XXXXXX")).string();
  if (mkdtemp(pattern.data()) == nullptr)
    throw ScratchDirectoryError("cannot create a scratch directory");
  return pattern;
#else
  // SDL chooses per-user storage; do not reuse a predictable directory in a
  // shared temporary area, even if an earlier process left one behind.
  const std::unique_ptr<char, decltype(&SDL_free)> pref_path(
      SDL_GetPrefPath("FlavioMili", "FootballManagementLab"), &SDL_free);
  if (pref_path == nullptr)
    throw ScratchDirectoryError("cannot locate private lab storage");
  const std::filesystem::path base(pref_path.get());
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  for (unsigned attempt = 0; attempt < 100; ++attempt)
  {
    const auto path = base / std::format("{}{}-{}", prefix, stamp, attempt);
    if (std::filesystem::create_directory(path)) return path;
  }
  throw ScratchDirectoryError("cannot create a scratch directory");
#endif
}

/** Points the game's runtime root at a scratch directory for this run. */
class ScratchRuntime
{
 public:
  ScratchRuntime()
  {
#if !defined(_WIN32)
    setenv("FM_RUNTIME_ROOT", path.c_str(), 1);
#else
    _putenv_s("FM_RUNTIME_ROOT", path.string().c_str());
#endif
  }
  ~ScratchRuntime()
  {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
  ScratchRuntime(const ScratchRuntime&) = delete;
  ScratchRuntime& operator=(const ScratchRuntime&) = delete;

 private:
  std::filesystem::path path = makeTempDirectory("fm-lab-runtime-");
};

void quietLogging()
{
  Logger::init();
  if (const auto logger = spdlog::get("main_logger"))
    logger->set_level(spdlog::level::warn);
}

/** --fidelity: true for background (unwatched fixture) fidelity. */
bool backgroundFidelity(const Arguments& arguments)
{
  const std::string fidelity = arguments.value("--fidelity").value_or("full");
  if (fidelity != "full" && fidelity != "background")
    throw UsageError("--fidelity must be full or background");
  return fidelity == "background";
}

Lab::LabReport runMatchesMode(const Arguments& arguments, unsigned threads)
{
  const int count =
      parseNumber<int>(arguments, "--n", DEFAULT_MATCHES, 1, 10'000'000);
  const auto seed =
      parseNumber<std::uint64_t>(arguments, "--seed", 1, 0, UINT64_MAX / 2);
  const bool spread = arguments.has("--spread");
  const float homeRating = parseNumber<float>(arguments, "--home-rating",
                                              DEFAULT_RATING, 1.0f, 99.0f);
  const float awayRating = parseNumber<float>(arguments, "--away-rating",
                                              DEFAULT_RATING, 1.0f, 99.0f);
  const float minRating = parseNumber<float>(arguments, "--rating-min",
                                             SPREAD_MIN_RATING, 1.0f, 99.0f);
  const float maxRating = parseNumber<float>(arguments, "--rating-max",
                                             SPREAD_MAX_RATING, 1.0f, 99.0f);
  if (spread &&
      (arguments.value("--home-rating") || arguments.value("--away-rating")))
    throw UsageError("--spread cannot be combined with fixed ratings");
  if (minRating > maxRating) throw UsageError("--rating-min > --rating-max");
  const bool background = backgroundFidelity(arguments);

  std::vector<Lab::MatchJob> jobs(static_cast<std::size_t>(count));
  for (std::size_t i = 0; i < jobs.size(); ++i)
  {
    jobs[i].seed = Lab::matchSeed(seed, i);
    jobs[i].background = background;
    if (spread)
    {
      const auto uniform = [minRating, maxRating, seed, i](std::uint64_t salt)
      {
        return minRating +
               static_cast<float>(Lab::matchSeed(seed ^ salt, i) / TWO_POW_32) *
                   (maxRating - minRating);
      };
      jobs[i].home.rating = uniform(SPREAD_SALT);
      jobs[i].away.rating = uniform(SPREAD_SALT + 1);
    }
    else
    {
      jobs[i].home.rating = homeRating;
      jobs[i].away.rating = awayRating;
    }
  }

  const StatsConfig config = Lab::loadStatsConfig();
  const auto started = std::chrono::steady_clock::now();
  const auto progress = [&jobs](std::size_t done)
  {
    if (done % 512 < 64 || done == jobs.size())
      std::cerr << std::format("[fm_lab] {}/{} matches\n", done, jobs.size());
  };
  const std::vector<Lab::MatchSample> samples =
      Lab::simulateMatches(jobs, config, threads, progress);
  const double wall =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();

  Lab::LabReport report;
  report.mode = "matches";
  report.seed = seed;
  report.samples = samples.size();
  report.threads = threads;
  report.wall_seconds = wall;
  report.ms_per_match = 1000.0 * wall / static_cast<double>(samples.size());
  report.parameters["pairing"] =
      spread ? std::format("spread, ratings uniform in [{:.0f}, {:.0f}]",
                           minRating, maxRating)
             : std::format("fixed, home {:.0f} v away {:.0f}", homeRating,
                           awayRating);
  report.parameters["cpu_ms_per_match_estimate"] =
      std::format("{:.1f}", report.ms_per_match * threads);
  report.parameters["fidelity"] = background ? "background" : "full";
  static constexpr std::array SCOPES = {Lab::Scope::Match, Lab::Scope::Engine};
  report.rows = Lab::evaluateTargets(Lab::matchMetrics(samples), SCOPES);
  report.tables = Lab::matchTables(samples);
  report.tables.emplace_back(Lab::ratingGapTable(samples));
  report.notes.emplace_back(
      "Synthetic 4-4-2 squads (lab_fixtures.cpp), default strategies, full "
      "condition; per-match seeds are splitmix64(seed, index).");
  if (!spread)
    report.notes.emplace_back(
        "Fixed ratings: results between equal teams overstate draws compared "
        "with a real league; use --spread for league-like rating gaps.");
  return report;
}

Lab::LabReport runTacticsMode(const Arguments& arguments, unsigned threads)
{
  const int seeds =
      parseNumber<int>(arguments, "--n", DEFAULT_TACTIC_SEEDS, 1, 1'000'000);
  const auto seed =
      parseNumber<std::uint64_t>(arguments, "--seed", 1, 0, UINT64_MAX / 2);
  const float rating =
      parseNumber<float>(arguments, "--rating", DEFAULT_RATING, 1.0f, 99.0f);
  std::vector<Lab::MatchJob> jobs = Lab::tacticJobs(seed, seeds, rating);
  const bool background = backgroundFidelity(arguments);
  for (Lab::MatchJob& job : jobs) job.background = background;
  const StatsConfig config = Lab::loadStatsConfig();
  const auto started = std::chrono::steady_clock::now();
  const std::vector<Lab::MatchSample> samples = Lab::simulateMatches(
      jobs, config, threads,
      [&jobs](std::size_t done)
      {
        if (done % 512 < 64 || done == jobs.size())
          std::cerr << std::format("[fm_lab] {}/{} matches\n", done,
                                   jobs.size());
      });
  const double wall =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();

  std::map<std::string, Lab::Estimate> metrics;
  Lab::LabReport report;
  Lab::tacticMetrics(samples, seeds, metrics, report.tables);
  report.mode = "tactics";
  report.seed = seed;
  report.samples = samples.size();
  report.threads = threads;
  report.wall_seconds = wall;
  report.ms_per_match = 1000.0 * wall / static_cast<double>(samples.size());
  report.parameters["rating"] = std::format("{:.0f}", rating);
  report.parameters["seeds_per_pair"] = std::to_string(seeds);
  report.parameters["fidelity"] = background ? "background" : "full";
  static constexpr std::array SCOPES = {Lab::Scope::Tactics};
  report.rows = Lab::evaluateTargets(metrics, SCOPES);
  report.notes.emplace_back(
      "FR-030 (no placebo controls): compare the per-preset columns; "
      "presets whose points-share intervals all contain 50% have no "
      "measurable result effect at this sample size.");
  return report;
}

Lab::LabReport runSeasonMode(const Arguments& arguments, unsigned threads)
{
  Lab::SeasonOptions options;
  options.seed =
      parseNumber<std::uint64_t>(arguments, "--seed", 1, 0, UINT64_MAX / 2);
  options.seasons = parseNumber<int>(arguments, "--seasons", 1, 1, 100);
  options.max_days_per_season =
      parseNumber<int>(arguments, "--max-days", 400, 30, 2000);
  options.leagues = arguments.value("--leagues").value_or("all");
  options.threads = threads;
  options.progress = &std::cerr;
  options.reload = arguments.has("--reload");
  return Lab::runSeasons(options);
}

void writeReport(const Lab::LabReport& report,
                 const std::filesystem::path& directory)
{
  std::filesystem::create_directories(directory);
  const std::string markdown = Lab::toMarkdown(report);
  std::ofstream(directory / "report.md") << markdown;
  std::ofstream(directory / "metrics.json")
      << Lab::toJson(report).dump(2) << "\n";
  std::cout << markdown << "\n";
  std::cout << std::format("[fm_lab] report: {}\n",
                           (directory / "report.md").string());
}
}  // namespace

int main(int argc, char** argv)
{
  Arguments arguments;
  try
  {
    arguments = parseArguments(argc, argv);
    if (arguments.has("--help") || arguments.has("-h") ||
        arguments.mode == "--help" || arguments.mode == "-h" ||
        arguments.mode == "help")
    {
      std::cout << USAGE;
      return Lab::ExitCode::OK;
    }
    if (arguments.mode != "matches" && arguments.mode != "season" &&
        arguments.mode != "tactics")
      throw UsageError(std::format("unknown mode '{}'", arguments.mode));
  }
  catch (const UsageError& error)
  {
    std::cerr << "fm_lab: " << error.what() << "\n\n" << USAGE;
    return Lab::ExitCode::INVALID_ARGUMENTS;
  }

#if !defined(_WIN32)
  // Batch runs share the machine: stay below interactive work.
  (void)nice(PRIORITY_INCREMENT);
#endif

  try
  {
    const unsigned threads = threadCount(arguments);
    const std::optional<std::string> outDir = arguments.value("--out-dir");
    const ScratchRuntime runtime;
    quietLogging();
    Lab::LabReport report;
    if (arguments.mode == "matches")
      report = runMatchesMode(arguments, threads);
    else if (arguments.mode == "tactics")
      report = runTacticsMode(arguments, threads);
    else
      report = runSeasonMode(arguments, threads);
    writeReport(report, outDir
                            ? std::filesystem::path(*outDir)
                            : makeTempDirectory("fm-lab-" + report.mode + "-"));
    return report.exitCode();
  }
  catch (const UsageError& error)
  {
    std::cerr << "fm_lab: " << error.what() << "\n\n" << USAGE;
    return Lab::ExitCode::INVALID_ARGUMENTS;
  }
  catch (const std::invalid_argument& error)
  {
    std::cerr << "fm_lab: " << error.what() << "\n";
    return Lab::ExitCode::INVALID_ARGUMENTS;
  }
  catch (const Lab::SimulationError& error)
  {
    std::cerr << std::format(
        "fm_lab: simulation error with match seed {}: {}\n", error.seed,
        error.what());
    return Lab::ExitCode::SIMULATION_ERROR;
  }
  catch (const std::exception& error)
  {
    std::cerr << "fm_lab: " << error.what() << "\n";
    return Lab::ExitCode::SIMULATION_ERROR;
  }
}
