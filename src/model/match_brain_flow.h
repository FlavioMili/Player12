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
  COUNT
};
inline constexpr std::size_t OPTION_COUNT =
    static_cast<std::size_t>(OptionIndex::COUNT);

/** One carrier decision (pass, shot, carry, shield), as the tool shows it. */
struct CarrierRecord
{
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
  /** The best pass's receiver (player id; 0 for none). */
  std::uint32_t bestReceiver = 0;
  /** The best pass's progress steps from the receiver's role. */
  float receiverRoleSteps = 0.0F;
  /** Where his carry takes him by his next decision. */
  PitchPoint carryEnd{};
  /** The chosen option (OPTION_COUNT for none); a shield deep in his own
   * third is a clearance. */
  std::size_t chosen = OPTION_COUNT;
  bool clearance = false;
  bool filled = false;
};

}  // namespace BrainFlow
