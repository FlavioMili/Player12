// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/opposition_report.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

#include "model/data_hub.h"
#include "model/world_rng.h"

namespace
{
namespace R = OppositionRules;

constexpr std::array<const char*,
                     std::to_underlying(OppositionInstruction::COUNT)>
    INSTRUCTION_KEYS = {
        "OPPOSITION_INSTRUCTION_NONE", "OPPOSITION_INSTRUCTION_TIGHT",
        "OPPOSITION_INSTRUCTION_PRESS", "OPPOSITION_INSTRUCTION_WEAK_FOOT",
        "OPPOSITION_INSTRUCTION_DOUBLE"};

std::string oneDecimal(float value) { return std::format("{:.1f}", value); }
std::string twoDecimals(float value) { return std::format("{:.2f}", value); }
std::string whole(float value) { return std::format("{:.0f}", value); }

void accumulate(OppositionAverages& totals, const MatchReport& report,
                bool home)
{
  const TeamMatchStats& own = home ? report.home_stats : report.away_stats;
  const TeamMatchStats& other = home ? report.away_stats : report.home_stats;
  totals.goals_for += home ? report.home_goals : report.away_goals;
  totals.goals_against += home ? report.away_goals : report.home_goals;
  totals.xg_for += own.expected_goals;
  totals.xg_against += other.expected_goals;
  totals.shots_for += own.shots;
  totals.shots_against += other.shots;
  totals.possession += own.possession;
  totals.pass_completion += own.passes_attempted > 0
                                ? 100.0f *
                                      static_cast<float>(own.passes_completed) /
                                      static_cast<float>(own.passes_attempted)
                                : 0.0f;
  ++totals.matches;
}

OppositionAverages averaged(OppositionAverages totals)
{
  if (totals.matches == 0)
  {
    totals.possession = 50.0f;
    return totals;
  }
  const auto n = static_cast<float>(totals.matches);
  totals.goals_for /= n;
  totals.goals_against /= n;
  totals.xg_for /= n;
  totals.xg_against /= n;
  totals.shots_for /= n;
  totals.shots_against /= n;
  totals.possession /= n;
  totals.pass_completion /= n;
  return totals;
}

float ratio(float value, float reference)
{
  return reference > 0.0f ? value / reference : 1.0f;
}

bool isDefender(PlayerRole role)
{
  using enum PlayerRole;
  return role == CB || role == LB || role == RB;
}

bool isForward(PlayerRole role)
{
  using enum PlayerRole;
  return role == LW || role == RW || role == ST;
}

float threatScore(const OppositionPlayer& player)
{
  const float rating = player.appearances >= 2 && player.average_rating > 0.0f
                           ? std::max(0.0f, player.average_rating - 6.5f) * 2.0f
                           : 0.0f;
  return static_cast<float>(player.goals) +
         0.7f * static_cast<float>(player.assists) + rating +
         (player.estimate > 0.0f ? player.estimate / 100.0f : 0.0f);
}
}  // namespace

const char* oppositionInstructionKey(OppositionInstruction instruction)
{
  const auto index = std::to_underlying(instruction);
  return index < INSTRUCTION_KEYS.size() ? INSTRUCTION_KEYS[index] : "";
}

void OppositionPlan::set(TeamID opponent, PlayerID player,
                         OppositionInstruction instruction)
{
  std::erase_if(
      orders, [opponent, player](const OppositionOrder& order)
      { return order.opponent == opponent && order.player == player; });
  if (instruction == OppositionInstruction::None ||
      instruction >= OppositionInstruction::COUNT)
    return;
  orders.push_back({opponent, player, instruction});
}

OppositionInstruction OppositionPlan::get(TeamID opponent,
                                          PlayerID player) const
{
  const auto found = std::ranges::find_if(
      orders, [opponent, player](const OppositionOrder& order)
      { return order.opponent == opponent && order.player == player; });
  return found != orders.end() ? found->instruction
                               : OppositionInstruction::None;
}

std::vector<OppositionOrder> OppositionPlan::forOpponent(TeamID opponent) const
{
  std::vector<OppositionOrder> result;
  for (const OppositionOrder& order : orders)
    if (order.opponent == opponent) result.push_back(order);
  return result;
}

void OppositionPlan::clear(TeamID opponent)
{
  std::erase_if(orders, [opponent](const OppositionOrder& order)
                { return order.opponent == opponent; });
}

void OppositionPlan::restore(const std::vector<OppositionOrder>& restored)
{
  orders.clear();
  for (const OppositionOrder& order : restored)
    set(order.opponent, order.player, order.instruction);
}

