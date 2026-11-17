// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <vector>

#include "global/runtime_paths.h"
#include "gui/render/match_render_snapshot.h"
#include "model/match_commentary.h"
#include "model/match_engine.h"
#include "model/match_report.h"
#include "model/player.h"
#include "model/team.h"

namespace
{
// Tests play on a match pitch, placed by fractions of it.
const MatchPitch& PITCH = MatchPitch::matchSize();

StatsConfig createStatsConfig()
{
  StatsConfig config;
  config.possible_stats = {"Pace",      "Shooting",  "Passing",
                           "Dribbling", "Defending", "Physicality",
                           "Stamina",   "Vision",    "Goalkeeping"};
  config.role_focus["Goalkeeper"] = {
      .stats = {"Goalkeeping", "Vision", "Physicality"},
      .weights = {0.7, 0.2, 0.1}};
  config.role_focus["Defender"] = {
      .stats = {"Defending", "Physicality", "Pace", "Vision"},
      .weights = {0.4, 0.3, 0.15, 0.15}};
  config.role_focus["Midfielder"] = {
      .stats = {"Passing", "Vision", "Stamina", "Dribbling"},
      .weights = {0.3, 0.3, 0.2, 0.2}};
  config.role_focus["Striker"] = {
      .stats = {"Shooting", "Pace", "Dribbling", "Physicality"},
      .weights = {0.4, 0.2, 0.2, 0.2}};
  return config;
}

Team createDummyTeam(TeamID id, const std::string& name, int rating,
                     std::vector<std::unique_ptr<Player>>& players)
{
  using enum PlayerRole;
  Team team(id, 1, name, 50'000'000, {}, Strategy{}, Lineup{});
  static constexpr std::array ROLES = {GK, LB, CB, CB, RB, CM,
                                       CM, LW, RW, ST, ST};
  static constexpr std::array<Vector2F, 11> POSITIONS = {
      {{.x = 0.04F, .y = 0.50F},
       {.x = 0.18F, .y = 0.12F},
       {.x = 0.18F, .y = 0.38F},
       {.x = 0.18F, .y = 0.62F},
       {.x = 0.18F, .y = 0.88F},
       {.x = 0.43F, .y = 0.35F},
       {.x = 0.43F, .y = 0.65F},
       {.x = 0.68F, .y = 0.16F},
       {.x = 0.68F, .y = 0.84F},
       {.x = 0.78F, .y = 0.38F},
       {.x = 0.78F, .y = 0.62F}}};

  for (uint32_t index = 0; index < 11; ++index)
  {
    const auto value = static_cast<float>(rating);
    const std::map<std::string, float> stats = {
        {"Pace", value},      {"Shooting", value},  {"Passing", value},
        {"Dribbling", value}, {"Defending", value}, {"Physicality", value},
        {"Stamina", value},   {"Vision", value},    {"Goalkeeping", value}};
    auto player = std::make_unique<Player>(
        (static_cast<PlayerID>(id) * 100U) + index, id, "First",
        std::to_string(index), ROLES[index], Language::EN, 100'000, 0, 25, 3,
        180, Foot::Right, stats);
    if (ROLES[index] == GK)
    {
      team.getLineup().setGoalkeeper(player.get());
    }
    else
    {
      team.getLineup().addOutfieldPlayer(player.get(), POSITIONS[index]);
    }
    players.push_back(std::move(player));
  }
  return team;
}

/** Plays a match through the live update() path at a fixed frame length. */
void simulateToFullTime(MatchEngine& engine, float frameDelta = 0.05F)
{
  // A match lasts at most ~7,200 simulated seconds.
  const auto maxFrames = static_cast<int>(8'000.0F / frameDelta);
  for (int frame = 0;
       frame < maxFrames && engine.getState() != MatchState::FULL_TIME; ++frame)
  {
    engine.update(frameDelta);
  }
}
}  // namespace

TEST(MatchEngineTest, CompletesARealisticMatch)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 65, players);
  Team away = createDummyTeam(2, "Away", 65, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 42);

  simulateToFullTime(engine);

  EXPECT_EQ(engine.getState(), MatchState::FULL_TIME);
  // Real match time: two 45-minute halves plus stoppage and added time.
  EXPECT_GE(engine.getSimulatedSeconds(), 5'400.0);
  EXPECT_LE(engine.getSimulatedSeconds(), 7'300.0);
  // The match ends once the second-half added time has been played.
  const int addedMinutes = engine.getAddedMinutes(2);
  EXPECT_GE(addedMinutes, MatchTuning::Stoppage::MIN_ADDED_MINUTES);
  EXPECT_LE(addedMinutes, MatchTuning::Stoppage::MAX_ADDED_MINUTES);
  EXPECT_GE(engine.getMatchTimeMinutes(), 90.0F + addedMinutes);
  EXPECT_LE(engine.getMatchTimeMinutes(),
            90.0F + MatchTuning::Stoppage::MAX_ADDED_MINUTES +
                MatchTuning::Stoppage::MAX_OVERRUN_MINUTES);
  EXPECT_GE(engine.getAddedMinutes(1),
            MatchTuning::Stoppage::MIN_ADDED_MINUTES);
  ASSERT_FALSE(engine.getEvents().empty());
  EXPECT_EQ(engine.getEvents().back().type, MatchEventType::FULL_TIME);
  const MatchStats& stats = engine.getStats();
  EXPECT_GE(stats.homeShots + stats.awayShots, 8);
  EXPECT_GE(stats.homePassesAttempted + stats.awayPassesAttempted, 400);
  // Goals are shots on target, except own goals credited to the other side.
  int homeOwnGoalsFor = 0;
  int awayOwnGoalsFor = 0;
  for (const PlayerMatchStats& entry : engine.getPlayerStats())
  {
    (entry.isHomeTeam ? awayOwnGoalsFor : homeOwnGoalsFor) += entry.ownGoals;
  }
  EXPECT_LE(engine.getHomeScore(), stats.homeOnTarget + homeOwnGoalsFor);
  EXPECT_LE(engine.getAwayScore(), stats.awayOnTarget + awayOwnGoalsFor);
  EXPECT_NEAR(stats.homePossession + stats.awayPossession, 100.0F, 0.01F);
  EXPECT_NE(engine.getDebugSnapshotJson().find("\"full_time\""),
            std::string::npos);

  const auto snapshotPath = RuntimePaths::capturePath("engine.json");
  std::filesystem::remove(snapshotPath);
  ASSERT_TRUE(engine.writeDebugSnapshot(snapshotPath.string()));
  EXPECT_GT(std::filesystem::file_size(snapshotPath), 100U);
}

TEST(MatchEngineTest, AverageShotVolumeStaysWithinExpectedRange)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 65, players);
  Team away = createDummyTeam(2, "Away", 65, players);
  const StatsConfig config = createStatsConfig();
  constexpr std::uint32_t MATCHES = 64;
  unsigned shots = 0;
  // Individual replays can be outliers. Check the shot-volume band across
  // a fixed sample instead of relying on one seed to represent the average.
  for (std::uint32_t seed = 1; seed <= MATCHES; ++seed)
  {
    MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                       away.getStrategy(), config, seed);
    simulateToFullTime(engine);
    ASSERT_EQ(engine.getState(), MatchState::FULL_TIME);
    const MatchStats& stats = engine.getStats();
    shots += stats.homeShots + stats.awayShots;
  }
  const double averageShots = static_cast<double>(shots) / MATCHES;
  EXPECT_GE(averageShots, 8.0);
  EXPECT_LE(averageShots, 45.0);
}

TEST(MatchEngineTest, SetPieceOwnGoalsKeepTheirRestartOrigin)
{
  constexpr int RATING = 65;
  constexpr float FRAME_SECONDS = 0.05F;
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", RATING, players);
  Team away = createDummyTeam(2, "Away", RATING, players);
  const StatsConfig config = createStatsConfig();
  // This replay contains a real restart delivery deflected into its own goal.
  // Pin the fixture instead of hoping a rare event appears in a seed sample.
  // The split engine's current tuning produces the restart deflection here.
  constexpr std::uint32_t SEED = 8;
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, SEED);
  while (engine.getState() != MatchState::FULL_TIME)
  {
    // A defensive deflection is a replacement flight, not an attacking
    // shot. Its restart origin must survive until the ball crosses goal.
    const bool restartDeflection =
        engine.getBall().possessionState().shotFromSetPiece &&
        !engine.getBall().possessionState().isShot;
    const int goals = engine.getHomeScore() + engine.getAwayScore();
    const int setPieceGoals = engine.getStats().homeSetPieceGoals +
                              engine.getStats().awaySetPieceGoals;
    const std::size_t events = engine.getEvents().size();
    engine.update(FRAME_SECONDS);
    if (!restartDeflection ||
        engine.getHomeScore() + engine.getAwayScore() == goals)
    {
      continue;
    }
    ASSERT_TRUE(
        std::any_of(engine.getEvents().begin() + events,
                    engine.getEvents().end(), [](const MatchEvent& event)
                    { return event.type == MatchEventType::OWN_GOAL; }));
    EXPECT_EQ(engine.getStats().homeSetPieceGoals +
                  engine.getStats().awaySetPieceGoals,
              setPieceGoals + 1);
    return;
  }
  FAIL() << "The pinned replay did not produce its set-piece own goal";
}

TEST(MatchEngineTest, InterpolationSnapshotTracksPreviousFixedStep)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 65, players);
  Team away = createDummyTeam(2, "Away", 65, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 77);

  const auto& initial = engine.getPlayers();
  std::vector<PitchPoint> before;
  for (const auto& matchPlayer : initial)
  {
    before.push_back(matchPlayer.position);
  }

  // A fresh engine mirrors current positions and reports a zero alpha.
  const MatchRenderSnapshot initialSnapshot = buildMatchRenderSnapshot(engine);
  ASSERT_EQ(initialSnapshot.players.size(), before.size());
  EXPECT_EQ(initialSnapshot.interpolationAlpha, 0.0F);
  EXPECT_FLOAT_EQ(initialSnapshot.ball.currentPosition.x,
                  engine.getBall().position.x);
  EXPECT_FLOAT_EQ(initialSnapshot.ball.currentPosition.y,
                  engine.getBall().position.y);
  for (std::size_t index = 0; index < before.size(); ++index)
  {
    EXPECT_FLOAT_EQ(initialSnapshot.players[index].currentPosition.x,
                    before[index].x);
    EXPECT_FLOAT_EQ(initialSnapshot.players[index].currentPosition.y,
                    before[index].y);
    EXPECT_FLOAT_EQ(initialSnapshot.players[index].previousPosition.x,
                    before[index].x);
    EXPECT_FLOAT_EQ(initialSnapshot.players[index].previousPosition.y,
                    before[index].y);
  }

  // One fixed step plus a partial frame: previous positions now equal the
  // pre-step positions and the alpha lies strictly inside (0, 1).
  const PitchPoint ballBefore = engine.getBall().position;
  engine.update(MatchTuning::Timing::FIXED_STEP_SECONDS + 0.005F);
  const auto& previous = engine.getPreviousPlayerPositions();
  ASSERT_EQ(previous.size(), before.size());
  for (std::size_t index = 0; index < before.size(); ++index)
  {
    EXPECT_FLOAT_EQ(previous[index].x, before[index].x);
    EXPECT_FLOAT_EQ(previous[index].y, before[index].y);
  }
  EXPECT_FLOAT_EQ(engine.getPreviousBallPosition().x, ballBefore.x);
  EXPECT_FLOAT_EQ(engine.getPreviousBallPosition().y, ballBefore.y);
  EXPECT_GT(engine.getInterpolationAlpha(), 0.0F);
  EXPECT_LT(engine.getInterpolationAlpha(), 1.0F);

  // The rebuilt snapshot carries the same interpolation endpoints.
  const MatchRenderSnapshot advancedSnapshot = buildMatchRenderSnapshot(engine);
  EXPECT_EQ(advancedSnapshot.interpolationAlpha,
            engine.getInterpolationAlpha());
  for (std::size_t index = 0; index < before.size(); ++index)
  {
    EXPECT_FLOAT_EQ(advancedSnapshot.players[index].previousPosition.x,
                    before[index].x);
  }
}

TEST(MatchEngineTest, SameSeedIsIndependentOfRenderFrameRate)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 65, players);
  Team away = createDummyTeam(2, "Away", 65, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine fine(home.getLineup(), away.getLineup(), home.getStrategy(),
                   away.getStrategy(), config, 1234);
  MatchEngine coarse(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 1234);

  simulateToFullTime(fine, 0.02F);
  simulateToFullTime(coarse, 0.10F);

  EXPECT_EQ(fine.getHomeScore(), coarse.getHomeScore());
  EXPECT_EQ(fine.getAwayScore(), coarse.getAwayScore());
  EXPECT_EQ(fine.getStats().homeShots, coarse.getStats().homeShots);
  EXPECT_EQ(fine.getStats().awayShots, coarse.getStats().awayShots);
  EXPECT_EQ(fine.getStats().homePassesCompleted,
            coarse.getStats().homePassesCompleted);
  EXPECT_EQ(fine.getStats().awayPassesCompleted,
            coarse.getStats().awayPassesCompleted);
}

TEST(MatchEngineTest, DelayedRenderFrameHasBoundedCatchUpWork)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 65, players);
  Team away = createDummyTeam(2, "Away", 65, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 9876);

  engine.update(10.0F);

  EXPECT_EQ(engine.getLastUpdateStepCount(),
            MatchTuning::Timing::MAX_FIXED_STEPS_PER_UPDATE);
  EXPECT_GT(engine.getDroppedSimulationSteps(), 0U);
  EXPECT_NE(engine.getState(), MatchState::FULL_TIME);
  for (const MatchPlayer& player : engine.getPlayers())
  {
    EXPECT_TRUE(std::isfinite(player.position.x));
    EXPECT_TRUE(std::isfinite(player.position.y));
  }
}

TEST(MatchEngineTest, LivePlayAlwaysAssignsPressureOrBallRecovery)
{
  using enum PlayerIntent;
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 65, players);
  Team away = createDummyTeam(2, "Away", 65, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 2468);
  EXPECT_EQ(engine.getHomePhase(), TeamPhase::SET_PIECE);
  EXPECT_EQ(engine.getAwayPhase(), TeamPhase::SET_PIECE);

  constexpr int MAX_KICK_OFF_FRAMES = 100;
  constexpr float TEST_FRAME_SECONDS = 0.05F;
  for (int frame = 0;
       frame < MAX_KICK_OFF_FRAMES && engine.getState() != MatchState::PLAYING;
       ++frame)
  {
    engine.update(TEST_FRAME_SECONDS);
  }
  ASSERT_EQ(engine.getState(), MatchState::PLAYING);
  engine.update(MatchTuning::Timing::FIXED_STEP_SECONDS);
  EXPECT_NE(engine.getLastPassDecision().receiverId, 0U);
  EXPECT_GT(engine.getLastPassDecision().completionProbability, 0.0F);
  EXPECT_LE(engine.getLastPassDecision().completionProbability, 1.0F);
  EXPECT_GE(engine.getLastPassDecision().targetPoint.x, 0.0F);
  EXPECT_LE(engine.getLastPassDecision().targetPoint.x, PITCH.atX(1.0F));
  EXPECT_GE(engine.getLastPassDecision().targetPoint.y, 0.0F);
  EXPECT_LE(engine.getLastPassDecision().targetPoint.y, PITCH.atY(1.0F));
  EXPECT_NE(engine.getDebugSnapshotJson().find("\"last_pass\":{"),
            std::string::npos);
  EXPECT_NE(engine.getDebugSnapshotJson().find("\"completion_probability\":"),
            std::string::npos);
  EXPECT_NE(engine.getDebugSnapshotJson().find("\"team_phase\":{"),
            std::string::npos);

  bool homeEngagesBall = false;
  bool awayEngagesBall = false;
  for (const MatchPlayer& player : engine.getPlayers())
  {
    const bool engagesBall = player.intent == PRESS_BALL ||
                             player.intent == CLAIM_LOOSE_BALL ||
                             player.intent == RECEIVE_PASS;
    if (player.isHomeTeam)
    {
      homeEngagesBall = homeEngagesBall || engagesBall;
    }
    else
    {
      awayEngagesBall = awayEngagesBall || engagesBall;
    }
  }

  if (engine.getBall().possessionState().possessedBy != nullptr)
  {
    const bool homeHasBall = std::ranges::any_of(
        engine.getPlayers(),
        [&engine](const MatchPlayer& player)
        {
          return player.player ==
                     engine.getBall().possessionState().possessedBy &&
                 player.isHomeTeam;
        });
    EXPECT_TRUE(homeHasBall ? awayEngagesBall : homeEngagesBall);
    EXPECT_EQ(homeHasBall ? engine.getAwayPhase() : engine.getHomePhase(),
              TeamPhase::DEFENSIVE_BLOCK);
  }
  else
  {
    EXPECT_TRUE(homeEngagesBall);
    EXPECT_TRUE(awayEngagesBall);
    const bool attackingHome = engine.getBall().possessionState().passByHome;
    if (engine.getTransitionSecondsRemaining() > 0.0F)
    {
      EXPECT_EQ(attackingHome ? engine.getHomePhase() : engine.getAwayPhase(),
                TeamPhase::ATTACKING_TRANSITION);
      EXPECT_EQ(attackingHome ? engine.getAwayPhase() : engine.getHomePhase(),
                TeamPhase::DEFENSIVE_TRANSITION);
    }
    else
    {
      const TeamPhase attackingPhase =
          attackingHome ? engine.getHomePhase() : engine.getAwayPhase();
      EXPECT_TRUE(attackingPhase == TeamPhase::POSSESSION ||
                  attackingPhase == TeamPhase::FINAL_THIRD);
      EXPECT_EQ(attackingHome ? engine.getAwayPhase() : engine.getHomePhase(),
                TeamPhase::DEFENSIVE_BLOCK);
    }

    const bool attackingTeamKeepsSupporting = std::ranges::any_of(
        engine.getPlayers(),
        [attackingHome](const MatchPlayer& player)
        {
          return player.isHomeTeam == attackingHome &&
                 (player.intent == PlayerIntent::RUN_IN_BEHIND ||
                  player.intent == PlayerIntent::OFFER_SUPPORT);
        });
    EXPECT_TRUE(attackingTeamKeepsSupporting);
  }
}

TEST(MatchEngineTest, ControlledPassDoesNotInventAPossessionTransition)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 70, players);
  Team away = createDummyTeam(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 1122);

  constexpr float TEST_FRAME_SECONDS = MatchTuning::Timing::FIXED_STEP_SECONDS;
  constexpr int MAX_OBSERVATION_FRAMES = 1'200;
  bool inspectedControlledPass = false;
  for (int frame = 0; frame < MAX_OBSERVATION_FRAMES; ++frame)
  {
    engine.update(TEST_FRAME_SECONDS);
    const MatchBall& matchBall = engine.getBall();
    if (engine.getState() != MatchState::PLAYING ||
        !matchBall.possessionState().isPass ||
        engine.getTransitionSecondsRemaining() > 0.0F)
    {
      continue;
    }

    const TeamPhase attackingPhase = matchBall.possessionState().passByHome
                                         ? engine.getHomePhase()
                                         : engine.getAwayPhase();
    const TeamPhase defendingPhase = matchBall.possessionState().passByHome
                                         ? engine.getAwayPhase()
                                         : engine.getHomePhase();
    EXPECT_TRUE(attackingPhase == TeamPhase::POSSESSION ||
                attackingPhase == TeamPhase::FINAL_THIRD);
    EXPECT_EQ(defendingPhase, TeamPhase::DEFENSIVE_BLOCK);

    int attackingPlayersRecoveringShape = 0;
    for (const MatchPlayer& player : engine.getPlayers())
    {
      if (player.isHomeTeam == matchBall.possessionState().passByHome &&
          player.player->getRole() != PlayerRole::GK &&
          player.intent == PlayerIntent::RECOVER_SHAPE)
      {
        ++attackingPlayersRecoveringShape;
      }
    }
    EXPECT_EQ(attackingPlayersRecoveringShape, 0)
        << "A controlled pass must preserve attacking support lanes";
    inspectedControlledPass = true;
    break;
  }
  EXPECT_TRUE(inspectedControlledPass);
}

