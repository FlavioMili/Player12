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

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "controller/game_controller.h"
#include "global/logger.h"
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

void useOwnRuntimeRoot()
{
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "player12-match-sandbox";
#if !defined(_WIN32)
  setenv("FM_RUNTIME_ROOT", root.string().c_str(), 1);
#else
  _putenv_s("FM_RUNTIME_ROOT", root.string().c_str());
#endif
}
}  // namespace

int main(int argc, char* argv[])
{
  std::uint64_t worldSeed = DEFAULT_WORLD_SEED;
  std::optional<int> checkMatches;
  SandboxCheckGroup checkGroup = SandboxCheckGroup::ALL;
  bool kickOff = false;
  std::optional<double> traceLapsSeconds;
  std::optional<double> traceMatchSeconds;
  std::optional<double> tracePossessionSeconds;
  std::string tracePossessionPath;
  int traceMatchCases = 12;
  std::string traceMatchKind = "neither";
  std::string traceLapsFollow;
  bool zoneRoundTrip = false;
  std::string drill;
  int drillView = 0;
  std::optional<std::vector<std::string>> shotMapArguments;
  std::optional<std::vector<std::string>> benchArguments;
  std::optional<std::string> measureDrill;
  std::vector<std::string> measureArguments;
  bool brainsGiven = false;
  bool bodiesGiven = false;
  bool homeAdvantageGiven = false;
  bool handshakeGiven = false;
  bool possessionGiven = false;
  for (int index = 1; index < argc; ++index)
  {
    const std::string_view argument(argv[index]);
    if (argument == "--world-seed" && index + 1 < argc)
    {
      const std::string_view value(argv[++index]);
      if (std::from_chars(value.data(), value.data() + value.size(), worldSeed)
              .ec == std::errc())
      {
        continue;
      }
    }
    else if (argument == "--brains" && index + 1 < argc)
    {
      if (const auto brains = parseBrainPair(argv[++index]))
      {
        sandboxBrains() = *brains;
        brainsGiven = true;
        continue;
      }
    }
    else if (argument == "--bodies" && index + 1 < argc)
    {
      if (const auto bodies = parseBodyPair(argv[++index]))
      {
        sandboxBodies() = *bodies;
        bodiesGiven = true;
        continue;
      }
    }
    else if (argument == "--handshake" && index + 1 < argc)
    {
      if (const auto handshake = handshakeFromKey(argv[++index]))
      {
        sandboxHandshake() = *handshake;
        handshakeGiven = true;
        continue;
      }
    }
    else if (argument == "--referee" && index + 1 < argc)
    {
      if (const auto referee = refereeFromKey(argv[++index]))
      {
        sandboxReferee() = *referee;
        continue;
      }
    }
    else if (argument == "--managers" && index + 1 < argc)
    {
      if (const auto managers = parseManagerPair(argv[++index]))
      {
        sandboxManagers() = *managers;
        continue;
      }
    }
    else if (argument == "--ball" && index + 1 < argc)
    {
      if (const auto physics = ballPhysicsFromKey(argv[++index]))
      {
        sandboxBallPhysics() = *physics;
        continue;
      }
    }
    else if (argument == "--out-of-play" && index + 1 < argc)
    {
      if (const auto version = outOfPlayFromKey(argv[++index]))
      {
        sandboxOutOfPlay() = *version;
        continue;
      }
    }
    else if (argument == "--possession" && index + 1 < argc)
    {
      if (const auto version = possessionFromKey(argv[++index]))
      {
        sandboxPossession() = *version;
        possessionGiven = true;
        continue;
      }
    }
    else if (argument == "--home-advantage" && index + 1 < argc)
    {
      const std::string_view value(argv[++index]);
      float scale = 0.0F;
      if (std::from_chars(value.data(), value.data() + value.size(), scale)
                  .ec == std::errc() &&
          scale >= 0.0F &&
          scale <= MatchTuning::Context::MAX_HOME_ADVANTAGE_SCALE)
      {
        sandboxHomeAdvantage() = scale;
        homeAdvantageGiven = true;
        continue;
      }
    }
    else if (argument == "--measure-view" && index + 1 < argc)
    {
      drill = argv[++index];
      drillView = 1;
      continue;
    }
    else if (argument == "--contact-view")
    {
      drillView = 3;
      continue;
    }
    else if (argument == "--bench-view")
    {
      drillView = 4;
      continue;
    }
    else if (argument == "--true-view")
    {
      drillView = 5;
      continue;
    }
    else if (argument == "--shot-map-view")
    {
      drill = "shot";
      drillView = 2;
      continue;
    }
    else if (argument == "--shot-map")
    {
      // The rest of the line belongs to the shot map.
      shotMapArguments.emplace();
      while (++index < argc)
      {
        shotMapArguments->emplace_back(argv[index]);
      }
      break;
    }
    else if (argument == "--drill" && index + 1 < argc)
    {
      drill = argv[++index];
      continue;
    }
    else if (argument == "--bench")
    {
      // The rest of the line belongs to the bench.
      benchArguments.emplace();
      while (++index < argc)
      {
        benchArguments->emplace_back(argv[index]);
      }
      break;
    }
    else if (argument == "--measure" && index + 1 < argc)
    {
      // The rest of the line belongs to the measure.
      measureDrill = argv[++index];
      while (++index < argc)
      {
        measureArguments.emplace_back(argv[index]);
      }
      break;
    }
    else if (argument == "--trace-possession" && index + 2 < argc)
    {
      const std::string_view duration(argv[++index]);
      double seconds = 0.0;
      const auto parsed = std::from_chars(
          duration.data(), duration.data() + duration.size(), seconds);
      if (parsed.ec != std::errc() ||
          parsed.ptr != duration.data() + duration.size() ||
          !std::isfinite(seconds) || seconds <= 0.0 || seconds > 3600.0)
      {
        std::cerr << "--trace-possession requires seconds in (0, 3600] and an "
                     "output path.\n";
        return 2;
      }
      tracePossessionSeconds = seconds;
      tracePossessionPath = argv[++index];
      continue;
    }
    else if (argument == "--trace-match" && index + 1 < argc)
    {
      traceMatchSeconds = std::stod(argv[++index]);
      if (index + 1 < argc && argv[index + 1][0] != '-')
      {
        traceMatchCases = std::stoi(argv[++index]);
      }
      if (index + 1 < argc && argv[index + 1][0] != '-')
      {
        traceMatchKind = argv[++index];
      }
      continue;
    }
    else if (argument == "--trace-laps" && index + 1 < argc)
    {
      traceLapsSeconds = std::stod(argv[++index]);
      if (index + 1 < argc && argv[index + 1][0] != '-')
      {
        traceLapsFollow = argv[++index];
      }
      continue;
    }
    else if (argument == "--zone-round-trip")
    {
      zoneRoundTrip = true;
      continue;
    }
    else if (argument == "--kick-off")
    {
      kickOff = true;
      continue;
    }
    else if (argument == "--check-first-touch")
    {
      checkMatches = DEFAULT_CHECK_MATCHES;
      checkGroup = SandboxCheckGroup::FIRST_TOUCH;
      continue;
    }
    else if (argument == "--check-restarts")
    {
      checkMatches = DEFAULT_CHECK_MATCHES;
      checkGroup = SandboxCheckGroup::RESTARTS;
      continue;
    }
    else if (argument == "--check")
    {
      checkMatches = DEFAULT_CHECK_MATCHES;
      if (index + 1 < argc)
      {
        const std::string_view value(argv[index + 1]);
        int matches = 0;
        if (std::from_chars(value.data(), value.data() + value.size(), matches)
                    .ec == std::errc() &&
            matches > 0)
        {
          checkMatches = matches;
          ++index;
        }
      }
      continue;
    }
    std::cerr
        << "Usage: " << argv[0]
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
    return 2;
  }

  if (tracePossessionSeconds &&
      (checkMatches || traceMatchSeconds || traceLapsSeconds || measureDrill ||
       shotMapArguments || benchArguments || kickOff || !drill.empty()))
  {
    std::cerr << "--trace-possession must be used without another run mode.\n";
    return 2;
  }

  // The checks compare with the inherited engine, which had the full home
  // edge and the Aardvark handshake; everything else plays at a neutral
  // venue with the Binturong handshake unless asked.
  if (checkMatches && !homeAdvantageGiven)
  {
    sandboxHomeAdvantage() = 1.0F;
  }
  if (checkMatches && !handshakeGiven)
  {
    sandboxHandshake() = HandshakeId::AARDVARK;
  }
  if (checkMatches && !possessionGiven)
  {
    sandboxPossession() = PossessionId::AARDVARK;
  }

  // Before anything resolves a runtime path (logs, settings, saves).
  useOwnRuntimeRoot();
  Logger::init();
  try
  {
    GameController controller;
    Logger::info("Match sandbox: generating world " +
                 std::to_string(worldSeed));
    // Normal startup opens the window before generating the world. Diagnostic
    // commands and explicit match/drill entry points retain their direct path.
    const bool splash = !checkMatches && !tracePossessionSeconds &&
                        !traceMatchSeconds && !traceLapsSeconds &&
                        !zoneRoundTrip && !shotMapArguments &&
                        !benchArguments && !measureDrill && !kickOff &&
                        drill.empty() && drillView == 0;
    if (splash)
    {
      GUIView view(controller);
      view.run(
          [&controller, worldSeed](GUIView* gui)
          {
            return std::make_unique<SandboxSplashScene>(
                gui,
                [&controller, worldSeed]
                {
                  controller.newGame(SANDBOX_SAVE_SLOT, worldSeed);
                  addSandboxTestTeams(*controller.getGameData());
                });
          });
      return 0;
    }
    controller.newGame(SANDBOX_SAVE_SLOT, worldSeed);
    if (checkMatches)
    {
      return runSandboxChecks(controller, *checkMatches, checkGroup);
    }
    if (tracePossessionSeconds)
    {
      return runPossessionTrace(controller, *tracePossessionSeconds,
                                tracePossessionPath);
    }
    if (traceMatchSeconds)
    {
      return runMatchTrace(controller, *traceMatchSeconds, traceMatchCases,
                           traceMatchKind);
    }
    if (shotMapArguments)
    {
      return runShotMapCommand(controller.getStatsConfig(), *shotMapArguments);
    }
    if (benchArguments)
    {
      // The technique bench measures the Binturong body's technique.
      if (!brainsGiven)
      {
        sandboxBrains() = {BrainId::BINTURONG, BrainId::BINTURONG};
      }
      if (!bodiesGiven)
      {
        sandboxBodies() = {BodyId::BINTURONG, BodyId::BINTURONG};
      }
      return runBenchCommand(controller.getStatsConfig(), *benchArguments);
    }
    if (measureDrill)
    {
      return runMeasureCommand(controller.getStatsConfig(), *measureDrill,
                               measureArguments);
    }
    // The window opens on Team 66 v Team 66, Binturong for both (every engine
    // version defaults to Binturong). The headless commands above keep the
    // plain world.
    addSandboxTestTeams(*controller.getGameData());
    if (zoneRoundTrip)
    {
      return runZoneRoundTrip(controller.getStatsConfig());
    }
    if (traceLapsSeconds)
    {
      // Diagnostic: Team Tiered's laps, headless.
      std::vector<TrainingPlayer> trainees;
      for (const auto& team : controller.getTeams())
      {
        if (team.get().getName() == "Team Tiered")
        {
          for (const auto& player :
               controller.getPlayersForTeam(team.get().getId()))
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
      }
      return runLapsTrace(trainees, controller.getStatsConfig(),
                          *traceLapsSeconds, traceLapsFollow);
    }
    if (!brainsGiven)
    {
      sandboxBrains() = {BrainId::BINTURONG, BrainId::BINTURONG};
    }
    if (!bodiesGiven)
    {
      sandboxBodies() = {BodyId::BINTURONG, BodyId::BINTURONG};
    }
    GUIView view(controller);
    view.run(
        [kickOff, drill, drillView](GUIView* gui)
        {
          return std::make_unique<MatchSandboxScene>(gui, kickOff, drill,
                                                     drillView);
        });
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
