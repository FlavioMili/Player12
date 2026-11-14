// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Aardvark brain: the player decision-making inherited with the engine,
// kept frozen so the brains being developed can be compared with it (see
// match_brain.h). Every function here is a MatchEngine member with the
// "Aardvark" suffix, declared in match_brain_aardvark_members.h; the engine
// reaches them through the dispatchers in match_brain_dispatch.cpp.
//
// Do not change behaviour here. A brain-only helper this file calls has its
// own Aardvark copy; a helper shared with the body (match_engine.cpp) is
// shared by every brain, so a new brain copies it before changing it.

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/match_recorder.h"
#include "model/player.h"
#include "model/role_utils.h"

using namespace MatchEngineInternal;

namespace
{
/** How hard each intent runs, as a share of the fresh top speed. */
float movementSpeedScale(PlayerIntent intent)
{
  switch (intent)
  {
    case PlayerIntent::CARRY_BALL:
      return MatchTuning::Player::CARRY_BALL_SPEED_SCALE;
    case PlayerIntent::OFFER_SUPPORT:
      return MatchTuning::Player::SUPPORT_SPEED_SCALE;
    case PlayerIntent::RECEIVE_PASS:
      return MatchTuning::Player::ATTACKING_RUN_SPEED_SCALE;
    case PlayerIntent::RUN_IN_BEHIND:
    case PlayerIntent::ATTACK_BOX:
    case PlayerIntent::OVERLAP:
      return MatchTuning::Player::ATTACKING_RUN_SPEED_SCALE;
    case PlayerIntent::PRESS_BALL:
    case PlayerIntent::CLAIM_LOOSE_BALL:
      return MatchTuning::Player::PRESS_SPEED_SCALE;
    case PlayerIntent::COVER_PRESS:
    case PlayerIntent::BLOCK_PASSING_LANE:
      return MatchTuning::Player::COVER_SPEED_SCALE;
    case PlayerIntent::MARK_OPPONENT:
      return MatchTuning::Player::MARKING_SPEED_SCALE;
    case PlayerIntent::RECOVER_SHAPE:
      return MatchTuning::Player::RECOVERY_SPEED_SCALE;
    case PlayerIntent::GOALKEEP:
      return MatchTuning::Player::GOALKEEPER_MOVEMENT_SPEED_SCALE;
    case PlayerIntent::HOLD_SHAPE:
      return MatchTuning::Player::HOLD_SHAPE_SPEED_SCALE;
  }
  return MatchTuning::Player::HOLD_SHAPE_SPEED_SCALE;
}
}  // namespace

const MatchEngine::MovementStyle& MatchEngine::movementStyleAardvark()
{
  using P = MatchTuning::Player;
  static const MovementStyle style = []
  {
    MovementStyle built;
    built.tacticalResponsePerSecond = P::TACTICAL_TARGET_RESPONSE_PER_SECOND;
    built.urgentResponsePerSecond = P::URGENT_TARGET_RESPONSE_PER_SECOND;
    built.familiarityResponseLoss = P::FAMILIARITY_RESPONSE_LOSS;
    for (std::size_t intent = 0; intent < PLAYER_INTENT_COUNT; ++intent)
      built.intentSpeedShares[intent] =
          movementSpeedScale(static_cast<PlayerIntent>(intent));
    built.restartWalkShare = P::RESTART_WALK_SPEED_SCALE;
    built.urgencyDistanceMetres = P::URGENCY_DISTANCE_METRES;
    built.maxUrgencyShare = P::MAX_URGENCY_SPEED_SCALE;
    return built;
  }();
  return style;
}

void MatchEngine::applyTeamMentalityAardvark(StrategySliders& sliders,
                                             bool homeTeam) const
{
  // Score effects: a side in front sits deeper and takes fewer risks, a side
  // behind pushes on, more so as the match runs out.
  using T = MatchTuning::Touchline;
  const int lead =
      std::clamp(homeTeam ? homeScore - awayScore : awayScore - homeScore,
                 -T::SCORE_EFFECT_MAX_GOALS, T::SCORE_EFFECT_MAX_GOALS);
  const auto clampSlider = [](float& value)
  { value = std::clamp(value, 0.0f, 1.0f); };
  if (lead != 0)
  {
    const float urgency =
        static_cast<float>(lead) *
        (T::SCORE_EFFECT_BASE +
         (1.0f - T::SCORE_EFFECT_BASE) *
             std::min(1.0f,
                      matchTimeMinutes /
                          (2.0f * MatchTuning::Timing::HALF_TIME_MINUTE)));
    // Only a side chasing the game commits more men forward; one in front
    // keeps its passing but stops pressing and taking risks.
    if (lead < 0) sliders.offensiveBias -= urgency * T::SCORE_EFFECT_OFFENSIVE;
    if (lead >= MatchTuning::Decision::COMFORTABLE_LEAD)
    {
      sliders.offensiveBias -=
          urgency / static_cast<float>(lead) *
          static_cast<float>(lead - MatchTuning::Decision::COMFORTABLE_LEAD +
                             1) *
          T::GAME_MANAGEMENT_OFFENSIVE;
    }
    sliders.riskTaking -= urgency * T::SCORE_EFFECT_RISK;
    sliders.pressing -= urgency * T::SCORE_EFFECT_PRESSING;
    sliders.compactness += urgency * T::SCORE_EFFECT_COMPACTNESS;
    clampSlider(sliders.pressing);
    clampSlider(sliders.riskTaking);
    clampSlider(sliders.offensiveBias);
    clampSlider(sliders.compactness);
  }
  if (const float underdog = underdogShares[homeTeam ? 0 : 1]; underdog > 0.0f)
  {
    sliders.pressing -= underdog * T::UNDERDOG_PRESSING;
    sliders.compactness += underdog * T::UNDERDOG_COMPACTNESS;
    sliders.offensiveBias -= underdog * T::UNDERDOG_OFFENSIVE;
    clampSlider(sliders.pressing);
    clampSlider(sliders.offensiveBias);
    clampSlider(sliders.compactness);
  }
}

float MatchEngine::shoutShotBiasAardvark(bool homeTeam) const
{
  const MatchShout shout = shouts[homeTeam ? 0 : 1].shout;
  const float bias = shout == MatchShout::SHOOT_ON_SIGHT       ? 1.0f
                     : shout == MatchShout::WORK_BALL_INTO_BOX ? -1.0f
                                                               : 0.0f;
  return bias * MatchTuning::Touchline::SHOUT_SHOT_BIAS *
         getShoutStrength(homeTeam);
}

