// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Binturong ball physics: the ball on its own - flight under gravity and
// drag (with the drag crisis), bounces, rolling, the swerve of spin, and the
// launch solvers built on them - the version being developed (see
// match_ball_physics.h). It started as an exact copy of the Aardvark ball
// physics (match_ball_physics_aardvark.cpp). Every function here is a
// MatchEngine member with the "Binturong" suffix, declared in
// match_ball_physics_binturong_members.h; the engine reaches them through
// the dispatchers in match_ball_physics_dispatch.cpp.
//
// Change behaviour freely here. The ball's constants (MatchTuning::Ball) are
// shared by every ball physics: copy one into this file before changing it.
// Contacts with players are not ball physics. To keep this version for
// comparison, freeze it and copy it to a new version the same way.

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
                 0.0f, 1.0f);
  const float smooth = t * t * (3.0f - 2.0f * t);
  return B::DRAG_SUBCRITICAL +
         (B::DRAG_SUPERCRITICAL - B::DRAG_SUBCRITICAL) * smooth;
}

/** Quadratic drag per unit of drag coefficient (1/m): 0.5 rho A / m. */
constexpr float FLIGHT_DRAG_SCALE = 0.5f * MatchTuning::Ball::AIR_DENSITY *
                                    MatchTuning::Ball::CROSS_SECTION_M2 /
                                    MatchTuning::Ball::MASS_KG;

/** The Magnus force's acceleration (m/s^2) on a ball moving `along` its
 * heading and `up` (m/s) with `spin`: along the heading, across it (the
 * way a positive sidespin turns it) and up. */
struct MagnusAcceleration
{
  float along = 0.0f;
  float across = 0.0f;
  float up = 0.0f;
};
MagnusAcceleration magnusAcceleration(float along, float up, BallSpin spin)
{
  using B = MatchTuning::Ball;
  // Spin x velocity, in the heading's axes: square to both.
  const float spinAlong = spin.top * up;
  const float spinAcross = spin.side * along;
  const float spinUp = -spin.top * along;
  const float size = std::sqrt(spinAlong * spinAlong +
                               spinAcross * spinAcross + spinUp * spinUp);
  if (size <= 0.0f) return {};
  // 0.5 rho A C_L v^2 / m, C_L the spin ratio's share, capped.
  const float force =
      FLIGHT_DRAG_SCALE *
      std::min(B::LIFT_PER_SPIN_RATIO * B::RADIUS_METRES * size,
               B::MAX_LIFT_COEFFICIENT * (along * along + up * up));
  const float scale = force / size;
  return {spinAlong * scale, spinAcross * scale, spinUp * scale};
}
}  // namespace

