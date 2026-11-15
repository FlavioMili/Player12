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

namespace
{
/** Real time spent measuring per frame. */
constexpr double WORK_PER_FRAME_SECONDS = 0.012;

constexpr std::array<ImU32, 6> CURVE_COLOURS{
    IM_COL32(255, 200, 60, 255),  IM_COL32(90, 170, 255, 255),
    IM_COL32(120, 220, 120, 255), IM_COL32(240, 100, 100, 255),
    IM_COL32(200, 130, 255, 255), IM_COL32(240, 240, 240, 255)};

void field(const char* label)
{
  ImGui::TextWrapped("%s", label);
  ImGui::SetNextItemWidth(-1);
}

void nextButton(const char* label, float scale, ImFont* font)
{
  if (ImGui::GetItemRectMax().x + 6 * scale +
          SandboxPageUI::buttonWidth(label, scale, font) <=
      ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
    ImGui::SameLine(0, 6 * scale);
}

int findParameter(const std::vector<DrillParameter>& parameters,
                  std::string_view name)
{
  for (std::size_t index = 0; index < parameters.size(); ++index)
    if (name == parameters[index].name) return static_cast<int>(index);
  return -1;
}

/** "Nice" tick spacing for a range: 1, 2 or 5 times a power of ten. */
double tickStep(double range, int ticks)
{
  const double raw = range / std::max(1, ticks);
  const double power = std::pow(10.0, std::floor(std::log10(raw)));
  for (const double step : {1.0, 2.0, 5.0, 10.0})
    if (raw <= step * power) return step * power;
  return 10.0 * power;
}
}  // namespace

DrillMeasureScene::DrillMeasureScene(GUIView* guiView_ptr, Drill& drill_ref,
                                     bool runNow)
    : GUIScene(guiView_ptr), drill(drill_ref)
{
  // A first measure worth running: Pace, with and without the ball.
  const auto parameters = drill.parameters();
  sweep.parameter = findParameter(parameters, "Pace");
  if (sweep.parameter < 0) sweep.parameter = 0;
  resetAxis(sweep);
  if (parameters[static_cast<std::size_t>(sweep.parameter)].name ==
      std::string_view("Pace"))
  {
    sweep.from = 40.0f;
    sweep.to = 95.0f;
    sweep.count = 12;
  }
  by.parameter = findParameter(parameters, "With the ball");
  if (by.parameter >= 0) resetAxis(by);
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
      if (std::string_view(existing->GetDebugName()).find(file) !=
              std::string_view::npos &&
          !existing->Sources.empty() &&
          existing->Sources[0]->ExtraSizeScale == config.ExtraSizeScale)
        return existing;
    return atlas->AddFontFromFileTTF(
        AssetPaths::file((std::string("assets/fonts/") + file).c_str()).c_str(),
        size, &config);
  };
  regularFont = font("Manrope-Regular.ttf", 16);
  boldFont = font("Manrope-Bold.ttf", 14);
}

void DrillMeasureScene::update(float /*deltaTime*/)
{
  if (runPending)
  {
    runPending = false;
    start();
  }
  if (measure && !measure->done()) measure->work(WORK_PER_FRAME_SECONDS);
}

void DrillMeasureScene::resetAxis(AxisSetup& axis)
{
  const auto parameters = drill.parameters();
  if (axis.parameter < 0 ||
      axis.parameter >= static_cast<int>(parameters.size()))
    return;
  const DrillParameter& parameter =
      parameters[static_cast<std::size_t>(axis.parameter)];
  axis.from = parameter.min;
  axis.to = parameter.max;
  axis.count = parameter.choices.empty()
                   ? 6
                   : static_cast<int>(headlessChoices(parameter).size());
}