void MatchEngine::refreshTacticalTargetsAardvark(float dt, bool planHome)
{
  // Team planning happens in two passes: choose shared assignments (pressers,
  // runners, support and marks), then give each player a target and intent.
  // Bodies stay still until updateMovement integrates all targets, so player
  // iteration order cannot make later players see an already-moved opponent.
  // Add an off-ball behavior here; physical acceleration belongs downstream.
  const MatchPlayer* carrier = findMatchPlayer(getPossessionAttachment());
  const MatchPlayer* transitionSource = findMatchPlayer(getLastPossessionAttachment());
  // The players on the pitch of each side, in slot order (index 0 home).
  std::array<std::array<MatchPlayer*, 16>, 2> sides{};
  std::array<std::size_t, 2> sideSizes{};
  for (auto& player : players)
  {
    const std::size_t side = player.isHomeTeam ? 0 : 1;
    if (active(player) && sideSizes[side] < sides[side].size())
      sides[side][sideSizes[side]++] = &player;
  }
  const auto onPitch = [&sides, &sideSizes](bool homeTeam)
  {
    const std::size_t side = homeTeam ? 0 : 1;
    return std::span<MatchPlayer* const>(sides[side].data(), sideSizes[side]);
  };
  // Nobody moves while the targets are chosen, so each side's offside line
  // is read once.
  const std::array<float, 2> offsideLines{offsideLine(true),
                                          offsideLine(false)};
  const auto lineFor = [&offsideLines](bool attackingHome)
  { return offsideLines[attackingHome ? 0 : 1]; };
  const PitchPoint pressurePosition = carrier ? carrier->position : ball.position;
  const StrategySliders homeSliders = getEffectiveSliders(true);
  const StrategySliders awaySliders = getEffectiveSliders(false);

  const auto estimatedArrivalTime = [&](const MatchPlayer& candidate)
  {
    const Vector2F toTarget =
        pressurePosition - candidate.position;
    const float targetDistance = length(toTarget);
    if (targetDistance <= EPSILON) return 0.0f;
    const Vector2F pursuitDirection{toTarget.x / targetDistance,
                                    toTarget.y / targetDistance};
    const float velocityTowardTarget =
        std::max(0.0f, candidate.velocity.x * pursuitDirection.x +
                           candidate.velocity.y * pursuitDirection.y);
    const float pursuitSpeed =
        std::max(currentTopSpeed(candidate) +
                     velocityTowardTarget *
                         MatchTuning::Player::CURRENT_VELOCITY_PURSUIT_WEIGHT,
                 MatchTuning::Player::MINIMUM_PURSUIT_SPEED);
    float arrivalTime =
        std::max(0.0f, targetDistance / pursuitSpeed -
                           pressEagernessAardvark(candidate, carrier));
    if (candidate.intent == PlayerIntent::PRESS_BALL ||
        candidate.intent == PlayerIntent::CLAIM_LOOSE_BALL)
    {
      arrivalTime = std::max(
          0.0f, arrivalTime - MatchTuning::Shape::PRESSER_CONTINUITY_SECONDS);
    }
    return arrivalTime;
  };

  const auto closestOutfieldPair = [&](bool homeTeam)
  {
    std::array<MatchPlayer*, 2> closest{nullptr, nullptr};
    std::array<float, 2> arrivalTimes{std::numeric_limits<float>::max(),
                                      std::numeric_limits<float>::max()};
    for (MatchPlayer* const pointer : onPitch(homeTeam))
    {
      MatchPlayer& candidate = *pointer;
      if (candidate.isGoalkeeper) continue;
      const float arrivalTime = estimatedArrivalTime(candidate);
      if (arrivalTime < arrivalTimes[0])
      {
        closest[1] = closest[0];
        arrivalTimes[1] = arrivalTimes[0];
        closest[0] = &candidate;
        arrivalTimes[0] = arrivalTime;
      }
      else if (arrivalTime < arrivalTimes[1])
      {
        closest[1] = &candidate;
        arrivalTimes[1] = arrivalTime;
      }
    }

    // A purposeful pass creates a receiving run. Prioritizing that receiver
    // prevents a merely nearby teammate from making the play look arbitrary.
    if (!carrier && getPossessionState().isPass && getPossessionState().passByHome == homeTeam)
    {
      MatchPlayer* intended = findMatchPlayer(getExpectedReceiver());
      if (intended && active(*intended) && !intended->isGoalkeeper &&
          closest[0] != intended)
      {
        closest[1] = closest[0];
        closest[0] = intended;
      }
    }
    return closest;
  };

  const auto homePressers = closestOutfieldPair(true);
  const auto awayPressers = closestOutfieldPair(false);

  const auto runPriority = [&](const MatchPlayer& candidate)
  {
    float rolePriority = MatchTuning::Shape::MIDFIELDER_RUN_PRIORITY;
    switch (candidate.player->getRole())
    {
      case PlayerRole::ST:
        rolePriority = MatchTuning::Shape::STRIKER_RUN_PRIORITY;
        break;
      case PlayerRole::LW:
      case PlayerRole::RW:
        rolePriority = MatchTuning::Shape::WINGER_RUN_PRIORITY;
        break;
      case PlayerRole::CAM:
        rolePriority = MatchTuning::Shape::ATTACKING_MIDFIELDER_RUN_PRIORITY;
        break;
      case PlayerRole::CDM:
        rolePriority = MatchTuning::Shape::HOLDING_MIDFIELDER_RUN_PRIORITY;
        break;
      case PlayerRole::CM:
      case PlayerRole::LM:
      case PlayerRole::RM:
        break;
      default:
        return -std::numeric_limits<float>::infinity();
    }
    const float depth = candidate.isHomeTeam
                                             ? pitch.fractionX(candidate.position)
                                             : 1.0f - pitch.fractionX(candidate.position);
    const float separation =
        carrier ? pitch.fractionY(std::abs(candidate.position.y - carrier->position.y)) : 0.0f;
    // Who makes the run also varies from attack to attack, so strike
    // partners share the runs in behind.
    const float variety = epochNoise(candidate.player->getId(), 0x7a11edU,
                                     RUN_TIMING_EPOCH_STEPS) *
                          MatchTuning::Shape::RUN_VARIETY_PRIORITY;
    return rolePriority + variety +
           candidate.pace * MatchTuning::Shape::RUN_PACE_PRIORITY +
           depth * MatchTuning::Shape::RUN_DEPTH_PRIORITY +
           separation * MatchTuning::Shape::RUN_SEPARATION_PRIORITY +
           (candidate.isMakingRun ? MatchTuning::Shape::RUN_CONTINUITY_PRIORITY
                                  : 0.0f) +
           roleProfileOf(candidate).runBias * TacticsTuning::RUN_BIAS_WEIGHT;
  };

  const auto selectAttackingRunners = [&](bool homeTeam)
  {
    std::array<MatchPlayer*, MatchTuning::Shape::MAX_COMMITTED_RUNNERS>
        runners{};
    std::array<float, MatchTuning::Shape::MAX_COMMITTED_RUNNERS> scores;
    scores.fill(-std::numeric_limits<float>::infinity());
    const bool possessionContext = carrier && carrier->isHomeTeam == homeTeam;
    const bool looseTransitionContext =
        !carrier && !getPossessionState().isPass && transitionSource &&
        transitionSource->isHomeTeam == homeTeam;
    if (!possessionContext && !looseTransitionContext) return runners;

    const StrategySliders& sliders = homeTeam ? homeSliders : awaySliders;
    const float attackingPosition =
        carrier ? carrier->position.x : ball.position.x;
    const float attackingProgress =
        homeTeam ? pitch.fractionX(attackingPosition)
                 : 1.0f - pitch.fractionX(attackingPosition);
    const float attackingCommitment =
        sliders.offensiveBias + sliders.riskTaking;
    const TeamPhase phase = homeTeam ? homePhase : awayPhase;
    const int lead = homeTeam ? homeScore - awayScore : awayScore - homeScore;
    const bool managingGame = lead >= MatchTuning::Decision::COMFORTABLE_LEAD;
    const std::size_t runnerCount =
        !managingGame &&
                (phase == TeamPhase::ATTACKING_TRANSITION ||
                 attackingProgress >=
                     MatchTuning::Shape::SECOND_RUNNER_PROGRESS_THRESHOLD ||
                 attackingCommitment >=
                     MatchTuning::Shape::SECOND_RUNNER_ATTACK_THRESHOLD)
            ? MatchTuning::Shape::MAX_COMMITTED_RUNNERS
            : MatchTuning::Shape::MIN_COMMITTED_RUNNERS;

    // The best runs by priority, with at most one striker among them: the
    // other striker comes short and the late runs come from midfield.
    std::size_t strikers = 0;
    for (std::size_t slot = 0; slot < runnerCount; ++slot)
    {
      for (MatchPlayer* const pointer : onPitch(homeTeam))
      {
        MatchPlayer& candidate = *pointer;
        if (&candidate == carrier || candidate.isInjured || candidate.fallen ||
            std::ranges::find(runners, &candidate) != runners.end())
          continue;
        const bool striker = candidate.player->getRole() == PlayerRole::ST;
        if (striker && strikers >= MatchTuning::Shape::MAX_STRIKER_RUNNERS)
          continue;
        const float score = runPriority(candidate);
        if (score > scores[slot])
        {
          scores[slot] = score;
          runners[slot] = &candidate;
        }
      }
      if (runners[slot] && runners[slot]->player->getRole() == PlayerRole::ST)
        ++strikers;
    }
    return runners;
  };

  const auto homeRunners = selectAttackingRunners(true);
  const auto awayRunners = selectAttackingRunners(false);

  const auto runChannel = [this](const MatchPlayer& runner)
  {
    const PlayerRole role = runner.player->getRole();
    if (role == PlayerRole::LW)
      return MatchTuning::Shape::LEFT_INSIDE_FORWARD_CHANNEL;
    if (role == PlayerRole::RW)
      return MatchTuning::Shape::RIGHT_INSIDE_FORWARD_CHANNEL;
    if (role == PlayerRole::LM)
      return MatchTuning::Shape::LEFT_WIDE_ATTACK_CHANNEL;
    if (role == PlayerRole::RM)
      return MatchTuning::Shape::RIGHT_WIDE_ATTACK_CHANNEL;
    if (runner.basePosition.y < pitch.atY(MatchTuning::Pitch::CENTRE))
      return MatchTuning::Shape::LEFT_STRIKER_ATTACK_CHANNEL;
    if (runner.basePosition.y > pitch.atY(MatchTuning::Pitch::CENTRE))
      return MatchTuning::Shape::RIGHT_STRIKER_ATTACK_CHANNEL;
    return MatchTuning::Shape::CENTRAL_ATTACK_CHANNEL;
  };

  // In the final third a central midfielder arrives late at the edge of
  // the box, the wide player on the far side attacks the far post and a
  // full-back overlaps.
  enum class Arrival
  {
    MIDFIELDER,
    FAR_POST,
    FULLBACK
  };
  const auto selectFinalThirdPlayer = [&](bool homeTeam,
                                          Arrival arrival) -> MatchPlayer*
  {
    if (!carrier || carrier->isHomeTeam != homeTeam) return nullptr;
    const TeamPhase phase = homeTeam ? homePhase : awayPhase;
    if (phase != TeamPhase::FINAL_THIRD) return nullptr;
    const float ballSide =
        carrier->position.y - pitch.atY(MatchTuning::Pitch::CENTRE);
    if (arrival == Arrival::FAR_POST &&
        std::abs(ballSide) < MatchTuning::Shape::FAR_POST_MIN_BALL_WIDTH_METRES)
      return nullptr;

    MatchPlayer* selected = nullptr;
    float bestScore = std::numeric_limits<float>::max();
    for (MatchPlayer* const pointer : onPitch(homeTeam))
    {
      MatchPlayer& candidate = *pointer;
      if (&candidate == carrier || candidate.isInjured || candidate.fallen) continue;
      const auto& runners = homeTeam ? homeRunners : awayRunners;
      if (std::ranges::find(runners, &candidate) != runners.end()) continue;
      const PlayerRole role = candidate.player->getRole();
      const bool wide = role == PlayerRole::LM || role == PlayerRole::RM ||
                        role == PlayerRole::LW || role == PlayerRole::RW;
      const bool farSide =
          (candidate.basePosition.y - pitch.atY(MatchTuning::Pitch::CENTRE)) * ballSide <
          0.0f;
      const bool eligible =
          arrival == Arrival::FULLBACK
              ? role == PlayerRole::LB || role == PlayerRole::RB
          : arrival == Arrival::FAR_POST
              ? wide && farSide
              : role == PlayerRole::CM || role == PlayerRole::CAM;
      if (!eligible) continue;
      // A defensive full-back never overlaps; an attacking one is keener.
      const float overlapBias = arrival == Arrival::FULLBACK
                                    ? roleProfileOf(candidate).overlapBias
                                    : 0.0f;
      if (overlapBias <= -1.0f) continue;

      float score =
          pitch.fractionY(std::abs(candidate.basePosition.y - carrier->position.y)) -
                    overlapBias * TacticsTuning::OVERLAP_BIAS_WEIGHT;
      if (arrival == Arrival::FAR_POST) score = -score;
      const PlayerIntent continuityIntent = arrival == Arrival::FULLBACK
                                                ? PlayerIntent::OVERLAP
                                                : PlayerIntent::ATTACK_BOX;
      if (candidate.intent == continuityIntent)
        score -= MatchTuning::Shape::FINAL_THIRD_SELECTION_CONTINUITY;
      if (score < bestScore)
      {
        bestScore = score;
        selected = &candidate;
      }
    }
    return selected;
  };

  MatchPlayer* homeMidfieldArrival =
      selectFinalThirdPlayer(true, Arrival::MIDFIELDER);
  MatchPlayer* awayMidfieldArrival =
      selectFinalThirdPlayer(false, Arrival::MIDFIELDER);
  MatchPlayer* homeFarPostRunner =
      selectFinalThirdPlayer(true, Arrival::FAR_POST);
  MatchPlayer* awayFarPostRunner =
      selectFinalThirdPlayer(false, Arrival::FAR_POST);
  MatchPlayer* homeOverlappingFullback =
      selectFinalThirdPlayer(true, Arrival::FULLBACK);
  MatchPlayer* awayOverlappingFullback =
      selectFinalThirdPlayer(false, Arrival::FULLBACK);

  const auto selectActiveSupporters = [&](bool homeTeam)
  {
    std::array<MatchPlayer*, MatchTuning::Shape::MAX_ACTIVE_SUPPORTERS>
        supporters{};
    std::array<float, MatchTuning::Shape::MAX_ACTIVE_SUPPORTERS> scores;
    scores.fill(std::numeric_limits<float>::max());
    if (!carrier || carrier->isHomeTeam != homeTeam) return supporters;

    const auto& runners = homeTeam ? homeRunners : awayRunners;
    const MatchPlayer* midfieldArrival =
        homeTeam ? homeMidfieldArrival : awayMidfieldArrival;
    const MatchPlayer* farPostRunner =
        homeTeam ? homeFarPostRunner : awayFarPostRunner;
    const MatchPlayer* overlappingFullback =
        homeTeam ? homeOverlappingFullback : awayOverlappingFullback;
    for (MatchPlayer* const pointer : onPitch(homeTeam))
    {
      MatchPlayer& candidate = *pointer;
      if (&candidate == carrier || candidate.isGoalkeeper ||
          &candidate == midfieldArrival || &candidate == farPostRunner ||
          &candidate == overlappingFullback ||
          std::ranges::find(runners, &candidate) != runners.end())
      {
        continue;
      }

      const PlayerRole role = candidate.player->getRole();
      const bool eligibleSupportRole =
          role == PlayerRole::CDM || role == PlayerRole::CM ||
          role == PlayerRole::CAM || role == PlayerRole::LM ||
          role == PlayerRole::RM || role == PlayerRole::LW ||
          role == PlayerRole::RW || role == PlayerRole::ST;
      if (!eligibleSupportRole) continue;

      const float score =
          distance(candidate.position, carrier->position) -
          (candidate.intent == PlayerIntent::OFFER_SUPPORT
               ? MatchTuning::Shape::SUPPORT_SELECTION_CONTINUITY_METRES
               : 0.0f);
      for (std::size_t slot = 0; slot < supporters.size(); ++slot)
      {
        if (score >= scores[slot]) continue;
        for (std::size_t shifted = supporters.size() - 1; shifted > slot;
             --shifted)
        {
          supporters[shifted] = supporters[shifted - 1];
          scores[shifted] = scores[shifted - 1];
        }
        supporters[slot] = &candidate;
        scores[slot] = score;
        break;
      }
    }
    return supporters;
  };

  const auto homeSupporters = selectActiveSupporters(true);
  const auto awaySupporters = selectActiveSupporters(false);

  const auto findCoverOutlet = [&](bool defendingHome) -> const MatchPlayer*
  {
    if (!carrier || carrier->isHomeTeam == defendingHome) return nullptr;
    const float possessionDirection = carrier->isHomeTeam ? 1.0f : -1.0f;
    const MatchPlayer* outlet = nullptr;
    float bestScore = std::numeric_limits<float>::max();
    for (const MatchPlayer* const pointer : onPitch(carrier->isHomeTeam))
    {
      const MatchPlayer& candidate = *pointer;
      if (&candidate == carrier || candidate.isGoalkeeper) continue;
      const float outletDistance =
          distance(candidate.position, carrier->position);
      if (outletDistance > MatchTuning::Shape::COVER_OUTLET_MAX_DISTANCE_METRES)
        continue;
      const bool forwardOption =
          (candidate.position.x - carrier->position.x) * possessionDirection >
          0.0f;
      const float score =
          outletDistance -
          (forwardOption ? MatchTuning::Shape::COVER_FORWARD_OPTION_METRES
                         : 0.0f);
      if (score < bestScore)
      {
        bestScore = score;
        outlet = &candidate;
      }
    }
    return outlet;
  };

  const MatchPlayer* homeCoverOutlet = findCoverOutlet(true);
  const MatchPlayer* awayCoverOutlet = findCoverOutlet(false);

  if (MatchRecorder* recorder = getRecorder())
  {
    // Reported with the scores that ranked them; recomputing them reads the
    // same frozen snapshot and draws nothing.
    const auto idOf = [](const MatchPlayer* player)
    { return player && player->player ? player->player->getId() : 0U; };
    const auto report = [&](bool homeTeam)
    {
      MatchTeamPlan plan;
      plan.homeTeam = homeTeam;
      plan.phase = homeTeam ? homePhase : awayPhase;
      const auto& pressers = homeTeam ? homePressers : awayPressers;
      for (std::size_t index = 0; index < plan.pressers.size(); ++index)
      {
        plan.pressers[index] = idOf(pressers[index]);
        if (pressers[index])
          plan.pressArrivalSeconds[index] =
              estimatedArrivalTime(*pressers[index]);
      }
      const auto& runners = homeTeam ? homeRunners : awayRunners;
      for (std::size_t index = 0; index < plan.runners.size(); ++index)
      {
        plan.runners[index] = idOf(runners[index]);
        if (runners[index])
          plan.runPriority[index] = runPriority(*runners[index]);
      }
      plan.midfieldArrival =
          idOf(homeTeam ? homeMidfieldArrival : awayMidfieldArrival);
      plan.farPostRunner =
          idOf(homeTeam ? homeFarPostRunner : awayFarPostRunner);
      plan.overlappingFullback =
          idOf(homeTeam ? homeOverlappingFullback : awayOverlappingFullback);
      const auto& supporters = homeTeam ? homeSupporters : awaySupporters;
      for (std::size_t index = 0; index < plan.supporters.size(); ++index)
        plan.supporters[index] = idOf(supporters[index]);
      plan.coverOutlet = idOf(homeTeam ? homeCoverOutlet : awayCoverOutlet);
      recorder->onTeamPlan(*this, plan);
    };
    report(planHome);

    if (recorder->wantsDetail())
    {
      // Debugger detail: the two rankings broken down, for every candidate.
      // The same terms as estimatedArrivalTime() and runPriority() above.
      const auto arrivalTerms = [&](const MatchPlayer& candidate)
      {
        ScoreBreakdown score;
        score.total = estimatedArrivalTime(candidate);
        const Vector2F toTarget =
            pressurePosition - candidate.position;
        const float targetDistance = length(toTarget);
        if (targetDistance > EPSILON)
        {
          const Vector2F direction{toTarget.x / targetDistance,
                                   toTarget.y / targetDistance};
          const float toward =
              std::max(0.0f, candidate.velocity.x * direction.x +
                                 candidate.velocity.y * direction.y);
          const float speed = std::max(
              currentTopSpeed(candidate) +
                  toward * MatchTuning::Player::CURRENT_VELOCITY_PURSUIT_WEIGHT,
              MatchTuning::Player::MINIMUM_PURSUIT_SPEED);
          score.add("distance / pursuit speed (Pace, stamina, momentum)",
                    TermSource::SITUATION, targetDistance / speed);
        }
        if (carrier && carrier->isHomeTeam != candidate.isHomeTeam)
        {
          score.add("role's press eagerness", TermSource::SLOT_ROLE,
                    -roleProfileOf(candidate).pressBias *
                        TacticsTuning::PRESS_BIAS_SECONDS);
          score.add("team talk", TermSource::MORALE,
                    -talkSwing(candidate.isHomeTeam) *
                        TacticsTuning::TALK_PRESS_SECONDS);
        }
        if (candidate.intent == PlayerIntent::PRESS_BALL ||
            candidate.intent == PlayerIntent::CLAIM_LOOSE_BALL)
          score.add("already going (continuity)", TermSource::SITUATION,
                    -MatchTuning::Shape::PRESSER_CONTINUITY_SECONDS);
        // Arrival times cannot go below zero.
        if (const float clamp = score.residual(); std::abs(clamp) > 1e-6f)
          score.add("clamped at zero", TermSource::SITUATION, clamp);
        return score;
      };
      const auto runTerms = [&](const MatchPlayer& candidate)
      {
        ScoreBreakdown score;
        score.total = runPriority(candidate);
        float rolePriority = MatchTuning::Shape::MIDFIELDER_RUN_PRIORITY;
        switch (candidate.player->getRole())
        {
          case PlayerRole::ST:
            rolePriority = MatchTuning::Shape::STRIKER_RUN_PRIORITY;
            break;
          case PlayerRole::LW:
          case PlayerRole::RW:
            rolePriority = MatchTuning::Shape::WINGER_RUN_PRIORITY;
            break;
          case PlayerRole::CAM:
            rolePriority =
                MatchTuning::Shape::ATTACKING_MIDFIELDER_RUN_PRIORITY;
            break;
          case PlayerRole::CDM:
            rolePriority = MatchTuning::Shape::HOLDING_MIDFIELDER_RUN_PRIORITY;
            break;
          default:
            break;
        }
        const float depth = candidate.isHomeTeam
                                                 ? pitch.fractionX(candidate.position)
                                                 : 1.0f - pitch.fractionX(candidate.position);
        const float separation =
            carrier ? pitch.fractionY(std::abs(candidate.position.y - carrier->position.y))
                    : 0.0f;
        score.add("his position (ST, W, CAM…)", TermSource::NATURAL_POSITION,
                  rolePriority);
        score.add("variety between attacks", TermSource::NOISE,
                  epochNoise(candidate.player->getId(), 0x7a11edU,
                             RUN_TIMING_EPOCH_STEPS) *
                      MatchTuning::Shape::RUN_VARIETY_PRIORITY);
        score.add("Pace", TermSource::ATTRIBUTE,
                  candidate.pace * MatchTuning::Shape::RUN_PACE_PRIORITY);
        score.add("how far forward he is", TermSource::LOCATION,
                  depth * MatchTuning::Shape::RUN_DEPTH_PRIORITY);
        score.add("width away from the ball", TermSource::SITUATION,
                  separation * MatchTuning::Shape::RUN_SEPARATION_PRIORITY);
        score.add("already running (continuity)", TermSource::SITUATION,
                  candidate.isMakingRun
                      ? MatchTuning::Shape::RUN_CONTINUITY_PRIORITY
                      : 0.0f);
        score.add(
            "role's run bias", TermSource::SLOT_ROLE,
            roleProfileOf(candidate).runBias * TacticsTuning::RUN_BIAS_WEIGHT);
        return score;
      };
      {
        const bool homeTeam = planHome;
        MatchRankingDetail toBall;
        toBall.kind = MatchRankingDetail::Kind::TO_BALL;
        toBall.homeTeam = homeTeam;
        MatchRankingDetail runs;
        runs.kind = MatchRankingDetail::Kind::RUN_PRIORITY;
        runs.homeTeam = homeTeam;
        const bool selectingRuns = (homeTeam ? homeRunners : awayRunners)[0];
        for (MatchPlayer* const pointer : onPitch(homeTeam))
        {
          const MatchPlayer& candidate = *pointer;
          if (candidate.isGoalkeeper) continue;
          toBall.candidates.push_back(
              {idOf(&candidate), arrivalTerms(candidate)});
          if (selectingRuns && &candidate != carrier && !candidate.isInjured &&
              std::isfinite(runPriority(candidate)))
            runs.candidates.push_back({idOf(&candidate), runTerms(candidate)});
        }
        recorder->onRankingDetail(*this, toBall);
        if (!runs.candidates.empty()) recorder->onRankingDetail(*this, runs);
      }
    }
  }

  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != planHome) continue;
    const bool goalkeeper = player.isGoalkeeper;
    const StrategySliders& sliders =
        player.isHomeTeam ? homeSliders : awaySliders;
    const TeamPhase teamPhase = player.isHomeTeam ? homePhase : awayPhase;
    const float attackDirection = player.isHomeTeam ? 1.0f : -1.0f;
    const bool ownPossession =
        carrier && carrier->isHomeTeam == player.isHomeTeam;
    // While his side has the ball (or its pass is on the way) every slot
    // starts from its attacking-phase spot (role and in-possession shape),
    // otherwise from the formation.
    const bool ownControl =
        carrier ? ownPossession
                : getPossessionHistory().lastControlledTeamHome &&
                      *getPossessionHistory().lastControlledTeamHome == player.isHomeTeam;
    PitchPoint target =
        ownControl ? possessionAnchorAardvark(player) : player.basePosition;
    player.intent = PlayerIntent::HOLD_SHAPE;
    player.isPressing = false;
    player.isMakingRun = false;

    // The whole block follows the ball while preserving its formation. This
    // produces recognizable defensive, middle and attacking lines rather than
    // twenty outfield players independently chasing one point.
    const float widthScale =
        MatchTuning::Shape::MIN_WIDTH_SCALE +
        sliders.widthUsage * MatchTuning::Shape::WIDTH_SLIDER_SCALE;
    const float centreX = pitch.atX(MatchTuning::Pitch::CENTRE);
    const float centreY = pitch.atY(MatchTuning::Pitch::CENTRE);
    target.y = centreY + (target.y - centreY) * widthScale;
    target.x += (pressurePosition.x - centreX) *
                (MatchTuning::Shape::BASE_LONGITUDINAL_SHIFT +
                 sliders.compactness *
                     MatchTuning::Shape::COMPACTNESS_LONGITUDINAL_SHIFT);
    target.y +=
        (pressurePosition.y - centreY) *
        (MatchTuning::Shape::BASE_LATERAL_SHIFT +
         sliders.compactness * MatchTuning::Shape::COMPACTNESS_LATERAL_SHIFT);

    // Out of possession the block drops and compacts around the ball: the
    // back line sits deeper the closer the ball is to goal, and the lines
    // behind it squeeze into a 20-40 m long, ~45 m wide unit.
    const bool defending =
        !goalkeeper &&
        (carrier ? carrier->isHomeTeam != player.isHomeTeam
                 : getPossessionHistory().lastControlledTeamHome &&
                       *getPossessionHistory().lastControlledTeamHome != player.isHomeTeam);
    if (defending)
    {
      using S = MatchTuning::Shape;
      const auto ownGoalDepth = [&](float x)
      {
        const float fraction = pitch.fractionX(x);
        return player.isHomeTeam ? fraction : 1.0f - fraction;
      };
      const float ballDepth = ownGoalDepth(pressurePosition.x);
      const float lineDepth = std::clamp(
          S::BLOCK_LINE_BASE + ballDepth * S::BLOCK_LINE_BALL_FACTOR +
              sliders.pressing * S::BLOCK_LINE_PRESSING,
          S::BLOCK_LINE_MIN, S::BLOCK_LINE_MAX);
      const float lengthShare =
          std::clamp((ballDepth - S::BLOCK_SHORT_BALL_DEPTH) /
                         (S::BLOCK_LONG_BALL_DEPTH - S::BLOCK_SHORT_BALL_DEPTH),
                     0.0f, 1.0f);
      const float teamLength =
          (S::BLOCK_SHORT_LENGTH +
           (S::BLOCK_LONG_LENGTH - S::BLOCK_SHORT_LENGTH) * lengthShare) *
          (1.0f - sliders.compactness * S::BLOCK_COMPACTNESS_SQUEEZE);
      const float formationShare = std::clamp(
          (ownGoalDepth(player.basePosition.x) - S::BLOCK_FORMATION_BACK) /
              (S::BLOCK_FORMATION_FRONT - S::BLOCK_FORMATION_BACK),
          0.0f, 1.0f);
      const float depth = lineDepth + formationShare * teamLength;
      target.x = pitch.atX(player.isHomeTeam ? depth : 1.0f - depth);
      target.y = centreY +
                 (player.basePosition.y - centreY) *
                     (S::BLOCK_WIDTH_SCALE -
                      sliders.compactness * S::BLOCK_COMPACTNESS_NARROWING) +
                 (pressurePosition.y - centreY) *
                     S::BLOCK_BALL_SIDE_SHIFT;
      target.x += defensiveRoleShiftAardvark(player);
    }

    if (&player == carrier)
    {
      player.intent = PlayerIntent::CARRY_BALL;
      target = player.position;
      target.x +=
          attackDirection *
          (MatchTuning::Shape::CARRIER_BASE_ADVANCE_METRES +
           player.dribbling * MatchTuning::Shape::CARRIER_DRIBBLING_ADVANCE_METRES +
           sliders.riskTaking * MatchTuning::Shape::CARRIER_RISK_ADVANCE_METRES);
      if (teamPhase == TeamPhase::ATTACKING_TRANSITION)
      {
        target.x += attackDirection *
                              MatchTuning::Shape::ATTACKING_TRANSITION_CARRIER_ADVANCE_METRES;
      }

      // Carry away from the nearest defender instead of running directly
      // through them, while gradually looking for a central shooting lane.
      const MatchPlayer* closestOpponent = nullptr;
      float closestDistance = std::numeric_limits<float>::max();
      for (const MatchPlayer* const opponent : onPitch(!player.isHomeTeam))
      {
        const float opponentDistance =
            distance(opponent->position, player.position);
        if (opponentDistance < closestDistance)
        {
          closestDistance = opponentDistance;
          closestOpponent = opponent;
        }
      }
      // A wide forward cutting inside heads for the box; otherwise wide
      // players keep their lane to reach the byline and cross.
      using S = MatchTuning::Shape;
      const PlayerRole role = player.player->getRole();
      const bool wideLane =
          (role == PlayerRole::LM || role == PlayerRole::RM ||
           role == PlayerRole::LW || role == PlayerRole::RW ||
           role == PlayerRole::LB || role == PlayerRole::RB) &&
          std::abs(pitch.fractionY(player.position) - MatchTuning::Pitch::CENTRE) >
              S::WIDE_LANE_DEVIATION;
      if (cutsInsideAardvark(player))
      {
        target.y = player.position.y +
                   (centreY - player.position.y) * S::CUT_INSIDE_PULL;
      }
      else if (closestOpponent &&
               closestDistance < S::CARRIER_EVASION_RANGE_METRES)
      {
        const float evadeDirection =
            player.position.y <= closestOpponent->position.y ? -1.0f : 1.0f;
        target.y +=
            evadeDirection * (S::CARRIER_BASE_EVASION_METRES +
                              player.dribbling * S::CARRIER_DRIBBLING_EVASION_METRES);
      }
      else if (wideLane)
      {
        target.y = player.position.y;
      }
      else
      {
        target.y += (centreY - player.position.y) * S::CARRIER_CENTRALITY;
      }
    }
    else if (ownPossession)
    {
      player.intent = PlayerIntent::HOLD_SHAPE;
      const float attackingProgress =
          player.isHomeTeam ? pitch.fractionX(carrier->position)
                            : 1.0f - pitch.fractionX(carrier->position);
      target.x += pitch.atX(
          attackDirection *
          std::max(0.0f, attackingProgress -
                             MatchTuning::Shape::POSSESSION_PROGRESS_START) *
          MatchTuning::Shape::POSSESSION_BLOCK_PROGRESS);
      target.x += pitch.atX(
          attackDirection * (MatchTuning::Shape::SUPPORT_BASE_ADVANCE +
                             sliders.offensiveBias *
                                 MatchTuning::Shape::SUPPORT_OFFENSIVE_ADVANCE));
      const PlayerRole role = player.player->getRole();
      const bool forward = role == PlayerRole::ST || role == PlayerRole::LW ||
                           role == PlayerRole::RW || role == PlayerRole::CAM;
      const auto& runners = player.isHomeTeam ? homeRunners : awayRunners;
      const auto runnerPosition = std::ranges::find(runners, &player);
      const bool committedRunner = runnerPosition != runners.end();
      if (committedRunner)
      {
        const std::size_t runnerSlot = static_cast<std::size_t>(
            std::distance(runners.begin(), runnerPosition));
        player.intent = runnerSlot == 0 ? PlayerIntent::RUN_IN_BEHIND
                                        : PlayerIntent::ATTACK_BOX;
        player.isMakingRun = true;
        const float defenderLine = lineFor(player.isHomeTeam);
        const float legalRunLine =
            player.isHomeTeam ? std::max(defenderLine, ball.position.x)
                              : std::min(defenderLine, ball.position.x);
        target.x += pitch.atX(
            attackDirection *
            (MatchTuning::Shape::RUN_BASE_ADVANCE +
             sliders.riskTaking * MatchTuning::Shape::RUN_RISK_ADVANCE));
        // Run timing is imperfect: runners hover around the line and now and
        // then drift beyond it, which is where offsides come from.
        const float timing =
            defenderLine * attackDirection >= ball.position.x * attackDirection
                ? (epochNoise(player.player->getId(), 0x0ff51deU,
                              RUN_TIMING_EPOCH_STEPS) *
                       0.5f +
                   0.25f) *
                      MatchTuning::Passing::RUN_TIMING_GAMBLE_METRES
                : 0.0f;
        const float onsideTarget =
            legalRunLine -
            attackDirection *
                      (MatchTuning::Shape::RUN_ONSIDE_BUFFER_METRES - timing);
        const float runDepthTarget =
            onsideTarget -
            attackDirection * static_cast<float>(runnerSlot) *
                      MatchTuning::Shape::SECONDARY_RUN_DEPTH_STAGGER_METRES;
        target.x += (runDepthTarget - target.x) *
                    MatchTuning::Shape::RUN_DEPTH_TARGET_PULL;
        target.x = player.isHomeTeam ? std::min(target.x, onsideTarget)
                                     : std::max(target.x, onsideTarget);

        float channel = runChannel(player);
        if (runnerSlot > 0 && runners[0])
        {
          const float primaryChannel = runChannel(*runners[0]);
          if (std::abs(channel - primaryChannel) <
              MatchTuning::Shape::MINIMUM_RUN_CHANNEL_SEPARATION)
          {
            channel = primaryChannel <= MatchTuning::Pitch::CENTRE
                          ? MatchTuning::Shape::RIGHT_INSIDE_FORWARD_CHANNEL
                          : MatchTuning::Shape::LEFT_INSIDE_FORWARD_CHANNEL;
          }
        }
        target.y +=
            (pitch.atY(channel) - target.y) * MatchTuning::Shape::RUN_CHANNEL_BLEND;

        // Only a runner caught clearly beyond the line checks back; one who
        // is marginally off relies on his timing (and sometimes gets caught).
        if (isOffside(player, player.isHomeTeam, defenderLine,
                      MatchTuning::Passing::RUN_TIMING_GAMBLE_METRES * 0.75f))
          target.x -= attackDirection * MatchTuning::Shape::ONSIDE_RECOVERY_METRES;
      }
      else
      {
        MatchPlayer* midfieldArrival =
            player.isHomeTeam ? homeMidfieldArrival : awayMidfieldArrival;
        MatchPlayer* farPostRunner =
            player.isHomeTeam ? homeFarPostRunner : awayFarPostRunner;
        MatchPlayer* overlappingFullback = player.isHomeTeam
                                               ? homeOverlappingFullback
                                               : awayOverlappingFullback;
        const auto& supporters =
            player.isHomeTeam ? homeSupporters : awaySupporters;
        const auto supportPosition = std::ranges::find(supporters, &player);
        if (&player == midfieldArrival || &player == farPostRunner)
        {
          // Late runs into the box: the midfielder to the penalty spot on
          // the side away from the ball, the wide man to the far post; both
          // stay onside.
          using S = MatchTuning::Shape;
          player.intent = PlayerIntent::ATTACK_BOX;
          const bool farPost = &player == farPostRunner;
          const float goalX = player.isHomeTeam ? pitch.length : 0.0f;
          const float ballSide =
              carrier->position.y < centreY ? -1.0f : 1.0f;
          const float arrivalX =
              goalX - attackDirection * (farPost ? S::FAR_POST_ARRIVAL_DEPTH_METRES
                                                 : S::MIDFIELD_ARRIVAL_DEPTH_METRES);
          const float onside =
              lineFor(player.isHomeTeam) -
              attackDirection * MatchTuning::Shape::RUN_ONSIDE_BUFFER_METRES;
          target.x = player.isHomeTeam ? std::min(arrivalX, onside)
                                       : std::max(arrivalX, onside);
          target.y = pitch.atY(MatchTuning::Pitch::CENTRE) -
                               ballSide * (farPost ? S::FAR_POST_ARRIVAL_WIDTH_METRES
                                                   : S::MIDFIELD_ARRIVAL_WIDTH_METRES);
        }
        else if (&player == overlappingFullback)
        {
          player.intent = PlayerIntent::OVERLAP;
          target.x += pitch.atX(attackDirection *
                                MatchTuning::Shape::FINAL_THIRD_FULLBACK_OVERLAP);
        }
        else if (supportPosition != supporters.end())
        {
          player.intent = PlayerIntent::OFFER_SUPPORT;
          const std::size_t supportSlot = static_cast<std::size_t>(
              std::distance(supporters.begin(), supportPosition));
          const float primarySide =
              supporters[MatchTuning::Shape::NEAR_SUPPORT_SLOT] &&
                      supporters[MatchTuning::Shape::NEAR_SUPPORT_SLOT]
                              ->basePosition.y <= carrier->position.y
                  ? -1.0f
                  : 1.0f;
          float supportDepth = MatchTuning::Shape::NEAR_SUPPORT_DEPTH_METRES;
          float supportWidth = MatchTuning::Shape::NEAR_SUPPORT_WIDTH_METRES;
          float supportSide = primarySide;
          if (supportSlot == MatchTuning::Shape::SQUARE_SUPPORT_SLOT)
          {
            supportDepth = MatchTuning::Shape::SQUARE_SUPPORT_DEPTH_METRES;
            supportWidth = MatchTuning::Shape::SQUARE_SUPPORT_WIDTH_METRES;
            supportSide = -primarySide;
          }
          else if (supportSlot == MatchTuning::Shape::TRAILING_SUPPORT_SLOT)
          {
            supportDepth = MatchTuning::Shape::TRAILING_SUPPORT_DEPTH_METRES;
            supportWidth = MatchTuning::Shape::TRAILING_SUPPORT_WIDTH_METRES;
          }

          target = carrier->position;
          target.x -= attackDirection * supportDepth;
          target.y += supportSide * supportWidth;
          if (teamPhase == TeamPhase::ATTACKING_TRANSITION)
          {
            target.x += pitch.atX(attackDirection *
                                  MatchTuning::Shape::TRANSITION_SUPPORT_FORWARD_BONUS);
          }
        }
        else if (forward)
        {
          player.intent = PlayerIntent::OFFER_SUPPORT;
          // Not every attacker runs beyond the defence. A complementary
          // forward checks toward the ball to form a passing triangle and drag
          // a marker.
          target.x =
              carrier->position.x -
              attackDirection * MatchTuning::Shape::FORWARD_SHORT_OPTION_DEPTH_METRES;
          const float lateralDirection =
              player.basePosition.y <= carrier->position.y ? -1.0f : 1.0f;
          target.y =
              carrier->position.y +
              lateralDirection *
                        MatchTuning::Shape::FORWARD_SHORT_OPTION_LATERAL_SEPARATION_METRES;
        }
        if (distance(target, carrier->position) >
            MatchTuning::Shape::MAX_SUPPORT_DISTANCE_METRES)
        {
          target.x += (carrier->position.x - target.x) *
                      MatchTuning::Shape::SUPPORT_LONGITUDINAL_PULL;
          target.y += (carrier->position.y - target.y) *
                      MatchTuning::Shape::SUPPORT_LATERAL_PULL;
        }
      }
    }
    else if (carrier)
    {
      const auto& pressers = player.isHomeTeam ? homePressers : awayPressers;
      if (pressers[0] == &player)
      {
        player.intent = PlayerIntent::PRESS_BALL;
        player.isPressing = true;
        const float standOff =
            (MatchTuning::Shape::PRESSING_STANDOFF_BASE_METRES +
             (1.0f - sliders.pressing) *
                 MatchTuning::Shape::PRESSING_STANDOFF_CAUTIOUS_BONUS_METRES) *
            pressStandOffShareAardvark(player, *carrier);
        target = pressurePosition;
        target.x -= attackDirection * standOff;
        target.y += weakFootShadeAardvark(*carrier);
      }
      else if (const auto doubling =
                   pressers[1] == &player
                       ? doubleUpPointAardvark(player, *carrier, pressers[0])
                       : std::nullopt)
      {
        // Ordered to double up: the second man closes the carrier too.
        player.intent = PlayerIntent::COVER_PRESS;
        player.isPressing = true;
        target = *doubling;
      }
      else if (pressers[1] == &player &&
               sliders.pressing > MatchTuning::Shape::COVER_PRESS_MINIMUM)
      {
        player.isPressing = true;
        const MatchPlayer* coverOutlet =
            player.isHomeTeam ? homeCoverOutlet : awayCoverOutlet;
        if (coverOutlet)
        {
          player.intent = PlayerIntent::BLOCK_PASSING_LANE;
          target.x = pressurePosition.x +
                     (coverOutlet->position.x - pressurePosition.x) *
                         MatchTuning::Shape::COVER_LANE_INTERCEPTION_POINT;
          target.y = pressurePosition.y +
                     (coverOutlet->position.y - pressurePosition.y) *
                         MatchTuning::Shape::COVER_LANE_INTERCEPTION_POINT;
          target.x -= attackDirection * MatchTuning::Shape::COVER_LANE_GOAL_SIDE_OFFSET_METRES;
        }
        else
        {
          player.intent = PlayerIntent::COVER_PRESS;
          const float lateralSide =
              player.position.y <= pressurePosition.y ? -1.0f : 1.0f;
          target.x = pressurePosition.x -
                     attackDirection * MatchTuning::Shape::COVER_FALLBACK_DEPTH_METRES;
          target.y =
              pressurePosition.y +
              lateralSide * MatchTuning::Shape::COVER_FALLBACK_LATERAL_OFFSET_METRES;
        }
      }
      else if (const MatchPlayer* tight = tightMarkTargetAardvark(player))
      {
        // Under a tight-marking order he stays with his man in every phase,
        // goal-side and close enough to contest each ball played to him.
        player.intent = PlayerIntent::MARK_OPPONENT;
        target = tightMarkPointAardvark(player, *tight, target);
      }
      else if (teamPhase == TeamPhase::DEFENSIVE_TRANSITION)
      {
        // The nearest players counter-press above. Everyone else first gets
        // goal-side and narrows toward the danger before settling into the
        // normal defensive block.
        player.intent = PlayerIntent::RECOVER_SHAPE;
        target.x -= pitch.atX(attackDirection * MatchTuning::Shape::DEFENSIVE_TRANSITION_RECOVERY);
        target.y += (pressurePosition.y - target.y) *
                    MatchTuning::Shape::DEFENSIVE_TRANSITION_BALL_COMPACTNESS;
      }
      else
      {
        // Zonal marking: shade toward the opponent assigned to this player
        // (one marker per opponent), without abandoning the formation anchor.
        const std::int8_t assigned = markAssignments[slotOf(player)];
        const MatchPlayer* mark =
            assigned >= 0 && static_cast<std::size_t>(assigned) < players.size()
                ? &players[static_cast<std::size_t>(assigned)]
                : nullptr;
        if (mark && (!active(*mark) || mark->isHomeTeam == player.isHomeTeam))
          mark = nullptr;
        if (mark)
        {
          player.intent = PlayerIntent::MARK_OPPONENT;
          float markWeight =
              MatchTuning::Shape::BASE_MARK_WEIGHT +
              sliders.compactness * MatchTuning::Shape::COMPACTNESS_MARK_WEIGHT;
          PitchPoint markPoint = mark->position;
          const float markFromGoal =
              pitch.fromOwnGoalLine(mark->position, player.isHomeTeam);
          if (markFromGoal <= MatchTuning::Shape::TIGHT_MARK_WITHIN_METRES)
          {
            // Near the own goal a marker stays tight and goal-side.
            markWeight =
                std::max(markWeight, MatchTuning::Shape::TIGHT_MARK_WEIGHT);
            markPoint.x -= attackDirection *
                                           MatchTuning::Shape::GOAL_SIDE_MARK_METRES;
          }
          target.x += (markPoint.x - target.x) * markWeight;
          target.y += (markPoint.y - target.y) * markWeight;
        }
      }
    }
    else
    {
      const bool receivingPass =
          getPossessionState().isPass && getPossessionState().passByHome == player.isHomeTeam;
      const auto& pressers = player.isHomeTeam ? homePressers : awayPressers;
      if (receivingPass)
      {
        if (player.player == getExpectedReceiver())
        {
          // Meet the ball on its path instead of chasing where it is: the
          // closest point of the remaining flight line ahead of the ball.
          player.intent = PlayerIntent::RECEIVE_PASS;
          const float ballSpeed = length(ball.velocity);
          const Vector2F lookahead = {ball.velocity.x *
                   MatchTuning::Player::PASS_RECEIVER_LOOKAHEAD_SECONDS,
               ball.velocity.y *
                   MatchTuning::Player::PASS_RECEIVER_LOOKAHEAD_SECONDS};
          Vector2F meet{ball.position.x + lookahead.x,
                        ball.position.y + lookahead.y};
          const float heightMetres = ball.z;
          if (heightMetres > MatchTuning::Aerial::CONTROL_CEILING_METRES ||
              ball.velocityZ > 0.0f)
          {
            // A lofted ball is met where it drops to a controllable height,
            // not intercepted on the way up.
            constexpr float G = MatchTuning::Ball::GRAVITY;
            const float drop = std::max(
                0.0f, heightMetres -
                          MatchTuning::Passing::LOFTED_ARRIVAL_HEIGHT_METRES);
            const float fallSeconds =
                (ball.velocityZ +
                 std::sqrt(ball.velocityZ * ball.velocityZ + 2.0f * G * drop)) /
                G;
            const Vector2F landing =
                {ball.velocity.x * fallSeconds * AIR_TRAVEL_SHARE,
                         ball.velocity.y * fallSeconds * AIR_TRAVEL_SHARE};
            meet = {ball.position.x + landing.x, ball.position.y + landing.y};
          }
          else if (ballSpeed > EPSILON)
          {
            const Vector2F heading{ball.velocity.x / ballSpeed,
                                   ball.velocity.y / ballSpeed};
            const Vector2F offset =
                player.position - ball.position;
            const float along = offset.x * heading.x + offset.y * heading.y;
            if (along > 0.0f)
            {
              const Vector2F ahead =
                  {heading.x * along, heading.y * along};
              meet = {ball.position.x + ahead.x, ball.position.y + ahead.y};
            }
          }
          target = PitchPoint{clampToPitchX(meet.x), clampToPitchY(meet.y)};
        }
        else
        {
          player.intent = PlayerIntent::OFFER_SUPPORT;
          // Preserve the lane established before release instead of snapping
          // every supporting player back toward the formation anchor for the
          // duration of each pass; drift gently forward and toward the ball
          // (rates per second, so a long pass does not accumulate a run).
          target = player.movementTarget;
          target.x += pitch.atX(attackDirection * MatchTuning::Shape::IN_FLIGHT_SUPPORT_ADVANCE_PER_SECOND * dt);
          target.y +=
              (ball.position.y - target.y) *
              MatchTuning::Shape::IN_FLIGHT_SUPPORT_BALL_PULL_PER_SECOND * dt;
        }
      }
      // For a genuinely loose ball, only the nearest players contest it.
      // Everybody else either joins a selected transition run or recovers the
      // team shape. A normal pass is handled above so it does not trigger a
      // full-team scramble on every release of the ball.
      else if (pressers[0] == &player ||
               (pressers[1] == &player &&
                sliders.pressing >
                    MatchTuning::Shape::SECOND_LOOSE_BALL_PRESS_THRESHOLD))
      {
        player.intent = pressers[0] == &player ? PlayerIntent::CLAIM_LOOSE_BALL
                                               : PlayerIntent::COVER_PRESS;
        player.isPressing = true;
        // Run at where the ball will be when he gets there.
        const float lookaheadSeconds =
            std::min(distance(player.position, pressurePosition) /
                         std::max(currentTopSpeed(player), EPSILON),
                     MatchTuning::Player::MAX_LOOSE_BALL_LOOKAHEAD_SECONDS);
        const Vector2F lead = {ball.velocity.x * lookaheadSeconds,
                                       ball.velocity.y * lookaheadSeconds};
        target = PitchPoint{clampToPitchX(pressurePosition.x + lead.x),
                            clampToPitchY(pressurePosition.y + lead.y)};
        if (pressers[1] == &player)
        {
          target.x -= attackDirection * MatchTuning::Player::COVER_LOOSE_BALL_OFFSET_METRES;
        }
      }
      else if (getPossessionState().isPass || getPossessionState().isShot)
      {
        const MatchPlayer* tight =
            getPossessionState().isPass && getPossessionState().passByHome != player.isHomeTeam
                ? tightMarkTargetAardvark(player)
                : nullptr;
        if (tight)
        {
          // A tight marker goes with his man while the pass is on its way.
          player.intent = PlayerIntent::MARK_OPPONENT;
          target = tightMarkPointAardvark(player, *tight, target);
        }
        else if (teamPhase == TeamPhase::DEFENSIVE_TRANSITION)
        {
          player.intent = PlayerIntent::RECOVER_SHAPE;
          target.x -= pitch.atX(attackDirection * MatchTuning::Shape::DEFENSIVE_TRANSITION_RECOVERY);
          target.y += (pressurePosition.y - target.y) *
                      MatchTuning::Shape::DEFENSIVE_TRANSITION_BALL_COMPACTNESS;
        }
        else
        {
          player.intent = PlayerIntent::HOLD_SHAPE;
        }
      }
      else
      {
        const bool attackingTransition =
            teamPhase == TeamPhase::ATTACKING_TRANSITION;
        if (attackingTransition)
        {
          const auto& runners = player.isHomeTeam ? homeRunners : awayRunners;
          const bool transitionRunner =
              std::ranges::find(runners, &player) != runners.end();
          player.intent = transitionRunner ? PlayerIntent::RUN_IN_BEHIND
                                           : PlayerIntent::OFFER_SUPPORT;
          player.isMakingRun = transitionRunner;
          target.x += pitch.atX(attackDirection * (transitionRunner ? MatchTuning::Shape::ATTACKING_TRANSITION_RUN_ADVANCE : MatchTuning::Shape::ATTACKING_TRANSITION_SUPPORT_ADVANCE));
          target.y += (pressurePosition.y - target.y) *
                      MatchTuning::Shape::ATTACKING_TRANSITION_BALL_PULL;
          if (transitionRunner &&
              isOffside(player, player.isHomeTeam, lineFor(player.isHomeTeam)))
          {
            target.x -= attackDirection * MatchTuning::Shape::ONSIDE_RECOVERY_METRES;
          }
        }
        else
        {
          player.intent = PlayerIntent::RECOVER_SHAPE;
          target.x -= pitch.atX(attackDirection * MatchTuning::Shape::DEFENSIVE_TRANSITION_RECOVERY);
        }
      }
    }

    // A lofted delivery into the box is attacked by the receiver while the
    // defenders hold their marks instead of drifting back into shape.
    if (getPossessionState().isAerialDelivery && !carrier &&
        getPossessionState().passByHome != player.isHomeTeam &&
        player.intent != PlayerIntent::CLAIM_LOOSE_BALL)
    {
      target = player.movementTarget;
    }

    if (goalkeeper)
    {
      const bool receivingBackPass =
          getPossessionState().isPass && getExpectedReceiver() == player.player &&
          player.intent == PlayerIntent::RECEIVE_PASS;
      player.intent = PlayerIntent::GOALKEEP;
      if (keepers[player.isHomeTeam ? 0 : 1].state == GoalkeeperState::DIVE)
        continue;
      // A keeper receiving a team-mate's pass steps onto the ball's path.
      if (!receivingBackPass)
        target = goalkeeperTargetAardvark(player, carrier);
    }

    // Keepers shuffle constantly to stay on the ball-goal line.
    const bool urgentTarget = player.intent == PlayerIntent::PRESS_BALL ||
                              player.intent == PlayerIntent::CLAIM_LOOSE_BALL ||
                              player.intent == PlayerIntent::GOALKEEP;
    player.tacticalTarget = target;
    player.urgentMovement = urgentTarget;
  }
}

