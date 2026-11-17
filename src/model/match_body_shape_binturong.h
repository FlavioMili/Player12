// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

// The Binturong body's shapes as plain functions of the player, so tools
// can read them without a match (the sandbox's true view): the engine's
// members call these, so the two can't drift apart.

#include <algorithm>

#include "model/match_engine.h"
#include "model/match_tuning.h"

/** His lines between feet, thigh, chest and head (chest and thigh control
 * plan step 1): his knee, the top of a raised thigh, the top of his
 * shoulders, as shares of his height. */
inline BodyLines bodyLinesOfHeight(float heightMetres)
{
  using B = MatchTuning::Ball;
  return BodyLines{.feet = B::FEET_LINE_HEIGHT_SHARE * heightMetres,
                   .chest = B::CHEST_LINE_HEIGHT_SHARE * heightMetres,
                   .head = B::HEAD_LINE_HEIGHT_SHARE * heightMetres};
}

/** Where his head meets a ball, up and down, with his feet `feetHeight` off
 * the ground (aerial challenge plan step 1): from `lowest` (a duck below
 * his forehead, never below his head line) to `top` (the top of his head);
 * above `forehead`, only the top of his head. */
struct HeadBand
{
  float lowest = 0.0F;
  float forehead = 0.0F;
  float top = 0.0F;
};

inline HeadBand headBandOf(float heightMetres, float feetHeight)
{
  using A = MatchTuning::Aerial;
  HeadBand band;
  band.top = heightMetres + feetHeight;
  band.forehead = band.top - A::HEAD_OFFSET_METRES;
  band.lowest = std::max(bodyLinesOfHeight(heightMetres).head + feetHeight,
                         band.forehead - (A::DUCK_HEIGHT_SHARE * heightMetres));
  return band;
}

/** How far his feet rise in a jump (m): his running jump takes the top of
 * his head to his Jumping reach; his standing jump rises a share of that. */
inline float jumpRiseOf(float jumpingReachMetres, float heightMetres,
                        bool running)
{
  const float rise = std::max(jumpingReachMetres - heightMetres, 0.0F);
  return running ? rise : rise * MatchTuning::Aerial::STANDING_JUMP_SHARE;
}

/** How far from his centre a dribbler meets the ball (m): a better dribbler
 * keeps it close (the comfort gap), a poor one plays it at a stretch (out
 * to his kick reach). `dribbling` is his rating, 0-1. */
inline float dribbleMeetGapOf(float comfortGap, float dribbling)
{
  return comfortGap +
         ((std::max(MatchTuning::Dribble::KICK_REACH_METRES, comfortGap) -
           comfortGap) *
          (1.0F - std::clamp(dribbling, 0.0F, 1.0F)));
}

/** The standing tackle's range (m; the old roll), from his Defending
 * rating (0-1). In a Binturong body no standing roll fires inside it with
 * the ball at the carrier's feet (a contest touch instead): it is the inner
 * edge of the slide's range. */
inline float standingTackleReachOf(float defending)
{
  using D = MatchTuning::Defending;
  return D::TACKLE_DISTANCE_METRES +
         (defending * D::TACKLE_DEFENDING_REACH_METRES);
}

/** Where his legs naturally are when a ball needs them (shot block plan
 * §3c): his movement type from his speed and his travel against his
 * facing (metric, unit), then a point of his stride rolled by the time
 * spent there, with noise. `rolls` are six uniform draws in [0, 1): the
 * legs' direction, which leg leads, the point of the stride, the spread,
 * and each foot side to side. */
