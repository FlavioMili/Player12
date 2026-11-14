// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The ball leaving play and every restart go to the match's out-of-play
// version (MatchEngine::setOutOfPlay). To add one: an OutOfPlayId
// (match_out_of_play.h), its members (match_out_of_play_<name>_members.h,
// included in MatchEngine), its definitions (match_out_of_play_<name>.cpp),
// and a case in each function here.

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "model/match_engine.h"
#include "model/match_engine_internal.h"

void MatchEngine::bounceOffWalls()
{
  // Hard walls at the pitch's edge, unlimited height: the ball comes back
  // off a wall keeping the boards' share of its speed into it, its centre a
  // radius clear of the wall (training ground plan section 18).
  constexpr float RADIUS = MatchTuning::Units::BALL_RADIUS_METRES;
  constexpr float RESTITUTION = MatchTuning::DeadBall::BOARD_RESTITUTION;
  const auto bounce = [](float& at, float& speed, float low, float high)
  {
    if (at < low)
    {
      at = low + (low - at) * RESTITUTION;
      speed = std::abs(speed) * RESTITUTION;
    }
    else if (at > high)
    {
      at = high - (at - high) * RESTITUTION;
      speed = -std::abs(speed) * RESTITUTION;
    }
  };
  // The end walls are open at the goal mouths (between the posts' outer
  // edges, under the bar's top): the frame and the net take it from there.
  const bool throughMouth =
      pitch.goal.present() &&
      std::abs(ball.position.y - 0.5f * pitch.width) <
          pitch.goal.halfWidth + pitch.goal.post &&
      ball.z < pitch.goal.crossbar + pitch.goal.post;
  if (!throughMouth)
    bounce(ball.position.x, ball.velocity.x, RADIUS, pitch.length - RADIUS);
  bounce(ball.position.y, ball.velocity.y, RADIUS, pitch.width - RADIUS);
  // Past a goal line it is in a goal: the net holds it, a closed box behind
  // the line (back, sides and roof), stopping it where it meets the net.
  if (pastGoalLine())
  {
    const bool right = ball.position.x > pitch.length;
    const float line = right ? pitch.length : 0.0f;
    const float behind = right ? 1.0f : -1.0f;
    const float back = std::max(0.0f, pitch.goal.netDepth - RADIUS);
    if ((ball.position.x - line) * behind > back)
    {
      ball.position.x = line + behind * back;
      ball.velocity.x = 0.0f;
    }
    const float centre = 0.5f * pitch.width;
    const float side = std::max(0.0f, pitch.goal.halfWidth - RADIUS);
    if (std::abs(ball.position.y - centre) > side)
    {
      ball.position.y = centre + (ball.position.y > centre ? side : -side);
      ball.velocity.y = 0.0f;
    }
    const float roof = std::max(RADIUS, pitch.goal.crossbar - RADIUS);
    if (ball.z > roof)
    {
      ball.z = roof;
      ball.velocityZ = std::min(ball.velocityZ, 0.0f);
    }
  }
}

bool MatchEngine::pastGoalLine() const
{
  return pitch.goal.present() &&
         (ball.position.x < 0.0f || ball.position.x > pitch.length);
}

void MatchEngine::checkOutOfBounds()
{
  // A walled pitch: the walls send it back (bounceOffWalls), so it only
  // leaves through a goal mouth, where the out-of-play version decides.
  if (pitch.edge == PitchEdge::WALLS && !pastGoalLine()) return;
  // A shootout kick (goalkeeper plan step 9): off the pitch it ends the
  // kick, in or out, and nothing restarts.
  if (shootout.live)
  {
    shootoutBallOut();
    return;
  }
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      checkOutOfBoundsBinturong();
      return;
  }
  checkOutOfBoundsAardvark();
}

bool MatchEngine::ballInPlayAt(PitchPoint at) const
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      return ballInPlayAtBinturong(at);
  }
  return ballInPlayAtAardvark(at);
}

void MatchEngine::scoreGoal(bool homeTeam)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      scoreGoalBinturong(homeTeam);
      break;
  }
  if (outOfPlayId == OutOfPlayId::AARDVARK) scoreGoalAardvark(homeTeam);
  // A serving pitch: no kick-off to set up; the coach serves after the stop.
  if (restartsByServe() && state == MatchState::GOAL) holdAfterGoal();
}

void MatchEngine::kickOffAfterGoal(bool homeKickingOff)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      kickOffAfterGoalBinturong(homeKickingOff);
      return;
  }
  setupKickOff(homeKickingOff);
}

