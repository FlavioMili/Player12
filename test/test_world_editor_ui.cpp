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

TEST(WorldEditorUI, SwitchingPagesAfterAddingALeagueNeverShowsStaleRows)
{
  GameController controller;
  auto view = openEditor(controller, World::templateDocument());
  State& state = WorldEditorTestAccess::state(*view);
  state.setPage(State::Page::League);
  frames(*view, 1);
  // The reported crash: add a league, then open an empty page.
  state.setPage(State::Page::City);
  state.doc.setSection(Kind::City, nlohmann::json::array());
  state.changed();
  state.setPage(State::Page::League);
  frames(*view, 1);
  state.setPage(State::Page::City);
  EXPECT_TRUE(state.rows.empty());
  frames(*view, 2);
  for (int page = 0; page <= static_cast<int>(State::Page::Player); ++page)
  {
    state.setPage(static_cast<State::Page>(page));
    if (const auto kind = state.kind())
    {
      for (const std::uint32_t row : state.rows)
      {
        EXPECT_LT(row, state.doc.count(*kind));
      }
    }
    frames(*view, 1);
  }
}

TEST(WorldEditorUI, LanguagesConfederationsAndRegionsAreEditedLikeAnyPage)
{
  GameController controller;
  auto view = openEditor(controller, World::templateDocument());
  State& state = WorldEditorTestAccess::state(*view);
  // Pages exist even while empty, then add, edit and delete work on each.
  for (const auto page :
       {State::Page::Language, State::Page::Confederation, State::Page::Region})
  {
    state.setPage(page);
    frames(*view, 2);
    EXPECT_TRUE(state.rows.empty());
    state.add();
    frames(*view, 2);
    ASSERT_EQ(state.rows.size(), 1U);
    EXPECT_GE(state.selected, 1);
    frames(*view, 2);
  }
  // The language takes names as text and a country can speak it.
  state.setPage(State::Page::Language);
  state.select(Kind::Language, 1);
  frames(*view);
  const auto field = [&](std::string_view name)
  {
    const auto found =
        std::ranges::find(state.values, name, &EditorForm::Value::name);
    EXPECT_NE(found, state.values.end());
    return static_cast<std::size_t>(found - state.values.begin());
  };
  state.values[field("first_names")].text = "Aegon, Rhaenys";
  ASSERT_TRUE(state.applyForm());
  EXPECT_EQ(state.doc.find(Kind::Language, 1)->at("first_names"),
            (nlohmann::json{"Aegon", "Rhaenys"}));
  state.setPage(State::Page::Country);
  state.select(Kind::Country, 1);
  frames(*view);
  state.values[field("language")].text = "1";
  state.values[field("demonym")].text = "Westerosi";
  ASSERT_TRUE(state.applyForm());
  EXPECT_EQ(state.doc.find(Kind::Country, 1)->at("language"), 1);
  EXPECT_EQ(state.doc.find(Kind::Country, 1)->at("demonym"), "Westerosi");
  // Removing the language detaches the country; the form shows no Nationality.
  state.setPage(State::Page::Language);
  state.select(Kind::Language, 1);
  state.removeSelected();
  EXPECT_FALSE(state.doc.find(Kind::Country, 1)->contains("language"));
  state.setPage(State::Page::Country);
  state.select(Kind::Country, 1);
  frames(*view);
  for (const auto& descriptor : state.descriptors)
  {
    EXPECT_NE(descriptor.label, "PROFILE_NATIONALITY");
  }
  // Every page still draws after the edits.
  for (int page = 0; page <= static_cast<int>(State::Page::Player); ++page)
  {
    state.setPage(static_cast<State::Page>(page));
    frames(*view, 2);
  }
}

