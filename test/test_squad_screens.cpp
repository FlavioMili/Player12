// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Medical centre, calendar, squad planner, player comparison and the set-piece
// dialog through the real GUIView at several window sizes and interface
// scales. Screenshots land in the test runtime's captures folder
// (FM_KEEP_TEST_ARTIFACTS=1 keeps them).

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/calendar_scene.h"
#include "gui/scenes/lineup_scene.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/medical_scene.h"
#include "gui/scenes/player_compare_scene.h"
#include "gui/scenes/squad_planner_scene.h"
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
  static bool sidebarOverflows(const GUIView& view)
  {
    const auto* shell = dynamic_cast<ManagementScene*>(view.getActiveScene());
    return shell == nullptr || shell->sidebar_nav_overflow;
  }
  static std::size_t injuredLines(const MedicalScene& scene)
  {
    return scene.injured.size();
  }
  static std::size_t riskLines(const MedicalScene& scene)
  {
    return scene.risks.size();
  }
  static std::size_t agendaEntries(const CalendarScene& scene)
  {
    return scene.entries.size();
  }
  static void selectDay(CalendarScene& scene, int day)
  {
    scene.selected_day = day;
  }
  static void selectPlanner(SquadPlannerScene& scene, PlayerID id, int season)
  {
    scene.selected = id;
    scene.season = season;
  }
  static std::size_t plannerRows(const SquadPlannerScene& scene)
  {
    std::size_t rows = 0;
    for (const auto& group : scene.views[0].groups) rows += group.rows.size();
    return rows;
  }
  static const std::vector<ScoutedAttribute>& compared(
      const PlayerCompareScene& scene, std::size_t slot)
  {
    return scene.slots[slot].attributes;
  }
  static void openSetPieces(LineupScene& scene) { scene.set_pieces.open(); }
  static void closeSetPieces(LineupScene& scene) { scene.set_pieces.close(); }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 800'000 + (getpid() % 100'000) * 10 + offset;
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

template <typename Scene>
Scene* active(GUIView& view)
{
  return dynamic_cast<Scene*>(Bridge::activeScene(view));
}
}  // namespace

