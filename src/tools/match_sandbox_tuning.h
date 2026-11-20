// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <string>
#include <vector>

#include "model/match_engine.h"
#include "model/match_first_touch_knobs.h"
#include "model/match_strike_knobs.h"

/**
 * The technique tuning the sandbox's contact view edits and the technique
 * bench runs with (Notes and plans/match_engine_technique_bench_plan.md §5):
 * every strike knob, every touch knob and the stretch map. Starts at the
 * game's values.
 */
struct TechniqueTuning
{
  StrikeKnobs strike;
  FirstTouchKnobs touch;
  StretchMap stretch;

  /** Sets it on an engine (after the drill has set the run up). */
  void applyTo(MatchEngine& engine) const;
  /** What differs from the game's values, one line each ("Ground pass:
   * Angle fail base 0.05 -> 0.08"); empty when nothing does. */
  [[nodiscard]] std::vector<std::string> changes() const;
  /** The changes as text to copy into match_tuning.h by hand. */
  [[nodiscard]] std::string changesText() const;
  [[nodiscard]] bool isDefault() const { return changes().empty(); }
};

/** The sandbox session's tuning, shared by the contact view and the bench. */
TechniqueTuning& sandboxTuning();
