// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------
//
// Adversarial scenarios: the awkward things a real user does at the worst
// possible moment (double-clicking Continue, saving while days are being
// simulated, releasing the lineup, running out of players or money, closing
// the game mid-match, loading old saves, corrupt settings, ...). Each test
// is deterministic (fixed world seed, fixed delta time) and checks the
// world's invariants afterwards.
//
// Product bugs that are known and routed are marked with GTEST_SKIP()
// messages starting with "KNOWN BUG:" and the finding they reference.
//
// FM_ADVERSARIAL_LONG=1 also plays a whole season (rollover with open deals).
// Label "adversarial;slow" (own executable): exclude with `-LE adversarial`.

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <print>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include "controller/game_controller.h"
#include "database/database_connection.h"
#include "database/gamedata.h"
#include "database/save_manager.h"
#include "global/global.h"
#include "global/language_manager.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/render_scale.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/main_menu_scene.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/match_scene.h"
#include "gui/scenes/player_profile_scene.h"
#include "gui/scenes/settings_scene.h"
#include "gui/scenes/transfer_market_scene.h"
#include "gui/widgets/theme.h"
#include "model/finances.h"
#include "model/injury.h"
#include "model/match_engine.h"
#include "settings_manager.h"

#if defined(__clang__) || defined(__GNUC__)
extern "C" const char* __lsan_default_suppressions()
{
  return "leak:libSDL3.so\n";
}
#endif

/** Bridge to GUI internals (see test_gui_monkey.cpp). */
class GameFlowTest_GUIFlowLifecycle_Test
{
 public:
  static bool initialize(GUIView& view) { return view.initialize(); }
  static void applyPending(GUIView& view) { view.applyPendingSceneChanges(); }
  static void handleEvents(GUIView& view) { view.handleEvents(); }
  static void update(GUIView& view, float seconds) { view.update(seconds); }
  static GUIScene* activeScene(const GUIView& view)
  {
    return view.getActiveScene();
  }
  static bool running(const GUIView& view) { return view.running; }

  /** GUIView::render() with a fixed delta time (see test_gui_monkey.cpp). */
  static void render(GUIView& view, float seconds)
  {
    SDL_Renderer* renderer = view.renderer;
    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    SDL_RenderClear(renderer);
    if (view.backdropTexture != nullptr && !view.backdropPending)
      SDL_RenderTexture(renderer, view.backdropTexture, nullptr, nullptr);
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::GetIO().DeltaTime = seconds;
    ImGui::NewFrame();
    if (GUIScene* scene = view.getActiveScene()) scene->render();
    ImGui::Render();
    {
      const ScopedRenderScale scale(renderer,
                                    ImGui::GetIO().DisplayFramebufferScale);
      ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    }
    if (view.backdropPending)
    {
      view.backdropPending = false;
      view.releaseBackdrop();
      if (SDL_Surface* frame = SDL_RenderReadPixels(renderer, nullptr))
      {
        view.backdropTexture = SDL_CreateTextureFromSurface(renderer, frame);
        SDL_DestroySurface(frame);
      }
    }
    SDL_RenderPresent(renderer);
  }
  static SDL_Renderer* renderer(GUIView& view) { return view.renderer; }

  static bool continueRequested(const MainGameScene& hub)
  {
    return hub.continuation_requested;
  }
  static void refresh(MainGameScene& hub) { hub.refreshData(); }
  static bool lineupBlocked(const MainGameScene& hub)
  {
    return hub.lineupBlocked();
  }
  static std::optional<GameDateValue> nextFixtureDate(const MainGameScene& hub)
  {
    if (!hub.cached_next) return std::nullopt;
    return hub.cached_next->date;
  }
  static void requestMainMenu(ManagementScene& scene)
  {
    scene.main_menu_confirm_requested = true;
  }
  static void requestRenew(PlayerProfileScene& scene)
  {
    scene.renew_requested = true;
  }

  static void showSubstitutions(MatchScene& scene)
  {
    scene.show_substitutions = true;
  }
  static bool openFirstOffer(TransferMarketScene& scene, bool loan)
  {
    if (scene.targets.empty()) return false;
    if (loan)
      scene.openLoanDialog(scene.targets.front());
    else
      scene.openOfferDialog(scene.targets.front());
    return true;
  }
  static MatchEngine* engine(const MatchScene& scene)
  {
    return scene.engine.get();
  }
  static bool finished(const MatchScene& scene) { return scene.match_finished; }
  static void setSpeed(MatchScene& scene, float speed)
  {
    scene.setPlaybackSpeed(speed);
  }
  /** Quick Result simulates on a worker thread until update() sees it. */
  static bool quickResultPending(const MatchScene& scene)
  {
    return scene.quick_result.valid();
  }
  static bool quickResult(MatchScene& scene) { return scene.quickResult(); }
  static bool finishMatch(MatchScene& scene) { return scene.finishMatch(); }
  static size_t lineupProblems(const MatchScene& scene)
  {
    return scene.lineup_problems.size();
  }

  static bool menuLoading(const MainMenuScene& menu)
  {
    return menu.loading_slot > 0;
  }
  static void menuLoad(MainMenuScene& menu, int slot)
  {
    menu.is_new_game = false;
    menu.loading_slot = slot;
    menu.is_loading_rendered = false;
  }
};

namespace
{
using Bridge = GameFlowTest_GUIFlowLifecycle_Test;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

constexpr float FRAME_SECONDS = 1.0f / 60.0f;
constexpr uint64_t WORLD_SEED = 20250702;
constexpr int BASE_SLOT = 1;
constexpr int WORK_SLOT = 2;

int dayNumber(const GameDateValue& date)
{
  const int year = date.month <= 2 ? date.year - 1 : date.year;
  const int era = year / 400;
  const int yoe = year - era * 400;
  const int month = date.month;
  const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + date.day - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 730425;
}

/** World invariants (subset of the monkey's): membership, ledger, lineups. */
std::vector<std::string> checkWorld(const GameController& controller)
{
  std::vector<std::string> problems;
  const auto data = controller.getGameData();
  std::unordered_set<const Player*> alive;
  for (const auto& player : data->getPlayersVector())
    alive.insert(&player.get());
  std::unordered_map<PlayerID, TeamID> owner;
  const auto managed = controller.getManagedTeam();
  const TeamID managedId = managed ? managed->get().getId() : 0;
  for (const auto& [teamId, team] : data->getTeams())
  {
    if (teamId == FREE_AGENTS_TEAM_ID) continue;
    // Club players: the senior squad plus the academy.
    std::vector<PlayerID> members = team.getPlayerIDs();
    members.insert(members.end(), team.getAcademyIDs().begin(),
                   team.getAcademyIDs().end());
    for (const PlayerID playerId : team.getAcademyIDs())
      if (const auto player = data->getPlayer(playerId);
          player && !player->get().isAcademyPlayer())
        problems.push_back(std::format("club {} lists senior {} in its academy",
                                       teamId, playerId));
    for (const PlayerID playerId : members)
    {
      const auto player = data->getPlayer(playerId);
      if (!owner.emplace(playerId, teamId).second)
        problems.push_back(std::format("player {} in two squads", playerId));
      else if (!player)
        problems.push_back(
            std::format("club {} lists unknown player {}", teamId, playerId));
      else if (player->get().getTeamId() != teamId)
        problems.push_back(std::format("club {} lists player {} of club {}",
                                       teamId, playerId,
                                       player->get().getTeamId()));
    }
    const Lineup& lineup = team.getLineup();
    std::vector selected{lineup.getGoalkeeper()};
    for (const auto& positioned : lineup.getOutfieldPlayers())
      selected.push_back(positioned.player);
    for (const Player* reserve : lineup.getReserves())
      selected.push_back(reserve);
    for (const Player* player : selected)
    {
      if (player == nullptr) continue;
      if (!alive.contains(player))
        problems.push_back(
            std::format("club {} lineup holds a dangling player", teamId));
      else if (teamId == managedId && player->getTeamId() != teamId)
        problems.push_back(std::format("managed lineup keeps player {} of {}",
                                       player->getId(), player->getTeamId()));
    }
    const auto& ledger = controller.getFinanceLedger(teamId);
    const int64_t sum =
        std::accumulate(ledger.begin(), ledger.end(), int64_t{0},
                        [](int64_t total, const FinanceTransaction& entry)
                        { return total + entry.amount; });
    if (sum != team.getFinances().getBalance())
      problems.push_back(std::format("club {} balance {} != ledger {}", teamId,
                                     team.getFinances().getBalance(), sum));
  }
  for (const auto& player : data->getPlayersVector())
    if (player.get().getTeamId() != FREE_AGENTS_TEAM_ID &&
        !owner.contains(player.get().getId()))
      problems.push_back(std::format("player {} missing from club {}",
                                     player.get().getId(),
                                     player.get().getTeamId()));
  if (problems.size() > 8) problems.resize(8);
  return problems;
}

/** Explicit saves only, so a test can tell what wrote the slot. */
void disableAutosave(GameController& controller)
{
  AutosavePolicy policy = controller.getAutosavePolicy();
  policy.frequency = AutosaveFrequency::Off;
  controller.setAutosavePolicy(policy);
}

std::string joined(const std::vector<std::string>& lines)
{
  std::string text;
  for (const std::string& line : lines) text += "\n  " + line;
  return text;
}

/** Game date stored in a slot's GameState row (what the last save wrote). */
std::string savedDate(int slot)
{
  sqlite3* db = nullptr;
  std::string date;
  if (sqlite3_open_v2(RuntimePaths::savePath(slot).c_str(), &db,
                      SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK)
  {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT game_date FROM GameState LIMIT 1;", -1,
                           &statement, nullptr) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_ROW)
      date = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
    sqlite3_finalize(statement);
  }
  sqlite3_close(db);
  return date;
}

