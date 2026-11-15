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

#include <spdlog/spdlog.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "global/logger.h"
#include "global/thread_pool.h"
#include "model/match_tuning.h"
#include "gui/view_models/formation.h"
#include "tools/lab_fixtures.h"
#include "tools/lab_report.h"
#include "tools/lab_runner.h"
#include "tools/lab_season.h"
#include "tools/lab_touch_sensitivity.h"
#include "tools/lab_trace.h"
#include "tools/match_sandbox_measure.h"
#include "tools/match_sandbox_session.h"
#include "tools/match_sandbox_setup.h"

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace
{
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
                 [--brains HOME,AWAY] [--bodies HOME,AWAY] [--threads T]
                 [--handshake H] [--referee R] [--managers HOME,AWAY]
                 [--ball B] [--out-of-play O] [--home-advantage X] [--uniform]
                 [--knockout] [--possession aardvark|binturong]
                 [--formation F|HOME,AWAY]
                 [--knobs "Name=value;Name=value"] [--out-dir DIR]
  fm_lab trace   [the matches options] --out FILE [--seconds S]
  fm_lab season  [--leagues all|top|ID,ID...] [--seasons N] [--seed S]
                 [--threads T] [--max-days D] [--reload] [--out-dir DIR]
  fm_lab tactics [--n SEEDS_PER_PAIR] [--seed S] [--rating X] [--threads T]
                 [--fidelity full|background] [--brains HOME,AWAY]
                 [--bodies HOME,AWAY] [--handshake H] [--referee R]
                 [--managers HOME,AWAY] [--ball B] [--out-of-play O]
                 [--home-advantage X] [--possession aardvark|binturong]
                 [--out-dir DIR]
  fm_lab drill   [--brains HOME,AWAY] [--bodies HOME,AWAY] [--handshake H]
                 [--referee R] [--managers HOME,AWAY] [--ball B]
                 [--out-of-play O] [--possession aardvark|binturong]
                 [--pitch LENGTHxWIDTH [--goal KIND]]
                 "DRILL" --sweep "Setting=FROM:TO:COUNT" [--by ...]
                 [--preset NAME] [--set "Setting=value"]... [--repeats N]
                 [--seed S] [--csv [FILE]]
                 (or, for one run, [--preset NAME] [--set ...] [--seed S]
                 --trace FILE [--seconds S])
  fm_lab session [--brains HOME,AWAY] [--bodies HOME,AWAY] [--seed S] [--full]
                 "SESSION"
  fm_lab touch-sensitivity [--repeats N] [--ratings A,B,...]
                 [--situations all|NAME;NAME] [--only NAME;NAME] [--step S]
                 [--range "Name=LOW:HIGH"]... [--seed S] [--out-dir DIR]
                 [--brains HOME,AWAY] [--bodies HOME,AWAY]

matches  isolated matches between synthetic 4-4-2 squads (default 2000 at
         rating 65 v 65; --spread draws both ratings uniformly per match;
         --uniform gives every player every stat at the rating, the match
         sandbox's "Team N", instead of role-shaped attributes)
season   whole AI-only world via GameController in a scratch directory
         ("seasons" is accepted too); the soak trend table tracks
         population, ability, finances by tier, competitions, save size and
         day timings season by season; --reload saves and reloads the career
         after every season
trace    the first --seconds (default 10) of each match matches would play
         with the same options: every player and the ball in metres after
         every fixed step, as CSV (run,tick,who,x,y,z,facing) in --out; for
         comparing two builds tick by tick
tactics  round robin of the tactic presets between equal teams; each seed is
         played twice with home and away swapped
drill    a match sandbox drill measured headless: the same options as
         match_sandbox --measure (a sweep of one setting, optionally by a
         second, over repeated seeds; a preset fills settings in, --set
         overrides any, beyond its slider range too); the table goes to the
         console, the runs to CSV with --csv. The drill plays with the match
         options given before its name (brains, bodies, handshake, referee,
         managers, ball, out of play), as the sandbox's drills play with
         its settings; --pitch plays it on a walled training pitch of that
         size instead of its own, with --goal a goal of
         that kind at each end (full, 9v9, 7v7, futsal, 5-a-side, small,
         pop-up)
touch-sensitivity
         which variables move the first touch most: every First touch
         situation at several First Touch ratings, as tuned, then with each
         tuning knob and the receiver's Pace, Condition, Foot and First
         Touch set low and high one at a time (same seeds); ranked by the
         largest change in balls lost and in heavy touches, side by side,
         with the angle and weight errors beside them. Knob steps are x(1
         -/+ step) within each knob's range; --range sets any variable's two
         values as given, beyond its range too. report.md, ranking.csv and
         cells.csv in --out-dir (default a fresh fm-lab-touch-* temp folder);
         Binturong brains and bodies unless --brains/--bodies say otherwise

Fidelity: full (default, the live engine's 10 Hz step) or background (the
cheaper step unwatched fixtures use); compare both to check equivalence.

Brains: each side's decision-making, aardvark (default) or binturong; one name
sets both sides ("--brains binturong"), two set home and away
("--brains aardvark,binturong").

Bodies: each side's players' bodies (movement, fitness, kicking, first
touch, goalkeeping), aardvark (default) or binturong, set the same way.

Handshake: how the inputs become match state (attribute scale, physical
limits, roles), one for the match: binturong (default) or aardvark.

Referee: the officials' judgement (fouls, cards, advantage, offside calls,
added time), one for the match: aardvark (default) or binturong.

Managers: each side's AI manager (substitutions, who comes on, late shouts),
aardvark (default) or binturong, set the same way as the brains.

Ball: the ball physics (flight, drag, bounces, rolling, swerve, and the
launch solvers), one for the match: aardvark (default) or binturong;
drills default to binturong, as in the match sandbox.

Out of play: goals, the woodwork, corners, goal kicks, throw-ins and every
restart set up and taken, one for the match: aardvark (default) or binturong.

Home advantage: the home side's edge on attributes, execution and
refereeing (MatchContext::homeAdvantageScale), 0 a neutral venue (default)
to 2; 1 is the game's full edge (the setting of reports made before this
option existed).

