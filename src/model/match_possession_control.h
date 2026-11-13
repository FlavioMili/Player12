// Football Management Project — Copyright (c) 2025 - 2026 Flavio Milinanni.
// See LICENSE in the project root.
#pragma once

#include <algorithm>

#include "model/match_pitch.h"
#include "model/match_point.h"
#include "model/match_possession.h"

namespace PossessionControl
{
// A retaining contact changes velocity, not position. A ball whose centre is
// slightly over a line can still be legally in play. Preserve the legacy inset
// only for Aardvark; Binturong must not snap such a touch back onto the pitch.
constexpr PitchPoint retainingTouchPosition(const MatchPitch& pitch,
                                            PossessionId version,
                                            PitchPoint contact)
{
  if (version == PossessionId::BINTURONG) return contact;
  return PitchPoint{
      std::clamp(contact.x, pitch.atX(0.001f), pitch.atX(0.999f)),
      std::clamp(contact.y, pitch.atY(0.001f), pitch.atY(0.999f))};
}
// Initial Binturong policy values, in metres and simulated seconds. These
// describe current control, not claim expiry or permission to contest a ball.
inline constexpr float RETAIN_GAP_METRES = 3.0f;
inline constexpr float CLEAR_LOSS_GAP_METRES = 4.5f;
inline constexpr float PREDICT_SECONDS = 0.35f;
inline constexpr float HEIGHT_TOLERANCE_METRES = 0.15f;
inline constexpr float CLEAR_LOSS_HEIGHT_METRES = 0.75f;
inline constexpr float SEPARATION_GRACE_SECONDS = 0.20f;

struct Evidence
{
  bool playerAvailable = false;
  float gapMetres = 0.0f;
  float predictedGapMetres = 0.0f;
  float relativeSpeed = 0.0f;
  float heightMetres = 0.0f;
  float feetHeightMetres = 0.0f;
  float collectionReachMetres = 0.0f;
  float collectionRelativeSpeed = 0.0f;
};

struct Decision
{
  bool collected = false;
  float separationSeconds = 0.0f;
  bool newlyCollected = false;
};

// A wider retention envelope cannot establish control: collection must first
// meet foot reach, height and relative-speed evidence. Predicting relative
// separation accounts for both actors' speed/direction without moving the ball.
constexpr Decision evaluate(bool collected, float separationSeconds,
                            const Evidence& evidence, float dt)
{
  const bool collectable =
      evidence.playerAvailable &&
      evidence.gapMetres <= evidence.collectionReachMetres &&
      evidence.relativeSpeed <= evidence.collectionRelativeSpeed &&
      evidence.heightMetres <= evidence.feetHeightMetres;
  if (!collected) return {collectable, 0.0f, collectable};

  const bool clearlyLost =
      !evidence.playerAvailable || evidence.gapMetres > CLEAR_LOSS_GAP_METRES ||
      evidence.predictedGapMetres > CLEAR_LOSS_GAP_METRES ||
      evidence.heightMetres >
          evidence.feetHeightMetres + CLEAR_LOSS_HEIGHT_METRES;
  if (clearlyLost) return {};

  const bool retained = evidence.gapMetres <= RETAIN_GAP_METRES &&
                        evidence.predictedGapMetres <= RETAIN_GAP_METRES &&
                        evidence.heightMetres <=
                            evidence.feetHeightMetres + HEIGHT_TOLERANCE_METRES;
  if (retained) return {true, 0.0f, false};

  const float separated =
      std::max(0.0f, separationSeconds) + std::max(0.0f, dt);
  return separated + 1e-6f < SEPARATION_GRACE_SECONDS
             ? Decision{true, separated, false}
             : Decision{};
}
}  // namespace PossessionControl