std::optional<MeasureAxis> DrillMeasureScene::axisOf(const AxisSetup& axis)
{
  const auto parameters = drill.parameters();
  if (axis.parameter < 0 ||
      axis.parameter >= static_cast<int>(parameters.size()))
    return std::nullopt;
  const DrillParameter& parameter =
      parameters[static_cast<std::size_t>(axis.parameter)];
  MeasureAxis out{parameter.name, {}};
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
  if (!sweepAxis) return;
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
  if (!measure) return;
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
                      ImVec2(24 * scale, 24 * scale));
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
  const float gutter = 6 * scale + ImGui::GetStyle().ScrollbarSize;
  const bool columns = available.x >= 1000 * scale;
  const float panel = columns
                          ? (available.x >= 1400 * scale ? 440 : 400) * scale
                          : available.x - gutter;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  if (columns)
  {
    ImGui::BeginChild("##measure_controls", ImVec2(panel + gutter, available.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderPanel(panel);
    ImGui::EndChild();
    ImGui::SameLine(0, 6 * scale);
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("##measure_results", ImVec2(width, available.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderResults(std::max(scale, width - gutter), available.y);
    ImGui::EndChild();
  }
  else
  {
    ImGui::BeginChild("##measure_page", available,
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderPanel(panel);
    renderResults(panel, available.y);
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
  const char* current =
      axis.parameter >= 0 &&
              axis.parameter < static_cast<int>(parameters.size())
          ? parameters[static_cast<std::size_t>(axis.parameter)].name
          : "(none)";
  field("Setting");
  if (ImGui::BeginCombo("##setting", current))
  {
    if (optional && ImGui::Selectable("(none)", axis.parameter < 0))
      axis.parameter = -1;
    for (std::size_t index = 0; index < parameters.size(); ++index)
    {
      if (static_cast<int>(index) == exclude) continue;
      if (ImGui::Selectable(parameters[index].name,
                            axis.parameter == static_cast<int>(index)))
      {
        axis.parameter = static_cast<int>(index);
        resetAxis(axis);
      }
    }
    ImGui::EndCombo();
  }
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
      ImGui::SliderInt("##count", &axis.count, 2, 25);
    }
    else
    {
      std::string all;
      for (const float value : headlessChoices(parameter))
        all += std::string(all.empty() ? "" : ", ") +
               parameter.choices[static_cast<std::size_t>(value)];
      ImGui::PushTextWrapPos(0);
      ImGui::TextDisabled("All of: %s", all.c_str());
      ImGui::PopTextWrapPos();
    }
  }
  ImGui::PopID();
}

void DrillMeasureScene::renderPanel(float width)
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
    if (measure) measure->stop();
    guiView->popScene();
  }
  if (!running)
  {
    if (Page::button("Run", true, scale, boldFont)) start();
  }
  else if (Page::button("Stop", false, scale, boldFont))
    measure->stop();
  nextButton("Export CSV", scale, boldFont);
  const bool canExport = measure && measure->done() && !measure->runs().empty();
  ImGui::BeginDisabled(!canExport);
  if (Page::button("Export CSV", false, scale, boldFont)) exportCsv();
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip(
        "Export every measured run, settings, seed and metrics to "
        "Documents/Player12 drill results. Available when the measure is "
        "finished or stopped.");
  if (measure)
  {
    ImGui::Text("%zu / %zu runs / %.2f s", measure->runsDone(),
                measure->runsTotal(), measure->seconds());
    const float share =
        measure->runsTotal() == 0
            ? 1
            : float(measure->runsDone()) / float(measure->runsTotal());
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Page::LIME);
    ImGui::ProgressBar(share, ImVec2(-1, 8 * scale), "");
    ImGui::PopStyleColor();
    ImGui::PushTextWrapPos(0);
    for (const auto& note : measure->warnings())
      ImGui::TextColored(ImVec4(1, .75f, .3f, 1), "%s", note.c_str());
    ImGui::PopTextWrapPos();
  }
  if (!exported.empty()) ImGui::TextWrapped("%s", exported.c_str());
  Page::endCard(scale);
  Page::beginCard("##measure_tabs", "CONTROLS", width, Page::MID, scale,
                  boldFont);
  const std::array<const char*, 4> tabs{"Sweep", "Compare", "Runs", "Settings"};
  for (int tab = 0; tab < 4; ++tab)
  {
    if (tab > 0) ImGui::SameLine(0, 6 * scale);
    if (Page::button(tabs[tab], panelTab == tab, scale, boldFont))
      panelTab = tab;
  }
  Page::endCard(scale);
  Page::beginCard(panelTab == 0   ? "##measure_sweep"
                  : panelTab == 1 ? "##measure_compare"
                  : panelTab == 2 ? "##measure_runs"
                                  : "##measure_settings",
                  panelTab == 0   ? "SWEEP"
                  : panelTab == 1 ? "COMPARE BY"
                  : panelTab == 2 ? "RUN SETTINGS"
                                  : "HELD SETTINGS",
                  width, Page::LIGHT, scale, boldFont);
  ImGui::BeginDisabled(running);
  if (panelTab == 0)
    renderAxis("Sweep", sweep, false, by.parameter);
  else if (panelTab == 1)
    renderAxis("Compare", by, true, sweep.parameter);
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
      ImGui::SliderInt("##repeats", &repeats, 1, 50);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Runs per point, on consecutive seeds from the first. Seeds affect "
            "the course and engine randomness.");
      ImGui::TableNextColumn();
      ImGui::Dummy(ImVec2(0, 0));
      field("First seed");
      ImGui::InputInt("##first_seed", &firstSeed);
      ImGui::EndTable();
    }
  }
  else
  {
    ImGui::TextWrapped("%s", drill.summary());
    ImGui::TextWrapped(
        "Shared with Watch. Sweep and comparison axes override these settings "
        "for each sample.");
    drill.renderSettings(width >= 360 * scale ? 2 : 1, false, true);
  }
  ImGui::EndDisabled();
  Page::endCard(scale);
}

