// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Aardvark out of play: the ball leaving play and the restarts as
// inherited with the engine - goals (and who scored), the woodwork, corners,
// goal kicks and throw-ins, the ball in the net, and every restart set up
// and released. Kept frozen so the versions being developed can be compared
// with it (see match_out_of_play.h). Every function here is a MatchEngine
// member with the "Aardvark" suffix, declared in
// match_out_of_play_aardvark_members.h; the engine reaches them through
// match_out_of_play_dispatch.cpp.
//
// Do not change behaviour here.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/player.h"

using namespace MatchEngineInternal;

bool MatchEngine::ballInPlayAtAardvark(Vector2F at) const
{
  // Out once its centre is over a line.
  return at.x > 0.0f && at.x < 1.0f && at.y > 0.0f && at.y < 1.0f;
}

void MatchEngine::checkOutOfBoundsAardvark()
{
  if (ball.position.x <= 0.0f || ball.position.x >= 1.0f)
  {
    const bool rightGoalLine = ball.position.x >= 1.0f;
    const float lineX = rightGoalLine ? 1.0f : 0.0f;
    const float span = ball.position.x - substepBallPosition.x;
    const float t =
        std::abs(span) > EPSILON
            ? std::clamp((lineX - substepBallPosition.x) / span, 0.0f, 1.0f)
            : 1.0f;
    const float crossingY =
        substepBallPosition.y + (ball.position.y - substepBallPosition.y) * t;
    const float crossingHeight = (substepBallZ + (ball.z - substepBallZ) * t) *
                                 MatchTuning::Units::BALL_Z_METRES;
    const float lateralMetres =
        std::abs(crossingY - MatchTuning::Pitch::CENTRE) *
        MatchTuning::Pitch::WIDTH_METRES;
    constexpr float RADIUS = MatchTuning::Units::BALL_RADIUS_METRES;
    constexpr float HALF_GOAL = MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES;
    constexpr float BAR = MatchTuning::Units::CROSSBAR_HEIGHT_METRES;
    if (lateralMetres < HALF_GOAL - RADIUS && crossingHeight < BAR - RADIUS &&
        !getPossessionState().fromThrowIn)
    {
      // Any ball that crosses the line inside the frame is a goal.
      ball.position.y = crossingY;
      scoreGoalAardvark(rightGoalLine);
      return;
    }
    const bool hitsPost =
        std::abs(lateralMetres - HALF_GOAL) <
            RADIUS + MatchTuning::Shooting::WOODWORK_BAND_METRES &&
        crossingHeight < BAR + RADIUS;
    const bool hitsBar =
        lateralMetres < HALF_GOAL + RADIUS &&
        std::abs(crossingHeight - BAR) <
            RADIUS + MatchTuning::Shooting::WOODWORK_BAND_METRES;
    if (getPossessionState().isShot && (hitsPost || hitsBar))
    {
      MatchPlayer* shooter = findMatchPlayer(getPossessionHistory().lastShooter);
      MatchEvent& event = logEvent(MatchEventType::WOODWORK);
      if (shooter)
      {
        event.hasTeam = true;
        event.isHomeTeam = shooter->isHomeTeam;
        event.primaryPlayerId = shooter->player->getId();
      }
      endDeflectedShotFlight();
      ball.position.x = rightGoalLine ? 1.0f - 0.004f : 0.004f;
      ball.position.y = crossingY;
      ball.velocity = {
          -ball.velocity.x * MatchTuning::Shooting::WOODWORK_REBOUND,
          ball.velocity.y + randomFloat(-3.0f, 3.0f)};
      resetRuleContactAttempts();
      ball.velocityZ = std::abs(ball.velocityZ) * 0.3f;
      return;
    }

    const bool lastTouchHome = isHomePlayer(lastTouchForRestart());
    if (!rightGoalLine)
    {
      // Home defends the left goal.
      if (lastTouchHome)
        setupCornerAardvark(false,
                            ball.position.y < MatchTuning::Pitch::CENTRE);
      else
        setupGoalKickAardvark(true);
    }
    else
    {
      // Away defends the right goal.
      if (!lastTouchHome && lastTouchForRestart())
        setupCornerAardvark(true, ball.position.y < MatchTuning::Pitch::CENTRE);
      else
        setupGoalKickAardvark(false);
    }
    return;
  }

  if (ball.position.y <= 0.0f || ball.position.y >= 1.0f)
  {
    const bool receivingHome = !isHomePlayer(lastTouchForRestart());
    setupThrowInAardvark(receivingHome);
  }
}