TEST(MatchEngineTest, TurnoverCreatesTimedTeamTransitions)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 70, players);
  Team away = createDummyTeam(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 3344);

  constexpr float TEST_FRAME_SECONDS = 0.05F;
  constexpr int MAX_OBSERVATION_FRAMES = 12'000;
  bool observedTransition = false;
  for (int frame = 0; frame < MAX_OBSERVATION_FRAMES; ++frame)
  {
    engine.update(TEST_FRAME_SECONDS);
    if (engine.getState() != MatchState::PLAYING ||
        (engine.getBall().possessionState().possessedBy == nullptr) ||
        engine.getTransitionSecondsRemaining() <= 0.0F)
    {
      continue;
    }

    const auto carrier = std::ranges::find_if(
        engine.getPlayers(),
        [&engine](const MatchPlayer& player)
        {
          return player.player ==
                 engine.getBall().possessionState().possessedBy;
        });
    ASSERT_NE(carrier, engine.getPlayers().end());
    EXPECT_EQ(
        carrier->isHomeTeam ? engine.getHomePhase() : engine.getAwayPhase(),
        TeamPhase::ATTACKING_TRANSITION);
    EXPECT_EQ(
        carrier->isHomeTeam ? engine.getAwayPhase() : engine.getHomePhase(),
        TeamPhase::DEFENSIVE_TRANSITION);
    EXPECT_LE(engine.getTransitionSecondsRemaining(),
              MatchTuning::Timing::POSSESSION_TRANSITION_SECONDS);
    observedTransition = true;
    break;
  }
  EXPECT_TRUE(observedTransition);
}

TEST(MatchEngineTest, PossessionBuildsDistinctSupportAngles)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 70, players);
  Team away = createDummyTeam(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 5566);

  constexpr float TEST_FRAME_SECONDS = 0.05F;
  constexpr int MAX_OBSERVATION_FRAMES = 1'200;
  constexpr float MAX_SUPPORT_DISTANCE = 0.24F;
  constexpr float MIN_LATERAL_ANGLE = 0.015F;
  bool observedSupportAngles = false;
  for (int frame = 0; frame < MAX_OBSERVATION_FRAMES; ++frame)
  {
    engine.update(TEST_FRAME_SECONDS);
    if (engine.getState() != MatchState::PLAYING ||
        (engine.getBall().possessionState().possessedBy == nullptr))
    {
      continue;
    }

    const auto carrier = std::ranges::find_if(
        engine.getPlayers(),
        [&engine](const MatchPlayer& player)
        {
          return player.player ==
                 engine.getBall().possessionState().possessedBy;
        });
    ASSERT_NE(carrier, engine.getPlayers().end());
    int nearbySupporters = 0;
    bool supportAbove = false;
    bool supportBelow = false;
    for (const MatchPlayer& player : engine.getPlayers())
    {
      if (player.isHomeTeam != carrier->isHomeTeam ||
          player.intent != PlayerIntent::OFFER_SUPPORT)
      {
        continue;
      }
      const float targetDistance = std::hypot(
          PITCH.fractionX(player.movementTarget.x - carrier->position.x),
          PITCH.fractionY(player.movementTarget.y - carrier->position.y));
      if (targetDistance > MAX_SUPPORT_DISTANCE)
      {
        continue;
      }
      ++nearbySupporters;
      const float lateralOffset =
          PITCH.fractionY(player.movementTarget.y - carrier->position.y);
      supportAbove = supportAbove || lateralOffset < -MIN_LATERAL_ANGLE;
      supportBelow = supportBelow || lateralOffset > MIN_LATERAL_ANGLE;
    }
    if (nearbySupporters >= 2 && supportAbove && supportBelow)
    {
      observedSupportAngles = true;
      break;
    }
  }
  EXPECT_TRUE(observedSupportAngles)
      << "Possession should create nearby options on both sides of the carrier";
}

TEST(MatchEngineTest, FinalThirdPossessionKeepsRestDefenseBehindTheBall)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 70, players);
  Team away = createDummyTeam(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 7788);

  constexpr float TEST_FRAME_SECONDS = 0.05F;
  constexpr int MAX_OBSERVATION_FRAMES = 12'000;
  constexpr int MINIMUM_REST_DEFENDERS = 2;
  constexpr float MINIMUM_GOAL_SIDE_GAP = 0.12F;
  bool inspectedFinalThird = false;
  for (int frame = 0; frame < MAX_OBSERVATION_FRAMES; ++frame)
  {
    engine.update(TEST_FRAME_SECONDS);
    if (engine.getState() != MatchState::PLAYING ||
        (engine.getBall().possessionState().possessedBy == nullptr))
    {
      continue;
    }

    const auto carrier = std::ranges::find_if(
        engine.getPlayers(),
        [&engine](const MatchPlayer& player)
        {
          return player.player ==
                 engine.getBall().possessionState().possessedBy;
        });
    ASSERT_NE(carrier, engine.getPlayers().end());
    if (const TeamPhase attackingPhase =
            carrier->isHomeTeam ? engine.getHomePhase() : engine.getAwayPhase();
        attackingPhase != TeamPhase::FINAL_THIRD)
    {
      continue;
    }

    int restDefenders = 0;
    for (const MatchPlayer& player : engine.getPlayers())
    {
      if (player.isHomeTeam != carrier->isHomeTeam ||
          (player.player == nullptr))
      {
        continue;
      }
      const PlayerRole role = player.player->getRole();
      if (const bool defensiveRole = role == PlayerRole::CB ||
                                     role == PlayerRole::LB ||
                                     role == PlayerRole::RB;
          !defensiveRole)
      {
        continue;
      }
      const float goalSideGap =
          carrier->isHomeTeam
              ? PITCH.fractionX(carrier->position.x - player.position.x)
              : PITCH.fractionX(player.position.x - carrier->position.x);
      if (goalSideGap >= MINIMUM_GOAL_SIDE_GAP)
      {
        ++restDefenders;
      }
    }
    EXPECT_GE(restDefenders, MINIMUM_REST_DEFENDERS);
    inspectedFinalThird = true;
    break;
  }
  EXPECT_TRUE(inspectedFinalThird);
}

TEST(MatchEngineTest, PossessionCreatesCoordinatedRunsAndSupport)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 70, players);
  Team away = createDummyTeam(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 8642);

  constexpr float TEST_FRAME_SECONDS = 0.05F;
  constexpr int MAX_POSSESSION_FRAMES = 300;
  constexpr float SIGNIFICANT_TACTICAL_MOVEMENT = 0.08F;
  constexpr int MIN_SIGNIFICANT_TEAM_MOVEMENTS = 3;
  bool inspectedPossession = false;
  for (int frame = 0; frame < MAX_POSSESSION_FRAMES; ++frame)
  {
    engine.update(TEST_FRAME_SECONDS);
    if (engine.getState() != MatchState::PLAYING ||
        (engine.getBall().possessionState().possessedBy == nullptr))
    {
      continue;
    }

    // Possession can be established after movement resolution. Advance one
    // fixed step so the team has applied its possession-phase assignments.
    engine.update(MatchTuning::Timing::FIXED_STEP_SECONDS);
    if (engine.getBall().possessionState().possessedBy == nullptr)
    {
      continue;
    }

    const auto carrier = std::ranges::find_if(
        engine.getPlayers(),
        [&engine](const MatchPlayer& player)
        {
          return player.player ==
                 engine.getBall().possessionState().possessedBy;
        });
    ASSERT_NE(carrier, engine.getPlayers().end());

    int committedRuns = 0;
    int supportOptions = 0;
    int significantMovements = 0;
    for (const MatchPlayer& player : engine.getPlayers())
    {
      if (player.isHomeTeam != carrier->isHomeTeam ||
          player.player == nullptr ||
          player.player->getRole() == PlayerRole::GK)
      {
        continue;
      }
      if (player.isMakingRun)
      {
        ++committedRuns;
      }
      if (player.intent == PlayerIntent::OFFER_SUPPORT)
      {
        ++supportOptions;
      }
      const float movement = std::hypot(
          PITCH.fractionX(player.movementTarget.x - player.basePosition.x),
          PITCH.fractionY(player.movementTarget.y - player.basePosition.y));
      if (movement > SIGNIFICANT_TACTICAL_MOVEMENT)
      {
        ++significantMovements;
      }
    }

    EXPECT_GE(committedRuns,
              static_cast<int>(MatchTuning::Shape::MIN_COMMITTED_RUNNERS));
    EXPECT_LE(committedRuns,
              static_cast<int>(MatchTuning::Shape::MAX_COMMITTED_RUNNERS));
    EXPECT_GE(supportOptions, 2);
    EXPECT_GE(significantMovements, MIN_SIGNIFICANT_TEAM_MOVEMENTS);
    inspectedPossession = true;
    break;
  }
  EXPECT_TRUE(inspectedPossession);
}

TEST(MatchEngineTest, MovementAssignmentsRemainCoherentAcrossLivePlay)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 70, players);
  Team away = createDummyTeam(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 97531);

  constexpr float TEST_FRAME_SECONDS = 0.05F;
  constexpr int OBSERVATION_FRAMES = 1'200;
  constexpr float MINIMUM_NON_OVERLAP_METRES = 0.25F;
  float observedMinimumSeparation = std::numeric_limits<float>::max();
  for (int frame = 0; frame < OBSERVATION_FRAMES; ++frame)
  {
    engine.update(TEST_FRAME_SECONDS);
    int homeRunners = 0;
    int awayRunners = 0;
    int homeReceivers = 0;
    int awayReceivers = 0;
    const auto& matchPlayers = engine.getPlayers();
    for (const MatchPlayer& player : matchPlayers)
    {
      EXPECT_TRUE(std::isfinite(player.position.x));
      EXPECT_TRUE(std::isfinite(player.position.y));
      EXPECT_TRUE(std::isfinite(player.movementTarget.x));
      EXPECT_TRUE(std::isfinite(player.movementTarget.y));
      EXPECT_GE(player.position.x, MatchTuning::Pitch::PLAYER_EDGE_END_METRES);
      EXPECT_LE(player.position.x,
                PITCH.length - MatchTuning::Pitch::PLAYER_EDGE_END_METRES);
      EXPECT_GE(player.position.y, MatchTuning::Pitch::PLAYER_EDGE_SIDE_METRES);
      EXPECT_LE(player.position.y,
                PITCH.width - MatchTuning::Pitch::PLAYER_EDGE_SIDE_METRES);
      if (player.isMakingRun)
      {
        if (player.isHomeTeam)
        {
          ++homeRunners;
        }
        else
        {
          ++awayRunners;
        }
      }
      if (player.intent == PlayerIntent::RECEIVE_PASS)
      {
        if (player.isHomeTeam)
        {
          ++homeReceivers;
        }
        else
        {
          ++awayReceivers;
        }
      }
    }

    EXPECT_LE(homeRunners,
              static_cast<int>(MatchTuning::Shape::MAX_COMMITTED_RUNNERS));
    EXPECT_LE(awayRunners,
              static_cast<int>(MatchTuning::Shape::MAX_COMMITTED_RUNNERS));
    EXPECT_LE(homeReceivers, 1);
    EXPECT_LE(awayReceivers, 1);
    if (engine.getBall().possessionState().isPass)
    {
      EXPECT_EQ(engine.getBall().possessionState().passByHome ? homeRunners
                                                              : awayRunners,
                0)
          << "A normal pass must not make every forward start a transition run";
    }

    for (std::size_t first = 0; first < matchPlayers.size(); ++first)
    {
      for (std::size_t second = first + 1; second < matchPlayers.size();
           ++second)
      {
        const float dxMetres =
            matchPlayers[second].position.x - matchPlayers[first].position.x;
        const float dyMetres =
            matchPlayers[second].position.y - matchPlayers[first].position.y;
        observedMinimumSeparation =
            std::min(observedMinimumSeparation, std::hypot(dxMetres, dyMetres));
      }
    }
  }

  RecordProperty("minimum_player_separation_metres",
                 std::to_string(observedMinimumSeparation));
  EXPECT_GT(observedMinimumSeparation, MINIMUM_NON_OVERLAP_METRES);
}

TEST(MatchEngineTest, StrongerTeamHasStatisticalAdvantage)
{
  std::vector<std::unique_ptr<Player>> players;
  // A wide but professional gap (attributes are stretched around a typical
  // level, so 88 against 42 would be a top side against amateurs).
  Team strong = createDummyTeam(1, "Strong", 82, players);
  Team weak = createDummyTeam(2, "Weak", 52, players);
  const StatsConfig config = createStatsConfig();

  int strongGoals = 0;
  int weakGoals = 0;
  int setPieceGoals = 0;
  int penaltyGoals = 0;
  int headedGoals = 0;
  int strongShots = 0;
  int totalGoals = 0;
  int totalShots = 0;
  int totalPassesAttempted = 0;
  int totalPassesCompleted = 0;
  int totalPurposefulPasses = 0;
  int totalCrosses = 0;
  int totalCutbacks = 0;
  float totalExpectedGoals = 0.0F;
  constexpr int SAMPLE_MATCHES = 32;
  for (uint32_t seed = 1; seed <= SAMPLE_MATCHES; ++seed)
  {
    MatchEngine engine(strong.getLineup(), weak.getLineup(),
                       strong.getStrategy(), weak.getStrategy(), config, seed);
    simulateToFullTime(engine, 0.1F);
    setPieceGoals += engine.getStats().homeSetPieceGoals;
    penaltyGoals += engine.getStats().homePenaltyGoals;
    headedGoals += engine.getStats().homeHeadedGoals;
    strongShots += engine.getStats().homeShots;
    strongGoals += engine.getHomeScore();
    weakGoals += engine.getAwayScore();
    totalGoals += engine.getHomeScore() + engine.getAwayScore();
    totalShots += engine.getStats().homeShots + engine.getStats().awayShots;
    totalExpectedGoals +=
        engine.getStats().homeShotXG + engine.getStats().awayShotXG;
    totalPassesAttempted += engine.getStats().homePassesAttempted +
                            engine.getStats().awayPassesAttempted;
    totalPassesCompleted += engine.getStats().homePassesCompleted +
                            engine.getStats().awayPassesCompleted;
    totalCrosses +=
        engine.getStats().homeCrosses + engine.getStats().awayCrosses;
    totalCutbacks +=
        engine.getStats().homeCutbacks + engine.getStats().awayCutbacks;
    totalPurposefulPasses +=
        engine.getStats().homeProgressivePasses +
        engine.getStats().awayProgressivePasses +
        engine.getStats().homeThroughBalls +
        engine.getStats().awayThroughBalls + engine.getStats().homeCrosses +
        engine.getStats().awayCrosses + engine.getStats().homeCutbacks +
        engine.getStats().awayCutbacks + engine.getStats().homeSwitchesOfPlay +
        engine.getStats().awaySwitchesOfPlay;
  }

  const float goalsPerMatch =
      static_cast<float>(totalGoals) / static_cast<float>(SAMPLE_MATCHES);
  const float shotsPerMatch =
      static_cast<float>(totalShots) / static_cast<float>(SAMPLE_MATCHES);
  const float expectedGoalsPerShot =
      totalShots > 0 ? totalExpectedGoals / static_cast<float>(totalShots)
                     : 0.0F;
  const float passCompletion =
      totalPassesAttempted > 0 ? static_cast<float>(totalPassesCompleted) /
                                     static_cast<float>(totalPassesAttempted)
                               : 0.0F;
  const float purposefulPassShare =
      totalPassesAttempted > 0 ? static_cast<float>(totalPurposefulPasses) /
                                     static_cast<float>(totalPassesAttempted)
                               : 0.0F;
  RecordProperty("sample_matches", SAMPLE_MATCHES);
  RecordProperty("shots_per_match", std::to_string(shotsPerMatch));
  RecordProperty("goals_per_match", std::to_string(goalsPerMatch));
  RecordProperty("expected_goals_per_shot",
                 std::to_string(expectedGoalsPerShot));
  RecordProperty("pass_completion", std::to_string(passCompletion));
  RecordProperty("purposeful_pass_share", std::to_string(purposefulPassShare));
  RecordProperty("crosses", totalCrosses);
  RecordProperty("cutbacks", totalCutbacks);
  RecordProperty("strong_goals", strongGoals);
  RecordProperty("weak_goals", weakGoals);
  EXPECT_GT(strongGoals, weakGoals);
  EXPECT_GT(shotsPerMatch, 12.0F) << "goals/match=" << goalsPerMatch;
  EXPECT_LT(shotsPerMatch, 36.0F) << "goals/match=" << goalsPerMatch;
  EXPECT_GT(goalsPerMatch, 1.0F) << "shots/match=" << shotsPerMatch;
  EXPECT_LT(goalsPerMatch, 5.5F)
      << "shots/match=" << shotsPerMatch << " strong=" << strongGoals
      << " weak=" << weakGoals << " xG/shot=" << expectedGoalsPerShot
      << " strongShots=" << strongShots << " setPieceGoals=" << setPieceGoals
      << " penaltyGoals=" << penaltyGoals << " headedGoals=" << headedGoals;
  EXPECT_GT(expectedGoalsPerShot, 0.04F);
  EXPECT_LT(expectedGoalsPerShot, 0.28F);
  EXPECT_GT(passCompletion, 0.60F);
  EXPECT_LT(passCompletion, 0.95F);
  EXPECT_GT(purposefulPassShare, 0.15F);
  EXPECT_LT(purposefulPassShare, 0.90F);
  EXPECT_GT(totalCrosses + totalCutbacks, 0);
}

