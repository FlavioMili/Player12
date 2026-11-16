// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <SDL3/SDL.h>
#include <fmt/printf.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <utility>
extern char** environ;
#endif

#if defined(__clang__) || defined(__GNUC__)
extern "C" const char* __lsan_default_suppressions()
{
  return "leak:libSDL3.so\n";
}
#endif

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "global/logger.h"
#include "global/paths.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/fixtures_scene.h"
#include "gui/scenes/lineup_scene.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/main_menu_scene.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/match_scene.h"
#include "gui/scenes/player_profile_scene.h"
#include "gui/scenes/roster_scene.h"
#include "gui/scenes/settings_scene.h"
#include "gui/scenes/staff_scene.h"
#include "gui/scenes/standings_scene.h"
#include "gui/scenes/strategy_scene.h"
#include "gui/scenes/team_selection_scene.h"
#include "gui/scenes/training_scene.h"
#include "gui/scenes/transfer_market_scene.h"
#include "gui/view_models/match_clock.h"
#include "gui/widgets/theme.h"
#include "model/game.h"
#include "model/holiday.h"
#include "model/player.h"
#include "model/scouting.h"
#include "model/season_review.h"
#include "model/settings_manager.h"
#include "model/team.h"

namespace
{
constexpr std::chrono::milliseconds MAX_MATCH_SCENE_ENTRY_TIME{100};
constexpr std::chrono::milliseconds MAX_FIRST_HARDWARE_MATCH_RENDER_TIME{100};
constexpr int LIVE_MATCH_WARMUP_FRAMES = 40;
#ifdef DEBUG
constexpr int AI_DEBUG_WARMUP_FRAMES = 160;
#endif
constexpr float LIVE_MATCH_TEST_FRAME_SECONDS = 0.05f;
#ifdef DEBUG
#endif
}  // namespace

class GameFlowTest : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    // Initialize Logger to prevent segfaults when Game or Database try to log
    Logger::init();

    controller = std::make_unique<GameController>();
    // We cannot easily inject a path into newGame unless we modify it, so for
    // testing we can just call newGame(99) which maps to slot 99
    controller->newGame(99);
  }

  void TearDown() override
  {
    // cleanup
  }

  /** Opens the window of @p view (GUIView grants this fixture access). */
  static bool initializeView(GUIView& view) { return view.initialize(); }

  /** One GUI frame. */
  static void hubFrame(GUIView& view)
  {
    view.applyPendingSceneChanges();
    view.handleEvents();
    view.update(0.016f);
    view.render();
  }

  /** A long holiday started from the hub, running on the Continue worker
   * with its first day complete. Returns the day it would end on. */
  static GameDateValue startLongHoliday(GUIView& view, MainGameScene& hub,
                                        const GameController& controller)
  {
    HolidayPlan plan;
    plan.mode = HolidayMode::UntilDate;
    plan.until = SeasonCalendar::addDays(controller.getCurrentDate(), 120);
    plan.preferences.stop_big_bid = false;
    plan.preferences.stop_injury_crisis = false;
    plan.preferences.stop_key_injury = false;
    plan.preferences.stop_sacking_warning = false;
    hub.requestHoliday(plan);
    for (int frame = 0; frame < 20 && !hub.isAdvancing(); ++frame)
      hubFrame(view);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (hub.isAdvancing() &&
           controller.getContinueProgress().days_done < 1 &&
           std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return plan.until;
  }

  std::unique_ptr<GameController> controller;
};

TEST_F(GameFlowTest, FullLifecycle)
{
  // 1. Start a New Game
  // We assume there's a valid team ID, e.g., team ID 1.
  // Normally we'd fetch an actual team from the game's team list.
  auto teams = controller->getTeams();
  ASSERT_FALSE(teams.empty()) << "No teams loaded in the database!";

  TeamID firstTeamId = teams.front().get().getId();
  EXPECT_NO_THROW(controller->selectManagedTeam(firstTeamId));

  // 2. Data Access Check
  auto userTeamOpt = controller->getManagedTeam();
  ASSERT_TRUE(userTeamOpt.has_value())
      << "User team should be assigned after selectManagedTeam";

  auto roster = controller->getPlayersForTeam(userTeamOpt->get().getId());
  EXPECT_GT(roster.size(), 0) << "User team should have players in the roster";

  // 3. Time Advancement
  // Simulate advancing a few days
  EXPECT_NO_THROW({
    controller->advanceDay();
    controller->advanceDay();
  });

  // 4. Persistence Check
  constexpr Vector2F CUSTOM_POSITION{0.36f, 0.27f};
  constexpr float CUSTOM_PRESSING = 0.83f;
  Team& managedTeam = controller->getManagedTeam()->get();
  managedTeam.getStrategy().setPressing(CUSTOM_PRESSING);
  ASSERT_FALSE(managedTeam.getLineup().getOutfieldPlayers().empty());
  const PlayerID repositionedPlayerId =
      managedTeam.getLineup().getOutfieldPlayers().front().player->getId();
  ASSERT_TRUE(managedTeam.getLineup().moveOutfieldPlayer(repositionedPlayerId,
                                                         CUSTOM_POSITION));
  EXPECT_NO_THROW({ controller->saveGame(); })
      << "saveGame() should not throw or core dump";

  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(99));
  EXPECT_EQ(controller->getManagedTeam()->get().getId(), firstTeamId);
  const Team& reloadedTeam = controller->getManagedTeam()->get();
  EXPECT_FLOAT_EQ(reloadedTeam.getStrategy().getSliders().pressing,
                  CUSTOM_PRESSING);
  const auto reloadedPlayer = std::ranges::find_if(
      reloadedTeam.getLineup().getOutfieldPlayers(),
      [repositionedPlayerId](const Lineup::PositionedPlayer& positioned)
      {
        return positioned.player &&
               positioned.player->getId() == repositionedPlayerId;
      });
  ASSERT_NE(reloadedPlayer,
            reloadedTeam.getLineup().getOutfieldPlayers().end());
  EXPECT_FLOAT_EQ(reloadedPlayer->position.x, CUSTOM_POSITION.x);
  EXPECT_FLOAT_EQ(reloadedPlayer->position.y, CUSTOM_POSITION.y);
}

TEST_F(GameFlowTest, ContinueStopsOnNextManagedMatchday)
{
  const auto teams = controller->getTeams();
  ASSERT_FALSE(teams.empty());
  const TeamID managedTeamId = teams.front().get().getId();
  controller->selectManagedTeam(managedTeamId);

  std::optional<GameDateValue> expectedDate;
  for (const auto& [date, matches] :
       controller->getGame()->getCalendar().getFullCalendar())
  {
    if (date < controller->getCurrentDate()) continue;
    if (std::ranges::any_of(matches,
                            [managedTeamId](const Match& match)
                            {
                              return !match.isPlayed() &&
                                     (match.getHomeTeamId() == managedTeamId ||
                                      match.getAwayTeamId() == managedTeamId);
                            }))
    {
      expectedDate = date;
      break;
    }
  }

  ASSERT_TRUE(expectedDate.has_value());
  const GameDateValue startingDate = controller->getCurrentDate();
  const int advancedDays = controller->advanceToNextManagedFixture();
  EXPECT_GT(advancedDays, 0);
  EXPECT_EQ(controller->getCurrentDate(), *expectedDate);
  EXPECT_FALSE(controller->getCurrentDate() == startingDate);
  EXPECT_EQ(controller->advanceToNextManagedFixture(), 0)
      << "Continue must not skip an unplayed managed fixture";
}

