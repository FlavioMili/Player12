// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Binturong body: how one player's body carries out his brain's choices
// and what it costs him (movement, fitness, kicking, dribbling, first touch,
// goalkeeping, non-contact injuries, and his half of each contest with an
// opponent), the version being developed (see match_body.h). It started as
// an exact copy of the Aardvark body (match_body_aardvark.cpp). Every
// function here is a MatchEngine member with the "Binturong" suffix, declared
// in match_body_binturong_members.h; the engine reaches them through the
// dispatchers in match_body_dispatch.cpp.
//
// Change behaviour freely here. A helper shared with the rest of the engine
// (settling a contest, the ball's flight, rules such as
// MatchRules::tackleMake) is shared by every body: copy it into this body
// (with the Binturong suffix) before changing it. To keep this version for
// comparison, freeze it and copy it to a new version the same way.

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <vector>
#include <tuple>
#include <utility>

#include "model/match_body_shape_binturong.h"
#include "model/match_contact_binturong.h"
#include "model/match_engine.h"
#include "model/match_possession_control.h"
#include "model/match_engine_internal.h"
#include "model/match_recorder.h"
#include "model/match_rules.h"
#include "model/player.h"

using namespace MatchEngineInternal;

namespace
{
// Arriving: a player at full effort slows at this share of his braking, the
// average deceleration of a maximal stop from a sprint (about 4.2-5 m/s^2 in
// team-sport athletes, Harper et al. 2020), against braking peaks of 6-8.
// Lower effort slows more gently, never below the old fixed arrival rate.
constexpr float ARRIVAL_BRAKING_SHARE = 0.7f;
// He stops this close to his target rather than short of it.
constexpr float ARRIVAL_TOLERANCE_METRES = 0.1f;
// A waypoint on his route is reached this close, if he has not moved on
// from it already.
constexpr float WAYPOINT_REACHED_METRES = 0.35f;
// The radius of a turn that is no turn (straight on through a waypoint).
constexpr float STRAIGHT_ON_RADIUS = 1.0e6f;
// Following his line (movement targets plan 3b): he closes an offset from
// it over the near-point time of steering (about 0.5 s ahead: Land & Lee
// 1994, Salvucci & Gray 2004; a driving figure, none found for running).
constexpr float NEAR_POINT_SECONDS = 0.5f;
// Dribbling, he runs his corner to the meet only with the meet ahead of his
// run (within about 60 degrees of it); beside or behind him, straight to it.
constexpr float MEET_AHEAD_COSINE = 0.5f;
// On a route, his run coming round onto his line is capped like a turn once
// it is this far off it (about 5 degrees; less is steering), with at least
// this much room (m) to make it in.
constexpr float HEADING_TURN_MIN_RADIANS = 0.09f;
constexpr float HEADING_ROOM_MIN_METRES = 1.0f;
// Below this speed (m/s) a curve his route has moved on from is not held:
// he simply takes up his new line.
constexpr float HOLD_TURN_MIN_SPEED = 1.0f;
// Holding a curve is timed round it in steps of this length (m), at most
// this many.
constexpr float HOLD_TRACE_METRES = 0.25f;
constexpr int HOLD_TRACE_STEPS = 64;
// His route aim (the point his dribble aim reads) is at least this far on
// (seconds at his speed) along his line.
constexpr float ROUTE_AIM_LEAD_SECONDS = 0.5f;
// He is on the arc of a turn within this much of it (else joining it).
constexpr float ON_ARC_METRES = 0.5f;
// Pivoting: from a standstill he can push off in any direction; on the move
// he speeds up or brakes along his run and turns within a lateral limit.
// The second takes over gradually, fully from this speed (no cliff). A stop
// on his route is made once he is this slow on it: he can turn there.
constexpr float PIVOT_BLEND_SPEED = 1.2f;

Vector2F rotatedBy(Vector2F vector, float angle)
{
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  return {vector.x * c - vector.y * s, vector.x * s + vector.y * c};
}

Vector2F unitOf(Vector2F vector)
{
  const float size = std::sqrt(vector.x * vector.x + vector.y * vector.y);
  return size > 1e-6f ? Vector2F{vector.x / size, vector.y / size}
                      : Vector2F{0.0f, 0.0f};
}

// His turn through a waypoint (movement targets plan 3b), in metres: the
// arc joining the leg in (from `from`) to the leg out (to `to`). Its apex
// passes `apexOffset` inside the waypoint (negative: outside), and the line
// may be pushed out up to `outwardRoom` at the corner to widen it; the turn
// starts no more than half the shorter leg before the corner. Straight on
// is no turn; a sharp reversal (or a leg of no length) is a stop on the
// spot. Where its curve would run the length of a leg's half (no straight
// before the next turn's), it is joined: two arcs meeting at the apex, each
// from half the shorter leg out along its leg (the middle of a slalom's
// alike legs, where the curve before or after it takes over).
struct Turn
{
  bool straight = false;
  /** 0: a stop at `spot`. The tighter arc's, when joined. */
  float radius = 0.0f;
  /** The arc's centre (joined: the arc in's). */
  Vector2F centre{};
  /** +1 turning left (anticlockwise), -1 right. */
  float sense = 1.0f;
  Vector2F spot{};
  /** How far before the waypoint it starts (about). */
  float lead = 0.0f;
  /** Joined: the arc in (from the middle of the leg in to the apex) and the
   * arc out (from the apex to the middle of the leg out), the apex and the
   * way through it, and where the curve starts and ends. */
  bool joined = false;
  float radiusIn = 0.0f;
  float radiusOut = 0.0f;
  Vector2F centreOut{};
  Vector2F apex{};
  Vector2F way{};
  Vector2F entry{};
  Vector2F end{};
};

Turn turnThrough(Vector2F from, Vector2F waypoint, Vector2F to,
                 float apexOffset, float outwardRoom)
{
  Turn turn;
  turn.spot = waypoint;
  const Vector2F in{waypoint.x - from.x, waypoint.y - from.y};
  const Vector2F out{to.x - waypoint.x, to.y - waypoint.y};
  const float inLength = std::sqrt(in.x * in.x + in.y * in.y);
  const float outLength = std::sqrt(out.x * out.x + out.y * out.y);
  if (inLength < 1e-3f || outLength < 1e-3f) return turn;
  const Vector2F inward = unitOf(Vector2F{out.x / outLength - in.x / inLength,
                                          out.y / outLength - in.y / inLength});
  const float cosine = std::clamp(
      (in.x * out.x + in.y * out.y) / (inLength * outLength), -1.0f, 1.0f);
  // Half the heading change, phi / 2.
  const float halfCos = std::sqrt((1.0f + cosine) * 0.5f);
  const float halfSin = std::sqrt((1.0f - cosine) * 0.5f);
  if (halfSin < 1e-3f)
  {
    turn.straight = true;
    turn.radius = STRAIGHT_ON_RADIUS;
    return turn;
  }
  // A stop on a spot outside the waypoint passes outside it. A sharp
  // reversal is a stop: he plants and turns rather than curving round.
  turn.spot = {waypoint.x + inward.x * std::min(apexOffset, 0.0f),
               waypoint.y + inward.y * std::min(apexOffset, 0.0f)};
  const float reversalDegrees = MatchTuning::Player::SHARP_REVERSAL_DEGREES;
  if (halfCos < 1e-3f ||
      cosine < std::cos(reversalDegrees * std::numbers::pi_v<float> / 180.0f))
    return turn;
  const float halfTan = halfSin / halfCos;
  // The apex of an arc of radius r lies r (1 / cos(phi/2) - 1) inside the
  // corner its legs meet at.
  const float bulge = 1.0f / halfCos - 1.0f;
  const float radius =
      std::min((outwardRoom + apexOffset) / bulge,
               0.5f * std::min(inLength, outLength) / halfTan);
  if (radius < 0.05f) return turn;
  // The corner moves out as far as it must to put the apex where asked.
  const float shift = std::max(0.0f, radius * bulge - apexOffset);
  turn.sense = in.x * out.y - in.y * out.x >= 0.0f ? 1.0f : -1.0f;
  // Its curve reaching halfway along a leg (no room for a straight before
  // the next turn's), and moved out for its apex: moved out, it would cross
  // the next turn's curve rather than meet it. Joined instead: an arc in
  // from the middle of the leg in to the apex, the way between the legs
  // there, and an arc out from it to the middle of the leg out, where the
  // next turn's arc in takes over (with the same way, a slalom's next turn
  // being the mirror of this one).
  const bool halfLegs =
      0.5f * std::min(inLength, outLength) / halfTan <= (outwardRoom + apexOffset) / bulge;
  if (halfLegs && shift > 0.0f)
  {
    const Vector2F apex{waypoint.x + inward.x * apexOffset,
                        waypoint.y + inward.y * apexOffset};
    const Vector2F way =
        unitOf({in.x / inLength + out.x / outLength, in.y / inLength + out.y / outLength});
    // The radius of the arc through the apex (along `way` there) and `middle`.
    const auto arcTo = [&](Vector2F middle)
    {
      const Vector2F offset{middle.x - apex.x, middle.y - apex.y};
      const float across = offset.x * inward.x + offset.y * inward.y;
      const float along = offset.x * way.x + offset.y * way.y;
      return across > 1e-3f ? (along * along + across * across) / (2.0f * across) : 0.0f;
    };
    // Both ends half the shorter leg from the corner: the curve is only
    // squeezed by the shorter one (a slalom's legs are alike), and a long
    // leg is left straight, not curved down to its middle (from a start
    // just short of a corner, that curve swung far wide of the line).
    const float reach = 0.5f * std::min(inLength, outLength);
    const Vector2F entry{waypoint.x - in.x / inLength * reach,
                         waypoint.y - in.y / inLength * reach};
    const Vector2F end{waypoint.x + out.x / outLength * reach,
                       waypoint.y + out.y / outLength * reach};
    const float radiusIn = arcTo(entry);
    const float radiusOut = arcTo(end);
    if (radiusIn >= 0.05f && radiusOut >= 0.05f)
    {
      turn.joined = true;
      turn.radiusIn = radiusIn;
      turn.radiusOut = radiusOut;
      turn.radius = std::min(radiusIn, radiusOut);
      turn.centre = {apex.x + inward.x * radiusIn, apex.y + inward.y * radiusIn};
      turn.centreOut = {apex.x + inward.x * radiusOut, apex.y + inward.y * radiusOut};
      turn.apex = apex;
      turn.way = way;
      turn.entry = entry;
      turn.end = end;
      turn.lead = reach;
      return turn;
    }
  }
  turn.radius = radius;
  turn.centre = {waypoint.x + inward.x * (radius / halfCos - shift),
                 waypoint.y + inward.y * (radius / halfCos - shift)};
  turn.lead = radius * halfTan;
  return turn;
}

// The way along an arc (centre, sense) at `point` on it.
Vector2F arcHeading(Vector2F centre, float sense, Vector2F point)
{
  const Vector2F radial = unitOf({point.x - centre.x, point.y - centre.y});
  return {-radial.y * sense, radial.x * sense};
}

// Where his line through the turn points from `position` (metres), going
// on to `next`, and how far it is to the start of the turn: off the arc,
// the point where he joins it; on it, along it (aiming where it will be
// after `ahead` metres), closing any offset over the near-point time at
// `speed`. Also whether he has reached the arc's exit line to `next` (or
// come within WAYPOINT_REACHED_METRES of the waypoint at or past the apex,
// when `reachCounts`).
struct LineAim
{
  Vector2F direction{};
  float toTurn = 0.0f;
  bool exited = false;
  /** On a curve: moved on only by coming that close to the waypoint, short
   * of where the curve ends. */
  bool reachedEarly = false;
  /** On a curve: where it ends, and the way on from there. */
  Vector2F exit{};
  Vector2F exitHeading{};
};

LineAim aimThrough(const Turn& turn, Vector2F waypoint, Vector2F position,
                   Vector2F next, float speed, float ahead, bool reachCounts = true)
{
  LineAim aim;
  const Vector2F toWaypoint{waypoint.x - position.x, waypoint.y - position.y};
  const float waypointMetres =
      std::sqrt(toWaypoint.x * toWaypoint.x + toWaypoint.y * toWaypoint.y);
  if (turn.straight)
  {
    // Straight on: through it, and on once past it.
    aim.direction = unitOf(toWaypoint);
    aim.toTurn = waypointMetres;
    const Vector2F onward = unitOf({next.x - waypoint.x, next.y - waypoint.y});
    aim.exited = (reachCounts && waypointMetres <= WAYPOINT_REACHED_METRES) ||
                 -toWaypoint.x * onward.x - toWaypoint.y * onward.y >= 0.0f;
    return aim;
  }
  if (turn.radius <= 0.0f)
  {
    const Vector2F toSpot{turn.spot.x - position.x, turn.spot.y - position.y};
    aim.direction = unitOf(toSpot);
    aim.toTurn = std::sqrt(toSpot.x * toSpot.x + toSpot.y * toSpot.y);
    // Made once he is on it (within reach of it: with the ball he stops
    // where he stops it) and slow enough to turn there.
    aim.exited = aim.toTurn <= MatchTuning::Dribble::STOP_TOUCH_METRES &&
                 speed <= PIVOT_BLEND_SPEED;
    return aim;
  }
  // Along an arc (centre, radius): joined to it from off it, or along it
  // and back onto it.
  const auto follow = [&](Vector2F centre, float r)
  {
    const Vector2F fromCentre{position.x - centre.x, position.y - centre.y};
    const float d =
        std::sqrt(fromCentre.x * fromCentre.x + fromCentre.y * fromCentre.y);
    const Vector2F radial = unitOf(fromCentre);
    if (d > r)
    {
      const Vector2F t =
          rotatedBy(radial, turn.sense * std::acos(std::min(r / d, 1.0f)));
      const Vector2F join{centre.x + t.x * r, centre.y + t.y * r};
      const Vector2F toJoin{join.x - position.x, join.y - position.y};
      aim.toTurn = std::sqrt(toJoin.x * toJoin.x + toJoin.y * toJoin.y);
      if (aim.toTurn > 1e-3f)
      {
        aim.direction = {toJoin.x / aim.toTurn, toJoin.y / aim.toTurn};
        return;
      }
    }
    const Vector2F along = rotatedBy(
        Vector2F{-radial.y * turn.sense, radial.x * turn.sense},
        turn.sense * ahead / r);
    const float correction =
        (d - r) / (NEAR_POINT_SECONDS * std::max(speed, 1.0f));
    aim.direction = unitOf(
        {along.x - radial.x * correction, along.y - radial.y * correction});
    aim.toTurn = 0.0f;
  };
  if (turn.joined)
  {
    // The arc in up to the apex, the arc out past it; done at the middle of
    // the leg out (where the next turn's curve takes over), not on reaching
    // the waypoint.
    const bool pastApex = (position.x - turn.apex.x) * turn.way.x +
                              (position.y - turn.apex.y) * turn.way.y >=
                          0.0f;
    aim.exit = turn.end;
    aim.exitHeading = arcHeading(turn.centreOut, turn.sense, turn.end);
    aim.exited = pastApex && (position.x - turn.end.x) * aim.exitHeading.x +
                                     (position.y - turn.end.y) * aim.exitHeading.y >=
                                 0.0f;
    if (pastApex)
      follow(turn.centreOut, turn.radiusOut);
    else
      follow(turn.centre, turn.radiusIn);
    return aim;
  }
  const float r = turn.radius;
  const Vector2F fromCentre{position.x - turn.centre.x,
                            position.y - turn.centre.y};
  const float d =
      std::sqrt(fromCentre.x * fromCentre.x + fromCentre.y * fromCentre.y);
  const Vector2F radial = unitOf(fromCentre);
  // The exit: where the line on to `next` leaves the arc.
  const Vector2F nextFromCentre{next.x - turn.centre.x,
                                next.y - turn.centre.y};
  const float nextDistance = std::sqrt(nextFromCentre.x * nextFromCentre.x +
                                       nextFromCentre.y * nextFromCentre.y);
  Vector2F exit;
  Vector2F exitHeading;
  if (nextDistance > r)
  {
    const Vector2F e = rotatedBy(unitOf(nextFromCentre),
                                 -turn.sense * std::acos(r / nextDistance));
    exit = {turn.centre.x + e.x * r, turn.centre.y + e.y * r};
    exitHeading = unitOf({next.x - exit.x, next.y - exit.y});
  }
  else
  {
    const Vector2F e = unitOf(nextFromCentre);
    exit = {turn.centre.x + e.x * r, turn.centre.y + e.y * r};
    exitHeading = {-e.y * turn.sense, e.x * turn.sense};
  }
  // He has reached it when round on the exit's side of the turn (not still
  // where he joined a sharp one) and at or past the exit line, on the arc or
  // swung wide of it (else, wide, he would be sent round to join it again).
  const Vector2F exitRadial = unitOf({exit.x - turn.centre.x,
                                      exit.y - turn.centre.y});
  const bool pastExit =
      d <= r * 1.5f + ON_ARC_METRES &&
      radial.x * exitRadial.x + radial.y * exitRadial.y > 0.0f &&
      (position.x - exit.x) * exitHeading.x + (position.y - exit.y) * exitHeading.y >=
          0.0f;
  // Close to the waypoint counts only once he is level with the apex or past
  // it (round from the centre): on a tight, slow curve he is that close well
  // before it, with most of the turn still to make.
  const Vector2F apexRadial = unitOf({waypoint.x - turn.centre.x,
                                      waypoint.y - turn.centre.y});
  const bool pastApex =
      turn.sense * (apexRadial.x * radial.y - apexRadial.y * radial.x) >= 0.0f &&
      apexRadial.x * radial.x + apexRadial.y * radial.y > 0.0f;
  const bool reached = waypointMetres <= WAYPOINT_REACHED_METRES && pastApex;
  aim.exited = (reachCounts && reached) || pastExit;
  aim.reachedEarly = reached && !pastExit;
  aim.exit = exit;
  aim.exitHeading = exitHeading;
  // Off the arc, to the point where he joins it; on it, along it, where it
  // will be after this step, and back onto it.
  follow(turn.centre, r);
  return aim;
}

// How far `point` is off his line through the turn (metres): the leg in
// from `from` to the arc, round it, and the leg out from it to `next`.
float metresOffLine(const Turn& turn, Vector2F from, Vector2F waypoint,
                    Vector2F next, Vector2F point)
{
  const auto toSegment = [&](Vector2F a, Vector2F b)
  {
    const Vector2F ab{b.x - a.x, b.y - a.y};
    const float squared = ab.x * ab.x + ab.y * ab.y;
    const float share =
        squared > 1e-6f
            ? std::clamp(((point.x - a.x) * ab.x + (point.y - a.y) * ab.y) /
                             squared,
                         0.0f, 1.0f)
            : 0.0f;
    return std::hypot(point.x - a.x - ab.x * share,
                      point.y - a.y - ab.y * share);
  };
  if (turn.straight)
    return std::min(toSegment(from, waypoint), toSegment(waypoint, next));
  if (turn.radius <= 0.0f)
    return std::min(toSegment(from, turn.spot), toSegment(turn.spot, next));
  if (turn.joined)
  {
    // Off an arc (centre, radius) from `start` to `stop`, if beside it.
    const auto offArc = [&](Vector2F centre, float r, Vector2F start, Vector2F stop)
    {
      const Vector2F a{start.x - centre.x, start.y - centre.y};
      const Vector2F b{stop.x - centre.x, stop.y - centre.y};
      const Vector2F p{point.x - centre.x, point.y - centre.y};
      const auto round = [&](Vector2F v)
      {
        const float angle =
            turn.sense * std::atan2(a.x * v.y - a.y * v.x, a.x * v.x + a.y * v.y);
        return angle < 0.0f ? angle + 2.0f * std::numbers::pi_v<float> : angle;
      };
      return round(p) <= round(b) ? std::abs(std::hypot(p.x, p.y) - r)
                                  : std::numeric_limits<float>::max();
    };
    return std::min({toSegment(from, turn.entry),
                     offArc(turn.centre, turn.radiusIn, turn.entry, turn.apex),
                     offArc(turn.centreOut, turn.radiusOut, turn.apex, turn.end),
                     toSegment(turn.end, next)});
  }
  const float r = turn.radius;
  // Where a leg from `outside` meets the arc (as aimThrough() joins and
  // leaves it).
  const auto touching = [&](Vector2F outside, float sense)
  {
    const Vector2F fromCentre{outside.x - turn.centre.x,
                              outside.y - turn.centre.y};
    const float d = std::hypot(fromCentre.x, fromCentre.y);
    const Vector2F radial = unitOf(fromCentre);
    const Vector2F t =
        d > r ? rotatedBy(radial, sense * std::acos(r / d)) : radial;
    return Vector2F{turn.centre.x + t.x * r, turn.centre.y + t.y * r};
  };
  const Vector2F entry = touching(from, turn.sense);
  const Vector2F exit = touching(next, -turn.sense);
  // How far round the arc from its entry, the way it turns.
  const auto around = [&](Vector2F at)
  {
    const Vector2F e{entry.x - turn.centre.x, entry.y - turn.centre.y};
    const Vector2F a{at.x - turn.centre.x, at.y - turn.centre.y};
    const float angle =
        turn.sense * std::atan2(e.x * a.y - e.y * a.x, e.x * a.x + e.y * a.y);
    return angle < 0.0f ? angle + 2.0f * std::numbers::pi_v<float> : angle;
  };
  const float onArc =
      around(point) <= around(exit)
          ? std::abs(std::hypot(point.x - turn.centre.x,
                                point.y - turn.centre.y) -
                     r)
          : std::numeric_limits<float>::max();
  return std::min({toSegment(from, entry), onArc, toSegment(exit, next)});
}

// Fatigue tiers (fatigue plan 4.5): each effect is a loss from the fresh
// value as a function of fuel left, in straight lines between these points:
// none from full down to 60%; the match research's at 30% (sprint ~3%
// slower, repeat efforts down through a smaller, slower-refilling burst
// tank: Krustrup 2006, Chorley 2022); the 2-3 h research's at 10% (critical
// speed -11 to -15%, W' about -20% and more: Clark 2019, Norte 2026); and at
// empty, critical speed and top speed down to his walk-run transition, so
// he can only walk. Named ranges: Fresh 100-60%, Tiring 60-30%, Tired
// 30-10%, Exhausted 10-0%, Spent at 0. The points 60/30/10 and the Spent
// values are design choices.
constexpr std::array<float, 4> TIER_FUEL{0.6f, 0.3f, 0.1f, 0.0f};
// Critical speed and top speed: below 10% they head for the walk-run
// transition (tierSpeed), so their last entries only hold the line.
constexpr std::array<float, 4> CRITICAL_SPEED_LOSS{0.0f, 0.05f, 0.15f, 0.15f};
constexpr std::array<float, 4> TOP_SPEED_LOSS{0.0f, 0.03f, 0.10f, 0.10f};
constexpr std::array<float, 4> BURST_CAPACITY_LOSS{0.0f, 0.10f, 0.30f, 1.0f};
// The burst tank's slow refill only (Chorley 2022: repeated bouts slow the
// slow phase; the fast one keeps its pace).
constexpr std::array<float, 4> SLOW_REFILL_SLOWING{0.0f, 0.30f, 0.80f, 0.80f};
// Acceleration and braking; turning takes the same for now (a stand-in
// until turning is reviewed).
constexpr std::array<float, 4> MOVEMENT_LOSS{0.0f, 0.02f, 0.10f, 0.30f};
// Technique error's shape (+10%, +30%, +60% at 30%, 10%, empty), scaled so
// SPENT_TECHNIQUE_ERROR (and the touch knob Fatigue error gain) is its size
// at empty (a stand-in until fatigue's effect on technique is reviewed).
constexpr std::array<float, 4> TECHNIQUE_SHAPE{0.0f, 1.0f / 6.0f, 0.5f, 1.0f};

/** A tier column's loss at `fuel` left (0-1). */
inline float tierLoss(const std::array<float, 4>& losses, float fuel)
{
  const float left = std::clamp(fuel, 0.0f, 1.0f);
  if (left >= TIER_FUEL[0]) return losses[0];
  for (std::size_t point = 1; point < TIER_FUEL.size(); ++point)
    if (left >= TIER_FUEL[point])
      return losses[point - 1] +
             (losses[point] - losses[point - 1]) *
                 (TIER_FUEL[point - 1] - left) /
                 (TIER_FUEL[point - 1] - TIER_FUEL[point]);
  return losses.back();
}

/** A speed (critical or top) at `fuel` left: its tier losses down to 10%,
 * then on down to his walk-run transition at empty. */
inline float tierSpeed(float fresh, const std::array<float, 4>& losses,
                       float fuel, float transition)
{
  const float left = std::clamp(fuel, 0.0f, 1.0f);
  if (left >= TIER_FUEL[2]) return fresh * (1.0f - tierLoss(losses, left));
  const float exhausted = fresh * (1.0f - losses[2]);
  const float spent = std::min(transition, exhausted);
  return exhausted + (spent - exhausted) * (TIER_FUEL[2] - left) / TIER_FUEL[2];
}

/** The Agility or Acceleration rating his body moves by: `freeShare` of
 * his own whatever his Dribbling, and of the rest only the share his
 * Dribbling unlocks (1 without the ball: all of it; with it, as his pace:
 * freeShareBinturong). Every Agility and Acceleration read below goes
 * through here, so his body and his dribble's plan move him alike. */
inline float movementRating(const MatchPlayer& player, DerivedStat stat,
                            float freeShare)
{
  const float free = std::clamp(freeShare, 0.0f, 1.0f);
  return player.derivedStat(stat) *
         (free + (1.0f - free) * std::clamp(player.dribbling, 0.0f, 1.0f));
}

/** His grip: the Agility stat's multiplier on the lateral limit every
 * player has (0.9 for a part-timer, 1.1 for the elite). One value for both
 * feet: the body does not know which foot is down, so the outside foot's
 * stronger cut and the inside foot's weaker one average out. */
inline float playerGrip(const MatchPlayer& player, float freeShare)
{
  return derivedPhysicalValue(DerivedStat::AGILITY,
                              movementRating(player, DerivedStat::AGILITY, freeShare));
}

/** How fast his body turns (rad/s): his Agility rating, from the
 * part-timer to the elite anchor, across the range (orientation plan). */
inline float bodyTurnRate(const MatchPlayer& player, float freeShare)
{
  using P = MatchTuning::Player;
  const DerivedStatInfo& info = derivedStatInfo(DerivedStat::AGILITY);
  const float share = std::clamp(
      (movementRating(player, DerivedStat::AGILITY, freeShare) - info.lowRating) /
          (info.highRating - info.lowRating),
      0.0f, 1.0f);
  return P::BODY_TURN_RATE_LOW +
         (P::BODY_TURN_RATE_HIGH - P::BODY_TURN_RATE_LOW) * share;
}

/** Where his Agility or Acceleration rating (movementRating) sits from 1
 * (0) to 99 (1). */
inline float statShare(const MatchPlayer& player, DerivedStat stat, float freeShare)
{
  return std::clamp((movementRating(player, stat, freeShare) - STAT_1_RATING) /
                        (STAT_99_RATING - STAT_1_RATING),
                    0.0f, 1.0f);
}

/** His fresh maximum acceleration A0 (m/s^2): the Acceleration stat, plus
 * Agility's boost (a share of the stat's range, the same for everyone at
 * the same Agility). */
inline float playerAcceleration(const MatchPlayer& player, float freeShare)
{
  const DerivedStatInfo& info = derivedStatInfo(DerivedStat::ACCELERATION);
  return derivedPhysicalValue(
             DerivedStat::ACCELERATION,
             movementRating(player, DerivedStat::ACCELERATION, freeShare)) +
         MatchTuning::Player::AGILITY_ACCELERATION_BOOST *
             (info.highValue - info.lowValue) *
             statShare(player, DerivedStat::AGILITY, freeShare);
}

/** His fresh hardest braking (m/s^2): Agility, plus Acceleration's boost
 * (a share of the braking range). The handshake's braking (from
 * Physicality) is the Aardvark body's. */
inline float playerBraking(const MatchPlayer& player, float freeShare)
{
  using P = MatchTuning::Player;
  const float range = P::BRAKING_AT_AGILITY_99 - P::BRAKING_AT_AGILITY_1;
  return P::BRAKING_AT_AGILITY_1 +
         range * statShare(player, DerivedStat::AGILITY, freeShare) +
         P::ACCELERATION_BRAKING_BOOST * range *
             statShare(player, DerivedStat::ACCELERATION, freeShare);
}

// Energy: two tanks (critical speed and D', the W' balance model of Skiba et
// al. for intermittent running). The fatigue plan
// (match_engine_fatigue_plan.md) is the reference for every value here.
// - Fuel (`stamina`, 1 full to 0 empty): the whole match's work, never
//   refilled in play. It drains with the carbohydrate share of the metabolic
//   power of his movement (below), sized by his fuel capacity: running at
//   FUEL_ANCHOR_SHARE of his critical speed empties a full tank in exactly
//   his fuel capacity. What fuel used costs him is the fatigue tiers'
//   (above).
constexpr float FUEL_ANCHOR_SHARE = 0.9f;
// Metabolic power: accelerating on the flat costs what running up the
// equivalent slope a / g does (di Prampero 2005, Osgnach 2010), plus air
// resistance K v^2 / g; running cost per metre from Minetti & Pavei 2018
// (as in the floodlight library), walking below his walk-run transition
// from the flat walking polynomial. Only the shape matters: each player is
// measured against his own anchor, so the constants' scale and his mass
// cancel.
constexpr float GRAVITY = 9.81f;
constexpr float AIR_COST_K = 0.0037f;
// The load period's acceleration is limited to what a body can do (a guard
// against a jolt from body contact).
constexpr float MAX_LOAD_ACCELERATION = 10.0f;
// Energy drawn from the burst reserve costs extra fuel: glycolysis from
// glycogen nets 3 ATP per glucose unit against ~31-33 oxidised (~10x the
// glycogen per unit of energy), but only part of a burst is glycolytic:
// about half of a fresh 6 s sprint is phosphocreatine, which uses none
// (Gaitanos 1993), and glycolysis's share falls over repeated efforts
// (Girard 2011). 0.4 glycolytic x 10: 4x, the single-sprint value (a match
// of repeated efforts is likely lower).
constexpr float ANAEROBIC_FUEL_PREMIUM = 4.0f;

/** Running cost (J/kg/m) up the equivalent slope `slope` (Minetti & Pavei
 * 2018): 3.6 on the flat. */
inline float runningCost(float slope)
{
  const float cost = slope >= 0.0f
                         ? 39.5f * slope + 3.6f * std::exp(-4.0f * slope)
                         : -8.34f * slope + 3.6f * std::exp(13.0f * slope);
  return cost * std::sqrt(slope * slope + 1.0f);
}

/** Walking cost on the flat (J/kg/m) at `speed` (m/s): about 2.2 at 1.35. */
inline float walkingCost(float speed)
{
  return (((1.25f * speed - 6.57f) * speed + 13.14f) * speed - 11.15f) * speed +
         5.35f;
}

/** Metabolic power (W/kg) of moving at `speed` while changing speed by
 * `acceleration`. Below his walk-run transition he walks, with the same
 * surcharge (or saving) for changing speed as a runner. */
inline float metabolicPower(float speed, float acceleration, float transition)
{
  const float flat = AIR_COST_K * speed * speed / GRAVITY;
  const float slope = acceleration / GRAVITY + flat;
  const float cost =
      speed < transition
          ? std::max(0.0f, walkingCost(speed) + runningCost(slope) -
                               runningCost(flat))
          : runningCost(slope);
  return cost * speed;
}

/** The carbohydrate share of aerobic power at `intensity` (power over the
 * power at his critical speed): the crossover (Romijn 1993/2000, review
 * 2023), the same for everyone; piecewise linear. */
inline float carbohydrateShare(float intensity)
{
  constexpr std::array<std::array<float, 2>, 6> CROSSOVER{{{0.0f, 0.10f},
                                                           {0.30f, 0.15f},
                                                           {0.75f, 0.50f},
                                                           {0.82f, 0.60f},
                                                           {0.90f, 0.70f},
                                                           {1.00f, 0.85f}}};
  if (intensity >= CROSSOVER.back()[0]) return CROSSOVER.back()[1];
  for (std::size_t point = 1; point < CROSSOVER.size(); ++point)
    if (intensity < CROSSOVER[point][0])
    {
      const auto& [x0, y0] = CROSSOVER[point - 1];
      const auto& [x1, y1] = CROSSOVER[point];
      return y0 + (y1 - y0) * (std::max(intensity, 0.0f) - x0) / (x1 - x0);
    }
  return CROSSOVER.back()[1];
}

/** The equivalent slope at which running costs least per metre (a gentle
 * downhill, about -13%: the bottom of the running cost's U), found once. */
inline float cheapestSlope()
{
  static const float slope = []
  {
    float low = -0.5f;
    float high = 0.0f;
    for (int iteration = 0; iteration < 40; ++iteration)
    {
      const float left = low + (high - low) / 3.0f;
      const float right = high - (high - low) / 3.0f;
      if (runningCost(left) < runningCost(right))
        high = right;
      else
        low = left;
    }
    return 0.5f * (low + high);
  }();
  return slope;
}

/** The slope in [low, high] where the running cost, monotonic there, meets
 * `target` (an end if it never does). */
inline float slopeAtCost(float target, float low, float high, bool rising)
{
  for (int iteration = 0; iteration < 16; ++iteration)
  {
    const float middle = 0.5f * (low + high);
    // Below the target: on a rising stretch the answer is further up the
    // slope, on a falling one further down.
    if ((runningCost(middle) < target) == rising)
      low = middle;
    else
      high = middle;
  }
  return 0.5f * (low + high);
}

/** The speed changes (m/s^2) he can afford at `speed` with his metabolic
 * power held to `powerCap`. */
struct AccelerationRange
{
  /** The hardest braking (negative). */
  float low = -MAX_LOAD_ACCELERATION;
  /** The hardest speeding up; negative when even holding this speed costs
   * more than the cap, so he slows whatever he wants. */
  float high = MAX_LOAD_ACCELERATION;
};

/** Running cost is U-shaped in the slope, so at a power cap there is a
 * range: braking harder or speeding up harder than its ends costs more.
 * When even the cheapest change of speed costs more than the cap he slows
 * at that cheapest rate (coasting). */
inline AccelerationRange accelerationsAtPower(float speed, float powerCap,
                                              float transition)
{
  // Getting going from a standstill costs next to nothing per second.
  constexpr float STANDING_SPEED = 0.2f;
  if (speed < STANDING_SPEED) return {};
  const float flat = AIR_COST_K * speed * speed / GRAVITY;
  // The running cost per metre the cap allows (a walker pays the same
  // surcharge on top of his walking cost).
  float target = powerCap / speed;
  if (speed < transition) target += runningCost(flat) - walkingCost(speed);
  const float cheapest = cheapestSlope();
  const auto toAcceleration = [flat](float slope)
  {
    return std::clamp((slope - flat) * GRAVITY, -MAX_LOAD_ACCELERATION,
                      MAX_LOAD_ACCELERATION);
  };
  if (target <= runningCost(cheapest))
  {
    const float coasting = toAcceleration(cheapest);
    return {coasting, coasting};
  }
  constexpr float STEEPEST = 1.5f;
  return {toAcceleration(slopeAtCost(target, -STEEPEST, cheapest, false)),
          toAcceleration(slopeAtCost(target, cheapest, STEEPEST, true))};
}

// An empty burst reserve is a ceiling on power: no more than his critical
// speed's, both to speed up (or hold a speed above his critical speed) and
// to brake. Through the last share of the reserve the limit closes in (a
// share of the fresh capacity; a design choice, so the limit does not
// switch on and off).
constexpr float BURST_LIMIT_BAND = 0.05f;

/** His speed-change limits (m/s^2) after the burst reserve: his full drive
 * and braking while there is reserve to spend, closing to what his
 * critical speed's power affords as it runs out. */
inline AccelerationRange burstLimits(float drive, float brake, float speed,
                                     float fullness, float critical,
                                     float transition)
{
  const AccelerationRange affordable = accelerationsAtPower(
      speed, metabolicPower(critical, 0.0f, transition), transition);
  const float open = std::clamp(fullness / BURST_LIMIT_BAND, 0.0f, 1.0f);
  const float high = std::min(drive, affordable.high);
  const float low = std::max(-brake, affordable.low);
  return {low + (-brake - low) * open, high + (drive - high) * open};
}

/** Carbohydrate power (W/kg, glycogen-equivalent) of `power` for a player
 * whose critical speed takes `criticalPower`: the crossover share below it;
 * above it, the aerobic part at its share and the rest anaerobic, at the
 * premium. */
inline float carbohydratePower(float power, float criticalPower)
{
  const float critical = std::max(criticalPower, EPSILON);
  if (power <= critical) return power * carbohydrateShare(power / critical);
  return critical * carbohydrateShare(1.0f) +
         (power - critical) * ANAEROBIC_FUEL_PREMIUM;
}

// - Burst reserve (`sprintReserve`, a share of the fresh burst capacity):
//   D' as energy, the capacity's metres at his critical speed's running
//   cost. Metabolic power above his critical speed's drains it (running
//   faster, and accelerating: on the flat a run of metres above critical
//   speed drains about one metre each); below it, it refills toward full
//   with the recovery rate's time constant, slower the harder he still
//   works (x 2.8 at critical power: about 120 s after light and 340 s after
//   heavy recovery in intermittent trials). What it supplies costs fuel at
//   the anaerobic premium. The refill has two parallel phases (Chorley
//   2022: a fast one, tau ~22 s, about 48% of what was spent; the rest
//   slow): this share of every spend comes back on the fast track, with
//   the Fast recovery stat's time constant (40%, a design choice within
//   the research's 48 +/- 18%).
constexpr float RECOVERY_EFFORT_SLOWING = 1.8f;
constexpr float FAST_REFILL_SHARE = 0.4f;
/** The top speed his legs allow now: the fatigue tiers trim it (to his
 * walk-run transition when spent), an injury cuts it. The burst reserve is
 * not a speed limit: it limits power (burstLimits), so with it empty he
 * cannot hold a speed above his critical speed and slows from it
 * naturally. */
inline float fatiguedTopSpeed(float maxSpeed, float fuel, float transition,
                              float injury)
{
  return tierSpeed(maxSpeed, TOP_SPEED_LOSS, fuel, transition) * injury;
}

/** His critical speed now: the fatigue tiers' (his walk-run transition when
 * spent). */
inline float tieredCriticalSpeed(float fresh, float fuel, float transition)
{
  return tierSpeed(fresh, CRITICAL_SPEED_LOSS, fuel, transition);
}

/** The share of his fresh burst capacity his fuel leaves (none when
 * spent). */
inline float tieredBurstCapacity(float fuel)
{
  return 1.0f - tierLoss(BURST_CAPACITY_LOSS, fuel);
}

/** The share of his fresh acceleration, braking and turning his fuel
 * leaves. */
inline float tieredMovement(float fuel)
{
  return 1.0f - tierLoss(MOVEMENT_LOSS, fuel);
}

/** One turn of his line (metres): the leg in from `from`, its waypoint, the
 * way on to `next`. `held`: a curve he is finishing after his route moved
 * on past its waypoint (done where the curve ends, not on reaching the
 * waypoint). */
struct LineLeg
{
  Vector2F from{};
  Vector2F waypoint{};
  Vector2F next{};
  bool held = false;
};

/** His line's turns from now, in order: a curve he is holding, then the one
 * at his waypoint, then the one at the waypoint after (on into `end`,
 * metres). Empty off a route. */
std::vector<LineLeg> lineLegsOf(const MatchPlayer& player, Vector2F end)
{
  std::vector<LineLeg> legs;
  if (player.routeWaypoints <= 0) return legs;
  if (player.holdingTurn)
    legs.push_back({asVector(player.heldLegStart), asVector(player.heldWaypoint),
                    asVector(player.heldNext), true});
  legs.push_back({asVector(player.routeLegStart), asVector(player.movementTarget),
                  asVector(player.nextTarget), false});
  if (player.routeWaypoints > 1)
    legs.push_back({asVector(player.movementTarget), asVector(player.nextTarget), end,
                    false});
  return legs;
}

/** The fastest he can run a curve of `radius` (m): where his lateral limit
 * holds him on it, as the movement kernel has it (with the ball, by the
 * grip his Dribbling unlocks: movementRating). */
float cornerSpeedOf(const MatchPlayer& player, float radius, float freeShare)
{
  using P = MatchTuning::Player;
  const float grip = playerGrip(player, freeShare) * tieredMovement(player.stamina);
  const float b = (P::LATERAL_ACCELERATION_SLOW - P::LATERAL_ACCELERATION_FAST) *
                  grip * radius / std::max(player.maxSpeed, EPSILON);
  const float c = P::LATERAL_ACCELERATION_SLOW * grip * radius;
  return 0.5f * (std::sqrt(b * b + 4.0f * c) - b);
}

/** Seconds from `from` (metres), running along `heading` at `speed`, onto
 * his line through `leg` and on to its waypoint: held, while he comes round
 * onto it, to what the movement kernel's joining cap allows (that angle
 * turned within the room before the line's turn, at least
 * HEADING_ROOM_MIN_METRES), at `speed` otherwise. */
float joinSecondsOf(const MatchPlayer& player, Vector2F from, Vector2F heading,
                    float speed, const LineLeg& leg, float freeShare)
{
  const Turn turn = turnThrough(leg.from, leg.waypoint, leg.next,
                                player.routeApexOffset, player.routeOutwardRoom);
  const LineAim aim =
      aimThrough(turn, leg.waypoint, from, leg.next, speed, 0.0f, false);
  const Vector2F join{from.x + aim.direction.x * aim.toTurn,
                      from.y + aim.direction.y * aim.toTurn};
  const float metres =
      aim.toTurn + length({leg.waypoint.x - join.x, leg.waypoint.y - join.y});
  const float off = std::acos(std::clamp(
      heading.x * aim.direction.x + heading.y * aim.direction.y, -1.0f, 1.0f));
  if (off <= HEADING_TURN_MIN_RADIANS) return metres / speed;
  const float room = std::max(aim.toTurn, HEADING_ROOM_MIN_METRES);
  const float joining = std::min(speed, cornerSpeedOf(player, room / off, freeShare));
  const float slowMetres = std::min(metres, room);
  return slowMetres / joining + (metres - slowMetres) / speed;
}

/** His route has moved on to `waypoint` (then `next`, metres) while he is
 * on the curve `held`: where to hand over (metres) if holding it until his
 * new line (planned from the held waypoint) points the way the curve has
 * him running (within HEADING_TURN_MIN_RADIANS), but no further than where
 * that line's turn starts (so he takes it whole) or the curve ends, then
 * taking up that line, gets him to `waypoint` sooner than taking
 * up his new line now (its leg in from `switchFrom`). Not when he is not on
 * the curve or is past its end (none then, or when holding is no quicker).
 * Each way is timed at his speed now, held to what the curve, and coming
 * round onto his line, allow. */
std::optional<Vector2F> holdTurnHandoverOf(const MatchPlayer& player,
                                           const LineLeg& held, Vector2F waypoint,
                                           Vector2F next, Vector2F switchFrom,
                                           float freeShare)
{
  const float speed = length(player.velocity);
  if (speed < HOLD_TURN_MIN_SPEED) return std::nullopt;
  const Turn turn = turnThrough(held.from, held.waypoint, held.next,
                                player.routeApexOffset, player.routeOutwardRoom);
  if (turn.straight || turn.radius <= 0.0f) return std::nullopt;
  const Vector2F at = asVector(player.position);
  const LineAim aim =
      aimThrough(turn, held.waypoint, at, held.next, speed, 0.0f, false);
  if (aim.exited || aim.toTurn > ON_ARC_METRES) return std::nullopt;
  const LineLeg onwardLeg{held.waypoint, waypoint, next, false};
  if (turn.joined)
  {
    // A joined curve hands over where it ends (the middle of the leg out,
    // where the next turn's curve starts, the same way): the rest of it,
    // round the arc in to the apex and the arc out, or the arc out alone.
    const auto round = [&](Vector2F centre, Vector2F from, Vector2F to)
    {
      const Vector2F a = unitOf({from.x - centre.x, from.y - centre.y});
      const Vector2F b = unitOf({to.x - centre.x, to.y - centre.y});
      return std::max(0.0f, turn.sense * std::atan2(a.x * b.y - a.y * b.x,
                                                    a.x * b.x + a.y * b.y));
    };
    const bool pastApex =
        (at.x - turn.apex.x) * turn.way.x + (at.y - turn.apex.y) * turn.way.y >= 0.0f;
    const float rest =
        pastApex ? turn.radiusOut * round(turn.centreOut, at, turn.end)
                 : turn.radiusIn * round(turn.centre, at, turn.apex) +
                       turn.radiusOut * round(turn.centreOut, turn.apex, turn.end);
    const float holding =
        (aim.toTurn + rest) / std::min(speed, cornerSpeedOf(player, turn.radius, freeShare)) +
        joinSecondsOf(player, aim.exit, aim.exitHeading, speed, onwardLeg,
                      freeShare);
    const float switching =
        joinSecondsOf(player, at, unitOf(player.velocity), speed,
                      {switchFrom, waypoint, next, false}, freeShare);
    if (holding >= switching) return std::nullopt;
    return aim.exit;
  }
  // Round the curve in short steps, to where his new line takes over.
  const LineLeg onward{held.waypoint, waypoint, next, false};
  const Turn onwardTurn = turnThrough(onward.from, waypoint, next,
                                      player.routeApexOffset, player.routeOutwardRoom);
  const float r = turn.radius;
  const Vector2F exitRadial =
      unitOf({aim.exit.x - turn.centre.x, aim.exit.y - turn.centre.y});
  Vector2F radial = unitOf({at.x - turn.centre.x, at.y - turn.centre.y});
  Vector2F point{turn.centre.x + radial.x * r, turn.centre.y + radial.y * r};
  Vector2F heading{-radial.y * turn.sense, radial.x * turn.sense};
  float arcMetres = aim.toTurn;
  const float aligned = std::cos(HEADING_TURN_MIN_RADIANS);
  // Where his new line's turn starts (on its leg in from the held waypoint):
  // he hands over by then, so as to take that turn whole.
  const LineAim intoOnward =
      aimThrough(onwardTurn, waypoint, held.waypoint, next, speed, 0.0f);
  for (int step = 0; step < HOLD_TRACE_STEPS; ++step)
  {
    const float along = (point.x - held.waypoint.x) * intoOnward.direction.x +
                        (point.y - held.waypoint.y) * intoOnward.direction.y;
    if (along >= intoOnward.toTurn) break;
    const LineAim onwardAim =
        aimThrough(onwardTurn, waypoint, point, next, speed, 0.0f);
    const float match =
        heading.x * onwardAim.direction.x + heading.y * onwardAim.direction.y;
    if (match >= aligned) break;
    const float left = turn.sense * std::atan2(radial.x * exitRadial.y -
                                                   radial.y * exitRadial.x,
                                               radial.x * exitRadial.x +
                                                   radial.y * exitRadial.y);
    if (left <= 0.0f)
    {
      point = aim.exit;
      heading = aim.exitHeading;
      break;
    }
    const float by = std::min(left, HOLD_TRACE_METRES / r);
    radial = rotatedBy(radial, turn.sense * by);
    arcMetres += r * by;
    point = {turn.centre.x + radial.x * r, turn.centre.y + radial.y * r};
    heading = {-radial.y * turn.sense, radial.x * turn.sense};
  }
  const float holding = arcMetres / std::min(speed, cornerSpeedOf(player, r, freeShare)) +
                        joinSecondsOf(player, point, heading, speed, onward, freeShare);
  const float switching =
      joinSecondsOf(player, at, unitOf(player.velocity), speed,
                    {switchFrom, waypoint, next, false}, freeShare);
  if (holding >= switching) return std::nullopt;
  return point;
}
}  // namespace

float MatchEngine::executionErrorScaleBinturong(const MatchPlayer& player) const
{
  // Tired players lose precision (the old tethered dribble's heavy touch;
  // contacts scale theirs in carryOutContactBinturong).
  return 1.0f + tierLoss(TECHNIQUE_SHAPE, player.stamina) *
                    MatchTuning::Fatigue::SPENT_TECHNIQUE_ERROR;
}

void MatchEngine::moveControlledBinturong(MatchPlayer& player, Vector2F desired,
                                          float desiredSpeed, float topSpeed,
                                          float fatigue, float dt,
                                          const MatchPlayer* faceToward)
{
  using C = MatchTuning::Control;
  // Finer sub-steps: the stick is followed with 20 ms kinematics while the
  // AI keeps the common step.
  const float facing = player.facingAngle;
  const float subStep = dt / static_cast<float>(C::PHYSICS_SUBSTEPS);
  for (int step = 0; step < C::PHYSICS_SUBSTEPS; ++step)
    stepKinematicsBinturong(player, desired, desiredSpeed, topSpeed, fatigue,
                            subStep);
  if (faceToward)
  {
    // A jockeying defender keeps his eyes (and hips) on the ball.
    const float target =
        fastAtan2(pitch.fractionY(faceToward->position.y - player.position.y),
                  pitch.fractionX(faceToward->position.x - player.position.x));
    float difference = target - facing;
    if (difference > std::numbers::pi_v<float>)
      difference -= TWO_PI;
    else if (difference < -std::numbers::pi_v<float>)
      difference += TWO_PI;
    player.targetAngle = target;
    player.facingAngle = facing + std::clamp(difference, -player.turnRate * dt,
                                             player.turnRate * dt);
    if (player.facingAngle > std::numbers::pi_v<float>)
      player.facingAngle -= TWO_PI;
    else if (player.facingAngle < -std::numbers::pi_v<float>)
      player.facingAngle += TWO_PI;
  }
}

float MatchEngine::currentTopSpeedBinturong(const MatchPlayer& player) const
{
  const float injury =
      player.isInjured ? MatchTuning::Injury::INJURED_SPEED_SCALE : 1.0f;
  return fatiguedTopSpeed(
      player.maxSpeed, player.stamina,
      MatchRules::walkRunTransitionSpeed(player.heightMetres), injury);
}

SpeedBand MatchEngine::speedBandBinturong(const MatchPlayer& player,
                                          MovementMode mode) const
{
  const float transition =
      MatchRules::walkRunTransitionSpeed(player.heightMetres);
  const float critical = tieredCriticalSpeed(
      derivedPhysicalValue(DerivedStat::ENDURANCE,
                           player.derivedStat(DerivedStat::ENDURANCE)),
      player.stamina, transition);
  return speedBandFrom(mode, transition, critical,
                       currentTopSpeedBinturong(player));
}

float MatchEngine::directionTopSpeedBinturong(const MatchPlayer& player,
                                              Vector2F velocity) const
{
  using P = MatchTuning::Player;
  // His body's facing as a metric unit vector.
  const Vector2F body{std::cos(player.facingAngle),
                      std::sin(player.facingAngle)};
  const float bodyLength = std::max(length(body), EPSILON);
  const float speed = length(velocity);
  const float alignment =
      speed > EPSILON
          ? std::clamp((body.x * velocity.x + body.y * velocity.y) /
                           (bodyLength * speed),
                       -1.0f, 1.0f)
          : 1.0f;
  const std::array<float, MOVEMENT_MODE_COUNT> tops{
      speedBandBinturong(player, MovementMode::WALK).high,
      speedBandBinturong(player, MovementMode::JOG).high,
      speedBandBinturong(player, MovementMode::RUN).high,
      currentTopSpeedBinturong(player)};
  const auto cap = [&](std::size_t direction)
  {
    return P::DIRECTION_SPEED_SHARE[direction] *
           tops[static_cast<std::size_t>(P::DIRECTION_TOP_BAND[direction])];
  };
  const DirectionMix mix = directionMix(std::acos(alignment));
  return cap(mix.below) + (cap(mix.below + 1) - cap(mix.below)) * mix.onward;
}

float MatchEngine::hardestBrakingBinturong(const MatchPlayer& player) const
{
  return playerBraking(player, 1.0f);
}

void MatchEngine::coastBinturong(MatchPlayer& player, float dt)
{
  // No drive, no steering, no braking of his own: he carries the velocity
  // the collision left him, slowing as a runner who has stopped running.
  const float speed = length(player.velocity);
  const float slowed =
      std::max(0.0f, speed - MatchTuning::Collision::PAUSE_DECELERATION * dt);
  const float scale = speed > EPSILON ? slowed / speed : 0.0f;
  const Vector2F before = player.velocity;
  player.velocity = {before.x * scale, before.y * scale};
  // The mean of the step's velocities: exact for a steady slowing.
  const Vector2F moved = {0.5f * (before.x + player.velocity.x) * dt,
                                  0.5f * (before.y + player.velocity.y) * dt};
  player.position =
      moveBody(player.position,
               PitchPoint{player.position.x + moved.x, player.position.y + moved.y});
  player.collisionPause = std::max(0.0f, player.collisionPause - dt);
}

void MatchEngine::stumbleBinturong(MatchPlayer& player, float dt)
{
  using C = MatchTuning::Collision;
  constexpr float PI = std::numbers::pi_v<float>;
  // His recovery steps brake him; he neither drives nor steers.
  const float speed = length(player.velocity);
  const float slowed = std::max(
      0.0f, speed - C::STUMBLE_BRAKING_SHARE * hardestBrakingBinturong(player) * dt);
  const float scale = speed > EPSILON ? slowed / speed : 0.0f;
  const Vector2F before = player.velocity;
  player.velocity = {before.x * scale, before.y * scale};
  const Vector2F moved = {0.5f * (before.x + player.velocity.x) * dt,
                                  0.5f * (before.y + player.velocity.y) * dt};
  player.position =
      moveBody(player.position,
               PitchPoint{player.position.x + moved.x, player.position.y + moved.y});
  // His body turns to face the way he is carried (forward is where he
  // copes best), at his normal rate.
  if (speed > EPSILON)
  {
    const float target = std::atan2(before.y,
                                    before.x);
    float difference = target - player.facingAngle;
    while (difference > PI) difference -= TWO_PI;
    while (difference < -PI) difference += TWO_PI;
    const float turn = bodyTurnRate(player, 1.0f) * dt;
    player.facingAngle += std::clamp(difference, -turn, turn);
    if (player.facingAngle > PI)
      player.facingAngle -= TWO_PI;
    else if (player.facingAngle < -PI)
      player.facingAngle += TWO_PI;
    player.targetAngle = player.facingAngle;
  }
  player.stumbleSeconds = std::max(0.0f, player.stumbleSeconds - dt);
}

void MatchEngine::fallenBinturong(MatchPlayer& player, float dt)
{
  using C = MatchTuning::Collision;
  using Pitch = MatchTuning::Pitch;
  player.downSeconds += dt;
  stats.collisionDownSeconds += dt;
  if (state == MatchState::PLAYING) stats.collisionDownPlaySeconds += dt;
  const auto moveBy = [this, &player](Vector2F metres)
  {
    const Vector2F moved = metres;
    player.position =
        moveBody(player.position,
               PitchPoint{player.position.x + moved.x, player.position.y + moved.y});
  };
  // Sliding or tumbling on: the grass slows him to a stop.
  const float speed = length(player.velocity);
  if (speed > EPSILON)
  {
    const float slowed = std::max(0.0f, speed - C::FALL_DECELERATION * dt);
    const float scale = slowed / speed;
    const Vector2F before = player.velocity;
    player.velocity = {before.x * scale, before.y * scale};
    // Exact for a steady slowing that stops within the step.
    const float travelled = slowed > 0.0f
                                ? 0.5f * (speed + slowed) * dt
                                : speed * speed / (2.0f * C::FALL_DECELERATION);
    moveBy({before.x / speed * travelled, before.y / speed * travelled});
    if (player.lyingAcross) player.rolledMetres += travelled;
    return;
  }
  player.velocity = {0.0f, 0.0f};

  // Rolling about in pain: through a big hit's least time down, a roll now
  // and then, mostly back the way the last one came.
  if (player.rollSeconds > 0.0f)
  {
    const float rolling = std::min(player.rollSeconds, dt);
    // Across his body: a quarter turn from the way he lies.
    const Vector2F along{std::cos(player.lyingAngle),
                         std::sin(player.lyingAngle)};
    const float alongLength = std::max(length(along), EPSILON);
    const Vector2F across{-along.y / alongLength, along.x / alongLength};
    const float metres = C::ROLL_SPEED * rolling * player.rollDirection;
    moveBy({across.x * metres, across.y * metres});
    player.rolledMetres += metres;
    player.rollSeconds -= rolling;
    if (player.rollSeconds <= 0.0f)
      player.rollPauseSeconds =
          randomFloat(C::ROLL_PAUSE_SHORTEST_SECONDS, C::ROLL_PAUSE_LONGEST_SECONDS);
    return;
  }
  if (player.downSeconds < player.bigHitSeconds)
  {
    player.rollPauseSeconds -= dt;
    if (player.rollPauseSeconds > 0.0f) return;
    const float chance =
        std::clamp((player.fallJolt - C::BIG_HIT_JOLT_FROM) /
                       (C::BIG_HIT_JOLT_CERTAIN - C::BIG_HIT_JOLT_FROM),
                   0.0f, 1.0f);
    if (randomFloat(0.0f, 1.0f) < chance)
    {
      const float turns = randomFloat(C::ROLL_SHORTEST_TURNS, C::ROLL_LONGEST_TURNS);
      player.rollSeconds = turns * C::ROLL_METRES_PER_TURN / C::ROLL_SPEED;
      player.rollDirection =
          player.rollDirection == 0.0f
              ? (randomFloat(0.0f, 1.0f) < 0.5f ? 1.0f : -1.0f)
              : (randomFloat(0.0f, 1.0f) < C::ROLL_REVERSE_SHARE ? -player.rollDirection
                                                                  : player.rollDirection);
    }
    else
    {
      player.rollPauseSeconds =
          randomFloat(C::ROLL_PAUSE_SHORTEST_SECONDS, C::ROLL_PAUSE_LONGEST_SECONDS);
    }
    return;
  }

  // Stopped, and past a big hit's least time: up when his brain says.
  if (!player.gettingUp)
  {
    const float seconds = chooseGetUp(player);
    if (seconds <= 0.0f) return;
    player.gettingUp = true;
    player.getUpSeconds = seconds;
    player.getUpTotalSeconds = seconds;
  }
  player.getUpSeconds -= dt;
  if (player.getUpSeconds > 0.0f) return;
  if (player.downSeconds >= C::GINGERLY_AFTER_SECONDS) ++stats.collisionDownsFifteen;
  standUp(player);
}

FM_KERNEL_CLONES void MatchEngine::integrateMovementsBinturong(
    const std::uint8_t* slots, std::size_t count, float dt, bool walking)
{
  using P = MatchTuning::Player;
  using Pitch = MatchTuning::Pitch;
  constexpr float PI = std::numbers::pi_v<float>;
  // Positions are metres and facing angles true angles.
  planHeaderTurnBinturong();
  // The movers are gathered into arrays and advanced with straight-line code
  // (every branch of stepKinematicsBinturong() evaluated and selected per
  // player), so the compiler can process several players at once. The
  // arithmetic of each player is exactly that of stepKinematicsBinturong().
  constexpr std::size_t LANES = 32;
  alignas(16) std::array<float, LANES> offsetX;
  alignas(16) std::array<float, LANES> offsetY;
  alignas(16) std::array<float, LANES> velocityX;
  alignas(16) std::array<float, LANES> velocityY;
  alignas(16) std::array<float, LANES> positionX;
  alignas(16) std::array<float, LANES> positionY;
  alignas(16) std::array<float, LANES> facing;
  alignas(16) std::array<std::int32_t, LANES> turning;
  alignas(16) std::array<float, LANES> nextFacing;
  alignas(16) std::array<float, LANES> nextHeading;
  alignas(16) std::array<float, LANES> maxSpeed;
  alignas(16) std::array<float, LANES> stamina;
  alignas(16) std::array<float, LANES> reserve;
  alignas(16) std::array<float, LANES> injury;
  // Giving way (way-round plan): the speed he holds himself to (m/s), or
  // none (a large value).
  alignas(16) std::array<float, LANES> giveWay;
  alignas(16) std::array<float, LANES> acceleration;
  alignas(16) std::array<float, LANES> braking;
  alignas(16) std::array<float, LANES> intentScale;
  alignas(16) std::array<float, LANES> urgencyDistance;
  alignas(16) std::array<float, LANES> maxUrgency;
  alignas(16) std::array<float, LANES> pacing;
  alignas(16) std::array<float, LANES> criticalSpeed;
  alignas(16) std::array<float, LANES> transition;
  alignas(16) std::array<float, LANES> burstSaving;
  alignas(16) std::array<float, LANES> talk;
  alignas(16) std::array<float, LANES> shout;
  alignas(16) std::array<float, LANES> turnRate;
  // A body orientation asked of him (orientation plan), and its angle.
  alignas(16) std::array<std::int32_t, LANES> bodyRequested;
  alignas(16) std::array<float, LANES> bodyRequest;
  alignas(16) std::array<float, LANES> grip;
  // Into a stop on his route (a sharp reversal): the plant's extra braking
  // (a share of his own), and just out of one the push-off's extra
  // acceleration (a share).
  alignas(16) std::array<float, LANES> plantGain;
  alignas(16) std::array<float, LANES> pushOffGain;
  alignas(16) std::array<float, LANES> targetVelocityX;
  alignas(16) std::array<float, LANES> targetVelocityY;
  // On a route: the metres beyond the waypoint he heads for, to the end.
  alignas(16) std::array<float, LANES> routeBeyond;
  alignas(16) std::array<std::int32_t, LANES> onRoute;
  // On a route: where his line points (a unit vector, metres), the turn at
  // his waypoint (its radius, and how far he still has to its start), the
  // turn at the one after (radius, and its lead before that waypoint), and
  // the leg between them.
  alignas(16) std::array<float, LANES> aimX;
  alignas(16) std::array<float, LANES> aimY;
  alignas(16) std::array<float, LANES> cornerRadius;
  alignas(16) std::array<float, LANES> toTurn;
  alignas(16) std::array<float, LANES> nextCornerRadius;
  alignas(16) std::array<float, LANES> nextCornerLead;
  alignas(16) std::array<float, LANES> nextLeg;
  // Dribbling to a planned meet: the most he may run toward it so as not
  // to get there before the ball (dribble meet plan).
  alignas(16) std::array<float, LANES> onTimeSpeed;
  // Dribbling a route: its turn beyond the second (radius, and metres from
  // the point he heads for to its start), and the pace he carries it at as
  // a share of his top speed (0: not dribbling).
  alignas(16) std::array<float, LANES> thirdCornerRadius;
  alignas(16) std::array<float, LANES> thirdCornerLead;
  alignas(16) std::array<float, LANES> meetPaceShare;
  // He means to reach his target at full speed (1) rather than stop on it.
  alignas(16) std::array<float, LANES> throughSpeed;
  // Movement modes (when his brain uses them): his mode and its style.
  std::array<std::int8_t, LANES> modeOf;
  std::array<const MovementStyle*, LANES> styleOf;
  std::array<MatchPlayer*, LANES> movers;
  std::size_t moving = 0;
  // The ball's flight ahead, for timing jumps (aerial challenge plan step 1):
  // only a loose ball in open play that is high or rising.
  jumpPath.clear();
  if (!walking && state == MatchState::PLAYING && !getPossessionState().isShot &&
      (!getPossessionAttachment() || hasTouchClaim()) &&
      (ball.z > lowestHeadLine() ||
       ball.velocityZ > 0.0f))
  {
    // Its own flight (drag, spin and curve), run ahead and put back.
    constexpr float TICK = MatchTuning::Timing::FIXED_STEP_SECONDS /
                           static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
    const BallMotion saved = ballMotion();
    for (float seconds = TICK; seconds <= MatchTuning::Aerial::JUMP_LOOKAHEAD_SECONDS;
         seconds += TICK)
    {
      integrateBall(TICK);
      const float height = ball.z;
      if (height <= 0.0f) break;
      jumpPath.push_back({seconds, asVector(ball.position), height});
    }
    setBallMotion(saved);
  }
  for (std::size_t index = 0; index < count; ++index)
  {
    MatchPlayer& player = players[slots[index]];
    // In the air he carries on as he took off; on the ground he may jump. A
    // restart walk starts on the ground.
    if (walking)
    {
      player.feetHeight = 0.0f;
      player.jumpSpeed = 0.0f;
    }
    else
    {
      // A keeper may go down at a loose ball (goalkeeper plan step 8): from
      // the next step his dive moves him.
      if (player.isGoalkeeper)
      {
        player.slowSeconds = length(player.velocity) <= MatchTuning::Stride::WALK_TOP_SPEED
                                 ? player.slowSeconds + dt
                                 : 0.0f;
        if (considerSmotherBinturong(player)) continue;
      }
      const bool wasAirborne = player.airborne() && player.feetHeight > 0.0f;
      stepJumpBinturong(player, dt, jumpPath);
      if (wasAirborne || player.feetHeight > 0.0f) continue;
    }
    // In play he means to stay on the pitch; going to a restart position
    // (fetching the ball that went out) he can go as far as the boards, and
    // walking out or off, through the tunnel.
    PitchPoint target = walking ? clampToReach(player.movementTarget)
                              : clampToPitch(player.tacticalTarget);
    // The end of a route: his target; dribbling it, where he is taking the
    // ball (his target is then his next meet with it, beside his waypoint).
    PitchPoint routeEnd = !walking && hasTouchClaim() &&
                                getPossessionAttachment() == player.player &&
                                ball.meetFor == player.player
                            ? ball.dribbleGoal
                            : target;
    routeEnd = walking ? clampToReach(routeEnd) : clampToPitch(routeEnd);
    // A brain with a target lag has its players absorb a new target
    // gradually (a side unfamiliar with its tactics more slowly); without one
    // they head straight for it.
    const bool urgent = !walking && player.urgentMovement;
    const float targetBlend =
        targetBlends[(player.isHomeTeam ? 0U : 2U) + (urgent ? 1U : 0U)];
    // On a route (planned by a brain without target lag) he heads for its
    // waypoints in turn, moving on to the next as he reaches each one. A
    // restart walk has none.
    player.sinceStopSeconds = std::min(player.sinceStopSeconds + dt, 1.0e6f);
    if (walking) player.routeWaypoints = 0;
    if (player.routeWaypoints == 0) player.holdingTurn = false;
    if (player.routeWaypoints == 1) player.nextTarget = routeEnd;
    const float speedNow = length(player.velocity);
    // With the ball, he moves by what his Dribbling unlocks of his Agility
    // and Acceleration (movementRating).
    const float freeShare =
        freeShareBinturong(player, !walking && carryingBinturong(player));
    // His route moving on before the curve he is on ends (a new route given
    // him, or his waypoint reached at the curve's middle): he holds that
    // curve to its end if that gets him to his new waypoint sooner than
    // coming off it onto his new line now, which can be sharp; his new line
    // is then the one planned from that waypoint (its turn starting about
    // where the held curve ends).
    const auto holdCurve = [&](const LineLeg& curve, Vector2F switchFrom)
    {
      const std::optional<Vector2F> handover =
          holdTurnHandoverOf(player, curve, asVector(player.movementTarget),
                             asVector(player.nextTarget), switchFrom, freeShare);
      if (!handover) return false;
      player.holdingTurn = true;
      player.heldUntil = asPoint(*handover);
      player.heldLegStart = asPoint(curve.from);
      player.heldWaypoint = asPoint(curve.waypoint);
      player.heldNext = asPoint(curve.next);
      player.routeLegStart = asPoint(curve.waypoint);
      return true;
    };
    // A waypoint given him (not one he moved on to): its leg starts here,
    // unless he holds the curve he was on.
    if (player.routeWaypoints > 0 &&
        (player.movementTarget.x != player.routeLegEnd.x ||
         player.movementTarget.y != player.routeLegEnd.y))
    {
      const LineLeg was =
          player.holdingTurn
              ? LineLeg{asVector(player.heldLegStart), asVector(player.heldWaypoint),
                        asVector(player.heldNext), true}
              : LineLeg{asVector(player.routeLegStart), asVector(player.routeLegEnd),
                        asVector(player.routeLegNext), true};
      player.holdingTurn = false;
      if (!holdCurve(was, asVector(player.position)))
        player.routeLegStart = player.position;
      player.routeLegEnd = player.movementTarget;
    }
    // His line through a turn (movement targets plan 3b), in metres: the
    // turn there, and where the line points from where he is.
    const auto lineThrough = [&](const LineLeg& leg)
    {
      const Turn turn = turnThrough(leg.from, leg.waypoint, leg.next,
                                    player.routeApexOffset, player.routeOutwardRoom);
      return std::pair{turn, aimThrough(turn, leg.waypoint, asVector(player.position),
                                        leg.next, speedNow, speedNow * dt, !leg.held)};
    };
    const auto routeLeg = [&]
    {
      return LineLeg{asVector(player.routeLegStart), asVector(player.movementTarget),
                     asVector(player.nextTarget), false};
    };
    // A curve he holds he lets go of where he decided to hand over to his
    // route's line (round the curve, level with that point), or where the
    // curve ends.
    if (player.holdingTurn)
    {
      const LineLeg held{asVector(player.heldLegStart), asVector(player.heldWaypoint),
                         asVector(player.heldNext), true};
      const auto [heldTurn, heldLine] = lineThrough(held);
      const Vector2F at = asVector(player.position);
      const Vector2F until = asVector(player.heldUntil);
      const Vector2F here = unitOf({at.x - heldTurn.centre.x, at.y - heldTurn.centre.y});
      const Vector2F there =
          unitOf({until.x - heldTurn.centre.x, until.y - heldTurn.centre.y});
      const float past = heldTurn.sense * std::atan2(there.x * here.y - there.y * here.x,
                                                     there.x * here.x + there.y * here.y);
      if (heldLine.exited || heldTurn.straight || heldTurn.radius <= 0.0f ||
          (!heldTurn.joined && past >= 0.0f))
        player.holdingTurn = false;
    }
    // He moves on from his waypoint where his line leaves its turn (a stop:
    // made, where he turns).
    if (const auto [made, madeLine] = player.routeWaypoints > 0
                                          ? lineThrough(routeLeg())
                                          : std::pair<Turn, LineAim>{};
        player.routeWaypoints > 0 && madeLine.exited)
    {
      if (made.radius <= 0.0f && !made.straight) player.sinceStopSeconds = 0.0f;
      const LineLeg was = routeLeg();
      player.routeLegStart = player.movementTarget;
      player.movementTarget = player.nextTarget;
      player.routeLegEnd = player.movementTarget;
      --player.routeWaypoints;
      ++player.routeAdvances;
      if (player.routeWaypoints == 1) player.nextTarget = routeEnd;
      // Reached at the curve's middle, he may hold the rest of it.
      if (madeLine.reachedEarly && player.routeWaypoints > 0 && !player.holdingTurn)
        holdCurve(was, was.waypoint);
    }
    // Dribbling with a planned meet (dribble meet plan), he runs his route's
    // line if he has one (below), else a corner at the meet point, on the
    // way his touch there sends the ball; the route stays, for his touches.
    // Without a meet, his body chases the ball (its target).
    const bool hasRoute = player.routeWaypoints > 0;
    const bool meeting = !walking && hasTouchClaim() &&
                         getPossessionAttachment() == player.player &&
                         ball.meetFor == player.player;
    const bool chasingBall = !meeting && hasRoute && hasTouchClaim() &&
                             getPossessionAttachment() == player.player;
    const bool routed = meeting || (hasRoute && !chasingBall);
    Turn turn;
    turn.straight = true;
    turn.radius = STRAIGHT_ON_RADIUS;
    const Turn straightOn = turn;
    Turn nextTurn = turn;
    LineAim line;
    // Planting into a stop on his route, his body twists toward the way
    // out (by his Agility), at his turn rate.
    bool planting = false;
    float plantFacing = 0.0f;
    if (hasRoute)
    {
      std::tie(turn, line) =
          lineThrough(lineLegsOf(player, asVector(routeEnd)).front());
      if (turn.radius <= 0.0f && !turn.straight &&
          line.toTurn <= P::PLANT_METRES && speedNow > PIVOT_BLEND_SPEED)
      {
        const Vector2F run = unitOf(player.velocity);
        const Vector2F nextMetres = asVector(player.nextTarget);
        const Vector2F way =
            unitOf({nextMetres.x - turn.spot.x, nextMetres.y - turn.spot.y});
        const float wanted = std::atan2(run.x * way.y - run.y * way.x,
                                        run.x * way.x + run.y * way.y);
        const float twist =
            (P::PLANT_TWIST_DEGREES_LOW +
             (P::PLANT_TWIST_DEGREES_HIGH - P::PLANT_TWIST_DEGREES_LOW) *
                 statShare(player, DerivedStat::AGILITY, freeShare)) *
            PI / 180.0f;
        const Vector2F body = rotatedBy(run, std::clamp(wanted, -twist, twist));
        plantFacing = std::atan2(body.y,
                                 body.x);
        planting = true;
      }
      // Where his line points, at least as far on as he runs in
      // ROUTE_AIM_LEAD_SECONDS: read a step later (his dribble aim), he is
      // not yet on it.
      const float aimMetres = std::max(
          {1.0f, speedNow * ROUTE_AIM_LEAD_SECONDS,
           length(player.movementTarget - player.position)});
      const Vector2F aimed =
          {line.direction.x * aimMetres, line.direction.y * aimMetres};
      player.routeAim = PitchPoint{player.position.x + aimed.x,
                         player.position.y + aimed.y};
      // The turn after: holding a curve, the one at his waypoint; with a
      // second waypoint, the one there (into the end).
      const std::vector<LineLeg> legs = lineLegsOf(player, asVector(routeEnd));
      if (legs.size() > 1)
        nextTurn = turnThrough(legs[1].from, legs[1].waypoint, legs[1].next,
                               player.routeApexOffset, player.routeOutwardRoom);
      player.routeLegNext = player.nextTarget;
    }
    else if (targetBlend >= 1.0f)
    {
      player.movementTarget = target;
    }
    else
    {
      player.movementTarget.x +=
          (target.x - player.movementTarget.x) * targetBlend;
      player.movementTarget.y +=
          (target.y - player.movementTarget.y) * targetBlend;
    }
    if (!hasRoute) player.nextTarget = player.movementTarget;
    // The end of his run: the tactical target (lagged, for a brain with a
    // target lag), whatever waypoints lie before it.
    const PitchPoint end = hasRoute ? target : player.movementTarget;
    const PitchPoint heading =
        chasingBall || meeting ? target : player.movementTarget;
    const Vector2F offset = heading - player.position;
    // The rest of the route beyond the waypoint he heads for.
    const Vector2F onward =
        player.nextTarget - player.movementTarget;
    const Vector2F last = end - player.nextTarget;
    float beyond = routed ? length(onward) + length(last) : 0.0f;
    float onTime = std::numeric_limits<float>::max();
    // Dribbling, the corners beyond the meet, as (radius, metres from the
    // meet to where the turn starts); none by default.
    std::pair<float, float> secondCorner{STRAIGHT_ON_RADIUS, 0.0f};
    std::pair<float, float> thirdCorner{STRAIGHT_ON_RADIUS, 0.0f};
    if (meeting)
    {
      // Dribbling a route, he also slows ahead for its turns beyond the
      // meet: the one at his waypoint and the one after (seen from the
      // meet point; past the first's exit, only the second).
      const Turn routeTurn = turn;
      const Turn routeNext = nextTurn;
      if (hasRoute)
      {
        // His line's turn now (a curve he holds, else the one at his
        // waypoint) and the waypoint of the one after.
        const std::vector<LineLeg> legs = lineLegsOf(player, asVector(routeEnd));
        const Vector2F meet = asVector(ball.meetPoint);
        const Vector2F waypoint = legs.front().waypoint;
        const Vector2F next = legs.front().next;
        const Vector2F after = legs.size() > 1 ? legs[1].waypoint : next;
        const LineAim fromMeet = aimThrough(routeTurn, waypoint, meet, next, speedNow,
                                            0.0f, !legs.front().held);
        const float toNext =
            length({waypoint.x - meet.x, waypoint.y - meet.y}) +
            length({after.x - waypoint.x, after.y - waypoint.y}) - routeNext.lead;
        if (fromMeet.exited)
          secondCorner = {routeNext.radius,
                          length({after.x - meet.x, after.y - meet.y}) -
                              routeNext.lead};
        else
        {
          secondCorner = {routeTurn.radius, fromMeet.toTurn};
          thirdCorner = {routeNext.radius, toNext};
        }
      }
    }
    if (meeting)
    {
      // The corner at the meet: in from where his last touch left him, out
      // along his next touch, as far as it pushes the ball (he runs on
      // through it). He gets there no sooner than the ball, within his
      // comfortable reach of it.
      const Vector2F meet = asVector(ball.meetPoint);
      const Vector2F out{meet.x + ball.meetOut.x * ball.meetPush,
                         meet.y + ball.meetOut.y * ball.meetPush};
      // The corner, unless his last touch went wrong, the meet is not ahead
      // of him (beside or behind: the corner would carry him past it), or
      // his line has already left the turn without him meeting the ball.
      // Slow (below close-control pace), he simply goes to it: a corner
      // only matters at pace, and from a standstill its arc can trap him.
      const float toMeet = length(offset);
      const bool ahead =
          speedNow > MatchTuning::Dribble::CLOSE_CONTROL_SPEED &&
          (offset.x * player.velocity.x + offset.y * player.velocity.y) >
              MEET_AHEAD_COSINE * toMeet * speedNow;
      // On a route he runs his own line through it, as without the ball,
      // his touches keeping the ball on it. He leaves it only where it would
      // lose him the ball (a touch gone wrong, the meet not ahead of him, so
      // that his line would carry him past it, or out of his reach from it),
      // or to shape his run into a strike decided for the meet.
      const bool striking = pendingStrike && pendingStrike->atMeet &&
                            pendingStrike->striker == &player;
      const bool onLine =
          hasRoute && !ball.meetRecovering && !striking && ahead &&
          [&]
          {
            const LineLeg now = lineLegsOf(player, asVector(routeEnd)).front();
            return metresOffLine(turn, now.from, now.waypoint, now.next, meet);
          }() <= MatchTuning::Dribble::KICK_REACH_METRES;
      bool straight = !onLine && (ball.meetRecovering || !ahead);
      if (!onLine && !straight)
      {
        turn = turnThrough(asVector(ball.meetLegStart), meet, out,
                           MatchTuning::Dribble::MEET_APEX_METRES,
                           MatchTuning::Dribble::MEET_OUTWARD_ROOM_METRES);
        line = aimThrough(turn, meet, asVector(player.position), out, speedNow,
                          speedNow * dt);
        straight = line.exited;
      }
      if (straight)
      {
        // Straight to where he can collect it, no corner; his next touch
        // takes up the route again.
        turn = straightOn;
        line = LineAim{};
        line.direction = toMeet > EPSILON
                             ? Vector2F{offset.x / toMeet, offset.y / toMeet}
                             : Vector2F{0.0f, 0.0f};
        line.toTurn = toMeet;
      }
      nextTurn = straightOn;
      beyond = ball.meetPush;
      // Due within the step (the ball in his reach, a touch not yet
      // allowed): he may run up level with it, never past it (how far it is
      // ahead of him along its roll, beyond its own pace).
      const float ballSpeed = length(ball.velocity);
      const Vector2F toBall = ball.position - player.position;
      const float level =
          ballSpeed > EPSILON
              ? (toBall.x * ball.velocity.x + toBall.y * ball.velocity.y) / ballSpeed
              : length(toBall);
      onTime = ball.meetSeconds > dt
                   ? std::max(0.0f, length(offset) -
                                        dribbleMeetGapBinturong(player)) /
                         ball.meetSeconds
                   : ballSpeed + std::max(0.0f, level) / std::max(dt, EPSILON);
    }
    // A moving end (the brain says how fast) is followed only on the last
    // leg: waypoints stand still.
    const Vector2F movingTarget =
        walking || routed ? Vector2F{0.0f, 0.0f} : player.targetVelocity;
    // A pass he holds turns his body to it (turning to pass), and so does a
    // header he means (turning to head).
    const std::optional<float> passFacing = strikeFacingBinturong(player);
    // A player standing on his spot stays put (the common case in stoppages).
    if (offset.x * offset.x + offset.y * offset.y <=
            P::ARRIVAL_DEAD_ZONE_METRES * P::ARRIVAL_DEAD_ZONE_METRES &&
        player.velocity.x == 0.0f && player.velocity.y == 0.0f &&
        movingTarget.x == 0.0f && movingTarget.y == 0.0f &&
        (!player.facingRequested || player.facingAngle == player.facingRequest) &&
        !passFacing)
      continue;
    const std::size_t lane = moving++;
    movers[lane] = &player;
    offsetX[lane] = offset.x;
    offsetY[lane] = offset.y;
    targetVelocityX[lane] = movingTarget.x;
    targetVelocityY[lane] = movingTarget.y;
    routeBeyond[lane] = beyond;
    onRoute[lane] = routed ? 1 : 0;
    aimX[lane] = line.direction.x;
    aimY[lane] = line.direction.y;
    cornerRadius[lane] = turn.radius;
    toTurn[lane] = line.toTurn;
    nextCornerRadius[lane] = meeting ? secondCorner.first : nextTurn.radius;
    nextCornerLead[lane] = meeting ? 0.0f : nextTurn.lead;
    // Holding a curve, the turn after it is the one at his waypoint.
    nextLeg[lane] = meeting                          ? secondCorner.second
                    : routed && !player.holdingTurn ? length(onward)
                                                    : 0.0f;
    thirdCornerRadius[lane] = thirdCorner.first;
    thirdCornerLead[lane] = thirdCorner.second;
    onTimeSpeed[lane] = onTime;
    // Dribbling, his legs run at the pace he carries it (above his run band
    // for a better dribbler): a sprint job, held to that pace.
    throughSpeed[lane] = player.arriveAtSpeed && !walking && !meeting ? 1.0f : 0.0f;
    meetPaceShare[lane] =
        meeting ? ball.meetPace / std::max(player.maxSpeed, EPSILON) : 0.0f;
    velocityX[lane] = player.velocity.x;
    velocityY[lane] = player.velocity.y;
    positionX[lane] = player.position.x;
    positionY[lane] = player.position.y;
    facing[lane] = player.facingAngle;
    maxSpeed[lane] = player.maxSpeed;
    stamina[lane] = player.stamina;
    reserve[lane] = player.sprintReserve;
    injury[lane] =
        player.isInjured ? MatchTuning::Injury::INJURED_SPEED_SCALE : 1.0f;
    giveWay[lane] = !walking && player.giveWaySpeed >= 0.0f
                        ? player.giveWaySpeed
                        : std::numeric_limits<float>::max();
    // Landing from a jump, he has only a share of his acceleration, braking
    // and turning.
    const float landing =
        player.landingSeconds > 0.0f ? MatchTuning::Aerial::LANDING_MOVEMENT_SHARE : 1.0f;
    // Planted for a thigh touch, he can't set off.
    acceleration[lane] = player.plantedSeconds > 0.0f
                             ? 0.0f
                             : playerAcceleration(player, freeShare) * landing;
    braking[lane] = playerBraking(player, freeShare) * landing;
    // How hard he chooses to run is his side's brain's.
    const MovementStyle& style = movementStyle(player.isHomeTeam);
    intentScale[lane] =
        walking
            ? style.restartWalkShare
            : style.intentSpeedShares[static_cast<std::size_t>(player.intent)];
    urgencyDistance[lane] = style.urgencyDistanceMetres;
    maxUrgency[lane] = style.maxUrgencyShare;
    pacing[lane] = style.fatigueWorkRateLoss;
    // Whether he keeps something in reserve on this job is his brain's.
    burstSaving[lane] =
        !walking && style.savesBurst[static_cast<std::size_t>(player.intent)]
            ? style.burstSavingReserve
            : 0.0f;
    styleOf[lane] = &style;
    modeOf[lane] =
        !style.usesModes
            ? std::int8_t{-1}
            : static_cast<std::int8_t>(
                  walking ? MovementMode::WALK
                          : style.intentModes[static_cast<std::size_t>(
                                player.intent)]);
    // Dribbling above his run band spends his burst reserve, as a sprint.
    if (meeting && modeOf[lane] >= 0)
      modeOf[lane] = static_cast<std::int8_t>(MovementMode::SPRINT);
    criticalSpeed[lane] = derivedPhysicalValue(
        DerivedStat::ENDURANCE, player.derivedStat(DerivedStat::ENDURANCE));
    transition[lane] = MatchRules::walkRunTransitionSpeed(player.heightMetres);
    talk[lane] = talkOf(player.isHomeTeam);
    shout[lane] = shoutWorkRate(player.isHomeTeam);
    turnRate[lane] = bodyTurnRate(player, freeShare) * landing;
    // Turning to a pass he holds comes before anything else asked of him.
    bodyRequested[lane] =
        passFacing || player.facingRequested || planting ? 1 : 0;
    bodyRequest[lane] = passFacing ? *passFacing
                        : planting ? plantFacing
                                   : player.facingRequest;
    const float agility = statShare(player, DerivedStat::AGILITY, freeShare);
    plantGain[lane] = P::PLANT_BRAKING_GAIN_LOW +
                      (P::PLANT_BRAKING_GAIN_HIGH - P::PLANT_BRAKING_GAIN_LOW) * agility;
    pushOffGain[lane] =
        player.sinceStopSeconds < P::PUSH_OFF_SECONDS
            ? P::PUSH_OFF_GAIN_LOW + (P::PUSH_OFF_GAIN_HIGH - P::PUSH_OFF_GAIN_LOW) * agility
            : 0.0f;
    grip[lane] = playerGrip(player, freeShare);
  }

  for (std::size_t lane = 0; lane < moving; ++lane)
  {
    const float targetX = offsetX[lane];
    const float targetY = offsetY[lane];
    const float targetDistance =
        std::sqrt(targetX * targetX + targetY * targetY);
    // How far he still has to go: on a route, to its end (the run's speed is
    // the whole route's, not the next waypoint's).
    const float remaining = targetDistance + routeBeyond[lane];
    const float fuel = stamina[lane];
    const float fatigue = std::clamp(1.0f - fuel, 0.0f, 1.0f);
    const float topSpeed =
        fatiguedTopSpeed(maxSpeed[lane], fuel, transition[lane], injury[lane]);
    const float critical =
        tieredCriticalSpeed(criticalSpeed[lane], fuel, transition[lane]);
    // Tired legs push, brake and turn less sharply (the fatigue tiers).
    const float legs = tieredMovement(fuel);
    // Players jog while in position but run hard to recover a lost position.
    // Intent speeds are shares of the fresh top speed; how much less hard a
    // tired player chooses to run is his brain's pacing.
    const float urgency =
        walking ? intentScale[lane]
                : std::max(intentScale[lane],
                           std::min(maxUrgency[lane],
                                    remaining / urgencyDistance[lane]));
    // Planting into a stop (a sharp reversal): the planted leg brakes
    // harder than his run does.
    const bool stopCorner = cornerRadius[lane] <= 0.0f;
    const float plant =
        stopCorner && toTurn[lane] <= P::PLANT_METRES ? plantGain[lane] : 0.0f;
    const float brake = braking[lane] * legs * (1.0f + plant);
    // With movement modes: the top of his mode's band (a jog job by its
    // distance ramp), as a share of his fresh top speed.
    const bool moded = modeOf[lane] >= 0;
    const std::array<float, MOVEMENT_MODE_COUNT> tops{
        speedBandFrom(MovementMode::WALK, transition[lane], critical, topSpeed)
            .high,
        speedBandFrom(MovementMode::JOG, transition[lane], critical, topSpeed)
            .high,
        speedBandFrom(MovementMode::RUN, transition[lane], critical, topSpeed)
            .high,
        topSpeed};
    // Going to a restart position: he walks, or jogs while walking the rest
    // would take more than a few seconds.
    const auto restartMode =
        remaining > tops[static_cast<std::size_t>(MovementMode::WALK)] *
                        P::RESTART_JOG_AFTER_SECONDS
            ? MovementMode::JOG
            : MovementMode::WALK;
    const float modeShare =
        !moded                    ? 0.0f
        : meetPaceShare[lane] > 0 ? meetPaceShare[lane]
        : walking ? tops[static_cast<std::size_t>(restartMode)] /
                        std::max(maxSpeed[lane], EPSILON)
                                  : modeTargetSpeed(
                                        static_cast<MovementMode>(modeOf[lane]),
                                        remaining, styleOf[lane]->jogRampMetres,
                                        tops) /
                                        std::max(maxSpeed[lane], EPSILON);
    const float effort =
        moded ? modeShare
              : (1.0f - pacing[lane] * fatigue + talk[lane] + shout[lane]) *
                    urgency;
    // On a job where his brain saves the burst reserve, a low reserve keeps
    // him closer to his critical speed (at an empty one, to it).
    const float sustainable = std::min(critical, topSpeed);
    const float fullness = std::clamp(
        reserve[lane] / std::max(tieredBurstCapacity(fuel), EPSILON), 0.0f,
        1.0f);
    const float saved =
        burstSaving[lane] > 0.0f
            ? std::clamp(fullness / std::max(burstSaving[lane], EPSILON), 0.0f,
                         1.0f)
            : 1.0f;
    const float pacedTop = sustainable + (topSpeed - sustainable) * saved;
    // Giving way to someone crossing ahead, he eases off to the speed his
    // brain holds him to.
    const float speedCap =
        std::min({pacedTop, maxSpeed[lane] * effort, giveWay[lane]});
    // Acceleration-speed profile a(v) = A0 (1 - v / vmax), stronger braking,
    // and a lateral limit (scaled by his grip) that forces a player to slow
    // down to turn sharply.
    // Just out of a stop, the push-off adds to his drive.
    const float push = acceleration[lane] * legs * (1.0f + pushOffGain[lane]);
    const float oldX = velocityX[lane];
    const float oldY = velocityY[lane];
    const float speed = std::sqrt(oldX * oldX + oldY * oldY);
    // An emptying burst reserve holds his power to his critical speed's,
    // speeding up and braking alike: above his critical speed he then
    // slows whatever he wants. Below a sprint, a moded job keeps to that
    // power whatever is in the reserve: only a sprint spends it.
    const bool spendsBurst =
        !moded || modeOf[lane] == static_cast<std::int8_t>(MovementMode::SPRINT);
    const AccelerationRange limits = burstLimits(
        push * std::max(0.05f, 1.0f - speed / topSpeed), brake, speed,
        spendsBurst ? fullness : 0.0f, critical, transition[lane]);
    // Quotients are formed for every lane and discarded where unused.
    const bool aimed = targetDistance > EPSILON;
    // On a route he follows his line; otherwise he heads for the target.
    const bool onLine = onRoute[lane] != 0;
    const float directionX = onLine ? aimX[lane] : targetX / targetDistance;
    const float directionY = onLine ? aimY[lane] : targetY / targetDistance;
    // His body against his run (orientation plan). Asked to face a way, he
    // moves in one of eight directions to it (forward, the forward
    // diagonals, sideways, the back diagonals, backward), or a blend of the
    // two his angle falls between: each with its own speed, top band and
    // drive (MatchTuning::Player::DIRECTION_*). Turning to the facing he
    // glides at first, then drives more as the turn completes. With nothing
    // asked his body follows his run, as before.
    const bool bodyAsked = bodyRequested[lane] != 0;
    const float bodyMetresX = std::cos(facing[lane]);
    const float bodyMetresY = std::sin(facing[lane]);
    const float bodyLength = std::max(
        std::sqrt(bodyMetresX * bodyMetresX + bodyMetresY * bodyMetresY),
        EPSILON);
    const float alignment = std::clamp(
        (bodyMetresX * directionX + bodyMetresY * directionY) / bodyLength,
        -1.0f, 1.0f);
    // Among the eight directions: his speed in each is its share of the
    // job's speed, held to the top of its highest band, blended between the
    // two his angle falls between.
    const DirectionMix mix =
        directionMix(bodyAsked && aimed ? std::acos(alignment) : 0.0f);
    const auto directionCap = [&](std::size_t direction)
    {
      return P::DIRECTION_SPEED_SHARE[direction] *
             std::min(speedCap,
                      tops[static_cast<std::size_t>(P::DIRECTION_TOP_BAND[direction])]);
    };
    const float runCap = directionCap(mix.below) +
                         (directionCap(mix.below + 1) - directionCap(mix.below)) *
                             mix.onward;
    const float drive = directionValue(P::DIRECTION_DRIVE_SHARE, mix);
    float bodyTurn = bodyRequest[lane] - facing[lane];
    bodyTurn = bodyTurn > PI    ? bodyTurn - TWO_PI
               : bodyTurn < -PI ? bodyTurn + TWO_PI
                                : bodyTurn;
    // Through a turn: no push beyond the drive angle, then more as it
    // completes.
    const float turnDrive =
        bodyAsked ? std::clamp(1.0f - std::abs(bodyTurn) /
                                          P::BODY_TURN_DRIVE_RADIANS,
                               0.0f, 1.0f)
                  : 1.0f;
    const bool gliding = turnDrive <= 0.0f;
    const float driveHigh =
        limits.high > 0.0f ? limits.high * drive * turnDrive : limits.high;
    // Arrive without overshooting: the desired speed falls off so he stops
    // on the target, braking as hard as the effort he is putting in (and
    // his power allows). A target moving away is closed on at that speed on
    // top.
    const float arrivalDeceleration = std::min(
        std::max(P::ARRIVAL_DECELERATION, ARRIVAL_BRAKING_SHARE *
                                              std::min(brake, -limits.low) *
                                              std::min(effort, 1.0f)),
        std::max(-limits.low, EPSILON));
    const float approach = remaining - ARRIVAL_TOLERANCE_METRES;
    // Arriving at full speed (running through his target), no slowing for
    // it; meeting his ball, slowed only to the pace he carries it on at
    // (the turns are the corners' to brake for); otherwise he stops on it.
    const float meetPaceNow = meetPaceShare[lane] * maxSpeed[lane];
    const float stopping =
        throughSpeed[lane] > 0.0f
            ? topSpeed
            : std::sqrt(meetPaceNow * meetPaceNow +
                        2.0f * arrivalDeceleration * std::max(approach, 0.0f));
    // On a route he must also make the turns at his waypoints: slowed to
    // the speed of each where it starts (on a turn, held to it). The
    // fastest he can take a turn of radius r is where his lateral limit
    // (below) holds him on it,
    // v^2 = a_lat(v) r with a_lat falling linearly with speed: the root of
    // v^2 + B v - C = 0. (Chang & Kram 2007 measured about the same on
    // flat curves: movement targets plan 3.)
    const float turnGrip = grip[lane] * legs;
    const auto turnSpeed = [&](float radius)
    {
      const float b = (P::LATERAL_ACCELERATION_SLOW -
                       P::LATERAL_ACCELERATION_FAST) *
                      turnGrip * radius / maxSpeed[lane];
      const float c = P::LATERAL_ACCELERATION_SLOW * turnGrip * radius;
      return 0.5f * (std::sqrt(b * b + 4.0f * c) - b);
    };
    const float firstTurn = turnSpeed(cornerRadius[lane]);
    const float secondTurn = turnSpeed(nextCornerRadius[lane]);
    // Into a stop he brakes later, for the plant's harder braking over its
    // last PLANT_METRES (already his, planting).
    const float plantDeceleration =
        plant > 0.0f ? arrivalDeceleration
                     : arrivalDeceleration * (1.0f + plantGain[lane]);
    const float cornering =
        stopCorner
            ? std::sqrt(2.0f * plantDeceleration *
                            std::clamp(toTurn[lane], 0.0f, P::PLANT_METRES) +
                        2.0f * arrivalDeceleration *
                            std::max(toTurn[lane] - P::PLANT_METRES, 0.0f))
            : std::sqrt(firstTurn * firstTurn +
                        2.0f * arrivalDeceleration *
                            std::max(toTurn[lane], 0.0f));
    const float nextCornering = std::sqrt(
        secondTurn * secondTurn +
        2.0f * arrivalDeceleration *
            std::max(targetDistance + nextLeg[lane] - nextCornerLead[lane],
                     0.0f));
    const float thirdTurn = turnSpeed(thirdCornerRadius[lane]);
    const float thirdCornering = std::sqrt(
        thirdTurn * thirdTurn +
        2.0f * arrivalDeceleration *
            std::max(targetDistance + thirdCornerLead[lane], 0.0f));
    // On a route, his own run must come round onto his line too: the angle
    // between the way he is running and his line, turned within the room he
    // has before his line's turn, is a turn of radius room / angle (taken at
    // its speed now, as there is no room to brake for it later).
    const float headingCosine =
        speed > EPSILON
            ? std::clamp((oldX * directionX + oldY * directionY) / speed, -1.0f, 1.0f)
            : 1.0f;
    const float headingTurn = std::acos(headingCosine);
    const float joining =
        headingTurn > HEADING_TURN_MIN_RADIANS
            ? turnSpeed(std::max(toTurn[lane], HEADING_ROOM_MIN_METRES) / headingTurn)
            : std::numeric_limits<float>::max();
    const float closing = std::min(
        onTimeSpeed[lane],
        onRoute[lane] != 0
            ? std::min(std::min(std::min(stopping, thirdCornering), joining),
                       std::min(cornering, nextCornering))
            : stopping);
    // With movement modes he follows a moving target: its velocity plus a
    // closing speed that falls to nothing as he reaches it (a still target:
    // arrive and stop), within his mode's speed. Otherwise he heads for it,
    // a target moving away closed on at its speed on top.
    const float followX =
        targetVelocityX[lane] + (aimed ? directionX : 0.0f) * closing;
    const float followY =
        targetVelocityY[lane] + (aimed ? directionY : 0.0f) * closing;
    const float followSpeed = std::sqrt(followX * followX + followY * followY);
    const float followScale =
        followSpeed > runCap ? runCap / followSpeed : 1.0f;
    const float receding =
        std::max(0.0f, aimed ? targetVelocityX[lane] * directionX +
                                   targetVelocityY[lane] * directionY
                             : 0.0f);
    const float headingSpeed = std::min(runCap, closing + receding);
    const float desiredSpeed =
        moded ? std::min(runCap, followSpeed) : headingSpeed;
    const float desiredX = moded ? followX * followScale
                                 : (aimed ? directionX : 0.0f) * headingSpeed;
    const float desiredY = moded ? followY * followScale
                                 : (aimed ? directionY : 0.0f) * headingSpeed;

    const float changeX = desiredX - oldX;
    const float changeY = desiredY - oldY;
    // From a standstill: push off straight toward the target.
    const float limit =
        (desiredSpeed >= speed ? std::max(driveHigh, 0.0f) : -limits.low) *
        dt;
    const float magnitude = std::sqrt(changeX * changeX + changeY * changeY);
    const float shortfall = limit / magnitude;
    const float scale = magnitude > limit ? shortfall : 1.0f;
    const float slowX = oldX + changeX * scale;
    const float slowY = oldY + changeY * scale;
    // On the move: accelerate or brake along the run, turn within the limit.
    const float tangentX = oldX / speed;
    const float tangentY = oldY / speed;
    const float normalX = -tangentY;
    const float normalY = tangentX;
    float along = changeX * tangentX + changeY * tangentY;
    float across = changeX * normalX + changeY * normalY;
    // Wanting more speed than he has (a tired man who cannot add it), he
    // turns onto the heading he wants at his own speed: the sideways part of
    // speed he cannot add would swing him past it, step after step.
    const float wantedAcross =
        (desiredX * normalX + desiredY * normalY) * speed /
        std::max(desiredSpeed, EPSILON);
    across = desiredSpeed > speed ? wantedAcross : across;
    along = std::clamp(along, limits.low * dt, driveHigh * dt);
    const float speedShare = std::clamp(speed / maxSpeed[lane], 0.0f, 1.0f);
    const float lateral =
        (P::LATERAL_ACCELERATION_SLOW +
         (P::LATERAL_ACCELERATION_FAST - P::LATERAL_ACCELERATION_SLOW) *
             speedShare) *
        grip[lane] * legs * dt;
    // Gliding through a body turn: no new push, and no steering either.
    across = gliding ? 0.0f : std::clamp(across, -lateral, lateral);
    const float fastX = oldX + (tangentX * along + normalX * across);
    const float fastY = oldY + (tangentY * along + normalY * across);
    // The run's own rule takes over gradually with speed (none at a
    // standstill, where the run has no direction).
    const float pivot = std::min(speed / PIVOT_BLEND_SPEED, 1.0f);
    const float runX = speed > 0.0f ? fastX : slowX;
    const float runY = speed > 0.0f ? fastY : slowY;
    const bool stop = (desiredSpeed == 0.0f) & (speed <= -limits.low * dt);
    float nextVelocityX = stop ? 0.0f : slowX + (runX - slowX) * pivot;
    float nextVelocityY = stop ? 0.0f : slowY + (runY - slowY) * pivot;
    const float squaredSpeed =
        nextVelocityX * nextVelocityX + nextVelocityY * nextVelocityY;
    const float capScale = topSpeed / std::sqrt(squaredSpeed);
    const bool capped = squaredSpeed > topSpeed * topSpeed;
    nextVelocityX = capped ? nextVelocityX * capScale : nextVelocityX;
    nextVelocityY = capped ? nextVelocityY * capScale : nextVelocityY;

    const float nextX = positionX[lane] + nextVelocityX * dt;
    const float nextY = positionY[lane] + nextVelocityY * dt;
    const PitchPoint clamped =
        moveBody(PitchPoint{positionX[lane], positionY[lane]}, PitchPoint{nextX, nextY});
    const float clampedX = clamped.x;
    const float clampedY = clamped.y;
    nextVelocityX = clampedX != nextX ? 0.0f : nextVelocityX;
    nextVelocityY = clampedY != nextY ? 0.0f : nextVelocityY;
    positionX[lane] = clampedX;
    positionY[lane] = clampedY;
    velocityX[lane] = nextVelocityX;
    velocityY[lane] = nextVelocityY;

    // His body's facing (a true angle)
    // turns at his turn rate toward the facing asked of him, else toward the
    // run; fastAtan2() without branches.
    const float y = nextVelocityY;
    const float x = nextVelocityX;
    const float absX = std::abs(x);
    const float absY = std::abs(y);
    const float ratio = std::min(absX, absY) / std::max(absX, absY);
    const float ratioSquared = ratio * ratio;
    float angle =
        ((-0.0464964749f * ratioSquared + 0.15931422f) * ratioSquared -
         0.327622764f) *
            ratioSquared * ratio +
        ratio;
    angle = absY > absX ? 1.57079637f - angle : angle;
    angle = x < 0.0f ? 3.14159274f - angle : angle;
    angle = y < 0.0f ? -angle : angle;
    angle = std::max(absX, absY) < 1e-12f ? 0.0f : angle;
    // Toward the facing asked of him, else his run.
    float difference = (bodyAsked ? bodyRequest[lane] : angle) - facing[lane];
    difference = difference > PI    ? difference - TWO_PI
                 : difference < -PI ? difference + TWO_PI
                                    : difference;
    const float turn = turnRate[lane] * dt;
    float turned = facing[lane] + std::clamp(difference, -turn, turn);
    turned = turned > PI    ? turned - TWO_PI
             : turned < -PI ? turned + TWO_PI
                            : turned;
    const bool running = squaredSpeed > P::MOVEMENT_FACING_THRESHOLD *
                                            P::MOVEMENT_FACING_THRESHOLD;
    nextHeading[lane] = angle;
    turning[lane] = running ? 1 : 0;
    nextFacing[lane] = running || bodyAsked ? turned : facing[lane];
  }

  for (std::size_t lane = 0; lane < moving; ++lane)
  {
    MatchPlayer& player = *movers[lane];
    player.velocity = {velocityX[lane], velocityY[lane]};
    player.position = PitchPoint{positionX[lane], positionY[lane]};
    player.facingAngle = nextFacing[lane];
    if (turning[lane] != 0) player.targetAngle = nextHeading[lane];
  }
}

[[gnu::always_inline]] inline void MatchEngine::stepKinematicsBinturong(
    MatchPlayer& player, Vector2F desired, float desiredSpeed, float topSpeed,
    float fatigue, float dt)
{
  using P = MatchTuning::Player;
  const float fuel = 1.0f - fatigue;
  // Acceleration-speed profile a(v) = A0 (1 - v / vmax), stronger braking,
  // and a lateral limit (scaled by his grip) that forces a player to slow
  // down to turn sharply. Tired legs push, brake and turn less sharply (the
  // fatigue tiers).
  const float legs = tieredMovement(fuel);
  const float freeShare = freeShareBinturong(player, carryingBinturong(player));
  const float acceleration = playerAcceleration(player, freeShare) * legs;
  const float braking = playerBraking(player, freeShare) * legs;
  Vector2F velocity = player.velocity;
  const float speed = length(velocity);
  const Vector2F change{desired.x - velocity.x, desired.y - velocity.y};
  // An emptying burst reserve holds his power to his critical speed's,
  // speeding up and braking alike: above his critical speed he then slows
  // whatever he wants.
  const float fullness = std::clamp(
      player.sprintReserve / std::max(tieredBurstCapacity(fuel), EPSILON),
      0.0f, 1.0f);
  const float transition =
      MatchRules::walkRunTransitionSpeed(player.heightMetres);
  const AccelerationRange limits = burstLimits(
      acceleration * std::max(0.05f, 1.0f - speed / topSpeed), braking, speed,
      fullness,
      tieredCriticalSpeed(
          derivedPhysicalValue(DerivedStat::ENDURANCE,
                               player.derivedStat(DerivedStat::ENDURANCE)),
          fuel, transition),
      transition);
  if (desiredSpeed == 0.0f && speed <= -limits.low * dt)
  {
    velocity = {0.0f, 0.0f};
  }
  else
  {
    // From a standstill: push off straight toward the target.
    const float limit =
        (desiredSpeed >= speed ? std::max(limits.high, 0.0f) : -limits.low) *
        dt;
    const float magnitude = length(change);
    const float shortfall = limit / magnitude;
    const float scale = magnitude > limit ? shortfall : 1.0f;
    const Vector2F pushOff{velocity.x + change.x * scale,
                           velocity.y + change.y * scale};
    Vector2F run = pushOff;
    if (speed > 0.0f)
    {
      // On the move: accelerate or brake along the run, turn within the
      // limit.
      const Vector2F tangent{velocity.x / speed, velocity.y / speed};
      const Vector2F normal{-tangent.y, tangent.x};
      float along = change.x * tangent.x + change.y * tangent.y;
      float across = change.x * normal.x + change.y * normal.y;
      along = std::clamp(along, limits.low * dt, limits.high * dt);
      const float speedShare = std::clamp(speed / player.maxSpeed, 0.0f, 1.0f);
      const float lateral =
          (P::LATERAL_ACCELERATION_SLOW +
           (P::LATERAL_ACCELERATION_FAST - P::LATERAL_ACCELERATION_SLOW) *
               speedShare) *
          playerGrip(player, freeShare) * legs * dt;
      across = std::clamp(across, -lateral, lateral);
      run = {velocity.x + tangent.x * along + normal.x * across,
             velocity.y + tangent.y * along + normal.y * across};
    }
    // The run's own rule takes over gradually with speed.
    const float pivot = std::min(speed / PIVOT_BLEND_SPEED, 1.0f);
    velocity = {pushOff.x + (run.x - pushOff.x) * pivot,
                pushOff.y + (run.y - pushOff.y) * pivot};
  }
  const float squaredSpeed = velocity.x * velocity.x + velocity.y * velocity.y;
  if (squaredSpeed > topSpeed * topSpeed)
  {
    const float scale = topSpeed / std::sqrt(squaredSpeed);
    velocity.x *= scale;
    velocity.y *= scale;
  }
  player.velocity = velocity;

  const Vector2F step = {velocity.x * dt, velocity.y * dt};
  const float nextX = player.position.x + step.x;
  const float nextY = player.position.y + step.y;
  player.position = moveBody(player.position, PitchPoint{nextX, nextY});
  if (player.position.x != nextX) player.velocity.x = 0.0f;
  if (player.position.y != nextY) player.velocity.y = 0.0f;

  if (squaredSpeed >
      P::MOVEMENT_FACING_THRESHOLD * P::MOVEMENT_FACING_THRESHOLD)
  {
    // Facing is a true angle on the ground.
    player.targetAngle = fastAtan2(player.velocity.y,
                                   player.velocity.x);
    float angleDifference = player.targetAngle - player.facingAngle;
    if (angleDifference > std::numbers::pi_v<float>)
      angleDifference -= TWO_PI;
    else if (angleDifference < -std::numbers::pi_v<float>)
      angleDifference += TWO_PI;
    player.facingAngle += std::clamp(angleDifference, -player.turnRate * dt,
                                     player.turnRate * dt);
    if (player.facingAngle > std::numbers::pi_v<float>)
      player.facingAngle -= TWO_PI;
    else if (player.facingAngle < -std::numbers::pi_v<float>)
      player.facingAngle += TWO_PI;
  }
}

void MatchEngine::spendEnergyBinturong(FitnessLanes& lanes, std::size_t begin,
                                       std::size_t end, float dt,
                                       bool /*homeTeam*/) const
{
  // Pressing costs what its running costs: the pressing and side terms of
  // the old drain are not read.
  auto& [fromX, fromY, toX, toY, topSpeed, endurance, pressingEffort, sideDrain,
         stamina, reserve, stepMetres, nextStamina, nextReserve,
         stepEnergy, stepAnaerobicEnergy, stepBrakingEnergy,
         stepBrakingAnaerobicEnergy, fastDebt, nextFastDebt] = lanes;
  // The endurance stats, lane by lane (filled with these lanes).
  const EnduranceLanes& physiology = enduranceLanes;
  for (std::size_t lane = begin; lane < end; ++lane)
  {
    const float metresX = fromX[lane] - toX[lane];
    const float metresY = fromY[lane] - toY[lane];
    const float metres = std::sqrt(metresX * metresX + metresY * metresY);
    const float speed = metres / dt;
    stepMetres[lane] = metres;
    const float fuel = stamina[lane];
    const float fresh = physiology.criticalSpeed[lane];
    const float transition = physiology.transitionSpeed[lane];
    const float critical = tieredCriticalSpeed(fresh, fuel, transition);
    // Fuel: the carbohydrate power of this period's movement against his
    // anchor's (FUEL_ANCHOR_SHARE of his fresh critical speed, which a full
    // tank lasts his fuel capacity).
    const float acceleration =
        std::clamp((speed - physiology.previousSpeed[lane]) / dt,
                   -MAX_LOAD_ACCELERATION, MAX_LOAD_ACCELERATION);
    const float anchor = carbohydratePower(
        metabolicPower(FUEL_ANCHOR_SHARE * fresh, 0.0f, transition),
        metabolicPower(fresh, 0.0f, transition));
    // Moving sideways or backward to his body costs more (orientation plan).
    const float power = metabolicPower(speed, acceleration, transition) *
                        physiology.directionCost[lane];
    const float criticalPower =
        std::max(metabolicPower(critical, 0.0f, transition), EPSILON);
    // Burst reserve (a share of the fresh burst capacity): energy, the burst
    // capacity's metres at the running cost of his fresh critical speed.
    // Power above his critical speed's, from running fast or accelerating,
    // is drawn from it; below, it refills toward what the fuel left allows,
    // slower the harder he still works.
    const float burstEnergy = std::max(
        physiology.burstMetres[lane] *
            runningCost(AIR_COST_K * fresh * fresh / GRAVITY),
        EPSILON);
    const float capacity = tieredBurstCapacity(fuel);
    const float above = std::max(0.0f, power - criticalPower);
    const float drawn = std::min(reserve[lane], above * dt / burstEnergy);
    // The shortfall refills in two parallel phases (Chorley 2022): a share
    // of every spend on the fast track, the rest slowly; both slower the
    // harder he still works.
    const float shortfall = std::max(0.0f, capacity - reserve[lane]);
    const float fast = std::min(fastDebt[lane], shortfall);
    const float intensity = std::min(power / criticalPower, 1.0f);
    const float slowing = 1.0f + RECOVERY_EFFORT_SLOWING * intensity * intensity;
    const float fastRepaid =
        fast * (1.0f - std::exp(-dt / (physiology.fastRecoverySeconds[lane] *
                                       slowing)));
    // Fuel fatigue slows only the slow phase (the fatigue tiers).
    const float slowRepaid =
        (shortfall - fast) *
        (1.0f - std::exp(-dt / (physiology.recoverySeconds[lane] * slowing *
                                (1.0f + tierLoss(SLOW_REFILL_SLOWING, fuel)))));
    const bool spending = above > 0.0f;
    const float next = std::min(
        spending ? reserve[lane] - drawn
                 : reserve[lane] + fastRepaid + slowRepaid,
        capacity);
    const float nextFast =
        spending ? fast + FAST_REFILL_SHARE * drawn : fast - fastRepaid;
    // A restart's repositioning jump is not running.
    const bool ran = speed <= MatchTuning::Player::MAX_COUNTED_STEP_SPEED;
    nextReserve[lane] = ran ? next : reserve[lane];
    nextFastDebt[lane] =
        ran ? std::clamp(nextFast, 0.0f, std::max(0.0f, capacity - next))
            : fastDebt[lane];
    // Fuel: the aerobic part at its carbohydrate share, and what was drawn
    // from the burst reserve at the anaerobic premium; against his anchor.
    // (With the reserve empty, power above his critical speed's is not yet
    // charged: the fatigue plan's step 4 makes an empty reserve a ceiling.)
    const float aerobic = std::min(power, criticalPower);
    const float carbohydrate =
        aerobic * carbohydrateShare(aerobic / criticalPower) +
        drawn * burstEnergy / dt * ANAEROBIC_FUEL_PREMIUM;
    const float drain =
        carbohydrate / std::max(anchor * physiology.fuelSeconds[lane], EPSILON);
    nextStamina[lane] = std::clamp(stamina[lane] - drain * dt, 0.0f, 1.0f);
    stepEnergy[lane] = power * dt;
    stepAnaerobicEnergy[lane] = above * dt;
    const bool decelerating = acceleration < 0.0f;
    stepBrakingEnergy[lane] = decelerating ? power * dt : 0.0f;
    stepBrakingAnaerobicEnergy[lane] = decelerating ? above * dt : 0.0f;
  }
}

bool MatchEngine::updateDribbleBinturong(MatchPlayer& carrier, float dt)
{
  using R = MatchTuning::Dribble;
  const bool inHands = carrier.isGoalkeeper &&
                       inPenaltyArea(carrier.position, carrier.isHomeTeam);
  const float speed = length(carrier.velocity);
  if (!carrier.isGoalkeeper)
  {
    // An outfielder's ball is never tethered: however he came by it (a
    // restart, a tackle, a drill), it is loose at his feet from here, moving
    // with him, and he plays it on touch by touch (the ball's sweep).
    const Vector2F ahead =
        speed > EPSILON
            ? Vector2F{carrier.velocity.x / speed, carrier.velocity.y / speed}
            : ball.dribbleDirection;
    const Vector2F offset =
        {ahead.x * R::FEET_METRES, ahead.y * R::FEET_METRES};
    ball.position = PitchPoint{std::clamp(carrier.position.x + offset.x, pitch.atX(0.001f), pitch.atX(0.999f)),
                     std::clamp(carrier.position.y + offset.y, pitch.atY(0.001f), pitch.atY(0.999f))};
    ball.z = 0.0f;
    ball.velocity = carrier.velocity;
    beginCollectedDribble(carrier);
    return true;
  }
  if (inHands && isKeeperHoldingBall())
  {
    // In his hands (goalkeeper plan step 5): in a dive, between his hands
    // as the dive moves them; else brought to his chest and held there.
    ball.dribbleExposure = 0.0f;
    ball.dribbleTouchLength = 0.0f;
    if (keeperDiving(carrier))
    {
      const KeeperDivePose pose = keeperDivePoseBinturong(
          carrier, keepers[carrier.isHomeTeam ? 0 : 1].dive, getSimulatedSeconds());
      const Vector2F centre = asVector(carrier.position);
      rebaseKeeperHold({0.5f * (pose.hands[0].x + pose.hands[1].x) - centre.x,
                        0.5f * (pose.hands[0].y + pose.hands[1].y) - centre.y,
                        std::max(0.5f * (pose.hands[0].z + pose.hands[1].z),
                                 MatchTuning::Units::BALL_RADIUS_METRES)});
    }
    const Point3F held = heldBallBinturong(carrier);
    ball.position = PitchPoint{std::clamp(asPoint({held.x, held.y}).x, pitch.atX(0.001f), pitch.atX(0.999f)),
                     std::clamp(asPoint({held.x, held.y}).y, pitch.atY(0.001f), pitch.atY(0.999f))};
    ball.z = held.z;
    ball.velocity = carrier.velocity;
    ball.velocityZ = 0.0f;
    return true;
  }
  // Out of his area with it in his hands: at his feet from here.
  endKeeperHold();
  if (inHands)
  {
    ball.dribbleExposure = 0.0f;
    ball.dribbleTouchLength = 0.0f;
  }
  else if (speed < R::CLOSE_CONTROL_SPEED)
  {
    // Close control: the ball is kept at the feet, shielded by the body.
    ball.dribbleExposure = R::FEET_METRES;
    ball.dribbleTouchLength = 0.0f;
    ball.touchTimer = 0.0f;
    if (speed > EPSILON)
      ball.dribbleDirection = {carrier.velocity.x / speed,
                               carrier.velocity.y / speed};
  }
  else
  {
    ball.touchTimer -= dt;
    if (ball.touchTimer <= 0.0f)
    {
      ++dribbleTouches;
      // A touch pushes the ball ahead along the run; faster runs and poorer
      // dribblers push it further, and pressure risks a heavy touch.
      ball.dribbleDirection = {carrier.velocity.x / speed,
                               carrier.velocity.y / speed};
      const float pressure =
          std::clamp(1.0f - nearestOpponentDistance(carrier) /
                                R::HEAVY_TOUCH_PRESSURE_METRES,
                     0.0f, 1.0f);
      const float heavyChance = R::HEAVY_TOUCH_BASE *
                                (1.0f - carrier.dribbling) *
                                std::min(1.0f, speed / carrier.maxSpeed) *
                                (1.0f + pressure * R::HEAVY_TOUCH_PRESSURE) *
                                executionErrorScaleBinturong(carrier);
      if (randomFloat(0.0f, 1.0f) < heavyChance)
      {
        const float push = speed + randomFloat(R::MIN_HEAVY_PUSH_SPEED,
                                               R::MAX_HEAVY_PUSH_SPEED);
        const PitchPoint origin = ball.position;
        launchBall(carrier, BallRelease{origin, 0.0f, 0.0f},
                   // A far point that way, the direction taken in fractions (as it was).
                   PitchPoint{origin.x + pitch.atX(ball.dribbleDirection.x),
                              origin.y + pitch.atY(ball.dribbleDirection.y)},
                   push, 0.0f, BallSpin{});
        ball.velocity = {ball.dribbleDirection.x * push,
                         ball.dribbleDirection.y * push};
        return false;
      }
      trackTouch(carrier);
      ball.dribbleTouchLength =
          std::clamp(R::TOUCH_BASE_METRES + speed * R::TOUCH_METRES_PER_SPEED *
                                                (1.3f - carrier.dribbling),
                     R::MIN_TOUCH_METRES, R::MAX_TOUCH_METRES);
      ball.dribbleExposure = ball.dribbleTouchLength;
      ball.touchTimer = std::clamp(
          R::TOUCH_INTERVAL_BASE - speed * R::TOUCH_INTERVAL_PER_SPEED -
              carrier.dribbling * R::TOUCH_INTERVAL_DRIBBLING,
          R::MIN_TOUCH_INTERVAL, R::MAX_TOUCH_INTERVAL);
    }
    else
    {
      // The carrier closes on the ball until the next touch.
      ball.dribbleExposure =
          R::FEET_METRES + (ball.dribbleExposure - R::FEET_METRES) *
                               (ball.touchTimer / (ball.touchTimer + dt));
    }
  }
  const Vector2F ahead =
      {ball.dribbleDirection.x * ball.dribbleExposure,
               ball.dribbleDirection.y * ball.dribbleExposure};
  ball.position = PitchPoint{std::clamp(carrier.position.x + ahead.x, pitch.atX(0.001f), pitch.atX(0.999f)),
                   std::clamp(carrier.position.y + ahead.y, pitch.atY(0.001f), pitch.atY(0.999f))};
  ball.z = 0.0f;
  ball.velocity = carrier.velocity;
  return true;
}

TouchResult MatchEngine::dribbleTouchBinturong(const MatchPlayer& player,
                                               const TouchIntent& intent,
                                               const TouchMoment& moment)
{
  // A dribble touch is a first touch on a ball he already has: the same two
  // jobs, carried out with his Dribbling (contactBinturong).
  return firstTouchBinturong(player, intent, moment, true);
}
ContactProfile MatchEngine::touchProfileBinturong() const
{
  ContactProfile profile = touchContactProfile(touchKnobs);
  profile.stretchMap = contactStretchMap;
  return profile;
}

ContactProfile MatchEngine::bodyTouchProfileBinturong(bool thigh) const
{
  // The touch's, but no foot is weaker and it is never off the ground for a
  // chest or thigh; never sent down, nor off a thigh flatter than its
  // lowest.
  ContactProfile profile = touchProfileBinturong();
  profile.weakerFoot = {};
  profile.weakerFootErrorGain = 0.0f;
  profile.offGround = {};
  profile.lowestElevation =
      thigh ? MatchTuning::Ball::THIGH_MIN_UP_DEGREES * std::numbers::pi_v<float> / 180.0f
            : 0.0f;
  return profile;
}

ContactProfile touchContactProfile(const FirstTouchKnobs& knob)
{
  using K = TouchKnob;
  ContactProfile profile;
  profile.paceChangeWeight = knob[K::PACE_CHANGE_WEIGHT];
  profile.turnAngle = knob[K::TURN_ANGLE];
  profile.turnOutward = knob[K::TURN_OUTWARD];
  profile.turnInward = knob[K::TURN_INWARD];
  profile.verticalTurnAngle = knob[K::VERTICAL_TURN_ANGLE];
  profile.carriesOutVertical = true;
  profile.incomingPace = {knob[K::INCOMING_SPEED_ANGLE],
                          knob[K::INCOMING_SPEED_WEIGHT]};
  profile.approach = {knob[K::APPROACH_ANGLE], knob[K::APPROACH_WEIGHT]};
  profile.ownSpeed = {knob[K::OWN_MOVEMENT_ANGLE], knob[K::OWN_MOVEMENT_WEIGHT]};
  profile.stretch = {knob[K::STRETCH_ANGLE], knob[K::STRETCH_WEIGHT]};
  profile.weakerFoot = {knob[K::WEAK_FOOT_ANGLE], knob[K::WEAK_FOOT_WEIGHT]};
  profile.offGround = {knob[K::OFF_GROUND_ANGLE], knob[K::OFF_GROUND_WEIGHT]};
  profile.surprise = {knob[K::SURPRISE_ANGLE], knob[K::SURPRISE_WEIGHT]};
  profile.pressure = {knob[K::PRESSURE_ANGLE], knob[K::PRESSURE_WEIGHT]};
  profile.referenceSpeed = knob[K::REFERENCE_SPEED];
  profile.pressureRadius = knob[K::PRESSURE_RADIUS];
  profile.comfortGap = knob[K::COMFORT_GAP];
  profile.skillReduction = knob[K::SKILL_REDUCTION];
  profile.fatigueErrorGain = knob[K::FATIGUE_ERROR_GAIN];
  profile.weakerFootErrorGain = knob[K::WEAKER_FOOT_ERROR_GAIN];
  profile.angleFailBase = knob[K::ANGLE_FAIL_BASE];
  profile.angleFailRange = knob[K::ANGLE_FAIL_RANGE];
  profile.weightFailBase = knob[K::WEIGHT_FAIL_BASE];
  profile.weightFailRange = knob[K::WEIGHT_FAIL_RANGE];
  profile.angleMissBase = knob[K::ANGLE_MISS_BASE];
  profile.angleMissRange = knob[K::ANGLE_MISS_RANGE];
  profile.weightMissBase = knob[K::WEIGHT_MISS_BASE];
  profile.weightMissRange = knob[K::WEIGHT_MISS_RANGE];
  profile.underTurnShare = knob[K::UNDER_TURN_SHARE];
  profile.heavyShare = knob[K::HEAVY_SHARE];
  profile.angleJitter = knob[K::ANGLE_JITTER];
  profile.weightJitter = knob[K::WEIGHT_JITTER];
  profile.reboundShare = knob[K::REBOUND_SHARE];
  return profile;
}

ContactProfile MatchEngine::strikeProfileBinturong(StrikeKind kind) const
{
  ContactProfile profile = strikeContactProfile(strikeKnobs, kind, touchKnobs);
  // A header keeps the head's own map (strikeContactProfile).
  if (kind != StrikeKind::HEADER) profile.stretchMap = contactStretchMap;
  return profile;
}

ContactProfile strikeContactProfile(const StrikeKnobs& strikeKnobs,
                                    StrikeKind kind,
                                    const FirstTouchKnobs& touchKnobs)
{
  // Contact plan §3, from the strike knobs (match_strike_knobs.h): ground
  // passes are carried out; lofted balls and shots are still only
  // measured, with provisional weights.
  using K = StrikeKnob;
  const auto knob = [&](StrikeKnob id) { return strikeKnobs.at(kind, id); };
  const auto weights = [&](StrikeKnob angle, StrikeKnob weight)
  { return ContactProfile::Weights{knob(angle), knob(weight)}; };
  ContactProfile profile;
  profile.paceChangeWeight = knob(K::PACE_CHANGE_WEIGHT);
  profile.turnAngle = knob(K::TURN_ANGLE);
  profile.turnPaceReference = knob(K::TURN_PACE_REFERENCE);
  profile.verticalTurnAngle = knob(K::VERTICAL_TURN_ANGLE);
  profile.incomingPace =
      weights(K::INCOMING_PACE_ANGLE, K::INCOMING_PACE_WEIGHT);
  profile.ownSpeed = weights(K::OWN_SPEED_ANGLE, K::OWN_SPEED_WEIGHT);
  profile.stretch = weights(K::STRETCH_ANGLE, K::STRETCH_WEIGHT);
  profile.weakerFoot = weights(K::WEAK_FOOT_ANGLE, K::WEAK_FOOT_WEIGHT);
  profile.offGround = weights(K::OFF_GROUND_ANGLE, K::OFF_GROUND_WEIGHT);
  profile.pressure = weights(K::PRESSURE_ANGLE, K::PRESSURE_WEIGHT);
  profile.approach = weights(K::APPROACH_ANGLE, K::APPROACH_WEIGHT);
  profile.spinAsked = weights(K::SPIN_ASKED_ANGLE, K::SPIN_ASKED_WEIGHT);
  profile.spinAskedSpin = knob(K::SPIN_ASKED_SPIN);
  profile.powerAsked = weights(K::POWER_ASKED_ANGLE, K::POWER_ASKED_WEIGHT);
  profile.powerComfortShare = knob(K::POWER_COMFORT_SHARE);
  profile.powerInwardSquare = knob(K::POWER_INWARD_SQUARE);
  profile.powerOutwardSquare = knob(K::POWER_OUTWARD_SQUARE);
  profile.powerBehind = knob(K::POWER_BEHIND);
  profile.referenceSpeed = knob(K::REFERENCE_SPEED);
  profile.pressureRadius = knob(K::PRESSURE_RADIUS);
  // Where in his reach he meets it is measured as for the touch; a header
  // against the head's reach (heading technique plan step 4).
  profile.comfortGap = touchKnobs[TouchKnob::COMFORT_GAP];
  if (kind == StrikeKind::HEADER)
  {
    using A = MatchTuning::Aerial;
    profile.comfortGap = A::HEAD_COMFORT_GAP_METRES;
    profile.stretchMap.distances = A::HEAD_STRETCH_DISTANCES;
    profile.stretchMap.values = A::HEAD_STRETCH_VALUES;
  }
  // Tiredness, and his weaker foot when he uses it, as for every strike.
  profile.fatigueErrorGain = MatchTuning::Fatigue::SPENT_TECHNIQUE_ERROR;
  profile.weakerFootErrorGain = MatchTuning::Player::WEAKER_FOOT_ERROR_GAIN;
  profile.skillReduction = knob(K::SKILL_REDUCTION);
  profile.angleFailBase = knob(K::ANGLE_FAIL_BASE);
  profile.angleFailRange = knob(K::ANGLE_FAIL_RANGE);
  profile.weightFailBase = knob(K::WEIGHT_FAIL_BASE);
  profile.weightFailRange = knob(K::WEIGHT_FAIL_RANGE);
  profile.angleMissBase = knob(K::ANGLE_MISS_BASE);
  profile.angleMissRange = knob(K::ANGLE_MISS_RANGE);
  profile.angleMissAbsoluteBase = knob(K::ANGLE_MISS_ABSOLUTE_BASE);
  profile.angleMissAbsoluteRange = knob(K::ANGLE_MISS_ABSOLUTE_RANGE);
  profile.weightMissBase = knob(K::WEIGHT_MISS_BASE);
  profile.weightMissRange = knob(K::WEIGHT_MISS_RANGE);
  profile.underTurnShare = knob(K::UNDER_TURN_SHARE);
  profile.heavyShare = knob(K::HEAVY_SHARE);
  profile.angleJitter = knob(K::ANGLE_JITTER);
  profile.jitterDifficultyShare = knob(K::JITTER_DIFFICULTY_SHARE);
  profile.weightJitter = knob(K::WEIGHT_JITTER);
  profile.verticalShare = knob(K::VERTICAL_SHARE);
  profile.upShare = knob(K::UP_SHARE);
  profile.elevationJitter = knob(K::ELEVATION_JITTER);
  profile.spinFailBase = knob(K::SPIN_FAIL_BASE);
  profile.spinFailRange = knob(K::SPIN_FAIL_RANGE);
  profile.spinMissBase = knob(K::SPIN_MISS_BASE);
  profile.spinMissRange = knob(K::SPIN_MISS_RANGE);
  profile.underSpinShare = knob(K::UNDER_SPIN_SHARE);
  profile.spinJitter = knob(K::SPIN_JITTER);
  return profile;
}

TouchMoment MatchEngine::strikeMomentBinturong(const MatchPlayer& striker) const
{
  // Where he meets it in the ball's sweep, when he does (a loose ball at
  // his feet: unified kick plan step 3).
  if (strikeAt) return *strikeAt;
  // Otherwise (a dead ball, a keeper's from his hands, or the brain looking
  // ahead): the ball where it lies, how far from his centre against his
  // feet's reach (as for a touch), and its height.
  TouchMoment moment;
  moment.contactPoint = ball.position;
  moment.gap = distance(striker.position, ball.position);
  moment.reach = feetReachBinturong(striker);
  moment.height = ball.z;
  return moment;
}

ContactAssessment MatchEngine::assessStrikeBinturong(
    const MatchPlayer& striker, const ContactProfile& profile, Vector2F aim,
    float speed, float lift) const
{
  // He chose to strike it. Strikes have no intent from the brain yet: the
  // body reads the pressure (vertical contact plan step 4).
  return assessContactBinturong(
      striker, profile, aim, speed, lift, strikeMomentBinturong(striker), 0.0f,
      contactPressure(nearestOpponentDistance(striker), profile.pressureRadius));
}

ContactResult MatchEngine::contactBinturong(const MatchPlayer& player,
                                            const ContactRequest& asked,
                                            const TouchMoment& moment)
{
  // Every contact the same way (unified kick plan step 1): the kind's
  // profile, assessed where he meets the ball, carried out with his skill
  // for it, and reported. A header within what his head can do there.
  ContactRequest request = asked;
  const bool strike = request.kind == ContactRequest::Kind::STRIKE;
  ContactProfile profile =
      strike ? strikeProfileBinturong(request.strike)
      : request.kind == ContactRequest::Kind::CHEST_TOUCH
          ? bodyTouchProfileBinturong(false)
      : request.kind == ContactRequest::Kind::THIGH_TOUCH
          ? bodyTouchProfileBinturong(true)
          : touchProfileBinturong();
  if (strike && request.strike == StrikeKind::HEADER)
    headerLimitsBinturong(player, moment, request, profile);
  if (request.precision != 1.0f)
  {
    profile.angleJitter *= request.precision;
    profile.angleMissAbsoluteBase *= request.precision;
    profile.angleMissAbsoluteRange *= request.precision;
  }
  // Without the brain's read of the pressure, the body reads it.
  const float pressure = request.pressure.value_or(contactPressure(
      nearestOpponentDistance(player), profile.pressureRadius));
  // A strike asks for some of his power for the kind, less what turning a
  // moving ball costs him (a touch: none; a throw: its own limit; a header:
  // the fastest the ball can come off his head that way, before his body's
  // angle, which the condition takes off as for a foot).
  const float powerCapacity =
      !strike                        ? 0.0f
      : request.powerCapacity > 0.0f ? request.powerCapacity
      : request.strike == StrikeKind::THROW
          ? strikePowerBinturong(player, request.strike)
      : request.strike == StrikeKind::HEADER
          ? headerCeilingBinturong(player, request.aim,
                                   std::atan2(request.lift, request.speed), false)
          : incomingPowerBudgetBinturong(
                strikePowerBinturong(player, request.strike), request.aim);
  ContactResult result;
  result.contact =
      assessContactBinturong(player, profile, request.aim, request.speed,
                             request.lift, moment, request.surprise, pressure,
                             request.spin, request.spinCapacity, powerCapacity);
  // The least it can leave him with: the nearer he asks to it, the harder
  // the weight job (heading technique plan step 6).
  if (request.paceFloor > 0.0f)
    result.contact.weightDifficulty = std::max(
        result.contact.weightDifficulty,
        std::clamp(1.0f - (result.contact.intendedSpeed - request.paceFloor) /
                              request.paceFloor,
                   0.0f, 1.0f));
  // A touch on receiving the ball is his First Touch; a touch on his own
  // ball, dribbling it, is his Dribbling.
  result.skill = strike ? strikeSkillBinturong(player, request.strike)
                 : request.kind == ContactRequest::Kind::DRIBBLE_TOUCH
                     ? player.dribbling
                 : request.kind == ContactRequest::Kind::BLOCK
                     ? player.derivedStat(DerivedStat::TACKLING)
                     : player.derivedStat(DerivedStat::FIRST_TOUCH);
  result.outcome =
      carryOutContactBinturong(player, profile, result.contact, result.skill);
  // ...and the error can't take it below that: a light miss comes off at it.
  if (ContactOutcome& outcome = result.outcome;
      request.paceFloor > 0.0f && outcome.actualSpeed < request.paceFloor)
  {
    const float scale =
        outcome.actualSpeed > EPSILON ? request.paceFloor / outcome.actualSpeed : 0.0f;
    if (scale > 0.0f)
    {
      outcome.velocity = {outcome.velocity.x * scale, outcome.velocity.y * scale};
      outcome.verticalSpeed *= scale;
    }
    else
    {
      const float along = request.paceFloor * std::cos(outcome.achievedElevation);
      outcome.velocity = {request.aim.x * along, request.aim.y * along};
      outcome.verticalSpeed =
          request.paceFloor * std::sin(outcome.achievedElevation);
    }
    outcome.actualSpeed = request.paceFloor;
  }
  // ...nor above the most it may leave with (it adds no pace).
  if (ContactOutcome& outcome = result.outcome;
      request.paceCeiling > 0.0f && outcome.actualSpeed > request.paceCeiling)
  {
    const float scale = request.paceCeiling / outcome.actualSpeed;
    outcome.velocity = {outcome.velocity.x * scale, outcome.velocity.y * scale};
    outcome.verticalSpeed *= scale;
    outcome.actualSpeed = request.paceCeiling;
  }
  reportContactBinturong(player, request, result);
  return result;
}

void MatchEngine::reportContactBinturong(const MatchPlayer& player,
                                         const ContactRequest& request,
                                         const ContactResult& result)
{
  const ContactAssessment& contact = result.contact;
  const ContactOutcome& outcome = result.outcome;
  const bool strike = request.kind == ContactRequest::Kind::STRIKE;
  // Strikes are tallied in open play (and throw-ins, a throw's only
  // restart): a dead ball is struck from a standstill.
  if (strike &&
      (state == MatchState::PLAYING || request.strike == StrikeKind::THROW))
  {
    StrikeContactStats::ByKind& tally =
        strikeContacts.byKind[static_cast<std::size_t>(request.strike)];
    ++tally.strikes;
    tally.angleDifficulty += contact.angleDifficulty;
    tally.weightDifficulty += contact.weightDifficulty;
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
    {
      const float value = contact.factors[index].value;
      tally.factorSum[index] += value;
      if (value >= 0.5f) ++tally.factorMarked[index];
    }
    ++tally.carriedOut;
    if (outcome.angleFailed) ++tally.angleFailed;
    if (outcome.weightFailed) ++tally.weightFailed;
    tally.angleErrorSum += std::abs(outcome.achievedTurn - contact.turn);
    tally.weightErrorSum += outcome.actualSpeed - contact.intendedSpeed;
    tally.elevationErrorSum +=
        outcome.achievedElevation - contact.intendedElevation;
  }

  if (!wantsDetail()) return;
  MatchDuelDetail duel;
  duel.kind = strike ? MatchDuelDetail::Kind::STRIKE
                     : MatchDuelDetail::Kind::FIRST_TOUCH;
  if (strike)
  {
    duel.strike = request.strike;
    duel.strikeCarriedOut = true;
  }
  duel.player = player.player ? player.player->getId() : 0;
  duel.homeTeam = player.isHomeTeam;
  duel.chance.total = contact.angleDifficulty;
  duel.foul.total = contact.weightDifficulty;
  for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
  {
    const ContactFactor& factor = contact.factors[index];
    if (factor.angleWeight != 0.0f)
      duel.chance.add(factor.name, factor.source, contact.added(index, ContactJob::ANGLE));
    if (factor.weightWeight != 0.0f)
      duel.foul.add(factor.name, factor.source, contact.added(index, ContactJob::WEIGHT));
  }
  for (ScoreBreakdown* breakdown : {&duel.chance, &duel.foul})
    if (const float clamp = breakdown->residual(); std::abs(clamp) > 1e-6f)
      breakdown->add("clamped to its range", TermSource::SITUATION, clamp);
  duel.touchSkill = result.skill;
  duel.intendedSpeed = contact.intendedSpeed;
  duel.actualSpeed = outcome.actualSpeed;
  duel.turn = std::abs(contact.turn);
  duel.outward = contact.outward && std::abs(contact.turn) > EPSILON;
  duel.paceChange = contact.paceChange;
  // Signed along the turn: - short of it, + past it.
  duel.angleError = (outcome.achievedTurn - contact.turn) *
                    (contact.turn < 0.0f ? -1.0f : 1.0f);
  duel.angleFailChance = outcome.angleFailChance;
  duel.angleFailed = outcome.angleFailed;
  duel.weightError = outcome.actualSpeed - contact.intendedSpeed;
  duel.weightFailChance = outcome.weightFailChance;
  duel.weightFailed = outcome.weightFailed;
  if (MatchRecorder* recorder = getRecorder())
    recorder->onDuelDetail(*this, duel);
}

float MatchEngine::strikeSkillBinturong(const MatchPlayer& striker,
                                        StrikeKind kind) const
{
  switch (kind)
  {
    case StrikeKind::SHOT:
    case StrikeKind::PLACED_SHOT:
    case StrikeKind::CURLED_SHOT:
    case StrikeKind::CHIP_SHOT:
    case StrikeKind::TOE_POKE:
      return striker.shooting;
    case StrikeKind::HEADER:
      // Heading (heading technique plan step 5).
      return striker.derivedStat(DerivedStat::HEADING);
    case StrikeKind::CLEARANCE:
      return striker.defending;
    case StrikeKind::THROW:
      // A keeper's roll or throw from his hands: Throwing (goalkeeper plan
      // step 5); a throw-in, Passing.
      if (getPossessionState().heldRelease == HeldRelease::ROLL || getPossessionState().heldRelease == HeldRelease::OVERARM)
        return striker.derivedStat(DerivedStat::THROWING);
      break;
    case StrikeKind::GROUND_PASS:
    case StrikeKind::LOFTED_PASS:
    case StrikeKind::CROSS:
    case StrikeKind::DRIVEN_PASS:
    case StrikeKind::CHIP:
      break;
  }
  return striker.passing;
}

MatchEngine::Kick MatchEngine::strikeContactBinturong(
    const MatchPlayer& striker, StrikeKind kind, PitchPoint target, float speed,
    float lift, float precision, BallSpin spin, float spinCapacity)
{
  // What he means: from the ball where it lies towards `target`, at `speed`
  // over the ground and `lift` upward. Struck with the kind's profile (its
  // angle error scaled by `precision`) and his skill for it; the ball
  // leaves as it came off him.
  const PitchPoint origin = ball.position;
  const Vector2F aim = metricDirection(origin, target);
  ContactRequest request;
  request.kind = ContactRequest::Kind::STRIKE;
  request.strike = kind;
  request.aim = aim;
  request.speed = speed;
  request.lift = lift;
  request.precision = precision;
  request.spin = spin;
  request.spinCapacity = spinCapacity;
  const ContactResult result =
      contactBinturong(striker, request, strikeMomentBinturong(striker));
  const ContactOutcome& outcome = result.outcome;
  const float along = length(outcome.velocity);
  const Vector2F heading =
      along > EPSILON
          ? Vector2F{outcome.velocity.x / along, outcome.velocity.y / along}
          : aim;
  // The target only gives the direction: as far away as the one he meant,
  // and not pulled back onto the pitch, which would bend it.
  const float metres = std::max(distance(origin, target), 1.0f);
  const Vector2F reach = {heading.x * metres, heading.y * metres};
  Kick kick;
  kick.target = PitchPoint{origin.x + reach.x, origin.y + reach.y};
  kick.speed = along;
  kick.verticalSpeed = outcome.verticalSpeed;
  kick.spin = outcome.achievedSpin;
  // Which foot: his weaker one if the contact put the ball on that side.
  const bool weaker = result.contact.factors[WEAK_FOOT_FACTOR].value >= 0.5f;
  const bool rightFooted =
      !striker.player || striker.player->getFoot() == Foot::Right;
  kick.leftFoot = rightFooted == weaker;
  return kick;
}

float MatchEngine::strikePowerBinturong(const MatchPlayer& striker,
                                        StrikeKind kind) const
{
  using Sh = MatchTuning::Shooting;
  switch (kind)
  {
    case StrikeKind::SHOT:
      return shotPowerBinturong(striker);
    case StrikeKind::HEADER:
      return headerPowerBinturong(striker);
    // A shot type: the share of his shot power its part of the foot uses.
    case StrikeKind::PLACED_SHOT:
      return shotPowerBinturong(striker) * Sh::PLACED_FOOT_SHARE;
    case StrikeKind::CURLED_SHOT:
      return shotPowerBinturong(striker) * Sh::CURLED_FOOT_SHARE;
    case StrikeKind::CHIP_SHOT:
      return shotPowerBinturong(striker) * Sh::CHIP_FOOT_SHARE;
    case StrikeKind::TOE_POKE:
      return shotPowerBinturong(striker) * Sh::TOE_FOOT_SHARE;
    case StrikeKind::THROW:
      return MatchTuning::Passing::MAX_GROUND_SPEED;
    case StrikeKind::GROUND_PASS:
    case StrikeKind::LOFTED_PASS:
    case StrikeKind::CROSS:
    case StrikeKind::CLEARANCE:
    case StrikeKind::DRIVEN_PASS:
    case StrikeKind::CHIP:
      break;
  }
  return kickPowerBinturong(striker);
}

ContactAssessment MatchEngine::assessContactBinturong(
    const MatchPlayer& player, const ContactProfile& profile, Vector2F aim,
    float intendedSpeed, float intendedLift, const TouchMoment& moment,
    float surprise, float pressure, BallSpin spin, float spinCapacity,
    float powerCapacity) const
{
  ContactSituation situation;
  situation.facing = normalized(
      {std::cos(player.facingAngle),
       std::sin(player.facingAngle)});
  situation.aim = aim;
  situation.intendedSpeed = intendedSpeed;
  situation.intendedLift = intendedLift;
  situation.ballVelocity = ball.velocity;
  situation.ballVerticalSpeed = ball.velocityZ;
  situation.playerVelocity = player.velocity;
  situation.playerMaxSpeed = player.maxSpeed;
  situation.toBall = metricDirection(player.position, moment.contactPoint);
  situation.gap = moment.gap;
  situation.reach = moment.reach;
  situation.height = moment.height;
  situation.feetLine = bodyLinesBinturong(player).feet;
  situation.rightFooted =
      !player.player || player.player->getFoot() == Foot::Right;
  situation.surprise = surprise;
  situation.pressure = pressure;
  situation.intendedSpin = spin;
  situation.spinCapacity = spinCapacity;
  situation.powerCapacity = powerCapacity;
  return assessContact(profile, situation);
}

float StretchMap::at(float degrees, float share) const
{
  // The pair of points either side along each axis, and how far between
  // them (held at the ends).
  const auto bracket = [](const auto& points, float value)
  {
    const std::size_t count = points.size();
    if (value <= points[0]) return std::pair<std::size_t, float>{0, 0.0f};
    for (std::size_t index = 1; index < count; ++index)
      if (value <= points[index])
      {
        const float span = points[index] - points[index - 1];
        return std::pair<std::size_t, float>{
            index - 1,
            span > 0.0f ? (value - points[index - 1]) / span : 1.0f};
      }
    return std::pair<std::size_t, float>{count - 2, 1.0f};
  };
  const auto [row, rowShare] = bracket(angles, std::abs(degrees));
  const auto [column, columnShare] = bracket(distances, share);
  const auto blend = [&](std::size_t angle)
  {
    return values[angle][column] +
           (values[angle][column + 1] - values[angle][column]) * columnShare;
  };
  return blend(row) + (blend(row + 1) - blend(row)) * rowShare;
}

float approachAngleValue(Vector2F from, Vector2F facing)
{
  using F = MatchTuning::FirstTouch;
  const float cosine =
      std::clamp(from.x * facing.x + from.y * facing.y, -1.0f, 1.0f);
  const float degrees = std::acos(cosine) * 180.0f / std::numbers::pi_v<float>;
  if (degrees <= F::BEHIND_CLEAR_DEGREES) return 0.0f;
  if (degrees <= F::BEHIND_SQUARE_DEGREES)
    return F::BEHIND_SQUARE_VALUE * (degrees - F::BEHIND_CLEAR_DEGREES) /
           (F::BEHIND_SQUARE_DEGREES - F::BEHIND_CLEAR_DEGREES);
  if (degrees < F::BEHIND_FULL_DEGREES)
    return F::BEHIND_SQUARE_VALUE +
           (1.0f - F::BEHIND_SQUARE_VALUE) *
               (degrees - F::BEHIND_SQUARE_DEGREES) /
               (F::BEHIND_FULL_DEGREES - F::BEHIND_SQUARE_DEGREES);
  return 1.0f;
}

bool turnsOutward(const ContactSituation& situation)
{
  // In his own frame (metres, his centre at the origin): the ball where he
  // meets it, the way he sends it, and where that path leaves his reach.
  const Vector2F facing = situation.facing;
  const Vector2F aim = situation.aim;
  const Vector2F contact{situation.toBall.x * situation.gap,
                         situation.toBall.y * situation.gap};
  const auto cross = [](Vector2F a, Vector2F b) { return a.x * b.y - a.y * b.x; };
  const auto dot = [](Vector2F a, Vector2F b) { return a.x * b.x + a.y * b.y; };
  const float along = dot(contact, aim);
  const float exitDistance =
      -along + std::sqrt(std::max(along * along - dot(contact, contact) +
                                      situation.reach * situation.reach,
                                  0.0f));
  const Vector2F exit{contact.x + aim.x * exitDistance,
                      contact.y + aim.y * exitDistance};

  // Inward if the path crosses his facing line (from his centre straight
  // out the way he faces) before it leaves his reach...
  const float sideways = cross(aim, facing);
  if (std::abs(sideways) > EPSILON)
  {
    const float at = -cross(contact, facing) / sideways;
    const Vector2F crossing{contact.x + aim.x * at, contact.y + aim.y * at};
    if (at > EPSILON && at <= exitDistance && dot(crossing, facing) >= 0.0f)
      return false;
  }
  // ...or leaves it between his facing line and the line from his centre
  // through the ball (the narrower way round).
  const float ballSide = cross(facing, situation.toBall);
  if (situation.gap > EPSILON && std::abs(ballSide) > EPSILON)
  {
    const float sign = ballSide > 0.0f ? 1.0f : -1.0f;
    if (cross(facing, exit) * sign >= 0.0f &&
        cross(exit, situation.toBall) * sign >= 0.0f)
      return false;
  }
  return true;
}

ContactAssessment assessContact(const ContactProfile& profile,
                                const ContactSituation& situation)
{
  constexpr float RIGHT_ANGLE = std::numbers::pi_v<float> * 0.5f;
  constexpr std::size_t TURN_FACTOR = 1;
  const Vector2F facing = situation.facing;
  const Vector2F aim = situation.aim;
  const Vector2F ballVelocity = situation.ballVelocity;
  const Vector2F playerVelocity = situation.playerVelocity;
  // What he means, in three dimensions: his aim seen from above, tipped up
  // (or down) by the lift he means against his pace over the ground.
  const float intendedSpeed =
      std::hypot(situation.intendedSpeed, situation.intendedLift);
  const float elevation =
      intendedSpeed > EPSILON
          ? std::atan2(situation.intendedLift, situation.intendedSpeed)
          : 0.0f;
  const float level = std::cos(elevation);
  const std::array<float, 3> aim3{aim.x * level, aim.y * level,
                                  std::sin(elevation)};
  ContactAssessment contact;
  contact.aim = aim;
  contact.intendedSpeed = intendedSpeed;
  contact.intendedElevation = elevation;

  // The line it came on, in three dimensions: sending it back along it (a
  // cushion) or on along it (taking it on) is no turn; whichever is nearer
  // what he means is the reference. A still ball has no line.
  const std::array<float, 3> incoming3{ballVelocity.x, ballVelocity.y,
                                       situation.ballVerticalSpeed};
  const float incomingSpeed =
      std::hypot(incoming3[0], incoming3[1], incoming3[2]);
  const bool moving = incomingSpeed > MatchTuning::Ball::STOP_SPEED;
  const bool rebound =
      moving && incoming3[0] * aim3[0] + incoming3[1] * aim3[1] +
                        incoming3[2] * aim3[2] <
                    0.0f;
  const float sense = rebound ? -1.0f : 1.0f;
  const std::array<float, 3> line3 =
      moving ? std::array<float, 3>{sense * incoming3[0] / incomingSpeed,
                                    sense * incoming3[1] / incomingSpeed,
                                    sense * incoming3[2] / incomingSpeed}
             : aim3;
  // Seen from above (none when the line is straight up or down: his aim),
  // and its steepness.
  const float lineLevel = std::hypot(line3[0], line3[1]);
  const Vector2F line =
      lineLevel > 1e-4f ? Vector2F{line3[0] / lineLevel, line3[1] / lineLevel}
                        : aim;
  const float lineElevation = std::atan2(line3[2], lineLevel);
  contact.incomingSpeed = incomingSpeed;
  contact.moving = moving;
  contact.rebound = rebound;
  contact.line = line;
  contact.lineElevation = lineElevation;
  // The three jobs: the turn off that line seen from above (+
  // anticlockwise), the turn in steepness (+ up), and the pace change in
  // three dimensions (- taking pace off, + adding it).
  const float turn = std::atan2(line.x * aim.y - line.y * aim.x,
                                line.x * aim.x + line.y * aim.y);
  const float verticalTurn = moving ? elevation - lineElevation : 0.0f;
  const bool outward = turnsOutward(situation);
  const float paceChange = intendedSpeed - (moving ? incomingSpeed : 0.0f);
  contact.turn = turn;
  contact.verticalTurn = verticalTurn;
  contact.outward = outward;
  contact.paceChange = paceChange;
  const auto clamp01 = [](float value)
  { return std::clamp(value, 0.0f, 1.0f); };
  // A turn's size: 1 at 90 degrees, in proportion to the incoming pace
  // below the profile's reference (strikes; the touch counts it in full).
  const auto turnSize = [&](float radians)
  {
    return profile.turnPaceReference > 0.0f
               ? clamp01(std::abs(radians) / RIGHT_ANGLE) *
                     clamp01(incomingSpeed / profile.turnPaceReference)
               : clamp01(std::abs(radians) / RIGHT_ANGLE);
  };

  // How hard each job is: its size, then the conditions (each 0-1). How
  // fast it meets him and where from are read relative to him: a ball
  // rolling ahead at his own pace is a slow ball met from the front.
  const Vector2F toBall = situation.toBall;
  const Vector2F relative{ballVelocity.x - playerVelocity.x,
                          ballVelocity.y - playerVelocity.y};
  const float relativeLevel = length(relative);
  const float meetingSpeed =
      std::hypot(relative.x, relative.y, situation.ballVerticalSpeed);
  // Where it comes from seen from above: back along its path (relative to
  // him), or where it lies if it keeps pace with him.
  const Vector2F from =
      relativeLevel > MatchTuning::Ball::STOP_SPEED
          ? Vector2F{-relative.x / relativeLevel, -relative.y / relativeLevel}
          : toBall;
  // Which foot: the side of his facing line it is on (+ his right). On
  // the line he uses his stronger foot.
  const float side = facing.y * toBall.x - facing.x * toBall.y;
  const float weakSide =
      (situation.rightFooted ? side < 0.0f : side > 0.0f) ? 1.0f : 0.0f;
  // The spin he means, and how much of what he can put on it is.
  contact.intendedSpin = situation.intendedSpin;
  const float spinAsked =
      std::hypot(situation.intendedSpin.side, situation.intendedSpin.top);
  const float spinShare =
      situation.spinCapacity > 0.0f
          ? clamp01(spinAsked / situation.spinCapacity)
          : 0.0f;
  contact.factors = {{
      {"pace to take off or add", TermSource::SITUATION,
       clamp01(std::abs(paceChange) / profile.referenceSpeed), 0.0f,
       profile.paceChangeWeight},
      {outward ? "turning it outward" : "turning it inward",
       TermSource::SITUATION, turnSize(turn),
       profile.turnAngle *
           (outward ? profile.turnOutward : profile.turnInward),
       0.0f},
      {"incoming pace", TermSource::SITUATION,
       clamp01(meetingSpeed / profile.referenceSpeed),
       profile.incomingPace.angle, profile.incomingPace.weight},
      {"its approach angle", TermSource::SITUATION,
       approachAngleValue(from, facing), profile.approach.angle,
       profile.approach.weight},
      {"his own speed", TermSource::SITUATION,
       clamp01(length(playerVelocity) /
               std::max(situation.playerMaxSpeed, EPSILON)),
       profile.ownSpeed.angle, profile.ownSpeed.weight},
      {"stretching for it", TermSource::SITUATION,
       clamp01(profile.stretchMap.at(
           std::acos(std::clamp(toBall.x * facing.x + toBall.y * facing.y,
                                -1.0f, 1.0f)) *
               180.0f / std::numbers::pi_v<float>,
           situation.gap / std::max(situation.reach, EPSILON))),
       profile.stretch.angle, profile.stretch.weight},
      {"on his weaker foot", TermSource::ATTRIBUTE, clamp01(weakSide),
       profile.weakerFoot.angle, profile.weakerFoot.weight},
      {"off the ground", TermSource::SITUATION,
       clamp01(situation.height / std::max(situation.feetLine, EPSILON)),
       profile.offGround.angle, profile.offGround.weight},
      {"not expecting it", TermSource::SITUATION, situation.surprise,
       profile.surprise.angle, profile.surprise.weight},
      {"pressure", TermSource::SITUATION, clamp01(situation.pressure),
       profile.pressure.angle, profile.pressure.weight},
      {verticalTurn > 0.0f ? "turning it up" : "turning it down",
       TermSource::SITUATION, turnSize(verticalTurn), 0.0f, 0.0f,
       profile.verticalTurnAngle},
      // The spin he asks for against the most he can put on.
      {"the spin he asks for", TermSource::ATTRIBUTE, spinShare,
       profile.spinAsked.angle, profile.spinAsked.weight},
      // The pace he asks for (in three dimensions) against his power, less
      // the further the strike is off the way his body faces.
      {"how much of his power", TermSource::ATTRIBUTE,
       powerAskedValue(
           intendedSpeed,
           situation.powerCapacity *
               powerByAngleShare(
                   std::acos(std::clamp(aim.x * facing.x + aim.y * facing.y,
                                        -1.0f, 1.0f)) *
                       180.0f / std::numbers::pi_v<float>,
                   outward ? profile.powerOutwardSquare
                           : profile.powerInwardSquare,
                   profile.powerBehind),
           profile.powerComfortShare),
       profile.powerAsked.angle, profile.powerAsked.weight},
  }};
  // The conditions weigh on the turn in steepness, and on the spin, as on
  // the turn seen from above; the spin asked on the spin by its own weight.
  for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
    if (index != TURN_FACTOR && index != VERTICAL_TURN_FACTOR)
    {
      contact.factors[index].verticalWeight = contact.factors[index].angleWeight;
      contact.factors[index].spinWeight = contact.factors[index].angleWeight;
    }
  contact.factors[SPIN_ASKED_FACTOR].spinWeight = profile.spinAskedSpin;
  std::array<float, 4> sums{};
  constexpr std::array<ContactJob, 4> JOBS{ContactJob::ANGLE, ContactJob::WEIGHT,
                                           ContactJob::VERTICAL, ContactJob::SPIN};
  for (std::size_t job = 0; job < JOBS.size(); ++job)
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
      if (index != WEAK_FOOT_FACTOR)
        sums[job] += contact.factors[index].value *
                     contact.factors[index].weightFor(JOBS[job]);
  // His weaker foot makes every job harder by its share: little on an easy
  // one, a lower ceiling on a hard one.
  const ContactFactor& weakFoot = contact.factors[WEAK_FOOT_FACTOR];
  contact.weakFootAngle = sums[0] * weakFoot.value * weakFoot.angleWeight;
  contact.weakFootWeight = sums[1] * weakFoot.value * weakFoot.weightWeight;
  contact.weakFootVertical =
      sums[2] * weakFoot.value * weakFoot.verticalWeight;
  contact.angleDifficulty = clamp01(sums[0] + contact.weakFootAngle);
  contact.weightDifficulty = clamp01(sums[1] + contact.weakFootWeight);
  contact.verticalDifficulty = clamp01(sums[2] + contact.weakFootVertical);
  contact.weakFootSpin = sums[3] * weakFoot.value * weakFoot.spinWeight;
  contact.spinDifficulty = clamp01(sums[3] + contact.weakFootSpin);
  return contact;
}

ContactOutcome MatchEngine::carryOutContactBinturong(
    const MatchPlayer& player, const ContactProfile& profile,
    const ContactAssessment& contact, float skill)
{
  const auto clamp01 = [](float value)
  { return std::clamp(value, 0.0f, 1.0f); };
  // His skill cuts the chance of a job going wrong (and a clean contact's
  // jitter); tiredness raises it, and so does playing it with his weaker
  // foot, less the better his technique. How badly a job goes wrong is the
  // ball's doing.
  const float fatigue =
      1.0f + tierLoss(TECHNIQUE_SHAPE, player.stamina) * profile.fatigueErrorGain;
  // Only where a foot plays it (a header or a throw has none).
  const bool footed =
      profile.weakerFoot.angle > 0.0f || profile.weakerFoot.weight > 0.0f;
  const float weakerFoot =
      1.0f + (footed ? contact.factors[WEAK_FOOT_FACTOR].value : 0.0f) *
                 profile.weakerFootErrorGain *
                 (1.0f - player.derivedStat(DerivedStat::TECHNIQUE));
  const float execution = fatigue * weakerFoot;
  const float scale = (1.0f - skill * profile.skillReduction) * execution;
  // A clean contact's angle jitter: the profile's share of it grows with the
  // angle job's difficulty (an easy strike is tight), the rest is there at
  // any difficulty.
  const float angleJitter =
      profile.angleJitter *
      (1.0f - profile.jitterDifficultyShare +
       profile.jitterDifficultyShare * contact.angleDifficulty);
  ContactOutcome outcome;
  outcome.angleFailChance = clamp01(
      (profile.angleFailBase + contact.angleDifficulty * profile.angleFailRange) *
      scale);
  outcome.weightFailChance = clamp01(
      (profile.weightFailBase +
       contact.weightDifficulty * profile.weightFailRange) *
      scale);

  // Each job: whether it went wrong, by what share of the job, which way
  // (the usual way: heavy, under-turned), and a clean contact's jitter. The
  // same draws every contact, so changing a knob keeps the random stream.
  struct Job
  {
    bool failed = false;
    /** Share of the job missed: + the usual way, - the other. */
    float miss = 0.0f;
    float jitter = 0.0f;
    /** How badly a failure went (0-1, before its size), and whether the
     * usual way. */
    float severity = 0.0f;
    bool usual = true;
  };
  const auto carryOut =
      [this](float chance, float worstMiss, float usualShare, float jitterSigma)
  {
    const float failRoll = randomFloat(0.0f, 1.0f);
    const float missRoll = randomFloat(0.0f, 1.0f);
    const float wayRoll = randomFloat(0.0f, 1.0f);
    // Near-normal (a sum of three uniforms), bounded at three sigma.
    const float jitter = (randomFloat(0.0f, 1.0f) + randomFloat(0.0f, 1.0f) +
                          randomFloat(0.0f, 1.0f) - 1.5f) *
                         2.0f * jitterSigma;
    Job job;
    job.failed = failRoll < chance;
    job.severity = missRoll;
    job.usual = wayRoll < usualShare;
    if (job.failed)
      job.miss = std::min(missRoll * worstMiss, 1.0f) *
                 (wayRoll < usualShare ? 1.0f : -1.0f);
    job.jitter = jitter;
    return job;
  };
  const Job angleJob = carryOut(
      outcome.angleFailChance,
      profile.angleMissBase + contact.angleDifficulty * profile.angleMissRange,
      profile.underTurnShare, angleJitter * scale);
  const Job weightJob = carryOut(
      outcome.weightFailChance,
      profile.weightMissBase +
          contact.weightDifficulty * profile.weightMissRange,
      profile.heavyShare, profile.weightJitter * scale);
  outcome.angleFailed = angleJob.failed;
  outcome.weightFailed = weightJob.failed;

  // A strike can miss a line it never meant to turn off (a dead ball sliced
  // wide): failed, it also misses by a fixed angle, sized by the difficulty
  // and the same severity, the usual way towards the line it came on. Only
  // a profile with such a miss draws its side, so a touch's draws are
  // unchanged.
  float absoluteMiss = 0.0f;
  if (profile.angleMissAbsoluteBase > 0.0f ||
      profile.angleMissAbsoluteRange > 0.0f)
  {
    const float sideRoll = randomFloat(0.0f, 1.0f);
    if (angleJob.failed)
    {
      const float size =
          angleJob.severity *
          (profile.angleMissAbsoluteBase +
           contact.angleDifficulty * profile.angleMissAbsoluteRange);
      const float side =
          std::abs(contact.turn) > EPSILON
              ? (contact.turn > 0.0f ? 1.0f : -1.0f) *
                    (angleJob.usual ? -1.0f : 1.0f)
              : (sideRoll < 0.5f ? -1.0f : 1.0f);
      absoluteMiss = side * size;
    }
  }

  // A ball struck into the air: the profile's vertical share of the fixed
  // miss and of the jitter goes into the elevation (up or down, the usual
  // way up), the rest stays sideways. Only such a profile draws these, so a
  // ground contact's draws are unchanged.
  float elevation = contact.intendedElevation;
  float sideways = 1.0f;
  if (profile.elevationJitter > 0.0f)
  {
    // Or the elevation has its own error, independent of the line's (which
    // keeps all of its own): its own jitter, and on a failed angle its own
    // miss, as big as the line's can be, the usual way up.
    const float upRoll = randomFloat(0.0f, 1.0f);
    const float severityRoll = randomFloat(0.0f, 1.0f);
    const float jitter = (randomFloat(0.0f, 1.0f) + randomFloat(0.0f, 1.0f) +
                          randomFloat(0.0f, 1.0f) - 1.5f) *
                         2.0f * profile.elevationJitter * scale;
    elevation += jitter;
    if (angleJob.failed)
      elevation += severityRoll *
                   (profile.angleMissAbsoluteBase +
                    contact.angleDifficulty * profile.angleMissAbsoluteRange) *
                   (upRoll < profile.upShare ? 1.0f : -1.0f);
    elevation = std::clamp(elevation, profile.lowestElevation, 1.4f);
  }
  else if (profile.verticalShare > 0.0f)
  {
    const float upRoll = randomFloat(0.0f, 1.0f);
    const float jitter = (randomFloat(0.0f, 1.0f) + randomFloat(0.0f, 1.0f) +
                          randomFloat(0.0f, 1.0f) - 1.5f) *
                         2.0f * angleJitter * scale;
    const float share = profile.verticalShare;
    sideways = 1.0f - share;
    elevation += share * jitter;
    if (angleJob.failed)
      elevation += share * std::abs(absoluteMiss) *
                   (upRoll < profile.upShare ? 1.0f : -1.0f);
    elevation = std::clamp(elevation, profile.lowestElevation, 1.4f);
  }

  // The vertical angle job (profiles that carry it out): the turn in
  // steepness off the line, done as the angle job is, with its technique
  // values. A failure mostly under-turns: the ball stays nearer the
  // steepness it came at. Its jitter is a share of the turn, so a ball
  // kept along the ground stays on it. Only such a profile draws these.
  if (profile.carriesOutVertical)
  {
    constexpr float RIGHT_ANGLE = std::numbers::pi_v<float> * 0.5f;
    outcome.verticalFailChance =
        clamp01((profile.angleFailBase +
                 contact.verticalDifficulty * profile.angleFailRange) *
                scale);
    const Job verticalJob = carryOut(
        outcome.verticalFailChance,
        profile.angleMissBase +
            contact.verticalDifficulty * profile.angleMissRange,
        profile.underTurnShare,
        profile.angleJitter * scale *
            std::min(std::abs(contact.verticalTurn) / RIGHT_ANGLE, 1.0f));
    outcome.verticalFailed = verticalJob.failed;
    elevation = std::clamp(contact.lineElevation +
                               contact.verticalTurn * (1.0f - verticalJob.miss) +
                               verticalJob.jitter,
                           -RIGHT_ANGLE, RIGHT_ANGLE);
  }

  // What he actually did. The angle: the turn, short by the miss (or past
  // it), plus the jitter in radians. The weight: heavier than meant by the
  // miss's share of the pace change (or lighter), plus the jitter's share.
  float achievedTurn =
      sideways == 1.0f
          ? contact.turn * (1.0f - angleJob.miss) + angleJob.jitter
          : contact.turn * (1.0f - angleJob.miss) + angleJob.jitter * sideways;
  if (absoluteMiss != 0.0f) achievedTurn += absoluteMiss * sideways;
  float actualSpeed =
      std::max(contact.intendedSpeed + (weightJob.miss + weightJob.jitter) *
                                           std::abs(contact.paceChange),
               0.0f);
  // Within what a contact can do: back the way it came no faster than a
  // rebound, on the way it came no faster than it arrived unless he meant
  // to add pace.
  if (contact.rebound)
    actualSpeed = std::min(
        actualSpeed, std::max(contact.intendedSpeed,
                              contact.incomingSpeed * profile.reboundShare));
  else if (contact.moving && contact.paceChange < 0.0f)
    actualSpeed = std::min(actualSpeed, contact.incomingSpeed);
  const float cosine = std::cos(achievedTurn);
  const float sine = std::sin(achievedTurn);
  const Vector2F direction{contact.line.x * cosine - contact.line.y * sine,
                           contact.line.x * sine + contact.line.y * cosine};
  if (elevation == 0.0f)
  {
    outcome.velocity = {direction.x * actualSpeed, direction.y * actualSpeed};
  }
  else
  {
    // The pace is the strike's, in three dimensions: split by the
    // elevation he achieved.
    const float along = actualSpeed * std::cos(elevation);
    outcome.velocity = {direction.x * along, direction.y * along};
    outcome.verticalSpeed = actualSpeed * std::sin(elevation);
  }
  outcome.achievedElevation = elevation;
  outcome.achievedTurn = achievedTurn;
  outcome.actualSpeed = actualSpeed;

  // The spin job: as asked, unless the profile says how it goes wrong. A
  // failure puts on a share less (or more) than he meant; a clean strike
  // jitters it. Only such a profile draws these, so the draws are
  // unchanged until it is tuned.
  outcome.achievedSpin = contact.intendedSpin;
  if (profile.spinFailBase > 0.0f || profile.spinFailRange > 0.0f ||
      profile.spinJitter > 0.0f)
  {
    outcome.spinFailChance = clamp01(
        (profile.spinFailBase + contact.spinDifficulty * profile.spinFailRange) *
        scale);
    const Job spinJob = carryOut(
        outcome.spinFailChance,
        profile.spinMissBase + contact.spinDifficulty * profile.spinMissRange,
        profile.underSpinShare, profile.spinJitter * scale);
    outcome.spinFailed = spinJob.failed;
    const float asked =
        std::hypot(contact.intendedSpin.side, contact.intendedSpin.top);
    if (asked > EPSILON)
    {
      const float share =
          std::max(1.0f - spinJob.miss + spinJob.jitter / asked, 0.0f);
      outcome.achievedSpin = {contact.intendedSpin.side * share,
                              contact.intendedSpin.top * share};
    }
  }
  return outcome;
}

TouchResult MatchEngine::firstTouchBinturong(const MatchPlayer& player,
                                             const TouchIntent& intent,
                                             const TouchMoment& moment,
                                             bool ownBall)
{
  using K = TouchKnob;
  const FirstTouchKnobs& knob = touchKnobs;
  // What he means to do: straight at the target spot, weighted with the
  // rolling physics to arrive at the intended speed. A first touch is no
  // pass; a dribble touch is no faster than a push ahead of his run.
  const Vector2F gap = intent.targetSpot - moment.contactPoint;
  const float metres = length(gap);
  const Vector2F facing = normalized(
      {std::cos(player.facingAngle),
       std::sin(player.facingAngle)});
  const Vector2F aim =
      metres > EPSILON ? Vector2F{gap.x / metres, gap.y / metres} : facing;
  // A knock-on into space is as hard as he means it.
  const float maxSpeed =
      intent.knockOn ? std::numeric_limits<float>::max()
      : ownBall      ? std::max(length(player.velocity), intent.arrivalSpeed) +
                      MatchTuning::Dribble::LOOSE_MAX_SPEED_OVER_RUN
                     : knob[K::MAX_TOUCH_SPEED];
  const float intendedSpeed =
      metres > EPSILON
          ? std::min(groundLaunchSpeed(metres, intent.arrivalSpeed), maxSpeed)
          : 0.0f;

  // The touch is a contact with the touch profile, carried out with his
  // First Touch (his Dribbling for his own ball).
  ContactRequest request;
  request.kind = ownBall ? ContactRequest::Kind::DRIBBLE_TOUCH
                         : ContactRequest::Kind::FIRST_TOUCH;
  request.aim = aim;
  request.speed = intendedSpeed;
  request.lift = intent.liftSpeed;
  request.surprise = ownBall ? 0.0f : intent.surprise;
  request.pressure = intent.pressure;
  const ContactResult contact = contactBinturong(player, request, moment);
  const ContactOutcome& outcome = contact.outcome;
  const float turn = contact.contact.turn;

  TouchResult result;
  result.velocity = outcome.velocity;
  result.verticalSpeed = outcome.verticalSpeed;
  // Signed along the turn: - short of it, + past it.
  result.angleError =
      (outcome.achievedTurn - turn) * (turn < 0.0f ? -1.0f : 1.0f);
  result.weightError = outcome.actualSpeed - contact.contact.intendedSpeed;
  result.angleDifficulty = contact.contact.angleDifficulty;
  result.weightDifficulty = contact.contact.weightDifficulty;
  result.angleFailed = outcome.angleFailed;
  result.weightFailed = outcome.weightFailed;
  result.turn = std::abs(turn);
  result.outward = contact.contact.outward && std::abs(turn) > EPSILON;
  result.paceChange = contact.contact.paceChange;
  return result;
}

float MatchEngine::kickPowerFromSkillBinturong(float skill)
{
  // Skill 1 (0.01) to 99 (0.99) spans the range; a home edge past 99 adds
  // nothing.
  using P = MatchTuning::Passing;
  const float share = std::clamp((skill - 0.01f) / 0.98f, 0.0f, 1.0f);
  return P::MIN_KICK_SPEED + (P::MAX_KICK_SPEED - P::MIN_KICK_SPEED) * share;
}

float MatchEngine::kickPowerBinturong(const MatchPlayer& player)
{
  return kickPowerFromSkillBinturong(player.passing);
}

float MatchEngine::shotPowerBinturong(const MatchPlayer& player)
{
  return kickPowerFromSkillBinturong(player.shooting);
}

float MatchEngine::incomingPowerBudgetBinturong(float power, Vector2F aim) const
{
  // The ball as it meets him (over the ground, m/s): what of its pace runs
  // across the line he strikes costs his foot power to turn; what runs
  // along it helps a little (coming at him) or takes a little (running on).
  using P = MatchTuning::Passing;
  const float along = ball.velocity.x * aim.x + ball.velocity.y * aim.y;
  const float across = std::abs(ball.velocity.x * aim.y - ball.velocity.y * aim.x);
  const float held = P::INCOMING_ACROSS_SHARE * across;
  const float budget = std::sqrt(std::max(power * power - held * held, 0.0f)) -
                       P::INCOMING_ALONG_SHARE * along;
  return std::max(budget, P::INCOMING_BUDGET_FLOOR * power);
}

std::pair<float, float> MatchEngine::fullPowerLoftBinturong(
    float distanceMetres, float speed, float startHeight, float arrivalHeight,
    BallSpin spin) const
{
  // How far out a ball launched at `speed` and this angle is when it comes
  // down through the arrival height (or lands): its reach.
  constexpr float DT = MatchTuning::Timing::FIXED_STEP_SECONDS /
                       static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
  const auto reach = [&](float angle)
  {
    FlightState flight{speed * std::cos(angle), speed * std::sin(angle),
                       startHeight, false, spin};
    float x = 0.0f;
    float y = 0.0f;
    float heading = 0.0f;
    for (float time = 0.0f; time < MatchTuning::Ball::MAX_FLIGHT_SECONDS &&
                            flight.horizontalSpeed > 0.0f;
         time += DT)
    {
      const float previousHeight = flight.height;
      const float moved = stepFlight(flight, DT);
      heading += flight.turnRate * DT;
      const float lastX = x;
      const float lastY = y;
      x += std::cos(heading) * moved;
      y += std::sin(heading) * moved;
      if (flight.bounced) return std::hypot(x, y);
      if (flight.verticalSpeed < 0.0f && flight.height <= arrivalHeight)
      {
        const float share = (previousHeight - arrivalHeight) /
                            std::max(previousHeight - flight.height, EPSILON);
        return std::hypot(lastX + (x - lastX) * share,
                          lastY + (y - lastY) * share);
      }
    }
    return std::hypot(x, y);
  };
  // The angle he sends it furthest (golden section), then the flattest
  // that gets there (bisection below it), if any does.
  constexpr float DEGREE = std::numbers::pi_v<float> / 180.0f;
  constexpr float GOLDEN = 0.618034f;
  float low = 5.0f * DEGREE;
  float high = 60.0f * DEGREE;
  float left = high - GOLDEN * (high - low);
  float right = low + GOLDEN * (high - low);
  float leftReach = reach(left);
  float rightReach = reach(right);
  for (int round = 0; round < 14; ++round)
  {
    if (leftReach < rightReach)
    {
      low = left;
      left = right;
      leftReach = rightReach;
      right = low + GOLDEN * (high - low);
      rightReach = reach(right);
    }
    else
    {
      high = right;
      right = left;
      rightReach = leftReach;
      left = high - GOLDEN * (high - low);
      leftReach = reach(left);
    }
  }
  float best = 0.5f * (low + high);
  if (reach(best) >= distanceMetres)
  {
    float flat = 5.0f * DEGREE;
    float steep = best;
    for (int round = 0; round < 12; ++round)
    {
      const float middle = 0.5f * (flat + steep);
      (reach(middle) >= distanceMetres ? steep : flat) = middle;
    }
    best = steep;
  }
  return {speed * std::cos(best), speed * std::sin(best)};
}

MatchEngine::LoftFlight MatchEngine::flyLoftBinturong(
    float along, float up, float startHeight, float arrivalHeight,
    BallSpin spin, float probeMetres) const
{
  constexpr float DT = MatchTuning::Timing::FIXED_STEP_SECONDS /
                       static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
  FlightState flight{along, up, startHeight, false, spin};
  LoftFlight result;
  float x = 0.0f;
  float y = 0.0f;
  float heading = 0.0f;
  for (float time = 0.0f; time < MatchTuning::Ball::MAX_FLIGHT_SECONDS &&
                          flight.horizontalSpeed > 0.0f;
       time += DT)
  {
    const float previousHeight = flight.height;
    const float before = std::hypot(x, y);
    const float moved = stepFlight(flight, DT);
    heading += flight.turnRate * DT;
    x += std::cos(heading) * moved;
    y += std::sin(heading) * moved;
    const float now = std::hypot(x, y);
    const auto at = [&](float out)
    {
      const float share = (out - before) / std::max(now - before, EPSILON);
      return previousHeight + (flight.height - previousHeight) * share;
    };
    if (result.probeHeight < 0.0f && now >= probeMetres && !flight.bounced)
      result.probeHeight = std::max(at(probeMetres), 0.0f);
    if (flight.bounced)
    {
      result.reach = now;
      return result;
    }
    if (flight.verticalSpeed < 0.0f && flight.height <= arrivalHeight)
    {
      const float share = (previousHeight - arrivalHeight) /
                          std::max(previousHeight - flight.height, EPSILON);
      result.reach = before + (now - before) * share;
      return result;
    }
  }
  result.reach = std::hypot(x, y);
  return result;
}

std::pair<float, float> MatchEngine::chipLaunchBinturong(
    float distanceMetres, float maxSpeed, float startHeight,
    float arrivalHeight, BallSpin spin, float clearMetres,
    float clearHeight) const
{
  using P = MatchTuning::Passing;
  constexpr float DEGREE = std::numbers::pi_v<float> / 180.0f;
  const float probe =
      clearMetres >= 0.0f
          ? clearMetres
          : std::min(P::CHIP_CLEAR_DISTANCE_METRES, 0.5f * distanceMetres);
  const float clearAt = clearHeight >= 0.0f ? clearHeight : P::CHIP_CLEAR_HEIGHT_METRES;
  const auto fly = [&](float speed, float angle)
  {
    return flyLoftBinturong(speed * std::cos(angle), speed * std::sin(angle),
                            startHeight, arrivalHeight, spin, probe);
  };
  // The pace that gets it there at this angle (his limit, if none does).
  const auto speedFor = [&](float angle)
  {
    if (fly(maxSpeed, angle).reach < distanceMetres) return maxSpeed;
    float slow = 0.0f;
    float fast = maxSpeed;
    for (int round = 0; round < 14; ++round)
    {
      const float middle = 0.5f * (slow + fast);
      (fly(middle, angle).reach >= distanceMetres ? fast : slow) = middle;
    }
    return fast;
  };
  const auto clears = [&](float angle)
  {
    return fly(speedFor(angle), angle).probeHeight >= clearAt;
  };
  // The flattest angle that clears (bisection; steeper clears higher).
  float flat = P::CHIP_MIN_ANGLE_DEGREES * DEGREE;
  float steep = P::CHIP_MAX_ANGLE_DEGREES * DEGREE;
  float angle = steep;
  if (clears(flat))
  {
    angle = flat;
  }
  else if (clears(steep))
  {
    for (int round = 0; round < 10; ++round)
    {
      const float middle = 0.5f * (flat + steep);
      (clears(middle) ? steep : flat) = middle;
    }
    angle = steep;
  }
  const float speed = speedFor(angle);
  return {speed * std::cos(angle), speed * std::sin(angle)};
}

PitchPoint MatchEngine::curlAimBinturong(PitchPoint target, float curl) const
{
  // Aimed off by the curl, the other way: as far out, turned round the ball.
  if (curl == 0.0f) return target;
  const Vector2F gap = target - ball.position;
  const float cosine = std::cos(-curl);
  const float sine = std::sin(-curl);
  const Vector2F turned = {gap.x * cosine - gap.y * sine,
                                   gap.x * sine + gap.y * cosine};
  return PitchPoint{ball.position.x + turned.x, ball.position.y + turned.y};
}

namespace
{
/** Spin capacity (rad/s) for a skill of 0-1: skill 1 (0.01) to 99 (0.99)
 * spans the range, as kick power does. */
float spinCapacityFromSkill(float skill)
{
  using P = MatchTuning::Passing;
  const float share = std::clamp((skill - 0.01f) / 0.98f, 0.0f, 1.0f);
  return (P::MIN_SPIN_REVS + (P::MAX_SPIN_REVS - P::MIN_SPIN_REVS) * share) *
         2.0f * std::numbers::pi_v<float>;
}
}  // namespace

float MatchEngine::spinCapacityBinturong(const MatchPlayer& player)
{
  return spinCapacityFromSkill(player.passing);
}

float MatchEngine::shotSpinCapacityBinturong(const MatchPlayer& player)
{
  return spinCapacityFromSkill(player.shooting);
}

MatchEngine::Kick MatchEngine::kickPassBinturong(const MatchPlayer& passer,
                                                 const MatchPlayer* receiver,
                                                 PitchPoint target,
                                                 float /*passDistance*/,
                                                 bool lofted, bool delivery,
                                                 std::optional<BallSpin> spin,
                                                 std::optional<float> arrivalSpeed,
                                                 std::optional<PassTechnique> technique)
{
  // Every pass is a contact (contact plan step 3), struck where the ball
  // lies. A kick: a ground pass, a lofted pass, a cross, a driven pass or a
  // chip; a keeper's punt from his hands is a lofted kick. A throw: a
  // throw-in, or a keeper rolling it out or throwing it overarm (goalkeeper
  // plan step 5: a ground pass up to 25 m rolled, beyond thrown; only a ball
  // in his hands, not one at his feet).
  const bool fromHands = passer.isGoalkeeper && isKeeperHoldingBall() &&
                         getPossessionAttachment() == passer.player &&
                         inPenaltyArea(passer.position, passer.isHomeTeam);
  const bool thrown = state == MatchState::THROW_IN || (fromHands && !lofted);
  const bool overarm = fromHands && !lofted &&
                       distance(ball.position, target) > MatchTuning::KeeperHeld::ROLL_LONGEST_METRES;
  if (fromHands)
  {
    noteKeeperDistribution(lofted, overarm);
  }
  const PassTechnique how = technique.value_or(
      lofted ? PassTechnique::LOFTED : PassTechnique::GROUND);
  const StrikeKind kind = thrown ? StrikeKind::THROW
                          : how == PassTechnique::DRIVEN ? StrikeKind::DRIVEN_PASS
                          : how == PassTechnique::CHIP   ? StrikeKind::CHIP
                          : !lofted                      ? StrikeKind::GROUND_PASS
                          : delivery                     ? StrikeKind::CROSS
                                                         : StrikeKind::LOFTED_PASS;
  const float passDistance = distance(ball.position, target);
  // The spin he means: what the brain asks for, else the kind's natural
  // spin; no more than he can put on. A throw has none.
  constexpr float REV = 2.0f * std::numbers::pi_v<float>;
  const float spinCapacity = thrown ? 0.0f : spinCapacityBinturong(passer);
  using P = MatchTuning::Passing;
  const float naturalTopspin =
      kind == StrikeKind::CROSS         ? P::DEFAULT_CROSS_TOPSPIN_REVS
      : kind == StrikeKind::LOFTED_PASS ? P::DEFAULT_LOFTED_TOPSPIN_REVS
      : kind == StrikeKind::DRIVEN_PASS ? P::DEFAULT_DRIVEN_TOPSPIN_REVS
      : kind == StrikeKind::CHIP        ? P::DEFAULT_CHIP_TOPSPIN_REVS
                                        : 0.0f;
  BallSpin meant = spin.value_or(BallSpin{0.0f, naturalTopspin * REV});
  if (const float asked = std::hypot(meant.side, meant.top);
      asked > spinCapacity)
  {
    const float share = asked > 0.0f ? spinCapacity / asked : 0.0f;
    meant = {meant.side * share, meant.top * share};
  }
  // Curved only if he means it; now and then by accident, less often the
  // better he passes, and the way the foot he struck it with bends it:
  // the left foot to his right, the right foot to his left (a curve, a
  // heading turn, put on as sidespin).
  const auto curveOf = [&](const Kick& kick, float chance, float maxCurve)
  {
    BallSpin put = kick.spin;
    if (randomFloat(0.0f, 1.0f) <
        chance * (1.0f - std::clamp(passer.passing, 0.0f, 1.0f)))
      put.side += (kick.leftFoot ? 1.0f : -1.0f) * randomFloat(0.0f, maxCurve) /
                  MatchTuning::Ball::TURN_PER_SIDESPIN;
    return put;
  };
  // His kick power: the fastest he can send it, along the ground or through
  // the air, less what turning a moving ball costs him. A pass that needs
  // more is struck at his limit and falls short.
  const float power =
      thrown ? kickPowerBinturong(passer)
             : incomingPowerBudgetBinturong(kickPowerBinturong(passer),
                                            metricDirection(ball.position, target));
  // From his hands, where he lets it go (goalkeeper plan step 5).
  const float startMetres = fromHands ? heldReleaseBinturong(passer, getPossessionState().heldRelease).second
                                      : ball.z;
  if (overarm)
  {
    // Overarm (goalkeeper plan step 5): to drop at the target (chest height
    // to a team-mate) no higher than the throw's angle, with the pace that
    // gets it there at that angle, at no more than his fastest throw
    // (Throwing); beyond his reach, at his fastest at that angle, short.
    using K = MatchTuning::KeeperHeld;
    const float throwing = std::clamp((passer.derivedStat(DerivedStat::THROWING) - STAT_1_RATING) /
                                          (STAT_99_RATING - STAT_1_RATING),
                                      0.0f, 1.0f);
    const float most = K::THROW_SPEED_POOR + (K::THROW_SPEED_BEST - K::THROW_SPEED_POOR) * throwing;
    const float steepest = K::THROW_MAX_ANGLE_DEGREES * std::numbers::pi_v<float> / 180.0f;
    const float arrivalMetres = receiver ? P::LOFTED_ARRIVAL_HEIGHT_METRES : 0.0f;
    const auto upFor = [&](float pace)
    { return loftVerticalSpeed(passDistance, pace, startMetres, arrivalMetres, meant); };
    // The faster along, the flatter: the slowest pace within the angle.
    float low = 1.0f;
    float high = most * std::cos(steepest);
    float along = high;
    float up = upFor(high);
    if (std::atan2(up, high) <= steepest)
    {
      for (int step = 0; step < 24; ++step)
      {
        const float mid = 0.5f * (low + high);
        if (std::atan2(upFor(mid), mid) > steepest)
          low = mid;
        else
          high = mid;
      }
      along = high;
      up = upFor(high);
    }
    else
    {
      // Out of his reach within the angle: his fastest, at the angle.
      along = most * std::cos(steepest);
      up = most * std::sin(steepest);
    }
    return strikeContactBinturong(
        passer, kind, curlAimBinturong(target, curlAngle(passDistance, along, up, startMetres, meant)),
        along, up, 1.0f, meant, spinCapacity);
  }
  // Struck at, and aimed off for its curl; it curls by accident as a
  // ground pass does, or (`moreCurl`) as a lofted one.
  const auto strike = [&](float along, float up, bool moreCurl)
  {
    Kick kick = strikeContactBinturong(
        passer, kind,
        curlAimBinturong(target, curlAngle(passDistance, along, up,
                                           startMetres, meant)),
        along, up, 1.0f, meant, spinCapacity);
    kick.spin = moreCurl ? curveOf(kick, P::LOFTED_ACCIDENTAL_CURVE_CHANCE,
                                   P::LOFTED_MAX_CURVE)
                         : curveOf(kick, P::ACCIDENTAL_CURVE_CHANCE,
                                   P::MAX_CURVE);
    return kick;
  };
  if (kind == StrikeKind::DRIVEN_PASS)
  {
    // Low and hard with the instep: at a set share of his power, launched
    // just enough to come down at the target; a long one, needing more
    // than the driven angle at that pace, at his limit as flat as gets it
    // there (a flat arc).
    float along = power * P::DRIVEN_SPEED_SHARE;
    float up = loftVerticalSpeed(passDistance, along, startMetres, 0.0f, meant);
    if (std::atan2(up, along) >
            P::DRIVEN_MAX_ANGLE_DEGREES * std::numbers::pi_v<float> / 180.0f ||
        std::hypot(along, up) > power)
      std::tie(along, up) =
          fullPowerLoftBinturong(passDistance, power, startMetres, 0.0f, meant);
    return strike(along, up, false);
  }
  if (kind == StrikeKind::CHIP)
  {
    // Stabbed up steeply: to drop at the target (chest height to a
    // team-mate), clearing someone close, at no more than a stab's power.
    const float arrivalMetres =
        receiver ? P::LOFTED_ARRIVAL_HEIGHT_METRES : 0.0f;
    const auto [along, up] =
        chipLaunchBinturong(passDistance, power * P::CHIP_POWER_SHARE,
                            startMetres, arrivalMetres, meant);
    return strike(along, up, true);
  }
  if (lofted)
  {
    // Launched to drop at the target: long passes at chest height to be
    // controlled, crosses at head height to be attacked, a pass to a spot
    // with nobody there (a drill's) onto the ground; from the height the
    // ball is struck at.
    const float arrivalMetres =
        !receiver  ? 0.0f
        : delivery ? MatchTuning::Aerial::ARRIVAL_HEIGHT_METRES
                   : MatchTuning::Passing::LOFTED_ARRIVAL_HEIGHT_METRES;
    float along = MatchTuning::Passing::LOFTED_BASE_SPEED +
                  passDistance * MatchTuning::Passing::LOFTED_SPEED_PER_METRE;
    float up = loftVerticalSpeed(passDistance, along, startMetres,
                                 arrivalMetres, meant);
    // Beyond his power: struck at his limit, as flat as gets it there, or
    // as far as he can send it, short.
    if (std::hypot(along, up) > power)
      std::tie(along, up) = fullPowerLoftBinturong(passDistance, power,
                                                   startMetres, arrivalMetres,
                                                   meant);
    Kick kick = strikeContactBinturong(
        passer, kind,
        curlAimBinturong(target, curlAngle(passDistance, along, up,
                                           startMetres, meant)),
        along, up, 1.0f, meant, spinCapacity);
    // A lofted pass (a keeper's punt too) curls as a ground pass does,
    // more; crosses only by the spin he means, for now.
    if (kind == StrikeKind::LOFTED_PASS)
      kick.spin =
          curveOf(kick, MatchTuning::Passing::LOFTED_ACCIDENTAL_CURVE_CHANCE,
                  MatchTuning::Passing::LOFTED_MAX_CURVE);
    return kick;
  }
  // Along the ground, to arrive at the pace the brain asked for; without
  // one, weighted to arrive at a controllable pace: longer passes and
  // better passers zip it in harder.
  const float arrival = arrivalSpeed.value_or(
      MatchTuning::Passing::ARRIVAL_SPEED_BASE +
      passer.passing * MatchTuning::Passing::ARRIVAL_SPEED_PASSING +
      passDistance * MatchTuning::Passing::ARRIVAL_SPEED_PER_METRE);
  // A kick is held to what the side of his foot can do with his kick
  // power, a throw to the throw's own limit.
  const float speed = std::clamp(groundLaunchSpeed(passDistance, arrival),
                                 MatchTuning::Passing::MIN_GROUND_SPEED,
                                 thrown ? MatchTuning::Passing::MAX_GROUND_SPEED
                                        : power * P::SIDE_FOOT_POWER_SHARE);
  Kick kick = strikeContactBinturong(
      passer, kind,
      curlAimBinturong(target, curlAngle(passDistance, speed, 0.0f, 0.0f, meant)),
      speed, 0.0f, 1.0f, meant, spinCapacity);
  kick.spin = curveOf(kick, MatchTuning::Passing::ACCIDENTAL_CURVE_CHANCE,
                      MatchTuning::Passing::MAX_CURVE);
  return kick;
}

float MatchEngine::recoverySecondsBinturong(const MatchPlayer& /*player*/,
                                            Recovery after)
{
  // How long before he can act again.
  switch (after)
  {
    case Recovery::SHOT:
      return randomFloat(MatchTuning::Shooting::MIN_ACTION_COOLDOWN,
                         MatchTuning::Shooting::MAX_ACTION_COOLDOWN);
    case Recovery::PASS:
      break;
  }
  return randomFloat(MatchTuning::Passing::MIN_ACTION_COOLDOWN,
                     MatchTuning::Passing::MAX_ACTION_COOLDOWN);
}

float MatchEngine::shotSpreadBinturong(const MatchPlayer& /*shooter*/,
                                       bool /*header*/, bool penalty,
                                       bool freeKick, float /*metres*/) const
{
  // Not metres for this body: a multiplier on the shot contact's angle
  // error (its jitter and fixed miss). The contact itself reads Shooting,
  // power, pressure and the rest; distance is geometry. A penalty is a dead
  // ball struck with all the time he wants; a free kick is placed with care.
  // (No match calibration: shooting technique plan step 3.)
  float precision =
      penalty ? MatchTuning::Shooting::PENALTY_ERROR_SCALE : 1.0f;
  if (freeKick) precision *= MatchTuning::SetPiece::FREE_KICK_ERROR_SCALE;
  return precision;
}

namespace
{
/** Where a ball launched from `origin` (metres) along `heading`
 * (metric, unit), `along` m/s over the ground and `up` m/s upward, from
 * `startHeight` metres, crosses the goal line at `goalX`: sideways from the
 * goal's centre (m) and height (m), with the match's flight physics. A ball
 * that stops short is on the ground there. */
struct GoalLineCrossing
{
  float sideways = 0.0f;
  float height = 0.0f;
  /** It gets there (rather than stopping short or going the other way). */
  bool reaches = false;
};
GoalLineCrossing predictGoalLineCrossing(const MatchEngine& engine,
                                         PitchPoint origin, Vector2F heading,
                                         float along, float up,
                                         float startHeight, float goalX,
                                         BallSpin spin = {})
{
  // Flown with its spin: sidespin turns its heading as it goes (a curled
  // shot), topspin and backspin bring it down or hold it up.
  using P = MatchTuning::Pitch;
  const MatchPitch& pitch = engine.getPitch();
  constexpr float DT = MatchTuning::Timing::FIXED_STEP_SECONDS /
                       static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
  const float lineMetres = goalX - origin.x;
  const float sidewaysAtOrigin = origin.y - pitch.atY(P::CENTRE);
  GoalLineCrossing crossing;
  // Struck away from the goal line: far wide, on the ground.
  if (heading.x * lineMetres <= EPSILON)
  {
    crossing.sideways = heading.y >= 0.0f ? pitch.width : -pitch.width;
    return crossing;
  }
  // A ball that stops short: where its line meets the goal line, on the
  // ground there.
  crossing.sideways = sidewaysAtOrigin + heading.y * lineMetres / heading.x;
  FlightState flight{along, up, startHeight, false, spin};
  float x = 0.0f;
  float y = 0.0f;
  Vector2F way = heading;
  for (float time = 0.0f; time < MatchTuning::Ball::MAX_FLIGHT_SECONDS &&
                          flight.horizontalSpeed > 0.0f;
       time += DT)
  {
    const float previousHeight = flight.height;
    const float step = engine.stepFlight(flight, DT);
    const float nextX = x + way.x * step;
    const float nextY = y + way.y * step;
    if ((nextX - lineMetres) * (x - lineMetres) <= 0.0f && nextX != x)
    {
      const float share = (lineMetres - x) / (nextX - x);
      crossing.sideways = sidewaysAtOrigin + y + (nextY - y) * share;
      crossing.height =
          previousHeight + (flight.height - previousHeight) * share;
      crossing.reaches = true;
      return crossing;
    }
    x = nextX;
    y = nextY;
    const float turn = flight.turnRate * DT;
    way = {way.x * std::cos(turn) - way.y * std::sin(turn),
           way.x * std::sin(turn) + way.y * std::cos(turn)};
  }
  return crossing;
}
}  // namespace

MatchEngine::ShotStrike MatchEngine::strikeShotBodyBinturong(
    const MatchPlayer& shooter, float aimY, float aimZ, float spread,
    bool header, float powerScale, std::optional<ShotTechnique> technique)
{
  // The shot is a contact (contact plan step 3c): the spot he picked, at
  // the pace he means, launched to cross the line at the height he aimed
  // for; then struck with the profile of how he strikes it (a header's, or
  // a foot shot's type), its angle error scaled by `spread`
  // (shotSpreadBinturong: a dead ball, play mode's power). Where it crosses
  // the line is where that strike's flight takes it, spin and all, and the
  // ball is launched exactly so.
  using Sh = MatchTuning::Shooting;
  constexpr float REV = 2.0f * std::numbers::pi_v<float>;
  const ShotTechnique how = technique.value_or(ShotTechnique::DRIVEN);
  const StrikeKind kind = header                           ? StrikeKind::HEADER
                          : how == ShotTechnique::PLACED   ? StrikeKind::PLACED_SHOT
                          : how == ShotTechnique::CURLED   ? StrikeKind::CURLED_SHOT
                          : how == ShotTechnique::CHIP     ? StrikeKind::CHIP_SHOT
                          : how == ShotTechnique::TOE_POKE ? StrikeKind::TOE_POKE
                                                           : StrikeKind::SHOT;
  const float goalX = pitch.atX(shooter.isHomeTeam ? 1.0f : 0.0f);
  const PitchPoint origin = ball.position;
  const PitchPoint aimPoint{goalX, pitch.atY(MatchTuning::Pitch::CENTRE) +
                                       aimY};
  // From the height the ball is at, as it is released (releaseFromBinturong).
  const float startHeight = ball.z;
  const float metres = distance(origin, aimPoint);
  // The spin he means, no more than he can put on: a little topspin on a
  // driven shot, backspin on a chip, sidespin on a curled one, bent in
  // towards the middle of the goal (from his right of it to his left, and
  // the other way). A header has none.
  BallSpin meant{};
  const float spinCapacity = header ? 0.0f : shotSpinCapacityBinturong(shooter);
  if (!header)
  {
    switch (how)
    {
      case ShotTechnique::DRIVEN:
        meant = {0.0f, Sh::DRIVEN_TOPSPIN_REVS * REV};
        break;
      case ShotTechnique::CURLED:
        meant = {(aimY >= 0.0f ? -1.0f : 1.0f) * Sh::CURLED_SIDESPIN_REVS * REV, 0.0f};
        break;
      case ShotTechnique::CHIP:
        meant = {0.0f, -Sh::CHIP_BACKSPIN_REVS * REV};
        break;
      case ShotTechnique::PLACED:
      case ShotTechnique::TOE_POKE:
        break;
    }
    if (const float asked = std::hypot(meant.side, meant.top); asked > spinCapacity)
    {
      const float share = spinCapacity / asked;
      meant = {meant.side * share, meant.top * share};
    }
  }
  float speed = 0.0f;
  float up = 0.0f;
  if (header)
  {
    // A share of the fastest he can head it on the shot's line (heading
    // technique plan step 3; play mode's power scales the share), as its
    // whole launch speed: a few rounds settle the lift and the pace.
    const float share =
        std::min(MatchTuning::Aerial::HEADER_SHOT_SHARE * powerScale, 1.0f);
    const Vector2F toGoal = metricDirection(origin, aimPoint);
    speed = share * headerCeilingBinturong(shooter, toGoal, 0.0f);
    for (int round = 0; round < 4; ++round)
    {
      up = loftVerticalSpeed(metres, speed, startHeight, aimZ);
      const float launch =
          share * headerCeilingBinturong(shooter, toGoal, std::atan2(up, speed));
      speed = std::sqrt(std::max(launch * launch - up * up, 0.25f * launch * launch));
    }
    up = loftVerticalSpeed(metres, speed, startHeight, aimZ);
  }
  else
  {
    // His power at this moment (less what turning a moving ball costs him)
    // and the share of it his part of the foot can use.
    const float budget = incomingPowerBudgetBinturong(
        shotPowerBinturong(shooter), metricDirection(origin, aimPoint));
    const float footShare =
        how == ShotTechnique::PLACED     ? Sh::PLACED_FOOT_SHARE
        : how == ShotTechnique::CURLED   ? Sh::CURLED_FOOT_SHARE
        : how == ShotTechnique::CHIP     ? Sh::CHIP_FOOT_SHARE
        : how == ShotTechnique::TOE_POKE ? Sh::TOE_FOOT_SHARE
                                         : 1.0f;
    if (how == ShotTechnique::CHIP)
    {
      // Over the keeper where he stands (his reach, and a margin), coming
      // down through the height aimed for on the line; else as a chip pass
      // clears someone close. No faster than the stab allows: a long one
      // falls short.
      float clearMetres = -1.0f;
      float clearHeight = -1.0f;
      const Vector2F toGoal = metricDirection(origin, aimPoint);
      for (const MatchPlayer& keeper : players)
      {
        if (!active(keeper) || !keeper.isGoalkeeper ||
            keeper.isHomeTeam == shooter.isHomeTeam)
          continue;
        const Vector2F gap = keeper.position - origin;
        const float out = gap.x * toGoal.x + gap.y * toGoal.y;
        if (out > 1.0f && out < metres - 0.5f)
        {
          clearMetres = out;
          clearHeight = MatchRules::goalkeeperReachMetres(keeper.heightMetres,
                                                          keeper.goalkeeping) +
                        Sh::CHIP_KEEPER_MARGIN_METRES;
        }
      }
      std::tie(speed, up) =
          chipLaunchBinturong(metres, budget * footShare, startHeight, aimZ,
                              meant, clearMetres, clearHeight);
    }
    else
    {
      // A share of his shot power (the type's, or play mode's or a drill's
      // as a scale on the driven one's), no more than the foot allows, as
      // the ball's whole launch speed: what goes into the lift comes off its
      // pace over the ground. A few rounds settle the two (the lift is
      // small against the pace).
      const float typeShare =
          how == ShotTechnique::PLACED     ? Sh::PLACED_POWER_SHARE
          : how == ShotTechnique::CURLED   ? Sh::CURLED_POWER_SHARE
          : how == ShotTechnique::TOE_POKE ? Sh::TOE_POWER_SHARE
                                           : Sh::DEFAULT_POWER_SHARE;
      const float share =
          powerScale == 1.0f ? typeShare : Sh::DEFAULT_POWER_SHARE * powerScale;
      const float launch = budget * std::min(footShare, share);
      speed = launch;
      for (int round = 0; round < 4; ++round)
      {
        up = loftVerticalSpeed(metres, speed, startHeight, aimZ, meant);
        speed = std::sqrt(std::max(launch * launch - up * up, 0.25f * launch * launch));
      }
      up = loftVerticalSpeed(metres, speed, startHeight, aimZ, meant);
      // Too weak a strike to get there on a shot's line (it would have to
      // be lobbed): at that pace, the flattest angle that gets there, or the
      // one that gets furthest (short), as a pass beyond his power.
      if (std::hypot(speed, up) > launch * 1.01f ||
          up > launch * std::sin(Sh::MAX_LAUNCH_DEGREES *
                                 std::numbers::pi_v<float> / 180.0f))
        std::tie(speed, up) =
            fullPowerLoftBinturong(metres, launch, startHeight, aimZ, meant);
    }
  }
  // Aimed off for the curl he means, so it bends onto the spot.
  const PitchPoint contactAim =
      meant.side != 0.0f
          ? curlAimBinturong(aimPoint,
                             curlAngle(metres, speed, up, startHeight, meant))
          : aimPoint;
  const Kick kick = strikeContactBinturong(shooter, kind, contactAim, speed, up,
                                           spread, meant, spinCapacity);
  const Vector2F heading = metricDirection(origin, kick.target);
  const GoalLineCrossing crossing =
      predictGoalLineCrossing(*this, origin, heading, kick.speed,
                              kick.verticalSpeed, startHeight, goalX, kick.spin);
  ShotStrike strike;
  strike.crossingY = crossing.sideways;
  strike.crossingZ = crossing.height;
  strike.speed = kick.speed;
  strike.verticalSpeed = kick.verticalSpeed;
  strike.launchSet = true;
  strike.spin = kick.spin;
  // A curled ball leaves along its first heading, not at where it crosses.
  if (kick.spin.side != 0.0f) strike.launchToward = kick.target;
  return strike;
}

void MatchEngine::readShotBinturong(const MatchPlayer& keeper,
                                    GoalkeeperControl& control)
{
  // Predict where the shot crosses the keeper's plane and react after a
  // skill-dependent delay; defenders between ball and goal screen the shot.
  const float goalX = pitch.atX(keeper.isHomeTeam ? 0.0f : 1.0f);
  const float span = goalX - ball.position.x;
  const float t =
      std::abs(span) > EPSILON
          ? std::clamp((keeper.position.x - ball.position.x) / span, 0.0f, 1.0f)
          : 1.0f;
  const float predictedY =
      ball.position.y + (getPossessionState().shotTargetY - ball.position.y) * t;
  const float readError =
      gaussian(rng) * MatchTuning::Goalkeeper::READ_ERROR_METRES *
                        (1.0f - keeper.goalkeeping * 0.6f);
  control.diveTargetY = std::clamp(
      predictedY + readError,
      pitch.atY(MatchTuning::Pitch::GOAL_TOP - MatchTuning::Goalkeeper::DIVE_POST_MARGIN),
      pitch.atY(MatchTuning::Pitch::GOAL_BOTTOM +
                MatchTuning::Goalkeeper::DIVE_POST_MARGIN));

  bool screened = false;
  for (const auto& other : players)
  {
    if (!active(other) || other.isGoalkeeper) continue;
    const float along = (other.position.x - ball.position.x) / span;
    if (along <= 0.05f || along >= 0.95f) continue;
    const float laneY =
        ball.position.y + (getPossessionState().shotTargetY - ball.position.y) * along;
    if (std::abs(other.position.y - laneY) <
        MatchTuning::Goalkeeper::SCREEN_WIDTH_METRES)
    {
      screened = true;
      break;
    }
  }
  const float reactionSeconds =
      MatchTuning::Goalkeeper::REACTION_BASE_SECONDS +
      (1.0f - keeper.goalkeeping) *
          MatchTuning::Goalkeeper::REACTION_SKILL_SECONDS +
      (screened ? MatchTuning::Goalkeeper::SCREENED_REACTION_SECONDS : 0.0f);
  control.reactionRemaining = reactionSeconds;
}

void MatchEngine::diveGoalkeeperBinturong(MatchPlayer& keeper, float dt)
{
  // Goalkeeper plan step 3: set for a shot he stands, hands only, until he
  // has reacted; then he dives if its line passes beyond his standing
  // reach. A dive, once he has pushed off, runs to the floor and up again
  // whatever the ball does.
  GoalkeeperControl& control = keepers[keeper.isHomeTeam ? 0 : 1];
  control.timer += dt;
  KeeperDive& dive = control.dive;
  const double now = getSimulatedSeconds();
  if (dive.active)
  {
    // His body over the ground under his hips.
    const KeeperDivePose pose = keeperDivePoseBinturong(keeper, dive, now);
    const PitchPoint was = keeper.position;
    keeper.position = moveBody(keeper.position, asPoint({pose.hip.x, pose.hip.y}));
    const Vector2F moved = keeper.position - was;
    keeper.velocity = {moved.x / std::max(dt, EPSILON), moved.y / std::max(dt, EPSILON)};
    keeper.movementTarget = keeper.position;
    keeper.isDiving = true;
    // A smother (step 8): a ball he comes down on (still, or too slow
    // against him for the sweep to meet) is in his midriff or arms: he
    // wraps it up as any ball his midriff takes.
    if (dive.smother && now >= dive.from && state == MatchState::PLAYING && !getPossessionState().isShot &&
        getPossessionAttachment() != keeper.player && (!getPossessionAttachment() || hasTouchClaim()))
    {
      const Vector2F atMetres = asVector(ball.position);
      const Point3F at{atMetres.x, atMetres.y, ball.z};
      const float radius = MatchTuning::Units::BALL_RADIUS_METRES;
      const Point3F axis{pose.neck.x - pose.hip.x, pose.neck.y - pose.hip.y, pose.neck.z - pose.hip.z};
      const float axisSquared = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
      const float t =
          axisSquared > EPSILON
              ? std::clamp(((at.x - pose.hip.x) * axis.x + (at.y - pose.hip.y) * axis.y +
                            (at.z - pose.hip.z) * axis.z) /
                               axisSquared,
                           0.0f, 1.0f)
              : 0.0f;
      const Point3F out{at.x - (pose.hip.x + axis.x * t), at.y - (pose.hip.y + axis.y * t),
                        at.z - (pose.hip.z + axis.z * t)};
      const float gap = std::hypot(out.x, out.y, out.z);
      bool inHands = false;
      for (const Point3F& hand : pose.hands)
        if (std::hypot(at.x - hand.x, at.y - hand.y, at.z - hand.z) <=
            MatchTuning::KeeperHands::HAND_RADIUS_METRES + radius)
          inHands = true;
      if (gap <= MatchTuning::Body::RADIUS_METRES + radius || inHands)
      {
        ShotBodyHit hit;
        hit.part = ShotBodyPart::TORSO;
        hit.normal = gap > 1e-5f ? Point3F{out.x / gap, out.y / gap, out.z / gap}
                                 : Point3F{dive.faceTo.x, dive.faceTo.y, 0.0f};
        hit.surfaceVelocity = {keeper.velocity.x, keeper.velocity.y, 0.0f};
        const auto slot = static_cast<std::size_t>(&keeper - players.data());
        if (hasTouchClaim()) clearPossession();
        ballMeetsBodyBinturong(keeper, slot, hit);
      }
    }
    if (now >= dive.from + static_cast<double>(dive.stretchSeconds + dive.fallSeconds +
                                               dive.downSeconds))
    {
      // Back on his feet where his hips were.
      dive.active = false;
      control.state = GoalkeeperState::SET_POSITION;
      control.timer = 0.0f;
      keeper.velocity = {0.0f, 0.0f};
      keeper.isDiving = false;
      keeper.diveTimer = 0.0f;
    }
    return;
  }
  keeper.velocity = {0.0f, 0.0f};
  keeper.movementTarget = keeper.position;
  if (!getPossessionState().isShot || state != MatchState::PLAYING)
  {
    control.state = GoalkeeperState::SET_POSITION;
    control.timer = 0.0f;
    return;
  }
  if (dive.decidedFor == getPossessionState().pathSince) return;
  const float reflexes = std::clamp(
      (keeper.derivedStat(DerivedStat::REFLEXES) - STAT_1_RATING) / (STAT_99_RATING - STAT_1_RATING),
      0.0f, 1.0f);
  using H = MatchTuning::KeeperHands;
  const double reacted =
      getPossessionState().pathSince +
      (H::REACTION_SECONDS_POOR + (H::REACTION_SECONDS_BEST - H::REACTION_SECONDS_POOR) * reflexes);
  if (now < reacted) return;
  dive.decidedFor = getPossessionState().pathSince;
  planKeeperDiveBinturong(keeper, dive, reacted);
}

bool MatchEngine::keeperDiving(const MatchPlayer& player) const
{
  if (!player.isGoalkeeper) return false;
  const KeeperDive& dive = keepers[player.isHomeTeam ? 0 : 1].dive;
  return dive.active && ballNow() >= dive.from;
}

void MatchEngine::planKeeperDiveBinturong(MatchPlayer& keeper, KeeperDive& dive, double from,
                                          std::optional<Point3F> forced,
                                          std::optional<double> smotherBallThere)
{
  using D = MatchTuning::KeeperDive;
  using H = MatchTuning::KeeperHands;
  using K = MatchTuning::ShotBlock;
  const auto rating = [](float stat)
  { return std::clamp((stat - STAT_1_RATING) / (STAT_99_RATING - STAT_1_RATING), 0.0f, 1.0f); };
  const float reflexes = rating(keeper.derivedStat(DerivedStat::REFLEXES));
  const float agility = rating(keeper.derivedStat(DerivedStat::AGILITY));
  const float height = keeper.heightMetres;
  const Vector2F centre = asVector(keeper.position);
  const Vector2F facing = normalized(
      {std::cos(keeper.facingAngle),
       std::sin(keeper.facingAngle)});
  const Vector2F left{-facing.y, facing.x};
  const float fullReach = D::REACH_HEIGHT_SHARE * height + D::REACH_AGILITY_METRES * agility;
  const float fullStretch =
      D::STRETCH_SECONDS_POOR + (D::STRETCH_SECONDS_BEST - D::STRETCH_SECONDS_POOR) * agility;

  // Where its line passes nearest him: straight on its heading, falling
  // under gravity (its curl and drag not read).
  Point3F aim{};
  double ballThere = getSimulatedSeconds();
  if (forced)
  {
    aim = *forced;
    // A smother (step 8): the ball there then, and he aims with his miss.
    if (smotherBallThere) ballThere = *smotherBallThere;
  }
  else
  {
    if (!getPossessionState().shotOnTarget) return;
    const Vector2F at = asVector(ball.position);
    const Vector2F velocity = ball.velocity;
    const float speedSquared = velocity.x * velocity.x + velocity.y * velocity.y;
    if (speedSquared <= 1e-4f) return;
    const float until = ((centre.x - at.x) * velocity.x + (centre.y - at.y) * velocity.y) / speedSquared;
    if (until <= 0.0f) return;
    const float z = ball.z + ball.velocityZ * until -
                    0.5f * MatchTuning::Ball::GRAVITY * until * until;
    aim = {at.x + velocity.x * until - centre.x, at.y + velocity.y * until - centre.y,
           std::max(z, MatchTuning::Units::BALL_RADIUS_METRES)};
    ballThere += static_cast<double>(until);
    // Within his standing reach his hands are enough: no dive.
    const std::size_t near = aim.x * left.x + aim.y * left.y >= 0.0f ? 0 : 1;
    const Point3F standing = keeperHandReachBinturong(keeper, near, aim);
    if (std::hypot(standing.x - aim.x, standing.y - aim.y, standing.z - aim.z) < 0.02f) return;
  }

  // His run carries him on as he dives (step 8): aimed from where it will
  // have taken him by full stretch.
  dive.carry = keeper.velocity;
  if (length(dive.carry) > EPSILON)
  {
    const float seconds =
        fullStretch * std::clamp(std::hypot(aim.x, aim.y) / std::max(fullReach, EPSILON),
                                 D::SHORTEST_STRETCH_SHARE, 1.0f);
    aim.x -= dive.carry.x * seconds;
    aim.y -= dive.carry.y * seconds;
  }

  // The miss, as his hands' (Reflexes; bigger when rushed).
  const float sideways = std::hypot(aim.x, aim.y);
  const float stretchSeconds =
      fullStretch * std::clamp(sideways / std::max(fullReach, EPSILON), D::SHORTEST_STRETCH_SHARE, 1.0f);
  dive.spareSeconds = static_cast<float>(ballThere - (from + stretchSeconds));
  if (!forced || smotherBallThere)
  {
    const float rushed = std::clamp(1.0f - dive.spareSeconds / K::RUSHED_SECONDS, 0.0f, 1.0f);
    dive.errorRadius = (H::MISS_METRES_POOR + (H::MISS_METRES_BEST - H::MISS_METRES_POOR) * reflexes) *
                       (1.0f + (K::RUSHED_ERROR_FACTOR - 1.0f) * rushed);
    Point3F miss{};
    for (int attempt = 0; attempt < 16; ++attempt)
    {
      miss = {randomFloat(-1.0f, 1.0f), randomFloat(-1.0f, 1.0f), randomFloat(-1.0f, 1.0f)};
      if (miss.x * miss.x + miss.y * miss.y + miss.z * miss.z <= 1.0f) break;
    }
    if (const float size = std::hypot(miss.x, miss.y, miss.z); size > 1.0f)
      miss = {miss.x / size, miss.y / size, miss.z / size};
    aim = {aim.x + miss.x * dive.errorRadius, aim.y + miss.y * dive.errorRadius,
           std::max(H::HAND_RADIUS_METRES, aim.z + miss.z * dive.errorRadius)};
    dive.error = dive.errorRadius * std::hypot(miss.x, miss.y, miss.z);
  }

  // The dive: his feet out along the way he goes (as far as his Agility
  // takes them) and up (his jump, Aerial Reach less his standing reach),
  // his body tilted from them to put his hand on the aim, arms straight on
  // from his head.
  const float horizontal = std::hypot(aim.x, aim.y);
  dive.along = horizontal > 1e-3f ? Vector2F{aim.x / horizontal, aim.y / horizontal} : facing;
  dive.across = {-dive.along.y, dive.along.x};
  const float armLine = H::STANDING_REACH_HEIGHT_SHARE * height - H::HAND_RADIUS_METRES;
  const float jump = std::max(
      0.0f, derivedPhysicalValue(DerivedStat::AERIAL_REACH, keeper.derivedStat(DerivedStat::AERIAL_REACH)) -
                H::STANDING_REACH_HEIGHT_SHARE * height);
  const float feetOutMost = std::max(0.0f, fullReach - H::STANDING_REACH_HEIGHT_SHARE * height);
  dive.feetUp = std::clamp(aim.z - armLine, 0.0f, jump);
  const float rise = aim.z - dive.feetUp;
  dive.feetOut = std::clamp(horizontal - std::sqrt(std::max(0.0f, armLine * armLine - rise * rise)),
                            0.0f, feetOutMost);
  const float out = std::max(0.0f, horizontal - dive.feetOut);
  dive.tilt = std::clamp(std::atan2(out, std::max(rise, 0.0f)), 0.0f, std::numbers::pi_v<float> / 2.0f);
  const float handLine = std::min(std::hypot(out, rise), armLine);
  // Both hands where the far one gets there too (a shoulder's width short
  // of his longest reach); else the near one there and the other short.
  const float shoulderOff = H::SHOULDER_OFF_CENTRE_SHARE * height;
  dive.together = std::hypot(out, rise) <= armLine - shoulderOff;
  dive.near = dive.along.x * left.x + dive.along.y * left.y >= 0.0f ? 0 : 1;
  const Point3F feetEnd{centre.x + dive.along.x * dive.feetOut, centre.y + dive.along.y * dive.feetOut,
                        dive.feetUp};
  const Point3F axis{dive.along.x * std::sin(dive.tilt), dive.along.y * std::sin(dive.tilt),
                     std::cos(dive.tilt)};
  const auto onAxis = [&](float metres, float sideMetres)
  {
    return Point3F{feetEnd.x + axis.x * metres + dive.across.x * sideMetres,
                   feetEnd.y + axis.y * metres + dive.across.y * sideMetres,
                   feetEnd.z + axis.z * metres};
  };
  // The near hand on the side of the pair the way he faces it (across is
  // his left as he dives).
  const float nearSide = dive.near == 0 ? 1.0f : -1.0f;
  const float half = 0.5f * H::HANDS_APART_METRES;
  if (dive.together)
  {
    dive.handEnd[dive.near] = onAxis(handLine, nearSide * half);
    dive.handEnd[1 - dive.near] = onAxis(handLine, -nearSide * half);
  }
  else
  {
    dive.handEnd[dive.near] = onAxis(handLine, 0.0f);
    dive.handEnd[1 - dive.near] = onAxis(handLine - shoulderOff, -nearSide * shoulderOff);
  }
  // From where his hands are now.
  const auto slot = static_cast<std::size_t>(&keeper - players.data());
  const ShotHands* hands = slot < shotLegPoses.size() ? &shotLegPoses[slot].hands : nullptr;
  for (std::size_t index = 0; index < 2; ++index)
  {
    const float side = index == 0 ? 1.0f : -1.0f;
    dive.handStart[index] =
        hands && hands->active
            ? hands->at[index]
            : Point3F{facing.x * H::READY_AHEAD_METRES + left.x * side * H::READY_SIDE_METRES,
                      facing.y * H::READY_AHEAD_METRES + left.y * side * H::READY_SIDE_METRES,
                      H::READY_HEIGHT_SHARE * height};
  }

  dive.active = true;
  dive.smother = false;
  dive.from = from;
  dive.origin = centre;
  dive.stretchSeconds = stretchSeconds;
  // Then he falls from full stretch until the first of him reaches the
  // floor, and lies there until he is up (Agility).
  dive.dropMetres = 0.0f;
  dive.fallSeconds = 0.0f;
  {
    const KeeperDivePose pose = keeperDivePoseBinturong(keeper, dive, from + stretchSeconds);
    const float torso = MatchTuning::Body::RADIUS_METRES;
    float drop = std::min({pose.hip.z - torso, pose.neck.z - torso, pose.top.z - torso,
                           pose.feet.z - D::LEGS_RADIUS_METRES, pose.hands[0].z - H::HAND_RADIUS_METRES,
                           pose.hands[1].z - H::HAND_RADIUS_METRES});
    drop = std::max(0.0f, drop);
    dive.dropMetres = drop;
    dive.fallSeconds = std::sqrt(2.0f * drop / MatchTuning::Ball::GRAVITY);
  }
  dive.downSeconds =
      D::DOWN_SECONDS_POOR + (D::DOWN_SECONDS_BEST - D::DOWN_SECONDS_POOR) * agility;
  ++stats.keeperDives;
  if (dive.spareSeconds < 0.0f) ++stats.keeperDivesLate;
}

float MatchEngine::smotherReachBinturong(const MatchPlayer& keeper) const
{
  using D = MatchTuning::KeeperDive;
  using H = MatchTuning::KeeperHands;
  const float height = keeper.heightMetres;
  const float agility =
      std::clamp((keeper.derivedStat(DerivedStat::AGILITY) - STAT_1_RATING) /
                     (STAT_99_RATING - STAT_1_RATING),
                 0.0f, 1.0f);
  const float fullReach = D::REACH_HEIGHT_SHARE * height + D::REACH_AGILITY_METRES * agility;
  const float feetOutMost = std::max(0.0f, fullReach - H::STANDING_REACH_HEIGHT_SHARE * height);
  const BodyLines lines = bodyLinesBinturong(keeper);
  return feetOutMost + 0.5f * (lines.chest + lines.head);
}

void MatchEngine::planKeeperSmotherBinturong(MatchPlayer& keeper, KeeperDive& dive, double from,
                                             Point3F at, Vector2F ballWay, double ballThere)
{
  // Goalkeeper plan step 8 (user 2026-11-09): a smother reaches the ball
  // with his midriff, his arms wrapping it as it is cushioned. He goes down
  // side-on across its line, chest to the ball, over the dive's time for
  // that distance (Agility); his run carries him on; aimed with his miss.
  using D = MatchTuning::KeeperDive;
  using H = MatchTuning::KeeperHands;
  using K = MatchTuning::ShotBlock;
  const float agility =
      std::clamp((keeper.derivedStat(DerivedStat::AGILITY) - STAT_1_RATING) /
                     (STAT_99_RATING - STAT_1_RATING),
                 0.0f, 1.0f);
  const float reflexes =
      std::clamp((keeper.derivedStat(DerivedStat::REFLEXES) - STAT_1_RATING) /
                     (STAT_99_RATING - STAT_1_RATING),
                 0.0f, 1.0f);
  const float fullStretch =
      D::STRETCH_SECONDS_POOR + (D::STRETCH_SECONDS_BEST - D::STRETCH_SECONDS_POOR) * agility;
  const float reach = smotherReachBinturong(keeper);
  const Vector2F centre = asVector(keeper.position);
  const auto secondsFor = [&](float metres)
  {
    return fullStretch * std::clamp(metres / std::max(reach, EPSILON), D::SHORTEST_STRETCH_SHARE, 1.0f);
  };
  // From where his run will have carried him by then.
  dive.carry = keeper.velocity;
  Vector2F aim{at.x, at.y};
  {
    const float seconds = secondsFor(std::hypot(aim.x, aim.y));
    aim = {aim.x - dive.carry.x * seconds, aim.y - dive.carry.y * seconds};
  }
  const float seconds = std::min(secondsFor(std::hypot(aim.x, aim.y)), fullStretch);
  dive.spareSeconds = static_cast<float>(ballThere - (from + seconds));
  // The miss, as his dive's (Reflexes; bigger when rushed), over the ground.
  const float rushed = std::clamp(1.0f - dive.spareSeconds / K::RUSHED_SECONDS, 0.0f, 1.0f);
  dive.errorRadius = (H::MISS_METRES_POOR + (H::MISS_METRES_BEST - H::MISS_METRES_POOR) * reflexes) *
                     (1.0f + (K::RUSHED_ERROR_FACTOR - 1.0f) * rushed);
  Vector2F miss{};
  for (int attempt = 0; attempt < 16; ++attempt)
  {
    miss = {randomFloat(-1.0f, 1.0f), randomFloat(-1.0f, 1.0f)};
    if (miss.x * miss.x + miss.y * miss.y <= 1.0f) break;
  }
  aim = {aim.x + miss.x * dive.errorRadius, aim.y + miss.y * dive.errorRadius};
  dive.error = dive.errorRadius * length(miss);
  // Across its line, his feet on the side he comes from; chest to the ball.
  const Vector2F way = length(ballWay) > EPSILON ? normalized(ballWay) : Vector2F{1.0f, 0.0f};
  Vector2F lie{-way.y, way.x};
  if (lie.x * aim.x + lie.y * aim.y < 0.0f) lie = {-lie.x, -lie.y};
  dive.smother = true;
  dive.active = true;
  dive.from = from;
  dive.origin = centre;
  dive.midriffAt = {centre.x + aim.x, centre.y + aim.y};
  dive.lieAlong = lie;
  dive.faceTo = {-way.x, -way.y};
  const float horizontal = std::hypot(aim.x, aim.y);
  dive.along = horizontal > 1e-3f ? Vector2F{aim.x / horizontal, aim.y / horizontal} : lie;
  dive.across = {-dive.along.y, dive.along.x};
  dive.stretchSeconds = seconds;
  // Down on the floor at full stretch: no fall after it.
  dive.dropMetres = 0.0f;
  dive.fallSeconds = 0.0f;
  dive.downSeconds = D::DOWN_SECONDS_POOR + (D::DOWN_SECONDS_BEST - D::DOWN_SECONDS_POOR) * agility;
  dive.feetOut = 0.0f;
  dive.feetUp = 0.0f;
  dive.tilt = std::numbers::pi_v<float> / 2.0f;
  dive.together = true;
}

KeeperDivePose MatchEngine::keeperDivePoseBinturong(const MatchPlayer& keeper, const KeeperDive& dive,
                                                    double when) const
{
  using D = MatchTuning::KeeperDive;
  using H = MatchTuning::KeeperHands;
  const float height = keeper.heightMetres;
  const float since = static_cast<float>(when - dive.from);
  if (dive.smother)
  {
    // Smoothly from upright where he stood to lying side-on across the
    // ball's line, his midriff on it, his arms tucked in front of his chest.
    const float stretch =
        dive.stretchSeconds > EPSILON ? std::clamp(since / dive.stretchSeconds, 0.0f, 1.0f) : 1.0f;
    const float share = stretch * stretch * (3.0f - 2.0f * stretch);
    const BodyLines lines = bodyLinesBinturong(keeper);
    const float torso = MatchTuning::Body::RADIUS_METRES;
    const float midriffUp = 0.5f * (lines.chest + lines.head);
    const Point3F midriff{dive.origin.x + (dive.midriffAt.x - dive.origin.x) * share,
                          dive.origin.y + (dive.midriffAt.y - dive.origin.y) * share,
                          midriffUp + (torso - midriffUp) * share};
    Point3F axis{dive.lieAlong.x * share, dive.lieAlong.y * share, 1.0f - share};
    const float size = std::hypot(axis.x, axis.y, axis.z);
    axis = {axis.x / size, axis.y / size, axis.z / size};
    const Point3F feet{midriff.x - axis.x * midriffUp, midriff.y - axis.y * midriffUp,
                       midriff.z - axis.z * midriffUp};
    const auto onAxis = [&](float metres)
    { return Point3F{feet.x + axis.x * metres, feet.y + axis.y * metres, feet.z + axis.z * metres}; };
    const auto lift = [](Point3F point, float radius)
    { return Point3F{point.x, point.y, std::max(point.z, radius)}; };
    KeeperDivePose pose;
    pose.feet = lift(feet, D::LEGS_RADIUS_METRES);
    pose.hip = lift(onAxis(lines.chest), torso);
    pose.neck = lift(onAxis(lines.head), torso);
    pose.top = lift(onAxis(height - torso), torso);
    const float shoulderUp = MatchTuning::Goalkeeper::SHOULDER_HEIGHT_SHARE * height;
    const Point3F front{dive.faceTo.x * (torso + H::HAND_RADIUS_METRES),
                        dive.faceTo.y * (torso + H::HAND_RADIUS_METRES), 0.0f};
    for (std::size_t index = 0; index < 2; ++index)
    {
      const float side = index == 0 ? 1.0f : -1.0f;
      pose.shoulders[index] = lift(onAxis(shoulderUp), torso);
      // Tucked: in front of his chest, one a little above the other along
      // his body.
      const Point3F hand = onAxis(midriffUp + side * 0.5f * H::HANDS_APART_METRES);
      pose.hands[index] =
          lift({hand.x + front.x, hand.y + front.y, hand.z + front.z}, H::HAND_RADIUS_METRES);
    }
    shiftDivePoseByRunBinturong(pose, dive, since);
    return pose;
  }
  const float stretch =
      dive.stretchSeconds > EPSILON ? std::clamp(since / dive.stretchSeconds, 0.0f, 1.0f) : 1.0f;
  // Smoothly out and back to rest at full stretch.
  const float share = stretch * stretch * (3.0f - 2.0f * stretch);
  // After full stretch he falls under gravity to the floor.
  const float falling = std::max(0.0f, since - dive.stretchSeconds);
  const float drop = std::min(dive.dropMetres, 0.5f * MatchTuning::Ball::GRAVITY * falling * falling);

  const float tilt = dive.tilt * share;
  const Point3F axis{dive.along.x * std::sin(tilt), dive.along.y * std::sin(tilt), std::cos(tilt)};
  const Point3F feet{dive.origin.x + dive.along.x * dive.feetOut * share,
                     dive.origin.y + dive.along.y * dive.feetOut * share,
                     dive.feetUp * share - drop};
  const auto onAxis = [&](float metres, float sideMetres)
  {
    return Point3F{feet.x + axis.x * metres + dive.across.x * sideMetres,
                   feet.y + axis.y * metres + dive.across.y * sideMetres,
                   feet.z + axis.z * metres};
  };
  KeeperDivePose pose;
  const BodyLines lines = bodyLinesBinturong(keeper);
  // Nothing of him below the floor.
  const auto lift = [](Point3F point, float radius)
  { return Point3F{point.x, point.y, std::max(point.z, radius)}; };
  const float torso = MatchTuning::Body::RADIUS_METRES;
  pose.feet = lift(feet, D::LEGS_RADIUS_METRES);
  pose.hip = lift(onAxis(lines.chest, 0.0f), torso);
  pose.neck = lift(onAxis(lines.head, 0.0f), torso);
  pose.top = lift(onAxis(height - torso, 0.0f), torso);
  const float shoulderOff = H::SHOULDER_OFF_CENTRE_SHARE * height;
  const float shoulderUp = MatchTuning::Goalkeeper::SHOULDER_HEIGHT_SHARE * height;
  for (std::size_t index = 0; index < 2; ++index)
  {
    // His left shoulder on his left as he faced at push-off.
    const Vector2F facing = normalized(
        {std::cos(keeper.facingAngle),
         std::sin(keeper.facingAngle)});
    const Vector2F left{-facing.y, facing.x};
    const float sign = (left.x * dive.across.x + left.y * dive.across.y >= 0.0f ? 1.0f : -1.0f) *
                       (index == 0 ? 1.0f : -1.0f);
    pose.shoulders[index] = lift(onAxis(shoulderUp, sign * shoulderOff), torso);
    const Point3F start{dive.origin.x + dive.handStart[index].x, dive.origin.y + dive.handStart[index].y,
                        dive.handStart[index].z};
    const Point3F end{dive.handEnd[index].x, dive.handEnd[index].y, dive.handEnd[index].z - drop};
    pose.hands[index] = lift({start.x + (end.x - start.x) * share, start.y + (end.y - start.y) * share,
                              start.z + (end.z - start.z) * share},
                             H::HAND_RADIUS_METRES);
  }
  shiftDivePoseByRunBinturong(pose, dive, since);
  return pose;
}

void MatchEngine::shiftDivePoseByRunBinturong(KeeperDivePose& pose, const KeeperDive& dive,
                                              float since) const
{
  // His run carried on (step 8): through the air as it was, then sliding on
  // the floor, slowed by the grass, until it stops him.
  const float speed = length(dive.carry);
  if (speed <= EPSILON || since <= 0.0f) return;
  const float airborne = dive.stretchSeconds + dive.fallSeconds;
  float metres = speed * std::min(since, airborne);
  if (since > airborne)
  {
    const float slowing = MatchTuning::Collision::FALL_DECELERATION;
    const float sliding = std::min(since - airborne, speed / slowing);
    metres += speed * sliding - 0.5f * slowing * sliding * sliding;
  }
  const Vector2F shift{dive.carry.x / speed * metres, dive.carry.y / speed * metres};
  const auto move = [&](Point3F& point)
  {
    point.x += shift.x;
    point.y += shift.y;
  };
  move(pose.feet);
  move(pose.hip);
  move(pose.neck);
  move(pose.top);
  for (std::size_t index = 0; index < 2; ++index)
  {
    move(pose.shoulders[index]);
    move(pose.hands[index]);
  }
}

MatchEngine::SaveAttempt MatchEngine::attemptSaveBinturong(
    const MatchPlayer& keeper)
{
  const float span = ball.position.x - substepBallPosition.x;
  const float t =
      std::abs(span) > EPSILON
          ? std::clamp((keeper.position.x - substepBallPosition.x) / span, 0.0f,
                       1.0f)
          : 1.0f;
  const float crossingY =
      substepBallPosition.y + (ball.position.y - substepBallPosition.y) * t;
  const float crossingHeight =
      substepBallZ + (ball.z - substepBallZ) * t;
  const float gapMetres = std::abs(crossingY - keeper.position.y);

  const float lateralReach = MatchRules::goalkeeperHandlingRadiusMetres(
      keeper.heightMetres, keeper.goalkeeping, crossingHeight, keeper.isDiving);
  if (lateralReach <= 0.0F || gapMetres > lateralReach) return {};

  const float stretch = gapMetres / std::max(lateralReach, EPSILON);
  const float speedExcess =
      std::max(0.0f, length(ball.velocity) -
                         MatchTuning::Goalkeeper::COMFORT_SPEED_METRES);
  const float saveChance =
      std::clamp(
          MatchTuning::Goalkeeper::SAVE_BASE -
              stretch * stretch *
                  MatchTuning::Goalkeeper::SAVE_STRETCH_PENALTY -
              speedExcess * MatchTuning::Goalkeeper::SAVE_SPEED_PENALTY +
              (keeper.goalkeeping - MatchTuning::Player::DEFAULT_ATTRIBUTE) *
                  MatchTuning::Goalkeeper::SAVE_SKILL_BONUS,
          MatchTuning::Goalkeeper::MIN_SAVE_CHANCE,
          MatchTuning::Goalkeeper::MAX_SAVE_CHANCE) *
      (getPossessionState().shotIsPenalty ? MatchTuning::Goalkeeper::PENALTY_SAVE_FACTOR
                          : 1.0f);
  if (randomFloat(0.0f, 1.0f) >= saveChance) return {};
  return {true, stretch, speedExcess, crossingHeight};
}

bool MatchEngine::holdsShotBinturong(const MatchPlayer& keeper, float stretch,
                                     float speedExcess)
{
  // A save he can hold rather than parry: good hands, close to him, not
  // hit too hard.
  const float holdChance =
      std::clamp(MatchTuning::Goalkeeper::HOLD_BASE +
                     keeper.goalkeeping * MatchTuning::Goalkeeper::HOLD_SKILL -
                     stretch * MatchTuning::Goalkeeper::HOLD_STRETCH_PENALTY -
                     speedExcess * MatchTuning::Goalkeeper::HOLD_SPEED_PENALTY,
                 MatchTuning::Goalkeeper::MIN_HOLD_CHANCE,
                 MatchTuning::Goalkeeper::MAX_HOLD_CHANCE);
  return randomFloat(0.0f, 1.0f) < holdChance;
}

MatchEngine::Spill MatchEngine::parrySpillBinturong(const MatchPlayer& keeper,
                                                    float speed)
{
  // The ball spills back out off his hands, away from goal.
  const float outward = keeper.isHomeTeam ? 1.0f : -1.0f;
  Spill spill;
  const float x = outward * speed * randomFloat(0.35f, 0.9f);
  const float y = speed * randomFloat(-0.9f, 0.9f);
  spill.velocity = {x, y};
  spill.verticalSpeed =
      randomFloat(0.0f, MatchTuning::Goalkeeper::PARRY_MAX_VERTICAL_SPEED);
  return spill;
}

bool MatchEngine::blocksShotBinturong(const MatchPlayer& /*player*/,
                                      float /*gap*/, float /*reach*/)
{
  // The sweep only offers a shot that met him (shotMeetsBodyBinturong): it
  // hits him. Reacting and reading it are the brain's, later.
  return true;
}

namespace
{
Point3F minus(Point3F a, Point3F b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float dot3(Point3F a, Point3F b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Point3F along3(Point3F from, Point3F to, float share)
{
  return {from.x + (to.x - from.x) * share, from.y + (to.y - from.y) * share,
          from.z + (to.z - from.z) * share};
}

float distanceToSegment(Point3F point, Point3F a, Point3F b)
{
  const Point3F ab = minus(b, a);
  const float lengthSquared = dot3(ab, ab);
  const float share =
      lengthSquared > EPSILON ? std::clamp(dot3(minus(point, a), ab) / lengthSquared, 0.0f, 1.0f)
                              : 0.0f;
  const Point3F gap = minus(point, along3(a, b, share));
  return std::sqrt(dot3(gap, gap));
}

/** Where along the path from `from` to `to` (share 0-1) the ball first
 * comes within `radius` of the rod from `a` to `b`. */
std::optional<float> rodEntry(Point3F from, Point3F to, Point3F a, Point3F b, float radius)
{
  // The closest pair of points of the two segments (Ericson, 5.1.9).
  const Point3F d1 = minus(to, from);
  const Point3F d2 = minus(b, a);
  const Point3F r = minus(from, a);
  const float aa = dot3(d1, d1);
  const float ee = dot3(d2, d2);
  const float ff = dot3(d2, r);
  float s = 0.0f;
  float t = 0.0f;
  if (aa <= EPSILON) return std::nullopt;
  const float c = dot3(d1, r);
  if (ee <= EPSILON)
  {
    s = std::clamp(-c / aa, 0.0f, 1.0f);
  }
  else
  {
    const float bb = dot3(d1, d2);
    const float denominator = aa * ee - bb * bb;
    s = denominator > EPSILON ? std::clamp((bb * ff - c * ee) / denominator, 0.0f, 1.0f) : 0.0f;
    t = (bb * s + ff) / ee;
    if (t < 0.0f)
    {
      t = 0.0f;
      s = std::clamp(-c / aa, 0.0f, 1.0f);
    }
    else if (t > 1.0f)
    {
      t = 1.0f;
      s = std::clamp((bb - c) / aa, 0.0f, 1.0f);
    }
  }
  if (distanceToSegment(along3(from, to, s), a, b) > radius) return std::nullopt;
  if (distanceToSegment(from, a, b) <= radius) return 0.0f;
  // Inside at s, outside at the start: the distance along a line to a rod
  // is convex, so halving finds where it came within reach.
  float outside = 0.0f;
  float inside = s;
  for (int iteration = 0; iteration < 16; ++iteration)
  {
    const float middle = 0.5f * (outside + inside);
    (distanceToSegment(along3(from, to, middle), a, b) <= radius ? inside : outside) = middle;
  }
  return inside;
}

/** Where along the path (share 0-1) the ball first enters the upright
 * cylinder round the z axis of `radius`, from `bottom` to `top`. */
std::optional<float> uprightEntry(Point3F from, Point3F to, float radius, float bottom,
                                  float top)
{
  float low = 0.0f;
  float high = 1.0f;
  const float dx = to.x - from.x;
  const float dy = to.y - from.y;
  const float dz = to.z - from.z;
  const float a = dx * dx + dy * dy;
  const float b = 2.0f * (from.x * dx + from.y * dy);
  const float c = from.x * from.x + from.y * from.y - radius * radius;
  if (a <= EPSILON)
  {
    if (c > 0.0f) return std::nullopt;
  }
  else
  {
    const float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f) return std::nullopt;
    const float root = std::sqrt(discriminant);
    low = std::max(low, (-b - root) / (2.0f * a));
    high = std::min(high, (-b + root) / (2.0f * a));
  }
  if (std::abs(dz) <= EPSILON)
  {
    if (from.z < bottom || from.z > top) return std::nullopt;
  }
  else
  {
    const float first = (bottom - from.z) / dz;
    const float second = (top - from.z) / dz;
    low = std::max(low, std::min(first, second));
    high = std::min(high, std::max(first, second));
  }
  if (low > high) return std::nullopt;
  return low;
}

/** Where along the path (share 0-1) a ball of `radius` first touches a box
 * lying flat (`facing` along its length, metric unit; half sizes `half`
 * without the ball): the box grown by the ball with rounded edges and
 * corners, so a ball skimming an edge meets it where it really would. */
std::optional<float> roundedBoxEntry(Point3F from, Point3F to, Point3F centre, Vector2F facing,
                                     Point3F half, float radius)
{
  const auto gap = [&](float share)
  {
    const Point3F offset = minus(along3(from, to, share), centre);
    const Point3F local{offset.x * facing.x + offset.y * facing.y,
                        -offset.x * facing.y + offset.y * facing.x, offset.z};
    const float x = std::max(std::abs(local.x) - half.x, 0.0f);
    const float y = std::max(std::abs(local.y) - half.y, 0.0f);
    const float z = std::max(std::abs(local.z) - half.z, 0.0f);
    return std::sqrt(x * x + y * y + z * z);
  };
  // The distance to a box along a line is convex: its least by thirds,
  // then where it first came within reach by halving.
  float low = 0.0f;
  float high = 1.0f;
  for (int iteration = 0; iteration < 40; ++iteration)
  {
    const float first = low + (high - low) / 3.0f;
    const float second = high - (high - low) / 3.0f;
    if (gap(first) <= gap(second))
      high = second;
    else
      low = first;
  }
  const float closest = 0.5f * (low + high);
  if (gap(closest) > radius) return std::nullopt;
  if (gap(0.0f) <= radius) return 0.0f;
  float outside = 0.0f;
  float inside = closest;
  for (int iteration = 0; iteration < 20; ++iteration)
  {
    const float middle = 0.5f * (outside + inside);
    (gap(middle) <= radius ? inside : outside) = middle;
  }
  return inside;
}

/** The ball's line along the ground as it passes him (shot block plan
 * §3d), moving as he is: from his centre, its path from `start` to `end`
 * (metres) over `seconds`, against his `velocity` (m/s). */
struct PassingLine
{
  Vector2F along{};
  /** How far along it to its nearest point to him (m; not more than none:
   * past him), that point, its distance from him, when the ball gets there
   * (s from `start`) and its height there (m). */
  float toNearest = 0.0f;
  Vector2F nearest{};
  float gap = 0.0f;
  float untilThere = 0.0f;
  float height = 0.0f;
};

std::optional<PassingLine> passingLineOf(Point3F start, Point3F end, float seconds,
                                         Vector2F velocity)
{
  if (seconds <= EPSILON) return std::nullopt;
  const Vector2F passing{(end.x - start.x) / seconds - velocity.x,
                         (end.y - start.y) / seconds - velocity.y};
  const float speed = std::hypot(passing.x, passing.y);
  if (speed <= 1e-3f) return std::nullopt;
  PassingLine line;
  line.along = {passing.x / speed, passing.y / speed};
  line.toNearest = -(start.x * line.along.x + start.y * line.along.y);
  line.nearest = {start.x + line.along.x * line.toNearest,
                  start.y + line.along.y * line.toNearest};
  line.gap = std::hypot(line.nearest.x, line.nearest.y);
  line.untilThere = line.toNearest / speed;
  line.height = start.z + (end.z - start.z) * line.untilThere / seconds;
  return line;
}
}  // namespace

std::optional<ShotBodyHit> MatchEngine::shotMeetsBodyBinturong(const MatchPlayer& player,
                                                               std::size_t slot, Point3F from,
                                                               Point3F to, bool canPutLegOut)
{
  // A keeper in a dive meets it as his dive has him (goalkeeper plan step
  // 3).
  if (keeperDiving(player)) return diveMeetsBallBinturong(player, from, to);
  using S = MatchTuning::Stride;
  const float ballRadius = MatchTuning::Units::BALL_RADIUS_METRES;
  const float scale = player.heightMetres / S::REFERENCE_HEIGHT_METRES;
  // From his centre on the ground (off it in a jump), part-way along his
  // move over the step: the ball's path is read against him as he moves
  // through this stretch (from where he is as it starts to where he is as
  // it ends), so it meets him where he is when it gets there.
  const PitchPoint was =
      slot < previousPlayerPositions.size() ? previousPlayerPositions[slot] : player.position;
  const auto baseAt = [&](float stepShare)
  {
    const Vector2F centre = asVector(PitchPoint{was.x + (player.position.x - was.x) * stepShare,
                                      was.y + (player.position.y - was.y) * stepShare});
    return Point3F{centre.x, centre.y,
                   feetAtBinturong(player, ballStepSeconds * (1.0f - stepShare))};
  };
  const auto [fromShare, toShare] = stretchStepSharesBinturong();
  const Point3F base = baseAt(toShare);
  // Against him where he is at the stretch's end (his reach and his leg's
  // aim reckon his own motion themselves); and against him as he moves, for
  // where it meets him.
  const Point3F start = minus(from, base);
  const Point3F end = minus(to, base);
  const Point3F moving = minus(from, baseAt(fromShare));

  // The first surface it meets, and its shape, for the normal there: his
  // torso (an upright cylinder), a rod from `a` to `b` (thigh, shin) or a
  // foot box (centre `a`, its length along `facing`, half sizes `half`
  // without the ball); `leg` for a leg's own motion.
  enum class Shape : std::uint8_t { TORSO, ROD, BOX };
  struct Candidate
  {
    float share = 0.0f;
    ShotBodyPart part = ShotBodyPart::TORSO;
    Shape shape = Shape::TORSO;
    Point3F a{};
    Point3F b{};
    Vector2F facing{};
    Point3F half{};
    std::size_t leg = 0;
  };
  // Every surface its path reaches on this stretch (torso, each leg's
  // thigh, shin and foot, and a keeper's arms and hands).
  std::array<Candidate, 11> candidates{};
  std::size_t candidateCount = 0;
  const auto take = [&](std::optional<float> share, Candidate candidate)
  {
    if (!share || candidateCount >= candidates.size()) return;
    candidate.share = *share;
    candidates[candidateCount++] = candidate;
  };
  // His torso: solid, 50 cm across, from his hip to the top of his head.
  const float hip = bodyLines(player).chest;
  const float torsoReach = MatchTuning::Body::RADIUS_METRES + ballRadius;
  take(uprightEntry(moving, end, torsoReach, hip - ballRadius, player.heightMetres + ballRadius),
       {});

  // Below it, his legs where his stride has them: rolled once for this
  // path, as he moves (in a jump, as he stands).
  if (shotLegPoses.size() < players.size()) shotLegPoses.resize(players.size());
  ShotLegs& legs = shotLegPoses[slot];
  const Vector2F facing = normalized(
      {std::cos(player.facingAngle),
       std::sin(player.facingAngle)});
  if (legs.pathFor != getPossessionState().pathSince)
  {
    std::array<float, 6> rolls{};
    for (float& roll : rolls) roll = randomFloat(0.0f, 1.0f);
    // A keeper's hands go on from where they were on the path just before
    // (a deflection); otherwise ready in front of him.
    const bool handsRecent = legs.hands.active && ballNow() - legs.rolledAt < 1.0;
    const std::array<Point3F, 2> handsWere = legs.hands.at;
    legs = ShotLegs{};
    if (getPossessionState().isShot && player.isGoalkeeper && inPenaltyArea(player.position, player.isHomeTeam))
    {
      using H = MatchTuning::KeeperHands;
      const Vector2F left{-facing.y, facing.x};
      ShotHands& hands = legs.hands;
      hands.active = true;
      for (std::size_t index = 0; index < 2; ++index)
      {
        const float side = index == 0 ? 1.0f : -1.0f;
        hands.start[index] =
            handsRecent ? handsWere[index]
                        : Point3F{facing.x * H::READY_AHEAD_METRES + left.x * side * H::READY_SIDE_METRES,
                                  facing.y * H::READY_AHEAD_METRES + left.y * side * H::READY_SIDE_METRES,
                                  H::READY_HEIGHT_SHARE * player.heightMetres};
        hands.at[index] = hands.start[index];
      }
    }
    legs.stride = legPoseOf(player.heightMetres,
                            player.airborne() ? Vector2F{} : player.velocity, facing, rolls);
    legs.pathFor = getPossessionState().pathSince;
    // For the lab: does a shot's line pass within his full reach?
    if (const auto line = passingLineOf(start, end, substepStretchSeconds, player.velocity);
        getPossessionState().isShot && line && line->toNearest > 0.0f &&
        line->gap <= MatchTuning::ShotBlock::REACH_METRES)
    {
      legs.chance = true;
      ++stats.shotLegChances;
    }
  }
  // Once he has reacted, a leg across its line if he can reach it (shot
  // block plan §3d), out as far as it has got by now; before, his stride.
  const double now = ballNow() + 0.5 * static_cast<double>(substepStretchSeconds);
  const float reaction =
      MatchTuning::ShotBlock::REACTION_SECONDS_POOR +
      (MatchTuning::ShotBlock::REACTION_SECONDS_BEST -
       MatchTuning::ShotBlock::REACTION_SECONDS_POOR) *
          std::clamp((player.defending - STAT_1_RATING) / (STAT_99_RATING - STAT_1_RATING), 0.0f,
                     1.0f);
  // (A team-mate's pass: no leg; getting out of its way is his brain's,
  // later. Pass block plan.)
  if (!legs.reacted && now >= getPossessionState().pathSince + reaction)
  {
    legs.reacted = true;
    legs.legFrom = getPossessionState().pathSince + reaction;
    if (canPutLegOut)
      planShotBlockBinturong(player, legs, start, end);
    else
      legs.noBlock = "a team-mate's ball";
    if (legs.chance) ++stats.shotLegOutcomes[static_cast<std::size_t>(legs.outcome)];
  }
  if (legs.blocking)
  {
    legs.outShare = legs.legSeconds > EPSILON
                        ? std::clamp(static_cast<float>(now - legs.legFrom) / legs.legSeconds,
                                     0.0f, 1.0f)
                        : 1.0f;
    legs.pose = blockLegPoseOf(player.heightMetres, facing, legs.leg, legs.aim,
                               legs.footFacing, legs.outShare);
  }
  else
  {
    legs.pose = legs.stride;
  }
  legs.rolledAt = ballNow();
  const float footAhead = (0.5f * S::FOOT_LENGTH - S::HEEL_BEHIND_ANKLE) * scale;
  const Point3F footHalf{0.5f * S::FOOT_LENGTH * scale, 0.5f * S::FOOT_WIDTH * scale,
                         0.5f * S::FOOT_HEIGHT * scale};
  for (std::size_t index = 0; index < legs.pose.legs.size(); ++index)
  {
    const LegPose::Leg& leg = legs.pose.legs[index];
    take(rodEntry(moving, end, leg.hip, leg.knee, S::THIGH_RADIUS * scale + ballRadius),
         {0.0f, ShotBodyPart::THIGH, Shape::ROD, leg.hip, leg.knee, {}, {}, index});
    take(rodEntry(moving, end, leg.knee, leg.ankle, S::SHIN_RADIUS * scale + ballRadius),
         {0.0f, ShotBodyPart::SHIN, Shape::ROD, leg.knee, leg.ankle, {}, {}, index});
    const Point3F footCentre{leg.ankle.x + leg.footFacing.x * footAhead,
                             leg.ankle.y + leg.footFacing.y * footAhead,
                             leg.ankle.z - 0.5f * S::FOOT_HEIGHT * scale};
    take(roundedBoxEntry(moving, end, footCentre, leg.footFacing, footHalf, ballRadius),
         {0.0f, ShotBodyPart::FOOT, Shape::BOX, footCentre, {}, leg.footFacing, footHalf, index});
  }
  // A keeper's hands (goalkeeper plan step 2): ready until he has reacted
  // (Reflexes), then out towards its line at his hand speed; each on an
  // arm from its shoulder.
  ShotHands& hands = legs.hands;
  std::array<Point3F, 2> handVelocity{};
  if (hands.active)
  {
    using H = MatchTuning::KeeperHands;
    const float reflexes = std::clamp(
        (player.derivedStat(DerivedStat::REFLEXES) - STAT_1_RATING) / (STAT_99_RATING - STAT_1_RATING),
        0.0f, 1.0f);
    const float handReaction =
        H::REACTION_SECONDS_POOR + (H::REACTION_SECONDS_BEST - H::REACTION_SECONDS_POOR) * reflexes;
    if (!hands.reacted && now >= getPossessionState().pathSince + handReaction)
    {
      hands.reacted = true;
      hands.from = getPossessionState().pathSince + handReaction;
      planKeeperHandsBinturong(player, hands, start, end, {base.x, base.y});
    }
    const Vector2F left{-facing.y, facing.x};
    for (std::size_t index = 0; index < 2; ++index)
    {
      Point3F wanted = hands.start[index];
      if (hands.going)
      {
        hands.share[index] =
            hands.seconds[index] > EPSILON
                ? std::clamp(static_cast<float>(now - hands.from) / hands.seconds[index], 0.0f, 1.0f)
                : 1.0f;
        const Point3F target{hands.target[index].x - base.x, hands.target[index].y - base.y,
                             hands.target[index].z};
        wanted = along3(hands.start[index], target, hands.share[index]);
        if (hands.share[index] < 1.0f && hands.seconds[index] > EPSILON)
          handVelocity[index] = {(target.x - hands.start[index].x) / hands.seconds[index],
                                 (target.y - hands.start[index].y) / hands.seconds[index],
                                 (target.z - hands.start[index].z) / hands.seconds[index]};
      }
      hands.at[index] = keeperHandReachBinturong(player, index, wanted);
      const float side = index == 0 ? 1.0f : -1.0f;
      const float off = H::SHOULDER_OFF_CENTRE_SHARE * player.heightMetres;
      const Point3F shoulder{left.x * side * off, left.y * side * off,
                             MatchTuning::Goalkeeper::SHOULDER_HEIGHT_SHARE * player.heightMetres};
      take(rodEntry(moving, end, shoulder, hands.at[index], H::ARM_RADIUS_METRES + ballRadius),
           {0.0f, ShotBodyPart::ARM, Shape::ROD, shoulder, hands.at[index], {}, {}, index});
      take(rodEntry(moving, end, hands.at[index], hands.at[index],
                    H::HAND_RADIUS_METRES + ballRadius),
           {0.0f, ShotBodyPart::HAND, Shape::ROD, hands.at[index], hands.at[index], {}, {}, index});
    }
  }
  // The first surface along its path that it is coming into, against that
  // surface's own motion (a ball on its way out of one meets nothing
  // there).
  std::sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(candidateCount),
            [](const Candidate& a, const Candidate& b) { return a.share < b.share; });
  const auto unit = [](Point3F v, Point3F fallback)
  {
    const float size = std::hypot(v.x, v.y, v.z);
    return size > 1e-5f ? Point3F{v.x / size, v.y / size, v.z / size} : fallback;
  };
  const Point3F back = unit(minus(start, end), {0.0f, 0.0f, 1.0f});
  const Point3F ballVelocity =
      substepStretchSeconds > EPSILON
          ? Point3F{(end.x - start.x) / substepStretchSeconds,
                    (end.y - start.y) / substepStretchSeconds,
                    (end.z - start.z) / substepStretchSeconds}
          : Point3F{};
  for (std::size_t index = 0; index < candidateCount; ++index)
  {
    const Candidate& first = candidates[index];
    // The normal where it meets it: from the nearest point of that surface
    // to the ball's centre (a box's edges and corners rounded by the ball).
    const Point3F at = along3(moving, end, first.share);
    ShotBodyHit hit;
    hit.share = first.share;
    hit.part = first.part;
    switch (first.shape)
    {
      case Shape::TORSO:
      {
        // Its side, or under the hip, or the top of his head: whichever it
        // is nearest (already inside him: the way out it is least deep).
        const float side = torsoReach - std::hypot(at.x, at.y);
        const float under = at.z - (hip - ballRadius);
        const float over = player.heightMetres + ballRadius - at.z;
        if (side <= std::min(under, over))
          hit.normal = unit({at.x, at.y, 0.0f}, back);
        else
          hit.normal = under < over ? Point3F{0.0f, 0.0f, -1.0f} : Point3F{0.0f, 0.0f, 1.0f};
        // Above the top of his shoulders, his head: it comes off it as off
        // a header.
        if (at.z > bodyLines(player).head) hit.part = ShotBodyPart::HEAD;
        break;
      }
      case Shape::ROD:
      {
        const Point3F axis = minus(first.b, first.a);
        const float axisSquared = dot3(axis, axis);
        const float t = axisSquared > EPSILON
                            ? std::clamp(dot3(minus(at, first.a), axis) / axisSquared, 0.0f, 1.0f)
                            : 0.0f;
        hit.normal = unit(minus(at, along3(first.a, first.b, t)), back);
        break;
      }
      case Shape::BOX:
      {
        const Vector2F f = first.facing;
        const Point3F offset = minus(at, first.a);
        const Point3F local{offset.x * f.x + offset.y * f.y, -offset.x * f.y + offset.y * f.x,
                            offset.z};
        const Point3F out{local.x - std::clamp(local.x, -first.half.x, first.half.x),
                          local.y - std::clamp(local.y, -first.half.y, first.half.y),
                          local.z - std::clamp(local.z, -first.half.z, first.half.z)};
        const Point3F localNormal = unit(out, {0.0f, 0.0f, 1.0f});
        hit.normal = unit({localNormal.x * f.x - localNormal.y * f.y,
                           localNormal.x * f.y + localNormal.y * f.x, localNormal.z},
                          back);
        break;
      }
    }
    // The surface's motion: his own, and a leg put out still going out (its
    // foot at the ankle's pace, the shin most of it, the thigh a little).
    hit.surfaceVelocity = {player.velocity.x, player.velocity.y, 0.0f};
    if (legs.blocking && isLegPart(first.part) && first.leg == legs.leg &&
        legs.outShare < 1.0f && legs.legSeconds > EPSILON)
    {
      const Point3F under{legs.pose.legs[legs.leg].hip.x, legs.pose.legs[legs.leg].hip.y,
                          S::FOOT_HEIGHT * scale};
      const float share = first.part == ShotBodyPart::FOOT   ? 1.0f
                          : first.part == ShotBodyPart::SHIN ? 0.75f
                                                             : 0.25f;
      const float rate = share / legs.legSeconds;
      hit.surfaceVelocity.x += (legs.aim.x - under.x) * rate;
      hit.surfaceVelocity.y += (legs.aim.y - under.y) * rate;
      hit.surfaceVelocity.z += (legs.aim.z - under.z) * rate;
    }
    // A keeper's hand still on its way out (his arm half its pace).
    if (first.part == ShotBodyPart::HAND || first.part == ShotBodyPart::ARM)
    {
      const float share = first.part == ShotBodyPart::HAND ? 1.0f : 0.5f;
      hit.surfaceVelocity.x += handVelocity[first.leg].x * share;
      hit.surfaceVelocity.y += handVelocity[first.leg].y * share;
      hit.surfaceVelocity.z += handVelocity[first.leg].z * share;
    }
    // A hand (goalkeeper plan step 4): his other hand on it too, and his
    // stretch from his chest where he stands.
    if (first.part == ShotBodyPart::HAND)
    {
      using H = MatchTuning::KeeperHands;
      const Point3F other = hands.at[1 - first.leg];
      hit.bothHands = std::hypot(at.x - other.x, at.y - other.y, at.z - other.z) <=
                      H::HAND_RADIUS_METRES + ballRadius + H::BOTH_HANDS_SLACK_METRES;
      const Point3F hand = hands.at[first.leg];
      hit.stretch = keeperStretchBinturong(player, {base.x, base.y},
                                           {base.x + hand.x, base.y + hand.y, hand.z});
    }
    const float into = dot3(minus(ballVelocity, hit.surfaceVelocity), hit.normal);
    if (into >= 0.0f) continue;
    // Inside it already at the start (he moved into its path over his
    // step, or two bodies closer than their width): it meets him only if
    // they are clearly coming together.
    if (first.share <= 1e-6f && into > -MatchTuning::ShotBlock::INSIDE_MEETS_SPEED) continue;
    hit.blockFoot = legs.blocking && first.part == ShotBodyPart::FOOT && first.leg == legs.leg;
    return hit;
  }
  return std::nullopt;
}

std::optional<ShotBodyHit> MatchEngine::diveMeetsBallBinturong(const MatchPlayer& keeper, Point3F from,
                                                               Point3F to)
{
  // His body in the air or on the floor (goalkeeper plan step 3): torso
  // and head on the line from his hips to the top of his head, his legs
  // together, his arms out to his hands; where he is as the ball passes
  // through this stretch, moving as his dive moves him.
  using D = MatchTuning::KeeperDive;
  using H = MatchTuning::KeeperHands;
  const KeeperDive& dive = keepers[keeper.isHomeTeam ? 0 : 1].dive;
  const float ballRadius = MatchTuning::Units::BALL_RADIUS_METRES;
  const double when = ballNow() + 0.5 * static_cast<double>(substepStretchSeconds);
  constexpr double LOOK = 0.02;
  const KeeperDivePose pose = keeperDivePoseBinturong(keeper, dive, when);
  const KeeperDivePose before = keeperDivePoseBinturong(keeper, dive, when - LOOK);
  const auto speedOf = [&](Point3F now, Point3F was)
  {
    return Point3F{(now.x - was.x) / static_cast<float>(LOOK), (now.y - was.y) / static_cast<float>(LOOK),
                   (now.z - was.z) / static_cast<float>(LOOK)};
  };
  struct Rod
  {
    Point3F a;
    Point3F b;
    float radius;
    ShotBodyPart part;
    Point3F velocity;
  };
  const Point3F bodySpeed = speedOf(pose.hip, before.hip);
  const float torso = MatchTuning::Body::RADIUS_METRES;
  std::array<Rod, 8> rods{{
      {pose.hip, pose.neck, torso, ShotBodyPart::TORSO, bodySpeed},
      {pose.neck, pose.top, torso, ShotBodyPart::HEAD, bodySpeed},
      {pose.feet, pose.hip, D::LEGS_RADIUS_METRES, ShotBodyPart::THIGH, bodySpeed},
      {pose.shoulders[0], pose.hands[0], H::ARM_RADIUS_METRES, ShotBodyPart::ARM,
       speedOf(pose.hands[0], before.hands[0])},
      {pose.shoulders[1], pose.hands[1], H::ARM_RADIUS_METRES, ShotBodyPart::ARM,
       speedOf(pose.hands[1], before.hands[1])},
      {pose.hands[0], pose.hands[0], H::HAND_RADIUS_METRES, ShotBodyPart::HAND,
       speedOf(pose.hands[0], before.hands[0])},
      {pose.hands[1], pose.hands[1], H::HAND_RADIUS_METRES, ShotBodyPart::HAND,
       speedOf(pose.hands[1], before.hands[1])},
      {pose.feet, pose.feet, D::LEGS_RADIUS_METRES, ShotBodyPart::FOOT, bodySpeed},
  }};
  // An arm's half its hand's pace.
  for (std::size_t index = 3; index < 5; ++index)
    rods[index].velocity = {0.5f * rods[index].velocity.x, 0.5f * rods[index].velocity.y,
                            0.5f * rods[index].velocity.z};
  std::array<std::pair<float, std::size_t>, 8> met{};
  std::size_t count = 0;
  for (std::size_t index = 0; index < rods.size(); ++index)
    if (const auto share = rodEntry(from, to, rods[index].a, rods[index].b, rods[index].radius + ballRadius))
      met[count++] = {*share, index};
  std::sort(met.begin(), met.begin() + static_cast<std::ptrdiff_t>(count));
  const Point3F ballVelocity =
      substepStretchSeconds > EPSILON
          ? Point3F{(to.x - from.x) / substepStretchSeconds, (to.y - from.y) / substepStretchSeconds,
                    (to.z - from.z) / substepStretchSeconds}
          : Point3F{};
  for (std::size_t index = 0; index < count; ++index)
  {
    const Rod& rod = rods[met[index].second];
    ShotBodyHit hit;
    hit.share = met[index].first;
    hit.part = rod.part;
    const Point3F at = along3(from, to, hit.share);
    const Point3F axis = minus(rod.b, rod.a);
    const float axisSquared = dot3(axis, axis);
    const float t =
        axisSquared > EPSILON ? std::clamp(dot3(minus(at, rod.a), axis) / axisSquared, 0.0f, 1.0f) : 0.0f;
    const Point3F out = minus(at, along3(rod.a, rod.b, t));
    const float size = std::sqrt(dot3(out, out));
    const Point3F back = minus(from, to);
    const float backSize = std::sqrt(dot3(back, back));
    hit.normal = size > 1e-5f ? Point3F{out.x / size, out.y / size, out.z / size}
                 : backSize > 1e-5f ? Point3F{back.x / backSize, back.y / backSize, back.z / backSize}
                                    : Point3F{0.0f, 0.0f, 1.0f};
    hit.surfaceVelocity = rod.velocity;
    const float into = dot3(minus(ballVelocity, hit.surfaceVelocity), hit.normal);
    if (into >= 0.0f) continue;
    if (hit.share <= 1e-6f && into > -MatchTuning::ShotBlock::INSIDE_MEETS_SPEED) continue;
    // A hand (goalkeeper plan step 4): his other hand on it too, and his
    // stretch from his chest where he pushed off.
    if (rod.part == ShotBodyPart::HAND)
    {
      const std::size_t hand = met[index].second == 5 ? 0 : 1;
      const Point3F other = pose.hands[1 - hand];
      hit.bothHands = std::hypot(at.x - other.x, at.y - other.y, at.z - other.z) <=
                      H::HAND_RADIUS_METRES + ballRadius + H::BOTH_HANDS_SLACK_METRES;
      hit.stretch = keeperStretchBinturong(keeper, dive.origin, pose.hands[hand]);
    }
    return hit;
  }
  return std::nullopt;
}

std::pair<float, float> MatchEngine::stretchStepSharesBinturong() const
{
  const int substepsPerStep =
      MatchTuning::Timing::BALL_SUBSTEPS * static_cast<int>(stepTicks);
  const float substepSeconds = ballStepSeconds / static_cast<float>(substepsPerStep);
  const float done = static_cast<float>(substepsPerStep - 1 - substepsLeftInStep);
  const float stretch = substepSeconds > EPSILON ? substepStretchSeconds / substepSeconds : 1.0f;
  const float from = (done + substepStretchFromShare) / static_cast<float>(substepsPerStep);
  const float to = (done + substepStretchFromShare + stretch) / static_cast<float>(substepsPerStep);
  return {std::clamp(from, 0.0f, 1.0f), std::clamp(to, 0.0f, 1.0f)};
}

void MatchEngine::checkShotThroughTorsoBinturong(const MatchPlayer& player, std::size_t slot,
                                                 Point3F from, Point3F to)
{
  if (slot >= shotLegPoses.size() || shotLegPoses[slot].passedThrough) return;
  // Where he was as the stretch began and ended (part-way along his move
  // over the step), and the ball's path against him over it.
  const PitchPoint was =
      slot < previousPlayerPositions.size() ? previousPlayerPositions[slot] : player.position;
  const auto at = [&](float share)
  {
    return asVector(PitchPoint{was.x + (player.position.x - was.x) * share,
                     was.y + (player.position.y - was.y) * share});
  };
  const auto [fromShare, toShare] = stretchStepSharesBinturong();
  const Vector2F first = at(fromShare);
  const Vector2F last = at(toShare);
  // Well inside: 5 cm into his torso, 5 cm clear of his hip and his head.
  constexpr float MARGIN = 0.05f;
  const float inside =
      MatchTuning::Body::RADIUS_METRES + MatchTuning::Units::BALL_RADIUS_METRES - MARGIN;
  const float bottom = bodyLines(player).chest + MARGIN;
  const float top = player.heightMetres - MARGIN;
  constexpr int SAMPLES = 32;
  for (int index = 0; index <= SAMPLES; ++index)
  {
    const float share = static_cast<float>(index) / SAMPLES;
    const Point3F ball = along3(from, to, share);
    const float x = ball.x - (first.x + (last.x - first.x) * share);
    const float y = ball.y - (first.y + (last.y - first.y) * share);
    if (ball.z < bottom || ball.z > top || std::hypot(x, y) > inside) continue;
    shotLegPoses[slot].passedThrough = true;
    ++stats.shotTorsoPassThroughs;
    if (index == 0) ++stats.shotTorsoInsideAtStart;
    return;
  }
}

bool MatchEngine::shotStaysOnGoalBinturong()
{
  // Where its flight from here, spin and all, crosses the goal line the
  // shot was going for: between the posts and under the bar, it is still a
  // shot, and the keeper reads it afresh.
  using P = MatchTuning::Pitch;
  const float goalX = pitch.atX(getPossessionState().shotByHome ? 1.0f : 0.0f);
  const float speed = length(ball.velocity);
  if (speed <= EPSILON) return false;
  const Vector2F heading{ball.velocity.x / speed, ball.velocity.y / speed};
  const GoalLineCrossing crossing = predictGoalLineCrossing(
      *this, ball.position, heading, speed, ball.velocityZ,
      ball.z, goalX, ball.spin);
  const float halfGoal = MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES;
  if (!crossing.reaches ||
      std::abs(crossing.sideways) >= halfGoal - MatchTuning::Units::BALL_RADIUS_METRES ||
      crossing.height >=
          MatchTuning::Units::CROSSBAR_HEIGHT_METRES - MatchTuning::Units::BALL_RADIUS_METRES)
    return false;
  rearmShotAfterBounce(pitch.atY(P::CENTRE) + crossing.sideways,
                       crossing.height);
  planGoalkeeperDive(!getPossessionState().shotByHome, false);
  return true;
}

void MatchEngine::bounceShotOffBodyBinturong(const MatchPlayer& player, const ShotBodyHit& hit)
{
  using K = MatchTuning::ShotBlock;
  using B = MatchTuning::Ball;
  const auto part = static_cast<std::size_t>(hit.part);
  const float radius = B::RADIUS_METRES;
  const float inertia = B::BALL_INERTIA_SHARE;
  const Point3F velocity{ball.velocity.x, ball.velocity.y, ball.velocityZ};
  // Its spin as a turning (rad/s) about the vertical and about the
  // horizontal across its heading (BallSpin's axes).
  const float speed = length(ball.velocity);
  const Vector2F heading =
      speed > EPSILON ? Vector2F{ball.velocity.x / speed, ball.velocity.y / speed}
                      : Vector2F{1.0f, 0.0f};
  const Point3F spin{-heading.y * ball.spin.top, heading.x * ball.spin.top, ball.spin.side};
  const auto cross = [](Point3F a, Point3F b)
  { return Point3F{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
  const auto plus = [](Point3F a, Point3F b) { return Point3F{a.x + b.x, a.y + b.y, a.z + b.z}; };
  const auto times = [](Point3F a, float k) { return Point3F{a.x * k, a.y * k, a.z * k}; };

  // Off a surface facing `normal`: against its motion, this share of its
  // pace into it springs back; along it, the grip takes its slip (its pace
  // against the surface at the contact, spin included) part of the way to
  // rolling, as the grass does, trading pace for spin.
  struct Outcome
  {
    Point3F velocity;
    Point3F spin;
  };
  // A keeper's hand: soft hands to firm wrists (Handling; goalkeeper plan
  // step 2).
  const float springBack =
      hit.part == ShotBodyPart::HAND
          ? MatchTuning::KeeperHands::SPRING_BACK_POOR +
                (MatchTuning::KeeperHands::SPRING_BACK_BEST -
                 MatchTuning::KeeperHands::SPRING_BACK_POOR) *
                    std::clamp((player.derivedStat(DerivedStat::HANDLING) - STAT_1_RATING) /
                                   (STAT_99_RATING - STAT_1_RATING),
                               0.0f, 1.0f)
          : K::SPRING_BACK[part];
  const auto bounce = [&](Point3F normal) -> Outcome
  {
    const Point3F relative = minus(velocity, hit.surfaceVelocity);
    const float into = dot3(relative, normal);
    if (into >= 0.0f) return {velocity, spin};
    const Point3F alongSurface = minus(relative, times(normal, into));
    const Point3F slipAll = plus(alongSurface, cross(spin, times(normal, -radius)));
    const Point3F slip = minus(slipAll, times(normal, dot3(slipAll, normal)));
    const float grip = (1.0f - K::ALONG_KEPT[part]) * (1.0f + inertia);
    const Point3F change = times(slip, -grip / (1.0f + inertia));
    const Point3F after =
        plus(plus(alongSurface, change), times(normal, -into * springBack));
    return {plus(after, hit.surfaceVelocity),
            minus(spin, times(cross(normal, change), 1.0f / (inertia * radius)))};
  };

  Outcome outcome = bounce(hit.normal);
  if (hit.blockFoot)
  {
    // A leg put out, met with the foot: its face turned a little (Tackling)
    // to send it away from his goal.
    const float tackling =
        std::clamp((player.derivedStat(DerivedStat::TACKLING) - STAT_1_RATING) /
                       (STAT_99_RATING - STAT_1_RATING),
                   0.0f, 1.0f);
    const float most = (K::FOOT_AIM_DEGREES_POOR +
                        (K::FOOT_AIM_DEGREES_BEST - K::FOOT_AIM_DEGREES_POOR) * tackling) *
                       std::numbers::pi_v<float> / 180.0f;
    const Vector2F at = asVector(ball.position);
    const Vector2F goal = asVector(pitch.at(player.isHomeTeam ? 0.0f : 1.0f, 0.5f));
    const Vector2F away = normalized({at.x - goal.x, at.y - goal.y});
    const auto awayness = [&](const Outcome& candidate)
    {
      const float size = std::hypot(candidate.velocity.x, candidate.velocity.y);
      return size > EPSILON ? (candidate.velocity.x * away.x + candidate.velocity.y * away.y) / size
                            : -1.0f;
    };
    float best = awayness(outcome);
    constexpr int TURNS = 8;
    for (int turn = -TURNS; turn <= TURNS; ++turn)
    {
      const float angle = most * static_cast<float>(turn) / static_cast<float>(TURNS);
      const Point3F turned{hit.normal.x * std::cos(angle) - hit.normal.y * std::sin(angle),
                           hit.normal.x * std::sin(angle) + hit.normal.y * std::cos(angle),
                           hit.normal.z};
      const Outcome candidate = bounce(turned);
      if (const float score = awayness(candidate); score > best)
      {
        best = score;
        outcome = candidate;
      }
    }
  }
  else if (hit.parryAim)
  {
    // A keeper's parry (goalkeeper plan step 4): the hand's face turned up
    // to its most (Handling) any way round, to send it as far from the goal
    // frame as it can: away from the middle of the goal mouth, over the bar
    // or round a post as much as out.
    using H = MatchTuning::KeeperHands;
    const float handling =
        std::clamp((player.derivedStat(DerivedStat::HANDLING) - STAT_1_RATING) /
                       (STAT_99_RATING - STAT_1_RATING),
                   0.0f, 1.0f);
    const float most = (H::PARRY_AIM_DEGREES_POOR +
                        (H::PARRY_AIM_DEGREES_BEST - H::PARRY_AIM_DEGREES_POOR) * handling) *
                       std::numbers::pi_v<float> / 180.0f;
    const Vector2F at = asVector(ball.position);
    const Vector2F goal = asVector(pitch.at(player.isHomeTeam ? 0.0f : 1.0f, 0.5f));
    const Point3F fromGoal{at.x - goal.x, at.y - goal.y,
                           ball.z -
                               0.5f * MatchTuning::Units::CROSSBAR_HEIGHT_METRES};
    const float fromGoalSize = std::sqrt(dot3(fromGoal, fromGoal));
    const Point3F away = fromGoalSize > EPSILON ? times(fromGoal, 1.0f / fromGoalSize)
                                                : Point3F{player.isHomeTeam ? 1.0f : -1.0f, 0.0f, 0.0f};
    const auto awayness = [&](const Outcome& candidate)
    {
      const float size = std::sqrt(dot3(candidate.velocity, candidate.velocity));
      return size > EPSILON ? dot3(candidate.velocity, away) / size : -1.0f;
    };
    // Two directions square to the face, to turn it about.
    const Point3F n = hit.normal;
    const Point3F seed = std::abs(n.z) < 0.9f ? Point3F{0.0f, 0.0f, 1.0f} : Point3F{1.0f, 0.0f, 0.0f};
    Point3F u = cross(n, seed);
    u = times(u, 1.0f / std::max(std::sqrt(dot3(u, u)), 1e-6f));
    const Point3F v = cross(n, u);
    float best = awayness(outcome);
    constexpr int RINGS = 4;
    constexpr int ROUND = 16;
    for (int ring = 1; ring <= RINGS; ++ring)
    {
      const float angle = most * static_cast<float>(ring) / RINGS;
      for (int step = 0; step < ROUND; ++step)
      {
        const float phi = 2.0f * std::numbers::pi_v<float> * static_cast<float>(step) / ROUND;
        const Point3F turned = plus(times(n, std::cos(angle)),
                                    times(plus(times(u, std::cos(phi)), times(v, std::sin(phi))),
                                          std::sin(angle)));
        const Outcome candidate = bounce(turned);
        if (const float score = awayness(candidate); score > best)
        {
          best = score;
          outcome = candidate;
        }
      }
    }
  }

  ball.velocity = {outcome.velocity.x, outcome.velocity.y};
  ball.velocityZ = outcome.velocity.z;
  // Its spin about its new heading's axes (any about the heading itself,
  // a spin the ball's flight doesn't have, is let go).
  const float after = length(ball.velocity);
  const Vector2F newHeading =
      after > EPSILON ? Vector2F{ball.velocity.x / after, ball.velocity.y / after} : heading;
  ball.spin = {outcome.spin.z, -newHeading.y * outcome.spin.x + newHeading.x * outcome.spin.y};
  shotBounce = {outcome.velocity, ball.spin, ballNow(), hit};
}

void MatchEngine::ballMeetsBodyBinturong(MatchPlayer& player, std::size_t slot,
                                         const ShotBodyHit& hit)
{
  // A keeper's hand in his area (a smother: goalkeeper plan step 8): step
  // 4's catch, held into his hands, else it comes off as below (the face
  // turned away from goal when he meant to parry).
  ShotBodyHit met = hit;
  // In a smother his midriff takes it and his arms wrap round it as it is
  // cushioned (user 2026-11-09): both arms on it, no stretch.
  const bool smothering = player.isGoalkeeper && keepers[player.isHomeTeam ? 0 : 1].dive.active &&
                          keepers[player.isHomeTeam ? 0 : 1].dive.smother;
  if (smothering && met.part == ShotBodyPart::TORSO)
  {
    met.bothHands = true;
    met.stretch = 0.0f;
  }
  if ((met.part == ShotBodyPart::HAND || (smothering && met.part == ShotBodyPart::TORSO)) &&
      player.isGoalkeeper && inPenaltyArea(player.position, player.isHomeTeam) &&
      !(getPossessionState().isPass && getPossessionState().passByHome == player.isHomeTeam))
  {
    if (keeperHandsHoldShotBinturong(player, met, false))
    {
      ++stats.keeperSmothersHeld;
      recordPassCompletion(player);
      const PitchPoint wasAt = ball.position;
      const float wasMetres = ball.z;
      setPossession(player);
      takeInHandsBinturong(player, wasAt, wasMetres);
      player.actionCooldown = keeperHoldSecondsBinturong(player);
      const KeeperDive& dive = keepers[player.isHomeTeam ? 0 : 1].dive;
      if (dive.active)
        player.actionCooldown += std::max(
            0.0f, static_cast<float>(dive.from + static_cast<double>(dive.stretchSeconds +
                                                                     dive.fallSeconds +
                                                                     dive.downSeconds) -
                                     getSimulatedSeconds()));
      return;
    }
  }
  // Whose ball it was: an opponent's pass he blocks; off a team-mate's (or
  // nobody's) ball it is a deflection.
  const MatchPlayer* lastPlayedBy = findMatchPlayer(getLastPossessionAttachment());
  const bool opponents = lastPlayedBy && lastPlayedBy->isHomeTeam != player.isHomeTeam;
  const ShotLegs* legs = slot < shotLegPoses.size() ? &shotLegPoses[slot] : nullptr;
  const bool legPutOut = legs && legs->blocking && isLegPart(hit.part);
  noteBodyDeflectionHit(player, hit.part, legPutOut, opponents);
  // A loose ball now, nobody's pass; him kept off it while it comes off
  // him (off a leg put out, until it is back), as after a shot block.
  beginBodyDeflection(player);
  ball.kickerLockout = legPutOut ? legs->outShare * legs->legSeconds +
                                       MatchTuning::ShotBlock::RECOVERY_LEG_EXTRA_SECONDS
                                 : MatchTuning::ShotBlock::RECOVERY_BODY_SECONDS;
  ball.touchAttempts = 0;
  notePathChange();
  bounceShotOffBodyBinturong(player, met);
}

void MatchEngine::planShotBlockBinturong(const MatchPlayer& player, ShotLegs& legs,
                                         Point3F start, Point3F end)
{
  using K = MatchTuning::ShotBlock;
  const float ballRadius = MatchTuning::Units::BALL_RADIUS_METRES;
  const float scale = player.heightMetres / MatchTuning::Stride::REFERENCE_HEIGHT_METRES;
  const auto rating = [](float stat)
  { return std::clamp((stat - STAT_1_RATING) / (STAT_99_RATING - STAT_1_RATING), 0.0f, 1.0f); };
  const float tackling = rating(player.derivedStat(DerivedStat::TACKLING));
  const Vector2F facing = normalized(
      {std::cos(player.facingAngle),
       std::sin(player.facingAngle)});

  const auto refuse = [&](ShotLegOutcome outcome, const char* why)
  {
    legs.outcome = outcome;
    legs.noBlock = why;
  };
  // In a jump, planted for a thigh touch or with his foot committed, no
  // leg goes out; and only if his brain means to block.
  if (player.airborne() || player.trapTimer > 0.0f || player.footCooldown > 1e-4f)
    return refuse(ShotLegOutcome::FEET_BUSY, "his feet are busy");
  if (!choosesToBlock(player)) return refuse(ShotLegOutcome::CHOSE_NOT, "chose not to");

  // The ball's line along the ground as it passes him, moving as he is
  // (his leg goes with him).
  const auto line = passingLineOf(start, end, substepStretchSeconds, player.velocity);
  if (!line) return refuse(ShotLegOutcome::NOT_TRAVELLING, "the ball is not travelling");
  if (line->toNearest <= 0.0f) return refuse(ShotLegOutcome::PAST_HIM, "it is past him");
  const Vector2F nearest = line->nearest;
  const float lineGap = line->gap;
  const float lineHeight = line->height;
  legs.lineGap = lineGap;
  legs.ballThere = ballNow() + static_cast<double>(line->untilThere);
  legs.lineHeight = lineHeight;
  // Out across its line: towards it, or (straight at him) square to it.
  const Vector2F out = lineGap > 1e-3f ? Vector2F{nearest.x / lineGap, nearest.y / lineGap}
                                       : Vector2F{-line->along.y, line->along.x};

  // How far his ankle reaches at that height: on the ground to his knee in
  // full, narrowing to his body at his hip; above it, his torso only. Less
  // running (step 2's limit).
  const BodyLines lines = bodyLines(player);
  if (lineHeight >= lines.chest) return refuse(ShotLegOutcome::ABOVE_HIP, "above his hip");
  const float body = MatchTuning::Body::RADIUS_METRES + ballRadius;
  float reach = K::REACH_METRES;
  if (lineHeight > lines.feet)
    reach += (body - reach) * (lineHeight - lines.feet) / std::max(lines.chest - lines.feet, EPSILON);
  if (lineGap > reach)
    return refuse(ShotLegOutcome::OUT_OF_REACH_HEIGHT, "it passes out of his reach at that height");
  const float speedShare =
      std::clamp(length(player.velocity) / std::max(currentTopSpeed(player), EPSILON), 0.0f, 1.0f);
  if (speedShare > K::FULL_REACH_SPEED_SHARE)
    reach *= 1.0f - (1.0f - K::SPRINT_REACH_SHARE) * (speedShare - K::FULL_REACH_SPEED_SHARE) /
                        (1.0f - K::FULL_REACH_SPEED_SHARE);
  if (lineGap > reach)
    return refuse(ShotLegOutcome::OUT_OF_REACH_RUNNING, "it passes out of his reach running");
  if (inTouchDeadZone(facing, out, lineGap, touchKnobs[TouchKnob::COMFORT_GAP],
                      touchKnobs[TouchKnob::DEAD_ZONE_ANGLE]))
    return refuse(ShotLegOutcome::BEHIND_HIM, "it passes behind him");

  // The leg nearer its line; its ankle aimed on the line, the foot's
  // middle at the ball's height, side-on (pointing out along the leg).
  legs.leg = (-facing.y * out.x + facing.x * out.y) >= 0.0f ? 0 : 1;
  const float ankleLowest = MatchTuning::Stride::FOOT_HEIGHT * scale;
  Point3F aim{nearest.x, nearest.y,
              std::max(ankleLowest, lineHeight + 0.5f * MatchTuning::Stride::FOOT_HEIGHT * scale)};
  const Point3F hip = hipOf(player.heightMetres, facing, legs.leg);
  const auto outFrom = [&](Point3F to)
  { return std::hypot(to.x - hip.x, to.y - hip.y, to.z - ankleLowest); };
  const float legOutSeconds =
      K::LEG_OUT_SECONDS_POOR + (K::LEG_OUT_SECONDS_BEST - K::LEG_OUT_SECONDS_POOR) * tackling;

  // Time to spare: the ball at its line's nearest point, less his leg
  // getting there; the less, the bigger his miss.
  const float needSeconds = legOutSeconds * outFrom(aim) / K::REACH_METRES;
  legs.spareSeconds = static_cast<float>(legs.ballThere - (legs.legFrom + needSeconds));
  const float rushed = std::clamp(1.0f - legs.spareSeconds / K::RUSHED_SECONDS, 0.0f, 1.0f);
  legs.errorRadius = (K::ERROR_METRES_POOR + (K::ERROR_METRES_BEST - K::ERROR_METRES_POOR) * tackling) *
                     (1.0f + (K::RUSHED_ERROR_FACTOR - 1.0f) * rushed);
  // The miss: anywhere in a ball of that radius, kept off the ground.
  Point3F miss{};
  for (int attempt = 0; attempt < 16; ++attempt)
  {
    miss = {randomFloat(-1.0f, 1.0f), randomFloat(-1.0f, 1.0f), randomFloat(-1.0f, 1.0f)};
    if (miss.x * miss.x + miss.y * miss.y + miss.z * miss.z <= 1.0f) break;
  }
  if (const float size = std::hypot(miss.x, miss.y, miss.z); size > 1.0f)
    miss = {miss.x / size, miss.y / size, miss.z / size};
  aim = {aim.x + miss.x * legs.errorRadius, aim.y + miss.y * legs.errorRadius,
         std::max(ankleLowest, aim.z + miss.z * legs.errorRadius)};
  legs.error = legs.errorRadius * std::sqrt(miss.x * miss.x + miss.y * miss.y + miss.z * miss.z);

  legs.blocking = true;
  legs.aim = aim;
  legs.footFacing = out;
  legs.legSeconds = legOutSeconds * outFrom(aim) / K::REACH_METRES;
}

Point3F MatchEngine::keeperHandReachBinturong(const MatchPlayer& player, std::size_t index,
                                              Point3F wanted) const
{
  using H = MatchTuning::KeeperHands;
  const Vector2F facing = normalized(
      {std::cos(player.facingAngle),
       std::sin(player.facingAngle)});
  const Vector2F left{-facing.y, facing.x};
  // Not far behind him (the more agile, the further).
  const float behind = alongStatScale(player.derivedStat(DerivedStat::AGILITY),
                                      H::BEHIND_METRES_POOR, H::BEHIND_METRES_BEST);
  const float ahead = wanted.x * facing.x + wanted.y * facing.y;
  if (ahead < -behind)
  {
    wanted.x += facing.x * (-behind - ahead);
    wanted.y += facing.y * (-behind - ahead);
  }
  // From the ground up to his standing reach: the hand's centre a hand
  // inside both.
  const float top = player.heightMetres * H::STANDING_REACH_HEIGHT_SHARE - H::HAND_RADIUS_METRES;
  wanted.z = std::clamp(wanted.z, H::HAND_RADIUS_METRES, top);
  // Round from its own shoulder, as far as his centre's reach less the
  // shoulder's offset (the far hand reaches less across him).
  const float side = index == 0 ? 1.0f : -1.0f;
  const float off = H::SHOULDER_OFF_CENTRE_SHARE * player.heightMetres;
  const Vector2F shoulder{left.x * side * off, left.y * side * off};
  const float reach = (H::REACH_HEIGHT_SHARE - H::SHOULDER_OFF_CENTRE_SHARE) * player.heightMetres;
  const Vector2F out{wanted.x - shoulder.x, wanted.y - shoulder.y};
  if (const float size = std::hypot(out.x, out.y); size > reach)
  {
    wanted.x = shoulder.x + out.x * reach / size;
    wanted.y = shoulder.y + out.y * reach / size;
  }
  return wanted;
}

float MatchEngine::keeperStretchBinturong(const MatchPlayer& keeper, Vector2F stood,
                                          Point3F hand) const
{
  // From his chest where he stood (his hands' ready height) to the hand,
  // against the longest of his reaches from there: a full dive along the
  // ground (his dive reach) or straight up at the top of his jump (Aerial
  // Reach). Mine: his dive reach is measured from his feet, near enough.
  using H = MatchTuning::KeeperHands;
  using D = MatchTuning::KeeperDive;
  const float height = keeper.heightMetres;
  const float chest = H::READY_HEIGHT_SHARE * height;
  const float agility =
      std::clamp((keeper.derivedStat(DerivedStat::AGILITY) - STAT_1_RATING) /
                     (STAT_99_RATING - STAT_1_RATING),
                 0.0f, 1.0f);
  const float longest = std::max(D::REACH_HEIGHT_SHARE * height + D::REACH_AGILITY_METRES * agility,
                                 derivedPhysicalValue(DerivedStat::AERIAL_REACH,
                                                      keeper.derivedStat(DerivedStat::AERIAL_REACH)) -
                                     chest);
  const float out = std::hypot(hand.x - stood.x, hand.y - stood.y, hand.z - chest);
  return std::clamp(out / std::max(longest, EPSILON), 0.0f, 1.0f);
}

bool MatchEngine::keeperHandsHoldShotBinturong(MatchPlayer& keeper, ShotBodyHit& hit, bool shot)
{
  // (A high claim, not a shot: the same hands, counted as a claim.)
  if (!shot)
  {
    const MatchStats saved = stats;
    const bool held = keeperHandsHoldShotBinturong(keeper, hit, true);
    stats = saved;
    return held;
  }
  // The brain's stand-in for catch or parry (goalkeeper plan §3): he goes
  // to catch it if both hands are on it and it is within his Handling limit
  // for its pace against his hands and his stretch; otherwise he parries,
  // the face turned away from goal. A catch can be fumbled: a small chance
  // on any, more near his limit (user 2026-11-07); it springs off his hands
  // as it met them.
  using H = MatchTuning::KeeperHands;
  ++stats.keeperHandShots;
  const auto parry = [&]
  {
    hit.parryAim = true;
    ++stats.keeperParries;
    return false;
  };
  if (!hit.bothHands) return parry();
  ++stats.keeperBothHands;
  const float handling =
      std::clamp((keeper.derivedStat(DerivedStat::HANDLING) - STAT_1_RATING) /
                     (STAT_99_RATING - STAT_1_RATING),
                 0.0f, 1.0f);
  const float limit = (H::HOLD_SPEED_POOR + (H::HOLD_SPEED_BEST - H::HOLD_SPEED_POOR) * handling) *
                      (1.0f - (1.0f - H::HOLD_FULL_STRETCH_SHARE) * hit.stretch);
  const Point3F relative{ball.velocity.x - hit.surfaceVelocity.x,
                         ball.velocity.y - hit.surfaceVelocity.y,
                         ball.velocityZ - hit.surfaceVelocity.z};
  const float pace = std::sqrt(dot3(relative, relative));
  if (pace > limit) return parry();
  ++stats.keeperCatches;
  const float nearLimit = std::clamp((pace / std::max(limit, EPSILON) - H::FUMBLE_RISES_FROM_SHARE) /
                                         (1.0f - H::FUMBLE_RISES_FROM_SHARE),
                                     0.0f, 1.0f);
  const float fumble = H::FUMBLE_CHANCE + (H::FUMBLE_CHANCE_AT_LIMIT - H::FUMBLE_CHANCE) * nearLimit;
  if (randomFloat(0.0f, 1.0f) < fumble)
  {
    ++stats.keeperFumbles;
    return false;
  }
  ++stats.keeperHeld;
  return true;
}

float MatchEngine::keeperArmBinturong(const MatchPlayer& keeper) const
{
  // Straight up, his hand's top at his standing fingertip reach.
  using H = MatchTuning::KeeperHands;
  return (H::STANDING_REACH_HEIGHT_SHARE - MatchTuning::Goalkeeper::SHOULDER_HEIGHT_SHARE) *
             keeper.heightMetres -
         H::HAND_RADIUS_METRES;
}

bool MatchEngine::keeperMayClaimBinturong(const MatchPlayer& keeper) const
{
  return keeper.isGoalkeeper && active(keeper) && !keeper.fallen &&
         getBody(keeper.isHomeTeam) == BodyId::BINTURONG && !keeperDiving(keeper) &&
         inPenaltyArea(keeper.position, keeper.isHomeTeam) && !getPossessionState().isShot &&
         !(getPossessionState().isPass && getPossessionState().passByHome == keeper.isHomeTeam) &&
         !(ball.kickerLockout > 0.0f && getLegacyContactActor() == keeper.player) &&
         !(getPossessionAttachment() && !hasTouchClaim());
}

std::optional<KeeperReach> MatchEngine::keeperReachBinturong(const MatchPlayer& keeper, Vector2F at,
                                                             float feet, Point3F start,
                                                             Point3F end) const
{
  // Each hand anywhere within an arm of its shoulder (goalkeeper plan step
  // 7): up, forward, out to the side; where his body is then (in a jump,
  // his feet that high).
  using H = MatchTuning::KeeperHands;
  const float height = keeper.heightMetres;
  const Vector2F facing = normalized(
      {std::cos(keeper.facingAngle),
       std::sin(keeper.facingAngle)});
  const Vector2F left{-facing.y, facing.x};
  const float off = H::SHOULDER_OFF_CENTRE_SHARE * height;
  const float shoulderZ = feet + MatchTuning::Goalkeeper::SHOULDER_HEIGHT_SHARE * height;
  const std::array<Point3F, 2> shoulders{
      Point3F{at.x + left.x * off, at.y + left.y * off, shoulderZ},
      Point3F{at.x - left.x * off, at.y - left.y * off, shoulderZ}};
  const float arm = keeperArmBinturong(keeper);
  const float reach = arm + H::HAND_RADIUS_METRES + MatchTuning::Units::BALL_RADIUS_METRES;
  const Point3F path = minus(end, start);
  const float pathSquared = dot3(path, path);
  // When the stretch is within each shoulder's reach (from, to: 0-1).
  std::array<std::optional<std::pair<float, float>>, 2> inside{};
  for (std::size_t index = 0; index < 2; ++index)
  {
    const Point3F from = minus(start, shoulders[index]);
    const float c = dot3(from, from) - reach * reach;
    if (pathSquared <= EPSILON)
    {
      if (c <= 0.0f) inside[index] = std::pair{0.0f, 1.0f};
      continue;
    }
    const float b = 2.0f * dot3(from, path);
    const float disc = b * b - 4.0f * pathSquared * c;
    if (disc < 0.0f) continue;
    const float root = std::sqrt(disc);
    const float in = std::max((-b - root) / (2.0f * pathSquared), 0.0f);
    const float out = std::min((-b + root) / (2.0f * pathSquared), 1.0f);
    if (in <= out && in <= 1.0f && out >= 0.0f) inside[index] = std::pair{in, out};
  }
  std::optional<float> first;
  for (const auto& span : inside)
    if (span && (!first || span->first < *first)) first = span->first;
  if (!first) return std::nullopt;
  KeeperReach result;
  result.along = *first;
  if (inside[0] && inside[1])
  {
    const float in = std::max(inside[0]->first, inside[1]->first);
    if (in <= std::min(inside[0]->second, inside[1]->second)) result.bothAlong = in;
  }
  const Point3F ballAt = along3(start, end, *first);
  // A hair of slack: the far shoulder's sphere touching it too.
  constexpr float BOTH_SLACK = 1e-3f;
  result.bothHands =
      std::hypot(ballAt.x - shoulders[0].x, ballAt.y - shoulders[0].y, ballAt.z - shoulders[0].z) <=
          reach + BOTH_SLACK &&
      std::hypot(ballAt.x - shoulders[1].x, ballAt.y - shoulders[1].y, ballAt.z - shoulders[1].z) <=
          reach + BOTH_SLACK;
  result.gap = std::hypot(ballAt.x - at.x, ballAt.y - at.y);
  result.stretch = keeperStretchBinturong(keeper, at, {ballAt.x, ballAt.y, ballAt.z - feet});
  result.shoulders = {at.x, at.y, shoulderZ};
  result.handTop = shoulderZ + arm + H::HAND_RADIUS_METRES;
  return result;
}

void MatchEngine::bounceOffKeeperHandBinturong(MatchPlayer& keeper, const ShotBodyHit& hit)
{
  beginKeeperHandRebound(keeper);
  bounceShotOffBodyBinturong(keeper, hit);
}

void MatchEngine::keeperClaimBinturong(MatchPlayer& keeper, const KeeperReach& reach)
{
  // Goalkeeper plan step 7. The brain's stand-in: with an opponent close he
  // punches it, both fists driven through it (Handling), the face turned
  // away from goal; otherwise he means to catch it, and his hands hold it as
  // they hold a shot (both on it, within his Handling for its pace and his
  // stretch, a small chance of a fumble), else it springs off them.
  using K = MatchTuning::KeeperClaim;
  ++(keeper.isHomeTeam ? stats.homeKeeperClaimAttempts : stats.awayKeeperClaimAttempts);
  keepers[keeper.isHomeTeam ? 0 : 1].state = GoalkeeperState::CLAIM;
  const Vector2F atMetres = asVector(ball.position);
  const Point3F ballAt{atMetres.x, atMetres.y, ball.z};
  const Point3F fromShoulders = minus(ballAt, reach.shoulders);
  const float size = std::sqrt(dot3(fromShoulders, fromShoulders));
  ShotBodyHit hit;
  hit.part = ShotBodyPart::HAND;
  hit.normal = size > 1e-5f ? Point3F{fromShoulders.x / size, fromShoulders.y / size,
                                      fromShoulders.z / size}
                            : Point3F{0.0f, 0.0f, 1.0f};
  hit.surfaceVelocity = {keeper.velocity.x, keeper.velocity.y, keeper.jumpSpeed};
  hit.bothHands = reach.bothHands;
  hit.stretch = reach.stretch;
  // How close an attacker can be before he punches: Command of Area.
  const float near = alongStatScale(keeper.derivedStat(DerivedStat::COMMAND_OF_AREA),
                                    K::PUNCH_NEAR_METRES_POOR, K::PUNCH_NEAR_METRES_BEST);
  bool crowded = false;
  for (const MatchPlayer& other : players)
    if (active(other) && other.isHomeTeam != keeper.isHomeTeam &&
        length(Vector2F{asVector(other.position).x - atMetres.x,
                        asVector(other.position).y - atMetres.y}) <= near)
      crowded = true;
  if (crowded)
  {
    const float punch = alongStatScale(keeper.derivedStat(DerivedStat::HANDLING), K::PUNCH_SPEED_POOR,
                                       K::PUNCH_SPEED_BEST);
    hit.surfaceVelocity = {hit.surfaceVelocity.x + hit.normal.x * punch,
                           hit.surfaceVelocity.y + hit.normal.y * punch,
                           hit.surfaceVelocity.z + hit.normal.z * punch};
    hit.parryAim = true;
    ++stats.keeperPunches;
    ++statsOf(keeper).clearances;
    bounceOffKeeperHandBinturong(keeper, hit);
    return;
  }
  if (keeperHandsHoldShotBinturong(keeper, hit, false))
  {
    ++(keeper.isHomeTeam ? stats.homeKeeperClaims : stats.awayKeeperClaims);
    recordPassCompletion(keeper);
    const PitchPoint wasAt = ball.position;
    const float wasMetres = ball.z;
    setPossession(keeper);
    takeInHandsBinturong(keeper, wasAt, wasMetres);
    keeper.actionCooldown = keeperHoldSecondsBinturong(keeper);
    return;
  }
  ++stats.keeperClaimParries;
  bounceOffKeeperHandBinturong(keeper, hit);
}

bool MatchEngine::keeperCanGatherBinturong(const MatchPlayer& player) const
{
  if (!player.isGoalkeeper || getBody(player.isHomeTeam) != BodyId::BINTURONG) return true;
  return length(player.velocity) <= MatchTuning::Stride::WALK_TOP_SPEED &&
         player.slowSeconds >= MatchTuning::KeeperSmother::GATHER_SECONDS;
}

namespace
{
/** From `speed` (m/s, towards the point), how long his sprint profile
 * (push `push` m/s^2 from a standstill, fading to none at `top`) takes to
 * cover `metres`. */
float sprintSeconds(float metres, float speed, float push, float top)
{
  if (metres <= 0.0f) return 0.0f;
  top = std::max(top, 0.5f);
  speed = std::clamp(speed, 0.0f, top * 0.999f);
  const float k = std::max(push, EPSILON) / top;
  const auto covered = [&](float seconds)
  { return top * seconds - (top - speed) * (1.0f - std::exp(-k * seconds)) / k; };
  float low = 0.0f;
  float high = 0.5f;
  while (covered(high) < metres && high < 30.0f) high *= 2.0f;
  for (int step = 0; step < 24; ++step)
  {
    const float mid = 0.5f * (low + high);
    (covered(mid) < metres ? low : high) = mid;
  }
  return high;
}

/** As sprintSeconds, but arriving no faster than `arrive` (m/s), braking at
 * `brake` (m/s^2) at the end: the quickest such run (how fast he can get
 * there and be slow enough). */
float arriveSlowSeconds(float metres, float speed, float push, float top, float brake,
                        float arrive)
{
  if (metres <= 0.0f) return speed <= arrive ? 0.0f : (speed - arrive) / brake;
  top = std::max(top, 0.5f);
  speed = std::clamp(speed, 0.0f, top * 0.999f);
  const float k = std::max(push, EPSILON) / top;
  const float stop = [&]
  { return speed > arrive ? (speed * speed - arrive * arrive) / (2.0f * brake) : 0.0f; }();
  // Too fast to slow in time: he runs past and comes back (a rough turn).
  if (stop > metres) return (speed - arrive) / brake + 2.0f * (stop - metres) / std::max(arrive, 1.0f);
  // The peak speed he reaches before braking, by halving.
  const auto accelSeconds = [&](float peak)
  { return peak <= speed ? 0.0f : -std::log((top - peak) / (top - speed)) / k; };
  const auto accelMetres = [&](float peak)
  {
    const float t = accelSeconds(peak);
    return top * t - (peak - speed) / k;
  };
  const auto brakeMetres = [&](float peak)
  { return peak > arrive ? (peak * peak - arrive * arrive) / (2.0f * brake) : 0.0f; };
  float low = std::max(speed, arrive);
  float high = top * 0.999f;
  if (accelMetres(high) + brakeMetres(high) <= metres)
  {
    // Up to near top speed, a stretch at it, then braking.
    const float cruise = metres - accelMetres(high) - brakeMetres(high);
    return accelSeconds(high) + cruise / top + (high - arrive) / brake;
  }
  for (int step = 0; step < 24; ++step)
  {
    const float mid = 0.5f * (low + high);
    (accelMetres(mid) + brakeMetres(mid) < metres ? low : high) = mid;
  }
  return accelSeconds(low) + std::max(low - arrive, 0.0f) / brake;
}
}  // namespace

bool MatchEngine::considerSmotherBinturong(MatchPlayer& keeper)
{
  // Goalkeeper plan step 8, the brain's stand-in until One on Ones and
  // Rushing Out: a loose ball in his area (nobody's, or an attacker's touch
  // gone out of his reach). Along its roll ahead: when he could play it on
  // foot, when an attacker could, and when his dive would get there. On
  // foot first: he runs (nothing here). An attacker first on foot but his
  // dive sooner: he dives for it, aimed low at where it will be, as late as
  // he still beats the attacker.
  using S = MatchTuning::KeeperSmother;
  using D = MatchTuning::KeeperDive;
  using H = MatchTuning::KeeperHands;
  GoalkeeperControl& control = keepers[keeper.isHomeTeam ? 0 : 1];
  if (!keeper.isGoalkeeper || !active(keeper) || keeper.fallen || keeper.airborne() ||
      control.dive.active || state != MatchState::PLAYING ||
      getBody(keeper.isHomeTeam) != BodyId::BINTURONG ||
      !inPenaltyArea(keeper.position, keeper.isHomeTeam) || getPossessionState().isShot ||
      (getPossessionState().isPass && getPossessionState().passByHome == keeper.isHomeTeam) ||
      (ball.kickerLockout > 0.0f && getLegacyContactActor() == keeper.player))
    return false;
  if (getPossessionAttachment())
  {
    const MatchPlayer* owner = findMatchPlayer(getPossessionAttachment());
    if (!hasTouchClaim() || !owner || owner->isHomeTeam == keeper.isHomeTeam || ballInReach(*owner))
      return false;
  }
  const float height = keeper.heightMetres;
  if (ball.z > S::HIGHEST_HEIGHT_SHARE * height) return false;
  const float agility =
      std::clamp((keeper.derivedStat(DerivedStat::AGILITY) - STAT_1_RATING) /
                     (STAT_99_RATING - STAT_1_RATING),
                 0.0f, 1.0f);
  // His midriff's reach in a smother, not his fingertips'.
  const float fullReach = smotherReachBinturong(keeper);
  const float fullStretch =
      D::STRETCH_SECONDS_POOR + (D::STRETCH_SECONDS_BEST - D::STRETCH_SECONDS_POOR) * agility;
  const Vector2F centre = asVector(keeper.position);
  const float keeperTop = std::max(currentTopSpeed(keeper), 1.0f);
  const float keeperPush = playerAcceleration(keeper, 1.0f);
  const float keeperBrake = playerBraking(keeper, 1.0f);
  // Already bent to it, the rest of his gather.
  const float gatherLeft =
      length(keeper.velocity) <= MatchTuning::Stride::WALK_TOP_SPEED
          ? std::max(S::GATHER_SECONDS - keeper.slowSeconds, 0.0f)
          : S::GATHER_SECONDS;
  // How fast he is going towards a point (m/s).
  const auto towards = [](const MatchPlayer& runner, Vector2F from, Vector2F to)
  {
    const Vector2F way{to.x - from.x, to.y - from.y};
    const float size = length(way);
    return size > EPSILON
               ? std::max(0.0f, (runner.velocity.x * way.x + runner.velocity.y * way.y) / size)
               : 0.0f;
  };

  // Its roll ahead (its own physics, run and put back).
  const BallMotion saved = ballMotion();
  std::optional<float> attackerFirst;
  std::optional<float> footFirst;
  std::optional<std::pair<float, Point3F>> diveFirst;
  Vector2F ballWay{};
  for (float seconds = S::LOOK_STEP_SECONDS; seconds <= S::LOOK_SECONDS;
       seconds += S::LOOK_STEP_SECONDS)
  {
    integrateBall(S::LOOK_STEP_SECONDS);
    const Vector2F at = asVector(ball.position);
    const float z = ball.z;
    if (!inPenaltyArea(ball.position, keeper.isHomeTeam)) break;
    if (!attackerFirst)
      for (const MatchPlayer& other : players)
      {
        if (!active(other) || other.fallen || other.isHomeTeam == keeper.isHomeTeam) continue;
        // He gets his feet to it at any pace: his sprint profile.
        const Vector2F them = asVector(other.position);
        const float metres =
            std::max(length(Vector2F{at.x - them.x, at.y - them.y}) - feetReach(other), 0.0f);
        if (sprintSeconds(metres, towards(other, them, at), playerAcceleration(other, 1.0f),
                          std::max(currentTopSpeed(other), 1.0f)) <= seconds)
        {
          attackerFirst = seconds;
          break;
        }
      }
    // A pick-up: there within his ground reach at walking pace or slower,
    // then bent to it (user 2026-11-09).
    const float gap = length(Vector2F{at.x - centre.x, at.y - centre.y});
    // His dive from his run: from where his run carries him by full
    // stretch (it carries on through the air).
    const float carriedSeconds =
        fullStretch * std::clamp(gap / std::max(fullReach, EPSILON), D::SHORTEST_STRETCH_SHARE, 1.0f);
    const float diveGap = length(Vector2F{at.x - (centre.x + keeper.velocity.x * carriedSeconds),
                                          at.y - (centre.y + keeper.velocity.y * carriedSeconds)});
    const float diveSeconds =
        fullStretch * std::clamp(diveGap / std::max(fullReach, EPSILON), D::SHORTEST_STRETCH_SHARE, 1.0f);
    const float pickReach = MatchRules::goalkeeperHandlingRadiusMetres(
        height, keeper.goalkeeping, std::max(z, 0.0f), false);
    if (!footFirst &&
        arriveSlowSeconds(std::max(gap - pickReach, 0.0f), towards(keeper, centre, at), keeperPush,
                          keeperTop, keeperBrake, MatchTuning::Stride::WALK_TOP_SPEED) +
                gatherLeft <=
            seconds)
      footFirst = seconds;
    if (!diveFirst && diveGap <= fullReach && diveSeconds <= seconds)
    {
      diveFirst = std::pair{seconds, Point3F{at.x - centre.x, at.y - centre.y,
                                             std::max(z, MatchTuning::Units::BALL_RADIUS_METRES)}};
      ballWay = ball.velocity;
    }
    if (attackerFirst && footFirst && diveFirst) break;
  }
  setBallMotion(saved);
  if (!attackerFirst || (footFirst && *footFirst <= *attackerFirst) || !diveFirst ||
      diveFirst->first >= *attackerFirst ||
      *attackerFirst - diveFirst->first > S::DECIDE_SLACK_SECONDS)
    return false;
  const double now = getSimulatedSeconds();
  control.state = GoalkeeperState::DIVE;
  control.timer = 0.0f;
  control.dive.decidedFor = getPossessionState().pathSince;
  // A still ball: across his own line to it.
  if (length(ballWay) <= 0.1f) ballWay = {diveFirst->second.x, diveFirst->second.y};
  planKeeperSmotherBinturong(keeper, control.dive, now, diveFirst->second, ballWay,
                             now + static_cast<double>(diveFirst->first));
  ++stats.keeperSmotherDives;
  return true;
}

std::optional<PitchPoint> MatchEngine::ownGoalThreatBinturong(const MatchPlayer& keeper)
{
  // Only a free ball (nobody has it, not a shot) in his half, rolling
  // towards his goal line.
  using G = MatchTuning::Goalkeeper;
  using U = MatchTuning::Units;
  if (!keeper.isGoalkeeper || !active(keeper) || state != MatchState::PLAYING ||
      getPossessionState().isShot || getPossessionAttachment())
    return std::nullopt;
  const auto depthOf = [&keeper, this](PitchPoint at)
  {
    return keeper.isHomeTeam ? pitch.fractionX(at) : 1.0f - pitch.fractionX(at);
  };
  if (depthOf(ball.position) > MatchTuning::Pitch::CENTRE ||
      ball.velocity.x * (keeper.isHomeTeam ? -1.0f : 1.0f) <= 0.0f)
    return std::nullopt;

  // How fast a runner is going towards a point (m/s).
  const auto towards = [](const MatchPlayer& runner, Vector2F from, Vector2F to)
  {
    const Vector2F way{to.x - from.x, to.y - from.y};
    const float size = length(way);
    return size > EPSILON
               ? std::max(0.0f, (runner.velocity.x * way.x + runner.velocity.y * way.y) / size)
               : 0.0f;
  };
  // Whether he gets his feet (or body: no hands) to the ball at `at`, `z`
  // metres up, within `seconds`: his sprint profile, as the smother's.
  const auto reaches = [&](const MatchPlayer& runner, Vector2F at, float z, float seconds)
  {
    if (z > runner.heightMetres) return false;
    const Vector2F them = asVector(runner.position);
    const float metres =
        std::max(length(Vector2F{at.x - them.x, at.y - them.y}) - feetReach(runner), 0.0f);
    return sprintSeconds(metres, towards(runner, them, at), playerAcceleration(runner, 1.0f),
                         std::max(currentTopSpeed(runner), 1.0f)) <= seconds;
  };

  // Its roll ahead (its own physics, run and put back).
  const BallMotion saved = ballMotion();
  std::optional<PitchPoint> meet;
  std::optional<PitchPoint> crossing;
  for (float seconds = G::OWN_GOAL_LOOK_STEP_SECONDS; seconds <= G::OWN_GOAL_LOOK_SECONDS;
       seconds += G::OWN_GOAL_LOOK_STEP_SECONDS)
  {
    const PitchPoint before = ball.position;
    integrateBall(G::OWN_GOAL_LOOK_STEP_SECONDS);
    const float z = ball.z;
    const float depth = depthOf(ball.position);
    if (depth <= 0.0f)
    {
      // Over his line: in, if between the posts and under the bar.
      const float beforeDepth = depthOf(before);
      const float share = beforeDepth / std::max(beforeDepth - depth, EPSILON);
      const float y = before.y + (ball.position.y - before.y) * share;
      if (std::abs(y - pitch.atY(MatchTuning::Pitch::CENTRE)) <=
              MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES + U::BALL_RADIUS_METRES &&
          z < U::CROSSBAR_HEIGHT_METRES + U::BALL_RADIUS_METRES)
        crossing = PitchPoint{pitch.atX(keeper.isHomeTeam ? 0.0f : 1.0f), y};
      break;
    }
    // Stopped short.
    if (length(ball.velocity) < 0.2f && z <= U::BALL_RADIUS_METRES + 0.01f) break;
    const Vector2F at = asVector(ball.position);
    // Someone else gets to it first (the pass's receiver, a defender, an
    // attacker): it is played before it goes in, not his to cover.
    bool someoneElse = false;
    for (const MatchPlayer& other : players)
    {
      if (&other == &keeper || !active(other) || other.fallen) continue;
      if (reaches(other, at, z, seconds))
      {
        someoneElse = true;
        break;
      }
    }
    if (someoneElse) break;
    if (!meet && reaches(keeper, at, z, seconds)) meet = ball.position;
  }
  setBallMotion(saved);
  if (!crossing) return std::nullopt;
  return meet ? meet : crossing;
}

void MatchEngine::takeInHandsBinturong(const MatchPlayer& keeper, PitchPoint wasAt, float wasMetres)
{
  if (getBody(keeper.isHomeTeam) != BodyId::BINTURONG || !keeper.isGoalkeeper ||
      getPossessionAttachment() != keeper.player || !inPenaltyArea(keeper.position, keeper.isHomeTeam))
    return;
  const Vector2F centre = asVector(keeper.position);
  const Vector2F at = asVector(wasAt);
  beginKeeperHold({at.x - centre.x, at.y - centre.y, std::max(wasMetres, 0.0f)});
  const Point3F held = heldBallBinturong(keeper);
  ball.position = asPoint({held.x, held.y});
  ball.z = held.z;
  ++stats.keeperBallsInHands;
}

Point3F MatchEngine::heldBallBinturong(const MatchPlayer& keeper) const
{
  // From where he took it (or, after a dive, where it was as he got up) to
  // his chest, in front of him, over the gather; then there, moving with
  // him.
  using K = MatchTuning::KeeperHeld;
  const Vector2F centre = asVector(keeper.position);
  const Vector2F facing = normalized(
      {std::cos(keeper.facingAngle),
       std::sin(keeper.facingAngle)});
  const Point3F chest{centre.x + facing.x * K::AHEAD_METRES, centre.y + facing.y * K::AHEAD_METRES,
                      K::HEIGHT_SHARE * keeper.heightMetres};
  const Point3F from{centre.x + getPossessionState().heldFrom.x, centre.y + getPossessionState().heldFrom.y, getPossessionState().heldFrom.z};
  const float share = std::clamp(
      static_cast<float>(getSimulatedSeconds() - getPossessionState().heldSince) / K::GATHER_SECONDS, 0.0f, 1.0f);
  return along3(from, chest, share);
}

std::pair<PitchPoint, float> MatchEngine::heldReleaseBinturong(const MatchPlayer& keeper,
                                                             HeldRelease how) const
{
  using K = MatchTuning::KeeperHeld;
  const Vector2F centre = asVector(keeper.position);
  const Vector2F facing = normalized(
      {std::cos(keeper.facingAngle),
       std::sin(keeper.facingAngle)});
  const auto at = [&](float ahead)
  { return asPoint({centre.x + facing.x * ahead, centre.y + facing.y * ahead}); };
  switch (how)
  {
    case HeldRelease::ROLL:
      return {at(K::AHEAD_METRES), K::ROLL_RELEASE_METRES};
    case HeldRelease::OVERARM:
      return {at(-K::OVERARM_BEHIND_METRES), K::OVERARM_HEIGHT_SHARE * keeper.heightMetres};
    case HeldRelease::PUNT:
      return {at(K::AHEAD_METRES), K::PUNT_STRIKE_METRES};
    case HeldRelease::NONE:
      break;
  }
  return {ball.position, ball.z};
}

void MatchEngine::planKeeperHandsBinturong(const MatchPlayer& player, ShotHands& hands,
                                           Point3F start, Point3F end, Vector2F centre)
{
  using H = MatchTuning::KeeperHands;
  using K = MatchTuning::ShotBlock;
  const auto rating = [](float stat)
  { return std::clamp((stat - STAT_1_RATING) / (STAT_99_RATING - STAT_1_RATING), 0.0f, 1.0f); };
  const float reflexes = rating(player.derivedStat(DerivedStat::REFLEXES));
  const float agility = rating(player.derivedStat(DerivedStat::AGILITY));
  const float handSpeed = H::HAND_SPEED_POOR + (H::HAND_SPEED_BEST - H::HAND_SPEED_POOR) * agility;
  const Vector2F facing = normalized(
      {std::cos(player.facingAngle),
       std::sin(player.facingAngle)});
  const Vector2F left{-facing.y, facing.x};

  // The ball's line as it passes him, moving as he is; past him already,
  // his hands stay where they are.
  const auto line = passingLineOf(start, end, substepStretchSeconds, player.velocity);
  if (!line || line->toNearest <= 0.0f) return;
  Point3F aim{line->nearest.x, line->nearest.y, line->height};
  const double ballThere = ballNow() + static_cast<double>(line->untilThere);
  const auto seconds = [&](std::size_t index, Point3F to)
  {
    const Point3F from = hands.start[index];
    return std::hypot(to.x - from.x, to.y - from.y, to.z - from.z) / handSpeed;
  };

  // The nearer hand to its line; time to spare with it, the less the
  // bigger his miss (as a leg put out).
  hands.near = aim.x * left.x + aim.y * left.y >= 0.0f ? 0 : 1;
  hands.spareSeconds = static_cast<float>(
      ballThere - (hands.from + seconds(hands.near, keeperHandReachBinturong(player, hands.near, aim))));
  const float rushed = std::clamp(1.0f - hands.spareSeconds / K::RUSHED_SECONDS, 0.0f, 1.0f);
  hands.errorRadius = (H::MISS_METRES_POOR + (H::MISS_METRES_BEST - H::MISS_METRES_POOR) * reflexes) *
                      (1.0f + (K::RUSHED_ERROR_FACTOR - 1.0f) * rushed);
  Point3F miss{};
  for (int attempt = 0; attempt < 16; ++attempt)
  {
    miss = {randomFloat(-1.0f, 1.0f), randomFloat(-1.0f, 1.0f), randomFloat(-1.0f, 1.0f)};
    if (miss.x * miss.x + miss.y * miss.y + miss.z * miss.z <= 1.0f) break;
  }
  if (const float size = std::hypot(miss.x, miss.y, miss.z); size > 1.0f)
    miss = {miss.x / size, miss.y / size, miss.z / size};
  aim = {aim.x + miss.x * hands.errorRadius, aim.y + miss.y * hands.errorRadius,
         aim.z + miss.z * hands.errorRadius};
  hands.error = hands.errorRadius * std::hypot(miss.x, miss.y, miss.z);

  // Both side by side across its line where the far hand reaches it too;
  // otherwise the nearer one there and the other as far towards it as it
  // goes.
  const std::size_t far = 1 - hands.near;
  const Point3F farReach = keeperHandReachBinturong(player, far, aim);
  hands.together = std::hypot(farReach.x - aim.x, farReach.y - aim.y, farReach.z - aim.z) < 1e-3f;
  std::array<Point3F, 2> wanted{aim, aim};
  if (hands.together)
  {
    const Vector2F across{-line->along.y, line->along.x};
    const float half = 0.5f * H::HANDS_APART_METRES;
    // The left hand on the side of the pair further to his left.
    const float sign = across.x * left.x + across.y * left.y >= 0.0f ? 1.0f : -1.0f;
    wanted[0] = {aim.x + across.x * half * sign, aim.y + across.y * half * sign, aim.z};
    wanted[1] = {aim.x - across.x * half * sign, aim.y - across.y * half * sign, aim.z};
  }
  for (std::size_t index = 0; index < 2; ++index)
  {
    const Point3F to = keeperHandReachBinturong(player, index, wanted[index]);
    hands.target[index] = {centre.x + to.x, centre.y + to.y, to.z};
    hands.seconds[index] = seconds(index, to);
  }
  hands.going = true;
  ++stats.keeperHandsWent;
  if (hands.together) ++stats.keeperHandsTogether;
  if (hands.spareSeconds < 0.0f) ++stats.keeperHandsLate;
}

bool MatchEngine::reachesBallBinturong(const MatchPlayer& /*player*/,
                                       float /*ballSpeed*/, float /*stretch*/)
{
  // No dice (pass block plan): a ball in his reach is his to play. Whether
  // he had time to judge it and set himself is the brain's, later; a ball
  // he doesn't play meets his body.
  return true;
}

float MatchEngine::touchSkillBinturong(const MatchPlayer& player,
                                       bool hands) const
{
  using B = MatchTuning::Ball;
  return hands ? player.goalkeeping
               : std::max({player.dribbling,
                           player.passing * B::PASSING_TOUCH_WEIGHT,
                           player.isGoalkeeper
                               ? player.goalkeeping * B::KEEPER_FEET_WEIGHT
                               : 0.0f});
}

bool MatchEngine::controlsBallBinturong(const MatchPlayer& player,
                                        float skill, float ballSpeed,
                                        float heightMetres)
{
  using B = MatchTuning::Ball;
  const float controlChance =
      std::clamp(B::BASE_CONTROL_CHANCE + skill * B::TOUCH_SKILL_BONUS -
                     ballSpeed * B::SPEED_CONTROL_PENALTY -
                     (heightMetres > bodyLinesBinturong(player).feet ? 0.08f : 0.0f),
                 B::MIN_CONTROL_CHANCE, B::MAX_CONTROL_CHANCE);
  return randomFloat(0.0f, 1.0f) <= controlChance;
}

Vector2F MatchEngine::heavyTouchVelocityBinturong(const MatchPlayer& player,
                                                  Vector2F velocity)
{
  using B = MatchTuning::Ball;
  const float angle =
      randomFloat(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);
  const float popSpeed =
      randomFloat(B::MIN_HEAVY_TOUCH_SPEED, B::MAX_HEAVY_TOUCH_SPEED);
  Vector2F popped{
      velocity.x * B::HEAVY_TOUCH_RETAINED + std::cos(angle) * popSpeed,
      velocity.y * B::HEAVY_TOUCH_RETAINED + std::sin(angle) * popSpeed};
  // Near his own goal a player's heavy touch still goes away from it.
  const float ownDepth =
      player.isHomeTeam ? player.position.x : 1.0f - player.position.x;
  const float outward = player.isHomeTeam ? 1.0f : -1.0f;
  if (ownDepth < MatchTuning::SetPiece::CROSS_CLEARANCE_DEPTH &&
      popped.x * outward < 0.0f)
    popped.x = -popped.x;
  return popped;
}

float MatchEngine::clearanceSpeedBinturong(const MatchPlayer& /*defender*/)
{
  return MatchTuning::Defending::CLEARANCE_SPEED * randomFloat(0.85f, 1.0f);
}

float MatchEngine::headerClearanceSpeedBinturong(const MatchPlayer& /*header*/)
{
  // Not used for its pace: a clearing header goes as hard as he can head it
  // (headerPaceBinturong).
  return MatchTuning::Aerial::HEADER_CLEARANCE_SPEED;
}

float MatchEngine::headerPowerBinturong(const MatchPlayer& header) const
{
  // The pace he gives a ball that arrives with almost no pace of its own
  // (heading technique plan step 3): by Heading alone, 8 m/s at 1 to
  // 17 m/s at 99, in a straight line. Met with the top of his head, a
  // share of it (step 5).
  using A = MatchTuning::Aerial;
  const float share = (header.derivedStat(DerivedStat::HEADING) - STAT_1_RATING) /
                      (STAT_99_RATING - STAT_1_RATING);
  const float power = std::max(
      A::HEADER_POWER_LOW + (A::HEADER_POWER_HIGH - A::HEADER_POWER_LOW) * share,
      0.0f);
  return metWithTopOfHeadBinturong() ? power * A::TOP_POWER_SHARE : power;
}

bool MatchEngine::metWithTopOfHeadBinturong() const
{
  // Only while a header met in the sweep is being played.
  return strikeAt && lastHeadPart == HeadPart::TOP;
}

void MatchEngine::headerLimitsBinturong(const MatchPlayer& header,
                                        const TouchMoment& moment,
                                        ContactRequest& request,
                                        ContactProfile& profile) const
{
  // What his head can do with the ball where he meets it (heading
  // technique plan step 5); beyond it, he does the nearest he can.
  using A = MatchTuning::Aerial;
  const float pace = std::hypot(request.speed, request.lift);
  if (metWithTopOfHeadBinturong())
  {
    // The top of his head glances it on: not far off the way it was going,
    // and flat or upward.
    const float level = length(ball.velocity);
    if (level > MatchTuning::Ball::STOP_SPEED)
    {
      const Vector2F onward{ball.velocity.x / level, ball.velocity.y / level};
      const float turn =
          std::clamp(std::atan2(onward.x * request.aim.y - onward.y * request.aim.x,
                                onward.x * request.aim.x + onward.y * request.aim.y),
                     -A::TOP_MAX_TURN, A::TOP_MAX_TURN);
      request.aim = {onward.x * std::cos(turn) - onward.y * std::sin(turn),
                     onward.x * std::sin(turn) + onward.y * std::cos(turn)};
    }
    profile.lowestElevation = 0.0f;
  }
  else
  {
    // His forehead sends it down only from above it: as steeply as his
    // forehead, where it is, is above the ball's centre, against its radius
    // (it meets the upper half of the ball).
    const float above =
        headTopBinturong(header) - A::HEAD_OFFSET_METRES - moment.height;
    profile.lowestElevation = -std::asin(std::clamp(
        above / MatchTuning::Units::BALL_RADIUS_METRES, 0.0f, 1.0f));
  }
  const float elevation =
      std::max(std::atan2(request.lift, request.speed), profile.lowestElevation);
  request.speed = pace * std::cos(elevation);
  request.lift = pace * std::sin(elevation);
}

float MatchEngine::headerFloorBinturong(const MatchPlayer& header, Vector2F aim,
                                        float elevation) const
{
  // The least pace the ball can leave his head with along `aim` at
  // `elevation` (heading technique plan step 6). His head pushes along one
  // line (headerCeilingBinturong): across it the ball keeps its pace, and
  // along it he can take off only so much of the pace it hits him with:
  // at least a share comes back off it, 50% at Heading 1 to 20% at 99.
  using A = MatchTuning::Aerial;
  const float rating = (header.derivedStat(DerivedStat::HEADING) - STAT_1_RATING) /
                       (STAT_99_RATING - STAT_1_RATING);
  const float keep =
      A::CUSHION_KEEP_LOW + (A::CUSHION_KEEP_HIGH - A::CUSHION_KEEP_LOW) * rating;
  return cushionFloorBinturong(keep, aim, elevation);
}

float MatchEngine::cushionFloorBinturong(float keep, Vector2F aim,
                                         float elevation) const
{
  // The body part pushes along one line: across it the ball keeps its
  // pace, and along it at least `keep` of the pace it hits him with comes
  // back off it.
  const float c = std::cos(elevation);
  const std::array<float, 3> way{aim.x * c, aim.y * c, std::sin(elevation)};
  const std::array<float, 3> in{ball.velocity.x, ball.velocity.y, ball.velocityZ};
  // Whether leaving at `pace` that way keeps enough of the pace into his
  // head: the push from the incoming velocity to it is along his line.
  const auto possible = [&](float pace)
  {
    std::array<float, 3> push{};
    float size = 0.0f;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
      push[axis] = pace * way[axis] - in[axis];
      size += push[axis] * push[axis];
    }
    size = std::sqrt(size);
    if (size < 1.0e-4f) return true;
    float into = 0.0f;
    float out = 0.0f;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
      into -= in[axis] * push[axis] / size;
      out += pace * way[axis] * push[axis] / size;
    }
    return out >= keep * into;
  };
  float low = 0.0f;
  float high = std::hypot(in[0], in[1], in[2]) + 1.0f;
  if (possible(low)) return 0.0f;
  for (int round = 0; round < 30; ++round)
  {
    const float middle = 0.5f * (low + high);
    (possible(middle) ? high : low) = middle;
  }
  return high;
}

void MatchEngine::cushionHeaderBinturong(MatchPlayer& header)
{
  // A cushioned header (heading technique plan step 6): as softly as his
  // head can send it. The least pace comes straight back along the line it
  // came on (all its pace into his head), so he heads it that way, within
  // what his head can do there (headerLimitsBinturong), at the least it can
  // leave with; it drops in front of him. He owns it from here, as after a
  // first touch, and collects it once it is down at his feet. A
  // team-mate's ball counts as completed when he collects it.
  const bool receivingPass = getPossessionState().isPass && getPossessionState().passByHome == header.isHomeTeam;
  const Player* passer = getLastPossessionAttachment();
  if (!receivingPass) recordPassCompletion(header);
  const TouchMoment moment = strikeMomentBinturong(header);
  ContactRequest request;
  request.kind = ContactRequest::Kind::STRIKE;
  request.strike = StrikeKind::HEADER;
  const float level = length(ball.velocity);
  const Vector2F facing = normalized(
      {std::cos(header.facingAngle),
       std::sin(header.facingAngle)});
  request.aim = level > MatchTuning::Ball::STOP_SPEED
                    ? Vector2F{-ball.velocity.x / level, -ball.velocity.y / level}
                    : facing;
  request.speed = level;
  request.lift = -ball.velocityZ;
  ContactProfile limits = strikeProfileBinturong(StrikeKind::HEADER);
  headerLimitsBinturong(header, moment, request, limits);
  const float elevation = std::atan2(request.lift, request.speed);
  request.paceFloor = headerFloorBinturong(header, request.aim, elevation);
  request.speed = request.paceFloor * std::cos(elevation);
  request.lift = request.paceFloor * std::sin(elevation);
  const ContactResult result = contactBinturong(header, request, moment);

  takePossession(header);
  sendBallOff(header,
              PossessionControl::retainingTouchPosition(pitch, possessionId, moment.contactPoint),
              moment.height,
              result.outcome.velocity, result.outcome.verticalSpeed, BallSpin{},
              0.0f);
  noteCushionedHeader(header, receivingPass ? passer : nullptr, moment,
                      result.outcome.actualSpeed);
}

void MatchEngine::bodyTouchLimitsBinturong(const MatchPlayer& player,
                                           ContactRequest& request,
                                           bool thigh) const
{
  // His chest sends it out the way it faces, which he can turn so far, up
  // and never down; his thigh only so far round, and only upward.
  using B = MatchTuning::Ball;
  constexpr float DEGREES = std::numbers::pi_v<float> / 180.0f;
  const Vector2F facing = normalized(
      {std::cos(player.facingAngle),
       std::sin(player.facingAngle)});
  const float off = std::atan2(facing.x * request.aim.y - facing.y * request.aim.x,
                               facing.x * request.aim.x + facing.y * request.aim.y);
  const float side =
      (thigh ? B::THIGH_MAX_SIDE_DEGREES : B::CHEST_MAX_SIDE_DEGREES) * DEGREES;
  if (std::abs(off) > side) request.aim = rotatedBy(facing, off > 0.0f ? side : -side);
  const float pace = std::hypot(request.speed, request.lift);
  const float elevation = std::clamp(
      std::atan2(request.lift, request.speed),
      thigh ? B::THIGH_MIN_UP_DEGREES * DEGREES : 0.0f,
      (thigh ? B::THIGH_MAX_UP_DEGREES : B::CHEST_MAX_UP_DEGREES) * DEGREES);
  request.speed = pace * std::cos(elevation);
  request.lift = pace * std::sin(elevation);
}

void MatchEngine::playBodyTouchBinturong(MatchPlayer& player,
                                         const TouchMoment& moment, bool thigh)
{
  // A chest or thigh touch (chest and thigh control plan steps 3 and 4): as
  // softly as it can send it, back the way it came (the least pace: no
  // turn) and up (a real chest cushion goes up and drops in front of him; a
  // thigh pops it up to drop at his feet), within what that part can do
  // (bodyTouchLimitsBinturong), never faster than it came. He owns it from
  // here, as after a first touch, and collects it once it is down at his
  // feet. A team-mate's ball counts as completed when he collects it. A
  // thigh touch plants him for a moment.
  using B = MatchTuning::Ball;
  const bool receivingPass = getPossessionState().isPass && getPossessionState().passByHome == player.isHomeTeam;
  const Player* passer = getLastPossessionAttachment();
  if (!receivingPass) recordPassCompletion(player);
  const Vector2F facing = normalized(
      {std::cos(player.facingAngle),
       std::sin(player.facingAngle)});
  const float level = length(ball.velocity);
  ContactRequest request;
  request.kind =
      thigh ? ContactRequest::Kind::THIGH_TOUCH : ContactRequest::Kind::CHEST_TOUCH;
  request.aim = level > B::STOP_SPEED
                    ? Vector2F{-ball.velocity.x / level, -ball.velocity.y / level}
                    : facing;
  const float up = (thigh ? B::THIGH_CUSHION_UP_DEGREES : B::CHEST_CUSHION_UP_DEGREES) *
                   std::numbers::pi_v<float> / 180.0f;
  request.speed = std::cos(up);
  request.lift = std::sin(up);
  bodyTouchLimitsBinturong(player, request, thigh);
  const float elevation = std::atan2(request.lift, request.speed);
  const float rating =
      (player.derivedStat(DerivedStat::FIRST_TOUCH) - STAT_1_RATING) /
      (STAT_99_RATING - STAT_1_RATING);
  const float keepLow = thigh ? B::THIGH_KEEP_LOW : B::CHEST_KEEP_LOW;
  const float keepHigh = thigh ? B::THIGH_KEEP_HIGH : B::CHEST_KEEP_HIGH;
  const float keep = keepLow + (keepHigh - keepLow) * rating;
  request.paceFloor = cushionFloorBinturong(keep, request.aim, elevation);
  request.paceCeiling = std::hypot(ball.velocity.x, ball.velocity.y, ball.velocityZ);
  request.speed = request.paceFloor * std::cos(elevation);
  request.lift = request.paceFloor * std::sin(elevation);
  const ContactResult result = contactBinturong(player, request, moment);

  takePossession(player);
  sendBallOff(player,
              PossessionControl::retainingTouchPosition(pitch, possessionId, moment.contactPoint),
              moment.height,
              result.outcome.velocity, result.outcome.verticalSpeed, BallSpin{},
              0.0f);
  beginTouchClaim(receivingPass ? passer : nullptr, true);
  ball.touchContactGap = moment.gap;
  ball.touchContactHeight = moment.height;
  ball.touchLaunchSpeed = result.outcome.actualSpeed;
  const float gap = distance(player.position, ball.position);
  ball.touchGap = gap;
  ball.touchPeakGap = gap;
  ball.dribbleExposure = gap;
  if (thigh) player.plantedSeconds = B::THIGH_PLANTED_SECONDS;
  ball.dribbleTouchLength = std::max(gap, MatchTuning::Dribble::MAX_TOUCH_METRES);
}

float MatchEngine::headerCeilingBinturong(const MatchPlayer& header,
                                          Vector2F aim, float elevation,
                                          bool byAngle) const
{
  // The fastest the ball can leave his head along `aim` (metric unit, seen
  // from above) at `elevation` (rad, + up), as a ball bounces off a head
  // (heading technique plan step 3): his head pushes only along one line,
  // straight out of his forehead, which he angles. Across that line the
  // ball keeps the pace it came with; along it, a share of the pace it hits
  // him with springs back, and his power adds to that. So the ball leaves
  // with its incoming velocity plus (power + (1 + spring) x pace into his
  // head) along the line. With `byAngle`, his power is his body's share of
  // it for a header that far off the way he faces.
  using A = MatchTuning::Aerial;
  float power = headerPowerBinturong(header);
  if (byAngle)
  {
    const ContactProfile profile = strikeProfileBinturong(StrikeKind::HEADER);
    const Vector2F facing = normalized(
        {std::cos(header.facingAngle),
         std::sin(header.facingAngle)});
    const float degrees =
        std::acos(std::clamp(aim.x * facing.x + aim.y * facing.y, -1.0f, 1.0f)) *
        180.0f / std::numbers::pi_v<float>;
    power *= powerByAngleShare(degrees, profile.powerInwardSquare,
                               profile.powerBehind);
  }
  const float spring = A::HEADER_SPRING;
  const float c = std::cos(elevation);
  const std::array<float, 3> way{aim.x * c, aim.y * c, std::sin(elevation)};
  const std::array<float, 3> in{ball.velocity.x, ball.velocity.y, ball.velocityZ};
  // The power a launch at `pace` along the way asks of him: the push along
  // the line from the incoming velocity to the one he means, less what
  // springs back.
  const auto asked = [&](float pace)
  {
    std::array<float, 3> push{};
    float size = 0.0f;
    float into = 0.0f;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
      push[axis] = pace * way[axis] - in[axis];
      size += push[axis] * push[axis];
    }
    size = std::sqrt(size);
    if (size < EPSILON) return 0.0f;
    for (std::size_t axis = 0; axis < 3; ++axis)
      into += in[axis] * push[axis] / size;
    return size + (1.0f + spring) * into;
  };
  const float incoming = std::hypot(in[0], in[1], in[2]);
  float low = 0.0f;
  float high = power + 2.0f * incoming + 1.0f;
  if (asked(high) <= power) return high;
  for (int round = 0; round < 30; ++round)
  {
    const float middle = 0.5f * (low + high);
    (asked(middle) <= power ? low : high) = middle;
  }
  return low;
}

std::pair<float, float> MatchEngine::headerPaceBinturong(const MatchPlayer& header,
                                                         HeaderPace purpose,
                                                         PitchPoint target,
                                                         float speed,
                                                         float lift) const
{
  // The pace of a header, over the ground and upward (m/s), from the body
  // (heading technique plan step 3): to a spot, weighted to land on it at
  // a set elevation, no harder than he can head it that way (beyond, at his
  // limit, short); a clearance as hard as he can at its elevation; anything
  // else as hard as he can, at the elevation of `speed` and `lift`.
  using A = MatchTuning::Aerial;
  const Vector2F aim = metricDirection(ball.position, target);
  const float elevation = purpose == HeaderPace::TO_SPOT ? A::HEADER_PASS_ELEVATION
                          : purpose == HeaderPace::CLEAR ? A::HEADER_CLEAR_ELEVATION
                                                         : std::atan2(lift, speed);
  float pace = headerCeilingBinturong(header, aim, elevation);
  if (purpose == HeaderPace::TO_SPOT)
    pace = std::min(pace, landingPaceBinturong(distance(ball.position, target),
                                               elevation,
                                               ball.z));
  return {pace * std::cos(elevation), pace * std::sin(elevation)};
}

float MatchEngine::landingPaceBinturong(float metres, float elevation,
                                        float startHeight) const
{
  // The launch pace at `elevation` (rad) from `startHeight` (m) that first
  // lands `metres` away, in the match's own flight (bisection: harder, further).
  constexpr float DT = MatchTuning::Timing::FIXED_STEP_SECONDS /
                       static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
  const auto landing = [&](float pace)
  {
    FlightState flight{pace * std::cos(elevation), pace * std::sin(elevation),
                       startHeight, false, {}};
    float gone = 0.0f;
    for (float time = 0.0f; time < MatchTuning::Ball::MAX_FLIGHT_SECONDS &&
                            flight.horizontalSpeed > 0.0f && !flight.bounced;
         time += DT)
      gone += stepFlight(flight, DT);
    return gone;
  };
  float low = 0.0f;
  float high = 60.0f;
  for (int round = 0; round < 30; ++round)
  {
    const float middle = 0.5f * (low + high);
    (landing(middle) < metres ? low : high) = middle;
  }
  return high;
}

float MatchEngine::jumpingReachBinturong(const MatchPlayer& player) const
{
  return derivedPhysicalValue(DerivedStat::JUMPING_REACH,
                              player.derivedStat(DerivedStat::JUMPING_REACH));
}

std::optional<HeadPart> MatchEngine::headMeetBinturong(const MatchPlayer& player,
                                                       Vector2F toBall, float gap,
                                                       float heightMetres,
                                                       float feetHeight) const
{
  // Heading technique plan step 4: within a lean of the neck of his centre.
  // Up and down, where his head is then (aerial challenge plan step 1):
  // from a duck below his forehead (never below his head line, the top of
  // his shoulders where he is then) up to the top of his head, a ball
  // touching it included (shot block plan §3i: if his head can reach it,
  // he can head it).
  using A = MatchTuning::Aerial;
  const HeadBand band = headBandOf(player.heightMetres, feetHeight);
  const float forehead = band.forehead;
  if (gap > A::HEAD_REACH_METRES || heightMetres < band.lowest ||
      heightMetres > band.top + MatchTuning::Units::BALL_RADIUS_METRES)
    return std::nullopt;
  const Vector2F facing = normalized(
      {std::cos(player.facingAngle),
       std::sin(player.facingAngle)});
  // Beside or behind him the forehead can't get to it: only the top of his
  // head, for a ball at it.
  if (inTouchDeadZone(facing, toBall, gap, A::HEAD_COMFORT_GAP_METRES,
                      A::HEAD_DEAD_ZONE_DEGREES))
  {
    if (heightMetres < forehead) return std::nullopt;
    return HeadPart::TOP;
  }
  // Above his forehead, the top of his head.
  return heightMetres > forehead ? HeadPart::TOP : HeadPart::FOREHEAD;
}

float MatchEngine::headTopBinturong(const MatchPlayer& player) const
{
  return player.heightMetres + player.feetHeight;
}

float MatchEngine::feetAtBinturong(const MatchPlayer& player, float secondsBefore) const
{
  // Back along his flight from where it is at the step's end (players move
  // before the ball's sub-steps): f(T - s) = f(T) - v(T) s - g s^2 / 2. On
  // the ground then, 0.
  if (!player.airborne()) return 0.0f;
  return std::max(player.feetHeight - player.jumpSpeed * secondsBefore -
                      0.5f * MatchTuning::Ball::GRAVITY * secondsBefore * secondsBefore,
                  0.0f);
}

float MatchEngine::jumpRiseBinturong(const MatchPlayer& player, bool running) const
{
  // His running jump takes the top of his head to his Jumping reach; his
  // standing jump rises a share of that (aerial challenge plan step 1).
  return jumpRiseOf(jumpingReachBinturong(player), player.heightMetres, running);
}

void MatchEngine::stepJumpBinturong(MatchPlayer& player, float dt,
                                    const std::vector<JumpPathPoint>& path)
{
  // Aerial challenge plan step 1. In the air: his feet rise and fall under
  // gravity and his run carries on as it was (he can't steer); on landing,
  // his landing begins. On the ground he jumps for a header that would pass
  // above his standing head, timed so his head reaches the ball's height as
  // it arrives (perfectly, for now).
  using A = MatchTuning::Aerial;
  constexpr float G = MatchTuning::Ball::GRAVITY;
  // His run carries on as it was.
  const auto carry = [&]
  {
    const Vector2F moved = {player.velocity.x * dt, player.velocity.y * dt};
    player.position =
        moveBody(player.position,
               PitchPoint{player.position.x + moved.x, player.position.y + moved.y});
  };
  if (player.airborne())
  {
    player.feetHeight += player.jumpSpeed * dt - 0.5f * G * dt * dt;
    player.jumpSpeed -= G * dt;
    if (player.feetHeight <= 0.0f)
    {
      player.feetHeight = 0.0f;
      player.jumpSpeed = 0.0f;
      player.landingSeconds = player.jumpLandingSeconds;
    }
    carry();
    return;
  }
  player.landingSeconds = std::max(player.landingSeconds - dt, 0.0f);
  // A keeper going to claim it (goalkeeper plan step 7): he jumps for a
  // ball his hands can't reach standing, timed so the highest his hands
  // reach where it passes him is at its height as it arrives (perfectly, as
  // a header's jump is for now); as high as his Aerial Reach allows.
  if (player.isGoalkeeper)
  {
    if (path.empty() || player.landingSeconds > 0.0f || !keeperMayClaimBinturong(player) ||
        keepers[player.isHomeTeam ? 0 : 1].state != GoalkeeperState::CLAIM)
      return;
    const Vector2F at = asVector(player.position);
    std::optional<JumpPathPoint> closest;
    float closestGap = std::numeric_limits<float>::max();
    for (std::size_t index = 1; index < path.size(); ++index)
    {
      const auto rel = [&](const JumpPathPoint& point)
      {
        return Vector2F{point.at.x - (at.x + player.velocity.x * point.seconds),
                        point.at.y - (at.y + player.velocity.y * point.seconds)};
      };
      const Vector2F from = rel(path[index - 1]);
      const Vector2F to = rel(path[index]);
      const Vector2F step{to.x - from.x, to.y - from.y};
      const float stepSquared = step.x * step.x + step.y * step.y;
      const float share = stepSquared > EPSILON
                              ? std::clamp(-(from.x * step.x + from.y * step.y) / stepSquared, 0.0f, 1.0f)
                              : 0.0f;
      const float gap = length({from.x + step.x * share, from.y + step.y * share});
      if (gap > closestGap) break;
      closestGap = gap;
      closest = JumpPathPoint{path[index - 1].seconds + (path[index].seconds - path[index - 1].seconds) * share,
                              {}, path[index - 1].height + (path[index].height - path[index - 1].height) * share};
    }
    const float arm = keeperArmBinturong(player);
    if (!closest || closestGap > arm || closest->seconds <= 0.0f) return;
    const float up = std::sqrt(std::max(arm * arm - closestGap * closestGap, 0.0f));
    const float standingTop =
        MatchTuning::Goalkeeper::SHOULDER_HEIGHT_SHARE * player.heightMetres + up;
    const bool running = length(player.velocity) >= A::RUNNING_JUMP_MIN_SPEED;
    const float aerialReach = derivedPhysicalValue(DerivedStat::AERIAL_REACH,
                                                   player.derivedStat(DerivedStat::AERIAL_REACH));
    const float rise = jumpRiseOf(aerialReach,
                                  MatchTuning::KeeperHands::STANDING_REACH_HEIGHT_SHARE * player.heightMetres,
                                  running);
    if (rise <= 0.0f || closest->height <= standingTop || closest->height > standingTop + rise) return;
    const float needed = closest->height - standingTop;
    const float launch = std::sqrt(2.0f * G * rise);
    const float upSeconds = (launch - std::sqrt(std::max(launch * launch - 2.0f * G * needed, 0.0f))) / G;
    const float wait = closest->seconds - upSeconds;
    if (wait >= dt) return;
    const float into = dt - std::max(wait, 0.0f);
    player.feetHeight = std::max(launch * into - 0.5f * G * into * into, 0.0f);
    player.jumpSpeed = launch - G * into;
    player.runningJump = running;
    player.jumpLandingSeconds = launch / G;
    carry();
    return;
  }
  if (path.empty() || player.landingSeconds > 0.0f ||
      player.trapTimer > 0.0f || !active(player) || player.fallen ||
      (ball.kickerLockout > 0.0f && getLegacyContactActor() == player.player) ||
      (hasTouchClaim() && getPossessionAttachment() == player.player))
    return;
  // Where the ball passes closest to him on its way (he carries on as he is
  // going), between the points of its path, and when and how high it is
  // there.
  const Vector2F at = asVector(player.position);
  const auto relative = [&](const JumpPathPoint& point)
  {
    return Vector2F{point.at.x - (at.x + player.velocity.x * point.seconds),
                    point.at.y - (at.y + player.velocity.y * point.seconds)};
  };
  std::optional<JumpPathPoint> closest;
  float closestGap = std::numeric_limits<float>::max();
  for (std::size_t index = 1; index < path.size(); ++index)
  {
    const Vector2F from = relative(path[index - 1]);
    const Vector2F to = relative(path[index]);
    const Vector2F step{to.x - from.x, to.y - from.y};
    const float stepSquared = step.x * step.x + step.y * step.y;
    const float share =
        stepSquared > EPSILON
            ? std::clamp(-(from.x * step.x + from.y * step.y) / stepSquared, 0.0f, 1.0f)
            : 0.0f;
    const float gap = length({from.x + step.x * share, from.y + step.y * share});
    if (gap > closestGap) break;
    closestGap = gap;
    const JumpPathPoint& a = path[index - 1];
    const JumpPathPoint& b = path[index];
    closest = JumpPathPoint{a.seconds + (b.seconds - a.seconds) * share, {},
                            a.height + (b.height - a.height) * share};
  }
  if (!closest || closestGap > A::HEAD_REACH_METRES || closest->seconds <= 0.0f)
    return;
  // Only above his standing forehead; only as high as his jump reaches.
  const float standing = player.heightMetres;
  const float forehead = standing - A::HEAD_OFFSET_METRES;
  const bool running = length(player.velocity) >= A::RUNNING_JUMP_MIN_SPEED;
  const float rise = jumpRiseBinturong(player, running);
  if (closest->height <= forehead || closest->height > standing + rise) return;
  // His forehead a little above the ball's centre, or at the top of his
  // jump the top of his head.
  const float needed =
      std::min(closest->height - forehead + A::JUMP_FOREHEAD_ABOVE_METRES, rise);
  const float launch = std::sqrt(2.0f * G * rise);
  const float upSeconds =
      (launch - std::sqrt(std::max(launch * launch - 2.0f * G * needed, 0.0f))) / G;
  // He takes off during this step if he must before it ends (the ball's
  // sub-steps read him part-way through it: feetAtBinturong), and is that
  // far into his jump by its end.
  const float wait = closest->seconds - upSeconds;
  if (wait >= dt) return;
  const float into = dt - std::max(wait, 0.0f);
  player.feetHeight = std::max(launch * into - 0.5f * G * into * into, 0.0f);
  player.jumpSpeed = launch - G * into;
  player.runningJump = running;
  player.jumpLandingSeconds = launch / G;
  carry();
}

PitchPoint MatchEngine::knockDownPointBinturong(const MatchPlayer& /*header*/,
                                              PitchPoint aim)
{
  // Aimed where it was meant to go: the header's contact (contact plan
  // step 3c) is what sends it astray.
  return aim;
}

void MatchEngine::settleOnBallBinturong(MatchPlayer& player, float touchSkill,
                                        bool hands)
{
  // The touch that brings the ball under control takes a moment, longer
  // for a poor touch; a keeper with it in his hands holds it a while.
  using B = MatchTuning::Ball;
  player.isTrapping = true;
  player.trapTimer =
      B::BASE_TRAP_TIME + (1.0f - touchSkill) * B::TRAP_SKILL_PENALTY;
  player.actionCooldown =
      hands ? keeperHoldSecondsBinturong(player)
            : player.trapTimer +
                  randomFloat(B::MIN_POST_TOUCH_DELAY, B::MAX_POST_TOUCH_DELAY);
}

float MatchEngine::keeperHoldSecondsBinturong(const MatchPlayer& /*keeper*/)
{
  return randomFloat(MatchTuning::Ball::MIN_KEEPER_HOLD_SECONDS,
                     MatchTuning::Ball::MAX_KEEPER_HOLD_SECONDS);
}

float MatchEngine::injuryChanceBinturong(const MatchPlayer& player) const
{
  // Non-contact risk grows with tiredness and with running hard.
  const float speedRatio =
      length(player.velocity) / std::max(player.maxSpeed, EPSILON);
  const float tiredness = 1.0f - player.stamina;
  const float hazard =
      MatchTuning::Injury::BASE_HAZARD_PER_MINUTE *
      (1.0f +
       tiredness * tiredness * MatchTuning::Injury::FATIGUE_HAZARD_MULTIPLIER) *
      (1.0f + speedRatio * MatchTuning::Injury::INTENSITY_HAZARD_MULTIPLIER);
  return 1.0f - std::exp(-hazard * MatchTuning::Injury::CHECK_INTERVAL_MINUTES);
}

// ---------------------------------------------------------------------------
// Contest halves: what this player's body is worth in a duel with a player of
// the other side (whose own body gives the other half). Each is in the
// duel's own units, on the scale the Aardvark halves set (see
// match_engine_contests_plan.md, section 3). They started as exact copies
// of the Aardvark halves; a change here must keep to that scale (the mirror
// test, section 6).
// ---------------------------------------------------------------------------

float MatchEngine::takeOnMakeBinturong(const MatchPlayer& carrier,
                                       ScoreBreakdown* terms) const
{
  using R = MatchTuning::Dribble;
  const float skill = carrier.dribbling * R::TAKE_ON_SKILL;
  const float pace = carrier.pace * R::TAKE_ON_PACE;
  if (terms)
  {
    terms->add("his Dribbling", TermSource::ATTRIBUTE, skill);
    terms->add("his Pace", TermSource::ATTRIBUTE, pace);
  }
  return skill + pace;
}

float MatchEngine::takeOnResistBinturong(const MatchPlayer& defender,
                                         ScoreBreakdown* terms) const
{
  using R = MatchTuning::Dribble;
  const float skill = defender.defending * R::TAKE_ON_SKILL;
  const float pace = defender.pace * R::TAKE_ON_PACE;
  if (terms)
  {
    terms->add("defender's Defending", TermSource::ATTRIBUTE, skill);
    terms->add("defender's Pace", TermSource::ATTRIBUTE, pace);
  }
  return skill + pace;
}

float MatchEngine::tackleMakeBinturong(const MatchPlayer& tackler,
                                       ScoreBreakdown* terms) const
{
  using D = MatchTuning::Defending;
  if (terms)
  {
    terms->add("his Defending", TermSource::ATTRIBUTE,
               tackler.defending * D::DEFENDING_WIN_BONUS);
    terms->add("his Physicality", TermSource::ATTRIBUTE,
               tackler.physicality * D::PHYSICALITY_DUEL_WEIGHT);
  }
  return MatchRules::tackleMake(tackler.defending, tackler.physicality);
}

float MatchEngine::tackleResistBinturong(const MatchPlayer& carrier,
                                         bool shielding,
                                         ScoreBreakdown* terms) const
{
  using D = MatchTuning::Defending;
  if (terms)
  {
    terms->add("carrier's Dribbling", TermSource::ATTRIBUTE,
               carrier.dribbling * D::DRIBBLING_WIN_PENALTY);
    terms->add("carrier's Physicality", TermSource::ATTRIBUTE,
               carrier.physicality * D::PHYSICALITY_DUEL_WEIGHT);
    terms->add("carrier shielding (his Physicality)", TermSource::ATTRIBUTE,
               shielding ? carrier.physicality * D::SHIELD_PHYSICALITY_PENALTY
                         : 0.0f);
  }
  return MatchRules::tackleResist(carrier.dribbling, carrier.physicality,
                                  shielding);
}

float MatchEngine::tackleTechniqueBinturong(const MatchPlayer& tackler,
                                            ScoreBreakdown* terms) const
{
  const float technique = MatchRules::tackleTechnique(tackler.defending);
  if (terms)
    terms->add("tackling technique (Defending)", TermSource::ATTRIBUTE,
               technique);
  return technique;
}

float MatchEngine::aerialStrengthBinturong(const MatchPlayer& player,
                                           float ballHeightMetres) const
{
  return MatchRules::aerialDuelStrength(player.heightMetres,
                                        player.physicality, ballHeightMetres);
}

float MatchEngine::claimChanceBinturong(const MatchPlayer& keeper,
                                        int crowding) const
{
  using A = MatchTuning::Aerial;
  return std::clamp(A::BASE_CLAIM_CHANCE +
                        keeper.goalkeeping * A::SKILL_CLAIM_BONUS -
                        static_cast<float>(crowding) * A::CROWDING_PENALTY,
                    0.05f, 0.97f);
}

void MatchEngine::recoverFromTakeOnBinturong(MatchPlayer& defender)
{
  using R = MatchTuning::Dribble;
  defender.tackleCooldown =
      std::max(defender.tackleCooldown, R::BEATEN_SECONDS);
  defender.velocity.x *= R::BEATEN_VELOCITY_RETAINED;
  defender.velocity.y *= R::BEATEN_VELOCITY_RETAINED;
}

// --- Striking and the wall ---------------------------------------------------

void MatchEngine::takeDirectFreeKickBinturong(MatchPlayer& taker)
{
  // A shot like any other, struck from a dead ball: it goes where his
  // strike sends it, and meets whatever bodies are in its way (no wall dice,
  // no screening delay; a real wall comes later). The xG is a label for the
  // statistics only.
  takeShot(taker, MatchTuning::SetPiece::DIRECT_FREE_KICK_XG, false, true);
  noteDirectFreeKickShot();
  ++stats.directFreeKicks;
  if (getPossessionState().shotOnTarget) ++stats.directFreeKicksOnTarget;
}

bool MatchEngine::playsFirstTouchBinturong(const MatchPlayer& player,
                                           bool hands,
                                           float heightMetres) const
{
  // Outfielders only, below his head line: goalkeepers keep the old trap for
  // now (first-touch plan §1). His thigh only while he is nearly stopped
  // (the sweep gives a moving player no thigh contact; chest and thigh
  // control plan step 4).
  const BodyLines lines = bodyLinesBinturong(player);
  const bool thigh = heightMetres > lines.feet && heightMetres <= lines.chest;
  return !player.isGoalkeeper && !hands && heightMetres <= lines.head &&
         (!thigh || length(player.velocity) <= MatchTuning::Ball::THIGH_MAX_SPEED);
}

BodyLines MatchEngine::bodyLinesBinturong(const MatchPlayer& player) const
{
  // From his height (chest and thigh control plan step 1): his knee, the top
  // of a raised thigh, the top of his shoulders.
  return bodyLinesOfHeight(player.heightMetres);
}

float MatchEngine::feetReachBinturong(const MatchPlayer& player) const
{
  // The knob reaches only outfielders with the new first touch.
  return !player.isGoalkeeper
             ? touchKnobs[TouchKnob::FEET_REACH]
             : MatchTuning::Ball::OUTFIELD_CONTROL_RADIUS_METRES;
}

bool MatchEngine::ballBehindReachBinturong(const MatchPlayer& player,
                                           float gapX, float gapY,
                                           float gap, float turnSeconds) const
{
  // An outfielder cannot touch a ball behind him beyond the comfort gap
  // (the dead zone).
  if (player.isGoalkeeper) return false;
  const float angle = predictedFacingBinturong(player, turnSeconds);
  const Vector2F facing = normalized(
      {std::cos(angle),
       std::sin(angle)});
  const Vector2F toBall =
      gap > EPSILON ? Vector2F{-gapX / gap, -gapY / gap} : facing;
  return inTouchDeadZone(facing, toBall, gap,
                         touchKnobs[TouchKnob::COMFORT_GAP],
                         touchKnobs[TouchKnob::DEAD_ZONE_ANGLE]);
}

float MatchEngine::predictedFacingBinturong(const MatchPlayer& player,
                                            float turnSeconds) const
{
  float angle = player.facingAngle;
  if (turnSeconds > 0.0f)
  {
    // As his body turns (integrateMovementsBinturong): toward the facing
    // asked of him, else toward his run, at his turn rate.
    using P = MatchTuning::Player;
    const float speedSquared = player.velocity.x * player.velocity.x +
                               player.velocity.y * player.velocity.y;
    const bool running = speedSquared > P::MOVEMENT_FACING_THRESHOLD *
                                            P::MOVEMENT_FACING_THRESHOLD;
    const std::optional<float> passFacing = strikeFacingBinturong(player);
    if (passFacing || player.facingRequested || running)
    {
      const float target =
          passFacing                ? *passFacing
          : player.facingRequested ? player.facingRequest
                                   : fastAtan2(player.velocity.y,
                          player.velocity.x);
      float difference = target - angle;
      if (difference > std::numbers::pi_v<float>)
        difference -= TWO_PI;
      else if (difference < -std::numbers::pi_v<float>)
        difference += TWO_PI;
      const float turn =
          bodyTurnRate(player, freeShareBinturong(player, carryingBinturong(player))) *
          turnSeconds;
      angle += std::clamp(difference, -turn, turn);
    }
  }
  return angle;
}

PitchPoint MatchEngine::strikePointBinturong(const MatchPlayer& /*striker*/) const
{
  // He meets the ball anywhere in his reach and strikes it there.
  return ball.position;
}

MatchEngine::Kick MatchEngine::strikeAsBinturong(const MatchPlayer& striker,
                                                 StrikeKind kind,
                                                 PitchPoint target, float speed,
                                                 float lift)
{
  return strikeContactBinturong(striker, kind, target, speed, lift);
}

float MatchEngine::clearanceDistanceBinturong(const MatchPlayer& /*defender*/,
                                              PitchPoint origin,
                                              const ClearanceAim& aim) const
{
  // Lofted over the distance it really travels from where he strikes it.
  return distance(origin, aim.target);
}

MatchEngine::BallRelease MatchEngine::releaseFromBinturong(const MatchPlayer& kicker,
                                                 ReleaseKind kind) const
{
  // Not all of these leave the ball as it was: a clearance starts at a set
  // height rather than the height it is at.
  BallRelease from;
  switch (kind)
  {
    case ReleaseKind::PASS:
      // From his hands as he lets it go (goalkeeper plan step 5); else from
      // where the ball is, at its height.
      if (getPossessionState().heldRelease != HeldRelease::NONE)
      {
        const auto [at, metres] = heldReleaseBinturong(kicker, getPossessionState().heldRelease);
        from.position = at;
        from.heightMetres = metres;
        from.z = metres;
        break;
      }
      from.position = ball.position;
      from.z = ball.z;
      from.heightMetres = ball.z;
      break;
    case ReleaseKind::SHOT:
    case ReleaseKind::HEADED_SHOT:
      // From where the ball is, at its height.
      from.position = ball.position;
      from.heightMetres = ball.z;
      from.z = from.heightMetres;
      break;
    case ReleaseKind::CLEARANCE:
      // A loose ball where it lies, else where he meets it; off the ground.
      from.position = hasTouchClaim() ? ball.position : strikePointBinturong(kicker);
      break;
    case ReleaseKind::CLEAR_BEHIND:
    case ReleaseKind::HEADER:
      // Where he meets it, at its height.
      from.position = strikePointBinturong(kicker);
      from.z = ball.z;
      from.heightMetres = ball.z;
      break;
  }
  return from;
}

void MatchEngine::meetWithHeadBinturong(const MatchPlayer& header)
{
  ball.position = strikePointBinturong(header);
}

bool MatchEngine::followsRoutesBinturong(const MatchPlayer& /*player*/) const
{
  // Routes (waypoints on the way) are this body's.
  return true;
}

bool MatchEngine::carryingBinturong(const MatchPlayer& player) const
{
  return hasTouchClaim() && getPossessionAttachment() == player.player;
}

float MatchEngine::freeShareBinturong(const MatchPlayer& player, bool withBall) const
{
  // With the ball, the share of his Agility and Acceleration his Dribbling
  // cannot take away: his run band's top as a share of his top speed, as it
  // is of his pace (carryPaceBinturong). As fuel leaves both, it moves with
  // them. Without the ball, all of it.
  if (!withBall) return 1.0f;
  return std::clamp(speedBandBinturong(player, MovementMode::RUN).high /
                        std::max(currentTopSpeedBinturong(player), EPSILON),
                    0.0f, 1.0f);
}

std::optional<Vector2F> MatchEngine::dribbleOutAlongBinturong(
    const MatchPlayer& player, PitchPoint from, float& metres) const
{
  // His line from there, walked in short steps as he would run it: round
  // his line's turn now (a curve he holds, else the one at his waypoint),
  // then on round the next (toward his goal, as his body sees it).
  if (player.routeWaypoints <= 0 || isControlled(player) || metres <= EPSILON)
    return std::nullopt;
  constexpr float STEP_METRES = 0.25f;
  constexpr int MOST_STEPS = 64;
  const float speed = length(player.velocity);
  const Vector2F start = asVector(from);
  const std::vector<LineLeg> legs = lineLegsOf(player, asVector(ball.dribbleGoal));
  std::size_t leg = 0;
  Vector2F at = start;
  Vector2F waypoint = legs[0].waypoint;
  Vector2F next = legs[0].next;
  Turn turn = turnThrough(legs[0].from, waypoint, next, player.routeApexOffset,
                          player.routeOutwardRoom);
  const int steps =
      std::clamp(static_cast<int>(std::ceil(metres / STEP_METRES)), 1, MOST_STEPS);
  const float step = metres / static_cast<float>(steps);
  using R = MatchTuning::Dribble;
  const float cut = R::PUSH_LINE_CUT_POOR_METRES +
                    (R::PUSH_LINE_CUT_BEST_METRES - R::PUSH_LINE_CUT_POOR_METRES) *
                        std::clamp(player.dribbling, 0.0f, 1.0f);
  bool past = false;
  std::vector<Vector2F> traced;
  traced.reserve(static_cast<std::size_t>(steps));
  for (int index = 0; index < steps; ++index)
  {
    LineAim line = aimThrough(turn, waypoint, at, next, speed, step, !legs[leg].held);
    if (line.exited && !past)
    {
      // On to the next leg: round its turn if one follows, else straight to
      // its end.
      if (leg + 1 < legs.size())
      {
        ++leg;
        waypoint = legs[leg].waypoint;
        next = legs[leg].next;
        turn = turnThrough(legs[leg].from, waypoint, next, player.routeApexOffset,
                           player.routeOutwardRoom);
        line = aimThrough(turn, waypoint, at, next, speed, step);
      }
      else
        past = true;
    }
    if (past)
    {
      const Vector2F toEnd{next.x - at.x, next.y - at.y};
      if (length(toEnd) <= step) break;
      line.direction = unitOf(toEnd);
    }
    const Vector2F onward{at.x + line.direction.x * step,
                          at.y + line.direction.y * step};
    // No further than a straight roll stays on his line: round a tight
    // turn, a point further on would take the ball across its inside (the
    // wrong side of a cone); a better dribbler cuts more.
    const Vector2F chord = unitOf({onward.x - start.x, onward.y - start.y});
    bool cuts = false;
    for (const Vector2F& passed : traced)
      cuts = cuts || std::abs((passed.x - start.x) * chord.y -
                              (passed.y - start.y) * chord.x) >
                         cut;
    if (cuts) break;
    traced.push_back(onward);
    at = onward;
  }
  const Vector2F way{at.x - start.x, at.y - start.y};
  const float wayMetres = length(way);
  if (wayMetres <= EPSILON) return std::nullopt;
  return clearOfObstaclesBinturong(player, start,
                                   {way.x / wayMetres, way.y / wayMetres}, metres);
}

Vector2F MatchEngine::clearOfObstaclesBinturong(const MatchPlayer& player,
                                                Vector2F start, Vector2F direction,
                                                float& metres) const
{
  const auto cross = [](Vector2F a, Vector2F b) { return a.x * b.y - a.y * b.x; };
  // How near an obstacle an end of the leg he is on is to be the one his
  // line goes round it by.
  constexpr float ROUTE_BESIDE_METRES = 1.5f;
  const LineLeg now = player.routeWaypoints > 0
                          ? lineLegsOf(player, asVector(ball.dribbleGoal)).front()
                          : LineLeg{asVector(player.routeLegStart),
                                    asVector(player.movementTarget), {}, false};
  const std::array<Vector2F, 2> route{now.from, now.waypoint};
  for (const Obstacle& obstacle : obstacles)
  {
    const Vector2F at = asVector(obstacle.position);
    // The ball's centre clears it by its radius as well.
    const float clearance = obstacle.radiusMetres + MatchTuning::Units::BALL_RADIUS_METRES;
    const Vector2F to{at.x - start.x, at.y - start.y};
    const float distance = length(to);
    // Only one the push reaches (ahead of the ball, within its length) and
    // passes close by: how far to the side (+ on the push's left, - its
    // right).
    const float ahead = to.x * direction.x + to.y * direction.y;
    if (distance <= EPSILON || ahead <= 0.0f || ahead > metres + clearance) continue;
    const float passes = cross(direction, {start.x - at.x, start.y - at.y});
    if (std::abs(passes) >= ROUTE_BESIDE_METRES) continue;
    // His side of it: where an end of the leg he is on is beside it (his
    // line goes round it by that point), the side his leg's line passes it
    // (it stays that side of him as he turns round it); else the side the
    // ball is already passing it.
    float side = passes >= 0.0f ? 1.0f : -1.0f;
    const float nearestMetres =
        std::min(length({route[0].x - at.x, route[0].y - at.y}),
                 length({route[1].x - at.x, route[1].y - at.y}));
    const Vector2F leg{route[1].x - route[0].x, route[1].y - route[0].y};
    const float legMetres = length(leg);
    if (nearestMetres <= ROUTE_BESIDE_METRES && legMetres > EPSILON)
    {
      const Vector2F legWay{leg.x / legMetres, leg.y / legMetres};
      const float routeSide = cross(legWay, {route[0].x - at.x, route[0].y - at.y});
      if (std::abs(routeSide) > EPSILON) side = routeSide > 0.0f ? 1.0f : -1.0f;
    }
    // Clear of it on his side: left alone. Else (touching it, or the other
    // side of it from him) turned to just graze it on his side, square to
    // it when the ball is already that close.
    if (passes * side > 0.0f && std::abs(passes) >= clearance) continue;
    const float angle = side * std::asin(std::min(clearance / distance, 1.0f));
    const Vector2F toward{to.x / distance, to.y / distance};
    direction = {toward.x * std::cos(angle) - toward.y * std::sin(angle),
                 toward.x * std::sin(angle) + toward.y * std::cos(angle)};
    // Turned off his line, it goes only as far as takes it past (the next
    // touch brings it back onto his line).
    metres = std::min(metres, to.x * direction.x + to.y * direction.y + clearance);
  }
  return direction;
}

std::optional<Vector2F> MatchEngine::dribbleOutFromBinturong(
    const MatchPlayer& player, PitchPoint from) const
{
  // Along his route's line from there (past its turn, on toward the point
  // after), else toward where he means to carry it.
  const Vector2F at = asVector(from);
  Vector2F towards = asVector(ball.dribbleGoal);
  if (player.routeWaypoints > 0)
  {
    const LineLeg now = lineLegsOf(player, towards).front();
    const Vector2F waypoint = now.waypoint;
    const Vector2F next = now.next;
    const Turn turn = turnThrough(now.from, waypoint, next, player.routeApexOffset,
                                  player.routeOutwardRoom);
    const LineAim line = aimThrough(turn, waypoint, at, next, length(player.velocity),
                                    0.0f, !now.held);
    if (!line.exited &&
        line.direction.x * line.direction.x + line.direction.y * line.direction.y >
            0.5f)
      return line.direction;
    towards = next;
  }
  const Vector2F offset{towards.x - at.x, towards.y - at.y};
  const float metres = length(offset);
  if (metres <= MatchTuning::Player::ARRIVAL_DEAD_ZONE_METRES)
    return std::nullopt;
  return Vector2F{offset.x / metres, offset.y / metres};
}

void MatchEngine::planDribbleMeetBinturong(MatchPlayer& owner)
{
  // He runs onto it at the pace he wants: the earliest point of its roll
  // (read with the rolling physics the ball obeys) he can have as close as
  // he means to play it at that pace; failing that, at his top speed;
  // failing that, the last point read. Then his touch there: where it
  // sends the ball, and how far.
  using F = MatchTuning::FirstTouch;
  using R = MatchTuning::Dribble;
  constexpr int SUBSTEPS = MatchTuning::Timing::BALL_SUBSTEPS;
  DribbleAim aim = dribbleAim(owner);
  const float topSpeed = std::max(currentTopSpeed(owner), EPSILON);
  // Knocked on into space, he sprints after it: his full pace.
  if (ball.knockedOn) aim.speed = topSpeed;
  const float pace = std::clamp(aim.speed, R::LOOSE_START_SPEED, topSpeed);
  const float speed = length(ball.velocity);
  const Vector2F heading =
      speed > EPSILON ? Vector2F{ball.velocity.x / speed, ball.velocity.y / speed}
                      : Vector2F{0.0f, 0.0f};
  FlightState flight{speed, 0.0f, 0.0f, false};
  float travelled = 0.0f;
  PitchPoint meet = ball.position;
  float seconds = 0.0f;
  // How far short he would be at `runSpeed` of having the last point read
  // as close as he means to play it (a better dribbler closer): where that
  // crosses zero, between two points read, is the meet.
  const float meetGap = dribbleMeetGapBinturong(owner);
  const auto lateBy = [&](float runSpeed, float elapsed)
  {
    return distance(owner.position, meet) - meetGap -
           runSpeed * elapsed;
  };
  const auto between = [](PitchPoint a, PitchPoint b, float share)
  { return PitchPoint{a.x + (b.x - a.x) * share, a.y + (b.y - a.y) * share}; };
  float lateAtPace = lateBy(pace, 0.0f);
  float lateAtTop = lateBy(topSpeed, 0.0f);
  bool atPace = lateAtPace <= 0.0f;
  std::optional<std::pair<PitchPoint, float>> atTop;
  if (!atPace && lateAtTop <= 0.0f) atTop = std::pair{meet, 0.0f};
  for (float elapsed = F::CHASE_STEP_SECONDS;
       !atPace && elapsed <= F::CHASE_HORIZON_SECONDS + EPSILON;
       elapsed += F::CHASE_STEP_SECONDS)
  {
    for (int step = 0; step < SUBSTEPS && flight.horizontalSpeed > 0.0f; ++step)
      travelled +=
          stepFlight(flight, F::CHASE_STEP_SECONDS / static_cast<float>(SUBSTEPS));
    const PitchPoint before = meet;
    const Vector2F rolled =
        {heading.x * travelled, heading.y * travelled};
    meet = PitchPoint{ball.position.x + rolled.x, ball.position.y + rolled.y};
    const float earlier = elapsed - F::CHASE_STEP_SECONDS;
    seconds = elapsed;
    const float pacedLate = lateBy(pace, elapsed);
    const float topLate = lateBy(topSpeed, elapsed);
    if (pacedLate <= 0.0f)
    {
      const float share = lateAtPace / std::max(lateAtPace - pacedLate, EPSILON);
      meet = between(before, meet, share);
      seconds = earlier + F::CHASE_STEP_SECONDS * share;
      atPace = true;
    }
    else if (!atTop && topLate <= 0.0f)
    {
      const float share = lateAtTop / std::max(lateAtTop - topLate, EPSILON);
      atTop = std::pair{between(before, meet, share),
                        earlier + F::CHASE_STEP_SECONDS * share};
    }
    lateAtPace = pacedLate;
    lateAtTop = topLate;
  }
  if (!atPace && atTop) std::tie(meet, seconds) = *atTop;

  // His touch there: along his way from that point, as far as the pace he
  // wants puts it. With nowhere to go he stops on it.
  const std::optional<Vector2F> out = dribbleOutFromBinturong(owner, meet);
  const Vector2F approach =
      meet - owner.position;
  const float approachMetres = length(approach);
  ball.meetFor = owner.player;
  ball.meetPace = aim.speed;
  ball.meetPoint = meet;
  ball.meetSeconds = seconds;
  ball.meetOut = out ? *out
                 : approachMetres > EPSILON
                     ? Vector2F{approach.x / approachMetres,
                                approach.y / approachMetres}
                     : ball.dribbleDirection;
  const float toGoal = length(ball.dribbleGoal - meet);
  // On a route, no further than its next turn allows (as the touch).
  const DribbleTurnAhead turnAhead = dribbleTurnAheadBinturong(owner, meet);
  // A brain that pushes by intent (dribble meet plan 3c) does here too, as
  // the touch will.
  const bool intentPush = brainTraits(owner.isHomeTeam).pushesByIntent;
  const bool drilled =
      owner.player && std::ranges::find(ballKeepers, owner.player->getId()) !=
                          ballKeepers.end();
  const float toEnd =
      std::min(turnAhead.toTurn, drilled ? toGoal : std::numeric_limits<float>::max());
  std::optional<float> pushed;
  if (out && aim.speed >= R::LOOSE_START_SPEED && intentPush)
    if (const std::optional<float> seconds =
            chooseTouchSeconds(owner, aim.speed, toEnd))
      pushed = choosePushMetres(owner, meet, *out, 0.0f, aim.speed, *seconds,
                                toGoal);
  ball.meetPush =
      out && aim.speed >= R::LOOSE_START_SPEED
          ? std::min(pushed ? *pushed : dribblePushMetres(owner, aim.speed, toGoal),
                     std::max(turnAhead.furthest,
                              touchKnobs[TouchKnob::SET_OFFSET]))
          : 0.0f;
  // On a route, aimed where his line will be when it gets there (a bend
  // ahead: angled into it), as the touch will.
  if (out && ball.meetPush > EPSILON)
    if (const std::optional<Vector2F> along =
            dribbleOutAlongBinturong(owner, meet, ball.meetPush))
      ball.meetOut = *along;
  // A pass or shot decided for this meet bends his run toward its line.
  shapeMeetForStrikeBinturong(owner);
  // His body runs there (integrateMovementsBinturong takes the corner).
  owner.tacticalTarget = meet;
  owner.targetVelocity = {0.0f, 0.0f};
  owner.intent = PlayerIntent::RECEIVE_PASS;
  owner.urgentMovement = true;
}

namespace
{
/** His speed-change limits (m/s^2) at `speed` as his body has them
 * (stepKinematicsBinturong): a(v) = A0 (1 - v / top) and his braking on
 * tired legs, held to his critical speed's power as his burst reserve
 * runs out. */
AccelerationRange legLimitsOf(const MatchPlayer& player, float speed, float top,
                              float freeShare)
{
  const float fuel = player.stamina;
  const float legs = tieredMovement(fuel);
  const float fullness = std::clamp(
      player.sprintReserve / std::max(tieredBurstCapacity(fuel), EPSILON), 0.0f,
      1.0f);
  const float transition = MatchRules::walkRunTransitionSpeed(player.heightMetres);
  return burstLimits(
      playerAcceleration(player, freeShare) * legs *
          std::max(0.05f, 1.0f - speed / std::max(top, EPSILON)),
      playerBraking(player, freeShare) * legs, speed, fullness,
      tieredCriticalSpeed(
          derivedPhysicalValue(DerivedStat::ENDURANCE,
                               player.derivedStat(DerivedStat::ENDURANCE)),
          fuel, transition),
      transition);
}
}  // namespace

float MatchEngine::dribbleReachablePaceBinturong(const MatchPlayer& player,
                                                 float startSpeed, float target,
                                                 float seconds,
                                                 float* speedingUpShare) const
{
  // His push as the body gives it (legLimitsOf: tired legs, and an empty
  // burst reserve), held at the target once there; the mean over the time.
  constexpr int STEPS = 10;
  if (speedingUpShare) *speedingUpShare = 0.0f;
  if (seconds <= EPSILON || startSpeed >= target) return target;
  const float top = std::max(currentTopSpeedBinturong(player), EPSILON);
  const float dt = seconds / static_cast<float>(STEPS);
  float speed = startSpeed;
  float covered = 0.0f;
  float rising = 0.0f;
  const float freeShare = freeShareBinturong(player, true);
  for (int step = 0; step < STEPS; ++step)
  {
    const float gain =
        std::max(legLimitsOf(player, speed, top, freeShare).high, 0.0f) * dt;
    const float next = std::min(target, speed + gain);
    // Up to the target part way through the step: only that part of it.
    if (speed < target)
      rising += gain > EPSILON ? dt * std::min((target - speed) / gain, 1.0f) : dt;
    covered += 0.5f * (speed + next) * dt;
    speed = next;
  }
  if (speedingUpShare) *speedingUpShare = std::min(rising / seconds, 1.0f);
  return covered / seconds;
}

MatchEngine::DribbleTurnAhead MatchEngine::dribbleTurnAheadBinturong(
    const MatchPlayer& player, PitchPoint from) const
{
  // The route's next turn from that point: the one at his waypoint, or once
  // past it, the one after. The ball can't brake: it must roll into the
  // turn no faster than he can take it (his lateral limit, as his body
  // has it), and as it rolls straight while his line curves, a push goes
  // no further into the turn than where the line has curved off it by the
  // allowed deviation.
  DribbleTurnAhead ahead;
  if (player.routeWaypoints == 0) return ahead;
  const Vector2F at = asVector(from);
  const std::vector<LineLeg> legs = lineLegsOf(player, asVector(ball.dribbleGoal));
  Turn turn = turnThrough(legs[0].from, legs[0].waypoint, legs[0].next,
                          player.routeApexOffset, player.routeOutwardRoom);
  LineAim line = aimThrough(turn, legs[0].waypoint, at, legs[0].next,
                            length(player.velocity), 0.0f, !legs[0].held);
  if (line.exited)
  {
    if (legs.size() < 2) return ahead;
    turn = turnThrough(legs[1].from, legs[1].waypoint, legs[1].next,
                       player.routeApexOffset, player.routeOutwardRoom);
    line = aimThrough(turn, legs[1].waypoint, at, legs[1].next,
                      length(player.velocity), 0.0f);
    if (line.exited) return ahead;
  }
  if (turn.straight) return ahead;
  const float radius = std::max(turn.radius, 0.0f);
  ahead.toTurn = std::max(line.toTurn, 0.0f);
  ahead.turnSpeed = cornerSpeedOf(player, radius, freeShareBinturong(player, true));
  // A stop (a sharp reversal): it gets there with pace left for his touch
  // to stop it, the more the better his Dribbling.
  if (radius <= 0.0f)
  {
    using R = MatchTuning::Dribble;
    ahead.stop = true;
    ahead.turnSpeed = R::STOP_MEET_PACE_POOR +
                      (R::STOP_MEET_PACE_BEST - R::STOP_MEET_PACE_POOR) *
                          std::clamp(player.dribbling, 0.0f, 1.0f);
  }
  ahead.furthest =
      ahead.toTurn +
      std::sqrt(2.0f * radius * MatchTuning::Dribble::PUSH_LINE_DEVIATION_METRES);
  // As his body brakes for a turn ahead (integrateMovementsBinturong), at
  // full effort: on tired legs, and less once his burst reserve is empty.
  const float speedNow = length(player.velocity);
  const float canBrake = std::max(
      -legLimitsOf(player, speedNow, currentTopSpeedBinturong(player),
                  freeShareBinturong(player, true)).low, EPSILON);
  ahead.braking = std::min(
      std::max(MatchTuning::Player::ARRIVAL_DECELERATION,
               ARRIVAL_BRAKING_SHARE * canBrake),
      canBrake);
  return ahead;
}

float MatchEngine::dribbleMeetGapBinturong(const MatchPlayer& player) const
{
  // A better dribbler keeps it close; a poor one plays it at a stretch.
  return dribbleMeetGapOf(touchKnobs[TouchKnob::COMFORT_GAP], player.dribbling);
}

void MatchEngine::shapeMeetForStrikeBinturong(const MatchPlayer& owner)
{
  // A pass or shot decided for his meet (dribble meet plan 2b): his run in
  // bends toward its line, from straight at the meet (as fast as he can get
  // there, and the strike from that angle: harder) to his run along it at
  // the meet (his body opened into it: straight on). How far he bends it is
  // the race to the meet: the more time to spare before an opponent could
  // get there, the more he opens up.
  using R = MatchTuning::Dribble;
  if (!pendingStrike || !pendingStrike->atMeet || pendingStrike->striker != &owner)
    return;
  const std::optional<PitchPoint> aimAt = heldStrikeTargetBinturong(owner);
  if (!aimAt) return;
  const PitchPoint target = *aimAt;
  const Vector2F meet = asVector(ball.meetPoint);
  const Vector2F approach = unitOf(
      {meet.x - asVector(owner.position).x, meet.y - asVector(owner.position).y});
  const Vector2F action =
      unitOf({asVector(target).x - meet.x, asVector(target).y - meet.y});
  if (approach.x == 0.0f && approach.y == 0.0f) return;
  if (action.x == 0.0f && action.y == 0.0f) return;
  // When the first opponent could get to the meet, against when he will.
  const float theirs = opponentArrivalSecondsBinturong(owner, ball.meetPoint);
  const float share = std::clamp(
      (theirs - ball.meetSeconds) / R::MEET_SHAPE_MARGIN_SECONDS, 0.0f, 1.0f);
  const float angle = std::atan2(approach.x * action.y - approach.y * action.x,
                                 approach.x * action.x + approach.y * action.y);
  ball.meetOut = rotatedBy(approach, share * angle);
  // He runs on along it after the strike (the corner's way out).
  ball.meetPush = std::max(ball.meetPush, R::MEET_STRIKE_OUT_METRES);
}

float MatchEngine::opponentArrivalSecondsBinturong(const MatchPlayer& owner,
                                                  PitchPoint point) const
{
  const Vector2F spot = asVector(point);
  float soonest = std::numeric_limits<float>::max();
  for (const MatchPlayer& opponent : players)
  {
    if (!active(opponent) || opponent.isHomeTeam == owner.isHomeTeam) continue;
    const Vector2F at = asVector(opponent.position);
    const float reach = opponent.isGoalkeeper
                            ? MatchTuning::Ball::GOALKEEPER_CONTROL_RADIUS_METRES
                            : feetReach(opponent);
    soonest = std::min(
        soonest, std::max(0.0f, length({spot.x - at.x, spot.y - at.y}) - reach) /
                         std::max(currentTopSpeed(opponent), EPSILON) +
                     MatchTuning::Dribble::MEET_SHAPE_READ_SECONDS);
  }
  return soonest;
}

std::optional<PitchPoint> MatchEngine::heldStrikeTargetBinturong(
    const MatchPlayer& player) const
{
  // A pass: where it is aimed. A shot: the spot he picked on the goal line
  // (or, without one, the middle of the goal).
  if (!pendingStrike || pendingStrike->striker != &player) return std::nullopt;
  switch (pendingStrike->kind)
  {
    case PendingStrike::Kind::PASS:
      return pendingStrike->pass.targetPoint;
    case PendingStrike::Kind::SHOT:
      return PitchPoint{pitch.atX(player.isHomeTeam ? 1.0f : 0.0f),
                        pitch.atY(MatchTuning::Pitch::CENTRE) +
                            (pendingStrike->shotAim
                                                  ? pendingStrike->shotAim->aimY
                                                  : 0.0f)};
    case PendingStrike::Kind::CLEAR:
      break;
  }
  return std::nullopt;
}

std::optional<float> MatchEngine::turnToPassFacingBinturong(
    const MatchPlayer& player) const
{
  // Along the pass's line from the ball (a first-time ball: from where he
  // will meet it); but with the ball beyond his comfort gap he turns only so
  // far that it stays in front of his dead zone's edge (to send it further
  // round he would have to step round it).
  if (!pendingStrike || pendingStrike->striker != &player) return std::nullopt;
  const std::optional<PitchPoint> aimAt = heldStrikeTargetBinturong(player);
  if (!aimAt) return std::nullopt;
  const Vector2F ballAt = asVector(pendingStrike->firstTime
                                       ? pendingStrike->meetPoint
                                       : ball.position);
  const Vector2F target = asVector(*aimAt);
  Vector2F line = unitOf({target.x - ballAt.x, target.y - ballAt.y});
  if (line.x == 0.0f && line.y == 0.0f) return std::nullopt;
  const Vector2F at = asVector(player.position);
  const Vector2F ballNow = asVector(ball.position);
  const Vector2F gap{ballNow.x - at.x, ballNow.y - at.y};
  if (length(gap) > touchKnobs[TouchKnob::COMFORT_GAP])
  {
    const Vector2F toBall = unitOf(gap);
    const float limit =
        (touchKnobs[TouchKnob::DEAD_ZONE_ANGLE] -
         MatchTuning::Dribble::TURN_TO_PASS_DEAD_ZONE_MARGIN_DEGREES) *
        std::numbers::pi_v<float> / 180.0f;
    const float off = std::atan2(toBall.x * line.y - toBall.y * line.x,
                                 toBall.x * line.x + toBall.y * line.y);
    if (std::abs(off) > limit)
      line = rotatedBy(toBall, off > 0.0f ? limit : -limit);
  }
  return std::atan2(line.y,
                    line.x);
}

void MatchEngine::planHeaderTurnBinturong()
{
  // Turning to head (heading technique plan step 7): a player who knows the
  // header he means before the ball comes down to him turns his body to it,
  // as far as his time allows, so he strikes it nearer straight ahead. He
  // knows it when a drill forces it, or when a team-mate's high ball is
  // played to him and his brain's choice is a headed shot (towards the
  // goal) or a cushion (facing the ball, to send it straight back). A
  // knock-down's or a clearance's aim is chosen only as he meets it.
  headerTurn.reset();
  if (state != MatchState::PLAYING || getPossessionAttachment() || getPossessionState().isShot) return;
  if (ball.z <= lowestHeadLine() &&
      ball.velocityZ <= 0.0f)
    return;
  const MatchPlayer* header = nullptr;
  std::optional<PitchPoint> aimAt;
  bool faceBall = false;
  if (forcedHeader)
  {
    if (!forcedHeader->knownEarly) return;
    for (const MatchPlayer& player : players)
      if (player.player && player.player->getId() == forcedHeader->player)
        header = &player;
    if (!header) return;
    switch (forcedHeader->aim)
    {
      case ForcedHeaderAim::SPOT:
        aimAt = forcedHeader->spot;
        break;
      case ForcedHeaderAim::GOAL:
        aimAt = PitchPoint{pitch.atX(header->isHomeTeam ? 1.0f : 0.0f),
                           pitch.atY(MatchTuning::Pitch::CENTRE) +
                               forcedHeader->goalAim.x};
        break;
      case ForcedHeaderAim::KEEP:
        faceBall = true;
        break;
    }
  }
  else if (getPossessionState().isPass && getExpectedReceiver())
  {
    MatchPlayer* receiver = findMatchPlayer(getExpectedReceiver());
    if (possessionId == PossessionId::BINTURONG)
    {
      receiver = nullptr;
      for (MatchPlayer& candidate : players)
        if (active(candidate) && !candidate.isGoalkeeper &&
            candidate.isHomeTeam == getPossessionState().passByHome &&
            (candidate.intent == PlayerIntent::RECEIVE_PASS ||
             candidate.intent == PlayerIntent::CLAIM_LOOSE_BALL))
        {
          receiver = &candidate;
          break;
        }
    }
    if (!receiver || receiver->isHomeTeam != getPossessionState().passByHome ||
        getBody(receiver->isHomeTeam) != BodyId::BINTURONG)
      return;
    header = receiver;
    switch (chooseHeaderAttempt(*receiver, true))
    {
      case HeaderAttempt::SHOOT:
        aimAt = pitch.at(receiver->isHomeTeam ? 1.0f : 0.0f, MatchTuning::Pitch::CENTRE);
        break;
      case HeaderAttempt::CUSHION:
        faceBall = true;
        break;
      case HeaderAttempt::PLAY:
        return;
    }
  }
  if (!header || !active(*header)) return;
  Vector2F way{};
  if (faceBall)
  {
    const float level = length(ball.velocity);
    if (level <= MatchTuning::Ball::STOP_SPEED) return;
    way = {-ball.velocity.x / level, -ball.velocity.y / level};
  }
  else
  {
    way = unitOf(Vector2F{aimAt->x - header->position.x, aimAt->y - header->position.y});
    if (way.x == 0.0f && way.y == 0.0f) return;
  }
  headerTurn = HeaderTurn{header, std::atan2(way.y,
                                             way.x)};
}

std::optional<float> MatchEngine::strikeFacingBinturong(const MatchPlayer& player) const
{
  if (const std::optional<float> pass = turnToPassFacingBinturong(player)) return pass;
  if (headerTurn && headerTurn->player == &player) return headerTurn->facing;
  return std::nullopt;
}

bool MatchEngine::turnedToPassBinturong(const MatchPlayer& player) const
{
  if (!pendingStrike || pendingStrike->heldSeconds >= pendingStrike->turnSeconds)
    return true;
  const std::optional<float> wanted = turnToPassFacingBinturong(player);
  if (!wanted) return true;
  const auto metric = [this](float angle)
  {
    return normalized({std::cos(angle),
                       std::sin(angle)});
  };
  const Vector2F body = metric(player.facingAngle);
  const Vector2F want = metric(*wanted);
  return body.x * want.x + body.y * want.y >=
         std::cos(MatchTuning::Dribble::TURN_TO_PASS_READY_DEGREES *
                  std::numbers::pi_v<float> / 180.0f);
}

float MatchEngine::dribbleTouchIntervalBinturong(const MatchPlayer& player) const
{
  // A better dribbler can play it again sooner: his feet are quicker to
  // the ball. In whole steps, as the cooldown counts down.
  using R = MatchTuning::Dribble;
  const float seconds =
      R::TOUCH_INTERVAL_POOR_SECONDS +
      (R::TOUCH_INTERVAL_BEST_SECONDS - R::TOUCH_INTERVAL_POOR_SECONDS) *
          std::clamp(player.dribbling, 0.0f, 1.0f);
  const float step = MatchTuning::Timing::FIXED_STEP_SECONDS;
  return std::max(step, std::round(seconds / step) * step);
}

bool MatchEngine::dribbleTouchNeededEarlyBinturong(const MatchPlayer& player,
                                                   const TouchMoment& moment,
                                                   float pace) const
{
  // He lets it run until he meant to play it again, unless it needs playing
  // now: judged against the way he pushed it, not where he now wants to go.
  using R = MatchTuning::Dribble;
  const float ballSpeed = length(ball.velocity);
  // Stopped, and he wants to go on.
  if (ballSpeed <= MatchTuning::Ball::STOP_SPEED)
    return pace >= R::LOOSE_START_SPEED;
  // Running well off the way he pushed it (a deflection, a bounce).
  const float along = (ball.velocity.x * ball.pushDirection.x +
                       ball.velocity.y * ball.pushDirection.y) /
                      ballSpeed;
  if (along < std::cos(R::LOOSE_REDIRECT_RADIANS)) return true;
  // Caught: level with him (holding back for it would slow him to its pace).
  const Vector2F toBall = moment.contactPoint - player.position;
  if ((toBall.x * ball.velocity.x + toBall.y * ball.velocity.y) / ballSpeed <=
      R::EARLY_TOUCH_CAUGHT_METRES)
    return true;
  // An opponent close enough to it to take it.
  const Vector2F at = asVector(moment.contactPoint);
  for (const MatchPlayer& opponent : players)
  {
    if (!active(opponent) || opponent.isHomeTeam == player.isHomeTeam) continue;
    const Vector2F them = asVector(opponent.position);
    if (length({them.x - at.x, them.y - at.y}) <= R::EARLY_TOUCH_THREAT_METRES)
      return true;
  }
  return false;
}

PitchPoint MatchEngine::tackleNudgeSpotBinturong(const MatchPlayer& tackler,
                                               const MatchPlayer& attacker,
                                               PitchPoint contact) const
{
  // Tackling plan step 3. Coming from the side or behind (the attacker
  // nearer the goal he defends), he nicks it out square to the attacker's
  // line, on his own side. Goal-side and face on, forward (away from his
  // goal) and to the side, past the attacker: the side he is on of the
  // attacker's line, or dead centre his weaker foot's (his stronger foot
  // makes the tackle). It stops closer to him the better his Tackling
  // (its recipe is Defending until the game carries it).
  using D = MatchTuning::Defending;
  const PitchPoint goal =
      pitch.at(tackler.isHomeTeam ? 0.0f : 1.0f, MatchTuning::Pitch::CENTRE);
  const bool goalSide = distance(tackler.position, goal) < distance(attacker.position, goal);
  const Vector2F away = metricDirection(goal, contact);
  // The attacker's line: his run, or (barely moving) from him to the ball.
  Vector2F line = length(attacker.velocity) > MatchTuning::Ball::STOP_SPEED
                      ? normalized(attacker.velocity)
                      : metricDirection(attacker.position, contact);
  if (length(line) <= EPSILON) line = away;
  // Which side of that line he is on, in metres.
  const Vector2F offset = tackler.position - contact;
  const float along = offset.x * line.x + offset.y * line.y;
  const Vector2F lateral{offset.x - line.x * along, offset.y - line.y * along};
  const float lateralMetres = length(lateral);
  const auto weakerSide = [&](Vector2F facing)
  {
    // Left of `facing` is it turned a quarter anticlockwise.
    const bool rightFooted = !tackler.player || tackler.player->getFoot() == Foot::Right;
    const Vector2F left{-facing.y, facing.x};
    return rightFooted ? left : Vector2F{-left.x, -left.y};
  };
  Vector2F aim;
  if (!goalSide)
  {
    aim = lateralMetres > EPSILON
              ? Vector2F{lateral.x / lateralMetres, lateral.y / lateralMetres}
              : weakerSide(line);
  }
  else
  {
    const Vector2F side =
        lateralMetres > D::NUDGE_CENTRE_METRES
            ? Vector2F{lateral.x / lateralMetres, lateral.y / lateralMetres}
            : weakerSide(away);
    const float round = D::FACE_ON_NUDGE_DEGREES * std::numbers::pi_v<float> / 180.0f;
    aim = normalized({away.x * std::cos(round) + side.x * std::sin(round),
                      away.y * std::cos(round) + side.y * std::sin(round)});
  }
  const float tackling =
      std::clamp((tackler.derivedStat(DerivedStat::TACKLING) - STAT_1_RATING) /
                     (STAT_99_RATING - STAT_1_RATING),
                 0.0f, 1.0f);
  const float metres =
      D::NUDGE_METRES_POOR + (D::NUDGE_METRES_BEST - D::NUDGE_METRES_POOR) * tackling;
  const Vector2F reach = {aim.x * metres, aim.y * metres};
  return PitchPoint{std::clamp(contact.x + reach.x, pitch.atX(0.001f), pitch.atX(0.999f)),
          std::clamp(contact.y + reach.y, pitch.atY(0.001f), pitch.atY(0.999f))};
}
