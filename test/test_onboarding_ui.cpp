// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Onboarding, controls and accessibility: the welcome tour of a new career
// and the Help screen through the real GUIView, rebindable shortcuts
// (conflicts, persistence, reset), WCAG contrast of every theme and a scan
// of the GUI sources for English text that bypasses the language files.

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "controller/game_controller.h"
#include "global/language_manager.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/input_actions.h"
#include "gui/scenes/help_scene.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/settings_scene.h"
#include "gui/scenes/team_selection_scene.h"
#include "gui/widgets/theme.h"
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
  static void startCareer(TeamSelectionScene& scene, TeamID team)
  {
    scene.startCareer(team);
  }
  static void captureSlot(SettingsScene& scene, Input::ActionId action,
                          std::size_t slot)
  {
    scene.capture = SettingsScene::Capture{action, slot};
  }
  static bool hasConflict(const SettingsScene& scene)
  {
    return scene.conflict.has_value();
  }
  static Input::ActionId conflictWith(const SettingsScene& scene)
  {
    return scene.conflict ? scene.conflict->other : Input::ActionId{};
  }
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

/** Restores the default bindings whatever a test did. */
struct BindingsCleanup
{
  BindingsCleanup() = default;
  BindingsCleanup(const BindingsCleanup&) = delete;
  BindingsCleanup& operator=(const BindingsCleanup&) = delete;

  ~BindingsCleanup()
  {
    Input::registry().resetAll();
    SettingsManager::instance()->get() = Settings{};
  }
};

void frames(GUIView& view, int count)
{
  for (int index = 0; index < count; ++index) Bridge::frame(view);
}

void capture(GUIView& view, const char* name)
{
  const auto path = RuntimePaths::capturePath(name);
  std::filesystem::remove(path);
  EXPECT_TRUE(view.captureScreenshot(path.string())) << name;
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

void pressKey(GUIView& view, ImGuiKey key, ImGuiKeyChord mods = 0)
{
  ImGuiIO& io = ImGui::GetIO();
  // Inject logical test chords consistently: ImGui swaps physical Ctrl and
  // Super events when macOS behavior is enabled.
  const ImGuiKey control =
      io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
  if ((mods & ImGuiMod_Ctrl) != 0) io.AddKeyEvent(control, true);
  if ((mods & ImGuiMod_Shift) != 0) io.AddKeyEvent(ImGuiMod_Shift, true);
  if ((mods & ImGuiMod_Alt) != 0) io.AddKeyEvent(ImGuiMod_Alt, true);
  io.AddKeyEvent(key, true);
  Bridge::frame(view);
  io.AddKeyEvent(key, false);
  if ((mods & ImGuiMod_Ctrl) != 0) io.AddKeyEvent(control, false);
  if ((mods & ImGuiMod_Shift) != 0) io.AddKeyEvent(ImGuiMod_Shift, false);
  if ((mods & ImGuiMod_Alt) != 0) io.AddKeyEvent(ImGuiMod_Alt, false);
  Bridge::frame(view);
}

/** An active window whose name contains @p part (children included). */
const ImGuiWindow* findWindow(std::string_view part)
{
  const ImGuiWindow* found = nullptr;
  for (const ImGuiWindow* window : GImGui->Windows)
    if (window->Active && std::string_view(window->Name).contains(part))
      found = window;
  return found;
}

/**
 * Presses the widget with ID @p id inside @p window through ImGui's own
 * activation (what a click or Enter on the focused widget does); the
 * widget must be drawn on the next frame for it to take effect.
 */
bool clickItem(GUIView& view, const ImGuiWindow* window, ImGuiID id)
{
  if (window == nullptr) return false;
  ImGui::ActivateItemByID(id);
  Bridge::frame(view);
  Bridge::frame(view);
  return true;
}

/** Clicks a button drawn directly in @p window (no ID pushed). */
bool clickButton(GUIView& view, std::string_view windowPart, const char* label)
{
  // An exact (top-level) name first, else a child window containing it.
  const ImGuiWindow* window =
      ImGui::FindWindowByName(std::string(windowPart).c_str());
  if (window == nullptr || !window->Active) window = findWindow(windowPart);
  if (window == nullptr) return false;
  return clickItem(view, window, ImHashStr(label, 0, window->ID));
}

bool welcomeOpen(const GUIView& view)
{
  const auto* hub = dynamic_cast<const MainGameScene*>(view.getBaseScene());
  return hub != nullptr && hub->welcomeTour().isOpen();
}

std::size_t welcomePage(const GUIView& view)
{
  const auto* hub = dynamic_cast<const MainGameScene*>(view.getBaseScene());
  return hub != nullptr ? hub->welcomeTour().page() : 0;
}
}  // namespace