/** A small driver: fixed-delta frames and real SDL/ImGui input. */
class Driver
{
 public:
  Driver(GUIView& view_ref, GameController& controller_ref)
      : view(view_ref), controller(controller_ref)
  {
  }
  GUIView& view;
  GameController& controller;
  int imgui_errors = 0;

  GUIScene* active() const { return Bridge::activeScene(view); }
  SceneID activeId() const
  {
    return active() ? active()->getID() : SceneID::MAIN_MENU;
  }
  MainGameScene* hub() const
  {
    return dynamic_cast<MainGameScene*>(view.getBaseScene());
  }

  void frame()
  {
    Bridge::applyPending(view);
    Bridge::handleEvents(view);
    Bridge::update(view, FRAME_SECONDS);
    Bridge::render(view, FRAME_SECONDS);
    imgui_errors += GImGui->ErrorCountCurrentFrame;
  }
  void frames(int count)
  {
    for (int index = 0; index < count; ++index) frame();
  }
  void key(SDL_Keycode code, SDL_Scancode scancode,
           SDL_Keymod mod = SDL_KMOD_NONE)
  {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = SDL_GetWindowID(view.getWindow());
    event.key.key = code;
    event.key.scancode = scancode;
    event.key.mod = mod;
    event.key.down = true;
    SDL_PushEvent(&event);
    frame();
    event.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    event.key.mod = SDL_KMOD_NONE;
    SDL_PushEvent(&event);
    frame();
  }
  void space() { key(SDLK_SPACE, SDL_SCANCODE_SPACE); }
  void escape() { key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE); }
  void save() { key(SDLK_S, SDL_SCANCODE_S, SDL_KMOD_LCTRL); }

  void click(ImVec2 point, int count = 1)
  {
    ImGui::GetIO().AddMousePosEvent(point.x, point.y);
    frame();
    for (int index = 0; index < count; ++index)
    {
      ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
      frame();
      ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
      frame();
    }
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    frame();
  }

  /**
   * Point on the top bar's Continue button, found by its ID ("###shell_
   * continue" in the top bar window) along the bar's centre line.
   */
  std::optional<ImVec2> continuePoint()
  {
    ImGuiID id = 0;
    for (ImGuiWindow* window : GImGui->Windows)
      if (window->Active && std::string_view(window->Name).contains("##topbar"))
        id = window->GetID("###shell_continue");
    if (id == 0) return std::nullopt;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float y = 30.0f * Theme::scale();
    // The button's horizontal extent along the probe line; its centre is
    // clicked, so a layout shift of a few pixels cannot move it away.
    std::optional<float> right;
    std::optional<float> left;
    for (int stepIndex = 0; !left.has_value(); ++stepIndex)
    {
      const float x = display.x - 4.0f - static_cast<float>(stepIndex) * 4.0f;
      if (!(x > display.x * 0.3f)) break;
      ImGui::GetIO().AddMousePosEvent(x, y);
      frame();
      const bool hovered = GImGui->HoveredId == id;
      if (hovered && !right.has_value()) right = x;
      if (!hovered && right.has_value()) left = x + 4.0f;
    }
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    frame();
    if (!right.has_value()) return std::nullopt;
    return ImVec2((*right + left.value_or(*right)) * 0.5f, y);
  }

  bool advancing() const { return hub() != nullptr && hub()->isAdvancing(); }

  /** Frames until no Continue or slot loading is pending. */
  bool settle(std::chrono::seconds deadline = std::chrono::seconds(120))
  {
    // A Continue requested from a screen above the hub (e.g. Finish match)
    // starts once that screen has closed and the backdrop is captured.
    frames(2);
    const auto start = Clock::now();
    while (Clock::now() - start < deadline)
    {
      const auto* menu = dynamic_cast<MainMenuScene*>(view.getBaseScene());
      const bool loading = menu != nullptr && Bridge::menuLoading(*menu);
      const bool requested = hub() != nullptr &&
                             Bridge::continueRequested(*hub()) &&
                             active() == hub();
      const auto* match = dynamic_cast<const MatchScene*>(active());
      if (const bool quick =
              match != nullptr && Bridge::quickResultPending(*match);
          !advancing() && !loading && !requested && !quick)
        break;
      frame();
    }
    frames(2);
    return !advancing();
  }
};

// ---- Fixture
// -------------------------------------------------------------------

/**
 * One generated world per process (slot 1); every test plays a copy in
 * slot 2 so saves never leak between tests.
 */
class Adversarial : public ::testing::Test
{
 protected:
  static inline TeamID managed_id = 0;

  static void SetUpTestSuite()
  {
    setenv("SDL_VIDEODRIVER", "dummy", 0);
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    Logger::init();
    GameController controller;
    controller.newGame(BASE_SLOT, WORLD_SEED);
    // A mid-table club of the first league.
    const LeagueID league = controller.getLeagues().front().get().getId();
    auto teams = controller.getTeamsInLeague(league);
    std::ranges::sort(teams, [](const auto& left, const auto& right)
                      { return left.get().getId() < right.get().getId(); });
    managed_id = teams[teams.size() / 2].get().getId();
    controller.selectManagedTeam(managed_id);
    controller.saveGame();
  }

  void SetUp() override
  {
    std::error_code ignored;
    fs::remove(RuntimePaths::settingsPath(), ignored);
    SettingsManager::instance()->get() = Settings{};
    LanguageManager::instance().loadLanguage(Language::EN);
    RuntimePaths::removeSave(WORK_SLOT);
    const fs::path base = RuntimePaths::savePath(BASE_SLOT);
    const fs::path work = RuntimePaths::savePath(WORK_SLOT);
    for (const char* suffix : {"", "-wal", "-shm"})
      if (fs::exists(base.string() + suffix))
        fs::copy_file(base.string() + suffix, work.string() + suffix,
                      fs::copy_options::overwrite_existing);
    controller = std::make_unique<GameController>();
    ASSERT_TRUE(controller->loadGame(WORK_SLOT));
    ASSERT_TRUE(controller->hasSelectedTeam());
  }

  void TearDown() override
  {
    driver.reset();
    view.reset();
    controller.reset();
    SettingsManager::instance()->get() = Settings{};
    LanguageManager::instance().loadLanguage(Language::EN);
  }

  /** Opens the GUI on the club hub. */
  void openGui()
  {
    view = std::make_unique<GUIView>(*controller);
    ASSERT_TRUE(Bridge::initialize(*view));
    ImGui::GetIO().IniFilename = nullptr;
    view->changeScene(std::make_unique<MainGameScene>(view.get()));
    driver = std::make_unique<Driver>(*view, *controller);
    driver->frames(3);
    ASSERT_EQ(driver->activeId(), SceneID::GAME_MENU);
  }

  const Team& club() const { return controller->getManagedTeam()->get(); }
  Team& mutableClub() { return controller->getManagedTeam()->get(); }
  Player& mutablePlayer(PlayerID id)
  {
    return controller->getGameData()->getPlayers().at(id);
  }

  /** Continues headlessly until today is a managed match day. */
  void advanceToMatchDay()
  {
    for (int guard = 0; guard < 8; ++guard)
    {
      if (managedFixtureToday()) return;
      controller->advanceToNextManagedFixture();
    }
    ASSERT_TRUE(managedFixtureToday()) << "no managed fixture reached";
  }
  /** What a manager does on a FIX LINEUP day: let the assistant fix it. */
  void fixLineupForToday()
  {
    if (const auto fixture = managedFixtureToday())
      controller->autoFixLineup(managed_id, fixture->getMatchType());
  }
  std::optional<Match> managedFixtureToday() const
  {
    for (const Match& match : controller->getTeamFixtures(managed_id))
      if (match.getDate() == controller->getCurrentDate() && !match.isPlayed())
        return match;
    return std::nullopt;
  }

