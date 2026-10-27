// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <string>
#include <vector>

#include "database/user_database.h"

struct SDL_Window;

/**
 * @brief Manages imported custom databases ("*.fmdb"): import a file,
 * inspect it (manifest, counts, validation findings), activate one for new
 * careers, or remove an imported copy. Self-contained, like SetPiecesDialog.
 */
class ImportDatabaseDialog
{
 public:
  /** @brief Opens the dialog on the next render(). */
  void open()
  {
    open_requested = true;
    close_requested = false;
  }

  /** @brief Closes the dialog on the next render(). */
  void close() { close_requested = true; }

  /**
   * @brief Draws the dialog while it is open.
   * @param window Parent window for the system file picker (may be null).
   */
  void render(SDL_Window* window);

 private:
  struct Entry
  {
    std::string file;  /*!< File name inside the databases directory. */
    std::string title; /*!< "Name version" from the manifest, else the file. */
    bool active = false;
  };

  /** Re-scans the databases directory; keeps the selection when possible. */
  void refresh();
  /** Validates @p file into result_ for the detail pane. */
  void select(const std::string& file);
  void renderFileList(float scale);
  void renderDetail(float scale);
  void beginBrowse(SDL_Window* window);
  static void fileDialogCallback(void* userdata, const char* const* filelist,
                                 int filter);
  /** Imports a file the system picker returned (on the UI thread). */
  void importPicked(const std::string& path);

  bool open_requested = false;
  bool close_requested = false;
  std::vector<Entry> entries_;
  std::string selected_;
  bool has_result_ = false;
  UserDatabase::LoadResult result_;
  std::string status_message_;
  bool status_is_error_ = false;
  bool browser_open_ = false;
  std::string pending_pickup_;
  bool has_pickup_ = false;
};
