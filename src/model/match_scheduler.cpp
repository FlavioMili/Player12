// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/match_scheduler.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "model/match.h"
#include "model/match_engine.h"
#include "model/world_tuning.h"

namespace
{
uint8_t toGoals(int goals)
{
  return static_cast<uint8_t>(std::clamp(
      goals, 0, static_cast<int>(std::numeric_limits<uint8_t>::max())));
}
}  // namespace

MatchContext MatchSimulation::leagueContext(LeagueID league_id)
{
  using Reference = WorldTuning::MatchContext;
  const LeagueProfile& profile = leagueProfile(league_id);
  MatchContext context;
  if (league_id == 0 || profile.league_id != league_id) return context;
  const LeagueMatchStyle& style = profile.match_style;
  // Second divisions are the rows without continental income. A league of
  // more unequal clubs scores more at the same scale (more mismatches).
  const bool top = profile.continental_share > 0.0f;
  const float spread =
      profile.shape.level_sd - (top ? Reference::REFERENCE_SPREAD_TOP
                                    : Reference::REFERENCE_SPREAD_SECOND);
  const float reference =
      (top ? Reference::REFERENCE_GOALS_TOP
           : Reference::REFERENCE_GOALS_SECOND) *
      std::max(0.5f, 1.0f + Reference::GOALS_PER_SPREAD_POINT * spread);
  context.goalRateScale = std::pow(style.goals / reference,
                                   1.0f / Reference::GOAL_RESPONSE_EXPONENT);
  context.refereeStrictnessMean =
      style.yellow_cards / Reference::REFERENCE_YELLOWS;
  // Referees differ more in lower tiers: the per-match spread follows the
  // league's spread of referee averages around the engine's calibrated one.
  context.refereeStrictnessSd = context.refereeStrictnessSd *
                                style.referee_spread /
                                Reference::REFERENCE_REFEREE_SPREAD;
  context.homeAdvantageScale =
      std::clamp(std::sqrt(std::max(0.0f, style.home_edge) /
                           Reference::REFERENCE_HOME_EDGE),
                 Reference::MIN_HOME_SCALE, Reference::MAX_HOME_SCALE);
  return context;
}

MatchSimulationResult MatchSimulation::run(const MatchSimulationInput& input,
                                           const StatsConfig& config)
{
  MatchEngine engine(input.home_lineup, input.away_lineup, input.home_strategy,
                     input.away_strategy, config, input.seed);
  // Only knockout fixtures opt into extra time and shootout resolution.
  if (input.knockout.required) engine.setKnockout(input.knockout);
  // Resolve settings once on this engine, before any step or condition carry.
  // An explicit context replaces the whole league profile; absent overrides
  // retain the existing path (including no setter call for league 0).
  if (input.context_override)
  {
    engine.setMatchContext(*input.context_override);
  }
  else if (input.league_id != 0)
  {
    engine.setMatchContext(leagueContext(input.league_id));
  }
  MatchdaySquad::carryCondition(engine, input.home_lineup);
  MatchdaySquad::carryCondition(engine, input.away_lineup);
  for (int half = 1; half <= 2; ++half)
  {
    const auto index = static_cast<std::size_t>(half - 1);
    if (input.home_talk[index] != 0.0f)
      engine.setTeamTalkModifier(true, half, input.home_talk[index]);
    if (input.away_talk[index] != 0.0f)
      engine.setTeamTalkModifier(false, half, input.away_talk[index]);
  }
  for (const auto& [player, flags] : input.medical_flags)
    engine.setMedicalFlags(player, flags);
  // Nobody watches these matches: the background fidelity is enough.
  engine.simulateToEnd(MatchFidelity::BACKGROUND);

  MatchSimulationResult result;
  // Extra time and the shootout are played by the engine: its score
  // includes the extra-time goals, the shootout is reported separately.
  result.report.fillFromEngine(engine, input.home_id, input.away_id);
  result.home_goals = toGoals(engine.getHomeScore());
  result.away_goals = toGoals(engine.getAwayScore());
  result.extra_time = engine.wentToExtraTime();
  if (engine.hasShootout())
    result.penalties.emplace(toGoals(engine.getShootoutScore(true)),
                             toGoals(engine.getShootoutScore(false)));
  result.tie_winner_home = engine.getTieWinnerHome();
  if (result.report.players.empty())
  {
    result.report.addLineupAppearances(input.home_lineup, input.home_id);
    result.report.addLineupAppearances(input.away_lineup, input.away_id);
  }
  result.consequences = MatchdaySquad::consequences(engine);
  return result;
}

MatchScheduler::MatchScheduler(unsigned thread_count)
{
  setThreadCount(thread_count);
}

void MatchScheduler::setThreadCount(unsigned thread_count)
{
  const unsigned clamped =
      std::clamp(thread_count, 1U, ThreadPool::MAX_THREADS);
  if (clamped == threads) return;
  threads = clamped;
  pool.reset();  // Recreated with the new size by the next parallel batch.
}

std::vector<MatchSimulationResult> MatchScheduler::run(
    const std::vector<MatchSimulationInput>& inputs, const StatsConfig& config)
{
  std::vector<MatchSimulationResult> results(inputs.size());
  // Workers write disjoint result slots and read captured inputs. Applying
  // reports to the career belongs to the caller after the whole batch joins;
  // mutating GameData here would race with other fixtures reading players.
  total.fetch_add(static_cast<uint32_t>(inputs.size()),
                  std::memory_order_relaxed);
  const auto simulate = [this, &results, &inputs, &config](std::size_t index)
  {
    results[index] = MatchSimulation::run(inputs[index], config);
    completed.fetch_add(1, std::memory_order_relaxed);
  };

  if (threads <= 1 || inputs.size() <= 1)
  {
    for (std::size_t index = 0; index < inputs.size(); ++index) simulate(index);
    return results;
  }
  if (!pool) pool = std::make_unique<ThreadPool>(threads - 1);
  pool->parallelFor(inputs.size(), simulate);
  return results;
}
