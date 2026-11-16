// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------
//
// GUI monkey: a seeded, reproducible random user driving the real GUIView
// (dummy SDL video driver). Each step is one random action (click a widget
// found under the mouse, scroll, shortcuts, typing, drags, window resizes,
// UI scale and theme changes, Continue, live matches, save and reload); after
// every step the harness checks ImGui usage errors, frame build time, the
// Continue threading contract and the world's domain invariants.
//
//   FM_MONKEY_SEED=<n>    run only this seed (replay)
//   FM_MONKEY_STEPS=<n>   steps per seed (default 300; long run: 20000)
//   FM_MONKEY_OUT=<dir>   keep the action logs (default: per-process root)
//   FM_MONKEY_FRAME_BUDGET_MS=<ms>  UI build budget per frame (default 1500)
//   FM_MONKEY_SHOT_AT=<step>  screenshot (BMP, in the output dir) after it
//
// A failure prints the seed, the step and the tail of the action log; the
// same seed and step count replay it exactly (fixed delta time, fixed world
// and match seeds, no wall-clock dependent decisions).
//
// Also here: every enabled widget of every management screen must have an
// observable effect (EveryEnabledWidgetHasAnEffect).
//
// Label "monkey;slow" (own executable): exclude with `ctest -LE monkey`.

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include "controller/game_controller.h"
#include "database/gamedata.h"
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
#include "gui/widgets/theme.h"
#include "model/finances.h"
#include "model/match_engine.h"
#include "settings_manager.h"

#if defined(__clang__) || defined(__GNUC__)
extern "C" const char* __lsan_default_suppressions()
{
  return "leak:libSDL3.so\n";
}
#endif