bool MatchEngine::cutsInsideAardvark(const MatchPlayer& player) const
{
  using S = MatchTuning::Shape;
  const PlayerRole role = player.player->getRole();
  const bool wideForward = role == PlayerRole::LM || role == PlayerRole::RM ||
                           role == PlayerRole::LW || role == PlayerRole::RW;
  const float depth = player.isHomeTeam
                          ? pitch.fractionX(player.position)
                          : 1.0f - pitch.fractionX(player.position);
  // An inside forward always looks to come inside, a touchline winger
  // never does, whatever his natural position.
  const float roleCut = roleProfileOf(player).cutInside;
  if ((!wideForward && roleCut == 0.0f) ||
      depth < MatchTuning::Rules::HOME_FINAL_THIRD_START)
    return false;
  // The choice holds for a few seconds at a time.
  const float share = std::clamp(
      std::clamp(S::CUT_INSIDE_BASE + (player.shooting - player.passing) *
                                          S::CUT_INSIDE_PREFERENCE,
                 S::MIN_CUT_INSIDE, S::MAX_CUT_INSIDE) +
          roleCut * TacticsTuning::CUT_INSIDE_SHIFT,
      0.0f, 1.0f);
  return (epochNoise(player.player->getId(), 0xc0715eU,
                     RUN_TIMING_EPOCH_STEPS) +
          1.0f) *
             0.5f <
         share;
}

