// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_session_scene.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <string_view>

#include "controller/game_controller.h"
#include "global/paths.h"
#include "gui/gui_view.h"
#include "gui/render/match_renderer_2d.h"
#include "gui/render/match_renderer_3d.h"
#include "gui/widgets/theme.h"
#include "model/player.h"
#include "tools/match_sandbox_page_ui.h"
#include "tools/match_sandbox_typography.h"

namespace
{
constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
/** At most this many steps per frame, so a slow frame cannot snowball. */
constexpr int MAX_STEPS_PER_FRAME = 400;
constexpr std::array<float, 8> SPEEDS{0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f};
}  // namespace

ZoneSessionScene::ZoneSessionScene(GUIView* guiView_ptr, const SessionPlan& plan_ref,
                                   std::vector<TrainingPlayer> picks_list)
    : GUIScene(guiView_ptr),
      plan(plan_ref),
      picks(std::move(picks_list)),
      renderer(std::make_unique<MatchRenderer2D>())
{
}

ZoneSessionScene::~ZoneSessionScene() { run.reset(); }

SceneID ZoneSessionScene::getID() const { return SceneID::MATCH_SANDBOX; }

void ZoneSessionScene::onEnter()
{
  // The launch page's fonts, as the training scene loads them.
  auto* atlas = ImGui::GetIO().Fonts;
  const auto font = [&](const char* file, float size)
  {
    const auto config = SandboxTypography::manropeConfig();
    for (auto* existing : atlas->Fonts)
      if (std::string_view(existing->GetDebugName()).find(file) != std::string_view::npos &&
          !existing->Sources.empty() &&
          existing->Sources[0]->ExtraSizeScale == config.ExtraSizeScale)
        return existing;
    return atlas->AddFontFromFileTTF(
        AssetPaths::file((std::string("assets/fonts/") + file).c_str()).c_str(), size,
        &config);
  };
  regularFont = font("Manrope-Regular.ttf", 16);
  boldFont = font("Manrope-Bold.ttf", 14);
  restart();
}

void ZoneSessionScene::restart()
{
  run.reset();
  run = std::make_unique<SessionRun>(plan, guiView->getController().getStatsConfig(), 1, false,
                                     picks);
  watchedZone = 0;
  accumulator = 0.0f;
  paused = false;
}

void ZoneSessionScene::update(float deltaTime)
{
  if (ImGui::IsKeyPressed(ImGuiKey_Space, false) && !ImGui::GetIO().WantTextInput)
    paused = !paused;
  if (ImGui::IsKeyPressed(ImGuiKey_R, false) && !ImGui::GetIO().WantTextInput) restart();
  if (!run || run->finished() || paused) return;
  accumulator += deltaTime * speed;
  int steps = 0;
  while (accumulator >= STEP && steps < MAX_STEPS_PER_FRAME && !run->finished())
  {
    accumulator -= STEP;
    run->step();
    ++steps;
  }
  if (steps == MAX_STEPS_PER_FRAME) accumulator = 0.0f;
}