// ---- Welcome tour and Help
// ---------------------------------------------------

namespace
{
/** New career through the club choice: the tour, Help and the checklist. */
void welcomeJourney(GameController& controller);
}  // namespace

TEST(OnboardingUiTest, WelcomeTourOfANewCareerAndHelpScreen)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const BindingsCleanup bindings;
  const SlotCleanup slot{uniqueSlot(0)};
  SettingsManager::instance()->get().screen_tips = false;
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  welcomeJourney(controller);
  if (HasFatalFailure()) return;

  // The tour belongs to starting a career: a reloaded save never shows it
  // (one GUIView at a time: each owns the ImGui context).
  ASSERT_TRUE(controller.saveGame());
  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  GUIView second(reloaded);
  ASSERT_TRUE(Bridge::initialize(second));
  second.changeScene(std::make_unique<MainGameScene>(&second));
  frames(second, 3);
  EXPECT_EQ(Bridge::activeScene(second)->getID(), SceneID::GAME_MENU);
  EXPECT_FALSE(welcomeOpen(second));
}

namespace
{
void welcomeJourney(GameController& controller)
{
  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  resize(view, 1280, 720);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 3);
  // A new career opens the club choice above Home.
  auto* selection =
      dynamic_cast<TeamSelectionScene*>(Bridge::activeScene(view));
  ASSERT_NE(selection, nullptr);
  const TeamID club = controller.getTeams().front().get().getId();
  Bridge::startCareer(*selection, club);
  frames(view, 3);
  ASSERT_EQ(Bridge::activeScene(view)->getID(), SceneID::GAME_MENU);
  ASSERT_TRUE(welcomeOpen(view));
  EXPECT_EQ(welcomePage(view), 0U);
  auto* hub = dynamic_cast<MainGameScene*>(view.getBaseScene());
  ASSERT_NE(hub, nullptr);
  const std::string clubName = controller.getTeamById(club)->get().getName();
  EXPECT_NE(hub->welcomeTour().body(0).find(clubName), std::string::npos);
  for (std::size_t page = 0; page < WelcomeTour::PAGE_COUNT; ++page)
  {
    EXPECT_FALSE(hub->welcomeTour().body(page).empty()) << page;
    EXPECT_EQ(hub->welcomeTour().body(page).find('{'), std::string::npos)
        << "unsubstituted placeholder on page " << page;
  }
  capture(view, "welcome_club.bmp");

