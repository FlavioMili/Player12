// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Binturong ball physics's MatchEngine members (defined in
// match_ball_physics_binturong.cpp). Included inside the MatchEngine class
// body, so no include guard and no includes of its own.

// clang-format off
  // --- Binturong ball physics ------------------------------------------------
  /** One flight step (see stepFlight). */
  static float stepFlightBinturong(FlightState& flight, float dt);
  void integrateBallBinturong(float dt);
  static float groundLaunchSpeedBinturong(float distanceMetres, float arrivalSpeed);
  static float loftVerticalSpeedBinturong(float distanceMetres, float horizontalSpeed,
                                   float startHeight, float arrivalHeight,
                                   BallSpin spin = {});
  static float curlAngleBinturong(float distanceMetres, float along, float up,
                                  float startHeight, BallSpin spin);
// clang-format on