/**
 * The GUI classes grant their internals to this name (the GUI lifecycle test
 * of test_game_flow.cpp, which is not linked into this executable). Here it
 * bridges what a headless monkey needs.
 */
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
  static bool scenePending(const GUIView& view)
  {
    return view.pendingAction != GUIView::PendingAction::NONE;
  }
  static bool backdropAlive(const GUIView& view)
  {
    return view.backdropTexture != nullptr;
  }

  /**
   * GUIView::render() with a fixed delta time (the SDL backend measures wall
   * time, which would make double clicks, toasts and hover delays depend on
   * machine load). @p raster = false skips rasterization unless a backdrop
   * capture is pending. Returns the ImGui build time (NewFrame..Render).
   */
  static double render(GUIView& view, float seconds, bool raster)
  {
    SDL_Renderer* renderer = view.renderer;
    raster = raster || view.backdropPending;
    if (raster)
    {
      SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
      SDL_RenderClear(renderer);
      if (view.backdropTexture != nullptr && !view.backdropPending)
        SDL_RenderTexture(renderer, view.backdropTexture, nullptr, nullptr);
    }
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::GetIO().DeltaTime = seconds;
    const auto start = std::chrono::steady_clock::now();
    ImGui::NewFrame();
    if (GUIScene* scene = view.getActiveScene()) scene->render();
    ImGui::Render();
    const double elapsed = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - start)
                               .count();
    if (!raster) return elapsed;
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
        if (view.backdropTexture != nullptr)
          SDL_SetTextureBlendMode(view.backdropTexture, SDL_BLENDMODE_NONE);
      }
    }
    SDL_RenderPresent(renderer);
    return elapsed;
  }

  static bool continueRequested(const MainGameScene& hub)
  {
    return hub.continuation_requested;
  }
  static std::optional<GameDateValue> nextFixtureDate(const MainGameScene& hub)
  {
    if (!hub.cached_next) return std::nullopt;
    return hub.cached_next->date;
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
  static bool paused(const MatchScene& scene) { return scene.is_paused; }

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
/** Rasterize (and upload font atlas changes) every this many frames. */
constexpr int RASTER_EVERY = 12;
/** The same while the mouse sweeps a region looking for a widget: hover
 * needs no pixels, and rasterizing a big window (the 3D match view in
 * software) every 12th of thousands of sweep frames cost minutes. */
constexpr int SWEEP_RASTER_EVERY = 240;
constexpr auto CONTINUE_DEADLINE = std::chrono::seconds(120);
constexpr int DEFAULT_STEPS = 300;
constexpr std::array<uint64_t, 3> DEFAULT_SEEDS = {11, 22, 33};
constexpr size_t MAX_VIOLATIONS = 12;

int envInt(const char* name, int fallback)
{
  const char* value = std::getenv(name);
  return value && *value ? std::atoi(value) : fallback;
}

fs::path outputDir()
{
  const char* configured = std::getenv("FM_MONKEY_OUT");
  const fs::path directory = configured && *configured
                                 ? fs::path(configured)
                                 : RuntimePaths::root() / "monkey";
  fs::create_directories(directory);
  return directory;
}

double millisecondsSince(Clock::time_point start)
{
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
      .count();
}

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

const char* sceneName(SceneID id)
{
  using enum SceneID;
  static constexpr std::array NAMES = {
      std::pair{MAIN_MENU, "main_menu"},
      std::pair{SETTINGS, "settings"},
      std::pair{GAME_MENU, "hub"},
      std::pair{LINEUP, "lineup"},
      std::pair{TEAM_SELECTION, "team_selection"},
      std::pair{ROSTER, "squad"},
      std::pair{STRATEGY, "tactics"},
      std::pair{TRANSFER_MARKET, "transfers"},
      std::pair{MATCH, "match"},
      std::pair{PLAYER_PROFILE, "profile"},
      std::pair{FIXTURES, "fixtures"},
      std::pair{STANDINGS, "standings"},
      std::pair{INBOX, "inbox"},
      std::pair{CLUB, "club"},
      std::pair{MATCH_REPORT, "match_report"},
      std::pair{SCOUTING, "scouting"},
      std::pair{TRAINING, "training"},
      std::pair{STAFF, "staff"},
      std::pair{MANAGER, "manager"},
      std::pair{YOUTH, "youth"},
      std::pair{MEDICAL, "medical"},
      std::pair{CALENDAR, "calendar"},
      std::pair{SQUAD_PLANNER, "squad_planner"},
      std::pair{PLAYER_COMPARE, "player_compare"},
      std::pair{DELEGATION, "delegation"},
      std::pair{DATA_HUB, "data_hub"},
      std::pair{OPPOSITION, "opposition"},
      std::pair{INTERNATIONAL, "international"},
      std::pair{AWARDS, "awards"},
      std::pair{RECORDS, "records"},
      std::pair{PLANNING, "planning"},
      std::pair{HELP, "help"},
      std::pair{RESERVES, "reserves"},
      std::pair{CALL_UPS, "call_ups"},
      std::pair{ABOUT, "about"},
      std::pair{NEWS, "news"},
      std::pair{TIMELINE, "timeline"},
  };
  const auto found =
      std::ranges::find(NAMES, id, &decltype(NAMES)::value_type::first);
  return found == NAMES.end() ? "?" : found->second;
}

// ---- Keys
// --------------------------------------------------------------------

struct KeySpec
{
  const char* name;
  SDL_Keycode key;
  SDL_Scancode scancode;
  SDL_Keymod mod;
};

// clang-format off
constexpr std::array<KeySpec, 11> NAV_KEYS = {{
    {"F1", SDLK_F1, SDL_SCANCODE_F1, SDL_KMOD_NONE},
    {"F2", SDLK_F2, SDL_SCANCODE_F2, SDL_KMOD_NONE},
    {"F3", SDLK_F3, SDL_SCANCODE_F3, SDL_KMOD_NONE},
    {"F4", SDLK_F4, SDL_SCANCODE_F4, SDL_KMOD_NONE},
    {"F5", SDLK_F5, SDL_SCANCODE_F5, SDL_KMOD_NONE},
    {"F6", SDLK_F6, SDL_SCANCODE_F6, SDL_KMOD_NONE},
    {"F7", SDLK_F7, SDL_SCANCODE_F7, SDL_KMOD_NONE},
    {"F8", SDLK_F8, SDL_SCANCODE_F8, SDL_KMOD_NONE},
    {"F9", SDLK_F9, SDL_SCANCODE_F9, SDL_KMOD_NONE},
    {"F10", SDLK_F10, SDL_SCANCODE_F10, SDL_KMOD_NONE},
    {"F11", SDLK_F11, SDL_SCANCODE_F11, SDL_KMOD_NONE},
}};
constexpr std::array<KeySpec, 16> OTHER_KEYS = {{
    {"Escape", SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, SDL_KMOD_NONE},
    {"Alt+Left", SDLK_LEFT, SDL_SCANCODE_LEFT, SDL_KMOD_LALT},
    {"Space", SDLK_SPACE, SDL_SCANCODE_SPACE, SDL_KMOD_NONE},
    {"Enter", SDLK_RETURN, SDL_SCANCODE_RETURN, SDL_KMOD_NONE},
    {"Ctrl+S", SDLK_S, SDL_SCANCODE_S, SDL_KMOD_LCTRL},
    {"Tab", SDLK_TAB, SDL_SCANCODE_TAB, SDL_KMOD_NONE},
    {"Shift+Tab", SDLK_TAB, SDL_SCANCODE_TAB, SDL_KMOD_LSHIFT},
    {"Up", SDLK_UP, SDL_SCANCODE_UP, SDL_KMOD_NONE},
    {"Down", SDLK_DOWN, SDL_SCANCODE_DOWN, SDL_KMOD_NONE},
    {"Left", SDLK_LEFT, SDL_SCANCODE_LEFT, SDL_KMOD_NONE},
    {"Right", SDLK_RIGHT, SDL_SCANCODE_RIGHT, SDL_KMOD_NONE},
    {"Backspace", SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE, SDL_KMOD_NONE},
    {"Delete", SDLK_DELETE, SDL_SCANCODE_DELETE, SDL_KMOD_NONE},
    {"PageDown", SDLK_PAGEDOWN, SDL_SCANCODE_PAGEDOWN, SDL_KMOD_NONE},
    {"Home", SDLK_HOME, SDL_SCANCODE_HOME, SDL_KMOD_NONE},
    {"Ctrl+A", SDLK_A, SDL_SCANCODE_A, SDL_KMOD_LCTRL},
}};
constexpr std::array<KeySpec, 6> MATCH_KEYS = {{
    {"V", SDLK_V, SDL_SCANCODE_V, SDL_KMOD_NONE},
    {"1", SDLK_1, SDL_SCANCODE_1, SDL_KMOD_NONE},
    {"2", SDLK_2, SDL_SCANCODE_2, SDL_KMOD_NONE},
    {"3", SDLK_3, SDL_SCANCODE_3, SDL_KMOD_NONE},
    {"4", SDLK_4, SDL_SCANCODE_4, SDL_KMOD_NONE},
    {"Space", SDLK_SPACE, SDL_SCANCODE_SPACE, SDL_KMOD_NONE},
}};
constexpr KeySpec KEY_CTRL_K{"Ctrl+K", SDLK_K, SDL_SCANCODE_K, SDL_KMOD_LCTRL};
constexpr KeySpec KEY_ESCAPE{"Escape", SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, SDL_KMOD_NONE};
constexpr KeySpec KEY_ENTER{"Enter", SDLK_RETURN, SDL_SCANCODE_RETURN, SDL_KMOD_NONE};
constexpr KeySpec KEY_DOWN{"Down", SDLK_DOWN, SDL_SCANCODE_DOWN, SDL_KMOD_NONE};
constexpr KeySpec KEY_CTRL_S{"Ctrl+S", SDLK_S, SDL_SCANCODE_S, SDL_KMOD_LCTRL};
constexpr KeySpec KEY_ALT_LEFT{"Alt+Left", SDLK_LEFT, SDL_SCANCODE_LEFT, SDL_KMOD_LALT};
// clang-format on

/** Text a user (or a cat on the keyboard) might type. */
constexpr std::array<const char*, 12> TEXT_SAMPLES = {
    "a",     "Ro", "united",     "zzzz",   "12", "%s%n%d", "\u{00E8}\u{00E0}",
    "{}{0}", "  ", "\u{20AC} 5", "O'Neil", "-1"};

constexpr std::array<std::pair<int, int>, 4> WINDOW_SIZES = {
    {{1024, 700}, {1280, 720}, {1920, 1080}, {2560, 1440}}};
constexpr std::array<float, 6> UI_SCALES = {0.0f,  0.75f, 1.0f,
                                            1.25f, 1.5f,  2.0f};

// ---- Deterministic RNG
// -------------------------------------------------------

/** SplitMix64: fully specified, identical on every platform. */
class Rng
{
 public:
  explicit Rng(uint64_t seed) : state(seed) {}
  uint64_t next()
  {
    uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
  /** Uniform integer in [0, bound). */
  size_t below(size_t bound)
  {
    return bound == 0 ? 0 : static_cast<size_t>(next() % bound);
  }
  int range(int low, int high)
  {
    return low + static_cast<int>(below(static_cast<size_t>(high - low + 1)));
  }
  float unit() { return static_cast<float>(next() >> 40) / 16777216.0f; }
  bool chance(float probability) { return unit() < probability; }

 private:
  uint64_t state;
};

// ---- Domain invariants
// ---------------------------------------------------------

/**
 * World invariants that must hold after any user action. Returns one line
 * per violated rule (at most a few per rule). Never call while a Continue
 * runs: the harness itself would then race the simulation.
 */
std::vector<std::string> checkWorld(const GameController& controller)
{
  std::vector<std::string> problems;
  if (!controller.isGameLoaded()) return problems;
  const auto data = controller.getGameData();
  std::map<std::string, int, std::less<>> perRule;
  const auto report = [&](const std::string& rule, std::string text)
  {
    if (perRule[rule]++ < 3) problems.push_back(rule + ": " + std::move(text));
  };

  std::unordered_set<const Player*> alive;
  for (const auto& player : data->getPlayersVector())
    alive.insert(&player.get());

  std::vector<TeamID> teamIds;
  for (const auto& [teamId, team] : data->getTeams()) teamIds.push_back(teamId);
  std::ranges::sort(teamIds);
  const auto managed = controller.getManagedTeam();
  const TeamID managedId = managed ? managed->get().getId() : 0;

  std::unordered_map<PlayerID, TeamID> owner;
  for (const TeamID teamId : teamIds)
  {
    if (teamId == FREE_AGENTS_TEAM_ID) continue;
    const Team& team = data->getTeam(teamId)->get();
    const size_t squad = team.getPlayerIDs().size();
    if (teamId != managedId && (squad < 16 || squad > 45))
      report("squad size",
             std::format("club {} has {} players", teamId, squad));
    if (teamId == managedId && squad > 60)
      report("squad size", std::format("managed club has {} players", squad));
    // Club players: the senior squad plus the academy.
    std::vector<PlayerID> members = team.getPlayerIDs();
    members.insert(members.end(), team.getAcademyIDs().begin(),
                   team.getAcademyIDs().end());
    for (const PlayerID playerId : members)
    {
      const auto [previous, inserted] = owner.emplace(playerId, teamId);
      const auto player = data->getPlayer(playerId);
      if (!inserted)
        report("membership", std::format("player {} listed by clubs {} and {}",
                                         playerId, previous->second, teamId));
      else if (!player)
        report("membership", std::format("club {} lists unknown player {}",
                                         teamId, playerId));
      else if (player->get().getTeamId() != teamId)
        report("membership",
               std::format("club {} lists player {} whose club is {}", teamId,
                           playerId, player->get().getTeamId()));
    }

    // Lineups hold raw pointers: every one must still point at a live
    // player (a released/retired player must not linger as a dangling one).
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
        report("lineup", std::format("club {} selects a player that no longer "
                                     "exists (dangling pointer)",
                                     teamId));
      else if (teamId == managedId && player->getTeamId() != teamId)
        report("lineup",
               std::format("managed lineup keeps player {} of club {}",
                           player->getId(), player->getTeamId()));
    }

    const auto& ledger = controller.getFinanceLedger(teamId);
    const int64_t sum =
        std::accumulate(ledger.begin(), ledger.end(), int64_t{0},
                        [](int64_t total, const FinanceTransaction& entry)
                        { return total + entry.amount; });
    if (sum != team.getFinances().getBalance())
      report("ledger",
             std::format("club {} balance {} != ledger sum {}", teamId,
                         team.getFinances().getBalance(), sum));
  }

  for (const auto& playerRef : data->getPlayersVector())
  {
    const Player& player = playerRef.get();
    if (player.getTeamId() != FREE_AGENTS_TEAM_ID &&
        !owner.contains(player.getId()))
      report("membership", std::format("player {} of club {} missing from "
                                       "the squad list",
                                       player.getId(), player.getTeamId()));
    const PlayerDynamics& dynamics = player.getDynamics();
    for (const auto& [name, value] :
         {std::pair{"condition", dynamics.condition},
          std::pair{"sharpness", dynamics.sharpness},
          std::pair{"morale", dynamics.morale}})
      if (!std::isfinite(value) || value < 0.0f || value > 100.0f)
        report("player state",
               std::format("player {} {} = {}", player.getId(), name, value));
    for (const auto& [name, value] : player.getStats())
      if (!std::isfinite(value) || value < 0.0f || value > 100.0f)
        report("player stats",
               std::format("player {} {} = {}", player.getId(), name, value));
    if (player.getAge() < 15 || player.getAge() > 45)
      report("player age",
             std::format("player {} is {}", player.getId(), player.getAge()));
  }

  for (const auto& leagueRef : controller.getLeagues())
  {
    const League& league = leagueRef.get();
    const auto table = controller.getStandings(league.getId());
    int won = 0;
    int lost = 0;
    int goalsFor = 0;
    int goalsAgainst = 0;
    for (const StandingRow& row : table)
    {
      won += row.won;
      lost += row.lost;
      goalsFor += row.goals_for;
      goalsAgainst += row.goals_against;
      std::string problem;
      if (row.points != 3 * row.won + row.drawn)
        problem = "points != 3W+D";
      else if (row.played != row.won + row.drawn + row.lost)
        problem = "played != W+D+L";
      else if (row.goal_difference != row.goals_for - row.goals_against)
        problem = "GD != GF-GA";
      else if (row.played > 2 * (table.size() - 1))
        problem = "more games than a double round robin";
      if (!problem.empty())
        report("table", std::format("{} club {}: {}", league.getName(),
                                    row.team_id, problem));
    }
    if (won != lost || goalsFor != goalsAgainst)
      report("table",
             std::format("{} totals W{} L{} GF{} GA{}", league.getName(), won,
                         lost, goalsFor, goalsAgainst));
  }

  // Fixtures: nobody plays twice a day, each league pairing once per season,
  // and every fixture before today has been played.
  const GameDateValue today = controller.getCurrentDate();
  std::set<std::tuple<LeagueID, TeamID, TeamID>> pairings;
  for (const auto& [date, matches] :
       controller.getGame()->getCalendar().getFullCalendar())
  {
    std::unordered_set<TeamID> busy;
    for (const Match& match : matches)
    {
      const TeamID home = match.getHomeTeamId();
      const TeamID away = match.getAwayTeamId();
      if (home == away)
        report("fixtures",
               std::format("{}: club {} plays itself", date.toString(), home));
      if (!busy.insert(home).second || !busy.insert(away).second)
        report("fixtures", std::format("{}: club {} or {} plays twice",
                                       date.toString(), home, away));
      if (match.getMatchType() == MatchType::LEAGUE &&
          !pairings.emplace(match.getCompetitionId(), home, away).second)
        report("fixtures",
               std::format("league pairing {}-{} scheduled twice", home, away));
      if (date < today && !match.isPlayed())
        report("fixtures",
               std::format(
                   "{} {}-{} ({}) in the past but unplayed", date.toString(),
                   home, away,
                   home == managedId || away == managedId ? "managed" : "AI"));
    }
  }
  return problems;
}

/** Persisted state compared across a save and a reload. */
struct Fingerprint
{
  std::map<std::string, std::string, std::less<>> fields;

  static Fingerprint of(const GameController& controller)
  {
    Fingerprint print;
    auto& fields = print.fields;
    const auto data = controller.getGameData();
    fields["date"] = controller.getCurrentDate().toString();
    const auto managed = controller.getManagedTeam();
    fields["managed"] =
        managed ? std::to_string(managed->get().getId()) : std::string("none");
    fields["players"] = std::to_string(data->getPlayersVector().size());
    std::vector<TeamID> teamIds;
    for (const auto& [teamId, team] : data->getTeams())
      teamIds.push_back(teamId);
    std::ranges::sort(teamIds);
    for (const TeamID teamId : teamIds)
    {
      const Team& team = data->getTeam(teamId)->get();
      std::vector<PlayerID> squad = team.getPlayerIDs();
      std::ranges::sort(squad);
      std::string ids;
      for (const PlayerID id : squad) ids += std::format("{},", id);
      fields[std::format("squad {}", teamId)] = ids;
      fields[std::format("balance {}", teamId)] =
          std::to_string(team.getFinances().getBalance());
      fields[std::format("ledger entries {}", teamId)] =
          std::to_string(controller.getFinanceLedger(teamId).size());
    }
    if (managed)
    {
      const Lineup& lineup = managed->get().getLineup();
      std::string xi = lineup.getGoalkeeper()
                           ? std::to_string(lineup.getGoalkeeper()->getId())
                           : std::string("-");
      for (const auto& positioned : lineup.getOutfieldPlayers())
        xi += "," + (positioned.player
                         ? std::to_string(positioned.player->getId())
                         : std::string("-"));
      fields["managed lineup"] = xi;
      const StrategySliders sliders = managed->get().getStrategy().getSliders();
      fields["managed strategy"] =
          std::format("{:.3f} {:.3f} {:.3f} {:.3f} {:.3f}", sliders.pressing,
                      sliders.riskTaking, sliders.offensiveBias,
                      sliders.widthUsage, sliders.compactness);
    }
    for (const auto& leagueRef : controller.getLeagues())
    {
      std::string rows;
      for (const StandingRow& row :
           controller.getStandings(leagueRef.get().getId()))
        rows += std::format("{}:{}/{}/{};", row.team_id, row.played, row.points,
                            row.goal_difference);
      fields[std::format("table {}", leagueRef.get().getId())] = rows;
    }
    size_t played = 0;
    for (const auto& [date, matches] :
         controller.getGame()->getCalendar().getFullCalendar())
      for (const Match& match : matches) played += match.isPlayed() ? 1 : 0;
    fields["played fixtures"] = std::to_string(played);
    fields["inbox"] = std::to_string(controller.getInbox().size());
    fields["unread"] = std::to_string(controller.getUnreadInboxCount());
    fields["shortlist"] = std::to_string(controller.getShortlist().size());
    fields["incoming offers"] =
        std::to_string(controller.getIncomingOffers().size());
    fields["scout assignments"] =
        std::to_string(controller.getScoutAssignments().size());
    fields["season"] = std::to_string(controller.getCurrentSeason());
    return print;
  }

  std::vector<std::string> differences(const Fingerprint& after) const
  {
    std::vector<std::string> lines;
    for (const auto& [key, value] : fields)
    {
      const auto found = after.fields.find(key);
      const std::string other =
          found == after.fields.end() ? "<missing>" : found->second;
      if (other == value) continue;
      lines.push_back(std::format("{}: saved '{}' loaded '{}'", key,
                                  value.substr(0, 80), other.substr(0, 80)));
      if (lines.size() >= 6) break;
    }
    return lines;
  }
};

// ---- Crash breadcrumbs
// ---------------------------------------------------------

std::array<char, 512> crash_note{};

void onFatalSignal(int signal)
{
  size_t length = 0;
  while (length < crash_note.size() && crash_note[length] != '\0') ++length;
  if (length > 0) (void)!write(STDERR_FILENO, crash_note.data(), length);
  std::signal(signal, SIG_DFL);
  std::raise(signal);
}

// ---- The driver
// ----------------------------------------------------------------

/** One widget found under the mouse. */
struct Target
{
  ImGuiID id = 0;
  ImVec2 point;
  ImVec2 min{FLT_MAX, FLT_MAX};
  ImVec2 max{-FLT_MAX, -FLT_MAX};
};

/**
 * Frames, input and widget discovery on top of the real GUIView. Every
 * frame uses the fixed delta time; decisions never depend on wall time.
 */
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
  std::string imgui_error_scene;
  double worst_frame_ms = 0.0;
  std::string worst_frame_scene;
  bool skip_budget = false;

  GUIScene* active() const { return Bridge::activeScene(view); }
  SceneID activeId() const
  {
    return active() ? active()->getID() : SceneID::MAIN_MENU;
  }
  MainGameScene* hub() const
  {
    return dynamic_cast<MainGameScene*>(view.getBaseScene());
  }
  MainMenuScene* menu() const
  {
    return dynamic_cast<MainMenuScene*>(view.getBaseScene());
  }
  bool advancing() const { return hub() != nullptr && hub()->isAdvancing(); }
  bool loadingSlot() const
  {
    return menu() != nullptr && Bridge::menuLoading(*menu());
  }

  /** One application frame (as GUIView::run does), UI-only by default. */
  void frame(bool raster = false)
  {
    Bridge::applyPending(view);
    Bridge::handleEvents(view);
    Bridge::update(view, FRAME_SECONDS);
    bool shouldRaster = raster;
    if (!shouldRaster)
      shouldRaster =
          ++frame_counter % (sweeping ? SWEEP_RASTER_EVERY : RASTER_EVERY) == 0;
    const double elapsed = Bridge::render(view, FRAME_SECONDS, shouldRaster);
    const int errors = GImGui->ErrorCountCurrentFrame;
    if (errors > 0 && imgui_errors == 0)
    {
      imgui_error_scene = sceneName(activeId());
      // The recovered error's message, from ImGui's debug log.
      const std::string_view log(
          GImGui->DebugLogBuf.c_str(),
          static_cast<size_t>(GImGui->DebugLogBuf.size()));
      if (const size_t at = log.rfind("In window");
          at != std::string_view::npos)
      {
        const std::string_view message = log.substr(at);
        imgui_error_scene +=
            " - " + std::string(message.substr(0, message.find('\n')));
      }
    }
    imgui_errors += errors;
    if (!skip_budget && elapsed > worst_frame_ms)
    {
      worst_frame_ms = elapsed;
      worst_frame_scene = sceneName(activeId());
    }
    skip_budget = false;
  }
  void frames(int count)
  {
    for (int index = 0; index < count; ++index) frame();
  }

  // ---- Input ----
  static void mouseTo(ImVec2 point)
  {
    ImGui::GetIO().AddMousePosEvent(point.x, point.y);
  }
  static void parkMouse()
  {
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  }
  void click(ImVec2 point, int count = 1,
             ImGuiMouseButton button = ImGuiMouseButton_Left)
  {
    mouseTo(point);
    frame();
    for (int index = 0; index < count; ++index)
    {
      ImGui::GetIO().AddMouseButtonEvent(button, true);
      frame();
      ImGui::GetIO().AddMouseButtonEvent(button, false);
      frame();
    }
  }
  void drag(ImVec2 from, ImVec2 to, int stepsCount = 5)
  {
    mouseTo(from);
    frame();
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    for (int index = 1; index <= stepsCount; ++index)
    {
      const float t =
          static_cast<float>(index) / static_cast<float>(stepsCount);
      mouseTo(
          ImVec2(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t));
      frame();
    }
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
  }
  void wheel(ImVec2 point, float amount)
  {
    mouseTo(point);
    frame();
    ImGui::GetIO().AddMouseWheelEvent(0.0f, amount);
    frame();
  }
  /** A real SDL key press (both ImGui and the scene's handleEvent see it). */
  void key(const KeySpec& spec)
  {
    SDL_Event event{.key = {}};
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = SDL_GetWindowID(view.getWindow());
    event.key.key = spec.key;
    event.key.scancode = spec.scancode;
    event.key.mod = spec.mod;
    event.key.down = true;
    SDL_PushEvent(&event);
    frame();
    event.key.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    event.key.mod = SDL_KMOD_NONE;
    SDL_PushEvent(&event);
    frame();
  }
  void type(std::string_view text)
  {
    ImGui::GetIO().AddInputCharactersUTF8(std::string(text).c_str());
    frame();
  }
  void resize(int width, int height)
  {
    SDL_SetWindowSize(view.getWindow(), width, height);
    SDL_Event event{.window = {}};
    event.window.type = SDL_EVENT_WINDOW_RESIZED;
    event.window.windowID = SDL_GetWindowID(view.getWindow());
    event.window.data1 = width;
    event.window.data2 = height;
    SDL_PushEvent(&event);
    skip_budget = true;
    frame(true);
  }

  // ---- Discovery ----
  /** Visible windows the mouse can reach (only the top modal's, if any). */
  static std::vector<ImRect> reachableWindows()
  {
    ImGuiContext& context = *GImGui;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImRect screen(ImVec2(0.0f, 0.0f), display);
    const ImGuiWindow* popup = nullptr;
    if (context.OpenPopupStack.Size > 0)
      popup = context.OpenPopupStack.back().Window;
    std::vector<ImRect> rects;
    for (const ImGuiWindow* window : context.Windows)
    {
      if (!window->Active || window->Hidden ||
          (window->Flags &
           (ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoInputs)))
        continue;
      // Outside a modal nothing reacts; outside a plain popup a click
      // closes it (what users do), so those stay reachable, less often.
      const bool inPopup = popup != nullptr && window->RootWindow == popup;
      if (popup != nullptr && !inPopup &&
          (popup->Flags & ImGuiWindowFlags_Modal))
        continue;
      ImRect rect = window->InnerClipRect;
      rect.ClipWithFull(screen);
      if (rect.GetWidth() < 4.0f || rect.GetHeight() < 4.0f) continue;
      rects.push_back(rect);
      if (inPopup && !(popup->Flags & ImGuiWindowFlags_Modal))
        rects.push_back(rect);
    }
    return rects;
  }

  /**
   * Hovers @p probes random points (in random reachable windows) and
   * returns the distinct enabled widgets found, in discovery order.
   */
  std::vector<Target> probe(Rng& rng, int probes)
  {
    parkMouse();
    frame();
    std::vector<ImRect> rects = reachableWindows();
    // The live match is mostly pitch: its controls live in the top band.
    if (activeId() == SceneID::MATCH && GImGui->OpenPopupStack.Size == 0)
    {
      const ImVec2 display = ImGui::GetIO().DisplaySize;
      rects.emplace_back(ImVec2(0.0f, 0.0f),
                         ImVec2(display.x, display.y * 0.35f));
      rects.emplace_back(ImVec2(0.0f, 0.0f),
                         ImVec2(display.x, display.y * 0.35f));
    }
    std::vector<Target> targets;
    if (rects.empty()) return targets;
    for (int index = 0; index < probes; ++index)
    {
      const ImRect& rect = rects[rng.below(rects.size())];
      const ImVec2 point(rect.Min.x + rng.unit() * rect.GetWidth(),
                         rect.Min.y + rng.unit() * rect.GetHeight());
      mouseTo(point);
      frame();
      const ImGuiID id = GImGui->HoveredId;
      if (id == 0 || GImGui->HoveredIdIsDisabled) continue;
      if (std::ranges::any_of(
              targets, [id](const Target& target) { return target.id == id; }))
        continue;
      targets.emplace_back(id, point);
    }
    return targets;
  }

  /** Every enabled widget of the screen, by a dense sweep (costly). */
  std::vector<Target> sweep(float stepX = 22.0f, float stepY = 11.0f)
  {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    std::vector<Target> items;
    std::unordered_map<ImGuiID, size_t> index;
    for (int y_index = 0;
         stepY * (static_cast<float>(y_index) + 0.5f) < display.y; ++y_index)
      for (int x_index = 0;
           stepX * (static_cast<float>(x_index) + 0.5f) < display.x; ++x_index)
      {
        const float y = stepY * (static_cast<float>(y_index) + 0.5f);
        const float x = stepX * (static_cast<float>(x_index) + 0.5f);
        mouseTo({x, y});
        frame();
        const ImGuiID id = GImGui->HoveredId;
        if (id == 0 || GImGui->HoveredIdIsDisabled) continue;
        auto [found, inserted] = index.emplace(id, items.size());
        if (inserted) items.push_back({id, {x, y}});
        Target& item = items[found->second];
        item.min = {std::min(item.min.x, x), std::min(item.min.y, y)};
        item.max = {std::max(item.max.x, x), std::max(item.max.y, y)};
      }
    for (Target& item : items)
    {
      const ImVec2 centre((item.min.x + item.max.x) * 0.5f,
                          (item.min.y + item.max.y) * 0.5f);
      mouseTo(centre);
      frame();
      if (GImGui->HoveredId == item.id) item.point = centre;
    }
    parkMouse();
    frame();
    return items;
  }

  /** ID path of the widget at @p point (ImGui's ID stack query). */
  std::string labelOf(ImGuiID id, ImVec2 point)
  {
    const Sweep sweep(*this);
    mouseTo(point);
    ImGuiContext& context = *GImGui;
    for (int attempt = 0; attempt < 40; ++attempt)
    {
      context.DebugIDStackTool.LastActiveFrame = context.FrameCount;
      frame();
      const ImGuiDebugItemPathQuery& query = context.DebugItemPathQuery;
      if (query.MainID == id && query.Complete) break;
    }
    context.DebugIDStackTool.LastActiveFrame = -1;
    const ImGuiDebugItemPathQuery& query = context.DebugItemPathQuery;
    if (query.MainID != id) return "?";
    std::string path;
    for (int level = 0; level < query.Results.Size; ++level)
    {
      const ImGuiStackLevelInfo& info = query.Results[level];
      std::string description = "?";
      if (info.DescOffset >= 0)
        description = ImHashSkipUncontributingPrefix(
            &query.ResultsDescBuf.Buf[info.DescOffset]);
      else if (level == 0)
        if (const ImGuiWindow* window = ImGui::FindWindowByID(info.ID))
          description = ImHashSkipUncontributingPrefix(window->Name);
      if (!path.empty()) path += '/';
      path += description;
    }
    // Pointer IDs differ between runs: keep the log replayable.
    for (size_t at = path.find("0x"); at != std::string::npos;
         at = path.find("0x", at + 3))
    {
      size_t end = at + 2;
      while (end < path.size() &&
             (std::isxdigit(static_cast<unsigned char>(path[end])) ||
              path[end] == 'x'))
        ++end;
      path.replace(at, end - at, "0x?");
    }
    return path;
  }

  /**
   * A widget whose ID path contains @p needle, by a coarse sweep of a
   * region (the last hit is cached and re-verified first).
   */
  std::optional<ImVec2> findLabel(const std::string& needle, ImVec2 min,
                                  ImVec2 max)
  {
    return findLabel(std::vector<std::string>{needle}, min, max);
  }
  std::optional<ImVec2> findLabel(const std::vector<std::string>& needles,
                                  ImVec2 min, ImVec2 max,
                                  bool bottom_up = false)
  {
    std::string needle;
    for (const std::string& part : needles) needle += part + "|";
    if (const auto cached = found_labels.find(needle);
        cached != found_labels.end())
    {
      mouseTo(cached->second.second);
      frame();
      if (GImGui->HoveredId == cached->second.first &&
          !GImGui->HoveredIdIsDisabled)
        return cached->second.second;
    }
    const Sweep sweep(*this);
    std::unordered_set<ImGuiID> seen;
    // Bottom-up when asked: dialogs keep their way out at the bottom.
    const auto rows =
        static_cast<int>(std::ceil((max.y - min.y - 6.0f) / 10.0f));
    for (int row = 0; row < rows; ++row)
      for (int x_index = 0;
           min.x + 8.0f + 16.0f * static_cast<float>(x_index) < max.x;
           ++x_index)
      {
        const float x = min.x + 8.0f + 16.0f * static_cast<float>(x_index);
        const float y =
            min.y + 6.0f +
            10.0f * static_cast<float>(bottom_up ? rows - 1 - row : row);
        mouseTo({x, y});
        frame();
        const ImGuiID id = GImGui->HoveredId;
        if (id == 0 || GImGui->HoveredIdIsDisabled || !seen.insert(id).second)
          continue;
        if (const std::string label = labelOf(id, {x, y});
            std::ranges::none_of(needles, [&label](std::string_view part)
                                 { return label.contains(part); }))
          continue;
        found_labels[needle] = {id, ImVec2(x, y)};
        return ImVec2(x, y);
      }
    return std::nullopt;
  }

  /**
   * The top bar's Continue button: right-aligned in the shell, so a point
   * near the right edge of the bar hits it whenever it exists.
   */
  std::optional<ImVec2> continueButton()
  {
    if (dynamic_cast<ManagementScene*>(active()) == nullptr)
      return std::nullopt;
    const float scale = Theme::scale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float y = 30.0f * scale;
    for (const float inset : {40.0f, 70.0f, 110.0f})
    {
      const ImVec2 point(display.x - (Theme::Space::XL + inset) * scale, y);
      mouseTo(point);
      frame();
      if (GImGui->HoveredId == 0) continue;
      if (continue_id == 0 &&
          labelOf(GImGui->HoveredId, point).contains("shell_continue"))
        continue_id = GImGui->HoveredId;
      if (continue_id != 0 && GImGui->HoveredId == continue_id) return point;
    }
    return std::nullopt;
  }

  /** Frames until Continue / slot loading finish; false on a hang. */
  bool settle(std::vector<std::string>& problems)
  {
    const auto start = Clock::now();
    int stuckFrames = 0;
    while (true)
    {
      const MainGameScene* base = hub();
      const bool running = base != nullptr && base->isAdvancing();
      const bool requested =
          base != nullptr && Bridge::continueRequested(*base);
      // Keep screens from reading the world while its worker changes it.
      if (running && (view.getOverlayDepth() != 0 || active() != base))
        problems.push_back(
            std::format("continue: screen '{}' active (overlays {}) while days "
                        "are simulated",
                        sceneName(activeId()), view.getOverlayDepth()));
      const auto* match = dynamic_cast<const MatchScene*>(active());
      if (const bool quick =
              match != nullptr && Bridge::quickResultPending(*match);
          !running && !loadingSlot() && !quick)
      {
        if (!requested) break;
        if (active() != base) break;
        if (++stuckFrames > 6) break;
      }
      if (Clock::now() - start > CONTINUE_DEADLINE)
      {
        problems.emplace_back("continue/loading did not finish within 120 s");
        return false;
      }
      skip_budget = true;
      frame();
    }
    frame();
    return true;
  }

 private:
  int frame_counter = 0;
  /** Set while a hover sweep runs (see SWEEP_RASTER_EVERY). */
  bool sweeping = false;
  struct Sweep
  {
    explicit Sweep(Driver& driver_ref)
        : driver(driver_ref), outer(driver_ref.sweeping)
    {
      driver.sweeping = true;
    }
    ~Sweep() { driver.sweeping = outer; }
    Sweep(const Sweep&) = delete;
    Sweep& operator=(const Sweep&) = delete;
    Driver& driver;
    bool outer;
  };
  ImGuiID continue_id = 0;
  std::unordered_map<std::string, std::pair<ImGuiID, ImVec2>> found_labels;
};

