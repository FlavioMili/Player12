// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Aardvark body: how one player's body carries out his brain's choices
// and what it costs him (movement, fitness, kicking, dribbling, first touch,
// goalkeeping, non-contact injuries, and his half of each contest with an
// opponent), as inherited with the engine. Kept frozen so the bodies being
// developed can be compared with it (see match_body.h). Every function here
// is a MatchEngine member with the "Aardvark" suffix, declared in
// match_body_aardvark_members.h; the engine reaches them through the
// dispatchers in match_body_dispatch.cpp.
//
// Do not change behaviour here. A helper shared with the rest of the engine
// (settling a contest, the ball's flight, rules such as
// MatchRules::tackleMake) is shared by every body, so a new body copies it
// before changing it.

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/match_recorder.h"
#include "model/match_rules.h"
#include "model/player.h"

using namespace MatchEngineInternal;

float MatchEngine::executionErrorScaleAardvark(const MatchPlayer& player) const
{
  // Tired players lose precision, and a home crowd lifts the home side a
  // little: both scale the technical error of passes and shots.
  const float fatigue = 1.0f + (1.0f - player.stamina) *
                                   MatchTuning::Fatigue::TECHNIQUE_ERROR_GAIN;
  return fatigue * std::max(0.5f, 1.0f - teamEdge(player.isHomeTeam)) *
         (1.0f +
          weakFootPressure(player) * TacticsTuning::WEAK_FOOT_ERROR_GAIN);
}

void MatchEngine::moveControlledAardvark(MatchPlayer& player, Vector2F desired,
                                         float desiredSpeed, float topSpeed,
                                         float fatigue, float dt,
                                         const MatchPlayer* faceToward)
{
  using C = MatchTuning::Control;
  // Finer sub-steps: the stick is followed with 20 ms kinematics while the
  // AI keeps the common step.
  const float facing = player.facingAngle;
  const float subStep = dt / static_cast<float>(C::PHYSICS_SUBSTEPS);
  for (int step = 0; step < C::PHYSICS_SUBSTEPS; ++step)
    stepKinematicsAardvark(player, desired, desiredSpeed, topSpeed, fatigue,
                           subStep);
  if (faceToward)
  {
    // A jockeying defender keeps his eyes (and hips) on the ball.
    const float target =
        fastAtan2(pitch.fractionY(faceToward->position.y - player.position.y),
                  pitch.fractionX(faceToward->position.x - player.position.x));
    float difference = target - facing;
    if (difference > std::numbers::pi_v<float>)
      difference -= TWO_PI;
    else if (difference < -std::numbers::pi_v<float>)
      difference += TWO_PI;
    player.targetAngle = target;
    player.facingAngle = facing + std::clamp(difference, -player.turnRate * dt,
                                             player.turnRate * dt);
    if (player.facingAngle > std::numbers::pi_v<float>)
      player.facingAngle -= TWO_PI;
    else if (player.facingAngle < -std::numbers::pi_v<float>)
      player.facingAngle += TWO_PI;
  }
}

float MatchEngine::currentTopSpeedAardvark(const MatchPlayer& player) const
{
  using P = MatchTuning::Player;
  const float fatigue =
      std::clamp((1.0f - player.stamina) * INVERSE_FATIGUE_RANGE, 0.0f, 1.0f);
  const float reserve =
      P::EMPTY_RESERVE_TOP_SPEED +
      (1.0f - P::EMPTY_RESERVE_TOP_SPEED) * player.sprintReserve;
  const float injury =
      player.isInjured ? MatchTuning::Injury::INJURED_SPEED_SCALE : 1.0f;
  return player.maxSpeed * (1.0f - P::FATIGUE_TOP_SPEED_LOSS * fatigue) *
         reserve * injury;
}

SpeedBand MatchEngine::speedBandAardvark(const MatchPlayer& player,
                                         MovementMode mode) const
{
  return speedBandFrom(
      mode, MatchRules::walkRunTransitionSpeed(player.heightMetres),
      derivedPhysicalValue(DerivedStat::ENDURANCE,
                           player.derivedStat(DerivedStat::ENDURANCE)),
      currentTopSpeedAardvark(player));
}