inline LegPose legPoseOf(float heightMetres, Vector2F velocity, Vector2F facing,
                         const std::array<float, 6>& rolls)
{
  using S = MatchTuning::Stride;
  const float scale = heightMetres / S::REFERENCE_HEIGHT_METRES;
  const float hipHeight =
      MatchTuning::Ball::CHEST_LINE_HEIGHT_SHARE * heightMetres;
  const float kneeHeight =
      MatchTuning::Ball::FEET_LINE_HEIGHT_SHARE * heightMetres;
  const float ankleHeight = S::FOOT_HEIGHT * scale;
  const auto symmetric = [](float roll) { return (2.0F * roll) - 1.0F; };

  const float speed = std::hypot(velocity.x, velocity.y);
  const Vector2F travel =
      speed > 1e-4F ? Vector2F{.x = velocity.x / speed, .y = velocity.y / speed}
                    : facing;
  const float offFacing =
      std::acos(std::clamp((travel.x * facing.x) + (travel.y * facing.y), -1.0F,
                           1.0F)) *
      57.2957795F;

  enum class Kind : std::uint8_t
  {
    STANDING,
    FORWARD,
    SIDESTEP,
    BACKPEDAL
  };
  Kind kind = Kind::FORWARD;
  int gait = 0;  // walk, jog, run, sprint (a backpedal as a walk)
  float step = S::STEP_WALK;
  if (speed < S::STANDING_SPEED)
  {
    kind = Kind::STANDING;
    step = S::STANDING_FEET_APART;
  }
  else if (speed <= S::SHUFFLE_TOP_SPEED && offFacing > S::BACKPEDAL_DEGREES)
  {
    kind = Kind::BACKPEDAL;
    step = S::STEP_BACKPEDAL;
  }
  else if (speed <= S::SHUFFLE_TOP_SPEED && offFacing > S::SIDESTEP_DEGREES)
  {
    kind = Kind::SIDESTEP;
    step = S::STEP_SIDESTEP;
  }
  else
  {
    // Forward, or on a diagonal: his legs spread along his travel.
    constexpr float STEPS[4] = {S::STEP_WALK, S::STEP_JOG, S::STEP_RUN,
                                S::STEP_SPRINT};
    gait = speed <= S::WALK_TOP_SPEED  ? 0
           : speed <= S::JOG_TOP_SPEED ? 1
           : speed <= S::RUN_TOP_SPEED ? 2
                                       : 3;
    step = STEPS[gait];
  }

  // Hips side by side across his body; the legs spread along an axis
  // (standing: across him too), turned a little by the noise.
  const Vector2F across{.x = -facing.y, .y = facing.x};
  Vector2F axis = kind == Kind::STANDING ? across : travel;
  const float turn = symmetric(rolls[0]) * S::NOISE_DEGREES * 0.0174532925F;
  axis = {.x = (axis.x * std::cos(turn)) - (axis.y * std::sin(turn)),
          .y = (axis.x * std::sin(turn)) + (axis.y * std::cos(turn))};
  const Vector2F sideways{.x = -axis.y, .y = axis.x};
  const float spread =
      step * scale * (1.0F + symmetric(rolls[3]) * S::NOISE_SPREAD);

  // Each leg: where its foot is along the axis and how far it is lifted;
  // a passing swing leg folds (knee ahead, heel up behind).
  struct Foot
  {
    float along = 0.0F;
    float rise = 0.0F;
    bool folded = false;
  };
  std::array<Foot, 2> feet{};
  const std::size_t lead = rolls[1] < 0.5F ? 0 : 1;
  const std::size_t other = 1 - lead;
  const float point = rolls[2];
  if (kind == Kind::STANDING)
  {
    feet[0].along = 0.5F * spread;
    feet[1].along = -0.5F * spread;
  }
  else if (kind == Kind::SIDESTEP)
  {
    const float apart =
        point < S::SIDESTEP_SHARE_WIDE ? spread
        : point < S::SIDESTEP_SHARE_WIDE + S::SIDESTEP_SHARE_HALF
            ? 0.5F * spread
            : S::SIDESTEP_TOGETHER * scale;
    feet[0].along = 0.5F * apart;
    feet[1].along = -0.5F * apart;
  }
  else
  {
    const float full = S::SHARE_FULL[gait];
    const float part = S::SHARE_PART[gait];
    if (point < full)
    {
      feet[lead].along = 0.5F * spread;
      feet[other].along = -0.5F * spread;
    }
    else if (point < full + part)
    {
      // Opening (the swing foot reaching ahead) or closing (just off the
      // ground behind): the planted foot opposite.
      const bool opening = point < full + 0.5F * part;
      const float half = 0.5F * S::PART_SPREAD_SHARE * spread;
      feet[lead].along = opening ? half : -half;
      feet[lead].rise = S::SWING_RISE_PART[gait];
      feet[other].along = opening ? -half : half;
    }
    else
    {
      feet[lead].along = -S::PASSING_ANKLE_BEHIND * scale;
      feet[lead].rise = S::SWING_RISE_PASSING[gait];
      feet[lead].folded = true;
    }
  }

  LegPose pose;
  const Vector2F footFacing = kind == Kind::FORWARD ? axis : facing;
  const float runningApart = kind == Kind::FORWARD || kind == Kind::BACKPEDAL
                                 ? 0.5F * S::RUNNING_FEET_APART * scale
                                 : 0.0F;
  for (std::size_t index = 0; index < 2; ++index)
  {
    const float hipSide = (index == 0 ? 0.5F : -0.5F) * S::HIPS_APART * scale;
    // His feet stay on their own hip's side of the line they run along.
    const float sideSign =
        (across.x * sideways.x + across.y * sideways.y) * hipSide >= 0.0F
            ? 1.0F
            : -1.0F;
    const float side = (sideSign * runningApart) +
                       (symmetric(rolls[4 + index]) * S::NOISE_SIDEWAYS);
    const Foot& foot = feet[index];
    LegPose::Leg& leg = pose.legs[index];
    leg.hip = {
        .x = across.x * hipSide, .y = across.y * hipSide, .z = hipHeight};
    leg.footFacing = footFacing;
    leg.ankle = {.x = (axis.x * foot.along) + (sideways.x * side),
                 .y = (axis.y * foot.along) + (sideways.y * side),
                 .z = ankleHeight + foot.rise};
    if (foot.folded)
    {
      const float ahead = S::PASSING_KNEE_AHEAD * scale;
      leg.knee = {.x = leg.hip.x + (axis.x * ahead),
                  .y = leg.hip.y + (axis.y * ahead),
                  .z = kneeHeight + (0.5F * foot.rise)};
    }
    else
    {
      leg.knee = {.x = 0.5F * (leg.hip.x + leg.ankle.x),
                  .y = 0.5F * (leg.hip.y + leg.ankle.y),
                  .z = 0.5F * (leg.hip.z + leg.ankle.z)};
    }
  }
  return pose;
}