  void expectWorldConsistent(const std::string& stage) const
  {
    const auto problems = checkWorld(*controller);
    EXPECT_TRUE(problems.empty()) << stage << ":" << joined(problems);
  }

  std::unique_ptr<GameController> controller;
  std::unique_ptr<GUIView> view;
  std::unique_ptr<Driver> driver;
};

// ---- Continue
// ----------------------------------------------------------------

/** Three clicks on Continue in consecutive frames are one Continue. */
TEST_F(Adversarial, TripleClickContinueIsOneContinue)
{
  openGui();
  const MainGameScene* hub = driver->hub();
  const auto next = Bridge::nextFixtureDate(*hub);
  ASSERT_TRUE(next.has_value());
  ASSERT_FALSE(*next == controller->getCurrentDate());
  const auto button = driver->continuePoint();
  ASSERT_TRUE(button.has_value()) << "Continue button not found";
  const std::string today = controller->getCurrentDate().toString();
  driver->click(*button, 3);
  // Running, about to start, or (only once no worker runs) already done.
  ASSERT_TRUE(driver->advancing() || Bridge::continueRequested(*hub) ||
              controller->getCurrentDate().toString() != today)
      << "the clicks on Continue did not start a Continue";
  ASSERT_TRUE(driver->settle());
  EXPECT_EQ(controller->getCurrentDate().toString(), next->toString())
      << "Continue should stop on the next managed fixture";
  EXPECT_EQ(driver->activeId(), SceneID::GAME_MENU)
      << "an extra click must not also kick off the match";
  EXPECT_EQ(driver->imgui_errors, 0);
  expectWorldConsistent("after triple click");
}

/**
 * While days are simulated, shortcuts and clicks (save, navigation, another
 * Continue) must neither touch the world nor open screens.
 */
