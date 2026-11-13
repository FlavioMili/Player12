// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/render/match_render_snapshot.h"

#include <cmath>

#include "model/match_engine.h"
#include "model/match_ground.h"

namespace
{
/** Deep in the tunnel, out of sight (as the engine's deepInTunnel). */
bool inTunnel(PitchPoint at)
{
  return MatchPitch::matchSize().metresY(at) > MatchGround::Match::STAND_Y + MatchTuning::WalkOut::IN_METRES;
}
}  // namespace

void fillMatchRenderSnapshot(const MatchEngine& engine,
                             MatchRenderSnapshot& snapshot)
{
  const auto& players = engine.getPlayers();
  const auto& previousPositions = engine.getPreviousPlayerPositions();
  const auto& previousFacingAngles = engine.getPreviousPlayerFacingAngles();
  const auto& previousFeetHeights = engine.getPreviousPlayerFeetHeights();
  snapshot.players.clear();
  snapshot.players.reserve(players.size());
  for (std::size_t index = 0; index < players.size(); ++index)
  {
    const MatchPlayer& source = players[index];
    MatchRenderPlayer& renderPlayer = snapshot.players.emplace_back();
    renderPlayer.player = source.player;
    renderPlayer.isHomeTeam = source.isHomeTeam;
    renderPlayer.currentPosition = source.position;
    renderPlayer.movementTarget = source.movementTarget;
    renderPlayer.intent = source.intent;
    renderPlayer.currentFacingAngle = source.facingAngle;
    renderPlayer.stamina = source.stamina;
    renderPlayer.possessesBall = source.player != nullptr &&
                                 engine.getPossessionAttachment() == source.player;
    renderPlayer.onPitch = source.onPitch;
    renderPlayer.hidden = inTunnel(source.position);
    renderPlayer.isGoalkeeper = source.isGoalkeeper;
    renderPlayer.isDiving = source.isDiving;
    renderPlayer.isInjured = source.isInjured;
    renderPlayer.yellowCards = source.yellowCards;
    renderPlayer.heightMetres = source.heightMetres;
    renderPlayer.speedMetresPerSecond =
        std::sqrt(source.velocity.x * source.velocity.x +
                  source.velocity.y * source.velocity.y);
    renderPlayer.tackleCooldown = source.tackleCooldown;
    renderPlayer.fallen = source.fallen;
    renderPlayer.lyingAngle = source.lyingAngle;
    renderPlayer.onFront = source.onFront();
    renderPlayer.getUpShare = source.getUpShare();
    renderPlayer.upFacingAngle = source.slideAngle;
    renderPlayer.previousPosition = index < previousPositions.size()
                                        ? previousPositions[index]
                                        : renderPlayer.currentPosition;
    renderPlayer.previousFacingAngle = index < previousFacingAngles.size()
                                           ? previousFacingAngles[index]
                                           : renderPlayer.currentFacingAngle;
    renderPlayer.currentFeetHeightMetres = source.feetHeight;
    renderPlayer.previousFeetHeightMetres = index < previousFeetHeights.size()
                                                ? previousFeetHeights[index]
                                                : source.feetHeight;
  }

  const auto& bodies = engine.getTouchlineBodies();
  const auto& people = engine.getTouchlinePeople();
  const auto& previousTouchline = engine.getPreviousTouchlinePositions();
  const auto& previousTouchlineFacing = engine.getPreviousTouchlineFacingAngles();
  snapshot.touchline.clear();
  for (std::size_t index = 0; index < bodies.size() && index < people.size(); ++index)
  {
    const MatchPlayer& body = bodies[index];
    MatchRenderTouchlinePerson& person = snapshot.touchline.emplace_back();
    person.player = body.player;
    person.isHomeTeam = body.isHomeTeam;
    person.role = people[index].role;
    person.seated = people[index].seated;
    person.hidden = inTunnel(body.position);
    person.currentPosition = body.position;
    person.previousPosition =
        index < previousTouchline.size() ? previousTouchline[index] : body.position;
    person.currentFacingAngle = body.facingAngle;
    person.previousFacingAngle = index < previousTouchlineFacing.size()
                                     ? previousTouchlineFacing[index]
                                     : body.facingAngle;
    person.heightMetres = body.heightMetres;
    person.speedMetresPerSecond =
        std::sqrt(body.velocity.x * body.velocity.x + body.velocity.y * body.velocity.y);
    person.fallen = body.fallen;
    person.lyingAngle = body.lyingAngle;
    person.onFront = body.onFront();
    person.getUpShare = body.getUpShare();
  }

  const MatchBall& ball = engine.getBall();
  snapshot.ball.currentPosition = ball.position;
  snapshot.ball.previousPosition = engine.getPreviousBallPosition();
  snapshot.ball.currentZ = ball.z;
  snapshot.ball.previousZ = engine.getPreviousBallZ();
  snapshot.ball.possessedBy = engine.getPossessionAttachment();
  snapshot.ball.outOfSight = engine.ballOutOfSight();
  snapshot.ball.currentHeightMetres =
      MatchPitch::heightMetres(ball.z);
  snapshot.ball.previousHeightMetres =
      MatchPitch::heightMetres(engine.getPreviousBallZ());
  snapshot.ball.isShot = ball.possessionState().isShot;
  snapshot.ball.isAerialDelivery = ball.possessionState().isAerialDelivery;
  snapshot.ball.fromThrowIn = ball.possessionState().fromThrowIn;
  snapshot.ball.kicker = engine.getLegacyContactActor();
  snapshot.ball.kickerLockout = ball.kickerLockout;

  snapshot.pitchLengthMetres = engine.getPitch().length;
  snapshot.pitchWidthMetres = engine.getPitch().width;
  snapshot.state = engine.getState();
  snapshot.homePhase = engine.getHomePhase();
  snapshot.awayPhase = engine.getAwayPhase();
  snapshot.transitionSecondsRemaining = engine.getTransitionSecondsRemaining();
  snapshot.restartReady = engine.isRestartReady();
  snapshot.advantagePending = engine.isAdvantagePending();
  snapshot.homeScore = engine.getHomeScore();
  snapshot.awayScore = engine.getAwayScore();
  snapshot.goalScoredByHome = engine.getGoalScoredByHome();
  snapshot.goalCelebrationRemaining = engine.getGoalCelebrationRemaining();
  snapshot.goalCelebrationDuration = engine.getGoalCelebrationDuration();
  snapshot.matchTimeMinutes = engine.getMatchTimeMinutes();
  snapshot.events = &engine.getEvents();
  snapshot.stats = &engine.getStats();
  snapshot.interpolationAlpha = engine.getInterpolationAlpha();
  snapshot.period = engine.getPeriod();
  snapshot.addedMinutesFirstHalf = engine.getAddedMinutes(1);
  snapshot.addedMinutesSecondHalf = engine.getAddedMinutes(2);
  snapshot.homeGoalkeeperState = engine.getHomeGoalkeeperState();
  snapshot.awayGoalkeeperState = engine.getAwayGoalkeeperState();
  snapshot.playerStats = &engine.getPlayerStats();
}

MatchRenderSnapshot buildMatchRenderSnapshot(const MatchEngine& engine)
{
  MatchRenderSnapshot snapshot;
  fillMatchRenderSnapshot(engine, snapshot);
  return snapshot;
}
