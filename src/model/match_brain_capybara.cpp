// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Capybara brain: generation C, the brain redesign (Notes and plans/
// match_engine_brain_redesign_plan.md). It started as an exact copy of the
// Binturong brain (match_brain_binturong.cpp), which is frozen from then on.
// Every function here is a MatchEngine member with the "Capybara" suffix,
// declared in match_brain_capybara_members.h; the engine reaches them through
// the dispatchers in match_brain_dispatch.cpp.
//
// Change behaviour freely here. A brain-only helper this file calls has its
// own Capybara copy; a helper shared with the body (match_engine.cpp, or a
// body's own helpers such as strikeProfileBinturong) is shared by every brain,
// so copy it into this brain (with the Capybara suffix) before changing it. To
// keep this version for comparison, freeze it and copy it to a new version the
// same way this one was made.

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
#include "model/match_brain_flow.h"
#include "model/match_contact_binturong.h"
#include "model/match_decision_frame.h"
#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/match_game_xg.h"
#include "model/match_possession_coordination.h"
#include "model/match_recorder.h"
#include "model/player.h"
#include "model/role_utils.h"
#include "model/world_rng.h"

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
constexpr float OVERTAKE_HORIZON_SECONDS = 3.0F;
constexpr float OVERTAKE_SAME_TARGET_METRES = 2.0F;
constexpr float OVERTAKE_SAME_DIRECTION_COSINE = 0.7071F;
constexpr float OVERTAKE_STICKY_METRES = 0.5F;
constexpr float OVERTAKE_MINIMUM_SPEED = 1.0F;
// A way round (way-round plan): a loose-ball chaser steers round anyone not
// chasing it whose predicted closest approach is under the overtake's
// clearances, looking this far ahead (or to the ball, if sooner), never in
// the last metres before he meets it; his waypoint a margin outside the
// clearance (his curve cuts a few centimetres inside); a side kept unless
// the other is this much shorter (decisions 1-7, agreed 2026-10-30).
constexpr float WAY_ROUND_HORIZON_SECONDS = 3.0F;
constexpr float WAY_ROUND_FINAL_METRES = 2.0F;
constexpr float WAY_ROUND_MARGIN_METRES = 0.1F;
constexpr float WAY_ROUND_STICKY_METRES = 0.5F;
// Giving way (decisions 1-2, agreed 2026-10-30): a man moving at least this
// fast (m/s) is running a line (slower: standing, gone round as before);
// paths within this cosine of square to each other cross (between 45 and
// 135 degrees); below the moving speed, giving way is a stop.
constexpr float GIVE_WAY_MOVING_SPEED = 0.5F;
constexpr float GIVE_WAY_CROSSING_COSINE = 0.7071F;
// Giving way the same way with no room to go round: this much under the
// speed the man ahead is going his way (m/s).
constexpr float GIVE_WAY_UNDER_LEADER_SPEED = 0.5F;
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

const MatchEngine::BrainTraits& MatchEngine::brainTraitsCapybara() const
{
  // Waits for a ball, decides ahead of his meet with it, pushes it by
  // intent and marks its loose-ball chasers.
  static constexpr BrainTraits traits{.waitsForTouches = true,
                                      .decidesForMeet = true,
                                      .pushesByIntent = true,
                                      .flagsLooseBallChasers = true};
  return traits;
}

const MatchEngine::MovementStyle& MatchEngine::movementStyleCapybara()
{
  using P = MatchTuning::Player;
  static const MovementStyle style = []
  {
    MovementStyle built;
    // No target lag: players head for the latest plan at once.
    built.smoothsTargets = false;
    for (std::size_t intent = 0; intent < PLAYER_INTENT_COUNT; ++intent)
    {
      built.intentSpeedShares[intent] =
          movementSpeedScale(static_cast<PlayerIntent>(intent));
    }
    built.restartWalkShare = P::RESTART_WALK_SPEED_SCALE;
    built.urgencyDistanceMetres = P::URGENCY_DISTANCE_METRES;
    built.maxUrgencyShare = P::MAX_URGENCY_SPEED_SCALE;
    // Movement modes (fatigue plan 4.2, step 6b): each job a mode, run at
    // the top of its band. The shares above stay for an Aardvark body. How
    // hard to run against a deadline, and the old pacing (a tired player
    // easing off, saving the burst reserve) and the touchline's lift, are
    // left to the brain work to come.
    built.usesModes = true;
    built.fatigueWorkRateLoss = 0.0F;
    const auto mode = [&built](PlayerIntent intent, MovementMode chosen)
    { built.intentModes[static_cast<std::size_t>(intent)] = chosen; };
    using M = MovementMode;
    using I = PlayerIntent;
    for (const I intent :
         {I::HOLD_SHAPE, I::OFFER_SUPPORT, I::BLOCK_PASSING_LANE, I::GOALKEEP})
    {
      mode(intent, M::JOG);
    }
    for (const I intent : {I::CARRY_BALL, I::MARK_OPPONENT, I::COVER_PRESS,
                           I::RECOVER_SHAPE, I::RECEIVE_PASS})
    {
      mode(intent, M::RUN);
    }
    for (const I intent : {I::PRESS_BALL, I::CLAIM_LOOSE_BALL, I::RUN_IN_BEHIND,
                           I::ATTACK_BOX, I::OVERLAP})
    {
      mode(intent, M::SPRINT);
    }
    // A jog job walks within 5 m of its target, jogs from 10 m, runs from
    // 25 m (a ramp, so a moving target never flips him between modes).
    built.jogRampMetres = {5.0F, 10.0F, 20.0F, 25.0F};
    return built;
  }();
  return style;
}

void MatchEngine::applyTeamMentalityCapybara(StrategySliders& sliders,
                                             bool homeTeam) const
{
  // Score effects: a side in front sits deeper and takes fewer risks, a side
  // behind pushes on, more so as the match runs out.
  using T = MatchTuning::Touchline;
  const int lead =
      std::clamp(homeTeam ? homeScore - awayScore : awayScore - homeScore,
                 -T::SCORE_EFFECT_MAX_GOALS, T::SCORE_EFFECT_MAX_GOALS);
  const auto clampSlider = [](float& value)
  { value = std::clamp(value, 0.0F, 1.0F); };
  if (lead != 0)
  {
    const float urgency =
        static_cast<float>(lead) *
        (T::SCORE_EFFECT_BASE +
         (1.0F - T::SCORE_EFFECT_BASE) *
             std::min(1.0F,
                      matchTimeMinutes /
                          (2.0F * MatchTuning::Timing::HALF_TIME_MINUTE)));
    // Only a side chasing the game commits more men forward; one in front
    // keeps its passing but stops pressing and taking risks.
    if (lead < 0)
    {
      sliders.offensiveBias -= urgency * T::SCORE_EFFECT_OFFENSIVE;
    }
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
  if (const float underdog = underdogShares[homeTeam ? 0 : 1]; underdog > 0.0F)
  {
    sliders.pressing -= underdog * T::UNDERDOG_PRESSING;
    sliders.compactness += underdog * T::UNDERDOG_COMPACTNESS;
    sliders.offensiveBias -= underdog * T::UNDERDOG_OFFENSIVE;
    clampSlider(sliders.pressing);
    clampSlider(sliders.offensiveBias);
    clampSlider(sliders.compactness);
  }
}

float MatchEngine::shoutShotStepsCapybara(bool homeTeam) const
{
  const MatchShout shout = shouts[homeTeam ? 0 : 1].shout;
  const float bias = shout == MatchShout::SHOOT_ON_SIGHT       ? 1.0F
                     : shout == MatchShout::WORK_BALL_INTO_BOX ? -1.0F
                                                               : 0.0F;
  return bias * DecisionFrame::SHOUT_MAX_STEPS * getShoutStrength(homeTeam);
}

float MatchEngine::roleStepsCapybara(float profile)
{
  // Role profiles (tactics.cpp) run to about +-0.25 after the duty.
  constexpr float LARGEST_PROFILE = 0.25F;
  return std::clamp(profile * DecisionFrame::LAYER_MAX_STEPS / LARGEST_PROFILE,
                    -DecisionFrame::LAYER_MAX_STEPS,
                    DecisionFrame::LAYER_MAX_STEPS);
}

namespace
{
/** "Our ball, a team-mate has it" (step O1): his off-ball options, in
 * the stopgap order (today's). */
enum class OurBallJob : std::uint8_t
{
  RUN_IN_BEHIND,
  BOX_ARRIVAL,
  OVERLAP,
  SUPPORT,
  FORWARD_SHORT,
  HOLD_SHAPE,
  COUNT
};
constexpr std::size_t OUR_BALL_JOB_COUNT =
    static_cast<std::size_t>(OurBallJob::COUNT);
// The brain flow tool's words for each frame's options (step O5).
constexpr std::array<const char*, OUR_BALL_JOB_COUNT> OUR_BALL_NAMES = {
    "run in behind", "attack the box", "overlap",
    "support",       "come short",     "hold shape"};
constexpr std::array<const char*, OUR_BALL_JOB_COUNT> OUR_BALL_WHEN = {
    "the team made him a runner",
    "the team picked him to arrive late or attack the far post",
    "the team picked him to overlap",
    "the team made him a supporter",
    "he is a forward (ST, LW, RW, CAM)",
    "always"};
/** "Opponents' ball" (step O2): his off-ball options, in the stopgap
 * order (today's). */
enum class TheirBallJob : std::uint8_t
{
  PRESS,
  DOUBLE_UP,
  BLOCK_LANE,
  COVER,
  TIGHT_MARK,
  TRANSITION_RECOVER,
  ZONAL_MARK,
  HOLD_SHAPE,
  COUNT
};
constexpr std::size_t THEIR_BALL_JOB_COUNT =
    static_cast<std::size_t>(TheirBallJob::COUNT);
constexpr std::array<const char*, THEIR_BALL_JOB_COUNT> THEIR_BALL_NAMES = {
    "press",      "double up",  "block a lane",
    "cover",      "tight mark", "recover in transition",
    "zonal mark", "hold shape"};
constexpr std::array<const char*, THEIR_BALL_JOB_COUNT> THEIR_BALL_WHEN = {
    "the team made him the first presser",
    "the second presser, under a double-up order",
    "the second presser, pressing high enough, an outlet to cut off",
    "the second presser, pressing high enough, no outlet",
    "an order to mark a man tightly",
    "his side has just lost the ball",
    "the team gave him a man to mark",
    "always"};
/** Nobody has the ball (step O3): his off-ball options across its three
 * situations, in the stopgap order (today's). */
enum class LooseJob : std::uint8_t
{
  RECEIVE,
  FLIGHT_SUPPORT,
  CHASE,
  COVER_CHASE,
  TIGHT_MARK,
  TRANSITION_RECOVER,
  HOLD_SHAPE,
  TRANSITION_RUN,
  TRANSITION_SUPPORT,
  RECOVER,
  COUNT
};
constexpr std::size_t LOOSE_JOB_COUNT =
    static_cast<std::size_t>(LooseJob::COUNT);
constexpr std::array<const char*, LOOSE_JOB_COUNT> LOOSE_NAMES = {
    "receive it",         "support while it travels",
    "chase it",           "cover the chase",
    "tight mark",         "recover in transition",
    "hold shape",         "transition run",
    "transition support", "recover"};
constexpr std::array<const char*, LOOSE_JOB_COUNT> LOOSE_WHEN = {
    "our pass is meant for him",
    "our pass is travelling",
    "the team's first to the ball",
    "the team's second to it, pressing high enough",
    "their pass travelling, under a tight-mark order",
    "a pass or shot travelling, his side just lost the ball",
    "a pass or shot travelling",
    "ball loose, his side just won it, he is a runner",
    "ball loose, his side just won it",
    "always"};
/** "I'm on the ground" (step J): his options, in the stopgap order. */
enum class GetUp : std::uint8_t
{
  FOR_THE_RESTART,
  FOR_THE_BALL,
  GINGERLY,
  STAY_DOWN,
  COUNT
};
constexpr std::size_t GET_UP_COUNT = static_cast<std::size_t>(GetUp::COUNT);
/** "I'm the keeper" (step O4): his options, in the stopgap order. */
enum class KeeperJob : std::uint8_t
{
  SWEEP_OWN_GOAL,
  TAKE_PASS,
  POSITION,
  COUNT
};
constexpr std::size_t KEEPER_JOB_COUNT =
    static_cast<std::size_t>(KeeperJob::COUNT);
constexpr std::array<const char*, KEEPER_JOB_COUNT> KEEPER_NAMES = {
    "sweep a ball going into his own goal", "take a team-mate's pass",
    "take up his position"};
constexpr std::array<const char*, KEEPER_JOB_COUNT> KEEPER_WHEN = {
    "a ball heading in, once he has reacted", "our pass is meant for him",
    "always"};
}  // namespace

void MatchEngine::refreshTacticalTargetsCapybara(float dt, bool planHome)
{
  // Team planning happens in two passes: choose shared assignments (pressers,
  // runners, support and marks), then give each player a target and intent.
  // Bodies stay still until updateMovement integrates all targets, so player
  // iteration order cannot make later players see an already-moved opponent.
  // Add an off-ball behavior here; physical acceleration belongs downstream.
  const MatchPlayer* carrier = findMatchPlayer(getPossessionAttachment());
  const MatchPlayer* transitionSource =
      findMatchPlayer(getLastPossessionAttachment());
  // The players on the pitch of each side, in slot order (index 0 home).
  std::array<std::array<MatchPlayer*, 16>, 2> sides{};
  std::array<std::size_t, 2> sideSizes{};
  for (auto& player : players)
  {
    const std::size_t side = player.isHomeTeam ? 0 : 1;
    if (active(player) && sideSizes[side] < sides[side].size())
    {
      sides[side][sideSizes[side]++] = &player;
    }
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
  const PitchPoint pressurePosition =
      (carrier != nullptr) ? carrier->position : ball.position;
  // The team shape reads the play where it is heading: while a pass
  // travels, where it is going, so the block shifts once instead of
  // sliding with the ball. Pressing and loose balls still go to the ball.
  const PitchPoint shapeAnchor =
      (carrier == nullptr) && getPossessionState().isPass
          ? getPossessionState().passTarget
          : pressurePosition;
  const StrategySliders homeSliders = getEffectiveSliders(true);
  const StrategySliders awaySliders = getEffectiveSliders(false);

  // A loose ball's path, from the ball's own physics (flight, bounces,
  // rolling to a stop): where it will be each tick, and whether it is low
  // enough there to be played. Who gets to it first is who can meet it
  // first on this path (interception: walkers and catchers head for a
  // meeting point, not for where it is now; Fajen & Warren).
  using PathPoint = LoosePathPointCapybara;
  std::array<PathPoint, LOOSE_BALL_PATH_TICKS + 1> loosePath{};
  std::size_t loosePoints = 0;
  if (carrier == nullptr)
  {
    const float speed = length(ball.velocity);
    const Vector2F heading = speed > EPSILON
                                 ? Vector2F{.x = ball.velocity.x / speed,
                                            .y = ball.velocity.y / speed}
                                 : Vector2F{.x = 0.0F, .y = 0.0F};
    FlightState flight{.horizontalSpeed = speed,
                       .verticalSpeed = ball.velocityZ,
                       .height = ball.z,
                       .bounced = false,
                       .spin = ball.spin};
    Vector2F at = asVector(ball.position);
    constexpr int SUBSTEPS = MatchTuning::Timing::BALL_SUBSTEPS;
    constexpr float TICK = MatchTuning::Timing::FIXED_STEP_SECONDS;
    loosePath[loosePoints++] = {
        .seconds = 0.0F, .at = at, .height = flight.height};
    for (int tick = 1; tick <= LOOSE_BALL_PATH_TICKS; ++tick)
    {
      for (int sub = 0; sub < SUBSTEPS; ++sub)
      {
        const float moved = stepFlight(flight, TICK / SUBSTEPS);
        at = {.x = at.x + (heading.x * moved), .y = at.y + (heading.y * moved)};
      }
      loosePath[loosePoints++] = {.seconds = static_cast<float>(tick) * TICK,
                                  .at = at,
                                  .height = flight.height};
      // At rest: the rest of the path is this spot.
      if (flight.horizontalSpeed <= 0.0F && flight.height <= 0.0F &&
          flight.verticalSpeed <= 0.0F)
      {
        break;
      }
    }
  }
  // When and where a candidate can first meet the loose ball on its path
  // (within his reach of it), at his pursuit speed toward each point; at
  // its resting spot if he cannot catch it up.
  const auto interception = [&](const MatchPlayer& candidate)
  {
    const Vector2F from = asVector(candidate.position);
    // Below his head line it is for his feet, thigh or chest.
    const float headLine = bodyLines(candidate).head;
    float seconds = 0.0F;
    Vector2F meet = loosePath[0].at;
    for (std::size_t index = 0; index < loosePoints; ++index)
    {
      const PathPoint& point = loosePath[index];
      const Vector2F toPoint{.x = point.at.x - from.x,
                             .y = point.at.y - from.y};
      const float metres = length(toPoint);
      const float toward =
          metres > EPSILON ? std::max(0.0F, (candidate.velocity.x * toPoint.x +
                                             candidate.velocity.y * toPoint.y) /
                                                metres)
                           : 0.0F;
      const float pursuit = std::max(
          currentTopSpeed(candidate) +
              (toward * MatchTuning::Player::CURRENT_VELOCITY_PURSUIT_WEIGHT),
          MatchTuning::Player::MINIMUM_PURSUIT_SPEED);
      const float reach =
          std::max(0.0F,
                   metres - MatchTuning::Ball::OUTFIELD_CONTROL_RADIUS_METRES) /
          pursuit;
      seconds = std::max(point.seconds, reach);
      meet = point.at;
      if (point.height <= headLine && reach <= point.seconds)
      {
        break;
      }
    }
    return std::pair{seconds, asPoint(meet)};
  };

  const auto estimatedArrivalTime = [&](const MatchPlayer& candidate)
  {
    if (!carrier && loosePoints > 0)
    {
      // A loose ball: when he can meet it, the one already after it a tick
      // ahead (so a near tie does not swap who goes, step to step).
      float arrival = interception(candidate).first;
      if (possessionId == PossessionId::AARDVARK &&
          candidate.intent == PlayerIntent::CLAIM_LOOSE_BALL)
      {
        arrival = std::max(0.0F, arrival - LOOSE_BALL_CONTINUITY_SECONDS);
      }
      return arrival;
    }
    const Vector2F toTarget = pressurePosition - candidate.position;
    const float targetDistance = length(toTarget);
    if (targetDistance <= EPSILON)
    {
      return 0.0F;
    }
    const Vector2F pursuitDirection{.x = toTarget.x / targetDistance,
                                    .y = toTarget.y / targetDistance};
    const float velocityTowardTarget =
        std::max(0.0F, (candidate.velocity.x * pursuitDirection.x) +
                           (candidate.velocity.y * pursuitDirection.y));
    const float pursuitSpeed =
        std::max(currentTopSpeed(candidate) +
                     (velocityTowardTarget *
                      MatchTuning::Player::CURRENT_VELOCITY_PURSUIT_WEIGHT),
                 MatchTuning::Player::MINIMUM_PURSUIT_SPEED);
    float arrivalTime =
        std::max(0.0F, (targetDistance / pursuitSpeed) -
                           pressEagernessCapybara(candidate, carrier));
    if (candidate.intent == PlayerIntent::PRESS_BALL ||
        candidate.intent == PlayerIntent::CLAIM_LOOSE_BALL)
    {
      arrivalTime = std::max(
          0.0F, arrivalTime - MatchTuning::Shape::PRESSER_CONTINUITY_SECONDS);
    }
    return arrivalTime;
  };

  const auto collectionCandidate = [&](const MatchPlayer& p, float arrival)
  {
    float space = 8.0F;
    for (const MatchPlayer* rival : onPitch(!p.isHomeTeam))
    {
      space = std::min(space, distance(p.position, rival->position));
    }
    return PossessionCoordination::Candidate{
        .arrivalSeconds = arrival,
        .collectionQuality = PossessionCoordination::quality(
            p.velocity, ball.velocity, p.isHomeTeam, space),
        .intendedReceiver = getPossessionState().isPass &&
                            getPossessionState().passByHome == p.isHomeTeam &&
                            getExpectedReceiver() == p.player,
        .id = p.player ? p.player->getId() : 0};
  };
  const auto closestOutfieldPair = [&](bool homeTeam)
  {
    std::array<MatchPlayer*, 2> closest{nullptr, nullptr};
    std::array<float, 2> arrivalTimes{std::numeric_limits<float>::max(),
                                      std::numeric_limits<float>::max()};
    for (MatchPlayer* const pointer : onPitch(homeTeam))
    {
      MatchPlayer& candidate = *pointer;
      if (candidate.isGoalkeeper)
      {
        continue;
      }
      const float arrivalTime = estimatedArrivalTime(candidate);
      const auto preferred = [&](std::size_t place)
      {
        if (!closest[place])
        {
          return true;
        }
        if (possessionId == PossessionId::BINTURONG && !carrier)
        {
          return PossessionCoordination::better(
              collectionCandidate(candidate, arrivalTime),
              collectionCandidate(*closest[place], arrivalTimes[place]));
        }
        return arrivalTime < arrivalTimes[place];
      };
      if (preferred(0))
      {
        closest[1] = closest[0];
        arrivalTimes[1] = arrivalTimes[0];
        closest[0] = &candidate;
        arrivalTimes[0] = arrivalTime;
      }
      else if (preferred(1))
      {
        closest[1] = &candidate;
        arrivalTimes[1] = arrivalTime;
      }
    }

    // A purposeful pass creates a receiving run. Prioritizing that receiver
    // prevents a merely nearby teammate from making the play look arbitrary.
    if (possessionId == PossessionId::AARDVARK && !carrier &&
        getPossessionState().isPass &&
        getPossessionState().passByHome == homeTeam)
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
  bool looseShadowRan = false;
  // Step O6 (brain redesign plan section 27): each player's own call on a
  // loose ball decides who goes for it (live since 2026-11-23); the lab
  // still tallies how often it agrees with the team's pick (the first
  // presser). Not while our own pass travels (it has a receiver).
  if (carrier == nullptr && loosePoints > 0 && state == MatchState::PLAYING &&
      (!getPossessionState().isPass ||
       getPossessionState().passByHome != planHome))
  {
    looseShadowRan = true;
    const MatchPlayer* teamPick =
        (planHome ? homePressers : awayPressers).front();
    const LooseShadowOutcomeCapybara outcome = shadowLooseBallCapybara(
        planHome, std::span<const PathPoint>(loosePath.data(), loosePoints),
        teamPick);
    if (outcome.mine != nullptr && teamPick != nullptr &&
        outcome.mine != teamPick)
    {
      tallyWrongLooseCallCapybara(outcome.result,
                                  interception(*outcome.mine).first,
                                  interception(*teamPick).first);
    }
  }
  // Step P1 (brain redesign plan section 28): each defender's own call on
  // pressing the carrier, in the shadow of the team's pick (the first
  // presser). Draws nothing, changes nothing.
  // The planner's pursuit time without its press eagerness and continuity:
  // the diagnosis, and step P3's detailed look.
  const auto pressPursuitSeconds = [&](const MatchPlayer& candidate)
  {
    const Vector2F toTarget = pressurePosition - candidate.position;
    const float targetDistance = length(toTarget);
    if (targetDistance <= EPSILON)
    {
      return 0.0F;
    }
    const float toward = std::max(0.0F, ((candidate.velocity.x * toTarget.x) +
                                         (candidate.velocity.y * toTarget.y)) /
                                            targetDistance);
    constexpr float WEIGHT =
        MatchTuning::Player::CURRENT_VELOCITY_PURSUIT_WEIGHT;
    const float speed = std::max(currentTopSpeed(candidate) + (toward * WEIGHT),
                                 MatchTuning::Player::MINIMUM_PURSUIT_SPEED);
    return targetDistance / speed;
  };
  bool pressShadowRan = false;
  if (carrier != nullptr && carrier->isHomeTeam != planHome &&
      state == MatchState::PLAYING)
  {
    pressShadowRan = true;
    const MatchPlayer* teamPick =
        (planHome ? homePressers : awayPressers).front();
    const MatchPlayer* shadowPick =
        shadowPressCapybara(planHome, *carrier, teamPick);
    if (shadowPick != nullptr && teamPick != nullptr && shadowPick != teamPick)
    {
      tallyWrongPressCallCapybara(pressPursuitSeconds(*shadowPick),
                                  pressPursuitSeconds(*teamPick),
                                  teamPick->intent == PlayerIntent::PRESS_BALL);
    }
  }

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
    PitchPoint meet{};
    bool contests = true;
    bool running = false;
  };
  std::array<AerialRole, 2> aerialRoles{};
  challengePath.clear();
  keeperClaimMeet = {};
  const float lowestHead = lowestHeadLine();
  if ((carrier == nullptr) && !getPossessionState().isShot &&
      (ball.z > lowestHead || ball.velocityZ > 0.0F))
  {
    // Its own flight (drag, spin and curve), run ahead and put back, until
    // it drops below everyone's head line.
    using A = MatchTuning::Aerial;
    const float headLine = lowestHead;
    constexpr float TICK =
        MatchTuning::Timing::FIXED_STEP_SECONDS /
        static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
    const BallMotion saved = ballMotion();
    for (float seconds = TICK; seconds <= A::CHALLENGE_PATH_SECONDS;
         seconds += TICK)
    {
      integrateBall(TICK);
      const float height = ball.z;
      if (height < headLine && ball.velocityZ <= 0.0F)
      {
        break;
      }
      challengePath.push_back({.seconds = seconds,
                               .at = asVector(ball.position),
                               .height = height});
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
      const Vector2F from = asVector(candidate.position);
      const Vector2F toPoint{.x = point.x - from.x, .y = point.y - from.y};
      const float metres = length(toPoint);
      const float toward =
          metres > EPSILON ? std::max(0.0F, (candidate.velocity.x * toPoint.x +
                                             candidate.velocity.y * toPoint.y) /
                                                metres)
                           : 0.0F;
      const float pursuit = std::max(
          currentTopSpeed(candidate) +
              (toward * MatchTuning::Player::CURRENT_VELOCITY_PURSUIT_WEIGHT),
          MatchTuning::Player::MINIMUM_PURSUIT_SPEED);
      return std::pair{
          std::max(0.0F, metres - A::HEAD_COMFORT_GAP_METRES) / pursuit,
          metres};
    };
    // His earliest meet on the path: the first point he can head it at
    // that he can get to, holding the spot (standing jump, set in time) or
    // arriving with it (running jump, with a run-up).
    struct Meet
    {
      float seconds = 0.0F;
      Vector2F at{};
      bool running = false;
    };
    const auto earliestMeet =
        [&](const MatchPlayer& candidate) -> std::optional<Meet>
    {
      const float reach = derivedPhysicalValue(
          DerivedStat::JUMPING_REACH,
          candidate.derivedStat(DerivedStat::JUMPING_REACH));
      const float rise = std::max(reach - candidate.heightMetres, 0.0F);
      const float forehead = candidate.heightMetres - A::HEAD_OFFSET_METRES;
      const float standingTop = forehead + (rise * A::STANDING_JUMP_SHARE) -
                                A::JUMP_FOREHEAD_ABOVE_METRES;
      const float runningTop = forehead + rise - A::JUMP_FOREHEAD_ABOVE_METRES;
      const float lowest =
          std::max(bodyLines(candidate).head,
                   forehead - (A::DUCK_HEIGHT_SHARE * candidate.heightMetres));
      const bool moving =
          length(candidate.velocity) >= A::RUNNING_JUMP_MIN_SPEED;
      for (const JumpPathPoint& point : challengePath)
      {
        if (point.height > runningTop || point.height < lowest)
        {
          continue;
        }
        const auto [seconds, metres] = runSeconds(candidate, point.at);
        if (point.height <= standingTop &&
            seconds + A::CHALLENGE_SETTLE_SECONDS <= point.seconds)
        {
          return Meet{
              .seconds = point.seconds, .at = point.at, .running = false};
        }
        if (seconds <= point.seconds &&
            (moving || metres >= A::CHALLENGE_RUN_UP_METRES))
        {
          return Meet{
              .seconds = point.seconds, .at = point.at, .running = true};
        }
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
      MatchPlayer* receiver =
          getPossessionState().isPass && getPossessionState().passByHome == home
              ? findMatchPlayer(getExpectedReceiver())
              : nullptr;
      if (possessionId == PossessionId::AARDVARK && (receiver != nullptr) &&
          active(*receiver) && !receiver->isGoalkeeper)
      {
        if (const std::optional<Meet> meet = earliestMeet(*receiver))
        {
          best[side] = meet;
          bestPlayer[side] = receiver;
          continue;
        }
      }
      for (MatchPlayer* const candidate : onPitch(home))
      {
        if (candidate->isGoalkeeper)
        {
          continue;
        }
        const std::optional<Meet> meet = earliestMeet(*candidate);
        if (meet && (!best[side] ||
                     (possessionId == PossessionId::BINTURONG
                          ? PossessionCoordination::better(
                                collectionCandidate(*candidate, meet->seconds),
                                collectionCandidate(*bestPlayer[side],
                                                    best[side]->seconds))
                          : meet->seconds < best[side]->seconds)))
        {
          best[side] = meet;
          bestPlayer[side] = candidate;
        }
      }
      // His keeper, for a ball coming down in his area (goalkeeper plan step
      // 7): where it comes through his hands' reach, held (standing jump) or
      // attacked (running jump), as for a header. Not his own side's pass.
      MatchPlayer* keeper = findGoalkeeper(home);
      if ((keeper != nullptr) && getBody(home) == BodyId::BINTURONG &&
          !keeperInDive(*keeper) && !keeper->fallen &&
          !(getPossessionState().isPass &&
            getPossessionState().passByHome == home))
      {
        const float height = keeper->heightMetres;
        const float shoulder =
            MatchTuning::Goalkeeper::SHOULDER_HEIGHT_SHARE * height;
        const float standingTop = shoulder + keeperArmBinturong(*keeper);
        const float aerialReach = derivedPhysicalValue(
            DerivedStat::AERIAL_REACH,
            keeper->derivedStat(DerivedStat::AERIAL_REACH));
        const float rise = jumpRiseOf(
            aerialReach,
            MatchTuning::KeeperHands::STANDING_REACH_HEIGHT_SHARE * height,
            true);
        const bool moving =
            length(keeper->velocity) >= A::RUNNING_JUMP_MIN_SPEED;
        std::optional<Meet> meet;
        for (const JumpPathPoint& point : challengePath)
        {
          if (point.height < shoulder || point.height > standingTop + rise ||
              !inPenaltyArea(asPoint(point.at), home))
          {
            continue;
          }
          const auto [seconds, metres] = runSeconds(*keeper, point.at);
          if (point.height <= standingTop + rise * A::STANDING_JUMP_SHARE &&
              seconds + A::CHALLENGE_SETTLE_SECONDS <= point.seconds)
          {
            meet = Meet{
                .seconds = point.seconds, .at = point.at, .running = false};
            break;
          }
          if (seconds <= point.seconds &&
              (moving || metres >= A::CHALLENGE_RUN_UP_METRES))
          {
            meet =
                Meet{.seconds = point.seconds, .at = point.at, .running = true};
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
      aerialRoles[first] = {.player = bestPlayer[first],
                            .meet = asPoint(meet.at),
                            .contests = true,
                            .running = meet.running};
      // His keeper meets it first: he comes for it (goalkeeperTarget).
      if (bestPlayer[first]->isGoalkeeper)
      {
        keeperClaimMeet[first] = asPoint(meet.at);
      }
      // The other side's player who can get there soonest.
      const std::size_t other = 1 - first;
      MatchPlayer* challenger = nullptr;
      float soonest = std::numeric_limits<float>::max();
      for (MatchPlayer* const candidate : onPitch(other == 0))
      {
        if (candidate->isGoalkeeper)
        {
          continue;
        }
        const float seconds = runSeconds(*candidate, meet.at).first;
        if (seconds < soonest)
        {
          soonest = seconds;
          challenger = candidate;
        }
      }
      if (challenger != nullptr)
      {
        aerialRoles[other] = {
            .player = challenger,
            .meet = asPoint(meet.at),
            .contests =
                soonest <= meet.seconds + A::CHALLENGE_TOLERANCE_SECONDS,
            .running =
                soonest + A::CHALLENGE_SETTLE_SECONDS > meet.seconds &&
                soonest <= meet.seconds + A::CHALLENGE_TOLERANCE_SECONDS};
      }
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
      // No natural position (a drill with no layers): the neutral case.
      case PlayerRole::UNKNOWN:
        break;
      default:
        return -std::numeric_limits<float>::infinity();
    }
    const float depth = candidate.isHomeTeam
                            ? pitch.fractionX(candidate.position)
                            : 1.0F - pitch.fractionX(candidate.position);
    const float separation =
        carrier ? pitch.fractionY(
                      std::abs(candidate.position.y - carrier->position.y))
                : 0.0F;
    // Who makes the run also varies from attack to attack, so strike
    // partners share the runs in behind.
    const float variety = epochNoise(candidate.player->getId(), 0x7a11edU,
                                     RUN_TIMING_EPOCH_STEPS) *
                          MatchTuning::Shape::RUN_VARIETY_PRIORITY;
    return rolePriority + variety +
           (candidate.pace * MatchTuning::Shape::RUN_PACE_PRIORITY) +
           (depth * MatchTuning::Shape::RUN_DEPTH_PRIORITY) +
           (separation * MatchTuning::Shape::RUN_SEPARATION_PRIORITY) +
           (candidate.isMakingRun ? MatchTuning::Shape::RUN_CONTINUITY_PRIORITY
                                  : 0.0F) +
           (roleProfileOf(candidate).runBias * TacticsTuning::RUN_BIAS_WEIGHT);
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
    if (!possessionContext && !looseTransitionContext)
    {
      return runners;
    }

    const StrategySliders& sliders = homeTeam ? homeSliders : awaySliders;
    const float attackingPosition =
        carrier ? carrier->position.x : ball.position.x;
    const float attackingProgress =
        homeTeam ? pitch.fractionX(attackingPosition)
                 : 1.0F - pitch.fractionX(attackingPosition);
    const float attackingCommitment =
        sliders.offensiveBias + sliders.riskTaking;
    const TeamPhase phase = homeTeam ? homePhase : awayPhase;
    const int lead = homeTeam ? homeScore - awayScore : awayScore - homeScore;
    const bool managingGame =
        drillRules.layers && lead >= MatchTuning::Decision::COMFORTABLE_LEAD;
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
        {
          continue;
        }
        const bool striker = candidate.player->getRole() == PlayerRole::ST;
        if (striker && strikers >= MatchTuning::Shape::MAX_STRIKER_RUNNERS)
        {
          continue;
        }
        const float score = runPriority(candidate);
        if (score > scores[slot])
        {
          scores[slot] = score;
          runners[slot] = &candidate;
        }
      }
      if (runners[slot] && runners[slot]->player->getRole() == PlayerRole::ST)
      {
        ++strikers;
      }
    }
    return runners;
  };

  const auto homeRunners = selectAttackingRunners(true);
  const auto awayRunners = selectAttackingRunners(false);

  const auto runChannel = [this](const MatchPlayer& runner)
  {
    const PlayerRole role = runner.player->getRole();
    if (role == PlayerRole::LW)
    {
      return MatchTuning::Shape::LEFT_INSIDE_FORWARD_CHANNEL;
    }
    if (role == PlayerRole::RW)
    {
      return MatchTuning::Shape::RIGHT_INSIDE_FORWARD_CHANNEL;
    }
    if (role == PlayerRole::LM)
    {
      return MatchTuning::Shape::LEFT_WIDE_ATTACK_CHANNEL;
    }
    if (role == PlayerRole::RM)
    {
      return MatchTuning::Shape::RIGHT_WIDE_ATTACK_CHANNEL;
    }
    if (runner.basePosition.y < pitch.atY(MatchTuning::Pitch::CENTRE))
    {
      return MatchTuning::Shape::LEFT_STRIKER_ATTACK_CHANNEL;
    }
    if (runner.basePosition.y > pitch.atY(MatchTuning::Pitch::CENTRE))
    {
      return MatchTuning::Shape::RIGHT_STRIKER_ATTACK_CHANNEL;
    }
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
    if (!carrier || carrier->isHomeTeam != homeTeam)
    {
      return nullptr;
    }
    const TeamPhase phase = homeTeam ? homePhase : awayPhase;
    if (phase != TeamPhase::FINAL_THIRD)
    {
      return nullptr;
    }
    const float ballSide =
        carrier->position.y - pitch.atY(MatchTuning::Pitch::CENTRE);
    if (arrival == Arrival::FAR_POST &&
        std::abs(ballSide) < MatchTuning::Shape::FAR_POST_MIN_BALL_WIDTH_METRES)
    {
      return nullptr;
    }

    MatchPlayer* selected = nullptr;
    float bestScore = std::numeric_limits<float>::max();
    for (MatchPlayer* const pointer : onPitch(homeTeam))
    {
      MatchPlayer& candidate = *pointer;
      if (&candidate == carrier || candidate.isInjured || candidate.fallen)
      {
        continue;
      }
      const auto& runners = homeTeam ? homeRunners : awayRunners;
      if (std::ranges::find(runners, &candidate) != runners.end())
      {
        continue;
      }
      const PlayerRole role = candidate.player->getRole();
      const bool wide = role == PlayerRole::LM || role == PlayerRole::RM ||
                        role == PlayerRole::LW || role == PlayerRole::RW;
      const bool farSide =
          (candidate.basePosition.y - pitch.atY(MatchTuning::Pitch::CENTRE)) *
              ballSide <
          0.0F;
      const bool eligible =
          arrival == Arrival::FULLBACK
              ? role == PlayerRole::LB || role == PlayerRole::RB
          : arrival == Arrival::FAR_POST
              ? wide && farSide
              : role == PlayerRole::CM || role == PlayerRole::CAM ||
                    role == PlayerRole::UNKNOWN;
      if (!eligible)
      {
        continue;
      }
      // A defensive full-back never overlaps; an attacking one is keener.
      const float overlapBias = arrival == Arrival::FULLBACK
                                    ? roleProfileOf(candidate).overlapBias
                                    : 0.0F;
      if (overlapBias <= -1.0F)
      {
        continue;
      }

      float score = pitch.fractionY(std::abs(candidate.basePosition.y -
                                             carrier->position.y)) -
                    (overlapBias * TacticsTuning::OVERLAP_BIAS_WEIGHT);
      if (arrival == Arrival::FAR_POST)
      {
        score = -score;
      }
      const PlayerIntent continuityIntent = arrival == Arrival::FULLBACK
                                                ? PlayerIntent::OVERLAP
                                                : PlayerIntent::ATTACK_BOX;
      if (candidate.intent == continuityIntent)
      {
        score -= MatchTuning::Shape::FINAL_THIRD_SELECTION_CONTINUITY;
      }
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
    if (!carrier || carrier->isHomeTeam != homeTeam)
    {
      return supporters;
    }

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
          role == PlayerRole::RW || role == PlayerRole::ST ||
          role == PlayerRole::UNKNOWN;
      if (!eligibleSupportRole)
      {
        continue;
      }

      const float score =
          distance(candidate.position, carrier->position) -
          (candidate.intent == PlayerIntent::OFFER_SUPPORT
               ? MatchTuning::Shape::SUPPORT_SELECTION_CONTINUITY_METRES
               : 0.0F);
      for (std::size_t slot = 0; slot < supporters.size(); ++slot)
      {
        if (score >= scores[slot])
        {
          continue;
        }
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
    if (!carrier || carrier->isHomeTeam == defendingHome)
    {
      return nullptr;
    }
    const float possessionDirection = carrier->isHomeTeam ? 1.0F : -1.0F;
    const MatchPlayer* outlet = nullptr;
    float bestScore = std::numeric_limits<float>::max();
    for (const MatchPlayer* const pointer : onPitch(carrier->isHomeTeam))
    {
      const MatchPlayer& candidate = *pointer;
      if (&candidate == carrier || candidate.isGoalkeeper)
      {
        continue;
      }
      const float outletDistance =
          distance(candidate.position, carrier->position);
      if (outletDistance > MatchTuning::Shape::COVER_OUTLET_MAX_DISTANCE_METRES)
      {
        continue;
      }
      const bool forwardOption =
          (candidate.position.x - carrier->position.x) * possessionDirection >
          0.0F;
      const float score =
          outletDistance -
          (forwardOption ? MatchTuning::Shape::COVER_FORWARD_OPTION_METRES
                         : 0.0F);
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
        {
          plan.pressArrivalSeconds[index] =
              estimatedArrivalTime(*pressers[index]);
        }
      }
      const auto& runners = homeTeam ? homeRunners : awayRunners;
      for (std::size_t index = 0; index < plan.runners.size(); ++index)
      {
        plan.runners[index] = idOf(runners[index]);
        if (runners[index])
        {
          plan.runPriority[index] = runPriority(*runners[index]);
        }
      }
      plan.midfieldArrival =
          idOf(homeTeam ? homeMidfieldArrival : awayMidfieldArrival);
      plan.farPostRunner =
          idOf(homeTeam ? homeFarPostRunner : awayFarPostRunner);
      plan.overlappingFullback =
          idOf(homeTeam ? homeOverlappingFullback : awayOverlappingFullback);
      const auto& supporters = homeTeam ? homeSupporters : awaySupporters;
      for (std::size_t index = 0; index < plan.supporters.size(); ++index)
      {
        plan.supporters[index] = idOf(supporters[index]);
      }
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
        const Vector2F toTarget = pressurePosition - candidate.position;
        const float targetDistance = length(toTarget);
        if (targetDistance > EPSILON)
        {
          const Vector2F direction{.x = toTarget.x / targetDistance,
                                   .y = toTarget.y / targetDistance};
          const float toward =
              std::max(0.0F, (candidate.velocity.x * direction.x) +
                                 (candidate.velocity.y * direction.y));
          const float speed = std::max(
              currentTopSpeed(candidate) +
                  (toward *
                   MatchTuning::Player::CURRENT_VELOCITY_PURSUIT_WEIGHT),
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
        {
          score.add("already going (continuity)", TermSource::SITUATION,
                    -MatchTuning::Shape::PRESSER_CONTINUITY_SECONDS);
        }
        // Arrival times cannot go below zero.
        if (const float clamp = score.residual(); std::abs(clamp) > 1e-6F)
        {
          score.add("clamped at zero", TermSource::SITUATION, clamp);
        }
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
                                : 1.0F - pitch.fractionX(candidate.position);
        const float separation =
            carrier ? pitch.fractionY(
                          std::abs(candidate.position.y - carrier->position.y))
                    : 0.0F;
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
                      : 0.0F);
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
        const bool selectingRuns =
            (homeTeam ? homeRunners : awayRunners)[0] != nullptr;
        for (MatchPlayer* const pointer : onPitch(homeTeam))
        {
          const MatchPlayer& candidate = *pointer;
          if (candidate.isGoalkeeper)
          {
            continue;
          }
          toBall.candidates.push_back(
              {.player = idOf(&candidate), .score = arrivalTerms(candidate)});
          if (selectingRuns && &candidate != carrier && !candidate.isInjured &&
              std::isfinite(runPriority(candidate)))
          {
            runs.candidates.push_back(
                {.player = idOf(&candidate), .score = runTerms(candidate)});
          }
        }
        recorder->onRankingDetail(*this, toBall);
        if (!runs.candidates.empty())
        {
          recorder->onRankingDetail(*this, runs);
        }
      }
    }
  }

  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != planHome)
    {
      continue;
    }
    const bool goalkeeper = player.isGoalkeeper;
    const StrategySliders& sliders =
        player.isHomeTeam ? homeSliders : awaySliders;
    const TeamPhase teamPhase = player.isHomeTeam ? homePhase : awayPhase;
    const float attackDirection = player.isHomeTeam ? 1.0F : -1.0F;
    const bool ownPossession =
        (carrier != nullptr) && carrier->isHomeTeam == player.isHomeTeam;
    // While his side has the ball (or its pass is on the way) every slot
    // starts from its attacking-phase spot (role and in-possession shape),
    // otherwise from the formation.
    const bool ownControl =
        (carrier != nullptr)
            ? ownPossession
            : getPossessionHistory().lastControlledTeamHome &&
                  *getPossessionHistory().lastControlledTeamHome ==
                      player.isHomeTeam;
    PitchPoint target =
        ownControl ? possessionAnchorCapybara(player) : player.basePosition;
    // How fast the target itself moves, when it is pinned to a moving man
    // (otherwise worked out from his last plan, below), and his job.
    Vector2F targetVelocity{.x = 0.0F, .y = 0.0F};
    bool pinned = false;
    // The ground loose-ball chaser, and when he meets it (way-round plan).
    bool looseChase = false;
    float looseMeetSeconds = 0.0F;
    std::uint64_t job = planJob(PlanBranch::SHAPE);
    player.intent = PlayerIntent::HOLD_SHAPE;
    player.isPressing = false;
    player.isMakingRun = false;

    // The whole block follows the ball while preserving its formation. This
    // produces recognizable defensive, middle and attacking lines rather than
    // twenty outfield players independently chasing one point.
    const float widthScale =
        MatchTuning::Shape::MIN_WIDTH_SCALE +
        (sliders.widthUsage * MatchTuning::Shape::WIDTH_SLIDER_SCALE);
    const float centreX = pitch.atX(MatchTuning::Pitch::CENTRE);
    const float centreY = pitch.atY(MatchTuning::Pitch::CENTRE);
    target.y = centreY + (target.y - centreY) * widthScale;
    target.x += (shapeAnchor.x - centreX) *
                (MatchTuning::Shape::BASE_LONGITUDINAL_SHIFT +
                 sliders.compactness *
                     MatchTuning::Shape::COMPACTNESS_LONGITUDINAL_SHIFT);
    target.y +=
        (shapeAnchor.y - centreY) *
        (MatchTuning::Shape::BASE_LATERAL_SHIFT +
         sliders.compactness * MatchTuning::Shape::COMPACTNESS_LATERAL_SHIFT);

    // Out of possession the block drops and compacts around the ball: the
    // back line sits deeper the closer the ball is to goal, and the lines
    // behind it squeeze into a 20-40 m long, ~45 m wide unit.
    const bool defending =
        !goalkeeper &&
        ((carrier != nullptr)
             ? carrier->isHomeTeam != player.isHomeTeam
             : getPossessionHistory().lastControlledTeamHome &&
                   *getPossessionHistory().lastControlledTeamHome !=
                       player.isHomeTeam);
    if (defending)
    {
      using S = MatchTuning::Shape;
      const auto ownGoalDepth = [&](float x)
      {
        const float fraction = pitch.fractionX(x);
        return player.isHomeTeam ? fraction : 1.0F - fraction;
      };
      const float ballDepth = ownGoalDepth(shapeAnchor.x);
      const float lineDepth = std::clamp(
          S::BLOCK_LINE_BASE + (ballDepth * S::BLOCK_LINE_BALL_FACTOR) +
              (sliders.pressing * S::BLOCK_LINE_PRESSING),
          S::BLOCK_LINE_MIN, S::BLOCK_LINE_MAX);
      const float lengthShare =
          std::clamp((ballDepth - S::BLOCK_SHORT_BALL_DEPTH) /
                         (S::BLOCK_LONG_BALL_DEPTH - S::BLOCK_SHORT_BALL_DEPTH),
                     0.0F, 1.0F);
      const float teamLength =
          (S::BLOCK_SHORT_LENGTH +
           (S::BLOCK_LONG_LENGTH - S::BLOCK_SHORT_LENGTH) * lengthShare) *
          (1.0F - sliders.compactness * S::BLOCK_COMPACTNESS_SQUEEZE);
      const float formationShare = std::clamp(
          (ownGoalDepth(player.basePosition.x) - S::BLOCK_FORMATION_BACK) /
              (S::BLOCK_FORMATION_FRONT - S::BLOCK_FORMATION_BACK),
          0.0F, 1.0F);
      const float depth = lineDepth + (formationShare * teamLength);
      target.x = pitch.atX(player.isHomeTeam ? depth : 1.0F - depth);
      target.y = centreY +
                 (player.basePosition.y - centreY) *
                     (S::BLOCK_WIDTH_SCALE -
                      sliders.compactness * S::BLOCK_COMPACTNESS_NARROWING) +
                 (shapeAnchor.y - centreY) * S::BLOCK_BALL_SIDE_SHIFT;
      target.x += defensiveRoleShiftCapybara(player);
    }
    // Steps O6 and P3: his shape spot (the block moved with the ball), for
    // the first-to-commit call.
    if (looseShadowRan || pressShadowRan)
    {
      shapeSpotsCapybara.at(playerIndexCapybara(player)) = target;
    }

    if (&player == carrier)
    {
      player.intent = PlayerIntent::CARRY_BALL;
      target = carryTargetCapybara(player);
      job = planJob(PlanBranch::CARRY);
    }
    else if (ownPossession)
    {
      player.intent = PlayerIntent::HOLD_SHAPE;
      const float attackingProgress =
          player.isHomeTeam ? pitch.fractionX(carrier->position)
                            : 1.0F - pitch.fractionX(carrier->position);
      target.x += pitch.atX(
          attackDirection *
          std::max(0.0F, attackingProgress -
                             MatchTuning::Shape::POSSESSION_PROGRESS_START) *
          MatchTuning::Shape::POSSESSION_BLOCK_PROGRESS);
      target.x +=
          pitch.atX(attackDirection *
                    (MatchTuning::Shape::SUPPORT_BASE_ADVANCE +
                     sliders.offensiveBias *
                         MatchTuning::Shape::SUPPORT_OFFENSIVE_ADVANCE));
      const PlayerRole role = player.player->getRole();
      const bool forward = role == PlayerRole::ST || role == PlayerRole::LW ||
                           role == PlayerRole::RW || role == PlayerRole::CAM;
      const auto& runners = player.isHomeTeam ? homeRunners : awayRunners;
      const auto runnerPosition = std::ranges::find(runners, &player);
      MatchPlayer* midfieldArrival =
          player.isHomeTeam ? homeMidfieldArrival : awayMidfieldArrival;
      MatchPlayer* farPostRunner =
          player.isHomeTeam ? homeFarPostRunner : awayFarPostRunner;
      MatchPlayer* overlappingFullback =
          player.isHomeTeam ? homeOverlappingFullback : awayOverlappingFullback;
      const auto& supporters =
          player.isHomeTeam ? homeSupporters : awaySupporters;
      const auto supportPosition = std::ranges::find(supporters, &player);
      // The situation "Our ball, a team-mate has it" (brain redesign plan
      // section 22, step O1): his off-ball frame. An option is open when
      // the team's assignments (above) give it to him; hold shape is always
      // open. Follow-up: no measures yet; the stopgap pick is today's
      // order (the first open option).
      const std::array<bool, OUR_BALL_JOB_COUNT> ourBallOpen{
          runnerPosition != runners.end(),
          &player == midfieldArrival || &player == farPostRunner,
          &player == overlappingFullback,
          supportPosition != supporters.end(),
          forward,
          true};
      const auto ourBallJob = static_cast<OurBallJob>(std::distance(
          ourBallOpen.begin(), std::ranges::find(ourBallOpen, true)));
      noteOffBallCapybara(player, "Our ball, a team-mate has it", ourBallOpen,
                          OUR_BALL_NAMES, OUR_BALL_WHEN,
                          static_cast<std::size_t>(ourBallJob));
      if (ourBallJob == OurBallJob::RUN_IN_BEHIND)
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
                       0.5F +
                   0.25F) *
                      MatchTuning::Passing::RUN_TIMING_GAMBLE_METRES
                : 0.0F;
        const float onsideTarget =
            legalRunLine -
            (attackDirection *
             (MatchTuning::Shape::RUN_ONSIDE_BUFFER_METRES - timing));
        const float runDepthTarget =
            onsideTarget -
            (attackDirection * static_cast<float>(runnerSlot) *
             MatchTuning::Shape::SECONDARY_RUN_DEPTH_STAGGER_METRES);
        target.x += (runDepthTarget - target.x) *
                    MatchTuning::Shape::RUN_DEPTH_TARGET_PULL;
        target.x = player.isHomeTeam ? std::min(target.x, onsideTarget)
                                     : std::max(target.x, onsideTarget);

        float channel = runChannel(player);
        bool channelMoved = false;
        if (runnerSlot > 0 && (runners[0] != nullptr))
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
        target.y += (pitch.atY(channel) - target.y) *
                    MatchTuning::Shape::RUN_CHANNEL_BLEND;

        // Only a runner caught clearly beyond the line checks back; one who
        // is marginally off relies on his timing (and sometimes gets caught).
        const bool checkingBack =
            isOffside(player, player.isHomeTeam, defenderLine,
                      MatchTuning::Passing::RUN_TIMING_GAMBLE_METRES * 0.75F);
        if (checkingBack)
        {
          target.x -=
              attackDirection * MatchTuning::Shape::ONSIDE_RECOVERY_METRES;
        }
        // His timing is drawn afresh each epoch: a new run.
        job = planJob(
            PlanBranch::RUN,
            static_cast<std::uint32_t>(runnerSlot) |
                (timing != 0.0F ? 1U << 8 : 0U) |
                (channelMoved ? 1U << 9 : 0U) | (checkingBack ? 1U << 10 : 0U),
            static_cast<std::uint32_t>(stepCounter / RUN_TIMING_EPOCH_STEPS));
      }
      else
      {
        if (ourBallJob == OurBallJob::BOX_ARRIVAL)
        {
          // Late runs into the box: the midfielder to the penalty spot on
          // the side away from the ball, the wide man to the far post; both
          // stay onside.
          using S = MatchTuning::Shape;
          player.intent = PlayerIntent::ATTACK_BOX;
          const bool farPost = &player == farPostRunner;
          const float goalX = player.isHomeTeam ? pitch.length : 0.0F;
          const float ballSide = carrier->position.y < centreY ? -1.0F : 1.0F;
          const float arrivalX =
              goalX -
              (attackDirection * (farPost ? S::FAR_POST_ARRIVAL_DEPTH_METRES
                                          : S::MIDFIELD_ARRIVAL_DEPTH_METRES));
          const float onside =
              lineFor(player.isHomeTeam) -
              (attackDirection * MatchTuning::Shape::RUN_ONSIDE_BUFFER_METRES);
          target.x = player.isHomeTeam ? std::min(arrivalX, onside)
                                       : std::max(arrivalX, onside);
          target.y = pitch.atY(MatchTuning::Pitch::CENTRE) -
                     ballSide * (farPost ? S::FAR_POST_ARRIVAL_WIDTH_METRES
                                         : S::MIDFIELD_ARRIVAL_WIDTH_METRES);
          job = planJob(PlanBranch::BOX_ARRIVAL,
                        (farPost ? 1U : 0U) | (ballSide > 0.0F ? 2U : 0U));
        }
        else if (ourBallJob == OurBallJob::OVERLAP)
        {
          player.intent = PlayerIntent::OVERLAP;
          job = planJob(PlanBranch::OVERLAP);
          target.x +=
              pitch.atX(attackDirection *
                        MatchTuning::Shape::FINAL_THIRD_FULLBACK_OVERLAP);
        }
        else if (ourBallJob == OurBallJob::SUPPORT)
        {
          player.intent = PlayerIntent::OFFER_SUPPORT;
          const std::size_t supportSlot = static_cast<std::size_t>(
              std::distance(supporters.begin(), supportPosition));
          const float primarySide =
              (supporters[MatchTuning::Shape::NEAR_SUPPORT_SLOT] != nullptr) &&
                      supporters[MatchTuning::Shape::NEAR_SUPPORT_SLOT]
                              ->basePosition.y <= carrier->position.y
                  ? -1.0F
                  : 1.0F;
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
          const bool transitionBonus =
              teamPhase == TeamPhase::ATTACKING_TRANSITION;
          if (transitionBonus)
          {
            target.x +=
                pitch.atX(attackDirection *
                          MatchTuning::Shape::TRANSITION_SUPPORT_FORWARD_BONUS);
          }
          job = planJob(PlanBranch::SUPPORT,
                        static_cast<std::uint32_t>(supportSlot) |
                            (supportSide > 0.0F ? 1U << 8 : 0U) |
                            (transitionBonus ? 1U << 9 : 0U));
        }
        else if (ourBallJob == OurBallJob::FORWARD_SHORT)
        {
          player.intent = PlayerIntent::OFFER_SUPPORT;
          // Not every attacker runs beyond the defence. A complementary
          // forward checks toward the ball to form a passing triangle and drag
          // a marker.
          target.x = carrier->position.x -
                     attackDirection *
                         MatchTuning::Shape::FORWARD_SHORT_OPTION_DEPTH_METRES;
          const float lateralDirection =
              player.basePosition.y <= carrier->position.y ? -1.0F : 1.0F;
          target.y = carrier->position.y +
                     lateralDirection *
                         MatchTuning::Shape::
                             FORWARD_SHORT_OPTION_LATERAL_SEPARATION_METRES;
          job = planJob(PlanBranch::FORWARD_SHORT,
                        lateralDirection > 0.0F ? 1U : 0U);
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
    else if (carrier != nullptr)
    {
      const auto& pressers = player.isHomeTeam ? homePressers : awayPressers;
      const std::optional<PitchPoint> doubling =
          pressers[1] == &player
              ? doubleUpPointCapybara(player, *carrier, pressers[0])
              : std::nullopt;
      const bool coveringPress =
          pressers[1] == &player &&
          sliders.pressing > MatchTuning::Shape::COVER_PRESS_MINIMUM;
      const MatchPlayer* coverOutlet =
          player.isHomeTeam ? homeCoverOutlet : awayCoverOutlet;
      const MatchPlayer* tight = tightMarkTargetCapybara(player);
      // Zonal marking: the opponent assigned to this player (one marker
      // per opponent).
      const std::int8_t assigned = markAssignments.at(slotOf(player));
      const MatchPlayer* mark =
          assigned >= 0 && static_cast<std::size_t>(assigned) < players.size()
              ? &players[static_cast<std::size_t>(assigned)]
              : nullptr;
      if ((mark != nullptr) &&
          (!active(*mark) || mark->isHomeTeam == player.isHomeTeam))
      {
        mark = nullptr;
      }
      // The situation "Opponents' ball" (brain redesign plan section 22,
      // step O2): his off-ball frame. Open when the team's assignments,
      // orders or phase give it to him; hold shape is always open.
      // Follow-up: no measures yet; the stopgap pick is today's order (the
      // first open option).
      const std::array<bool, THEIR_BALL_JOB_COUNT> theirBallOpen{
          pressers[0] == &player,
          doubling.has_value(),
          coveringPress && coverOutlet != nullptr,
          coveringPress && coverOutlet == nullptr,
          tight != nullptr,
          teamPhase == TeamPhase::DEFENSIVE_TRANSITION,
          mark != nullptr,
          true};
      const auto theirBallJob = static_cast<TheirBallJob>(std::distance(
          theirBallOpen.begin(), std::ranges::find(theirBallOpen, true)));
      noteOffBallCapybara(player, "Opponents' ball", theirBallOpen,
                          THEIR_BALL_NAMES, THEIR_BALL_WHEN,
                          static_cast<std::size_t>(theirBallJob));
      if (theirBallJob == TheirBallJob::PRESS)
      {
        player.intent = PlayerIntent::PRESS_BALL;
        player.isPressing = true;
        const float standOff =
            (MatchTuning::Shape::PRESSING_STANDOFF_BASE_METRES +
             (1.0F - sliders.pressing) *
                 MatchTuning::Shape::PRESSING_STANDOFF_CAUTIOUS_BONUS_METRES) *
            pressStandOffShareCapybara(player, *carrier);
        target = pressurePosition;
        target.x -= attackDirection * standOff;
        target.y += weakFootShadeCapybara(*carrier);
        targetVelocity = carrier->velocity;
        pinned = true;
        job = planJob(PlanBranch::PRESS);
      }
      else if (theirBallJob == TheirBallJob::DOUBLE_UP)
      {
        // Ordered to double up: the second man closes the carrier too.
        player.intent = PlayerIntent::COVER_PRESS;
        player.isPressing = true;
        target = *doubling;
        job = planJob(PlanBranch::DOUBLE_UP);
      }
      else if (theirBallJob == TheirBallJob::BLOCK_LANE ||
               theirBallJob == TheirBallJob::COVER)
      {
        player.isPressing = true;
        if (theirBallJob == TheirBallJob::BLOCK_LANE)
        {
          player.intent = PlayerIntent::BLOCK_PASSING_LANE;
          target.x = pressurePosition.x +
                     (coverOutlet->position.x - pressurePosition.x) *
                         MatchTuning::Shape::COVER_LANE_INTERCEPTION_POINT;
          target.y = pressurePosition.y +
                     (coverOutlet->position.y - pressurePosition.y) *
                         MatchTuning::Shape::COVER_LANE_INTERCEPTION_POINT;
          target.x -= attackDirection *
                      MatchTuning::Shape::COVER_LANE_GOAL_SIDE_OFFSET_METRES;
          job = planJob(PlanBranch::BLOCK_LANE, 0,
                        static_cast<std::uint32_t>(slotOf(*coverOutlet)));
        }
        else
        {
          player.intent = PlayerIntent::COVER_PRESS;
          const float lateralSide =
              player.position.y <= pressurePosition.y ? -1.0F : 1.0F;
          target.x =
              pressurePosition.x -
              attackDirection * MatchTuning::Shape::COVER_FALLBACK_DEPTH_METRES;
          target.y =
              pressurePosition.y +
              lateralSide *
                  MatchTuning::Shape::COVER_FALLBACK_LATERAL_OFFSET_METRES;
          job = planJob(PlanBranch::COVER, lateralSide > 0.0F ? 1U : 0U);
        }
      }
      else if (theirBallJob == TheirBallJob::TIGHT_MARK)
      {
        // Under a tight-marking order he stays with his man in every phase,
        // goal-side and close enough to contest each ball played to him.
        player.intent = PlayerIntent::MARK_OPPONENT;
        target = tightMarkPointCapybara(player, *tight, target);
        targetVelocity = tightMarkVelocityCapybara(*tight);
        pinned = true;
        job = planJob(PlanBranch::TIGHT_MARK, 0,
                      static_cast<std::uint32_t>(slotOf(*tight)));
      }
      else if (theirBallJob == TheirBallJob::TRANSITION_RECOVER)
      {
        // The nearest players counter-press above. Everyone else first gets
        // goal-side and narrows toward the danger before settling into the
        // normal defensive block.
        player.intent = PlayerIntent::RECOVER_SHAPE;
        target.x -=
            pitch.atX(attackDirection *
                      MatchTuning::Shape::DEFENSIVE_TRANSITION_RECOVERY);
        target.y += (shapeAnchor.y - target.y) *
                    MatchTuning::Shape::DEFENSIVE_TRANSITION_BALL_COMPACTNESS;
        job = planJob(PlanBranch::TRANSITION_RECOVER);
      }
      else if (theirBallJob == TheirBallJob::ZONAL_MARK)
      {
        // Zonal marking: shade toward his assigned opponent without
        // abandoning the formation anchor.
        {
          player.intent = PlayerIntent::MARK_OPPONENT;
          float markWeight = MatchTuning::Shape::BASE_MARK_WEIGHT +
                             (sliders.compactness *
                              MatchTuning::Shape::COMPACTNESS_MARK_WEIGHT);
          PitchPoint markPoint = mark->position;
          const float markFromGoal =
              pitch.fromOwnGoalLine(mark->position, player.isHomeTeam);
          const bool goalSide =
              markFromGoal <= MatchTuning::Shape::TIGHT_MARK_WITHIN_METRES;
          job = planJob(PlanBranch::ZONAL_MARK, goalSide ? 1U : 0U,
                        static_cast<std::uint32_t>(assigned));
          if (goalSide)
          {
            // Near the own goal a marker stays tight and goal-side.
            markWeight =
                std::max(markWeight, MatchTuning::Shape::TIGHT_MARK_WEIGHT);
            markPoint.x -=
                attackDirection * MatchTuning::Shape::GOAL_SIDE_MARK_METRES;
          }
          target.x += (markPoint.x - target.x) * markWeight;
          target.y += (markPoint.y - target.y) * markWeight;
        }
      }
    }
    else
    {
      const bool receivingPass =
          getPossessionState().isPass &&
          getPossessionState().passByHome == player.isHomeTeam;
      const auto& pressers = player.isHomeTeam ? homePressers : awayPressers;
      // Nobody has the ball (brain redesign plan section 22, step O3):
      // three situations, each his off-ball frame. Open when the team's
      // assignments, orders or phase give it to him; the stopgap pick is
      // today's order (the first open option). Follow-up: measures.
      // - "Our pass travelling": receive it, or support while it travels.
      // - "Their pass, or a shot, travelling": chase it (the first
      //   presser), cover the chase, tight mark, recover in transition,
      //   hold shape.
      // - "Ball loose": chase, cover the chase, transition run or support,
      //   recover.
      const bool travelling =
          getPossessionState().isPass || getPossessionState().isShot;
      const MatchPlayer* tight =
          getPossessionState().isPass &&
                  getPossessionState().passByHome != player.isHomeTeam
              ? tightMarkTargetCapybara(player)
              : nullptr;
      const bool attackingTransition =
          teamPhase == TeamPhase::ATTACKING_TRANSITION;
      const auto& runners = player.isHomeTeam ? homeRunners : awayRunners;
      const bool transitionRunner =
          std::ranges::find(runners, &player) != runners.end();
      const std::array<bool, LOOSE_JOB_COUNT> looseOpen{
          receivingPass && (possessionId == PossessionId::BINTURONG
                                ? pressers[0] == &player
                                : player.player == getExpectedReceiver()),
          receivingPass,
          looseShadowRan ? looseChaserCapybara(player, pressers[0])
                         : pressers[0] == &player,
          (pressers[1] == &player &&
           sliders.pressing >
               MatchTuning::Shape::SECOND_LOOSE_BALL_PRESS_THRESHOLD) ||
              (looseShadowRan && looseStoodDownCapybara(player)),
          travelling && tight != nullptr,
          travelling && teamPhase == TeamPhase::DEFENSIVE_TRANSITION,
          travelling,
          attackingTransition && transitionRunner,
          attackingTransition,
          true};
      const auto looseJob = static_cast<LooseJob>(
          std::distance(looseOpen.begin(), std::ranges::find(looseOpen, true)));
      const char* looseSituation = "Ball loose";
      if (receivingPass)
      {
        looseSituation = "Our pass travelling";
      }
      else if (travelling)
      {
        looseSituation = "Their pass, or a shot, travelling";
      }
      noteOffBallCapybara(player, looseSituation, looseOpen, LOOSE_NAMES,
                          LOOSE_WHEN, static_cast<std::size_t>(looseJob));
      if (receivingPass)
      {
        if (looseJob == LooseJob::RECEIVE)
        {
          // Meet the ball on its path instead of chasing where it is: the
          // closest point of the remaining flight line ahead of the ball.
          player.intent = PlayerIntent::RECEIVE_PASS;
          job = planJob(PlanBranch::RECEIVE);
          const float ballSpeed = length(ball.velocity);
          const Vector2F lookahead = {
              .x = ball.velocity.x *
                   MatchTuning::Player::PASS_RECEIVER_LOOKAHEAD_SECONDS,
              .y = ball.velocity.y *
                   MatchTuning::Player::PASS_RECEIVER_LOOKAHEAD_SECONDS};
          PitchPoint meet{ball.position.x + lookahead.x,
                          ball.position.y + lookahead.y};
          const float heightMetres = ball.z;
          if (heightMetres > bodyLines(player).head || ball.velocityZ > 0.0F)
          {
            // A lofted ball is met where it drops to a controllable height,
            // not intercepted on the way up.
            constexpr float G = MatchTuning::Ball::GRAVITY;
            const float drop = std::max(
                0.0F, heightMetres -
                          MatchTuning::Passing::LOFTED_ARRIVAL_HEIGHT_METRES);
            const float fallSeconds =
                (ball.velocityZ + std::sqrt((ball.velocityZ * ball.velocityZ) +
                                            (2.0F * G * drop))) /
                G;
            const Vector2F landing = {
                .x = ball.velocity.x * fallSeconds * AIR_TRAVEL_SHARE,
                .y = ball.velocity.y * fallSeconds * AIR_TRAVEL_SHARE};
            meet = PitchPoint{ball.position.x + landing.x,
                              ball.position.y + landing.y};
            job = planJob(PlanBranch::RECEIVE, 1U);
          }
          else if (ballSpeed > EPSILON)
          {
            const Vector2F heading{.x = ball.velocity.x / ballSpeed,
                                   .y = ball.velocity.y / ballSpeed};
            const Vector2F offset = player.position - ball.position;
            const float along = (offset.x * heading.x) + (offset.y * heading.y);
            if (along > 0.0F)
            {
              // On its path, not where it is: a different spot.
              job = planJob(PlanBranch::RECEIVE, 2U);
              const Vector2F ahead = {.x = heading.x * along,
                                      .y = heading.y * along};
              meet = PitchPoint{ball.position.x + ahead.x,
                                ball.position.y + ahead.y};
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
          target.x += pitch.atX(
              attackDirection *
              MatchTuning::Shape::IN_FLIGHT_SUPPORT_ADVANCE_PER_SECOND * dt);
          target.y +=
              (ball.position.y - target.y) *
              MatchTuning::Shape::IN_FLIGHT_SUPPORT_BALL_PULL_PER_SECOND * dt;
        }
      }
      // For a genuinely loose ball, only the nearest players contest it.
      // Everybody else either joins a selected transition run or recovers the
      // team shape. A normal pass is handled above so it does not trigger a
      // full-team scramble on every release of the ball.
      else if (looseJob == LooseJob::CHASE || looseJob == LooseJob::COVER_CHASE)
      {
        const bool chasing = looseJob == LooseJob::CHASE;
        player.intent = chasing ? PlayerIntent::CLAIM_LOOSE_BALL
                                : PlayerIntent::COVER_PRESS;
        player.isPressing = true;
        job = planJob(chasing ? PlanBranch::LOOSE_BALL
                              : PlanBranch::LOOSE_COVER);
        // Run at where he can first meet it on its path (the stopgap pick
        // of where to meet it: the earliest).
        const auto [meetSeconds, meet] = interception(player);
        looseChase = chasing;
        looseMeetSeconds = meetSeconds;
        target = clampToPitch(meet);
        if (!chasing)
        {
          target.x -= attackDirection *
                      MatchTuning::Player::COVER_LOOSE_BALL_OFFSET_METRES;
        }
      }
      else if (travelling)
      {
        if (looseJob == LooseJob::TIGHT_MARK)
        {
          // A tight marker goes with his man while the pass is on its way.
          player.intent = PlayerIntent::MARK_OPPONENT;
          target = tightMarkPointCapybara(player, *tight, target);
          targetVelocity = tightMarkVelocityCapybara(*tight);
          pinned = true;
          job = planJob(PlanBranch::TIGHT_MARK, 0,
                        static_cast<std::uint32_t>(slotOf(*tight)));
        }
        else if (looseJob == LooseJob::TRANSITION_RECOVER)
        {
          player.intent = PlayerIntent::RECOVER_SHAPE;
          target.x -=
              pitch.atX(attackDirection *
                        MatchTuning::Shape::DEFENSIVE_TRANSITION_RECOVERY);
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
        if (looseJob == LooseJob::TRANSITION_RUN ||
            looseJob == LooseJob::TRANSITION_SUPPORT)
        {
          player.intent = transitionRunner ? PlayerIntent::RUN_IN_BEHIND
                                           : PlayerIntent::OFFER_SUPPORT;
          player.isMakingRun = transitionRunner;
          target.x += pitch.atX(
              attackDirection *
              (transitionRunner
                   ? MatchTuning::Shape::ATTACKING_TRANSITION_RUN_ADVANCE
                   : MatchTuning::Shape::ATTACKING_TRANSITION_SUPPORT_ADVANCE));
          target.y += (shapeAnchor.y - target.y) *
                      MatchTuning::Shape::ATTACKING_TRANSITION_BALL_PULL;
          const bool checkingBack =
              transitionRunner &&
              isOffside(player, player.isHomeTeam, lineFor(player.isHomeTeam));
          if (checkingBack)
          {
            target.x -=
                attackDirection * MatchTuning::Shape::ONSIDE_RECOVERY_METRES;
          }
          job = planJob(transitionRunner ? PlanBranch::TRANSITION_RUN
                                         : PlanBranch::TRANSITION_SUPPORT,
                        checkingBack ? 1U : 0U);
        }
        else
        {
          player.intent = PlayerIntent::RECOVER_SHAPE;
          target.x -=
              pitch.atX(attackDirection *
                        MatchTuning::Shape::DEFENSIVE_TRANSITION_RECOVERY);
          job = planJob(PlanBranch::RECOVER);
        }
      }
    }

    // Overrides of the situation's job (step O4). "A high ball coming
    // down": one per side goes for it, by a convention (whoever meets it
    // first, aerialRoles), not his own choice yet; it pre-empts his job.
    // Going for a loose high ball: to the spot, held, or arriving with the
    // ball for a running jump (he runs through it).
    bool throughTarget = false;
    bool challenging = false;
    for (const AerialRole& role : aerialRoles)
    {
      if (role.player != &player)
      {
        continue;
      }
      challenging = true;
      target = clampToPitch(role.meet);
      targetVelocity = {.x = 0.0F, .y = 0.0F};
      pinned = true;
      player.intent = role.contests ? PlayerIntent::CLAIM_LOOSE_BALL
                                    : PlayerIntent::PRESS_BALL;
      throughTarget = role.running;
      job = planJob(PlanBranch::AERIAL_CHALLENGE,
                    (role.contests ? 1U : 0U) | (role.running ? 2U : 0U));
    }

    // Override (step O4): during the opponents' lofted delivery into the
    // box, the defenders hold their marks instead of drifting back into
    // shape; the one going for it (above) is released from his.
    if (getPossessionState().isAerialDelivery && (carrier == nullptr) &&
        !challenging && getPossessionState().passByHome != player.isHomeTeam &&
        player.intent != PlayerIntent::CLAIM_LOOSE_BALL)
    {
      target = player.tacticalTarget;
      targetVelocity = {.x = 0.0F, .y = 0.0F};
      pinned = true;
      job = planJob(PlanBranch::AERIAL_HOLD);
    }

    if (goalkeeper)
    {
      const bool receivingBackPass =
          getPossessionState().isPass &&
          getExpectedReceiver() == player.player &&
          player.intent == PlayerIntent::RECEIVE_PASS;
      player.intent = PlayerIntent::GOALKEEP;
      targetVelocity = {.x = 0.0F, .y = 0.0F};
      pinned = false;
      if (keeperInDive(player))
      {
        continue;
      }
      // A ball going into his own goal that nobody else gets to (user
      // 2026-11-11): once he has reacted, between it and the goal, at
      // once (no hands: it is played with his feet or body).
      GoalkeeperControl& control = keepers[player.isHomeTeam ? 0 : 1];
      const std::optional<PitchPoint> ownGoal = ownGoalThreatBinturong(player);
      if (!ownGoal)
      {
        control.ownGoalThreatSince.reset();
      }
      else if (!control.ownGoalThreatSince)
      {
        control.ownGoalThreatSince = getSimulatedSeconds();
      }
      const float reflexes = std::clamp(
          (player.derivedStat(DerivedStat::REFLEXES) - STAT_1_RATING) /
              (STAT_99_RATING - STAT_1_RATING),
          0.0F, 1.0F);
      using H = MatchTuning::KeeperHands;
      const double reaction =
          H::REACTION_SECONDS_POOR +
          ((H::REACTION_SECONDS_BEST - H::REACTION_SECONDS_POOR) * reflexes);
      // The situation "I'm the keeper" (step O4): his frame. Sweep a ball
      // going into his own goal (open once he has reacted to it), take a
      // team-mate's pass to him (open when it is his: the receive target
      // above stands), or take up his position (always open; his own state
      // machine, goalkeeperTargetCapybara). Follow-up: the stopgap pick is
      // today's order.
      const std::array<bool, KEEPER_JOB_COUNT> keeperOpen{
          ownGoal &&
              getSimulatedSeconds() >= *control.ownGoalThreatSince + reaction,
          receivingBackPass, true};
      const auto keeperJob = static_cast<KeeperJob>(std::distance(
          keeperOpen.begin(), std::ranges::find(keeperOpen, true)));
      noteOffBallCapybara(player, "I'm the keeper", keeperOpen, KEEPER_NAMES,
                          KEEPER_WHEN, static_cast<std::size_t>(keeperJob));
      if (keeperJob == KeeperJob::SWEEP_OWN_GOAL)
      {
        control.state = GoalkeeperState::SWEEP;
        target = clampToPitch(*ownGoal);
        job = planJob(PlanBranch::GOALKEEPER, 1U);
      }
      else if (keeperJob == KeeperJob::POSITION)
      {
        target = goalkeeperTargetCapybara(player, carrier);
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
      const Vector2F moved = target - player.plannedTarget;
      const float seconds = static_cast<float>(sincePlan) *
                            MatchTuning::Timing::FIXED_STEP_SECONDS;
      targetVelocity = {.x = moved.x / seconds, .y = moved.y / seconds};
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
    // No route planning yet (overtakes are added after). His body (facing
    // the ball plan step 1): a run, a chase, a pass to receive, a man he
    // tracks at a run, or ahead of the ball, and his body follows his run;
    // otherwise, goal-side of the ball, he faces it while he can keep up
    // with his job that way.
    player.routeWaypoints = 0;
    if (!goalkeeper && &player != carrier)
    {
      planOffBallFacingCapybara(
          player, target, targetVelocity, pressurePosition,
          offBallRunCapybara(player, targetVelocity, challenging, looseChase,
                             pinned));
    }
    else
    {
      // Keepers (their own state machine) and the carrier: not asked, and
      // not left turned for when he is next off the ball.
      player.facingRequested = false;
      if (!facingTurnedCapybara.empty())
      {
        facingTurnedCapybara.at(playerIndexCapybara(player)) = 0;
      }
    }
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
    // The brain flow tool (step O5): where his frame sends him, and what
    // overrode its pick.
    if (offBallRecord != nullptr && player.player != nullptr &&
        player.player->getId() == offBallRecord->offBall.player)
    {
      if (&player == carrier)
      {
        offBallRecord->offBall.situation =
            "I have the ball (the carrier decision)";
        offBallRecord->offBall.options.clear();
      }
      if (challenging)
      {
        offBallRecord->offBall.overriddenBy =
            "going for the high ball (a convention: whoever meets it first)";
      }
      else if (job == planJob(PlanBranch::AERIAL_HOLD))
      {
        offBallRecord->offBall.overriddenBy =
            "holding his mark during their delivery into the box";
      }
      offBallRecord->offBall.target = target;
      offBallRecord->offBall.filled = true;
    }
  }
  // Step O6: first to commit (shadow), with this step's shape spots.
  if (looseShadowRan)
  {
    std::vector<PreciseMeetCapybara> precise(players.size());
    for (std::size_t index = 0; index < players.size(); ++index)
    {
      if (looseCommitsCapybara.at(index).committed &&
          players.at(index).isHomeTeam == planHome)
      {
        const auto [seconds, at] = interception(players.at(index));
        precise.at(index) = {.seconds = seconds, .at = at};
      }
    }
    looseCommitStageCapybara(CommitKindCapybara::LOOSE, planHome, precise,
                             (planHome ? homePressers : awayPressers).front());
    // The brain flow tool: the example player's sequence, projected.
    for (std::size_t index = 0;
         offBallRecord != nullptr && index < players.size(); ++index)
    {
      const MatchPlayer& player = players.at(index);
      if (player.player == nullptr || player.isHomeTeam != planHome ||
          player.player->getId() != offBallRecord->offBall.player)
      {
        continue;
      }
      const std::size_t partner =
          loosePartnerCapybara(CommitKindCapybara::LOOSE, index);
      const auto [hisSeconds, hisAt] = interception(player);
      PreciseMeetCapybara theirs{};
      if (partner != SIZE_MAX)
      {
        const auto [seconds, at] = interception(players.at(partner));
        theirs = {.seconds = seconds, .at = at};
      }
      noteLooseCommitCapybara(CommitKindCapybara::LOOSE, index, partner,
                              {.seconds = hisSeconds, .at = hisAt}, theirs);
    }
  }
  // Step P3: first to commit on the press (shadow), with this step's shape
  // spots; the detailed look uses the planner's pursuit time (no eagerness
  // or continuity), collected where the carrier is.
  if (pressShadowRan)
  {
    std::vector<PreciseMeetCapybara> precise(players.size());
    for (std::size_t index = 0; index < players.size(); ++index)
    {
      if (players.at(index).isHomeTeam == planHome)
      {
        precise.at(index) = {.seconds = pressPursuitSeconds(players.at(index)),
                             .at = pressurePosition};
      }
    }
    const MatchPlayer* teamPick =
        (planHome ? homePressers : awayPressers).front();
    looseCommitStageCapybara(CommitKindCapybara::PRESS, planHome, precise,
                             teamPick);
    tallyPressGoersCapybara(planHome, teamPick);
    // Step P4: the second man (shadow), where today's second man has a job.
    tallyPressSecondCapybara(*carrier, planHome ? homePressers : awayPressers,
                             planHome ? homeCoverOutlet : awayCoverOutlet,
                             (planHome ? homeSliders : awaySliders).pressing,
                             precise);
    for (std::size_t index = 0;
         offBallRecord != nullptr && index < players.size(); ++index)
    {
      const MatchPlayer& player = players.at(index);
      if (player.player == nullptr || player.isHomeTeam != planHome ||
          player.player->getId() != offBallRecord->offBall.player)
      {
        continue;
      }
      const std::size_t partner =
          loosePartnerCapybara(CommitKindCapybara::PRESS, index);
      noteLooseCommitCapybara(
          CommitKindCapybara::PRESS, index, partner, precise.at(index),
          partner != SIZE_MAX ? precise.at(partner) : PreciseMeetCapybara{});
    }
  }
}

void MatchEngine::noteOffBallCapybara(const MatchPlayer& player,
                                      const char* situation,
                                      std::span<const bool> open,
                                      std::span<const char* const> names,
                                      std::span<const char* const> opensWhen,
                                      std::size_t chosen)
{
  if (offBallRecord == nullptr || player.player == nullptr ||
      player.player->getId() != offBallRecord->offBall.player)
  {
    return;
  }
  offBallRecord->offBall.situation = situation;
  offBallRecord->offBall.options.clear();
  for (std::size_t index = 0; index < open.size(); ++index)
  {
    offBallRecord->offBall.options.push_back({.name = names[index],
                                              .open = open[index],
                                              .opensWhen = opensWhen[index]});
  }
  offBallRecord->offBall.chosen = chosen;
  offBallRecord->offBall.overriddenBy = nullptr;
}

namespace
{
// Facing the ball plan step 1 (the user, 2026-11-25; a design choice, not
// researched): turned to run, he faces the ball again once his job asks
// under this share of what he can manage facing it, so he doesn't flick
// between the two.
constexpr float FACING_RETURN_SHARE = 0.8F;
}  // namespace

MatchEngine::OffBallFacingCapybara MatchEngine::offBallFacingCapybara(
    const MatchPlayer& player, PitchPoint target, Vector2F targetVelocity,
    PitchPoint ballAt)
{
  if (facingTurnedCapybara.size() != players.size())
  {
    facingTurnedCapybara.assign(players.size(), 0);
  }
  std::uint8_t& turned = facingTurnedCapybara.at(playerIndexCapybara(player));
  OffBallFacingCapybara result;
  const Vector2F toBall = ballAt - player.position;
  const float ballMetres = length(toBall);
  result.goalSide = pitch.fromOwnGoalLine(player.position, player.isHomeTeam) <
                    pitch.fromOwnGoalLine(ballAt, player.isHomeTeam);
  if (!result.goalSide || ballMetres <= EPSILON)
  {
    turned = 0;
    return result;
  }
  // The speed his job asks: what his job's movement mode asks at this
  // distance (the body's modeTargetSpeed), held to what he can stop in at
  // the body's arrival deceleration (its floor), toward his target, plus
  // the target's own movement (as the body follows a moving target).
  const MovementStyle& style = movementStyle(player.isHomeTeam);
  const auto mode =
      style.intentModes.at(static_cast<std::size_t>(player.intent));
  const SpeedBandInputs inputs = speedBandInputs(player);
  const SpeedBandTops bands =
      speedBandTops(inputs.transition, inputs.critical, inputs.top);
  const std::array<float, 4> tops{bands.walk, bands.jog, bands.run, inputs.top};
  const Vector2F toTarget = target - player.position;
  const float remaining = length(toTarget);
  const float closing =
      remaining > EPSILON
          ? std::min(
                modeTargetSpeed(mode, remaining, style.jogRampMetres, tops),
                std::sqrt(2.0F * MatchTuning::Player::ARRIVAL_DECELERATION *
                          remaining))
          : 0.0F;
  const float share = remaining > EPSILON ? closing / remaining : 0.0F;
  const Vector2F wanted{.x = targetVelocity.x + (toTarget.x * share),
                        .y = targetVelocity.y + (toTarget.y * share)};
  result.asked = length(wanted);
  // The most he can manage that way facing the ball: the body's direction
  // speeds (forward, diagonal, sideways, back) at the angle between his
  // way and the ball, blended as the body blends them.
  using P = MatchTuning::Player;
  const float alignment =
      result.asked > EPSILON
          ? std::clamp(((wanted.x * toBall.x) + (wanted.y * toBall.y)) /
                           (result.asked * ballMetres),
                       -1.0F, 1.0F)
          : 1.0F;
  const DirectionMix mix = directionMix(std::acos(alignment));
  const auto directionCap = [&](std::size_t direction)
  {
    return P::DIRECTION_SPEED_SHARE.at(direction) *
           tops.at(
               static_cast<std::size_t>(P::DIRECTION_TOP_BAND.at(direction)));
  };
  result.cap =
      directionCap(mix.below) +
      ((directionCap(mix.below + 1) - directionCap(mix.below)) * mix.onward);
  // Only an urgent job turns him (the user, 2026-11-25, option A): on a
  // walk or jog job he keeps facing the ball and takes the slower speed
  // (shuffling back into shape); a run or sprint job turns him when it asks
  // more than he can manage facing it.
  const bool urgent = mode == MovementMode::RUN || mode == MovementMode::SPRINT;
  turned = static_cast<std::uint8_t>(
      urgent && (turned != 0 ? result.asked >= FACING_RETURN_SHARE * result.cap
                             : result.asked > result.cap));
  result.turned = turned != 0;
  if (!result.turned)
  {
    result.angle = std::atan2(toBall.y, toBall.x);
  }
  return result;
}

MatchEngine::OffBallRunCapybara MatchEngine::offBallRunCapybara(
    const MatchPlayer& player, Vector2F targetVelocity, bool challenging,
    bool looseChase, bool pinned) const
{
  using R = OffBallRunCapybara;
  if (challenging)
  {
    return R::AERIAL;
  }
  if (looseChase || player.intent == PlayerIntent::CLAIM_LOOSE_BALL)
  {
    return R::CHASE;
  }
  if (player.intent == PlayerIntent::RECEIVE_PASS)
  {
    return R::RECEIVE;
  }
  if (player.isMakingRun)
  {
    return R::RUN;
  }
  // Tracking a runner: tight on his man while the man runs (over a jog).
  const float jogTop =
      speedBandFrom(MovementMode::JOG, speedBandInputs(player)).high;
  if (player.intent == PlayerIntent::MARK_OPPONENT && pinned &&
      length(targetVelocity) > jogTop)
  {
    return R::TRACKS;
  }
  return R::NONE;
}

void MatchEngine::planOffBallFacingCapybara(MatchPlayer& player,
                                            PitchPoint target,
                                            Vector2F targetVelocity,
                                            PitchPoint ballAt,
                                            OffBallRunCapybara run)
{
  using R = OffBallRunCapybara;
  OffBallFacingCapybara facing;
  if (run == R::NONE)
  {
    facing = offBallFacingCapybara(player, target, targetVelocity, ballAt);
  }
  else
  {
    facing.goalSide =
        pitch.fromOwnGoalLine(player.position, player.isHomeTeam) <
        pitch.fromOwnGoalLine(ballAt, player.isHomeTeam);
    if (!facingTurnedCapybara.empty())
    {
      facingTurnedCapybara.at(playerIndexCapybara(player)) = 0;
    }
  }
  player.facingRequested = facing.angle.has_value();
  player.facingRequest = facing.angle.value_or(0.0F);
  // The lab's tallies (facing_*), goal-side of the ball only.
  if (facing.goalSide)
  {
    ++stats.facingGoalSide;
    switch (run)
    {
      case R::NONE:
        stats.facingFaces += facing.angle.has_value() ? 1 : 0;
        stats.facingTurned += facing.turned ? 1 : 0;
        stats.facingTurnedPressing +=
            facing.turned && player.intent == PlayerIntent::PRESS_BALL ? 1 : 0;
        break;
      case R::AERIAL:
        ++stats.facingAerial;
        break;
      case R::CHASE:
        ++stats.facingChase;
        break;
      case R::RECEIVE:
        ++stats.facingReceive;
        break;
      case R::RUN:
        ++stats.facingRun;
        break;
      case R::TRACKS:
        ++stats.facingTracks;
        break;
    }
  }
  static constexpr std::array<const char*, OFF_BALL_RUN_COUNT> RUN_NAMES{
      "",
      "going up for the high ball",
      "chasing the loose ball",
      "receiving a pass",
      "making a run",
      "tracking a runner (his man over a jog)"};
  const char* why = RUN_NAMES.at(static_cast<std::size_t>(run));
  if (run == R::NONE)
  {
    if (facing.angle.has_value())
    {
      why = "facing the ball";
    }
    else if (facing.turned)
    {
      why =
          "turned to run (his run job asks more speed than he has facing "
          "the ball)";
    }
    else
    {
      why = "ahead of the ball (his run)";
    }
  }
  noteOffBallFacingCapybara(player, why, facing);
}

void MatchEngine::noteOffBallFacingCapybara(const MatchPlayer& player,
                                            const char* why,
                                            const OffBallFacingCapybara& facing)
{
  if (offBallRecord == nullptr || player.player == nullptr ||
      player.player->getId() != offBallRecord->offBall.player)
  {
    return;
  }
  offBallRecord->offBall.facing = why;
  offBallRecord->offBall.facingChecked = facing.goalSide;
  offBallRecord->offBall.facingAsked = facing.asked;
  offBallRecord->offBall.facingCap = facing.cap;
  offBallRecord->offBall.facingTurned = facing.turned;
}

bool MatchEngine::brainFlowOffBallCapybara(const MatchScenario& scenario,
                                           float matchMinute, int homeGoals,
                                           int awayGoals,
                                           BrainFlow::CarrierRecord& record)
{
  record.offBall.filled = false;
  record.offBall.pressCall = nullptr;
  record.offBall.facing = nullptr;
  record.offBall.facingChecked = false;
  record.offBall.pressSecond = nullptr;
  record.offBall.looseCall = nullptr;
  record.offBall.meetPoints.clear();
  record.offBall.commit.reset();
  if (scenario.players.empty() || state == MatchState::FULL_TIME ||
      !placeScenario(scenario, MatchState::PLAYING, matchMinute, homeGoals,
                     awayGoals, true))
  {
    return false;
  }
  offBallRecord = &record;
  assignMarks(true);
  assignMarks(false);
  refreshTacticalTargets(MatchTuning::Timing::FIXED_STEP_SECONDS, true);
  refreshTacticalTargets(MatchTuning::Timing::FIXED_STEP_SECONDS, false);
  offBallRecord = nullptr;
  return record.offBall.filled;
}

namespace
{
// Brain redesign plan section 27 (step O6), our estimates (to tune): a
// rival beating him to it by more than the margin makes it certain it is
// not his (the margin: this share of his time to it, at most this; the
// user, 2026-11-23: about how finely people tell two arrival times apart,
// 7-13% in time-to-contact studies, from memory, unchecked); he compares
// himself with this many of the players closest to the earliest point; at
// or below this height a ball is comfortable to control; at or under this
// pace (m/s) on the ground it has settled.
constexpr float LOOSE_SHADOW_MARGIN_SECONDS = 0.5F;
constexpr float LOOSE_MARGIN_SHARE = 0.10F;
constexpr std::size_t LOOSE_SHADOW_RIVALS = 3;
// Seeing a team-mate also going (the user, 2026-11-23, our estimates): in
// view while he looks at the ball (within this angle of his line to it),
// or alongside him (touch circles meeting, 2 m, running within 30 deg of
// his way): he knows at once, no look-up delay.
constexpr float SEES_WHILE_LOOKING_DEGREES = 60.0F;
constexpr float ALONGSIDE_METRES = 2.0F;
constexpr float ALONGSIDE_HEADING_DEGREES = 30.0F;

// The margin for a chase that takes him this long.
float looseMargin(float seconds)
{
  return std::min(LOOSE_SHADOW_MARGIN_SECONDS, LOOSE_MARGIN_SHARE * seconds);
}
constexpr float COMFORTABLE_CONTROL_METRES = 1.0F;
constexpr float SETTLED_BALL_SPEED = 2.0F;
constexpr float GROUNDED_METRES = 0.01F;

float metresBetween(Vector2F a, Vector2F b)
{
  return length(Vector2F{.x = a.x - b.x, .y = a.y - b.y});
}

// Distance from a point to the segment from start to end.
float metresToSegment(Vector2F point, Vector2F start, Vector2F end)
{
  const Vector2F along{.x = end.x - start.x, .y = end.y - start.y};
  const float squared = (along.x * along.x) + (along.y * along.y);
  if (squared <= EPSILON)
  {
    return metresBetween(point, start);
  }
  const float share = std::clamp(((point.x - start.x) * along.x +
                                  (point.y - start.y) * along.y) /
                                     squared,
                                 0.0F, 1.0F);
  return metresBetween(point, Vector2F{.x = start.x + (along.x * share),
                                       .y = start.y + (along.y * share)});
}
}  // namespace

MatchEngine::LooseCallCapybara MatchEngine::judgeLooseCallCapybara(
    float mine, float bestRival)
{
  if (mine <= bestRival)
  {
    return LooseCallCapybara::MINE;
  }
  if (bestRival + looseMargin(mine) < mine)
  {
    return LooseCallCapybara::NOT_MINE;
  }
  return LooseCallCapybara::IN_DOUBT;
}

float MatchEngine::roughSecondsCapybara(const MatchPlayer& player,
                                        Vector2F at) const
{
  // Step O6, the rough look: his run in a straight line at his top speed,
  // plus his way already moving toward it (weighted as in the planner's
  // path search), plus the time to turn to face it (the engine's facing
  // turn rate).
  const Vector2F from = asVector(player.position);
  const Vector2F toPoint{.x = at.x - from.x, .y = at.y - from.y};
  const float metres = length(toPoint);
  const float run =
      std::max(0.0F, metres - MatchTuning::Ball::OUTFIELD_CONTROL_RADIUS_METRES);
  if (metres <= EPSILON)
  {
    return 0.0F;
  }
  const Vector2F way{.x = toPoint.x / metres, .y = toPoint.y / metres};
  const float toward = std::max(
      0.0F, (player.velocity.x * way.x) + (player.velocity.y * way.y));
  const float speed = std::max(
      currentTopSpeed(player) +
          (toward * MatchTuning::Player::CURRENT_VELOCITY_PURSUIT_WEIGHT),
      MatchTuning::Player::MINIMUM_PURSUIT_SPEED);
  const float facingCosine =
      std::clamp((std::cos(player.facingAngle) * way.x) +
                     (std::sin(player.facingAngle) * way.y),
                 -1.0F, 1.0F);
  const float turn = std::acos(facingCosine) /
                     MatchTuning::Player::FACING_TURN_RATE_RADIANS;
  return (run / speed) + turn;
}

std::pair<std::size_t, float> MatchEngine::roughMeetCapybara(
    const MatchPlayer& player, std::span<const LoosePathPointCapybara> path,
    std::size_t from) const
{
  const float headLine = bodyLines(player).head;
  for (std::size_t index = from; index < path.size(); ++index)
  {
    const LoosePathPointCapybara& point = path[index];
    const float rough = roughSecondsCapybara(player, point.at);
    if (point.height <= headLine && rough <= point.seconds)
    {
      return {index, std::max(rough, point.seconds)};
    }
  }
  const LoosePathPointCapybara& rest = path.back();
  return {path.size() - 1,
          std::max(roughSecondsCapybara(player, rest.at), rest.seconds)};
}

MatchEngine::LooseCallResultCapybara MatchEngine::looseCallCapybara(
    const MatchPlayer& player, std::span<const LoosePathPointCapybara> path,
    const MatchPlayer* watch) const
{
  // The earliest point: the first he could play it (under his head line).
  const float headLine = bodyLines(player).head;
  std::size_t earliest = path.size() - 1;
  for (std::size_t index = 0; index < path.size(); ++index)
  {
    if (path[index].height <= headLine)
    {
      earliest = index;
      break;
    }
  }
  const LoosePathPointCapybara& first = path[earliest];
  // 1. Can he get there before the few players closest to it?
  std::array<float, LOOSE_SHADOW_RIVALS> closestMetres{};
  closestMetres.fill(std::numeric_limits<float>::max());
  std::array<const MatchPlayer*, LOOSE_SHADOW_RIVALS> closest{};
  for (const MatchPlayer& other : players)
  {
    if (&other == &player || !active(other) || other.isGoalkeeper)
    {
      continue;
    }
    float metres = metresBetween(asVector(other.position), first.at);
    const MatchPlayer* who = &other;
    for (std::size_t place = 0; place < LOOSE_SHADOW_RIVALS; ++place)
    {
      if (metres < closestMetres.at(place))
      {
        std::swap(metres, closestMetres.at(place));
        std::swap(who, closest.at(place));
      }
    }
  }
  float bestRival = std::numeric_limits<float>::max();
  for (const MatchPlayer* rival : closest)
  {
    if (rival != nullptr)
    {
      bestRival = std::min(bestRival, roughSecondsCapybara(*rival, first.at));
    }
  }
  const float mine = roughSecondsCapybara(player, first.at);
  if (std::min(mine, bestRival) <= first.seconds)
  {
    return {.call = judgeLooseCallCapybara(mine, bestRival),
            .laterPoint = false,
            .meetIndex = earliest,
            .watchConsidered =
                std::ranges::find(closest, watch) != closest.end()};
  }
  // 2. Nobody gets there in time: his own earliest point, against the
  // players in contention (as close as him to it, or to the line from the
  // earliest point to it).
  const auto [meetIndex, meetSeconds] =
      roughMeetCapybara(player, path, earliest);
  const Vector2F meetAt = path[meetIndex].at;
  const float reach = metresBetween(asVector(player.position), meetAt);
  float bestContender = std::numeric_limits<float>::max();
  bool watchConsidered = false;
  for (const MatchPlayer& other : players)
  {
    if (&other == &player || !active(other) || other.isGoalkeeper)
    {
      continue;
    }
    const Vector2F at = asVector(other.position);
    if (metresBetween(at, meetAt) <= reach ||
        metresToSegment(at, first.at, meetAt) <= reach)
    {
      bestContender = std::min(bestContender,
                               roughMeetCapybara(other, path, earliest).second);
      watchConsidered = watchConsidered || &other == watch;
    }
  }
  return {.call = judgeLooseCallCapybara(meetSeconds, bestContender),
          .laterPoint = true,
          .meetIndex = meetIndex,
          .watchConsidered = watchConsidered};
}

MatchEngine::LooseShadowOutcomeCapybara MatchEngine::shadowLooseBallCapybara(
    bool home, std::span<const LoosePathPointCapybara> path,
    const MatchPlayer* teamPick)
{
  LooseShadowOutcomeCapybara outcome;
  // A new loose ball (or the first; the planner runs every few steps): the
  // commitments start afresh.
  if (looseCommitsCapybara.size() != players.size() ||
      stepCounter > looseShadowStepCapybara +
                        MatchTuning::Timing::TACTICAL_REFRESH_STEPS)
  {
    looseCommitsCapybara.assign(players.size(), {});
    looseCallsNowCapybara.assign(players.size(), LooseCallCapybara::NOT_MINE);
    shapeSpotsCapybara.assign(players.size(), PitchPoint{});
  }
  looseShadowStepCapybara = stepCounter;
  if (!getPossessionState().isPass)
  {
    looseShadowSideStepCapybara.at(home ? 0 : 1) = stepCounter;
  }
  int mine = 0;
  int inDoubt = 0;
  bool theirs = false;
  const MatchPlayer* mineWho = nullptr;
  for (const MatchPlayer& player : players)
  {
    if (!active(player) || player.isGoalkeeper)
    {
      continue;
    }
    const LooseCallResultCapybara result =
        looseCallCapybara(player, path, teamPick);
    looseCallsNowCapybara.at(playerIndexCapybara(player)) = result.call;
    if (player.isHomeTeam != home)
    {
      theirs = theirs || result.call == LooseCallCapybara::MINE;
      continue;
    }
    ++stats.looseShadowCalls;
    if (result.laterPoint)
    {
      ++stats.looseShadowLaterPoint;
    }
    if (result.call == LooseCallCapybara::MINE)
    {
      ++mine;
      mineWho = &player;
      outcome.result = result;
    }
    else if (result.call == LooseCallCapybara::IN_DOUBT)
    {
      ++inDoubt;
    }
    noteLooseCallCapybara(player, path, result);
  }
  ++stats.looseShadowChecks;
  if (mine >= 2)
  {
    ++stats.looseShadowTwoGo;
  }
  else if (mine == 1)
  {
    ++(mineWho == teamPick ? stats.looseShadowAgree
                           : stats.looseShadowWrongPlayer);
  }
  else
  {
    ++(inDoubt > 0 ? stats.looseShadowInDoubt : stats.looseShadowNobody);
    if (inDoubt == 0 && theirs)
    {
      ++stats.looseShadowTheirs;
    }
  }
  outcome.mine = mine == 1 ? mineWho : nullptr;
  return outcome;
}

namespace
{
// Step O6, first to commit (the user, 2026-11-23; our estimates, to tune):
// the look-up delay runs from 1 s at Concentration 1 to 0.1 s at 100, the
// call delay the same by Leadership (stats are 1-99 in the game: the line
// is set at 99); the chance his call is right from 50% at Decisions 1 to
// 100% at 100.
constexpr float STAT_DELAY_AT_1 = 1.0F;
constexpr float STAT_DELAY_AT_100 = 0.1F;
constexpr float STAT_DELAY_AT_99 =
    STAT_DELAY_AT_1 - ((STAT_DELAY_AT_1 - STAT_DELAY_AT_100) * 98.0F / 99.0F);
constexpr float RIGHT_CALL_AT_1 = 0.5F;
constexpr float RIGHT_CALL_AT_99 = 1.0F - (0.5F * 1.0F / 99.0F);

}  // namespace

bool MatchEngine::looseChaserCapybara(const MatchPlayer& player,
                                      const MatchPlayer* teamPick) const
{
  const auto goes = [this](std::size_t index)
  {
    const MatchPlayer& each = players.at(index);
    return active(each) && !each.isGoalkeeper &&
           looseCallsNowCapybara.at(index) != LooseCallCapybara::NOT_MINE &&
           !looseCommitsCapybara.at(index).standsDown;
  };
  bool anyGoes = false;
  for (std::size_t index = 0; index < players.size(); ++index)
  {
    anyGoes = anyGoes ||
              (players.at(index).isHomeTeam == player.isHomeTeam && goes(index));
  }
  return anyGoes ? goes(playerIndexCapybara(player)) : &player == teamPick;
}

bool MatchEngine::seesTeamMateGoingCapybara(CommitKindCapybara kind,
                                            std::size_t index) const
{
  const std::vector<LooseCommitCapybara>& commits = commitsCapybara(kind);
  const MatchPlayer& player = players.at(index);
  const auto unit = [](Vector2F v)
  {
    const float size = length(v);
    return size > EPSILON ? Vector2F{.x = v.x / size, .y = v.y / size}
                          : Vector2F{.x = 0.0F, .y = 0.0F};
  };
  const auto heading = [&unit](const MatchPlayer& who)
  {
    return length(who.velocity) > EPSILON
               ? unit(who.velocity)
               : Vector2F{.x = std::cos(who.facingAngle),
                          .y = std::sin(who.facingAngle)};
  };
  const auto dot = [](Vector2F a, Vector2F b)
  { return (a.x * b.x) + (a.y * b.y); };
  const float viewCosine =
      std::cos(SEES_WHILE_LOOKING_DEGREES * std::numbers::pi_v<float> / 180.0F);
  const float alongsideCosine =
      std::cos(ALONGSIDE_HEADING_DEGREES * std::numbers::pi_v<float> / 180.0F);
  const Vector2F toBall = unit(ball.position - player.position);
  for (std::size_t other = 0; other < players.size(); ++other)
  {
    const MatchPlayer& mate = players.at(other);
    const LooseCommitCapybara& theirs = commits.at(other);
    if (other == index || mate.isHomeTeam != player.isHomeTeam ||
        !theirs.committed || theirs.settled)
    {
      continue;
    }
    const Vector2F toMate = mate.position - player.position;
    const bool inView = dot(toBall, unit(toMate)) >= viewCosine;
    const bool alongside =
        length(toMate) <= ALONGSIDE_METRES &&
        dot(heading(player), heading(mate)) >= alongsideCosine;
    if (inView || alongside)
    {
      return true;
    }
  }
  return false;
}

bool MatchEngine::looseStoodDownCapybara(const MatchPlayer& player) const
{
  return looseCommitsCapybara.at(playerIndexCapybara(player)).standsDown;
}

std::size_t MatchEngine::playerIndexCapybara(const MatchPlayer& player) const
{
  const auto found = std::ranges::find_if(
      players, [&player](const MatchPlayer& each) { return &each == &player; });
  return static_cast<std::size_t>(std::distance(players.begin(), found));
}

void MatchEngine::looseCommitStageCapybara(
    CommitKindCapybara kind, bool home,
    std::span<const PreciseMeetCapybara> precise, const MatchPlayer* teamPick)
{
  const double now = getSimulatedSeconds();
  std::vector<LooseCommitCapybara>& commits = commitsCapybara(kind);
  const std::vector<LooseCallCapybara>& calls = callsNowCapybara(kind);
  for (std::size_t index = 0; index < players.size(); ++index)
  {
    const MatchPlayer& player = players.at(index);
    LooseCommitCapybara& commit = commits.at(index);
    if (player.isHomeTeam != home || !active(player) || player.isGoalkeeper)
    {
      continue;
    }
    const LooseCallCapybara call = calls.at(index);
    // Settled: the call holds, unless the one going for it gives up; then
    // both are free again.
    if (commit.settled)
    {
      if (!commit.standsDown && call == LooseCallCapybara::NOT_MINE)
      {
        if (commit.partner < commits.size() &&
            commits.at(commit.partner).standsDown)
        {
          commits.at(commit.partner) = LooseCommitCapybara{};
        }
        commit = LooseCommitCapybara{};
      }
      continue;
    }
    // He goes for it (mine, or in doubt) and commits; he gives up when it
    // is certainly not his.
    if (!commit.committed)
    {
      if (call != LooseCallCapybara::NOT_MINE)
      {
        commit = LooseCommitCapybara{
            .committed = true,
            .committedAt = now,
            .lookUpAt = now + alongStatScale(player.derivedStat(
                                                 DerivedStat::CONCENTRATION),
                                             STAT_DELAY_AT_1,
                                             STAT_DELAY_AT_99)};
      }
      continue;
    }
    if (call == LooseCallCapybara::NOT_MINE)
    {
      commit = LooseCommitCapybara{};
      continue;
    }
    // A team-mate going whom he can see, or alongside him: no delay.
    if (!commit.lookedUp &&
        (now >= commit.lookUpAt || seesTeamMateGoingCapybara(kind, index)))
    {
      lookUpLooseCapybara(kind, index, precise, now);
    }
  }
  // The calls that are due, the soonest first; each settles its pair.
  for (;;)
  {
    std::size_t first = SIZE_MAX;
    for (std::size_t index = 0; index < players.size(); ++index)
    {
      const LooseCommitCapybara& commit = commits.at(index);
      if (players.at(index).isHomeTeam == home && commit.lookedUp &&
          !commit.settled && commit.callAt <= now &&
          (first == SIZE_MAX || commit.callAt < commits.at(first).callAt))
      {
        first = index;
      }
    }
    if (first == SIZE_MAX)
    {
      break;
    }
    settleLooseCallCapybara(kind, first, teamPick, now);
  }
}

namespace
{
// A fixed number in [0, 1) from three keys (mixHash): a shadow's stand-in
// for a roll, so it draws nothing from the match's streams.
float shadowRoll(std::uint64_t a, std::uint64_t b, std::uint64_t c)
{
  // The top 24 bits, as a share of 2^24.
  constexpr int KEPT_BITS = 24;
  constexpr int DROPPED_BITS = 64 - KEPT_BITS;
  const std::uint64_t hash = mixHash(mixHash(a, b), c);
  return std::ldexp(static_cast<float>(hash >> DROPPED_BITS), -KEPT_BITS);
}
}  // namespace

void MatchEngine::lookUpLooseCapybara(
    CommitKindCapybara kind, std::size_t index,
    std::span<const PreciseMeetCapybara> precise, double now)
{
  std::vector<LooseCommitCapybara>& commits = commitsCapybara(kind);
  const MatchPlayer& player = players.at(index);
  LooseCommitCapybara& commit = commits.at(index);
  // A team-mate also going for it (the nearest, if more than one).
  std::size_t partner = SIZE_MAX;
  float nearest = std::numeric_limits<float>::max();
  for (std::size_t other = 0; other < players.size(); ++other)
  {
    const LooseCommitCapybara& theirs = commits.at(other);
    if (other == index || players.at(other).isHomeTeam != player.isHomeTeam ||
        !theirs.committed || theirs.settled)
    {
      continue;
    }
    const float metres =
        distance(player.position, players.at(other).position);
    if (metres < nearest)
    {
      nearest = metres;
      partner = other;
    }
  }
  if (partner == SIZE_MAX)
  {
    return;  // nobody else going: he looks again next step
  }
  // The detailed look: the clearly quicker takes it; else the one whose
  // shape spot is nearest where it will be collected.
  const float his = precise[index].seconds;
  const float theirs = precise[partner].seconds;
  const PitchPoint collect = precise[index].at;
  bool rightMine = false;
  const float margin = looseMargin(his);
  if (theirs - his > margin)
  {
    rightMine = true;
  }
  else if (his - theirs <= margin)
  {
    rightMine = distance(shapeSpotsCapybara.at(index), collect) <=
                distance(shapeSpotsCapybara.at(partner), collect);
  }
  const float chance =
      alongStatScale(player.derivedStat(DerivedStat::DECISIONS),
                     RIGHT_CALL_AT_1, RIGHT_CALL_AT_99);
  // The press is still a shadow: a hashed roll, nothing drawn.
  const auto idOf = [](const MatchPlayer& who)
  { return who.player != nullptr ? who.player->getId() : 0U; };
  const bool right =
      (kind == CommitKindCapybara::PRESS
           ? shadowRoll(idOf(player), idOf(players.at(partner)), stepCounter)
           : randomFloat(0.0F, 1.0F)) < chance;
  commit.lookedUp = true;
  commit.partner = partner;
  commit.answerMine = right ? rightMine : !rightMine;
  commit.answerRight = right;
  commit.callAt =
      now + alongStatScale(player.derivedStat(DerivedStat::LEADERSHIP),
                           STAT_DELAY_AT_1, STAT_DELAY_AT_99);
}

void MatchEngine::settleLooseCallCapybara(CommitKindCapybara kind,
                                          std::size_t index,
                                          const MatchPlayer* teamPick,
                                          double now)
{
  std::vector<LooseCommitCapybara>& commits = commitsCapybara(kind);
  LooseCommitCapybara& commit = commits.at(index);
  LooseCommitCapybara& mate = commits.at(commit.partner);
  commit.settled = true;
  if (mate.settled)
  {
    return;  // his partner was settled by another call meanwhile
  }
  mate.settled = true;
  mate.partner = index;
  const std::size_t chaser = commit.answerMine ? index : commit.partner;
  commits.at(chaser == index ? commit.partner : index).standsDown = true;
  constexpr double MILLIS = 1000.0;
  const int millis = static_cast<int>(
      (now - std::max(commit.committedAt, mate.committedAt)) * MILLIS);
  if (kind == CommitKindCapybara::PRESS)
  {
    ++stats.pressCallsMade;
    stats.pressCallsRight += commit.answerRight ? 1 : 0;
    stats.pressCallsAgree += &players.at(chaser) == teamPick ? 1 : 0;
    stats.pressCallMillis += millis;
    return;
  }
  ++stats.looseCallsMade;
  if (commit.answerRight)
  {
    ++stats.looseCallsRight;
  }
  if (&players.at(chaser) == teamPick)
  {
    ++stats.looseCallsAgree;
  }
  stats.looseCallMillis += millis;
}

std::size_t MatchEngine::loosePartnerCapybara(CommitKindCapybara kind,
                                              std::size_t index) const
{
  const std::vector<LooseCallCapybara>& calls = callsNowCapybara(kind);
  const MatchPlayer& player = players.at(index);
  std::size_t partner = SIZE_MAX;
  float nearest = std::numeric_limits<float>::max();
  for (std::size_t other = 0; other < players.size(); ++other)
  {
    const MatchPlayer& mate = players.at(other);
    if (other == index || mate.isHomeTeam != player.isHomeTeam ||
        !active(mate) || mate.isGoalkeeper ||
        calls.at(other) == LooseCallCapybara::NOT_MINE)
    {
      continue;
    }
    const float metres = distance(player.position, mate.position);
    if (metres < nearest)
    {
      nearest = metres;
      partner = other;
    }
  }
  return partner;
}

void MatchEngine::noteLooseCommitCapybara(CommitKindCapybara kind,
                                          std::size_t index,
                                          std::size_t partner,
                                          PreciseMeetCapybara his,
                                          PreciseMeetCapybara theirs)
{
  if (callsNowCapybara(kind).at(index) == LooseCallCapybara::NOT_MINE)
  {
    return;
  }
  const MatchPlayer& player = players.at(index);
  const auto delay = [](const MatchPlayer& who, DerivedStat stat)
  {
    return alongStatScale(who.derivedStat(stat), STAT_DELAY_AT_1,
                          STAT_DELAY_AT_99);
  };
  const bool seesAtOnce = seesTeamMateGoingCapybara(kind, index);
  BrainFlow::OffBallRecord::CommitCall commit{
      .press = kind == CommitKindCapybara::PRESS,
      .lookUpSeconds =
          seesAtOnce ? 0.0F : delay(player, DerivedStat::CONCENTRATION),
      .callSeconds = delay(player, DerivedStat::LEADERSHIP),
      .seesAtOnce = seesAtOnce};
  if (partner != SIZE_MAX)
  {
    const MatchPlayer& mate = players.at(partner);
    commit.partner = mate.player != nullptr ? mate.player->getId() : 0U;
    commit.partnerLookUpSeconds = delay(mate, DerivedStat::CONCENTRATION);
    commit.partnerCallSeconds = delay(mate, DerivedStat::LEADERSHIP);
    commit.hisSeconds = his.seconds;
    commit.partnerSeconds = theirs.seconds;
    commit.byShape =
        std::abs(his.seconds - theirs.seconds) <= looseMargin(his.seconds);
    commit.shouldBeHis =
        commit.byShape
            ? distance(shapeSpotsCapybara.at(index), his.at) <=
                  distance(shapeSpotsCapybara.at(partner), his.at)
            : his.seconds < theirs.seconds;
    commit.rightChance =
        alongStatScale(player.derivedStat(DerivedStat::DECISIONS),
                       RIGHT_CALL_AT_1, RIGHT_CALL_AT_99);
    commit.heCalls = commit.lookUpSeconds + commit.callSeconds <=
                     commit.partnerLookUpSeconds + commit.partnerCallSeconds;
  }
  offBallRecord->offBall.commit = commit;
}

void MatchEngine::tallyWrongLooseCallCapybara(
    const LooseCallResultCapybara& result, float shadowSeconds,
    float pickSeconds)
{
  constexpr float NEAR_TIE_SECONDS = 0.1F;
  const float later = shadowSeconds - pickSeconds;
  if (later < -NEAR_TIE_SECONDS)
  {
    ++stats.looseWrongShadowFaster;
    return;
  }
  if (later <= NEAR_TIE_SECONDS)
  {
    ++stats.looseWrongNearTie;
    if (std::abs(later) <= EPSILON)
    {
      ++stats.looseWrongExactTie;
    }
    return;
  }
  ++stats.looseWrongSlower;
  if (later > LOOSE_SHADOW_MARGIN_SECONDS)
  {
    ++stats.looseWrongSlowerBig;
  }
  if (result.laterPoint)
  {
    ++stats.looseWrongSlowerLater;
  }
  if (!result.watchConsidered)
  {
    ++stats.looseWrongSlowerMissed;
  }
}

MatchEngine::LooseCallCapybara MatchEngine::pressCallCapybara(
    const MatchPlayer& player, Vector2F at) const
{
  // Step P1, the rough look (as the loose ball's): can he get to the
  // carrier before the few team-mates nearest him? The carrier's own side
  // does not race him there, so only team-mates count.
  return judgeLooseCallCapybara(roughSecondsCapybara(player, at),
                                quickestMateSecondsCapybara(player, at, false));
}

float MatchEngine::quickestMateSecondsCapybara(const MatchPlayer& player,
                                               Vector2F at,
                                               bool notGoingOnly) const
{
  std::array<float, LOOSE_SHADOW_RIVALS> closestMetres{};
  closestMetres.fill(std::numeric_limits<float>::max());
  std::array<const MatchPlayer*, LOOSE_SHADOW_RIVALS> closest{};
  for (const MatchPlayer& other : players)
  {
    if (&other == &player || !active(other) || other.isGoalkeeper ||
        other.isHomeTeam != player.isHomeTeam ||
        (notGoingOnly && pressCallsNowCapybara.at(playerIndexCapybara(other)) !=
                             LooseCallCapybara::NOT_MINE))
    {
      continue;
    }
    float metres = metresBetween(asVector(other.position), at);
    const MatchPlayer* who = &other;
    for (std::size_t place = 0; place < LOOSE_SHADOW_RIVALS; ++place)
    {
      if (metres < closestMetres.at(place))
      {
        std::swap(metres, closestMetres.at(place));
        std::swap(who, closest.at(place));
      }
    }
  }
  float bestRival = std::numeric_limits<float>::max();
  for (const MatchPlayer* rival : closest)
  {
    if (rival != nullptr)
    {
      bestRival = std::min(bestRival, roughSecondsCapybara(*rival, at));
    }
  }
  return bestRival;
}

const MatchPlayer* MatchEngine::shadowPressCapybara(bool home,
                                                    const MatchPlayer& carrier,
                                                    const MatchPlayer* teamPick)
{
  const Vector2F at = asVector(carrier.position);
  if (pressCommitsCapybara.size() != players.size())
  {
    pressCommitsCapybara.assign(players.size(), {});
    pressCallsNowCapybara.assign(players.size(), LooseCallCapybara::NOT_MINE);
  }
  if (shapeSpotsCapybara.size() != players.size())
  {
    shapeSpotsCapybara.assign(players.size(), PitchPoint{});
  }
  // Step P3: a new carrier (or the first press for a while) is a fresh
  // call; right after the side's own loose ball, its commitments carry
  // over, no new delays (the user, 2026-11-24).
  const std::size_t side = home ? 0 : 1;
  const std::uint32_t carrierId =
      carrier.player != nullptr ? carrier.player->getId() : 0U;
  constexpr auto REFRESH = MatchTuning::Timing::TACTICAL_REFRESH_STEPS;
  if (pressCarrierCapybara.at(side) != carrierId ||
      stepCounter > pressShadowSideStepCapybara.at(side) + REFRESH)
  {
    const bool afterLoose =
        looseCommitsCapybara.size() == players.size() &&
        looseShadowSideStepCapybara.at(side) != 0 &&
        stepCounter <= looseShadowSideStepCapybara.at(side) + REFRESH;
    for (std::size_t index = 0; index < players.size(); ++index)
    {
      if (players.at(index).isHomeTeam != home)
      {
        continue;
      }
      pressCommitsCapybara.at(index) =
          afterLoose ? looseCommitsCapybara.at(index) : LooseCommitCapybara{};
      if (afterLoose && pressCommitsCapybara.at(index).committed)
      {
        ++stats.pressCommitsCarried;
      }
    }
    pressCarrierCapybara.at(side) = carrierId;
    pressSecondHolderCapybara.at(side) = {};
    looseShadowSideStepCapybara.at(side) = 0;  // carried over once
  }
  pressShadowSideStepCapybara.at(side) = stepCounter;
  int mine = 0;
  int inDoubt = 0;
  const MatchPlayer* mineWho = nullptr;
  for (const MatchPlayer& player : players)
  {
    if (!active(player) || player.isGoalkeeper || player.isHomeTeam != home)
    {
      continue;
    }
    const LooseCallCapybara call = pressCallCapybara(player, at);
    pressCallsNowCapybara.at(playerIndexCapybara(player)) = call;
    if (call == LooseCallCapybara::MINE)
    {
      ++mine;
      mineWho = &player;
    }
    else if (call == LooseCallCapybara::IN_DOUBT)
    {
      ++inDoubt;
    }
    notePressCallCapybara(player, call);
  }
  ++stats.pressShadowChecks;
  if (mine >= 2)
  {
    ++stats.pressShadowTwoGo;
  }
  else if (mine == 1)
  {
    ++(mineWho == teamPick ? stats.pressShadowAgree
                           : stats.pressShadowWrongPlayer);
    if (inDoubt > 0)
    {
      ++stats.pressShadowWithDoubt;
    }
  }
  else
  {
    ++stats.pressShadowNobody;
  }
  return mine == 1 ? mineWho : nullptr;
}

void MatchEngine::tallyWrongPressCallCapybara(float shadowSeconds,
                                              float pickSeconds,
                                              bool pickPressing)
{
  constexpr float NEAR_TIE_SECONDS = 0.1F;
  const float later = shadowSeconds - pickSeconds;
  if (later < -NEAR_TIE_SECONDS)
  {
    ++stats.pressWrongShadowFaster;
  }
  else if (later <= NEAR_TIE_SECONDS)
  {
    ++stats.pressWrongNearTie;
  }
  else
  {
    ++stats.pressWrongSlower;
  }
  if (pickPressing)
  {
    ++stats.pressWrongPickPressing;
  }
}

void MatchEngine::tallyPressGoersCapybara(bool home,
                                          const MatchPlayer* teamPick)
{
  int goers = 0;
  const MatchPlayer* goer = nullptr;
  for (std::size_t index = 0; index < players.size(); ++index)
  {
    const MatchPlayer& player = players.at(index);
    if (player.isHomeTeam != home || !pressGoesCapybara(index))
    {
      continue;
    }
    ++goers;
    goer = &player;
  }
  if (goers >= 2)
  {
    ++stats.pressGoSeveral;
  }
  else if (goers == 0)
  {
    ++stats.pressGoNone;
  }
  else if (goer == teamPick)
  {
    ++stats.pressGoAgree;
  }
  else
  {
    ++stats.pressGoOther;
    if (teamPick != nullptr && teamPick->intent == PlayerIntent::PRESS_BALL)
    {
      ++stats.pressGoOtherPickPressing;
    }
  }
}

bool MatchEngine::pressGoesCapybara(std::size_t index) const
{
  const MatchPlayer& player = players.at(index);
  return active(player) && !player.isGoalkeeper &&
         pressCallsNowCapybara.at(index) != LooseCallCapybara::NOT_MINE &&
         !pressCommitsCapybara.at(index).standsDown;
}

MatchEngine::PressSecondCapybara MatchEngine::pressSecondCallCapybara(
    const MatchPlayer& player, Vector2F at) const
{
  const std::size_t self = playerIndexCapybara(player);
  if (!active(player) || player.isGoalkeeper || pressGoesCapybara(self))
  {
    return PressSecondCapybara::NONE;
  }
  if (pressCommitsCapybara.at(self).standsDown)
  {
    return PressSecondCapybara::STOOD_DOWN;
  }
  int goers = 0;
  for (std::size_t index = 0; index < players.size(); ++index)
  {
    if (players.at(index).isHomeTeam != player.isHomeTeam)
    {
      continue;
    }
    if (active(players.at(index)) && pressCommitsCapybara.at(index).standsDown)
    {
      return PressSecondCapybara::NONE;
    }
    goers += pressGoesCapybara(index) ? 1 : 0;
  }
  if (goers != 1)
  {
    return PressSecondCapybara::NONE;
  }
  switch (judgeLooseCallCapybara(roughSecondsCapybara(player, at),
                                 quickestMateSecondsCapybara(player, at, true)))
  {
    case LooseCallCapybara::MINE:
      return PressSecondCapybara::MINE;
    case LooseCallCapybara::IN_DOUBT:
      return PressSecondCapybara::IN_DOUBT;
    case LooseCallCapybara::NOT_MINE:
      break;
  }
  return PressSecondCapybara::NOT_MINE;
}

void MatchEngine::tallyPressSecondCapybara(
    const MatchPlayer& carrier, std::span<MatchPlayer* const, 2> teamPair,
    const MatchPlayer* coverOutlet, float pressing,
    std::span<const PreciseMeetCapybara> precise)
{
  const MatchPlayer* teamSecond = teamPair[1];
  if (teamSecond == nullptr)
  {
    return;
  }
  const bool home = teamSecond->isHomeTeam;
  const Vector2F at = asVector(carrier.position);
  // Today's opening: the order, else the pressing slider; today's order.
  const bool doubling =
      doubleUpPointCapybara(*teamSecond, carrier, teamPair[0]).has_value();
  if (!doubling && pressing <= MatchTuning::Shape::COVER_PRESS_MINIMUM)
  {
    return;
  }
  ++stats.pressSecondChecks;
  if (doubling)
  {
    ++stats.pressSecondDoubleUp;
  }
  else
  {
    ++(coverOutlet != nullptr ? stats.pressSecondBlockLane
                              : stats.pressSecondCover);
  }
  const PressSecondsCapybara calls = pressSecondsCapybara(home, at);
  const bool several = calls.goers >= 2;
  if (calls.seconds >= 2)
  {
    ++stats.pressSecondSeveral;
  }
  else if (calls.seconds == 1)
  {
    stats.pressSecondWithDoubt += calls.inDoubt ? 1 : 0;
    stats.pressSecondStoodDown += calls.stoodDown ? 1 : 0;
    tallyOneSecondCapybara(calls.second,
                           calls.goers == 1 ? calls.goer : SIZE_MAX, at,
                           teamPair, precise);
  }
  else if (several)
  {
    ++stats.pressSecondHeld;
    stats.pressSecondHeldTeamGoing +=
        pressGoesCapybara(playerIndexCapybara(*teamSecond)) ? 1 : 0;
  }
  else
  {
    ++stats.pressSecondNone;
  }
  tallySecondSwitchesCapybara(
      home, calls.seconds == 1 ? &players.at(calls.second) : nullptr,
      teamSecond);
}

MatchEngine::PressSecondsCapybara MatchEngine::pressSecondsCapybara(bool home,
                                                                    Vector2F at)
{
  PressSecondsCapybara result;
  for (std::size_t index = 0; index < players.size(); ++index)
  {
    const MatchPlayer& player = players.at(index);
    if (player.isHomeTeam != home)
    {
      continue;
    }
    if (pressGoesCapybara(index))
    {
      ++result.goers;
      result.goer = index;
    }
    const PressSecondCapybara call = pressSecondCallCapybara(player, at);
    result.inDoubt = result.inDoubt || call == PressSecondCapybara::IN_DOUBT;
    result.stoodDown =
        result.stoodDown || call == PressSecondCapybara::STOOD_DOWN;
    if (call == PressSecondCapybara::STOOD_DOWN ||
        call == PressSecondCapybara::MINE)
    {
      ++result.seconds;
      result.second = index;
    }
    notePressSecondCapybara(player, call);
  }
  return result;
}

void MatchEngine::tallySecondSwitchesCapybara(bool home,
                                              const MatchPlayer* shadowSecond,
                                              const MatchPlayer* teamSecond)
{
  // How often the job changes hands while one opponent has the ball.
  const auto changed = [](std::uint32_t& holder, const MatchPlayer* who)
  {
    const std::uint32_t id =
        who != nullptr && who->player != nullptr ? who->player->getId() : 0U;
    if (id == 0U)
    {
      return 0;
    }
    const int switched = holder != 0U && holder != id ? 1 : 0;
    holder = id;
    return switched;
  };
  auto& [shadowHolder, teamHolder] = pressSecondHolderCapybara.at(home ? 0 : 1);
  stats.pressSecondSwitches += changed(shadowHolder, shadowSecond);
  stats.pressSecondTeamSwitches += changed(teamHolder, teamSecond);
}

void MatchEngine::tallyOneSecondCapybara(
    std::size_t second, std::size_t goer, Vector2F carrierAt,
    std::span<MatchPlayer* const, 2> teamPair,
    std::span<const PreciseMeetCapybara> precise)
{
  const MatchPlayer& who = players.at(second);
  const MatchPlayer* teamFirst = teamPair[0];
  const MatchPlayer* teamSecond = teamPair[1];
  const bool agree = &who == teamSecond;
  const bool stoodDown = pressCommitsCapybara.at(second).standsDown;
  if (agree)
  {
    ++stats.pressSecondAgree;
    stats.pressSecondStoodDownAgree += stoodDown ? 1 : 0;
  }
  else if (&who == teamFirst)
  {
    // The team's first presser: is the team's second the one going?
    ++stats.pressSecondTeamFirst;
    stats.pressSecondSwapped +=
        goer != SIZE_MAX && &players.at(goer) == teamSecond ? 1 : 0;
  }
  else
  {
    ++stats.pressSecondOther;
    constexpr float NEAR_TIE_SECONDS = 0.1F;
    const float later = precise[second].seconds -
                        precise[playerIndexCapybara(*teamSecond)].seconds;
    if (later < -NEAR_TIE_SECONDS)
    {
      ++stats.pressSecondOtherFaster;
    }
    else if (later <= NEAR_TIE_SECONDS)
    {
      ++stats.pressSecondOtherNearTie;
    }
    else
    {
      ++stats.pressSecondOtherSlower;
      stats.pressSecondOtherSlowerStoodDown += stoodDown ? 1 : 0;
    }
  }
  if (goer != SIZE_MAX && &players.at(goer) == teamFirst)
  {
    ++stats.pressSecondSameFirst;
    stats.pressSecondSameFirstAgree += agree ? 1 : 0;
  }
  if (!stoodDown)
  {
    tallyNearestSecondCapybara(second, carrierAt, teamSecond);
  }
}

void MatchEngine::tallyNearestSecondCapybara(std::size_t second,
                                             Vector2F carrierAt,
                                             const MatchPlayer* teamSecond)
{
  // The plan's wording, the nearest: is he the nearest of those not going?
  const MatchPlayer& who = players.at(second);
  float nearest = std::numeric_limits<float>::max();
  std::size_t nearestIndex = second;
  for (std::size_t index = 0; index < players.size(); ++index)
  {
    const MatchPlayer& player = players.at(index);
    const float metres = metresBetween(asVector(player.position), carrierAt);
    if (player.isHomeTeam == who.isHomeTeam && active(player) &&
        !player.isGoalkeeper && !pressGoesCapybara(index) && metres < nearest)
    {
      nearest = metres;
      nearestIndex = index;
    }
  }
  ++stats.pressSecondOwnCall;
  stats.pressSecondOwnCallAgree += &who == teamSecond ? 1 : 0;
  stats.pressSecondIsNearest += nearestIndex == second ? 1 : 0;
  stats.pressSecondNearestAgree +=
      &players.at(nearestIndex) == teamSecond ? 1 : 0;
}

void MatchEngine::notePressSecondCapybara(const MatchPlayer& player,
                                          PressSecondCapybara call)
{
  if (offBallRecord == nullptr || player.player == nullptr ||
      player.player->getId() != offBallRecord->offBall.player)
  {
    return;
  }
  constexpr std::array<const char*, 5> CALLS = {
      nullptr, "his (a team-mate's call stood him down)",
      "his (the quickest of those not going)",
      "in doubt (another not going is close): he leaves it",
      "not his (another not going is quicker)"};
  offBallRecord->offBall.pressSecond = CALLS.at(static_cast<std::size_t>(call));
}

void MatchEngine::notePressCallCapybara(const MatchPlayer& player,
                                        LooseCallCapybara call)
{
  if (offBallRecord == nullptr || player.player == nullptr ||
      player.player->getId() != offBallRecord->offBall.player)
  {
    return;
  }
  constexpr std::array<const char*, 3> CALLS = {
      "mine (I get to him first)", "in doubt (a team-mate is close)",
      "not mine (beaten by more than the margin)"};
  offBallRecord->offBall.pressCall = CALLS.at(static_cast<std::size_t>(call));
}

void MatchEngine::noteLooseCallCapybara(
    const MatchPlayer& player, std::span<const LoosePathPointCapybara> path,
    const LooseCallResultCapybara& result)
{
  if (offBallRecord == nullptr || player.player == nullptr ||
      player.player->getId() != offBallRecord->offBall.player)
  {
    return;
  }
  BrainFlow::OffBallRecord& record = offBallRecord->offBall;
  constexpr std::array<const char*, 3> CALLS = {
      "mine (I get there first)", "chasing in doubt (a rival is close)",
      "not mine (beaten by more than the margin)"};
  record.looseCall = CALLS.at(static_cast<std::size_t>(result.call));
  record.meetPoints.clear();
  if (result.call != LooseCallCapybara::MINE)
  {
    return;
  }
  // Where he could meet it: each candidate open when nobody beats him there
  // within the margin (the earliest always). Stopgap pick: the earliest
  // (today's); measures (control against time given away) are a follow-up.
  const std::size_t meet = result.meetIndex;
  const auto firstFrom = [&path, meet](const auto& test)
  {
    for (std::size_t index = std::max<std::size_t>(meet, 1);
         index < path.size(); ++index)
    {
      if (test(index))
      {
        return index;
      }
    }
    return path.size();
  };
  constexpr float TICK = MatchTuning::Timing::FIXED_STEP_SECONDS;
  const std::array<std::pair<const char*, std::size_t>, 4> candidates = {{
      {"earliest", meet},
      {"lower (waist height or under)",
       firstFrom([&path](std::size_t index)
                 { return path[index].height <= COMFORTABLE_CONTROL_METRES; })},
      {"after the bounce",
       firstFrom(
           [&path](std::size_t index)
           {
             return path[index].height <= GROUNDED_METRES &&
                    path[index - 1].height > GROUNDED_METRES;
           })},
      {"settled",
       firstFrom(
           [&path](std::size_t index)
           {
             return path[index].height <= GROUNDED_METRES &&
                    metresBetween(path[index].at, path[index - 1].at) <=
                        SETTLED_BALL_SPEED * TICK;
           })},
  }};
  for (const auto& [name, index] : candidates)
  {
    if (index >= path.size())
    {
      continue;
    }
    const LoosePathPointCapybara& point = path[index];
    const float his =
        std::max(roughSecondsCapybara(player, point.at), point.seconds);
    float rival = std::numeric_limits<float>::max();
    for (const MatchPlayer& other : players)
    {
      if (&other != &player && active(other) && !other.isGoalkeeper)
      {
        rival = std::min(rival, roughSecondsCapybara(other, point.at));
      }
    }
    record.meetPoints.push_back(
        {.name = name,
         .open = index == meet || rival > his + looseMargin(his),
         .seconds = his,
         .height = point.height,
         .margin = rival - his});
  }
}

void MatchEngine::planOvertakeCapybara(std::size_t slot)
{
  // One runner (slot), planned in slot order. Racing someone to the same
  // target (movement targets plan 4a): two players running the same way to
  // about the same point, the one behind
  // closing and about to run into the other (predicted closest approach
  // under the clearance) before he gets there. He goes round, on the
  // shorter side that still wins the race and that nobody else is about to
  // run into; else he carries on (and bumps).
  const auto metresAt = [this](PitchPoint point) { return asVector(point); };
  const auto lengthOf = [](Vector2F v)
  { return std::sqrt((v.x * v.x) + (v.y * v.y)); };
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
      seconds =
          already + lengthOf({.x = at.x - from.x, .y = at.y - from.y}) / speed;
      at = {.x = target.x + (velocity.x * seconds),
            .y = target.y + (velocity.y * seconds)};
    }
    return std::pair{seconds, at};
  };
  // The closest two straight runs come within `window` seconds: one from
  // `a` at `va`, the other from `b` at `vb`.
  const auto closestApproach =
      [&](Vector2F a, Vector2F va, Vector2F b, Vector2F vb, float window)
  {
    const Vector2F r{.x = b.x - a.x, .y = b.y - a.y};
    const Vector2F v{.x = vb.x - va.x, .y = vb.y - va.y};
    const float vv = (v.x * v.x) + (v.y * v.y);
    const float t =
        vv > 1e-6F ? std::clamp(-((r.x * v.x) + (r.y * v.y)) / vv, 0.0F, window)
                   : 0.0F;
    return std::pair{lengthOf({.x = r.x + (v.x * t), .y = r.y + (v.y * t)}), t};
  };
  // When `who` reaches his target from `from` (after `already` seconds) at
  // `speed`, by the waypoints of his route, and where it is then.
  const auto finishTime =
      [&](const MatchPlayer& who, Vector2F from, float already, float speed)
  {
    float seconds = already;
    Vector2F at = from;
    const std::array<Vector2F, 2> waypoints{metresAt(who.movementTarget),
                                            metresAt(who.nextTarget)};
    for (int index = 0; index < std::min<int>(who.routeWaypoints, 2); ++index)
    {
      const Vector2F next = waypoints[static_cast<std::size_t>(index)];
      seconds += lengthOf({.x = next.x - at.x, .y = next.y - at.y}) / speed;
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
  const Vector2F aimed = routed ? metresAt(runner.movementTarget) : target;
  const float toTarget =
      lengthOf({.x = aimed.x - position.x, .y = aimed.y - position.y});
  if (speed < OVERTAKE_MINIMUM_SPEED || toTarget < 1e-3F)
  {
    runner.overtaking = -1;
    return;
  }
  // His run as he means it (straight at where he is running), not as his
  // feet point mid-swerve: judged on that, a way round once found is kept
  // until he is past (judged on his velocity, it switched itself off).
  const Vector2F heading{.x = (aimed.x - position.x) / toTarget,
                         .y = (aimed.y - position.y) / toTarget};
  const Vector2F velocity{.x = heading.x * speed, .y = heading.y * speed};
  // The one he would run into first, if any.
  int ahead = -1;
  float meetAt = 0.0F;
  for (std::size_t other = 0; other < players.size(); ++other)
  {
    const MatchPlayer& leader = players[other];
    if (other == slot || !active(leader))
    {
      continue;
    }
    const float leaderSpeed = lengthOf(leader.velocity);
    if (leaderSpeed < OVERTAKE_MINIMUM_SPEED)
    {
      continue;
    }
    const Vector2F leaderTarget = metresAt(leader.tacticalTarget);
    if (lengthOf(
            {.x = leaderTarget.x - target.x, .y = leaderTarget.y - target.y}) >
        OVERTAKE_SAME_TARGET_METRES)
    {
      continue;
    }
    if ((leader.velocity.x * heading.x + leader.velocity.y * heading.y) /
            leaderSpeed <
        OVERTAKE_SAME_DIRECTION_COSINE)
    {
      continue;
    }
    const Vector2F leaderAt = metresAt(leader.position);
    if ((leaderAt.x - position.x) * heading.x +
            (leaderAt.y - position.y) * heading.y <=
        0.0F)
    {
      continue;
    }
    const auto [closest, when] =
        closestApproach(position, velocity, leaderAt, leader.velocity,
                        std::min(OVERTAKE_HORIZON_SECONDS, toTarget / speed));
    if (closest >= clearanceFor(runner, leader))
    {
      continue;
    }
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
      routed
          ? lengthOf({.x = aimed.x - leaderAt.x, .y = aimed.y - leaderAt.y}) /
                leaderSpeed
          : finishTime(leader, leaderAt, 0.0F, leaderSpeed).first;
  // Where the leader will be when they draw level, and a waypoint beside
  // him there, either side.
  const Vector2F level{.x = leaderAt.x + (leader.velocity.x * meetAt),
                       .y = leaderAt.y + (leader.velocity.y * meetAt)};
  const Vector2F across{.x = -heading.y, .y = heading.x};
  struct Option
  {
    bool ok = false;
    Vector2F waypoint{};
    float metres = 0.0F;
  };
  std::array<Option, 2> options;
  for (std::size_t index = 0; index < 2; ++index)
  {
    const float side = index == 0 ? 1.0F : -1.0F;
    Option& option = options[index];
    option.waypoint = {.x = level.x + (across.x * side * clearance),
                       .y = level.y + (across.y * side * clearance)};
    const Vector2F toWaypoint{.x = option.waypoint.x - position.x,
                              .y = option.waypoint.y - position.y};
    const float firstLeg = lengthOf(toWaypoint);
    const float firstTime = firstLeg / speed;
    // Then on to where he is running (his route's next waypoint, or his
    // target, moving or not).
    const Vector2F onTo =
        routed ? aimed
               : finishTime(runner, option.waypoint, firstTime, speed).second;
    const Vector2F toEnd{.x = onTo.x - option.waypoint.x,
                         .y = onTo.y - option.waypoint.y};
    const float time = firstTime + (lengthOf(toEnd) / speed);
    option.metres = time * speed;
    // He must still win.
    if (time >= leaderTime)
    {
      continue;
    }
    // And nobody else may be about to run into his way round.
    const Vector2F firstVelocity{
        .x = toWaypoint.x / std::max(firstTime, 1e-3F),
        .y = toWaypoint.y / std::max(firstTime, 1e-3F)};
    const float secondTime = std::max(lengthOf(toEnd) / speed, 1e-3F);
    const Vector2F secondVelocity{.x = toEnd.x / secondTime,
                                  .y = toEnd.y / secondTime};
    bool clear = true;
    for (std::size_t other = 0; other < players.size() && clear; ++other)
    {
      const MatchPlayer& third = players[other];
      if (other == slot || other == static_cast<std::size_t>(ahead) ||
          !active(third))
      {
        continue;
      }
      const Vector2F at = metresAt(third.position);
      const float room = clearanceFor(runner, third);
      const float window = std::min(firstTime, OVERTAKE_HORIZON_SECONDS);
      if (closestApproach(position, firstVelocity, at, third.velocity, window)
              .first < room)
      {
        clear = false;
      }
      else if (firstTime < OVERTAKE_HORIZON_SECONDS)
      {
        const Vector2F later{.x = at.x + (third.velocity.x * firstTime),
                             .y = at.y + (third.velocity.y * firstTime)};
        if (closestApproach(
                option.waypoint, secondVelocity, later, third.velocity,
                std::min(secondTime, OVERTAKE_HORIZON_SECONDS - firstTime))
                .first < room)
        {
          clear = false;
        }
      }
    }
    option.ok = clear;
  }
  // The shorter side that works; the one he chose before unless the other
  // is clearly shorter.
  int chosen = -1;
  for (int index = 0; index < 2; ++index)
  {
    if (options[static_cast<std::size_t>(index)].ok &&
        (chosen < 0 || options[static_cast<std::size_t>(index)].metres <
                           options[static_cast<std::size_t>(chosen)].metres))
    {
      chosen = index;
    }
  }
  if (chosen >= 0 && runner.overtaking == ahead && runner.overtakeSide != 0)
  {
    const int previous = runner.overtakeSide > 0 ? 0 : 1;
    if (previous != chosen && options[static_cast<std::size_t>(previous)].ok &&
        options[static_cast<std::size_t>(previous)].metres <=
            options[static_cast<std::size_t>(chosen)].metres +
                OVERTAKE_STICKY_METRES)
    {
      chosen = previous;
    }
  }
  if (chosen < 0)
  {
    runner.overtaking = -1;
    return;
  }
  const Option& option = options[static_cast<std::size_t>(chosen)];
  runner.overtaking = static_cast<std::int8_t>(ahead);
  runner.overtakeSide = chosen == 0 ? 1 : -1;
  const PitchPoint waypoint = asPoint(option.waypoint);
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
    runner.routeApexOffset = 0.0F;
    runner.routeOutwardRoom = clearance;
  }
}

void MatchEngine::planWayRoundCapybara(std::size_t slot)
{
  // A loose-ball chaser, or a player moving to hold shape, offer support
  // or recover shape
  // (way-round plan): the first player not chasing the ball he would come
  // within the clearance of, before he gets where he is going and short of
  // its last metres, is gone round by one waypoint beside where
  // that player will be then: on the side that makes the shorter run, on
  // the pitch, clear of everyone else (the man passed included, on the way
  // back) and, chasing, still his ball. Neither side: he carries on (and may
  // bump).
  MatchPlayer& runner = players[slot];
  const auto previousOf = runner.wayRoundOf;
  const auto previousSide = runner.wayRoundSide;
  // Who he gave way to, or had the right of way over, at the last plan.
  const auto previousGiveTo = runner.givingWayTo;
  const auto previousRightOver = runner.rightOfWayOver;
  runner.wayRoundOf = -1;
  runner.wayRoundSide = 0;
  runner.wayRoundBlockedBy = -1;
  runner.giveWaySpeed = -1.0F;
  runner.givingWayTo = -1;
  runner.rightOfWayOver = -1;
  if (!active(runner) || !runner.steersRound || runner.fallen ||
      !followsRoutes(runner) || runner.routeWaypoints > 0 ||
      state != MatchState::PLAYING)
  {
    return;
  }
  // A chaser: open play only, with the ball loose. Getting into position
  // (hold shape, support, recover): with the ball owned too, and in a set
  // piece's phase everywhere but the box it attacks, until the ball has
  // been in that box and come out.
  if (runner.chasingLooseBall
          ? setPiecePhaseRemaining > 0.0F ||
                getPossessionAttachment() != nullptr
          : setPiecePhaseRemaining > 0.0F && !setPieceBoxCleared &&
                inPenaltyArea(runner.position, !setPieceHome))
  {
    return;
  }
  const auto lengthOf = [](Vector2F v)
  { return std::sqrt((v.x * v.x) + (v.y * v.y)); };
  const auto minus = [](Vector2F a, Vector2F b)
  { return Vector2F{.x = a.x - b.x, .y = a.y - b.y}; };
  // The closest two straight runs come within `window` seconds, and when.
  const auto closestApproach =
      [&](Vector2F a, Vector2F va, Vector2F b, Vector2F vb, float window)
  {
    const Vector2F r = minus(b, a);
    const Vector2F v = minus(vb, va);
    const float vv = (v.x * v.x) + (v.y * v.y);
    const float t =
        vv > 1e-6F ? std::clamp(-((r.x * v.x) + (r.y * v.y)) / vv, 0.0F, window)
                   : 0.0F;
    return std::pair{lengthOf({.x = r.x + (v.x * t), .y = r.y + (v.y * t)}), t};
  };
  const auto clearanceFor = [&](const MatchPlayer& other)
  {
    return other.isHomeTeam == runner.isHomeTeam
               ? OVERTAKE_TEAM_MATE_CLEARANCE_METRES
               : OVERTAKE_OPPONENT_CLEARANCE_METRES;
  };
  // Anyone not after the ball himself (a chaser of either side is a
  // contest; an Aardvark brain's, by his intent) and on his feet (a fallen
  // player is hurdled).
  const auto inPlay = [&](const MatchPlayer& other)
  {
    return active(other) && &other != &runner && !other.fallen &&
           !other.chasingLooseBall &&
           !(!brainTraits(other.isHomeTeam).flagsLooseBallChasers &&
             other.intent == PlayerIntent::CLAIM_LOOSE_BALL);
  };

  const Vector2F position = asVector(runner.position);
  const Vector2F meet = asVector(runner.tacticalTarget);
  const Vector2F toMeet = minus(meet, position);
  const float meetMetres = lengthOf(toMeet);
  if (meetMetres <= WAY_ROUND_FINAL_METRES)
  {
    return;
  }
  // His run as he means it: straight at where he is going (his meet with
  // the ball, chasing; his spot): flat out, chasing; else at the speed he
  // is going (a job's pace is only what he works up to).
  const bool chasing = runner.chasingLooseBall;
  const float speed =
      std::max(chasing ? currentTopSpeed(runner) : length(runner.velocity),
               OVERTAKE_MINIMUM_SPEED);
  const float toBallSeconds = meetMetres / speed;
  const Vector2F heading{.x = toMeet.x / meetMetres,
                         .y = toMeet.y / meetMetres};
  const Vector2F velocity{.x = heading.x * speed, .y = heading.y * speed};
  const float window = std::min(WAY_ROUND_HORIZON_SECONDS, toBallSeconds);
  // Within the last metres before his meet it is a challenge, not a run.
  const auto nearBall = [&](Vector2F point)
  { return lengthOf(minus(meet, point)) <= WAY_ROUND_FINAL_METRES; };

  // The first he would come within the clearance of.
  int first = -1;
  float firstAt = 0.0F;
  for (std::size_t other = 0; other < players.size(); ++other)
  {
    const MatchPlayer& man = players[other];
    if (!inPlay(man))
    {
      continue;
    }
    const Vector2F at = asVector(man.position);
    if (nearBall(at))
    {
      continue;
    }
    const auto [closest, when] =
        closestApproach(position, velocity, at, man.velocity, window);
    if (closest >= clearanceFor(man))
    {
      continue;
    }
    if (nearBall({.x = position.x + (velocity.x * when),
                  .y = position.y + (velocity.y * when)}) ||
        nearBall({.x = at.x + (man.velocity.x * when),
                  .y = at.y + (man.velocity.y * when)}))
    {
      continue;
    }
    if (first < 0 || when < firstAt)
    {
      first = static_cast<int>(other);
      firstAt = when;
    }
  }
  if (first < 0)
  {
    return;
  }
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
    const auto cross = [](Vector2F a, Vector2F b)
    { return (a.x * b.y) - (a.y * b.x); };
    const Vector2F gap = minus(asVector(man.position), position);
    const float turn = cross(mine, theirs);
    const float cosine = (mine.x * theirs.x + mine.y * theirs.y) /
                         std::max(lengthOf(mine) * lengthOf(theirs), 1e-6F);
    // When each reaches where their paths cross (s), if ahead of both.
    const float myTime =
        std::abs(turn) > 1e-6F ? cross(gap, theirs) / turn : -1.0F;
    const float theirTime =
        std::abs(turn) > 1e-6F ? cross(gap, mine) / turn : -1.0F;
    const bool crossing = cosine > -GIVE_WAY_CROSSING_COSINE &&
                          cosine < GIVE_WAY_CROSSING_COSINE && myTime > 0.0F &&
                          theirTime > 0.0F;
    // The same two still meeting: as they were (a near tie read afresh
    // each plan would swap them back and forth), the other man's record of
    // it first, so the two always agree.
    const auto me = static_cast<std::int8_t>(slot);
    const bool wasLeading =
        man.givingWayTo == me ||
        (man.rightOfWayOver != me && previousRightOver == first);
    const bool wasGiving =
        !wasLeading && (man.rightOfWayOver == me || previousGiveTo == first);
    // Crossing: the first there. The same way: the one in front. Head-on:
    // the lower slot (a fixed rule both read alike).
    const bool lead =
        wasLeading  ? true
        : wasGiving ? false
        : crossing
            ? (myTime < theirTime ||
               (myTime == theirTime && slot < static_cast<std::size_t>(first)))
        : cosine >= GIVE_WAY_CROSSING_COSINE
            ? -((gap.x * mine.x) + (gap.y * mine.y)) > 0.0F
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
      const float leaderPast =
          theirTime + (clearance / std::max(lengthOf(theirs), 1e-3F));
      const float myRoom =
          std::max(0.0F, (lengthOf(mine) * myTime) - clearance);
      float allowed = myRoom / std::max(leaderPast, 1e-3F);
      if (allowed < GIVE_WAY_MOVING_SPEED)
      {
        allowed = 0.0F;
      }
      runner.giveWaySpeed = allowed;
      ++stats.giveWaySlows;
      return;
    }
    ++stats.giveWayRounds;
  }
  const Vector2F manAt = asVector(man.position);
  const Vector2F level{.x = manAt.x + (man.velocity.x * firstAt),
                       .y = manAt.y + (man.velocity.y * firstAt)};
  const Vector2F across{.x = -heading.y, .y = heading.x};
  // When the other side's chaser meets it: going round must not hand it
  // to him (if he was going to be first).
  float rival = std::numeric_limits<float>::max();
  for (const MatchPlayer& other : players)
  {
    if (active(other) && other.isHomeTeam != runner.isHomeTeam &&
        other.chasingLooseBall && other.looseBallMeetSeconds > 0.0F)
    {
      rival = std::min(rival, other.looseBallMeetSeconds);
    }
  }
  const float minX = MatchTuning::Pitch::PLAYER_EDGE_END_METRES;
  const float maxX = pitch.length - MatchTuning::Pitch::PLAYER_EDGE_END_METRES;
  const float minY = MatchTuning::Pitch::PLAYER_EDGE_SIDE_METRES;
  const float maxY = pitch.width - MatchTuning::Pitch::PLAYER_EDGE_SIDE_METRES;

  struct Option
  {
    bool ok = false;
    Vector2F waypoint{};
    float metres = 0.0F;
  };
  std::array<Option, 2> options;
  for (std::size_t index = 0; index < 2; ++index)
  {
    const float side = index == 0 ? 1.0F : -1.0F;
    Option& option = options[index];
    option.waypoint = {.x = level.x + (across.x * side * room),
                       .y = level.y + (across.y * side * room)};
    // On the pitch.
    if (option.waypoint.x < minX || option.waypoint.x > maxX ||
        option.waypoint.y < minY || option.waypoint.y > maxY)
    {
      continue;
    }
    const Vector2F firstLeg = minus(option.waypoint, position);
    const Vector2F secondLeg = minus(meet, option.waypoint);
    const float firstMetres = lengthOf(firstLeg);
    const float secondMetres = lengthOf(secondLeg);
    option.metres = firstMetres + secondMetres;
    const float firstTime = firstMetres / speed;
    const float secondTime = std::max(secondMetres / speed, 1e-3F);
    // Chasing, still his ball.
    if (chasing && toBallSeconds < rival && firstTime + secondTime >= rival)
    {
      continue;
    }
    const Vector2F firstVelocity{.x = firstLeg.x / std::max(firstTime, 1e-3F),
                                 .y = firstLeg.y / std::max(firstTime, 1e-3F)};
    const Vector2F secondVelocity{.x = secondLeg.x / secondTime,
                                  .y = secondLeg.y / secondTime};
    // Clear of everyone else on the way there, and of everyone (the man
    // passed too) on the way back to the ball, short of its last metres.
    bool clear = true;
    for (std::size_t other = 0; other < players.size() && clear; ++other)
    {
      const MatchPlayer& third = players[other];
      if (!inPlay(third))
      {
        continue;
      }
      const Vector2F at = asVector(third.position);
      const float clearance = clearanceFor(third);
      if (other != static_cast<std::size_t>(first))
      {
        const auto [gap, when] =
            closestApproach(position, firstVelocity, at, third.velocity,
                            std::min(firstTime, WAY_ROUND_HORIZON_SECONDS));
        if (gap < clearance &&
            !nearBall({.x = position.x + (firstVelocity.x * when),
                       .y = position.y + (firstVelocity.y * when)}))
        {
          clear = false;
        }
      }
      if (!clear || firstTime >= WAY_ROUND_HORIZON_SECONDS)
      {
        continue;
      }
      const Vector2F later{.x = at.x + (third.velocity.x * firstTime),
                           .y = at.y + (third.velocity.y * firstTime)};
      const auto [gap, when] = closestApproach(
          option.waypoint, secondVelocity, later, third.velocity,
          std::min(secondTime, WAY_ROUND_HORIZON_SECONDS - firstTime));
      // The man passed is level with the waypoint as the leg back starts:
      // only closing in on him again after it counts.
      if (gap < clearance && when > 1e-3F &&
          !nearBall({.x = option.waypoint.x + (secondVelocity.x * when),
                     .y = option.waypoint.y + (secondVelocity.y * when)}))
      {
        clear = false;
      }
    }
    option.ok = clear;
  }
  // The shorter side that works; the one he chose before unless the other
  // is clearly shorter.
  int chosen = -1;
  for (int index = 0; index < 2; ++index)
  {
    if (options[static_cast<std::size_t>(index)].ok &&
        (chosen < 0 || options[static_cast<std::size_t>(index)].metres <
                           options[static_cast<std::size_t>(chosen)].metres))
    {
      chosen = index;
    }
  }
  if (chosen >= 0 && previousOf == first && previousSide != 0)
  {
    const int previous = previousSide > 0 ? 0 : 1;
    if (previous != chosen && options[static_cast<std::size_t>(previous)].ok &&
        options[static_cast<std::size_t>(previous)].metres <=
            options[static_cast<std::size_t>(chosen)].metres +
                WAY_ROUND_STICKY_METRES)
    {
      chosen = previous;
    }
  }
  if (chosen < 0)
  {
    runner.wayRoundBlockedBy = static_cast<std::int8_t>(first);
    // Giving way with no way round: he eases off instead. The same way, to
    // a little under the speed the other is going his way; head-on, a stop.
    if (runner.givingWayTo == first)
    {
      const float along =
          (man.velocity.x * heading.x) + (man.velocity.y * heading.y);
      runner.giveWaySpeed = std::max(0.0F, along - GIVE_WAY_UNDER_LEADER_SPEED);
      if (runner.giveWaySpeed < GIVE_WAY_MOVING_SPEED)
      {
        runner.giveWaySpeed = 0.0F;
      }
      ++stats.giveWaySlows;
    }
    return;
  }
  const int side = chosen == 0 ? 1 : -1;
  runner.wayRoundOf = static_cast<std::int8_t>(first);
  runner.wayRoundSide = static_cast<std::int8_t>(side);
  runner.movementTarget =
      asPoint(options[static_cast<std::size_t>(chosen)].waypoint);
  runner.nextTarget = runner.tacticalTarget;
  runner.routeWaypoints = 1;
  runner.routeApexOffset = 0.0F;
  runner.routeOutwardRoom = room;
  const std::size_t kind = chasing                                        ? 0
                           : runner.intent == PlayerIntent::HOLD_SHAPE    ? 1
                           : runner.intent == PlayerIntent::OFFER_SUPPORT ? 2
                                                                          : 3;
  ++stats.wayRoundRefreshes[kind];
  if (previousOf != first || previousSide != side)
  {
    ++stats.wayRoundsStarted[kind];
  }
}

namespace
{
// Carry candidates (brain redesign plan section 16, step C). NT: our own
// estimates, each needs tuning.
/** He decides again about a second later (individual possessions last
 * about a second), so a carry is measured where he will be then. */
constexpr float CARRY_HORIZON_SECONDS = 1.0F;
/** NT: the directions, in degrees off straight down the pitch. */
constexpr std::array<float, 10> CARRY_ANGLES_DEGREES = {
    0.0F, 30.0F, -30.0F, 60.0F, -60.0F, 90.0F, -90.0F, 135.0F, -135.0F, 180.0F};
/** NT: "towards the goal" is left out within this of another direction. */
constexpr float SAME_DIRECTION_DEGREES = 5.0F;
/** NT: a candidate whose 1 s point is nearer the lines than this is out. */
constexpr float CARRY_PITCH_MARGIN_METRES = 1.0F;
/** NT: paces the Dribbling cap makes this close count once (m/s). */
constexpr float SAME_PACE_MPS = 0.05F;
/** The pace levels: walk, jog, run, sprint (as carryPaceCapybara maps). */
constexpr std::array<float, 4> CARRY_PACE_LEVELS = {0.0F, 1.0F / 3.0F,
                                                    2.0F / 3.0F, 1.0F};
/** NT: the planner's carry target sits this far along the held
 * direction (today's target sat about 17 m ahead). */
constexpr float CARRY_TARGET_METRES = 15.0F;
/** NT: before he has chosen a carry: a jog. */
constexpr float UNCHOSEN_CARRY_URGENCY = 1.0F / 3.0F;
constexpr float DEGREES_PER_HALF_TURN = 180.0F;
constexpr float DEGREES_PER_TURN = 360.0F;
/** Header frames (step G): the stopgap order's weights. */
constexpr float HEADER_FIRST = 2.0F;
constexpr float HEADER_SECOND = 1.0F;
/** The brain flow tool: an angle this close to a fixed one is that one. */
constexpr float FIXED_ANGLE_MATCH_DEGREES = 0.5F;
}  // namespace

PitchPoint MatchEngine::carryTargetCapybara(const MatchPlayer& player) const
{
  // Along the direction of the carry he chose (bestCarryCapybara), held
  // until he decides again; straight down the pitch before he has chosen
  // one. Lost until roles act on the options step (plan section 16): the
  // cut inside, keeping a wide lane, the forward metres for Dribbling, risk
  // and a counter-attack, and the dodge away from the nearest opponent.
  const HeldCarryCapybara* held = heldCarryOfCapybara(player);
  const Vector2F direction =
      held != nullptr
          ? held->direction
          : Vector2F{.x = player.isHomeTeam ? 1.0F : -1.0F, .y = 0.0F};
  return clampToPitch(
      PitchPoint{player.position.x + (direction.x * CARRY_TARGET_METRES),
                 player.position.y + (direction.y * CARRY_TARGET_METRES)});
}

PitchPoint MatchEngine::firstTouchAimCapybara(const MatchPlayer& player) const
{
  // The first touch's aim (it comes before his decision on the ball):
  // the carry target as it was before step C, kept until the first touch
  // moves onto the decision frame.
  const StrategySliders sliders = getEffectiveSliders(player.isHomeTeam);
  const TeamPhase teamPhase = player.isHomeTeam ? homePhase : awayPhase;
  const float attackDirection = player.isHomeTeam ? 1.0F : -1.0F;
  PitchPoint target = player.position;
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
  for (const MatchPlayer& opponent : players)
  {
    if (!active(opponent) || opponent.isHomeTeam == player.isHomeTeam)
    {
      continue;
    }
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
      std::abs(pitch.fractionY(player.position) - MatchTuning::Pitch::CENTRE) >
          S::WIDE_LANE_DEVIATION;
  if (cutsInsideCapybara(player))
  {
    target.y = player.position.y +
               (pitch.atY(MatchTuning::Pitch::CENTRE) - player.position.y) *
                   S::CUT_INSIDE_PULL;
  }
  else if ((closestOpponent != nullptr) &&
           closestDistance < S::CARRIER_EVASION_RANGE_METRES)
  {
    const float evadeDirection =
        player.position.y <= closestOpponent->position.y ? -1.0F : 1.0F;
    target.y += evadeDirection *
                (S::CARRIER_BASE_EVASION_METRES +
                 player.dribbling * S::CARRIER_DRIBBLING_EVASION_METRES);
  }
  else if (wideLane)
  {
    target.y = player.position.y;
  }
  else
  {
    target.y += (pitch.atY(MatchTuning::Pitch::CENTRE) - player.position.y) *
                S::CARRIER_CENTRALITY;
  }
  return target;
}

bool MatchEngine::cutsInsideCapybara(const MatchPlayer& player) const
{
  using S = MatchTuning::Shape;
  const PlayerRole role = player.player->getRole();
  const bool wideForward = role == PlayerRole::LM || role == PlayerRole::RM ||
                           role == PlayerRole::LW || role == PlayerRole::RW;
  const float depth = player.isHomeTeam
                          ? pitch.fractionX(player.position)
                          : 1.0F - pitch.fractionX(player.position);
  // An inside forward always looks to come inside, a touchline winger
  // never does, whatever his natural position.
  const float roleCut = roleProfileOf(player).cutInside;
  if ((!wideForward && roleCut == 0.0F) ||
      depth < MatchTuning::Rules::HOME_FINAL_THIRD_START)
  {
    return false;
  }
  // The choice holds for a few seconds at a time.
  const float share = std::clamp(
      std::clamp(S::CUT_INSIDE_BASE + ((player.shooting - player.passing) *
                                       S::CUT_INSIDE_PREFERENCE),
                 S::MIN_CUT_INSIDE, S::MAX_CUT_INSIDE) +
          (roleCut * TacticsTuning::CUT_INSIDE_SHIFT),
      0.0F, 1.0F);
  return (epochNoise(player.player->getId(), 0xc0715eU,
                     RUN_TIMING_EPOCH_STEPS) +
          1.0F) *
             0.5F <
         share;
}

[[gnu::always_inline]] inline PitchPoint MatchEngine::possessionAnchorCapybara(
    const MatchPlayer& player) const
{
  const std::size_t side = player.isHomeTeam ? 0 : 1;
  if (player.isGoalkeeper || player.formationSlot < 0)
  {
    return player.basePosition;
  }
  const auto slot =
      static_cast<std::size_t>(static_cast<int>(player.formationSlot));
  if (slot >= slotTactics[side].size())
  {
    return player.basePosition;
  }
  const SlotTactic& tactic = slotTactics[side][slot];
  if (tactic.possessionOffset.x == 0.0F && tactic.possessionOffset.y == 0.0F &&
      tactic.profile.possessionAdvanceMetres == 0.0F &&
      tactic.profile.possessionWidthMetres == 0.0F)
  {
    return player.basePosition;
  }
  using Pitch = MatchTuning::Pitch;
  const float direction = player.isHomeTeam ? 1.0F : -1.0F;
  const float lateral =
      pitch.fractionY(player.basePosition.y - pitch.atY(Pitch::CENTRE));
  // Width pushes toward his own touchline; a central player has none.
  const float flank = std::abs(lateral) < 0.05F ? 0.0F
                      : lateral > 0.0F          ? 1.0F
                                                : -1.0F;
  PitchPoint anchor = player.basePosition;
  anchor.x += direction * (tactic.possessionOffset.x +
                           tactic.profile.possessionAdvanceMetres);
  anchor.y += tactic.possessionOffset.y +
              (flank * tactic.profile.possessionWidthMetres);
  return clampToPitch(anchor);
}

[[gnu::always_inline]] inline float MatchEngine::defensiveRoleShiftCapybara(
    const MatchPlayer& player) const
{
  return (player.isHomeTeam ? 1.0F : -1.0F) *
         roleProfileOf(player).defensiveAdvanceMetres;
}

[[gnu::always_inline]] inline const MatchPlayer*
MatchEngine::tightMarkTargetCapybara(const MatchPlayer& marker) const
{
  if (oppositionOrders[marker.isHomeTeam ? 0 : 1].empty())
  {
    return nullptr;
  }
  const std::int8_t assigned = markAssignments[slotOf(marker)];
  if (assigned < 0 || static_cast<std::size_t>(assigned) >= players.size())
  {
    return nullptr;
  }
  const MatchPlayer& mark = players[static_cast<std::size_t>(assigned)];
  if (!active(mark) || mark.isHomeTeam == marker.isHomeTeam)
  {
    return nullptr;
  }
  return instructionAgainst(mark) == OppositionInstruction::TightMark ? &mark
                                                                      : nullptr;
}

PitchPoint MatchEngine::tightMarkPointCapybara(const MatchPlayer& marker,
                                               const MatchPlayer& target,
                                               PitchPoint current) const
{
  namespace T = TacticsTuning;
  const float direction = marker.isHomeTeam ? 1.0F : -1.0F;
  const Vector2F point{
      .x = target.position.x - (direction * T::TIGHT_MARK_GOAL_SIDE_METRES),
      .y = target.position.y};
  return PitchPoint{current.x + ((point.x - current.x) * T::TIGHT_MARK_WEIGHT),
                    current.y + ((point.y - current.y) * T::TIGHT_MARK_WEIGHT)};
}

Vector2F MatchEngine::tightMarkVelocityCapybara(const MatchPlayer& target) const
{
  // The marking point moves with his man, by the share it is pulled to him.
  return {.x = target.velocity.x * TacticsTuning::TIGHT_MARK_WEIGHT,
          .y = target.velocity.y * TacticsTuning::TIGHT_MARK_WEIGHT};
}

float MatchEngine::tightMarkPenaltyCapybara(const MatchPlayer& receiver) const
{
  if (instructionAgainst(receiver) != OppositionInstruction::TightMark)
  {
    return 0.0F;
  }
  for (std::size_t slot = 0; slot < players.size() && slot < 32; ++slot)
  {
    const MatchPlayer& marker = players[slot];
    if (!active(marker) || marker.isHomeTeam == receiver.isHomeTeam ||
        markAssignments[slot] < 0 ||
        &players[static_cast<std::size_t>(markAssignments[slot])] != &receiver)
    {
      continue;
    }
    if (distance(marker.position, receiver.position) <=
        TacticsTuning::TIGHT_MARK_REACH_METRES)
    {
      return TacticsTuning::TIGHT_MARK_COMPLETION_PENALTY *
             MatchTuning::Passing::COMPLETION_UTILITY_WEIGHT;
    }
  }
  return 0.0F;
}

[[gnu::always_inline]] inline float MatchEngine::pressEagernessCapybara(
    const MatchPlayer& candidate, const MatchPlayer* carrier) const
{
  if ((carrier == nullptr) || carrier->isHomeTeam == candidate.isHomeTeam)
  {
    return 0.0F;
  }
  // A rousing talk sends the side after the ball sooner (a flat one later).
  return (roleProfileOf(candidate).pressBias *
          TacticsTuning::PRESS_BIAS_SECONDS) +
         (talkSwing(candidate.isHomeTeam) * TacticsTuning::TALK_PRESS_SECONDS);
}

float MatchEngine::engageBoostCapybara(const MatchPlayer& defender,
                                       const MatchPlayer& carrier) const
{
  namespace T = TacticsTuning;
  float boost = 1.0F +
                (roleProfileOf(defender).pressBias * T::PRESS_BIAS_ENGAGE) +
                (talkSwing(defender.isHomeTeam) * T::TALK_ENGAGE);
  const OppositionInstruction order = instructionAgainst(carrier);
  if (order == OppositionInstruction::Press)
  {
    boost += T::PRESS_ORDER_ENGAGE;
  }
  if (order == OppositionInstruction::DoubleUp)
  {
    boost += T::DOUBLE_UP_ENGAGE;
  }
  return std::max(0.2F, boost);
}

float MatchEngine::pressStandOffShareCapybara(const MatchPlayer& presser,
                                              const MatchPlayer& carrier) const
{
  float share = 1.0F - (0.2F * roleProfileOf(presser).pressBias) -
                (0.2F * talkSwing(presser.isHomeTeam));
  if (instructionAgainst(carrier) == OppositionInstruction::Press)
  {
    share *= TacticsTuning::PRESS_ORDER_STANDOFF_SHARE;
  }
  return std::clamp(share, 0.3F, 1.3F);
}

float MatchEngine::weakFootShadeCapybara(const MatchPlayer& carrier) const
{
  if ((carrier.player == nullptr) ||
      instructionAgainst(carrier) != OppositionInstruction::ShowWeakFoot)
  {
    return 0.0F;
  }
  // Facing his attacking direction, a right-footer's strong side is +y for
  // the home side (attacking toward x = 1) and -y for the away side.
  const float strongSide =
      (carrier.player->getFoot() == Foot::Right ? 1.0F : -1.0F) *
      (carrier.isHomeTeam ? 1.0F : -1.0F);
  return strongSide * TacticsTuning::WEAK_FOOT_SHADE_METRES;
}

std::optional<PitchPoint> MatchEngine::doubleUpPointCapybara(
    const MatchPlayer& helper, const MatchPlayer& carrier,
    const MatchPlayer* firstPresser) const
{
  if (instructionAgainst(carrier) != OppositionInstruction::DoubleUp)
  {
    return std::nullopt;
  }
  // The second man closes from the side the first presser leaves open,
  // goal-side of the carrier.
  const float lateral =
      (firstPresser != nullptr) && firstPresser->position.y > carrier.position.y
          ? -1.0F
      : (firstPresser != nullptr)               ? 1.0F
      : helper.position.y <= carrier.position.y ? -1.0F
                                                : 1.0F;
  const float direction = helper.isHomeTeam ? 1.0F : -1.0F;
  const float reach = TacticsTuning::DOUBLE_UP_SUPPORT_METRES;
  return clampToPitch(
      PitchPoint{carrier.position.x - (direction * reach * 0.5F),
                 carrier.position.y + (lateral * reach)});
}

void MatchEngine::assignMarksCapybara(bool home)
{
  // Greedy one-to-one assignment of a side's outfield markers to opposing
  // outfield players by channel, depth and danger, so nobody is left unmarked
  // while two defenders shadow the same attacker.
  for (std::size_t slot = 0;
       slot < players.size() && slot < markAssignments.size(); ++slot)
  {
    if (players[slot].isHomeTeam == home)
    {
      markAssignments[slot] = -1;
    }
  }
  const auto score =
      [](bool markerHome, Vector2F base, float opponentX, float opponentY)
  {
    // All in fractions of the pitch.
    const float dangerDepth = markerHome ? 1.0F - opponentX : opponentX;
    return (std::abs(opponentY - base.y) * MatchTuning::Shape::CHANNEL_WEIGHT) +
           std::abs(opponentX - base.x) -
           (dangerDepth * MatchTuning::Shape::DANGER_DEPTH_WEIGHT);
  };
  {
    std::array<std::uint8_t, 16> markers{};
    std::array<std::uint8_t, 16> opponents{};
    std::size_t markerCount = 0;
    std::size_t opponentCount = 0;
    for (std::size_t slot = 0; slot < players.size() && slot < 32; ++slot)
    {
      const MatchPlayer& candidate = players[slot];
      if (!active(candidate) || candidate.isGoalkeeper)
      {
        continue;
      }
      if (candidate.isHomeTeam == home && markerCount < markers.size())
      {
        markers[markerCount++] = static_cast<std::uint8_t>(slot);
      }
      else if (candidate.isHomeTeam != home && opponentCount < opponents.size())
      {
        opponents[opponentCount++] = static_cast<std::uint8_t>(slot);
      }
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
      {
        scores[(m * 16) + o] =
            score(marker.isHomeTeam, base, opponentX[o], opponentY[o]);
      }
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
        if ((usedOpponents & (1U << o)) != 0U)
        {
          continue;
        }
        if (scores[(m * 16) + o] < best)
        {
          best = scores[(m * 16) + o];
          bestOpponent = static_cast<std::uint8_t>(o);
        }
      }
      bestOf[m] = bestOpponent;
    };
    std::uint32_t usedMarkers = 0;
    std::uint32_t usedOpponents = 0;
    for (std::size_t m = 0; m < markerCount; ++m)
    {
      findBest(m, 0U);
    }
    for (std::size_t round = 0; round < std::min(markerCount, opponentCount);
         ++round)
    {
      float best = std::numeric_limits<float>::max();
      std::size_t bestMarker = 0;
      for (std::size_t m = 0; m < markerCount; ++m)
      {
        if ((usedMarkers & (1U << m)) != 0U)
        {
          continue;
        }
        if (scores[(m * 16) + bestOf[m]] < best)
        {
          best = scores[(m * 16) + bestOf[m]];
          bestMarker = m;
        }
      }
      const std::uint8_t bestOpponent = bestOf[bestMarker];
      usedMarkers |= 1U << bestMarker;
      usedOpponents |= 1U << bestOpponent;
      markAssignments[markers[bestMarker]] =
          static_cast<std::int8_t>(opponents[bestOpponent]);
      for (std::size_t m = 0; m < markerCount; ++m)
      {
        if ((usedMarkers & (1U << m)) == 0U && bestOf[m] == bestOpponent)
        {
          findBest(m, usedOpponents);
        }
      }
    }
  }
}

void MatchEngine::updateRestartSupportCapybara(MatchPlayer& player,
                                               const MatchPlayer* taker)
{
  if (player.isGoalkeeper || &player == taker || restartIsSetPiece ||
      (state != MatchState::THROW_IN && state != MatchState::GOAL_KICK))
  {
    return;
  }

  player.movementTarget = player.basePosition;
  if ((taker != nullptr) && player.isHomeTeam == taker->isHomeTeam)
  {
    const float gap = distance(player.position, restartSpot());
    if (gap < MatchTuning::Timing::RESTART_SUPPORT_DISTANCE_METRES)
    {
      // Offer a lane inside the pitch, with teammates spread along it.
      player.movementTarget = clampToPitch(PitchPoint{
          restartSpot().x + ((player.basePosition.x - restartSpot().x) *
                             MatchTuning::Timing::RESTART_SUPPORT_BLEND),
          restartSpot().y + ((player.basePosition.y - restartSpot().y) *
                             MatchTuning::Timing::RESTART_SUPPORT_BLEND)});
    }
  }
  else if (taker != nullptr)
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
      const Vector2F offset = {.x = direction.x * minimum,
                               .y = direction.y * minimum};
      player.movementTarget =
          PitchPoint{restartSpot().x + offset.x, restartSpot().y + offset.y};
    }
  }
}

bool MatchEngine::touchNowCapybara(const MatchPlayer& player,
                                   const TouchMoment& moment) const
{
  // Control is easier the closer the ball is: he lets it come to the
  // closest comfortable point it will pass, unless waiting would risk
  // losing it. Nobody waits to steal a ball another player is playing.
  if (moment.gap <= touchKnobs[TouchKnob::COMFORT_GAP] || hasTouchClaim())
  {
    return true;
  }
  // Off the ground (his chest) he plays it where it meets him, for now
  // (chest and thigh control plan step 5 decides waiting).
  if (moment.height > bodyLines(player).feet)
  {
    return true;
  }
  return !waitsForCloserCapybara(player, moment);
}

bool MatchEngine::dribbleTouchNowCapybara(const MatchPlayer& player,
                                          const TouchMoment& moment) const
{
  // His own ball, due a touch: the same timing as a first touch, played at
  // once inside the gap he means to play it at (close for a better
  // dribbler, out to the strike reach for a poor one), else let closer.
  if (moment.gap <= (getBody(player.isHomeTeam) == BodyId::BINTURONG
                         ? dribbleMeetGapBinturong(player)
                         : touchKnobs[TouchKnob::COMFORT_GAP]))
  {
    return true;
  }
  return !waitsForCloserCapybara(player, moment);
}

bool MatchEngine::waitsForCloserCapybara(const MatchPlayer& player,
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
      std::clamp(static_cast<int>((knob[K::TIMING_LOOKAHEAD] / STEP) + 0.5F), 0,
                 MAX_LOOKAHEAD);
  const float speed = length(ball.velocity);
  // Its heading turns as its sidespin bends it (wait dead zone plan step 2).
  Vector2F heading = speed > EPSILON ? Vector2F{.x = ball.velocity.x / speed,
                                                .y = ball.velocity.y / speed}
                                     : Vector2F{.x = 0.0F, .y = 0.0F};
  FlightState flight{.horizontalSpeed = speed,
                     .verticalSpeed = ball.velocityZ,
                     .height = ball.z,
                     .bounced = false,
                     .spin = ball.spin};
  const Vector2F ballStart = asVector(ball.position);
  Vector2F ballAt = ballStart;
  const Vector2F playerStart = asVector(player.position);
  // He stands still through the rest of this step's ball sub-steps, then
  // moves at the step boundaries (read from his current velocity).
  const int perStep = std::max(
      1, static_cast<int>((moment.stepSeconds / moment.substepSeconds) + 0.5F));
  const auto movesBy = [&](int step)
  {
    return step > moment.substepsLeft
               ? 1 + ((step - moment.substepsLeft - 1) / perStep)
               : 0;
  };
  std::array<Vector2F, MAX_LOOKAHEAD + 1> path{};
  float bestGap = moment.gap;
  int bestStep = 0;
  float gapAfterMove = 0.0F;
  bool nearDeadZoneAfterMove = false;
  // The first sub-step at which a bounce lifts it out of reach of his feet.
  int risesAt = LOOKAHEAD + 1;
  const float feetLine = bodyLines(player).feet;
  for (int step = 1; step <= LOOKAHEAD; ++step)
  {
    const float rolled = stepFlight(flight, STEP);
    if (risesAt > LOOKAHEAD && flight.height > feetLine)
    {
      risesAt = step;
    }
    ballAt = {.x = ballAt.x + (heading.x * rolled),
              .y = ballAt.y + (heading.y * rolled)};
    path[static_cast<std::size_t>(step)] = ballAt;
    const float turn = flight.turnRate * STEP;
    heading = {
        .x = (heading.x * std::cos(turn)) - (heading.y * std::sin(turn)),
        .y = (heading.x * std::sin(turn)) + (heading.y * std::cos(turn))};
    const float moved = moment.stepSeconds * static_cast<float>(movesBy(step));
    const Vector2F gapVector{.x = path[static_cast<std::size_t>(step)].x -
                                  (playerStart.x + player.velocity.x * moved),
                             .y = path[static_cast<std::size_t>(step)].y -
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
      if (step == 1)
      {
        return false;
      }
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
        for (const float side : {-slack, 0.0F, slack})
        {
          const float c = std::cos(side) * out;
          const float s = std::sin(side) * out;
          const Vector2F round{.x = (gapVector.x * c) - (gapVector.y * s),
                               .y = (gapVector.x * s) + (gapVector.y * c)};
          if (ballBehindReach(player, -round.x, -round.y, gap + margin, moved))
          {
            nearDeadZoneAfterMove = true;
          }
        }
      }
    }
    if (gap < bestGap)
    {
      bestGap = gap;
      bestStep = step;
    }
    if (gap <= knob[K::COMFORT_GAP])
    {
      break;
    }
  }
  // Only worth waiting for a clearly closer point, safely inside his reach.
  if (bestStep == 0 || bestGap > moment.gap - knob[K::WAIT_GAIN] ||
      bestGap > moment.reach - knob[K::WAIT_MARGIN])
  {
    return false;
  }
  // A bounce on the way would take it up off his feet first.
  if (risesAt <= bestStep)
  {
    return false;
  }
  // His own next move is the least certain part of the read: wait through
  // it only if the ball is still safely inside his reach just after.
  if (bestStep > moment.substepsLeft &&
      (gapAfterMove > moment.reach - knob[K::WAIT_MARGIN] ||
       nearDeadZoneAfterMove))
  {
    return false;
  }
  // An opponent who can get to the ball's path first would take it: play
  // it now, stretched or not.
  for (const MatchPlayer& opponent : players)
  {
    if (!active(opponent) || opponent.isHomeTeam == player.isHomeTeam)
    {
      continue;
    }
    const float reach =
        opponent.isGoalkeeper
            ? MatchTuning::Ball::GOALKEEPER_CONTROL_RADIUS_METRES
            : feetReach(opponent);
    const Vector2F from = asVector(opponent.position);
    for (int step = 1; step <= bestStep; ++step)
    {
      const float elapsed = STEP * static_cast<float>(step);
      const Vector2F at = path[static_cast<std::size_t>(step)];
      const float gapX = at.x - (from.x + opponent.velocity.x * elapsed);
      const float gapY = at.y - (from.y + opponent.velocity.y * elapsed);
      if (gapX * gapX + gapY * gapY <= reach * reach)
      {
        return false;
      }
    }
  }
  return true;
}

TouchIntent MatchEngine::chooseFirstTouchCapybara(
    const MatchPlayer& player, const TouchMoment& /*moment*/) const
{
  using K = TouchKnob;
  const FirstTouchKnobs& knob = touchKnobs;
  const float attackDirection = player.isHomeTeam ? 1.0F : -1.0F;
  const float speed = length(player.velocity);
  TouchIntent intent;
  // Not expecting it: a pass meant for someone else. A stand-in until the
  // brain has the mental attributes to read it (known issues).
  intent.surprise =
      getPossessionState().isPass &&
              (possessionId == PossessionId::BINTURONG
                   ? player.intent != PlayerIntent::RECEIVE_PASS &&
                         player.intent != PlayerIntent::CLAIM_LOOSE_BALL
                   : getExpectedReceiver() != player.player)
          ? 1.0F
          : 0.0F;
  // Pressed: an opponent close enough to be in the way. Also a stand-in
  // (known issues).
  intent.pressure = contactPressure(nearestOpponentDistance(player),
                                    knob[K::PRESSURE_RADIUS]);

  // Two situations share this frame, feet branch:
  // - "I'm expecting the ball" (brain redesign plan section 19, step F
  //   stage 1): our pass, or a loose ball he goes to;
  // - "I can intercept the ball" (step I): the opponents' pass reaches him
  //   (getPossessionState().isPass, passByHome not his side).
  // Options in both: a control touch, laying it out, and first-time pass,
  // shot and clear (listed, not judged by the brain yet). The same
  // availability and stopgap pick for now; the situations will part when
  // the options get measures (an interception is scored differently).
  using S = TermSource;
  // Lay it out: open when he is running into open space (today's "into
  // his stride"), along his run.
  std::optional<PitchPoint> layOutSpot;
  if (speed >= MatchTuning::Dribble::CLOSE_CONTROL_SPEED)
  {
    const Vector2F run{.x = player.velocity.x / speed,
                       .y = player.velocity.y / speed};
    const float lead = (speed * knob[K::STRIDE_LEAD]) + knob[K::STRIDE_EXTRA];
    const Vector2F ahead = {.x = run.x * lead, .y = run.y * lead};
    const PitchPoint spot = clampToPitch(
        PitchPoint{player.position.x + ahead.x, player.position.y + ahead.y});
    const bool clear = std::ranges::none_of(
        players,
        [&](const MatchPlayer& opponent)
        {
          return active(opponent) && opponent.isHomeTeam != player.isHomeTeam &&
                 distance(opponent.position, spot) < knob[K::STRIDE_CLEARANCE];
        });
    if (clear)
    {
      layOutSpot = spot;
    }
  }
  // Follow-up: no measures yet. The stopgap keeps today's outcomes: lay
  // it out whenever it is open, else the control touch.
  DecisionFrame::Score control;
  control.add("stopgap: the fallback", S::SITUATION, 0.0F, 1.0F);
  DecisionFrame::Score layOut;
  if (layOutSpot)
  {
    layOut.add("stopgap: lay it out whenever it is open", S::SITUATION, 1.0F,
               1.0F);
  }
  else
  {
    layOut.ruleOut();
  }
  // First-time pass, shot and clear: the body plays the first two in
  // drills; the brain does not judge them yet (stage 2).
  if (layOut.isAvailable() && layOut.total() >= control.total())
  {
    intent.kind = TouchIntent::Kind::INTO_STRIDE;
    intent.targetSpot = *layOutSpot;
    intent.arrivalSpeed = speed * knob[K::STRIDE_SPEED_SHARE];
    return intent;
  }

  // The control touch: set it just ahead of where he will be, toward where he
  // means to carry it, turned away from the nearest opponent the closer he is,
  // and never back toward his own goal near it.
  Vector2F aim =
      metricDirection(player.position, firstTouchAimCapybara(player));
  if (length(aim) <= EPSILON)
  {
    aim = {.x = attackDirection, .y = 0.0F};
  }
  const MatchPlayer* nearest = nullptr;
  float nearestDistance = std::numeric_limits<float>::max();
  for (const MatchPlayer& opponent : players)
  {
    if (!active(opponent) || opponent.isHomeTeam == player.isHomeTeam)
    {
      continue;
    }
    const float gap = distance(opponent.position, player.position);
    if (gap < nearestDistance)
    {
      nearestDistance = gap;
      nearest = &opponent;
    }
  }
  constexpr float PRESSURE = MatchTuning::Decision::PRESSURE_RADIUS_METRES;
  if ((nearest != nullptr) && nearestDistance < PRESSURE)
  {
    const float weight = 1.0F - (nearestDistance / PRESSURE);
    const Vector2F away = metricDirection(nearest->position, player.position);
    aim = normalized({.x = (aim.x * (1.0F - weight)) + (away.x * weight),
                      .y = (aim.y * (1.0F - weight)) + (away.y * weight)});
    if (length(aim) <= EPSILON)
    {
      aim = {.x = attackDirection, .y = 0.0F};
    }
  }
  const float fromOwnGoal =
      pitch.fromOwnGoalLine(player.position, player.isHomeTeam);
  if (fromOwnGoal < MatchTuning::SetPiece::CROSS_CLEARANCE_WITHIN_METRES &&
      aim.x * attackDirection < 0.0F)
  {
    aim.x = 0.0F;
    aim = length(aim) > EPSILON ? normalized(aim)
                                : Vector2F{.x = attackDirection, .y = 0.0F};
  }
  const Vector2F offset = {.x = (player.velocity.x * knob[K::SET_LEAD]) +
                                (aim.x * knob[K::SET_OFFSET]),
                           .y = (player.velocity.y * knob[K::SET_LEAD]) +
                                (aim.y * knob[K::SET_OFFSET])};
  intent.targetSpot = clampToPitch(
      PitchPoint{player.position.x + offset.x, player.position.y + offset.y});
  return intent;
}

MatchEngine::Challenge MatchEngine::chooseChallengeCapybara(
    const MatchPlayer& carrier, float dt)
{
  // The situation "I can challenge the carrier" (brain redesign plan
  // section 24, step K), its older way in: each step, the nearest defender
  // to the ball (not to the man) may engage. Options: hold, tackle
  // (standing), slide in. Open on today's roll (the engage rate below):
  // standing within his reach, sliding beyond it; hold always. Follow-up:
  // no measures yet; the stopgap pick takes the open tackle.
  // He waits for the ball to be away from the attacker's foot; near his
  // own box and with a pressing instruction he goes in sooner.
  Challenge challenge;
  MatchPlayer* defender =
      findClosestPlayer(ball.position, !carrier.isHomeTeam, false);
  if ((defender == nullptr) || defender->tackleCooldown > 0.0F ||
      defender->isInjured || defender->fallen || defender->trapTimer > 0.0F ||
      isControlled(*defender))
  {
    return challenge;
  }
  using D = MatchTuning::Defending;
  const float reach = standingTackleReachOf(defender->defending);
  const float ballDistance = distance(defender->position, ball.position);
  if (ballDistance >= D::SLIDE_TACKLE_DISTANCE_METRES)
  {
    return challenge;
  }
  // A loose dribbled ball is played, not rolled for: outside the carrier's
  // reach as a free ball (tackling plan step 1), inside it as a contest
  // (step 2), both in resolveLooseBall. No standing challenge; slides stay
  // as they are until their own plan. (Only a Binturong body contests; an
  // Aardvark one still rolls inside the carrier's reach.)
  if (hasTouchClaim() && ballDistance <= reach &&
      (!ballInReach(carrier) ||
       getBody(defender->isHomeTeam) == BodyId::BINTURONG))
  {
    return challenge;
  }
  const float carrierDepth = carrier.isHomeTeam
                                 ? pitch.fractionX(carrier.position)
                                 : 1.0F - pitch.fractionX(carrier.position);
  const float engageRate =
      D::ENGAGE_RATE_PER_SECOND *
      (1.0F + getEffectiveSliders(defender->isHomeTeam).pressing *
                  D::PRESSING_ENGAGE_BONUS) *
      (carrierDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START
           ? D::FINAL_THIRD_ENGAGE_FACTOR
           : 1.0F) *
      (D::CLOSE_CONTROL_ENGAGE_SHARE +
       D::EXPOSED_ENGAGE_SHARE * dribbleExposureShare()) *
      (ballDistance > reach ? 0.5F : 1.0F) *
      engageBoostCapybara(*defender, carrier);
  const float engageRoll = randomFloat(0.0F, 1.0F);
  const float engageChance = 1.0F - std::exp(-engageRate * dt);
  if (wantsDetail())
  {
    // Debugger detail: the rate is a product; each factor is noted as
    // what it adds to it.
    MatchDuelDetail duel;
    duel.kind = MatchDuelDetail::Kind::ENGAGE;
    duel.player = (defender->player != nullptr) ? defender->player->getId() : 0;
    duel.opponent = (carrier.player != nullptr) ? carrier.player->getId() : 0;
    duel.homeTeam = defender->isHomeTeam;
    duel.chance.total = engageRate;
    duel.probability = engageChance;
    duel.roll = engageRoll;
    float product = D::ENGAGE_RATE_PER_SECOND;
    duel.chance.add("base rate", TermSource::SITUATION, product);
    const auto factor = [&](const char* name, TermSource source, float by)
    {
      duel.chance.add(name, source, product * (by - 1.0F));
      product *= by;
    };
    factor("pressing instruction", TermSource::INSTRUCTION,
           1.0F + (getEffectiveSliders(defender->isHomeTeam).pressing *
                   D::PRESSING_ENGAGE_BONUS));
    factor("final third", TermSource::LOCATION,
           carrierDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START
               ? D::FINAL_THIRD_ENGAGE_FACTOR
               : 1.0F);
    factor("ball away from his feet", TermSource::SITUATION,
           D::CLOSE_CONTROL_ENGAGE_SHARE +
               (D::EXPOSED_ENGAGE_SHARE * dribbleExposureShare()));
    factor("beyond tackling reach", TermSource::SITUATION,
           ballDistance > reach ? 0.5F : 1.0F);
    factor("role, team talk and opposition orders", TermSource::SLOT_ROLE,
           engageBoostCapybara(*defender, carrier));
    if (MatchRecorder* recorder = getRecorder())
    {
      recorder->onDuelDetail(*this, duel);
    }
  }
  if (engageRoll < engageChance)
  {
    challenge.defender = defender;
    // Out of standing reach he goes to ground.
    challenge.sliding = ballDistance > reach;
  }
  return challenge;
}

MatchEngine::DefensiveCoverCapybara MatchEngine::defensiveCoverCapybara(
    const MatchPlayer& defender) const
{
  // Goal-side: nearer his own goal line than he is.
  const auto depth = [&](const MatchPlayer& player)
  {
    return defender.isHomeTeam ? pitch.fractionX(player.position)
                               : 1.0F - pitch.fractionX(player.position);
  };
  DefensiveCoverCapybara cover;
  for (const MatchPlayer& mate : players)
  {
    if (&mate == &defender || mate.isHomeTeam != defender.isHomeTeam ||
        mate.isGoalkeeper || !active(mate) || depth(mate) >= depth(defender))
    {
      continue;
    }
    cover.lastMan = false;
    if (distance(mate.position, defender.position) <=
        MatchTuning::Defending::COVER_METRES)
    {
      cover.covered = true;
    }
  }
  return cover;
}

bool MatchEngine::goesIntoContestCapybara(const MatchPlayer& defender,
                                          const MatchPlayer& owner,
                                          PitchPoint contact)
{
  // The situation "I can challenge the carrier" (step K), its contest way
  // in (tackling plan step 5): the loose dribbled ball within both their
  // reaches. Options: hold, go in to win it, go in to clear it (go in to
  // pass it listed, not judged yet). Going in is open when the cue as he
  // reads it clears his bar; the intent is today's rule (clear in his own
  // third, at a stretch or as the last man; else win), carried by the
  // option and held to the contact. Follow-up: the stopgap pick goes in
  // whenever it is open.
  // He reads the carrier's foot: recovering from a touch, or his touch
  // about to come (he would swing at nothing or meet it in a block), and
  // how much of the ball he has shown; how true his read of the timing is,
  // is his Defending (judgement). The bar: lower pressing, against a "press
  // him" order or with cover; higher as the last man or in his own box.
  // Free with no touch due is no cue at all: a defender who reads it right
  // holds, however hard his side presses.
  using D = MatchTuning::Defending;
  using R = MatchTuning::Dribble;
  const float judgement = std::clamp(
      (defender.defending - STAT_1_RATING) / (STAT_99_RATING - STAT_1_RATING),
      0.0F, 1.0F);
  const float error = D::READ_ERROR_POOR +
                      ((D::READ_ERROR_BEST - D::READ_ERROR_POOR) * judgement);
  // The carrier's foot as it is, and as he reads it.
  const float recovery = std::max(ball.ownTouchCooldown, owner.footCooldown);
  const bool planned = ball.meetFor == owner.player;
  const float touchIn =
      planned ? ball.meetSeconds : dribbleTouchIntervalBinturong(owner);
  const auto footCue = [&](float recoveryRead, float touchInRead)
  {
    if (recoveryRead > 1e-4F)
    {
      return D::CUE_RECOVERING;
    }
    if (touchInRead <= R::BLOCK_SECONDS)
    {
      return D::CUE_BLOCK;
    }
    if (touchInRead <= R::COMMIT_SECONDS)
    {
      return D::CUE_SWING;
    }
    return 0.0F;
  };
  const float readCue =
      footCue(recovery > 1e-4F ? recovery + randomFloat(-error, error) : 0.0F,
              touchIn + randomFloat(-error, error));
  const float shown =
      D::CUE_SHOWN_SHARE * std::max(0.0F, ballStretch(owner, contact) -
                                              ballStretch(defender, contact));
  const float cue = std::max(readCue, shown);
  float bar =
      D::GO_IN_BAR -
      (D::GO_IN_PRESSING * getEffectiveSliders(defender.isHomeTeam).pressing);
  if (instructionAgainst(owner) == OppositionInstruction::Press)
  {
    bar -= D::GO_IN_PRESS_ORDER;
  }
  const DefensiveCoverCapybara cover = defensiveCoverCapybara(defender);
  if (cover.covered)
  {
    bar -= D::GO_IN_COVER;
  }
  if (cover.lastMan)
  {
    bar += D::GO_IN_LAST_MAN;
  }
  if (inPenaltyArea(defender.position, defender.isHomeTeam))
  {
    bar += D::GO_IN_OWN_BOX;
  }
  bar = std::max(bar, D::GO_IN_BAR_MIN);
  const bool goes = cue >= bar;
  // A misread: in on the carrier's free foot (row 5), nothing shown.
  if (goes && footCue(recovery, touchIn) <= 0.0F && shown < bar)
  {
    ++stats.goInsOnFreeFoot;
  }
  if (goes && defender.player != nullptr)
  {
    heldTackleCapybara = HeldTackleCapybara{
        .player = defender.player->getId(),
        .step = stepCounter,
        .contact = contact,
        .intent = tackleIntentRuleCapybara(defender, owner, contact, false)};
  }
  return goes;
}

TackleIntent MatchEngine::chooseTackleIntentCapybara(
    const MatchPlayer& tackler, const MatchPlayer& attacker, PitchPoint contact,
    bool freeBall)
{
  // A contest he went into: the intent his option carried (step K), once.
  if (!freeBall && heldTackleCapybara && tackler.player != nullptr &&
      heldTackleCapybara->player == tackler.player->getId() &&
      heldTackleCapybara->step == stepCounter &&
      heldTackleCapybara->contact.x == contact.x &&
      heldTackleCapybara->contact.y == contact.y)
  {
    const TackleIntent intent = heldTackleCapybara->intent;
    heldTackleCapybara.reset();
    return intent;
  }
  // A free ball outside the carrier's reach (step K's free-ball way in):
  // going for it and what for are the same moment, decided here.
  return tackleIntentRuleCapybara(tackler, attacker, contact, freeBall);
}

TackleIntent MatchEngine::tackleIntentRuleCapybara(const MatchPlayer& tackler,
                                                   const MatchPlayer& attacker,
                                                   PitchPoint contact,
                                                   bool freeBall) const
{
  // Tackling plan step 5. A free ball well away from the carrier he simply
  // controls. Otherwise he knocks it away in his own third, at a stretch or
  // as the last man, and wins it everywhere else.
  using D = MatchTuning::Defending;
  if (freeBall &&
      distance(attacker.position, contact) >= D::CONTROL_FREE_BALL_METRES)
  {
    return TackleIntent::CONTROL;
  }
  const float fromOwnGoal =
      pitch.fromOwnGoalLine(tackler.position, tackler.isHomeTeam);
  if (fromOwnGoal < D::KNOCK_AWAY_WITHIN_METRES ||
      ballStretch(tackler, contact) >= D::KNOCK_AWAY_STRETCH ||
      defensiveCoverCapybara(tackler).lastMan)
  {
    return TackleIntent::CLEAR;
  }
  return TackleIntent::WIN;
}

MatchEngine::CarrierWeightsCapybara MatchEngine::neutralCarrierWeightsCapybara()
{
  // Weighting (brain redesign plan section 15): each weight's default from
  // MatchTuning. The weights a layer sets stay 0 here.
  using D = MatchTuning::Decision;
  using P = MatchTuning::Passing;
  namespace T = TacticsTuning;
  CarrierWeightsCapybara weights;
  // Shared by pass and carry: progress and space (the pass's weights).
  weights.progress = P::BASE_PROGRESS_WEIGHT;
  weights.space = P::OPENNESS_WEIGHT;
  // Pass.
  weights.lengthOffIdeal = -P::DISTANCE_PENALTY_PER_METRE;
  weights.safeOutlet = P::SAFE_OUTLET_WEIGHT;
  weights.completion = P::COMPLETION_UTILITY_WEIGHT;
  weights.runner = P::ACTIVE_RUNNER_UTILITY_BONUS;
  weights.spaceBehind = P::SPACE_BEHIND_UTILITY;
  // Threat: one weight for every option, the shot's own scale.
  weights.threat = D::SHOT_SCORE_SCALE;
  // Shot.
  weights.shotThreat = D::SHOT_SCORE_SCALE;
  weights.wideShot = D::WIDE_SHOT_DISCOUNT;
  weights.goalOpen = D::GOAL_OPEN_SHOT_BONUS;
  weights.cutInsideShot = D::CUT_INSIDE_SHOT_BONUS;
  weights.weakFootShot = -T::WEAK_FOOT_SHOT_PENALTY;
  // Carry. Keeping the ball counts as much as a completed pass does.
  weights.carryKeep = P::COMPLETION_UTILITY_WEIGHT;
  weights.carryHeld = -D::CARRY_HOLD_PENALTY_PER_SECOND;
  // Shield.
  weights.shieldKeep = P::COMPLETION_UTILITY_WEIGHT;
  return weights;
}

MatchEngine::CarrierWeightsCapybara MatchEngine::carrierWeightsCapybara(
    const MatchPlayer& carrier) const
{
  // The defaults, then each layer's steps on them (ratio^steps, step B5).
  // The game state reaches them only through the team sliders
  // (applyTeamMentalityCapybara).
  namespace T = TacticsTuning;
  const StrategySliders strategy = getEffectiveSliders(carrier.isHomeTeam);
  const RoleProfile& role = roleProfileOf(carrier);
  CarrierWeightsCapybara weights = neutralCarrierWeightsCapybara();
  using DecisionFrame::sliderSteps;
  using DecisionFrame::stepped;
  const float daringSteps = roleStepsCapybara(role.passDaring);
  // Progress (pass and carry): the attacking instruction, his role's
  // daring.
  weights.progressSteps = sliderSteps(strategy.offensiveBias) + daringSteps;
  // Keeping the ball (pass completion, carry and shield keep): less for
  // the risk-taking instruction, and for a daring passer a third as much
  // as on progress (the ratio of the two daring terms before step B5).
  weights.keepSteps = -sliderSteps(strategy.riskTaking) -
                      (daringSteps * T::PASS_DARING_SAFETY_WEIGHT /
                       T::PASS_DARING_PROGRESS_WEIGHT);
  // Shot threat: his role, a shout.
  weights.shotThreatSteps = roleStepsCapybara(role.shotBias) +
                            shoutShotStepsCapybara(carrier.isHomeTeam);
  // The brain flow tool with some layers off: only the layers on count.
  if (flowLayers != BrainFlow::ALL_LAYERS)
  {
    const LayerStepsCapybara parts = layerStepsCapybara(carrier);
    const auto sumOn =
        [mask = flowLayers](const std::array<float, FLOW_LAYER_COUNT>& steps)
    {
      float sum = 0.0F;
      for (std::size_t layer = 0; layer < steps.size(); ++layer)
      {
        if (BrainFlow::isOn(mask, static_cast<BrainFlow::Layer>(layer)))
        {
          sum += steps.at(layer);
        }
      }
      return sum;
    };
    weights.progressSteps = sumOn(parts.progress);
    weights.keepSteps = sumOn(parts.keep);
    weights.shotThreatSteps = sumOn(parts.shotThreat);
  }
  weights.progress = stepped(weights.progress, weights.progressSteps);
  weights.completion = stepped(weights.completion, weights.keepSteps);
  weights.carryKeep = stepped(weights.carryKeep, weights.keepSteps);
  weights.shieldKeep = stepped(weights.shieldKeep, weights.keepSteps);
  weights.shotThreat = stepped(weights.shotThreat, weights.shotThreatSteps);
  return weights;
}

MatchEngine::LayerStepsCapybara MatchEngine::layerStepsCapybara(
    const MatchPlayer& carrier) const
{
  // The same steps as carrierWeightsCapybara, split by layer. The team
  // sliders: the manager's tactics, then the team mentality (the game
  // state), then the shout (computeEffectiveSliders).
  static_assert(BrainFlow::LAYER_COUNT == FLOW_LAYER_COUNT,
                "LayerStepsCapybara holds a step per BrainFlow::Layer");
  static_assert(BrainFlow::ALL_LAYERS == FLOW_ALL_LAYERS,
                "flowLayers starts with every BrainFlow::Layer on");
  namespace T = TacticsTuning;
  using BrainFlow::Layer;
  using DecisionFrame::sliderSteps;
  const auto at = [](Layer layer) { return static_cast<std::size_t>(layer); };
  const StrategySliders tactics =
      (carrier.isHomeTeam ? homeStrategy : awayStrategy).getSliders();
  StrategySliders mentality = tactics;
  if (drillRules.layers)
  {
    applyTeamMentality(mentality, carrier.isHomeTeam);
  }
  const StrategySliders effective = getEffectiveSliders(carrier.isHomeTeam);
  const RoleProfile& role = roleProfileOf(carrier);
  const float daringSteps = roleStepsCapybara(role.passDaring);
  LayerStepsCapybara steps;
  steps.progress[at(Layer::INSTRUCTIONS)] = sliderSteps(tactics.offensiveBias);
  steps.progress[at(Layer::GAME_STATE)] =
      sliderSteps(mentality.offensiveBias) - sliderSteps(tactics.offensiveBias);
  steps.progress[at(Layer::SHOUT)] = sliderSteps(effective.offensiveBias) -
                                     sliderSteps(mentality.offensiveBias);
  steps.progress[at(Layer::ROLE)] = daringSteps;
  steps.keep[at(Layer::INSTRUCTIONS)] = -sliderSteps(tactics.riskTaking);
  steps.keep[at(Layer::GAME_STATE)] =
      sliderSteps(tactics.riskTaking) - sliderSteps(mentality.riskTaking);
  steps.keep[at(Layer::SHOUT)] =
      sliderSteps(mentality.riskTaking) - sliderSteps(effective.riskTaking);
  steps.keep[at(Layer::ROLE)] = -(daringSteps * T::PASS_DARING_SAFETY_WEIGHT /
                                  T::PASS_DARING_PROGRESS_WEIGHT);
  steps.shotThreat[at(Layer::ROLE)] = roleStepsCapybara(role.shotBias);
  steps.shotThreat[at(Layer::SHOUT)] =
      shoutShotStepsCapybara(carrier.isHomeTeam);
  return steps;
}

void MatchEngine::noteFlowWeightsCapybara(
    const MatchPlayer& carrier, const CarrierWeightsCapybara& weights) const
{
  if (flowRecord == nullptr)
  {
    return;
  }
  const CarrierWeightsCapybara neutral = neutralCarrierWeightsCapybara();
  const LayerStepsCapybara steps = layerStepsCapybara(carrier);
  const BrainFlow::LayerSteps none{};
  // Steps only from the layers the tool has on, as carrierWeightsCapybara.
  const auto onOnly =
      [mask = flowLayers](const std::array<float, FLOW_LAYER_COUNT>& all)
  {
    BrainFlow::LayerSteps kept{};
    for (std::size_t layer = 0; layer < all.size(); ++layer)
    {
      if (BrainFlow::isOn(mask, static_cast<BrainFlow::Layer>(layer)))
      {
        kept.at(layer) = all.at(layer);
      }
    }
    return kept;
  };
  auto& list = flowRecord->weights;
  const auto note = [&list](const char* name, float base,
                            const BrainFlow::LayerSteps& layerSteps, float used)
  {
    list.push_back(
        {.name = name, .base = base, .steps = layerSteps, .used = used});
  };
  note("progress", neutral.progress, onOnly(steps.progress), weights.progress);
  note("space", neutral.space, none, weights.space);
  note("completion (keep)", neutral.completion, onOnly(steps.keep),
       weights.completion);
  note("carry keep", neutral.carryKeep, onOnly(steps.keep), weights.carryKeep);
  note("shield keep", neutral.shieldKeep, onOnly(steps.keep),
       weights.shieldKeep);
  note("threat (pass, carry)", neutral.threat, none, weights.threat);
  note("shot threat", neutral.shotThreat, onOnly(steps.shotThreat),
       weights.shotThreat);
  note("length off ideal", neutral.lengthOffIdeal, none,
       weights.lengthOffIdeal);
  note("safe outlet", neutral.safeOutlet, none, weights.safeOutlet);
  note("runner", neutral.runner, none, weights.runner);
  note("space in behind", neutral.spaceBehind, none, weights.spaceBehind);
  note("time on the ball", neutral.carryHeld, none, weights.carryHeld);
  note("cutting inside (shot)", neutral.cutInsideShot, none,
       weights.cutInsideShot);
  note("weak foot (shot)", neutral.weakFootShot, none, weights.weakFootShot);
  note("wide angle (shot)", neutral.wideShot, none, weights.wideShot);
}

float MatchEngine::addShotCoreCapybara(DecisionFrame::Score& shot,
                                       const CarrierWeightsCapybara& weights,
                                       const MatchPitch& pitch, PitchPoint at,
                                       bool attackingRight, float shotXG)
{
  using D = MatchTuning::Decision;
  using S = TermSource;
  // Range eligibility: the role terms that are still flat (cutting
  // inside) fade with it, so they never invent a nonsensical long shot.
  const float range = std::clamp(
      (shotXG - D::SHOT_ELIGIBILITY_FLOOR) / D::SHOT_ELIGIBILITY_RANGE, 0.0F,
      1.0F);
  const float depth =
      attackingRight ? pitch.fractionX(at) : 1.0F - pitch.fractionX(at);
  const bool finalThird = depth >= MatchTuning::Rules::HOME_FINAL_THIRD_START;
  const bool wideInFinalThird =
      finalThird && std::abs(pitch.atY(MatchTuning::Pitch::CENTRE) - at.y) >=
                        D::WIDE_SHOT_WIDTH_METRES;
  shot.add("threat (expected goals)", S::SITUATION, shotXG, weights.shotThreat);
  if (wideInFinalThird)
  {
    shot.scale("wide-angle discount", S::LOCATION, weights.wideShot);
  }
  return range;
}

namespace
{
Vector2F rotatedBy(Vector2F vector, float degrees)
{
  const float radians =
      degrees * std::numbers::pi_v<float> / DEGREES_PER_HALF_TURN;
  const float cosine = std::cos(radians);
  const float sine = std::sin(radians);
  return {.x = (vector.x * cosine) - (vector.y * sine),
          .y = (vector.x * sine) + (vector.y * cosine)};
}

/** Metres from `point` to the segment from `from` to `to`. */
float segmentMetres(PitchPoint point, PitchPoint from, PitchPoint to)
{
  const Vector2F along = to - from;
  const Vector2F toPoint = point - from;
  const float lengthSquared = (along.x * along.x) + (along.y * along.y);
  const float share =
      lengthSquared > 0.0F
          ? std::clamp(
                ((toPoint.x * along.x) + (toPoint.y * along.y)) / lengthSquared,
                0.0F, 1.0F)
          : 0.0F;
  const PitchPoint nearest{from.x + (along.x * share),
                           from.y + (along.y * share)};
  return distance(point, nearest);
}

/** A carry's measures at its 1 s point. */
struct CarryMeasures
{
  float space;
  float keep;
  float gain;
  float endXG;
};

/** A carry's terms: space where it ends, progress and threat (each times
 * the chance he keeps it) and keeping it. The search (true chances) and
 * the chosen carry (as he reads them) are scored the same way. */
void addCarryTerms(DecisionFrame::Score& carry,
                   const MatchEngine::CarrierWeightsCapybara& weights,
                   const CarryMeasures& measures)
{
  using S = TermSource;
  carry.add("space where the ball ends", S::SITUATION, measures.space,
            weights.space);
  carry.add("progress (keeps it x gain)", S::SITUATION,
            measures.keep * measures.gain, weights.progress);
  carry.add("keeps the ball", S::SITUATION, measures.keep, weights.carryKeep);
  carry.add("threat (keeps it x xG at the carry's end)", S::SITUATION,
            measures.keep * measures.endXG, weights.threat);
}
}  // namespace

const MatchEngine::HeldCarryCapybara* MatchEngine::heldCarryOfCapybara(
    const MatchPlayer& player) const
{
  if (!heldCarryCapybara || player.player == nullptr ||
      heldCarryCapybara->player != player.player->getId() ||
      heldCarryCapybara->possessionStart !=
          getPossessionHistory().possessionStartSeconds)
  {
    return nullptr;
  }
  return &*heldCarryCapybara;
}

float MatchEngine::tackleWinChanceCapybara(const MatchPlayer& tackler,
                                           const MatchPlayer& carrier,
                                           bool shielding) const
{
  // The same challenge the rules would settle (resolveTackle): his body
  // against the carrier's, the team edges and the tackler's pressing; a
  // standing tackle from the front with the ball at the carrier's foot.
  MatchRules::TackleContext context;
  context.make = tackleMake(tackler, nullptr);
  context.resist = tackleResist(carrier, shielding, nullptr);
  context.defenderEdge =
      teamEdge(tackler.isHomeTeam) * MatchTuning::Rules::DUEL_EDGE_WEIGHT;
  context.carrierEdge =
      teamEdge(carrier.isHomeTeam) * MatchTuning::Rules::DUEL_EDGE_WEIGHT;
  context.pressing = getEffectiveSliders(tackler.isHomeTeam).pressing;
  return MatchRules::tackleWinChance(context);
}

float MatchEngine::pathKeepCapybara(const MatchPlayer& carrier,
                                    PitchPoint to) const
{
  // The simple path keep (step C): the opponents where they stand now, as
  // near his line as the pressure radius. A longer, faster carry passes
  // nearer more of them. Better later: whether each can reach the line
  // before he passes (plan section 16, "Better measures, later").
  constexpr float RADIUS = MatchTuning::Decision::PRESSURE_RADIUS_METRES;
  float worst = 0.0F;
  for (const MatchPlayer& opponent : players)
  {
    if (opponent.isHomeTeam == carrier.isHomeTeam || !active(opponent))
    {
      continue;
    }
    const float pressure = std::clamp(
        (RADIUS - segmentMetres(opponent.position, carrier.position, to)) /
            RADIUS,
        0.0F, 1.0F);
    if (pressure > 0.0F)
    {
      worst = std::max(
          worst, pressure * tackleWinChanceCapybara(opponent, carrier, false));
    }
  }
  return 1.0F - worst;
}

std::vector<Vector2F> MatchEngine::carryDirectionsCapybara(
    const MatchPlayer& carrier) const
{
  // Straight down the pitch and turned off it, and towards the goal;
  // backwards near his own goal ruled out (the first touch's guard, as an
  // Options rule).
  const float forward = carrier.isHomeTeam ? 1.0F : -1.0F;
  const bool nearOwnGoal =
      pitch.fromOwnGoalLine(carrier.position, carrier.isHomeTeam) <
      MatchTuning::SetPiece::CROSS_CLEARANCE_WITHIN_METRES;
  std::vector<Vector2F> directions;
  directions.reserve(CARRY_ANGLES_DEGREES.size() + 1);
  for (const float angle : CARRY_ANGLES_DEGREES)
  {
    directions.push_back(rotatedBy({.x = forward, .y = 0.0F}, angle));
  }
  const PitchPoint goal =
      pitch.at(carrier.isHomeTeam ? 1.0F : 0.0F, MatchTuning::Pitch::CENTRE);
  const Vector2F towardsGoal = metricDirection(carrier.position, goal);
  const float sameCosine =
      std::cos(SAME_DIRECTION_DEGREES * std::numbers::pi_v<float> /
               DEGREES_PER_HALF_TURN);
  const auto close = [&](Vector2F direction)
  {
    return (direction.x * towardsGoal.x) + (direction.y * towardsGoal.y) >
           sameCosine;
  };
  if (length(towardsGoal) > EPSILON && std::ranges::none_of(directions, close))
  {
    directions.push_back(towardsGoal);
  }
  if (nearOwnGoal)
  {
    std::erase_if(directions, [forward](Vector2F direction)
                  { return direction.x * forward < -EPSILON; });
  }
  return directions;
}

std::optional<MatchEngine::CarryCandidateCapybara>
MatchEngine::carryCandidateCapybara(const MatchPlayer& carrier,
                                    const CarrierWeightsCapybara& weights,
                                    Vector2F direction,
                                    CarryPaceCapybara pace) const
{
  // Its 1 s point, measured and scored on the true chances; none off the
  // pitch.
  const float reach = pace.metresPerSecond * CARRY_HORIZON_SECONDS;
  const PitchPoint end{carrier.position.x + (direction.x * reach),
                       carrier.position.y + (direction.y * reach)};
  const float minX =
      std::min(pitch.atX(0.0F), pitch.atX(1.0F)) + CARRY_PITCH_MARGIN_METRES;
  const float maxX =
      std::max(pitch.atX(0.0F), pitch.atX(1.0F)) - CARRY_PITCH_MARGIN_METRES;
  const float minY =
      std::min(pitch.atY(0.0F), pitch.atY(1.0F)) + CARRY_PITCH_MARGIN_METRES;
  const float maxY =
      std::max(pitch.atY(0.0F), pitch.atY(1.0F)) - CARRY_PITCH_MARGIN_METRES;
  if (end.x < minX || end.x > maxX || end.y < minY || end.y > maxY)
  {
    return std::nullopt;
  }
  CarryCandidateCapybara candidate{.direction = direction,
                                   .urgency = pace.level,
                                   .end = end,
                                   .keep = pathKeepCapybara(carrier, end),
                                   .endXG = carryEndXGCapybara(carrier, end),
                                   .score = 0.0F};
  DecisionFrame::Score terms;
  addCarryTerms(
      terms, weights,
      {.space = spaceAtCapybara(carrier.isHomeTeam, end),
       .keep = candidate.keep,
       .gain = progressCapybara(carrier.isHomeTeam, carrier.position, end),
       .endXG = candidate.endXG});
  candidate.score = terms.total();
  return candidate;
}

MatchEngine::CarryCandidateCapybara MatchEngine::bestCarryCapybara(
    const MatchPlayer& carrier, const CarrierWeightsCapybara& weights) const
{
  // Paces: the tops of his walk, jog and run bands and his top speed, each
  // capped by Dribbling (as carryPaceCapybara carries it).
  std::vector<CarryPaceCapybara> paces;
  for (const float level : CARRY_PACE_LEVELS)
  {
    const float metresPerSecond = carryPaceAtCapybara(carrier, level);
    if (paces.empty() || std::abs(metresPerSecond -
                                  paces.back().metresPerSecond) > SAME_PACE_MPS)
    {
      paces.push_back({.level = level, .metresPerSecond = metresPerSecond});
    }
  }
  std::optional<CarryCandidateCapybara> best;
  const auto keepBest = [&best](std::optional<CarryCandidateCapybara> next)
  {
    if (next && (!best || next->score > best->score))
    {
      best = next;
    }
  };
  const float forward = carrier.isHomeTeam ? 1.0F : -1.0F;
  for (const Vector2F direction : carryDirectionsCapybara(carrier))
  {
    for (const CarryPaceCapybara pace : paces)
    {
      const std::optional<CarryCandidateCapybara> candidate =
          carryCandidateCapybara(carrier, weights, direction, pace);
      if (candidate && flowRecord != nullptr)
      {
        // The brain flow tool: every candidate, its angle off straight
        // down the pitch (positive to his right as the tool draws it).
        const float angle =
            std::atan2(direction.y * forward, direction.x * forward) *
            DEGREES_PER_HALF_TURN / std::numbers::pi_v<float>;
        const bool fixed = std::ranges::any_of(
            CARRY_ANGLES_DEGREES,
            [angle](float grid)
            {
              return std::abs(std::remainder(angle - grid, DEGREES_PER_TURN)) <
                     FIXED_ANGLE_MATCH_DEGREES;
            });
        flowRecord->carryCandidates.push_back({.angleDegrees = angle,
                                               .towardsGoal = !fixed,
                                               .level = candidate->urgency,
                                               .end = candidate->end,
                                               .keep = candidate->keep,
                                               .endXG = candidate->endXG,
                                               .score = candidate->score});
      }
      keepBest(candidate);
    }
  }
  if (best)
  {
    return *best;
  }
  // Hemmed in on every side: stand on it (a walk, going nowhere).
  CarryCandidateCapybara still;
  still.direction = {.x = forward, .y = 0.0F};
  still.end = carrier.position;
  still.keep = pathKeepCapybara(carrier, carrier.position);
  still.endXG = carryEndXGCapybara(carrier, carrier.position);
  return still;
}

namespace
{
/** The nearest active opponent of `homeTeam` to `at`, in metres. */
float nearestOpponentMetres(const std::vector<MatchPlayer>& players,
                            bool homeTeam, PitchPoint at, const auto& isActive)
{
  float nearest = std::numeric_limits<float>::max();
  for (const auto& opponent : players)
  {
    if (opponent.isHomeTeam != homeTeam && isActive(opponent))
    {
      nearest = std::min(nearest, distance(at, opponent.position));
    }
  }
  return nearest;
}
}  // namespace

float MatchEngine::carryEndXGCapybara(const MatchPlayer& carrier,
                                      PitchPoint end) const
{
  return gameShotXGCapybara(carrier, end);
}

const MatchPlayer* MatchEngine::opponentKeeperCapybara(bool homeTeam) const
{
  for (const MatchPlayer& player : players)
  {
    if (player.isHomeTeam != homeTeam && player.isGoalkeeper && active(player))
    {
      return &player;
    }
  }
  return nullptr;
}

float MatchEngine::gameShotXGCapybara(const MatchPlayer& shooter,
                                      PitchPoint from) const
{
  // The game xG maps (game xG plan), where this goal has them; else the
  // fitted model, with the nearest opponent's pressure as before.
  const GameXgTable* empty =
      GameXg::table(GameXgTable::EMPTY_GOAL, pitch.goal.kind);
  if (empty == nullptr)
  {
    const float nearest =
        nearestOpponentMetres(players, shooter.isHomeTeam, from,
                              [](const MatchPlayer& p) { return active(p); });
    return xgBreakdown(pitch, from, shooter.isHomeTeam, shooter.shooting,
                       nearest)
        .xg;
  }
  // The maps attack the right-hand goal with a right-footed shooter: turned
  // round for the other end, mirrored for a left-footer (the engine's feet
  // mirror each other).
  constexpr float RATING_SCALE = 100.0F;
  const float goalX = pitch.atX(shooter.isHomeTeam ? 1.0F : 0.0F);
  const float line = std::abs(goalX - from.x);
  float side = from.y - pitch.atY(MatchTuning::Pitch::CENTRE);
  if (!shooter.isHomeTeam)
  {
    side = -side;
  }
  if ((shooter.player != nullptr) && shooter.player->getFoot() == Foot::Left)
  {
    side = -side;
  }
  const float shooting = shooter.shooting * RATING_SCALE;
  float chance = empty->value(line, side, shooting);
  // A keeper in his usual place takes his share off it (the keeper map's
  // factor); how far he is from it is the shot score's "goal open" term.
  if (const MatchPlayer* keeper = opponentKeeperCapybara(shooter.isHomeTeam))
  {
    if (const GameXgTable* withKeeper =
            GameXg::table(GameXgTable::KEEPER, pitch.goal.kind))
    {
      chance *= withKeeper->value(line, side, shooting,
                                  keeper->goalkeeping * RATING_SCALE);
    }
  }
  // And the bodies in the way (layer 3).
  return chance * bodiesPassCapybara(shooter, from);
}

namespace
{
/** Half (an even split, a middle). */
constexpr float HALF_SHARE = 0.5F;
/** As strikeShotBodyBinturong launches a driven shot: rounds settling the
 * lift against the pace, the least of the pace kept over the ground, and
 * how far past his launch speed counts as beyond his power. */
constexpr int LAUNCH_ROUNDS = 4;
constexpr float QUARTER_SHARE = 0.25F;
constexpr float LAUNCH_SLACK = 1.01F;

/** One line of the aim cone: where in the goal (m across from its middle,
 * pitch y; m up) and its weight. */
struct ConeLine
{
  float aimY = 0.0F;
  float aimZ = 0.0F;
  float weight = 0.0F;
  /** Its height's flight (an index into the flights). */
  std::size_t flight = 0;
};

/** The ball's height and time along its flight, every `step` metres. */
struct FlightProfile
{
  float step = 1.0F;
  std::vector<float> height;
  std::vector<float> seconds;
  [[nodiscard]] std::pair<float, float> at(float along) const
  {
    if (height.empty())
    {
      return {0.0F, 0.0F};
    }
    const float position =
        std::clamp(along / step, 0.0F, static_cast<float>(height.size() - 1));
    const auto low = std::min(static_cast<std::size_t>(position),
                              height.size() > 1 ? height.size() - 2 : 0);
    const std::size_t high = std::min(low + 1, height.size() - 1);
    const float share = position - static_cast<float>(low);
    return {height[low] + ((height[high] - height[low]) * share),
            seconds[low] + ((seconds[high] - seconds[low]) * share)};
  }
};

/** What the aim cone is built from: the goal (its middle's y, its
 * half-width), the keeper's offset from its middle and the shooter's
 * Shooting (0-1). */
struct ConeSetup
{
  float middle = 0.0F;
  float halfGoal = 0.0F;
  float keeperOffset = 0.0F;
  float shooting = 0.0F;
};

/** The middle of the `index`th of `samples` equal shares of 0-1. */
float sampleSpot(int index, int samples)
{
  return (static_cast<float>(index) + HALF_SHARE) / static_cast<float>(samples);
}

/** The aim heights the cone samples (m), evenly over the aim's range. */
std::vector<float> coneHeights(int samples)
{
  using Sh = MatchTuning::Shooting;
  std::vector<float> heights;
  heights.reserve(static_cast<std::size_t>(samples));
  for (int h = 0; h < samples; ++h)
  {
    heights.push_back(Sh::AIM_MIN_HEIGHT_METRES +
                      ((Sh::AIM_MAX_HEIGHT_METRES - Sh::AIM_MIN_HEIGHT_METRES) *
                       sampleSpot(h, samples)));
  }
  return heights;
}

/** The aim cone, as chooseShotAimCapybara picks (open play, a foot): the
 * side away from the keeper (a coin flip if he's central, or on the
 * near-side chance), inset from the post; or a power shot near him; at
 * each of `heights`. Each choice's range sampled evenly. */
std::vector<ConeLine> aimCone(const ConeSetup& setup,
                              const std::vector<float>& heights)
{
  using Sh = MatchTuning::Shooting;
  const int samples = MatchTuning::Decision::BLOCK_CONE_SAMPLES;
  const float farShare =
      std::abs(setup.keeperOffset) < Sh::KEEPER_CENTRED_METRES
          ? HALF_SHARE
          : 1.0F - (HALF_SHARE * Sh::NEAR_SIDE_AIM_CHANCE);
  const float farSide = setup.keeperOffset > 0.0F ? -1.0F : 1.0F;
  const float powerShare =
      std::clamp(Sh::POWER_SHOT_BASE_CHANCE -
                     (setup.shooting * Sh::POWER_SHOT_SKILL_REDUCTION),
                 0.0F, 1.0F);
  const float insetFrom = Sh::AIM_POST_INSET_BASE +
                          ((1.0F - setup.shooting) * Sh::AIM_POST_INSET_SKILL);
  const float w =
      1.0F / static_cast<float>(samples * static_cast<int>(heights.size()));
  std::vector<ConeLine> cone;
  constexpr std::size_t AIM_CHOICES = 3;  // far side, near side, power
  cone.reserve(static_cast<std::size_t>(samples) * heights.size() *
               AIM_CHOICES);
  for (int i = 0; i < samples; ++i)
  {
    const float spot = sampleSpot(i, samples);
    const float across = std::max(
        0.0F, setup.halfGoal - (insetFrom + (Sh::AIM_INSET_SPREAD * spot)));
    const float powerAcross =
        setup.keeperOffset +
        (Sh::POWER_SHOT_WIDTH_METRES * ((2.0F * spot) - 1.0F));
    for (std::size_t h = 0; h < heights.size(); ++h)
    {
      cone.push_back({.aimY = setup.middle + (farSide * across),
                      .aimZ = heights[h],
                      .weight = (1.0F - powerShare) * farShare * w,
                      .flight = h});
      cone.push_back({.aimY = setup.middle - (farSide * across),
                      .aimZ = heights[h],
                      .weight = (1.0F - powerShare) * (1.0F - farShare) * w,
                      .flight = h});
      cone.push_back({.aimY = setup.middle + powerAcross,
                      .aimZ = heights[h],
                      .weight = powerShare * w,
                      .flight = h});
    }
  }
  return cone;
}

/** A body that may be in the way, as the estimate reads him: where he is
 * and how he moves, his hip and head heights, which leg-table kind he is
 * and his blocking rating (1-100). */
struct BlockBody
{
  PitchPoint position;
  Vector2F velocity{};
  float hip = 0.0F;
  float top = 0.0F;
  GameXgLegTable::Kind kind = GameXgLegTable::Kind::OPPONENT_STANDING;
  float rating = 0.0F;
};

/** One line a ball can take: along `heading` (unit) to the goal line
 * `length` m off, its flight raised `rise` m a metre. */
struct ShotLine
{
  Vector2F heading{};
  float length = 0.0F;
  float rise = 0.0F;
};

/** A shot's lines against the bodies in its way (game xG layer 3). */
struct BlockScene
{
  PitchPoint from;
  float goalX = 0.0F;
  float middle = 0.0F;
  float halfGoal = 0.0F;
  const GameXgLegTable* legs = nullptr;
  std::vector<BlockBody> bodies;

  /** A torso met off-centre (`offLeft` m to the left of the line, at
   * `at`): the ball glances away, turned less the nearer the edge (the
   * torso's spring-back across its surface, its along-kept along it); true
   * if it then still crosses between the posts. */
  [[nodiscard]] bool glancesIn(Vector2F heading, PitchPoint at,
                               float offLeft) const
  {
    const auto torso = static_cast<std::size_t>(ShotBodyPart::TORSO);
    const float ballRadius = MatchTuning::Units::BALL_RADIUS_METRES;
    const float torsoReach = MatchTuning::Body::RADIUS_METRES + ballRadius;
    const float sine = std::min(std::abs(offLeft) / torsoReach, 1.0F);
    const float cosine = std::sqrt(1.0F - (sine * sine));
    const float into = MatchTuning::ShotBlock::SPRING_BACK[torso] * cosine;
    const float onward = MatchTuning::ShotBlock::ALONG_KEPT[torso] * sine;
    const float turned = std::atan2(into, onward) +
                         (std::numbers::pi_v<float> / 2.0F) - std::asin(sine);
    // The ball passes the side of him it is off: turned that way.
    const float turn = (offLeft > 0.0F ? -1.0F : 1.0F) * turned;
    const Vector2F way{
        .x = (heading.x * std::cos(turn)) - (heading.y * std::sin(turn)),
        .y = (heading.x * std::sin(turn)) + (heading.y * std::cos(turn))};
    if ((goalX - at.x) * way.x <= EPSILON)
    {
      return false;
    }
    const float crossing = at.y + (way.y * (goalX - at.x) / way.x);
    return std::abs(crossing - middle) < halfGoal - ballRadius;
  }

  /** The chance one body lets a ball along `heading` (unit; to the goal
   * line `length` m off, its flight raised `rise` m a metre) past: where
   * he'll be as it gets level with him (carried on at his speed); his
   * torso (hip to the top of his head) stops it unless it glances in, his
   * leg-space by the leg table. */
  [[nodiscard]] float passesBody(const BlockBody& body,
                                 const FlightProfile& flight,
                                 const ShotLine& line) const
  {
    const Vector2F heading = line.heading;
    const float length = line.length;
    const float rise = line.rise;
    const float ballRadius = MatchTuning::Units::BALL_RADIUS_METRES;
    const float alongNow = ((body.position.x - from.x) * heading.x) +
                           ((body.position.y - from.y) * heading.y);
    if (alongNow <= 0.0F || alongNow >= length)
    {
      return 1.0F;
    }
    const float arrive = flight.at(alongNow).second;
    const PitchPoint then{body.position.x + (body.velocity.x * arrive),
                          body.position.y + (body.velocity.y * arrive)};
    const float along =
        ((then.x - from.x) * heading.x) + ((then.y - from.y) * heading.y);
    if (along <= 0.0F || along >= length)
    {
      return 1.0F;
    }
    const float offLeft =
        ((then.x - from.x) * -heading.y) + ((then.y - from.y) * heading.x);
    const auto heightAt = [&](float out)
    { return std::max(flight.at(out).first + (rise * out), ballRadius); };
    const float height = heightAt(along);
    if (height < body.hip - ballRadius)
    {
      return 1.0F - std::clamp(legs->value(body.kind, body.rating, along,
                                           std::abs(offLeft)),
                               0.0F, 1.0F);
    }
    // Through his torso's column the ball is lowest at its front (rising)
    // or its back (falling): over his head only if it clears both.
    const float torsoReach = MatchTuning::Body::RADIUS_METRES + ballRadius;
    if (std::abs(offLeft) > torsoReach)
    {
      return 1.0F;
    }
    const float depth =
        std::sqrt((torsoReach * torsoReach) - (offLeft * offLeft));
    const float lowest = std::min(heightAt(std::max(along - depth, 0.0F)),
                                  heightAt(std::min(along + depth, length)));
    const bool torso = lowest <= body.top + ballRadius;
    return torso && !glancesIn(heading,
                               PitchPoint{from.x + (heading.x * along),
                                          from.y + (heading.y * along)},
                               offLeft)
               ? 0.0F
               : 1.0F;
  }

  /** The chance a ball along that line gets past every body. */
  [[nodiscard]] float throughLine(const FlightProfile& flight,
                                  const ShotLine& line) const
  {
    float through = 1.0F;
    for (const BlockBody& body : bodies)
    {
      through *= passesBody(body, flight, line);
      if (through <= 0.0F)
      {
        break;
      }
    }
    return through;
  }

  /** One aim line's share of shots past the bodies: the shooter misses it
   * by his error (equal shares over the ground, turning the line, and
   * upward, raising its flight by the distance times the angle). */
  template <std::size_t ACROSS, std::size_t UP>
  [[nodiscard]] float throughAim(const ConeLine& line,
                                 const FlightProfile& flight,
                                 const std::array<float, ACROSS>& acrossErrors,
                                 const std::array<float, UP>& upErrors) const
  {
    const PitchPoint aim{goalX, line.aimY};
    const float aimLength = distance(from, aim);
    if (aimLength <= EPSILON)
    {
      return 1.0F;
    }
    const Vector2F aimHeading{.x = (aim.x - from.x) / aimLength,
                              .y = (aim.y - from.y) / aimLength};
    float passed = 0.0F;
    float lines = 0.0F;
    for (const float acrossError : acrossErrors)
    {
      const Vector2F heading{.x = (aimHeading.x * std::cos(acrossError)) -
                                  (aimHeading.y * std::sin(acrossError)),
                             .y = (aimHeading.x * std::sin(acrossError)) +
                                  (aimHeading.y * std::cos(acrossError))};
      // To the goal line (past it, nobody's in the way).
      if ((goalX - from.x) * heading.x <= EPSILON)
      {
        continue;
      }
      const float length = (goalX - from.x) / heading.x;
      for (const float upError : upErrors)
      {
        passed += throughLine(
            flight,
            {.heading = heading, .length = length, .rise = std::tan(upError)});
        lines += 1.0F;
      }
    }
    return lines > 0.0F ? passed / lines : 1.0F;
  }
};
}  // namespace

std::pair<float, float> MatchEngine::drivenLaunchCapybara(float distanceMetres,
                                                          float speed,
                                                          float startHeight,
                                                          float arrivalHeight,
                                                          BallSpin spin) const
{
  // As strikeShotBodyBinturong launches a driven shot: the lift comes off
  // the pace over the ground; too far for his power, the full-power loft.
  constexpr float RADIANS_PER_DEGREE = std::numbers::pi_v<float> / 180.0F;
  float ground = speed;
  float lift = 0.0F;
  for (int round = 0; round < LAUNCH_ROUNDS; ++round)
  {
    lift = loftVerticalSpeed(distanceMetres, ground, startHeight, arrivalHeight,
                             spin);
    ground = std::sqrt(std::max((speed * speed) - (lift * lift),
                                QUARTER_SHARE * speed * speed));
  }
  lift = loftVerticalSpeed(distanceMetres, ground, startHeight, arrivalHeight,
                           spin);
  if (std::hypot(ground, lift) > speed * LAUNCH_SLACK ||
      lift > speed * std::sin(MatchTuning::Shooting::MAX_LAUNCH_DEGREES *
                              RADIANS_PER_DEGREE))
  {
    return fullPowerLoftBinturong(distanceMetres, speed, startHeight,
                                  arrivalHeight, spin);
  }
  return {ground, lift};
}

float MatchEngine::bodiesPassCapybara(const MatchPlayer& shooter,
                                      PitchPoint from) const
{
  using D = MatchTuning::Decision;
  using Sh = MatchTuning::Shooting;
  const GameXgLegTable* legChance = GameXg::legs();
  if ((legChance == nullptr) || !pitch.goal.present())
  {
    return 1.0F;
  }
  constexpr float RATING_SCALE = 100.0F;
  constexpr float REV = 2.0F * std::numbers::pi_v<float>;
  constexpr std::size_t TEAMMATE_KINDS = 3;
  const float ballRadius = MatchTuning::Units::BALL_RADIUS_METRES;
  // The ball leaves from where it is, not his middle: with it at his feet,
  // as far from any spot he might shoot from as it is from him now.
  if (getPossessionAttachment() == shooter.player)
  {
    from = PitchPoint{from.x + (ball.position.x - shooter.position.x),
                      from.y + (ball.position.y - shooter.position.y)};
  }
  BlockScene scene{.from = from,
                   .goalX = pitch.atX(shooter.isHomeTeam ? 1.0F : 0.0F),
                   .middle = pitch.atY(MatchTuning::Pitch::CENTRE),
                   .halfGoal = pitch.goal.halfWidth,
                   .legs = legChance,
                   .bodies = {}};

  // The bodies that may be in the way: all but the shooter and the
  // opponents' keeper; each as the leg table reads him (whose, how he
  // moves, his Defending and Tackling).
  const MatchPlayer* keeper = opponentKeeperCapybara(shooter.isHomeTeam);
  for (const MatchPlayer& body : players)
  {
    if (&body == &shooter || !active(body) || &body == keeper)
    {
      continue;
    }
    const float moving = std::hypot(body.velocity.x, body.velocity.y);
    // Standing, sidestepping or sprinting (the table's first three kinds).
    std::size_t kind = 0;
    if (moving >= D::BLOCK_SPRINT_SPEED)
    {
      kind = 2;
    }
    else if (moving >= D::BLOCK_STANDING_SPEED)
    {
      kind = 1;
    }
    if (body.isHomeTeam == shooter.isHomeTeam)
    {
      kind += TEAMMATE_KINDS;
    }
    scene.bodies.push_back(
        {.position = body.position,
         .velocity = body.velocity,
         .hip = bodyLines(body).chest,
         .top = body.heightMetres,
         .kind = static_cast<GameXgLegTable::Kind>(kind),
         .rating = RATING_SCALE * HALF_SHARE *
                   (body.defending + body.derivedStat(DerivedStat::TACKLING))});
  }
  if (scene.bodies.empty())
  {
    return 1.0F;
  }

  const std::vector<float> heights = coneHeights(D::BLOCK_CONE_SAMPLES);
  const std::vector<ConeLine> cone = aimCone(
      {.middle = scene.middle,
       .halfGoal = scene.halfGoal,
       .keeperOffset =
           (keeper != nullptr) ? keeper->position.y - scene.middle : 0.0F,
       .shooting = shooter.shooting},
      heights);

  // The ball's flight for each height: a driven shot at his usual share of
  // his power (as strikeShotBodyBinturong), lofted to reach that height at
  // the goal's middle, flown with its topspin.
  const float speed = shotPowerBinturong(shooter) * Sh::DEFAULT_POWER_SHARE;
  const BallSpin topspin{.side = 0.0F, .top = Sh::DRIVEN_TOPSPIN_REVS * REV};
  const float reach = distance(from, PitchPoint{scene.goalX, scene.middle});
  constexpr float DT = MatchTuning::Timing::FIXED_STEP_SECONDS /
                       static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
  std::vector<FlightProfile> flights;
  flights.reserve(heights.size());
  for (const float aimZ : heights)
  {
    const auto [ground, lift] =
        drivenLaunchCapybara(reach, speed, ballRadius, aimZ, topspin);
    FlightState flight{.horizontalSpeed = ground,
                       .verticalSpeed = lift,
                       .height = ballRadius,
                       .bounced = false,
                       .spin = topspin};
    FlightProfile& profile = flights.emplace_back();
    profile.step = ground * DT;
    profile.height.push_back(flight.height);
    profile.seconds.push_back(0.0F);
    float time = 0.0F;
    float along = 0.0F;
    while (along < reach + 1.0F && flight.horizontalSpeed > 0.0F &&
           time < MatchTuning::Ball::MAX_FLIGHT_SECONDS)
    {
      along += stepFlight(flight, DT);
      time += DT;
      // Resampled to even steps along (the ball slows with drag).
      while (static_cast<float>(profile.height.size()) * profile.step <= along)
      {
        profile.height.push_back(flight.height);
        profile.seconds.push_back(time);
      }
    }
  }

  // Each aim line, spread by the shooter's error (the leg table's, from the
  // same drill: a still ball, unpressured), weighted as the cone has it.
  const float shootingRating = RATING_SCALE * shooter.shooting;
  const auto acrossErrors = legChance->acrossPoints(shootingRating);
  const auto upErrors = legChance->upPoints(shootingRating);
  float passed = 0.0F;
  float total = 0.0F;
  for (const ConeLine& line : cone)
  {
    passed += line.weight * scene.throughAim(line, flights[line.flight],
                                             acrossErrors, upErrors);
    total += line.weight;
  }
  return total > 0.0F ? passed / total : 1.0F;
}

bool MatchEngine::goalOpenCapybara(const MatchPlayer& shooter) const
{
  using D = MatchTuning::Decision;
  const MatchPlayer* keeper = opponentKeeperCapybara(shooter.isHomeTeam);
  if ((keeper == nullptr) || !pitch.goal.present())
  {
    return false;
  }
  // Rushing him: closing on him faster than a keeper holding his place.
  const float towardX = shooter.position.x - keeper->position.x;
  const float towardY = shooter.position.y - keeper->position.y;
  const float apart = std::hypot(towardX, towardY);
  if (apart > EPSILON &&
      ((keeper->velocity.x * towardX) + (keeper->velocity.y * towardY)) /
              apart >
          D::KEEPER_RUSH_SPEED)
  {
    return true;
  }
  // Or more of the mouth left open than from his set position.
  const float goalX = pitch.atX(shooter.isHomeTeam ? 1.0F : 0.0F);
  const float middle = pitch.atY(MatchTuning::Pitch::CENTRE);
  const auto open = [&](PitchPoint at)
  {
    return GameXg::openShare(shooter.position, at, goalX, middle,
                             pitch.goal.halfWidth, D::KEEPER_COVER_METRES);
  };
  return open(keeper->position) -
             open(keeperSetPositionCapybara(*keeper, shooter.position)) >=
         D::GOAL_OPEN_SHARE_MARGIN;
}

float MatchEngine::progressCapybara(bool homeTeam, PitchPoint from,
                                    PitchPoint to) const
{
  const float direction = homeTeam ? 1.0F : -1.0F;
  return pitch.fractionX(to.x - from.x) * direction;
}

float MatchEngine::spaceAtCapybara(bool homeTeam, PitchPoint at) const
{
  const float nearest = nearestOpponentMetres(
      players, homeTeam, at, [](const MatchPlayer& p) { return active(p); });
  return std::clamp(nearest / MatchTuning::Passing::OPENNESS_RADIUS_METRES,
                    0.0F, 1.0F);
}

float MatchEngine::keepChanceCapybara(const MatchPlayer& carrier,
                                      bool shielding, float pressure) const
{
  // The nearest opponent is the one who would challenge.
  const MatchPlayer* tackler = nullptr;
  float nearest = std::numeric_limits<float>::max();
  for (const auto& opponent : players)
  {
    if (opponent.isHomeTeam != carrier.isHomeTeam && active(opponent))
    {
      const float metres = distance(carrier.position, opponent.position);
      if (metres < nearest)
      {
        nearest = metres;
        tackler = &opponent;
      }
    }
  }
  if (tackler == nullptr)
  {
    return 1.0F;
  }
  return 1.0F -
         (pressure * tackleWinChanceCapybara(*tackler, carrier, shielding));
}

MatchEngine::BallAction MatchEngine::chooseBallActionCapybara(
    MatchPlayer& carrier)
{
  // Decision layer: score pass/shot/carry/shield on comparable utility scales,
  // perturb close choices by decision quality, record the explanation, and
  // choose exactly one winner. Utilities are preferences, not probabilities.
  // The body executes the choice; it and subsequent contacts decide the
  // outcome.
  const float pressure =
      std::clamp((MatchTuning::Decision::PRESSURE_RADIUS_METRES -
                  nearestOpponentDistance(carrier)) /
                     MatchTuning::Decision::PRESSURE_RADIUS_METRES,
                 0.0F, 1.0F);
  const float shotXG = gameShotXGCapybara(carrier, carrier.position);
  const float opennessAhead = openSpaceAhead(carrier);

  // Debugger detail: the parts of every score are noted beside the
  // calculations below. Only a recorder that asks for them gets them, and
  // nothing in the decision reads them.
  std::unique_ptr<MatchDecisionDetail> detail;
  if (wantsDetail())
  {
    detail = std::make_unique<MatchDecisionDetail>();
    detail->player = (carrier.player != nullptr) ? carrier.player->getId() : 0;
    detail->homeTeam = carrier.isHomeTeam;
  }
  ScoreBreakdown* const passTerms = detail ? &detail->options[0] : nullptr;
  ScoreBreakdown* const shotTerms = detail ? &detail->options[1] : nullptr;
  ScoreBreakdown* const carryTerms = detail ? &detail->options[2] : nullptr;
  ScoreBreakdown* const shieldTerms = detail ? &detail->options[3] : nullptr;
  // Evaluate the passing candidates first: the task is pure (no random draws)
  // and the deterministic best plus runner-up options are visible in the
  // decision snapshot even when a shot or dribble is eventually chosen.
  const std::optional<PassOption> option =
      choosePassTargetCapybara(carrier, detail ? &detail->candidates : nullptr);

  // The noise is two things (step B7), both from his Decisions (a derived
  // stat: Vision and Passing, with its own spread). Our own estimates.
  // Judgement: he misreads each chance an option uses, by up to this many
  // log-odds either way (DecisionFrame::misread). Drawn once per decision.
  constexpr float JUDGEMENT_LOG_ODDS = 1.0F;
  // Pick randomness: once the options are read and scored he doesn't
  // always take the best. Bigger for a side unfamiliar with its tactics,
  // smaller with the team's edge and a good team talk.
  constexpr float PICK_NOISE = 0.4F;
  const float decisions = carrier.derivedStat(DerivedStat::DECISIONS);
  const float judgement = (1.0F - decisions) * JUDGEMENT_LOG_ODDS;
  // The brain flow tool may ask for steady values (no offsets, no pick
  // noise); the draws are made either way.
  const bool steady = flowRecord != nullptr && flowRecord->steady;
  const auto misreadOffset = [this, judgement, steady]()
  {
    const float draw = randomFloat(-1.0F, 1.0F);
    return steady ? 0.0F : judgement * draw;
  };
  // A chance as he reads it, noted for the brain flow tool.
  const auto readChance = [this](const char* name, float truth, float offset)
  {
    const float read = DecisionFrame::misread(truth, offset);
    if (flowRecord != nullptr)
    {
      flowRecord->chances.push_back(
          {.name = name, .truth = truth, .offset = offset, .read = read});
    }
    return read;
  };
  if (flowRecord != nullptr)
  {
    *flowRecord = {};
    flowRecord->steady = steady;
    flowRecord->layers = flowLayers;
  }
  const bool mood = BrainFlow::isOn(flowLayers, BrainFlow::Layer::MOOD);
  const float familiarityFactor =
      mood ? 1.0F + ((1.0F - familiarityOf(carrier)) *
                     MatchTuning::Decision::FAMILIARITY_NOISE_GAIN)
           : 1.0F;
  const float edgeFactor =
      mood ? std::max(0.5F, 1.0F - teamEdge(carrier.isHomeTeam)) : 1.0F;
  const float talkFactor = mood ? 1.0F - (talkSwing(carrier.isHomeTeam) *
                                          TacticsTuning::TALK_COMPOSURE)
                                : 1.0F;
  const float pickNoiseScale = (1.0F - decisions) * PICK_NOISE *
                               familiarityFactor * edgeFactor * talkFactor;
  if (detail)
  {
    // The scale is a product: each factor is noted as what it adds.
    const float base = (1.0F - decisions) * PICK_NOISE;
    const float familiarity =
        1.0F + ((1.0F - familiarityOf(carrier)) *
                MatchTuning::Decision::FAMILIARITY_NOISE_GAIN);
    const float edge = std::max(0.5F, 1.0F - teamEdge(carrier.isHomeTeam));
    const float talk =
        1.0F - (talkSwing(carrier.isHomeTeam) * TacticsTuning::TALK_COMPOSURE);
    ScoreBreakdown& scale = detail->noiseScale;
    scale.total = pickNoiseScale;
    scale.add("Decisions", TermSource::ATTRIBUTE, base);
    scale.add("tactical familiarity", TermSource::MORALE,
              base * (familiarity - 1.0F));
    scale.add("team edge (home, numbers, talk)", TermSource::SITUATION,
              base * familiarity * (edge - 1.0F));
    scale.add("team talk composure", TermSource::MORALE,
              base * familiarity * edge * (talk - 1.0F));
  }

  // The decision frame (DecisionFrame; brain redesign plan section 15, step
  // A): Goal -> Options -> Scoring -> Weighting -> Decision.
  // Goal: the one default, keep the ball and make progress towards a goal.
  // No goal layer exists yet, so it weights nothing.
  // Weighting: how much each measure counts for him.
  const CarrierWeightsCapybara weights = carrierWeightsCapybara(carrier);
  using DecisionFrame::gate;
  using S = TermSource;
  const bool cutsInside = BrainFlow::isOn(flowLayers, BrainFlow::Layer::ROLE) &&
                          cutsInsideCapybara(carrier);
  noteFlowWeightsCapybara(carrier, weights);

  // Options and Scoring: each option open to him, scored as measures times
  // their weights.
  // Pass: the best candidate (choosePassTargetCapybara, on the true
  // chances), scored again on the chances as he reads them.
  DecisionFrame::Score pass;
  // The debugger shows the best pass's terms as he reads it.
  std::optional<PassCandidateDetail> readPassDetail;
  if (detail)
  {
    readPassDetail.emplace();
  }
  if (option && option->receiver != nullptr)
  {
    const float completionOffset = misreadOffset();
    const float receiverXGOffset = misreadOffset();
    const PassOption read = evaluatePassOptionCapybara(
        carrier, *option->receiver, readPassDetail ? &*readPassDetail : nullptr,
        {.completion = completionOffset, .receiverXG = receiverXGOffset},
        flowRecord != nullptr ? &flowRecord->bestPass : nullptr);
    if (flowRecord != nullptr)
    {
      readChance("pass completion", read.completionProbability,
                 completionOffset);
      readChance(
          "receiver's xG",
          gameShotXGCapybara(*option->receiver, option->receiver->position),
          receiverXGOffset);
      flowRecord->bestReceiver = option->receiver->player != nullptr
                                     ? option->receiver->player->getId()
                                     : 0;
      flowRecord->receiverRoleSteps =
          BrainFlow::isOn(flowLayers, BrainFlow::Layer::ROLE)
              ? roleStepsCapybara(roleProfileOf(*option->receiver).targetBias)
              : 0.0F;
    }
    pass.add("the best pass (as he reads it)", S::SITUATION, read.utility,
             1.0F);
  }
  else if (option)
  {
    pass.add("the best pass", S::SITUATION, option->utility, 1.0F);
  }
  else
  {
    pass.ruleOut();
  }

  // Shot: open from a credible range (xG at least MIN_SHOT_XG). Its core
  // (addShotCoreCapybara) is what the xG map draws; the role terms fade
  // with the same range.
  DecisionFrame::Score shot;
  if (shotXG >= MatchTuning::Decision::MIN_SHOT_XG)
  {
    const float readXG = readChance("shot xG", shotXG, misreadOffset());
    const float range = addShotCoreCapybara(
        shot, weights, pitch, carrier.position, carrier.isHomeTeam, readXG);
    // The keeper away from his usual place, or rushing him (game xG plan:
    // the keeper map assumes him set, so a flat bonus covers the rest).
    shot.add("goal more open than usual", S::SITUATION,
             gate(goalOpenCapybara(carrier)), weights.goalOpen);
    // A wide forward who has cut inside is looking for his shot; roles
    // shoot more or less readily; a man forced onto his weaker foot under
    // pressure is less inclined to try.
    shot.add("cutting inside", S::SLOT_ROLE, gate(cutsInside) * range,
             weights.cutInsideShot);
    shot.add("forced onto weaker foot", S::INSTRUCTION,
             BrainFlow::isOn(flowLayers, BrainFlow::Layer::OPPOSITION_ORDERS)
                 ? weakFootPressure(carrier)
                 : 0.0F,
             weights.weakFootShot);
  }
  else
  {
    shot.ruleOut();
  }
  // The shot option carries its aim (today's rules, rolled now): if the
  // shot is chosen, the strike uses it.
  std::optional<ShotAim> shotAim;
  if (shot.isAvailable())
  {
    shotAim = rollShotAimCapybara(carrier, false, false, false);
  }

  // Carry: always open. The longer he has had the ball, the keener he is to
  // release it: individual possessions last about a second, rarely more
  // than three.
  const auto heldSeconds = static_cast<float>(
      getSimulatedSeconds() - getPossessionHistory().possessionStartSeconds);
  // The best carry candidate, a direction and a pace (step C,
  // bestCarryCapybara, on the true chances), scored again on the chances
  // as he reads them. Its measures are taken at its 1 s point, where he
  // will be at his next decision, as a pass's are taken at the receiver.
  DecisionFrame::Score carry;
  const CarryCandidateCapybara bestCarry = bestCarryCapybara(carrier, weights);
  const PitchPoint carryEnd = bestCarry.end;
  // Braced, so the two misreadings are drawn in this order.
  addCarryTerms(
      carry, weights,
      {.space = spaceAtCapybara(carrier.isHomeTeam, carryEnd),
       .keep = readChance("carry keep", bestCarry.keep, misreadOffset()),
       .gain = progressCapybara(carrier.isHomeTeam, carrier.position, carryEnd),
       .endXG = readChance("carry end xG", bestCarry.endXG, misreadOffset())});
  carry.add("time already on the ball", S::SITUATION, heldSeconds,
            weights.carryHeld);

  // Shield and clear (step E): open only when he is pressed and has no
  // pass worth playing; near his own goal it is a clearance, else a
  // shield. Follow-up: the clear option has no measures of its own yet; it
  // is scored with the shield's keep (what decided a clearance before), so
  // the same situations clear as before.
  DecisionFrame::Score shield;
  DecisionFrame::Score clear;
  const bool passUnavailableOrWeak =
      !option ||
      pass.total() <= MatchTuning::Passing::MIN_ACCEPTABLE_OPTION_SCORE;
  const bool nearOwnGoal =
      pitch.fromOwnGoalLine(carrier.position, carrier.isHomeTeam) <
      MatchTuning::Defending::CLEARANCE_WITHIN_METRES;
  std::optional<ClearanceAim> clearAim;
  const bool keepOpen =
      pressure >= MatchTuning::Decision::SHIELD_PRESSURE_THRESHOLD &&
      passUnavailableOrWeak;
  if (keepOpen)
  {
    const float keep =
        readChance("shield keep", keepChanceCapybara(carrier, true, pressure),
                   misreadOffset());
    if (nearOwnGoal)
    {
      clear.add("keeps the ball (stopgap: the shield's score)", S::SITUATION,
                keep, weights.shieldKeep);
      // The clear option carries its aim (today's rules, rolled now): if it
      // is chosen, the strike uses it.
      clearAim = rollClearanceAimCapybara(carrier);
    }
    else
    {
      shield.add("keeps the ball", S::SITUATION, keep, weights.shieldKeep);
    }
  }
  if (!keepOpen || nearOwnGoal)
  {
    shield.ruleOut();
  }
  if (!keepOpen || !nearOwnGoal)
  {
    clear.ruleOut();
  }

  float passScore = pass.total();
  float shotScore = shot.total();
  float carryScore = carry.total();
  // At most one of shield and clear is open: the pick compares the open
  // one where the shield used to be.
  const bool clearing = clear.isAvailable();
  float shieldScore = clearing ? clear.total() : shield.total();
  if (detail)
  {
    // The pass: the best candidate's own terms as he reads them, then the
    // carrier's.
    if (option && (option->receiver != nullptr) && readPassDetail)
    {
      passTerms->terms = readPassDetail->utility.terms;
      for (const DecisionFrame::Term& term : pass.allTerms().subspan(1))
      {
        passTerms->add(term.name, term.layer, term.value());
      }
    }
    shot.fill(*shotTerms);
    carry.fill(*carryTerms);
    // The debugger has four slots: the clear uses the shield's.
    (clearing ? clear : shield).fill(*shieldTerms);
  }

  // Kept for the recorder: which option the scores alone would choose.
  const std::array<float, 4> scoresBeforeNoise{passScore, shotScore, carryScore,
                                               shieldScore};

  // Vision scales how much randomness perturbs close choices. The noise is
  // bounded so a truly nonsensical option can never win.
  const auto pickNoise = [this, pickNoiseScale, steady]()
  {
    const float draw = randomFloat(-1.0F, 1.0F);
    return steady ? 0.0F : pickNoiseScale * draw;
  };
  shotScore += pickNoise();
  carryScore += pickNoise();
  if (shieldScore > -std::numeric_limits<float>::infinity())
  {
    shieldScore += pickNoise();
  }
  if (option)
  {
    passScore += pickNoise();
  }

  if (flowRecord != nullptr)
  {
    using BrainFlow::OptionIndex;
    const auto at = [](OptionIndex index)
    { return static_cast<std::size_t>(index); };
    BrainFlow::CarrierRecord& record = *flowRecord;
    record.options[at(OptionIndex::PASS)].score = pass;
    record.options[at(OptionIndex::SHOT)].score = shot;
    record.options[at(OptionIndex::CARRY)].score = carry;
    record.options[at(OptionIndex::SHIELD)].score = shield;
    record.options[at(OptionIndex::CLEAR)].score = clear;
    record.options[at(OptionIndex::PASS)].ruledOut =
        option ? nullptr : "no pass candidate in range";
    record.options[at(OptionIndex::SHOT)].ruledOut =
        shotXG >= MatchTuning::Decision::MIN_SHOT_XG
            ? nullptr
            : "xG below the least a shot needs";
    for (const OptionIndex keepOption :
         {OptionIndex::SHIELD, OptionIndex::CLEAR})
    {
      const char*& ruledOut = record.options.at(at(keepOption)).ruledOut;
      if (pressure < MatchTuning::Decision::SHIELD_PRESSURE_THRESHOLD)
      {
        ruledOut = "not pressed closely enough";
      }
      else if (!passUnavailableOrWeak)
      {
        ruledOut = "a pass worth playing";
      }
      else if (keepOption == OptionIndex::SHIELD && nearOwnGoal)
      {
        ruledOut = "near his own goal: he clears it";
      }
      else if (keepOption == OptionIndex::CLEAR && !nearOwnGoal)
      {
        ruledOut = "not near his own goal";
      }
    }
    const std::array<float, 4> withNoise{passScore, shotScore, carryScore,
                                         shieldScore};
    for (std::size_t index = 0; index < withNoise.size(); ++index)
    {
      // The fourth score is the shield's or the clear's, whichever is open.
      const std::size_t slot = index == at(OptionIndex::SHIELD) && clearing
                                   ? at(OptionIndex::CLEAR)
                                   : index;
      record.options.at(slot).noise =
          std::isfinite(scoresBeforeNoise.at(index))
              ? withNoise.at(index) - scoresBeforeNoise.at(index)
              : 0.0F;
    }
    if (clearAim)
    {
      record.clearAimed = true;
      record.clearTarget = clearAim->target;
    }
    record.judgement = judgement;
    record.pickNoise = pickNoiseScale;
    record.decisions = decisions;
    record.familiarityFactor = familiarityFactor;
    record.edgeFactor = edgeFactor;
    record.talkFactor = talkFactor;
    record.carryEnd = carryEnd;
    if (shotAim)
    {
      record.shotAimed = true;
      record.shotAimY = shotAim->aimY;
      record.shotAimZ = shotAim->aimZ;
    }
    // The same comparisons as the branches below, in the same order.
    if (shotScore >= passScore && shotScore >= carryScore &&
        shotScore >= shieldScore)
    {
      record.chosen = at(OptionIndex::SHOT);
    }
    else if (carryScore >= passScore && carryScore >= shieldScore)
    {
      record.chosen = at(OptionIndex::CARRY);
    }
    else if (shieldScore >= passScore)
    {
      record.chosen =
          clearing ? at(OptionIndex::CLEAR) : at(OptionIndex::SHIELD);
    }
    else if (option)
    {
      record.chosen = at(OptionIndex::PASS);
    }
    record.filled = true;
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
      {
        return ScenarioAction::SHOT;
      }
      if (carry >= pass && carry >= shield)
      {
        return ScenarioAction::CARRY;
      }
      if (shield >= pass)
      {
        return fromOwnGoal < MatchTuning::Defending::CLEARANCE_WITHIN_METRES
                   ? ScenarioAction::CLEAR
                   : ScenarioAction::SHIELD;
      }
      return ScenarioAction::PASS;
    };
    MatchDecisionRecord decision;
    decision.player = (carrier.player != nullptr) ? carrier.player->getId() : 0;
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
    decision.noiseScale = pickNoiseScale;
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
                                   : 0.0F;
        // Terms that are exactly zero say nothing here.
        std::erase_if(detail->options[index].terms,
                      [](const ScoreTerm& term) { return term.value == 0.0F; });
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
    if (shotAim && carrier.player != nullptr)
    {
      heldShotAimCapybara = HeldShotAimCapybara{
          .player = carrier.player->getId(),
          .possessionStart = getPossessionHistory().possessionStartSeconds,
          .aim = *shotAim};
    }
    return action;
  }
  if (carryScore >= passScore && carryScore >= shieldScore)
  {
    lastScenarioDecision.action = ScenarioAction::CARRY;
    lastScenarioDecision.reason =
        option
            ? "carry (score " + std::to_string(carryScore) +
                  ") beats the best pass (score " + std::to_string(passScore) +
                  ") with intent " + std::string(passIntentName(option->intent))
            : std::string("carry: no acceptable pass candidate in range");
    action.kind = ScenarioAction::CARRY;
    action.holdSeconds = randomFloat(MatchTuning::Decision::MIN_DRIBBLE_TIME,
                                     MatchTuning::Decision::MAX_DRIBBLE_TIME);
    // The chosen direction and pace hold until he decides again: the
    // planner and the dribble pace read them (step C).
    if (carrier.player != nullptr)
    {
      heldCarryCapybara = HeldCarryCapybara{
          .player = carrier.player->getId(),
          .possessionStart = getPossessionHistory().possessionStartSeconds,
          .direction = bestCarry.direction,
          .urgency = bestCarry.urgency};
    }
    return action;
  }
  if (shieldScore >= passScore && clearing)
  {
    lastScenarioDecision.action = ScenarioAction::CLEAR;
    lastScenarioDecision.reason =
        "clear:pressed in the own third with no safe outlet";
    action.kind = ScenarioAction::CLEAR;
    if (clearAim && carrier.player != nullptr)
    {
      heldClearAimCapybara = HeldClearAimCapybara{
          .player = carrier.player->getId(),
          .possessionStart = getPossessionHistory().possessionStartSeconds,
          .aim = *clearAim};
    }
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

  if (option.has_value())
  {
    lastScenarioDecision.action = ScenarioAction::PASS;
    lastScenarioDecision.reason =
        std::string("pass:") + std::string(passIntentName(option->intent)) +
        " to player " +
        std::to_string((option->receiver != nullptr) &&
                               (option->receiver->player != nullptr)
                           ? option->receiver->player->getId()
                           : 0) +
        " (utility " + std::to_string(option->utility) + ")";
    action.kind = ScenarioAction::PASS;
    action.pass = option;
  }
  return action;
}

float MatchEngine::passingLaneRiskCapybara(const MatchPlayer& passer,
                                           const MatchPlayer& receiver) const
{
  // Each opponent ahead of the passer along the straight lane threatens it
  // by how close he stands to the line, against how far he could reach by
  // the time the ball gets there. A man already in the lane next to the
  // passer is the surest block of all: the ball runs into him.
  using P = MatchTuning::Passing;
  const Vector2F segment = receiver.position - passer.position;
  const float segmentLengthSquared =
      (segment.x * segment.x) + (segment.y * segment.y);
  if (segmentLengthSquared <= EPSILON)
  {
    return 1.0F;
  }
  const float segmentLength = std::sqrt(segmentLengthSquared);

  float combinedSafety = 1.0F;
  for (const auto& opponent : players)
  {
    if (!active(opponent) || opponent.isHomeTeam == passer.isHomeTeam)
    {
      continue;
    }
    const Vector2F fromPasser = opponent.position - passer.position;
    // Level with or behind the passer he cannot get in front of the ball;
    // at the far end he is contesting the reception, not the lane.
    const float projection =
        (fromPasser.x * segment.x + fromPasser.y * segment.y) /
        segmentLengthSquared;
    if (projection <= 0.0F || projection > P::LANE_END_MARGIN)
    {
      continue;
    }

    const float laneDistance =
        length({.x = fromPasser.x - (segment.x * projection),
                .y = fromPasser.y - (segment.y * projection)});
    // How far along the ball has to travel to reach him decides how much
    // time he has: none close to the passer, the full radius past the ramp.
    const float ramp =
        std::min(1.0F, projection * segmentLength / P::LANE_REACH_RAMP_METRES);
    const float fullRadius =
        P::BASE_INTERCEPTION_RADIUS_METRES +
        (opponent.defending * P::DEFENDING_INTERCEPTION_METRES) +
        (opponent.pace * P::PACE_INTERCEPTION_METRES);
    const float interceptionRadius =
        P::NEAR_LANE_REACH_METRES +
        ((fullRadius - P::NEAR_LANE_REACH_METRES) * ramp);
    const float individualRisk = std::clamp(
        1.0F - (laneDistance / std::max(interceptionRadius, EPSILON)), 0.0F,
        1.0F);
    const float laneWeight =
        P::NEAR_LANE_RISK +
        ((P::BASE_INTERCEPTION_RISK + projection * P::LATE_LANE_RISK -
          P::NEAR_LANE_RISK) *
         ramp);
    combinedSafety *= 1.0F - individualRisk * laneWeight;
  }
  return std::clamp(1.0F - combinedSafety, 0.0F, 1.0F);
}

MatchEngine::PassOption MatchEngine::evaluatePassOptionCapybara(
    MatchPlayer& passer, MatchPlayer& receiver, PassCandidateDetail* detail,
    PassMisreadCapybara misread, DecisionFrame::Score* terms) const
{
  // Estimate one receiver's safety, progress and space without consuming RNG
  // or changing the match. The completion estimate ranks options; passBall
  // does not roll against it to guarantee a completed pass. Error at release,
  // ball flight and contested receptions determine whether it arrives.
  const float passDistance = distance(passer.position, receiver.position);
  const float progression =
      progressCapybara(passer.isHomeTeam, passer.position, receiver.position);
  const float openness =
      spaceAtCapybara(receiver.isHomeTeam, receiver.position);
  const float laneRisk = passingLaneRiskCapybara(passer, receiver);
  const float pressure =
      std::clamp((MatchTuning::Decision::PRESSURE_RADIUS_METRES -
                  nearestOpponentDistance(passer)) /
                     MatchTuning::Decision::PRESSURE_RADIUS_METRES,
                 0.0F, 1.0F);
  const float passerToGoal =
      pitch.toGoalLine(passer.position, passer.isHomeTeam);
  const float receiverToGoal =
      pitch.toGoalLine(receiver.position, receiver.isHomeTeam);
  const bool widePasser = pitch.fractionY(passer.position) <=
                              MatchTuning::Passing::WIDE_ATTACK_MINIMUM_Y ||
                          pitch.fractionY(passer.position) >=
                              MatchTuning::Passing::WIDE_ATTACK_MAXIMUM_Y;
  const bool centralReceiver =
      pitch.fractionY(receiver.position) >=
          MatchTuning::Passing::CENTRAL_TARGET_MINIMUM_Y &&
      pitch.fractionY(receiver.position) <=
          MatchTuning::Passing::CENTRAL_TARGET_MAXIMUM_Y;
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
  // A high defensive line leaves space in behind: a forward already running
  // into it is worth finding, and the ball is easier to play there.
  float spaceBehindShare = 0.0F;
  // Any player running in behind is worth finding, a midfielder arriving
  // late as much as a striker.
  if (receiver.isMakingRun && progression > 0.0F)
  {
    const float line = offsideLine(passer.isHomeTeam);
    const float spaceBehind = passer.isHomeTeam ? pitch.atX(1.0F) - line : line;
    spaceBehindShare = std::clamp(
        (spaceBehind - MatchTuning::Passing::SPACE_BEHIND_MIN_METRES) /
            MatchTuning::Passing::SPACE_BEHIND_RANGE_METRES,
        0.0F, 1.0F);
  }
  const float completionProbability = std::clamp(
      MatchTuning::Passing::BASE_COMPLETION_PROBABILITY +
          (passer.passing * MatchTuning::Passing::PASSING_COMPLETION_BONUS) +
          (openness * MatchTuning::Passing::OPENNESS_COMPLETION_BONUS) -
          (laneRisk * MatchTuning::Passing::LANE_COMPLETION_PENALTY) -
          // Completion falls steeply for long passes (~86% at 10-20 m,
          // ~41% beyond 40 m).
          (passDistance * passDistance /
           (MatchTuning::Passing::LONG_PASS_REFERENCE_METRES *
            MatchTuning::Passing::LONG_PASS_REFERENCE_METRES) *
           MatchTuning::Passing::DISTANCE_COMPLETION_PENALTY) -
          (pressure * MatchTuning::Passing::PRESSURE_COMPLETION_PENALTY) -
          (crossOption ? MatchTuning::Passing::CROSS_COMPLETION_PENALTY
                       : 0.0F) +
          (cutbackOption ? MatchTuning::Passing::CUTBACK_COMPLETION_BONUS
                         : 0.0F) +
          (spaceBehindShare *
           MatchTuning::Passing::SPACE_BEHIND_COMPLETION_BONUS),
      MatchTuning::Passing::MIN_COMPLETION_PROBABILITY,
      MatchTuning::Passing::MAX_COMPLETION_PROBABILITY);
  // The decision frame (DecisionFrame; brain redesign plan section 15, step
  // A). Weighting: how much each measure counts for the passer, then the
  // weights the receiver brings (his role, an opposition order on him).
  const CarrierWeightsCapybara weights = carrierWeightsCapybara(passer);
  // The receiver's role: a target man is looked for, so passes to him get
  // his steps on progress too.
  const float progressWeight = DecisionFrame::stepped(
      neutralCarrierWeightsCapybara().progress,
      weights.progressSteps +
          (BrainFlow::isOn(flowLayers, BrainFlow::Layer::ROLE)
               ? roleStepsCapybara(roleProfileOf(receiver).targetBias)
               : 0.0F));
  const float markedWeight =
      BrainFlow::isOn(flowLayers, BrainFlow::Layer::OPPOSITION_ORDERS)
          ? -tightMarkPenaltyCapybara(receiver)
          : 0.0F;
  // Scoring: the measures, each times its weight, summed in this order.
  using DecisionFrame::gate;
  using S = TermSource;
  DecisionFrame::Score score;
  // The chances as he reads them (judgement; unchanged in the pass search).
  const float readCompletion =
      DecisionFrame::misread(completionProbability, misread.completion);
  const float readReceiverXG = DecisionFrame::misread(
      gameShotXGCapybara(receiver, receiver.position), misread.receiverXG);
  // Progress: the chance it arrives times the gain, as for the carry.
  const float passProgress = readCompletion * progression;
  score.add("space where the ball ends", S::SITUATION, openness, weights.space);
  score.add("progress (completion x gain)", S::SITUATION, passProgress,
            progressWeight);
  score.add(
      "distance from the ideal length", S::SITUATION,
      std::abs(passDistance - MatchTuning::Passing::IDEAL_DISTANCE_METRES),
      weights.lengthOffIdeal);
  // Pressed, a ball back to a team-mate is worth more.
  score.add("safe outlet under pressure", S::SITUATION,
            pressure * std::max(0.0F, -progression), weights.safeOutlet);
  score.add("completion chance", S::SITUATION, readCompletion,
            weights.completion);
  score.add("receiver making a run", S::SITUATION,
            gate(receiver.isMakingRun && progression > 0.0F), weights.runner);
  score.add("receiver tightly marked (order)", S::INSTRUCTION, 1.0F,
            markedWeight);
  // Threat: the chance the pass arrives times the receiver's xG there.
  score.add("threat (completion x receiver's xG)", S::SITUATION,
            readCompletion * readReceiverXG, weights.threat);
  score.add("space in behind", S::SITUATION, spaceBehindShare,
            weights.spaceBehind);
  const float utility = score.total();
  if (terms != nullptr)
  {
    *terms = score;
  }

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
  const Vector2F lead = {.x = receiver.velocity.x * flightEstimate *
                              MatchTuning::Passing::RECEIVER_LEAD_SCALE,
                         .y = receiver.velocity.y * flightEstimate *
                              MatchTuning::Passing::RECEIVER_LEAD_SCALE};
  PitchPoint target{
      std::clamp(receiver.position.x + lead.x, 0.0F, pitch.atX(1.0F)),
      std::clamp(receiver.position.y + lead.y, 0.0F, pitch.atY(1.0F))};
  if (intent == PassIntent::THROUGH_BALL)
  {
    target.x += (receiver.tacticalTarget.x - target.x) *
                MatchTuning::Passing::THROUGH_BALL_TARGET_BLEND;
    target.y += (receiver.tacticalTarget.y - target.y) *
                MatchTuning::Passing::THROUGH_BALL_TARGET_BLEND;
    const float direction = passer.isHomeTeam ? 1.0F : -1.0F;
    target.x +=
        direction * MatchTuning::Passing::THROUGH_BALL_FORWARD_LEAD_METRES;
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

  if (detail != nullptr)
  {
    // Debugger detail: the completion estimate's terms, by source.
    using P = MatchTuning::Passing;
    detail->receiver =
        (receiver.player != nullptr) ? receiver.player->getId() : 0;
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
                   crossOption ? -P::CROSS_COMPLETION_PENALTY : 0.0F);
    completion.add("cutback", S::LOCATION,
                   cutbackOption ? P::CUTBACK_COMPLETION_BONUS : 0.0F);
    completion.add("space in behind", S::SITUATION,
                   spaceBehindShare * P::SPACE_BEHIND_COMPLETION_BONUS);
    // The estimate is clamped to its range.
    completion.add("clamped to its range", S::SITUATION, completion.residual());

    // The utility: the frame's own terms (exact, not reported beside it).
    score.fill(detail->utility);
    // Terms that are exactly zero say nothing here.
    std::erase_if(detail->utility.terms,
                  [](const ScoreTerm& term) { return term.value == 0.0F; });
    std::erase_if(completion.terms,
                  [](const ScoreTerm& term) { return term.value == 0.0F; });
  }

  return {
      .receiver = &receiver,
      .targetPoint = target,
      .intent = intent,
      .utility = utility,
      .progression = progression,
      .laneRisk = laneRisk,
      .completionProbability = completionProbability,
      .passDistance = travelDistance,
      .lofted = !receiver.isGoalkeeper &&
                (intent == PassIntent::CROSS ||
                 travelDistance > MatchTuning::Passing::LOFTED_DISTANCE_METRES),
      .spin = {},
      .arrivalSpeed = {},
      .technique = {}};
}

std::optional<MatchEngine::PassOption> MatchEngine::choosePassTargetCapybara(
    MatchPlayer& passer, std::vector<PassCandidateDetail>* details)
{
  // Debugger detail: a team-mate left out, and why.
  const auto excluded = [details](const MatchPlayer& candidate, float metres,
                                  PassExclusion reason)
  {
    if (!details)
    {
      return;
    }
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
    return PassDecision{.passerId = 0,
                        .receiverId = option.receiver && option.receiver->player
                                          ? option.receiver->player->getId()
                                          : 0,
                        .targetPoint = option.targetPoint,
                        .intent = option.intent,
                        .utility = option.utility,
                        .progression = option.progression,
                        .laneRisk = option.laneRisk,
                        .completionProbability = option.completionProbability};
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
        (1.0F - passer.vision) *
        MatchTuning::Passing::OFFSIDE_PERCEPTION_ERROR_METRES;
    if (isOffside(candidate, passer.isHomeTeam, defenderLine,
                  perceivedLineError))
    {
      excluded(candidate, passDistance, PassExclusion::LOOKS_OFFSIDE);
      continue;
    }

    PassCandidateDetail* detail = nullptr;
    if (details != nullptr)
    {
      details->emplace_back();
      detail = &details->back();
    }
    PassOption option = evaluatePassOptionCapybara(passer, candidate, detail);
    // A training dummy in the way (training ground plan section 21): left
    // out.
    if (passBlockedByDummy(passer, option))
    {
      if (detail != nullptr)
      {
        detail->excluded = PassExclusion::BLOCKED_BY_DUMMY;
      }
      ++passesBlockedByDummies;
      continue;
    }
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
  if (best && (best->receiver != nullptr) &&
      (best->receiver->player != nullptr))
  {
    lastScenarioDecision.best = toDecision(*best);
  }
  if (runnerUp && (runnerUp->receiver != nullptr) &&
      (runnerUp->receiver->player != nullptr))
  {
    lastScenarioDecision.runnerUp = toDecision(*runnerUp);
  }

  if (bestScore < MatchTuning::Passing::MIN_ACCEPTABLE_OPTION_SCORE)
  {
    return std::nullopt;
  }
  return best;
}

void MatchEngine::afterPassReleasedCapybara(const MatchPlayer& passer,
                                            const MatchPlayer& receiver)
{
  // Keep externally visible AI state consistent with the newly released
  // pass. Without this, the final fixed step of a rendered frame could expose
  // the pre-pass run assignments until the next simulation update.
  for (auto& teammate : players)
  {
    if (teammate.isHomeTeam != passer.isHomeTeam || !active(teammate))
    {
      continue;
    }
    teammate.isMakingRun = false;
    if (&teammate == &receiver)
    {
      teammate.intent = PlayerIntent::RECEIVE_PASS;
    }
    else if (!teammate.isGoalkeeper)
    {
      teammate.intent = PlayerIntent::OFFER_SUPPORT;
    }
  }
}

MatchEngine::ShotAim MatchEngine::chooseShotAimCapybara(
    const MatchPlayer& shooter, bool header, bool penalty, bool freeKick)
{
  // An open-play shot: the aim his shot option carried, once.
  if (!header && !penalty && !freeKick && heldShotAimCapybara &&
      shooter.player != nullptr &&
      heldShotAimCapybara->player == shooter.player->getId() &&
      heldShotAimCapybara->possessionStart ==
          getPossessionHistory().possessionStartSeconds)
  {
    const ShotAim aim = heldShotAimCapybara->aim;
    heldShotAimCapybara.reset();
    return aim;
  }
  // A direct free kick or a penalty: the aim its restart option carried,
  // once.
  if ((freeKick || penalty) && !header && heldRestartAimCapybara &&
      shooter.player != nullptr &&
      heldRestartAimCapybara->player == shooter.player->getId() &&
      heldRestartAimCapybara->penalty == penalty)
  {
    const ShotAim aim = heldRestartAimCapybara->aim;
    heldRestartAimCapybara.reset();
    return aim;
  }
  return rollShotAimCapybara(shooter, header, penalty, freeKick);
}

MatchEngine::ShotAim MatchEngine::rollShotAimCapybara(
    const MatchPlayer& shooter, bool header, bool penalty, bool freeKick)
{
  // Aim for the side away from the keeper, a little inside the post; better
  // finishers pick tighter spots.
  const float HALF_GOAL = pitch.goal.halfWidth;
  const MatchPlayer* keeper = findGoalkeeper(!shooter.isHomeTeam);
  const float keeperOffset =
      (keeper != nullptr)
          ? keeper->position.y - pitch.atY(MatchTuning::Pitch::CENTRE)
          : 0.0F;
  float side = keeperOffset > 0.0F ? -1.0F : 1.0F;
  if (std::abs(keeperOffset) < MatchTuning::Shooting::KEEPER_CENTRED_METRES ||
      randomFloat(0.0F, 1.0F) < MatchTuning::Shooting::NEAR_SIDE_AIM_CHANCE)
  {
    side = randomFloat(0.0F, 1.0F) < 0.5F ? -1.0F : 1.0F;
  }
  // From the spot a taker can pick a corner; in open play the spot is
  // chosen on the move and lands further inside the post.
  // A free kick is curled over the wall into a corner.
  ShotAim aim;
  aim.inset =
      MatchTuning::Shooting::AIM_POST_INSET_BASE +
      (1.0F - shooter.shooting) * MatchTuning::Shooting::AIM_POST_INSET_SKILL +
      randomFloat(0.0F,
                  penalty    ? MatchTuning::Shooting::PENALTY_AIM_INSET_SPREAD
                  : freeKick ? MatchTuning::SetPiece::FREE_KICK_AIM_INSET_SPREAD
                             : MatchTuning::Shooting::AIM_INSET_SPREAD);
  aim.aimY = side * std::max(0.0F, HALF_GOAL - aim.inset);
  if (!penalty && !freeKick && !header &&
      randomFloat(0.0F, 1.0F) <
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

void MatchEngine::decidePenaltyCapybara(const MatchPlayer& taker)
{
  // Decided before the run-up, so stutters, swerves and waiting for the
  // keeper to commit can come later. For now one option, the shot, with
  // today's penalty aim.
  heldRestartAimCapybara.reset();
  if (taker.player != nullptr)
  {
    heldRestartAimCapybara = HeldRestartAimCapybara{
        .player = taker.player->getId(),
        .aim = rollShotAimCapybara(taker, false, true, false),
        .penalty = true};
  }
}

float MatchEngine::choosePenaltyGuessCapybara(const MatchPlayer& keeper)
{
  // The situation "I'm facing a penalty" (brain redesign plan section 26,
  // step L), decided before the kick (goalkeeper plan step 9). Options:
  // dive left, dive right (each carrying its height, high or low), stay.
  // Open on today's blind rolls: a side at 50/50, then the stay chance;
  // the height is rolled with a dive and held to the dive plan (a Binturong
  // body's dive; other bodies have no height). Follow-up: no measures
  // (reading the taker, Anticipation, is for later); the stopgap pick is
  // the rolled option.
  float guess = randomFloat(0.0F, 1.0F) < 0.5F ? -1.0F : 1.0F;
  if (randomFloat(0.0F, 1.0F) < MatchTuning::Goalkeeper::PENALTY_STAY_CHANCE)
  {
    guess = 0.0F;
  }
  heldPenaltyDiveCapybara.reset();
  if (guess != 0.0F && keeper.player != nullptr &&
      getBody(keeper.isHomeTeam) == BodyId::BINTURONG)
  {
    using D = MatchTuning::KeeperDive;
    heldPenaltyDiveCapybara = HeldPenaltyDiveCapybara{
        .player = keeper.player->getId(),
        .height = randomFloat(0.0F, 1.0F) < D::PENALTY_HIGH_CHANCE
                      ? D::PENALTY_HIGH_METRES
                      : D::PENALTY_LOW_METRES};
  }
  return guess;
}

float MatchEngine::penaltyDiveHeight(const MatchPlayer& keeper)
{
  using D = MatchTuning::KeeperDive;
  if (heldPenaltyDiveCapybara && keeper.player != nullptr &&
      heldPenaltyDiveCapybara->player == keeper.player->getId())
  {
    const float height = heldPenaltyDiveCapybara->height;
    heldPenaltyDiveCapybara.reset();
    return height;
  }
  return randomFloat(0.0F, 1.0F) < D::PENALTY_HIGH_CHANCE
             ? D::PENALTY_HIGH_METRES
             : D::PENALTY_LOW_METRES;
}

PitchPoint MatchEngine::goalkeeperTargetCapybara(MatchPlayer& keeper,
                                                 const MatchPlayer* carrier)
{
  GoalkeeperControl& control = keepers[keeper.isHomeTeam ? 0 : 1];
  const auto setState = [&control](GoalkeeperState next)
  {
    if (control.state != next)
    {
      control.state = next;
      control.timer = 0.0F;
    }
  };
  control.timer += stepSeconds();
  const float goalX = keeper.isHomeTeam ? 0.0F : 1.0F;
  const float outward = keeper.isHomeTeam ? 1.0F : -1.0F;
  const PitchPoint goalCentre = pitch.at(goalX, MatchTuning::Pitch::CENTRE);

  if (getPossessionAttachment() == keeper.player)
  {
    setState(GoalkeeperState::HOLD);
    return keeper.position;
  }
  if (keeper.diveTimer > 0.0F)
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
        meet && ((getPossessionAttachment() == nullptr) || hasTouchClaim()) &&
        !getPossessionState().isShot)
    {
      setState(GoalkeeperState::CLAIM);
      return clampToPitch(*meet);
    }
  }
  // Claim a high delivery dropping into the own box.
  else if ((getPossessionAttachment() == nullptr) &&
           getPossessionState().isAerialDelivery &&
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
    const Vector2F drift = {.x = ball.velocity.x * lookahead,
                            .y = ball.velocity.y * lookahead};
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
  if ((carrier == nullptr) && !getPossessionState().isShot &&
      pitch.fromOwnGoalLine(ball.position, keeper.isHomeTeam) <
          MatchTuning::Pitch::GOALKEEPER_SWEEP_METRES *
              (1.0F + keeperSweep * TacticsTuning::KEEPER_SWEEP_DEPTH_GAIN) &&
      !(getPossessionState().isPass &&
        getPossessionState().passByHome == keeper.isHomeTeam))
  {
    const float keeperDistance = distance(keeper.position, ball.position);
    bool keeperFirst = true;
    for (const auto& other : players)
    {
      if (!active(other) || other.isHomeTeam == keeper.isHomeTeam)
      {
        continue;
      }
      if (distance(other.position, ball.position) <
          keeperDistance * MatchTuning::Goalkeeper::SWEEP_ADVANTAGE *
              (1.0F - keeperSweep * TacticsTuning::KEEPER_SWEEP_ADVANTAGE_GAIN))
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
      const Vector2F lead = {.x = ball.velocity.x * lookahead,
                             .y = ball.velocity.y * lookahead};
      return PitchPoint{ball.position.x + lead.x, ball.position.y + lead.y};
    }
  }

  // Rush a carrier who is through on goal inside the box.
  if ((carrier != nullptr) && carrier->isHomeTeam != keeper.isHomeTeam &&
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
      if ((carrier->position.x - other.position.x) * outward > 0.0F &&
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
      return PitchPoint{
          goalCentre.x + ((carrier->position.x - goalCentre.x) *
                          MatchTuning::Goalkeeper::RUSH_CLOSING_SHARE),
          goalCentre.y + ((carrier->position.y - goalCentre.y) *
                          MatchTuning::Goalkeeper::RUSH_CLOSING_SHARE)};
    }
  }

  setState(GoalkeeperState::SET_POSITION);
  return keeperSetPositionCapybara(
      keeper, (carrier != nullptr) ? carrier->position : ball.position);
}

PitchPoint MatchEngine::keeperSetPositionCapybara(const MatchPlayer& keeper,
                                                  PitchPoint threat) const
{
  // Set position on the ball-goal line, advancing to narrow the angle as the
  // threat approaches and acting as a sweeper when play is far away.
  const float goalX = keeper.isHomeTeam ? 0.0F : 1.0F;
  const PitchPoint goalCentre = pitch.at(goalX, MatchTuning::Pitch::CENTRE);
  const float threatMetres = distance(threat, goalCentre);
  float depthMetres = MatchTuning::Goalkeeper::NEAR_DEPTH_METRES +
                      (threatMetres * MatchTuning::Goalkeeper::DEPTH_PER_METRE);
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
      depthMetres + (keeperDepth * std::min(1.0F, threatMetres / 40.0F)), 0.5F,
      MatchTuning::Goalkeeper::MAX_DEPTH_METRES + std::max(0.0F, keeperDepth));
  const float dxMetres = threat.x - goalCentre.x;
  const float dyMetres = threat.y - goalCentre.y;
  const float norm = std::max(
      std::sqrt((dxMetres * dxMetres) + (dyMetres * dyMetres)), EPSILON);
  return PitchPoint{
      goalCentre.x + (dxMetres / norm * depthMetres),
      std::clamp(
          goalCentre.y + (dyMetres / norm * depthMetres),
          goalCentre.y - MatchTuning::Pitch::GOALKEEPER_SIDE_LIMIT_METRES,
          goalCentre.y + MatchTuning::Pitch::GOALKEEPER_SIDE_LIMIT_METRES)};
}

bool MatchEngine::choosesToBlockCapybara(const MatchPlayer& /*player*/)
{
  // The situation "I can intercept the ball", its shot-block branch (step
  // L): an outfielder in a shot's path. Options: throw myself in, let it
  // go. Throwing himself in is always open; whether he gets there is the
  // body's. Follow-up: no measures; the stopgap pick throws himself in.
  return true;
}

bool MatchEngine::choosesToPutBlockBehindCapybara(const MatchPlayer& /*player*/)
{
  // No second kick after a block (shot block plan step 4): the ball goes
  // where it comes off him. A closed option of the shot-block branch (step
  // L), kept for the older brains.
  return false;
}

bool MatchEngine::choosesToClearCrossBehindCapybara(
    const MatchPlayer& /*player*/)
{
  // No instant kick behind on meeting a cross (shot block plan step 4): he
  // plays it with his touch like any other ball, through the receiving and
  // intercept frames (steps F, G, I). A closed option (step L).
  return false;
}

PitchPoint MatchEngine::chooseClearBehindTargetCapybara(
    const MatchPlayer& defender)
{
  // Over the own goal line, wide of the posts on his side.
  // 2.1 m behind the line, 4.08 m wide of the post to 30.6 m off centre.
  constexpr float BEHIND_METRES = 2.1F;
  constexpr float WIDE_OF_POST_METRES = 4.08F;
  constexpr float WIDEST_METRES = 30.6F;
  const float side = defender.position.y < pitch.atY(MatchTuning::Pitch::CENTRE)
                         ? -1.0F
                         : 1.0F;
  return PitchPoint{
      defender.isHomeTeam ? -BEHIND_METRES : pitch.length + BEHIND_METRES,
      pitch.atY(MatchTuning::Pitch::CENTRE) +
          (side * randomFloat(pitch.goal.halfWidth + WIDE_OF_POST_METRES,
                              WIDEST_METRES))};
}

MatchEngine::HeaderPlay MatchEngine::chooseHeaderOutCapybara(
    const MatchPlayer& header) const
{
  // Forced to head it out of play, he looks for the safest way out rather
  // than a point: only the direction of each aim matters, so they lie
  // beyond the lines.
  using A = MatchTuning::Aerial;
  constexpr float BEYOND_SIDE_METRES = 3.4F;
  constexpr float BEYOND_END_METRES = 5.25F;
  const PitchPoint from = ball.position;
  const float lateralMetres = from.y - pitch.atY(MatchTuning::Pitch::CENTRE);
  const float sideY = lateralMetres < 0.0F ? -BEYOND_SIDE_METRES
                                           : pitch.width + BEYOND_SIDE_METRES;
  HeaderPlay play;
  play.kind = HeaderPlay::Kind::BEHIND;
  play.speed = A::HEADER_CLEARANCE_SPEED;
  play.lift = A::HEADER_OUT_LIFT;
  // Wide of the six-yard box: hard and high over the nearest touchline.
  if (std::abs(lateralMetres) > MatchTuning::Pitch::GOAL_AREA_HALF_WIDTH_METRES)
  {
    play.aim = PitchPoint{from.x, sideY};
    return play;
  }
  // In line with it: glanced on towards the corner on his side, or up and
  // over the bar, whichever is the easier header.
  const float goalX = pitch.atX(header.isHomeTeam ? 0.0F : 1.0F);
  const float behindX =
      header.isHomeTeam ? -BEYOND_END_METRES : pitch.length + BEYOND_END_METRES;
  play.aim = PitchPoint{behindX, sideY};
  // Over the bar: as hard, steeply up, and only if that clears it.
  const float pace = std::hypot(play.speed, play.lift);
  const float overSpeed = pace * std::cos(A::HEADER_OVER_ELEVATION);
  const float overLift = pace * std::sin(A::HEADER_OVER_ELEVATION);
  const float lineMetres = std::max(std::abs(from.x - goalX), 0.5F);
  if (loftVerticalSpeed(
          lineMetres, overSpeed, ball.z,
          pitch.goal.crossbar + A::HEADER_OVER_BAR_MARGIN_METRES) > overLift)
  {
    return play;
  }
  const ContactProfile profile = strikeProfileBinturong(StrikeKind::HEADER);
  const auto difficulty = [&](PitchPoint aim, float speed, float lift)
  {
    const ContactAssessment contact = assessStrikeBinturong(
        header, profile, metricDirection(from, aim), speed, lift);
    return contact.angleDifficulty + contact.weightDifficulty;
  };
  const PitchPoint over{behindX, from.y};
  if (difficulty(over, overSpeed, overLift) <
      difficulty(play.aim, play.speed, play.lift))
  {
    play.aim = over;
    play.speed = overSpeed;
    play.lift = overLift;
  }
  return play;
}

MatchEngine::ClearanceAim MatchEngine::chooseClearanceAimCapybara(
    const MatchPlayer& defender)
{
  // His clear option's aim, once (step E); otherwise rolled now (a tackler
  // clearing it, Play mode).
  if (heldClearAimCapybara && defender.player != nullptr &&
      heldClearAimCapybara->player == defender.player->getId() &&
      heldClearAimCapybara->possessionStart ==
          getPossessionHistory().possessionStartSeconds)
  {
    const ClearanceAim aim = heldClearAimCapybara->aim;
    heldClearAimCapybara.reset();
    return aim;
  }
  return rollClearanceAimCapybara(defender);
}

MatchEngine::ClearanceAim MatchEngine::rollClearanceAimCapybara(
    const MatchPlayer& defender)
{
  // A long, high clearance upfield and towards the nearer touchline.
  const float forward = defender.isHomeTeam ? 1.0F : -1.0F;
  const float touchline =
      defender.position.y < pitch.atY(MatchTuning::Pitch::CENTRE) ? -1.0F
                                                                  : 1.0F;
  ClearanceAim aim;
  aim.metres =
      randomFloat(MatchTuning::Defending::CLEARANCE_MIN_DISTANCE_METRES,
                  MatchTuning::Defending::CLEARANCE_MAX_DISTANCE_METRES);
  const Vector2F direction = normalized(
      {.x = forward,
       .y = touchline * randomFloat(0.0F, 1.0F) *
            MatchTuning::Defending::CLEARANCE_TOUCHLINE_BIAS * 2.0F});
  const Vector2F reach = {.x = direction.x * aim.metres,
                          .y = direction.y * aim.metres};
  aim.target =
      PitchPoint{defender.position.x + reach.x, defender.position.y + reach.y};
  return aim;
}

MatchEngine::HeaderAttempt MatchEngine::chooseHeaderAttemptCapybara(
    const MatchPlayer& header, bool fromTeammate)
{
  // Not a team-mate's ball: the situation "I can intercept the ball"
  // decides (chooseHeaderPlayCapybara).
  if (!fromTeammate)
  {
    return HeaderAttempt::PLAY;
  }
  // The situation "I'm expecting the ball", head branch (brain redesign
  // plan section 20, step G): header at goal, cushion it (the control
  // touch), or knock it down to a team-mate (a first-time pass).
  using S = TermSource;
  const float toGoal = pitch.toGoalLine(header.position, header.isHomeTeam);
  // At goal: it needs a real chance, close in and fairly central.
  DecisionFrame::Score atGoal;
  if (toGoal <= MatchTuning::Aerial::HEADER_SHOT_WITHIN_METRES &&
      std::abs(header.position.y - pitch.atY(MatchTuning::Pitch::CENTRE)) <=
          MatchTuning::Aerial::HEADER_SHOT_WIDTH_METRES &&
      estimateShotXG(header) * MatchTuning::Shooting::HEADER_XG_FACTOR >=
          MatchTuning::Aerial::HEADER_SHOT_MIN_XG)
  {
    atGoal.add("stopgap: first in today's order", S::SITUATION, 1.0F,
               HEADER_FIRST);
  }
  else
  {
    atGoal.ruleOut();
  }
  // Cushion it: a lofted ball away from goal, kept.
  DecisionFrame::Score cushion;
  if (toGoal > MatchTuning::Aerial::HEADER_KNOCK_DOWN_WITHIN_METRES)
  {
    cushion.add("stopgap: second in today's order", S::SITUATION, 1.0F,
                HEADER_SECOND);
  }
  else
  {
    cushion.ruleOut();
  }
  // Knock it down: always open, last in today's order. Follow-up: no
  // measures yet; the stopgap keeps today's choices.
  DecisionFrame::Score knockDown;
  knockDown.add("stopgap: the fallback", S::SITUATION, 0.0F, 1.0F);
  if (atGoal.isAvailable() && atGoal.total() >= knockDown.total())
  {
    return HeaderAttempt::SHOOT;
  }
  if (cushion.isAvailable() && cushion.total() >= knockDown.total())
  {
    return HeaderAttempt::CUSHION;
  }
  return HeaderAttempt::PLAY;
}

MatchEngine::HeaderPlay MatchEngine::chooseHeaderPlayCapybara(
    const MatchPlayer& header, bool fromTeammate)
{
  const float attackDirection = header.isHomeTeam ? 1.0F : -1.0F;
  const float fromOwnGoal =
      pitch.fromOwnGoalLine(header.position, header.isHomeTeam);
  HeaderPlay play;
  // A team-mate's ball: the knock-down the "expecting the ball" frame
  // chose (chooseHeaderAttemptCapybara). Otherwise the situation "I can
  // intercept the ball", head branch (step G): head it behind, clear it,
  // or knock it down. Follow-up: no measures yet; the stopgap keeps
  // today's order (and today's 33% roll for heading it behind).
  using S = TermSource;
  DecisionFrame::Score behind;
  DecisionFrame::Score clearIt;
  DecisionFrame::Score knockDown;
  knockDown.add("stopgap: the fallback", S::SITUATION, 0.0F, 1.0F);
  // Behind his own penalty spot a defender often plays safe and heads it
  // out of play.
  if (!fromTeammate && fromOwnGoal < MatchTuning::Pitch::PENALTY_SPOT_METRES &&
      randomFloat(0.0F, 1.0F) < MatchTuning::SetPiece::HEADER_BEHIND_CHANCE)
  {
    behind.add("stopgap: first in today's order", S::SITUATION, 1.0F,
               HEADER_FIRST);
  }
  else
  {
    behind.ruleOut();
  }
  if (!fromTeammate &&
      fromOwnGoal < MatchTuning::SetPiece::DEFENSIVE_HEADER_WITHIN_METRES)
  {
    clearIt.add("stopgap: second in today's order", S::SITUATION, 1.0F,
                HEADER_SECOND);
  }
  else
  {
    clearIt.ruleOut();
  }
  if (behind.isAvailable() && behind.total() >= knockDown.total())
  {
    return chooseHeaderOutCapybara(header);
  }
  if (clearIt.isAvailable() && clearIt.total() >= knockDown.total())
  {
    // Defensive header: clear the danger, upfield and away from the middle.
    const float wide = header.position.y < pitch.atY(MatchTuning::Pitch::CENTRE)
                           ? -1.0F
                           : 1.0F;
    const Vector2F direction =
        normalized({.x = attackDirection * randomFloat(0.5F, 1.0F),
                    .y = wide * randomFloat(0.1F, 0.8F)});
    const Vector2F reach = {.x = direction.x, .y = direction.y};
    play.kind = HeaderPlay::Kind::CLEAR;
    play.aim =
        PitchPoint{header.position.x + reach.x, header.position.y + reach.y};
    return play;
  }
  // Knock-down or flick-on toward the nearest team-mate who is not
  // behind the header (never back toward the own goal).
  float best = std::numeric_limits<float>::max();
  for (const auto& other : players)
  {
    if (!active(other) || &other == &header ||
        other.isHomeTeam != header.isHomeTeam || other.isGoalkeeper)
    {
      continue;
    }
    if ((other.position.x - header.position.x) * attackDirection <
        -MatchTuning::Aerial::KNOCK_DOWN_MAX_BACKWARD_METRES)
    {
      continue;
    }
    const float d = distance(other.position, header.position);
    if (d < best)
    {
      best = d;
      play.target = &other;
    }
  }
  play.kind = HeaderPlay::Kind::KNOCK_DOWN;
  play.aim =
      (play.target != nullptr)
          ? play.target->position
          : PitchPoint{header.position.x + pitch.atX(attackDirection * 0.1F),
                       header.position.y};
  return play;
}

MatchPlayer* MatchEngine::bestSetPieceTakerCapybara(bool homeTeam,
                                                    bool shooting)
{
  MatchPlayer* best = nullptr;
  float bestScore = -std::numeric_limits<float>::infinity();
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam ||
        player.isGoalkeeper || player.isInjured)
    {
      continue;
    }
    const float score =
        shooting ? (player.shooting * 0.75F) + (player.passing * 0.15F) +
                       (player.vision * 0.10F)
                 : (player.passing * 0.6F) + (player.vision * 0.4F);
    if (score > bestScore)
    {
      bestScore = score;
      best = &player;
    }
  }
  return best;
}

MatchPlayer* MatchEngine::dutyTakerCapybara(bool homeTeam, SetPieceDuty duty)
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
      {
        return &player;
      }
    }
  }
  switch (duty)
  {
    case SetPieceDuty::Penalties:
    case SetPieceDuty::FreeKicks:
      return bestSetPieceTakerCapybara(homeTeam, true);
    case SetPieceDuty::CornersLeft:
    case SetPieceDuty::CornersRight:
      return bestSetPieceTakerCapybara(homeTeam, false);
    default:
      return nullptr;
  }
}

MatchEngine::SetPieceAttackers MatchEngine::arrangeSetPieceAttackCapybara(
    bool attackingHome, PitchPoint ballPosition)
{
  // Box targets for the attacking side, in metres from the goal line and
  // from the centre of the goal (positive towards the ball's side).
  const float goalX = attackingHome ? 1.0F : 0.0F;
  const float inward = attackingHome ? -1.0F : 1.0F;
  const float ballSide =
      ballPosition.y < pitch.atY(MatchTuning::Pitch::CENTRE) ? -1.0F : 1.0F;
  const auto spot = [&](float depthMetres, float lateralMetres)
  {
    return PitchPoint{
        pitch.atX(goalX) + (inward * depthMetres),
        pitch.atY(MatchTuning::Pitch::CENTRE) + (ballSide * lateralMetres)};
  };
  const std::array<PitchPoint, 5> attackSlots = {
      spot(5.5F, 3.0F), spot(6.5F, -3.5F), spot(10.5F, 0.0F), spot(8.0F, 1.0F),
      spot(17.5F, -2.0F)};

  // Rank attackers by aerial threat; the best go into the box.
  std::array<MatchPlayer*, 11> attackers{};
  std::size_t attackerCount = 0;
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != attackingHome ||
        player.isGoalkeeper || &player == restartTaker() ||
        attackerCount == attackers.size())
    {
      continue;
    }
    attackers[attackerCount++] = &player;
  }
  // Centre-backs and strikers attack the delivery; full-backs and a
  // holding midfielder form the rest defence.
  const auto boxThreat = [](const MatchPlayer* candidate)
  {
    float roleBias = 0.0F;
    switch (candidate->player->getRole())
    {
      case PlayerRole::ST:
        roleBias = 0.12F;
        break;
      case PlayerRole::CB:
        roleBias = 0.0F;
        break;
      case PlayerRole::LB:
      case PlayerRole::RB:
        roleBias = -0.3F;
        break;
      case PlayerRole::CDM:
        roleBias = -0.15F;
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
        if (a != b)
        {
          return a > b;
        }
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

void MatchEngine::arrangeSetPieceDefenceCapybara(
    bool defendingHome, PitchPoint ballPosition,
    const SetPieceAttackers& attackers)
{
  // Spots measured from the defended goal, as the attackers' are.
  const float goalX = defendingHome ? 0.0F : 1.0F;
  const float inward = defendingHome ? 1.0F : -1.0F;
  const float ballSide =
      ballPosition.y < pitch.atY(MatchTuning::Pitch::CENTRE) ? -1.0F : 1.0F;
  const auto spot = [&](float depthMetres, float lateralMetres)
  {
    return PitchPoint{
        pitch.atX(goalX) + (inward * depthMetres),
        pitch.atY(MatchTuning::Pitch::CENTRE) + (ballSide * lateralMetres)};
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
    {
      continue;
    }
    if (player.isGoalkeeper)
    {
      player.movementTarget = spot(0.8F, 0.0F);
      continue;
    }
    defenders[defenderCount++] = &player;
  }
  insertionSort(defenders, defenderCount,
                [](const MatchPlayer* first, const MatchPlayer* second)
                {
                  const float a = first->defending + first->physicality;
                  const float b = second->defending + second->physicality;
                  if (a != b)
                  {
                    return a > b;
                  }
                  return first->player->getId() < second->player->getId();
                });
  std::size_t next = 0;
  for (std::size_t index = 0; index < boxAttackers && next < defenderCount;
       ++index, ++next)
  {
    const PitchPoint mark = attackers.players[index]->movementTarget;
    defenders[next]->movementTarget = PitchPoint{
        mark.x -
            (inward * MatchTuning::SetPiece::MARKING_GOAL_SIDE_OFFSET_METRES),
        mark.y};
  }
  const std::array<PitchPoint, 3> zones = {spot(3.0F, 2.5F), spot(6.0F, 0.0F),
                                           spot(16.0F, 0.0F)};
  for (std::size_t zone = 0; zone < zones.size() && next < defenderCount;
       ++zone, ++next)
  {
    defenders[next]->movementTarget = zones[zone];
  }
}

MatchPlayer* MatchEngine::chooseRestartTakerCapybara(MatchState restart,
                                                     bool homeTeam,
                                                     PitchPoint spot,
                                                     bool topCorner)
{
  switch (restart)
  {
    case MatchState::KICK_OFF:
      if (MatchPlayer* taker = findClosestPlayer(spot, homeTeam, false))
      {
        return taker;
      }
      return findClosestPlayer(spot, homeTeam, true);
    case MatchState::GOAL_KICK:
      if (MatchPlayer* keeper = findGoalkeeper(homeTeam))
      {
        return keeper;
      }
      return findClosestPlayer(spot, homeTeam, true);
    case MatchState::PENALTY:
      return dutyTakerCapybara(homeTeam, SetPieceDuty::Penalties);
    case MatchState::CORNER_KICK:
      if (MatchPlayer* taker = dutyTakerCapybara(
              homeTeam, topCorner ? SetPieceDuty::CornersLeft
                                  : SetPieceDuty::CornersRight))
      {
        return taker;
      }
      return findClosestPlayer(spot, homeTeam, false);
    case MatchState::FREE_KICK:
      if (restartIsSetPiece)
      {
        if (MatchPlayer* taker =
                dutyTakerCapybara(homeTeam, SetPieceDuty::FreeKicks))
        {
          return taker;
        }
      }
      return findClosestPlayer(spot, homeTeam, false);
    case MatchState::THROW_IN:
    {
      // A designated long-throw specialist takes the throws in the attacking
      // third; elsewhere (and without one) the nearest player does.
      const float attackingDepth =
          homeTeam ? pitch.fractionX(spot) : 1.0F - pitch.fractionX(spot);
      if (attackingDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START)
      {
        if (MatchPlayer* taker =
                dutyTakerCapybara(homeTeam, SetPieceDuty::LongThrows))
        {
          return taker;
        }
      }
      return findClosestPlayer(spot, homeTeam, false);
    }
    default:
      return findClosestPlayer(spot, homeTeam, false);
  }
}

MatchEngine::RestartPlay MatchEngine::chooseRestartPlayCapybara(
    MatchPlayer& taker, MatchState restartState)
{
  // The situation "Restart: I'm the taker" (brain redesign plan section 21,
  // step H). Options: a pass, a short corner, a delivery into the box, a
  // direct shot, hold it. Open as before (the corner's and the free kick's
  // rolls included); the stopgap pick is today's order (follow-up:
  // measures). Each option carries what it chooses: the pass, the
  // delivery's target, the direct shot's aim.
  heldRestartAimCapybara.reset();
  if (restartState == MatchState::CORNER_KICK)
  {
    return chooseCornerPlayCapybara(taker);
  }
  if (restartState == MatchState::FREE_KICK && restartIsSetPiece)
  {
    const PitchPoint goal =
        pitch.at(taker.isHomeTeam ? 1.0F : 0.0F, MatchTuning::Pitch::CENTRE);
    const float goalMetres = distance(ball.position, goal);
    const float lateralMetres =
        std::abs(ball.position.y - pitch.atY(MatchTuning::Pitch::CENTRE));
    const bool shootingRange =
        goalMetres <= MatchTuning::SetPiece::DIRECT_FREE_KICK_METRES &&
        lateralMetres <= MatchTuning::SetPiece::DIRECT_FREE_KICK_WIDTH_METRES;
    const float shootChance = std::clamp(
        MatchTuning::SetPiece::DIRECT_SHOT_BASE +
            (taker.shooting * MatchTuning::SetPiece::DIRECT_SHOT_SKILL) -
            (goalMetres * MatchTuning::SetPiece::DIRECT_SHOT_DISTANCE_PENALTY),
        0.0F, 0.95F);
    if (shootingRange && randomFloat(0.0F, 1.0F) < shootChance)
    {
      // The direct shot carries its aim (today's free-kick rules, rolled
      // now), held for the strike.
      if (taker.player != nullptr)
      {
        heldRestartAimCapybara = HeldRestartAimCapybara{
            .player = taker.player->getId(),
            .aim = rollShotAimCapybara(taker, false, false, true),
            .penalty = false};
      }
      return {.kind = RestartPlay::Kind::DIRECT_SHOT,
              .pass = std::nullopt,
              .lofted = false};
    }
    // From wide or deep it is delivered into the box like a corner.
    const bool wide =
        lateralMetres > MatchTuning::SetPiece::CROSS_MIN_WIDTH_METRES ||
        goalMetres > MatchTuning::SetPiece::DIRECT_FREE_KICK_METRES;
    if (wide)
    {
      return chooseCornerPlayCapybara(taker);
    }
  }

  std::optional<PassOption> passOption = choosePassTargetCapybara(taker);
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
      {
        continue;
      }
      const float gap = distance(taker.position, candidate.position);
      if (gap > EPSILON && gap < nearest)
      {
        outlet = &candidate;
        nearest = gap;
      }
    }
    if (outlet != nullptr)
    {
      passOption = evaluatePassOptionCapybara(taker, *outlet);
    }
  }
  if (passOption)
  {
    return {.kind = RestartPlay::Kind::PASS,
            .pass = passOption,
            .lofted = restartState == MatchState::GOAL_KICK};
  }
  return {
      .kind = RestartPlay::Kind::HOLD, .pass = std::nullopt, .lofted = false};
}

MatchEngine::RestartPlay MatchEngine::chooseCornerPlayCapybara(
    MatchPlayer& taker)
{
  // Routine: short, near post, far post or penalty spot, delivered at head
  // height to the attacker assigned to that zone.
  MatchPlayer* target = nullptr;
  const float roll = randomFloat(0.0F, 1.0F);
  if (roll < MatchTuning::SetPiece::SHORT_CORNER_CHANCE)
  {
    std::optional<PassOption> shortOption = choosePassTargetCapybara(taker);
    if (shortOption &&
        shortOption->passDistance <
            MatchTuning::SetPiece::SHORT_CORNER_MAX_DISTANCE_METRES)
    {
      PassOption groundPass = *shortOption;
      groundPass.lofted = false;
      return {
          .kind = RestartPlay::Kind::PASS, .pass = groundPass, .lofted = false};
    }
  }
  const PitchPoint goal =
      pitch.at(taker.isHomeTeam ? 1.0F : 0.0F, MatchTuning::Pitch::CENTRE);
  float bestScore = -std::numeric_limits<float>::infinity();
  const float nearPostShare = MatchTuning::SetPiece::NEAR_POST_CHANCE;
  const bool nearPost =
      roll < MatchTuning::SetPiece::SHORT_CORNER_CHANCE + nearPostShare;
  for (auto& candidate : players)
  {
    if (!active(candidate) || &candidate == &taker ||
        candidate.isHomeTeam != taker.isHomeTeam || candidate.isGoalkeeper)
    {
      continue;
    }
    if (!inPenaltyArea(candidate.tacticalTarget, !taker.isHomeTeam))
    {
      continue;
    }
    const float targetMetres = distance(candidate.tacticalTarget, goal);
    const float centreY = pitch.atY(MatchTuning::Pitch::CENTRE);
    const float sideMetres = (candidate.tacticalTarget.y - centreY) *
                             (ball.position.y < centreY ? -1.0F : 1.0F);
    const float threat = MatchRules::headerReachMetres(candidate.heightMetres,
                                                       candidate.physicality);
    const float zone = nearPost ? sideMetres : -sideMetres;
    const float score =
        (threat * MatchTuning::SetPiece::TARGET_THREAT_WEIGHT) +
        (zone * 0.15F) - (targetMetres * 0.05F) +
        randomFloat(0.0F, MatchTuning::SetPiece::TARGET_RANDOMNESS);
    if (score > bestScore)
    {
      bestScore = score;
      target = &candidate;
    }
  }
  if (target == nullptr)
  {
    std::optional<PassOption> fallback = choosePassTargetCapybara(taker);
    if (fallback)
    {
      return {
          .kind = RestartPlay::Kind::PASS, .pass = fallback, .lofted = true};
    }
    return {
        .kind = RestartPlay::Kind::HOLD, .pass = std::nullopt, .lofted = false};
  }
  PassOption delivery = evaluatePassOptionCapybara(taker, *target);
  delivery.targetPoint = target->tacticalTarget;
  delivery.passDistance = distance(taker.position, delivery.targetPoint);
  delivery.lofted = true;
  delivery.intent = PassIntent::CROSS;
  return {.kind = RestartPlay::Kind::PASS, .pass = delivery, .lofted = true};
}

MatchPlayer* MatchEngine::chooseTakeOnCapybara(MatchPlayer& /*carrier*/)
{
  // No take-on choice (step C2, the user's decision): a carry is a carry,
  // scored like any other direction and pace. Take-ons come back when the
  // contest is reworked to arise from the physics (plan section 16).
  return nullptr;
}

float MatchEngine::carryUrgencyCapybara(const MatchPlayer& player) const
{
  // How urgently he carries it: the pace of the carry he chose (step C;
  // walk 0, jog 1/3, run 2/3, sprint 1), held until he decides again. A
  // drill's dribbler goes as hard as the drill asks.
  if (player.player != nullptr)
  {
    for (const auto& [keeper, urgency] : ballKeeperUrgency)
    {
      if (keeper == player.player->getId())
      {
        return std::clamp(urgency, 0.0F, 1.0F);
      }
    }
  }
  const HeldCarryCapybara* held = heldCarryOfCapybara(player);
  return held != nullptr ? held->urgency : UNCHOSEN_CARRY_URGENCY;
}

float MatchEngine::carryPaceCapybara(const MatchPlayer& player) const
{
  return carryPaceAtCapybara(player, carryUrgencyCapybara(player));
}

float MatchEngine::carryPaceAtCapybara(const MatchPlayer& player,
                                       float urgency) const
{
  // Walk at urgency 0, jog at 1/3, run at 2/3, sprint at 1 (the tops of his
  // bands, as fuel has left them), in straight lines between.
  const std::array<float, 4> tops{speedBand(player, MovementMode::WALK).high,
                                  speedBand(player, MovementMode::JOG).high,
                                  speedBand(player, MovementMode::RUN).high,
                                  currentTopSpeed(player)};
  const float scaled = urgency * 3.0F;
  const auto band = std::min(static_cast<std::size_t>(scaled), std::size_t{2});
  const float pace = tops[band] + ((tops[band + 1] - tops[band]) *
                                   (scaled - static_cast<float>(band)));
  // Dribbling caps how much of his pace he can use with the ball: his run
  // band's top with none, his top speed with the best.
  const float cap = tops[2] + (std::max(0.0F, tops[3] - tops[2]) *
                               std::clamp(player.dribbling, 0.0F, 1.0F));
  return std::min(pace, cap);
}

float MatchEngine::chooseTouchSecondsCapybara(const MatchPlayer& player,
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
  const float urgency = carryUrgencyCapybara(player);
  constexpr float JOG = 1.0F / 3.0F;
  const float open =
      urgency <= JOG
          ? soonest + ((R::PUSH_JOG_SECONDS - soonest) * urgency / JOG)
          : R::PUSH_JOG_SECONDS +
                ((R::PUSH_SPRINT_SECONDS - R::PUSH_JOG_SECONDS) *
                 (urgency - JOG) / (1.0F - JOG));
  const float closeness =
      std::clamp((R::PUSH_CLOSE_FAR_METRES - nearestOpponentDistance(player)) /
                     (R::PUSH_CLOSE_FAR_METRES - R::PUSH_CLOSE_NEAR_METRES),
                 0.0F, 1.0F);
  const float wanted = std::max(soonest, open + ((soonest - open) * closeness));
  // A turn or the end coming within it: as far as there, to play it again
  // where he turns or stops it.
  if (toEnd < pace * wanted)
  {
    return std::max(soonest, toEnd / std::max(pace, R::LOOSE_START_SPEED));
  }
  return wanted;
}

float MatchEngine::pushMetresCapybara(const MatchPlayer& player,
                                      PitchPoint from, Vector2F direction,
                                      float startGap, float pace, float seconds,
                                      std::optional<float> toGoal) const
{
  // As far as he runs at `pace` in `seconds` (from `startGap` behind it),
  // so he gets to it about then; never past where he is going, with room
  // for it to roll on past the spot; and no further than he still gets to
  // it first: an opponent who could reach a point of it (after reading
  // it) no later than he does, less a margin, holds it short of there.
  using R = MatchTuning::Dribble;
  const float shortest = touchKnobs[TouchKnob::SET_OFFSET];
  float longest = std::min(R::PUSH_MAX_METRES, (pace * seconds) - startGap);
  if (toGoal)
  {
    longest = std::min(longest, *toGoal - (pace * R::PUSH_ROLL_ON_SECONDS));
  }
  if (longest <= shortest)
  {
    return shortest;
  }
  const float startSpeed =
      std::max(0.0F, (player.velocity.x * direction.x) +
                         (player.velocity.y * direction.y));
  const float run = std::max(pace, R::LOOSE_START_SPEED);
  // How long his run to a point `metres` beyond the contact takes, speeding
  // up toward his pace as his legs allow.
  const auto hisSeconds = [&](float metres)
  {
    const float distanceRun = metres + startGap;
    float time = distanceRun / run;
    for (int pass = 0; pass < 2; ++pass)
    {
      time = distanceRun / std::max(dribbleReachablePaceBinturong(
                                        player, startSpeed, run, time),
                                    EPSILON);
    }
    return time;
  };
  const Vector2F start = asVector(from);
  constexpr float STEP_METRES = 0.5F;
  // Stop short of the lines: 1.05 m from the goal lines, 0.68 m from the
  // touchlines.
  constexpr float EDGE_END_METRES = 1.05F;
  constexpr float EDGE_SIDE_METRES = 0.68F;
  for (float along = shortest; along <= longest + EPSILON; along += STEP_METRES)
  {
    const Vector2F point{.x = start.x + (direction.x * along),
                         .y = start.y + (direction.y * along)};
    const PitchPoint spot = asPoint(point);
    if (spot.x < EDGE_END_METRES || spot.x > pitch.length - EDGE_END_METRES ||
        spot.y < EDGE_SIDE_METRES || spot.y > pitch.width - EDGE_SIDE_METRES)
    {
      return std::max(shortest, along - STEP_METRES);
    }
    const float his = hisSeconds(along);
    for (const MatchPlayer& opponent : players)
    {
      if (!active(opponent) || opponent.isHomeTeam == player.isHomeTeam)
      {
        continue;
      }
      const float reach =
          opponent.isGoalkeeper
              ? MatchTuning::Ball::GOALKEEPER_CONTROL_RADIUS_METRES
              : feetReach(opponent);
      // His run there, after reading it: from his speed toward it now,
      // speeding up as his legs allow.
      const Vector2F at = asVector(opponent.position);
      const Vector2F toPoint{.x = point.x - at.x, .y = point.y - at.y};
      const float apart = length(toPoint);
      const float gap = std::max(0.0F, apart - reach);
      const float top = std::max(currentTopSpeed(opponent), EPSILON);
      if (gap / top + R::PUSH_READ_SECONDS > his + R::PUSH_MARGIN_SECONDS)
      {
        continue;
      }
      const float theirStart =
          apart > EPSILON ? std::max(0.0F, (opponent.velocity.x * toPoint.x +
                                            opponent.velocity.y * toPoint.y) /
                                               apart)
                          : 0.0F;
      float theirRun = gap / top;
      for (int pass = 0; pass < 2; ++pass)
      {
        theirRun = gap / std::max(dribbleReachablePaceBinturong(
                                      opponent, theirStart, top, theirRun),
                                  EPSILON);
      }
      if (theirRun + R::PUSH_READ_SECONDS <= his + R::PUSH_MARGIN_SECONDS)
      {
        return std::max(shortest, along - STEP_METRES);
      }
    }
  }
  return longest;
}

float MatchEngine::chooseGetUpCapybara(const MatchPlayer& player)
{
  // The situation "I'm on the ground" (brain redesign plan section 23,
  // step J): how quick to get up is his call, by urgency (collision plan
  // step 5). Options: up for the restart (play has stopped), jump up for
  // the ball (near and still to be won), up gingerly (play has run on a
  // long while), stay down for now. Follow-up: no measures yet; the
  // stopgap pick is today's order (the first open option).
  using C = MatchTuning::Collision;
  const float agility = player.derivedStat(DerivedStat::AGILITY);
  const float gingerly =
      alongStatScale(agility, C::GINGERLY_SECONDS_AT_AGILITY_1,
                     C::GINGERLY_SECONDS_AT_AGILITY_99);
  const bool restart = state != MatchState::PLAYING;
  // The ball near and still to be won: loose, his side's, or an
  // opponent's with a team-mate of his on it.
  bool forTheBall = false;
  if (!restart &&
      distance(player.position, ball.position) <= C::GET_UP_BALL_METRES)
  {
    const MatchPlayer* holder = findMatchPlayer(getPossessionAttachment());
    bool chance = holder == nullptr || holder->isHomeTeam == player.isHomeTeam;
    if (holder == nullptr && getPossessionState().isPass)
    {
      chance = getPossessionState().passByHome == player.isHomeTeam;
    }
    if (!chance)
    {
      for (const MatchPlayer& mate : players)
      {
        if (active(mate) && &mate != &player &&
            mate.isHomeTeam == player.isHomeTeam && !mate.fallen &&
            distance(mate.position, ball.position) <= C::CONTESTED_METRES)
        {
          chance = true;
          break;
        }
      }
    }
    forTheBall = chance;
  }
  const std::array<bool, GET_UP_COUNT> open{
      restart, forTheBall, player.downSeconds >= C::GINGERLY_AFTER_SECONDS,
      true};
  switch (static_cast<GetUp>(
      std::distance(open.begin(), std::ranges::find(open, true))))
  {
    case GetUp::FOR_THE_RESTART:
      ++stats.collisionGetUpsInStoppage;
      return gingerly;
    case GetUp::FOR_THE_BALL:
      ++stats.collisionGetUpsForBall;
      return alongStatScale(agility, C::GET_UP_SECONDS_AT_AGILITY_1,
                            C::GET_UP_SECONDS_AT_AGILITY_99);
    case GetUp::GINGERLY:
      ++stats.collisionGetUpsGingerly;
      return gingerly;
    case GetUp::STAY_DOWN:
    case GetUp::COUNT:
      break;
  }
  return 0.0F;
}
