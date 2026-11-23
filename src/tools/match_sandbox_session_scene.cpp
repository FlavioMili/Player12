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
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "controller/game_controller.h"
#include "global/paths.h"
#include "gui/gui_view.h"
#include "gui/render/match_renderer_2d.h"
#include "gui/render/match_renderer_3d.h"
#include "gui/widgets/text.h"
#include "gui/widgets/theme.h"
#include "model/player.h"
#include "tools/match_sandbox_page_ui.h"
#include "tools/match_sandbox_recording.h"
#include "tools/match_sandbox_typography.h"
#include "tools/match_sandbox_zone_recording.h"

namespace
{
constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
/** At most this many steps per frame, so a slow frame cannot snowball. */
constexpr int MAX_STEPS_PER_FRAME = 400;
constexpr std::array<float, 8> SPEEDS{0.5F, 1.0F,  2.0F,  4.0F,
                                      8.0F, 16.0F, 32.0F, 64.0F};
constexpr int SECONDS_PER_MINUTE = 60;
constexpr float ZONE_TITLE_FONT_SIZE = 16.0F;
/** Layout (scaled pixels). */
constexpr float GROUP_GAP = 12.0F;
constexpr float MIN_PITCH_HEIGHT = 200.0F;
constexpr float GROUND_CARD_CHROME = 56.0F;
constexpr float SPEED_COMBO_WIDTH = 90.0F;
constexpr float CAMERA_COMBO_WIDTH = 180.0F;
constexpr float PICK_COMBO_WIDTH = 160.0F;
constexpr float ZONE_COMBO_WIDTH = 220.0F;
/** From this width the panel sits beside the ground. */
constexpr float COLUMN_LAYOUT_WIDTH = 1000.0F;
constexpr float PANEL_WIDTH = 420.0F;
}  // namespace

ZoneSessionScene::ZoneSessionScene(GUIView* guiView_ptr, SessionPlan plan_ref,
                                   std::vector<TrainingPlayer> picks_list)
    : GUIScene(guiView_ptr),
      plan(std::move(plan_ref)),
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
  regularFont = font("Manrope-Regular.ttf", SandboxPageUI::REGULAR_FONT_SIZE);
  boldFont = font("Manrope-Bold.ttf", SandboxPageUI::BOLD_FONT_SIZE);
  restart();
}

void ZoneSessionScene::restart()
{
  run.reset();
  runError.clear();
  // Recorded, so the debugger can read each zone (T7e). A plan with a
  // problem doesn't start: the panel says why.
  try
  {
    run = std::make_unique<SessionRun>(
        plan, guiView->getController().getStatsConfig(), 1, true, picks);
  }
  catch (const std::invalid_argument& problem)
  {
    runError = problem.what();
  }
  watchedZone = 0;
  debuggedRun = nullptr;
  debugger.reset();
  inspector.reset();
  // The debugger's Step buttons step the whole session (every zone together).
  debugger.stepper = [this](int steps)
  {
    for (int step = 0; step < steps && run && !run->finished(); ++step)
    {
      run->step();
    }
  };
  accumulator = 0.0F;
  paused = false;
}

void ZoneSessionScene::update(float deltaTime)
{
  if (ImGui::IsKeyPressed(ImGuiKey_Space, false) &&
      !ImGui::GetIO().WantTextInput)
  {
    paused = !paused;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_R, false) && !ImGui::GetIO().WantTextInput)
  {
    restart();
  }
  if (!run || run->finished() || paused)
  {
    return;
  }
  accumulator += deltaTime * speed;
  int steps = 0;
  while (accumulator >= STEP && steps < MAX_STEPS_PER_FRAME && !run->finished())
  {
    accumulator -= STEP;
    run->step();
    ++steps;
  }
  if (steps == MAX_STEPS_PER_FRAME)
  {
    accumulator = 0.0F;
  }
}