TEST(SquadScreensTest, ScreensRenderAtEverySizeAndScale)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(0)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  const TeamID club = controller.getTeams().front().get().getId();
  controller.selectManagedTeam(club);
  // A few weeks in: friendlies played, loads built up; two injuries.
  for (int day = 0; day < 40; ++day) controller.advanceDay();
  auto& players = controller.getGameData()->getPlayers();
  const auto& squad = controller.getPlayersForTeam(club);
  ASSERT_GE(squad.size(), 16u);
  const PlayerID hamstring = squad[3].get().getId();
  const PlayerID knee = squad[7].get().getId();
  players.at(hamstring).mutableDynamics().injury = InjuryType::HamstringStrain;
  players.at(hamstring).mutableDynamics().injury_days = 18;
  players.at(knee).mutableDynamics().injury = InjuryType::KneeMcl;
  players.at(knee).mutableDynamics().injury_days = 45;
  const PlayerID youngest =
      std::ranges::min_element(
          squad, {}, [](const auto& player) { return player.get().getAge(); })
          ->get()
          .getId();
  ASSERT_TRUE(
      controller.setSquadStatus(squad[0].get().getId(), SquadStatus::Star));

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  EXPECT_FALSE(Bridge::sidebarOverflows(view))
      << "every sidebar destination fits at 720p";

  Navigation::open(&view, NavSection::MEDICAL);
  frames(view, 3);
  auto* medical = active<MedicalScene>(view);
  ASSERT_NE(medical, nullptr);
  const std::size_t injured = controller.getInjuredPlayers(club).size();
  EXPECT_GE(injured, 2u);
  EXPECT_EQ(Bridge::injuredLines(*medical), injured);
  EXPECT_EQ(Bridge::riskLines(*medical) + injured, squad.size());
  capture(view, "squad_medical.bmp");

  Navigation::open(&view, NavSection::CALENDAR);
  frames(view, 3);
  auto* calendar = active<CalendarScene>(view);
  ASSERT_NE(calendar, nullptr);
  EXPECT_GT(Bridge::agendaEntries(*calendar), 20u);
  capture(view, "squad_calendar.bmp");

  Navigation::open(&view, NavSection::SQUAD_PLANNER);
  frames(view, 3);
  auto* planner = active<SquadPlannerScene>(view);
  ASSERT_NE(planner, nullptr);
  EXPECT_EQ(Bridge::plannerRows(*planner), squad.size());
  Bridge::selectPlanner(*planner, youngest, 0);
  frames(view, 2);
  capture(view, "squad_planner.bmp");
  Bridge::selectPlanner(*planner, 0, 1);
  frames(view, 2);
  capture(view, "squad_planner_next.bmp");

  Navigation::open(&view, NavSection::LINEUP);
  frames(view, 2);
  auto* lineup = active<LineupScene>(view);
  ASSERT_NE(lineup, nullptr);
  Bridge::openSetPieces(*lineup);
  frames(view, 3);
  capture(view, "squad_set_pieces.bmp");
  Bridge::closeSetPieces(*lineup);
  frames(view, 2);
  Navigation::open(&view, NavSection::HOME);
  frames(view, 2);

  // Every screen at small, 4K and HiDPI (scale 2) sizes.
  Settings& settings = SettingsManager::instance()->get();
  const Settings original = settings;
  const TeamID rival = controller.getTeams().back().get().getId();
  const PlayerID foreign =
      controller.getPlayersForTeam(rival).front().get().getId();
  for (const auto& [width, height, scale, tag] :
       {std::tuple{1280, 720, 1.0f, "1280"}, std::tuple{900, 700, 1.0f, "900"},
        std::tuple{3840, 2160, 1.0f, "3840"},
        std::tuple{2560, 1440, 2.0f, "2560_200"}})
  {
    resize(view, width, height);
    settings.ui_scale = scale;
    view.refreshTheme();
    for (const auto& [section, name] :
         {std::pair{NavSection::MEDICAL, "medical"},
          std::pair{NavSection::CALENDAR, "calendar"},
          std::pair{NavSection::SQUAD_PLANNER, "planner"}})
    {
      Navigation::open(&view, section);
      frames(view, 3);
      capture(view, std::string("squad_") + name + "_" + tag + ".bmp");
    }
    Navigation::openCompare(&view, squad[0].get().getId(), foreign);
    frames(view, 3);
    capture(view, std::string("squad_compare_") + tag + ".bmp");
    Navigation::back(&view);
    frames(view, 1);
  }
  settings = original;
  view.refreshTheme();
}

TEST(SquadScreensTest, ComparisonShowsScoutedEstimatesForOtherClubs)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(1)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  const TeamID club = controller.getTeams().front().get().getId();
  controller.selectManagedTeam(club);
  const PlayerID own = controller.getPlayersForTeam(club).front().get().getId();
  const TeamID rival = controller.getTeams().back().get().getId();
  const Player& stranger = controller.getPlayersForTeam(rival).front().get();

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  Navigation::openCompare(&view, own, stranger.getId());
  frames(view, 3);
  auto* compare = active<PlayerCompareScene>(view);
  ASSERT_NE(compare, nullptr);

  const auto estimate = controller.getScoutedView(stranger.getId());
  ASSERT_TRUE(estimate.has_value());
  ASSERT_FALSE(estimate->own);
  const auto& shown = Bridge::compared(*compare, 1);
  ASSERT_EQ(shown.size(), estimate->attributes.size());
  bool differs = false;
  for (std::size_t index = 0; index < shown.size(); ++index)
  {
    EXPECT_EQ(shown[index].name, estimate->attributes[index].name);
    EXPECT_FLOAT_EQ(shown[index].estimate,
                    estimate->attributes[index].estimate);
    const auto truth = stranger.getStats().find(shown[index].name);
    ASSERT_NE(truth, stranger.getStats().end());
    differs |= std::abs(truth->second - shown[index].estimate) > 0.5f;
  }
  EXPECT_TRUE(differs) << "an unscouted player's true attributes leaked";
  // Own players are known exactly.
  const auto& mine = Bridge::compared(*compare, 0);
  const Player& ownPlayer = controller.getGameData()->getPlayer(own)->get();
  for (const ScoutedAttribute& attribute : mine)
    EXPECT_NEAR(attribute.estimate, ownPlayer.getStats().at(attribute.name),
                0.01f);
}

