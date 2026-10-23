// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "global/types.h"
#include "gui/scenes/match_touchline.h"
#include "gui/view_models/match_changes.h"

class Lineup;
class Player;

/**
 * @brief Substitutions dialog of the live match.
 *
 * The eleven stand on a small pitch in their formation slots with their
 * condition, cards and rating; the bench lists fitness, rating and how well
 * each substitute suits the selected position. Dragging a substitute onto a
 * player (or clicking one then the other) plans a change; several planned
 * changes are made together at the next stoppage, so they use one window.
 * Dragging one player onto another swaps their positions at once. The
 * assistant proposes changes that can be added with one click.
 */
class MatchSubsPanel
{
 public:
  /** Window id kept stable for tests and for the Escape handling. */
  static constexpr const char* WINDOW_ID = "###match_substitutions_modal";

  /**
   * @brief Draws the dialog while it is shown.
   * @return False once it closed (Close, Escape or another dialog).
   */
  bool render(const TouchlineContext& context);

  /** @brief Makes the confirmed plan once play stops (every frame). */
  void update(const TouchlineContext& context);

  /** @brief Next render() opens the popup again (after it was hidden). */
  void resetPopup() { popup_opened = false; }

  /** @brief Closes the dialog on its next frame. */
  void requestClose() { close_requested = true; }

  /** @brief Clears the selection and the dialog's own messages. */
  void resetSelection();

  /**
   * @brief Plans @p out -> @p in; the change is refused (with the reason in
   * the HUD status) when the rules or the plan do not allow it.
   */
  bool plan(const TouchlineContext& context, PlayerID out, PlayerID in);

  /**
   * @brief Makes one change right away (play stops for it, as the engine
   * rules); false with the reason in the HUD status when refused.
   */
  bool substituteNow(const TouchlineContext& context, PlayerID out,
                     PlayerID in);

  /**
   * @brief Confirms the plan: made at once when play is stopped, otherwise
   * at the next stoppage.
   */
  void confirm(const TouchlineContext& context);

  /** @brief Swaps the formation slots of two players on the pitch. */
  bool swapPositions(const TouchlineContext& context, PlayerID first,
                     PlayerID second);

  [[nodiscard]] const MatchChanges::SubstitutionPlan& getPlan() const
  {
    return substitution_plan;
  }
  MatchChanges::SubstitutionPlan& getPlan() { return substitution_plan; }

  /** Player chosen on the pitch / on the bench (0 when none). */
  PlayerID selected_out = 0;
  PlayerID selected_in = 0;

 private:
  /** A player of the eleven as drawn on the mini pitch. */
  struct PitchMarker
  {
    const Player* player = nullptr;
    PlayerID id = 0;
    Vector2F spot{};
    float condition = 1.0f;
    float rating = 0.0f;
    int yellow_cards = 0;
    bool injured = false;
    bool goalkeeper = false;
  };

  void collectMarkers(const TouchlineContext& context);
  void renderUsage(const TouchlineContext& context) const;
  void renderPitch(const TouchlineContext& context, float width);
  void renderBench(const TouchlineContext& context, const Lineup& lineup,
                   float width);
  void renderPlan(const TouchlineContext& context, const Lineup& lineup);
  void renderSuggestions(const TouchlineContext& context, const Lineup& lineup);
  void renderFooter(const TouchlineContext& context);
  /** Applies the outcomes of made changes to the HUD status. */
  void report(
      const TouchlineContext& context, const Lineup& lineup,
      const std::vector<MatchChanges::SubstitutionOutcome>& outcomes) const;
  /** Selection changed: plans the change once both players are chosen. */
  void selectionChanged(const TouchlineContext& context);

  MatchChanges::SubstitutionPlan substitution_plan;
  std::vector<PitchMarker> markers;
  /** The assistant's proposals and when they were last worked out. */
  std::vector<MatchChanges::Suggestion> suggestions;
  std::uint64_t suggestions_key = UINT64_MAX;
  std::size_t suggestions_changes = 0;
  bool popup_opened = false;
  bool close_requested = false;
  /** The next pitch click swaps with the selected player. */
  bool swap_mode = false;
};
