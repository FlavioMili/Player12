// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include "global/types.h"

/**
 * A point on an engine's pitch, in pitch units: x is a fraction of the
 * pitch's length (0 at the left goal line, 1 at the right; the home side
 * attacks +x), y a fraction of its width (0 at the top touchline).
 *
 * Its own type so that a point is never taken for a metric vector (a
 * velocity, a direction, a position in metres: all Vector2F) or the other
 * way round. Nothing converts silently: a point is made by writing
 * `PitchPoint{x, y}`, and turned into metres with the engine's `toMetres`.
 */
struct PitchPoint final
{
  constexpr PitchPoint() = default;
  constexpr explicit PitchPoint(float pitchX, float pitchY) : x(pitchX), y(pitchY)
  {
  }

  float x = 0.0f;
  float y = 0.0f;
};

/**
 * The step from one PitchPoint to another, in pitch units (fractions of the
 * length and the width): not a distance, since a unit of x and a unit of y
 * are different lengths on the ground. `toMetres` makes it a metric vector.
 */
struct PitchOffset final
{
  constexpr PitchOffset() = default;
  constexpr explicit PitchOffset(float pitchX, float pitchY) : x(pitchX), y(pitchY)
  {
  }

  float x = 0.0f;
  float y = 0.0f;
};

/** The step from `from` to `to`. */
constexpr PitchOffset operator-(PitchPoint to, PitchPoint from)
{
  return PitchOffset{to.x - from.x, to.y - from.y};
}

/** `from` moved by `step`. */
constexpr PitchPoint operator+(PitchPoint from, PitchOffset step)
{
  return PitchPoint{from.x + step.x, from.y + step.y};
}
