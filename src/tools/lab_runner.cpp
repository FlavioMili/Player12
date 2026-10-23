// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/lab_runner.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <numeric>
#include <utility>

#include "global/thread_pool.h"
#include "model/match.h"
#include "model/match_engine.h"
#include "model/match_report.h"
#include "model/player.h"
#include "tools/lab_fixtures.h"

namespace Lab
{
namespace
{
/** Headless update step when the engine has no simulateToEnd() (same as
 * MatchSimulation::run). */
constexpr float HEADLESS_STEP_SECONDS = 0.25f;
/** A match needing more updates than this is reported as stuck. */
constexpr int MAX_HEADLESS_UPDATES = 2'000'000;
constexpr TeamID HOME_TEAM_ID = 1;
constexpr TeamID AWAY_TEAM_ID = 2;
constexpr double REGULATION_MINUTES = 90.0;
constexpr double HALF_MINUTES = 45.0;
/** Jobs per parallel batch between progress callbacks. */
constexpr std::size_t PROGRESS_CHUNK = 64;
constexpr int TOP_SCORELINES = 10;
constexpr int HISTOGRAM_GOALS = 8;
/** Starter-rating gaps below this are ties without a stronger side. */
constexpr double RATING_TIE = 0.05;
/** Reference goals per match for the Poisson column (CT-M13 target). */
constexpr double REFERENCE_GOALS = 2.83;

template <typename Engine>
void runToFullTime(Engine& engine, std::uint32_t seed, bool background)
{
  if constexpr (requires { engine.simulateToEnd(MatchFidelity::FULL); })
  {
    engine.simulateToEnd(background ? MatchFidelity::BACKGROUND
                                    : MatchFidelity::FULL);
  }
  else if constexpr (requires { engine.simulateToEnd(); })
  {
    engine.simulateToEnd();
  }
  else
  {
    for (int step = 0; engine.getState() != MatchState::FULL_TIME; ++step)
    {
      if (step >= MAX_HEADLESS_UPDATES)
        throw SimulationError(seed, "match did not reach full time");
      engine.update(HEADLESS_STEP_SECONDS);
    }
  }
  if (engine.getState() != MatchState::FULL_TIME)
    throw SimulationError(seed, "match did not reach full time");
}

std::size_t goalBin(int period, double minute)
{
  if (period <= 1)
  {
    if (minute < 15.0) return 0;
    if (minute < 30.0) return 1;
    return 2;
  }
  if (minute < 60.0) return 3;
  if (minute < 75.0) return 4;
  return 5;
}

struct ScoringEvent
{
  double time = 0.0;
  bool goal = false; /*!< Otherwise a sending-off. */
  bool home = false; /*!< Side credited with the goal / sent-off side. */
};

/**
 * Splits the match into intervals between goals and sendings-off and adds,
 * for each team, the minutes and goals by score state and numbers. A goal
 * counts in the state before it.
 */
void accumulateGameStates(MatchSample& sample,
                          std::span<const ScoringEvent> events, double length)
{
  std::array<int, 2> score{};
  std::array<int, 2> sentOff{};
  double previous = 0.0;
  const auto stateOf = [&](std::size_t team) -> int
  {
    const int diff = score[team] - score[1 - team];
    if (diff == 1) return LEAD_BY_ONE;
    if (diff == 0) return LEVEL;
    if (diff == -1) return TRAIL_BY_ONE;
    return -1;
  };
  const auto strengthOf = [&](std::size_t team) -> int
  {
    if (sentOff[team] > sentOff[1 - team]) return MAN_DOWN;
    if (sentOff[team] < sentOff[1 - team]) return MAN_UP;
    return EVEN;
  };
  const auto addMinutes = [&](double until)
  {
    const double span = std::max(0.0, until - previous);
    for (std::size_t team = 0; team < 2; ++team)
    {
      if (const int state = stateOf(team); state >= 0)
        sample.state_minutes[static_cast<std::size_t>(state)] += span;
      sample.strength_minutes[static_cast<std::size_t>(strengthOf(team))] +=
          span;
    }
    previous = std::max(previous, until);
  };
  for (const ScoringEvent& event : events)
  {
    const double time = std::clamp(event.time, previous, length);
    addMinutes(time);
    const std::size_t team = event.home ? 0 : 1;
    if (event.goal)
    {
      if (const int state = stateOf(team); state >= 0)
        ++sample.state_goals[static_cast<std::size_t>(state)];
      ++sample.strength_goals[static_cast<std::size_t>(strengthOf(team))];
      ++score[team];
    }
    else
    {
      ++sentOff[team];
    }
  }
  addMinutes(length);
}

void extractEngine(MatchSample& sample, const MatchEngine& engine)
{
  const MatchStats& stats = engine.getStats();
  sample.detailed = true;
  sample.abandoned = engine.isAbandoned();
  sample.goals = {engine.getHomeScore(), engine.getAwayScore()};
  sample.shots = {stats.homeShots, stats.awayShots};
  sample.on_target = {stats.homeOnTarget, stats.awayOnTarget};
  sample.passes_attempted = {stats.homePassesAttempted,
                             stats.awayPassesAttempted};
  sample.passes_completed = {stats.homePassesCompleted,
                             stats.awayPassesCompleted};
  sample.fouls = {stats.homeFouls, stats.awayFouls};
  sample.xg = {static_cast<double>(stats.homeShotXG),
               static_cast<double>(stats.awayShotXG)};
  sample.home_possession = stats.homePossession;
  sample.corners = stats.homeCorners + stats.awayCorners;
  sample.yellows = stats.homeYellowCards + stats.awayYellowCards;
  sample.reds = stats.homeRedCards + stats.awayRedCards;
  sample.offsides = stats.homeOffsides + stats.awayOffsides;
  sample.inside_box = stats.homeShotsInsideBox + stats.awayShotsInsideBox;
  sample.headed_goals = stats.homeHeadedGoals + stats.awayHeadedGoals;
  sample.set_piece_goals = stats.homeSetPieceGoals + stats.awaySetPieceGoals;
  sample.penalty_goals = stats.homePenaltyGoals + stats.awayPenaltyGoals;
  sample.penalties = stats.homePenalties + stats.awayPenalties;
  sample.substitutions = stats.homeSubstitutions + stats.awaySubstitutions;
  sample.injuries = stats.homeInjuries + stats.awayInjuries;
  sample.added_minutes = engine.getAddedMinutes(1) + engine.getAddedMinutes(2);
  sample.ball_in_play = static_cast<double>(stats.ballInPlayMinutes);
  sample.match_length = static_cast<double>(engine.getElapsedMatchMinutes());
  for (const MatchSubstitution& change : engine.getSubstitutions())
    sample.substitution_minute_sum += static_cast<double>(change.timeMinute);

  // Clock of the second half restarts at 45; convert to elapsed minutes.
  const double secondHalf = std::max(
      0.0, static_cast<double>(engine.getMatchTimeMinutes()) - HALF_MINUTES);
  const double firstHalf = std::max(0.0, sample.match_length - secondHalf);
  std::vector<ScoringEvent> timeline;
  for (const MatchEvent& event : engine.getEvents())
  {
    const auto minute = static_cast<double>(event.timeMinute);
    const double elapsed =
        event.period <= 1 ? minute : firstHalf + (minute - HALF_MINUTES);
    using enum MatchEventType;
    const bool goal = event.type == GOAL;
    const bool ownGoal = event.type == OWN_GOAL;
    if (goal || ownGoal)
    {
      if (ownGoal) ++sample.own_goals;
      if (event.period >= 2 && minute >= REGULATION_MINUTES)
        ++sample.late_goals;
      ++sample.goal_bins[goalBin(event.period, minute)];
      // An own goal counts for the side of the other team.
      const bool home = ownGoal ? !event.isHomeTeam : event.isHomeTeam;
      timeline.push_back({elapsed, true, home});
    }
    else if ((event.type == RED_CARD || event.type == SECOND_YELLOW) &&
             event.hasTeam)
    {
      timeline.push_back({elapsed, false, event.isHomeTeam});
    }
  }
  std::ranges::stable_sort(timeline, {}, &ScoringEvent::time);
  accumulateGameStates(sample, timeline, sample.match_length);

  for (const PlayerMatchStats& entry : engine.getPlayerStats())
  {
    if (const bool fullMatch = entry.started && !entry.substitutedOff &&
                               !entry.sentOff && !entry.injured;
        !fullMatch || entry.role == PlayerRole::GK)
      continue;
    ++sample.full_match_players;
    sample.full_match_distance += static_cast<double>(entry.distanceMetres);
    sample.second_half_distance +=
        static_cast<double>(entry.secondHalfDistanceMetres);
  }
  sample.first_half_distance =
      sample.full_match_distance - sample.second_half_distance;
  sample.first_half_minutes = firstHalf;
  sample.second_half_minutes = secondHalf;
}

bool finite(const MatchSample& sample)
{
  return std::isfinite(sample.xg[0]) && std::isfinite(sample.xg[1]) &&
         std::isfinite(sample.home_possession) &&
         std::isfinite(sample.ball_in_play) &&
         std::isfinite(sample.match_length) &&
         std::isfinite(sample.full_match_distance) &&
         std::isfinite(sample.second_half_distance);
}

double share(std::size_t part, std::size_t whole)
{
  return whole == 0 ? 0.0
                    : static_cast<double>(part) / static_cast<double>(whole);
}

std::string percent(double fraction)
{
  return std::format("{:.1f}%", 100.0 * fraction);
}

std::string ciPercent(const Estimate& estimate)
{
  if (!estimate.valid()) return "-";
  return std::format("{:.1f}% [{:.1f}, {:.1f}]", 100.0 * estimate.value,
                     100.0 * estimate.low, 100.0 * estimate.high);
}

/** Pairs (a, b) with a < b of the presets, in a fixed order. */
std::vector<std::pair<std::size_t, std::size_t>> presetPairs()
{
  std::vector<std::pair<std::size_t, std::size_t>> pairs;
  for (std::size_t a = 0; a < TACTIC_PRESETS.size(); ++a)
    for (std::size_t b = a + 1; b < TACTIC_PRESETS.size(); ++b)
      pairs.emplace_back(a, b);
  return pairs;
}
}  // namespace

std::uint32_t matchSeed(std::uint64_t seed, std::uint64_t index)
{
  // splitmix64 of (seed, index): decorrelated, stable across platforms.
  std::uint64_t z = seed * 0x9E3779B97F4A7C15ULL + index + 1;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  z ^= z >> 31;
  return static_cast<std::uint32_t>(z >> 32);
}

MatchSample simulateMatch(const MatchJob& job, const StatsConfig& config)
{
  std::vector<std::unique_ptr<Player>> pool;
  const Lineup home = buildLabLineup(HOME_TEAM_ID, job.home.rating, pool);
  const Lineup away = buildLabLineup(AWAY_TEAM_ID, job.away.rating, pool);
  Strategy homeStrategy;
  homeStrategy.setAllSliders(job.home.sliders);
  Strategy awayStrategy;
  awayStrategy.setAllSliders(job.away.sliders);

  MatchEngine engine(home, away, homeStrategy, awayStrategy, config, job.seed);
  MatchdaySquad::carryCondition(engine, home);
  MatchdaySquad::carryCondition(engine, away);
  runToFullTime(engine, job.seed, job.background);

  MatchSample sample;
  sample.seed = job.seed;
  sample.rating = {lineupRating(home, config), lineupRating(away, config)};
  extractEngine(sample, engine);
  if (!finite(sample))
    throw SimulationError(job.seed, "match produced non-finite statistics");
  return sample;
}

std::vector<MatchSample> simulateMatches(
    std::span<const MatchJob> jobs, const StatsConfig& config, unsigned threads,
    const std::function<void(std::size_t)>& progress)
{
  std::vector<MatchSample> samples(jobs.size());
  const unsigned count = std::clamp(threads, 1U, ThreadPool::MAX_THREADS);
  std::unique_ptr<ThreadPool> pool;
  if (count > 1) pool = std::make_unique<ThreadPool>(count - 1);
  for (std::size_t begin = 0; begin < jobs.size(); begin += PROGRESS_CHUNK)
  {
    const std::size_t size = std::min(PROGRESS_CHUNK, jobs.size() - begin);
    const auto task = [&samples, &jobs, &config, begin](std::size_t offset)
    {
      const std::size_t index = begin + offset;
      samples[index] = simulateMatch(jobs[index], config);
    };
    if (pool)
      pool->parallelFor(size, task);
    else
      for (std::size_t offset = 0; offset < size; ++offset) task(offset);
    if (progress) progress(begin + size);
  }
  return samples;
}

MatchSample sampleFromReport(const MatchReport& report)
{
  MatchSample sample;
  sample.detailed = false;
  sample.goals = {report.home_goals, report.away_goals};
  const auto side = [&sample](std::size_t index, const TeamMatchStats& stats)
  {
    sample.shots[index] = stats.shots;
    sample.on_target[index] = stats.shots_on_target;
    sample.passes_attempted[index] = stats.passes_attempted;
    sample.passes_completed[index] = stats.passes_completed;
    sample.fouls[index] = stats.fouls;
    sample.xg[index] = static_cast<double>(stats.expected_goals);
    sample.corners += stats.corners;
    sample.yellows += stats.yellow_cards;
    sample.reds += stats.red_cards;
    sample.offsides += stats.offsides;
  };
  side(0, report.home_stats);
  side(1, report.away_stats);
  sample.home_possession = report.home_stats.possession;
  for (const MatchReportEvent& event : report.events)
  {
    if (event.kind != MatchEventKind::GOAL &&
        event.kind != MatchEventKind::OWN_GOAL)
      continue;
    if (event.kind == MatchEventKind::OWN_GOAL) ++sample.own_goals;
    const bool firstHalfAdded =
        event.added_minute > 0 &&
        event.minute - event.added_minute <= static_cast<int>(HALF_MINUTES);
    const int period =
        firstHalfAdded || event.minute <= static_cast<int>(HALF_MINUTES) ? 1
                                                                         : 2;
    const auto minute = static_cast<double>(event.minute);
    if (period == 2 && minute >= REGULATION_MINUTES) ++sample.late_goals;
    ++sample.goal_bins[goalBin(period, minute)];
  }
  return sample;
}

std::map<std::string, Estimate> matchMetrics(
    std::span<const MatchSample> samples)
{
  std::map<std::string, Estimate> metrics;
  std::vector<const MatchSample*> valid;
  std::size_t abandoned = 0;
  bool detailed = !samples.empty();
  for (const MatchSample& sample : samples)
  {
    if (sample.abandoned)
      ++abandoned;
    else
      valid.push_back(&sample);
    detailed = detailed && sample.detailed;
  }
  if (detailed)
    metrics["abandoned_share"] = proportionEstimate(abandoned, samples.size());
  if (valid.empty()) return metrics;

  const std::size_t n = valid.size();
  const auto column = [&](auto extract)
  {
    std::vector<double> values;
    values.reserve(n);
    for (const MatchSample* sample : valid)
      values.push_back(static_cast<double>(extract(*sample)));
    return values;
  };
  const auto count = [&](auto predicate)
  {
    return static_cast<std::size_t>(std::ranges::count_if(
        valid, [&](const MatchSample* sample) { return predicate(*sample); }));
  };

  const std::vector<double> goals =
      column([](const MatchSample& s) { return s.totalGoals(); });
  const std::vector<double> shots =
      column([](const MatchSample& s) { return s.shots[0] + s.shots[1]; });

  // Goals per match: both teams, regulation plus added time.
  metrics["goals_per_match"] = meanEstimate(goals);
  // Home multiplier: sum of home goals / sum of away goals.
  metrics["home_goal_multiplier"] =
      ratioEstimate(column([](const MatchSample& s) { return s.goals[0]; }),
                    column([](const MatchSample& s) { return s.goals[1]; }));
  // Result shares over matches (Wilson intervals).
  metrics["home_win_share"] = proportionEstimate(
      count([](const MatchSample& s) { return s.goals[0] > s.goals[1]; }), n);
  metrics["draw_share"] = proportionEstimate(
      count([](const MatchSample& s) { return s.goals[0] == s.goals[1]; }), n);
  metrics["away_win_share"] = proportionEstimate(
      count([](const MatchSample& s) { return s.goals[0] < s.goals[1]; }), n);
  metrics["goalless_share"] = proportionEstimate(
      count([](const MatchSample& s) { return s.totalGoals() == 0; }), n);
  metrics["one_one_share"] =
      proportionEstimate(count([](const MatchSample& s)
                               { return s.goals[0] == 1 && s.goals[1] == 1; }),
                         n);
  metrics["one_nil_share"] = proportionEstimate(
      count([](const MatchSample& s) { return s.totalGoals() == 1; }), n);
  // Dispersion of total goals: sample variance / mean (Poisson = 1).
  metrics["goals_variance_mean"] = bootstrapEstimate(
      n,
      [&goals](std::span<const std::size_t> indices)
      {
        if (indices.size() < 2) return std::nan("");
        double sum = 0.0;
        for (const std::size_t i : indices) sum += goals[i];
        const double mean = sum / static_cast<double>(indices.size());
        if (mean <= 0.0) return std::nan("");
        double squares = 0.0;
        for (const std::size_t i : indices)
          squares += (goals[i] - mean) * (goals[i] - mean);
        return squares / static_cast<double>(indices.size() - 1) / mean;
      },
      0xB0075747ULL);
  // Per-team counts are per-match totals halved.
  const auto perTeam = [&column](auto extract)
  {
    return meanEstimate(
        column([extract](const MatchSample& s) { return extract(s) / 2.0; }));
  };
  metrics["shots_per_team"] =
      perTeam([](const MatchSample& s) { return s.shots[0] + s.shots[1]; });
  metrics["on_target_per_team"] = perTeam(
      [](const MatchSample& s) { return s.on_target[0] + s.on_target[1]; });
  metrics["fouls_per_team"] =
      perTeam([](const MatchSample& s) { return s.fouls[0] + s.fouls[1]; });
  metrics["offsides_per_team"] =
      perTeam([](const MatchSample& s) { return s.offsides; });
  // Goals per shot: goals credited to a shooter (own goals excluded).
  metrics["goals_per_shot"] = ratioEstimate(
      column([](const MatchSample& s) { return s.totalGoals() - s.own_goals; }),
      shots);
  metrics["on_target_share"] =
      ratioEstimate(column([](const MatchSample& s)
                           { return s.on_target[0] + s.on_target[1]; }),
                    shots);
  // Mean xG of the engine's own shot model per shot.
  metrics["xg_per_shot"] = ratioEstimate(
      column([](const MatchSample& s) { return s.xg[0] + s.xg[1]; }), shots);
  const std::vector<double> attempted =
      column([](const MatchSample& s)
             { return s.passes_attempted[0] + s.passes_attempted[1]; });
  metrics["passes_per_match"] = meanEstimate(attempted);
  metrics["pass_completion"] = ratioEstimate(
      column([](const MatchSample& s)
             { return s.passes_completed[0] + s.passes_completed[1]; }),
      attempted);
  metrics["corners_per_match"] =
      meanEstimate(column([](const MatchSample& s) { return s.corners; }));
  metrics["yellows_per_match"] =
      meanEstimate(column([](const MatchSample& s) { return s.yellows; }));
  metrics["reds_per_match"] =
      meanEstimate(column([](const MatchSample& s) { return s.reds; }));
  metrics["late_goal_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.late_goals; }), goals);
  metrics["own_goal_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.own_goals; }), goals);
  // Possession spread: mean absolute distance of home possession from 50%.
  metrics["possession_spread"] = meanEstimate(column(
      [](const MatchSample& s)
      { return std::abs(static_cast<double>(s.home_possession) - 50.0); }));

  if (!detailed) return metrics;

  metrics["inside_box_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.inside_box; }), shots);
  // Non-penalty set-piece goals: the engine flags penalties as set pieces.
  metrics["set_piece_goal_share"] =
      ratioEstimate(column([](const MatchSample& s)
                           { return s.set_piece_goals - s.penalty_goals; }),
                    goals);
  metrics["headed_goal_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.headed_goals; }), goals);
  const std::vector<double> penalties =
      column([](const MatchSample& s) { return s.penalties; });
  metrics["penalties_per_match"] = meanEstimate(penalties);
  metrics["penalty_conversion"] = ratioEstimate(
      column([](const MatchSample& s) { return s.penalty_goals; }), penalties);
  const std::vector<double> substitutions =
      column([](const MatchSample& s) { return s.substitutions; });
  metrics["subs_per_team"] =
      perTeam([](const MatchSample& s) { return s.substitutions; });
  // Mean clock minute of a substitution (second half runs 45-90+).
  metrics["sub_mean_minute"] = ratioEstimate(
      column([](const MatchSample& s) { return s.substitution_minute_sum; }),
      substitutions);
  metrics["injuries_per_team"] =
      perTeam([](const MatchSample& s) { return s.injuries; });
  metrics["ball_in_play_minutes"] =
      meanEstimate(column([](const MatchSample& s) { return s.ball_in_play; }));
  metrics["match_length_minutes"] =
      meanEstimate(column([](const MatchSample& s) { return s.match_length; }));
  metrics["added_time_minutes"] = meanEstimate(
      column([](const MatchSample& s) { return s.added_minutes; }));
  // Distance: full-match outfield starters, km per player.
  metrics["distance_per_outfielder_km"] = ratioEstimate(
      column([](const MatchSample& s)
             { return s.full_match_distance / 1000.0; }),
      column([](const MatchSample& s) { return s.full_match_players; }));
  // Second-half vs first-half distance per minute of those players, pooled
  // over matches (distance / (half length x players)); bootstrap.
  metrics["second_half_distance_change"] = bootstrapEstimate(
      n,
      [&valid](std::span<const std::size_t> indices)
      {
        double first = 0.0;
        double second = 0.0;
        double firstTime = 0.0;
        double secondTime = 0.0;
        for (const std::size_t i : indices)
        {
          const MatchSample& s = *valid[i];
          const auto players = static_cast<double>(s.full_match_players);
          first += s.first_half_distance;
          second += s.second_half_distance;
          firstTime += s.first_half_minutes * players;
          secondTime += s.second_half_minutes * players;
        }
        if (first <= 0.0 || firstTime <= 0.0 || secondTime <= 0.0)
          return std::nan("");
        return (second / secondTime) / (first / firstTime) - 1.0;
      },
      0xD157ULL);

  // Rate multipliers: goals per team-minute in a state / in the reference
  // state, pooled over matches; bootstrap over matches.
  const auto rateMultiplier = [&](auto minutes, auto goalsOf, std::size_t state,
                                  std::size_t reference, std::uint64_t seed)
  {
    return bootstrapEstimate(
        n,
        [&valid, minutes, goalsOf, state,
         reference](std::span<const std::size_t> indices)
        {
          double stateMinutes = 0.0;
          double referenceMinutes = 0.0;
          double stateGoals = 0.0;
          double referenceGoals = 0.0;
          for (const std::size_t i : indices)
          {
            stateMinutes += minutes(*valid[i])[state];
            referenceMinutes += minutes(*valid[i])[reference];
            stateGoals += goalsOf(*valid[i])[state];
            referenceGoals += goalsOf(*valid[i])[reference];
          }
          if (stateMinutes <= 0.0 || referenceMinutes <= 0.0 ||
              referenceGoals <= 0.0)
            return std::nan("");
          return (stateGoals / stateMinutes) /
                 (referenceGoals / referenceMinutes);
        },
        seed);
  };
  const auto stateMinutes = [](const MatchSample& s) -> const auto&
  { return s.state_minutes; };
  const auto stateGoals = [](const MatchSample& s) -> const auto&
  { return s.state_goals; };
  const auto strengthMinutes = [](const MatchSample& s) -> const auto&
  { return s.strength_minutes; };
  const auto strengthGoals = [](const MatchSample& s) -> const auto&
  { return s.strength_goals; };
  metrics["lead1_goal_multiplier"] =
      rateMultiplier(stateMinutes, stateGoals, LEAD_BY_ONE, LEVEL, 0x1EAD1ULL);
  metrics["trail1_goal_multiplier"] =
      rateMultiplier(stateMinutes, stateGoals, TRAIL_BY_ONE, LEVEL, 0x7EA11ULL);
  metrics["red_penalised_multiplier"] = rateMultiplier(
      strengthMinutes, strengthGoals, MAN_DOWN, EVEN, 0x4EDD0ULL);
  metrics["red_opponent_multiplier"] =
      rateMultiplier(strengthMinutes, strengthGoals, MAN_UP, EVEN, 0x4EDA1ULL);

  // NFR-019: the stronger side (higher starter rating) wins; exact rating
  // ties have no stronger side and are left out.
  static constexpr std::array<std::pair<const char*, std::array<double, 2>>, 4>
      GAP_BANDS = {{{"stronger_win_gap_lt3", {0.0, 3.0}},
                    {"stronger_win_gap_3_8", {3.0, 8.0}},
                    {"stronger_win_gap_8_15", {8.0, 15.0}},
                    {"stronger_win_gap_gt15", {15.0, 1.0e9}}}};
  for (const auto& [key, band] : GAP_BANDS)
  {
    std::size_t matches = 0;
    std::size_t wins = 0;
    for (const MatchSample* sample : valid)
    {
      if (const double gap = std::abs(static_cast<double>(sample->rating[0]) -
                                      static_cast<double>(sample->rating[1]));
          gap < RATING_TIE || gap < band[0] || gap >= band[1])
        continue;
      ++matches;
      const std::size_t strong = sample->rating[0] > sample->rating[1] ? 0 : 1;
      if (sample->goals[strong] > sample->goals[1 - strong]) ++wins;
    }
    if (matches > 0) metrics[key] = proportionEstimate(wins, matches);
  }
  return metrics;
}

std::vector<TextTable> matchTables(std::span<const MatchSample> samples)
{
  std::vector<const MatchSample*> valid;
  for (const MatchSample& sample : samples)
    if (!sample.abandoned) valid.push_back(&sample);
  std::vector<TextTable> tables;
  if (valid.empty()) return tables;
  const std::size_t n = valid.size();

  TextTable histogram{"Total goals per match",
                      {"Goals", "Matches", "Share", "Poisson(2.83) reference"},
                      {}};
  std::array<std::size_t, HISTOGRAM_GOALS> totals{};
  for (const MatchSample* sample : valid)
    ++totals[static_cast<std::size_t>(
        std::min(sample->totalGoals(), HISTOGRAM_GOALS - 1))];
  double poissonTerm = std::exp(-REFERENCE_GOALS);
  double poissonUsed = 0.0;
  for (int goals = 0; goals < HISTOGRAM_GOALS; ++goals)
  {
    const bool last = goals == HISTOGRAM_GOALS - 1;
    const double reference = last ? 1.0 - poissonUsed : poissonTerm;
    poissonUsed += poissonTerm;
    poissonTerm *= REFERENCE_GOALS / (goals + 1);
    histogram.rows.push_back(
        {last ? std::format("{}+", goals) : std::to_string(goals),
         std::to_string(totals[static_cast<std::size_t>(goals)]),
         percent(share(totals[static_cast<std::size_t>(goals)], n)),
         percent(reference)});
  }
  tables.push_back(std::move(histogram));

  std::map<std::pair<int, int>, std::size_t> scorelines;
  for (const MatchSample* sample : valid)
    ++scorelines[{sample->goals[0], sample->goals[1]}];
  std::vector<std::pair<std::pair<int, int>, std::size_t>> sorted(
      scorelines.begin(), scorelines.end());
  std::ranges::stable_sort(sorted, [](const auto& left, const auto& right)
                           { return left.second > right.second; });
  TextTable top{
      "Top scorelines (home-away)", {"Score", "Matches", "Share"}, {}};
  for (std::size_t i = 0;
       i < sorted.size() && i < static_cast<std::size_t>(TOP_SCORELINES); ++i)
    top.rows.push_back(
        {std::format("{}-{}", sorted[i].first.first, sorted[i].first.second),
         std::to_string(sorted[i].second),
         percent(share(sorted[i].second, n))});
  tables.push_back(std::move(top));

  static constexpr std::array<const char*, GOAL_TIME_BINS> BIN_NAMES = {
      "0-15", "16-30", "31-45+", "46-60", "61-75", "76-90+"};
  std::array<std::size_t, GOAL_TIME_BINS> bins{};
  std::size_t goals = 0;
  for (const MatchSample* sample : valid)
    for (std::size_t bin = 0; bin < GOAL_TIME_BINS; ++bin)
    {
      bins[bin] += static_cast<std::size_t>(sample->goal_bins[bin]);
      goals += static_cast<std::size_t>(sample->goal_bins[bin]);
    }
  TextTable timing{"Goal timing", {"Minutes", "Goals", "Share"}, {}};
  for (std::size_t bin = 0; bin < GOAL_TIME_BINS; ++bin)
    timing.rows.push_back({BIN_NAMES[bin], std::to_string(bins[bin]),
                           percent(share(bins[bin], goals))});
  tables.push_back(std::move(timing));
  return tables;
}

TextTable ratingGapTable(std::span<const MatchSample> samples)
{
  static constexpr std::array<std::pair<const char*, std::array<double, 3>>, 4>
      BANDS = {{{"< 3", {0.0, 3.0, 0.0}},
                {"3-8", {3.0, 8.0, 0.0}},
                {"8-15", {8.0, 15.0, 0.0}},
                {"> 15", {15.0, 1.0e9, 0.0}}}};
  static constexpr std::array<const char*, 4> TARGETS = {"38-48%", "45-55%",
                                                         "52-65%", "60-75%"};
  TextTable table{
      "Stronger side by starter-rating gap (NFR-019; exact ties "
      "excluded)",
      {"Gap", "Matches", "Stronger wins (95% CI)", "Draws", "Stronger losses",
       "Target win share"},
      {}};
  for (std::size_t b = 0; b < BANDS.size(); ++b)
  {
    const auto& band = BANDS[b].second;
    std::size_t matches = 0;
    std::size_t wins = 0;
    std::size_t draws = 0;
    for (const MatchSample& sample : samples)
    {
      if (sample.abandoned) continue;
      if (const double gap = std::abs(static_cast<double>(sample.rating[0]) -
                                      static_cast<double>(sample.rating[1]));
          gap < RATING_TIE || gap < band[0] || gap >= band[1])
        continue;
      ++matches;
      const std::size_t strong = sample.rating[0] > sample.rating[1] ? 0 : 1;
      if (sample.goals[strong] > sample.goals[1 - strong])
        ++wins;
      else if (sample.goals[0] == sample.goals[1])
        ++draws;
    }
    table.rows.push_back({BANDS[b].first, std::to_string(matches),
                          ciPercent(proportionEstimate(wins, matches)),
                          percent(share(draws, matches)),
                          percent(share(matches - wins - draws, matches)),
                          TARGETS[b]});
  }
  return table;
}

std::vector<MatchJob> tacticJobs(std::uint64_t seed, int seeds_per_pair,
                                 float rating)
{
  std::vector<MatchJob> jobs;
  const auto pairs = presetPairs();
  const auto perPair = static_cast<std::size_t>(std::max(seeds_per_pair, 0));
  jobs.reserve(pairs.size() * perPair * 2);
  for (std::size_t p = 0; p < pairs.size(); ++p)
  {
    const auto [a, b] = pairs[p];
    for (std::size_t k = 0; k < perPair; ++k)
    {
      const std::uint32_t matchSeedValue = matchSeed(seed, p * perPair + k);
      // Same seed with home and away swapped removes venue and seed luck.
      jobs.push_back({matchSeedValue,
                      {rating, TACTIC_PRESETS[a].sliders},
                      {rating, TACTIC_PRESETS[b].sliders}});
      jobs.push_back({matchSeedValue,
                      {rating, TACTIC_PRESETS[b].sliders},
                      {rating, TACTIC_PRESETS[a].sliders}});
    }
  }
  return jobs;
}

std::array<std::size_t, 2> tacticPresetsOf(std::size_t job_index,
                                           int seeds_per_pair)
{
  const auto pairs = presetPairs();
  const auto perPair = static_cast<std::size_t>(std::max(seeds_per_pair, 1));
  const std::size_t pair = (job_index / 2) / perPair;
  const auto [a, b] = pairs[std::min(pair, pairs.size() - 1)];
  return job_index % 2 == 0 ? std::array<std::size_t, 2>{a, b}
                            : std::array<std::size_t, 2>{b, a};
}

void tacticMetrics(std::span<const MatchSample> samples, int seeds_per_pair,
                   std::map<std::string, Estimate>& metrics,
                   std::vector<TextTable>& tables)
{
  constexpr std::size_t PRESETS = TACTIC_PRESETS.size();
  struct PresetTotals
  {
    std::vector<double> points;
    int wins = 0;
    int draws = 0;
    int losses = 0;
    double goals_for = 0.0;
    double goals_against = 0.0;
    double shots = 0.0;
    double possession = 0.0;
    double fouls = 0.0;
    double passes_attempted = 0.0;
    double passes_completed = 0.0;
  };
  std::array<PresetTotals, PRESETS> totals{};
  std::array<std::array<double, PRESETS>, PRESETS> headPoints{};
  std::array<std::array<int, PRESETS>, PRESETS> headMatches{};

  for (std::size_t i = 0; i < samples.size(); ++i)
  {
    const MatchSample& sample = samples[i];
    if (sample.abandoned) continue;
    const auto presets = tacticPresetsOf(i, seeds_per_pair);
    for (std::size_t side = 0; side < 2; ++side)
    {
      PresetTotals& preset = totals[presets[side]];
      const int own = sample.goals[side];
      const int other = sample.goals[1 - side];
      double points = 0.0;
      if (own > other)
        points = 3.0;
      else if (own == other)
        points = 1.0;
      preset.points.push_back(points);
      (own > other    ? preset.wins
       : own == other ? preset.draws
                      : preset.losses) += 1;
      preset.goals_for += own;
      preset.goals_against += other;
      preset.shots += sample.shots[side];
      if (side == 0)
        preset.possession += static_cast<double>(sample.home_possession);
      else
        preset.possession +=
            100.0 - static_cast<double>(sample.home_possession);
      preset.fouls += sample.fouls[side];
      preset.passes_attempted += sample.passes_attempted[side];
      preset.passes_completed += sample.passes_completed[side];
      headPoints[presets[side]][presets[1 - side]] += points;
      ++headMatches[presets[side]][presets[1 - side]];
    }
  }

  TextTable table{"Tactic presets round robin (equal teams, paired seeds)",
                  {"Preset", "Matches", "W-D-L", "Points share (95% CI)",
                   "GF/match", "GA/match", "Shots/match", "Possession",
                   "Fouls/match", "Pass completion"},
                  {}};
  Estimate best;
  for (std::size_t p = 0; p < PRESETS; ++p)
  {
    const PresetTotals& preset = totals[p];
    const std::vector maxPoints(preset.points.size(), 3.0);
    const Estimate pointsShare = ratioEstimate(preset.points, maxPoints);
    if (pointsShare.valid() &&
        (!best.valid() || pointsShare.value > best.value))
      best = pointsShare;
    const auto matches = static_cast<double>(preset.points.size());
    const auto perMatch = [&](double value)
    { return matches > 0.0 ? std::format("{:.2f}", value / matches) : "-"; };
    table.rows.push_back(
        {std::string(TACTIC_PRESETS[p].name),
         std::to_string(preset.points.size()),
         std::format("{}-{}-{}", preset.wins, preset.draws, preset.losses),
         ciPercent(pointsShare), perMatch(preset.goals_for),
         perMatch(preset.goals_against), perMatch(preset.shots),
         matches > 0.0 ? std::format("{:.1f}%", preset.possession / matches)
                       : "-",
         perMatch(preset.fouls),
         preset.passes_attempted > 0.0
             ? percent(preset.passes_completed / preset.passes_attempted)
             : "-"});
  }
  metrics["max_preset_points_share"] = best;
  tables.push_back(std::move(table));

  TextTable matrix{
      "Head to head: points per match of the row preset", {"Preset"}, {}};
  for (const TacticPreset& preset : TACTIC_PRESETS)
    matrix.header.emplace_back(preset.name);
  for (std::size_t row = 0; row < PRESETS; ++row)
  {
    std::vector cells{std::string(TACTIC_PRESETS[row].name)};
    for (std::size_t col = 0; col < PRESETS; ++col)
      cells.push_back(headMatches[row][col] == 0
                          ? "-"
                          : std::format("{:.2f}", headPoints[row][col] /
                                                      headMatches[row][col]));
    matrix.rows.push_back(std::move(cells));
  }
  tables.push_back(std::move(matrix));
}
}  // namespace Lab
