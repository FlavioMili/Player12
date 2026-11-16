// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <fmt/format.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/background_task.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "global/thread_pool.h"
#include "model/calendar.h"
#include "model/discipline.h"
#include "model/match.h"
#include "model/match_engine.h"
#include "model/match_scheduler.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;
constexpr unsigned PARALLEL_THREADS = 4;

/** Save slot unique to this process (the test binaries run in parallel). */
int uniqueSlot(int offset)
{
  return 300'000 + (getpid() % 100'000) * 10 + offset;
}

struct SlotCleanup
{
  int slot;
  explicit SlotCleanup(int slotValue) : slot(slotValue) {}
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;
};

std::unique_ptr<GameController> makeWorld(int slot)
{
  Logger::init();
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot, WORLD_SEED);
  return controller;
}

/**
 * Aborts the process when its scope does not end within @p limit: a hang
 * during shutdown fails fast with a message instead of stalling the run.
 */
class ShutdownWatchdog
{
 public:
  ShutdownWatchdog(std::chrono::seconds limit, const char* what)
      : watcher(
            [this, limit, what]
            {
              std::unique_lock lock(mutex);
              if (!finished.wait_for(lock, limit, [this] { return done; }))
              {
                std::print(stderr, "{} did not finish within {} s\n", what,
                           limit.count());
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
  std::mutex mutex;
  std::condition_variable finished;
  bool done = false;
  std::jthread watcher;  // Last: starts once the state above exists.
};

constexpr std::chrono::seconds SHUTDOWN_LIMIT{20};

/** Restores an environment variable when the test ends. */
class ScopedEnv
{
 public:
  ScopedEnv(const char* name, const char* value) : name(name)
  {
    if (const char* previous_value = std::getenv(name))
      previous = previous_value;
    if (value)
      setenv(name, value, 1);
    else
      unsetenv(name);
  }
  ~ScopedEnv()
  {
    if (previous)
      setenv(name, previous->c_str(), 1);
    else
      unsetenv(name);
  }
  ScopedEnv(const ScopedEnv&) = delete;
  ScopedEnv& operator=(const ScopedEnv&) = delete;

 private:
  const char* name;
  std::optional<std::string> previous;
};

/** First day with at least @p minimum fixtures (a full league matchday). */
std::optional<GameDateValue> firstBusyDay(const GameController& controller,
                                          std::size_t minimum)
{
  for (const auto& [date, matches] :
       controller.getGame()->getCalendar().getFullCalendar())
  {
    if (matches.size() >= minimum) return date;
  }
  return std::nullopt;
}

void advanceUntil(GameController& controller, const GameDateValue& date)
{
  while (controller.getCurrentDate() < date) controller.advanceDay();
}

std::vector<MatchSimulationInput> prepareDay(const GameController& controller,
                                             const GameDateValue& date)
{
  std::vector<MatchSimulationInput> inputs;
  for (const Match& match :
       controller.getGame()->getCalendar().getMatchesForDate(date))
  {
    if (auto input = match.prepareSimulation(*controller.getGameData()))
      inputs.push_back(std::move(*input));
  }
  return inputs;
}

std::string describe(const MatchSimulationResult& result)
{
  std::string text =
      fmt::format("{}-{} et{} pens{} {} {} {}", result.home_goals,
                  result.away_goals, result.extra_time,
                  result.penalties.has_value(), result.report.eventsToJson(),
                  result.report.playersToJson(), result.report.statsToJson());
  for (const PlayerMatchConsequence& consequence : result.consequences)
    text += fmt::format(" {}:{}:{}:{}", consequence.player_id,
                        consequence.minutes_played, consequence.end_condition,
                        consequence.injured);
  return text;
}

/**
 * One line per standing row, played fixture (with its full report), club and
 * player: everything a matchday changes. Floats print in shortest round-trip
 * form, so equal lines mean bit-identical values.
 */
std::vector<std::string> worldFingerprint(const GameController& controller)
{
  std::vector<std::string> lines;
  for (const League& league : controller.getLeagues())
  {
    for (const StandingRow& row : controller.getStandings(league.getId()))
      lines.push_back(fmt::format(
          "table {} {} pos{} {}/{}/{}/{} {}:{} pts{} {}", league.getId(),
          row.team_id, row.position, row.played, row.won, row.drawn, row.lost,
          row.goals_for, row.goals_against, row.points, row.form));
  }
  for (const auto& [date, matches] :
       controller.getGame()->getCalendar().getFullCalendar())
  {
    for (const Match& match : matches)
    {
      if (!match.isPlayed()) continue;
      std::string line = fmt::format(
          "fixture {} {}-{} {}:{} et{} pens{}:{}", date.toString(),
          match.getHomeTeamId(), match.getAwayTeamId(), match.getHomeScore(),
          match.getAwayScore(), match.wentToExtraTime(),
          match.getHomePenalties(), match.getAwayPenalties());
      if (const auto report = controller.getMatchReport(
              date, match.getHomeTeamId(), match.getAwayTeamId()))
        line += fmt::format(" att{} {} {} {}", report->attendance,
                            report->eventsToJson(), report->playersToJson(),
                            report->statsToJson());
      lines.push_back(std::move(line));
    }
  }
  for (const Team& team : controller.getTeams())
  {
    const Lineup& lineup = team.getLineup();
    std::string line = fmt::format(
        "club {} bal{} ledger{} gk{}", team.getId(),
        team.getFinances().getBalance(), team.getFinances().ledgerTotal(),
        lineup.getGoalkeeper() ? lineup.getGoalkeeper()->getId() : 0);
    for (const auto& slot : lineup.getOutfieldPlayers())
      line += fmt::format(" {}", slot.player ? slot.player->getId() : 0);
    lines.push_back(std::move(line));
  }
  for (const auto& [id, player] : controller.getGameData()->getPlayers())
  {
    const PlayerDynamics& dynamics = player.getDynamics();
    std::string line = fmt::format(
        "player {} team{} cond{} sharp{} morale{} share{} inj{}/{} last{}"
        " apps{} mins{}",
        id, player.getTeamId(), dynamics.condition, dynamics.sharpness,
        dynamics.morale, dynamics.playing_share,
        static_cast<int>(std::to_underlying(dynamics.injury)),
        dynamics.injury_days, dynamics.last_match_day,
        dynamics.season_appearances, dynamics.season_minutes);
    for (const float rating : dynamics.recent_ratings)
      line += fmt::format(" r{}", rating);
    for (const auto& [name, value] : player.getStats())
      line += fmt::format(" {}={}", name, value);
    lines.push_back(std::move(line));
  }
  return lines;
}

void expectSameWorld(const std::vector<std::string>& sequential,
                     const std::vector<std::string>& parallel)
{
  ASSERT_EQ(sequential.size(), parallel.size());
  for (std::size_t i = 0; i < sequential.size(); ++i)
  {
    if (sequential[i] != parallel[i])
    {
      constexpr std::size_t SHOWN = 400;
      FAIL() << "first difference at line " << i
             << "\n sequential: " << sequential[i].substr(0, SHOWN)
             << "\n parallel:   " << parallel[i].substr(0, SHOWN);
    }
  }
}
}  // namespace

// ---------------------------------------------------------------------------
// Thread pool
// ---------------------------------------------------------------------------

TEST(ThreadPoolTest, ParallelForRunsEveryIndexExactlyOnce)
{
  ThreadPool pool(3);
  EXPECT_EQ(pool.getWorkerCount(), 3U);
  constexpr std::size_t COUNT = 2000;
  std::vector<std::atomic<int>> calls(COUNT);
  pool.parallelFor(COUNT, [&calls](std::size_t index) { ++calls[index]; });
  for (std::size_t i = 0; i < COUNT; ++i) ASSERT_EQ(calls[i].load(), 1) << i;

  pool.parallelFor(0, [](std::size_t) { FAIL() << "no index to run"; });
}

TEST(ThreadPoolTest, RethrowsTheFirstTaskExceptionAfterTheBatch)
{
  ThreadPool pool(2);
  std::atomic finished{0};
  EXPECT_THROW(pool.parallelFor(64,
                                [&finished](std::size_t index)
                                {
                                  if (index == 10)
                                    throw std::runtime_error("task failed");
                                  ++finished;
                                }),
               std::runtime_error);
  EXPECT_EQ(finished.load(), 63);
}

TEST(ThreadPoolTest, NestedBatchesOnBusyWorkersComplete)
{
  // Every worker blocks inside an outer task; the inner batches must still
  // finish because their callers work through them.
  ThreadPool pool(2);
  std::atomic inner{0};
  pool.parallelFor(
      4, [&pool, &inner](std::size_t)
      { pool.parallelFor(16, [&inner](std::size_t) { ++inner; }); });
  EXPECT_EQ(inner.load(), 64);
}

TEST(ThreadPoolTest, DestroyingAPoolJoinsItsWorkersPromptly)
{
  // Idle pools, pools right after a batch and pools with helper jobs still
  // queued (a batch the caller finished alone) all shut down at once.
  const ShutdownWatchdog watchdog(SHUTDOWN_LIMIT, "destroying thread pools");
  for (int round = 0; round < 50; ++round)
  {
    {
      ThreadPool idle(ThreadPool::MAX_THREADS);
    }
    ThreadPool pool(3);
    std::atomic calls{0};
    pool.parallelFor(64, [&calls](std::size_t) { ++calls; });
    pool.parallelFor(2, [&calls](std::size_t) { ++calls; });
    EXPECT_EQ(calls.load(), 66);
  }
}

TEST(ThreadPoolTest, DefaultThreadCountHonoursTheEnvironment)
{
  {
    const ScopedEnv env("FM_SIM_THREADS", "3");
    EXPECT_EQ(ThreadPool::defaultThreadCount(), 3U);
  }
  {
    const ScopedEnv env("FM_SIM_THREADS", "64");
    EXPECT_EQ(ThreadPool::defaultThreadCount(), ThreadPool::MAX_THREADS);
  }
  for (const char* invalid : {"0", "two", "", "-1"})
  {
    const ScopedEnv env("FM_SIM_THREADS", invalid);
    const unsigned threads = ThreadPool::defaultThreadCount();
    EXPECT_GE(threads, 1U) << invalid;
    EXPECT_LE(threads, ThreadPool::MAX_THREADS) << invalid;
  }
}

// ---------------------------------------------------------------------------
// Parallel matchday simulation
// ---------------------------------------------------------------------------

TEST(MatchSchedulerTest, BatchResultsDoNotDependOnTheThreadCount)
{
  const SlotCleanup slot{uniqueSlot(0)};
  const auto controller = makeWorld(slot.slot);
  const auto matchday = firstBusyDay(*controller, 50);
  ASSERT_TRUE(matchday);
  const std::vector<MatchSimulationInput> inputs =
      prepareDay(*controller, *matchday);
  ASSERT_GE(inputs.size(), 50U);

  const StatsConfig& config = controller->getStatsConfig();
  MatchScheduler sequential(1);
  MatchScheduler parallel(PARALLEL_THREADS);
  const auto expected = sequential.run(inputs, config);
  const auto actual = parallel.run(inputs, config);
  ASSERT_EQ(actual.size(), inputs.size());
  for (std::size_t i = 0; i < inputs.size(); ++i)
    ASSERT_EQ(describe(actual[i]), describe(expected[i])) << "match " << i;

  const SimulationProgress progress = parallel.getProgress();
  EXPECT_EQ(progress.completed, inputs.size());
  EXPECT_EQ(progress.total, inputs.size());
  parallel.resetProgress();
  EXPECT_EQ(parallel.getProgress().total, 0U);
}

TEST(MatchSchedulerTest, ParallelCareerMatchesSequentialCareer)
{
  const SlotCleanup sequential_slot{uniqueSlot(1)};
  const SlotCleanup parallel_slot{uniqueSlot(2)};
  const auto sequential = makeWorld(sequential_slot.slot);
  const auto parallel = makeWorld(parallel_slot.slot);
  sequential->setSimulationThreads(1);
  parallel->setSimulationThreads(PARALLEL_THREADS);
  ASSERT_EQ(parallel->getGame()->getSimulationThreads(), PARALLEL_THREADS);

  // One full league matchday first, then eight more weeks (friendlies,
  // league rounds and the first cup round with extra time and penalties).
  const auto matchday = firstBusyDay(*sequential, 50);
  ASSERT_TRUE(matchday);
  advanceUntil(*sequential, *matchday);
  advanceUntil(*parallel, *matchday);
  expectSameWorld(worldFingerprint(*sequential), worldFingerprint(*parallel));

  const GameDateValue end = SeasonCalendar::addDays(*matchday, 56);
  advanceUntil(*sequential, end);
  advanceUntil(*parallel, end);
  expectSameWorld(worldFingerprint(*sequential), worldFingerprint(*parallel));

  std::size_t cup_ties = 0;
  for (const auto& [date, matches] :
       parallel->getGame()->getCalendar().getFullCalendar())
  {
    for (const Match& match : matches)
      if (match.isPlayed() && match.isKnockout()) ++cup_ties;
  }
  EXPECT_GT(cup_ties, 0U);
}

TEST(MatchSchedulerTest, ContinueReportsMatchdayProgress)
{
  const SlotCleanup slot{uniqueSlot(3)};
  const auto controller = makeWorld(slot.slot);
  EXPECT_EQ(controller->getContinueProgress().fraction(), 0.0f);
  const auto matchday = firstBusyDay(*controller, 50);
  ASSERT_TRUE(matchday);
  advanceUntil(*controller, SeasonCalendar::addDays(*matchday, -1));

  controller->advanceDay();
  const GameController::ContinueProgress progress =
      controller->getContinueProgress();
  EXPECT_EQ(progress.days_done, 0);
  EXPECT_EQ(progress.days_total, 1);
  EXPECT_GE(progress.matches_total, 50U);
  EXPECT_EQ(progress.matches_done, progress.matches_total);
  EXPECT_FLOAT_EQ(progress.fraction(), 1.0f);
}

// Quitting after parallel matchdays: the scheduler's pool (created on the
// first parallel batch) is torn down with the game, on the thread that
// destroys the controller, also when the days ran on another thread (as
// Continue does). Each destruction must finish promptly.
TEST(MatchSchedulerTest, ControllerShutsDownCleanlyAfterParallelMatchdays)
{
  for (const bool on_worker : {false, true})
  {
    SCOPED_TRACE(on_worker ? "days simulated on a worker thread"
                           : "days simulated on the test thread");
    const SlotCleanup slot{uniqueSlot(on_worker ? 8 : 7)};
    auto controller = makeWorld(slot.slot);
    controller->setSimulationThreads(PARALLEL_THREADS);
    const auto matchday = firstBusyDay(*controller, 50);
    ASSERT_TRUE(matchday);
    // Two full league matchdays and the days between them.
    const GameDateValue end = SeasonCalendar::addDays(*matchday, 8);
    if (on_worker)
    {
      std::jthread worker;
      launchBackgroundTask(
          worker, [&controller, end] { advanceUntil(*controller, end); })
          .get();
    }
    else
      advanceUntil(*controller, end);
    std::size_t played = 0;
    for (const auto& [date, matches] :
         controller->getGame()->getCalendar().getFullCalendar())
      played += static_cast<std::size_t>(std::ranges::count_if(
          matches, [](const Match& match) { return match.isPlayed(); }));
    ASSERT_GE(played, 100U) << "the parallel scheduler never ran";
    ASSERT_EQ(controller->getGame()->getSimulationThreads(), PARALLEL_THREADS);

    const ShutdownWatchdog watchdog(SHUTDOWN_LIMIT,
                                    "destroying the game controller");
    controller.reset();
  }
}

// The scheduler must hand the engine the fixture's real home side: its
// result for a fixture equals a direct engine run with the home line-up
// first, and swapping the venue swaps the sides the engine sees. (Home
// advantage itself is the engine's job and is calibrated there.)
TEST(MatchSchedulerTest, SchedulerKeepsTheFixtureVenue)
{
  const SlotCleanup slot{uniqueSlot(6)};
  const auto controller = makeWorld(slot.slot);
  const auto matchday = firstBusyDay(*controller, 50);
  ASSERT_TRUE(matchday);
  const std::vector<MatchSimulationInput> inputs =
      prepareDay(*controller, *matchday);
  ASSERT_GE(inputs.size(), 8U);
  const StatsConfig& config = controller->getStatsConfig();
  for (std::size_t i = 0; i < 8; ++i)
  {
    const MatchSimulationInput& input = inputs[i];
    const auto team = controller->getGameData()->getTeam(input.home_id);
    ASSERT_TRUE(team);
    EXPECT_EQ(input.home_lineup.getGoalkeeper(),
              team->get().getLineup().getGoalkeeper());

    for (const bool swapped : {false, true})
    {
      MatchSimulationInput venue = input;
      venue.knockout = {};
      if (swapped)
      {
        std::swap(venue.home_id, venue.away_id);
        std::swap(venue.home_lineup, venue.away_lineup);
        std::swap(venue.home_strategy, venue.away_strategy);
      }
      const MatchSimulationResult result = MatchSimulation::run(venue, config);
      MatchEngine engine(venue.home_lineup, venue.away_lineup,
                         venue.home_strategy, venue.away_strategy, config,
                         venue.seed);
      // Both paths play the fixture in its league's style.
      engine.setMatchContext(MatchSimulation::leagueContext(venue.league_id));
      MatchdaySquad::carryCondition(engine, venue.home_lineup);
      MatchdaySquad::carryCondition(engine, venue.away_lineup);
      engine.simulateToEnd(MatchFidelity::BACKGROUND);
      EXPECT_EQ(result.home_goals, engine.getHomeScore()) << "match " << i;
      EXPECT_EQ(result.away_goals, engine.getAwayScore()) << "match " << i;
    }
  }
}

// ---------------------------------------------------------------------------
// Serving bans only visits outstanding bans (it ran after every match)
// ---------------------------------------------------------------------------

TEST(DisciplineBansTest, RestoredBansAreServedOnlyByTheirTeamAndScope)
{
  GameData gamedata;
  for (const TeamID team : {10, 20, 30, 40})
    gamedata.addTeam(team, Team(team, 1, "Club", 0));
  const auto addPlayer = [&](PlayerID id, TeamID team)
  {
    gamedata.addPlayer(
        id, Player(id, team, "Test", "Player", PlayerRole::CM, Language::EN,
                   1000, 0, 25, 2, 180, Foot::Right, {}));
  };
  addPlayer(1, 10);
  addPlayer(2, 10);
  addPlayer(3, 20);

  Discipline discipline;
  discipline.restore({{1, MatchType::LEAGUE, 5, 0, 2},
                      {2, MatchType::LEAGUE, 3, 0, 0},
                      {3, MatchType::CUP, 0, 1, 1}});
  const auto play = [&](MatchType type, TeamID home, TeamID away)
  {
    MatchReport report;
    report.match_type = type;
    report.home_team_id = home;
    report.away_team_id = away;
    discipline.processMatch(report, gamedata);
  };

  play(MatchType::LEAGUE, 30, 40);  // Neither club involved.
  play(MatchType::LEAGUE, 20, 30);  // League match does not serve a cup ban.
  EXPECT_EQ(discipline.banMatches(1, MatchType::LEAGUE), 2);
  EXPECT_EQ(discipline.banMatches(3, MatchType::CUP), 1);
  ASSERT_EQ(discipline.suspendedPlayers(10, gamedata).size(), 1U);

  play(MatchType::LEAGUE, 40, 10);
  play(MatchType::CUP, 20, 40);
  EXPECT_EQ(discipline.banMatches(1, MatchType::LEAGUE), 1);
  EXPECT_FALSE(discipline.isSuspended(3, MatchType::CUP));
  EXPECT_TRUE(discipline.suspendedPlayers(20, gamedata).empty());

  play(MatchType::LEAGUE, 10, 30);
  play(MatchType::LEAGUE, 10, 30);  // Nothing left to serve.
  EXPECT_FALSE(discipline.isSuspended(1, MatchType::LEAGUE));
  EXPECT_TRUE(discipline.suspendedPlayers(10, gamedata).empty());
  EXPECT_EQ(discipline.records().size(), 3U);
  discipline.resetSeason();
  EXPECT_TRUE(discipline.records().empty());
}

// ---------------------------------------------------------------------------
// Opt-in timing. FM_SEASON_TIMING=1 [FM_SEASON_THREADS=n] fm-test -R Timing
// (the test environment pins FM_SIM_THREADS=2; FM_SEASON_THREADS overrides).
// ---------------------------------------------------------------------------

namespace
{
unsigned timingThreads()
{
  if (const char* configured = std::getenv("FM_SEASON_THREADS"))
    return static_cast<unsigned>(std::max(1, std::atoi(configured)));
  return ThreadPool::defaultThreadCount();
}
}  // namespace

TEST(MatchSchedulerTest, MatchdayTiming)
{
  if (!std::getenv("FM_SEASON_TIMING"))
    GTEST_SKIP() << "set FM_SEASON_TIMING=1 to time a matchday";
  const SlotCleanup slot{uniqueSlot(4)};
  const auto controller = makeWorld(slot.slot);
  const Calendar& calendar = controller->getGame()->getCalendar();

  // League rounds are spread from Friday to Monday: the Continue that
  // matters is the busiest day of a round, not the whole round.
  const auto leagueMatches = [](const std::vector<Match>& matches)
  {
    return std::ranges::count_if(
        matches, [](const Match& match)
        { return match.getMatchType() == MatchType::LEAGUE; });
  };
  const GameDateValue first_round = SeasonCalendar::leagueStart(
      SeasonCalendar::seasonStartYear(controller->getCurrentDate()));
  std::optional<GameDateValue> matchday;
  for (int offset = -1; offset <= 2; ++offset)
  {
    const GameDateValue day = SeasonCalendar::addDays(first_round, offset);
    const auto& matches = calendar.getMatchesForDate(day);
    std::cout << "[matchday] first round " << day.toString() << ": "
              << matches.size() << " matches\n";
    if (!matchday ||
        matches.size() > calendar.getMatchesForDate(*matchday).size())
      matchday = day;
  }
  ASSERT_TRUE(matchday);
  advanceUntil(*controller, SeasonCalendar::addDays(*matchday, -1));
  // Busiest days still ahead (the season's worst league day is a short
  // weekend: no Monday before an international window).
  std::optional<GameDateValue> season_busiest;
  std::optional<GameDateValue> busiest_day;
  for (const auto& [date, matches] : calendar.getFullCalendar())
  {
    if (date < *matchday) continue;
    if (!season_busiest ||
        leagueMatches(matches) >
            leagueMatches(calendar.getMatchesForDate(*season_busiest)))
      season_busiest = date;
    if (!busiest_day ||
        matches.size() > calendar.getMatchesForDate(*busiest_day).size())
      busiest_day = date;
  }
  ASSERT_TRUE(season_busiest && busiest_day);

  const unsigned threads = timingThreads();
  const auto time = [&](const GameDateValue& date, const char* label)
  {
    const std::vector<MatchSimulationInput> inputs =
        prepareDay(*controller, date);
    const auto run = [&](unsigned workers)
    {
      MatchScheduler scheduler(workers);
      const auto started = std::chrono::steady_clock::now();
      const auto results = scheduler.run(inputs, controller->getStatsConfig());
      EXPECT_EQ(results.size(), inputs.size());
      return std::chrono::duration<double, std::milli>(
                 std::chrono::steady_clock::now() - started)
          .count();
    };
    const double sequential_ms = run(1);
    const double parallel_ms = run(threads);
    std::cout << "[matchday] " << label << " " << date.toString() << ", "
              << inputs.size() << " matches: 1 thread " << sequential_ms
              << " ms, " << threads << " threads " << parallel_ms << " ms\n";
  };
  time(*matchday, "busiest day of the first round");
  time(*season_busiest, "busiest league day of the season");
  time(*busiest_day, "busiest day ahead (any competition)");

  controller->setSimulationThreads(threads);
  const auto started = std::chrono::steady_clock::now();
  controller->advanceDay();
  std::cout << "[matchday] full advanceDay of " << matchday->toString()
            << " with " << threads << " threads: "
            << std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - started)
                   .count()
            << " ms\n";
}

TEST(MatchSchedulerTest, SeasonTiming)
{
  if (!std::getenv("FM_SEASON_TIMING"))
    GTEST_SKIP() << "set FM_SEASON_TIMING=1 to time a full season";
  const SlotCleanup slot{uniqueSlot(5)};
  const auto controller = makeWorld(slot.slot);
  const unsigned threads = timingThreads();
  controller->setSimulationThreads(threads);
  const auto started = std::chrono::steady_clock::now();
  for (int day = 0; day < 365; ++day) controller->advanceDay();
  std::cout << "[season] 365 days with " << threads << " threads: "
            << std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                             started)
                   .count()
            << " s\n";
}

