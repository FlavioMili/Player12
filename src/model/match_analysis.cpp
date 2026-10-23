// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/match_analysis.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <utility>

#include "model/match_engine.h"

namespace
{
namespace R = MatchAnalysisRules;

constexpr float FLANK_EDGE = 1.0f / 3.0f;

std::string percent(float share)
{
  return std::format("{:.0f}%", std::round(share * 100.0f));
}

std::string decimal(float value) { return std::format("{:.2f}", value); }

std::string oneDecimal(float value) { return std::format("{:.1f}", value); }

bool isShotOutcome(MatchEventType type)
{
  using enum MatchEventType;
  return type == GOAL || type == SAVE || type == SHOT_BLOCKED ||
         type == SHOT_OFF_TARGET || type == WOODWORK;
}

void fillFromStats(const MatchStats& stats, bool home, SideSummary& side)
{
  side.possession = home ? stats.homePossession : stats.awayPossession;
  side.passes_attempted =
      home ? stats.homePassesAttempted : stats.awayPassesAttempted;
  side.passes_completed =
      home ? stats.homePassesCompleted : stats.awayPassesCompleted;
  side.tackles_attempted =
      home ? stats.homeTackleAttempts : stats.awayTackleAttempts;
  side.tackles_won = home ? stats.homeTackles : stats.awayTackles;
  side.shots = home ? stats.homeShots : stats.awayShots;
  side.shots_on_target = home ? stats.homeOnTarget : stats.awayOnTarget;
  side.xg = home ? stats.homeShotXG : stats.awayShotXG;
  side.box_shots = home ? stats.homeShotsInsideBox : stats.awayShotsInsideBox;
  side.set_piece_shots =
      home ? stats.homeSetPieceShots : stats.awaySetPieceShots;
  side.headed_shots = home ? stats.homeHeadedShots : stats.awayHeadedShots;
}

// Right flank of the defending side is the attacker's left, and so on.
Flank mirrored(Flank flank)
{
  using enum Flank;
  switch (flank)
  {
    case Left:
      return Right;
    case Right:
      return Left;
    case Centre:
      break;
  }
  return Centre;
}

const char* flankKey(Flank flank)
{
  using enum Flank;
  switch (flank)
  {
    case Left:
      return "@ANALYSIS_FLANK_LEFT";
    case Right:
      return "@ANALYSIS_FLANK_RIGHT";
    case Centre:
      break;
  }
  return "@ANALYSIS_FLANK_CENTRE";
}

/** Flank with the largest share of a side's xG, if it dominates. */
std::optional<std::pair<Flank, float>> dominantFlank(const SideSummary& side)
{
  if (side.xg < R::FLANK_MIN_XG || side.shots < R::FLANK_MIN_SHOTS)
    return std::nullopt;
  const auto best = std::ranges::max_element(side.flank_xg);
  const float share = *best / std::max(side.xg, 0.01f);
  if (share < R::FLANK_SHARE) return std::nullopt;
  return std::pair{
      static_cast<Flank>(std::distance(side.flank_xg.begin(), best)), share};
}

void addSuggestion(MatchAnalysis& analysis, AnalysisSuggestion::Kind kind,
                   int priority, AnalysisLine action, AnalysisLine reason,
                   PlayerID player = 0)
{
  analysis.suggestions.emplace_back(kind, std::move(action), std::move(reason),
                                    player, priority);
}
}  // namespace