namespace
{
/** Presses and releases a key through ImGui's input queue. */
void pressKey(GUIView& view, ImGuiKey key)
{
  ImGui::GetIO().AddKeyEvent(key, true);
  Bridge::frame(view);
  ImGui::GetIO().AddKeyEvent(key, false);
  frames(view, 2);
}

SceneID activeId(GUIView& view)
{
  const GUIScene* scene = Bridge::activeScene(view);
  return scene != nullptr ? scene->getID() : SceneID::MAIN_MENU;
}

/** Centres of the hub tab buttons above the page (hover probe). */
std::vector<ImVec2> hubTabPoints(GUIView& view)
{
  const ImGuiWindow* content = nullptr;
  // The shell's page child itself, not the tables inside it.
  constexpr std::string_view PREFIX = "##management_shell/##content";
  for (const ImGuiWindow* window : GImGui->Windows)
  {
    const std::string_view name(window->Name);
    if (window->Active && name.starts_with(PREFIX) &&
        name.find('/', PREFIX.size()) == std::string_view::npos)
      content = window;
  }
  if (content == nullptr) return {};
  const float y = content->Pos.y + content->WindowPadding.y + 18.0f;
  std::vector<ImVec2> points;
  std::vector<ImGuiID> seen;
  // Inner rect only: a page that scrolls has its scrollbar on this line.
  for (int x_index = 0;
       content->InnerRect.Min.x + 6.0f * static_cast<float>(x_index) <
       content->InnerRect.Max.x;
       ++x_index)
  {
    const float x =
        content->InnerRect.Min.x + 6.0f * static_cast<float>(x_index);
    ImGui::GetIO().AddMousePosEvent(x, y);
    Bridge::frame(view);
    const ImGuiID hovered = GImGui->HoveredId;
    if (hovered == 0 || std::ranges::contains(seen, hovered)) continue;
    seen.push_back(hovered);
    points.emplace_back(x + 12.0f, y);
  }
  ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  return points;
}

/** Centres of the sidebar navigation entries, top to bottom (hover probe). */
std::vector<ImVec2> sidebarEntryPoints(GUIView& view)
{
  const ImGuiWindow* nav = nullptr;
  for (const ImGuiWindow* window : GImGui->Windows)
    if (window->Active &&
        std::string_view(window->Name).find("/##sidebar_nav") !=
            std::string_view::npos)
      nav = window;
  if (nav == nullptr) return {};
  const float x = nav->Pos.x + nav->Size.x * 0.5f;
  std::vector<ImVec2> points;
  ImGuiID last = 0;
  float first = 0.0f;
  for (int y_index = 0; nav->Pos.y + 1.0f + 2.0f * static_cast<float>(y_index) <
                        nav->Pos.y + nav->Size.y;
       ++y_index)
  {
    const float y = nav->Pos.y + 1.0f + 2.0f * static_cast<float>(y_index);
    ImGui::GetIO().AddMousePosEvent(x, y);
    Bridge::frame(view);
    const ImGuiID hovered = GImGui->HoveredId;
    if (hovered != last && last != 0)
      points.emplace_back(x, (first + y - 2.0f) * 0.5f);
    if (hovered != last) first = y;
    last = hovered;
  }
  if (last != 0)
    points.emplace_back(x, (first + nav->Pos.y + nav->Size.y) * 0.5f);
  ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  return points;
}

void click(GUIView& view, ImVec2 point)
{
  ImGui::GetIO().AddMousePosEvent(point.x, point.y);
  Bridge::frame(view);
  ImGui::GetIO().AddMouseButtonEvent(0, true);
  Bridge::frame(view);
  ImGui::GetIO().AddMouseButtonEvent(0, false);
  frames(view, 3);
}

void setUiScale(GUIView& view, float scale)
{
  SettingsManager::instance()->get().ui_scale = scale;
  view.refreshTheme();
}
}  // namespace

