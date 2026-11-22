// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

class GameXgLegSweep;

/**
 * The leg sweep's scan view (game xG plan, layer 3), in the xG map tool:
 * the latest Shot block runs as they are measured. On the left, the whole
 * shot in its own frame (the shooter at the bottom, the goal at the top):
 * the aim line, the ball's actual path coloured by what came of it, the
 * defender as it was struck with his velocity as an arrow coloured by how
 * he moves, and a ghost where he'd have been as it passed him; the runs
 * before it fade behind. On the right, close in round the ghost: the
 * offset bands the run counts in, shaded by the chances measured so far,
 * and the gap; below, the table for the kind and rating being measured,
 * filling in.
 */
class LegScanView
{
 public:
  /** Draws into the rectangle at `origin` of `size` (pixels); `sweep` may
   * be null (the legend and a note only). */
  void draw(const GameXgLegSweep* sweep, ImVec2 origin, ImVec2 size,
            float scale);

 private:
  /** The overview's length shown (m), eased towards each run's. */
  float shownLength = 0.0F;
};