Knobs (matches): tuning knobs set for every match, by their registry names,
separated by semicolons: first-touch knobs ("Angle fail base") and strike
knobs by kind ("Ground pass: Angle fail base"); the rest keep their tuned
values. Example: --knobs "Ground pass: Angle jitter=0.08;Comfort gap=0.4".

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
  std::map<std::string, std::string> values;
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
  static constexpr std::array<std::string_view, 6> FLAGS = {
      "--spread", "--uniform", "--knockout", "--reload", "--help", "-h"};
  static constexpr std::array<std::string_view, 26> OPTIONS = {
      "--formation",
      "--n",       "--seed",       "--home-rating", "--away-rating",
      "--rating",  "--rating-min", "--rating-max",  "--threads",
      "--out-dir", "--leagues",    "--seasons",     "--max-days",
      "--fidelity", "--brains",    "--bodies",      "--handshake",
      "--referee", "--managers",   "--ball",        "--out-of-play",
      "--home-advantage", "--knobs", "--possession", "--out", "--seconds"};
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
  catch (const std::exception&)
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
    catch (const std::exception&)
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
  std::string pattern =
      (std::filesystem::temp_directory_path() / (prefix + "XXXXXX")).string();
#if !defined(_WIN32)
  if (mkdtemp(pattern.data()) == nullptr)
    throw std::runtime_error("cannot create a scratch directory");
  return pattern;
#else
  const auto path =
      std::filesystem::temp_directory_path() /
      std::format("{}{}", prefix,
                  std::chrono::steady_clock::now().time_since_epoch().count());
  std::filesystem::create_directories(path);
  return path;
#endif
}

