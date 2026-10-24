// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/match_rules.h"

#include <algorithm>
#include <cmath>

std::string_view matchEventTypeName(MatchEventType type)
{
  using enum MatchEventType;
  switch (type)
  {
    case INFO:
      return "info";
    case KICK_OFF:
      return "kick_off";
    case GOAL:
      return "goal";
    case OWN_GOAL:
      return "own_goal";
    case SHOT:
      return "shot";
    case SAVE:
      return "save";
    case SHOT_BLOCKED:
      return "shot_blocked";
    case SHOT_OFF_TARGET:
      return "shot_off_target";
    case WOODWORK:
      return "woodwork";
    case FOUL:
      return "foul";
    case ADVANTAGE:
      return "advantage";
    case YELLOW_CARD:
      return "yellow_card";
    case SECOND_YELLOW:
      return "second_yellow";
    case RED_CARD:
      return "red_card";
    case INJURY:
      return "injury";
    case SUBSTITUTION:
      return "substitution";
    case OFFSIDE:
      return "offside";
    case CORNER:
      return "corner";
    case FREE_KICK:
      return "free_kick";
    case PENALTY:
      return "penalty";
    case PENALTY_MISSED:
      return "penalty_missed";
    case THROW_IN:
      return "throw_in";
    case GOAL_KICK:
      return "goal_kick";
    case ADDED_TIME:
      return "added_time";
    case HALF_TIME:
      return "half_time";
    case SECOND_HALF:
      return "second_half";
    case FULL_TIME:
      return "full_time";
    case PENALTY_SHOOTOUT:
      return "penalty_shootout";
  }
  return "unknown";
}