void MatchEngine::scoreGoalAardvark(bool homeTeam)
{
  MatchPlayer* last = findMatchPlayer(getLastPossessionAttachment());
  MatchPlayer* shooter = findMatchPlayer(getPossessionHistory().lastShooter);
  MatchPlayer* scorer = last;
  // A shot parried or deflected in stays the shooter's goal; only a
  // defender's own deliberate touch makes it an own goal.
  if (shooter && shooter->isHomeTeam == homeTeam &&
      (!last || last->isHomeTeam != homeTeam))
  {
    scorer = shooter;
  }
  const bool ownGoal = scorer && scorer->isHomeTeam != homeTeam;

  if (homeTeam)
    ++homeScore;
  else
    ++awayScore;
  slidersStale = true;
  ++stoppageLogs[static_cast<std::size_t>(period - 1)].goals;

  if (getPossessionState().isShot && scorer && !ownGoal)
  {
    if (getPossessionState().shotIsHeader)
      ++(homeTeam ? stats.homeHeadedGoals : stats.awayHeadedGoals);
    if (getPossessionState().shotFromSetPiece)
      ++(homeTeam ? stats.homeSetPieceGoals : stats.awaySetPieceGoals);
    if (getPossessionState().shotIsPenalty)
      ++(homeTeam ? stats.homePenaltyGoals : stats.awayPenaltyGoals);
  }
  if (scorer && !ownGoal)
  {
    // Every goal is an attempt on target; a knock-down or a ball bundled in
    // without a recorded shot is logged as one here.
    PlayerMatchStats& entry = statsOf(*scorer);
    if (scorer != shooter)
    {
      ++entry.shots;
      ++(homeTeam ? stats.homeShots : stats.awayShots);
    }
    ++entry.shotsOnTarget;
    ++(homeTeam ? stats.homeOnTarget : stats.awayOnTarget);
  }
  PlayerID assisterId = 0;
  if (scorer && !ownGoal)
  {
    ++statsOf(*scorer).goals;
    if (MatchPlayer* assister = findMatchPlayer(getPossessionHistory().shotAssistCandidate);
        assister && assister != scorer && assister->isHomeTeam == homeTeam)
    {
      ++statsOf(*assister).assists;
      assisterId = assister->player->getId();
    }
  }
  else if (scorer)
  {
    ++statsOf(*scorer).ownGoals;
  }
  for (const auto& player : players)
  {
    if (active(player) && player.isHomeTeam != homeTeam)
      ++playerStats[player.statsIndex].goalsConceded;
  }

  MatchEvent& event =
      logEvent(ownGoal ? MatchEventType::OWN_GOAL : MatchEventType::GOAL);
  // The team fields describe the primary player's side, so an own goal is
  // reported for the defender's team (it counts for the other side).
  event.hasTeam = true;
  event.isHomeTeam = scorer ? scorer->isHomeTeam : homeTeam;
  event.primaryPlayerId = scorer ? scorer->player->getId() : 0;
  event.secondaryPlayerId = assisterId;
  event.position = scorer ? scorer->position : ball.position;
  event.xg = getPossessionState().isShot ? getPossessionState().shotXG : 0.0f;

  goalScoredByHome = homeTeam;
  goalCelebrationRemaining = getGoalCelebrationDuration();
  state = MatchState::GOAL;
  clearPossession();
  noteGoalSide(homeTeam);
  pendingAdvantage.active = false;
  restartTakerIndex.reset();
  restartLineUp.reset();
  restartIsSetPiece = false;
  // Everyone walks back to the kick-off formation during the celebration.
  for (auto& player : players)
  {
    if (active(player)) player.movementTarget = kickOffPosition(player);
  }
  updateTeamPhases();
}

void MatchEngine::updateBallInNetAardvark(float dt)
{
  integrateBall(dt);
  const float netDepth = MatchTuning::Ball::GOAL_NET_BALL_DEPTH;
  if (getPossessionState().shotByHome)
  {
    ball.position.x = std::clamp(ball.position.x, 1.0f, 1.0f + netDepth);
  }
  else
  {
    ball.position.x = std::clamp(ball.position.x, -netDepth, 0.0f);
  }
  ball.position.y = std::clamp(ball.position.y, MatchTuning::Pitch::GOAL_TOP,
                               MatchTuning::Pitch::GOAL_BOTTOM);
  // The ball settles into the net instead of bouncing out of the goal mouth.
  ball.velocityZ = 0.0f;
  ball.z = 0.0f;
  ball.spin = {};
}

