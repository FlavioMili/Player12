// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The medical centre's load chart and instructions and the club screen's
// supporters card through the real GUIView, at 720p and at interface scale
// 2. Screenshots land in the test runtime's captures folder
// (FM_KEEP_TEST_ARTIFACTS=1 keeps them).

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/club_scene.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/medical_scene.h"
#include "model/game.h"
#include "model/medical_centre.h"
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
  static PlayerID selected(const MedicalScene& scene) { return scene.selected; }
  static bool chartEmpty(const MedicalScene& scene)
  {
    return scene.chart_empty;
  }
  static void select(MedicalScene& scene, PlayerID id) { scene.select(id); }
  static void setFlag(MedicalScene& scene, std::byte flag)
  {
    scene.setFlag(flag, true);
  }
  static bool supportersShown(const ClubScene& scene)
  {
    return scene.supporters_index.has_value();
  }
  static std::size_t supporterReasons(const ClubScene& scene)
  {
    return scene.supporter_reasons.size();
  }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 870'000 + getpid() % 100'000 * 10 + offset;
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

void frames(GUIView& view, int count)
{
  for (int index = 0; index < count; ++index) Bridge::frame(view);
}

void setUiScale(GUIView& view, float scale)
{
  SettingsManager::instance()->get().ui_scale = scale;
  view.refreshTheme();
}

template <typename Scene>
Scene* active(const GUIView& view)
{
  return dynamic_cast<Scene*>(Bridge::activeScene(view));
}
}  // namespace

TEST(MedicalSupportersUiTest, LoadChartInstructionsAndSupportersRender)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(0)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  const TeamID club = controller.getTeams().front().get().getId();
  controller.selectManagedTeam(club);
  // Five weeks of pre-season: a full load chart and a few weekly moods.
  for (int day = 0; day < 35; ++day) controller.advanceDay();
  const PlayerID player = controller.getPlayersForTeam(club)[5].get().getId();

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);

  Navigation::open(&view, NavSection::MEDICAL);
  frames(view, 3);
  auto* medical = active<MedicalScene>(view);
  ASSERT_NE(medical, nullptr);
  EXPECT_NE(Bridge::selected(*medical), 0u) << "a player is selected at once";
  Bridge::select(*medical, player);
  Bridge::setFlag(*medical, static_cast<std::byte>(MEDICAL_FLAG_LIMIT_MINUTES));
  EXPECT_EQ(Bridge::selected(*medical), player);
  EXPECT_FALSE(Bridge::chartEmpty(*medical));
  EXPECT_TRUE(controller.getGame()->getMedical().hasFlag(
      player, MEDICAL_FLAG_LIMIT_MINUTES));
  frames(view, 3);
  capture(view, "medical_load_chart.bmp");

  Navigation::open(&view, NavSection::CLUB);
  frames(view, 3);
  auto* club_scene = active<ClubScene>(view);
  ASSERT_NE(club_scene, nullptr);
  EXPECT_TRUE(Bridge::supportersShown(*club_scene));
  EXPECT_LE(Bridge::supporterReasons(*club_scene), 3u);
  capture(view, "club_supporters.bmp");

  // HiDPI: the same screens at interface scale 2 render without errors.
  setUiScale(view, 2.0f);
  frames(view, 3);
  capture(view, "club_supporters_scale2.bmp");
  Navigation::open(&view, NavSection::MEDICAL);
  frames(view, 3);
  capture(view, "medical_load_chart_scale2.bmp");
  setUiScale(view, 0.0f);
}
