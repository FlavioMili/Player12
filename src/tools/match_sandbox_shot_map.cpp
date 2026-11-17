// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_shot_map.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <numbers>
#include <string_view>

#include "controller/game_controller.h"
#include "global/paths.h"
#include "gui/gui_view.h"
#include "gui/widgets/theme.h"
#include "tools/match_sandbox_page_ui.h"

namespace
{
constexpr const char* LINE = "Out from goal line (m)";
constexpr const char* SIDE = "Off centre (m)";

constexpr double WORK_PER_FRAME_SECONDS = 0.012;
constexpr double PERCENT_SCALE = 100.0;
/** Edges of the calibration bins (engine xG). */
constexpr std::array<double, 10> BIN_EDGES{0.0, 0.02, 0.05, 0.1, 0.15,
                                           0.2, 0.3,  0.4,  0.6, 1.0};
/** Most shots drawn in the choice-mode scatter. */
constexpr std::size_t MAX_SCATTER = 3000;

enum View : std::uint8_t
{
  CONVERSION,
  ENGINE_XG,
  DIFFERENCE,
  ON_TARGET,
  SHOT_TAKEN,
  VIEW_COUNT
};
constexpr std::array<const char*, VIEW_COUNT> VIEW_NAMES{
    "Conversion (goals per shot)", "Engine xG (mean per shot)",
    "Conversion minus xG", "On target (per shot)",
    "Shot taken (per run; choice mode)"};

double metricOf(const MeasureRun& run, std::string_view name, double fallback)
{
  for (const DrillMetric& metric : run.metrics)
  {
    if (metric.name == name)
    {
      return metric.value;
    }
  }
  return fallback;
}

std::optional<double> cellValue(const ShotMapCell& cell, int view)
{
  if (view == SHOT_TAKEN)
  {
    return cell.runs > 0
               ? std::optional(static_cast<double>(cell.shots) / cell.runs)
               : std::nullopt;
  }
  if (cell.shots == 0)
  {
    return std::nullopt;
  }
  const double conversion = static_cast<double>(cell.goals) / cell.shots;
  const double xg = cell.xg / cell.shots;
  switch (view)
  {
    case CONVERSION:
      return conversion;
    case ENGINE_XG:
      return xg;
    case DIFFERENCE:
      return conversion - xg;
    case ON_TARGET:
      return static_cast<double>(cell.onTarget) / cell.shots;
    default:
      return std::nullopt;
  }
}

/** Sequential scale (dark blue, teal, yellow) for 0..1, or diverging
 * (blue below zero, red above) for the difference. */
ImU32 heatColour(double value, bool diverging)
{
  if (diverging)
  {
    constexpr double FULL_DIFFERENCE = 0.3;
    constexpr int NEUTRAL_CHANNEL = 235;
    constexpr int COOL_RED_OFFSET = 185;
    constexpr int COOL_GREEN_OFFSET = 105;
    constexpr int WARM_CHANNEL_OFFSET = 175;
    constexpr int DIFFERENCE_ALPHA = 220;
    const float t =
        static_cast<float>(std::clamp(value / FULL_DIFFERENCE, -1.0, 1.0));
    if (t < 0.0F)
    {
      return IM_COL32(
          static_cast<int>(NEUTRAL_CHANNEL + (COOL_RED_OFFSET * t)),
          static_cast<int>(NEUTRAL_CHANNEL + (COOL_GREEN_OFFSET * t)),
          NEUTRAL_CHANNEL, DIFFERENCE_ALPHA);
    }
    return IM_COL32(
        NEUTRAL_CHANNEL,
        static_cast<int>(NEUTRAL_CHANNEL - (WARM_CHANNEL_OFFSET * t)),
        static_cast<int>(NEUTRAL_CHANNEL - (WARM_CHANNEL_OFFSET * t)),
        DIFFERENCE_ALPHA);
  }
  constexpr std::array<float, 3> LOW{30, 40, 90};
  constexpr std::array<float, 3> MID{30, 150, 140};
  constexpr std::array<float, 3> HIGH{250, 220, 60};
  constexpr float MIDPOINT_SHARE = 0.5F;
  constexpr float HALF_SCALE = 1.0F / MIDPOINT_SHARE;
  constexpr int HEAT_ALPHA = 225;
  const float t = static_cast<float>(std::clamp(value, 0.0, 1.0));
  const float s = std::sqrt(t);  // more contrast at the low end
  const auto mix = [](float a, float b, float u) { return a + ((b - a) * u); };
  const auto rgb = [&]() -> std::array<float, 3>
  {
    if (s < MIDPOINT_SHARE)
    {
      const float u = s * HALF_SCALE;
      return {mix(LOW[0], MID[0], u), mix(LOW[1], MID[1], u),
              mix(LOW[2], MID[2], u)};
    }
    const float u = (s - MIDPOINT_SHARE) * HALF_SCALE;
    return {mix(MID[0], HIGH[0], u), mix(MID[1], HIGH[1], u),
            mix(MID[2], HIGH[2], u)};
  }();
  return IM_COL32(static_cast<int>(rgb[0]), static_cast<int>(rgb[1]),
                  static_cast<int>(rgb[2]), HEAT_ALPHA);
}

/** Dark text on light cells, light text on dark ones. */
ImU32 textColour(double value, bool diverging)
{
  constexpr double DARK_TEXT_THRESHOLD = 0.55;
  constexpr int DARK_TEXT_ALPHA = 220;
  constexpr int LIGHT_TEXT_ALPHA = 230;
  const bool light =
      diverging || std::sqrt(std::clamp(value, 0.0, 1.0)) > DARK_TEXT_THRESHOLD;
  return light ? IM_COL32(0, 0, 0, DARK_TEXT_ALPHA)
               : IM_COL32(255, 255, 255, LIGHT_TEXT_ALPHA);
}

std::optional<float> number(std::string_view text)
{
  float value = 0.0F;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc() || end != text.data() + text.size())
  {
    return std::nullopt;
  }
  return value;
}

