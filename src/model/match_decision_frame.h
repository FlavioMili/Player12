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
#include <limits>
#include <span>

#include "model/match_recorder.h"

/**
 * The decision frame (brain redesign plan, section 11 phase 2 and section
 * 15): Goal -> Options -> Scoring -> Weighting -> Decision.
 *
 * An option's score is a sum of terms. Each term is a measure (Scoring:
 * what he reads, on its own scale) times a weight (Weighting: how much it
 * counts, set by a layer: the default, an instruction, his role, a shout,
 * the game state). The terms are the calculation itself, so the debugger's
 * breakdown of a score is exact rather than reported beside it.
 */
namespace DecisionFrame
{

/** What a decision is for. Step A has the one default, for every carrier;
 * no goal layer exists yet and it weights nothing. */
enum class Goal : std::uint8_t
{
  /** Keep the ball and make progress towards a goal. */
  KEEP_AND_PROGRESS
};

/** One term: a measure times its weight. `name` is a static string;
 * `layer` is where the weight comes from. */
struct Term
{
  const char* name = "";
  TermSource layer = TermSource::SITUATION;
  float measure = 0.0F;
  float weight = 0.0F;
  [[nodiscard]] float value() const { return measure * weight; }
};

/** A 0/1 measure: whether a condition holds (a cross, the final third). */
[[nodiscard]] inline float gate(bool condition)
{
  return condition ? 1.0F : 0.0F;
}

/**
 * One option's score, summed in the order the terms are added. Fixed
 * capacity, so scoring allocates nothing on the hot path.
 */
class Score
{
 public:
  static constexpr std::size_t MAX_TERMS = 24;

  /** Adds measure x weight to the score (nothing once ruled out). */
  void add(const char* name, TermSource layer, float measure, float weight)
  {
    if (!available || count == MAX_TERMS)
    {
      return;
    }
    terms[count++] = {
        .name = name, .layer = layer, .measure = measure, .weight = weight};
    sum += measure * weight;
  }
  /** Multiplies the score so far by `factor`. Recorded as the term the
   * change makes: the score before it times (factor - 1). */
  void scale(const char* name, TermSource layer, float factor)
  {
    if (!available || count == MAX_TERMS)
    {
      return;
    }
    terms[count++] = {
        .name = name, .layer = layer, .measure = sum, .weight = factor - 1.0F};
    sum *= factor;
  }
  /** The option is not open to him (Options): it cannot win. */
  void ruleOut() { available = false; }
  [[nodiscard]] bool isAvailable() const { return available; }
  [[nodiscard]] float total() const
  {
    return available ? sum : -std::numeric_limits<float>::infinity();
  }
  [[nodiscard]] std::span<const Term> allTerms() const
  {
    return {terms.data(), count};
  }
  /** The debugger's breakdown: each term's value, by its weight's layer. */
  void fill(ScoreBreakdown& breakdown) const
  {
    breakdown.total = total();
    for (const Term& term : allTerms())
    {
      breakdown.add(term.name, term.layer, term.value());
    }
  }

 private:
  std::array<Term, MAX_TERMS> terms{};
  std::size_t count = 0;
  float sum = 0.0F;
  bool available = true;
};

}  // namespace DecisionFrame