TEST(WorldEditorUI, ConfederationCupsAndRoutesAreEditedWithoutJson)
{
  GameController controller;
  auto view = openEditor(controller, World::templateDocument());
  State& state = WorldEditorTestAccess::state(*view);
  state.setPage(State::Page::Confederation);
  state.add();
  const std::int64_t home = state.selected;
  state.add();
  const std::int64_t other = state.selected;
  (*state.doc.find(Kind::Confederation, other))["name"] = "Other";
  // Undo replaces the document: always look the confederation up again.
  const auto conf = [&]() -> nlohmann::json&
  { return *state.doc.find(Kind::Confederation, home); };
  nlohmann::json& union_ = conf();
  union_["name"] = "Union";
  union_["name_key"] = "UNION_KEY";
  union_["cups"] = {{{"name", "Old"}, {"note", 7}}};
  state.select(Kind::Confederation, home);
  frames(*view);

  // The form has a toggle for the finals; cups, routes and the key stay out.
  const auto descriptor = [&](std::string_view name)
  { return std::ranges::find(state.descriptors, name, &EditorField::column); };
  ASSERT_NE(descriptor("national_finals"), state.descriptors.end());
  EXPECT_EQ(descriptor("national_finals")->control,
            EditorField::Control::Toggle);
  EXPECT_FALSE(descriptor("national_finals")->hint.empty());
  for (const char* hidden : {"cups", "routes", "name_key"})
  {
    EXPECT_EQ(descriptor(hidden), state.descriptors.end());
  }
  setField(state, "national_finals", "0");
  ASSERT_TRUE(state.applyForm());
  EXPECT_EQ(union_.at("national_finals"), false);
  EXPECT_EQ(union_.at("name_key"), "UNION_KEY");

  // A new cup: fields show what the game would use, edits are written alone.
  state.addCup();
  EXPECT_EQ(union_.at("cups").size(), 2U);
  EXPECT_EQ(state.cup, 1);
  frames(*view);
  const auto cupField = [&](std::string_view name)
  {
    if (state.cup_stale)
    {
      state.buildCup();  // Drawn with the Cups tab, which is not open here.
    }
    const auto found =
        std::ranges::find(state.cup_values, name, &EditorForm::Value::name);
    EXPECT_NE(found, state.cup_values.end());
    return found;
  };
  EXPECT_EQ(cupField("clubs")->text, "36");
  EXPECT_EQ(cupField("places")->text, "4, 4, 4, 4, 4, 3, 2, 2");
  EXPECT_FALSE(state.applyCup());
  const auto setCup = [&](std::string_view name, std::string text)
  {
    cupField(name)->text = std::move(text);
    EXPECT_TRUE(state.applyCup());
  };
  setCup("clubs", "16");
  setCup("places", "3, 2, 1");
  setCup("weekday_offsets.1", "4");
  setCup("prizes.win", "1000");
  setCup("participation_bonus", "2.5");
  setCup("name", "Plate");
  const nlohmann::json& cup = union_.at("cups").at(1);
  EXPECT_EQ(cup.at("clubs"), 16);
  EXPECT_EQ(cup.at("places"), (nlohmann::json{3, 2, 1}));
  EXPECT_EQ(cup.at("weekday_offsets"), (nlohmann::json{0, 4}));
  EXPECT_EQ(cup.at("prizes"), (nlohmann::json{{"win", 1000}}));
  EXPECT_EQ(cup.at("participation_bonus"), 2.5);
  EXPECT_EQ(cup.at("name"), "Plate");
  EXPECT_FALSE(cup.contains("matches"));
  setCup("tier", "2");
  frames(*view);
  EXPECT_EQ(cupField("matches")->text, "6");  // The shield's default.
  // Bad text is ignored; the other cup keeps its unknown keys.
  cupField("places")->text = "3, x";
  EXPECT_FALSE(state.applyCup());
  EXPECT_EQ(union_.at("cups").at(0).at("note"), 7);
  state.moveCup(-1);
  EXPECT_EQ(state.cup, 0);
  EXPECT_EQ(union_.at("cups").at(0).at("name"), "Plate");
  EXPECT_EQ(union_.at("cups").at(1).at("note"), 7);
  EXPECT_TRUE(state.dirty);

  // Routes: a destination chosen among the other confederations.
  state.addRoute();
  ASSERT_EQ(union_.at("routes").size(), 1U);
  EXPECT_EQ(union_.at("routes").at(0).at("to"), "Other");
  state.setRoute(0, "km", 5000);
  state.setRoute(0, "hours", 7);
  EXPECT_EQ(union_.at("routes").at(0),
            (nlohmann::json{{"to", "Other"}, {"km", 5000}, {"hours", 7}}));
  state.undoStep();
  EXPECT_EQ(conf().at("routes").at(0).at("hours"), 12);
  state.undoStep();
  state.undoStep();
  EXPECT_FALSE(conf().contains("routes"));
  state.redoStep();
  ASSERT_EQ(conf().at("routes").size(), 1U);
  state.removeRoute(0);
  EXPECT_TRUE(conf().at("routes").empty());

  // Bad values are reported with their field path.
  conf().at("cups")[0]["clubs"] = 2;
  const auto& issues = state.problems(true);
  EXPECT_TRUE(std::ranges::any_of(issues, [](const World::Issue& issue)
                                  { return issue.field == "cups[0].clubs"; }));
  state.removeCup();
  frames(*view);
  EXPECT_EQ(conf().at("cups").size(), 1U);
}