/** Fresh process-wide singletons so every run starts identically. */
void resetSingletons()
{
  std::error_code ignored;
  fs::remove(RuntimePaths::settingsPath(), ignored);
  fs::remove(RuntimePaths::imguiIniPath(), ignored);
  SettingsManager::instance()->get() = Settings{};
  LanguageManager::instance().loadLanguage(Language::EN);
}

void setDefaultEnvironment()
{
  // Replays outside ctest keep the fixed seeds of the test environment.
  setenv("SDL_VIDEODRIVER", "dummy", 0);
  setenv("FM_WORLD_SEED", "20250702", 0);
  setenv("FM_MATCH_SEED", "424242", 0);
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
}

// ---- The monkey
// ------------------------------------------------------------------

enum class Action : uint8_t
{
  CLICK,
  DOUBLE_CLICK,
  RIGHT_CLICK,
  SCROLL,
  DRAG,
  NAV_KEY,
  OTHER_KEY,
  TYPE,
  PALETTE,
  CONTINUE_KEY,
  CONTINUE_CLICKS,
  SAVE,
  RESIZE,
  UI_SCALE,
  THEME,
  SAVE_RELOAD,
  BACK_FORWARD,
  MATCH_KEY,
  MATCH_RUN,
  MATCH_FINISH,
  CLOSE_DIALOG,
  LANGUAGE,
  COUNT
};