// ---------------------------------------------------------------------------
// League match context
// ---------------------------------------------------------------------------

// Each league plays in its own style: Portuguese referees book more than
// French ones, Argentina scores less than Germany, home advantage is strong
// in Brazil's Serie B and weak in Portugal's Liga 2, and second tiers have
// more variable referees. Unknown leagues keep the calibrated engine.
TEST(MatchContextTest, LeaguesCarryTheirOwnMatchStyle)
{
  constexpr LeagueID ENGLAND = 3;
  constexpr LeagueID ENGLAND_2 = 14;
  constexpr LeagueID GERMANY = 4;
  constexpr LeagueID FRANCE = 5;
  constexpr LeagueID PORTUGAL = 12;
  constexpr LeagueID PORTUGAL_2 = 22;
  constexpr LeagueID ARGENTINA = 10;
  constexpr LeagueID BRAZIL_2 = 21;

  const MatchContext neutral;
  for (const LeagueID league : {LeagueID{0}, LeagueID{250}})
  {
    const MatchContext context = MatchSimulation::leagueContext(league);
    EXPECT_FLOAT_EQ(context.goalRateScale, neutral.goalRateScale);
    EXPECT_FLOAT_EQ(context.refereeStrictnessMean,
                    neutral.refereeStrictnessMean);
    EXPECT_FLOAT_EQ(context.refereeStrictnessSd, neutral.refereeStrictnessSd);
    EXPECT_FLOAT_EQ(context.homeAdvantageScale, neutral.homeAdvantageScale);
  }

  const auto context = [](LeagueID league)
  { return MatchSimulation::leagueContext(league); };
  EXPECT_GT(context(PORTUGAL).refereeStrictnessMean,
            1.3f * context(FRANCE).refereeStrictnessMean);
  EXPECT_LT(context(ENGLAND).refereeStrictnessMean, 1.0f);
  // Goals grow faster than the scale (it sharpens shooting): 3.19 against
  // 2.05 goals a match is about a 1.4x scale.
  EXPECT_GT(context(GERMANY).goalRateScale,
            1.25f * context(ARGENTINA).goalRateScale);
  EXPECT_GT(context(BRAZIL_2).homeAdvantageScale,
            1.5f * context(PORTUGAL_2).homeAdvantageScale);
  EXPECT_GT(context(ENGLAND_2).refereeStrictnessSd,
            1.5f * context(ENGLAND).refereeStrictnessSd);
  // Each tier has its own neutral goal level, so compare the goals the
  // contexts aim for: the Championship scores less than the Premier League.
  using Reference = WorldTuning::MatchContext;
  const auto expectedGoals = [&](LeagueID league, float neutralGoals)
  {
    return neutralGoals * std::pow(context(league).goalRateScale,
                                   Reference::GOAL_RESPONSE_EXPONENT);
  };
  EXPECT_LT(expectedGoals(ENGLAND_2, Reference::REFERENCE_GOALS_SECOND),
            0.9f * expectedGoals(ENGLAND, Reference::REFERENCE_GOALS_TOP));

  // Every league's context is inside the engine's accepted ranges.
  for (const LeagueProfile& profile : LEAGUE_PROFILES)
  {
    const MatchContext value = context(profile.league_id);
    EXPECT_GE(value.goalRateScale, MatchTuning::Context::MIN_GOAL_RATE_SCALE);
    EXPECT_LE(value.goalRateScale, MatchTuning::Context::MAX_GOAL_RATE_SCALE);
    EXPECT_GE(value.refereeStrictnessMean,
              MatchTuning::Context::MIN_REFEREE_STRICTNESS);
    EXPECT_LE(value.refereeStrictnessMean,
              MatchTuning::Context::MAX_REFEREE_STRICTNESS);
    EXPECT_LE(value.refereeStrictnessSd, MatchTuning::Context::MAX_REFEREE_SD);
    EXPECT_LE(value.homeAdvantageScale,
              MatchTuning::Context::MAX_HOME_ADVANTAGE_SCALE);
  }
}

