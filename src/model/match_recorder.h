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
#include <vector>

#include "global/types.h"
#include "model/match_engine.h"
#include "model/match_point.h"
#include "model/match_tuning.h"

/**
 * Observation hooks of the match engine for debugging tools (the match
 * sandbox). See MatchEngine::setRecorder.
 *
 * The engine calls a recorder only when one is attached, and only to report
 * what it has already decided: a recorder must never change the engine, so a
 * recorded match plays exactly like an unrecorded one. Everything the engine
 * exposes through its getters (phases, intents, targets, keeper states, the
 * ball, events, substitutions) is read by the recorder in onStepEnd(); these
 * records carry what the getters cannot show (rankings, scores, actions).
 *
 * Copies of an engine (highlight look-ahead, saved states) never carry the
 * recorder, so predictions are never recorded.
 */

/** Team-wide jobs chosen by the off-ball planner for one side. */
struct MatchTeamPlan
{
  static constexpr std::size_t PRESSERS = 2;
  static constexpr std::size_t RUNNERS =
      MatchTuning::Shape::MAX_COMMITTED_RUNNERS;
  static constexpr std::size_t SUPPORTERS =
      MatchTuning::Shape::MAX_ACTIVE_SUPPORTERS;

  bool homeTeam = true;
  TeamPhase phase = TeamPhase::STOPPAGE;
  /** Closest to the ball by estimated arrival, first presser first (0: none). */
  std::array<PlayerID, PRESSERS> pressers{};
  /** Estimated arrival time in seconds (the ranking key, lower is sooner). */
  std::array<float, PRESSERS> pressArrivalSeconds{};
  /** Committed runs in behind, by run priority (0: none). */
  std::array<PlayerID, RUNNERS> runners{};
  std::array<float, RUNNERS> runPriority{};
  /** Final-third arrivals (0: none this refresh). */
  PlayerID midfieldArrival = 0;
  PlayerID farPostRunner = 0;
  PlayerID overlappingFullback = 0;
  /** Team-mates offering short support to the carrier (0: none). */
  std::array<PlayerID, SUPPORTERS> supporters{};
  /** Out of possession: the opponent left as the carrier's safe outlet. */
  PlayerID coverOutlet = 0;
};

/** One on-ball choice of the AI: the four option utilities and the winner.
 * A human-controlled player makes no such choice; his actions are still
 * reported through onAction. */
struct MatchDecisionRecord
{
  static constexpr float NOT_AVAILABLE = -std::numeric_limits<float>::infinity();

  PlayerID player = 0;
  bool homeTeam = true;
  ScenarioAction chosen = ScenarioAction::NONE;
  /** Utilities in force (noise included): pass, shot, carry, shield. */
  float pass = NOT_AVAILABLE;
  float shot = NOT_AVAILABLE;
  float carry = NOT_AVAILABLE;
  float shield = NOT_AVAILABLE;
  /** The option that would have won without the decision noise. */
  ScenarioAction chosenWithoutNoise = ScenarioAction::NONE;
  /** Shared inputs of the scores. */
  float pressure = 0.0f;
  float shotXG = 0.0f;
  float opennessAhead = 0.0f;
  float noiseScale = 0.0f;
  /** Best and runner-up pass candidates (0: none). */
  PlayerID bestReceiver = 0;
  float bestPassUtility = NOT_AVAILABLE;
  PassIntent bestPassIntent = PassIntent::RECYCLE;
  PlayerID runnerUpReceiver = 0;
  float runnerUpPassUtility = NOT_AVAILABLE;
};

/** Kind of an executed action. */
enum class MatchActionKind : std::uint8_t
{
  PASS,
  SHOT,
  CLEARANCE,
  /** A headed knock-down or flick-on to a team-mate. */
  KNOCK_DOWN,
  TAKE_ON,
  TACKLE
};

/** How a take-on or tackle ended. */
enum class MatchDuelResult : std::uint8_t
{
  NONE,
  /** Take-on: the defender was beaten. Tackle: the ball was won cleanly. */
  WON,
  /** Tackle: the ball was knocked loose rather than won. */
  POKED_LOOSE,
  /** The challenge failed and play went on. */
  LOST,
  /** Tackle: the referee gave a foul (whether or not the ball was won). */
  FOUL
};

