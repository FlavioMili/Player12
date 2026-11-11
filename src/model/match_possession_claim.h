// Football Management Project — Copyright (c) 2025 - 2026 Flavio Milinanni.
// See LICENSE in the project root.
#pragma once

#include <algorithm>
#include <cmath>

namespace PossessionClaim
{
inline constexpr float RECOVERY_HORIZON_SECONDS = 2.5f;
inline constexpr float SAMPLE_SECONDS = 0.1f;
inline constexpr float UNRECOVERABLE_GRACE_SECONDS = 0.3f;
inline constexpr float MAX_UNCONTROLLED_SECONDS = 6.0f;

enum class Expiry
{
  NONE,
  UNAVAILABLE,
  UNRECOVERABLE,
  AGE_LIMIT
};
struct Decision
{
  float uncontrolledSeconds = 0.0f;
  float unrecoverableSeconds = 0.0f;
  Expiry expiry = Expiry::NONE;
};

// A conservative radial arrival estimate. Moving away costs time before the
// player can make progress toward this point; acceleration is capped at top
// speed.
inline float travelBudget(float towardSpeed, float topSpeed, float acceleration,
                          float seconds)
{
  const float top = std::max(0.0f, topSpeed);
  const float initial = std::clamp(towardSpeed, -top, top);
  const float time = std::max(0.0f, seconds);
  const float a = std::max(0.0f, acceleration);
  if (a <= 1e-6f) return std::max(0.0f, initial * time);
  const float accelerating = std::min(time, (top - initial) / a);
  return std::max(0.0f, initial * accelerating +
                            0.5f * a * accelerating * accelerating +
                            top * (time - accelerating));
}

inline bool canRecoverAt(float distanceMetres, float towardSpeed,
                         float topSpeed, float acceleration, float seconds,
                         float contactReadyIn, float reachMetres,
                         float heightMetres, float feetHeight, bool onPitch)
{
  return onPitch && seconds + 1e-6f >= std::max(0.0f, contactReadyIn) &&
         heightMetres <= feetHeight &&
         distanceMetres <=
             std::max(0.0f, reachMetres) +
                 travelBudget(towardSpeed, topSpeed, acceleration, seconds);
}

constexpr Decision evaluate(bool available, bool controlled, bool recoverable,
                            float age, float unrecoverable, float dt)
{
  if (!available) return {0.0f, 0.0f, Expiry::UNAVAILABLE};
  if (controlled) return {};
  const float elapsed = std::max(0.0f, dt);
  const float nextAge = std::max(0.0f, age) + elapsed;
  const float nextUnrecoverable =
      recoverable ? 0.0f : std::max(0.0f, unrecoverable) + elapsed;
  const auto reason = nextAge + 1e-6f >= MAX_UNCONTROLLED_SECONDS
                          ? Expiry::AGE_LIMIT
                      : nextUnrecoverable + 1e-6f >= UNRECOVERABLE_GRACE_SECONDS
                          ? Expiry::UNRECOVERABLE
                          : Expiry::NONE;
  return {nextAge, nextUnrecoverable, reason};
}
}  // namespace PossessionClaim
