// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

// First-touch knobs: every game constant that changes how well a first touch
// comes off (Binturong brain and body, see Notes and plans/
// match_engine_first_touch_tuning_plan.md), as runtime values each engine
// holds. They default to the MatchTuning constants, so a match nobody sets
// them on plays exactly as before; drills and the lab change one at a time.
//
// To add a knob: an id in TouchKnob (before COUNT), a row in TOUCH_KNOBS in
// the same place, then read it with knobs[TouchKnob::...] where the
// constant was used. The drill, the lab and the sensitivity ranking read
// this registry, so they need nothing more.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "model/match_tuning.h"
#include "model/tactics.h"

enum class TouchKnob : std::uint8_t
{
  // Technique (body): how often each job goes wrong, and how badly.
  ANGLE_FAIL_BASE,
  ANGLE_FAIL_RANGE,
  WEIGHT_FAIL_BASE,
  WEIGHT_FAIL_RANGE,
  SKILL_REDUCTION,
  ANGLE_MISS_BASE,
  ANGLE_MISS_RANGE,
  WEIGHT_MISS_BASE,
  WEIGHT_MISS_RANGE,
  HEAVY_SHARE,
  UNDER_TURN_SHARE,
  ANGLE_JITTER,
  WEIGHT_JITTER,
  REBOUND_SHARE,
  MAX_TOUCH_SPEED,
  FATIGUE_ERROR_GAIN,
  WEAKER_FOOT_ERROR_GAIN,
  // Difficulty (body): the jobs' sizes, then the conditions' angle and
  // weight weights.
  PACE_CHANGE_WEIGHT,
  TURN_ANGLE,
  TURN_OUTWARD,
  TURN_INWARD,
  VERTICAL_TURN_ANGLE,
  INCOMING_SPEED_ANGLE,
  INCOMING_SPEED_WEIGHT,
  APPROACH_ANGLE,
  APPROACH_WEIGHT,
  OWN_MOVEMENT_ANGLE,
  OWN_MOVEMENT_WEIGHT,
  STRETCH_ANGLE,
  STRETCH_WEIGHT,
  WEAK_FOOT_ANGLE,
  WEAK_FOOT_WEIGHT,
  OFF_GROUND_ANGLE,
  OFF_GROUND_WEIGHT,
  SURPRISE_ANGLE,
  SURPRISE_WEIGHT,
  PRESSURE_ANGLE,
  PRESSURE_WEIGHT,
  // How the factors are measured (body).
  REFERENCE_SPEED,
  PRESSURE_RADIUS,
  // Reach (Binturong bodies only): feet height, and the dead zone behind.
  FEET_REACH,
  DEAD_ZONE_ANGLE,
  // Timing (brain): when he plays it.
  COMFORT_GAP,
  WAIT_GAIN,
  WAIT_MARGIN,
  TIMING_LOOKAHEAD,
  // Intent (brain): what he tries to do with it.
  SET_OFFSET,
  SET_LEAD,
  STRIDE_LEAD,
  STRIDE_EXTRA,
  STRIDE_SPEED_SHARE,
  STRIDE_CLEARANCE,
  // The claim: collecting it, or a team-mate taking it over.
  COLLECT_RELATIVE_SPEED,
  TEAMMATE_DISTANCE,
  COUNT
};
inline constexpr std::size_t TOUCH_KNOB_COUNT =
    static_cast<std::size_t>(TouchKnob::COUNT);

/** A knob's name, group, default and sensible range (the sensitivity steps
 * stay inside it; a value set explicitly may go beyond). */
struct TouchKnobInfo
{
  std::string_view name;
  std::string_view group;
  float defaultValue = 0.0F;
  float min = 0.0F;
  float max = 1.0F;
  std::string_view unit;
};

namespace TouchKnobDefaults
{
using F = MatchTuning::FirstTouch;
using Ball = MatchTuning::Ball;
}  // namespace TouchKnobDefaults

