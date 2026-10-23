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

#include "global/types.h"
#include "model/match_analysis.h"

class GameController;
class MatchEngine;

/**
 * @brief The assistant's half-time / full-time analysis of a live match:
 * where the chances came from, pressing, duels, standout and struggling
 * players, fatigue and two or three suggestions with their evidence.
 *
 * Self-driving like TeamTalkDialog: call renderForMatch() once per frame.
 * It opens at the break (after the team talk) and at full time, and
 * openNow() shows it on demand. It is a panel in the bottom-right corner,
 * never a modal: the match keeps running and its controls stay usable.
 */
class MatchAnalysisPanel
{
 public:
  void renderForMatch(const GameController& controller,
                      const MatchEngine& engine, TeamID home_id, TeamID away_id,
                      bool other_dialog_open);

  /** Opens the analysis of the match so far (the managed side only). */
  void openNow(const GameController& controller, const MatchEngine& engine,
               TeamID home_id, TeamID away_id);

  void close() { visible = false; }

  [[nodiscard]] bool isOpen() const { return visible; }

 private:
  struct CompareRow
  {
    std::string label;
    std::string own;
    std::string other;
  };

  void build(const GameController& controller, const MatchEngine& engine,
             bool managed_home);
  void render();

  bool visible = false;
  bool focus_requested = false;
  bool half_offered = false;
  bool full_offered = false;

  std::string title;
  std::string own_name;
  std::string other_name;
  std::vector<CompareRow> rows;
  std::vector<std::string> observations;
  std::vector<std::string> notes;
  struct SuggestionText
  {
    std::string action;
    std::string reason;
  };
  std::vector<SuggestionText> suggestions;
  std::string sample; /*!< Evidence the analysis rests on. */
};
