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
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <nlohmann/json.hpp>

#include "database/gamedata.h"
#include "global/language_manager.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/runtime_editor_dialog.h"
#include "model/settings_manager.h"

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
  static RuntimeEditorDialog* editor(GUIView& view)
  {
    return view.editor_dialog.get();
  }
  static std::string table(const RuntimeEditorDialog& editor)
  {
    return editor.selected_table;
  }
  static bool related(const RuntimeEditorDialog& editor)
  {
    return editor.browse_all;
  }
  static std::size_t rows(const RuntimeEditorDialog& editor)
  {
    return editor.page.rows.size();
  }
  static bool form(const RuntimeEditorDialog& editor)
  {
    return !editor.fields.empty() && !editor.show_records;
  }
  static std::string error(const RuntimeEditorDialog& editor)
  {
    return editor.error;
  }
  static std::string name(const RuntimeEditorDialog& editor)
  {
    return editor.focus_label;
  }
  static bool exposesTable(const RuntimeEditorDialog& editor,
                           const std::string& name)
  {
    return std::ranges::any_of(editor.tables, [&name](const auto& table)
                               { return table.name == name; });
  }
  static void field(RuntimeEditorDialog& editor, const std::string& name,
                    std::string value)
  {
    const auto field =
        std::ranges::find_if(editor.fields, [&name](const auto& field)
                             { return field.name == name; });
    ASSERT_NE(field, editor.fields.end());
    field->text = std::move(value);
  }
  static std::string fieldValue(const RuntimeEditorDialog& editor,
                                const std::string& name)
  {
    const auto field =
        std::ranges::find_if(editor.fields, [&name](const auto& field)
                             { return field.name == name; });
    return field == editor.fields.end() ? "" : field->text;
  }
  static void apply(RuntimeEditorDialog& editor)
  {
    editor.stage();
    if (editor.error.empty())
      editor.request = RuntimeEditorDialog::Request::Apply;
  }
  static void close(RuntimeEditorDialog& editor)
  {
    editor.close_requested = true;
  }
  static void choose(RuntimeEditorDialog& editor, const std::string& table)
  {
    editor.chooseTable(table);
  }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;
namespace
{
void frames(GUIView& view, int count = 3)
{
  for (int i = 0; i < count; ++i) Bridge::frame(view);
}
void capture(GUIView& view, const char* name)
{
  const auto path = RuntimePaths::capturePath(name);
  ASSERT_TRUE(view.captureScreenshot(path.string()));
}
struct Cleanup
{
  int slot;
  ~Cleanup() { RuntimePaths::removeSave(slot); }
};
}  // namespace

TEST(RuntimeEditorUI, PlayerOpensDirectlyAndEditsAtAFrameBoundary)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const Cleanup cleanup{920'000 + static_cast<int>(getpid())};
  GameController controller;
  controller.newGame(cleanup.slot, 20250702);
  const auto team =
      controller.getGameData()->getTeamsVector().front().get().getId();
  controller.selectManagedTeam(team);
  const auto player = controller.getPlayersForTeam(team).front().get().getId();
  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view);
  Navigation::openPlayer(&view, player);
  frames(view);
  view.openEditor(NavEntry::ofPlayer(player));
  frames(view);
  auto* editor = Bridge::editor(view);
  ASSERT_NE(editor, nullptr);
  EXPECT_EQ(Bridge::table(*editor), "Players");
  EXPECT_EQ(Bridge::rows(*editor), 1U);
  EXPECT_TRUE(Bridge::form(*editor));
  EXPECT_FALSE(Bridge::related(*editor));
  EXPECT_FALSE(Bridge::exposesTable(*editor, "AiManagers"));
  EXPECT_FALSE(Bridge::exposesTable(*editor, "save_meta"));
  EXPECT_TRUE(Bridge::error(*editor).empty());
  EXPECT_EQ(Bridge::name(*editor),
            controller.getGameData()->getPlayer(player)->get().getName());
  capture(view, "editor-player-attributes.bmp");
  auto stats = nlohmann::json::parse(Bridge::fieldValue(*editor, "stats"));
  stats["new_custom_attribute"] = 73;
  Bridge::field(*editor, "stats", stats.dump());
  frames(view);
  capture(view, "editor-player-custom-attribute.bmp");
  Bridge::apply(*editor);
  const auto revision = controller.getEditorRevision();
  frames(view);
  EXPECT_EQ(controller.getEditorRevision(), revision + 1);
  EXPECT_EQ(controller.getGameData()->getPlayer(player)->get().getStats().at(
                "new_custom_attribute"),
            73);
  EXPECT_FALSE(editor->isOpen());
  ASSERT_NE(view.getTopScene(), nullptr);
  EXPECT_EQ(view.getTopScene()->historyEntry(), NavEntry::ofPlayer(player));
  EXPECT_EQ(ImGui::GetCurrentContext()->OpenPopupStack.Size, 0);
  capture(view, "editor-player-applied.bmp");
  // Invalid values retain the form and old runtime; they never escape render.
  view.openEditor(NavEntry::ofPlayer(player));
  frames(view);
  Bridge::field(*editor, "age", "999");
  Bridge::apply(*editor);
  frames(view);
  EXPECT_FALSE(Bridge::error(*editor).empty());
  EXPECT_EQ(controller.getEditorRevision(), revision + 1);
  EXPECT_TRUE(editor->isOpen());
  capture(view, "editor-validation-error.bmp");
  Bridge::field(*editor, "age", "24");
  Bridge::apply(*editor);
  frames(view);
  EXPECT_TRUE(Bridge::error(*editor).empty());
  EXPECT_EQ(controller.getGameData()->getPlayer(player)->get().getAge(), 24);
  EXPECT_FALSE(editor->isOpen());
  view.openEditor(NavEntry::ofPlayer(player));
  frames(view);
  Bridge::close(*editor);
  frames(view);
  EXPECT_FALSE(editor->isOpen());
}

