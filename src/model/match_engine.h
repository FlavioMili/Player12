// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "global/stats_config.h"
#include "global/types.h"
#include "model/lineup.h"
#include "model/match_ball_physics.h"
#include "model/match_body.h"
#include "model/match_brain.h"
#include "model/match_handshake.h"
#include "model/match_manager.h"
#include "model/match_out_of_play.h"
#include "model/match_possession.h"
#include "model/match_context.h"
#include "model/match_derived_stats.h"
#include "model/match_first_touch_knobs.h"
#include "model/match_strike_knobs.h"
#include "model/match_events.h"
#include "model/match_referee.h"
#include "model/match_rules.h"
#include "model/match_scenario.h"
#include "model/match_state.h"
#include "model/match_tracking.h"
#include "model/match_tuning.h"
#include "model/strategy.h"
#include "model/tactics.h"

/** How a player moves (fatigue plan 4.2): the brain's vocabulary, which the
 * body turns into a speed band for him (MatchEngine::speedBand). */
enum class MovementMode : std::uint8_t
{
  WALK,
  JOG,
  RUN,
  SPRINT
};
inline constexpr std::size_t MOVEMENT_MODE_COUNT = 4;

/** A range of speeds (m/s). */
struct SpeedBand
{
  float low = 0.0f;
  float high = 0.0f;
};

/** The first threshold (moderate-heavy boundary) as a share of critical
 * speed: 82.3% (Hunter et al. 2024, 26 studies): where a jog becomes a run. */
inline constexpr float JOG_RUN_CRITICAL_SHARE = 0.823f;

/**
 * A mode's band from a player's walk-run transition, critical speed and top
 * speed (m/s), each now: walk up to the transition, jog to the first
 * threshold, run to critical speed, sprint to top speed. The bands never
 * pass his top speed, so as fatigue lowers his top speed the faster bands
 * close (a spent player only walks).
 */
inline SpeedBand speedBandFrom(MovementMode mode, float transition,
                               float critical, float top)
{
  const float walkTop = std::min(transition, top);
  const float jogTop =
      std::clamp(JOG_RUN_CRITICAL_SHARE * critical, walkTop, top);
  const float runTop = std::clamp(critical, jogTop, top);
  switch (mode)
  {
    case MovementMode::WALK:
      return {0.0f, walkTop};
    case MovementMode::JOG:
      return {walkTop, jogTop};
    case MovementMode::RUN:
      return {jogTop, runTop};
    case MovementMode::SPRINT:
      return {runTop, top};
  }
  return {0.0f, walkTop};
}

/**
 * The speed a mode asks for, `distance` metres from his target: the top of
 * its band; on a jog job, a ramp by distance (`jogRamp`, m) from the top of
 * walk close in, through the top of jog, to the top of run far off. The
 * tops are his now (walk, jog, run, sprint).
 */
inline float modeTargetSpeed(MovementMode mode, float distance,
                             const std::array<float, 4>& jogRamp,
                             const std::array<float, 4>& tops)
{
  const auto top = [&tops](MovementMode band)
  { return tops[static_cast<std::size_t>(band)]; };
  if (mode != MovementMode::JOG) return top(mode);
  const auto ramp = [distance](float from, float to)
  { return std::clamp((distance - from) / std::max(to - from, 1e-3f), 0.0f, 1.0f); };
  const float walk = top(MovementMode::WALK);
  const float jog = top(MovementMode::JOG);
  const float run = top(MovementMode::RUN);
  return walk + (jog - walk) * ramp(jogRamp[0], jogRamp[1]) +
         (run - jog) * ramp(jogRamp[2], jogRamp[3]);
}

inline const char* movementModeName(MovementMode mode)
{
  constexpr std::array<const char*, MOVEMENT_MODE_COUNT> NAMES{
      "walk", "jog", "run", "sprint"};
  return NAMES[static_cast<std::size_t>(mode)];
}

/** Tactical purpose of the most recently executed pass. */
enum class PassIntent
{
  RECYCLE,
  PROGRESSIVE,
  THROUGH_BALL,
  CROSS,
  CUTBACK,
  SWITCH_PLAY,
  PRESSURE_RELEASE,
  SET_PIECE
};

class MatchRecorder;
struct MatchActionRecord;
struct PassCandidateDetail;
struct ScoreBreakdown;

struct PassDecision
{
  std::uint32_t passerId = 0;
  std::uint32_t receiverId = 0;
  Vector2F targetPoint{MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE};
  PassIntent intent = PassIntent::RECYCLE;
  float utility = 0.0f;
  float progression = 0.0f;
  float laneRisk = 0.0f;
  float completionProbability = 0.0f;
};

/** Best and runner-up pass candidates plus the final chosen reason. */
enum class ScenarioAction
{
  NONE,
  SHOT,
  PASS,
  CARRY,
  SHIELD,
  CLEAR
};

struct ScenarioDecision
{
  ScenarioAction action = ScenarioAction::NONE;
  std::optional<PassDecision> best;
  std::optional<PassDecision> runnerUp;
  std::string reason;
  float passUtility = -std::numeric_limits<float>::infinity();
  float shotUtility = -std::numeric_limits<float>::infinity();
  float carryUtility = -std::numeric_limits<float>::infinity();
  float shieldUtility = -std::numeric_limits<float>::infinity();
};

/**
 * One on-pitch slot. `position`, `basePosition` and `movementTarget` are
 * normalised pitch coordinates; `velocity` is in metres per second (x along
 * the length, y across the width). `facingAngle` is the heading in
 * normalised pitch space, as renderers expect.
 */
struct MatchPlayer
{
  const Player* player = nullptr;
  bool isHomeTeam = false;
  Vector2F position{MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE};
  Vector2F velocity{0.0f, 0.0f};
  Vector2F basePosition{MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE};
  Vector2F movementTarget{MatchTuning::Pitch::CENTRE,
                          MatchTuning::Pitch::CENTRE};
  PlayerIntent intent = PlayerIntent::HOLD_SHAPE;
  /** Latest tactical target (refreshed at the tactical rate) and whether it
   * is chased urgently. */
  Vector2F tacticalTarget{MatchTuning::Pitch::CENTRE,
                          MatchTuning::Pitch::CENTRE};
  bool urgentMovement = false;
  /** How fast the tactical target itself moves (m/s): a man pressed or
   * marked tight, or (Binturong brain) the target's own move since the last
   * plan on the same job; zero on a new job. */
  Vector2F targetVelocity{0.0f, 0.0f};
  /** He means to reach his tactical target at full speed (running through
   * it), not to stop on it (a drill's finish; the brain, later). Cleared by
   * every plan refresh. */
  bool arriveAtSpeed = false;
  /** A route to the tactical target (Binturong body): movementTarget is the
   * point he heads for now and nextTarget the one after. routeWaypoints is
   * how many of the two are waypoints short of the tactical target (0: both
   * are it). Planned by the brain, advanced by the body as he reaches each
   * waypoint, and cleared by every other writer of the tactical target. */
  Vector2F nextTarget{MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE};
  std::uint8_t routeWaypoints = 0;
  /** His line through each waypoint of the route (movement targets plan
   * 3b): how far (m) inside the waypoint the turn's apex may pass
   * (negative: outside it), and how far the line may be pushed out at the
   * corner to widen the turn. Together they set how fast he takes it. */
  float routeApexOffset = 0.0f;
  float routeOutwardRoom = 0.0f;
  /** Where his line points now (normalised pitch), on a route: what his
   * touches aim at when he is dribbling. */
  Vector2F routeAim{};
  /** The player he is overtaking (slot, -1: none) and on which side (+1
   * his left), kept between refreshes so he does not swap sides on a near
   * tie (movement targets plan 4a). */
  std::int8_t overtaking = -1;
  std::int8_t overtakeSide = 0;
  /** Collision plan: the player he last ran into (slot, -1: none) and when
   * (engine seconds). */
  std::int8_t lastContactSlot = -1;
  double lastContactAt = -1.0e9;
  /** Way-round plan: chasing a loose ball (the brain's ground chaser, or one
   * going up for a loose high ball), so no one else's obstacle; and whether
   * he steers round others (the ground chaser, a player moving to hold
   * shape, offer support or recover shape), with when he meets it
   * (s, as the brain reckoned). Set by every plan; a drill's held player is
   * neither. */
  bool chasingLooseBall = false;
  bool steersRound = false;
  float looseBallMeetSeconds = 0.0f;
  /** The player he is going round (slot, -1: none) and on which side (+1
   * his left), kept between refreshes (way-round plan, decision 7). */
  std::int8_t wayRoundOf = -1;
  std::int8_t wayRoundSide = 0;
  /** The player he saw in his way at the last plan but found no clear side
   * round (slot, -1: none). */
  std::int8_t wayRoundBlockedBy = -1;
  /** Giving way (way-round plan): the speed (m/s) he holds himself to
   * while someone with the right of way crosses ahead of him; below 0,
   * none. Set by every plan. And whom he gives way to, or has the right
   * of way over (slot, -1: nobody). */
  float giveWaySpeed = -1.0f;
  std::int8_t givingWayTo = -1;
  std::int8_t rightOfWayOver = -1;
  /** A body orientation asked of him (orientation plan, stage 1), as an
   * angle like facingAngle (normalised pitch space): his body turns to it
   * and he moves forward, sideways or backward relative to it. Without one
   * his body follows his run. Set by drills (the brain, later); cleared at
   * each refresh and on restart walks. */
  bool facingRequested = false;
  float facingRequest = 0.0f;
  /** Waypoints reached so far (counts up; the planner reads it to move its
   * plan on). */
  std::uint32_t routeAdvances = 0;
  /** Seconds since he last made a stop on his route (a sharp reversal:
   * planted and turned there), counting up; large when none lately. */
  float sinceStopSeconds = 1.0e6f;
  /** Where the leg to his waypoint began (the waypoint before, or where he
   * was when given it), and that waypoint (body's own bookkeeping). */
  Vector2F routeLegStart{};
  Vector2F routeLegEnd{};
  /** The point after his waypoint, as his line last had it. */
  Vector2F routeLegNext{};
  /** His route moved on before the curve he was on ended, and finishing it
   * was the quicker way on: his line holds that curve (its leg in, its
   * waypoint and the way out) to its end, then takes up his route. */
  bool holdingTurn = false;
  Vector2F heldLegStart{};
  Vector2F heldWaypoint{};
  Vector2F heldNext{};
  /** Where on the held curve he hands over to his route's line. */
  Vector2F heldUntil{};

  float facingAngle = 0.0f;
  float targetAngle = 0.0f;
  float turnRate = MatchTuning::Player::FACING_TURN_RATE_RADIANS;

  bool isTrapping = false;
  float trapTimer = 0.0f;
  bool isDiving = false;
  float diveTimer = 0.0f;
  /** A Binturong keeper: how long he has been at walking pace or slower
   * (s): bent to gather a loose ball (goalkeeper plan step 8). */
  float slowSeconds = 0.0f;
  /** A jump (Binturong body; aerial challenge plan step 1): his feet's
   * height (m) and vertical speed (m/s, + up) while he is in the air;
   * whether it was his running jump (else his standing one); the landing
   * it will take him, half his time in the air (s); and how much of that
   * landing is still to come (s). */
  float feetHeight = 0.0f;
  float jumpSpeed = 0.0f;
  bool runningJump = false;
  float jumpLandingSeconds = 0.0f;
  float landingSeconds = 0.0f;
  /** Planted for a thigh touch: how long until he can accelerate again (s;
   * he can still brake). */
  float plantedSeconds = 0.0f;
  /** Collision plan: after a knock he kept his feet and balance through,
   * how long until he can drive, steer or brake again (s); until then he
   * coasts (MatchTuning::Collision). */
  float collisionPause = 0.0f;
  /** Collision plan: knocked off balance, how long his stumble still lasts
   * (s): his recovery steps brake him, his body turns to his movement, and
   * he neither drives nor steers. */
  float stumbleSeconds = 0.0f;
  /** Collision plan step 4: knocked over, he is down, the cylinder on its
   * side. He slides to a stop, cannot play the ball or be run into (others
   * hurdle him), and gets up when the brain says (fallenBinturong). */
  bool fallen = false;
  /** While down: the way his body lies (an angle like facingAngle), and
   * whether that is across his slide (he rolls along it). */
  float lyingAngle = 0.0f;
  bool lyingAcross = false;
  /** Knocked forward against his facing he went down on his front,
   * back on his back (for a sideways knock, whichever way it leaned). */
  bool fellOnFront = false;
  /** While down: how far his body has rolled (m), for its turn. */
  float rolledMetres = 0.0f;
  /** How long he has been down (s). */
  float downSeconds = 0.0f;
  /** A big hit: the least time he stays down (s; 0 if not one), and the
   * jolt that put him down (m/s). */
  float bigHitSeconds = 0.0f;
  float fallJolt = 0.0f;
  /** Rolling about in pain: what is left of this roll (s), its way (+1 or
   * -1 across his body; 0 before the first), and the wait for the next. */
  float rollSeconds = 0.0f;
  float rollDirection = 0.0f;
  float rollPauseSeconds = 0.0f;
  /** Getting up: under way, and how long it still takes (s). */
  bool gettingUp = false;
  float getUpSeconds = 0.0f;
  float getUpTotalSeconds = 0.0f;
  /** The way he slid (an angle like facingAngle): he faces it when up. */
  float slideAngle = 0.0f;
  /** While down: on his front now (each half turn he rolls turns him over). */
  /** How far through getting up he is: 0 lying (or not yet started), 1 up. */
  [[nodiscard]] float getUpShare() const
  {
    if (!gettingUp || getUpTotalSeconds <= 0.0f) return 0.0f;
    return std::clamp(1.0f - getUpSeconds / getUpTotalSeconds, 0.0f, 1.0f);
  }
  [[nodiscard]] bool onFront() const
  {
    const float turns =
        rolledMetres / MatchTuning::Collision::ROLL_METRES_PER_TURN + (fellOnFront ? 0.0f : 0.5f);
    return std::cos(turns * 6.2831853f) > 0.0f;
  }
  [[nodiscard]] bool airborne() const { return feetHeight > 0.0f || jumpSpeed > 0.0f; }
  bool isPressing = false;
  bool isMakingRun = false;

  /** Slow energy pool (match condition, fuel) in [floor, 1]: the floor is
   * MINIMUM_STAMINA for an Aardvark body, 0 for a Binturong one. */
  float stamina = 1.0f;
  /** Fast repeat-sprint reserve in [0, 1]. */
  float sprintReserve = 1.0f;
  /** How much of the reserve's shortfall (a share of its fresh capacity)
   * refills on the fast track (Binturong body; the rest refills slowly). */
  float fastBurstDebt = 0.0f;
  /** His speed over the last load period (m/s), for the next one's
   * acceleration in the energy model. */
  float loadSpeed = 0.0f;
  /** The Binturong brain's last plan for him: its target, its job (the
   * planner's branch and the choices made in it), the step and the plan
   * epoch. A refresh on the same job turns the target's move since then
   * into targetVelocity. */
  Vector2F plannedTarget{};
  std::uint64_t plannedJob = 0;
  std::uint64_t plannedStep = 0;
  std::uint32_t plannedEpoch = 0;
  bool planned = false;
  /** Fresh top speed (m/s), maximum acceleration A0 and braking (m/s^2).
   * A Binturong body reads A0 from the Acceleration derived stat instead. */
  float maxSpeed = MatchTuning::Player::TOP_SPEED_BASE;
  float acceleration = MatchTuning::Player::ACCELERATION_BASE;
  float braking = MatchTuning::Player::BRAKING_BASE;
  bool isSprinting = false;
  float tackleCooldown = 0.0f;
  float actionCooldown = 0.0f;
  /** His foot is committed after a swing in a contest (tackling plan step
   * 2: the attacker's ball inside both reaches): no foot touch of the ball
   * until it has run down (s). Started only by the contest. */
  float footCooldown = 0.0f;
  /** Tackling plan step 5: the step he last decided whether to go into a
   * contest, and what he decided. */
  std::uint64_t goInStep = ~std::uint64_t{0};
  bool goingIn = false;

  float pace = MatchTuning::Player::DEFAULT_ATTRIBUTE;
  float shooting = MatchTuning::Player::DEFAULT_ATTRIBUTE;
  float passing = MatchTuning::Player::DEFAULT_ATTRIBUTE;
  float dribbling = MatchTuning::Player::DEFAULT_ATTRIBUTE;
  float defending = MatchTuning::Player::DEFAULT_ATTRIBUTE;
  float goalkeeping = MatchTuning::Player::DEFAULT_ATTRIBUTE;
  float physicality = MatchTuning::Player::DEFAULT_ATTRIBUTE;
  /** Stamina attribute (endurance); `stamina` above is the live condition. */
  float endurance = MatchTuning::Player::DEFAULT_ATTRIBUTE;
  float vision = MatchTuning::Player::DEFAULT_ATTRIBUTE;
  float heightMetres = MatchTuning::Units::DEFAULT_PLAYER_HEIGHT_METRES;
  /** Derived stats (match_derived_stats.h: future game stats and
   * match-engine-only stats), ratings 0-1 worked out from the attributes
   * above when he is loaded. */
  std::array<float, DERIVED_STAT_COUNT> derived{};
  /** Bit per derived stat set by a drill (kept when the rest re-derive). */
  std::uint32_t derivedOverrides = 0;
  static_assert(DERIVED_STAT_COUNT <= 32, "derivedOverrides holds a bit per derived stat");
  float derivedStat(DerivedStat stat) const
  {
    return derived[static_cast<std::size_t>(stat)];
  }

  /** False once sent off or forced off injured with no substitute left. */
  bool onPitch = true;
  /** Goalkeeper duties (the natural keeper, or an emergency replacement). */
  bool isGoalkeeper = false;
  bool isInjured = false;
  /** Outfield slot of the side's formation (see MatchEngine::getFormation);
   * -1 for the starting goalkeeper. Ignored while keeping goal. */
  std::int8_t formationSlot = -1;
  int yellowCards = 0;
  /** Index into MatchEngine::getPlayerStats() for the current occupant. */
  std::size_t statsIndex = 0;
};

/** Where a walk-out or walk-off is (touchline plan stage 4). */
enum class WalkPhase : std::uint8_t
{
  NONE,
  /** Half-time: everyone walking into the tunnel. */
  OFF,
  /** Half-time: everyone in; the break runs. */
  IN,
  /** Out of the tunnel to the line-up (first half) or the kick-off. */
  OUT,
  /** Lined up before the handshake. */
  LINED_UP,
  HANDSHAKE
};

/** A walk-out or walk-off in progress (touchline plan stage 4). */
struct WalkState
{
  WalkPhase phase = WalkPhase::NONE;
  /** Seconds in this phase. */
  float seconds = 0.0f;
  /** The first step has been taken (the first walk-out set up, or not). */
  bool started = false;
  /** This walk-out lines up for the handshake (the match's first). */
  bool lineUp = false;
  bool homeKicksOff = true;
  /** When each sets off out of the tunnel (seconds into OUT): players by
   * slot, the touchline's bodies by index. */
  std::vector<float> playerRelease;
  std::vector<float> touchlineRelease;
  /** Line-up spots (metres, MatchGround's frame) by slot / index. */
  std::vector<Vector2F> playerSpot;
  std::vector<Vector2F> touchlineSpot;
  /** The handshake: who walks (away slots in order) and when each sets
   * off; along the line, the x of each hand to shake (officials then the
   * home players, the walkers' way); each walker's next hand, his pause
   * there, and whether he is through. */
  std::vector<std::size_t> walkers;
  std::vector<float> hands;
  std::vector<std::size_t> nextHand;
  std::vector<float> handPause;
  std::vector<bool> through;
};

/** Who a touchline body is and where he belongs (touchline plan stage 2);
 * his body is a MatchPlayer, so it moves and collides like a player's. */
struct TouchlinePerson
{
  TouchlineRole role = TouchlineRole::SUBSTITUTE;
  /** His seat on his side's dugout bench (normalised pitch); the manager's
   * is his spot in his technical area, moved with the play. */
  Vector2F seat{};
  /** Sitting on it (else on his feet). */
  bool seated = false;
  /** An assistant referee on the top touchline (else the bottom one). */
  bool topLine = false;
  /** Just substituted off: walking round the pitch's edge to his
   * technical area, then to his seat (touchline plan stage 5). */
  bool walkingRound = false;
};

/** What a first touch tries to do (first-touch plan): send the ball to
 * `targetSpot` (normalised), arriving at `arrivalSpeed` m/s. */
struct TouchIntent
{
  enum class Kind : std::uint8_t
  {
    SET,
    INTO_STRIDE,
  };
  Kind kind = Kind::SET;
  Vector2F targetSpot{0.0f, 0.0f};
  float arrivalSpeed = 0.0f;
  /** How little he expected the ball (0-1), the brain's read: for now 1
   * for a pass meant for someone else, else 0 (to be revisited with the
   * brain's mental attributes). */
  float surprise = 0.0f;
  /** How pressed he is (0-1), the brain's read: for now an opponent's
   * nearness, 1 on top of him, 0 at the pressure radius (to be revisited
   * with decision time and composure). */
  float pressure = 0.0f;
  /** The vertical speed he means it to leave with (m/s, + up): 0 sends it
   * off level, cushioning any drop. */
  float liftSpeed = 0.0f;
  /** A dribble touch knocked on into space (no cap on how hard he means
   * it). */
  bool knockOn = false;
  /** A dribble touch: how long until he means to play it again (s; 0,
   * none: the next touch is due as soon as it is needed). */
  float plannedSeconds = 0.0f;
};

/** A loose ball in a player's reach during one ball sub-step: where it is
 * closest to him on that stretch of its path, how close (m), and his
 * reach (m). Players move only between steps: `substepsLeft` ball
 * sub-steps of `substepSeconds` come before his next move, which then
 * comes every `stepSeconds`. */
struct TouchMoment
{
  Vector2F contactPoint{0.0f, 0.0f};
  float gap = 0.0f;
  float reach = 0.0f;
  /** Ball height there (m). */
  float height = 0.0f;
  int substepsLeft = 0;
  float substepSeconds = 0.0f;
  float stepSeconds = 0.0f;
};

/** What a first touch actually did: the ball's velocity off it (m/s), and
 * how each of its two jobs went (first-touch jobs plan). */
struct TouchResult
{
  /** Along the ground (m/s), and upward (m/s). */
  Vector2F velocity{0.0f, 0.0f};
  float verticalSpeed = 0.0f;
  /** The angle error along the turn (radians, - short of it) and the
   * weight error (m/s, + heavier than meant). */
  float angleError = 0.0f;
  float weightError = 0.0f;
  /** How hard each job was (0-1), and whether it went wrong. */
  float angleDifficulty = 0.0f;
  float weightDifficulty = 0.0f;
  bool angleFailed = false;
  bool weightFailed = false;
  /** The jobs: the turn off the line it came on (radians, 0-pi/2), whether
   * outward (turnsOutward), and the pace change (m/s). */
  float turn = 0.0f;
  bool outward = false;
  float paceChange = 0.0f;
};

/**
 * Stretch over his reach: a value (0-1) at each angle off his facing
 * (degrees, ascending, 0-180, the same either side) and each distance from
 * his centre as a share of his reach (ascending), blended linearly between
 * neighbours in both, held at the ends. Defaults:
 * MatchTuning::FirstTouch::STRETCH_*.
 */
struct StretchMap
{
  static constexpr std::size_t ANGLES = 6;
  static constexpr std::size_t DISTANCES = 6;
  std::array<float, ANGLES> angles = MatchTuning::FirstTouch::STRETCH_ANGLES;
  std::array<float, DISTANCES> distances =
      MatchTuning::FirstTouch::STRETCH_DISTANCES;
  std::array<std::array<float, DISTANCES>, ANGLES> values =
      MatchTuning::FirstTouch::STRETCH_VALUES;

  /** The stretch `degrees` off his facing, `share` of his reach out. */
  [[nodiscard]] float at(float degrees, float share) const;
};

/**
 * Everything that differs between kinds of contact with the ball (contact
 * plan §3): the jobs' sizes, what each condition adds to each job's
 * difficulty, and how each job goes wrong. A first touch and a dribble
 * touch share the touch profile (the touch knobs).
 */
struct ContactProfile
{
  /** A condition's weights: what it adds to the angle and weight jobs. */
  struct Weights
  {
    float angle = 0.0f;
    float weight = 0.0f;
  };
  // Difficulty: the jobs' sizes, then the conditions.
  float paceChangeWeight = 0.0f;
  float turnAngle = 0.0f;
  float turnOutward = 1.0f;
  float turnInward = 1.0f;
  /** The turn counts in full when the ball comes at this pace (m/s), in
   * proportion below it; 0 (the touch): always in full. */
  float turnPaceReference = 0.0f;
  /** The vertical angle job's size: the turn in steepness (1 at 90 degrees)
   * times this; neither inward nor outward. */
  float verticalTurnAngle = 0.0f;
  /** Whether the vertical angle job is carried out (its own fail, miss and
   * jitter, from the angle job's technique values). Strikes still put a
   * share of the angle error into the elevation (verticalShare) until
   * vertical contact plan step 3. */
  bool carriesOutVertical = false;
  Weights incomingPace;
  Weights approach;
  Weights ownSpeed;
  Weights stretch;
  Weights weakerFoot;
  Weights offGround;
  Weights surprise;
  Weights pressure;
  /** The spin he asks for against what he can put on (strikes): what it
   * adds to the angle and weight jobs, and to the spin job. */
  Weights spinAsked;
  float spinAskedSpin = 0.0f;
  /** The pace he asks for against his power for it (strikes): what it
   * adds to the angle and weight jobs, and the share of his power below
   * which it adds nothing. */
  Weights powerAsked;
  float powerComfortShare = 0.7f;
  /** The share of his power left for a strike square to his body, inward
   * (across it) and outward, and straight behind him (powerByAngleShare). */
  float powerInwardSquare = 1.0f;
  float powerOutwardSquare = 1.0f;
  float powerBehind = 1.0f;
  // How the conditions are measured.
  float referenceSpeed = 25.0f;
  float pressureRadius = 3.0f;
  float comfortGap = 0.0f;
  StretchMap stretchMap;
  // Technique: how often each job goes wrong, and how badly.
  float skillReduction = 0.0f;
  /** Technique error added at an empty tank (the fatigue tiers' shape
   * scales up to it). */
  float fatigueErrorGain = 0.0f;
  /** Error added when he plays it with his weaker foot, at Technique 0
   * (none at 1). */
  float weakerFootErrorGain = 0.0f;
  float angleFailBase = 0.0f;
  float angleFailRange = 0.0f;
  float weightFailBase = 0.0f;
  float weightFailRange = 0.0f;
  float angleMissBase = 0.0f;
  float angleMissRange = 0.0f;
  /** A failed angle job's miss beyond its share of the turn (radians, base
   * + difficulty x range): a strike can miss a line it never meant to turn
   * off. 0 for the touch. */
  float angleMissAbsoluteBase = 0.0f;
  float angleMissAbsoluteRange = 0.0f;
  float weightMissBase = 0.0f;
  float weightMissRange = 0.0f;
  /** Failures that go the usual way (under-turned; heavy). */
  float underTurnShare = 1.0f;
  float heavyShare = 1.0f;
  /** The share of an angle miss and jitter that goes into the elevation
   * (0 for a ball played along the ground), and of those vertical misses
   * the share that go up. */
  float verticalShare = 0.0f;
  float upShare = 0.5f;
  /** Above 0: the elevation's own jitter (rad), and on a failed angle its
   * own miss, independent of the line's, which keeps all of its error
   * (verticalShare unused). */
  float elevationJitter = 0.0f;
  /** The lowest elevation (rad) the error may leave it at: 0, along the
   * ground (a foot); a header set per contact, as far down as his head
   * can send it there (headerLimitsBinturong). */
  float lowestElevation = 0.0f;
  float angleJitter = 0.0f;
  /** The share of the angle jitter that grows with the angle job's
   * difficulty: angleJitter x (1 - share + share x difficulty). */
  float jitterDifficultyShare = 0.0f;
  float weightJitter = 0.0f;
  /** The spin job (strikes): how often the spin he asks for comes off
   * wrong, by what share of it, the share of failures that put on less
   * than he meant, and a clean strike's jitter (rad/s). All 0 until tuned:
   * the spin comes off as asked. */
  float spinFailBase = 0.0f;
  float spinFailRange = 0.0f;
  float spinMissBase = 0.0f;
  float spinMissRange = 0.0f;
  float underSpinShare = 1.0f;
  float spinJitter = 0.0f;
  /** A ball sent back the way it came leaves at most this share of its
   * incoming pace. */
  float reboundShare = 1.0f;
};