float MatchEngine::stepFlightBinturong(FlightState& flight, float dt)
{
  using B = MatchTuning::Ball;
  constexpr float DRAG_SCALE = FLIGHT_DRAG_SCALE;
  flight.bounced = false;
  flight.turnRate = 0.0f;
  BallSpin& spin = flight.spin;
  const bool airborne = flight.height > 0.0f || flight.verticalSpeed > 0.0f;
  if (airborne)
  {
    const float speed =
        std::sqrt(flight.horizontalSpeed * flight.horizontalSpeed +
                  flight.verticalSpeed * flight.verticalSpeed);
    // Spin's Magnus force: sidespin bends its heading, backspin holds it
    // up, topspin brings it down.
    const MagnusAcceleration magnus =
        magnusAcceleration(flight.horizontalSpeed, flight.verticalSpeed, spin);
    if (flight.horizontalSpeed > 0.0f)
      flight.turnRate = magnus.across / flight.horizontalSpeed;
    // Implicit quadratic drag stays stable at any sub-step length.
    const float damping =
        1.0f / (1.0f + DRAG_SCALE * dragCoefficient(speed) * speed * dt);
    flight.horizontalSpeed =
        std::max(flight.horizontalSpeed * damping + magnus.along * dt, 0.0f);
    flight.verticalSpeed = flight.verticalSpeed * damping +
                           (magnus.up - B::GRAVITY) * dt;
    flight.height += flight.verticalSpeed * dt;
    // Only the air works on the spin.
    const float kept = std::max(1.0f - B::SPIN_DECAY_PER_SECOND * dt, 0.0f);
    spin.side *= kept;
    spin.top *= kept;
    if (flight.height <= 0.0f)
    {
      flight.height = 0.0f;
      flight.bounced = true;
      flight.verticalSpeed = -flight.verticalSpeed * B::BOUNCE_RESTITUTION;
      if (flight.verticalSpeed < B::MIN_BOUNCE_SPEED)
        flight.verticalSpeed = 0.0f;
      // The grass grips it: its slip (its speed against its spin's at the
      // contact) is taken part of the way toward rolling, trading speed for
      // spin. Backspin checks it up, topspin sends it on.
      const float slip = flight.horizontalSpeed - spin.top * B::RADIUS_METRES;
      const float change =
          -B::BOUNCE_GRIP * slip / (1.0f + B::BALL_INERTIA_SHARE);
      flight.horizontalSpeed = std::max(flight.horizontalSpeed + change, 0.0f);
      spin.top -= change / (B::BALL_INERTIA_SHARE * B::RADIUS_METRES);
      spin.side *= B::SIDESPIN_RETAINED_ON_BOUNCE;
    }
    return flight.horizontalSpeed * dt;
  }
  // Rolling: constant rolling resistance, a small viscous term and air drag.
  // The grass holds it down: only its sidespin bends it, and bleeds off
  // fast; its topspin is its roll.
  const float speed = flight.horizontalSpeed;
  const MagnusAcceleration magnus =
      magnusAcceleration(speed, 0.0f, {spin.side, 0.0f});
  if (speed > 0.0f) flight.turnRate = magnus.across / speed;
  const float deceleration =
      B::ROLLING_DECELERATION + B::ROLLING_VISCOUS_PER_SECOND * speed +
      DRAG_SCALE * dragCoefficient(speed) * speed * speed;
  const float next = std::max(0.0f, speed - deceleration * dt);
  flight.horizontalSpeed = next < B::STOP_SPEED ? 0.0f : next;
  spin.side *=
      std::max(1.0f - B::SIDESPIN_DECAY_ROLLING_PER_SECOND * dt, 0.0f);
  spin.top = flight.horizontalSpeed / B::RADIUS_METRES;
  return 0.5f * (speed + next) * dt;
}

void MatchEngine::integrateBallBinturong(float dt)
{
  const float speed = length(ball.velocity);
  Vector2F direction = speed > EPSILON ? Vector2F{ball.velocity.x / speed,
                                                  ball.velocity.y / speed}
                                       : Vector2F{0.0f, 0.0f};
  FlightState flight{speed, ball.velocityZ,
                     ball.z * MatchTuning::Units::BALL_Z_METRES, false,
                     ball.spin};
  const float travelled = stepFlightBinturong(flight, dt);
  ball.spin = flight.spin;
  // Its sidespin has turned its heading over the step.
  if (std::abs(flight.turnRate) > EPSILON)
  {
    const float rotation = flight.turnRate * dt;
    const float cosine = std::cos(rotation);
    const float sine = std::sin(rotation);
    direction = {direction.x * cosine - direction.y * sine,
                 direction.x * sine + direction.y * cosine};
  }
  // A delivery that has bounced is no longer a clean aerial ball.
  if (flight.bounced) endAerialDelivery();
  const Vector2F step =
      toPitch({direction.x * travelled, direction.y * travelled});
  ball.position.x += step.x;
  ball.position.y += step.y;
  ball.z = flight.height / MatchTuning::Units::BALL_Z_METRES;
  ball.velocityZ = flight.verticalSpeed;
  ball.velocity = {direction.x * flight.horizontalSpeed,
                   direction.y * flight.horizontalSpeed};
  if (flight.horizontalSpeed <= 0.0f) ball.spin = {};
}