/** "from:to:count" */
bool parseRange(const std::string& text, float& from, float& to, int& count)
{
  const std::size_t first = text.find(':');
  const std::size_t second = text.find(':', first + 1);
  if (first == std::string::npos || second == std::string::npos)
  {
    return false;
  }
  const auto a = number(std::string_view(text).substr(0, first));
  const auto b =
      number(std::string_view(text).substr(first + 1, second - first - 1));
  const auto c = number(std::string_view(text).substr(second + 1));
  if (!a || !b || !c || *c < 1.0F)
  {
    return false;
  }
  from = *a;
  to = *b;
  count = static_cast<int>(*c);
  return true;
}

MeasureSpec measureSpecOf(const ShotMapSpec& spec)
{
  MeasureSpec out;
  out.sweep = {.parameter = LINE,
               .values = spreadValues(spec.lineFrom, spec.lineTo,
                                      spec.lineCount, false)};
  out.by = MeasureAxis{.parameter = SIDE,
                       .values = spreadValues(spec.sideFrom, spec.sideTo,
                                              spec.sideCount, false)};
  out.repeats = spec.repeats;
  out.firstSeed = spec.firstSeed;
  out.fixed = spec.fixed;
  return out;
}
}  // namespace

// --- ShotMap -----------------------------------------------------------------

ShotMap::ShotMap(Drill& shotDrill, const StatsConfig& config,
                 const ShotMapSpec& spec)
    : setup(spec),
      measure(std::make_unique<DrillMeasure>(shotDrill, config,
                                             measureSpecOf(spec)))
{
}

float ShotMap::lineStep() const
{
  return setup.lineCount > 1 ? std::abs(setup.lineTo - setup.lineFrom) /
                                   static_cast<float>(setup.lineCount - 1)
                             : 3.0F;
}

float ShotMap::sideStep() const
{
  return setup.sideCount > 1 ? std::abs(setup.sideTo - setup.sideFrom) /
                                   static_cast<float>(setup.sideCount - 1)
                             : 3.0F;
}

void ShotMap::refresh()
{
  if (counted == measure->runs().size())
  {
    return;
  }
  counted = measure->runs().size();
  grid.clear();
  taken.clear();
  bins.clear();
  for (std::size_t edge = 0; edge + 1 < BIN_EDGES.size(); ++edge)
  {
    bins.push_back({.from = BIN_EDGES.at(edge),
                    .to = BIN_EDGES.at(edge + 1),
                    .shots = 0,
                    .goals = 0,
                    .xg = 0.0});
  }
  std::map<std::pair<float, float>, std::size_t> index;
  for (const MeasureRun& run : measure->runs())
  {
    if (run.metrics.empty())
    {
      continue;
    }
    const auto key = std::pair{run.x, run.by};
    auto found = index.find(key);
    if (found == index.end())
    {
      found = index.emplace(key, grid.size()).first;
      ShotMapCell cell;
      cell.line = run.x;
      cell.side = run.by;
      grid.push_back(cell);
    }
    ShotMapCell& cell = grid[found->second];
    ++cell.runs;
    if (metricOf(run, "Shot", 0.0) < 0.5)
    {
      continue;
    }
    const bool goal = metricOf(run, "Goal", 0.0) > 0.5;
    const double xg = metricOf(run, "xG", 0.0);
    ++cell.shots;
    cell.goals += goal ? 1 : 0;
    cell.onTarget += metricOf(run, "On target", 0.0) > 0.5 ? 1 : 0;
    cell.xg += xg;
    const auto line =
        static_cast<float>(metricOf(run, "Shot from goal line (m)", run.x));
    const auto side =
        static_cast<float>(metricOf(run, "Shot off centre (m)", run.by));
    cell.shotLine += line;
    cell.shotSide += side;
    if (taken.size() < MAX_SCATTER)
    {
      taken.push_back({.line = line, .side = side, .goal = goal});
    }
    for (CalibrationBin& bin : bins)
    {
      if (xg >= bin.from && (xg < bin.to || bin.to >= 1.0))
      {
        ++bin.shots;
        bin.goals += goal ? 1 : 0;
        bin.xg += xg;
        break;
      }
    }
  }
}

