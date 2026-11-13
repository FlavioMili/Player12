// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Collisions (Notes and plans/match_engine_collision_plan.md): two players
// running into each other. Each moves in a straight line through a movement
// step; a pair's first contact in the step is found on those lines, both are
// wound back to it, the hit is applied (momentum, no bounce along the line
// between their centres), and both are wound forward through the rest of the
// step at their new velocities. Contact changes velocity, never position.
// Pairs with an Aardvark body in them keep today's separation
// (separatePlayers).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>

#include "model/match_derived_stats.h"
#include "model/match_engine.h"
#include "model/match_engine_internal.h"

using namespace MatchEngineInternal;

namespace
{
/** A relative speed along the line between them below this (m/s) is not
 * closing: a pair just hit leaves with none. */
constexpr float CLOSING_TOLERANCE = 1e-3f;

float dot(Vector2F first, Vector2F second)
{
  return first.x * second.x + first.y * second.y;
}

Vector2F add(Vector2F first, Vector2F second)
{
  return {first.x + second.x, first.y + second.y};
}

Vector2F scaled(Vector2F vector, float factor)
{
  return {vector.x * factor, vector.y * factor};
}

/** When in [from, until] two bodies moving in straight lines first touch
 * (centres `reach` apart while closing): `gap` is the second's centre less
 * the first's at time 0, `closing` the second's velocity less the first's.
 * Touching and closing at `from`: then. */
std::optional<float> contactTime(Vector2F gap, Vector2F closing, float from,
                                 float until, float reach)
{
  const Vector2F atFrom = add(gap, scaled(closing, from));
  const float apart = length(atFrom);
  if (apart <= reach)
  {
    if (apart <= EPSILON) return from;
    if (dot(atFrom, closing) / apart < -CLOSING_TOLERANCE) return from;
    return std::nullopt;
  }
  const float a = dot(closing, closing);
  if (a <= EPSILON) return std::nullopt;
  const float b = 2.0f * dot(gap, closing);
  const float c = dot(gap, gap) - reach * reach;
  const float discriminant = b * b - 4.0f * a * c;
  if (discriminant < 0.0f) return std::nullopt;
  const float time = (-b - std::sqrt(discriminant)) / (2.0f * a);
  if (time < from || time > until) return std::nullopt;
  return time;
}
}  // namespace

