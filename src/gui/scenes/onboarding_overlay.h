// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "gui/scenes/management_scene.h"
#include "model/next_action.h"
#include "model/onboarding.h"

class GameController;
class GUIView;

/**
 * @brief Guidance pieces shared by the management screens: first-visit
 * screen tips, the first-week checklist and the "Next steps" card.
 */
namespace GuidanceUI
{
/** Checklist step a section completes when it is opened. */
std::optional<OnboardingTask> taskForSection(NavSection section);

/** Section where a checklist step is done. */
NavSection sectionForTask(OnboardingTask task);

/** Ticks the checklist step of a section the manager just opened. */
void noteVisit(GameController& controller, NavSection section);

/** Opens the screen (or player) where an action is handled. */
void openAction(GUIView* view, const NextAction& action);

/** Localised text of an analysis/report line. */
std::string text(const AnalysisLine& line);

/**
 * One-line tip at the top of a screen on its first visit. It stays for the
 * rest of that visit and never comes back once seen; "Got it" hides it and
 * "Turn off tips" disables tips (the Settings screen brings them back).
 */
void renderScreenTip(NavSection section);

/**
 * Notice after the manager took a delegated duty back by changing it by
 * hand, with Undo (hand it back) and OK.
 */
void renderReclaimNotice(const GUIView* view);

/** Language key of a section's tip (nullptr when it has none). */
const char* tipKey(NavSection section);
}  // namespace GuidanceUI

/**
 * @brief Home card: first-week checklist (while visible) and the top next
 * steps with their reasons and a button to act on each.
 */
class NextStepsCard
{
 public:
  /** Rebuilds the rows from the game state (call from refresh()). */
  void refresh(const GameController& controller);
  /** Draws the card across @p width (auto height, never scrolls). */
  void render(GUIView* view, float width);

  /** Actions shown on Home. */
  static constexpr std::size_t MAX_ROWS = 3;

 private:
  struct Row
  {
    NextAction action;
    std::string title;
    std::string reason;
  };
  std::vector<Row> rows;
  OnboardingState checklist;
  bool refreshed = false;
};
