// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Binturong brain: the player decision-making being developed (see
// match_brain.h). It started as an exact copy of the Aardvark brain
// (match_brain_aardvark.cpp). Every function here is a MatchEngine member
// with the "Binturong" suffix, declared in match_brain_binturong_members.h; the
// engine reaches them through the dispatchers in match_brain_dispatch.cpp.
//
// Change behaviour freely here. A brain-only helper this file calls has its
// own Binturong copy; a helper shared with the body (match_engine.cpp) is
// shared by every brain, so copy it into this brain (with the Binturong suffix)
// before changing it. To keep this version for comparison, freeze it and copy
// it to a new version the same way this one was made.

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "model/match_body_shape_binturong.h"
#include "model/match_contact_binturong.h"
#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/match_recorder.h"
#include "model/player.h"
#include "model/role_utils.h"

using namespace MatchEngineInternal;

namespace
{
/** The planner's branches. A player's job is the branch his target came
 * from plus the choices made in it (a slot, a side, a man): moving to
 * another branch, or changing a choice, is a new job and a new target, not
 * the old one moving (movement targets plan, step 2). */
enum class PlanBranch : std::uint8_t
{
  SHAPE,
  CARRY,
  RUN,
  BOX_ARRIVAL,
  OVERLAP,
  SUPPORT,
  FORWARD_SHORT,
  PRESS,
  DOUBLE_UP,
  BLOCK_LANE,
  COVER,
  TIGHT_MARK,
  TRANSITION_RECOVER,
  ZONAL_MARK,
  RECEIVE,
  FLIGHT_SUPPORT,
  LOOSE_BALL,
  LOOSE_COVER,
  TRANSITION_RUN,
  TRANSITION_SUPPORT,
  RECOVER,
  AERIAL_HOLD,
  AERIAL_CHALLENGE,
  GOALKEEPER,
};

/** A job: its branch, small choices (slots, sides, switches: under 2^24)
 * and a detail (a man's slot, an epoch). */
constexpr std::uint64_t planJob(PlanBranch branch, std::uint32_t choices = 0,
                                std::uint32_t detail = 0)
{
  return static_cast<std::uint64_t>(branch) |
         static_cast<std::uint64_t>(choices & 0xFFFFFFU) << 8 |
         static_cast<std::uint64_t>(detail) << 32;
}

// Overtaking in a race to a shared target (movement targets plan 4a).
// Walkers adjust only when their predicted closest approach is under ~1 m
// (Olivier et al. 2012), and look ahead about 3 s (time to collision:
// Karamouzas, Skinner & Guy 2014). Design choices: a team-mate is passed
// close, with a little margin (0.6 m between centres: the 0.4 m bodies of
// the collision plan and 0.2 m between them; collision plan step 7);
// "the same target" and "the same way"; a side kept unless the other is
// this much shorter; running speeds below this are not racing. The
// clearances are in MatchTuning::Collision (the sandbox's true view draws
// them).
constexpr float OVERTAKE_OPPONENT_CLEARANCE_METRES =
    MatchTuning::Collision::OVERTAKE_OPPONENT_CLEARANCE_METRES;
constexpr float OVERTAKE_TEAM_MATE_CLEARANCE_METRES =
    MatchTuning::Collision::OVERTAKE_TEAM_MATE_CLEARANCE_METRES;
constexpr float OVERTAKE_HORIZON_SECONDS = 3.0f;
constexpr float OVERTAKE_SAME_TARGET_METRES = 2.0f;
constexpr float OVERTAKE_SAME_DIRECTION_COSINE = 0.7071f;
constexpr float OVERTAKE_STICKY_METRES = 0.5f;
constexpr float OVERTAKE_MINIMUM_SPEED = 1.0f;
// A way round (way-round plan): a loose-ball chaser steers round anyone not
// chasing it whose predicted closest approach is under the overtake's
// clearances, looking this far ahead (or to the ball, if sooner), never in
// the last metres before he meets it; his waypoint a margin outside the
// clearance (his curve cuts a few centimetres inside); a side kept unless
// the other is this much shorter (decisions 1-7, agreed 2026-10-30).
constexpr float WAY_ROUND_HORIZON_SECONDS = 3.0f;
constexpr float WAY_ROUND_FINAL_METRES = 2.0f;
constexpr float WAY_ROUND_MARGIN_METRES = 0.1f;
constexpr float WAY_ROUND_STICKY_METRES = 0.5f;
// Giving way (decisions 1-2, agreed 2026-10-30): a man moving at least this
// fast (m/s) is running a line (slower: standing, gone round as before);
// paths within this cosine of square to each other cross (between 45 and
// 135 degrees); below the moving speed, giving way is a stop.
constexpr float GIVE_WAY_MOVING_SPEED = 0.5f;
constexpr float GIVE_WAY_CROSSING_COSINE = 0.7071f;
// Giving way the same way with no room to go round: this much under the
// speed the man ahead is going his way (m/s).
constexpr float GIVE_WAY_UNDER_LEADER_SPEED = 0.5f;
// A loose ball's path is projected this far ahead (5 s, as for a first
// touch); the player already after it keeps it on a near tie by one tick.
constexpr int LOOSE_BALL_PATH_TICKS = 50;
constexpr float LOOSE_BALL_CONTINUITY_SECONDS =
    MatchTuning::Timing::FIXED_STEP_SECONDS;

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

const MatchEngine::MovementStyle& MatchEngine::movementStyleBinturong()
{
  using P = MatchTuning::Player;
  static const MovementStyle style = []
  {
    MovementStyle built;
    // No target lag: players head for the latest plan at once.
    built.smoothsTargets = false;
    for (std::size_t intent = 0; intent < PLAYER_INTENT_COUNT; ++intent)
      built.intentSpeedShares[intent] =
          movementSpeedScale(static_cast<PlayerIntent>(intent));
    built.restartWalkShare = P::RESTART_WALK_SPEED_SCALE;
    built.urgencyDistanceMetres = P::URGENCY_DISTANCE_METRES;
    built.maxUrgencyShare = P::MAX_URGENCY_SPEED_SCALE;
    // Movement modes (fatigue plan 4.2, step 6b): each job a mode, run at
    // the top of its band. The shares above stay for an Aardvark body. How
    // hard to run against a deadline, and the old pacing (a tired player
    // easing off, saving the burst reserve) and the touchline's lift, are
    // left to the brain work to come.
    built.usesModes = true;
    built.fatigueWorkRateLoss = 0.0f;
    const auto mode = [&built](PlayerIntent intent, MovementMode chosen)
    { built.intentModes[static_cast<std::size_t>(intent)] = chosen; };
    using M = MovementMode;
    using I = PlayerIntent;
    for (const I intent : {I::HOLD_SHAPE, I::OFFER_SUPPORT,
                           I::BLOCK_PASSING_LANE, I::GOALKEEP})
      mode(intent, M::JOG);
    for (const I intent : {I::CARRY_BALL, I::MARK_OPPONENT, I::COVER_PRESS,
                           I::RECOVER_SHAPE, I::RECEIVE_PASS})
      mode(intent, M::RUN);
    for (const I intent : {I::PRESS_BALL, I::CLAIM_LOOSE_BALL,
                           I::RUN_IN_BEHIND, I::ATTACK_BOX, I::OVERLAP})
      mode(intent, M::SPRINT);
    // A jog job walks within 5 m of its target, jogs from 10 m, runs from
    // 25 m (a ramp, so a moving target never flips him between modes).
    built.jogRampMetres = {5.0f, 10.0f, 20.0f, 25.0f};
    return built;
  }();
  return style;
}

void MatchEngine::applyTeamMentalityBinturong(StrategySliders& sliders,
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

float MatchEngine::shoutShotBiasBinturong(bool homeTeam) const
{
  const MatchShout shout = shouts[homeTeam ? 0 : 1].shout;
  const float bias = shout == MatchShout::SHOOT_ON_SIGHT       ? 1.0f
                     : shout == MatchShout::WORK_BALL_INTO_BOX ? -1.0f
                                                               : 0.0f;
  return bias * MatchTuning::Touchline::SHOUT_SHOT_BIAS *
         getShoutStrength(homeTeam);
}

void MatchEngine::refreshTacticalTargetsBinturong(float dt, bool planHome)
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
  const Vector2F pressurePosition = carrier ? carrier->position : ball.position;
  // The team shape reads the play where it is heading: while a pass
  // travels, where it is going, so the block shifts once instead of
  // sliding with the ball. Pressing and loose balls still go to the ball.
  const Vector2F shapeAnchor =
      !carrier && getPossessionState().isPass ? getPossessionState().passTarget : pressurePosition;
  const StrategySliders homeSliders = getEffectiveSliders(true);
  const StrategySliders awaySliders = getEffectiveSliders(false);

  // A loose ball's path, from the ball's own physics (flight, bounces,
  // rolling to a stop): where it will be each tick, and whether it is low
  // enough there to be played. Who gets to it first is who can meet it
  // first on this path (interception: walkers and catchers head for a
  // meeting point, not for where it is now; Fajen & Warren).
  struct PathPoint
  {
    float seconds = 0.0f;
    Vector2F at{};
    float height = 0.0f;
  };
  std::array<PathPoint, LOOSE_BALL_PATH_TICKS + 1> loosePath{};
  std::size_t loosePoints = 0;
  if (!carrier)
  {
    const float speed = length(ball.velocity);
    const Vector2F heading = speed > EPSILON
                                 ? Vector2F{ball.velocity.x / speed,
                                            ball.velocity.y / speed}
                                 : Vector2F{0.0f, 0.0f};
    FlightState flight{speed, ball.velocityZ,
                       ball.z * MatchTuning::Units::BALL_Z_METRES, false,
                       ball.spin};
    Vector2F at = toMetres(ball.position);
    constexpr int SUBSTEPS = MatchTuning::Timing::BALL_SUBSTEPS;
    constexpr float TICK = MatchTuning::Timing::FIXED_STEP_SECONDS;
    loosePath[loosePoints++] = {0.0f, at, flight.height};
    for (int tick = 1; tick <= LOOSE_BALL_PATH_TICKS; ++tick)
    {
      for (int sub = 0; sub < SUBSTEPS; ++sub)
      {
        const float moved = stepFlight(flight, TICK / SUBSTEPS);
        at = {at.x + heading.x * moved, at.y + heading.y * moved};
      }
      loosePath[loosePoints++] = {static_cast<float>(tick) * TICK, at,
                                  flight.height};
      // At rest: the rest of the path is this spot.
      if (flight.horizontalSpeed <= 0.0f && flight.height <= 0.0f &&
          flight.verticalSpeed <= 0.0f)
        break;
    }
  }
  // When and where a candidate can first meet the loose ball on its path
  // (within his reach of it), at his pursuit speed toward each point; at
  // its resting spot if he cannot catch it up.
  const auto interception = [&](const MatchPlayer& candidate)
  {
    const Vector2F from = toMetres(candidate.position);
    // Below his head line it is for his feet, thigh or chest.
    const float headLine = bodyLines(candidate).head;
    float seconds = 0.0f;
    Vector2F meet = loosePath[0].at;
    for (std::size_t index = 0; index < loosePoints; ++index)
    {
      const PathPoint& point = loosePath[index];
      const Vector2F toPoint{point.at.x - from.x, point.at.y - from.y};
      const float metres = length(toPoint);
      const float toward =
          metres > EPSILON ? std::max(0.0f, (candidate.velocity.x * toPoint.x +
                                             candidate.velocity.y * toPoint.y) /
                                                metres)
                           : 0.0f;
      const float pursuit = std::max(
          currentTopSpeed(candidate) +
              toward * MatchTuning::Player::CURRENT_VELOCITY_PURSUIT_WEIGHT,
          MatchTuning::Player::MINIMUM_PURSUIT_SPEED);
      const float reach =
          std::max(0.0f, metres - MatchTuning::Ball::OUTFIELD_CONTROL_RADIUS_METRES) /
          pursuit;
      seconds = std::max(point.seconds, reach);
      meet = point.at;
      if (point.height <= headLine && reach <= point.seconds) break;
    }
    return std::pair{seconds, toPitch(meet)};
  };

  const auto estimatedArrivalTime = [&](const MatchPlayer& candidate)
  {
    if (!carrier && loosePoints > 0)
    {
      // A loose ball: when he can meet it, the one already after it a tick
      // ahead (so a near tie does not swap who goes, step to step).
      float arrival = interception(candidate).first;
      if (candidate.intent == PlayerIntent::CLAIM_LOOSE_BALL)
        arrival = std::max(0.0f, arrival - LOOSE_BALL_CONTINUITY_SECONDS);
      return arrival;
    }
    const Vector2F toTarget =
        toMetres({pressurePosition.x - candidate.position.x,
                  pressurePosition.y - candidate.position.y});
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
                           pressEagernessBinturong(candidate, carrier));
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

  // Going for a loose high ball (aerial challenge plan step 2): goal kicks,
  // clearances, long balls and the head tennis after them, and crosses
  // (everyone else in the box keeps his man or his slot: AERIAL_HOLD). From
  // each side one player
  // goes to where the ball will be at a height he can head it. He holds
  // the spot and makes his standing jump if he gets there in time to set
  // himself; otherwise he arrives with the ball and makes his running jump,
  // higher, so meeting it sooner. The side that meets it first decides the
  // spot; the other side's player who can get there soonest goes there
  // too: he contests it if he arrives in time, else he presses the winner.
  struct AerialRole
  {
    MatchPlayer* player = nullptr;
    Vector2F meet{};
    bool contests = true;
    bool running = false;
  };
  std::array<AerialRole, 2> aerialRoles{};
  challengePath.clear();
  keeperClaimMeet = {};
  const float lowestHead = lowestHeadLine();
  if (!carrier && !getPossessionState().isShot &&
      (ball.z * MatchTuning::Units::BALL_Z_METRES > lowestHead ||
       ball.velocityZ > 0.0f))
  {
    // Its own flight (drag, spin and curve), run ahead and put back, until
    // it drops below everyone's head line.
    using A = MatchTuning::Aerial;
    const float headLine = lowestHead;
    constexpr float TICK = MatchTuning::Timing::FIXED_STEP_SECONDS /
                           static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
    const BallMotion saved = ballMotion();
    for (float seconds = TICK; seconds <= A::CHALLENGE_PATH_SECONDS; seconds += TICK)
    {
      integrateBall(TICK);
      const float height = ball.z * MatchTuning::Units::BALL_Z_METRES;
      if (height < headLine && ball.velocityZ <= 0.0f) break;
      challengePath.push_back({seconds, toMetres(ball.position), height});
    }
    setBallMotion(saved);
  }
  if (!challengePath.empty())
  {
    using A = MatchTuning::Aerial;
    // How long he takes to get within his head's comfort of a point
    // (metres), at his pursuit speed, as for a loose ball.
    const auto runSeconds = [&](const MatchPlayer& candidate, Vector2F point)
    {
      const Vector2F from = toMetres(candidate.position);
      const Vector2F toPoint{point.x - from.x, point.y - from.y};
      const float metres = length(toPoint);
      const float toward =
          metres > EPSILON ? std::max(0.0f, (candidate.velocity.x * toPoint.x +
                                             candidate.velocity.y * toPoint.y) /
                                                metres)
                           : 0.0f;
      const float pursuit = std::max(
          currentTopSpeed(candidate) +
              toward * MatchTuning::Player::CURRENT_VELOCITY_PURSUIT_WEIGHT,
          MatchTuning::Player::MINIMUM_PURSUIT_SPEED);
      return std::pair{std::max(0.0f, metres - A::HEAD_COMFORT_GAP_METRES) / pursuit,
                       metres};
    };
    // His earliest meet on the path: the first point he can head it at
    // that he can get to, holding the spot (standing jump, set in time) or
    // arriving with it (running jump, with a run-up).
    struct Meet
    {
      float seconds = 0.0f;
      Vector2F at{};
      bool running = false;
    };
    const auto earliestMeet = [&](const MatchPlayer& candidate) -> std::optional<Meet>
    {
      const float reach = derivedPhysicalValue(
          DerivedStat::JUMPING_REACH, candidate.derivedStat(DerivedStat::JUMPING_REACH));
      const float rise = std::max(reach - candidate.heightMetres, 0.0f);
      const float forehead = candidate.heightMetres - A::HEAD_OFFSET_METRES;
      const float standingTop =
          forehead + rise * A::STANDING_JUMP_SHARE - A::JUMP_FOREHEAD_ABOVE_METRES;
      const float runningTop = forehead + rise - A::JUMP_FOREHEAD_ABOVE_METRES;
      const float lowest = std::max(bodyLines(candidate).head,
                                    forehead - A::DUCK_HEIGHT_SHARE * candidate.heightMetres);
      const bool moving = length(candidate.velocity) >= A::RUNNING_JUMP_MIN_SPEED;
      for (const JumpPathPoint& point : challengePath)
      {
        if (point.height > runningTop || point.height < lowest) continue;
        const auto [seconds, metres] = runSeconds(candidate, point.at);
        if (point.height <= standingTop &&
            seconds + A::CHALLENGE_SETTLE_SECONDS <= point.seconds)
          return Meet{point.seconds, point.at, false};
        if (seconds <= point.seconds && (moving || metres >= A::CHALLENGE_RUN_UP_METRES))
          return Meet{point.seconds, point.at, true};
      }
      return std::nullopt;
    };
    // Each side's earliest; a pass's receiver goes for his own side if he
    // can get to it.
    std::array<std::optional<Meet>, 2> best{};
    std::array<MatchPlayer*, 2> bestPlayer{};
    for (std::size_t side = 0; side < 2; ++side)
    {
      const bool home = side == 0;
      MatchPlayer* receiver = getPossessionState().isPass && getPossessionState().passByHome == home
                                  ? findMatchPlayer(getExpectedReceiver())
                                  : nullptr;
      if (receiver && active(*receiver) && !receiver->isGoalkeeper)
        if (const std::optional<Meet> meet = earliestMeet(*receiver))
        {
          best[side] = meet;
          bestPlayer[side] = receiver;
          continue;
        }
      for (MatchPlayer* const candidate : onPitch(home))
      {
        if (candidate->isGoalkeeper) continue;
        const std::optional<Meet> meet = earliestMeet(*candidate);
        if (meet && (!best[side] || meet->seconds < best[side]->seconds))
        {
          best[side] = meet;
          bestPlayer[side] = candidate;
        }
      }
      // His keeper, for a ball coming down in his area (goalkeeper plan step
      // 7): where it comes through his hands' reach, held (standing jump) or
      // attacked (running jump), as for a header. Not his own side's pass.
      MatchPlayer* keeper = findGoalkeeper(home);
      if (keeper && getBody(home) == BodyId::BINTURONG && !keeperInDive(*keeper) &&
          !keeper->fallen && !(getPossessionState().isPass && getPossessionState().passByHome == home))
      {
        const float height = keeper->heightMetres;
        const float shoulder = MatchTuning::Goalkeeper::SHOULDER_HEIGHT_SHARE * height;
        const float standingTop = shoulder + keeperArmBinturong(*keeper);
        const float aerialReach = derivedPhysicalValue(
            DerivedStat::AERIAL_REACH, keeper->derivedStat(DerivedStat::AERIAL_REACH));
        const float rise = jumpRiseOf(
            aerialReach, MatchTuning::KeeperHands::STANDING_REACH_HEIGHT_SHARE * height, true);
        const bool moving = length(keeper->velocity) >= A::RUNNING_JUMP_MIN_SPEED;
        std::optional<Meet> meet;
        for (const JumpPathPoint& point : challengePath)
        {
          if (point.height < shoulder || point.height > standingTop + rise ||
              !inPenaltyArea(toPitch(point.at), home))
            continue;
          const auto [seconds, metres] = runSeconds(*keeper, point.at);
          if (point.height <= standingTop + rise * A::STANDING_JUMP_SHARE &&
              seconds + A::CHALLENGE_SETTLE_SECONDS <= point.seconds)
          {
            meet = Meet{point.seconds, point.at, false};
            break;
          }
          if (seconds <= point.seconds && (moving || metres >= A::CHALLENGE_RUN_UP_METRES))
          {
            meet = Meet{point.seconds, point.at, true};
            break;
          }
        }
        if (meet && (!best[side] || meet->seconds < best[side]->seconds))
        {
          best[side] = meet;
          bestPlayer[side] = keeper;
        }
      }
    }
    // The side that meets it first sets the spot.
    const std::size_t first =
        !best[1] || (best[0] && best[0]->seconds <= best[1]->seconds) ? 0 : 1;
    if (best[first])
    {
      const Meet& meet = *best[first];
      aerialRoles[first] = {bestPlayer[first], toPitch(meet.at), true, meet.running};
      // His keeper meets it first: he comes for it (goalkeeperTarget).
      if (bestPlayer[first]->isGoalkeeper) keeperClaimMeet[first] = toPitch(meet.at);
      // The other side's player who can get there soonest.
      const std::size_t other = 1 - first;
      MatchPlayer* challenger = nullptr;
      float soonest = std::numeric_limits<float>::max();
      for (MatchPlayer* const candidate : onPitch(other == 0))
      {
        if (candidate->isGoalkeeper) continue;
        const float seconds = runSeconds(*candidate, meet.at).first;
        if (seconds < soonest)
        {
          soonest = seconds;
          challenger = candidate;
        }
      }
      if (challenger)
        aerialRoles[other] = {
            challenger, toPitch(meet.at),
            soonest <= meet.seconds + A::CHALLENGE_TOLERANCE_SECONDS,
            soonest + A::CHALLENGE_SETTLE_SECONDS > meet.seconds &&
                soonest <= meet.seconds + A::CHALLENGE_TOLERANCE_SECONDS};
    }
  }

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
    const float depth = candidate.isHomeTeam ? candidate.position.x
                                             : 1.0f - candidate.position.x;
    const float separation =
        carrier ? std::abs(candidate.position.y - carrier->position.y) : 0.0f;
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
        homeTeam ? attackingPosition : 1.0f - attackingPosition;
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

  const auto runChannel = [](const MatchPlayer& runner)
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
    if (runner.basePosition.y < MatchTuning::Pitch::CENTRE)
      return MatchTuning::Shape::LEFT_STRIKER_ATTACK_CHANNEL;
    if (runner.basePosition.y > MatchTuning::Pitch::CENTRE)
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
    const float ballSide = carrier->position.y - MatchTuning::Pitch::CENTRE;
    if (arrival == Arrival::FAR_POST &&
        std::abs(ballSide) < MatchTuning::Shape::FAR_POST_MIN_BALL_WIDTH)
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
          (candidate.basePosition.y - MatchTuning::Pitch::CENTRE) * ballSide <
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

      float score = std::abs(candidate.basePosition.y - carrier->position.y) -
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
            toMetres({pressurePosition.x - candidate.position.x,
                      pressurePosition.y - candidate.position.y});
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
        const float depth = candidate.isHomeTeam ? candidate.position.x
                                                 : 1.0f - candidate.position.x;
        const float separation =
            carrier ? std::abs(candidate.position.y - carrier->position.y)
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
    Vector2F target =
        ownControl ? possessionAnchorBinturong(player) : player.basePosition;
    // How fast the target itself moves, when it is pinned to a moving man
    // (otherwise worked out from his last plan, below), and his job.
    Vector2F targetVelocity{0.0f, 0.0f};
    bool pinned = false;
    // The ground loose-ball chaser, and when he meets it (way-round plan).
    bool looseChase = false;
    float looseMeetSeconds = 0.0f;
    std::uint64_t job = planJob(PlanBranch::SHAPE);
    player.intent = PlayerIntent::HOLD_SHAPE;
    player.isPressing = false;
    player.isMakingRun = false;

    // The whole block follows the ball while preserving its formation. This
    // produces recognizable defensive, middle and attacking lines rather than
    // twenty outfield players independently chasing one point.
    const float widthScale =
        MatchTuning::Shape::MIN_WIDTH_SCALE +
        sliders.widthUsage * MatchTuning::Shape::WIDTH_SLIDER_SCALE;
    target.y = MatchTuning::Pitch::CENTRE +
               (target.y - MatchTuning::Pitch::CENTRE) * widthScale;
    target.x += (shapeAnchor.x - MatchTuning::Pitch::CENTRE) *
                (MatchTuning::Shape::BASE_LONGITUDINAL_SHIFT +
                 sliders.compactness *
                     MatchTuning::Shape::COMPACTNESS_LONGITUDINAL_SHIFT);
    target.y +=
        (shapeAnchor.y - MatchTuning::Pitch::CENTRE) *
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
      { return player.isHomeTeam ? x : 1.0f - x; };
      const float ballDepth = ownGoalDepth(shapeAnchor.x);
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
      target.x = player.isHomeTeam ? depth : 1.0f - depth;
      target.y = MatchTuning::Pitch::CENTRE +
                 (player.basePosition.y - MatchTuning::Pitch::CENTRE) *
                     (S::BLOCK_WIDTH_SCALE -
                      sliders.compactness * S::BLOCK_COMPACTNESS_NARROWING) +
                 (shapeAnchor.y - MatchTuning::Pitch::CENTRE) *
                     S::BLOCK_BALL_SIDE_SHIFT;
      target.x += defensiveRoleShiftBinturong(player);
    }

