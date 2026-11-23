// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "database/world/world_document.h"
#include "database/world/world_library.h"
#include "gui/widgets/file_dialog.h"

struct SDL_Window;

/**
 * Modal that asks which world a new career is played in: the default world,
 * a world edited first, or a world database from the library (imported by
 * file picker or drag and drop). In manage mode it only lists, imports and
 * removes worlds (Settings). Self-contained, like SetPiecesDialog.
 */
class WorldChoiceDialog
{
 public:
  enum class Mode : std::uint8_t
  {
    Choose,  ///< New career: pick a world and start.
    Manage,  ///< Library upkeep only.
  };
  enum class Action : std::uint8_t
  {
    None,
    DefaultWorld,  ///< Start in the built-in world.
    EditDefault,   ///< Open the World editor on the built-in world.
    EditFile,      ///< Open the World editor on `world` (from `file`).
    Start,         ///< Start in `world`.
  };
  struct Result
  {
    Action action = Action::None;
    std::shared_ptr<const World::Document> world;
    std::filesystem::path file;
  };

  /** Opens on the next render(); @p select is a library file to preselect. */
  void open(Mode mode, const std::filesystem::path& select = {});
  void close() { close_requested = true; }
  bool isOpen() const { return is_open; }
  /** Imports a file the player dropped on the window or picked. */
  void importFile(const std::filesystem::path& path);
  /** Draws the dialog while it is open; the action chosen this frame. */
  Result render(SDL_Window* window);

  /** One localised line for a problem: section, entity, field and reason. */
  static std::string issueText(const World::Issue& issue);

 private:
  void refresh();
  void select(const std::filesystem::path& path);
  void renderCards(Result& result);
  void renderLibrary(SDL_Window* window, Result& result);
  void renderEntry(const World::Library::Entry& entry, Result& result);
  void renderSummary();
  void renderProblems() const;
  Result start();

  Mode mode = Mode::Choose;
  bool open_requested = false;
  bool close_requested = false;
  bool is_open = false;
  FileDialog file_dialog;
  std::vector<World::Library::Entry> entries;
  std::filesystem::path selected;
  std::optional<World::LoadResult> selection;  ///< Detail of the selection.
  std::filesystem::path removing;              ///< Awaiting confirmation.
  /** Problems of the last failed import or start, shown in red. */
  std::vector<World::Issue> problems;
  std::string problems_title;
  std::string notice;  ///< Last success message.
};