[[gnu::always_inline]] inline PitchPoint MatchEngine::possessionAnchorAardvark(
    const MatchPlayer& player) const
{
  const std::size_t side = player.isHomeTeam ? 0 : 1;
  const auto slot = static_cast<std::size_t>(player.formationSlot);
  if (player.isGoalkeeper || player.formationSlot < 0 ||
      slot >= slotTactics[side].size())
    return player.basePosition;
  const SlotTactic& tactic = slotTactics[side][slot];
  if (tactic.possessionOffset.x == 0.0f && tactic.possessionOffset.y == 0.0f &&
      tactic.profile.possessionAdvanceMetres == 0.0f &&
      tactic.profile.possessionWidthMetres == 0.0f)
    return player.basePosition;
  using Pitch = MatchTuning::Pitch;
  const float direction = player.isHomeTeam ? 1.0f : -1.0f;
  const float lateral =
      pitch.fractionY(player.basePosition.y - pitch.atY(Pitch::CENTRE));
  // Width pushes toward his own touchline; a central player has none.
  const float flank = std::abs(lateral) < 0.05f ? 0.0f
                      : lateral > 0.0f          ? 1.0f
                                                : -1.0f;
  PitchPoint anchor = player.basePosition;
  anchor.x += direction *
              (tactic.possessionOffset.x +
               tactic.profile.possessionAdvanceMetres);
  anchor.y +=
      tactic.possessionOffset.y +
      (flank * tactic.profile.possessionWidthMetres);
  return PitchPoint{clampToPitchX(anchor.x), clampToPitchY(anchor.y)};
}

[[gnu::always_inline]] inline float MatchEngine::defensiveRoleShiftAardvark(
    const MatchPlayer& player) const
{
  return (player.isHomeTeam ? 1.0f : -1.0f) *
                          roleProfileOf(player).defensiveAdvanceMetres;
}

[[gnu::always_inline]] inline const MatchPlayer*
MatchEngine::tightMarkTargetAardvark(const MatchPlayer& marker) const
{
  if (oppositionOrders[marker.isHomeTeam ? 0 : 1].empty()) return nullptr;
  const std::int8_t assigned = markAssignments[slotOf(marker)];
  if (assigned < 0 || static_cast<std::size_t>(assigned) >= players.size())
    return nullptr;
  const MatchPlayer& mark = players[static_cast<std::size_t>(assigned)];
  if (!active(mark) || mark.isHomeTeam == marker.isHomeTeam) return nullptr;
  return instructionAgainst(mark) == OppositionInstruction::TightMark ? &mark
                                                                      : nullptr;
}

PitchPoint MatchEngine::tightMarkPointAardvark(const MatchPlayer& marker,
                                             const MatchPlayer& target,
                                             PitchPoint current) const
{
  namespace T = TacticsTuning;
  const float direction = marker.isHomeTeam ? 1.0f : -1.0f;
  const Vector2F point{
      target.position.x - (direction * T::TIGHT_MARK_GOAL_SIDE_METRES),
      target.position.y};
  return PitchPoint{current.x + (point.x - current.x) * T::TIGHT_MARK_WEIGHT,
          current.y + (point.y - current.y) * T::TIGHT_MARK_WEIGHT};
}

float MatchEngine::tightMarkPenaltyAardvark(const MatchPlayer& receiver) const
{
  if (instructionAgainst(receiver) != OppositionInstruction::TightMark)
    return 0.0f;
  for (std::size_t slot = 0; slot < players.size() && slot < 32; ++slot)
  {
    const MatchPlayer& marker = players[slot];
    if (!active(marker) || marker.isHomeTeam == receiver.isHomeTeam ||
        markAssignments[slot] < 0 ||
        &players[static_cast<std::size_t>(markAssignments[slot])] != &receiver)
      continue;
    if (distance(marker.position, receiver.position) <=
        TacticsTuning::TIGHT_MARK_REACH_METRES)
      return TacticsTuning::TIGHT_MARK_COMPLETION_PENALTY *
             MatchTuning::Passing::COMPLETION_UTILITY_WEIGHT;
  }
  return 0.0f;
}

float MatchEngine::passingRoleBiasAardvark(const MatchPlayer& passer,
                                           const MatchPlayer& receiver,
                                           float progression,
                                           float completion) const
{
  namespace T = TacticsTuning;
  const float daring = roleProfileOf(passer).passDaring;
  const float target = roleProfileOf(receiver).targetBias;
  return daring * (progression * T::PASS_DARING_PROGRESS_WEIGHT +
                   (1.0f - completion) * T::PASS_DARING_SAFETY_WEIGHT) +
         target * T::TARGET_BIAS_WEIGHT * (progression > 0.0f ? 1.0f : 0.4f) -
         tightMarkPenaltyAardvark(receiver);
}

[[gnu::always_inline]] inline float MatchEngine::pressEagernessAardvark(
    const MatchPlayer& candidate, const MatchPlayer* carrier) const
{
  if (!carrier || carrier->isHomeTeam == candidate.isHomeTeam) return 0.0f;
  // A rousing talk sends the side after the ball sooner (a flat one later).
  return roleProfileOf(candidate).pressBias *
             TacticsTuning::PRESS_BIAS_SECONDS +
         talkSwing(candidate.isHomeTeam) * TacticsTuning::TALK_PRESS_SECONDS;
}

float MatchEngine::engageBoostAardvark(const MatchPlayer& defender,
                                       const MatchPlayer& carrier) const
{
  namespace T = TacticsTuning;
  float boost = 1.0f +
                roleProfileOf(defender).pressBias * T::PRESS_BIAS_ENGAGE +
                talkSwing(defender.isHomeTeam) * T::TALK_ENGAGE;
  const OppositionInstruction order = instructionAgainst(carrier);
  if (order == OppositionInstruction::Press) boost += T::PRESS_ORDER_ENGAGE;
  if (order == OppositionInstruction::DoubleUp) boost += T::DOUBLE_UP_ENGAGE;
  return std::max(0.2f, boost);
}

float MatchEngine::pressStandOffShareAardvark(const MatchPlayer& presser,
                                              const MatchPlayer& carrier) const
{
  float share = 1.0f - 0.2f * roleProfileOf(presser).pressBias -
                0.2f * talkSwing(presser.isHomeTeam);
  if (instructionAgainst(carrier) == OppositionInstruction::Press)
    share *= TacticsTuning::PRESS_ORDER_STANDOFF_SHARE;
  return std::clamp(share, 0.3f, 1.3f);
}

float MatchEngine::weakFootShadeAardvark(const MatchPlayer& carrier) const
{
  if (!carrier.player ||
      instructionAgainst(carrier) != OppositionInstruction::ShowWeakFoot)
    return 0.0f;
  // Facing his attacking direction, a right-footer's strong side is +y for
  // the home side (attacking toward x = 1) and -y for the away side.
  const float strongSide =
      (carrier.player->getFoot() == Foot::Right ? 1.0f : -1.0f) *
      (carrier.isHomeTeam ? 1.0f : -1.0f);
  return strongSide * TacticsTuning::WEAK_FOOT_SHADE_METRES;
}

std::optional<PitchPoint> MatchEngine::doubleUpPointAardvark(
    const MatchPlayer& helper, const MatchPlayer& carrier,
    const MatchPlayer* firstPresser) const
{
  if (instructionAgainst(carrier) != OppositionInstruction::DoubleUp)
    return std::nullopt;
  // The second man closes from the side the first presser leaves open,
  // goal-side of the carrier.
  const float lateral =
      firstPresser && firstPresser->position.y > carrier.position.y ? -1.0f
      : firstPresser                                                ? 1.0f
      : helper.position.y <= carrier.position.y                     ? -1.0f
                                                                    : 1.0f;
  const float direction = helper.isHomeTeam ? 1.0f : -1.0f;
  const float reach = TacticsTuning::DOUBLE_UP_SUPPORT_METRES;
  return PitchPoint{
      clampToPitchX(carrier.position.x - (direction * reach * 0.5f)),
      clampToPitchY(carrier.position.y + (lateral * reach))};
}

void MatchEngine::assignMarksAardvark(bool home)
{
  // Greedy one-to-one assignment of a side's outfield markers to opposing
  // outfield players by channel, depth and danger, so nobody is left unmarked
  // while two defenders shadow the same attacker.
  for (std::size_t slot = 0;
       slot < players.size() && slot < markAssignments.size(); ++slot)
    if (players[slot].isHomeTeam == home) markAssignments[slot] = -1;
  const auto score =
      [](bool markerHome, Vector2F base, float opponentX, float opponentY)
  {
    // All in fractions of the pitch.
    const float dangerDepth = markerHome ? 1.0f - opponentX : opponentX;
    return std::abs(opponentY - base.y) * MatchTuning::Shape::CHANNEL_WEIGHT +
           std::abs(opponentX - base.x) -
           dangerDepth * MatchTuning::Shape::DANGER_DEPTH_WEIGHT;
  };
  {
    std::array<std::uint8_t, 16> markers{};
    std::array<std::uint8_t, 16> opponents{};
    std::size_t markerCount = 0;
    std::size_t opponentCount = 0;
    for (std::size_t slot = 0; slot < players.size() && slot < 32; ++slot)
    {
      const MatchPlayer& candidate = players[slot];
      if (!active(candidate) || candidate.isGoalkeeper) continue;
      if (candidate.isHomeTeam == home && markerCount < markers.size())
        markers[markerCount++] = static_cast<std::uint8_t>(slot);
      else if (candidate.isHomeTeam != home && opponentCount < opponents.size())
        opponents[opponentCount++] = static_cast<std::uint8_t>(slot);
    }
    std::array<float, 16 * 16> scores{};
    std::array<float, 16> opponentX{};
    std::array<float, 16> opponentY{};
    for (std::size_t o = 0; o < opponentCount; ++o)
    {
      opponentX[o] = pitch.fractionX(players[opponents[o]].position);
      opponentY[o] = pitch.fractionY(players[opponents[o]].position);
    }
    for (std::size_t m = 0; m < markerCount; ++m)
    {
      const MatchPlayer& marker = players[markers[m]];
      const Vector2F base = pitch.fractions(marker.basePosition);
      for (std::size_t o = 0; o < opponentCount; ++o)
        scores[m * 16 + o] =
            score(marker.isHomeTeam, base, opponentX[o], opponentY[o]);
    }
    // Each round takes the lowest remaining score (ties: lowest marker, then
    // lowest opponent). Every marker keeps its best free opponent, so a round
    // only rescans the markers whose best opponent was just taken.
    std::array<std::uint8_t, 16> bestOf{};
    const auto findBest = [&](std::size_t m, std::uint32_t usedOpponents)
    {
      float best = std::numeric_limits<float>::max();
      std::uint8_t bestOpponent = 0;
      for (std::size_t o = 0; o < opponentCount; ++o)
      {
        if ((usedOpponents & (1U << o)) != 0U) continue;
        if (scores[m * 16 + o] < best)
        {
          best = scores[m * 16 + o];
          bestOpponent = static_cast<std::uint8_t>(o);
        }
      }
      bestOf[m] = bestOpponent;
    };
    std::uint32_t usedMarkers = 0;
    std::uint32_t usedOpponents = 0;
    for (std::size_t m = 0; m < markerCount; ++m) findBest(m, 0U);
    for (std::size_t round = 0; round < std::min(markerCount, opponentCount);
         ++round)
    {
      float best = std::numeric_limits<float>::max();
      std::size_t bestMarker = 0;
      for (std::size_t m = 0; m < markerCount; ++m)
      {
        if ((usedMarkers & (1U << m)) != 0U) continue;
        if (scores[m * 16 + bestOf[m]] < best)
        {
          best = scores[m * 16 + bestOf[m]];
          bestMarker = m;
        }
      }
      const std::uint8_t bestOpponent = bestOf[bestMarker];
      usedMarkers |= 1U << bestMarker;
      usedOpponents |= 1U << bestOpponent;
      markAssignments[markers[bestMarker]] =
          static_cast<std::int8_t>(opponents[bestOpponent]);
      for (std::size_t m = 0; m < markerCount; ++m)
        if ((usedMarkers & (1U << m)) == 0U && bestOf[m] == bestOpponent)
          findBest(m, usedOpponents);
    }
  }
}

void MatchEngine::updateRestartSupportAardvark(MatchPlayer& player,
                                               const MatchPlayer* taker)
{
  if (player.isGoalkeeper || &player == taker || restartIsSetPiece ||
      (state != MatchState::THROW_IN && state != MatchState::GOAL_KICK))
    return;

  player.movementTarget = player.basePosition;
  if (taker && player.isHomeTeam == taker->isHomeTeam)
  {
    const float gap = distance(player.position, ball.position);
    if (gap < MatchTuning::Timing::RESTART_SUPPORT_DISTANCE_METRES)
    {
      // Offer a lane inside the pitch, with teammates spread along it.
      player.movementTarget.x = clampToPitchX(
          ball.position.x + (player.basePosition.x - ball.position.x) *
                                MatchTuning::Timing::RESTART_SUPPORT_BLEND);
      player.movementTarget.y = clampToPitchY(
          ball.position.y + (player.basePosition.y - ball.position.y) *
                                MatchTuning::Timing::RESTART_SUPPORT_BLEND);
    }
  }
  else if (taker)
  {
    const float gap = distance(player.movementTarget, ball.position);
    const float minimum =
        state == MatchState::THROW_IN
            ? MatchTuning::Timing::THROW_OPPONENT_DISTANCE_METRES
            : MatchTuning::Timing::KICK_OPPONENT_DISTANCE_METRES;
    if (gap < minimum)
    {
      const Vector2F direction =
          metricDirection(ball.position, player.movementTarget);
      const Vector2F offset =
          {direction.x * minimum, direction.y * minimum};
      player.movementTarget = PitchPoint{ball.position.x + offset.x,
                               ball.position.y + offset.y};
    }
  }
}

