// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_training_scene.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "controller/game_controller.h"
#include "global/paths.h"
#include "gui/gui_view.h"
#include "gui/render/match_renderer_2d.h"
#include "gui/widgets/theme.h"
#include "tools/match_sandbox_page_ui.h"
#include "tools/match_sandbox_setup.h"
#include "tools/match_sandbox_text.h"

namespace
{
constexpr float REGULAR_FONT_SIZE = 16.0F;
constexpr float HEADER_FONT_SIZE = 14.0F;
constexpr float PAGE_PADDING = 24.0F;
constexpr float PANEL_GAP = 6.0F;
constexpr float COLUMN_LAYOUT_WIDTH = 1000.0F;
constexpr float WIDE_LAYOUT_WIDTH = 1400.0F;
constexpr float WIDE_PANEL_WIDTH = 440.0F;
constexpr float PANEL_WIDTH = 400.0F;
constexpr float MIN_PITCH_HEIGHT = 300.0F;
constexpr float NARROW_PITCH_SHARE = 0.65F;
constexpr float PITCH_CARD_CHROME = 56.0F;
constexpr float VIEWPORT_CENTRE = 0.5F;
constexpr float SPEED_COMBO_WIDTH = 90.0F;
constexpr double SECONDS_PER_MINUTE = 60.0;
constexpr float STATUS_COLUMN_WIDTH = 72.0F;
constexpr float RESULT_CELL_PADDING = 3.0F;
constexpr float ROW_NUMBER_WIDTH = 36.0F;
constexpr float METRIC_COLUMN_WIDTH = 88.0F;
constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
/** At most this many steps per frame, so a slow frame cannot snowball. */
constexpr int MAX_STEPS_PER_FRAME = 400;
/** Skipping to the end: this many steps per frame (the window stays
 * responsive). */
constexpr int SKIP_STEPS_PER_FRAME = 3000;
constexpr std::array<float, 8> SPEEDS{0.5F, 1.0F,  2.0F,  4.0F,
                                      8.0F, 16.0F, 32.0F, 64.0F};
}  // namespace

TrainingSessionScene::TrainingSessionScene(
    GUIView* guiView_ptr, std::unique_ptr<TrainingSession> session_ptr,
    std::string club_name)
    : GUIScene(guiView_ptr),
      session(std::move(session_ptr)),
      club(std::move(club_name)),
      renderer(std::make_unique<MatchRenderer2D>())
{
}

TrainingSessionScene::~TrainingSessionScene()
{
  // The run borrows the session's drill: it goes first.
  run.reset();
}

SceneID TrainingSessionScene::getID() const { return SceneID::MATCH_SANDBOX; }

void TrainingSessionScene::onEnter()
{
  // Reuse the launch page's fonts. Direct tool entry loads them once into
  // the current atlas, so reopening a training session does not add font
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
  startDrill(0);
}

void TrainingSessionScene::startDrill(std::size_t index)
{
  if (!session || index >= session->drills.size())
  {
    return;
  }
  if (current != index)
  {
    resultMetrics = {0, 1};
  }
  current = index;
  run.reset();
  run = startTrainingRun(*session->drills[current],
                         guiView->getController().getStatsConfig());
  accumulator = 0.0F;
  skipping = false;
  paused = false;
}

void TrainingSessionScene::stepRun(int steps)
{
  for (int step = 0; step < steps && run && !run->finished(); ++step)
  {
    run->step();
  }
}

void TrainingSessionScene::update(float deltaTime)
{
  if (ImGui::IsKeyPressed(ImGuiKey_Space, false) &&
      !ImGui::GetIO().WantTextInput)
  {
    paused = !paused;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_R, false) && !ImGui::GetIO().WantTextInput)
  {
    startDrill(current);
  }
  if (!run || run->finished())
  {
    return;
  }
  if (skipping)
  {
    stepRun(SKIP_STEPS_PER_FRAME);
    return;
  }
  if (paused)
  {
    return;
  }
  accumulator += deltaTime * speed;
  int steps = 0;
  while (accumulator >= STEP && steps < MAX_STEPS_PER_FRAME)
  {
    accumulator -= STEP;
    stepRun(1);
    ++steps;
  }
  if (steps == MAX_STEPS_PER_FRAME)
  {
    accumulator = 0.0F;
  }
}

