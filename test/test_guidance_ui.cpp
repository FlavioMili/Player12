// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Guidance screens through the real GUIView: Home with the first-week
// checklist and next steps, delegation, opposition report, data hub, the
// inbox decisions and the half-time analysis, at 1280x720, a narrow window
// and UI scale 2. Screenshots land in the test runtime's captures folder
// (FM_KEEP_TEST_ARTIFACTS=1 keeps them).

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unistd.h>

#include <filesystem>
#include <memory>
#include <string_view>

#include "controller/game_controller.h"
#include "global/language_manager.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/data_hub_scene.h"
#include "gui/scenes/inbox_scene.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/match_scene.h"
#include "model/game.h"
#include "model/inbox.h"
#include "model/match_engine.h"
#include "model/settings_manager.h"
#include "model/world_simulation.h"

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
  static void showPlayers(DataHubScene& scene) { scene.tab = 1; }
  static void showUnreadInformation(InboxScene& scene)
  {
    scene.tab = 1;
    scene.unread_only = true;
    scene.rebuildThreads();
  }
  static std::size_t threadCount(const InboxScene& scene)
  {
    return scene.threads.size();
  }
  static std::size_t firstThreadSize(const InboxScene& scene)
  {
    return scene.threads.empty() ? 0 : scene.threads.front().messages.size();
  }
  static size_t trendPoints(const DataHubScene& scene)
  {
    return scene.hub.team.trend.size();
  }
};

/** Live match internals (MatchScene grants them to this name). */
class GameFlowTest_ManagedMatchIntegration_Test
{
 public:
  static MatchEngine* engine(MatchScene& scene) { return scene.engine.get(); }
  static bool analysisOpen(const MatchScene& scene)
  {
    return scene.analysis_panel.isOpen();
  }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;
using MatchBridge = GameFlowTest_ManagedMatchIntegration_Test;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 410'000 + (getpid() % 100'000) * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
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

/** Hovers the whole window on a grid (tooltips, hover states), one frame
 * per position; every frame must be free of ImGui usage errors. */
void hoverEverywhere(GUIView& view, float step = 40.0f)
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

/**
 * True when the match control with this label can be hovered: no modal and
 * no panel sits over it. Scans the band under the scoreboard, where the
 * controls sit, row by row and stops at the first hit.
 */
bool matchControlHoverable(GUIView& view, const char* label)
{
  const ImGuiWindow* match = ImGui::FindWindowByName("MatchScene");
  const ImGuiWindow* scoreboard = nullptr;
  for (const ImGuiWindow* window : GImGui->Windows)
    if (window->Active &&
        std::string_view(window->Name).find("/##match_scoreboard") !=
            std::string_view::npos)
      scoreboard = window;
  if (match == nullptr || scoreboard == nullptr) return false;
  const ImGuiID button = ImHashStr(label, 0, match->ID);
  ImGuiIO& io = ImGui::GetIO();
  const float top = scoreboard->Rect().Max.y;
  const float bottom = top + 3.0f * ImGui::GetFrameHeightWithSpacing();
  bool found = false;
  for (int y_index = 0;
       top + 3.0f + 6.0f * static_cast<float>(y_index) < bottom && !found;
       ++y_index)
    for (int x_index = 0;
         4.0f + 24.0f * static_cast<float>(x_index) < io.DisplaySize.x &&
         !found;
         ++x_index)
    {
      const float y = top + 3.0f + 6.0f * static_cast<float>(y_index);
      const float x = 4.0f + 24.0f * static_cast<float>(x_index);
      io.AddMousePosEvent(x, y);
      Bridge::frame(view);
      found = GImGui->HoveredId == button;
    }
  io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  Bridge::frame(view);
  return found;
}

/** The analysis panel is the window under its own centre (still on top). */
bool analysisPanelOnTop(GUIView& view)
{
  const ImGuiWindow* panel = ImGui::FindWindowByName("##match_analysis");
  if (panel == nullptr || !panel->Active) return false;
  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent(panel->Rect().GetCenter().x, panel->Rect().GetCenter().y);
  Bridge::frame(view);
  const bool onTop = GImGui->HoveredWindow != nullptr &&
                     GImGui->HoveredWindow->RootWindow == panel;
  io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  Bridge::frame(view);
  return onTop;
}