FM_KERNEL_CLONES void MatchEngine::integrateMovementsAardvark(
    const std::uint8_t* slots, std::size_t count, float dt, bool walking)
{
  using P = MatchTuning::Player;
  using Pitch = MatchTuning::Pitch;
  constexpr float PI = std::numbers::pi_v<float>;
  // This engine's pitch, read once, so the loops below (several players at
  // a time) do not read it again after each store.
  // Facing angles are in normalised pitch space (fractions); positions
  // step in point units, with the pitch limits in point units too.
  const float inverseLength = pitch.inverseLength;
  const float inverseWidth = pitch.inverseWidth;
  const float unitsPerMetreX = 1.0f;
  const float unitsPerMetreY = 1.0f;
  const float minX = pitch.atX(Pitch::PLAYER_MIN_X);
  const float maxX = pitch.atX(Pitch::PLAYER_MAX_X);
  const float minY = pitch.atY(Pitch::PLAYER_MIN_Y);
  const float maxY = pitch.atY(Pitch::PLAYER_MAX_Y);
  // The movers are gathered into arrays and advanced with straight-line code
  // (every branch of stepKinematicsAardvark() evaluated and selected per
  // player), so the compiler can process several players at once. The
  // arithmetic of each player is exactly that of stepKinematicsAardvark().
  constexpr std::size_t LANES = 32;
  alignas(16) std::array<float, LANES> offsetX;
  alignas(16) std::array<float, LANES> offsetY;
  alignas(16) std::array<float, LANES> velocityX;
  alignas(16) std::array<float, LANES> velocityY;
  alignas(16) std::array<float, LANES> positionX;
  alignas(16) std::array<float, LANES> positionY;
  alignas(16) std::array<float, LANES> facing;
  alignas(16) std::array<std::int32_t, LANES> turning;
  alignas(16) std::array<float, LANES> nextFacing;
  alignas(16) std::array<float, LANES> nextHeading;
  alignas(16) std::array<float, LANES> maxSpeed;
  alignas(16) std::array<float, LANES> stamina;
  alignas(16) std::array<float, LANES> reserve;
  alignas(16) std::array<float, LANES> injury;
  alignas(16) std::array<float, LANES> acceleration;
  alignas(16) std::array<float, LANES> braking;
  alignas(16) std::array<float, LANES> intentScale;
  alignas(16) std::array<float, LANES> urgencyDistance;
  alignas(16) std::array<float, LANES> maxUrgency;
  alignas(16) std::array<float, LANES> talk;
  alignas(16) std::array<float, LANES> shout;
  alignas(16) std::array<float, LANES> turnRate;
  std::array<MatchPlayer*, LANES> movers;
  std::size_t moving = 0;
  for (std::size_t index = 0; index < count; ++index)
  {
    MatchPlayer& player = players[slots[index]];
    PitchPoint target = walking ? player.movementTarget : player.tacticalTarget;
    target.x = std::clamp(target.x, minX, maxX);
    target.y = std::clamp(target.y, minY, maxY);
    // Players react to a moving target with a perception/intent lag; a side
    // unfamiliar with its tactics reacts later.
    const bool urgent = !walking && player.urgentMovement;
    const float targetBlend =
        targetBlends[(player.isHomeTeam ? 0U : 2U) + (urgent ? 1U : 0U)];
    player.movementTarget.x +=
        (target.x - player.movementTarget.x) * targetBlend;
    player.movementTarget.y +=
        (target.y - player.movementTarget.y) * targetBlend;
    const Vector2F offset =
        player.movementTarget - player.position;
    // A player standing on his spot stays put (the common case in stoppages).
    if (offset.x * offset.x + offset.y * offset.y <=
            P::ARRIVAL_DEAD_ZONE_METRES * P::ARRIVAL_DEAD_ZONE_METRES &&
        player.velocity.x == 0.0f && player.velocity.y == 0.0f)
      continue;
    const std::size_t lane = moving++;
    movers[lane] = &player;
    offsetX[lane] = offset.x;
    offsetY[lane] = offset.y;
    velocityX[lane] = player.velocity.x;
    velocityY[lane] = player.velocity.y;
    positionX[lane] = player.position.x;
    positionY[lane] = player.position.y;
    facing[lane] = player.facingAngle;
    maxSpeed[lane] = player.maxSpeed;
    stamina[lane] = player.stamina;
    reserve[lane] = player.sprintReserve;
    injury[lane] =
        player.isInjured ? MatchTuning::Injury::INJURED_SPEED_SCALE : 1.0f;
    acceleration[lane] = player.acceleration;
    braking[lane] = player.braking;
    // How hard he chooses to run is his side's brain's.
    const MovementStyle& style = movementStyle(player.isHomeTeam);
    intentScale[lane] =
        walking
            ? style.restartWalkShare
            : style.intentSpeedShares[static_cast<std::size_t>(player.intent)];
    urgencyDistance[lane] = style.urgencyDistanceMetres;
    maxUrgency[lane] = style.maxUrgencyShare;
    talk[lane] = talkOf(player.isHomeTeam);
    shout[lane] = shoutWorkRate(player.isHomeTeam);
    turnRate[lane] = player.turnRate;
  }

  for (std::size_t lane = 0; lane < moving; ++lane)
  {
    const float targetX = offsetX[lane];
    const float targetY = offsetY[lane];
    const float targetDistance =
        std::sqrt(targetX * targetX + targetY * targetY);
    const float fatigue =
        std::clamp((1.0f - stamina[lane]) * INVERSE_FATIGUE_RANGE, 0.0f, 1.0f);
    const float topSpeed =
        maxSpeed[lane] * (1.0f - P::FATIGUE_TOP_SPEED_LOSS * fatigue) *
        (P::EMPTY_RESERVE_TOP_SPEED +
         (1.0f - P::EMPTY_RESERVE_TOP_SPEED) * reserve[lane]) *
        injury[lane];
    // Players jog while in position but run hard to recover a lost position.
    // Intent speeds are shares of the fresh top speed; tired players choose
    // to run a little less hard and lose the top end.
    const float urgency =
        walking ? intentScale[lane]
                : std::max(intentScale[lane],
                           std::min(maxUrgency[lane],
                                    targetDistance / urgencyDistance[lane]));
    const float speedCap =
        std::min(topSpeed, maxSpeed[lane] *
                               (1.0f - P::FATIGUE_WORK_RATE_LOSS * fatigue +
                                talk[lane] + shout[lane]) *
                               urgency);
    // Arrive without overshooting: the desired speed falls off so a
    // comfortable deceleration stops the player on the target.
    const float approach = targetDistance - P::ARRIVAL_DEAD_ZONE_METRES;
    const float arrivalSpeed =
        std::sqrt(2.0f * P::ARRIVAL_DECELERATION * std::max(approach, 0.0f));
    const float desiredSpeed =
        approach <= 0.0f ? 0.0f : std::min(speedCap, arrivalSpeed);
    // Quotients are formed for every lane and discarded where unused.
    const bool aimed = targetDistance > EPSILON;
    const float directionX = targetX / targetDistance;
    const float directionY = targetY / targetDistance;
    const float desiredX = (aimed ? directionX : 0.0f) * desiredSpeed;
    const float desiredY = (aimed ? directionY : 0.0f) * desiredSpeed;

    // Acceleration-speed profile a(v) = A0 (1 - v / vmax), stronger braking,
    // and a lateral limit that forces a player to slow down to turn sharply.
    const float push =
        acceleration[lane] * (1.0f - P::FATIGUE_ACCELERATION_LOSS * fatigue);
    const float oldX = velocityX[lane];
    const float oldY = velocityY[lane];
    const float speed = std::sqrt(oldX * oldX + oldY * oldY);
    const float changeX = desiredX - oldX;
    const float changeY = desiredY - oldY;
    const float brake = braking[lane];
    const float drive = push * std::max(0.05f, 1.0f - speed / topSpeed);
    // Starting from (almost) a standstill: move straight toward the target.
    const float limit = (desiredSpeed >= speed ? drive : brake) * dt;
    const float magnitude = std::sqrt(changeX * changeX + changeY * changeY);
    const float shortfall = limit / magnitude;
    const float scale = magnitude > limit ? shortfall : 1.0f;
    const float slowX = oldX + changeX * scale;
    const float slowY = oldY + changeY * scale;
    // On the move: accelerate or brake along the run, turn within the limit.
    const float tangentX = oldX / speed;
    const float tangentY = oldY / speed;
    const float normalX = -tangentY;
    const float normalY = tangentX;
    float along = changeX * tangentX + changeY * tangentY;
    float across = changeX * normalX + changeY * normalY;
    along = along > 0.0f ? std::min(along, drive * dt)
                         : std::max(along, -brake * dt);
    const float speedShare = std::clamp(speed / maxSpeed[lane], 0.0f, 1.0f);
    const float lateral =
        (P::LATERAL_ACCELERATION_SLOW +
         (P::LATERAL_ACCELERATION_FAST - P::LATERAL_ACCELERATION_SLOW) *
             speedShare) *
        dt;
    across = std::clamp(across, -lateral, lateral);
    const float fastX = oldX + (tangentX * along + normalX * across);
    const float fastY = oldY + (tangentY * along + normalY * across);
    const bool stop = (desiredSpeed == 0.0f) & (speed <= brake * dt);
    float nextVelocityX = stop ? 0.0f : speed < 0.5f ? slowX : fastX;
    float nextVelocityY = stop ? 0.0f : speed < 0.5f ? slowY : fastY;
    const float squaredSpeed =
        nextVelocityX * nextVelocityX + nextVelocityY * nextVelocityY;
    const float capScale = topSpeed / std::sqrt(squaredSpeed);
    const bool capped = squaredSpeed > topSpeed * topSpeed;
    nextVelocityX = capped ? nextVelocityX * capScale : nextVelocityX;
    nextVelocityY = capped ? nextVelocityY * capScale : nextVelocityY;

    const float nextX = positionX[lane] + nextVelocityX * dt * unitsPerMetreX;
    const float nextY = positionY[lane] + nextVelocityY * dt * unitsPerMetreY;
    const float clampedX = std::clamp(nextX, minX, maxX);
    const float clampedY = std::clamp(nextY, minY, maxY);
    nextVelocityX = clampedX != nextX ? 0.0f : nextVelocityX;
    nextVelocityY = clampedY != nextY ? 0.0f : nextVelocityY;
    positionX[lane] = clampedX;
    positionY[lane] = clampedY;
    velocityX[lane] = nextVelocityX;
    velocityY[lane] = nextVelocityY;

    // Facing (normalised pitch space, for the renderers) turns toward the
    // run at a limited rate; fastAtan2() without branches.
    const float y = nextVelocityY * inverseWidth;
    const float x = nextVelocityX * inverseLength;
    const float absX = std::abs(x);
    const float absY = std::abs(y);
    const float ratio = std::min(absX, absY) / std::max(absX, absY);
    const float ratioSquared = ratio * ratio;
    float angle =
        ((-0.0464964749f * ratioSquared + 0.15931422f) * ratioSquared -
         0.327622764f) *
            ratioSquared * ratio +
        ratio;
    angle = absY > absX ? 1.57079637f - angle : angle;
    angle = x < 0.0f ? 3.14159274f - angle : angle;
    angle = y < 0.0f ? -angle : angle;
    angle = std::max(absX, absY) < 1e-12f ? 0.0f : angle;
    float difference = angle - facing[lane];
    difference = difference > PI    ? difference - TWO_PI
                 : difference < -PI ? difference + TWO_PI
                                    : difference;
    const float turn = turnRate[lane] * dt;
    float turned = facing[lane] + std::clamp(difference, -turn, turn);
    turned = turned > PI    ? turned - TWO_PI
             : turned < -PI ? turned + TWO_PI
                            : turned;
    const bool running = squaredSpeed > P::MOVEMENT_FACING_THRESHOLD *
                                            P::MOVEMENT_FACING_THRESHOLD;
    nextHeading[lane] = angle;
    turning[lane] = running ? 1 : 0;
    nextFacing[lane] = running ? turned : facing[lane];
  }

  for (std::size_t lane = 0; lane < moving; ++lane)
  {
    MatchPlayer& player = *movers[lane];
    player.velocity = {velocityX[lane], velocityY[lane]};
    player.position = PitchPoint{positionX[lane], positionY[lane]};
    player.facingAngle = nextFacing[lane];
    if (turning[lane] != 0) player.targetAngle = nextHeading[lane];
  }
}