  // Next, Next, Back, Next... through the real buttons.
  ASSERT_TRUE(clickButton(view, "###welcome_tour", LOC("WELCOME_NEXT")));
  EXPECT_EQ(welcomePage(view), 1U);
  capture(view, "welcome_board.bmp");
  ASSERT_TRUE(clickButton(view, "###welcome_tour", LOC("WELCOME_NEXT")));
  EXPECT_EQ(welcomePage(view), 2U);
  capture(view, "welcome_next.bmp");
  ASSERT_TRUE(clickButton(view, "###welcome_tour", LOC("WELCOME_BACK")));
  EXPECT_EQ(welcomePage(view), 1U);
  ASSERT_TRUE(clickButton(view, "###welcome_tour", LOC("WELCOME_NEXT")));
  ASSERT_TRUE(clickButton(view, "###welcome_tour", LOC("WELCOME_NEXT")));
  EXPECT_EQ(welcomePage(view), 3U);
  capture(view, "welcome_continue.bmp");
  ASSERT_TRUE(clickButton(view, "###welcome_tour", LOC("WELCOME_START")));
  EXPECT_FALSE(welcomeOpen(view));
  EXPECT_FALSE(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));

  // The Help shortcut opens Help; it lists the glossary and replays the tour.
  pressKey(view, ImGuiKey_F8);
  frames(view, 2);
  ASSERT_EQ(Bridge::activeScene(view)->getID(), SceneID::HELP);
  capture(view, "help_screen.bmp");
  ASSERT_TRUE(clickButton(view, "help_start", LOC("HELP_REPLAY_TOUR")));
  frames(view, 3);
  EXPECT_EQ(Bridge::activeScene(view)->getID(), SceneID::GAME_MENU);
  ASSERT_TRUE(welcomeOpen(view));
  ASSERT_TRUE(clickButton(view, "###welcome_tour", LOC("WELCOME_SKIP")));
  EXPECT_FALSE(welcomeOpen(view));

  // A hidden checklist comes back from Help.
  controller.dismissOnboarding();
  view.navigateTo(std::make_unique<HelpScene>(&view, true));
  frames(view, 3);
  ASSERT_TRUE(clickButton(view, "help_start", LOC("SETTINGS_CHECKLIST_SHOW")));
  frames(view, 2);
  EXPECT_FALSE(controller.getOnboarding().isDismissed());
  EXPECT_EQ(Bridge::activeScene(view)->getID(), SceneID::GAME_MENU);

  // Help at UI scale 2 on a HiDPI-sized window, and Escape leaves it.
  resize(view, 2560, 1440);
  SettingsManager::instance()->get().ui_scale = 2.0f;
  view.refreshTheme();
  view.navigateTo(std::make_unique<HelpScene>(&view, true, 3));
  frames(view, 3);
  capture(view, "help_screen_hidpi.bmp");
  pressKey(view, ImGuiKey_Escape);
  frames(view, 2);
  EXPECT_EQ(Bridge::activeScene(view)->getID(), SceneID::GAME_MENU);
  SettingsManager::instance()->get().ui_scale = 0.0f;
  view.refreshTheme();
}
}  // namespace

// ---- Rebindable shortcuts
// ----------------------------------------------------

TEST(InputActionsTest, ChordNamesRoundTrip)
{
  for (const ImGuiKeyChord chord :
       {ImGuiKeyChord{ImGuiKey_F1}, ImGuiMod_Ctrl | ImGuiKey_K,
        ImGuiMod_Shift | ImGuiKey_Minus, ImGuiMod_Alt | ImGuiKey_LeftArrow,
        ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Keypad5,
        ImGuiKeyChord{ImGuiKey_Space}})
  {
    const std::string name = Input::chordToString(chord);
    ASSERT_FALSE(name.empty());
    const auto parsed = Input::chordFromString(name);
    ASSERT_TRUE(parsed.has_value()) << name;
    EXPECT_EQ(*parsed, chord) << name;
  }
  EXPECT_EQ(Input::chordToString(ImGuiMod_Ctrl | ImGuiKey_K), "Ctrl+K");
  EXPECT_EQ(Input::chordLabel(ImGuiMod_Alt | ImGuiKey_LeftArrow), "Alt+Left");
  EXPECT_EQ(*Input::chordFromString(""), ImGuiKey_None);
  EXPECT_FALSE(Input::chordFromString("Hyper+K").has_value());
  EXPECT_FALSE(Input::chordFromString("Ctrl+").has_value());
  EXPECT_FALSE(Input::chordFromString("NotAKey").has_value());
}

TEST(InputActionsTest, DefaultsHaveNoConflicts)
{
  const BindingsCleanup cleanup;
  Input::ActionRegistry& registry = Input::registry();
  registry.resetAll();
  ASSERT_GE(registry.size(), 40U);
  for (Input::ActionId id = 0; id < registry.size(); ++id)
    for (std::size_t slot = 0; slot < Input::BINDING_SLOTS; ++slot)
    {
      const auto clash =
          registry.conflictFor(id, registry.chord(id, slot), slot);
      EXPECT_FALSE(clash.has_value())
          << registry.action(id).def.id << " clashes with "
          << (clash ? registry.action(clash->first).def.id : "");
    }
  // Space is Continue on the career screens and Pause in a match.
  EXPECT_EQ(registry.chord(*registry.find(Input::Ids::CAREER_CONTINUE)),
            ImGuiKey_Space);
  EXPECT_EQ(registry.chord(*registry.find(Input::Ids::MATCH_PAUSE)),
            ImGuiKey_Space);
}