enum class TermSource : std::uint8_t;

/** A contact's conditions, in ContactAssessment::factors order, with short
 * labels for tables. */
inline constexpr std::size_t CONTACT_FACTOR_COUNT = 13;
/** The weaker-foot condition's place in the list: it scales the other
 * conditions' sum rather than adding to it. */
inline constexpr std::size_t WEAK_FOOT_FACTOR = 6;
/** The turn off the line's steepness: the vertical angle job's size. */
inline constexpr std::size_t VERTICAL_TURN_FACTOR = 10;
/** The spin he asks for against what he can put on. */
inline constexpr std::size_t SPIN_ASKED_FACTOR = 11;
/** The pace he asks for against his power (strikes). */
inline constexpr std::size_t POWER_ASKED_FACTOR = 12;
inline constexpr std::array<std::string_view, CONTACT_FACTOR_COUNT>
    CONTACT_FACTOR_LABELS = {
        "Pace change",     "Turn off its line",  "Incoming pace",
        "Approach angle",  "His own speed",      "Stretching",
        "Weaker foot",     "Off the ground",     "Surprise",
        "Pressure",        "Turn up or down",    "Spin asked",
        "Power asked"};

/** A contact's three jobs (vertical contact plan): the turn seen from
 * above, the pace (in 3D), and the turn in steepness. */
enum class ContactJob : std::uint8_t
{
  ANGLE,
  WEIGHT,
  VERTICAL,
  /** Putting on the spin he asks for (strikes). */
  SPIN,
};

/** One condition of a contact: its value (0-1) and what it adds to each
 * job's difficulty (value x weight). The conditions add to the vertical
 * angle job as to the angle job (the same weight). */
struct ContactFactor
{
  const char* name = "";
  TermSource source{};
  float value = 0.0f;
  float angleWeight = 0.0f;
  float weightWeight = 0.0f;
  float verticalWeight = 0.0f;
  float spinWeight = 0.0f;

  [[nodiscard]] float weightFor(ContactJob job) const
  {
    return job == ContactJob::ANGLE      ? angleWeight
           : job == ContactJob::WEIGHT   ? weightWeight
           : job == ContactJob::VERTICAL ? verticalWeight
                                         : spinWeight;
  }
};

/** How hard a contact is, before it is carried out (contact plan §2): the
 * line it came on, the two jobs, the conditions and each job's
 * difficulty. Reading it draws nothing at random. */
struct ContactAssessment
{
  /** What he means to do: the direction seen from above (metric, unit),
   * pace (m/s, in three dimensions) and elevation (radians, + up). */
  Vector2F aim{0.0f, 0.0f};
  float intendedSpeed = 0.0f;
  float intendedElevation = 0.0f;
  /** The ball's pace as it reaches him (m/s, in three dimensions),
   * whether it is moving, and whether he sends it back the way it came. */
  float incomingSpeed = 0.0f;
  bool moving = false;
  bool rebound = false;
  /** The reference line seen from above (metric, unit; his aim when the
   * line is straight up or down), and its elevation (radians, + up). */
  Vector2F line{0.0f, 0.0f};
  float lineElevation = 0.0f;
  /** The jobs: the turn off the line (radians, + anticlockwise), whether
   * outward, and the pace change (m/s). */
  float turn = 0.0f;
  bool outward = false;
  float paceChange = 0.0f;
  /** The turn in steepness from the line to his aim (radians, + up). */
  float verticalTurn = 0.0f;
  std::array<ContactFactor, CONTACT_FACTOR_COUNT> factors{};
  float angleDifficulty = 0.0f;
  float weightDifficulty = 0.0f;
  float verticalDifficulty = 0.0f;
  /** The spin he means (rad/s) and the spin job's difficulty. */
  BallSpin intendedSpin{};
  float spinDifficulty = 0.0f;
  /** What his weaker foot added to each job: the rest's sum times its
   * weight, when he plays it with that foot. */
  float weakFootAngle = 0.0f;
  float weakFootWeight = 0.0f;
  float weakFootVertical = 0.0f;
  float weakFootSpin = 0.0f;

  /** What condition `index` added to `job`. */
  [[nodiscard]] float added(std::size_t index, ContactJob job) const
  {
    if (index == WEAK_FOOT_FACTOR)
      return job == ContactJob::ANGLE      ? weakFootAngle
             : job == ContactJob::WEIGHT   ? weakFootWeight
             : job == ContactJob::VERTICAL ? weakFootVertical
                                           : weakFootSpin;
    const ContactFactor& factor = factors[index];
    return factor.value * factor.weightFor(job);
  }
  /** A job's difficulty (0-1). */
  [[nodiscard]] float difficulty(ContactJob job) const
  {
    return job == ContactJob::ANGLE      ? angleDifficulty
           : job == ContactJob::WEIGHT   ? weightDifficulty
           : job == ContactJob::VERTICAL ? verticalDifficulty
                                         : spinDifficulty;
  }
};

/** Open-play strikes read by the contact model (contact plan steps 2 and
 * 3), by kind: how many, and sums of each job's difficulty and of each
 * condition. */
struct StrikeContactStats
{
  struct ByKind
  {
    std::uint32_t strikes = 0;
    double angleDifficulty = 0.0;
    double weightDifficulty = 0.0;
    std::array<double, CONTACT_FACTOR_COUNT> factorSum{};
    /** Strikes with the condition at half or more. */
    std::array<std::uint32_t, CONTACT_FACTOR_COUNT> factorMarked{};
    /** Strikes the contact model carried out (step 3), the jobs that went
     * wrong, and sums of the angle error (rad, unsigned) and the weight
     * error (m/s, + harder than meant). */
    std::uint32_t carriedOut = 0;
    std::uint32_t angleFailed = 0;
    std::uint32_t weightFailed = 0;
    double angleErrorSum = 0.0;
    double weightErrorSum = 0.0;
    /** Sum of the elevation error (rad, + higher than meant). */
    double elevationErrorSum = 0.0;
    /** Held strikes dropped where his foot met the ball, because it was
     * above foot height (shots: no volleys yet); not in `strikes`. */
    std::uint32_t droppedOffGround = 0;
  };
  std::array<ByKind, STRIKE_KIND_COUNT> byKind{};
};

/** How a contact came off: the ball's velocity off it (m/s), each job's
 * chance of going wrong, whether it did, and the result against the
 * intent. */
struct ContactOutcome
{
  /** Along the ground (m/s), and upward (m/s; 0 off a ground contact). */
  Vector2F velocity{0.0f, 0.0f};
  float verticalSpeed = 0.0f;
  /** The elevation he achieved (radians). */
  float achievedElevation = 0.0f;
  float angleFailChance = 0.0f;
  float weightFailChance = 0.0f;
  bool angleFailed = false;
  bool weightFailed = false;
  /** The vertical angle job (profiles that carry it out). */
  float verticalFailChance = 0.0f;
  bool verticalFailed = false;
  /** The turn he achieved (radians) and the pace (m/s). */
  float achievedTurn = 0.0f;
  float actualSpeed = 0.0f;
  /** The spin job: the spin he put on (rad/s). */
  float spinFailChance = 0.0f;
  bool spinFailed = false;
  BallSpin achievedSpin{};
};

/** A standing player's lines between the parts of his body that play the
 * ball (metres above the ground): up to `feet`, his feet; up to `chest`,
 * his thigh; up to `head`, his chest; above it, his head. */
struct BodyLines
{
  float feet = MatchTuning::Ball::LOW_BALL_METRES;
  float chest = MatchTuning::Ball::LOW_BALL_METRES;
  float head = MatchTuning::Aerial::CONTROL_CEILING_METRES;
};

/** A point in metres: x, y over the pitch, z up. */
struct Point3F
{
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

/** His legs at one rolled point of his stride (shot block plan §3c), or
 * one put in a shot's way (§3d), as offsets (m) from his centre on the
 * ground: each leg's hip, knee and ankle, and the way its foot points
 * (metric, unit). */
struct LegPose
{
  struct Leg
  {
    Point3F hip;
    Point3F knee;
    Point3F ankle;
    Vector2F footFacing{1.0f, 0.0f};
  };
  std::array<Leg, 2> legs{};
};

/** What became of a shot passing within a defender's reach once he had
 * reacted (shot block plan §3d): a leg put out, or why not. */
enum class ShotLegOutcome : std::uint8_t
{
  LEG_OUT,
  FEET_BUSY,
  CHOSE_NOT,
  NOT_TRAVELLING,
  PAST_HIM,
  ABOVE_HIP,
  OUT_OF_REACH_HEIGHT,
  OUT_OF_REACH_RUNNING,
  BEHIND_HIM,
  COUNT,
};

/** A keeper's hands as a shot met them (goalkeeper plan step 2): held ready
 * in front of him, then, once he has reacted, both side by side to where
 * its line passes him (the nearer one alone when the other can't reach),
 * with a miss. Positions are metres from his centre (x, y) and up from the
 * ground (z); targets are on the pitch (metres). Index 0 is his left hand. */
struct ShotHands
{
  /** A keeper in his area against this shot: he has hands. */
  bool active = false;
  bool reacted = false;
  /** Going for its line (reacted, and it is not past him). */
  bool going = false;
  /** Both hands together (else the nearer one, `near`, alone). */
  bool together = false;
  std::size_t near = 0;
  /** When they start to move (ball clock, s), where from, where to, how
   * long each takes (s) and how far each has got (0-1). */
  double from = 0.0;
  std::array<Point3F, 2> start{};
  std::array<Point3F, 2> target{};
  std::array<float, 2> seconds{};
  std::array<float, 2> share{};
  /** Where they were when the ball last met them (from his centre). */
  std::array<Point3F, 2> at{};
  /** The miss's radius and the miss rolled (m); his spare time (s; less
   * than none: the ball beats his hand). */
  float errorRadius = 0.0f;
  float error = 0.0f;
  float spareSeconds = 0.0f;
};

/** A keeper's dive (goalkeeper plan step 3): committed at push-off, then
 * stretching out in the air, falling to the floor, down, and getting up.
 * Pitch positions in metres; the body tilts in the upright plane along
 * `along`. Index 0 of a pair is his left hand. */
struct KeeperDive
{
  /** Committed: from push-off until he is back on his feet. */
  bool active = false;
  /** The ball path (`pathSince`) he last decided on. */
  double decidedFor = -1.0;
  /** Push-off (simulated s), his feet there (m), the way he goes (metric
   * unit) and its left. */
  double from = 0.0;
  Vector2F origin{};
  Vector2F along{1.0f, 0.0f};
  Vector2F across{0.0f, 1.0f};
  /** His run at push-off (m/s over the ground): it carries him on through
   * the air, and he slides on the floor until friction stops him
   * (goalkeeper plan step 8). */
  Vector2F carry{};
  /** A smother (step 8): he goes down side-on across the ball's line, his
   * midriff to `midriffAt` (pitch metres), lying along `lieAlong` (feet to
   * head), his chest towards `faceTo`, his arms tucked in front of it. */
  bool smother = false;
  Vector2F midriffAt{};
  Vector2F lieAlong{1.0f, 0.0f};
  Vector2F faceTo{1.0f, 0.0f};
  /** At full stretch: his feet this far along and this high (m), his body
   * tilted this far from upright (rad), and his hands (pitch metres, z up). */
  float feetOut = 0.0f;
  float feetUp = 0.0f;
  float tilt = 0.0f;
  std::array<Point3F, 2> handEnd{};
  /** Where his hands started (from his feet at push-off). */
  std::array<Point3F, 2> handStart{};
  bool together = false;
  std::size_t near = 0;
  /** Push-off to full stretch, full stretch to the floor, on the floor
   * until up (s); how far he drops after full stretch (m). */
  float stretchSeconds = 0.0f;
  float fallSeconds = 0.0f;
  float dropMetres = 0.0f;
  float downSeconds = 0.0f;
  /** The miss's radius and the miss (m), his spare time (s). */
  float errorRadius = 0.0f;
  float error = 0.0f;
  float spareSeconds = 0.0f;
};

/** A diving keeper's shape at a moment (pitch metres, z up). */
struct KeeperDivePose
{
  Point3F feet{};
  Point3F hip{};
  Point3F neck{};
  Point3F top{};
  std::array<Point3F, 2> shoulders{};
  std::array<Point3F, 2> hands{};
};

/** His legs as a shot passing him met them (shot block plan §3c, §3d):
 * the stride rolled for the ball's path, then, once he has reacted and if
 * he can reach its line, a leg put across it. */
struct ShotLegs
{
  /** A keeper's hands (goalkeeper plan step 2). */
  ShotHands hands;
  /** The ball path (`pathSince`) this is for, and when `pose` was last set
   * (ball clock, s). */
  double pathFor = -1.0;
  double rolledAt = -1.0;
  /** As the ball last met them: the stride, or the block once he went. */
  LegPose pose;
  /** His natural stride for this path. */
  LegPose stride;
  /** Its line passed within his full reach (1.0 m, whatever its height or
   * his speed) when it first came near him: counted for the lab. */
  bool chance = false;
  /** He has reacted (whether or not he could reach its line), and what
   * came of it. */
  bool reacted = false;
  ShotLegOutcome outcome = ShotLegOutcome::LEG_OUT;
  /** Counted as gone through his torso (the check). */
  bool passedThrough = false;
  /** He put a leg across its line: which, from when (s), how long it takes
   * to get out to `aim` (s), and how far out it had got when last met
   * (0-1). */
  bool blocking = false;
  std::size_t leg = 0;
  double legFrom = 0.0;
  float legSeconds = 0.0f;
  float outShare = 0.0f;
  /** Where he aimed his ankle (from his centre; the error included), the
   * way the foot points, how far the line passed from his centre (m), the
   * error's radius and the miss rolled within it (m), and his spare time
   * (s; less than none: the ball beats his leg). */
  Point3F aim;
  Vector2F footFacing{1.0f, 0.0f};
  float lineGap = 0.0f;
  /** The ball's height there (m, its centre). */
  float lineHeight = 0.0f;
  float errorRadius = 0.0f;
  float error = 0.0f;
  float spareSeconds = 0.0f;
  /** When the ball gets to the nearest point of its line to him (ball
   * clock, s). */
  double ballThere = 0.0;
  /** Why he did not go, when he reacted but did not ("" when he did). */
  const char* noBlock = "";
};

/** What of him a shot met (shot block plan). */
enum class ShotBodyPart : std::uint8_t
{
  TORSO,
  THIGH,
  SHIN,
  FOOT,
  /** The upright body above the top of his shoulders (his head line). */
  HEAD,
  /** A keeper's gloved hand and his arm (goalkeeper plan step 2). */
  HAND,
  ARM,
  COUNT,
};

/** A thigh, shin or foot. */
constexpr bool isLegPart(ShotBodyPart part)
{
  return part == ShotBodyPart::THIGH || part == ShotBodyPart::SHIN ||
         part == ShotBodyPart::FOOT;
}

/** Where a shot meets his body (shot block plan §3c-3): how far along the
 * ball's stretch (0-1), the part, the surface's outward normal there (unit,
 * metres), how fast that surface is moving (m/s: his own velocity, and a
 * leg put out still going out), and whether it is the foot of a leg he put
 * out (it may aim a little). */
struct ShotBodyHit
{
  float share = 0.0f;
  ShotBodyPart part = ShotBodyPart::TORSO;
  Point3F normal{0.0f, 0.0f, 1.0f};
  Point3F surfaceVelocity{};
  bool blockFoot = false;
  /** A keeper's hand (goalkeeper plan step 4): his other hand on it too,
   * and how far he is stretched (0 at his chest, 1 at his longest reach). */
  bool bothHands = false;
  float stretch = 0.0f;
  /** A parry: the hand's face turned away from goal (Handling). */
  bool parryAim = false;
};

/** Where a ball's stretch first comes within a keeper's arms in the air
 * (goalkeeper plan step 7): how far along (0-1), how far from his centre
 * over the ground (m), whether both hands reach it, his stretch (0-1), the
 * middle of his shoulders (metres, z up) and the highest his hands reach
 * there (m). */
struct KeeperReach
{
  float along = 0.0f;
  /** Where on the stretch it is first within both arms, if it is. */
  std::optional<float> bothAlong;
  float gap = 0.0f;
  bool bothHands = false;
  float stretch = 0.0f;
  Point3F shoulders{};
  float handTop = 0.0f;
};

/** One contact the body is asked for (unified kick plan): a touch, with
 * the touch profile and his First Touch, or a strike of kind `strike`,
 * with its profile and his skill for it. What he means: the direction
 * seen from above (metric, unit), the pace over the ground and upward
 * (m/s). */
struct ContactRequest
{
  enum class Kind : std::uint8_t
  {
    FIRST_TOUCH,
    DRIBBLE_TOUCH,
    /** A first touch with his chest or thigh (chest and thigh control plan
     * steps 3 and 4). */
    CHEST_TOUCH,
    THIGH_TOUCH,
    STRIKE,
    /** A foot through the ball in a block (tackling plan step 4): the
     * touch profile, carried out with his Tackling. */
    BLOCK,
  };
  Kind kind = Kind::FIRST_TOUCH;
  StrikeKind strike = StrikeKind::GROUND_PASS;
  Vector2F aim{1.0f, 0.0f};
  float speed = 0.0f;
  float lift = 0.0f;
  /** How little he expected it (0-1, the brain's read; 0 for his own ball
   * or one he chose to strike). */
  float surprise = 0.0f;
  /** How pressed he is (0-1, the brain's read); without it the body reads
   * it from the profile's pressure radius. */
  std::optional<float> pressure;
  /** Scales a strike's angle error (a shot's spread). */
  float precision = 1.0f;
  /** The spin he means (rad/s; a strike), and the most he can put on
   * (rad/s, his skill's). */
  BallSpin spin{};
  float spinCapacity = 0.0f;
  /** A strike: his power for it (m/s; 0, the body's for the kind). */
  float powerCapacity = 0.0f;
  /** The least pace (m/s, in three dimensions) it can leave him with that
   * way (a cushioned header); 0, none. Asking near it is the weight job at
   * its hardest, and the error can't take it below. */
  float paceFloor = 0.0f;
  /** The most pace (m/s, in three dimensions) it can leave him with (a
   * chest touch: the pace it came with, as it adds none); 0, no limit. */
  float paceCeiling = 0.0f;
};

/** A contact assessed and carried out, and the skill it was carried out
 * with. */
struct ContactResult
{
  ContactAssessment contact;
  ContactOutcome outcome;
  float skill = 0.0f;
};

/**
 * The ball.`position` is normalised; `z` is the height in length units
 * (z * MatchTuning::Units::BALL_Z_METRES = metres) for renderers. Velocities
 * are in m/s and `spin` in rad/s (BallSpin).
 */
/** How a keeper lets go of a ball in his hands (goalkeeper plan step 5). */
enum class HeldRelease : std::uint8_t
{
  NONE,
  ROLL,
  OVERARM,
  PUNT,
};

struct MatchBall
{
  Vector2F position{MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE};
  float z = 0.0f;
  Vector2F velocity{0.0f, 0.0f};
  float velocityZ = 0.0f;
  BallSpin spin{};

  /** In its owner's hands, a Binturong keeper's (goalkeeper plan step 5):
   * where it was against him when he took it (metres from his centre, z
   * up) and when (simulated s), to bring it to his chest; and, as he lets
   * it go, how. The possession helpers clear it. */
  bool inKeeperHands = false;
  Point3F heldFrom{};
  double heldSince = 0.0;
  HeldRelease heldRelease = HeldRelease::NONE;

  const Player* possessedBy = nullptr;
  /** Owned but rolling free (a claimed ball): a first touch has played it,
   * or a Binturong body's outfielder is dribbling it, never tethered (first-
   * touch and dribble touches plans). Only ever set with an owner in open
   * play; the possession helpers clear it. */
  bool inTouch = false;
  /** A claimed ball he has collected (reached and moving with him): from
   * then on he dribbles it, touch by touch. */
  bool touchCollected = false;
  /** While dribbling: where he means to carry it (his plan's target, kept
   * from the last plan refresh, as the chase overrides his movement), and
   * the time until he may touch it again (s). */
  Vector2F dribbleGoal{MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE};
  float ownTouchCooldown = 0.0f;
  /** His next meet with his ball (dribble meet plan, Binturong body),
   * replanned every step: whose plan it is (none: he chases it), where on
   * its roll he plays it and how long until it is there (s), where that
   * touch sends it (metric unit) and how far (m), where his run to it
   * began (his last touch), and the pace he carries it at (m/s). */
  const Player* meetFor = nullptr;
  /** His last touch went wrong (angle or weight): he runs straight to it
   * to collect it, no corner, until his next touch. */
  bool meetRecovering = false;
  /** His last touch knocked it on into space: he sprints after it at his
   * full pace, until his next touch. */
  bool knockedOn = false;
  /** Until he means to play it again (s, counting down; dribble meet plan
   * 3d), and the way his last touch sent it (metric unit): before then he
   * plays it only if it needs it. */
  float plannedTouchIn = 0.0f;
  Vector2F pushDirection{1.0f, 0.0f};
  float meetPace = 0.0f;
  Vector2F meetPoint{};
  float meetSeconds = 0.0f;
  Vector2F meetOut{1.0f, 0.0f};
  float meetPush = 0.0f;
  Vector2F meetLegStart{};
  /** Tackling plan step 6: his touch was a win-it nudge off this
   * attacker; until he collects it he runs to put himself between them. */
  const Player* nudgedBy = nullptr;
  const Player* nudgedFrom = nullptr;
  /** While in a touch: the team-mate whose pass he is receiving, credited
   * with the completion only once the ball is collected. */
  const Player* touchPasser = nullptr;
  /** While in a touch: the gap (m) to the owner at the last check, and the
   * largest gap so far. */
  float touchGap = 0.0f;
  float touchPeakGap = 0.0f;
  /** Where the last contact sent it off from (normalised; for drills and
   * reports, read by nothing in play). */
  Vector2F sentFrom{};
  /** How high it was there (m; the same: for drills and reports). */
  float sentHeightMetres = 0.0f;
  /** The current touch was off his head, chest or thigh: he collects it
   * only once it is down at his feet. */
  bool touchDropping = false;
  /** The current touch: what was tried, and how far from his centre (m)
   * he met the ball. */
  TouchIntent::Kind touchKind = TouchIntent::Kind::SET;
  float touchContactGap = 0.0f;
  float touchContactHeight = 0.0f;
  /** How the current touch went (TouchResult). */
  float touchAngleError = 0.0f;
  float touchWeightError = 0.0f;
  float touchAngleDifficulty = 0.0f;
  float touchWeightDifficulty = 0.0f;
  bool touchAngleFailed = false;
  bool touchWeightFailed = false;
  float touchTurn = 0.0f;
  bool touchOutward = false;
  float touchPaceChange = 0.0f;
  /** The pace it left the current touch at, along the ground (m/s). */
  float touchLaunchSpeed = 0.0f;
  const Player* lastPossessor = nullptr;
  const Player* intendedReceiver = nullptr;
  /** The player who last kicked or deflected the ball. */
  const Player* kicker = nullptr;
  /** Seconds during which the kicker cannot touch the ball again. */
  float kickerLockout = 0.0f;
  /** Players (by slot bit) who already tried to reach this ball in flight. */
  std::uint32_t touchAttempts = 0;
  /** When its path last changed (a touch, kick or deflection; engine
   * seconds): a feet first touch needs it COMMIT_SECONDS old (tackling plan,
   * the commit rule). And the players (by slot bit) whose touch it has
   * already left too soon on this path. */
  double pathSince = 0.0;
  std::uint32_t commitMisses = 0;
  /** Collision plan step 6: the fallen player it last bounced off, his last
   * touch for restarts only (not a pass, possession or a deliberate play);
   * it stands while the path and last possessor it left are unchanged. */
  const Player* deflectedBy = nullptr;
  double deflectedPathSince = 0.0;
  const Player* deflectedAfter = nullptr;
  /** Players (by slot bit) letting this loose ball come closer before they
   * touch it (first-touch timing). */
  std::uint32_t touchWaiting = 0;
  /** While dribbled: metres ahead of the carrier's feet and the metric
   * direction of the last touch. */
  float dribbleExposure = 0.0f;
  float dribbleTouchLength = 0.0f;
  Vector2F dribbleDirection{1.0f, 0.0f};
  float touchTimer = 0.0f;