TEST_F(Adversarial, InputDuringContinueIsIgnored)
{
  openGui();
  const MainGameScene* hub = driver->hub();
  const auto next = Bridge::nextFixtureDate(*hub);
  ASSERT_TRUE(next.has_value());
  disableAutosave(*controller);
  const std::string saved = savedDate(WORK_SLOT);
  ASSERT_FALSE(saved.empty());
  const auto button = driver->continuePoint();
  ASSERT_TRUE(button.has_value()) << "Continue button not found";
  driver->space();
  int mashes = 0;
  const auto start = Clock::now();
  while ((driver->advancing() || Bridge::continueRequested(*hub)) &&
         Clock::now() - start < std::chrono::seconds(120))
  {
    if (!driver->advancing())
    {
      driver->frame();
      continue;
    }
    ++mashes;
    switch (mashes % 5)
    {
      case 0:
        driver->save();
        break;
      case 1:
        driver->key(SDLK_F3, SDL_SCANCODE_F3);
        break;
      case 2:
        driver->space();
        break;
      case 3:
        driver->key(SDLK_K, SDL_SCANCODE_K, SDL_KMOD_LCTRL);
        break;
      default:
        driver->click(*button);
        break;
    }
    if (driver->advancing())
      EXPECT_EQ(view->getOverlayDepth(), 0u)
          << "a screen opened while the world is simulated";
  }
  driver->settle();
  EXPECT_GT(mashes, 0) << "Continue finished before any input was tried";
  EXPECT_EQ(controller->getCurrentDate().toString(), next->toString());
  EXPECT_EQ(driver->imgui_errors, 0);
  EXPECT_EQ(savedDate(WORK_SLOT), saved)
      << "Ctrl+S pressed during Continue saved the game mid-simulation";
  // Saving works again once the days are done. A key mashed in the very
  // frame Continue finished is applied (Space kicks off today's match,
  // Ctrl+K opens the palette): return to the hub first.
  driver->escape();
  driver->escape();
  if (driver->activeId() != SceneID::GAME_MENU)
  {
    Navigation::open(view.get(), NavSection::HOME);
    driver->frames(3);
  }
  const std::string state = std::format(
      "scene {} overlays {} popup {} active id {} text input {} nav {} "
      "mashes {}",
      static_cast<int>(std::to_underlying(driver->activeId())),
      view->getOverlayDepth(),
      ImGui::IsPopupOpen(
          "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
      GImGui->ActiveId, ImGui::GetIO().WantTextInput, ImGui::GetIO().NavVisible,
      mashes);
  driver->save();
  driver->frames(2);
  EXPECT_EQ(savedDate(WORK_SLOT), controller->getCurrentDate().toString())
      << "Ctrl+S after Continue did not save (" << state << ")";
  expectWorldConsistent("after mashing during Continue");
}

/** A modal dialog swallows Continue (key and button) until it is closed. */
TEST_F(Adversarial, ContinueWhileDialogOpen)
{
  openGui();
  auto* shell = dynamic_cast<ManagementScene*>(driver->active());
  ASSERT_NE(shell, nullptr);
  const std::string today = controller->getCurrentDate().toString();
  const auto button = driver->continuePoint();
  ASSERT_TRUE(button.has_value()) << "Continue button not found";
  Bridge::requestMainMenu(*shell);
  driver->frames(2);
  ASSERT_TRUE(ImGui::IsPopupOpen(
      "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel));
  driver->space();
  driver->click(*button, 2);
  driver->settle();
  EXPECT_EQ(controller->getCurrentDate().toString(), today)
      << "Continue ran behind a modal dialog";
  EXPECT_NE(driver->view.getBaseScene(), nullptr);
  driver->escape();
  driver->frames(2);
  EXPECT_FALSE(ImGui::IsPopupOpen(
      "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel));
  EXPECT_EQ(driver->activeId(), SceneID::GAME_MENU);

  // Continue from a profile whose renewal dialog is open: the dialog's
  // screen closes first, and nothing is left half open.
  const PlayerID player = club().getPlayerIDs().front();
  Navigation::openPlayer(view.get(), player);
  driver->frames(3);
  auto* profile = dynamic_cast<PlayerProfileScene*>(driver->active());
  ASSERT_NE(profile, nullptr);
  Bridge::requestRenew(*profile);
  driver->frames(3);
  driver->hub()->requestContinue();
  driver->settle();
  EXPECT_EQ(view->getOverlayDepth(), 0u);
  EXPECT_NE(controller->getCurrentDate().toString(), today);
  EXPECT_EQ(driver->imgui_errors, 0);
  expectWorldConsistent("after Continue from a dialog");
}

/** Loading a save and pressing Continue on its very first frame. */
TEST_F(Adversarial, LoadThenImmediatelyContinue)
{
  openGui();
  driver->save();
  const std::string saved = controller->getCurrentDate().toString();
  view->changeScene(std::make_unique<MainMenuScene>(view.get()));
  driver->frames(2);
  auto* menu = dynamic_cast<MainMenuScene*>(view->getBaseScene());
  ASSERT_NE(menu, nullptr);
  Bridge::menuLoad(*menu, WORK_SLOT);
  const auto start = Clock::now();
  while (driver->hub() == nullptr &&
         Clock::now() - start < std::chrono::seconds(90))
    driver->frame();
  ASSERT_NE(driver->hub(), nullptr) << "slot did not load";
  EXPECT_EQ(controller->getCurrentDate().toString(), saved);
  driver->space();
  driver->settle();
  EXPECT_NE(controller->getCurrentDate().toString(), saved)
      << "Continue on the first frame after loading was lost";
  EXPECT_EQ(driver->imgui_errors, 0);
  expectWorldConsistent("load then continue");
}

// ---- Shutdown
// ------------------------------------------------------------------

/**
 * Aborts the process when its scope does not end within @p limit: a hang
 * while quitting fails fast with a message instead of stalling the run.
 */
class ShutdownWatchdog
{
 public:
  explicit ShutdownWatchdog(const char* what)
      : watcher(
            [this, what]
            {
              std::unique_lock lock(mutex);
              if (!finished.wait_for(lock, LIMIT, [this] { return done; }))
              {
                std::print(stderr, "{} did not finish within {} s\n", what,
                           LIMIT.count());
                std::abort();
              }
            })
  {
  }
  ~ShutdownWatchdog()
  {
    {
      const std::scoped_lock lock(mutex);
      done = true;
    }
    finished.notify_one();
    watcher.join();
  }
  ShutdownWatchdog(const ShutdownWatchdog&) = delete;
  ShutdownWatchdog& operator=(const ShutdownWatchdog&) = delete;

 private:
  static constexpr std::chrono::seconds LIMIT{60};
  std::mutex mutex;
  std::condition_variable finished;
  bool done = false;
  std::jthread watcher;  // Last: starts once the state above exists.
};

/** Played fixtures of the whole world (every league and cup). */
size_t playedFixtures(const GameController& controller)
{
  size_t played = 0;
  for (const auto& [date, matches] :
       controller.getGame()->getCalendar().getFullCalendar())
    played += static_cast<size_t>(std::ranges::count_if(
        matches, [](const Match& match) { return match.isPlayed(); }));
  return played;
}

/**
 * Quitting the app after parallel matchdays, in main()'s order: the window
 * closes, the game is saved, then the controller (with the scheduler's
 * worker pool) is destroyed.
 */
TEST_F(Adversarial, QuitAfterParallelMatchdaysShutsDownCleanly)
{
  controller->setSimulationThreads(4);
  openGui();
  // One Continue through the GUI (days on the Continue worker)...
  driver->hub()->requestContinue();
  ASSERT_TRUE(driver->settle());
  // ...then whole matchdays of every league.
  const GameDateValue end =
      SeasonCalendar::addDays(controller->getCurrentDate(), 45);
  while (controller->getCurrentDate() < end) controller->advanceDay();
  driver->frames(3);
  ASSERT_GE(playedFixtures(*controller), 100u)
      << "the parallel scheduler never ran";
  EXPECT_EQ(driver->imgui_errors, 0);

  const ShutdownWatchdog watchdog("quitting after parallel matchdays");
  driver.reset();
  view.reset();
  EXPECT_TRUE(controller->saveGame());
  controller.reset();
}

/**
 * The window closes while Continue is simulating: closing waits for the
 * days in flight (the hub owns the worker), so the controller is never
 * destroyed or saved under a running simulation.
 */
TEST_F(Adversarial, ClosingDuringContinueWaitsForTheDaysInFlight)
{
  controller->setSimulationThreads(4);
  openGui();
  const auto next = Bridge::nextFixtureDate(*driver->hub());
  ASSERT_TRUE(next.has_value());
  ASSERT_FALSE(*next == controller->getCurrentDate());
  driver->hub()->requestContinue();
  for (int frame = 0; frame < 10 && !driver->advancing(); ++frame)
    driver->frame();
  ASSERT_TRUE(driver->advancing());
  // The worker counts the days to simulate only once it has started: wait
  // for that count (without frames, so the hub cannot finish meanwhile).
  GameController::ContinueProgress progress = controller->getContinueProgress();
  const auto counting = std::chrono::steady_clock::now();
  while (progress.days_total == 0 &&
         std::chrono::steady_clock::now() - counting < std::chrono::seconds(10))
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    progress = controller->getContinueProgress();
  }
  ASSERT_GT(progress.days_total, 0) << "the Continue worker never started";
  EXPECT_LT(progress.days_done, progress.days_total)
      << "Continue finished before the window closed";

  {
    const ShutdownWatchdog watchdog("closing the window during Continue");
    driver.reset();
    view.reset();
  }
  EXPECT_EQ(controller->getCurrentDate().toString(), next->toString())
      << "the days in flight were not completed before the window closed";
  EXPECT_TRUE(controller->saveGame());
  const ShutdownWatchdog watchdog("destroying the controller after Continue");
  controller.reset();
}

// ---- Transfers
// -----------------------------------------------------------------

/** An affordable player of another club whose fee is accepted. */
std::optional<PlayerID> agreeFee(GameController& controller, TeamID managed)
{
  ScoutSearchFilter filter;
  filter.min_age = 20;
  filter.max_age = 30;
  const uint32_t budget = controller.transferBudgetForTeam(managed);
  filter.max_value = budget / 2;
  filter.limit = 0;
  const auto rows = controller.searchScoutedPlayers(filter);
  int tried = 0;
  for (const ScoutedPlayerRow& row : rows)
  {
    if (row.team_id == managed || row.team_id == FREE_AGENTS_TEAM_ID ||
        row.estimated_value <= 0)
      continue;
    if (++tried > 40) break;
    TransferNegotiation::OfferTerms terms;
    terms.fee = static_cast<uint32_t>(row.estimated_value * 3 / 2);
    const auto response = controller.makeTransferOffer(row.player_id, terms);
    if (response.decision ==
        TransferNegotiation::ClubResponse::Decision::Accept)
      return row.player_id;
  }
  return std::nullopt;
}

/** Terms the player demands for the pending talks. */
TransferNegotiation::ContractOffer demandedTerms(
    const GameController& controller, PlayerID player)
{
  TransferNegotiation::ContractOffer offer;
  const auto kind = controller.getContractTalkKind(player);
  if (!kind) return offer;
  const auto demand = controller.getPlayerDemand(player, *kind);
  offer.weekly_wage = demand.weekly_wage;
  offer.years = std::max<uint8_t>(demand.min_years, 2);
  offer.years = std::min(offer.years, demand.max_years);
  offer.signing_bonus = demand.signing_bonus;
  return offer;
}

/** The target retires (as the season-end sweep does) while talks are open. */
TEST_F(Adversarial, OfferForPlayerWhoRetiresMeanwhile)
{
  const auto target = agreeFee(*controller, managed_id);
  if (!target.has_value()) GTEST_SKIP() << "no affordable target in this world";
  const TeamID seller =
      controller->getGameData()->getPlayer(*target)->get().getTeamId();
  // WorldSimulation::retirePlayers: out of the squad, lineups rebuilt,
  // then erased.
  auto data = controller->getGameData();
  data->getTeam(seller)->get().removePlayerID(*target);
  data->getTeam(seller)->get().generateStartingXI(*data,
                                                  controller->getStatsConfig());
  ASSERT_TRUE(data->removePlayer(*target));

  EXPECT_FALSE(controller->getContractTalkKind(*target).has_value())
      << "talks with a retired player are still open";
  const auto talk =
      controller->proposeContract(*target, demandedTerms(*controller, *target));
  EXPECT_FALSE(talk.completed) << "signed a player who no longer exists";
  for (int day = 0; day < 8; ++day) controller->advanceDay();
  controller->saveGame();
  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(WORK_SLOT));
  EXPECT_FALSE(reloaded.getGameData()->getPlayer(*target).has_value());
  expectWorldConsistent("after retirement during talks");
}

/** The target is sold to a third club while talks are open. */
TEST_F(Adversarial, OfferForPlayerSoldMeanwhile)
{
  const auto target = agreeFee(*controller, managed_id);
  if (!target.has_value()) GTEST_SKIP() << "no affordable target in this world";
  const TeamID seller =
      controller->getGameData()->getPlayer(*target)->get().getTeamId();
  // An AI club completes a deal for him first (the AI transfer path).
  const uint32_t price = controller->getPlayerMarketValue(*target);
  controller->listPlayerForTransfer(*target, price);
  bool sold = false;
  for (const auto& team : controller->getTeams())
  {
    const TeamID buyer = team.get().getId();
    if (buyer == seller || buyer == managed_id || buyer == FREE_AGENTS_TEAM_ID)
      continue;
    if ((sold = controller->buyPlayer(*target, buyer, price))) break;
  }
  if (!sold) GTEST_SKIP() << "no AI club could buy the target";
  const int64_t balance = club().getFinances().getBalance();
  const auto talk =
      controller->proposeContract(*target, demandedTerms(*controller, *target));
  const auto player = controller->getGameData()->getPlayer(*target);
  ASSERT_TRUE(player.has_value());
  if (talk.completed)
  {
    EXPECT_EQ(player->get().getTeamId(), managed_id);
    EXPECT_NE(club().getFinances().getBalance(), balance)
        << "the player moved without a fee";
  }
  EXPECT_FALSE(talk.completed && player->get().getTeamId() != managed_id);
  EXPECT_NE(player->get().getTeamId(), seller);
  expectWorldConsistent("after the target was sold elsewhere");
}

/** Talks agreed on deadline day cannot complete once the window shuts. */
TEST_F(Adversarial, WindowClosesMidNegotiation)
{
  for (int guard = 0; guard < 120; ++guard)
  {
    if (const auto window = controller->getTransferWindow();
        window.open && window.days_to_deadline == 0)
      break;
    controller->advanceDay();
  }
  const auto window = controller->getTransferWindow();
  ASSERT_TRUE(window.open && window.days_to_deadline == 0)
      << "deadline day not reached";
  const auto target = agreeFee(*controller, managed_id);
  if (!target.has_value())
    GTEST_SKIP() << "no affordable target on deadline day";
  const TeamID seller =
      controller->getGameData()->getPlayer(*target)->get().getTeamId();
  controller->advanceDay();
  ASSERT_FALSE(controller->getTransferWindow().open);
  const auto talk =
      controller->proposeContract(*target, demandedTerms(*controller, *target));
  EXPECT_FALSE(talk.completed)
      << "a permanent transfer completed after the window shut";
  EXPECT_EQ(controller->getGameData()->getPlayer(*target)->get().getTeamId(),
            seller);
  expectWorldConsistent("after the window shut");
}

/** With no cash at all, free agents must not be signed into debt. */
TEST_F(Adversarial, ZeroCashThenSignFreeAgent)
{
  const auto findFreeAgent = [&]() -> std::optional<PlayerID>
  {
    for (const auto& player : controller->getGameData()->getPlayersVector())
      if (player.get().getTeamId() == FREE_AGENTS_TEAM_ID)
        return player.get().getId();
    return std::nullopt;
  };
  for (int day = 0; day < 20 && !findFreeAgent().has_value(); ++day)
    controller->advanceDay();
  std::optional<PlayerID> freeAgent = findFreeAgent();
  if (!freeAgent.has_value())
  {
    // None in the world yet: release a fringe player and try to re-sign him.
    const PlayerID fringe = club().getPlayerIDs().back();
    if (controller->releasePlayer(fringe)) freeAgent = fringe;
  }
  if (!freeAgent.has_value()) GTEST_SKIP() << "no free agent available";
  Team& team = mutableClub();
  team.getFinances().record(controller->getCurrentDate(),
                            FinanceCategory::Adjustment,
                            -team.getFinances().getBalance());
  ASSERT_EQ(team.getFinances().getBalance(), 0);
  const auto talk = controller->proposeContract(
      *freeAgent, demandedTerms(*controller, *freeAgent));
  EXPECT_GE(club().getFinances().getBalance(), 0)
      << "signing a free agent with no cash pushed the balance negative";
  if (talk.completed) EXPECT_TRUE(talk.over_budget == false);
  expectWorldConsistent("after signing with no cash");
}

// ---- Squad
// ---------------------------------------------------------------------

/** Releasing a starter must drop him from the lineup; the match still plays. */
TEST_F(Adversarial, ReleasePlayerInLineup)
{
  const Player* goalkeeper = club().getLineup().getGoalkeeper();
  ASSERT_NE(goalkeeper, nullptr);
  const PlayerID released = goalkeeper->getId();
  ASSERT_TRUE(controller->releasePlayer(released));
  expectWorldConsistent("after releasing the goalkeeper");
  const Lineup& lineup = club().getLineup();
  EXPECT_TRUE(lineup.getGoalkeeper() == nullptr ||
              lineup.getGoalkeeper()->getId() != released);

  advanceToMatchDay();
  fixLineupForToday();
  openGui();
  driver->space();
  driver->frames(3);
  auto* match = dynamic_cast<MatchScene*>(driver->active());
  ASSERT_NE(match, nullptr) << "PLAY MATCH did not open the match";
  if (Bridge::engine(*match) == nullptr) GTEST_SKIP() << "lineup gate shown";
  EXPECT_TRUE(Bridge::quickResult(*match));
  driver->settle();
  expectWorldConsistent("after the match without the released player");
}

/** Releasing a non-starter must not undo the manager's chosen XI. */
TEST_F(Adversarial, ReleasingAReserveKeepsTheChosenLineup)
{
  Lineup& lineup = mutableClub().getLineup();
  const auto& reserves = lineup.getReserves();
  ASSERT_GE(reserves.size(), 2u);
  ASSERT_FALSE(lineup.getOutfieldPlayers().empty());
  // The manager's call: a reserve starts instead of a regular.
  const PlayerID pick = reserves.front()->getId();
  const PlayerID dropped = lineup.getOutfieldPlayers().front().player->getId();
  ASSERT_TRUE(lineup.swapPlayers(pick, dropped));
  // A player outside the starting XI leaves.
  std::unordered_set starting{dropped};
  if (lineup.getGoalkeeper()) starting.insert(lineup.getGoalkeeper()->getId());
  for (const auto& positioned : lineup.getOutfieldPlayers())
    starting.insert(positioned.player->getId());
  std::optional<PlayerID> fringe;
  for (const PlayerID id : club().getPlayerIDs())
    if (!starting.contains(id)) fringe = id;
  ASSERT_TRUE(fringe.has_value());
  std::vector<PlayerID> chosen;
  if (lineup.getGoalkeeper()) chosen.push_back(lineup.getGoalkeeper()->getId());
  for (const auto& positioned : lineup.getOutfieldPlayers())
    chosen.push_back(positioned.player->getId());
  ASSERT_TRUE(controller->releasePlayer(*fringe));
  const auto& starters = club().getLineup().getOutfieldPlayers();
  const bool kept = std::ranges::any_of(
      starters, [pick](const auto& positioned)
      { return positioned.player && positioned.player->getId() == pick; });
  EXPECT_TRUE(kept) << "releasing a player outside the starting XI must not "
                       "rebuild the manager's selection";
  // The whole XI is unchanged and the released player is gone from the
  // bench too.
  std::vector<PlayerID> after;
  if (club().getLineup().getGoalkeeper())
    after.push_back(club().getLineup().getGoalkeeper()->getId());
  for (const auto& positioned : starters)
    after.push_back(positioned.player->getId());
  EXPECT_EQ(after, chosen);
  for (const Player* reserve : club().getLineup().getReserves())
    EXPECT_NE(reserve->getId(), *fringe);
  expectWorldConsistent("after releasing a reserve");
}

/** Try to release the squad down to ten, then play. */
TEST_F(Adversarial, SquadBelowElevenThenPlay)
{
  std::vector<PlayerID> squad = club().getPlayerIDs();
  size_t refused = 0;
  for (const PlayerID id : squad)
  {
    if (club().getPlayerIDs().size() <= 10) break;
    if (!controller->releasePlayer(id)) ++refused;
  }
  const size_t left = club().getPlayerIDs().size();
  RecordProperty("squad_left", static_cast<int>(left));
  expectWorldConsistent("after releasing the squad");
  advanceToMatchDay();
  fixLineupForToday();
  openGui();
  const std::string matchDay = controller->getCurrentDate().toString();
  driver->space();
  driver->frames(3);
  // Whatever the club has left, the career must go on: either the match is
  // played or the manager is told why and can leave the screen.
  if (auto* match = dynamic_cast<MatchScene*>(driver->active()))
  {
    if (Bridge::engine(*match) != nullptr)
      EXPECT_TRUE(Bridge::quickResult(*match));
    driver->settle();
  }
  for (int attempt = 0; attempt < 3 && driver->activeId() != SceneID::GAME_MENU;
       ++attempt)
  {
    driver->escape();
    driver->key(SDLK_F1, SDL_SCANCODE_F1);
  }
  EXPECT_EQ(driver->imgui_errors, 0);
  expectWorldConsistent("after playing with ten");
  // Releases stop at eleven seniors with a goalkeeper, so the match can
  // always be played.
  EXPECT_GE(left, 11u) << "release refused " << refused << " times";
  EXPECT_GT(refused, 0u);
  EXPECT_NE(controller->getCurrentDate().toString(), matchDay)
      << "the career is stuck on match day with " << left << " players";
}

/** Every player injured on match day: the career must not dead-end. */
TEST_F(Adversarial, AllPlayersInjuredOnMatchDay)
{
  advanceToMatchDay();
  for (const PlayerID id : club().getPlayerIDs())
  {
    PlayerDynamics& dynamics = mutablePlayer(id).mutableDynamics();
    dynamics.injury = InjuryType::HamstringStrain;
    dynamics.injury_days = 30;
  }
  const auto fixture = managedFixtureToday();
  ASSERT_TRUE(fixture.has_value());
  openGui();
  // The assistant's best selection is injured players nobody fit can
  // replace: the dashboard and the next steps must not ask for a fix that
  // changes nothing.
  controller->autoFixLineup(managed_id, fixture->getMatchType());
  ASSERT_TRUE(controller->canKickOff(managed_id, fixture->getMatchType()));
  ASSERT_FALSE(
      controller->getIneligibleSelections(managed_id, fixture->getMatchType())
          .empty());
  Bridge::refresh(*driver->hub());
  driver->frames(2);
  EXPECT_FALSE(Bridge::lineupBlocked(*driver->hub()));
  EXPECT_TRUE(std::ranges::none_of(
      controller->getNextActions(10), [](const NextAction& action)
      { return action.kind == NextActionKind::UnavailableInLineup; }));
  const std::string matchDay = controller->getCurrentDate().toString();
  bool kickedOff = false;
  for (int attempt = 0; attempt < 4 && !kickedOff; ++attempt)
  {
    driver->hub()->requestContinue();
    driver->frames(3);
    if (auto* match = dynamic_cast<MatchScene*>(driver->active()))
    {
      if (const MatchEngine* engine = Bridge::engine(*match))
      {
        kickedOff = true;
        // The assistant fielded a full XI of players who play through
        // their injuries, and could not pick a better one.
        const bool home = fixture->getHomeTeamId() == managed_id;
        const auto starters = std::ranges::count_if(
            engine->getPlayers(), [home](const MatchPlayer& player)
            { return player.isHomeTeam == home && player.onPitch; });
        EXPECT_EQ(starters, 11);
        EXPECT_TRUE(
            controller->canKickOff(managed_id, fixture->getMatchType()));
        EXPECT_TRUE(
            controller->previewLineupFix(managed_id, fixture->getMatchType())
                .empty())
            << "the assistant's selection is not stable";
        EXPECT_TRUE(Bridge::quickResult(*match));
        driver->settle();
        continue;
      }
    }
    Navigation::open(view.get(), NavSection::HOME);
    driver->frames(3);
  }
  EXPECT_TRUE(kickedOff) << "the managed match never kicked off";
  EXPECT_EQ(driver->imgui_errors, 0);
  expectWorldConsistent("all injured");
  EXPECT_NE(controller->getCurrentDate().toString(), matchDay)
      << "with every player injured the career is stuck on " << matchDay;
  EXPECT_TRUE(controller
                  ->getMatchReport(GameDateValue::fromString(matchDay),
                                   fixture->getHomeTeamId(),
                                   fixture->getAwayTeamId())
                  .has_value())
      << "the managed match was not played";
}

// ---- Match
// -------------------------------------------------------------------

/**
 * Finish match returns to the window at once: the rest of the match day
 * (other matches, autosave) runs on the Continue worker behind the progress
 * card, then the report opens and the date has moved on by one day.
 */
TEST_F(Adversarial, FinishMatchKeepsTheWindowResponsive)
{
  advanceToMatchDay();
  fixLineupForToday();
  const auto fixture = managedFixtureToday();
  ASSERT_TRUE(fixture.has_value());
  const GameDateValue matchDay = controller->getCurrentDate();
  openGui();
  driver->hub()->requestContinue();
  driver->frames(3);
  auto* match = dynamic_cast<MatchScene*>(driver->active());
  ASSERT_NE(match, nullptr) << "PLAY MATCH did not open the match";
  MatchEngine* engine = Bridge::engine(*match);
  ASSERT_NE(engine, nullptr) << "lineup gate shown";
  engine->simulateToEnd();
  driver->frame();
  ASSERT_TRUE(Bridge::finished(*match));

  // The slowest frame from pressing Finish until the day is simulating.
  using Seconds = std::chrono::duration<double>;
  auto started = Clock::now();
  ASSERT_TRUE(Bridge::finishMatch(*match));
  double slowest = Seconds(Clock::now() - started).count();
  for (int frame = 0; frame < 5 && !driver->advancing(); ++frame)
  {
    started = Clock::now();
    driver->frame();
    slowest = std::max(slowest, Seconds(Clock::now() - started).count());
  }
  ASSERT_TRUE(driver->advancing())
      << "the rest of the match day is not simulated in the background";
  EXPECT_EQ(driver->activeId(), SceneID::GAME_MENU)
      << "a screen above the hub while the day is simulated";
  // Frames keep coming while the worker simulates the day.
  for (int frame = 0; frame < 3 && driver->advancing(); ++frame)
  {
    started = Clock::now();
    driver->frame();
    slowest = std::max(slowest, Seconds(Clock::now() - started).count());
  }
  EXPECT_LT(slowest, 1.0) << "Finish match blocked the window";

  ASSERT_TRUE(driver->settle());
  EXPECT_EQ(driver->activeId(), SceneID::MATCH_REPORT)
      << "the match report did not open after the day";
  EXPECT_EQ(controller->getCurrentDate().toString(),
            SeasonCalendar::addDays(matchDay, 1).toString());
  EXPECT_TRUE(controller
                  ->getMatchReport(matchDay, fixture->getHomeTeamId(),
                                   fixture->getAwayTeamId())
                  .has_value());
  EXPECT_EQ(driver->imgui_errors, 0);
  expectWorldConsistent("after finishing the match");
}

/**
 * Tactics and a window resize mid-match, then the app is closed: the day is
 * saved unplayed (main() saves after the loop) and the match is offered
 * again after loading.
 */
TEST_F(Adversarial, ChangeTacticsMidMatchThenQuit)
{
  advanceToMatchDay();
  fixLineupForToday();
  openGui();
  driver->space();
  driver->frames(3);
  auto* match = dynamic_cast<MatchScene*>(driver->active());
  ASSERT_NE(match, nullptr);
  if (Bridge::engine(*match) == nullptr) GTEST_SKIP() << "lineup gate shown";
  Bridge::setSpeed(*match, 16.0f);
  for (int index = 0; index < 120; ++index) Bridge::update(*view, 0.1f);
  StrategySliders sliders = club().getStrategy().getSliders();
  sliders.pressing = 0.9f;
  sliders.offensiveBias = 0.8f;
  mutableClub().getStrategy().setAllSliders(sliders);
  driver->frames(2);
  const std::string matchDay = controller->getCurrentDate().toString();

  SDL_Event quit{};
  quit.type = SDL_EVENT_QUIT;
  SDL_PushEvent(&quit);
  driver->frame();
  EXPECT_FALSE(Bridge::running(*view));
  // main(): scenes are released, then the loaded game is saved.
  driver.reset();
  view.reset();
  controller->saveGame();
  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(WORK_SLOT));
  EXPECT_EQ(controller->getCurrentDate().toString(), matchDay);
  const auto fixture = managedFixtureToday();
  EXPECT_TRUE(fixture.has_value()) << "the interrupted match was lost";
  EXPECT_NEAR(club().getStrategy().getSliders().pressing, 0.9f, 1e-3f)
      << "tactics changed mid-match were not saved";
  openGui();
  EXPECT_NE(driver->hub()->continueLabel().find(LOC("DASHBOARD_PLAY_MATCH")),
            std::string::npos)
      << driver->hub()->continueLabel();
  expectWorldConsistent("after quitting mid-match");
}

/** Resizing through every size and minimizing during a live match. */
TEST_F(Adversarial, ResizeAndMinimizeDuringMatch)
{
  advanceToMatchDay();
  fixLineupForToday();
  openGui();
  driver->space();
  driver->frames(3);
  auto* match = dynamic_cast<MatchScene*>(driver->active());
  ASSERT_NE(match, nullptr);
  if (Bridge::engine(*match) == nullptr) GTEST_SKIP() << "lineup gate shown";
  SDL_Window* window = view->getWindow();
  const auto pushWindowEvent = [&](SDL_EventType type, int width, int height)
  {
    SDL_Event event{};
    event.type = type;
    event.window.windowID = SDL_GetWindowID(window);
    event.window.data1 = width;
    event.window.data2 = height;
    SDL_PushEvent(&event);
  };
  for (const auto [width, height] :
       {std::pair{1024, 700}, std::pair{2560, 1440}, std::pair{640, 480},
        std::pair{1920, 1080}, std::pair{1280, 720}})
  {
    SDL_SetWindowSize(window, width, height);
    pushWindowEvent(SDL_EVENT_WINDOW_RESIZED, width, height);
    driver->frames(2);
    for (int index = 0; index < 30; ++index) Bridge::update(*view, 0.1f);
    driver->frames(2);
  }
  SDL_MinimizeWindow(window);
  pushWindowEvent(SDL_EVENT_WINDOW_MINIMIZED, 0, 0);
  driver->frames(20);
  SDL_RestoreWindow(window);
  pushWindowEvent(SDL_EVENT_WINDOW_RESTORED, 0, 0);
  driver->frames(3);
  EXPECT_EQ(driver->imgui_errors, 0) << "ImGui errors while resizing a match";
  EXPECT_TRUE(Bridge::quickResult(*match));
  driver->settle();
  expectWorldConsistent("after resizing a match");
}

// ---- Persistence
// -------------------------------------------------------------

/**
 * A save from before the scouting/staff/training/transfer-deal/world tables
 * existed loads and plays on for a month.
 */
TEST_F(Adversarial, OldSchemaSaveLoadsAndContinues)
{
  controller->saveGame();
  const std::string date = controller->getCurrentDate().toString();
  controller.reset();
  {
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(RuntimePaths::savePath(WORK_SLOT).c_str(), &db),
              SQLITE_OK);
    for (const char* table :
         {"ScoutingState",   "ScoutingKnowledge",   "ScoutAssignments",
          "ScoutReports",    "RecruitmentFocus",    "ScoutShortlist",
          "Staff",           "TeamTraining",        "PlayerTraining",
          "TransferHistory", "TransferObligations", "Loans",
          "PreContracts",    "PlayerMarketFlags",   "TransferOffers",
          "InboxMessages",   "BoardState",          "WorldState",
          "SeasonHistory",   "PlayerDiscipline",    "PlayerSeasonStats",
          "MatchReports"})
    {
      const std::string sql = std::format("DROP TABLE IF EXISTS {};", table);
      EXPECT_EQ(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr),
                SQLITE_OK)
          << sqlite3_errmsg(db);
    }
    sqlite3_close(db);
  }
  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(WORK_SLOT)) << "old save refused";
  EXPECT_EQ(controller->getCurrentDate().toString(), date);
  expectWorldConsistent("old save loaded");
  const int start = dayNumber(controller->getCurrentDate());
  while (dayNumber(controller->getCurrentDate()) - start < 30)
    controller->advanceDay();
  controller->saveGame();
  expectWorldConsistent("old save after a month");
  GameController reloaded;
  EXPECT_TRUE(reloaded.loadGame(WORK_SLOT));
  EXPECT_EQ(reloaded.getCurrentDate().toString(),
            controller->getCurrentDate().toString());
}

