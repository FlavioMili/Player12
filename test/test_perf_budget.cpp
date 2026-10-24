// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Performance budgets: Continue on a busy and a quiet day, every management
// screen rendered at 2560x1440 with the interface at scale 2, and save/load
// of the career. Each measurement is repeated and the median is compared
// against a generous budget, so the tests catch real regressions (a screen
// sorting a big table every frame, a day loop going quadratic) without being
// flaky on a busy machine. The numbers are printed for the performance
// report.
//
// Knobs (environment):
//   FM_PERF_THREADS=<n>      simulation threads (default min(8, cores))
//   FM_PERF_FRAMES=<n>       measured frames per screen (default 20)
//   FM_PERF_DAYS=<n>         print the per-day Continue profile for n days
//   FM_PERF_FINGERPRINT=<n>  print a world fingerprint after n days
//   FM_PERF_SEASONS=<n>      play n seasons, then time save/load and memory

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_scene.h"
#include "gui/gui_view.h"
#include "gui/render_scale.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/management_scene.h"
#include "model/calendar.h"
#include "model/game.h"
#include "model/settings_manager.h"
#include "model/transfer_market.h"

// ---- Heap allocation counter ------------------------------------------------
// This executable replaces the global allocation functions so a frame can
// report how many heap allocations it made.

namespace
{
std::atomic<std::uint64_t> heap_allocations{0};

void* countedAllocation(std::size_t size)
{
  heap_allocations.fetch_add(1, std::memory_order_relaxed);
  if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
  throw std::bad_alloc();
}

void* countedAlignedAllocation(std::size_t size, std::align_val_t alignment)
{
  heap_allocations.fetch_add(1, std::memory_order_relaxed);
  const auto align = std::to_underlying(alignment);
  const std::size_t rounded =
      (std::max<std::size_t>(size, 1) + align - 1) / align * align;
  if (void* memory = std::aligned_alloc(align, rounded)) return memory;
  throw std::bad_alloc();
}
}  // namespace

void* operator new(std::size_t size) { return countedAllocation(size); }
void* operator new[](std::size_t size) { return countedAllocation(size); }
void* operator new(std::size_t size, std::align_val_t alignment)
{
  return countedAlignedAllocation(size, alignment);
}
void* operator new[](std::size_t size, std::align_val_t alignment)
{
  return countedAlignedAllocation(size, alignment);
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept
{
  std::free(memory);
}
void operator delete(void* memory, std::align_val_t) noexcept
{
  std::free(memory);
}
void operator delete[](void* memory, std::align_val_t) noexcept
{
  std::free(memory);
}
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept
{
  std::free(memory);
}
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept
{
  std::free(memory);
}

// ---- Frame timing through the real GUIView ----------------------------------

/** One measured frame: building the UI and drawing it. */
struct FrameSample
{
  double build_ms = 0.0; /**< Scene update, ImGui build and ImGui::Render. */
  double total_ms = 0.0; /**< The whole frame including SDL draw/present. */
  std::uint64_t allocations = 0; /**< Heap allocations of the whole frame. */
};

/**
 * The GUI classes grant their internals to this name (the lifecycle test of
 * test_game_flow.cpp, not linked into this executable).
 */
class GameFlowTest_GUIFlowLifecycle_Test
{
 public:
  static bool initialize(GUIView& view) { return view.initialize(); }
  static GUIScene* activeScene(const GUIView& view)
  {
    return view.getActiveScene();
  }

  /** GUIView::render() with the UI build timed apart from the drawing. */
  static FrameSample frame(GUIView& view)
  {
    using Clock = std::chrono::steady_clock;
    const std::uint64_t allocations_before =
        heap_allocations.load(std::memory_order_relaxed);
    const auto started = Clock::now();
    view.applyPendingSceneChanges();
    view.handleEvents();
    view.update(0.016f);

    SDL_SetRenderDrawColor(view.renderer, 30, 30, 30, 255);
    SDL_RenderClear(view.renderer);
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (GUIScene* scene = view.getActiveScene()) scene->render();
    ImGui::Render();
    const auto built = Clock::now();
    {
      const ScopedRenderScale scale(view.renderer,
                                    ImGui::GetIO().DisplayFramebufferScale);
      ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),
                                            view.renderer);
    }
    SDL_RenderPresent(view.renderer);
    const auto finished = Clock::now();
    EXPECT_EQ(ImGui::GetCurrentContext()->ErrorCountCurrentFrame, 0)
        << "ImGui reported a usage error";

    FrameSample sample;
    sample.build_ms =
        std::chrono::duration<double, std::milli>(built - started).count();
    sample.total_ms =
        std::chrono::duration<double, std::milli>(finished - started).count();
    sample.allocations =
        heap_allocations.load(std::memory_order_relaxed) - allocations_before;
    return sample;
  }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 900'000 + (getpid() % 10'000) * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