  bool isPass = false;
  /** Where the pass in flight is heading (the kick's real target, as its
   * flight shows it). */
  Vector2F passTarget{MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE};
  bool passByHome = false;
  bool passWasOffside = false;
  bool isShot = false;
  bool shotByHome = false;
  /** Whether the shot's flight crosses the goal line inside the frame. */
  bool shotOnTarget = false;
  float shotXG = 0.0f;
  /** Predicted goal-line crossing of the current shot (y and metres high). */
  float shotTargetY = MatchTuning::Pitch::CENTRE;
  float shotTargetHeightMetres = 0.0f;
  float shotElapsedSeconds = 0.0f;
  bool shotIsHeader = false;
  bool shotIsPenalty = false;
  bool shotFromSetPiece = false;
  bool shotSaveResolved = false;
  /** An opponent's body has blocked this shot already (shot block plan
   * step 4: one blocked shot per shot; after, a deflection). */
  bool shotBlocked = false;
  /** This shot is a direct free kick (counted for the lab). */
  bool shotFromDirectFreeKick = false;
  /** Lofted delivery (cross, corner, long ball) that can be contested. */
  bool isAerialDelivery = false;
  /** Thrown in and untouched since: it cannot score directly. */
  bool fromThrowIn = false;
  /** Thrown in from behind the touchline (by this side) and not yet on the
   * pitch: in play once any of it is over the line, taken again if it never
   * gets there (Binturong out of play). */
  bool throwEntering = false;
  bool throwByHome = false;
  /** A shot that hit the woodwork (Binturong out of play): a goal from it,
   * untouched since, is still the shot's (a penalty, a header, its xG). The
   * path it went on from the woodwork, and who had touched it then. */
  bool shotOffWoodwork = false;
  double woodworkPathSince = 0.0;
  const Player* woodworkLastPossessor = nullptr;
  const Player* woodworkDeflectedBy = nullptr;
  /** Its centre went over a goal line inside the frame (1 the right goal,
   * -1 the left) and has not come back out: a goal once the whole ball is
   * over (Binturong out of play). */
  std::int8_t inGoalMouth = 0;
};

/** Binturong out of play: the ball after it has gone out (a throw-in, goal
 * kick or corner), until the restart taker has it on the spot. */
struct DeadBallReturn
{
  enum class Phase : std::uint8_t
  {
    /** Moving (or resting) under physics: off the boards, the walls, the
     * nets, a throw back. */
    LOOSE,
    /** Caught by a fan in the rows the views show; thrown back soon. */
    CAUGHT,
    /** Gone beyond the rows shown; a replacement comes back after a pause. */
    OUT_OF_SIGHT,
    /** In the taker's hands, on its way to the spot. */
    CARRIED,
    /** A throw-in: on his spot, he lifts it over his head. */
    LIFTED,
  };
  bool active = false;
  Phase phase = Phase::LOOSE;
  /** Where the restart is taken from. */
  Vector2F spot{};
  /** Seconds in this phase, behind the boards, and in all. */
  float seconds = 0.0f;
  float gapSeconds = 0.0f;
  float totalSeconds = 0.0f;
  /** Out of sight: the front-row spot a replacement is thrown from. */
  Vector2F returnFrom{};
  /** A free kick or penalty: the fouled side's player nearest the ball
   * goes to it and plays it to stop on the spot (slot index), then goes
   * back to his restart position. */
  std::optional<std::size_t> fetcherIndex;
  Vector2F fetcherTarget{};
  /** Played towards the spot: the taker lets it roll, sets it on the spot
   * if it stops near enough, and fetches it if it drifts. */
  bool passedToSpot = false;
  /** After a goal: the fetcher (the conceding keeper) picks it out of the
   * net, carries it out and drop-kicks it at the spot (the centre). */
  bool dropKick = false;
};

/** Whether everyone is ready for a restart, the ball back on its spot
 * (Binturong out of play). */
struct RestartReadiness
{
  /** Per slot: his last distance from his restart position, and how long
   * he has been no closer to it. */
  std::array<float, 32> lastGap{};
  std::array<float, 32> stalledSeconds{};
  /** Since the ball was back on the spot; since everyone was ready. */
  float waitSeconds = 0.0f;
  float readySeconds = 0.0f;
  /** Everyone ready, and the taker's look up before he goes. */
  bool ready = false;
  float pauseSeconds = 0.0f;
};

struct MatchStats
{
  int homeShots = 0;
  int awayShots = 0;
  int homeOnTarget = 0;
  int awayOnTarget = 0;
  int homeCorners = 0;
  int awayCorners = 0;
  int homeFouls = 0;
  int awayFouls = 0;
  int homeSaves = 0;
  int awaySaves = 0;
  int homePassesAttempted = 0;
  int awayPassesAttempted = 0;
  int homePassesCompleted = 0;
  int awayPassesCompleted = 0;
  int homeProgressivePasses = 0;
  int awayProgressivePasses = 0;
  int homeThroughBalls = 0;
  int awayThroughBalls = 0;
  int homeCrosses = 0;
  int awayCrosses = 0;
  int homeCutbacks = 0;
  int awayCutbacks = 0;
  int homeSwitchesOfPlay = 0;
  int awaySwitchesOfPlay = 0;
  int homeTackles = 0;
  int awayTackles = 0;
  int homeOffsides = 0;
  int awayOffsides = 0;
  int homeYellowCards = 0;
  int awayYellowCards = 0;
  int homeRedCards = 0;
  int awayRedCards = 0;
  int homeTackleAttempts = 0;
  int awayTackleAttempts = 0;
  int homeInjuries = 0;
  int awayInjuries = 0;
  int homePenalties = 0;
  int awayPenalties = 0;
  int homeSubstitutions = 0;
  int awaySubstitutions = 0;
  int homeAerialDuelsWon = 0;
  int awayAerialDuelsWon = 0;
  /** Aerial duels with both sides involved (each won by one side above). */
  int aerialDuelsContested = 0;
  /** Header sequences (aerial challenge plan step 4): headers one after
   * another until someone has the ball or play stops. How many, the
   * headers in them, those of three or more, the longest; how many ended
   * with someone having it, and of those how many with the side that
   * headed it last. */
  int headerSequences = 0;
  int headerSequenceHeaders = 0;
  int headerSequencesLong = 0;
  int headerSequenceLongest = 0;
  int headerSequencesToControl = 0;
  int headerSequencesKept = 0;
  /** Take-ons by the carrier's side, and those that beat the defender. */
  int homeTakeOns = 0;
  int awayTakeOns = 0;
  int homeTakeOnsWon = 0;
  int awayTakeOnsWon = 0;
  /** Tackles by the tackler's side that won the duel roll, fouls included
   * (homeTackles counts only the clean ones). */
  int homeTackleDuelsWon = 0;
  int awayTackleDuelsWon = 0;
  /** Tackles on a free ball (tackling plan step 1): an opponent's ball
   * outside his reach played by the tackler's side, counted in the tackle
   * attempts too; won once that side next has the ball under control. */
  int homeFreeBallTackles = 0;
  int awayFreeBallTackles = 0;
  int homeFreeBallTacklesWon = 0;
  int awayFreeBallTacklesWon = 0;
  /** Contest tackles (tackling plan step 2): the tackler's touch on the
   * carrier's ball inside both reaches; counted in the tackle attempts and
   * settled like a free-ball tackle. */
  int homeContestTackles = 0;
  int awayContestTackles = 0;
  int homeContestTacklesWon = 0;
  int awayContestTacklesWon = 0;
  /** Swings at nothing in a contest, by the swinger's side: a carrier
   * whose ball the tackler played first, and a tackler whose ball the
   * carrier played first. */
  int homeCarrierAirSwings = 0;
  int awayCarrierAirSwings = 0;
  int homeTacklerAirSwings = 0;
  int awayTacklerAirSwings = 0;
  /** Feet first touches missed because the ball's path had changed too
   * recently to commit to (the commit rule), by the player's side. */
  int homeCommitMisses = 0;
  int awayCommitMisses = 0;
  /** Blocks (tackling plan step 4), by the tackler's side; those where
   * the carrier's foot was planned (row 3) rather than reacting (row 5),
   * and those that popped the ball up. */
  int homeBlocks = 0;
  int awayBlocks = 0;
  int blocksCommitted = 0;
  int blocksPoppedUp = 0;
  /** Open-play shots blocked by a Binturong body, by what they met
   * (ShotBodyPart: torso, thigh, shin, foot). */
  std::array<int, static_cast<std::size_t>(ShotBodyPart::COUNT)> shotBlocksByPart{};
  /** Shots meeting the defending keeper's body, arms or hands in his area
   * (goalkeeper plan steps 1 and 2), by what they met. */
  std::array<int, static_cast<std::size_t>(ShotBodyPart::COUNT)> keeperBodyHitsByPart{};
  /** Of those, still going in afterwards. */
  int keeperBodyStillOnGoal = 0;
  /** Shots a keeper's hands went for once he had reacted (goalkeeper plan
   * step 2); of those, both hands together, and the ball there before his
   * hand could be. */
  int keeperHandsWent = 0;
  int keeperHandsTogether = 0;
  int keeperHandsLate = 0;
  /** Shots a keeper dived for (goalkeeper plan step 3); of those, the ball
   * there before his full stretch. */
  int keeperDives = 0;
  int keeperDivesLate = 0;
  /** Shots off a keeper's hand (goalkeeper plan step 4): both hands on it,
   * of those within his Handling limit (a catch), held, fumbled; the rest
   * parried with the hand's face turned. */
  int keeperHandShots = 0;
  int keeperBothHands = 0;
  int keeperCatches = 0;
  int keeperHeld = 0;
  int keeperFumbles = 0;
  int keeperParries = 0;
  /** Balls a keeper took in his hands (goalkeeper plan step 5), and how he
   * let them go: rolled, thrown overarm, punted. */
  /** High claims (goalkeeper plan step 7): punched, and sprung off his
   * hands (parried or fumbled). Held claims are homeKeeperClaims. */
  int keeperPunches = 0;
  int keeperClaimParries = 0;
  /** Smothers (goalkeeper plan step 8): dives at a loose ball, and loose
   * balls held off his hands. */
  int keeperSmotherDives = 0;
  int keeperSmothersHeld = 0;
  int keeperBallsInHands = 0;
  int keeperRolls = 0;
  int keeperThrows = 0;
  int keeperPunts = 0;
  /** Shootout kicks taken, scored and saved (goalkeeper plan step 9; not
   * match play: counted after the match's record is put back). */
  int shootoutKicks = 0;
  int shootoutGoals = 0;
  int shootoutSaves = 0;
  /** Of those off a leg, the ones off a leg he put there (shot block plan
   * §3d: a reaction block) rather than his stride. */
  int shotReactionBlocks = 0;
  /** Shots off a Binturong body that were no block (shot block plan step
   * 4): off a team-mate, or off anyone once blocked already. */
  int shotDeflections = 0;
  /** Check (shot block plan §3f): shots whose path, against the player's
   * own move over the sub-step, went well inside a Binturong torso without
   * meeting him (one per shot and player; should be none), and of those,
   * the ones already inside at a sub-step's start. */
  /** Pass block plan: any other moving ball meeting a Binturong body it
   * wasn't played by, by part (ShotBodyPart order); of those, off a leg he
   * put out; an opponent's pass blocked; off a team-mate's or nobody's ball
   * (a deflection). */
  std::array<int, static_cast<std::size_t>(ShotBodyPart::COUNT)> ballBodyHitsByPart{};
  int ballReactionBlocks = 0;
  int passesBlocked = 0;
  int ballDeflections = 0;
  /** Of the body hits, those after his touch on it turned out no attempt
   * (the commit rule, the reach dice). */
  int ballBodyAfterNoTouch = 0;  /** Direct free kicks (Binturong body): taken, on target as struck,
   * blocked by a body, scored from. */
  int directFreeKicks = 0;
  int directFreeKicksOnTarget = 0;
  int directFreeKicksBlocked = 0;
  int directFreeKickGoals = 0;
  int shotTorsoPassThroughs = 0;
  int shotTorsoInsideAtStart = 0;
  /** Shots whose line passed within a Binturong outfielder's full reach
   * (shot block plan §3d), one per shot and defender; of those, what came
   * of it once he reacted (ShotLegOutcome; the rest: the ball got to him
   * first). */
  int shotLegChances = 0;
  std::array<int, static_cast<std::size_t>(ShotLegOutcome::COUNT)> shotLegOutcomes{};
  /** Collision plan: players running into each other (Binturong bodies),
   * and those where either was jolted by 2 m/s or more, and by 4 or more
   * (where a big hit becomes possible). */
  int collisions = 0;
  int collisionsJoltTwo = 0;
  int collisionsJoltFour = 0;
  /** Collision plan step 3: players knocked into a stumble, knocked over
   * (falls, stumbles until step 4), and of those the stumbles that slipped
   * into a fall. */
  int collisionStumbles = 0;
  int collisionFalls = 0;
  int collisionSlips = 0;
  /** Of the stumbles and falls: those where where-he-ends-up was the worse
   * measure (not how sudden), and those from a jolt under 1 m/s. */
  int collisionKnocksByEndsUp = 0;
  int collisionKnocksSmallJolt = 0;
  /** Step 4: falls that were big hits, falls that cost him the ball, and
   * player-seconds spent down (of them, in open play). Step 5: get-ups by
   * the call that started them (jumped up for the ball, in a stoppage,
   * gingerly as play ran on, or the fixed get-up of a brain without the
   * cues), and spells down of 15 s or more. */
  int collisionBigHits = 0;
  int collisionFallsLosingBall = 0;
  float collisionDownSeconds = 0.0f;
  float collisionDownPlaySeconds = 0.0f;
  int collisionGetUpsForBall = 0;
  int collisionGetUpsInStoppage = 0;
  int collisionGetUpsGingerly = 0;
  int collisionGetUpsFixed = 0;
  int collisionDownsFifteen = 0;
  /** Step 6: times the ball came off a fallen body. */
  int collisionBallOffFallen = 0;
  /** Stumbles and falls by the situation they came from (the knocked
   * player's view; the first that fits): 0 a re-hit (the same pair within
   * 1 s), 1 both at the ball (within 2 m of the contact), 2 his run aimed
   * at the other (his target within 1.5 m of him), 3 an overtake of the
   * other under way, 4 a race to the same spot, the same way, not being
   * overtaken, 5 either standing or slow (under 1 m/s), 6 paths
   * crossing or head-on (over 45 degrees apart), 7 anything else; and by
   * the knocked player's intent. */
  std::array<int, 8> collisionKnocksBySituation{};
  std::array<int, PLAYER_INTENT_COUNT> collisionKnocksByIntent{};
  /** Of those, the ones with a player chasing a loose ball in them (either
   * side's intent CLAIM_LOOSE_BALL), by the other player's intent (both
   * chasing: CLAIM_LOOSE_BALL for opponents, the last entry for team-mates),
   * and those where the knocked player was the chaser. */
  /** Every stumble and fall by the knocked player's intent (first index)
   * and the other player's (second). */
  std::array<std::array<int, PLAYER_INTENT_COUNT>, PLAYER_INTENT_COUNT>
      collisionKnocksByIntentPair{};
  std::array<int, PLAYER_INTENT_COUNT + 1> collisionChaseKnocksWith{};
  int collisionChaseKnocksOnChaser = 0;
  /** Way-round plan: a player starting to go round someone (a new one, or
   * a new side), and refreshes spent going round: chasing a loose ball,
   * moving to hold shape, offering support, recovering shape. */
  std::array<int, 4> wayRoundsStarted{};
  std::array<int, 4> wayRoundRefreshes{};
  /** Stumbles and falls between two players both moving to hold shape,
   * offer support or recover shape: by why the way round did not stop it
   * (0 one within 2 m of his spot, 1 one going round the other, 2 one saw
   * the other but found no clear side, 3 one going round someone else, 4
   * neither saw it coming, 5 one giving way to the other) and team-mates (0) or opponents (1); by each
   * one's speed just before (under 1, 1-3, 3-5, over 5 m/s: the knocked
   * one first); and by how they were moving (0 one standing, under 0.5
   * m/s, 1 the same way, 2 crossing, 3 head-on). */
  std::array<std::array<int, 2>, 6> positioningKnocksByReason{};
  std::array<std::array<int, 4>, 4> positioningKnocksBySpeed{};
  std::array<int, 4> positioningKnocksByMotion{};
  /** Giving way: plans where a player eased off for someone crossing ahead
   * of him, went round alone (head-on or the same way), and held his line
   * with the right of way. */
  int giveWaySlows = 0;
  int giveWayRounds = 0;
  int giveWayHolds = 0;
  /** Tackling plan step 5: steps a defender went into a contest (by his
   * side), those on the carrier's free foot (row 5, a misread), steps he
   * held, and the intents his brain chose. */
  int homeGoIns = 0;
  int awayGoIns = 0;
  int goInsOnFreeFoot = 0;
  int homeHolds = 0;
  int awayHolds = 0;
  int intentsWin = 0;
  int intentsClear = 0;
  int intentsControl = 0;
  /** A keeper coming for a high ball in his box, and catching it. */
  int homeKeeperClaimAttempts = 0;
  int awayKeeperClaimAttempts = 0;
  int homeKeeperClaims = 0;
  int awayKeeperClaims = 0;
  int homeHeadedShots = 0;
  int awayHeadedShots = 0;
  int homeSetPieceShots = 0;
  int awaySetPieceShots = 0;
  int homeAdvantagesPlayed = 0;
  int awayAdvantagesPlayed = 0;
  int homeShotsInsideBox = 0;
  int awayShotsInsideBox = 0;
  int homeHeadedGoals = 0;
  int awayHeadedGoals = 0;
  int homeSetPieceGoals = 0;
  int awaySetPieceGoals = 0;
  int homePenaltyGoals = 0;
  int awayPenaltyGoals = 0;
  float homePossession = MatchTuning::Statistics::EVEN_POSSESSION_PERCENT;
  float awayPossession = MatchTuning::Statistics::EVEN_POSSESSION_PERCENT;
  float homeShotXG = 0.0f;
  float awayShotXG = 0.0f;
  /** Clock minutes with the ball in play (restarts excluded). */
  float ballInPlayMinutes = 0.0f;
};

/** Touchline instructions a manager can shout during play. */
enum class MatchShout : std::uint8_t
{
  PUSH_HIGHER,
  DROP_DEEPER,
  PRESS_MORE,
  CALM_DOWN,
  ENCOURAGE,
  WORK_BALL_INTO_BOX,
  SHOOT_ON_SIGHT,
  STAND_OFF,
  DEMAND_MORE,
  HIT_ON_COUNTER,
  KEEP_POSSESSION
};

/** Kind of an external tactical change (see MatchCommandRecord). */
enum class MatchCommandType : std::uint8_t
{
  STRATEGY,
  SHOUT,
  FORMATION,
  MOVE_TO_SLOT,
  SUBSTITUTION,
  /** A team talk's modifier for one half (see setTeamTalkModifier). */
  TEAM_TALK,
  /** Drills: hold (or release) a player's movement target. */
  DRILL_TARGET,
  /** Drills: the player shoots as soon as he has the ball and can act. */
  FORCE_SHOT,
  /** Drills: the ball is played in (a pass to a player, or loose). */
  FEED_BALL,
  /** Drills: the player keeps the ball, deciding nothing with it (or is
   * released to decide again). */
  KEEP_BALL,
  /** Drills: the player passes to a team-mate as soon as he has the ball
   * and can act. */
  FORCE_PASS,
  /** Drills: what the player does with the next header he wins. */
  FORCE_HEADER,
  /** Drills: what the player means by a tackle on a free ball. */
  FORCE_TACKLE_INTENT
};

/** What a tackle means to do, on a free ball (tackling plan step 3) or in
 * a block (step 4). */
enum class TackleIntent : std::uint8_t
{
  /** Bring it under control (his first touch, as before). */
  CONTROL,
  /** Nudge it away from the attacker, to step across and win it. */
  WIN,
  /** Clear it, struck where he meets it. */
  CLEAR,
  /** Pass it first time to a team-mate. */
  PASS,
  /** Drills: no forcing; his brain chooses (tackling plan step 5). */
  BRAIN,
};

/** What a drill's forced header does (forceHeader). */
enum class ForcedHeaderAim : std::uint8_t
{
  /** Headed on to a spot on the ground (a knock-down to it). */
  SPOT,
  /** Headed at a spot in the goal (a headed shot). */
  GOAL,
  /** Cushioned down and kept. */
  KEEP,
};

/** Which part of his head meets a high ball (heading technique plan step
 * 4): the forehead, or the top of the head (above the forehead's reach, or
 * a ball passing over him from beside or behind). */
enum class HeadPart : std::uint8_t
{
  FOREHEAD,
  TOP,
};

/** How a drill's forced pass is played: as the AI would (ground or lofted
 * by its length), or always along the ground, or always lofted. */
enum class ForcedPassStyle : std::uint8_t
{
  AS_CHOSEN,
  GROUND,
  LOFTED,
  /** Low and hard (PassTechnique::DRIVEN). */
  DRIVEN,
  /** Steep, over someone close (PassTechnique::CHIP). */
  CHIP,
};

/** How a pass is struck (Binturong body; the Aardvark one plays a driven
 * pass along the ground and a chip as a lofted pass). */
enum class PassTechnique : std::uint8_t
{
  /** Side-foot, along the ground, to arrive at a pace he can control. */
  GROUND,
  /** Instep, to drop at the target (chest height to a team-mate). */
  LOFTED,
  /** Instep, low and hard: down at the target, a long one in a flat arc. */
  DRIVEN,
  /** Stabbed up steeply with backspin, clearing someone close. */
  CHIP,
};

/** How a foot shot is struck (shooting technique plan step 5). Without
 * one asked: driven, as every shot was. */
enum class ShotTechnique : std::uint8_t
{
  /** The instep, hard and low, with a little topspin. */
  DRIVEN,
  /** The side of the foot, at a lower pace, for the line. */
  PLACED,
  /** The instep's wrap, with sidespin, bent in towards the middle. */
  CURLED,
  /** Stabbed under it with backspin, over the keeper. */
  CHIP,
  /** The toe: quick, with little control. */
  TOE_POKE,
};

/**
 * A tactical change made from outside the engine (the touchline), stamped
 * with the fixed step it was made after. Only the fields of its type are
 * used.
 */
struct MatchCommandRecord
{
  /** Value of MatchEngine::getSimulatedSteps() when the change was made. */
  std::uint64_t step = 0;
  MatchCommandType type = MatchCommandType::STRATEGY;
  bool homeTeam = true;
  Strategy strategy;
  MatchShout shout = MatchShout::ENCOURAGE;
  /** Outfield slot positions in lineup coordinates (FORMATION). */
  std::vector<Vector2F> formation;
  /** Player moved (MOVE_TO_SLOT) or replaced (SUBSTITUTION). */
  PlayerID player = 0;
  /** Bench player coming on (SUBSTITUTION). */
  PlayerID incoming = 0;
  /** Target slot (MOVE_TO_SLOT, optional for SUBSTITUTION). */
  std::optional<std::size_t> slot;
  /** Half (1 or 2) and modifier of a TEAM_TALK. */
  int talkHalf = 1;
  float talkModifier = 0.0f;
  /** DRILL_TARGET: the target (normalised pitch; none releases it) and
   * whether he runs at it urgently. KEEP_BALL: `urgent` on keeps it, off
   * releases him. */
  std::optional<Vector2F> target;
  bool urgent = false;
  /** DRILL_TARGET: up to two waypoints on the way to `target`, and his
   * line through them (MatchPlayer::routeApexOffset, routeOutwardRoom). */
  std::vector<Vector2F> route;
  float apexOffset = 0.0f;
  float outwardRoom = 0.0f;
  /** DRILL_TARGET: a body orientation asked of him (MatchPlayer::
   * facingRequest), if any. */
  std::optional<float> facing;
  /** DRILL_TARGET: he means to reach the target at full speed (running
   * through it), not to stop on it. */
  bool atSpeed = false;
  /** FEED_BALL: played from `target` with this velocity along the ground
   * (m/s) and this upward speed (m/s), to `player` (0: loose). */
  Vector2F feedVelocity{0.0f, 0.0f};
  float feedLift = 0.0f;
  /** FORCE_PASS: `player` passes to `incoming`, played this way. With
   * no `incoming`, to the spot `target`. */
  ForcedPassStyle passStyle = ForcedPassStyle::AS_CHOSEN;
  /** FORCE_PASS to a spot: the spin he means (PassOption::spin), and the
   * pace it is to arrive at (PassOption::arrivalSpeed). */
  std::optional<BallSpin> passSpin;
  std::optional<float> passArrival;
  /** FORCE_PASS: played first time, off a pass to him (forcePassTo). */
  bool passFirstTime = false;
  /** FORCE_SHOT: the spot in the goal he strikes at, and the share of his
   * power (forceShot), if any. */
  std::optional<Vector2F> shotAim;
  std::optional<float> shotPower;
  /** FORCE_SHOT: struck first time off a pass to him, and how (forceShot). */
  bool shotFirstTime = false;
  std::optional<ShotTechnique> shotTechnique;
  /** FORCE_HEADER: what he does with it; to the spot `target` (SPOT) or
   * at the spot in the goal `shotAim` (GOAL). */
  ForcedHeaderAim headerAim = ForcedHeaderAim::SPOT;
  /** FORCE_HEADER: he knows the header before the ball comes (and turns
   * his body to it), or only as he meets it. */
  bool headerKnownEarly = true;
  /** KEEP_BALL: how hard he carries it (older recordings: flat out). */
  float keepUrgency = 1.0f;
  /** FORCE_TACKLE_INTENT: what his free-ball tackles mean to do. */
  TackleIntent tackleIntent = TackleIntent::CONTROL;
};

/** A substitution under way (touchline plan stage 5): the sub waits on the
 * touchline while the player going off walks off at the nearest point;
 * once that player is wholly over the line the sub takes his slot and
 * walks on, and play restarts once he is on. */
struct PendingSubstitution
{
  enum class Stage : std::uint8_t
  {
    /** Asked for in play: the player plays on until the next stoppage. */
    WAITING,
    LEAVING,
    ENTERING
  };
  Stage stage = Stage::WAITING;
  /** The slot: the player going off, then the sub coming on. */
  std::size_t slot = 0;
  const Player* incoming = nullptr;
  SubstitutionReason reason = SubstitutionReason::MANUAL;
  /** Where the player going off leaves the pitch (metres, MatchGround's
   * frame): the nearest point of the boundary, just over it. */
  Vector2F exit{};
  /** The manager's move of the sub to another slot, done once he is on. */
  std::optional<MatchCommandRecord> moveToSlot;
  /** How long it has held up play (s), for the safety net. */
  float seconds = 0.0f;
};

/** One-shot action requested by an external controller (play mode). */
enum class MatchInputAction : std::uint8_t
{
  NONE,
  PASS,
  LOFTED_PASS,
  SHOOT,
  CLEAR,
  TACKLE,
  SLIDE_TACKLE,
  /** A ground pass into the space ahead of a team-mate's run. */
  THROUGH_BALL
};

/**
 * Controller state for the externally controlled player, e.g. a human's pad
 * in play mode. Directions are in pitch metres (x along the length toward
 * x=105, y across toward y=68); only the direction and a magnitude up to 1
 * matter.
 *
 * Shots and passes go through the same execution model as the AI's (the
 * player's technique, pressure, distance and fatigue decide the error), so
 * the stick only chooses where the player tries to play the ball.
 */
/**
 * Movement jitter of one side's outfield players in open play, observed
 * only (it changes nothing in the match): how the plan's targets and
 * intents change from one refresh to the next, and how the runs follow.
 */
struct MovementJitterStats
{
  /** Scheduled refreshes (every 0.2 s) that nothing forced, by the intent
   * planned. */
  struct ByIntent
  {
    /** Player-refreshes ending in this intent. */
    std::uint32_t refreshes = 0;
    /** How far the target moved at those refreshes, in total (m). */
    double targetMoveMetres = 0.0;
    /** Refreshes that moved the target more than half a metre, and those
     * that also turned back on the previous refresh's move. */
    std::uint32_t moves = 0;
    std::uint32_t reversals = 0;
    /** Switches into this intent, and those back to the intent held within
     * the last second. */
    std::uint32_t switchesIn = 0;
    std::uint32_t flipBacks = 0;
    /** Speed surging on this job: running time (s) and reversals (see
     * speedReversals). */
    double runningSeconds = 0.0;
    std::uint32_t speedReversals = 0;
  };
  std::array<ByIntent, PLAYER_INTENT_COUNT> byIntent{};
  /** Refreshes forced (even on a scheduled step) by the ball changing hands
   * or starting or ending a flight, and how far they moved the targets (m).
   */
  std::uint32_t eventRefreshes = 0;
  double eventTargetMoveMetres = 0.0;
  /** Outfield time in open play (player-seconds). */
  double playerSeconds = 0.0;
  /** Over 2 s windows: distance run and distance actually gained (m). */
  double pathMetres = 0.0;
  double netMetres = 0.0;
  /** Velocity compared every 0.2 s while running above 1 m/s, and the
   * times it turned more than 90 degrees. */
  std::uint32_t turnSamples = 0;
  std::uint32_t sharpTurns = 0;
  /** Speed surging: time spent running above 2 m/s (s), and the times his
   * speed turned from rising to falling or back (each change over 0.5
   * m/s^2 in a step). */
  double runningSeconds = 0.0;
  std::uint32_t speedReversals = 0;
};

/**
 * Running by 15-minute spell of the clock (0-15, 15-30, 30-45 and added
 * time, then the same for the second half; extra time is left out), for one
 * side's outfield players, observed only.
 */
struct LoadProfileStats
{
  static constexpr std::size_t SPELLS = 6;
  /** Distance, and distance above the high-intensity speed (m). */
  std::array<double, SPELLS> metres{};
  std::array<double, SPELLS> highIntensityMetres{};
  /** Outfield time on the pitch (player-seconds). */
  std::array<double, SPELLS> playerSeconds{};
};

struct MatchPlayerInput
{
  /** Stick: run direction, magnitude 0..1 of the run speed (0 = stand). */
  float moveX = 0.0f;
  float moveY = 0.0f;
  /** Sprint button: run at top speed instead of the jog share. */
  bool sprint = false;
  /**
   * Action button, performed at the first step it is possible (carrying the
   * ball for passes, shots and clearances; near the opposing carrier for
   * tackles) or dropped after MatchTuning::Control::ACTION_BUFFER_SECONDS.
   * It fires on the press: holding the same action in later inputs does not
   * repeat it; release (NONE) or a different action re-arms.
   */
  MatchInputAction action = MatchInputAction::NONE;
  /** Pass aim direction; zero aims along the stick (or the facing). */
  float aimX = 0.0f;
  float aimY = 0.0f;
  /**
   * Strength of the action in [0, 1] from how long its button was held: the
   * shot's pace (a full bar also lifts and widens it), or how far ahead a
   * through ball or a pass into space is played. 0 lets the player choose.
   */
  float power = 0.0f;
  /**
   * Jockey (held): the player stays on his feet facing the ball at a contain
   * pace; with the stick idle he holds a goal-side line on the carrier.
   */
  bool jockey = false;
  /**
   * Pass assistance: 0 plays the pass along the stick, 1 (default) leans
   * toward the best placed team-mate near the aim, 2 picks him in a wide
   * cone.
   */
  std::uint8_t passAssist = 1;
};

/** A controller change, stamped with the fixed step it takes effect on. */
struct MatchInputRecord
{
  /** Fixed step (see MatchEngine::getSimulatedSteps()) that applies it. */
  std::uint64_t step = 0;
  /** Controlled player from this step; 0 hands the player back to the AI. */
  PlayerID player = 0;
  MatchPlayerInput input;
  /** Real minutes per half in Play; 0 preserves the watch clock. */
  int playHalfMinutes = 0;
};

/** Simulation detail of a headless match (see MatchEngine::simulateToEnd). */
enum class MatchFidelity : std::uint8_t
{
  /** The live engine's 10 Hz fixed step: watched matches and quick results. */
  FULL,
  /**
   * Unwatched background fixtures: live play is identical to FULL, but the
   * walk to each restart spot is simulated in coarser steps
   * (MatchTuning::Timing::BACKGROUND_STOPPAGE_TICKS). Deterministic per seed
   * and statistically equivalent to FULL; a given seed plays a different
   * match than in FULL.
   */
  BACKGROUND
};

/** How advancePlayback() presents the match. */
enum class MatchPlaybackMode
{
  /** Every simulated second is shown at the playback speed. */
  FULL_MATCH,
  /** Only highlight windows are shown; the rest is simulated headless. */
  HIGHLIGHTS
};

/**
 * A stretch of play worth showing, in simulated seconds since kick-off (see
 * MatchEngine::getSimulatedSeconds()): the build-up before a trigger event
 * (shot, goal, penalty, card) and a short aftermath.
 */
struct MatchHighlight
{
  MatchEventType type = MatchEventType::INFO;
  double startSeconds = 0.0;
  double triggerSeconds = 0.0;
  double endSeconds = 0.0;
  /** Match clock minute of the (most important) trigger. */
  float triggerMinute = 0.0f;
};

/**
 * Stateful, deterministic-when-seeded live match simulation in real match
 * time: in watch mode one simulated second is one second of the match (a full
 * match with stoppages and added time is roughly 5,900 simulated seconds).
 * Play mode can accelerate the match clock while body/ball physics retain
 * the same timestep; see setPlayHalfMinutes().
 *
 * update() uses a fixed internal timestep, so the same seed produces the same
 * match at different render frame rates. Home attacks toward x=1 and away
 * attacks toward x=0. The engine is copyable, which highlight prediction uses
 * to look ahead deterministically.
 *
 * Strategies and simulation state are owned by value. Player pointers and
 * the StatsConfig reference are borrowed: keep them alive and unchanged for
 * the engine's lifetime, including any prediction copies and worker batches.
 * The engine records match consequences; the career applies them afterwards.
 * See docs/development/match-engine.md for the step order and extension map,
 * and docs/development/player-behavior.md for movement and action selection.
 */
class MatchEngine
{
 public:
  MatchEngine(const Lineup& home_lineup, const Lineup& away_lineup,
              const Strategy& home_strat, const Strategy& away_strat,
              const StatsConfig& config);
  /**
   * @param handshake How the inputs become match state (match_handshake.h);
   * chosen here because the conversion starts here.
   */
  MatchEngine(const Lineup& home_lineup, const Lineup& away_lineup,
              const Strategy& home_strat, const Strategy& away_strat,
              const StatsConfig& config, uint32_t seed,
              HandshakeId handshake = HandshakeId::AARDVARK);
  /** The handshake this match was built with. */
  HandshakeId getHandshake() const { return handshakeId; }