void ZoneSessionScene::render()
{
  namespace Page = SandboxPageUI;
  const auto* viewport = ImGui::GetMainViewport();
  const float scale = Theme::scale();
  // The sandbox pages' theme for every control on the page.
  const Page::Style style(regularFont, scale);
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, Page::PAGE);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(SandboxPageUI::PAGE_PADDING * scale,
                             SandboxPageUI::PAGE_PADDING * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGui::Begin("##zone_session", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float gutter =
      (SandboxPageUI::PANEL_GAP * scale) + ImGui::GetStyle().ScrollbarSize;
  const bool columns = available.x >= COLUMN_LAYOUT_WIDTH * scale;
  const float panel = columns ? PANEL_WIDTH * scale : available.x - gutter;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  if (columns)
  {
    ImGui::BeginChild("##zone_session_controls",
                      ImVec2(panel + gutter, available.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderPanel(panel);
    ImGui::EndChild();
    ImGui::SameLine(0, SandboxPageUI::PANEL_GAP * scale);
    renderGround(
        ImVec2(std::max(scale, ImGui::GetContentRegionAvail().x), available.y));
  }
  else
  {
    ImGui::BeginChild("##zone_session_controls", available,
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderGround(ImVec2(
        panel, std::max(SandboxPageUI::MIN_CANVAS_HEIGHT * scale,
                        available.y * SandboxPageUI::NARROW_CANVAS_SHARE)));
    renderPanel(panel);
    ImGui::EndChild();
  }
  ImGui::PopStyleVar();
  ImGui::End();
}

namespace
{
/** A version picker: "Set-up's" (none) or one of `all`, by name. */
template <typename Id, typename All, typename Name>
void versionPicker(const char* label, std::optional<Id>& choice, const All& all,
                   Name name, float scale)
{
  const std::string current =
      choice ? std::string(name(*choice)) : std::string("Set-up's");
  ImGui::SetNextItemWidth(PICK_COMBO_WIDTH * scale);
  if (!SandboxPageUI::beginCombo(label, current.c_str()))
  {
    return;
  }
  if (ImGui::Selectable("Set-up's", !choice))
  {
    choice.reset();
  }
  for (const Id id : all)
  {
    if (ImGui::Selectable(std::string(name(id)).c_str(),
                          choice && *choice == id))
    {
      choice = id;
    }
  }
  ImGui::EndCombo();
}
}  // namespace

/** The speed picker, the 3D toggle and, in 3D, the camera. */
void ZoneSessionScene::renderViewControls(float scale)
{
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Speed");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(SPEED_COMBO_WIDTH * scale);
  const std::string speedLabel = std::format("{:g}x", speed);
  if (SandboxPageUI::beginCombo("##zone_speed", speedLabel.c_str()))
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
  ImGui::SameLine(0, GROUP_GAP * scale);
  if (ImGui::Checkbox("3D view", &view3d))
  {
    if (view3d)
    {
      renderer = std::make_unique<MatchRenderer3D>();
    }
    else
    {
      renderer = std::make_unique<MatchRenderer2D>();
    }
  }
  if (!view3d)
  {
    return;
  }
  constexpr std::array<std::pair<MatchCameraMode, const char*>, 3> CAMERAS{
      {{MatchCameraMode::BROADCAST, "Broadcast"},
       {MatchCameraMode::TACTICAL, "Tactical"},
       {MatchCameraMode::END, "End"}}};
  const char* current = "Broadcast";
  for (const auto& [mode, label] : CAMERAS)
  {
    if (mode == cameraMode)
    {
      current = label;
    }
  }
  ImGui::SetNextItemWidth(CAMERA_COMBO_WIDTH * scale);
  if (SandboxPageUI::beginCombo("Camera", current))
  {
    for (const auto& [mode, label] : CAMERAS)
    {
      if (ImGui::Selectable(label, mode == cameraMode))
      {
        cameraMode = mode;
      }
    }
    ImGui::EndCombo();
  }
}

/** Back, restart, play or pause, the view, and the clock. */
void ZoneSessionScene::renderPlayback(float width, float scale)
{
  namespace Page = SandboxPageUI;
  Page::beginCard("##zone_session_playback", "RUN SESSION", width, Page::DARK,
                  scale, boldFont);
  if (Page::button("Back to sandbox", false, scale, boldFont))
  {
    guiView->popScene();
  }
  ImGui::SameLine(0, SandboxPageUI::PANEL_GAP * scale);
  if (Page::button("Restart (R)", false, scale, boldFont))
  {
    restart();
  }
  const bool over = run->finished();
  ImGui::SameLine(0, SandboxPageUI::PANEL_GAP * scale);
  ImGui::BeginDisabled(over);
  if (Page::button(paused ? "Play (Space)" : "Pause (Space)", false, scale,
                   boldFont))
  {
    paused = !paused;
  }
  ImGui::EndDisabled();
  renderViewControls(scale);
  const int seconds = static_cast<int>(run->seconds());
  const char* state = "Running";
  if (over)
  {
    state = "Finished";
  }
  else if (paused)
  {
    state = "Paused";
  }
  UI::text("{} / {:d}:{:02d}", state, seconds / SECONDS_PER_MINUTE,
           seconds % SECONDS_PER_MINUTE);
  Page::endCard(scale);
}

/** The changing room (T7c): send a player there; he plays again in the
 * next exercise that names him. */
void ZoneSessionScene::renderChangingRoom(float width, float scale)
{
  namespace Page = SandboxPageUI;
  Page::beginCard("##zone_session_room", "CHANGING ROOM", width, Page::MID,
                  scale, boldFont);
  const std::vector<std::size_t> room = run->changingRoom();
  std::vector<std::size_t> playing;
  for (std::size_t index = 0; index < run->squad().size(); ++index)
  {
    if (std::ranges::find(room, index) == room.end())
    {
      playing.push_back(index);
    }
  }
  if (!run->finished() && !playing.empty())
  {
    renderRestPicker(playing, scale);
  }
  std::string resting;
  for (const std::size_t index : room)
  {
    resting += (resting.empty() ? "" : ", ") + run->squad()[index]->getName();
  }
  ImGui::PushTextWrapPos(0);
  UI::textDisabled("In the changing room: {}",
                   resting.empty() ? "nobody" : resting.c_str());
  ImGui::PopTextWrapPos();
  Page::endCard(scale);
}

void ZoneSessionScene::renderRestPicker(const std::vector<std::size_t>& playing,
                                        float scale)
{
  namespace Page = SandboxPageUI;
  if (std::ranges::find(playing, restPick) == playing.end())
  {
    restPick = playing.front();
  }
  ImGui::SetNextItemWidth(PICK_COMBO_WIDTH * scale);
  if (SandboxPageUI::beginCombo("##rest_pick",
                                run->squad()[restPick]->getName().c_str()))
  {
    for (const std::size_t index : playing)
    {
      if (ImGui::Selectable(run->squad()[index]->getName().c_str(),
                            index == restPick))
      {
        restPick = index;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine(0, SandboxPageUI::PANEL_GAP * scale);
  if (Page::button("Send to the changing room", false, scale, boldFont))
  {
    (void)run->sendToChangingRoom(restPick);
  }
}

/** Each zone of the exercise, live. */
void ZoneSessionScene::renderZones(float width, float scale)
{
  namespace Page = SandboxPageUI;
  Page::beginCard("##zone_session_zones", "ZONES", width, Page::LIGHT, scale,
                  boldFont);
  ImGui::PushTextWrapPos(0);
  if (run->finished())
  {
    ImGui::TextUnformatted("The session is over.");
  }
  else if (run->changing())
  {
    ImGui::TextUnformatted(
        "Changing exercise: the balls are with the coaches, and "
        "everyone walks to his place in the next exercise (marked "
        "out). It starts when they are all there.");
  }
  else
  {
    for (const SessionRun::Zone& zone : run->zones())
    {
      if (!zone.drill)
      {
        continue;
      }
      ImGui::PushFont(boldFont, ZONE_TITLE_FONT_SIZE);
      ImGui::TextUnformatted(zone.plan->name.c_str());
      ImGui::PopFont();
      const std::vector<std::string> lines =
          zone.run && zone.run->finished() ? zone.report : zone.drill->report();
      for (std::size_t line = 0; line < lines.size() && line < 3; ++line)
      {
        ImGui::TextUnformatted(lines[line].c_str());
      }
      ImGui::Separator();
    }
  }
  ImGui::PopTextWrapPos();
  Page::endCard(scale);
}

/** The exercises done so far, and what is to come. */
void ZoneSessionScene::renderSessionPlan(float width, float scale)
{
  namespace Page = SandboxPageUI;
  const std::size_t current =
      std::min(run->exercise(), plan.exercises.size() - 1);
  const bool over = run->finished();
  Page::beginCard("##zone_session_plan", "SESSION", width, Page::MID, scale,
                  boldFont);
  ImGui::PushTextWrapPos(0);
  ImGui::TextUnformatted(plan.summary.c_str());
  for (std::size_t exercise = 0; exercise < plan.exercises.size(); ++exercise)
  {
    const bool done = exercise < run->history().size();
    const char* status = "Next";
    if (done)
    {
      status = "Done";
    }
    else if (exercise == current && !over)
    {
      status = "Now";
    }
    UI::text("{:d} / {}: {}", exercise + 1,
             plan.exercises[exercise].name.c_str(), status);
    if (!done)
    {
      continue;
    }
    for (const std::string& line : run->history()[exercise])
    {
      if (line.find("OUTCOME") != std::string::npos)
      {
        UI::textDisabled("  {}", line.c_str());
      }
    }
  }
  ImGui::PopTextWrapPos();
  Page::endCard(scale);
}

void ZoneSessionScene::renderPanel(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  if (!run)
  {
    if (!runError.empty())
    {
      ImGui::PushTextWrapPos(0);
      UI::textDisabled("This session can't start: {}", runError.c_str());
      ImGui::PopTextWrapPos();
    }
    return;
  }
  const std::size_t exerciseCount = plan.exercises.size();
  const std::size_t index = std::min(run->exercise(), exerciseCount - 1);
  const std::string context =
      run->changing() && index + 1 < exerciseCount
          ? std::format(
                "Changing to exercise {} of {}: {}. Everyone walks to his "
                "place.",
                index + 2, exerciseCount, plan.exercises[index + 1].name)
          : std::format(
                "Exercise {} of {}: {}. Engine conditions come from Set-up.",
                index + 1, exerciseCount, plan.exercises[index].name);
  Page::header("Training", plan.name.c_str(), width, scale, boldFont,
               regularFont, context.c_str(), true);
  renderPlayback(width, scale);
  renderChangingRoom(width, scale);
  renderZoneTools(width);
  renderZones(width, scale);
  renderSessionPlan(width, scale);
}

void ZoneSessionScene::renderGround(ImVec2 area)
{
  const float width = area.x;
  const float height = area.y;
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##zone_session_ground", "TRAINING GROUND", width, Page::DARK,
                  scale, boldFont);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 size(std::max(scale, ImGui::GetContentRegionAvail().x),
                    std::max(MIN_PITCH_HEIGHT * scale,
                             height - (GROUND_CARD_CHROME * scale)));
  ImGui::InvisibleButton("##zone_session_ground", size);
  if (!run)
  {
    Page::endCard(scale);
    return;
  }
  MatchViewport view{
      .x = origin.x, .y = origin.y, .width = size.x, .height = size.y};
  fillSessionRenderSnapshot(*run, snapshot, watchedZone);
  snapshot.interpolationAlpha =
      paused || run->finished() ? 1.0F
                                : std::clamp(accumulator / STEP, 0.0F, 1.0F);
  MatchRenderOptions options;
  options.showPlayerNames = false;
  options.frameSeconds = ImGui::GetIO().DeltaTime;
  options.cameraMode = cameraMode;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                     true);
  renderer->render(snapshot, options, view);
  draw->PopClipRect();
  Page::endCard(scale);
}

/** Save the picked zone as far as it has played (T7f); it opens from the
 * Training page as a replay of that zone alone. */
void ZoneSessionScene::saveWatchedZone()
{
  std::string error;
  if (const auto recording = ZoneRecordings::capture(*run, watchedZone, error))
  {
    const std::filesystem::path path =
        Recordings::folder() / ZoneRecordings::fileName(*recording);
    saveMessage = ZoneRecordings::save(*recording, path, error)
                      ? std::format("Saved: {}", path.filename().string())
                      : std::format("Not saved: {}", error);
  }
  else
  {
    saveMessage = std::format("Not saved: {}", error);
  }
}

/** Its brain and body (both sides), in the scene's copy of the plan: brains
 * are set before a zone starts, so a change takes effect on Restart. The
 * picked zone's place in the plan comes from the zone itself (the run's copy
 * of the plan), not from its place among the running zones. */
void ZoneSessionScene::renderZoneVersions(float scale)
{
  const auto& zones = run->zones();
  const std::size_t exercise = run->exercise();
  const std::vector<SessionZone>& runPlanZones =
      run->plan().exercises[exercise].zones;
  const auto planIndex = static_cast<std::size_t>(
      std::distance(runPlanZones.data(), zones[watchedZone].plan));
  if (exercise >= plan.exercises.size() || planIndex >= runPlanZones.size() ||
      planIndex >= plan.exercises[exercise].zones.size())
  {
    return;
  }
  SessionZone& zone = plan.exercises[exercise].zones[planIndex];
  versionPicker("Brain", zone.brain, ALL_BRAINS, brainName, scale);
  ImGui::SameLine(0, GROUP_GAP * scale);
  versionPicker("Body", zone.body, ALL_BODIES, bodyName, scale);
  UI::textDisabled("Takes effect on Restart (R).");
}

/** The debugger on the picked zone (its log, the inspector). */
void ZoneSessionScene::renderZoneDebugger(float width, float scale)
{
  namespace Page = SandboxPageUI;
  const SessionRun::Zone& picked = run->zones()[watchedZone];
  if (!picked.run)
  {
    return;
  }
  if (picked.run.get() != debuggedRun)
  {
    debuggedRun = picked.run.get();
    debugger.reset();
    inspector.reset();
  }
  Page::beginCard("##zone_session_debugger", "DEBUGGER", width, Page::LIGHT,
                  scale, boldFont);
  MatchEngine& engine = picked.run->engine();
  debugger.renderContents(picked.run->recorder(), &engine, &paused,
                          std::nullopt, &engine, inspector);
  Page::endCard(scale);
}

void ZoneSessionScene::renderZoneTools(float width)
{
  // One zone picked (T7e): the 3D cameras follow its ball, its set-up can
  // be changed, and the debugger reads it.
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  if (!run || run->finished())
  {
    return;
  }
  Page::beginCard("##zone_session_tools", "ZONE", width, Page::MID, scale,
                  boldFont);
  const auto& zones = run->zones();
  if (run->changing() || zones.empty())
  {
    UI::textDisabled("Changing exercise: one engine for everyone.");
    Page::endCard(scale);
    return;
  }
  watchedZone = std::min(watchedZone, zones.size() - 1);
  ImGui::SetNextItemWidth(ZONE_COMBO_WIDTH * scale);
  if (SandboxPageUI::beginCombo("Zone", zones[watchedZone].plan->name.c_str()))
  {
    for (std::size_t index = 0; index < zones.size(); ++index)
    {
      if (ImGui::Selectable(zones[index].plan->name.c_str(),
                            index == watchedZone))
      {
        watchedZone = index;
      }
    }
    ImGui::EndCombo();
  }
  UI::textDisabled("The 3D cameras follow this zone's ball.");
  if (Page::button("Save this zone", false, scale, boldFont))
  {
    saveWatchedZone();
  }
  if (!saveMessage.empty())
  {
    ImGui::PushTextWrapPos(0);
    UI::textDisabled("{}", saveMessage.c_str());
    ImGui::PopTextWrapPos();
  }
  renderZoneVersions(scale);
  Page::endCard(scale);
  renderZoneDebugger(width, scale);
}