/** A save without the finance ledger (older schema) keeps its balances. */
TEST_F(Adversarial, SaveWithoutLedgerKeepsBalances)
{
  controller->saveGame();
  const int64_t balance = club().getFinances().getBalance();
  controller.reset();
  {
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(RuntimePaths::savePath(WORK_SLOT).c_str(), &db),
              SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "DROP TABLE IF EXISTS FinanceLedger;", nullptr,
                           nullptr, nullptr),
              SQLITE_OK);
    sqlite3_close(db);
  }
  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(WORK_SLOT));
  EXPECT_EQ(club().getFinances().getBalance(), balance);
  expectWorldConsistent("save without ledger");
}

/**
 * Playing on without saving and quitting must leave the slot exactly as it
 * was last saved: nothing of the unsaved days may leak into the file.
 */
TEST_F(Adversarial, QuitWithoutSavingKeepsTheSave)
{
  const std::string date = controller->getCurrentDate().toString();
  std::map<TeamID, std::vector<PlayerID>> squads;
  std::map<TeamID, int64_t> balances;
  for (const auto& team : controller->getTeams())
  {
    squads[team.get().getId()] = team.get().getPlayerIDs();
    std::ranges::sort(squads[team.get().getId()]);
    balances[team.get().getId()] = team.get().getFinances().getBalance();
  }
  disableAutosave(*controller);
  for (int day = 0; day < 14; ++day) controller->advanceDay();
  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(WORK_SLOT));
  EXPECT_EQ(controller->getCurrentDate().toString(), date)
      << "the slot moved on without a save (autosave is off)";
  int changedSquads = 0;
  int changedBalances = 0;
  for (const auto& team : controller->getTeams())
  {
    std::vector<PlayerID> ids = team.get().getPlayerIDs();
    std::ranges::sort(ids);
    if (ids != squads[team.get().getId()]) ++changedSquads;
    if (team.get().getFinances().getBalance() != balances[team.get().getId()])
      ++changedBalances;
  }
  const auto problems = checkWorld(*controller);
  if (changedSquads > 0 || changedBalances > 0 || !problems.empty())
    ADD_FAILURE() << "unsaved days leaked into the slot: after 14 unsaved "
                     "days and a reload, "
                  << changedSquads << " squads and " << changedBalances
                  << " balances differ from the last save; invariants:"
                  << joined(problems);
}