float MatchEngine::groundLaunchSpeedBinturong(float distanceMetres,
                                              float arrivalSpeed)
{
  // Rolling is one-dimensional and deterministic, so the distance a ball
  // rolls before stopping, D(v), is tabulated once with the live physics.
  // A ball launched at v0 arrives at va after D(v0) - D(va) metres.
  constexpr float SPEED_STEP = 0.25f;
  constexpr std::size_t ENTRIES = 161;
  static const std::array<float, ENTRIES> stoppingDistance = []
  {
    constexpr float DT = MatchTuning::Timing::FIXED_STEP_SECONDS /
                         static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
    std::array<float, ENTRIES> table{};
    for (std::size_t index = 1; index < ENTRIES; ++index)
    {
      FlightState flight{static_cast<float>(index) * SPEED_STEP, 0.0f, 0.0f,
                         false};
      float travelled = 0.0f;
      while (flight.horizontalSpeed > 0.0f)
        travelled += stepFlightBinturong(flight, DT);
      table[index] = travelled;
    }
    return table;
  }();
  const auto distanceFor = [&](float speed)
  {
    const float position =
        std::clamp(speed / SPEED_STEP, 0.0f, static_cast<float>(ENTRIES - 1));
    const auto low = static_cast<std::size_t>(position);
    const std::size_t high = std::min(low + 1, ENTRIES - 1);
    const float share = position - static_cast<float>(low);
    return stoppingDistance[low] +
           (stoppingDistance[high] - stoppingDistance[low]) * share;
  };
  const float needed = distanceFor(arrivalSpeed) + distanceMetres;
  const auto upper = std::ranges::lower_bound(stoppingDistance, needed);
  if (upper == stoppingDistance.end())
    return static_cast<float>(ENTRIES - 1) * SPEED_STEP;
  const auto index =
      static_cast<std::size_t>(std::distance(stoppingDistance.begin(), upper));
  if (index == 0) return 0.0f;
  const float below = stoppingDistance[index - 1];
  const float share =
      (needed - below) / std::max(stoppingDistance[index] - below, EPSILON);
  return (static_cast<float>(index - 1) + share) * SPEED_STEP;
}

namespace
{
/** A launched ball's path seen from above, from where it was struck along
 * the way it was launched: how far out it has got (its distance from
 * there) and how far round its sidespin has carried it. */
struct CurlPath
{
  float x = 0.0f;
  float y = 0.0f;
  float heading = 0.0f;
  [[nodiscard]] float out() const { return std::hypot(x, y); }
  /** One flight step of `moved` metres, turned as it went. */
  void advance(const FlightState& flight, float moved, float dt)
  {
    if (flight.spin.side == 0.0f && heading == 0.0f)
    {
      x += moved;
      return;
    }
    heading += flight.turnRate * dt;
    x += std::cos(heading) * moved;
    y += std::sin(heading) * moved;
  }
};
}  // namespace

float MatchEngine::curlAngleBinturong(float distanceMetres, float along,
                                      float up, float startHeight,
                                      BallSpin spin)
{
  // Fly it (or roll it) with the live physics to where it is that far out,
  // or wherever it stops.
  if (spin.side == 0.0f) return 0.0f;
  constexpr float DT = MatchTuning::Timing::FIXED_STEP_SECONDS /
                       static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
  FlightState flight{along, up, startHeight, false, spin};
  CurlPath path;
  for (float time = 0.0f;
       time < 2.0f * MatchTuning::Ball::MAX_FLIGHT_SECONDS &&
       flight.horizontalSpeed > 0.0f && path.out() < distanceMetres;
       time += DT)
    path.advance(flight, stepFlightBinturong(flight, DT), DT);
  return std::atan2(path.y, path.x);
}