void ZoneSessionScene::render()
{
  namespace Page = SandboxPageUI;
  const auto* viewport = ImGui::GetMainViewport();
  const float scale = Theme::scale();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, Page::PAGE);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24 * scale, 24 * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGui::Begin("##zone_session", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float gutter = 6 * scale + ImGui::GetStyle().ScrollbarSize;
  const bool columns = available.x >= 1000 * scale;
  const float panel = columns ? 420 * scale : available.x - gutter;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  if (columns)
  {
    ImGui::BeginChild("##zone_session_controls", ImVec2(panel + gutter, available.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderPanel(panel);
    ImGui::EndChild();
    ImGui::SameLine(0, 6 * scale);
    renderGround(std::max(scale, ImGui::GetContentRegionAvail().x), available.y);
  }
  else
  {
    ImGui::BeginChild("##zone_session_controls", available,
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderGround(panel, std::max(300 * scale, available.y * 0.65f));
    renderPanel(panel);
    ImGui::EndChild();
  }
  ImGui::PopStyleVar();
  ImGui::End();
}

void ZoneSessionScene::renderPanel(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  if (!run) return;
  const std::size_t exerciseCount = plan.exercises.size();
  const std::size_t index = std::min(run->exercise(), exerciseCount - 1);
  const std::string context =
      run->changing() && index + 1 < exerciseCount
          ? std::format("Changing to exercise {} of {}: {}. Everyone walks to his place.",
                        index + 2, exerciseCount, plan.exercises[index + 1].name)
          : std::format("Exercise {} of {}: {}. Engine conditions come from Set-up.", index + 1,
                        exerciseCount, plan.exercises[index].name);
  Page::header("Training", plan.name.c_str(), width, scale, boldFont, regularFont,
               context.c_str(), true);
  Page::beginCard("##zone_session_playback", "RUN SESSION", width, Page::DARK, scale,
                  boldFont);
  if (Page::button("Back to sandbox", false, scale, boldFont)) guiView->popScene();
  ImGui::SameLine(0, 6 * scale);
  if (Page::button("Restart (R)", false, scale, boldFont)) restart();
  const bool over = run->finished();
  ImGui::SameLine(0, 6 * scale);
  ImGui::BeginDisabled(over);
  if (Page::button(paused ? "Play (Space)" : "Pause (Space)", false, scale, boldFont))
    paused = !paused;
  ImGui::EndDisabled();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Speed");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90 * scale);
  const std::string speedLabel = std::format("{:g}x", speed);
  if (ImGui::BeginCombo("##zone_speed", speedLabel.c_str()))
  {
    for (const float option : SPEEDS)
      if (ImGui::Selectable(std::format("{:g}x", option).c_str(), option == speed))
        speed = option;
    ImGui::EndCombo();
  }
  ImGui::SameLine(0, 12 * scale);
  if (ImGui::Checkbox("3D view", &view3d))
  {
    if (view3d)
      renderer = std::make_unique<MatchRenderer3D>();
    else
      renderer = std::make_unique<MatchRenderer2D>();
  }
  if (view3d)
  {
    constexpr std::array<std::pair<MatchCameraMode, const char*>, 3> CAMERAS{
        {{MatchCameraMode::BROADCAST, "Broadcast"},
         {MatchCameraMode::TACTICAL, "Tactical"},
         {MatchCameraMode::END, "End"}}};
    const char* current = "Broadcast";
    for (const auto& [mode, label] : CAMERAS)
      if (mode == cameraMode) current = label;
    ImGui::SetNextItemWidth(180 * scale);
    if (ImGui::BeginCombo("Camera", current))
    {
      for (const auto& [mode, label] : CAMERAS)
        if (ImGui::Selectable(label, mode == cameraMode)) cameraMode = mode;
      ImGui::EndCombo();
    }
    // The zone the cameras follow (its ball); one engine in a change.
    const auto& zones = run->zones();
    if (!run->changing() && !zones.empty())
    {
      watchedZone = std::min(watchedZone, zones.size() - 1);
      ImGui::SetNextItemWidth(180 * scale);
      if (ImGui::BeginCombo("Watch", zones[watchedZone].plan->name.c_str()))
      {
        for (std::size_t index = 0; index < zones.size(); ++index)
          if (ImGui::Selectable(zones[index].plan->name.c_str(), index == watchedZone))
            watchedZone = index;
        ImGui::EndCombo();
      }
    }
  }
  const int seconds = static_cast<int>(run->seconds());
  ImGui::Text("%s / %d:%02d", over ? "Finished" : paused ? "Paused" : "Running",
              seconds / 60, seconds % 60);
  Page::endCard(scale);

  // The changing room (T7c): send a player there; he plays again in the
  // next exercise that names him.
  Page::beginCard("##zone_session_room", "CHANGING ROOM", width, Page::MID, scale, boldFont);
  const std::vector<std::size_t> room = run->changingRoom();
  std::vector<std::size_t> playing;
  for (std::size_t index = 0; index < run->squad().size(); ++index)
    if (std::ranges::find(room, index) == room.end()) playing.push_back(index);
  if (!over && !playing.empty())
  {
    if (std::ranges::find(playing, restPick) == playing.end()) restPick = playing.front();
    ImGui::SetNextItemWidth(160 * scale);
    if (ImGui::BeginCombo("##rest_pick", run->squad()[restPick]->getName().c_str()))
    {
      for (const std::size_t index : playing)
        if (ImGui::Selectable(run->squad()[index]->getName().c_str(), index == restPick))
          restPick = index;
      ImGui::EndCombo();
    }
    ImGui::SameLine(0, 6 * scale);
    if (Page::button("Send to the changing room", false, scale, boldFont))
      (void)run->sendToChangingRoom(restPick);
  }
  std::string resting;
  for (const std::size_t index : room)
    resting += (resting.empty() ? "" : ", ") + run->squad()[index]->getName();
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled("In the changing room: %s", resting.empty() ? "nobody" : resting.c_str());
  ImGui::PopTextWrapPos();
  Page::endCard(scale);

  // Each zone of the exercise, live.
  Page::beginCard("##zone_session_zones", "ZONES", width, Page::LIGHT, scale, boldFont);
  ImGui::PushTextWrapPos(0);
  if (over)
    ImGui::TextUnformatted("The session is over.");
  else if (run->changing())
    ImGui::TextUnformatted("Changing exercise: the balls are with the coaches, and "
                           "everyone walks to his place in the next exercise (marked "
                           "out). It starts when they are all there.");
  else
    for (const SessionRun::Zone& zone : run->zones())
    {
      if (!zone.drill) continue;
      ImGui::PushFont(boldFont, 16);
      ImGui::TextUnformatted(zone.plan->name.c_str());
      ImGui::PopFont();
      const std::vector<std::string> lines = zone.run && zone.run->finished()
                                                 ? zone.report
                                                 : zone.drill->report();
      for (std::size_t line = 0; line < lines.size() && line < 3; ++line)
        ImGui::TextUnformatted(lines[line].c_str());
      ImGui::Separator();
    }
  ImGui::PopTextWrapPos();
  Page::endCard(scale);

  // The exercises done so far, and what is to come.
  Page::beginCard("##zone_session_plan", "SESSION", width, Page::MID, scale, boldFont);
  ImGui::PushTextWrapPos(0);
  ImGui::TextUnformatted(plan.summary.c_str());
  for (std::size_t exercise = 0; exercise < exerciseCount; ++exercise)
  {
    const char* status = exercise < run->history().size() ? "Done"
                         : exercise == index && !over    ? "Now"
                                                         : "Next";
    ImGui::Text("%zu / %s: %s", exercise + 1, plan.exercises[exercise].name.c_str(), status);
    if (exercise < run->history().size())
      for (const std::string& line : run->history()[exercise])
        if (line.find("OUTCOME") != std::string::npos)
          ImGui::TextDisabled("  %s", line.c_str());
  }
  ImGui::PopTextWrapPos();
  Page::endCard(scale);
}

void ZoneSessionScene::renderGround(float width, float height)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##zone_session_ground", "TRAINING GROUND", width, Page::DARK, scale,
                  boldFont);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 size(std::max(scale, ImGui::GetContentRegionAvail().x),
                    std::max(200 * scale, height - 56 * scale));
  ImGui::InvisibleButton("##zone_session_ground", size);
  if (!run)
  {
    Page::endCard(scale);
    return;
  }
  MatchViewport view{origin.x, origin.y, size.x, size.y};
  fillSessionRenderSnapshot(*run, snapshot, watchedZone);
  snapshot.interpolationAlpha =
      paused || run->finished() ? 1.0f : std::clamp(accumulator / STEP, 0.0f, 1.0f);
  MatchRenderOptions options;
  options.showPlayerNames = false;
  options.frameSeconds = ImGui::GetIO().DeltaTime;
  options.cameraMode = cameraMode;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
  renderer->render(snapshot, options, view);
  draw->PopClipRect();
  Page::endCard(scale);
}