inline constexpr std::array<TouchKnobInfo, TOUCH_KNOB_COUNT> TOUCH_KNOBS{{
    // Technique
    {.name = "Angle fail base",
     .group = "Technique",
     .defaultValue = TouchKnobDefaults::F::ANGLE_FAIL_BASE,
     .min = 0.0F,
     .max = 0.3F,
     .unit = "chance"},
    {.name = "Angle fail range",
     .group = "Technique",
     .defaultValue = TouchKnobDefaults::F::ANGLE_FAIL_RANGE,
     .min = 0.0F,
     .max = 1.0F,
     .unit = "chance"},
    {.name = "Weight fail base",
     .group = "Technique",
     .defaultValue = TouchKnobDefaults::F::WEIGHT_FAIL_BASE,
     .min = 0.0F,
     .max = 0.3F,
     .unit = "chance"},
    {.name = "Weight fail range",
     .group = "Technique",
     .defaultValue = TouchKnobDefaults::F::WEIGHT_FAIL_RANGE,
     .min = 0.0F,
     .max = 1.0F,
     .unit = "chance"},
    {.name = "Skill reduction",
     .group = "Technique",
     .defaultValue = TouchKnobDefaults::F::SKILL_REDUCTION,
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Angle miss base",
     .group = "Technique",
     .defaultValue = TouchKnobDefaults::F::ANGLE_MISS_BASE,
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Angle miss range",
     .group = "Technique",
     .defaultValue = TouchKnobDefaults::F::ANGLE_MISS_RANGE,
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Weight miss base",
     .group = "Technique",
     .defaultValue = TouchKnobDefaults::F::WEIGHT_MISS_BASE,
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Weight miss range",
     .group = "Technique",
     .defaultValue = TouchKnobDefaults::F::WEIGHT_MISS_RANGE,
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Heavy share",
     .group = "Technique",
     .defaultValue = TouchKnobDefaults::F::HEAVY_SHARE,
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Under-turn share",
     .group = "Technique",
     .defaultValue = TouchKnobDefaults::F::UNDER_TURN_SHARE,
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Angle jitter",
     .group = "Technique",
     .defaultValue = TouchKnobDefaults::F::ANGLE_JITTER,
     .min = 0.0F,
     .max = 0.2F,
     .unit = "rad"},
    {.name = "Weight jitter",
     .group = "Technique",
     .defaultValue = TouchKnobDefaults::F::WEIGHT_JITTER,
     .min = 0.0F,
     .max = 0.2F,
     .unit = "share"},
    {.name = "Rebound share",
     .group = "Technique",
     .defaultValue = TouchKnobDefaults::F::REBOUND_SHARE,
     .min = 0.2F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Max touch speed",
     .group = "Technique",
     .defaultValue = TouchKnobDefaults::F::MAX_TOUCH_SPEED,
     .min = 2.0F,
     .max = 15.0F,
     .unit = "m/s"},
    {.name = "Fatigue error gain",
     .group = "Technique",
     .defaultValue = MatchTuning::Fatigue::SPENT_TECHNIQUE_ERROR,
     .min = 0.0F,
     .max = 1.5F,
     .unit = "x"},
    {.name = "Weaker foot error gain",
     .group = "Technique",
     .defaultValue = MatchTuning::Player::WEAKER_FOOT_ERROR_GAIN,
     .min = 0.0F,
     .max = 2.0F,
     .unit = "x"},
    // Factors
    {.name = "Pace change: weight",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::PACE_CHANGE_WEIGHT,
     .min = 0.0F,
     .max = 1.5F,
     .unit = ""},
    {.name = "Turn: angle",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::TURN_ANGLE,
     .min = 0.0F,
     .max = 1.5F,
     .unit = ""},
    {.name = "Turn outward",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::TURN_OUTWARD,
     .min = 0.5F,
     .max = 3.0F,
     .unit = "x"},
    {.name = "Turn inward",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::TURN_INWARD,
     .min = 0.25F,
     .max = 1.5F,
     .unit = "x"},
    {.name = "Vertical turn: angle",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::VERTICAL_TURN_ANGLE,
     .min = 0.0F,
     .max = 1.5F,
     .unit = ""},
    {.name = "Incoming pace: angle",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::INCOMING_SPEED.angle,
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Incoming pace: weight",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::INCOMING_SPEED.weight,
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Approach angle: angle",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::APPROACH.angle,
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Approach angle: weight",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::APPROACH.weight,
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Own speed: angle",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::OWN_MOVEMENT.angle,
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Own speed: weight",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::OWN_MOVEMENT.weight,
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Stretch: angle",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::STRETCH.angle,
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Stretch: weight",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::STRETCH.weight,
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Weaker foot: angle",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::WEAK_FOOT.angle,
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Weaker foot: weight",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::WEAK_FOOT.weight,
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Off the ground: angle",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::OFF_GROUND.angle,
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Off the ground: weight",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::OFF_GROUND.weight,
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Surprise: angle",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::SURPRISE.angle,
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Surprise: weight",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::SURPRISE.weight,
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Pressure: angle",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::PRESSURE.angle,
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Pressure: weight",
     .group = "Factors",
     .defaultValue = TouchKnobDefaults::F::PRESSURE.weight,
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    // Measures
    {.name = "Reference speed",
     .group = "Measures",
     .defaultValue = TouchKnobDefaults::F::REFERENCE_SPEED,
     .min = 10.0F,
     .max = 40.0F,
     .unit = "m/s"},
    {.name = "Pressure radius",
     .group = "Measures",
     .defaultValue = TouchKnobDefaults::F::PRESSURE_RADIUS_METRES,
     .min = 0.5F,
     .max = 8.0F,
     .unit = "m"},
    // Reach
    {.name = "Feet reach",
     .group = "Reach",
     .defaultValue = TouchKnobDefaults::Ball::OUTFIELD_CONTROL_RADIUS_METRES,
     .min = 0.5F,
     .max = 1.5F,
     .unit = "m"},
    {.name = "Dead zone from",
     .group = "Reach",
     .defaultValue = TouchKnobDefaults::F::DEAD_ZONE_DEGREES,
     .min = 90.0F,
     .max = 180.0F,
     .unit = "deg"},
    // Timing
    {.name = "Comfort gap",
     .group = "Timing",
     .defaultValue = TouchKnobDefaults::F::COMFORT_GAP_METRES,
     .min = 0.0F,
     .max = 0.8F,
     .unit = "m"},
    {.name = "Wait gain",
     .group = "Timing",
     .defaultValue = TouchKnobDefaults::F::WAIT_GAIN_METRES,
     .min = 0.0F,
     .max = 0.5F,
     .unit = "m"},
    {.name = "Wait margin",
     .group = "Timing",
     .defaultValue = TouchKnobDefaults::F::WAIT_MARGIN_METRES,
     .min = 0.0F,
     .max = 0.5F,
     .unit = "m"},
    {.name = "Timing lookahead",
     .group = "Timing",
     .defaultValue = TouchKnobDefaults::F::TIMING_LOOKAHEAD_SECONDS,
     .min = 0.05F,
     .max = 1.0F,
     .unit = "s"},
    // Intent
    {.name = "Set offset",
     .group = "Intent",
     .defaultValue = TouchKnobDefaults::F::SET_OFFSET_METRES,
     .min = 0.0F,
     .max = 2.0F,
     .unit = "m"},
    {.name = "Set lead",
     .group = "Intent",
     .defaultValue = TouchKnobDefaults::F::SET_LEAD_SECONDS,
     .min = 0.0F,
     .max = 1.0F,
     .unit = "s"},
    {.name = "Stride lead",
     .group = "Intent",
     .defaultValue = TouchKnobDefaults::F::STRIDE_LEAD_SECONDS,
     .min = 0.0F,
     .max = 2.0F,
     .unit = "s"},
    {.name = "Stride extra",
     .group = "Intent",
     .defaultValue = TouchKnobDefaults::F::STRIDE_EXTRA_METRES,
     .min = 0.0F,
     .max = 3.0F,
     .unit = "m"},
    {.name = "Stride speed share",
     .group = "Intent",
     .defaultValue = TouchKnobDefaults::F::STRIDE_SPEED_SHARE,
     .min = 0.0F,
     .max = 1.2F,
     .unit = "share"},
    {.name = "Stride clearance",
     .group = "Intent",
     .defaultValue = TouchKnobDefaults::F::STRIDE_CLEARANCE_METRES,
     .min = 0.0F,
     .max = 10.0F,
     .unit = "m"},
    // Claim
    {.name = "Collect relative speed",
     .group = "Claim",
     .defaultValue = TouchKnobDefaults::F::COLLECT_RELATIVE_SPEED,
     .min = 0.3F,
     .max = 4.0F,
     .unit = "m/s"},
    {.name = "Team-mate distance",
     .group = "Claim",
     .defaultValue = TouchKnobDefaults::F::TEAMMATE_TAKES_METRES,
     .min = 1.0F,
     .max = 10.0F,
     .unit = "m"},
}};

inline const TouchKnobInfo& touchKnobInfo(TouchKnob knob)
{
  return TOUCH_KNOBS[static_cast<std::size_t>(knob)];
}

/** A knob by its name (exact, case as in the registry). */
inline std::optional<TouchKnob> touchKnobByName(std::string_view name)
{
  for (std::size_t index = 0; index < TOUCH_KNOB_COUNT; ++index)
  {
    if (TOUCH_KNOBS[index].name == name)
    {
      return static_cast<TouchKnob>(index);
    }
  }
  return std::nullopt;
}

/** The knob values one engine plays with. */
struct FirstTouchKnobs
{
  std::array<float, TOUCH_KNOB_COUNT> values = []
  {
    std::array<float, TOUCH_KNOB_COUNT> defaults{};
    for (std::size_t index = 0; index < TOUCH_KNOB_COUNT; ++index)
    {
      defaults[index] = TOUCH_KNOBS[index].defaultValue;
    }
    return defaults;
  }();

  float operator[](TouchKnob knob) const
  {
    return values[static_cast<std::size_t>(knob)];
  }
  float& operator[](TouchKnob knob)
  {
    return values[static_cast<std::size_t>(knob)];
  }
};