/** His hip joints (shot block plan §3c): across his `facing` (metric,
 * unit), leg 0 on the side of (-facing.y, facing.x). */
inline Point3F hipOf(float heightMetres, Vector2F facing, std::size_t leg)
{
  const float scale =
      heightMetres / MatchTuning::Stride::REFERENCE_HEIGHT_METRES;
  const float side =
      (leg == 0 ? 0.5F : -0.5F) * MatchTuning::Stride::HIPS_APART * scale;
  return {.x = -facing.y * side,
          .y = facing.x * side,
          .z = MatchTuning::Ball::CHEST_LINE_HEIGHT_SHARE * heightMetres};
}

/** A leg put in a shot's way (shot block plan §3d): the other planted
 * under its hip, `leg` straight from its hip to an ankle `outShare` (0-1)
 * of the way from under the hip to `aim`, its foot along `footFacing`. */
inline LegPose blockLegPoseOf(float heightMetres, Vector2F facing,
                              std::size_t leg, Point3F aim, Vector2F footFacing,
                              float outShare)
{
  const float ankleHeight = MatchTuning::Stride::FOOT_HEIGHT * heightMetres /
                            MatchTuning::Stride::REFERENCE_HEIGHT_METRES;
  LegPose pose;
  for (std::size_t index = 0; index < 2; ++index)
  {
    LegPose::Leg& each = pose.legs[index];
    each.hip = hipOf(heightMetres, facing, index);
    const Point3F under{.x = each.hip.x, .y = each.hip.y, .z = ankleHeight};
    each.ankle = under;
    each.footFacing = facing;
    if (index == leg)
    {
      const float out = std::clamp(outShare, 0.0F, 1.0F);
      each.ankle = {.x = under.x + ((aim.x - under.x) * out),
                    .y = under.y + ((aim.y - under.y) * out),
                    .z = under.z + ((aim.z - under.z) * out)};
      each.footFacing = footFacing;
    }
    each.knee = {.x = 0.5F * (each.hip.x + each.ankle.x),
                 .y = 0.5F * (each.hip.y + each.ankle.y),
                 .z = 0.5F * (each.hip.z + each.ankle.z)};
  }
  return pose;
}
