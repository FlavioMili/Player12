// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Planning, holiday, awards and records screens through the real GUIView: the
// pre-season planner with its editor, the mentoring and holiday dialogs, a
// holiday run on the Continue machinery with its report, then the honours
// screens after a month of league football, at several window sizes and at
// UI scale 2. Screenshots land in the test runtime's captures folder
// (FM_KEEP_TEST_ARTIFACTS=1 keeps them).

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
#include "gui/scenes/awards_scene.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/preseason_scene.h"
#include "gui/scenes/records_scene.h"
#include "model/mentoring.h"
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
  static void editFriendly(PreseasonScene& scene, int index)
  {
    scene.selected_friendly = index;
    scene.loadOpponents();
  }
  static size_t friendlies(const PreseasonScene& scene)
  {
    return scene.friendlies.size();
  }
  static void openMentoring(PreseasonScene& scene, GameController& controller)
  {
    scene.mentoring_dialog.open(controller);
  }
  static HolidayDialog& holiday(ManagementScene& scene)
  {
    return scene.holiday_dialog;
  }
  static size_t monthRows(const AwardsScene& scene)
  {
    return scene.months.size();
  }
  static size_t raceRows(const AwardsScene& scene) { return scene.race.size(); }
  static size_t clubRecords(const RecordsScene& scene)
  {
    return scene.club_records.size();
  }
  static size_t allTimeRows(const RecordsScene& scene)
  {
    return scene.all_time.size();
  }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 8'000'000 + getpid() % 100'000 * 10 + offset;
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

/** Escape closes the open dialog. */
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
}  // namespace

TEST(CareerUiTest, PlanningHolidayAwardsAndRecords)
{
  using enum NavSection;
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(0)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  controller.selectManagedTeam(controller.getTeams().front().get().getId());
  const TeamID managed = controller.getManagedTeam()->get().getId();

  // A mentoring group so the planning card and the dialog have content.
  PlayerID mentor = 0;
  std::vector<PlayerID> youngsters;
  for (const auto& player : controller.getPlayersForTeam(managed))
  {
    if (player.get().getAge() >= 29 && mentor == 0)
      mentor = player.get().getId();
    else if (player.get().getAge() <= 21 && youngsters.size() < 2)
      youngsters.push_back(player.get().getId());
  }
  uint32_t group = 0;
  ASSERT_EQ(controller.createMentoringGroup(mentor, &group),
            MentoringError::None);
  for (const PlayerID youngster : youngsters)
    ASSERT_EQ(controller.addMentee(group, youngster), MentoringError::None);

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  resize(view, 1440, 900);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 3);

  auto* planning = open<PreseasonScene>(view, PLANNING);
  ASSERT_NE(planning, nullptr);
  EXPECT_EQ(Bridge::friendlies(*planning), 4u);
  capture(view, "career_planning.bmp");
  Bridge::editFriendly(*planning, 1);
  frames(view, 3);
  capture(view, "career_planning_friendly.bmp");
  Bridge::openMentoring(*planning, controller);
  frames(view, 3);
  capture(view, "career_mentoring_dialog.bmp");
  pressEscape(view);

  // The holiday planner from the shell, then a real holiday on the worker.
  Bridge::holiday(*planning).open(controller);
  frames(view, 3);
  capture(view, "career_holiday_planner.bmp");
  pressEscape(view);
  auto* hub = dynamic_cast<MainGameScene*>(view.getBaseScene());
  ASSERT_NE(hub, nullptr);
  HolidayPlan plan;
  plan.mode = HolidayMode::UntilDate;
  plan.until = GameDateValue(2025, 9, 2);
  plan.preferences.stop_big_bid = false;
  plan.preferences.stop_key_injury = false;
  plan.preferences.stop_injury_crisis = false;
  plan.preferences.stop_sacking_warning = false;
  hub->requestHoliday(plan);
  bool overlay_captured = false;
  for (int frame = 0; frame < 200'000; ++frame)
  {
    Bridge::frame(view);
    if (!overlay_captured && hub->isAdvancing() &&
        controller.getContinueProgress().days_done > 5)
    {
      capture(view, "career_holiday_progress.bmp");
      overlay_captured = true;
    }
    if (!hub->isAdvancing() && controller.getHolidaySummary().valid) break;
    SDL_Delay(2);
  }
  ASSERT_TRUE(controller.getHolidaySummary().valid);
  EXPECT_EQ(controller.getCurrentDate(), plan.until);
  frames(view, 4);
  EXPECT_TRUE(Bridge::holiday(*hub).isOpen());
  capture(view, "career_holiday_summary.bmp");
  pressEscape(view);

  auto* awards = open<AwardsScene>(view, AWARDS);
  ASSERT_NE(awards, nullptr);
  EXPECT_GT(Bridge::monthRows(*awards), 0u);
  EXPECT_GT(Bridge::raceRows(*awards), 0u);
  capture(view, "career_awards.bmp");

  auto* records = open<RecordsScene>(view, RECORDS);
  ASSERT_NE(records, nullptr);
  EXPECT_GT(Bridge::clubRecords(*records), 0u);
  capture(view, "career_records_club.bmp");
  records->showTab(RecordsScene::Tab::LEAGUE);
  frames(view, 2);
  capture(view, "career_records_league.bmp");
  records->showTab(RecordsScene::Tab::ALL_TIME);
  frames(view, 2);
  EXPECT_GT(Bridge::allTimeRows(*records), 0u);
  capture(view, "career_records_all_time.bmp");
  records->showTab(RecordsScene::Tab::HALL_OF_FAME);
  frames(view, 2);
  capture(view, "career_records_hall.bmp");

  // Narrow windows and UI scale 2 (a HiDPI desktop).
  for (const auto& [width, height, scale, suffix] :
       {std::tuple{900, 700, 1.0f, "900"},
        std::tuple{2560, 1600, 2.0f, "scale2"}})
  {
    SettingsManager::instance()->get().ui_scale = scale;
    view.refreshTheme();
    resize(view, width, height);
    frames(view, 3);
    for (const auto& [section, name] :
         {std::pair{AWARDS, "awards"}, std::pair{RECORDS, "records"},
          std::pair{PLANNING, "planning"}})
    {
      Navigation::open(&view, section);
      frames(view, 4);
      const std::string file =
          std::string("career_") + name + "_" + suffix + ".bmp";
      capture(view, file.c_str());
    }
  }
  SettingsManager::instance()->get().ui_scale = 0.0f;
}
