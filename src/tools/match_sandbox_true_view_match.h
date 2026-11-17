// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <array>
#include <utility>
#include <vector>

#include "global/types.h"
#include "tools/match_sandbox_true_view_shapes.h"

class IMatchRenderer;
class MatchEngine;
struct MatchRenderSnapshot;

/**
 * The match sandbox's true view of a match (Notes and plans/
 * match_engine_true_view_plan.md): the match screen's pitch in 3D with no
 * figures, every player drawn as the engine models him. Every player shows
 * his body cylinder and feet reach; two picked players (A and B) show
 * everything, so their reaches can be seen meeting on a replay. Its
 * settings sit in the match's side panel.
 */
class MatchTrueView
{
 public:
  /** The settings section: the picked players, the technique, the layers,
   * the notes and A's shapes. `engine` lists the players. */
  void renderSettings(const MatchEngine& engine);

  /**
   * Draws every player over the pitch `renderer` just drew from `snapshot`
   * (of `engine`), on the current window's draw list. A click picks A
   * (Ctrl: B); returns the player a click picked (0: none).
   */
  PlayerID drawOverlay(const IMatchRenderer& renderer,
                       const MatchEngine& engine,
                       const MatchRenderSnapshot& snapshot, bool clicked,
                       bool ctrl, ImVec2 pixel);

  /** The two players drawn in full (0: none). */
  std::array<PlayerID, 2> picked{};

 private:
  void renderPickedPlayers(const MatchEngine& engine);
  void renderPlayerPicker(const MatchEngine& engine, std::size_t slot);
  /** The player nearest a pixel on screen (0: none within reach). */
  [[nodiscard]] PlayerID playerAt(ImVec2 pixel) const;

  TrueView::Layers layers{.labels = false};
  TrueView::Technique technique = TrueView::Technique::ALL;
  int strikeKind = 0;
  /** Where each player's centre was on screen this frame (for picking). */
  std::vector<std::pair<PlayerID, ImVec2>> onScreen;
  /** A's shapes, for the table. */
  std::vector<TrueView::Entry> entries;
};
