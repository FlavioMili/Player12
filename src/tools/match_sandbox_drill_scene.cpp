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
#include "global/paths.h"
#include "gui/gui_view.h"
#include "gui/render/match_renderer_2d.h"
#include "gui/render/match_renderer_3d.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "tools/match_sandbox_page_ui.h"
#include "tools/match_sandbox_setup.h"

namespace
{
constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
/** At most this many steps per frame, so a slow frame cannot snowball. */
constexpr int MAX_STEPS_PER_FRAME = 40;
constexpr std::array<float, 7> SPEEDS{0.25f, 0.5f, 1.0f, 2.0f,
                                      4.0f,  8.0f, 16.0f};
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

void DrillScene::onEnter()
{
  // Reuse the launch page's fonts. Direct drill entry loads them once into
  // the current atlas, so reopening a drill does not keep adding font sources.
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
  restart();
}

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
  ImGui::Begin("##drill", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const bool columns = available.x >= 1000 * scale;
  const float gutter = 6 * scale + ImGui::GetStyle().ScrollbarSize;
  const float panel = columns
                          ? (available.x >= 1400 * scale ? 440 : 400) * scale
                          : std::max(scale, available.x - gutter);
  const std::string context = std::format(
      "{} / Engine versions and simulation conditions come from Set-up.",
      drillCategoryName(drill.category));
  const auto controls = [&]
  {
    Page::header(drill.name(), drill.summary(), panel, scale, boldFont,
                 regularFont, context.c_str(), true);
    renderControls(panel);
    Page::beginCard("##drill_analysis_tabs", "VIEW", panel, Page::MID, scale,
                    boldFont);
    const char* tabs[] = {"Run", "Review", "Debugger"};
    for (int i = 0; i < 3; ++i)
    {
      if (i) ImGui::SameLine(0, 6 * scale);
      if (Page::button(tabs[i], analysisTab == i, scale, boldFont))
      {
        analysisTab = i;
      }
    }
    Page::endCard(scale);
    if (analysisTab == 0)
      renderPanel(panel);
    else
    {
      Page::beginCard("##drill_review_controls",
                      analysisTab == 1 ? "REVIEW" : "ENGINE ANALYSIS", panel,
                      Page::LIGHT, scale, boldFont);
      if (run)
        review.renderPanel(run->recorder(), &run->engine(), &paused, nullptr);
      ImGui::TextWrapped(
          analysisTab == 1 ? "The pitch shows the selected moment. Back to "
                             "live resumes the drill."
                           : "Select a log entry to review it on the pitch. "
                             "Inspector tabs share the same selected moment.");
      Page::endCard(scale);
    }
  };
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  if (columns)
  {
    // Only the controls/settings scroll. The pitch occupies all remaining
    // window space and stays visible while browsing a long parameter form.
    ImGui::BeginChild("##drill_page", ImVec2(panel + gutter, available.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    controls();
    ImGui::EndChild();
    ImGui::SameLine(0, 6 * scale);
    renderPitch(std::max(scale, ImGui::GetContentRegionAvail().x), available.y);
  }
  else
  {
    // Narrow windows keep a full-width stage, with the controls below it.
    ImGui::BeginChild("##drill_page", available,
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderPitch(panel, std::max(300 * scale, available.y * 0.65f));
    controls();
    ImGui::EndChild();
  }
  ImGui::PopStyleVar();
  ImGui::End();
}

void DrillScene::renderControls(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##run_controls", "RUN DRILL", width, Page::DARK, scale,
                  boldFont);
  const auto next = [&](float itemWidth)
  {
    if (ImGui::GetItemRectMax().x + 6 * scale + itemWidth <=
        ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
      ImGui::SameLine(0, 6 * scale);
  };
  const auto buttonNext = [&](const char* label)
  { next(Page::buttonWidth(label, scale, boldFont)); };
  if (Page::button("Back to sandbox", false, scale, boldFont))
    guiView->popScene();
  buttonNext("Restart (R)");
  if (Page::button("Restart (R)", true, scale, boldFont)) restart();
  const char* playback = paused ? "Play (Space)" : "Pause (Space)";
  buttonNext(playback);
  ImGui::BeginDisabled(!run || run->finished());
  if (Page::button(playback, false, scale, boldFont)) paused = !paused;
  buttonNext("Step 0.1 s");
  if (Page::button("Step 0.1 s", false, scale, boldFont)) stepRun(1);
  buttonNext("Step 1 s");
  if (Page::button("Step 1 s", false, scale, boldFont)) stepRun(10);
  ImGui::EndDisabled();
  next(160 * scale);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Speed");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90 * scale);
  const std::string label = std::format("{:g}x", speed);
  if (ImGui::BeginCombo("##speed", label.c_str()))
  {
    for (const float option : SPEEDS)
      if (ImGui::Selectable(std::format("{:g}x", option).c_str(),
                            option == speed))
        speed = option;
    ImGui::EndCombo();
  }
  ImGui::Checkbox("New seed each run", &newSeedEachRun);

  next(ImGui::CalcTextSize("3D view").x + 40 * scale);
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
    next(240 * scale);
    ImGui::SetNextItemWidth(180 * scale);
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
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Page::LIME),
                       "You run: WASD or the arrows (or a gamepad's stick), "
                       "hold Shift to sprint. Esc pauses.%s",
                       paused && run && run->engine().getSimulatedSteps() == 0
                           ? " Press Space or Esc to start."
                           : "");
    ImGui::PopTextWrapPos();
  }

  Page::endCard(scale);
}

void DrillScene::renderPanel(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##drill_result", "RESULT", width, Page::MID, scale,
                  boldFont);
  if (run)
  {
    ImGui::Text("%s / %.1f s",
                run->finished() ? "Finished"
                : paused        ? "Paused"
                                : "Running",
                static_cast<double>(run->engine().getSimulatedSteps()) * STEP);
    ImGui::PushTextWrapPos(0.0f);
    // A line that starts PASS or FAIL (after OUTCOME, if it is one) is
    // coloured by it.
    for (const std::string& line : drill.report())
    {
      std::string_view verdict = line;
      if (verdict.starts_with("OUTCOME  ")) verdict.remove_prefix(9);
      if (verdict.starts_with("PASS"))
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Page::LIME), "%s",
                           line.c_str());
      else if (verdict.starts_with("FAIL"))
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s", line.c_str());
      else
        ImGui::TextUnformatted(line.c_str());
    }
    ImGui::PopTextWrapPos();
  }

  ImGui::PushTextWrapPos(0);
  drill.renderAfterSettings();
  ImGui::PopTextWrapPos();
  Page::endCard(scale);
  Page::beginCard("##drill_settings", "SETTINGS", width, Page::LIGHT, scale,
                  boldFont);
  ImGui::PushItemWidth(-1);
  drill.renderSettings(width >= 360 * scale ? 2 : 1, true, true);
  ImGui::PopItemWidth();
  ImGui::TextDisabled("Restart to apply starting conditions.");
  Page::endCard(scale);
}