    if (&player == carrier)
    {
      player.intent = PlayerIntent::CARRY_BALL;
      target = carryTargetBinturong(player);
      job = planJob(PlanBranch::CARRY);
    }
    else if (ownPossession)
    {
      player.intent = PlayerIntent::HOLD_SHAPE;
      const float attackingProgress =
          player.isHomeTeam ? carrier->position.x : 1.0f - carrier->position.x;
      target.x +=
          attackDirection *
          std::max(0.0f, attackingProgress -
                             MatchTuning::Shape::POSSESSION_PROGRESS_START) *
          MatchTuning::Shape::POSSESSION_BLOCK_PROGRESS;
      target.x +=
          attackDirection * (MatchTuning::Shape::SUPPORT_BASE_ADVANCE +
                             sliders.offensiveBias *
                                 MatchTuning::Shape::SUPPORT_OFFENSIVE_ADVANCE);
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
        target.x += attackDirection *
                    (MatchTuning::Shape::RUN_BASE_ADVANCE +
                     sliders.riskTaking * MatchTuning::Shape::RUN_RISK_ADVANCE);
        // Run timing is imperfect: runners hover around the line and now and
        // then drift beyond it, which is where offsides come from.
        const float timing =
            defenderLine * attackDirection >= ball.position.x * attackDirection
                ? (epochNoise(player.player->getId(), 0x0ff51deU,
                              RUN_TIMING_EPOCH_STEPS) *
                       0.5f +
                   0.25f) *
                      MatchTuning::Passing::RUN_TIMING_GAMBLE
                : 0.0f;
        const float onsideTarget =
            legalRunLine -
            attackDirection * (MatchTuning::Shape::RUN_ONSIDE_BUFFER - timing);
        const float runDepthTarget =
            onsideTarget - attackDirection * static_cast<float>(runnerSlot) *
                               MatchTuning::Shape::SECONDARY_RUN_DEPTH_STAGGER;
        target.x += (runDepthTarget - target.x) *
                    MatchTuning::Shape::RUN_DEPTH_TARGET_PULL;
        target.x = player.isHomeTeam ? std::min(target.x, onsideTarget)
                                     : std::max(target.x, onsideTarget);

        float channel = runChannel(player);
        bool channelMoved = false;
        if (runnerSlot > 0 && runners[0])
        {
          const float primaryChannel = runChannel(*runners[0]);
          if (std::abs(channel - primaryChannel) <
              MatchTuning::Shape::MINIMUM_RUN_CHANNEL_SEPARATION)
          {
            channelMoved = true;
            channel = primaryChannel <= MatchTuning::Pitch::CENTRE
                          ? MatchTuning::Shape::RIGHT_INSIDE_FORWARD_CHANNEL
                          : MatchTuning::Shape::LEFT_INSIDE_FORWARD_CHANNEL;
          }
        }
        target.y +=
            (channel - target.y) * MatchTuning::Shape::RUN_CHANNEL_BLEND;

        // Only a runner caught clearly beyond the line checks back; one who
        // is marginally off relies on his timing (and sometimes gets caught).
        const bool checkingBack =
            isOffside(player, player.isHomeTeam, defenderLine,
                      MatchTuning::Passing::RUN_TIMING_GAMBLE * 0.75f);
        if (checkingBack)
          target.x -= attackDirection * MatchTuning::Shape::ONSIDE_RECOVERY;
        // His timing is drawn afresh each epoch: a new run.
        job = planJob(PlanBranch::RUN,
                      static_cast<std::uint32_t>(runnerSlot) |
                          (timing != 0.0f ? 1U << 8 : 0U) |
                          (channelMoved ? 1U << 9 : 0U) |
                          (checkingBack ? 1U << 10 : 0U),
                      static_cast<std::uint32_t>(
                          stepCounter / RUN_TIMING_EPOCH_STEPS));
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
          const float goalX = player.isHomeTeam ? 1.0f : 0.0f;
          const float ballSide =
              carrier->position.y < MatchTuning::Pitch::CENTRE ? -1.0f : 1.0f;
          const float arrivalX =
              goalX - attackDirection * (farPost ? S::FAR_POST_ARRIVAL_DEPTH
                                                 : S::MIDFIELD_ARRIVAL_DEPTH);
          const float onside =
              lineFor(player.isHomeTeam) -
              attackDirection * MatchTuning::Shape::RUN_ONSIDE_BUFFER;
          target.x = player.isHomeTeam ? std::min(arrivalX, onside)
                                       : std::max(arrivalX, onside);
          target.y = MatchTuning::Pitch::CENTRE -
                     ballSide * (farPost ? S::FAR_POST_ARRIVAL_WIDTH
                                         : S::MIDFIELD_ARRIVAL_WIDTH);
          job = planJob(PlanBranch::BOX_ARRIVAL,
                        (farPost ? 1U : 0U) | (ballSide > 0.0f ? 2U : 0U));
        }
        else if (&player == overlappingFullback)
        {
          player.intent = PlayerIntent::OVERLAP;
          job = planJob(PlanBranch::OVERLAP);
          target.x += attackDirection *
                      MatchTuning::Shape::FINAL_THIRD_FULLBACK_OVERLAP;
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
          float supportDepth = MatchTuning::Shape::NEAR_SUPPORT_DEPTH;
          float supportWidth = MatchTuning::Shape::NEAR_SUPPORT_WIDTH;
          float supportSide = primarySide;
          if (supportSlot == MatchTuning::Shape::SQUARE_SUPPORT_SLOT)
          {
            supportDepth = MatchTuning::Shape::SQUARE_SUPPORT_DEPTH;
            supportWidth = MatchTuning::Shape::SQUARE_SUPPORT_WIDTH;
            supportSide = -primarySide;
          }
          else if (supportSlot == MatchTuning::Shape::TRAILING_SUPPORT_SLOT)
          {
            supportDepth = MatchTuning::Shape::TRAILING_SUPPORT_DEPTH;
            supportWidth = MatchTuning::Shape::TRAILING_SUPPORT_WIDTH;
          }

          target = carrier->position;
          target.x -= attackDirection * supportDepth;
          target.y += supportSide * supportWidth;
          const bool transitionBonus =
              teamPhase == TeamPhase::ATTACKING_TRANSITION;
          if (transitionBonus)
          {
            target.x += attackDirection *
                        MatchTuning::Shape::TRANSITION_SUPPORT_FORWARD_BONUS;
          }
          job = planJob(PlanBranch::SUPPORT,
                        static_cast<std::uint32_t>(supportSlot) |
                            (supportSide > 0.0f ? 1U << 8 : 0U) |
                            (transitionBonus ? 1U << 9 : 0U));
        }
        else if (forward)
        {
          player.intent = PlayerIntent::OFFER_SUPPORT;
          // Not every attacker runs beyond the defence. A complementary
          // forward checks toward the ball to form a passing triangle and drag
          // a marker.
          target.x =
              carrier->position.x -
              attackDirection * MatchTuning::Shape::FORWARD_SHORT_OPTION_DEPTH;
          const float lateralDirection =
              player.basePosition.y <= carrier->position.y ? -1.0f : 1.0f;
          target.y =
              carrier->position.y +
              lateralDirection *
                  MatchTuning::Shape::FORWARD_SHORT_OPTION_LATERAL_SEPARATION;
          job = planJob(PlanBranch::FORWARD_SHORT,
                        lateralDirection > 0.0f ? 1U : 0U);
        }
        if (distance(target, carrier->position) >
            MatchTuning::Shape::MAX_SUPPORT_DISTANCE_METRES)
        {
          // Pulled in from too far away: a different spot.
          job |= std::uint64_t{1} << 31;
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
            (MatchTuning::Shape::PRESSING_STANDOFF_BASE +
             (1.0f - sliders.pressing) *
                 MatchTuning::Shape::PRESSING_STANDOFF_CAUTIOUS_BONUS) *
            pressStandOffShareBinturong(player, *carrier);
        target = pressurePosition;
        target.x -= attackDirection * standOff;
        target.y += weakFootShadeBinturong(*carrier);
        targetVelocity = carrier->velocity;
        pinned = true;
        job = planJob(PlanBranch::PRESS);
      }
      else if (const auto doubling =
                   pressers[1] == &player
                       ? doubleUpPointBinturong(player, *carrier, pressers[0])
                       : std::nullopt)
      {
        // Ordered to double up: the second man closes the carrier too.
        player.intent = PlayerIntent::COVER_PRESS;
        player.isPressing = true;
        target = *doubling;
        job = planJob(PlanBranch::DOUBLE_UP);
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
          target.x -=
              attackDirection * MatchTuning::Shape::COVER_LANE_GOAL_SIDE_OFFSET;
          job = planJob(PlanBranch::BLOCK_LANE, 0,
                        static_cast<std::uint32_t>(slotOf(*coverOutlet)));
        }
        else
        {
          player.intent = PlayerIntent::COVER_PRESS;
          const float lateralSide =
              player.position.y <= pressurePosition.y ? -1.0f : 1.0f;
          target.x = pressurePosition.x -
                     attackDirection * MatchTuning::Shape::COVER_FALLBACK_DEPTH;
          target.y =
              pressurePosition.y +
              lateralSide * MatchTuning::Shape::COVER_FALLBACK_LATERAL_OFFSET;
          job = planJob(PlanBranch::COVER, lateralSide > 0.0f ? 1U : 0U);
        }
      }
      else if (const MatchPlayer* tight = tightMarkTargetBinturong(player))
      {
        // Under a tight-marking order he stays with his man in every phase,
        // goal-side and close enough to contest each ball played to him.
        player.intent = PlayerIntent::MARK_OPPONENT;
        target = tightMarkPointBinturong(player, *tight, target);
        targetVelocity = tightMarkVelocityBinturong(*tight);
        pinned = true;
        job = planJob(PlanBranch::TIGHT_MARK, 0,
                      static_cast<std::uint32_t>(slotOf(*tight)));
      }
      else if (teamPhase == TeamPhase::DEFENSIVE_TRANSITION)
      {
        // The nearest players counter-press above. Everyone else first gets
        // goal-side and narrows toward the danger before settling into the
        // normal defensive block.
        player.intent = PlayerIntent::RECOVER_SHAPE;
        target.x -=
            attackDirection * MatchTuning::Shape::DEFENSIVE_TRANSITION_RECOVERY;
        target.y += (shapeAnchor.y - target.y) *
                    MatchTuning::Shape::DEFENSIVE_TRANSITION_BALL_COMPACTNESS;
        job = planJob(PlanBranch::TRANSITION_RECOVER);
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
          Vector2F markPoint = mark->position;
          const float markDanger =
              player.isHomeTeam ? 1.0f - mark->position.x : mark->position.x;
          const bool goalSide =
              markDanger >= MatchTuning::Shape::TIGHT_MARK_DANGER_DEPTH;
          job = planJob(PlanBranch::ZONAL_MARK, goalSide ? 1U : 0U,
                        static_cast<std::uint32_t>(assigned));
          if (goalSide)
          {
            // Near the own goal a marker stays tight and goal-side.
            markWeight =
                std::max(markWeight, MatchTuning::Shape::TIGHT_MARK_WEIGHT);
            markPoint.x -= attackDirection *
                           MatchTuning::Shape::GOAL_SIDE_MARK_METRES /
                           MatchTuning::Pitch::LENGTH_METRES;
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
          job = planJob(PlanBranch::RECEIVE);
          const float ballSpeed = length(ball.velocity);
          const Vector2F lookahead = toPitch(
              {ball.velocity.x *
                   MatchTuning::Player::PASS_RECEIVER_LOOKAHEAD_SECONDS,
               ball.velocity.y *
                   MatchTuning::Player::PASS_RECEIVER_LOOKAHEAD_SECONDS});
          Vector2F meet{ball.position.x + lookahead.x,
                        ball.position.y + lookahead.y};
          const float heightMetres = ball.z * MatchTuning::Units::BALL_Z_METRES;
          if (heightMetres > bodyLines(player).head || ball.velocityZ > 0.0f)
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
                toPitch({ball.velocity.x * fallSeconds * AIR_TRAVEL_SHARE,
                         ball.velocity.y * fallSeconds * AIR_TRAVEL_SHARE});
            meet = {ball.position.x + landing.x, ball.position.y + landing.y};
            job = planJob(PlanBranch::RECEIVE, 1U);
          }
          else if (ballSpeed > EPSILON)
          {
            const Vector2F heading{ball.velocity.x / ballSpeed,
                                   ball.velocity.y / ballSpeed};
            const Vector2F offset =
                toMetres({player.position.x - ball.position.x,
                          player.position.y - ball.position.y});
            const float along = offset.x * heading.x + offset.y * heading.y;
            if (along > 0.0f)
            {
              // On its path, not where it is: a different spot.
              job = planJob(PlanBranch::RECEIVE, 2U);
              const Vector2F ahead =
                  toPitch({heading.x * along, heading.y * along});
              meet = {ball.position.x + ahead.x, ball.position.y + ahead.y};
            }
          }
          target = clampToPitch(meet);
        }
        else
        {
          player.intent = PlayerIntent::OFFER_SUPPORT;
          // Preserve the lane established before release instead of snapping
          // every supporting player back toward the formation anchor for the
          // duration of each pass; drift gently forward and toward the ball
          // (rates per second, so a long pass does not accumulate a run).
          job = planJob(PlanBranch::FLIGHT_SUPPORT);
          target = player.tacticalTarget;
          target.x += attackDirection *
                      MatchTuning::Shape::IN_FLIGHT_SUPPORT_ADVANCE_PER_SECOND *
                      dt;
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
        job = planJob(pressers[0] == &player ? PlanBranch::LOOSE_BALL
                                             : PlanBranch::LOOSE_COVER);
        // Run at where he can first meet it on its path.
        const auto [meetSeconds, meet] = interception(player);
        looseChase = pressers[0] == &player;
        looseMeetSeconds = meetSeconds;
        target = clampToPitch(meet);
        if (pressers[1] == &player)
        {
          target.x -=
              attackDirection * MatchTuning::Player::COVER_LOOSE_BALL_OFFSET;
        }
      }
      else if (getPossessionState().isPass || getPossessionState().isShot)
      {
        const MatchPlayer* tight =
            getPossessionState().isPass && getPossessionState().passByHome != player.isHomeTeam
                ? tightMarkTargetBinturong(player)
                : nullptr;
        if (tight)
        {
          // A tight marker goes with his man while the pass is on its way.
          player.intent = PlayerIntent::MARK_OPPONENT;
          target = tightMarkPointBinturong(player, *tight, target);
          targetVelocity = tightMarkVelocityBinturong(*tight);
          pinned = true;
          job = planJob(PlanBranch::TIGHT_MARK, 0,
                        static_cast<std::uint32_t>(slotOf(*tight)));
        }
        else if (teamPhase == TeamPhase::DEFENSIVE_TRANSITION)
        {
          player.intent = PlayerIntent::RECOVER_SHAPE;
          target.x -= attackDirection *
                      MatchTuning::Shape::DEFENSIVE_TRANSITION_RECOVERY;
          target.y += (shapeAnchor.y - target.y) *
                      MatchTuning::Shape::DEFENSIVE_TRANSITION_BALL_COMPACTNESS;
          job = planJob(PlanBranch::TRANSITION_RECOVER);
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
          target.x +=
              attackDirection *
              (transitionRunner
                   ? MatchTuning::Shape::ATTACKING_TRANSITION_RUN_ADVANCE
                   : MatchTuning::Shape::ATTACKING_TRANSITION_SUPPORT_ADVANCE);
          target.y += (shapeAnchor.y - target.y) *
                      MatchTuning::Shape::ATTACKING_TRANSITION_BALL_PULL;
          const bool checkingBack =
              transitionRunner &&
              isOffside(player, player.isHomeTeam, lineFor(player.isHomeTeam));
          if (checkingBack)
          {
            target.x -= attackDirection * MatchTuning::Shape::ONSIDE_RECOVERY;
          }
          job = planJob(transitionRunner ? PlanBranch::TRANSITION_RUN
                                         : PlanBranch::TRANSITION_SUPPORT,
                        checkingBack ? 1U : 0U);
        }
        else
        {
          player.intent = PlayerIntent::RECOVER_SHAPE;
          target.x -= attackDirection *
                      MatchTuning::Shape::DEFENSIVE_TRANSITION_RECOVERY;
          job = planJob(PlanBranch::RECOVER);
        }
      }
    }

    // Going for a loose high ball: to the spot, held, or arriving with the
    // ball for a running jump (he runs through it).
    bool throughTarget = false;
    bool challenging = false;
    for (const AerialRole& role : aerialRoles)
    {
      if (role.player != &player) continue;
      challenging = true;
      target = clampToPitch(role.meet);
      targetVelocity = {0.0f, 0.0f};
      pinned = true;
      player.intent =
          role.contests ? PlayerIntent::CLAIM_LOOSE_BALL : PlayerIntent::PRESS_BALL;
      throughTarget = role.running;
      job = planJob(PlanBranch::AERIAL_CHALLENGE,
                    (role.contests ? 1U : 0U) | (role.running ? 2U : 0U));
    }

    // A lofted delivery into the box is attacked by the receiver while the
    // defenders hold their marks instead of drifting back into shape; the
    // one going for it (above) is released from his.
    if (getPossessionState().isAerialDelivery && !carrier && !challenging &&
        getPossessionState().passByHome != player.isHomeTeam &&
        player.intent != PlayerIntent::CLAIM_LOOSE_BALL)
    {
      target = player.tacticalTarget;
      targetVelocity = {0.0f, 0.0f};
      pinned = true;
      job = planJob(PlanBranch::AERIAL_HOLD);
    }

    if (goalkeeper)
    {
      const bool receivingBackPass =
          getPossessionState().isPass && getExpectedReceiver() == player.player &&
          player.intent == PlayerIntent::RECEIVE_PASS;
      player.intent = PlayerIntent::GOALKEEP;
      targetVelocity = {0.0f, 0.0f};
      pinned = false;
      if (keeperInDive(player))
        continue;
      // A keeper receiving a team-mate's pass steps onto the ball's path.
      if (!receivingBackPass)
      {
        target = goalkeeperTargetBinturong(player, carrier);
        job = planJob(PlanBranch::GOALKEEPER);
      }
    }

    // Unless it is pinned to a man, how fast his target moves is its move
    // since his last plan, if he is still on the same job: the same branch
    // and choices, no forced refresh since, and planned no more than one
    // refresh ago. A new job is a new target, standing still.
    const std::uint64_t sincePlan = stepCounter - player.plannedStep;
    const bool sameJob =
        player.planned && player.plannedJob == job &&
        player.plannedEpoch == planEpoch && sincePlan > 0 &&
        sincePlan <= MatchTuning::Timing::TACTICAL_REFRESH_STEPS;
    if (!pinned && sameJob)
    {
      const Vector2F moved =
          toMetres({target.x - player.plannedTarget.x,
                    target.y - player.plannedTarget.y});
      const float seconds = static_cast<float>(sincePlan) *
                            MatchTuning::Timing::FIXED_STEP_SECONDS;
      targetVelocity = {moved.x / seconds, moved.y / seconds};
    }
    player.plannedTarget = target;
    player.plannedJob = job;
    player.plannedStep = stepCounter;
    player.plannedEpoch = planEpoch;
    player.planned = true;

    // Keepers shuffle constantly to stay on the ball-goal line.
    const bool urgentTarget = player.intent == PlayerIntent::PRESS_BALL ||
                              player.intent == PlayerIntent::CLAIM_LOOSE_BALL ||
                              player.intent == PlayerIntent::GOALKEEP;
    player.tacticalTarget = target;
    player.targetVelocity = targetVelocity;
    player.urgentMovement = urgentTarget;
    // No route planning yet (overtakes are added after), and no body
    // orientation asked: he heads straight for it, his body following.
    player.routeWaypoints = 0;
    player.facingRequested = false;
    player.arriveAtSpeed = throughTarget;
    // Chasing the loose ball (on the ground, or up for it), and whether he
    // steers round others on the way: the ground chaser, and a player
    // moving to hold shape, offer support or recover shape (way-round plan).
    player.chasingLooseBall =
        looseChase || player.intent == PlayerIntent::CLAIM_LOOSE_BALL;
    player.steersRound =
        !goalkeeper && !challenging &&
        (looseChase || player.intent == PlayerIntent::HOLD_SHAPE ||
         player.intent == PlayerIntent::OFFER_SUPPORT ||
         player.intent == PlayerIntent::RECOVER_SHAPE);
    player.looseBallMeetSeconds = looseMeetSeconds;
  }
}

