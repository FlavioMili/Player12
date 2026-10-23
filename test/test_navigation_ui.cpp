// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Back and Forward through the real GUIView: sideways touchpad swipes and
// mouse side buttons arrive as SDL events, Alt+arrows as keys, and the
// screen on top follows the history like a browser's. Also the ways out of
// the first screens: Back and Escape on the club choice, Escape on the main
// menu's dialogs, and Settings closing onto the screen it was opened from.

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unistd.h>

#include <cfloat>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "controller/game_controller.h"
#include "global/language_manager.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/main_menu_scene.h"
#include "gui/scenes/management_scene.h"

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
  /** Continue asked for, the hub not yet simulating. */
  static void setContinueRequested(MainGameScene& hub, bool requested)
  {
    hub.continuation_requested = requested;
  }
  static void openSlotPicker(MainMenuScene& menu, bool newGame)
  {
    menu.is_new_game = newGame;
    menu.slot_picker_requested = true;
  }
  static void openBackups(MainMenuScene& menu, int slot)
  {
    menu.openBackups(slot);
  }
  static void askOverwrite(MainMenuScene& menu, int slot)
  {
    menu.overwrite_slot = slot;
  }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;
constexpr std::uint64_t MS = 1'000'000;

int uniqueSlot(int offset)
{
  return 900'000 + getpid() % 100'000 * 10 + offset;
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

SceneID topId(const GUIView& view) { return view.getTopScene()->getID(); }

/**
 * A two-finger swipe as SDL delivers it: @p samples wheel events with a
 * sideways (@p dx) and vertical (@p dy) component, 8 ms apart. Each swipe
 * starts a second after the previous one, so it is a gesture of its own.
 */
class Touchpad
{
 public:
  explicit Touchpad(const GUIView& view)
      : window(SDL_GetWindowID(view.getWindow()))
  {
  }

  void swipe(float dx, float dy, int samples = 12)
  {
    clock += 1'000 * MS;
    for (int index = 0; index < samples; ++index)
    {
      SDL_Event event{.wheel = {}};
      event.wheel.type = SDL_EVENT_MOUSE_WHEEL;
      event.wheel.timestamp = clock;
      event.wheel.windowID = window;
      event.wheel.x = dx;
      event.wheel.y = dy;
      event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
      SDL_PushEvent(&event);
      clock += 8 * MS;
    }
  }

 private:
  SDL_WindowID window;
  std::uint64_t clock{SDL_GetTicksNS()};
};

void sideButton(const GUIView& view, Uint8 button)
{
  for (const bool down : {true, false})
  {
    SDL_Event event{.button = {}};
    event.button.type =
        down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = SDL_GetWindowID(view.getWindow());
    event.button.button = button;
    event.button.down = down;
    event.button.clicks = 1;
    SDL_PushEvent(&event);
  }
}

void pressEscape(GUIView& view)
{
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
  frames(view, 1);
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
  frames(view, 2);
}

void altArrow(GUIView& view, ImGuiKey arrow)
{
  ImGuiIO& io = ImGui::GetIO();
  io.AddKeyEvent(ImGuiMod_Alt, true);
  io.AddKeyEvent(arrow, true);
  frames(view, 1);
  io.AddKeyEvent(arrow, false);
  io.AddKeyEvent(ImGuiMod_Alt, false);
  frames(view, 2);
}
/** Modal dialogs open now (the main menu's slot picker, backups...). */
int openPopups() { return GImGui->OpenPopupStack.Size; }

/**
 * Moves the mouse over the item @p id of the first active window whose name
 * contains @p window_part (scanning @p from the top or the bottom) and
 * clicks it. False when the item never got the hover.
 */
template <typename IdOf>
bool clickItem(GUIView& view, std::string_view window_part, IdOf&& id_of,
               bool from_bottom)
{
  const ImGuiWindow* window = nullptr;
  for (const ImGuiWindow* candidate : GImGui->Windows)
    if (candidate->Active &&
        std::string_view(candidate->Name).contains(window_part))
    {
      window = candidate;
      break;
    }
  if (window == nullptr) return false;
  const ImGuiID item = id_of(*window);
  ImGuiIO& io = ImGui::GetIO();
  const ImRect area(ImMax(window->Rect().Min, ImVec2(0.0f, 0.0f)),
                    ImMin(window->Rect().Max, io.DisplaySize));
  const float step = 8.0f;
  for (int offset_index = 0;
       4.0f + step * static_cast<float>(offset_index) < area.GetHeight();
       ++offset_index)
  {
    const float offset = 4.0f + step * static_cast<float>(offset_index);
    const float y = from_bottom ? area.Max.y - offset : area.Min.y + offset;
    for (int x_index = 0;
         area.Min.x + 4.0f + 2.0f * step * static_cast<float>(x_index) <
         area.Max.x;
         ++x_index)
    {
      const float x =
          area.Min.x + 4.0f + 2.0f * step * static_cast<float>(x_index);
      io.AddMousePosEvent(x, y);
      Bridge::frame(view);
      if (GImGui->HoveredId != item) continue;
      io.AddMouseButtonEvent(0, true);
      Bridge::frame(view);
      io.AddMouseButtonEvent(0, false);
      Bridge::frame(view);
      io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
      Bridge::frame(view);
      return true;
    }
  }
  io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  Bridge::frame(view);
  return false;
}

ImGuiID backToMenuButton(const ImGuiWindow& window)
{
  return ImHashStr(LOC("TEAM_SELECTION_BACK"), 0, window.ID);
}

/** The sidebar's Settings entry (a full row, or an icon when short). */
ImGuiID settingsRow(const ImGuiWindow& window)
{
  return ImHashStr("##nav", 0, ImHashStr(LOC("MENU_SETTINGS"), 0, window.ID));
}
ImGuiID settingsIcon(const ImGuiWindow& window)
{
  return ImHashStr("##footer", 0,
                   ImHashStr(LOC("MENU_SETTINGS"), 0, window.ID));
}
}  // namespace

