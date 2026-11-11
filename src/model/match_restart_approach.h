// Football Management Project — Copyright (c) 2025 - 2026 Flavio Milinanni.
// See LICENSE in the project root.
#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

#include "global/types.h"
#include "model/match_ground.h"

namespace RestartApproach
{
// Metric walking target. A taker outside the bottom boards must return through
// a real gap, not walk through a solid advertising panel. Once inside, aim past
// the ball by the body's arrival dead zone so the approach reaches kicking gap.
inline Vector2F target(Vector2F from, Vector2F ball, float arrivalDeadZone)
{
  namespace G = MatchGround;
  if (from.y >= G::BOARDS_Y && ball.y < G::BOARDS_Y)
  {
    float chosen = G::HALFWAY;
    float best = std::numeric_limits<float>::max();
    for (std::size_t i = 1; i < G::BOTTOM_BOARDS.size(); ++i)
    {
      const float centre =
          0.5f * (G::BOTTOM_BOARDS[i - 1].to + G::BOTTOM_BOARDS[i].from);
      const float gap = std::abs(centre - from.x);
      if (gap < best)
      {
        best = gap;
        chosen = centre;
      }
    }
    return {chosen, G::BOARDS_Y - std::max(0.0f, arrivalDeadZone) - 1.0f};
  }
  const float dx = ball.x - from.x, dy = ball.y - from.y;
  const float distance = std::hypot(dx, dy);
  if (distance <= 1e-5f) return ball;
  return {ball.x + dx * arrivalDeadZone / distance,
          ball.y + dy * arrivalDeadZone / distance};
}
}  // namespace RestartApproach
