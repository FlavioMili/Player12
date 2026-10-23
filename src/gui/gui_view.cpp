// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/gui_view.h"

#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include "controller/game_controller.h"
#include "global/logger.h"
#include "global/paths.h"
#include "global/runtime_paths.h"
#include "gui/gui_scene.h"
#include "gui/input_actions.h"
#include "gui/render_scale.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/main_menu_scene.h"
#include "gui/scenes/match_scene.h"
#include "gui/scenes/runtime_editor_dialog.h"
#include "gui/scenes/team_selection_scene.h"
#include "gui/widgets/theme.h"
#include "imgui.h"
#include "settings_manager.h"

GUIView::GUIView(GameController& controller_ref)
    : controller(controller_ref), running(false), currentScene(nullptr)
{
}

GUIView::~GUIView()
{
  editor_dialog.reset();
  // Clean up any overlaid scenes and active scenes before shutting down
  // renderer
  while (!sceneStack.empty())
  {
    sceneStack.pop_back();
  }
  currentScene.reset();
  pendingScene.reset();
  releaseBackdrop();

  if (renderer != nullptr)
  {
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
  }
  if (window != nullptr)
  {
    SDL_DestroyWindow(window);
  }
  TTF_Quit();
  SDL_Quit();
}

bool GUIView::initialize()
{
  // Initialize SDL
  // Prefer native Wayland over XWayland: XWayland windows are upscaled by the
  // compositor on fractionally scaled outputs, which makes every glyph blurry.
  // Only a default: SDL_VIDEO_DRIVER or an explicit hint still wins.
  if (std::getenv("WAYLAND_DISPLAY") != nullptr &&
      SDL_GetHint(SDL_HINT_VIDEO_DRIVER) == nullptr)
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "wayland,x11",
                            SDL_HINT_DEFAULT);

  if (!SDL_Init(SDL_INIT_VIDEO))
  {
    std::cerr << "Failed to initialize SDL: " << SDL_GetError() << '\n';
    return false;
  }

  // Initialize SDL_ttf
  if (!TTF_Init())
  {
    std::cerr << "Failed to initialize SDL_ttf: " << SDL_GetError() << '\n';
    return false;
  }

  // Create window
  // High pixel density: the swapchain matches the output's real pixels, and
  // ImGui rasterises glyphs at that density (DisplayFramebufferScale), so
  // text stays sharp on HiDPI and fractionally scaled displays.
  window =
      SDL_CreateWindow("Player12", 1280, 720,
                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (window == nullptr)
  {
    std::cerr << "Failed to create window: " << SDL_GetError() << '\n';
    return false;
  }

  // Create renderer
  renderer = SDL_CreateRenderer(window, nullptr);
  if (renderer == nullptr)
  {
    std::cerr << "Failed to create renderer: " << SDL_GetError() << '\n';
    return false;
  }
  const char* rendererName = SDL_GetRendererName(renderer);
  Logger::info(std::string("SDL renderer driver: ") +
               (rendererName ? rendererName : "unknown"));
  rendererIsSoftware = (rendererName != nullptr &&
                        std::string_view(rendererName).contains("software"));
  if (rendererIsSoftware)
  {
    Logger::warn(
        "Software renderer active (SDL chose a CPU rendering driver). Match "
        "performance and responsiveness may be poor; install/select a GPU "
        "driver if the match appears slow.");
  }

  SettingsManager::instance()->load();
  // Bindings stored in the file replace the defaults.
  Input::registry().reloadFromSettings();
  applyWindowSettings();
  applySavePolicy();

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  static std::string iniPath = RuntimePaths::imguiIniPath().string();
  io.IniFilename = iniPath.c_str();
  applyManagementTheme();

  // One TTF serves every typography level: the dynamic atlas bakes glyphs
  // on demand for each size requested through Theme::ScopedText.
  std::string fontPath = AssetPaths::font();
  ImFontConfig fontConfig;
  fontConfig.OversampleH = 2;
  if (io.Fonts->AddFontFromFileTTF(fontPath.c_str(),
                                   Theme::textSize(Theme::Text::BODY),
                                   &fontConfig) == nullptr)
  {
    std::cerr << "Failed to load font: " << fontPath << '\n';
    return false;
  }

  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  changeScene(std::make_unique<MainMenuScene>(this));

  return true;
}