unsigned envNumber(const char* name, unsigned fallback)
{
  if (const char* text = std::getenv(name); text && *text)
    return static_cast<unsigned>(std::strtoul(text, nullptr, 10));
  return fallback;
}

unsigned perfThreads()
{
  const unsigned cores = std::max(1U, std::thread::hardware_concurrency());
  return std::max(1U, envNumber("FM_PERF_THREADS", std::min(8U, cores)));
}

double median(std::vector<double> values)
{
  if (values.empty()) return 0.0;
  std::ranges::sort(values);
  const std::size_t middle = values.size() / 2;
  return values.size() % 2 == 1 ? values[middle]
                                : 0.5 * (values[middle - 1] + values[middle]);
}

double maximum(const std::vector<double>& values)
{
  return values.empty() ? 0.0 : std::ranges::max(values);
}

double elapsedMs(std::chrono::steady_clock::time_point since)
{
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now() - since)
      .count();
}

std::unique_ptr<GameController> makeCareer(int slot)
{
  Logger::init();
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot, WORLD_SEED);
  controller->selectManagedTeam(controller->getTeams().front().get().getId());
  // Autosaves are measured on their own; a weekly one would land inside an
  // arbitrary measured day.
  controller->setAutosavePolicy({AutosaveFrequency::Off, 0});
  return controller;
}

std::size_t matchesOn(const GameController& controller,
                      const GameDateValue& date)
{
  return controller.getGame()->getCalendar().getMatchesForDate(date).size();
}

void advanceUntil(GameController& controller, const GameDateValue& date)
{
  while (controller.getCurrentDate() < date) controller.advanceDay();
}

/** 64-bit FNV-1a, stable across runs and builds. */
struct Fnv
{
  std::uint64_t hash = 14695981039346656037ULL;
  void add(std::string_view text)
  {
    for (const char c : text)
    {
      hash ^= static_cast<unsigned char>(c);
      hash *= 1099511628211ULL;
    }
    hash ^= 0xFF;
    hash *= 1099511628211ULL;
  }
};

/**
 * Hash of everything the days change: tables, played fixtures with their
 * full reports, club money and selections, every player's dynamics and
 * statistics, the inbox and the transfer history. Floats print in shortest
 * round-trip form, so an equal hash means bit-identical values.
 */