void MatchEngine::updateDeadBall(float dt)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      // The ball is put on the spot at once: nothing to bring back.
      return;
    case OutOfPlayId::BINTURONG:
      updateDeadBallBinturong(dt);
      return;
  }
}

std::optional<float> MatchEngine::frameInPath(float seconds) const
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      // The woodwork is met at the goal line (checkOutOfBounds).
      return std::nullopt;
    case OutOfPlayId::BINTURONG:
      return frameInPathBinturong(seconds);
  }
  return std::nullopt;
}

void MatchEngine::bounceOffFrame()
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      return;
    case OutOfPlayId::BINTURONG:
      bounceOffFrameBinturong();
      return;
  }
}

void MatchEngine::keepRestartDistance(MatchPlayer& player)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      // The restart goes on its countdown, wherever anyone is.
      return;
    case OutOfPlayId::BINTURONG:
      keepRestartDistanceBinturong(player);
      return;
  }
}

void MatchEngine::updateBallInNet(float dt)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      updateBallInNetBinturong(dt);
      return;
  }
  updateBallInNetAardvark(dt);
}

void MatchEngine::setupThrowIn(bool homeTeam)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      setupThrowInBinturong(homeTeam);
      return;
  }
  setupThrowInAardvark(homeTeam);
}

void MatchEngine::setupGoalKick(bool homeTeam)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      setupGoalKickBinturong(homeTeam);
      return;
  }
  setupGoalKickAardvark(homeTeam);
}

void MatchEngine::setupCorner(bool homeTeam, bool topCorner)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      setupCornerBinturong(homeTeam, topCorner);
      return;
  }
  setupCornerAardvark(homeTeam, topCorner);
}

void MatchEngine::setupFreeKick(bool homeTeam, PitchPoint foulPos)
{
  // A serving pitch: the coach serves to the side the kick was for.
  if (restartsByServe())
  {
    beginServe(homeTeam, MatchTuning::Serve::WAIT_SECONDS);
    return;
  }
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      setupFreeKickBinturong(homeTeam, foulPos);
      return;
  }
  setupFreeKickAardvark(homeTeam, foulPos);
}

void MatchEngine::setupPenalty(bool homeTeam)
{
  if (restartsByServe())
  {
    beginServe(homeTeam, MatchTuning::Serve::WAIT_SECONDS);
    return;
  }
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      setupPenaltyBinturong(homeTeam);
      return;
  }
  setupPenaltyAardvark(homeTeam);
}

void MatchEngine::placeTaker(MatchPlayer& taker, PitchPoint spot)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      placeTakerBinturong(taker, spot);
      return;
  }
  placeTakerAardvark(taker, spot);
}

void MatchEngine::completeRestart()
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
        if (possessionId == PossessionId::BINTURONG && !restartLineUp &&
            state != MatchState::KICK_OFF && !userRestartPassRequested())
          if (const MatchPlayer* taker = restartTaker(); taker &&
              MatchEngineInternal::active(*taker) && linesUpRestart(*taker, state) &&
              distance(taker->position, ball.position) >
                  touchKnobs[TouchKnob::COMFORT_GAP])
            return;
      break;
    case OutOfPlayId::BINTURONG:
      completeRestartBinturong();
      return;
  }
  completeRestartAardvark();
}

// --- A coach's serve (training ground plan section 20) ----------------------

// Shared by every out-of-play version: a training pitch's restart, not one of
// the Laws' set pieces.

using namespace MatchEngineInternal;

void MatchEngine::holdAfterGoal()
{
  // Nobody fetches the ball for a kick-off, and nobody walks back for one.
  deadBall = DeadBallReturn{};
  restartTakerIndex.reset();
  restartLineUp.reset();
  for (auto& player : players)
    if (active(player)) player.movementTarget = clampToPitch(player.position);
}

void MatchEngine::beginServe(bool home, float waitSeconds)
{
  beginStoppage();
  switch (pitch.laws.serveSide)
  {
    case ServeSide::CONCEDED_OR_FOULED:
      serveHome = home;
      break;
    case ServeSide::ALTERNATE:
      serveHome = !lastServeHome;
      break;
    case ServeSide::HOME:
      serveHome = true;
      break;
    case ServeSide::AWAY:
      serveHome = false;
      break;
  }
  lastServeHome = serveHome;
  serveRemaining = waitSeconds;
  restartIsSetPiece = false;
  restartTakerIndex.reset();
  restartLineUp.reset();
  deadBall = DeadBallReturn{};
  clearPossession();
  clearFlightState();
  // The ball goes back to the coach.
  ball.position = pitch.serveStation;
  ball.z = 0.0f;
  ball.velocity = {0.0f, 0.0f};
  ball.velocityZ = 0.0f;
  ball.spin = {};
  notePathChange();
  // Everyone holds where he is: no restart shape to take up.
  for (auto& player : players)
    if (active(player)) player.movementTarget = clampToPitch(player.position);
  state = MatchState::SERVE;
  updateTeamPhases();
}

