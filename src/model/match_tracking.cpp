// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/match_tracking.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "model/match_tuning.h"

namespace
{
namespace T = MatchTracking;

constexpr std::uint16_t COUNT_MAX = std::numeric_limits<std::uint16_t>::max();

void bump(std::uint16_t& counter)
{
  if (counter < COUNT_MAX) ++counter;
}

/** Engine frame to the side's attacking frame (both as fractions of the
 * pitch: x of the length, y of the width). */
Vector2F attackingFrame(Vector2F position, bool home)
{
  const float x = std::clamp(position.x, 0.0f, 1.0f);
  const float y = std::clamp(position.y, 0.0f, 1.0f);
  return home ? Vector2F{x, y} : Vector2F{1.0f - x, 1.0f - y};
}
}  // namespace

std::size_t MatchTracking::cellOf(float x, float y)
{
  const auto column = std::min(
      static_cast<std::size_t>(std::clamp(x, 0.0f, 1.0f) * GRID_COLUMNS),
      GRID_COLUMNS - 1);
  const auto row =
      std::min(static_cast<std::size_t>(std::clamp(y, 0.0f, 1.0f) * GRID_ROWS),
               GRID_ROWS - 1);
  return row * GRID_COLUMNS + column;
}

std::size_t MatchTracking::bucketOf(float minute, int period)
{
  // Regulation end of each period: 45, 90, 105, 120.
  const int clamped = std::clamp(period, 1, 4);
  const int end = clamped <= 2 ? 45 * clamped : 90 + 15 * (clamped - 2);
  const int last = end / BUCKET_MINUTES - 1;
  const int bucket = static_cast<int>(std::max(minute, 0.0f)) / BUCKET_MINUTES;
  return static_cast<std::size_t>(std::clamp(bucket, 0, last));
}

TrackedPlayer* MatchTracker::slot(std::size_t index)
{
  if (index >= T::MAX_PLAYERS) return nullptr;
  if (players.size() <= index)
  {
    if (players.capacity() == 0) players.reserve(32);
    players.resize(index + 1);
  }
  return &players[index];
}

const TrackedPlayer* MatchTracker::player(std::size_t index) const
{
  return index < players.size() ? &players[index] : nullptr;
}

std::uint16_t MatchTracker::passes(std::size_t from, std::size_t to) const
{
  if (pass_matrix.empty() || from >= T::MAX_PLAYERS || to >= T::MAX_PLAYERS)
    return 0;
  return pass_matrix[from * T::MAX_PLAYERS + to];
}

void MatchTracker::touch(std::size_t index, bool home, PitchPoint position,
                         const MatchPitch& pitch, float minute, int period)
{
  if (!enabled) return;
  TrackedPlayer* tracked = slot(index);
  if (tracked == nullptr) return;
  const Vector2F at = attackingFrame(pitch.fractions(position), home);
  bump(tracked->touches[T::cellOf(at.x, at.y)]);
  bump(tracked->touch_count);
  tracked->sum_x += at.x;
  tracked->sum_y += at.y;
  if (at.x >= 2.0f / 3.0f)
    bump(final_third[home ? 0 : 1][T::bucketOf(minute, period)]);
}

void MatchTracker::passReleased(PitchPoint origin, const MatchPitch& pitch,
                                bool open_play)
{
  if (!enabled) return;
  pass_origin = pitch.fractions(origin);
  pass_open_play = open_play;
}

void MatchTracker::passCompleted(std::size_t passer, std::size_t receiver,
                                 bool home, PitchPoint reception,
                                 const MatchPitch& pitch)
{
  if (!enabled || passer >= T::MAX_PLAYERS || receiver >= T::MAX_PLAYERS)
    return;
  if (pass_matrix.empty())
    pass_matrix.assign(T::MAX_PLAYERS * T::MAX_PLAYERS, 0);
  bump(pass_matrix[passer * T::MAX_PLAYERS + receiver]);
  TrackedPlayer* tracked = slot(passer);
  if (tracked == nullptr || !pass_open_play) return;
  const Vector2F from = attackingFrame(pass_origin, home);
  const Vector2F to = attackingFrame(pitch.fractions(reception), home);
  const float gained = (to.x - from.x) * pitch.length;
  if (gained >= T::PROGRESSIVE_METRES && to.x >= T::PROGRESSIVE_MIN_END_X)
    bump(tracked->progressive_passes);
}

void MatchTracker::shot(std::size_t creator, float xg, TrackedShot kind)
{
  if (!enabled) return;
  if (shot_kinds.capacity() == 0) shot_kinds.reserve(32);
  shot_kinds.push_back(kind);
  if (TrackedPlayer* tracked = slot(creator)) tracked->expected_assists += xg;
}

void MatchTracker::pressure(std::size_t index, double spell)
{
  if (!enabled) return;
  TrackedPlayer* tracked = slot(index);
  if (tracked == nullptr || tracked->pressure_spell == spell) return;
  tracked->pressure_spell = spell;
  bump(tracked->pressures);
}
