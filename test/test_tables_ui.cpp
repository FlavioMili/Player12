// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Big tables through the real GUIView: saved column views (picker choices
// survive a settings reload, never come back on wide windows, must-stay
// columns cannot be hidden) and the league table run-in (clinch badges and
// the "what you need" line) at 1280x720 and 2560x1440 with scale 2.
// Screenshots land in the test runtime's captures folder
// (FM_KEEP_TEST_ARTIFACTS=1 keeps them).

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/roster_scene.h"
#include "gui/scenes/scouting_scene.h"
#include "gui/scenes/standings_scene.h"
#include "gui/widgets/widgets.h"
#include "model/game.h"
#include "model/settings_manager.h"

/**
 * The GUI classes grant their internals to this name (the lifecycle test of
 * test_game_flow.cpp, not linked into this executable).
 */
class GameFlowTest_GUIFlowLifecycle_Test
{
 public:
  static bool initialize(GUIView& view) { return view.initialize(); }
  static void frame(GUIView& view)
  {
    view.applyPendingSceneChanges();
    view.handleEvents();
    view.update(0.016f);
    view.render();
    EXPECT_EQ(ImGui::GetCurrentContext()->ErrorCountCurrentFrame, 0)
        << "ImGui reported a usage error";
  }
  static GUIScene* activeScene(const GUIView& view)
  {
    return view.getActiveScene();
  }
  static UI::ColumnMask mask(const StandingsScene& scene)
  {
    return scene.table_mask;
  }
  static UI::ColumnMask mask(const RosterScene& scene)
  {
    return scene.table_mask;
  }
  static UI::ColumnMask mask(const ScoutingScene& scene)
  {
    return scene.player_table_mask;
  }
  static const std::vector<Standings::Clinch>& clinch(
      const StandingsScene& scene)
  {
    return scene.clinch;
  }
  static const std::vector<std::string>& needs(const StandingsScene& scene)
  {
    return scene.need_lines;
  }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 820'000 + (getpid() % 100'000) * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

/** Restores the user's settings (memory and file) when a test ends. */
struct SettingsRestore
{
  SettingsRestore() = default;
  SettingsRestore(const SettingsRestore&) = delete;
  SettingsRestore& operator=(const SettingsRestore&) = delete;

  Settings original = SettingsManager::instance()->get();
  ~SettingsRestore()
  {
    try
    {
      SettingsManager::instance()->get() = original;
      SettingsManager::instance()->save();
    }
    catch (const std::exception& error)
    {
      ADD_FAILURE() << "Could not restore test settings: " << error.what();
    }
  }
};

void capture(GUIView& view, const std::string& name)
{
  const auto path = RuntimePaths::capturePath(name.c_str());
  std::filesystem::remove(path);
  EXPECT_TRUE(view.captureScreenshot(path.string())) << name;
  EXPECT_TRUE(std::filesystem::exists(path)) << name;
}

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

void setUiScale(GUIView& view, float scale)
{
  SettingsManager::instance()->get().ui_scale = scale;
  view.refreshTheme();
}

void frames(GUIView& view, int count)
{
  for (int index = 0; index < count; ++index) Bridge::frame(view);
}

template <typename Scene>
Scene* active(GUIView& view)
{
  return dynamic_cast<Scene*>(Bridge::activeScene(view));
}

/** The page scrolls only vertically (no table scrolls sideways). */
void expectNoHorizontalScroll(const char* where)
{
  for (const ImGuiWindow* window : GImGui->Windows)
  {
    if (!window->Active || window->RootWindow == nullptr) continue;
    if (std::string_view(window->RootWindow->Name) != "##management_shell")
      continue;
    EXPECT_LE(window->ScrollMax.x, 0.5f)
        << "horizontal scroll in " << window->Name << " at " << where;
  }
}

constexpr UI::ColumnMask bit(int index)
{
  return UI::ColumnMask{1} << static_cast<unsigned>(index);
}

/**
 * Plays every league round of the managed club's league except the last
 * `left` with seeded scores (stronger results for lower team IDs), so the
 * table reaches its run-in without simulating a season.
 */
void playLeagueUntil(GameController& controller, LeagueID league, int left)
{
  Calendar& calendar = controller.getGame()->getCalendar();
  int last_round = 0;
  for (const auto& [date, matches] : calendar.getFullCalendar())
    for (const Match& match : matches)
      if (match.getMatchType() == MatchType::LEAGUE &&
          match.getCompetitionId() == league)
        last_round = std::max<int>(last_round, match.getStage());
  std::mt19937 rng(4242);
  std::poisson_distribution<int> goals(1.2);
  std::map<TeamID, int> strength;
  int rank = 0;
  for (const auto& team : controller.getTeams())
    if (team.get().getLeagueId() == league)
      strength[team.get().getId()] = rank++;
  for (const auto& [date, matches] : calendar.getFullCalendar())
  {
    for (Match& match : calendar.getMatchesForDateMutable(date))
    {
      if (match.getMatchType() != MatchType::LEAGUE ||
          match.getCompetitionId() != league || match.isPlayed() ||
          match.getStage() > last_round - left)
        continue;
      const int home_edge = strength[match.getHomeTeamId()] < 6 ? 1 : 0;
      const int away_edge = strength[match.getAwayTeamId()] < 6 ? 1 : 0;
      match.setPlayedResult(
          static_cast<uint8_t>(std::min(6, goals(rng) + home_edge)),
          static_cast<uint8_t>(std::min(6, goals(rng) + away_edge)));
    }
  }
}
}  // namespace