MatchEngine::Challenge MatchEngine::chooseChallengeAardvark(
    const MatchPlayer& carrier, float dt)
{
  // The nearest defender to the ball (not to the man) decides whether to
  // challenge. He waits for the ball to be away from the attacker's foot;
  // near his own box and with a pressing instruction he goes in sooner.
  Challenge challenge;
  MatchPlayer* defender =
      findClosestPlayer(ball.position, !carrier.isHomeTeam, false);
  if (!defender || defender->tackleCooldown > 0.0f || defender->isInjured ||
      defender->fallen ||
      defender->trapTimer > 0.0f || isControlled(*defender))
    return challenge;
  using D = MatchTuning::Defending;
  const float reach = D::TACKLE_DISTANCE_METRES +
                      defender->defending * D::TACKLE_DEFENDING_REACH_METRES;
  const float ballDistance = distance(defender->position, ball.position);
  if (ballDistance >= D::SLIDE_TACKLE_DISTANCE_METRES) return challenge;
  const float carrierDepth =
      carrier.isHomeTeam ? pitch.fractionX(carrier.position)
                           : 1.0f - pitch.fractionX(carrier.position);
  const float engageRate =
      D::ENGAGE_RATE_PER_SECOND *
      (1.0f + getEffectiveSliders(defender->isHomeTeam).pressing *
                  D::PRESSING_ENGAGE_BONUS) *
      (carrierDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START
           ? D::FINAL_THIRD_ENGAGE_FACTOR
           : 1.0f) *
      (D::CLOSE_CONTROL_ENGAGE_SHARE +
       D::EXPOSED_ENGAGE_SHARE * dribbleExposureShare()) *
      (ballDistance > reach ? 0.5f : 1.0f) *
      engageBoostAardvark(*defender, carrier);
  const float engageRoll = randomFloat(0.0f, 1.0f);
  const float engageChance = 1.0f - std::exp(-engageRate * dt);
  if (wantsDetail())
  {
    // Debugger detail: the rate is a product; each factor is noted as
    // what it adds to it.
    MatchDuelDetail duel;
    duel.kind = MatchDuelDetail::Kind::ENGAGE;
    duel.player = defender->player ? defender->player->getId() : 0;
    duel.opponent = carrier.player ? carrier.player->getId() : 0;
    duel.homeTeam = defender->isHomeTeam;
    duel.chance.total = engageRate;
    duel.probability = engageChance;
    duel.roll = engageRoll;
    float product = D::ENGAGE_RATE_PER_SECOND;
    duel.chance.add("base rate", TermSource::SITUATION, product);
    const auto factor = [&](const char* name, TermSource source, float by)
    {
      duel.chance.add(name, source, product * (by - 1.0f));
      product *= by;
    };
    factor("pressing instruction", TermSource::INSTRUCTION,
           1.0f + getEffectiveSliders(defender->isHomeTeam).pressing *
                      D::PRESSING_ENGAGE_BONUS);
    factor("final third", TermSource::LOCATION,
           carrierDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START
               ? D::FINAL_THIRD_ENGAGE_FACTOR
               : 1.0f);
    factor("ball away from his feet", TermSource::SITUATION,
           D::CLOSE_CONTROL_ENGAGE_SHARE +
               D::EXPOSED_ENGAGE_SHARE * dribbleExposureShare());
    factor("beyond tackling reach", TermSource::SITUATION,
           ballDistance > reach ? 0.5f : 1.0f);
    factor("role, team talk and opposition orders", TermSource::SLOT_ROLE,
           engageBoostAardvark(*defender, carrier));
    if (MatchRecorder* recorder = getRecorder())
      recorder->onDuelDetail(*this, duel);
  }
  if (engageRoll < engageChance)
  {
    challenge.defender = defender;
    // Out of standing reach he goes to ground.
    challenge.sliding = ballDistance > reach;
  }
  return challenge;
}

MatchEngine::BallAction MatchEngine::chooseBallActionAardvark(
    MatchPlayer& carrier)
{
  // Decision layer: score pass/shot/carry/shield on comparable utility scales,
  // perturb close choices by decision quality, record the explanation, and
  // choose exactly one winner. Utilities are preferences, not probabilities.
  // The body executes the choice; it and subsequent contacts decide the
  // outcome.
  const StrategySliders strategy = getEffectiveSliders(carrier.isHomeTeam);
  const float pressure =
      std::clamp((MatchTuning::Decision::PRESSURE_RADIUS_METRES -
                  nearestOpponentDistance(carrier)) /
                     MatchTuning::Decision::PRESSURE_RADIUS_METRES,
                 0.0f, 1.0f);
  const float shotXG = estimateShotXG(carrier);
  const float opennessAhead = openSpaceAhead(carrier);

  // Debugger detail: the parts of every score are noted beside the
  // calculations below. Only a recorder that asks for them gets them, and
  // nothing in the decision reads them.
  std::unique_ptr<MatchDecisionDetail> detail;
  if (wantsDetail())
  {
    detail = std::make_unique<MatchDecisionDetail>();
    detail->player = carrier.player ? carrier.player->getId() : 0;
    detail->homeTeam = carrier.isHomeTeam;
  }
  ScoreBreakdown* const passTerms = detail ? &detail->options[0] : nullptr;
  ScoreBreakdown* const shotTerms = detail ? &detail->options[1] : nullptr;
  ScoreBreakdown* const carryTerms = detail ? &detail->options[2] : nullptr;
  ScoreBreakdown* const shieldTerms = detail ? &detail->options[3] : nullptr;
  const auto note = [](ScoreBreakdown* terms, const char* name,
                       TermSource source, float value)
  {
    if (terms) terms->add(name, source, value);
  };

  // Evaluate the passing candidates first: the task is pure (no random draws)
  // and the deterministic best plus runner-up options are visible in the
  // decision snapshot even when a shot or dribble is eventually chosen.
  const std::optional<PassOption> option =
      choosePassTargetAardvark(carrier, detail ? &detail->candidates : nullptr);

  // Every candidate is scored in a shared utility currency.
  // Vision (scanning and reading the play) dominates decision quality, with
  // passing technique contributing to how well options are judged.
  const float decisionQuality =
      carrier.vision * MatchTuning::Decision::VISION_DECISION_WEIGHT +
      carrier.passing * (1.0f - MatchTuning::Decision::VISION_DECISION_WEIGHT);
  const float visionNoiseScale =
      (1.0f - decisionQuality) * MatchTuning::Decision::VISION_NOISE_SCALE *
      (1.0f + (1.0f - familiarityOf(carrier)) *
                  MatchTuning::Decision::FAMILIARITY_NOISE_GAIN) *
      std::max(0.5f, 1.0f - teamEdge(carrier.isHomeTeam)) *
      (1.0f - talkSwing(carrier.isHomeTeam) * TacticsTuning::TALK_COMPOSURE);
  if (detail)
  {
    // The scale is a product: each factor is noted as what it adds.
    const float base =
        (1.0f - decisionQuality) * MatchTuning::Decision::VISION_NOISE_SCALE;
    const float familiarity =
        1.0f + (1.0f - familiarityOf(carrier)) *
                   MatchTuning::Decision::FAMILIARITY_NOISE_GAIN;
    const float edge = std::max(0.5f, 1.0f - teamEdge(carrier.isHomeTeam));
    const float talk =
        1.0f - talkSwing(carrier.isHomeTeam) * TacticsTuning::TALK_COMPOSURE;
    ScoreBreakdown& scale = detail->noiseScale;
    scale.total = visionNoiseScale;
    scale.add("decision quality (Vision, Passing)", TermSource::ATTRIBUTE,
              base);
    scale.add("tactical familiarity", TermSource::MORALE,
              base * (familiarity - 1.0f));
    scale.add("team edge (home, numbers, talk)", TermSource::SITUATION,
              base * familiarity * (edge - 1.0f));
    scale.add("team talk composure", TermSource::MORALE,
              base * familiarity * edge * (talk - 1.0f));
  }

  float passScore = -std::numeric_limits<float>::infinity();
  if (option)
  {
    passScore = option->utility;
    // The pass score is the best candidate's utility (its terms).
    if (passTerms)
      for (const PassCandidateDetail& candidate : detail->candidates)
        if (option->receiver && option->receiver->player &&
            candidate.receiver == option->receiver->player->getId())
          passTerms->terms = candidate.utility.terms;
    const float carrierDepth =
        carrier.isHomeTeam ? pitch.fractionX(carrier.position)
                           : 1.0f - pitch.fractionX(carrier.position);
    const bool pinnedToByline =
        carrierDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START &&
        std::abs(pitch.atY(MatchTuning::Pitch::CENTRE) - carrier.position.y) >=
            MatchTuning::Decision::WIDE_SHOT_WIDTH_METRES;
    if (pinnedToByline && option->targetPoint.x <= carrier.position.x)
    {
      passScore += MatchTuning::Decision::WIDE_RECYCLE_BONUS;
      note(passTerms, "recycle from the byline", TermSource::LOCATION,
           MatchTuning::Decision::WIDE_RECYCLE_BONUS);
    }
  }

  float shotScore = -std::numeric_limits<float>::infinity();
  if (shotXG >= MatchTuning::Decision::MIN_SHOT_XG)
  {
    // The "have-a-go" inclination only matters from a credible shooting
    // range: it fades out completely for absurd-distance attempts, so the
    // scored decision can never invent a nonsensical long shot.
    const float rangeEligibility =
        std::clamp((shotXG - MatchTuning::Decision::SHOT_ELIGIBILITY_FLOOR) /
                       MatchTuning::Decision::SHOT_ELIGIBILITY_RANGE,
                   0.0f, 1.0f);
    const float carrierDepth =
        carrier.isHomeTeam ? pitch.fractionX(carrier.position)
                           : 1.0f - pitch.fractionX(carrier.position);
    const float finalThirdBonus =
        carrierDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START
            ? MatchTuning::Decision::FINAL_THIRD_SHOT_BONUS
            : 0.0f;
    shotScore = (shotXG - MatchTuning::Decision::BASE_SHOT_THRESHOLD) *
                    MatchTuning::Decision::SHOT_SCORE_SCALE +
                MatchTuning::Decision::SHOT_BASE_INCLINATION *
                    (0.6f + opennessAhead) * rangeEligibility +
                finalThirdBonus +
                carrier.shooting * MatchTuning::Decision::SHOT_SKILL_BONUS -
                pressure * MatchTuning::Decision::SHOT_PRESSURE_PENALTY +
                shoutShotBiasAardvark(carrier.isHomeTeam) * rangeEligibility;
    note(shotTerms, "expected goals above the threshold", TermSource::SITUATION,
         (shotXG - MatchTuning::Decision::BASE_SHOT_THRESHOLD) *
             MatchTuning::Decision::SHOT_SCORE_SCALE);
    note(shotTerms, "have-a-go inclination (space ahead, range)",
         TermSource::SITUATION,
         MatchTuning::Decision::SHOT_BASE_INCLINATION * (0.6f + opennessAhead) *
             rangeEligibility);
    note(shotTerms, "final-third bonus", TermSource::LOCATION, finalThirdBonus);
    note(shotTerms, "Shooting", TermSource::ATTRIBUTE,
         carrier.shooting * MatchTuning::Decision::SHOT_SKILL_BONUS);
    note(shotTerms, "pressure", TermSource::SITUATION,
         -pressure * MatchTuning::Decision::SHOT_PRESSURE_PENALTY);
    note(shotTerms, "touchline shout", TermSource::MORALE,
         shoutShotBiasAardvark(carrier.isHomeTeam) * rangeEligibility);
    if (carrierDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START &&
        std::abs(pitch.atY(MatchTuning::Pitch::CENTRE) - carrier.position.y) >=
            MatchTuning::Decision::WIDE_SHOT_WIDTH_METRES)
    {
      note(shotTerms, "wide-angle discount", TermSource::LOCATION,
           shotScore * (MatchTuning::Decision::WIDE_SHOT_DISCOUNT - 1.0f));
      shotScore *= MatchTuning::Decision::WIDE_SHOT_DISCOUNT;
    }
    // A wide forward who has cut inside is looking for his shot.
    if (cutsInsideAardvark(carrier))
    {
      shotScore +=
          MatchTuning::Decision::CUT_INSIDE_SHOT_BONUS * rangeEligibility;
      note(shotTerms, "cutting inside", TermSource::SLOT_ROLE,
           MatchTuning::Decision::CUT_INSIDE_SHOT_BONUS * rangeEligibility);
    }
    // Roles shoot more or less readily; a man forced onto his weaker foot
    // under pressure is less inclined to try.
    shotScore +=
        roleProfileOf(carrier).shotBias * TacticsTuning::SHOT_BIAS_WEIGHT *
            rangeEligibility -
        weakFootPressure(carrier) * TacticsTuning::WEAK_FOOT_SHOT_PENALTY;
    note(shotTerms, "role's shot bias", TermSource::SLOT_ROLE,
         roleProfileOf(carrier).shotBias * TacticsTuning::SHOT_BIAS_WEIGHT *
             rangeEligibility);
    note(shotTerms, "forced onto weaker foot", TermSource::INSTRUCTION,
         -weakFootPressure(carrier) * TacticsTuning::WEAK_FOOT_SHOT_PENALTY);
  }

  // The longer a player has had the ball, the keener he is to release it:
  // individual possessions last about a second, rarely more than three.
  const auto heldSeconds =
      static_cast<float>(getSimulatedSeconds() - getPossessionHistory().possessionStartSeconds);
  float carryScore =
      opennessAhead * MatchTuning::Decision::CARRY_OPENNESS_WEIGHT +
      carrier.dribbling * MatchTuning::Decision::CARRY_DRIBBLING_BONUS -
      pressure * MatchTuning::Decision::CARRY_PRESSURE_PENALTY +
      strategy.riskTaking * MatchTuning::Decision::CARRY_RISK_BIAS -
      heldSeconds * MatchTuning::Decision::CARRY_HOLD_PENALTY_PER_SECOND;
  note(carryTerms, "space ahead", TermSource::SITUATION,
       opennessAhead * MatchTuning::Decision::CARRY_OPENNESS_WEIGHT);
  note(carryTerms, "Dribbling", TermSource::ATTRIBUTE,
       carrier.dribbling * MatchTuning::Decision::CARRY_DRIBBLING_BONUS);
  note(carryTerms, "pressure", TermSource::SITUATION,
       -pressure * MatchTuning::Decision::CARRY_PRESSURE_PENALTY);
  note(carryTerms, "risk-taking instruction", TermSource::INSTRUCTION,
       strategy.riskTaking * MatchTuning::Decision::CARRY_RISK_BIAS);
  note(carryTerms, "time already on the ball", TermSource::SITUATION,
       -heldSeconds * MatchTuning::Decision::CARRY_HOLD_PENALTY_PER_SECOND);

  // A wide forward cutting inside wants to run at the box himself.
  if (cutsInsideAardvark(carrier))
  {
    carryScore += MatchTuning::Decision::CUT_INSIDE_CARRY_BONUS;
    note(carryTerms, "cutting inside", TermSource::SLOT_ROLE,
         MatchTuning::Decision::CUT_INSIDE_CARRY_BONUS);
  }

  float shieldScore = -std::numeric_limits<float>::infinity();
  const bool passUnavailableOrWeak =
      !option || passScore <= MatchTuning::Passing::MIN_ACCEPTABLE_OPTION_SCORE;
  if (pressure >= MatchTuning::Decision::SHIELD_PRESSURE_THRESHOLD &&
      passUnavailableOrWeak)
  {
    shieldScore = pressure * MatchTuning::Decision::SHIELD_BONUS +
                  carrier.dribbling * MatchTuning::Decision::SHIELD_DRIBBLING;
    note(shieldTerms, "pressure", TermSource::SITUATION,
         pressure * MatchTuning::Decision::SHIELD_BONUS);
    note(shieldTerms, "Dribbling", TermSource::ATTRIBUTE,
         carrier.dribbling * MatchTuning::Decision::SHIELD_DRIBBLING);
  }

  // A side protecting a late lead no longer forces speculative long-range
  // attempts; it prefers to retain the ball. Credible chances are unaffected.
  const int carrierScore = carrier.isHomeTeam ? homeScore : awayScore;
  const int opponentScore = carrier.isHomeTeam ? awayScore : homeScore;
  if (matchTimeMinutes >= MatchTuning::Decision::LATE_GAME_MINUTE &&
      carrierScore > opponentScore &&
      shotScore > -std::numeric_limits<float>::infinity() &&
      shotXG < MatchTuning::Decision::BASE_SHOT_THRESHOLD)
  {
    shotScore -= MatchTuning::Decision::LATE_LEAD_SPECULATIVE_PENALTY;
    note(shotTerms, "protecting a late lead", TermSource::GAME_STATE,
         -MatchTuning::Decision::LATE_LEAD_SPECULATIVE_PENALTY);
  }
  // Game state: a side two or more goals up manages the game, keeping the
  // ball rather than forcing half-chances.
  const int lead = carrierScore - opponentScore;
  if (lead >= MatchTuning::Decision::COMFORTABLE_LEAD)
  {
    const float margin =
        static_cast<float>(lead - MatchTuning::Decision::COMFORTABLE_LEAD + 1);
    if (shotScore > -std::numeric_limits<float>::infinity() &&
        shotXG < MatchTuning::Decision::CLEAR_CHANCE_XG)
    {
      shotScore -=
          MatchTuning::Decision::COMFORTABLE_LEAD_SHOT_PENALTY * margin;
      note(shotTerms, "managing a comfortable lead", TermSource::GAME_STATE,
           -MatchTuning::Decision::COMFORTABLE_LEAD_SHOT_PENALTY * margin);
    }
    carryScore -=
        MatchTuning::Decision::COMFORTABLE_LEAD_CARRY_PENALTY * margin;
    note(carryTerms, "managing a comfortable lead", TermSource::GAME_STATE,
         -MatchTuning::Decision::COMFORTABLE_LEAD_CARRY_PENALTY * margin);
  }

  // Kept for the recorder: which option the scores alone would choose.
  const std::array<float, 4> scoresBeforeNoise{passScore, shotScore, carryScore,
                                               shieldScore};

  // Vision scales how much randomness perturbs close choices. The noise is
  // bounded so a truly nonsensical option can never win.
  shotScore += visionNoiseScale * randomFloat(-1.0f, 1.0f);
  carryScore += visionNoiseScale * randomFloat(-1.0f, 1.0f);
  if (shieldScore > -std::numeric_limits<float>::infinity())
  {
    shieldScore += visionNoiseScale * randomFloat(-1.0f, 1.0f);
  }
  if (option)
  {
    passScore += visionNoiseScale * randomFloat(-1.0f, 1.0f);
  }

  lastScenarioDecision.passUtility = passScore;
  lastScenarioDecision.shotUtility = shotScore;
  lastScenarioDecision.carryUtility = carryScore;
  lastScenarioDecision.shieldUtility = shieldScore;

  if (MatchRecorder* recorder = getRecorder())
  {
    // The same comparisons as the branches below, in the same order.
    const float fromOwnGoal =
        pitch.fromOwnGoalLine(carrier.position, carrier.isHomeTeam);
    const auto winner =
        [fromOwnGoal](float pass, float shot, float carry, float shield)
    {
      if (shot >= pass && shot >= carry && shot >= shield)
        return ScenarioAction::SHOT;
      if (carry >= pass && carry >= shield) return ScenarioAction::CARRY;
      if (shield >= pass)
        return fromOwnGoal < MatchTuning::Defending::CLEARANCE_WITHIN_METRES
                   ? ScenarioAction::CLEAR
                   : ScenarioAction::SHIELD;
      return ScenarioAction::PASS;
    };
    MatchDecisionRecord decision;
    decision.player = carrier.player ? carrier.player->getId() : 0;
    decision.homeTeam = carrier.isHomeTeam;
    decision.pass = passScore;
    decision.shot = shotScore;
    decision.carry = carryScore;
    decision.shield = shieldScore;
    decision.chosen = winner(passScore, shotScore, carryScore, shieldScore);
    decision.chosenWithoutNoise =
        winner(scoresBeforeNoise[0], scoresBeforeNoise[1], scoresBeforeNoise[2],
               scoresBeforeNoise[3]);
    decision.pressure = pressure;
    decision.shotXG = shotXG;
    decision.opennessAhead = opennessAhead;
    decision.noiseScale = visionNoiseScale;
    if (const auto& best = lastScenarioDecision.best)
    {
      decision.bestReceiver = best->receiverId;
      decision.bestPassUtility = best->utility;
      decision.bestPassIntent = best->intent;
    }
    if (const auto& runnerUp = lastScenarioDecision.runnerUp)
    {
      decision.runnerUpReceiver = runnerUp->receiverId;
      decision.runnerUpPassUtility = runnerUp->utility;
    }
    recorder->onDecision(*this, decision);
    if (detail)
    {
      detail->chosen = decision.chosen;
      const std::array<float, 4> withNoise{passScore, shotScore, carryScore,
                                           shieldScore};
      for (std::size_t index = 0; index < withNoise.size(); ++index)
      {
        detail->options[index].total = scoresBeforeNoise[index];
        detail->noise[index] = std::isfinite(scoresBeforeNoise[index])
                                   ? withNoise[index] - scoresBeforeNoise[index]
                                   : 0.0f;
        // Terms that are exactly zero say nothing here.
        std::erase_if(detail->options[index].terms,
                      [](const ScoreTerm& term) { return term.value == 0.0f; });
      }
      recorder->onDecisionDetail(*this, *detail);
    }
  }

  BallAction action;
  lastScenarioDecision.action = ScenarioAction::NONE;
  if (shotScore >= passScore && shotScore >= carryScore &&
      shotScore >= shieldScore)
  {
    lastScenarioDecision.action = ScenarioAction::SHOT;
    lastScenarioDecision.reason =
        "shot with estimated xG " + std::to_string(shotXG);
    action.kind = ScenarioAction::SHOT;
    return action;
  }
  if (carryScore >= passScore && carryScore >= shieldScore)
  {
    lastScenarioDecision.action = ScenarioAction::CARRY;
    lastScenarioDecision.reason =
        option ? "carry:\"space ahead (value " + std::to_string(opennessAhead) +
                     ") beats the best pass with intent " +
                     std::string(passIntentName(option->intent))
               : std::string(
                     "carry:space ahead and no acceptable pass "
                     "candidate in range");
    action.kind = ScenarioAction::CARRY;
    action.holdSeconds = randomFloat(MatchTuning::Decision::MIN_DRIBBLE_TIME,
                                     MatchTuning::Decision::MAX_DRIBBLE_TIME);
    return action;
  }
  const float fromOwnGoal =
      pitch.fromOwnGoalLine(carrier.position, carrier.isHomeTeam);
  if (shieldScore >= passScore &&
      fromOwnGoal < MatchTuning::Defending::CLEARANCE_WITHIN_METRES)
  {
    lastScenarioDecision.action = ScenarioAction::CLEAR;
    lastScenarioDecision.reason =
        "clear:pressed in the own third with no safe outlet";
    action.kind = ScenarioAction::CLEAR;
    return action;
  }
  if (shieldScore >= passScore)
  {
    lastScenarioDecision.action = ScenarioAction::SHIELD;
    lastScenarioDecision.reason =
        "shield:retain and protect the ball under pressure with no safe "
        "outlet";
    action.kind = ScenarioAction::SHIELD;
    action.holdSeconds = randomFloat(MatchTuning::Decision::MIN_DRIBBLE_TIME,
                                     MatchTuning::Decision::MAX_DRIBBLE_TIME);
    return action;
  }

  lastScenarioDecision.action = ScenarioAction::PASS;
  lastScenarioDecision.reason =
      std::string("pass:") + std::string(passIntentName(option->intent)) +
      " to player " +
      std::to_string(option->receiver && option->receiver->player
                         ? option->receiver->player->getId()
                         : 0) +
      " (utility " + std::to_string(option->utility) + ")";
  action.kind = ScenarioAction::PASS;
  action.pass = option;
  return action;
}