TEST(NavigationUiTest, SwipesAndSideButtonsWalkTheHistory)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(0)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  const TeamID club = controller.getTeams().front().get().getId();
  controller.selectManagedTeam(club);
  const TeamID rival = controller.getTeams().back().get().getId();
  ASSERT_NE(rival, club);
  const PlayerID target =
      controller.getPlayersForTeam(rival).front().get().getId();

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  ASSERT_EQ(topId(view), SceneID::GAME_MENU);
  EXPECT_FALSE(Navigation::canGoBack(&view)) << "a new career has no past";
  EXPECT_FALSE(Navigation::canGoForward(&view));
  // The mouse is away from every window: nothing on the page claims the
  // wheel for its own sideways scrolling.
  ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  frames(view, 1);

  Navigation::open(&view, NavSection::SQUAD);
  frames(view, 2);
  ASSERT_EQ(topId(view), SceneID::ROSTER);
  Navigation::open(&view, NavSection::TRANSFERS);
  frames(view, 2);
  ASSERT_EQ(topId(view), SceneID::TRANSFER_MARKET);
  const GUIScene* market = view.getTopScene();
  Navigation::openPlayer(&view, target);
  frames(view, 2);
  ASSERT_EQ(topId(view), SceneID::PLAYER_PROFILE);
  EXPECT_EQ(view.getOverlayDepth(), 2U);
  EXPECT_TRUE(Navigation::canGoBack(&view));
  EXPECT_FALSE(Navigation::canGoForward(&view));

  Touchpad touchpad(view);
  // Scrolling the page up and down never navigates.
  touchpad.swipe(0.3f, -2.0f, 20);
  frames(view, 3);
  EXPECT_EQ(topId(view), SceneID::PLAYER_PROFILE);

  // Back #1: swipe right. The profile closes onto the very market screen
  // it was opened from.
  touchpad.swipe(1.5f, 0.0f);
  frames(view, 3);
  ASSERT_EQ(topId(view), SceneID::TRANSFER_MARKET);
  EXPECT_EQ(view.getTopScene(), market);
  EXPECT_TRUE(Navigation::canGoForward(&view));

  // Back #2: mouse button 4.
  sideButton(view, SDL_BUTTON_X1);
  frames(view, 3);
  ASSERT_EQ(topId(view), SceneID::ROSTER);

  // Forward: mouse button 5.
  sideButton(view, SDL_BUTTON_X2);
  frames(view, 3);
  ASSERT_EQ(topId(view), SceneID::TRANSFER_MARKET);

  // Forward again with a swipe to the left: the profile is back on top of
  // the market, so Back closes it again.
  touchpad.swipe(-1.5f, 0.0f);
  frames(view, 3);
  ASSERT_EQ(topId(view), SceneID::PLAYER_PROFILE);
  EXPECT_EQ(view.getOverlayDepth(), 2U);
  EXPECT_FALSE(Navigation::canGoForward(&view));

  // One long swipe is still one step.
  touchpad.swipe(1.5f, 0.0f, 40);
  frames(view, 3);
  EXPECT_EQ(topId(view), SceneID::TRANSFER_MARKET);

  // Alt+Left / Alt+Right.
  altArrow(view, ImGuiKey_LeftArrow);
  EXPECT_EQ(topId(view), SceneID::ROSTER);
  altArrow(view, ImGuiKey_RightArrow);
  EXPECT_EQ(topId(view), SceneID::TRANSFER_MARKET);

  // A new screen after going back drops the forward list.
  sideButton(view, SDL_BUTTON_X1);
  frames(view, 3);
  ASSERT_EQ(topId(view), SceneID::ROSTER);
  Navigation::open(&view, NavSection::INBOX);
  frames(view, 2);
  ASSERT_EQ(topId(view), SceneID::INBOX);
  EXPECT_FALSE(Navigation::canGoForward(&view));
  sideButton(view, SDL_BUTTON_X2);
  frames(view, 3);
  EXPECT_EQ(topId(view), SceneID::INBOX);

  // Back all the way stops at Home and never leaves the career.
  for (int step = 0; step < 6; ++step)
  {
    sideButton(view, SDL_BUTTON_X1);
    frames(view, 3);
  }
  EXPECT_EQ(topId(view), SceneID::GAME_MENU);
  EXPECT_EQ(view.getOverlayDepth(), 0U);
  EXPECT_EQ(view.getTopScene(), view.getBaseScene());
  EXPECT_FALSE(Navigation::canGoBack(&view));

  // Starting (or loading) a career forgets the old one's screens.
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  ASSERT_EQ(topId(view), SceneID::GAME_MENU);
  EXPECT_LE(view.navHistory().size(), 1U);
  EXPECT_FALSE(Navigation::canGoBack(&view));
  EXPECT_FALSE(Navigation::canGoForward(&view));
}