TEST(TablesUiTest, SavedColumnViewsSurviveReloadAndWideWindows)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(0)};
  const SettingsRestore restore;
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  const TeamID club = controller.getTeams().front().get().getId();
  controller.selectManagedTeam(club);

  // Hide goals for/against and wages; "Pts" and the player name are
  // must-stay columns, so hiding them has no effect.
  Settings& settings = SettingsManager::instance()->get();
  settings.hidden_columns.clear();
  settings.hidden_columns["standings"] = {"TABLE_COL_GF", "TABLE_COL_GA",
                                          "MAIN_GAME_PTS"};
  settings.hidden_columns["roster"] = {"ROSTER_COL_WAGE", "ROSTER_COL_NAME"};
  settings.hidden_columns["scouting"] = {"SCOUTING_COL_KNOWLEDGE"};
  SettingsManager::instance()->save();
  settings.hidden_columns.clear();
  SettingsManager::instance()->load();
  ASSERT_EQ(settings.hidden_columns.size(), 3u) << "views reload from disk";
  EXPECT_EQ(settings.hidden_columns["roster"],
            (std::vector<std::string>{"ROSTER_COL_WAGE", "ROSTER_COL_NAME"}));

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);

  // A 4K window has room for every column: the hidden ones still stay out.
  resize(view, 3840, 2160);
  frames(view, 2);
  Navigation::open(&view, NavSection::STANDINGS);
  frames(view, 3);
  auto* standings = active<StandingsScene>(view);
  ASSERT_NE(standings, nullptr);
  const UI::ColumnMask table = Bridge::mask(*standings);
  EXPECT_EQ(table & (bit(6) | bit(7)), 0u) << "GF and GA stay hidden";
  EXPECT_NE(table & bit(9), 0u) << "points can never be hidden";
  EXPECT_NE(table & bit(10), 0u) << "form is on and fits";
  expectNoHorizontalScroll("standings 4K");

  Navigation::open(&view, NavSection::SQUAD);
  frames(view, 3);
  auto* roster = active<RosterScene>(view);
  ASSERT_NE(roster, nullptr);
  const UI::ColumnMask squad = Bridge::mask(*roster);
  EXPECT_EQ(squad & bit(8), 0u) << "wage stays hidden";
  EXPECT_NE(squad & bit(1), 0u) << "the name can never be hidden";
  EXPECT_NE(squad & bit(7), 0u) << "value is on and fits";

  Navigation::open(&view, NavSection::SCOUTING);
  frames(view, 3);
  if (const auto* scouting = active<ScoutingScene>(view))
  {
    frames(view, 3);
    const UI::ColumnMask players = Bridge::mask(*scouting);
    if (players != ~UI::ColumnMask{0})
    {
      EXPECT_EQ(players & bit(6), 0u) << "knowledge stays hidden";
      EXPECT_NE(players & bit(4), 0u) << "ability can never be hidden";
    }
  }

  // Narrow window: responsive logic drops more columns, none come back.
  resize(view, 900, 700);
  Navigation::open(&view, NavSection::STANDINGS);
  frames(view, 3);
  standings = active<StandingsScene>(view);
  ASSERT_NE(standings, nullptr);
  EXPECT_EQ(Bridge::mask(*standings) & (bit(6) | bit(7)), 0u);
  EXPECT_NE(Bridge::mask(*standings) & (bit(0) | bit(1) | bit(9)), 0u);
  expectNoHorizontalScroll("standings 900");

  // The pure helpers agree: hiddenColumns() ignores must-stay columns.
  const std::array<UI::Column, 3> columns = {{
      {"TABLE_COL_GF", 38.0f, 4},
      {"MAIN_GAME_PTS", 44.0f, 0},
      {"TABLE_COL_GA", 38.0f, 4},
  }};
  EXPECT_EQ(UI::hiddenColumns("standings", columns), bit(0) | bit(2));
  EXPECT_EQ(UI::fitColumns(columns, 4000.0f, 160.0f, ~UI::ColumnMask{0}),
            bit(1));
  EXPECT_EQ(UI::hiddenColumns("unknown_table", columns), 0u);
  resize(view, 1280, 720);
  frames(view, 2);
}

TEST(TablesUiTest, StandingsRunInRendersAtEverySize)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(1)};
  const SettingsRestore restore;
  SettingsManager::instance()->get().hidden_columns.clear();
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  const TeamID club = controller.getTeams().front().get().getId();
  controller.selectManagedTeam(club);
  const LeagueID league = controller.getManagedTeam()->get().getLeagueId();
  playLeagueUntil(controller, league, 3);

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  for (const auto& [width, height, scale, tag] :
       {std::tuple{1280, 720, 1.0f, "1280x720"},
        std::tuple{2560, 1440, 2.0f, "2560x1440_scale2"}})
  {
    resize(view, width, height);
    setUiScale(view, scale);
    frames(view, 2);
    Navigation::open(&view, NavSection::STANDINGS);
    frames(view, 4);
    auto* standings = active<StandingsScene>(view);
    ASSERT_NE(standings, nullptr);
    const auto& clinch = Bridge::clinch(*standings);
    const auto decided =
        std::ranges::count_if(clinch, [](Standings::Clinch status)
                              { return status != Standings::Clinch::OPEN; });
    std::cout << tag << ": " << decided << " decided clubs, "
              << Bridge::needs(*standings).size() << " need lines\n";
    for (const std::string& line : Bridge::needs(*standings))
      std::cout << "  " << line << '\n';
    EXPECT_FALSE(clinch.empty());
    expectNoHorizontalScroll(tag);
    capture(view, std::string("tables_standings_") + tag + ".bmp");
  }
  setUiScale(view, 0.0f);
  resize(view, 1280, 720);
  frames(view, 2);
}