TEST(MatchEngineTest, ScoredGoalCelebratesBeforeKickoff)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 88, players);
  Team away = createDummyTeam(2, "Away", 42, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 12'345);

  int celebrations = 0;
  int goalsByEvent = 0;
  bool sawBallBeyondLine = false;
  bool wasInGoal = false;
  int lastTotalScore = engine.getHomeScore() + engine.getAwayScore();

  for (int frame = 0;
       frame < 200'000 && engine.getState() != MatchState::FULL_TIME; ++frame)
  {
    const bool inGoal = engine.getState() == MatchState::GOAL;
    if (inGoal && !wasInGoal)
    {
      ++celebrations;
      ++lastTotalScore;
    }
    if (inGoal)
    {
      const MatchBall& ball = engine.getBall();
      if (engine.getGoalScoredByHome() && ball.position.x > PITCH.atX(1.0F))
      {
        sawBallBeyondLine = true;
      }
      if (!engine.getGoalScoredByHome() && ball.position.x < PITCH.atX(0.0F))
      {
        sawBallBeyondLine = true;
      }
      EXPECT_GT(engine.getGoalCelebrationRemaining(), 0.0F);
    }
    wasInGoal = inGoal;
    engine.update(0.05F);
  }

  for (const MatchEvent& event : engine.getEvents())
  {
    if (event.type != MatchEventType::GOAL &&
        event.type != MatchEventType::OWN_GOAL)
    {
      continue;
    }
    ++goalsByEvent;
    EXPECT_FALSE(event.description.empty());
  }

  EXPECT_EQ(engine.getState(), MatchState::FULL_TIME)
      << "The goal celebration must complete and the match must finish";
  EXPECT_GT(lastTotalScore, 0)
      << "The test seed must produce at least one goal";
  EXPECT_EQ(celebrations, goalsByEvent);
  EXPECT_TRUE(sawBallBeyondLine)
      << "The scored ball must visibly enter the net past the goal line";
}

namespace
{
/** A full squad: the dummy starting XI plus a seven-player bench. */
Team createSquadWithBench(TeamID id, const std::string& name, int rating,
                          std::vector<std::unique_ptr<Player>>& players)
{
  using enum PlayerRole;
  Team team = createDummyTeam(id, name, rating, players);
  static constexpr std::array BENCH = {GK, CB, RB, CM, LW, ST, ST};
  std::vector<const Player*> reserves;
  for (uint32_t index = 0; index < 7; ++index)
  {
    const auto value = static_cast<float>(rating);
    const std::map<std::string, float> stats = {
        {"Pace", value},      {"Shooting", value},  {"Passing", value},
        {"Dribbling", value}, {"Defending", value}, {"Physicality", value},
        {"Stamina", value},   {"Vision", value},    {"Goalkeeping", value}};
    auto player = std::make_unique<Player>(
        (static_cast<PlayerID>(id) * 100U) + 50U + index, id, "Bench",
        std::to_string(index), BENCH[index], Language::EN, 100'000, 0, 25, 3,
        182, Foot::Right, stats);
    reserves.push_back(player.get());
    players.push_back(std::move(player));
  }
  team.getLineup().setReserves(reserves);
  return team;
}

int activePlayers(const MatchEngine& engine, bool homeTeam)
{
  return static_cast<int>(std::ranges::count_if(
      engine.getPlayers(), [homeTeam](const MatchPlayer& player)
      { return player.onPitch && player.isHomeTeam == homeTeam; }));
}

/** Advances fixed steps until play next stops (a new restart begins). */
bool advanceToNextStoppage(MatchEngine& engine)
{
  using enum MatchState;
  bool sawPlay = false;
  for (int step = 0; step < 20'000; ++step)
  {
    if (engine.getState() == FULL_TIME)
    {
      return false;
    }
    engine.update(MatchTuning::Timing::FIXED_STEP_SECONDS);
    const MatchState state = engine.getState();
    if (state == PLAYING)
    {
      sawPlay = true;
    }
    else if (sawPlay && state != GOAL && state != FULL_TIME)
    {
      return true;
    }
  }
  return false;
}
}  // namespace

TEST(MatchEngineTest, StructuredEventsAndPlayerStatsAreConsistent)
{
  using enum MatchEventType;
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 72, players);
  Team away = createSquadWithBench(2, "Away", 64, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 2024);
  simulateToFullTime(engine, 0.1F);
  ASSERT_EQ(engine.getState(), MatchState::FULL_TIME);

  int homeGoals = 0;
  int awayGoals = 0;
  int homePassesAttempted = 0;
  int awayPassesAttempted = 0;
  for (const PlayerMatchStats& entry : engine.getPlayerStats())
  {
    EXPECT_NE(entry.playerId, 0U);
    EXPECT_LE(entry.passesCompleted, entry.passesAttempted);
    EXPECT_LE(entry.shotsOnTarget, entry.shots);
    EXPECT_LE(entry.goals, entry.shotsOnTarget);
    EXPECT_GE(entry.rating, MatchTuning::Rating::MINIMUM);
    EXPECT_LE(entry.rating, MatchTuning::Rating::MAXIMUM);
    EXPECT_GE(entry.condition, MatchTuning::Player::MINIMUM_STAMINA);
    EXPECT_LE(entry.condition, 1.0F);
    (entry.isHomeTeam ? homeGoals : awayGoals) += entry.goals;
    (entry.isHomeTeam ? awayGoals : homeGoals) += entry.ownGoals;
    (entry.isHomeTeam ? homePassesAttempted : awayPassesAttempted) +=
        entry.passesAttempted;
    if (entry.started && !entry.substitutedOff && !entry.sentOff &&
        !entry.injured)
    {
      // A full-match player is on the pitch for every clock minute played.
      EXPECT_NEAR(entry.minutesPlayed, engine.getElapsedMatchMinutes(), 0.05F);
      if (entry.role != PlayerRole::GK)
      {
        EXPECT_GT(entry.distanceMetres, 4'000.0F);
      }
    }
  }
  EXPECT_EQ(homeGoals, engine.getHomeScore());
  EXPECT_EQ(awayGoals, engine.getAwayScore());
  EXPECT_EQ(homePassesAttempted, engine.getStats().homePassesAttempted);
  EXPECT_EQ(awayPassesAttempted, engine.getStats().awayPassesAttempted);

  int goalEvents = 0;
  int addedTimeEvents = 0;
  bool sawSecondHalf = false;
  for (const MatchEvent& event : engine.getEvents())
  {
    EXPECT_TRUE(event.period == 1 || event.period == 2);
    EXPECT_GE(event.addedMinute, 0.0F);
    if (event.type == GOAL || event.type == OWN_GOAL)
    {
      ++goalEvents;
      EXPECT_TRUE(event.hasTeam);
      EXPECT_NE(event.primaryPlayerId, 0U);
      EXPECT_STREQ(MatchCommentary::key(event), event.type == GOAL
                                                    ? "MATCH_COMMENT_GOAL"
                                                    : "MATCH_COMMENT_OWN_GOAL");
      EXPECT_EQ(event.homeScore + event.awayScore, goalEvents)
          << "goal events carry the score after the goal";
    }
    if (event.type == SHOT)
    {
      EXPECT_GT(event.xg, 0.0F);
      EXPECT_NE(event.primaryPlayerId, 0U);
    }
    if (event.type == ADDED_TIME)
    {
      ++addedTimeEvents;
    }
    if (event.type == SECOND_HALF)
    {
      sawSecondHalf = true;
      EXPECT_FLOAT_EQ(event.timeMinute, MatchTuning::Timing::HALF_TIME_MINUTE);
      EXPECT_EQ(event.period, 2);
    }
    if (event.type == HALF_TIME)
    {
      EXPECT_GE(event.timeMinute,
                MatchTuning::Timing::HALF_TIME_MINUTE +
                    static_cast<float>(engine.getAddedMinutes(1)));
    }
    if (event.type == SUBSTITUTION)
    {
      EXPECT_NE(event.primaryPlayerId, 0U);
      EXPECT_NE(event.secondaryPlayerId, 0U);
    }
  }
  EXPECT_EQ(goalEvents, engine.getHomeScore() + engine.getAwayScore());
  EXPECT_EQ(addedTimeEvents, 2);
  EXPECT_TRUE(sawSecondHalf);
  EXPECT_NE(engine.getDebugSnapshotJson().find("\"clock\":{\"period\":2"),
            std::string::npos);
}

TEST(MatchEngineTest, SentOffPlayerLeavesThePitchForGood)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 65, players);
  Team away = createSquadWithBench(2, "Away", 65, players);
  const StatsConfig config = createStatsConfig();

  bool inspected = false;
  for (uint32_t seed = 1; seed <= 400 && !inspected; ++seed)
  {
    MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                       away.getStrategy(), config, seed);
    std::size_t seenEvents = 0;
    PlayerID sentOff = 0;
    bool sentOffHome = false;
    while (engine.getState() != MatchState::FULL_TIME)
    {
      engine.update(0.1F);
      const auto& events = engine.getEvents();
      for (; seenEvents < events.size() && sentOff == 0; ++seenEvents)
      {
        const MatchEvent& event = events[seenEvents];
        if (event.type != MatchEventType::RED_CARD &&
            event.type != MatchEventType::SECOND_YELLOW)
        {
          continue;
        }
        sentOff = event.primaryPlayerId;
        sentOffHome = event.isHomeTeam;
      }
      if (sentOff == 0)
      {
        continue;
      }
      for (const MatchPlayer& player : engine.getPlayers())
      {
        if ((player.player != nullptr) && player.player->getId() == sentOff)
        {
          EXPECT_FALSE(player.onPitch);
        }
      }
      EXPECT_LE(activePlayers(engine, sentOffHome), 10);
      EXPECT_NE(engine.getBall().possessionState().possessedBy &&
                    engine.getBall().possessionState().possessedBy->getId() ==
                        sentOff,
                true);
      inspected = true;
    }
    if (sentOff != 0)
    {
      const PlayerMatchStats* entry = engine.findPlayerStats(sentOff);
      ASSERT_NE(entry, nullptr);
      EXPECT_TRUE(entry->sentOff);
      EXPECT_EQ(entry->redCards, 1);
      // A dismissed player cannot be replaced.
      EXPECT_FALSE(engine.substitutePlayer(sentOff, players.back().get()));
    }
  }
  EXPECT_TRUE(inspected) << "no red card in the sampled seeds";
}

TEST(MatchEngineTest, InjuredPlayersAreReplacedOrLeaveThePitch)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 65, players);
  Team away = createSquadWithBench(2, "Away", 65, players);
  const StatsConfig config = createStatsConfig();

  int injuriesChecked = 0;
  for (uint32_t seed = 1; seed <= 120 && injuriesChecked < 3; ++seed)
  {
    MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                       away.getStrategy(), config, seed);
    simulateToFullTime(engine, 0.1F);
    for (const MatchEvent& event : engine.getEvents())
    {
      if (event.type != MatchEventType::INJURY)
      {
        continue;
      }
      const PlayerMatchStats* entry =
          engine.findPlayerStats(event.primaryPlayerId);
      ASSERT_NE(entry, nullptr);
      EXPECT_TRUE(entry->injured);
      const bool replaced = std::ranges::any_of(
          engine.getSubstitutions(),
          [&event](const MatchSubstitution& change)
          {
            return change.outgoingPlayerId == event.primaryPlayerId &&
                   change.reason == SubstitutionReason::INJURY;
          });
      const bool stillOn = std::ranges::any_of(
          engine.getPlayers(),
          [&event](const MatchPlayer& player)
          {
            return player.onPitch && player.player &&
                   player.player->getId() == event.primaryPlayerId;
          });
      // Injured players leave at the next stoppage unless play never stopped
      // again before full time.
      EXPECT_TRUE(replaced || !stillOn ||
                  event.timeMinute > engine.getMatchTimeMinutes() - 2.0F)
          << "injured player " << event.primaryPlayerId << " kept playing";
      ++injuriesChecked;
    }
  }
  EXPECT_GT(injuriesChecked, 0) << "no injury in the sampled seeds";
}

TEST(MatchEngineTest, AiSubstitutionsRespectTheLaws)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 66, players);
  Team away = createSquadWithBench(2, "Away", 66, players);
  const StatsConfig config = createStatsConfig();

  int totalSubstitutions = 0;
  for (uint32_t seed = 1; seed <= 20; ++seed)
  {
    MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                       away.getStrategy(), config, seed * 31U);
    simulateToFullTime(engine, 0.1F);
    for (const bool side : {true, false})
    {
      EXPECT_LE(engine.getSubstitutionsUsed(side),
                MatchTuning::Rules::MAX_SUBSTITUTIONS_PER_TEAM);
      EXPECT_LE(engine.getSubstitutionWindowsUsed(side),
                MatchTuning::Substitution::MAX_WINDOWS);
    }
    std::vector<PlayerID> leftThePitch;
    for (const MatchSubstitution& change : engine.getSubstitutions())
    {
      ++totalSubstitutions;
      // Incoming players come from the bench and nobody returns.
      EXPECT_GE(change.incomingPlayerId % 100U, 50U);
      EXPECT_EQ(std::ranges::count(leftThePitch, change.incomingPlayerId), 0);
      leftThePitch.push_back(change.outgoingPlayerId);
      EXPECT_NE(change.reason, SubstitutionReason::MANUAL);
    }
  }
  EXPECT_GT(totalSubstitutions, 20 * 2 * 2)
      << "AI managers should use most of their substitutions";
}

TEST(MatchEngineTest, ManualSubstitutionsFollowLimitsAndWindows)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 66, players);
  Team away = createSquadWithBench(2, "Away", 66, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 99);
  engine.setAutoSubstitutions(false, false);
  const auto& bench = home.getLineup().getReserves();

  // Two changes at the kick-off stoppage share one window.
  EXPECT_TRUE(engine.substitutePlayer(109, bench[5]));
  EXPECT_TRUE(engine.substitutePlayer(110, bench[6]));
  EXPECT_EQ(engine.getSubstitutionWindowsUsed(true), 1);
  // Nobody can come back on, and a bench player cannot come on twice.
  EXPECT_FALSE(engine.substitutePlayer(107, bench[5]));
  EXPECT_FALSE(engine.substitutePlayer(107, players[9].get()));

  ASSERT_TRUE(advanceToNextStoppage(engine));
  EXPECT_TRUE(engine.substitutePlayer(107, bench[4]));
  EXPECT_EQ(engine.getSubstitutionWindowsUsed(true), 2);
  ASSERT_TRUE(advanceToNextStoppage(engine));
  EXPECT_TRUE(engine.substitutePlayer(105, bench[3]));
  EXPECT_EQ(engine.getSubstitutionWindowsUsed(true), 3);
  ASSERT_TRUE(advanceToNextStoppage(engine));
  // The three windows are used: a fourth stoppage cannot open another.
  EXPECT_FALSE(engine.canSubstitute(true));
  EXPECT_FALSE(engine.substitutePlayer(102, bench[1]));
  EXPECT_EQ(engine.getSubstitutionsUsed(true), 4);
  EXPECT_TRUE(engine.canSubstitute(false));
}

TEST(MatchEngineTest, GoalAndFollowingKickoffShareOneSubstitutionWindow)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 80, players);
  Team away = createSquadWithBench(2, "Away", 40, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 77);
  engine.setAutoSubstitutions(false, false);
  const auto& bench = home.getLineup().getReserves();
  ASSERT_TRUE(engine.substitutePlayer(107, bench[4]));
  ASSERT_EQ(engine.getSubstitutionWindowsUsed(true), 1);

  for (int step = 0; step < 20'000 && engine.getState() != MatchState::GOAL;
       ++step)
  {
    engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  }
  ASSERT_EQ(engine.getState(), MatchState::GOAL);
  ASSERT_TRUE(engine.substitutePlayer(109, bench[5]));
  EXPECT_EQ(engine.getSubstitutionWindowsUsed(true), 2)
      << "a goal starts a new break in play";

  for (int step = 0; step < 2'000 && engine.getState() == MatchState::GOAL;
       ++step)
  {
    engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  }
  ASSERT_EQ(engine.getState(), MatchState::KICK_OFF);
  ASSERT_TRUE(engine.substitutePlayer(110, bench[6]));
  EXPECT_EQ(engine.getSubstitutionWindowsUsed(true), 2)
      << "celebration and kick-off belong to the same break in play";
}

TEST(MatchEngineTest, PenaltiesAreTakenByTheBestOutfieldShooter)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 65, players);
  Team away = createSquadWithBench(2, "Away", 65, players);
  const StatsConfig config = createStatsConfig();
  const Player* bestShooter = players[7].get();
  std::map<std::string, float> sharp = bestShooter->getStats();
  sharp["Shooting"] = 92.0F;
  auto specialist = std::make_unique<Player>(
      bestShooter->getId(), 1, "Spot", "Kick", PlayerRole::LW, Language::EN,
      100'000, 0, 25, 3, 180, Foot::Right, sharp);
  std::map<std::string, float> keeperStats = players[0]->getStats();
  keeperStats["Shooting"] = 99.0F;
  auto shootingKeeper = std::make_unique<Player>(
      players[0]->getId(), 1, "Keeper", "Shooter", PlayerRole::GK, Language::EN,
      100'000, 0, 25, 3, 190, Foot::Right, keeperStats);
  home.getLineup().setGoalkeeper(shootingKeeper.get());
  home.getLineup().removeOutfieldPlayer(bestShooter->getId());
  home.getLineup().addOutfieldPlayer(specialist.get(),
                                     {.x = 0.68F, .y = 0.16F});

  int penaltiesChecked = 0;
  for (uint32_t seed = 1; seed <= 300 && penaltiesChecked < 3; ++seed)
  {
    MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                       away.getStrategy(), config, seed);
    engine.setAutoSubstitutions(false, false);
    simulateToFullTime(engine, 0.1F);
    const PlayerMatchStats* specialistStats =
        engine.findPlayerStats(specialist->getId());
    ASSERT_NE(specialistStats, nullptr);
    if (specialistStats->sentOff || specialistStats->injured)
    {
      continue;
    }
    for (const MatchEvent& event : engine.getEvents())
    {
      if (event.type != MatchEventType::PENALTY || !event.isHomeTeam)
      {
        continue;
      }
      EXPECT_EQ(event.primaryPlayerId, specialist->getId())
          << "the penalty taker must be the best outfield shooter";
      ++penaltiesChecked;
    }
  }
  EXPECT_GT(penaltiesChecked, 0) << "no home penalty in the sampled seeds";
}

TEST(MatchEngineTest, DesignatedSetPieceTakersTakeTheirDuties)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 65, players);
  Team away = createSquadWithBench(2, "Away", 65, players);
  const StatsConfig config = createStatsConfig();
  // A sharp shooter on the left wing would be the automatic taker.
  const Player* winger = players[7].get();
  std::map<std::string, float> sharp = winger->getStats();
  sharp["Shooting"] = 92.0F;
  auto specialist = std::make_unique<Player>(
      winger->getId(), 1, "Spot", "Kick", PlayerRole::LW, Language::EN, 100'000,
      0, 25, 3, 180, Foot::Right, sharp);
  home.getLineup().removeOutfieldPlayer(winger->getId());
  home.getLineup().addOutfieldPlayer(specialist.get(),
                                     {.x = 0.68F, .y = 0.16F});
  constexpr PlayerID PENALTY_TAKER = 102;  // a centre-back
  constexpr PlayerID CORNER_TAKER = 104;   // the right-back
  home.getLineup().setDesignated(SetPieceDuty::Penalties, PENALTY_TAKER);
  home.getLineup().setDesignated(SetPieceDuty::CornersLeft, CORNER_TAKER);
  home.getLineup().setDesignated(SetPieceDuty::CornersRight, CORNER_TAKER);

  int designatedPenalties = 0;
  int corners = 0;
  int fallbackPenalties = 0;
  for (uint32_t seed = 1;
       seed <= 300 && (designatedPenalties < 2 || fallbackPenalties < 2);
       ++seed)
  {
    // Odd seeds: the designated taker plays. Even seeds: he is replaced at
    // kick-off, so the automatic choice (the best shooter) takes over.
    const bool replaced = seed % 2 == 0;
    MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                       away.getStrategy(), config, seed);
    engine.setAutoSubstitutions(false, false);
    if (replaced)
    {
      ASSERT_TRUE(engine.substitutePlayer(PENALTY_TAKER,
                                          home.getLineup().getReserves()[1]));
    }
    simulateToFullTime(engine, 0.1F);
    const PlayerMatchStats* takerStats = engine.findPlayerStats(PENALTY_TAKER);
    const PlayerMatchStats* specialistStats =
        engine.findPlayerStats(specialist->getId());
    ASSERT_NE(takerStats, nullptr);
    ASSERT_NE(specialistStats, nullptr);
    const bool takerAvailable =
        !replaced && !takerStats->sentOff && !takerStats->injured;
    const bool specialistAvailable =
        !specialistStats->sentOff && !specialistStats->injured;
    const bool cornerTakerAvailable = [&]
    {
      const PlayerMatchStats* entry = engine.findPlayerStats(CORNER_TAKER);
      return entry && !entry->sentOff && !entry->injured;
    }();
    for (const MatchEvent& event : engine.getEvents())
    {
      if (!event.isHomeTeam)
      {
        continue;
      }
      if (event.type == MatchEventType::CORNER && cornerTakerAvailable)
      {
        EXPECT_EQ(event.primaryPlayerId, CORNER_TAKER);
        ++corners;
      }
      if (event.type != MatchEventType::PENALTY)
      {
        continue;
      }
      if (takerAvailable)
      {
        EXPECT_EQ(event.primaryPlayerId, PENALTY_TAKER);
        ++designatedPenalties;
      }
      else if (replaced && specialistAvailable)
      {
        EXPECT_EQ(event.primaryPlayerId, specialist->getId())
            << "without the designated taker the best shooter steps up";
        ++fallbackPenalties;
      }
    }
  }
  EXPECT_GT(designatedPenalties, 0);
  EXPECT_GT(fallbackPenalties, 0);
  EXPECT_GT(corners, 0);
}

TEST(MatchEngineTest, ConditionCanBeCarriedBetweenMatches)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 65, players);
  Team away = createSquadWithBench(2, "Away", 65, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 5150);
  engine.setAutoSubstitutions(false, false);
  ASSERT_TRUE(engine.setPlayerCondition(105, 0.7F));
  EXPECT_FLOAT_EQ(engine.getPlayerCondition(105).value_or(0.0F), 0.7F);
  EXPECT_FALSE(engine.getPlayerCondition(999'999).has_value());

  // Compare the same player under identical early-match workload. Different
  // positions run different distances, and full-match exhaustion may put
  // both players at the minimum condition regardless of their starting load.
  MatchEngine freshEngine(home.getLineup(), away.getLineup(),
                          home.getStrategy(), away.getStrategy(), config, 5150);
  freshEngine.setAutoSubstitutions(false, false);
  for (int step = 0; step < 600; ++step)
  {
    engine.update(0.1F);
    freshEngine.update(0.1F);
  }
  EXPECT_GT(freshEngine.getPlayerCondition(105).value_or(0.0F),
            engine.getPlayerCondition(105).value_or(1.0F));

  simulateToFullTime(engine, 0.1F);
  EXPECT_FALSE(engine.setPlayerCondition(106, 0.5F))
      << "condition can only be set before kick-off";
  const float tired = engine.getPlayerCondition(105).value_or(1.0F);
  const float fresh = engine.getPlayerCondition(106).value_or(0.0F);
  EXPECT_LT(tired, 0.7F);
  EXPECT_LT(fresh, 0.95F) << "a full match must cost condition";
  EXPECT_GE(tired, MatchTuning::Player::MINIMUM_STAMINA);
}

TEST(MatchEngineTest, GoalkeeperStateMachineCoversTheMatch)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 70, players);
  Team away = createSquadWithBench(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 4321);
  std::map<GoalkeeperState, int> observed;
  while (engine.getState() != MatchState::FULL_TIME)
  {
    engine.update(MatchTuning::Timing::FIXED_STEP_SECONDS * 2.0F);
    ++observed[engine.getHomeGoalkeeperState()];
    ++observed[engine.getAwayGoalkeeperState()];
  }
  EXPECT_GT(observed[GoalkeeperState::SET_POSITION], 0);
  EXPECT_GT(observed[GoalkeeperState::DIVE], 0);
  EXPECT_GT(observed[GoalkeeperState::HOLD], 0);
  EXPECT_GT(observed[GoalkeeperState::DISTRIBUTE] +
                observed[GoalkeeperState::RECOVER],
            0);
  EXPECT_EQ(goalkeeperStateName(GoalkeeperState::DIVE), "dive");
}