std::vector<ShotRecord> extractShots(std::span<const MatchEvent> events)
{
  using enum MatchEventType;
  using enum ShotOutcome;
  std::vector<ShotRecord> shots;
  std::optional<std::size_t> open;  // Shot waiting for its outcome.
  for (const MatchEvent& event : events)
  {
    if (event.type == SHOT)
    {
      ShotRecord shot;
      shot.minute = event.timeMinute;
      shot.period = static_cast<std::uint8_t>(std::clamp(event.period, 1, 4));
      shot.home = event.isHomeTeam;
      shot.player = event.primaryPlayerId;
      // Home attacks towards x = 1 with y = 0 on its left; away mirrored.
      shot.x = event.isHomeTeam ? event.position.x : 1.0f - event.position.x;
      shot.y = event.isHomeTeam ? event.position.y : 1.0f - event.position.y;
      shot.xg = event.xg;
      shot.header = event.detail == MatchEventDetail::HEADER;
      shots.push_back(shot);
      open = shots.size() - 1;
      continue;
    }
    if (!open.has_value() || !isShotOutcome(event.type)) continue;
    ShotRecord& shot = shots[*open];
    switch (event.type)
    {
      case GOAL:
        if (event.primaryPlayerId != shot.player) continue;
        shot.outcome = Goal;
        break;
      case SAVE:
        shot.outcome = Saved;
        break;
      case SHOT_BLOCKED:
        shot.outcome = Blocked;
        break;
      case WOODWORK:
        shot.outcome = Woodwork;
        continue;  // The rebound may still be saved or scored.
      default:
        shot.outcome = OffTarget;
        break;
    }
    open.reset();
  }
  return shots;
}

Flank flankOf(const ShotRecord& shot)
{
  using enum Flank;
  if (shot.y < FLANK_EDGE) return Left;
  if (shot.y > 1.0f - FLANK_EDGE) return Right;
  return Centre;
}

float SideSummary::passCompletion() const
{
  return passes_attempted > 0 ? static_cast<float>(passes_completed) /
                                    static_cast<float>(passes_attempted)
                              : 0.0f;
}

float SideSummary::passesAllowedPerAction(int opponent_passes) const
{
  const int actions = tackles_attempted + interceptions;
  return static_cast<float>(opponent_passes) /
         static_cast<float>(std::max(actions, 1));
}

