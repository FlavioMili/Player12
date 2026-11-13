// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Movement jitter: measuring how much players' plans and runs wobble from
// step to step (MovementJitterStats), for the sandbox and the lab. Observes
// only; nothing here changes the match.

#include <algorithm>
#include <cmath>

#include "model/match_engine.h"
#include "model/match_engine_internal.h"

using namespace MatchEngineInternal;

void MatchEngine::observePlanJitter(bool scheduled)
{
  // A move under half a metre is noise-level; a flip back within a second
  // (five refreshes) counts as undoing the switch.
  constexpr float MOVE_METRES = 0.5f;
  constexpr std::uint32_t FLIP_BACK_REFRESHES = 5;
  for (std::size_t slot = 0; slot < players.size() && slot < jitterTracks.size();
       ++slot)
  {
    const MatchPlayer& player = players[slot];
    JitterTrack& track = jitterTracks[slot];
    if (!active(player) || player.isGoalkeeper)
    {
      track.planned = false;
      continue;
    }
    if (!track.planned || track.player != player.player)
    {
      track.player = player.player;
      track.planned = true;
      track.lastTarget = player.tacticalTarget;
      track.lastMove = {0.0f, 0.0f};
      track.intent = player.intent;
      track.intentBefore = player.intent;
      track.refreshesSinceSwitch = FLIP_BACK_REFRESHES + 1;
      continue;
    }
    MovementJitterStats& side = movementJitter[player.isHomeTeam ? 0 : 1];
    const Vector2F move = player.tacticalTarget - track.lastTarget;
    const float moved = length(move);
    const bool switched = player.intent != track.intent;
    ++track.refreshesSinceSwitch;
    if (scheduled)
    {
      MovementJitterStats::ByIntent& bucket =
          side.byIntent[static_cast<std::size_t>(player.intent)];
      ++bucket.refreshes;
      bucket.targetMoveMetres += moved;
      if (moved > MOVE_METRES)
      {
        ++bucket.moves;
        if (length(track.lastMove) > MOVE_METRES &&
            move.x * track.lastMove.x + move.y * track.lastMove.y < 0.0f)
          ++bucket.reversals;
      }
      if (switched)
      {
        ++bucket.switchesIn;
        if (player.intent == track.intentBefore &&
            track.refreshesSinceSwitch <= FLIP_BACK_REFRESHES)
          ++bucket.flipBacks;
      }
    }
    else
    {
      ++side.eventRefreshes;
      side.eventTargetMoveMetres += moved;
    }
    if (switched)
    {
      track.intentBefore = track.intent;
      track.intent = player.intent;
      track.refreshesSinceSwitch = 0;
    }
    track.lastMove = move;
    track.lastTarget = player.tacticalTarget;
  }
}

void MatchEngine::observeRunJitter(float dt)
{
  // Runs are judged over 2 s windows; turns every 0.2 s above 1 m/s.
  constexpr std::uint64_t WINDOW_TICKS = 20;
  constexpr float RUNNING_SPEED = 1.0f;
  const bool sample = periodElapsed(MatchTuning::Timing::TACTICAL_REFRESH_STEPS);
  const bool windowEnds = periodElapsed(WINDOW_TICKS);
  for (std::size_t slot = 0; slot < players.size() && slot < jitterTracks.size();
       ++slot)
  {
    const MatchPlayer& player = players[slot];
    JitterTrack& track = jitterTracks[slot];
    if (!active(player) || player.isGoalkeeper)
    {
      track.moving = false;
      continue;
    }
    if (!track.moving || track.player != player.player)
    {
      track.player = player.player;
      track.moving = true;
      track.lastPosition = player.position;
      track.windowStart = player.position;
      track.windowPath = 0.0;
      track.lastVelocity = player.velocity;
      track.lastSpeed = length(player.velocity);
      track.speedTrend = 0;
      continue;
    }
    MovementJitterStats& side = movementJitter[player.isHomeTeam ? 0 : 1];
    side.playerSeconds += dt;
    // Speed surging: rising then falling (or back) while running.
    constexpr float SURGE_SPEED = 2.0f;
    constexpr float SURGE_ACCELERATION = 0.5f;
    const float speedNow = length(player.velocity);
    const float speedChange = speedNow - track.lastSpeed;
    track.lastSpeed = speedNow;
    if (speedNow > SURGE_SPEED)
    {
      auto& job = side.byIntent[static_cast<std::size_t>(player.intent)];
      side.runningSeconds += dt;
      job.runningSeconds += dt;
      if (std::abs(speedChange) > SURGE_ACCELERATION * dt)
      {
        const std::int8_t trend = speedChange > 0.0f ? 1 : -1;
        if (track.speedTrend != 0 && trend != track.speedTrend)
        {
          ++side.speedReversals;
          ++job.speedReversals;
        }
        track.speedTrend = trend;
      }
    }
    else
    {
      track.speedTrend = 0;
    }
    track.windowPath += length(player.position - track.lastPosition);
    track.lastPosition = player.position;
    if (windowEnds)
    {
      side.pathMetres += track.windowPath;
      side.netMetres += length(player.position - track.windowStart);
      track.windowStart = player.position;
      track.windowPath = 0.0;
    }
    if (sample)
    {
      const Vector2F before = track.lastVelocity;
      const Vector2F now = player.velocity;
      if (length(before) > RUNNING_SPEED && length(now) > RUNNING_SPEED)
      {
        ++side.turnSamples;
        if (before.x * now.x + before.y * now.y < 0.0f) ++side.sharpTurns;
      }
      track.lastVelocity = now;
    }
  }
}
