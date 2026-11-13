// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The people round the touchline (touchline plan, stage 2): each side's subs
// and staff seated in its dugout, and its manager in his technical area.
// Their bodies are MatchPlayers, so they walk, get knocked, stumble and fall
// as players do (the Binturong body), and players run into them
// (collidePlayers). Only a side with the Binturong body has them. The match
// officials (stage 3) are bodies here too: the referee on his diagonal, his
// assistants along the touchlines, the fourth official between the
// technical areas. They referee nothing (the engine still decides).

#include <algorithm>
#include <cmath>
#include <numbers>

#include "model/match_derived_stats.h"
#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/match_ground.h"

using namespace MatchEngineInternal;

namespace
{
/** Facing the pitch from the bottom touchline (normalised pitch space). */
constexpr float FACING_PITCH = -0.5f * std::numbers::pi_v<float>;

Vector2F metresOf(const MatchPitch& pitch, PitchPoint at)
{
  return {pitch.metresX(at), pitch.metresY(at)};
}

PitchPoint pitchOf(const MatchPitch& pitch, Vector2F metres)
{
  return PitchPoint{pitch.fromMetresX(metres.x), pitch.fromMetresY(metres.y)};
}

/**
 * The next point on the way from `at` to `goal` (metres, `goal` beside the
 * bottom touchline) round the pitch's edge, inside the boards: a lap of a
 * rectangle 2 m outside the touchlines and 3 m outside the goal lines
 * (clear of the nets), the shorter way round.
 */
Vector2F roundThePitch(const MatchPitch& pitch, Vector2F at, Vector2F goal)
{
  constexpr float X0 = -3.0f;
  const float X1 = pitch.length + 3.0f;
  constexpr float Y0 = -2.0f;
  const float Y1 = pitch.width + 2.0f;
  // Where along the lap (clockwise from the top-left corner: top, right,
  // bottom, left), from the nearest point on it.
  const auto along = [&](Vector2F p)
  {
    const float x = std::clamp(p.x, X0, X1);
    const float y = std::clamp(p.y, Y0, Y1);
    const std::array<float, 4> gaps{std::abs(y - Y0), std::abs(X1 - x), std::abs(Y1 - y),
                                    std::abs(x - X0)};
    const auto side = static_cast<int>(std::ranges::min_element(gaps) - gaps.begin());
    const float top = X1 - X0;
    const float right = Y1 - Y0;
    switch (side)
    {
      case 0: return x - X0;
      case 1: return top + (y - Y0);
      case 2: return top + right + (X1 - x);
      default: return 2.0f * top + right + (Y1 - y);
    }
  };
  const float lap = 2.0f * ((X1 - X0) + (Y1 - Y0));
  const std::array<std::pair<float, Vector2F>, 4> corners{{
      {0.0f, {X0, Y0}},
      {X1 - X0, {X1, Y0}},
      {(X1 - X0) + (Y1 - Y0), {X1, Y1}},
      {2.0f * (X1 - X0) + (Y1 - Y0), {X0, Y1}},
  }};
  const float from = along(at);
  const float to = along(goal);
  float ahead = to - from;
  if (ahead < 0.0f) ahead += lap;
  const bool clockwise = ahead <= 0.5f * lap;
  // The first corner between him and the goal that way, else the goal.
  float best = lap;
  Vector2F next = goal;
  for (const auto& [where, corner] : corners)
  {
    float gap = clockwise ? where - from : from - where;
    if (gap < 0.0f) gap += lap;
    const float total = clockwise ? ahead : lap - ahead;
    if (gap > 0.05f && gap < total && gap < best)
    {
      best = gap;
      next = corner;
    }
  }
  // Out to the lap first if he is still on the pitch side of it.
  if (at.x > X0 + 0.5f && at.x < X1 - 0.5f && at.y > Y0 + 0.5f && at.y < Y1 - 0.5f)
  {
    const float x = std::clamp(at.x, X0, X1);
    const float y = std::clamp(at.y, Y0, Y1);
    const std::array<std::pair<float, Vector2F>, 4> outs{{
        {std::abs(y - Y0), {x, Y0}},
        {std::abs(X1 - x), {X1, y}},
        {std::abs(Y1 - y), {x, Y1}},
        {std::abs(x - X0), {X0, y}},
    }};
    const auto nearest = std::ranges::min(outs, {}, &std::pair<float, Vector2F>::first);
    // Already near the goal along the bottom: straight to it.
    if (std::hypot(goal.x - at.x, goal.y - at.y) > 3.0f && nearest.first > 0.5f)
      return nearest.second;
  }
  return next;
}

/** Turns his body toward `angle` at his turn rate. */
void turnToward(MatchPlayer& body, float angle, float dt)
{
  constexpr float PI = std::numbers::pi_v<float>;
  float difference = angle - body.facingAngle;
  while (difference > PI) difference -= 2.0f * PI;
  while (difference < -PI) difference += 2.0f * PI;
  body.targetAngle = angle;
  body.facingAngle += std::clamp(difference, -body.turnRate * dt, body.turnRate * dt);
  if (body.facingAngle > PI)
    body.facingAngle -= 2.0f * PI;
  else if (body.facingAngle < -PI)
    body.facingAngle += 2.0f * PI;
}
}  // namespace

