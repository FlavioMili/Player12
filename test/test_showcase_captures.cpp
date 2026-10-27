// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The screenshots of the README (docs/images), taken from a real career
// through the real GUIView at a HiDPI scale. Skipped unless
// FM_SHOWCASE_DIR names the folder the PNG files are written to:
//
//   FM_SHOWCASE_DIR=/tmp/showcase ctest --test-dir build -R showcase
//
// (through ctest, so the career lives in the scratch test data directory).
// FM_SHOWCASE_THEME picks the theme preset (index of Theme::Preset) and
// FM_SHOWCASE_LANGUAGE the language by file name (e.g. "Italian").
// The images are rendered at interface scale 2 on a 3200x1800 window;
// downscale them by two before committing them.
// FM_SHOWCASE_VIDEO_DIR additionally records two silent, 15-second sequences
// at 1600x900 / 30 fps from the live match renderer. Each subfolder contains
// frame-00000.png onwards, ready for ffmpeg; no synthetic frames are added.

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "controller/game_controller.h"
#include "database/world/world_edit.h"
#include "database/world/world_io.h"
#include "database/world/world_library.h"
#include "global/language_manager.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/main_menu_scene.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/match_scene.h"
#include "gui/scenes/roster_scene.h"
#include "gui/scenes/transfer_market_scene.h"
#include "gui/scenes/world_editor_scene.h"
#include "gui/scenes/world_editor_state.h"
#include "model/calendar.h"
#include "model/match_engine.h"
#include "model/settings_manager.h"

/** GUI classes grant their internals to this name (see test_game_flow). */
class GameFlowTest_GUIFlowLifecycle_Test
{
 public:
  static constexpr float DEFAULT_FRAME_SECONDS = 0.016F;
  static bool initialize(GUIView& view) { return view.initialize(); }
  static void frame(GUIView& view, float seconds = DEFAULT_FRAME_SECONDS)
  {
    view.applyPendingSceneChanges();
    view.handleEvents();
    view.update(seconds);
    view.render();
    EXPECT_EQ(ImGui::GetCurrentContext()->ErrorCountCurrentFrame, 0)
        << "ImGui reported a usage error";
  }
  static GUIScene* activeScene(const GUIView& view)
  {
    return view.getActiveScene();
  }
  static void select(RosterScene& scene, PlayerID id)
  {
    scene.selected_player_id = id;
  }
  static WorldChoiceDialog& worldChoice(MainMenuScene& scene)
  {
    return scene.world_choice;
  }
};