[[gnu::always_inline]] inline void MatchEngine::stepKinematicsAardvark(
    MatchPlayer& player, Vector2F desired, float desiredSpeed, float topSpeed,
    float fatigue, float dt)
{
  using P = MatchTuning::Player;
  // Acceleration-speed profile a(v) = A0 (1 - v / vmax), stronger braking,
  // and a lateral limit that forces a player to slow down to turn sharply.
  const float acceleration =
      player.acceleration * (1.0f - P::FATIGUE_ACCELERATION_LOSS * fatigue);
  Vector2F velocity = player.velocity;
  const float speed = length(velocity);
  const Vector2F change{desired.x - velocity.x, desired.y - velocity.y};
  if (desiredSpeed == 0.0f && speed <= player.braking * dt)
  {
    velocity = {0.0f, 0.0f};
  }
  else if (speed < 0.5f)
  {
    const float limit =
        (desiredSpeed >= speed
             ? acceleration * std::max(0.05f, 1.0f - speed / topSpeed)
             : player.braking) *
        dt;
    const float magnitude = length(change);
    const float shortfall = limit / magnitude;
    const float scale = magnitude > limit ? shortfall : 1.0f;
    velocity.x += change.x * scale;
    velocity.y += change.y * scale;
  }
  else
  {
    const Vector2F tangent{velocity.x / speed, velocity.y / speed};
    const Vector2F normal{-tangent.y, tangent.x};
    float along = change.x * tangent.x + change.y * tangent.y;
    float across = change.x * normal.x + change.y * normal.y;
    along =
        along > 0.0f
            ? std::min(along, acceleration *
                                  std::max(0.05f, 1.0f - speed / topSpeed) * dt)
            : std::max(along, -player.braking * dt);
    const float speedShare = std::clamp(speed / player.maxSpeed, 0.0f, 1.0f);
    const float lateral =
        (P::LATERAL_ACCELERATION_SLOW +
         (P::LATERAL_ACCELERATION_FAST - P::LATERAL_ACCELERATION_SLOW) *
             speedShare) *
        dt;
    across = std::clamp(across, -lateral, lateral);
    velocity.x += tangent.x * along + normal.x * across;
    velocity.y += tangent.y * along + normal.y * across;
  }
  const float squaredSpeed = velocity.x * velocity.x + velocity.y * velocity.y;
  if (squaredSpeed > topSpeed * topSpeed)
  {
    const float scale = topSpeed / std::sqrt(squaredSpeed);
    velocity.x *= scale;
    velocity.y *= scale;
  }
  player.velocity = velocity;

  const Vector2F step = {velocity.x * dt, velocity.y * dt};
  const float nextX = player.position.x + step.x;
  const float nextY = player.position.y + step.y;
  player.position.x = clampToPitchX(nextX);
  player.position.y = clampToPitchY(nextY);
  if (player.position.x != nextX) player.velocity.x = 0.0f;
  if (player.position.y != nextY) player.velocity.y = 0.0f;

  if (squaredSpeed >
      P::MOVEMENT_FACING_THRESHOLD * P::MOVEMENT_FACING_THRESHOLD)
  {
    // Facing is kept in normalised pitch space for the renderers.
    player.targetAngle = fastAtan2(player.velocity.y * pitch.inverseWidth,
                                   player.velocity.x * pitch.inverseLength);
    float angleDifference = player.targetAngle - player.facingAngle;
    if (angleDifference > std::numbers::pi_v<float>)
      angleDifference -= TWO_PI;
    else if (angleDifference < -std::numbers::pi_v<float>)
      angleDifference += TWO_PI;
    player.facingAngle += std::clamp(angleDifference, -player.turnRate * dt,
                                     player.turnRate * dt);
    if (player.facingAngle > std::numbers::pi_v<float>)
      player.facingAngle -= TWO_PI;
    else if (player.facingAngle < -std::numbers::pi_v<float>)
      player.facingAngle += TWO_PI;
  }
}

void MatchEngine::spendEnergyAardvark(FitnessLanes& lanes, std::size_t begin,
                                      std::size_t end, float dt,
                                      bool /*homeTeam*/) const
{
  using L = MatchTuning::Load;
  using F = MatchTuning::Fatigue;
  auto& [fromX, fromY, toX, toY, topSpeed, endurance, pressingEffort, sideDrain,
         stamina, reserve, stepMetres, nextStamina, nextReserve,
         stepEnergy, stepAnaerobicEnergy, stepBrakingEnergy,
         stepBrakingAnaerobicEnergy, fastDebt, nextFastDebt] = lanes;
  for (std::size_t lane = begin; lane < end; ++lane)
  {
    const float metresX = fromX[lane] - toX[lane];
    const float metresY = fromY[lane] - toY[lane];
    const float metres = std::sqrt(metresX * metresX + metresY * metresY);
    const float speed = metres / dt;
    const float drain = MatchRules::staminaDrainPerSecond(
                            speed / std::max(topSpeed[lane], EPSILON),
                            endurance[lane], pressingEffort[lane]) +
                        sideDrain[lane];
    stepMetres[lane] = metres;
    nextStamina[lane] = std::clamp(stamina[lane] - drain * dt,
                                   MatchTuning::Player::MINIMUM_STAMINA, 1.0f);
    // Repeat-sprint reserve: emptied by high-intensity running, refilled
    // while jogging or walking.
    const float intensity = std::clamp(
        (speed - L::HIGH_INTENSITY_SPEED) /
            std::max(topSpeed[lane] - L::HIGH_INTENSITY_SPEED, EPSILON),
        0.0f, 1.0f);
    const float drained =
        std::max(0.0f, reserve[lane] - F::RESERVE_DRAIN_PER_SECOND * intensity *
                                           intensity * dt);
    const float refilled = reserve[lane] + (1.0f - reserve[lane]) * dt /
                                               F::RESERVE_RECOVERY_SECONDS;
    nextReserve[lane] = speed > L::HIGH_INTENSITY_SPEED ? drained : refilled;
  }
}