void MatchEngine::setupThrowInAardvark(bool homeTeam)
{
  beginStoppage();
  restartIsSetPiece = false;
  ball.position.x = std::clamp(
      ball.position.x, MatchTuning::Pitch::RESTART_LONGITUDINAL_MARGIN,
      1.0f - MatchTuning::Pitch::RESTART_LONGITUDINAL_MARGIN);
  ball.position.y = ball.position.y < MatchTuning::Pitch::CENTRE
                        ? MatchTuning::Pitch::RESTART_INSET
                        : 1.0f - MatchTuning::Pitch::RESTART_INSET;
  ball.z = 0.0f;
  ball.velocityZ = 0.0f;
  MatchPlayer* taker =
      chooseRestartTaker(MatchState::THROW_IN, homeTeam, ball.position);
  setRestartTaker(taker);
  // The taker walks to the touchline spot during the stoppage.
  if (taker) taker->movementTarget = ball.position;
  ball.velocity = {0.0f, 0.0f};
  assignRestartOwner(taker ? taker->player : nullptr);
  clearFlightState();
  state = MatchState::THROW_IN;
  setPieceTimer = MatchTuning::Timing::THROW_IN_DELAY_SECONDS;
  updateTeamPhases();
  MatchEvent& event = logEvent(MatchEventType::THROW_IN);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = ball.position;
  if (taker && taker->player) event.primaryPlayerId = taker->player->getId();
}

void MatchEngine::setupGoalKickAardvark(bool homeTeam)
{
  beginStoppage();
  restartIsSetPiece = false;
  ball.position = {homeTeam ? MatchTuning::Pitch::LEFT_GOAL_KICK_X
                            : MatchTuning::Pitch::RIGHT_GOAL_KICK_X,
                   MatchTuning::Pitch::CENTRE};
  ball.z = 0.0f;
  ball.velocityZ = 0.0f;
  MatchPlayer* taker =
      chooseRestartTaker(MatchState::GOAL_KICK, homeTeam, ball.position);
  setRestartTaker(taker);
  if (taker) taker->movementTarget = ball.position;
  ball.velocity = {0.0f, 0.0f};
  assignRestartOwner(taker ? taker->player : nullptr);
  clearFlightState();
  state = MatchState::GOAL_KICK;
  setPieceTimer = MatchTuning::Timing::GOAL_KICK_DELAY_SECONDS;
  updateTeamPhases();
  MatchEvent& event = logEvent(MatchEventType::GOAL_KICK);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = ball.position;
}

void MatchEngine::setupCornerAardvark(bool homeTeam, bool topCorner)
{
  beginStoppage();
  restartIsSetPiece = true;
  ball.position = {homeTeam ? 1.0f - MatchTuning::Pitch::RESTART_INSET
                            : MatchTuning::Pitch::RESTART_INSET,
                   topCorner ? MatchTuning::Pitch::RESTART_INSET
                             : 1.0f - MatchTuning::Pitch::RESTART_INSET};
  ball.z = 0.0f;
  ball.velocityZ = 0.0f;
  // Left and right follow the formation: the corner on the side of the
  // team's left-sided positions (low y) is its left corner.
  MatchPlayer* taker = chooseRestartTaker(MatchState::CORNER_KICK, homeTeam,
                                          ball.position, topCorner);
  setRestartTaker(taker);
  if (taker) placeTakerAardvark(*taker, ball.position);
  ball.velocity = {0.0f, 0.0f};
  assignRestartOwner(taker ? taker->player : nullptr);
  clearFlightState();
  arrangeSetPiece(homeTeam, ball.position);
  state = MatchState::CORNER_KICK;
  setPieceTimer = MatchTuning::Timing::CORNER_DELAY_SECONDS;
  updateTeamPhases();
  if (homeTeam)
    ++stats.homeCorners;
  else
    ++stats.awayCorners;
  MatchEvent& event = logEvent(MatchEventType::CORNER);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = ball.position;
  if (taker && taker->player) event.primaryPlayerId = taker->player->getId();
}

