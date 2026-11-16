// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// World news, the career timeline and the draw ceremonies through the real
// GUIView after six weeks of a seeded season, at several window sizes and
// at UI scale 2. Screenshots land in the test runtime's captures folder
// (FM_KEEP_TEST_ARTIFACTS=1 keeps them).

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <tuple>

#include "controller/game_controller.h"
#include "global/language_manager.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/inbox_scene.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/news_scene.h"
#include "gui/scenes/standings_scene.h"
#include "gui/scenes/timeline_scene.h"
#include "model/competition.h"
#include "model/continental.h"
#include "model/draw_ceremony.h"
#include "model/game.h"
#include "model/settings_manager.h"

/** The GUI classes grant their internals to this name. */
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
  static void showCup(StandingsScene& scene, LeagueID cup)
  {
    scene.cup_id = cup;
    scene.showing_cup = true;
    scene.refresh();
  }
  static DrawCeremonyDialog& cupDialog(StandingsScene& scene)
  {
    return scene.draw_dialog;
  }
  static void openMessage(InboxScene& scene, size_t index)
  {
    scene.openMessage(index);
  }
  static DrawCeremonyDialog& inboxDialog(InboxScene& scene)
  {
    return scene.draw_dialog;
  }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 6'500'000 + getpid() % 100'000 * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
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

void frames(GUIView& view, int count)
{
  for (int index = 0; index < count; ++index) Bridge::frame(view);
}

void pressEscape(GUIView& view)
{
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
  frames(view, 1);
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
  frames(view, 2);
}

template <typename Scene>
Scene* open(GUIView& view, NavSection section)
{
  Navigation::open(&view, section);
  frames(view, 3);
  return dynamic_cast<Scene*>(Bridge::activeScene(view));
}

/** Frames until the dialog has shown @p count ties (or a frame budget). */
void revealUntil(GUIView& view, const DrawCeremonyDialog& dialog,
                 std::size_t count)
{
  for (int frame = 0; frame < 2000 && dialog.shownCount() < count; ++frame)
  {
    Bridge::frame(view);
    SDL_Delay(2);
  }
}
}  // namespace

