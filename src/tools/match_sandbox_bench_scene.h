// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "gui/gui_scene.h"
#include "tools/match_sandbox_bench.h"

/**
 * Technique bench (Notes and plans/match_engine_technique_bench_plan.md §6):
 * the catalogue of pass and shot tests with tag filters, run with the
 * sandbox's tuning (the contact view edits it), results as success curves
 * against skill or a grid, each run kept to compare with the next; one
 * test's detail and Watch.
 */
class TechniqueBenchScene : public GUIScene
{
 public:
  explicit TechniqueBenchScene(GUIView* guiView_ptr, bool runNow = false);
  ~TechniqueBenchScene() override;

  void onEnter() override;
  void update(float deltaTime) override;
  void render() override;
  SceneID getID() const override;

 private:
  void reload();
  void start(bool onlyShown);
  void finishRun();
  /** Whether a catalogue test passes the tag filters. */
  [[nodiscard]] bool shown(const BenchTest& test) const;
  /** The tests drawn: ticked and shown, in catalogue order. */
  [[nodiscard]] std::vector<const BenchTest*> drawnTests() const;
  /** A test's colour (by the chosen tag's value, or its own place). */
  [[nodiscard]] ImU32 colourOf(const BenchTest& test) const;
  /** The value of the chosen result for a cell: success rate (%) or a
   * metric's mean. */
  [[nodiscard]] std::optional<double> valueOf(const BenchCell* cell) const;
  [[nodiscard]] const BenchResults* shownRun() const;
  [[nodiscard]] const BenchResults* compareRun() const;

  void renderPanel(float width);
  void renderTuning();
  void renderFilters();
  void renderTestList();
  void renderResults(float width, float height);
  void renderChart(ImVec2 size);
  void renderGrid(ImVec2 size);
  void renderTest();
  void watch(const BenchTest& test, float skill, int seed);
  void openContactView();
  void loadSaved(const std::filesystem::path& path);

  BenchCatalogue catalogue;
  std::vector<bool> ticked;
  /** Tag key -> values let through (empty: all). */
  std::map<std::string, std::set<std::string>> filters;
  /** Tag keys and their values, in catalogue order. */
  std::vector<std::pair<std::string, std::vector<std::string>>> tagValues;

  int repeats = 50;
  int firstSeed = 1;
  char skillsText[128] = {};
  /** 0: each test's own skill; else an entry of SKILL_OVERRIDES. */
  int skillOverride = 0;

  std::unique_ptr<BenchRunner> runner;
  /** Every run this session (and loaded files), oldest first. */
  std::vector<std::unique_ptr<BenchResults>> runs;
  int showIndex = -1;
  /** -1: none. */
  int compareIndex = -1;
  /** The run the runner fills; -1: none running. */
  int runningIndex = -1;
  /** Select this tab next frame (-1: none). */
  int selectView = -1;
  /** Each Watch drill's own settings, put back before a test's. */
  std::map<Drill*, std::map<std::string, float>> watchDefaults;
  std::string status;

  /** 0 success rate, else 1 + index into the shown run's metric names. */
  int result = 0;
  /** Index into tagValues, or -1: each test its own colour. */
  int colourBy = -1;
  int view = 0;
  bool gridChange = false;
  std::string focus;
  float watchSkill = 50.0f;
  int watchSeed = 1;
  /** Drills for Watch (they outlive the drill scene). */
  std::vector<std::unique_ptr<Drill>> watchDrills;
  bool runPending = false;
  int panelTab = 0;
  ImFont* regularFont = nullptr;
  ImFont* boldFont = nullptr;
};
