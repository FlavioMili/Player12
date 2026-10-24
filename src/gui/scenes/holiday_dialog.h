// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <optional>
#include <string>
#include <vector>

#include "model/holiday.h"

class GameController;

/**
 * @brief "Go on holiday" planner and the assistant's report on return.
 *
 * The planner chooses how long the manager is away (a date, the next match,
 * the next decision or the end of the transfer window), what brings him back
 * early and which duties the assistant takes over. render() hands the plan
 * back on the frame the manager leaves; the career hub runs it on the
 * Continue machinery and calls showSummary() when it is done.
 */
class HolidayDialog
{
 public:
  /** Opens the planner with the career's saved preferences. */
  void open(GameController& controller);

  /** Opens the report of a finished holiday. */
  void showSummary(const GameController& controller,
                   const HolidaySummary& summary);

  /** Draws the dialog; returns the plan on the frame the manager leaves. */
  std::optional<HolidayPlan> render(GameController& controller);

  [[nodiscard]] bool isOpen() const { return visible; }

 private:
  struct Line
  {
    std::string text;
    ImVec4 color;
  };

  std::optional<HolidayPlan> renderPlan(GameController& controller);
  void renderSummary();
  void refreshTarget(const GameController& controller);

  bool open_requested = false;
  bool visible = false;
  bool showing_summary = false;
  HolidayPlan plan;
  int mode = 0;
  int bid_mode = 0; /**< 0 = key players, 1 = from an amount. */
  std::int64_t bid_amount = 0;
  int crisis_count = 5;
  std::string target_text;
  bool can_leave = false;

  // Summary view, formatted once.
  std::string summary_title;
  std::string summary_reason;
  ImVec4 summary_reason_color{};
  std::vector<Line> results;
  std::string table_line;
  std::vector<std::pair<std::string, std::string>> money_rows;
  std::vector<Line> moves;
  std::vector<Line> injuries;
  std::string filed_line;
};