/** Season rollover with talks, offers, a loan and a pre-contract open. */
TEST_F(Adversarial, SeasonRolloverWithOpenDeals)
{
  if (std::getenv("FM_ADVERSARIAL_LONG") == nullptr)
    GTEST_SKIP() << "long (a whole season); set FM_ADVERSARIAL_LONG=1";
  const int season = controller->getCurrentSeason();
  bool loanedOut = false;
  bool preContract = false;
  const auto started = Clock::now();
  while (controller->getCurrentSeason() == season &&
         Clock::now() - started < std::chrono::minutes(8))
  {
    const GameDateValue today = controller->getCurrentDate();
    if (!loanedOut && today.month == 8 && today.day == 20)
    {
      const auto& squad = club().getPlayerIDs();
      loanedOut = controller->setLoanListed(squad.back(), true);
    }
    if (!preContract && today.month == 1 && today.day == 10)
    {
      for (const auto& player : controller->getGameData()->getPlayersVector())
      {
        if (const auto kind =
                controller->getContractTalkKind(player.get().getId());
            kind != TransferNegotiation::ContractKind::PreContract)
          continue;
        preContract = controller
                          ->proposeContract(
                              player.get().getId(),
                              demandedTerms(*controller, player.get().getId()))
                          .completed;
        if (preContract) break;
      }
    }
    if (today.month == 6 && today.day == 25)
    {
      agreeFee(*controller, managed_id);
      expectWorldConsistent("before rollover");
    }
    controller->advanceDay();
  }
  ASSERT_NE(controller->getCurrentSeason(), season) << "season did not end";
  RecordProperty("pre_contract", preContract);
  RecordProperty("loan_listed", loanedOut);
  expectWorldConsistent("after rollover");
  controller->saveGame();
  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(WORK_SLOT));
  EXPECT_TRUE(checkWorld(reloaded).empty()) << joined(checkWorld(reloaded));
}