TEST_F(GameFlowTest, GUIFlowLifecycle)
{
  // Enable headless SDL for testing
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");

  GUIView view(*controller);
  EXPECT_TRUE(view.initialize());

  auto step_frame = [&view]()
  {
    view.applyPendingSceneChanges();
    view.handleEvents();
    view.update(0.16f);
    view.render();
    EXPECT_EQ(ImGui::GetCurrentContext()->ErrorCountCurrentFrame, 0)
        << "ImGui reported a usage error (unbalanced push/pop, bad layout)";
  };

  // 1. Initial frame (Main Menu)
  EXPECT_NO_THROW(step_frame());
  EXPECT_NO_THROW(step_frame());
  const auto mainMenuScreenshotPath =
      RuntimePaths::capturePath("main_menu.bmp");
  std::filesystem::remove(mainMenuScreenshotPath);
  EXPECT_TRUE(view.captureScreenshot(mainMenuScreenshotPath.string()));

  // 2. Change to Settings
  view.changeScene(std::make_unique<SettingsScene>(&view));
  EXPECT_NO_THROW(step_frame());

  // 3. Change back to Main Menu
  view.changeScene(std::make_unique<MainMenuScene>(&view));
  EXPECT_NO_THROW(step_frame());

  // 3b. Save management dialogs: a failed load explains itself and the
  // backups list opens for the slot (empty in a fresh runtime root).
  {
    auto* menu = dynamic_cast<MainMenuScene*>(view.getActiveScene());
    ASSERT_NE(menu, nullptr);
    menu->load_error_text = LOC("SAVE_ERROR_CORRUPT");
    menu->load_error_slot = 1;
    menu->load_error_requested = true;
    EXPECT_NO_THROW(step_frame());
    EXPECT_NO_THROW(step_frame());
    const auto errorPath =
        RuntimePaths::capturePath("main_menu_load_error.bmp");
    std::filesystem::remove(errorPath);
    EXPECT_TRUE(view.captureScreenshot(errorPath.string()));
    EXPECT_FALSE(menu->load_error_requested);
    ImGui::GetCurrentContext()->OpenPopupStack.resize(0);
    menu->openBackups(1);
    EXPECT_NO_THROW(step_frame());
    EXPECT_NO_THROW(step_frame());
    EXPECT_FALSE(menu->backups_requested);
    const auto backupsPath = RuntimePaths::capturePath("main_menu_backups.bmp");
    std::filesystem::remove(backupsPath);
    EXPECT_TRUE(view.captureScreenshot(backupsPath.string()));
    ImGui::GetCurrentContext()->OpenPopupStack.resize(0);
  }

  // 4. Change to Main Game Scene
  view.changeScene(std::make_unique<MainGameScene>(&view));
  EXPECT_NO_THROW(step_frame());
  // The club choice draws its first frame transparent while its auto-sized
  // cards measure themselves.
  EXPECT_NO_THROW(step_frame());

  const auto teamSelectionScreenshotPath =
      RuntimePaths::capturePath("team_selection.bmp");
  std::filesystem::remove(teamSelectionScreenshotPath);
  EXPECT_TRUE(view.captureScreenshot(teamSelectionScreenshotPath.string()));
  ASSERT_TRUE(std::filesystem::exists(teamSelectionScreenshotPath));
  EXPECT_GT(std::filesystem::file_size(teamSelectionScreenshotPath), 1'000u);

  // Pop the implicit TeamSelectionScene overlay
  view.popScene();
  EXPECT_NO_THROW(step_frame());

  const auto uiTeams = controller->getTeams();
  ASSERT_FALSE(uiTeams.empty());
  controller->selectManagedTeam(uiTeams.front().get().getId());
  EXPECT_NO_THROW(step_frame());
  ASSERT_NE(view.getActiveScene(), nullptr);
  EXPECT_EQ(view.getActiveScene()->getID(), SceneID::GAME_MENU);
  const auto dashboardScreenshotPath =
      RuntimePaths::capturePath("dashboard.bmp");
  std::filesystem::remove(dashboardScreenshotPath);
  EXPECT_TRUE(view.captureScreenshot(dashboardScreenshotPath.string()));
  ASSERT_TRUE(std::filesystem::exists(dashboardScreenshotPath));
  EXPECT_GT(std::filesystem::file_size(dashboardScreenshotPath), 1'000u);

  auto* dashboard = dynamic_cast<MainGameScene*>(view.getActiveScene());
  ASSERT_NE(dashboard, nullptr);
  dashboard->active_page = MainGameScene::Page::FINANCES;
  view.render();
  const auto financeScreenshotPath = RuntimePaths::capturePath("finances.bmp");
  std::filesystem::remove(financeScreenshotPath);
  EXPECT_TRUE(view.captureScreenshot(financeScreenshotPath.string()));
  ASSERT_TRUE(std::filesystem::exists(financeScreenshotPath));
  EXPECT_GT(std::filesystem::file_size(financeScreenshotPath), 1'000u);
  dashboard->active_page = MainGameScene::Page::OVERVIEW;

  // 5. Roster Scene Overlay
  view.overlayScene(std::make_unique<RosterScene>(&view));
  EXPECT_NO_THROW(step_frame());
  ASSERT_NE(view.getActiveScene(), nullptr);
  EXPECT_EQ(view.getActiveScene()->getID(), SceneID::ROSTER);
  EXPECT_EQ(view.matchFramesTimed, -1);
  auto* rosterScene = dynamic_cast<RosterScene*>(view.getActiveScene());
  ASSERT_NE(rosterScene, nullptr);
  ASSERT_FALSE(rosterScene->roster_players.empty());
  rosterScene->selected_player_id =
      rosterScene->roster_players.front().get().getId();
  view.render();
  const auto rosterScreenshotPath = RuntimePaths::capturePath("roster.bmp");
  std::filesystem::remove(rosterScreenshotPath);
  EXPECT_TRUE(view.captureScreenshot(rosterScreenshotPath.string()));
  ASSERT_TRUE(std::filesystem::exists(rosterScreenshotPath));
  EXPECT_GT(std::filesystem::file_size(rosterScreenshotPath), 1'000u);

  ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::IT));
  view.render();
  const std::filesystem::path italianRosterScreenshotPath =
      RuntimePaths::capturePath("roster_italian.bmp");
  std::filesystem::remove(italianRosterScreenshotPath);
  EXPECT_TRUE(view.captureScreenshot(italianRosterScreenshotPath.string()));
  ASSERT_TRUE(std::filesystem::exists(italianRosterScreenshotPath));
  EXPECT_GT(std::filesystem::file_size(italianRosterScreenshotPath), 1'000u);
  ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::EN));

  // Pop Roster Scene
  view.popScene();
  EXPECT_NO_THROW(step_frame());

  // 6. Lineup Scene Overlay
  view.overlayScene(std::make_unique<LineupScene>(&view));
  EXPECT_NO_THROW(step_frame());
  ASSERT_NE(view.getActiveScene(), nullptr);
  EXPECT_EQ(view.getActiveScene()->getID(), SceneID::LINEUP);
  auto* lineupScene = dynamic_cast<LineupScene*>(view.getActiveScene());
  ASSERT_NE(lineupScene, nullptr);
  ASSERT_NE(lineupScene->current_lineup, nullptr);
  ASSERT_FALSE(lineupScene->current_lineup->getOutfieldPlayers().empty());
  ASSERT_FALSE(lineupScene->current_lineup->getReserves().empty());
  lineupScene->selected_pitch_player_id =
      lineupScene->current_lineup->getOutfieldPlayers().front().player->getId();
  lineupScene->selected_bench_player_id =
      lineupScene->current_lineup->getReserves().front()->getId();
  view.render();
  const auto lineupScreenshotPath = RuntimePaths::capturePath("lineup.bmp");
  std::filesystem::remove(lineupScreenshotPath);
  EXPECT_TRUE(view.captureScreenshot(lineupScreenshotPath.string()));
  ASSERT_TRUE(std::filesystem::exists(lineupScreenshotPath));
  EXPECT_GT(std::filesystem::file_size(lineupScreenshotPath), 1'000u);
  view.popScene();
  EXPECT_NO_THROW(step_frame());

  // 7. Strategy Scene Overlay
  view.overlayScene(std::make_unique<StrategyScene>(&view));
  EXPECT_NO_THROW(step_frame());
  const auto tacticsScreenshotPath = RuntimePaths::capturePath("tactics.bmp");
  std::filesystem::remove(tacticsScreenshotPath);
  EXPECT_TRUE(view.captureScreenshot(tacticsScreenshotPath.string()));
  ASSERT_TRUE(std::filesystem::exists(tacticsScreenshotPath));
  EXPECT_GT(std::filesystem::file_size(tacticsScreenshotPath), 1'000u);

  // Pop Strategy Scene
  view.popScene();
  EXPECT_NO_THROW(step_frame());

  // 7b. Management shell: every sidebar section, the player profile, the
  // command palette and in-career settings, captured for visual review.
  const auto captureScreen = [&view](const char* fileName)
  {
    const auto path = RuntimePaths::capturePath(fileName);
    std::filesystem::remove(path);
    EXPECT_TRUE(view.captureScreenshot(path.string()));
    ASSERT_TRUE(std::filesystem::exists(path));
    EXPECT_GT(std::filesystem::file_size(path), 1'000u);
  };
  const auto openSection = [&](NavSection section, SceneID expected)
  {
    Navigation::open(&view, section);
    step_frame();
    step_frame();
    ASSERT_NE(view.getActiveScene(), nullptr);
    EXPECT_EQ(view.getActiveScene()->getID(), expected);
    EXPECT_LE(view.getOverlayDepth(), 1u)
        << "Section navigation must not stack overlays";
  };
  openSection(NavSection::SQUAD, SceneID::ROSTER);
  captureScreen("shell_squad.bmp");
  openSection(NavSection::LINEUP, SceneID::LINEUP);
  captureScreen("shell_lineup.bmp");
  openSection(NavSection::TACTICS, SceneID::STRATEGY);
  captureScreen("shell_tactics.bmp");
  openSection(NavSection::FIXTURES, SceneID::FIXTURES);
  captureScreen("shell_fixtures.bmp");
  auto* fixturesScene = dynamic_cast<FixturesScene*>(view.getActiveScene());
  ASSERT_NE(fixturesScene, nullptr);
  EXPECT_FALSE(fixturesScene->club_fixtures.empty());
  EXPECT_GT(fixturesScene->round_count, 0);
  fixturesScene->view = FixturesScene::View::LEAGUE;
  step_frame();
  captureScreen("shell_fixtures_rounds.bmp");
  openSection(NavSection::STANDINGS, SceneID::STANDINGS);
  auto* standingsScene = dynamic_cast<StandingsScene*>(view.getActiveScene());
  ASSERT_NE(standingsScene, nullptr);
  EXPECT_EQ(
      standingsScene->table.size(),
      controller
          ->getLeagueById(controller->getManagedTeam()->get().getLeagueId())
          ->get()
          .getTeamIDs()
          .size());
  captureScreen("shell_standings.bmp");
  openSection(NavSection::TRANSFERS, SceneID::TRANSFER_MARKET);
  captureScreen("shell_transfers.bmp");
  openSection(NavSection::INBOX, SceneID::INBOX);
  captureScreen("shell_inbox.bmp");
  openSection(NavSection::CLUB, SceneID::CLUB);
  captureScreen("shell_club.bmp");
  openSection(NavSection::TRAINING, SceneID::TRAINING);
  auto* trainingScene = dynamic_cast<TrainingScene*>(view.getActiveScene());
  ASSERT_NE(trainingScene, nullptr);
  EXPECT_FALSE(trainingScene->rows.empty());
  EXPECT_EQ(trainingScene->week.size(), 7u);
  EXPECT_FALSE(trainingScene->advice.empty());
  captureScreen("shell_training.bmp");
  openSection(NavSection::STAFF, SceneID::STAFF);
  auto* staffScene = dynamic_cast<StaffScene*>(view.getActiveScene());
  ASSERT_NE(staffScene, nullptr);
  EXPECT_FALSE(staffScene->staff_rows.empty());
  EXPECT_FALSE(staffScene->market_rows.empty());
  captureScreen("shell_staff.bmp");
  // Selecting a row opens its inline detail strip (actions live there).
  staffScene->selected = staffScene->staff_rows.front().id;
  step_frame();
  step_frame();
  captureScreen("shell_staff_detail.bmp");
  // The market strip (contract length, Hire) draws without ImGui errors.
  staffScene->selected = staffScene->market_rows.front().id;
  step_frame();
  step_frame();
  staffScene->selected = 0;
  openSection(NavSection::FINANCES, SceneID::GAME_MENU);
  EXPECT_EQ(view.getOverlayDepth(), 0u);
  captureScreen("shell_finances.bmp");
  openSection(NavSection::HOME, SceneID::GAME_MENU);
  captureScreen("shell_home.bmp");

  // Player profile stacks above the current section and Back returns to it.
  openSection(NavSection::SQUAD, SceneID::ROSTER);
  const PlayerID profiledPlayer =
      controller->getPlayersForTeam(controller->getManagedTeam()->get().getId())
          .front()
          .get()
          .getId();
  Navigation::openPlayer(&view, profiledPlayer);
  step_frame();
  step_frame();
  ASSERT_NE(view.getActiveScene(), nullptr);
  EXPECT_EQ(view.getActiveScene()->getID(), SceneID::PLAYER_PROFILE);
  EXPECT_EQ(view.getOverlayDepth(), 2u);
  captureScreen("shell_profile.bmp");
  Navigation::back(&view);
  step_frame();
  EXPECT_EQ(view.getActiveScene()->getID(), SceneID::ROSTER);

  // A real Escape key press goes back as well (routed shortcut).
  Navigation::openPlayer(&view, profiledPlayer);
  step_frame();
  step_frame();
  ASSERT_EQ(view.getActiveScene()->getID(), SceneID::PLAYER_PROFILE);
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
  step_frame();
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
  step_frame();
  step_frame();
  EXPECT_EQ(view.getActiveScene()->getID(), SceneID::ROSTER);

  // Command palette with a query, then in-career settings.
  auto* shellScene = dynamic_cast<ManagementScene*>(view.getActiveScene());
  ASSERT_NE(shellScene, nullptr);
  shellScene->openPalette();
  step_frame();
  step_frame();
  for (const char character : std::string_view("ar"))
    ImGui::GetIO().AddInputCharacter(static_cast<unsigned int>(character));
  step_frame();
  step_frame();
  EXPECT_EQ(shellScene->palette_filtered_query, "ar");
  EXPECT_FALSE(shellScene->palette_matches.empty());
  captureScreen("shell_palette.bmp");
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
  step_frame();
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
  ImGui::GetIO().AddMousePosEvent(640.0f, 400.0f);
  step_frame();
  // Moving the mouse hides the keyboard-navigation frame, as for a user.
  ImGui::GetIO().AddMousePosEvent(660.0f, 420.0f);
  step_frame();
  EXPECT_FALSE(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
  view.navigateTo(std::make_unique<SettingsScene>(&view, true));
  step_frame();
  step_frame();
  EXPECT_EQ(view.getActiveScene()->getID(), SceneID::SETTINGS);
  captureScreen("shell_settings.bmp");
  view.popScene();
  step_frame();
  EXPECT_EQ(view.getActiveScene()->getID(), SceneID::GAME_MENU);

  // Responsive layouts: full HD and a narrow window (collapsed sidebar).
  const auto resizeTo = [&](int width, int height)
  {
    SDL_SetWindowSize(view.getWindow(), width, height);
    step_frame();
    step_frame();
  };
  resizeTo(1920, 1080);
  captureScreen("shell_home_1080p.bmp");
  openSection(NavSection::SQUAD, SceneID::ROSTER);
  captureScreen("shell_squad_1080p.bmp");
  openSection(NavSection::STANDINGS, SceneID::STANDINGS);
  captureScreen("shell_standings_1080p.bmp");
  Navigation::openPlayer(&view, profiledPlayer);
  step_frame();
  step_frame();
  captureScreen("shell_profile_1080p.bmp");
  resizeTo(1024, 700);
  openSection(NavSection::HOME, SceneID::GAME_MENU);
  captureScreen("shell_home_narrow.bmp");
  openSection(NavSection::TRANSFERS, SceneID::TRANSFER_MARKET);
  captureScreen("shell_transfers_narrow.bmp");
  // Target tables hide columns instead of scrolling sideways, down to a
  // 640-point wide window (1280x720 at UI scale 2), and never scroll inside
  // a page that scrolls itself.
  const auto targetTableProblems = [](bool& shown)
  {
    std::string problems;
    const ImGuiWindow* page = nullptr;
    constexpr std::string_view PAGE = "##management_shell/##content";
    for (const ImGuiWindow* window : GImGui->Windows)
    {
      const std::string_view name(window->Name);
      if (window->Active && name.starts_with(PAGE) &&
          !name.substr(PAGE.size()).contains('/'))
        page = window;
    }
    if (page == nullptr) return std::string("no page");
    for (int index = 0; index < GImGui->Tables.GetMapSize(); ++index)
    {
      const ImGuiTable* table = GImGui->Tables.TryGetMapData(index);
      if (table == nullptr || table->LastFrameActive < GImGui->FrameCount - 1 ||
          table->ColumnsCount < 4)
        continue;
      shown = true;
      if ((table->Flags & ImGuiTableFlags_ScrollX) != 0)
        problems += " [scrolls sideways]";
      if ((table->Flags & ImGuiTableFlags_ScrollY) != 0 && page->ScrollbarY)
        problems += " [scrolls inside the scrolling page]";
    }
    return problems;
  };
  {
    // The Search tab always lists players (Recommended can be empty).
    bool searchSelected = false;
    for (int index = 0; index < GImGui->TabBars.GetMapSize(); ++index)
      if (ImGuiTabBar* bar = GImGui->TabBars.TryGetMapData(index))
        for (ImGuiTabItem& tab : bar->Tabs)
          if (std::string_view(ImGui::TabBarGetTabName(bar, &tab)) ==
              LOC("TRANSFER_TAB_SEARCH"))
          {
            bar->NextSelectedTabId = tab.ID;
            searchSelected = true;
          }
    ASSERT_TRUE(searchSelected);
    // Everyone, not only the affordable players (the wage room may be 0).
    auto* market = dynamic_cast<TransferMarketScene*>(view.getActiveScene());
    ASSERT_NE(market, nullptr);
    market->filters.affordable_only = false;
    market->search_dirty = true;
    step_frame();
    step_frame();
    step_frame();
    captureScreen("shell_transfers_search_narrow.bmp");
    bool shown = false;
    EXPECT_EQ(targetTableProblems(shown), "") << "1024x700";
    EXPECT_TRUE(shown) << "no target table on the transfers screen";
    Settings& scaled = SettingsManager::instance()->get();
    const float originalScale = scaled.ui_scale;
    scaled.ui_scale = 2.0f;
    view.refreshTheme();
    resizeTo(1280, 720);
    step_frame();
    // Scroll the page down to the table for the screenshot.
    for (ImGuiWindow* window : GImGui->Windows)
      if (window->Active &&
          std::string_view(window->Name)
              .starts_with("##management_shell/##content") &&
          !std::string_view(window->Name).substr(29).contains('/'))
        ImGui::SetScrollY(window, 1150.0f);
    step_frame();
    step_frame();
    captureScreen("shell_transfers_scale2.bmp");
    shown = false;
    EXPECT_EQ(targetTableProblems(shown), "") << "1280x720 at scale 2";
    EXPECT_TRUE(shown) << "no target table at scale 2";
    scaled.ui_scale = originalScale;
    view.refreshTheme();
    resizeTo(1024, 700);
  }
  openSection(NavSection::LINEUP, SceneID::LINEUP);
  captureScreen("shell_lineup_narrow.bmp");
  resizeTo(1280, 720);
  openSection(NavSection::HOME, SceneID::GAME_MENU);

  // 8. Render a live match and export a frame for visual/headless debugging.
  const auto teams = controller->getTeams();
  ASSERT_GE(teams.size(), 2u);
  controller->selectManagedTeam(teams[0].get().getId());
  view.overlayScene(std::make_unique<MatchScene>(&view, teams[0].get().getId(),
                                                 teams[1].get().getId()));
  EXPECT_EQ(view.matchFramesTimed, -1);
  const auto matchSceneEntryStart = std::chrono::steady_clock::now();
  view.applyPendingSceneChanges();
  EXPECT_EQ(view.matchFramesTimed, 0);
  const auto matchSceneEntryDuration =
      std::chrono::steady_clock::now() - matchSceneEntryStart;
  const auto matchSceneEntryMicroseconds =
      std::chrono::duration_cast<std::chrono::microseconds>(
          matchSceneEntryDuration)
          .count();
  RecordProperty("match_scene_entry_microseconds", matchSceneEntryMicroseconds);
  EXPECT_LT(matchSceneEntryDuration, MAX_MATCH_SCENE_ENTRY_TIME)
      << "Match scene initialization blocked the UI thread";
  auto* matchScene = dynamic_cast<MatchScene*>(view.getActiveScene());
  ASSERT_NE(matchScene, nullptr);
  const Lineup& managedLineup = teams[0].get().getLineup();
  ASSERT_FALSE(managedLineup.getOutfieldPlayers().empty());
  ASSERT_FALSE(managedLineup.getReserves().empty());
  matchScene->show_substitutions = true;
  matchScene->is_paused = true;
  matchScene->subs_panel.selected_out =
      managedLineup.getOutfieldPlayers().front().player->getId();
  matchScene->subs_panel.selected_in =
      managedLineup.getReserves().front()->getId();
  view.render();
  view.render();
  const std::filesystem::path substitutionScreenshotPath =
      RuntimePaths::capturePath("substitution.bmp");
  std::filesystem::remove(substitutionScreenshotPath);
  EXPECT_TRUE(view.captureScreenshot(substitutionScreenshotPath.string()));
  ASSERT_TRUE(std::filesystem::exists(substitutionScreenshotPath));
  EXPECT_GT(std::filesystem::file_size(substitutionScreenshotPath), 1'000u);
  matchScene->show_substitutions = false;
  matchScene->is_paused = false;
  view.beginMatchRenderTimings();
  const auto firstMatchRenderStart = std::chrono::steady_clock::now();
  view.render();
  EXPECT_EQ(view.matchFramesTimed, 1);
  const auto firstMatchRenderDuration =
      std::chrono::steady_clock::now() - firstMatchRenderStart;
  const auto firstMatchRenderMicroseconds =
      std::chrono::duration_cast<std::chrono::microseconds>(
          firstMatchRenderDuration)
          .count();
  RecordProperty("first_match_render_microseconds",
                 firstMatchRenderMicroseconds);
  if (!view.rendererIsSoftware)
  {
    EXPECT_LT(firstMatchRenderDuration, MAX_FIRST_HARDWARE_MATCH_RENDER_TIME)
        << "The first hardware-rendered match frame blocked the UI thread";
  }
  for (int frame = 0; frame < LIVE_MATCH_WARMUP_FRAMES; ++frame)
  {
    view.update(LIVE_MATCH_TEST_FRAME_SECONDS);
  }
  const auto screenshotPath = RuntimePaths::capturePath("screenshot.bmp");
  std::filesystem::remove(screenshotPath);
  view.screenshotPending = true;
  EXPECT_NO_THROW(step_frame());
  ASSERT_TRUE(std::filesystem::exists(screenshotPath));
  EXPECT_GT(std::filesystem::file_size(screenshotPath), 1'000u);
  // The same HUD over the broadcast (3D) view.
  matchScene->setViewMode(MatchViewMode::BROADCAST_3D);
  EXPECT_NO_THROW(step_frame());
  EXPECT_NO_THROW(step_frame());
  captureScreen("match_hud_3d.bmp");
  matchScene->setViewMode(MatchViewMode::PITCH_2D);
  // Responsive HUD: narrow windows (the last one stacks the panels below the
  // pitch) and 1440p at a 200% interface scale.
  resizeTo(1024, 700);
  captureScreen("match_hud_narrow.bmp");
  resizeTo(900, 640);
  captureScreen("match_hud_stacked.bmp");
  {
    Settings& settings = SettingsManager::instance()->get();
    const float originalScale = settings.ui_scale;
    settings.ui_scale = 2.0f;
    view.refreshTheme();
    resizeTo(2560, 1440);
    captureScreen("match_hud_1440p_200.bmp");
    settings.ui_scale = originalScale;
    view.refreshTheme();
  }
  resizeTo(1280, 720);

  // Capture the tactical overlay later in the match. This artifact makes AI
  // target churn, duplicated runs and broken defensive spacing inspectable in
  // headless CI as well as during local development. The AI overlay and the
  // machine-readable snapshot are debug-only features, so they are exercised
  // only in Debug builds.
#ifdef DEBUG
  SDL_Event debugEvent{};
  debugEvent.type = SDL_EVENT_KEY_DOWN;
  debugEvent.key.key = SDLK_F10;
  ASSERT_NE(view.getActiveScene(), nullptr);
  view.getActiveScene()->handleEvent(debugEvent);
  for (int frame = 0; frame < AI_DEBUG_WARMUP_FRAMES; ++frame)
  {
    view.update(LIVE_MATCH_TEST_FRAME_SECONDS);
  }
  const auto debugSnapshotPath = RuntimePaths::capturePath("match.json");
  std::filesystem::remove(debugSnapshotPath);
  SDL_Event exportEvent{};
  exportEvent.type = SDL_EVENT_KEY_DOWN;
  exportEvent.key.key = SDLK_F11;
  view.getActiveScene()->handleEvent(exportEvent);
  ASSERT_TRUE(std::filesystem::exists(debugSnapshotPath));
  std::ifstream snapshotInput(debugSnapshotPath);
  const nlohmann::json debugSnapshot = nlohmann::json::parse(snapshotInput);
  EXPECT_TRUE(debugSnapshot.contains("team_phase"));
  EXPECT_TRUE(
      debugSnapshot["team_phase"].contains("transition_seconds_remaining"));
  EXPECT_TRUE(debugSnapshot.contains("players"));
  EXPECT_TRUE(debugSnapshot.contains("decision"));
  EXPECT_TRUE(debugSnapshot["decision"].contains("reason"));
  EXPECT_FALSE(debugSnapshot["decision"]["reason"].get<std::string>().empty());
  EXPECT_TRUE(debugSnapshot["decision"].contains("analysis"));
  const auto& analysis = debugSnapshot["decision"]["analysis"];
  EXPECT_TRUE(analysis.contains("pass"));
  EXPECT_TRUE(analysis.contains("shot"));
  EXPECT_TRUE(analysis.contains("carry"));
  EXPECT_TRUE(analysis.contains("shield"));
  const auto chosenUtility = [&debugSnapshot, &analysis]()
  {
    const std::string action =
        debugSnapshot["decision"]["action"].get<std::string>();
    if (action == "shot") return analysis["shot"].get<float>();
    if (action == "carry") return analysis["carry"].get<float>();
    if (action == "shield") return analysis["shield"].get<float>();
    return analysis["pass"].get<float>();
  }();
  for (const std::string& key : {"pass", "shot", "carry", "shield"})
  {
    if (analysis[key].is_null()) continue;
    EXPECT_GE(chosenUtility + 1e-4f, analysis[key].get<float>());
  }
  view.render();
  const auto debugScreenshotPath = RuntimePaths::capturePath("ai_debug.bmp");
  std::filesystem::remove(debugScreenshotPath);
  EXPECT_TRUE(view.captureScreenshot(debugScreenshotPath.string()));
  ASSERT_TRUE(std::filesystem::exists(debugScreenshotPath));
  EXPECT_GT(std::filesystem::file_size(debugScreenshotPath), 1'000u);
#endif
}

TEST_F(GameFlowTest, ManagementScreensMidSeason)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  const auto teams = controller->getTeams();
  ASSERT_FALSE(teams.empty());
  const TeamID managedId = teams.front().get().getId();
  controller->selectManagedTeam(managedId);

  // Play several managed fixtures so tables, results, the inbox, the ledger
  // and player statistics hold real data.
  constexpr int MATCHDAYS = 8;
  for (int matchday = 0; matchday < MATCHDAYS; ++matchday)
  {
    controller->advanceToNextManagedFixture();
    const GameDateValue today = controller->getCurrentDate();
    for (const Match& match :
         controller->getGame()->getCalendar().getMatchesForDate(today))
    {
      if (match.isPlayed() || (match.getHomeTeamId() != managedId &&
                               match.getAwayTeamId() != managedId))
        continue;
      const auto home = static_cast<uint8_t>(2 + matchday % 2);
      const auto away = static_cast<uint8_t>(matchday % 3);
      ASSERT_TRUE(controller->setMatchResult(
          today, match.getHomeTeamId(), match.getAwayTeamId(), home, away));
      break;
    }
    controller->advanceDay();
  }

  GUIView view(*controller);
  ASSERT_TRUE(view.initialize());
  const auto step_frame = [&view]()
  {
    view.applyPendingSceneChanges();
    view.handleEvents();
    view.update(0.016f);
    view.render();
    EXPECT_EQ(ImGui::GetCurrentContext()->ErrorCountCurrentFrame, 0)
        << "ImGui reported a usage error (unbalanced push/pop, bad layout)";
  };
  const auto capture = [&view](const char* fileName)
  {
    const auto path = RuntimePaths::capturePath(fileName);
    std::filesystem::remove(path);
    EXPECT_TRUE(view.captureScreenshot(path.string()));
    ASSERT_TRUE(std::filesystem::exists(path));
    EXPECT_GT(std::filesystem::file_size(path), 1'000u);
  };
  view.changeScene(std::make_unique<MainGameScene>(&view));
  step_frame();
  step_frame();
  ASSERT_EQ(view.getActiveScene()->getID(), SceneID::GAME_MENU);
  capture("season_home.bmp");

  // Every sidebar destination fits at 720p without scrolling.
  {
    auto* shell = dynamic_cast<ManagementScene*>(view.getActiveScene());
    ASSERT_NE(shell, nullptr);
    EXPECT_FALSE(shell->sidebar_nav_overflow);
  }

  const std::array<std::pair<NavSection, const char*>, 12> screens = {{
      {NavSection::INBOX, "season_inbox.bmp"},
      {NavSection::SQUAD, "season_squad.bmp"},
      {NavSection::LINEUP, "season_lineup.bmp"},
      {NavSection::FIXTURES, "season_fixtures.bmp"},
      {NavSection::STANDINGS, "season_standings.bmp"},
      {NavSection::TRANSFERS, "season_transfers.bmp"},
      {NavSection::FINANCES, "season_finances.bmp"},
      {NavSection::CLUB, "season_club.bmp"},
      {NavSection::TACTICS, "season_tactics.bmp"},
      {NavSection::SCOUTING, "season_scouting.bmp"},
      {NavSection::TRAINING, "season_training.bmp"},
      {NavSection::STAFF, "season_staff.bmp"},
  }};
  for (const auto& [section, fileName] : screens)
  {
    Navigation::open(&view, section);
    step_frame();
    step_frame();
    capture(fileName);
  }
  const PlayerID star =
      controller->getPlayersForTeam(managedId).front().get().getId();
  Navigation::openPlayer(&view, star);
  step_frame();
  step_frame();
  EXPECT_EQ(view.getActiveScene()->getID(), SceneID::PLAYER_PROFILE);
  capture("season_profile.bmp");

  // The busiest text screens again in Italian, for reviewing the wording.
  ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::IT));
  const std::array<std::pair<NavSection, const char*>, 4> italianScreens = {{
      {NavSection::INBOX, "season_inbox_italian.bmp"},
      {NavSection::SQUAD, "season_squad_italian.bmp"},
      {NavSection::FIXTURES, "season_fixtures_italian.bmp"},
      {NavSection::STANDINGS, "season_standings_italian.bmp"},
  }};
  for (const auto& [section, fileName] : italianScreens)
  {
    Navigation::open(&view, section);
    step_frame();
    step_frame();
    capture(fileName);
  }
  Navigation::openPlayer(&view, star);
  step_frame();
  step_frame();
  capture("season_profile_italian.bmp");
  ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::EN));
  view.popScene();
  step_frame();
  Navigation::openPlayer(&view, star);
  step_frame();
  step_frame();
  EXPECT_EQ(view.getActiveScene()->getID(), SceneID::PLAYER_PROFILE);

  // Every dialog renders cleanly: transfer-list confirm, contract renewal
  // and the leave-to-menu confirmation.
  auto* profile = dynamic_cast<PlayerProfileScene*>(view.getActiveScene());
  ASSERT_NE(profile, nullptr);
  profile->list_confirm_requested = true;
  step_frame();
  step_frame();
  capture("season_dialog_list.bmp");
  const auto pressEscape = [&]()
  {
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
    step_frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
    step_frame();
  };
  pressEscape();
  EXPECT_FALSE(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
  EXPECT_EQ(view.getActiveScene()->getID(), SceneID::PLAYER_PROFILE);
  profile->renew_requested = true;
  step_frame();
  step_frame();
  capture("season_dialog_renew.bmp");
  pressEscape();
  EXPECT_FALSE(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
  profile->main_menu_confirm_requested = true;
  step_frame();
  step_frame();
  capture("season_dialog_menu.bmp");
  pressEscape();
  EXPECT_FALSE(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));

  // Another club's player is shown through scouting estimates only.
  const TeamID otherClub = teams.back().get().getId();
  ASSERT_NE(otherClub, managedId);
  const PlayerID scoutedId =
      controller->getPlayersForTeam(otherClub).front().get().getId();
  const auto scoutedView = controller->getScoutedView(scoutedId);
  ASSERT_TRUE(scoutedView.has_value());
  EXPECT_FALSE(scoutedView->own);
  Navigation::openPlayer(&view, scoutedId);
  step_frame();
  step_frame();
  auto* scoutedProfile =
      dynamic_cast<PlayerProfileScene*>(view.getActiveScene());
  ASSERT_NE(scoutedProfile, nullptr);
  EXPECT_TRUE(scoutedProfile->scouted);
  EXPECT_FLOAT_EQ(scoutedProfile->row.overall, scoutedView->overall);
  EXPECT_EQ(scoutedProfile->knowledge, scoutedView->knowledge);
  EXPECT_TRUE(scoutedProfile->fits.empty()) << "role fits use true stats";
  size_t rangedLines = 0;
  for (const auto& section : scoutedProfile->sections)
  {
    for (const auto& line : section.lines)
    {
      const auto estimate = std::ranges::find_if(
          scoutedView->attributes, [&](const ScoutedAttribute& attribute)
          { return attribute.estimate == line.value; });
      EXPECT_NE(estimate, scoutedView->attributes.end()) << line.name;
      EXPECT_LE(line.low, line.value);
      EXPECT_GE(line.high, line.value);
      ++rangedLines;
    }
  }
  EXPECT_EQ(rangedLines, scoutedView->attributes.size());
  capture("season_profile_scouted.bmp");
  // Recruitment actions work from the profile.
  EXPECT_FALSE(controller->isShortlisted(scoutedId));
  ASSERT_TRUE(controller->addToShortlist(scoutedId));
  scoutedProfile->refresh();
  EXPECT_TRUE(scoutedProfile->shortlisted);
  scoutedProfile->sendScout();
  EXPECT_TRUE(scoutedProfile->being_scouted || controller->getScouts().empty());
  step_frame();
  capture("season_profile_scouted_actions.bmp");
  // "Make an offer" opens the market with the deal dialog for him.
  view.navigateTo(std::make_unique<TransferMarketScene>(&view, scoutedId));
  step_frame();
  step_frame();
  EXPECT_EQ(view.getActiveScene()->getID(), SceneID::TRANSFER_MARKET);
  EXPECT_TRUE(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
  capture("season_offer_from_profile.bmp");
  pressEscape();
  EXPECT_FALSE(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));

  // Report of the latest managed result, then the national cup draw.
  const auto fixtures = controller->getTeamFixtures(managedId);
  const auto lastPlayed =
      std::ranges::find_if(fixtures.rbegin(), fixtures.rend(),
                           [](const Match& match) { return match.isPlayed(); });
  ASSERT_NE(lastPlayed, fixtures.rend());
  Navigation::openMatchReport(&view, lastPlayed->getDate(),
                              lastPlayed->getHomeTeamId(),
                              lastPlayed->getAwayTeamId());
  step_frame();
  step_frame();
  EXPECT_EQ(view.getActiveScene()->getID(), SceneID::MATCH_REPORT);
  capture("season_report.bmp");

  Navigation::open(&view, NavSection::STANDINGS);
  step_frame();
  auto* standings = dynamic_cast<StandingsScene*>(view.getActiveScene());
  ASSERT_NE(standings, nullptr);
  EXPECT_FALSE(standings->table.empty());
  standings->showing_cup = true;
  standings->refresh();
  step_frame();
  step_frame();
  capture("season_cup.bmp");

  // Appearance options apply live: light and high-contrast palettes,
  // compact density and a 150% interface scale.
  Settings& settings = SettingsManager::instance()->get();
  const Settings original = settings;
  const auto appearance = [&](int preset, bool compact, float scale,
                              const char* home, const char* squad)
  {
    settings.theme_preset = preset;
    settings.compact_density = compact;
    settings.ui_scale = scale;
    view.refreshTheme();
    Navigation::open(&view, NavSection::HOME);
    step_frame();
    step_frame();
    capture(home);
    Navigation::open(&view, NavSection::SQUAD);
    step_frame();
    step_frame();
    capture(squad);
  };
  appearance(static_cast<int>(std::to_underlying(Theme::Preset::LIGHT)), false,
             0.0f, "theme_light_home.bmp", "theme_light_squad.bmp");
  appearance(static_cast<int>(std::to_underlying(Theme::Preset::HIGH_CONTRAST)),
             true, 0.0f, "theme_contrast_home.bmp", "theme_contrast_squad.bmp");
  appearance(static_cast<int>(std::to_underlying(Theme::Preset::MIDNIGHT_BLUE)),
             false, 1.5f, "theme_scaled_home.bmp", "theme_scaled_squad.bmp");
  EXPECT_FLOAT_EQ(Theme::scale(), 1.5f);
  appearance(static_cast<int>(std::to_underlying(Theme::Preset::TRUE_DARK)),
             false, 0.0f, "theme_truedark_home.bmp",
             "theme_truedark_squad.bmp");
  settings = original;
  view.refreshTheme();

  // Window sizes x interface scales: the screens that pack the most text.
  struct Layout
  {
    int width;
    int height;
    float scale;
    const char* tag;
  };
  const std::array<Layout, 5> layouts = {{{1366, 768, 1.0f, "1366_100"},
                                          {1280, 720, 1.25f, "1280_125"},
                                          {1920, 1080, 1.5f, "1920_150"},
                                          {2560, 1440, 2.0f, "2560_200"},
                                          {1280, 720, 2.0f, "1280_200"}}};
  for (const Layout& layout : layouts)
  {
    SDL_SetWindowSize(view.getWindow(), layout.width, layout.height);
    settings.ui_scale = layout.scale;
    view.refreshTheme();
    for (const auto& [section, name] :
         {std::pair{NavSection::HOME, "home"},
          std::pair{NavSection::SQUAD, "squad"},
          std::pair{NavSection::CLUB, "club"},
          std::pair{NavSection::STAFF, "staff"},
          std::pair{NavSection::TRAINING, "training"}})
    {
      Navigation::open(&view, section);
      step_frame();
      step_frame();
      capture(std::format("layout_{}_{}.bmp", layout.tag, name).c_str());
    }
    Navigation::openPlayer(&view, star);
    step_frame();
    step_frame();
    capture(std::format("layout_{}_profile.bmp", layout.tag).c_str());
  }
  settings = original;
  SDL_SetWindowSize(view.getWindow(), 1280, 720);
  view.refreshTheme();

  // Continue: a progress card over the dimmed, frozen screen.
  Navigation::open(&view, NavSection::HOME);
  step_frame();
  auto* hub = dynamic_cast<MainGameScene*>(view.getBaseScene());
  ASSERT_NE(hub, nullptr);
  if (hub->continueLabel() != LOC("DASHBOARD_PLAY_MATCH"))
  {
    hub->requestContinue();
    step_frame();
    EXPECT_NE(view.getBackdrop(), nullptr);
    step_frame();
    // The card appears only when Continue lasts longer than a blink.
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    step_frame();
    step_frame();
    if (hub->isAdvancing()) capture("season_continue.bmp");
    for (int frame = 0; frame < 2000 && hub->isAdvancing(); ++frame)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      step_frame();
    }
    EXPECT_FALSE(hub->isAdvancing());
    EXPECT_EQ(view.getBackdrop(), nullptr);
  }

  // Match day with an injured starter: kick-off is blocked by the lineup
  // check until the selection is fixed.
  const auto& matchDay = controller->getGame()->getCalendar().getMatchesForDate(
      controller->getCurrentDate());
  const auto managedFixture = std::ranges::find_if(
      matchDay,
      [managedId](const Match& match)
      {
        return !match.isPlayed() && (match.getHomeTeamId() == managedId ||
                                     match.getAwayTeamId() == managedId);
      });
  ASSERT_NE(managedFixture, matchDay.end());
  const PlayerID injuredStarter = controller->getManagedTeam()
                                      ->get()
                                      .getLineup()
                                      .getOutfieldPlayers()
                                      .front()
                                      .player->getId();
  controller->getGameData()
      ->getPlayers()
      .at(injuredStarter)
      .mutableDynamics()
      .injury_days = 5;
  controller->setAssistantFixesLineup(false);
  view.overlayScene(std::make_unique<MatchScene>(
      &view, managedFixture->getHomeTeamId(), managedFixture->getAwayTeamId()));
  step_frame();
  step_frame();
  auto* gate = dynamic_cast<MatchScene*>(view.getActiveScene());
  ASSERT_NE(gate, nullptr);
  EXPECT_EQ(gate->engine, nullptr);
  EXPECT_FALSE(gate->lineup_problems.empty());
  capture("season_match_lineup_gate.bmp");
  view.popScene();
  step_frame();
  // With the assistant in charge the match starts with a fixed lineup.
  controller->setAssistantFixesLineup(true);
  view.overlayScene(std::make_unique<MatchScene>(
      &view, managedFixture->getHomeTeamId(), managedFixture->getAwayTeamId()));
  step_frame();
  for (int frame = 0; frame < 30; ++frame) step_frame();
  auto* live = dynamic_cast<MatchScene*>(view.getActiveScene());
  ASSERT_NE(live, nullptr);
  ASSERT_NE(live->engine, nullptr);
  EXPECT_FALSE(live->pre_match_note.empty());
  capture("season_match_assistant_fixed.bmp");
  // Quick result: the rest is played off the UI thread (a progress card
  // shows meanwhile) and the report opens.
  const GameDateValue matchDate = controller->getCurrentDate();
  ASSERT_TRUE(live->quickResult());
  step_frame();
  capture("season_match_quick_progress.bmp");
  // Once the match is over the hub simulates the rest of the day on its
  // Continue worker, then opens the report.
  for (int frame = 0; frame < 6000 && (view.getActiveScene()->getID() !=
                                           SceneID::MATCH_REPORT ||
                                       hub->isAdvancing());
       ++frame)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    step_frame();
  }
  step_frame();
  EXPECT_FALSE(hub->isAdvancing());
  EXPECT_EQ(view.getActiveScene()->getID(), SceneID::MATCH_REPORT);
  EXPECT_TRUE(controller->getCurrentDate() == matchDate + size_t{1})
      << controller->getCurrentDate().toString();
  capture("season_match_quick_report.bmp");
}

TEST_F(GameFlowTest, SaveSlotMetadata)
{
  // Clean up any potential leftover from a previous run on slot 99
  controller.reset();

  // Re-create controller to ensure clean state
  controller = std::make_unique<GameController>();

  // Check metadata for empty/non-existent slot (e.g. slot 100)
  auto metadata_nonexistent = controller->getSaveSlotMetadata(100);
  EXPECT_FALSE(metadata_nonexistent.exists);

  // Re-create slot 99
  controller->newGame(99);

  // Check metadata for slot 99 (newly created game, no team selected yet)
  auto metadata_new = controller->getSaveSlotMetadata(99);
  EXPECT_TRUE(metadata_new.exists);
  EXPECT_TRUE(metadata_new.team_name.empty());

  // Select team, save, check metadata
  auto teams = controller->getTeams();
  ASSERT_FALSE(teams.empty());
  TeamID testTeamId = teams.front().get().getId();
  std::string testTeamName = teams.front().get().getName();

  controller->selectManagedTeam(testTeamId);
  controller->saveGame();

  auto metadata_saved = controller->getSaveSlotMetadata(99);
  EXPECT_TRUE(metadata_saved.exists);
  EXPECT_EQ(metadata_saved.team_name, testTeamName);
  EXPECT_FALSE(metadata_saved.game_date.empty());
  EXPECT_FALSE(metadata_saved.real_date.empty());
}

TEST_F(GameFlowTest, RuntimeRootDoesNotTouchExternalSentinels)
{
  const auto runtimeRoot = RuntimePaths::root();
  const auto sourceRoot = std::filesystem::path(FM_SOURCE_DIR);
  EXPECT_NE(runtimeRoot, sourceRoot);

  const auto sentinel = runtimeRoot.parent_path() /
                        std::format("football-management-sentinel-{}",
                                    static_cast<unsigned long>(SDL_GetTicks()));
  {
    std::ofstream output(sentinel);
    ASSERT_TRUE(output.is_open());
    output << "do not modify";
  }

  controller->newGame(1);
  std::ifstream input(sentinel);
  std::string contents;
  std::getline(input, contents);
  EXPECT_EQ(contents, "do not modify");
  std::filesystem::remove(sentinel);
}

namespace
{
/** Plays a live engine to full time (headless fast path when available). */
template <typename Engine>
void playToFullTime(Engine& engine)
{
  if constexpr (requires { engine.simulateToEnd(); })
  {
    engine.simulateToEnd();
  }
  else
  {
    while (engine.getState() != MatchState::FULL_TIME) engine.update(0.2f);
  }
}

/** Advances to the club's next match day and returns that fixture. */
std::optional<Match> nextFixtureOf(GameController& controller, TeamID club)
{
  controller.advanceToNextManagedFixture();
  for (const Match& match :
       controller.getGame()->getCalendar().getMatchesForDate(
           controller.getCurrentDate()))
  {
    if (!match.isPlayed() &&
        (match.getHomeTeamId() == club || match.getAwayTeamId() == club))
      return match;
  }
  return std::nullopt;
}

Player& worldPlayer(const GameController& controller, PlayerID id)
{
  return controller.getGameData()->getPlayers().at(id);
}
}  // namespace

TEST(MatchClock, MinuteLabelsShowAddedTime)
{
  EXPECT_EQ(MatchClock::minuteLabel(0.0f, 1, false), "0'");
  EXPECT_EQ(MatchClock::minuteLabel(12.3f, 1, false), "13'");
  EXPECT_EQ(MatchClock::minuteLabel(44.9f, 1, false), "45'");
  EXPECT_EQ(MatchClock::minuteLabel(45.2f, 1, true), "45+1'");
  EXPECT_EQ(MatchClock::minuteLabel(47.5f, 1, true), "45+3'");
  EXPECT_EQ(MatchClock::minuteLabel(45.0f, 2, false), "46'");
  EXPECT_EQ(MatchClock::minuteLabel(89.99f, 2, false), "90'");
  EXPECT_EQ(MatchClock::minuteLabel(93.1f, 2, true), "90+4'");
  // Extra time runs on from 90 to 105 and 120, each with its added time.
  EXPECT_EQ(MatchClock::minuteLabel(90.0f, 3, false), "91'");
  EXPECT_EQ(MatchClock::minuteLabel(104.9f, 3, false), "105'");
  EXPECT_EQ(MatchClock::minuteLabel(106.2f, 3, true), "105+2'");
  EXPECT_EQ(MatchClock::minuteLabel(105.0f, 4, false), "106'");
  EXPECT_EQ(MatchClock::minuteLabel(121.5f, 4, true), "120+2'");
  EXPECT_EQ(MatchClock::clockLabel(98.5f, 3, false), "98:30");
  EXPECT_EQ(MatchClock::clockLabel(121.25f, 4, true), "120+1:15");
}

TEST(MatchClock, ReportEventsMatchTheLiveFeed)
{
  // The report stores whole minutes; its label equals the live one.
  const auto reported = [](float timeMinute, float addedMinute)
  {
    MatchReportEvent event;
    event.minute = static_cast<uint8_t>(timeMinute);
    event.added_minute = static_cast<uint8_t>(addedMinute);
    return MatchClock::minuteLabel(event);
  };
  EXPECT_EQ(reported(69.4f, 0.0f), MatchClock::minuteLabel(69.4f, 2, false));
  EXPECT_EQ(reported(12.0f, 0.0f), MatchClock::minuteLabel(12.0f, 1, false));
  EXPECT_EQ(reported(47.3f, 2.3f), MatchClock::minuteLabel(47.3f, 1, true));
  EXPECT_EQ(reported(94.8f, 4.8f), MatchClock::minuteLabel(94.8f, 2, true));
  EXPECT_EQ(reported(99.2f, 0.0f), MatchClock::minuteLabel(99.2f, 3, false));
  EXPECT_EQ(reported(121.5f, 1.5f), MatchClock::minuteLabel(121.5f, 4, true));
}

TEST_F(GameFlowTest, ManagedMatchIntegration)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  const TeamID managedId = controller->getTeams().front().get().getId();
  controller->selectManagedTeam(managedId);
  const auto fixture = nextFixtureOf(*controller, managedId);
  ASSERT_TRUE(fixture.has_value());
  const bool managedHome = fixture->getHomeTeamId() == managedId;
  const Lineup& lineup = controller->getManagedTeam()->get().getLineup();
  ASSERT_GE(lineup.getOutfieldPlayers().size(), 2u);
  const PlayerID injuredId =
      lineup.getOutfieldPlayers().front().player->getId();
  const PlayerID tiredId = lineup.getOutfieldPlayers().back().player->getId();
  worldPlayer(*controller, injuredId).mutableDynamics().injury_days = 12;
  worldPlayer(*controller, tiredId).mutableDynamics().condition = 70.0f;

  // The scene logic runs headless: no window is needed to play the match.
  GUIView view(*controller);

  // When the manager decides, an injured starter blocks kick-off; the
  // check suggests the assistant's replacement.
  controller->setAssistantFixesLineup(false);
  {
    MatchScene blocked(&view, fixture->getHomeTeamId(),
                       fixture->getAwayTeamId());
    blocked.onEnter();
    EXPECT_EQ(blocked.engine, nullptr);
    const auto problem =
        std::ranges::find_if(blocked.lineup_problems, [&](const auto& entry)
                             { return entry.id == injuredId; });
    ASSERT_NE(problem, blocked.lineup_problems.end());
    EXPECT_FALSE(problem->replacement.empty());
  }
  // By default the assistant replaces him and the match starts.
  controller->setAssistantFixesLineup(true);
  auto sceneOwner = std::make_unique<MatchScene>(
      &view, fixture->getHomeTeamId(), fixture->getAwayTeamId());
  MatchScene* scene = sceneOwner.get();
  scene->onEnter();
  ASSERT_NE(scene->engine, nullptr);
  EXPECT_TRUE(scene->lineup_problems.empty());
  EXPECT_FALSE(scene->pre_match_note.empty());
  EXPECT_TRUE(
      controller->getIneligibleSelections(managedId, fixture->getMatchType())
          .empty());
  MatchEngine& engine = *scene->engine;
  for (const MatchPlayer& player : engine.getPlayers())
    EXPECT_NE(player.player->getId(), injuredId);

  // Persistent fatigue is carried into the match.
  const auto tired = engine.getPlayerCondition(tiredId);
  ASSERT_TRUE(tired.has_value());
  EXPECT_NEAR(*tired, 0.70f, 1e-4f);

  // The clock shows first-half added time as 45+N.
  for (int step = 0; step < 200000 && !engine.isInAddedTime() &&
                     engine.getState() != MatchState::HALF_TIME;
       ++step)
    engine.update(0.05f);
  ASSERT_TRUE(engine.isInAddedTime()) << "every half has added time";
  EXPECT_TRUE(scene->clockText().starts_with("45+")) << scene->clockText();

  // Manual changes follow the engine's rules and never touch the saved
  // lineup: five are allowed, the sixth is refused with the reason.
  const std::vector<const Player*> savedReserves = lineup.getReserves();
  std::vector<PlayerID> outgoing;
  for (const MatchPlayer& player : engine.getPlayers())
    if (player.isHomeTeam == managedHome && player.onPitch &&
        !player.isGoalkeeper)
      outgoing.push_back(player.player->getId());
  std::vector<PlayerID> incoming;
  for (const Player* reserve : lineup.getReserves())
    if (reserve->getRole() != PlayerRole::GK)
      incoming.push_back(reserve->getId());
  ASSERT_GE(outgoing.size(), 6u);
  ASSERT_GE(incoming.size(), 5u);
  scene->is_paused = true;
  EXPECT_FALSE(scene->substitute(outgoing[0], injuredId));
  EXPECT_TRUE(scene->substitution_refused);
  int made = 0;
  for (size_t index = 0; index < 5; ++index)
    made += scene->substitute(outgoing[index], incoming[index]) ? 1 : 0;
  EXPECT_EQ(made, 5);
  EXPECT_EQ(engine.getSubstitutionsUsed(managedHome), 5);
  EXPECT_FALSE(scene->substitute(outgoing[5], incoming[0]));
  EXPECT_EQ(scene->substitution_status,
            fmt::sprintf(LOC("SUBSTITUTION_REFUSED_LIMIT"),
                         MatchTuning::Rules::MAX_SUBSTITUTIONS_PER_TEAM));
  EXPECT_EQ(lineup.getReserves(), savedReserves);
  scene->is_paused = false;

  // Only the opponent's AI substitutes; the managed side's changes are ours.
  playToFullTime(engine);
  scene->update(0.0f);
  ASSERT_TRUE(scene->match_finished);
  for (const MatchSubstitution& change : engine.getSubstitutions())
    if (change.isHomeTeam == managedHome)
      EXPECT_EQ(change.reason, SubstitutionReason::MANUAL);
  EXPECT_GT(engine.getSubstitutionsUsed(!managedHome), 0);

  // The managed match is recorded with the full engine report.
  const GameDateValue date = controller->getCurrentDate();
  const size_t engineLines = engine.getPlayerStats().size();
  ASSERT_TRUE(scene->finishMatch());
  const auto report = controller->getMatchReport(date, fixture->getHomeTeamId(),
                                                 fixture->getAwayTeamId());
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->players.size(), engineLines);
  EXPECT_GT(
      report->home_stats.passes_attempted + report->away_stats.passes_attempted,
      0);
  EXPECT_TRUE(std::ranges::none_of(report->players,
                                   [&](const PlayerMatchLine& line)
                                   { return line.player_id == injuredId; }));
}

TEST_F(GameFlowTest, LiveMatchConsequencesUseEngineMeasurements)
{
  const TeamID managedId = controller->getTeams().front().get().getId();
  controller->selectManagedTeam(managedId);
  const auto fixture = nextFixtureOf(*controller, managedId);
  ASSERT_TRUE(fixture.has_value());
  controller->autoFixLineup(managedId, fixture->getMatchType());
  const Team& home = controller->getTeamById(fixture->getHomeTeamId())->get();
  const Team& away = controller->getTeamById(fixture->getAwayTeamId())->get();
  MatchEngine engine(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), controller->getStatsConfig(), 77u);
  MatchdaySquad::carryCondition(engine, home.getLineup());
  MatchdaySquad::carryCondition(engine, away.getLineup());
  playToFullTime(engine);
  const auto consequences = MatchdaySquad::consequences(engine);
  ASSERT_GE(consequences.size(), 22u);

  ASSERT_TRUE(controller->setMatchResult(controller->getCurrentDate(),
                                         fixture->getHomeTeamId(),
                                         fixture->getAwayTeamId(), engine));
  // Condition and injuries are the engine's, for both sides, exactly as for
  // simulated matches (not the estimated drain).
  for (const PlayerMatchConsequence& consequence : consequences)
  {
    const Player& player = worldPlayer(*controller, consequence.player_id);
    EXPECT_NEAR(player.getDynamics().condition, consequence.end_condition,
                1e-3f)
        << player.getName();
    if (consequence.injured) EXPECT_FALSE(player.isAvailable());
  }
}

