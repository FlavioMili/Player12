// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

/**
 * The system open/save file picker for world databases (*.fmdb). SDL may call
 * back on another thread, so the result is handed over through a shared,
 * locked slot and collected by take() on the UI thread. One request at a
 * time; the object can be destroyed while the picker is still open.
 */
class FileDialog
{
 public:
  FileDialog();
  ~FileDialog();
  FileDialog(const FileDialog&) = delete;
  FileDialog& operator=(const FileDialog&) = delete;
  FileDialog(FileDialog&&) = delete;
  FileDialog& operator=(FileDialog&&) = delete;

  /** Shows the picker to choose an existing world file. */
  void open(SDL_Window* window);
  /** Shows the picker to choose where to write @p suggested_name. */
  void save(SDL_Window* window, const std::string& suggested_name);
  /** True while the picker is showing. */
  bool busy() const;
  /** The chosen path, once (empty when cancelled or still open). */
  std::optional<std::filesystem::path> take();

  /** First dropped file of an SDL drop event, if it is a file drop. */
  static std::optional<std::filesystem::path> dropped(const SDL_Event& event);

  /** Shared with the picker callback; opaque to users. */
  struct Slot;

 private:
  std::shared_ptr<Slot> slot;
};
