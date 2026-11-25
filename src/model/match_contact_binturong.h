// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

// The Binturong body's contact difficulty (contact plan §2) as plain
// functions of the situation, so tools can read it without a match: the
// engine's assessContactBinturong fills a ContactSituation from the match
// and calls assessContact. Defined in match_body_binturong.cpp.

#include <algorithm>
#include <cmath>
#include <numbers>

#include "model/match_engine.h"
#include "model/match_first_touch_knobs.h"
#include "model/match_strike_knobs.h"

/** Everything a contact's difficulty reads. Directions are metric unit
 * vectors, velocities metric m/s. */
struct ContactSituation
{
  /** The way his body faces. */
  Vector2F facing{.x = 1.0F, .y = 0.0F};
  /** Where he means to send it seen from above, how fast over the ground
   * (m/s), and how fast upward (m/s, + up). */
  Vector2F aim{.x = 1.0F, .y = 0.0F};
  float intendedSpeed = 0.0F;
  float intendedLift = 0.0F;
  /** The ball's velocity over the ground and upward (m/s, + up). */
  Vector2F ballVelocity{.x = 0.0F, .y = 0.0F};
  float ballVerticalSpeed = 0.0F;
  Vector2F playerVelocity{.x = 0.0F, .y = 0.0F};
  float playerMaxSpeed = 1.0F;
  /** From his centre to where he meets it, how far (m) against his reach
   * (m), and the ball's height there (m). */
  Vector2F toBall{.x = 1.0F, .y = 0.0F};
  float gap = 0.0F;
  float reach = 1.0F;
  float height = 0.0F;
  /** The top of his feet's band (m): the ball is fully "off the ground"
   * there. */
  float feetLine = MatchTuning::Ball::LOW_BALL_METRES;
  bool rightFooted = true;
  /** How little he expected it (0-1): the brain's read. */
  float surprise = 0.0F;
  /** How pressed he is (0-1, contactPressure). */
  float pressure = 0.0F;
  /** The spin he means (rad/s), and the most he can put on (rad/s; 0, no
   * spin to ask of him). */
  BallSpin intendedSpin{};
  float spinCapacity = 0.0F;
  /** His power for the strike (m/s; 0, none to ask of him: a touch). */
  float powerCapacity = 0.0F;
};

/** The share of his power he can put into a strike `degrees` off the way
 * his body faces (0-180): all of it straight ahead, falling as the square
 * to `square` at 90 degrees (inward or outward, which may differ), then in
 * a straight line to `behind` at 180. */
inline float powerByAngleShare(float degrees, float square, float behind)
{
  const float off = std::clamp(degrees, 0.0F, 180.0F);
  if (off <= 90.0F)
  {
    const float into = off / 90.0F;
    return 1.0F - ((1.0F - square) * into * into);
  }
  return square + ((behind - square) * (off - 90.0F) / 90.0F);
}

/** How much of his power `speed` asks (0-1 as a condition): nothing up to
 * `comfortShare` of `capacity`, then the square of how far into the rest
 * (1 at all of it, and beyond). 0 without a capacity. */
inline float powerAskedValue(float speed, float capacity, float comfortShare)
{
  if (capacity <= 0.0F)
  {
    return 0.0F;
  }
  const float share = speed / capacity;
  if (share <= comfortShare)
  {
    return 0.0F;
  }
  const float into = std::min(
      (share - comfortShare) / std::max(1.0F - comfortShare, 1.0e-3F), 1.0F);
  return into * into;
}

/** Whether a ball met `gap` metres from his centre, in direction `toBall`
 * (metric unit), is in his dead zone: beyond the comfort gap and more than
 * `angleDegrees` round from his `facing`, where he cannot touch it. */
inline bool inTouchDeadZone(Vector2F facing, Vector2F toBall, float gap,
                            float comfortGap, float angleDegrees)
{
  if (gap <= comfortGap)
  {
    return false;
  }
  const float cosine = (facing.x * toBall.x) + (facing.y * toBall.y);
  return cosine < std::cos(angleDegrees * std::numbers::pi_v<float> / 180.0F);
}

/** The approach angle (0-1): how far the ball comes from behind him, by
 * the angle between where
 * it comes from and his `facing` (both metric unit): 0 within 45 degrees
 * of his front, up to 0.15 at square, up to 1 at 165 degrees, 1 beyond
 * (MatchTuning::FirstTouch::BEHIND_*). */
float approachAngleValue(Vector2F from, Vector2F facing);

/** Whether sending the ball on along `aim` from where he meets it turns it
 * outward (likely the outside of his foot) rather than inward (the
 * instep): inward if its path crosses his facing line (his centre straight
 * out the way he faces) before leaving his reach, or leaves his reach
 * between that line and the line from his centre through the ball. A
 * proxy, being tried out in the contact view. */
bool turnsOutward(const ContactSituation& situation);

/** How pressed he is (0-1): 1 with the nearest opponent on top of him, 0
 * at `radiusMetres` and beyond. */
inline float contactPressure(float nearestOpponentMetres, float radiusMetres)
{
  return std::clamp(1.0F - (nearestOpponentMetres / radiusMetres), 0.0F, 1.0F);
}

/** How hard a contact is. No random draws. */
ContactAssessment assessContact(const ContactProfile& profile,
                                const ContactSituation& situation);

/** The touch knobs as a contact profile (first and dribble touches). */
ContactProfile touchContactProfile(const FirstTouchKnobs& knobs);

/** A kind of strike's contact profile, from the strike knobs (and the
 * touch's comfort gap). */
ContactProfile strikeContactProfile(const StrikeKnobs& knobs, StrikeKind kind,
                                    const FirstTouchKnobs& touchKnobs);
