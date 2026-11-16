// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Youth screen through the real GUIView after a U18 season, the intake
// preview and intake day: every tab, the trialist offer flow, the squad
// actions, several window sizes and a 200% interface scale. Screenshots land
// in the test runtime's captures folder (FM_KEEP_TEST_ARTIFACTS=1 keeps them).

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unistd.h>

#include <filesystem>
#include <memory>
#include <string>
#include <tuple>

#include "controller/game_controller.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/roster_scene.h"
#include "gui/scenes/youth_scene.h"
#include "model/calendar.h"
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
  static void openTab(YouthScene& scene, int tab)
  {
    scene.openTab(static_cast<YouthScene::Tab>(tab));
  }
  static void select(YouthScene& scene, PlayerID id) { scene.selected = id; }
  static size_t candidates(const YouthScene& scene)
  {
    return scene.intake_rows.size();
  }
  static size_t squad(const YouthScene& scene)
  {
    return scene.squad_rows.size();
  }
  static PlayerID firstCandidate(const YouthScene& scene)
  {
    return scene.intake_rows.front().view.id;
  }
  static PlayerID firstSquadPlayer(const YouthScene& scene)
  {
    return scene.squad_rows.front().view.id;
  }
  static size_t improvers(const YouthScene& scene)
  {
    return scene.improvers.size();
  }
  static void sign(YouthScene& scene, PlayerID id)
  {
    scene.pending = {YouthScene::PendingAction::Kind::SIGN, id};
  }
  static void askBoard(YouthScene& scene)
  {
    scene.pending = {YouthScene::PendingAction::Kind::UPGRADE, 0,
                     AcademyUpgrade::Recruitment};
  }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;
constexpr LeagueID OWN_LEAGUE = 1;

int uniqueSlot(int offset)
{
  return 700'000 + (getpid() % 100'000) * 10 + offset;
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

TEST(YouthUiTest, AcademyScreenThroughTheIntakeCycle)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(0)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  // The club of the league with the most teenagers (progress charts).
  TeamID club = 0;
  {
    YouthAcademy census(controller.getGameData());
    census.ensureReady();
    std::size_t most = 0;
    for (const TeamID id :
         controller.getLeagueById(OWN_LEAGUE)->get().getTeamIDs())
    {
      const std::size_t teenagers =
          census.members(id, YouthStatus::Squad).size();
      if (club == 0 || teenagers > most)
      {
        club = id;
        most = teenagers;
      }
    }
    ASSERT_GT(most, 0u);
  }
  controller.selectManagedTeam(club);

  // A U18 season up to intake day through the world's days (training and
  // development included, so the Development tab has real improvers); the
  // senior calendar is not needed for these screens.
  auto* game = const_cast<Game*>(controller.getGame());
  WorldSimulation& world = game->getWorld();
  const YouthAcademy& academy = world.getYouth();
  for (GameDateValue date(2025, 7, 20); date < GameDateValue(2026, 3, 15);)
  {
    date = SeasonCalendar::addDays(date, 1);
    world.onDayAdvanced(date, club);
  }
  ASSERT_FALSE(academy.members(club, YouthStatus::Candidate).empty());

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  Navigation::open(&view, NavSection::YOUTH);
  frames(view, 3);
  auto* scene = dynamic_cast<YouthScene*>(Bridge::activeScene(view));
  ASSERT_NE(scene, nullptr);
  // Trialists are waiting: the screen opens on them.
  EXPECT_GT(Bridge::candidates(*scene), 0u);
  capture(view, "youth_intake.bmp");

  const PlayerID trialist = Bridge::firstCandidate(*scene);
  Bridge::select(*scene, trialist);
  frames(view, 2);
  capture(view, "youth_intake_offer.bmp");
  const size_t before = Bridge::squad(*scene);
  Bridge::sign(*scene, trialist);
  frames(view, 2);
  EXPECT_EQ(Bridge::squad(*scene), before + 1);
  EXPECT_TRUE(controller.isAcademyPlayer(trialist));

  Bridge::openTab(*scene, 0);
  frames(view, 3);
  capture(view, "youth_overview.bmp");
  Bridge::askBoard(*scene);
  frames(view, 3);
  capture(view, "youth_overview_request.bmp");

  Bridge::openTab(*scene, 1);
  frames(view, 3);
  Bridge::select(*scene, Bridge::firstSquadPlayer(*scene));
  frames(view, 2);
  capture(view, "youth_squad.bmp");

  Bridge::openTab(*scene, 3);
  frames(view, 3);
  EXPECT_GT(Bridge::improvers(*scene), 0u);
  capture(view, "youth_development.bmp");

  // Academy players are not listed in the first-team squad.
  Navigation::open(&view, NavSection::SQUAD);
  frames(view, 3);
  for (const auto& player : controller.getPlayersForTeam(club))
    if (controller.isAcademyPlayer(player.get().getId()))
      EXPECT_LE(player.get().getAge(), 18);
  Navigation::open(&view, NavSection::YOUTH);
  frames(view, 3);
  scene = dynamic_cast<YouthScene*>(Bridge::activeScene(view));
  ASSERT_NE(scene, nullptr);

  // Window sizes (no horizontal scrolling: columns drop instead).
  for (const auto& [width, height, tab, name] :
       {std::tuple{1024, 700, 1, "youth_squad_1024.bmp"},
        std::tuple{900, 640, 2, "youth_intake_900.bmp"},
        std::tuple{1280, 720, 0, "youth_overview_1280.bmp"},
        std::tuple{3840, 2160, 3, "youth_development_3840.bmp"}})
  {
    resize(view, width, height);
    Bridge::openTab(*scene, tab);
    frames(view, 4);
    capture(view, name);
  }

  // 200% interface scale on a 1440p window.
  Settings& settings = SettingsManager::instance()->get();
  const float original = settings.ui_scale;
  settings.ui_scale = 2.0f;
  view.refreshTheme();
  resize(view, 2560, 1440);
  for (const auto& [tab, name] : {std::pair{0, "youth_overview_200.bmp"},
                                  std::pair{1, "youth_squad_200.bmp"},
                                  std::pair{2, "youth_intake_200.bmp"}})
  {
    Bridge::openTab(*scene, tab);
    frames(view, 4);
    capture(view, name);
  }
  settings.ui_scale = original;
  view.refreshTheme();
}
