// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The match report's Analysis tab and the data hub's season analytics
// through the real GUIView at 1280x720, a narrow window and 2560x1440 at UI
// scale 2: every chart is hoverable without ImGui errors, the page is the
// only scroll surface and nothing scrolls sideways. Screenshots land in the
// test runtime's captures folder (FM_KEEP_TEST_ARTIFACTS=1 keeps them).

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string_view>

#include "controller/game_controller.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/data_hub_scene.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/match_report_scene.h"
#include "model/match_engine.h"
#include "model/settings_manager.h"

/** GUI classes grant their internals to this name (see test_game_flow). */
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
  static void showAnalysis(MatchReportScene& scene) { scene.tab = 1; }
  static bool analysisAvailable(const MatchReportScene& scene)
  {
    return scene.insights.available();
  }
  static bool hasDetail(const MatchReportScene& scene)
  {
    return scene.insights.has_detail;
  }
  static std::size_t summaryLines(const MatchReportScene& scene)
  {
    return scene.insights.summary.size();
  }
  static std::size_t moments(const MatchReportScene& scene)
  {
    return scene.insights.moments.size();
  }
  static std::size_t leaders(const DataHubScene& scene)
  {
    std::size_t total = 0;
    for (const auto& rows : scene.leaders) total += rows.size();
    return total;
  }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 470'000 + (getpid() % 100'000) * 10 + offset;
}

struct SlotCleanup
{
  int slot;
  explicit SlotCleanup(int slotValue) : slot(slotValue) {}
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;
};

void capture(GUIView& view, const char* name)
{
  const auto path = RuntimePaths::capturePath(name);
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

void frames(GUIView& view, int count)
{
  for (int index = 0; index < count; ++index) Bridge::frame(view);
}

void setUiScale(GUIView& view, float scale)
{
  SettingsManager::instance()->get().ui_scale = scale;
  view.refreshTheme();
}

/** Hovers the whole window on a grid, one frame per position. */
void hoverEverywhere(GUIView& view, float step)
{
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 size = io.DisplaySize;
  for (int y_index = 0; step * (static_cast<float>(y_index) + 0.5f) < size.y;
       ++y_index)
    for (int x_index = 0; step * (static_cast<float>(x_index) + 0.5f) < size.x;
         ++x_index)
    {
      const float y = step * (static_cast<float>(y_index) + 0.5f);
      const float x = step * (static_cast<float>(x_index) + 0.5f);
      io.AddMousePosEvent(x, y);
      Bridge::frame(view);
    }
  io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  Bridge::frame(view);
}

/** Scrolls the page to a share of its height (0 top, 1 bottom). */
void scrollPage(GUIView& view, float share)
{
  for (ImGuiWindow* window : GImGui->Windows)
    if (window->Active && window->ParentWindow != nullptr &&
        std::string_view(window->ParentWindow->Name) == "##management_shell" &&
        std::string_view(window->Name).contains("##content"))
      ImGui::SetScrollY(window, window->ScrollMax.y * share);
  frames(view, 2);
}

/** Scrolls the page from top to bottom, hovering each screenful. */
void hoverWholePage(GUIView& view, float step)
{
  for (int page = 0; page < 12; ++page)
  {
    hoverEverywhere(view, step);
    ImGuiWindow* content = nullptr;
    for (ImGuiWindow* window : GImGui->Windows)
      if (window->Active &&
          std::string_view(window->Name).contains("##content") &&
          window->ParentWindow != nullptr &&
          std::string_view(window->ParentWindow->Name) == "##management_shell")
        content = window;
    if (content == nullptr || content->Scroll.y >= content->ScrollMax.y) break;
    ImGui::SetScrollY(content, content->Scroll.y + content->Size.y * 0.8f);
    frames(view, 2);
  }
}

/**
 * The page scrolls only vertically, no card scrolls on its own and nothing
 * spills out of a card sideways (a few pixels of item spacing aside).
 */
void expectSingleScrollSurface(std::string_view prefix)
{
  const float tolerance = 16.0f * ImGui::GetIO().FontGlobalScale *
                          SettingsManager::instance()->get().ui_scale;
  for (const ImGuiWindow* window : GImGui->Windows)
  {
    if (!window->Active || window->RootWindow == nullptr) continue;
    if (const std::string_view root(window->RootWindow->Name);
        root != "##management_shell")
      continue;
    const std::string_view name(window->Name);
    EXPECT_FALSE(window->ScrollbarX) << "horizontal scrollbar in " << name;
    const bool page = name.contains("##content") &&
                      window->ParentWindow == window->RootWindow;
    if (const bool sidebar = name.contains("##sidebar"); page || sidebar)
      continue;
    EXPECT_FALSE(window->ScrollbarY) << "nested scrollbar in " << name;
    // The new analytics cards must not clip their content either.
    if (name.contains(prefix))
      EXPECT_LE(window->ScrollMax.x, std::max(tolerance, 16.0f))
          << "content wider than its card: " << name;
  }
}

struct PlayedMatch
{
  GameDateValue date;
  TeamID home = 0;
  TeamID away = 0;
};

/** Plays the managed club's next matches like a watched match (tracked). */
int playManagedMatches(GameController& controller, int count, PlayedMatch& last)
{
  int played = 0;
  const TeamID club = controller.getManagedTeam()->get().getId();
  for (int attempt = 0; attempt < count * 3 && played < count; ++attempt)
  {
    controller.advanceToNextManagedFixture(60);
    const auto fixture = controller.getNextManagedFixture();
    if (!fixture || !(fixture->date == controller.getCurrentDate())) continue;
    const TeamID home = fixture->home ? club : fixture->opponent;
    const TeamID away = fixture->home ? fixture->opponent : club;
    controller.autoFixLineup(club, fixture->type);
    const Team& home_team = controller.getTeamById(home)->get();
    const Team& away_team = controller.getTeamById(away)->get();
    MatchEngine engine(home_team.getLineup(), away_team.getLineup(),
                       home_team.getStrategy(), away_team.getStrategy(),
                       controller.getStatsConfig(),
                       static_cast<std::uint32_t>(2000 + played));
    engine.simulateToEnd();
    if (!controller.setMatchResult(controller.getCurrentDate(), home, away,
                                   engine))
      break;
    last = {controller.getCurrentDate(), home, away};
    controller.advanceDay();
    ++played;
  }
  return played;
}
}  // namespace