void MatchEngine::setUpTouchline()
{
  using T = MatchTuning::TouchlinePeople;
  const MatchGround::Layout& G = pitch.ground;
  static_assert(T::STAND_IN_ATTRIBUTE == MatchTuning::Player::DEFAULT_ATTRIBUTE,
                "staff (no player) load the stand-in stats");
  touchlineSetUp = true;
  touchlineBodies.clear();
  touchlinePeople.clear();
  for (const bool home : {true, false})
  {
    if (bodyOf(home) != BodyId::BINTURONG) continue;
    const MatchGround::Rect& dugout = G.dugouts[home ? 0 : 1];
    // Along the bench from the dugout's halfway end, backs to the stand.
    const float end = home ? dugout.x1 : dugout.x0;
    const float inward = home ? -1.0f : 1.0f;
    const float seatY = G.standY - MatchTuning::Body::RADIUS_METRES;
    int seats = 0;
    const auto add = [&](TouchlineRole role, const Player* player)
    {
      Vector2F at;
      if (role == TouchlineRole::MANAGER)
      {
        at = {end + inward * T::FIRST_SEAT_METRES, dugout.y0 - T::MANAGER_SPOT_OUT_METRES};
      }
      else
      {
        at = {end + inward * (T::FIRST_SEAT_METRES +
                              T::SEAT_SPACING_METRES * static_cast<float>(seats++)),
              seatY};
        // A full bench: nobody beyond its far end.
        if (at.x < dugout.x0 || at.x > dugout.x1) return;
      }
      MatchPlayer& body = touchlineBodies.emplace_back();
      body.player = player;
      body.isHomeTeam = home;
      body.onPitch = false;
      // A sub's own stats; staff, the stand-ins (every stat 50).
      loadAttributes(body, player);
      body.position = pitchOf(pitch, at);
      body.movementTarget = body.position;
      body.tacticalTarget = body.position;
      body.facingAngle = FACING_PITCH;
      body.targetAngle = FACING_PITCH;
      TouchlinePerson& person = touchlinePeople.emplace_back();
      person.role = role;
      person.seat = body.position;
      person.seated = role != TouchlineRole::MANAGER;
    };
    add(TouchlineRole::ASSISTANT, nullptr);
    add(TouchlineRole::GOALKEEPER_COACH, nullptr);
    add(TouchlineRole::PHYSIO, nullptr);
    for (const Player* sub : home ? homeBench : awayBench)
      add(TouchlineRole::SUBSTITUTE, sub);
    add(TouchlineRole::MANAGER, nullptr);
  }
  // The officials (stage 3), with either side on the Binturong body: the
  // stand-ins, the referee and his assistants reaching their top speed.
  if (bodyOf(true) == BodyId::BINTURONG || bodyOf(false) == BodyId::BINTURONG)
  {
    const auto addOfficial = [this](TouchlineRole role, bool topLine)
    {
      MatchPlayer& body = touchlineBodies.emplace_back();
      body.onPitch = false;
      loadAttributes(body, nullptr);
      if (role != TouchlineRole::FOURTH_OFFICIAL)
        body.maxSpeed = MatchTuning::Officials::TOP_SPEED;
      TouchlinePerson& person = touchlinePeople.emplace_back();
      person.role = role;
      person.topLine = topLine;
      // On their spots for the kick-off (the teams are placed too, until
      // they walk out: stage 4).
      body.position = pitchOf(pitch, officialSpot(body, person));
      body.movementTarget = body.position;
      body.tacticalTarget = body.position;
      person.seat = body.position;
      body.facingAngle = topLine ? -FACING_PITCH : FACING_PITCH;
      body.targetAngle = body.facingAngle;
    };
    addOfficial(TouchlineRole::REFEREE, false);
    addOfficial(TouchlineRole::ASSISTANT_REFEREE, true);
    addOfficial(TouchlineRole::ASSISTANT_REFEREE, false);
    addOfficial(TouchlineRole::FOURTH_OFFICIAL, false);
  }
  previousTouchlinePositions.clear();
  previousTouchlineFacingAngles.clear();
  touchlineStepStart.clear();
  for (const MatchPlayer& body : touchlineBodies)
  {
    previousTouchlinePositions.push_back(body.position);
    previousTouchlineFacingAngles.push_back(body.facingAngle);
    touchlineStepStart.push_back(body.position);
  }
}

