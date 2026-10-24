// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The tactics screen through the real GUIView: roles, duties and the shape
// with the ball at 1280x720 and at UI scale 2 on 2560x1440 (no scrolling
// inside the page), and edits that survive leaving the screen without a
// save on the UI thread. Screenshots land in the test runtime's captures
// folder (FM_KEEP_TEST_ARTIFACTS=1 keeps them).

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unistd.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "controller/game_controller.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/strategy_scene.h"
#include "model/settings_manager.h"
#include "model/tactics.h"
#include "model/team.h"

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
  static void selectSlot(StrategyScene& scene, int slot)
  {
    scene.selected_slot = slot;
  }
  static void setRole(StrategyScene& scene, const Lineup& lineup,
                      TacticalRole role)
  {
    scene.setRole(lineup, role);
  }
  static void setDuty(StrategyScene& scene, const Lineup& lineup, RoleDuty duty)
  {
    scene.setDuty(lineup, duty);
  }
  static void setPressing(StrategyScene& scene, float pressing)
  {
    scene.current_sliders.pressing = pressing;
    scene.applySliders();
  }
  static void applyShape(StrategyScene& scene, const Lineup& lineup,
                         PossessionShape shape)
  {
    scene.applyShape(lineup, shape);
  }
  static bool changed(const StrategyScene& scene)
  {
    return scene.changedSinceEntry();
  }
  static void revert(StrategyScene& scene) { scene.revert(); }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 470'000 + getpid() % 100'000 * 10 + offset;
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

void hoverEverywhere(GUIView& view, float step = 48.0f)
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

void setUiScale(GUIView& view, float scale)
{
  SettingsManager::instance()->get().ui_scale = scale;
  view.refreshTheme();
}

/** Windows inside the page that scroll (cards must size to their content;
 * only the page itself scrolls). */
std::string nestedScrolling()
{
  std::string problems;
  constexpr std::string_view PAGE = "##management_shell/##content/";
  for (const ImGuiWindow* window : GImGui->Windows)
  {
    const std::string_view name(window->Name);
    if (!window->Active || !name.starts_with(PAGE)) continue;
    if (window->ScrollbarY || window->ScrollbarX)
      problems += " [" + std::string(name) + "]";
  }
  return problems;
}

bool tacticsCardShown()
{
  return std::ranges::any_of(
      GImGui->Windows,
      [](const ImGuiWindow* window)
      {
        return window->Active &&
               std::string_view(window->Name).contains("tactic_roles");
      });
}
}  // namespace

TEST(TacticsUiTest, TacticsScreenAtEverySize)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(0)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  controller.selectManagedTeam(controller.getTeams().front().get().getId());

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  resize(view, 1280, 720);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  Navigation::open(&view, NavSection::TACTICS);
  frames(view, 3);
  ASSERT_EQ(Bridge::activeScene(view)->getID(), SceneID::STRATEGY);
  EXPECT_TRUE(tacticsCardShown());
  EXPECT_EQ(nestedScrolling(), "") << "1280x720";
  capture(view, "tactics_roles_720.bmp");
  hoverEverywhere(view);

  // A role with its description and key attributes, and a shape with the
  // ball, at both sizes.
  auto* scene = dynamic_cast<StrategyScene*>(Bridge::activeScene(view));
  ASSERT_NE(scene, nullptr);
  const Lineup& lineup = controller.getManagedTeam()->get().getLineup();
  ASSERT_FALSE(lineup.getOutfieldPlayers().empty());
  Bridge::selectSlot(*scene, 0);
  Bridge::applyShape(*scene, lineup, PossessionShape::BuildWithThree);
  frames(view, 2);
  capture(view, "tactics_roles_shape_720.bmp");

  resize(view, 2560, 1440);
  setUiScale(view, 2.0f);
  frames(view, 3);
  EXPECT_TRUE(tacticsCardShown());
  EXPECT_EQ(nestedScrolling(), "") << "2560x1440 at scale 2";
  capture(view, "tactics_roles_hidpi.bmp");
  hoverEverywhere(view, 96.0f);

  // 1280x720 at scale 2: a 640-point window stacks the columns.
  resize(view, 1280, 720);
  frames(view, 3);
  EXPECT_EQ(nestedScrolling(), "") << "1280x720 at scale 2";
  capture(view, "tactics_roles_narrow.bmp");
  setUiScale(view, 0.0f);
  frames(view, 2);
}