TEST(NewsUiTest, NewsTimelineAndDrawCeremonies)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(0)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  // A club of the top continental competition, so its draw reaches the
  // inbox.
  const auto* season =
      controller.getContinental()->getSeason(Continental::CHAMPIONS_CUP_ID);
  ASSERT_NE(season, nullptr);
  const TeamID managed = season->entrants.front().team_id;
  controller.selectManagedTeam(managed);
  for (int day = 0; day < 64; ++day) controller.advanceDay();

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  resize(view, 1440, 900);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 3);

  // ---- World news.
  auto* news = open<NewsScene>(view, NavSection::NEWS);
  ASSERT_NE(news, nullptr);
  EXPECT_GT(news->storyCount(), 0U);
  EXPECT_EQ(news->shownCount(), news->storyCount());
  capture(view, "news_feed.png");
  news->filterKind(NewsKind::Transfer);
  frames(view, 2);
  EXPECT_LE(news->shownCount(), news->storyCount());
  capture(view, "news_transfers.png");
  const LeagueID country = Competitions::rootLeague(
      *controller.getGameData(),
      controller.getManagedTeam()->get().getLeagueId());
  news->filterKind(std::nullopt);
  news->filterCountry(country);
  frames(view, 2);
  EXPECT_GT(news->shownCount(), 0U);
  capture(view, "news_country.png");
  news->filterCountry(std::nullopt);
  frames(view, 2);

  // ---- Career timeline and the journal export.
  auto* timeline = open<TimelineScene>(view, NavSection::TIMELINE);
  ASSERT_NE(timeline, nullptr);
  EXPECT_GT(timeline->entryCount(), 0U);
  capture(view, "timeline_season.png");
  timeline->setZoom(TimelineScene::Zoom::MONTH);
  frames(view, 2);
  EXPECT_GE(timeline->groupCount(), 1U);
  capture(view, "timeline_month.png");
  const auto journal = timeline->exportJournal();
  ASSERT_TRUE(journal.has_value());
  ASSERT_TRUE(std::filesystem::exists(*journal));
  EXPECT_EQ(journal->parent_path(), RuntimePaths::root() / "journals");
  {
    std::ifstream file(*journal);
    std::string first_line;
    std::getline(file, first_line);
    EXPECT_EQ(first_line.rfind("# ", 0), 0U) << first_line;
  }
  std::filesystem::remove(*journal);

  // ---- The cup draw from the competition screen, tie by tie.
  auto* standings = open<StandingsScene>(view, NavSection::STANDINGS);
  ASSERT_NE(standings, nullptr);
  Bridge::showCup(*standings, country);
  frames(view, 2);
  const auto ceremony = DrawCeremonies::latestCupRound(
      controller.getGame()->getCalendar(), *controller.getGameData(), country,
      controller.getCurrentDate(), managed);
  ASSERT_TRUE(ceremony.has_value());
  DrawCeremonyDialog& cup_dialog = Bridge::cupDialog(*standings);
  cup_dialog.open(*ceremony, controller);
  frames(view, 2);
  EXPECT_TRUE(cup_dialog.isOpen());
  EXPECT_LT(cup_dialog.shownCount(), cup_dialog.revealCount());
  revealUntil(view, cup_dialog, 2);
  capture(view, "draw_cup_reveal.png");
  cup_dialog.revealAll();
  frames(view, 2);
  EXPECT_EQ(cup_dialog.shownCount(), cup_dialog.revealCount());
  capture(view, "draw_cup_complete.png");
  pressEscape(view);
  EXPECT_FALSE(cup_dialog.isOpen());

  // Reduced motion shows the whole draw at once.
  SettingsManager::instance()->get().reduced_motion = true;
  view.refreshTheme();
  cup_dialog.open(*ceremony, controller);
  frames(view, 2);
  EXPECT_EQ(cup_dialog.shownCount(), cup_dialog.revealCount());
  pressEscape(view);
  SettingsManager::instance()->get().reduced_motion = false;
  view.refreshTheme();

  // ---- The continental draw from its inbox message.
  const auto& messages = controller.getInbox();
  std::size_t draw_message = messages.size();
  for (std::size_t index = 0; index < messages.size(); ++index)
    if (DrawCeremonies::announcesDraw(messages[index])) draw_message = index;
  ASSERT_LT(draw_message, messages.size()) << "the league-phase draw message";
  const auto continental = DrawCeremonies::forMessage(
      messages[draw_message], *controller.getContinental(),
      controller.getGame()->getCalendar(), managed);
  ASSERT_TRUE(continental.has_value());
  EXPECT_EQ(continental->focus_team, managed);
  auto* inbox = open<InboxScene>(view, NavSection::INBOX);
  ASSERT_NE(inbox, nullptr);
  Bridge::openMessage(*inbox, draw_message);
  frames(view, 3);
  capture(view, "draw_inbox_message.png");
  DrawCeremonyDialog& inbox_dialog = Bridge::inboxDialog(*inbox);
  inbox_dialog.open(*continental, controller);
  revealUntil(view, inbox_dialog, 2);
  EXPECT_GE(inbox_dialog.shownCount(), 1U);
  inbox_dialog.revealAll();
  frames(view, 2);
  EXPECT_EQ(inbox_dialog.shownCount(), inbox_dialog.revealCount());
  capture(view, "draw_league_phase.png");
  pressEscape(view);

  // Narrow windows and UI scale 2 (a HiDPI desktop).
  for (const auto& [width, height, scale, suffix] :
       {std::tuple{900, 700, 1.0f, "900"},
        std::tuple{2560, 1440, 2.0f, "scale2"}})
  {
    SettingsManager::instance()->get().ui_scale = scale;
    view.refreshTheme();
    resize(view, width, height);
    frames(view, 3);
    open<NewsScene>(view, NavSection::NEWS);
    frames(view, 2);
    capture(view, std::string("news_feed_") + suffix + ".png");
    auto* scaled_timeline = open<TimelineScene>(view, NavSection::TIMELINE);
    ASSERT_NE(scaled_timeline, nullptr);
    capture(view, std::string("timeline_") + suffix + ".png");
    auto* scaled_standings = open<StandingsScene>(view, NavSection::STANDINGS);
    ASSERT_NE(scaled_standings, nullptr);
    Bridge::showCup(*scaled_standings, country);
    Bridge::cupDialog(*scaled_standings).open(*ceremony, controller);
    frames(view, 2);
    Bridge::cupDialog(*scaled_standings).revealAll();
    frames(view, 3);
    capture(view, std::string("draw_cup_") + suffix + ".png");
    pressEscape(view);
  }
  SettingsManager::instance()->get().ui_scale = 0.0f;
}