void DrillScene::renderPitch(float width, float height)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##pitch_card", "PITCH", width, Page::DARK, scale, boldFont);
  if (analysisTab == 2 && run)
  {
    ImGui::BeginChild("##drill_debug_content",
                      ImVec2(0, std::max(300 * scale, height - 56 * scale)),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    renderDebugTools();
    ImGui::EndChild();
    Page::endCard(scale);
    return;
  }
  if (analysisTab == 1 && run)
  {
    ImGui::BeginChild("##drill_review_pitch",
                      ImVec2(0, std::max(300 * scale, height - 56 * scale)));
    review.renderPitch(run->recorder(), &run->engine(), 0, 0, &inspector);
    ImGui::EndChild();
    Page::endCard(scale);
    return;
  }
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 size(std::max(scale, ImGui::GetContentRegionAvail().x),
                    std::max(200 * scale, height - 56 * scale));
  ImGui::InvisibleButton("##drill_pitch", size);
  if (!run)
  {
    Page::endCard(scale);
    return;
  }
  MatchViewport view = computeMatchViewport(origin.x, origin.y, size.x, size.y);
  view.x = origin.x + std::max(0.0f, (size.x - view.width) * 0.5f);
  view.y = origin.y + std::max(0.0f, (size.y - view.height) * 0.5f);
  // The 3D view fills the whole area (its camera frames the pitch).
  if (view3d) view = {origin.x, origin.y, size.x, size.y};
  fillMatchRenderSnapshot(run->engine(), snapshot);
  // Players not in the drill would wait by the bench like players sent off.
  std::erase_if(snapshot.players, [](const MatchRenderPlayer& player)
                { return !player.onPitch; });
  snapshot.interpolationAlpha =
      paused || run->finished() ? 1.0f
                                : std::clamp(accumulator / STEP, 0.0f, 1.0f);
  MatchRenderOptions options;
  options.showPlayerNames = true;
  options.frameSeconds = ImGui::GetIO().DeltaTime;
  options.cameraMode = cameraMode;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                     true);
  renderer->render(snapshot, options, view);
  if (play.isActive()) updateBasis();
  drill.drawOverlay(*renderer, *draw);
  draw->PopClipRect();
  Page::endCard(scale);
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
        DrillDraw::project(
            *renderer,
            PitchPoint{player.position.x + METRE_X, player.position.y},
            length) &&
        DrillDraw::project(
            *renderer,
            PitchPoint{player.position.x, player.position.y + METRE_Y}, width))
      basis = {length.x - origin.x, length.y - origin.y, width.x - origin.x,
               width.y - origin.y};
  }
}

void DrillScene::renderDebugTools()
{
  if (!run) return;
  MatchEngine& engine = run->engine();

  const MatchEngine* inspected =
      review.shownEngine() ? review.shownEngine() : &engine;
  debugger.renderContents(run->recorder(), &engine, &paused, review.cursor(),
                          inspected, inspector);
  if (const auto tick = debugger.takeSeekRequest())
  {
    paused = true;
    review.seek(run->recorder(), *tick);
    analysisTab = 1;
  }
}