void GUIView::run()
{
  if (!initialize())
  {
    return;
  }

  running = true;
  Uint64 lastTime = SDL_GetTicksNS();

  while (running)
  {
    const Uint64 frameStart = SDL_GetTicksNS();
    const auto deltaTime =
        static_cast<float>(static_cast<double>(frameStart - lastTime) / 1e9);
    lastTime = frameStart;

    applyPendingSceneChanges();

    handleEvents();
    update(deltaTime);
    render();

    const int fpsLimit =
        std::clamp(SettingsManager::instance()->get().fps_limit, 15, 360);
    // Nanosecond budget: whole milliseconds turned a 144 cap into ~166.
    const Uint64 frameBudget =
        static_cast<Uint64>(SDL_NS_PER_SECOND) / static_cast<Uint64>(fpsLimit);
    const Uint64 elapsed = SDL_GetTicksNS() - frameStart;
    if (elapsed < frameBudget) SDL_DelayPrecise(frameBudget - elapsed);
  }

  // Release scenes before the caller saves controller state. A scene may own
  // bounded background work, and its destructor joins that work safely.
  while (!sceneStack.empty()) sceneStack.pop_back();
  currentScene.reset();
  pendingScene.reset();
}

bool GUIView::runMatchRenderProfile()
{
  if (!controller.isGameLoaded())
  {
    Logger::error("Match render profiling requires an existing loaded save");
    return false;
  }

  const auto& teams = controller.getTeams();
  if (teams.size() < 2)
  {
    Logger::error("Match render profiling requires at least two teams");
    return false;
  }

  TeamID homeTeamId = teams.front().get().getId();
  if (const auto managedTeam = controller.getManagedTeam();
      managedTeam.has_value())
  {
    homeTeamId = managedTeam->get().getId();
  }
  else
  {
    controller.selectManagedTeam(homeTeamId);
  }

  const auto opponent = std::ranges::find_if(
      teams, [homeTeamId](const std::reference_wrapper<const Team>& team)
      { return team.get().getId() != homeTeamId; });
  if (opponent == teams.end())
  {
    Logger::error("Match render profiling could not find an opponent");
    return false;
  }

  if (!initialize()) return false;

  applyPendingSceneChanges();
  overlayScene(
      std::make_unique<MatchScene>(this, homeTeamId, opponent->get().getId()));
  applyPendingSceneChanges();

  running = true;
  while (running && matchFramesTimed < MATCH_FRAME_TIMING_COUNT)
  {
    handleEvents();
    update(MATCH_PROFILE_FRAME_SECONDS);
    render();
  }

  const bool completed = matchFramesTimed == MATCH_FRAME_TIMING_COUNT;
  if (!completed)
  {
    Logger::warn("Match render profiling ended before collecting 120 frames");
  }
  running = false;
  return completed;
}

void GUIView::handleEvents()
{
  SDL_Event event;
  while (SDL_PollEvent(&event))
  {
    ImGui_ImplSDL3_ProcessEvent(&event);
    if (event.type == SDL_EVENT_QUIT)
    {
      running = false;
    }

    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F12)
    {
      screenshotPending = true;
    }

    if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
        event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
      applyManagementTheme();
    }

    if (event.type == SDL_EVENT_WINDOW_RESIZED)
    {
      int width = 0;
      int height = 0;
      SDL_GetWindowSizeInPixels(window, &width, &height);
      GUIScene* activeScene = getActiveScene();
      if (activeScene != nullptr)
      {
        activeScene->onResize(width, height);
      }
    }

    // Pass event to the topmost scene
    // (overlay if exists, otherwise current scene)
    GUIScene* activeScene = getActiveScene();
    if (activeScene != nullptr)
    {
      activeScene->handleEvent(event);
    }
  }
}

void GUIView::update(float deltaTime)
{
  // Update only the active scene
  // (topmost overlay or current scene)
  GUIScene* activeScene = getActiveScene();
  if (activeScene != nullptr)
  {
    activeScene->update(deltaTime);
  }
}

