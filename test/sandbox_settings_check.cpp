// Football Management Project - see LICENSE in the project root.
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "controller/game_controller.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "tools/match_sandbox_drill.h"
#include "tools/match_sandbox_setup.h"
#include "tools/match_sandbox_test_teams.h"
#include "tools/match_sandbox_training.h"

namespace
{
void require(bool condition, const char* message)
{
  if (!condition)
  {
    throw std::runtime_error(message);
  }
}

void selectVersions(bool reverse)
{
  sandboxHomeAdvantage() = reverse ? 1.5F : 0.25F;
  sandboxBrains() = reverse ? std::array{BrainId::BINTURONG, BrainId::AARDVARK}
                            : std::array{BrainId::AARDVARK, BrainId::BINTURONG};
  sandboxBodies() = reverse ? std::array{BodyId::AARDVARK, BodyId::BINTURONG}
                            : std::array{BodyId::BINTURONG, BodyId::AARDVARK};
  sandboxManagers() =
      reverse ? std::array{ManagerId::BINTURONG, ManagerId::AARDVARK}
              : std::array{ManagerId::AARDVARK, ManagerId::BINTURONG};
  sandboxHandshake() = reverse ? HandshakeId::BINTURONG : HandshakeId::AARDVARK;
  sandboxReferee() = reverse ? RefereeId::AARDVARK : RefereeId::BINTURONG;
  sandboxBallPhysics() =
      reverse ? BallPhysicsId::BINTURONG : BallPhysicsId::AARDVARK;
  sandboxOutOfPlay() = reverse ? OutOfPlayId::AARDVARK : OutOfPlayId::BINTURONG;
  sandboxPossession() =
      reverse ? PossessionId::BINTURONG : PossessionId::AARDVARK;
}

void verify(const MatchEngine& engine, const MatchSetup& expected)
{
  for (std::size_t side = 0; side < 2; ++side)
  {
    require(engine.getBrain(side == 0) == expected.brains[side],
            "brain selection lost");
    require(engine.getBody(side == 0) == expected.bodies[side],
            "body selection lost");
    require(engine.getManager(side == 0) == expected.managers[side],
            "manager selection lost");
  }
  require(engine.getHandshake() == expected.handshake,
          "handshake selection lost");
  require(engine.getReferee() == expected.referee, "referee selection lost");
  require(engine.getBallPhysics() == expected.ballPhysics,
          "ball selection lost");
  require(engine.getOutOfPlay() == expected.outOfPlay,
          "out-of-play selection lost");
  require(engine.getPossessionVersion() == expected.possession,
          "possession selection lost");
  require(std::abs(engine.getMatchContext().homeAdvantageScale -
                   expected.homeAdvantage) < 0.00001F,
          "home advantage selection lost");
}
}  // namespace

int main()
{
  try
  {
    // Never touch a career or the interactive sandbox's generated world.
    const ScratchDirectory scratch("sandbox-settings-check");
#ifdef _WIN32
    require(_putenv_s("FM_RUNTIME_ROOT", scratch.path.string().c_str()) == 0,
            "cannot set the test runtime directory");
#else
    require(setenv("FM_RUNTIME_ROOT", scratch.path.c_str(), 1) == 0,
            "cannot set the test runtime directory");
#endif
    // Close the log before removing the scratch directory on Windows.
    const struct LogCleanup
    {
      ~LogCleanup() { Logger::shutdown(); }
    } logCleanup;
    Logger::init();
    GameController controller;
    controller.newGame(1, 1);
    addSandboxTestTeams(*controller.getGameData());
    const auto clubs = sandboxDefaultClubs();
    auto drills = makeDrills();
    auto session = trainingSessions().front().make(
        {{.name = "Runner one",
          .role = PlayerRole::CM,
          .stats = {{"Pace", 66}, {"Stamina", 66}}},
         {.name = "Runner two",
          .role = PlayerRole::CM,
          .stats = {{"Pace", 66}, {"Stamina", 66}}}});
    for (const bool reverse : {false, true})
    {
      selectVersions(reverse);
      const auto expected = MatchSetup::capture(controller, clubs, 7, true);
      DrillRun drill(*drills.front(), controller.getStatsConfig(), false);
      auto training = startTrainingRun(*session->drills.front(),
                                       controller.getStatsConfig());
      verify(drill.engine(), expected);
      verify(training->engine(), expected);
      require(!training->engine().getDrillRules().periods &&
                  training->engine().getDrillRules().chaseEffort,
              "training rules were lost");
      require(sandboxBrains() == expected.brains &&
                  sandboxBodies() == expected.bodies,
              "training mutated shared selections");

      // Changing Set-up affects future runs, not engines or captured matches.
      selectVersions(!reverse);
      verify(drill.engine(), expected);
      verify(training->engine(), expected);
      std::string error;
      auto match = expected.build(controller, error);
      require(match != nullptr, error.c_str());
      verify(*match, expected);
      const auto changed = MatchSetup::capture(controller, clubs, 7, true);
      DrillRun next(*drills.front(), controller.getStatsConfig(), false);
      verify(next.engine(), changed);

      // The deliberately uniform headless diagnostic must restore selections.
      auto diagnostic = startTrainingRun(*session->drills.front(),
                                         controller.getStatsConfig(),
                                         BrainId::BINTURONG, BodyId::BINTURONG);
      require(diagnostic->engine().getBrain(true) == BrainId::BINTURONG &&
                  diagnostic->engine().getBrain(false) == BrainId::BINTURONG,
              "diagnostic override was lost");
      require(sandboxBrains() == changed.brains &&
                  sandboxBodies() == changed.bodies,
              "diagnostic changed subsequent runs");
    }
    std::cout
        << "PASS: mixed engine settings flow to matches, drills and training; "
           "existing runs keep their settings.\n";
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