float MatchEngine::loftVerticalSpeedBinturong(float distanceMetres,
                                              float horizontalSpeed,
                                              float startHeight,
                                              float arrivalHeight,
                                              BallSpin spin)
{
  // Bisection on the vertical launch speed with the live flight physics: the
  // height reached over the target distance rises with the launch angle.
  // With spin: backspin holds it up, topspin brings it down, and sidespin
  // curls it, so it is that far out (not along) where it gets there.
  constexpr float DT = MatchTuning::Timing::FIXED_STEP_SECONDS /
                       static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
  const auto heightAt = [&](float vertical)
  {
    FlightState flight{horizontalSpeed, vertical, startHeight, false, spin};
    CurlPath path;
    for (float time = 0.0f; time < MatchTuning::Ball::MAX_FLIGHT_SECONDS &&
                            flight.horizontalSpeed > 0.0f;
         time += DT)
    {
      const float previousHeight = flight.height;
      const float before = path.out();
      path.advance(flight, stepFlightBinturong(flight, DT), DT);
      const float now = path.out();
      // Landing short reads as a height deficit that shrinks to zero as the
      // landing point reaches the target, so the root is continuous.
      if (flight.bounced)
        return -0.25f * std::max(0.0f, distanceMetres - now);
      if (now >= distanceMetres)
      {
        const float share =
            (distanceMetres - before) / std::max(now - before, EPSILON);
        return previousHeight + (flight.height - previousHeight) * share;
      }
    }
    return -1.0e6f;
  };
  // Probe from the drag-free estimate and its correction, step on (in
  // growing steps) until two probes bracket the arrival height, then close
  // in with the Illinois false-position method: a handful of flights, and
  // the lowest trajectory that gets there when two exist.
  constexpr float MAX_VERTICAL = 28.0f;
  constexpr float TOLERANCE_METRES = 0.03f;
  constexpr float MAX_STEP = 4.0f;
  constexpr int MAX_FLIGHTS = 14;
  // Flight time under quadratic air drag at the launch speed's drag
  // coefficient: x(t) = ln(1 + k v t) / k.
  const float launchSpeed = std::max(horizontalSpeed, 1.0f);
  const float drag = FLIGHT_DRAG_SCALE * dragCoefficient(launchSpeed);
  const float flightTime =
      std::expm1(drag * distanceMetres) / (drag * launchSpeed);
  float first = std::clamp((arrivalHeight - startHeight) / flightTime +
                               0.5f * MatchTuning::Ball::GRAVITY * flightTime,
                           0.0f, MAX_VERTICAL);
  float firstMiss = heightAt(first) - arrivalHeight;
  if (std::abs(firstMiss) < TOLERANCE_METRES) return first;
  float second = std::clamp(first - firstMiss / flightTime, 0.0f, MAX_VERTICAL);
  float secondMiss = heightAt(second) - arrivalHeight;
  int flights = 2;
  while ((firstMiss < 0.0f) == (secondMiss < 0.0f))
  {
    if (std::abs(secondMiss) < TOLERANCE_METRES) return second;
    const float step = std::clamp((second - first) * 1.5f, -MAX_STEP, MAX_STEP);
    const float next = std::clamp(second + step, 0.0f, MAX_VERTICAL);
    // Pinned at a bound (or not moving): nothing closer is reachable.
    if (next == second || flights >= MAX_FLIGHTS) return second;
    first = second;
    firstMiss = secondMiss;
    second = next;
    secondMiss = heightAt(second) - arrivalHeight;
    ++flights;
  }
  float below = firstMiss < 0.0f ? first : second;
  float belowMiss = firstMiss < 0.0f ? firstMiss : secondMiss;
  float above = firstMiss < 0.0f ? second : first;
  float aboveMiss = firstMiss < 0.0f ? secondMiss : firstMiss;
  if (std::abs(aboveMiss) < TOLERANCE_METRES) return above;
  int lastSide = 0;
  float estimate = above;
  for (; flights < MAX_FLIGHTS; ++flights)
  {
    estimate = below + (above - below) * belowMiss / (belowMiss - aboveMiss);
    const float miss = heightAt(estimate) - arrivalHeight;
    if (std::abs(miss) < TOLERANCE_METRES) return estimate;
    if (miss < 0.0f)
    {
      below = estimate;
      belowMiss = miss;
      if (lastSide < 0) aboveMiss *= 0.5f;
      lastSide = -1;
    }
    else
    {
      above = estimate;
      aboveMiss = miss;
      if (lastSide > 0) belowMiss *= 0.5f;
      lastSide = 1;
    }
  }
  return estimate;
}