void GUIView::render()
{
  const auto renderStart = std::chrono::steady_clock::now();

  // Clear screen with dark background
  SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
  SDL_RenderClear(renderer);

  // A frozen frame (see requestBackdropCapture) sits behind the UI; scenes
  // drawing over it use transparent windows.
  if (backdropTexture != nullptr && !backdropPending)
    SDL_RenderTexture(renderer, backdropTexture, nullptr, nullptr);

  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  // Render only the active scene
  // (topmost overlay or current scene)
  if (GUIScene* activeScene = getActiveScene(); activeScene != nullptr)
  {
    activeScene->render();
  }

  if (editor_dialog) editor_dialog->render();

  ImGui::Render();
  // The SDL_Renderer backend scales only clip rectangles by the framebuffer
  // scale; vertex positions are in window coordinates. Without this, a
  // HiDPI output (e.g. Wayland scale 2) shows the UI in the top-left quarter.
  {
    const ScopedRenderScale scale(renderer,
                                  ImGui::GetIO().DisplayFramebufferScale);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
  }

  if (backdropPending)
  {
    backdropPending = false;
    releaseBackdrop();
    if (SDL_Surface* frame = SDL_RenderReadPixels(renderer, nullptr))
    {
      backdropTexture = SDL_CreateTextureFromSurface(renderer, frame);
      SDL_DestroySurface(frame);
      // Read-back alpha is undefined on some drivers; the frame is opaque.
      if (backdropTexture != nullptr)
        SDL_SetTextureBlendMode(backdropTexture, SDL_BLENDMODE_NONE);
    }
  }

  bool capturedScreenshot = false;
  if (screenshotPending)
  {
    const char* configuredPath = std::getenv("FM_SCREENSHOT_PATH");
    if (const std::string path =
            configuredPath && *configuredPath
                ? configuredPath
                : RuntimePaths::capturePath("screenshot.bmp").string();
        !captureScreenshot(path))
      std::cerr << "Failed to capture screenshot: " << SDL_GetError() << '\n';
    screenshotPending = false;
    capturedScreenshot = true;
  }

  // Present the rendered frame
  SDL_RenderPresent(renderer);

  // Keep screenshot encoding outside normal frame timing. A negative count
  // means no live match has been entered yet.
  if (!capturedScreenshot && matchFramesTimed >= 0 &&
      matchFramesTimed < MATCH_FRAME_TIMING_COUNT)
  {
    const auto renderMs =
        static_cast<float>(std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - renderStart)
                               .count());
    matchFrameTimes[static_cast<size_t>(matchFramesTimed++)] = renderMs;
    if (matchFramesTimed == MATCH_FRAME_TIMING_COUNT)
    {
      reportMatchRenderTimings();
    }
  }
}

void GUIView::beginMatchRenderTimings()
{
  matchFrameTimes.fill(0.0f);
  matchFramesTimed = 0;
  Logger::info("Measuring the first 120 live-match render/present frames");
}

void GUIView::reportMatchRenderTimings()
{
  std::vector<float> samples(matchFrameTimes.begin(), matchFrameTimes.end());
  std::ranges::sort(samples);
  const float median = samples[samples.size() / 2];
  const float p95 = samples[static_cast<size_t>(
      std::floor(0.95 * static_cast<double>(samples.size() - 1)))];
  const float worst = samples.back();
  Logger::info(std::format(
      "Live-match render/present timings over the first 120 frames: median "
      "{} ms, p95 {} ms, worst {} ms",
      median, p95, worst));
}

void GUIView::changeScene(std::unique_ptr<GUIScene> newScene)
{
  pendingAction = PendingAction::CHANGE;
  pendingScene = std::move(newScene);
  history_step = false;
}

void GUIView::overlayScene(std::unique_ptr<GUIScene> overlay)
{
  pendingAction = PendingAction::OVERLAY;
  pendingScene = std::move(overlay);
  history_step = false;
}

void GUIView::popScene()
{
  pendingAction = PendingAction::POP;
  history_step = false;
}

void GUIView::navigateTo(std::unique_ptr<GUIScene> scene)
{
  pendingAction = PendingAction::NAVIGATE;
  pendingScene = std::move(scene);
  history_step = false;
}

GUIScene* GUIView::getBaseScene() const { return currentScene.get(); }

size_t GUIView::getOverlayDepth() const { return sceneStack.size(); }

GUIScene* GUIView::getSceneBelowTop() const
{
  if (sceneStack.empty()) return nullptr;
  return sceneStack.size() > 1 ? sceneStack[sceneStack.size() - 2].get()
                               : currentScene.get();
}

void GUIView::recordHistory()
{
  const bool step = history_step;
  history_step = false;
  // A step through the history already moved to the entry being shown.
  if (step) return;
  if (const GUIScene* shown = getActiveScene())
    if (const std::optional<NavEntry> entry = shown->historyEntry())
      nav_history.visit(*entry);
}

