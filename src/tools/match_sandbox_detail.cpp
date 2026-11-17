// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_detail.h"

#include "tools/match_sandbox_recorder.h"
#include "tools/match_sandbox_rewind.h"

namespace
{
/** A stretch longer than this is rebuilt from a later saved copy. */
constexpr std::uint64_t MAX_STRETCH_TICKS = 3 * SandboxRecorder::KEYFRAME_TICKS;
}  // namespace

void DetailRecorder::onDecisionDetail(const MatchEngine& engine,
                                      const MatchDecisionDetail& detail)
{
  decisions.push_back({.tick = engine.getSimulatedSteps(),
                       .minute = engine.getMatchTimeMinutes(),
                       .detail = detail});
}

void DetailRecorder::onRankingDetail(const MatchEngine& engine,
                                     const MatchRankingDetail& detail)
{
  if (!keepRankings)
  {
    return;
  }
  rankings.push_back({.tick = engine.getSimulatedSteps(),
                      .minute = engine.getMatchTimeMinutes(),
                      .detail = detail});
}

void DetailRecorder::onDuelDetail(const MatchEngine& engine,
                                  const MatchDuelDetail& detail)
{
  if (!keepDuels)
  {
    return;
  }
  duels.push_back({.tick = engine.getSimulatedSteps(),
                   .minute = engine.getMatchTimeMinutes(),
                   .detail = detail});
}

void DetailRecorder::clear()
{
  decisions.clear();
  rankings.clear();
  duels.clear();
}

void DetailSession::reset()
{
  engine.reset();
  records.clear();
  start = 0;
  diverged.reset();
}

void DetailSession::ensure(const SandboxRecorder& recording, std::uint64_t tick)
{
  if (engine && engine->getSimulatedSteps() == tick)
  {
    return;
  }
  // Moving forward within a short stretch continues it.
  if (engine && engine->getSimulatedSteps() < tick &&
      tick - start <= MAX_STRETCH_TICKS)
  {
    if (const auto at = MatchRewind::stepForward(
            recording, *engine, tick - engine->getSimulatedSteps());
        at && !diverged)
    {
      diverged = at;
    }
    return;
  }
  // Otherwise rebuild from the saved copy before the tick, recording.
  records.clear();
  diverged.reset();
  // At least a minute of play before the moment.
  MatchRewind::Result rebuilt = MatchRewind::rebuild(
      recording, tick, &records, SandboxRecorder::KEYFRAME_TICKS);
  engine = std::move(rebuilt.engine);
  diverged = rebuilt.divergedAt;
  start = rebuilt.startTick + 1;
}
