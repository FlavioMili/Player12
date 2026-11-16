// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/view_models/match_clock.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

#include "model/match_report.h"

namespace
{
/** Clock minute at which a period's regulation time ends (45, 90, then
 * 105 and 120 in extra time). */
int regulationEndOf(int period)
{
  constexpr std::array<int, 4> ENDS{45, 90, 105, 120};
  return ENDS[static_cast<std::size_t>(std::clamp(period, 1, 4) - 1)];
}
}  // namespace

std::string MatchClock::minuteLabel(float matchMinutes, int period,
                                    bool addedTime)
{
  const int regulationEnd = regulationEndOf(period);
  if (addedTime)
  {
    const int added = std::max(
        1, static_cast<int>(
               std::floor(matchMinutes - static_cast<float>(regulationEnd))) +
               1);
    return std::format("{}+{}'", regulationEnd, added);
  }
  const int minute =
      matchMinutes <= 0.0f
          ? 0
          : std::min(regulationEnd,
                     static_cast<int>(std::floor(matchMinutes)) + 1);
  return std::format("{}'", minute);
}

std::string MatchClock::clockLabel(float matchMinutes, int period,
                                   bool addedTime)
{
  const int regulationEnd = regulationEndOf(period);
  const float shown = std::max(
      0.0f, addedTime ? matchMinutes - static_cast<float>(regulationEnd)
                      : matchMinutes);
  const auto totalSeconds = static_cast<int>(std::floor(shown * 60.0f));
  if (addedTime)
    return std::format("{}+{}:{:02}", regulationEnd, totalSeconds / 60,
                       totalSeconds % 60);
  return std::format("{:02}:{:02}", totalSeconds / 60, totalSeconds % 60);
}

std::string MatchClock::minuteLabel(const MatchReportEvent& event)
{
  // The event stores the whole minutes elapsed on the clock and, in added
  // time, the whole minutes beyond the regulation end.
  if (event.added_minute > 0)
    return std::format("{}+{}'", event.minute - event.added_minute,
                       event.added_minute + 1);
  return std::format("{}'", event.minute + 1);
}