TEST(TacticsUiTest, EditsSurviveLeavingWithoutAUiThreadSave)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(1)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  controller.selectManagedTeam(controller.getTeams().front().get().getId());

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  resize(view, 1280, 720);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  Navigation::open(&view, NavSection::TACTICS);
  frames(view, 2);
  auto* scene = dynamic_cast<StrategyScene*>(Bridge::activeScene(view));
  ASSERT_NE(scene, nullptr);

  const Team& club = controller.getManagedTeam()->get();
  const Lineup& lineup = club.getLineup();
  ASSERT_FALSE(lineup.getOutfieldPlayers().empty());
  const Vector2F firstSlot = lineup.getOutfieldPlayers().front().position;
  const RoleFamily family = Tactics::familyForSlot(firstSlot);
  const TacticalRole special = Tactics::rolesFor(family).back();
  ASSERT_NE(special, TacticalRole::Standard);
  const int savesBefore = controller.getSaveStatus().successful_saves;

  Bridge::selectSlot(*scene, 0);
  Bridge::setRole(*scene, lineup, special);
  Bridge::setDuty(*scene, lineup, RoleDuty::Attack);
  Bridge::setPressing(*scene, 0.93f);
  frames(view, 1);
  // Applied to the club at once, in memory: no save on the UI thread.
  ASSERT_NE(club.getStrategy().findSlot(firstSlot), nullptr);
  EXPECT_EQ(club.getStrategy().findSlot(firstSlot)->role, special);
  EXPECT_EQ(club.getStrategy().findSlot(firstSlot)->duty, RoleDuty::Attack);
  EXPECT_FLOAT_EQ(club.getStrategy().getSliders().pressing, 0.93f);
  EXPECT_TRUE(Bridge::changed(*scene));
  EXPECT_EQ(controller.getSaveStatus().successful_saves, savesBefore);

  // Leaving by the sidebar, then Back: nothing was dropped.
  Navigation::open(&view, NavSection::SQUAD);
  frames(view, 2);
  EXPECT_NE(Bridge::activeScene(view)->getID(), SceneID::STRATEGY);
  EXPECT_EQ(club.getStrategy().findSlot(firstSlot)->role, special);
  Navigation::back(&view);
  frames(view, 2);
  ASSERT_EQ(Bridge::activeScene(view)->getID(), SceneID::STRATEGY);
  scene = dynamic_cast<StrategyScene*>(Bridge::activeScene(view));
  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(club.getStrategy().findSlot(firstSlot)->role, special);
  EXPECT_FLOAT_EQ(club.getStrategy().getSliders().pressing, 0.93f);

  // Esc closes the screen the same way.
  Bridge::setPressing(*scene, 0.2f);
  Navigation::close(&view);
  frames(view, 2);
  EXPECT_FLOAT_EQ(club.getStrategy().getSliders().pressing, 0.2f);

  // Revert restores the tactic as it was on arrival.
  Navigation::open(&view, NavSection::TACTICS);
  frames(view, 2);
  scene = dynamic_cast<StrategyScene*>(Bridge::activeScene(view));
  ASSERT_NE(scene, nullptr);
  EXPECT_FALSE(Bridge::changed(*scene));
  Bridge::setPressing(*scene, 0.6f);
  Bridge::setRole(*scene, lineup, TacticalRole::Standard);
  EXPECT_TRUE(Bridge::changed(*scene));
  Bridge::revert(*scene);
  EXPECT_FALSE(Bridge::changed(*scene));
  EXPECT_FLOAT_EQ(club.getStrategy().getSliders().pressing, 0.2f);
  EXPECT_EQ(club.getStrategy().findSlot(firstSlot)->role, special);
  EXPECT_EQ(controller.getSaveStatus().successful_saves, savesBefore);
}