void GUIView::applyPendingSceneChanges()
{
  applyEditorRequest();
  if (pendingAction == PendingAction::NONE) return;
  while (pendingAction != PendingAction::NONE)
  {
    PendingAction currentAction = pendingAction;
    std::unique_ptr<GUIScene> sceneToApply = std::move(pendingScene);

    // Reset state before processing, so that onEnter/onExit can trigger new
    // scene changes
    pendingAction = PendingAction::NONE;

    if (currentAction == PendingAction::CHANGE)
    {
      // A new base scene starts a new career (or leaves it): its history
      // must not lead back into the previous one.
      nav_history.clear();
      swipe_gesture.reset();
      // Clear any overlays when changing main scene
      while (!sceneStack.empty())
      {
        sceneStack.back()->onExit();
        sceneStack.pop_back();
      }

      // Exit current scene
      if (currentScene)
      {
        currentScene->onExit();
      }

      // Switch to new scene
      currentScene = std::move(sceneToApply);

      // Enter new scene
      if (currentScene)
      {
        currentScene->onEnter();
      }
    }
    else if (currentAction == PendingAction::OVERLAY)
    {
      if (sceneToApply)
      {
        sceneToApply->onEnter();
        if (sceneToApply->getID() == SceneID::MATCH)
        {
          beginMatchRenderTimings();
        }
        sceneStack.push_back(std::move(sceneToApply));
      }
    }
    else if (currentAction == PendingAction::POP)
    {
      if (!sceneStack.empty())
      {
        // Exit the top overlay scene
        sceneStack.back()->onExit();
        sceneStack.pop_back();
        if (GUIScene* revealed = getActiveScene()) revealed->onResume();
      }
    }
    else if (currentAction == PendingAction::NAVIGATE)
    {
      const bool hadOverlays = !sceneStack.empty();
      while (!sceneStack.empty())
      {
        sceneStack.back()->onExit();
        sceneStack.pop_back();
      }
      if (sceneToApply)
      {
        sceneToApply->onEnter();
        if (sceneToApply->getID() == SceneID::MATCH)
        {
          beginMatchRenderTimings();
        }
        sceneStack.push_back(std::move(sceneToApply));
      }
      else if (hadOverlays && currentScene)
      {
        currentScene->onResume();
      }
    }
  }
  recordHistory();
}

void GUIView::quit() { running = false; }

SDL_Renderer* GUIView::getRenderer() const { return renderer; }

SDL_Window* GUIView::getWindow() const { return window; }

GameController& GUIView::getController() const { return controller; }

bool GUIView::captureScreenshot(std::string_view path) const
{
  if (!renderer || path.empty()) return false;
  // A bare file name has no folder to create; a failure to create one is
  // reported by the save below instead of throwing out of the frame.
  if (const std::filesystem::path folder =
          std::filesystem::path(path).parent_path();
      !folder.empty())
  {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
  }
  SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
  if (!surface) return false;
  const bool saved = SDL_SaveBMP(surface, std::string(path).c_str());
  SDL_DestroySurface(surface);
  return saved;
}

// Return the topmost scene
// (overlay if exists, otherwise current scene)
GUIScene* GUIView::getActiveScene() const
{
  if (!sceneStack.empty())
  {
    return sceneStack.back().get();
  }
  return currentScene.get();
}

float GUIView::displayScale() const
{
  if (window == nullptr) return 1.0f;
  // Display scale = pixel density x content scale. Window coordinates (and
  // therefore ImGui's) already include the pixel density wherever the
  // platform reports one (Wayland, macOS), so only the remaining content
  // scale enlarges the layout. On Windows density is 1 and the whole display
  // scale applies.
  const float display = SDL_GetWindowDisplayScale(window);
  const float density = SDL_GetWindowPixelDensity(window);
  if (display <= 0.0f) return 1.0f;
  return density > 0.0f ? display / density : display;
}

void GUIView::applyManagementTheme()
{
  // Palette, spacing and typography live in the shared design system so
  // scenes and widgets draw from the same tokens. Re-applied when appearance
  // settings change or the window moves to a display with another scale.
  const Settings& settings = SettingsManager::instance()->get();
  Theme::Appearance appearance;
  appearance.preset = static_cast<Theme::Preset>(std::clamp(
      settings.theme_preset, 0,
      static_cast<int>(std::to_underlying(Theme::Preset::COUNT)) - 1));
  appearance.club_accent = settings.club_accent;
  appearance.custom_accent = Theme::unpackRgb(settings.accent_rgb);
  appearance.ui_scale = settings.ui_scale;
  appearance.compact = settings.compact_density;
  appearance.reduced_motion = settings.reduced_motion;
  appearance.color_vision = static_cast<Theme::ColorVision>(std::clamp(
      settings.color_vision, 0,
      static_cast<int>(std::to_underlying(Theme::ColorVision::COUNT)) - 1));
  appearance.text_scale = settings.text_scale;
  Theme::apply(appearance, displayScale());
}