MatchEngine::PassOption MatchEngine::evaluatePassOptionAardvark(
    MatchPlayer& passer, MatchPlayer& receiver,
    PassCandidateDetail* detail) const
{
  // Estimate one receiver's safety, progress and space without consuming RNG
  // or changing the match. The completion estimate ranks options; passBall
  // does not roll against it to guarantee a completed pass. Error at release,
  // ball flight and contested receptions determine whether it arrives.
  const StrategySliders strategy = getEffectiveSliders(passer.isHomeTeam);
  const float direction = passer.isHomeTeam ? 1.0f : -1.0f;
  const float passDistance = distance(passer.position, receiver.position);
  const float progression =
      pitch.fractionX(receiver.position.x - passer.position.x) * direction;
  const float openness =
      std::clamp(nearestOpponentDistance(receiver) /
                     MatchTuning::Passing::OPENNESS_RADIUS_METRES,
                 0.0f, 1.0f);
  const float laneRisk = passingLaneRisk(passer, receiver);
  const float pressure =
      std::clamp((MatchTuning::Decision::PRESSURE_RADIUS_METRES -
                  nearestOpponentDistance(passer)) /
                     MatchTuning::Decision::PRESSURE_RADIUS_METRES,
                 0.0f, 1.0f);
  const PlayerRole role =
      receiver.player ? receiver.player->getRole() : PlayerRole::CM;
  const bool forwardRole = role == PlayerRole::ST || role == PlayerRole::LW ||
                           role == PlayerRole::RW || role == PlayerRole::CAM;
  const float passerToGoal =
      pitch.toGoalLine(passer.position, passer.isHomeTeam);
  const float receiverDepth =
      receiver.isHomeTeam ? pitch.fractionX(receiver.position)
                          : 1.0f - pitch.fractionX(receiver.position);
  const float receiverToGoal =
      pitch.toGoalLine(receiver.position, receiver.isHomeTeam);
  const bool widePasser =
      pitch.fractionY(passer.position) <= MatchTuning::Passing::WIDE_ATTACK_MINIMUM_Y ||
      pitch.fractionY(passer.position) >= MatchTuning::Passing::WIDE_ATTACK_MAXIMUM_Y;
  const bool centralReceiver =
      pitch.fractionY(receiver.position) >= MatchTuning::Passing::CENTRAL_TARGET_MINIMUM_Y &&
      pitch.fractionY(receiver.position) <= MatchTuning::Passing::CENTRAL_TARGET_MAXIMUM_Y;
  const bool cutbackOption =
      widePasser && centralReceiver &&
      passerToGoal <= MatchTuning::Passing::CUTBACK_PASSER_WITHIN_METRES &&
      receiverToGoal <= MatchTuning::Passing::CROSS_RECEIVER_WITHIN_METRES &&
      progression >= MatchTuning::Passing::CUTBACK_MINIMUM_PROGRESSION &&
      progression <= MatchTuning::Passing::CUTBACK_MAXIMUM_PROGRESSION;
  const bool crossOption =
      !cutbackOption && widePasser && centralReceiver &&
      passerToGoal <= MatchTuning::Passing::CROSS_PASSER_WITHIN_METRES &&
      receiverToGoal <= MatchTuning::Passing::CROSS_RECEIVER_WITHIN_METRES;
  const float safeOutlet = pressure * std::max(0.0f, -progression) *
                           MatchTuning::Passing::SAFE_OUTLET_WEIGHT;
  // A high defensive line leaves space in behind: a forward already running
  // into it is worth finding, and the ball is easier to play there.
  float spaceBehindShare = 0.0f;
  // Any player running in behind is worth finding, a midfielder arriving
  // late as much as a striker.
  if (receiver.isMakingRun && progression > 0.0f)
  {
    const float line = offsideLine(passer.isHomeTeam);
    const float spaceBehind =
        passer.isHomeTeam ? pitch.atX(1.0f) - line : line;
    spaceBehindShare = std::clamp(
        (spaceBehind - MatchTuning::Passing::SPACE_BEHIND_MIN_METRES) /
            MatchTuning::Passing::SPACE_BEHIND_RANGE_METRES,
        0.0f, 1.0f);
  }
  const float completionProbability = std::clamp(
      MatchTuning::Passing::BASE_COMPLETION_PROBABILITY +
          passer.passing * MatchTuning::Passing::PASSING_COMPLETION_BONUS +
          openness * MatchTuning::Passing::OPENNESS_COMPLETION_BONUS -
          laneRisk * MatchTuning::Passing::LANE_COMPLETION_PENALTY -
          // Completion falls steeply for long passes (~86% at 10-20 m,
          // ~41% beyond 40 m).
          passDistance * passDistance /
              (MatchTuning::Passing::LONG_PASS_REFERENCE_METRES *
               MatchTuning::Passing::LONG_PASS_REFERENCE_METRES) *
              MatchTuning::Passing::DISTANCE_COMPLETION_PENALTY -
          pressure * MatchTuning::Passing::PRESSURE_COMPLETION_PENALTY -
          (crossOption ? MatchTuning::Passing::CROSS_COMPLETION_PENALTY
                       : 0.0f) +
          (cutbackOption ? MatchTuning::Passing::CUTBACK_COMPLETION_BONUS
                         : 0.0f) +
          spaceBehindShare *
              MatchTuning::Passing::SPACE_BEHIND_COMPLETION_BONUS,
      MatchTuning::Passing::MIN_COMPLETION_PROBABILITY,
      MatchTuning::Passing::MAX_COMPLETION_PROBABILITY);
  // Risk-taking trades safety for progress: a patient side values keeping
  // the ball, a daring one the metres gained.
  const float daring = strategy.riskTaking - 0.5f;
  float utility =
      openness * MatchTuning::Passing::OPENNESS_WEIGHT -
      laneRisk * MatchTuning::Passing::LANE_RISK_WEIGHT +
      progression *
          (MatchTuning::Passing::BASE_PROGRESS_WEIGHT +
           strategy.offensiveBias *
               MatchTuning::Passing::OFFENSIVE_PROGRESS_WEIGHT) *
          (1.0f + daring * MatchTuning::Passing::RISK_PROGRESS_GAIN) -
      std::abs(passDistance - MatchTuning::Passing::IDEAL_DISTANCE_METRES) *
          MatchTuning::Passing::DISTANCE_PENALTY_PER_METRE +
      safeOutlet +
      (forwardRole && progression > 0.0f
           ? MatchTuning::Passing::FORWARD_ROLE_BONUS
           : 0.0f) +
      completionProbability * MatchTuning::Passing::COMPLETION_UTILITY_WEIGHT *
          (1.0f - daring * MatchTuning::Passing::RISK_SAFETY_GAIN);
  if (receiver.isMakingRun && progression > 0.0f)
    utility += MatchTuning::Passing::ACTIVE_RUNNER_UTILITY_BONUS;
  utility += passingRoleBiasAardvark(passer, receiver, progression,
                                     completionProbability);
  // In the final third a team-mate in a shooting position is worth finding.
  if (receiverDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START)
    utility +=
        estimateShotXG(receiver) * MatchTuning::Passing::SHOT_CREATION_WEIGHT;
  utility += spaceBehindShare * MatchTuning::Passing::SPACE_BEHIND_UTILITY;
  if (crossOption) utility += MatchTuning::Passing::CROSS_UTILITY_BONUS;
  if (cutbackOption) utility += MatchTuning::Passing::CUTBACK_UTILITY_BONUS;

  PassIntent intent = PassIntent::RECYCLE;
  if (cutbackOption)
  {
    intent = PassIntent::CUTBACK;
  }
  else if (crossOption)
  {
    intent = PassIntent::CROSS;
  }
  else if (pressure >= MatchTuning::Passing::PRESSURE_RELEASE_THRESHOLD &&
           progression <=
               MatchTuning::Passing::PRESSURE_RELEASE_MAX_PROGRESSION)
  {
    intent = PassIntent::PRESSURE_RELEASE;
  }
  else if (pitch.fractionY(std::abs(receiver.position.y - passer.position.y)) >=
           MatchTuning::Passing::SWITCH_PLAY_MINIMUM_WIDTH)
  {
    intent = PassIntent::SWITCH_PLAY;
  }
  else if (receiver.isMakingRun &&
           progression >=
               MatchTuning::Passing::THROUGH_BALL_MINIMUM_PROGRESSION)
  {
    intent = PassIntent::THROUGH_BALL;
  }
  else if (progression >= MatchTuning::Passing::PROGRESSIVE_PASS_MINIMUM)
  {
    intent = PassIntent::PROGRESSIVE;
  }

  const float flightEstimate =
      passDistance / MatchTuning::Passing::ESTIMATED_BALL_SPEED;
  const Vector2F lead =
      {receiver.velocity.x * flightEstimate *
                   MatchTuning::Passing::RECEIVER_LEAD_SCALE,
               receiver.velocity.y * flightEstimate *
                   MatchTuning::Passing::RECEIVER_LEAD_SCALE};
  PitchPoint target{std::clamp(receiver.position.x + lead.x, 0.0f, pitch.atX(1.0f)),
                    std::clamp(receiver.position.y + lead.y, 0.0f, pitch.atY(1.0f))};
  if (intent == PassIntent::THROUGH_BALL)
  {
    target.x += (receiver.movementTarget.x - target.x) *
                MatchTuning::Passing::THROUGH_BALL_TARGET_BLEND;
    target.y += (receiver.movementTarget.y - target.y) *
                MatchTuning::Passing::THROUGH_BALL_TARGET_BLEND;
    target.x += direction * MatchTuning::Passing::THROUGH_BALL_FORWARD_LEAD_METRES;
    target.x = clampToPitchX(target.x);
    target.y = clampToPitchY(target.y);
  }
  else if (intent == PassIntent::CROSS)
  {
    target.x += (receiver.movementTarget.x - target.x) *
                MatchTuning::Passing::CROSS_TARGET_BLEND;
    target.y += (receiver.movementTarget.y - target.y) *
                MatchTuning::Passing::CROSS_TARGET_BLEND;
  }
  const float travelDistance = distance(passer.position, target);

  if (detail)
  {
    // Debugger detail: the same terms as the sums above, by source.
    using P = MatchTuning::Passing;
    namespace T = TacticsTuning;
    using S = TermSource;
    detail->receiver = receiver.player ? receiver.player->getId() : 0;
    detail->intent = intent;
    detail->distanceMetres = passDistance;
    detail->lofted =
        !receiver.isGoalkeeper && (intent == PassIntent::CROSS ||
                                   travelDistance > P::LOFTED_DISTANCE_METRES);

    ScoreBreakdown& completion = detail->completion;
    completion.total = completionProbability;
    completion.add("base", S::SITUATION, P::BASE_COMPLETION_PROBABILITY);
    completion.add("Passing", S::ATTRIBUTE,
                   passer.passing * P::PASSING_COMPLETION_BONUS);
    completion.add("receiver in space", S::SITUATION,
                   openness * P::OPENNESS_COMPLETION_BONUS);
    completion.add("passing-lane risk", S::SITUATION,
                   -laneRisk * P::LANE_COMPLETION_PENALTY);
    completion.add(
        "distance", S::SITUATION,
        -passDistance * passDistance /
            (P::LONG_PASS_REFERENCE_METRES * P::LONG_PASS_REFERENCE_METRES) *
            P::DISTANCE_COMPLETION_PENALTY);
    completion.add("pressure on the passer", S::SITUATION,
                   -pressure * P::PRESSURE_COMPLETION_PENALTY);
    completion.add("cross", S::LOCATION,
                   crossOption ? -P::CROSS_COMPLETION_PENALTY : 0.0f);
    completion.add("cutback", S::LOCATION,
                   cutbackOption ? P::CUTBACK_COMPLETION_BONUS : 0.0f);
    completion.add("space in behind", S::SITUATION,
                   spaceBehindShare * P::SPACE_BEHIND_COMPLETION_BONUS);
    // The estimate is clamped to its range.
    completion.add("clamped to its range", S::SITUATION, completion.residual());

    ScoreBreakdown& value = detail->utility;
    value.total = utility;
    const float progressWeight =
        P::BASE_PROGRESS_WEIGHT +
        strategy.offensiveBias * P::OFFENSIVE_PROGRESS_WEIGHT;
    value.add("receiver in space", S::SITUATION, openness * P::OPENNESS_WEIGHT);
    value.add("passing-lane risk", S::SITUATION,
              -laneRisk * P::LANE_RISK_WEIGHT);
    value.add("forward progress", S::SITUATION,
              progression * P::BASE_PROGRESS_WEIGHT);
    value.add(
        "progress x attacking instruction", S::INSTRUCTION,
        progression * strategy.offensiveBias * P::OFFENSIVE_PROGRESS_WEIGHT);
    value.add("progress x risk-taking", S::INSTRUCTION,
              progression * progressWeight * daring * P::RISK_PROGRESS_GAIN);
    value.add("distance from the ideal length", S::SITUATION,
              -std::abs(passDistance - P::IDEAL_DISTANCE_METRES) *
                  P::DISTANCE_PENALTY_PER_METRE);
    value.add("safe outlet under pressure", S::SITUATION, safeOutlet);
    value.add("forward receiver (his position)", S::NATURAL_POSITION,
              forwardRole && progression > 0.0f ? P::FORWARD_ROLE_BONUS : 0.0f);
    value.add("completion chance", S::SITUATION,
              completionProbability * P::COMPLETION_UTILITY_WEIGHT);
    value.add("completion x risk-taking", S::INSTRUCTION,
              -completionProbability * P::COMPLETION_UTILITY_WEIGHT * daring *
                  P::RISK_SAFETY_GAIN);
    value.add("receiver making a run", S::SITUATION,
              receiver.isMakingRun && progression > 0.0f
                  ? P::ACTIVE_RUNNER_UTILITY_BONUS
                  : 0.0f);
    // passingRoleBiasAardvark(): the passer's daring, the receiver's target
    // role and an opposition tight-marking order.
    value.add(
        "passer's role: daring passes", S::SLOT_ROLE,
        roleProfileOf(passer).passDaring *
            (progression * T::PASS_DARING_PROGRESS_WEIGHT +
             (1.0f - completionProbability) * T::PASS_DARING_SAFETY_WEIGHT));
    value.add("receiver's role: target", S::SLOT_ROLE,
              roleProfileOf(receiver).targetBias * T::TARGET_BIAS_WEIGHT *
                  (progression > 0.0f ? 1.0f : 0.4f));
    value.add("receiver tightly marked (order)", S::INSTRUCTION,
              -tightMarkPenaltyAardvark(receiver));
    value.add("receiver's shot chance", S::LOCATION,
              receiverDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START
                  ? estimateShotXG(receiver) * P::SHOT_CREATION_WEIGHT
                  : 0.0f);
    value.add("space in behind", S::SITUATION,
              spaceBehindShare * P::SPACE_BEHIND_UTILITY);
    value.add("cross", S::LOCATION,
              crossOption ? P::CROSS_UTILITY_BONUS : 0.0f);
    value.add("cutback", S::LOCATION,
              cutbackOption ? P::CUTBACK_UTILITY_BONUS : 0.0f);
    // Terms that are exactly zero say nothing here.
    std::erase_if(value.terms,
                  [](const ScoreTerm& term) { return term.value == 0.0f; });
    std::erase_if(completion.terms,
                  [](const ScoreTerm& term) { return term.value == 0.0f; });
  }

  return {&receiver,
          target,
          intent,
          utility,
          progression,
          laneRisk,
          completionProbability,
          travelDistance,
          !receiver.isGoalkeeper &&
              (intent == PassIntent::CROSS ||
               travelDistance > MatchTuning::Passing::LOFTED_DISTANCE_METRES)};
}

