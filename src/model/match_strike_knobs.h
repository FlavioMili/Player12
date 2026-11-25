// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

// Strike knobs: every value of a strike's contact profile (Binturong body,
// Notes and plans/match_engine_contact_plan.md §3 and §5), per kind of
// strike, as runtime values each engine holds. They default to the
// MatchTuning constants, so a match nobody sets them on plays exactly as
// before; drills and the lab change them. A knob's full name is "<kind>:
// <name>", e.g. "Ground pass: Angle fail base".
//
// To add a knob: an id in StrikeKnob (before COUNT), a row in STRIKE_KNOBS
// in the same place, its default in strikeProfileDefault (and every
// kind's profile in MatchTuning::Strike), then read it in
// strikeProfileBinturong.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "model/match_tuning.h"
#include "model/tactics.h"

/** The kinds of strike the contact model carries out (contact plan §3). */
enum class StrikeKind : std::uint8_t
{
  GROUND_PASS,
  LOFTED_PASS,
  CROSS,
  SHOT,
  /** Shots, clearances and knock-downs with the head. */
  HEADER,
  /** A clearance with the foot, or putting it behind. */
  CLEARANCE,
  /** Throw-ins, and a keeper rolling or throwing it from his hands. */
  THROW,
  /** A pass struck low and hard with the instep. */
  DRIVEN_PASS,
  /** A pass stabbed up steeply with backspin, over someone close. */
  CHIP,
  /** Shot types (shooting technique plan step 5; SHOT is the driven one):
   * side-footed, placed; bent with the instep's wrap; chipped over the
   * keeper; poked with the toe. */
  PLACED_SHOT,
  CURLED_SHOT,
  CHIP_SHOT,
  TOE_POKE,
};
inline constexpr std::size_t STRIKE_KIND_COUNT = 13;
inline constexpr std::array<std::string_view, STRIKE_KIND_COUNT>
    STRIKE_KIND_NAMES = {
        "Ground pass", "Lofted pass", "Cross",       "Shot", "Header",
        "Clearance",   "Throw",       "Driven pass", "Chip", "Placed shot",
        "Curled shot", "Chip shot",   "Toe-poke"};

enum class StrikeKnob : std::uint8_t
{
  // Difficulty: the jobs' sizes, then the conditions' angle and weight
  // weights.
  PACE_CHANGE_WEIGHT,
  TURN_ANGLE,
  TURN_PACE_REFERENCE,
  VERTICAL_TURN_ANGLE,
  INCOMING_PACE_ANGLE,
  INCOMING_PACE_WEIGHT,
  OWN_SPEED_ANGLE,
  OWN_SPEED_WEIGHT,
  STRETCH_ANGLE,
  STRETCH_WEIGHT,
  WEAK_FOOT_ANGLE,
  WEAK_FOOT_WEIGHT,
  OFF_GROUND_ANGLE,
  OFF_GROUND_WEIGHT,
  PRESSURE_ANGLE,
  PRESSURE_WEIGHT,
  APPROACH_ANGLE,
  APPROACH_WEIGHT,
  SPIN_ASKED_ANGLE,
  SPIN_ASKED_WEIGHT,
  SPIN_ASKED_SPIN,
  // How the conditions are measured.
  REFERENCE_SPEED,
  PRESSURE_RADIUS,
  // Technique: how often each job goes wrong, and how badly.
  SKILL_REDUCTION,
  ANGLE_FAIL_BASE,
  ANGLE_FAIL_RANGE,
  WEIGHT_FAIL_BASE,
  WEIGHT_FAIL_RANGE,
  ANGLE_MISS_BASE,
  ANGLE_MISS_RANGE,
  ANGLE_MISS_ABSOLUTE_BASE,
  ANGLE_MISS_ABSOLUTE_RANGE,
  WEIGHT_MISS_BASE,
  WEIGHT_MISS_RANGE,
  UNDER_TURN_SHARE,
  HEAVY_SHARE,
  ANGLE_JITTER,
  WEIGHT_JITTER,
  VERTICAL_SHARE,
  UP_SHARE,
  ELEVATION_JITTER,
  SPIN_FAIL_BASE,
  SPIN_FAIL_RANGE,
  SPIN_MISS_BASE,
  SPIN_MISS_RANGE,
  UNDER_SPIN_SHARE,
  SPIN_JITTER,
  // Power asked (shooting technique plan step 3): its weights, where it
  // starts to cost, and how much of the jitter grows with difficulty.
  POWER_ASKED_ANGLE,
  POWER_ASKED_WEIGHT,
  POWER_COMFORT_SHARE,
  JITTER_DIFFICULTY_SHARE,
  // Power by the angle of the strike off his body (step 4).
  POWER_INWARD_SQUARE,
  POWER_OUTWARD_SQUARE,
  POWER_BEHIND,
  COUNT
};
inline constexpr std::size_t STRIKE_KNOB_COUNT =
    static_cast<std::size_t>(StrikeKnob::COUNT);

