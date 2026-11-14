// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_drill_scene.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>
#include <random>
#include <string>
#include <string_view>

#include "controller/game_controller.h"
#include "gui/gui_view.h"
#include "gui/render/match_renderer_2d.h"
#include "gui/render/match_renderer_3d.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "tools/match_sandbox_setup.h"

namespace
{
constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
constexpr float PANEL_WIDTH = 330.0f;
/** At most this many steps per frame, so a slow frame cannot snowball. */
constexpr int MAX_STEPS_PER_FRAME = 40;
constexpr std::array<float, 7> SPEEDS{0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f,
                                      16.0f};
}  // namespace

DrillScene::DrillScene(GUIView* guiView_ptr, Drill& drill_ref,
                       std::function<void(MatchEngine&)> prepare)
    : GUIScene(guiView_ptr),
      drill(drill_ref),
      prepareRun(std::move(prepare)),
      renderer(std::make_unique<MatchRenderer2D>())
{
  // The debugger's Step buttons run the drill's script too.
  debugger.stepper = [this](int steps) { stepRun(steps); };
  review.hideOffPitch = true;
}

DrillScene::~DrillScene() = default;

SceneID DrillScene::getID() const { return SceneID::MATCH_SANDBOX; }

void DrillScene::onEnter() { restart(); }

void DrillScene::onExit()
{
  if (run && play.isActive()) play.end(run->engine());
  suspendKeyboardNavigation(false);
  GUIScene::onExit();
}

void DrillScene::handleEvent(const SDL_Event& event)
{
  if (play.isActive()) play.handleEvent(event);
}

void DrillScene::suspendKeyboardNavigation(bool suspend)
{
  if (suspend == navSuspended) return;
  ImGuiIO& io = ImGui::GetIO();
  if (suspend)
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
  else
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  navSuspended = suspend;
}

void DrillScene::restart()
{
  if (newSeedEachRun) drill.seed = std::random_device{}();
  // The windows forget the old run before it goes.
  debugger.reset();
  review.reset();
  inspector.reset();
  if (run && play.isActive()) play.end(run->engine());
  run.reset();
  run = std::make_unique<DrillRun>(
      drill, guiView->getController().getStatsConfig(), true, prepareRun);
  accumulator = 0.0f;
  // You run: the run waits for you to press Space.
  paused = drill.humanControl();
  if (drill.humanControl())
    play.begin(run->engine(), true, {PlayAutoSwitch::OFF, 1, 0.2f});
}

void DrillScene::stepRun(int steps)
{
  for (int step = 0; step < steps && run && !run->finished(); ++step)
  {
    // Your input for this step (sampled once per step, as in Play).
    if (play.isActive()) play.update(run->engine(), STEP, basis);
    run->step();
  }
}

void DrillScene::update(float deltaTime)
{
  if (ImGui::IsKeyPressed(ImGuiKey_Space, false) &&
      !ImGui::GetIO().WantTextInput)
    paused = !paused;
  if (ImGui::IsKeyPressed(ImGuiKey_R, false) && !ImGui::GetIO().WantTextInput)
    restart();
  if (play.isActive() && play.takePauseRequest()) paused = !paused;
  // While you steer, the keys belong to the runner, not the panel.
  const bool steering = play.isActive() && !paused && run && !run->finished();
  suspendKeyboardNavigation(steering);
  if (play.isActive() && !steering) play.clearPresses();
  if (!run || paused || run->finished()) return;
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

void DrillScene::render()
{
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  const float scale = Theme::scale();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(Theme::Space::L * scale, Theme::Space::M * scale));
  ImGui::Begin("##drill", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();
  const float panel = PANEL_WIDTH * scale;
  renderPanel(panel);
  ImGui::SameLine();
  renderPitch();
  ImGui::End();
  if (showDebugger) renderDebugTools();
}

void DrillScene::renderPanel(float width)
{
  if (!ImGui::BeginChild("##panel", ImVec2(width, 0.0f)))
  {
    ImGui::EndChild();
    return;
  }
  UI::pageHeader(drill.name());
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled("%s", drill.summary());
  ImGui::PopTextWrapPos();

  ImGui::SeparatorText("Settings");
  ImGui::PushItemWidth(-130.0f * Theme::scale());
  drill.renderSettings();
  drill.renderAfterSettings();
  ImGui::InputScalar("Seed", ImGuiDataType_U32, &drill.seed);
  ImGui::PopItemWidth();
  ImGui::Checkbox("New seed each run", &newSeedEachRun);
  ImGui::TextDisabled("Engine versions come from Set-up.");

  ImGui::SeparatorText("Run");
  if (UI::primaryButton("Restart (R)")) restart();
  ImGui::SameLine();
  if (ImGui::Button(paused ? "Play (Space)" : "Pause (Space)"))
    paused = !paused;
  if (ImGui::Button("Step 0.1 s")) stepRun(1);
  ImGui::SameLine();
  if (ImGui::Button("Step 1 s")) stepRun(10);
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
  ImGui::Checkbox("Debugger windows", &showDebugger);
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
    if (ImGui::BeginCombo("Camera", current))
    {
      for (const auto& [mode, label] : CAMERAS)
        if (ImGui::Selectable(label, mode == cameraMode)) cameraMode = mode;
      ImGui::EndCombo();
    }
  }
  if (play.isActive())
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ImVec4(1.0f, 0.86f, 0.3f, 1.0f),
                       "You run: WASD or the arrows (or a gamepad's stick), "
                       "hold Shift to sprint. Esc pauses.%s",
                       paused && run && run->engine().getSimulatedSteps() == 0
                           ? " Press Space or Esc to start."
                           : "");
    ImGui::PopTextWrapPos();
  }

  ImGui::SeparatorText("Result");
  if (run)
  {
    ImGui::Text("%s  ·  %.1f s", run->finished() ? "Finished" : "Running",
                static_cast<double>(run->engine().getSimulatedSteps()) * STEP);
    ImGui::PushTextWrapPos(0.0f);
    // A line that starts PASS or FAIL (after OUTCOME, if it is one) is
    // coloured by it.
    for (const std::string& line : drill.report())
    {
      std::string_view verdict = line;
      if (verdict.starts_with("OUTCOME  ")) verdict.remove_prefix(9);
      if (verdict.starts_with("PASS"))
        ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.5f, 1.0f), "%s", line.c_str());
      else if (verdict.starts_with("FAIL"))
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s", line.c_str());
      else
        ImGui::TextUnformatted(line.c_str());
    }
    ImGui::PopTextWrapPos();
  }

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::L * Theme::scale()));
  if (ImGui::Button("Back to the sandbox")) guiView->popScene();
  ImGui::EndChild();
}