/** One executed action, as the engine performed it. */
struct MatchActionRecord
{
  MatchActionKind kind = MatchActionKind::PASS;
  PlayerID player = 0;
  bool homeTeam = true;
  /** Intended receiver (pass), beaten or tackling opponent (duels). */
  PlayerID target = 0;
  /** Where it was played from and aimed at (normalised pitch). */
  PitchPoint from{0.0f, 0.0f};
  PitchPoint to{0.0f, 0.0f};
  /** Launch speeds (m/s) and curve (rad/s) of a kick. */
  float speed = 0.0f;
  float verticalSpeed = 0.0f;
  float curve = 0.0f;
  /** Pass: lofted; tackle: sliding. */
  bool lofted = false;
  /** Played with the head (shot, clearance, knock-down). */
  bool header = false;
  PassIntent passIntent = PassIntent::RECYCLE;
  /** Shot: estimated xG; pass: estimated completion; duels: win chance. */
  float estimate = 0.0f;
  /** Tackle: foul propensity of the challenge. */
  float foulPropensity = 0.0f;
  MatchDuelResult result = MatchDuelResult::NONE;
};

// --- Detail (debugger phase 4): scores broken into their terms ------------

/** Where a term of a score comes from. */
enum class TermSource : std::uint8_t
{
  /** The moment itself, the same rule for everyone (space, pressure, xG). */
  SITUATION,
  /** A rule triggered by where he is (final third, wide, own third). */
  LOCATION,
  /** His ability (the engine's attribute values). */
  ATTRIBUTE,
  /** His formation slot's tactical role and duty (RoleProfile). */
  SLOT_ROLE,
  /** His own position from the player data (ST, LW, CB…). */
  NATURAL_POSITION,
  /** Team instructions and opposition orders. */
  INSTRUCTION,
  /** Score and time. */
  GAME_STATE,
  /** Team talks, shouts and tactical familiarity. */
  MORALE,
  /** Random perturbation. */
  NOISE,
  COUNT
};

/** One named part of a score. `name` is a static string. */
struct ScoreTerm
{
  const char* name = "";
  TermSource source = TermSource::SITUATION;
  float value = 0.0f;
};

/**
 * A score and the terms it is made of. `total` is the engine's own value;
 * the terms are reported beside the calculation and should add up to it
 * (residual() shows by how much they do not, e.g. after a formula changed).
 */
struct ScoreBreakdown
{
  float total = 0.0f;
  std::vector<ScoreTerm> terms;

  void add(const char* name, TermSource source, float value)
  {
    terms.push_back({name, source, value});
  }
  [[nodiscard]] float residual() const
  {
    float sum = 0.0f;
    for (const ScoreTerm& term : terms) sum += term.value;
    return total - sum;
  }
};

/** Why a team-mate was not considered as a receiver. */
enum class PassExclusion : std::uint8_t
{
  NONE,
  TOO_CLOSE,
  TOO_FAR,
  /** A long ball back to his own keeper. */
  KEEPER_TOO_FAR,
  /** He looks offside to the passer (who may misread the line). */
  LOOKS_OFFSIDE
};

/** One team-mate the passer looked at. */
struct PassCandidateDetail
{
  PlayerID receiver = 0;
  PassExclusion excluded = PassExclusion::NONE;
  /** Only for candidates that were not excluded. */
  ScoreBreakdown utility;
  ScoreBreakdown completion;
  PassIntent intent = PassIntent::RECYCLE;
  float distanceMetres = 0.0f;
  bool lofted = false;
};

/** One on-ball decision with every score broken down. */
struct MatchDecisionDetail
{
  PlayerID player = 0;
  bool homeTeam = true;
  ScenarioAction chosen = ScenarioAction::NONE;
  /** Pass, shot, carry, shield: the scores before noise, broken down. A
   * total of -infinity means the option was not available. */
  std::array<ScoreBreakdown, 4> options;
  /** The noise added to each option. */
  std::array<float, 4> noise{};
  /** How large the noise could be, and why. */
  ScoreBreakdown noiseScale;
  std::vector<PassCandidateDetail> candidates;
};

/** A ranking of the off-ball planner, every candidate broken down. */
struct MatchRankingDetail
{
  enum class Kind : std::uint8_t
  {
    /** Players nearest the ball by estimated arrival (lower is first). */
    TO_BALL,
    /** Run priority of the side with the ball (higher is first). */
    RUN_PRIORITY
  };
  struct Candidate
  {
    PlayerID player = 0;
    ScoreBreakdown score;
  };
  Kind kind = Kind::TO_BALL;
  bool homeTeam = true;
  std::vector<Candidate> candidates;
};