TEST(MatchEngineTest, HeadlessMatchesReproduceFixedStepSimulation)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 70, players);
  Team away = createSquadWithBench(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  for (uint32_t seed = 1; seed <= 10; ++seed)
  {
    SCOPED_TRACE(seed);
    MatchEngine headless(home.getLineup(), away.getLineup(), home.getStrategy(),
                         away.getStrategy(), config, seed);
    MatchEngine reference(home.getLineup(), away.getLineup(),
                          home.getStrategy(), away.getStrategy(), config, seed);
    headless.simulateToEnd();
    ASSERT_EQ(headless.getState(), MatchState::FULL_TIME);
    // Replay exactly the work performed by the headless path, without a
    // hardware-dependent time limit or an estimated match-duration range.
    for (std::uint64_t step = 0; step < headless.getSimulatedSteps(); ++step)
    {
      reference.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    }
    ASSERT_EQ(reference.getState(), MatchState::FULL_TIME);
    EXPECT_EQ(headless.getSimulatedSteps(), reference.getSimulatedSteps());
    // Settle per-call counters before comparing the complete debug snapshots.
    headless.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    reference.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    EXPECT_EQ(headless.getDebugSnapshotJson(),
              reference.getDebugSnapshotJson());
    ASSERT_FALSE(headless.getEvents().empty());
    EXPECT_EQ(headless.getEvents().back().type, MatchEventType::FULL_TIME);
    ASSERT_EQ(headless.getEvents().size(), reference.getEvents().size());
    for (std::size_t index = 0; index < headless.getEvents().size(); ++index)
    {
      EXPECT_EQ(headless.getEvents()[index].type,
                reference.getEvents()[index].type);
      EXPECT_EQ(headless.getEvents()[index].primaryPlayerId,
                reference.getEvents()[index].primaryPlayerId);
    }
  }
}

TEST(MatchEngineTest, BackgroundFidelityIsDeterministicAndCompletes)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 70, players);
  Team away = createSquadWithBench(2, "Away", 66, players);
  const StatsConfig config = createStatsConfig();
  const auto play = [&](MatchFidelity fidelity)
  {
    MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                       away.getStrategy(), config, 2024);
    engine.simulateToEnd(fidelity);
    return engine;
  };
  const MatchEngine first = play(MatchFidelity::BACKGROUND);
  const MatchEngine second = play(MatchFidelity::BACKGROUND);
  ASSERT_EQ(first.getState(), MatchState::FULL_TIME);
  EXPECT_EQ(first.getHomeScore(), second.getHomeScore());
  EXPECT_EQ(first.getAwayScore(), second.getAwayScore());
  EXPECT_EQ(first.getSimulatedSteps(), second.getSimulatedSteps());
  ASSERT_EQ(first.getEvents().size(), second.getEvents().size());
  for (std::size_t index = 0; index < first.getEvents().size(); ++index)
  {
    EXPECT_EQ(first.getEvents()[index].type, second.getEvents()[index].type);
    EXPECT_EQ(first.getEvents()[index].primaryPlayerId,
              second.getEvents()[index].primaryPlayerId);
  }
  // A whole match of real time at either fidelity.
  EXPECT_GE(first.getSimulatedSeconds(), 5'600.0);
  EXPECT_LE(first.getSimulatedSeconds(), 6'600.0);
  EXPECT_GE(first.getStats().ballInPlayMinutes, 45.0F);

  // The default headless path stays at full fidelity: it replays the live
  // update() path step for step.
  MatchEngine headless(home.getLineup(), away.getLineup(), home.getStrategy(),
                       away.getStrategy(), config, 2024);
  headless.simulateToEnd();
  const MatchEngine full = play(MatchFidelity::FULL);
  EXPECT_EQ(headless.getSimulatedSteps(), full.getSimulatedSteps());
  EXPECT_EQ(headless.getEvents().size(), full.getEvents().size());
}

TEST(MatchEngineTest, DefaultMatchContextPlaysExactlyLikeNone)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 70, players);
  Team away = createSquadWithBench(2, "Away", 66, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine plain(home.getLineup(), away.getLineup(), home.getStrategy(),
                    away.getStrategy(), config, 4242);
  MatchEngine withContext(home.getLineup(), away.getLineup(),
                          home.getStrategy(), away.getStrategy(), config, 4242);
  withContext.setMatchContext(MatchContext{});
  EXPECT_EQ(plain.getRefereeStrictness(), withContext.getRefereeStrictness());
  plain.simulateToEnd(MatchFidelity::BACKGROUND);
  withContext.simulateToEnd(MatchFidelity::BACKGROUND);
  EXPECT_EQ(plain.getHomeScore(), withContext.getHomeScore());
  EXPECT_EQ(plain.getAwayScore(), withContext.getAwayScore());
  EXPECT_EQ(plain.getSimulatedSteps(), withContext.getSimulatedSteps());
  EXPECT_EQ(plain.getStats().homePossession,
            withContext.getStats().homePossession);
  ASSERT_EQ(plain.getEvents().size(), withContext.getEvents().size());
  for (std::size_t index = 0; index < plain.getEvents().size(); ++index)
  {
    EXPECT_EQ(plain.getEvents()[index].type,
              withContext.getEvents()[index].type);
    EXPECT_EQ(plain.getEvents()[index].timeMinute,
              withContext.getEvents()[index].timeMinute);
    EXPECT_EQ(plain.getEvents()[index].primaryPlayerId,
              withContext.getEvents()[index].primaryPlayerId);
  }
}

TEST(MatchEngineTest, MatchContextIsSetBeforeKickOffWithinLimits)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 70, players);
  Team away = createSquadWithBench(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 7);
  MatchContext extreme;
  extreme.goalRateScale = 50.0F;
  extreme.refereeStrictnessMean = -3.0F;
  extreme.homeAdvantageScale = std::numeric_limits<float>::quiet_NaN();
  engine.setMatchContext(extreme);
  EXPECT_LE(engine.getMatchContext().goalRateScale,
            MatchTuning::Context::MAX_GOAL_RATE_SCALE);
  EXPECT_GE(engine.getMatchContext().refereeStrictnessMean,
            MatchTuning::Context::MIN_REFEREE_STRICTNESS);
  EXPECT_EQ(engine.getMatchContext().homeAdvantageScale, 1.0F);
  EXPECT_GT(engine.getRefereeStrictness(), 0.0F);

  // Once the match is under way the context no longer changes.
  engine.advance(30.0F);
  const MatchContext before = engine.getMatchContext();
  MatchContext later;
  later.goalRateScale = 0.7F;
  engine.setMatchContext(later);
  EXPECT_EQ(engine.getMatchContext().goalRateScale, before.goalRateScale);
}

TEST(MatchEngineTest, MatchContextMovesGoalsCardsAndHomeAdvantage)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 68, players);
  Team away = createSquadWithBench(2, "Away", 68, players);
  const StatsConfig config = createStatsConfig();
  struct Totals
  {
    int goals = 0;
    int yellows = 0;
    int homePoints = 0;
  };
  // The same seeds under each context, so only the context differs.
  const auto run = [&](const MatchContext& context, int matches)
  {
    Totals totals;
    for (int index = 0; index < matches; ++index)
    {
      MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                         away.getStrategy(), config,
                         900U + (static_cast<uint32_t>(index) * 31U));
      engine.setMatchContext(context);
      engine.simulateToEnd(MatchFidelity::BACKGROUND);
      totals.goals += engine.getHomeScore() + engine.getAwayScore();
      totals.yellows +=
          engine.getStats().homeYellowCards + engine.getStats().awayYellowCards;
      if (engine.getHomeScore() > engine.getAwayScore())
      {
        totals.homePoints += 3;
      }
      else if (engine.getHomeScore() == engine.getAwayScore())
      {
        totals.homePoints += 1;
      }
    }
    return totals;
  };
  MatchContext lowScoring;
  lowScoring.goalRateScale = 0.8F;
  MatchContext highScoring;
  highScoring.goalRateScale = 1.25F;
  const Totals fewGoals = run(lowScoring, 60);
  const Totals manyGoals = run(highScoring, 60);
  std::print("[context] goals {} vs {}\n", fewGoals.goals, manyGoals.goals);
  EXPECT_LT(fewGoals.goals * 115, manyGoals.goals * 100);

  MatchContext lenient;
  lenient.refereeStrictnessMean = 0.75F;
  MatchContext strict;
  strict.refereeStrictnessMean = 1.35F;
  const Totals fewCards = run(lenient, 40);
  const Totals manyCards = run(strict, 40);
  std::print("[context] yellows {} vs {}\n", fewCards.yellows,
             manyCards.yellows);
  EXPECT_LT(fewCards.yellows * 130, manyCards.yellows * 100);

  MatchContext neutral;
  neutral.homeAdvantageScale = 0.0F;
  MatchContext fortress;
  fortress.homeAdvantageScale = 2.0F;
  const Totals neutralVenue = run(neutral, 80);
  const Totals strongHome = run(fortress, 80);
  std::print("[context] home points {} vs {}\n", neutralVenue.homePoints,
             strongHome.homePoints);
  EXPECT_LT(neutralVenue.homePoints, strongHome.homePoints);
}

namespace
{
/** Plays a knockout match between two squads to the end. */
MatchEngine playKnockout(const Team& home, const Team& away,
                         const StatsConfig& config, uint32_t seed,
                         const MatchRules::Knockout& rules)
{
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, seed);
  engine.setKnockout(rules);
  engine.simulateToEnd();
  return engine;
}
}  // namespace

TEST(MatchEngineTest, LevelKnockoutGoesToExtraTimeThenPenalties)
{
  using enum MatchEventType;
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 68, players);
  Team away = createSquadWithBench(2, "Away", 68, players);
  const StatsConfig config = createStatsConfig();
  MatchRules::Knockout rules;
  rules.required = true;

  int shootouts = 0;
  for (uint32_t seed = 1; seed <= 200 && shootouts < 3; ++seed)
  {
    const MatchEngine engine = playKnockout(home, away, config, seed, rules);
    ASSERT_EQ(engine.getState(), MatchState::FULL_TIME);
    ASSERT_TRUE(engine.getTieWinnerHome().has_value())
        << "a knockout match always has a winner";
    if (!engine.hasShootout())
    {
      continue;
    }
    ++shootouts;
    EXPECT_TRUE(engine.wentToExtraTime());
    EXPECT_EQ(engine.getHomeScore(), engine.getAwayScore());
    EXPECT_EQ(engine.getPeriod(), 4);
    EXPECT_GE(engine.getElapsedMatchMinutes(), 120.0F);

    bool sawExtraTime = false;
    bool sawSecondExtraHalf = false;
    int goalEvents = 0;
    int kicks = 0;
    std::optional<bool> lastKicker;
    for (const MatchEvent& event : engine.getEvents())
    {
      if (event.type == SECOND_HALF && event.period == 3)
      {
        sawExtraTime = true;
      }
      if (event.type == SECOND_HALF && event.period == 4)
      {
        sawSecondExtraHalf = true;
      }
      if (event.type == GOAL || event.type == OWN_GOAL)
      {
        ++goalEvents;
      }
      if (event.type != PENALTY_SHOOTOUT ||
          event.detail == MatchEventDetail::NONE)
      {
        continue;
      }
      ++kicks;
      // ABAB: the sides alternate from the first kick to the last.
      if (lastKicker.has_value())
      {
        EXPECT_NE(*lastKicker, event.isHomeTeam);
      }
      lastKicker = event.isHomeTeam;
      EXPECT_NE(event.primaryPlayerId, 0U);
    }
    EXPECT_TRUE(sawExtraTime);
    EXPECT_TRUE(sawSecondExtraHalf);
    EXPECT_EQ(goalEvents, engine.getHomeScore() + engine.getAwayScore())
        << "shootout kicks never count as goals";
    EXPECT_EQ(kicks,
              engine.getShootoutKicks(true) + engine.getShootoutKicks(false));
    EXPECT_NE(engine.getShootoutScore(true), engine.getShootoutScore(false));
    EXPECT_TRUE(MatchRules::shootoutDecided(
        engine.getShootoutScore(true), engine.getShootoutKicks(true),
        engine.getShootoutScore(false), engine.getShootoutKicks(false)));
    EXPECT_EQ(*engine.getTieWinnerHome(),
              engine.getShootoutScore(true) > engine.getShootoutScore(false));
    EXPECT_EQ(engine.getEvents().back().type, FULL_TIME);
    EXPECT_LE(std::max(engine.getSubstitutionsUsed(true),
                       engine.getSubstitutionsUsed(false)),
              MatchTuning::Rules::MAX_SUBSTITUTIONS_PER_TEAM +
                  MatchTuning::Rules::EXTRA_TIME_SUBSTITUTIONS);

    // The same seed plays the same extra time and shootout.
    const MatchEngine replay = playKnockout(home, away, config, seed, rules);
    EXPECT_EQ(replay.getShootoutScore(true), engine.getShootoutScore(true));
    EXPECT_EQ(replay.getShootoutScore(false), engine.getShootoutScore(false));
    ASSERT_EQ(replay.getEvents().size(), engine.getEvents().size());
    for (std::size_t index = 0; index < engine.getEvents().size(); ++index)
    {
      EXPECT_EQ(replay.getEvents()[index].type, engine.getEvents()[index].type);
      EXPECT_EQ(replay.getEvents()[index].primaryPlayerId,
                engine.getEvents()[index].primaryPlayerId);
    }
  }
  EXPECT_GT(shootouts, 0) << "no shootout in the sampled seeds";
}

TEST(MatchEngineTest, AggregateScoreDecidesWhetherExtraTimeIsPlayed)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 68, players);
  Team away = createSquadWithBench(2, "Away", 68, players);
  const StatsConfig config = createStatsConfig();
  // Second leg: the home side won the first leg 1-0 away (so it leads 1-0).
  MatchRules::Knockout rules;
  rules.required = true;
  rules.homeAggregate = 1;
  rules.awayAggregate = 0;
  int extraTimes = 0;
  int decidedInNormalTime = 0;
  for (uint32_t seed = 1; seed <= 60; ++seed)
  {
    const MatchEngine engine = playKnockout(home, away, config, seed, rules);
    ASSERT_EQ(engine.getState(), MatchState::FULL_TIME);
    ASSERT_TRUE(engine.getTieWinnerHome().has_value());
    int normalTimeHome = 0;
    int normalTimeAway = 0;
    for (const MatchEvent& event : engine.getEvents())
    {
      if (event.period > 2)
      {
        break;
      }
      normalTimeHome = event.homeScore;
      normalTimeAway = event.awayScore;
    }
    const bool levelAfterNinety = normalTimeHome + 1 == normalTimeAway;
    EXPECT_EQ(engine.wentToExtraTime(), levelAfterNinety) << "seed " << seed;
    if (levelAfterNinety)
    {
      ++extraTimes;
      continue;
    }
    ++decidedInNormalTime;
    EXPECT_EQ(*engine.getTieWinnerHome(), normalTimeHome + 1 > normalTimeAway);
    EXPECT_FALSE(engine.hasShootout());
    EXPECT_EQ(engine.getPeriod(), 2);
  }
  EXPECT_GT(decidedInNormalTime, 0);
  // Without extra time a level tie goes straight to penalties.
  rules.extraTime = false;
  rules.homeAggregate = 0;
  for (uint32_t seed = 1; seed <= 60; ++seed)
  {
    const MatchEngine engine = playKnockout(home, away, config, seed, rules);
    EXPECT_FALSE(engine.wentToExtraTime());
    if (engine.getHomeScore() == engine.getAwayScore())
    {
      EXPECT_TRUE(engine.hasShootout());
      EXPECT_EQ(engine.getPeriod(), 2);
    }
  }
  RecordProperty("extra_times", extraTimes);
}

TEST(MatchEngineTest, LeagueMatchesCanEndLevel)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 68, players);
  Team away = createSquadWithBench(2, "Away", 68, players);
  const StatsConfig config = createStatsConfig();
  int draws = 0;
  for (uint32_t seed = 1; seed <= 40; ++seed)
  {
    MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                       away.getStrategy(), config, seed);
    engine.simulateToEnd();
    ASSERT_EQ(engine.getState(), MatchState::FULL_TIME);
    EXPECT_EQ(engine.getPeriod(), 2);
    EXPECT_FALSE(engine.wentToExtraTime());
    EXPECT_FALSE(engine.hasShootout());
    EXPECT_FALSE(engine.getTieWinnerHome().has_value());
    for (const MatchEvent& event : engine.getEvents())
    {
      EXPECT_NE(event.type, MatchEventType::PENALTY_SHOOTOUT);
    }
    if (engine.getHomeScore() == engine.getAwayScore())
    {
      ++draws;
    }
  }
  EXPECT_GT(draws, 0);
}

