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

bool MatchEngine::serveSideFor(bool home) const
{
  switch (pitch.laws.serveSide)
  {
    case ServeSide::CONCEDED_OR_FOULED:
      return home;
    case ServeSide::ALTERNATE:
      return !lastServeHome;
    case ServeSide::HOME:
      return true;
    case ServeSide::AWAY:
      return false;
  }
  return home;
}

Vector2F MatchEngine::serveInward() const
{
  const PitchPoint in = serveStart();
  const Vector2F way{in.x - pitch.serveStation.x, in.y - pitch.serveStation.y};
  const float metres = std::hypot(way.x, way.y);
  return metres > EPSILON ? Vector2F{way.x / metres, way.y / metres}
                          : Vector2F{0.0f, -1.0f};
}

void MatchEngine::beginServe(bool home, float waitSeconds)
{
  beginStoppage();
  serveHome = serveSideFor(home);
  lastServeHome = serveHome;
  serveRemaining = waitSeconds;
  restartIsSetPiece = false;
  restartTakerIndex.reset();
  restartLineUp.reset();
  deadBall = DeadBallReturn{};
  clearPossession();
  clearFlightState();
  // The ball goes back to the coach: at his feet, on the pitch's side.
  const Vector2F inward = serveInward();
  ball.position = PitchPoint{
      pitch.serveStation.x + inward.x * MatchTuning::Serve::BALL_AHEAD_METRES,
      pitch.serveStation.y + inward.y * MatchTuning::Serve::BALL_AHEAD_METRES};
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

// --- Training dummies (training ground plan section 21) ---------------------
// Shared by every version: solid kit, not a part of the Laws' pitch. A
// dummy's solid shape is an upright cylinder with a domed top: every point
// within its radius of its axis, the vertical line from the ground up to its
// shoulder (its height less the radius).

namespace
{
struct DummyPoint
{
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

DummyPoint between(DummyPoint a, DummyPoint b, float t)
{
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

// The nearest point of the dummy's axis to `at`.
DummyPoint nearestOnAxis(const PitchKit::Dummy& dummy, DummyPoint at)
{
  const PitchKit::DummyShape shape = PitchKit::dummyShape(dummy.kind);
  return {dummy.at.x, dummy.at.y,
          std::clamp(at.z, 0.0f, std::max(0.0f, shape.height - shape.radius))};
}

float gapToAxis(const PitchKit::Dummy& dummy, DummyPoint at)
{
  const DummyPoint near = nearestOnAxis(dummy, at);
  return std::sqrt((at.x - near.x) * (at.x - near.x) + (at.y - near.y) * (at.y - near.y) +
                   (at.z - near.z) * (at.z - near.z));
}

// The ball touches the dummy with its centre this close to the axis.
float dummyReach(const PitchKit::Dummy& dummy)
{
  return PitchKit::dummyShape(dummy.kind).radius + MatchTuning::Units::BALL_RADIUS_METRES;
}

// How far along the straight path from `from` to `to` the ball first meets
// the dummy (coming into it), if it does: as the woodwork's, the path
// stepped through 1 cm at a time, the touch then found by halving.
std::optional<float> dummyContactShare(const PitchKit::Dummy& dummy, DummyPoint from,
                                       DummyPoint to)
{
  constexpr float STEP_METRES = 0.01f;
  constexpr int HALVINGS = 14;
  const float reach = dummyReach(dummy);
  const float path = std::sqrt((to.x - from.x) * (to.x - from.x) +
                               (to.y - from.y) * (to.y - from.y) +
                               (to.z - from.z) * (to.z - from.z));
  const float start = gapToAxis(dummy, from);
  // Nowhere near it.
  if (start - path > reach) return std::nullopt;
  // Already touching: met now, if going further in.
  if (start <= reach)
  {
    if (path <= EPSILON) return std::nullopt;
    const float probe = std::min(1.0f, 0.001f / path);
    return gapToAxis(dummy, between(from, to, probe)) < start ? std::optional<float>{0.0f}
                                                             : std::nullopt;
  }
  const int steps = std::max(1, static_cast<int>(std::ceil(path / STEP_METRES)));
  float before = 0.0f;
  for (int step = 1; step <= steps; ++step)
  {
    const float after = static_cast<float>(step) / static_cast<float>(steps);
    if (gapToAxis(dummy, between(from, to, after)) > reach)
    {
      before = after;
      continue;
    }
    float low = before;
    float high = after;
    for (int halving = 0; halving < HALVINGS; ++halving)
    {
      const float middle = 0.5f * (low + high);
      if (gapToAxis(dummy, between(from, to, middle)) > reach)
        low = middle;
      else
        high = middle;
    }
    return low;
  }
  return std::nullopt;
}
}  // namespace

std::optional<std::pair<std::size_t, float>> MatchEngine::dummyInPath(float seconds) const
{
  const auto& dummies = kit.dummies;
  if (dummies.empty()) return std::nullopt;
  if (state != MatchState::PLAYING || (getPossessionAttachment() && !hasTouchClaim()))
    return std::nullopt;
  return dummyAlong(ball.position, ball.z,
                    PitchPoint{ball.position.x + ball.velocity.x * seconds,
                               ball.position.y + ball.velocity.y * seconds},
                    ball.z + ball.velocityZ * seconds);
}

std::optional<std::pair<std::size_t, float>> MatchEngine::dummyAlong(PitchPoint from,
                                                                     float fromZ,
                                                                     PitchPoint to,
                                                                     float toZ) const
{
  const auto& dummies = kit.dummies;
  const DummyPoint start{from.x, from.y, fromZ};
  const DummyPoint end{to.x, to.y, toZ};
  std::optional<std::pair<std::size_t, float>> first;
  for (std::size_t index = 0; index < dummies.size(); ++index)
  {
    const std::optional<float> share = dummyContactShare(dummies[index], start, end);
    if (share && (!first || *share < first->second)) first = std::pair{index, *share};
  }
  return first;
}

bool MatchEngine::bounceOffDummy(std::size_t index)
{
  if (!reflectOffDummy(index)) return false;
  ++dummyHits;
  // As the woodwork off a ball that is not a shot: a new path, nobody's
  // touch; a shot that hits it is a shot no longer (a loose ball).
  noteWoodworkContact(false);
  if (getPossessionState().isShot) endDeflectedShotFlight();
  return true;
}

bool MatchEngine::reflectOffDummy(std::size_t index)
{
  if (index >= kit.dummies.size()) return false;
  const PitchKit::Dummy& dummy = kit.dummies[index];
  const DummyPoint at{ball.position.x, ball.position.y, ball.z};
  const DummyPoint near = nearestOnAxis(dummy, at);
  const float apart = gapToAxis(dummy, at);
  // Touching: within its reach, to half a millimetre (the touch is found
  // just short of it).
  if (apart > dummyReach(dummy) + 0.0005f || apart <= EPSILON) return false;
  const DummyPoint normal{(at.x - near.x) / apart, (at.y - near.y) / apart,
                          (at.z - near.z) / apart};
  const float into = ball.velocity.x * normal.x + ball.velocity.y * normal.y +
                     ball.velocityZ * normal.z;
  if (into >= 0.0f) return false;
  const float restitution = dummy.kind == PitchKit::DummyKind::POLE
                                ? MatchTuning::Dummy::POLE_RESTITUTION
                                : MatchTuning::Dummy::MANNEQUIN_RESTITUTION;
  const float change = (1.0f + restitution) * into;
  ball.velocity = {ball.velocity.x - change * normal.x, ball.velocity.y - change * normal.y};
  ball.velocityZ -= change * normal.z;
  return true;
}

PitchPoint MatchEngine::keepOutOfDummies(PitchPoint to) const
{
  constexpr float R = MatchTuning::Body::RADIUS_METRES;
  for (const PitchKit::Dummy& dummy : kit.dummies)
  {
    const float reach = PitchKit::dummyShape(dummy.kind).radius + R;
    const float dx = to.x - dummy.at.x;
    const float dy = to.y - dummy.at.y;
    const float apart = std::hypot(dx, dy);
    if (apart >= reach) continue;
    // Straight out from its axis (on it: along the length).
    to = apart > EPSILON ? PitchPoint{dummy.at.x + dx / apart * reach,
                                      dummy.at.y + dy / apart * reach}
                         : PitchPoint{dummy.at.x + reach, dummy.at.y};
  }
  return to;
}

std::optional<PitchPoint> MatchEngine::steerRoundDummies(PitchPoint from,
                                                         PitchPoint target) const
{
  constexpr float R = MatchTuning::Body::RADIUS_METRES;
  const float wayX = target.x - from.x;
  const float wayY = target.y - from.y;
  const float lengthSquared = wayX * wayX + wayY * wayY;
  if (lengthSquared <= EPSILON) return std::nullopt;
  std::optional<PitchPoint> steer;
  float firstAlong = 2.0f;
  for (const PitchKit::Dummy& dummy : kit.dummies)
  {
    const float reach = PitchKit::dummyShape(dummy.kind).radius + R;
    const float clear = reach + MatchTuning::Dummy::STEER_CLEARANCE_METRES;
    const float toX = dummy.at.x - from.x;
    const float toY = dummy.at.y - from.y;
    // Ahead of him and short of his target, and his line within its reach.
    const float along = (toX * wayX + toY * wayY) / lengthSquared;
    if (along <= 0.0f || along >= 1.0f || along >= firstAlong) continue;
    // A target inside it can't be reached anyway: the keep-out holds him.
    if (std::hypot(target.x - dummy.at.x, target.y - dummy.at.y) < reach) continue;
    const float nearX = from.x + wayX * along;
    const float nearY = from.y + wayY * along;
    const float sideX = nearX - dummy.at.x;
    const float sideY = nearY - dummy.at.y;
    const float apart = std::hypot(sideX, sideY);
    if (apart >= reach) continue;
    // The nearest edge: out from its axis towards his line (dead on it: to
    // his left).
    const float length = std::sqrt(lengthSquared);
    const float outX = apart > EPSILON ? sideX / apart : -wayY / length;
    const float outY = apart > EPSILON ? sideY / apart : wayX / length;
    steer = PitchPoint{dummy.at.x + outX * clear, dummy.at.y + outY * clear};
    firstAlong = along;
  }
  return steer;
}

bool MatchEngine::passBlockedByDummy(const MatchPlayer& passer, const PassOption& option) const
{
  if (kit.dummies.empty()) return false;
  constexpr float BALL = MatchTuning::Units::BALL_RADIUS_METRES;
  const PitchPoint from = passer.position;
  const PitchPoint to = option.targetPoint;
  const float wayX = to.x - from.x;
  const float wayY = to.y - from.y;
  const float lengthSquared = wayX * wayX + wayY * wayY;
  if (lengthSquared <= EPSILON) return false;
  const float metres = std::sqrt(lengthSquared);
  for (const PitchKit::Dummy& dummy : kit.dummies)
  {
    const PitchKit::DummyShape shape = PitchKit::dummyShape(dummy.kind);
    // Between them, and his line within its reach.
    const float along = ((dummy.at.x - from.x) * wayX + (dummy.at.y - from.y) * wayY) /
                        lengthSquared;
    if (along <= 0.0f || along >= 1.0f) continue;
    const float sideways = std::hypot(from.x + wayX * along - dummy.at.x,
                                      from.y + wayY * along - dummy.at.y);
    if (sideways >= shape.radius + BALL) continue;
    if (!option.lofted) return true;
    // Lofted: launched as the lofted pass is (a set pace along, lift to
    // drop at the receiver's chest, a cross at head height), its height
    // where it passes the dummy against the dummy's top there.
    using P = MatchTuning::Passing;
    constexpr float G = MatchTuning::Ball::GRAVITY;
    const float arrival = option.intent == PassIntent::CROSS
                              ? MatchTuning::Aerial::ARRIVAL_HEIGHT_METRES
                              : P::LOFTED_ARRIVAL_HEIGHT_METRES;
    const float speed = P::LOFTED_BASE_SPEED + metres * P::LOFTED_SPEED_PER_METRE;
    const float flight = metres / speed;
    const float lift = (arrival - BALL) / flight + 0.5f * G * flight;
    const float seconds = along * flight;
    const float height = BALL + lift * seconds - 0.5f * G * seconds * seconds;
    // The dummy's top where the ball passes: its dome falls away to the side.
    const float shoulder = shape.height - shape.radius;
    const float offset = std::min(sideways, shape.radius);
    const float top = shoulder + std::sqrt(shape.radius * shape.radius - offset * offset);
    if (height - BALL < top) return true;
  }
  return false;
}