/** Live match internals (MatchScene grants them to this name). */
class GameFlowTest_ManagedMatchIntegration_Test
{
 public:
  static MatchEngine* engine(MatchScene& scene) { return scene.engine.get(); }
  static void setFocus(MatchScene& scene, bool focus)
  {
    scene.setPitchFocus(focus);
  }
  static MatchViewMode viewMode(const MatchScene& scene)
  {
    return scene.view_mode;
  }
  /** Normal play at 1x, so the shots show no highlight-skip notice. */
  static void watchLive(MatchScene& scene)
  {
    scene.setHighlightsOnly(false);
    scene.setPlaybackSpeed(1.0f);
  }
  static void resume(MatchScene& scene) { scene.is_paused = false; }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;
using MatchBridge = GameFlowTest_ManagedMatchIntegration_Test;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;
constexpr LeagueID OWN_LEAGUE = 1;
constexpr LeagueID FOREIGN_LEAGUE = 3;
constexpr int WIDTH = 3200;
constexpr int HEIGHT = 1800;
constexpr float SCALE = 2.0f;

int uniqueSlot(int offset)
{
  return 640'000 + (getpid() % 100'000) * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

void resize(GUIView& view, int width, int height)
{
  SDL_SetWindowSize(view.getWindow(), width, height);
  SDL_Event event{.window = {}};
  event.window.type = SDL_EVENT_WINDOW_RESIZED;
  event.window.windowID = SDL_GetWindowID(view.getWindow());
  event.window.data1 = width;
  event.window.data2 = height;
  SDL_PushEvent(&event);
}

void frames(GUIView& view, int count)
{
  for (int index = 0; index < count; ++index) Bridge::frame(view);
}

/** Writes the current frame as <folder>/<name>.png. */
void capture(GUIView& view, const std::filesystem::path& folder,
             const char* name, int width = WIDTH)
{
  const auto path = folder / (std::string(name) + ".png");
  std::filesystem::remove(path);
  SDL_Surface* surface = SDL_RenderReadPixels(view.getRenderer(), nullptr);
  ASSERT_NE(surface, nullptr) << SDL_GetError();
  EXPECT_EQ(surface->w, width) << name;
  EXPECT_TRUE(SDL_SavePNG(surface, path.string().c_str()))
      << path.string() << ": " << SDL_GetError();
  SDL_DestroySurface(surface);
  ASSERT_TRUE(std::filesystem::exists(path)) << name;
  EXPECT_GT(std::filesystem::file_size(path), width >= WIDTH ? 50'000u : 20'000u)
      << name;
}

/** Optional motion capture: every PNG is a rendered, fixed-time game frame.
 * Keep the normal screenshot run unchanged when no video folder is requested.
 */
void recordMotion(GUIView& view, MatchScene& match, const char* name,
                  bool focus)
{
  const char* setting = std::getenv("FM_SHOWCASE_VIDEO_DIR");
  if (setting == nullptr || *setting == '\0')
  {
    return;
  }
  constexpr int VIDEO_WIDTH = 1600;
  constexpr int VIDEO_HEIGHT = 900;
  constexpr int VIDEO_FPS = 30;
  constexpr int VIDEO_SECONDS = 15;
  constexpr int FRAME_DIGITS = 5;
  constexpr float VIDEO_STEP = 1.0F / static_cast<float>(VIDEO_FPS);
  const auto folder = std::filesystem::path(setting) / name;
  std::filesystem::create_directories(folder);
  SettingsManager::instance()->get().ui_scale = 1.0F;
  resize(view, VIDEO_WIDTH, VIDEO_HEIGHT);
  MatchBridge::setFocus(match, focus);
  MatchBridge::resume(match);
  MatchBridge::watchLive(match);

  // Find an upcoming real chance and include its build-up. Prediction runs a
  // copy of the match; the recorded live engine still executes every action.
  MatchEngine& engine = *MatchBridge::engine(match);
  if (const auto highlight = engine.predictNextHighlight(120.0F))
  {
    const double start =
        std::max(highlight->startSeconds, highlight->triggerSeconds - 9.0);
    const auto skip = static_cast<float>(start - engine.getSimulatedSeconds());
    if (skip > 0.0F)
    {
      engine.advance(skip);
    }
  }
  frames(view, 3);
  for (int index = 0; index < VIDEO_FPS * VIDEO_SECONDS; ++index)
  {
    Bridge::frame(view, VIDEO_STEP);
    const std::string filename =
        std::format("frame-{:0{}d}.png", index, FRAME_DIGITS);
    SDL_Surface* surface = SDL_RenderReadPixels(view.getRenderer(), nullptr);
    ASSERT_NE(surface, nullptr) << SDL_GetError();
    const auto path = folder / filename;
    ASSERT_TRUE(SDL_SavePNG(surface, path.string().c_str())) << SDL_GetError();
    SDL_DestroySurface(surface);
  }
  MatchBridge::setFocus(match, false);
  SettingsManager::instance()->get().ui_scale = SCALE;
  resize(view, WIDTH, HEIGHT);
  frames(view, 3);
}

/** Plays whole days until @p date (matches included). */
void advanceUntil(GameController& controller, GameDateValue date)
{
  for (int day = 0; day < 400 && controller.getCurrentDate() < date; ++day)
    controller.advanceDay();
  ASSERT_FALSE(controller.getCurrentDate() < date);
}

/** The club in the middle of the league by reputation: a squad, budget and
 * table position that look like most careers. */
TeamID middleOfTheLeague(const GameController& controller, LeagueID league)
{
  std::vector<TeamID> clubs =
      controller.getLeagueById(league)->get().getTeamIDs();
  std::ranges::sort(
      clubs, {}, [&](TeamID id)
      { return controller.getTeamById(id)->get().getReputation(); });
  return clubs.empty() ? 0 : clubs[clubs.size() / 2];
}

/** The best player abroad whom @p club can afford on fee and wage. */
PlayerID affordableTarget(const GameController& controller, TeamID club,
                          LeagueID league)
{
  const auto& squad = controller.getPlayersForTeam(club);
  std::uint64_t wages = 0;
  for (const auto& player : squad) wages += player.get().getWage();
  const std::uint64_t averageWage = squad.empty() ? 0 : wages / squad.size();
  const std::int64_t budget = controller.transferBudgetForTeam(club);
  PlayerID best = 0;
  float bestOverall = 0.0f;
  for (const TeamID id : controller.getLeagueById(league)->get().getTeamIDs())
    for (const auto& player : controller.getPlayersForTeam(id))
    {
      const auto row = controller.getScoutedRow(player.get().getId());
      if (!row || row->estimated_value <= 0 ||
          row->estimated_value > budget * 6 / 10 || row->wage > averageWage ||
          row->overall <= bestOverall)
        continue;
      best = row->player_id;
      bestOverall = row->overall;
    }
  return best;
}
}  // namespace

