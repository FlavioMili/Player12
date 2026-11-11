// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

// The match engine's stats come in three layers:
//
// 1. CURRENT stats: what the game and its database carry today (the nine in
//    stats_config.json, plus height). The handshake loads them; here they
//    are the BaseAttribute a recipe blends.
// 2. FUTURE game stats (DerivedLayer::FUTURE_GAME_STAT): stats the game is
//    meant to carry one day as stats of their own (shown, trained, stored).
//    Until it does, each is derived here from current stats by a recipe
//    (and, for a stat built on his body, his height).
//    When the game carries one, the handshake loads it in place of its
//    recipe, and nothing downstream changes: the engine reads the same
//    slot.
// 3. MATCH-ENGINE-ONLY stats (DerivedLayer::MATCH_ENGINE_ONLY): physiology
//    and other inner workings with no place in the game. They exist only
//    during a match, derived from current and future stats (never from
//    each other), so they keep working unchanged when a future stat starts
//    coming from the game.
//
// Some match-engine-only values are not stats here but physical values the
// handshake works out directly (top speed from Pace, braking from
// Physicality).
//
// A derived stat is a rating on the 0-1 scale; what it means physically is
// up to whoever uses it, with anchors here for the stats that have a
// natural unit. Future stats come first in DerivedStat, so deriving in
// order has every future stat ready before a match-engine-only recipe reads
// it.
//
// To add a stat: an id in DerivedStat (in its layer's block), a row in
// DERIVED_STATS, a recipe in DEFAULT_DERIVED_RECIPES (and in any handshake
// with its own recipes), then use it. The sandbox inspector, the --check
// test and fm_lab read this registry, so they need nothing more; --check
// enforces the layers.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

#include "model/match_tuning.h"

enum class DerivedStat : std::uint8_t
{
  // Future game stats.
  AGILITY,
  FIRST_TOUCH,
  ACCELERATION,
  TECHNIQUE,
  HEADING,
  JUMPING_REACH,
  TACKLING,
  STRENGTH,
  BALANCE,
  REFLEXES,
  HANDLING,
  AERIAL_REACH,
  THROWING,
  COMMAND_OF_AREA,
  // The player profile's attributes (UX design), slots for later plans:
  // nothing reads them yet.
  FINISHING,
  LONG_SHOTS,
  PENALTIES,
  CROSSING,
  CORNERS,
  FREE_KICKS,
  MARKING,
  ANTICIPATION,
  DECISIONS,
  FLAIR,
  COMPOSURE,
  OFF_THE_BALL,
  POSITIONING,
  CONCENTRATION,
  WORK_RATE,
  TEAMWORK,
  LEADERSHIP,
  DETERMINATION,
  BRAVERY,
  AGGRESSION,
  NATURAL_FITNESS,
  // Match-engine-only stats.
  ENDURANCE,
  BURST_CAPACITY,
  FUEL_CAPACITY,
  RECOVERY_RATE,
  FAST_RECOVERY,
};
inline constexpr std::size_t DERIVED_STAT_COUNT = 40;

enum class DerivedLayer : std::uint8_t
{
  FUTURE_GAME_STAT,
  MATCH_ENGINE_ONLY,
};

/** The current stats a recipe blends (MatchPlayer's fields). */
enum class BaseAttribute : std::uint8_t
{
  PACE,
  SHOOTING,
  PASSING,
  DRIBBLING,
  DEFENDING,
  PHYSICALITY,
  STAMINA,
  VISION,
  GOALKEEPING,
};
inline constexpr std::size_t BASE_ATTRIBUTE_COUNT = 9;

/** A derived stat's rating: the weighted blend of current stats and (for a
 * match-engine-only stat) future stats; the weights sum to 1; plus his own
 * deviation (spread x playerStatDeviation); clamped to 0-1. */