void MatchEngine::updateTouchline(float dt)
{
  using T = MatchTuning::TouchlinePeople;
  const MatchGround::Layout& G = pitch.ground;
  constexpr float R = MatchTuning::Body::RADIUS_METRES;
  const Vector2F ballAt = metresOf(pitch, ball.position);
  for (std::size_t index = 0; index < touchlineBodies.size(); ++index)
  {
    MatchPlayer& body = touchlineBodies[index];
    TouchlinePerson& person = touchlinePeople[index];
    const std::size_t side = body.isHomeTeam ? 0 : 1;
    // Knocked about: as a player is, off his seat.
    if (body.fallen || body.stumbleSeconds > 0.0f || body.collisionPause > 0.0f)
    {
      person.seated = false;
      if (body.fallen)
        fallenBinturong(body, dt);
      else if (body.stumbleSeconds > 0.0f)
        stumbleBinturong(body, dt);
      else
        coastBinturong(body, dt);
      continue;
    }

    // A walk-out or walk-off (stage 4): waiting in the tunnel, or walking
    // to where it has him (through the tunnel); else his usual place.
    if (walk.phase != WalkPhase::NONE)
    {
      const auto [waits, walkTarget] = touchlineWalk(index);
      if (waits)
      {
        body.velocity = {0.0f, 0.0f};
        continue;
      }
      if (walkTarget)
      {
        person.seated = false;
        const Vector2F at = metresOf(pitch, body.position);
        const Vector2F next = routeViaTunnel(at, *walkTarget);
        const Vector2F way{next.x - at.x, next.y - at.y};
        const float gap = std::hypot(way.x, way.y);
        const bool final = next.x == walkTarget->x && next.y == walkTarget->y;
        if (final && gap <= MatchTuning::WalkOut::ARRIVED_METRES)
        {
          // There: facing the dugouts' stand (the line-up).
          moveControlledBinturong(body, {0.0f, 0.0f}, 0.0f, T::WALK_SPEED, 0.0f, dt, nullptr);
          turnToward(body, -FACING_PITCH, dt);
          continue;
        }
        const float speed = std::min(T::WALK_SPEED, final ? gap * 2.0f : T::WALK_SPEED);
        body.movementTarget = pitchOf(pitch, next);
        moveControlledBinturong(body, {way.x / gap * speed, way.y / gap * speed}, speed,
                                T::WALK_SPEED, 0.0f, dt, nullptr);
        continue;
      }
    }

    // Sent off (stage 6): off at the nearest point of the boundary (his
    // `seat`), round the pitch's edge to the tunnel, through the gap in the
    // boards, and in he stays.
    if (person.role == TouchlineRole::SENT_OFF)
    {
      const Vector2F at = metresOf(pitch, body.position);
      if (deepInTunnel(body.position))
      {
        body.velocity = {0.0f, 0.0f};
        continue;
      }
      const bool on = at.x > -R && at.x < pitch.length + R && at.y > -R && at.y < pitch.width + R;
      const Vector2F front{G.halfway, pitch.width + 1.5f};
      if (!on && person.walkingRound && std::hypot(front.x - at.x, front.y - at.y) <= 1.5f)
        person.walkingRound = false;
      const float file = body.isHomeTeam ? -1.0f : 1.0f;
      const Vector2F tunnel{G.halfway + file * MatchTuning::WalkOut::FILE_METRES,
                            G.standY + MatchTuning::WalkOut::WAIT_METRES};
      const Vector2F next = on && person.walkingRound ? metresOf(pitch, person.seat)
                            : person.walkingRound    ? roundThePitch(pitch, at, front)
                                                     : routeViaTunnel(at, tunnel);      const Vector2F way{next.x - at.x, next.y - at.y};
      const float gap = std::max(std::hypot(way.x, way.y), 1e-4f);
      body.movementTarget = pitchOf(pitch, next);
      moveControlledBinturong(body, {way.x / gap * T::WALK_SPEED, way.y / gap * T::WALK_SPEED},
                              T::WALK_SPEED, T::WALK_SPEED, 0.0f, dt, nullptr);
      continue;
    }

    if (isOfficial(person.role))
    {
      const Vector2F spot = officialSpot(body, person);
      person.seat = pitchOf(pitch, spot);
      moveOfficial(body, person, routeViaTunnel(metresOf(pitch, body.position), spot), dt);
      continue;
    }

    const bool manager = person.role == TouchlineRole::MANAGER;
    if (manager)
    {
      // Level with the play along his technical area; up at its front edge
      // while the ball is near it or just after his side's shout, else a
      // little in front of his dugout.
      const MatchGround::Rect& area = G.technicalAreas[side];
      const float dx = std::max({area.x0 - ballAt.x, 0.0f, ballAt.x - area.x1});
      const float dy = std::max(area.y0 - ballAt.y, 0.0f);
      const bool forward =
          (state == MatchState::PLAYING && std::hypot(dx, dy) <= T::MANAGER_BALL_NEAR_METRES) ||
          shouts[side].remainingSeconds >
              MatchTuning::Touchline::SHOUT_DURATION_SECONDS - T::MANAGER_SHOUT_SECONDS;
      const Vector2F spot = metresOf(pitch, person.seat);
      const float y = forward ? area.y0 + R : G.dugouts[side].y0 - T::MANAGER_SPOT_OUT_METRES;
      const float x = std::clamp(ballAt.x, area.x0 + R, area.x1 - R);
      person.seat = pitchOf(pitch, {std::abs(x - spot.x) >= T::MANAGER_FOLLOW_METRES ? x : spot.x, y});
    }
    // A sub coming on (stage 5): up from the bench to wait on the touchline.
    const bool comingOnNow = person.role == TouchlineRole::SUBSTITUTE && comingOn(body.player);
    if (comingOnNow)
      person.seated = false;
    else if (!manager && person.seated)
    {
      body.velocity = {0.0f, 0.0f};
      turnToward(body, FACING_PITCH, dt);
      continue;
    }

    // To his seat (into the dugout by its nearer open end when he is out
    // in front of it), his spot, or where he waits to come on; out of the
    // dugout the same way; round the pitch's edge when just substituted.
    const Vector2F at = metresOf(pitch, body.position);
    const Vector2F goal = comingOnNow ? substituteWaitSpot(body.isHomeTeam, body.player) : metresOf(pitch, person.seat);
    const MatchGround::Rect& dugout = G.dugouts[side];
    const float middleY = 0.5f * (dugout.y0 + dugout.y1);
    const bool inDugout = at.x > dugout.x0 && at.x < dugout.x1 && at.y > dugout.y0;
    const bool goalInDugout = !manager && !comingOnNow;
    // Beside a dugout's open end: clear of its front, and of the boards'
    // end at the edge of the technical area, 1 m further out.
    constexpr float END_CLEAR = R + 0.05f;
    if (person.walkingRound)
    {
      // Round the pitch's edge (inside the boards) to the front of his
      // technical area; from there to his seat.
      const MatchGround::Rect& area = G.technicalAreas[side];
      const Vector2F front{0.5f * (area.x0 + area.x1), pitch.width + 1.5f};
      if (std::hypot(front.x - at.x, front.y - at.y) <= 1.5f) person.walkingRound = false;
    }
    Vector2F target = routeViaTunnel(at, goal);
    const bool viaTunnel = target.x != goal.x || target.y != goal.y;
    if (person.walkingRound)
    {
      const MatchGround::Rect& area = G.technicalAreas[side];
      target = roundThePitch(pitch, at, {0.5f * (area.x0 + area.x1), pitch.width + 1.5f});
    }
    else if (!viaTunnel && goalInDugout && !inDugout &&
             std::abs(at.y - middleY) > 2.0f * T::ARRIVED_METRES)
    {
      const bool nearLeft = std::abs(at.x - dugout.x0) < std::abs(at.x - dugout.x1);
      target = {nearLeft ? dugout.x0 - END_CLEAR : dugout.x1 + END_CLEAR, middleY};
    }
    else if (!viaTunnel && !goalInDugout && inDugout && std::abs(at.y - middleY) > 0.3f)
    {
      // Out along the bench to the nearer open end first.
      target = {std::clamp(at.x, dugout.x0 + 0.5f, dugout.x1 - 0.5f), middleY};
    }
    else if (!viaTunnel && !goalInDugout && inDugout)
    {
      const bool nearLeft = std::abs(at.x - dugout.x0) < std::abs(at.x - dugout.x1);
      target = {nearLeft ? dugout.x0 - END_CLEAR : dugout.x1 + END_CLEAR, middleY};
    }
    else if (!viaTunnel && !goalInDugout && at.y > dugout.y0 - 0.2f &&
             at.x > dugout.x0 - 2.0f * R - 0.5f && at.x < dugout.x1 + 2.0f * R + 0.5f)
    {
      // Out of its end, beside it: forward, clear of its front, first.
      target = {at.x < 0.5f * (dugout.x0 + dugout.x1) ? dugout.x0 - END_CLEAR
                                                       : dugout.x1 + END_CLEAR,
                dugout.y0 - 1.5f};
    }
    const Vector2F way{target.x - at.x, target.y - at.y};
    const float gap = std::hypot(way.x, way.y);
    const bool home = target.x == goal.x && target.y == goal.y;
    if (home && gap <= T::ARRIVED_METRES)
    {
      body.velocity = {0.0f, 0.0f};
      if (manager)
        turnToward(body,
                   std::atan2(pitch.fractionY(ball.position.y - body.position.y),
                              pitch.fractionX(ball.position.x - body.position.x)),
                   dt);
      else if (comingOnNow)
        turnToward(body, FACING_PITCH, dt);
      else
        person.seated = true;
      continue;
    }
    // A walk, easing in to stop on it.
    const float speed = std::min(T::WALK_SPEED, home ? gap * 2.0f : T::WALK_SPEED);
    const Vector2F desired{way.x / gap * speed, way.y / gap * speed};
    body.movementTarget = pitchOf(pitch, target);
    moveControlledBinturong(body, desired, speed, T::WALK_SPEED, 0.0f, dt, nullptr);
  }
}