std::uint64_t worldFingerprint(const GameController& controller)
{
  Fnv fnv;
  for (const League& league : controller.getLeagues())
  {
    for (const StandingRow& row : controller.getStandings(league.getId()))
      fnv.add(std::format("table {} {} {} {}/{}/{}/{} {}:{} {} {}",
                          league.getId(), row.team_id, row.position, row.played,
                          row.won, row.drawn, row.lost, row.goals_for,
                          row.goals_against, row.points, row.form));
  }
  for (const auto& [date, matches] :
       controller.getGame()->getCalendar().getFullCalendar())
  {
    for (const Match& match : matches)
    {
      if (!match.isPlayed()) continue;
      fnv.add(std::format("fixture {} {}-{} {}:{} {} {}:{}", date.toString(),
                          match.getHomeTeamId(), match.getAwayTeamId(),
                          match.getHomeScore(), match.getAwayScore(),
                          match.wentToExtraTime(), match.getHomePenalties(),
                          match.getAwayPenalties()));
      if (const auto report = controller.getMatchReport(
              date, match.getHomeTeamId(), match.getAwayTeamId()))
      {
        fnv.add(std::to_string(report->attendance));
        fnv.add(report->eventsToJson());
        fnv.add(report->playersToJson());
        fnv.add(report->statsToJson());
      }
    }
  }
  for (const Team& team : controller.getTeams())
  {
    const Lineup& lineup = team.getLineup();
    std::string line = std::format(
        "club {} {} {} {}", team.getId(), team.getFinances().getBalance(),
        team.getFinances().ledgerTotal(),
        lineup.getGoalkeeper() ? lineup.getGoalkeeper()->getId() : 0);
    for (const auto& slot : lineup.getOutfieldPlayers())
      line += std::format(" {}", slot.player ? slot.player->getId() : 0);
    for (const PlayerID id : team.getPlayerIDs())
      line += std::format(" p{}", id);
    fnv.add(line);
  }
  for (const auto& [id, player] : controller.getGameData()->getPlayers())
  {
    const PlayerDynamics& dynamics = player.getDynamics();
    std::string line = std::format(
        "player {} {} {} {} {} {} {}/{} {} {} {} {}", id, player.getTeamId(),
        dynamics.condition, dynamics.sharpness, dynamics.morale,
        dynamics.playing_share,
        static_cast<int>(std::to_underlying(dynamics.injury)),
        dynamics.injury_days, dynamics.last_match_day,
        dynamics.season_appearances, dynamics.season_minutes, player.getAge());
    for (const float rating : dynamics.recent_ratings)
      line += std::format(" r{}", rating);
    for (const auto& [name, value] : player.getStats())
      line += std::format(" {}={}", name, value);
    fnv.add(line);
  }
  for (const InboxMessage& message : controller.getInbox())
  {
    std::string line =
        std::format("inbox {} {} {} {}", message.id, message.date.toString(),
                    message.title_key, message.body_key);
    for (const std::string& argument : message.args) line += " " + argument;
    fnv.add(line);
  }
  for (const TransferRecord& record :
       controller.getGame()->getTransfers().history())
    fnv.add(std::format("transfer {} {} {} {} {} {}", record.player_id,
                        record.date.toString(), record.from_team,
                        record.to_team, record.fee,
                        static_cast<int>(std::to_underlying(record.kind))));
  fnv.add(controller.getCurrentDate().toString());
  return fnv.hash;
}

/**
 * Median wall time of Continue from the day before @p date, repeated by
 * reloading the same save so every repetition does the same work.
 */
double medianContinueMs(GameController& controller, int slot,
                        const GameDateValue& date, unsigned threads,
                        int repetitions)
{
  advanceUntil(controller, SeasonCalendar::addDays(date, -1));
  EXPECT_TRUE(controller.saveGame());
  std::vector<double> samples;
  for (int repetition = 0; repetition < repetitions; ++repetition)
  {
    EXPECT_TRUE(controller.loadGame(slot));
    controller.setAutosavePolicy({AutosaveFrequency::Off, 0});
    controller.setSimulationThreads(threads);
    const auto started = std::chrono::steady_clock::now();
    controller.advanceDay();
    samples.push_back(elapsedMs(started));
  }
  return median(samples);
}
}  // namespace

// Continue on the busiest day of the opening weeks and on a quiet day with no
// fixtures at all. Budgets are several times the measured cost (NFR-006 and
// NFR-034 ask for 0.25 s on a quiet day and about 0.5 s on a match day on the
// reference laptop), so only a real regression fails.
TEST(PerfBudget, ContinueOnBusyAndQuietDays)
{
  constexpr int REPETITIONS = 5;
  constexpr double BUSY_DAY_BUDGET_MS = 6000.0;
  constexpr double QUIET_DAY_BUDGET_MS = 1000.0;
  constexpr int SEARCH_DAYS = 75;

  const SlotCleanup slot{uniqueSlot(0)};
  const auto controller = makeCareer(slot.slot);
  const unsigned threads = perfThreads();
  controller->setSimulationThreads(threads);

  const GameDateValue start = controller->getCurrentDate();
  std::optional<GameDateValue> busy;
  for (int offset = 2; offset <= SEARCH_DAYS; ++offset)
  {
    const GameDateValue day = SeasonCalendar::addDays(start, offset);
    if (!busy || matchesOn(*controller, day) > matchesOn(*controller, *busy))
      busy = day;
  }
  ASSERT_TRUE(busy);
  std::optional<GameDateValue> quiet;
  for (int offset = 1; offset <= SEARCH_DAYS && !quiet; ++offset)
  {
    const GameDateValue day = SeasonCalendar::addDays(*busy, offset);
    const GameDateValue before = SeasonCalendar::addDays(day, -1);
    // No fixtures that day and none the day before (left-over fixtures of
    // yesterday are played on Continue).
    if (matchesOn(*controller, day) == 0 && matchesOn(*controller, before) == 0)
      quiet = day;
  }
  ASSERT_TRUE(quiet);

  const std::size_t busy_matches = matchesOn(*controller, *busy);
  const double busy_ms =
      medianContinueMs(*controller, slot.slot, *busy, threads, REPETITIONS);
  const double quiet_ms =
      medianContinueMs(*controller, slot.slot, *quiet, threads, REPETITIONS);
  std::cout << std::format(
      "[perf] Continue, {} threads, median of {}: busy day {} ({} fixtures) "
      "{:.1f} ms ({:.1f} ms per fixture); quiet day {} {:.1f} ms\n",
      threads, REPETITIONS, busy->toString(), busy_matches, busy_ms,
      busy_ms / static_cast<double>(std::max<std::size_t>(busy_matches, 1)),
      quiet->toString(), quiet_ms);
  EXPECT_LT(busy_ms, BUSY_DAY_BUDGET_MS);
  EXPECT_LT(quiet_ms, QUIET_DAY_BUDGET_MS);
}