bool MatchEngine::updateDribbleAardvark(MatchPlayer& carrier, float dt)
{
  using R = MatchTuning::Dribble;
  const bool inHands = carrier.isGoalkeeper &&
                       inPenaltyArea(carrier.position, carrier.isHomeTeam);
  const float speed = length(carrier.velocity);
  if (inHands)
  {
    ball.dribbleExposure = 0.0f;
    ball.dribbleTouchLength = 0.0f;
  }
  else if (speed < R::CLOSE_CONTROL_SPEED)
  {
    // Close control: the ball is kept at the feet, shielded by the body.
    ball.dribbleExposure = R::FEET_METRES;
    ball.dribbleTouchLength = 0.0f;
    ball.touchTimer = 0.0f;
    if (speed > EPSILON)
      ball.dribbleDirection = {carrier.velocity.x / speed,
                               carrier.velocity.y / speed};
  }
  else
  {
    ball.touchTimer -= dt;
    if (ball.touchTimer <= 0.0f)
    {
      ++dribbleTouches;
      // A touch pushes the ball ahead along the run; faster runs and poorer
      // dribblers push it further, and pressure risks a heavy touch.
      ball.dribbleDirection = {carrier.velocity.x / speed,
                               carrier.velocity.y / speed};
      const float pressure =
          std::clamp(1.0f - nearestOpponentDistance(carrier) /
                                R::HEAVY_TOUCH_PRESSURE_METRES,
                     0.0f, 1.0f);
      const float heavyChance = R::HEAVY_TOUCH_BASE *
                                (1.0f - carrier.dribbling) *
                                std::min(1.0f, speed / carrier.maxSpeed) *
                                (1.0f + pressure * R::HEAVY_TOUCH_PRESSURE) *
                                executionErrorScaleAardvark(carrier);
      if (randomFloat(0.0f, 1.0f) < heavyChance)
      {
        const float push = speed + randomFloat(R::MIN_HEAVY_PUSH_SPEED,
                                               R::MAX_HEAVY_PUSH_SPEED);
        const PitchPoint origin = ball.position;
        launchBall(carrier, BallRelease{origin, 0.0f, 0.0f},
                   // A far point that way, the direction taken in fractions (as it was).
                   PitchPoint{origin.x + pitch.atX(ball.dribbleDirection.x),
                              origin.y + pitch.atY(ball.dribbleDirection.y)},
                   push, 0.0f, BallSpin{});
        ball.velocity = {ball.dribbleDirection.x * push,
                         ball.dribbleDirection.y * push};
        return false;
      }
      trackTouch(carrier);
      ball.dribbleTouchLength =
          std::clamp(R::TOUCH_BASE_METRES + speed * R::TOUCH_METRES_PER_SPEED *
                                                (1.3f - carrier.dribbling),
                     R::MIN_TOUCH_METRES, R::MAX_TOUCH_METRES);
      ball.dribbleExposure = ball.dribbleTouchLength;
      ball.touchTimer = std::clamp(
          R::TOUCH_INTERVAL_BASE - speed * R::TOUCH_INTERVAL_PER_SPEED -
              carrier.dribbling * R::TOUCH_INTERVAL_DRIBBLING,
          R::MIN_TOUCH_INTERVAL, R::MAX_TOUCH_INTERVAL);
    }
    else
    {
      // The carrier closes on the ball until the next touch.
      ball.dribbleExposure =
          R::FEET_METRES + (ball.dribbleExposure - R::FEET_METRES) *
                               (ball.touchTimer / (ball.touchTimer + dt));
    }
  }
  const Vector2F ahead =
      {ball.dribbleDirection.x * ball.dribbleExposure,
               ball.dribbleDirection.y * ball.dribbleExposure};
  ball.position = PitchPoint{std::clamp(carrier.position.x + ahead.x, pitch.atX(0.001f), pitch.atX(0.999f)),
                   std::clamp(carrier.position.y + ahead.y, pitch.atY(0.001f), pitch.atY(0.999f))};
  ball.z = 0.0f;
  ball.velocity = carrier.velocity;
  return true;
}

MatchEngine::Kick MatchEngine::kickPassAardvark(const MatchPlayer& passer,
                                                const MatchPlayer* receiver,
                                                PitchPoint target,
                                                float passDistance, bool lofted,
                                                bool delivery,
                                                std::optional<BallSpin> /*spin*/,
                                                std::optional<float> arrivalSpeed)
{
  const float pressure =
      std::clamp((MatchTuning::Passing::PASS_PRESSURE_RADIUS_METRES -
                  nearestOpponentDistance(passer)) /
                     MatchTuning::Passing::PASS_PRESSURE_RADIUS_METRES,
                 0.0f, 1.0f);
  // Lateral execution error (metres) grows with poor technique, pressure,
  // distance, fatigue and away nerves. Passes back to the own keeper are
  // played safely.
  const Vector2F direct = metricDirection(passer.position, target);
  const float care = receiver && receiver->isGoalkeeper
                         ? MatchTuning::Passing::BACK_PASS_ERROR_SCALE
                         : 1.0f;
  const float error =
      ((1.0f - passer.passing) * MatchTuning::Passing::TECHNICAL_ERROR_METRES +
       pressure * MatchTuning::Passing::PRESSURE_ERROR_METRES +
       passDistance * MatchTuning::Passing::DISTANCE_ERROR) *
      executionErrorScaleAardvark(passer) * care * randomFloat(-1.0f, 1.0f);
  const Vector2F miss = {-direct.y * error, direct.x * error};
  Kick kick;
  kick.target = PitchPoint{std::clamp(target.x + miss.x, 0.0f, pitch.atX(1.0f)),
                 std::clamp(target.y + miss.y, 0.0f, pitch.atY(1.0f))};
  const float travel = distance(passer.position, kick.target);
  if (lofted)
  {
    // Lofted balls are launched so they drop at the target: long passes at
    // chest height to be controlled, crosses at head height to be attacked.
    // With no receiver (a drill's pass to a spot) it lands on the target.
    const float arrivalMetres =
        !receiver ? 0.0f
        : delivery
            ? MatchTuning::Aerial::ARRIVAL_HEIGHT_METRES +
                  randomFloat(-MatchTuning::Passing::DELIVERY_HEIGHT_SPREAD,
                              MatchTuning::Passing::DELIVERY_HEIGHT_SPREAD)
            : MatchTuning::Passing::LOFTED_ARRIVAL_HEIGHT_METRES;
    kick.speed =
        std::min(MatchTuning::Passing::LOFTED_BASE_SPEED +
                     travel * MatchTuning::Passing::LOFTED_SPEED_PER_METRE,
                 MatchTuning::Passing::MAX_LOFTED_SPEED);
    kick.verticalSpeed =
        loftVerticalSpeed(travel, kick.speed, 0.0f, arrivalMetres);
    return kick;
  }
  // Ground passes arrive at the pace the brain asked for; without one,
  // weighted to arrive at a controllable pace: longer passes and better
  // passers zip the ball in harder.
  const float arrival = arrivalSpeed.value_or(
      MatchTuning::Passing::ARRIVAL_SPEED_BASE +
      passer.passing * MatchTuning::Passing::ARRIVAL_SPEED_PASSING +
      travel * MatchTuning::Passing::ARRIVAL_SPEED_PER_METRE);
  kick.speed = std::clamp(groundLaunchSpeed(travel, arrival),
                          MatchTuning::Passing::MIN_GROUND_SPEED,
                          MatchTuning::Passing::MAX_GROUND_SPEED);
  // A random curve (a heading turn), as sidespin.
  kick.spin.side = randomFloat(-MatchTuning::Passing::MAX_CURVE,
                               MatchTuning::Passing::MAX_CURVE) *
                   (MatchTuning::Passing::CURVE_SKILL_BASE + passer.passing) /
                   MatchTuning::Ball::TURN_PER_SIDESPIN;
  return kick;
}

