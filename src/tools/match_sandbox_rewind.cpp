// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_rewind.h"

#include <vector>

#include "model/match_engine.h"
#include "tools/match_sandbox_recorder.h"

MatchRewind::Result MatchRewind::rebuild(const SandboxRecorder& recording,
                                         std::uint64_t tick,
                                         MatchRecorder* attach,
                                         std::uint64_t history)
{
  std::shared_ptr<const MatchEngine> start;
  std::vector<MatchCommandRecord> commands;
  std::vector<MatchInputRecord> inputs;
  std::uint64_t lastTick = 0;
  {
    const auto guard = recording.lock();
    const auto& keyframes = recording.keyframes();
    if (keyframes.empty())
    {
      return {};
    }
    // The latest saved copy at or before the tick (the first one for an
    // earlier tick: nothing before it was saved).
    start = keyframes.front().engine;
    const std::uint64_t latest = tick > history ? tick - history : 0;
    for (const auto& keyframe : keyframes)
    {
      if (keyframe.tick <= latest)
      {
        start = keyframe.engine;
      }
    }
    commands = recording.commands();
    inputs = recording.inputs();
    lastTick = recording.lastTick();
  }

  Result result;
  result.engine = std::make_unique<MatchEngine>(*start);
  result.engine->continueReplay(std::move(commands), std::move(inputs));
  result.startTick = result.engine->getSimulatedSteps();
  result.engine->setRecorder(attach);
  const std::uint64_t target = std::min(tick, lastTick);
  if (result.engine->getSimulatedSteps() < target)
  {
    result.divergedAt = stepForward(
        recording, *result.engine, target - result.engine->getSimulatedSteps());
  }
  return result;
}

std::optional<std::uint64_t> MatchRewind::stepForward(
    const SandboxRecorder& recording, MatchEngine& engine, std::uint64_t ticks)
{
  std::optional<std::uint64_t> diverged;
  for (std::uint64_t step = 0;
       step < ticks && engine.getState() != MatchState::FULL_TIME; ++step)
  {
    engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    if (diverged)
    {
      continue;
    }
    const std::uint64_t at = engine.getSimulatedSteps();
    std::optional<std::uint64_t> expected;
    {
      const auto guard = recording.lock();
      expected = recording.checksum(at);
    }
    if (expected && *expected != SandboxRecorder::stateChecksum(engine))
    {
      diverged = at;
    }
  }
  return diverged;
}
