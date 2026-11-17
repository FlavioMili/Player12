// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Aardvark ball physics: the ball on its own as inherited with the
// engine - flight under gravity and drag (with the drag crisis), bounces,
// rolling, the swerve of spin, and the launch solvers built on them. Kept
// frozen so the ball physics being developed can be compared with it (see
// match_ball_physics.h). Every function here is a MatchEngine member with
// the "Aardvark" suffix, declared in match_ball_physics_aardvark_members.h;
// the engine reaches them through match_ball_physics_dispatch.cpp.
//
// Do not change behaviour here.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "model/match_engine.h"
#include "model/match_engine_internal.h"

using namespace MatchEngineInternal;

namespace
{
/** Air drag coefficient with the drag crisis between two speeds. */
float dragCoefficient(float speed)
{
  using B = MatchTuning::Ball;
  const float t =
      std::clamp((speed - B::DRAG_CRISIS_LOW_SPEED) /
                     (B::DRAG_CRISIS_HIGH_SPEED - B::DRAG_CRISIS_LOW_SPEED),
                 0.0F, 1.0F);
  const float smooth = t * t * (3.0F - 2.0F * t);
  return B::DRAG_SUBCRITICAL +
         ((B::DRAG_SUPERCRITICAL - B::DRAG_SUBCRITICAL) * smooth);
}

/** Quadratic drag per unit of drag coefficient (1/m): 0.5 rho A / m. */
constexpr float FLIGHT_DRAG_SCALE = 0.5F * MatchTuning::Ball::AIR_DENSITY *
                                    MatchTuning::Ball::CROSS_SECTION_M2 /
                                    MatchTuning::Ball::MASS_KG;
}  // namespace

float MatchEngine::stepFlightAardvark(FlightState& flight, float dt)
{
  using B = MatchTuning::Ball;
  constexpr float DRAG_SCALE = FLIGHT_DRAG_SCALE;
  flight.bounced = false;
  const bool airborne = flight.height > 0.0F || flight.verticalSpeed > 0.0F;
  if (airborne)
  {
    const float speed =
        std::sqrt((flight.horizontalSpeed * flight.horizontalSpeed) +
                  (flight.verticalSpeed * flight.verticalSpeed));
    // Implicit quadratic drag stays stable at any sub-step length.
    const float damping =
        1.0F / (1.0F + DRAG_SCALE * dragCoefficient(speed) * speed * dt);
    flight.horizontalSpeed *= damping;
    flight.verticalSpeed = flight.verticalSpeed * damping - B::GRAVITY * dt;
    flight.height += flight.verticalSpeed * dt;
    if (flight.height <= 0.0F)
    {
      flight.height = 0.0F;
      flight.bounced = true;
      flight.verticalSpeed = -flight.verticalSpeed * B::BOUNCE_RESTITUTION;
      if (flight.verticalSpeed < B::MIN_BOUNCE_SPEED)
      {
        flight.verticalSpeed = 0.0F;
      }
      flight.horizontalSpeed *= B::BOUNCE_TANGENTIAL_RETAINED;
    }
    return flight.horizontalSpeed * dt;
  }
  // Rolling: constant rolling resistance, a small viscous term and air drag.
  const float speed = flight.horizontalSpeed;
  const float deceleration =
      B::ROLLING_DECELERATION + (B::ROLLING_VISCOUS_PER_SECOND * speed) +
      (DRAG_SCALE * dragCoefficient(speed) * speed * speed);
  const float next = std::max(0.0F, speed - (deceleration * dt));
  flight.horizontalSpeed = next < B::STOP_SPEED ? 0.0F : next;
  return 0.5F * (speed + next) * dt;
}