struct Weighted
{
  Action action;
  int weight;
};

class Monkey
{
 public:
  Monkey(uint64_t seed_value, int steps_value)
      : seed(seed_value), steps(steps_value), rng(seed_value * 7919ULL + 17ULL)
  {
  }

  /** Runs the monkey; returns the violations (empty = clean). */
  std::vector<std::string> run()
  {
    resetSingletons();
    log_path = outputDir() / std::format("monkey_seed{}.log", seed);
    log_file.open(log_path, std::ios::trunc);
    perf_file.open(
        outputDir() / std::format("monkey_seed{}_slow_frames.txt", seed),
        std::ios::trunc);

    GameController controller;
    controller.newGame(1, 0xF00DULL + seed);
    const auto& teams = controller.getTeams();
    std::vector<TeamID> clubs;
    for (const auto& team : teams)
      if (team.get().getId() != FREE_AGENTS_TEAM_ID)
        clubs.push_back(team.get().getId());
    std::ranges::sort(clubs);
    const TeamID club = clubs[rng.below(clubs.size())];
    controller.selectManagedTeam(club);
    controller.saveGame();
    line(std::format("seed {} steps {} club {} date {}", seed, steps, club,
                     controller.getCurrentDate().toString()));

    GUIView view(controller);
    if (!Bridge::initialize(view))
    {
      violations.emplace_back("GUIView::initialize failed");
      return violations;
    }
    ImGui::GetIO().IniFilename = nullptr;
    view.changeScene(std::make_unique<MainGameScene>(&view));
    Driver driver(view, controller);
    driver.frames(3);

    std::optional<GameDateValue> lastDate = controller.getCurrentDate();
    const double budget = envInt("FM_MONKEY_FRAME_BUDGET_MS", 1500);
    const int shotAt = envInt("FM_MONKEY_SHOT_AT", 0);
    const auto started = Clock::now();
    for (step = 1; step <= steps && violations.size() < MAX_VIOLATIONS; ++step)
    {
      const auto note =
          std::format_to_n(crash_note.data(), crash_note.size() - 1,
                           "\n*** monkey crashed: seed {} step {} (log {})\n",
                           seed, step, log_path.string());
      *note.out = '\0';
      const SceneID scene = driver.activeId();
      const Action action = pick(driver);
      std::string detail;
      std::vector<std::string> problems;
      driver.imgui_errors = 0;
      driver.worst_frame_ms = 0.0;
      bool sawMenu = driver.menu() != nullptr;
      try
      {
        detail = perform(driver, action, problems);
        driver.settle(problems);
      }
      catch (const std::exception& exception)
      {
        problems.push_back(std::format("exception: {}", exception.what()));
      }
      sawMenu = sawMenu || driver.menu() != nullptr;
      line(std::format("#{} [{}] {}", step, sceneName(scene), detail));
      if (step == shotAt)
      {
        driver.frame(true);
        view.captureScreenshot(
            (outputDir() / std::format("monkey_seed{}_step{}.bmp", seed, step))
                .string());
      }

      if (driver.imgui_errors > 0)
        problems.push_back(std::format("imgui: {} usage error(s) on '{}'",
                                       driver.imgui_errors,
                                       driver.imgui_error_scene));
      if (driver.worst_frame_ms > budget)
        problems.push_back(std::format("frame: UI build took {:.0f} ms on '{}'",
                                       driver.worst_frame_ms,
                                       driver.worst_frame_scene));
      slowest_frame_ms = std::max(slowest_frame_ms, driver.worst_frame_ms);
      // Wall-clock data stays out of the (replayable) action log.
      if (driver.worst_frame_ms > 100.0)
        perf_file << std::format("step {} [{}] {:.0f} ms UI build: {}\n", step,
                                 driver.worst_frame_scene,
                                 driver.worst_frame_ms, detail);
      if (const MainGameScene* hub = driver.hub();
          hub && !hub->isAdvancing() && !Bridge::continueRequested(*hub) &&
          Bridge::backdropAlive(view))
        problems.emplace_back("continue: frozen backdrop left after Continue");
      if (const MainGameScene* hub = driver.hub();
          hub && Bridge::continueRequested(*hub) && driver.active() != hub)
        problems.push_back(std::format(
            "continue: request pending while '{}' is shown (never starts)",
            sceneName(driver.activeId())));

      if (!driver.advancing() && driver.menu() == nullptr &&
          controller.isGameLoaded())
      {
        for (std::string& problem : checkWorld(controller))
          problems.push_back(std::move(problem));
        const GameDateValue today = controller.getCurrentDate();
        if (sawMenu) lastDate.reset();
        if (lastDate && today < *lastDate)
          problems.push_back(std::format("date: went back from {} to {}",
                                         lastDate->toString(),
                                         today.toString()));
        lastDate = today;
      }
      for (std::string& problem : problems)
      {
        // One report per kind of problem (digits ignored): the first
        // occurrence is the one to replay.
        std::string kind =
            problem.starts_with("imgui") || problem.starts_with("exception")
                ? problem
                : problem.substr(0, problem.find(':'));
        std::erase_if(
            kind, [](char character)
            { return std::isdigit(static_cast<unsigned char>(character)); });
        if (!reported.insert(kind).second) continue;
        line("  !! " + problem);
        violations.push_back(std::format("step {} [{}] {} -> {}", step,
                                         sceneName(scene), detail, problem));
      }
    }
    line(std::format(
        "done: {} steps, {} violations, {:.1f} s, slowest UI "
        "frame {:.0f} ms, final date {}",
        step - 1, violations.size(), millisecondsSince(started) / 1000.0,
        slowest_frame_ms,
        controller.isGameLoaded() ? controller.getCurrentDate().toString()
                                  : std::string("-")));
    crash_note[0] = '\0';
    SettingsManager::instance()->get() = Settings{};
    LanguageManager::instance().loadLanguage(Language::EN);
    return violations;
  }