float MatchEngine::recoverySecondsAardvark(const MatchPlayer& /*player*/,
                                           Recovery after)
{
  // How long before he can act again.
  switch (after)
  {
    case Recovery::SHOT:
      return randomFloat(MatchTuning::Shooting::MIN_ACTION_COOLDOWN,
                         MatchTuning::Shooting::MAX_ACTION_COOLDOWN);
    case Recovery::PASS:
      break;
  }
  return randomFloat(MatchTuning::Passing::MIN_ACTION_COOLDOWN,
                     MatchTuning::Passing::MAX_ACTION_COOLDOWN);
}

float MatchEngine::shotSpreadAardvark(const MatchPlayer& shooter, bool header,
                                      bool penalty, bool freeKick,
                                      float metres) const
{
  const float pressure =
      std::clamp((MatchTuning::Shooting::PRESSURE_RADIUS_METRES -
                  nearestOpponentDistance(shooter)) /
                     MatchTuning::Shooting::PRESSURE_RADIUS_METRES,
                 0.0f, 1.0f);
  // Heading well is as much about strength and timing as about finishing.
  const float finishing =
      header ? shooter.shooting *
                       (1.0f - MatchTuning::Aerial::HEADING_PHYSICAL_SHARE) +
                   shooter.physicality *
                       MatchTuning::Aerial::HEADING_PHYSICAL_SHARE
             : shooter.shooting;
  float spread =
      (MatchTuning::Shooting::ERROR_BASE_METRES +
       (1.0f - finishing) * MatchTuning::Shooting::ERROR_SKILL_METRES +
       pressure * MatchTuning::Shooting::ERROR_PRESSURE_METRES) *
      std::max(metres / MatchTuning::Shooting::ERROR_REFERENCE_METRES,
               MatchTuning::Shooting::ERROR_MIN_DISTANCE_SCALE) *
      executionErrorScaleAardvark(shooter);
  if (header) spread *= MatchTuning::Aerial::HEADER_ACCURACY_PENALTY;
  if (penalty)
    spread *= MatchTuning::Shooting::PENALTY_ERROR_SCALE;
  else
    spread /= finishingPrecision * levelPrecision;
  if (freeKick) spread *= MatchTuning::SetPiece::FREE_KICK_ERROR_SCALE;
  return spread;
}

MatchEngine::ShotStrike MatchEngine::strikeShotBodyAardvark(
    const MatchPlayer& shooter, float aimY, float aimZ, float spread,
    bool header, float powerScale)
{
  ShotStrike strike;
  strike.crossingY = aimY + gaussian(rng) * spread;
  strike.crossingZ =
      std::clamp(aimZ + gaussian(rng) * spread *
                            MatchTuning::Shooting::VERTICAL_ERROR_SHARE,
                 MatchTuning::Shooting::MIN_CROSSING_HEIGHT_METRES,
                 MatchTuning::Shooting::MAX_CROSSING_HEIGHT_METRES);
  strike.speed =
      (MatchTuning::Shooting::BASE_BALL_SPEED +
       shooter.shooting * MatchTuning::Shooting::SHOOTING_SPEED_BONUS +
       shooter.physicality * MatchTuning::Shooting::POWER_SPEED_BONUS) *
      randomFloat(MatchTuning::Shooting::MIN_SPEED_VARIATION, 1.0f) *
      (header ? MatchTuning::Aerial::HEADER_SPEED_SCALE : 1.0f) * powerScale;
  return strike;
}

void MatchEngine::readShotAardvark(const MatchPlayer& keeper,
                                   GoalkeeperControl& control)
{
  // Predict where the shot crosses the keeper's plane and react after a
  // skill-dependent delay; defenders between ball and goal screen the shot.
  const float goalX = pitch.atX(keeper.isHomeTeam ? 0.0f : 1.0f);
  const float span = goalX - ball.position.x;
  const float t =
      std::abs(span) > EPSILON
          ? std::clamp((keeper.position.x - ball.position.x) / span, 0.0f, 1.0f)
          : 1.0f;
  const float predictedY =
      ball.position.y + (getPossessionState().shotTargetY - ball.position.y) * t;
  const float readError =
      gaussian(rng) * MatchTuning::Goalkeeper::READ_ERROR_METRES *
                        (1.0f - keeper.goalkeeping * 0.6f);
  control.diveTargetY = std::clamp(
      predictedY + readError,
      pitch.atY(MatchTuning::Pitch::GOAL_TOP - MatchTuning::Goalkeeper::DIVE_POST_MARGIN),
      pitch.atY(MatchTuning::Pitch::GOAL_BOTTOM +
                MatchTuning::Goalkeeper::DIVE_POST_MARGIN));

  bool screened = false;
  for (const auto& other : players)
  {
    if (!active(other) || other.isGoalkeeper) continue;
    const float along = (other.position.x - ball.position.x) / span;
    if (along <= 0.05f || along >= 0.95f) continue;
    const float laneY =
        ball.position.y + (getPossessionState().shotTargetY - ball.position.y) * along;
    if (std::abs(other.position.y - laneY) <
        MatchTuning::Goalkeeper::SCREEN_WIDTH_METRES)
    {
      screened = true;
      break;
    }
  }
  const float reactionSeconds =
      MatchTuning::Goalkeeper::REACTION_BASE_SECONDS +
      (1.0f - keeper.goalkeeping) *
          MatchTuning::Goalkeeper::REACTION_SKILL_SECONDS +
      (screened ? MatchTuning::Goalkeeper::SCREENED_REACTION_SECONDS : 0.0f);
  control.reactionRemaining = reactionSeconds;
}