TEST(NavigationUiTest, EdgeCasesKeepTheHistoryConsistent)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(1)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  const TeamID club = controller.getTeams().front().get().getId();
  controller.selectManagedTeam(club);
  const PlayerID own = controller.getPlayersForTeam(club).front().get().getId();

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  ASSERT_EQ(topId(view), SceneID::GAME_MENU);
  ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  frames(view, 1);
  auto* hub = dynamic_cast<MainGameScene*>(view.getBaseScene());
  ASSERT_NE(hub, nullptr);
  const NavEntry squad = NavEntry::ofSection(NavSection::SQUAD);

  // The managed club's list opened as "a club" is the Squad screen: one
  // history entry, whichever way it was reached.
  Navigation::openClub(&view, club);
  frames(view, 2);
  ASSERT_EQ(topId(view), SceneID::ROSTER);
  EXPECT_EQ(view.navHistory().current(), std::optional<NavEntry>(squad));
  const std::size_t entries = view.navHistory().size();
  Navigation::open(&view, NavSection::SQUAD);
  frames(view, 2);
  EXPECT_EQ(view.navHistory().size(), entries);

  // While Continue is under way nothing steps through the history: the
  // hub has to close the screens above it before simulating.
  Navigation::open(&view, NavSection::TRANSFERS);
  frames(view, 2);
  ASSERT_EQ(topId(view), SceneID::TRANSFER_MARKET);
  Bridge::setContinueRequested(*hub, true);
  EXPECT_FALSE(Navigation::canGoBack(&view));
  Navigation::back(&view);
  frames(view, 2);
  EXPECT_EQ(topId(view), SceneID::TRANSFER_MARKET);
  sideButton(view, SDL_BUTTON_X1);
  Touchpad touchpad(view);
  touchpad.swipe(1.5f, 0.0f);
  frames(view, 3);
  EXPECT_EQ(topId(view), SceneID::TRANSFER_MARKET);
  altArrow(view, ImGuiKey_LeftArrow);
  EXPECT_EQ(topId(view), SceneID::TRANSFER_MARKET);
  Bridge::setContinueRequested(*hub, false);
  EXPECT_TRUE(Navigation::canGoBack(&view));

  // Closing a screen the history does not know about is still a step back:
  // Forward brings it back.
  Navigation::open(&view, NavSection::SQUAD);
  frames(view, 2);
  Navigation::openPlayer(&view, own);
  frames(view, 2);
  ASSERT_EQ(topId(view), SceneID::PLAYER_PROFILE);
  view.navHistory().clear();
  view.navHistory().visit(NavEntry::ofPlayer(own));
  sideButton(view, SDL_BUTTON_X1);
  frames(view, 3);
  ASSERT_EQ(topId(view), SceneID::ROSTER);
  EXPECT_EQ(view.navHistory().current(), std::optional<NavEntry>(squad));
  EXPECT_TRUE(Navigation::canGoForward(&view));
  sideButton(view, SDL_BUTTON_X2);
  frames(view, 3);
  EXPECT_EQ(topId(view), SceneID::PLAYER_PROFILE);
  EXPECT_EQ(view.getOverlayDepth(), 2U);

  // Esc on the Finances page of the hub goes Home; on Home it does nothing.
  Navigation::open(&view, NavSection::FINANCES);
  frames(view, 2);
  ASSERT_EQ(view.getOverlayDepth(), 0U);
  ASSERT_EQ(view.getTopScene()->historyEntry(),
            std::optional<NavEntry>(NavEntry::ofSection(NavSection::FINANCES)));
  pressEscape(view);
  const std::optional<NavEntry> home = NavEntry::ofSection(NavSection::HOME);
  EXPECT_EQ(view.getTopScene()->historyEntry(), home);
  pressEscape(view);
  EXPECT_EQ(view.getTopScene()->historyEntry(), home);
  EXPECT_EQ(view.getOverlayDepth(), 0U);
}

