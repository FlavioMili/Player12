// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "model/match_events.h"

/**
 * Pure, deterministic rules used by the match engine. Everything here is a
 * function of its arguments (random rolls are passed in), so each rule can be
 * unit tested without running a match.
 */
namespace MatchRules
{
/** Stoppages recorded during one half; drives the added time. */
struct StoppageLog
{
  int goals = 0;
  int substitutions = 0;
  int cards = 0;
  int injuries = 0;
  int penalties = 0;
};

/** Whole added minutes for a period (1-2, or 3-4 in extra time) from its
 * stoppages. */
int computeAddedMinutes(const StoppageLog& log, int period);

/** Clock minute at which a period starts (0, 45, 90, 105). */
float periodStartMinute(int period);
/** Clock minute at which a period's regulation time ends (45, 90, 105, 120). */
float periodEndMinute(int period);

/**
 * How a level match is settled. League matches (the default) may end drawn;
 * a knockout match (or the second leg of a tie) that is level, counting the
 * goals of earlier legs, goes to extra time and then to penalties.
 */
struct Knockout
{
  bool required = false;
  /** Goals scored in earlier legs by this match's home and away sides. */
  int homeAggregate = 0;
  int awayAggregate = 0;
  /** Two halves of extra time before the shootout. */
  bool extraTime = true;
};

/**
 * Whether a penalty shootout is decided: one side can no longer be caught
 * within the first MatchTuning::Timing::SHOOTOUT_KICKS kicks each, or, in
 * sudden death, the sides have taken as many kicks and one has scored more.
 */
bool shootoutDecided(int homeGoals, int homeKicks, int awayGoals,
                     int awayKicks);

/**
 * Event-driven match rating: 6.0 baseline, bounded to [3, 10].
 * `teamGoalDifference` is the player's team score minus the opponent's.
 */
float computeMatchRating(const PlayerMatchStats& stats, bool goalkeeper,
                         bool defender, int teamGoalDifference);

/** Player height in metres; unknown (0) heights fall back to the default. */
float playerHeightMetres(std::uint8_t heightCentimetres);

/** Highest ball height (metres) an outfield player can head. */
float headerReachMetres(float heightMetres, float physicality);

/** Highest ball height (metres) a goalkeeper can claim with the hands. */
float goalkeeperReachMetres(float heightMetres, float goalkeeping);
/** Horizontal hand reach from the current body position at a ball height. */
float goalkeeperHandlingRadiusMetres(float heightMetres, float goalkeeping,
                                     float ballHeightMetres,
                                     bool diving = false);

/** Relative strength of a player in an aerial duel for a ball at a height. */
float aerialDuelStrength(float heightMetres, float physicality,
                         float ballHeightMetres);

enum class FoulSanction : std::uint8_t
{
  NONE,
  YELLOW,
  SECOND_YELLOW,
  RED
};

struct FoulContext
{
  /** Uniform rolls in [0, 1). */
  float severityRoll = 0.0F;
  float cardRoll = 0.0F;
  /** Per-match referee strictness, ~N(1, 0.13). */
  float strictness = 1.0F;
  /** The foul stopped a promising attack (tactical foul). */
  bool tactical = false;
  /** Denied an obvious goal-scoring opportunity. */
  bool denyingGoalChance = false;
  /** The foul happened inside the offender's penalty area. */
  bool inPenaltyArea = false;
  bool offenderAlreadyBooked = false;
  bool offenderIsAway = false;
  /** Scales the referee's lean toward the home side (0 = neutral venue). */
  float venueBiasScale = 1.0F;
};

FoulSanction decideFoulSanction(const FoulContext& context);

// A ground challenge on a dribbler is two halves, one per player's body,
// settled by the rules. Each half is in probability points on one scale:
// what the player adds to (tackler) or takes from (carrier) the chance that
// the challenge reaches the ball. For two average players (0.5) the make is
// about 0.27 and the resist about 0.20.

/** What the tackler's Defending and Physicality are worth. */
float tackleMake(float defending, float physicality);

/** What the carrier's Dribbling and Physicality are worth; shielding (at
 * walking pace) lets his Physicality count again. */
float tackleResist(float dribbling, float physicality, bool shielding);

/** How clumsy the tackler is: foul propensity points (0 for a perfect
 * Defending of 1). */
float tackleTechnique(float defending);

/** Everything that decides a ground challenge on a dribbler. */
struct TackleContext
{
  /** The two bodies' halves (see tackleMake, tackleResist). */
  float make = tackleMake(0.5F, 0.5F);
  float resist = tackleResist(0.5F, 0.5F, false);
  /** The tackler's foul points (see tackleTechnique). */
  float technique = tackleTechnique(0.5F);
  /** Team edge of each side (home, numbers, talk) in attribute units:
   * MatchEngine::teamEdge x DUEL_EDGE_WEIGHT. */
  float defenderEdge = 0.0F;
  float carrierEdge = 0.0F;
  /** Team pressing instruction in [0, 1]. */
  float pressing = 0.5F;
  /** Team risk-taking instruction in [0, 1]. */
  float riskTaking = 0.5F;
  /** Share of the touch the ball is still away from the carrier's foot. */
  float exposure = 0.0F;
  bool sliding = false;
  bool fromBehind = false;
  bool inPenaltyArea = false;
  bool defenderBooked = false;
};

/** Chance that a challenge reaches the ball first. */
float tackleWinChance(const TackleContext& context);

/**
 * Propensity for the challenge to be a foul: applied in full to a missed
 * challenge and scaled down for one that also reached the ball.
 */
float tackleFoulPropensity(const TackleContext& context);

/**
 * Match condition drained per second for a player moving at `speedRatio`
 * of their fresh top speed. `endurance` is the Stamina attribute in [0, 1].
 */
inline float staminaDrainPerSecond(float speedRatio, float endurance,
                                   float pressingIntensity)
{
  using P = MatchTuning::Fatigue;
  const float ratio = std::clamp(speedRatio, 0.0F, 1.5F);
  const float sprint = std::max(0.0F, ratio - P::SPRINT_THRESHOLD) /
                       (1.0F - P::SPRINT_THRESHOLD);
  const float work =
      P::IDLE_DRAIN + (ratio * ratio * P::RUNNING_DRAIN) +
      (sprint * P::SPRINT_DRAIN) +
      (std::clamp(pressingIntensity, 0.0F, 1.0F) * P::PRESSING_DRAIN);
  return work * (P::ENDURANCE_BASE -
                 std::clamp(endurance, 0.0F, 1.0F) * P::ENDURANCE_RELIEF);
}

/**
 * The speed (m/s) at which he breaks from a walk into a run: Froude number
 * v^2 / (g L) of about 0.5 across people of different leg length (Kram et
 * al.), with leg length L about 0.53 of his height. 2.07 m/s at 1.65 m, 2.16
 * at 1.80, 2.25 at 1.95.
 */
inline float walkRunTransitionSpeed(float heightMetres)
{
  constexpr float GRAVITY = 9.81F;
  constexpr float TRANSITION_FROUDE = 0.5F;
  constexpr float LEG_LENGTH_SHARE = 0.53F;
  return std::sqrt(TRANSITION_FROUDE * GRAVITY * LEG_LENGTH_SHARE *
                   std::max(heightMetres, 1.0F));
}
}  // namespace MatchRules