TEST(InputActionsTest, RebindRefusesClashesUnlessReplacedAndPersists)
{
  const BindingsCleanup cleanup;
  Input::ActionRegistry& registry = Input::registry();
  registry.resetAll();
  const Input::ActionId home = *registry.find(Input::Ids::NAV_HOME);
  const Input::ActionId inbox = *registry.find(Input::Ids::NAV_INBOX);

  // F1 belongs to Home: refused, nothing changes.
  Input::RebindResult result = registry.rebind(inbox, 0, ImGuiKey_F1);
  EXPECT_FALSE(result.applied);
  ASSERT_TRUE(result.conflict.has_value());
  EXPECT_EQ(*result.conflict, home);
  EXPECT_EQ(registry.chord(inbox), ImGuiKey_F2);
  // Taken over: Home loses it.
  result = registry.rebind(inbox, 0, ImGuiKey_F1, true);
  EXPECT_TRUE(result.applied);
  EXPECT_EQ(registry.chord(inbox), ImGuiKey_F1);
  EXPECT_EQ(registry.chord(home), ImGuiKey_None);
  // A chord used in a match only does not clash with a career action.
  EXPECT_TRUE(registry.rebind(home, 1, ImGuiKey_V).applied);
  // Fixed actions and unbindable keys are refused.
  EXPECT_FALSE(
      registry.rebind(*registry.find(Input::Ids::SCREENSHOT), 0, ImGuiKey_F11)
          .applied);
  EXPECT_FALSE(registry.rebind(home, 0, ImGuiKey_MouseLeft).applied);

  // Stored by name and read back from the settings file.
  const auto& stored = SettingsManager::instance()->get().key_bindings;
  ASSERT_TRUE(stored.contains(std::string(Input::Ids::NAV_INBOX)));
  EXPECT_EQ(stored.at(std::string(Input::Ids::NAV_INBOX)).front(), "F1");
  SettingsManager::instance()->save();
  SettingsManager::instance()->get() = Settings{};
  SettingsManager::instance()->load();
  registry.reloadFromSettings();
  EXPECT_EQ(registry.chord(inbox), ImGuiKey_F1);
  EXPECT_EQ(registry.chord(home), ImGuiKey_None);
  EXPECT_EQ(registry.chord(home, 1), ImGuiKey_V);

  // A broken name keeps the default instead of unbinding.
  SettingsManager::instance()
      ->get()
      .key_bindings[std::string(Input::Ids::NAV_CLUB)] = {"NotAKey", ""};
  registry.reloadFromSettings();
  EXPECT_EQ(registry.chord(*registry.find(Input::Ids::NAV_CLUB)), ImGuiKey_F7);

  registry.resetAll();
  EXPECT_TRUE(SettingsManager::instance()->get().key_bindings.empty());
  EXPECT_EQ(registry.chord(home), ImGuiKey_F1);
  EXPECT_EQ(registry.chord(inbox), ImGuiKey_F2);
  EXPECT_TRUE(registry.isDefault(home));
}

