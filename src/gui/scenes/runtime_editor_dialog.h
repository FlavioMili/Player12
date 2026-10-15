// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "controller/game_controller.h"
#include "gui/nav_history.h"
#include "gui/view_models/editor_context.h"

/** A generated editor for the active schema. Owns a private draft database;
 * apply requests are consumed by GUIView at the next frame boundary. */
class RuntimeEditorDialog
{
 public:
  explicit RuntimeEditorDialog(GameController& controller);
  ~RuntimeEditorDialog();
  void open(const std::optional<NavEntry>& context);
  void open(const EditorContext& context);
  void render();
  bool isOpen() const { return opened; }
  enum class Request
  {
    None,
    Apply,
    Undo
  };
  Request takeRequest();
  const std::vector<RuntimeEditor::Change>& changes() const
  {
    return pending_changes;
  }
  std::shared_ptr<GameController::EditorSession> session() const
  {
    return editor_session;
  }
  void finish(bool success, std::string message);

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;
  struct Field
  {
    std::string name;
    std::string text;
    int type = SQLITE_TEXT;
    bool readonly = false;
  };
  void refresh();
  void chooseTable(const std::string& name);
  static void renderAttributes(Field& field);
  void selectRow(std::size_t index);
  void stage();
  void renderForm();
  void renderBrowser();
  bool formDirty() const;
  void close();
  GameController& controller;
  std::shared_ptr<GameController::EditorSession> editor_session;
  std::vector<RuntimeEditor::Table> tables;
  RuntimeEditor::Page page;
  std::string selected_table;
  PlayerID context_player = 0;
  TeamID context_team = 0;
  std::map<std::string, RuntimeEditor::Value, std::less<>> context_filter;
  std::map<std::string,
           std::map<std::string, RuntimeEditor::Value, std::less<>>,
           std::less<>>
      scoped_filters;
  std::array<char, 128> search{};
  int offset = 0;
  std::optional<RuntimeEditor::Row> original;
  std::vector<Field> fields;
  bool browse_all = false;
  bool show_records = false;
  std::string focus_label;
  bool opened = false;
  bool close_requested = false;
  bool popup_requested = false;
  bool apply_confirm = false;
  bool undo_confirm = false;
  std::string error;
  Request request = Request::None;
  std::vector<RuntimeEditor::Change> pending_changes;
};
