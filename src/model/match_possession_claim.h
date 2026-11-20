// Football Management Project — Copyright (c) 2025 - 2026 Flavio Milinanni.
// See LICENSE in the project root.
#pragma once

#include <algorithm>
#include <cmath>

namespace PossessionClaim
{
inline constexpr float RECOVERY_HORIZON_SECONDS = 2.5F;
inline constexpr float SAMPLE_SECONDS = 0.1F;
inline constexpr float UNRECOVERABLE_GRACE_SECONDS = 0.3F;
inline constexpr float MAX_UNCONTROLLED_SECONDS = 6.0F;

enum class Expiry
{
  NONE,
  UNAVAILABLE,
  UNRECOVERABLE,
  AGE_LIMIT
};
struct Decision
{
  float uncontrolledSeconds = 0.0F;
  float unrecoverableSeconds = 0.0F;
  Expiry expiry = Expiry::NONE;
};

// A conservative radial arrival estimate. Moving away costs time before the
// player can make progress toward this point; acceleration is capped at top
// speed.
inline float travelBudget(float towardSpeed, float topSpeed, float acceleration,
                          float seconds)
{
  const float top = std::max(0.0F, topSpeed);
  const float initial = std::clamp(towardSpeed, -top, top);
  const float time = std::max(0.0F, seconds);
  const float a = std::max(0.0F, acceleration);
  if (a <= 1e-6F)
  {
    return std::max(0.0F, initial * time);
  }
  const float accelerating = std::min(time, (top - initial) / a);
  return std::max(0.0F, (initial * accelerating) +
                            (0.5F * a * accelerating * accelerating) +
                            (top * (time - accelerating)));
}

inline bool canRecoverAt(float distanceMetres, float towardSpeed,
                         float topSpeed, float acceleration, float seconds,
                         float contactReadyIn, float reachMetres,
                         float heightMetres, float feetHeight, bool onPitch)
{
  return onPitch && seconds + 1e-6F >= std::max(0.0F, contactReadyIn) &&
         heightMetres <= feetHeight &&
         distanceMetres <=
             std::max(0.0F, reachMetres) +
                 travelBudget(towardSpeed, topSpeed, acceleration, seconds);
}

constexpr Decision evaluate(bool available, bool controlled, bool recoverable,
                            float age, float unrecoverable, float dt)
{
  if (!available)
  {
    return {.uncontrolledSeconds = 0.0F,
            .unrecoverableSeconds = 0.0F,
            .expiry = Expiry::UNAVAILABLE};
  }
  if (controlled)
  {
    return {};
  }
  const float elapsed = std::max(0.0F, dt);
  const float nextAge = std::max(0.0F, age) + elapsed;
  const float nextUnrecoverable =
      recoverable ? 0.0F : std::max(0.0F, unrecoverable) + elapsed;
  const auto reason = nextAge + 1e-6F >= MAX_UNCONTROLLED_SECONDS
                          ? Expiry::AGE_LIMIT
                      : nextUnrecoverable + 1e-6F >= UNRECOVERABLE_GRACE_SECONDS
                          ? Expiry::UNRECOVERABLE
                          : Expiry::NONE;
  return {.uncontrolledSeconds = nextAge,
          .unrecoverableSeconds = nextUnrecoverable,
          .expiry = reason};
}
}  // namespace PossessionClaim
