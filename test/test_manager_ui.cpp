// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The manager career through the real GUIView: the manager card of the club
// choice, the Manager screen, the Job Centre (vacancies, interview, offers
// and negotiation) and the home page out of work, at several window sizes
// and UI scales. Screenshots land in the test runtime's captures folder
// (FM_KEEP_TEST_ARTIFACTS=1 keeps them).

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>

#include "controller/game_controller.h"
#include "global/language_manager.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/manager_scene.h"
#include "gui/scenes/team_selection_scene.h"
#include "gui/widgets/theme.h"
#include "model/game.h"
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
  static void showTab(ManagerScene& scene, ManagerScene::Tab tab)
  {
    scene.tab = tab;
  }
  static void selectVacancy(ManagerScene& scene, TeamID team_id)
  {
    scene.selected_vacancy = team_id;
  }
  static size_t vacancyRows(const ManagerScene& scene)
  {
    return scene.vacancies.size();
  }
  static size_t offerRows(const ManagerScene& scene)
  {
    return scene.offers.size();
  }
  static size_t stintRows(const ManagerScene& scene)
  {
    return scene.stints.size();
  }
  static void openInterview(ManagerScene& scene, TeamID team_id, size_t step)
  {
    scene.interview_club = team_id;
    scene.interview_club_name = "";
    scene.interview_step = step;
    scene.interview_answers.fill(1);
    scene.interview_result.reset();
    scene.interview_requested = true;
  }
  static void negotiate(ManagerScene& scene, std::uint32_t offer_id,
                        std::int64_t wage)
  {
    scene.negotiating = offer_id;
    scene.counter_wage = wage;
    scene.counter_years = 1;
  }
  static void refresh(ManagerScene& scene) { scene.refresh(); }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 600'000 + (getpid() % 100'000) * 10 + offset;
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

void setUiScale(GUIView& view, float scale)
{
  SettingsManager::instance()->get().ui_scale = scale;
  view.refreshTheme();
}

std::vector<TeamID> clubsByReputation(const GameController& controller)
{
  std::vector<TeamID> clubs;
  for (const auto& team : controller.getTeams())
    if (team.get().getId() != FREE_AGENTS_TEAM_ID)
      clubs.push_back(team.get().getId());
  std::ranges::sort(
      clubs,
      [&](TeamID a, TeamID b)
      {
        const auto ra = controller.getTeamById(a)->get().getReputation();
        const auto rb = controller.getTeamById(b)->get().getReputation();
        return ra != rb ? ra < rb : a < b;
      });
  return clubs;
}
}  // namespace

TEST(ManagerUiTest, ClubChoiceShowsTheManagerStep)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(0)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 3);
  ASSERT_EQ(Bridge::activeScene(view)->getID(), SceneID::TEAM_SELECTION);
  capture(view, "manager_setup_club_choice.bmp");
  for (const auto& [width, height, name] :
       {std::tuple{1280, 720, "manager_setup_1280.bmp"},
        std::tuple{900, 700, "manager_setup_900.bmp"}})
  {
    resize(view, width, height);
    frames(view, 3);
    capture(view, name);
  }
  setUiScale(view, 2.0f);
  resize(view, 1280, 720);
  frames(view, 3);
  capture(view, "manager_setup_1280_scale2.bmp");
  setUiScale(view, 0.0f);
}