namespace MatchRules
{
int computeAddedMinutes(const StoppageLog& log, int period)
{
  using T = MatchTuning::Stoppage;
  float base = T::EXTRA_TIME_BASE_MINUTES;
  if (period == 1)
    base = T::FIRST_HALF_BASE_MINUTES;
  else if (period == 2)
    base = T::SECOND_HALF_BASE_MINUTES;
  const float minutes =
      base + static_cast<float>(log.goals) * T::MINUTES_PER_GOAL +
      static_cast<float>(log.substitutions) * T::MINUTES_PER_SUBSTITUTION +
      static_cast<float>(log.cards) * T::MINUTES_PER_CARD +
      static_cast<float>(log.injuries) * T::MINUTES_PER_INJURY +
      static_cast<float>(log.penalties) * T::MINUTES_PER_PENALTY;
  return std::clamp(static_cast<int>(std::lround(minutes)),
                    T::MIN_ADDED_MINUTES, T::MAX_ADDED_MINUTES);
}

float periodStartMinute(int period)
{
  using T = MatchTuning::Timing;
  if (period <= 1) return 0.0f;
  if (period == 2) return T::HALF_TIME_MINUTE;
  return T::FULL_TIME_MINUTE +
         static_cast<float>(period - 3) * T::EXTRA_TIME_HALF_MINUTES;
}

float periodEndMinute(int period)
{
  using T = MatchTuning::Timing;
  if (period <= 1) return T::HALF_TIME_MINUTE;
  if (period == 2) return T::FULL_TIME_MINUTE;
  return T::FULL_TIME_MINUTE +
         static_cast<float>(period - 2) * T::EXTRA_TIME_HALF_MINUTES;
}

bool shootoutDecided(int homeGoals, int homeKicks, int awayGoals, int awayKicks)
{
  if (constexpr int KICKS = MatchTuning::Timing::SHOOTOUT_KICKS;
      homeKicks <= KICKS && awayKicks <= KICKS)
  {
    if (homeGoals > awayGoals + (KICKS - awayKicks) ||
        awayGoals > homeGoals + (KICKS - homeKicks))
      return true;
    if (homeKicks < KICKS || awayKicks < KICKS) return false;
  }
  return homeKicks == awayKicks && homeGoals != awayGoals;
}

float computeMatchRating(const PlayerMatchStats& stats, bool goalkeeper,
                         bool defender, int teamGoalDifference)
{
  using R = MatchTuning::Rating;
  const int failedPasses =
      std::max(0, stats.passesAttempted - stats.passesCompleted);
  const int shotsOffTarget = std::max(0, stats.shots - stats.shotsOnTarget);
  float rating = R::BASELINE + static_cast<float>(stats.goals) * R::GOAL +
                 static_cast<float>(stats.ownGoals) * R::OWN_GOAL +
                 static_cast<float>(stats.assists) * R::ASSIST +
                 static_cast<float>(stats.shotsOnTarget) * R::SHOT_ON_TARGET +
                 static_cast<float>(shotsOffTarget) * R::SHOT_OFF_TARGET +
                 static_cast<float>(stats.keyPasses) * R::KEY_PASS +
                 static_cast<float>(stats.passesCompleted) * R::COMPLETED_PASS +
                 static_cast<float>(failedPasses) * R::FAILED_PASS +
                 static_cast<float>(stats.tacklesWon) * R::TACKLE_WON +
                 static_cast<float>(stats.interceptions) * R::INTERCEPTION +
                 static_cast<float>(stats.clearances) * R::CLEARANCE +
                 static_cast<float>(stats.aerialDuelsWon) * R::AERIAL_WON +
                 static_cast<float>(stats.saves) * R::SAVE +
                 static_cast<float>(stats.foulsCommitted) * R::FOUL +
                 static_cast<float>(stats.yellowCards) * R::YELLOW_CARD +
                 static_cast<float>(stats.redCards) * R::RED_CARD;

  if (goalkeeper || defender)
  {
    rating +=
        static_cast<float>(stats.goalsConceded) *
        (goalkeeper ? R::GOALKEEPER_GOAL_CONCEDED : R::DEFENDER_GOAL_CONCEDED);
    if (stats.goalsConceded == 0 &&
        stats.minutesPlayed >= R::CLEAN_SHEET_MINIMUM_MINUTES)
    {
      rating +=
          (goalkeeper ? R::CLEAN_SHEET_GOALKEEPER : R::CLEAN_SHEET_DEFENDER) *
          std::min(1.0f, stats.minutesPlayed / R::FULL_MATCH_MINUTES);
    }
  }

  if (teamGoalDifference != 0)
  {
    const float participation =
        std::min(1.0f, stats.minutesPlayed / R::FULL_MATCH_MINUTES);
    rating += (teamGoalDifference > 0 ? R::RESULT_BONUS : -R::RESULT_BONUS) *
              participation;
  }
  return std::clamp(rating, R::MINIMUM, R::MAXIMUM);
}

float playerHeightMetres(std::uint8_t heightCentimetres)
{
  // Unknown or implausible heights fall back to the default height.
  if (heightCentimetres < 150)
    return MatchTuning::Units::DEFAULT_PLAYER_HEIGHT_METRES;
  return static_cast<float>(heightCentimetres) / 100.0f;
}

float headerReachMetres(float heightMetres, float physicality)
{
  using A = MatchTuning::Aerial;
  return heightMetres - A::HEAD_OFFSET_METRES + A::BASE_JUMP_METRES +
         std::clamp(physicality, 0.0f, 1.0f) * A::PHYSICALITY_JUMP_METRES;
}

float goalkeeperReachMetres(float heightMetres, float goalkeeping)
{
  using A = MatchTuning::Aerial;
  return heightMetres * A::GOALKEEPER_ARM_REACH_RATIO +
         A::GOALKEEPER_BASE_JUMP_METRES +
         std::clamp(goalkeeping, 0.0f, 1.0f) * A::GOALKEEPER_SKILL_JUMP_METRES;
}

float goalkeeperHandlingRadiusMetres(float heightMetres, float goalkeeping,
                                     float ballHeightMetres, bool diving)
{
  const float verticalReach = goalkeeperReachMetres(heightMetres, goalkeeping);
  if (ballHeightMetres < 0.0F || ballHeightMetres > verticalReach)
  {
    return 0.0F;
  }
  const float shoulderHeight =
      heightMetres * MatchTuning::Goalkeeper::SHOULDER_HEIGHT_SHARE;
  const float stretch =
      std::max(0.0F, ballHeightMetres - shoulderHeight) /
      std::max(MatchTuning::Goalkeeper::MIN_VERTICAL_REACH_MARGIN,
               verticalReach - shoulderHeight);
  // A diving keeper's tilted torso contributes to the hand's reach;
  // lateral travel itself is already reflected in the body position.
  const float armReach =
      heightMetres * (diving
                          ? MatchTuning::Goalkeeper::DIVE_REACH_HEIGHT_SHARE
                          : MatchTuning::Goalkeeper::HAND_REACH_HEIGHT_SHARE);
  return armReach * std::sqrt(std::max(0.0F, 1.0F - (stretch * stretch)));
}

float aerialDuelStrength(float heightMetres, float physicality,
                         float ballHeightMetres)
{
  const float reach = headerReachMetres(heightMetres, physicality);
  const float margin = reach - ballHeightMetres;
  if (margin < 0.0f) return 0.0f;
  // Being able to meet the ball comfortably matters, but beyond a few
  // centimetres of spare reach it is timing and strength that decide.
  return (0.35f + std::clamp(physicality, 0.0f, 1.0f)) *
         (0.6f + std::min(margin, 0.4f));
}

FoulSanction decideFoulSanction(const FoulContext& context)
{
  using enum FoulSanction;
  using D = MatchTuning::Discipline;
  const float bias =
      context.offenderIsAway ? D::AWAY_CARD_BIAS : D::HOME_CARD_BIAS;
  const float strictness =
      std::clamp(context.strictness, D::MIN_CONTEXT_STRICTNESS,
                 D::MAX_CONTEXT_STRICTNESS) *
      (1.0f + (bias - 1.0f) * context.venueBiasScale);

  if (context.severityRoll >= 1.0f - D::SERIOUS_FOUL_PLAY_SHARE * strictness)
    return RED;
  if (context.denyingGoalChance)
  {
    const float redChance = context.inPenaltyArea
                                ? D::PENALTY_AREA_DOGSO_RED_CHANCE
                                : D::DOGSO_RED_CHANCE;
    if (context.cardRoll < redChance) return RED;
  }

  const bool reckless = context.severityRoll >= 1.0f - D::RECKLESS_SHARE;
  float yellowChance =
      reckless ? D::RECKLESS_YELLOW_CHANCE : D::CARELESS_YELLOW_CHANCE;
  if (context.tactical || context.denyingGoalChance)
    yellowChance = std::max(yellowChance, D::TACTICAL_YELLOW_CHANCE);
  yellowChance = std::clamp(yellowChance * strictness, 0.0f, 0.95f);
  if (context.cardRoll >= yellowChance) return NONE;
  return context.offenderAlreadyBooked ? SECOND_YELLOW : YELLOW;
}

float tackleWinChance(const TackleContext& context)
{
  using D = MatchTuning::Defending;
  return std::clamp(
      D::BASE_WIN_CHANCE + context.defending * D::DEFENDING_WIN_BONUS -
          context.dribbling * D::DRIBBLING_WIN_PENALTY +
          (context.defenderPhysicality - context.carrierPhysicality) *
              D::PHYSICALITY_DUEL_WEIGHT +
          context.pressing * D::PRESSING_WIN_EFFECT +
          std::clamp(context.exposure, 0.0f, 1.0f) * D::EXPOSURE_WIN_BONUS -
          (context.shielding
               ? context.carrierPhysicality * D::SHIELD_PHYSICALITY_PENALTY
               : 0.0f) -
          (context.sliding ? D::SLIDE_WIN_PENALTY : 0.0f) -
          (context.fromBehind ? D::FROM_BEHIND_WIN_PENALTY : 0.0f),
      D::MIN_WIN_CHANCE, D::MAX_WIN_CHANCE);
}

float tackleFoulPropensity(const TackleContext& context)
{
  using D = MatchTuning::Defending;
  // Defenders are far more careful inside their own penalty area, a booked
  // player picks his challenges, and a challenge while the ball is away from
  // the attacker's foot rarely catches the man.
  return (D::BASE_FOUL_CHANCE + context.riskTaking * D::RISK_FOUL_BONUS +
          context.pressing * D::PRESSING_FOUL_BONUS +
          (1.0f - context.defending) * D::TECHNIQUE_FOUL_BONUS) *
         (context.defenderBooked
              ? MatchTuning::Discipline::BOOKED_PLAYER_CAUTION
              : 1.0f) *
         (context.inPenaltyArea ? D::PENALTY_AREA_FOUL_SCALE : 1.0f) *
         (context.fromBehind ? D::FROM_BEHIND_FOUL_FACTOR : 1.0f) *
         (context.sliding ? D::SLIDE_FOUL_FACTOR : 1.0f) *
         (1.0f -
          std::clamp(context.exposure, 0.0f, 1.0f) * D::EXPOSED_FOUL_RELIEF);
}
}  // namespace MatchRules
