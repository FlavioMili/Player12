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

#include "model/data_hub.h"
#include "model/match_insights.h"
#include "model/match_report.h"

class GameController;
class GUIView;
struct ImVec2;

/**
 * @brief The Analysis tab of the match report: a written summary, the xG
 * race with final-third pressure, the shot map, key moments, both pass
 * networks and touch maps. Everything shown is cached in build(); render()
 * only lays out and draws (ImDrawList), with a tooltip on every mark.
 */
class MatchInsightsView
{
 public:
  /** Caches the view of a played match; available() is false when the
   * match was not tracked (other clubs' fixtures, quick results). */
  void build(const GameController& controller, const MatchReport& report,
             const std::optional<ManagedMatchSnapshot>& snapshot,
             const std::string& home_name, const std::string& away_name);
  [[nodiscard]] bool available() const { return has_data; }

  void render(const GUIView* view);

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  struct ShotMark
  {
    float x = 0.5f; /*!< Pitch frame: home attacks towards x = 1. */
    float y = 0.5f;
    float xg = 0.0f;
    float axis = 0.0f; /*!< Minute on the timeline axis. */
    bool home = true;
    bool goal = false;
    std::string tooltip;
  };
  struct StepPoint
  {
    float axis = 0.0f;
    float total = 0.0f;
  };
  struct MomentRow
  {
    KeyMoment::Kind kind = KeyMoment::Kind::Goal;
    bool home = true;
    float axis = 0.0f;
    int shot = -1;
    std::string minute;
    std::string text;
    std::string detail;
  };
  struct NetworkNode
  {
    float x = 0.5f; /*!< Attacking frame of the side. */
    float y = 0.5f;
    float weight = 0.0f; /*!< Share of the side's busiest player's touches. */
    std::size_t detail_index = 0;
    std::string label;
    std::string tooltip;
  };
  struct NetworkEdge
  {
    std::size_t a = 0; /*!< Indices into the side's nodes. */
    std::size_t b = 0;
    int total = 0;
    std::string tooltip;
  };
  struct Network
  {
    std::vector<NetworkNode> nodes;
    std::vector<NetworkEdge> edges;
    int busiest = 1;
    std::string note;
  };
  struct HeatChoice
  {
    std::string label;
    std::array<int, MatchTracking::CELLS> cells{};
    int total = 0;
    int most = 0;
  };

  void renderSummary(float width) const;
  void renderTimeline(float width);
  void renderShotMap(float width);
  void renderMoments(float width);
  void renderNetwork(std::size_t side, float width, bool selector);
  void renderHeatmap(float width);
  void selectHeat(std::size_t side, std::size_t choice);
  void buildCellTooltips();

  bool has_data = false;
  bool has_detail = false;
  std::array<std::string, 2> names;
  std::vector<std::string> summary;
  // Timeline.
  float axis_end = 90.0f;
  float xg_top = 1.0f;
  std::array<std::vector<StepPoint>, 2> xg_steps;
  std::array<std::string, 2> xg_totals;
  std::array<std::vector<int>, 2> pressure;
  int pressure_top = 1;
  std::vector<std::string> bucket_tooltips;
  std::vector<std::string> axis_labels;
  // Shots and moments.
  std::vector<ShotMark> shots;
  std::string shot_note;
  std::vector<MomentRow> moments;
  /** Moment under the cursor or picked in the list (-1: none). */
  int focus_moment = -1;
  int picked_moment = -1;
  int hovered_moment = -1; /*!< Set while drawing the list (next frame). */
  // Passing and touches.
  std::array<Network, 2> networks;
  int network_side = 0;
  std::array<std::vector<HeatChoice>, 2> heat;
  std::array<std::vector<std::size_t>, 2> heat_detail; /*!< Per choice. */
  int heat_side = 0;
  std::size_t heat_choice = 0;
  /** Cell tooltips of the shown touch map (rebuilt when it changes). */
  std::array<std::string, MatchTracking::CELLS> cell_tooltips;
  int cell_tooltip_side = -1;
  std::size_t cell_tooltip_choice = 0;
};
