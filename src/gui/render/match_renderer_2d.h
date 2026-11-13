// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <memory>

#include "gui/render/imatch_renderer.h"
#include "gui/render/match_kit_colors.h"
#include "gui/scenes/match_scene_tuning.h"

/**
 * ImGui/SDL-agnostic 2D tactical match renderer.
 *
 * Draws the stadium surround, a lit and mown pitch with its markings and
 * goals, top-down kit tokens (shirt, shorts ring, number, facing wedge), the
 * ball with its shadow and trail, brief pass and shot paths, offside flashes
 * and an optional pitch-control overlay into the current ImGui draw list.
 * Consumes only the read-only snapshot and never touches the simulation;
 * everything is interpolated with `snapshot.interpolationAlpha`.
 */
class MatchRenderer2D final : public IMatchRenderer
{
 public:
  MatchRenderer2D();
  ~MatchRenderer2D() override;
  MatchRenderer2D(const MatchRenderer2D&) = delete;
  MatchRenderer2D& operator=(const MatchRenderer2D&) = delete;
  MatchRenderer2D(MatchRenderer2D&&) = delete;
  MatchRenderer2D& operator=(MatchRenderer2D&&) = delete;

  void render(const MatchRenderSnapshot& snapshot,
              const MatchRenderOptions& options,
              const MatchViewport& viewport) override;
  bool projectPitch(PitchPoint pitch, float heightMetres, float& screenX,
                    float& screenY) const override;

 private:
  struct State;
  std::unique_ptr<State> state;
};

/**
 * Fits the pitch into the available screen space at its real 105 x 68 m
 * proportions (never stretched); the caller centres the result.
 */
MatchViewport computeMatchViewport(float topLeftX, float topLeftY,
                                   float availableWidth, float availableHeight);

/** How far the stadium drawn round a 2D pitch (run-off, boards, the gap to
 * the stand and the rows of crowd shown) reaches past the goal lines (x)
 * and the touchlines (y), in pixels. */
ImVec2 stadiumApron2D(const MatchViewport& viewport);

/** The 2D pitch fitted, with its stadium round it, into the available area
 * and centred in it. */
MatchViewport computeMatchViewportWithStadium(float topLeftX, float topLeftY,
                                              float availableWidth,
                                              float availableHeight);

/** Short English description of a player's current intent (tooltips). */
const char* playerIntentLabel(PlayerIntent intent);

/** Goal flash and banner centred on the viewport; shared by both views. */
void drawGoalCelebration(ImDrawList& drawList, const MatchViewport& viewport,
                         int homeScore, int awayScore,
                         float celebrationRemaining, float celebrationDuration);
