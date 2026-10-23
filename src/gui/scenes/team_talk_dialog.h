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
#include <string>
#include <vector>

#include "global/types.h"
#include "model/interactions.h"

class GameController;
class MatchEngine;

/**
 * @brief Modal pre-match / half-time team talk of the managed club.
 *
 * Self-contained: keep one instance per match screen. Either call open() and
 * render() yourself, or let renderForMatch() drive it from the live engine:
 * it opens the pre-match talk at kick-off and the half-time talk at the
 * break.
 *
 * Each tone shows its predicted reception in this situation (score, stakes,
 * the players' personalities); the result lists how the XI took it. Besides
 * a few morale points, the talk's outcome (GameController::getTeamTalkModifier)
 * reaches the live match: sharper (or shakier) decisions, execution and
 * pressing for the first minutes of the half (MatchEngine::getTeamTalkEffect).
 */
class TeamTalkDialog
{
 public:
  /** Opens the talk for @p moment (the score matters at half-time). */
  void open(const GameController& controller, TeamTalkMoment moment,
            int own_goals = 0, int other_goals = 0);

  /**
   * Draws the dialog while open.
   * @return True on the frame the talk was given.
   */
  bool render(GameController& controller);

  /**
   * One call per frame from a live match screen: opens the pre-match talk
   * at kick-off and the half-time talk at the break, and withdraws an
   * unanswered talk (without effect) once its moment has passed or the
   * match is over. The match keeps running meanwhile. Talks given (here or
   * before kick-off) are passed on to the engine of the managed side.
   */
  void renderForMatch(GameController& controller, MatchEngine& engine,
                      TeamID home_id, TeamID away_id);

  [[nodiscard]] bool isOpen() const { return visible; }

 private:
  struct ToneRow
  {
    TeamTalkTone tone = TeamTalkTone::Calm;
    std::string label;
    std::string description;
    std::string hint;
    ImVec4 hint_color;
    float positive = 0.0f;
    float negative = 0.0f;
  };

  void renderOptions(GameController& controller);
  void renderResult() const;

  TeamTalkMoment moment = TeamTalkMoment::PreMatch;
  int own_goals = 0;
  int other_goals = 0;
  bool open_requested = false;
  bool dismiss_requested = false;
  bool visible = false;
  bool has_result = false;

  // Match driving (renderForMatch).
  bool pre_offered = false;
  bool half_offered = false;
  /** Talk modifiers by half already passed to the engine. */
  std::array<float, 2> applied_talk{};

  std::string title;
  std::string situation;
  std::vector<std::string> tags;
  std::string mood;
  std::string leaders;
  std::vector<ToneRow> rows;

  std::string result_summary;
  std::string result_counts;
  std::string result_morale;
  ImVec4 result_color;
};
