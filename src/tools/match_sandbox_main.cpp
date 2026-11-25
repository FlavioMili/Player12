// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Match sandbox: open with a splash, then pick two clubs and both
// sides' tactics, and play the match as the home side. Its world is
// generated in a folder of its own (never the game's saves or settings).
//
// Usage: match_sandbox [--world-seed N] [--kick-off] [--drill NAME]
//                      [--check [MATCHES]]
//                      [--measure DRILL --sweep AXIS [--by AXIS]
//                       [--preset NAME] [--set NAME=VALUE]... [--repeats N]
//                       [--seed N] [--csv [PATH]]]
//   --kick-off  start a match with the default setup straight away
//   --drill     open a drill (e.g. sprint) straight away in Watch mode
//   --measure-view  open a drill straight away in Measure mode
//   --check     headless: verify that recording never changes a match and
//               measure simulation and recording costs (no window)
//   --measure   headless: run a drill over a sweep of one setting and print
//               the results (no window). AXIS is "Pace=40:95:12"
//               (from:to:count), "Pace=50,70,90", or a choice setting alone
//               ("With the ball"). --preset fills in a drill's named situation;
//               --set overrides any setting (beyond its slider range too).
//               --csv writes every run (to Documents/Player12 drill results
//               without a path). The same command runs from the lab (fm_lab
//               drill). E.g.
//               match_sandbox --measure turn --sweep Turn --by "With the ball"
//               match_sandbox --measure "First touch" --preset "Driven pass
//               to feet" --sweep "First Touch=30:90:4" --repeats 100
//   --shot-map  headless: the Shot drill over a grid of spots; prints
//               conversion, engine xG and their difference per spot and the
//               calibration. Options: --line from:to:count (metres out),
//               --side from:to:count (metres off centre), --repeats N,
//               --seed N, --set NAME=VALUE (e.g. "Shooter=1" for choice
//               mode, "Keeper=0"), --csv [PATH]
//   --shot-map-view  open the Shot map screen and run the default grid
//   --contact-view  open the contact difficulty view
//   --true-view  open the true view (a player as the engine models him)

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "controller/game_controller.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "tools/match_sandbox_bench.h"
#include "tools/match_sandbox_checks.h"
#include "tools/match_sandbox_measure.h"
#include "tools/match_sandbox_scene.h"
#include "tools/match_sandbox_setup.h"
#include "tools/match_sandbox_shot_map.h"
#include "tools/match_sandbox_splash.h"
#include "tools/match_sandbox_test_teams.h"
#include "tools/match_sandbox_training.h"
#include "tools/match_sandbox_zone_recording.h"

namespace
{
/** Same world every run unless asked otherwise, so clubs stay familiar. */
constexpr std::uint64_t DEFAULT_WORLD_SEED = 1;
constexpr int SANDBOX_SAVE_SLOT = 1;
constexpr int DEFAULT_CHECK_MATCHES = 5;
constexpr int DEFAULT_TRACE_MATCH_CASES = 12;
/** The longest possession trace: a whole hour of play. */
constexpr double MAX_TRACE_POSSESSION_SECONDS = 3600.0;

void useOwnRuntimeRoot(const std::filesystem::path& root)
{
#if !defined(_WIN32)
  if (setenv("FM_RUNTIME_ROOT", root.c_str(), 1) != 0)
    throw std::runtime_error("Cannot set the sandbox runtime directory");
#else
  if (_putenv_s("FM_RUNTIME_ROOT", root.string().c_str()) != 0)
    throw std::runtime_error("Cannot set the sandbox runtime directory");
#endif
}
/** What the command line asked for. */
struct SandboxOptions
{
  std::uint64_t worldSeed = DEFAULT_WORLD_SEED;
  std::optional<int> checkMatches;
  SandboxCheckGroup checkGroup = SandboxCheckGroup::ALL;
  bool kickOff = false;
  std::optional<double> traceLapsSeconds;
  std::optional<double> traceMatchSeconds;
  std::optional<double> tracePossessionSeconds;
  std::string tracePossessionPath;
  int traceMatchCases = DEFAULT_TRACE_MATCH_CASES;
  std::string traceMatchKind = "neither";
  std::string traceLapsFollow;
  bool zoneRoundTrip = false;
  std::string drill;
  SandboxOpenView drillView = SandboxOpenView::WATCH;
  std::optional<std::vector<std::string>> shotMapArguments;
  std::optional<std::vector<std::string>> benchArguments;
  std::optional<std::string> measureDrill;
  std::vector<std::string> measureArguments;
  bool brainsGiven = false;
  bool bodiesGiven = false;
  bool homeAdvantageGiven = false;
  bool handshakeGiven = false;
  bool possessionGiven = false;
};

/** The command line, read one argument at a time. */
class CommandLine
{
 public:
  explicit CommandLine(std::span<char*> arguments) : args(arguments) {}