TEST_F(GameFlowTest, MissedManagedFixtureFieldsOnlyEligiblePlayers)
{
  const TeamID managedId = controller->getTeams().front().get().getId();
  controller->selectManagedTeam(managedId);
  // The manager keeps his own selection between matches.
  controller->setAssistantFixesLineup(false);
  const auto fixture = nextFixtureOf(*controller, managedId);
  ASSERT_TRUE(fixture.has_value());
  const Lineup& lineup = controller->getManagedTeam()->get().getLineup();
  const PlayerID injuredId =
      lineup.getOutfieldPlayers().front().player->getId();
  worldPlayer(*controller, injuredId).mutableDynamics().injury_days = 12;

  // Skipping the match day lets the assistant play the fixture.
  controller->advanceDay();
  const auto report = controller->getMatchReport(
      fixture->getDate(), fixture->getHomeTeamId(), fixture->getAwayTeamId());
  ASSERT_TRUE(report.has_value());
  EXPECT_FALSE(report->players.empty());
  for (const PlayerMatchLine& line : report->players)
    EXPECT_NE(line.player_id, injuredId);
  // The manager's own selection is kept.
  EXPECT_EQ(lineup.getOutfieldPlayers().front().player->getId(), injuredId);
}

TEST_F(GameFlowTest, WatchedCupTieIsPlayedToAWinner)
{
  // A cup tie opened on the match screen gets the knockout rules and the
  // clubs' names; level after 90 minutes the clock runs on into extra time
  // and, still level, shows the shootout score.
  ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::EN));
  const TeamID managedId = controller->getTeams().front().get().getId();
  controller->selectManagedTeam(managedId);
  const GameDateValue today = controller->getCurrentDate();
  const auto busyToday = [&](TeamID team_id)
  {
    return std::ranges::any_of(controller->getTeamFixtures(team_id),
                               [&](const Match& match)
                               { return match.getDate() == today; });
  };
  ASSERT_FALSE(busyToday(managedId));
  const std::string homeName =
      controller->getTeamById(managedId)->get().getName();
  GUIView view(*controller);
  bool extraTime = false;
  int tries = 0;
  for (const auto& team : controller->getTeams())
  {
    const TeamID opponent = team.get().getId();
    if (opponent == managedId || opponent == FREE_AGENTS_TEAM_ID ||
        busyToday(opponent))
      continue;
    if (++tries > 40) break;
    controller->getGame()->getCalendar().addMatch(
        Match(managedId, opponent, today, MatchType::CUP));
    MatchScene scene(&view, managedId, opponent);
    scene.onEnter();
    ASSERT_NE(scene.engine, nullptr);
    MatchEngine& engine = *scene.engine;
    ASSERT_TRUE(engine.getKnockout().required);
    for (int step = 0;
         step < 400000 && engine.getState() != MatchState::FULL_TIME &&
         engine.getPeriod() < 3;
         ++step)
      engine.update(0.05f);
    // Commentary names the clubs, not "home side".
    EXPECT_TRUE(
        std::ranges::any_of(engine.getEvents(), [&](const MatchEvent& event)
                            { return event.description.contains(homeName); }))
        << homeName;
    if (engine.getState() == MatchState::FULL_TIME) continue;
    extraTime = true;
    EXPECT_TRUE(engine.wentToExtraTime());
    const std::string clock = scene.clockText();
    EXPECT_TRUE(clock.starts_with("9") || clock.starts_with("10")) << clock;
    for (int step = 0;
         step < 400000 && engine.getState() != MatchState::FULL_TIME &&
         !engine.hasShootout();
         ++step)
      engine.update(0.05f);
    if (engine.hasShootout())
    {
      EXPECT_EQ(scene.clockText(),
                std::format("{}-{}", engine.getShootoutScore(true),
                            engine.getShootoutScore(false)));
      engine.simulateToEnd();
    }
    ASSERT_EQ(engine.getState(), MatchState::FULL_TIME);
    EXPECT_TRUE(engine.getTieWinnerHome().has_value());
    EXPECT_EQ(scene.clockText(), LOC("MATCH_CLOCK_FULL_TIME"));
    break;
  }
  EXPECT_TRUE(extraTime) << "no cup tie went to extra time";
}