void DrillMeasureScene::renderResults(float width, float height)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##measure_result_controls", "RESULTS", width, Page::MID,
                  scale, boldFont);
  if (!measure || measure->metricNames().empty())
  {
    ImGui::TextWrapped("%s", measure ? "Measuring..."
                                     : "Choose a sweep and press Run to see "
                                       "measured curves and results.");
    Page::endCard(scale);
    return;
  }
  const auto& names = measure->metricNames();
  metric = std::clamp(metric, 0, int(names.size()) - 1);
  ImGui::TextUnformatted("Result");
  ImGui::SetNextItemWidth(
      std::min(440 * scale, ImGui::GetContentRegionAvail().x));
  if (ImGui::BeginCombo("##metric", names[metric].c_str()))
  {
    for (std::size_t index = 0; index < names.size(); ++index)
      if (ImGui::Selectable(names[index].c_str(), int(index) == metric))
        metric = int(index);
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", names[metric].c_str());
  ImGui::TextWrapped(
      "Mean of the repeats; error bars span one standard deviation either "
      "side.");
  const std::array<const char*, 3> views{"Chart", "Table", "Both"};
  for (int view = 0; view < 3; ++view)
  {
    if (view > 0) ImGui::SameLine(0, 6 * scale);
    if (Page::button(views[view], resultView == view, scale, boldFont))
      resultView = view;
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
        std::max(200 * scale, (resultView == 2 ? available * .55f : available) -
                                  56 * scale)));
    Page::endCard(scale);
  }
  if (resultView != 0)
  {
    const float tableHeight =
        std::max(160 * scale, height - ImGui::GetCursorPosY() - 62 * scale);
    Page::beginCard("##measure_table", "MEASURED RESULTS", width, Page::LIGHT,
                    scale, boldFont);
    renderTable(ImVec2(
        ImGui::GetContentRegionAvail().x,
        tableHeight));
    Page::endCard(scale);
  }
}