void MatchEngine::integrateBallAardvark(float dt)
{
  const float speed = length(ball.velocity);
  Vector2F direction = speed > EPSILON ? Vector2F{.x = ball.velocity.x / speed,
                                                  .y = ball.velocity.y / speed}
                                       : Vector2F{.x = 0.0F, .y = 0.0F};
  if (std::abs(ball.spin.side) > EPSILON)
  {
    // Sidespin bends the flight (a Magnus-effect stand-in: a steady heading
    // turn) and slowly decays. Topspin plays no part.
    const float rotation =
        ball.spin.side * MatchTuning::Ball::TURN_PER_SIDESPIN * dt;
    const float cosine = std::cos(rotation);
    const float sine = std::sin(rotation);
    direction = {.x = (direction.x * cosine) - (direction.y * sine),
                 .y = (direction.x * sine) + (direction.y * cosine)};
    ball.spin.side *= 1.0F - MatchTuning::Ball::SPIN_DECAY_PER_SECOND * dt;
  }
  FlightState flight{.horizontalSpeed = speed,
                     .verticalSpeed = ball.velocityZ,
                     .height = ball.z,
                     .bounced = false};
  const float travelled = stepFlightAardvark(flight, dt);
  // A delivery that has bounced is no longer a clean aerial ball.
  if (flight.bounced)
  {
    endAerialDelivery();
  }
  const Vector2F step = {.x = direction.x * travelled,
                         .y = direction.y * travelled};
  ball.position.x += step.x;
  ball.position.y += step.y;
  ball.z = flight.height;
  ball.velocityZ = flight.verticalSpeed;
  ball.velocity = {.x = direction.x * flight.horizontalSpeed,
                   .y = direction.y * flight.horizontalSpeed};
  if (flight.horizontalSpeed <= 0.0F)
  {
    ball.spin = {};
  }
}

float MatchEngine::groundLaunchSpeedAardvark(float distanceMetres,
                                             float arrivalSpeed)
{
  // Rolling is one-dimensional and deterministic, so the distance a ball
  // rolls before stopping, D(v), is tabulated once with the live physics.
  // A ball launched at v0 arrives at va after D(v0) - D(va) metres.
  constexpr float SPEED_STEP = 0.25F;
  constexpr std::size_t ENTRIES = 161;
  static const std::array<float, ENTRIES> stoppingDistance = []
  {
    constexpr float DT = MatchTuning::Timing::FIXED_STEP_SECONDS /
                         static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
    std::array<float, ENTRIES> table{};
    for (std::size_t index = 1; index < ENTRIES; ++index)
    {
      FlightState flight{
          .horizontalSpeed = static_cast<float>(index) * SPEED_STEP,
          .verticalSpeed = 0.0F,
          .height = 0.0F,
          .bounced = false};
      float travelled = 0.0F;
      while (flight.horizontalSpeed > 0.0F)
      {
        travelled += stepFlightAardvark(flight, DT);
      }
      table[index] = travelled;
    }
    return table;
  }();
  const auto distanceFor = [&](float speed)
  {
    const float position =
        std::clamp(speed / SPEED_STEP, 0.0F, static_cast<float>(ENTRIES - 1));
    const auto low = static_cast<std::size_t>(position);
    const std::size_t high = std::min(low + 1, ENTRIES - 1);
    const float share = position - static_cast<float>(low);
    return stoppingDistance[low] +
           ((stoppingDistance[high] - stoppingDistance[low]) * share);
  };
  const float needed = distanceFor(arrivalSpeed) + distanceMetres;
  const auto upper = std::ranges::lower_bound(stoppingDistance, needed);
  if (upper == stoppingDistance.end())
  {
    return static_cast<float>(ENTRIES - 1) * SPEED_STEP;
  }
  const auto index =
      static_cast<std::size_t>(std::distance(stoppingDistance.begin(), upper));
  if (index == 0)
  {
    return 0.0F;
  }
  const float below = stoppingDistance[index - 1];
  const float share =
      (needed - below) / std::max(stoppingDistance[index] - below, EPSILON);
  return (static_cast<float>(index - 1) + share) * SPEED_STEP;
}