void MatchEngine::planOvertakeBinturong(std::size_t slot)
{
  // One runner (slot), planned in slot order. Racing someone to the same
  // target (movement targets plan 4a): two players running the same way to
  // about the same point, the one behind
  // closing and about to run into the other (predicted closest approach
  // under the clearance) before he gets there. He goes round, on the
  // shorter side that still wins the race and that nobody else is about to
  // run into; else he carries on (and bumps).
  const auto metresAt = [](Vector2F pitch) { return toMetres(pitch); };
  const auto lengthOf = [](Vector2F v) { return std::sqrt(v.x * v.x + v.y * v.y); };
  const auto clearanceFor = [](const MatchPlayer& a, const MatchPlayer& b)
  {
    return a.isHomeTeam == b.isHomeTeam ? OVERTAKE_TEAM_MATE_CLEARANCE_METRES
                                        : OVERTAKE_OPPONENT_CLEARANCE_METRES;
  };
  // When he reaches a target moving at `velocity` (metres) from `from`,
  // running at `speed`, after `already` seconds, and where it is then.
  const auto arrival = [&](Vector2F from, Vector2F target, Vector2F velocity,
                           float speed, float already)
  {
    Vector2F at = target;
    float seconds = already;
    for (int pass = 0; pass < 3; ++pass)
    {
      seconds = already + lengthOf({at.x - from.x, at.y - from.y}) / speed;
      at = {target.x + velocity.x * seconds, target.y + velocity.y * seconds};
    }
    return std::pair{seconds, at};
  };
  // The closest two straight runs come within `window` seconds: one from
  // `a` at `va`, the other from `b` at `vb`.
  const auto closestApproach = [&](Vector2F a, Vector2F va, Vector2F b,
                                   Vector2F vb, float window)
  {
    const Vector2F r{b.x - a.x, b.y - a.y};
    const Vector2F v{vb.x - va.x, vb.y - va.y};
    const float vv = v.x * v.x + v.y * v.y;
    const float t =
        vv > 1e-6f ? std::clamp(-(r.x * v.x + r.y * v.y) / vv, 0.0f, window)
                   : 0.0f;
    return std::pair{lengthOf({r.x + v.x * t, r.y + v.y * t}), t};
  };
  // When `who` reaches his target from `from` (after `already` seconds) at
  // `speed`, by the waypoints of his route, and where it is then.
  const auto finishTime = [&](const MatchPlayer& who, Vector2F from,
                              float already, float speed)
  {
    float seconds = already;
    Vector2F at = from;
    const std::array<Vector2F, 2> waypoints{metresAt(who.movementTarget),
                                            metresAt(who.nextTarget)};
    for (int index = 0; index < std::min<int>(who.routeWaypoints, 2); ++index)
    {
      const Vector2F next = waypoints[static_cast<std::size_t>(index)];
      seconds += lengthOf({next.x - at.x, next.y - at.y}) / speed;
      at = next;
    }
    return arrival(at, metresAt(who.tacticalTarget), who.targetVelocity, speed,
                   seconds);
  };

  MatchPlayer& runner = players[slot];
  // Only a body that follows routes can go round.
  if (!active(runner) || runner.isGoalkeeper || !followsRoutes(runner) ||
      getPossessionAttachment() == runner.player)
  {
    runner.overtaking = -1;
    return;
  }
  const Vector2F position = metresAt(runner.position);
  const float speed = lengthOf(runner.velocity);
  const Vector2F target = metresAt(runner.tacticalTarget);
  // Where he is running now: his route's next waypoint, else his target.
  const bool routed = runner.routeWaypoints > 0;
  const Vector2F aimed =
      routed ? metresAt(runner.movementTarget) : target;
  const float toTarget = lengthOf({aimed.x - position.x, aimed.y - position.y});
  if (speed < OVERTAKE_MINIMUM_SPEED || toTarget < 1e-3f)
  {
    runner.overtaking = -1;
    return;
  }
  // His run as he means it (straight at where he is running), not as his
  // feet point mid-swerve: judged on that, a way round once found is kept
  // until he is past (judged on his velocity, it switched itself off).
  const Vector2F heading{(aimed.x - position.x) / toTarget,
                         (aimed.y - position.y) / toTarget};
  const Vector2F velocity{heading.x * speed, heading.y * speed};
  // The one he would run into first, if any.
  int ahead = -1;
  float meetAt = 0.0f;
  for (std::size_t other = 0; other < players.size(); ++other)
  {
    const MatchPlayer& leader = players[other];
    if (other == slot || !active(leader)) continue;
    const float leaderSpeed = lengthOf(leader.velocity);
    if (leaderSpeed < OVERTAKE_MINIMUM_SPEED) continue;
    const Vector2F leaderTarget = metresAt(leader.tacticalTarget);
    if (lengthOf({leaderTarget.x - target.x, leaderTarget.y - target.y}) >
        OVERTAKE_SAME_TARGET_METRES)
      continue;
    if ((leader.velocity.x * heading.x + leader.velocity.y * heading.y) /
            leaderSpeed <
        OVERTAKE_SAME_DIRECTION_COSINE)
      continue;
    const Vector2F leaderAt = metresAt(leader.position);
    if ((leaderAt.x - position.x) * heading.x +
            (leaderAt.y - position.y) * heading.y <=
        0.0f)
      continue;
    const auto [closest, when] = closestApproach(
        position, velocity, leaderAt, leader.velocity,
        std::min(OVERTAKE_HORIZON_SECONDS, toTarget / speed));
    if (closest >= clearanceFor(runner, leader)) continue;
    if (ahead < 0 || when < meetAt)
    {
      ahead = static_cast<int>(other);
      meetAt = when;
    }
  }
  if (ahead < 0)
  {
    runner.overtaking = -1;
    return;
  }
  const MatchPlayer& leader = players[static_cast<std::size_t>(ahead)];
  const float clearance = clearanceFor(runner, leader);
  const Vector2F leaderAt = metresAt(leader.position);
  const float leaderSpeed = lengthOf(leader.velocity);
  // The race is to where he is running now: on a route, its next
  // waypoint (a turn there is no place to be alongside someone), else his
  // target. The overtake must be over before he gets there: level with
  // the leader and clear of him by the clearance.
  if (meetAt * speed + clearance > toTarget)
  {
    runner.overtaking = -1;
    return;
  }
  const float leaderTime =
      routed ? lengthOf({aimed.x - leaderAt.x, aimed.y - leaderAt.y}) / leaderSpeed
             : finishTime(leader, leaderAt, 0.0f, leaderSpeed).first;
  // Where the leader will be when they draw level, and a waypoint beside
  // him there, either side.
  const Vector2F level{leaderAt.x + leader.velocity.x * meetAt,
                       leaderAt.y + leader.velocity.y * meetAt};
  const Vector2F across{-heading.y, heading.x};
  struct Option
  {
    bool ok = false;
    Vector2F waypoint{};
    float metres = 0.0f;
  };
  std::array<Option, 2> options;
  for (std::size_t index = 0; index < 2; ++index)
  {
    const float side = index == 0 ? 1.0f : -1.0f;
    Option& option = options[index];
    option.waypoint = {level.x + across.x * side * clearance,
                       level.y + across.y * side * clearance};
    const Vector2F toWaypoint{option.waypoint.x - position.x,
                              option.waypoint.y - position.y};
    const float firstLeg = lengthOf(toWaypoint);
    const float firstTime = firstLeg / speed;
    // Then on to where he is running (his route's next waypoint, or his
    // target, moving or not).
    const Vector2F onTo = routed ? aimed : finishTime(runner, option.waypoint,
                                                      firstTime, speed).second;
    const Vector2F toEnd{onTo.x - option.waypoint.x, onTo.y - option.waypoint.y};
    const float time = firstTime + lengthOf(toEnd) / speed;
    option.metres = time * speed;
    // He must still win.
    if (time >= leaderTime) continue;
    // And nobody else may be about to run into his way round.
    const Vector2F firstVelocity{toWaypoint.x / std::max(firstTime, 1e-3f),
                                 toWaypoint.y / std::max(firstTime, 1e-3f)};
    const float secondTime = std::max(lengthOf(toEnd) / speed, 1e-3f);
    const Vector2F secondVelocity{toEnd.x / secondTime, toEnd.y / secondTime};
    bool clear = true;
    for (std::size_t other = 0; other < players.size() && clear; ++other)
    {
      const MatchPlayer& third = players[other];
      if (other == slot || other == static_cast<std::size_t>(ahead) ||
          !active(third))
        continue;
      const Vector2F at = metresAt(third.position);
      const float room = clearanceFor(runner, third);
      const float window = std::min(firstTime, OVERTAKE_HORIZON_SECONDS);
      if (closestApproach(position, firstVelocity, at, third.velocity, window)
              .first < room)
        clear = false;
      else if (firstTime < OVERTAKE_HORIZON_SECONDS)
      {
        const Vector2F later{at.x + third.velocity.x * firstTime,
                             at.y + third.velocity.y * firstTime};
        if (closestApproach(option.waypoint, secondVelocity, later,
                            third.velocity,
                            std::min(secondTime,
                                     OVERTAKE_HORIZON_SECONDS - firstTime))
                .first < room)
          clear = false;
      }
    }
    option.ok = clear;
  }
  // The shorter side that works; the one he chose before unless the other
  // is clearly shorter.
  int chosen = -1;
  for (int index = 0; index < 2; ++index)
    if (options[static_cast<std::size_t>(index)].ok &&
        (chosen < 0 || options[static_cast<std::size_t>(index)].metres <
                           options[static_cast<std::size_t>(chosen)].metres))
      chosen = index;
  if (chosen >= 0 && runner.overtaking == ahead && runner.overtakeSide != 0)
  {
    const int previous = runner.overtakeSide > 0 ? 0 : 1;
    if (previous != chosen && options[static_cast<std::size_t>(previous)].ok &&
        options[static_cast<std::size_t>(previous)].metres <=
            options[static_cast<std::size_t>(chosen)].metres +
                OVERTAKE_STICKY_METRES)
      chosen = previous;
  }
  if (chosen < 0)
  {
    runner.overtaking = -1;
    return;
  }
  const Option& option = options[static_cast<std::size_t>(chosen)];
  runner.overtaking = static_cast<std::int8_t>(ahead);
  runner.overtakeSide = chosen == 0 ? 1 : -1;
  const Vector2F waypoint = toPitch(option.waypoint);
  if (routed)
  {
    // Ahead of his route: the way round first, then its next waypoint
    // (its line through them as it was).
    runner.nextTarget = runner.movementTarget;
    runner.movementTarget = waypoint;
    runner.routeWaypoints = 2;
  }
  else
  {
    runner.movementTarget = waypoint;
    runner.nextTarget = runner.tacticalTarget;
    runner.routeWaypoints = 1;
    runner.routeApexOffset = 0.0f;
    runner.routeOutwardRoom = clearance;
  }
}

