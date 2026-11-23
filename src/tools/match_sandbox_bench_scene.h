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
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "gui/gui_scene.h"
#include "tools/match_sandbox_bench.h"

/** Room for the skills field's text. */
inline constexpr std::size_t SKILLS_TEXT_BYTES = 128;

/** The bench chart's plot area and scales, for one frame. */
struct ChartFrame
{
  float left = 0.0F;
  float right = 1.0F;
  float top = 0.0F;
  float bottom = 1.0F;
  double xMin = 0.0;
  double xMax = 1.0;
  double yMin = 0.0;
  double yMax = 1.0;
  float scale = 1.0F;

  /** A skill across, and a value up, on the screen. */
  [[nodiscard]] float sx(double x) const;
  [[nodiscard]] float sy(double y) const;
};

struct ChartLegendEntry
{
  std::string label;
  ImU32 colour = 0;
};

/** One test's curve from one run, over the chart's skills. */
struct ChartCurve
{
  const BenchResults* source = nullptr;
  const BenchTest* test = nullptr;
  const std::vector<float>* skills = nullptr;
  bool faint = false;
};

/** The shown run's point nearest the mouse, so far. */
struct ChartHover
{
  const BenchTest* test = nullptr;
  float skill = 0.0F;
  float distance = 0.0F;
};

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
  void renderRunProgress(float scale);
  void renderRunActions(float width, float scale);
  void renderControlTabs(float width, float scale);
  void renderRunSetup(float width, float scale);
  [[nodiscard]] std::string runName(int index) const;
  void runCombo(const char* label, int& index, bool optional);
  void renderRunsCard(float width, float scale);
  void renderTuning();
  void renderFilters();
  void renderTestList();
  void tickShown(bool on);
  void renderTestRow(std::size_t index);
  void renderResults(ImVec2 area);
  void renderResultFields(const std::vector<std::string>& names, float scale);
  void renderResultTabs(float scale);
  void renderChart(ImVec2 size);
  [[nodiscard]] std::vector<ChartLegendEntry> chartLegend(
      const std::vector<const BenchTest*>& tests) const;
  [[nodiscard]] std::optional<std::pair<double, double>> chartValueRange(
      const std::vector<const BenchTest*>& tests,
      const std::vector<float>& skills) const;
  std::optional<ImVec2> drawCurve(ImDrawList& draw, const ChartFrame& frame,
                                  const ChartCurve& curve,
                                  ChartHover& hover) const;
  void chartTooltip(const ChartHover& hover, bool clicked);
  void renderGrid(ImVec2 size);
  void renderGridCell(const BenchTest& test, float skill, bool change) const;
  void renderTest();
  void renderTestSummary(const BenchTest& test) const;
  void renderWatchControls(const BenchTest& test);
  void renderTestTable(const BenchTest& test) const;
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
  std::array<char, SKILLS_TEXT_BYTES> skillsText{};
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
  float watchSkill = 50.0F;
  int watchSeed = 1;
  /** Drills for Watch (they outlive the drill scene). */
  std::vector<std::unique_ptr<Drill>> watchDrills;
  bool runPending = false;
  int panelTab = 0;
  ImFont* regularFont = nullptr;
  ImFont* boldFont = nullptr;
};
