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
#include <vector>

#include "controller/game_controller.h"
#include "gui/gui_view.h"
#include "gui/render/match_renderer_2d.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "tools/match_sandbox_setup.h"

namespace
{
constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
constexpr float PANEL_WIDTH = 560.0f;
/** At most this many steps per frame, so a slow frame cannot snowball. */
constexpr int MAX_STEPS_PER_FRAME = 400;
/** Skipping to the end: this many steps per frame (the window stays
 * responsive). */
constexpr int SKIP_STEPS_PER_FRAME = 3000;
constexpr std::array<float, 8> SPEEDS{0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f,
                                      32.0f, 64.0f};
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

void TrainingSessionScene::onEnter() { startDrill(0); }

void TrainingSessionScene::startDrill(std::size_t index)
{
  if (!session || index >= session->drills.size()) return;
  current = index;
  run.reset();
  run = startTrainingRun(*session->drills[current],
                         guiView->getController().getStatsConfig());
  accumulator = 0.0f;
  skipping = false;
  paused = false;
}

void TrainingSessionScene::stepRun(int steps)
{
  for (int step = 0; step < steps && run && !run->finished(); ++step)
    run->step();
}

void TrainingSessionScene::update(float deltaTime)
{
  if (ImGui::IsKeyPressed(ImGuiKey_Space, false) &&
      !ImGui::GetIO().WantTextInput)
    paused = !paused;
  if (ImGui::IsKeyPressed(ImGuiKey_R, false) && !ImGui::GetIO().WantTextInput)
    startDrill(current);
  if (!run || run->finished()) return;
  if (skipping)
  {
    stepRun(SKIP_STEPS_PER_FRAME);
    return;
  }
  if (paused) return;
  accumulator += deltaTime * speed;
  int steps = 0;
  while (accumulator >= STEP && steps < MAX_STEPS_PER_FRAME)
  {
    accumulator -= STEP;
    stepRun(1);
    ++steps;
  }
  if (steps == MAX_STEPS_PER_FRAME) accumulator = 0.0f;
}

void TrainingSessionScene::render()
{
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  const float scale = Theme::scale();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(Theme::Space::L * scale, Theme::Space::M * scale));
  ImGui::Begin("##training", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();
  renderPanel(PANEL_WIDTH * scale);
  ImGui::SameLine();
  renderPitch();
  ImGui::End();
}

void TrainingSessionScene::renderPanel(float width)
{
  if (!ImGui::BeginChild("##panel", ImVec2(width, 0.0f)))
  {
    ImGui::EndChild();
    return;
  }
  const std::string title =
      std::format("{} session · {}", session->name, club);
  UI::pageHeader(title.c_str());
  TrainingDrill& drill = *session->drills[current];
  ImGui::SeparatorText(std::format("Drill {} of {}: {}", current + 1,
                                   session->drills.size(), drill.name())
                           .c_str());
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled("%s", drill.summary());
  ImGui::PopTextWrapPos();

  ImGui::TextDisabled("Engine versions come from Set-up.");

  // Run controls.
  if (ImGui::Button("Restart drill (R)")) startDrill(current);
  ImGui::SameLine();
  if (ImGui::Button(paused ? "Play (Space)" : "Pause (Space)"))
    paused = !paused;
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70.0f * Theme::scale());
  const std::string label = std::format("{:g}x", speed);
  if (ImGui::BeginCombo("##speed", label.c_str()))
  {
    for (const float option : SPEEDS)
      if (ImGui::Selectable(std::format("{:g}x", option).c_str(),
                            option == speed))
        speed = option;
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  const bool over = run && run->finished();
  ImGui::BeginDisabled(over || skipping);
  if (ImGui::Button("Skip to the end")) skipping = true;
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Run the rest of the drill as fast as it goes.");

  if (run)
  {
    const double seconds =
        static_cast<double>(run->engine().getSimulatedSteps()) * STEP;
    const int whole = static_cast<int>(seconds);
    ImGui::Text("%s  ·  %d:%02d%s", over ? "Finished" : "Running", whole / 60,
                whole % 60, skipping && !over ? "  ·  skipping..." : "");
    for (const std::string& line : drill.report())
      ImGui::TextUnformatted(line.c_str());
  }

  ImGui::SeparatorText("Results");
  renderResults(drill);

  // Moving on.
  ImGui::Spacing();
  const bool last = current + 1 >= session->drills.size();
  ImGui::BeginDisabled(!over);
  if (UI::primaryButton(last ? "End session" : "Next drill"))
  {
    if (last)
    {
      ImGui::EndDisabled();
      ImGui::EndChild();
      guiView->popScene();
      return;
    }
    startDrill(current + 1);
  }
  ImGui::EndDisabled();
  if (!over && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Once everyone has finished (or skip to the end).");
  ImGui::SameLine();
  if (ImGui::Button("Back to the sandbox"))
  {
    ImGui::EndChild();
    guiView->popScene();
    return;
  }
  ImGui::EndChild();
}

void TrainingSessionScene::renderResults(TrainingDrill& drill)
{
  const std::vector<TrainingColumn> columns = drill.columns();
  const std::vector<TrainingRow> rows = drill.results();
  const int columnCount = static_cast<int>(columns.size()) + 2;
  // Leave room below for the buttons.
  const float height = std::max(
      120.0f * Theme::scale(),
      ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.0f);
  if (!ImGui::BeginTable("##results", columnCount,
                         ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_BordersInnerH |
                             ImGuiTableFlags_ScrollY |
                             ImGuiTableFlags_SizingFixedFit,
                         ImVec2(0.0f, height)))
    return;
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_DefaultSort);
  ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch);
  for (const TrainingColumn& column : columns)
    ImGui::TableSetupColumn(column.name, ImGuiTableColumnFlags_PreferSortDescending);
  // Headers with the columns' tooltips.
  ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
  for (int column = 0; column < columnCount; ++column)
  {
    if (!ImGui::TableSetColumnIndex(column)) continue;
    ImGui::TableHeader(ImGui::TableGetColumnName(column));
    if (column >= 2 && ImGui::IsItemHovered())
      if (const char* tip = columns[static_cast<std::size_t>(column - 2)].tooltip)
        ImGui::SetTooltip("%s", tip);
  }