void MatchEngine::judgeKnock(MatchPlayer& player, CollisionSide& side,
                             float remaining)
{
  using C = MatchTuning::Collision;
  // Where his Balance sits from 1 (0) to 99 (1).
  const float balance = std::clamp(
      (player.derivedStat(DerivedStat::BALANCE) - STAT_1_RATING) /
          (STAT_99_RATING - STAT_1_RATING),
      0.0f, 1.0f);
  const auto line = [balance](float at1, float at99)
  { return at1 + (at99 - at1) * balance; };
  // Already off balance, he goes over more easily.
  const float drop = player.stumbleSeconds > 0.0f ? C::STUMBLING_LINE_DROP : 0.0f;
  side.stumbleLine =
      line(C::STUMBLE_LINE_AT_BALANCE_1, C::STUMBLE_LINE_AT_BALANCE_99) - drop;
  side.fallLine = line(C::FALL_LINE_AT_BALANCE_1, C::FALL_LINE_AT_BALANCE_99) - drop;
  // The worse measure, wobbled on a bell curve (Box-Muller): unpredictable
  // for a poor Balance, nearly always as expected for a good one.
  const float uniform = std::max(randomFloat(0.0f, 1.0f), 1e-7f);
  const float angle = randomFloat(0.0f, 1.0f);
  const float bell = std::sqrt(-2.0f * std::log(uniform)) *
                     std::cos(2.0f * std::numbers::pi_v<float> * angle);
  side.score = std::max(side.endsUp, side.sudden) +
               bell * line(C::WOBBLE_AT_BALANCE_1, C::WOBBLE_AT_BALANCE_99);
  if (side.score < side.stumbleLine)
  {
    // Kept his feet and balance: a pause by the jolt, from the moment of
    // contact (the rest of this step he already carries on).
    side.outcome = 0;
    side.pause = C::PAUSE_SECONDS_PER_JOLT * length(side.jolt);
    player.collisionPause =
        std::max(player.collisionPause, side.pause - remaining);
    return;
  }
  side.outcome = side.score >= side.fallLine ? 2 : 1;
  if (side.endsUp >= side.sudden) ++stats.collisionKnocksByEndsUp;
  if (length(side.jolt) < 1.0f) ++stats.collisionKnocksSmallJolt;
  if (side.outcome == 1)
  {
    // A stumble can slip into a fall: his footing went.
    const float slip =
        balance < 0.5f
            ? C::SLIP_AT_BALANCE_1 +
                  (C::SLIP_AT_BALANCE_50 - C::SLIP_AT_BALANCE_1) * balance / 0.5f
            : C::SLIP_AT_BALANCE_50 + (C::SLIP_AT_BALANCE_99 - C::SLIP_AT_BALANCE_50) *
                                          (balance - 0.5f) / 0.5f;
    if (randomFloat(0.0f, 1.0f) < slip)
    {
      side.outcome = 2;
      side.slipped = true;
      ++stats.collisionSlips;
    }
  }
  if (side.outcome == 2)
  {
    knockOver(player, side);
    return;
  }
  // A stumble: longer the nearer he came to falling.
  const float share = std::clamp((side.score - side.stumbleLine) /
                                     std::max(side.fallLine - side.stumbleLine, EPSILON),
                                 0.0f, 1.0f);
  side.stumble = C::STUMBLE_SHORTEST_SECONDS +
                 (C::STUMBLE_LONGEST_SECONDS - C::STUMBLE_SHORTEST_SECONDS) * share;
  player.stumbleSeconds = std::max(player.stumbleSeconds, side.stumble - remaining);
  ++stats.collisionStumbles;
}

void MatchEngine::knockOver(MatchPlayer& player, CollisionSide& side)
{
  using C = MatchTuning::Collision;
  constexpr float PI = std::numbers::pi_v<float>;
  ++stats.collisionFalls;
  const float jolt = length(side.jolt);
  // A big hit, by chance rising with the jolt: he stays down a while.
  const float bigShare =
      std::clamp((jolt - C::BIG_HIT_JOLT_FROM) /
                     (C::BIG_HIT_JOLT_CERTAIN - C::BIG_HIT_JOLT_FROM),
                 0.0f, 1.0f);
  side.bigHit = bigShare > 0.0f && randomFloat(0.0f, 1.0f) < bigShare;
  if (side.bigHit)
  {
    side.downAtLeast =
        C::BIG_HIT_DOWN_AT_FROM + (C::BIG_HIT_DOWN_AT_CERTAIN - C::BIG_HIT_DOWN_AT_FROM) *
                                      bigShare;
    ++stats.collisionBigHits;
  }
  // Which way he lies: knocked (the jolt) mostly forward or back against
  // his facing, along his slide; mostly sideways, across it (and he rolls
  // along it). Stopped dead, the knock stands for his slide.
  constexpr float STILL = 0.1f;
  const float speed = length(side.after);
  const Vector2F slide = speed > STILL ? side.after : side.jolt;
  const float slideLength = length(slide);
  const float slideAngle =
      slideLength > EPSILON ? std::atan2(slide.y / pitch.width,
                                         slide.x / pitch.length)
                            : player.facingAngle;
  const Vector2F facing{std::cos(player.facingAngle) * pitch.length,
                        std::sin(player.facingAngle) * pitch.width};
  const float facingLength = std::max(length(facing), EPSILON);
  const float alignment =
      jolt > EPSILON ? std::abs(dot(side.jolt, facing)) / (jolt * facingLength) : 1.0f;
  side.lyingAcross =
      alignment < std::cos(C::LIE_ALONG_WITHIN_DEGREES * PI / 180.0f);
  // Across his slide: a quarter turn from it, in metres.
  float lying = slideAngle;
  if (side.lyingAcross)
  {
    const Vector2F along{std::cos(slideAngle) * pitch.length,
                         std::sin(slideAngle) * pitch.width};
    lying = std::atan2(along.x / pitch.width,
                       -along.y / pitch.length);
  }

  player.fallen = true;
  player.lyingAngle = lying;
  player.lyingAcross = side.lyingAcross;
  player.fellOnFront = dot(side.jolt, facing) >= 0.0f;
  player.rolledMetres = 0.0f;
  player.downSeconds = 0.0f;
  player.bigHitSeconds = side.downAtLeast;
  player.fallJolt = jolt;
  player.rollSeconds = 0.0f;
  player.rollDirection = 0.0f;
  player.rollPauseSeconds =
      randomFloat(C::ROLL_PAUSE_SHORTEST_SECONDS, C::ROLL_PAUSE_LONGEST_SECONDS);
  player.gettingUp = false;
  player.getUpSeconds = 0.0f;
  player.slideAngle = slideAngle;
  player.stumbleSeconds = 0.0f;
  player.collisionPause = 0.0f;
  player.isPressing = false;
  player.isMakingRun = false;

  loseBallOnFall(player);
}

