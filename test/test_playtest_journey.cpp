// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------
//
// Playtest journey: a career driven through the real GUI screens the way a
// player would (main menu, new game, club choice, every management screen,
// dialogs, a transfer, scouting, training, live matches in 2D and 3D, then
// months of Continue), with invariants, screenshots and timings.
//
// Artifacts (for human/agent review) go to $FM_PLAYTEST_OUT when set:
//   shots/ (BMP files) screenshots of every visited screen and dialog
//   journey_log.md    what happened (results, tables, transfer talks, ...)
//   metrics.md        per-screen frame times and Continue latency
//   buttons.md        interactive items of each screen and their effect
//
// Label "playtest" (own executable): exclude with `ctest -LE playtest`.

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
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
#include "gui/scenes/inbox_scene.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/match_scene.h"
#include "gui/scenes/player_profile_scene.h"
#include "gui/scenes/scouting_scene.h"
#include "gui/scenes/settings_scene.h"
#include "gui/scenes/standings_scene.h"
#include "gui/scenes/transfer_market_scene.h"
#include "gui/view_models/formation.h"
#include "model/calendar.h"
#include "model/finances.h"
#include "model/match_engine.h"

#if defined(__clang__) || defined(__GNUC__)
extern "C" const char* __lsan_default_suppressions()
{
  return "leak:libSDL3.so\n";
}
#endif

/**
 * The GUI classes grant their internals to this name (the GUI lifecycle test
 * of test_game_flow.cpp, which is not linked into this executable). Here it
 * is only a bridge exposing what a headless player needs.
 */
class GameFlowTest_GUIFlowLifecycle_Test
{
 public:
  static bool initialize(GUIView& view) { return view.initialize(); }
  static void applyPending(GUIView& view) { view.applyPendingSceneChanges(); }
  static void handleEvents(GUIView& view) { view.handleEvents(); }
  static void update(GUIView& view, float seconds) { view.update(seconds); }
  static void render(GUIView& view) { view.render(); }
  static GUIScene* activeScene(const GUIView& view)
  {
    return view.getActiveScene();
  }

  static MatchEngine* engine(const MatchScene& scene)
  {
    return scene.engine.get();
  }
  static std::pair<TeamID, TeamID> teams(const MatchScene& scene)
  {
    return {scene.home_team_id, scene.away_team_id};
  }
  static bool finished(const MatchScene& scene) { return scene.match_finished; }
  static bool paused(const MatchScene& scene) { return scene.is_paused; }
  /** What the speed and highlights buttons choose. */
  static void setPlayback(MatchScene& scene, float speed, bool highlights)
  {
    scene.setPlaybackSpeed(speed);
    scene.setHighlightsOnly(highlights);
  }
  static void setPaused(MatchScene& scene, bool paused)
  {
    scene.is_paused = paused;
  }
  static void showSubstitutions(MatchScene& scene, bool show)
  {
    scene.show_substitutions = show;
  }
  static void setView(MatchScene& scene, MatchViewMode mode)
  {
    scene.setViewMode(mode);
  }
  static void setCamera(MatchScene& scene, MatchCameraMode mode)
  {
    scene.setCameraMode(mode);
  }
  static void showNames(MatchScene& scene, bool show)
  {
    scene.show_player_names = show;
  }

  static bool openOfferDialog(TransferMarketScene& scene, PlayerID player)
  {
    for (const auto& row : scene.targets)
    {
      if (row.id != player) continue;
      scene.openOfferDialog(row);
      return true;
    }
    return false;
  }
  static void openContractDialog(TransferMarketScene& scene, PlayerID player)
  {
    scene.openContractDialog(player);
  }
  static size_t targetCount(const TransferMarketScene& scene)
  {
    return scene.targets.size();
  }

  static void requestRenew(PlayerProfileScene& scene)
  {
    scene.renew_requested = true;
  }
  static void requestList(PlayerProfileScene& scene)
  {
    scene.list_confirm_requested = true;
  }
  static void showCup(StandingsScene& scene)
  {
    scene.showing_cup = true;
    scene.refresh();
  }
  static void openPalette(ManagementScene& scene) { scene.openPalette(); }
  static size_t paletteMatches(const ManagementScene& scene)
  {
    return scene.palette_matches.size();
  }
  static void openInboxMessage(InboxScene& scene, size_t index)
  {
    if (scene.threads.empty()) return;
    scene.selected_thread = 0;
    scene.openMessage(index);
  }
};

namespace
{
using Bridge = GameFlowTest_GUIFlowLifecycle_Test;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

/** FM_PLAYTEST_OUT (e.g. /tmp/fm-playtest) keeps the artifacts; otherwise
 * they go to the per-process test root, removed at exit, so concurrent
 * runs never clobber each other. */
fs::path outputDirectory()
{
  const char* configured = std::getenv("FM_PLAYTEST_OUT");
  return configured && *configured ? fs::path(configured)
                                   : RuntimePaths::root() / "playtest";
}
const fs::path& outputDir()
{
  static const fs::path directory = outputDirectory();
  return directory;
}
const fs::path& shotDir()
{
  static const fs::path directory = outputDir() / "shots";
  return directory;
}
constexpr float FRAME_SECONDS = 1.0f / 60.0f;
constexpr int TIMED_FRAMES = 24;
constexpr int GUI_MONTHS_DAYS = 61;
/** Headless days after the GUI months; FM_PLAYTEST_FULL_SEASON=1 plays the
 * rest of the season and the rollover instead (several minutes, so not in
 * the timed ctest run). */
constexpr int HEADLESS_DAYS = 28;
constexpr auto HEADLESS_DEADLINE = std::chrono::seconds(120);
constexpr auto SEASON_DEADLINE = std::chrono::seconds(600);
constexpr auto LOAD_DEADLINE = std::chrono::seconds(90);

double millisecondsSince(Clock::time_point start)
{
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
      .count();
}

struct Stats
{
  double median = 0.0;
  double p95 = 0.0;
  double worst = 0.0;
};

Stats summarize(std::vector<double> samples)
{
  if (samples.empty()) return {};
  std::ranges::sort(samples);
  return {samples[samples.size() / 2],
          samples[static_cast<size_t>(
              std::floor(0.95 * static_cast<double>(samples.size() - 1)))],
          samples.back()};
}

std::string dateText(const GameDateValue& date) { return date.toString(); }

int dayNumber(const GameDateValue& date)
{
  // Days since 2000-01-01 on the proleptic Gregorian calendar.
  const int year = date.month <= 2 ? date.year - 1 : date.year;
  const int era = year / 400;
  const int yoe = year - era * 400;
  const int month = date.month;
  const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + date.day - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 730425;
}

/** One interactive widget found under the mouse. */
struct Item
{
  ImGuiID id = 0;
  ImVec2 point;
  ImVec2 min{FLT_MAX, FLT_MAX};
  ImVec2 max{-FLT_MAX, -FLT_MAX};
  std::string label; /**< ID path, e.g. "##main_menu/New Game". */
};

/** Markdown report writer shared by the tests. */
class Report
{
 public:
  explicit Report(const fs::path& path) : out(path) {}
  template <typename... Args>
  void line(std::format_string<Args...> format, Args&&... args)
  {
    out << std::format(format, std::forward<Args>(args)...) << '\n';
    out.flush();
  }

 private:
  std::ofstream out;
};

/**
 * Drives GUIView like a player: frames, clicks, discovery of clickable
 * widgets, screenshots and timings.
 */
class Tester
{
 public:
  Tester(GUIView& view_ref, GameController& controller_ref, Report& log_ref)
      : view(view_ref), controller(controller_ref), log(log_ref)
  {
  }

  std::string step = "start";

  GUIScene* active() const { return Bridge::activeScene(view); }
  SceneID activeId() const
  {
    return active() ? active()->getID() : SceneID::MAIN_MENU;
  }

  void frame(float seconds = FRAME_SECONDS)
  {
    Bridge::applyPending(view);
    Bridge::handleEvents(view);
    Bridge::update(view, seconds);
    Bridge::render(view);
    checkImGui();
    frames_since_flush = 0;
  }

  void frames(int count)
  {
    for (int index = 0; index < count; ++index) frame();
  }

  /** ImGui-only frame (no rasterization): returns the UI build time. */
  double uiFrame(float seconds = FRAME_SECONDS,
                 const std::function<void()>& inFrame = {})
  {
    Bridge::applyPending(view);
    Bridge::handleEvents(view);
    Bridge::update(view, seconds);
    const auto start = Clock::now();
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (GUIScene* scene = active()) scene->render();
    if (inFrame) inFrame();
    ImGui::Render();
    const double elapsed = millisecondsSince(start);
    checkImGui();
    // Font atlas updates are uploaded by the renderer backend; flush them
    // regularly with a real frame.
    if (++frames_since_flush >= 48)
    {
      frames_since_flush = 0;
      frame(0.0f);
    }
    return elapsed;
  }

  void checkImGui()
  {
    const int errors = ImGui::GetCurrentContext()->ErrorCountCurrentFrame;
    EXPECT_EQ(errors, 0) << "ImGui usage error during: " << step;
    if (errors > 0) ++imgui_error_frames;
  }

  fs::path shot(const std::string& name) const
  {
    const fs::path path = shotDir() / (name + ".bmp");
    fs::remove(path);
    EXPECT_TRUE(view.captureScreenshot(path.string())) << name;
    EXPECT_TRUE(fs::exists(path)) << name;
    return path;
  }