TEST(InputActionsTest, LaterRegisteredActionsPickUpStoredBindings)
{
  const BindingsCleanup cleanup;
  Input::ActionRegistry& registry = Input::registry();
  SettingsManager::instance()->get().key_bindings["test.sprint"] = {"Shift+W",
                                                                    ""};
  const Input::ActionDef def{"test.sprint", "ACTION_HELP",
                             Input::Category::PLAY, Input::Context::PLAY,
                             ImGuiKey_E};
  const Input::ActionId sprint = registry.registerAction(def);
  EXPECT_EQ(registry.registerAction(def), sprint);
  EXPECT_EQ(registry.chord(sprint), ImGuiMod_Shift | ImGuiKey_W);
  // Play controls clash with each other and with global keys only: they
  // replace the manager's match keys while the team is controlled.
  const Input::ActionId pass = registry.registerAction(
      {"test.pass", "ACTION_HELP", Input::Category::PLAY, Input::Context::PLAY,
       ImGuiKey_U});
  const auto clash = registry.conflictFor(sprint, ImGuiKey_U, 0);
  ASSERT_TRUE(clash.has_value());
  EXPECT_EQ(clash->first, pass);
  EXPECT_TRUE(registry.conflictFor(sprint, ImGuiKey_F12, 0).has_value());
  // T only means something to the manager (tactics), never on the pitch.
  EXPECT_FALSE(registry.conflictFor(sprint, ImGuiKey_T, 0).has_value());
  EXPECT_FALSE(registry.conflictFor(sprint, ImGuiKey_F3, 0).has_value());
}

TEST(InputActionsTest, KeyEventsMatchByCharacterOrPhysicalKey)
{
  const BindingsCleanup cleanup;
  Input::ActionRegistry& registry = Input::registry();
  registry.resetAll();
  SDL_KeyboardEvent event{};
  event.type = SDL_EVENT_KEY_DOWN;
  // Shift + the key left of 2 on an AZERTY keyboard types '1' but sends '&'.
  event.key = SDLK_AMPERSAND;
  event.scancode = SDL_SCANCODE_1;
  event.mod = SDL_KMOD_LSHIFT;
  EXPECT_TRUE(registry.matches(Input::Ids::shout(0), event));
  EXPECT_FALSE(registry.matches(Input::Ids::CAMERA_BROADCAST, event));
  event.mod = SDL_KMOD_NONE;
  event.key = SDLK_1;
  EXPECT_TRUE(registry.matches(Input::Ids::CAMERA_BROADCAST, event));
  event.key = SDLK_S;
  event.scancode = SDL_SCANCODE_S;
  EXPECT_TRUE(registry.matches(Input::Ids::MATCH_SUBSTITUTIONS, event));
  event.mod = SDL_KMOD_LCTRL;
  EXPECT_FALSE(registry.matches(Input::Ids::MATCH_SUBSTITUTIONS, event));
}