void MatchEngine::standUp(MatchPlayer& player)
{
  player.fallen = false;
  player.gettingUp = false;
  player.getUpSeconds = 0.0f;
  player.rollSeconds = 0.0f;
  player.bigHitSeconds = 0.0f;
  player.downSeconds = 0.0f;
  player.velocity = {0.0f, 0.0f};
  player.facingAngle = player.slideAngle;
  player.targetAngle = player.slideAngle;
}

bool MatchEngine::collidesByMomentum(const MatchPlayer& first,
                                     const MatchPlayer& second) const
{
  return bodyOf(first.isHomeTeam) == BodyId::BINTURONG &&
         bodyOf(second.isHomeTeam) == BodyId::BINTURONG;
}

void MatchEngine::rememberStepStart()
{
  const std::size_t count = std::min(players.size(), stepStartPositions.size());
  for (std::size_t index = 0; index < count; ++index)
    stepStartPositions[index] = players[index].position;
  touchlineStepStart.resize(touchlineBodies.size());
  for (std::size_t index = 0; index < touchlineBodies.size(); ++index)
    touchlineStepStart[index] = touchlineBodies[index].position;
}

void MatchEngine::collidePlayers(float dt)
{
  using C = MatchTuning::Collision;
  lastCollisions.clear();
  const bool anyMomentum = bodyOf(true) == BodyId::BINTURONG ||
                           bodyOf(false) == BodyId::BINTURONG;
  if (anyMomentum && dt > 0.0f)
  {
    // Each body's straight line through the step, in metres: where it would
    // be at time 0 (its anchor) and its velocity. A hit replaces both, so
    // the line still gives where he is at any time in the step.
    struct Path
    {
      Vector2F anchor{};
      Vector2F velocity{};
      /** 1 / his weight (kg); 0 for an immovable obstacle (a player parked
       * off the pitch). */
      float inverseMass = 0.0f;
      bool takesPart = false;
      bool hit = false;
    };
    const std::size_t playerCount = std::min(players.size(), stepStartPositions.size());
    // Then the touchline's bodies (touchline plan stage 2) a player could
    // reach this step: their slots follow the players'.
    constexpr std::size_t MOST = 64;
    std::array<MatchPlayer*, MOST> bodies{};
    std::array<std::size_t, MOST> touchlineIndex{};
    for (std::size_t index = 0; index < playerCount; ++index) bodies[index] = &players[index];
    std::size_t count = playerCount;
    for (std::size_t index = 0; index < touchlineBodies.size() && count < MOST; ++index)
    {
      const MatchPlayer& body = touchlineBodies[index];
      // The officials collide with no one (user): they keep out of the way.
      if (body.fallen || index >= touchlineStepStart.size() || deepInTunnel(body.position) ||
          isOfficial(touchlinePeople[index].role))
        continue;
      bool near = false;
      for (std::size_t slot = 0; slot < playerCount && !near; ++slot)
      {
        const MatchPlayer& player = players[slot];
        near = active(player) && bodyOf(player.isHomeTeam) == BodyId::BINTURONG &&
               !deepInTunnel(player.position) &&
               length(player.position - body.position) <=
                   MatchTuning::TouchlinePeople::COLLISION_RANGE_METRES;
      }
      if (!near) continue;
      bodies[count] = &touchlineBodies[index];
      touchlineIndex[count] = index;
      ++count;
    }
    const auto onLine = [playerCount](std::size_t index) { return index >= playerCount; };
    std::array<Path, MOST> paths{};
    for (std::size_t index = 0; index < count; ++index)
    {
      const MatchPlayer& player = *bodies[index];
      Path& path = paths[index];
      if (onLine(index))
      {
        // Someone round the touchline: a stand-in's weight, a sub's own.
        const Vector2F start = asVector(touchlineStepStart[touchlineIndex[index]]);
        const Vector2F end = asVector(player.position);
        path.takesPart = true;
        path.anchor = start;
        path.velocity = scaled({end.x - start.x, end.y - start.y}, 1.0f / dt);
        path.inverseMass =
            1.0f / (player.player
                        ? std::max(bodyMassKilograms(player.heightMetres,
                                                     player.derivedStat(DerivedStat::STRENGTH)),
                                   1.0f)
                        : MatchTuning::TouchlinePeople::STAND_IN_MASS_KILOGRAMS);
        continue;
      }
      // A diving keeper is out of it (keepers in traffic: a later plan),
      // and a fallen player: others hurdle him.
      path.takesPart =
          player.player && bodyOf(player.isHomeTeam) == BodyId::BINTURONG &&
          !player.fallen && !deepInTunnel(player.position) &&
          !(player.onPitch && player.isGoalkeeper &&
            keeperInDive(player));
      if (!path.takesPart) continue;
      const Vector2F end = asVector(player.position);
      if (!player.onPitch)
      {
        path.anchor = end;
        continue;
      }
      const Vector2F start = asVector(stepStartPositions[index]);
      path.velocity = scaled({end.x - start.x, end.y - start.y}, 1.0f / dt);
      path.anchor = start;
      path.inverseMass =
          1.0f / std::max(bodyMassKilograms(player.heightMetres,
                                            player.derivedStat(DerivedStat::STRENGTH)),
                          1.0f);
    }
    const auto at = [&paths](std::size_t index, float time)
    { return add(paths[index].anchor, scaled(paths[index].velocity, time)); };

    // The first contact in what is left of the step, again and again (a
    // player knocked on may meet a third), until the step is clear.
    float now = 0.0f;
    for (int contact = 0; contact < C::MAX_CONTACTS_PER_STEP; ++contact)
    {
      std::optional<float> earliest;
      std::size_t firstIndex = 0;
      std::size_t secondIndex = 0;
      for (std::size_t first = 0; first < count; ++first)
      {
        if (!paths[first].takesPart) continue;
        for (std::size_t second = first + 1; second < count; ++second)
        {
          if (!paths[second].takesPart) continue;
          if (paths[first].inverseMass == 0.0f && paths[second].inverseMass == 0.0f)
            continue;
          // Round the touchline, only players run into them.
          if (onLine(first) && onLine(second)) continue;
          const Vector2F gap{paths[second].anchor.x - paths[first].anchor.x,
                             paths[second].anchor.y - paths[first].anchor.y};
          const Vector2F closing{paths[second].velocity.x - paths[first].velocity.x,
                                 paths[second].velocity.y - paths[first].velocity.y};
          const std::optional<float> time =
              contactTime(gap, closing, now, dt, MatchTuning::Body::DIAMETER_METRES);
          if (time && (!earliest || *time < *earliest))
          {
            earliest = time;
            firstIndex = first;
            secondIndex = second;
          }
        }
      }
      if (!earliest) break;
      now = *earliest;

      // The hit: along the line between their centres both leave at the
      // same speed (momentum kept, by weight: no bounce); across it each
      // keeps his own (they slide past).
      Path& one = paths[firstIndex];
      Path& two = paths[secondIndex];
      const Vector2F oneAt = at(firstIndex, now);
      const Vector2F twoAt = at(secondIndex, now);
      Vector2F line{twoAt.x - oneAt.x, twoAt.y - oneAt.y};
      const float apart = length(line);
      line = apart > EPSILON ? scaled(line, 1.0f / apart)
                             : Vector2F{0.0f, firstIndex % 2 == 0 ? 1.0f : -1.0f};
      const float oneAlong = dot(one.velocity, line);
      const float twoAlong = dot(two.velocity, line);
      const float common =
          (oneAlong * two.inverseMass + twoAlong * one.inverseMass) /
          (one.inverseMass + two.inverseMass);
      const std::array<Vector2F, 2> jolts{scaled(line, common - oneAlong),
                                          scaled(line, common - twoAlong)};
      one.velocity = add(one.velocity, jolts[0]);
      two.velocity = add(two.velocity, jolts[1]);
      one.anchor = add(oneAt, scaled(one.velocity, -now));
      two.anchor = add(twoAt, scaled(two.velocity, -now));
      one.hit = one.inverseMass > 0.0f;
      two.hit = two.inverseMass > 0.0f;

      CollisionHit record;
      record.step = stepCounter;
      record.atSeconds = now;
      record.where = asPoint(scaled(add(oneAt, twoAt), 0.5f));
      const std::array<std::size_t, 2> indices{firstIndex, secondIndex};
      const bool bothPlayers = !onLine(firstIndex) && !onLine(secondIndex);
      for (std::size_t side = 0; side < 2; ++side)
      {
        MatchPlayer& player = *bodies[indices[side]];
        CollisionSide& entry = record.sides[side];
        entry.player = player.player ? player.player->getId() : 0;
        entry.jolt = jolts[side];
        entry.after = paths[indices[side]].velocity;
        if (paths[indices[side]].inverseMass == 0.0f) continue;
        const float jolt = length(jolts[side]);
        entry.endsUp = length(entry.after) /
                       std::max(directionTopSpeedBinturong(player, entry.after),
                                EPSILON);
        entry.sudden = jolt / std::max(hardestBrakingBinturong(player) *
                                           C::SUDDEN_WINDOW_SECONDS,
                                       EPSILON);
        judgeKnock(player, entry, dt - now);
        // The knock breakdowns are the players' own.
        if (entry.outcome > 0 && bothPlayers)
        {
          const std::size_t otherSide = 1 - side;
          const Vector2F before{entry.after.x - entry.jolt.x, entry.after.y - entry.jolt.y};
          const Vector2F otherAfter = paths[indices[otherSide]].velocity;
          const Vector2F otherBefore{otherAfter.x - jolts[otherSide].x,
                                     otherAfter.y - jolts[otherSide].y};
          ++stats.collisionKnocksBySituation[knockSituation(
              indices[side], indices[otherSide], before, otherBefore,
              scaled(add(oneAt, twoAt), 0.5f), getSimulatedSeconds() + now)];
          ++stats.collisionKnocksByIntent[static_cast<std::size_t>(player.intent)];
          // A chase for a loose ball in it: what the other was doing.
          const MatchPlayer& other = players[indices[otherSide]];
          ++stats.collisionKnocksByIntentPair[static_cast<std::size_t>(player.intent)]
                                             [static_cast<std::size_t>(other.intent)];
          // Two players getting into position: why the way round did not stop it.
          const auto positioning = [](const MatchPlayer& man)
          {
            return man.intent == PlayerIntent::HOLD_SHAPE ||
                   man.intent == PlayerIntent::OFFER_SUPPORT ||
                   man.intent == PlayerIntent::RECOVER_SHAPE;
          };
          if (positioning(player) && positioning(other))
          {
            const std::size_t mine = indices[side];
            const std::size_t theirs = indices[otherSide];
            constexpr float AT_SPOT_METRES = 2.0f;
            const auto atSpot = [this](const MatchPlayer& man)
            { return distance(man.position, man.tacticalTarget) <= AT_SPOT_METRES; };
            const bool givingWay =
                player.givingWayTo == static_cast<std::int8_t>(theirs) ||
                other.givingWayTo == static_cast<std::int8_t>(mine) ||
                player.rightOfWayOver == static_cast<std::int8_t>(theirs) ||
                other.rightOfWayOver == static_cast<std::int8_t>(mine);
            const std::size_t reason =
                atSpot(player) || atSpot(other) ? 0
                : givingWay                     ? 5
                : player.wayRoundOf == static_cast<std::int8_t>(theirs) ||
                        other.wayRoundOf == static_cast<std::int8_t>(mine)
                    ? 1
                : player.wayRoundBlockedBy == static_cast<std::int8_t>(theirs) ||
                        other.wayRoundBlockedBy == static_cast<std::int8_t>(mine)
                    ? 2
                : player.wayRoundOf >= 0 || other.wayRoundOf >= 0 ? 3
                                                                   : 4;
            ++stats.positioningKnocksByReason[reason]
                                             [player.isHomeTeam == other.isHomeTeam ? 0 : 1];
            const auto band = [](float speed)
            { return speed < 1.0f ? 0U : speed < 3.0f ? 1U : speed < 5.0f ? 2U : 3U; };
            ++stats.positioningKnocksBySpeed[band(length(before))][band(length(otherBefore))];
            const float speedA = length(before);
            const float speedB = length(otherBefore);
            const float cosine = speedA > EPSILON && speedB > EPSILON
                                     ? dot(before, otherBefore) / (speedA * speedB)
                                     : 1.0f;
            ++stats.positioningKnocksByMotion[speedA < 0.5f || speedB < 0.5f ? 0
                                              : cosine >= 0.7071f            ? 1
                                              : cosine > -0.7071f            ? 2
                                                                             : 3];
          }
          const bool chasing = player.intent == PlayerIntent::CLAIM_LOOSE_BALL;
          const bool otherChasing = other.intent == PlayerIntent::CLAIM_LOOSE_BALL;
          if (chasing || otherChasing)
          {
            const std::size_t with =
                chasing && otherChasing
                    ? (player.isHomeTeam == other.isHomeTeam ? PLAYER_INTENT_COUNT
                                                             : static_cast<std::size_t>(
                                                                   PlayerIntent::CLAIM_LOOSE_BALL))
                    : static_cast<std::size_t>(chasing ? other.intent : player.intent);
            ++stats.collisionChaseKnocksWith[with];
            if (chasing) ++stats.collisionChaseKnocksOnChaser;
          }
        }
      }
      // Who ran into whom, and when: a re-hit is read against it.
      for (std::size_t side = 0; side < 2 && bothPlayers; ++side)
      {
        players[indices[side]].lastContactSlot = static_cast<std::int8_t>(indices[1 - side]);
        players[indices[side]].lastContactAt = getSimulatedSeconds() + now;
      }
      lastCollisions.push_back(record);
      ++stats.collisions;
      const float hardest = std::max(length(jolts[0]), length(jolts[1]));
      if (hardest >= 2.0f) ++stats.collisionsJoltTwo;
      if (hardest >= 4.0f) ++stats.collisionsJoltFour;
    }

    // Wound forward to the end of the step: the hit players only (everyone
    // else's line ends where his body left him).
    for (std::size_t index = 0; index < count; ++index)
    {
      if (!paths[index].hit) continue;
      MatchPlayer& player = *bodies[index];
      const PitchPoint end = asPoint(at(index, dt));
      player.position = moveBody(player.position, end);
      player.velocity = paths[index].velocity;
    }

    // Bodies still overlapping (a pile-up, a player placed on another) are
    // eased apart by a separating speed, shared by weight; never moved.
    for (std::size_t first = 0; first < count; ++first)
    {
      if (!paths[first].takesPart) continue;
      for (std::size_t second = first + 1; second < count; ++second)
      {
        if (!paths[second].takesPart || (onLine(first) && onLine(second))) continue;
        const float inverseTotal =
            paths[first].inverseMass + paths[second].inverseMass;
        if (inverseTotal <= 0.0f) continue;
        MatchPlayer& one = *bodies[first];
        MatchPlayer& two = *bodies[second];
        const Vector2F gap = two.position - one.position;
        const float apart = length(gap);
        const float overlap = MatchTuning::Body::DIAMETER_METRES - apart;
        if (overlap <= 0.0f) continue;
        const Vector2F line =
            apart > EPSILON ? scaled(gap, 1.0f / apart)
                            : Vector2F{0.0f, first % 2 == 0 ? 1.0f : -1.0f};
        const float easing = C::OVERLAP_EASING_PER_SECOND * overlap;
        // Only what they lack of it: a pair already parting fast enough is
        // left alone.
        const float parting = dot(two.velocity, line) - dot(one.velocity, line);
        const float needed = easing - parting;
        if (needed <= 0.0f) continue;
        one.velocity =
            add(one.velocity, scaled(line, -needed * paths[first].inverseMass / inverseTotal));
        two.velocity =
            add(two.velocity, scaled(line, needed * paths[second].inverseMass / inverseTotal));
      }
    }
  }
  separatePlayers();
}