  /**
   * Live update: advances `deltaTime` simulated seconds (wall-clock seconds
   * times the viewer's speed; 1 = real time). Catch-up is bounded
   * (MAX_FRAME_DELTA_SECONDS, MAX_FIXED_STEPS_PER_UPDATE) so a stalled frame
   * never freezes the view; excess steps are dropped and counted in
   * getDroppedSimulationSteps().
   */
  void update(float deltaTime);
  /**
   * Headless: advances exactly the whole fixed steps covering `seconds` of
   * simulated time (never drops steps), stopping early at full time. Returns
   * the simulated seconds advanced.
   */
  float advance(float seconds);
  /** Headless: plays the rest of the match at full fidelity ("quick
   * result" of a match the user may have been watching). */
  void simulateToEnd();
  /**
   * Headless: plays the rest of the match at the given fidelity; background
   * fixtures nobody watches pass MatchFidelity::BACKGROUND. A controlled
   * player (play mode) always forces FULL.
   */
  void simulateToEnd(MatchFidelity fidelity);
  /** Simulated seconds since kick-off, including stoppages and half-time. */
  double getSimulatedSeconds() const;
  /** His legs as a shot last met them, player `slot` (shot block plan
   * §3c, §3d), and how long ago (s), if within `maxAgeSeconds` (the
   * sandbox's true view shows them; drills report them). */
  std::optional<std::pair<ShotLegs, float>> recentShotLegs(std::size_t slot,
                                                           float maxAgeSeconds) const;
  /** The last shot off a Binturong body as it left him (shot block plan
   * step 3), before anything after (a clear-behind): velocity (m/s, up as
   * z), spin, and when (ball clock, s; negative: none yet). For drills. */
  struct ShotBounce
  {
    Point3F velocity{};
    BallSpin spin{};
    double at = -1.0;
    /** Where it met him (the part, the surface's normal). */
    ShotBodyHit hit{};
  };
  const ShotBounce& lastShotBounce() const { return shotBounce; }

  /** Playback mode used by advancePlayback(); FULL_MATCH by default. */
  void setPlaybackMode(MatchPlaybackMode mode);
  MatchPlaybackMode getPlaybackMode() const { return playbackMode; }
  /** Simulated seconds per wall second in FULL_MATCH mode (clamped). */
  void setPlaybackSpeed(float simulatedSecondsPerWallSecond);
  float getPlaybackSpeed() const { return playbackSpeed; }
  /** Simulated seconds per wall second inside highlight windows. */
  void setHighlightPlaybackSpeed(float simulatedSecondsPerWallSecond);
  float getHighlightPlaybackSpeed() const { return highlightSpeed; }
  /**
   * Presentation helper for the live view: advances the match by
   * `wallSeconds` of wall-clock time according to the playback mode. In
   * HIGHLIGHTS mode it skips (simulates headless, never dropping steps) to
   * the start of the next predicted highlight window and then plays the
   * window at the highlight speed. Returns true when this call skipped
   * ahead, so the view can cut instead of interpolating.
   */
  bool advancePlayback(float wallSeconds);
  /** Whether the current moment lies inside a highlight window. */
  bool isInHighlight() const;
  /** The highlight window being played in HIGHLIGHTS mode, if any. */
  const std::optional<MatchHighlight>& getScheduledHighlight() const
  {
    return scheduledHighlight;
  }
  /**
   * Predicts the next highlight window after the current moment by running a
   * copy of the match up to `horizonSeconds` ahead. Deterministic: the live
   * match plays out identically unless a manual change is made first.
   */
  std::optional<MatchHighlight> predictNextHighlight(
      float horizonSeconds =
          MatchTuning::Playback::PREDICTION_HORIZON_SECONDS) const;
  /** Highlight windows of the match so far (merged when they overlap). */
  const std::vector<MatchHighlight>& getHighlights() const
  {
    return highlights;
  }
  /**
   * Tactical familiarity of a side in [0, 1] (1 = fully drilled, the
   * default). Low familiarity makes decisions noisier and positioning
   * looser.
   */
  void setTacticalFamiliarity(bool homeTeam, float familiarity);
  /** Familiarity in force: the drilled level, less a temporary loss after a
   * change of shape (setFormation). */
  float getTacticalFamiliarity(bool homeTeam) const
  {
    return effectiveFamiliarity(homeTeam ? 0 : 1);
  }
  /**
   * The brain a side decides with (see match_brain.h). Set before kickoff;
   * both sides use BrainId::AARDVARK unless told otherwise.
   */
  void setBrain(bool homeTeam, BrainId brain);
  BrainId getBrain(bool homeTeam) const { return brains[homeTeam ? 0 : 1]; }
  /**
   * The body a side's players move and play with (see match_body.h),
   * independent of its brain. Set before kickoff; Aardvark unless told
   * otherwise.
   */
  void setBody(bool homeTeam, BodyId body)
  {
    bodies[homeTeam ? 0 : 1] = body;
  }
  BodyId getBody(bool homeTeam) const { return bodies[homeTeam ? 0 : 1]; }
  /** The match's referee (see match_referee.h). Set before kickoff;
   * Aardvark unless told otherwise. */
  void setReferee(RefereeId referee) { refereeId = referee; }
  RefereeId getReferee() const { return refereeId; }
  /** What happens when the ball leaves play and how play restarts (see
   * match_out_of_play.h). Set before kickoff; Aardvark unless told
   * otherwise. */
  void setOutOfPlay(OutOfPlayId version) { outOfPlayId = version; }
  OutOfPlayId getOutOfPlay() const { return outOfPlayId; }
  /** The AI manager a side is run by when the engine manages it (see
   * match_manager.h, setAutoSubstitutions). Set before kickoff; Aardvark
   * unless told otherwise. */
  void setManager(bool homeTeam, ManagerId manager)
  {
    managers[homeTeam ? 0 : 1] = manager;
  }
  ManagerId getManager(bool homeTeam) const
  {
    return managers[homeTeam ? 0 : 1];
  }
  /** The match's ball physics (see match_ball_physics.h). Set before
   * kickoff; Aardvark unless told otherwise. */
  void setBallPhysics(BallPhysicsId physics) { ballPhysicsId = physics; }
  BallPhysicsId getBallPhysics() const { return ballPhysicsId; }
  /** Advances a flight by `dt` with the match's ball physics; returns the
   * horizontal distance travelled (metres). For predictions too, so they
   * match the real flight. */
  float stepFlight(FlightState& flight, float dt) const;
  /**
   * Replaces a side's tactics at any time; the sliders apply from the next
   * step and the players re-form over a few seconds (their target response).
   */
  void setStrategy(bool homeTeam, const Strategy& strategy);
  /**
   * A touchline shout: a temporary nudge to the side's sliders or shot
   * appetite that fades out over
   * MatchTuning::Touchline::SHOUT_DURATION_SECONDS. A new shout replaces the
   * previous one; shouts in quick succession have less effect (see
   * MatchTuning::Touchline::SHOUT_REPEAT_FADE_SECONDS).
   */
  void applyShout(bool homeTeam, MatchShout shout);
  /** Remaining strength of the side's shout in [0, 1] (0 when none). */
  float getShoutStrength(bool homeTeam) const;
  /** The shout in force, if any. */
  std::optional<MatchShout> getActiveShout(bool homeTeam) const;
  /**
   * Positions of the side's outfield slots in lineup coordinates (own goal
   * line at x = 0, as Lineup::addOutfieldPlayer), in the lineup's order; the
   * goalkeeper has no slot.
   */
  std::vector<Vector2F> getFormation(bool homeTeam) const;
  /**
   * Changes the shape: one position per slot (same size as getFormation()),
   * clamped to the pitch. The players walk to their new spots; a real change
   * of shape costs some tactical familiarity for a few minutes. Returns false
   * (and changes nothing) for a wrong size or non-finite positions.
   */
  bool setFormation(bool homeTeam, std::span<const Vector2F> shape);
  /** Slot of an outfield player on the pitch (nullopt otherwise). */
  std::optional<std::size_t> getFormationSlot(PlayerID playerId) const;
  /**
   * Moves an outfield player on the pitch to `slot`; the slot's occupant
   * takes his old one (a slot left empty by a dismissal is simply taken).
   */
  bool movePlayerToSlot(PlayerID playerId, std::size_t slot);
  /** Strategy sliders in force: the tactics plus any fading shout. */
  StrategySliders getEffectiveSliders(bool homeTeam) const;
  /**
   * Team-talk execution modifier for one half (clamped to
   * +-MatchTuning::Touchline::MAX_TEAM_TALK_MODIFIER): positive values make
   * execution and decisions slightly sharper and work rate slightly higher.
   * Logged in the command log (MatchCommandType::TEAM_TALK).
   */
  void setTeamTalkModifier(bool homeTeam, int half, float modifier);
  float getTeamTalkModifier(bool homeTeam, int half) const;
  /**
   * Team-talk effect in force for a side: the modifier of the current half
   * at full strength for the first TacticsTuning::TALK_FULL_MINUTES of the
   * half, fading to nothing by TALK_FADE_END_MINUTES (none in extra time).
   * Besides execution it sharpens decisions and pressing (or blunts them,
   * after a talk that went down badly).
   */
  float getTeamTalkEffect(bool homeTeam) const;
  /**
   * Role played by an outfield slot of a side (see getFormation), resolved
   * from the side's tactics by the slot's kick-off position.
   */
  TacticalRole getSlotRole(bool homeTeam, std::size_t slot) const;

  /**
   * Play-mode seam: hands one outfield player to an external controller from
   * the next fixed step (0 hands him back to the AI). While controlled, the
   * player moves by the stick (at MatchTuning::Control::PHYSICS_SUBSTEPS per
   * step, with the same acceleration, braking, turning and fatigue model) and
   * acts only on the action button; team-mates and opponents stay AI. Set
   * pieces and goalkeeping remain AI-driven. A controlled player who is
   * substituted, sent off or goes in goal hands control back to the AI.
   * Returns false (and changes nothing) for goalkeepers and players not on
   * the pitch.
   */
  bool setControlledPlayer(PlayerID playerId);
  /** Schedule play clock pacing independently of physics; 0 restores watch. */
  void setPlayHalfMinutes(int minutes);
  [[nodiscard]] int getPlayHalfMinutes() const { return playHalfMinutes; }
  /** The controlled player, or 0 when the AI controls everyone. */
  PlayerID getControlledPlayer() const;
  /**
   * Latest controller state for the controlled player; it holds until the
   * next call and takes effect on the next fixed step. Ignored when nobody is
   * controlled. Submitting input during a replay discards the rest of it.
   */
  void submitInput(const MatchPlayerInput& input);
  /**
   * Every controller change so far, step-stamped. Replaying it on a new
   * engine with the same lineups, tactics and seed reproduces the match.
   */
  const std::vector<MatchInputRecord>& getInputLog() const { return inputLog; }
  /** Schedules a recorded input log (records applied at their steps). */
  void loadInputReplay(std::vector<MatchInputRecord> log);
  /**
   * Play mode: the team-mate who should be the human's active footballer
   * now. The ball carrier while the side has the ball (outfield only), the
   * intended receiver of the side's pass, otherwise the player who can reach
   * the ball first along its path (the carrier's run when defending), with a
   * small preference for `current` so control does not flicker between two
   * equally placed players. 0 when the side has nobody outfield. Const and
   * random-free: asking never changes the match.
   */
  PlayerID suggestActivePlayer(bool homeTeam, PlayerID current) const;
  /**
   * The team-mate a manual switch would pick now: the best candidate by the
   * same ranking, never `current` nor `skip` (pass the previous pick so
   * repeated presses cycle). 0 when there is none.
   */
  PlayerID nextSwitchCandidate(bool homeTeam, PlayerID current,
                               PlayerID skip = 0) const;
  /**
   * Whether update(`deltaSeconds`) would simulate at least one fixed step,
   * so a controller can sample its device exactly once per step.
   */
  bool stepsWithin(float deltaSeconds) const;
  /** Play mode: whether a side was ever controlled from outside, and the
   * passes, shots, clearances and tackles its controlled players made. */
  bool wasControlled(bool homeTeam) const
  {
    return everControlled[homeTeam ? 0 : 1];
  }
  int getControlledActions(bool homeTeam) const
  {
    return controlledActions[homeTeam ? 0 : 1];
  }
  /**
   * Every tactical change made from outside (strategy, shout, formation,
   * slot move, substitution, team talk), step-stamped. Replaying it with
   * loadCommandReplay() on a new engine with the same lineups, tactics and
   * seed reproduces the match. AI touchline decisions are not logged: they
   * replay by themselves.
   */
  const std::vector<MatchCommandRecord>& getCommandLog() const
  {
    return commandLog;
  }
  /** Schedules a recorded command log; a new change made during the replay
   * discards the rest of it. */
  void loadCommandReplay(std::vector<MatchCommandRecord> log);
  /**
   * Continues a copy of a match taken earlier with the original's complete
   * logs (getCommandLog(), getInputLog()), so it replays the changes made
   * after the copy. This copy's own logs must be the start of those logs:
   * what it already applied stays applied and the rest is scheduled.
   */
  void continueReplay(std::vector<MatchCommandRecord> commands,
                      std::vector<MatchInputRecord> inputs);
  /** Fixed steps simulated since kick-off. */
  std::uint64_t getSimulatedSteps() const { return stepCounter; }

  const std::vector<MatchPlayer>& getPlayers() const { return players; }
  /** The people round the touchline (touchline plan stage 2): their
   * bodies (a sub's carries his player; staff have none) and who each is,
   * index for index. */
  const std::vector<MatchPlayer>& getTouchlineBodies() const { return touchlineBodies; }
  const std::vector<TouchlinePerson>& getTouchlinePeople() const { return touchlinePeople; }
  const std::vector<Vector2F>& getPreviousTouchlinePositions() const
  {
    return previousTouchlinePositions;
  }
  const std::vector<float>& getPreviousTouchlineFacingAngles() const
  {
    return previousTouchlineFacingAngles;
  }
  const MatchBall& getBall() const { return ball; }
  /** Seconds left of the set-piece phase (0: none). */
  float getSetPiecePhaseRemaining() const { return setPiecePhaseRemaining; }
  /** Whether the ball's ownership state is coherent: a ball in a first touch
   * has an owner, in open play. The sandbox checks this every step. */
  bool possessionConsistent() const;
  /** Stage 1: value-only legacy projection. Never changes gameplay state,
   * consumes randomness, or upgrades a claim into physical control. */
  PossessionSnapshot getPossessionSnapshot() const;
  std::uint32_t getPossessionIssues() const;
  const std::vector<MatchEvent>& getEvents() const { return events; }
  /**
   * Team names used in the commentary lines (MatchEvent::description) from
   * now on; without them the lines say "home/away side" (localised).
   */
  void setTeamNames(std::string homeTeam, std::string awayTeam);
  MatchState getState() const { return state; }
  const MatchStats& getStats() const { return stats; }
  TeamPhase getHomePhase() const { return homePhase; }
  TeamPhase getAwayPhase() const { return awayPhase; }
  float getTransitionSecondsRemaining() const
  {
    return transitionSecondsRemaining;
  }
  const PassDecision& getLastPassDecision() const { return lastPassDecision; }

  /**
   * Replaces an on-pitch player with a bench player of the same team, who
   * takes the outgoing player's slot or, if given, `slot` (its occupant then
   * moves to the vacated one). Enforces the substitution limit (5, 6 in extra
   * time) and windows (3, +1 in extra time, half-time excluded); returns
   * false when the change is not allowed.
   */
  bool substitutePlayer(uint32_t outPlayerId, const Player* inPlayer,
                        std::optional<std::size_t> slot = std::nullopt);
  /** Whether the side can still make a substitution right now. */
  bool canSubstitute(bool homeTeam) const;
  int getSubstitutionsUsed(bool homeTeam) const;
  int getSubstitutionWindowsUsed(bool homeTeam) const;
  /**
   * AI substitutions (fatigue, injuries, cards, game state) are enabled for
   * both sides by default so headless matches manage themselves. A side
   * managed by the human should disable its own.
   */
  void setAutoSubstitutions(bool home, bool away);
  /**
   * Medical staff instructions (MedicalFlag bits, see model/medical_centre.h)
   * for a squad player: while his side's substitutions are automatic, a
   * player limited to about an hour comes off at the first stoppage once he
   * has played MedicalCentre::MINUTE_LIMIT minutes on the pitch
   * (PlayerMatchStats::minutesPlayed: elapsed match-clock minutes, added
   * time included, so a starter can be due a little before 60' on the
   * second-half clock; a substitute counts from when he came on). Set
   * before kick-off; no flags plays as before.
   */
  void setMedicalFlags(PlayerID playerId, std::uint8_t flags);
  const std::vector<MatchSubstitution>& getSubstitutions() const
  {
    return substitutions;
  }