void setUiScale(GUIView& view, float scale)
{
  SettingsManager::instance()->get().ui_scale = scale;
  view.refreshTheme();
}

struct PlayedMatch
{
  GameDateValue date;
  TeamID home = 0;
  TeamID away = 0;
};

/** Plays the managed club's next matches headless, like a quick result. */
int playManagedMatches(GameController& controller, int count,
                       PlayedMatch* last = nullptr)
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
                       static_cast<std::uint32_t>(1000 + played));
    engine.simulateToEnd();
    if (!controller.setMatchResult(controller.getCurrentDate(), home, away,
                                   engine))
      break;
    if (last != nullptr) *last = {controller.getCurrentDate(), home, away};
    controller.advanceDay();
    ++played;
  }
  return played;
}
}  // namespace

TEST(GuidanceUiTest, GuidanceScreensAtEverySize)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(0)};
  Settings& settings = SettingsManager::instance()->get();
  settings.screen_tips = true;
  settings.screen_tips_seen = 0;
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  controller.selectManagedTeam(controller.getTeams().front().get().getId());

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  resize(view, 1280, 720);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 3);
  EXPECT_TRUE(controller.getOnboarding().isVisible());
  capture(view, "guidance_home.bmp");
  // The Home tip was shown once and does not come back.
  EXPECT_NE(settings.screen_tips_seen, 0U);

  Navigation::open(&view, NavSection::TACTICS);
  frames(view, 2);
  EXPECT_TRUE(controller.getOnboarding().isDone(OnboardingTask::ReviewTactics));

  Navigation::open(&view, NavSection::DELEGATION);
  frames(view, 3);
  capture(view, "guidance_delegation.bmp");

  Navigation::open(&view, NavSection::OPPOSITION);
  frames(view, 3);
  capture(view, "guidance_opposition.bmp");
  const auto fixture = controller.getNextManagedFixture();
  ASSERT_TRUE(fixture.has_value());
  EXPECT_TRUE(controller.wasOppositionReportViewed(fixture->opponent));

  // A few matches for the data hub and the opposition's form.
  Navigation::open(&view, NavSection::HOME);
  frames(view, 1);
  PlayedMatch lastMatch;
  EXPECT_GE(playManagedMatches(controller, 8, &lastMatch), 4);
  // A full match report (goals, assists, cards) at 720p.
  Navigation::openMatchReport(&view, lastMatch.date, lastMatch.home,
                              lastMatch.away);
  frames(view, 3);
  capture(view, "guidance_match_report.bmp");
  Navigation::back(&view);
  frames(view, 1);
  EXPECT_TRUE(
      controller.getOnboarding().isDone(OnboardingTask::PlayFirstMatch));

  Navigation::open(&view, NavSection::DATA_HUB);
  frames(view, 3);
  auto* hub = dynamic_cast<DataHubScene*>(Bridge::activeScene(view));
  ASSERT_NE(hub, nullptr);
  EXPECT_GE(Bridge::trendPoints(*hub), 4U);
  capture(view, "guidance_data_hub_team.bmp");
  // Chart tooltips keep the font stack balanced.
  hoverEverywhere(view);
  // A click on the xG trend opens the report of the match under the cursor.
  {
    const ImGuiWindow* card = nullptr;
    for (const ImGuiWindow* window : GImGui->Windows)
      if (window->Active && std::string_view(window->Name).find("/hub_trend") !=
                                std::string_view::npos)
        card = window;
    ASSERT_NE(card, nullptr);
    const ImGuiID plot = ImHashStr("##trend_plot", 0, card->ID);
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 centre = card->Rect().GetCenter();
    bool found = false;
    for (int y_index = 0;
         card->Rect().Min.y + 6.0f * static_cast<float>(y_index) <
             card->Rect().Max.y &&
         !found;
         ++y_index)
    {
      const float y = card->Rect().Min.y + 6.0f * static_cast<float>(y_index);
      io.AddMousePosEvent(centre.x, y);
      Bridge::frame(view);
      found = GImGui->HoveredId == plot;
    }
    ASSERT_TRUE(found) << "xG trend chart not hoverable";
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    Bridge::frame(view);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frames(view, 2);
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    frames(view, 1);
    EXPECT_EQ(Bridge::activeScene(view)->getID(), SceneID::MATCH_REPORT);
    capture(view, "guidance_data_hub_trend_report.bmp");
    Navigation::back(&view);
    frames(view, 2);
    hub = dynamic_cast<DataHubScene*>(Bridge::activeScene(view));
    ASSERT_NE(hub, nullptr);
  }
  Bridge::showPlayers(*hub);
  frames(view, 2);
  hoverEverywhere(view);
  capture(view, "guidance_data_hub_players.bmp");

  Navigation::open(&view, NavSection::INBOX);
  frames(view, 3);
  capture(view, "guidance_inbox.bmp");

  Navigation::open(&view, NavSection::OPPOSITION);
  frames(view, 3);
  capture(view, "guidance_opposition_form.bmp");

  // Narrow window: stacked layouts, no horizontal scrolling.
  resize(view, 900, 700);
  frames(view, 3);
  capture(view, "guidance_opposition_narrow.bmp");
  Navigation::open(&view, NavSection::DATA_HUB);
  frames(view, 3);
  capture(view, "guidance_data_hub_narrow.bmp");

  // UI scale 2 on a 2560x1440 window (a HiDPI desktop at scale 2).
  resize(view, 2560, 1440);
  setUiScale(view, 2.0f);
  frames(view, 3);
  capture(view, "guidance_data_hub_hidpi.bmp");
  Navigation::open(&view, NavSection::HOME);
  frames(view, 3);
  capture(view, "guidance_home_hidpi.bmp");
  Navigation::open(&view, NavSection::DELEGATION);
  frames(view, 3);
  capture(view, "guidance_delegation_hidpi.bmp");
  setUiScale(view, 0.0f);
  resize(view, 1280, 720);
  frames(view, 2);
}