Vector2F MatchEngine::officialSpot(const MatchPlayer& body, const TouchlinePerson& person)
{
  using O = MatchTuning::Officials;
  using P = MatchTuning::Pitch;
  const MatchGround::Layout& G = pitch.ground;
  const float CENTRE_Y = 0.5f * pitch.width;
  const Vector2F at = metresOf(pitch, body.position);
  const Vector2F ballAt = metresOf(pitch, ball.position);
  const Vector2F restartAt = metresOf(pitch, deadBall.active ? deadBall.spot : ball.position);
  const MatchPlayer* taker = restartTaker();
  const auto inside = [this](Vector2F spot)
  {
    return Vector2F{std::clamp(spot.x, O::INSIDE_LINES_METRES, pitch.length - O::INSIDE_LINES_METRES),
                    std::clamp(spot.y, O::INSIDE_LINES_METRES, pitch.width - O::INSIDE_LINES_METRES)};
  };

  if (person.role == TouchlineRole::FOURTH_OFFICIAL)
    return {G.fourthOfficialX, G.fourthOfficialY};

  if (person.role == TouchlineRole::ASSISTANT_REFEREE)
  {
    // His half: the top one the left (the home side defends it: home
    // attacks +x), the bottom one the right.
    const bool top = person.topLine;
    if (state == MatchState::PENALTY && (restartAt.x < G.halfway) == top)
    {
      // A penalty in his half: on the goal line where the area meets it.
      return {top ? 0.0f : pitch.length,
              CENTRE_Y + (top ? -1.0f : 1.0f) * P::PENALTY_AREA_HALF_WIDTH_METRES};
    }
    // Level with the second-last defender, or the ball if it is nearer his
    // goal line; never out of his half.
    float last = top ? pitch.length : 0.0f;
    float secondLast = last;
    for (const MatchPlayer& player : players)
    {
      if (!active(player) || player.isHomeTeam != top) continue;
      const float x = pitch.metresX(player.position);
      if (top ? x < last : x > last)
      {
        secondLast = last;
        last = x;
      }
      else if (top ? x < secondLast : x > secondLast)
      {
        secondLast = x;
      }
    }
    const float level = top ? std::min(secondLast, ballAt.x) : std::max(secondLast, ballAt.x);
    const float x =
        top ? std::clamp(level, 0.0f, G.halfway) : std::clamp(level, G.halfway, pitch.length);
    // A throw-in on his touchline near him (within the clearance he keeps
    // from players): behind the thrower, not beside him.
    const bool throwInNearHim = state == MatchState::THROW_IN &&
                                (restartAt.y < CENTRE_Y) == top &&
                                std::abs(x - restartAt.x) <= O::CLEARANCE_METRES;
    const float out = throwInNearHim
                          ? MatchTuning::DeadBall::THROW_IN_STAND_BEHIND_METRES +
                                O::ASSISTANT_BEHIND_THROWER_METRES
                          : O::ASSISTANT_OUT_METRES;
    return {x, top ? -out : pitch.width + out};
  }

  // The referee: on his diagonal (play between him and the assistant on
  // that half).
  const float x0 = O::DIAGONAL_END_X_METRES;
  const float x1 = pitch.length - O::DIAGONAL_END_X_METRES;
  const auto diagonalY = [&](float x)
  {
    const float share = std::clamp((x - x0) / (x1 - x0), 0.0f, 1.0f);
    const float low = pitch.width - O::DIAGONAL_END_Y_METRES;
    return low + (O::DIAGONAL_END_Y_METRES - low) * share;
  };
  const auto rotate = [](Vector2F v, float angle)
  {
    return Vector2F{v.x * std::cos(angle) - v.y * std::sin(angle),
                    v.x * std::sin(angle) + v.y * std::cos(angle)};
  };
  const auto usualSpot = [&]() -> std::optional<Vector2F>
  {
    switch (state)
    {
      case MatchState::KICK_OFF:
      {
        // In the half of the side not kicking off (home's half is the left).
        const float into = !taker || taker->isHomeTeam ? 1.0f : -1.0f;
        return Vector2F{G.halfway + into * O::KICK_OFF_INTO_HALF_METRES,
                        CENTRE_Y + O::KICK_OFF_BELOW_CENTRE_METRES};
      }
      case MatchState::GOAL_KICK:
      {
        const float into = taker && !taker->isHomeTeam ? 1.0f : -1.0f;
        const float x = G.halfway + into * O::GOAL_KICK_FROM_HALFWAY_METRES;
        return Vector2F{x, diagonalY(x)};
      }
      case MatchState::CORNER_KICK:
      {
        const float goalX = restartAt.x < G.halfway ? 0.0f : pitch.length;
        const float inward = goalX == 0.0f ? 1.0f : -1.0f;
        const float across = restartAt.y < CENTRE_Y ? 1.0f : -1.0f;
        return inside({goalX + inward * O::CORNER_OUT_METRES,
                       CENTRE_Y + across * (MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES +
                                            O::CORNER_PAST_FAR_POST_METRES)});
      }
      case MatchState::FREE_KICK:
      {
        // To the side, where he sees the wall: of the two, the one with more
        // pitch round it.
        const Vector2F goal{taker && taker->isHomeTeam ? pitch.length : 0.0f, CENTRE_Y};
        Vector2F toGoal{goal.x - restartAt.x, goal.y - restartAt.y};
        const float gap = std::max(std::hypot(toGoal.x, toGoal.y), 1e-3f);
        toGoal = {toGoal.x / gap * O::FREE_KICK_METRES, toGoal.y / gap * O::FREE_KICK_METRES};
        const Vector2F a = rotate(toGoal, O::FREE_KICK_ANGLE_RADIANS);
        const Vector2F b = rotate(toGoal, -O::FREE_KICK_ANGLE_RADIANS);
        const Vector2F one{restartAt.x + a.x, restartAt.y + a.y};
        const Vector2F two{restartAt.x + b.x, restartAt.y + b.y};
        return inside(std::abs(one.y - CENTRE_Y) <= std::abs(two.y - CENTRE_Y) ? one : two);
      }
      case MatchState::PENALTY:
      {
        // Outside the area by the end of the arc (user): a clear view of the
        // kicker, the defenders and the attackers. The end he is nearer.
        const float goalX = restartAt.x < G.halfway ? 0.0f : pitch.length;
        const float outward = goalX == 0.0f ? 1.0f : -1.0f;
        const float areaDepth = P::LEFT_PENALTY_AREA_EDGE * pitch.length;
        const float spotDepth = P::LEFT_PENALTY_SPOT_X * pitch.length;
        const float arc = MatchTuning::Timing::KICK_OPPONENT_DISTANCE_METRES;
        const float arcEnd = std::sqrt(
            std::max(arc * arc - (areaDepth - spotDepth) * (areaDepth - spotDepth), 0.0f));
        const float side = at.y < CENTRE_Y ? -1.0f : 1.0f;
        return Vector2F{goalX + outward * (areaDepth + O::PENALTY_BEYOND_AREA_METRES),
                        CENTRE_Y + side * (arcEnd + O::PENALTY_BEYOND_ARC_METRES)};
      }
      case MatchState::GOAL:
        return Vector2F{G.halfway, CENTRE_Y + O::GOAL_BELOW_CENTRE_METRES};
      case MatchState::PLAYING:
      case MatchState::THROW_IN:
        break;
      default:
        return std::nullopt;
    }
    // In play: on his diagonal by the ball, 15-25 m from it.
    const Vector2F onLine{std::clamp(ballAt.x, x0, x1), diagonalY(ballAt.x)};
    Vector2F away{onLine.x - ballAt.x, onLine.y - ballAt.y};
    float apart = std::hypot(away.x, away.y);
    if (apart < 1e-3f)
    {
      // On the diagonal itself: away from the assistant on that half.
      away = {0.0f, ballAt.x < G.halfway ? 1.0f : -1.0f};
      apart = 1.0f;
    }
    const float keep =
        std::clamp(apart, O::NEAREST_TO_BALL_METRES, O::FURTHEST_FROM_BALL_METRES);
    return inside({ballAt.x + away.x / apart * keep, ballAt.y + away.y / apart * keep});
  };
  const std::optional<Vector2F> usual = usualSpot();
  if (!usual) return at;

  // Out of the way (user): near his usual spot, where the nearest player is
  // furthest off, without dithering between near-equal spots.
  const auto clearance = [this](Vector2F spot)
  {
    float nearest = O::CLEAR_SPOT_METRES;
    for (const MatchPlayer& player : players)
    {
      if (!active(player)) continue;
      const Vector2F them = metresOf(pitch, player.position);
      nearest = std::min(nearest, std::hypot(them.x - spot.x, them.y - spot.y));
    }
    return nearest;
  };
  const auto score = [&](Vector2F spot)
  {
    return clearance(spot) -
           O::CLEAR_SPOT_MOVE_COST * (std::hypot(spot.x - usual->x, spot.y - usual->y) +
                                      0.5f * std::hypot(spot.x - at.x, spot.y - at.y));
  };
  Vector2F best = *usual;
  float bestScore = score(best);
  constexpr int DIRECTIONS = 12;
  for (const float ring : {0.5f * O::CLEAR_SPOT_SEARCH_METRES, O::CLEAR_SPOT_SEARCH_METRES})
    for (int step = 0; step < DIRECTIONS; ++step)
    {
      const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(step) /
                          static_cast<float>(DIRECTIONS);
      const Vector2F spot =
          inside({usual->x + ring * std::cos(angle), usual->y + ring * std::sin(angle)});
      const float value = score(spot);
      if (value > bestScore)
      {
        bestScore = value;
        best = spot;
      }
    }
  // Where he is, if no worse: he stays put rather than shuffle.
  if (const Vector2F here = inside(at); score(here) >= bestScore) best = here;
  return best;
}