void MatchEngine::planWayRoundBinturong(std::size_t slot)
{
  // A loose-ball chaser, or a player moving to hold shape, offer support
  // or recover shape
  // (way-round plan): the first player not chasing the ball he would come
  // within the clearance of, before he gets where he is going and short of
  // its last metres, is gone round by one waypoint beside where
  // that player will be then: on the side that makes the shorter run, on
  // the pitch, clear of everyone else (the man passed included, on the way
  // back) and, chasing, still his ball. Neither side: he carries on (and may bump).
  MatchPlayer& runner = players[slot];
  const int previousOf = runner.wayRoundOf;
  const int previousSide = runner.wayRoundSide;
  // Who he gave way to, or had the right of way over, at the last plan.
  const int previousGiveTo = runner.givingWayTo;
  const int previousRightOver = runner.rightOfWayOver;
  runner.wayRoundOf = -1;
  runner.wayRoundSide = 0;
  runner.wayRoundBlockedBy = -1;
  runner.giveWaySpeed = -1.0f;
  runner.givingWayTo = -1;
  runner.rightOfWayOver = -1;
  if (!active(runner) || !runner.steersRound || runner.fallen ||
      !followsRoutes(runner) || runner.routeWaypoints > 0 || state != MatchState::PLAYING)
    return;
  // A chaser: open play only, with the ball loose. Getting into position
  // (hold shape, support, recover): with the ball owned too, and in a set
  // piece's phase everywhere but the box it attacks, until the ball has
  // been in that box and come out.
  if (runner.chasingLooseBall
          ? setPiecePhaseRemaining > 0.0f || getPossessionAttachment() != nullptr
          : setPiecePhaseRemaining > 0.0f && !setPieceBoxCleared &&
                inPenaltyArea(runner.position, !setPieceHome))
    return;
  const auto lengthOf = [](Vector2F v) { return std::sqrt(v.x * v.x + v.y * v.y); };
  const auto minus = [](Vector2F a, Vector2F b) { return Vector2F{a.x - b.x, a.y - b.y}; };
  // The closest two straight runs come within `window` seconds, and when.
  const auto closestApproach = [&](Vector2F a, Vector2F va, Vector2F b, Vector2F vb,
                                   float window)
  {
    const Vector2F r = minus(b, a);
    const Vector2F v = minus(vb, va);
    const float vv = v.x * v.x + v.y * v.y;
    const float t =
        vv > 1e-6f ? std::clamp(-(r.x * v.x + r.y * v.y) / vv, 0.0f, window) : 0.0f;
    return std::pair{lengthOf({r.x + v.x * t, r.y + v.y * t}), t};
  };
  const auto clearanceFor = [&](const MatchPlayer& other)
  {
    return other.isHomeTeam == runner.isHomeTeam ? OVERTAKE_TEAM_MATE_CLEARANCE_METRES
                                                 : OVERTAKE_OPPONENT_CLEARANCE_METRES;
  };
  // Anyone not after the ball himself (a chaser of either side is a
  // contest; an Aardvark brain's, by his intent) and on his feet (a fallen
  // player is hurdled).
  const auto inPlay = [&](const MatchPlayer& other)
  {
    return active(other) && &other != &runner && !other.fallen &&
           !other.chasingLooseBall &&
           !(brainOf(other.isHomeTeam) == BrainId::AARDVARK &&
             other.intent == PlayerIntent::CLAIM_LOOSE_BALL);
  };

  const Vector2F position = toMetres(runner.position);
  const Vector2F meet = toMetres(runner.tacticalTarget);
  const Vector2F toMeet = minus(meet, position);
  const float meetMetres = lengthOf(toMeet);
  if (meetMetres <= WAY_ROUND_FINAL_METRES) return;
  // His run as he means it: straight at where he is going (his meet with
  // the ball, chasing; his spot): flat out, chasing; else at the speed he
  // is going (a job's pace is only what he works up to).
  const bool chasing = runner.chasingLooseBall;
  const float speed = std::max(chasing ? currentTopSpeed(runner) : length(runner.velocity),
                               OVERTAKE_MINIMUM_SPEED);
  const float toBallSeconds = meetMetres / speed;
  const Vector2F heading{toMeet.x / meetMetres, toMeet.y / meetMetres};
  const Vector2F velocity{heading.x * speed, heading.y * speed};
  const float window = std::min(WAY_ROUND_HORIZON_SECONDS, toBallSeconds);
  // Within the last metres before his meet it is a challenge, not a run.
  const auto nearBall = [&](Vector2F point)
  { return lengthOf(minus(meet, point)) <= WAY_ROUND_FINAL_METRES; };

  // The first he would come within the clearance of.
  int first = -1;
  float firstAt = 0.0f;
  for (std::size_t other = 0; other < players.size(); ++other)
  {
    const MatchPlayer& man = players[other];
    if (!inPlay(man)) continue;
    const Vector2F at = toMetres(man.position);
    if (nearBall(at)) continue;
    const auto [closest, when] =
        closestApproach(position, velocity, at, man.velocity, window);
    if (closest >= clearanceFor(man)) continue;
    if (nearBall({position.x + velocity.x * when, position.y + velocity.y * when}) ||
        nearBall({at.x + man.velocity.x * when, at.y + man.velocity.y * when}))
      continue;
    if (first < 0 || when < firstAt)
    {
      first = static_cast<int>(other);
      firstAt = when;
    }
  }
  if (first < 0) return;
  const MatchPlayer& man = players[static_cast<std::size_t>(first)];
  const float room = clearanceFor(man) + WAY_ROUND_MARGIN_METRES;

  // Both going round each other (giving way, decisions 1-2): only one of
  // them moves for it. Who reaches the point their paths cross first has
  // the right of way and holds his line; the other eases off to reach it
  // only once he is past, or (head-on, the same way: no crossing to wait
  // for) goes round alone. Judged on both men's actual runs, so each
  // reckons it the same way.
  // (Only with a man who can steer himself now: on his way, not in the last
  // metres to his own spot.)
  if (man.steersRound && !runner.chasingLooseBall && followsRoutes(man) &&
      length(man.velocity) >= GIVE_WAY_MOVING_SPEED &&
      distance(man.position, man.tacticalTarget) > WAY_ROUND_FINAL_METRES)
  {
    const Vector2F mine = length(runner.velocity) >= GIVE_WAY_MOVING_SPEED
                              ? runner.velocity
                              : velocity;
    const Vector2F theirs = man.velocity;
    const auto cross = [](Vector2F a, Vector2F b) { return a.x * b.y - a.y * b.x; };
    const Vector2F gap = minus(toMetres(man.position), position);
    const float turn = cross(mine, theirs);
    const float cosine = (mine.x * theirs.x + mine.y * theirs.y) /
                         std::max(lengthOf(mine) * lengthOf(theirs), 1e-6f);
    // When each reaches where their paths cross (s), if ahead of both.
    const float myTime = std::abs(turn) > 1e-6f ? cross(gap, theirs) / turn : -1.0f;
    const float theirTime = std::abs(turn) > 1e-6f ? cross(gap, mine) / turn : -1.0f;
    const bool crossing = cosine > -GIVE_WAY_CROSSING_COSINE &&
                          cosine < GIVE_WAY_CROSSING_COSINE && myTime > 0.0f &&
                          theirTime > 0.0f;
    // The same two still meeting: as they were (a near tie read afresh
    // each plan would swap them back and forth), the other man's record of
    // it first, so the two always agree.
    const auto me = static_cast<std::int8_t>(slot);
    const bool wasLeading = man.givingWayTo == me ||
                            (man.rightOfWayOver != me && previousRightOver == first);
    const bool wasGiving = !wasLeading && (man.rightOfWayOver == me || previousGiveTo == first);
    // Crossing: the first there. The same way: the one in front. Head-on:
    // the lower slot (a fixed rule both read alike).
    const bool lead = wasLeading ? true : wasGiving ? false :
        crossing ? (myTime < theirTime ||
                    (myTime == theirTime && slot < static_cast<std::size_t>(first)))
        : cosine >= GIVE_WAY_CROSSING_COSINE
            ? -(gap.x * mine.x + gap.y * mine.y) > 0.0f
            : slot < static_cast<std::size_t>(first);
    if (lead)
    {
      runner.rightOfWayOver = static_cast<std::int8_t>(first);
      ++stats.giveWayHolds;
      return;
    }
    runner.givingWayTo = static_cast<std::int8_t>(first);
    if (crossing)
    {
      // To reach the crossing (short of it by the clearance) only once he
      // is past it by the clearance; slower than a walk's start, a stop.
      const float clearance = clearanceFor(man);
      const float leaderPast = theirTime + clearance / std::max(lengthOf(theirs), 1e-3f);
      const float myRoom = std::max(0.0f, lengthOf(mine) * myTime - clearance);
      float allowed = myRoom / std::max(leaderPast, 1e-3f);
      if (allowed < GIVE_WAY_MOVING_SPEED) allowed = 0.0f;
      runner.giveWaySpeed = allowed;
      ++stats.giveWaySlows;
      return;
    }
    ++stats.giveWayRounds;
  }
  const Vector2F manAt = toMetres(man.position);
  const Vector2F level{manAt.x + man.velocity.x * firstAt,
                       manAt.y + man.velocity.y * firstAt};
  const Vector2F across{-heading.y, heading.x};
  // When the other side's chaser meets it: going round must not hand it
  // to him (if he was going to be first).
  float rival = std::numeric_limits<float>::max();
  for (const MatchPlayer& other : players)
    if (active(other) && other.isHomeTeam != runner.isHomeTeam && other.chasingLooseBall &&
        other.looseBallMeetSeconds > 0.0f)
      rival = std::min(rival, other.looseBallMeetSeconds);
  const float minX = MatchTuning::Pitch::PLAYER_MIN_X * MatchTuning::Pitch::LENGTH_METRES;
  const float maxX = MatchTuning::Pitch::PLAYER_MAX_X * MatchTuning::Pitch::LENGTH_METRES;
  const float minY = MatchTuning::Pitch::PLAYER_MIN_Y * MatchTuning::Pitch::WIDTH_METRES;
  const float maxY = MatchTuning::Pitch::PLAYER_MAX_Y * MatchTuning::Pitch::WIDTH_METRES;

  struct Option
  {
    bool ok = false;
    Vector2F waypoint{};
    float metres = 0.0f;
  };
  std::array<Option, 2> options;
  for (std::size_t index = 0; index < 2; ++index)
  {
    const float side = index == 0 ? 1.0f : -1.0f;
    Option& option = options[index];
    option.waypoint = {level.x + across.x * side * room, level.y + across.y * side * room};
    // On the pitch.
    if (option.waypoint.x < minX || option.waypoint.x > maxX ||
        option.waypoint.y < minY || option.waypoint.y > maxY)
      continue;
    const Vector2F firstLeg = minus(option.waypoint, position);
    const Vector2F secondLeg = minus(meet, option.waypoint);
    const float firstMetres = lengthOf(firstLeg);
    const float secondMetres = lengthOf(secondLeg);
    option.metres = firstMetres + secondMetres;
    const float firstTime = firstMetres / speed;
    const float secondTime = std::max(secondMetres / speed, 1e-3f);
    // Chasing, still his ball.
    if (chasing && toBallSeconds < rival && firstTime + secondTime >= rival) continue;
    const Vector2F firstVelocity{firstLeg.x / std::max(firstTime, 1e-3f),
                                 firstLeg.y / std::max(firstTime, 1e-3f)};
    const Vector2F secondVelocity{secondLeg.x / secondTime, secondLeg.y / secondTime};
    // Clear of everyone else on the way there, and of everyone (the man
    // passed too) on the way back to the ball, short of its last metres.
    bool clear = true;
    for (std::size_t other = 0; other < players.size() && clear; ++other)
    {
      const MatchPlayer& third = players[other];
      if (!inPlay(third)) continue;
      const Vector2F at = toMetres(third.position);
      const float clearance = clearanceFor(third);
      if (other != static_cast<std::size_t>(first))
      {
        const auto [gap, when] =
            closestApproach(position, firstVelocity, at, third.velocity,
                            std::min(firstTime, WAY_ROUND_HORIZON_SECONDS));
        if (gap < clearance &&
            !nearBall({position.x + firstVelocity.x * when,
                       position.y + firstVelocity.y * when}))
          clear = false;
      }
      if (!clear || firstTime >= WAY_ROUND_HORIZON_SECONDS) continue;
      const Vector2F later{at.x + third.velocity.x * firstTime,
                           at.y + third.velocity.y * firstTime};
      const auto [gap, when] =
          closestApproach(option.waypoint, secondVelocity, later, third.velocity,
                          std::min(secondTime, WAY_ROUND_HORIZON_SECONDS - firstTime));
      // The man passed is level with the waypoint as the leg back starts:
      // only closing in on him again after it counts.
      if (gap < clearance && when > 1e-3f &&
          !nearBall({option.waypoint.x + secondVelocity.x * when,
                     option.waypoint.y + secondVelocity.y * when}))
        clear = false;
    }
    option.ok = clear;
  }
  // The shorter side that works; the one he chose before unless the other
  // is clearly shorter.
  int chosen = -1;
  for (int index = 0; index < 2; ++index)
    if (options[static_cast<std::size_t>(index)].ok &&
        (chosen < 0 || options[static_cast<std::size_t>(index)].metres <
                           options[static_cast<std::size_t>(chosen)].metres))
      chosen = index;
  if (chosen >= 0 && previousOf == first && previousSide != 0)
  {
    const int previous = previousSide > 0 ? 0 : 1;
    if (previous != chosen && options[static_cast<std::size_t>(previous)].ok &&
        options[static_cast<std::size_t>(previous)].metres <=
            options[static_cast<std::size_t>(chosen)].metres + WAY_ROUND_STICKY_METRES)
      chosen = previous;
  }
  if (chosen < 0)
  {
    runner.wayRoundBlockedBy = static_cast<std::int8_t>(first);
    // Giving way with no way round: he eases off instead. The same way, to
    // a little under the speed the other is going his way; head-on, a stop.
    if (runner.givingWayTo == first)
    {
      const float along = man.velocity.x * heading.x + man.velocity.y * heading.y;
      runner.giveWaySpeed =
          std::max(0.0f, along - GIVE_WAY_UNDER_LEADER_SPEED);
      if (runner.giveWaySpeed < GIVE_WAY_MOVING_SPEED) runner.giveWaySpeed = 0.0f;
      ++stats.giveWaySlows;
    }
    return;
  }
  const int side = chosen == 0 ? 1 : -1;
  runner.wayRoundOf = static_cast<std::int8_t>(first);
  runner.wayRoundSide = static_cast<std::int8_t>(side);
  runner.movementTarget = toPitch(options[static_cast<std::size_t>(chosen)].waypoint);
  runner.nextTarget = runner.tacticalTarget;
  runner.routeWaypoints = 1;
  runner.routeApexOffset = 0.0f;
  runner.routeOutwardRoom = room;
  const std::size_t kind = chasing                                       ? 0
                           : runner.intent == PlayerIntent::HOLD_SHAPE     ? 1
                           : runner.intent == PlayerIntent::OFFER_SUPPORT ? 2
                                                                           : 3;
  ++stats.wayRoundRefreshes[kind];
  if (previousOf != first || previousSide != side) ++stats.wayRoundsStarted[kind];
}

