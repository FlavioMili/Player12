// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Play mode through the real GUIView: the managed club's match is taken
// over, key presses move the active footballer and play a pass, the switch
// key changes the active footballer, and the pause menu hands the team back
// to the AI. Screenshots of the 2D and 3D play views land in the test
// runtime's captures folder (FM_KEEP_TEST_ARTIFACTS=1 keeps them).

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <memory>

#include "controller/game_controller.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/match_scene.h"
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
};

/** Live match internals (MatchScene grants them to this name). */
class GameFlowTest_ManagedMatchIntegration_Test
{
 public:
  static MatchEngine& engine(MatchScene& scene) { return *scene.engine; }
  static MatchPlayController& play(MatchScene& scene) { return scene.play; }
  static bool paused(const MatchScene& scene) { return scene.is_paused; }
  static bool confirming(const MatchScene& scene) { return scene.play_confirm; }
  static bool inMenu(const MatchScene& scene) { return scene.play_menu; }
  static bool managedHome(const MatchScene& scene)
  {
    return scene.managed_is_home.value_or(true);
  }
  static void requestTakeControl(MatchScene& scene)
  {
    scene.requestTakeControl();
  }
  static void startPlaying(MatchScene& scene) { scene.startPlaying(); }
  static bool canHandBack(const MatchScene& scene)
  {
    return scene.canHandBack();
  }
  static void handBack(MatchScene& scene) { scene.handBack(); }
  static void setView(MatchScene& scene, MatchViewMode mode)
  {
    scene.setViewMode(mode);
  }
  static MatchCameraMode camera(const MatchScene& scene)
  {
    return scene.camera_mode;
  }
  static bool highlights(const MatchScene& scene)
  {
    return scene.highlights_only;
  }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;
using MatchBridge = GameFlowTest_ManagedMatchIntegration_Test;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 480'000 + getpid() % 100'000 * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

void frames(GUIView& view, int count)
{
  for (int index = 0; index < count; ++index) Bridge::frame(view);
}

void pushKey(SDL_Scancode scancode, SDL_Keycode key, bool down)
{
  SDL_Event event{.key = {}};
  event.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  event.key.scancode = scancode;
  event.key.key = key;
  event.key.down = down;
  SDL_PushEvent(&event);
}

void capture(GUIView& view, const char* name)
{
  const auto path = RuntimePaths::capturePath(name);
  std::filesystem::remove(path);
  EXPECT_TRUE(view.captureScreenshot(path.string())) << name;
}

const MatchPlayer* findPlayer(const MatchEngine& engine, PlayerID id)
{
  for (const MatchPlayer& player : engine.getPlayers())
    if (player.player && player.player->getId() == id) return &player;
  return nullptr;
}

/** The managed side's outfield carrier, if it has the ball. */
const MatchPlayer* managedCarrier(const MatchEngine& engine, bool home)
{
  for (const MatchPlayer& player : engine.getPlayers())
    if (player.player && player.player == engine.getBall().possessedBy &&
        player.isHomeTeam == home && !player.isGoalkeeper &&
        engine.getState() == MatchState::PLAYING)
      return &player;
  return nullptr;
}
}  // namespace

