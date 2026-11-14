// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

// Small helpers shared by the match engine's source files: the engine body
// (match_engine.cpp) and the brains (match_brain_*.cpp). Not part of the
// engine's interface; include only from those files.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>
#include <string_view>

#include "model/match_engine.h"
#include "model/match_ground.h"

// The kinematics kernel runs 8 players at a time where the processor
// supports it; every variant does the same IEEE arithmetic.
#if defined(__x86_64__) && defined(__linux__) && defined(__GNUC__)
#define FM_KERNEL_CLONES __attribute__((target_clones("avx2", "default")))
#else
#define FM_KERNEL_CLONES
#endif

namespace MatchEngineInternal
{
constexpr float EPSILON = 0.00001f;

constexpr float TWO_PI = 2.0f * std::numbers::pi_v<float>;
/** Share of the drag-free horizontal travel a lofted ball really covers. */
constexpr float AIR_TRAVEL_SHARE = 0.88f;
constexpr double HIGHLIGHT_LEAD = MatchTuning::Playback::HIGHLIGHT_LEAD_SECONDS;
constexpr double HIGHLIGHT_TAIL = MatchTuning::Playback::HIGHLIGHT_TAIL_SECONDS;
constexpr double HIGHLIGHT_MERGE_GAP = MatchTuning::Playback::MERGE_GAP_SECONDS;
constexpr double PREDICTION_HORIZON =
    MatchTuning::Playback::PREDICTION_HORIZON_SECONDS;
constexpr double STEP_SECONDS = MatchTuning::Timing::FIXED_STEP_SECONDS;
constexpr auto RUN_TIMING_EPOCH_STEPS =
    static_cast<std::uint32_t>(MatchTuning::Passing::RUN_TIMING_EPOCH_SECONDS /
                               MatchTuning::Timing::FIXED_STEP_SECONDS);

/** Where an angle between body and run (0 forward to pi backward) falls
 * among the eight directions (MatchTuning::Player::DIRECTION_*): the
 * direction below it (0-3), and how far on toward the next (0-1). */
struct DirectionMix
{
  std::size_t below = 0;
  float onward = 0.0f;
};
inline DirectionMix directionMix(float angle)
{
  constexpr float STEP = 0.25f * 3.14159265f;
  const float steps = std::clamp(angle / STEP, 0.0f, 4.0f);
  const std::size_t below = std::min<std::size_t>(static_cast<std::size_t>(steps), 3);
  return {below, steps - static_cast<float>(below)};
}
/** A direction figure at a mix, blended between its two directions. */
template <typename Table>
inline float directionValue(const Table& table, DirectionMix mix)
{
  return static_cast<float>(table[mix.below]) +
         (static_cast<float>(table[mix.below + 1]) -
          static_cast<float>(table[mix.below])) *
             mix.onward;
}

/** A spot kept on the pitch: where a player means to be, receive or put
 * the ball. */
inline float clampToPitchX(const MatchPitch& pitch, float x)
{
  return std::clamp(x, MatchTuning::Pitch::PLAYER_EDGE_END_METRES,
                    pitch.length - MatchTuning::Pitch::PLAYER_EDGE_END_METRES);
}
inline float clampToPitchY(const MatchPitch& pitch, float y)
{
  return std::clamp(y, MatchTuning::Pitch::PLAYER_EDGE_SIDE_METRES,
                    pitch.width - MatchTuning::Pitch::PLAYER_EDGE_SIDE_METRES);
}
inline PitchPoint clampToPitch(const MatchPitch& pitch, PitchPoint at)
{
  return PitchPoint{clampToPitchX(pitch, at.x), clampToPitchY(pitch, at.y)};
}

/** A body kept where it can physically be: inside the advertising boards. */
inline float clampToBoardsX(const MatchPitch& pitch, float x)
{
  return std::clamp(x, pitch.bodyMinX, pitch.bodyMaxX);
}
inline float clampToBoardsY(const MatchPitch& pitch, float y)
{
  return std::clamp(y, pitch.bodyMinY, pitch.bodyMaxY);
}
inline PitchPoint clampToBoards(const MatchPitch& pitch, PitchPoint at)
{
  return PitchPoint{clampToBoardsX(pitch, at.x), clampToBoardsY(pitch, at.y)};
}

/**
 * A body moving from `from` to `to` kept out of the goal frames: the posts,
 * side nets and back net are thin walls he cannot cross (the goal is open
 * at the front, under the bar), and he stays on the side of each he started
 * on, his footprint clear of it. An axis left alone is returned exactly.
 */
inline PitchPoint keepOutOfGoalFrames(const MatchPitch& pitch, PitchPoint from,
                                      PitchPoint to)
{
  using P = MatchTuning::Pitch;
  constexpr float R = MatchTuning::Body::RADIUS_METRES;
  const float W =
      pitch.goal.halfWidth + 0.5f * pitch.goal.post;
  const float D = pitch.goal.netDepth;
  // Local metres at the nearer goal: u behind its line, v across from its
  // centre.
  const bool right = to.x > pitch.atX(P::CENTRE);
  const float lineX = right ? pitch.length : 0.0f;
  const float behind = right ? 1.0f : -1.0f;
  const float u0 = (from.x - lineX) * behind;
  const float v0 = from.y - pitch.atY(P::CENTRE);
  float u = (to.x - lineX) * behind;
  float v = to.y - pitch.atY(P::CENTRE);
  // Nowhere near this frame, before or after: nothing to meet.
  if (std::max(u0, u) < -R || std::min(u0, u) > D + R ||
      std::min(v0, v) > W + R || std::max(v0, v) < -(W + R))
    return to;
  bool movedU = false;
  bool movedV = false;
  // The side nets, post to back net.
  for (const float side : {-W, W})
  {
    if (u < 0.0f || u > D) continue;
    const float keep = v0 > side || (v0 == side && side > 0.0f) ? 1.0f : -1.0f;
    if ((v - side) * keep < R)
    {
      v = side + keep * R;
      movedV = true;
    }
  }
  // The back net.
  if (std::abs(v) <= W)
  {
    const float keep = u0 >= D ? 1.0f : -1.0f;
    if ((u - D) * keep < R)
    {
      u = D + keep * R;
      movedU = true;
    }
  }
  // The posts and the back corners: round them.
  for (const float cornerU : {0.0f, D})
    for (const float cornerV : {-W, W})
    {
      const float du = u - cornerU;
      const float dv = v - cornerV;
      const float gap = std::sqrt(du * du + dv * dv);
      if (gap >= R) continue;
      const float fromU = u0 - cornerU;
      const float fromV = v0 - cornerV;
      const float fromGap = std::sqrt(fromU * fromU + fromV * fromV);
      // On it: back out the way he came (or out of the goal).
      const float awayU = gap > 1e-4f ? du / gap : fromGap > 1e-4f ? fromU / fromGap : -1.0f;
      const float awayV = gap > 1e-4f ? dv / gap : fromGap > 1e-4f ? fromV / fromGap : 0.0f;
      u = cornerU + awayU * R;
      v = cornerV + awayV * R;
      movedU = true;
      movedV = true;
    }
  return PitchPoint{movedU ? lineX + u * behind : to.x,
                    movedV ? pitch.atY(P::CENTRE) + v : to.y};
}

/**
 * A body's step from `from` to `to` (metres) kept off one thin upright wall
 * at `plane` across one axis (a wall along x when `alongX`), standing from
 * `low` to `high` along it: he stays on the side he started on, his
 * footprint clear of it, unless he passes beyond an end. Returns whether
 * it moved him.
 */
inline bool keepOffWall(bool alongX, float plane, float low, float high,
                        Vector2F from, Vector2F& to)
{
  constexpr float R = MatchTuning::Body::RADIUS_METRES;
  const float across0 = alongX ? from.y : from.x;
  float& across1 = alongX ? to.y : to.x;
  const float along0 = alongX ? from.x : from.y;
  const float along1 = alongX ? to.x : to.y;
  const float keep = across0 < plane ? -1.0f : 1.0f;
  const float face = plane + keep * R;
  if ((across1 - face) * keep >= 0.0f) return false;
  // Where along the wall his path reaches its face (where he starts, if he
  // is already over it).
  const float span = across1 - across0;
  const float t = (across0 - face) * keep > 0.0f && std::abs(span) > 1e-6f
                      ? (face - across0) / span
                      : 0.0f;
  const float along = along0 + (along1 - along0) * t;
  if (along < low || along > high) return false;
  across1 = face;
  return true;
}

/** A body's step to `to` (metres) kept off a wall's free end at `end`:
 * round it, his footprint clear. Returns whether it moved him. */
inline bool keepOffWallEnd(Vector2F end, Vector2F from, Vector2F& to)
{
  constexpr float R = MatchTuning::Body::RADIUS_METRES;
  const float dx = to.x - end.x;
  const float dy = to.y - end.y;
  const float gap = std::sqrt(dx * dx + dy * dy);
  if (gap >= R) return false;
  const float fromX = from.x - end.x;
  const float fromY = from.y - end.y;
  const float fromGap = std::sqrt(fromX * fromX + fromY * fromY);
  // On it: back out the way he came.
  const float awayX = gap > 1e-4f ? dx / gap : fromGap > 1e-4f ? fromX / fromGap : 0.0f;
  const float awayY = gap > 1e-4f ? dy / gap : fromGap > 1e-4f ? fromY / fromGap : -1.0f;
  to = {end.x + awayX * R, end.y + awayY * R};
  return true;
}

/**
 * A body moving from `from` to `to` (metres) kept where it can
 * physically be round the pitch (MatchGround): inside the stands' front
 * walls, off the boards, the dugout fronts and the tunnel walls. The gaps
 * in the boards let him through to the technical areas, the dugouts (at
 * their open ends) and down the tunnel. An axis left alone is returned
 * exactly.
 */
inline PitchPoint keepOffGround(const MatchPitch& pitch, PitchPoint from,
                                PitchPoint to)
{
  using P = MatchTuning::Pitch;
  const MatchGround::Layout& G = pitch.ground;
  constexpr float R = MatchTuning::Body::RADIUS_METRES;
  // A walled pitch: the walls, and nothing beyond them (no furniture).
  if (pitch.edge == PitchEdge::WALLS)
    return PitchPoint{std::clamp(to.x, pitch.bodyMinX, pitch.bodyMaxX),
                      std::clamp(to.y, pitch.bodyMinY, pitch.bodyMaxY)};
  // Inside the boards, before and after: nothing to meet.
  const auto insideBoards = [&pitch](PitchPoint at)
  {
    return at.x >= pitch.bodyMinX && at.x <= pitch.bodyMaxX &&
           at.y >= pitch.bodyMinY && at.y <= pitch.bodyMaxY;
  };
  if (insideBoards(from) && insideBoards(to)) return to;
  const Vector2F start{from.x, from.y};
  const Vector2F wanted{to.x, to.y};
  // The stands' front walls (and the tunnel's far end) all round.
  Vector2F end{
      std::clamp(wanted.x, -P::STAND_END_METRES + R, pitch.length + P::STAND_END_METRES - R),
      std::clamp(wanted.y, -P::STAND_SIDE_METRES + R, G.tunnel.y1 - R)};
  bool moved = end.x != wanted.x || end.y != wanted.y;
  // The boards: whole along the top and the ends, open along the bottom.
  moved |= keepOffWall(true, -P::BOARDS_SIDE_METRES, -P::BOARDS_END_METRES,
                       pitch.length + P::BOARDS_END_METRES, start, end);
  for (const float plane : {-P::BOARDS_END_METRES, pitch.length + P::BOARDS_END_METRES})
    moved |= keepOffWall(false, plane, -P::BOARDS_SIDE_METRES, G.boardsY,
                         start, end);
  for (const MatchGround::Span& span : G.bottomBoards)
  {
    moved |= keepOffWall(true, G.boardsY, span.from, span.to, start, end);
    moved |= keepOffWallEnd({span.from, G.boardsY}, start, end);
    moved |= keepOffWallEnd({span.to, G.boardsY}, start, end);
  }
  // The dugouts' fronts (their backs are the stand wall).
  for (const MatchGround::Rect& dugout : G.dugouts)
  {
    moved |= keepOffWall(true, dugout.y0, dugout.x0, dugout.x1, start, end);
    moved |= keepOffWallEnd({dugout.x0, dugout.y0}, start, end);
    moved |= keepOffWallEnd({dugout.x1, dugout.y0}, start, end);
  }
  // The bottom stand's front wall, open at the tunnel mouth, and the
  // tunnel's sides.
  for (const MatchGround::Span& span : G.bottomStandWall)
    moved |= keepOffWall(true, G.standY, span.from, span.to, start, end);
  for (const float side : {G.tunnel.x0, G.tunnel.x1})
  {
    moved |= keepOffWall(false, side, G.tunnel.y0, G.tunnel.y1, start, end);
    moved |= keepOffWallEnd({side, G.tunnel.y0}, start, end);
  }
  if (!moved) return to;
  return PitchPoint{end.x != wanted.x ? end.x : to.x,
                    end.y != wanted.y ? end.y : to.y};
}

/** Deep in the tunnel (touchline plan stage 4): out of sight and out of
 * play. */
inline bool deepInTunnel(const MatchPitch& pitch, PitchPoint at)
{
  return at.y >
         pitch.ground.standY + MatchTuning::WalkOut::IN_METRES;
}

/** Where a walker may mean to go: inside the boards, or the tunnel and
 * its walkway. */
inline PitchPoint clampToReach(const MatchPitch& pitch, PitchPoint at)
{
  const MatchGround::Layout& G = pitch.ground;
  const float x = at.x;
  const float y = at.y;
  if (x >= G.tunnel.x0 && x <= G.tunnel.x1 && y >= G.boardsY - 1.0f && y <= G.tunnel.y1)
    return at;
  return clampToBoards(pitch, at);
}

/**
 * The next point on a body's way from `at` to `target` (metres) through
 * the tunnel: out along it and through the gap in the boards before
 * crossing the pitch, or to the gap and straight in. Otherwise the target.
 */
inline Vector2F routeViaTunnel(const MatchPitch& pitch, Vector2F at,
                               Vector2F target)
{
  const MatchGround::Layout& G = pitch.ground;
  constexpr float MARGIN = 0.6f;
  const auto inLane = [&G](Vector2F p)
  { return p.x > G.tunnel.x0 && p.x < G.tunnel.x1 && p.y > G.boardsY - 0.5f; };
  const auto inTunnel = [&G](Vector2F p) { return p.y > G.standY - 0.2f; };
  const float laneX = std::clamp(at.x, G.tunnel.x0 + MARGIN, G.tunnel.x1 - MARGIN);
  // Coming out: up the tunnel, then through the gap.
  if (inTunnel(at) && !inTunnel(target)) return {laneX, G.standY - 1.0f};
  if (inLane(at) && target.y < G.boardsY - 0.5f) return {laneX, G.boardsY - 1.5f};
  // Going in: to the gap first, unless already lined up with it.
  if (inTunnel(target) && !inLane(at))
  {
    const float x = std::clamp(target.x, G.tunnel.x0 + MARGIN, G.tunnel.x1 - MARGIN);
    const bool lined = std::abs(at.x - x) < 0.5f && at.y > G.boardsY - 3.0f;
    if (!lined) return {x, G.boardsY - 1.5f};
  }
  return target;
}

/**
 * Where a player at `at` (metres) leaves the pitch: the nearest point of
 * the boundary, a little over it (beside the goal, not through it).
 */
inline Vector2F nearestExit(const MatchPitch& pitch, Vector2F at)
{
  const MatchGround::Layout& G = pitch.ground;
  const float out = MatchTuning::Body::RADIUS_METRES + 0.3f;
  const std::array<std::pair<float, Vector2F>, 4> exits{{
      {at.y, {at.x, -out}},
      {pitch.width - at.y, {at.x, pitch.width + out}},
      {at.x, {-out, at.y}},
      {pitch.length - at.x, {pitch.length + out, at.y}},
  }};
  Vector2F exit = std::ranges::min(exits, {}, &std::pair<float, Vector2F>::first).second;
  const float CLEAR_OF_POST = pitch.goal.halfWidth + 1.5f;
  if ((exit.x < 0.0f || exit.x > pitch.length) && std::abs(exit.y - 0.5f * pitch.width) < CLEAR_OF_POST)
    exit.y = 0.5f * pitch.width + (exit.y < 0.5f * pitch.width ? -CLEAR_OF_POST : CLEAR_OF_POST);
  return exit;
}

/** A body's step from `from` to `to`, where it can physically go: round
 * the pitch (keepOffGround) and out of the goal frames. */
inline PitchPoint moveBody(const MatchPitch& pitch, PitchPoint from, PitchPoint to)
{
  // A training pitch has no goals yet (T3): no frames to meet.
  if (pitch.surface == PitchSurface::TRAINING)
    return keepOffGround(pitch, from, to);
  return keepOutOfGoalFrames(pitch, from, keepOffGround(pitch, from, to));
}

inline float length(Vector2F vector)
{
  return std::sqrt(vector.x * vector.x + vector.y * vector.y);
}

/**
 * atan2 approximation (max error ~0.0015 rad) for per-step facing updates;
 * the facing angle is presentation state only.
 */
inline float fastAtan2(float y, float x)
{
  const float absX = std::abs(x);
  const float absY = std::abs(y);
  if (absX < 1e-12f && absY < 1e-12f) return 0.0f;
  const float ratio = std::min(absX, absY) / std::max(absX, absY);
  const float squared = ratio * ratio;
  float angle =
      ((-0.0464964749f * squared + 0.15931422f) * squared - 0.327622764f) *
          squared * ratio +
      ratio;
  if (absY > absX) angle = 1.57079637f - angle;
  if (x < 0.0f) angle = 3.14159274f - angle;
  return y < 0.0f ? -angle : angle;
}

constexpr float INVERSE_FATIGUE_RANGE =
    1.0f / (1.0f - MatchTuning::Player::MINIMUM_STAMINA);

/** Distance in metres between two points. */
inline float distance(PitchPoint first, PitchPoint second)
{
  return length(first - second);
}

inline Vector2F normalized(Vector2F vector)
{
  const float magnitude = length(vector);
  if (magnitude <= EPSILON) return {0.0f, 0.0f};
  return {vector.x / magnitude, vector.y / magnitude};
}

/** Unit direction on the ground (metric) from one point to another. */
inline Vector2F metricDirection(PitchPoint from, PitchPoint to)
{
  return normalized(to - from);
}

inline float uniform01(std::mt19937& engine)
{
  // Portable mapping of the 32-bit output to [0, 1) (24 bits of mantissa).
  return static_cast<float>(engine() >> 8U) * 0x1p-24f;
}

/** Standard normal sample (Box-Muller) from portable uniforms. */
inline float gaussian(std::mt19937& engine)
{
  const float first = std::max(uniform01(engine), 1e-7f);
  const float second = uniform01(engine);
  return std::sqrt(-2.0f * std::log(first)) * std::cos(TWO_PI * second);
}

inline bool stateClockRuns(MatchState state)
{
  return state != MatchState::KICK_OFF && state != MatchState::HALF_TIME &&
         state != MatchState::FULL_TIME &&
         state != MatchState::PENALTY_SHOOTOUT;
}

inline bool active(const MatchPlayer& player)
{
  return player.player != nullptr && player.onPitch;
}

inline bool isHighlightTrigger(MatchEventType type)
{
  return type == MatchEventType::SHOT || type == MatchEventType::GOAL ||
         type == MatchEventType::OWN_GOAL || type == MatchEventType::PENALTY ||
         type == MatchEventType::YELLOW_CARD ||
         type == MatchEventType::SECOND_YELLOW ||
         type == MatchEventType::RED_CARD ||
         type == MatchEventType::PENALTY_SHOOTOUT;
}

/** Whether a point lies in the penalty area defended by the given side. */
inline bool inPenaltyArea(const MatchPitch& pitch, PitchPoint position,
                          bool defendingHome)
{
  const float HALF_WIDTH =
      MatchTuning::Pitch::PENALTY_AREA_HALF_WIDTH_METRES;
  if (std::abs(position.y - pitch.atY(MatchTuning::Pitch::CENTRE)) > HALF_WIDTH)
    return false;
  return defendingHome
             ? position.x <= MatchTuning::Pitch::PENALTY_AREA_DEPTH_METRES
             : position.x >= pitch.length - MatchTuning::Pitch::PENALTY_AREA_DEPTH_METRES;
}

inline bool isDefensiveRole(PlayerRole role)
{
  return role == PlayerRole::CB || role == PlayerRole::LB ||
         role == PlayerRole::RB || role == PlayerRole::CDM;
}

inline bool isAttackingRole(PlayerRole role)
{
  return role == PlayerRole::ST || role == PlayerRole::LW ||
         role == PlayerRole::RW || role == PlayerRole::CAM;
}

/** Coarse role group used to pick like-for-like substitutes. */
inline int roleGroup(PlayerRole role)
{
  if (role == PlayerRole::GK) return 0;
  if (role == PlayerRole::CB || role == PlayerRole::LB ||
      role == PlayerRole::RB)
    return 1;
  if (isAttackingRole(role)) return 3;
  return 2;
}

/** Stable insertion sort of the first `count` entries (tiny fixed arrays). */
template <typename Array, typename Less>
inline void insertionSort(Array& values, std::size_t count, Less less)
{
  for (std::size_t index = 1; index < count; ++index)
  {
    auto value = values[index];
    std::size_t slot = index;
    while (slot > 0 && less(value, values[slot - 1]))
    {
      values[slot] = values[slot - 1];
      --slot;
    }
    values[slot] = value;
  }
}

inline std::uint64_t splitMix64(std::uint64_t value)
{
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

inline std::string_view stateName(MatchState state)
{
  switch (state)
  {
    case MatchState::KICK_OFF:
      return "kick_off";
    case MatchState::PLAYING:
      return "playing";
    case MatchState::THROW_IN:
      return "throw_in";
    case MatchState::GOAL_KICK:
      return "goal_kick";
    case MatchState::CORNER_KICK:
      return "corner_kick";
    case MatchState::FREE_KICK:
      return "free_kick";
    case MatchState::PENALTY:
      return "penalty";
    case MatchState::GOAL:
      return "goal";
    case MatchState::PENALTY_SHOOTOUT:
      return "penalty_shootout";
    case MatchState::HALF_TIME:
      return "half_time";
    case MatchState::FULL_TIME:
      return "full_time";
  }
  return "unknown";
}

inline std::string_view teamPhaseName(TeamPhase phase)
{
  switch (phase)
  {
    case TeamPhase::STOPPAGE:
      return "stoppage";
    case TeamPhase::SET_PIECE:
      return "set_piece";
    case TeamPhase::DEFENSIVE_BLOCK:
      return "defensive_block";
    case TeamPhase::DEFENSIVE_TRANSITION:
      return "defensive_transition";
    case TeamPhase::ATTACKING_TRANSITION:
      return "attacking_transition";
    case TeamPhase::POSSESSION:
      return "possession";
    case TeamPhase::FINAL_THIRD:
      return "final_third";
  }
  return "unknown";
}

inline std::string_view intentName(PlayerIntent intent)
{
  switch (intent)
  {
    case PlayerIntent::HOLD_SHAPE:
      return "hold_shape";
    case PlayerIntent::CARRY_BALL:
      return "carry_ball";
    case PlayerIntent::OFFER_SUPPORT:
      return "offer_support";
    case PlayerIntent::RECEIVE_PASS:
      return "receive_pass";
    case PlayerIntent::RUN_IN_BEHIND:
      return "run_in_behind";
    case PlayerIntent::ATTACK_BOX:
      return "attack_box";
    case PlayerIntent::OVERLAP:
      return "overlap";
    case PlayerIntent::PRESS_BALL:
      return "press_ball";
    case PlayerIntent::COVER_PRESS:
      return "cover_press";
    case PlayerIntent::BLOCK_PASSING_LANE:
      return "block_passing_lane";
    case PlayerIntent::MARK_OPPONENT:
      return "mark_opponent";
    case PlayerIntent::CLAIM_LOOSE_BALL:
      return "claim_loose_ball";
    case PlayerIntent::RECOVER_SHAPE:
      return "recover_shape";
    case PlayerIntent::GOALKEEP:
      return "goalkeep";
  }
  return "unknown";
}

inline std::string_view passIntentName(PassIntent intent)
{
  switch (intent)
  {
    case PassIntent::RECYCLE:
      return "recycle";
    case PassIntent::PROGRESSIVE:
      return "progressive";
    case PassIntent::THROUGH_BALL:
      return "through_ball";
    case PassIntent::CROSS:
      return "cross";
    case PassIntent::CUTBACK:
      return "cutback";
    case PassIntent::SWITCH_PLAY:
      return "switch_play";
    case PassIntent::PRESSURE_RELEASE:
      return "pressure_release";
    case PassIntent::SET_PIECE:
      return "set_piece";
  }
  return "unknown";
}

inline std::string_view scenarioActionName(ScenarioAction action)
{
  switch (action)
  {
    case ScenarioAction::SHOT:
      return "shot";
    case ScenarioAction::PASS:
      return "pass";
    case ScenarioAction::CARRY:
      return "carry";
    case ScenarioAction::SHIELD:
      return "shield";
    case ScenarioAction::CLEAR:
      return "clear";
    case ScenarioAction::NONE:
      return "none";
  }
  return "unknown";
}
}  // namespace MatchEngineInternal

// The engine's own pitch, under the helpers' names: inside a MatchEngine
// member these are what an unqualified call finds.
inline float MatchEngine::distance(PitchPoint first, PitchPoint second) const
{
  return MatchEngineInternal::distance(first, second);
}
inline Vector2F MatchEngine::metricDirection(PitchPoint from, PitchPoint to) const
{
  return MatchEngineInternal::metricDirection(from, to);
}
inline MatchBall MatchEngine::freshBall() const
{
  MatchBall fresh;
  const PitchPoint centre =
      pitch.at(MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE);
  fresh.position = centre;
  fresh.dribbleGoal = centre;
  fresh.possession.passTarget = centre;
  fresh.possession.shotTargetY = centre.y;
  return fresh;
}
inline float MatchEngine::clampToPitchX(float x) const
{
  return MatchEngineInternal::clampToPitchX(pitch, x);
}
inline float MatchEngine::clampToPitchY(float y) const
{
  return MatchEngineInternal::clampToPitchY(pitch, y);
}
inline PitchPoint MatchEngine::clampToPitch(PitchPoint at) const
{
  return MatchEngineInternal::clampToPitch(pitch, at);
}
inline float MatchEngine::clampToBoardsX(float x) const
{
  return MatchEngineInternal::clampToBoardsX(pitch, x);
}
inline float MatchEngine::clampToBoardsY(float y) const
{
  return MatchEngineInternal::clampToBoardsY(pitch, y);
}
inline PitchPoint MatchEngine::clampToBoards(PitchPoint at) const
{
  return MatchEngineInternal::clampToBoards(pitch, at);
}
inline PitchPoint MatchEngine::clampToReach(PitchPoint at) const
{
  return MatchEngineInternal::clampToReach(pitch, at);
}
inline PitchPoint MatchEngine::keepOutOfGoalFrames(PitchPoint from, PitchPoint to) const
{
  return MatchEngineInternal::keepOutOfGoalFrames(pitch, from, to);
}
inline PitchPoint MatchEngine::keepOffGround(PitchPoint from, PitchPoint to) const
{
  return MatchEngineInternal::keepOffGround(pitch, from, to);
}
inline PitchPoint MatchEngine::moveBody(PitchPoint from, PitchPoint to) const
{
  return MatchEngineInternal::moveBody(pitch, from, to);
}
inline bool MatchEngine::deepInTunnel(PitchPoint at) const
{
  return MatchEngineInternal::deepInTunnel(pitch, at);
}
inline Vector2F MatchEngine::routeViaTunnel(Vector2F at, Vector2F target) const
{
  return MatchEngineInternal::routeViaTunnel(pitch, at, target);
}
inline Vector2F MatchEngine::nearestExit(Vector2F at) const
{
  return MatchEngineInternal::nearestExit(pitch, at);
}
inline bool MatchEngine::inPenaltyArea(PitchPoint position, bool defendingHome) const
{
  return MatchEngineInternal::inPenaltyArea(pitch, position, defendingHome);
}