TEST(OnboardingUiTest, SettingsControlsRebindWithConflictPersistAndReset)
{
  for (const bool macBehavior : {false, true})
  {
    SCOPED_TRACE(macBehavior ? "macOS modifiers" : "standard modifiers");
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    Logger::init();
    const BindingsCleanup cleanup;
    Input::registry().resetAll();
    GameController controller;
    GUIView view(controller);
    ASSERT_TRUE(Bridge::initialize(view));
    ImGui::GetIO().ConfigMacOSXBehaviors = macBehavior;
    resize(view, 1280, 720);
    view.changeScene(std::make_unique<SettingsScene>(&view));
    frames(view, 3);
    auto* settings = dynamic_cast<SettingsScene*>(Bridge::activeScene(view));
    ASSERT_NE(settings, nullptr);
    capture(view, "settings_controls.bmp");

    Input::ActionRegistry& registry = Input::registry();
    const Input::ActionId home = *registry.find(Input::Ids::NAV_HOME);
    const Input::ActionId inbox = *registry.find(Input::Ids::NAV_INBOX);

    // Waiting for a key: F1 is taken by Home, so the screen asks first.
    Bridge::captureSlot(*settings, inbox, 0);
    frames(view, 1);
    pressKey(view, ImGuiKey_F1);
    EXPECT_TRUE(Bridge::hasConflict(*settings));
    EXPECT_EQ(Bridge::conflictWith(*settings), home);
    EXPECT_EQ(registry.chord(inbox), ImGuiKey_F2);
    // Escape while a key is awaited cancels the capture, not the screen.
    Bridge::captureSlot(*settings, inbox, 1);
    frames(view, 1);
    pressKey(view, ImGuiKey_Escape);
    EXPECT_EQ(Bridge::activeScene(view)->getID(), SceneID::SETTINGS);
    EXPECT_EQ(registry.chord(inbox, 1), ImGuiKey_None);

    // A free combination binds at once.
    Bridge::captureSlot(*settings, inbox, 1);
    frames(view, 1);
    pressKey(view, ImGuiKey_I, ImGuiMod_Ctrl);
    EXPECT_EQ(registry.chord(inbox, 1), ImGuiMod_Ctrl | ImGuiKey_I);
    frames(view, 2);
    capture(view, "settings_controls_rebound.bmp");

    // Apply & Back writes the file; a fresh load keeps the binding.
    ASSERT_TRUE(clickButton(view, "##settings", LOC("SETTINGS_APPLY")));
    frames(view, 2);
    EXPECT_EQ(Bridge::activeScene(view)->getID(), SceneID::MAIN_MENU);
    SettingsManager::instance()->get() = Settings{};
    SettingsManager::instance()->load();
    registry.reloadFromSettings();
    EXPECT_EQ(registry.chord(inbox, 1), ImGuiMod_Ctrl | ImGuiKey_I);

    // Reset to defaults puts every key back; Apply saves it.
    view.changeScene(std::make_unique<SettingsScene>(&view));
    frames(view, 3);
    ASSERT_TRUE(clickButton(view, "##settings", LOC("SETTINGS_RESET")));
    EXPECT_TRUE(registry.isDefault(inbox));
    ASSERT_TRUE(clickButton(view, "##settings", LOC("SETTINGS_APPLY")));
    frames(view, 2);
    SettingsManager::instance()->get() = Settings{};
    SettingsManager::instance()->load();
    EXPECT_TRUE(SettingsManager::instance()->get().key_bindings.empty());

    // Cancel drops bindings changed on the screen.
    view.changeScene(std::make_unique<SettingsScene>(&view));
    frames(view, 3);
    settings = dynamic_cast<SettingsScene*>(Bridge::activeScene(view));
    ASSERT_NE(settings, nullptr);
    Bridge::captureSlot(*settings, home, 1);
    frames(view, 1);
    pressKey(view, ImGuiKey_H, ImGuiMod_Ctrl);
    EXPECT_EQ(registry.chord(home, 1), ImGuiMod_Ctrl | ImGuiKey_H);
    ASSERT_TRUE(clickButton(view, "##settings", LOC("SETTINGS_CANCEL")));
    frames(view, 2);
    EXPECT_EQ(registry.chord(home, 1), ImGuiKey_None);

    // The whole screen at UI scale 2 and with the larger text.
    resize(view, 2560, 1440);
    SettingsManager::instance()->get().ui_scale = 2.0f;
    SettingsManager::instance()->get().text_scale = 1.3f;
    SettingsManager::instance()->get().color_vision = 1;
    view.refreshTheme();
    view.changeScene(std::make_unique<SettingsScene>(&view));
    frames(view, 3);
    capture(view, "settings_hidpi_large_text.bmp");
    SettingsManager::instance()->get() = Settings{};
    view.refreshTheme();
  }
}

// ---- Contrast
// ----------------------------------------------------------------

TEST(AccessibilityTest, EveryThemeMeetsWcagContrast)
{
  const ImVec4 accent(0.130f, 0.650f, 0.390f, 1.0f);
  constexpr float BODY_TEXT = 4.5f;  // WCAG AA, normal text
  constexpr float NON_TEXT = 3.0f;   // WCAG AA, large text and UI parts
  for (int preset = 0;
       preset < static_cast<int>(std::to_underlying(Theme::Preset::COUNT));
       ++preset)
    for (int vision = 0;
         vision <
         static_cast<int>(std::to_underlying(Theme::ColorVision::COUNT));
         ++vision)
    {
      const Theme::Palette p =
          Theme::presetPalette(static_cast<Theme::Preset>(preset),
                               static_cast<Theme::ColorVision>(vision), accent);
      const std::string where = std::format(
          "{} / vision {}",
          Theme::presetKey(static_cast<Theme::Preset>(preset)), vision);
      for (const ImVec4* ground : {&p.background, &p.surface, &p.raised})
      {
        EXPECT_GE(Theme::contrastRatio(p.text, *ground), BODY_TEXT) << where;
        EXPECT_GE(Theme::contrastRatio(p.muted, *ground), BODY_TEXT) << where;
        EXPECT_GE(Theme::contrastRatio(p.faint, *ground), NON_TEXT) << where;
        for (const ImVec4* status :
             {&p.positive, &p.warning, &p.negative, &p.info})
          EXPECT_GE(Theme::contrastRatio(*status, *ground), NON_TEXT) << where;
      }
      for (const ImVec4* status :
           {&p.positive, &p.warning, &p.negative, &p.info})
        EXPECT_GE(Theme::contrastRatio(*status, p.background), BODY_TEXT)
            << where;
      // The safe modes tell good from bad on the axis the player still sees:
      // blue against orange for red-green, red against teal for blue-yellow.
      if (vision == 1)
        EXPECT_GT(std::abs(p.positive.z - p.negative.z), 0.3f) << where;
      if (vision == 2)
        EXPECT_GT(std::abs(p.positive.x - p.negative.x), 0.3f) << where;
    }
  // The ratio itself: black on white is 21:1.
  EXPECT_NEAR(Theme::contrastRatio(ImVec4(0, 0, 0, 1), ImVec4(1, 1, 1, 1)),
              21.0f, 0.01f);
}