void DrillMeasureScene::renderChart(ImVec2 size)
{
  const MeasureSpec& spec = measure->spec();
  const std::string& name =
      measure->metricNames()[static_cast<std::size_t>(metric)];
  const std::vector<MeasurePoint> points = measure->points();
  const bool choiceX = !std::ranges::all_of(
      spec.sweep.values,
      [&](float value)
      {
        return measure->valueText(spec.sweep.parameter, value) ==
               std::format("{:g}", value);
      });

  // Position of a sweep value along x: its value, or its place for choices.
  const auto xOf = [&](float value) -> double
  {
    if (!choiceX) return value;
    const auto found = std::ranges::find(spec.sweep.values, value);
    return static_cast<double>(found - spec.sweep.values.begin());
  };
  double xMin = std::numeric_limits<double>::max();
  double xMax = std::numeric_limits<double>::lowest();
  for (const float value : spec.sweep.values)
  {
    xMin = std::min(xMin, xOf(value));
    xMax = std::max(xMax, xOf(value));
  }
  double yMin = std::numeric_limits<double>::max();
  double yMax = std::numeric_limits<double>::lowest();
  for (const MeasurePoint& point : points)
    if (const auto found = point.stats.find(name); found != point.stats.end())
    {
      yMin = std::min(yMin, found->second.mean - found->second.sd);
      yMax = std::max(yMax, found->second.mean + found->second.sd);
    }
  if (yMin > yMax) return;
  if (xMax - xMin < 1e-9)
  {
    xMin -= 1.0;
    xMax += 1.0;
  }
  const double yPad =
      std::max((yMax - yMin) * 0.1, 0.05 * std::abs(yMax) + 1e-3);
  yMin -= yPad;
  yMax += yPad;
  if (choiceX)
  {
    xMin -= 0.5;
    xMax += 0.5;
  }

  // One curve per "by" value.
  std::vector<float> groups;
  for (const MeasurePoint& point : points)
    if (std::ranges::find(groups, point.by) == groups.end())
      groups.push_back(point.by);
  float legendUsed = 0;
  int legendRows = spec.by ? 1 : 0;
  for (float group : groups)
    if (spec.by)
    {
      const float width =
          ImGui::CalcTextSize(
              measure->valueText(spec.by->parameter, group).c_str())
              .x +
          30 * Theme::scale();
      if (legendUsed > 0 && legendUsed + width > size.x)
      {
        ++legendRows;
        legendUsed = 0;
      }
      legendUsed += width;
    }
  const float plotHeight =
      std::max(160 * Theme::scale(),
               size.y - legendRows * ImGui::GetTextLineHeightWithSpacing());
  const float scale = Theme::scale();
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##chart", ImVec2(size.x, plotHeight));
  const bool hovering = ImGui::IsItemHovered();
  ImDrawList& draw = *ImGui::GetWindowDrawList();
  const float left = origin.x + 56.0f * scale;
  const float right = origin.x + size.x - 12.0f * scale;
  const float top = origin.y + 10.0f * scale;
  const float bottom = origin.y + plotHeight - 34.0f * scale;
  const auto sx = [&](double x)
  {
    return left +
           static_cast<float>((x - xMin) / (xMax - xMin)) * (right - left);
  };
  const auto sy = [&](double y)
  {
    return bottom -
           static_cast<float>((y - yMin) / (yMax - yMin)) * (bottom - top);
  };
  draw.AddRectFilled(ImVec2(left, top), ImVec2(right, bottom),
                     SandboxPageUI::DARK);
  const ImU32 grid = SandboxPageUI::RULE;
  const ImU32 text = SandboxPageUI::MUTED;
  // Y ticks.
  const double yStep = tickStep(yMax - yMin, 5);
  for (double y = std::ceil(yMin / yStep) * yStep; y <= yMax; y += yStep)
  {
    draw.AddLine(ImVec2(left, sy(y)), ImVec2(right, sy(y)), grid);
    const std::string label =
        std::format("{:g}", std::round(y / yStep) * yStep);
    const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
    draw.AddText(
        ImVec2(left - labelSize.x - 6.0f * scale, sy(y) - labelSize.y * 0.5f),
        text, label.c_str());
  }
  // X ticks: every value (choices), or nice steps.
  const auto xLabel = [&](double x, const std::string& label)
  {
    draw.AddLine(ImVec2(sx(x), top), ImVec2(sx(x), bottom), grid);
    const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
    draw.AddText(ImVec2(sx(x) - labelSize.x * 0.5f, bottom + 4.0f * scale),
                 text, label.c_str());
  };
  if (choiceX)
  {
    for (const float value : spec.sweep.values)
      xLabel(xOf(value), measure->valueText(spec.sweep.parameter, value));
  }
  else
  {
    const double xStep = tickStep(xMax - xMin, 8);
    for (double x = std::ceil(xMin / xStep) * xStep; x <= xMax + 1e-9;
         x += xStep)
      xLabel(x, std::format("{:g}", std::round(x / xStep) * xStep));
  }
  const ImVec2 axisSize = ImGui::CalcTextSize(spec.sweep.parameter.c_str());
  draw.AddText(ImVec2((left + right - axisSize.x) * 0.5f,
                      bottom + 4.0f * scale + axisSize.y),
               SandboxPageUI::MUTED, spec.sweep.parameter.c_str());

  // One curve per comparison value.
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const MeasurePoint* hovered = nullptr;
  float hoveredDistance = 10.0f * scale;
  for (std::size_t group = 0; group < groups.size(); ++group)
  {
    const ImU32 colour = CURVE_COLOURS[group % CURVE_COLOURS.size()];
    std::vector<const MeasurePoint*> curve;
    for (const MeasurePoint& point : points)
      if (point.by == groups[group] && point.stats.contains(name))
        curve.push_back(&point);
    std::ranges::sort(curve, {},
                      [&](const MeasurePoint* point) { return xOf(point->x); });
    for (std::size_t index = 0; index < curve.size(); ++index)
    {
      const MeasureStat& stat = curve[index]->stats.at(name);
      const ImVec2 at(sx(xOf(curve[index]->x)), sy(stat.mean));
      if (index > 0)
      {
        const MeasureStat& before = curve[index - 1]->stats.at(name);
        draw.AddLine(ImVec2(sx(xOf(curve[index - 1]->x)), sy(before.mean)), at,
                     colour, 2.0f * scale);
      }
      if (stat.sd > 0.0)
        draw.AddLine(ImVec2(at.x, sy(stat.mean - stat.sd)),
                     ImVec2(at.x, sy(stat.mean + stat.sd)), colour,
                     1.0f * scale);
      draw.AddCircleFilled(at, 3.5f * scale, colour);
      const float distance = std::hypot(mouse.x - at.x, mouse.y - at.y);
      if (distance < hoveredDistance)
      {
        hoveredDistance = distance;
        hovered = curve[index];
      }
    }
  }
  if (hovered && hovering)
  {
    const MeasureStat& stat = hovered->stats.at(name);
    std::string tip =
        std::format("{} {}", spec.sweep.parameter,
                    measure->valueText(spec.sweep.parameter, hovered->x));
    if (spec.by)
      tip += std::format(" · {} {}", spec.by->parameter,
                         measure->valueText(spec.by->parameter, hovered->by));
    tip += std::format(
        "\n{}: {:.3f} ± {:.3f}\nrange {:.3f} – {:.3f} · {} of {} runs "
        "finished",
        name, stat.mean, stat.sd, stat.min, stat.max, hovered->completed,
        hovered->runs);
    ImGui::SetTooltip("%s", tip.c_str());
  }
  float used = 0;
  if (spec.by)
    for (std::size_t group = 0; group < groups.size(); ++group)
    {
      const std::string label =
          measure->valueText(spec.by->parameter, groups[group]);
      const float width = ImGui::CalcTextSize(label.c_str()).x + 30 * scale;
      if (used > 0 && used + width <= size.x)
        ImGui::SameLine(0, 12 * scale);
      else
        used = 0;
      const ImVec2 pos = ImGui::GetCursorScreenPos();
      draw.AddRectFilled(ImVec2(pos.x, pos.y + 4 * scale),
                         ImVec2(pos.x + 12 * scale, pos.y + 12 * scale),
                         CURVE_COLOURS[group % CURVE_COLOURS.size()]);
      ImGui::Dummy(ImVec2(14 * scale, ImGui::GetTextLineHeight()));
      ImGui::SameLine(0, 4 * scale);
      ImGui::TextUnformatted(label.c_str());
      used += width;
    }
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
    return;
  ImGui::TableSetupScrollFreeze(spec.by ? 2 : 1, 1);
  ImGui::TableSetupColumn(spec.sweep.parameter.c_str());
  if (spec.by) ImGui::TableSetupColumn(spec.by->parameter.c_str());
  ImGui::TableSetupColumn("Finished");
  for (const std::string& name : names) ImGui::TableSetupColumn(name.c_str());
  ImGui::PushFont(boldFont, 14);
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
    ImGui::Text("%d / %d", point.completed, point.runs);
    for (std::size_t index = 0; index < names.size(); ++index)
    {
      ImGui::TableNextColumn();
      const auto found = point.stats.find(names[index]);
      if (found == point.stats.end())
      {
        ImGui::TextDisabled("-");
        continue;
      }
      const MeasureStat& stat = found->second;
      if (stat.count > 1 && stat.sd >= 0.0005)
        ImGui::Text("%.2f ± %.2f", stat.mean, stat.sd);
      else
        ImGui::Text("%.2f", stat.mean);
      if (static_cast<int>(index) == metric)
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                               IM_COL32(155, 208, 107, 40));
    }
  }
  ImGui::EndTable();
}
