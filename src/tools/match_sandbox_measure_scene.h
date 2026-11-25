// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <memory>
#include <string>

#include "gui/gui_scene.h"
#include "tools/match_sandbox_drill.h"
#include "tools/match_sandbox_measure.h"

/**
 * Measure mode of a drill: choose a setting to sweep (and optionally one to
 * compare by), run the drill headless over it, and read the results as a
 * curve and a table; export every run as CSV. Runs a slice of the measure
 * each frame so the window stays responsive.
 */
class DrillMeasureScene : public GUIScene
{
 public:
  /** `runNow`: start the default measure straight away. */
  DrillMeasureScene(GUIView* guiView_ptr, Drill& drill, bool runNow = false);
  ~DrillMeasureScene() override;

  void onEnter() override;
  void update(float deltaTime) override;
  void render() override;
  SceneID getID() const override;

 private:
  /** The setting an axis varies and the range it covers. */
  struct AxisSetup
  {
    /** Index into the drill's parameters; -1: none (the "by" axis only). */
    int parameter = -1;
    float from = 0.0F;
    float to = 1.0F;
    int count = 6;
  };

  void start();
  void renderPanel(float width);
  [[nodiscard]] bool renderActions(float width);
  void renderRunStatus();
  void renderResults(ImVec2 size);
  void renderChart(ImVec2 size);
  void renderTable(ImVec2 size);
  /** Combo and range widgets of one axis. */
  void renderAxis(const char* label, AxisSetup& axis, bool optional,
                  int exclude);
  void renderAxisSetting(const std::vector<DrillParameter>& parameters,
                         AxisSetup& axis, bool optional, int exclude);
  /** Puts the range to the parameter's own (all choices, or min to max). */
  void resetAxis(AxisSetup& axis);
  [[nodiscard]] std::optional<MeasureAxis> axisOf(const AxisSetup& axis);
  void exportCsv();

  Drill& drill;
  AxisSetup sweep;
  AxisSetup by;
  int repeats = 3;
  int firstSeed = 1;
  std::unique_ptr<DrillMeasure> measure;
  int metric = 0;
  std::string exported;
  bool runPending = false;
  int panelTab = 0;
  int resultView = 2;
  ImFont* regularFont = nullptr;
  ImFont* boldFont = nullptr;
};