  // Finishing order by default; any column by clicking its header.
  std::vector<std::size_t> order(rows.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  if (const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
      specs && specs->SpecsCount > 0)
  {
    const int column = specs->Specs[0].ColumnIndex;
    const bool ascending =
        specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
    const auto key = [&](std::size_t row) -> double
    {
      if (column < 2) return static_cast<double>(row);
      return rows[row].values[static_cast<std::size_t>(column - 2)];
    };
    if (column == 1)
      std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b)
      {
        const std::string& left = session->squad[rows[a].player].name;
        const std::string& right = session->squad[rows[b].player].name;
        return ascending ? left < right : right < left;
      });
    else
      std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b)
                               { return ascending ? key(a) < key(b) : key(b) < key(a); });
  }

  for (const std::size_t index : order)
  {
    const TrainingRow& row = rows[index];
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("%zu", index + 1);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(session->squad[row.player].name.c_str());
    for (const std::string& cell : row.cells)
    {
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(cell.c_str());
    }
  }
  ImGui::EndTable();
}

void TrainingSessionScene::renderPitch()
{
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const ImVec2 size(std::max(1.0f, available.x), std::max(1.0f, available.y));
  ImGui::InvisibleButton("##training_pitch", size);
  if (!run) return;
  MatchViewport view = computeMatchViewport(origin.x, origin.y, size.x, size.y);
  view.x = origin.x + std::max(0.0f, (size.x - view.width) * 0.5f);
  view.y = origin.y + std::max(0.0f, (size.y - view.height) * 0.5f);
  fillMatchRenderSnapshot(run->engine(), snapshot);
  // Players not in the session would wait by the bench like players sent off.
  std::erase_if(snapshot.players,
                [](const MatchRenderPlayer& player) { return !player.onPitch; });
  snapshot.interpolationAlpha =
      paused || skipping || run->finished()
          ? 1.0f
          : std::clamp(accumulator / STEP, 0.0f, 1.0f);
  MatchRenderOptions options;
  options.showPlayerNames = true;
  options.frameSeconds = ImGui::GetIO().DeltaTime;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
  renderer->render(snapshot, options, view);
  session->drills[current]->drawOverlay(*renderer, *draw);
  draw->PopClipRect();
}