float MatchEngine::loftVerticalSpeedAardvark(float distanceMetres,
                                             float horizontalSpeed,
                                             float startHeight,
                                             float arrivalHeight)
{
  // Bisection on the vertical launch speed with the live flight physics: the
  // height reached over the target distance rises with the launch angle.
  constexpr float DT = MatchTuning::Timing::FIXED_STEP_SECONDS /
                       static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
  const auto heightAt = [&](float vertical)
  {
    FlightState flight{.horizontalSpeed = horizontalSpeed,
                       .verticalSpeed = vertical,
                       .height = startHeight,
                       .bounced = false};
    float travelled = 0.0F;
    for (float time = 0.0F; time < MatchTuning::Ball::MAX_FLIGHT_SECONDS &&
                            flight.horizontalSpeed > 0.0F;
         time += DT)
    {
      const float previousHeight = flight.height;
      const float step = stepFlightAardvark(flight, DT);
      // Landing short reads as a height deficit that shrinks to zero as the
      // landing point reaches the target, so the root is continuous.
      if (flight.bounced)
      {
        return -0.25F * std::max(0.0F, distanceMetres - travelled - step);
      }
      if (travelled + step >= distanceMetres)
      {
        const float share =
            (distanceMetres - travelled) / std::max(step, EPSILON);
        return previousHeight + ((flight.height - previousHeight) * share);
      }
      travelled += step;
    }
    return -1.0e6F;
  };
  // Probe from the drag-free estimate and its correction, step on (in
  // growing steps) until two probes bracket the arrival height, then close
  // in with the Illinois false-position method: a handful of flights, and
  // the lowest trajectory that gets there when two exist.
  constexpr float MAX_VERTICAL = 28.0F;
  constexpr float TOLERANCE_METRES = 0.03F;
  constexpr float MAX_STEP = 4.0F;
  constexpr int MAX_FLIGHTS = 14;
  // Flight time under quadratic air drag at the launch speed's drag
  // coefficient: x(t) = ln(1 + k v t) / k.
  const float launchSpeed = std::max(horizontalSpeed, 1.0F);
  const float drag = FLIGHT_DRAG_SCALE * dragCoefficient(launchSpeed);
  const float flightTime =
      std::expm1(drag * distanceMetres) / (drag * launchSpeed);
  float first = std::clamp(((arrivalHeight - startHeight) / flightTime) +
                               (0.5F * MatchTuning::Ball::GRAVITY * flightTime),
                           0.0F, MAX_VERTICAL);
  float firstMiss = heightAt(first) - arrivalHeight;
  if (std::abs(firstMiss) < TOLERANCE_METRES)
  {
    return first;
  }
  float second =
      std::clamp(first - (firstMiss / flightTime), 0.0F, MAX_VERTICAL);
  float secondMiss = heightAt(second) - arrivalHeight;
  int flights = 2;
  while ((firstMiss < 0.0F) == (secondMiss < 0.0F))
  {
    if (std::abs(secondMiss) < TOLERANCE_METRES)
    {
      return second;
    }
    const float step = std::clamp((second - first) * 1.5F, -MAX_STEP, MAX_STEP);
    const float next = std::clamp(second + step, 0.0F, MAX_VERTICAL);
    // Pinned at a bound (or not moving): nothing closer is reachable.
    if (next == second || flights >= MAX_FLIGHTS)
    {
      return second;
    }
    first = second;
    firstMiss = secondMiss;
    second = next;
    secondMiss = heightAt(second) - arrivalHeight;
    ++flights;
  }
  float below = firstMiss < 0.0F ? first : second;
  float belowMiss = firstMiss < 0.0F ? firstMiss : secondMiss;
  float above = firstMiss < 0.0F ? second : first;
  float aboveMiss = firstMiss < 0.0F ? secondMiss : firstMiss;
  if (std::abs(aboveMiss) < TOLERANCE_METRES)
  {
    return above;
  }
  int lastSide = 0;
  float estimate = above;
  for (; flights < MAX_FLIGHTS; ++flights)
  {
    estimate = below + (above - below) * belowMiss / (belowMiss - aboveMiss);
    const float miss = heightAt(estimate) - arrivalHeight;
    if (std::abs(miss) < TOLERANCE_METRES)
    {
      return estimate;
    }
    if (miss < 0.0F)
    {
      below = estimate;
      belowMiss = miss;
      if (lastSide < 0)
      {
        aboveMiss *= 0.5F;
      }
      lastSide = -1;
    }
    else
    {
      above = estimate;
      aboveMiss = miss;
      if (lastSide > 0)
      {
        belowMiss *= 0.5F;
      }
      lastSide = 1;
    }
  }
  return estimate;
}