/**
 * The world is a pure function of the save and the days simulated: the same
 * month played twice (parallel matchdays included) ends identically.
 */
TEST_F(Adversarial, SameSaveSameMonth)
{
  const auto playMonth = [](std::string& out)
  {
    GameController run;
    ASSERT_TRUE(run.loadGame(WORK_SLOT));
    disableAutosave(run);
    for (int day = 0; day < 30; ++day) run.advanceDay();
    for (const auto& team : run.getTeams())
    {
      std::vector<PlayerID> ids = team.get().getPlayerIDs();
      std::ranges::sort(ids);
      out += std::format("{}:{}:", team.get().getId(),
                         team.get().getFinances().getBalance());
      for (const PlayerID id : ids) out += std::format("{},", id);
      out += "\n";
    }
    for (const auto& [date, matches] :
         run.getGame()->getCalendar().getFullCalendar())
      for (const Match& match : matches)
        if (match.isPlayed())
          out += std::format("{} {}-{} {}:{}\n", date.toString(),
                             match.getHomeTeamId(), match.getAwayTeamId(),
                             match.getHomeScore(), match.getAwayScore());
  };
  controller.reset();
  std::string first;
  std::string second;
  playMonth(first);
  playMonth(second);
  ASSERT_FALSE(first.empty());
  if (first != second)
  {
    size_t line = 0;
    size_t index = 0;
    while (index < first.size() && index < second.size() &&
           first[index] == second[index])
      if (first[index++] == '\n') ++line;
    ADD_FAILURE() << "two runs of the same month differ from line " << line
                  << ":\n  " << first.substr(first.rfind('\n', index) + 1, 120)
                  << "\n  "
                  << second.substr(second.rfind('\n', index) + 1, 120);
  }
}

// ---- Settings and language
// -------------------------------------------------------