TEST(ShowcaseCaptures, ReadmeScreens)
{
  const char* folderSetting = std::getenv("FM_SHOWCASE_DIR");
  if (folderSetting == nullptr || *folderSetting == '\0')
    GTEST_SKIP() << "set FM_SHOWCASE_DIR to write the README screenshots";
  const std::filesystem::path folder(folderSetting);
  std::filesystem::create_directories(folder);

  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  Language language = Language::EN;
  if (const char* name = std::getenv("FM_SHOWCASE_LANGUAGE"))
    if (const auto found = stringToLanguage.find(name);
        found != stringToLanguage.end())
      language = found->second;
  ASSERT_TRUE(LanguageManager::instance().loadLanguage(language));
  Settings& settings = SettingsManager::instance()->get();
  settings.screen_tips = false;
  settings.audio_muted = true;
  settings.ui_scale = SCALE;
  if (const char* theme = std::getenv("FM_SHOWCASE_THEME"))
    settings.theme_preset = std::atoi(theme);

  const SlotCleanup slot{uniqueSlot(0)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  const TeamID club = middleOfTheLeague(controller, OWN_LEAGUE);
  ASSERT_NE(club, 0u);
  controller.selectManagedTeam(club);

  // A scout covers the league from the start, so the Scouting screen has
  // reports to show later on.
  const auto scouts = controller.getScouts();
  ASSERT_FALSE(scouts.empty());
  ASSERT_EQ(controller.startScoutAssignment(
                scouts.front().id, ScoutTargetKind::League, FOREIGN_LEAGUE, 60),
            ScoutAssignError::None);

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.refreshTheme();
  resize(view, WIDTH, HEIGHT);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 3);

  // Late August, window still open: an offer for a player the club can
  // afford.
  advanceUntil(controller, GameDateValue(2025, 8, 20));
  ASSERT_TRUE(controller.isTransferWindowOpen());
  {
    const PlayerID target = affordableTarget(controller, club, FOREIGN_LEAGUE);
    EXPECT_NE(target, 0u) << "nobody affordable abroad";
    if (target != 0)
    {
      view.navigateTo(std::make_unique<TransferMarketScene>(&view, target));
      frames(view, 4);
      EXPECT_TRUE(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
      capture(view, folder, "transfer_offer");
      ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
      frames(view, 1);
      ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
      frames(view, 1);
    }
  }

  // A couple of months into the season: tables, form and reports.
  advanceUntil(controller, GameDateValue(2025, 10, 28));
  Navigation::open(&view, NavSection::HOME);
  frames(view, 4);
  capture(view, folder, "home");

  Navigation::open(&view, NavSection::SQUAD);
  frames(view, 3);
  if (auto* roster = dynamic_cast<RosterScene*>(Bridge::activeScene(view)))
  {
    Bridge::select(*roster,
                   controller.getPlayersForTeam(club).front().get().getId());
    frames(view, 3);
  }
  capture(view, folder, "squad");

  for (const auto& [section, name] :
       {std::pair{NavSection::TACTICS, "tactics"},
        std::pair{NavSection::LINEUP, "lineup"},
        std::pair{NavSection::STANDINGS, "competitions"},
        std::pair{NavSection::SCOUTING, "scouting"},
        std::pair{NavSection::YOUTH, "youth"},
        std::pair{NavSection::FINANCES, "finances"},
        std::pair{NavSection::INBOX, "inbox"}})
  {
    Navigation::open(&view, section);
    frames(view, 4);
    capture(view, folder, name);
  }

  // The next match of the club, some way into the first half.
  controller.advanceToNextManagedFixture(30);
  const auto fixture = controller.getNextManagedFixture();
  ASSERT_TRUE(fixture.has_value());
  ASSERT_TRUE(fixture->date == controller.getCurrentDate());
  const TeamID home = fixture->home ? club : fixture->opponent;
  const TeamID away = fixture->home ? fixture->opponent : club;
  controller.giveTeamTalk(TeamTalkMoment::PreMatch, TeamTalkTone::Motivate);
  controller.giveTeamTalk(TeamTalkMoment::HalfTime, TeamTalkTone::Calm);
  view.changeScene(std::make_unique<MatchScene>(&view, home, away));
  frames(view, 3);
  auto* match = dynamic_cast<MatchScene*>(Bridge::activeScene(view));
  ASSERT_NE(match, nullptr);
  MatchBridge::watchLive(*match);
  MatchEngine& engine = *MatchBridge::engine(*match);
  const auto playTo = [&](float minute)
  {
    for (int step = 0;
         step < 100'000 && (engine.getMatchTimeMinutes() < minute ||
                            engine.getState() != MatchState::PLAYING);
         ++step)
      engine.advance(0.1f);
  };
  playTo(31.0f);
  frames(view, 30);
  if (MatchBridge::viewMode(*match) != MatchViewMode::PITCH_2D)
  {
    SDL_Event key{.key = {}};
    key.key.type = SDL_EVENT_KEY_DOWN;
    key.key.key = SDLK_V;
    match->handleEvent(key);
    frames(view, 10);
  }
  capture(view, folder, "match_2d");
  recordMotion(view, *match, "match-2d", false);

  playTo(38.0f);
  {
    SDL_Event key{.key = {}};
    key.key.type = SDL_EVENT_KEY_DOWN;
    key.key.key = SDLK_V;
    match->handleEvent(key);
    key.key.key = SDLK_1;
    match->handleEvent(key);
  }
  frames(view, 60);
  ASSERT_EQ(MatchBridge::viewMode(*match), MatchViewMode::BROADCAST_3D);
  capture(view, folder, "match_3d");
  recordMotion(view, *match, "match-3d", true);

  playTo(52.0f);
  MatchBridge::setFocus(*match, true);
  frames(view, 60);
  capture(view, folder, "match_3d_focus");
  MatchBridge::setFocus(*match, false);
  frames(view, 2);
}

