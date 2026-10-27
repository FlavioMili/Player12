// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <memory>

#include "controller/game_controller.h"
#include "database/world/world_io.h"
#include "global/language_manager.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/world_editor_scene.h"
#include "gui/scenes/world_editor_state.h"

class GameFlowTest_GUIFlowLifecycle_Test
{
 public:
  static bool initialize(GUIView& view) { return view.initialize(); }
  static void frame(GUIView& view)
  {
    view.applyPendingSceneChanges();
    view.handleEvents();
    view.update(.016f);
    view.render();
    EXPECT_EQ(ImGui::GetCurrentContext()->ErrorCountCurrentFrame, 0);
  }
};

struct WorldEditorTestAccess
{
  static WorldEditorScene& scene(GUIView& view)
  {
    auto* scene = dynamic_cast<WorldEditorScene*>(view.getTopScene());
    EXPECT_NE(scene, nullptr);
    return *scene;
  }
  static WorldEditorScene::State& state(GUIView& view)
  {
    auto* scene = dynamic_cast<WorldEditorScene*>(view.getTopScene());
    EXPECT_NE(scene, nullptr);
    return *scene->state;
  }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;
using State = WorldEditorScene::State;
using World::Kind;

namespace
{
void frames(GUIView& view, int count = 3)
{
  for (int i = 0; i < count; ++i)
  {
    Bridge::frame(view);
  }
}

std::unique_ptr<GUIView> openEditor(GameController& controller,
                                    World::Document document)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  auto view = std::make_unique<GUIView>(controller);
  EXPECT_TRUE(Bridge::initialize(*view));
  WorldEditorScene::Options options;
  options.world = std::make_shared<World::Document>(std::move(document));
  view->changeScene(std::make_unique<WorldEditorScene>(view.get(), options));
  frames(*view);
  return view;
}

void setField(State& state, const std::string& name, std::string text)
{
  const auto field =
      std::ranges::find(state.values, name, &EditorForm::Value::name);
  ASSERT_NE(field, state.values.end());
  field->text = std::move(text);
}

World::Document otherWorld()
{
  World::Document other = World::Document::empty("Other World");
  other.add(Kind::Country,
            {{"id", 7}, {"name", "Freeland"}, {"nationality", "English"}});
  other.add(Kind::League,
            {{"id", 3}, {"name", "Freeland League"}, {"country_id", 7}});
  other.add(Kind::Club, {{"id", 9},
                         {"league_id", 3},
                         {"name", "Northport"},
                         {"short_name", "NOR"},
                         {"nickname", "The Ports"}});
  other.add(Kind::Club, {{"id", 10},
                         {"league_id", 3},
                         {"name", "Midtown"},
                         {"short_name", "MID"}});
  return other;
}
}  // namespace

TEST(WorldEditorUI, EditAddDeleteUndoAndPlayabilityProblems)
{
  GameController controller;
  auto view = openEditor(controller, World::templateDocument());
  State& state = WorldEditorTestAccess::state(*view);
  EXPECT_EQ(state.page, State::Page::Details);
  EXPECT_FALSE(state.dirty);

  state.setPage(State::Page::Club);
  frames(*view);
  EXPECT_EQ(state.rows.size(), 4U);
  state.select(Kind::Club, 101);
  frames(*view);
  EXPECT_FALSE(state.values.empty());
  setField(state, "name", "Northport Rovers");
  ASSERT_TRUE(state.applyForm());
  EXPECT_EQ(state.doc.find(Kind::Club, 101)->at("name"), "Northport Rovers");
  EXPECT_TRUE(state.dirty);
  frames(*view);

  // A club moves to another competition by changing its reference.
  setField(state, "league_id", "");
  ASSERT_TRUE(state.applyForm());
  EXPECT_FALSE(state.doc.find(Kind::Club, 101)->contains("league_id"));
  frames(*view);
  const auto& issues = state.problems(true);
  EXPECT_TRUE(std::ranges::any_of(
      issues, [](const World::Issue& issue)
      { return issue.code == World::IssueCode::Unassigned; }));
  frames(*view);

  state.undoStep();
  EXPECT_EQ(state.doc.find(Kind::Club, 101)->at("league_id"), 1);
  state.undoStep();
  EXPECT_EQ(state.doc.find(Kind::Club, 101)->at("name"), "Northport");
  state.redoStep();
  EXPECT_EQ(state.doc.find(Kind::Club, 101)->at("name"), "Northport Rovers");
  frames(*view);

  state.add();
  EXPECT_EQ(state.doc.count(Kind::Club), 5U);
  const std::int64_t added = state.selected;
  ASSERT_NE(state.doc.find(Kind::Club, added), nullptr);
  frames(*view);
  state.removeSelected();
  EXPECT_EQ(state.doc.count(Kind::Club), 4U);
  EXPECT_EQ(state.selected, -1);
  frames(*view);

  // Three clubs are not enough for a competition: the career is blocked.
  state.setPage(State::Page::Club);
  state.select(Kind::Club, 104);
  state.removeSelected();
  const auto& problems = state.problems(true);
  EXPECT_TRUE(std::ranges::any_of(
      problems,
      [](const World::Issue& issue)
      {
        return issue.code == World::IssueCode::TooFewClubs &&
               issue.severity == World::Severity::Blocking;
      }));
  state.setPage(State::Page::Problems);
  frames(*view);
  EXPECT_FALSE(state.startCareer());
  EXPECT_EQ(state.page, State::Page::Problems);
  EXPECT_EQ(state.next_scene, nullptr);
}