TEST(WorldEditorUI, PendingCombineDecisionsBlockSaveAndStartUntilChosen)
{
  GameController controller;
  auto view = openEditor(controller, World::templateDocument());
  State& state = WorldEditorTestAccess::state(*view);
  ASSERT_TRUE(state.combineDocument(otherWorld(), "other"));
  state.applyCombined();
  frames(*view);
  ASSERT_GT(state.openConflicts(), 0U);
  state.setPage(State::Page::Details);
  EXPECT_FALSE(state.save());
  EXPECT_EQ(state.page, State::Page::Conflicts);
  EXPECT_TRUE(state.status_error);
  EXPECT_TRUE(state.file.empty());
  EXPECT_FALSE(state.exportTo(std::filesystem::temp_directory_path() /
                              "fm_pending_export.fmdb"));
  EXPECT_FALSE(state.startCareer());
  EXPECT_EQ(state.next_scene, nullptr);
  frames(*view);
  // The Problems page names the pending decisions.
  state.setPage(State::Page::Problems);
  frames(*view);

  state.keepAllAsIs();
  EXPECT_EQ(state.openConflicts(), 0U);
  ASSERT_TRUE(state.save());
  World::Library::remove(state.file);
}

TEST(WorldEditorUI, ReviewListsTheCombineAndKeepsBothOnRequest)
{
  GameController controller;
  World::Document base = World::templateDocument();
  World::Document incoming = base;
  incoming.add(Kind::Club,
               {{"id", 50}, {"league_id", 1}, {"name", "Newtown Athletic"}});
  auto view = openEditor(controller, base);
  State& state = WorldEditorTestAccess::state(*view);
  ASSERT_TRUE(state.combineDocument(incoming, "other"));
  frames(*view);
  const std::size_t added = state.combined->additions.size();
  const std::size_t merged = state.combined->auto_merges.size();
  EXPECT_EQ(added, 1U);
  EXPECT_GT(merged, 0U);
  state.applyCombined();
  frames(*view);
  EXPECT_EQ(state.page, State::Page::Review);
  EXPECT_EQ(state.additions.size(), added);
  EXPECT_EQ(state.auto_merges.size(), merged);
  state.review_tab = 1;
  frames(*view);
  state.review_tab = 0;
  frames(*view);

  const auto league = std::ranges::find(state.auto_merges, Kind::League,
                                        &World::AutoMerge::kind);
  ASSERT_NE(league, state.auto_merges.end());
  const std::size_t leagues = state.doc.count(Kind::League);
  state.separateMerged(
      static_cast<std::size_t>(league - state.auto_merges.begin()));
  EXPECT_EQ(state.doc.count(Kind::League), leagues + 1);
  EXPECT_EQ(state.auto_merges.size(), merged - 1);
  const std::int64_t club = state.additions.front().new_id;
  EXPECT_NE(state.doc.find(Kind::Club, club)->at("league_id"), 1);
  // Undo brings the merge back for review.
  state.undoStep();
  EXPECT_EQ(state.doc.count(Kind::League), leagues);
  EXPECT_EQ(state.auto_merges.size(), merged);
}
