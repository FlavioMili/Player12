// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The scout pages of the Scouting screen through the real GUIView: a scout on
// assignment, one with history, one never sent anywhere, the send flow and
// the new-reports badges, at several window sizes. Screenshots land in the
// test runtime's captures folder (FM_KEEP_TEST_ARTIFACTS=1 keeps them).

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "controller/game_controller.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/scouting_scene.h"

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
  static void selectScout(ScoutingScene& scene, uint32_t id)
  {
    scene.selectScout(id);
  }
  static void openSendFlow(ScoutingScene& scene)
  {
    scene.send_open = true;
    scene.send_dirty = true;
  }
  static void pick(ScoutingScene& scene, ScoutTargetKind kind, uint32_t id)
  {
    scene.send_kind = kind;
    scene.send_target = id;
    scene.send_dirty = true;
  }
  static const ScoutingScene::ScoutLine* selectedLine(
      const ScoutingScene& scene)
  {
    return scene.selectedScoutLine();
  }
  static size_t reportRows(const ScoutingScene& scene)
  {
    return scene.scout_reports.size();
  }
  static size_t freshRows(const ScoutingScene& scene)
  {
    return static_cast<size_t>(std::ranges::count_if(
        scene.scout_reports,
        [](const ScoutingScene::ReportLine& line) { return line.fresh; }));
  }
  static size_t effectLines(const ScoutingScene& scene)
  {
    return scene.effect_lines.size();
  }
  static int64_t sendCost(const ScoutingScene& scene)
  {
    return scene.send_cost;
  }
  static size_t pickerEntries(const ScoutingScene& scene)
  {
    return scene.picker_multipliers.size();
  }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;
constexpr LeagueID OWN_LEAGUE = 1;
constexpr LeagueID FOREIGN_LEAGUE = 3;

int uniqueSlot(int offset)
{
  return 400'000 + getpid() % 100'000 * 10 + offset;
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
}  // namespace

