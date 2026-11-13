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
 * A point on an engine's pitch, in metres: x along its length (0 at the
 * left goal line; the home side attacks +x), y across its width (0 at the
 * top touchline).
 *
 * Its own type so that a point is never taken for a metric vector (a
 * velocity, a direction, a position in metres: all Vector2F) or the other
 * way round. Nothing converts silently: a point is made by writing
 * `PitchPoint{x, y}` (or `asPoint`), and read as a vector with `asVector`.
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

/** The step from `from` to `to`, in metres. */
constexpr Vector2F operator-(PitchPoint to, PitchPoint from)
{
  return Vector2F{to.x - from.x, to.y - from.y};
}

/** `from` moved by `step` (metres). */
constexpr PitchPoint operator+(PitchPoint from, Vector2F step)
{
  return PitchPoint{from.x + step.x, from.y + step.y};
}

/** The point as a metric vector from the top left corner, for geometry
 * worked in Vector2F (the same numbers: both are metres). */
constexpr Vector2F asVector(PitchPoint point) { return {point.x, point.y}; }

/** A metric vector from the top left corner as a point. */
constexpr PitchPoint asPoint(Vector2F metres)
{
  return PitchPoint{metres.x, metres.y};
}