const std::vector<ShotMapCell>& ShotMap::cells()
{
  refresh();
  return grid;
}

const std::vector<CalibrationBin>& ShotMap::calibration()
{
  refresh();
  return bins;
}

const std::vector<ShotMapShot>& ShotMap::shots()
{
  refresh();
  return taken;
}

// --- Scene -------------------------------------------------------------------

ShotMapScene::ShotMapScene(GUIView* guiView_ptr, Drill& shotDrill, bool runNow)
    : GUIScene(guiView_ptr), drill(shotDrill), runPending(runNow)
{
  spec.firstSeed = drill.seed;
}

ShotMapScene::~ShotMapScene() = default;

SceneID ShotMapScene::getID() const { return SceneID::MATCH_SANDBOX; }

void ShotMapScene::onEnter()
{
  // Reuse the launch page's fonts. Direct tool entry loads them once into
  // the current atlas, so reopening Shot Map does not add font
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
  regularFont = font("Manrope-Regular.ttf", 16);
  boldFont = font("Manrope-Bold.ttf", 14);
}

void ShotMapScene::start()
{
  map.reset();
  map = std::make_unique<ShotMap>(
      drill, guiView->getController().getStatsConfig(), spec);
  exported.clear();
}

void ShotMapScene::update(float /*deltaTime*/)
{
  if (runPending)
  {
    runPending = false;
    start();
  }
  if (map && !map->done())
  {
    map->work(WORK_PER_FRAME_SECONDS);
  }
}

void ShotMapScene::exportCsv()
{
  if (!map)
  {
    return;
  }
  const auto path = measurePath(drill.name(), "spot grid");
  std::error_code ignored;
  std::filesystem::create_directories(path.parent_path(), ignored);
  std::ofstream file(path, std::ios::binary);
  file << map->runs().csv();
  exported =
      file ? "Saved " + path.string() : "Could not write " + path.string();
}

