// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "global/types.h"
#include "model/match_analysis.h"
#include "model/match_report.h"
#include "model/strategy.h"

/** Language key of an instruction. */
const char* oppositionInstructionKey(OppositionInstruction instruction);

/** @brief An instruction stored for the next match against a club. */
struct OppositionOrder
{
  TeamID opponent = 0;
  PlayerID player = 0;
  OppositionInstruction instruction = OppositionInstruction::None;
};

/**
 * @brief Instructions against upcoming opponents, per opponent and player
 * (OppositionInstruction is declared in model/tactics.h). The managed club's
 * Strategy carries them into the match engine (see
 * Strategy::setOppositionOrders and GameController).
 */
class OppositionPlan
{
 public:
  /** Sets (None clears) the instruction for a player of @p opponent. */
  void set(TeamID opponent, PlayerID player, OppositionInstruction instruction);
  OppositionInstruction get(TeamID opponent, PlayerID player) const;
  /** Instructions against @p opponent. */
  std::vector<OppositionOrder> forOpponent(TeamID opponent) const;
  /** Drops every instruction against @p opponent (after the match). */
  void clear(TeamID opponent);
  const std::vector<OppositionOrder>& all() const { return orders; }
  void restore(const std::vector<OppositionOrder>& restored);

 private:
  std::vector<OppositionOrder> orders;
};

/** @brief What the report knows about one opposing player. */
struct OppositionPlayer
{
  PlayerID player = 0;
  std::string name;
  PlayerRole role = PlayerRole::UNKNOWN;
  bool likely_starter = false;
  /** Scouted overall estimate; < 0 when the club has not seen him. */
  float estimate = -1.0f;
  float knowledge = 0.0f; /*!< Scouting knowledge 0-100. */
  bool right_footed = true;
  int appearances = 0;
  int goals = 0;
  int assists = 0;
  float average_rating = 0.0f; /*!< 0 without rated matches. */
};

/** @brief A key player with the reason he stands out. */
struct KeyOpponent
{
  OppositionPlayer player;
  AnalysisLine reason;
};

/** @brief A suggested adjustment of the managed tactic. */
struct CounterTactic
{
  AnalysisLine action;
  AnalysisLine reason;
  /** Slider changes (added to the current tactic, then clamped). */
  StrategySliders shift{0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  /** Player to mark tightly when the counter is about him. */
  PlayerID mark = 0;
};

/** @brief Facts gathered for the opposition report. */
struct OppositionInput
{
  TeamID opponent = 0;
  /** Opponent's played reports this season (any order). */
  std::span<const MatchReport> reports;
  /** Reports of the opponent's league this season (league averages). */
  std::span<const MatchReport> league_reports;
  std::vector<OppositionPlayer> squad;
};

/** @brief Per-match averages of a side. */
struct OppositionAverages
{
  float goals_for = 0.0f;
  float goals_against = 0.0f;
  float xg_for = 0.0f;
  float xg_against = 0.0f;
  float shots_for = 0.0f;
  float shots_against = 0.0f;
  float possession = 50.0f;
  float pass_completion = 0.0f; /*!< Percent. */
  int matches = 0;
};

/** @brief The pre-match opposition report. */
struct OppositionReport
{
  TeamID opponent = 0;
  /** Latest results, oldest first: 1 win, 0 draw, -1 defeat. */
  std::vector<int> form;
  OppositionAverages recent; /*!< Last OppositionRules::RECENT_MATCHES. */
  OppositionAverages league; /*!< League average per team and match. */
  bool enough_data = false;
  /** "4-3-3" from the likely XI (empty when unknown). */
  std::string formation;
  std::vector<OppositionPlayer> likely_xi;
  std::vector<KeyOpponent> key_players;
  std::vector<AnalysisLine> strengths;
  std::vector<AnalysisLine> weaknesses;
  std::vector<CounterTactic> counters;
  /** Mean scouting knowledge of the likely XI, 0-100. */
  float confidence = 0.0f;
};

namespace OppositionRules
{
inline constexpr std::size_t RECENT_MATCHES = 5;
inline constexpr int MIN_MATCHES = 3;
inline constexpr std::size_t KEY_PLAYERS = 3;
inline constexpr std::size_t MAX_COUNTERS = 3;
/** Ratio to the league average that counts as a strength / weakness. */
inline constexpr float HIGH_RATIO = 1.15f;
inline constexpr float LOW_RATIO = 0.85f;
inline constexpr float HIGH_POSSESSION = 55.0f;
inline constexpr float LOW_POSSESSION = 45.0f;
/** Pass completion (points) away from the league average. */
inline constexpr float PASSING_MARGIN = 4.0f;
/** Share of the side's goals that makes a scorer the main threat. */
inline constexpr float MAIN_THREAT_SHARE = 0.4f;
/** Team goals needed before one scorer's share means anything. */
inline constexpr int MAIN_THREAT_MIN_GOALS = 5;
}  // namespace OppositionRules

/** Formation label from the roles of a starting XI ("4-4-2"). */
std::string formationLabel(std::span<const PlayerRole> starters);

/** Builds the report (pure and deterministic). */
OppositionReport buildOppositionReport(const OppositionInput& input);

/** Applies a counter's slider shift to a tactic. */
Strategy applyCounter(const Strategy& strategy, const CounterTactic& counter);
