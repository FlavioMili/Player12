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

namespace
{
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
  regularFont = font("Manrope-Regular.ttf", 16);
  boldFont = font("Manrope-Bold.ttf", 14);
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
                      ImVec2(24 * scale, 24 * scale));
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
  const float gutter = (6 * scale) + ImGui::GetStyle().ScrollbarSize;
  const bool columns = available.x >= 1000 * scale;
  const float panel =
      columns ? (available.x >= 1400 * scale ? 440.0F : 400.0F) * scale
              : available.x - gutter;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  if (columns)
  {
    ImGui::BeginChild("##training_controls",
                      ImVec2(panel + gutter, available.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderPanel(panel);
    ImGui::EndChild();
    ImGui::SameLine(0, 6 * scale);
    renderPitch(std::max(scale, ImGui::GetContentRegionAvail().x), available.y);
  }
  else
  {
    ImGui::BeginChild("##training_controls", available,
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderPitch(panel, std::max(300 * scale, available.y * 0.65F));
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
    if (ImGui::GetItemRectMax().x + 6 * scale + itemWidth <=
        ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
    {
      ImGui::SameLine(0, 6 * scale);
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
    ImGui::SetTooltip("Once everyone has finished, or skip to the end.");
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
  ImGui::SetNextItemWidth(90 * scale);
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
    ImGui::SetTooltip("Run the rest of the drill as fast as it goes.");
  }
  if (run)
  {
    const double seconds = run->engine().getSimulatedSeconds();
    ImGui::Text("%s / %.0f:%02.0f",
                over       ? "Finished"
                : skipping ? "Skipping"
                : paused   ? "Paused"
                           : "Running",
                std::floor(seconds / 60.0),
                std::floor(std::fmod(seconds, 60.0)));
    ImGui::Text("%zu of %zu players finished",
                session->drills[current]->results().size(),
                session->squad.size());
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
  ImGui::SameLine(0, 6 * scale);
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
  ImGui::TextWrapped("%s", session->summary.c_str());
  if (ImGui::BeginTable("##session_drills", 2,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_BordersInnerV))
  {
    ImGui::TableSetupColumn("Drill", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed,
                            72 * scale);
    for (std::size_t index = 0; index < session->drills.size(); ++index)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (index == current)
      {
        ImGui::PushFont(boldFont, 16);
      }
      ImGui::TextWrapped("%zu / %s", index + 1, session->drills[index]->name());
      if (index == current)
      {
        ImGui::PopFont();
      }
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(
          index < current    ? "Done"
          : index == current ? (run && run->finished() ? "Finished" : "Current")
                             : "Next");
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
  for (int slot = 0; slot < metricCount; ++slot)
  {
    resultMetrics[slot] =
        std::clamp(resultMetrics[slot], 0, int(columns.size()) - 1);
  }
  if (columns.size() > 2 &&
      ImGui::BeginTable("##metric_choices", 2,
                        ImGuiTableFlags_SizingStretchSame))
  {
    for (int slot = 0; slot < 2; ++slot)
    {
      ImGui::TableNextColumn();
      ImGui::PushID(slot);
      ImGui::Dummy(ImVec2(0, 0));
      ImGui::TextUnformatted(slot == 0 ? "First metric" : "Second metric");
      ImGui::SetNextItemWidth(-1);
      if (SandboxPageUI::beginCombo("##metric",
                                    columns[resultMetrics[slot]].name))
      {
        for (int metric = 0; metric < int(columns.size()); ++metric)
        {
          if (metric == resultMetrics[1 - slot])
          {
            continue;
          }
          if (ImGui::Selectable(columns[metric].name,
                                metric == resultMetrics[slot]))
          {
            resultMetrics[slot] = metric;
          }
          if ((columns[metric].tooltip != nullptr) && ImGui::IsItemHovered())
          {
            ImGui::SetTooltip("%s", columns[metric].tooltip);
          }
        }
        ImGui::EndCombo();
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (rows.empty())
  {
    ImGui::TextWrapped(
        "Results appear as each player finishes. Choose the metrics above and "
        "click a table heading to sort.");
  }
  ImGui::PushID(static_cast<int>(current));
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(3 * scale, 3 * scale));
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
  ImGui::TableSetupColumn(
      "#", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed,
      36 * scale);
  ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch);
  for (int slot = 0; slot < metricCount; ++slot)
  {
    ImGui::TableSetupColumn(columns[resultMetrics[slot]].name,
                            ImGuiTableColumnFlags_PreferSortDescending |
                                ImGuiTableColumnFlags_WidthFixed,
                            88 * scale);
  }
  ImGui::PushFont(boldFont, 14);
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
      if (const char* tip = columns[resultMetrics[column - 2]].tooltip)
      {
        ImGui::SetTooltip("%s", tip);
      }
    }
  }
  ImGui::PopFont();
  // Finishing order by default; any column by clicking its header.
  std::vector<std::size_t> order(rows.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  if (const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
      (specs != nullptr) && specs->SpecsCount > 0)
  {
    const int column = specs->Specs[0].ColumnIndex;
    const bool ascending =
        specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
    const auto key = [&](std::size_t row) -> double
    {
      if (column < 2)
      {
        return static_cast<double>(row);
      }
      return rows[row]
          .values[static_cast<std::size_t>(resultMetrics[column - 2])];
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

  for (const std::size_t index : order)
  {
    const TrainingRow& row = rows[index];
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("%zu", index + 1);
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
      ImGui::SetTooltip("%s", name);
    }
    for (int slot = 0; slot < metricCount; ++slot)
    {
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(row.cells[resultMetrics[slot]].c_str());
    }
  }
  ImGui::EndTable();
  ImGui::PopStyleVar();
  ImGui::PopID();
}

void TrainingSessionScene::renderPitch(float width, float height)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##training_pitch", "PITCH", width, Page::DARK, scale,
                  boldFont);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 size(std::max(scale, ImGui::GetContentRegionAvail().x),
                    std::max(200 * scale, height - (56 * scale)));
  ImGui::InvisibleButton("##training_pitch", size);
  if (!run)
  {
    Page::endCard(scale);
    return;
  }
  MatchViewport view = computeMatchViewport(origin.x, origin.y, size.x, size.y);
  view.x = origin.x + std::max(0.0F, (size.x - view.width) * 0.5F);
  view.y = origin.y + std::max(0.0F, (size.y - view.height) * 0.5F);
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