namespace
{
/** A fallen body in metres: its centre, the way it lies (a unit vector),
 * and half the length of its straight part (its ends are rounded). */
struct LyingBody
{
  Vector2F centre{};
  Vector2F along{};
  float half = 0.0f;
};

LyingBody lyingBody(const MatchPitch& pitch, const MatchPlayer& player)
{
  const Vector2F along{std::cos(player.lyingAngle) * pitch.length,
                       std::sin(player.lyingAngle) * pitch.width};
  const float alongLength = std::max(length(along), EPSILON);
  return {asVector(player.position),
          scaled(along, 1.0f / alongLength),
          std::max(0.0f, 0.5f * player.heightMetres - 0.5f * MatchTuning::Body::DIAMETER_METRES)};
}

/** The nearest point of the body's middle line to `point` (metres). */
Vector2F nearestOnBody(const LyingBody& body, Vector2F point)
{
  const Vector2F offset{point.x - body.centre.x, point.y - body.centre.y};
  const float along = std::clamp(dot(offset, body.along), -body.half, body.half);
  return add(body.centre, scaled(body.along, along));
}

/** When (0-1) a point moving `start` + `move` * t first comes `reach` from
 * the body's middle line, closing; touching and closing at the start: 0. */
std::optional<float> bodyContactTime(const LyingBody& body, Vector2F start, Vector2F move,
                                     float reach)
{
  const Vector2F nearest = nearestOnBody(body, start);
  const Vector2F gap{start.x - nearest.x, start.y - nearest.y};
  if (length(gap) <= reach)
  {
    if (dot(gap, move) < 0.0f) return 0.0f;
    return std::nullopt;
  }
  const Vector2F across{-body.along.y, body.along.x};
  const Vector2F offset{start.x - body.centre.x, start.y - body.centre.y};
  const float a0 = dot(offset, body.along);
  const float b0 = dot(offset, across);
  const float da = dot(move, body.along);
  const float db = dot(move, across);
  std::optional<float> first;
  const auto take = [&first](float time)
  {
    if (time >= 0.0f && time <= 1.0f && (!first || time < *first)) first = time;
  };
  // Its flat sides.
  for (const float side : {reach, -reach})
  {
    if (std::abs(db) <= EPSILON || (b0 - side) * db >= 0.0f) continue;
    const float time = (side - b0) / db;
    if (std::abs(a0 + da * time) <= body.half) take(time);
  }
  // Its rounded ends.
  for (const float end : {body.half, -body.half})
  {
    const Vector2F rel{a0 - end, b0};
    const Vector2F moving{da, db};
    const float a = dot(moving, moving);
    if (a <= EPSILON) continue;
    const float b = 2.0f * dot(rel, moving);
    const float c = dot(rel, rel) - reach * reach;
    const float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f) continue;
    take((-b - std::sqrt(discriminant)) / (2.0f * a));
  }
  return first;
}
}  // namespace