TEST(MatchEngineTest, HeadlessSimulationMatchesLivePlayback)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 68, players);
  Team away = createSquadWithBench(2, "Away", 66, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine headless(home.getLineup(), away.getLineup(), home.getStrategy(),
                       away.getStrategy(), config, 31'337);
  MatchEngine live(home.getLineup(), away.getLineup(), home.getStrategy(),
                   away.getStrategy(), config, 31'337);

  headless.simulateToEnd();
  simulateToFullTime(live, 0.35F);

  ASSERT_EQ(headless.getState(), MatchState::FULL_TIME);
  ASSERT_EQ(live.getState(), MatchState::FULL_TIME);
  EXPECT_EQ(live.getDroppedSimulationSteps(), 0U);
  EXPECT_EQ(headless.getHomeScore(), live.getHomeScore());
  EXPECT_EQ(headless.getAwayScore(), live.getAwayScore());
  EXPECT_EQ(headless.getStats().homeShots, live.getStats().homeShots);
  EXPECT_EQ(headless.getStats().awayPassesCompleted,
            live.getStats().awayPassesCompleted);
  EXPECT_EQ(headless.getEvents().size(), live.getEvents().size());
  EXPECT_DOUBLE_EQ(headless.getSimulatedSeconds(), live.getSimulatedSeconds());
  ASSERT_EQ(headless.getPlayerStats().size(), live.getPlayerStats().size());
  EXPECT_FLOAT_EQ(headless.getPlayerStats()[3].distanceMetres,
                  live.getPlayerStats()[3].distanceMetres);
}

TEST(MatchEngineTest, AdvanceRunsEveryStepWhileUpdateBoundsCatchUp)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 65, players);
  Team away = createDummyTeam(2, "Away", 65, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 4'242);

  const float advanced = engine.advance(600.0F);
  EXPECT_NEAR(advanced, 600.0F, 0.001F);
  EXPECT_NEAR(engine.getSimulatedSeconds(), 600.0, 0.001);
  EXPECT_EQ(engine.getDroppedSimulationSteps(), 0U);
  // Ten minutes of real match time on the clock, less the few seconds of
  // kick-off setup that do not run it.
  EXPECT_LE(engine.getElapsedMatchMinutes(), 10.0F);
  EXPECT_GE(engine.getElapsedMatchMinutes(), 9.5F);

  engine.update(60.0F);
  EXPECT_EQ(engine.getLastUpdateStepCount(),
            MatchTuning::Timing::MAX_FIXED_STEPS_PER_UPDATE);
  EXPECT_GT(engine.getDroppedSimulationSteps(), 0U);
}

TEST(MatchEngineTest, PlayersAndBallMoveAtRealisticSpeeds)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 70, players);
  Team away = createSquadWithBench(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 2'718);

  float fastestPass = 0.0F;
  float fastestShot = 0.0F;
  float fastestPlayer = 0.0F;
  while (engine.getState() != MatchState::FULL_TIME)
  {
    engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    const MatchBall& ball = engine.getBall();
    const float ballSpeed = std::hypot(ball.velocity.x, ball.velocity.y);
    if (ball.possessionState().isPass)
    {
      fastestPass = std::max(fastestPass, ballSpeed);
    }
    if (ball.possessionState().isShot)
    {
      fastestShot = std::max(fastestShot, ballSpeed);
    }
    for (const MatchPlayer& player : engine.getPlayers())
    {
      fastestPlayer = std::max(
          fastestPlayer, std::hypot(player.velocity.x, player.velocity.y));
    }
  }
  // Passes 7-25 m/s, struck shots up to ~33 m/s, sprints below 10.5 m/s.
  EXPECT_GT(fastestPass, 12.0F);
  EXPECT_LE(fastestPass, 25.5F);
  EXPECT_GT(fastestShot, 18.0F);
  EXPECT_LE(fastestShot, 34.0F);
  EXPECT_GT(fastestPlayer, 7.0F);
  EXPECT_LE(fastestPlayer, 10.5F);

  for (const PlayerMatchStats& entry : engine.getPlayerStats())
  {
    EXPECT_LE(entry.topSpeed, 10.5F);
    EXPECT_LE(entry.highIntensityMetres, entry.distanceMetres);
    EXPECT_LE(entry.sprintMetres, entry.highIntensityMetres);
    if (const bool fullMatch = entry.started && !entry.substitutedOff &&
                               !entry.sentOff && !entry.injured;
        !fullMatch || entry.role == PlayerRole::GK)
    {
      continue;
    }
    EXPECT_GE(entry.distanceMetres, 6'000.0F);
    EXPECT_LE(entry.distanceMetres, 14'000.0F);
    EXPECT_GT(entry.topSpeed, 6.5F);
  }
}

TEST(MatchEngineTest, PredictedHighlightHappensInTheLiveMatch)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 72, players);
  Team away = createSquadWithBench(2, "Away", 64, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 8'080);
  engine.advance(120.0F);
  const double now = engine.getSimulatedSeconds();
  const std::size_t knownHighlights = engine.getHighlights().size();

  const std::optional<MatchHighlight> predicted = engine.predictNextHighlight();
  if (!predicted)
  {
    FAIL() << "expected a predicted highlight";
  }
  EXPECT_GE(predicted->startSeconds, now);
  EXPECT_GT(predicted->triggerSeconds, now);
  EXPECT_LE(predicted->startSeconds, predicted->triggerSeconds);
  EXPECT_GT(predicted->endSeconds, predicted->triggerSeconds);
  // Prediction runs on a copy: the live match has not moved.
  EXPECT_DOUBLE_EQ(engine.getSimulatedSeconds(), now);

  engine.advance(static_cast<float>(predicted->triggerSeconds - now) + 0.05F);
  ASSERT_GT(engine.getHighlights().size(), knownHighlights);
  const MatchHighlight& recorded = engine.getHighlights().back();
  EXPECT_NEAR(recorded.triggerSeconds, predicted->triggerSeconds, 0.001);
  EXPECT_EQ(recorded.type, predicted->type);
}

TEST(MatchEngineTest, HighlightPlaybackSkipsBetweenWindowsAndFinishes)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 70, players);
  Team away = createSquadWithBench(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 606);
  engine.setPlaybackMode(MatchPlaybackMode::HIGHLIGHTS);
  engine.setHighlightPlaybackSpeed(4.0F);
  EXPECT_FLOAT_EQ(engine.getHighlightPlaybackSpeed(), 4.0F);

  constexpr float FRAME_SECONDS = 1.0F / 30.0F;
  int skips = 0;
  double watchedSeconds = 0.0;
  for (int frame = 0;
       frame < 400'000 && engine.getState() != MatchState::FULL_TIME; ++frame)
  {
    const double before = engine.getSimulatedSeconds();
    if (engine.advancePlayback(FRAME_SECONDS))
    {
      ++skips;
      continue;
    }
    ASSERT_TRUE(engine.getScheduledHighlight().has_value() ||
                engine.getState() == MatchState::FULL_TIME);
    watchedSeconds += engine.getSimulatedSeconds() - before;
  }
  EXPECT_EQ(engine.getState(), MatchState::FULL_TIME);
  EXPECT_GE(skips, 5);
  // Only the highlight windows are shown live.
  EXPECT_GT(watchedSeconds, 60.0);
  EXPECT_LT(watchedSeconds, engine.getSimulatedSeconds() * 0.5);
  EXPECT_FALSE(engine.getHighlights().empty());

  engine.setPlaybackSpeed(1'000.0F);
  EXPECT_FLOAT_EQ(engine.getPlaybackSpeed(), MatchTuning::Playback::MAX_SPEED);
}

TEST(MatchEngineTest, BenchConditionAndFamiliarityInputs)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 66, players);
  Team away = createSquadWithBench(2, "Away", 66, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 77);
  engine.setAutoSubstitutions(false, false);
  const Player* substitute = home.getLineup().getReserves()[3];
  ASSERT_TRUE(engine.setPlayerCondition(substitute->getId(), 0.6F));
  EXPECT_FALSE(engine.setPlayerCondition(999'999, 0.6F));

  engine.setTacticalFamiliarity(true, 3.0F);
  EXPECT_FLOAT_EQ(engine.getTacticalFamiliarity(true), 1.0F);
  engine.setTacticalFamiliarity(false, 0.25F);
  EXPECT_FLOAT_EQ(engine.getTacticalFamiliarity(false), 0.25F);

  ASSERT_TRUE(engine.substitutePlayer(105, substitute));
  EXPECT_FLOAT_EQ(engine.getPlayerCondition(substitute->getId()).value_or(1.0F),
                  0.6F);
}

namespace
{
float metresBetweenPoints(Vector2F first, Vector2F second)
{
  return std::hypot(first.x - second.x, first.y - second.y);
}

/** Metric distance from a point to the segment [start, end]. */
float metresToSegment(PitchPoint point, PitchPoint start, PitchPoint end)
{
  const float sx = end.x - start.x;
  const float sy = end.y - start.y;
  const float px = point.x - start.x;
  const float py = point.y - start.y;
  const float squared = (sx * sx) + (sy * sy);
  const float along =
      squared > 0.0F ? std::clamp((px * sx + py * sy) / squared, 0.0F, 1.0F)
                     : 0.0F;
  return std::hypot(px - (sx * along), py - (sy * along));
}
}  // namespace

TEST(MatchEngineTest, PlayersNeverExceedTheirTopSpeed)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 88, players);
  Team away = createSquadWithBench(2, "Away", 45, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 1'999);
  while (engine.getState() != MatchState::FULL_TIME)
  {
    engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    for (const MatchPlayer& player : engine.getPlayers())
    {
      if (!player.onPitch)
      {
        continue;
      }
      const float pace = player.player->getStats().at("Pace") / 100.0F;
      const float topSpeed = MatchTuning::Player::TOP_SPEED_BASE +
                             (pace * MatchTuning::Player::TOP_SPEED_PACE);
      ASSERT_LE(std::hypot(player.velocity.x, player.velocity.y),
                topSpeed + 0.01F)
          << "player " << player.player->getId();
    }
  }
}

TEST(MatchEngineTest, PossessionIsOnlyGainedWithinReach)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 70, players);
  Team away = createSquadWithBench(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 7'007);
  // The ball is only won by getting a foot, the body or (a keeper) the hands
  // to it: its path during the step must pass within reach of the player's
  // body, allowing for his own movement in that step.
  constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
  constexpr float SLACK = MatchTuning::Aerial::GOALKEEPER_CLAIM_RADIUS_METRES +
                          (10.0F * STEP) + 0.05F;
  const Player* previousOwner = engine.getBall().possessionState().possessedBy;
  int gains = 0;
  while (engine.getState() != MatchState::FULL_TIME)
  {
    const MatchBall before = engine.getBall();
    const MatchState stateBefore = engine.getState();
    engine.advance(STEP);
    const MatchBall& after = engine.getBall();
    if ((after.possessionState().possessedBy != nullptr) &&
        after.possessionState().possessedBy != previousOwner &&
        (before.possessionState().possessedBy == nullptr) &&
        stateBefore == MatchState::PLAYING &&
        engine.getState() == MatchState::PLAYING)
    {
      const auto owner = std::ranges::find_if(
          engine.getPlayers(), [&after](const MatchPlayer& player)
          { return player.player == after.possessionState().possessedBy; });
      ASSERT_NE(owner, engine.getPlayers().end());
      const Vector2F travel{.x = before.velocity.x * STEP,
                            .y = before.velocity.y * STEP};
      const PitchPoint end{before.position.x + travel.x,
                           before.position.y + travel.y};
      EXPECT_LE(metresToSegment(owner->position, before.position, end), SLACK);
      ++gains;
    }
    previousOwner = after.possessionState().possessedBy;
  }
  EXPECT_GT(gains, 100);
}

namespace
{
/**
 * Share of scenarios in which a lone dribbler gets past a defender jockeying
 * 3 m in front of him: after three seconds his side still has the ball, or
 * he has already used it (shot or pass).
 */
double dribblerBeatsTheDefender(int dribblerRating, int defenderRating)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", dribblerRating, players);
  Team away = createDummyTeam(2, "Away", defenderRating, players);
  const StatsConfig config = createStatsConfig();
  int beaten = 0;
  int duels = 0;
  for (std::uint32_t seed = 1; seed <= 160; ++seed)
  {
    MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                       away.getStrategy(), config, seed);
    engine.setAutoSubstitutions(false, false);
    MatchScenario scenario;
    for (std::uint32_t index = 0; index < 11; ++index)
    {
      // Team-mates are far behind so the carrier has to take the man on.
      scenario.players.push_back({.playerId = 100U + index,
                                  .position = PITCH.at(0.02F + (0.005F * index),
                                                       0.1F + (0.08F * index)),
                                  .makingRun = false});
      scenario.players.push_back(
          {.playerId = 200U + index,
           .position = PITCH.at(0.95F, 0.1F + (0.08F * index)),
           .makingRun = false});
    }
    scenario.players[18] = {.playerId = 109U,
                            .position = PITCH.at(0.70F, 0.50F),
                            .makingRun = false};
    scenario.players[19] = {.playerId = 209U,
                            .position = PITCH.at(0.73F, 0.50F),
                            .makingRun = false};
    scenario.players[1] = {.playerId = 200U,
                           .position = PITCH.at(0.97F, 0.50F),
                           .makingRun = false};
    scenario.carrierId = 109U;
    scenario.ballPosition = PITCH.at(0.70F, 0.50F);
    if (!engine.applyScenario(scenario))
    {
      continue;
    }
    if (engine.getLastScenarioDecision().action != ScenarioAction::CARRY)
    {
      continue;
    }
    ++duels;
    const int usedBefore =
        engine.getStats().homePassesAttempted + engine.getStats().homeShots;
    engine.advance(3.0F);
    const MatchBall& ball = engine.getBall();
    const bool used =
        engine.getStats().homePassesAttempted + engine.getStats().homeShots !=
        usedBefore;
    const bool homeHasIt =
        (ball.possessionState().possessedBy != nullptr)
            ? ball.possessionState().possessedBy->getTeamId() == 1
            : (ball.possessionState().lastPossessor != nullptr) &&
                  ball.possessionState().lastPossessor->getTeamId() == 1;
    if (used || homeHasIt)
    {
      ++beaten;
    }
  }
  return duels > 20 ? static_cast<double>(beaten) / duels : -1.0;
}
}  // namespace

TEST(MatchEngineTest, DribblerVersusJockeyingDefenderDependsOnSkill)
{
  const double skilled = dribblerBeatsTheDefender(85, 45);
  const double outclassed = dribblerBeatsTheDefender(45, 85);
  RecordProperty("skilled_keep_rate", std::to_string(skilled));
  RecordProperty("outclassed_keep_rate", std::to_string(outclassed));
  ASSERT_GE(skilled, 0.0) << "too few duels sampled";
  ASSERT_GE(outclassed, 0.0) << "too few duels sampled";
  // Neither side always wins, but skill clearly shifts the duel.
  EXPECT_GT(skilled, outclassed + 0.15);
  EXPECT_LT(skilled, 0.98);
  EXPECT_GT(outclassed, 0.02);
}

namespace
{
/** Mean own-goal depth of a side's outfield players over `seconds`. */
double meanOutfieldDepth(MatchEngine& engine, bool homeTeam, float seconds)
{
  double sum = 0.0;
  int samples = 0;
  const auto steps =
      static_cast<int>(seconds / MatchTuning::Timing::FIXED_STEP_SECONDS);
  for (int step = 0; step < steps; ++step)
  {
    engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    for (const MatchPlayer& player : engine.getPlayers())
    {
      if (!player.onPitch || player.isGoalkeeper ||
          player.isHomeTeam != homeTeam)
      {
        continue;
      }
      sum += homeTeam ? PITCH.fractionX(player.position)
                      : 1.0F - PITCH.fractionX(player.position);
      ++samples;
    }
  }
  return samples > 0 ? sum / samples : 0.0;
}
}  // namespace

TEST(MatchEngineTest, MidMatchStrategyChangeMovesTheTeam)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 66, players);
  Team away = createSquadWithBench(2, "Away", 66, players);
  const StatsConfig config = createStatsConfig();
  double cautiousDepth = 0.0;
  double boldDepth = 0.0;
  for (std::uint32_t seed = 1; seed <= 6; ++seed)
  {
    MatchEngine cautious(home.getLineup(), away.getLineup(), home.getStrategy(),
                         away.getStrategy(), config, seed);
    MatchEngine bold(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, seed);
    cautious.advance(120.0F);
    bold.advance(120.0F);
    StrategySliders low{.pressing = 0.1F,
                        .riskTaking = 0.2F,
                        .offensiveBias = 0.1F,
                        .widthUsage = 0.5F,
                        .compactness = 0.9F};
    StrategySliders high{.pressing = 1.0F,
                         .riskTaking = 0.9F,
                         .offensiveBias = 1.0F,
                         .widthUsage = 0.6F,
                         .compactness = 0.2F};
    Strategy cautiousPlan;
    cautiousPlan.setAllSliders(low);
    Strategy boldPlan;
    boldPlan.setAllSliders(high);
    cautious.setStrategy(true, cautiousPlan);
    bold.setStrategy(true, boldPlan);
    // Score effects temper the instruction once a goal has gone in.
    if (bold.getHomeScore() == bold.getAwayScore())
    {
      EXPECT_FLOAT_EQ(bold.getEffectiveSliders(true).pressing, 1.0F);
    }
    else
    {
      EXPECT_GT(bold.getEffectiveSliders(true).pressing, 0.8F);
    }
    cautiousDepth += meanOutfieldDepth(cautious, true, 600.0F);
    boldDepth += meanOutfieldDepth(bold, true, 600.0F);
  }
  EXPECT_GT(boldDepth, cautiousDepth * 1.03)
      << "bold tactics must push the team higher up the pitch";
}

TEST(MatchEngineTest, MidMatchPressingChangeDisruptsTheOpponent)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 66, players);
  Team away = createSquadWithBench(2, "Away", 66, players);
  const StatsConfig config = createStatsConfig();
  // Opponent passes (attempted, completed) in the half hour after the
  // change, summed over the seeds.
  struct Window
  {
    int attempted = 0;
    int completed = 0;
  };
  const auto play = [&](const StrategySliders& sliders, std::uint32_t seed)
  {
    MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                       away.getStrategy(), config, seed);
    engine.setAutoSubstitutions(false, false);
    engine.advance(120.0F);
    Strategy plan;
    plan.setAllSliders(sliders);
    engine.setStrategy(true, plan);
    const MatchStats before = engine.getStats();
    engine.advance(1'800.0F);
    const MatchStats& after = engine.getStats();
    Window window;
    window.attempted = after.awayPassesAttempted - before.awayPassesAttempted;
    window.completed = after.awayPassesCompleted - before.awayPassesCompleted;
    return window;
  };
  Window standOff;
  Window press;
  for (std::uint32_t seed = 1; seed <= 6; ++seed)
  {
    const Window low = play({.pressing = 0.1F,
                             .riskTaking = 0.5F,
                             .offensiveBias = 0.5F,
                             .widthUsage = 0.5F,
                             .compactness = 0.6F},
                            seed);
    const Window high = play({.pressing = 1.0F,
                              .riskTaking = 0.5F,
                              .offensiveBias = 0.5F,
                              .widthUsage = 0.5F,
                              .compactness = 0.4F},
                             seed);
    standOff.attempted += low.attempted;
    standOff.completed += low.completed;
    press.attempted += high.attempted;
    press.completed += high.completed;
  }
  ASSERT_GT(standOff.attempted, 0);
  ASSERT_GT(press.attempted, 0);
  const double standOffCompletion =
      static_cast<double>(standOff.completed) / standOff.attempted;
  const double pressCompletion =
      static_cast<double>(press.completed) / press.attempted;
  EXPECT_LT(pressCompletion, standOffCompletion - 0.01)
      << "a high press must cost the opponent passes";
}

TEST(MatchEngineTest, ShoutsNudgeTheSlidersAndFade)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 66, players);
  Team away = createSquadWithBench(2, "Away", 66, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 3);
  engine.setAutoSubstitutions(false, false);
  const float basePressing = engine.getEffectiveSliders(true).pressing;
  EXPECT_FLOAT_EQ(engine.getShoutStrength(true), 0.0F);
  engine.applyShout(true, MatchShout::PRESS_MORE);
  EXPECT_FLOAT_EQ(engine.getShoutStrength(true), 1.0F);
  EXPECT_GT(engine.getEffectiveSliders(true).pressing, basePressing + 0.2F);
  EXPECT_FLOAT_EQ(engine.getEffectiveSliders(false).pressing,
                  away.getStrategy().getSliders().pressing);
  engine.advance(MatchTuning::Touchline::SHOUT_DURATION_SECONDS * 0.5F);
  EXPECT_NEAR(engine.getShoutStrength(true), 0.5F, 0.01F);
  engine.advance(MatchTuning::Touchline::SHOUT_DURATION_SECONDS * 0.6F);
  EXPECT_FLOAT_EQ(engine.getShoutStrength(true), 0.0F);
  // Only the score effect remains once the shout has faded.
  const float scoreEffect = MatchTuning::Touchline::SCORE_EFFECT_MAX_GOALS *
                            MatchTuning::Touchline::SCORE_EFFECT_PRESSING;
  EXPECT_NEAR(engine.getEffectiveSliders(true).pressing, basePressing,
              engine.getHomeScore() == engine.getAwayScore()
                  ? 1e-6F
                  : scoreEffect + 1e-6F);
}

TEST(MatchEngineTest, RepeatedShoutsLoseEffectAndNewShoutsNudgeTheSliders)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 66, players);
  Team away = createSquadWithBench(2, "Away", 66, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 5);
  engine.setAutoSubstitutions(false, false);
  const StrategySliders base = engine.getEffectiveSliders(true);
  EXPECT_FALSE(engine.getActiveShout(true).has_value());

  engine.applyShout(true, MatchShout::KEEP_POSSESSION);
  EXPECT_EQ(engine.getActiveShout(true), MatchShout::KEEP_POSSESSION);
  EXPECT_LT(engine.getEffectiveSliders(true).riskTaking,
            base.riskTaking - 0.1F);
  EXPECT_GT(engine.getEffectiveSliders(true).widthUsage, base.widthUsage);
  // Shouting again at once works only half as well, then a third of it.
  engine.applyShout(true, MatchShout::STAND_OFF);
  EXPECT_NEAR(engine.getShoutStrength(true), 0.5F, 1e-5F);
  EXPECT_LT(engine.getEffectiveSliders(true).pressing, base.pressing);
  engine.applyShout(true, MatchShout::HIT_ON_COUNTER);
  EXPECT_NEAR(engine.getShoutStrength(true), 1.0F / 3.0F, 1e-5F);
  EXPECT_GT(engine.getEffectiveSliders(true).riskTaking, base.riskTaking);
  EXPECT_FLOAT_EQ(engine.getEffectiveSliders(false).pressing,
                  away.getStrategy().getSliders().pressing);

  // Once the three shouts are forgotten, a new one has its full effect again.
  engine.advance((3.0F * MatchTuning::Touchline::SHOUT_REPEAT_FADE_SECONDS) +
                 10.0F);
  EXPECT_FALSE(engine.getActiveShout(true).has_value());
  engine.applyShout(true, MatchShout::DEMAND_MORE);
  EXPECT_FLOAT_EQ(engine.getShoutStrength(true), 1.0F);
  EXPECT_GE(engine.getEffectiveSliders(true).offensiveBias,
            base.offensiveBias + 0.05F -
                MatchTuning::Touchline::SCORE_EFFECT_MAX_GOALS *
                    MatchTuning::Touchline::SCORE_EFFECT_OFFENSIVE);
}

