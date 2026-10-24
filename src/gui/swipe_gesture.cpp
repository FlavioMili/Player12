// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/swipe_gesture.h"

#include <algorithm>
#include <cmath>

bool SwipeGesture::expired(std::uint64_t now_ns) const
{
  using enum Phase;
  return phase != IDLE && now_ns > last_ns && now_ns - last_ns > tuning.idle_ns;
}

SwipeGesture::Step SwipeGesture::feed(float dx, float dy,
                                      std::uint64_t timestamp_ns)
{
  using enum Phase;
  using enum Step;
  if (expired(timestamp_ns)) reset();
  if (phase == IDLE) phase = UNDECIDED;
  last_ns = std::max(last_ns, timestamp_ns);
  if (phase == IGNORED || phase == DONE) return NONE;

  travel_x += dx;
  travel_y += dy;
  const float sideways = std::fabs(travel_x);
  const float vertical = std::fabs(travel_y);
  if (phase == UNDECIDED)
  {
    if (std::max(sideways, vertical) < tuning.slop) return NONE;
    phase = sideways >= vertical * tuning.dominance ? SIDEWAYS : IGNORED;
    if (phase == IGNORED) return NONE;
  }
  // A swipe that turns into a vertical scroll is a scroll after all.
  if (vertical * tuning.dominance > sideways)
  {
    phase = IGNORED;
    return NONE;
  }
  if (sideways < tuning.threshold) return NONE;
  phase = DONE;
  return travel_x > 0.0f ? BACK : FORWARD;
}

void SwipeGesture::suppress(std::uint64_t timestamp_ns)
{
  using enum Phase;
  if (expired(timestamp_ns)) reset();
  phase = IGNORED;
  last_ns = std::max(last_ns, timestamp_ns);
}

void SwipeGesture::reset()
{
  using enum Phase;
  phase = IDLE;
  travel_x = 0.0f;
  travel_y = 0.0f;
  last_ns = 0;
}

float SwipeGesture::progress(std::uint64_t now_ns) const
{
  using enum Phase;
  if (expired(now_ns)) return 0.0f;
  switch (phase)
  {
    case SIDEWAYS:
      return std::clamp(travel_x / tuning.threshold, -1.0f, 1.0f);
    case DONE:
      return travel_x > 0.0f ? 1.0f : -1.0f;
    case IDLE:
    case UNDECIDED:
    case IGNORED:
      return 0.0f;
  }
  return 0.0f;
}