TEST_F(GameFlowTest, WatchedMatchSeedIsDeterministic)
{
  // Without the test override the seed comes from the world and fixture.
  const char* configured = std::getenv("FM_MATCH_SEED");
  const std::optional<std::string> savedSeed =
      configured ? std::optional<std::string>(configured) : std::nullopt;
  unsetenv("FM_MATCH_SEED");

  const TeamID managedId = controller->getTeams().front().get().getId();
  controller->selectManagedTeam(managedId);
  const auto fixture = nextFixtureOf(*controller, managedId);
  ASSERT_TRUE(fixture.has_value());
  controller->autoFixLineup(managedId, fixture->getMatchType());
  GUIView view(*controller);
  MatchScene first(&view, fixture->getHomeTeamId(), fixture->getAwayTeamId());
  MatchScene second(&view, fixture->getHomeTeamId(), fixture->getAwayTeamId());
  first.onEnter();
  second.onEnter();
  ASSERT_NE(first.engine, nullptr);
  ASSERT_NE(second.engine, nullptr);
  for (int step = 0; step < 600; ++step)
  {
    first.engine->update(0.05f);
    second.engine->update(0.05f);
  }
  EXPECT_EQ(first.engine->getEvents().size(),
            second.engine->getEvents().size());
  EXPECT_EQ(first.engine->getBall().position.x,
            second.engine->getBall().position.x);
  EXPECT_EQ(first.engine->getBall().position.y,
            second.engine->getBall().position.y);

  if (savedSeed) setenv("FM_MATCH_SEED", savedSeed->c_str(), 1);
}

