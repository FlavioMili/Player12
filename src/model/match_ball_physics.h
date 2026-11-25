// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

/**
 * The match's "ball physics": how the ball moves on its own once struck -
 * flight under gravity and air drag, bounces, rolling, the swerve of spin -
 * and the launch solvers that turn "reach there at this speed or height"
 * into a kick, worked out with the same physics so predicted and real
 * flights match. Contacts with players (kicks, touches, saves, rebounds) and
 * what the ball's position means (goals, out of play) are shared by every
 * ball. Brains and bodies predict flights through MatchEngine::stepFlight,
 * so they always use the match's own physics.
 *
 * AARDVARK is the inherited physics, kept frozen for comparison; the
 * BINTURONG and later ones are the ones being developed. One per match,
 * chosen before kickoff (MatchEngine::setBallPhysics).
 */
enum class BallPhysicsId : std::uint8_t
{
  AARDVARK,
  BINTURONG,
};

inline constexpr std::array<BallPhysicsId, 2> ALL_BALL_PHYSICS = {
    BallPhysicsId::AARDVARK, BallPhysicsId::BINTURONG};

/** Display name ("Aardvark", "Binturong"). */
constexpr std::string_view ballPhysicsName(BallPhysicsId physics)
{
  switch (physics)
  {
    case BallPhysicsId::AARDVARK:
      return "Aardvark";
    case BallPhysicsId::BINTURONG:
      return "Binturong";
  }
  return "Unknown";
}

/** Command-line and file key ("aardvark", "binturong"). */
constexpr std::string_view ballPhysicsKey(BallPhysicsId physics)
{
  switch (physics)
  {
    case BallPhysicsId::AARDVARK:
      return "aardvark";
    case BallPhysicsId::BINTURONG:
      return "binturong";
  }
  return "unknown";
}

/** The ball physics with this key, if any. */
constexpr std::optional<BallPhysicsId> ballPhysicsFromKey(std::string_view key)
{
  for (const BallPhysicsId physics : ALL_BALL_PHYSICS)
  {
    if (ballPhysicsKey(physics) == key)
    {
      return physics;
    }
  }
  return std::nullopt;
}

/**
 * Ball state along its horizontal direction of travel, advanced by
 * MatchEngine::stepFlight. One physics function serves the live integrator,
 * the launch solvers and every prediction, so predicted and real flights
 * match exactly.
 */
/**
 * The ball's spin (rad/s), about axes that turn with its heading:
 * sidespin about the vertical (+ bends its heading the way a positive
 * PassOption::curve does) and topspin about the horizontal across its path
 * (+ topspin: it dips, and runs on at a bounce; - backspin: it lifts, and
 * checks up).
 */
struct BallSpin
{
  float side = 0.0F;
  float top = 0.0F;
  friend bool operator==(const BallSpin&, const BallSpin&) = default;
};

struct FlightState
{
  float horizontalSpeed = 0.0F;
  float verticalSpeed = 0.0F;
  float height = 0.0F;
  bool bounced = false;
  /** Its spin (Binturong physics; the Aardvark flight ignores it). */
  BallSpin spin{};
  /** Out: how fast the sidespin turned its heading over the step (rad/s). */
  float turnRate = 0.0F;
};