MatchAnalysis analyseMatch(const AnalysisInput& input)
{
  MatchAnalysis analysis;
  analysis.full_time = input.full_time;
  if (input.stats == nullptr) return analysis;
  const bool home = input.managed_home;
  const auto name = [&input](PlayerID id)
  { return input.name_of ? input.name_of(id) : std::string(); };

  SideSummary& own = analysis.own;
  SideSummary& opp = analysis.opponent;
  fillFromStats(*input.stats, home, own);
  fillFromStats(*input.stats, !home, opp);
  for (const ShotRecord& shot : extractShots(input.events))
  {
    SideSummary& side = shot.home == home ? own : opp;
    const auto flank = std::to_underlying(flankOf(shot));
    side.flank_xg[flank] += shot.xg;
    ++side.flank_shots[flank];
  }
  for (const MatchEvent& event : input.events)
  {
    if (event.type == MatchEventType::GOAL)
      ++(event.isHomeTeam == home ? own : opp).goals;
    else if (event.type == MatchEventType::OWN_GOAL)
      ++(event.isHomeTeam == home ? opp : own).goals;
  }
  for (const PlayerMatchStats& line : input.players)
  {
    SideSummary& side = line.isHomeTeam == home ? own : opp;
    side.interceptions += line.interceptions;
    side.aerials_won += line.aerialDuelsWon;
    side.aerials_lost += line.aerialDuelsLost;
  }

  const int totalShots = own.shots + opp.shots;
  analysis.enough_data = totalShots >= R::MIN_SHOTS;
  analysis.tentative = totalShots < R::CONFIDENT_SHOTS;
  const char* sample = "ANALYSIS_SAMPLE_OK";
  if (!analysis.enough_data)
    sample = "ANALYSIS_SAMPLE_TOO_SMALL";
  else if (analysis.tentative)
    sample = "ANALYSIS_SAMPLE_TENTATIVE";
  analysis.sample = {sample,
                     {std::to_string(totalShots),
                      std::to_string(static_cast<int>(input.minute))}};

  // Players: ratings far from the baseline and low condition.
  for (const PlayerMatchStats& line : input.players)
  {
    if (line.isHomeTeam != home || line.minutesPlayed < R::RATING_MIN_MINUTES)
      continue;
    if (line.rating >= R::STANDOUT_RATING)
      analysis.notes.push_back(
          {PlayerNote::Kind::Standout, line.playerId, line.rating});
    else if (line.rating <= R::STRUGGLING_RATING && !line.substitutedOff &&
             !line.sentOff)
      analysis.notes.push_back(
          {PlayerNote::Kind::Struggling, line.playerId, line.rating});
  }
  for (const auto& [player, condition] : input.conditions)
  {
    if (condition < R::TIRED_CONDITION)
      analysis.notes.push_back({PlayerNote::Kind::Tired, player, condition});
  }
  std::ranges::stable_sort(analysis.notes,
                           [](const PlayerNote& a, const PlayerNote& b)
                           { return a.kind < b.kind; });

  // Observations: what happened, with the numbers behind it.
  analysis.observations.push_back(
      {"ANALYSIS_OBS_CHANCES",
       {std::to_string(own.shots), decimal(own.xg), std::to_string(opp.shots),
        decimal(opp.xg)}});
  if (own.shots > 0)
  {
    analysis.observations.push_back(
        {"ANALYSIS_OBS_OWN_ORIGIN",
         {std::to_string(own.box_shots), std::to_string(own.shots),
          std::to_string(own.set_piece_shots),
          std::to_string(own.headed_shots)}});
  }
  if (const auto flank =
          analysis.enough_data ? dominantFlank(opp) : std::nullopt)
  {
    analysis.observations.push_back(
        {"ANALYSIS_OBS_THEIR_FLANK",
         {percent(flank->second), flankKey(mirrored(flank->first)),
          std::to_string(opp.flank_shots[std::to_underlying(flank->first)])}});
  }
  const float allowed = own.passesAllowedPerAction(opp.passes_attempted);
  analysis.observations.push_back(
      {"ANALYSIS_OBS_PRESS",
       {oneDecimal(allowed), std::to_string(opp.passes_attempted),
        std::to_string(own.tackles_attempted + own.interceptions),
        percent(opp.passCompletion())}});
  const int aerials = own.aerials_won + own.aerials_lost;
  if (const int tackles = own.tackles_attempted; aerials > 0 || tackles > 0)
  {
    analysis.observations.push_back(
        {"ANALYSIS_OBS_DUELS",
         {std::to_string(own.aerials_won), std::to_string(aerials),
          std::to_string(own.tackles_won), std::to_string(tackles)}});
  }

  // Suggestions, each with the evidence that triggered it.
  const bool canSubstitute = !input.full_time && input.substitutions_left > 0;
  for (const PlayerNote& note : analysis.notes)
  {
    if (note.kind != PlayerNote::Kind::Tired || !canSubstitute) continue;
    addSuggestion(
        analysis, AnalysisSuggestion::Kind::SubstituteTired, 90,
        {"ANALYSIS_SUGGEST_SUB_TIRED", {name(note.player)}},
        {"ANALYSIS_REASON_SUB_TIRED", {name(note.player), percent(note.value)}},
        note.player);
    break;  // One fatigue change at a time.
  }
  if (analysis.enough_data)
  {
    if (const auto flank = dominantFlank(opp))
    {
      addSuggestion(analysis, AnalysisSuggestion::Kind::ProtectFlank, 80,
                    {"ANALYSIS_SUGGEST_PROTECT_FLANK",
                     {flankKey(mirrored(flank->first))}},
                    {"ANALYSIS_REASON_PROTECT_FLANK",
                     {percent(flank->second), decimal(opp.xg)}});
    }
    if (const auto flank = dominantFlank(own))
    {
      addSuggestion(analysis, AnalysisSuggestion::Kind::AttackFlank, 50,
                    {"ANALYSIS_SUGGEST_ATTACK_FLANK", {flankKey(flank->first)}},
                    {"ANALYSIS_REASON_ATTACK_FLANK",
                     {percent(flank->second), decimal(own.xg)}});
    }
    if (own.possession >= 55.0f && own.xg < opp.xg && own.shots <= opp.shots &&
        own.passes_attempted >= R::CREATE_MIN_PASSES)
    {
      addSuggestion(analysis, AnalysisSuggestion::Kind::CreateMore, 70,
                    {"ANALYSIS_SUGGEST_CREATE", {}},
                    {"ANALYSIS_REASON_CREATE",
                     {percent(own.possession / 100.0f), decimal(own.xg),
                      std::to_string(own.shots)}});
    }
    if (allowed > R::LOOSE_PRESS && opp.passCompletion() >= 0.8f &&
        opp.passes_attempted >= R::PRESS_MIN_PASSES &&
        own.tackles_attempted + own.interceptions >= R::PRESS_MIN_ACTIONS)
    {
      addSuggestion(analysis, AnalysisSuggestion::Kind::PressHigher, 60,
                    {"ANALYSIS_SUGGEST_PRESS", {}},
                    {"ANALYSIS_REASON_PRESS",
                     {oneDecimal(allowed), percent(opp.passCompletion())}});
    }
    if (aerials >= R::MIN_AERIALS &&
        static_cast<float>(own.aerials_lost) / static_cast<float>(aerials) >=
            R::AERIAL_LOSS_SHARE)
    {
      addSuggestion(
          analysis, AnalysisSuggestion::Kind::KeepBallOnGround, 55,
          {"ANALYSIS_SUGGEST_GROUND", {}},
          {"ANALYSIS_REASON_GROUND",
           {std::to_string(own.aerials_lost), std::to_string(aerials)}});
    }
    if (own.goals > opp.goals && opp.xg > own.xg + 0.4f &&
        opp.shots >= R::TIGHTEN_MIN_SHOTS)
    {
      addSuggestion(
          analysis, AnalysisSuggestion::Kind::TightenUp, 75,
          {"ANALYSIS_SUGGEST_TIGHTEN", {}},
          {"ANALYSIS_REASON_TIGHTEN", {decimal(opp.xg), decimal(own.xg)}});
    }
    for (const PlayerNote& note : analysis.notes)
    {
      if (note.kind != PlayerNote::Kind::Struggling || !canSubstitute) continue;
      addSuggestion(analysis, AnalysisSuggestion::Kind::SubstituteStruggling,
                    45, {"ANALYSIS_SUGGEST_SUB_POOR", {name(note.player)}},
                    {"ANALYSIS_REASON_SUB_POOR",
                     {name(note.player), oneDecimal(note.value)}},
                    note.player);
      break;
    }
  }
  std::ranges::stable_sort(analysis.suggestions, std::greater{},
                           &AnalysisSuggestion::priority);
  if (analysis.suggestions.size() >
      static_cast<std::size_t>(R::MAX_SUGGESTIONS))
    analysis.suggestions.resize(R::MAX_SUGGESTIONS);
  if (analysis.suggestions.empty())
  {
    addSuggestion(analysis, AnalysisSuggestion::Kind::KeepGoing, 0,
                  {analysis.enough_data ? "ANALYSIS_SUGGEST_KEEP_GOING"
                                        : "ANALYSIS_SUGGEST_TOO_EARLY",
                   {}},
                  {analysis.enough_data ? "ANALYSIS_REASON_KEEP_GOING"
                                        : "ANALYSIS_REASON_TOO_EARLY",
                   {std::to_string(totalShots)}});
  }
  return analysis;
}

MatchAnalysis analyseLiveMatch(const MatchEngine& engine, bool managed_home,
                               std::function<std::string(PlayerID)> name_of)
{
  std::vector<std::pair<PlayerID, float>> conditions;
  for (const MatchPlayer& player : engine.getPlayers())
  {
    if (player.isHomeTeam != managed_home || !player.onPitch ||
        player.player == nullptr || player.isGoalkeeper)
      continue;
    conditions.emplace_back(player.player->getId(), player.stamina);
  }
  constexpr int MAX_SUBSTITUTIONS = 5;
  AnalysisInput input;
  input.events = engine.getEvents();
  input.stats = &engine.getStats();
  input.players = engine.getPlayerStats();
  input.conditions = conditions;
  input.managed_home = managed_home;
  input.minute = engine.getElapsedMatchMinutes();
  input.full_time = engine.getState() == MatchState::FULL_TIME;
  input.substitutions_left =
      engine.canSubstitute(managed_home)
          ? MAX_SUBSTITUTIONS - engine.getSubstitutionsUsed(managed_home)
          : 0;
  input.name_of = std::move(name_of);
  return analyseMatch(input);
}
