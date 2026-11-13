// Football Management Project — Copyright (c) 2025 - 2026 Flavio Milinanni.
// See LICENSE in the project root.
#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

#include "global/types.h"
#include "model/match_pitch.h"

namespace RestartApproach
{
// Metric walking target. A taker outside the bottom boards must return through
// a real gap, not walk through a solid advertising panel. Once inside, aim past
// the ball by the body's arrival dead zone so the approach reaches kicking gap.
inline Vector2F target(const MatchPitch& pitch, Vector2F from, Vector2F ball,
                       float arrivalDeadZone)
{
  const MatchGround::Layout& G = pitch.ground;
  if (from.y >= G.boardsY && ball.y < G.boardsY)
  {
    float chosen = G.halfway;
    float best = std::numeric_limits<float>::max();
    for (std::size_t i = 1; i < G.bottomBoards.size(); ++i)
    {
      const float centre =
          0.5f * (G.bottomBoards[i - 1].to + G.bottomBoards[i].from);
      const float gap = std::abs(centre - from.x);
      if (gap < best)
      {
        best = gap;
        chosen = centre;
      }
    }
    return {chosen, G.boardsY - std::max(0.0f, arrivalDeadZone) - 1.0f};
  }
  const float dx = ball.x - from.x, dy = ball.y - from.y;
  const float distance = std::hypot(dx, dy);
  if (distance <= 1e-5f) return ball;
  return {ball.x + dx * arrivalDeadZone / distance,
          ball.y + dy * arrivalDeadZone / distance};
}
}  // namespace RestartApproach