void MatchEngine::diveGoalkeeperAardvark(MatchPlayer& keeper, float dt)
{
  GoalkeeperControl& control = keepers[keeper.isHomeTeam ? 0 : 1];
  control.timer += dt;
  if (!getPossessionState().isShot || getPossessionState().shotSaveResolved)
  {
    control.state = GoalkeeperState::RECOVER;
    control.timer = 0.0f;
    keeper.diveTimer = std::max(keeper.diveTimer,
                                MatchTuning::Goalkeeper::RECOVER_TIME_SECONDS);
    keeper.velocity = {0.0f, 0.0f};
    return;
  }
  if (control.reactionRemaining > 0.0f)
  {
    control.reactionRemaining -= dt;
    keeper.velocity = {0.0f, 0.0f};
    return;
  }
  // Lateral dive dynamics in m/s (the keeper moves across the width).
  // Move the visible body into reach rather than granting a distant save.
  constexpr float DIVE_CLOSING_GAIN = 1.25F;
  const float acceleration = MatchTuning::Goalkeeper::DIVE_ACCELERATION_METRES * DIVE_CLOSING_GAIN;
  const float maximumSpeed = std::min(currentTopSpeedAardvark(keeper),
               (MatchTuning::Goalkeeper::DIVE_BASE_SPEED_METRES +
                keeper.goalkeeping *
                    MatchTuning::Goalkeeper::DIVE_SKILL_SPEED_METRES) *
                   DIVE_CLOSING_GAIN);
  const float offset = control.diveTargetY - keeper.position.y;
  const float direction = offset >= 0.0f ? 1.0f : -1.0f;
  control.lateralVelocity =
      std::clamp(control.lateralVelocity + direction * acceleration * dt,
                 -maximumSpeed, maximumSpeed);
  float step = control.lateralVelocity * dt;
  if (std::abs(step) >= std::abs(offset))
  {
    step = offset;
    control.lateralVelocity = 0.0f;
  }
  keeper.position.y = clampToPitchY(keeper.position.y + step);
  keeper.velocity = {0.0f, step / std::max(dt, EPSILON)};
  keeper.movementTarget = keeper.position;
  keeper.isDiving = true;
}

MatchEngine::SaveAttempt MatchEngine::attemptSaveAardvark(
    const MatchPlayer& keeper)
{
  const float span = ball.position.x - substepBallPosition.x;
  const float t =
      std::abs(span) > EPSILON
          ? std::clamp((keeper.position.x - substepBallPosition.x) / span, 0.0f,
                       1.0f)
          : 1.0f;
  const float crossingY =
      substepBallPosition.y + (ball.position.y - substepBallPosition.y) * t;
  const float crossingHeight =
      substepBallZ + (ball.z - substepBallZ) * t;
  const float gapMetres = std::abs(crossingY - keeper.position.y);

  const float lateralReach = MatchRules::goalkeeperHandlingRadiusMetres(
      keeper.heightMetres, keeper.goalkeeping, crossingHeight, keeper.isDiving);
  if (lateralReach <= 0.0F || gapMetres > lateralReach) return {};

  const float stretch = gapMetres / std::max(lateralReach, EPSILON);
  const float speedExcess =
      std::max(0.0f, length(ball.velocity) -
                         MatchTuning::Goalkeeper::COMFORT_SPEED_METRES);
  const float saveChance =
      std::clamp(
          MatchTuning::Goalkeeper::SAVE_BASE -
              stretch * stretch *
                  MatchTuning::Goalkeeper::SAVE_STRETCH_PENALTY -
              speedExcess * MatchTuning::Goalkeeper::SAVE_SPEED_PENALTY +
              (keeper.goalkeeping - MatchTuning::Player::DEFAULT_ATTRIBUTE) *
                  MatchTuning::Goalkeeper::SAVE_SKILL_BONUS,
          MatchTuning::Goalkeeper::MIN_SAVE_CHANCE,
          MatchTuning::Goalkeeper::MAX_SAVE_CHANCE) *
      (getPossessionState().shotIsPenalty ? MatchTuning::Goalkeeper::PENALTY_SAVE_FACTOR
                          : 1.0f);
  if (randomFloat(0.0f, 1.0f) >= saveChance) return {};
  return {true, stretch, speedExcess, crossingHeight};
}

bool MatchEngine::holdsShotAardvark(const MatchPlayer& keeper, float stretch,
                                    float speedExcess)
{
  // A save he can hold rather than parry: good hands, close to him, not
  // hit too hard.
  const float holdChance =
      std::clamp(MatchTuning::Goalkeeper::HOLD_BASE +
                     keeper.goalkeeping * MatchTuning::Goalkeeper::HOLD_SKILL -
                     stretch * MatchTuning::Goalkeeper::HOLD_STRETCH_PENALTY -
                     speedExcess * MatchTuning::Goalkeeper::HOLD_SPEED_PENALTY,
                 MatchTuning::Goalkeeper::MIN_HOLD_CHANCE,
                 MatchTuning::Goalkeeper::MAX_HOLD_CHANCE);
  return randomFloat(0.0f, 1.0f) < holdChance;
}

MatchEngine::Spill MatchEngine::parrySpillAardvark(const MatchPlayer& keeper,
                                                   float speed)
{
  // The ball spills back out off his hands, away from goal.
  const float outward = keeper.isHomeTeam ? 1.0f : -1.0f;
  Spill spill;
  const float x = outward * speed * randomFloat(0.35f, 0.9f);
  const float y = speed * randomFloat(-0.9f, 0.9f);
  spill.velocity = {x, y};
  spill.verticalSpeed =
      randomFloat(0.0f, MatchTuning::Goalkeeper::PARRY_MAX_VERTICAL_SPEED);
  return spill;
}

bool MatchEngine::blocksShotAardvark(const MatchPlayer& player, float gap,
                                     float reach)
{
  const float stretch = gap / std::max(reach, EPSILON);
  // Getting a body in the way of a shot: easier for good defenders and
  // when it comes straight at him.
  return randomFloat(0.0f, 1.0f) <
         (MatchTuning::Defending::BASE_BLOCK_CHANCE +
          player.defending * MatchTuning::Defending::DEFENDING_BLOCK_BONUS) *
             (1.0f - 0.5f * stretch);
}

bool MatchEngine::reachesBallAardvark(const MatchPlayer& player,
                                      float ballSpeed, float stretch)
{
  // A ball not meant for him has to be read and reached: close balls are
  // met, fast wide ones fly past.
  using B = MatchTuning::Ball;
  const float reaction =
      std::clamp(B::INTERCEPT_BASE_CHANCE +
                     player.defending * B::INTERCEPT_DEFENDING_BONUS -
                     ballSpeed * B::INTERCEPT_SPEED_PENALTY,
                 B::INTERCEPT_MIN_CHANCE, 1.0f);
  const float closeness = (1.0f - stretch) * (1.0f - stretch);
  return randomFloat(0.0f, 1.0f) < reaction + (1.0f - reaction) * closeness;
}

float MatchEngine::touchSkillAardvark(const MatchPlayer& player,
                                      bool hands) const
{
  using B = MatchTuning::Ball;
  return hands ? player.goalkeeping
               : std::max({player.dribbling,
                           player.passing * B::PASSING_TOUCH_WEIGHT,
                           player.isGoalkeeper
                               ? player.goalkeeping * B::KEEPER_FEET_WEIGHT
                               : 0.0f});
}

bool MatchEngine::controlsBallAardvark(const MatchPlayer& /*player*/,
                                       float skill, float ballSpeed,
                                       float heightMetres)
{
  using B = MatchTuning::Ball;
  const float controlChance =
      std::clamp(B::BASE_CONTROL_CHANCE + skill * B::TOUCH_SKILL_BONUS -
                     ballSpeed * B::SPEED_CONTROL_PENALTY -
                     (heightMetres > B::LOW_BALL_METRES ? 0.08f : 0.0f),
                 B::MIN_CONTROL_CHANCE, B::MAX_CONTROL_CHANCE);
  return randomFloat(0.0f, 1.0f) <= controlChance;
}