TEST(MatchInsightsUiTest, AnalysisAndSeasonAnalyticsAtEverySize)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(0)};
  SettingsManager::instance()->get().screen_tips = false;
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  controller.selectManagedTeam(controller.getTeams().front().get().getId());

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  resize(view, 1280, 720);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  PlayedMatch last;
  ASSERT_GE(playManagedMatches(controller, 6, last), 4);

  Navigation::openMatchReport(&view, last.date, last.home, last.away);
  frames(view, 3);
  auto* report = dynamic_cast<MatchReportScene*>(Bridge::activeScene(view));
  ASSERT_NE(report, nullptr);
  ASSERT_TRUE(Bridge::analysisAvailable(*report));
  EXPECT_TRUE(Bridge::hasDetail(*report));
  EXPECT_GE(Bridge::summaryLines(*report), 1U);
  EXPECT_GE(Bridge::moments(*report), 1U);
  capture(view, "insights_report_overview.bmp");
  Bridge::showAnalysis(*report);
  frames(view, 3);
  capture(view, "insights_report_analysis.bmp");
  expectSingleScrollSurface("/insight_");
  scrollPage(view, 0.45f);
  capture(view, "insights_report_analysis_middle.bmp");
  scrollPage(view, 0.0f);
  hoverWholePage(view, 120.0f);
  capture(view, "insights_report_analysis_bottom.bmp");
  expectSingleScrollSurface("/insight_");

  // Narrow: one column, one pass network at a time.
  resize(view, 900, 700);
  frames(view, 5);
  scrollPage(view, 0.0f);
  capture(view, "insights_report_narrow.bmp");
  expectSingleScrollSurface("/insight_");
  hoverWholePage(view, 128.0f);
  capture(view, "insights_report_narrow_bottom.bmp");

  // HiDPI desktop at scale 2.
  resize(view, 2560, 1440);
  setUiScale(view, 2.0f);
  frames(view, 5);
  scrollPage(view, 0.0f);
  capture(view, "insights_report_hidpi.bmp");
  expectSingleScrollSurface("/insight_");
  scrollPage(view, 0.5f);
  capture(view, "insights_report_hidpi_middle.bmp");
  scrollPage(view, 0.0f);
  hoverWholePage(view, 360.0f);
  capture(view, "insights_report_hidpi_bottom.bmp");
  setUiScale(view, 1.0f);
  resize(view, 1280, 720);
  frames(view, 3);

  Navigation::open(&view, NavSection::DATA_HUB);
  frames(view, 3);
  auto* hub = dynamic_cast<DataHubScene*>(Bridge::activeScene(view));
  ASSERT_NE(hub, nullptr);
  EXPECT_GT(Bridge::leaders(*hub), 0U);
  capture(view, "insights_hub_team.bmp");
  expectSingleScrollSurface("/hub_");
  hoverWholePage(view, 120.0f);
  capture(view, "insights_hub_team_bottom.bmp");
  expectSingleScrollSurface("/hub_");
  resize(view, 2560, 1440);
  setUiScale(view, 2.0f);
  frames(view, 5);
  scrollPage(view, 1.0f);
  capture(view, "insights_hub_hidpi.bmp");
  expectSingleScrollSurface("/hub_");
  setUiScale(view, 1.0f);
  resize(view, 1280, 720);
  frames(view, 2);
}
