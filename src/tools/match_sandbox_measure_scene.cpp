// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_measure_scene.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <string_view>

#include "controller/game_controller.h"
#include "global/paths.h"
#include "gui/gui_view.h"
#include "gui/widgets/theme.h"
#include "tools/match_sandbox_page_ui.h"
#include "tools/match_sandbox_text.h"

namespace
{
constexpr float BUTTON_GAP = 6.0F;
constexpr double TICK_BASE = 10.0;
constexpr float DEFAULT_SWEEP_FROM = 40.0F;
constexpr float DEFAULT_SWEEP_TO = 95.0F;
constexpr int DEFAULT_SWEEP_COUNT = 12;
constexpr int DEFAULT_AXIS_COUNT = 6;
constexpr int MAX_AXIS_COUNT = 25;
constexpr int MAX_REPEATS = 50;
constexpr float REGULAR_FONT_SIZE = 16.0F;
constexpr float HEADER_FONT_SIZE = 14.0F;
constexpr float PAGE_PADDING = 24.0F;
constexpr float PROGRESS_HEIGHT = 8.0F;
constexpr ImVec4 NOTE_COLOR{1.0F, 0.75F, 0.3F, 1.0F};
constexpr float SETTINGS_COLUMNS_WIDTH = 360.0F;
constexpr float MAX_PANEL_WIDTH = 440.0F;
constexpr float MIN_CHART_HEIGHT = 200.0F;
constexpr float SPLIT_CHART_SHARE = 0.55F;
constexpr float CARD_CHROME_HEIGHT = 56.0F;
constexpr double X_SPAN_EPSILON = 1e-9;
constexpr double EMPTY_X_PADDING = 0.5;
constexpr float LABEL_ALIGNMENT = 0.5F;
constexpr float Y_LABEL_GAP = 6.0F;
constexpr float X_LABEL_GAP = 4.0F;
constexpr float PLOT_TOP_PADDING = 10.0F;
constexpr float HOVER_RADIUS = 10.0F;
constexpr float CURVE_STROKE = 2.0F;
constexpr float POINT_RADIUS = 3.5F;
constexpr float LEGEND_GAP = 12.0F;
constexpr float LEGEND_MARKER_SIZE = 12.0F;
constexpr float LEGEND_MARKER_ADVANCE = 14.0F;
constexpr double DISPLAYED_SD_THRESHOLD = 0.0005;

struct SettingsCard
{
  const char* id;
  const char* title;
};
constexpr std::array<SettingsCard, 4> SETTINGS_CARDS{
    {{.id = "##measure_sweep", .title = "SWEEP"},
     {.id = "##measure_compare", .title = "COMPARE BY"},
     {.id = "##measure_runs", .title = "RUN SETTINGS"},
     {.id = "##measure_settings", .title = "HELD SETTINGS"}}};

const SettingsCard& settingsCard(int tab)
{
  if (tab < 0 || tab >= static_cast<int>(SETTINGS_CARDS.size()))
  {
    return SETTINGS_CARDS.back();
  }
  return SETTINGS_CARDS.at(static_cast<std::size_t>(tab));
}

/** Real time spent measuring per frame. */
constexpr double WORK_PER_FRAME_SECONDS = 0.012;

constexpr std::array<ImU32, 6> CURVE_COLOURS{
    IM_COL32(255, 200, 60, 255),  IM_COL32(90, 170, 255, 255),
    IM_COL32(120, 220, 120, 255), IM_COL32(240, 100, 100, 255),
    IM_COL32(200, 130, 255, 255), IM_COL32(240, 240, 240, 255)};

void field(const char* label)
{
  SandboxText::wrappedText(label);
  ImGui::SetNextItemWidth(-1);
}

void nextButton(const char* label, float scale, ImFont* font)
{
  if (ImGui::GetItemRectMax().x + BUTTON_GAP * scale +
          SandboxPageUI::buttonWidth(label, scale, font) <=
      ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
  {
    ImGui::SameLine(0, BUTTON_GAP * scale);
  }
}

int findParameter(const std::vector<DrillParameter>& parameters,
                  std::string_view name)
{
  for (std::size_t index = 0; index < parameters.size(); ++index)
  {
    if (name == parameters[index].name)
    {
      return static_cast<int>(index);
    }
  }
  return -1;
}

/** "Nice" tick spacing for a range: 1, 2 or 5 times a power of ten. */
double tickStep(double range, int ticks)
{
  const double raw = range / std::max(1, ticks);
  const double power = std::pow(TICK_BASE, std::floor(std::log10(raw)));
  for (const double step : {1.0, 2.0, 5.0, 10.0})
  {
    if (raw <= step * power)
    {
      return step * power;
    }
  }
  return TICK_BASE * power;
}
}  // namespace

DrillMeasureScene::DrillMeasureScene(GUIView* guiView_ptr, Drill& drill_ref,
                                     bool runNow)
    : GUIScene(guiView_ptr), drill(drill_ref)
{
  // A first measure worth running: Pace, with and without the ball.
  const auto parameters = drill.parameters();
  sweep.parameter = std::max(0, findParameter(parameters, "Pace"));
  resetAxis(sweep);
  if (parameters[static_cast<std::size_t>(sweep.parameter)].name ==
      std::string_view("Pace"))
  {
    sweep.from = DEFAULT_SWEEP_FROM;
    sweep.to = DEFAULT_SWEEP_TO;
    sweep.count = DEFAULT_SWEEP_COUNT;
  }
  by.parameter = findParameter(parameters, "With the ball");
  if (by.parameter >= 0)
  {
    resetAxis(by);
  }
  firstSeed = static_cast<int>(drill.seed);
  runPending = runNow;
}

DrillMeasureScene::~DrillMeasureScene() = default;

SceneID DrillMeasureScene::getID() const { return SceneID::MATCH_SANDBOX; }

void DrillMeasureScene::onEnter()
{
  // Reuse the launch page's fonts. Direct tool entry loads them once into
  // the current atlas, so reopening Measure does not add font
  // sources.
  auto* atlas = ImGui::GetIO().Fonts;
  const auto font = [&](const char* file, float size)
  {
    const auto config = SandboxTypography::manropeConfig();
    for (auto* existing : atlas->Fonts)
    {
      if (std::string_view(existing->GetDebugName()).find(file) !=
              std::string_view::npos &&
          !existing->Sources.empty() &&
          existing->Sources[0]->ExtraSizeScale == config.ExtraSizeScale)
      {
        return existing;
      }
    }
    return atlas->AddFontFromFileTTF(
        AssetPaths::file((std::string("assets/fonts/") + file).c_str()).c_str(),
        size, &config);
  };
  regularFont = font("Manrope-Regular.ttf", REGULAR_FONT_SIZE);
  boldFont = font("Manrope-Bold.ttf", HEADER_FONT_SIZE);
}

void DrillMeasureScene::update(float /*deltaTime*/)
{
  if (runPending)
  {
    runPending = false;
    start();
  }
  if (measure && !measure->done())
  {
    measure->work(WORK_PER_FRAME_SECONDS);
  }
}

void DrillMeasureScene::resetAxis(AxisSetup& axis)
{
  const auto parameters = drill.parameters();
  if (axis.parameter < 0 ||
      axis.parameter >= static_cast<int>(parameters.size()))
  {
    return;
  }
  const DrillParameter& parameter =
      parameters[static_cast<std::size_t>(axis.parameter)];
  axis.from = parameter.min;
  axis.to = parameter.max;
  axis.count = parameter.choices.empty()
                   ? DEFAULT_AXIS_COUNT
                   : static_cast<int>(headlessChoices(parameter).size());
}

std::optional<MeasureAxis> DrillMeasureScene::axisOf(const AxisSetup& axis)
{
  const auto parameters = drill.parameters();
  if (axis.parameter < 0 ||
      axis.parameter >= static_cast<int>(parameters.size()))
  {
    return std::nullopt;
  }
  const DrillParameter& parameter =
      parameters[static_cast<std::size_t>(axis.parameter)];
  MeasureAxis out{.parameter = parameter.name, .values = {}};
  out.values =
      parameter.choices.empty()
          ? spreadValues(axis.from, axis.to, axis.count, parameter.integer)
          : headlessChoices(parameter);
  return out;
}

void DrillMeasureScene::start()
{
  MeasureSpec spec;
  const auto sweepAxis = axisOf(sweep);
  if (!sweepAxis)
  {
    return;
  }
  spec.sweep = *sweepAxis;
  spec.by = axisOf(by);
  spec.repeats = repeats;
  spec.firstSeed = static_cast<std::uint32_t>(std::max(0, firstSeed));
  measure.reset();
  measure = std::make_unique<DrillMeasure>(
      drill, guiView->getController().getStatsConfig(), std::move(spec));
  metric = 0;
  exported.clear();
}

void DrillMeasureScene::exportCsv()
{
  if (!measure)
  {
    return;
  }
  const auto path = measurePath(drill.name(), measure->spec().sweep.parameter);
  std::error_code ignored;
  std::filesystem::create_directories(path.parent_path(), ignored);
  std::ofstream file(path, std::ios::binary);
  file << measure->csv();
  exported =
      file ? "Saved " + path.string() : "Could not write " + path.string();
}

void DrillMeasureScene::render()
{
  namespace Page = SandboxPageUI;
  const auto* viewport = ImGui::GetMainViewport();
  const float scale = Theme::scale();
  const Page::Style style(regularFont, scale);
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, Page::PAGE);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(PAGE_PADDING * scale, PAGE_PADDING * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGui::Begin("##measure", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float gutter = (BUTTON_GAP * scale) + ImGui::GetStyle().ScrollbarSize;
  const bool columns = available.x >= 1000 * scale;
  const float panel =
      columns ? (available.x >= 1400 * scale ? 440.0F : 400.0F) * scale
              : available.x - gutter;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  if (columns)
  {
    ImGui::BeginChild("##measure_controls", ImVec2(panel + gutter, available.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderPanel(panel);
    ImGui::EndChild();
    ImGui::SameLine(0, BUTTON_GAP * scale);
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("##measure_results", ImVec2(width, available.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderResults(ImVec2(std::max(scale, width - gutter), available.y));
    ImGui::EndChild();
  }
  else
  {
    ImGui::BeginChild("##measure_page", available,
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderPanel(panel);
    renderResults(ImVec2(panel, available.y));
    ImGui::EndChild();
  }
  ImGui::PopStyleVar();
  ImGui::End();
}

void DrillMeasureScene::renderAxis(const char* label, AxisSetup& axis,
                                   bool optional, int exclude)
{
  const auto parameters = drill.parameters();
  ImGui::PushID(label);
  renderAxisSetting(parameters, axis, optional, exclude);
  if (axis.parameter >= 0 &&
      axis.parameter < static_cast<int>(parameters.size()))
  {
    const DrillParameter& parameter =
        parameters[static_cast<std::size_t>(axis.parameter)];
    if (parameter.choices.empty())
    {
      if (ImGui::BeginTable("##range", 2,
                            ImGuiTableFlags_SizingStretchSame |
                                ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_BordersInnerH |
                                ImGuiTableFlags_BordersInnerV))
      {
        ImGui::TableNextColumn();
        ImGui::Dummy(ImVec2(0, 0));
        field("From");
        ImGui::SliderFloat("##from", &axis.from, parameter.min, parameter.max,
                           parameter.integer ? "%.0f" : "%.1f");
        ImGui::TableNextColumn();
        ImGui::Dummy(ImVec2(0, 0));
        field("To");
        ImGui::SliderFloat("##to", &axis.to, parameter.min, parameter.max,
                           parameter.integer ? "%.0f" : "%.1f");
        ImGui::EndTable();
      }
      field("Points");
      ImGui::SliderInt("##count", &axis.count, 2, MAX_AXIS_COUNT);
    }
    else
    {
      std::string all;
      for (const float value : headlessChoices(parameter))
      {
        all += std::string(all.empty() ? "" : ", ") +
               parameter.choices[static_cast<std::size_t>(value)];
      }
      ImGui::PushTextWrapPos(0);
      SandboxText::disabledText(std::format("All of: {}", all).c_str());
      ImGui::PopTextWrapPos();
    }
  }
  ImGui::PopID();
}

void DrillMeasureScene::renderPanel(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  const bool running = renderActions(width);
  Page::beginCard("##measure_tabs", "CONTROLS", width, Page::MID, scale,
                  boldFont);
  const std::array<const char*, 4> tabs{"Sweep", "Compare", "Runs", "Settings"};
  for (int tab = 0; tab < 4; ++tab)
  {
    if (tab > 0)
    {
      ImGui::SameLine(0, BUTTON_GAP * scale);
    }
    if (Page::button(tabs.at(tab), panelTab == tab, scale, boldFont))
    {
      panelTab = tab;
    }
  }
  Page::endCard(scale);
  const SettingsCard& card = settingsCard(panelTab);
  Page::beginCard(card.id, card.title, width, Page::LIGHT, scale, boldFont);
  ImGui::BeginDisabled(running);
  if (panelTab == 0)
  {
    renderAxis("Sweep", sweep, false, by.parameter);
  }
  else if (panelTab == 1)
  {
    renderAxis("Compare", by, true, sweep.parameter);
  }
  else if (panelTab == 2)
  {
    if (ImGui::BeginTable(
            "##run_fields", 2,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersInnerV))
    {
      ImGui::TableNextColumn();
      ImGui::Dummy(ImVec2(0, 0));
      field("Repeats");
      ImGui::SliderInt("##repeats", &repeats, 1, MAX_REPEATS);
      if (ImGui::IsItemHovered())
      {
        SandboxText::tooltipText(
            "Runs per point, on consecutive seeds from the first. Seeds affect "
            "the course and engine randomness.");
      }
      ImGui::TableNextColumn();
      ImGui::Dummy(ImVec2(0, 0));
      field("First seed");
      ImGui::InputInt("##first_seed", &firstSeed);
      ImGui::EndTable();
    }
  }
  else
  {
    SandboxText::wrappedText(drill.summary());
    SandboxText::wrappedText(
        "Shared with Watch. Sweep and comparison axes override these settings "
        "for each sample.");
    drill.renderSettings(width >= SETTINGS_COLUMNS_WIDTH * scale ? 2 : 1, false,
                         true);
  }
  ImGui::EndDisabled();
  Page::endCard(scale);
}

void DrillMeasureScene::renderResults(ImVec2 size)
{
  const float width = size.x;
  const float height = size.y;
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##measure_result_controls", "RESULTS", width, Page::MID,
                  scale, boldFont);
  if (!measure || measure->metricNames().empty())
  {
    SandboxText::wrappedText(measure ? "Measuring..."
                                     : "Choose a sweep and press Run to see "
                                       "measured curves and results.");
    Page::endCard(scale);
    return;
  }
  const auto& names = measure->metricNames();
  metric = std::clamp(metric, 0, int(names.size()) - 1);
  ImGui::TextUnformatted("Result");
  ImGui::SetNextItemWidth(
      std::min(MAX_PANEL_WIDTH * scale, ImGui::GetContentRegionAvail().x));
  if (SandboxPageUI::beginCombo("##metric", names[metric].c_str()))
  {
    for (std::size_t index = 0; index < names.size(); ++index)
    {
      if (ImGui::Selectable(names[index].c_str(), int(index) == metric))
      {
        metric = int(index);
      }
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
  {
    SandboxText::tooltipText(names[metric].c_str());
  }
  SandboxText::wrappedText(
      "Mean of the repeats; error bars span one standard deviation either "
      "side.");
  const std::array<const char*, 3> views{"Chart", "Table", "Both"};
  for (int view = 0; view < 3; ++view)
  {
    if (view > 0)
    {
      ImGui::SameLine(0, BUTTON_GAP * scale);
    }
    if (Page::button(views.at(view), resultView == view, scale, boldFont))
    {
      resultView = view;
    }
  }
  Page::endCard(scale);
  const float available =
      std::max(240 * scale, height - ImGui::GetCursorPosY());
  if (resultView != 1)
  {
    Page::beginCard("##measure_chart", "MEASURED CURVES", width, Page::DARK,
                    scale, boldFont);
    renderChart(ImVec2(
        ImGui::GetContentRegionAvail().x,
        std::max(MIN_CHART_HEIGHT * scale,
                 (resultView == 2 ? available * SPLIT_CHART_SHARE : available) -
                     (CARD_CHROME_HEIGHT * scale))));
    Page::endCard(scale);
  }
  if (resultView != 0)
  {
    const float tableHeight =
        std::max(160 * scale, height - ImGui::GetCursorPosY() - (62 * scale));
    Page::beginCard("##measure_table", "MEASURED RESULTS", width, Page::LIGHT,
                    scale, boldFont);
    renderTable(ImVec2(ImGui::GetContentRegionAvail().x, tableHeight));
    Page::endCard(scale);
  }
}

namespace
{
/** Room round the plot for the axes' labels (scaled pixels). */
constexpr float PLOT_LEFT_MARGIN = 56.0F;
constexpr float PLOT_RIGHT_MARGIN = 12.0F;
constexpr float PLOT_BOTTOM_MARGIN = 34.0F;

/** One metric's chart over a measure's sweep: a curve per "by" value. */
class MeasureChart
{
 public:
  MeasureChart(const DrillMeasure& source, const std::string& metric)
      : measure(&source),
        spec(&source.spec()),
        name(&metric),
        points(source.points()),
        choiceX(!std::ranges::all_of(
            source.spec().sweep.values,
            [&source](float value)
            {
              return source.valueText(source.spec().sweep.parameter, value) ==
                     std::format("{:g}", value);
            }))
  {
    for (const MeasurePoint& point : points)
    {
      if (std::ranges::find(groups, point.by) == groups.end())
      {
        groups.push_back(point.by);
      }
    }
  }

  /** The value ranges, padded; false if the metric has no values. */
  bool findRanges()
  {
    for (const float value : spec->sweep.values)
    {
      xMin = std::min(xMin, xOf(value));
      xMax = std::max(xMax, xOf(value));
    }
    for (const MeasurePoint& point : points)
    {
      if (const auto found = point.stats.find(*name);
          found != point.stats.end())
      {
        yMin = std::min(yMin, found->second.mean - found->second.sd);
        yMax = std::max(yMax, found->second.mean + found->second.sd);
      }
    }
    if (yMin > yMax)
    {
      return false;
    }
    if (xMax - xMin < X_SPAN_EPSILON)
    {
      xMin -= 1.0;
      xMax += 1.0;
    }
    const double yPad =
        std::max((yMax - yMin) * 0.1, (0.05 * std::abs(yMax)) + 1e-3);
    yMin -= yPad;
    yMax += yPad;
    if (choiceX)
    {
      xMin -= EMPTY_X_PADDING;
      xMax += EMPTY_X_PADDING;
    }
    return true;
  }

  /** Rows the legend takes at that width (none without a "by"). */
  [[nodiscard]] int legendRows(float width) const
  {
    if (!spec->by)
    {
      return 0;
    }
    float used = 0;
    int rows = 1;
    for (const float group : groups)
    {
      const float labelWidth =
          ImGui::CalcTextSize(
              measure->valueText(spec->by->parameter, group).c_str())
              .x +
          (30 * Theme::scale());
      if (used > 0 && used + labelWidth > width)
      {
        ++rows;
        used = 0;
      }
      used += labelWidth;
    }
    return rows;
  }

  void place(ImVec2 origin, ImVec2 plot)
  {
    const float scale = Theme::scale();
    left = origin.x + (PLOT_LEFT_MARGIN * scale);
    right = origin.x + plot.x - (PLOT_RIGHT_MARGIN * scale);
    top = origin.y + (PLOT_TOP_PADDING * scale);
    bottom = origin.y + plot.y - (PLOT_BOTTOM_MARGIN * scale);
  }

  void drawAxes(ImDrawList& draw) const
  {
    draw.AddRectFilled(ImVec2(left, top), ImVec2(right, bottom),
                       SandboxPageUI::DARK);
    drawYTicks(draw);
    drawXTicks(draw);
    const float scale = Theme::scale();
    const std::string& parameter = spec->sweep.parameter;
    const ImVec2 axisSize = ImGui::CalcTextSize(parameter.c_str());
    draw.AddText(ImVec2((left + right - axisSize.x) * LABEL_ALIGNMENT,
                        bottom + (X_LABEL_GAP * scale) + axisSize.y),
                 SandboxPageUI::MUTED, parameter.c_str());
  }

  /** The curves; returns the point nearest the mouse, if near enough. */
  const MeasurePoint* drawCurves(ImDrawList& draw) const
  {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const MeasurePoint* hovered = nullptr;
    float hoveredDistance = HOVER_RADIUS * Theme::scale();
    for (std::size_t group = 0; group < groups.size(); ++group)
    {
      for (const auto& [at, point] : drawCurve(draw, group))
      {
        const float distance = std::hypot(mouse.x - at.x, mouse.y - at.y);
        if (distance < hoveredDistance)
        {
          hoveredDistance = distance;
          hovered = point;
        }
      }
    }
    return hovered;
  }

  void tooltip(const MeasurePoint& hovered) const
  {
    const MeasureStat& stat = hovered.stats.at(*name);
    std::string tip =
        std::format("{} {}", spec->sweep.parameter,
                    measure->valueText(spec->sweep.parameter, hovered.x));
    if (spec->by)
    {
      tip += std::format(" · {} {}", spec->by->parameter,
                         measure->valueText(spec->by->parameter, hovered.by));
    }
    tip += std::format(
        "\n{}: {:.3f} ± {:.3f}\nrange {:.3f} – {:.3f} · {} of {} runs "
        "finished",
        *name, stat.mean, stat.sd, stat.min, stat.max, hovered.completed,
        hovered.runs);
    SandboxText::tooltipText(tip.c_str());
  }

  /** A swatch and the value for each "by" value, wrapping to the width. */
  void legend(ImDrawList& draw, float width) const
  {
    if (!spec->by)
    {
      return;
    }
    const float scale = Theme::scale();
    float used = 0;
    for (std::size_t group = 0; group < groups.size(); ++group)
    {
      const std::string label =
          measure->valueText(spec->by->parameter, groups[group]);
      const float labelWidth =
          ImGui::CalcTextSize(label.c_str()).x + (30 * scale);
      if (used > 0 && used + labelWidth <= width)
      {
        ImGui::SameLine(0, LEGEND_GAP * scale);
      }
      else
      {
        used = 0;
      }
      const ImVec2 pos = ImGui::GetCursorScreenPos();
      draw.AddRectFilled(ImVec2(pos.x, pos.y + (4 * scale)),
                         ImVec2(pos.x + (LEGEND_MARKER_SIZE * scale),
                                pos.y + (LEGEND_MARKER_SIZE * scale)),
                         CURVE_COLOURS.at(group % CURVE_COLOURS.size()));
      ImGui::Dummy(
          ImVec2(LEGEND_MARKER_ADVANCE * scale, ImGui::GetTextLineHeight()));
      ImGui::SameLine(0, 4 * scale);
      ImGui::TextUnformatted(label.c_str());
      used += labelWidth;
    }
  }

 private:
  /** Position of a sweep value along x: its value, or its place for
   * choices. */
  [[nodiscard]] double xOf(float value) const
  {
    if (!choiceX)
    {
      return value;
    }
    const auto found = std::ranges::find(spec->sweep.values, value);
    return static_cast<double>(found - spec->sweep.values.begin());
  }

  [[nodiscard]] float sx(double x) const
  {
    return left +
           (static_cast<float>((x - xMin) / (xMax - xMin)) * (right - left));
  }

  [[nodiscard]] float sy(double y) const
  {
    return bottom -
           (static_cast<float>((y - yMin) / (yMax - yMin)) * (bottom - top));
  }

  void drawYTicks(ImDrawList& draw) const
  {
    const float scale = Theme::scale();
    const double yStep = tickStep(yMax - yMin, 5);
    const double firstY = std::ceil(yMin / yStep) * yStep;
    const int yTicks =
        static_cast<int>(std::floor((yMax - firstY) / yStep)) + 1;
    for (int tick = 0; tick < yTicks; ++tick)
    {
      const double y = firstY + (static_cast<double>(tick) * yStep);
      draw.AddLine(ImVec2(left, sy(y)), ImVec2(right, sy(y)),
                   SandboxPageUI::RULE);
      const std::string label =
          std::format("{:g}", std::round(y / yStep) * yStep);
      const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
      draw.AddText(ImVec2(left - labelSize.x - (Y_LABEL_GAP * scale),
                          sy(y) - (labelSize.y * LABEL_ALIGNMENT)),
                   SandboxPageUI::MUTED, label.c_str());
    }
  }

  void xLabel(ImDrawList& draw, double x, const std::string& label) const
  {
    draw.AddLine(ImVec2(sx(x), top), ImVec2(sx(x), bottom),
                 SandboxPageUI::RULE);
    const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
    draw.AddText(ImVec2(sx(x) - (labelSize.x * LABEL_ALIGNMENT),
                        bottom + (X_LABEL_GAP * Theme::scale())),
                 SandboxPageUI::MUTED, label.c_str());
  }

  /** X ticks: every value (choices), or nice steps. */
  void drawXTicks(ImDrawList& draw) const
  {
    if (choiceX)
    {
      for (const float value : spec->sweep.values)
      {
        xLabel(draw, xOf(value),
               measure->valueText(spec->sweep.parameter, value));
      }
      return;
    }
    const double xStep = tickStep(xMax - xMin, 8);
    const double firstX = std::ceil(xMin / xStep) * xStep;
    const int xTicks =
        static_cast<int>(std::floor((xMax + X_SPAN_EPSILON - firstX) / xStep)) +
        1;
    for (int tick = 0; tick < xTicks; ++tick)
    {
      const double x = firstX + (static_cast<double>(tick) * xStep);
      xLabel(draw, x, std::format("{:g}", std::round(x / xStep) * xStep));
    }
  }

  /** A group's points with the metric, in order along x. */
  [[nodiscard]] std::vector<const MeasurePoint*> curveOf(
      std::size_t group) const
  {
    std::vector<const MeasurePoint*> curve;
    for (const MeasurePoint& point : points)
    {
      if (point.by == groups[group] && point.stats.contains(*name))
      {
        curve.push_back(&point);
      }
    }
    std::ranges::sort(
        curve, {}, [this](const MeasurePoint* point) { return xOf(point->x); });
    return curve;
  }

  /** One group's curve with its spreads; returns its points, each where it
   * is on the screen. */
  std::vector<std::pair<ImVec2, const MeasurePoint*>> drawCurve(
      ImDrawList& draw, std::size_t group) const
  {
    const float scale = Theme::scale();
    const ImU32 colour = CURVE_COLOURS.at(group % CURVE_COLOURS.size());
    const std::vector<const MeasurePoint*> curve = curveOf(group);
    std::vector<std::pair<ImVec2, const MeasurePoint*>> marks;
    for (std::size_t index = 0; index < curve.size(); ++index)
    {
      const MeasureStat& stat = curve[index]->stats.at(*name);
      const ImVec2 at(sx(xOf(curve[index]->x)), sy(stat.mean));
      if (index > 0)
      {
        const MeasureStat& before = curve[index - 1]->stats.at(*name);
        draw.AddLine(ImVec2(sx(xOf(curve[index - 1]->x)), sy(before.mean)), at,
                     colour, CURVE_STROKE * scale);
      }
      if (stat.sd > 0.0)
      {
        draw.AddLine(ImVec2(at.x, sy(stat.mean - stat.sd)),
                     ImVec2(at.x, sy(stat.mean + stat.sd)), colour,
                     1.0F * scale);
      }
      draw.AddCircleFilled(at, POINT_RADIUS * scale, colour);
      marks.emplace_back(at, curve[index]);
    }
    return marks;
  }

  const DrillMeasure* measure;
  const MeasureSpec* spec;
  const std::string* name;
  std::vector<MeasurePoint> points;
  bool choiceX;
  std::vector<float> groups;
  double xMin = std::numeric_limits<double>::max();
  double xMax = std::numeric_limits<double>::lowest();
  double yMin = std::numeric_limits<double>::max();
  double yMax = std::numeric_limits<double>::lowest();
  float left = 0.0F;
  float right = 1.0F;
  float top = 0.0F;
  float bottom = 1.0F;
};
}  // namespace

void DrillMeasureScene::renderChart(ImVec2 size)
{
  MeasureChart chart(*measure,
                     measure->metricNames()[static_cast<std::size_t>(metric)]);
  if (!chart.findRanges())
  {
    return;
  }
  const float plotHeight =
      std::max(160 * Theme::scale(),
               size.y - (static_cast<float>(chart.legendRows(size.x)) *
                         ImGui::GetTextLineHeightWithSpacing()));
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##chart", ImVec2(size.x, plotHeight));
  const bool hovering = ImGui::IsItemHovered();
  ImDrawList& draw = *ImGui::GetWindowDrawList();
  chart.place(origin, ImVec2(size.x, plotHeight));
  chart.drawAxes(draw);
  const MeasurePoint* hovered = chart.drawCurves(draw);
  if ((hovered != nullptr) && hovering)
  {
    chart.tooltip(*hovered);
  }
  chart.legend(draw, size.x);
}

void DrillMeasureScene::renderTable(ImVec2 size)
{
  const MeasureSpec& spec = measure->spec();
  const auto& names = measure->metricNames();
  const int columns = 2 + (spec.by ? 1 : 0) + static_cast<int>(names.size());
  if (!ImGui::BeginTable("##table", columns,
                         ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                             ImGuiTableFlags_SizingFixedFit,
                         size))
  {
    return;
  }
  ImGui::TableSetupScrollFreeze(spec.by ? 2 : 1, 1);
  ImGui::TableSetupColumn(spec.sweep.parameter.c_str());
  if (spec.by)
  {
    ImGui::TableSetupColumn(spec.by->parameter.c_str());
  }
  ImGui::TableSetupColumn("Finished");
  for (const std::string& name : names)
  {
    ImGui::TableSetupColumn(name.c_str());
  }
  ImGui::PushFont(boldFont, HEADER_FONT_SIZE);
  ImGui::TableHeadersRow();
  ImGui::PopFont();
  for (const MeasurePoint& point : measure->points())
  {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(
        measure->valueText(spec.sweep.parameter, point.x).c_str());
    if (spec.by)
    {
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(
          measure->valueText(spec.by->parameter, point.by).c_str());
    }
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(
        std::format("{} / {}", point.completed, point.runs).c_str());
    for (std::size_t index = 0; index < names.size(); ++index)
    {
      ImGui::TableNextColumn();
      const auto found = point.stats.find(names[index]);
      if (found == point.stats.end())
      {
        SandboxText::disabledText("-");
        continue;
      }
      const MeasureStat& stat = found->second;
      if (stat.count > 1 && stat.sd >= DISPLAYED_SD_THRESHOLD)
      {
        ImGui::TextUnformatted(std::format("{:.2f} ± {:.2f}",
                                           static_cast<double>(stat.mean),
                                           static_cast<double>(stat.sd))
                                   .c_str());
      }
      else
      {
        ImGui::TextUnformatted(
            std::format("{:.2f}", static_cast<double>(stat.mean)).c_str());
      }
      if (static_cast<int>(index) == metric)
      {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                               IM_COL32(155, 208, 107, 40));
      }
    }
  }
  ImGui::EndTable();
}

bool DrillMeasureScene::renderActions(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  const bool running = measure && !measure->done();
  Page::header("Measure", drill.name(), width, scale, boldFont, regularFont,
               "Sweep one setting and compare a second. Engine conditions come "
               "from Set-up.",
               true);
  Page::beginCard("##measure_actions", "RUN MEASURE", width, Page::DARK, scale,
                  boldFont);
  if (Page::button("Back to sandbox", false, scale, boldFont))
  {
    if (measure)
    {
      measure->stop();
    }
    guiView->popScene();
  }
  if (!running)
  {
    if (Page::button("Run", true, scale, boldFont))
    {
      start();
    }
  }
  else if (Page::button("Stop", false, scale, boldFont))
  {
    measure->stop();
  }
  nextButton("Export CSV", scale, boldFont);
  const bool canExport = measure && measure->done() && !measure->runs().empty();
  ImGui::BeginDisabled(!canExport);
  if (Page::button("Export CSV", false, scale, boldFont))
  {
    exportCsv();
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
  {
    SandboxText::tooltipText(
        "Export every measured run, settings, seed and metrics to "
        "Documents/Player12 drill results. Available when the measure is "
        "finished or stopped.");
  }
  renderRunStatus();
  Page::endCard(scale);
  return running;
}

void DrillMeasureScene::renderRunStatus()
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  if (measure)
  {
    ImGui::TextUnformatted(std::format("{} / {} runs / {:.2f} s",
                                       measure->runsDone(),
                                       measure->runsTotal(), measure->seconds())
                               .c_str());
    const float share =
        measure->runsTotal() == 0
            ? 1
            : float(measure->runsDone()) / float(measure->runsTotal());
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Page::LIME);
    ImGui::ProgressBar(share, ImVec2(-1, PROGRESS_HEIGHT * scale), "");
    ImGui::PopStyleColor();
    ImGui::PushTextWrapPos(0);
    for (const auto& note : measure->warnings())
    {
      SandboxText::coloredText(NOTE_COLOR, note.c_str());
    }
    ImGui::PopTextWrapPos();
  }
  if (!exported.empty())
  {
    SandboxText::wrappedText(exported.c_str());
  }
}

void DrillMeasureScene::renderAxisSetting(
    const std::vector<DrillParameter>& parameters, AxisSetup& axis,
    bool optional, int exclude)
{
  const char* current =
      axis.parameter >= 0 &&
              axis.parameter < static_cast<int>(parameters.size())
          ? parameters[static_cast<std::size_t>(axis.parameter)].name
          : "(none)";
  field("Setting");
  if (SandboxPageUI::beginCombo("##setting", current))
  {
    if (optional && ImGui::Selectable("(none)", axis.parameter < 0))
    {
      axis.parameter = -1;
    }
    for (std::size_t index = 0; index < parameters.size(); ++index)
    {
      if (static_cast<int>(index) == exclude)
      {
        continue;
      }
      if (ImGui::Selectable(parameters[index].name,
                            axis.parameter == static_cast<int>(index)))
      {
        axis.parameter = static_cast<int>(index);
        resetAxis(axis);
      }
    }
    ImGui::EndCombo();
  }
}