TEST(ScoutingUiTest, ScoutPagesFollowWhatEachScoutIsDoing)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(0)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  // The most reputable club of the league employs several scouts.
  TeamID club = 0;
  for (const TeamID id :
       controller.getLeagueById(OWN_LEAGUE)->get().getTeamIDs())
  {
    if (club == 0 || controller.getTeamById(id)->get().getReputation() >
                         controller.getTeamById(club)->get().getReputation())
      club = id;
  }
  controller.selectManagedTeam(club);
  const auto scouts = controller.getScouts();
  ASSERT_GE(scouts.size(), 3u);
  const uint32_t assigned = scouts[0].id;
  const uint32_t experienced = scouts[1].id;
  const uint32_t fresh = scouts[2].id;
  const auto& foreign_clubs =
      controller.getLeagueById(FOREIGN_LEAGUE)->get().getTeamIDs();
  ASSERT_FALSE(foreign_clubs.empty());
  const auto foreign_players =
      controller.getPlayersForTeam(foreign_clubs.front());
  ASSERT_FALSE(foreign_players.empty());
  const PlayerID target = foreign_players.front().get().getId();
  // Domestic rivals are known best: reports come quickly.
  ASSERT_EQ(controller.startScoutAssignment(assigned, ScoutTargetKind::League,
                                            OWN_LEAGUE, 40),
            ScoutAssignError::None);
  ASSERT_EQ(controller.startScoutAssignment(experienced,
                                            ScoutTargetKind::Player, target, 4),
            ScoutAssignError::None);
  for (int day = 0; day < 14; ++day) controller.advanceDay();
  const size_t unread = controller.getUnreadScoutReportCount();
  ASSERT_GT(unread, 0u);

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  Navigation::open(&view, NavSection::SCOUTING);
  frames(view, 3);
  auto* scene = dynamic_cast<ScoutingScene*>(Bridge::activeScene(view));
  ASSERT_NE(scene, nullptr);
  capture(view, "scouting_scouts_opened.bmp");

  // A scout on assignment: live assignment and the reports filed so far.
  Bridge::selectScout(*scene, assigned);
  frames(view, 3);
  ASSERT_NE(Bridge::selectedLine(*scene), nullptr);
  EXPECT_EQ(Bridge::selectedLine(*scene)->summary.status,
            ScoutStatus::OnAssignment);
  const uint32_t active =
      Bridge::selectedLine(*scene)->summary.active_assignment_id;
  const auto filed = static_cast<size_t>(std::ranges::count_if(
      controller.getScoutReports(), [active](const ScoutReport& report)
      { return report.assignment_id == active; }));
  EXPECT_GT(filed, 0u);
  EXPECT_EQ(Bridge::reportRows(*scene), filed);
  EXPECT_GT(Bridge::effectLines(*scene), 0u);
  capture(view, "scouting_scout_on_assignment.bmp");

  // A scout back from a trip: his history and its reports.
  Bridge::selectScout(*scene, experienced);
  frames(view, 3);
  EXPECT_EQ(Bridge::selectedLine(*scene)->summary.status,
            ScoutStatus::IdleWithHistory);
  EXPECT_EQ(Bridge::reportRows(*scene), 1u);
  capture(view, "scouting_scout_history.bmp");
  EXPECT_EQ(controller.getUnreadScoutReportCount(), 0u)
      << "opening both scouts' pages reads their reports";

  // A scout never sent anywhere: the world picker with his effectiveness.
  Bridge::selectScout(*scene, fresh);
  frames(view, 3);
  EXPECT_EQ(Bridge::selectedLine(*scene)->summary.status, ScoutStatus::IdleNew);
  EXPECT_GT(Bridge::pickerEntries(*scene), 5u);
  EXPECT_GT(Bridge::sendCost(*scene), 0) << "a destination is suggested";
  capture(view, "scouting_scout_new.bmp");
  Bridge::pick(
      *scene, ScoutTargetKind::Region,
      static_cast<uint32_t>(std::to_underlying(Continent::SouthAmerica)));
  frames(view, 2);
  EXPECT_GT(Bridge::effectLines(*scene), 0u);
  capture(view, "scouting_scout_new_region.bmp");

  // The experienced scout's send flow, then every window size.
  Bridge::selectScout(*scene, experienced);
  Bridge::openSendFlow(*scene);
  frames(view, 3);
  capture(view, "scouting_scout_send.bmp");
  for (const auto& [width, height, name] :
       {std::tuple{1280, 720, "scouting_scouts_1280.bmp"},
        std::tuple{900, 700, "scouting_scouts_900.bmp"},
        std::tuple{3840, 2160, "scouting_scouts_3840.bmp"}})
  {
    resize(view, width, height);
    Bridge::selectScout(*scene, assigned);
    frames(view, 4);
    capture(view, name);
  }
}

TEST(ScoutingUiTest, NewReportsAreBadgedUntilTheScoutPageIsOpened)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(1)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  controller.selectManagedTeam(
      controller.getLeagueById(OWN_LEAGUE)->get().getTeamIDs().front());
  const auto scouts = controller.getScouts();
  ASSERT_FALSE(scouts.empty());
  ASSERT_EQ(controller.startScoutAssignment(
                scouts.front().id, ScoutTargetKind::League, FOREIGN_LEAGUE, 20),
            ScoutAssignError::None);
  for (int day = 0; day < 20; ++day) controller.advanceDay();
  const size_t unread = controller.getUnreadScoutReportCount();
  ASSERT_GT(unread, 0u);

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  capture(view, "scouting_sidebar_badge.bmp");
  EXPECT_EQ(controller.getUnreadScoutReportCount(), unread)
      << "the badge alone does not read the reports";

  Navigation::open(&view, NavSection::SCOUTING);
  frames(view, 3);
  auto* scene = dynamic_cast<ScoutingScene*>(Bridge::activeScene(view));
  ASSERT_NE(scene, nullptr);
  // The page opens on the scout with news; his reports are marked new once.
  EXPECT_EQ(Bridge::selectedLine(*scene)->summary.profile.id,
            scouts.front().id);
  EXPECT_EQ(Bridge::freshRows(*scene), Bridge::reportRows(*scene));
  EXPECT_EQ(controller.getUnreadScoutReportCount(), 0u);
}