struct DerivedRecipe
{
  std::array<float, BASE_ATTRIBUTE_COUNT> weights{};
  /** Weights on future game stats (DerivedStat ids); a future stat's own
   * recipe leaves these 0. */
  std::array<float, DERIVED_STAT_COUNT> futureWeights{};
  /** A stat in metres built on his height (Jumping reach): the blend rates
   * only the part on top of his height, worth heightPartLow metres at
   * STAT_1_RATING to heightPartHigh at STAT_99_RATING (a straight line);
   * his height is added, and the total is rated on the stat's own line.
   * Both 0: not built on his height. */
  float heightPartLow = 0.0f;
  float heightPartHigh = 0.0f;
  /** How far one player's rating strays from the blend: the standard
   * deviation (as a rating, 0.10 = 10 stat points) of his own deviation,
   * keyed on his id so it is the same every match. 0: the blend exactly.
   * The tighter a stat follows the current stats, the smaller. */
  float spread = 0.0f;
  [[nodiscard]] constexpr bool onHeight() const
  {
    return heightPartLow != 0.0f || heightPartHigh != 0.0f;
  }
};
using DerivedRecipes = std::array<DerivedRecipe, DERIVED_STAT_COUNT>;

/** Ratings that anchor the physical meanings: a lower-league part-timer and
 * the world's elite (world generation: a league's first-team level is
 * 38 + 0.42 x its reputation; the best clubs reach 90). On the linear scale
 * of the Binturong handshake a rating is the stat / 100; the Aardvark
 * handshake's stretched scale places them differently. */
inline constexpr float PART_TIME_RATING = 0.45f;
inline constexpr float ELITE_RATING = 0.85f;
/** The ends of the stat scale (1-99) as ratings: the fatigue plan's stats
 * run their physical range across the whole scale. */
inline constexpr float STAT_1_RATING = 0.01f;
inline constexpr float STAT_99_RATING = 0.99f;

struct DerivedStatInfo
{
  DerivedLayer layer = DerivedLayer::MATCH_ENGINE_ONLY;
  std::string_view name;
  /** Three letters for compact displays. */
  std::string_view label;
  std::string_view description;
  /** Physical meaning, if any: the unit, the value at two anchor ratings (a
   * straight line through them), and its safe range. The range holds the
   * whole line from rating 0 to 1, so the clamp only catches a bad recipe or
   * override and never flattens a real rating (the --check test enforces
   * it). Empty unit: a rating only. */
  std::string_view unit;
  float lowValue = 0.0f;
  float highValue = 0.0f;
  float minValue = 0.0f;
  float maxValue = 0.0f;
  /** The ratings lowValue and highValue belong to. */
  float lowRating = PART_TIME_RATING;
  float highRating = ELITE_RATING;
};