namespace
{
/**
 * Scroll surfaces of the team selection page: nothing scrolls sideways,
 * and when the page itself scrolls no card inside it scrolls as well.
 * Returns a description of the offending windows (empty when fine).
 */
std::string scrollViolations()
{
  std::string found;
  bool pageScrolls = false;
  std::string inner;
  for (const ImGuiWindow* window : GImGui->Windows)
  {
    if (!window->Active || window->Hidden) continue;
    const std::string_view name(window->Name);
    if (name.find("##team_selection") == std::string_view::npos) continue;
    if (window->ScrollbarX) found += std::format(" [sideways: {}]", name);
    if (!window->ScrollbarY) continue;
    if (name == "##team_selection")
      pageScrolls = true;
    else
      inner += std::format(" [{}]", name);
  }
  if (pageScrolls && !inner.empty())
    found += " page scrolls and so do:" + inner;
  return found;
}

/**
 * True when the start button of the club card can be hovered on screen: a
 * grid of mouse probes over the card must land on it inside the window.
 */
bool startButtonReachable(GUIView& view)
{
  const ImGuiWindow* card = nullptr;
  for (const ImGuiWindow* window : GImGui->Windows)
  {
    const std::string_view name(window->Name);
    if (window->Active && (name.ends_with("##selected_club") ||
                           name.find("/##selected_club_") != std::string::npos))
      card = window;
  }
  if (card == nullptr) return false;
  const ImGuiID button = ImHashStr(LOC("TEAM_SELECTION_CONFIRM"), 0, card->ID);
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const ImRect area(ImMax(card->Rect().Min, ImVec2(0.0f, 0.0f)),
                    ImMin(card->Rect().Max, display));
  bool found = false;
  for (int y_index = 0;
       area.Min.y + 4.0f + 8.0f * static_cast<float>(y_index) < area.Max.y &&
       !found;
       ++y_index)
    for (int x_index = 0;
         area.Min.x + 4.0f + 24.0f * static_cast<float>(x_index) < area.Max.x &&
         !found;
         ++x_index)
    {
      const float y = area.Min.y + 4.0f + 8.0f * static_cast<float>(y_index);
      const float x = area.Min.x + 4.0f + 24.0f * static_cast<float>(x_index);
      ImGui::GetIO().AddMousePosEvent(x, y);
      Bridge::frame(view);
      found = GImGui->HoveredId == button;
    }
  ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  Bridge::frame(view);
  return found;
}
}  // namespace

TEST(ManagerUiTest, TeamSelectionKeepsStartVisibleAtEverySize)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(2)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  resize(view, 1280, 720);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  // The very first frame, before auto-sized cards have measured themselves.
  frames(view, 1);
  ASSERT_EQ(Bridge::activeScene(view)->getID(), SceneID::TEAM_SELECTION);
  capture(view, "team_selection_first_frame.bmp");
  // Every size the start button must be on screen without scrolling; the
  // narrow one shows the club card as a strip above the table.
  for (const auto& [width, height, scale, name] :
       {std::tuple{1280, 720, 0.0f, "team_selection_1280.bmp"},
        std::tuple{1366, 768, 0.0f, "team_selection_1366.bmp"},
        std::tuple{1920, 1080, 0.0f, "team_selection_1920.bmp"},
        std::tuple{2560, 1440, 2.0f, "team_selection_2560_scale2.bmp"},
        std::tuple{900, 700, 0.0f, "team_selection_900.bmp"}})
  {
    setUiScale(view, scale);
    resize(view, width, height);
    frames(view, 3);
    capture(view, name);
    EXPECT_TRUE(startButtonReachable(view)) << name;
    EXPECT_EQ(scrollViolations(), "") << name;
  }
  // Too short for the club browser at scale 2 (the manager card alone fills
  // the window): the cards stack at their natural height and only the page
  // scrolls; the club strip with the start button comes first.
  setUiScale(view, 2.0f);
  resize(view, 1280, 720);
  frames(view, 3);
  capture(view, "team_selection_1280_scale2.bmp");
  EXPECT_EQ(scrollViolations(), "") << "1280x720 at scale 2";
  // Scrolling to the club strip brings the start button into view.
  {
    ImGuiWindow* page = ImGui::FindWindowByName("##team_selection");
    const ImGuiWindow* strip = nullptr;
    for (const ImGuiWindow* window : GImGui->Windows)
      if (window->Active &&
          std::string_view(window->Name).find("/##selected_club_strip") !=
              std::string_view::npos)
        strip = window;
    ASSERT_NE(page, nullptr);
    ASSERT_NE(strip, nullptr) << "the stacked layout shows the club strip";
    ImGui::SetScrollY(page, page->Scroll.y + strip->Pos.y - page->Pos.y);
    frames(view, 3);
    EXPECT_TRUE(startButtonReachable(view)) << "scrolled to the club strip";
    capture(view, "team_selection_1280_scale2_strip.bmp");
  }
  // Scrolled to the bottom: still one scroll surface, the club list whole.
  ImGui::SetScrollY(ImGui::FindWindowByName("##team_selection"), 1.0e6f);
  frames(view, 3);
  capture(view, "team_selection_1280_scale2_scrolled.bmp");
  EXPECT_EQ(scrollViolations(), "") << "scrolled";
  setUiScale(view, 0.0f);
}