TEST(WorldEditorUI, CombineResolvesConflictsAndSavesToTheLibrary)
{
  GameController controller;
  auto view = openEditor(controller, World::templateDocument());
  State& state = WorldEditorTestAccess::state(*view);
  ASSERT_TRUE(state.combineDocument(otherWorld(), "other"));
  frames(*view);
  ASSERT_TRUE(state.combined.has_value());
  EXPECT_EQ(state.popup, State::Popup::Summary);
  state.applyCombined();
  frames(*view);
  EXPECT_EQ(state.page, State::Page::Conflicts);
  ASSERT_FALSE(state.conflicts.empty());
  const auto club =
      std::ranges::find(state.conflicts, Kind::Club, &World::Conflict::kind);
  ASSERT_NE(club, state.conflicts.end());
  EXPECT_NE(std::ranges::find(club->fields, "nickname"), club->fields.end());
  EXPECT_TRUE(state.dirty);
  EXPECT_EQ(state.doc.count(Kind::Club), 5U);
  EXPECT_FALSE(state.doc.find(Kind::Club, 101)->contains("nickname"));

  const std::size_t total = state.conflicts.size();
  state.conflict_selected = static_cast<int>(club - state.conflicts.begin());
  frames(*view);
  state.resolveSelected(World::Choice::KeepIncoming);
  EXPECT_EQ(state.conflicts.size(), total - 1);
  EXPECT_EQ(state.doc.find(Kind::Club, 101)->at("nickname"), "The Ports");
  frames(*view);

  // Undo brings the conflict back; the bulk action resolves everything.
  state.undoStep();
  EXPECT_EQ(state.conflicts.size(), total);
  EXPECT_FALSE(state.doc.find(Kind::Club, 101)->contains("nickname"));
  state.resolveAllFrom("Other World");
  EXPECT_NE(state.page, State::Page::Conflicts);
  EXPECT_TRUE(state.conflicts.empty());
  EXPECT_EQ(state.doc.find(Kind::Club, 101)->at("nickname"), "The Ports");

  ASSERT_TRUE(state.save());
  EXPECT_FALSE(state.dirty);
  ASSERT_FALSE(state.file.empty());
  const auto loaded = World::readFile(state.file);
  ASSERT_TRUE(loaded.ok());
  EXPECT_EQ(loaded.document->count(Kind::Club), 5U);
  State reopened(view.get(), {});
  EXPECT_TRUE(reopened.openFile(state.file));
  EXPECT_EQ(reopened.doc.count(Kind::Club), 5U);
  World::Library::remove(state.file);
}

TEST(WorldEditorUI, DuplicatesMergeAndUnsavedWorldsAskBeforeLeaving)
{
  GameController controller;
  auto view = openEditor(controller, World::templateDocument());
  State& state = WorldEditorTestAccess::state(*view);
  auto other = otherWorld();
  other.add(Kind::Club, {{"id", 11}, {"league_id", 3}, {"name", "North Port"}});
  state.combineDocument(other, "other");
  state.applyCombined();
  frames(*view);
  ASSERT_FALSE(state.duplicates.empty());
  state.conflict_selected = static_cast<int>(state.conflicts.size());
  frames(*view);
  const auto clubs = state.doc.count(Kind::Club);
  state.mergeSelected(true);
  EXPECT_EQ(state.doc.count(Kind::Club), clubs - 1);
  frames(*view);

  EXPECT_TRUE(state.dirty);
  state.request(State::Action::Close);
  EXPECT_EQ(state.open_popup, State::Popup::Discard);
  EXPECT_EQ(state.next_scene, nullptr);
  frames(*view);
  EXPECT_EQ(state.popup, State::Popup::Discard);
  state.pending = State::Action::None;
}

TEST(WorldEditorUI, DroppedFilesOpenOrAsk)
{
  GameController controller;
  auto view = openEditor(controller, World::templateDocument());
  State& state = WorldEditorTestAccess::state(*view);
  const auto path = RuntimePaths::root() / "dropped-world.fmdb";
  ASSERT_TRUE(World::writeFile(World::Document::empty("Dropped"), path));
  const std::string utf8 = World::pathToUtf8(path);
  SDL_Event drop{};
  drop.type = SDL_EVENT_DROP_FILE;
  drop.drop.data = utf8.c_str();
  WorldEditorTestAccess::scene(*view).handleEvent(drop);
  EXPECT_EQ(state.doc.manifest().name, "Dropped");
  state.setPage(State::Page::Country);
  state.add();
  EXPECT_TRUE(state.dirty);
  WorldEditorTestAccess::scene(*view).handleEvent(drop);
  EXPECT_EQ(state.open_popup, State::Popup::Dropped);
  frames(*view);
  std::error_code error;
  std::filesystem::remove(path, error);
}