// ---- Hard-coded English in GUI code
// --------------------------------------------

namespace
{
/** Source with comments removed and #ifdef DEBUG blocks blanked. */
std::string preparedSource(const std::filesystem::path& path)
{
  std::ifstream in(path);
  std::stringstream buffer;
  buffer << in.rdbuf();
  const std::string text = buffer.str();
  std::string out;
  out.reserve(text.size());
  int debugDepth = 0;
  int depth = 0;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line))
  {
    const std::string trimmed = line.substr(
        std::min(line.size(), line.find_first_not_of(" \t") == std::string::npos
                                  ? line.size()
                                  : line.find_first_not_of(" \t")));
    if (trimmed.starts_with("#if"))
    {
      ++depth;
      if (trimmed.starts_with("#ifdef DEBUG") && debugDepth == 0)
        debugDepth = depth;
    }
    else if (trimmed.starts_with("#endif"))
    {
      if (debugDepth == depth) debugDepth = 0;
      --depth;
      out += '\n';
      continue;
    }
    const bool comment = trimmed.starts_with("//") ||
                         trimmed.starts_with("*") || trimmed.starts_with("/*");
    out += (debugDepth != 0 || comment) ? std::string() : line;
    out += '\n';
  }
  return out;
}

/** Arguments of the call whose '(' is at @p open, split at top level. */
std::vector<std::string> callArguments(const std::string& text,
                                       std::size_t open)
{
  std::vector<std::string> args(1);
  int depth = 0;
  for (std::size_t index = open; index < text.size(); ++index)
  {
    const char c = text[index];
    if (c == '"')
    {
      const std::size_t start = index;
      for (++index; index < text.size() && text[index] != '"'; ++index)
        if (text[index] == '\\') ++index;
      if (depth >= 1) args.back() += text.substr(start, index - start + 1);
      continue;
    }
    if (c == '(' || c == '[' || c == '{')
    {
      if (depth++ == 0) continue;
    }
    else if (c == ')' || c == ']' || c == '}')
    {
      if (--depth == 0) break;
    }
    else if (c == ',' && depth == 1)
    {
      args.emplace_back();
      continue;
    }
    args.back() += c;
  }
  return args;
}

/** Text a player would read: words, not ids, keys or format strings. */
bool looksLikeEnglish(std::string literal)
{
  if (static const std::regex KEY(R"(^[A-Z0-9_]+$)");
      std::regex_match(literal, KEY))
    return false;
  literal = literal.substr(0, literal.find("##"));
  static const std::regex FORMAT(
      R"(%[-+ #0]*[0-9*]*(?:\.[0-9*]+)?(?:hh|h|ll|l|z|j|t)?[a-zA-Z%]|\{[^}]*\})");
  literal = std::regex_replace(literal, FORMAT, " ");
  static const std::regex CAPITALISED(R"([A-Z][a-z]{1,})");
  static const std::regex PHRASE(R"([A-Za-z]{2,}[ ]+[A-Za-z]{2,})");
  return std::regex_search(literal, CAPITALISED) ||
         std::regex_search(literal, PHRASE);
}
}  // namespace