void MatchEngine::moveOfficial(MatchPlayer& body, const TouchlinePerson& person,
                               Vector2F spot, float dt)
{
  using O = MatchTuning::Officials;
  const bool referee = person.role == TouchlineRole::REFEREE;
  const bool assistant = person.role == TouchlineRole::ASSISTANT_REFEREE;
  const float topSpeed = person.role == TouchlineRole::FOURTH_OFFICIAL
                             ? MatchTuning::TouchlinePeople::WALK_SPEED
                             : O::TOP_SPEED;
  // The referee watches the ball; the others face the pitch.
  const float facing =
      referee ? std::atan2(pitch.fractionY(ball.position.y - body.position.y),
                           pitch.fractionX(ball.position.x - body.position.x))
      : person.topLine ? -FACING_PITCH
                       : FACING_PITCH;
  const Vector2F at = metresOf(pitch, body.position);
  const Vector2F way{spot.x - at.x, spot.y - at.y};
  const float gap = std::hypot(way.x, way.y);
  body.movementTarget = pitchOf(pitch, spot);
  const bool there = gap <= O::DEAD_BAND_METRES;
  // As fast as lets him pull up on it.
  float speed = there ? 0.0f : std::min(topSpeed, std::sqrt(2.0f * O::ARRIVE_BRAKING * gap));
  Vector2F desired = there ? Vector2F{} : Vector2F{way.x / gap * speed, way.y / gap * speed};
  bool dodging = false;
  // Every official out of the players' way (user). Of the speeds and
  // directions he could take, the nearest to what he wants that keeps
  // everyone (going on as they are) clear of him through the look-ahead;
  // failing that, the one that keeps them off longest.
  {
    // When a player first comes within the clearance (the look-ahead and
    // more if never; 0 if he already is and they are not parting).
    const auto soonest = [&](Vector2F velocity)
    {
      float first = O::LOOK_AHEAD_SECONDS + 1.0f;
      for (const MatchPlayer& player : players)
      {
        if (!active(player)) continue;
        const Vector2F them = metresOf(pitch, player.position);
        const Vector2F rel{them.x - at.x, them.y - at.y};
        const Vector2F closing{player.velocity.x - velocity.x, player.velocity.y - velocity.y};
        const float along = rel.x * closing.x + rel.y * closing.y;
        if (along >= 0.0f) continue;  // parting
        const float c = rel.x * rel.x + rel.y * rel.y - O::CLEARANCE_METRES * O::CLEARANCE_METRES;
        if (c <= 0.0f)
        {
          first = 0.0f;
          continue;
        }
        const float a = closing.x * closing.x + closing.y * closing.y;
        const float discriminant = along * along - a * c;
        if (discriminant < 0.0f) continue;  // passes clear
        first = std::min(first, (-along - std::sqrt(discriminant)) / a);
      }
      return first;
    };
    const auto cost = [&](Vector2F velocity)
    {
      const float when = soonest(velocity);
      const float threat =
          when <= O::LOOK_AHEAD_SECONDS ? O::THREAT_COST * (O::LOOK_AHEAD_SECONDS + 1.0f - when)
                                        : 0.0f;
      return std::hypot(velocity.x - desired.x, velocity.y - desired.y) + threat;
    };
    Vector2F best = desired;
    float bestCost = cost(desired);
    if (soonest(desired) <= O::LOOK_AHEAD_SECONDS)
    {
      // An assistant keeps off the pitch: he steps along or back from his
      // touchline, never in over it.
      const float inward = person.topLine ? 1.0f : -1.0f;
      const auto consider = [&](Vector2F velocity)
      {
        if (assistant && velocity.y * inward > 0.0f) return;
        const float value = cost(velocity);
        if (value < bestCost)
        {
          bestCost = value;
          best = velocity;
        }
      };
      consider({0.0f, 0.0f});
      constexpr int DIRECTIONS = 16;
      for (const float pace : O::DODGE_SPEEDS)
        for (int step = 0; step < DIRECTIONS; ++step)
        {
          const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(step) /
                              static_cast<float>(DIRECTIONS);
          consider({pace * std::cos(angle), pace * std::sin(angle)});
        }
      dodging = true;
      desired = best;
      speed = std::hypot(best.x, best.y);
    }
  }
  if (there && !dodging)
  {
    // There: he pulls up and faces the play.
    moveControlledBinturong(body, {0.0f, 0.0f}, 0.0f, topSpeed, 0.0f, dt, nullptr);
    turnToward(body, facing, dt);
    return;
  }
  const float before = body.facingAngle;
  moveControlledBinturong(body, desired, speed, topSpeed, 0.0f, dt, nullptr);
  // An assistant side-steps along his line facing the pitch, unless he
  // has to sprint.
  if (assistant && speed < MatchTuning::Officials::TOP_SPEED * 0.5f)
  {
    body.facingAngle = before;
    turnToward(body, facing, dt);
  }
}