TEST(WorldEditorUI, EveryPageFitsSmallAndLargeWindowsInBothLanguages)
{
  GameController controller;
  auto view = openEditor(controller, World::templateDocument());
  State& state = WorldEditorTestAccess::state(*view);
  state.combineDocument(otherWorld(), "other");
  state.applyCombined();
  for (const auto language : {Language::EN, Language::IT})
  {
    LanguageManager::instance().loadLanguage(language);
    for (const ImVec2 size : {ImVec2(1024, 640), ImVec2(1920, 1080)})
    {
      SDL_SetWindowSize(view->getWindow(), static_cast<int>(size.x),
                        static_cast<int>(size.y));
      for (int page = 0; page <= static_cast<int>(State::Page::Conflicts);
           ++page)
      {
        state.setPage(static_cast<State::Page>(page));
        if (state.kind() && state.rows.empty())
        {
          state.rebuildRows();
        }
        if (state.kind() && !state.rows.empty())
        {
          state.select(*state.kind(), State::kindOf(state.page) == Kind::Travel
                                          ? 0
                                          : World::entityId(state.doc.section(
                                                *state.kind())[state.rows[0]]));
        }
        frames(*view, 2);
      }
    }
  }
  LanguageManager::instance().loadLanguage(Language::EN);
}

TEST(WorldEditorUI, DialogsFitInBothLanguages)
{
  GameController controller;
  auto view = openEditor(controller, World::templateDocument());
  State& state = WorldEditorTestAccess::state(*view);
  for (const auto language : {Language::IT, Language::EN})
  {
    LanguageManager::instance().loadLanguage(language);
    SDL_SetWindowSize(view->getWindow(), 1024, 640);
    state.setPage(State::Page::League);
    state.select(Kind::League, 1);
    state.add();
    state.select(Kind::League, 1);
    for (const auto popup : {State::Popup::New, State::Popup::Chooser,
                             State::Popup::Delete, State::Popup::Discard})
    {
      if (popup == State::Popup::Delete)
      {
        state.popup_text = "x";
      }
      state.open_popup = popup;
      frames(*view, 2);
      EXPECT_EQ(state.popup, popup);
      ImGui::ClosePopupToLevel(0, false);
      frames(*view, 2);
    }
  }
  LanguageManager::instance().loadLanguage(Language::EN);
}

TEST(WorldEditorUI, FifteenThousandPlayersStayResponsive)
{
  GameController controller;
  World::Document big = World::templateDocument();
  for (int i = 2; i <= 15000; ++i)
  {
    big.add(Kind::Player, {{"id", i},
                           {"team_id", 101 + i % 4},
                           {"first_name", "Player"},
                           {"last_name", "Number" + std::to_string(i)},
                           {"age", 20 + i % 15},
                           {"role", "CM"},
                           {"stats", {{"Pace", 50}, {"Passing", 60}}}});
  }
  auto view = openEditor(controller, std::move(big));
  State& state = WorldEditorTestAccess::state(*view);
  state.setPage(State::Page::Player);
  const auto start = std::chrono::steady_clock::now();
  frames(*view, 20);
  EXPECT_EQ(state.rows.size(), 15000U);
  state.search = "number 1499";
  state.rows_dirty = true;
  frames(*view);
  EXPECT_LT(state.rows.size(), 20U);
  state.search.clear();
  state.parent_filter = 101;
  state.rows_dirty = true;
  frames(*view);
  EXPECT_GT(state.rows.size(), 3000U);
  state.parent_filter = 0;
  state.rows_dirty = true;
  frames(*view);
  EXPECT_EQ(state.rows.size(), 0U);
  state.setPage(State::Page::Problems);
  frames(*view, 5);
  const auto elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start);
  EXPECT_LT(elapsed.count(), 30.0);
}

TEST(WorldEditorUI, OptionalNumbersStartUnsetAndCanBeSetAndCleared)
{
  GameController controller;
  auto view = openEditor(controller, World::templateDocument());
  State& state = WorldEditorTestAccess::state(*view);
  state.setPage(State::Page::Club);
  state.select(Kind::Club, 101);
  frames(*view);
  const auto founded =
      std::ranges::find(state.values, "founded", &EditorForm::Value::name);
  ASSERT_NE(founded, state.values.end());
  EXPECT_TRUE(founded->text.empty());
  EXPECT_TRUE(std::ranges::none_of(
      state.values, [](const auto& value) { return value.name == "balance"; }));
  const auto& descriptor =
      *std::ranges::find(state.descriptors, "founded", &EditorField::column);
  EXPECT_TRUE(descriptor.optional);
  setField(state, "founded", descriptor.initial);
  ASSERT_TRUE(state.applyForm());
  EXPECT_EQ(state.doc.find(Kind::Club, 101)->at("founded"), 1900);
  setField(state, "founded", "");
  ASSERT_TRUE(state.applyForm());
  EXPECT_FALSE(state.doc.find(Kind::Club, 101)->contains("founded"));
  frames(*view);
}