TEST(MatchEngineTest, FormationChangeReshapesTheSideForAWhile)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 66, players);
  Team away = createSquadWithBench(2, "Away", 66, players);
  const StatsConfig config = createStatsConfig();
  double changedDepth = 0.0;
  double keptDepth = 0.0;
  for (std::uint32_t seed = 1; seed <= 4; ++seed)
  {
    MatchEngine changed(home.getLineup(), away.getLineup(), home.getStrategy(),
                        away.getStrategy(), config, seed);
    MatchEngine kept(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, seed);
    // Keep the same carrier throughout the comparison. Independent match
    // trajectories can otherwise spend different amounts of time defending
    // or celebrating a goal, masking the formation's movement effect.
    for (MatchEngine* engine : {&changed, &kept})
    {
      MatchEngine::DrillRules rules;
      rules.minimumPlayers = false;
      engine->setDrillRules(rules);
      for (const MatchPlayer& player : engine->getPlayers())
      {
        if (player.onPitch && !player.isHomeTeam && !player.isGoalkeeper)
        {
          ASSERT_TRUE(engine->removeBeforeKickOff(player.player->getId()));
        }
      }
      ASSERT_TRUE(engine->setControlledPlayer(105));
      engine->advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
      MatchScenario scenario;
      scenario.carrierId = 105;
      scenario.ballPosition = PITCH.at(0.35F, 0.5F);
      for (const MatchPlayer& player : engine->getPlayers())
      {
        if (player.onPitch)
        {
          scenario.players.push_back(
              {.playerId = player.player->getId(),
               .position = player.player->getId() == scenario.carrierId
                               ? scenario.ballPosition
                               : player.basePosition,
               .makingRun = false});
        }
      }
      ASSERT_TRUE(engine->startDrill(scenario));
      engine->advance(120.0F);
    }
    std::vector<Vector2F> shape = changed.getFormation(true);
    ASSERT_EQ(shape.size(), 10U);
    EXPECT_FLOAT_EQ(shape[0].x, 0.18F);
    // Wrong sizes and non-finite spots are refused.
    EXPECT_FALSE(changed.setFormation(
        true, std::span<const Vector2F>(shape.data(), shape.size() - 1)));
    std::vector<Vector2F> broken = shape;
    broken[3].x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(changed.setFormation(true, broken));
    // Everyone ten metres further up the pitch.
    for (Vector2F& spot : shape)
    {
      spot.x += 10.0F / MatchTuning::Pitch::MATCH_LENGTH_METRES;
    }
    ASSERT_TRUE(changed.setFormation(true, shape));
    EXPECT_NEAR(changed.getFormation(true)[0].x, shape[0].x, 1e-6F);
    const float reshaped = changed.getTacticalFamiliarity(true);
    EXPECT_LT(reshaped, 1.0F);
    EXPECT_GE(reshaped,
              1.0F - MatchTuning::Touchline::MAX_RESHAPE_FAMILIARITY_COST);
    EXPECT_FLOAT_EQ(changed.getTacticalFamiliarity(false), 1.0F);
    changedDepth += meanOutfieldDepth(changed, true, 300.0F);
    keptDepth += meanOutfieldDepth(kept, true, 300.0F);
    for (const MatchEngine* engine : {&changed, &kept})
    {
      ASSERT_NE(engine->getBall().possessionState().possessedBy, nullptr);
      EXPECT_EQ(engine->getBall().possessionState().possessedBy->getId(), 105);
      EXPECT_EQ(engine->getState(), MatchState::PLAYING);
    }
    EXPECT_GT(changed.getTacticalFamiliarity(true), reshaped);
    changed.advance(MatchTuning::Touchline::RESHAPE_RECOVERY_SECONDS);
    EXPECT_FLOAT_EQ(changed.getTacticalFamiliarity(true), 1.0F);
  }
  EXPECT_GT(changedDepth, keptDepth * 1.03)
      << "a higher shape must move the side up the pitch";
}

TEST(MatchEngineTest, PlayersSwapSlotsAndSubstitutesTakeAChosenSlot)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 66, players);
  Team away = createSquadWithBench(2, "Away", 66, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 11);
  engine.setAutoSubstitutions(false, false);
  const auto baseOf = [&engine](PlayerID id)
  {
    for (const MatchPlayer& player : engine.getPlayers())
    {
      if (player.player && player.player->getId() == id)
      {
        return player.basePosition;
      }
    }
    return PITCH.at(-1.0F, -1.0F);
  };
  EXPECT_FALSE(engine.getFormationSlot(100).has_value()) << "keeper";
  ASSERT_EQ(engine.getFormationSlot(101), 0U);
  ASSERT_EQ(engine.getFormationSlot(109), 8U);
  const PitchPoint leftBack = baseOf(101);
  const PitchPoint striker = baseOf(109);
  // The left-back and a striker swap places.
  ASSERT_TRUE(engine.movePlayerToSlot(101, 8));
  EXPECT_EQ(engine.getFormationSlot(101), 8U);
  EXPECT_EQ(engine.getFormationSlot(109), 0U);
  EXPECT_FLOAT_EQ(baseOf(101).x, striker.x);
  EXPECT_FLOAT_EQ(baseOf(109).y, leftBack.y);
  EXPECT_FALSE(engine.movePlayerToSlot(101, 10));
  EXPECT_FALSE(
      engine.movePlayerToSlot(101, std::numeric_limits<std::size_t>::max()));
  constexpr std::size_t FIRST_UNREPRESENTABLE_SLOT =
      1U << std::numeric_limits<std::int8_t>::digits;
  EXPECT_FALSE(engine.movePlayerToSlot(101, FIRST_UNREPRESENTABLE_SLOT));
  EXPECT_EQ(engine.getFormationSlot(101), 8U);
  EXPECT_FALSE(engine.movePlayerToSlot(100, 3)) << "keeper";
  EXPECT_FALSE(engine.movePlayerToSlot(999, 3)) << "not playing";
  EXPECT_FLOAT_EQ(engine.getTacticalFamiliarity(true), 1.0F)
      << "the shape itself did not change";

  // A substitute can come on in another slot; its occupant moves over.
  const Player* substitute = home.getLineup().getReserves()[5];
  ASSERT_TRUE(engine.substitutePlayer(110, substitute, 4));
  EXPECT_EQ(engine.getFormationSlot(substitute->getId()), 4U);
  EXPECT_EQ(engine.getFormationSlot(105), 9U);
  EXPECT_FALSE(
      engine.substitutePlayer(106, home.getLineup().getReserves()[4], 10))
      << "no such slot";
  EXPECT_FALSE(
      engine.substitutePlayer(106, home.getLineup().getReserves()[4],
                              std::numeric_limits<std::size_t>::max()));
  EXPECT_EQ(engine.getSubstitutionsUsed(true), 1);
}

TEST(MatchEngineTest, RecordedTouchlineChangesReplayExactly)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 66, players);
  Team away = createSquadWithBench(2, "Away", 64, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine live(home.getLineup(), away.getLineup(), home.getStrategy(),
                   away.getStrategy(), config, 21);
  live.setAutoSubstitutions(false, true);
  live.advance(600.0F);
  Strategy bold;
  bold.setAllSliders({.pressing = 0.9F,
                      .riskTaking = 0.8F,
                      .offensiveBias = 0.9F,
                      .widthUsage = 0.6F,
                      .compactness = 0.3F});
  live.setStrategy(true, bold);
  live.applyShout(true, MatchShout::PRESS_MORE);
  live.advance(900.0F);
  std::vector<Vector2F> shape = live.getFormation(true);
  shape[4] = {.x = 0.30F, .y = 0.50F};
  ASSERT_TRUE(live.setFormation(true, shape));
  live.applyShout(false, MatchShout::KEEP_POSSESSION);
  live.advance(1'200.0F);
  ASSERT_TRUE(advanceToNextStoppage(live));
  ASSERT_TRUE(live.substitutePlayer(109, home.getLineup().getReserves()[5], 2));
  ASSERT_TRUE(live.movePlayerToSlot(101, 7));
  EXPECT_FALSE(live.movePlayerToSlot(101, 42))
      << "refused changes are not logged";
  live.simulateToEnd();
  ASSERT_EQ(live.getState(), MatchState::FULL_TIME);
  ASSERT_EQ(live.getCommandLog().size(), 6U);

  MatchEngine replay(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 21);
  replay.setAutoSubstitutions(false, true);
  replay.loadCommandReplay(live.getCommandLog());
  replay.simulateToEnd();
  EXPECT_EQ(replay.getHomeScore(), live.getHomeScore());
  EXPECT_EQ(replay.getAwayScore(), live.getAwayScore());
  EXPECT_EQ(replay.getSimulatedSteps(), live.getSimulatedSteps());
  EXPECT_EQ(replay.getEvents().size(), live.getEvents().size());
  EXPECT_FLOAT_EQ(replay.getStats().homePossession,
                  live.getStats().homePossession);
  EXPECT_EQ(replay.getStats().homePassesCompleted,
            live.getStats().homePassesCompleted);
  EXPECT_EQ(replay.getSubstitutions().size(), live.getSubstitutions().size());
  EXPECT_EQ(replay.getFormationSlot(101), live.getFormationSlot(101));
}

TEST(MatchEngineTest, TeamTalkModifierIsBoundedAndDeterministic)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 66, players);
  Team away = createSquadWithBench(2, "Away", 66, players);
  const StatsConfig config = createStatsConfig();
  const auto play = [&](float modifier)
  {
    MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                       away.getStrategy(), config, 99);
    engine.setTeamTalkModifier(true, 1, modifier);
    engine.setTeamTalkModifier(true, 2, modifier);
    engine.simulateToEnd();
    return (engine.getStats().homePassesCompleted * 1000) +
           (engine.getHomeScore() * 10) + engine.getAwayScore();
  };
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 99);
  engine.setTeamTalkModifier(true, 1, 0.5F);
  EXPECT_FLOAT_EQ(engine.getTeamTalkModifier(true, 1),
                  MatchTuning::Touchline::MAX_TEAM_TALK_MODIFIER);
  engine.setTeamTalkModifier(false, 2, -0.5F);
  EXPECT_FLOAT_EQ(engine.getTeamTalkModifier(false, 2),
                  -MatchTuning::Touchline::MAX_TEAM_TALK_MODIFIER);
  EXPECT_FLOAT_EQ(engine.getTeamTalkModifier(true, 3), 0.0F);
  EXPECT_EQ(play(0.012F), play(0.012F));
}

namespace
{
/** Advances until an outfield player of `homeTeam` holds the ball. */
const MatchPlayer* advanceUntilOutfieldCarrier(MatchEngine& engine,
                                               bool homeTeam)
{
  for (int step = 0; step < 20'000; ++step)
  {
    engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    if (engine.getState() != MatchState::PLAYING)
    {
      continue;
    }
    const Player* holder = engine.getBall().possessionState().possessedBy;
    if (holder == nullptr)
    {
      continue;
    }
    for (const auto& candidate : engine.getPlayers())
    {
      if (candidate.player == holder && candidate.isHomeTeam == homeTeam &&
          !candidate.isGoalkeeper)
      {
        return &candidate;
      }
    }
  }
  return nullptr;
}

const MatchPlayer* findOnPitch(const MatchEngine& engine, PlayerID id)
{
  for (const auto& candidate : engine.getPlayers())
  {
    if ((candidate.player != nullptr) && candidate.player->getId() == id)
    {
      return &candidate;
    }
  }
  return nullptr;
}
}  // namespace

TEST(MatchEngineTest, ControlledPlayerFollowsTheStickWithinHisPhysics)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 70, players);
  Team away = createSquadWithBench(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 17);
  engine.setAutoSubstitutions(false, false);
  engine.advance(30.0F);
  for (int step = 0; step < 2'000 && engine.getState() != MatchState::PLAYING;
       ++step)
  {
    engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  }
  ASSERT_EQ(engine.getState(), MatchState::PLAYING);

  // Goalkeepers and unknown players cannot be taken over.
  const PlayerID keeper = home.getLineup().getGoalkeeper()->getId();
  EXPECT_FALSE(engine.setControlledPlayer(keeper));
  EXPECT_FALSE(engine.setControlledPlayer(999'999));
  EXPECT_EQ(engine.getControlledPlayer(), 0U);

  // A centre-back runs across the pitch toward the touchline on the stick.
  const PlayerID controlled = 102;
  ASSERT_TRUE(engine.setControlledPlayer(controlled));
  EXPECT_EQ(engine.getControlledPlayer(), controlled);
  MatchPlayerInput input;
  input.moveY = engine.getBall().position.y < PITCH.atY(0.5F) ? 1.0F : -1.0F;
  input.sprint = true;
  engine.submitInput(input);
  const MatchPlayer* player = findOnPitch(engine, controlled);
  ASSERT_NE(player, nullptr);
  const PitchPoint start = player->position;
  float fastest = 0.0F;
  for (int step = 0; step < 25; ++step)
  {
    engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    player = findOnPitch(engine, controlled);
    ASSERT_NE(player, nullptr);
    const float speed = std::hypot(player->velocity.x, player->velocity.y);
    EXPECT_LE(speed, player->maxSpeed + 1e-3F);
    // The run follows the stick: sideways, barely any drift along x.
    if (speed > 1.0F)
    {
      EXPECT_GT(std::abs(player->velocity.y),
                4.0F * std::abs(player->velocity.x));
    }
    fastest = std::max(fastest, speed);
  }
  // Accelerates realistically: a hard 2.5 s run nears top speed.
  EXPECT_GT(fastest, 0.8F * player->maxSpeed);
  const float runMetres = std::abs(player->position.y - start.y);
  EXPECT_GT(runMetres, 10.0F);
  EXPECT_LT(runMetres, 25.0F);

  // Releasing the stick stops him under his braking limit.
  engine.submitInput(MatchPlayerInput{});
  engine.advance(1.5F);
  player = findOnPitch(engine, controlled);
  EXPECT_LT(std::hypot(player->velocity.x, player->velocity.y), 0.5F);

  // Handing him back to the AI.
  ASSERT_TRUE(engine.setControlledPlayer(0));
  engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  EXPECT_EQ(engine.getControlledPlayer(), 0U);
}

TEST(MatchEngineTest, ControlledCarrierPassesTowardTheAim)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 70, players);
  Team away = createSquadWithBench(2, "Away", 60, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 23);
  engine.setAutoSubstitutions(false, false);
  engine.advance(20.0F);
  const MatchPlayer* carrier = advanceUntilOutfieldCarrier(engine, true);
  ASSERT_NE(carrier, nullptr);
  const PlayerID carrierId = carrier->player->getId();
  const PitchPoint from = carrier->position;
  ASSERT_TRUE(engine.setControlledPlayer(carrierId));

  // Aim at the farthest-away team-mate: the pass goes to the team-mate best
  // aligned with that direction (usually him), never behind the aim.
  const MatchPlayer* aimed = nullptr;
  float farthest = 0.0F;
  for (const auto& mate : engine.getPlayers())
  {
    if ((mate.player == nullptr) || !mate.onPitch || mate.isHomeTeam != true ||
        mate.isGoalkeeper || mate.player->getId() == carrierId)
    {
      continue;
    }
    const float metres =
        std::hypot(mate.position.x - from.x, mate.position.y - from.y);
    if (metres > farthest && metres < 40.0F)
    {
      farthest = metres;
      aimed = &mate;
    }
  }
  ASSERT_NE(aimed, nullptr);
  MatchPlayerInput input;
  input.aimX = aimed->position.x - from.x;
  input.aimY = aimed->position.y - from.y;
  input.action = MatchInputAction::PASS;
  engine.submitInput(input);
  bool passed = false;
  const auto buffered =
      static_cast<int>(MatchTuning::Control::ACTION_BUFFER_SECONDS /
                       MatchTuning::Timing::FIXED_STEP_SECONDS);
  for (int step = 0; step < buffered && !passed; ++step)
  {
    engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    passed =
        engine.getBall().possessionState().possessedBy != carrier->player &&
        engine.getBall().possessionState().isPass;
  }
  ASSERT_TRUE(passed);
  const Player* receiver = engine.getBall().possessionState().intendedReceiver;
  ASSERT_NE(receiver, nullptr);
  const MatchPlayer* target = findOnPitch(engine, receiver->getId());
  ASSERT_NE(target, nullptr);
  const float toTargetX = target->position.x - from.x;
  const float toTargetY = target->position.y - from.y;
  const float alignment =
      (toTargetX * input.aimX + toTargetY * input.aimY) /
      (std::hypot(toTargetX, toTargetY) * std::hypot(input.aimX, input.aimY));
  EXPECT_GT(alignment, MatchTuning::Control::PASS_MIN_ALIGNMENT - 0.1F);
}