TEST(LocalizationScanTest, GuiCodeHasNoHardCodedEnglish)
{
  // Widgets that show their text argument(s). For the UI helpers that take
  // an id first, that argument is skipped (see SKIP_FIRST / link()).
  static const std::regex CALL(
      R"(\b(ImGui::(?:Text|TextUnformatted|TextColored|TextWrapped|TextDisabled|BulletText|LabelText|Button|SmallButton|Checkbox|RadioButton|Selectable|MenuItem|BeginMenu|BeginTabItem|TabItemButton|SeparatorText|SetTooltip|CollapsingHeader|TreeNode|TableSetupColumn|TextLink|InputTextWithHint)|UI::(?:primaryButton|secondaryButton|dangerButton|toggleButton|pageHeader|sectionLabel|emptyState|badge|textFitted|keyValue|summaryRow|textRight|textRightColored|link|beginCard|beginAutoHeightCard|statTile|confirmDialog))\s*\()");
  static const std::set<std::string, std::less<>> SKIP_FIRST = {
      "UI::beginCard", "UI::beginAutoHeightCard", "UI::statTile",
      "UI::confirmDialog", "ImGui::InputTextWithHint"};
  const std::filesystem::path root =
      std::filesystem::path(FM_SOURCE_DIR) / "src/gui";
  ASSERT_TRUE(std::filesystem::exists(root));
  std::vector<std::string> findings;
  std::size_t scanned = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
  {
    if (entry.path().extension() != ".cpp") continue;
    ++scanned;
    const std::string text = preparedSource(entry.path());
    for (auto it = std::sregex_iterator(text.begin(), text.end(), CALL);
         it != std::sregex_iterator(); ++it)
    {
      const std::string function = (*it)[1].str();
      const std::size_t open =
          static_cast<std::size_t>(it->position(0) + it->length(0) - 1);
      const std::vector<std::string> args = callArguments(text, open);
      for (std::size_t index = 0; index < args.size(); ++index)
      {
        if (index == 0 && SKIP_FIRST.contains(function)) continue;
        if (index == 1 && function == "UI::link") continue;
        // Literals passed through the language files are fine.
        static const std::regex LOCALISED(
            R"((LOC|formatLocalized|plural)\s*\()");
        if (std::regex_search(args[index], LOCALISED)) continue;
        static const std::regex LITERAL(R"lit("((?:[^"\\]|\\.)*)")lit");
        for (auto lit = std::sregex_iterator(args[index].begin(),
                                             args[index].end(), LITERAL);
             lit != std::sregex_iterator(); ++lit)
          if (looksLikeEnglish((*lit)[1].str()))
          {
            const auto line =
                std::count(text.begin(), text.begin() + it->position(0), '\n') +
                1;
            findings.push_back(
                std::filesystem::relative(entry.path(), root).string() + ":" +
                std::to_string(line) + " " + function + " \"" +
                (*lit)[1].str() + "\"");
          }
      }
    }
  }
  EXPECT_GT(scanned, 40U);
  std::string list;
  for (const std::string& finding : findings) list += "\n  " + finding;
  EXPECT_TRUE(findings.empty())
      << "Text shown without the language files (use LOC):" << list;
}

// The scanner itself: it must catch what it is meant to catch.
TEST(LocalizationScanTest, ScannerRecognisesEnglishButNotIds)
{
  EXPECT_TRUE(looksLikeEnglish("Show AI"));
  EXPECT_TRUE(looksLikeEnglish("Goal!##goal"));
  EXPECT_TRUE(looksLikeEnglish("expected completion %.0f%%"));
  EXPECT_FALSE(looksLikeEnglish("##settings"));
  EXPECT_FALSE(looksLikeEnglish("SETTINGS_TITLE"));
  EXPECT_FALSE(looksLikeEnglish("%s  /  %s"));
  EXPECT_FALSE(looksLikeEnglish("player"));
  EXPECT_FALSE(looksLikeEnglish("%d'"));
}