// Saving and loading the career (the opening weeks of a standard world).
TEST(PerfBudget, SaveAndLoad)
{
  constexpr int REPETITIONS = 5;
  constexpr double SAVE_BUDGET_MS = 4000.0;
  constexpr double LOAD_BUDGET_MS = 6000.0;

  const SlotCleanup slot{uniqueSlot(1)};
  const auto controller = makeCareer(slot.slot);
  controller->setSimulationThreads(perfThreads());
  for (int day = 0; day < 30; ++day) controller->advanceDay();

  std::vector<double> saves;
  std::vector<double> loads;
  for (int repetition = 0; repetition < REPETITIONS; ++repetition)
  {
    auto started = std::chrono::steady_clock::now();
    ASSERT_TRUE(controller->saveGame());
    saves.push_back(elapsedMs(started));
    started = std::chrono::steady_clock::now();
    ASSERT_TRUE(controller->loadGame(slot.slot));
    loads.push_back(elapsedMs(started));
  }
  const auto bytes =
      std::filesystem::file_size(RuntimePaths::savePath(slot.slot));
  std::cout << std::format(
      "[perf] save median {:.1f} ms (max {:.1f}), load median {:.1f} ms "
      "(max {:.1f}), file {:.1f} MiB\n",
      median(saves), maximum(saves), median(loads), maximum(loads),
      static_cast<double>(bytes) / (1024.0 * 1024.0));
  EXPECT_LT(median(saves), SAVE_BUDGET_MS);
  EXPECT_LT(median(loads), LOAD_BUDGET_MS);
}