  /** Per-player statistics for everyone who took part (never truncated). */
  const std::vector<PlayerMatchStats>& getPlayerStats() const
  {
    return playerStats;
  }
  /** Running by 15-minute spell so far, home then away. */
  const std::array<LoadProfileStats, 2>& getLoadProfile() const
  {
    return loadProfile;
  }
  /** Movement jitter observed so far, home then away. */
  /** First-touch timing: how often a player who let the ball come closer
   * then lost it out of his reach untouched (should stay 0). */
  std::uint32_t getFirstTouchWaitsMissed() const { return firstTouchWaitsMissed; }
  /** Restart line-ups (a Binturong body's taker), counted as he kicks. */
  struct RestartLineUpStats
  {
    /** Kicked, by the restart (MatchState's index). */
    std::array<std::uint32_t, 7> byRestart{};
    std::uint32_t kicked = 0;
    /** Kicked turned to his line, or once out of time to turn. */
    std::uint32_t turned = 0;
    std::uint32_t outOfTime = 0;
    /** Kicked with the ball outside his comfort gap, or in his dead zone
     * (both should stay 0). */
    std::uint32_t outsideComfort = 0;
    std::uint32_t inDeadZone = 0;
    /** How long the line-ups took (s), and how far the ball was from his
     * centre as he kicked (m). */
    float secondsSum = 0.0f;
    float secondsMax = 0.0f;
    float gapSum = 0.0f;
    float gapMax = 0.0f;
  };
  const RestartLineUpStats& getRestartLineUpStats() const
  {
    return restartLineUpStats;
  }
  /** How long the taker has been lining the ball up (0: nobody is). */
  float getRestartLineUpSeconds() const
  {
    return restartLineUp ? restartLineUp->seconds : 0.0f;
  }
  /** Dribble touches taken so far, heavy ones included (for the drills),
   * and the sum of how far from his centre he met the ball for the loose
   * ones (m). */
  std::uint32_t getDribbleTouches() const { return dribbleTouches; }
  double getDribbleContactGapSum() const { return dribbleContactGapSum; }
  /** Loose dribble touches that went wrong (either job). */
  std::uint32_t getDribbleFailedTouches() const { return dribbleFailedTouches; }
  const std::array<MovementJitterStats, 2>& getMovementJitter() const
  {
    return movementJitter;
  }
  /** Open-play strikes by Binturong bodies as the contact model reads them
   * (contact plan step 2; no effect on the strikes). */
  const StrikeContactStats& getStrikeContacts() const { return strikeContacts; }
  /** Loose balls a Binturong body's outfielder played with his body (not
   * his head), by the part that met it: feet, thigh, chest (chest and thigh
   * control plan step 2). The intended receivers of a team-mate's pass
   * apart from everyone else. */
  struct BodyContactStats
  {
    std::array<std::uint32_t, 3> receiver{};
    std::array<std::uint32_t, 3> other{};
  };
  const BodyContactStats& getBodyContacts() const { return bodyContacts; }
  /** Collision plan: one player's side of a run-in. */
  struct CollisionSide
  {
    PlayerID player = 0;
    /** His sudden change of velocity (m/s, x along the length). */
    Vector2F jolt{};
    /** His velocity after it (m/s). */
    Vector2F after{};
    /** Where he ends up: his speed after, as a share of the fastest he can
     * naturally move that way against his facing. */
    float endsUp = 0.0f;
    /** How sudden: the jolt against his hardest braking over
     * SUDDEN_WINDOW_SECONDS. */
    float sudden = 0.0f;
    /** The pause it gave him (s), if he kept his feet and balance. */
    float pause = 0.0f;
    /** The judged score (the worse measure, with its wobble) and the two
     * lines it met (lowered if he was already stumbling). */
    float score = 0.0f;
    float stumbleLine = 0.0f;
    float fallLine = 0.0f;
    /** 0 kept his feet and balance, 1 stumbled, 2 fell; and whether a
     * stumble slipped into the fall. */
    std::uint8_t outcome = 0;
    bool slipped = false;
    /** His stumble's length (s), if he stumbled. */
    float stumble = 0.0f;
    /** If he fell: whether a big hit, the least time it keeps him down (s),
     * and whether he lies across his slide. */
    bool bigHit = false;
    float downAtLeast = 0.0f;
    bool lyingAcross = false;
  };
  /** One run-in: when in the step (s), where (pitch), and both sides. */
  struct CollisionHit
  {
    std::uint64_t step = 0;
    float atSeconds = 0.0f;
    Vector2F where{};
    std::array<CollisionSide, 2> sides{};
  };
  /** The run-ins of the last movement step. */
  const std::vector<CollisionHit>& getLastCollisions() const { return lastCollisions; }
  /** His lines between feet, thigh, chest and head, standing (his body's:
   * chest and thigh control plan step 1). */
  BodyLines bodyLines(const MatchPlayer& player) const;
  /** Drills: whether a pass of this player's is held for its strike (for
   * the sweep, his meet, or his turn to it). */
  bool passHeldFor(PlayerID playerId) const;
  const PlayerMatchStats* findPlayerStats(PlayerID playerId) const;
  /**
   * Touch maps, pass network, pressures and the like for the match report
   * (indexed like getPlayerStats()). On by default; background fidelity
   * and highlight look-ahead switch it off.
   */
  const MatchTracker& getTracker() const { return tracker; }
  void setTracking(bool on) { tracker.setEnabled(on); }
  /**
   * Live physical condition in [0, 1] (end-of-match condition after full
   * time) so a career simulation can carry fatigue between matches.
   */
  std::optional<float> getPlayerCondition(PlayerID playerId) const;
  /**
   * Sets a player's condition before kick-off: a starter directly, a bench
   * player when he comes on. False if the player is not in the squad or the
   * match has started.
   */
  bool setPlayerCondition(PlayerID playerId, float condition);

  /** Current period: 1 or 2, then 3 and 4 in extra time. */
  int getPeriod() const { return period; }
  /** Announced added minutes of a period 1-4 (0 until announced). */
  int getAddedMinutes(int half) const
  {
    return half >= 1 && half <= static_cast<int>(addedMinutes.size())
               ? addedMinutes[static_cast<std::size_t>(half - 1)]
               : 0;
  }
  /**
   * Makes this a knockout match (before kick-off): level at full time,
   * counting earlier legs, it goes to extra time and then to a penalty
   * shootout. League matches keep the default and may end drawn.
   */
  void setKnockout(const MatchRules::Knockout& rules);
  const MatchRules::Knockout& getKnockout() const { return knockout; }
  /**
   * Sets the league character (see MatchContext) before kick-off; ignored
   * once the match has started. Redraws the referee from the same seed.
   */
  void setMatchContext(const MatchContext& context);
  const MatchContext& getMatchContext() const { return matchContext; }
  /** True once the match has gone into extra time. */
  bool wentToExtraTime() const { return extraTimeReached; }
  /** True once a penalty shootout has started. */
  bool hasShootout() const { return shootout.started; }
  /** Shootout goals of a side (0 without a shootout). */
  int getShootoutScore(bool homeTeam) const
  {
    return shootout.goals[homeTeam ? 0 : 1];
  }
  /** Shootout kicks taken by a side. */
  int getShootoutKicks(bool homeTeam) const
  {
    return shootout.kicks[homeTeam ? 0 : 1];
  }
  /**
   * Winner of a knockout tie at full time (true = this match's home side),
   * from the aggregate score and then the shootout; empty for league matches
   * and before full time.
   */
  std::optional<bool> getTieWinnerHome() const;
  bool isInAddedTime() const;
  /** True when a side fell below seven players and the match was stopped. */
  bool isAbandoned() const { return abandoned; }
  /** Per-match referee strictness (1 is average). */
  float getRefereeStrictness() const { return refereeStrictness; }

  int getHomeScore() const { return homeScore; }
  int getAwayScore() const { return awayScore; }
  float getMatchTimeMinutes() const { return matchTimeMinutes; }
  /** Total clock minutes played so far, including both added times. */
  float getElapsedMatchMinutes() const { return elapsedMatchMinutes; }
  int getLastUpdateStepCount() const { return lastUpdateStepCount; }
  std::uint64_t getDroppedSimulationSteps() const
  {
    return droppedSimulationSteps;
  }

  /** Whether the last scored goal was conceded by the away team's keeper. */
  bool getGoalScoredByHome() const { return goalScoredByHome; }

  /** Seconds left in the goal celebration before the kick-off restart. */
  float getGoalCelebrationRemaining() const { return goalCelebrationRemaining; }
  [[nodiscard]] float getGoalCelebrationDuration() const
  {
    return playHalfMinutes > 0 ? MatchTuning::Timing::PLAY_STOPPAGE_SECONDS
                               : MatchTuning::Timing::GOAL_CELEBRATION_SECONDS;
  }

  /**
   * Interpolation fraction between the previous and current fixed-step state,
   * derived from the engine accumulator. In [0, 1).
   */
  float getInterpolationAlpha() const;
  const std::vector<Vector2F>& getPreviousPlayerPositions() const
  {
    return previousPlayerPositions;
  }
  const std::vector<float>& getPreviousPlayerFacingAngles() const
  {
    return previousPlayerFacingAngles;
  }
  const std::vector<float>& getPreviousPlayerFeetHeights() const
  {
    return previousPlayerFeetHeights;
  }
  Vector2F getPreviousBallPosition() const { return previousBallPosition; }
  float getPreviousBallZ() const { return previousBallZ; }
  /** The ball has gone beyond the crowd the views show (not drawn). */
  bool ballOutOfSight() const
  {
    return deadBall.active &&
           deadBall.phase == DeadBallReturn::Phase::OUT_OF_SIGHT;
  }

  /** Machine-readable state for headless tests and external debug tooling. */
  std::string getDebugSnapshotJson() const;
  bool writeDebugSnapshot(std::string_view path) const;

  /** Semantic goalkeeper state for each side, updated every fixed step. */
  GoalkeeperState getHomeGoalkeeperState() const { return keepers[0].state; }
  GoalkeeperState getAwayGoalkeeperState() const { return keepers[1].state; }

  /**
   * Loads a fully-specified deterministic scenario and evaluates the carrier's
   * decision exactly as the live engine would (no physics step is advanced).
   * The renderer never calls this; it is only an explicit headless evaluation
   * hook for the scenario suite. The optional parameters let scenario tests
   * reproduce late-game and score-state behaviour deterministically. A
   * controlled carrier (setControlledPlayer, already applied by a step)
   * makes no AI decision and waits for his input.
   */
  bool applyScenario(const MatchScenario& scenario,
                     MatchState scenarioState = MatchState::PLAYING,
                     float scenarioMatchTime = 0.0f, int scenarioHomeScore = 0,
                     int scenarioAwayScore = 0);
  const ScenarioDecision& getLastScenarioDecision() const
  {
    return lastScenarioDecision;
  }

  /**
   * Attaches an observer of the simulation (nullptr detaches it); see
   * model/match_recorder.h. It only receives reports and never changes the
   * match. Copies of this engine start without one.
   */
  void setRecorder(MatchRecorder* recorder) { recorderLink.target = recorder; }

  // --- Obstacles ------------------------------------------------------------

  /** Something on the pitch a dribbler keeps his ball clear of: a drill's
   * cone now; an opponent's reach, later. */
  struct Obstacle
  {
    /** Normalised pitch. */
    Vector2F position{0.0f, 0.0f};
    /** How far round it (m) the ball must not touch. */
    float radiusMetres = 0.0f;
  };
  /** The obstacles on the pitch (none in a match yet; cleared when a
   * scenario or drill is placed). */
  void setObstacles(std::vector<Obstacle> list) { obstacles = std::move(list); }
  const std::vector<Obstacle>& getObstacles() const { return obstacles; }

  // --- Drills (the match sandbox's minigames) ------------------------------
  // All opt-in: an engine none of these are called on plays by the Laws.

  /** Rule switches for drills; the defaults are the Laws of the Game. */
  struct DrillRules
  {
    /** Off: nobody is ever offside. */
    bool offside = true;
    /** Off: a side below seven players does not abandon the match. */
    bool minimumPlayers = true;
    /** Off: the halves never end (no half time, its rest or full time), so
     * a drill can run as long as it likes. */
    bool periods = true;
    /** On: a player held on an urgent drill target runs as hard as one
     * chasing a loose ball, whatever job his side's brain gave him, so a
     * squad drill's runners all go equally hard (a lone runner gets that
     * job anyway). Off: his brain's job sets his effort. */
    bool chaseEffort = false;
  };
  void setDrillRules(const DrillRules& rules) { drillRules = rules; }
  /** The lowest a side's body lets fuel (condition) fall: MINIMUM_STAMINA
   * for an Aardvark body, 0 (an empty tank) for a Binturong one. */
  float fuelFloor(bool homeTeam) const;
  /** A movement mode's speed band for him now, as his side's body sees it
   * (fatigue plan 4.2). */
  SpeedBand speedBand(const MatchPlayer& player, MovementMode mode) const;
  /** The mode whose band holds `speed` for him now. */
  MovementMode movementModeAt(const MatchPlayer& player, float speed) const;
  const DrillRules& getDrillRules() const { return drillRules; }

  /**
   * Before the match starts: the player takes no part, as if he had not
   * been selected (no keeper is promoted in his place, no shape changes).
   * False once play has begun or for an unknown player.
   */
  bool removeBeforeKickOff(PlayerID playerId);

  /**
   * Starts a drill: places the players and the ball as `scenario` says and
   * puts the ball in play, without the carrier deciding anything yet
   * (applyScenario() makes his decision at once). `carrierId` 0 leaves the
   * ball loose. Players keep their condition. False when a placed player or
   * the carrier is unknown.
   */
  bool startDrill(const MatchScenario& scenario);

  /**
   * Drills: holds a player's tactical target (normalised pitch), which then
   * replaces the planner's; `urgent` makes him run at it flat out. A route
   * gives up to two waypoints on the way, and his line through them: how
   * far inside each the turn's apex may pass (`apexOffsetMetres`; negative
   * is outside) and how far out the line may be pushed to widen it
   * (`outwardRoomMetres`). `facing` asks his body to face that way (an
   * angle like facingAngle) whichever way he moves. `atSpeed`: he means to
   * reach it at full speed (running through it), not stop on it. No target
   * releases him to the planner. Logged and replayed like any manager
   * change.
   */
  void setDrillTarget(PlayerID playerId, std::optional<Vector2F> target,
                      bool urgent, const std::vector<Vector2F>& route = {},
                      float apexOffsetMetres = 0.0f,
                      float outwardRoomMetres = 0.0f,
                      std::optional<float> facing = std::nullopt,
                      bool atSpeed = false);
  /** The target a drill holds for a player, if any. */
  std::optional<Vector2F> getDrillTarget(PlayerID playerId) const;

  /** Drills: overrides one of a player's derived stats (a 0-1 rating),
   * replacing what his recipe worked out. Before kick-off; a substitute
   * coming on works his out afresh. */
  void setDerivedStat(PlayerID playerId, DerivedStat stat, float rating);

  /** Drills: the player shoots, through the AI's own shot, as soon as he
   * has the ball and can act; with `aim`, at that spot in the goal instead
   * of one he picks (metres: across from the goal's centre, + toward pitch
   * y = 1, and height); with `powerShare`, struck at that share of his
   * shot power (Binturong body) instead of the usual; `firstTime`: off a
   * pass to him, struck as it reaches him without a touch (a ground ball,
   * Binturong body; otherwise after his touch). Logged and replayed. */
  void forceShot(PlayerID playerId, std::optional<Vector2F> aim = std::nullopt,
                 std::optional<float> powerShare = std::nullopt,
                 bool firstTime = false,
                 std::optional<ShotTechnique> technique = std::nullopt);
  /** Drills: the player passes to `receiverId`, through the AI's own pass
   * (its target, lead and weight), as soon as he has the ball and can act;
   * `style` can force it along the ground or lofted. Logged and replayed. */
  void forcePass(PlayerID playerId, PlayerID receiverId,
                 ForcedPassStyle style);
  /** Drills: the next header the player wins goes to `spot` (normalised;
   * SPOT), at `goalAim` in the goal (metres across from the middle and up;
   * GOAL), or is cushioned and kept (KEEP), instead of what the brain
   * would choose. Logged and replayed. */
  /** Drills: from now on the player's tackles on a free ball (an
   * opponent's ball outside his reach) mean `intent`, instead of the
   * control touch (tackling plan step 3; the brain chooses in step 5).
   * Logged and replayed. */
  void forceTackleIntent(PlayerID playerId, TackleIntent intent);
  /** Where the last free-ball tackle's "win it" nudge was aimed to stop,
   * and how many there have been. For drills. */
  [[nodiscard]] Vector2F getLastNudgeSpot() const { return lastNudgeSpot; }
  [[nodiscard]] int getNudges() const { return nudges; }
  void forceHeader(PlayerID playerId, ForcedHeaderAim aim,
                   std::optional<Vector2F> spot = std::nullopt,
                   std::optional<Vector2F> goalAim = std::nullopt,
                   bool knownEarly = true);
  /** The part of his head the last unchallenged header was met with (met in
   * the sweep; none for a duel's header). For drills. */
  [[nodiscard]] std::optional<HeadPart> getLastHeadPart() const
  {
    return lastHeadPart;
  }
  /** Drills: the player passes to `spot` (normalised), with nobody there,
   * as soon as he has the ball and can act: weighted as for a man standing
   * on it, and a lofted pass is aimed to land on it; `spin` is the spin he
   * means (PassOption::spin), `arrivalSpeed` its pace there. `firstTime`:
   * a pass played to him he plays on as it reaches him, without a touch (a
   * ground ball, Binturong body; otherwise after his touch, as usual).
   * Logged and replayed. */
  void forcePassTo(PlayerID playerId, Vector2F spot, ForcedPassStyle style,
                   std::optional<BallSpin> spin = std::nullopt,
                   std::optional<float> arrivalSpeed = std::nullopt,
                   bool firstTime = false);
  /** Drills: while he has the ball, the player only carries it (no pass,
   * shot, clearance or take-on), so a drill measures his dribbling alone;
   * `keep` off releases him. `urgency` is how hard he carries it (the
   * Binturong brain's carry urgency: 0 a walk, 1/3 a jog, 2/3 a run, 1 a
   * sprint). Logged and replayed. */
  void keepBall(PlayerID playerId, bool keep, float urgency = 1.0f);
  /** Drills: plays the ball in from `from` (normalised) with `velocity`
   * along the ground (m/s) and `lift` upward (m/s): as a pass to
   * `receiverId` (his own pass to receive), or loose for 0. Nobody kicked
   * it, so a completion credits only his side. In play only; logged and
   * replayed. */
  void feedBall(Vector2F from, Vector2F velocity, float lift,
                PlayerID receiverId);

  /** Drills and the lab: the first-touch knobs (match_first_touch_knobs.h),
   * defaulting to the tuned constants. Set before kick-off. A saved
   * recording does not keep them: it replays with the defaults. */
  void setFirstTouchKnobs(const FirstTouchKnobs& knobs) { touchKnobs = knobs; }
  const FirstTouchKnobs& getFirstTouchKnobs() const { return touchKnobs; }
  /** Drills and the lab: the strike knobs (match_strike_knobs.h), the same
   * way. */
  void setStrikeKnobs(const StrikeKnobs& knobs) { strikeKnobs = knobs; }
  const StrikeKnobs& getStrikeKnobs() const { return strikeKnobs; }
  /** Drills: the stretch map every contact profile uses (the touch and
   * every strike), the same way; defaults to FirstTouch::STRETCH_*. */
  void setStretchMap(const StretchMap& map) { contactStretchMap = map; }
  const StretchMap& getStretchMap() const { return contactStretchMap; }
  MatchRecorder* getRecorder() const { return recorderLink.target; }

 private:
  /** The attached recorder; a copy of the engine never inherits it, so
   * highlight look-ahead and saved states are never recorded. */
  struct RecorderLink
  {
    RecorderLink() = default;
    RecorderLink(const RecorderLink& /*other*/) noexcept {}
    RecorderLink& operator=(const RecorderLink& /*other*/) noexcept
    {
      target = nullptr;
      return *this;
    }
    ~RecorderLink() = default;
    MatchRecorder* target = nullptr;
  };
  RecorderLink recorderLink;

  std::vector<Obstacle> obstacles;

  // Drills (see the public drill section).
  DrillRules drillRules;
  struct DrillTarget
  {
    PlayerID player = 0;
    Vector2F target{0.0f, 0.0f};
    bool urgent = false;
    /** Up to two waypoints before it, and his line through them. */
    std::vector<Vector2F> route;
    float apexOffset = 0.0f;
    float outwardRoom = 0.0f;
    /** A body orientation asked of him, if any. */
    std::optional<float> facing;
    /** Reached at full speed, not stopped on. */
    bool atSpeed = false;
  };
  std::vector<DrillTarget> drillTargets;
  /** Puts a player on a held drill target (and its route). */
  void holdDrillTarget(MatchPlayer& player, const DrillTarget& held);
  /** A forced shot waiting for its shooter to be able to act (0: none). */
  PlayerID forcedShooter = 0;
  /** A forced shot's spot in the goal and share of his power (forceShot),
   * used by its strike. */
  std::optional<Vector2F> forcedShotAim;
  std::optional<float> forcedShotPower;
  /** The forced shot is struck first time off a pass to him. */
  bool forcedShotFirstTime = false;
  /** The forced shot's technique (forceShot), used by its strike. */
  std::optional<ShotTechnique> forcedShotTechnique;
  /** A forced header waiting for its player to win a header (forceHeader). */
  struct ForcedHeader
  {
    PlayerID player = 0;
    ForcedHeaderAim aim = ForcedHeaderAim::SPOT;
    Vector2F spot{};
    Vector2F goalAim{};
    bool knownEarly = true;
  };
  std::optional<ForcedHeader> forcedHeader;
  /** A forced pass waiting for its passer to be able to act (passer 0:
   * none). */
  struct ForcedPass
  {
    PlayerID passer = 0;
    PlayerID receiver = 0;
    ForcedPassStyle style = ForcedPassStyle::AS_CHOSEN;
    /** A pass to a spot instead (receiver 0). */
    std::optional<Vector2F> spot;
    std::optional<BallSpin> spin;
    std::optional<float> arrivalSpeed;
    /** Off a pass to him, played as it reaches him (decideFirstTimePass). */
    bool firstTime = false;
  };
  ForcedPass forcedPass;
  /** Players who only carry the ball (keepBall), and how hard each does. */
  std::vector<PlayerID> ballKeepers;
  /** Drills: players whose free-ball tackles mean something other than
   * control (forceTackleIntent). */
  std::vector<std::pair<PlayerID, TackleIntent>> forcedTackleIntents;
  Vector2F lastNudgeSpot{};
  int nudges = 0;
  std::vector<std::pair<PlayerID, float>> ballKeeperUrgency;
  /** Places the scenario's players and ball; the carrier (null when the
   * ball is loose), or nothing when a player is unknown. */
  std::optional<MatchPlayer*> placeScenario(const MatchScenario& scenario,
                                            MatchState scenarioState,
                                            float scenarioMatchTime,
                                            int scenarioHomeScore,
                                            int scenarioAwayScore,
                                            bool resetCondition);
  /** Holds the drill targets over the planner's (end of a refresh). */
  void applyDrillTargets();
  /** The slot of a player by id (null when he is not in the match). */
  MatchPlayer* drillPlayer(PlayerID playerId);
  /** Completes a kick's report from the launched ball and sends it. */
  void reportKick(MatchActionRecord& action, const MatchPlayer& kicker,
                  Vector2F target) const;

  std::vector<MatchPlayer> players;
  /** The people round the touchline, index for index (touchline plan
   * stage 2), set up at the first step; their positions at the last
   * interpolation frame and at the start of this step's movement. */
  std::vector<MatchPlayer> touchlineBodies;
  std::vector<TouchlinePerson> touchlinePeople;
  std::vector<Vector2F> previousTouchlinePositions;
  std::vector<float> previousTouchlineFacingAngles;
  std::vector<Vector2F> touchlineStepStart;
  bool touchlineSetUp = false;
  /** Touchline plan stage 4: the walk-out or walk-off in progress. */
  WalkState walk;
  /** Touchline plan stage 5: the substitutions under way. */
  std::vector<PendingSubstitution> pendingSubstitutions;
  MatchBall ball;
  const StatsConfig& statsConfig;
  Strategy homeStrategy;
  Strategy awayStrategy;
  /** Each side's brain (home, away); see setBrain. */
  std::array<BrainId, 2> brains{BrainId::AARDVARK, BrainId::AARDVARK};
  /** Each side's body (home, away); see setBody. */
  std::array<BodyId, 2> bodies{BodyId::AARDVARK, BodyId::AARDVARK};
  /** The match's handshake (constructor). */
  HandshakeId handshakeId = HandshakeId::AARDVARK;
  /** The match's referee; see setReferee. */
  RefereeId refereeId = RefereeId::AARDVARK;
  /** The match's out-of-play version; see setOutOfPlay. */
  OutOfPlayId outOfPlayId = OutOfPlayId::AARDVARK;
  /** Each side's AI manager (home, away); see setManager. */
  std::array<ManagerId, 2> managers{ManagerId::AARDVARK, ManagerId::AARDVARK};
  /** The match's ball physics; see setBallPhysics. */
  BallPhysicsId ballPhysicsId = BallPhysicsId::AARDVARK;
  std::mt19937 rng;
  // Referee decisions and injuries draw from their own stream so tuning them
  // does not reshuffle player decisions and ball physics.
  std::mt19937 incidentRng;
  std::uint32_t matchSeed = 0;
  /** Simulated time in fixed-step ticks (FIXED_STEP_SECONDS each). */
  std::uint64_t stepCounter = 0;
  /** Ticks advanced per simulated step (1, or more in background fidelity). */
  std::uint32_t stepTicks = 1;
  float refereeStrictness = 1.0f;
  /** Standard normal draw behind the referee's strictness. */
  float refereeDraw = 0.0f;
  MatchContext matchContext;
  /** Shot precision factor from MatchContext::goalRateScale. */
  float finishingPrecision = 1.0f;
  /** Raw attribute shift toward the reference level, and the finishing
   * precision of the match's level. */
  float levelShift = 0.0f;
  float levelPrecision = 1.0f;

  std::vector<PlayerMatchStats> playerStats;
  std::vector<MatchSubstitution> substitutions;
  std::vector<const Player*> homeBench;
  std::vector<const Player*> awayBench;
  /** Everyone in both matchday squads (names for the commentary). */
  std::vector<const Player*> squad;
  /** Set-piece designations of each side (index 0 home), from the lineups. */
  std::array<SetPieceDesignations, 2> designations{};
  bool homeAutoSubstitutions = true;
  bool awayAutoSubstitutions = true;
  int homeSubstitutionWindows = 0;
  int awaySubstitutionWindows = 0;
  std::uint32_t stoppageSequence = 0;
  std::uint64_t lastStoppageStep = 0;
  std::uint64_t manualSubstitutionStep =
      std::numeric_limits<std::uint64_t>::max();
  std::uint32_t homeLastWindowStoppage = 0;
  std::uint32_t awayLastWindowStoppage = 0;
  std::uint32_t homeLastAiReviewStoppage = 0;
  std::uint32_t awayLastAiReviewStoppage = 0;

  int period = 1;
  bool abandoned = false;
  std::array<int, 4> addedMinutes{0, 0, 0, 0};
  std::array<MatchRules::StoppageLog, 4> stoppageLogs{};
  MatchRules::Knockout knockout;
  bool extraTimeReached = false;
  /** Penalty shootout progress (index 0 home). */
  struct Shootout
  {
    bool started = false;
    bool homeFirst = true;
    bool inFlight = false;
    bool kickerHome = true;
    PlayerID taker = 0;
    float timer = 0.0f;
    std::array<int, 2> kicks{0, 0};
    std::array<int, 2> goals{0, 0};
    /** Kicking order of each side (player ids), cycled when exhausted. */
    std::array<std::vector<PlayerID>, 2> order;
    /** A kick at a Binturong keeper (goalkeeper plan step 9): played as an
     * in-match penalty, the open-play ball sweep running while it flies;
     * its outcome once known; and the match's stats, tracker and log as
     * they were before it, put back after (a shootout kick is not match
     * play). */
    bool live = false;
    /** Its strike, at the end of the run-up, and whether it has come. */
    double strikeAt = 0.0;
    bool struck = false;
    std::optional<MatchEventDetail> outcome;
    std::size_t eventsBefore = 0;
    MatchStats statsBefore;
    std::vector<PlayerMatchStats> playerStatsBefore;
    MatchTracker trackerBefore;
  };
  Shootout shootout;
  float elapsedMatchMinutes = 0.0f;
  float injuryCheckTimer = 0.0f;