inline constexpr std::array<DerivedStatInfo, DERIVED_STAT_COUNT> DERIVED_STATS{{
    // ---- Future game stats ------------------------------------------------
    {DerivedLayer::FUTURE_GAME_STAT, "Agility", "Agi",
     "Changing direction: how sharply he can turn at speed (a multiplier on "
     "the lateral grip every player has); also his braking, and a small "
     "boost to his acceleration.",
     "x", 0.9f, 1.1f, 0.65f, 1.2f},
    {DerivedLayer::FUTURE_GAME_STAT, "First touch", "Tch",
     "Receiving the ball: taking the pace off it and placing it where he "
     "wants it.",
     "", 0.0f, 0.0f, 0.0f, 0.0f},
    // Sprint profiles: elite men across sports 7-10 N/kg (Haugen 2019),
    // footballers ~0.5 below sprinters; French youth players 6.0-6.7; in-situ
    // A0 of professionals 8.3-8.7 m/s^2 on match days (Alonso-Callejo 2022).
    {DerivedLayer::FUTURE_GAME_STAT, "Acceleration", "Acc",
     "Getting going: his maximum push from a standstill (A0 of the "
     "acceleration-speed profile, fading to nothing at his top speed).",
     "m/s^2", 5.5f, 9.5f, 5.4f, 9.6f, STAT_1_RATING, STAT_99_RATING},
    {DerivedLayer::FUTURE_GAME_STAT, "Technique", "Tec",
     "Clean striking of the ball with either foot: how little his weaker "
     "foot costs him.",
     "", 0.0f, 0.0f, 0.0f, 0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Heading", "Hea",
     "Making a header go where he means it: the pace he puts on it, how "
     "accurately he places it, and how much pace he can take off it. How "
     "high he gets is Jumping reach.",
     "", 0.0f, 0.0f, 0.0f, 0.0f},
    // The heading technique plan (step 1): 1 is a 1.60 m man with a poor
    // jump (0.25 m), 99 a 2.00 m man with an elite leap (0.70 m; Ronaldo's
    // header against Sampdoria in 2019 was about 0.7 m off the ground).
    {DerivedLayer::FUTURE_GAME_STAT, "Jumping reach", "Jmp",
     "How high he can get the top of his head at the peak of his jump: his "
     "height plus his jump.",
     "m", 1.85f, 2.70f, 1.80f, 2.75f, STAT_1_RATING, STAT_99_RATING},
    // The tackling plan (step 4): the strength of his foot through the ball
    // in a block (Defending stands in until the game carries it).
    {DerivedLayer::FUTURE_GAME_STAT, "Tackling", "Tkl",
     "Putting his foot through the ball in a tackle: how firmly he meets it "
     "in a block (and, later, how cleanly he avoids the man). Reading when to "
     "go in is Defending.",
     "", 0.0f, 0.0f, 0.0f, 0.0f},
    // The collision plan (step 1): in a run-in Strength counts only through
    // his weight (bodyMassKilograms); a deliberate push comes later.
    {DerivedLayer::FUTURE_GAME_STAT, "Strength", "Str",
     "Applying force to another player beyond his momentum. For now it "
     "counts only through his weight: height squared times a body-mass index "
     "of 17 at Strength 1 to 31 at 99.",
     "", 0.0f, 0.0f, 0.0f, 0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Balance", "Bal",
     "Resisting a knock: how far a collision can take him past what he can "
     "run in that direction, or how suddenly, before he stumbles or falls.",
     "", 0.0f, 0.0f, 0.0f, 0.0f},
    // The goalkeeper plan (step 2): FM's keeper attributes; Goalkeeping
    // stands in until the game carries them.
    {DerivedLayer::FUTURE_GAME_STAT, "Reflexes", "Ref",
     "A keeper's reaction to a struck ball: how soon his hands start to "
     "move (0.28 s at 1 to 0.16 s at 99), and how close to its line they "
     "land.",
     "", 0.0f, 0.0f, 0.0f, 0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Handling", "Han",
     "A keeper's hands on the ball: how firmly a parry springs off them "
     "(later, whether it sticks).",
     "", 0.0f, 0.0f, 0.0f, 0.0f},
    // The goalkeeper plan (step 3): Jumping reach's range (1.60 m and
    // 2.00 m men) plus about 0.6 m of arm above the head.
    {DerivedLayer::FUTURE_GAME_STAT, "Aerial reach", "Aer",
     "How high a keeper can get his fingertips at the top of his jump: his "
     "height plus his arm and his jump.",
     "m", 2.45f, 3.35f, 2.40f, 3.40f, STAT_1_RATING, STAT_99_RATING},
    // The goalkeeper plan (step 5).
    {DerivedLayer::FUTURE_GAME_STAT, "Throwing", "Thr",
     "A keeper's ball from his hands: how far he can throw it overarm (20 m "
     "at 1 to 50 m at 99) and how accurately he throws or rolls it.",
     "", 0.0f, 0.0f, 0.0f, 0.0f},
    // The goalkeeper plan (step 7).
    {DerivedLayer::FUTURE_GAME_STAT, "Command of area", "Cmd",
     "A keeper coming for high balls: how close an attacker can be before "
     "he punches rather than catches (2.0 m at 1 to 0.5 m at 99).",
     "", 0.0f, 0.0f, 0.0f, 0.0f},
    // The player profile's attributes (UX design). Their blends and spreads
    // are our own estimates (agreed 2026-11-11), not research values.
    {DerivedLayer::FUTURE_GAME_STAT, "Finishing", "Fin",
     "Putting a chance away: placing a shot from inside the box.", "", 0.0f,
     0.0f, 0.0f, 0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Long shots", "Lon",
     "Shooting with power and accuracy from outside the box.", "", 0.0f,
     0.0f, 0.0f, 0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Penalties", "Pen",
     "Scoring from the penalty spot.", "", 0.0f, 0.0f, 0.0f, 0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Crossing", "Cro",
     "Delivering the ball into the box from wide.", "", 0.0f, 0.0f, 0.0f,
     0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Corners", "Cor",
     "Delivering a corner kick to where he means it.", "", 0.0f, 0.0f, 0.0f,
     0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Free kicks", "Fre",
     "Striking a dead ball: shots and deliveries from free kicks.", "", 0.0f,
     0.0f, 0.0f, 0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Marking", "Mar",
     "Staying tight to an opponent he is picking up.", "", 0.0f, 0.0f, 0.0f,
     0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Anticipation", "Ant",
     "Reading what will happen next, and reacting before it does.", "", 0.0f,
     0.0f, 0.0f, 0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Decisions", "Dec",
     "Choosing the right option, with or without the ball.", "", 0.0f, 0.0f,
     0.0f, 0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Flair", "Fla",
     "Trying the unexpected: the inventive option over the safe one.", "",
     0.0f, 0.0f, 0.0f, 0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Composure", "Cmp",
     "Keeping his technique under pressure: on the ball, and in front of "
     "goal.",
     "", 0.0f, 0.0f, 0.0f, 0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Off the ball", "OtB",
     "Moving into space to receive the ball in attack.", "", 0.0f, 0.0f, 0.0f,
     0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Positioning", "Pos",
     "Taking up the right place when his side defends.", "", 0.0f, 0.0f, 0.0f,
     0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Concentration", "Con",
     "Staying switched on through the match: how seldom he loses focus.", "",
     0.0f, 0.0f, 0.0f, 0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Work rate", "Wor",
     "How willingly he puts in the running, with or without the ball.", "",
     0.0f, 0.0f, 0.0f, 0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Teamwork", "Tea",
     "Playing for the side: following the plan and the team's movement.", "",
     0.0f, 0.0f, 0.0f, 0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Leadership", "Ldr",
     "His influence on his teammates.", "", 0.0f, 0.0f, 0.0f, 0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Determination", "Det",
     "His will to succeed, and to keep going when the match goes against "
     "him.",
     "", 0.0f, 0.0f, 0.0f, 0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Bravery", "Bra",
     "Committing to a ball where he may get hurt: a block, a header in "
     "traffic.",
     "", 0.0f, 0.0f, 0.0f, 0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Aggression", "Agg",
     "How hard he goes into physical contests.", "", 0.0f, 0.0f, 0.0f, 0.0f},
    {DerivedLayer::FUTURE_GAME_STAT, "Natural fitness", "Nat",
     "How well his body holds its fitness and recovers between efforts and "
     "between matches.",
     "", 0.0f, 0.0f, 0.0f, 0.0f},
    // ---- Match-engine-only stats ------------------------------------------
    // The fatigue plan (4.1): from the research ranges, a little wider.
    {DerivedLayer::MATCH_ENGINE_ONLY, "Endurance", "End",
     "Aerobic fitness: his critical speed, the fastest he can run without "
     "spending his burst capacity.",
     "m/s", 3.0f, 4.5f, 2.9f, 4.6f, STAT_1_RATING, STAT_99_RATING},
    {DerivedLayer::MATCH_ENGINE_ONLY, "Burst capacity", "Bur",
     "How far he can run above his critical speed before he must ease off.",
     "m", 80.0f, 220.0f, 75.0f, 225.0f, STAT_1_RATING, STAT_99_RATING},
    {DerivedLayer::MATCH_ENGINE_ONLY, "Fuel capacity", "Fue",
     "How long he could run at 90% of his critical speed before his tank is "
     "empty: the whole match's work.",
     "min", 60.0f, 150.0f, 55.0f, 155.0f, STAT_1_RATING, STAT_99_RATING},
    {DerivedLayer::MATCH_ENGINE_ONLY, "Recovery rate", "Rec",
     "How fast his burst capacity refills when he slows down: the time "
     "constant of the refill while walking (slower the harder he still "
     "runs).",
     "s", 300.0f, 100.0f, 95.0f, 305.0f, STAT_1_RATING, STAT_99_RATING},
    // The fast phase of D' reconstitution: 22 +/- 12 s (Chorley 2022);
    // phosphocreatine is rebuilt faster in aerobically trained muscle
    // (Takahashi 1995), so from Stamina.
    {DerivedLayer::MATCH_ENGINE_ONLY, "Fast recovery", "Frc",
     "How fast the quick part of his burst capacity comes back: the time "
     "constant of its refill while walking (slower the harder he still "
     "works).",
     "s", 34.0f, 10.0f, 9.5f, 34.5f, STAT_1_RATING, STAT_99_RATING},
}};

