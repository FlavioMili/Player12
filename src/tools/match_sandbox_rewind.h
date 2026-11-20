// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <memory>
#include <optional>

class MatchEngine;
class MatchRecorder;
class SandboxRecorder;

/**
 * Rebuilds a recorded match at any tick (debugger phase 2): a copy of the
 * nearest saved state before it, continued with the match's logs and
 * simulated forward. Every rebuilt tick is checked against the fingerprint
 * recorded when the match was played, so a rebuild that does not reproduce
 * the match is reported instead of silently shown.
 */
class MatchRewind
{
 public:
  struct Result
  {
    /** The match at the end of the requested tick (or the nearest one
     * recorded); null when nothing was recorded yet. */
    std::unique_ptr<MatchEngine> engine;
    /** First tick whose state differed from the recording, if any. */
    std::optional<std::uint64_t> divergedAt;
    /** Tick of the saved copy the rebuild started from. */
    std::uint64_t startTick = 0;
  };

  /**
   * @param attach Recorder attached while re-simulating (full detail of the
   * stretch rebuilt); may be null.
   * @param history Start from a saved copy at least this many ticks before
   * `tick` (when there is one), so the stretch covers that much play.
   */
  [[nodiscard]] static Result rebuild(const SandboxRecorder& recording,
                                      std::uint64_t tick,
                                      MatchRecorder* attach = nullptr,
                                      std::uint64_t history = 0);

  /**
   * Moves a rebuilt match on by `ticks`, checking each against the
   * recording. Returns the first differing tick, if any.
   */
  static std::optional<std::uint64_t> stepForward(
      const SandboxRecorder& recording, MatchEngine& engine,
      std::uint64_t ticks);
};