TEST(PlayModeUiTest, PlayMovePassSwitchAndHandBack)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(0)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  controller.selectManagedTeam(controller.getTeams().front().get().getId());
  controller.advanceToNextManagedFixture(60);
  const auto fixture = controller.getNextManagedFixture();
  ASSERT_TRUE(fixture && fixture->date == controller.getCurrentDate());
  const TeamID club = controller.getManagedTeam()->get().getId();
  const TeamID home = fixture->home ? club : fixture->opponent;
  const TeamID away = fixture->home ? fixture->opponent : club;
  controller.giveTeamTalk(TeamTalkMoment::PreMatch, TeamTalkTone::Calm);
  controller.giveTeamTalk(TeamTalkMoment::HalfTime, TeamTalkTone::Calm);
  // Manual switching keeps the active footballer still between our presses.
  Settings& settings = SettingsManager::instance()->get();
  settings.play_auto_switch = 0;
  settings.play_half_minutes = 7;
  settings.pause_at_breaks = false;

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  SDL_SetWindowSize(view.getWindow(), 1280, 720);
  view.changeScene(std::make_unique<MatchScene>(&view, home, away));
  frames(view, 2);
  auto* scene = dynamic_cast<MatchScene*>(Bridge::activeScene(view));
  ASSERT_NE(scene, nullptr);
  MatchEngine& engine = MatchBridge::engine(*scene);
  const MatchPlayController& play = MatchBridge::play(*scene);
  const bool managedHome = MatchBridge::managedHome(*scene);

  // Play (Take control) pauses first and asks; then the human has the team.
  MatchBridge::requestTakeControl(*scene);
  EXPECT_TRUE(MatchBridge::paused(*scene));
  EXPECT_TRUE(MatchBridge::confirming(*scene));
  frames(view, 2);
  MatchBridge::startPlaying(*scene);
  frames(view, 2);
  ASSERT_TRUE(play.isActive());
  ASSERT_FALSE(engine.getInputLog().empty());
  EXPECT_EQ(engine.getInputLog().back().playHalfMinutes, 7);
  EXPECT_FALSE(MatchBridge::paused(*scene));
  EXPECT_FALSE(MatchBridge::highlights(*scene));
  EXPECT_EQ(MatchBridge::camera(*scene), MatchCameraMode::PLAY);
  EXPECT_NE(engine.getControlledPlayer(), 0u);

  // Into open play with the managed side on the ball: the carrier is taken.
  const MatchPlayer* carrier = nullptr;
  for (int step = 0; step < 30'000 && !carrier; ++step)
  {
    engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    carrier = managedCarrier(engine, managedHome);
  }
  ASSERT_NE(carrier, nullptr);
  const PlayerID carrierId = carrier->player->getId();
  frames(view, 1);
  ASSERT_EQ(engine.getControlledPlayer(), carrierId);
  capture(view, "play_mode_2d.png");

  // D runs him to the right of the screen, which is +x in the 2D view; he
  // turns within his physics (a second covers a full reversal at speed).
  const float startVelocity = carrier->velocity.x;
  pushKey(SDL_SCANCODE_D, SDLK_D, true);
  frames(view, 8);
  ASSERT_FALSE(engine.getInputLog().empty());
  EXPECT_GT(engine.getInputLog().back().input.moveX, 0.5f);
  frames(view, 56);
  if (const MatchPlayer* runner = findPlayer(engine, carrierId);
      runner && engine.getControlledPlayer() == carrierId)
    EXPECT_GT(runner->velocity.x, std::max(startVelocity, 0.0f) + 0.5f);
  pushKey(SDL_SCANCODE_D, SDLK_D, false);

  // J plays a pass (if he still has the ball, he must pass it now).
  if (managedCarrier(engine, managedHome) &&
      managedCarrier(engine, managedHome)->player->getId() ==
          engine.getControlledPlayer())
  {
    const PlayerID passer = engine.getControlledPlayer();
    const int before = engine.findPlayerStats(passer)->passesAttempted;
    pushKey(SDL_SCANCODE_J, SDLK_J, true);
    frames(view, 2);
    pushKey(SDL_SCANCODE_J, SDLK_J, false);
    frames(view, 16);
    EXPECT_GT(engine.findPlayerStats(passer)->passesAttempted, before);
    bool pressed = false;
    for (const MatchInputRecord& record : engine.getInputLog())
      pressed = pressed || record.input.action == MatchInputAction::PASS;
    EXPECT_TRUE(pressed);
  }

  // Q switches to the next candidate while we do not have the ball.
  for (int step = 0; step < 30'000; ++step)
  {
    if (const MatchPlayer* holder = managedCarrier(engine, managedHome);
        engine.getState() == MatchState::PLAYING &&
        (!holder || holder->player->getId() != engine.getControlledPlayer()) &&
        play.nextSwitch() != 0)
      break;
    engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    frames(view, 1);
  }
  const PlayerID before = engine.getControlledPlayer();
  const PlayerID expected = play.nextSwitch();
  ASSERT_NE(expected, 0u);
  pushKey(SDL_SCANCODE_Q, SDLK_Q, true);
  frames(view, 1);
  pushKey(SDL_SCANCODE_Q, SDLK_Q, false);
  frames(view, 1);
  const PlayerID after = engine.getControlledPlayer();
  EXPECT_NE(after, before);
  // Unless play took the ball to someone else in that frame, it is him.
  if (!managedCarrier(engine, managedHome)) EXPECT_EQ(after, expected);

  // The 3D view follows with the play camera.
  MatchBridge::setView(*scene, MatchViewMode::BROADCAST_3D);
  frames(view, 30);
  capture(view, "play_mode_3d.png");

  // Esc opens the pause menu; the team goes back to the AI from there.
  pushKey(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, true);
  frames(view, 1);
  pushKey(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, false);
  frames(view, 2);
  EXPECT_TRUE(MatchBridge::inMenu(*scene));
  EXPECT_TRUE(MatchBridge::paused(*scene));
  capture(view, "play_mode_menu.png");
  // A second Esc closes it and play goes on; a third opens it again.
  pushKey(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, true);
  frames(view, 1);
  pushKey(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, false);
  frames(view, 2);
  EXPECT_FALSE(MatchBridge::inMenu(*scene));
  EXPECT_FALSE(MatchBridge::paused(*scene));
  EXPECT_FALSE(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
  pushKey(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, true);
  frames(view, 1);
  pushKey(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, false);
  frames(view, 2);
  EXPECT_TRUE(MatchBridge::inMenu(*scene));
  EXPECT_TRUE(MatchBridge::paused(*scene));
  ASSERT_TRUE(MatchBridge::canHandBack(*scene));
  MatchBridge::handBack(*scene);
  frames(view, 2);
  EXPECT_FALSE(play.isActive());
  EXPECT_EQ(engine.getControlledPlayer(), 0u);
  EXPECT_NE(MatchBridge::camera(*scene), MatchCameraMode::PLAY);
  // Keyboard navigation is back for the menus.
  EXPECT_NE(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard, 0);

  // The rest is watched: the match still finishes normally.
  engine.simulateToEnd();
  frames(view, 2);
  EXPECT_EQ(engine.getState(), MatchState::FULL_TIME);
}