std::optional<MatchEngine::PassOption> MatchEngine::choosePassTargetAardvark(
    MatchPlayer& passer, std::vector<PassCandidateDetail>* details)
{
  // Debugger detail: a team-mate left out, and why.
  const auto excluded = [details](const MatchPlayer& candidate, float metres,
                                  PassExclusion reason)
  {
    if (!details) return;
    PassCandidateDetail detail;
    detail.receiver = candidate.player ? candidate.player->getId() : 0;
    detail.excluded = reason;
    detail.distanceMetres = metres;
    details->push_back(std::move(detail));
  };
  std::optional<PassOption> best;
  float bestScore = -std::numeric_limits<float>::infinity();
  std::optional<PassOption> runnerUp;
  float runnerUpScore = -std::numeric_limits<float>::infinity();

  auto toDecision = [](const PassOption& option)
  {
    return PassDecision{0,
                        option.receiver && option.receiver->player
                            ? option.receiver->player->getId()
                            : 0,
                        option.targetPoint,
                        option.intent,
                        option.utility,
                        option.progression,
                        option.laneRisk,
                        option.completionProbability};
  };

  const float defenderLine = offsideLine(passer.isHomeTeam);
  for (auto& candidate : players)
  {
    if (&candidate == &passer || candidate.isHomeTeam != passer.isHomeTeam ||
        !active(candidate))
    {
      continue;
    }

    const float passDistance = distance(passer.position, candidate.position);
    if (passDistance < MatchTuning::Passing::MIN_DISTANCE_METRES ||
        passDistance > MatchTuning::Passing::MAX_DISTANCE_METRES)
    {
      excluded(candidate, passDistance,
               passDistance < MatchTuning::Passing::MIN_DISTANCE_METRES
                   ? PassExclusion::TOO_CLOSE
                   : PassExclusion::TOO_FAR);
      continue;
    }
    // Nobody plays a long ball back towards his own goal.
    if (candidate.isGoalkeeper &&
        passDistance > MatchTuning::Passing::MAX_BACK_PASS_METRES)
    {
      excluded(candidate, passDistance, PassExclusion::KEEPER_TOO_FAR);
      continue;
    }
    // The passer judges the offside line from what he sees: poorer vision
    // misreads tight lines, which is where real offsides come from.
    const float perceivedLineError =
        hashNoise(passer.player->getId(), candidate.player->getId()) *
        (1.0f - passer.vision) * MatchTuning::Passing::OFFSIDE_PERCEPTION_ERROR_METRES;
    if (isOffside(candidate, passer.isHomeTeam, defenderLine,
                  perceivedLineError))
    {
      excluded(candidate, passDistance, PassExclusion::LOOKS_OFFSIDE);
      continue;
    }

    PassCandidateDetail* detail = nullptr;
    if (details)
    {
      details->emplace_back();
      detail = &details->back();
    }
    PassOption option = evaluatePassOptionAardvark(passer, candidate, detail);
    if (option.utility > bestScore)
    {
      runnerUp = std::move(best);
      runnerUpScore = bestScore;
      bestScore = option.utility;
      best = std::move(option);
    }
    else if (option.utility > runnerUpScore)
    {
      runnerUpScore = option.utility;
      runnerUp = std::move(option);
    }
  }

  lastScenarioDecision.best.reset();
  lastScenarioDecision.runnerUp.reset();
  if (best && best->receiver && best->receiver->player)
  {
    lastScenarioDecision.best = toDecision(*best);
  }
  if (runnerUp && runnerUp->receiver && runnerUp->receiver->player)
  {
    lastScenarioDecision.runnerUp = toDecision(*runnerUp);
  }

  if (bestScore < MatchTuning::Passing::MIN_ACCEPTABLE_OPTION_SCORE)
    return std::nullopt;
  return best;
}

void MatchEngine::afterPassReleasedAardvark(const MatchPlayer& passer,
                                            const MatchPlayer& receiver)
{
  // Keep externally visible AI state consistent with the newly released
  // pass. Without this, the final fixed step of a rendered frame could expose
  // the pre-pass run assignments until the next simulation update.
  for (auto& teammate : players)
  {
    if (teammate.isHomeTeam != passer.isHomeTeam || !active(teammate)) continue;
    teammate.isMakingRun = false;
    if (&teammate == &receiver)
      teammate.intent = PlayerIntent::RECEIVE_PASS;
    else if (!teammate.isGoalkeeper)
      teammate.intent = PlayerIntent::OFFER_SUPPORT;
  }
}

MatchEngine::ShotAim MatchEngine::chooseShotAimAardvark(
    const MatchPlayer& shooter, bool header, bool penalty, bool freeKick)
{
  // Aim for the side away from the keeper, a little inside the post; better
  // finishers pick tighter spots.
  constexpr float HALF_GOAL = MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES;
  const MatchPlayer* keeper = findGoalkeeper(!shooter.isHomeTeam);
  const float keeperOffset =
      keeper ? keeper->position.y -
                              pitch.atY(MatchTuning::Pitch::CENTRE)
             : 0.0f;
  float side = keeperOffset > 0.0f ? -1.0f : 1.0f;
  if (std::abs(keeperOffset) < MatchTuning::Shooting::KEEPER_CENTRED_METRES ||
      randomFloat(0.0f, 1.0f) < MatchTuning::Shooting::NEAR_SIDE_AIM_CHANCE)
  {
    side = randomFloat(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f;
  }
  // From the spot a taker can pick a corner; in open play the spot is
  // chosen on the move and lands further inside the post.
  // A free kick is curled over the wall into a corner.
  ShotAim aim;
  aim.inset =
      MatchTuning::Shooting::AIM_POST_INSET_BASE +
      (1.0f - shooter.shooting) * MatchTuning::Shooting::AIM_POST_INSET_SKILL +
      randomFloat(0.0f,
                  penalty    ? MatchTuning::Shooting::PENALTY_AIM_INSET_SPREAD
                  : freeKick ? MatchTuning::SetPiece::FREE_KICK_AIM_INSET_SPREAD
                             : MatchTuning::Shooting::AIM_INSET_SPREAD);
  aim.aimY = side * std::max(0.0f, HALF_GOAL - aim.inset);
  if (!penalty && !freeKick && !header &&
      randomFloat(0.0f, 1.0f) <
          MatchTuning::Shooting::POWER_SHOT_BASE_CHANCE -
              shooter.shooting *
                  MatchTuning::Shooting::POWER_SHOT_SKILL_REDUCTION)
  {
    // Struck hard at the frame rather than placed: often near the keeper.
    aim.aimY = keeperOffset +
               randomFloat(-MatchTuning::Shooting::POWER_SHOT_WIDTH_METRES,
                           MatchTuning::Shooting::POWER_SHOT_WIDTH_METRES);
  }
  aim.aimZ =
      freeKick
          ? randomFloat(MatchTuning::SetPiece::FREE_KICK_AIM_MIN_HEIGHT_METRES,
                        MatchTuning::Shooting::AIM_MAX_HEIGHT_METRES)
          : randomFloat(
                MatchTuning::Shooting::AIM_MIN_HEIGHT_METRES,
                header ? MatchTuning::Shooting::HEADER_AIM_MAX_HEIGHT_METRES
                       : MatchTuning::Shooting::AIM_MAX_HEIGHT_METRES);
  return aim;
}

float MatchEngine::choosePenaltyGuessAardvark(const MatchPlayer& keeper)
{
  // He picks a side, and a good keeper reads the taker a little more often.
  const float shotSide =
      getPossessionState().shotTargetY < pitch.atY(MatchTuning::Pitch::CENTRE) ? -1.0f
                                                                             : 1.0f;
  float guess = randomFloat(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f;
  if (randomFloat(0.0f, 1.0f) <
      MatchTuning::Goalkeeper::PENALTY_READ_BASE +
          keeper.goalkeeping * MatchTuning::Goalkeeper::PENALTY_READ_SKILL)
  {
    guess = shotSide;
  }
  if (randomFloat(0.0f, 1.0f) < MatchTuning::Goalkeeper::PENALTY_STAY_CHANCE)
    guess = 0.0f;
  return guess;
}

PitchPoint MatchEngine::goalkeeperTargetAardvark(MatchPlayer& keeper,
                                               const MatchPlayer* carrier)
{
  GoalkeeperControl& control = keepers[keeper.isHomeTeam ? 0 : 1];
  const auto setState = [&control](GoalkeeperState next)
  {
    if (control.state != next)
    {
      control.state = next;
      control.timer = 0.0f;
    }
  };
  control.timer += stepSeconds();
  const float goalX = keeper.isHomeTeam ? 0.0f : 1.0f;
  const float outward = keeper.isHomeTeam ? 1.0f : -1.0f;
  const PitchPoint goalCentre = pitch.at(goalX, MatchTuning::Pitch::CENTRE);

  if (getPossessionAttachment() == keeper.player)
  {
    setState(GoalkeeperState::HOLD);
    return keeper.position;
  }
  if (keeper.diveTimer > 0.0f)
  {
    setState(GoalkeeperState::RECOVER);
    return keeper.position;
  }
  if (control.state == GoalkeeperState::DISTRIBUTE &&
      control.timer < MatchTuning::Goalkeeper::DISTRIBUTE_TIME_SECONDS)
  {
    return keeper.position;
  }

  // Claim a high delivery dropping into the own box.
  if (!getPossessionAttachment() && getPossessionState().isAerialDelivery &&
      getPossessionState().passByHome != keeper.isHomeTeam)
  {
    // Anticipate high flight, then close on the ball itself once it drops
    // within handling height. Continuing to lead a low cross keeps the
    // keeper several metres ahead of it and prevents a physical catch.
    const float height = ball.z;
    const float lookahead =
        height > MatchRules::goalkeeperReachMetres(keeper.heightMetres,
                                                   keeper.goalkeeping)
            ? MatchTuning::Goalkeeper::CLAIM_LOOKAHEAD_SECONDS
            : 0.0F;
    const Vector2F drift =
        {ball.velocity.x * lookahead, ball.velocity.y * lookahead};
    const PitchPoint landing{ball.position.x + drift.x,
                           ball.position.y + drift.y};
    if (inPenaltyArea(landing, keeper.isHomeTeam) &&
        distance(landing, keeper.position) <
            MatchTuning::Goalkeeper::CLAIM_BOX_DEPTH_METRES)
    {
      setState(GoalkeeperState::CLAIM);
      return landing;
    }
  }

  // Sweep a loose ball or through ball he can reach before any attacker; a
  // sweeper keeper comes further and sooner, a line keeper hardly at all.
  const float keeperSweep = roleProfileOf(keeper).keeperSweep;
  if (!carrier && !getPossessionState().isShot &&
      pitch.fromOwnGoalLine(ball.position, keeper.isHomeTeam) <
          MatchTuning::Pitch::GOALKEEPER_SWEEP_METRES *
              (1.0f + keeperSweep * TacticsTuning::KEEPER_SWEEP_DEPTH_GAIN) &&
      !(getPossessionState().isPass && getPossessionState().passByHome == keeper.isHomeTeam))
  {
    const float keeperDistance = distance(keeper.position, ball.position);
    bool keeperFirst = true;
    for (const auto& other : players)
    {
      if (!active(other) || other.isHomeTeam == keeper.isHomeTeam) continue;
      if (distance(other.position, ball.position) <
          keeperDistance * MatchTuning::Goalkeeper::SWEEP_ADVANTAGE *
              (1.0f - keeperSweep * TacticsTuning::KEEPER_SWEEP_ADVANTAGE_GAIN))
      {
        keeperFirst = false;
        break;
      }
    }
    if (keeperFirst)
    {
      setState(GoalkeeperState::SWEEP);
      const float lookahead =
          std::min(keeperDistance / std::max(currentTopSpeed(keeper), EPSILON),
                   MatchTuning::Player::MAX_LOOSE_BALL_LOOKAHEAD_SECONDS);
      const Vector2F lead =
          {ball.velocity.x * lookahead, ball.velocity.y * lookahead};
      return PitchPoint{ball.position.x + lead.x, ball.position.y + lead.y};
    }
  }

  // Rush a carrier who is through on goal inside the box.
  if (carrier && carrier->isHomeTeam != keeper.isHomeTeam &&
      inPenaltyArea(carrier->position, keeper.isHomeTeam))
  {
    bool covered = false;
    for (const auto& other : players)
    {
      if (!active(other) || other.isHomeTeam != keeper.isHomeTeam ||
          other.isGoalkeeper)
      {
        continue;
      }
      if ((carrier->position.x - other.position.x) * outward > 0.0f &&
          distance(other.position, carrier->position) <
              MatchTuning::Goalkeeper::RUSH_COVER_DISTANCE_METRES)
      {
        covered = true;
        break;
      }
    }
    if (!covered)
    {
      setState(GoalkeeperState::RUSH);
      return PitchPoint{goalCentre.x + (carrier->position.x - goalCentre.x) *
                                 MatchTuning::Goalkeeper::RUSH_CLOSING_SHARE,
              goalCentre.y + (carrier->position.y - goalCentre.y) *
                                 MatchTuning::Goalkeeper::RUSH_CLOSING_SHARE};
    }
  }

  // Set position on the ball-goal line, advancing to narrow the angle as the
  // threat approaches and acting as a sweeper when play is far away.
  setState(GoalkeeperState::SET_POSITION);
  const PitchPoint threat = carrier ? carrier->position : ball.position;
  const float threatMetres = distance(threat, goalCentre);
  float depthMetres = MatchTuning::Goalkeeper::NEAR_DEPTH_METRES +
                      threatMetres * MatchTuning::Goalkeeper::DEPTH_PER_METRE;
  if (threatMetres > MatchTuning::Goalkeeper::SWEEPER_DISTANCE_METRES)
  {
    depthMetres +=
        (threatMetres - MatchTuning::Goalkeeper::SWEEPER_DISTANCE_METRES) *
        MatchTuning::Goalkeeper::SWEEPER_DEPTH_PER_METRE;
  }
  // A sweeper keeper stands higher (a line keeper deeper) the further away
  // the play is.
  const float keeperDepth = roleProfileOf(keeper).keeperDepthMetres;
  depthMetres = std::clamp(
      depthMetres + keeperDepth * std::min(1.0f, threatMetres / 40.0f), 0.5f,
      MatchTuning::Goalkeeper::MAX_DEPTH_METRES + std::max(0.0f, keeperDepth));
  const float dxMetres = threat.x - goalCentre.x;
  const float dyMetres = threat.y - goalCentre.y;
  const float norm =
      std::max(std::sqrt(dxMetres * dxMetres + dyMetres * dyMetres), EPSILON);
  return PitchPoint{
      goalCentre.x + (dxMetres / norm * depthMetres),
      std::clamp(goalCentre.y + (dyMetres / norm * depthMetres),
                 goalCentre.y - MatchTuning::Pitch::GOALKEEPER_SIDE_LIMIT_METRES,
                 goalCentre.y + MatchTuning::Pitch::GOALKEEPER_SIDE_LIMIT_METRES)};
}

bool MatchEngine::choosesToBlockAardvark(const MatchPlayer& /*player*/)
{
  // Every outfielder in the path tries; whether he gets there is the body's.
  return true;
}

bool MatchEngine::choosesToPutBlockBehindAardvark(const MatchPlayer& player)
{
  // Deep in his own half a blocker knocks the loose ball out of play.
  const float fromOwnGoal =
      pitch.fromOwnGoalLine(player.position, player.isHomeTeam);
  return fromOwnGoal < MatchTuning::SetPiece::BLOCK_BEHIND_WITHIN_METRES &&
         randomFloat(0.0f, 1.0f) < MatchTuning::SetPiece::BLOCK_BEHIND_CHANCE;
}

bool MatchEngine::choosesToClearCrossBehindAardvark(
    const MatchPlayer& /*player*/)
{
  // Meeting a delivery close to his own goal line, he often plays safe.
  return randomFloat(0.0f, 1.0f) <
         MatchTuning::SetPiece::CLEARANCE_BEHIND_CHANCE;
}

PitchPoint MatchEngine::chooseClearBehindTargetAardvark(
    const MatchPlayer& defender)
{
  // Over the own goal line, wide of the posts on his side.
  const float goalX = defender.isHomeTeam ? 0.0f : 1.0f;
  const float side =
      defender.position.y < pitch.atY(MatchTuning::Pitch::CENTRE) ? -1.0f : 1.0f;
  return pitch.at(goalX + (defender.isHomeTeam ? -0.02f : 0.02f),
                  MatchTuning::Pitch::CENTRE +
                      side * randomFloat(MatchTuning::Pitch::GOAL_BOTTOM -
                                             MatchTuning::Pitch::CENTRE + 0.06f,
                                         0.45f));
}

MatchEngine::ClearanceAim MatchEngine::chooseClearanceAimAardvark(
    const MatchPlayer& defender)
{
  // A long, high clearance upfield and towards the nearer touchline.
  const float forward = defender.isHomeTeam ? 1.0f : -1.0f;
  const float touchline =
      defender.position.y < pitch.atY(MatchTuning::Pitch::CENTRE) ? -1.0f : 1.0f;
  ClearanceAim aim;
  aim.metres =
      randomFloat(MatchTuning::Defending::CLEARANCE_MIN_DISTANCE_METRES,
                  MatchTuning::Defending::CLEARANCE_MAX_DISTANCE_METRES);
  const Vector2F direction = normalized(
      {forward, touchline * randomFloat(0.0f, 1.0f) *
                    MatchTuning::Defending::CLEARANCE_TOUCHLINE_BIAS * 2.0f});
  const Vector2F reach =
      {direction.x * aim.metres, direction.y * aim.metres};
  aim.target = PitchPoint{defender.position.x + reach.x, defender.position.y + reach.y};
  return aim;
}

MatchEngine::HeaderAttempt MatchEngine::chooseHeaderAttemptAardvark(
    const MatchPlayer& header, bool fromTeammate)
{
  const float toGoal =
                          pitch.toGoalLine(header.position, header.isHomeTeam);
  // A header at goal needs a real chance; from further out the ball is
  // nodded down for a team-mate instead.
  if (fromTeammate && toGoal <= MatchTuning::Aerial::HEADER_SHOT_WITHIN_METRES &&
      std::abs(header.position.y - pitch.atY(MatchTuning::Pitch::CENTRE)) <=
          MatchTuning::Aerial::HEADER_SHOT_WIDTH_METRES &&
      estimateShotXG(header) * MatchTuning::Shooting::HEADER_XG_FACTOR >=
          MatchTuning::Aerial::HEADER_SHOT_MIN_XG)
    return HeaderAttempt::SHOOT;
  // A team-mate's lofted ball away from goal is cushioned down and kept.
  if (fromTeammate && toGoal > MatchTuning::Aerial::HEADER_KNOCK_DOWN_WITHIN_METRES)
    return HeaderAttempt::CUSHION;
  return HeaderAttempt::PLAY;
}

MatchEngine::HeaderPlay MatchEngine::chooseHeaderPlayAardvark(
    const MatchPlayer& header, bool fromTeammate)
{
  const float attackDirection = header.isHomeTeam ? 1.0f : -1.0f;
  const float fromOwnGoal =
                             pitch.fromOwnGoalLine(header.position, header.isHomeTeam);
  HeaderPlay play;
  // Near his own goal a defender often plays safe and heads it behind.
  if (!fromTeammate &&
      fromOwnGoal < MatchTuning::SetPiece::CROSS_CLEARANCE_WITHIN_METRES &&
      randomFloat(0.0f, 1.0f) < MatchTuning::SetPiece::HEADER_BEHIND_CHANCE)
  {
    play.kind = HeaderPlay::Kind::BEHIND;
    return play;
  }
  if (!fromTeammate && fromOwnGoal < MatchTuning::SetPiece::DEFENSIVE_HEADER_WITHIN_METRES)
  {
    // Defensive header: clear the danger, upfield and away from the middle.
    const float wide =
        header.position.y < pitch.atY(MatchTuning::Pitch::CENTRE) ? -1.0f : 1.0f;
    const Vector2F direction =
        normalized({attackDirection * randomFloat(0.5f, 1.0f),
                    wide * randomFloat(0.1f, 0.8f)});
    const Vector2F reach = {direction.x, direction.y};
    play.kind = HeaderPlay::Kind::CLEAR;
    play.aim = PitchPoint{header.position.x + reach.x, header.position.y + reach.y};
    return play;
  }
  // Knock-down or flick-on toward the nearest team-mate who is not
  // behind the header (never back toward the own goal).
  float best = std::numeric_limits<float>::max();
  for (const auto& other : players)
  {
    if (!active(other) || &other == &header ||
        other.isHomeTeam != header.isHomeTeam || other.isGoalkeeper)
      continue;
    if ((other.position.x - header.position.x) * attackDirection <
        -MatchTuning::Aerial::KNOCK_DOWN_MAX_BACKWARD_METRES)
      continue;
    const float d = distance(other.position, header.position);
    if (d < best)
    {
      best = d;
      play.target = &other;
    }
  }
  play.kind = HeaderPlay::Kind::KNOCK_DOWN;
  play.aim = play.target ? play.target->position
                         : PitchPoint{header.position.x + pitch.atX(attackDirection * 0.1f),
                                      header.position.y};
  return play;
}

MatchPlayer* MatchEngine::bestSetPieceTakerAardvark(bool homeTeam,
                                                    bool shooting)
{
  MatchPlayer* best = nullptr;
  float bestScore = -std::numeric_limits<float>::infinity();
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam ||
        player.isGoalkeeper || player.isInjured)
      continue;
    const float score = shooting
                            ? player.shooting * 0.75f + player.passing * 0.15f +
                                  player.vision * 0.10f
                            : player.passing * 0.6f + player.vision * 0.4f;
    if (score > bestScore)
    {
      bestScore = score;
      best = &player;
    }
  }
  return best;
}

