// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include "database/world/world_document.h"
#include "gui/gui_scene.h"

/**
 * World editor: builds, edits, combines and saves world databases with the
 * in-game editor's controls (see docs/user/world-editor.md).
 */
class WorldEditorScene : public GUIScene
{
 public:
  /** Editing state; defined in world_editor_state.h. */
  struct State;
  struct Options
  {
    /** World to open; null opens the built-in world. */
    std::shared_ptr<const World::Document> world;
    /** File it was read from (library or elsewhere); empty for new worlds. */
    std::filesystem::path file;
    /** Set when opened from a new career's world choice: "Start career"
     * then starts that slot directly. */
    std::optional<int> career_slot;
  };

  WorldEditorScene(GUIView* view, Options options);
  ~WorldEditorScene() override;
  WorldEditorScene(const WorldEditorScene&) = delete;
  WorldEditorScene& operator=(const WorldEditorScene&) = delete;
  WorldEditorScene(WorldEditorScene&&) = delete;
  WorldEditorScene& operator=(WorldEditorScene&&) = delete;

  void handleEvent(const SDL_Event& event) override;
  void update(float deltaTime) override;
  void render() override;
  [[nodiscard]] SceneID getID() const override { return SceneID::WORLD_EDITOR; }

 private:
  friend struct WorldEditorTestAccess;
  std::unique_ptr<State> state;
};