void DrillScene::renderPitch()
{
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const ImVec2 size(std::max(1.0f, available.x), std::max(1.0f, available.y));
  ImGui::InvisibleButton("##drill_pitch", size);
  if (!run) return;
  MatchViewport view = computeMatchViewport(origin.x, origin.y, size.x, size.y);
  view.x = origin.x + std::max(0.0f, (size.x - view.width) * 0.5f);
  view.y = origin.y + std::max(0.0f, (size.y - view.height) * 0.5f);
  // The 3D view fills the whole area (its camera frames the pitch).
  if (view3d) view = {origin.x, origin.y, size.x, size.y};
  fillMatchRenderSnapshot(run->engine(), snapshot);
  // Players not in the drill would wait by the bench like players sent off.
  std::erase_if(snapshot.players,
                [](const MatchRenderPlayer& player) { return !player.onPitch; });
  snapshot.interpolationAlpha =
      paused || run->finished() ? 1.0f : std::clamp(accumulator / STEP, 0.0f, 1.0f);
  MatchRenderOptions options;
  options.showPlayerNames = true;
  options.frameSeconds = ImGui::GetIO().DeltaTime;
  options.cameraMode = cameraMode;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
  renderer->render(snapshot, options, view);
  if (play.isActive()) updateBasis();
  drill.drawOverlay(*renderer, *draw);
  draw->PopClipRect();
}

void DrillScene::updateBasis()
{
  // The stick follows the screen: pixels per metre along the pitch's length
  // and width at the runner, from the view's projection (as in Play).
  const PlayerID active = run->engine().getControlledPlayer();
  const float METRE_X = 1.0f;
  const float METRE_Y = 1.0f;
  for (const MatchPlayer& player : run->engine().getPlayers())
  {
    if (!player.player || player.player->getId() != active) continue;
    ImVec2 origin;
    ImVec2 length;
    ImVec2 width;
    if (DrillDraw::project(*renderer, player.position, origin) &&
        DrillDraw::project(*renderer,
                           PitchPoint{player.position.x + METRE_X, player.position.y},
                           length) &&
        DrillDraw::project(*renderer,
                           PitchPoint{player.position.x, player.position.y + METRE_Y},
                           width))
      basis = {length.x - origin.x, length.y - origin.y, width.x - origin.x,
               width.y - origin.y};
  }
}

void DrillScene::renderDebugTools()
{
  if (!run) return;
  MatchEngine& engine = run->engine();
  review.render(run->recorder(), &engine, &paused, 0, 0, &inspector);
  const MatchEngine* inspected =
      review.shownEngine() ? review.shownEngine() : &engine;
  debugger.render(run->recorder(), &engine, &paused, review.cursor(), inspected,
                  inspector);
  if (const auto tick = debugger.takeSeekRequest())
  {
    paused = true;
    review.seek(run->recorder(), *tick);
  }
}