namespace DerivedStats
{
constexpr DerivedRecipe only(BaseAttribute attribute)
{
  DerivedRecipe recipe;
  recipe.weights[static_cast<std::size_t>(attribute)] = 1.0f;
  return recipe;
}

constexpr DerivedRecipe accelerationRecipe()
{
  // Pace and Physicality, weighted as the old fixed formula weighed them.
  using P = MatchTuning::Player;
  constexpr float total = P::ACCELERATION_PACE + P::ACCELERATION_PHYSICALITY;
  DerivedRecipe recipe;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::PACE)] =
      P::ACCELERATION_PACE / total;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::PHYSICALITY)] =
      P::ACCELERATION_PHYSICALITY / total;
  return recipe;
}

constexpr DerivedRecipe agilityRecipe()
{
  // Mirrors acceleration's blend of Pace and Physicality for now.
  return accelerationRecipe();
}

constexpr DerivedRecipe firstTouchRecipe()
{
  // Dribbling for close control, Passing standing in for clean technique
  // (there is no Technique stat).
  DerivedRecipe recipe;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::DRIBBLING)] = 0.6f;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::PASSING)] = 0.4f;
  return recipe;
}

constexpr DerivedRecipe techniqueRecipe()
{
  // A placeholder until the game carries Technique: the three stats that
  // strike or touch the ball.
  DerivedRecipe recipe;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::PASSING)] = 0.4f;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::DRIBBLING)] = 0.3f;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::SHOOTING)] = 0.3f;
  return recipe;
}

