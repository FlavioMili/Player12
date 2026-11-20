// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <vector>

#include "global/types.h"
#include "gui/scenes/match_touchline.h"
#include "model/strategy.h"

/**
 * @brief Tactics dialog of the live match: style presets, the five
 * instructions and the formation, edited as a draft and applied together.
 *
 * The engine takes new instructions at once (the players re-form within a
 * few seconds); a new shape costs some familiarity for a few minutes, which
 * the dialog shows before applying. Every apply can be undone. In-match
 * changes never alter the club's saved tactics.
 */
class MatchTacticsPanel
{
 public:
  /** Window id kept stable for tests and for the Escape handling. */
  static constexpr const char* WINDOW_ID = "###match_tactics_modal";

  /** @brief Starts a match from the club's tactics (clears the history). */
  void reset(const Strategy& plan);

  /**
   * @brief Draws the dialog while it is shown.
   * @return False once it closed (Close, Escape or another dialog).
   */
  bool render(const TouchlineContext& context, bool embedded = false);

  /** @brief Next render() opens the popup again, with a fresh draft. */
  void resetPopup()
  {
    popup_opened = false;
    close_requested = false;
  }

  /** @brief Closes the dialog on its next frame. */
  void requestClose() { close_requested = true; }

  /** @brief Draft starts again from the tactics in force. */
  void syncDraft(const TouchlineContext& context);

  /** @brief Whether the draft differs from the tactics in force. */
  [[nodiscard]] bool hasChanges(const TouchlineContext& context) const;

  /** @brief Sends the draft to the engine; false when nothing changed. */
  bool apply(const TouchlineContext& context);

  /** @brief Restores the tactics in force before the last apply. */
  bool undo(const TouchlineContext& context);

  [[nodiscard]] bool canUndo() const { return !history.empty(); }
  [[nodiscard]] const Strategy& getApplied() const { return applied; }

  /** Edited instructions and outfield shape (lineup coordinates). */
  StrategySliders draft_sliders;
  std::vector<Vector2F> draft_shape;

 private:
  struct Snapshot
  {
    Strategy strategy;
    std::vector<Vector2F> shape;
  };

  void renderStyle();
  void renderInstructions();
  void renderFormation(const TouchlineContext& context);
  void renderFamiliarity(const TouchlineContext& context);
  void renderFooter(const TouchlineContext& context);

  Strategy applied;
  std::vector<Snapshot> history;
  bool popup_opened = false;
  bool close_requested = false;
};
