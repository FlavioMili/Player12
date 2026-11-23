// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gui/gui_scene.h"
#include "tools/match_sandbox_drill.h"
#include "tools/match_sandbox_measure.h"

/**
 * Shot map: the Shot drill measured over a grid of spots in the final
 * third, many runs per spot. Per spot: conversion against the engine's own
 * xG (the calibration heatmap) and, in choice mode, whether and from where
 * the shooter chose to shoot. Built on DrillMeasure (sweep: distance from
 * the goal line; by: off centre).
 */

struct ShotMapSpec
{
  float lineFrom = 4.0F;
  float lineTo = 34.0F;
  int lineCount = 11;
  float sideFrom = -20.0F;
  float sideTo = 20.0F;
  int sideCount = 11;
  int repeats = 40;
  std::uint32_t firstSeed = 1;
  /** Other Shot settings held for the whole map (keeper, mode...). */
  std::map<std::string, float> fixed;
};

/** One spot of the grid. */
struct ShotMapCell
{
  float line = 0.0F;
  float side = 0.0F;
  int runs = 0;
  int shots = 0;
  int goals = 0;
  int onTarget = 0;
  double xg = 0.0;
  /** Sum of where the shots were taken from (choice mode moves them). */
  double shotLine = 0.0;
  double shotSide = 0.0;
};

/** Shots grouped by the engine's xG: predicted against scored. */
struct CalibrationBin
{
  double from = 0.0;
  double to = 0.0;
  int shots = 0;
  int goals = 0;
  double xg = 0.0;
};

/** A single shot, for the choice-mode scatter. */
struct ShotMapShot
{
  float line = 0.0F;
  float side = 0.0F;
  bool goal = false;
};

class ShotMap
{
 public:
  ShotMap(Drill& shotDrill, const StatsConfig& config, const ShotMapSpec& spec);

  bool work(double budgetSeconds) { return measure->work(budgetSeconds); }
  void stop() { measure->stop(); }
  [[nodiscard]] bool done() const { return measure->done(); }
  [[nodiscard]] const DrillMeasure& runs() const { return *measure; }
  [[nodiscard]] const ShotMapSpec& spec() const { return setup; }

  /** Results so far (recomputed when more runs are done). */
  [[nodiscard]] const std::vector<ShotMapCell>& cells();
  [[nodiscard]] const std::vector<CalibrationBin>& calibration();
  [[nodiscard]] const std::vector<ShotMapShot>& shots();
  /** Spacing of the grid in metres (along the goal line, across). */
  [[nodiscard]] float lineStep() const;
  [[nodiscard]] float sideStep() const;

 private:
  void refresh();
  /** One shot into its cell, the scatter and the calibration bins. */
  void addShot(ShotMapCell& cell, const MeasureRun& run);

  ShotMapSpec setup;
  std::unique_ptr<DrillMeasure> measure;
  std::size_t counted = static_cast<std::size_t>(-1);
  std::vector<ShotMapCell> grid;
  std::vector<CalibrationBin> bins;
  std::vector<ShotMapShot> taken;
};

/** The map's screen: grid settings, the heatmap, the calibration chart. */
class ShotMapScene : public GUIScene
{
 public:
  /** `runNow`: start the default map straight away. */
  ShotMapScene(GUIView* guiView_ptr, Drill& shotDrill, bool runNow = false);
  ~ShotMapScene() override;

  void onEnter() override;
  void update(float deltaTime) override;
  void render() override;
  SceneID getID() const override;

 private:
  void start();
  void renderPanel(float width);
  void renderResults(ImVec2 area);
  void renderMap(ImVec2 size);
  void renderShotActions(float width, float scale);
  void renderShotFields();
  [[nodiscard]] bool shotsMoved() const;
  void renderCalibration(ImVec2 size);
  void exportCsv();

  Drill& drill;
  ShotMapSpec spec;
  std::unique_ptr<ShotMap> map;
  int view = 0;
  int panelTab = 0;
  int resultTab = 0;
  ImFont* regularFont = nullptr;
  ImFont* boldFont = nullptr;
  bool runPending = false;
  std::string exported;
};

/** Headless shot map from the command line (see match_sandbox_main.cpp). */
int runShotMapCommand(const StatsConfig& config,
                      const std::vector<std::string>& arguments);