TEST(MatchEngineTest, RecordedControllerInputReplaysIdentically)
{
  using enum MatchInputAction;
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 68, players);
  Team away = createSquadWithBench(2, "Away", 67, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine live(home.getLineup(), away.getLineup(), home.getStrategy(),
                   away.getStrategy(), config, 4'242);
  // A scripted "human": takes a striker, runs, sprints, presses buttons.
  ASSERT_TRUE(live.setControlledPlayer(109));
  for (int second = 0; second < 900; ++second)
  {
    MatchPlayerInput input;
    const float phase = static_cast<float>(second) * 0.37F;
    input.moveX = std::cos(phase);
    input.moveY = std::sin(phase * 1.3F);
    input.sprint = second % 7 < 3;
    if (second % 11 == 0)
    {
      input.action = PASS;
    }
    if (second % 29 == 0)
    {
      input.action = SHOOT;
    }
    if (second % 13 == 0)
    {
      input.action = TACKLE;
    }
    live.submitInput(input);
    live.update(0.5F);
    live.update(0.5F);
  }
  ASSERT_TRUE(live.setControlledPlayer(0));
  live.simulateToEnd();
  ASSERT_EQ(live.getState(), MatchState::FULL_TIME);
  ASSERT_GT(live.getInputLog().size(), 900U);

  MatchEngine replay(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 4'242);
  replay.loadInputReplay(live.getInputLog());
  replay.simulateToEnd();
  EXPECT_EQ(replay.getHomeScore(), live.getHomeScore());
  EXPECT_EQ(replay.getAwayScore(), live.getAwayScore());
  EXPECT_EQ(replay.getEvents().size(), live.getEvents().size());
  EXPECT_EQ(replay.getStats().homePassesCompleted,
            live.getStats().homePassesCompleted);
  EXPECT_DOUBLE_EQ(replay.getSimulatedSeconds(), live.getSimulatedSeconds());
  ASSERT_EQ(replay.getPlayerStats().size(), live.getPlayerStats().size());
  for (std::size_t index = 0; index < live.getPlayerStats().size(); ++index)
  {
    EXPECT_FLOAT_EQ(replay.getPlayerStats()[index].distanceMetres,
                    live.getPlayerStats()[index].distanceMetres);
  }

  // The controlled run really changed the match versus pure AI.
  MatchEngine ai(home.getLineup(), away.getLineup(), home.getStrategy(),
                 away.getStrategy(), config, 4'242);
  ai.simulateToEnd();
  EXPECT_NE(
      ai.getEvents().size() * 1000 + ai.getStats().homePassesCompleted,
      live.getEvents().size() * 1000 + live.getStats().homePassesCompleted);
}

// --- Play mode: the human's team through its active footballer --------------

namespace
{
/** Home CM 105 on the ball in midfield, team-mates spread ahead, the away
 * side well away from him, as a deterministic starting point. */
MatchScenario playModeScenario()
{
  MatchScenario scenario;
  scenario.carrierId = 105;
  scenario.ballPosition = PITCH.at(0.45F, 0.5F);
  scenario.players = {
      {.playerId = 100, .position = PITCH.at(0.05F, 0.50F), .makingRun = false},
      {.playerId = 101, .position = PITCH.at(0.30F, 0.15F), .makingRun = false},
      {.playerId = 102, .position = PITCH.at(0.28F, 0.40F), .makingRun = false},
      {.playerId = 103, .position = PITCH.at(0.28F, 0.60F), .makingRun = false},
      {.playerId = 104, .position = PITCH.at(0.30F, 0.85F), .makingRun = false},
      {.playerId = 105, .position = PITCH.at(0.45F, 0.50F), .makingRun = false},
      {.playerId = 106, .position = PITCH.at(0.50F, 0.70F), .makingRun = false},
      {.playerId = 107, .position = PITCH.at(0.62F, 0.15F), .makingRun = false},
      {.playerId = 108, .position = PITCH.at(0.62F, 0.85F), .makingRun = false},
      {.playerId = 109, .position = PITCH.at(0.66F, 0.32F), .makingRun = true},
      {.playerId = 110, .position = PITCH.at(0.68F, 0.62F), .makingRun = false},
      {.playerId = 200, .position = PITCH.at(0.95F, 0.50F), .makingRun = false},
      {.playerId = 201, .position = PITCH.at(0.80F, 0.15F), .makingRun = false},
      {.playerId = 202, .position = PITCH.at(0.82F, 0.40F), .makingRun = false},
      {.playerId = 203, .position = PITCH.at(0.82F, 0.60F), .makingRun = false},
      {.playerId = 204, .position = PITCH.at(0.80F, 0.85F), .makingRun = false},
      {.playerId = 205, .position = PITCH.at(0.70F, 0.42F), .makingRun = false},
      {.playerId = 206, .position = PITCH.at(0.72F, 0.70F), .makingRun = false},
      {.playerId = 207, .position = PITCH.at(0.60F, 0.05F), .makingRun = false},
      {.playerId = 208, .position = PITCH.at(0.60F, 0.95F), .makingRun = false},
      {.playerId = 209, .position = PITCH.at(0.58F, 0.50F), .makingRun = false},
      {.playerId = 210,
       .position = PITCH.at(0.60F, 0.65F),
       .makingRun = false}};
  return scenario;
}

/** Pitch-metre direction from one engine player to another. */
Vector2F aimAt(const MatchEngine& engine, PlayerID from, PlayerID to)
{
  const MatchPlayer* source = findOnPitch(engine, from);
  const MatchPlayer* target = findOnPitch(engine, to);
  if (source == nullptr || target == nullptr)
  {
    throw std::invalid_argument("Aim requires both players on the pitch");
  }
  return {.x = target->position.x - source->position.x,
          .y = target->position.y - source->position.y};
}

/** Pitch-metre direction between two players of the play-mode scenario. */
Vector2F scenarioAim(PlayerID from, PlayerID to)
{
  const MatchScenario scenario = playModeScenario();
  const auto source =
      std::ranges::find(scenario.players, from, &MatchScenarioPlayer::playerId);
  const auto target =
      std::ranges::find(scenario.players, to, &MatchScenarioPlayer::playerId);
  if (source == scenario.players.end() || target == scenario.players.end())
  {
    throw std::invalid_argument("Aim requires both players in the scenario");
  }
  return {.x = target->position.x - source->position.x,
          .y = target->position.y - source->position.y};
}

/**
 * Hands `controlled` to the controller (a step applies it), then loads the
 * scenario: a controlled carrier makes no AI decision of his own.
 */
bool loadControlled(MatchEngine& engine, const MatchScenario& scenario,
                    PlayerID controlled)
{
  if (!engine.setControlledPlayer(controlled))
  {
    return false;
  }
  engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  return engine.getControlledPlayer() == controlled &&
         engine.applyScenario(scenario);
}

/** Takes control of the scenario carrier and submits one action. */
void controlledAction(MatchEngine& engine, MatchPlayerInput input)
{
  ASSERT_TRUE(loadControlled(engine, playModeScenario(), 105));
  ASSERT_EQ(engine.getBall().possessionState().possessedBy->getId(), 105U);
  engine.submitInput(input);
  engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
}

/** Debug state after a no-op call (at full time) so the per-call step
 * counter does not depend on how the match was driven. */
std::string settledSnapshot(MatchEngine& engine)
{
  engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  return engine.getDebugSnapshotJson();
}

/** Angle (radians) between the launched ball and the intended target. */
float launchError(const MatchEngine& engine)
{
  const MatchBall& ball = engine.getBall();
  const PitchPoint target = engine.getLastPassDecision().targetPoint;
  const float toX = target.x - ball.position.x;
  const float toY = target.y - ball.position.y;
  const float cross = (ball.velocity.x * toY) - (ball.velocity.y * toX);
  const float dot = (ball.velocity.x * toX) + (ball.velocity.y * toY);
  return std::abs(std::atan2(cross, dot));
}

/** Mean launch error of one controlled pass over many match seeds. */
float meanPassError(int rating)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", rating, players);
  Team away = createSquadWithBench(2, "Away", 60, players);
  const StatsConfig config = createStatsConfig();
  float total = 0.0F;
  int passes = 0;
  for (std::uint32_t seed = 1; seed <= 60; ++seed)
  {
    MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                       away.getStrategy(), config, seed);
    MatchPlayerInput input;
    const Vector2F aim = scenarioAim(105, 109);
    input.aimX = aim.x;
    input.aimY = aim.y;
    input.action = MatchInputAction::PASS;
    input.passAssist = 0;
    controlledAction(engine, input);
    if (!engine.getBall().possessionState().isPass ||
        engine.getLastPassDecision().receiverId != 109)
    {
      continue;
    }
    total += launchError(engine);
    ++passes;
  }
  EXPECT_GE(passes, 50);
  return passes > 0 ? total / static_cast<float>(passes) : 0.0F;
}
}  // namespace

TEST(PlayModeTest, ScenarioAimRejectsMissingPlayers)
{
  const Vector2F direction = scenarioAim(105, 109);
  EXPECT_FLOAT_EQ(direction.x, (0.66F - 0.45F) * 105.0F);
  EXPECT_FLOAT_EQ(direction.y, (0.32F - 0.50F) * 68.0F);
  EXPECT_THROW(scenarioAim(0, 109), std::invalid_argument);
  EXPECT_THROW(scenarioAim(105, 0), std::invalid_argument);
}

TEST(PlayModeTest, WeakPasserStaysImpreciseUnderTheSameInput)
{
  // The same stick and button on the same seeds: the engine's execution
  // model, not the human, decides how true the ball runs.
  const float weak = meanPassError(25);
  const float strong = meanPassError(92);
  EXPECT_GT(weak, 0.0F);
  EXPECT_GT(weak, strong * 1.5F) << "weak " << weak << " strong " << strong;
}

namespace
{
// Isolate the shot lane so a delayed dive cannot be hidden by screening or
// defensive contact. Keep scenario preparation separate from the observation.
MatchScenario keeperReactionScenario()
{
  constexpr PlayerID SHOOTER = 105;
  constexpr PlayerID KEEPER = 200;
  const PitchPoint SIDELINE = PITCH.at(0.1F, 0.1F);
  const PitchPoint SHOT_ORIGIN = PITCH.at(0.65F, 0.5F);
  const PitchPoint KEEPER_ORIGIN = PITCH.at(0.95F, 0.5F);
  MatchScenario scenario = playModeScenario();
  // Leave a clear shooting lane and enough flight time to observe the dive
  // before a save. No screening delay or intervening contact masks the clock.
  for (MatchScenarioPlayer& player : scenario.players)
  {
    player.position = SIDELINE;
    if (player.playerId == SHOOTER)
    {
      player.position = SHOT_ORIGIN;
    }
    else if (player.playerId == KEEPER)
    {
      player.position = KEEPER_ORIGIN;
    }
  }
  scenario.carrierId = SHOOTER;
  scenario.ballPosition = SHOT_ORIGIN;
  return scenario;
}
}  // namespace

TEST(PlayModeTest, KeeperMovesWhenReactionEndsWithinTheNextStep)
{
  constexpr int RATING = 65;
  constexpr PlayerID SHOOTER = 105;
  constexpr PlayerID KEEPER = 200;
  constexpr std::uint32_t SEED = 31;
  constexpr float AIM_FORWARD_METRES = 36.75F;
  constexpr float AIM_LATERAL_METRES = 2.0F;
  constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", RATING, players);
  Team away = createSquadWithBench(2, "Away", RATING, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, SEED);
  const MatchScenario scenario = keeperReactionScenario();
  ASSERT_TRUE(loadControlled(engine, scenario, SHOOTER));
  MatchPlayerInput input;
  input.action = MatchInputAction::SHOOT;
  input.aimX = AIM_FORWARD_METRES;
  input.aimY = AIM_LATERAL_METRES;
  engine.submitInput(input);
  engine.advance(STEP);
  ASSERT_TRUE(engine.getBall().possessionState().isShot);
  ASSERT_EQ(engine.getAwayGoalkeeperState(), GoalkeeperState::DIVE);
  const float before = findOnPitch(engine, KEEPER)->position.y;
  engine.advance(STEP);
  ASSERT_TRUE(engine.getBall().possessionState().isShot);
  ASSERT_EQ(engine.getAwayGoalkeeperState(), GoalkeeperState::DIVE);
  // The launch step already consumed part of the reaction. When it finishes
  // inside this step, the remaining fraction must move the visible keeper.
  EXPECT_NE(findOnPitch(engine, KEEPER)->position.y, before);
}

TEST(PlayModeTest, SwitchingTakesTheCarrierTheReceiverAndTheInterceptor)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 70, players);
  Team away = createSquadWithBench(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 31);
  ASSERT_TRUE(loadControlled(engine, playModeScenario(), 105));

  // In possession: the carrier, whoever was active before.
  EXPECT_EQ(engine.suggestActivePlayer(true, 102), 105U);
  EXPECT_EQ(engine.suggestActivePlayer(true, 0), 105U);
  // A manual switch never stays on the man and never picks the keeper.
  const PlayerID next = engine.nextSwitchCandidate(true, 105);
  EXPECT_NE(next, 0U);
  EXPECT_NE(next, 105U);
  EXPECT_NE(next, 100U);
  // Pressing again (skipping the previous pick) cycles to someone else.
  const PlayerID after = engine.nextSwitchCandidate(true, next, 105);
  EXPECT_NE(after, next);
  EXPECT_NE(after, 105U);
  EXPECT_NE(after, 0U);

  // A pass: control goes to its intended receiver while it travels.
  MatchPlayerInput input;
  const Vector2F aim = aimAt(engine, 105, 109);
  input.aimX = aim.x;
  input.aimY = aim.y;
  input.action = MatchInputAction::PASS;
  ASSERT_TRUE(engine.setControlledPlayer(105));
  engine.submitInput(input);
  engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  ASSERT_TRUE(engine.getBall().possessionState().isPass);
  ASSERT_NE(engine.getBall().possessionState().intendedReceiver, nullptr);
  EXPECT_EQ(engine.suggestActivePlayer(true, 105),
            engine.getBall().possessionState().intendedReceiver->getId());

  // Defending: the away striker 209 on the ball, home centre-back 103 right
  // next to him and everyone else far away.
  MatchScenario defending = playModeScenario();
  defending.carrierId = 209;
  defending.ballPosition = PITCH.at(0.40F, 0.5F);
  for (MatchScenarioPlayer& placement : defending.players)
  {
    if (placement.playerId == 209)
    {
      placement.position = PITCH.at(0.40F, 0.5F);
    }
    if (placement.playerId == 103)
    {
      placement.position = PITCH.at(0.37F, 0.52F);
    }
  }
  // The away carrier is held by the controller so he does not play the
  // ball away before the question is asked.
  ASSERT_TRUE(loadControlled(engine, defending, 209));
  EXPECT_EQ(engine.suggestActivePlayer(true, 109), 103U);
  EXPECT_EQ(engine.suggestActivePlayer(false, 0), 209U);
  // The queries never change the match.
  const std::string before = engine.getDebugSnapshotJson();
  (void)engine.suggestActivePlayer(true, 0);
  (void)engine.nextSwitchCandidate(true, 103, 102);
  EXPECT_EQ(engine.getDebugSnapshotJson(), before);
}

TEST(PlayModeTest, PlayModeOffKeepsTheMatchBitIdentical)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 72, players);
  Team away = createSquadWithBench(2, "Away", 66, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine plain(home.getLineup(), away.getLineup(), home.getStrategy(),
                    away.getStrategy(), config, 9'001);
  plain.simulateToEnd();

  // Same seed, an empty input replay and the play-mode queries asked every
  // step (as the match screen does): nothing may change.
  MatchEngine watched(home.getLineup(), away.getLineup(), home.getStrategy(),
                      away.getStrategy(), config, 9'001);
  watched.loadInputReplay({});
  for (int step = 0;
       step < 120'000 && watched.getState() != MatchState::FULL_TIME; ++step)
  {
    (void)watched.suggestActivePlayer(true, 105);
    (void)watched.nextSwitchCandidate(false, 205, 206);
    (void)watched.stepsWithin(0.016F);
    watched.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  }
  ASSERT_EQ(watched.getState(), MatchState::FULL_TIME);
  EXPECT_EQ(watched.getHomeScore(), plain.getHomeScore());
  EXPECT_EQ(watched.getAwayScore(), plain.getAwayScore());
  ASSERT_EQ(watched.getEvents().size(), plain.getEvents().size());
  for (std::size_t index = 0; index < plain.getEvents().size(); ++index)
  {
    EXPECT_EQ(watched.getEvents()[index].type, plain.getEvents()[index].type);
    EXPECT_EQ(watched.getEvents()[index].minutes,
              plain.getEvents()[index].minutes);
  }
  EXPECT_EQ(watched.getStats().homePassesCompleted,
            plain.getStats().homePassesCompleted);
  EXPECT_EQ(watched.getStats().awayTackles, plain.getStats().awayTackles);
  ASSERT_EQ(watched.getPlayerStats().size(), plain.getPlayerStats().size());
  for (std::size_t index = 0; index < plain.getPlayerStats().size(); ++index)
  {
    EXPECT_EQ(watched.getPlayerStats()[index].distanceMetres,
              plain.getPlayerStats()[index].distanceMetres);
  }
  EXPECT_EQ(settledSnapshot(watched), settledSnapshot(plain));
}

TEST(PlayModeTest, TeamControlWithSwitchingReplaysExactly)
{
  using enum MatchInputAction;
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 68, players);
  Team away = createSquadWithBench(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine live(home.getLineup(), away.getLineup(), home.getStrategy(),
                   away.getStrategy(), config, 5'150);
  // A scripted human playing the home team: follows the switching rule,
  // presses every button kind with varying power, sometimes switches by hand.
  PlayerID active = live.suggestActivePlayer(true, 0);
  ASSERT_TRUE(live.setControlledPlayer(active));
  int switches = 0;
  for (int tick = 0; tick < 12'000 && live.getState() != MatchState::FULL_TIME;
       ++tick)
  {
    if (PlayerID wanted = tick % 97 == 0
                              ? live.nextSwitchCandidate(true, active)
                              : live.suggestActivePlayer(true, active);
        wanted != 0 && wanted != active && live.setControlledPlayer(wanted))
    {
      active = wanted;
      ++switches;
    }
    MatchPlayerInput input;
    const float phase = static_cast<float>(tick) * 0.05F;
    input.moveX = std::cos(phase);
    input.moveY = std::sin(phase * 1.7F);
    input.sprint = tick % 9 < 4;
    input.jockey = tick % 23 < 5;
    input.passAssist = static_cast<std::uint8_t>(tick % 3);
    input.power = static_cast<float>(tick % 10) / 10.0F;
    constexpr std::array ACTIONS{PASS,  THROUGH_BALL, LOFTED_PASS,
                                 SHOOT, TACKLE,       SLIDE_TACKLE};
    if (tick % 7 == 0)
    {
      input.action = ACTIONS[(tick / 7) % ACTIONS.size()];
    }
    live.submitInput(input);
    live.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  }
  ASSERT_TRUE(live.setControlledPlayer(0));
  live.simulateToEnd();
  ASSERT_EQ(live.getState(), MatchState::FULL_TIME);
  EXPECT_GT(switches, 20);

  MatchEngine replay(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 5'150);
  replay.loadInputReplay(live.getInputLog());
  replay.simulateToEnd();
  EXPECT_EQ(replay.getHomeScore(), live.getHomeScore());
  EXPECT_EQ(replay.getAwayScore(), live.getAwayScore());
  EXPECT_EQ(replay.getEvents().size(), live.getEvents().size());
  EXPECT_EQ(replay.getStats().homeThroughBalls,
            live.getStats().homeThroughBalls);
  EXPECT_EQ(replay.getStats().homeShots, live.getStats().homeShots);
  EXPECT_EQ(settledSnapshot(replay), settledSnapshot(live));
  // The human really played: through balls and shots of his own.
  EXPECT_GT(live.getStats().homePassesAttempted, 0);
}

TEST(PlayModeTest, ShotPowerAndAimShapeTheStrike)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 75, players);
  Team away = createSquadWithBench(2, "Away", 60, players);
  const StatsConfig config = createStatsConfig();
  const auto strike = [&](float power, float aimY)
  {
    MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                       away.getStrategy(), config, 77);
    MatchScenario scenario = playModeScenario();
    // On the ball 20 m out, in front of goal.
    for (MatchScenarioPlayer& placement : scenario.players)
    {
      if (placement.playerId == 105)
      {
        placement.position = PITCH.at(0.81F, 0.5F);
      }
    }
    scenario.ballPosition = PITCH.at(0.81F, 0.5F);
    EXPECT_TRUE(loadControlled(engine, scenario, 105));
    MatchPlayerInput input;
    input.aimX = 20.0F;
    input.aimY = aimY;
    input.action = MatchInputAction::SHOOT;
    input.power = power;
    engine.submitInput(input);
    engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    EXPECT_TRUE(engine.getBall().possessionState().isShot);
    return engine.getBall();
  };
  const MatchBall soft = strike(0.1F, 0.0F);
  const MatchBall hard = strike(0.7F, 0.0F);
  EXPECT_LT(std::hypot(soft.velocity.x, soft.velocity.y),
            0.8F * std::hypot(hard.velocity.x, hard.velocity.y));
  // The stick picks the side of the goal (same seed, same error draw).
  const MatchBall left = strike(0.6F, -3.0F);
  const MatchBall right = strike(0.6F, 3.0F);
  EXPECT_LT(left.possessionState().shotTargetY,
            right.possessionState().shotTargetY);
}

TEST(PlayModeTest, ThroughBallIsPlayedIntoTheRunnersPath)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 75, players);
  Team away = createSquadWithBench(2, "Away", 60, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 12);
  MatchPlayerInput input;
  const Vector2F aim = scenarioAim(105, 109);
  input.aimX = aim.x;
  input.aimY = aim.y;
  input.action = MatchInputAction::THROUGH_BALL;
  input.power = 1.0F;
  controlledAction(engine, input);
  ASSERT_TRUE(engine.getBall().possessionState().isPass);
  const PassDecision& pass = engine.getLastPassDecision();
  EXPECT_EQ(pass.receiverId, 109U);
  EXPECT_EQ(pass.intent, PassIntent::THROUGH_BALL);
  // Into space well ahead of him, toward the goal he attacks.
  const MatchPlayer* runner = findOnPitch(engine, 109);
  ASSERT_NE(runner, nullptr);
  EXPECT_GT(pass.targetPoint.x, runner->position.x + 8.0F);
}

TEST(PlayModeTest, JockeyingDefenderHoldsAGoalSideLine)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 70, players);
  Team away = createSquadWithBench(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 44);
  engine.setAutoSubstitutions(false, false);
  constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
  engine.advance(30.0F);
  // Live play: whenever an away outfielder has the ball, the best placed
  // home player is taken over and only jockeys (no stick) for a moment.
  int windows = 0;
  for (int attempt = 0; attempt < 80 && windows < 3; ++attempt)
  {
    const MatchPlayer* carrier = nullptr;
    for (int step = 0; step < 6'000 && (carrier == nullptr); ++step)
    {
      engine.advance(STEP);
      if (engine.getState() != MatchState::PLAYING)
      {
        continue;
      }
      for (const MatchPlayer& candidate : engine.getPlayers())
      {
        if ((candidate.player != nullptr) &&
            candidate.player ==
                engine.getBall().possessionState().possessedBy &&
            !candidate.isHomeTeam && !candidate.isGoalkeeper)
        {
          carrier = &candidate;
        }
      }
    }
    if (carrier == nullptr)
    {
      break;
    }
    const Player* holder = carrier->player;
    const PlayerID defenderId = engine.suggestActivePlayer(true, 0);
    ASSERT_NE(defenderId, 0U);
    ASSERT_TRUE(engine.setControlledPlayer(defenderId));
    MatchPlayerInput input;
    input.jockey = true;
    engine.submitInput(input);
    const float cap = findOnPitch(engine, defenderId)->maxSpeed *
                      MatchTuning::Control::JOCKEY_SPEED_SHARE;
    float previous = std::numeric_limits<float>::infinity();
    int held = 0;
    for (; held < 10; ++held)
    {
      engine.advance(STEP);
      if (engine.getBall().possessionState().possessedBy != holder ||
          engine.getState() != MatchState::PLAYING ||
          engine.getControlledPlayer() != defenderId)
      {
        break;
      }
      const MatchPlayer* defender = findOnPitch(engine, defenderId);
      const float speed =
          std::hypot(defender->velocity.x, defender->velocity.y);
      // Never faster than a contain pace (a run he was on only slows down).
      EXPECT_LE(speed, std::max(cap, previous) + 1e-3F);
      previous = speed;
    }
    if (held == 10)
    {
      const MatchPlayer* defender = findOnPitch(engine, defenderId);
      const MatchPlayer* attacker = findOnPitch(engine, holder->getId());
      ASSERT_NE(defender, nullptr);
      ASSERT_NE(attacker, nullptr);
      // Heading for the spot goal-side of the carrier (between him and the
      // home goal), and facing the ball.
      const float toGoalX = PITCH.atX(0.0F) - attacker->position.x;
      const float toGoalY = PITCH.atY(0.5F) - attacker->position.y;
      const float toGoal = std::hypot(toGoalX, toGoalY);
      const float spotX =
          attacker->position.x +
          (toGoalX / toGoal * MatchTuning::Control::JOCKEY_CONTAIN_METRES);
      const float spotY =
          attacker->position.y +
          (toGoalY / toGoal * MatchTuning::Control::JOCKEY_CONTAIN_METRES);
      const float offsetX = spotX - defender->position.x;
      const float offsetY = spotY - defender->position.y;
      const float metres = std::hypot(offsetX, offsetY);
      if (metres > 1.0F && previous > 0.5F)
      {
        EXPECT_GT(
            (defender->velocity.x * offsetX + defender->velocity.y * offsetY) /
                (metres * previous),
            0.0F)
            << "attempt " << attempt;
      }
      EXPECT_LE(previous, cap + 1e-3F) << "attempt " << attempt;
      const float toBall =
          std::atan2(attacker->position.y - defender->position.y,
                     attacker->position.x - defender->position.x);
      float turn = std::abs(toBall - defender->facingAngle);
      if (turn > std::numbers::pi_v<float>)
      {
        turn = 2.0F * std::numbers::pi_v<float> - turn;
      }
      EXPECT_LT(turn, std::numbers::pi_v<float> * 0.5F)
          << "attempt " << attempt;
      ++windows;
    }
    ASSERT_TRUE(engine.setControlledPlayer(0));
    engine.advance(STEP);
  }
  EXPECT_GE(windows, 1);
}