TEST(MatchContextTest, ExplicitContextReplacesLeagueAndCanBeCleared)
{
  constexpr LeagueID PORTUGAL = 12;
  const SlotCleanup slot{uniqueSlot(8)};
  const auto controller = makeWorld(slot.slot);
  const auto day = firstBusyDay(*controller, 2);
  ASSERT_TRUE(day);
  auto inputs = prepareDay(*controller, *day);
  ASSERT_FALSE(inputs.empty());
  auto input = inputs.front();
  const auto& config = controller->getGameData()->getStatsConfig();
  const auto simulate = [&]
  { return describe(MatchSimulation::run(input, config)); };

  input.league_id = 0;
  const auto defaults = simulate();
  input.league_id = PORTUGAL;
  const auto league = simulate();
  ASSERT_NE(defaults, league);

  // An explicit neutral context must win over a known league's settings.
  input.context_override = MatchContext{};
  EXPECT_EQ(simulate(), defaults);

  // Supplying the league's own context leaves its seeded result unchanged.
  input.context_override = MatchSimulation::leagueContext(input.league_id);
  EXPECT_EQ(simulate(), league);

  input.context_override.reset();
  EXPECT_EQ(simulate(), league);
}

TEST(MatchContextTest, CustomContextsAreValidatedAndIndependentAcrossWorkers)
{
  constexpr float SHARPER_FINISHING = 1.2F;
  constexpr float STRICTER_REFEREE = 1.3F;
  constexpr float EXCESSIVE_FINISHING = 50.0F;
  constexpr float NEGATIVE_HOME_EDGE = -10.0F;
  const SlotCleanup slot{uniqueSlot(9)};
  const auto controller = makeWorld(slot.slot);
  const auto day = firstBusyDay(*controller, 2);
  ASSERT_TRUE(day);
  auto inputs = prepareDay(*controller, *day);
  ASSERT_GE(inputs.size(), 2U);
  inputs.resize(2);
  const auto& config = controller->getGameData()->getStatsConfig();

  MatchContext custom;
  custom.goalRateScale = SHARPER_FINISHING;
  custom.refereeStrictnessMean = STRICTER_REFEREE;
  custom.refereeStrictnessSd = 0.0F;
  custom.homeAdvantageScale = 0.0F;
  inputs.at(0).context_override = custom;
  MatchContext bounded;
  bounded.goalRateScale = MatchTuning::Context::MAX_GOAL_RATE_SCALE;
  bounded.homeAdvantageScale = 0.0F;
  inputs.at(1).context_override = bounded;

  MatchScheduler sequential(1);
  const auto expected = sequential.run(inputs, config);
  // Finite extremes are clamped and non-finite settings use engine defaults.
  MatchContext invalid;
  invalid.goalRateScale = EXCESSIVE_FINISHING;
  invalid.homeAdvantageScale = NEGATIVE_HOME_EDGE;
  invalid.refereeStrictnessMean = std::numeric_limits<float>::quiet_NaN();
  inputs.at(1).context_override = invalid;
  MatchScheduler parallel(2);
  const auto actual = parallel.run(inputs, config);
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i)
  {
    EXPECT_EQ(describe(actual.at(i)), describe(expected.at(i)))
        << "fixture " << i;
  }
}

// Domestic league fixtures retain their league id without opting into
// overrides.
TEST(MatchContextTest, LeagueFixturesCarryTheirLeague)
{
  const SlotCleanup slot{uniqueSlot(7)};
  const auto controller = makeWorld(slot.slot);
  bool league_seen = false;
  bool friendly_seen = false;
  for (const auto& [date, matches] :
       controller->getGame()->getCalendar().getFullCalendar())
  {
    for (const Match& match : matches)
    {
      const auto input = match.prepareSimulation(*controller->getGameData());
      if (!input) continue;
      if (match.getMatchType() == MatchType::LEAGUE)
      {
        EXPECT_EQ(input->league_id, match.getCompetitionId());
        league_seen = true;
      }
      else if (match.getMatchType() == MatchType::FRIENDLY)
      {
        EXPECT_EQ(input->league_id, 0);
        friendly_seen = true;
      }
    }
    if (league_seen && friendly_seen) break;
  }
  EXPECT_TRUE(league_seen);
  EXPECT_TRUE(friendly_seen);
}