  struct PendingAdvantage
  {
    bool active = false;
    bool fouledTeamHome = false;
    Vector2F position{MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE};
    float secondsRemaining = 0.0f;
  };
  PendingAdvantage pendingAdvantage;

  /** Chain used for assists and key passes. */
  const Player* lastCompletedPasser = nullptr;
  const Player* lastCompletedReceiver = nullptr;
  const Player* shotAssistCandidate = nullptr;
  const Player* lastShooter = nullptr;
  bool restartIsSetPiece = false;
  /** Shots by the attacking side shortly after a corner or attacking free
   * kick count as set-piece shots, until the defence wins the ball or play
   * stops. */
  float setPiecePhaseRemaining = 0.0f;
  bool setPieceHome = true;
  /** In a set-piece phase: whether the ball has been in the box the set
   * piece attacks, and whether it has since come out (way-round plan: the
   * scramble in that box is left alone until then). */
  bool setPieceBallInBox = false;
  bool setPieceBoxCleared = false;

  std::vector<Vector2F> previousPlayerPositions;
  std::vector<float> previousPlayerFacingAngles;
  std::vector<float> previousPlayerFeetHeights;
  Vector2F previousBallPosition{MatchTuning::Pitch::CENTRE,
                                MatchTuning::Pitch::CENTRE};
  float previousBallZ = 0.0f;

  std::vector<MatchEvent> events;
  /** Trailing events still without their commentary line. */
  std::size_t undescribedEvents = 0;
  std::array<std::string, 2> teamNames;
  MatchStats stats;
  MatchTracker tracker;
  PassDecision lastPassDecision;
  ScenarioDecision lastScenarioDecision;
  struct GoalkeeperControl
  {
    GoalkeeperState state = GoalkeeperState::SET_POSITION;
    float timer = 0.0f;
    float reactionRemaining = 0.0f;
    float diveTargetY = MatchTuning::Pitch::CENTRE;
    float lateralVelocity = 0.0f;
    /** A Binturong keeper's dive (goalkeeper plan step 3). */
    KeeperDive dive;
    /** His penalty guess is made (step 9: as the taker set off). */
    bool penaltyGuessed = false;
  };
  /** Whether this keeper is diving or down from a dive (or set for a shot):
   * his body is the dive's, not the brain's. */
  [[nodiscard]] bool keeperInDive(const MatchPlayer& player) const
  {
    if (!player.isGoalkeeper) return false;
    const GoalkeeperControl& control = keepers[player.isHomeTeam ? 0 : 1];
    return control.state == GoalkeeperState::DIVE || control.dive.active;
  }
  /** Whether this keeper has pushed off into a dive and is not yet up. */
  [[nodiscard]] bool keeperDiving(const MatchPlayer& player) const;
  /** Index 0 is the home keeper, 1 the away keeper. */
  std::array<GoalkeeperControl, 2> keepers{};
  MatchState state = MatchState::KICK_OFF;
  TeamPhase homePhase = TeamPhase::SET_PIECE;
  TeamPhase awayPhase = TeamPhase::SET_PIECE;
  std::optional<bool> lastControlledTeamHome;
  float transitionSecondsRemaining = 0.0f;
  /** Slot index of the restart taker (an index keeps the engine copyable). */
  std::optional<std::size_t> restartTakerIndex;
  float setPieceTimer = 0.0f;
  /** A kick-off after a goal that everyone walks to (Binturong out of
   * play): everyone moves during it, and it waits for them. */
  bool kickOffWalkIn = false;
  /** The ball on its way back after going out (Binturong out of play). */
  DeadBallReturn deadBall;
  RestartReadiness restartReadiness;
  bool goalScoredByHome = false;
  float goalCelebrationRemaining = 0.0f;
  float accumulator = 0.0f;
  float ratingRefreshTimer = 0.0f;
  /** When the current carrier gained the ball (simulated seconds). */
  double possessionStartSeconds = 0.0;
  /** Ball state at the start of the current ball sub-step. */
  Vector2F substepBallPosition{MatchTuning::Pitch::CENTRE,
                               MatchTuning::Pitch::CENTRE};
  float substepBallZ = 0.0f;
  /** Ball sub-steps still to come in the current step after this one, and
   * the step's length: players move only between steps. */
  int substepsLeftInStep = 0;
  float ballStepSeconds = MatchTuning::Timing::FIXED_STEP_SECONDS;
  /** What the ball physics moves: the ball at the start of the stretch of
   * sub-step about to happen and where it would be by its end (the sweep
   * reads who it meets on the way), and how long that stretch is (s). A
   * contact is played where the ball reaches it, part way through. */
  struct BallMotion
  {
    Vector2F position{};
    float z = 0.0f;
    Vector2F velocity{};
    float velocityZ = 0.0f;
    BallSpin spin{};
    bool isAerialDelivery = false;
  };
  BallMotion substepStartMotion;
  BallMotion substepEndMotion;
  /** Each player's legs as a shot passing him met them (shot block plan
   * §3c, §3d). */
  std::vector<ShotLegs> shotLegPoses;
  ShotBounce shotBounce;
  float substepStretchSeconds = 0.0f;
  /** How much of the sub-step had gone (share) when this stretch began. */
  float substepStretchFromShare = 0.0f;
  /** Whether the ball is still on the path the physics read for the
   * stretch (no parry or woodwork has turned it since). */
  bool substepPathKnown = true;
  /** Set by the sweep when a contact was played part way through the
   * stretch: the share of it the ball had travelled. The ball goes on for
   * the rest of it with what the contact gave it. */
  std::optional<float> contactAlong;
  [[nodiscard]] BallMotion ballMotion() const;
  void setBallMotion(const BallMotion& motion);
  /** Moves the ball through one stretch of a sub-step (the shot at the
   * keeper's plane, out of play, the sweep); the share of it travelled
   * when a contact was played part way, if one was. */
  std::optional<float> moveBallStretch(float seconds);
  /** The stretch up to a fallen body in the way, or all of it (the old
   * stretch: keeper's plane, out of play, the sweep). */
  std::optional<float> moveBallPart(float seconds);
  float homeFamiliarity = 1.0f;
  float awayFamiliarity = 1.0f;
  struct ShoutState
  {
    MatchShout shout = MatchShout::ENCOURAGE;
    float remainingSeconds = 0.0f;
    /** Share of the full effect (lower for repeated shouts). */
    float impact = 1.0f;
    /** Recent shouts, fading by one per SHOUT_REPEAT_FADE_SECONDS. */
    float repeats = 0.0f;
  };
  /** Index 0 is the home side, 1 the away side. */
  std::array<ShoutState, 2> shouts{};
  /** Sliders in force (tactics plus shout), refreshed when they change. */
  std::array<StrategySliders, 2> effectiveSliders{};
  /** The score changed (or was set) since the sliders were computed. */
  bool slidersStale = false;
  /** Underdog caution by side (0 = not the weaker side), from the XIs. */
  std::array<float, 2> underdogShares{};
  /** Team-talk modifiers by side and half. */
  std::array<std::array<float, 2>, 2> teamTalks{};
  /** Role of an outfield slot and its in-possession shift. */
  struct SlotTactic
  {
    TacticalRole role = TacticalRole::Standard;
    RoleProfile profile;
    Vector2F possessionOffset{0.0f, 0.0f};
  };
  /** Slot roles by side (index 0 home), indexed by formation slot. */
  std::array<std::array<SlotTactic, 16>, 2> slotTactics{};
  std::array<RoleProfile, 2> keeperProfiles{};
  /** Kick-off slot positions by side (lineup coordinates): the keys that
   * match slots to the tactics' role instructions. */
  std::array<std::vector<Vector2F>, 2> slotAnchors{};
  /** Instructions each side (index 0 home) gives against opposing players. */
  std::array<std::vector<PlayerInstruction>, 2> oppositionOrders{};
  /** Per-step target blends: home tactical/urgent, away tactical/urgent. */
  std::array<float, 4> targetBlends{};
  /** Movement jitter (observed only), and what each slot's observation
   * compares with. */
  std::array<MovementJitterStats, 2> movementJitter{};
  std::uint32_t firstTouchWaitsMissed = 0;
  std::uint32_t dribbleTouches = 0;
  double dribbleContactGapSum = 0.0;
  std::uint32_t dribbleFailedTouches = 0;
  /** Open-play strikes read by the contact model (observed only). */
  StrikeContactStats strikeContacts;
  BodyContactStats bodyContacts;
  FirstTouchKnobs touchKnobs;
  StrikeKnobs strikeKnobs;
  StretchMap contactStretchMap;
  std::array<LoadProfileStats, 2> loadProfile{};
  struct JitterTrack
  {
    const Player* player = nullptr;
    bool planned = false;
    Vector2F lastTarget{};
    Vector2F lastMove{};
    PlayerIntent intent = PlayerIntent::HOLD_SHAPE;
    PlayerIntent intentBefore = PlayerIntent::HOLD_SHAPE;
    std::uint32_t refreshesSinceSwitch = 0;
    bool moving = false;
    Vector2F lastPosition{};
    Vector2F windowStart{};
    double windowPath = 0.0;
    Vector2F lastVelocity{};
    /** Speed last step, and whether it was last rising (1) or falling (-1). */
    float lastSpeed = 0.0f;
    std::int8_t speedTrend = 0;
  };
  std::array<JitterTrack, 32> jitterTracks{};
  std::uint64_t lastJitterStep = 0;
  std::array<std::uint8_t, 32> separationOrder{};
  std::size_t separationCount = 0;
  /** Positions after the last separation pass and whether it found no
   * contact (an unchanged, contact-free field needs no new pass). */
  std::array<Vector2F, 32> separationPositions{};
  bool separationClear = false;
  /** Collision plan: everyone's position before this step's movement (the
   * start of each swept path), and the run-ins it found. */
  std::array<Vector2F, 32> stepStartPositions{};
  std::vector<CollisionHit> lastCollisions;
  /** Ball situation at the last tactical refresh (event-driven refresh). */
  const Player* tacticalOwner = nullptr;
  std::uint8_t tacticalFlags = 0;
  /** Counts the forced refreshes (the ball changed hands, or a flight
   * started or ended): every player's plan starts afresh at one. */
  std::uint32_t planEpoch = 0;
  /** Seconds and anchor positions since the last load accounting. */
  float loadSeconds = 0.0f;
  std::vector<Vector2F> loadAnchors;
  /** Opponent slot each outfield player marks (-1 when unassigned). */
  std::array<std::int8_t, 32> markAssignments{};
  /** Medical staff instructions by squad player (see setMedicalFlags). */
  std::vector<std::pair<PlayerID, std::uint8_t>> medicalFlags;
  /** Pre-match condition of bench players, applied when they come on. */
  std::vector<std::pair<PlayerID, float>> benchConditions;

  /** External control (play mode); an index keeps the engine copyable. */
  std::optional<std::size_t> controlledIndex;
  /** Who was handed the controls (the slot may change hands). */
  PlayerID controlledPlayerId = 0;
  /** By side (index 0 home): ever controlled, and the actions played. */
  std::array<bool, 2> everControlled{};
  std::array<int, 2> controlledActions{};
  int playHalfMinutes = 0;
  int pendingPlayHalfMinutes = 0;
  [[nodiscard]] float matchClockRate() const
  {
    return playHalfMinutes > 0 ? MatchTuning::Timing::HALF_TIME_MINUTE /
                                     static_cast<float>(playHalfMinutes)
                               : 1.0F;
  }
  MatchPlayerInput controlInput;
  float controlActionRemaining = 0.0f;
  MatchInputAction lastInputAction = MatchInputAction::NONE;
  /** Aim and power of the controlled player's shot being struck. */
  struct ControlledShot
  {
    bool active = false;
    /** Aimed crossing point (metres from the goal's centre), if aimed. */
    std::optional<float> aimMetres;
    float power = 0.0f;
  };
  ControlledShot controlledShot;
  std::vector<MatchInputRecord> inputLog;
  std::size_t inputCursor = 0;
  std::vector<MatchCommandRecord> commandLog;
  std::size_t commandCursor = 0;
  /** Outfield slot positions by side (index 0 home), lineup coordinates. */
  std::array<std::vector<Vector2F>, 2> formations{};
  /** Familiarity lost to a change of shape and the seconds it still lasts. */
  std::array<float, 2> reshapeFamiliarityCost{};
  std::array<float, 2> reshapeSecondsRemaining{};

  std::vector<MatchHighlight> highlights;
  std::uint64_t highlightTriggerCount = 0;
  MatchEventType lastTriggerType = MatchEventType::INFO;
  double lastTriggerSeconds = 0.0;
  float lastTriggerMinute = 0.0f;
  MatchPlaybackMode playbackMode = MatchPlaybackMode::FULL_MATCH;
  float playbackSpeed = MatchTuning::Playback::DEFAULT_SPEED;
  float highlightSpeed = MatchTuning::Playback::DEFAULT_HIGHLIGHT_SPEED;
  std::optional<MatchHighlight> scheduledHighlight;
  /** Bumped by manual interventions; stale predictions are discarded. */
  std::uint32_t inputRevision = 0;
  std::uint32_t scheduledRevision = 0;
  float homePossessionMinutes = 0.0f;
  float awayPossessionMinutes = 0.0f;
  float matchTimeMinutes = 0.0f;
  int homeScore = 0;
  int awayScore = 0;
  int lastUpdateStepCount = 0;
  std::uint64_t droppedSimulationSteps = 0;

  struct PassOption
  {
    MatchPlayer* receiver = nullptr;
    Vector2F targetPoint{MatchTuning::Pitch::CENTRE,
                         MatchTuning::Pitch::CENTRE};
    PassIntent intent = PassIntent::RECYCLE;
    float utility = 0.0f;
    float progression = 0.0f;
    float laneRisk = 0.0f;
    float completionProbability = 0.0f;
    float passDistance = 0.0f;
    bool lofted = false;
    /** The spin he means (rad/s): sidespin (+ curls it to the right of its
     * way) and topspin (- backspin). None: the body's default for the kind
     * of pass. */
    std::optional<BallSpin> spin;
    /** The pace (m/s) he wants a ground pass to reach the receiver (or its
     * target) at; none, the body's default weighting. */
    std::optional<float> arrivalSpeed;
    /** How he strikes it; none, along the ground or lofted by `lofted`. A
     * chip is lofted, a driven pass not. */
    std::optional<PassTechnique> technique;
  };

  /** A strike decided at the step's start on a loose ball at his feet,
   * played where he next meets it in the ball's sweep (unified kick plan
   * step 3). Everything it does (offside, stats, the record) happens then;
   * unplayed by the step's end, it is dropped and he decides again. One
   * decided for his next meet with the ball (dribble meet plan 2a) waits
   * for that meet, over steps, while it holds (`atMeet`: how long so far,
   * and the meet it was decided for). */
  /** Where a shot is aimed, in metres across and up the goal mouth. */
  struct ShotAim
  {
    /** Across the goal from its centre (positive toward pitch y = 1). */
    float aimY = 0.0f;
    float aimZ = 0.0f;
    /** How far inside the post he aimed (the controlled shot uses it). */
    float inset = 0.0f;
  };
  struct PendingStrike
  {
    enum class Kind : std::uint8_t
    {
      PASS,
      SHOT,
      CLEAR,
    };
    Kind kind = Kind::PASS;
    MatchPlayer* striker = nullptr;
    PassOption pass;
    bool forceLofted = false;
    float forcedXG = -1.0f;
    /** A shot: the spot he picked when he decided on it (shooting
     * technique plan step 4), struck at when his foot meets the ball. */
    std::optional<ShotAim> shotAim;
    /** The striker's slot in `players`, and the pass's receiver's: an
     * engine copy re-points `striker` and `pass.receiver` by them
     * (rebindPendingStrike). */
    std::size_t strikerSlot = 0;
    std::optional<std::size_t> receiverSlot;
    bool atMeet = false;
    float heldSeconds = 0.0f;
    Vector2F meetPoint{};
    /** A pass or shot held while his body turns to it (Binturong body),
     * for at most turnSeconds. */
    bool turning = false;
    float turnSeconds = 0.0f;
    /** A pass he means to play first time off a ball still coming to him
     * (`meetPoint`: where he reads he will meet it), struck in the sweep
     * instead of a touch while it holds. */
    bool firstTime = false;
  };
  std::optional<PendingStrike> pendingStrike;
  /** Where and when the ball, as it is moving, first comes within his feet's
   * reach, him on at his pace, within the first-time horizon. */
  struct FirstTimeMeet
  {
    float seconds = 0.0f;
    float heightMetres = 0.0f;
    Vector2F point{};
  };
  [[nodiscard]] std::optional<FirstTimeMeet> firstTimeMeet(
      const MatchPlayer& player) const;
  /** Each step, before the ball moves: a player with a pass coming to him
   * who is to play it first time (a drill's forced pass, for now) holds
   * that pass for the sweep once the ball is within the horizon and stays on
   * the ground; it is re-read every step, and dropped (he takes a touch)
   * when it no longer holds. */
  void decideFirstTimePass();
  /** A drill's forced pass as he would play it now: the option, and
   * whether it is forced lofted; none when its receiver is gone. */
  std::optional<std::pair<PassOption, bool>> forcedPassOption(
      MatchPlayer& carrier, const ForcedPass& pass);
  /** Whether he plays the ball first time where he meets it now. */
  [[nodiscard]] bool playsFirstTime(const MatchPlayer& player, bool hands,
                                    float heightMetres) const;
  /** The pass to him is completed and his held pass played on from where
   * he met it, the ball as it was moving. */
  void playFirstTimePass(MatchPlayer& player, const TouchMoment& moment);
  /** True while the carrier decides what to do with a loose ball at his
   * feet: a pass, shot or clearance then waits for the sweep. */
  bool strikesWaitForSweep = false;
  /** True while he decides for his next meet with the ball (a strike then
   * waits for that meet). */
  bool decidingAtMeet = false;
  /** The carrier decides as at his next meet with the ball (dribble meet
   * plan 2a): he put at the meet point, the others where he reads they will
   * be by then (a blend of their current velocity and their planned target,
   * more of the target for better Vision). */
  void decideActionAtMeet(MatchPlayer& carrier);
  /** A strike held for his meet that no longer holds (the ball lost, the
   * meet moved, too long) is dropped, after each step; any other is dropped
   * at the step's end. */
  void ageHeldStrike(float dt);
  /** The sweep moment a strike is being played at, while it is. */
  std::optional<TouchMoment> strikeAt;
  /** Turning to head (heading technique plan step 7): the facing a player
   * turns his body to before a header he already means, re-read each step
   * (planHeaderTurnBinturong). */
  struct HeaderTurn
  {
    const MatchPlayer* player = nullptr;
    float facing = 0.0f;
  };
  std::optional<HeaderTurn> headerTurn;
  /** The loose ball's flight ahead, read once per move for timing jumps
   * (aerial challenge plan step 1): seconds from now, where (metres), how
   * high (m). */
  struct JumpPathPoint
  {
    float seconds = 0.0f;
    Vector2F at{};
    float height = 0.0f;
  };
  std::vector<JumpPathPoint> jumpPath;
  /** The same, further ahead, for the Binturong brain's challenge for a
   * loose high ball (aerial challenge plan step 2). */
  std::vector<JumpPathPoint> challengePath;
  /** Holds `strike` for the sweep when strikes wait for it; false if it is
   * to be played now. */
  bool holdStrikeForSweep(const PendingStrike& strike);
  /** Plays the held strike where he meets the ball (`moment`). */
  void playPendingStrike(const TouchMoment& moment);
  /** Notes the held strike's players' slots, as it is held. */
  void notePendingStrikeSlots();
  /** At each step's start: the held strike's players by their slots (an
   * engine copied with one held still points into the original). */
  void rebindPendingStrike();

  // --- Brain: decisions ------------------------------------------------------
  // What a side's players choose to do. Each choice returns an intention that
  // the body (physics, execution, contests, rules) then carries out; see
  // match_brain.h. Nothing here launches the ball or resolves a contest.

  /**
   * How a side's players move toward their plan: how fast they absorb a new
   * target (the target lag) and how hard they choose to run. The shared
   * movement code reads it; the side's brain supplies it.
   */
  struct MovementStyle
  {
    /** False: players head straight for the latest tactical target, with no
     * target lag (the response rates below are then unused). */
    bool smoothsTargets = true;
    /** Target response per second, normally and when the move is urgent. */
    float tacticalResponsePerSecond = 0.0f;
    float urgentResponsePerSecond = 0.0f;
    /** Response lost by a side with no familiarity with its tactics. */
    float familiarityResponseLoss = 0.0f;
    /** Share of fresh top speed each intent runs at (by PlayerIntent). */
    std::array<float, PLAYER_INTENT_COUNT> intentSpeedShares{};
    /** Walking to a restart position, as a share of fresh top speed. */
    float restartWalkShare = 0.0f;
    /** Far from his target a player runs harder: maxUrgencyShare at this
     * distance and beyond, proportionally less closer in. */
    float urgencyDistanceMetres = 0.0f;
    float maxUrgencyShare = 0.0f;
    /** Pacing: how much less hard a fully tired player chooses to run
     * (read by the Binturong body; the Aardvark body has it built in). */
    float fatigueWorkRateLoss = MatchTuning::Player::FATIGUE_WORK_RATE_LOSS;
    /** Pacing on the burst reserve (read by the Binturong body): the intents
     * that save it, and the reserve below which a saving player keeps
     * closer to his critical speed (at an empty reserve, to it). 0: never
     * saves. */
    std::array<bool, PLAYER_INTENT_COUNT> savesBurst{};
    float burstSavingReserve = 0.0f;
    /** Movement modes (fatigue plan 4.2, read by the Binturong body; an
     * Aardvark body keeps the shares above): each intent's mode, run at the
     * top of its band (restarts at a walk), in place of the shares, the
     * urgency, the pacing and the touchline's lift. */
    bool usesModes = false;
    std::array<MovementMode, PLAYER_INTENT_COUNT> intentModes{};
    /** A jog job's distance ramp (m): the top of walk within [0], rising to
     * the top of jog by [1], jog to [2], rising to the top of run by [3]. */
    std::array<float, 4> jogRampMetres{};
  };
  /** The brain a side decides with. */
  BrainId brainOf(bool homeTeam) const { return brains[homeTeam ? 0 : 1]; }
  /** The body a side's players move and play with. */
  BodyId bodyOf(bool homeTeam) const { return bodies[homeTeam ? 0 : 1]; }
  /** The movement style of a side's brain. */
  const MovementStyle& movementStyle(bool homeTeam) const;

  /** The carrier's choice: shoot, carry, shield, clear or pass. */
  struct BallAction
  {
    ScenarioAction kind = ScenarioAction::NONE;
    /** PASS: the chosen pass. */
    std::optional<PassOption> pass;
    /** CARRY and SHIELD: how long he keeps the ball before deciding again. */
    float holdSeconds = 0.0f;
  };
  /** Scores the carrier's options and picks one (decision record included). */
  BallAction chooseBallAction(MatchPlayer& carrier);
  /** Carrying the ball: the defender he tries to beat now, if any. */
  MatchPlayer* chooseTakeOn(MatchPlayer& carrier);
  /** Whether the runner in `slot` goes round someone he is about to run
   * into (MatchPlayer::overtaking). */
  void planOvertake(std::size_t slot);
  /** Way-round plan: a loose-ball chaser's way round a player in his way
   * (the side's brain; after the overtakes). */
  void planWayRound(std::size_t slot);
  /** The team's off-ball intents the moment a pass is released. */
  void afterPassReleased(const MatchPlayer& passer, const MatchPlayer& receiver);
  ShotAim chooseShotAim(const MatchPlayer& shooter, bool header, bool penalty,
                        bool freeKick);
  /** A keeper's guess at a penalty: -1 or 1 (side of pitch y), 0 to stay. */
  float choosePenaltyGuess(const MatchPlayer& keeper);
  /** Where a clearance is aimed and how far it should go. */
  struct ClearanceAim
  {
    Vector2F target;
    float metres = 0.0f;
  };
  ClearanceAim chooseClearanceAim(const MatchPlayer& defender);
  /** Where to knock the ball over the own goal line, wide of the posts. */
  Vector2F chooseClearBehindTarget(const MatchPlayer& defender);
  /** What a header is for: a shot, cushioning a team-mate's ball, or play. */
  enum class HeaderAttempt
  {
    SHOOT,
    CUSHION,
    PLAY
  };
  HeaderAttempt chooseHeaderAttempt(const MatchPlayer& header,
                                    bool fromTeammate);
  /** A header in play: over the own line, cleared, or knocked down. */
  /** What a header's pace is for (headerPace): to land on a spot, a
   * clearance, or as hard as he can at a set elevation. */
  enum class HeaderPace : std::uint8_t
  {
    TO_SPOT,
    CLEAR,
    FULL,
  };
  struct HeaderPlay
  {
    enum class Kind
    {
      BEHIND,
      CLEAR,
      KNOCK_DOWN
    };
    Kind kind = Kind::KNOCK_DOWN;
    /** CLEAR and KNOCK_DOWN: the point headed toward. */
    Vector2F aim;
    /** KNOCK_DOWN: the team-mate it is meant for (may be null). */
    const MatchPlayer* target = nullptr;
    /** BEHIND with a speed: headed out along `aim` at this pace over the
     * ground (m/s) and lift (m/s up). Without one, clearBehind aims it. */
    float speed = 0.0f;
    float lift = 0.0f;
  };
  HeaderPlay chooseHeaderPlay(const MatchPlayer& header, bool fromTeammate);
  /** A defender going in on the carrier this step (none: no challenge). */
  struct Challenge
  {
    MatchPlayer* defender = nullptr;
    bool sliding = false;
  };
  Challenge chooseChallenge(const MatchPlayer& carrier, float dt);
  /** Tackling plan step 5: the attacker's ball in his reach and the
   * attacker's (a contest), whether he goes in this step or holds
   * (decided once a step, kept for the step). */
  bool goesIntoContest(MatchPlayer& defender, const MatchPlayer& owner,
                       Vector2F contact);
  /** Tackling plan step 5: what his tackle means to do. */
  TackleIntent chooseTackleIntent(const MatchPlayer& tackler,
                                  const MatchPlayer& attacker, Vector2F contact,
                                  bool freeBall);
  /** First touch: whether he plays the ball now or lets it come closer
   * (asked every ball sub-step it is in his reach). */
  bool touchNow(const MatchPlayer& player, const TouchMoment& moment);
  /** Brain: his own ball is due a touch; play it now, or let it come closer
   * first (false). */
  bool dribbleTouchNow(const MatchPlayer& player, const TouchMoment& moment);
  /** First touch: what he tries to do with it. */
  TouchIntent chooseFirstTouch(const MatchPlayer& player,
                               const TouchMoment& moment);
  /** Whether an outfielder in a shot's path throws his body in. */
  bool choosesToBlock(const MatchPlayer& player);
  /** Whether a defender who blocked a shot puts it behind. */
  bool choosesToPutBlockBehind(const MatchPlayer& player);
  /** Whether a defender meeting a delivery near his goal puts it behind. */
  bool choosesToClearCrossBehind(const MatchPlayer& player);
  /** Who takes a restart of the given kind (spot: where it is taken). */
  MatchPlayer* chooseRestartTaker(MatchState restart, bool homeTeam,
                                  Vector2F spot, bool topCorner = false);
  /** How a restart is released: a direct shot, a pass or kept. */
  struct RestartPlay
  {
    enum class Kind
    {
      DIRECT_SHOT,
      PASS,
      HOLD
    };
    Kind kind = Kind::HOLD;
    std::optional<PassOption> pass;
    /** PASS: played in the air whatever the option says. */
    bool lofted = false;
  };
  RestartPlay chooseRestartPlay(MatchPlayer& taker, MatchState restartState);
  /** Releases a restart as chosen. */
  void playRestart(MatchPlayer& taker, const RestartPlay& play);
  /**
   * A Binturong body's taker lining up a dead ball (restart lining-up): the
   * play he chose when his time was up (or the pass his controller asked
   * for), the line he means to strike it along (metric unit; none for a
   * controller's pass), and how long he has been at it. He walks to a spot
   * just behind the ball on that line, turning to it, and kicks it once it
   * is inside his comfort gap (so out of his dead zone), turned or out of
   * time.
   */
  struct RestartLineUp
  {
    RestartPlay play;
    MatchInputAction userAction = MatchInputAction::NONE;
    std::optional<Vector2F> line;
    float seconds = 0.0f;
  };
  std::optional<RestartLineUp> restartLineUp;
  /** Whether this taker lines the ball up before kicking it: a Binturong
   * body's outfielder, at any restart he kicks (not a throw-in). */
  bool linesUpRestart(const MatchPlayer& taker, MatchState restart) const;
  /** Lines the taker up, choosing his play the first time (a controller's
   * pass if `userPass`); whether the ball is ready to kick. */
  bool lineUpRestart(MatchPlayer& taker, MatchState restart, bool userPass);
  /** Steers a lining-up taker this step: to his spot behind the ball,
   * turning to his line. */
  void steerRestartLineUp(MatchPlayer& taker, float dt);
  /** Counts a lined-up restart as he kicks it. */
  void noteRestartKick(const MatchPlayer& taker, const RestartLineUp& lineUp,
                       MatchState restart);
  RestartLineUpStats restartLineUpStats;
  /**
   * How the score and the gap in quality bend a side's tactics: in front it
   * sits deeper and takes fewer risks, behind it pushes on; the underdog
   * sits deeper and presses less.
   */
  void applyTeamMentality(StrategySliders& sliders, bool homeTeam) const;
  /** The attackers a side sends into the box at a set piece, most
   * dangerous first (what the defending side sees). */
  struct SetPieceAttackers
  {
    std::array<const MatchPlayer*, 5> players{};
    std::size_t count = 0;
  };
  SetPieceAttackers arrangeSetPieceAttack(bool attackingHome,
                                          Vector2F ballPosition);
  void arrangeSetPieceDefence(bool defendingHome, Vector2F ballPosition,
                              const SetPieceAttackers& attackers);