/** Points the game's runtime root at a scratch directory for this run. */
class ScratchRuntime
{
 public:
  ScratchRuntime() : path(makeTempDirectory("fm-lab-runtime-"))
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
  std::filesystem::path path;
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

/** --brains HOME,AWAY (or one brain for both sides); default aardvark. */
std::array<BrainId, 2> brainPair(const Arguments& arguments)
{
  const std::string text = arguments.value("--brains").value_or("aardvark");
  const auto brains = parseBrainPair(text);
  if (!brains)
    throw UsageError(
        "--brains must be HOME,AWAY or one brain, each aardvark or binturong");
  return *brains;
}

/** --formation F (both sides) or HOME,AWAY: the game's formation presets
 * (4-4-2, 4-3-3, 4-2-3-1, 3-5-2, 5-3-2); none given, the lab's 4-4-2. */
std::array<std::optional<Lab::FormationSlots>, 2>
formationPair(const Arguments& arguments)
{
  const auto text = arguments.value("--formation");
  if (!text) return {};
  const auto lookup = [](std::string_view name)
  {
    for (const Formation::Preset& preset : Formation::PRESETS)
    {
      if (name != preset.name) continue;
      Lab::FormationSlots slots;
      for (std::size_t index = 0; index < slots.size(); ++index)
        slots[index] = {preset.slots[index].role,
                        preset.slots[index].position};
      return slots;
    }
    throw UsageError(std::format(
        "--formation: unknown '{}' (4-4-2, 4-3-3, 4-2-3-1, 3-5-2, 5-3-2)",
        name));
  };
  const std::size_t comma = text->find(',');
  if (comma == std::string::npos) return {lookup(*text), lookup(*text)};
  return {lookup(std::string_view(*text).substr(0, comma)),
          lookup(std::string_view(*text).substr(comma + 1))};
}

/** --bodies HOME,AWAY (or one body for both sides); default aardvark. */
std::array<BodyId, 2> bodyPair(const Arguments& arguments)
{
  const std::string text = arguments.value("--bodies").value_or("aardvark");
  const auto bodies = parseBodyPair(text);
  if (!bodies)
    throw UsageError(
        "--bodies must be HOME,AWAY or one body, each aardvark or binturong");
  return *bodies;
}

void setBrainsAndBodies(std::vector<Lab::MatchJob>& jobs,
                        std::array<BrainId, 2> brains,
                        std::array<BodyId, 2> bodies)
{
  for (Lab::MatchJob& job : jobs)
  {
    job.home.brain = brains[0];
    job.away.brain = brains[1];
    job.home.body = bodies[0];
    job.away.body = bodies[1];
  }
}

/** --knobs "Name=value;Name=value": first-touch and strike knobs by name. */
struct KnobSettings
{
  FirstTouchKnobs touch;
  StrikeKnobs strike;
  std::string text;
};

KnobSettings knobsOption(const Arguments& arguments)
{
  KnobSettings settings;
  const std::optional<std::string> text = arguments.value("--knobs");
  if (!text) return settings;
  settings.text = *text;
  std::string_view rest = *text;
  while (!rest.empty())
  {
    const std::size_t end = rest.find(';');
    const std::string_view item = rest.substr(0, end);
    rest = end == std::string_view::npos ? std::string_view{}
                                         : rest.substr(end + 1);
    if (item.empty()) continue;
    const std::size_t equals = item.rfind('=');
    if (equals == std::string_view::npos)
      throw UsageError(std::format("--knobs: '{}' is not Name=value", item));
    const std::string name(item.substr(0, equals));
    const std::string number(item.substr(equals + 1));
    float value = 0.0f;
    try
    {
      std::size_t used = 0;
      value = std::stof(number, &used);
      if (used != number.size()) throw std::invalid_argument(number);
    }
    catch (const std::exception&)
    {
      throw UsageError(std::format("--knobs: '{}' is not a number", number));
    }
    if (const auto knob = touchKnobByName(name))
      settings.touch[*knob] = value;
    else if (float* strike = settings.strike.find(name))
      *strike = value;
    else
      throw UsageError(std::format("--knobs: no knob named '{}'", name));
  }
  return settings;
}

std::string brainsParameter(std::array<BrainId, 2> brains)
{
  return std::format("home {}, away {}", brainName(brains[0]),
                     brainName(brains[1]));
}

std::string bodiesParameter(std::array<BodyId, 2> bodies)
{
  return std::format("home {}, away {}", bodyName(bodies[0]),
                     bodyName(bodies[1]));
}

/** --handshake aardvark|binturong (one for the match); default binturong. */
HandshakeId handshakeOption(const Arguments& arguments)
{
  const std::string text = arguments.value("--handshake").value_or("binturong");
  const auto handshake = handshakeFromKey(text);
  if (!handshake) throw UsageError("--handshake must be aardvark or binturong");
  return *handshake;
}

/** --referee, --managers HOME,AWAY, --ball and --out-of-play; each aardvark
 * by default. */
struct SeamChoices
{
  RefereeId referee = RefereeId::AARDVARK;
  std::array<ManagerId, 2> managers{ManagerId::AARDVARK, ManagerId::AARDVARK};
  BallPhysicsId ball = BallPhysicsId::AARDVARK;
  OutOfPlayId outOfPlay = OutOfPlayId::AARDVARK;
  PossessionId possession = PossessionId::AARDVARK;
};

SeamChoices seamChoices(const Arguments& arguments)
{
  SeamChoices choice;
  const auto referee =
      refereeFromKey(arguments.value("--referee").value_or("aardvark"));
  if (!referee) throw UsageError("--referee must be aardvark or binturong");
  choice.referee = *referee;
  const auto managers =
      parseManagerPair(arguments.value("--managers").value_or("aardvark"));
  if (!managers)
    throw UsageError(
        "--managers must be HOME,AWAY or one manager, each aardvark or "
        "binturong");
  choice.managers = *managers;
  const auto ball =
      ballPhysicsFromKey(arguments.value("--ball").value_or("aardvark"));
  if (!ball) throw UsageError("--ball must be aardvark or binturong");
  choice.ball = *ball;
  const auto outOfPlay =
      outOfPlayFromKey(arguments.value("--out-of-play").value_or("aardvark"));
  if (!outOfPlay)
    throw UsageError("--out-of-play must be aardvark or binturong");
  choice.outOfPlay = *outOfPlay;
  const auto possession = possessionFromKey(arguments.value("--possession").value_or("aardvark"));
  if (!possession) throw UsageError("--possession must be aardvark or binturong");
  choice.possession = *possession;
  return choice;
}

void setSeamChoices(std::vector<Lab::MatchJob>& jobs,
                    const SeamChoices& choice)
{
  for (Lab::MatchJob& job : jobs)
  {
    job.referee = choice.referee;
    job.home.manager = choice.managers[0];
    job.away.manager = choice.managers[1];
    job.ballPhysics = choice.ball;
    job.outOfPlay = choice.outOfPlay;
    job.possession = choice.possession;
  }
}

void reportSeamChoices(Lab::LabReport& report, const SeamChoices& choice)
{
  report.parameters["referee"] = std::string(refereeName(choice.referee));
  report.parameters["managers"] =
      std::format("home {}, away {}", managerName(choice.managers[0]),
                  managerName(choice.managers[1]));
  report.parameters["ball_physics"] = std::string(ballPhysicsName(choice.ball));
  report.parameters["out_of_play"] =
      std::string(outOfPlayName(choice.outOfPlay));
  report.parameters["possession"] = std::string(possessionName(choice.possession));
}

/** --home-advantage X, 0 (a neutral venue, the default) to 2. */
float homeAdvantageOption(const Arguments& arguments)
{
  return parseNumber<float>(arguments, "--home-advantage", 0.0f, 0.0f,
                            MatchTuning::Context::MAX_HOME_ADVANTAGE_SCALE);
}

/** The options of matches (and trace): who plays, with what, how often. */
struct MatchesOptions
{
  int count = DEFAULT_MATCHES;
  std::uint64_t seed = 1;
  bool spread = false;
  float homeRating = DEFAULT_RATING;
  float awayRating = DEFAULT_RATING;
  float minRating = SPREAD_MIN_RATING;
  float maxRating = SPREAD_MAX_RATING;
  bool background = false;
  std::array<BrainId, 2> brains{};
  std::array<BodyId, 2> bodies{};
  HandshakeId handshake = HandshakeId::BINTURONG;
  float homeAdvantage = 0.0f;
  SeamChoices seams;
  bool uniform = false;
  bool knockout = false;
  KnobSettings knobs;
  std::array<std::optional<Lab::FormationSlots>, 2> formations;
};

MatchesOptions matchesOptions(const Arguments& arguments)
{
  MatchesOptions o;
  o.count = parseNumber<int>(arguments, "--n", DEFAULT_MATCHES, 1, 10'000'000);
  o.seed =
      parseNumber<std::uint64_t>(arguments, "--seed", 1, 0, UINT64_MAX / 2);
  o.spread = arguments.has("--spread");
  o.homeRating = parseNumber<float>(arguments, "--home-rating", DEFAULT_RATING,
                                    1.0f, 99.0f);
  o.awayRating = parseNumber<float>(arguments, "--away-rating", DEFAULT_RATING,
                                    1.0f, 99.0f);
  o.minRating = parseNumber<float>(arguments, "--rating-min", SPREAD_MIN_RATING,
                                   1.0f, 99.0f);
  o.maxRating = parseNumber<float>(arguments, "--rating-max", SPREAD_MAX_RATING,
                                   1.0f, 99.0f);
  if (o.spread &&
      (arguments.value("--home-rating") || arguments.value("--away-rating")))
    throw UsageError("--spread cannot be combined with fixed ratings");
  if (o.minRating > o.maxRating) throw UsageError("--rating-min > --rating-max");
  o.background = backgroundFidelity(arguments);
  o.brains = brainPair(arguments);
  o.bodies = bodyPair(arguments);
  o.handshake = handshakeOption(arguments);
  o.homeAdvantage = homeAdvantageOption(arguments);
  o.seams = seamChoices(arguments);
  o.uniform = arguments.has("--uniform");
  o.knockout = arguments.has("--knockout");
  o.knobs = knobsOption(arguments);
  o.formations = formationPair(arguments);
  return o;
}

std::vector<Lab::MatchJob> matchJobs(const MatchesOptions& o)
{
  std::vector<Lab::MatchJob> jobs(static_cast<std::size_t>(o.count));
  for (std::size_t i = 0; i < jobs.size(); ++i)
  {
    jobs[i].seed = Lab::matchSeed(o.seed, i);
    jobs[i].background = o.background;
    if (o.spread)
    {
      const auto uniform = [&](std::uint64_t salt)
      {
        return o.minRating +
               static_cast<float>(Lab::matchSeed(o.seed ^ salt, i) /
                                  TWO_POW_32) *
                   (o.maxRating - o.minRating);
      };
      jobs[i].home.rating = uniform(SPREAD_SALT);
      jobs[i].away.rating = uniform(SPREAD_SALT + 1);
    }
    else
    {
      jobs[i].home.rating = o.homeRating;
      jobs[i].away.rating = o.awayRating;
    }
  }
  setBrainsAndBodies(jobs, o.brains, o.bodies);
  setSeamChoices(jobs, o.seams);
  for (Lab::MatchJob& job : jobs)
  {
    job.home.formation = o.formations[0];
    job.away.formation = o.formations[1];
    job.handshake = o.handshake;
    job.homeAdvantage = o.homeAdvantage;
    job.home.uniform = o.uniform;
    job.away.uniform = o.uniform;
    job.knockout = o.knockout;
    job.touchKnobs = o.knobs.touch;
    job.strikeKnobs = o.knobs.strike;
  }
  return jobs;
}

/** trace: the first --seconds (default 10) of each match that matches would
 * play, every player and the ball every fixed step, to --out (CSV, one run
 * per match, labelled by its index). */
int runTraceMode(const Arguments& arguments)
{
  const MatchesOptions options = matchesOptions(arguments);
  const float seconds =
      parseNumber<float>(arguments, "--seconds", 10.0f, 0.1f, 600.0f);
  const std::optional<std::string> path = arguments.value("--out");
  if (!path) throw UsageError("trace: --out FILE is required");
  const int ticks = static_cast<int>(
      std::lround(seconds / MatchTuning::Timing::FIXED_STEP_SECONDS));
  const std::vector<Lab::MatchJob> jobs = matchJobs(options);
  const StatsConfig config = Lab::loadStatsConfig();
  std::ofstream out(*path, std::ios::binary);
  if (!out) throw std::runtime_error("trace: cannot write " + *path);
  Lab::writeTraceHeader(out);
  for (std::size_t index = 0; index < jobs.size(); ++index)
    Lab::traceMatch(jobs[index], config, ticks, std::format("m{}", index),
                    out);
  std::cerr << std::format("[fm_lab] trace: {} matches, {} ticks each, {}\n",
                           jobs.size(), ticks, *path);
  return out ? Lab::ExitCode::OK : Lab::ExitCode::SIMULATION_ERROR;
}

Lab::LabReport runMatchesMode(const Arguments& arguments, unsigned threads)
{
  const MatchesOptions options = matchesOptions(arguments);
  const std::uint64_t seed = options.seed;
  const bool spread = options.spread;
  const float homeRating = options.homeRating;
  const float awayRating = options.awayRating;
  const float minRating = options.minRating;
  const float maxRating = options.maxRating;
  const bool background = options.background;
  const std::array<BrainId, 2> brains = options.brains;
  const std::array<BodyId, 2> bodies = options.bodies;
  const HandshakeId handshake = options.handshake;
  const float homeAdvantage = options.homeAdvantage;
  const SeamChoices& seams = options.seams;
  const bool uniform = options.uniform;
  const KnobSettings& knobs = options.knobs;
  const std::vector<Lab::MatchJob> jobs = matchJobs(options);

  const StatsConfig config = Lab::loadStatsConfig();
  const auto started = std::chrono::steady_clock::now();
  const auto progress = [&](std::size_t done)
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
  report.parameters["brains"] = brainsParameter(brains);
  report.parameters["bodies"] = bodiesParameter(bodies);
  report.parameters["handshake"] = std::string(handshakeName(handshake));
  reportSeamChoices(report, seams);
  report.parameters["home_advantage"] = std::format("{:.2f}", homeAdvantage);
  report.parameters["squads"] =
      uniform ? "uniform (every stat at the rating)" : "role-shaped";
  if (!knobs.text.empty()) report.parameters["knobs"] = knobs.text;
  report.parameters["formation"] =
      arguments.value("--formation").value_or("4-4-2 (lab)");
  static constexpr std::array SCOPES = {Lab::Scope::Match, Lab::Scope::Engine};
  report.rows = Lab::evaluateTargets(Lab::matchMetrics(samples), SCOPES);
  report.tables = Lab::matchTables(samples);
  report.tables.push_back(Lab::ratingGapTable(samples));
  report.tables.push_back(Lab::perSideTable(samples));
  report.notes.push_back(
      "Synthetic 4-4-2 squads (lab_fixtures.cpp), default strategies, full "
      "condition; per-match seeds are splitmix64(seed, index).");
  if (!spread)
    report.notes.push_back(
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
  const std::array<BrainId, 2> brains = brainPair(arguments);
  const std::array<BodyId, 2> bodies = bodyPair(arguments);
  const HandshakeId handshake = handshakeOption(arguments);
  const float homeAdvantage = homeAdvantageOption(arguments);
  const SeamChoices seams = seamChoices(arguments);
  setBrainsAndBodies(jobs, brains, bodies);
  setSeamChoices(jobs, seams);
  for (Lab::MatchJob& job : jobs)
  {
    job.handshake = handshake;
    job.homeAdvantage = homeAdvantage;
  }
  const StatsConfig config = Lab::loadStatsConfig();
  const auto started = std::chrono::steady_clock::now();
  const std::vector<Lab::MatchSample> samples = Lab::simulateMatches(
      jobs, config, threads,
      [&](std::size_t done)
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
  report.parameters["brains"] = brainsParameter(brains);
  report.parameters["bodies"] = bodiesParameter(bodies);
  report.parameters["handshake"] = std::string(handshakeName(handshake));
  reportSeamChoices(report, seams);
  report.parameters["home_advantage"] = std::format("{:.2f}", homeAdvantage);
  static constexpr std::array SCOPES = {Lab::Scope::Tactics};
  report.rows = Lab::evaluateTargets(metrics, SCOPES);
  report.notes.push_back(
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

/**
 * fm_lab drill: a match sandbox drill measured headless. The match options
 * come first, then the drill's name, then the measure's own options, which
 * go to the sandbox's measure command unchanged.
 */
int runDrillMode(int argc, char** argv)
{
  const std::vector<std::string> rest(argv + 2, argv + argc);
  std::size_t index = 0;
  std::optional<GoalKind> drillGoal;
  for (; index + 1 < rest.size() && rest[index].starts_with("--"); index += 2)
  {
    const std::string& option = rest[index];
    const std::string& value = rest[index + 1];
    if (option == "--brains")
    {
      const auto brains = parseBrainPair(value);
      if (!brains) throw UsageError("--brains must be HOME,AWAY or one brain");
      sandboxBrains() = *brains;
    }
    else if (option == "--bodies")
    {
      const auto bodies = parseBodyPair(value);
      if (!bodies) throw UsageError("--bodies must be HOME,AWAY or one body");
      sandboxBodies() = *bodies;
    }
    else if (option == "--handshake")
    {
      const auto handshake = handshakeFromKey(value);
      if (!handshake)
        throw UsageError("--handshake must be aardvark or binturong");
      sandboxHandshake() = *handshake;
    }
    else if (option == "--referee")
    {
      const auto referee = refereeFromKey(value);
      if (!referee) throw UsageError("--referee must be aardvark or binturong");
      sandboxReferee() = *referee;
    }
    else if (option == "--managers")
    {
      const auto managers = parseManagerPair(value);
      if (!managers)
        throw UsageError("--managers must be HOME,AWAY or one manager");
      sandboxManagers() = *managers;
    }
    else if (option == "--ball")
    {
      const auto ball = ballPhysicsFromKey(value);
      if (!ball) throw UsageError("--ball must be aardvark or binturong");
      sandboxBallPhysics() = *ball;
    }
    else if (option == "--possession")
    {
      const auto version = possessionFromKey(value);
      if (!version) throw UsageError("--possession must be aardvark or binturong");
      sandboxPossession() = *version;
    }
    else if (option == "--out-of-play")
    {
      const auto version = outOfPlayFromKey(value);
      if (!version)
        throw UsageError("--out-of-play must be aardvark or binturong");
      sandboxOutOfPlay() = *version;
    }
    else if (option == "--pitch")
    {
      // A walled training pitch, LENGTHxWIDTH in metres (plan section 18).
      float length = 0.0f;
      float width = 0.0f;
      const std::size_t cross = value.find_first_of("xX");
      try
      {
        if (cross == std::string::npos) throw std::invalid_argument("no x");
        length = std::stof(value.substr(0, cross));
        width = std::stof(value.substr(cross + 1));
      }
      catch (const std::exception&)
      {
        throw UsageError("--pitch must be LENGTHxWIDTH in metres, e.g. 40x30");
      }
      if (length < 5.0f || width < 5.0f || length > 120.0f || width > 90.0f)
        throw UsageError("--pitch: 5 to 120 m long and 5 to 90 m wide");
      sandboxDrillPitch() = MatchPitch::training(length, width);
    }
    else if (option == "--goal")
    {
      // A goal at each end of the --pitch training pitch (plan section 19).
      std::optional<GoalKind> kind;
      for (int index = 0; index <= static_cast<int>(GoalKind::POP_UP); ++index)
        if (value == goalKindName(static_cast<GoalKind>(index)))
          kind = static_cast<GoalKind>(index);
      if (!kind)
        throw UsageError(
            "--goal must be none, full, 9v9, 7v7, futsal, 5-a-side, small or pop-up");
      drillGoal = *kind;
    }
    else
    {
      throw UsageError(std::format(
          "drill: '{}' before the drill's name (only --brains, --bodies, "
          "--handshake, --referee, --managers, --ball, --out-of-play, --possession, "
          "--pitch and --goal go there)",
          option));
    }
  }
  if (drillGoal)
  {
    if (!sandboxDrillPitch())
      throw UsageError("--goal needs --pitch (a training pitch to put the goals on)");
    sandboxDrillPitch()->goal = PitchGoal::ofKind(*drillGoal);
  }
  if (index >= rest.size()) throw UsageError("drill: missing the drill's name");
  const std::string drill = rest[index];
  const std::vector<std::string> options(rest.begin() + static_cast<std::ptrdiff_t>(index) + 1,
                                         rest.end());
  const ScratchRuntime runtime;
  quietLogging();
  return runMeasureCommand(Lab::loadStatsConfig(), drill, options) == 0
             ? Lab::ExitCode::OK
             : Lab::ExitCode::INVALID_ARGUMENTS;
}

/** fm_lab session: runs a training session headless (training ground plan
 * section 23) and prints each exercise's zones' outcomes (with --full,
 * their whole reports), then the session's time. */
int runSessionMode(int argc, char** argv)
{
  const std::vector<std::string> rest(argv + 2, argv + argc);
  std::uint32_t seed = 1;
  bool full = false;
  std::string name;
  for (std::size_t index = 0; index < rest.size(); ++index)
  {
    const std::string& option = rest[index];
    const bool hasValue = index + 1 < rest.size();
    if (option == "--full")
      full = true;
    else if (option == "--brains" && hasValue)
    {
      const auto brains = parseBrainPair(rest[++index]);
      if (!brains) throw UsageError("--brains must be HOME,AWAY or one brain");
      sandboxBrains() = *brains;
    }
    else if (option == "--bodies" && hasValue)
    {
      const auto bodies = parseBodyPair(rest[++index]);
      if (!bodies) throw UsageError("--bodies must be HOME,AWAY or one body");
      sandboxBodies() = *bodies;
    }
    else if (option == "--seed" && hasValue)
      seed = static_cast<std::uint32_t>(std::stoul(rest[++index]));
    else if (!option.starts_with("--") && name.empty())
      name = option;
    else
      throw UsageError(std::format("session: unknown option '{}'", option));
  }
  const auto& plans = sessionPlans();
  const auto found = std::ranges::find_if(plans, [&](const SessionPlan& plan)
                                          { return plan.name == name; });
  if (found == plans.end())
  {
    std::string list;
    for (const SessionPlan& plan : plans) list += std::format(" \"{}\"", plan.name);
    throw UsageError(std::format("session: no session \"{}\". Sessions:{}", name, list));
  }
  if (const std::optional<std::string> problem = checkSessionPlan(*found))
    throw UsageError(std::format("session \"{}\": {}", name, *problem));
  const ScratchRuntime runtime;
  quietLogging();
  const StatsConfig config = Lab::loadStatsConfig();
  SessionRun run(*found, config, seed);
  while (run.step())
  {
  }
  for (std::size_t exercise = 0; exercise < run.history().size(); ++exercise)
  {
    std::cout << found->exercises[exercise].name << "\n";
    for (const std::string& line : run.history()[exercise])
      if (full || line.find("OUTCOME") != std::string::npos) std::cout << "  " << line << "\n";
  }
  std::cout << std::format("Session over at {:.1f} s\n", run.seconds());
  return Lab::ExitCode::OK;
}

int main(int argc, char** argv)
{
  // Lab commands preserve the legacy default, independently of sandbox UI defaults.
  sandboxPossession() = PossessionId::AARDVARK;
  if (argc >= 2 && std::string_view(argv[1]) == "touch-sensitivity")
  {
    try
    {
      const ScratchRuntime runtime;
      quietLogging();
      return runTouchSensitivity(Lab::loadStatsConfig(),
                                 std::vector<std::string>(argv + 2, argv + argc)) ==
                     0
                 ? Lab::ExitCode::OK
                 : Lab::ExitCode::INVALID_ARGUMENTS;
    }
    catch (const std::exception& error)
    {
      std::cerr << "fm_lab: " << error.what() << "\n";
      return Lab::ExitCode::SIMULATION_ERROR;
    }
  }
  if (argc >= 2 && std::string_view(argv[1]) == "session")
  {
    try
    {
      return runSessionMode(argc, argv);
    }
    catch (const UsageError& error)
    {
      std::cerr << "fm_lab: " << error.what() << "\n\n" << USAGE;
      return Lab::ExitCode::INVALID_ARGUMENTS;
    }
    catch (const std::exception& error)
    {
      std::cerr << "fm_lab: " << error.what() << "\n";
      return Lab::ExitCode::SIMULATION_ERROR;
    }
  }
  if (argc >= 2 && std::string_view(argv[1]) == "drill")
  {
    try
    {
      return runDrillMode(argc, argv);
    }
    catch (const UsageError& error)
    {
      std::cerr << "fm_lab: " << error.what() << "\n\n" << USAGE;
      return Lab::ExitCode::INVALID_ARGUMENTS;
    }
    catch (const std::exception& error)
    {
      std::cerr << "fm_lab: " << error.what() << "\n";
      return Lab::ExitCode::SIMULATION_ERROR;
    }
  }
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
        arguments.mode != "tactics" && arguments.mode != "trace")
      throw UsageError(std::format("unknown mode '{}'", arguments.mode));
    if (arguments.mode == "season" && arguments.value("--possession"))
      throw UsageError("--possession is supported by matches, tactics and drill, not season");
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
    if (arguments.mode == "trace") return runTraceMode(arguments);
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
