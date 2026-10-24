// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Under-21 screen and the national-team Call-ups screen through the real
// GUIView: every tab, the promote and call-up actions, and both screens at
// 1280x720 and at a 200% interface scale on 2560x1440. Screenshots land in
// the test runtime's captures folder (FM_KEEP_TEST_ARTIFACTS=1 keeps them).

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
#include <vector>

#include "controller/game_controller.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/callup_scene.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/manager_scene.h"
#include "gui/scenes/reserves_scene.h"
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
  // Under-21s
  static void openTab(ReservesScene& scene, int tab)
  {
    scene.openTab(static_cast<ReservesScene::Tab>(tab));
  }
  static size_t squad(const ReservesScene& scene)
  {
    return scene.squad_rows.size();
  }
  static size_t candidates(const ReservesScene& scene)
  {
    return scene.candidate_rows.size();
  }
  static size_t tableRows(const ReservesScene& scene)
  {
    return scene.table.size();
  }
  static PlayerID firstSquadPlayer(const ReservesScene& scene)
  {
    return scene.squad_rows.front().view.id;
  }
  static void select(ReservesScene& scene, PlayerID id) { scene.selected = id; }
  static void promote(ReservesScene& scene, PlayerID id)
  {
    scene.pending = {ReservesScene::PendingAction::Kind::PROMOTE, id};
  }
  // Call-ups
  static size_t squad(const CallUpScene& scene) { return scene.working.size(); }
  static size_t pool(const CallUpScene& scene)
  {
    return scene.candidate_rows.size();
  }
  static void select(CallUpScene& scene, PlayerID id) { scene.selected = id; }
  static PlayerID lastInSquad(const CallUpScene& scene)
  {
    return scene.squad_rows.back()->candidate.id;
  }
  static void drop(CallUpScene& scene, PlayerID id)
  {
    std::erase(scene.working, id);
    scene.dirty = true;
    scene.lists_stale = true;
  }
  static void confirm(CallUpScene& scene) { scene.confirm_requested = true; }
  static void filter(CallUpScene& scene, int group)
  {
    scene.filter = group;
    scene.lists_stale = true;
  }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 840'000 + (getpid() % 10'000) * 10 + offset;
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

/** Runs @p body at 1280x720 (scale 1) and 2560x1440 (scale 2). */
template <typename Body>
void atBothSizes(GUIView& view, Body body)
{
  Settings& settings = SettingsManager::instance()->get();
  const float original = settings.ui_scale;
  resize(view, 1280, 720);
  frames(view, 3);
  body("1280");
  settings.ui_scale = 2.0f;
  view.refreshTheme();
  resize(view, 2560, 1440);
  frames(view, 3);
  body("200");
  settings.ui_scale = original;
  view.refreshTheme();
}
}  // namespace

TEST(ReserveUiTest, UnderTwentyOneScreenTabsActionsAndSizes)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(0)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  TeamID club = 0;
  for (const TeamID id : controller.getLeagueById(1)->get().getTeamIDs())
    if (club == 0 || controller.getTeamById(id)->get().getReputation() >
                         controller.getTeamById(club)->get().getReputation())
      club = id;
  controller.selectManagedTeam(club);
  // Two young first-team players join the U21s, then the world runs into
  // the season so the U21 league has a table and results.
  // Candidates first: a move changes the senior list being read.
  std::vector<PlayerID> young;
  for (const auto& player : controller.getPlayersForTeam(club))
    if (player.get().getAge() <= 21 && player.get().getRole() != PlayerRole::GK)
      young.push_back(player.get().getId());
  int moved = 0;
  for (const PlayerID id : young)
    if (moved < 2 && controller.moveToReserves(id) == YouthActionResult::Ok)
      ++moved;
  ASSERT_EQ(moved, 2);
  auto* game = controller.getGame();
  WorldSimulation& world = game->getWorld();
  for (GameDateValue date(2025, 7, 20); date < GameDateValue(2025, 10, 5);)
  {
    date = SeasonCalendar::addDays(date, 1);
    world.onDayAdvanced(date, club);
  }
  ASSERT_FALSE(controller.getReserveResults().empty());

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  Navigation::open(&view, NavSection::RESERVES);
  frames(view, 3);
  auto* scene = dynamic_cast<ReservesScene*>(Bridge::activeScene(view));
  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(Bridge::squad(*scene), 2u);
  EXPECT_GT(Bridge::candidates(*scene), 0u);
  EXPECT_GT(Bridge::tableRows(*scene), 20u);

  // Promote one player back to the first team from the detail strip.
  const PlayerID promoted = Bridge::firstSquadPlayer(*scene);
  Bridge::select(*scene, promoted);
  frames(view, 2);
  capture(view, "u21_squad_detail.bmp");
  Bridge::promote(*scene, promoted);
  frames(view, 2);
  EXPECT_EQ(Bridge::squad(*scene), 1u);
  EXPECT_FALSE(controller.isAcademyPlayer(promoted));

  atBothSizes(
      view,
      [&](const std::string& size)
      {
        for (const auto& [tab, name] :
             {std::pair{0, "squad"}, std::pair{1, "league"},
              std::pair{2, "candidates"}})
        {
          Bridge::openTab(*scene, tab);
          frames(view, 4);
          capture(view,
                  ("u21_" + std::string(name) + "_" + size + ".bmp").c_str());
        }
      });
}