std::optional<std::pair<std::size_t, float>> MatchEngine::fallenBodyInPath(
    float seconds) const
{
  using C = MatchTuning::Collision;
  if (state != MatchState::PLAYING || (getPossessionAttachment() && !hasTouchClaim())) return std::nullopt;
  const Vector2F start = asVector(ball.position);
  const Vector2F move = scaled(ball.velocity, seconds);
  const float startHeight = ball.z;
  const float reach =
      0.5f * MatchTuning::Body::DIAMETER_METRES + MatchTuning::Units::BALL_RADIUS_METRES;
  std::optional<std::pair<std::size_t, float>> first;
  for (std::size_t slot = 0; slot < players.size(); ++slot)
  {
    const MatchPlayer& player = players[slot];
    if (!active(player) || !player.fallen) continue;
    const std::optional<float> time =
        bodyContactTime(lyingBody(pitch, player), start, move, reach);
    if (!time) continue;
    // Over him, it goes on.
    if (startHeight + ball.velocityZ * seconds * *time > C::FALLEN_BALL_BELOW_METRES)
      continue;
    if (!first || *time < first->second) first = std::pair{slot, *time};
  }
  return first;
}

void MatchEngine::bounceOffFallen(MatchPlayer& player)
{
  using C = MatchTuning::Collision;
  // Off the surface where it meets him (it may have been sent another way
  // on the way there: then it is not coming into him).
  const LyingBody body = lyingBody(pitch, player);
  const Vector2F at = asVector(ball.position);
  const Vector2F nearest = nearestOnBody(body, at);
  Vector2F normal{at.x - nearest.x, at.y - nearest.y};
  const float apart = length(normal);
  const float speed = length(ball.velocity);
  if (apart > EPSILON)
    normal = scaled(normal, 1.0f / apart);
  else if (speed > EPSILON)
    normal = scaled(ball.velocity, -1.0f / speed);
  else
    return;
  const float into = dot(ball.velocity, normal);
  if (into >= 0.0f) return;
  const Vector2F alongSurface = add(ball.velocity, scaled(normal, -into));
  ball.velocity = add(scaled(alongSurface, C::FALLEN_ALONG_KEPT_SHARE),
                      scaled(normal, -into * C::FALLEN_BOUNCE_BACK_SHARE));
  noteFallenDeflection(player);
}