  [[nodiscard]] bool done() const { return index >= args.size(); }
  [[nodiscard]] std::string_view program() const { return at(0); }
  [[nodiscard]] std::string_view current() const { return at(index); }
  /** Whether `ahead` more arguments follow this one. */
  [[nodiscard]] bool has(std::size_t ahead) const
  {
    return index + ahead < args.size();
  }
  /** The next argument, if there is one and it isn't a flag. */
  [[nodiscard]] bool nextIsValue() const
  {
    return has(1) && at(index + 1)[0] != '-';
  }
  [[nodiscard]] std::string_view peek() const { return at(index + 1); }
  /** Moves on to the next argument and reads it. */
  std::string_view take() { return at(++index); }
  void skip() { ++index; }
  /** Every argument after this one; the line is then used up. */
  std::vector<std::string> rest()
  {
    std::vector<std::string> taken;
    while (++index < args.size())
    {
      taken.emplace_back(at(index));
    }
    return taken;
  }

 private:
  [[nodiscard]] std::string_view at(std::size_t position) const
  {
    return {args[position]};
  }

  std::span<char*> args;
  std::size_t index = 1;
};

/** How a flag parser got on with the current argument. */
enum class Parsed : std::uint8_t
{
  DONE,      // Read it (and any value); go on to the next.
  NOT_MINE,  // Not one of this parser's flags.
  BAD,       // Its flag, but missing or bad: print the usage.
  STOP,      // It took the rest of the line.
  FAILED     // Its flag, already reported: end the run.
};

/** Sets `target` from a parsed value; BAD if it didn't parse. */
template <typename T, typename U>
Parsed choose(const std::optional<U>& parsed, T& target, bool* given = nullptr)
{
  if (!parsed)
  {
    return Parsed::BAD;
  }
  target = *parsed;
  if (given != nullptr)
  {
    *given = true;
  }
  return Parsed::DONE;
}

/** A whole number, if all of `text` is one. */
template <typename T>
std::optional<T> wholeNumber(std::string_view text)
{
  T value{};
  if (std::from_chars(text.data(), text.data() + text.size(), value).ec !=
      std::errc())
  {
    return std::nullopt;
  }
  return value;
}

/** A home advantage scale in range. */
std::optional<float> homeAdvantageScale(std::string_view text)
{
  float scale = 0.0F;
  if (std::from_chars(text.data(), text.data() + text.size(), scale).ec ==
          std::errc() &&
      scale >= 0.0F && scale <= MatchTuning::Context::MAX_HOME_ADVANTAGE_SCALE)
  {
    return scale;
  }
  return std::nullopt;
}

/** The world and the engine versions: flags with one value each. */
Parsed parseEngineChoice(CommandLine& line, SandboxOptions& options)
{
  const std::string_view argument = line.current();
  if (!line.has(1))
  {
    return Parsed::NOT_MINE;
  }
  if (argument == "--world-seed")
  {
    return choose(wholeNumber<std::uint64_t>(line.take()), options.worldSeed);
  }
  if (argument == "--brains")
  {
    return choose(parseBrainPair(line.take()), sandboxBrains(),
                  &options.brainsGiven);
  }
  if (argument == "--bodies")
  {
    return choose(parseBodyPair(line.take()), sandboxBodies(),
                  &options.bodiesGiven);
  }
  if (argument == "--handshake")
  {
    return choose(handshakeFromKey(line.take()), sandboxHandshake(),
                  &options.handshakeGiven);
  }
  if (argument == "--referee")
  {
    return choose(refereeFromKey(line.take()), sandboxReferee());
  }
  if (argument == "--managers")
  {
    return choose(parseManagerPair(line.take()), sandboxManagers());
  }
  if (argument == "--ball")
  {
    return choose(ballPhysicsFromKey(line.take()), sandboxBallPhysics());
  }
  if (argument == "--out-of-play")
  {
    return choose(outOfPlayFromKey(line.take()), sandboxOutOfPlay());
  }
  if (argument == "--possession")
  {
    return choose(possessionFromKey(line.take()), sandboxPossession(),
                  &options.possessionGiven);
  }
  if (argument == "--home-advantage")
  {
    return choose(homeAdvantageScale(line.take()), sandboxHomeAdvantage(),
                  &options.homeAdvantageGiven);
  }
  return Parsed::NOT_MINE;
}

/** Which view the window opens on, and the drill in it. */
Parsed parseViewChoice(CommandLine& line, SandboxOptions& options)
{
  const std::string_view argument = line.current();
  if ((argument == "--measure-view" || argument == "--drill") && line.has(1))
  {
    options.drill = line.take();
    if (argument == "--measure-view")
    {
      options.drillView = SandboxOpenView::MEASURE;
    }
    return Parsed::DONE;
  }
  if (argument == "--shot-map-view")
  {
    options.drill = "shot";
  }
  static constexpr std::array<std::pair<std::string_view, SandboxOpenView>, 4>
      VIEWS{{{"--contact-view", SandboxOpenView::CONTACT},
             {"--bench-view", SandboxOpenView::BENCH},
             {"--true-view", SandboxOpenView::TRUE_VIEW},
             {"--shot-map-view", SandboxOpenView::SHOT_MAP}}};
  for (const auto& [flag, view] : VIEWS)
  {
    if (argument == flag)
    {
      options.drillView = view;
      return Parsed::DONE;
    }
  }
  return Parsed::NOT_MINE;
}

/** --trace-possession SECONDS OUTPUT: seconds in (0, 3600]. */
Parsed parseTracePossession(CommandLine& line, SandboxOptions& options)
{
  const std::string_view duration = line.take();
  double seconds = 0.0;
  const auto parsed = std::from_chars(
      duration.data(), duration.data() + duration.size(), seconds);
  if (parsed.ec != std::errc() ||
      parsed.ptr != duration.data() + duration.size() ||
      !std::isfinite(seconds) || seconds <= 0.0 ||
      seconds > MAX_TRACE_POSSESSION_SECONDS)
  {
    std::cerr << "--trace-possession requires seconds in (0, 3600] and an "
                 "output path.\n";
    return Parsed::FAILED;
  }
  options.tracePossessionSeconds = seconds;
  options.tracePossessionPath = line.take();
  return Parsed::DONE;
}

/** --trace-match SECONDS [CASES [KIND]] and --trace-laps SECONDS [PLAYER]. */
Parsed parseTrace(CommandLine& line, SandboxOptions& options)
{
  const std::string_view argument = line.current();
  if (argument == "--trace-match")
  {
    options.traceMatchSeconds = std::stod(std::string(line.take()));
    if (line.nextIsValue())
    {
      options.traceMatchCases = std::stoi(std::string(line.take()));
    }
    if (line.nextIsValue())
    {
      options.traceMatchKind = line.take();
    }
    return Parsed::DONE;
  }
  options.traceLapsSeconds = std::stod(std::string(line.take()));
  if (line.nextIsValue())
  {
    options.traceLapsFollow = line.take();
  }
  return Parsed::DONE;
}

/** --check [MATCHES] and its single-group forms. */
Parsed parseCheck(CommandLine& line, SandboxOptions& options)
{
  const std::string_view argument = line.current();
  options.checkMatches = DEFAULT_CHECK_MATCHES;
  if (argument == "--check-first-touch")
  {
    options.checkGroup = SandboxCheckGroup::FIRST_TOUCH;
  }
  else if (argument == "--check-restarts")
  {
    options.checkGroup = SandboxCheckGroup::RESTARTS;
  }
  else if (line.has(1))
  {
    const auto matches = wholeNumber<int>(line.peek());
    if (matches && *matches > 0)
    {
      options.checkMatches = *matches;
      line.skip();
    }
  }
  return Parsed::DONE;
}

/** The headless commands, and the window's kick-off. */
Parsed parseCommand(CommandLine& line, SandboxOptions& options)
{
  const std::string_view argument = line.current();
  // These take the rest of the line.
  if (argument == "--shot-map")
  {
    options.shotMapArguments = line.rest();
    return Parsed::STOP;
  }
  if (argument == "--bench")
  {
    options.benchArguments = line.rest();
    return Parsed::STOP;
  }
  if (argument == "--measure" && line.has(1))
  {
    options.measureDrill = line.take();
    options.measureArguments = line.rest();
    return Parsed::STOP;
  }
  if (argument == "--trace-possession" && line.has(2))
  {
    return parseTracePossession(line, options);
  }
  if ((argument == "--trace-match" || argument == "--trace-laps") &&
      line.has(1))
  {
    return parseTrace(line, options);
  }
  if (argument == "--zone-round-trip")
  {
    options.zoneRoundTrip = true;
    return Parsed::DONE;
  }
  if (argument == "--kick-off")
  {
    options.kickOff = true;
    return Parsed::DONE;
  }
  if (argument == "--check" || argument == "--check-first-touch" ||
      argument == "--check-restarts")
  {
    return parseCheck(line, options);
  }
  return Parsed::NOT_MINE;
}

void printUsage(std::string_view program)
{
  std::cerr
      << "Usage: " << program
      << " [--world-seed N] [--brains HOME,AWAY] [--bodies HOME,AWAY]"
         " [--handshake H] [--referee R] [--managers HOME,AWAY] [--ball B]"
         " [--out-of-play O]"
         " [--possession aardvark|binturong]"
         " [--home-advantage X] [--kick-off] [--drill NAME]"
         " [--check [MATCHES]] [--check-first-touch] [--check-restarts] "
         "[--trace-laps SECONDS [PLAYER]] [--zone-round-trip]"
         " [--trace-possession SECONDS OUTPUT.jsonl]"
         " [--trace-match SECONDS [CASES [neither|giveway|spot|all]]]\n"
         "  (--brains, --bodies, --managers: aardvark or binturong per side "
         "(brains also capybara), "
         "one name for both; before --measure or --shot-map)\n"
         "  (--referee: binturong (default) or aardvark; --ball, "
         "--out-of-play: binturong (default) or aardvark)\n"
         "  (--possession: binturong for new sandbox matches; --check "
         "defaults to aardvark unless explicitly selected)\n"
         "  (--home-advantage: 0 neutral venue (default) to 2, 1 the "
         "game's full home edge; --check defaults to 1)\n"
         "  (--handshake: binturong (default) or aardvark; --check "
         "defaults to aardvark)\n"
         "  [--measure DRILL --sweep \"Pace=40:95:12\" [--by \"Who runs\"]\n"
         "   [--set NAME=VALUE]... [--repeats N] [--seed N] [--csv [PATH]]]\n"
         "  [--shot-map [--line A:B:N] [--side A:B:N] [--repeats N]\n"
         "   [--set NAME=VALUE]... [--csv [PATH]]]\n"
         "  [--measure-view DRILL] [--shot-map-view] [--contact-view]"
         " [--bench-view] [--true-view]\n"
         "  [--bench [--tests P0*,S1*] [--tag KEY=VALUE]... [--skills LIST]\n"
         "   [--repeats N] [--seed N] [--threads N] [--skill SETTING]\n"
         "   [--catalogue PATH] [--csv PATH]] (Binturong on both sides\n"
         "   unless --brains/--bodies say otherwise)\n";
}

/** Reads the whole command line into `options`; an exit code if the run
 * should end here (bad usage). */
std::optional<int> parseCommandLine(CommandLine& line, SandboxOptions& options)
{
  for (; !line.done(); line.skip())
  {
    Parsed parsed = parseEngineChoice(line, options);
    if (parsed == Parsed::NOT_MINE)
    {
      parsed = parseViewChoice(line, options);
    }
    if (parsed == Parsed::NOT_MINE)
    {
      parsed = parseCommand(line, options);
    }
    if (parsed == Parsed::STOP)
    {
      break;
    }
    if (parsed == Parsed::FAILED)
    {
      return 2;
    }
    if (parsed != Parsed::DONE)
    {
      printUsage(line.program());
      return 2;
    }
  }
  return std::nullopt;
}

/** The checks compare with the inherited engine, which had the full home
 * edge and the Aardvark handshake; everything else plays at a neutral venue
 * with the Binturong handshake unless asked. */
void applyCheckDefaults(const SandboxOptions& options)
{
  if (!options.checkMatches)
  {
    return;
  }
  if (!options.homeAdvantageGiven)
  {
    sandboxHomeAdvantage() = 1.0F;
  }
  if (!options.handshakeGiven)
  {
    sandboxHandshake() = HandshakeId::AARDVARK;
  }
  if (!options.possessionGiven)
  {
    sandboxPossession() = PossessionId::AARDVARK;
  }
}

/** Binturong on a side unless the command line chose its brains or bodies. */
void defaultToBinturong(const SandboxOptions& options)
{
  if (!options.brainsGiven)
  {
    sandboxBrains() = {BrainId::BINTURONG, BrainId::BINTURONG};
  }
  if (!options.bodiesGiven)
  {
    sandboxBodies() = {BodyId::BINTURONG, BodyId::BINTURONG};
  }
}

/** Team Tiered's players, for the laps trace. */
std::vector<TrainingPlayer> tieredTrainees(const GameController& controller)
{
  std::vector<TrainingPlayer> trainees;
  for (const auto& team : controller.getTeams())
  {
    if (team.get().getName() != "Team Tiered")
    {
      continue;
    }
    for (const auto& player : controller.getPlayersForTeam(team.get().getId()))
    {
      if (trainees.size() < TrainingDrill::MAX_PLAYERS)
      {
        trainees.push_back({.name = player.get().getName(),
                            .role = player.get().getRole(),
                            .stats = player.get().getStats(),
                            .height = player.get().getHeight(),
                            .foot = player.get().getFoot()});
      }
    }
  }
  return trainees;
}

/** The headless commands on the plain world; none if none was asked for. */
std::optional<int> runPlainWorldCommand(GameController& controller,
                                        const SandboxOptions& options)
{
  if (options.checkMatches)
  {
    return runSandboxChecks(controller, *options.checkMatches,
                            options.checkGroup);
  }
  if (options.tracePossessionSeconds)
  {
    return runPossessionTrace(controller, *options.tracePossessionSeconds,
                              options.tracePossessionPath);
  }
  if (options.traceMatchSeconds)
  {
    return runMatchTrace(controller, *options.traceMatchSeconds,
                         options.traceMatchCases, options.traceMatchKind);
  }
  if (options.shotMapArguments)
  {
    return runShotMapCommand(controller.getStatsConfig(),
                             *options.shotMapArguments);
  }
  if (options.benchArguments)
  {
    // The technique bench measures the Binturong body's technique.
    defaultToBinturong(options);
    return runBenchCommand(controller.getStatsConfig(),
                           *options.benchArguments);
  }
  if (options.measureDrill)
  {
    return runMeasureCommand(controller.getStatsConfig(), *options.measureDrill,
                             options.measureArguments);
  }
  return std::nullopt;
}

/** Normal startup: the window opens before the world is generated. */
bool opensOnSplash(const SandboxOptions& options)
{
  return !options.checkMatches && !options.tracePossessionSeconds &&
         !options.traceMatchSeconds && !options.traceLapsSeconds &&
         !options.zoneRoundTrip && !options.shotMapArguments &&
         !options.benchArguments && !options.measureDrill && !options.kickOff &&
         options.drill.empty() && options.drillView == SandboxOpenView::WATCH;
}

/** Generates the world and runs what the options ask for. */
int runWithOptions(const SandboxOptions& options)
{
  GameController controller;
  Logger::info("Match sandbox: generating world " +
               std::to_string(options.worldSeed));
  // Normal startup opens the window before generating the world. Diagnostic
  // commands and explicit match/drill entry points retain their direct path.
  if (opensOnSplash(options))
  {
    GUIView view(controller);
    const std::uint64_t worldSeed = options.worldSeed;
    view.run(
        [&controller, worldSeed](GUIView* gui)
        {
          return std::make_unique<SandboxSplashScene>(
              gui,
              [&controller, worldSeed](std::stop_token stop)
              {
                controller.newGame(SANDBOX_SAVE_SLOT, worldSeed, nullptr, stop);
                if (!stop.stop_requested())
                  addSandboxTestTeams(*controller.getGameData());
              });
        });
    return 0;
  }
  controller.newGame(SANDBOX_SAVE_SLOT, options.worldSeed);
  if (const auto code = runPlainWorldCommand(controller, options))
  {
    return *code;
  }
  // The window opens on Team 66 v Team 66, Binturong for both (every engine
  // version defaults to Binturong). The headless commands above keep the
  // plain world.
  addSandboxTestTeams(*controller.getGameData());
  if (options.zoneRoundTrip)
  {
    return runZoneRoundTrip(controller.getStatsConfig());
  }
  if (options.traceLapsSeconds)
  {
    // Diagnostic: Team Tiered's laps, headless.
    return runLapsTrace(tieredTrainees(controller), controller.getStatsConfig(),
                        *options.traceLapsSeconds, options.traceLapsFollow);
  }
  defaultToBinturong(options);
  GUIView view(controller);
  view.run(
      [kickOff = options.kickOff, drill = options.drill,
       drillView = options.drillView](GUIView* gui)
      {
        return std::make_unique<MatchSandboxScene>(gui, kickOff, drill,
                                                   drillView);
      });
  return 0;
}

/** The sandbox: parses the command line and runs what it asks for. */
int runSandbox(int argc, char** argv)
{
  CommandLine line(std::span<char*>(argv, static_cast<std::size_t>(argc)));
  SandboxOptions options;
  if (const auto code = parseCommandLine(line, options))
  {
    return *code;
  }
  if (options.tracePossessionSeconds &&
      (options.checkMatches || options.traceMatchSeconds ||
       options.traceLapsSeconds || options.measureDrill ||
       options.shotMapArguments || options.benchArguments || options.kickOff ||
       !options.drill.empty()))
  {
    std::cerr << "--trace-possession must be used without another run mode.\n";
    return 2;
  }
  applyCheckDefaults(options);

  // Before anything resolves a runtime path (logs, settings, saves).
  std::optional<ScratchDirectory> checkRuntime;
  if (options.checkMatches || options.zoneRoundTrip)
    checkRuntime.emplace("sandbox-check-runtime");
  useOwnRuntimeRoot(
      checkRuntime ? checkRuntime->path
                   : RuntimePaths::preferenceRoot("FootballManagementSandbox"));
  // Close the log before removing a check's runtime directory on Windows.
  const struct LogCleanup
  {
    ~LogCleanup() { Logger::shutdown(); }
  } logCleanup;
  Logger::init();
  try
  {
    return runWithOptions(options);
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << '\n';
    return 1;
  }
}

}  // namespace

int main(int argc, char* argv[])
{
  // Errors while reading the command line end the run too, not the process.
  try
  {
    return runSandbox(argc, argv);
  }
  // C stdio here: reporting the error must not throw in turn.
  catch (const std::exception& e)
  {
    std::fputs("Error: ", stderr);
    std::fputs(e.what(), stderr);
    std::fputc('\n', stderr);
  }
  catch (...)
  {
    std::fputs("Error: unknown exception\n", stderr);
  }
  return 1;
}
