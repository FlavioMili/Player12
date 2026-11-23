// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_tuning.h"

#include <cmath>
#include <format>

namespace
{
/** Smaller differences are rounding, not a changed value. */
constexpr float SAME_VALUE_TOLERANCE = 1e-6F;

bool differs(float a, float b)
{
  return std::abs(a - b) > SAME_VALUE_TOLERANCE;
}
}  // namespace

void TechniqueTuning::applyTo(MatchEngine& engine) const
{
  engine.setStrikeKnobs(strike);
  engine.setFirstTouchKnobs(touch);
  engine.setStretchMap(stretch);
}

std::vector<std::string> TechniqueTuning::changes() const
{
  std::vector<std::string> lines;
  const StrikeKnobs strikeDefaults;
  for (std::size_t kind = 0; kind < STRIKE_KIND_COUNT; ++kind)
  {
    for (std::size_t knob = 0; knob < STRIKE_KNOB_COUNT; ++knob)
    {
      const float was = strikeDefaults.values.at(kind).at(knob);
      const float now = strike.values.at(kind).at(knob);
      if (differs(was, now))
      {
        lines.push_back(
            std::format("{} {:g} -> {:g}",
                        strikeKnobName(static_cast<StrikeKind>(kind),
                                       static_cast<StrikeKnob>(knob)),
                        was, now));
      }
    }
  }
  const FirstTouchKnobs touchDefaults;
  for (std::size_t knob = 0; knob < TOUCH_KNOB_COUNT; ++knob)
  {
    const float was = touchDefaults.values.at(knob);
    const float now = touch.values.at(knob);
    if (differs(was, now))
    {
      lines.push_back(std::format("Touch: {} {:g} -> {:g}",
                                  TOUCH_KNOBS.at(knob).name, was, now));
    }
  }
  const StretchMap stretchDefaults;
  for (std::size_t row = 0; row < StretchMap::ANGLES; ++row)
  {
    if (differs(stretch.angles.at(row), stretchDefaults.angles.at(row)))
    {
      lines.push_back(std::format("Stretch map: angle {} {:g} -> {:g}", row + 1,
                                  stretchDefaults.angles.at(row),
                                  stretch.angles.at(row)));
    }
  }
  for (std::size_t column = 0; column < StretchMap::DISTANCES; ++column)
  {
    if (differs(stretch.distances.at(column),
                stretchDefaults.distances.at(column)))
    {
      lines.push_back(std::format(
          "Stretch map: distance {} {:g} -> {:g}", column + 1,
          stretchDefaults.distances.at(column), stretch.distances.at(column)));
    }
  }
  for (std::size_t row = 0; row < StretchMap::ANGLES; ++row)
  {
    for (std::size_t column = 0; column < StretchMap::DISTANCES; ++column)
    {
      if (differs(stretch.values.at(row).at(column),
                  stretchDefaults.values.at(row).at(column)))
      {
        lines.push_back(
            std::format("Stretch map: {:g} deg, {:g} of reach {:g} -> {:g}",
                        stretch.angles.at(row), stretch.distances.at(column),
                        stretchDefaults.values.at(row).at(column),
                        stretch.values.at(row).at(column)));
      }
    }
  }
  return lines;
}

std::string TechniqueTuning::changesText() const
{
  std::string text;
  for (const std::string& line : changes())
  {
    text += line + "\n";
  }
  return text.empty() ? std::string("The game's values (nothing changed).\n")
                      : text;
}

TechniqueTuning& sandboxTuning()
{
  static TechniqueTuning tuning;
  return tuning;
}