  // The brain functions above take the decision for a side: each dispatches
  // to that side's brain (match_brain_dispatch.cpp). The brains' own members
  // follow, one file per brain.
#include "model/match_brain_aardvark_members.h"
#include "model/match_brain_binturong_members.h"

  // --- Body: carrying out the brain's choices --------------------------------
  void executeBallAction(MatchPlayer& carrier, const BallAction& action);

  // --- Handshake: the game's inputs become match state (match_handshake.h) --
  // One per match. Besides these, computeLevel, rawAttribute, attribute,
  // loadAttributes, initializePlayers, resolveTactics, placeFormation,
  // drawRefereeStrictness and computeUnderdogShares are the handshake's.

  /** The match context, sanitised, and the finishing precision it sets. */
  void convertMatchContext(const MatchContext& context);
  /** A player's starting condition (0-1) as match stamina. */
  float convertCondition(float condition) const;
  /** A side's tactical familiarity (0-1). */
  float convertFamiliarity(float familiarity) const;
  /** A team talk's effect, within its allowed swing. */
  float convertTeamTalk(float modifier) const;

  // --- Player body: one player's body (see match_body.h) ---------------------
  // How well one player's body carries out his brain's choice and what it
  // costs him. Each runs with the body of the player's side; contests between
  // players, the ball's flight and the rules are shared.

  /** Whether his body can follow a route (waypoints on the way). */
  bool followsRoutes(const MatchPlayer& player) const;

  /** The controlled player's body following the stick (Play mode): physics
   * sub-steps, and his facing kept on `faceToward` while jockeying. */
  void moveControlled(MatchPlayer& player, Vector2F desired, float desiredSpeed,
                      float topSpeed, float fatigue, float dt,
                      const MatchPlayer* faceToward);
  /** One load period of energy for players gathered in lanes. */
  struct FitnessLanes
  {
    static constexpr std::size_t LANES = 32;
    alignas(16) std::array<float, LANES> fromX;
    alignas(16) std::array<float, LANES> fromY;
    alignas(16) std::array<float, LANES> toX;
    alignas(16) std::array<float, LANES> toY;
    alignas(16) std::array<float, LANES> topSpeed;
    alignas(16) std::array<float, LANES> endurance;
    alignas(16) std::array<float, LANES> pressingEffort;
    alignas(16) std::array<float, LANES> sideDrain;
    alignas(16) std::array<float, LANES> stamina;
    alignas(16) std::array<float, LANES> reserve;
    /** Results: distance covered, and the stamina and sprint reserve after. */
    alignas(16) std::array<float, LANES> stepMetres;
    alignas(16) std::array<float, LANES> nextStamina;
    alignas(16) std::array<float, LANES> nextReserve;
    /** Observed only (a body may leave them 0): metabolic energy of the
     * period (J/kg), and the part above his critical speed's power. */
    alignas(16) std::array<float, LANES> stepEnergy{};
    alignas(16) std::array<float, LANES> stepAnaerobicEnergy{};
    /** The same two while he was slowing down. */
    alignas(16) std::array<float, LANES> stepBrakingEnergy{};
    alignas(16) std::array<float, LANES> stepBrakingAnaerobicEnergy{};
    /** The reserve's fast-track shortfall before and after (a body
     * without one leaves it 0). */
    alignas(16) std::array<float, LANES> fastDebt{};
    alignas(16) std::array<float, LANES> nextFastDebt{};
  };
  /** The endurance stats' physical values, lane by lane alongside the
   * FitnessLanes of the same step (match_derived_stats.h): critical speed
   * (m/s), burst capacity (m), the walking refill time constant (s) and the
   * fuel capacity (s), with his walk-run transition speed (m/s) and his
   * speed over the previous load period (m/s). Read by the Binturong body. */
  struct EnduranceLanes
  {
    static constexpr std::size_t LANES = FitnessLanes::LANES;
    alignas(16) std::array<float, LANES> criticalSpeed{};
    alignas(16) std::array<float, LANES> burstMetres{};
    alignas(16) std::array<float, LANES> recoverySeconds{};
    alignas(16) std::array<float, LANES> fastRecoverySeconds{};
    alignas(16) std::array<float, LANES> fuelSeconds{};
    alignas(16) std::array<float, LANES> transitionSpeed{};
    alignas(16) std::array<float, LANES> previousSpeed{};
    /** What his movement costs against forward running at the same speed
     * (more, moving sideways or backward to his body: orientation plan). */
    alignas(16) std::array<float, LANES> directionCost{};
  };
  EnduranceLanes enduranceLanes;
  /** Stamina drain and sprint-reserve drain or recovery for lanes
   * [begin, end), all players of one side. */
  void spendEnergy(FitnessLanes& lanes, std::size_t begin, std::size_t end,
                   float dt, bool homeTeam) const;
  /** Chance of a non-contact injury in the next check interval. */
  float injuryChance(const MatchPlayer& player) const;
  /** How a kicked ball leaves the foot: the point it is really struck to
   * and its launch (the world's launchBall then flies it). */
  struct Kick
  {
    Vector2F target;
    float speed = 0.0f;
    float verticalSpeed = 0.0f;
    /** The spin it leaves with (rad/s). */
    BallSpin spin{};
    /** Struck with his left foot (the contact model's: the foot on the side
     * the ball is on, his stronger one on his line). */
    bool leftFoot = false;
  };
  /** The passer's strike: execution error, weight, height and curve. No
   * receiver (a drill's pass to a spot): a lofted ball lands on the target.
   * `spin`: the spin he means (PassOption::spin); `arrivalSpeed`: the
   * pace a ground pass is to arrive at (PassOption::arrivalSpeed). */
  Kick kickPass(const MatchPlayer& passer, const MatchPlayer* receiver,
                Vector2F target, float passDistance, bool lofted,
                bool delivery, std::optional<BallSpin> spin,
                std::optional<float> arrivalSpeed,
                std::optional<PassTechnique> technique = std::nullopt);
  /** How long a player needs after an action before he can act again. */
  enum class Recovery
  {
    PASS,
    SHOT
  };
  float recoverySeconds(const MatchPlayer& player, Recovery after);
  /** A shot's execution error: metres, one standard deviation (Aardvark);
   * a multiplier on the shot contact's angle error (Binturong). */
  float shotSpread(const MatchPlayer& shooter, bool header, bool penalty,
                   bool freeKick, float metres) const;
  /** Where a shot crosses the goal line (metres) and how hard it is hit;
   * with `launchSet`, the upward launch speed too (else it is solved to
   * cross at that height). */
  struct ShotStrike
  {
    float crossingY = 0.0f;
    float crossingZ = 0.0f;
    float speed = 0.0f;
    float verticalSpeed = 0.0f;
    bool launchSet = false;
    /** The spin it leaves with, and a point (normalised) along the way it
     * leaves when that isn't straight at where it crosses the line (a
     * curled shot). */
    BallSpin spin{};
    std::optional<Vector2F> launchToward;
  };
  /** @param powerScale Play mode's power (1 for the AI).
   * @param technique How it is struck (Binturong body; none: driven). */
  ShotStrike strikeShotBody(const MatchPlayer& shooter, float aimY, float aimZ,
                            float spread, bool header, float powerScale,
                            std::optional<ShotTechnique> technique = std::nullopt);
  float clearanceSpeed(const MatchPlayer& defender);
  float headerClearanceSpeed(const MatchPlayer& header);
  /** Where a knock-down meant for `aim` really goes. */
  Vector2F knockDownPoint(const MatchPlayer& header, Vector2F aim);
  /** A header's pace over the ground and upward (m/s), for its purpose,
   * towards `target`; `speed` and `lift` are the brain's (the Aardvark body
   * heads it at those; the Binturong body takes only their elevation, for
   * FULL). */
  std::pair<float, float> headerPace(const MatchPlayer& header,
                                     HeaderPace purpose, Vector2F target,
                                     float speed, float lift) const;
  /** Brings a received ball under control: trap time and when he can act
   * (a keeper with it in his hands: how long he holds it). */
  void settleOnBall(MatchPlayer& player, float touchSkill, bool hands);
  float keeperHoldSeconds(const MatchPlayer& keeper);
  /** Whether an outfielder in a shot's path gets his body in the way
   * (gap: from his centre to the ball's path; reach: the most he had). */
  bool blocksShot(const MatchPlayer& player, float gap, float reach);
  /** Whether he reads and reaches a ball not meant for him. */
  bool reachesBall(const MatchPlayer& player, float ballSpeed, float stretch);
  /** His first-touch skill (hands: a keeper allowed to handle it). */
  float touchSkill(const MatchPlayer& player, bool hands) const;
  /** Whether his first touch brings the ball under control. */
  bool controlsBall(const MatchPlayer& player, float skill, float ballSpeed,
                    float heightMetres);
  /** The ball's velocity after a heavy first touch. */
  Vector2F heavyTouchVelocity(const MatchPlayer& player, Vector2F velocity);
  /** The keeper reads an open-play shot: where to dive and how long he
   * needs to react (defenders in the line of the shot screen it). */
  void readShot(const MatchPlayer& keeper, GoalkeeperControl& control);
  /** Whether a save is held rather than parried. */
  bool holdsShot(const MatchPlayer& keeper, float stretch, float speedExcess);
  /** How a parried ball comes back off the keeper. */
  struct Spill
  {
    Vector2F velocity;
    float verticalSpeed = 0.0f;
  };
  Spill parrySpill(const MatchPlayer& keeper, float speed);