TEST(SquadScreensTest, SidebarHubsListTheirScreens)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(2)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  controller.setSimulationThreads(1);
  controller.selectManagedTeam(controller.getTeams().front().get().getId());

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  resize(view, 1280, 720);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 3);
  EXPECT_FALSE(Bridge::sidebarOverflows(view))
      << "every hub fits the 720p sidebar";

  // F1-F7 open the hubs' first screens.
  const std::array<std::pair<ImGuiKey, SceneID>, 7> hubs = {{
      {ImGuiKey_F3, SceneID::ROSTER},
      {ImGuiKey_F4, SceneID::TRAINING},
      {ImGuiKey_F5, SceneID::FIXTURES},
      {ImGuiKey_F6, SceneID::TRANSFER_MARKET},
      {ImGuiKey_F7, SceneID::CLUB},
      {ImGuiKey_F2, SceneID::INBOX},
      {ImGuiKey_F1, SceneID::GAME_MENU},
  }};
  for (const auto& [key, expected] : hubs)
  {
    pressKey(view, key);
    EXPECT_EQ(activeId(view), expected) << ImGui::GetKeyName(key);
    EXPECT_LE(view.getOverlayDepth(), 1u);
  }

  // Every hub expanded (Club is the largest, eight screens) fits the
  // sidebar without scrolling at 1280x720 and at 2560x1440 with scale 2.
  const auto everyHubFits = [&](const char* size)
  {
    for (const NavSection section :
         {NavSection::SQUAD, NavSection::TRAINING, NavSection::FIXTURES,
          NavSection::TRANSFERS, NavSection::CLUB})
    {
      Navigation::open(&view, section);
      frames(view, 3);
      EXPECT_FALSE(Bridge::sidebarOverflows(view))
          << size << " hub of section "
          << static_cast<int>(std::to_underlying(section));
    }
  };
  everyHubFits("1280x720");
  capture(view, "shell_submenu_club.bmp");
  resize(view, 2560, 1440);
  setUiScale(view, 2.0f);
  frames(view, 3);
  everyHubFits("2560x1440 scale 2");
  capture(view, "shell_submenu_club_scale2.bmp");
  setUiScale(view, 0.0f);
  resize(view, 1280, 720);
  frames(view, 3);

  // The Squad hub lists its seven screens under its label, between the
  // Inbox and Training entries; each one opens, the tabs are gone.
  Navigation::open(&view, NavSection::SQUAD);
  frames(view, 3);
  const std::vector<ImVec2> entries = sidebarEntryPoints(view);
  ASSERT_EQ(entries.size(), 14u) << "seven hubs and the seven Squad screens";
  EXPECT_TRUE(hubTabPoints(view).empty()) << "no tabs beside the full sidebar";
  const std::array<std::pair<size_t, SceneID>, 8> screens = {{
      {4, SceneID::LINEUP},
      {5, SceneID::STRATEGY},
      {6, SceneID::SQUAD_PLANNER},
      {7, SceneID::MEDICAL},
      {8, SceneID::PLAYER_COMPARE},
      {9, SceneID::RESERVES},
      {3, SceneID::ROSTER},
      {10, SceneID::TRAINING},
  }};
  for (const auto& [index, expected] : screens)
  {
    Navigation::open(&view, NavSection::LINEUP);
    frames(view, 2);
    click(view, entries[index]);
    EXPECT_EQ(activeId(view), expected) << "sidebar entry " << index;
    EXPECT_LE(view.getOverlayDepth(), 1u);
  }
  ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  Navigation::open(&view, NavSection::MEDICAL);
  frames(view, 3);
  capture(view, "shell_submenu_squad.bmp");

  // With the sidebar holding the keyboard (after its F-key), the arrows
  // walk the hub's screens and stop at its ends.
  pressKey(view, ImGuiKey_F3);
  ASSERT_EQ(activeId(view), SceneID::ROSTER);
  pressKey(view, ImGuiKey_UpArrow);
  EXPECT_EQ(activeId(view), SceneID::ROSTER) << "first screen of the hub";
  pressKey(view, ImGuiKey_DownArrow);
  EXPECT_EQ(activeId(view), SceneID::LINEUP);
  pressKey(view, ImGuiKey_DownArrow);
  EXPECT_EQ(activeId(view), SceneID::STRATEGY);
  pressKey(view, ImGuiKey_UpArrow);
  EXPECT_EQ(activeId(view), SceneID::LINEUP);
  // A click on the page hands the keyboard back to it.
  {
    const ImGuiWindow* topbar = nullptr;
    for (const ImGuiWindow* window : GImGui->Windows)
      if (window->Active && std::string_view(window->Name).find("/##topbar") !=
                                std::string_view::npos)
        topbar = window;
    ASSERT_NE(topbar, nullptr);
    click(view, ImVec2(topbar->Pos.x + topbar->Size.x * 0.45f,
                       topbar->Pos.y + topbar->Size.y + 3.0f));
    ASSERT_EQ(activeId(view), SceneID::LINEUP);
    pressKey(view, ImGuiKey_DownArrow);
    EXPECT_EQ(activeId(view), SceneID::LINEUP);
  }

  // Narrow window: icon-only sidebar. Hovering a hub opens a flyout with
  // its screens; the tabs above the page come back.
  resize(view, 1000, 700);
  Navigation::open(&view, NavSection::SQUAD);
  frames(view, 3);
  EXPECT_EQ(hubTabPoints(view).size(), 6u) << "Squad's other six screens";
  const std::vector<ImVec2> icons = sidebarEntryPoints(view);
  ASSERT_EQ(icons.size(), 7u) << "seven hub icons";
  ImGui::GetIO().AddMousePosEvent(icons[6].x, icons[6].y);
  frames(view, 3);
  const ImGuiWindow* flyout = ImGui::FindWindowByName("##nav_flyout");
  ASSERT_NE(flyout, nullptr);
  EXPECT_TRUE(flyout->Active);
  capture(view, "shell_submenu_flyout.bmp");
  // Travel into the flyout: eight entries; pick the third (Staff).
  std::vector<ImVec2> choices;
  {
    ImGuiID last = 0;
    const ImVec2 origin = flyout->Pos;
    const ImVec2 extent = flyout->Size;
    for (int y_index = 0; origin.y + 1.0f + 2.0f * static_cast<float>(y_index) <
                          origin.y + extent.y;
         ++y_index)
    {
      const float y = origin.y + 1.0f + 2.0f * static_cast<float>(y_index);
      ImGui::GetIO().AddMousePosEvent(origin.x + extent.x * 0.5f, y);
      Bridge::frame(view);
      const ImGuiID hovered = GImGui->HoveredId;
      if (hovered != 0 && hovered != last)
        choices.emplace_back(origin.x + extent.x * 0.5f, y + 4.0f);
      last = hovered;
    }
  }
  ASSERT_EQ(choices.size(), 8u) << "the Club hub's eight screens";
  click(view, choices[2]);
  EXPECT_EQ(activeId(view), SceneID::STAFF);
  frames(view, 30);
  EXPECT_FALSE(ImGui::FindWindowByName("##nav_flyout")->Active)
      << "the flyout closes once used";
  resize(view, 1280, 720);
  frames(view, 3);

  // Out of work only the manager's own screens and the world stay: the
  // Squad hub is gone, Matches opens the table, Club the manager page.
  Navigation::open(&view, NavSection::HOME);
  frames(view, 2);
  ASSERT_TRUE(controller.resignFromClub());
  frames(view, 3);
  pressKey(view, ImGuiKey_F5);
  EXPECT_EQ(activeId(view), SceneID::STANDINGS);
  pressKey(view, ImGuiKey_F7);
  EXPECT_EQ(activeId(view), SceneID::MANAGER);
  pressKey(view, ImGuiKey_F3);
  EXPECT_EQ(activeId(view), SceneID::MANAGER) << "no Squad hub out of work";
  capture(view, "shell_hubs_unemployed.bmp");
}