Vector2F MatchEngine::heavyTouchVelocityAardvark(const MatchPlayer& player,
                                                 Vector2F velocity)
{
  using B = MatchTuning::Ball;
  const float angle =
      randomFloat(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);
  const float popSpeed =
      randomFloat(B::MIN_HEAVY_TOUCH_SPEED, B::MAX_HEAVY_TOUCH_SPEED);
  Vector2F popped{
      velocity.x * B::HEAVY_TOUCH_RETAINED + std::cos(angle) * popSpeed,
      velocity.y * B::HEAVY_TOUCH_RETAINED + std::sin(angle) * popSpeed};
  // Near his own goal a player's heavy touch still goes away from it.
  const float ownDepth = player.isHomeTeam
                             ? pitch.fractionX(player.position)
                             : 1.0f - pitch.fractionX(player.position);
  const float outward = player.isHomeTeam ? 1.0f : -1.0f;
  if (ownDepth < MatchTuning::SetPiece::CROSS_CLEARANCE_DEPTH &&
      popped.x * outward < 0.0f)
    popped.x = -popped.x;
  return popped;
}

bool MatchEngine::tryOwnGoalTouchAardvark(MatchPlayer& defender)
{
  using S = MatchTuning::SetPiece;
  // Only a touch in front of his own goal can go in off a defender.
  const float goalLineMetres =
      defender.isHomeTeam ? defender.position.x
                                        : pitch.atX(1.0f) - defender.position.x;
  const float lateralMetres = std::abs(defender.position.y - pitch.atY(MatchTuning::Pitch::CENTRE));
  if (goalLineMetres > S::OWN_GOAL_TOUCH_DEPTH_METRES ||
      lateralMetres > S::OWN_GOAL_TOUCH_WIDTH_METRES ||
      randomFloat(0.0f, 1.0f) >= S::OWN_GOAL_TOUCH_CHANCE)
    return false;
  // The ball skews off him toward the goal he defends.
  const float goalX = pitch.atX(defender.isHomeTeam ? -0.01f : 1.01f);
  const float aimMetres =
      randomFloat(-MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES,
                  MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES);
  const PitchPoint target{goalX, pitch.atY(MatchTuning::Pitch::CENTRE) +
                                   aimMetres};
  const float height = ball.z;
  clearFlightState();
  // It skews off him at the height it met him.
  launchBall(defender,
             BallRelease{ball.position, height,
                         height},
             target,
             randomFloat(S::OWN_GOAL_MIN_SPEED, S::OWN_GOAL_MAX_SPEED),
             randomFloat(0.0f, S::OWN_GOAL_MAX_LIFT), BallSpin{});
  clearShooterHistory();
  return true;
}

float MatchEngine::clearanceSpeedAardvark(const MatchPlayer& /*defender*/)
{
  return MatchTuning::Defending::CLEARANCE_SPEED * randomFloat(0.85f, 1.0f);
}

float MatchEngine::headerClearanceSpeedAardvark(const MatchPlayer& /*header*/)
{
  return MatchTuning::Aerial::HEADER_CLEARANCE_SPEED *
         randomFloat(MatchTuning::Aerial::MIN_CLEARANCE_SHARE, 1.0f);
}

std::pair<float, float> MatchEngine::headerPaceAardvark(
    const MatchPlayer& /*header*/, HeaderPace /*purpose*/, PitchPoint /*target*/,
    float speed, float lift) const
{
  return {speed, lift};
}

PitchPoint MatchEngine::knockDownPointAardvark(const MatchPlayer& /*header*/,
                                             PitchPoint aim)
{
  // A knock-down is never quite where it was meant to go.
  const float x = aim.x + pitch.atX(randomFloat(-0.03f, 0.03f));
  const float y = aim.y + pitch.atY(randomFloat(-0.03f, 0.03f));
  return PitchPoint{x, y};
}

void MatchEngine::settleOnBallAardvark(MatchPlayer& player, float touchSkill,
                                       bool hands)
{
  // The touch that brings the ball under control takes a moment, longer
  // for a poor touch; a keeper with it in his hands holds it a while.
  using B = MatchTuning::Ball;
  player.isTrapping = true;
  player.trapTimer =
      B::BASE_TRAP_TIME + (1.0f - touchSkill) * B::TRAP_SKILL_PENALTY;
  player.actionCooldown =
      hands ? keeperHoldSecondsAardvark(player)
            : player.trapTimer +
                  randomFloat(B::MIN_POST_TOUCH_DELAY, B::MAX_POST_TOUCH_DELAY);
}

float MatchEngine::keeperHoldSecondsAardvark(const MatchPlayer& /*keeper*/)
{
  return randomFloat(MatchTuning::Ball::MIN_KEEPER_HOLD_SECONDS,
                     MatchTuning::Ball::MAX_KEEPER_HOLD_SECONDS);
}

float MatchEngine::injuryChanceAardvark(const MatchPlayer& player) const
{
  // Non-contact risk grows with tiredness and with running hard.
  const float speedRatio =
      length(player.velocity) / std::max(player.maxSpeed, EPSILON);
  const float tiredness = 1.0f - player.stamina;
  const float hazard =
      MatchTuning::Injury::BASE_HAZARD_PER_MINUTE *
      (1.0f +
       tiredness * tiredness * MatchTuning::Injury::FATIGUE_HAZARD_MULTIPLIER) *
      (1.0f + speedRatio * MatchTuning::Injury::INTENSITY_HAZARD_MULTIPLIER);
  return 1.0f - std::exp(-hazard * MatchTuning::Injury::CHECK_INTERVAL_MINUTES);
}

// ---------------------------------------------------------------------------
// Contest halves: what this player's body is worth in a duel with a player of
// the other side (whose own body gives the other half). Each is in the
// duel's own units, on the scale these Aardvark halves set (see
// match_engine_contests_plan.md, section 3); a new body keeps to it.
// ---------------------------------------------------------------------------

float MatchEngine::takeOnMakeAardvark(const MatchPlayer& carrier,
                                      ScoreBreakdown* terms) const
{
  using R = MatchTuning::Dribble;
  const float skill = carrier.dribbling * R::TAKE_ON_SKILL;
  const float pace = carrier.pace * R::TAKE_ON_PACE;
  if (terms)
  {
    terms->add("his Dribbling", TermSource::ATTRIBUTE, skill);
    terms->add("his Pace", TermSource::ATTRIBUTE, pace);
  }
  return skill + pace;
}

float MatchEngine::takeOnResistAardvark(const MatchPlayer& defender,
                                        ScoreBreakdown* terms) const
{
  using R = MatchTuning::Dribble;
  const float skill = defender.defending * R::TAKE_ON_SKILL;
  const float pace = defender.pace * R::TAKE_ON_PACE;
  if (terms)
  {
    terms->add("defender's Defending", TermSource::ATTRIBUTE, skill);
    terms->add("defender's Pace", TermSource::ATTRIBUTE, pace);
  }
  return skill + pace;
}

float MatchEngine::tackleMakeAardvark(const MatchPlayer& tackler,
                                      ScoreBreakdown* terms) const
{
  using D = MatchTuning::Defending;
  if (terms)
  {
    terms->add("his Defending", TermSource::ATTRIBUTE,
               tackler.defending * D::DEFENDING_WIN_BONUS);
    terms->add("his Physicality", TermSource::ATTRIBUTE,
               tackler.physicality * D::PHYSICALITY_DUEL_WEIGHT);
  }
  return MatchRules::tackleMake(tackler.defending, tackler.physicality);
}