constexpr DerivedRecipe headingRecipe()
{
  // A stand-in until the game carries Heading: strikers and centre-backs
  // both head the ball well.
  DerivedRecipe recipe;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::SHOOTING)] = 0.5f;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::DEFENDING)] = 0.5f;
  return recipe;
}

constexpr DerivedRecipe jumpingReachRecipe()
{
  // His height plus his jump. The jump is a stand-in blend of Pace (spring)
  // and Physicality until the game carries a jumping stat.
  DerivedRecipe recipe;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::PACE)] = 0.5f;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::PHYSICALITY)] = 0.5f;
  recipe.heightPartLow = MatchTuning::Aerial::JUMP_LOW_METRES;
  recipe.heightPartHigh = MatchTuning::Aerial::JUMP_HIGH_METRES;
  return recipe;
}

constexpr DerivedRecipe aerialReachRecipe()
{
  // His height plus his arm and jump: Goalkeeping stands in for the part
  // on top of his height (0.85 m at 1 to 1.35 m at 99).
  DerivedRecipe recipe = only(BaseAttribute::GOALKEEPING);
  recipe.heightPartLow = 0.85f;
  recipe.heightPartHigh = 1.35f;
  return recipe;
}

constexpr DerivedRecipe balanceRecipe()
{
  // A stand-in until the game carries Balance: half Physicality (how solid
  // he is), half Agility (how quickly he fixes his footing). A future stat
  // reads current stats only, so Agility's half is its recipe's blend.
  const DerivedRecipe agility = agilityRecipe();
  DerivedRecipe recipe;
  for (std::size_t index = 0; index < BASE_ATTRIBUTE_COUNT; ++index)
    recipe.weights[index] = 0.5f * agility.weights[index];
  recipe.weights[static_cast<std::size_t>(BaseAttribute::PHYSICALITY)] += 0.5f;
  return recipe;
}

struct Part
{
  BaseAttribute attribute;
  float weight;
};

