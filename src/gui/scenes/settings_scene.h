// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once
#include <imgui.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gui/gui_constants.h"
#include "gui/gui_scene.h"
#include "gui/gui_view.h"
#include "gui/scenes/import_database_dialog.h"
#include "gui/input_actions.h"
#include "settings_manager.h"

/**
 * @brief Scene for managing application settings.
 *
 * Appearance, audio, match, guidance and control changes apply at once (a
 * live preview; Cancel restores them). Language and the display settings
 * (resolution, frame cap, fullscreen, VSync) apply when the player presses
 * Apply. Every row explains itself in a tooltip.
 */
class SettingsScene : public GUIScene
{
 public:
  /**
   * @brief Constructs a new SettingsScene.
   * @param guiView_ptr Pointer to the GUIView.
   */
  explicit SettingsScene(GUIView* guiView_ptr);

  /**
   * @brief Constructs a SettingsScene opened from inside a career.
   * @param guiView_ptr Pointer to the GUIView.
   * @param inCareer When true, leaving the scene returns to the career
   * instead of the main menu, and save wiping is not offered.
   */
  SettingsScene(GUIView* guiView_ptr, bool inCareer);

  /**
   * @brief Destroys the SettingsScene.
   */
  ~SettingsScene() = default;

  /**
   * @brief Updates scene logic.
   * @param deltaTime Time elapsed since last update.
   */
  void update(float deltaTime) override;

  /**
   * @brief Renders the scene.
   */
  void render() override;

  /**
   * @brief Called when entering the scene.
   */
  void onEnter() override;

  /**
   * @brief Gets the ID of this scene.
   * @return The SceneID (SETTINGS).
   */
  SceneID getID() const override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;
  void applyAndSaveSettings();
  void cancel();
  void leave();
  /** Restores every setting but the language (and seen tips). */
  void resetToDefaults();
  /** Points the Apply-time widgets at the current settings. */
  void syncPendingFromSettings();
  void renderGeneral();
  void renderAppearance();
  void renderGuidance();
  void renderAudio();
  void renderControls();
  void renderControlRow(Input::ActionId id, const Input::Action& action,
                        float labelWidth);
  void renderData();
  void renderSaving();
  void previewAppearance();
  /** Handles a key pressed while a binding waits for one. */
  void updateCapture();

  std::vector<std::string> languageOptions;
  std::vector<Language> availableLanguageEnums;
  std::vector<GUIConstants::Resolution> resolutions;
  std::vector<std::string> resolutionOptions;
  std::vector<std::string> fpsOptionsStrings;

  int selectedLanguage = 0;
  int selectedFPS = 0;
  int selectedResolution = 0;
  bool fullscreen = false;
  bool vsync = false;

  bool showWipeDataOverlay = false;
  float wipeDataTimer = 0.0f;
  ImportDatabaseDialog import_database_dialog;

  bool in_career = false;
  Settings original_settings;
  float pending_ui_scale = 1.0f;
  float pending_text_scale = 1.0f;

  /** Binding slot waiting for a key ("press a key"). */
  struct Capture
  {
    Input::ActionId action = 0;
    std::size_t slot = 0;
  };
  std::optional<Capture> capture;
  /** A chord refused because another action uses it. */
  struct Conflict
  {
    Input::ActionId action = 0;
    std::size_t slot = 0;
    ImGuiKeyChord chord = ImGuiKey_None;
    Input::ActionId other = 0;
  };
  std::optional<Conflict> conflict;
  /** Frame a capture ended on: Space/Enter must not also press a button. */
  int capture_end_frame = -1;
  /** Chord labels per action and slot, rebuilt when a binding changes. */
  std::vector<std::array<std::string, Input::BINDING_SLOTS>> chord_labels;
  std::uint32_t labels_revision = 0;
};