float MatchEngine::tackleResistAardvark(const MatchPlayer& carrier,
                                        bool shielding,
                                        ScoreBreakdown* terms) const
{
  using D = MatchTuning::Defending;
  if (terms)
  {
    terms->add("carrier's Dribbling", TermSource::ATTRIBUTE,
               carrier.dribbling * D::DRIBBLING_WIN_PENALTY);
    terms->add("carrier's Physicality", TermSource::ATTRIBUTE,
               carrier.physicality * D::PHYSICALITY_DUEL_WEIGHT);
    terms->add("carrier shielding (his Physicality)", TermSource::ATTRIBUTE,
               shielding ? carrier.physicality * D::SHIELD_PHYSICALITY_PENALTY
                         : 0.0f);
  }
  return MatchRules::tackleResist(carrier.dribbling, carrier.physicality,
                                  shielding);
}

float MatchEngine::tackleTechniqueAardvark(const MatchPlayer& tackler,
                                           ScoreBreakdown* terms) const
{
  const float technique = MatchRules::tackleTechnique(tackler.defending);
  if (terms)
    terms->add("tackling technique (Defending)", TermSource::ATTRIBUTE,
               technique);
  return technique;
}

float MatchEngine::aerialStrengthAardvark(const MatchPlayer& player,
                                          float ballHeightMetres) const
{
  return MatchRules::aerialDuelStrength(player.heightMetres,
                                        player.physicality, ballHeightMetres);
}

float MatchEngine::claimChanceAardvark(const MatchPlayer& keeper,
                                       int crowding) const
{
  using A = MatchTuning::Aerial;
  return std::clamp(A::BASE_CLAIM_CHANCE +
                        keeper.goalkeeping * A::SKILL_CLAIM_BONUS -
                        static_cast<float>(crowding) * A::CROWDING_PENALTY,
                    0.05f, 0.97f);
}

void MatchEngine::recoverFromTakeOnAardvark(MatchPlayer& defender)
{
  using R = MatchTuning::Dribble;
  defender.tackleCooldown =
      std::max(defender.tackleCooldown, R::BEATEN_SECONDS);
  defender.velocity.x *= R::BEATEN_VELOCITY_RETAINED;
  defender.velocity.y *= R::BEATEN_VELOCITY_RETAINED;
}

// --- Striking and the wall ---------------------------------------------------

void MatchEngine::takeDirectFreeKickAardvark(MatchPlayer& taker)
{
  // The wall blocks a share of attempts; the rest are struck over it with
  // a little less precision, and the keeper's view is partly screened.
  const float wallChance =
      std::clamp(MatchTuning::SetPiece::WALL_BLOCK_BASE -
                     taker.shooting * MatchTuning::SetPiece::WALL_BLOCK_SKILL,
                 0.05f, 0.6f);
  if (randomFloat(0.0f, 1.0f) < wallChance)
  {
    takeShot(taker);
    const float outward = taker.isHomeTeam ? -1.0f : 1.0f;
    const float speed =
        length(ball.velocity) * MatchTuning::SetPiece::WALL_REBOUND_SPEED_SHARE;
    resetRuleContactAttempts();
    MatchPlayer* wallPlayer = nullptr;
    float best = std::numeric_limits<float>::max();
    for (auto& player : players)
    {
      if (!active(player) || player.isHomeTeam == taker.isHomeTeam ||
          player.isGoalkeeper)
        continue;
      const float d = distance(player.position, ball.position);
      if (d < best)
      {
        best = d;
        wallPlayer = &player;
      }
    }
    markWallStoppedShot();
    ball.velocity = {outward * speed * randomFloat(0.3f, 1.0f),
                     speed * randomFloat(-0.8f, 0.8f)};
    ball.velocityZ = randomFloat(0.0f, MatchTuning::Aerial::HEADER_LIFT);
    if (wallPlayer)
    {
      ball.position = wallPlayer->position;
      creditWallBlocker(*wallPlayer);
    }
    return;
  }
  takeShot(taker, MatchTuning::SetPiece::DIRECT_FREE_KICK_XG, false, true);
  if (MatchPlayer* keeper = findGoalkeeper(!taker.isHomeTeam))
  {
    keepers[keeper->isHomeTeam ? 0 : 1].reactionRemaining +=
        MatchTuning::SetPiece::WALL_SCREEN_SECONDS;
  }
}

bool MatchEngine::playsFirstTouchAardvark(const MatchPlayer& /*player*/,
                                          bool /*hands*/,
                                          float /*heightMetres*/) const
{
  // The Aardvark body traps every ball the old way.
  return false;
}

float MatchEngine::feetReachAardvark(const MatchPlayer& /*player*/) const
{
  return MatchTuning::Ball::OUTFIELD_CONTROL_RADIUS_METRES;
}

BodyLines MatchEngine::bodyLinesAardvark(const MatchPlayer& /*player*/) const
{
  // The same fixed lines for everyone, and no thigh.
  return BodyLines{};
}

bool MatchEngine::ballBehindReachAardvark(const MatchPlayer& /*player*/,
                                          float /*gapX*/, float /*gapY*/,
                                          float /*gap*/,
                                          float /*turnSeconds*/) const
{
  // He reaches all round.
  return false;
}

PitchPoint MatchEngine::strikePointAardvark(const MatchPlayer& striker) const
{
  // Measured from his centre.
  return striker.position;
}

MatchEngine::Kick MatchEngine::strikeAsAardvark(const MatchPlayer& /*striker*/,
                                                StrikeKind /*kind*/,
                                                PitchPoint target, float speed,
                                                float lift)
{
  // Struck exactly as asked.
  Kick kick;
  kick.target = target;
  kick.speed = speed;
  kick.verticalSpeed = lift;
  return kick;
}

float MatchEngine::clearanceDistanceAardvark(const MatchPlayer& /*defender*/,
                                             PitchPoint /*origin*/,
                                             const ClearanceAim& aim) const
{
  // The distance his brain aimed for.
  return aim.metres;
}

MatchEngine::BallRelease MatchEngine::releaseFromAardvark(const MatchPlayer& kicker,
                                                 ReleaseKind kind) const
{
  BallRelease from;
  switch (kind)
  {
    case ReleaseKind::PASS:
      // From where the ball is, off the ground.
      from.position = ball.position;
      break;
    case ReleaseKind::SHOT:
      // From where the ball is, at the shot's release height.
      from.position = ball.position;
      from.heightMetres = MatchTuning::Shooting::RELEASE_HEIGHT_METRES;
      from.z = from.heightMetres;
      break;
    case ReleaseKind::HEADED_SHOT:
      // From where the ball is, at its height.
      from.position = ball.position;
      from.heightMetres = ball.z;
      from.z = from.heightMetres;
      break;
    case ReleaseKind::CLEARANCE:
      // A loose ball where it lies, else where he meets it; off the ground.
      from.position = hasTouchClaim() ? ball.position : strikePointAardvark(kicker);
      break;
    case ReleaseKind::CLEAR_BEHIND:
    case ReleaseKind::HEADER:
      // Where he meets it, at its height.
      from.position = strikePointAardvark(kicker);
      from.z = ball.z;
      from.heightMetres = ball.z;
      break;
  }
  return from;
}

void MatchEngine::meetWithHeadAardvark(const MatchPlayer& header)
{
  ball.position = strikePointAardvark(header);
}

bool MatchEngine::followsRoutesAardvark(const MatchPlayer& /*player*/) const
{
  // No routes: he heads straight for his target.
  return false;
}
