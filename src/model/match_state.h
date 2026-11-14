// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

/**
 * The match's state words that views read as well as the engine: what the
 * match is doing, each side's phase, each player's intent and each keeper's
 * state. Kept apart from match_engine.h so the render snapshot (and the
 * renderers behind it) need not include the engine.
 */
enum class MatchState
{
  KICK_OFF,
  PLAYING,
  THROW_IN,
  GOAL_KICK,
  CORNER_KICK,
  FREE_KICK,
  PENALTY,
  GOAL,
  /** A level knockout match decided from the spot (clock stopped). */
  PENALTY_SHOOTOUT,
  /** Half-time, and the breaks before and during extra time. */
  HALF_TIME,
  FULL_TIME,
  /** A training pitch: play stopped, waiting for the coach's serve
   * (training ground plan section 20). A match never enters it. */
  SERVE
};

/** Team-level context shared by individual player decisions. */
enum class TeamPhase
{
  STOPPAGE,
  SET_PIECE,
  DEFENSIVE_BLOCK,
  DEFENSIVE_TRANSITION,
  ATTACKING_TRANSITION,
  POSSESSION,
  FINAL_THIRD
};

/** High-level decision currently driving a player's renderer-independent AI. */
enum class PlayerIntent
{
  HOLD_SHAPE,
  CARRY_BALL,
  OFFER_SUPPORT,
  RECEIVE_PASS,
  RUN_IN_BEHIND,
  ATTACK_BOX,
  OVERLAP,
  PRESS_BALL,
  COVER_PRESS,
  BLOCK_PASSING_LANE,
  MARK_OPPONENT,
  CLAIM_LOOSE_BALL,
  RECOVER_SHAPE,
  GOALKEEP
};
inline constexpr std::size_t PLAYER_INTENT_COUNT =
    static_cast<std::size_t>(PlayerIntent::GOALKEEP) + 1;

/** Semantic goalkeeper state. Animation stays out of the engine. */
enum class GoalkeeperState
{
  SET_POSITION,
  SWEEP,
  RUSH,
  CLAIM,
  DIVE,
  HOLD,
  DISTRIBUTE,
  RECOVER
};

std::string_view goalkeeperStateName(GoalkeeperState state);

/** Who a person round the touchline is (touchline plan, stage 2). */
enum class TouchlineRole : std::uint8_t
{
  SUBSTITUTE,
  ASSISTANT,
  GOALKEEPER_COACH,
  PHYSIO,
  MANAGER,
  /** The match officials (stage 3). */
  REFEREE,
  ASSISTANT_REFEREE,
  FOURTH_OFFICIAL,
  /** Sent off (stage 6): on his way to the tunnel, and staying there. */
  SENT_OFF
};

/** Whether a touchline role is one of the match officials. */
constexpr bool isOfficial(TouchlineRole role)
{
  return role == TouchlineRole::REFEREE || role == TouchlineRole::ASSISTANT_REFEREE ||
         role == TouchlineRole::FOURTH_OFFICIAL;
}