TEST(ManagerUiTest, ManagerScreenAndJobCentre)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(1)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  ManagerSetup setup;
  setup.first_name = "Marta";
  setup.last_name = "Ferraro";
  setup.nationality = Language::IT;
  setup.background = ManagerBackground::FormerInternational;
  controller.createManager(setup);
  const std::vector<TeamID> clubs = clubsByReputation(controller);
  const TeamID first = clubs[clubs.size() / 2];
  controller.selectManagedTeam(first);
  Game& game = *controller.getGame();
  // A few clubs part with their managers: vacancies to look at.
  for (std::size_t i = 0; i < 6; ++i)
    game.getCareer().dismissClubManager(clubs[i * 7], game.getCurrentDate(),
                                        game.getWorld().getInbox());

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  Navigation::open(&view, NavSection::MANAGER);
  frames(view, 3);
  auto* scene = dynamic_cast<ManagerScene*>(Bridge::activeScene(view));
  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(Bridge::stintRows(*scene), 1u);
  capture(view, "manager_profile.bmp");
  Bridge::showTab(*scene, ManagerScene::Tab::JOB_CENTRE);
  frames(view, 3);
  EXPECT_EQ(Bridge::vacancyRows(*scene), 6u);
  capture(view, "manager_jobs_employed.bmp");

  // Out of work: the home page and the Job Centre take over.
  ASSERT_TRUE(controller.resignFromClub());
  Navigation::open(&view, NavSection::HOME);
  frames(view, 4);
  ASSERT_EQ(Bridge::activeScene(view)->getID(), SceneID::GAME_MENU);
  capture(view, "manager_unemployed_home.bmp");

  // An application, an interview and an offer.
  ManagerCareer& career = game.getCareer();
  const TeamID target = clubs[7];
  ASSERT_EQ(controller.applyForJob(target), ApplyResult::Ok);
  const TeamID invited = clubs[14];
  ASSERT_EQ(controller.applyForJob(invited), ApplyResult::Ok);
  for (int day = 0; day < 6; ++day) controller.advanceDay();
  Navigation::open(&view, NavSection::MANAGER);
  frames(view, 3);
  scene = dynamic_cast<ManagerScene*>(Bridge::activeScene(view));
  ASSERT_NE(scene, nullptr);
  for (const JobApplication& application : career.getApplications())
    if (application.stage == ApplicationStage::Interview)
    {
      std::array<std::uint8_t, INTERVIEW_TOPICS> answers{};
      answers.fill(1);
      if (application.team_id == target)
        controller.attendInterview(application.team_id, answers);
    }
  Bridge::refresh(*scene);
  Bridge::selectVacancy(*scene, clubs[21]);
  frames(view, 3);
  capture(view, "manager_jobs_unemployed.bmp");

  if (Bridge::offerRows(*scene) > 0)
  {
    const JobOffer& offer = career.getOffers().front();
    Bridge::negotiate(*scene, offer.id, offer.weekly_wage * 6 / 5);
    frames(view, 3);
    capture(view, "manager_negotiation.bmp");
  }
  for (const auto& [width, height, name] :
       {std::tuple{1280, 720, "manager_jobs_1280.bmp"},
        std::tuple{900, 700, "manager_jobs_900.bmp"},
        std::tuple{3840, 2160, "manager_jobs_3840.bmp"}})
  {
    resize(view, width, height);
    frames(view, 4);
    capture(view, name);
  }
  setUiScale(view, 2.0f);
  resize(view, 1280, 720);
  frames(view, 4);
  capture(view, "manager_jobs_1280_scale2.bmp");
  Bridge::showTab(*scene, ManagerScene::Tab::PROFILE);
  frames(view, 3);
  capture(view, "manager_profile_1280_scale2.bmp");
  // The interview dialog, last: it is modal.
  if (const JobApplication* application = career.findApplication(invited);
      application && application->stage == ApplicationStage::Interview)
  {
    Bridge::showTab(*scene, ManagerScene::Tab::JOB_CENTRE);
    Bridge::openInterview(*scene, invited, 1);
    frames(view, 3);
    capture(view, "manager_interview_1280_scale2.bmp");
  }
  setUiScale(view, 0.0f);
  resize(view, 1280, 720);
  frames(view, 3);
  capture(view, "manager_interview_1280.bmp");

  // Accepting an offer moves the career to the new club.
  if (!career.getOffers().empty())
  {
    ASSERT_TRUE(controller.acceptJobOffer(career.getOffers().front().id));
    Navigation::open(&view, NavSection::HOME);
    frames(view, 4);
    EXPECT_FALSE(controller.isUnemployed());
    capture(view, "manager_new_club_home.bmp");
  }
}