TEST_F(GameFlowTest, AssistantKeepsSelectionEligibleBetweenMatches)
{
  const TeamID managedId = controller->getTeams().front().get().getId();
  controller->selectManagedTeam(managedId);
  ASSERT_TRUE(controller->getAssistantFixesLineup());
  const Lineup& lineup = controller->getManagedTeam()->get().getLineup();
  const PlayerID injuredId =
      lineup.getOutfieldPlayers().front().player->getId();
  worldPlayer(*controller, injuredId).mutableDynamics().injury_days = 20;

  // The next day the assistant has replaced him in the saved lineup.
  controller->advanceDay();
  EXPECT_TRUE(controller->getUnavailableLineupPlayers(managedId).empty());
  EXPECT_NE(lineup.getOutfieldPlayers().front().player->getId(), injuredId);
}

TEST_F(GameFlowTest, PlaybackModeAndSpeedNeverChangeTheResult)
{
  const TeamID managedId = controller->getTeams().front().get().getId();
  controller->selectManagedTeam(managedId);
  const auto fixture = nextFixtureOf(*controller, managedId);
  ASSERT_TRUE(fixture.has_value());
  const Team& home = controller->getTeamById(fixture->getHomeTeamId())->get();
  const Team& away = controller->getTeamById(fixture->getAwayTeamId())->get();
  const auto makeEngine = [&]
  {
    return MatchEngine(home.getLineup(), away.getLineup(), home.getStrategy(),
                       away.getStrategy(), controller->getStatsConfig(), 2024u);
  };

  // Reference: the headless fast path (what Quick result uses).
  MatchEngine reference = makeEngine();
  const auto startedAt = std::chrono::steady_clock::now();
  reference.simulateToEnd();
  RecordProperty(
      "simulate_to_end_milliseconds",
      static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - startedAt)
                           .count()));

  // Highlights only, and the full match with the speed changed repeatedly.
  MatchEngine highlights = makeEngine();
  highlights.setPlaybackMode(MatchPlaybackMode::HIGHLIGHTS);
  highlights.setHighlightPlaybackSpeed(30.0f);
  MatchEngine full = makeEngine();
  full.setPlaybackMode(MatchPlaybackMode::FULL_MATCH);
  constexpr std::array<float, 4> SPEEDS = {30.0f, 7.0f, 16.0f, 1.0f};
  for (int frame = 0;
       frame < 2'000'000 && (highlights.getState() != MatchState::FULL_TIME ||
                             full.getState() != MatchState::FULL_TIME);
       ++frame)
  {
    highlights.advancePlayback(1.0f / 60.0f);
    full.setPlaybackSpeed(
        SPEEDS[static_cast<size_t>(frame / 500) % SPEEDS.size()]);
    full.advancePlayback(1.0f / 60.0f);
  }
  for (const MatchEngine* played : {&highlights, &full})
  {
    ASSERT_EQ(played->getState(), MatchState::FULL_TIME);
    EXPECT_EQ(played->getHomeScore(), reference.getHomeScore());
    EXPECT_EQ(played->getAwayScore(), reference.getAwayScore());
    EXPECT_EQ(played->getEvents().size(), reference.getEvents().size());
    EXPECT_EQ(played->getStats().homeShots, reference.getStats().homeShots);
    EXPECT_EQ(played->getStats().awayShots, reference.getStats().awayShots);
    EXPECT_EQ(played->getStats().homePassesCompleted,
              reference.getStats().homePassesCompleted);
  }
  EXPECT_EQ(highlights.getDroppedSimulationSteps(), 0u);
  EXPECT_EQ(full.getDroppedSimulationSteps(), 0u);
}

