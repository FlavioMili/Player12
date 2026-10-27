// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "database/world/world_combine.h"
#include "database/world/world_document.h"
#include "database/world/world_library.h"
#include "database/world/world_schema.h"
#include "database/world/world_validate.h"
#include "gui/gui_view.h"
#include "gui/scenes/world_editor_scene.h"
#include "gui/view_models/editor_fields.h"
#include "gui/widgets/editor_form.h"
#include "gui/widgets/file_dialog.h"
#include "gui/widgets/widgets.h"

/**
 * Everything the World editor knows: the document being edited, its history,
 * list filters, the form values and the combine session. The scene renders it;
 * the editing operations live here so they can be exercised without drawing.
 */
struct WorldEditorScene::State
{
  enum class Page : std::uint8_t
  {
    Details,
    Language,
    Confederation,
    Region,
    Country,
    City,
    Travel,
    League,
    Club,
    Player,
    Problems,
    Conflicts,
  };
  enum class Popup : std::uint8_t
  {
    None,
    New,
    Chooser,
    Discard,
    Delete,
    Summary,
    Dropped,
  };
  enum class Action : std::uint8_t
  {
    None,
    NewTemplate,
    NewEmpty,
    Open,
    Close,
  };
  enum class Purpose : std::uint8_t
  {
    Open,
    Combine,
    Export,
  };
  struct Snapshot
  {
    std::string text;
    std::vector<World::Conflict> conflicts;
    std::vector<World::PossibleDuplicate> duplicates;
  };

  State(GUIView* owner, Options start);

  // Document and history.
  GUIView* view;
  Options options;
  World::Document doc;
  std::filesystem::path file;
  bool dirty = false;
  std::deque<Snapshot> undo;
  std::deque<Snapshot> redo;
  std::string last_edit;

  // Navigation, list and selection.
  Page page = Page::Details;
  std::string search;
  std::int64_t parent_filter = -1;  ///< -1 all, 0 none, else a parent id.
  std::vector<std::uint32_t> rows;  ///< Section positions shown in the list.
  std::vector<std::pair<std::int64_t, std::string>> parent_options;
  std::unordered_map<std::int64_t, std::uint32_t> counts;
  bool rows_dirty = true;
  std::int64_t selected = -1;  ///< Entity id (travel: position), -1 none.
  bool scroll_to_selected = false;

  // Form of the selected entity (or the manifest).
  std::vector<EditorForm::Value> values;
  std::vector<EditorField> descriptors;
  std::vector<std::string> original;
  bool form_stale = true;

  // Problems, recomputed lazily after edits.
  std::vector<World::Issue> issues;
  bool issues_stale = true;
  float since_edit = 1.0F;

  // Combine session.
  std::vector<World::Conflict> conflicts;
  std::vector<World::PossibleDuplicate> duplicates;
  std::optional<World::CombineResult> combined;
  std::string combined_name;
  int conflict_selected = 0;  ///< Conflicts first, then possible duplicates.
  std::vector<char> take_incoming;
  int bulk_source = 0;

  // Dialogs and feedback.
  FileDialog dialog;
  Purpose purpose = Purpose::Open;
  Popup popup = Popup::None;
  Popup open_popup = Popup::None;  ///< Popup requested for the next frame.
  Action pending = Action::None;
  std::filesystem::path pending_path;
  bool choosing_combine = false;
  std::vector<World::Library::Entry> library;
  std::string popup_text;  ///< Body of the delete confirmation.
  std::string status;
  bool status_error = false;
  float status_seconds = 0.0F;
  std::unique_ptr<GUIScene> next_scene;

  // Pages and kinds.
  static std::optional<World::Kind> kindOf(Page page);
  static Page pageOf(World::Kind kind);
  std::optional<World::Kind> kind() const { return kindOf(page); }
  nlohmann::json* entity(World::Kind kind, std::int64_t key);
  std::string selectedName() const;

  // Loading and files.
  void load(World::Document document, std::filesystem::path path, bool changed);
  bool openFile(const std::filesystem::path& path);
  bool combineFile(const std::filesystem::path& path);
  bool combineDocument(const World::Document& incoming,
                       const std::string& fallback_name);
  void applyCombined();
  bool save();
  bool exportTo(const std::filesystem::path& path);
  void request(Action action, std::filesystem::path path = {});
  void perform(Action action, const std::filesystem::path& path);
  bool startCareer();
  void notify(std::string message, bool error = false);
  bool pristine() const { return !dirty; }

  // History.
  Snapshot capture() const;
  void snapshot(const std::string& key = {});
  void undoStep();
  void redoStep();
  void restore(const Snapshot& snapshot);
  void changed();

  // Editing.
  void setPage(Page next);
  void select(World::Kind kind, std::int64_t key);
  void showIssue(const World::Issue& issue);
  void add();
  void duplicateSelected();
  void removeSelected();
  bool applyForm();
  void buildForm();
  void rebuildRows();
  const std::vector<World::Issue>& problems(bool force = false);

  // Combining.
  void resolveSelected(World::Choice choice);
  void resolveChosenFields();
  void resolveAllFrom(const std::string& source);
  void mergeSelected(bool keep_base);
  void keepSeparate();
  void settleConflicts();
  std::vector<std::string> sources() const;
  std::size_t openConflicts() const
  {
    return conflicts.size() + duplicates.size();
  }

  // Drawing.
  void update(float delta);
  void handleDrop(const std::filesystem::path& path);
  void render();
  void renderHeader();
  void renderNavigation(float width);
  void renderNavigationCombo();
  void renderList(float width, float height);
  void renderForm(float width, float height);
  void renderDetails(float width, float height);
  void renderProblems(float width, float height);
  void renderConflicts(float width, float height);
  void renderPopups();
  /** Opens the current popup as a titled modal; closes it when it is gone. */
  bool modal(const std::string& title);
  static void endModal(bool close);
  void renderChooser();
  void renderSummary();
  void renderConflictDetail();
  void renderDuplicateDetail();
};

/** Small helpers shared by the World editor's drawing and editing code. */
namespace WorldEditorUi
{
std::int64_t integer(const nlohmann::json& entity, const char* key);
std::string format(const char* key, std::vector<std::string> args = {});
const World::FieldSpec* specOf(World::Kind kind, std::string_view path);
std::string fieldLabel(std::optional<World::Kind> kind,
                       const std::string& path);
/** Localised plural name of an entity kind ("Clubs"). */
std::string kindName(World::Kind kind);
std::string stem(const std::filesystem::path& path);
float scaled(float value);
void muted(const std::string& text);
/** Button that starts a new line when it does not fit; false while disabled. */
bool flowButton(const char* label, bool& first, bool enabled = true,
                bool primary = false,
                UI::ButtonSize size = UI::ButtonSize::REGULAR);
/** Secondary "Cancel" button that Escape also presses. */
bool cancelPressed(ImVec2 size = ImVec2(0, 0));
}  // namespace WorldEditorUi