/** A blend of current stats (weights summing to 1) with a spread. */
constexpr DerivedRecipe blend(std::initializer_list<Part> parts, float spread)
{
  DerivedRecipe recipe;
  for (const Part& part : parts)
    recipe.weights[static_cast<std::size_t>(part.attribute)] = part.weight;
  recipe.spread = spread;
  return recipe;
}

/** His general level (the nine equally) with a spread: for a stat the
 * current stats say little about. A keeper's level comes out a little
 * lower, his outfield stats being weak. */
constexpr DerivedRecipe levelRecipe(float spread)
{
  DerivedRecipe recipe;
  recipe.weights.fill(1.0f / static_cast<float>(BASE_ATTRIBUTE_COUNT));
  recipe.spread = spread;
  return recipe;
}

/** Spreads for the player profile's attributes: TIGHT for a stat that is
 * nearly its current stat (Finishing and Shooting), WIDE for one the
 * current stats barely predict (Leadership). Our own estimates. */
inline constexpr float SPREAD_TIGHT = 0.06f;
inline constexpr float SPREAD_MEDIUM = 0.10f;
inline constexpr float SPREAD_LOOSE = 0.12f;
inline constexpr float SPREAD_WIDE = 0.15f;

constexpr DerivedRecipe burstRecipe()
{
  // D' grows with the speed reserve above critical speed, so half Stamina,
  // half Acceleration (fatigue plan 4.1).
  DerivedRecipe recipe;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::STAMINA)] = 0.5f;
  recipe.futureWeights[static_cast<std::size_t>(DerivedStat::ACCELERATION)] =
      0.5f;
  return recipe;
}
}  // namespace DerivedStats

/** The recipes a handshake uses unless it has its own (the Aardvark's).
 * Future stats: agility and acceleration blend Pace and Physicality, first
 * touch Dribbling and Passing, technique Passing, Dribbling and Shooting,
 * heading Shooting and Defending, jumping reach his height plus a jump from
 * Pace and Physicality, tackling Defending, strength Physicality, balance
 * half Physicality and half Agility's blend, reflexes, handling,
 * throwing and command of area Goalkeeping. The player profile's
 * attributes blend the current stats nearest them, each with a spread of
 * its own (our estimates). Match-engine-only: the endurance stats echo
 * Stamina, except burst capacity (half Stamina, half Acceleration). */