void MatchEngine::leaveTouchline(const Player* player)
{
  if (!player) return;
  for (std::size_t index = 0; index < touchlineBodies.size(); ++index)
  {
    if (touchlineBodies[index].player != player) continue;
    const auto at = static_cast<std::ptrdiff_t>(index);
    touchlineBodies.erase(touchlineBodies.begin() + at);
    touchlinePeople.erase(touchlinePeople.begin() + at);
    if (index < previousTouchlinePositions.size())
      previousTouchlinePositions.erase(previousTouchlinePositions.begin() + at);
    if (index < previousTouchlineFacingAngles.size())
      previousTouchlineFacingAngles.erase(previousTouchlineFacingAngles.begin() + at);
    if (index < touchlineStepStart.size())
      touchlineStepStart.erase(touchlineStepStart.begin() + at);
    return;
  }
}

bool MatchEngine::onTouchline(const MatchPlayer& body) const
{
  return !touchlineBodies.empty() && &body >= touchlineBodies.data() &&
         &body < touchlineBodies.data() + touchlineBodies.size();
}

float MatchEngine::touchlineGetUpSeconds(const MatchPlayer& body) const
{
  // No ball to win: up once he has stopped, gingerly.
  using C = MatchTuning::Collision;
  return alongStatScale(body.derivedStat(DerivedStat::AGILITY),
                        C::GINGERLY_SECONDS_AT_AGILITY_1,
                        C::GINGERLY_SECONDS_AT_AGILITY_99);
}