std::string formationLabel(std::span<const PlayerRole> starters)
{
  using enum PlayerRole;
  int defenders = 0;
  int midfielders = 0;
  int forwards = 0;
  for (const PlayerRole role : starters)
  {
    if (role == GK || role == UNKNOWN) continue;
    if (isDefender(role))
      ++defenders;
    else if (isForward(role))
      ++forwards;
    else
      ++midfielders;
  }
  if (defenders + midfielders + forwards == 0) return {};
  return std::format("{}-{}-{}", defenders, midfielders, forwards);
}

OppositionReport buildOppositionReport(const OppositionInput& input)
{
  OppositionReport report;
  report.opponent = input.opponent;

  // Recent form and averages from the opponent's latest reports.
  std::vector<const MatchReport*> played;
  for (const MatchReport& match : input.reports)
  {
    if ((match.home_team_id == input.opponent ||
         match.away_team_id == input.opponent) &&
        DataHub::hasStatistics(match))
      played.push_back(&match);
  }
  std::ranges::sort(played, {}, [](const MatchReport* match)
                    { return dayOrdinal(match->date); });
  if (played.size() > R::RECENT_MATCHES)
    played.erase(played.begin(),
                 played.end() - static_cast<std::ptrdiff_t>(R::RECENT_MATCHES));
  OppositionAverages recent;
  int points = 0;
  for (const MatchReport* match : played)
  {
    const bool home = match->home_team_id == input.opponent;
    accumulate(recent, *match, home);
    const int scored = home ? match->home_goals : match->away_goals;
    const int conceded = home ? match->away_goals : match->home_goals;
    int outcome = 0;
    if (scored > conceded)
      outcome = 1;
    else if (scored < conceded)
      outcome = -1;
    report.form.push_back(outcome);
    if (outcome > 0)
      points += 3;
    else if (outcome == 0)
      ++points;
  }
  report.recent = averaged(recent);

  OppositionAverages league;
  for (const MatchReport& match : input.league_reports)
  {
    if (match.match_type != MatchType::LEAGUE || !DataHub::hasStatistics(match))
      continue;
    accumulate(league, match, true);
    accumulate(league, match, false);
  }
  report.league = averaged(league);
  report.enough_data =
      report.recent.matches >= R::MIN_MATCHES && report.league.matches > 0;

  // Likely XI, formation and scouting confidence.
  std::vector<PlayerRole> roles;
  float knowledge = 0.0f;
  for (const OppositionPlayer& player : input.squad)
  {
    if (!player.likely_starter) continue;
    report.likely_xi.push_back(player);
    roles.push_back(player.role);
    knowledge += player.knowledge;
  }
  report.formation = formationLabel(roles);
  if (!report.likely_xi.empty())
    report.confidence = knowledge / static_cast<float>(report.likely_xi.size());

  // Key players: goals, assists and ratings this season.
  std::vector<const OppositionPlayer*> ranked;
  for (const OppositionPlayer& player : input.squad) ranked.push_back(&player);
  std::ranges::stable_sort(ranked, std::greater{}, [](const auto* player)
                           { return threatScore(*player); });
  int team_goals = 0;
  for (const OppositionPlayer& player : input.squad) team_goals += player.goals;
  for (const OppositionPlayer* player : ranked)
  {
    if (report.key_players.size() >= R::KEY_PLAYERS) break;
    if (player->appearances == 0) continue;
    KeyOpponent key;
    key.player = *player;
    key.reason = {
        "OPPOSITION_KEY_REASON",
        {std::to_string(player->goals), std::to_string(player->assists),
         std::to_string(player->appearances),
         player->average_rating > 0.0f ? oneDecimal(player->average_rating)
                                       : std::string("-")}};
    report.key_players.push_back(std::move(key));
  }

  if (!report.enough_data)
  {
    report.counters.push_back({{"OPPOSITION_COUNTER_NO_DATA", {}},
                               {"OPPOSITION_REASON_NO_DATA",
                                {std::to_string(report.recent.matches)}}});
    return report;
  }

  // Strengths and weaknesses against the league average.
  const OppositionAverages& r = report.recent;
  const OppositionAverages& l = report.league;
  const std::string sample = std::to_string(r.matches);
  if (ratio(r.xg_for, l.xg_for) >= R::HIGH_RATIO)
    report.strengths.push_back(
        {"OPPOSITION_STRENGTH_ATTACK",
         {twoDecimals(r.xg_for), twoDecimals(l.xg_for), sample}});
  else if (ratio(r.xg_for, l.xg_for) <= R::LOW_RATIO)
    report.weaknesses.push_back(
        {"OPPOSITION_WEAKNESS_ATTACK",
         {twoDecimals(r.xg_for), twoDecimals(l.xg_for), sample}});
  if (ratio(r.xg_against, l.xg_against) <= R::LOW_RATIO)
    report.strengths.push_back(
        {"OPPOSITION_STRENGTH_DEFENCE",
         {twoDecimals(r.xg_against), twoDecimals(l.xg_against), sample}});
  else if (ratio(r.xg_against, l.xg_against) >= R::HIGH_RATIO)
    report.weaknesses.push_back(
        {"OPPOSITION_WEAKNESS_DEFENCE",
         {twoDecimals(r.xg_against), twoDecimals(l.xg_against), sample}});
  if (r.pass_completion >= l.pass_completion + R::PASSING_MARGIN)
    report.strengths.push_back(
        {"OPPOSITION_STRENGTH_PASSING",
         {whole(r.pass_completion), whole(l.pass_completion)}});
  else if (r.pass_completion <= l.pass_completion - R::PASSING_MARGIN)
    report.weaknesses.push_back(
        {"OPPOSITION_WEAKNESS_PASSING",
         {whole(r.pass_completion), whole(l.pass_completion)}});
  if (const int max_points = 3 * r.matches;
      r.matches >= 3 && points * 3 >= max_points * 2)
    report.strengths.push_back(
        {"OPPOSITION_STRENGTH_FORM",
         {std::to_string(points), std::to_string(max_points)}});
  else if (r.matches >= 3 && points * 3 <= max_points)
    report.weaknesses.push_back(
        {"OPPOSITION_WEAKNESS_FORM",
         {std::to_string(points), std::to_string(max_points)}});

  // Counter-tactics, strongest evidence first.
  const auto counter = [&report](AnalysisLine action, AnalysisLine reason,
                                 StrategySliders shift, PlayerID mark = 0)
  {
    if (report.counters.size() < R::MAX_COUNTERS)
      report.counters.emplace_back(std::move(action), std::move(reason), shift,
                                   mark);
  };
  if (!report.key_players.empty() && team_goals >= R::MAIN_THREAT_MIN_GOALS)
  {
    const OppositionPlayer& threat = report.key_players.front().player;
    const float share =
        static_cast<float>(threat.goals) / static_cast<float>(team_goals);
    if (share >= R::MAIN_THREAT_SHARE)
      counter({"OPPOSITION_COUNTER_MARK", {threat.name}},
              {"OPPOSITION_REASON_MARK",
               {threat.name, std::to_string(threat.goals),
                std::to_string(team_goals)}},
              {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, threat.player);
  }
  if (r.pass_completion <= l.pass_completion - R::PASSING_MARGIN)
    counter({"OPPOSITION_COUNTER_PRESS", {}},
            {"OPPOSITION_REASON_PRESS",
             {whole(r.pass_completion), whole(l.pass_completion)}},
            {0.2f, 0.0f, 0.0f, 0.0f, 0.1f});
  if (r.possession >= R::HIGH_POSSESSION)
    counter({"OPPOSITION_COUNTER_COMPACT", {}},
            {"OPPOSITION_REASON_COMPACT", {whole(r.possession), sample}},
            {-0.1f, 0.0f, 0.1f, 0.0f, -0.15f});
  else if (r.possession <= R::LOW_POSSESSION)
    counter({"OPPOSITION_COUNTER_PATIENT", {}},
            {"OPPOSITION_REASON_PATIENT", {whole(r.possession), sample}},
            {0.0f, -0.1f, -0.05f, 0.1f, 0.0f});
  if (ratio(r.xg_against, l.xg_against) >= R::HIGH_RATIO)
    counter({"OPPOSITION_COUNTER_ATTACK", {}},
            {"OPPOSITION_REASON_ATTACK",
             {twoDecimals(r.xg_against), twoDecimals(l.xg_against)}},
            {0.0f, 0.15f, 0.1f, 0.0f, 0.0f});
  if (ratio(r.xg_for, l.xg_for) >= R::HIGH_RATIO)
    counter({"OPPOSITION_COUNTER_PROTECT", {}},
            {"OPPOSITION_REASON_PROTECT",
             {twoDecimals(r.xg_for), twoDecimals(l.xg_for)}},
            {0.0f, -0.1f, 0.0f, 0.0f, -0.1f});
  if (report.counters.empty())
    report.counters.push_back({{"OPPOSITION_COUNTER_NONE", {}},
                               {"OPPOSITION_REASON_NONE", {sample}}});
  return report;
}

Strategy applyCounter(const Strategy& strategy, const CounterTactic& counter)
{
  Strategy result = strategy;
  const StrategySliders current = strategy.getSliders();
  result.setPressing(current.pressing + counter.shift.pressing);
  result.setRiskTaking(current.riskTaking + counter.shift.riskTaking);
  result.setOffensiveBias(current.offensiveBias + counter.shift.offensiveBias);
  result.setWidthUsage(current.widthUsage + counter.shift.widthUsage);
  result.setCompactness(current.compactness + counter.shift.compactness);
  return result;
}