TEST_F(GameFlowTest, StopOnTheProgressCardEndsTheHolidayAfterTheDay)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  controller->selectManagedTeam(controller->getTeams().front().get().getId());
  GUIView view(*controller);
  ASSERT_TRUE(initializeView(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  hubFrame(view);
  hubFrame(view);
  auto* hub = dynamic_cast<MainGameScene*>(view.getBaseScene());
  ASSERT_NE(hub, nullptr);
  const GameDateValue target = startLongHoliday(view, *hub, *controller);
  ASSERT_TRUE(hub->isAdvancing());
  hub->requestStop();
  EXPECT_TRUE(controller->isContinueStopRequested());
  for (int frame = 0; frame < 4000 && hub->isAdvancing(); ++frame)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    hubFrame(view);
  }
  ASSERT_FALSE(hub->isAdvancing());
  EXPECT_EQ(controller->getHolidaySummary().reason, HolidayStop::Interrupted);
  EXPECT_LT(dayOrdinal(controller->getCurrentDate()) + 60, dayOrdinal(target));
}

TEST_F(GameFlowTest, ClosingTheWindowEndsAHolidayAfterTheCurrentDay)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  controller->selectManagedTeam(controller->getTeams().front().get().getId());
  const GameDateValue start = controller->getCurrentDate();
  GameDateValue target;
  {
    GUIView view(*controller);
    ASSERT_TRUE(initializeView(view));
    view.changeScene(std::make_unique<MainGameScene>(&view));
    hubFrame(view);
    hubFrame(view);
    auto* hub = dynamic_cast<MainGameScene*>(view.getBaseScene());
    ASSERT_NE(hub, nullptr);
    target = startLongHoliday(view, *hub, *controller);
    ASSERT_TRUE(hub->isAdvancing());
  }
  // The window closed mid-holiday: the days in flight were completed, the
  // rest of the holiday was not simulated, and the career saves cleanly.
  EXPECT_LT(start, controller->getCurrentDate());
  EXPECT_LT(dayOrdinal(controller->getCurrentDate()) + 60, dayOrdinal(target));
  EXPECT_EQ(controller->getHolidaySummary().reason, HolidayStop::Interrupted);
  EXPECT_TRUE(controller->saveGame());
}