TEST(GuidanceUiTest, HalfTimeAnalysisOpensAtTheBreak)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(1)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  controller.selectManagedTeam(controller.getTeams().front().get().getId());
  controller.advanceToNextManagedFixture(60);
  const auto fixture = controller.getNextManagedFixture();
  ASSERT_TRUE(fixture.has_value());
  ASSERT_TRUE(fixture->date == controller.getCurrentDate());
  const TeamID club = controller.getManagedTeam()->get().getId();
  const TeamID home = fixture->home ? club : fixture->opponent;
  const TeamID away = fixture->home ? fixture->opponent : club;
  // Talks given beforehand, so their dialogs stay closed.
  controller.giveTeamTalk(TeamTalkMoment::PreMatch, TeamTalkTone::Calm);
  controller.giveTeamTalk(TeamTalkMoment::HalfTime, TeamTalkTone::Calm);

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  resize(view, 1280, 720);
  view.changeScene(std::make_unique<MatchScene>(&view, home, away));
  frames(view, 2);
  auto* scene = dynamic_cast<MatchScene*>(Bridge::activeScene(view));
  ASSERT_NE(scene, nullptr);
  MatchEngine* engine = MatchBridge::engine(*scene);
  ASSERT_NE(engine, nullptr);
  EXPECT_FALSE(MatchBridge::analysisOpen(*scene));
  for (int step = 0;
       step < 200000 && engine->getState() != MatchState::HALF_TIME; ++step)
    engine->update(0.05f);
  ASSERT_EQ(engine->getState(), MatchState::HALF_TIME);
  frames(view, 3);
  EXPECT_TRUE(MatchBridge::analysisOpen(*scene));
  // A side panel, not a modal: the match controls stay usable. The match
  // stops at the break, so the control is Resume.
  EXPECT_TRUE(matchControlHoverable(view, LOC("MATCH_RESUME")))
      << "the half-time analysis covers or blocks the Resume button";
  capture(view, "guidance_half_time_analysis.bmp");
  resize(view, 2560, 1440);
  setUiScale(view, 2.0f);
  frames(view, 3);
  capture(view, "guidance_half_time_analysis_hidpi.bmp");
  EXPECT_TRUE(matchControlHoverable(view, LOC("MATCH_RESUME")))
      << "at UI scale 2";
  setUiScale(view, 0.0f);
  resize(view, 1280, 720);
  frames(view, 2);

  // Full time: the analysis opens again and the Finish button stays
  // reachable (it used to sit under a modal).
  for (int step = 0;
       step < 400000 && engine->getState() != MatchState::FULL_TIME; ++step)
    engine->update(0.05f);
  ASSERT_EQ(engine->getState(), MatchState::FULL_TIME);
  frames(view, 3);
  EXPECT_TRUE(MatchBridge::analysisOpen(*scene));
  EXPECT_TRUE(matchControlHoverable(view, LOC("MATCH_FINISH")))
      << "the full-time analysis covers or blocks the Finish button";
  capture(view, "guidance_full_time_analysis.bmp");
  // Clicking the pitch does not bury the panel under the match window.
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 pitch(io.DisplaySize.x * 0.3f, io.DisplaySize.y * 0.6f);
  io.AddMousePosEvent(pitch.x, pitch.y);
  io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
  frames(view, 1);
  io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
  frames(view, 2);
  EXPECT_TRUE(MatchBridge::analysisOpen(*scene));
  EXPECT_TRUE(analysisPanelOnTop(view));
  // Narrow window: the panel still leaves the controls free.
  resize(view, 900, 700);
  frames(view, 3);
  EXPECT_TRUE(matchControlHoverable(view, LOC("MATCH_FINISH"))) << "900x700";
  capture(view, "guidance_full_time_analysis_narrow.bmp");
  resize(view, 1280, 720);
  frames(view, 2);
}