TEST(NavigationUiTest, ClubSelectionGoesBackToTheMainMenu)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::EN));
  const SlotCleanup slot{uniqueSlot(2)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  ASSERT_FALSE(controller.hasSelectedTeam());

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 3);
  ASSERT_EQ(topId(view), SceneID::TEAM_SELECTION);

  // Escape leaves the club choice for the main menu; the new save stays
  // in its slot as a career not started yet.
  pressEscape(view);
  EXPECT_EQ(topId(view), SceneID::MAIN_MENU);
  EXPECT_EQ(view.getOverlayDepth(), 0U);
  const GameController::SaveSlotMetadata metadata =
      controller.getSaveSlotMetadata(slot.slot);
  EXPECT_TRUE(metadata.exists);
  EXPECT_TRUE(metadata.team_name.empty());

  // The Back button does the same.
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 3);
  ASSERT_EQ(topId(view), SceneID::TEAM_SELECTION);
  ASSERT_TRUE(clickItem(view, "##team_selection", backToMenuButton, false));
  frames(view, 2);
  EXPECT_EQ(topId(view), SceneID::MAIN_MENU);
}

TEST(NavigationUiTest, MainMenuDialogsCloseWithEscapeOneAtATime)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::EN));
  GameController controller;
  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainMenuScene>(&view));
  frames(view, 2);
  auto* menu = dynamic_cast<MainMenuScene*>(view.getTopScene());
  ASSERT_NE(menu, nullptr);
  ASSERT_EQ(openPopups(), 0);

  // The slot picker.
  Bridge::openSlotPicker(*menu, true);
  frames(view, 2);
  ASSERT_EQ(openPopups(), 1);
  pressEscape(view);
  EXPECT_EQ(openPopups(), 0);

  // The backups list on its own.
  Bridge::openBackups(*menu, 1);
  frames(view, 2);
  ASSERT_EQ(openPopups(), 1);
  pressEscape(view);
  EXPECT_EQ(openPopups(), 0);

  // Backups opened from the picker: Escape closes the list, then the
  // picker.
  Bridge::openSlotPicker(*menu, false);
  frames(view, 2);
  Bridge::openBackups(*menu, 1);
  frames(view, 2);
  ASSERT_EQ(openPopups(), 2);
  pressEscape(view);
  EXPECT_EQ(openPopups(), 1);
  pressEscape(view);
  EXPECT_EQ(openPopups(), 0);

  // An overwrite confirmation above the picker closes first as well.
  Bridge::openSlotPicker(*menu, true);
  frames(view, 2);
  Bridge::askOverwrite(*menu, 1);
  frames(view, 2);
  ASSERT_EQ(openPopups(), 2);
  pressEscape(view);
  EXPECT_EQ(openPopups(), 1);
  pressEscape(view);
  EXPECT_EQ(openPopups(), 0);
  EXPECT_EQ(topId(view), SceneID::MAIN_MENU);
}

TEST(NavigationUiTest, SettingsReturnsToTheScreenItWasOpenedFrom)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::EN));
  const SlotCleanup slot{uniqueSlot(3)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  const TeamID club = controller.getTeams().front().get().getId();
  controller.selectManagedTeam(club);
  const PlayerID own = controller.getPlayersForTeam(club).front().get().getId();

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  Navigation::open(&view, NavSection::TRANSFERS);
  frames(view, 2);
  Navigation::openPlayer(&view, own);
  frames(view, 2);
  ASSERT_EQ(topId(view), SceneID::PLAYER_PROFILE);
  ASSERT_EQ(view.getOverlayDepth(), 2U);
  const std::optional<NavEntry> before = view.navHistory().current();

  // The sidebar's Settings entry stacks Settings over the profile...
  const bool clicked = clickItem(view, "##sidebar", settingsRow, true) ||
                       clickItem(view, "##sidebar", settingsIcon, true);
  ASSERT_TRUE(clicked);
  frames(view, 2);
  ASSERT_EQ(topId(view), SceneID::SETTINGS);
  EXPECT_EQ(view.getOverlayDepth(), 3U);

  // ...and leaving it (Escape) shows the profile again, over the market,
  // with the history where it was.
  pressEscape(view);
  EXPECT_EQ(topId(view), SceneID::PLAYER_PROFILE);
  EXPECT_EQ(view.getOverlayDepth(), 2U);
  EXPECT_EQ(view.getSceneBelowTop()->getID(), SceneID::TRANSFER_MARKET);
  EXPECT_EQ(view.navHistory().current(), before);
}