void TrainingSessionScene::render()
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
  ImGui::Begin("##training", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float gutter = (PANEL_GAP * scale) + ImGui::GetStyle().ScrollbarSize;
  const bool columns = available.x >= COLUMN_LAYOUT_WIDTH * scale;
  const float panel =
      columns ? (available.x >= WIDE_LAYOUT_WIDTH * scale ? WIDE_PANEL_WIDTH
                                                          : PANEL_WIDTH) *
                    scale
              : available.x - gutter;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  if (columns)
  {
    ImGui::BeginChild("##training_controls",
                      ImVec2(panel + gutter, available.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderPanel(panel);
    ImGui::EndChild();
    ImGui::SameLine(0, PANEL_GAP * scale);
    renderPitch(
        ImVec2(std::max(scale, ImGui::GetContentRegionAvail().x), available.y));
  }
  else
  {
    ImGui::BeginChild("##training_controls", available,
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderPitch(ImVec2(panel, std::max(MIN_PITCH_HEIGHT * scale,
                                       available.y * NARROW_PITCH_SHARE)));
    renderPanel(panel);
    ImGui::EndChild();
  }
  ImGui::PopStyleVar();
  ImGui::End();
}

void TrainingSessionScene::renderPanel(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  if (!session || session->drills.empty())
  {
    return;
  }
  const std::string subtitle = std::format("{} / {}", session->name, club);
  const std::string context = std::format(
      "Drill {} of {}: {}. Engine conditions come from Set-up.", current + 1,
      session->drills.size(), session->drills[current]->name());
  Page::header("Training", subtitle.c_str(), width, scale, boldFont,
               regularFont, context.c_str(), true);
  Page::beginCard("##training_playback", "RUN SESSION", width, Page::DARK,
                  scale, boldFont);
  const auto next = [&](float itemWidth)
  {
    if (ImGui::GetItemRectMax().x + PANEL_GAP * scale + itemWidth <=
        ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
    {
      ImGui::SameLine(0, PANEL_GAP * scale);
    }
  };
  const auto nextButton = [&](const char* label)
  { next(Page::buttonWidth(label, scale, boldFont)); };
  if (Page::button("Back to sandbox", false, scale, boldFont))
  {
    guiView->popScene();
  }
  const bool over = run && run->finished();
  const bool last = current + 1 >= session->drills.size();
  const char* advance = last ? "End session" : "Next drill";
  nextButton(advance);
  ImGui::BeginDisabled(!over);
  const bool advanceClicked = Page::button(advance, true, scale, boldFont);
  ImGui::EndDisabled();
  if (!over && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
  {
    SandboxText::tooltipText("Once everyone has finished, or skip to the end.");
  }
  if (Page::button("Restart drill (R)", false, scale, boldFont))
  {
    startDrill(current);
  }
  const char* playback = paused ? "Play (Space)" : "Pause (Space)";
  nextButton(playback);
  ImGui::BeginDisabled(over || skipping);
  if (Page::button(playback, false, scale, boldFont))
  {
    paused = !paused;
  }
  ImGui::EndDisabled();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Speed");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(SPEED_COMBO_WIDTH * scale);
  const std::string speedLabel = std::format("{:g}x", speed);
  if (SandboxPageUI::beginCombo("##speed", speedLabel.c_str()))
  {
    for (const float option : SPEEDS)
    {
      if (ImGui::Selectable(std::format("{:g}x", option).c_str(),
                            option == speed))
      {
        speed = option;
      }
    }
    ImGui::EndCombo();
  }
  nextButton("Skip to the end");
  ImGui::BeginDisabled(over || skipping);
  if (Page::button("Skip to the end", false, scale, boldFont))
  {
    skipping = true;
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
  {
    SandboxText::tooltipText("Run the rest of the drill as fast as it goes.");
  }
  if (run)
  {
    const double seconds = run->engine().getSimulatedSeconds();
    const char* status = "Running";
    if (over)
    {
      status = "Finished";
    }
    else if (skipping)
    {
      status = "Skipping";
    }
    else if (paused)
    {
      status = "Paused";
    }
    ImGui::TextUnformatted(
        std::format("{} / {:.0f}:{:02.0f}", status,
                    std::floor(seconds / SECONDS_PER_MINUTE),
                    std::floor(std::fmod(seconds, SECONDS_PER_MINUTE)))
            .c_str());
    ImGui::TextUnformatted(
        std::format("{} of {} players finished",
                    session->drills[current]->results().size(),
                    session->squad.size())
            .c_str());
  }
  Page::endCard(scale);
  if (advanceClicked)
  {
    if (last)
    {
      guiView->popScene();
    }
    else
    {
      startDrill(current + 1);
    }
    return;
  }
  Page::beginCard("##training_tabs", "DETAILS", width, Page::MID, scale,
                  boldFont);
  if (Page::button("Progress", !showResults, scale, boldFont))
  {
    showResults = false;
  }
  ImGui::SameLine(0, PANEL_GAP * scale);
  if (Page::button("Results", showResults, scale, boldFont))
  {
    showResults = true;
  }
  Page::endCard(scale);
  TrainingDrill& drill = *session->drills[current];
  if (showResults)
  {
    Page::beginCard("##training_results", "PLAYER RESULTS", width, Page::LIGHT,
                    scale, boldFont);
    renderResults(drill);
    Page::endCard(scale);
    return;
  }
  Page::beginCard("##training_progress", "DRILL PROGRESS", width, Page::LIGHT,
                  scale, boldFont);
  ImGui::PushTextWrapPos(0);
  ImGui::TextUnformatted(drill.summary());
  ImGui::Separator();
  for (const auto& line : drill.report())
  {
    ImGui::TextUnformatted(line.c_str());
  }
  ImGui::PopTextWrapPos();
  Page::endCard(scale);
  Page::beginCard("##training_session", "SESSION", width, Page::MID, scale,
                  boldFont);
  SandboxText::wrappedText(session->summary.c_str());
  if (ImGui::BeginTable("##session_drills", 2,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_BordersInnerV))
  {
    ImGui::TableSetupColumn("Drill", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed,
                            STATUS_COLUMN_WIDTH * scale);
    for (std::size_t index = 0; index < session->drills.size(); ++index)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (index == current)
      {
        ImGui::PushFont(boldFont, REGULAR_FONT_SIZE);
      }
      SandboxText::wrappedText(
          std::format("{} / {}", index + 1, session->drills[index]->name())
              .c_str());
      if (index == current)
      {
        ImGui::PopFont();
      }
      ImGui::TableNextColumn();
      const char* progress = "Next";
      if (index < current)
      {
        progress = "Done";
      }
      else if (index == current)
      {
        progress = run && run->finished() ? "Finished" : "Current";
      }
      ImGui::TextUnformatted(progress);
    }
    ImGui::EndTable();
  }
  Page::endCard(scale);
}

void TrainingSessionScene::renderResults(TrainingDrill& drill)
{
  const std::vector<TrainingColumn> columns = drill.columns();
  const std::vector<TrainingRow> rows = drill.results();
  const float scale = Theme::scale();
  const int metricCount = std::min(2, int(columns.size()));
  renderMetricChoices(columns, metricCount);
  if (rows.empty())
  {
    SandboxText::wrappedText(
        "Results appear as each player finishes. Choose the metrics above and "
        "click a table heading to sort.");
  }
  ImGui::PushID(static_cast<int>(current));
  ImGui::PushStyleVar(
      ImGuiStyleVar_CellPadding,
      ImVec2(RESULT_CELL_PADDING * scale, RESULT_CELL_PADDING * scale));
  if (!ImGui::BeginTable("##results", 2 + metricCount,
                         ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_BordersInnerH |
                             ImGuiTableFlags_BordersInnerV |
                             ImGuiTableFlags_SizingFixedFit))
  {
    ImGui::PopStyleVar();
    ImGui::PopID();
    return;
  }
  renderResultHeaders(columns, metricCount);
  const auto order = resultOrder(rows);
  for (const std::size_t index : order)
  {
    const TrainingRow& row = rows[index];
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(std::to_string(index + 1).c_str());
    ImGui::TableNextColumn();
    const char* name = session->squad[row.player].name.c_str();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = std::max(scale, ImGui::GetContentRegionAvail().x);
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(
        start, ImVec2(start.x + width, start.y + ImGui::GetTextLineHeight()),
        true);
    draw->AddText(start, SandboxPageUI::CREAM, name);
    draw->PopClipRect();
    ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
    if (ImGui::IsItemHovered())
    {
      SandboxText::tooltipText(name);
    }
    for (int slot = 0; slot < metricCount; ++slot)
    {
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(
          row.cells[resultMetrics.at(static_cast<std::size_t>(slot))].c_str());
    }
  }
  ImGui::EndTable();
  ImGui::PopStyleVar();
  ImGui::PopID();
}

void TrainingSessionScene::renderPitch(ImVec2 area)
{
  const float width = area.x;
  const float height = area.y;
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##training_pitch", "PITCH", width, Page::DARK, scale,
                  boldFont);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 size(
      std::max(scale, ImGui::GetContentRegionAvail().x),
      std::max(200 * scale, height - (PITCH_CARD_CHROME * scale)));
  ImGui::InvisibleButton("##training_pitch", size);
  if (!run)
  {
    Page::endCard(scale);
    return;
  }
  MatchViewport view = computeMatchViewport(origin.x, origin.y, size.x, size.y);
  view.x = origin.x + std::max(0.0F, (size.x - view.width) * VIEWPORT_CENTRE);
  view.y = origin.y + std::max(0.0F, (size.y - view.height) * VIEWPORT_CENTRE);
  fillMatchRenderSnapshot(run->engine(), snapshot);
  // Players not in the session would wait by the bench like players sent off.
  std::erase_if(snapshot.players, [](const MatchRenderPlayer& player)
                { return !player.onPitch; });
  snapshot.interpolationAlpha =
      paused || skipping || run->finished()
          ? 1.0F
          : std::clamp(accumulator / STEP, 0.0F, 1.0F);
  MatchRenderOptions options;
  options.showPlayerNames = true;
  options.frameSeconds = ImGui::GetIO().DeltaTime;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                     true);
  renderer->render(snapshot, options, view);
  session->drills[current]->drawOverlay(*renderer, *draw);
  draw->PopClipRect();
  Page::endCard(scale);
}

void TrainingSessionScene::renderMetricChoice(
    const std::vector<TrainingColumn>& columns, int slot)
{
  ImGui::TableNextColumn();
  ImGui::PushID(slot);
  ImGui::Dummy(ImVec2(0, 0));
  ImGui::TextUnformatted(slot == 0 ? "First metric" : "Second metric");
  ImGui::SetNextItemWidth(-1);
  if (SandboxPageUI::beginCombo(
          "##metric",
          columns[resultMetrics.at(static_cast<std::size_t>(slot))].name))
  {
    for (int metric = 0; metric < int(columns.size()); ++metric)
    {
      if (metric == resultMetrics.at(static_cast<std::size_t>(1 - slot)))
      {
        continue;
      }
      if (ImGui::Selectable(
              columns[metric].name,
              metric == resultMetrics.at(static_cast<std::size_t>(slot))))
      {
        resultMetrics.at(static_cast<std::size_t>(slot)) = metric;
      }
      if ((columns[metric].tooltip != nullptr) && ImGui::IsItemHovered())
      {
        SandboxText::tooltipText(columns[metric].tooltip);
      }
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
}

void TrainingSessionScene::renderMetricChoices(
    const std::vector<TrainingColumn>& columns, int metricCount)
{
  for (int slot = 0; slot < metricCount; ++slot)
  {
    resultMetrics.at(static_cast<std::size_t>(slot)) =
        std::clamp(resultMetrics.at(static_cast<std::size_t>(slot)), 0,
                   int(columns.size()) - 1);
  }
  if (columns.size() > 2 &&
      ImGui::BeginTable("##metric_choices", 2,
                        ImGuiTableFlags_SizingStretchSame))
  {
    for (int slot = 0; slot < 2; ++slot)
    {
      renderMetricChoice(columns, slot);
    }
    ImGui::EndTable();
  }
}

void TrainingSessionScene::renderResultHeaders(
    const std::vector<TrainingColumn>& columns, int metricCount)
{
  const float scale = Theme::scale();
  ImGui::TableSetupColumn(
      "#", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed,
      ROW_NUMBER_WIDTH * scale);
  ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch);
  for (int slot = 0; slot < metricCount; ++slot)
  {
    ImGui::TableSetupColumn(
        columns[resultMetrics.at(static_cast<std::size_t>(slot))].name,
        ImGuiTableColumnFlags_PreferSortDescending |
            ImGuiTableColumnFlags_WidthFixed,
        METRIC_COLUMN_WIDTH * scale);
  }
  ImGui::PushFont(boldFont, HEADER_FONT_SIZE);
  ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
  for (int column = 0; column < 2 + metricCount; ++column)
  {
    if (!ImGui::TableSetColumnIndex(column))
    {
      continue;
    }
    ImGui::TableHeader(ImGui::TableGetColumnName(column));
    if (column >= 2 && ImGui::IsItemHovered())
    {
      if (const char* tip =
              columns[resultMetrics.at(static_cast<std::size_t>(column - 2))]
                  .tooltip)
      {
        SandboxText::tooltipText(tip);
      }
    }
  }
  ImGui::PopFont();
}

std::vector<std::size_t> TrainingSessionScene::resultOrder(
    const std::vector<TrainingRow>& rows) const
{
  // Finishing order by default; any column by clicking its header.
  std::vector<std::size_t> order(rows.size());
  std::ranges::iota(order, std::size_t{0});
  if (const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
      (specs != nullptr) && specs->SpecsCount > 0)
  {
    const auto& sort =
        std::span(specs->Specs, static_cast<std::size_t>(specs->SpecsCount))
            .front();
    const int column = sort.ColumnIndex;
    const bool ascending = sort.SortDirection != ImGuiSortDirection_Descending;
    const auto key = [&](std::size_t row) -> double
    {
      if (column < 2)
      {
        return static_cast<double>(row);
      }
      return rows[row].values[static_cast<std::size_t>(
          resultMetrics.at(static_cast<std::size_t>(column - 2)))];
    };
    if (column == 1)
    {
      std::ranges::stable_sort(
          order,
          [&](std::size_t a, std::size_t b)
          {
            const std::string& left = session->squad[rows[a].player].name;
            const std::string& right = session->squad[rows[b].player].name;
            return ascending ? left < right : right < left;
          });
    }
    else
    {
      std::ranges::stable_sort(
          order, [&](std::size_t a, std::size_t b)
          { return ascending ? key(a) < key(b) : key(b) < key(a); });
    }
  }

  return order;
}
