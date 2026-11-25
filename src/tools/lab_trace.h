// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <iosfwd>
#include <string_view>

class MatchEngine;

/**
 * Position traces (fm_lab trace, fm_lab drill --trace): every player and the
 * ball in metres, every fixed step, as CSV. Two builds' traces from the same
 * seed are compared tick by tick to find a change that should only round
 * differently (training ground plan §12, 2026-11-13).
 */
namespace Lab
{
/** The CSV header: run, tick, who, x, y, z (metres; z the ball's height). */
void writeTraceHeader(std::ostream& out);
/** One row per player (who = "p" and its index in getPlayers()) and one for
 * the ball, at `tick` (0 = before the first step). */
void writeTraceRows(std::ostream& out, std::string_view run, int tick,
                    const MatchEngine& engine);
}  // namespace Lab