void MatchEngine::setupFreeKickAardvark(bool homeTeam, Vector2F foulPos)
{
  beginStoppage();
  pendingAdvantage.active = false;
  ball.position = {
      std::clamp(foulPos.x, MatchTuning::Pitch::RESTART_LONGITUDINAL_MARGIN,
                 1.0f - MatchTuning::Pitch::RESTART_LONGITUDINAL_MARGIN),
      std::clamp(foulPos.y, MatchTuning::Pitch::RESTART_LONGITUDINAL_MARGIN,
                 1.0f - MatchTuning::Pitch::RESTART_LONGITUDINAL_MARGIN)};
  ball.z = 0.0f;
  ball.velocityZ = 0.0f;
  const Vector2F goal{homeTeam ? 1.0f : 0.0f, MatchTuning::Pitch::CENTRE};
  const float goalMetres = distance(ball.position, goal);
  // Free kicks in the attacking third are set pieces taken by the specialist.
  restartIsSetPiece =
      goalMetres <= MatchTuning::SetPiece::CROSSING_FREE_KICK_METRES;
  MatchPlayer* taker =
      chooseRestartTaker(MatchState::FREE_KICK, homeTeam, ball.position);
  setRestartTaker(taker);
  if (taker && restartIsSetPiece)
    placeTakerAardvark(*taker, ball.position);
  else if (taker)
    taker->movementTarget = ball.position;
  ball.velocity = {0.0f, 0.0f};
  assignRestartOwner(taker ? taker->player : nullptr);
  clearFlightState();
  if (restartIsSetPiece) arrangeSetPiece(homeTeam, ball.position);
  state = MatchState::FREE_KICK;
  setPieceTimer = restartIsSetPiece
                      ? MatchTuning::Timing::SET_PIECE_FREE_KICK_DELAY_SECONDS
                      : MatchTuning::Timing::FREE_KICK_DELAY_SECONDS;
  updateTeamPhases();
  MatchEvent& event = logEvent(MatchEventType::FREE_KICK);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = ball.position;
  if (taker && taker->player) event.primaryPlayerId = taker->player->getId();
}

void MatchEngine::setupPenaltyAardvark(bool homeTeam)
{
  beginStoppage();
  restartIsSetPiece = true;
  pendingAdvantage.active = false;
  const Vector2F spot{homeTeam ? MatchTuning::Pitch::RIGHT_PENALTY_SPOT_X
                               : MatchTuning::Pitch::LEFT_PENALTY_SPOT_X,
                      MatchTuning::Pitch::CENTRE};
  MatchPlayer* taker =
      chooseRestartTaker(MatchState::PENALTY, homeTeam, ball.position);
  setRestartTaker(taker);
  ball.position = spot;
  ball.z = 0.0f;
  ball.velocityZ = 0.0f;
  ball.velocity = {0.0f, 0.0f};
  assignRestartOwner(taker ? taker->player : nullptr);
  if (taker) placeTakerAardvark(*taker, spot);
  // Everyone else waits outside the area; the keeper stands on his line.
  const float edge = homeTeam ? MatchTuning::Pitch::RIGHT_PENALTY_AREA_EDGE -
                                    MatchTuning::SetPiece::PENALTY_WAIT_OFFSET
                              : MatchTuning::Pitch::LEFT_PENALTY_AREA_EDGE +
                                    MatchTuning::SetPiece::PENALTY_WAIT_OFFSET;
  for (auto& player : players)
  {
    if (!active(player) || &player == taker) continue;
    if (player.isGoalkeeper && player.isHomeTeam != homeTeam)
    {
      // Law 14: the defending keeper stays on his goal line.
      player.movementTarget = {homeTeam ? MatchTuning::Pitch::PLAYER_MAX_X
                                        : MatchTuning::Pitch::PLAYER_MIN_X,
                               MatchTuning::Pitch::CENTRE};
      player.position = player.movementTarget;
      continue;
    }
    if (player.isGoalkeeper) continue;
    const bool inside =
        homeTeam ? player.position.x > edge : player.position.x < edge;
    if (inside) player.movementTarget.x = edge;
  }
  clearFlightState();
  if (homeTeam)
    ++stats.homePenalties;
  else
    ++stats.awayPenalties;
  ++stoppageLogs[static_cast<std::size_t>(period - 1)].penalties;
  state = MatchState::PENALTY;
  setPieceTimer = MatchTuning::Timing::PENALTY_DELAY_SECONDS;
  updateTeamPhases();
  MatchEvent& event = logEvent(MatchEventType::PENALTY);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = spot;
  if (taker && taker->player) event.primaryPlayerId = taker->player->getId();
}