TEST(PlayModeTest, ReleasedPlayerResumesHisRoleWithoutJitter)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 70, players);
  Team away = createSquadWithBench(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 17);
  engine.setAutoSubstitutions(false, false);
  engine.advance(30.0F);
  for (int step = 0; step < 2'000 && engine.getState() != MatchState::PLAYING;
       ++step)
  {
    engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  }
  ASSERT_EQ(engine.getState(), MatchState::PLAYING);

  // A full-back is run up the line on the stick, then handed back.
  ASSERT_TRUE(engine.setControlledPlayer(101));
  MatchPlayerInput input;
  input.moveX = 1.0F;
  input.sprint = true;
  engine.submitInput(input);
  engine.advance(2.0F);
  const MatchPlayer* player = findOnPitch(engine, 101);
  ASSERT_NE(player, nullptr);
  Vector2F velocity = player->velocity;
  const float runSpeed = std::hypot(velocity.x, velocity.y);
  ASSERT_GT(runSpeed, 3.0F);
  ASSERT_TRUE(engine.setControlledPlayer(0));
  // Within a decision cycle he is the AI's again, and no step changes his
  // velocity by more than his body allows (no stop-start).
  const float limit = (player->braking + player->acceleration) *
                      MatchTuning::Timing::FIXED_STEP_SECONDS;
  for (std::uint64_t step = 0;
       step <= MatchTuning::Timing::TACTICAL_REFRESH_STEPS + 1; ++step)
  {
    engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    player = findOnPitch(engine, 101);
    ASSERT_NE(player, nullptr);
    EXPECT_LE(std::hypot(player->velocity.x - velocity.x,
                         player->velocity.y - velocity.y),
              limit + 1e-3F);
    velocity = player->velocity;
  }
  EXPECT_EQ(engine.getControlledPlayer(), 0U);
  // Released at speed, he does not brake to a standstill on the spot.
  EXPECT_GT(std::hypot(velocity.x, velocity.y), 0.3F * runSpeed);
}

TEST(PlayModeTest, SubstitutedActivePlayerHandsControlBack)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 70, players);
  Team away = createSquadWithBench(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 21);
  engine.setAutoSubstitutions(false, false);
  engine.advance(60.0F);
  ASSERT_TRUE(engine.setControlledPlayer(106));
  engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  ASSERT_EQ(engine.getControlledPlayer(), 106U);
  ASSERT_TRUE(advanceToNextStoppage(engine));
  const Player* bench = home.getLineup().getReserves()[3];
  ASSERT_TRUE(engine.substitutePlayer(106, bench));
  engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  // The newcomer is not silently driven by the old controls.
  EXPECT_EQ(engine.getControlledPlayer(), 0U);
  EXPECT_NE(engine.suggestActivePlayer(true, 0), 0U);
}

TEST(PlayModeTest, PlayedMatchIsRecordedInTheReport)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 70, players);
  Team away = createSquadWithBench(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine played(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 64);
  ASSERT_TRUE(loadControlled(played, playModeScenario(), 105));
  MatchPlayerInput input;
  input.aimX = 10.0F;
  input.action = MatchInputAction::PASS;
  played.submitInput(input);
  played.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  ASSERT_TRUE(played.setControlledPlayer(0));
  played.simulateToEnd();
  EXPECT_TRUE(played.wasControlled(true));
  EXPECT_FALSE(played.wasControlled(false));
  EXPECT_EQ(played.getControlledActions(true), 1);

  MatchReport report;
  report.fillFromEngine(played, 1, 2);
  ASSERT_TRUE(report.played_home.has_value());
  EXPECT_TRUE(*report.played_home);
  EXPECT_GT(report.played_share, 0.0F);
  EXPECT_LT(report.played_share, 0.1F);
  MatchReport loaded;
  loaded.statsFromJson(report.statsToJson());
  ASSERT_TRUE(loaded.played_home.has_value());
  EXPECT_TRUE(*loaded.played_home);
  EXPECT_FLOAT_EQ(loaded.played_share, report.played_share);

  // A watched match carries no such mark, also after a round trip.
  MatchEngine watched(home.getLineup(), away.getLineup(), home.getStrategy(),
                      away.getStrategy(), config, 64);
  watched.simulateToEnd();
  MatchReport plain;
  plain.fillFromEngine(watched, 1, 2);
  EXPECT_FALSE(plain.played_home.has_value());
  MatchReport reloaded;
  reloaded.statsFromJson(plain.statsToJson());
  EXPECT_FALSE(reloaded.played_home.has_value());
}

TEST(MatchEngineTest, MedicalMinuteLimitTakesThePlayerOffAroundTheHour)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 70, players);
  Team away = createSquadWithBench(2, "Away", 70, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine limited(home.getLineup(), away.getLineup(), home.getStrategy(),
                      away.getStrategy(), config, 73);
  // Limited to an hour by the medical staff (MEDICAL_FLAG_LIMIT_MINUTES).
  limited.setMedicalFlags(106, 1U << 1);
  limited.simulateToEnd();
  const auto change = std::ranges::find_if(
      limited.getSubstitutions(), [](const MatchSubstitution& substitution)
      { return substitution.outgoingPlayerId == 106; });
  ASSERT_NE(change, limited.getSubstitutions().end());
  // At least the hour on the pitch, first-half added time included (so on
  // the clock it can be a little before 60'), and off at the next stoppage.
  const PlayerMatchStats* limitedStats = limited.findPlayerStats(106);
  ASSERT_NE(limitedStats, nullptr);
  EXPECT_GE(limitedStats->minutesPlayed, 60.0F);
  EXPECT_LT(limitedStats->minutesPlayed, 72.0F);
  EXPECT_EQ(change->period, 2);
  EXPECT_GE(change->timeMinute,
            60.0F - static_cast<float>(limited.getAddedMinutes(1)) - 1.0F);

  // The limit counts his own minutes: a flagged substitute who comes on
  // around 50' is not taken off soon after the hour (no wasted change).
  MatchEngine substitute(home.getLineup(), away.getLineup(), home.getStrategy(),
                         away.getStrategy(), config, 73);
  // Keep the reserve on the bench until the controlled introduction. Normal
  // tactical or injury changes must not consume the player this case measures.
  substitute.setAutoSubstitutions(false, true);
  const Player* flaggedSub = home.getLineup().getReserves()[3];
  substitute.setMedicalFlags(flaggedSub->getId(), 1U << 1);
  while (substitute.getState() != MatchState::FULL_TIME &&
         substitute.getMatchTimeMinutes() < 50.0F)
  {
    substitute.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  }
  ASSERT_TRUE(advanceToNextStoppage(substitute));
  const auto outgoing = std::ranges::find_if(
      substitute.getPlayers(),
      [&](const MatchPlayer& player)
      {
        return player.player && player.onPitch && player.isHomeTeam &&
               !player.isInjured &&
               player.player->getRole() == flaggedSub->getRole();
      });
  ASSERT_NE(outgoing, substitute.getPlayers().end());
  ASSERT_TRUE(
      substitute.substitutePlayer(outgoing->player->getId(), flaggedSub));
  substitute.setAutoSubstitutions(true, true);
  substitute.simulateToEnd();
  EXPECT_TRUE(std::ranges::none_of(substitute.getSubstitutions(),
                                   [&](const MatchSubstitution& substitution)
                                   {
                                     return substitution.outgoingPlayerId ==
                                                flaggedSub->getId() &&
                                            substitution.timeMinute < 100.0F;
                                   }));
  // He played on to the end of normal time, well short of his hour.
  const PlayerMatchStats* subStats =
      substitute.findPlayerStats(flaggedSub->getId());
  ASSERT_NE(subStats, nullptr);
  EXPECT_LT(subStats->minutesPlayed, 60.0F);

  // Without automatic changes for his side the manager decides.
  MatchEngine manual(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 73);
  manual.setAutoSubstitutions(false, true);
  manual.setMedicalFlags(106, 1U << 1);
  manual.simulateToEnd();
  EXPECT_TRUE(std::ranges::none_of(
      manual.getSubstitutions(), [](const MatchSubstitution& substitution)
      { return substitution.outgoingPlayerId == 106; }));

  // A rest flag alone, or no flag, changes nothing.
  MatchEngine rested(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 73);
  rested.setMedicalFlags(106, 1U << 0);
  rested.simulateToEnd();
  MatchEngine plain(home.getLineup(), away.getLineup(), home.getStrategy(),
                    away.getStrategy(), config, 73);
  plain.simulateToEnd();
  EXPECT_EQ(rested.getEvents().size(), plain.getEvents().size());
  EXPECT_EQ(rested.getHomeScore(), plain.getHomeScore());
  EXPECT_EQ(rested.getAwayScore(), plain.getAwayScore());
}

TEST(MatchEngineTest, TeamTalksReplayFromTheCommandLog)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createSquadWithBench(1, "Home", 69, players);
  Team away = createSquadWithBench(2, "Away", 69, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine live(home.getLineup(), away.getLineup(), home.getStrategy(),
                   away.getStrategy(), config, 515);
  // A talk before kick-off, and the away side's at half-time.
  live.setTeamTalkModifier(true, 1, 0.04F);
  while (live.getState() != MatchState::HALF_TIME &&
         live.getState() != MatchState::FULL_TIME)
  {
    live.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  }
  ASSERT_EQ(live.getState(), MatchState::HALF_TIME);
  live.setTeamTalkModifier(false, 2, -0.05F);
  live.simulateToEnd();
  ASSERT_EQ(std::ranges::count_if(
                live.getCommandLog(), [](const MatchCommandRecord& command)
                { return command.type == MatchCommandType::TEAM_TALK; }),
            2);

  MatchEngine replay(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 515);
  replay.loadCommandReplay(live.getCommandLog());
  replay.simulateToEnd();
  EXPECT_FLOAT_EQ(replay.getTeamTalkModifier(true, 1), 0.04F);
  EXPECT_FLOAT_EQ(replay.getTeamTalkModifier(false, 2), -0.05F);
  EXPECT_EQ(replay.getHomeScore(), live.getHomeScore());
  EXPECT_EQ(replay.getAwayScore(), live.getAwayScore());
  EXPECT_EQ(replay.getEvents().size(), live.getEvents().size());
  EXPECT_EQ(settledSnapshot(replay), settledSnapshot(live));
}

TEST(PlayModeTest, HalfDurationChangesClockWithoutChangingPhysicalMovement)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 65, players);
  Team away = createDummyTeam(2, "Away", 65, players);
  const StatsConfig config = createStatsConfig();
  for (int minutes = 3; minutes <= 15; ++minutes)
  {
    MatchEngine watch(home.getLineup(), away.getLineup(), home.getStrategy(),
                      away.getStrategy(), config, 77);
    MatchEngine play = watch;
    play.setPlayHalfMinutes(minutes);
    ASSERT_TRUE(loadControlled(play, playModeScenario(), 105));
    ASSERT_TRUE(loadControlled(watch, playModeScenario(), 105));
    MatchPlayerInput input;
    input.moveX = 1.0F;
    input.sprint = true;
    play.submitInput(input);
    watch.submitInput(input);
    play.advance(0.1F);
    watch.advance(0.1F);
    EXPECT_EQ(play.getPlayHalfMinutes(), minutes);
    EXPECT_NEAR(play.getMatchTimeMinutes(),
                0.1F / 60.0F * 45.0F / static_cast<float>(minutes), 0.00001F);
    EXPECT_NEAR(watch.getMatchTimeMinutes(), 0.1F / 60.0F, 0.00001F);
    const MatchPlayer* played = findOnPitch(play, 105);
    const MatchPlayer* watched = findOnPitch(watch, 105);
    EXPECT_FLOAT_EQ(played->position.x, watched->position.x);
    EXPECT_FLOAT_EQ(played->position.y, watched->position.y);
    EXPECT_FLOAT_EQ(played->velocity.x, watched->velocity.x);
    EXPECT_FLOAT_EQ(play.getBall().position.x, watch.getBall().position.x);
    EXPECT_NEAR(play.getStats().ballInPlayMinutes,
                watch.getStats().ballInPlayMinutes * 45.0F /
                    static_cast<float>(minutes),
                0.00001F);
    play.setPlayHalfMinutes(0);
    play.setControlledPlayer(0);
    const float before = play.getMatchTimeMinutes();
    play.advance(0.1F);
    EXPECT_EQ(play.getPlayHalfMinutes(), 0);
    EXPECT_NEAR(play.getMatchTimeMinutes() - before, 0.1F / 60.0F, 0.00001F);
  }
}

TEST(PlayModeTest, ClockPacingAndHandBackReplayAtTheSameSteps)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 65, players);
  Team away = createDummyTeam(2, "Away", 65, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine live(home.getLineup(), away.getLineup(), home.getStrategy(),
                   away.getStrategy(), config, 77);
  MatchEngine replay = live;
  live.setPlayHalfMinutes(3);
  live.setControlledPlayer(105);
  live.advance(5.0F);
  live.setPlayHalfMinutes(15);
  live.advance(5.0F);
  live.setPlayHalfMinutes(0);
  live.setControlledPlayer(0);
  live.advance(5.0F);
  replay.loadInputReplay(live.getInputLog());
  replay.advance(15.0F);
  EXPECT_EQ(live.getPlayHalfMinutes(), 0);
  EXPECT_EQ(settledSnapshot(live), settledSnapshot(replay));
}

TEST(PlayModeTest, OutOfPlayRestartsAutomaticallyWithinThreeSeconds)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 65, players);
  Team away = createDummyTeam(2, "Away", 65, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 77);
  engine.setPlayHalfMinutes(15);
  const auto isRestart = [](MatchState state)
  {
    using enum MatchState;
    return state == THROW_IN || state == GOAL_KICK || state == CORNER_KICK ||
           state == FREE_KICK;
  };
  int restarts = 0;
  for (int step = 0; step < 6000 && restarts < 8; ++step)
  {
    engine.advance(0.1F);
    if (!isRestart(engine.getState()))
    {
      continue;
    }
    const auto before = engine.getPlayers();
    int waited = 0;
    while (isRestart(engine.getState()) && waited < 32)
    {
      engine.advance(0.1F);
      ++waited;
    }
    EXPECT_LE(waited, 31);
    EXPECT_FALSE(isRestart(engine.getState()));
    int moved = 0;
    for (size_t slot = 0; slot < before.size(); ++slot)
    {
      if (before[slot].position.x != engine.getPlayers()[slot].position.x ||
          before[slot].position.y != engine.getPlayers()[slot].position.y)
      {
        ++moved;
      }
    }
    EXPECT_GT(moved, 5);
    ++restarts;
  }
  EXPECT_GE(restarts, 4);
}

TEST(PlayModeTest, PassButtonReleasesOutOfPlayBallForTheRestartSide)
{
  using enum MatchState;
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 65, players);
  Team away = createDummyTeam(2, "Away", 65, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 77);
  engine.setPlayHalfMinutes(5);
  int passes = 0;
  for (int step = 0; step < 4000 && passes < 5; ++step)
  {
    engine.advance(0.1F);
    const MatchState state = engine.getState();
    if (state != THROW_IN && state != GOAL_KICK && state != CORNER_KICK &&
        state != FREE_KICK)
    {
      continue;
    }
    const Player* owner = engine.getBall().possessionState().possessedBy;
    ASSERT_NE(owner, nullptr);
    const bool homeRestart = owner->getTeamId() == home.getId();
    const PlayerID controller = homeRestart ? 105 : 205;
    engine.setControlledPlayer(controller);
    MatchPlayerInput input;
    input.action = MatchInputAction::PASS;
    input.aimX = homeRestart ? 1.0F : -1.0F;
    input.aimY = engine.getBall().position.y < PITCH.atY(0.5F) ? 0.5F : -0.5F;
    engine.submitInput(input);
    engine.advance(0.1F);
    EXPECT_EQ(engine.getState(), MatchState::PLAYING);
    EXPECT_EQ(engine.getBall().possessionState().possessedBy, nullptr);
    EXPECT_TRUE(engine.getBall().possessionState().isPass);
    if (state == MatchState::THROW_IN)
    {
      EXPECT_TRUE(engine.getBall().possessionState().fromThrowIn);
    }
    EXPECT_GT(engine.getControlledActions(homeRestart), 0);
    engine.setControlledPlayer(0);
    ++passes;
  }
  EXPECT_GE(passes, 3);
}

TEST(PlayModeTest, RegulationHalfUsesTheChosenRealDuration)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 65, players);
  Team away = createDummyTeam(2, "Away", 65, players);
  const StatsConfig config = createStatsConfig();
  for (const int minutes : {3, 5, 10, 15})
  {
    MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                       away.getStrategy(), config, 77);
    engine.setPlayHalfMinutes(minutes);
    int runningSteps = 0;
    for (int step = 0; step < 12000 && engine.getMatchTimeMinutes() < 45.0F;
         ++step)
    {
      const MatchState before = engine.getState();
      engine.advance(0.1F);
      if (before != MatchState::KICK_OFF && before != MatchState::HALF_TIME)
      {
        ++runningSteps;
      }
    }
    ASSERT_GE(engine.getMatchTimeMinutes(), 45.0F);
    EXPECT_NEAR(static_cast<float>(runningSteps) * 0.1F,
                static_cast<float>(minutes) * 60.0F, 0.3F);
  }
}

namespace
{
[[nodiscard]] StretchMap linearStretchMap()
{
  constexpr auto AXIS = std::to_array<float>({0, 1, 2, 3, 4, 5});
  constexpr float ROW_WEIGHT = 8.0F;
  constexpr float VALUE_SCALE = 1.0F / 64.0F;
  StretchMap map;
  map.angles = AXIS;
  map.distances = AXIS;
  for (std::size_t row = 0; row < map.values.size(); ++row)
  {
    for (std::size_t column = 0; column < map.distances.size(); ++column)
    {
      map.values.at(row).at(column) = ((static_cast<float>(row) * ROW_WEIGHT) +
                                       static_cast<float>(column)) *
                                      VALUE_SCALE;
    }
  }
  return map;
}
}  // namespace

TEST(StretchMapTest, ReproducesEveryKnot)
{
  const StretchMap map = linearStretchMap();
  for (std::size_t row = 0; row < map.values.size(); ++row)
  {
    for (std::size_t column = 0; column < map.distances.size(); ++column)
    {
      EXPECT_FLOAT_EQ(map.at(map.angles.at(row), map.distances.at(column)),
                      map.values.at(row).at(column));
    }
  }
}

TEST(StretchMapTest, InterpolatesAndClampsAtTheEdges)
{
  constexpr float ANGLE = 1.25F;
  constexpr float DISTANCE_SHARE = 2.5F;
  constexpr float EXPECTED_BLEND = 12.5F / 64.0F;
  constexpr float OUTSIDE_AXIS = 100.0F;
  const StretchMap map = linearStretchMap();
  EXPECT_FLOAT_EQ(map.at(ANGLE, DISTANCE_SHARE), EXPECTED_BLEND);
  EXPECT_FLOAT_EQ(map.at(-ANGLE, DISTANCE_SHARE), EXPECTED_BLEND);
  EXPECT_FLOAT_EQ(map.at(0.0F, -OUTSIDE_AXIS), map.values.front().front());
  EXPECT_FLOAT_EQ(map.at(OUTSIDE_AXIS, OUTSIDE_AXIS), map.values.back().back());
}

TEST(PlayModeTest, GoalCelebrationReturnsToKickoffWithinThreeSeconds)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createDummyTeam(1, "Home", 65, players);
  Team away = createDummyTeam(2, "Away", 65, players);
  const StatsConfig config = createStatsConfig();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 77);
  engine.setPlayHalfMinutes(15);
  for (int step = 0; step < 10000 && engine.getState() != MatchState::GOAL;
       ++step)
  {
    engine.advance(0.1F);
  }
  ASSERT_EQ(engine.getState(), MatchState::GOAL);
  const MatchRenderSnapshot snapshot = buildMatchRenderSnapshot(engine);
  EXPECT_FLOAT_EQ(snapshot.goalCelebrationDuration, 3.0F);
  EXPECT_LE(snapshot.goalCelebrationRemaining,
            snapshot.goalCelebrationDuration);
  int waited = 0;
  while (engine.getState() == MatchState::GOAL && waited < 32)
  {
    engine.advance(0.1F);
    ++waited;
  }
  EXPECT_LE(waited, 31);
  EXPECT_EQ(engine.getState(), MatchState::KICK_OFF);
}