const Player* MatchEngine::lastTouchForRestart() const
{
  if (getPossessionState().deflectedBy && getPossessionState().pathSince == getPossessionState().deflectedPathSince &&
      getLastPossessionAttachment() == getPossessionState().deflectedAfter)
    return getPossessionState().deflectedBy;
  return getLastPossessionAttachment();
}

std::size_t MatchEngine::knockSituation(std::size_t slot, std::size_t otherSlot,
                                        Vector2F before, Vector2F otherBefore,
                                        Vector2F where, double at) const
{
  // Constants of the breakdown only (a measure, not a rule).
  constexpr double REHIT_SECONDS = 1.0;
  constexpr float BALL_METRES = 2.0f;
  constexpr float AIMED_METRES = 1.5f;
  constexpr float SAME_TARGET_METRES = 2.0f;
  constexpr float SAME_WAY_COSINE = 0.7071f;
  constexpr float SLOW_SPEED = 1.0f;
  const MatchPlayer& player = players[slot];
  const MatchPlayer& other = players[otherSlot];
  if (player.lastContactSlot == static_cast<std::int8_t>(otherSlot) &&
      at - player.lastContactAt <= REHIT_SECONDS)
    return 0;
  const Vector2F ballAt = asVector(ball.position);
  if (length({ballAt.x - where.x, ballAt.y - where.y}) <= BALL_METRES) return 1;
  if (distance(player.tacticalTarget, other.position) <= AIMED_METRES) return 2;
  if (player.overtaking == static_cast<std::int8_t>(otherSlot)) return 3;
  const float speed = length(before);
  const float otherSpeed = length(otherBefore);
  const bool sameWay = speed > EPSILON && otherSpeed > EPSILON &&
                       dot(before, otherBefore) / (speed * otherSpeed) >= SAME_WAY_COSINE;
  if (sameWay && distance(player.tacticalTarget, other.tacticalTarget) <= SAME_TARGET_METRES)
    return 4;
  if (speed < SLOW_SPEED || otherSpeed < SLOW_SPEED) return 5;
  if (!sameWay) return 6;
  return 7;
}
