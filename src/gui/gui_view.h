// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once
#include <SDL3/SDL.h>

#include <array>
#include <memory>
#include <string_view>
#include <vector>

#include "controller/game_controller.h"
#include "gui/nav_history.h"
#include "gui/swipe_gesture.h"
#include "gui/view_models/editor_context.h"

class GUIScene;
class RuntimeEditorDialog;

/**
 * @brief Main view manager for the GUI. Handles scenes and overlays.
 */
class GUIView
{
 public:
  // TODO change to a unique_ptr
  /**
   * @brief Constructs a new GUIView.
   * @param controller_ref Reference to the game controller.
   */
  explicit GUIView(GameController& controller_ref);

  /**
   * @brief Destroys the GUIView.
   */
  ~GUIView();

  /**
   * @brief Starts the scene run loop.
   */
  void run();

  /**
   * @brief Profiles the first live-match frames and exits automatically.
   * @return True when all profiling frames were rendered.
   */
  bool runMatchRenderProfile();

  /**
   * @brief Changes the current scene.
   * @param newScene The new scene to switch to.
   *
   * Function to change the scene, this should be used
   * when we also want to remove the previous scene from
   * the Scenes Stack.
   */
  void changeScene(std::unique_ptr<GUIScene> newScene);

  /**
   * @brief Overlays a new scene on top of the current one.
   * @param overlay The scene to overlay.
   *
   * This functions adds a scene as an overlay, this
   * makes it possible to show faster the scene that
   * was previously dispalyed.
   * Example: When we are on the main game dashboard
   * if we want to display the Lineup, we could just
   * overlay on the main game dashboard since we will
   * for sure go back. Then if we click on a player to
   * see his profile, we can make a new overlay, and these
   * scenes will be just popped at the end of their usage.
   */
  void overlayScene(std::unique_ptr<GUIScene> overlay);

  /**
   * @brief Pops the top overlaid scene.
   *
   * This is the functio to pop Overlaid scenes
   */
  void popScene();

  /**
   * @brief Routine navigation: removes every overlay and shows a new screen.
   * @param scene Screen to show above the base scene, or nullptr to return
   * to the base scene itself.
   *
   * Unlike popScene() followed by overlayScene(), this is a single deferred
   * action, so it can safely be requested from inside a scene's render().
   */
  void navigateTo(std::unique_ptr<GUIScene> scene);

  /** @brief The scene underneath all overlays (e.g. the club dashboard). */
  GUIScene* getBaseScene() const;

  /** @brief Number of overlays currently stacked above the base scene. */
  size_t getOverlayDepth() const;

  /** @brief The scene shown now (the top overlay, else the base scene). */
  GUIScene* getTopScene() const { return getActiveScene(); }

  /**
   * @brief The scene popScene() would reveal (nullptr without overlays).
   */
  GUIScene* getSceneBelowTop() const;

  /**
   * @brief Screens visited in this career, recorded whenever the shown
   * screen changes; cleared whenever the base scene changes (a career is
   * started, loaded or left).
   */
  NavHistory& navHistory() { return nav_history; }
  const NavHistory& navHistory() const { return nav_history; }

  /**
   * @brief Marks the scene change just requested as a step through the
   * history (Back / Forward), so it is not recorded as a new visit. Any
   * scene request made after it cancels the mark.
   */
  void markHistoryStep() { history_step = true; }

  /** @brief Sideways touchpad swipe, shared by every career screen. */
  SwipeGesture& swipeGesture() { return swipe_gesture; }

  /**
   * @brief Stops the run loop and quits.
   */
  void quit();

  // Resource access for scenes

  /**
   * @brief Gets the SDL renderer.
   * @return The SDL_Renderer pointer.
   */
  SDL_Renderer* getRenderer() const;

  /**
   * @brief Gets the SDL window.
   * @return The SDL_Window pointer.
   */
  SDL_Window* getWindow() const;

  /**
   * @brief Gets the game controller.
   * @return Reference to the GameController.
   */
  GameController& getController() const;

  /**
   * @brief Freezes the frame being rendered into a texture at its end.
   *
   * Used as a dimmed backdrop while the game state must not be read (e.g.
   * days simulated in the background), so the screen stays visible without
   * any scene touching live data. While it exists it is drawn behind the UI
   * every frame, so scenes shown over it must use transparent windows.
   */
  void requestBackdropCapture();

  /** @brief True until the requested frame has been frozen. */
  bool isBackdropPending() const { return backdropPending; }

  /** @brief Frozen frame from requestBackdropCapture(), or nullptr. */
  SDL_Texture* getBackdrop() const { return backdropTexture; }

  /** @brief Frees the frozen frame. */
  void releaseBackdrop();

  /** Re-applies appearance settings (theme, scale, density) live. */
  void refreshTheme();

  /** Hands the saved autosave frequency and backup count to the controller. */
  void applySavePolicy();

  /**
   * Applies the saved window mode and size. The size is in logical units:
   * where window coordinates are physical pixels (Windows, scaled X11) it
   * is multiplied by the display scale, so 1280x720 at 200% opens a
   * 2560x1440-pixel window instead of a 640x360 layout, always kept
   * within the display's usable area. With @p resize false only the mode,
   * VSync and language are applied (a maximised window stays maximised).
   */
  void applyWindowSettings(bool resize = true);

  /** Captures the current renderer contents as a BMP image. */
  bool captureScreenshot(std::string_view path) const;
  /** Opens a schema-driven editor with optional page/entity context. */
  void openEditor(const std::optional<NavEntry>& context);
  void openEditor(const EditorContext& context);

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;
  friend class GameFlowTest_ManagementScreensMidSeason_Test;
  friend class GameFlowTest;
  bool initialize();
  void applyManagementTheme();
  float displayScale() const;
  void handleEvents();
  void update(float deltaTime);
  void render();

  // Helper method to get the currently active scene (top overlay or current
  // scene)
  GUIScene* getActiveScene() const;

  GameController& controller;
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  bool running;
  bool screenshotPending = false;
  bool backdropPending = false;
  std::unique_ptr<RuntimeEditorDialog> editor_dialog;
  void applyEditorRequest();
  SDL_Texture* backdropTexture = nullptr;

  // Live-match render diagnostics (Priority 0)
  static constexpr int MATCH_FRAME_TIMING_COUNT = 120;
  static constexpr float MATCH_PROFILE_FRAME_SECONDS = 1.0f / 60.0f;
  bool rendererIsSoftware = false;
  std::array<float, MATCH_FRAME_TIMING_COUNT> matchFrameTimes{};
  int matchFramesTimed = -1;
  void beginMatchRenderTimings();
  void reportMatchRenderTimings();

  // Main scene
  std::unique_ptr<GUIScene> currentScene;

  // Overlay scene stack (back() is the top)
  std::vector<std::unique_ptr<GUIScene>> sceneStack;

  NavHistory nav_history;
  bool history_step = false;
  SwipeGesture swipe_gesture;
  /** Records the screen shown after a scene change. */
  void recordHistory();

  // Deferred scene management
  enum class PendingAction
  {
    NONE,
    CHANGE,
    OVERLAY,
    POP,
    NAVIGATE
  };
  PendingAction pendingAction = PendingAction::NONE;
  std::unique_ptr<GUIScene> pendingScene;
  void applyPendingSceneChanges();
};