TEST(RuntimeEditorUI, ClubAndSquadAreScopedAndLayoutsFitBothLanguages)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const Cleanup cleanup{930'000 + static_cast<int>(getpid())};
  GameController controller;
  controller.newGame(cleanup.slot, 20250702);
  const auto club =
      controller.getGameData()->getTeamsVector().front().get().getId();
  controller.selectManagedTeam(club);
  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view);
  for (const auto language : {Language::EN, Language::IT})
  {
    LanguageManager::instance().loadLanguage(language);
    for (const auto size :
         {ImVec2(1280, 720), ImVec2(640, 720), ImVec2(1920, 1080)})
    {
      SDL_SetWindowSize(view.getWindow(), static_cast<int>(size.x),
                        static_cast<int>(size.y));
      frames(view);
      view.openEditor(NavEntry::ofSection(NavSection::HOME));
      frames(view);
      auto* editor = Bridge::editor(view);
      ASSERT_NE(editor, nullptr);
      EXPECT_TRUE(Bridge::form(*editor));
      EXPECT_EQ(Bridge::rows(*editor), 1U);
      EXPECT_EQ(Bridge::table(*editor), "Teams");
      EXPECT_FALSE(Bridge::exposesTable(*editor, "WorldState"));
      const auto* popup = ImGui::FindWindowByName("###runtime_editor");
      ASSERT_NE(popup, nullptr);
      EXPECT_GE(popup->Pos.x, 0);
      EXPECT_GE(popup->Pos.y, 0);
      EXPECT_LE(popup->Pos.x + popup->Size.x, ImGui::GetIO().DisplaySize.x + 1);
      EXPECT_LE(popup->Pos.y + popup->Size.y, ImGui::GetIO().DisplaySize.y + 1);
      capture(view, language == Language::IT ? "editor-club-italian.bmp"
                                             : "editor-club-english.bmp");
      Bridge::close(*editor);
      frames(view);
    }
  }
  LanguageManager::instance().loadLanguage(Language::EN);
  view.openEditor(NavEntry::ofClub(club));
  frames(view);
  auto* editor = Bridge::editor(view);
  EXPECT_EQ(Bridge::table(*editor), "Players");
  EXPECT_EQ(Bridge::rows(*editor),
            controller.getPlayersForTeam(club).size() +
                controller.getGameData()->getAcademyForTeam(club).size());
  Bridge::choose(*editor, "Teams");
  frames(view);
  EXPECT_EQ(Bridge::rows(*editor), 1U);
  EXPECT_EQ(Bridge::name(*editor),
            controller.getGameData()->getTeam(club)->get().getName());
}

TEST(RuntimeEditorUI, NewFeatureProvidesContextWithoutChangingTheEditor)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const Cleanup cleanup{940'000 + static_cast<int>(getpid())};
  GameController controller;
  controller.newGame(cleanup.slot, 20250702);
  controller.selectManagedTeam(
      controller.getGameData()->getTeamsVector().front().get().getId());
  ASSERT_EQ(
      sqlite3_exec(
          controller.getDbConn()->getRaw(),
          "CREATE TABLE CustomScores(id INTEGER PRIMARY KEY, nutrition INTEGER "
          "CHECK(nutrition BETWEEN 0 AND 100), custom_label TEXT); INSERT INTO "
          "CustomScores VALUES(41,50,'other'),(42,60,'current');",
          nullptr, nullptr, nullptr),
      SQLITE_OK);
  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view);
  EditorContext context;
  context.table = "CustomScores";
  context.filter["id"] = std::int64_t{42};
  view.openEditor(context);
  frames(view);
  auto* editor = Bridge::editor(view);
  ASSERT_NE(editor, nullptr);
  EXPECT_EQ(Bridge::table(*editor), "CustomScores");
  EXPECT_EQ(Bridge::rows(*editor), 1U);
  EXPECT_TRUE(Bridge::form(*editor));
  EXPECT_FALSE(Bridge::exposesTable(*editor, "Teams"));
  Bridge::field(*editor, "nutrition", "85");
  Bridge::apply(*editor);
  frames(view);
  EXPECT_FALSE(editor->isOpen());
  const auto page =
      RuntimeEditor::read(controller.getDbConn()->getRaw(), "CustomScores");
  ASSERT_EQ(page.rows.size(), 2U);
  EXPECT_EQ(std::get<std::int64_t>(page.rows[0].values[1]), 50);
  EXPECT_EQ(std::get<std::int64_t>(page.rows[1].values[1]), 85);
}