void ShotMapScene::render()
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
  ImGui::Begin("##shot_map", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float gutter = (6 * scale) + ImGui::GetStyle().ScrollbarSize;
  const bool columns = available.x >= 1000 * scale;
  const float panel =
      columns ? (available.x >= 1400 * scale ? 440.0F : 400.0F) * scale
              : available.x - gutter;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  if (columns)
  {
    ImGui::BeginChild("##shot_map_controls",
                      ImVec2(panel + gutter, available.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderPanel(panel);
    ImGui::EndChild();
    ImGui::SameLine(0, 6 * scale);
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("##shot_map_results", ImVec2(width, available.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderResults(std::max(scale, width - gutter), available.y);
    ImGui::EndChild();
  }
  else
  {
    ImGui::BeginChild("##shot_map_page", available,
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderPanel(panel);
    renderResults(panel, available.y);
    ImGui::EndChild();
  }
  ImGui::PopStyleVar();
  ImGui::End();
}

void ShotMapScene::renderPanel(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  const bool running = map && !map->done();
  Page::header(
      "Shot map", drill.name(), width, scale, boldFont, regularFont,
      "Conversion, engine xG and shooting choices over a grid of spots.", true);
  Page::beginCard("##shot_actions", "RUN SHOT MAP", width, Page::DARK, scale,
                  boldFont);
  if (Page::button("Back to sandbox", false, scale, boldFont))
  {
    if (map)
    {
      map->stop();
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
    map->stop();
  }
  ImGui::SameLine(0, 6 * scale);
  ImGui::BeginDisabled(!map || !map->done() || map->runs().runs().empty());
  if (Page::button("Export CSV", false, scale, boldFont))
  {
    exportCsv();
  }
  ImGui::EndDisabled();
  if (map)
  {
    const auto& runs = map->runs();
    ImGui::Text("%zu / %zu runs / %.1f s", runs.runsDone(), runs.runsTotal(),
                runs.seconds());
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Page::LIME);
    ImGui::ProgressBar((runs.runsTotal() != 0U)
                           ? float(runs.runsDone()) / float(runs.runsTotal())
                           : 1,
                       ImVec2(-1, 8 * scale), "");
    ImGui::PopStyleColor();
  }
  if (!exported.empty())
  {
    ImGui::TextWrapped("%s", exported.c_str());
  }
  Page::endCard(scale);
  Page::beginCard("##shot_tabs", "CONTROLS", width, Page::MID, scale, boldFont);
  constexpr std::array tabs{"Grid", "Runs", "Settings"};
  for (int i = 0; i < static_cast<int>(tabs.size()); ++i)
  {
    if (i != 0)
    {
      ImGui::SameLine(0, 6 * scale);
    }
    if (Page::button(tabs.at(i), panelTab == i, scale, boldFont))
    {
      panelTab = i;
    }
  }
  Page::endCard(scale);
  Page::beginCard("##shot_settings",
                  panelTab == 0   ? "SPOT GRID"
                  : panelTab == 1 ? "RUN SETTINGS"
                                  : "SHOT SETTINGS",
                  width, Page::LIGHT, scale, boldFont);
  ImGui::BeginDisabled(running);
  auto field = [](const char* label)
  {
    ImGui::TableNextColumn();
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::TextWrapped("%s", label);
    ImGui::SetNextItemWidth(-1);
  };
  if (panelTab < 2 && ImGui::BeginTable("##fields", 2,
                                        ImGuiTableFlags_SizingStretchSame |
                                            ImGuiTableFlags_RowBg |
                                            ImGuiTableFlags_BordersInnerH |
                                            ImGuiTableFlags_BordersInnerV))
  {
    if (panelTab == 0)
    {
      field("Nearest (m)");
      ImGui::SliderFloat("##near", &spec.lineFrom, 1, 40, "%.0f");
      field("Furthest (m)");
      ImGui::SliderFloat("##far", &spec.lineTo, 1, 45, "%.0f");
      field("Left (m)");
      ImGui::SliderFloat("##left", &spec.sideFrom, -30, 0, "%.0f");
      field("Right (m)");
      ImGui::SliderFloat("##right", &spec.sideTo, 0, 30, "%.0f");
      field("Rows");
      ImGui::SliderInt("##rows", &spec.lineCount, 2, 25);
      field("Columns");
      ImGui::SliderInt("##cols", &spec.sideCount, 1, 25);
    }
    else
    {
      field("Runs per spot");
      ImGui::SliderInt("##repeats", &spec.repeats, 1, 500);
      field("First seed");
      int seed = int(spec.firstSeed);
      if (ImGui::InputInt("##seed", &seed))
      {
        spec.firstSeed = std::uint32_t(std::max(0, seed));
      }
    }
    ImGui::EndTable();
  }
  if (panelTab == 2)
  {
    ImGui::TextWrapped(
        "The spot comes from the grid. Other settings are shared with Watch.");
    drill.renderSettings(width >= 360 * scale ? 2 : 1, false, true);
  }
  else
  {
    ImGui::TextWrapped("%d spots / %d runs", spec.lineCount * spec.sideCount,
                       spec.lineCount * spec.sideCount * spec.repeats);
  }
  ImGui::EndDisabled();
  Page::endCard(scale);
}
void ShotMapScene::renderResults(float width, float height)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##shot_results", "RESULTS", width, Page::MID, scale,
                  boldFont);
  if (!map)
  {
    ImGui::TextWrapped("Set the grid and press Run to build the shot map.");
    Page::endCard(scale);
    return;
  }
  ImGui::TextUnformatted("Colour by");
  ImGui::SetNextItemWidth(
      std::min(440 * scale, ImGui::GetContentRegionAvail().x));
  SandboxPageUI::combo("##colour", &view, VIEW_NAMES.data(), VIEW_COUNT);
  constexpr std::array tabs{"Map", "Calibration"};
  for (int i = 0; i < static_cast<int>(tabs.size()); ++i)
  {
    if (i != 0)
    {
      ImGui::SameLine(0, 6 * scale);
    }
    if (Page::button(tabs.at(i), resultTab == i, scale, boldFont))
    {
      resultTab = i;
    }
  }
  Page::endCard(scale);
  const float canvasHeight =
      std::max(320 * scale, height - ImGui::GetCursorPosY() - (60 * scale));
  Page::beginCard("##shot_canvas", resultTab == 0 ? "SHOT MAP" : "CALIBRATION",
                  width, Page::DARK, scale, boldFont);
  if (resultTab == 0)
  {
    renderMap(ImVec2(ImGui::GetContentRegionAvail().x,
                     std::max(220 * scale, canvasHeight - (54 * scale))));
  }
  else
  {
    renderCalibration(ImVec2(ImGui::GetContentRegionAvail().x, canvasHeight));
  }
  Page::endCard(scale);
}

void ShotMapScene::renderMap(ImVec2 size)
{
  // The attacking end, goal at the top: x across the pitch (off centre),
  // y down from the goal line.
  const float scale = Theme::scale();
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##map", size);
  ImDrawList& draw = *ImGui::GetWindowDrawList();
  const float depth =
      std::max(spec.lineFrom, spec.lineTo) + map->lineStep() + 2.0F;
  const float width = MatchTuning::Pitch::MATCH_WIDTH_METRES;
  const float margin = 24.0F * scale;
  const float ppm = std::min((size.x - margin * 2.0F) / width,
                             (size.y - margin * 2.0F) / depth);
  const float left = origin.x + ((size.x - width * ppm) * 0.5F);
  const float top = origin.y + margin;
  const auto at = [&](float side, float line)
  { return ImVec2(left + ((side + width * 0.5F) * ppm), top + (line * ppm)); };
  draw.AddRectFilled(at(-width * 0.5F, 0.0F), at(width * 0.5F, depth),
                     IM_COL32(34, 92, 46, 255));

  // Keep coarse edge cells inside the pitch.
  draw.PushClipRect(at(-width * 0.5F, 0.0F), at(width * 0.5F, depth), true);
  // Cells.
  const bool diverging = view == DIFFERENCE;
  const float halfLine = map->lineStep() * 0.5F;
  const float halfSide = map->sideStep() * 0.5F;
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const ShotMapCell* hovered = nullptr;
  for (const ShotMapCell& cell : map->cells())
  {
    const auto value = cellValue(cell, view);
    const ImVec2 a = at(cell.side - halfSide, cell.line - halfLine);
    const ImVec2 b = at(cell.side + halfSide, cell.line + halfLine);
    if (value)
    {
      draw.AddRectFilled(a, b, heatColour(*value, diverging));
      const std::string text =
          diverging ? std::format("{:+.0f}", *value * PERCENT_SCALE)
                    : std::format("{:.0f}", *value * PERCENT_SCALE);
      const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
      if (textSize.x < b.x - a.x - 2.0F)
      {
        draw.AddText(ImVec2((a.x + b.x - textSize.x) * 0.5F,
                            (a.y + b.y - textSize.y) * 0.5F),
                     textColour(*value, diverging), text.c_str());
      }
    }
    else
    {
      draw.AddRect(a, b, IM_COL32(255, 255, 255, 40));
    }
    if (mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y && mouse.y < b.y)
    {
      hovered = &cell;
    }
  }

  draw.PopClipRect();

  // Markings: goal line, box, six-yard box, spot, arc, goal.
  const ImU32 white = IM_COL32(255, 255, 255, 200);
  const float thick = 1.5F * scale;
  draw.AddLine(at(-width * 0.5F, 0.0F), at(width * 0.5F, 0.0F), white, thick);
  draw.AddRect(at(-20.16F, 0.0F), at(20.16F, 16.5F), white, 0.0F, 0, thick);
  draw.AddRect(at(-9.16F, 0.0F), at(9.16F, 5.5F), white, 0.0F, 0, thick);
  draw.AddCircleFilled(at(0.0F, 11.0F), 2.0F * scale, white);
  // The arc: the part of the 9.15 m circle round the spot outside the box.
  const float reach = std::acos(5.5F / 9.15F);
  draw.PathArcTo(at(0.0F, 11.0F), 9.15F * ppm,
                 (std::numbers::pi_v<float> * 0.5F) - reach,
                 (std::numbers::pi_v<float> * 0.5F) + reach, 24);
  draw.PathStroke(white, 0, thick);
  draw.AddRectFilled(at(-3.66F, -1.2F), at(3.66F, 0.0F),
                     IM_COL32(255, 255, 255, 120));

  // Choice mode: where the shots were actually taken.
  bool moved = false;
  for (const ShotMapCell& cell : map->cells())
  {
    if (cell.shots > 0 &&
        (std::abs((cell.shotLine / cell.shots) - cell.line) > 0.3 ||
         std::abs((cell.shotSide / cell.shots) - cell.side) > 0.3))
    {
      moved = true;
    }
  }
  if (moved || view == SHOT_TAKEN)
  {
    for (const ShotMapShot& shot : map->shots())
    {
      draw.AddCircleFilled(at(shot.side, shot.line), 1.6F * scale,
                           shot.goal ? IM_COL32(120, 255, 140, 220)
                                     : IM_COL32(255, 255, 255, 110));
    }
  }

  if ((hovered != nullptr) && ImGui::IsItemHovered())
  {
    std::string tip = std::format(
        "{:.1f} m out, {:+.1f} m off centre\n{} runs, "
        "{} shots, {} goals",
        hovered->line, hovered->side, hovered->runs, hovered->shots,
        hovered->goals);
    if (hovered->shots > 0)
    {
      tip += std::format(
          "\nconversion {:.1f}% · engine xG {:.1f}% · on target {:.0f}%"
          "\nshot from {:.1f} m out, {:+.1f} m (mean)",
          PERCENT_SCALE * hovered->goals / hovered->shots,
          PERCENT_SCALE * hovered->xg / hovered->shots,
          PERCENT_SCALE * hovered->onTarget / hovered->shots,
          hovered->shotLine / hovered->shots,
          hovered->shotSide / hovered->shots);
    }
    ImGui::SetTooltip("%s", tip.c_str());
  }
  ImGui::TextWrapped(
      "%s", std::format("{} (%; cells every {:.1f} x {:.1f} m{})",
                        VIEW_NAMES.at(static_cast<std::size_t>(view)),
                        map->lineStep(), map->sideStep(),
                        moved ? "; dots show shots taken, green = goal" : "")
                .c_str());
}

void ShotMapScene::renderCalibration(ImVec2 size)
{
  constexpr float CHART_HEIGHT_SHARE = 0.55F;
  constexpr float AXIS_MARGIN = 34.0F;
  constexpr float EDGE_MARGIN = 8.0F;
  constexpr double MIN_AXIS_LIMIT = 0.1;
  constexpr double AXIS_STEPS = 10.0;
  constexpr float X_LABEL_HALF_WIDTH = 6.0F;
  constexpr float X_LABEL_GAP = 3.0F;
  constexpr float Y_LABEL_WIDTH = 26.0F;
  constexpr float Y_LABEL_HALF_HEIGHT = 7.0F;
  constexpr ImU32 DIAGONAL_COLOUR = IM_COL32(255, 255, 255, 90);
  constexpr ImU32 ERROR_BAR_COLOUR = IM_COL32(255, 200, 60, 160);
  constexpr ImU32 POINT_COLOUR = IM_COL32(255, 200, 60, 230);
  constexpr float MIN_POINT_RADIUS = 2.5F;
  constexpr float POINT_RADIUS_RANGE = 5.0F;
  constexpr int BIN_COLUMNS = 5;
  if (!ImGui::BeginChild("##calibration", size))
  {
    ImGui::EndChild();
    return;
  }
  const auto& bins = map->calibration();
  int shots = 0;
  int goals = 0;
  double xg = 0.0;
  for (const CalibrationBin& bin : bins)
  {
    shots += bin.shots;
    goals += bin.goals;
    xg += bin.xg;
  }

  ImGui::Text("%d shots: %d goals against %.1f expected (%.2f×)", shots, goals,
              xg, xg > 0.0 ? goals / xg : 0.0);

  // Chart: engine xG (x) against the share scored (y); the diagonal is a
  // perfectly calibrated model.
  const float scale = Theme::scale();
  const float side =
      std::min(ImGui::GetContentRegionAvail().x,
               ImGui::GetContentRegionAvail().y * CHART_HEIGHT_SHARE);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##chart", ImVec2(side, side));
  ImDrawList& draw = *ImGui::GetWindowDrawList();
  const float pad = AXIS_MARGIN * scale;
  const float left = origin.x + pad;
  const float right = origin.x + side - (EDGE_MARGIN * scale);
  const float top = origin.y + (EDGE_MARGIN * scale);
  const float bottom = origin.y + side - pad;
  double maxValue = MIN_AXIS_LIMIT;
  for (const CalibrationBin& bin : bins)
  {
    if (bin.shots > 0)
    {
      maxValue = std::max({maxValue, bin.xg / bin.shots,
                           static_cast<double>(bin.goals) / bin.shots});
    }
  }
  maxValue = std::min(1.0, std::ceil(maxValue * AXIS_STEPS) / AXIS_STEPS);
  const auto sx = [&](double v)
  { return left + (static_cast<float>(v / maxValue) * (right - left)); };
  const auto sy = [&](double v)
  { return bottom - (static_cast<float>(v / maxValue) * (bottom - top)); };
  draw.AddRectFilled(ImVec2(left, top), ImVec2(right, bottom),
                     ImGui::GetColorU32(SandboxPageUI::DARK));
  const ImU32 grid = ImGui::GetColorU32(SandboxPageUI::RULE);
  const ImU32 text = ImGui::GetColorU32(SandboxPageUI::MUTED);
  constexpr int GRID_INTERVALS = 5;
  for (int line = 0; line <= GRID_INTERVALS; ++line)
  {
    const double v = (static_cast<double>(line) * maxValue) / GRID_INTERVALS;
    draw.AddLine(ImVec2(sx(v), top), ImVec2(sx(v), bottom), grid);
    draw.AddLine(ImVec2(left, sy(v)), ImVec2(right, sy(v)), grid);
    const std::string label = std::format("{:.0f}", v * PERCENT_SCALE);
    draw.AddText(ImVec2(sx(v) - (X_LABEL_HALF_WIDTH * scale),
                        bottom + (X_LABEL_GAP * scale)),
                 text, label.c_str());
    draw.AddText(ImVec2(left - (Y_LABEL_WIDTH * scale),
                        sy(v) - (Y_LABEL_HALF_HEIGHT * scale)),
                 text, label.c_str());
  }
  draw.AddLine(ImVec2(sx(0.0), sy(0.0)), ImVec2(sx(maxValue), sy(maxValue)),
               DIAGONAL_COLOUR, 1.0F * scale);
  int most = 1;
  for (const CalibrationBin& bin : bins)
  {
    most = std::max(most, bin.shots);
  }
  for (const CalibrationBin& bin : bins)
  {
    if (bin.shots == 0)
    {
      continue;
    }
    const double predicted = bin.xg / bin.shots;
    const double actual = static_cast<double>(bin.goals) / bin.shots;
    // Error bar: ± one binomial standard error of the share scored.
    const double error = std::sqrt(actual * (1.0 - actual) / bin.shots);
    const ImVec2 point(sx(predicted), sy(actual));
    draw.AddLine(ImVec2(point.x, sy(std::max(0.0, actual - error))),
                 ImVec2(point.x, sy(std::min(maxValue, actual + error))),
                 ERROR_BAR_COLOUR, 1.0F * scale);
    const float radius =
        (MIN_POINT_RADIUS +
         POINT_RADIUS_RANGE * std::sqrt(static_cast<float>(bin.shots) /
                                        static_cast<float>(most))) *
        scale;
    draw.AddCircleFilled(point, radius, POINT_COLOUR);
  }
  ImGui::TextWrapped(
      "Across: engine xG (%%). Up: scored (%%). Dot size represents shots.");

  if (ImGui::BeginTable("##bins", BIN_COLUMNS,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchSame))
  {
    ImGui::TableSetupColumn("Engine xG");
    ImGui::TableSetupColumn("Shots");
    ImGui::TableSetupColumn("Mean xG");
    ImGui::TableSetupColumn("Scored");
    ImGui::TableSetupColumn("Scored / xG");
    ImGui::TableHeadersRow();
    for (const CalibrationBin& bin : bins)
    {
      if (bin.shots == 0)
      {
        continue;
      }
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%.0f-%.0f%%", bin.from * PERCENT_SCALE,
                  bin.to * PERCENT_SCALE);
      ImGui::TableNextColumn();
      ImGui::Text("%d", bin.shots);
      ImGui::TableNextColumn();
      ImGui::Text("%.1f%%", PERCENT_SCALE * bin.xg / bin.shots);
      ImGui::TableNextColumn();
      ImGui::Text("%.1f%%", PERCENT_SCALE * bin.goals / bin.shots);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", bin.xg > 0.0 ? bin.goals / bin.xg : 0.0);
    }
    ImGui::EndTable();
  }
  ImGui::EndChild();
}

// --- Command line ------------------------------------------------------------

int runShotMapCommand(const StatsConfig& config,
                      const std::vector<std::string>& arguments)
{
  auto drills = makeDrills();
  Drill* shotDrill = nullptr;
  for (const auto& drill : drills)
  {
    if (drill->offersShotMap())
    {
      shotDrill = drill.get();
    }
  }
  if (shotDrill == nullptr)
  {
    return 2;
  }
  ShotMapSpec spec;
  bool csvAuto = false;
  std::optional<std::filesystem::path> csvPath;
  std::string error;
  for (std::size_t index = 0; index < arguments.size(); ++index)
  {
    const std::string& argument = arguments[index];
    const bool hasValue = index + 1 < arguments.size();
    const std::string value = hasValue ? arguments[index + 1] : std::string();
    if (argument == "--line" && hasValue)
    {
      if (!parseRange(value, spec.lineFrom, spec.lineTo, spec.lineCount))
      {
        error = "--line expects from:to:count";
      }
    }
    else if (argument == "--side" && hasValue)
    {
      if (!parseRange(value, spec.sideFrom, spec.sideTo, spec.sideCount))
      {
        error = "--side expects from:to:count";
      }
    }
    else if (argument == "--repeats" && hasValue)
    {
      spec.repeats = static_cast<int>(number(value).value_or(1.0F));
    }
    else if (argument == "--seed" && hasValue)
    {
      spec.firstSeed = static_cast<std::uint32_t>(number(value).value_or(1.0F));
    }
    else if (argument == "--set" && hasValue)
    {
      const std::size_t equals = value.find('=');
      const auto parsed =
          equals == std::string::npos
              ? std::nullopt
              : number(std::string_view(value).substr(equals + 1));
      std::string name = value.substr(0, equals);
      bool known = false;
      for (const DrillParameter& parameter : shotDrill->parameters())
      {
        if (name == parameter.name)
        {
          known = true;
        }
      }
      if (!parsed || !known)
      {
        error = std::format("--set \"{}\": expected Setting=value", value);
      }
      else
      {
        spec.fixed[name] = *parsed;
      }
    }
    else if (argument == "--csv")
    {
      if (hasValue && !value.starts_with("--"))
      {
        csvPath = value;
      }
      else
      {
        csvAuto = true;
        continue;
      }
    }
    else
    {
      error = std::format("unknown option \"{}\"", argument);
    }
    if (!error.empty())
    {
      break;
    }
    ++index;
  }
  if (!error.empty())
  {
    std::cerr << "Shot map: " << error << "\nShot settings:";
    for (const DrillParameter& parameter : shotDrill->parameters())
    {
      std::cerr << " \"" << parameter.name << '"';
    }
    std::cerr << '\n';
    return 2;
  }

  ShotMap map(*shotDrill, config, spec);
  std::cout << std::format("Shot map: {} × {} spots, {} runs each\n",
                           spec.lineCount, spec.sideCount, spec.repeats);
  for (const std::string& note : map.runs().warnings())
  {
    std::cout << note << '\n';
  }
  while (map.work(1.0))
  {
    std::cerr << std::format("  {} / {}\r", map.runs().runsDone(),
                             map.runs().runsTotal());
  }
  std::cout << std::format("Done in {:.2f} s.\n", map.runs().seconds());

  // Grids: rows by distance, columns by side.
  std::vector<float> sides;
  for (const ShotMapCell& cell : map.cells())
  {
    if (std::ranges::find(sides, cell.side) == sides.end())
    {
      sides.push_back(cell.side);
    }
  }
  std::ranges::sort(sides);
  const auto grid = [&](const char* title, int view)
  {
    std::cout << '\n'
              << title
              << " (%), rows: metres out; columns: metres off centre\n";
    std::string header = "        ";
    for (const float side : sides)
    {
      header += std::format("{:>6g}", side);
    }
    std::cout << header << '\n';
    std::vector<float> lines;
    for (const ShotMapCell& cell : map.cells())
    {
      if (std::ranges::find(lines, cell.line) == lines.end())
      {
        lines.push_back(cell.line);
      }
    }
    std::ranges::sort(lines);
    for (const float line : lines)
    {
      std::string row = std::format("{:>6g}  ", line);
      for (const float side : sides)
      {
        std::string cellText = "     -";
        for (const ShotMapCell& cell : map.cells())
        {
          if (cell.line == line && cell.side == side)
          {
            if (const auto value = cellValue(cell, view))
            {
              cellText = view == DIFFERENCE
                             ? std::format("{:>+6.0f}", *value * PERCENT_SCALE)
                             : std::format("{:>6.0f}", *value * PERCENT_SCALE);
            }
          }
        }
        row += cellText;
      }
      std::cout << row << '\n';
    }
  };
  grid("Conversion", CONVERSION);
  grid("Engine xG", ENGINE_XG);
  grid("Conversion minus xG", DIFFERENCE);
  bool choice = false;
  for (const auto& [name, value] : spec.fixed)
  {
    if (name == "Shooter" && value > 0.5F)
    {
      choice = true;
    }
  }
  if (choice)
  {
    grid("Shot taken", SHOT_TAKEN);
  }

  std::cout << "\nCalibration (shots grouped by engine xG)\n"
               "   engine xG    shots   mean xG    scored   scored/xG\n";
  int shots = 0;
  int goals = 0;
  double xg = 0.0;
  for (const CalibrationBin& bin : map.calibration())
  {
    shots += bin.shots;
    goals += bin.goals;
    xg += bin.xg;
    if (bin.shots == 0)
    {
      continue;
    }
    std::cout << std::format(
        "  {:>4.0f}-{:<4.0f}%  {:>7}  {:>7.1f}%  {:>7.1f}%  {:>9.2f}\n",
        bin.from * PERCENT_SCALE, bin.to * PERCENT_SCALE, bin.shots,
        PERCENT_SCALE * bin.xg / bin.shots,
        PERCENT_SCALE * bin.goals / bin.shots,
        bin.xg > 0.0 ? bin.goals / bin.xg : 0.0);
  }
  std::cout << std::format(
      "  all       {:>7}  {:>7.1f}%  {:>7.1f}%  {:>9.2f}\n", shots,
      (shots != 0) ? PERCENT_SCALE * xg / shots : 0.0,
      (shots != 0) ? PERCENT_SCALE * goals / shots : 0.0,
      xg > 0.0 ? goals / xg : 0.0);

  if (csvAuto)
  {
    csvPath = measurePath(shotDrill->name(), "spot grid");
  }
  if (csvPath)
  {
    std::error_code ignored;
    if (csvPath->has_parent_path())
    {
      std::filesystem::create_directories(csvPath->parent_path(), ignored);
    }
    std::ofstream file(*csvPath, std::ios::binary);
    file << map.runs().csv();
    if (!file)
    {
      return 1;
    }
    std::cout << "\nRuns written to " << csvPath->string() << '\n';
  }
  return 0;
}