Vector2F MatchEngine::carryTargetBinturong(const MatchPlayer& player) const
{
  const StrategySliders sliders = getEffectiveSliders(player.isHomeTeam);
  const TeamPhase teamPhase = player.isHomeTeam ? homePhase : awayPhase;
  const float attackDirection = player.isHomeTeam ? 1.0f : -1.0f;
  Vector2F target = player.position;
  target.x +=
      attackDirection *
      (MatchTuning::Shape::CARRIER_BASE_ADVANCE +
       player.dribbling * MatchTuning::Shape::CARRIER_DRIBBLING_ADVANCE +
       sliders.riskTaking * MatchTuning::Shape::CARRIER_RISK_ADVANCE);
  if (teamPhase == TeamPhase::ATTACKING_TRANSITION)
  {
    target.x += attackDirection *
                MatchTuning::Shape::ATTACKING_TRANSITION_CARRIER_ADVANCE;
  }

  // Carry away from the nearest defender instead of running directly
  // through them, while gradually looking for a central shooting lane.
  const MatchPlayer* closestOpponent = nullptr;
  float closestDistance = std::numeric_limits<float>::max();
  for (const MatchPlayer& opponent : players)
  {
    if (!active(opponent) || opponent.isHomeTeam == player.isHomeTeam) continue;
    const float opponentDistance = distance(opponent.position, player.position);
    if (opponentDistance < closestDistance)
    {
      closestDistance = opponentDistance;
      closestOpponent = &opponent;
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
      std::abs(player.position.y - MatchTuning::Pitch::CENTRE) >
          S::WIDE_LANE_DEVIATION;
  if (cutsInsideBinturong(player))
  {
    target.y =
        player.position.y +
        (MatchTuning::Pitch::CENTRE - player.position.y) * S::CUT_INSIDE_PULL;
  }
  else if (closestOpponent && closestDistance < S::CARRIER_EVASION_RANGE_METRES)
  {
    const float evadeDirection =
        player.position.y <= closestOpponent->position.y ? -1.0f : 1.0f;
    target.y +=
        evadeDirection * (S::CARRIER_BASE_EVASION +
                          player.dribbling * S::CARRIER_DRIBBLING_EVASION);
  }
  else if (wideLane)
  {
    target.y = player.position.y;
  }
  else
  {
    target.y += (MatchTuning::Pitch::CENTRE - player.position.y) *
                S::CARRIER_CENTRALITY;
  }
  return target;
}

bool MatchEngine::cutsInsideBinturong(const MatchPlayer& player) const
{
  using S = MatchTuning::Shape;
  const PlayerRole role = player.player->getRole();
  const bool wideForward = role == PlayerRole::LM || role == PlayerRole::RM ||
                           role == PlayerRole::LW || role == PlayerRole::RW;
  const float depth =
      player.isHomeTeam ? player.position.x : 1.0f - player.position.x;
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

[[gnu::always_inline]] inline Vector2F MatchEngine::possessionAnchorBinturong(
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
  const float lateral = player.basePosition.y - Pitch::CENTRE;
  // Width pushes toward his own touchline; a central player has none.
  const float flank = std::abs(lateral) < 0.05f ? 0.0f
                      : lateral > 0.0f          ? 1.0f
                                                : -1.0f;
  Vector2F anchor = player.basePosition;
  anchor.x += direction *
              (tactic.possessionOffset.x +
               tactic.profile.possessionAdvanceMetres / Pitch::LENGTH_METRES);
  anchor.y +=
      tactic.possessionOffset.y +
      flank * tactic.profile.possessionWidthMetres / Pitch::WIDTH_METRES;
  return clampToPitch(anchor);
}

[[gnu::always_inline]] inline float MatchEngine::defensiveRoleShiftBinturong(
    const MatchPlayer& player) const
{
  return (player.isHomeTeam ? 1.0f : -1.0f) *
         roleProfileOf(player).defensiveAdvanceMetres /
         MatchTuning::Pitch::LENGTH_METRES;
}

[[gnu::always_inline]] inline const MatchPlayer*
MatchEngine::tightMarkTargetBinturong(const MatchPlayer& marker) const
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

Vector2F MatchEngine::tightMarkPointBinturong(const MatchPlayer& marker,
                                              const MatchPlayer& target,
                                              Vector2F current) const
{
  namespace T = TacticsTuning;
  const float direction = marker.isHomeTeam ? 1.0f : -1.0f;
  const Vector2F point{
      target.position.x - direction * T::TIGHT_MARK_GOAL_SIDE_METRES /
                              MatchTuning::Pitch::LENGTH_METRES,
      target.position.y};
  return {current.x + (point.x - current.x) * T::TIGHT_MARK_WEIGHT,
          current.y + (point.y - current.y) * T::TIGHT_MARK_WEIGHT};
}

Vector2F MatchEngine::tightMarkVelocityBinturong(
    const MatchPlayer& target) const
{
  // The marking point moves with his man, by the share it is pulled to him.
  return {target.velocity.x * TacticsTuning::TIGHT_MARK_WEIGHT,
          target.velocity.y * TacticsTuning::TIGHT_MARK_WEIGHT};
}

float MatchEngine::tightMarkPenaltyBinturong(const MatchPlayer& receiver) const
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

float MatchEngine::passingRoleBiasBinturong(const MatchPlayer& passer,
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
         tightMarkPenaltyBinturong(receiver);
}

[[gnu::always_inline]] inline float MatchEngine::pressEagernessBinturong(
    const MatchPlayer& candidate, const MatchPlayer* carrier) const
{
  if (!carrier || carrier->isHomeTeam == candidate.isHomeTeam) return 0.0f;
  // A rousing talk sends the side after the ball sooner (a flat one later).
  return roleProfileOf(candidate).pressBias *
             TacticsTuning::PRESS_BIAS_SECONDS +
         talkSwing(candidate.isHomeTeam) * TacticsTuning::TALK_PRESS_SECONDS;
}

float MatchEngine::engageBoostBinturong(const MatchPlayer& defender,
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

float MatchEngine::pressStandOffShareBinturong(const MatchPlayer& presser,
                                               const MatchPlayer& carrier) const
{
  float share = 1.0f - 0.2f * roleProfileOf(presser).pressBias -
                0.2f * talkSwing(presser.isHomeTeam);
  if (instructionAgainst(carrier) == OppositionInstruction::Press)
    share *= TacticsTuning::PRESS_ORDER_STANDOFF_SHARE;
  return std::clamp(share, 0.3f, 1.3f);
}

float MatchEngine::weakFootShadeBinturong(const MatchPlayer& carrier) const
{
  if (!carrier.player ||
      instructionAgainst(carrier) != OppositionInstruction::ShowWeakFoot)
    return 0.0f;
  // Facing his attacking direction, a right-footer's strong side is +y for
  // the home side (attacking toward x = 1) and -y for the away side.
  const float strongSide =
      (carrier.player->getFoot() == Foot::Right ? 1.0f : -1.0f) *
      (carrier.isHomeTeam ? 1.0f : -1.0f);
  return strongSide * TacticsTuning::WEAK_FOOT_SHADE_METRES /
         MatchTuning::Pitch::WIDTH_METRES;
}

std::optional<Vector2F> MatchEngine::doubleUpPointBinturong(
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
  return clampToPitch(
      {carrier.position.x -
           direction * reach * 0.5f / MatchTuning::Pitch::LENGTH_METRES,
       carrier.position.y + lateral * reach / MatchTuning::Pitch::WIDTH_METRES});
}

void MatchEngine::assignMarksBinturong(bool home)
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
      opponentX[o] = players[opponents[o]].position.x;
      opponentY[o] = players[opponents[o]].position.y;
    }
    for (std::size_t m = 0; m < markerCount; ++m)
    {
      const MatchPlayer& marker = players[markers[m]];
      const Vector2F base = marker.basePosition;
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

void MatchEngine::updateRestartSupportBinturong(MatchPlayer& player,
                                                const MatchPlayer* taker)
{
  if (player.isGoalkeeper || &player == taker || restartIsSetPiece ||
      (state != MatchState::THROW_IN && state != MatchState::GOAL_KICK))
    return;

  player.movementTarget = player.basePosition;
  if (taker && player.isHomeTeam == taker->isHomeTeam)
  {
    const float gap = distance(player.position, restartSpot());
    if (gap < MatchTuning::Timing::RESTART_SUPPORT_DISTANCE_METRES)
    {
      // Offer a lane inside the pitch, with teammates spread along it.
      player.movementTarget = clampToPitch(
          {restartSpot().x + (player.basePosition.x - restartSpot().x) *
                                 MatchTuning::Timing::RESTART_SUPPORT_BLEND,
           restartSpot().y + (player.basePosition.y - restartSpot().y) *
                                 MatchTuning::Timing::RESTART_SUPPORT_BLEND});
    }
  }
  else if (taker)
  {
    const float gap = distance(player.movementTarget, restartSpot());
    const float minimum =
        state == MatchState::THROW_IN
            ? MatchTuning::Timing::THROW_OPPONENT_DISTANCE_METRES
            : MatchTuning::Timing::KICK_OPPONENT_DISTANCE_METRES;
    if (gap < minimum)
    {
      const Vector2F direction =
          metricDirection(restartSpot(), player.movementTarget);
      const Vector2F offset =
          toPitch({direction.x * minimum, direction.y * minimum});
      player.movementTarget = {restartSpot().x + offset.x,
                               restartSpot().y + offset.y};
    }
  }
}

bool MatchEngine::touchNowBinturong(const MatchPlayer& player,
                                    const TouchMoment& moment) const
{
  // Control is easier the closer the ball is: he lets it come to the
  // closest comfortable point it will pass, unless waiting would risk
  // losing it. Nobody waits to steal a ball another player is playing.
  if (moment.gap <= touchKnobs[TouchKnob::COMFORT_GAP] || hasTouchClaim())
    return true;
  // Off the ground (his chest) he plays it where it meets him, for now
  // (chest and thigh control plan step 5 decides waiting).
  if (moment.height > bodyLines(player).feet) return true;
  return !waitsForCloserBinturong(player, moment);
}

bool MatchEngine::dribbleTouchNowBinturong(const MatchPlayer& player,
                                           const TouchMoment& moment) const
{
  // His own ball, due a touch: the same timing as a first touch, played at
  // once inside the gap he means to play it at (close for a better
  // dribbler, out to the strike reach for a poor one), else let closer.
  if (moment.gap <= (getBody(player.isHomeTeam) == BodyId::BINTURONG
                         ? dribbleMeetGapBinturong(player)
                         : touchKnobs[TouchKnob::COMFORT_GAP]))
    return true;
  return !waitsForCloserBinturong(player, moment);
}

bool MatchEngine::waitsForCloserBinturong(const MatchPlayer& player,
                                          const TouchMoment& moment) const
{
  // Reads the ball's roll and his own run: whether a clearly closer point,
  // safely inside his reach, is coming before anything could take it.
  using K = TouchKnob;
  const FirstTouchKnobs& knob = touchKnobs;
  constexpr int SUBSTEPS = MatchTuning::Timing::BALL_SUBSTEPS;
  constexpr float STEP =
      MatchTuning::Timing::FIXED_STEP_SECONDS / static_cast<float>(SUBSTEPS);
  // Up to five seconds ahead, however far the knob is set.
  constexpr int MAX_LOOKAHEAD = 200;
  const int LOOKAHEAD =
      std::clamp(static_cast<int>(knob[K::TIMING_LOOKAHEAD] / STEP + 0.5f), 0,
                 MAX_LOOKAHEAD);
  const float speed = length(ball.velocity);
  // Its heading turns as its sidespin bends it (wait dead zone plan step 2).
  Vector2F heading = speed > EPSILON ? Vector2F{ball.velocity.x / speed,
                                                ball.velocity.y / speed}
                                     : Vector2F{0.0f, 0.0f};
  FlightState flight{speed, ball.velocityZ,
                     ball.z * MatchTuning::Units::BALL_Z_METRES, false,
                     ball.spin};
  const Vector2F ballStart = toMetres(ball.position);
  Vector2F ballAt = ballStart;
  const Vector2F playerStart = toMetres(player.position);
  // He stands still through the rest of this step's ball sub-steps, then
  // moves at the step boundaries (read from his current velocity).
  const int perStep = std::max(
      1, static_cast<int>(moment.stepSeconds / moment.substepSeconds + 0.5f));
  const auto movesBy = [&](int step)
  {
    return step > moment.substepsLeft
               ? 1 + (step - moment.substepsLeft - 1) / perStep
               : 0;
  };
  std::array<Vector2F, MAX_LOOKAHEAD + 1> path{};
  float bestGap = moment.gap;
  int bestStep = 0;
  float gapAfterMove = 0.0f;
  bool nearDeadZoneAfterMove = false;
  // The first sub-step at which a bounce lifts it out of reach of his feet.
  int risesAt = LOOKAHEAD + 1;
  const float feetLine = bodyLines(player).feet;
  for (int step = 1; step <= LOOKAHEAD; ++step)
  {
    const float rolled = stepFlight(flight, STEP);
    if (risesAt > LOOKAHEAD && flight.height > feetLine)
      risesAt = step;
    ballAt = {ballAt.x + heading.x * rolled, ballAt.y + heading.y * rolled};
    path[static_cast<std::size_t>(step)] = ballAt;
    const float turn = flight.turnRate * STEP;
    heading = {heading.x * std::cos(turn) - heading.y * std::sin(turn),
               heading.x * std::sin(turn) + heading.y * std::cos(turn)};
    const float moved = moment.stepSeconds * static_cast<float>(movesBy(step));
    const Vector2F gapVector{path[static_cast<std::size_t>(step)].x -
                                 (playerStart.x + player.velocity.x * moved),
                             path[static_cast<std::size_t>(step)].y -
                                 (playerStart.y + player.velocity.y * moved)};
    const float gap = length(gapVector);
    // Out of his reach, or behind him in his dead zone (as he will face
    // after his moves before then): by the next sub-step this is his last
    // chance; later, nothing past it is worth waiting for.
    const bool outOfReach =
        gap > moment.reach ||
        ballBehindReach(player, -gapVector.x, -gapVector.y, gap, moved);
    if (outOfReach)
    {
      if (step == 1) return false;
      break;
    }
    if (step == moment.substepsLeft + 1)
    {
      gapAfterMove = gap;
      // Where he will really be is uncertain by about the wait margin: the
      // ball should be clear of his dead zone even that much further from
      // him, or that far round him either way.
      if (gap > EPSILON)
      {
        const float margin = knob[K::WAIT_MARGIN];
        const float slack = std::atan2(margin, gap);
        const float out = (gap + margin) / gap;
        for (const float side : {-slack, 0.0f, slack})
        {
          const float c = std::cos(side) * out;
          const float s = std::sin(side) * out;
          const Vector2F round{gapVector.x * c - gapVector.y * s,
                               gapVector.x * s + gapVector.y * c};
          if (ballBehindReach(player, -round.x, -round.y, gap + margin,
                              moved))
            nearDeadZoneAfterMove = true;
        }
      }
    }
    if (gap < bestGap)
    {
      bestGap = gap;
      bestStep = step;
    }
    if (gap <= knob[K::COMFORT_GAP]) break;
  }
  // Only worth waiting for a clearly closer point, safely inside his reach.
  if (bestStep == 0 || bestGap > moment.gap - knob[K::WAIT_GAIN] ||
      bestGap > moment.reach - knob[K::WAIT_MARGIN])
    return false;
  // A bounce on the way would take it up off his feet first.
  if (risesAt <= bestStep) return false;
  // His own next move is the least certain part of the read: wait through
  // it only if the ball is still safely inside his reach just after.
  if (bestStep > moment.substepsLeft &&
      (gapAfterMove > moment.reach - knob[K::WAIT_MARGIN] ||
       nearDeadZoneAfterMove))
    return false;
  // An opponent who can get to the ball's path first would take it: play
  // it now, stretched or not.
  for (const MatchPlayer& opponent : players)
  {
    if (!active(opponent) || opponent.isHomeTeam == player.isHomeTeam) continue;
    const float reach =
        opponent.isGoalkeeper
            ? MatchTuning::Ball::GOALKEEPER_CONTROL_RADIUS_METRES
            : feetReach(opponent);
    const Vector2F from = toMetres(opponent.position);
    for (int step = 1; step <= bestStep; ++step)
    {
      const float elapsed = STEP * static_cast<float>(step);
      const Vector2F at = path[static_cast<std::size_t>(step)];
      const float gapX = at.x - (from.x + opponent.velocity.x * elapsed);
      const float gapY = at.y - (from.y + opponent.velocity.y * elapsed);
      if (gapX * gapX + gapY * gapY <= reach * reach) return false;
    }
  }
  return true;
}

TouchIntent MatchEngine::chooseFirstTouchBinturong(
    const MatchPlayer& player, const TouchMoment& /*moment*/) const
{
  using K = TouchKnob;
  const FirstTouchKnobs& knob = touchKnobs;
  const float attackDirection = player.isHomeTeam ? 1.0f : -1.0f;
  const float speed = length(player.velocity);
  TouchIntent intent;
  // Not expecting it: a pass meant for someone else. A stand-in until the
  // brain has the mental attributes to read it (known issues).
  intent.surprise =
      getPossessionState().isPass && getExpectedReceiver() != player.player ? 1.0f : 0.0f;
  // Pressed: an opponent close enough to be in the way. Also a stand-in
  // (known issues).
  intent.pressure = contactPressure(nearestOpponentDistance(player),
                                    knob[K::PRESSURE_RADIUS]);

  // Running into open space: push it on along his run.
  if (speed >= MatchTuning::Dribble::CLOSE_CONTROL_SPEED)
  {
    const Vector2F run{player.velocity.x / speed, player.velocity.y / speed};
    const float lead = speed * knob[K::STRIDE_LEAD] + knob[K::STRIDE_EXTRA];
    const Vector2F ahead = toPitch({run.x * lead, run.y * lead});
    const Vector2F spot =
        clampToPitch({player.position.x + ahead.x, player.position.y + ahead.y});
    bool clear = true;
    for (const MatchPlayer& opponent : players)
      if (active(opponent) && opponent.isHomeTeam != player.isHomeTeam &&
          distance(opponent.position, spot) < knob[K::STRIDE_CLEARANCE])
        clear = false;
    if (clear)
    {
      intent.kind = TouchIntent::Kind::INTO_STRIDE;
      intent.targetSpot = spot;
      intent.arrivalSpeed = speed * knob[K::STRIDE_SPEED_SHARE];
      return intent;
    }
  }

  // Otherwise set it just ahead of where he will be, toward where he means
  // to carry it, turned away from the nearest opponent the closer he is,
  // and never back toward his own goal near it.
  Vector2F aim = metricDirection(player.position, carryTargetBinturong(player));
  if (length(aim) <= EPSILON) aim = {attackDirection, 0.0f};
  const MatchPlayer* nearest = nullptr;
  float nearestDistance = std::numeric_limits<float>::max();
  for (const MatchPlayer& opponent : players)
  {
    if (!active(opponent) || opponent.isHomeTeam == player.isHomeTeam) continue;
    const float gap = distance(opponent.position, player.position);
    if (gap < nearestDistance)
    {
      nearestDistance = gap;
      nearest = &opponent;
    }
  }
  constexpr float PRESSURE = MatchTuning::Decision::PRESSURE_RADIUS_METRES;
  if (nearest && nearestDistance < PRESSURE)
  {
    const float weight = 1.0f - nearestDistance / PRESSURE;
    const Vector2F away = metricDirection(nearest->position, player.position);
    aim = normalized({aim.x * (1.0f - weight) + away.x * weight,
                      aim.y * (1.0f - weight) + away.y * weight});
    if (length(aim) <= EPSILON) aim = {attackDirection, 0.0f};
  }
  const float ownDepth =
      player.isHomeTeam ? player.position.x : 1.0f - player.position.x;
  if (ownDepth < MatchTuning::SetPiece::CROSS_CLEARANCE_DEPTH &&
      aim.x * attackDirection < 0.0f)
  {
    aim.x = 0.0f;
    aim = length(aim) > EPSILON ? normalized(aim)
                                : Vector2F{attackDirection, 0.0f};
  }
  const Vector2F offset = toPitch(
      {player.velocity.x * knob[K::SET_LEAD] + aim.x * knob[K::SET_OFFSET],
       player.velocity.y * knob[K::SET_LEAD] + aim.y * knob[K::SET_OFFSET]});
  intent.targetSpot =
      clampToPitch({player.position.x + offset.x, player.position.y + offset.y});
  return intent;
}

MatchEngine::Challenge MatchEngine::chooseChallengeBinturong(
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
  const float reach = standingTackleReachOf(defender->defending);
  const float ballDistance = distance(defender->position, ball.position);
  if (ballDistance >= D::SLIDE_TACKLE_DISTANCE_METRES) return challenge;
  // A loose dribbled ball is played, not rolled for: outside the carrier's
  // reach as a free ball (tackling plan step 1), inside it as a contest
  // (step 2), both in resolveLooseBall. No standing challenge; slides stay
  // as they are until their own plan. (Only a Binturong body contests; an
  // Aardvark one still rolls inside the carrier's reach.)
  if (hasTouchClaim() && ballDistance <= reach &&
      (!ballInReach(carrier) ||
       getBody(defender->isHomeTeam) == BodyId::BINTURONG))
    return challenge;
  const float carrierDepth =
      carrier.isHomeTeam ? carrier.position.x : 1.0f - carrier.position.x;
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
      engageBoostBinturong(*defender, carrier);
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
           engageBoostBinturong(*defender, carrier));
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

MatchEngine::DefensiveCover MatchEngine::defensiveCoverBinturong(
    const MatchPlayer& defender) const
{
  // Goal-side: nearer his own goal line than he is.
  const auto depth = [&](const MatchPlayer& player)
  { return defender.isHomeTeam ? player.position.x : 1.0f - player.position.x; };
  DefensiveCover cover;
  for (const MatchPlayer& mate : players)
  {
    if (&mate == &defender || mate.isHomeTeam != defender.isHomeTeam ||
        mate.isGoalkeeper || !active(mate) || depth(mate) >= depth(defender))
      continue;
    cover.lastMan = false;
    if (distance(mate.position, defender.position) <=
        MatchTuning::Defending::COVER_METRES)
      cover.covered = true;
  }
  return cover;
}

bool MatchEngine::goesIntoContestBinturong(const MatchPlayer& defender,
                                           const MatchPlayer& owner,
                                           Vector2F contact)
{
  // Tackling plan step 5. He reads the carrier's foot: recovering from a
  // touch, or his touch about to come (he would swing at nothing or meet
  // it in a block), and how much of the ball he has shown; how true his
  // read of the timing is, is his Defending (judgement). He goes in when
  // the cue as he reads it clears his bar: lower pressing, against a
  // "press him" order or with cover; higher as the last man or in his own
  // box. Free with no touch due is no cue at all: a defender who reads it
  // right holds, however hard his side presses.
  using D = MatchTuning::Defending;
  using R = MatchTuning::Dribble;
  const float judgement =
      std::clamp((defender.defending - STAT_1_RATING) / (STAT_99_RATING - STAT_1_RATING),
                 0.0f, 1.0f);
  const float error =
      D::READ_ERROR_POOR + (D::READ_ERROR_BEST - D::READ_ERROR_POOR) * judgement;
  // The carrier's foot as it is, and as he reads it.
  const float recovery = std::max(ball.ownTouchCooldown, owner.footCooldown);
  const bool planned = ball.meetFor == owner.player;
  const float touchIn = planned ? ball.meetSeconds : dribbleTouchIntervalBinturong(owner);
  const auto footCue = [&](float recoveryRead, float touchInRead)
  {
    if (recoveryRead > 1e-4f) return D::CUE_RECOVERING;
    if (touchInRead <= R::BLOCK_SECONDS) return D::CUE_BLOCK;
    if (touchInRead <= R::COMMIT_SECONDS) return D::CUE_SWING;
    return 0.0f;
  };
  const float readCue =
      footCue(recovery > 1e-4f ? recovery + randomFloat(-error, error) : 0.0f,
              touchIn + randomFloat(-error, error));
  const float shown =
      D::CUE_SHOWN_SHARE *
      std::max(0.0f, ballStretch(owner, contact) - ballStretch(defender, contact));
  const float cue = std::max(readCue, shown);
  float bar = D::GO_IN_BAR -
              D::GO_IN_PRESSING * getEffectiveSliders(defender.isHomeTeam).pressing;
  if (instructionAgainst(owner) == OppositionInstruction::Press)
    bar -= D::GO_IN_PRESS_ORDER;
  const DefensiveCover cover = defensiveCoverBinturong(defender);
  if (cover.covered) bar -= D::GO_IN_COVER;
  if (cover.lastMan) bar += D::GO_IN_LAST_MAN;
  if (inPenaltyArea(defender.position, defender.isHomeTeam)) bar += D::GO_IN_OWN_BOX;
  bar = std::max(bar, D::GO_IN_BAR_MIN);
  const bool goes = cue >= bar;
  // A misread: in on the carrier's free foot (row 5), nothing shown.
  if (goes && footCue(recovery, touchIn) <= 0.0f && shown < bar)
    ++stats.goInsOnFreeFoot;
  return goes;
}

TackleIntent MatchEngine::chooseTackleIntentBinturong(const MatchPlayer& tackler,
                                                      const MatchPlayer& attacker,
                                                      Vector2F contact,
                                                      bool freeBall)
{
  // Tackling plan step 5. A free ball well away from the carrier he simply
  // controls. Otherwise he knocks it away in his own third, at a stretch or
  // as the last man, and wins it everywhere else.
  using D = MatchTuning::Defending;
  if (freeBall && distance(attacker.position, contact) >= D::CONTROL_FREE_BALL_METRES)
    return TackleIntent::CONTROL;
  const float depth = tackler.isHomeTeam ? tackler.position.x : 1.0f - tackler.position.x;
  if (depth < D::KNOCK_AWAY_DEPTH || ballStretch(tackler, contact) >= D::KNOCK_AWAY_STRETCH ||
      defensiveCoverBinturong(tackler).lastMan)
    return TackleIntent::CLEAR;
  return TackleIntent::WIN;
}

MatchEngine::BallAction MatchEngine::chooseBallActionBinturong(
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
  const std::optional<PassOption> option = choosePassTargetBinturong(
      carrier, detail ? &detail->candidates : nullptr);

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
        carrier.isHomeTeam ? carrier.position.x : 1.0f - carrier.position.x;
    const bool pinnedToByline =
        carrierDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START &&
        std::abs(MatchTuning::Pitch::CENTRE - carrier.position.y) >=
            MatchTuning::Decision::WIDE_SHOT_WIDTH_DEVIATION;
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
        carrier.isHomeTeam ? carrier.position.x : 1.0f - carrier.position.x;
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
                shoutShotBiasBinturong(carrier.isHomeTeam) * rangeEligibility;
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
         shoutShotBiasBinturong(carrier.isHomeTeam) * rangeEligibility);
    if (carrierDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START &&
        std::abs(MatchTuning::Pitch::CENTRE - carrier.position.y) >=
            MatchTuning::Decision::WIDE_SHOT_WIDTH_DEVIATION)
    {
      note(shotTerms, "wide-angle discount", TermSource::LOCATION,
           shotScore * (MatchTuning::Decision::WIDE_SHOT_DISCOUNT - 1.0f));
      shotScore *= MatchTuning::Decision::WIDE_SHOT_DISCOUNT;
    }
    // A wide forward who has cut inside is looking for his shot.
    if (cutsInsideBinturong(carrier))
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
  if (cutsInsideBinturong(carrier))
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
    const float ownThird =
        carrier.isHomeTeam ? carrier.position.x : 1.0f - carrier.position.x;
    const auto winner =
        [ownThird](float pass, float shot, float carry, float shield)
    {
      if (shot >= pass && shot >= carry && shot >= shield)
        return ScenarioAction::SHOT;
      if (carry >= pass && carry >= shield) return ScenarioAction::CARRY;
      if (shield >= pass)
        return ownThird < MatchTuning::Defending::CLEARANCE_MAX_DEPTH
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
  const float ownDepth =
      carrier.isHomeTeam ? carrier.position.x : 1.0f - carrier.position.x;
  if (shieldScore >= passScore &&
      ownDepth < MatchTuning::Defending::CLEARANCE_MAX_DEPTH)
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

float MatchEngine::passingLaneRiskBinturong(const MatchPlayer& passer,
                                            const MatchPlayer& receiver) const
{
  // Each opponent ahead of the passer along the straight lane threatens it
  // by how close he stands to the line, against how far he could reach by
  // the time the ball gets there. A man already in the lane next to the
  // passer is the surest block of all: the ball runs into him.
  using P = MatchTuning::Passing;
  const Vector2F segment = toMetres({receiver.position.x - passer.position.x,
                                     receiver.position.y - passer.position.y});
  const float segmentLengthSquared =
      segment.x * segment.x + segment.y * segment.y;
  if (segmentLengthSquared <= EPSILON) return 1.0f;
  const float segmentLength = std::sqrt(segmentLengthSquared);

  float combinedSafety = 1.0f;
  for (const auto& opponent : players)
  {
    if (!active(opponent) || opponent.isHomeTeam == passer.isHomeTeam) continue;
    const Vector2F fromPasser =
        toMetres({opponent.position.x - passer.position.x,
                  opponent.position.y - passer.position.y});
    // Level with or behind the passer he cannot get in front of the ball;
    // at the far end he is contesting the reception, not the lane.
    const float projection =
        (fromPasser.x * segment.x + fromPasser.y * segment.y) /
        segmentLengthSquared;
    if (projection <= 0.0f || projection > P::LANE_END_MARGIN) continue;

    const float laneDistance = length({fromPasser.x - segment.x * projection,
                                       fromPasser.y - segment.y * projection});
    // How far along the ball has to travel to reach him decides how much
    // time he has: none close to the passer, the full radius past the ramp.
    const float ramp = std::min(
        1.0f, projection * segmentLength / P::LANE_REACH_RAMP_METRES);
    const float fullRadius = P::BASE_INTERCEPTION_RADIUS_METRES +
                             opponent.defending *
                                 P::DEFENDING_INTERCEPTION_METRES +
                             opponent.pace * P::PACE_INTERCEPTION_METRES;
    const float interceptionRadius =
        P::NEAR_LANE_REACH_METRES +
        (fullRadius - P::NEAR_LANE_REACH_METRES) * ramp;
    const float individualRisk =
        std::clamp(1.0f - laneDistance / std::max(interceptionRadius, EPSILON),
                   0.0f, 1.0f);
    const float laneWeight =
        P::NEAR_LANE_RISK +
        (P::BASE_INTERCEPTION_RISK + projection * P::LATE_LANE_RISK -
         P::NEAR_LANE_RISK) *
            ramp;
    combinedSafety *= 1.0f - individualRisk * laneWeight;
  }
  return std::clamp(1.0f - combinedSafety, 0.0f, 1.0f);
}

MatchEngine::PassOption MatchEngine::evaluatePassOptionBinturong(
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
      (receiver.position.x - passer.position.x) * direction;
  const float openness =
      std::clamp(nearestOpponentDistance(receiver) /
                     MatchTuning::Passing::OPENNESS_RADIUS_METRES,
                 0.0f, 1.0f);
  const float laneRisk = passingLaneRiskBinturong(passer, receiver);
  const float pressure =
      std::clamp((MatchTuning::Decision::PRESSURE_RADIUS_METRES -
                  nearestOpponentDistance(passer)) /
                     MatchTuning::Decision::PRESSURE_RADIUS_METRES,
                 0.0f, 1.0f);
  const PlayerRole role =
      receiver.player ? receiver.player->getRole() : PlayerRole::CM;
  const bool forwardRole = role == PlayerRole::ST || role == PlayerRole::LW ||
                           role == PlayerRole::RW || role == PlayerRole::CAM;
  const float passerDepth =
      passer.isHomeTeam ? passer.position.x : 1.0f - passer.position.x;
  const float receiverDepth =
      receiver.isHomeTeam ? receiver.position.x : 1.0f - receiver.position.x;
  const bool widePasser =
      passer.position.y <= MatchTuning::Passing::WIDE_ATTACK_MINIMUM_Y ||
      passer.position.y >= MatchTuning::Passing::WIDE_ATTACK_MAXIMUM_Y;
  const bool centralReceiver =
      receiver.position.y >= MatchTuning::Passing::CENTRAL_TARGET_MINIMUM_Y &&
      receiver.position.y <= MatchTuning::Passing::CENTRAL_TARGET_MAXIMUM_Y;
  const bool cutbackOption =
      widePasser && centralReceiver &&
      passerDepth >= MatchTuning::Passing::CUTBACK_MINIMUM_PASSER_DEPTH &&
      receiverDepth >= MatchTuning::Passing::CROSS_MINIMUM_RECEIVER_DEPTH &&
      progression >= MatchTuning::Passing::CUTBACK_MINIMUM_PROGRESSION &&
      progression <= MatchTuning::Passing::CUTBACK_MAXIMUM_PROGRESSION;
  const bool crossOption =
      !cutbackOption && widePasser && centralReceiver &&
      passerDepth >= MatchTuning::Passing::CROSS_MINIMUM_PASSER_DEPTH &&
      receiverDepth >= MatchTuning::Passing::CROSS_MINIMUM_RECEIVER_DEPTH;
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
    const float spaceBehind = (passer.isHomeTeam ? 1.0f - line : line) *
                              MatchTuning::Pitch::LENGTH_METRES;
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
  utility += passingRoleBiasBinturong(passer, receiver, progression,
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
  else if (std::abs(receiver.position.y - passer.position.y) >=
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
      toPitch({receiver.velocity.x * flightEstimate *
                   MatchTuning::Passing::RECEIVER_LEAD_SCALE,
               receiver.velocity.y * flightEstimate *
                   MatchTuning::Passing::RECEIVER_LEAD_SCALE});
  Vector2F target{std::clamp(receiver.position.x + lead.x, 0.0f, 1.0f),
                  std::clamp(receiver.position.y + lead.y, 0.0f, 1.0f)};
  if (intent == PassIntent::THROUGH_BALL)
  {
    target.x += (receiver.tacticalTarget.x - target.x) *
                MatchTuning::Passing::THROUGH_BALL_TARGET_BLEND;
    target.y += (receiver.tacticalTarget.y - target.y) *
                MatchTuning::Passing::THROUGH_BALL_TARGET_BLEND;
    target.x += direction * MatchTuning::Passing::THROUGH_BALL_FORWARD_LEAD;
    target = clampToPitch(target);
  }
  else if (intent == PassIntent::CROSS)
  {
    target.x += (receiver.tacticalTarget.x - target.x) *
                MatchTuning::Passing::CROSS_TARGET_BLEND;
    target.y += (receiver.tacticalTarget.y - target.y) *
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
    // passingRoleBiasBinturong(): the passer's daring, the receiver's target
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
              -tightMarkPenaltyBinturong(receiver));
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

std::optional<MatchEngine::PassOption> MatchEngine::choosePassTargetBinturong(
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
        (1.0f - passer.vision) * MatchTuning::Passing::OFFSIDE_PERCEPTION_ERROR;
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
    PassOption option = evaluatePassOptionBinturong(passer, candidate, detail);
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

void MatchEngine::afterPassReleasedBinturong(const MatchPlayer& passer,
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

MatchEngine::ShotAim MatchEngine::chooseShotAimBinturong(
    const MatchPlayer& shooter, bool header, bool penalty, bool freeKick)
{
  // Aim for the side away from the keeper, a little inside the post; better
  // finishers pick tighter spots.
  constexpr float HALF_GOAL = MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES;
  const MatchPlayer* keeper = findGoalkeeper(!shooter.isHomeTeam);
  const float keeperOffset =
      keeper ? (keeper->position.y - MatchTuning::Pitch::CENTRE) *
                   MatchTuning::Pitch::WIDTH_METRES
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

float MatchEngine::choosePenaltyGuessBinturong(const MatchPlayer& keeper)
{
  // A blind guess, before the kick (goalkeeper plan step 9): a side, or he
  // stays. Reading the taker (Anticipation) is for later.
  (void)keeper;
  float guess = randomFloat(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f;
  if (randomFloat(0.0f, 1.0f) < MatchTuning::Goalkeeper::PENALTY_STAY_CHANCE)
    guess = 0.0f;
  return guess;
}

Vector2F MatchEngine::goalkeeperTargetBinturong(MatchPlayer& keeper,
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
  const Vector2F goalCentre{goalX, MatchTuning::Pitch::CENTRE};
  const float ballDepth =
      keeper.isHomeTeam ? ball.position.x : 1.0f - ball.position.x;

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

  // A Binturong body's keeper goes for a high ball coming down in his area
  // when he meets it first (goalkeeper plan step 7): to where it comes
  // through his hands' reach. Otherwise he keeps his place.
  if (getBody(keeper.isHomeTeam) == BodyId::BINTURONG)
  {
    if (const auto& meet = keeperClaimMeet[keeper.isHomeTeam ? 0 : 1];
        meet && (!getPossessionAttachment() || hasTouchClaim()) && !getPossessionState().isShot)
    {
      setState(GoalkeeperState::CLAIM);
      return clampToPitch(*meet);
    }
  }
  // Claim a high delivery dropping into the own box.
  else if (!getPossessionAttachment() && getPossessionState().isAerialDelivery &&
           getPossessionState().passByHome != keeper.isHomeTeam)
  {
    // Anticipate high flight, then close on the ball itself once it drops
    // within handling height. Continuing to lead a low cross keeps the
    // keeper several metres ahead of it and prevents a physical catch.
    const float height = ball.z * MatchTuning::Units::BALL_Z_METRES;
    const float lookahead =
        height > MatchRules::goalkeeperReachMetres(keeper.heightMetres,
                                                   keeper.goalkeeping)
            ? MatchTuning::Goalkeeper::CLAIM_LOOKAHEAD_SECONDS
            : 0.0F;
    const Vector2F drift =
        toPitch({ball.velocity.x * lookahead, ball.velocity.y * lookahead});
    const Vector2F landing{ball.position.x + drift.x,
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
      ballDepth <
          MatchTuning::Pitch::GOALKEEPER_SWEEP_DEPTH *
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
          toPitch({ball.velocity.x * lookahead, ball.velocity.y * lookahead});
      return {ball.position.x + lead.x, ball.position.y + lead.y};
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
      return {goalCentre.x + (carrier->position.x - goalCentre.x) *
                                 MatchTuning::Goalkeeper::RUSH_CLOSING_SHARE,
              goalCentre.y + (carrier->position.y - goalCentre.y) *
                                 MatchTuning::Goalkeeper::RUSH_CLOSING_SHARE};
    }
  }

  // Set position on the ball-goal line, advancing to narrow the angle as the
  // threat approaches and acting as a sweeper when play is far away.
  setState(GoalkeeperState::SET_POSITION);
  const Vector2F threat = carrier ? carrier->position : ball.position;
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
  const float dxMetres =
      (threat.x - goalCentre.x) * MatchTuning::Pitch::LENGTH_METRES;
  const float dyMetres =
      (threat.y - goalCentre.y) * MatchTuning::Pitch::WIDTH_METRES;
  const float norm =
      std::max(std::sqrt(dxMetres * dxMetres + dyMetres * dyMetres), EPSILON);
  return {goalCentre.x +
              dxMetres / norm * depthMetres / MatchTuning::Pitch::LENGTH_METRES,
          std::clamp(goalCentre.y + dyMetres / norm * depthMetres /
                                        MatchTuning::Pitch::WIDTH_METRES,
                     MatchTuning::Pitch::GOALKEEPER_MIN_Y,
                     MatchTuning::Pitch::GOALKEEPER_MAX_Y)};
}

bool MatchEngine::choosesToBlockBinturong(const MatchPlayer& /*player*/)
{
  // Every outfielder in the path tries; whether he gets there is the body's.
  return true;
}

bool MatchEngine::choosesToPutBlockBehindBinturong(const MatchPlayer& /*player*/)
{
  // No second kick after a block (shot block plan step 4): the ball goes
  // where it comes off him.
  return false;
}

bool MatchEngine::choosesToClearCrossBehindBinturong(
    const MatchPlayer& /*player*/)
{
  // No instant kick behind on meeting a cross (shot block plan step 4): he
  // plays it with his touch like any other ball.
  return false;
}

Vector2F MatchEngine::chooseClearBehindTargetBinturong(
    const MatchPlayer& defender)
{
  // Over the own goal line, wide of the posts on his side.
  const float goalX = defender.isHomeTeam ? 0.0f : 1.0f;
  const float side =
      defender.position.y < MatchTuning::Pitch::CENTRE ? -1.0f : 1.0f;
  return {goalX + (defender.isHomeTeam ? -0.02f : 0.02f),
          MatchTuning::Pitch::CENTRE +
              side * randomFloat(MatchTuning::Pitch::GOAL_BOTTOM -
                                     MatchTuning::Pitch::CENTRE + 0.06f,
                                 0.45f)};
}

MatchEngine::HeaderPlay MatchEngine::chooseHeaderOutBinturong(
    const MatchPlayer& header) const
{
  // Forced to head it out of play, he looks for the safest way out rather
  // than a point: only the direction of each aim matters, so they lie
  // beyond the lines.
  using A = MatchTuning::Aerial;
  constexpr float BEYOND = 0.05f;
  const Vector2F from = ball.position;
  const float lateralMetres =
      (from.y - MatchTuning::Pitch::CENTRE) * MatchTuning::Pitch::WIDTH_METRES;
  const float sideY = lateralMetres < 0.0f ? -BEYOND : 1.0f + BEYOND;
  HeaderPlay play;
  play.kind = HeaderPlay::Kind::BEHIND;
  play.speed = A::HEADER_CLEARANCE_SPEED;
  play.lift = A::HEADER_OUT_LIFT;
  // Wide of the six-yard box: hard and high over the nearest touchline.
  if (std::abs(lateralMetres) > MatchTuning::Pitch::GOAL_AREA_HALF_WIDTH_METRES)
  {
    play.aim = {from.x, sideY};
    return play;
  }
  // In line with it: glanced on towards the corner on his side, or up and
  // over the bar, whichever is the easier header.
  const float goalX = header.isHomeTeam ? 0.0f : 1.0f;
  const float behindX = header.isHomeTeam ? -BEYOND : 1.0f + BEYOND;
  play.aim = {behindX, sideY};
  // Over the bar: as hard, steeply up, and only if that clears it.
  const float pace = std::hypot(play.speed, play.lift);
  const float overSpeed = pace * std::cos(A::HEADER_OVER_ELEVATION);
  const float overLift = pace * std::sin(A::HEADER_OVER_ELEVATION);
  const float lineMetres = std::max(
      std::abs(from.x - goalX) * MatchTuning::Pitch::LENGTH_METRES, 0.5f);
  if (loftVerticalSpeed(lineMetres, overSpeed,
                        ball.z * MatchTuning::Units::BALL_Z_METRES,
                        MatchTuning::Units::CROSSBAR_HEIGHT_METRES +
                            A::HEADER_OVER_BAR_MARGIN_METRES) > overLift)
    return play;
  const ContactProfile profile = strikeProfileBinturong(StrikeKind::HEADER);
  const auto difficulty = [&](Vector2F aim, float speed, float lift)
  {
    const ContactAssessment contact = assessStrikeBinturong(
        header, profile, metricDirection(from, aim), speed, lift);
    return contact.angleDifficulty + contact.weightDifficulty;
  };
  const Vector2F over{behindX, from.y};
  if (difficulty(over, overSpeed, overLift) <
      difficulty(play.aim, play.speed, play.lift))
  {
    play.aim = over;
    play.speed = overSpeed;
    play.lift = overLift;
  }
  return play;
}

MatchEngine::ClearanceAim MatchEngine::chooseClearanceAimBinturong(
    const MatchPlayer& defender)
{
  // A long, high clearance upfield and towards the nearer touchline.
  const float forward = defender.isHomeTeam ? 1.0f : -1.0f;
  const float touchline =
      defender.position.y < MatchTuning::Pitch::CENTRE ? -1.0f : 1.0f;
  ClearanceAim aim;
  aim.metres =
      randomFloat(MatchTuning::Defending::CLEARANCE_MIN_DISTANCE_METRES,
                  MatchTuning::Defending::CLEARANCE_MAX_DISTANCE_METRES);
  const Vector2F direction = normalized(
      {forward, touchline * randomFloat(0.0f, 1.0f) *
                    MatchTuning::Defending::CLEARANCE_TOUCHLINE_BIAS * 2.0f});
  const Vector2F reach =
      toPitch({direction.x * aim.metres, direction.y * aim.metres});
  aim.target = {defender.position.x + reach.x, defender.position.y + reach.y};
  return aim;
}

MatchEngine::HeaderAttempt MatchEngine::chooseHeaderAttemptBinturong(
    const MatchPlayer& header, bool fromTeammate)
{
  const float depth =
      header.isHomeTeam ? header.position.x : 1.0f - header.position.x;
  // A header at goal needs a real chance; from further out the ball is
  // nodded down for a team-mate instead.
  if (fromTeammate && depth >= MatchTuning::Aerial::HEADER_SHOT_MIN_DEPTH &&
      std::abs(header.position.y - MatchTuning::Pitch::CENTRE) <=
          MatchTuning::Aerial::HEADER_SHOT_WIDTH &&
      estimateShotXG(header) * MatchTuning::Shooting::HEADER_XG_FACTOR >=
          MatchTuning::Aerial::HEADER_SHOT_MIN_XG)
    return HeaderAttempt::SHOOT;
  // A team-mate's lofted ball away from goal is cushioned down and kept.
  if (fromTeammate && depth < MatchTuning::Aerial::HEADER_KNOCK_DOWN_MIN_DEPTH)
    return HeaderAttempt::CUSHION;
  return HeaderAttempt::PLAY;
}

MatchEngine::HeaderPlay MatchEngine::chooseHeaderPlayBinturong(
    const MatchPlayer& header, bool fromTeammate)
{
  const float attackDirection = header.isHomeTeam ? 1.0f : -1.0f;
  const float ownDepth =
      header.isHomeTeam ? 1.0f - header.position.x : header.position.x;
  HeaderPlay play;
  // Behind his own penalty spot a defender often plays safe and heads it
  // out of play.
  if (!fromTeammate &&
      ownDepth > 1.0f - MatchTuning::Pitch::LEFT_PENALTY_SPOT_X &&
      randomFloat(0.0f, 1.0f) < MatchTuning::SetPiece::HEADER_BEHIND_CHANCE)
    return chooseHeaderOutBinturong(header);
  if (!fromTeammate && ownDepth > 0.55f)
  {
    // Defensive header: clear the danger, upfield and away from the middle.
    const float wide =
        header.position.y < MatchTuning::Pitch::CENTRE ? -1.0f : 1.0f;
    const Vector2F direction =
        normalized({attackDirection * randomFloat(0.5f, 1.0f),
                    wide * randomFloat(0.1f, 0.8f)});
    const Vector2F reach = toPitch({direction.x, direction.y});
    play.kind = HeaderPlay::Kind::CLEAR;
    play.aim = {header.position.x + reach.x, header.position.y + reach.y};
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
    if ((other.position.x - header.position.x) * attackDirection *
            MatchTuning::Pitch::LENGTH_METRES <
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
                         : Vector2F{header.position.x + attackDirection * 0.1f,
                                    header.position.y};
  return play;
}

MatchPlayer* MatchEngine::bestSetPieceTakerBinturong(bool homeTeam,
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

MatchPlayer* MatchEngine::dutyTakerBinturong(bool homeTeam, SetPieceDuty duty)
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
      return bestSetPieceTakerBinturong(homeTeam, true);
    case SetPieceDuty::CornersLeft:
    case SetPieceDuty::CornersRight:
      return bestSetPieceTakerBinturong(homeTeam, false);
    default:
      return nullptr;
  }
}

MatchEngine::SetPieceAttackers MatchEngine::arrangeSetPieceAttackBinturong(
    bool attackingHome, Vector2F ballPosition)
{
  // Box targets for the attacking side, in metres from the goal line and
  // from the centre of the goal (positive towards the ball's side).
  const float goalX = attackingHome ? 1.0f : 0.0f;
  const float inward = attackingHome ? -1.0f : 1.0f;
  const float ballSide =
      ballPosition.y < MatchTuning::Pitch::CENTRE ? -1.0f : 1.0f;
  const auto spot = [&](float depthMetres, float lateralMetres)
  {
    return Vector2F{
        goalX + inward * depthMetres / MatchTuning::Pitch::LENGTH_METRES,
        MatchTuning::Pitch::CENTRE +
            ballSide * lateralMetres / MatchTuning::Pitch::WIDTH_METRES};
  };
  const std::array<Vector2F, 5> attackSlots = {
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

void MatchEngine::arrangeSetPieceDefenceBinturong(
    bool defendingHome, Vector2F ballPosition,
    const SetPieceAttackers& attackers)
{
  // Spots measured from the defended goal, as the attackers' are.
  const float goalX = defendingHome ? 0.0f : 1.0f;
  const float inward = defendingHome ? 1.0f : -1.0f;
  const float ballSide =
      ballPosition.y < MatchTuning::Pitch::CENTRE ? -1.0f : 1.0f;
  const auto spot = [&](float depthMetres, float lateralMetres)
  {
    return Vector2F{
        goalX + inward * depthMetres / MatchTuning::Pitch::LENGTH_METRES,
        MatchTuning::Pitch::CENTRE +
            ballSide * lateralMetres / MatchTuning::Pitch::WIDTH_METRES};
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
    const Vector2F mark = attackers.players[index]->movementTarget;
    defenders[next]->movementTarget = {
        mark.x - inward * MatchTuning::SetPiece::MARKING_GOAL_SIDE_OFFSET,
        mark.y};
  }
  const std::array<Vector2F, 3> zones = {spot(3.0f, 2.5f), spot(6.0f, 0.0f),
                                         spot(16.0f, 0.0f)};
  for (std::size_t zone = 0; zone < zones.size() && next < defenderCount;
       ++zone, ++next)
  {
    defenders[next]->movementTarget = zones[zone];
  }
}

MatchPlayer* MatchEngine::chooseRestartTakerBinturong(MatchState restart,
                                                      bool homeTeam,
                                                      Vector2F spot,
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
      return dutyTakerBinturong(homeTeam, SetPieceDuty::Penalties);
    case MatchState::CORNER_KICK:
      if (MatchPlayer* taker = dutyTakerBinturong(
              homeTeam, topCorner ? SetPieceDuty::CornersLeft
                                  : SetPieceDuty::CornersRight))
        return taker;
      return findClosestPlayer(spot, homeTeam, false);
    case MatchState::FREE_KICK:
      if (restartIsSetPiece)
        if (MatchPlayer* taker =
                dutyTakerBinturong(homeTeam, SetPieceDuty::FreeKicks))
          return taker;
      return findClosestPlayer(spot, homeTeam, false);
    case MatchState::THROW_IN:
    {
      // A designated long-throw specialist takes the throws in the attacking
      // third; elsewhere (and without one) the nearest player does.
      const float attackingDepth = homeTeam ? spot.x : 1.0f - spot.x;
      if (attackingDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START)
        if (MatchPlayer* taker =
                dutyTakerBinturong(homeTeam, SetPieceDuty::LongThrows))
          return taker;
      return findClosestPlayer(spot, homeTeam, false);
    }
    default:
      return findClosestPlayer(spot, homeTeam, false);
  }
}

MatchEngine::RestartPlay MatchEngine::chooseRestartPlayBinturong(
    MatchPlayer& taker, MatchState restartState)
{
  if (restartState == MatchState::CORNER_KICK)
    return chooseCornerPlayBinturong(taker);
  if (restartState == MatchState::FREE_KICK && restartIsSetPiece)
  {
    const Vector2F goal{taker.isHomeTeam ? 1.0f : 0.0f,
                        MatchTuning::Pitch::CENTRE};
    const float goalMetres = distance(ball.position, goal);
    const float lateralMetres =
        std::abs(ball.position.y - MatchTuning::Pitch::CENTRE) *
        MatchTuning::Pitch::WIDTH_METRES;
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
    if (wide) return chooseCornerPlayBinturong(taker);
  }

  std::optional<PassOption> passOption = choosePassTargetBinturong(taker);
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
    if (outlet) passOption = evaluatePassOptionBinturong(taker, *outlet);
  }
  if (passOption)
    return {RestartPlay::Kind::PASS, passOption,
            restartState == MatchState::GOAL_KICK};
  return {RestartPlay::Kind::HOLD, std::nullopt, false};
}

MatchEngine::RestartPlay MatchEngine::chooseCornerPlayBinturong(
    MatchPlayer& taker)
{
  // Routine: short, near post, far post or penalty spot, delivered at head
  // height to the attacker assigned to that zone.
  MatchPlayer* target = nullptr;
  const float roll = randomFloat(0.0f, 1.0f);
  if (roll < MatchTuning::SetPiece::SHORT_CORNER_CHANCE)
  {
    std::optional<PassOption> shortOption = choosePassTargetBinturong(taker);
    if (shortOption &&
        shortOption->passDistance <
            MatchTuning::SetPiece::SHORT_CORNER_MAX_DISTANCE_METRES)
    {
      PassOption groundPass = *shortOption;
      groundPass.lofted = false;
      return {RestartPlay::Kind::PASS, groundPass, false};
    }
  }
  const Vector2F goal{taker.isHomeTeam ? 1.0f : 0.0f,
                      MatchTuning::Pitch::CENTRE};
  float bestScore = -std::numeric_limits<float>::infinity();
  const float nearPostShare = MatchTuning::SetPiece::NEAR_POST_CHANCE;
  const bool nearPost =
      roll < MatchTuning::SetPiece::SHORT_CORNER_CHANCE + nearPostShare;
  for (auto& candidate : players)
  {
    if (!active(candidate) || &candidate == &taker ||
        candidate.isHomeTeam != taker.isHomeTeam || candidate.isGoalkeeper)
      continue;
    if (!inPenaltyArea(candidate.tacticalTarget, !taker.isHomeTeam)) continue;
    const float targetMetres = distance(candidate.tacticalTarget, goal);
    const float sideMetres =
        (candidate.tacticalTarget.y - MatchTuning::Pitch::CENTRE) *
        (ball.position.y < MatchTuning::Pitch::CENTRE ? -1.0f : 1.0f) *
        MatchTuning::Pitch::WIDTH_METRES;
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
    std::optional<PassOption> fallback = choosePassTargetBinturong(taker);
    if (fallback) return {RestartPlay::Kind::PASS, fallback, true};
    return {RestartPlay::Kind::HOLD, std::nullopt, false};
  }
  PassOption delivery = evaluatePassOptionBinturong(taker, *target);
  delivery.targetPoint = target->tacticalTarget;
  delivery.passDistance = distance(taker.position, delivery.targetPoint);
  delivery.lofted = true;
  delivery.intent = PassIntent::CROSS;
  return {RestartPlay::Kind::PASS, delivery, true};
}

MatchPlayer* MatchEngine::chooseTakeOnBinturong(MatchPlayer& carrier)
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

float MatchEngine::carryUrgencyBinturong(const MatchPlayer& player) const
{
  // How urgently he carries it: open space along his way, a break on (an
  // attacking transition) and a side that takes risks push him on; with
  // nobody near he may stroll it out, with someone in front he slows to
  // keep it. A drill's dribbler goes as hard as the drill asks.
  using R = MatchTuning::Dribble;
  if (player.player)
    for (const auto& [keeper, urgency] : ballKeeperUrgency)
      if (keeper == player.player->getId()) return std::clamp(urgency, 0.0f, 1.0f);
  const Vector2F at = toMetres(player.position);
  const Vector2F way = toMetres({ball.dribbleGoal.x - player.position.x,
                                 ball.dribbleGoal.y - player.position.y});
  const float wayMetres = length(way);
  float nearest = R::CARRY_SPACE_OPEN_METRES;
  for (const MatchPlayer& opponent : players)
  {
    if (!active(opponent) || opponent.isHomeTeam == player.isHomeTeam) continue;
    const Vector2F toThem = {toMetres(opponent.position).x - at.x,
                             toMetres(opponent.position).y - at.y};
    const float metres = length(toThem);
    if (wayMetres > EPSILON && metres > EPSILON &&
        (toThem.x * way.x + toThem.y * way.y) <
            R::CARRY_SPACE_CONE_COSINE * metres * wayMetres)
      continue;
    nearest = std::min(nearest, metres);
  }
  const float space =
      std::clamp((nearest - R::CARRY_SPACE_CLOSE_METRES) /
                     (R::CARRY_SPACE_OPEN_METRES - R::CARRY_SPACE_CLOSE_METRES),
                 0.0f, 1.0f);
  const TeamPhase phase = player.isHomeTeam ? homePhase : awayPhase;
  const StrategySliders sliders = getEffectiveSliders(player.isHomeTeam);
  return std::clamp(
      R::CARRY_URGENCY_BASE + R::CARRY_URGENCY_SPACE * space +
          (phase == TeamPhase::ATTACKING_TRANSITION ? R::CARRY_URGENCY_TRANSITION
                                                    : 0.0f) +
          R::CARRY_URGENCY_RISK * (sliders.riskTaking - 0.5f) * 2.0f,
      0.0f, 1.0f);
}

float MatchEngine::carryPaceBinturong(const MatchPlayer& player) const
{
  // Walk at urgency 0, jog at 1/3, run at 2/3, sprint at 1 (the tops of his
  // bands, as fuel has left them), in straight lines between.
  const float urgency = carryUrgencyBinturong(player);
  const std::array<float, 4> tops{speedBand(player, MovementMode::WALK).high,
                                  speedBand(player, MovementMode::JOG).high,
                                  speedBand(player, MovementMode::RUN).high,
                                  currentTopSpeed(player)};
  const float scaled = urgency * 3.0f;
  const auto band = std::min(static_cast<std::size_t>(scaled), std::size_t{2});
  const float pace =
      tops[band] + (tops[band + 1] - tops[band]) * (scaled - static_cast<float>(band));
  // Dribbling caps how much of his pace he can use with the ball: his run
  // band's top with none, his top speed with the best.
  const float cap = tops[2] + std::max(0.0f, tops[3] - tops[2]) *
                                  std::clamp(player.dribbling, 0.0f, 1.0f);
  return std::min(pace, cap);
}

float MatchEngine::chooseTouchSecondsBinturong(const MatchPlayer& player,
                                               float pace, float toEnd) const
{
  // How long until he wants the ball again (dribble meet plan 3c): his
  // intent, not his ability. In open space, longer the more urgently he
  // carries it: about every stride at a walk, every two or three at a jog,
  // knocked on into space at a sprint. Closed down, as soon as he can play
  // it again, easing back to that as the nearest opponent is further off.
  // A turn on his route or the end of his run coming up within the push: as
  // far as there, to turn or stop it.
  using R = MatchTuning::Dribble;
  const float soonest = dribbleTouchIntervalBinturong(player);
  const float urgency = carryUrgencyBinturong(player);
  constexpr float JOG = 1.0f / 3.0f;
  const float open =
      urgency <= JOG
          ? soonest + (R::PUSH_JOG_SECONDS - soonest) * urgency / JOG
          : R::PUSH_JOG_SECONDS + (R::PUSH_SPRINT_SECONDS - R::PUSH_JOG_SECONDS) *
                                      (urgency - JOG) / (1.0f - JOG);
  const float closeness = std::clamp(
      (R::PUSH_CLOSE_FAR_METRES - nearestOpponentDistance(player)) /
          (R::PUSH_CLOSE_FAR_METRES - R::PUSH_CLOSE_NEAR_METRES),
      0.0f, 1.0f);
  const float wanted = std::max(soonest, open + (soonest - open) * closeness);
  // A turn or the end coming within it: as far as there, to play it again
  // where he turns or stops it.
  if (toEnd < pace * wanted)
    return std::max(soonest, toEnd / std::max(pace, R::LOOSE_START_SPEED));
  return wanted;
}

float MatchEngine::pushMetresBinturong(const MatchPlayer& player, Vector2F from,
                                       Vector2F direction, float startGap,
                                       float pace, float seconds,
                                       std::optional<float> toGoal) const
{
  // As far as he runs at `pace` in `seconds` (from `startGap` behind it),
  // so he gets to it about then; never past where he is going, with room
  // for it to roll on past the spot; and no further than he still gets to
  // it first: an opponent who could reach a point of it (after reading
  // it) no later than he does, less a margin, holds it short of there.
  using R = MatchTuning::Dribble;
  const float shortest = touchKnobs[TouchKnob::SET_OFFSET];
  float longest = std::min(R::PUSH_MAX_METRES, pace * seconds - startGap);
  if (toGoal)
    longest = std::min(longest, *toGoal - pace * R::PUSH_ROLL_ON_SECONDS);
  if (longest <= shortest) return shortest;
  const float startSpeed = std::max(
      0.0f, player.velocity.x * direction.x + player.velocity.y * direction.y);
  const float run = std::max(pace, R::LOOSE_START_SPEED);
  // How long his run to a point `metres` beyond the contact takes, speeding
  // up toward his pace as his legs allow.
  const auto hisSeconds = [&](float metres)
  {
    const float distanceRun = metres + startGap;
    float time = distanceRun / run;
    for (int pass = 0; pass < 2; ++pass)
      time = distanceRun /
             std::max(dribbleReachablePaceBinturong(player, startSpeed, run, time),
                      EPSILON);
    return time;
  };
  const Vector2F start = toMetres(from);
  constexpr float STEP_METRES = 0.5f;
  for (float along = shortest; along <= longest + EPSILON; along += STEP_METRES)
  {
    const Vector2F point{start.x + direction.x * along,
                         start.y + direction.y * along};
    const Vector2F pitch = toPitch(point);
    if (pitch.x < 0.01f || pitch.x > 0.99f || pitch.y < 0.01f || pitch.y > 0.99f)
      return std::max(shortest, along - STEP_METRES);
    const float his = hisSeconds(along);
    for (const MatchPlayer& opponent : players)
    {
      if (!active(opponent) || opponent.isHomeTeam == player.isHomeTeam) continue;
      const float reach = opponent.isGoalkeeper
                              ? MatchTuning::Ball::GOALKEEPER_CONTROL_RADIUS_METRES
                              : feetReach(opponent);
      // His run there, after reading it: from his speed toward it now,
      // speeding up as his legs allow.
      const Vector2F at = toMetres(opponent.position);
      const Vector2F toPoint{point.x - at.x, point.y - at.y};
      const float apart = length(toPoint);
      const float gap = std::max(0.0f, apart - reach);
      const float top = std::max(currentTopSpeed(opponent), EPSILON);
      if (gap / top + R::PUSH_READ_SECONDS > his + R::PUSH_MARGIN_SECONDS)
        continue;
      const float theirStart =
          apart > EPSILON ? std::max(0.0f, (opponent.velocity.x * toPoint.x +
                                            opponent.velocity.y * toPoint.y) /
                                               apart)
                          : 0.0f;
      float theirRun = gap / top;
      for (int pass = 0; pass < 2; ++pass)
        theirRun = gap / std::max(dribbleReachablePaceBinturong(
                                      opponent, theirStart, top, theirRun),
                                  EPSILON);
      if (theirRun + R::PUSH_READ_SECONDS <= his + R::PUSH_MARGIN_SECONDS)
        return std::max(shortest, along - STEP_METRES);
    }
  }
  return longest;
}

float MatchEngine::chooseGetUpBinturong(const MatchPlayer& player)
{
  // How quick to get up is his call, by urgency (collision plan step 5).
  using C = MatchTuning::Collision;
  const float agility = player.derivedStat(DerivedStat::AGILITY);
  const float gingerly = alongStatScale(agility, C::GINGERLY_SECONDS_AT_AGILITY_1,
                                        C::GINGERLY_SECONDS_AT_AGILITY_99);
  // Play has stopped: up, ready for the restart.
  if (state != MatchState::PLAYING)
  {
    ++stats.collisionGetUpsInStoppage;
    return gingerly;
  }
  // The ball near and still to be won (loose, his side's, or an opponent's
  // with a team-mate of his on it): he jumps up.
  if (distance(player.position, ball.position) <= C::GET_UP_BALL_METRES)
  {
    const MatchPlayer* holder = findMatchPlayer(getPossessionAttachment());
    bool chance = holder == nullptr || holder->isHomeTeam == player.isHomeTeam;
    if (holder == nullptr && getPossessionState().isPass) chance = getPossessionState().passByHome == player.isHomeTeam;
    if (!chance)
      for (const MatchPlayer& mate : players)
        if (active(mate) && &mate != &player && mate.isHomeTeam == player.isHomeTeam &&
            !mate.fallen &&
            distance(mate.position, ball.position) <=
                C::CONTESTED_METRES)
        {
          chance = true;
          break;
        }
    if (chance)
    {
      ++stats.collisionGetUpsForBall;
      return alongStatScale(agility, C::GET_UP_SECONDS_AT_AGILITY_1,
                            C::GET_UP_SECONDS_AT_AGILITY_99);
    }
  }
  // Play has run on a long while with him down: up, gingerly.
  if (player.downSeconds >= C::GINGERLY_AFTER_SECONDS)
  {
    ++stats.collisionGetUpsGingerly;
    return gingerly;
  }
  return 0.0f;
}