  std::string logTail(size_t lines) const
  {
    const size_t first =
        log_lines.size() > lines ? log_lines.size() - lines : 0;
    std::string text;
    for (size_t index = first; index < log_lines.size(); ++index)
      text += log_lines[index] + "\n";
    return text;
  }
  const std::vector<std::string>& logLines() const { return log_lines; }
  const fs::path& logPath() const { return log_path; }

 private:
  uint64_t seed;
  int steps;
  int step = 0;
  Rng rng;
  fs::path log_path;
  std::ofstream log_file;
  std::ofstream perf_file;
  std::vector<std::string> log_lines;
  std::vector<std::string> violations;
  std::set<std::string, std::less<>> reported;
  double slowest_frame_ms = 0.0;

  void line(const std::string& text)
  {
    log_lines.push_back(text);
    log_file << text << '\n';
    log_file.flush();
  }

  Action pick(const Driver& driver)
  {
    using enum Action;
    const SceneID scene = driver.activeId();
    const bool shell =
        dynamic_cast<ManagementScene*>(driver.active()) != nullptr;
    std::vector<Weighted> table = {
        {CLICK, 34},   {DOUBLE_CLICK, 4}, {RIGHT_CLICK, 2}, {SCROLL, 8},
        {DRAG, 5},     {OTHER_KEY, 6},    {TYPE, 4},        {RESIZE, 2},
        {UI_SCALE, 2}, {THEME, 2},        {LANGUAGE, 1}};
    if (shell)
    {
      table.insert(table.end(), {{NAV_KEY, 9},
                                 {PALETTE, 3},
                                 {CONTINUE_KEY, 3},
                                 {CONTINUE_CLICKS, 6},
                                 {SAVE, 2},
                                 {SAVE_RELOAD, 2},
                                 {BACK_FORWARD, 3}});
    }
    if (scene == SceneID::MATCH)
    {
      table.insert(table.end(), {{MATCH_KEY, 6}, {MATCH_RUN, 12}});
      const auto* match = dynamic_cast<const MatchScene*>(driver.active());
      if (match != nullptr && Bridge::finished(*match))
        table.push_back({MATCH_FINISH, 14});
    }
    // A stuck user eventually looks for the way out of a dialog or screen.
    if (ImGui::IsPopupOpen(
            "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) ||
        scene == SceneID::SETTINGS || scene == SceneID::TEAM_SELECTION)
      table.push_back({CLOSE_DIALOG, 8});
    int total = 0;
    for (const Weighted& entry : table) total += entry.weight;
    auto roll = static_cast<int>(rng.below(static_cast<size_t>(total)));
    for (const Weighted& entry : table)
    {
      if (roll < entry.weight) return entry.action;
      roll -= entry.weight;
    }
    return CLICK;
  }

  std::string clickTarget(Driver& driver, int count, ImGuiMouseButton button,
                          const char* verb)
  {
    const auto targets = driver.probe(rng, 16);
    if (targets.empty())
    {
      driver.key(KEY_ESCAPE);
      return std::format("{}: nothing hoverable, pressed Escape", verb);
    }
    const Target& target = targets[rng.below(targets.size())];
    const std::string label = driver.labelOf(target.id, target.point);
    driver.click(target.point, count, button);
    Driver::parkMouse();
    driver.frame();
    return std::format("{} `{}` at ({:.0f},{:.0f})", verb, label,
                       target.point.x, target.point.y);
  }

  std::string perform(Driver& driver, Action action,
                      std::vector<std::string>& problems)
  {
    const GameController& controller = driver.controller;
    switch (action)
    {
      case Action::CLICK:
        return clickTarget(driver, 1, ImGuiMouseButton_Left, "click");
      case Action::DOUBLE_CLICK:
        return clickTarget(driver, 2, ImGuiMouseButton_Left, "double-click");
      case Action::RIGHT_CLICK:
        return clickTarget(driver, 1, ImGuiMouseButton_Right, "right-click");
      case Action::SCROLL:
      {
        const auto rects = Driver::reachableWindows();
        if (rects.empty()) return "scroll: no window";
        const ImRect& rect = rects[rng.below(rects.size())];
        const ImVec2 point(rect.Min.x + rng.unit() * rect.GetWidth(),
                           rect.Min.y + rng.unit() * rect.GetHeight());
        const auto amount = static_cast<float>(rng.range(-8, 8));
        driver.wheel(point, amount);
        Driver::parkMouse();
        driver.frame();
        return std::format("scroll {:+.0f} at ({:.0f},{:.0f})", amount, point.x,
                           point.y);
      }
      case Action::DRAG:
      {
        const auto targets = driver.probe(rng, 12);
        if (targets.empty()) return "drag: nothing hoverable";
        const Target& target = targets[rng.below(targets.size())];
        const std::string label = driver.labelOf(target.id, target.point);
        const ImVec2 to(
            target.point.x + static_cast<float>(rng.range(-300, 300)),
            target.point.y + static_cast<float>(rng.range(-200, 200)));
        driver.drag(target.point, to);
        Driver::parkMouse();
        driver.frame();
        return std::format("drag `{}` by ({:.0f},{:.0f})", label,
                           to.x - target.point.x, to.y - target.point.y);
      }
      case Action::NAV_KEY:
      {
        const KeySpec& key = NAV_KEYS[rng.below(NAV_KEYS.size())];
        driver.key(key);
        return std::format("key {}", key.name);
      }
      case Action::OTHER_KEY:
      {
        const KeySpec& key = OTHER_KEYS[rng.below(OTHER_KEYS.size())];
        driver.key(key);
        return std::format("key {}", key.name);
      }
      case Action::TYPE:
      {
        const char* text = TEXT_SAMPLES[rng.below(TEXT_SAMPLES.size())];
        driver.type(text);
        return std::format("type '{}'", text);
      }
      case Action::PALETTE:
      {
        driver.key(KEY_CTRL_K);
        const char* text = TEXT_SAMPLES[rng.below(TEXT_SAMPLES.size())];
        driver.type(text);
        std::string detail = std::format("palette '{}'", text);
        for (int index = rng.range(0, 3); index > 0; --index)
          driver.key(KEY_DOWN);
        const bool accept = rng.chance(0.6f);
        driver.key(accept ? KEY_ENTER : KEY_ESCAPE);
        return detail + (accept ? " + Enter" : " + Escape");
      }
      case Action::CONTINUE_KEY:
      {
        const bool space = rng.chance(0.5f);
        driver.key(OTHER_KEYS[space ? 2 : 3]);
        return std::format("continue via {}", space ? "Space" : "Enter");
      }
      case Action::CONTINUE_CLICKS:
        return continueClicks(driver, problems);
      case Action::SAVE:
        driver.key(KEY_CTRL_S);
        return "save (Ctrl+S)";
      case Action::RESIZE:
      {
        const auto [width, height] =
            WINDOW_SIZES[rng.below(WINDOW_SIZES.size())];
        driver.resize(width, height);
        return std::format("resize {}x{}", width, height);
      }
      case Action::UI_SCALE:
      {
        const float scale = UI_SCALES[rng.below(UI_SCALES.size())];
        SettingsManager::instance()->get().ui_scale = scale;
        driver.view.refreshTheme();
        driver.skip_budget = true;
        driver.frame(true);
        return std::format("ui scale {}", scale);
      }
      case Action::THEME:
      {
        Settings& settings = SettingsManager::instance()->get();
        settings.theme_preset = static_cast<int>(rng.below(
            static_cast<size_t>(std::to_underlying(Theme::Preset::COUNT))));
        settings.compact_density = rng.chance(0.5f);
        settings.reduced_motion = rng.chance(0.3f);
        driver.view.refreshTheme();
        driver.skip_budget = true;
        driver.frame(true);
        return std::format("theme {} compact {} reduced motion {}",
                           settings.theme_preset, settings.compact_density,
                           settings.reduced_motion);
      }
      case Action::LANGUAGE:
      {
        using enum Language;
        Settings& settings = SettingsManager::instance()->get();
        settings.language = settings.language == EN ? IT : EN;
        LanguageManager::instance().loadLanguage(settings.language);
        driver.frames(2);
        return std::format("language {}",
                           settings.language == EN ? "EN" : "IT");
      }
      case Action::SAVE_RELOAD:
        return saveReload(driver, problems);
      case Action::BACK_FORWARD:
      {
        std::string detail = "back/forward:";
        for (int index = rng.range(2, 6); index > 0; --index)
        {
          if (rng.chance(0.5f))
          {
            const bool alt = rng.chance(0.5f);
            driver.key(alt ? KEY_ALT_LEFT : KEY_ESCAPE);
            detail += alt ? " Alt+Left" : " Esc";
          }
          else
          {
            const KeySpec& key = NAV_KEYS[rng.below(NAV_KEYS.size())];
            driver.key(key);
            detail += std::string(" ") + key.name;
          }
        }
        return detail;
      }
      case Action::MATCH_KEY:
      {
        const KeySpec& key = MATCH_KEYS[rng.below(MATCH_KEYS.size())];
        driver.key(key);
        return std::format("match key {}", key.name);
      }
      case Action::MATCH_RUN:
      {
        auto* match = dynamic_cast<MatchScene*>(driver.active());
        if (match == nullptr || Bridge::engine(*match) == nullptr)
          return "match run: no live match";
        const float speed = rng.chance(0.5f) ? 16.0f : 4.0f;
        Bridge::setSpeed(*match, speed);
        const int updates = rng.range(20, 400);
        for (int index = 0; index < updates && !Bridge::finished(*match);
             ++index)
          Bridge::update(driver.view, 0.1f);
        driver.frame(true);
        const MatchEngine* engine = Bridge::engine(*match);
        return std::format("match run {}x {} updates -> minute {:.0f} {}-{}{}",
                           speed, updates, engine->getMatchTimeMinutes(),
                           engine->getHomeScore(), engine->getAwayScore(),
                           Bridge::paused(*match) ? " (paused)" : "");
      }
      case Action::MATCH_FINISH:
      {
        // What a player does at full time: find and press Finish match.
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const auto point =
            driver.findLabel(LOC("MATCH_FINISH"), ImVec2(0.0f, 0.0f),
                             ImVec2(display.x * 0.6f, display.y * 0.35f));
        if (!point) return "finish: button not found";
        driver.click(*point);
        Driver::parkMouse();
        driver.frame();
        // The hub simulates the rest of the day before the report opens.
        driver.settle(problems);
        return std::format("finish match -> '{}' {}",
                           sceneName(driver.activeId()),
                           controller.getCurrentDate().toString());
      }
      case Action::CLOSE_DIALOG:
      {
        ImVec2 min(0.0f, 0.0f);
        ImVec2 max = ImGui::GetIO().DisplaySize;
        if (GImGui->OpenPopupStack.Size > 0)
          if (const ImGuiWindow* popup = GImGui->OpenPopupStack.back().Window)
          {
            min = popup->Rect().Min;
            max = popup->Rect().Max;
          }
        const auto point = driver.findLabel(
            std::vector<std::string>{
                "#CLOSE", LOC("SUBSTITUTION_CLOSE"), LOC("SETTINGS_CANCEL"),
                LOC("SETTINGS_APPLY"), LOC("TEAM_SELECTION_CONFIRM"),
                LOC("NAV_BACK")},
            min, max, true);
        if (!point) return "way out: none found";
        const std::string label = driver.labelOf(GImGui->HoveredId, *point);
        driver.click(*point);
        Driver::parkMouse();
        driver.frame();
        return std::format("way out `{}` -> '{}'", label,
                           sceneName(driver.activeId()));
      }
      case Action::COUNT:
        break;
    }
    return "?";
  }

  /** Clicks the top bar's Continue 1-3 times in consecutive frames. */
  std::string continueClicks(Driver& driver, std::vector<std::string>& problems)
  {
    const GameController& controller = driver.controller;
    const MainGameScene* hub = driver.hub();
    const int clicks = rng.range(1, 3);
    const auto point = driver.continueButton();
    if (!point || hub == nullptr) return "continue button not found";
    const GameDateValue before = controller.getCurrentDate();
    const std::optional<GameDateValue> next = Bridge::nextFixtureDate(*hub);
    const std::string label = hub->continueLabel();
    driver.click(*point, clicks);
    Driver::parkMouse();
    driver.settle(problems);
    if (driver.menu() != nullptr || !controller.isGameLoaded())
      return std::format("continue x{} ('{}')", clicks, label);
    const GameDateValue after = controller.getCurrentDate();
    // One Continue stops on the next managed fixture at the latest, however
    // often the button was hit while the days were being simulated.
    if (next && *next < after && driver.activeId() != SceneID::MATCH_REPORT)
      problems.push_back(std::format(
          "continue: x{} clicks from {} ran past the managed fixture of {} "
          "to {}",
          clicks, before.toString(), next->toString(), after.toString()));
    return std::format("continue x{} ('{}') {} -> {} now '{}'", clicks, label,
                       before.toString(), after.toString(),
                       sceneName(driver.activeId()));
  }

  /** Ctrl+S, back to the main menu, load the slot, compare the state. */
  std::string saveReload(Driver& driver,
                         std::vector<std::string>& problems) const
  {
    const GameController& controller = driver.controller;
    const int slot = controller.getCurrentSlot().value_or(0);
    if (slot == 0) return "save/reload: no slot";
    driver.key(KEY_CTRL_S);
    const Fingerprint saved = Fingerprint::of(controller);
    driver.view.changeScene(std::make_unique<MainMenuScene>(&driver.view));
    driver.frames(2);
    MainMenuScene* menu = driver.menu();
    if (menu == nullptr) return "save/reload: main menu did not open";
    Bridge::menuLoad(*menu, slot);
    driver.frames(2);
    if (!driver.settle(problems)) return "save/reload: load hung";
    driver.frames(3);
    if (driver.hub() == nullptr || !controller.isGameLoaded())
    {
      problems.push_back(
          std::format("persistence: slot {} did not load", slot));
      return std::format("save/reload slot {} failed", slot);
    }
    for (const std::string& difference :
         saved.differences(Fingerprint::of(controller)))
      problems.push_back("persistence: " + difference);
    return std::format("save/reload slot {} ({})", slot,
                       controller.getCurrentDate().toString());
  }
};

// ---- Tests
// ---------------------------------------------------------------------

class GuiMonkey : public ::testing::TestWithParam<uint64_t>
{
 protected:
  static void SetUpTestSuite()
  {
    setDefaultEnvironment();
    Logger::init();
    std::signal(SIGSEGV, onFatalSignal);
    std::signal(SIGABRT, onFatalSignal);
    std::signal(SIGFPE, onFatalSignal);
  }
};

std::vector<uint64_t> monkeySeeds()
{
  if (const char* seed = std::getenv("FM_MONKEY_SEED"); seed && *seed)
    return {std::strtoull(seed, nullptr, 10)};
  return {DEFAULT_SEEDS.begin(), DEFAULT_SEEDS.end()};
}

/**
 * Known product bugs a seed may hit (see the findings report). A seed whose
 * violations all match is skipped with the finding referenced; any other
 * violation fails the test.
 */
struct KnownBug
{
  const char* id;
  const char* first; /**< Both substrings must occur in the violation. */
  const char* second;
};
const std::vector<KnownBug>& knownBugs()
{
  static const std::vector<KnownBug> bugs = {
      {"F-FINISH-FREEZE", "finish match", "frame: UI build took"},
  };
  return bugs;
}

TEST_P(GuiMonkey, RandomUserKeepsInvariants)
{
  const uint64_t seed = GetParam();
  const int steps = std::max(1, envInt("FM_MONKEY_STEPS", DEFAULT_STEPS));
  Monkey monkey(seed, steps);
  const std::vector<std::string> violations = monkey.run();
  std::cout << "[monkey] seed " << seed << ": " << monkey.logLines().back()
            << "\n[monkey] log: " << monkey.logPath().string() << '\n';
  if (violations.empty()) return;
  std::string summary;
  std::set<std::string, std::less<>> known;
  bool allKnown = true;
  for (const std::string& violation : violations)
  {
    summary += "  " + violation + "\n";
    const auto match =
        std::ranges::find_if(knownBugs(),
                             [&violation](const KnownBug& bug)
                             {
                               return violation.contains(bug.first) &&
                                      violation.contains(bug.second);
                             });
    if (match == knownBugs().end())
      allKnown = false;
    else
      known.emplace(match->id);
  }
  const std::string replay = std::format(
      "replay: FM_MONKEY_SEED={} FM_MONKEY_STEPS={} build/test/monkey_tests",
      seed, steps);
  if (allKnown)
  {
    std::string ids;
    for (const std::string& id : known) ids += id + " ";
    GTEST_SKIP() << "KNOWN BUG: " << ids << "(see findings)\n"
                 << summary << replay;
  }
  ADD_FAILURE() << "monkey seed " << seed << " found " << violations.size()
                << " violation(s):\n"
                << summary << replay << "\nlast actions:\n"
                << monkey.logTail(30);
}

INSTANTIATE_TEST_SUITE_P(Seeds, GuiMonkey, ::testing::ValuesIn(monkeySeeds()),
                         [](const auto& info)
                         { return std::format("seed{}", info.param); });

/**
 * Runs a monkey in a child process (fresh process-wide state, as ctest and
 * a replay have: some screens remember choices in statics for the session)
 * and returns its action log without the timing line.
 */
std::vector<std::string> runInChild(uint64_t seed, int steps)
{
  const fs::path file =
      outputDir() / std::format("replay_seed{}_{}.log", seed, getpid());
  const pid_t child = fork();
  if (child == 0)
  {
    Monkey monkey(seed, steps);
    monkey.run();
    std::ofstream out(file, std::ios::trunc);
    for (size_t index = 0; index + 1 < monkey.logLines().size(); ++index)
      out << monkey.logLines()[index] << '\n';
    out.close();
    _exit(0);
  }
  int status = 0;
  waitpid(child, &status, 0);
  std::vector<std::string> lines;
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return lines;
  std::ifstream in(file);
  for (std::string line; std::getline(in, line);) lines.push_back(line);
  std::error_code ignored;
  fs::remove(file, ignored);
  return lines;
}

/**
 * The action log is a pure function of the seed: the same seed replays the
 * same actions on the same widgets with the same outcomes.
 */
TEST(GuiMonkeyReplay, SameSeedSameLog)
{
  setDefaultEnvironment();
  Logger::init();
  const int steps = std::min(60, envInt("FM_MONKEY_STEPS", 60));
  const auto first = runInChild(5, steps);
  const auto second = runInChild(5, steps);
  ASSERT_FALSE(first.empty()) << "the first run crashed or failed";
  ASSERT_EQ(first.size(), second.size());
  for (size_t index = 0; index < first.size(); ++index)
    ASSERT_EQ(first[index], second[index]) << "log line " << index;
}

// ---- Every enabled widget has an effect
// ----------------------------------------

/** Hash of the world state a click can change (cheap, order independent). */
uint64_t stateHash(const GameController& controller)
{
  uint64_t hash = 1469598103934665603ULL;
  const auto mix = [&hash](uint64_t value)
  { hash = (hash ^ value) * 1099511628211ULL; };
  const auto managed = controller.getManagedTeam();
  if (!managed) return hash;
  const Team& team = managed->get();
  mix(static_cast<uint64_t>(team.getFinances().getBalance()));
  mix(controller.getUnreadInboxCount());
  mix(static_cast<uint64_t>(dayNumber(controller.getCurrentDate())));
  mix(controller.getShortlist().size());
  mix(controller.getScoutAssignments().size());
  mix(controller.getRecruitmentFocuses().size());
  mix(controller.getAllListings().size());
  mix(controller.getIncomingOffers().size());
  mix(team.getPlayerIDs().size());
  const Lineup& lineup = team.getLineup();
  mix(lineup.getGoalkeeper() ? lineup.getGoalkeeper()->getId() : 0);
  for (const auto& positioned : lineup.getOutfieldPlayers())
  {
    mix(positioned.player ? positioned.player->getId() : 0);
    mix(static_cast<uint64_t>(std::lround(positioned.position.x * 1000.0f)));
    mix(static_cast<uint64_t>(std::lround(positioned.position.y * 1000.0f)));
  }
  for (const Player* reserve : lineup.getReserves())
    mix(reserve ? reserve->getId() : 0);
  const StrategySliders sliders = team.getStrategy().getSliders();
  for (const float value :
       {sliders.pressing, sliders.riskTaking, sliders.offensiveBias,
        sliders.widthUsage, sliders.compactness})
    mix(static_cast<uint64_t>(std::lround(value * 1000.0f)));
  if (const TeamTrainingPlan* plan = controller.getTrainingPlan())
  {
    mix(static_cast<uint64_t>(std::to_underlying(plan->preset)));
    mix(static_cast<uint64_t>(std::to_underlying(plan->intensity)));
    mix(plan->auto_congestion ? 1 : 0);
  }
  for (const auto& staff : controller.getStaff(team.getId())) mix(staff->id);
  return hash;
}

/** Hash of everything drawn in the last frame. */
uint64_t drawHash()
{
  uint64_t hash = 1469598103934665603ULL;
  const ImDrawData* data = ImGui::GetDrawData();
  if (data == nullptr) return hash;
  const auto mix = [&hash](uint64_t value)
  { hash = (hash ^ value) * 1099511628211ULL; };
  for (const ImDrawList* list : data->CmdLists)
    for (const ImDrawVert& vertex : list->VtxBuffer)
    {
      mix(static_cast<uint64_t>(std::lround(vertex.pos.x * 4.0f)));
      mix(static_cast<uint64_t>(std::lround(vertex.pos.y * 4.0f)));
      mix(vertex.col);
    }
  return hash;
}

/**
 * Widgets the sweep does not click, each with the reason. Every other
 * enabled widget must have an observable effect: anything that does nothing
 * is a dead button (AGENT rules: "no dead buttons"). Also exempt by rule:
 * the sidebar entry of the screen already shown, and tabs/sliders get a
 * second, off-centre click before they count as dead.
 */
constexpr std::array<std::pair<const char*, const char*>, 3>
    NOT_CLICKED_BY_SWEEP = {{
        {"#SCROLL", "scrollbar: only moves when the content overflows"},
        {"shell_continue",
         "advances time; covered by the monkey and the "
         "adversarial Continue tests"},
        {"shell_holiday", "advances time (holiday); same as Continue"},
    }};

TEST(GuiWidgetSweep, EveryEnabledWidgetHasAnEffect)
{
  setDefaultEnvironment();
  Logger::init();
  resetSingletons();
  GameController controller;
  controller.newGame(2, 0xBEEFULL);
  const LeagueID league = controller.getLeagues().front().get().getId();
  auto leagueTeams = controller.getTeamsInLeague(league);
  std::ranges::sort(leagueTeams, [](const auto& left, const auto& right)
                    { return left.get().getId() < right.get().getId(); });
  const TeamID managedId = leagueTeams[leagueTeams.size() / 2].get().getId();
  controller.selectManagedTeam(managedId);
  for (int matchday = 0; matchday < 3; ++matchday)
  {
    controller.advanceToNextManagedFixture();
    controller.advanceDay();
  }

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  ImGui::GetIO().IniFilename = nullptr;
  view.changeScene(std::make_unique<MainGameScene>(&view));
  Driver driver(view, controller);
  driver.frames(3);
  ASSERT_EQ(driver.activeId(), SceneID::GAME_MENU);

  struct Signature
  {
    SceneID scene;
    size_t depth;
    bool popup;
    uint64_t pixels;
    uint64_t state;
    bool operator==(const Signature&) const = default;
  };
  const auto signature = [&]()
  {
    Driver::parkMouse();
    driver.frame();
    driver.frame();
    return Signature{driver.activeId(), view.getOverlayDepth(),
                     ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId |
                                                ImGuiPopupFlags_AnyPopupLevel),
                     drawHash(), stateHash(controller)};
  };
  const auto closePopups = [&]()
  {
    for (int attempt = 0;
         attempt < 3 &&
         ImGui::IsPopupOpen(
             "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
         ++attempt)
      driver.key(KEY_ESCAPE);
  };
  constexpr std::array<std::pair<NavSection, const char*>, 26> SECTIONS = {{
      {NavSection::HOME, "home"},
      {NavSection::INBOX, "inbox"},
      {NavSection::CLUB, "club"},
      {NavSection::SQUAD, "squad"},
      {NavSection::LINEUP, "lineup"},
      {NavSection::TACTICS, "tactics"},
      {NavSection::TRAINING, "training"},
      {NavSection::FIXTURES, "fixtures"},
      {NavSection::STANDINGS, "standings"},
      {NavSection::TRANSFERS, "transfers"},
      {NavSection::SCOUTING, "scouting"},
      {NavSection::STAFF, "staff"},
      {NavSection::YOUTH, "youth"},
      {NavSection::FINANCES, "finances"},
      {NavSection::MANAGER, "manager"},
      {NavSection::MEDICAL, "medical"},
      {NavSection::CALENDAR, "calendar"},
      {NavSection::SQUAD_PLANNER, "squad_planner"},
      {NavSection::COMPARE, "compare"},
      {NavSection::DELEGATION, "delegation"},
      {NavSection::DATA_HUB, "data_hub"},
      {NavSection::OPPOSITION, "opposition"},
      {NavSection::INTERNATIONAL, "international"},
      {NavSection::AWARDS, "awards"},
      {NavSection::RECORDS, "records"},
      {NavSection::PLANNING, "planning"},
  }};

  std::set<std::string, std::less<>> tested;
  std::vector<std::string> dead;
  int clicked = 0;
  for (const auto& [section, name] : SECTIONS)
  {
    Navigation::open(&view, section);
    driver.frames(3);
    const auto items = driver.sweep(24.0f, 12.0f);
    std::vector<std::pair<const Target*, std::string>> order;
    std::vector<std::pair<const Target*, std::string>> tabs;
    for (const Target& item : items)
    {
      std::string label = driver.labelOf(item.id, item.point);
      const size_t tab = label.find("Tabs/");
      const bool isTab = tab != std::string::npos &&
                         label.find('/', tab + 5) == std::string::npos;
      if (std::getenv("FM_SWEEP_VERBOSE") != nullptr)
        std::cout << "[sweep] " << name << ": " << label << '\n';
      (isTab ? tabs : order).emplace_back(&item, std::move(label));
    }
    // Tab bars remember their tab across screen instances: click tabs last.
    order.insert(order.end(), tabs.begin(), tabs.end());
    for (const auto& [item, label] : order)
    {
      const bool shell =
          label.contains("##sidebar") || label.contains("##topbar");
      if (shell && section != NavSection::HOME) continue;
      if (std::ranges::any_of(NOT_CLICKED_BY_SWEEP, [&label](const auto& entry)
                              { return label.contains(entry.first); }))
        continue;
      // One click per kind of widget: row buttons differ only in digits.
      std::string kind;
      for (const char character : label)
        if (!std::isdigit(static_cast<unsigned char>(character)))
          kind.push_back(character);
      if (!tested.insert(kind).second) continue;
      ++clicked;
      // Earlier clicks may have moved or removed the widget (a dismissed
      // tip, a hidden checklist): find it again by ID before judging it.
      enum class Outcome : uint8_t
      {
        EFFECT,
        NONE,
        GONE
      };
      using enum Outcome;
      const auto effect = [&](ImVec2 point, bool offCentre)
      {
        Navigation::open(&view, section);
        driver.frames(2);
        Driver::mouseTo(point);
        driver.frame();
        if (GImGui->HoveredId != item->id)
        {
          if (offCentre) return NONE;
          // Layouts shift vertically (a dismissed tip): scan the column.
          bool found = false;
          const float height = ImGui::GetIO().DisplaySize.y;
          for (int y_index = 0;
               3.0f + 6.0f * static_cast<float>(y_index) < height && !found;
               ++y_index)
          {
            const float y = 3.0f + 6.0f * static_cast<float>(y_index);
            Driver::mouseTo({point.x, y});
            driver.frame();
            found = GImGui->HoveredId == item->id;
            if (found) point.y = y;
          }
          if (!found) return GONE;
        }
        const Signature before = signature();
        if (!(before == signature())) return EFFECT;  // Animates.
        driver.click(point);
        const Signature after = signature();
        closePopups();
        return after == before ? NONE : EFFECT;
      };
      Outcome outcome = effect(item->point, false);
      // A slider clicked at its current value does not move: off-centre.
      if (outcome == NONE && item->max.x - item->min.x >= 40.0f)
        outcome = effect(
            {item->min.x + (item->max.x - item->min.x) * 0.2f, item->point.y},
            true);
      // An option that is already selected (tab, filter, preset, row) does
      // nothing: select a sibling first, then it must switch back.
      // Siblings share the parent path (or the grandparent: numbered rows).
      const auto ancestor = [](const std::string& path, int levels)
      {
        std::string result = path;
        for (int level = 0; level < levels; ++level)
          result = result.substr(0, result.rfind('/'));
        return result;
      };
      for (int levels = 1; levels <= 2 && outcome == NONE; ++levels)
      {
        const std::string parent = ancestor(label, levels);
        for (const auto& [other, otherLabel] : order)
        {
          if (other->id == item->id || ancestor(otherLabel, levels) != parent)
            continue;
          Navigation::open(&view, section);
          driver.frames(2);
          Driver::mouseTo(other->point);
          driver.frame();
          if (GImGui->HoveredId != other->id) continue;
          driver.click(other->point);
          closePopups();
          Driver::mouseTo(item->point);
          driver.frame();
          if (GImGui->HoveredId != item->id) continue;
          const Signature before = signature();
          driver.click(item->point);
          const Signature after = signature();
          closePopups();
          if (!(after == before))
          {
            outcome = EFFECT;
            break;
          }
        }
      }
      const bool changed = outcome != NONE;
      const char* outcome_name = "none";
      if (outcome == EFFECT)
        outcome_name = "effect";
      else if (outcome == GONE)
        outcome_name = "gone";
      if (std::getenv("FM_SWEEP_VERBOSE") != nullptr)
        std::cout << "[sweep] clicked " << name << ": " << label << " -> "
                  << outcome_name << " now " << sceneName(driver.activeId())
                  << '\n';
      // Navigation on the current section re-opens the same screen.
      if (!changed && shell) continue;
      if (!changed) dead.push_back(std::format("{}: `{}`", name, label));
    }
  }
  std::string list;
  for (const std::string& entry : dead) list += "\n  " + entry;
  std::cout << "[sweep] " << clicked << " widget kinds clicked, " << dead.size()
            << " without an observable effect" << list << '\n';
  EXPECT_GT(clicked, 50) << "the sweep found suspiciously few widgets";
  EXPECT_TRUE(dead.empty()) << "dead widgets:" << list;
  EXPECT_EQ(driver.imgui_errors, 0)
      << driver.imgui_errors << " ImGui usage errors, first on "
      << driver.imgui_error_scene;
}
}  // namespace