void MatchEngine::placeTakerAardvark(MatchPlayer& taker, Vector2F spot)
{
  taker.position = {std::clamp(spot.x, MatchTuning::Pitch::PLAYER_MIN_X,
                               MatchTuning::Pitch::PLAYER_MAX_X),
                    std::clamp(spot.y, MatchTuning::Pitch::PLAYER_MIN_Y,
                               MatchTuning::Pitch::PLAYER_MAX_Y)};
  taker.movementTarget = taker.position;
  taker.velocity = {0.0f, 0.0f};
  // Anyone standing on the spot steps aside for the taker.
  for (auto& other : players)
  {
    if (!active(other) || &other == &taker) continue;
    const float dx = (other.position.x - taker.position.x) *
                     MatchTuning::Pitch::LENGTH_METRES;
    const float dy = (other.position.y - taker.position.y) *
                     MatchTuning::Pitch::WIDTH_METRES;
    const float gap = std::sqrt(dx * dx + dy * dy);
    if (gap >= MatchTuning::Player::MINIMUM_BODY_SEPARATION_METRES) continue;
    const float towardCentre =
        taker.position.y < MatchTuning::Pitch::CENTRE ? 1.0f : -1.0f;
    const Vector2F away = gap > EPSILON ? Vector2F{dx / gap, dy / gap}
                                        : Vector2F{0.0f, towardCentre};
    const float push =
        MatchTuning::Player::MINIMUM_BODY_SEPARATION_METRES - gap;
    other.position.x = std::clamp(
        other.position.x + away.x * push / MatchTuning::Pitch::LENGTH_METRES,
        MatchTuning::Pitch::PLAYER_MIN_X, MatchTuning::Pitch::PLAYER_MAX_X);
    other.position.y = std::clamp(
        other.position.y + away.y * push / MatchTuning::Pitch::WIDTH_METRES,
        MatchTuning::Pitch::PLAYER_MIN_Y, MatchTuning::Pitch::PLAYER_MAX_Y);
  }
}

void MatchEngine::completeRestartAardvark()
{
  // Restart setup functions own placement and waiting; this function owns the
  // actual release. Preserve the restart state until the kick is executed:
  // passBall uses it for offside exemptions and set-piece classification.
  const MatchState restartState = state;
  MatchPlayer* taker = restartTaker();
  const bool userPass = userRestartPassRequested();
  // A Binturong body's taker lines the ball up first: he moves until it is
  // inside his comfort gap, out of his dead zone (restart lining-up).
  if (taker && active(*taker) && linesUpRestart(*taker, restartState) &&
      !lineUpRestart(*taker, restartState, userPass))
    return;
  const std::optional<RestartLineUp> linedUp = restartLineUp;
  restartTakerIndex.reset();
  restartLineUp.reset();
  if (!taker || !active(*taker))
  {
    clearPossession();
    ball.velocity = {0.0F, 0.0F};
    ball.velocityZ = 0.0F;
    clearFlightState();
    restartIsSetPiece = false;
    state = MatchState::PLAYING;
    updateTeamPhases();
    return;
  }

  // The taker has walked to the spot; line him up exactly on the ball
  // (one who lined it up kicks it from where he stands).
  if (linedUp) noteRestartKick(*taker, *linedUp, restartState);
  if (restartState != MatchState::KICK_OFF && !linedUp)
  {
    taker->position = ball.position;
    taker->velocity = {0.0f, 0.0f};
  }
  if (restartState == MatchState::PENALTY)
  {
    if (!linedUp) ball.position = taker->position;
    takeShot(*taker, MatchTuning::Shooting::PENALTY_XG);
    state = MatchState::PLAYING;
    restartIsSetPiece = false;
    updateTeamPhases();
    return;
  }

  // Corners, free kicks near goal and throw-ins in the attacking third open
  // a set-piece phase for the side taking them.
  const float restartDepth =
      taker->isHomeTeam ? ball.position.x : 1.0f - ball.position.x;
  if (restartIsSetPiece ||
      (restartState == MatchState::THROW_IN &&
       restartDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START))
  {
    setPiecePhaseRemaining = MatchTuning::SetPiece::SET_PIECE_PHASE_SECONDS;
    setPieceHome = taker->isHomeTeam;
    setPieceBallInBox = false;
    setPieceBoxCleared = false;
  }
  if (linedUp ? linedUp->userAction != MatchInputAction::NONE : userPass)
  {
    const MatchInputAction action =
        linedUp && linedUp->userAction != MatchInputAction::NONE
            ? linedUp->userAction
            : controlInput.action;
    playControlledPass(*taker, action);
    controlInput.action = MatchInputAction::NONE;
    controlActionRemaining = 0.0F;
    ++controlledActions[taker->isHomeTeam ? 0 : 1];
  }
  else if (linedUp)
    playRestart(*taker, linedUp->play);
  else
    takeSetPiece(*taker, restartState);
  if (state == restartState) state = MatchState::PLAYING;
  restartIsSetPiece = false;
  updateTeamPhases();
}
