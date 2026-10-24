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
#include <limits>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "global/stats_config.h"
#include "global/types.h"
#include "model/lineup.h"
#include "model/match_context.h"
#include "model/match_events.h"
#include "model/match_rules.h"
#include "model/match_scenario.h"
#include "model/match_tracking.h"
#include "model/match_tuning.h"
#include "model/strategy.h"
#include "model/tactics.h"

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
  FULL_TIME
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

  float facingAngle = 0.0f;
  float targetAngle = 0.0f;
  float turnRate = MatchTuning::Player::FACING_TURN_RATE_RADIANS;

  bool isTrapping = false;
  float trapTimer = 0.0f;
  bool isDiving = false;
  float diveTimer = 0.0f;
  bool isPressing = false;
  bool isMakingRun = false;

  /** Slow energy pool (match condition) in [MINIMUM_STAMINA, 1]. */
  float stamina = 1.0f;
  /** Fast repeat-sprint reserve in [0, 1]. */
  float sprintReserve = 1.0f;
  /** Fresh top speed (m/s), maximum acceleration A0 and braking (m/s^2). */
  float maxSpeed = MatchTuning::Player::TOP_SPEED_BASE;
  float acceleration = MatchTuning::Player::ACCELERATION_BASE;
  float braking = MatchTuning::Player::BRAKING_BASE;
  bool isSprinting = false;
  float tackleCooldown = 0.0f;
  float actionCooldown = 0.0f;

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

/**
 * The ball. `position` is normalised; `z` is the height in length units
 * (z * MatchTuning::Units::BALL_Z_METRES = metres) for renderers. Velocities
 * are in m/s and `curve` is the heading rotation rate in rad/s.
 */
struct MatchBall
{
  Vector2F position{MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE};
  float z = 0.0f;
  Vector2F velocity{0.0f, 0.0f};
  float velocityZ = 0.0f;
  float curve = 0.0f;

  const Player* possessedBy = nullptr;
  const Player* lastPossessor = nullptr;
  const Player* intendedReceiver = nullptr;
  /** The player who last kicked or deflected the ball. */
  const Player* kicker = nullptr;
  /** Seconds during which the kicker cannot touch the ball again. */
  float kickerLockout = 0.0f;
  /** Players (by slot bit) who already tried to reach this ball in flight. */
  std::uint32_t touchAttempts = 0;
  /** While dribbled: metres ahead of the carrier's feet and the metric
   * direction of the last touch. */
  float dribbleExposure = 0.0f;
  float dribbleTouchLength = 0.0f;
  Vector2F dribbleDirection{1.0f, 0.0f};
  float touchTimer = 0.0f;

  bool isPass = false;
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
  /** Lofted delivery (cross, corner, long ball) that can be contested. */
  bool isAerialDelivery = false;
  /** Thrown in and untouched since: it cannot score directly. */
  bool fromThrowIn = false;
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
  TEAM_TALK
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
  MatchEngine(const Lineup& home_lineup, const Lineup& away_lineup,
              const Strategy& home_strat, const Strategy& away_strat,
              const StatsConfig& config, uint32_t seed);

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
  /** Fixed steps simulated since kick-off. */
  std::uint64_t getSimulatedSteps() const { return stepCounter; }

  const std::vector<MatchPlayer>& getPlayers() const { return players; }
  const MatchBall& getBall() const { return ball; }
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
  Vector2F getPreviousBallPosition() const { return previousBallPosition; }
  float getPreviousBallZ() const { return previousBallZ; }

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

 private:
  enum class TacticalArrival
  {
    MIDFIELDER,
    FAR_POST,
    FULLBACK
  };

  std::vector<MatchPlayer> players;
  MatchBall ball;
  const StatsConfig& statsConfig;
  Strategy homeStrategy;
  Strategy awayStrategy;
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