MatchPlayer* MatchEngine::dutyTakerAardvark(bool homeTeam, SetPieceDuty duty)
{
  const PlayerID designated =
      designations[homeTeam ? 0 : 1][static_cast<std::size_t>(duty)];
  if (designated != PlayerID{})
  {
    for (auto& player : players)
    {
      if (active(player) && player.isHomeTeam == homeTeam &&
          !player.isGoalkeeper && !player.isInjured &&
          player.player->getId() == designated)
        return &player;
    }
  }
  switch (duty)
  {
    case SetPieceDuty::Penalties:
    case SetPieceDuty::FreeKicks:
      return bestSetPieceTakerAardvark(homeTeam, true);
    case SetPieceDuty::CornersLeft:
    case SetPieceDuty::CornersRight:
      return bestSetPieceTakerAardvark(homeTeam, false);
    default:
      return nullptr;
  }
}

MatchEngine::SetPieceAttackers MatchEngine::arrangeSetPieceAttackAardvark(
    bool attackingHome, PitchPoint ballPosition)
{
  // Box targets for the attacking side, in metres from the goal line and
  // from the centre of the goal (positive towards the ball's side).
  const float goalX = attackingHome ? 1.0f : 0.0f;
  const float inward = attackingHome ? -1.0f : 1.0f;
  const float ballSide =
      ballPosition.y < pitch.atY(MatchTuning::Pitch::CENTRE) ? -1.0f : 1.0f;
  const auto spot = [&](float depthMetres, float lateralMetres)
  {
    return PitchPoint{
        pitch.atX(goalX) + (inward * depthMetres),
        pitch.atY(MatchTuning::Pitch::CENTRE) +
            (ballSide * lateralMetres)};
  };
  const std::array<PitchPoint, 5> attackSlots = {
      spot(5.5f, 3.0f), spot(6.5f, -3.5f), spot(10.5f, 0.0f), spot(8.0f, 1.0f),
      spot(17.5f, -2.0f)};

  // Rank attackers by aerial threat; the best go into the box.
  std::array<MatchPlayer*, 11> attackers{};
  std::size_t attackerCount = 0;
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != attackingHome ||
        player.isGoalkeeper || &player == restartTaker() ||
        attackerCount == attackers.size())
      continue;
    attackers[attackerCount++] = &player;
  }
  // Centre-backs and strikers attack the delivery; full-backs and a
  // holding midfielder form the rest defence.
  const auto boxThreat = [](const MatchPlayer* candidate)
  {
    float roleBias = 0.0f;
    switch (candidate->player->getRole())
    {
      case PlayerRole::ST:
        roleBias = 0.12f;
        break;
      case PlayerRole::CB:
        roleBias = 0.0f;
        break;
      case PlayerRole::LB:
      case PlayerRole::RB:
        roleBias = -0.3f;
        break;
      case PlayerRole::CDM:
        roleBias = -0.15f;
        break;
      default:
        break;
    }
    return MatchRules::headerReachMetres(candidate->heightMetres,
                                         candidate->physicality) +
           roleBias;
  };
  insertionSort(
      attackers, attackerCount,
      [&boxThreat](const MatchPlayer* first, const MatchPlayer* second)
      {
        const float a = boxThreat(first);
        const float b = boxThreat(second);
        if (a != b) return a > b;
        return first->player->getId() < second->player->getId();
      });
  const std::size_t boxAttackers =
      std::min(attackSlots.size(),
               attackerCount > MatchTuning::SetPiece::REST_DEFENDERS
                   ? attackerCount - MatchTuning::SetPiece::REST_DEFENDERS
                   : std::size_t{0});
  SetPieceAttackers inBox;
  for (std::size_t index = 0; index < boxAttackers; ++index)
  {
    attackers[index]->movementTarget = attackSlots[index];
    inBox.players[inBox.count++] = attackers[index];
  }
  return inBox;
}

void MatchEngine::arrangeSetPieceDefenceAardvark(
    bool defendingHome, PitchPoint ballPosition,
    const SetPieceAttackers& attackers)
{
  // Spots measured from the defended goal, as the attackers' are.
  const float goalX = defendingHome ? 0.0f : 1.0f;
  const float inward = defendingHome ? 1.0f : -1.0f;
  const float ballSide =
      ballPosition.y < pitch.atY(MatchTuning::Pitch::CENTRE) ? -1.0f : 1.0f;
  const auto spot = [&](float depthMetres, float lateralMetres)
  {
    return PitchPoint{
        pitch.atX(goalX) + (inward * depthMetres),
        pitch.atY(MatchTuning::Pitch::CENTRE) +
            (ballSide * lateralMetres)};
  };
  const std::size_t boxAttackers = attackers.count;

  // Defenders: the keeper on his line, markers goal-side of each attacker in
  // the box, and the rest zonal at the near post and the edge of the area.
  std::array<MatchPlayer*, 11> defenders{};
  std::size_t defenderCount = 0;
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != defendingHome ||
        defenderCount == defenders.size())
      continue;
    if (player.isGoalkeeper)
    {
      player.movementTarget = spot(0.8f, 0.0f);
      continue;
    }
    defenders[defenderCount++] = &player;
  }
  insertionSort(defenders, defenderCount,
                [](const MatchPlayer* first, const MatchPlayer* second)
                {
                  const float a = first->defending + first->physicality;
                  const float b = second->defending + second->physicality;
                  if (a != b) return a > b;
                  return first->player->getId() < second->player->getId();
                });
  std::size_t next = 0;
  for (std::size_t index = 0; index < boxAttackers && next < defenderCount;
       ++index, ++next)
  {
    const PitchPoint mark = attackers.players[index]->movementTarget;
    defenders[next]->movementTarget = PitchPoint{
        mark.x - inward * MatchTuning::SetPiece::MARKING_GOAL_SIDE_OFFSET_METRES,
        mark.y};
  }
  const std::array<PitchPoint, 3> zones = {spot(3.0f, 2.5f), spot(6.0f, 0.0f),
                                         spot(16.0f, 0.0f)};
  for (std::size_t zone = 0; zone < zones.size() && next < defenderCount;
       ++zone, ++next)
  {
    defenders[next]->movementTarget = zones[zone];
  }
}

MatchPlayer* MatchEngine::chooseRestartTakerAardvark(MatchState restart,
                                                     bool homeTeam,
                                                     PitchPoint spot,
                                                     bool topCorner)
{
  switch (restart)
  {
    case MatchState::KICK_OFF:
      if (MatchPlayer* taker = findClosestPlayer(spot, homeTeam, false))
        return taker;
      return findClosestPlayer(spot, homeTeam, true);
    case MatchState::GOAL_KICK:
      if (MatchPlayer* keeper = findGoalkeeper(homeTeam)) return keeper;
      return findClosestPlayer(spot, homeTeam, true);
    case MatchState::PENALTY:
      return dutyTakerAardvark(homeTeam, SetPieceDuty::Penalties);
    case MatchState::CORNER_KICK:
      if (MatchPlayer* taker = dutyTakerAardvark(
              homeTeam, topCorner ? SetPieceDuty::CornersLeft
                                  : SetPieceDuty::CornersRight))
        return taker;
      return findClosestPlayer(spot, homeTeam, false);
    case MatchState::FREE_KICK:
      if (restartIsSetPiece)
        if (MatchPlayer* taker =
                dutyTakerAardvark(homeTeam, SetPieceDuty::FreeKicks))
          return taker;
      return findClosestPlayer(spot, homeTeam, false);
    case MatchState::THROW_IN:
    {
      // A designated long-throw specialist takes the throws in the attacking
      // third; elsewhere (and without one) the nearest player does.
      const float attackingDepth =
          homeTeam ? pitch.fractionX(spot) : 1.0f - pitch.fractionX(spot);
      if (attackingDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START)
        if (MatchPlayer* taker =
                dutyTakerAardvark(homeTeam, SetPieceDuty::LongThrows))
          return taker;
      return findClosestPlayer(spot, homeTeam, false);
    }
    default:
      return findClosestPlayer(spot, homeTeam, false);
  }
}

MatchEngine::RestartPlay MatchEngine::chooseRestartPlayAardvark(
    MatchPlayer& taker, MatchState restartState)
{
  if (restartState == MatchState::CORNER_KICK)
    return chooseCornerPlayAardvark(taker);
  if (restartState == MatchState::FREE_KICK && restartIsSetPiece)
  {
    const PitchPoint goal =
      pitch.at(taker.isHomeTeam ? 1.0f : 0.0f, MatchTuning::Pitch::CENTRE);
    const float goalMetres = distance(ball.position, goal);
    const float lateralMetres = std::abs(ball.position.y - pitch.atY(MatchTuning::Pitch::CENTRE));
    const bool shootingRange =
        goalMetres <= MatchTuning::SetPiece::DIRECT_FREE_KICK_METRES &&
        lateralMetres <= MatchTuning::SetPiece::DIRECT_FREE_KICK_WIDTH_METRES;
    const float shootChance = std::clamp(
        MatchTuning::SetPiece::DIRECT_SHOT_BASE +
            taker.shooting * MatchTuning::SetPiece::DIRECT_SHOT_SKILL -
            goalMetres * MatchTuning::SetPiece::DIRECT_SHOT_DISTANCE_PENALTY,
        0.0f, 0.95f);
    if (shootingRange && randomFloat(0.0f, 1.0f) < shootChance)
      return {RestartPlay::Kind::DIRECT_SHOT, std::nullopt, false};
    // From wide or deep it is delivered into the box like a corner.
    const bool wide =
        lateralMetres > MatchTuning::SetPiece::CROSS_MIN_WIDTH_METRES ||
        goalMetres > MatchTuning::SetPiece::DIRECT_FREE_KICK_METRES;
    if (wide) return chooseCornerPlayAardvark(taker);
  }

  std::optional<PassOption> passOption = choosePassTargetAardvark(taker);
  if (!passOption && playHalfMinutes > 0)
  {
    // A restart must release the ball even when no lane meets the AI's
    // open-play safety threshold. Find the nearest teammate as an outlet.
    MatchPlayer* outlet = nullptr;
    float nearest = std::numeric_limits<float>::infinity();
    for (auto& candidate : players)
    {
      if (!active(candidate) || &candidate == &taker ||
          candidate.isHomeTeam != taker.isHomeTeam || candidate.isGoalkeeper)
        continue;
      const float gap = distance(taker.position, candidate.position);
      if (gap > EPSILON && gap < nearest)
      {
        outlet = &candidate;
        nearest = gap;
      }
    }
    if (outlet) passOption = evaluatePassOptionAardvark(taker, *outlet);
  }
  if (passOption)
    return {RestartPlay::Kind::PASS, passOption,
            restartState == MatchState::GOAL_KICK};
  return {RestartPlay::Kind::HOLD, std::nullopt, false};
}

MatchEngine::RestartPlay MatchEngine::chooseCornerPlayAardvark(
    MatchPlayer& taker)
{
  // Routine: short, near post, far post or penalty spot, delivered at head
  // height to the attacker assigned to that zone.
  MatchPlayer* target = nullptr;
  const float roll = randomFloat(0.0f, 1.0f);
  if (roll < MatchTuning::SetPiece::SHORT_CORNER_CHANCE)
  {
    std::optional<PassOption> shortOption = choosePassTargetAardvark(taker);
    if (shortOption &&
        shortOption->passDistance <
            MatchTuning::SetPiece::SHORT_CORNER_MAX_DISTANCE_METRES)
    {
      PassOption groundPass = *shortOption;
      groundPass.lofted = false;
      return {RestartPlay::Kind::PASS, groundPass, false};
    }
  }
  const PitchPoint goal =
      pitch.at(taker.isHomeTeam ? 1.0f : 0.0f, MatchTuning::Pitch::CENTRE);
  float bestScore = -std::numeric_limits<float>::infinity();
  const float nearPostShare = MatchTuning::SetPiece::NEAR_POST_CHANCE;
  const bool nearPost =
      roll < MatchTuning::SetPiece::SHORT_CORNER_CHANCE + nearPostShare;
  for (auto& candidate : players)
  {
    if (!active(candidate) || &candidate == &taker ||
        candidate.isHomeTeam != taker.isHomeTeam || candidate.isGoalkeeper)
      continue;
    if (!inPenaltyArea(candidate.movementTarget, !taker.isHomeTeam)) continue;
    const float targetMetres = distance(candidate.movementTarget, goal);
    const float centreY = pitch.atY(MatchTuning::Pitch::CENTRE);
    const float sideMetres =
        (candidate.movementTarget.y - centreY) *
                      (ball.position.y < centreY ? -1.0f : 1.0f);
    const float threat = MatchRules::headerReachMetres(candidate.heightMetres,
                                                       candidate.physicality);
    const float zone = nearPost ? sideMetres : -sideMetres;
    const float score =
        threat * MatchTuning::SetPiece::TARGET_THREAT_WEIGHT + zone * 0.15f -
        targetMetres * 0.05f +
        randomFloat(0.0f, MatchTuning::SetPiece::TARGET_RANDOMNESS);
    if (score > bestScore)
    {
      bestScore = score;
      target = &candidate;
    }
  }
  if (!target)
  {
    std::optional<PassOption> fallback = choosePassTargetAardvark(taker);
    if (fallback) return {RestartPlay::Kind::PASS, fallback, true};
    return {RestartPlay::Kind::HOLD, std::nullopt, false};
  }
  PassOption delivery = evaluatePassOptionAardvark(taker, *target);
  delivery.targetPoint = target->movementTarget;
  delivery.passDistance = distance(taker.position, delivery.targetPoint);
  delivery.lofted = true;
  delivery.intent = PassIntent::CROSS;
  return {RestartPlay::Kind::PASS, delivery, true};
}

MatchPlayer* MatchEngine::chooseTakeOnAardvark(MatchPlayer& carrier)
{
  // A defender standing in his path, close and ready to challenge, is taken
  // on rather than run through.
  MatchPlayer* blocker =
      findClosestPlayer(carrier.position, !carrier.isHomeTeam, false);
  const float forward = carrier.isHomeTeam ? 1.0f : -1.0f;
  if (blocker && blocker->tackleCooldown <= 0.0f &&
      distance(blocker->position, carrier.position) <
          MatchTuning::Dribble::TAKE_ON_RANGE_METRES &&
      (blocker->position.x - carrier.position.x) * forward > 0.0f)
    return blocker;
  return nullptr;
}

void MatchEngine::planOvertakeAardvark(std::size_t /*slot*/)
{
  // The Aardvark brain never plans a way round: he carries on (and bumps).
}

float MatchEngine::chooseGetUpAardvark(const MatchPlayer& player)
{
  // No urgency cues: up as soon as he has stopped (collision plan step 4).
  using C = MatchTuning::Collision;
  ++stats.collisionGetUpsFixed;
  return alongStatScale(player.derivedStat(DerivedStat::AGILITY),
                        C::GET_UP_SECONDS_AT_AGILITY_1, C::GET_UP_SECONDS_AT_AGILITY_99);
}
