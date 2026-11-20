// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Aardvark ball physics's MatchEngine members (defined in
// match_ball_physics_aardvark.cpp). Included inside the MatchEngine class body,
// so no include guard and no includes of its own.

// clang-format off
  // --- Aardvark ball physics -------------------------------------------------
  /** One flight step (see stepFlight). */
  static float stepFlightAardvark(FlightState& flight, float dt);
  void integrateBallAardvark(float dt);
  static float groundLaunchSpeedAardvark(float distanceMetres, float arrivalSpeed);
  static float loftVerticalSpeedAardvark(float distanceMetres, float horizontalSpeed,
                                   float startHeight, float arrivalHeight);
// clang-format on