TEST_F(GameFlowTest, TheSeasonSummaryOpensOnceOnTheHub)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  const TeamID managed = controller->getTeams().front().get().getId();
  controller->selectManagedTeam(managed);
  SeasonReview review;
  review.season = 1;
  review.start_year = 2025;
  review.team_id = managed;
  review.league_id = controller->getTeamById(managed)->get().getLeagueId();
  review.position = 2;
  review.league_size = 20;
  review.promoted = true;
  review.next_league_id = review.league_id;
  review.top_scorer = "Test Scorer";
  review.top_scorer_goals = 21;
  review.result.verdict = SeasonVerdict::Delighted;
  review.next_objective = BoardObjective::MidTable;
  controller->getGame()->getSeasonArchive().recordReview(review);

  GUIView view(*controller);
  ASSERT_TRUE(initializeView(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  auto* hub = dynamic_cast<MainGameScene*>(view.getBaseScene());
  for (int frame = 0; frame < 4; ++frame)
  {
    hubFrame(view);
    hub = dynamic_cast<MainGameScene*>(view.getBaseScene());
    EXPECT_EQ(ImGui::GetCurrentContext()->ErrorCountCurrentFrame, 0);
  }
  ASSERT_NE(hub, nullptr);
  EXPECT_TRUE(hub->isShowingSeasonReview());
  const auto capture = RuntimePaths::capturePath("season_review.bmp");
  std::filesystem::remove(capture);
  EXPECT_TRUE(view.captureScreenshot(capture.string()));

  ImGuiIO& io = ImGui::GetIO();
  io.AddKeyEvent(ImGuiKey_Escape, true);
  hubFrame(view);
  io.AddKeyEvent(ImGuiKey_Escape, false);
  for (int frame = 0; frame < 4; ++frame) hubFrame(view);
  hub = dynamic_cast<MainGameScene*>(view.getBaseScene());
  ASSERT_NE(hub, nullptr);
  EXPECT_FALSE(hub->isShowingSeasonReview());
  EXPECT_EQ(GImGui->OpenPopupStack.Size, 0) << "Escape opened another dialog";
  EXPECT_EQ(controller->getUnseenSeasonReview(), nullptr)
      << "closing the summary marks it seen";
}

#if defined(__linux__)
// Runs in child processes of the test below; harmless on its own.
TEST(RuntimeRoot, ChildHelperWritesACapture)
{
  const auto path = RuntimePaths::capturePath("cleanup_probe.txt");
  std::ofstream(path) << "probe";
  EXPECT_TRUE(std::filesystem::exists(path));
}

TEST(RuntimeRoot, TestProcessesRemoveTheirRootAtExit)
{
  const auto base =
      RuntimePaths::root() / std::format("fm-root-probe-{}", getpid());
  std::filesystem::remove_all(base);
  const std::string exe = std::filesystem::read_symlink("/proc/self/exe");
  const auto runChild = [&](bool keep, bool succeeds = true)
  {
    std::vector<std::string> variables;
    for (char** entry = environ; *entry != nullptr; ++entry)
    {
      const std::string_view variable(*entry);
      if (variable.starts_with("FM_TEST_RUNTIME_ROOT=") ||
          variable.starts_with("FM_KEEP_TEST_ARTIFACTS="))
        continue;
      variables.emplace_back(variable);
    }
    variables.push_back("FM_TEST_RUNTIME_ROOT=" + base.string());
    if (keep) variables.emplace_back("FM_KEEP_TEST_ARTIFACTS=1");
    std::vector<char*> envp;
    for (std::string& variable : variables) envp.push_back(variable.data());
    envp.push_back(nullptr);
    std::string program = exe;
    std::string filter = "--gtest_filter=RuntimeRoot.ChildHelperWritesACapture";
    std::array<char*, 3> argv = {program.data(), filter.data(), nullptr};
    pid_t child = 0;
    ASSERT_EQ(posix_spawn(&child, exe.c_str(), nullptr, nullptr, argv.data(),
                          envp.data()),
              0);
    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), succeeds ? 0 : 1);
  };

  // A test process leaves nothing behind...
  runChild(false);
  ASSERT_TRUE(std::filesystem::exists(base));
  EXPECT_EQ(std::filesystem::status(base).permissions() &
                (std::filesystem::perms::group_all |
                 std::filesystem::perms::others_all),
            std::filesystem::perms::none);
  EXPECT_TRUE(std::filesystem::is_empty(base));
  // ...unless its artifacts were asked for.
  runChild(true);
  EXPECT_FALSE(std::filesystem::is_empty(base));
  std::filesystem::remove_all(base);

  // A planted symlink must not redirect captures into another directory.
  const auto outside = base.string() + "-outside";
  std::filesystem::create_directory(outside);
  std::filesystem::create_directory_symlink(outside, base);
  runChild(false, false);
  EXPECT_TRUE(std::filesystem::is_empty(outside));
  std::filesystem::remove(base);
  std::filesystem::remove(outside);
}
#endif