TEST(NationalCallUpUiTest, CallUpScreenSquadEditingAndSizes)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(1)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  // The national teams form on the first day (season calendar).
  controller.advanceDay();
  Game* game = controller.getGame();
  // The lowest-rated nations with a pool deep enough for any squad.
  std::vector<Language> nations;
  {
    const std::vector<Language> ranking = game->getNationalTeams().ranking();
    for (auto it = ranking.rbegin(); it != ranking.rend() && nations.size() < 5;
         ++it)
      if (game->getNationalTeams()
              .eligiblePool(*it, controller.getCurrentDate())
              .size() >= 40)
        nations.push_back(*it);
  }
  ASSERT_FALSE(nations.empty());
  ManagerSetup setup;
  setup.first_name = "Livio";
  setup.last_name = "Castellani";
  setup.nationality = nations.front();
  setup.background = ManagerBackground::FormerInternational;
  controller.createManager(setup);
  for (const Language nation : nations)
  {
    game->getNationalJob().openVacancy(nation, controller.getCurrentDate());
    controller.applyForNationalJob(nation);
  }
  for (int day = 0; day < 12 && controller.getNationalJobOffers().empty();
       ++day)
    controller.advanceDay();
  ASSERT_FALSE(controller.getNationalJobOffers().empty());

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  // The Job Centre lists the national offer and the open national jobs.
  view.navigateTo(
      std::make_unique<ManagerScene>(&view, ManagerScene::Tab::JOB_CENTRE));
  frames(view, 3);
  capture(view, "national_job_centre.bmp");
  ASSERT_EQ(controller.acceptNationalJobOffer(
                controller.getNationalJobOffers().front().id),
            NationalApplyResult::Ok);

  // Up to the next call-up: the squad is announced a week before.
  bool announced = false;
  for (int day = 0; day < 120 && !announced; ++day)
  {
    controller.advanceDay();
    announced = game->getLastNationalEvents().squad_to_pick;
  }
  ASSERT_TRUE(announced);

  Navigation::open(&view, NavSection::CALL_UPS);
  frames(view, 3);
  auto* scene = dynamic_cast<CallUpScene*>(Bridge::activeScene(view));
  ASSERT_NE(scene, nullptr);
  const size_t squad = Bridge::squad(*scene);
  EXPECT_GE(squad, International::MIN_CALL_UPS);
  const auto* announced_squad = game->getNationalTeams().squadOf(
      controller.getNationalJob()->nation, controller.getCurrentDate());
  ASSERT_NE(announced_squad, nullptr);
  EXPECT_EQ(game->getNationalTeams().validateSquad(
                controller.getNationalJob()->nation, announced_squad->players,
                controller.getCurrentDate()),
            International::CallUpResult::Ok);
  EXPECT_EQ(announced_squad->players.size(), squad);
  EXPECT_GT(Bridge::pool(*scene), 0u);
  capture(view, "callup_announced.bmp");

  // Drop the last player and confirm: the nation's squad changes.
  const PlayerID dropped = Bridge::lastInSquad(*scene);
  Bridge::select(*scene, dropped);
  frames(view, 2);
  capture(view, "callup_detail.bmp");
  Bridge::drop(*scene, dropped);
  frames(view, 2);
  EXPECT_EQ(Bridge::squad(*scene), squad - 1);
  Bridge::confirm(*scene);
  frames(view, 2);
  const auto* nation_squad = game->getNationalTeams().squadOf(
      controller.getNationalJob()->nation, controller.getCurrentDate());
  ASSERT_NE(nation_squad, nullptr);
  EXPECT_EQ(nation_squad->players.size(), squad - 1);
  EXPECT_FALSE(std::ranges::contains(nation_squad->players, dropped));

  atBothSizes(view,
              [&](const std::string& size)
              {
                Bridge::filter(*scene, 0);
                frames(view, 3);
                capture(view, ("callup_" + size + ".bmp").c_str());
                Bridge::filter(*scene, 1);
                frames(view, 3);
                capture(view, ("callup_keepers_" + size + ".bmp").c_str());
              });

  // The sidebar opens the screen only for a head coach.
  ASSERT_TRUE(controller.resignNationalJob());
  frames(view, 3);
  Navigation::open(&view, NavSection::CALL_UPS);
  frames(view, 3);
  capture(view, "callup_no_job.bmp");
}