void MatchEngine::holdForServe(float dt)
{
  rememberStepStart();
  std::array<std::uint8_t, 32> batch;
  std::size_t batched = 0;
  for (std::size_t slot = 0; slot < players.size() && slot < batch.size(); ++slot)
  {
    MatchPlayer& player = players[slot];
    if (!active(player)) continue;
    // Where he holds is what the brain reads when play goes on.
    player.tacticalTarget = player.movementTarget;
    player.targetVelocity = {0.0f, 0.0f};
    player.routeWaypoints = 0;
    player.arriveAtSpeed = false;
    player.facingRequested = false;
    if (player.fallen)
      fallenBinturong(player, dt);
    else if (player.stumbleSeconds > 0.0f)
      stumbleBinturong(player, dt);
    else if (player.collisionPause > 0.0f)
      coastBinturong(player, dt);
    else
      batch[batched++] = static_cast<std::uint8_t>(slot);
  }
  movePlayers(batch.data(), batched, dt, true);
  updateTouchline(dt);
  collidePlayers(dt);
}

void MatchEngine::settleBallInNet(float dt)
{
  // Under physics, held by the net (the walls' closed box past the line).
  integrateBall(dt);
  bounceOffWalls();
}

void MatchEngine::updateServe(float dt)
{
  holdForServe(dt);
  advanceClock(dt);
  serveRemaining = std::max(0.0f, serveRemaining - dt);
  if (serveRemaining <= 0.0f) deliverServe();
}

PitchPoint MatchEngine::serveStart() const
{
  constexpr float IN = MatchTuning::Serve::START_INSIDE_METRES;
  return PitchPoint{std::clamp(pitch.serveStation.x, IN, pitch.length - IN),
                    std::clamp(pitch.serveStation.y, IN, pitch.width - IN)};
}

MatchPlayer* MatchEngine::serveReceiverOn(bool home, PitchPoint from)
{
  MatchPlayer* best = nullptr;
  float bestScore = 0.0f;
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != home) continue;
    const float fromCoach = distance(player.position, from);
    float score = -fromCoach;
    if (pitch.laws.serveReceiver == ServeReceiver::MOST_OPEN)
    {
      // Farthest from his nearest opponent (nobody against him: nearest
      // the coach).
      float nearest = std::numeric_limits<float>::max();
      for (const auto& other : players)
        if (active(other) && other.isHomeTeam != home)
          nearest = std::min(nearest, distance(player.position, other.position));
      if (nearest < std::numeric_limits<float>::max()) score = nearest;
    }
    if (!best || score > bestScore)
    {
      best = &player;
      bestScore = score;
    }
  }
  return best;
}

void MatchEngine::deliverServe()
{
  using P = MatchTuning::Passing;
  const PitchPoint from = serveStart();
  MatchPlayer* receiver = serveReceiverOn(serveHome, from);
  const PitchPoint to =
      receiver ? receiver->position
               : pitch.at(MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE);
  const Vector2F way{to.x - from.x, to.y - from.y};
  const float metres = std::hypot(way.x, way.y);
  const Vector2F along = metres > EPSILON ? Vector2F{way.x / metres, way.y / metres}
                                          : Vector2F{0.0f, -1.0f};
  // Along the ground, weighted as a pass is (the coach a stand-in passer).
  const float arrival = P::ARRIVAL_SPEED_BASE +
                        MatchTuning::Serve::COACH_PASSING * P::ARRIVAL_SPEED_PASSING +
                        metres * P::ARRIVAL_SPEED_PER_METRE;
  const float speed = std::clamp(groundLaunchSpeed(metres, arrival),
                                 P::MIN_GROUND_SPEED, P::MAX_GROUND_SPEED);
  state = MatchState::PLAYING;
  restartIsSetPiece = false;
  beginFedBall();
  ball.position = from;
  ball.z = 0.0f;
  notePathChange();
  ball.velocity = {along.x * speed, along.y * speed};
  ball.velocityZ = 0.0f;
  ball.spin = {};
  finishFedBall(receiver);
  ++servesTaken;
  updateTeamPhases();
}