inline constexpr DerivedRecipes DEFAULT_DERIVED_RECIPES{{
    DerivedStats::agilityRecipe(),
    DerivedStats::firstTouchRecipe(),
    DerivedStats::accelerationRecipe(),
    DerivedStats::techniqueRecipe(),
    DerivedStats::headingRecipe(),
    DerivedStats::jumpingReachRecipe(),
    DerivedStats::only(BaseAttribute::DEFENDING),
    DerivedStats::only(BaseAttribute::PHYSICALITY),
    DerivedStats::balanceRecipe(),
    DerivedStats::only(BaseAttribute::GOALKEEPING),
    DerivedStats::only(BaseAttribute::GOALKEEPING),
    DerivedStats::aerialReachRecipe(),
    DerivedStats::only(BaseAttribute::GOALKEEPING),
    DerivedStats::only(BaseAttribute::GOALKEEPING),
    // The player profile's attributes.
    DerivedStats::blend({{BaseAttribute::SHOOTING, 1.0f}},
                        DerivedStats::SPREAD_TIGHT),  // Finishing
    DerivedStats::blend({{BaseAttribute::SHOOTING, 0.7f},
                         {BaseAttribute::PASSING, 0.3f}},
                        DerivedStats::SPREAD_MEDIUM),  // Long shots
    DerivedStats::blend({{BaseAttribute::SHOOTING, 0.8f},
                         {BaseAttribute::PASSING, 0.2f}},
                        DerivedStats::SPREAD_LOOSE),  // Penalties
    DerivedStats::blend({{BaseAttribute::PASSING, 0.7f},
                         {BaseAttribute::VISION, 0.3f}},
                        DerivedStats::SPREAD_TIGHT),  // Crossing
    DerivedStats::blend({{BaseAttribute::PASSING, 0.6f},
                         {BaseAttribute::VISION, 0.4f}},
                        DerivedStats::SPREAD_MEDIUM),  // Corners
    DerivedStats::blend({{BaseAttribute::SHOOTING, 0.5f},
                         {BaseAttribute::PASSING, 0.5f}},
                        DerivedStats::SPREAD_LOOSE),  // Free kicks
    DerivedStats::blend({{BaseAttribute::DEFENDING, 0.8f},
                         {BaseAttribute::PHYSICALITY, 0.2f}},
                        DerivedStats::SPREAD_TIGHT),  // Marking
    DerivedStats::blend({{BaseAttribute::VISION, 0.5f},
                         {BaseAttribute::DEFENDING, 0.25f},
                         {BaseAttribute::SHOOTING, 0.25f}},
                        DerivedStats::SPREAD_MEDIUM),  // Anticipation
    DerivedStats::blend({{BaseAttribute::VISION, 0.6f},
                         {BaseAttribute::PASSING, 0.4f}},
                        DerivedStats::SPREAD_MEDIUM),  // Decisions
    DerivedStats::blend({{BaseAttribute::DRIBBLING, 0.6f},
                         {BaseAttribute::VISION, 0.4f}},
                        DerivedStats::SPREAD_LOOSE),  // Flair
    DerivedStats::blend({{BaseAttribute::SHOOTING, 0.35f},
                         {BaseAttribute::PASSING, 0.35f},
                         {BaseAttribute::DRIBBLING, 0.3f}},
                        DerivedStats::SPREAD_MEDIUM),  // Composure
    DerivedStats::blend({{BaseAttribute::SHOOTING, 0.5f},
                         {BaseAttribute::VISION, 0.3f},
                         {BaseAttribute::PACE, 0.2f}},
                        DerivedStats::SPREAD_MEDIUM),  // Off the ball
    DerivedStats::blend({{BaseAttribute::DEFENDING, 0.7f},
                         {BaseAttribute::VISION, 0.3f}},
                        DerivedStats::SPREAD_TIGHT),  // Positioning
    DerivedStats::blend({{BaseAttribute::DEFENDING, 0.5f},
                         {BaseAttribute::VISION, 0.5f}},
                        DerivedStats::SPREAD_MEDIUM),  // Concentration
    DerivedStats::blend({{BaseAttribute::STAMINA, 0.7f},
                         {BaseAttribute::DEFENDING, 0.3f}},
                        DerivedStats::SPREAD_LOOSE),  // Work rate
    DerivedStats::blend({{BaseAttribute::PASSING, 0.4f},
                         {BaseAttribute::VISION, 0.3f},
                         {BaseAttribute::STAMINA, 0.3f}},
                        DerivedStats::SPREAD_LOOSE),  // Teamwork
    DerivedStats::levelRecipe(DerivedStats::SPREAD_WIDE),  // Leadership
    DerivedStats::levelRecipe(DerivedStats::SPREAD_WIDE),  // Determination
    DerivedStats::blend({{BaseAttribute::PHYSICALITY, 0.6f},
                         {BaseAttribute::DEFENDING, 0.4f}},
                        DerivedStats::SPREAD_LOOSE),  // Bravery
    DerivedStats::blend({{BaseAttribute::PHYSICALITY, 0.5f},
                         {BaseAttribute::DEFENDING, 0.5f}},
                        DerivedStats::SPREAD_WIDE),  // Aggression
    DerivedStats::blend({{BaseAttribute::STAMINA, 0.6f},
                         {BaseAttribute::PHYSICALITY, 0.4f}},
                        DerivedStats::SPREAD_LOOSE),  // Natural fitness
    // Match-engine-only.
    DerivedStats::only(BaseAttribute::STAMINA),
    DerivedStats::burstRecipe(),
    DerivedStats::only(BaseAttribute::STAMINA),
    DerivedStats::only(BaseAttribute::STAMINA),
    DerivedStats::only(BaseAttribute::STAMINA),
}};

inline const DerivedStatInfo& derivedStatInfo(DerivedStat stat)
{
  return DERIVED_STATS[static_cast<std::size_t>(stat)];
}