  /** Full-frame and UI-only frame times of the current screen. */
  void measure(const std::string& screen, int count = TIMED_FRAMES)
  {
    std::vector<double> full;
    std::vector<double> ui;
    for (int index = 0; index < count; ++index)
    {
      const auto start = Clock::now();
      frame();
      full.push_back(millisecondsSince(start));
    }
    for (int index = 0; index < count; ++index) ui.push_back(uiFrame());
    frame();
    timings.emplace_back(screen, summarize(full), summarize(ui));
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
  void click(ImVec2 point)
  {
    mouseTo(point);
    frame();
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    parkMouse();
    frame();
  }
  void key(ImGuiKey imguiKey)
  {
    ImGui::GetIO().AddKeyEvent(imguiKey, true);
    frame();
    ImGui::GetIO().AddKeyEvent(imguiKey, false);
    frame();
  }

  // ---- Discovery ----
  /** Sweeps the mouse over a region and records every hoverable widget. */
  std::vector<Item> discover(ImVec2 regionMin = {0.0f, 0.0f},
                             ImVec2 regionMax = {0.0f, 0.0f},
                             float stepX = 22.0f, float stepY = 11.0f)
  {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (regionMax.x <= 0.0f) regionMax = display;
    std::vector<Item> items;
    std::unordered_map<ImGuiID, size_t> index;
    for (int row = 0;; ++row)
    {
      const float y =
          regionMin.y + stepY * 0.5f + static_cast<float>(row) * stepY;
      if (!(y < regionMax.y)) break;
      for (int column = 0;; ++column)
      {
        const float x =
            regionMin.x + stepX * 0.5f + static_cast<float>(column) * stepX;
        if (!(x < regionMax.x)) break;
        mouseTo({x, y});
        uiFrame();
        const ImGuiID id = GImGui->HoveredId;
        if (id == 0) continue;
        auto [found, inserted] = index.emplace(id, items.size());
        if (inserted)
        {
          Item item;
          item.id = id;
          item.point = {x, y};
          items.push_back(item);
        }
        Item& item = items[found->second];
        item.min = {std::min(item.min.x, x), std::min(item.min.y, y)};
        item.max = {std::max(item.max.x, x), std::max(item.max.y, y)};
      }
    }
    for (Item& item : items)
    {
      // Prefer the centre of the hit area when it still hits the widget.
      const ImVec2 centre((item.min.x + item.max.x) * 0.5f,
                          (item.min.y + item.max.y) * 0.5f);
      mouseTo(centre);
      uiFrame();
      if (GImGui->HoveredId == item.id) item.point = centre;
      item.label = labelOf(item);
    }
    parkMouse();
    frame();
    return items;
  }

  /** Resolves the ID path of a widget with ImGui's ID stack query. */
  std::string labelOf(const Item& item)
  {
    mouseTo(item.point);
    ImGuiContext& context = *GImGui;
    for (int attempt = 0; attempt < 40; ++attempt)
    {
      context.DebugIDStackTool.LastActiveFrame = context.FrameCount;
      uiFrame();
      const ImGuiDebugItemPathQuery& query = context.DebugItemPathQuery;
      if (query.MainID == item.id && query.Complete) break;
    }
    context.DebugIDStackTool.LastActiveFrame = -1;
    const ImGuiDebugItemPathQuery& query = context.DebugItemPathQuery;
    if (query.MainID != item.id) return "?";
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
    return path;
  }

  static const Item* find(const std::vector<Item>& items,
                          std::string_view needle)
  {
    for (const Item& item : items)
      if (item.label.contains(needle)) return &item;
    return nullptr;
  }

  bool clickLabel(const std::vector<Item>& items, std::string_view needle)
  {
    const Item* item = find(items, needle);
    if (item == nullptr)
    {
      log.line("- could not find a widget labelled `{}` during {}", needle,
               step);
      return false;
    }
    click(item->point);
    return true;
  }

  // ---- Screen timing table ----
  struct Timing
  {
    std::string screen;
    Stats full;
    Stats ui;
  };
  std::vector<Timing> timings;
  int imgui_error_frames = 0;

  GUIView& view;
  GameController& controller;
  Report& log;

 private:
  int frames_since_flush = 0;
};

// ---- World invariants ------------------------------------------------------

struct WorldCheck
{
  int membership_errors = 0;
  int ledger_errors = 0;
  int table_errors = 0;
  size_t min_squad = SIZE_MAX;
  size_t max_squad = 0;
  int min_age = 100;
  int max_age = 0;
};

WorldCheck checkWorld(const GameController& controller,
                      const std::string& stage, Report& log)
{
  WorldCheck result;
  const auto data = controller.getGameData();
  constexpr int MAX_REPORTED = 8;

  std::unordered_map<PlayerID, TeamID> owner;
  for (const auto& [teamId, team] : data->getTeams())
  {
    if (teamId == FREE_AGENTS_TEAM_ID) continue;
    result.min_squad = std::min(result.min_squad, team.getPlayerIDs().size());
    result.max_squad = std::max(result.max_squad, team.getPlayerIDs().size());
    // Club players: the senior squad plus the academy.
    std::vector<PlayerID> members = team.getPlayerIDs();
    members.insert(members.end(), team.getAcademyIDs().begin(),
                   team.getAcademyIDs().end());
    for (const PlayerID playerId : members)
    {
      const auto [previous, inserted] = owner.emplace(playerId, teamId);
      const auto player = data->getPlayer(playerId);
      std::string problem;
      if (!inserted)
        problem = std::format("player {} listed by clubs {} and {}", playerId,
                              previous->second, teamId);
      else if (!player)
        problem =
            std::format("club {} lists unknown player {}", teamId, playerId);
      else if (player->get().getTeamId() != teamId)
        problem = std::format("club {} lists player {} whose club is {}",
                              teamId, playerId, player->get().getTeamId());
      if (problem.empty()) continue;
      if (result.membership_errors++ < MAX_REPORTED)
      {
        ADD_FAILURE() << stage << ": " << problem;
        log.line("- **membership** ({}): {}", stage, problem);
      }
    }
  }
  for (const auto& playerRef : data->getPlayersVector())
  {
    const Player& player = playerRef.get();
    result.min_age = std::min(result.min_age, player.getAge());
    result.max_age = std::max(result.max_age, player.getAge());
    if (player.getTeamId() == FREE_AGENTS_TEAM_ID ||
        owner.contains(player.getId()))
      continue;
    if (result.membership_errors++ < MAX_REPORTED)
    {
      ADD_FAILURE() << stage << ": player " << player.getId() << " of club "
                    << player.getTeamId() << " missing from its squad";
      log.line(
          "- **membership** ({}): player {} of club {} missing from "
          "the squad list",
          stage, player.getId(), player.getTeamId());
    }
  }

  for (const auto& [teamId, team] : data->getTeams())
  {
    if (teamId == FREE_AGENTS_TEAM_ID) continue;
    const auto& ledger = controller.getFinanceLedger(teamId);
    const int64_t sum =
        std::accumulate(ledger.begin(), ledger.end(), int64_t{0},
                        [](int64_t total, const FinanceTransaction& transaction)
                        { return total + transaction.amount; });
    const int64_t balance = team.getFinances().getBalance();
    if (sum == balance) continue;
    if (result.ledger_errors++ < MAX_REPORTED)
    {
      ADD_FAILURE() << stage << ": club " << teamId << " balance " << balance
                    << " != ledger sum " << sum;
      log.line("- **ledger** ({}): club {} balance {} != ledger sum {}", stage,
               teamId, balance, sum);
    }
  }

  for (const auto& leagueRef : controller.getLeagues())
  {
    const League& league = leagueRef.get();
    const auto table = controller.getStandings(league.getId());
    int won = 0;
    int lost = 0;
    int goalsFor = 0;
    int goalsAgainst = 0;
    int minPlayed = INT32_MAX;
    int maxPlayed = 0;
    for (const StandingRow& row : table)
    {
      won += row.won;
      lost += row.lost;
      goalsFor += row.goals_for;
      goalsAgainst += row.goals_against;
      minPlayed = std::min<int>(minPlayed, row.played);
      maxPlayed = std::max<int>(maxPlayed, row.played);
      std::string problem;
      if (row.points != 3 * row.won + row.drawn)
        problem = std::format("{} pts != 3W+D", row.points);
      else if (row.played != row.won + row.drawn + row.lost)
        problem = "played != W+D+L";
      else if (row.goal_difference != row.goals_for - row.goals_against)
        problem = "GD != GF-GA";
      else if (row.played > 2 * (table.size() - 1))
        problem = "played more than a double round robin";
      if (problem.empty()) continue;
      if (result.table_errors++ < MAX_REPORTED)
      {
        ADD_FAILURE() << stage << ": " << league.getName() << " team "
                      << row.team_id << " " << problem;
        log.line("- **table** ({}): {} team {} {}", stage, league.getName(),
                 row.team_id, problem);
      }
    }
    if (won != lost || goalsFor != goalsAgainst || (maxPlayed - minPlayed) > 2)
    {
      if (result.table_errors++ < MAX_REPORTED)
      {
        ADD_FAILURE() << stage << ": " << league.getName()
                      << " table totals inconsistent (W " << won << " L "
                      << lost << " GF " << goalsFor << " GA " << goalsAgainst
                      << " played " << minPlayed << ".." << maxPlayed << ")";
        log.line("- **table** ({}): {} totals W{} L{} GF{} GA{} played {}..{}",
                 stage, league.getName(), won, lost, goalsFor, goalsAgainst,
                 minPlayed, maxPlayed);
      }
    }
  }
  EXPECT_GE(result.min_squad, 16u) << stage;
  EXPECT_LE(result.max_squad, 45u) << stage;
  EXPECT_GE(result.min_age, 15) << stage;
  EXPECT_LE(result.max_age, 42) << stage;
  log.line(
      "- invariants ({}): membership errors {}, ledger errors {}, table "
      "errors {}, squad sizes {}..{}, ages {}..{}",
      stage, result.membership_errors, result.ledger_errors,
      result.table_errors, result.min_squad, result.max_squad, result.min_age,
      result.max_age);
  return result;
}

/** Result shape of every played league match (realism check). */
void logResultRealism(const GameController& controller,
                      const std::string& stage, Report& log)
{
  struct Totals
  {
    int matches = 0;
    int goals = 0;
    int home = 0;
    int draws = 0;
    int away = 0;
    int goalless = 0;
    int max_goals = 0;
    std::map<std::string, int> scores;
  };
  Totals all;
  Totals cup;
  for (const auto& [date, matches] :
       controller.getGame()->getCalendar().getFullCalendar())
  {
    for (const Match& match : matches)
    {
      if (!match.isPlayed() || match.getMatchType() == MatchType::FRIENDLY)
        continue;
      Totals& totals = match.getMatchType() == MatchType::CUP ? cup : all;
      const int home = match.getHomeScore();
      const int away = match.getAwayScore();
      ++totals.matches;
      totals.goals += home + away;
      if (home > away) ++totals.home;
      if (home == away) ++totals.draws;
      if (home < away) ++totals.away;
      if (home + away == 0) ++totals.goalless;
      totals.max_goals = std::max(totals.max_goals, home + away);
      ++totals.scores[std::format("{}-{}", home, away)];
    }
  }
  const auto describe = [&log, &stage](const char* name, const Totals& totals)
  {
    if (totals.matches == 0) return;
    const double count = totals.matches;
    std::vector<std::pair<int, std::string>> common;
    for (const auto& [score, times] : totals.scores)
      common.emplace_back(times, score);
    std::ranges::sort(common, std::greater<>());
    std::string top;
    for (size_t index = 0; index < std::min<size_t>(6, common.size()); ++index)
      top += std::format("{} ({:.1f}%) ", common[index].second,
                         100.0 * common[index].first / count);
    log.line(
        "- {} results ({}): {} matches, {:.2f} goals/match, home {:.1f}% "
        "draw {:.1f}% away {:.1f}%, 0-0 {:.1f}%, max goals {}, most "
        "common: {}",
        name, stage, totals.matches, totals.goals / count,
        100.0 * totals.home / count, 100.0 * totals.draws / count,
        100.0 * totals.away / count, 100.0 * totals.goalless / count,
        totals.max_goals, top);
  };
  describe("League", all);
  describe("Cup", cup);
}

void logTable(const GameController& controller, LeagueID league,
              const std::string& stage, Report& log)
{
  const auto table = controller.getStandings(league);
  const auto leagueRef = controller.getLeagueById(league);
  log.line("\n#### {} table ({})\n",
           leagueRef ? leagueRef->get().getName() : "?", stage);
  log.line("| # | Club | P | W | D | L | GF | GA | Pts |");
  log.line("|---|---|---|---|---|---|---|---|---|");
  for (const StandingRow& row : table)
  {
    const auto team = controller.getTeamById(row.team_id);
    log.line("| {} | {} | {} | {} | {} | {} | {} | {} | {} |", row.position,
             team ? team->get().getName() : "?", row.played, row.won, row.drawn,
             row.lost, row.goals_for, row.goals_against, row.points);
  }
  log.line("");
}

double averageTopOverall(const GameController& controller, TeamID team,
                         size_t count = 16)
{
  std::vector<double> overalls;
  for (const auto& player : controller.getPlayersForTeam(team))
    overalls.push_back(player.get().getOverall(controller.getStatsConfig()));
  std::ranges::sort(overalls, std::greater<>());
  overalls.resize(std::min(overalls.size(), count));
  if (overalls.empty()) return 0.0;
  return std::accumulate(overalls.begin(), overalls.end(), 0.0) /
         static_cast<double>(overalls.size());
}

const char* sectionName(NavSection section)
{
  using enum NavSection;
  switch (section)
  {
    case HOME:
      return "home";
    case INBOX:
      return "inbox";
    case CLUB:
      return "club";
    case SQUAD:
      return "squad";
    case LINEUP:
      return "lineup";
    case TACTICS:
      return "tactics";
    case FIXTURES:
      return "fixtures";
    case STANDINGS:
      return "standings";
    case TRANSFERS:
      return "transfers";
    case FINANCES:
      return "finances";
    case SCOUTING:
      return "scouting";
    case TRAINING:
      return "training";
    case STAFF:
      return "staff";
    case YOUTH:
      return "youth";
    case MANAGER:
      return "manager";
    case MEDICAL:
      return "medical";
    case CALENDAR:
      return "calendar";
    case SQUAD_PLANNER:
      return "squad_planner";
    case COMPARE:
      return "compare";
    case DELEGATION:
      return "delegation";
    case DATA_HUB:
      return "data_hub";
    case OPPOSITION:
      return "opposition";
    case INTERNATIONAL:
      return "international";
    case AWARDS:
      return "awards";
    case RECORDS:
      return "records";
    case PLANNING:
      return "planning";
    case RESERVES:
      return "reserves";
    case CALL_UPS:
      return "call_ups";
    case NEWS:
      return "news";
    case TIMELINE:
      return "timeline";
    case NONE:
      return "none";
  }
  return "?";
}

SceneID sectionScene(NavSection section)
{
  switch (section)
  {
    case NavSection::HOME:
    case NavSection::FINANCES:
      return SceneID::GAME_MENU;
    case NavSection::INBOX:
      return SceneID::INBOX;
    case NavSection::CLUB:
      return SceneID::CLUB;
    case NavSection::SQUAD:
      return SceneID::ROSTER;
    case NavSection::LINEUP:
      return SceneID::LINEUP;
    case NavSection::TACTICS:
      return SceneID::STRATEGY;
    case NavSection::FIXTURES:
      return SceneID::FIXTURES;
    case NavSection::STANDINGS:
      return SceneID::STANDINGS;
    case NavSection::TRANSFERS:
      return SceneID::TRANSFER_MARKET;
    case NavSection::SCOUTING:
      return SceneID::SCOUTING;
    case NavSection::TRAINING:
      return SceneID::TRAINING;
    case NavSection::STAFF:
      return SceneID::STAFF;
    case NavSection::YOUTH:
      return SceneID::YOUTH;
    case NavSection::MANAGER:
      return SceneID::MANAGER;
    case NavSection::MEDICAL:
      return SceneID::MEDICAL;
    case NavSection::CALENDAR:
      return SceneID::CALENDAR;
    case NavSection::SQUAD_PLANNER:
      return SceneID::SQUAD_PLANNER;
    case NavSection::COMPARE:
      return SceneID::PLAYER_COMPARE;
    case NavSection::DELEGATION:
      return SceneID::DELEGATION;
    case NavSection::DATA_HUB:
      return SceneID::DATA_HUB;
    case NavSection::OPPOSITION:
      return SceneID::OPPOSITION;
    case NavSection::INTERNATIONAL:
      return SceneID::INTERNATIONAL;
    case NavSection::AWARDS:
      return SceneID::AWARDS;
    case NavSection::RECORDS:
      return SceneID::RECORDS;
    case NavSection::PLANNING:
      return SceneID::PLANNING;
    case NavSection::RESERVES:
      return SceneID::RESERVES;
    case NavSection::CALL_UPS:
      return SceneID::CALL_UPS;
    case NavSection::NEWS:
      return SceneID::NEWS;
    case NavSection::TIMELINE:
      return SceneID::TIMELINE;
    case NavSection::NONE:
      break;
  }
  return SceneID::GAME_MENU;
}

constexpr std::array<NavSection, 27> ALL_SECTIONS = {
    NavSection::HOME,          NavSection::INBOX,
    NavSection::CLUB,          NavSection::SQUAD,
    NavSection::LINEUP,        NavSection::TACTICS,
    NavSection::TRAINING,      NavSection::YOUTH,
    NavSection::FIXTURES,      NavSection::STANDINGS,
    NavSection::TRANSFERS,     NavSection::SCOUTING,
    NavSection::STAFF,         NavSection::FINANCES,
    NavSection::MANAGER,       NavSection::MEDICAL,
    NavSection::CALENDAR,      NavSection::SQUAD_PLANNER,
    NavSection::COMPARE,       NavSection::DELEGATION,
    NavSection::DATA_HUB,      NavSection::OPPOSITION,
    NavSection::INTERNATIONAL, NavSection::AWARDS,
    NavSection::RECORDS,       NavSection::PLANNING,
    NavSection::RESERVES};

void openSection(Tester& player, NavSection section)
{
  player.step = std::string("open ") + sectionName(section);
  Navigation::open(&player.view, section);
  player.frames(3);
  EXPECT_EQ(player.activeId(), sectionScene(section)) << player.step;
  EXPECT_LE(player.view.getOverlayDepth(), 1u) << player.step;
}

void tour(Tester& player, const std::string& prefix, bool timed)
{
  for (const NavSection section : ALL_SECTIONS)
  {
    openSection(player, section);
    if (timed) player.measure(sectionName(section));
    player.shot(prefix + sectionName(section));
  }
  openSection(player, NavSection::HOME);
}

/** Everything a live managed match needs, from kick-off to Finish Match. */
/** How a managed match is played through MatchScene. */
enum class MatchPlay
{
  SHOWCASE, /**< Live, with presentation shots at set minutes. */
  LIVE,     /**< Live highlights to full time, resumed at every break. */
  QUICK,    /**< The Quick result button right after kick-off. */
};

/** Match controls found once and clicked where they are afterwards. */
struct MatchControls
{
  /** Pause, Resume and Finish share the first slot. */
  std::optional<ImVec2> first_slot;
  std::optional<ImVec2> quick_result;
};

struct LiveMatchResult
{
  bool played = false;
  int home_goals = 0;
  int away_goals = 0;
  double simulate_ms = 0.0;
  int gates = 0;
  std::string summary;
};

}  // namespace

// ---------------------------------------------------------------------------

class PlaytestJourney : public ::testing::Test
{
 protected:
  static void SetUpTestSuite()
  {
    Logger::init();
    fs::create_directories(shotDir());
  }
};

/** True while any popup (a matchday dialog) is open. */
bool anyPopupOpen()
{
  return ImGui::IsPopupOpen(
      "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

/**
 * Closes the matchday dialogs with Escape (saying nothing), like a manager
 * who wants to get on with the match. Two frames per round: a talk that
 * lapses closes on the first and the next talk opens on the second.
 */
void dismissMatchDialogs(Tester& player)
{
  for (int attempt = 0; attempt < 4; ++attempt)
  {
    player.frames(2);
    if (!anyPopupOpen()) return;
    player.key(ImGuiKey_Escape);
  }
  EXPECT_FALSE(anyPopupOpen()) << "a matchday dialog would not close";
}

/** Finds a match control by its label in the top of the screen. */
std::optional<ImVec2> findMatchControl(Tester& player, const char* label)
{
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  auto items = player.discover({0.0f, 0.0f}, {display.x, display.y * 0.3f});
  const Item* found = Tester::find(items, label);
  if (found == nullptr)
  {
    items = player.discover();
    found = Tester::find(items, label);
  }
  return found != nullptr ? std::optional<ImVec2>(found->point) : std::nullopt;
}

/**
 * Plays one managed match through MatchScene with the real match controls:
 * Resume at every break and Finish at full time, or Quick result right
 * after kick-off. Presentation shots only for MatchPlay::SHOWCASE. Live
 * matches play highlights at the fastest speed, as a hurried player would.
 */
LiveMatchResult playLiveMatch(Tester& player, MatchPlay mode,
                              MatchControls& controls)
{
  const bool showcase = mode == MatchPlay::SHOWCASE;
  LiveMatchResult result;
  GameController& controller = player.controller;
  auto* hub = dynamic_cast<MainGameScene*>(player.view.getBaseScene());
  if (hub == nullptr) return result;
  player.step = "kick-off";
  const std::string label = hub->continueLabel();
  const SceneID before = player.activeId();
  const bool advancing = hub->isContinuing();
  hub->requestContinue();
  player.frames(2);
  auto* match = dynamic_cast<MatchScene*>(player.active());
  const auto next = controller.getNextManagedFixture();
  EXPECT_NE(match, nullptr)
      << "PLAY MATCH did not open the match: label '" << label
      << "', scene before " << static_cast<int>(std::to_underlying(before))
      << " after " << static_cast<int>(std::to_underlying(player.activeId()))
      << ", advancing " << advancing << ", overlays "
      << player.view.getOverlayDepth() << ", next fixture "
      << (next ? dateText(next->date) : std::string("none")) << " type "
      << (next ? static_cast<int>(std::to_underlying(next->type)) : -1)
      << ", today " << dateText(controller.getCurrentDate());
  if (match == nullptr) return result;
  if (Bridge::engine(*match) == nullptr)
  {
    // Lineup gate: an injured or suspended player blocks kick-off.
    ++result.gates;
    if (static bool gateShot = false; !gateShot)
    {
      player.shot("j30_match_lineup_gate");
      gateShot = true;
    }
    player.step = "lineup gate auto-fix";
    const auto gateItems = player.discover();
    player.clickLabel(gateItems, LOC("MATCH_LINEUP_AUTOFIX"));
    player.frames(2);
  }
  MatchEngine* engine = Bridge::engine(*match);
  EXPECT_NE(engine, nullptr) << "match did not start";
  if (engine == nullptr) return result;
  const GameDateValue matchDate = controller.getCurrentDate();
  const auto [homeId, awayId] = Bridge::teams(*match);

  if (showcase)
  {
    Bridge::setView(*match, MatchViewMode::PITCH_2D);
    player.frames(2);
    player.shot("j30_match_kickoff_2d");
    player.measure("match 2D (1x)");
  }
  else
  {
    Bridge::setView(*match, MatchViewMode::PITCH_2D);
  }

  const auto simulateStart = Clock::now();
  if (mode == MatchPlay::QUICK)
  {
    // The pre-match talk opens at kick-off: skipped, then Quick result.
    player.step = "quick result";
    Bridge::setPaused(*match, true);
    dismissMatchDialogs(player);
    if (!controls.quick_result)
      controls.quick_result =
          findMatchControl(player, LOC("MATCH_QUICK_RESULT"));
    EXPECT_TRUE(controls.quick_result.has_value())
        << "Quick result button not found";
    if (!controls.quick_result) return result;
    // The scene closes by itself once the result is in: nothing of it is
    // touched after this click.
    player.click(*controls.quick_result);
    match = nullptr;
    engine = nullptr;
  }
  else
  {
    Bridge::setPlayback(*match, MatchSceneTuning::Controls::SPEED_STEPS.back(),
                        true);
    // The match stops at half-time (and the other breaks) until the manager
    // presses Resume, like a real user does here.
    const auto resumeAfterBreak = [&]
    {
      // The half-time talk opens first (and an unanswered pre-match talk
      // lapses): the manager closes it, then resumes.
      dismissMatchDialogs(player);
      if (!controls.first_slot)
        controls.first_slot = findMatchControl(player, LOC("MATCH_RESUME"));
      EXPECT_TRUE(controls.first_slot.has_value())
          << "Resume button not found at a break";
      if (!controls.first_slot) return false;
      player.click(*controls.first_slot);
      player.frames(1);
      EXPECT_FALSE(Bridge::paused(*match))
          << "Resume did not restart play (state "
          << std::to_underlying(engine->getState()) << ", minute "
          << engine->getMatchTimeMinutes() << ", popup open " << anyPopupOpen()
          << ")";
      return !Bridge::paused(*match);
    };
    const auto runUntil = [&](float minute)
    {
      const auto begun = Clock::now();
      while (!Bridge::finished(*match) &&
             engine->getMatchTimeMinutes() < minute &&
             Clock::now() - begun < std::chrono::seconds(60))
      {
        if (Bridge::paused(*match) && !resumeAfterBreak()) break;
        Bridge::update(player.view, 0.1f);
      }
    };

    if (showcase)
    {
      runUntil(28.0f);
      player.frame();
      player.shot("j31_match_2d_28min");
      Bridge::showSubstitutions(*match, true);
      Bridge::setPaused(*match, true);
      player.frames(3);
      player.shot("j32_match_substitutions");
      Bridge::showSubstitutions(*match, false);
      Bridge::setPaused(*match, false);
      player.frames(2);
      Bridge::setView(*match, MatchViewMode::BROADCAST_3D);
      player.frames(3);
      player.shot("j33_match_3d_broadcast");
      player.measure("match 3D broadcast");
      runUntil(40.0f);
      Bridge::setCamera(*match, MatchCameraMode::TACTICAL);
      player.frames(3);
      player.shot("j34_match_3d_tactical");
      Bridge::setCamera(*match, MatchCameraMode::END);
      player.frames(3);
      player.shot("j35_match_3d_end");
      Bridge::setCamera(*match, MatchCameraMode::PLAYER_FOLLOW);
      Bridge::showNames(*match, true);
      player.frames(3);
      player.shot("j36_match_3d_follow_names");
      Bridge::showNames(*match, false);
      Bridge::setCamera(*match, MatchCameraMode::BROADCAST);
      int guard = 0;
      while (!Bridge::finished(*match) &&
             engine->getState() != MatchState::HALF_TIME)
      {
        if (guard++ >= 20000) break;
        Bridge::update(player.view, 0.1f);
      }
      player.frames(2);
      player.shot("j37_match_half_time");
      runUntil(70.0f);
      Bridge::setView(*match, MatchViewMode::PITCH_2D);
      player.frames(2);
      player.shot("j38_match_2d_70min");
    }
    runUntil(1000.0f);
    EXPECT_TRUE(Bridge::finished(*match)) << "match never reached full time";
    player.frames(2);
    if (showcase) player.shot("j39_match_full_time");

    result.home_goals = engine->getHomeScore();
    result.away_goals = engine->getAwayScore();
    const MatchStats& stats = engine->getStats();
    result.summary = std::format(
        "shots {}-{} (on target {}-{}), xG {:.2f}-{:.2f}, possession "
        "{:.0f}-{:.0f}%, passes {}/{} - {}/{}, events {}",
        stats.homeShots, stats.awayShots, stats.homeOnTarget,
        stats.awayOnTarget, stats.homeShotXG, stats.awayShotXG,
        stats.homePossession, stats.awayPossession, stats.homePassesCompleted,
        stats.homePassesAttempted, stats.awayPassesCompleted,
        stats.awayPassesAttempted, engine->getEvents().size());

    // The real Finish button of the match controls.
    player.step = "finish match";
    if (!controls.first_slot)
      controls.first_slot = findMatchControl(player, LOC("MATCH_FINISH"));
    EXPECT_TRUE(controls.first_slot.has_value()) << "Finish button not found";
    if (!controls.first_slot) return result;
    player.click(*controls.first_slot);
    player.frames(2);
  }
  // Finish (or the quick result) returns to the hub, which simulates the
  // rest of the day on its Continue worker and then shows the match report.
  const auto finishStart = Clock::now();
  while ((player.activeId() != SceneID::MATCH_REPORT || hub->isContinuing()) &&
         Clock::now() - finishStart < LOAD_DEADLINE)
    player.frame();
  result.simulate_ms = millisecondsSince(simulateStart);
  player.frames(2);
  EXPECT_FALSE(hub->isContinuing()) << "the rest of the match day never ended";
  // Back (Escape) from the report returns to the club hub.
  EXPECT_EQ(player.activeId(), SceneID::MATCH_REPORT)
      << "Finish Match did not show the match report";
  if (showcase) player.shot("j39_match_report_after_finish");
  if (mode == MatchPlay::QUICK)
  {
    const Match* played = controller.getGame()->getCalendar().findMatch(
        matchDate, homeId, awayId);
    EXPECT_TRUE(played != nullptr && played->isPlayed())
        << "Quick result did not record the match";
    if (played != nullptr)
    {
      result.home_goals = played->getHomeScore();
      result.away_goals = played->getAwayScore();
    }
    result.summary = "quick result";
  }
  player.step = "close match report";
  player.key(ImGuiKey_Escape);
  player.frames(2);
  EXPECT_EQ(player.activeId(), SceneID::GAME_MENU)
      << "Back from the match report did not return to the club hub";
  EXPECT_FALSE(controller.getCurrentDate() == matchDate)
      << "Finish Match did not advance the day";
  result.played = player.activeId() == SceneID::GAME_MENU;
  return result;
}

TEST_F(PlaytestJourney, NewCareerThroughTheGui)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  for (const auto& entry : fs::directory_iterator(shotDir()))
    if (entry.path().filename().string().starts_with("j"))
      fs::remove(entry.path());
  Report log(outputDir() / "journey_log.md");
  Report metrics(outputDir() / "metrics.md");
  log.line("# Playtest journey log\n");

  GameController controller;
  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  Tester player(view, controller, log);

  // ---- 1. First impression: main menu, new game ---------------------------
  player.step = "main menu";
  player.frames(3);
  int windowWidth = 0;
  int windowHeight = 0;
  SDL_GetWindowSize(view.getWindow(), &windowWidth, &windowHeight);
  log.line("- window {}x{}, ImGui display {}x{}", windowWidth, windowHeight,
           ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
  player.measure("main menu");
  player.shot("j01_main_menu");
  auto items = player.discover();
  log.line("- main menu widgets: {}", items.size());
  for (const Item& item : items) log.line("  - `{}`", item.label);
  ASSERT_TRUE(player.clickLabel(items, "New Game"));
  player.frames(3);
  player.shot("j02_new_game_slots");
  items = player.discover();
  for (const Item& item : items) log.line("  - slot dialog: `{}`", item.label);
  ASSERT_TRUE(player.clickLabel(items, "1/##slot"));

  player.step = "new game loading";
  const auto loadStart = Clock::now();
  bool loadingShot = false;
  while (player.activeId() != SceneID::TEAM_SELECTION &&
         Clock::now() - loadStart < LOAD_DEADLINE)
  {
    player.frame();
    if (!loadingShot)
    {
      player.shot("j03_new_game_loading");
      loadingShot = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  const double newGameMs = millisecondsSince(loadStart);
  ASSERT_EQ(player.activeId(), SceneID::TEAM_SELECTION)
      << "new game did not reach the club choice";
  log.line("- new game (world generation) took {:.0f} ms, world seed {}",
           newGameMs, controller.getWorldSeed());

  // ---- 2. Club choice: a mid-table club of the league shown first ---------
  player.step = "team selection";
  player.frames(3);
  player.measure("team selection");
  player.shot("j04_team_selection");
  items = player.discover();
  std::vector<std::pair<double, const Team*>> visibleClubs;
  for (const auto& teamRef : controller.getTeams())
  {
    const Team& team = teamRef.get();
    const bool visible = std::ranges::any_of(
        items,
        [&team](const Item& item)
        {
          return item.label.ends_with("/" + team.getName()) ||
                 item.label == team.getName();
        });
    if (visible)
      visibleClubs.emplace_back(averageTopOverall(controller, team.getId()),
                                &team);
  }
  ASSERT_FALSE(visibleClubs.empty()) << "no club rows found on the screen";
  std::ranges::sort(visibleClubs, std::greater<>());
  const Team* chosen = visibleClubs[visibleClubs.size() / 2].second;
  const TeamID managedId = chosen->getId();
  const LeagueID leagueId = chosen->getLeagueId();
  log.line(
      "- {} clubs visible; picked {} (rank {} of {} by squad quality, "
      "avg top-16 overall {:.1f}; best {:.1f}, worst {:.1f})",
      visibleClubs.size(), chosen->getName(), visibleClubs.size() / 2 + 1,
      visibleClubs.size(), visibleClubs[visibleClubs.size() / 2].first,
      visibleClubs.front().first, visibleClubs.back().first);
  ASSERT_TRUE(player.clickLabel(items, "/" + chosen->getName()));
  player.frames(2);
  player.shot("j05_team_selected");
  items = player.discover();
  ASSERT_TRUE(player.clickLabel(items, "Start career"));
  player.frames(4);
  ASSERT_TRUE(controller.hasSelectedTeam());
  ASSERT_EQ(controller.getManagedTeam()->get().getId(), managedId);
  EXPECT_EQ(player.activeId(), SceneID::GAME_MENU);
  auto* hub = dynamic_cast<MainGameScene*>(view.getBaseScene());
  ASSERT_NE(hub, nullptr);
  player.shot("j06_home_day1");
  log.line(
      "- day 1: {} ; Continue label `{}`; balance {}; weekly wages {}; "
      "squad {}; unread inbox {}",
      dateText(controller.getCurrentDate()), hub->continueLabel(),
      chosen->getFinances().getBalance(),
      controller.getWeeklyWageBill(managedId),
      controller.getPlayersForTeam(managedId).size(),
      controller.getUnreadInboxCount());
  checkWorld(controller, "day 1", log);

  // ---- 3. Every screen on day 1, timed -------------------------------------
  tour(player, "j1_", true);

  // Keyboard shortcuts and the command palette.
  player.step = "shortcuts";
  player.key(ImGuiKey_F3);
  player.frames(2);
  EXPECT_EQ(player.activeId(), SceneID::ROSTER) << "F3 should open Squad";
  player.key(ImGuiKey_F5);
  player.frames(2);
  EXPECT_EQ(player.activeId(), SceneID::FIXTURES)
      << "F5 should open Matches (fixtures first)";
  player.key(ImGuiKey_F7);
  player.frames(2);
  EXPECT_EQ(player.activeId(), SceneID::CLUB)
      << "F7 should open Club (club and board first)";
  player.key(ImGuiKey_F1);
  player.frames(2);
  EXPECT_EQ(player.activeId(), SceneID::GAME_MENU) << "F1 should go Home";
  if (auto* shell = dynamic_cast<ManagementScene*>(player.active()))
  {
    player.step = "palette";
    Bridge::openPalette(*shell);
    player.frames(2);
    for (const char character : chosen->getName().substr(0, 3))
      ImGui::GetIO().AddInputCharacter(static_cast<unsigned>(character));
    player.frames(3);
    EXPECT_GT(Bridge::paletteMatches(*shell), 0u);
    player.shot("j20_palette");
    player.key(ImGuiKey_Escape);
    player.frames(2);
  }

  // Best player's profile and its dialogs; a rival's squad.
  const auto squadRefs = controller.getPlayersForTeam(managedId);
  const Player* star = &squadRefs.front().get();
  for (const auto& ref : squadRefs)
    if (ref.get().getOverall(controller.getStatsConfig()) >
        star->getOverall(controller.getStatsConfig()))
      star = &ref.get();
  openSection(player, NavSection::SQUAD);
  player.step = "profile";
  Navigation::openPlayer(&view, star->getId());
  player.frames(3);
  EXPECT_EQ(player.activeId(), SceneID::PLAYER_PROFILE);
  player.measure("player profile");
  player.shot("j21_profile_star");
  if (auto* profile = dynamic_cast<PlayerProfileScene*>(player.active()))
  {
    Bridge::requestRenew(*profile);
    player.frames(3);
    player.shot("j22_profile_renew_dialog");
    player.key(ImGuiKey_Escape);
    Bridge::requestList(*profile);
    player.frames(3);
    player.shot("j23_profile_list_dialog");
    player.key(ImGuiKey_Escape);
  }
  const auto leagueTeams = controller.getTeamsInLeague(leagueId);
  const Team* rival = nullptr;
  for (const auto& team : leagueTeams)
    if (team.get().getId() != managedId &&
        (rival == nullptr || averageTopOverall(controller, team.get().getId()) >
                                 averageTopOverall(controller, rival->getId())))
      rival = &team.get();
  if (rival != nullptr)
  {
    Navigation::openClub(&view, rival->getId());
    player.frames(3);
    player.shot("j24_rival_squad");
  }

  // ---- 4. Tactics: 4-3-3 with the best XI and a pressing plan ------------
  player.step = "tactics";
  {
    Team& club = controller.getManagedTeam()->get();
    std::vector<const Player*> squad;
    for (const auto& ref : controller.getPlayersForTeam(managedId))
      squad.push_back(&ref.get());
    Formation::autoPick(club.getLineup(), Formation::PRESETS[1], squad,
                        controller.getStatsConfig());
    EXPECT_EQ(Formation::detectPreset(club.getLineup()), 1);
    club.getStrategy().setPressing(0.75f);
    club.getStrategy().setOffensiveBias(0.65f);
    club.getStrategy().setWidthUsage(0.7f);
  }
  openSection(player, NavSection::LINEUP);
  player.shot("j25_lineup_433");
  openSection(player, NavSection::TACTICS);
  player.shot("j26_tactics_pressing");

  // ---- 5. Inbox ------------------------------------------------------------
  openSection(player, NavSection::INBOX);
  if (auto* inbox = dynamic_cast<InboxScene*>(player.active()))
  {
    Bridge::openInboxMessage(*inbox, 0);
    player.frames(2);
    player.shot("j27_inbox_message");
  }
  log.line("- inbox on day 1: {} messages", controller.getInbox().size());
  for (const InboxMessage& message : controller.getInbox())
    log.line("  - {} | {}", message.formatTitle(),
             message.formatBody().substr(0, 160));

  // ---- 6. Scouting ---------------------------------------------------------
  player.step = "scouting";
  const auto& scouts = controller.getScouts();
  log.line("- scouts: {}", scouts.size());
  LeagueID scoutedLeague = 0;
  for (const auto& league : controller.getLeagues())
    if (league.get().getId() != leagueId &&
        controller.getLeagueTier(league.get().getId()) == 1)
    {
      scoutedLeague = league.get().getId();
      break;
    }
  if (!scouts.empty() && scoutedLeague != 0)
  {
    const int64_t cost = controller.getScoutAssignmentCost(
        ScoutTargetKind::League, scoutedLeague, 28);
    const ScoutAssignError error = controller.startScoutAssignment(
        scouts.front().id, ScoutTargetKind::League, scoutedLeague, 28);
    EXPECT_EQ(error, ScoutAssignError::None);
    log.line("- sent scout {} to league {} for 28 days, cost {}, error {}",
             scouts.front().name, scoutedLeague, cost,
             static_cast<int>(std::to_underlying(error)));
  }
  for (const auto tab :
       {ScoutingScene::Tab::OVERVIEW, ScoutingScene::Tab::SEARCH,
        ScoutingScene::Tab::SHORTLIST, ScoutingScene::Tab::FOCUS})
  {
    view.navigateTo(std::make_unique<ScoutingScene>(&view, tab));
    player.frames(3);
    player.shot(std::format("j28_scouting_tab{}",
                            static_cast<int>(std::to_underlying(tab))));
  }

  // ---- 7. Transfer: offer, negotiation, contract --------------------------
  player.step = "transfer";
  const auto window = controller.getTransferWindow();
  const int64_t budget = controller.transferBudgetForTeam(managedId);
  const double squadLevel = averageTopOverall(controller, managedId, 11);
  log.line(
      "\n## Transfer\n\n- window open {} (days to deadline {}), transfer "
      "budget {}, XI level {:.1f}",
      window.open, window.days_to_deadline, budget, squadLevel);
  ScoutSearchFilter filter;
  filter.min_age = 20;
  filter.max_age = 29;
  filter.max_value = budget * 8 / 10;
  filter.limit = 400;
  const auto candidates = controller.searchScoutedPlayers(filter);
  // Best estimated player the budget can reach (a squad upgrade if any).
  std::optional<ScoutedPlayerRow> target;
  int affordable = 0;
  for (const ScoutedPlayerRow& row : candidates)
  {
    if (row.team_id == managedId || row.team_id == FREE_AGENTS_TEAM_ID ||
        row.estimated_value <= 0 || row.estimated_value > budget * 8 / 10 ||
        row.injured)
      continue;
    ++affordable;
    if (!target || row.overall > target->overall) target = row;
  }
  log.line(
      "- {} of the candidates are affordable; best estimate {:.1f} vs XI "
      "{:.1f}",
      affordable, target ? target->overall : 0.0f, squadLevel);
  log.line("- {} scouted candidates aged 20-29; affordable upgrade: {}",
           candidates.size(), target ? "yes" : "none");
  bool signedPlayer = false;
  if (target)
  {
    const Player& targetPlayer =
        controller.getGameData()->getPlayer(target->player_id)->get();
    const TeamID sellerId = target->team_id;
    log.line(
        "- target {} ({} , age {}, est. overall {:.1f}, true {:.1f}, "
        "est. value {}, wage {}, club {})",
        targetPlayer.getName(),
        static_cast<int>(std::to_underlying(targetPlayer.getRole())),
        targetPlayer.getAge(), target->overall,
        targetPlayer.getOverall(controller.getStatsConfig()),
        target->estimated_value, targetPlayer.getWage(),
        controller.getTeamById(sellerId)->get().getName());
    EXPECT_TRUE(controller.addToShortlist(target->player_id));
    if (!scouts.empty() && scouts.size() > 1)
      controller.startScoutAssignment(scouts[1].id, ScoutTargetKind::Player,
                                      target->player_id, 7);

    openSection(player, NavSection::TRANSFERS);
    player.measure("transfers");
    player.shot("j40_transfers_search");
    if (auto* market = dynamic_cast<TransferMarketScene*>(player.active()))
    {
      log.line("- transfer search rows: {}", Bridge::targetCount(*market));
      if (Bridge::openOfferDialog(*market, target->player_id))
      {
        player.frames(3);
        player.shot("j41_transfer_offer_dialog");
        player.key(ImGuiKey_Escape);
      }
      else
      {
        log.line("- target not in the default transfer search list");
      }
    }

    TransferNegotiation::OfferTerms terms;
    terms.fee = static_cast<uint32_t>(target->estimated_value);
    TransferNegotiation::ClubResponse response;
    for (int round = 0; round < 5; ++round)
    {
      response = controller.makeTransferOffer(target->player_id, terms);
      log.line(
          "  - offer round {}: fee {} -> decision {} (counter {}), {} "
          "reasons",
          round + 1, terms.fee,
          static_cast<int>(std::to_underlying(response.decision)),
          response.counter_fee, response.reasons.size());
      if (response.decision ==
          TransferNegotiation::ClubResponse::Decision::Accept)
        break;
      if (response.decision ==
              TransferNegotiation::ClubResponse::Decision::Counter &&
          response.counter_fee <= budget * 9 / 10)
        terms.fee = response.counter_fee;
      else
        terms.fee = static_cast<uint32_t>(terms.fee * 1.2);
    }
    if (response.decision ==
        TransferNegotiation::ClubResponse::Decision::Accept)
    {
      const auto kind = controller.getContractTalkKind(target->player_id);
      EXPECT_TRUE(kind.has_value()) << "accepted fee must open contract talks";
      if (kind)
      {
        const auto demand =
            controller.getPlayerDemand(target->player_id, *kind);
        log.line("  - player demands wage {}/wk, {}-{} years, bonus {}",
                 demand.weekly_wage, demand.min_years, demand.max_years,
                 demand.signing_bonus);
        openSection(player, NavSection::TRANSFERS);
        if (auto* market = dynamic_cast<TransferMarketScene*>(player.active()))
        {
          Bridge::openContractDialog(*market, target->player_id);
          player.frames(3);
          player.shot("j42_transfer_contract_dialog");
          player.key(ImGuiKey_Escape);
        }
        TransferNegotiation::ContractOffer offer;
        offer.weekly_wage = demand.weekly_wage * 85 / 100;
        offer.years =
            std::clamp<uint8_t>(3, demand.min_years, demand.max_years);
        offer.signing_bonus = demand.signing_bonus / 2;
        auto talk = controller.proposeContract(target->player_id, offer);
        log.line(
            "  - contract 85% wage: accepted {} completed {} over budget "
            "{} rounds left {}",
            talk.response.accepted, talk.completed, talk.over_budget,
            talk.rounds_left);
        if (!talk.completed)
        {
          offer.weekly_wage = demand.weekly_wage;
          offer.signing_bonus = demand.signing_bonus;
          talk = controller.proposeContract(target->player_id, offer);
          log.line(
              "  - contract at demand: accepted {} completed {} over "
              "budget {}",
              talk.response.accepted, talk.completed, talk.over_budget);
        }
        signedPlayer = talk.completed;
      }
    }
    if (signedPlayer)
    {
      const Player& signing =
          controller.getGameData()->getPlayer(target->player_id)->get();
      EXPECT_EQ(signing.getTeamId(), managedId);
      const auto& ids = controller.getManagedTeam()->get().getPlayerIDs();
      EXPECT_NE(std::ranges::find(ids, target->player_id), ids.end());
      const auto& sellerIds =
          controller.getTeamById(sellerId)->get().getPlayerIDs();
      EXPECT_EQ(std::ranges::find(sellerIds, target->player_id),
                sellerIds.end());
      const auto hasToday = [&](TeamID team, FinanceCategory category)
      {
        const auto& ledger = controller.getFinanceLedger(team);
        return std::ranges::any_of(ledger,
                                   [&](const FinanceTransaction& transaction)
                                   {
                                     return transaction.category == category &&
                                            transaction.date ==
                                                controller.getCurrentDate();
                                   });
      };
      EXPECT_TRUE(hasToday(managedId, FinanceCategory::TransferFeeOut));
      EXPECT_TRUE(hasToday(sellerId, FinanceCategory::TransferFeeIn));
      log.line("- signed {}: new wage {}, contract {} years; balance now {}",
               signing.getName(), signing.getWage(), signing.getContractYears(),
               controller.getManagedTeam()->get().getFinances().getBalance());
      openSection(player, NavSection::SQUAD);
      player.shot("j43_squad_after_signing");
    }
    checkWorld(controller, "after transfer", log);
  }

  // ---- 8. Training ---------------------------------------------------------
  player.step = "training";
  EXPECT_TRUE(controller.setTrainingPreset(TrainingPreset::Attacking));
  EXPECT_TRUE(controller.setTrainingIntensity(TrainingIntensity::High));
  {
    const Player* youngest =
        &controller.getPlayersForTeam(managedId).front().get();
    for (const auto& ref : controller.getPlayersForTeam(managedId))
      if (ref.get().getAge() < youngest->getAge()) youngest = &ref.get();
    EXPECT_TRUE(controller.setPlayerTrainingFocus(youngest->getId(),
                                                  TrainingFocus::Physical));
  }
  openSection(player, NavSection::TRAINING);
  player.shot("j44_training_attacking_high");

  // ---- 9. Continue to the first match and play it live -------------------
  metrics.line("# Playtest metrics\n");
  metrics.line("New game (world generation + load): {:.0f} ms\n", newGameMs);
  metrics.line("## Continue latency (GUI, async)\n");
  metrics.line("| From | To | Days | Wall ms | ms/day |");
  metrics.line("|---|---|---|---|---|");
  std::vector<double> perDay;
  int busyShots = 0;
  const auto guiContinue = [&]()
  {
    player.step = "continue";
    const GameDateValue from = controller.getCurrentDate();
    const auto start = Clock::now();
    hub->requestContinue();
    player.frame();
    // Until the days are simulated, counting the frames before the worker
    // starts (the hub first closes screens and captures its backdrop).
    while (hub->isContinuing() && Clock::now() - start < LOAD_DEADLINE)
    {
      if (busyShots == 0)
      {
        player.shot("j29_continue_busy");
        ++busyShots;
      }
      player.frame();
    }
    EXPECT_FALSE(hub->isContinuing()) << "Continue never finished";
    player.frames(2);
    const double wall = millisecondsSince(start);
    const int days = dayNumber(controller.getCurrentDate()) - dayNumber(from);
    if (days > 0) perDay.push_back(wall / days);
    metrics.line("| {} | {} | {} | {:.0f} | {:.1f} |", dateText(from),
                 dateText(controller.getCurrentDate()), days, wall,
                 days > 0 ? wall / days : 0.0);
    return days;
  };

  const auto isMatchDay = [&]()
  { return hub->continueLabel().contains("PLAY MATCH"); };
  int guard = 0;
  while (!isMatchDay())
  {
    if (guard++ >= 10) break;
    guiContinue();
  }
  ASSERT_TRUE(isMatchDay()) << "Continue never reached a managed match";
  player.shot("j29_home_matchday");

  MatchControls matchControls;
  log.line("\n## Managed matches\n");
  const auto recordMatch =
      [&](const LiveMatchResult& result, const GameDateValue& date)
  {
    for (const Match& match : controller.getTeamFixtures(managedId))
    {
      if (!(match.getDate() == date)) continue;
      const auto home = controller.getTeamById(match.getHomeTeamId());
      const auto away = controller.getTeamById(match.getAwayTeamId());
      const auto report = controller.getMatchReport(date, match.getHomeTeamId(),
                                                    match.getAwayTeamId());
      EXPECT_TRUE(match.isPlayed());
      EXPECT_EQ(match.getHomeScore(), result.home_goals);
      EXPECT_EQ(match.getAwayScore(), result.away_goals);
      log.line(
          "- {} [{}] {} {}-{} {} | live: {} | report: {} players, {} "
          "events, attendance {}, home shots {}",
          dateText(date),
          static_cast<int>(std::to_underlying(match.getMatchType())),
          home ? home->get().getName() : "?", match.getHomeScore(),
          match.getAwayScore(), away ? away->get().getName() : "?",
          result.summary, report ? report->players.size() : 0,
          report ? report->events.size() : 0, report ? report->attendance : 0,
          report ? report->home_stats.shots : 0);
      return;
    }
  };

  GameDateValue firstMatchDate = controller.getCurrentDate();
  auto firstResult = playLiveMatch(player, MatchPlay::SHOWCASE, matchControls);
  ASSERT_TRUE(firstResult.played);
  recordMatch(firstResult, firstMatchDate);
  // A live match should feed the report and top scorers like simulated ones.
  {
    const Match* played = nullptr;
    const auto fixtures = controller.getTeamFixtures(managedId);
    for (const Match& match : fixtures)
      if (match.getDate() == firstMatchDate) played = &match;
    if (played != nullptr)
    {
      const auto report = controller.getMatchReport(
          firstMatchDate, played->getHomeTeamId(), played->getAwayTeamId());
      EXPECT_TRUE(report.has_value()) << "no report for the live match";
      if (report)
      {
        EXPECT_FALSE(report->players.empty())
            << "live match report has no player lines";
        const int goals = played->getHomeScore() + played->getAwayScore();
        // Own goals are reported as their own kind and still count.
        const auto goalEvents = std::ranges::count_if(
            report->events,
            [](const MatchReportEvent& event)
            {
              return event.kind == MatchEventKind::GOAL ||
                     event.kind == MatchEventKind::OWN_GOAL;
            });
        EXPECT_EQ(goalEvents, goals)
            << "live match report lacks the scorers of its goals";
      }
      Navigation::openMatchReport(&view, firstMatchDate,
                                  played->getHomeTeamId(),
                                  played->getAwayTeamId());
      player.frames(3);
      player.shot("j45_match_report_live");
      openSection(player, NavSection::HOME);
    }
  }
  player.shot("j46_home_after_first_match");
  metrics.line(
      "\nLive match (30x highlights, first match incl. shots, to the "
      "report): {:.0f} ms\n",
      firstResult.simulate_ms);

  // ---- 10. Two months through the GUI --------------------------------------
  const int startDay = dayNumber(controller.getCurrentDate());
  // One more match live (breaks, Resume and Finish without the showcase's
  // extra frames); the others through Quick result, which is cheaper and
  // ends in the same match day and report.
  int liveMatches = 1;
  int quickMatches = 0;
  std::vector<double> liveSimulationMs;
  std::vector<double> quickMs;
  bool monthShot = false;
  while (dayNumber(controller.getCurrentDate()) - startDay < GUI_MONTHS_DAYS)
  {
    if (isMatchDay())
    {
      const GameDateValue date = controller.getCurrentDate();
      const bool live = liveMatches < 2;
      const auto result = playLiveMatch(
          player, live ? MatchPlay::LIVE : MatchPlay::QUICK, matchControls);
      if (!result.played) break;
      (live ? liveSimulationMs : quickMs).push_back(result.simulate_ms);
      ++(live ? liveMatches : quickMatches);
      if (result.gates > 0)
        log.line("- {}: lineup gate blocked kick-off (auto-fix used)",
                 dateText(date));
      recordMatch(result, date);
    }
    else if (guiContinue() <= 0)
    {
      log.line("- Continue advanced 0 days on {}",
               dateText(controller.getCurrentDate()));
      break;
    }
    if (!monthShot && dayNumber(controller.getCurrentDate()) - startDay >= 30)
    {
      monthShot = true;
      tour(player, "j5_month1_", false);
    }
  }
  const Stats liveStats = summarize(liveSimulationMs);
  const Stats quickStats = summarize(quickMs);
  metrics.line(
      "\nMatches played through the GUI: {} live (30x highlights, kick-off "
      "to report: median {:.0f} ms, worst {:.0f} ms), {} by Quick result "
      "(median {:.0f} ms, worst {:.0f} ms)\n",
      liveMatches, liveStats.median, liveStats.worst, quickMatches,
      quickStats.median, quickStats.worst);
  // Frame times were measured on day 1; perf_tests own the screen budgets.
  tour(player, "j6_month2_", false);
  if (auto* standings = dynamic_cast<StandingsScene*>(
          (openSection(player, NavSection::STANDINGS), player.active())))
  {
    Bridge::showCup(*standings);
    player.frames(3);
    player.shot("j6_month2_cup");
  }
  openSection(player, NavSection::HOME);
  log.line("\n## After two months ({})\n",
           dateText(controller.getCurrentDate()));
  checkWorld(controller, "two months", log);
  logResultRealism(controller, "two months", log);
  logTable(controller, leagueId, "two months", log);
  log.line(
      "- balance {} ; weekly wages {} ; last home attendance {} ; board "
      "confidence {} ; unread inbox {} ; scout reports {}",
      controller.getManagedTeam()->get().getFinances().getBalance(),
      controller.getWeeklyWageBill(managedId),
      controller.getLastHomeAttendance(managedId),
      controller.getBoardState().confidence, controller.getUnreadInboxCount(),
      controller.getScoutReports().size());
  EXPECT_FALSE(controller.getScoutReports().empty())
      << "a 28-day league assignment produced no scout reports";

  // ---- 11. Headless days (auto-simulated matches) --------------------------
  player.step = "season";
  const char* fullSeasonFlag = std::getenv("FM_PLAYTEST_FULL_SEASON");
  const bool wholeSeason = fullSeasonFlag != nullptr &&
                           *fullSeasonFlag != '\0' &&
                           std::string_view(fullSeasonFlag) != "0";
  const int season = controller.getCurrentSeason();
  const auto seasonStart = Clock::now();
  std::vector<double> dayMs;
  int simulatedDays = 0;
  const auto seasonOver = [&]()
  {
    const GameDateValue today = controller.getCurrentDate();
    return controller.getCurrentSeason() != season ||
           (today.month == 6 && today.day == 30);
  };
  const auto deadline = wholeSeason ? SEASON_DEADLINE : HEADLESS_DEADLINE;
  while (!seasonOver() && (wholeSeason || simulatedDays < HEADLESS_DAYS) &&
         Clock::now() - seasonStart < deadline)
  {
    const auto dayStart = Clock::now();
    controller.advanceDay();
    dayMs.push_back(millisecondsSince(dayStart));
    ++simulatedDays;
    if (simulatedDays % 7 == 0)
    {
      // Weekly: keep a legal XI like a player would before each match.
      const auto unavailable =
          controller.getUnavailableLineupPlayers(managedId);
      if (!unavailable.empty())
      {
        Team& club = controller.getManagedTeam()->get();
        std::vector<const Player*> squad;
        for (const auto& ref : controller.getPlayersForTeam(managedId))
          if (std::ranges::find(unavailable, ref.get().getId()) ==
              unavailable.end())
            squad.push_back(&ref.get());
        Formation::autoPick(club.getLineup(), Formation::PRESETS[1], squad,
                            controller.getStatsConfig());
      }
    }
    if (dayNumber(controller.getCurrentDate()) % 45 == 0)
      checkWorld(controller, "season " + dateText(controller.getCurrentDate()),
                 log);
  }
  const double seasonWall = millisecondsSince(seasonStart);
  const Stats dayStats = summarize(dayMs);
  const bool fullSeason = seasonOver();
  log.line("\n## Headless end ({}; full season reached: {})\n",
           dateText(controller.getCurrentDate()), fullSeason);
  metrics.line("\n## Headless day simulation (advanceDay)\n");
  metrics.line(
      "{} days in {:.0f} ms: median {:.1f} ms/day, p95 {:.1f}, worst "
      "{:.1f}; full season reached: {}\n",
      simulatedDays, seasonWall, dayStats.median, dayStats.p95, dayStats.worst,
      fullSeason);
  const Stats continueStats = summarize(perDay);
  metrics.line("GUI Continue ms/day: median {:.1f}, p95 {:.1f}, worst {:.1f}\n",
               continueStats.median, continueStats.p95, continueStats.worst);
  checkWorld(controller, "season end", log);
  logResultRealism(controller, "season end", log);
  logTable(controller, leagueId, "season end", log);
  for (const auto& entry : controller.getSeasonHistory())
    log.line(
        "- history: season {} {} champion {} runner-up {} top scorer "
        "{} ({} goals)",
        entry.season, entry.competition_name, entry.champion_id,
        entry.runner_up_id, entry.top_scorer_id, entry.top_scorer_goals);
  const auto scorers = controller.getTopScorers(MatchType::LEAGUE, leagueId, 5);
  for (const PlayerSeasonStats& scorer : scorers)
  {
    const auto who = controller.getGameData()->getPlayer(scorer.player_id);
    log.line("- top scorer: {} {} goals in {} apps",
             who ? who->get().getName() : "?", scorer.goals,
             scorer.appearances);
  }
  log.line("- balance {} ; weekly wages {} ; squad {} ; board confidence {}",
           controller.getManagedTeam()->get().getFinances().getBalance(),
           controller.getWeeklyWageBill(managedId),
           controller.getPlayersForTeam(managedId).size(),
           controller.getBoardState().confidence);
  tour(player, "j7_end_", false);

  // Season rollover (1 July): contracts, promotions, new fixtures.
  player.step = "rollover";
  if (fullSeason)
  {
    const size_t squadBefore = controller.getPlayersForTeam(managedId).size();
    const auto rolloverStart = Clock::now();
    controller.advanceDay();
    const double rolloverMs = millisecondsSince(rolloverStart);
    metrics.line("Season rollover day: {:.0f} ms\n", rolloverMs);
    log.line("\n## New season ({})\n", dateText(controller.getCurrentDate()));
    log.line("- managed squad {} -> {} ; league now {} ; fixtures {}",
             squadBefore, controller.getPlayersForTeam(managedId).size(),
             controller.getManagedTeam()->get().getLeagueId(),
             controller.getTeamFixtures(managedId).size());
    checkWorld(controller, "new season", log);
    player.frames(3);
    tour(player, "j8_newseason_", false);
  }

  // ---- Metrics table -------------------------------------------------------
  metrics.line(
      "\n## Screen frame times (ms; full = update+ImGui+software "
      "raster+present, ui = ImGui build only)\n");
  metrics.line(
      "| Screen | full median | full p95 | full max | ui median | "
      "ui p95 | ui max |");
  metrics.line("|---|---|---|---|---|---|---|");
  for (const auto& timing : player.timings)
    metrics.line("| {} | {:.2f} | {:.2f} | {:.2f} | {:.2f} | {:.2f} | {:.2f} |",
                 timing.screen, timing.full.median, timing.full.p95,
                 timing.full.worst, timing.ui.median, timing.ui.p95,
                 timing.ui.worst);
  metrics.line("\nFrames with ImGui usage errors: {}",
               player.imgui_error_frames);
  EXPECT_EQ(player.imgui_error_frames, 0);
}

/**
 * Clicks every interactive widget of every management screen (fresh career,
 * day 1 and after a few matchdays) and records whether it had any visible
 * effect: scene change, popup, game-state change or a different frame.
 */
TEST_F(PlaytestJourney, EveryWidgetDoesSomething)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Report log(outputDir() / "buttons.md");
  log.line("# Widget sweep\n");
  GameController controller;
  controller.newGame(2);
  const auto teams = controller.getTeams();
  ASSERT_FALSE(teams.empty());
  // A mid-table club of the first league.
  const LeagueID league = teams.front().get().getLeagueId();
  auto leagueTeams = controller.getTeamsInLeague(league);
  std::ranges::sort(leagueTeams,
                    [&](const auto& left, const auto& right)
                    {
                      return averageTopOverall(controller, left.get().getId()) >
                             averageTopOverall(controller, right.get().getId());
                    });
  const TeamID managedId = leagueTeams[leagueTeams.size() / 2].get().getId();
  controller.selectManagedTeam(managedId);
  for (int matchday = 0; matchday < 3; ++matchday)
  {
    controller.advanceToNextManagedFixture();
    controller.advanceDay();
  }

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  Tester player(view, controller, log);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  player.frames(3);
  ASSERT_EQ(player.activeId(), SceneID::GAME_MENU);

  // Signature of everything a click can visibly change.
  const auto drawHash = []()
  {
    uint64_t hash = 1469598103934665603ULL;
    const ImDrawData* data = ImGui::GetDrawData();
    if (data == nullptr) return hash;
    for (const ImDrawList* list : data->CmdLists)
      for (const ImDrawVert& vertex : list->VtxBuffer)
      {
        const auto mix = [&hash](uint64_t value)
        { hash = (hash ^ value) * 1099511628211ULL; };
        mix(static_cast<uint64_t>(std::lround(vertex.pos.x * 4.0f)));
        mix(static_cast<uint64_t>(std::lround(vertex.pos.y * 4.0f)));
        mix(vertex.col);
      }
    return hash;
  };
  struct Signature
  {
    SceneID scene;
    size_t depth;
    bool popup;
    uint64_t pixels;
    int64_t balance;
    size_t unread;
    int date;
    bool operator==(const Signature&) const = default;
  };
  const auto signature = [&]()
  {
    Tester::parkMouse();
    player.uiFrame();
    player.uiFrame();
    return Signature{
        player.activeId(),
        view.getOverlayDepth(),
        ImGui::IsPopupOpen(
            "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
        drawHash(),
        controller.getManagedTeam()->get().getFinances().getBalance(),
        controller.getUnreadInboxCount(),
        dayNumber(controller.getCurrentDate())};
  };
  const auto closePopups = [&]()
  {
    for (int attempt = 0;
         attempt < 3 &&
         ImGui::IsPopupOpen(
             "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
         ++attempt)
      player.key(ImGuiKey_Escape);
    if (ImGui::IsPopupOpen(
            "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
      player.uiFrame(FRAME_SECONDS,
                     []() { ImGui::ClosePopupToLevel(0, true); });
  };

  std::set<std::string> tested;
  int deadTotal = 0;
  int clickedTotal = 0;
  const auto start = Clock::now();
  for (const NavSection section : ALL_SECTIONS)
  {
    if (Clock::now() - start > std::chrono::seconds(170)) break;
    openSection(player, section);
    player.step = std::string("sweep ") + sectionName(section);
    const auto items = player.discover();
    log.line("\n## {} ({} widgets)\n", sectionName(section), items.size());
    // Tab bars remember their tab across screen instances: click tabs last.
    const auto isTab = [](const Item& item)
    {
      const size_t tabs = item.label.find("Tabs/");
      return tabs != std::string::npos &&
             item.label.find('/', tabs + 5) == std::string::npos;
    };
    std::vector<const Item*> order;
    for (const Item& item : items)
      if (!isTab(item)) order.push_back(&item);
    for (const Item& item : items)
      if (isTab(item)) order.push_back(&item);
    const auto clickEffect = [&](ImVec2 point) -> std::string
    {
      openSection(player, section);
      const Signature before = signature();
      if (!(before == signature())) return "animates";
      player.click(point);
      const Signature after = signature();
      player.uiFrame(FRAME_SECONDS, []() { ImGui::ClearActiveID(); });
      if (after.scene != before.scene || after.depth != before.depth)
        return "navigates";
      if (after.popup) return "opens dialog";
      if (after.date != before.date) return "advances time";
      if (after.balance != before.balance || after.unread != before.unread)
        return "changes game state";
      if (after.pixels != before.pixels) return "changes the screen";
      return {};
    };
    int dead = 0;
    for (const Item* itemPointer : order)
    {
      const Item& item = *itemPointer;
      // One click per kind of widget: row buttons differ only in digits.
      std::string kind;
      for (const char character : item.label)
        if (!std::isdigit(static_cast<unsigned char>(character)))
          kind.push_back(character);
      if (const bool shell = item.label.contains("##sidebar") ||
                             item.label.contains("##topbar");
          shell && section != NavSection::HOME)
        continue;
      if (item.label.contains("shell_continue")) continue;
      if (item.label.contains("#SCROLL")) continue;
      if (!tested.insert(kind).second) continue;
      ++clickedTotal;
      std::string effect = clickEffect(item.point);
      // A slider clicked at its current value does not move: try off-centre.
      if (effect.empty() && item.max.x - item.min.x >= 40.0f)
        effect = clickEffect(
            {item.min.x + (item.max.x - item.min.x) * 0.2f, item.point.y});
      closePopups();
      if (effect.empty())
      {
        ++dead;
        log.line("- **NO VISIBLE EFFECT**: `{}` at ({:.0f},{:.0f})", item.label,
                 item.point.x, item.point.y);
      }
      else
      {
        log.line("- `{}`: {}", item.label, effect);
      }
    }
    deadTotal += dead;
    if (dead > 0) player.shot(std::string("b_") + sectionName(section));
  }
  log.line(
      "\n**{} widget kinds clicked, {} without any visible effect.** "
      "({:.0f} s)",
      clickedTotal, deadTotal, millisecondsSince(start) / 1000.0);
  RecordProperty("widgets_without_effect", deadTotal);
}
