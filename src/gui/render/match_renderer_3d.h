// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <memory>

#include "gui/render/imatch_renderer.h"

/**
 * Stylised 3D broadcast renderer.
 *
 * Projects a metric world through a perspective camera on the CPU and draws
 * the result as ImDrawList triangles, so it shares the SDL_Renderer/ImGui
 * path with the 2D view. Static geometry (pitch, markings, stands, crowd,
 * boards, goals) is built once and only re-projected; players, ball and goal
 * parts are depth-sorted back to front each frame (painter's algorithm) with
 * near-plane clipping for ground polygons. Consumes only the read-only
 * snapshot and never touches the simulation.
 */
class MatchRenderer3D final : public IMatchRenderer
{
 public:
  MatchRenderer3D();
  ~MatchRenderer3D() override;
  MatchRenderer3D(const MatchRenderer3D&) = delete;
  MatchRenderer3D& operator=(const MatchRenderer3D&) = delete;
  MatchRenderer3D(MatchRenderer3D&&) = delete;
  MatchRenderer3D& operator=(MatchRenderer3D&&) = delete;

  void render(const MatchRenderSnapshot& snapshot,
              const MatchRenderOptions& options,
              const MatchViewport& viewport) override;
  bool projectPitch(PitchPoint pitch, float heightMetres, float& screenX,
                    float& screenY) const override;

 private:
  struct State;
  std::unique_ptr<State> state;
};