/** A random duel resolved against a chance: the roll and what made it. */
struct MatchDuelDetail
{
  enum class Kind : std::uint8_t
  {
    /** The nearest defender deciding whether to challenge this step. */
    ENGAGE,
    TAKE_ON,
    TACKLE,
    /** A first touch (Binturong body): `chance` is the angle difficulty and
     * `foul` the weight difficulty; no roll. */
    FIRST_TOUCH,
    /** A strike read by the contact model (Binturong body; contact plan):
     * `chance` is the angle difficulty and `foul` the weight difficulty;
     * carried out or only measured (strikeCarriedOut). */
    STRIKE
  };
  Kind kind = Kind::ENGAGE;
  PlayerID player = 0;
  PlayerID opponent = 0;
  bool homeTeam = true;
  /** What makes the chance: the probability itself for a take-on or a
   * tackle, the engagement rate per second for ENGAGE. */
  ScoreBreakdown chance;
  /** Probability of success (ENGAGE: of going in during this step). */
  float probability = 0.0f;
  /** The uniform roll; success when roll < probability. */
  float roll = 0.0f;
  /** TACKLE: the foul propensity, its threshold (scaled down for a tackle
   * that won the ball) and the referee's roll (a foul when roll <
   * threshold); no roll when the referee had nothing to decide. */
  ScoreBreakdown foul;
  float foulThreshold = 0.0f;
  std::optional<float> foulRoll;
  /** FIRST_TOUCH: his touch skill; the speed he meant to give the ball and
   * the speed it got (m/s); the two jobs (the turn in radians, outward or
   * not, and the pace change in m/s); each job's chance of going wrong,
   * whether it did, and its error (angle in radians along the turn, weight
   * in m/s). The angle difficulty is `chance`, the weight's `foul`. */
  float touchSkill = 0.0f;
  float intendedSpeed = 0.0f;
  float actualSpeed = 0.0f;
  float turn = 0.0f;
  bool outward = false;
  float paceChange = 0.0f;
  float angleError = 0.0f;
  float angleFailChance = 0.0f;
  bool angleFailed = false;
  float weightError = 0.0f;
  float weightFailChance = 0.0f;
  bool weightFailed = false;
  /** STRIKE: which kind (the speed he meant is `intendedSpeed`, the jobs
   * `turn`, `outward` and `paceChange`). */
  StrikeKind strike = StrikeKind::GROUND_PASS;
  /** STRIKE: carried out by the contact model (step 3), with the
   * FIRST_TOUCH fields filled as for a touch; otherwise measured only. */
  bool strikeCarriedOut = false;
};

/** Receives the engine's observations. Every hook defaults to nothing. */
class MatchRecorder
{
 public:
  virtual ~MatchRecorder() = default;

  /**
   * Whether the engine should also report the detail hooks below. Building
   * the breakdowns costs time, so a recorder asks for them only while it
   * rebuilds a stretch to inspect.
   */
  [[nodiscard]] virtual bool wantsDetail() const { return false; }
  virtual void onDecisionDetail(const MatchEngine& /*engine*/,
                                const MatchDecisionDetail& /*detail*/)
  {
  }
  virtual void onRankingDetail(const MatchEngine& /*engine*/,
                               const MatchRankingDetail& /*detail*/)
  {
  }
  virtual void onDuelDetail(const MatchEngine& /*engine*/,
                            const MatchDuelDetail& /*detail*/)
  {
  }

  /** Both sides' jobs, each time the off-ball plan is refreshed. */
  virtual void onTeamPlan(const MatchEngine& /*engine*/,
                          const MatchTeamPlan& /*plan*/)
  {
  }

  /** An on-ball decision, before its action is executed. */
  virtual void onDecision(const MatchEngine& /*engine*/,
                          const MatchDecisionRecord& /*decision*/)
  {
  }

  /** An action as executed. */
  virtual void onAction(const MatchEngine& /*engine*/,
                        const MatchActionRecord& /*action*/)
  {
  }

  /** The end of a fixed step: read the engine's state through its getters. */
  virtual void onStepEnd(const MatchEngine& /*engine*/) {}

  /** Stage 1 possession diagnostics are opt-in and observational. Helper
   * snapshots may be intermediate; validate invariants only at step end. */
  [[nodiscard]] virtual bool wantsPossession() const { return false; }
  virtual void onPossessionObservation(const MatchEngine& /*engine*/,
                                      PossessionObservation /*observation*/,
                                      const PossessionSnapshot& /*snapshot*/,
                                      PossessionId /*executedVersion*/) {}
  /** An actual gate taken in the contact sweep. Some gates precede reach
   * testing: a gate observation does not prove the ball was playable. */
  virtual void onPossessionContactGate(const MatchEngine& /*engine*/,
                                      const MatchPlayer& /*player*/,
                                      PossessionContactGate /*gate*/) {}
};