/** Test access to the World editor (see WorldEditorScene). */
struct WorldEditorTestAccess
{
  static WorldEditorScene::State& state(GUIView& view)
  {
    return *dynamic_cast<WorldEditorScene&>(*Bridge::activeScene(view)).state;
  }
};

namespace
{
using World::Kind;
using EditorState = WorldEditorScene::State;

constexpr int WORLD_WIDTH = 1600;
constexpr int WORLD_HEIGHT = 900;

/** Selects the editor form's tab named @p label. */
void selectTab(const char* label)
{
  for (ImGuiTabBar& bar : ImGui::GetCurrentContext()->TabBars.Buf)
  {
    for (ImGuiTabItem& tab : bar.Tabs)
    {
      if (std::string(ImGui::TabBarGetTabName(&bar, &tab)) == label)
      {
        bar.NextSelectedTabId = tab.ID;
        return;
      }
    }
  }
  ADD_FAILURE() << "missing tab " << label;
}

/** The example world with more clubs and a few named players. */
World::Document showcaseWorld()
{
  World::Document world = World::templateDocument();
  const std::array<const char*, 6> clubs = {"Harbour Town", "Ironbridge",
                                            "Lakeside",     "Marlow Park",
                                            "Oldcastle",    "Redhill"};
  for (const char* name : clubs)
  {
    const auto id = World::create(world, Kind::Club, 1);
    world.find(Kind::Club, id)->operator[]("name") = name;
    world.find(Kind::Club, id)->operator[]("short_name") =
        std::string(name).substr(0, 3);
  }
  const std::array<std::pair<const char*, const char*>, 8> players = {{
      {"Luca", "Marino"},
      {"Tom", "Hartley"},
      {"Idris", "Okoye"},
      {"Marco", "Bellini"},
      {"Noah", "Fischer"},
      {"Jonas", "Lindqvist"},
      {"Diego", "Ramos"},
      {"Elias", "Moreau"},
  }};
  const std::array<const char*, 4> roles = {"GK", "CB", "CM", "ST"};
  for (std::size_t i = 0; i < players.size(); ++i)
  {
    const auto id = World::create(world, Kind::Player, 101 + i % 4);
    auto& player = *world.find(Kind::Player, id);
    player["first_name"] = players[i].first;
    player["last_name"] = players[i].second;
    player["role"] = roles[i % roles.size()];
    player["age"] = 19 + static_cast<int>(i) * 2;
    for (auto& [stat, value] : player["stats"].items())
      value = 48 + static_cast<int>((i * 7 + stat.size() * 3) % 38);
  }
  return world;
}

EditorState& openWorldEditor(GUIView& view, World::Document document)
{
  WorldEditorScene::Options options;
  options.world = std::make_shared<World::Document>(std::move(document));
  view.changeScene(std::make_unique<WorldEditorScene>(&view, options));
  frames(view, 3);
  return WorldEditorTestAccess::state(view);
}
}  // namespace

