// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "model/match_decision_frame.h"
#include "model/match_point.h"

/**
 * The brain flow tool's view of the brain (Notes and plans/
 * match_engine_brain_flow_tool_plan.md): the influence layers it can switch
 * on and off, and the record of one carrier decision it shows. The engine
 * fills the record only when the tool asks for one; matches never do.
 */
namespace BrainFlow
{

/** The influence layers the tool switches, one at a time. */
enum class Layer : std::uint8_t
{
  /** The team sliders as the manager set them. */
  INSTRUCTIONS,
  /** His role and duty, and the receivers' roles. */
  ROLE,
  /** A touchline shout. */
  SHOUT,
  /** Opposition orders: tight marking, showing him onto his weaker foot. */
  OPPOSITION_ORDERS,
  /** Score, minute and being the underdog, through the team sliders. */
  GAME_STATE,
  /** Familiarity with the tactics, the team's edge, the team talk. */
  MOOD,
  COUNT
};
inline constexpr std::size_t LAYER_COUNT =
    static_cast<std::size_t>(Layer::COUNT);

/** A set of layers, a bit each. */
using LayerMask = std::uint8_t;
inline constexpr LayerMask ALL_LAYERS = (1U << LAYER_COUNT) - 1U;

[[nodiscard]] inline bool isOn(LayerMask mask, Layer layer)
{
  return (mask & (1U << static_cast<unsigned>(layer))) != 0U;
}

/** Each layer's steps on one weight (DecisionFrame::stepped). */
using LayerSteps = std::array<float, LAYER_COUNT>;

/** One weight: its default, each layer's steps, and the weight he used. */
struct Weight
{
  const char* name = "";
  float base = 0.0F;
  LayerSteps steps{};
  float used = 0.0F;
};

/** One chance he reads: the truth, his judgement offset (log-odds), and
 * what he reads it as. */
struct Chance
{
  const char* name = "";
  float truth = 0.0F;
  float offset = 0.0F;
  float read = 0.0F;
};

/** One option: its score before pick noise, term by term; why it was ruled
 * out (null if open); and the pick noise it got. */
struct Option
{
  DecisionFrame::Score score;
  const char* ruledOut = nullptr;
  float noise = 0.0F;
};

/** The options, in the decision's order. */
enum class OptionIndex : std::uint8_t
{
  PASS,
  SHOT,
  CARRY,
  SHIELD,
  /** Pressed near his own goal with no pass worth playing (step E: its own
   * option, scored with the shield's keep for now). */
  CLEAR,
  COUNT
};
inline constexpr std::size_t OPTION_COUNT =
    static_cast<std::size_t>(OptionIndex::COUNT);

/** One carry candidate (brain redesign plan section 16, step C): a
 * direction and a pace, measured at its 1 s point on the true chances.
 * Candidates ruled out (off the pitch, backwards near his own goal) and
 * paces the Dribbling cap makes equal are not listed. */
struct CarryCandidate
{
  /** Degrees off straight down the pitch; positive to his right as the
   * tool draws the pitch (y downwards). */
  float angleDegrees = 0.0F;
  /** "Towards the goal" rather than one of the fixed angles. */
  bool towardsGoal = false;
  /** Pace level: walk 0, jog 1/3, run 2/3, sprint 1. */
  float level = 0.0F;
  PitchPoint end{};
  float keep = 0.0F;
  float endXG = 0.0F;
  float score = 0.0F;
};

/** One off-ball option: whether it is open to him, and what opens it. */
struct OffBallOption
{
  const char* name = "";
  bool open = false;
  const char* opensWhen = "";
};

/** One player's off-ball frame, as the tool shows it (route A, brain
 * redesign plan section 22): his situation, its options, the stopgap pick
 * (today's order), an override, and where it sends him. */
struct OffBallRecord
{
  /** Set by the tool: whose frame to record (player id). */
  std::uint32_t player = 0;
  const char* situation = "";
  std::vector<OffBallOption> options;
  std::size_t chosen = 0;
  /** What overrode the pick (null for nothing). */
  const char* overriddenBy = nullptr;
  /** Facing the ball plan step 1: why his body faces as it does (null:
   * not recorded); when goal-side, the speed his job asks and the most he
   * can manage that way facing the ball (m/s), and whether he turned. */
  const char* facing = nullptr;
  bool facingChecked = false;
  float facingAsked = 0.0F;
  float facingCap = 0.0F;
  bool facingTurned = false;
  PitchPoint target{};
  bool filled = false;
  /** Step P1 (shadow): his own call on pressing the carrier (null when
   * none). */
  const char* pressCall = nullptr;
  /** Facing the ball plan §4c, the pounce (the presser, with the ball out
   * of the carrier's reach): his call (null when not asked), and when he
   * and the carrier can meet it (s). */
  const char* pounce = nullptr;
  /** The press squeeze: its strongest reason (null: none), how far (0 to
   * 1), and the stand-off before and after (m). */
  const char* squeeze = nullptr;
  float squeezeShare = 0.0F;
  float squeezeFrom = 0.0F;
  float squeezeTo = 0.0F;
  float pounceSeconds = 0.0F;
  float pounceCarrierSeconds = 0.0F;
  /** Step P4 (shadow): his own call on the second man's job (null when not
   * asked, or the job is closed). */
  const char* pressSecond = nullptr;
  /** Step O6 (shadow): his own loose-ball call (null when none), and when
   * it is his, where he could meet it. */
  const char* looseCall = nullptr;
  struct MeetPoint
  {
    const char* name = "";
    bool open = false;
    float seconds = 0.0F;
    float height = 0.0F;
    /** The nearest rival's rough time there less his (s). */
    float margin = 0.0F;
  };
  std::vector<MeetPoint> meetPoints;
  /** Step O6, first to commit: his projected sequence (the tool's moment
   * does not run on, so the delays are shown, not waited). */
  struct CommitCall
  {
    /** On the press (step P3), not a loose ball (step O6). */
    bool press = false;
    float lookUpSeconds = 0.0F;
    float callSeconds = 0.0F;
    /** The team-mate is in view or alongside: no look-up delay. */
    bool seesAtOnce = false;
    /** The nearest team-mate also going (0 for none), his delays, and both
     * precise meeting times. */
    std::uint32_t partner = 0;
    float partnerLookUpSeconds = 0.0F;
    float partnerCallSeconds = 0.0F;
    float hisSeconds = 0.0F;
    float partnerSeconds = 0.0F;
    /** Decided by the shape spots (within 0.5 s), not the clearly
     * quicker; whose it should be; his chance of getting that right. */
    bool byShape = false;
    bool shouldBeHis = false;
    float rightChance = 0.0F;
    /** He finishes both delays first, so he makes the call. */
    bool heCalls = false;
  };
  std::optional<CommitCall> commit;
};

/** One carrier decision (pass, shot, carry, shield, clear), as the tool
 * shows it. */
struct CarrierRecord
{
  /** Set by the tool before the decision (kept when the record is
   * filled): no judgement offsets and no pick noise, so the scores are at
   * the true chances. The sizes of both are still recorded. */
  bool steady = false;
  LayerMask layers = ALL_LAYERS;
  std::array<Option, OPTION_COUNT> options;
  std::vector<Weight> weights;
  std::vector<Chance> chances;
  /** Judgement: the most a chance can be misread by, in log-odds. */
  float judgement = 0.0F;
  /** Pick randomness: the most any score can move, and its factors. */
  float pickNoise = 0.0F;
  float decisions = 0.0F;
  float familiarityFactor = 1.0F;
  float edgeFactor = 1.0F;
  float talkFactor = 1.0F;
  /** The best pass's own terms, as he reads it. */
  DecisionFrame::Score bestPass;
  /** The best pass's receiver (player id; 0 for none). */
  std::uint32_t bestReceiver = 0;
  /** The best pass's progress steps from the receiver's role. */
  float receiverRoleSteps = 0.0F;
  /** Where his carry takes him by his next decision. */
  PitchPoint carryEnd{};
  /** The aim the shot option carries (today's rules, rolled at the
   * decision), if the shot is open: across the goal from its centre
   * (positive towards pitch y = 1) and up, in metres. */
  bool shotAimed = false;
  float shotAimY = 0.0F;
  float shotAimZ = 0.0F;
  /** Every carry candidate; the best (highest score) is the carry option. */
  std::vector<CarryCandidate> carryCandidates;
  /** Where the clear option is aimed (today's rules, rolled at the
   * decision), if it is open. */
  bool clearAimed = false;
  PitchPoint clearTarget{};
  /** The chosen option (OPTION_COUNT for none). */
  std::size_t chosen = OPTION_COUNT;
  bool filled = false;
  /** The tool's off-ball view of one player at the same moment (step O5),
   * filled by MatchEngine::brainFlowOffBallCapybara. */
  OffBallRecord offBall;
};

}  // namespace BrainFlow