  void initializePlayers(const Lineup& lineup, bool isHomeTeam);
  void loadAttributes(MatchPlayer& matchPlayer, const Player* player) const;
  /** The derived-stat recipes of the match's handshake. */
  const DerivedRecipes& derivedRecipes() const;
  /** Works out a loaded player's derived stats from his attributes. */
  void deriveStats(MatchPlayer& matchPlayer) const;
  std::size_t addPlayerStats(const MatchPlayer& matchPlayer, bool started);
  PlayerMatchStats& statsOf(const MatchPlayer& matchPlayer);
  void refreshRatings();
  /** Clock, minutes played, distance, load statistics and energy. */
  void advanceClock(float dt);
  void accumulatePlayerLoad(float dt);
  void updateMatchClock();
  void announceAddedTime();
  void endPeriod();
  /** Stops the clock for a break (half-time, before and in extra time). */
  void beginBreak(float seconds, float recovery);
  /** Level on aggregate (knockout ties). */
  bool tieLevel() const;
  void finishMatch();
  void startShootout();
  void updateShootout(float dt);
  /** A live shootout kick off the pitch (goalkeeper plan step 9): in or
   * out, and the kick stops there. */
  void shootoutBallOut();
  void beginShootoutKick();
  void finishShootoutKick(MatchEventDetail outcome);
  /** Substitutions a side may make (one more in extra time). */
  int maxSubstitutions() const;
  void checkInjuries();
  void injurePlayer(MatchPlayer& player, bool fromContact);
  /** Off the pitch for good: parked beside the dugouts, or (`walksOff`, a
   * sending-off with walk-outs; touchline plan stage 6) on foot to the
   * tunnel as a touchline body. */
  void removeFromPitch(MatchPlayer& player, bool walksOff = false);
  /** Touchline plan stage 6: a sent-off player, from where his body stands,
   * becomes a touchline body walking to the tunnel. */
  void sendToTunnel(const MatchPlayer& body);
  /** Whether a sent-off player is still on the pitch (the restart waits). */
  [[nodiscard]] bool sentOffStillOn() const;
  void rebalanceShape(bool homeTeam, Vector2F vacatedBase,
                      std::int8_t vacatedSlot);
  void ensureGoalkeeper(bool homeTeam);
  void beginStoppage();
  /** Extends the current restart delay (card, treatment, substitution). */
  void extendRestart(float seconds);
  /** At a new stoppage, each side's AI manager reviews its side once. */
  void runAiSubstitutions();
  /** A substitution: with walk-outs (touchline plan stage 5) it is set
   * under way (the player walks off, the sub walks on; true once set),
   * else made at once. */
  bool performSubstitution(MatchPlayer& outgoing, const Player* inPlayer,
                           SubstitutionReason reason);
  /** Makes the change in the slot at once: the sub becomes the slot's
   * player where its body stands (the counts, windows, stats, event). */
  bool applySubstitution(MatchPlayer& outgoing, const Player* inPlayer,
                         SubstitutionReason reason);
  /** Touchline plan stage 5, each step: the changes under way move on. */
  void updateSubstitutions(float dt);
  /** Whether a change under way holds up the restart. */
  [[nodiscard]] bool substitutionHoldsRestart() const;
  /** The change taking this slot's player off, if he is leaving now. */
  [[nodiscard]] const PendingSubstitution* leavingFrom(std::size_t slot) const;
  /** The change bringing this player on, if one is under way. */
  [[nodiscard]] const PendingSubstitution* comingOn(const Player* player) const;
  /** Where a sub waits to come on (metres): his side's spot, or his place
   * in the queue behind it when several wait (user). */
  [[nodiscard]] Vector2F substituteWaitSpot(bool home, const Player* incoming) const;
  /** A player off the pitch for good goes to his side's bench: a new
   * touchline body where his body stands (walking round to it unless he
   * is in the tunnel). */
  void benchSubstitutedPlayer(const MatchPlayer& body);
  /** What a card does: logged, counted, and a sending-off leaves. */
  void applySanction(MatchPlayer& offender, MatchRules::FoulSanction sanction);
  /** Current top speed (m/s) after fatigue, sprint reserve and injury. */
  float currentTopSpeed(const MatchPlayer& player) const;
  /** His fatigue, 0 fresh to 1 spent, as his side's body reads his fuel. */
  float fuelFatigue(const MatchPlayer& player) const;
  /**
   * Moves the players in `slots` toward their targets: the tactical target
   * (urgent when urgentMovement is set) in play, or the restart spot
   * (movementTarget) at a walk.
   */
  void integrateMovements(const std::uint8_t* slots, std::size_t count,
                          float dt, bool walking);
  /** Moves the players in `slots`, each side with its own body. */
  void movePlayers(const std::uint8_t* slots, std::size_t count, float dt,
                   bool walking);
  void updateRestartMovement(float dt);
  bool userRestartPassRequested() const;
  void updateRestartSupport(MatchPlayer& player, const MatchPlayer* taker);
  void separatePlayers();
  /** Collision plan: whether a pair meets by the new rules (both on the
   * Binturong body; otherwise today's separation). */
  bool collidesByMomentum(const MatchPlayer& first, const MatchPlayer& second) const;
  /** Touchline plan stage 2: seats each Binturong-bodied side's subs and
   * staff in its dugout and puts its manager in his technical area. */
  void setUpTouchline();
  /** Touchline plan stage 2: everyone round the touchline for a step (to
   * and from their seats and spots, or knocked about). */
  void updateTouchline(float dt);
  /** A sub has gone on: off the touchline. */
  void leaveTouchline(const Player* player);
  /** Touchline plan stage 4: whether halves start with a walk-out and
   * half-time with a walk-off (Binturong out of play, both sides on the
   * Binturong body). */
  [[nodiscard]] bool walksOut() const;
  /** Everyone out of the tunnel to the line-up (`lineUp`) or straight to
   * the kick-off; nobody is placed (but at the match's start everyone is
   * put in the tunnel first). */
  void beginWalkOut(bool homeKickingOff, bool lineUp);
  /** Half-time: everyone into the tunnel; the break runs once all are in. */
  void beginWalkOff();
  /** The match's start: everyone waiting in the tunnel (their files), then
   * the first walk-out, with the line-up and the handshake. */
  void startInTunnel();
  /** A step of the walk-out or walk-off. */
  void updateWalk(float dt);
  /** The walk-out is done: on to the kick-off as after a goal. */
  void finishWalkOut();
  /** For a touchline body during a walk: whether he waits where he is
   * (in the tunnel), else where he walks to (metres; none: his usual
   * place). */
  [[nodiscard]] std::pair<bool, std::optional<Vector2F>> touchlineWalk(std::size_t index) const;
  /** Touchline plan stage 3: where an official means to be now (metres,
   * MatchGround's frame), by the play or the restart. */
  [[nodiscard]] Vector2F officialSpot(const MatchPlayer& body,
                                      const TouchlinePerson& person);
  /** An official runs (or walks) to his spot, the referee steering round
   * players in his way; stopped, he faces the play. */
  void moveOfficial(MatchPlayer& body, const TouchlinePerson& person, Vector2F spot,
                    float dt);
  /** Whether this body is one of the touchline's (not a player's). */
  [[nodiscard]] bool onTouchline(const MatchPlayer& body) const;
  /** How long a touchline body takes to get up once it has stopped. */
  [[nodiscard]] float touchlineGetUpSeconds(const MatchPlayer& body) const;
  /** Before a movement step: where everyone starts. */
  void rememberStepStart();
  /** After a movement step: run-ins found along each path within the step
   * (wound back to the moment of contact, the hit, wound forward), easing
   * of any overlap left, then today's separation for the other pairs. */
  void collidePlayers(float dt);
  /** Collision plan step 3: judges one player's knock (its measures are
   * set): kept his feet and balance (a pause), a stumble, or a fall;
   * `remaining` is what is left of the step after the contact. */
  void judgeKnock(MatchPlayer& player, CollisionSide& side, float remaining);
  /** Which situation a knock came from (MatchStats::collisionKnocksBySituation),
   * from `player`'s view: their velocities before it and when (s). */
  std::size_t knockSituation(std::size_t slot, std::size_t otherSlot, Vector2F before,
                             Vector2F otherBefore, Vector2F where, double at) const;
  /** Step 6: the first fallen body the ball's straight path over the next
   * `seconds` meets low enough to hit: its slot and where along (0-1). */
  std::optional<std::pair<std::size_t, float>> fallenBodyInPath(float seconds) const;
  /** Step 6: the ball, at that body, comes off it (35% back off the
   * surface, 80% along it kept). */
  void bounceOffFallen(MatchPlayer& player);
  /** Who last touched the ball for a restart: a fallen body it bounced off
   * since, else its last possessor. */
  const Player* lastTouchForRestart() const;
  /** Step 4: puts him down from a knock judged a fall (the big hit, which
   * way he lies; a ball he had runs on loose). */
  void knockOver(MatchPlayer& player, CollisionSide& side);
  /** Back on his feet: standing still, facing the way he slid. */
  void standUp(MatchPlayer& player);
  /** Step 5: whether a fallen player, stopped and past a big hit's least
   * time down, starts getting up now; how long it takes (s), else 0 (the
   * side's brain). */
  float chooseGetUp(const MatchPlayer& player);
  Vector2F goalkeeperTarget(MatchPlayer& keeper, const MatchPlayer* carrier);
  void diveGoalkeeper(MatchPlayer& keeper, float dt);
  void planGoalkeeperDive(bool defendingHome, bool penalty);
  /** A Binturong keeper's blind penalty guess (goalkeeper plan step 9),
   * made as the taker sets off for a strike at `strikeAt` (simulated s):
   * a side, low or high, pushed off around the strike; or he stays. */
  void planPenaltyDiveBinturong(MatchPlayer& keeper, double strikeAt);
  /** An in-match penalty's strike, once the taker has set off (step 9). */
  std::optional<double> penaltyStrikeAt;
  /** Where each keeper (index 0 home) goes to claim a high ball, when he
   * meets it first (goalkeeper plan step 7; set at each plan). */
  std::array<std::optional<Vector2F>, 2> keeperClaimMeet{};
  void resolveShotAtGoalkeeper(MatchPlayer& goalkeeper);
  /** A keeper's claim, or a duel for a high ball (true when it was played).
   * A lone side under it on a Binturong body is left to headInSweep. */
  bool resolveAerialContest();
  /** The unchallenged header (heading technique plan step 4): a Binturong
   * body meets a high ball where it passes through his head's reach, the
   * first on its path over the last stretch (true when one did). */
  bool headInSweep();
  /** The part of his head of the last unchallenged header. */
  std::optional<HeadPart> lastHeadPart;
  /** The header sequence under way (MatchStats::headerSequences): headers
   * so far, and whether the last was by the home side. */
  int headerRun = 0;
  bool headerRunLastHome = false;
  /** Ends the header sequence under way once someone has the ball or play
   * has stopped (read at the start of each step). */
  void trackHeaderRun();
  void headBall(MatchPlayer& header);
  /** An Aardvark body: a defender's touch in front of his own goal that now
   * and then goes toward it instead (true when it did). A Binturong body
   * has no such roll: its own goals come from the contact. */
  bool tryOwnGoalTouch(MatchPlayer& defender);
  /** Puts the ball over his own goal line, wide of the posts (`headed`:
   * with his head). */
  void clearBehind(MatchPlayer& defender, bool headed = false);
  void clearBall(MatchPlayer& defender);
  void parryShot(MatchPlayer& goalkeeper, bool overTheBar);
  /** Releases a restart the way the taker's brain chooses. */
  void takeSetPiece(MatchPlayer& taker, MatchState restartState);
  void arrangeSetPiece(bool attackingHome, Vector2F ballPosition);
  /**
   * Taker of a dead-ball duty right now: the side's designated player while
   * he is on the pitch and fit (so substitutions are followed), otherwise
   * bestSetPieceTaker(). Long throws have no automatic specialist (nullptr).
   */
  MatchPlayer* dutyTaker(bool homeTeam, SetPieceDuty duty);
  MatchPlayer* restartTaker();
  void setRestartTaker(MatchPlayer* taker);
  std::size_t slotOf(const MatchPlayer& player) const;
  float familiarityOf(const MatchPlayer& player) const;
  /** Current team-talk modifier of a side (by the current half). */
  float talkOf(bool homeTeam) const;
  // Roles, opposition instructions and team talks (see model/tactics.h).
  /** Reads roles and opposition instructions from a side's tactics. */
  void resolveTactics(bool homeTeam);
  const RoleProfile& roleProfileOf(const MatchPlayer& player) const;
  OppositionInstruction instructionAgainst(const MatchPlayer& target) const;
  /** Pressure in [0, 1] on a carrier forced onto his weaker foot. */
  float weakFootPressure(const MatchPlayer& carrier) const;
  /** Take-on success lost to a second defender doubling up. */
  float doubleUpPenalty(const MatchPlayer& carrier,
                        const MatchPlayer& defender) const;
  /** Team-talk effect in [-1, 1] (see getTeamTalkEffect). */
  float talkSwing(bool homeTeam) const;
  StrategySliders computeEffectiveSliders(bool homeTeam) const;
  /** The touchline shout's nudge to a side's sliders. */
  StrategySliders shoutSliders(StrategySliders sliders, bool homeTeam) const;
  /** How clearly each side is the weaker one in [0, 1] (see Touchline). */
  void computeUnderdogShares();
  void refreshEffectiveSliders();
  /** Team execution edge: home crowd, team talk and numerical advantage. */
  float teamEdge(bool homeTeam) const;
  /** Per-match referee strictness from the draw and the context. */
  float drawRefereeStrictness() const;
  /** Work-rate bonus from an encouraging shout. */
  float shoutWorkRate(bool homeTeam) const;
  void refreshTargetBlends();
  /** A side's man-marking assignments (markAssignments). */
  void assignMarks(bool home);
  /** Changes the ticks per step (the fidelity) and the per-step blends. */
  void setStepTicks(std::uint32_t ticks);
  /** Ticks of the next background-fidelity step (see MatchFidelity). */
  std::uint32_t backgroundStepTicks() const;
  /** Seconds simulated by one step at the current fidelity. */
  float stepSeconds() const;
  /** Whether the last step crossed a multiple of `ticks` (a periodic job is
   * due); every `ticks` steps at full fidelity. */
  bool periodElapsed(std::uint64_t ticks) const;
  float hashNoise(std::uint32_t salt, std::uint32_t key) const;
  /** Like hashNoise, but constant over windows of `epochSteps` steps. */
  float epochNoise(std::uint32_t salt, std::uint32_t key,
                   std::uint32_t epochSteps) const;
  float executionErrorScale(const MatchPlayer& player) const;
  void recordPassCompletion(MatchPlayer& receiver);
  /** Credits a completed pass from `passer` (a team-mate) to `receiver`. */
  void creditPassCompletion(MatchPlayer& receiver, const Player* passer);
  void captureInterpolationFrame();
  void simulateStep(float dt);
  void updateTeamPhases();
  void updateMovement(float dt);
  /** Movement jitter: the plan just refreshed (scheduled or forced). */
  void observePlanJitter(bool scheduled);
  /** Movement jitter: the bodies just moved. */
  void observeRunJitter(float dt);
  /** A side's shape and every off-ball target (see updateMovement). */
  void refreshTacticalTargets(float dt, bool planHome);
  /** Integrates the free ball in sub-steps, resolving contacts in each. */
  void updateBall(float dt);
  void resolvePossessionAndActions(float dt);
  /** Moves the dribbled ball with its carrier; false if a heavy touch lost it.
   */
  bool updateDribble(MatchPlayer& carrier, float dt);
  /** Share of the current touch the ball is still away from the foot. */
  float dribbleExposureShare() const;
  /** Tries to beat a jockeying defender; true if the carrier kept the ball. */
  bool attemptTakeOn(MatchPlayer& carrier, MatchPlayer& defender);
  // Contest halves: what one player's body is worth in a duel, in the
  // duel's own units (see match_engine_contests_plan.md); the contest adds
  // them up, rolls once and plays the outcome. `terms`, when given, receives
  // the half's named parts, each counted positive.
  /** Take-on, carrier: success chance points from his Dribbling and Pace. */
  float takeOnMake(const MatchPlayer& carrier, ScoreBreakdown* terms) const;
  /** Take-on, defender: success chance points he takes away. */
  float takeOnResist(const MatchPlayer& defender, ScoreBreakdown* terms) const;
  /** Tackle, tackler: win chance points (MatchRules::tackleMake). */
  float tackleMake(const MatchPlayer& tackler, ScoreBreakdown* terms) const;
  /** Tackle, carrier: win chance points he takes away
   * (MatchRules::tackleResist). */
  float tackleResist(const MatchPlayer& carrier, bool shielding,
                     ScoreBreakdown* terms) const;
  /** Tackle, tackler: foul propensity points from his technique
   * (MatchRules::tackleTechnique). */
  float tackleTechnique(const MatchPlayer& tackler,
                        ScoreBreakdown* terms) const;
  /** Aerial duel: a candidate's strength for a ball at this height (0 when
   * he cannot reach it); the winner is drawn in proportion. */
  float aerialStrength(const MatchPlayer& player, float ballHeightMetres) const;
  /** A keeper coming for a high ball in his box: chance he holds it, with
   * `crowding` opponents around the ball. */
  float claimChance(const MatchPlayer& keeper, int crowding) const;
  /** A defender beaten by a take-on is wrong-footed: the momentum he loses
   * and how long before he can challenge again. */
  void recoverFromTakeOn(MatchPlayer& defender);
  /** Swept contact over the stretch of ball sub-step about to happen: who
   * the ball meets on its way, in order. A Binturong body plays it where
   * the ball reaches him (contactAlong says how far along). `beforeLine`:
   * the ball is heading out of play, and only a Binturong body meeting it
   * while it is still on the pitch can play it first. */
  void resolveLooseBall(bool beforeLine);
  void attemptTackle(MatchPlayer& carrier, MatchPlayer& defender, bool sliding);
  /** The carrier's turn: the brain chooses, the body carries it out. */
  void decideAction(MatchPlayer& carrier);
  /** Applies input records due at the current step. */
  void applyDueInputs();
  /** Performs an external tactical change (live or replayed); false when it
   * is not allowed, in which case nothing changed. */
  bool executeCommand(const MatchCommandRecord& command);
  /** Logs a live change that executed (truncating any pending replay). */
  bool recordCommand(const MatchCommandRecord& command);
  void applyDueCommands();
  void startShout(bool homeTeam, MatchShout shout);
  /** Base positions of a side's outfield players from its formation slots
   * (a side short of players also sits a little deeper). */
  void placeFormation(bool homeTeam);
  /** Tactical familiarity in force: the drilled level less a reshape cost. */
  float effectiveFamiliarity(std::size_t team) const;
  bool isControlled(const MatchPlayer& player) const;
  /** Stick-driven movement of the controlled player (fine sub-steps). */
  void integrateControlled(MatchPlayer& player, float dt);
  /** Performs the controlled carrier's action; true if the ball left him. */
  bool performControlledAction(MatchPlayer& carrier);
  /** Stick direction of the controlled pass or shot (metres, unit length). */
  Vector2F controlledAim(const MatchPlayer& carrier) const;
  /** Team-mate a controlled pass along `aim` goes to (nullptr: none). */
  MatchPlayer* controlledPassReceiver(MatchPlayer& carrier, Vector2F aim,
                                      bool throughBall);
  /** Pass, through ball or lofted ball of the controlled carrier. */
  void playControlledPass(MatchPlayer& carrier, MatchInputAction action);
  /** The controlled shot's aim, lift and spread (strikeShot calls it). */
  void shapeControlledShot(const MatchPlayer& shooter, float halfGoal,
                           float inset, float& aimY, float& aimZ,
                           float& spread) const;
  /** Movement target of the controlled player with the stick idle (meeting
   * a pass to him, or the goal-side spot while jockeying); nullopt to stand. */
  std::optional<Vector2F> controlledAssistTarget(
      const MatchPlayer& player) const;
  /** Seconds a team-mate needs to get to the ball (see
   * suggestActivePlayer). */
  float switchSeconds(const MatchPlayer& candidate) const;
  void passBall(MatchPlayer& passer, const PassOption& option,
                bool forceLofted = false);
  /** `aim`: the spot he picked when he decided (a held shot); none, he
   * picks it now. */
  void takeShot(MatchPlayer& shooter, float forcedXG = -1.0f,
                bool header = false, bool freeKick = false,
                std::optional<ShotAim> aim = std::nullopt);
  /** Strikes a shot at goal (aim, execution error, launch) and returns its
   * xG; takeShot() adds the statistics and the event. */
  float strikeShot(MatchPlayer& shooter, float forcedXG, bool header,
                   bool penalty, bool freeKick = false,
                   std::optional<ShotAim> aim = std::nullopt);
  /** The spot in the goal for a shot he is deciding on now (a drill's
   * forced spot replaces his pick). */
  ShotAim pickShotAim(MatchPlayer& shooter, bool header, bool penalty,
                      bool freeKick);
  struct SaveAttempt
  {
    bool saved = false;
    float stretch = 0.0f;
    float speedExcess = 0.0f;
    float crossingHeight = 0.0f;
  };
  /** Whether the keeper stops the shot crossing his plane in the last ball
   * sub-step (reach and save roll only, no consequences). */
  SaveAttempt attemptSave(const MatchPlayer& keeper);
  /** Gives the player the ball, put still at his feet (takePossession, and
   * the ball placed). */
  void setPossession(MatchPlayer& player);
  /** A tackle on a free ball: counted as attempted now, and won if his side
   * next has the ball under control (settleTackle). */
  void noteFreeBallTackle(MatchPlayer& tackler, bool contest = false);
  /** `homeTeam`'s side has the ball under control: a pending free-ball
   * tackle is won if that is the tackler's side, lost otherwise. */
  void settleTackle(bool homeTeam);
  /** The ball's path has just changed (a touch, kick or deflection). */
  void notePathChange();
  /** Engine seconds on the ball's clock: inside the ball's sub-steps, the
   * sub-step's own time. */
  double ballNow() const;
  /** Opt-in observations; attached recorders are detached on engine copy. */
  void observePossession(PossessionObservation observation);
  void observePossessionGate(const MatchPlayer& player, PossessionContactGate gate);
  double ballClockSeconds = 0.0;
  bool ballClockRunning = false;
  /** Whether `player` can contest `owner`'s collected ball while it is
   * inside the owner's reach (tackling plan step 2): an outfield opponent,
   * both on the Binturong body. */
  bool contestsBall(const MatchPlayer& player, const MatchPlayer& owner) const;
  /** The tackler has played the owner's ball inside both reaches: both
   * feet's cooldowns (rows 1, 2 and 5 of the step 2 table). */
  /** What a contest touch meets (tackling plan, the step 2 table as step
   * 4 left it). */
  enum class ContestMeet : std::uint8_t
  {
    /** Rows 1 and 2: the tackler's touch alone (the carrier recovering,
     * or swinging at nothing). */
    UNOPPOSED,
    /** Row 3: the carrier's planned touch in the same moment: a block. */
    BLOCK_COMMITTED,
    /** Row 5: the carrier's foot free, his touch not due: it reacts, a
     * wall: a block. */
    BLOCK_REACTING,
  };
  ContestMeet commitContestTouch(MatchPlayer& tackler, MatchPlayer& owner);
  /** A block (tackling plan step 4): both feet on the ball, each a push
   * (set by Tackling, less at a stretch, at speed, or reacting), each
   * through the contact model; opposed along the line between them (the
   * squeeze), added across it (the escape), a strong near-equal squeeze
   * popping it up. The ball goes off loose; both feet are committed. */
  void playBlock(MatchPlayer& tackler, MatchPlayer& owner,
                 const TouchMoment& moment, bool committed);
  /** A block push's strength (m/s, as if unopposed). */
  float blockPushBinturong(const MatchPlayer& player, Vector2F contact,
                           bool reacting) const;
  /** A contest swing at nothing: his foot committed for `seconds`. */
  void swingAtNothing(MatchPlayer& player, float seconds, bool carrier);
  struct PendingTackle
  {
    const Player* tackler = nullptr;
    bool homeTeam = false;
    bool contest = false;
  };
  /** A free-ball tackle not yet won or lost; play stopping lapses it. */
  std::optional<PendingTackle> pendingTackle;
  /** Gives the player the ball without moving it: for a contact that sends
   * it on from where he met it. */
  void takePossession(MatchPlayer& player);
  /** Nobody owns the ball any more (kicked, lost, stopped, removed). */
  void clearPossession();
  /** Gives the ball to a player outside open play (restarts, substitutes,
   * scenarios): ownership only, none of setPossession's touch bookkeeping. */
  void handBallTo(const Player* holder);
  /** The touch when no brain chooses one: set it just ahead of him, toward
   * the goal he attacks. */
  TouchIntent defaultTouchIntent(const MatchPlayer& player) const;
  /** Plays the touch where he meets the ball and claims it. */
  /** `forced`: this intent instead of the brain's (a tackle's nudge). */
  void playFirstTouch(MatchPlayer& player, const TouchMoment& moment,
                      const TouchIntent* forced = nullptr);
  /** A tackle on a free ball played with its intent (tackling plan step
   * 3): the nudge, a clearance or a first-time pass, from where he meets
   * it. False for control (played as his first touch). */
  bool playTackleIntent(MatchPlayer& tackler, const MatchPlayer& attacker,
                        const TouchMoment& moment);
  /** What his tackle is for: a drill's forced intent, or his brain's
   * (tackling plan step 5). */
  TackleIntent tackleIntentOf(const MatchPlayer& tackler,
                              const MatchPlayer& attacker, Vector2F contact,
                              bool freeBall);
  /** How far he would stretch for the ball at `point` (0 at his feet, 1 at
   * full stretch; the stretch map). */
  float ballStretch(const MatchPlayer& player, Vector2F point) const;
  /** Where a dribbler wants to go: the metric direction and the pace he
   * wants (m/s): the AI toward his plan's target (ball.dribbleGoal) at the
   * pace his brain would run there, the controlled player along his stick. */
  struct DribbleAim
  {
    Vector2F direction{1.0f, 0.0f};
    float speed = 0.0f;
    /** How far his target is (m); none for the stick. */
    std::optional<float> metres;
  };
  /** A dribbler's route turn ahead of a touch (dribble meet plan): how far
   * it starts (m), the fastest the ball may be rolling there for him to
   * take it with the ball (m/s), and the furthest a push may go (a little
   * into the turn). All unlimited off a route. */
  struct DribbleTurnAhead
  {
    float toTurn = std::numeric_limits<float>::max();
    float turnSpeed = std::numeric_limits<float>::max();
    float furthest = std::numeric_limits<float>::max();
    /** How hard he brakes toward it (m/s^2), as his body would. */
    float braking = 0.0f;
    /** The turn is a stop (a sharp reversal): the ball is to get there
     * with `turnSpeed` left for him to stop it with a touch. */
    bool stop = false;
  };
  /** `from`: where he plays it (normalised pitch). A Binturong body then
   * aims from there (dribbleOutFromBinturong). */
  DribbleAim dribbleAim(const MatchPlayer& player,
                        std::optional<Vector2F> from = std::nullopt) const;
  /** How far a dribble touch at `pace` (m/s) puts the ball ahead of where
   * he meets it: into his stride at pace, else the set offset; never past
   * his goal `toGoal` (m) away. */
  float dribblePushMetres(const MatchPlayer& player, float pace,
                          std::optional<float> toGoal) const;
  /** The owner of a collected ball meets it in the sweep: whether it is due
   * a touch (he is catching it up, it is off his way, or it lies still and
   * he wants to go) or can run on. */
  bool dribbleTouchDue(const MatchPlayer& player, const TouchMoment& moment) const;
  /** What a dribble touch tries: into his stride at the pace he wants, or a
   * short set touch when he goes slowly. */
  TouchIntent dribbleTouchIntent(const MatchPlayer& player,
                                 const TouchMoment& moment) const;
  /** Plays his own ball on from where he meets it; he keeps the claim. */
  void playDribbleTouch(MatchPlayer& player, const TouchMoment& moment);
  /** A claimed ball, each step: collected once he reaches it, and how far
   * it is from him (for tackles). */
  void updateTouch(MatchPlayer& carrier, float dt);
  /** The owner of a touched ball runs to meet it (every step, after the
   * brains plan, so the plan never sends him elsewhere). A collected ball
   * on a Binturong body: he plans the meet (planDribbleMeetBinturong). */
  void chaseTouchedBall();
  // Feeds the tracker (match_engine_tracking.cpp); no effect when it is off.
  void trackTouch(const MatchPlayer& player);
  void trackPassRelease(const MatchPlayer& passer);
  void trackPassCompletion(const MatchPlayer& passer,
                           const MatchPlayer& receiver);
  void trackShot(const MatchPlayer& shooter, float xg, bool setPiece,
                 bool header, bool penalty);
  /** Pressing players close to the carrier apply a pressure. */
  void trackPressures();
  void clearFlightState();

  /** @param detail Debugger detail: the utility and completion terms. */
  PassOption evaluatePassOption(MatchPlayer& passer, MatchPlayer& receiver,
                                PassCandidateDetail* detail = nullptr) const;
  /** Whether the attached recorder asks for score breakdowns. */
  [[nodiscard]] bool wantsDetail() const;
  MatchPlayer* findClosestPlayer(Vector2F position, bool homeTeam,
                                 bool includeGoalkeeper = true);
  MatchPlayer* findGoalkeeper(bool homeTeam);
  MatchPlayer* findMatchPlayer(const Player* player);
  float nearestOpponentDistance(const MatchPlayer& player) const;
  float openSpaceAhead(const MatchPlayer& carrier) const;
  float passingLaneRisk(const MatchPlayer& passer,
                        const MatchPlayer& receiver) const;
  float estimateShotXG(const MatchPlayer& shooter) const;
  /** Whether `receiver` stands offside against the given offside line
   * (see offsideLine()), with an extra tolerance for misjudged lines. */
  bool isOffside(const MatchPlayer& receiver, bool attackingHome,
                 float defenderLine, float lineTolerance = 0.0f) const;
  float offsideLine(bool attackingHome) const;

  void makeSave(MatchPlayer& goalkeeper);
  void setupKickOff(bool homeKickingOff);
  void resetPositions();
  /** Kick-off formation spot of a player (his own half). */
  Vector2F kickOffPosition(const MatchPlayer& player) const;

  void recordHighlight(MatchEventType type);
  float attribute(const Player* player, std::string_view name) const;
  /** Rating of an attribute in [0, 1] before the stretch. */
  float rawAttribute(const Player* player, std::string_view name) const;
  /** Match level from both elevens (see MatchTuning::Player). */
  void computeLevel(const Lineup& home, const Lineup& away);
  /** Uniform in [minimum, maximum) from the play stream (portable). */
  float randomFloat(float minimum, float maximum);
  /** Uniform in [0, 1) from the referee/injury stream (portable). */
  float incidentRoll();
  bool isHomePlayer(const Player* player) const;
  MatchEvent& logEvent(MatchEventType type);
  MatchEvent& logEvent(MatchEventType type, const MatchPlayer& actor);
  /** Writes the commentary line of the events logged since the last call. */
  void describePendingEvents();
  /** Display name of a squad player (empty when unknown). */
  std::string squadPlayerName(PlayerID playerId) const;
  /** One fixed step of the simulation (simulateStep adds the commentary). */
  void simulateStepBody(float dt);

  // --- Referee: the officials' judgement (match_referee.h) ------------------
  // One per match; each dispatches to it (match_referee_dispatch.cpp).

  /** A foul by `offender` on `victim`: counted, then dealt with - sanction,
   * advantage or the restart (`ballWon`: the challenge reached the ball
   * first; `reckless`: a sliding tackle from behind). */
  void commitFoul(MatchPlayer& offender, MatchPlayer& victim, bool ballWon,
                  bool reckless);
  /** An advantage being played: ends it, or calls back the free kick. */
  void updatePendingAdvantage(float dt);
  /** Whether `passer`'s pass to `option`'s receiver is given offside, the
   * defenders' line at `defenderLine` (normalised x). */
  bool judgeOffside(const MatchPlayer& passer, const PassOption& option,
                    float defenderLine) const;
  /** Whole added minutes due for the current period from its stoppages. */
  int addedMinutesDue() const;

  // --- Player body: meeting and striking the ball ---------------------------
  // Each runs with the body of the player's side (match_body_dispatch.cpp).

  /** What kind of strike the ball is leaving from (see releaseFrom). */
  enum class ReleaseKind : std::uint8_t
  {
    PASS,
    SHOT,
    HEADED_SHOT,
    CLEARANCE,
    CLEAR_BEHIND,
    HEADER,
  };
  /** Where and how high the ball leaves a strike: `position` and `z` (length
   * units, as MatchBall::z); `heightMetres` the same height in metres, which
   * a shot's flight is worked out from. */
  struct BallRelease
  {
    Vector2F position{};
    float z = 0.0f;
    float heightMetres = 0.0f;
  };
  /** Where his body releases the ball for a strike of this kind. */
  BallRelease releaseFrom(const MatchPlayer& kicker, ReleaseKind kind) const;
  /** His head meets the ball: it is where his body plays it from. */
  void meetWithHead(const MatchPlayer& header);
  /**
   * Sends the ball from `from` (where his body released it) toward `target`
   * (normalised) with a horizontal speed and vertical speed in m/s; the
   * kicker is locked out of touching it again for a moment. Shared: it only
   * sets the ball off as the body released it.
   */
  void launchBall(const MatchPlayer& kicker, const BallRelease& from,
                  Vector2F target, float horizontalSpeed, float verticalSpeed,
                  BallSpin spin);
  /** What every contact does to the ball (unified kick plan): it leaves
   * from `position` (normalised) at height `z` (length units) with the
   * velocity off his foot, played by `kicker`, who may not touch it again
   * for `kickerLockout` seconds. Possession is the caller's. */
  void sendBallOff(const MatchPlayer& kicker, Vector2F position, float z,
                   Vector2F velocity, float velocityZ, BallSpin spin,
                   float kickerLockout);
  /** A touch's record on the ball: what he tried, how it went, and how far
   * it is from him now. */
  void noteTouchOnBall(const MatchPlayer& player, const TouchIntent& intent,
                       const TouchResult& touch, const TouchMoment& moment);
  /** Whether this contact is played with the new first touch (a feet-height
   * ball played on under physics, not trapped dead). */
  bool playsFirstTouch(const MatchPlayer& player, bool hands,
                       float heightMetres) const;
  /** How far from his centre he can play a ball at his feet. */
  float feetReach(const MatchPlayer& player) const;
  /** Whether he could play the ball where it is now: within his feet reach
   * and not behind him beyond it (his foot could be on it). */
  bool ballInReach(const MatchPlayer& player) const;
  /** The lowest and highest head lines of the outfielders on the pitch:
   * above the lowest the ball is a header for someone, above the highest
   * for everyone. */
  std::pair<float, float> headLineRange() const;
  float lowestHeadLine() const;
  /** Whether a ball `gap` metres away (`gapX`, `gapY` from the ball to him)
   * is behind him beyond his reach (a dead zone), or will be once his body
   * has turned for `turnSeconds` the way it turns now. */
  bool ballBehindReach(const MatchPlayer& player, float gapX, float gapY,
                       float gap, float turnSeconds = 0.0f) const;
  /** Where his strike starts. */
  Vector2F strikePoint(const MatchPlayer& striker) const;
  /** A strike towards `target` at `speed` over the ground and `lift`
   * upward, as `kind`: where it really goes and how. */
  Kick strikeAs(const MatchPlayer& striker, StrikeKind kind, Vector2F target,
                float speed, float lift);
  /** How far a clearance struck from `origin` toward `aim` is lofted for. */
  float clearanceDistance(const MatchPlayer& defender, Vector2F origin,
                          const ClearanceAim& aim) const;
  /** A direct free kick: blocked by the wall, or a shot over it. */
  void takeDirectFreeKick(MatchPlayer& taker);

  // --- Out of play: the ball out and restarts (match_out_of_play.h) --------
  // One per match; each dispatches to it (match_out_of_play_dispatch.cpp).

  /** The ball over a line this sub-step: a goal, the woodwork, a corner,
   * goal kick or throw-in. */
  void checkOutOfBounds();
  /** Whether a ball at `at` is still in play (not over a line), by the
   * match's out-of-play version. */
  bool ballInPlayAt(Vector2F at) const;
  /** A goal for the home side (`homeTeam`) or the away side. */
  void scoreGoal(bool homeTeam);
  /** The kick-off once the goal celebration is over. */
  void kickOffAfterGoal(bool homeKickingOff);
  /** During a restart: the ball on its way back to the spot. */
  void updateDeadBall(float dt);
  /** During a restart: his restart position kept where the laws allow
   * him (not the taker's). */
  void keepRestartDistance(MatchPlayer& player);
  /** The woodwork in the free ball's way over the next `seconds`: the share
   * of the way it gets before it meets it (none, by Aardvark). */
  std::optional<float> frameInPath(float seconds) const;
  /** The free ball off the woodwork where it meets it. */
  void bounceOffFrame();
  /** Where the restart is taken from: the spot the ball is on its way back
   * to, or the ball. */
  Vector2F restartSpot() const
  {
    return deadBall.active ? deadBall.spot : ball.position;
  }
  /** The ball settling in the net during the celebration. */
  void updateBallInNet(float dt);
  void setupThrowIn(bool homeTeam);
  void setupGoalKick(bool homeTeam);
  void setupCorner(bool homeTeam, bool topCorner);
  void setupFreeKick(bool homeTeam, Vector2F foulPos);
  void setupPenalty(bool homeTeam);
  /** Puts the taker on the spot; anyone standing there steps aside. */
  void placeTaker(MatchPlayer& taker, Vector2F spot);
  /** The restart's wait is over: the taker plays it. */
  void completeRestart();

  // --- AI manager: a managed side's touchline (match_manager.h) -------------
  // Per side; each dispatches to that side's manager
  // (match_manager_dispatch.cpp).

  /** A side's substitutions at a stoppage (injuries, medical limits,
   * fatigue, cards, the score). */
  void runAiSubstitutionsFor(bool homeTeam);
  /** The bench player a side brings on for `role` (a keeper or not). */
  const Player* chooseReplacement(bool homeTeam, PlayerRole role,
                                  bool wantGoalkeeper) const;
  /** Shouts to the score late in the game. */
  void runAiTouchline(bool homeTeam);
  /** The AI manager of a side. */
  ManagerId managerOf(bool homeTeam) const { return managers[homeTeam ? 0 : 1]; }

  // --- Ball physics: the ball on its own (match_ball_physics.h) -------------
  // One per match; each dispatches to it (match_ball_physics_dispatch.cpp),
  // as does stepFlight.

  /** Pure ball physics for one sub-step (no contacts or rules). */
  void integrateBall(float dt);
  /** Launch speed for a ground ball to arrive at `arrivalSpeed`. */
  float groundLaunchSpeed(float distanceMetres, float arrivalSpeed) const;
  /** Vertical launch speed so the ball is at `arrivalHeight` metres when
   * `distanceMetres` from where it was struck, launched with `spin` (the
   * Binturong ball physics flies it; the Aardvark one ignores it). */
  float loftVerticalSpeed(float distanceMetres, float horizontalSpeed,
                          float startHeight, float arrivalHeight,
                          BallSpin spin = {}) const;
  /** How far round (rad, + the way a positive sidespin turns it) the
   * sidespin of a ball launched `along` and `up` (m/s) from
   * `startHeight` (m) has carried it by the time it is `distanceMetres`
   * from where it was struck: aimed that much the other way, it gets there
   * (0 with the Aardvark ball physics). */
  float curlAngle(float distanceMetres, float along, float up,
                  float startHeight, BallSpin spin) const;

  // The bodies' own members, one file per body (after every type they use;
  // the functions in "Player body" above dispatch to them,
  // match_body_dispatch.cpp). Likewise the handshakes', referees', managers'
  // ball physics' and out of play's.
#include "model/match_body_aardvark_members.h"
#include "model/match_body_binturong_members.h"
#include "model/match_handshake_aardvark_members.h"
#include "model/match_handshake_binturong_members.h"
#include "model/match_referee_aardvark_members.h"
#include "model/match_referee_binturong_members.h"
#include "model/match_manager_aardvark_members.h"
#include "model/match_manager_binturong_members.h"
#include "model/match_out_of_play_aardvark_members.h"
#include "model/match_out_of_play_binturong_members.h"
#include "model/match_ball_physics_aardvark_members.h"
#include "model/match_ball_physics_binturong_members.h"
};