/** One player's own deviation in a stat, in standard deviations: keyed on
 * his id and the stat's name, so it is the same every match and draws
 * nothing from the match's random numbers. A sum of four uniforms (close to
 * a normal, held within +/-3.46) from integer hashing and plain float
 * arithmetic, so it is identical on every platform. Renaming a stat
 * re-deals it. */
inline float playerStatDeviation(std::uint32_t playerId,
                                 std::string_view statName)
{
  std::uint64_t key = 14695981039346656037ULL;  // FNV-1a of the name.
  for (const char c : statName)
  {
    key ^= static_cast<unsigned char>(c);
    key *= 1099511628211ULL;
  }
  std::uint64_t state = key ^ (std::uint64_t{playerId} * 0x9E3779B97F4A7C15ULL);
  float sum = 0.0f;
  for (int draw = 0; draw < 4; ++draw)
  {
    // SplitMix64.
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t mixed = state;
    mixed = (mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9ULL;
    mixed = (mixed ^ (mixed >> 27)) * 0x94D049BB133111EBULL;
    mixed ^= mixed >> 31;
    sum += static_cast<float>(mixed >> 40) / 16777216.0f;  // [0, 1)
  }
  // Four uniforms: mean 2, variance 1/3.
  return (sum - 2.0f) * 1.7320508f;
}

inline std::string_view derivedLayerName(DerivedLayer layer)
{
  return layer == DerivedLayer::FUTURE_GAME_STAT ? "future game stat"
                                                 : "match engine only";
}

/** A rating's physical value (stats with a unit): the straight line through
 * its two anchors, clamped to the safe range. */
inline float derivedPhysicalValue(DerivedStat stat, float rating)
{
  const DerivedStatInfo& info = derivedStatInfo(stat);
  const float share =
      (rating - info.lowRating) / (info.highRating - info.lowRating);
  const float value = info.lowValue + (info.highValue - info.lowValue) * share;
  return std::clamp(value, info.minValue, info.maxValue);
}

/** The rating of a physical value (stats with a unit): the inverse of
 * derivedPhysicalValue's line, unclamped. */
inline float derivedRatingOf(DerivedStat stat, float value)
{
  const DerivedStatInfo& info = derivedStatInfo(stat);
  const float share = (value - info.lowValue) / (info.highValue - info.lowValue);
  return info.lowRating + (info.highRating - info.lowRating) * share;
}

/** His weight (kg), a proxy until the game carries one (collision plan step
 * 1): his height squared times a body-mass index from his Strength rating,
 * a straight line across the stat scale. */
inline float bodyMassKilograms(float heightMetres, float strengthRating)
{
  using P = MatchTuning::Player;
  const float share =
      (strengthRating - STAT_1_RATING) / (STAT_99_RATING - STAT_1_RATING);
  const float index =
      P::BODY_MASS_INDEX_AT_STRENGTH_1 +
      (P::BODY_MASS_INDEX_AT_STRENGTH_99 - P::BODY_MASS_INDEX_AT_STRENGTH_1) *
          share;
  return heightMetres * heightMetres * index;
}

/** A value on a straight line from `at1` (the rating at stat 1) to `at99`
 * (at stat 99), held at the ends. */
inline float alongStatScale(float rating, float at1, float at99)
{
  const float share = std::clamp(
      (rating - STAT_1_RATING) / (STAT_99_RATING - STAT_1_RATING), 0.0f, 1.0f);
  return at1 + (at99 - at1) * share;
}

/** A stat built on his height (DerivedRecipe::onHeight): the rating of his
 * height plus the part `blend` rates, unclamped. */
inline float derivedRatingOnHeight(DerivedStat stat, const DerivedRecipe& recipe,
                                   float blend, float heightMetres)
{
  const float share =
      (blend - STAT_1_RATING) / (STAT_99_RATING - STAT_1_RATING);
  const float part = recipe.heightPartLow +
                     (recipe.heightPartHigh - recipe.heightPartLow) * share;
  return derivedRatingOf(stat, heightMetres + part);
}