TEST(ShowcaseCaptures, WorldScreens)
{
  const char* folderSetting = std::getenv("FM_SHOWCASE_DIR");
  if (folderSetting == nullptr || *folderSetting == '\0')
    GTEST_SKIP() << "set FM_SHOWCASE_DIR to write the screenshots";
  const std::filesystem::path folder(folderSetting);
  std::filesystem::create_directories(folder);

  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::EN));
  Settings& settings = SettingsManager::instance()->get();
  settings.screen_tips = false;
  settings.audio_muted = true;
  settings.ui_scale = 1.0f;

  GameController controller;
  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.refreshTheme();
  resize(view, WORLD_WIDTH, WORLD_HEIGHT);
  frames(view, 3);
  const auto shoot = [&](const char* name)
  {
    frames(view, 3);
    capture(view, folder, name, WORLD_WIDTH);
  };

  EditorState& clubs = openWorldEditor(view, showcaseWorld());
  clubs.setPage(EditorState::Page::Club);
  clubs.select(Kind::Club, 103);
  shoot("world_editor_clubs");

  clubs.setPage(EditorState::Page::Player);
  clubs.select(Kind::Player, 3);
  shoot("world_editor_player");

  auto& club = *clubs.doc.find(Kind::Club, 102);
  club["colours"]["home"] = {{"shirt", "#1D428A"}, {"shorts", "#FFFFFF"},
                             {"socks", "#1D428A"}, {"trim", "#FDB913"}};
  club["colours"]["away"] = {{"shirt", "#FFFFFF"}, {"shorts", "#1D428A"},
                             {"socks", "#FFFFFF"}, {"trim", "#1D428A"}};
  club["colours"]["third"] = {{"shirt", "#00843D"}, {"shorts", "#00843D"},
                              {"socks", "#FFFFFF"}, {"trim", "#FFFFFF"}};
  clubs.setPage(EditorState::Page::Club);
  clubs.select(Kind::Club, 102);
  frames(view, 3);
  selectTab(LOC("WORLD_SECTION_KIT"));
  shoot("world_editor_kits");

  World::Document small = World::templateDocument();
  World::removeEntity(small, Kind::Club, 104);
  EditorState& problems = openWorldEditor(view, std::move(small));
  problems.setPage(EditorState::Page::Problems);
  problems.problems(true);
  shoot("world_editor_problems");

  World::Document incoming = World::Document::empty("Coastal Cup World");
  incoming.add(Kind::Country, {{"id", 7}, {"name", "Freeland"},
                               {"nationality", "English"}});
  incoming.add(Kind::League,
               {{"id", 3}, {"name", "Freeland League"}, {"country_id", 7}});
  incoming.add(Kind::Club, {{"id", 9}, {"league_id", 3}, {"name", "Northport"},
                            {"short_name", "NOR"}, {"nickname", "The Ports"},
                            {"founded", 1888}});
  incoming.add(Kind::Club, {{"id", 10}, {"league_id", 3}, {"name", "Midtown"},
                            {"short_name", "MID"}});
  EditorState& merged = openWorldEditor(view, World::templateDocument());
  merged.combineDocument(incoming, "Coastal Cup World");
  merged.applyCombined();
  const auto conflict =
      std::ranges::find(merged.conflicts, Kind::Club, &World::Conflict::kind);
  ASSERT_NE(conflict, merged.conflicts.end());
  merged.conflict_selected = static_cast<int>(conflict - merged.conflicts.begin());
  frames(view, 3);
  merged.popup = EditorState::Popup::None;
  shoot("world_editor_conflicts");

  World::Document library = World::templateDocument();
  World::Manifest manifest = library.manifest();
  manifest.name = "Freeland Classic";
  manifest.author = "Showcase";
  library.setManifest(manifest);
  const auto stored = World::Library::save(library);
  ASSERT_FALSE(stored.empty());
  view.changeScene(std::make_unique<MainMenuScene>(&view));
  frames(view, 3);
  auto& menu = dynamic_cast<MainMenuScene&>(*Bridge::activeScene(view));
  Bridge::worldChoice(menu).open(WorldChoiceDialog::Mode::Choose, stored);
  shoot("world_choice");
  Bridge::worldChoice(menu).close();
  frames(view, 2);
  World::Library::remove(stored);
}