/** A knob's name, group and sensible range (the same for every kind). */
struct StrikeKnobInfo
{
  std::string_view name;
  std::string_view group;
  float min = 0.0F;
  float max = 1.0F;
  std::string_view unit;
};

inline constexpr std::array<StrikeKnobInfo, STRIKE_KNOB_COUNT> STRIKE_KNOBS{{
    {.name = "Pace change: weight",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.5F,
     .unit = ""},
    {.name = "Turn: angle",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.5F,
     .unit = ""},
    {.name = "Turn: pace reference",
     .group = "Factors",
     .min = 0.0F,
     .max = 25.0F,
     .unit = "m/s"},
    {.name = "Vertical turn: angle",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.5F,
     .unit = ""},
    {.name = "Incoming pace: angle",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Incoming pace: weight",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Own speed: angle",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Own speed: weight",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Stretch: angle",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Stretch: weight",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Weaker foot: angle",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Weaker foot: weight",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Off the ground: angle",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Off the ground: weight",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Pressure: angle",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Pressure: weight",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Approach angle: angle",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Approach angle: weight",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Spin asked: angle",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Spin asked: weight",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Spin asked: spin",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.0F,
     .unit = ""},
    {.name = "Reference speed",
     .group = "Measures",
     .min = 10.0F,
     .max = 40.0F,
     .unit = "m/s"},
    {.name = "Pressure radius",
     .group = "Measures",
     .min = 0.5F,
     .max = 12.0F,
     .unit = "m"},
    {.name = "Skill reduction",
     .group = "Technique",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Angle fail base",
     .group = "Technique",
     .min = 0.0F,
     .max = 0.3F,
     .unit = "chance"},
    {.name = "Angle fail range",
     .group = "Technique",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "chance"},
    {.name = "Weight fail base",
     .group = "Technique",
     .min = 0.0F,
     .max = 0.3F,
     .unit = "chance"},
    {.name = "Weight fail range",
     .group = "Technique",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "chance"},
    {.name = "Angle miss base",
     .group = "Technique",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Angle miss range",
     .group = "Technique",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Angle miss absolute base",
     .group = "Technique",
     .min = 0.0F,
     .max = 0.3F,
     .unit = "rad"},
    {.name = "Angle miss absolute range",
     .group = "Technique",
     .min = 0.0F,
     .max = 0.6F,
     .unit = "rad"},
    {.name = "Weight miss base",
     .group = "Technique",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Weight miss range",
     .group = "Technique",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Under-turn share",
     .group = "Technique",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Heavy share",
     .group = "Technique",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Angle jitter",
     .group = "Technique",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "rad"},
    {.name = "Weight jitter",
     .group = "Technique",
     .min = 0.0F,
     .max = 0.2F,
     .unit = "share"},
    {.name = "Vertical share",
     .group = "Technique",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Up share",
     .group = "Technique",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Elevation jitter",
     .group = "Technique",
     .min = 0.0F,
     .max = 0.3F,
     .unit = "rad"},
    {.name = "Spin fail base",
     .group = "Technique",
     .min = 0.0F,
     .max = 0.3F,
     .unit = "chance"},
    {.name = "Spin fail range",
     .group = "Technique",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "chance"},
    {.name = "Spin miss base",
     .group = "Technique",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Spin miss range",
     .group = "Technique",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Under-spin share",
     .group = "Technique",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Spin jitter",
     .group = "Technique",
     .min = 0.0F,
     .max = 20.0F,
     .unit = "rad/s"},
    {.name = "Power asked: angle",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.5F,
     .unit = ""},
    {.name = "Power asked: weight",
     .group = "Factors",
     .min = 0.0F,
     .max = 1.5F,
     .unit = ""},
    {.name = "Power comfort share",
     .group = "Measures",
     .min = 0.0F,
     .max = 0.99F,
     .unit = "share"},
    {.name = "Jitter difficulty share",
     .group = "Technique",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Power square, inward",
     .group = "Measures",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Power square, outward",
     .group = "Measures",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
    {.name = "Power behind",
     .group = "Measures",
     .min = 0.0F,
     .max = 1.0F,
     .unit = "share"},
}};

inline const StrikeKnobInfo& strikeKnobInfo(StrikeKnob knob)
{
  return STRIKE_KNOBS[static_cast<std::size_t>(knob)];
}

/** A knob's default from a kind's profile in MatchTuning::Strike. */
template <typename P>
constexpr float strikeProfileDefault(StrikeKnob knob)
{
  switch (knob)
  {
    case StrikeKnob::PACE_CHANGE_WEIGHT:
      return P::PACE_CHANGE;
    case StrikeKnob::TURN_ANGLE:
      return P::TURN_ANGLE;
    case StrikeKnob::TURN_PACE_REFERENCE:
      return P::TURN_PACE_REFERENCE;
    // The turn in steepness weighs as the turn seen from above, to start.
    case StrikeKnob::VERTICAL_TURN_ANGLE:
      return P::TURN_ANGLE;
    case StrikeKnob::INCOMING_PACE_ANGLE:
    case StrikeKnob::INCOMING_PACE_WEIGHT:
      return P::INCOMING_PACE;
    case StrikeKnob::OWN_SPEED_ANGLE:
    case StrikeKnob::OWN_SPEED_WEIGHT:
      return P::OWN_SPEED;
    case StrikeKnob::STRETCH_ANGLE:
    case StrikeKnob::STRETCH_WEIGHT:
      return P::STRETCH;
    case StrikeKnob::WEAK_FOOT_ANGLE:
    case StrikeKnob::WEAK_FOOT_WEIGHT:
      return P::WEAKER_FOOT;
    case StrikeKnob::OFF_GROUND_ANGLE:
    case StrikeKnob::OFF_GROUND_WEIGHT:
      return P::OFF_GROUND;
    case StrikeKnob::PRESSURE_ANGLE:
    case StrikeKnob::PRESSURE_WEIGHT:
      return P::PRESSURE;
    // The approach angle (where it comes from against his facing), as the
    // first touch weighs it, for passes and shots.
    case StrikeKnob::APPROACH_ANGLE:
      return P::APPROACH_ANGLE;
    case StrikeKnob::APPROACH_WEIGHT:
      return P::APPROACH_WEIGHT;
    // Spin: in the model, nothing set yet (it comes off as asked).
    case StrikeKnob::SPIN_ASKED_ANGLE:
    case StrikeKnob::SPIN_ASKED_WEIGHT:
    case StrikeKnob::SPIN_ASKED_SPIN:
    case StrikeKnob::SPIN_FAIL_BASE:
    case StrikeKnob::SPIN_FAIL_RANGE:
    case StrikeKnob::SPIN_MISS_BASE:
    case StrikeKnob::SPIN_MISS_RANGE:
    case StrikeKnob::SPIN_JITTER:
      return 0.0F;
    case StrikeKnob::UNDER_SPIN_SHARE:
      return 1.0F;
    case StrikeKnob::REFERENCE_SPEED:
      return P::REFERENCE_SPEED;
    case StrikeKnob::PRESSURE_RADIUS:
      return P::PRESSURE_RADIUS_METRES;
    case StrikeKnob::SKILL_REDUCTION:
      return P::SKILL_REDUCTION;
    case StrikeKnob::ANGLE_FAIL_BASE:
      return P::ANGLE_FAIL_BASE;
    case StrikeKnob::ANGLE_FAIL_RANGE:
      return P::ANGLE_FAIL_RANGE;
    case StrikeKnob::WEIGHT_FAIL_BASE:
      return P::WEIGHT_FAIL_BASE;
    case StrikeKnob::WEIGHT_FAIL_RANGE:
      return P::WEIGHT_FAIL_RANGE;
    case StrikeKnob::ANGLE_MISS_BASE:
      return P::ANGLE_MISS_BASE;
    case StrikeKnob::ANGLE_MISS_RANGE:
      return P::ANGLE_MISS_RANGE;
    case StrikeKnob::ANGLE_MISS_ABSOLUTE_BASE:
      return P::ANGLE_MISS_ABSOLUTE_BASE;
    case StrikeKnob::ANGLE_MISS_ABSOLUTE_RANGE:
      return P::ANGLE_MISS_ABSOLUTE_RANGE;
    case StrikeKnob::WEIGHT_MISS_BASE:
      return P::WEIGHT_MISS_BASE;
    case StrikeKnob::WEIGHT_MISS_RANGE:
      return P::WEIGHT_MISS_RANGE;
    case StrikeKnob::UNDER_TURN_SHARE:
      return P::UNDER_TURN_SHARE;
    case StrikeKnob::HEAVY_SHARE:
      return P::HEAVY_SHARE;
    case StrikeKnob::ANGLE_JITTER:
      return P::ANGLE_JITTER;
    case StrikeKnob::WEIGHT_JITTER:
      return P::WEIGHT_JITTER;
    case StrikeKnob::VERTICAL_SHARE:
      return P::VERTICAL_SHARE;
    case StrikeKnob::UP_SHARE:
      return P::UP_SHARE;
    case StrikeKnob::ELEVATION_JITTER:
      return P::ELEVATION_JITTER;
    case StrikeKnob::POWER_ASKED_ANGLE:
      return P::POWER_ASKED_ANGLE;
    case StrikeKnob::POWER_ASKED_WEIGHT:
      return P::POWER_ASKED_WEIGHT;
    case StrikeKnob::POWER_COMFORT_SHARE:
      return P::POWER_COMFORT_SHARE;
    case StrikeKnob::JITTER_DIFFICULTY_SHARE:
      return P::JITTER_DIFFICULTY_SHARE;
    case StrikeKnob::POWER_INWARD_SQUARE:
      return P::POWER_INWARD_SQUARE;
    case StrikeKnob::POWER_OUTWARD_SQUARE:
      return P::POWER_OUTWARD_SQUARE;
    case StrikeKnob::POWER_BEHIND:
      return P::POWER_BEHIND;
    case StrikeKnob::COUNT:
      break;
  }
  return 0.0F;
}

/** A knob's default for a kind of strike (none of them tuned yet). */
constexpr float strikeKnobDefault(StrikeKind kind, StrikeKnob knob)
{
  using S = MatchTuning::Strike;
  switch (kind)
  {
    case StrikeKind::GROUND_PASS:
      return strikeProfileDefault<S::GroundPass>(knob);
    case StrikeKind::LOFTED_PASS:
      return strikeProfileDefault<S::Lofted>(knob);
    case StrikeKind::CROSS:
      return strikeProfileDefault<S::Cross>(knob);
    case StrikeKind::SHOT:
      return strikeProfileDefault<S::Shot>(knob);
    case StrikeKind::HEADER:
      return strikeProfileDefault<S::Header>(knob);
    case StrikeKind::CLEARANCE:
      return strikeProfileDefault<S::Clearance>(knob);
    case StrikeKind::THROW:
      return strikeProfileDefault<S::Throw>(knob);
    case StrikeKind::DRIVEN_PASS:
      return strikeProfileDefault<S::DrivenPass>(knob);
    case StrikeKind::CHIP:
      return strikeProfileDefault<S::Chip>(knob);
    case StrikeKind::PLACED_SHOT:
      return strikeProfileDefault<S::PlacedShot>(knob);
    case StrikeKind::CURLED_SHOT:
      return strikeProfileDefault<S::CurledShot>(knob);
    case StrikeKind::CHIP_SHOT:
      return strikeProfileDefault<S::ChipShot>(knob);
    case StrikeKind::TOE_POKE:
      return strikeProfileDefault<S::ToePoke>(knob);
  }
  return 0.0F;
}

/** A knob's full name: "<kind>: <name>". */
inline std::string strikeKnobName(StrikeKind kind, StrikeKnob knob)
{
  return std::string(STRIKE_KIND_NAMES[static_cast<std::size_t>(kind)]) + ": " +
         std::string(strikeKnobInfo(knob).name);
}

/** The knob values one engine plays with, by kind of strike. */
struct StrikeKnobs
{
  std::array<std::array<float, STRIKE_KNOB_COUNT>, STRIKE_KIND_COUNT> values =
      []
  {
    std::array<std::array<float, STRIKE_KNOB_COUNT>, STRIKE_KIND_COUNT>
        defaults{};
    for (std::size_t kind = 0; kind < STRIKE_KIND_COUNT; ++kind)
    {
      for (std::size_t knob = 0; knob < STRIKE_KNOB_COUNT; ++knob)
      {
        defaults[kind][knob] = strikeKnobDefault(static_cast<StrikeKind>(kind),
                                                 static_cast<StrikeKnob>(knob));
      }
    }
    return defaults;
  }();

  float at(StrikeKind kind, StrikeKnob knob) const
  {
    return values[static_cast<std::size_t>(kind)]
                 [static_cast<std::size_t>(knob)];
  }
  float& at(StrikeKind kind, StrikeKnob knob)
  {
    return values[static_cast<std::size_t>(kind)]
                 [static_cast<std::size_t>(knob)];
  }
  /** The value behind a full name ("Ground pass: Angle fail base"), if it
   * names a knob. */
  float* find(std::string_view fullName)
  {
    for (std::size_t kind = 0; kind < STRIKE_KIND_COUNT; ++kind)
    {
      for (std::size_t knob = 0; knob < STRIKE_KNOB_COUNT; ++knob)
      {
        if (strikeKnobName(static_cast<StrikeKind>(kind),
                           static_cast<StrikeKnob>(knob)) == fullName)
        {
          return &values[kind][knob];
        }
      }
    }
    return nullptr;
  }
};