void GUIView::refreshTheme() { applyManagementTheme(); }

void GUIView::applyWindowSettings(bool resize)
{
  SettingsManager* settings = SettingsManager::instance();
  settings->apply(window);
  if (renderer != nullptr)
    SDL_SetRenderVSync(renderer,
                       settings->get().vsync ? 1 : SDL_RENDERER_VSYNC_DISABLED);
  if (window == nullptr || settings->get().fullscreen || !resize) return;
  // Wayland and macOS report the scale as pixel density (displayScale() is 1
  // there); elsewhere the logical size is scaled up, within the display.
  const float scale = std::max(1.0f, displayScale());
  auto width = static_cast<int>(std::lround(
      static_cast<float>(settings->get().resolution_width) * scale));
  auto height = static_cast<int>(std::lround(
      static_cast<float>(settings->get().resolution_height) * scale));
  if (SDL_Rect usable{};
      displayScale() > 1.0f &&
      SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &usable))
  {
    width = std::min(width, usable.w);
    height = std::min(height, usable.h);
  }
  // A maximised window ignores a new size until it is restored.
  if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0)
    SDL_RestoreWindow(window);
  SDL_SetWindowSize(window, width, height);
}

void GUIView::applySavePolicy()
{
  const Settings& settings = SettingsManager::instance()->get();
  AutosavePolicy policy;
  policy.frequency = static_cast<AutosaveFrequency>(std::clamp(
      settings.autosave_frequency, 0,
      static_cast<int>(std::to_underlying(AutosaveFrequency::SeasonEnd))));
  policy.backups = std::clamp(settings.autosave_backups, 0, 9);
  controller.setAutosavePolicy(policy);
}

void GUIView::requestBackdropCapture() { backdropPending = true; }

void GUIView::releaseBackdrop()
{
  if (backdropTexture != nullptr) SDL_DestroyTexture(backdropTexture);
  backdropTexture = nullptr;
}

void GUIView::openEditor(const std::optional<NavEntry>& context)
{
  if (const auto* hub = dynamic_cast<MainGameScene*>(getBaseScene());
      !controller.isGameLoaded() || !hub || hub->isContinuing() ||
      (getTopScene() && getTopScene()->getID() == SceneID::MATCH))
    return;
  if (!editor_dialog)
    editor_dialog = std::make_unique<RuntimeEditorDialog>(controller);
  editor_dialog->open(context);
}

void GUIView::applyEditorRequest()
{
  if (!editor_dialog) return;
  const auto request = editor_dialog->takeRequest();
  if (request == RuntimeEditorDialog::Request::None) return;
  const auto context =
      getActiveScene() ? getActiveScene()->historyEntry() : std::nullopt;
  std::string error;
  const bool success =
      request == RuntimeEditorDialog::Request::Undo
          ? controller.undoEditor(error)
          : controller.applyEditor(editor_dialog->session(),
                                   editor_dialog->changes(), error);
  editor_dialog->finish(success, std::move(error));
  if (!success) return;
  // Replacement happens outside render/update. Discard cached model references
  // before reopening a screen against the new world.
  changeScene(std::make_unique<MainGameScene>(this));
  applyPendingSceneChanges();
  if (!context || !Navigation::canOpen(this, *context)) return;
  using enum NavEntry::Kind;
  switch (context->kind)
  {
    case SECTION:
      Navigation::open(this, context->section);
      break;
    case PLAYER:
      Navigation::openPlayer(this, context->player);
      break;
    case CLUB:
      Navigation::openClub(this, context->team);
      break;
    case COMPARE:
      Navigation::openCompare(this, context->player, context->second_player);
      break;
    case MATCH_REPORT:
      Navigation::openMatchReport(this, context->date, context->team,
                                  context->away_team);
      break;
  }
}

void GUIView::openEditor(const EditorContext& context)
{
  if (const auto* hub = dynamic_cast<MainGameScene*>(getBaseScene());
      !controller.isGameLoaded() || !hub || hub->isContinuing() ||
      (getTopScene() && getTopScene()->getID() == SceneID::MATCH))
    return;
  if (!editor_dialog)
    editor_dialog = std::make_unique<RuntimeEditorDialog>(controller);
  editor_dialog->open(context);
}