/**
 * Opening the newest message of an unread-only digest rebuilds the thread
 * list mid-click: the list must not keep drawing the old rows (it read a
 * freed thread and indexed an empty title list before).
 */
TEST(GuidanceUiTest, OpeningAnUnreadDigestRebuildsTheListSafely)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(2)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  controller.selectManagedTeam(controller.getTeams().front().get().getId());
  controller.markAllInboxMessagesRead();
  Inbox& inbox = controller.getGame()->getWorld().getInbox();
  const auto post = [&](const char* title)
  {
    InboxMessage message;
    message.date = controller.getCurrentDate();
    message.category = InboxCategory::Board;
    message.title_key = title;
    message.body_key = "INBOX_BOARD_EMBARGO_BODY";
    message.args = {"-1"};
    inbox.add(std::move(message));
  };
  post("INBOX_BOARD_CASH_WARNING_TITLE");
  post("INBOX_BOARD_EMBARGO_TITLE");
  post("INBOX_BOARD_EMBARGO_TITLE");

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  resize(view, 1280, 720);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  Navigation::open(&view, NavSection::INBOX);
  frames(view, 3);
  auto* scene = dynamic_cast<InboxScene*>(Bridge::activeScene(view));
  ASSERT_NE(scene, nullptr);
  Bridge::showUnreadInformation(*scene);
  frames(view, 2);
  ASSERT_EQ(Bridge::threadCount(*scene), 2U);
  ASSERT_EQ(Bridge::firstThreadSize(*scene), 2U) << "newest thread: digest";

  // Click the digest row (the first row of the thread list).
  const ImGuiWindow* list = nullptr;
  for (const ImGuiWindow* window : GImGui->Windows)
    if (window->Active &&
        std::string_view(window->Name).find("/inbox_threads") !=
            std::string_view::npos)
      list = window;
  ASSERT_NE(list, nullptr);
  const ImVec2 row(
      list->Pos.x + list->Size.x * 0.5f,
      list->Pos.y + list->WindowPadding.y + ImGui::GetTextLineHeight());
  ImGui::GetIO().AddMousePosEvent(row.x, row.y);
  Bridge::frame(view);
  ImGui::GetIO().AddMouseButtonEvent(0, true);
  Bridge::frame(view);
  ImGui::GetIO().AddMouseButtonEvent(0, false);
  frames(view, 3);
  ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  frames(view, 2);
  // The opened message is read now; the list shows what is still unread.
  EXPECT_EQ(controller.getUnreadInboxCount(), 2U);
  EXPECT_EQ(Bridge::threadCount(*scene), 2U);
}
