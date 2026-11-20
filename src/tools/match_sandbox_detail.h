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
#include <vector>

#include "model/match_engine.h"
#include "model/match_recorder.h"

class SandboxRecorder;

/**
 * Full-detail records of a stretch of a match (debugger phase 4, "Tier B"):
 * every decision with its scores broken down, the planner's rankings and
 * the duels with their rolls. Kept only for the stretch being inspected.
 */
class DetailRecorder : public MatchRecorder
{
 public:
  template <typename T>
  struct At
  {
    std::uint64_t tick = 0;
    float minute = 0.0F;
    T detail;
  };

  [[nodiscard]] bool wantsDetail() const override { return true; }
  void onDecisionDetail(const MatchEngine& engine,
                        const MatchDecisionDetail& detail) override;
  void onRankingDetail(const MatchEngine& engine,
                       const MatchRankingDetail& detail) override;
  void onDuelDetail(const MatchEngine& engine,
                    const MatchDuelDetail& detail) override;

  void clear();

  /** Rankings and duels are many; a whole-match capture leaves them out. */
  bool keepRankings = true;
  bool keepDuels = true;

  std::vector<At<MatchDecisionDetail>> decisions;
  std::vector<At<MatchRankingDetail>> rankings;
  std::vector<At<MatchDuelDetail>> duels;
};

/**
 * Rebuilds the stretch around the moment inspected with a DetailRecorder
 * attached: from the saved copy before it up to it. Moving the moment
 * forward a little continues the same stretch; anything else rebuilds.
 */
class DetailSession
{
 public:
  /** Detail up to the end of `tick` (rebuilding only when needed). */
  void ensure(const SandboxRecorder& recording, std::uint64_t tick);
  void reset();

  [[nodiscard]] const DetailRecorder& detail() const { return records; }
  /** First tick with detail (just after the saved copy rebuilt from). */
  [[nodiscard]] std::uint64_t from() const { return start; }
  /** First tick where the rebuild stopped reproducing the match, if any. */
  [[nodiscard]] std::optional<std::uint64_t> divergedAt() const
  {
    return diverged;
  }

 private:
  std::unique_ptr<MatchEngine> engine;
  DetailRecorder records;
  std::uint64_t start = 0;
  std::optional<std::uint64_t> diverged;
};