  std::vector<Vector2F> previousPlayerPositions;
  std::vector<float> previousPlayerFacingAngles;
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
  };
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
  std::array<std::uint8_t, 32> separationOrder{};
  std::size_t separationCount = 0;
  /** Positions after the last separation pass and whether it found no
   * contact (an unchanged, contact-free field needs no new pass). */
  std::array<Vector2F, 32> separationPositions{};
  bool separationClear = false;
  /** Ball situation at the last tactical refresh (event-driven refresh). */
  const Player* tacticalOwner = nullptr;
  std::uint8_t tacticalFlags = 0;
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
  };
  void initializePlayers(const Lineup& lineup, bool isHomeTeam);
  void loadAttributes(MatchPlayer& matchPlayer, const Player* player) const;
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
  void beginShootoutKick();
  void finishShootoutKick(MatchEventDetail outcome);
  /** Substitutions a side may make (one more in extra time). */
  int maxSubstitutions() const;
  void checkInjuries();
  void injurePlayer(MatchPlayer& player, bool fromContact);
  void removeFromPitch(MatchPlayer& player);
  void rebalanceShape(bool homeTeam, Vector2F vacatedBase,
                      std::int8_t vacatedSlot);
  void ensureGoalkeeper(bool homeTeam);
  void beginStoppage();
  /** Extends the current restart delay (card, treatment, substitution). */
  void extendRestart(float seconds);
  void runAiSubstitutions();
  void runAiSubstitutionsFor(bool homeTeam);
  /** Takes off the side's players the medical staff limited to an hour. */
  void runMedicalSubstitutions(bool homeTeam);
  bool performSubstitution(MatchPlayer& outgoing, const Player* inPlayer,
                           SubstitutionReason reason);
  const Player* chooseReplacement(bool homeTeam, PlayerRole role,
                                  bool wantGoalkeeper) const;
  void commitFoul(MatchPlayer& offender, MatchPlayer& victim, bool ballWon,
                  bool reckless);
  void applySanction(MatchPlayer& offender, MatchRules::FoulSanction sanction);
  bool deniesGoalChance(const MatchPlayer& victim,
                        const MatchPlayer& offender) const;
  void updatePendingAdvantage(float dt);
  /** Current top speed (m/s) after fatigue, sprint reserve and injury. */
  float currentTopSpeed(const MatchPlayer& player) const;
  /**
   * Moves the players in `slots` toward their targets: the tactical target
   * (urgent when urgentMovement is set) in play, or the restart spot
   * (movementTarget) at a walk.
   */
  void integrateMovements(std::span<const std::uint8_t> slots, float dt,
                          bool walking);
  void updateRestartMovement(float dt);
  bool userRestartPassRequested() const;
  void updateRestartSupport(MatchPlayer& player,
                            const MatchPlayer* taker) const;
  void separatePlayers();
  Vector2F goalkeeperTarget(MatchPlayer& keeper, const MatchPlayer* carrier);
  void diveGoalkeeper(MatchPlayer& keeper, float dt);
  void planGoalkeeperDive(bool defendingHome, bool penalty);
  void resolveShotAtGoalkeeper(MatchPlayer& goalkeeper);
  bool resolveAerialContest();
  void headBall(MatchPlayer& header);
  /** A defender's touch in front of his own goal that now and then goes
   * toward it instead (true when it did). */
  bool tryOwnGoalTouch(MatchPlayer& defender);
  void clearBehind(MatchPlayer& defender);
  void clearBall(MatchPlayer& defender);
  void parryShot(MatchPlayer& goalkeeper, bool overTheBar);
  /**
   * Sends the ball from `origin` toward `target` (normalised) with a
   * horizontal speed and vertical speed in m/s; the kicker is locked out of
   * touching it again for a moment.
   */
  void launchBall(const MatchPlayer& kicker, Vector2F origin, Vector2F target,
                  float horizontalSpeed, float verticalSpeed, float curve);
  /** Launch speed for a ground ball to arrive at `arrivalSpeed`. */
  static float groundLaunchSpeed(float distanceMetres, float arrivalSpeed);
  /** Vertical launch speed so the ball is at `arrivalHeight` metres after
   * `distanceMetres` of horizontal travel. */
  static float loftVerticalSpeed(float distanceMetres, float horizontalSpeed,
                                 float startHeight, float arrivalHeight);
  void takeSetPiece(MatchPlayer& taker, MatchState restartState);
  void takeCorner(MatchPlayer& taker);
  void takeDirectFreeKick(MatchPlayer& taker);
  void arrangeSetPiece(bool attackingHome, Vector2F ballPosition);
  MatchPlayer* bestSetPieceTaker(bool homeTeam, bool shooting);
  /**
   * Taker of a dead-ball duty right now: the side's designated player while
   * he is on the pitch and fit (so substitutions are followed), otherwise
   * bestSetPieceTaker(). Long throws have no automatic specialist (nullptr).
   */
  MatchPlayer* dutyTaker(bool homeTeam, SetPieceDuty duty);
  void placeTaker(MatchPlayer& taker, Vector2F spot);
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
  /** Formation spot while his side has the ball (role and shape shift). */
  Vector2F possessionAnchor(const MatchPlayer& player) const;
  /** Signed x shift of the role in the defensive block (normalised). */
  float defensiveRoleShift(const MatchPlayer& player) const;
  OppositionInstruction instructionAgainst(const MatchPlayer& target) const;
  /** The man this player marks when he is under a tight-marking order. */
  const MatchPlayer* tightMarkTarget(const MatchPlayer& marker) const;
  /** Moves a marker's target tight and goal-side of his man. */
  Vector2F tightMarkPoint(const MatchPlayer& marker, const MatchPlayer& target,
                          Vector2F current) const;
  /** Pass utility lost to a tight marker standing next to the receiver. */
  float tightMarkPenalty(const MatchPlayer& receiver) const;
  /** Pass utility added by the passer's and the receiver's roles. */
  float passingRoleBias(const MatchPlayer& passer, const MatchPlayer& receiver,
                        float progression, float completion) const;
  /** Seconds a role is quicker (+) or slower to close the ball down. */
  float pressEagerness(const MatchPlayer& candidate,
                       const MatchPlayer* carrier) const;
  /** Multiplier of a defender's rate of engaging the carrier. */
  float engageBoost(const MatchPlayer& defender,
                    const MatchPlayer& carrier) const;
  /** Share of the presser's normal stand-off distance. */
  float pressStandOffShare(const MatchPlayer& presser,
                           const MatchPlayer& carrier) const;
  /** Presser's lateral shade onto the carrier's stronger side (normalised
   * y; 0 without a weaker-foot order). */
  float weakFootShade(const MatchPlayer& carrier) const;
  /** Pressure in [0, 1] on a carrier forced onto his weaker foot. */
  float weakFootPressure(const MatchPlayer& carrier) const;
  /** Second presser's spot when doubling up on the carrier, if ordered. */
  std::optional<Vector2F> doubleUpPoint(const MatchPlayer& helper,
                                        const MatchPlayer& carrier,
                                        const MatchPlayer* firstPresser) const;
  /** Take-on success lost to a second defender doubling up. */
  float doubleUpPenalty(const MatchPlayer& carrier,
                        const MatchPlayer& defender) const;
  /** Team-talk effect in [-1, 1] (see getTeamTalkEffect). */
  float talkSwing(bool homeTeam) const;
  StrategySliders computeEffectiveSliders(bool homeTeam) const;
  /** How clearly each side is the weaker one in [0, 1] (see Touchline). */
  void computeUnderdogShares();
  void refreshEffectiveSliders();
  /** Team execution edge: home crowd, team talk and numerical advantage. */
  float teamEdge(bool homeTeam) const;
  /** Per-match referee strictness from the draw and the context. */
  float drawRefereeStrictness() const;
  /** Shot appetite added by a shout (positive shoots more). */
  float shoutShotBias(bool homeTeam) const;
  /** Work-rate bonus from an encouraging shout. */
  float shoutWorkRate(bool homeTeam) const;
  /** AI managers adjust with shouts to the score late in the game. */
  void runAiTouchline(bool homeTeam);
  void refreshTargetBlends();
  void assignMarks();
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
  void captureInterpolationFrame();
  void simulateStep(float dt);
  void updateTeamPhases();
  void setTransitionPhases(bool attackingHome);
  void setControlledPhases(bool attackingHome, float ballX);
  void updateMovement(float dt);
  /** Team shape and every off-ball target (see updateMovement). */
  void refreshTacticalTargets(float dt);
  float estimatedArrivalTime(const MatchPlayer& candidate,
                             Vector2F pressurePosition,
                             const MatchPlayer* carrier);
  std::array<MatchPlayer*, 2> closestOutfieldPair(bool homeTeam,
                                                  const MatchPlayer* carrier,
                                                  Vector2F pressurePosition);
  float runPriority(const MatchPlayer& candidate, const MatchPlayer* carrier);
  std::array<MatchPlayer*, MatchTuning::Shape::MAX_COMMITTED_RUNNERS>
  selectAttackingRunners(bool homeTeam, const MatchPlayer* carrier,
                         const MatchPlayer* transitionSource);
  float runChannel(const MatchPlayer& runner) const;
  MatchPlayer* selectFinalThirdPlayer(
      bool homeTeam, TacticalArrival arrival, const MatchPlayer* carrier,
      const std::array<MatchPlayer*, MatchTuning::Shape::MAX_COMMITTED_RUNNERS>&
          homeRunners,
      const std::array<MatchPlayer*, MatchTuning::Shape::MAX_COMMITTED_RUNNERS>&
          awayRunners);
  std::array<MatchPlayer*, MatchTuning::Shape::MAX_ACTIVE_SUPPORTERS>
  selectActiveSupporters(
      bool homeTeam, const MatchPlayer* carrier,
      const std::array<MatchPlayer*, MatchTuning::Shape::MAX_COMMITTED_RUNNERS>&
          runners,
      const MatchPlayer* midfieldArrival, const MatchPlayer* farPostRunner,
      const MatchPlayer* overlappingFullback);
  const MatchPlayer* findCoverOutlet(bool defendingHome,
                                     const MatchPlayer* carrier) const;
  /** Integrates the free ball in sub-steps, resolving contacts in each. */
  void updateBall(float dt);
  /** Pure ball physics for one sub-step (no contacts or rules). */
  void integrateBall(float dt);
  void updateBallInNet(float dt);
  void resolvePossessionAndActions(float dt);
  /** Moves the dribbled ball with its carrier; false if a heavy touch lost it.
   */
  bool updateDribble(const MatchPlayer& carrier, float dt);
  /** Share of the current touch the ball is still away from the foot. */
  float dribbleExposureShare() const;
  /** Tries to beat a jockeying defender; true if the carrier kept the ball. */
  bool attemptTakeOn(MatchPlayer& carrier, MatchPlayer& defender);
  /** Swept first-touch resolution over the last ball sub-step. */
  void resolveLooseBall();
  void attemptTackle(MatchPlayer& carrier, MatchPlayer& defender, bool sliding);
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
  /** Whether a wide forward in the final third is cutting inside toward
   * the box rather than going down the line to cross. */
  bool cutsInside(const MatchPlayer& player) const;
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
  /** Acceleration, braking, lateral limit, position and facing update. */
  void stepKinematics(MatchPlayer& player, Vector2F desired, float desiredSpeed,
                      float topSpeed, float fatigue, float dt) const;
  void passBall(MatchPlayer& passer, const PassOption& option,
                bool forceLofted = false);
  void takeShot(MatchPlayer& shooter, float forcedXG = -1.0f,
                bool header = false, bool freeKick = false);
  /** Strikes a shot at goal (aim, execution error, launch) and returns its
   * xG; takeShot() adds the statistics and the event. */
  float strikeShot(const MatchPlayer& shooter, float forcedXG, bool header,
                   bool penalty, bool freeKick = false);
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
  void setPossession(MatchPlayer& player);
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

  std::optional<PassOption> choosePassTarget(MatchPlayer& passer);
  PassOption evaluatePassOption(const MatchPlayer& passer,
                                MatchPlayer& receiver) const;
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

  void checkOutOfBounds();
  void scoreGoal(bool homeTeam);
  void makeSave(MatchPlayer& goalkeeper);
  void completeRestart();
  void setupKickOff(bool homeKickingOff);
  void setupThrowIn(bool homeTeam);
  void setupGoalKick(bool homeTeam);
  void setupCorner(bool homeTeam, bool topCorner);
  void setupFreeKick(bool homeTeam, Vector2F foulPos);
  void setupPenalty(bool homeTeam);
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
};