// Every management screen at 2560x1440 with the interface at scale 2 (a
// 1280x720 logical layout, as on a HiDPI laptop). The budget applies to the
// median time to build a frame; drawing goes through the software renderer
// here, so it is printed but not budgeted.
TEST(PerfBudget, ManagementScreenFrames)
{
  constexpr double BUILD_BUDGET_MS = 16.7;
  const unsigned measured = std::max(3U, envNumber("FM_PERF_FRAMES", 20));

  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  const SlotCleanup slot{uniqueSlot(2)};
  const auto controller = makeCareer(slot.slot);
  controller->setSimulationThreads(perfThreads());
  // A few weeks in: friendlies and league matches played, a full inbox.
  for (int day = 0; day < 45; ++day) controller->advanceDay();

  Settings& settings = SettingsManager::instance()->get();
  const float original_scale = settings.ui_scale;
  settings.ui_scale = 2.0f;

  GUIView view(*controller);
  ASSERT_TRUE(Bridge::initialize(view));
  view.refreshTheme();
  SDL_SetWindowSize(view.getWindow(), 2560, 1440);
  SDL_Event resized{.window = {}};
  resized.window.type = SDL_EVENT_WINDOW_RESIZED;
  resized.window.windowID = SDL_GetWindowID(view.getWindow());
  resized.window.data1 = 2560;
  resized.window.data2 = 1440;
  SDL_PushEvent(&resized);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  for (int warmup = 0; warmup < 3; ++warmup) Bridge::frame(view);

  const TeamID managed = controller->getGame()->getManagedTeamId();
  const PlayerID star =
      controller->getPlayersForTeam(managed).front().get().getId();
  std::optional<std::tuple<GameDateValue, TeamID, TeamID>> played;
  for (const auto& [date, matches] :
       controller->getGame()->getCalendar().getFullCalendar())
    for (const Match& match : matches)
      if (match.isPlayed() && !played)
        played.emplace(date, match.getHomeTeamId(), match.getAwayTeamId());
  const TeamID rival = controller->getTeams().back().get().getId();

  struct Screen
  {
    const char* name;
    std::function<void()> open;
  };
  std::vector<Screen> screens;
  const std::array<std::pair<NavSection, const char*>, 26> sections = {{
      {NavSection::HOME, "home"},
      {NavSection::INBOX, "inbox"},
      {NavSection::CLUB, "club"},
      {NavSection::SQUAD, "squad"},
      {NavSection::LINEUP, "lineup"},
      {NavSection::TACTICS, "tactics"},
      {NavSection::FIXTURES, "fixtures"},
      {NavSection::STANDINGS, "standings"},
      {NavSection::TRANSFERS, "transfers"},
      {NavSection::FINANCES, "finances"},
      {NavSection::SCOUTING, "scouting"},
      {NavSection::TRAINING, "training"},
      {NavSection::STAFF, "staff"},
      {NavSection::YOUTH, "youth"},
      {NavSection::MANAGER, "manager"},
      {NavSection::MEDICAL, "medical"},
      {NavSection::CALENDAR, "calendar"},
      {NavSection::SQUAD_PLANNER, "squad planner"},
      {NavSection::COMPARE, "compare"},
      {NavSection::DELEGATION, "delegation"},
      {NavSection::DATA_HUB, "data hub"},
      {NavSection::OPPOSITION, "opposition"},
      {NavSection::INTERNATIONAL, "international"},
      {NavSection::AWARDS, "awards"},
      {NavSection::RECORDS, "records"},
      {NavSection::PLANNING, "planning"},
  }};
  for (const auto& [section, name] : sections)
    screens.emplace_back(
        name, [&view, section] { Navigation::open(&view, section); });
  screens.emplace_back("player profile",
                       [&view, star] { Navigation::openPlayer(&view, star); });
  screens.emplace_back("other club",
                       [&view, rival] { Navigation::openClub(&view, rival); });
  if (played)
    screens.emplace_back("match report",
                         [&view, &played]
                         {
                           const auto& [date, home, away] = *played;
                           Navigation::openMatchReport(&view, date, home, away);
                         });

  std::cout << std::format(
      "[perf] screen frames at 2560x1440, scale 2, {} frames each\n"
      "[perf] {:<15} {:>9} {:>9} {:>9} {:>9} {:>9} {:>11}\n",
      measured, "screen", "build med", "build max", "frame med", "frame max",
      "first ms", "allocs/frame");
  for (const Screen& screen : screens)
  {
    screen.open();
    // The first frame after opening builds the screen's caches.
    const FrameSample first = Bridge::frame(view);
    Bridge::frame(view);
    std::vector<double> builds;
    std::vector<double> totals;
    std::vector<double> allocations;
    for (unsigned index = 0; index < measured; ++index)
    {
      const FrameSample sample = Bridge::frame(view);
      builds.push_back(sample.build_ms);
      totals.push_back(sample.total_ms);
      allocations.push_back(static_cast<double>(sample.allocations));
    }
    std::cout << std::format(
        "[perf] {:<15} {:>9.2f} {:>9.2f} {:>9.2f} {:>9.2f} {:>9.1f} "
        "{:>11.0f}\n",
        screen.name, median(builds), maximum(builds), median(totals),
        maximum(totals), first.total_ms, median(allocations));
    EXPECT_LT(median(builds), BUILD_BUDGET_MS) << screen.name;
  }
  settings.ui_scale = original_scale;
}