/** Escape closes every dialog, as it does the confirmation dialogs. */
TEST_F(Adversarial, EscapeClosesEveryDialog)
{
  advanceToMatchDay();
  fixLineupForToday();
  openGui();
  const auto anyPopup = []()
  {
    return ImGui::IsPopupOpen(
        "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
  };
  std::vector<std::string> stuck;
  const auto check = [&](const char* name)
  {
    driver->frames(3);
    if (!anyPopup())
    {
      ADD_FAILURE() << name << " did not open";
      return;
    }
    driver->escape();
    driver->frames(2);
    if (anyPopup())
    {
      stuck.emplace_back(name);
      // Leaving its screen drops the dialog (ImGui closes popups that are
      // no longer submitted).
      Navigation::open(view.get(), NavSection::HOME);
      driver->frames(3);
    }
  };

  auto* shell = dynamic_cast<ManagementScene*>(driver->active());
  ASSERT_NE(shell, nullptr);
  Bridge::requestMainMenu(*shell);
  check("main menu confirmation");

  driver->key(SDLK_K, SDL_SCANCODE_K, SDL_KMOD_LCTRL);
  check("command palette");

  Navigation::openPlayer(view.get(), club().getPlayerIDs().front());
  driver->frames(3);
  if (auto* profile = dynamic_cast<PlayerProfileScene*>(driver->active()))
  {
    Bridge::requestRenew(*profile);
    check("contract renewal (player profile)");
  }
  Navigation::open(view.get(), NavSection::HOME);
  driver->frames(2);

  Navigation::open(view.get(), NavSection::TRANSFERS);
  driver->frames(3);
  if (auto* market = dynamic_cast<TransferMarketScene*>(driver->active()))
  {
    if (Bridge::openFirstOffer(*market, false)) check("transfer offer");
    Navigation::open(view.get(), NavSection::TRANSFERS);
    driver->frames(3);
    market = dynamic_cast<TransferMarketScene*>(driver->active());
    if (market != nullptr && Bridge::openFirstOffer(*market, true))
      check("loan offer");
  }
  Navigation::open(view.get(), NavSection::HOME);
  driver->frames(2);

  driver->space();
  driver->frames(3);
  if (auto* match = dynamic_cast<MatchScene*>(driver->active());
      match != nullptr && Bridge::engine(*match) != nullptr)
  {
    // At 720p and UI scale 1.25 the whole dialog, Close included, fits.
    SettingsManager::instance()->get().ui_scale = 1.25f;
    view->refreshTheme();
    Bridge::showSubstitutions(*match);
    driver->frames(3);
    const ImGuiWindow* dialog =
        ImGui::FindWindowByName("###match_substitutions_modal");
    ASSERT_NE(dialog, nullptr);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    EXPECT_LE(dialog->Rect().Max.y, viewport->WorkPos.y + viewport->WorkSize.y);
    EXPECT_GE(dialog->Rect().Min.y, viewport->WorkPos.y);
    check("substitutions (live match)");
    SettingsManager::instance()->get().ui_scale = 0.0f;
    view->refreshTheme();
  }

  // The in-career settings screen backs out with Escape like Cancel.
  Navigation::open(view.get(), NavSection::HOME);
  driver->frames(2);
  view->navigateTo(std::make_unique<SettingsScene>(view.get(), true));
  driver->frames(3);
  ASSERT_EQ(driver->activeId(), SceneID::SETTINGS);
  driver->escape();
  driver->frames(3);
  if (driver->activeId() == SceneID::SETTINGS)
    stuck.emplace_back("settings screen");

  EXPECT_EQ(driver->imgui_errors, 0);
  std::string list;
  for (const std::string& name : stuck) list += "\n  " + name;
  EXPECT_TRUE(stuck.empty()) << "Escape does not close:" << list;
}

/** Switching language mid-session re-renders every screen cleanly. */
TEST_F(Adversarial, SwitchLanguageMidSession)
{
  using enum NavSection;
  openGui();
  for (const Language language : {Language::IT, Language::EN})
  {
    SettingsManager::instance()->get().language = language;
    LanguageManager::instance().loadLanguage(language);
    for (const NavSection section :
         {HOME, INBOX, CLUB, SQUAD, LINEUP, TACTICS, TRAINING, FIXTURES,
          STANDINGS, TRANSFERS, SCOUTING, STAFF, FINANCES})
    {
      Navigation::open(view.get(), section);
      driver->frames(3);
    }
    Navigation::open(view.get(), HOME);
    driver->frames(2);
  }
  EXPECT_STREQ(LOC("NAV_HOME"), "Home");
  EXPECT_EQ(driver->imgui_errors, 0);
}

/** Garbage in the settings file never stops the game from starting. */
TEST_F(Adversarial, CorruptSettingsRecovery)
{
  const std::vector<std::string> files = {
      "{not json",
      "",
      "[]",
      R"({"language": "klingon", "ui_scale": "big"})",
      R"({"resolution": [1000000000, 1000000000], "fps_limit": -5})",
      R"({"resolution": [800], "theme_preset": 99, "ui_scale": 1e30})",
      R"({"language": 250, "accent_rgb": -1, "fullscreen": "yes"})",
      std::string("\0\x{ff}\x{fe} garbage", 12)};
  for (const std::string& content : files)
  {
    SCOPED_TRACE(content);
    SettingsManager::instance()->get() = Settings{};
    {
      std::ofstream out(RuntimePaths::settingsPath(), std::ios::binary);
      out << content;
    }
    SettingsManager::instance()->load();
    const Settings& settings = SettingsManager::instance()->get();
    EXPECT_GE(settings.resolution_width, 640);
    EXPECT_GE(settings.resolution_height, 480);
    EXPECT_LE(settings.resolution_width, 16384)
        << "absurd resolution accepted from the settings file";
    EXPECT_LE(settings.resolution_height, 16384);
    EXPECT_GE(settings.fps_limit, 15);
    EXPECT_LE(settings.theme_preset,
              static_cast<int>(Theme::Preset::COUNT) - 1);
    EXPECT_TRUE(settings.ui_scale == 0.0f ||
                (settings.ui_scale >= 0.75f && settings.ui_scale <= 2.0f));
    EXPECT_STRNE(LOC("NAV_HOME"), "NAV_HOME")
        << "no language loaded after a corrupt settings file";
    LanguageManager::instance().loadLanguage(Language::EN);
  }
  // And the GUI starts on top of the last one.
  {
    std::ofstream out(RuntimePaths::settingsPath());
    out << R"({"resolution": [1000000000, 1000000000], "ui_scale": 7})";
  }
  SettingsManager::instance()->get() = Settings{};
  GUIView corrupted(*controller);
  ASSERT_TRUE(Bridge::initialize(corrupted));
  ImGui::GetIO().IniFilename = nullptr;
  Driver local(corrupted, *controller);
  local.frames(3);
  int width = 0;
  int height = 0;
  SDL_GetWindowSize(corrupted.getWindow(), &width, &height);
  EXPECT_LE(width, 16384);
  EXPECT_LE(height, 16384);
  EXPECT_EQ(local.imgui_errors, 0);
}

// ---- HiDPI
// -------------------------------------------------------------------

/**
 * On a scale-2 output (Hyprland/Wayland, Retina) the framebuffer has twice
 * the window's pixels. The UI drawn with DisplayFramebufferScale 2 must fill
 * the whole framebuffer, not its top-left quarter. The dummy driver has no
 * pixel density, so a 2x render target stands in for the framebuffer.
 */
TEST_F(Adversarial, HiDpiUiFillsTheFramebuffer)
{
  openGui();
  SDL_Renderer* renderer = Bridge::renderer(*view);
  int width = 0;
  int height = 0;
  SDL_GetWindowSize(view->getWindow(), &width, &height);
  SDL_Texture* target =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                        SDL_TEXTUREACCESS_TARGET, width * 2, height * 2);
  ASSERT_NE(target, nullptr) << SDL_GetError();
  const auto drawAt = [&](float density)
  {
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplayFramebufferScale = ImVec2(density, density);
    io.DeltaTime = FRAME_SECONDS;
    ImGui::NewFrame();
    driver->active()->render();
    ImGui::Render();
    SDL_SetRenderTarget(renderer, target);
    SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255);
    SDL_RenderClear(renderer);
    {
      const ScopedRenderScale scale(renderer, io.DisplayFramebufferScale);
      ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    }
    SDL_Surface* pixels = SDL_RenderReadPixels(renderer, nullptr);
    SDL_SetRenderTarget(renderer, nullptr);
    return pixels;
  };
  const auto clearShare =
      [](SDL_Surface* surface, int x0, int y0, int x1, int y1)
  {
    int clear = 0;
    int total = 0;
    for (int y = y0; y < y1; y += 7)
      for (int x = x0; x < x1; x += 7)
      {
        Uint8 r = 0;
        Uint8 g = 0;
        Uint8 b = 0;
        Uint8 a = 0;
        SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a);
        if (r == 255 && g == 0 && b == 255) ++clear;
        ++total;
      }
    return total > 0 ? static_cast<double>(clear) / total : 1.0;
  };
  SDL_Surface* frame = drawAt(2.0f);
  ASSERT_NE(frame, nullptr) << SDL_GetError();
  // The shell's full-screen window covers the whole framebuffer: nothing of
  // the magenta clear colour may remain in the bottom-right quarter.
  EXPECT_LT(clearShare(frame, width, height, width * 2, height * 2), 0.02)
      << "HiDPI: the UI does not reach the bottom-right of the framebuffer";
  EXPECT_LT(clearShare(frame, 0, 0, width, height), 0.02);
  SDL_DestroySurface(frame);
  SDL_DestroyTexture(target);
  driver->frames(2);
  EXPECT_EQ(driver->imgui_errors, 0);
}

}  // namespace
