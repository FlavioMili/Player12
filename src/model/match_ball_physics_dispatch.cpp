// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The ball moves with the match's ball physics (MatchEngine::setBallPhysics).
// To add one: a BallPhysicsId (match_ball_physics.h), its members
// (match_ball_physics_<name>_members.h, included in MatchEngine), its
// definitions (match_ball_physics_<name>.cpp), and a case in each function
// here.

#include "model/match_engine.h"

float MatchEngine::stepFlight(FlightState& flight, float dt) const
{
  switch (ballPhysicsId)
  {
    case BallPhysicsId::AARDVARK:
      break;
    case BallPhysicsId::BINTURONG:
      return stepFlightBinturong(flight, dt);
  }
  return stepFlightAardvark(flight, dt);
}

void MatchEngine::integrateBall(float dt)
{
  switch (ballPhysicsId)
  {
    case BallPhysicsId::AARDVARK:
      break;
    case BallPhysicsId::BINTURONG:
      integrateBallBinturong(dt);
      return;
  }
  integrateBallAardvark(dt);
}

float MatchEngine::groundLaunchSpeed(float distanceMetres,
                                     float arrivalSpeed) const
{
  switch (ballPhysicsId)
  {
    case BallPhysicsId::AARDVARK:
      break;
    case BallPhysicsId::BINTURONG:
      return groundLaunchSpeedBinturong(distanceMetres, arrivalSpeed);
  }
  return groundLaunchSpeedAardvark(distanceMetres, arrivalSpeed);
}

float MatchEngine::curlAngle(float distanceMetres, float along, float up,
                             float startHeight, BallSpin spin) const
{
  switch (ballPhysicsId)
  {
    case BallPhysicsId::AARDVARK:
      break;
    case BallPhysicsId::BINTURONG:
      return curlAngleBinturong(distanceMetres, along, up, startHeight, spin);
  }
  return 0.0F;
}

float MatchEngine::loftVerticalSpeed(float distanceMetres,
                                     float horizontalSpeed, float startHeight,
                                     float arrivalHeight, BallSpin spin) const
{
  switch (ballPhysicsId)
  {
    case BallPhysicsId::AARDVARK:
      break;
    case BallPhysicsId::BINTURONG:
      return loftVerticalSpeedBinturong(distanceMetres, horizontalSpeed,
                                        startHeight, arrivalHeight, spin);
  }
  return loftVerticalSpeedAardvark(distanceMetres, horizontalSpeed, startHeight,
                                   arrivalHeight);
}