// Per-day Continue profile (FM_PERF_DAYS=n): wall time and fixtures of every
// day, for the performance report.
TEST(PerfBudget, DayProfile)
{
  const unsigned days = envNumber("FM_PERF_DAYS", 0);
  if (days == 0) GTEST_SKIP() << "set FM_PERF_DAYS=<n> to profile n days";
  const SlotCleanup slot{uniqueSlot(3)};
  const auto controller = makeCareer(slot.slot);
  const unsigned threads = perfThreads();
  controller->setSimulationThreads(threads);
  double total_ms = 0.0;
  std::size_t total_matches = 0;
  std::vector<double> quiet;
  for (unsigned day = 0; day < days; ++day)
  {
    const GameDateValue next =
        SeasonCalendar::addDays(controller->getCurrentDate(), 1);
    const std::size_t fixtures = matchesOn(*controller, next);
    const auto started = std::chrono::steady_clock::now();
    controller->advanceDay();
    const double ms = elapsedMs(started);
    total_ms += ms;
    total_matches += fixtures;
    if (fixtures == 0) quiet.push_back(ms);
    std::cout << std::format("[perf] day {} {:>3} fixtures {:>8.1f} ms\n",
                             next.toString(), fixtures, ms);
  }
  std::cout << std::format(
      "[perf] {} days, {} threads: {:.2f} s total, {} fixtures, quiet-day "
      "median {:.1f} ms\n",
      days, threads, total_ms / 1000.0, total_matches, median(quiet));
}

// World fingerprint after n days (FM_PERF_FINGERPRINT=n): equal hashes before
// and after an optimisation, and for any thread count, prove that results are
// bit-identical.
TEST(PerfBudget, WorldFingerprint)
{
  const unsigned days = envNumber("FM_PERF_FINGERPRINT", 0);
  if (days == 0) GTEST_SKIP() << "set FM_PERF_FINGERPRINT=<n> to hash n days";
  const SlotCleanup slot{uniqueSlot(4)};
  const auto controller = makeCareer(slot.slot);
  const unsigned threads = perfThreads();
  controller->setSimulationThreads(threads);
  for (unsigned day = 0; day < days; ++day) controller->advanceDay();
  std::cout << std::format(
      "[perf] fingerprint after {} days ({} threads): "
      "{:016x}\n",
      days, threads, worldFingerprint(*controller));
}

namespace
{
/** Resident and peak resident memory of this process in MiB. */
std::pair<double, double> residentMiB()
{
  std::ifstream status("/proc/self/status");
  std::string line;
  double rss = 0.0;
  double peak = 0.0;
  while (std::getline(status, line))
  {
    if (line.starts_with("VmRSS:"))
      rss = std::strtod(line.c_str() + 6, nullptr) / 1024.0;
    else if (line.starts_with("VmHWM:"))
      peak = std::strtod(line.c_str() + 6, nullptr) / 1024.0;
  }
  return {rss, peak};
}
}  // namespace

// Save, load and memory of a long career (FM_PERF_SEASONS=n seasons).
TEST(PerfBudget, LongCareerSaveAndLoad)
{
  const unsigned seasons = envNumber("FM_PERF_SEASONS", 0);
  if (seasons == 0) GTEST_SKIP() << "set FM_PERF_SEASONS=<n> to play n seasons";
  constexpr int REPETITIONS = 3;
  const SlotCleanup slot{uniqueSlot(5)};
  const auto controller = makeCareer(slot.slot);
  controller->setSimulationThreads(perfThreads());
  const int first_season = controller->getCurrentSeason();
  const auto started = std::chrono::steady_clock::now();
  while (controller->getCurrentSeason() <
         first_season + static_cast<int>(seasons))
    controller->advanceDay();
  const double played_s = elapsedMs(started) / 1000.0;
  const auto [rss, peak] = residentMiB();

  std::vector<double> saves;
  std::vector<double> loads;
  for (int repetition = 0; repetition < REPETITIONS; ++repetition)
  {
    auto begun = std::chrono::steady_clock::now();
    ASSERT_TRUE(controller->saveGame());
    saves.push_back(elapsedMs(begun));
    begun = std::chrono::steady_clock::now();
    ASSERT_TRUE(controller->loadGame(slot.slot));
    loads.push_back(elapsedMs(begun));
  }
  const auto [rss_after, peak_after] = residentMiB();
  const auto bytes =
      std::filesystem::file_size(RuntimePaths::savePath(slot.slot));
  std::cout << std::format(
      "[perf] {} seasons played in {:.1f} s ({} threads); save median {:.1f} "
      "ms, load median {:.1f} ms, file {:.1f} MiB; RSS {:.0f} MiB (peak "
      "{:.0f}), after reloads {:.0f} MiB (peak {:.0f})\n",
      seasons, played_s, perfThreads(), median(saves), median(loads),
      static_cast<double>(bytes) / (1024.0 * 1024.0), rss, peak, rss_after,
      peak_after);
}
