// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Binturong out of play: the ball leaving play and the restarts - goals
// (and who scored), the woodwork, corners, goal kicks and throw-ins, the
// ball in the net, and every restart set up and released - the version
// being developed (see match_out_of_play.h). It started as an exact copy of
// the Aardvark version (match_out_of_play_aardvark.cpp). Every function here
// is a MatchEngine member with the "Binturong" suffix, declared in
// match_out_of_play_binturong_members.h; the engine reaches them through the
// dispatchers in match_out_of_play_dispatch.cpp.
//
// Change behaviour freely here. A helper shared with the rest of the engine
// (the brains' taker and set-piece choices, the kick-off, logging, stats) is
// shared by every version: copy it into this file (with the Binturong
// suffix) before changing it. To keep this version for comparison, freeze
// it and copy it to a new version the same way.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/player.h"

using namespace MatchEngineInternal;

bool MatchEngine::ballInPlayAtBinturong(PitchPoint at) const
{
  // In play until the whole ball is over a line (Law 9); on a walled pitch
  // only a goal line (through a goal mouth) can be crossed.
  const float OVER_X = MatchTuning::Units::BALL_RADIUS_METRES;
  if (pitch.edge == PitchEdge::WALLS)
  {
    return at.x > -OVER_X && at.x < pitch.length + OVER_X;
  }
  const float OVER_Y = MatchTuning::Units::BALL_RADIUS_METRES;
  return at.x > -OVER_X && at.x < pitch.atX(1.0F) + OVER_X && at.y > -OVER_Y &&
         at.y < pitch.atY(1.0F) + OVER_Y;
}

void MatchEngine::checkOutOfBoundsBinturong()
{
  constexpr float RADIUS = MatchTuning::Units::BALL_RADIUS_METRES;
  const float HALF_GOAL = pitch.goal.halfWidth;
  const float BAR = pitch.goal.crossbar;
  const bool rightGoalLine =
      ball.position.x > pitch.atX(MatchTuning::Pitch::CENTRE);
  const float lineX = rightGoalLine ? pitch.atX(1.0F) : 0.0F;
  const float behind = rightGoalLine ? 1.0F : -1.0F;
  const std::int8_t thisGoal = rightGoalLine ? 1 : -1;

  // A throw-in from behind the touchline: in play once any of it is over
  // the line (Law 15); taken again by the same side if it turns away or
  // stops without getting there.
  if (getPossessionState().throwEntering)
  {
    if (ballInPlayAtBinturong(ball.position))
    {
      markThrowEntry(false);
    }
    else
    {
      const float inward =
          ball.position.y < pitch.atY(MatchTuning::Pitch::CENTRE)
              ? ball.velocity.y
              : -ball.velocity.y;
      if (inward <= MatchTuning::DeadBall::SETTLED_SPEED)
      {
        setupThrowInBinturong(getPossessionState().throwByHome);
      }
      return;
    }
  }

  // Its centre reaching the goal line inside the frame: into the goal
  // mouth (any nearer a post or the bar, it has met the woodwork on its
  // way: frameInPathBinturong). Back on the pitch side, it is no longer in
  // the mouth.
  if ((ball.position.x - lineX) * behind < 0.0F)
  {
    ball.inGoalMouth = 0;
  }
  else if ((substepBallPosition.x - lineX) * behind < 0.0F)
  {
    const float span = ball.position.x - substepBallPosition.x;
    const float t =
        std::abs(span) > EPSILON
            ? std::clamp((lineX - substepBallPosition.x) / span, 0.0F, 1.0F)
            : 1.0F;
    const float crossingY =
        substepBallPosition.y + ((ball.position.y - substepBallPosition.y) * t);
    const float crossingHeight = substepBallZ + ((ball.z - substepBallZ) * t);
    const float lateralMetres =
        std::abs(crossingY - pitch.atY(MatchTuning::Pitch::CENTRE));
    if (lateralMetres < HALF_GOAL - RADIUS && crossingHeight < BAR - RADIUS)
    {
      ball.inGoalMouth = thisGoal;
    }
  }

  if (ballInPlayAtBinturong(ball.position))
  {
    return;
  }

  // The whole ball over the goal line.
  if ((ball.position.x - lineX) * behind > 0.0F &&
      !ballInPlayAtBinturong(
          PitchPoint{ball.position.x, pitch.atY(MatchTuning::Pitch::CENTRE)}))
  {
    // In through the goal mouth: a goal (not straight from a throw-in).
    if (ball.inGoalMouth == thisGoal && !getPossessionState().fromThrowIn)
    {
      scoreGoalBinturong(rightGoalLine);
      return;
    }
    const bool lastTouchHome = isHomePlayer(lastTouchForRestart());
    if (!rightGoalLine)
    {
      // Home defends the left goal.
      if (lastTouchHome)
      {
        setupCornerBinturong(
            false, ball.position.y < pitch.atY(MatchTuning::Pitch::CENTRE));
      }
      else
      {
        setupGoalKickBinturong(true);
      }
    }
    else
    {
      // Away defends the right goal.
      if (!lastTouchHome && (lastTouchForRestart() != nullptr))
      {
        setupCornerBinturong(
            true, ball.position.y < pitch.atY(MatchTuning::Pitch::CENTRE));
      }
      else
      {
        setupGoalKickBinturong(false);
      }
    }
    return;
  }

  // The whole ball over a touchline.
  const bool receivingHome = !isHomePlayer(lastTouchForRestart());
  setupThrowInBinturong(receivingHome);
}

void MatchEngine::scoreGoalBinturong(bool homeTeam)
{
  MatchPlayer* last = findMatchPlayer(getLastPossessionAttachment());
  MatchPlayer* shooter = findMatchPlayer(getPossessionHistory().lastShooter);
  MatchPlayer* scorer = last;
  // A shot parried or deflected in stays the shooter's goal; only a
  // defender's own deliberate touch makes it an own goal.
  if ((shooter != nullptr) && shooter->isHomeTeam == homeTeam &&
      ((last == nullptr) || last->isHomeTeam != homeTeam))
  {
    scorer = shooter;
  }
  const bool ownGoal = (scorer != nullptr) && scorer->isHomeTeam != homeTeam;
  // The shot it came from: in flight, or off the woodwork untouched since.
  const bool fromShot =
      getPossessionState().isShot || shotOffWoodworkStandsBinturong();

  if (homeTeam)
  {
    ++homeScore;
  }
  else
  {
    ++awayScore;
  }
  slidersStale = true;
  ++stoppageLogs[static_cast<std::size_t>(period - 1)].goals;
  // Own goals from a restart still count as set-piece goals.
  if (getPossessionState().shotFromSetPiece)
  {
    ++(homeTeam ? stats.homeSetPieceGoals : stats.awaySetPieceGoals);
  }

  if (fromShot && (scorer != nullptr) && !ownGoal)
  {
    if (getPossessionState().shotIsHeader)
    {
      ++(homeTeam ? stats.homeHeadedGoals : stats.awayHeadedGoals);
    }

    if (getPossessionState().shotIsPenalty)
    {
      ++(homeTeam ? stats.homePenaltyGoals : stats.awayPenaltyGoals);
    }
  }
  if ((scorer != nullptr) && !ownGoal)
  {
    // Every goal is an attempt on target; a knock-down or a ball bundled in
    // without a recorded shot is logged as one here.
    PlayerMatchStats& entry = statsOf(*scorer);
    if (scorer != shooter)
    {
      ++entry.shots;
      ++(homeTeam ? stats.homeShots : stats.awayShots);
    }
    ++entry.shotsOnTarget;
    ++(homeTeam ? stats.homeOnTarget : stats.awayOnTarget);
  }
  PlayerID assisterId = 0;
  if ((scorer != nullptr) && !ownGoal)
  {
    ++statsOf(*scorer).goals;
    if (MatchPlayer* assister =
            findMatchPlayer(getPossessionHistory().shotAssistCandidate);
        (assister != nullptr) && assister != scorer &&
        assister->isHomeTeam == homeTeam)
    {
      ++statsOf(*assister).assists;
      assisterId = assister->player->getId();
    }
  }
  else if (scorer != nullptr)
  {
    ++statsOf(*scorer).ownGoals;
  }
  for (const auto& player : players)
  {
    if (active(player) && player.isHomeTeam != homeTeam)
    {
      ++playerStats[player.statsIndex].goalsConceded;
    }
  }

  MatchEvent& event =
      logEvent(ownGoal ? MatchEventType::OWN_GOAL : MatchEventType::GOAL);
  // The team fields describe the primary player's side, so an own goal is
  // reported for the defender's team (it counts for the other side).
  event.hasTeam = true;
  event.isHomeTeam = (scorer != nullptr) ? scorer->isHomeTeam : homeTeam;
  event.primaryPlayerId = (scorer != nullptr) ? scorer->player->getId() : 0;
  event.secondaryPlayerId = assisterId;
  event.position = (scorer != nullptr) ? scorer->position : ball.position;
  event.xg = fromShot ? getPossessionState().shotXG : 0.0F;
  if (fromShot && getPossessionState().shotFromDirectFreeKick)
  {
    ++stats.directFreeKickGoals;
  }

  goalScoredByHome = homeTeam;
  goalCelebrationRemaining = shortStoppages()
                                 ? getGoalCelebrationDuration()
                                 : MatchTuning::DeadBall::CELEBRATION_SECONDS;
  beginStoppage();
  state = MatchState::GOAL;
  clearPossession();
  noteGoalSide(homeTeam);
  pendingAdvantage.active = false;
  restartTakerIndex.reset();
  restartLineUp.reset();
  restartIsSetPiece = false;
  // Everyone walks back to the kick-off formation during the celebration.
  for (auto& player : players)
  {
    if (active(player))
    {
      player.movementTarget = kickOffPosition(player);
    }
  }
  // The ball stays in the net: after a moment the conceding keeper (anyone
  // on his side, without one) picks it out and drop-kicks it towards the
  // centre spot, where the kick-off taker collects it.
  const bool kickingOff = !homeTeam;
  const PitchPoint centre =
      pitch.at(MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE);
  setRestartTaker(chooseRestartTaker(MatchState::KICK_OFF, kickingOff, centre));
  startDeadBallBinturong(centre);
  MatchPlayer* keeper = findGoalkeeper(kickingOff);
  if ((keeper == nullptr) || !active(*keeper) || keeper->fallen)
  {
    chooseDeadBallFetcherBinturong(kickingOff);
  }
  else
  {
    deadBall.fetcherIndex = slotOf(*keeper);
    deadBall.fetcherTarget = keeper->movementTarget;
  }
  deadBall.dropKick = true;
  updateTeamPhases();
}

void MatchEngine::kickOffAfterGoalBinturong(bool homeKickingOff)
{
  // As the shared kick-off (setupKickOff), but nobody is placed: everyone
  // goes on walking (or jogging) to his kick-off position, and the kick-off
  // waits for them (kickOffReadyBinturong).
  if (state != MatchState::GOAL)
  {
    beginStoppage();
  }
  restartIsSetPiece = false;
  pendingAdvantage.active = false;
  constexpr float CLEAR_METRES = 0.5F;
  const PitchPoint centre =
      pitch.at(MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE);
  for (auto& player : players)
  {
    if (!active(player))
    {
      continue;
    }
    // His kick-off position, kept in his own half and, for the side not
    // kicking off, outside the centre circle (with a little to spare).
    PitchPoint target = kickOffPosition(player);
    const float halfway = CLEAR_METRES;
    const float centreX = pitch.atX(MatchTuning::Pitch::CENTRE);
    target.x = player.isHomeTeam ? std::min(target.x, centreX - halfway)
                                 : std::max(target.x, centreX + halfway);
    constexpr float CIRCLE =
        MatchTuning::Timing::KICK_OPPONENT_DISTANCE_METRES + CLEAR_METRES;
    if (player.isHomeTeam != homeKickingOff &&
        distance(target, centre) < CIRCLE)
    {
      const Vector2F out = metricDirection(centre, target);
      const Vector2F reach = {.x = out.x * CIRCLE, .y = out.y * CIRCLE};
      target = PitchPoint{centre.x + reach.x, centre.y + reach.y};
    }
    player.movementTarget = target;
    player.intent = PlayerIntent::HOLD_SHAPE;
    player.isPressing = false;
    player.isMakingRun = false;
    player.actionCooldown = 0.0F;
  }
  // The ball is not moved: the keeper has it on its way to the centre (or
  // the taker already has it on the spot), chosen when the goal went in.
  clearShooterHistory();
  MatchPlayer* taker = restartTaker();
  if (taker == nullptr)
  {
    taker = chooseRestartTaker(MatchState::KICK_OFF, homeKickingOff, centre);
    setRestartTaker(taker);
    if (!deadBall.active)
    {
      startDeadBallBinturong(centre);
    }
  }
  if ((taker != nullptr) && !deadBall.active)
  {
    taker->movementTarget =
        PitchPoint{pitch.atX(MatchTuning::Pitch::CENTRE) +
                       ((homeKickingOff ? -1.0F : 1.0F) *
                        MatchTuning::Pitch::KICKOFF_TAKER_BEHIND_METRES),
                   pitch.atY(MatchTuning::Pitch::CENTRE)};
  }
  state = MatchState::KICK_OFF;
  kickOffWalkIn = true;
  setPieceTimer = MatchTuning::Timing::KICKOFF_DELAY_SECONDS;
  updateTeamPhases();
}

bool MatchEngine::kickOffReadyBinturong()
{
  // Law 8: everyone in his own half, the opponents outside the centre
  // circle; the taker at the ball is lining it up.
  const MatchPlayer* taker = restartTaker();
  for (const auto& player : players)
  {
    if (!active(player) || &player == taker)
    {
      continue;
    }
    const bool ownHalf =
        player.isHomeTeam
            ? player.position.x <= pitch.atX(MatchTuning::Pitch::CENTRE)
            : player.position.x >= pitch.atX(MatchTuning::Pitch::CENTRE);
    if (!ownHalf)
    {
      return false;
    }
    if ((taker != nullptr) && player.isHomeTeam != taker->isHomeTeam &&
        distance(player.position, ball.position) <
            MatchTuning::Timing::KICK_OPPONENT_DISTANCE_METRES)
    {
      return false;
    }
  }
  return true;
}

void MatchEngine::updateBallInNetBinturong(float dt)
{
  // The ball in the net (the nets stop it) until the keeper picks it out,
  // and on its way back to the centre spot.
  updateDeadBallBinturong(dt);
}

void MatchEngine::setupThrowInBinturong(bool homeTeam)
{
  beginStoppage();
  restartIsSetPiece = false;
  // Where it went out; the ball itself goes on and is brought back
  // (startDeadBallBinturong). A Binturong body throws it from there,
  // standing behind the touchline; an Aardvark body, which stays on the
  // pitch, from just inside it.
  const bool top = ball.position.y < pitch.atY(MatchTuning::Pitch::CENTRE);
  const float outX = std::clamp(ball.position.x, 0.0F, pitch.atX(1.0F));
  MatchPlayer* taker =
      chooseRestartTaker(MatchState::THROW_IN, homeTeam,
                         PitchPoint{outX, top ? 0.0F : pitch.atY(1.0F)});
  setRestartTaker(taker);
  const float BEHIND = MatchTuning::DeadBall::THROW_IN_STAND_BEHIND_METRES;
  const PitchPoint spot =
      (taker != nullptr) && getBody(taker->isHomeTeam) == BodyId::BINTURONG
          ? PitchPoint{outX, top ? -BEHIND : pitch.atY(1.0F) + BEHIND}
          : PitchPoint{
                std::clamp(
                    ball.position.x, MatchTuning::Pitch::PLAYER_EDGE_END_METRES,
                    pitch.length - MatchTuning::Pitch::PLAYER_EDGE_END_METRES),
                top ? MatchTuning::Pitch::PLAYER_EDGE_SIDE_METRES
                    : pitch.width -
                          MatchTuning::Pitch::PLAYER_EDGE_SIDE_METRES};
  startDeadBallBinturong(spot);
  state = MatchState::THROW_IN;
  // Taken once everyone is ready (restartReadyBinturong), not on a
  // countdown; a card, an injury or a substitution still adds its wait.
  setPieceTimer = 0.0F;
  updateTeamPhases();
  MatchEvent& event = logEvent(MatchEventType::THROW_IN);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = spot;
  if ((taker != nullptr) && (taker->player != nullptr))
  {
    event.primaryPlayerId = taker->player->getId();
  }
}

void MatchEngine::setupGoalKickBinturong(bool homeTeam)
{
  beginStoppage();
  restartIsSetPiece = false;
  const PitchPoint spot = PitchPoint{
      homeTeam ? MatchTuning::DeadBall::GOAL_AREA_DEPTH_METRES
               : pitch.length - MatchTuning::DeadBall::GOAL_AREA_DEPTH_METRES,
      pitch.atY(MatchTuning::Pitch::CENTRE)};
  MatchPlayer* taker =
      chooseRestartTaker(MatchState::GOAL_KICK, homeTeam, spot);
  setRestartTaker(taker);
  startDeadBallBinturong(spot);
  state = MatchState::GOAL_KICK;
  // Taken once everyone is ready (restartReadyBinturong), not on a
  // countdown; a card, an injury or a substitution still adds its wait.
  setPieceTimer = 0.0F;
  updateTeamPhases();
  MatchEvent& event = logEvent(MatchEventType::GOAL_KICK);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = spot;
}

void MatchEngine::setupCornerBinturong(bool homeTeam, bool topCorner)
{
  beginStoppage();
  restartIsSetPiece = true;
  const PitchPoint spot = PitchPoint{
      homeTeam ? pitch.length - MatchTuning::Pitch::CORNER_SPOT_METRES
               : MatchTuning::Pitch::CORNER_SPOT_METRES,
      topCorner ? MatchTuning::Pitch::CORNER_SPOT_METRES
                : pitch.width - MatchTuning::Pitch::CORNER_SPOT_METRES};
  // Left and right follow the formation: the corner on the side of the
  // team's left-sided positions (low y) is its left corner.
  MatchPlayer* taker =
      chooseRestartTaker(MatchState::CORNER_KICK, homeTeam, spot, topCorner);
  setRestartTaker(taker);
  startDeadBallBinturong(spot);
  arrangeSetPiece(homeTeam, spot);
  state = MatchState::CORNER_KICK;
  // Taken once everyone is ready (restartReadyBinturong), not on a
  // countdown; a card, an injury or a substitution still adds its wait.
  setPieceTimer = 0.0F;
  updateTeamPhases();
  if (homeTeam)
  {
    ++stats.homeCorners;
  }
  else
  {
    ++stats.awayCorners;
  }
  MatchEvent& event = logEvent(MatchEventType::CORNER);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = spot;
  if ((taker != nullptr) && (taker->player != nullptr))
  {
    event.primaryPlayerId = taker->player->getId();
  }
}

void MatchEngine::startDeadBallBinturong(PitchPoint spot)
{
  // Nobody has it: it goes on under physics (its spin too) until it is
  // brought back to the spot.
  const BallSpin spin = ball.spin;
  clearPossession();
  clearFlightState();
  ball.spin = spin;
  ball.inGoalMouth = 0;
  deadBall = {};
  deadBall.active = true;
  deadBall.spot = spot;
  restartReadiness = {};
  restartReadiness.lastGap.fill(std::numeric_limits<float>::max());
  if (MatchPlayer* taker = restartTaker())
  {
    taker->movementTarget = spot;
  }
}

namespace
{
/** The stadium round the pitch, in metres from the left goal line (x) and
 * the top touchline (y). */
struct Ground
{
  static constexpr float BOARD_X = MatchTuning::Pitch::BOARDS_END_METRES;
  static constexpr float BOARD_Y = MatchTuning::Pitch::BOARDS_SIDE_METRES;
  static constexpr float WALL_X = MatchTuning::Pitch::STAND_END_METRES;
  static constexpr float WALL_Y = MatchTuning::Pitch::STAND_SIDE_METRES;

  /** How far beyond the rectangle `outX` / `outY` out from the pitch a
   * point is (the further of the two ways out); 0 or less inside it. */
  static float beyond(const MatchPitch& pitch, Vector2F at, float outX,
                      float outY)
  {
    return std::max({-outX - at.x, at.x - (pitch.length + outX), -outY - at.y,
                     at.y - (pitch.width + outY)});
  }
  /** The seating's height this far into the stand (the lower tier). */
  static float seating(float depth)
  {
    using P = MatchTuning::Pitch;
    return P::STAND_WALL_HEIGHT_METRES +
           (std::max(0.0F, depth) *
            (P::LOWER_TIER_TOP_METRES - P::STAND_WALL_HEIGHT_METRES) /
            P::LOWER_TIER_DEPTH_METRES);
  }
};

/** A ball moving from `from` to `to` (metres) through an upright face at
 * `plane` on one axis (x when `alongX`), standing between `low` and `high`
 * on the other and up to `top` high: bounced back off it, keeping
 * `restitution` of its speed into it, its centre a radius clear of the
 * face. Returns whether it bounced. */
bool bounceOffFace(bool alongX, float plane, float low, float high, float top,
                   float restitution, Vector2F from, Vector2F& to,
                   float fromHeight, float toHeight, Vector2F& velocity)
{
  constexpr float RADIUS = MatchTuning::Units::BALL_RADIUS_METRES;
  float& toAxis = alongX ? to.x : to.y;
  float& speedAxis = alongX ? velocity.x : velocity.y;
  const float fromAxis = alongX ? from.x : from.y;
  const float side = fromAxis < plane ? -1.0F : 1.0F;
  const float face = plane + (side * RADIUS);
  // Still on its side of the face.
  if ((toAxis - face) * side >= 0.0F)
  {
    return false;
  }
  const float span = toAxis - fromAxis;
  const float t = std::abs(span) > 1e-6F
                      ? std::clamp((face - fromAxis) / span, 0.0F, 1.0F)
                      : 0.0F;
  const float across =
      alongX ? from.y + ((to.y - from.y) * t) : from.x + ((to.x - from.x) * t);
  const float height = fromHeight + ((toHeight - fromHeight) * t);
  if (across < low || across > high || height >= top)
  {
    return false;
  }
  // Back off the face by what it had left, at the speed it keeps.
  toAxis = face + (face - toAxis) * restitution;
  speedAxis = -speedAxis * restitution;
  return true;
}

/**
 * Where a player at `from` heads first to get to `to` (metres) without
 * walking into a goal frame (MatchEngineInternal::keepOutOfGoalFrames): out
 * through the goal mouth if he is in the goal, else round the outside of
 * the frame by its nearest corner when the straight line crosses a net.
 */
PitchPoint routeRoundGoalFrames(const MatchPitch& pitch, PitchPoint from,
                                PitchPoint to)
{
  using P = MatchTuning::Pitch;
  const float W = pitch.goal.halfWidth + (0.5F * pitch.goal.post);
  const float DEPTH = pitch.goal.netDepth;
  // Clear of the frame by his footprint and a little more.
  constexpr float CLEAR = MatchTuning::Body::RADIUS_METRES + 0.4F;
  const bool right = 0.5F * (from.x + to.x) > pitch.atX(P::CENTRE);
  const float line = right ? pitch.length : 0.0F;
  const float behind = right ? 1.0F : -1.0F;
  // Local metres at that goal: u behind its line, v across from its centre.
  const auto local = [&](PitchPoint at)
  {
    return Vector2F{.x = (at.x - line) * behind,
                    .y = at.y - pitch.atY(P::CENTRE)};
  };
  const auto onPitch = [&](Vector2F at)
  { return PitchPoint{line + (at.x * behind), pitch.atY(P::CENTRE) + at.y}; };
  const Vector2F a = local(from);
  const Vector2F b = local(to);
  const auto inside = [=](Vector2F at)
  { return at.x > 0.0F && at.x < DEPTH && std::abs(at.y) < W; };
  // Whether the line from p to q crosses a side net or the back net (each
  // grown by his footprint at its ends).
  const auto crossesNet = [=](Vector2F p, Vector2F q)
  {
    constexpr float R = MatchTuning::Body::RADIUS_METRES;
    const auto crossesAt = [&](bool alongU, float wall, float low, float high)
    {
      const float pa = alongU ? p.x : p.y;
      const float qa = alongU ? q.x : q.y;
      if ((pa - wall) * (qa - wall) > 0.0F || pa == qa)
      {
        return false;
      }
      const float t = (wall - pa) / (qa - pa);
      const float across =
          alongU ? p.y + ((q.y - p.y) * t) : p.x + ((q.x - p.x) * t);
      return across >= low - R && across <= high + R;
    };
    return crossesAt(false, -W, 0.0F, DEPTH) ||
           crossesAt(false, W, 0.0F, DEPTH) || crossesAt(true, DEPTH, -W, W);
  };
  // In the goal: out through its mouth first.
  if (inside(a) && !inside(b) && crossesNet(a, b))
  {
    return onPitch(
        {.x = -CLEAR, .y = std::clamp(a.y, -(W - CLEAR), W - CLEAR)});
  }
  // Going into the goal from outside: to its mouth first.
  if (!inside(a) && inside(b) && crossesNet(a, b))
  {
    return onPitch(
        {.x = -CLEAR, .y = std::clamp(b.y, -(W - CLEAR), W - CLEAR)});
  }
  if (!crossesNet(a, b))
  {
    return to;
  }
  // Round the outside: by the frame's corners, the next one he can walk
  // straight to (not the one he is on), on the shortest way round,
  // preferring one from which the rest of the way is clear.
  constexpr float AT_CORNER = 0.3F;
  constexpr float STILL_BLOCKED = 50.0F;
  PitchPoint best = to;
  float bestMetres = 1.0e9F;
  for (const float u : {-CLEAR, DEPTH + CLEAR})
  {
    for (const float v : {-(W + CLEAR), W + CLEAR})
    {
      const Vector2F corner{.x = u, .y = v};
      const float toCorner = std::hypot(corner.x - a.x, corner.y - a.y);
      if (toCorner < AT_CORNER || crossesNet(a, corner))
      {
        continue;
      }
      const float metres = toCorner +
                           std::hypot(b.x - corner.x, b.y - corner.y) +
                           (crossesNet(corner, b) ? STILL_BLOCKED : 0.0F);
      if (metres < bestMetres)
      {
        bestMetres = metres;
        best = onPitch(corner);
      }
    }
  }
  return best;
}

// A throw-in: where he holds it over his head, in front of him as he stands
// (on his spot if he is not near it), never closer to the touchline than
// it would be from his spot.
PitchPoint throwInHold(const MatchPitch& pitch, PitchPoint thrower,
                       PitchPoint spot)
{
  using D = MatchTuning::DeadBall;
  using P = MatchTuning::Pitch;
  constexpr float NEAR_METRES = 1.0F;
  const float FORWARD = D::THROW_IN_FORWARD_METRES;
  const float CLEAR =
      D::THROW_IN_STAND_BEHIND_METRES - D::THROW_IN_FORWARD_METRES;
  const PitchPoint from =
      distance(thrower, spot) <= NEAR_METRES ? thrower : spot;
  return spot.y < pitch.atY(P::CENTRE)
             ? PitchPoint{from.x, std::min(from.y + FORWARD, -CLEAR)}
             : PitchPoint{from.x,
                          std::max(from.y - FORWARD, pitch.atY(1.0F) + CLEAR)};
}

// The woodwork. A ball's centre in metres: along the pitch, across it and
// up.
struct Point3
{
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

Point3 lerp(Point3 a, Point3 b, float t)
{
  return {.x = a.x + ((b.x - a.x) * t),
          .y = a.y + ((b.y - a.y) * t),
          .z = a.z + ((b.z - a.z) * t)};
}

// The ball touches the frame with its centre this close to a post's or the
// bar's centre line.
float frameReach(const MatchPitch& pitch)
{
  return (0.5F * pitch.goal.post) + MatchTuning::Units::BALL_RADIUS_METRES;
}

float apartMetres(Point3 a, Point3 b)
{
  return std::sqrt(((a.x - b.x) * (a.x - b.x)) + ((a.y - b.y) * (a.y - b.y)) +
                   ((a.z - b.z) * (a.z - b.z)));
}

// The nearest point of each part of the frame of the goal on the ball's side
// of halfway: each post a line from the ground up to the bar's centre, the
// bar a line between the posts' centres (round, the inside edges on the goal
// mouth). Where they join, the outside is a rounded elbow and the inside a
// corner.
std::array<Point3, 3> nearestOnFrameParts(const MatchPitch& pitch, Point3 at)
{
  using P = MatchTuning::Pitch;
  const float HALF = pitch.goal.halfWidth + (0.5F * pitch.goal.post);
  const float BAR = pitch.goal.crossbar + (0.5F * pitch.goal.post);
  const float MIDDLE = 0.5F * pitch.width;
  const float line = at.x > 0.5F * pitch.length ? pitch.length : 0.0F;
  const Point3 segments[3][2] = {{{.x = line, .y = MIDDLE - HALF, .z = 0.0F},
                                  {.x = line, .y = MIDDLE - HALF, .z = BAR}},
                                 {{.x = line, .y = MIDDLE + HALF, .z = 0.0F},
                                  {.x = line, .y = MIDDLE + HALF, .z = BAR}},
                                 {{.x = line, .y = MIDDLE - HALF, .z = BAR},
                                  {.x = line, .y = MIDDLE + HALF, .z = BAR}}};
  std::array<Point3, 3> nearest{};
  for (std::size_t part = 0; part < nearest.size(); ++part)
  {
    const Point3 a = segments[part][0];
    const Point3 b = segments[part][1];
    const Point3 ab{.x = b.x - a.x, .y = b.y - a.y, .z = b.z - a.z};
    const float span = (ab.x * ab.x) + (ab.y * ab.y) + (ab.z * ab.z);
    const float t = std::clamp(
        ((at.x - a.x) * ab.x + (at.y - a.y) * ab.y + (at.z - a.z) * ab.z) /
            span,
        0.0F, 1.0F);
    nearest[part] = lerp(a, b, t);
  }
  return nearest;
}

// How far the ball's centre is from the nearest part of the frame.
float frameGap(const MatchPitch& pitch, Point3 at)
{
  float gap = std::numeric_limits<float>::max();
  for (const Point3& point : nearestOnFrameParts(pitch, at))
  {
    gap = std::min(gap, apartMetres(at, point));
  }
  return gap;
}

// How far along the straight path from `from` to `to` the ball first meets
// the frame (coming into it), if it does: the path stepped through 1 cm at a
// time, the touch then found to a fraction of a millimetre by halving, so a
// faint glance is met where it happens.
std::optional<float> frameContactShare(const MatchPitch& pitch, Point3 from,
                                       Point3 to)
{
  constexpr float STEP_METRES = 0.01F;
  constexpr int HALVINGS = 14;
  const float path = std::sqrt(((to.x - from.x) * (to.x - from.x)) +
                               ((to.y - from.y) * (to.y - from.y)) +
                               ((to.z - from.z) * (to.z - from.z)));
  const float start = frameGap(pitch, from);
  // Nowhere near it.
  if (start - path > frameReach(pitch))
  {
    return std::nullopt;
  }
  // Already touching (a little into it): met now, if going further in.
  if (start <= frameReach(pitch))
  {
    if (path <= EPSILON)
    {
      return std::nullopt;
    }
    const float probe = std::min(1.0F, 0.001F / path);
    return frameGap(pitch, lerp(from, to, probe)) < start
               ? std::optional<float>{0.0F}
               : std::nullopt;
  }
  const int steps =
      std::max(1, static_cast<int>(std::ceil(path / STEP_METRES)));
  float before = 0.0F;
  for (int step = 1; step <= steps; ++step)
  {
    const float after = static_cast<float>(step) / static_cast<float>(steps);
    if (frameGap(pitch, lerp(from, to, after)) > frameReach(pitch))
    {
      before = after;
      continue;
    }
    float low = before;
    float high = after;
    for (int halving = 0; halving < HALVINGS; ++halving)
    {
      const float middle = 0.5F * (low + high);
      if (frameGap(pitch, lerp(from, to, middle)) > frameReach(pitch))
      {
        low = middle;
      }
      else
      {
        high = middle;
      }
    }
    return low;
  }
  return std::nullopt;
}

// Off the frame where the ball is: a mirror bounce off the round surface of
// each part it is touching and coming into (along it kept, into it reversed
// and this share kept); in the inside corner, off the post and the bar
// both. False if it is coming into none.
bool bounceOffFrameAt(const MatchPitch& pitch, Point3 at, Vector2F& velocity,
                      float& velocityZ, float restitution)
{
  // Touching: within its reach, to half a millimetre (the touch is found
  // just short of it).
  const float TOUCHING = frameReach(pitch) + 0.0005F;
  bool bounced = false;
  for (const Point3& near : nearestOnFrameParts(pitch, at))
  {
    const float apart = apartMetres(at, near);
    if (apart > TOUCHING || apart <= EPSILON)
    {
      continue;
    }
    const Point3 normal{.x = (at.x - near.x) / apart,
                        .y = (at.y - near.y) / apart,
                        .z = (at.z - near.z) / apart};
    const float into = (velocity.x * normal.x) + (velocity.y * normal.y) +
                       (velocityZ * normal.z);
    if (into >= 0.0F)
    {
      continue;
    }
    const float change = (1.0F + restitution) * into;
    velocity = {.x = velocity.x - (change * normal.x),
                .y = velocity.y - (change * normal.y)};
    velocityZ -= change * normal.z;
    bounced = true;
  }
  return bounced;
}
}  // namespace

std::optional<float> MatchEngine::frameInPathBinturong(float seconds) const
{
  // A free ball in play, on its way over the next `seconds` (straight: the
  // stretch is short). A pitch without goals has no frames.
  if (!pitch.goal.present())
  {
    return std::nullopt;
  }
  if (state != MatchState::PLAYING ||
      ((getPossessionAttachment() != nullptr) && !hasTouchClaim()))
  {
    return std::nullopt;
  }
  const Vector2F at = asVector(ball.position);
  const Point3 from{.x = at.x, .y = at.y, .z = ball.z};
  const Point3 to{.x = from.x + (ball.velocity.x * seconds),
                  .y = from.y + (ball.velocity.y * seconds),
                  .z = from.z + (ball.velocityZ * seconds)};
  return frameContactShare(pitch, from, to);
}

void MatchEngine::bounceOffFrameBinturong()
{
  // Off the post or the bar where it meets it. A shot that hits it is a
  // shot no longer (in off it, a goal as any other).
  const Vector2F at = asVector(ball.position);
  if (!bounceOffFrameAt(pitch, {.x = at.x, .y = at.y, .z = ball.z},
                        ball.velocity, ball.velocityZ,
                        MatchTuning::DeadBall::POST_RESTITUTION))
  {
    return;
  }
  // Still the shot's, off the post and then the bar too.
  const bool shot =
      getPossessionState().isShot || shotOffWoodworkStandsBinturong();
  noteWoodworkContact(shot);
  if (!getPossessionState().isShot)
  {
    return;
  }
  MatchPlayer* shooter = findMatchPlayer(getPossessionHistory().lastShooter);
  MatchEvent& event = logEvent(MatchEventType::WOODWORK);
  if (shooter != nullptr)
  {
    event.hasTeam = true;
    event.isHomeTeam = shooter->isHomeTeam;
    event.primaryPlayerId = shooter->player->getId();
  }
  // Still going in, it is still a shot (the keeper reads it afresh);
  // otherwise a loose ball (shot block plan step 4).
  if (!shotStaysOnGoalBinturong())
  {
    endDeflectedShotFlight();
  }
}

bool MatchEngine::shotOffWoodworkStandsBinturong() const
{
  // Nobody has touched it since it came off the woodwork: its path is the
  // one it went on with, and the same players had touched it.
  return getPossessionState().shotOffWoodwork &&
         getPossessionState().pathSince ==
             getPossessionState().woodworkPathSince &&
         getLastPossessionAttachment() ==
             getPossessionState().woodworkLastPossessor &&
         getPossessionState().deflectedBy ==
             getPossessionState().woodworkDeflectedBy;
}

void MatchEngine::moveDeadBallBinturong(float dt)
{
  using D = MatchTuning::DeadBall;
  using P = MatchTuning::Pitch;
  using G = Ground;
  constexpr int SUBSTEPS = MatchTuning::Timing::BALL_SUBSTEPS;
  for (int sub = 0; sub < SUBSTEPS; ++sub)
  {
    const Vector2F from{.x = ball.position.x, .y = ball.position.y};
    const float fromHeight = ball.z;
    const float seconds = dt / static_cast<float>(SUBSTEPS);
    integrateBall(seconds);
    // The woodwork on its way: off it where it meets it, then on through
    // the rest of the sub-step.
    {
      const Point3 start{.x = from.x, .y = from.y, .z = fromHeight};
      const Point3 end{.x = ball.position.x, .y = ball.position.y, .z = ball.z};
      if (const std::optional<float> share =
              frameContactShare(pitch, start, end))
      {
        const Point3 at = lerp(start, end, *share);
        if (bounceOffFrameAt(pitch, at, ball.velocity, ball.velocityZ,
                             D::POST_RESTITUTION))
        {
          const float rest = (1.0F - *share) * seconds;
          ball.position = PitchPoint{at.x + (ball.velocity.x * rest),
                                     at.y + (ball.velocity.y * rest)};
          ball.z = std::max(0.0F, at.z + (ball.velocityZ * rest));
        }
      }
    }
    // A training dummy on its way (training ground plan section 21): off
    // it the same way.
    if (!kit.dummies.empty())
    {
      const PitchPoint start{from.x, from.y};
      if (const auto hit = dummyAlong(start, fromHeight, ball.position, ball.z))
      {
        const PitchPoint end = ball.position;
        const float endZ = ball.z;
        const float share = hit->second;
        ball.position = PitchPoint{start.x + ((end.x - start.x) * share),
                                   start.y + ((end.y - start.y) * share)};
        ball.z = fromHeight + (endZ - fromHeight) * share;
        if (reflectOffDummy(hit->first))
        {
          const float rest = (1.0F - share) * seconds;
          ball.position =
              PitchPoint{ball.position.x + (ball.velocity.x * rest),
                         ball.position.y + (ball.velocity.y * rest)};
          ball.z = std::max(0.0F, ball.z + (ball.velocityZ * rest));
        }
        else
        {
          ball.position = end;
          ball.z = endZ;
        }
      }
    }
    Vector2F to{.x = ball.position.x, .y = ball.position.y};
    const float height = ball.z;
    Vector2F velocity = ball.velocity;

    // The advertising boards and the stand's front wall, from either side
    // (along the bottom, open at the technical areas and the tunnel).
    for (const bool alongX : {true, false})
    {
      const float extent = alongX ? pitch.length : pitch.width;
      const float otherExtent = alongX ? pitch.width : pitch.length;
      const float boardOut = alongX ? G::BOARD_X : G::BOARD_Y;
      const float boardSpan = alongX ? G::BOARD_Y : G::BOARD_X;
      const float wallOut = alongX ? G::WALL_X : G::WALL_Y;
      const float wallSpan = alongX ? G::WALL_Y : G::WALL_X;
      for (const float plane : {-boardOut, extent + boardOut})
      {
        if (!alongX && plane > 0.0F)
        {
          for (const MatchGround::Span& span : pitch.ground.bottomBoards)
          {
            bounceOffFace(false, plane, span.from, span.to,
                          P::BOARDS_HEIGHT_METRES, D::BOARD_RESTITUTION, from,
                          to, fromHeight, height, velocity);
          }
          continue;
        }
        bounceOffFace(alongX, plane, -boardSpan, otherExtent + boardSpan,
                      P::BOARDS_HEIGHT_METRES, D::BOARD_RESTITUTION, from, to,
                      fromHeight, height, velocity);
      }
      for (const float plane : {-wallOut, extent + wallOut})
      {
        if (!alongX && plane > 0.0F)
        {
          for (const MatchGround::Span& span : pitch.ground.bottomStandWall)
          {
            bounceOffFace(false, plane, span.from, span.to,
                          P::STAND_WALL_HEIGHT_METRES, D::WALL_RESTITUTION,
                          from, to, fromHeight, height, velocity);
          }
          continue;
        }
        bounceOffFace(alongX, plane, -wallSpan, otherExtent + wallSpan,
                      P::STAND_WALL_HEIGHT_METRES, D::WALL_RESTITUTION, from,
                      to, fromHeight, height, velocity);
      }
    }

    // The dugouts: to the ball, a closed box under the roof (the front's
    // perspex, the people inside and the open ends between them). Off the
    // front and the ends; onto the roof from above, rolling down it and off
    // the front.
    for (const MatchGround::Rect& dugout : pitch.ground.dugouts)
    {
      bounceOffFace(false, dugout.y0, dugout.x0, dugout.x1,
                    P::DUGOUT_ROOF_FRONT_METRES, D::DUGOUT_RESTITUTION, from,
                    to, fromHeight, height, velocity);
      for (const float end : {dugout.x0, dugout.x1})
      {
        bounceOffFace(true, end, dugout.y0, dugout.y1,
                      MatchGround::dugoutRoofHeight(dugout, to.y),
                      D::DUGOUT_RESTITUTION, from, to, fromHeight, height,
                      velocity);
      }
      constexpr float RADIUS = MatchTuning::Units::BALL_RADIUS_METRES;
      if (!dugout.contains(to.x, to.y))
      {
        continue;
      }
      const float surface =
          MatchGround::dugoutRoofHeight(dugout, to.y) + RADIUS;
      if (height >= surface)
      {
        continue;
      }
      constexpr float GRAVITY = 9.81F;
      constexpr float SLOPE =
          (P::DUGOUT_ROOF_BACK_METRES - P::DUGOUT_ROOF_FRONT_METRES) /
          P::DUGOUT_DEPTH_METRES;
      // On the roof: off it as it lands, resting on it once it is slow,
      // and pulled down the slope towards the pitch.
      ball.velocityZ = ball.velocityZ < -D::SETTLED_SPEED
                           ? -ball.velocityZ * D::DUGOUT_RESTITUTION
                           : 0.0F;
      ball.z = surface + (surface - height) * D::DUGOUT_RESTITUTION;
      velocity.y -=
          GRAVITY * SLOPE / std::sqrt(1.0F + (SLOPE * SLOPE)) * seconds;
    }

    // The tunnel: its sides and far end, and its ceiling from below.
    const bool inTunnel = pitch.ground.tunnel.contains(to.x, to.y) &&
                          height < P::TUNNEL_HEIGHT_METRES;
    if (inTunnel || pitch.ground.tunnel.contains(from.x, from.y))
    {
      constexpr float RADIUS = MatchTuning::Units::BALL_RADIUS_METRES;
      const MatchGround::Rect& tunnel = pitch.ground.tunnel;
      for (const float side : {tunnel.x0, tunnel.x1})
      {
        bounceOffFace(true, side, tunnel.y0, tunnel.y1, P::TUNNEL_HEIGHT_METRES,
                      D::TUNNEL_RESTITUTION, from, to, fromHeight, height,
                      velocity);
      }
      bounceOffFace(false, tunnel.y1, tunnel.x0, tunnel.x1,
                    P::TUNNEL_HEIGHT_METRES, D::TUNNEL_RESTITUTION, from, to,
                    fromHeight, height, velocity);
      const float ceiling = P::TUNNEL_HEIGHT_METRES - RADIUS;
      if (fromHeight < P::TUNNEL_HEIGHT_METRES && height > ceiling &&
          ball.velocityZ > 0.0F)
      {
        ball.z = ceiling - (height - ceiling) * D::TUNNEL_RESTITUTION;
        ball.velocityZ = -ball.velocityZ * D::TUNNEL_RESTITUTION;
      }
    }

    // The goal frames from outside: a side net (its post at the front), and
    // the back net sloping from the roof down to the ground.
    {
      const float W = pitch.goal.halfWidth + (0.5F * pitch.goal.post);
      const float DEPTH = pitch.goal.netDepth;
      const float ROOF = pitch.goal.crossbar - 0.25F;
      const bool right = to.x > 0.5F * pitch.length;
      const float line = right ? pitch.length : 0.0F;
      const float behind = right ? 1.0F : -1.0F;
      const float u0 = (from.x - line) * behind;
      const float u1 = (to.x - line) * behind;
      const float v0 = from.y - (0.5F * pitch.width);
      const float v1 = to.y - (0.5F * pitch.width);
      // The net's top this far behind the line (the 3D view's shape).
      const auto netTop = [&](float u)
      {
        return u <= 1.0F ? pitch.goal.crossbar - (0.25F * u)
                         : ROOF * std::max(0.0F, DEPTH - u);
      };
      const bool outside = u0 < 0.0F || u0 > DEPTH || std::abs(v0) > W;
      if (outside && u1 >= 0.0F && u1 <= DEPTH + 0.5F)
      {
        // A side net, from beside the goal (its post is the woodwork's).
        if (std::abs(v0) > W)
        {
          const float side = v0 > 0.0F ? W : -W;
          bounceOffFace(false, (0.5F * pitch.width) + side,
                        std::min(line, line + (behind * DEPTH)),
                        std::max(line, line + (behind * DEPTH)),
                        netTop(std::clamp(u1, 0.0F, DEPTH)), D::NET_RESTITUTION,
                        from, to, fromHeight, height, velocity);
        }
        // The back net, from behind it: where it stands at the ball's height.
        else if (std::abs(v1) < W && height < ROOF)
        {
          const float backU = std::max(1.0F, DEPTH - (height / ROOF));
          if (u0 > backU)
          {
            bounceOffFace(true, line + (behind * backU),
                          (0.5F * pitch.width) - W, (0.5F * pitch.width) + W,
                          ROOF, D::NET_RESTITUTION, from, to, fromHeight,
                          height, velocity);
          }
        }
      }
      else if (!outside)
      {
        // In the goal: the side nets, the back net and the roof keep it in
        // (the front, under the bar, is open).
        constexpr float RADIUS = MatchTuning::Units::BALL_RADIUS_METRES;
        for (const float side : {-W, W})
        {
          bounceOffFace(false, (0.5F * pitch.width) + side,
                        std::min(line, line + (behind * DEPTH)),
                        std::max(line, line + (behind * DEPTH)),
                        netTop(std::clamp(u1, 0.0F, DEPTH)) + RADIUS,
                        D::NET_RESTITUTION, from, to, fromHeight, height,
                        velocity);
        }
        const float backU =
            std::max(1.0F, DEPTH - (std::max(0.0F, height) / ROOF));
        bounceOffFace(true, line + (behind * backU), (0.5F * pitch.width) - W,
                      (0.5F * pitch.width) + W, ROOF + RADIUS,
                      D::NET_RESTITUTION, from, to, fromHeight, height,
                      velocity);
        const float u = (to.x - line) * behind;
        const float roof = netTop(std::clamp(u, 0.0F, 1.0F)) - RADIUS;
        if (u >= 0.0F && u <= 1.0F && height > roof && ball.velocityZ > 0.0F)
        {
          ball.z = roof - (height - roof) * D::NET_RESTITUTION;
          ball.velocityZ = -ball.velocityZ * D::NET_RESTITUTION;
        }
      }
    }

    ball.position = PitchPoint{to.x, to.y};
    ball.velocity = velocity;
    // A walled pitch: its walls hold the dead ball too (the boards and the
    // stand are further out, so it never reaches them).
    if (pitch.edge == PitchEdge::WALLS)
    {
      bounceOffWalls();
    }

    // Into the stand: a fan in the rows shown catches it within his reach;
    // beyond those rows it has gone out of sight. Not down the tunnel,
    // under the seats: a steward fetches it from there.
    const float depth = G::beyond(pitch, to, G::WALL_X, G::WALL_Y);
    if (depth <= 0.0F || (pitch.ground.tunnel.contains(to.x, to.y) &&
                          ball.z < P::TUNNEL_HEIGHT_METRES))
    {
      continue;
    }
    if (depth > P::VISIBLE_CROWD_METRES)
    {
      // A replacement will come back from the front row where it went.
      constexpr float FRONT_ROW = 0.5F;
      Vector2F front = to;
      front.x = std::clamp(front.x, -G::WALL_X - FRONT_ROW,
                           pitch.length + G::WALL_X + FRONT_ROW);
      front.y = std::clamp(front.y, -G::WALL_Y - FRONT_ROW,
                           pitch.width + G::WALL_Y + FRONT_ROW);
      deadBall.returnFrom = PitchPoint{front.x, front.y};
      deadBall.phase = DeadBallReturn::Phase::OUT_OF_SIGHT;
      deadBall.seconds = 0.0F;
      return;
    }
    if (height <= G::seating(depth) + D::FAN_REACH_METRES)
    {
      ball.velocity = {.x = 0.0F, .y = 0.0F};
      ball.velocityZ = 0.0F;
      ball.spin = {};
      deadBall.phase = DeadBallReturn::Phase::CAUGHT;
      deadBall.seconds = 0.0F;
      return;
    }
  }
}

void MatchEngine::throwDeadBallBinturong(float releaseMetres)
{
  // From where it is, released this high, at the spot (give or take): the
  // lower of the two arcs that land there, or as far as a throw goes.
  using D = MatchTuning::DeadBall;
  constexpr float GRAVITY = 9.81F;
  const float errorAngle = randomFloat(0.0F, 6.2831853F);
  const float errorMetres = randomFloat(0.0F, D::THROW_ERROR_METRES);
  const Vector2F aim = {.x = std::cos(errorAngle) * errorMetres,
                        .y = std::sin(errorAngle) * errorMetres};
  const PitchPoint target{deadBall.spot.x + aim.x, deadBall.spot.y + aim.y};
  const Vector2F way = metricDirection(ball.position, target);
  const float metres = distance(ball.position, target);
  const float v = D::THROW_SPEED;
  const float root =
      (v * v * v * v) -
      (GRAVITY * (GRAVITY * metres * metres - 2.0F * releaseMetres * v * v));
  const float angle =
      root >= 0.0F && metres > 0.01F
          ? std::atan((v * v - std::sqrt(root)) / (GRAVITY * metres))
          : 0.7853982F;
  ball.z = releaseMetres;
  ball.velocity = {.x = way.x * v * std::cos(angle),
                   .y = way.y * v * std::cos(angle)};
  ball.velocityZ = v * std::sin(angle);
  ball.spin = {};
  deadBall.phase = DeadBallReturn::Phase::LOOSE;
  deadBall.seconds = 0.0F;
  deadBall.gapSeconds = 0.0F;
}

void MatchEngine::dropKickDeadBallBinturong(MatchPlayer& keeper)
{
  // Out of his hands and volleyed at the spot (give or take), on a high arc
  // (its range on flat ground at that pace and angle), then back to his
  // place; the kick-off taker collects it.
  using D = MatchTuning::DeadBall;
  constexpr float GRAVITY = 9.81F;
  const float errorAngle = randomFloat(0.0F, 6.2831853F);
  const float errorMetres = randomFloat(0.0F, D::DROP_KICK_ERROR_METRES);
  const Vector2F aim = {.x = std::cos(errorAngle) * errorMetres,
                        .y = std::sin(errorAngle) * errorMetres};
  const PitchPoint target{deadBall.spot.x + aim.x, deadBall.spot.y + aim.y};
  const Vector2F way = metricDirection(keeper.position, target);
  const float angle = D::DROP_KICK_ANGLE_DEGREES * 3.14159265F / 180.0F;
  const float pace = std::sqrt(GRAVITY * distance(keeper.position, target) /
                               std::sin(2.0F * angle));
  ball.position = keeper.position;
  ball.z = D::DROP_KICK_RELEASE_METRES;
  ball.velocity = {.x = way.x * pace * std::cos(angle),
                   .y = way.y * pace * std::cos(angle)};
  ball.velocityZ = pace * std::sin(angle);
  ball.spin = {};
  keeper.movementTarget = deadBall.fetcherTarget;
  deadBall.fetcherIndex.reset();
  deadBall.passedToSpot = true;
  deadBall.phase = DeadBallReturn::Phase::LOOSE;
  deadBall.seconds = 0.0F;
}

void MatchEngine::placeDeadBallBinturong(MatchPlayer* taker)
{
  // Put down on the spot: the restart is ready to be taken.
  ball.position = deadBall.spot;
  ball.z = 0.0F;
  ball.velocity = {.x = 0.0F, .y = 0.0F};
  ball.velocityZ = 0.0F;
  ball.spin = {};
  // A throw-in from behind the touchline: he holds it over his head, still
  // behind the line, ready to throw.
  if (state == MatchState::THROW_IN && (taker != nullptr) &&
      !ballInPlayAtBinturong(deadBall.spot))
  {
    ball.position = throwInHold(pitch, taker->position, deadBall.spot);
    ball.z =
        taker->heightMetres + MatchTuning::DeadBall::THROW_IN_ABOVE_HEAD_METRES;
  }
  deadBall.active = false;
  assignRestartOwner((taker != nullptr) ? taker->player : nullptr);
  if (taker != nullptr)
  {
    taker->movementTarget = deadBall.spot;
  }
}

void MatchEngine::updateDeadBallBinturong(float dt)
{
  // Back on the spot: everyone gets ready.
  if (!deadBall.active)
  {
    updateRestartReadinessBinturong(dt);
    return;
  }
  using D = MatchTuning::DeadBall;
  using P = MatchTuning::Pitch;
  using Phase = DeadBallReturn::Phase;
  deadBall.seconds += dt;
  deadBall.totalSeconds += dt;
  MatchPlayer* taker = restartTaker();
  if ((taker != nullptr) && !active(*taker))
  {
    taker = nullptr;
  }
  // Safety net (and nobody to fetch it): it is put on the spot.
  if (deadBall.totalSeconds >= D::GIVE_UP_SECONDS || (taker == nullptr))
  {
    placeDeadBallBinturong(taker);
    return;
  }
  switch (deadBall.phase)
  {
    case Phase::CAUGHT:
      if (deadBall.seconds >= D::CATCH_THROW_SECONDS)
      {
        const Vector2F at{.x = ball.position.x, .y = ball.position.y};
        const float depth =
            Ground::beyond(pitch, at, Ground::WALL_X, Ground::WALL_Y);
        throwDeadBallBinturong(Ground::seating(depth) +
                               D::THROW_RELEASE_METRES);
      }
      break;
    case Phase::OUT_OF_SIGHT:
      if (deadBall.seconds >= D::OUT_OF_SIGHT_SECONDS)
      {
        // A replacement from the front row (a new ball, not this one).
        ball.position = deadBall.returnFrom;
        throwDeadBallBinturong(Ground::seating(0.5F) + D::THROW_RELEASE_METRES);
        previousBallPosition = ball.position;
        previousBallZ = ball.z;
      }
      break;
    case Phase::CARRIED:
    {
      // In his hands: the keeper's on his way out to his six-yard line to
      // drop-kick it, anyone else's on his way to the spot, put down once he
      // is there.
      MatchPlayer* keeper = deadBall.dropKick && deadBall.fetcherIndex
                                ? &players[*deadBall.fetcherIndex]
                                : nullptr;
      MatchPlayer& carrier = (keeper != nullptr) ? *keeper : *taker;
      ball.position = carrier.position;
      ball.z = D::CARRY_HEIGHT_METRES;
      ball.velocity = carrier.velocity;
      ball.velocityZ = 0.0F;
      if (keeper != nullptr)
      {
        if (keeper != taker)
        {
          taker->movementTarget = deadBall.spot;
        }
        const bool right = keeper->position.x > pitch.atX(P::CENTRE);
        const float OUT = D::GOAL_AREA_DEPTH_METRES;
        const float ACROSS = P::GOAL_AREA_HALF_WIDTH_METRES - 1.0F;
        const float middle = pitch.atY(P::CENTRE);
        const PitchPoint kickFrom{
            right ? pitch.atX(1.0F) - OUT : OUT,
            std::clamp(keeper->position.y, middle - ACROSS, middle + ACROSS)};
        keeper->movementTarget =
            routeRoundGoalFrames(pitch, keeper->position, kickFrom);
        if (distance(keeper->position, kickFrom) <=
            MatchTuning::Player::RESTART_ARRIVED_METRES)
        {
          dropKickDeadBallBinturong(*keeper);
        }
        break;
      }
      taker->movementTarget = deadBall.spot;
      const float gap = distance(taker->position, deadBall.spot);
      if (state == MatchState::THROW_IN &&
          !ballInPlayAtBinturong(deadBall.spot))
      {
        // A throw-in: on his spot (or near it, after a while) he lifts it.
        if (gap <= MatchTuning::Player::RESTART_ARRIVED_METRES)
        {
          deadBall.gapSeconds += dt;
        }
        if (gap <= D::THROW_IN_SET_METRES ||
            deadBall.gapSeconds >= D::THROW_IN_SET_LIMIT_SECONDS)
        {
          deadBall.phase = Phase::LIFTED;
          deadBall.seconds = 0.0F;
        }
      }
      else if (gap <= MatchTuning::Player::RESTART_ARRIVED_METRES)
      {
        placeDeadBallBinturong(taker);
      }
      break;
    }
    case Phase::LIFTED:
    {
      // Up from carrying height to over his head, and out in front of him,
      // as he stands; then it is ready to throw.
      const float share =
          std::min(deadBall.seconds / D::THROW_IN_LIFT_SECONDS, 1.0F);
      const PitchPoint hold =
          throwInHold(pitch, taker->position, deadBall.spot);
      const float overhead =
          taker->heightMetres + D::THROW_IN_ABOVE_HEAD_METRES;
      taker->movementTarget = deadBall.spot;
      ball.position = PitchPoint{
          taker->position.x + ((hold.x - taker->position.x) * share),
          taker->position.y + ((hold.y - taker->position.y) * share)};
      ball.z =
          D::CARRY_HEIGHT_METRES + (overhead - D::CARRY_HEIGHT_METRES) * share;
      ball.velocity = taker->velocity;
      ball.velocityZ = 0.0F;
      if (share >= 1.0F)
      {
        placeDeadBallBinturong(taker);
      }
      break;
    }
    case Phase::LOOSE:
    {
      moveDeadBallBinturong(dt);
      if (deadBall.phase != Phase::LOOSE)
      {
        break;
      }
      const Vector2F at{.x = ball.position.x, .y = ball.position.y};
      const float height = ball.z;
      const float speed = length(ball.velocity);
      // Out of the taker's reach (behind the boards; for an Aardvark body,
      // which stays on the pitch, off it): a steward throws it back once it
      // settles (or after a while).
      const bool leavesPitch = getBody(taker->isHomeTeam) == BodyId::BINTURONG;
      if (leavesPitch ? Ground::beyond(pitch, at, Ground::BOARD_X,
                                       Ground::BOARD_Y) > 0.0F
                      : Ground::beyond(pitch, at, 0.0F, 0.0F) > 0.0F)
      {
        deadBall.gapSeconds += dt;
        const bool settled = speed < D::SETTLED_SPEED && height < 0.05F;
        if (settled || deadBall.gapSeconds >= D::STEWARD_SECONDS)
        {
          // Off a dugout's roof, from where it lies on it.
          float release = D::THROW_RELEASE_METRES;
          for (const MatchGround::Rect& dugout : pitch.ground.dugouts)
          {
            if (dugout.contains(at.x, at.y))
            {
              release = std::max(release,
                                 MatchGround::dugoutRoofHeight(dugout, at.y) +
                                     MatchTuning::Units::BALL_RADIUS_METRES);
            }
          }
          throwDeadBallBinturong(release);
        }
        break;
      }
      deadBall.gapSeconds = 0.0F;
      const bool settled = speed < D::SETTLED_SPEED && height < 0.05F;
      // A free kick or penalty: the fouled side's nearest player goes to it
      // and plays it along the ground to stop on the spot.
      if (deadBall.fetcherIndex)
      {
        MatchPlayer& fetcher = players[*deadBall.fetcherIndex];
        if (!active(fetcher) || (fetcher.fallen && !deadBall.dropKick))
        {
          deadBall.fetcherIndex.reset();
        }
        else if (deadBall.dropKick)
        {
          // After a goal: a moment after it went in (and once he is up) the
          // keeper goes for it and picks it up.
          if (&fetcher != taker)
          {
            taker->movementTarget = deadBall.spot;
          }
          if (deadBall.totalSeconds < D::GOAL_FETCH_DELAY_SECONDS ||
              fetcher.fallen)
          {
            fetcher.movementTarget = deadBall.fetcherTarget;
          }
          else if (height <= D::PICKUP_MAX_HEIGHT_METRES &&
                   speed <= D::PICKUP_MAX_SPEED &&
                   distance(fetcher.position, ball.position) <=
                       D::PICKUP_REACH_METRES)
          {
            deadBall.phase = Phase::CARRIED;
            deadBall.seconds = 0.0F;
          }
          else
          {
            fetcher.movementTarget = routeRoundGoalFrames(
                pitch, fetcher.position, clampToBoards(ball.position));
          }
          break;
        }
        else
        {
          if (&fetcher != taker)
          {
            taker->movementTarget = deadBall.spot;
          }
          if (height <= D::FOOT_PASS_MAX_HEIGHT_METRES &&
              speed <= D::FOOT_PASS_MAX_SPEED &&
              distance(fetcher.position, ball.position) <=
                  D::PICKUP_REACH_METRES)
          {
            const Vector2F way = metricDirection(ball.position, deadBall.spot);
            const float pace =
                groundLaunchSpeed(distance(ball.position, deadBall.spot), 0.0F);
            ball.velocity = {.x = way.x * pace, .y = way.y * pace};
            ball.velocityZ = 0.0F;
            ball.spin = {};
            fetcher.movementTarget =
                &fetcher == taker ? deadBall.spot : deadBall.fetcherTarget;
            deadBall.fetcherIndex.reset();
            deadBall.passedToSpot = true;
          }
          else
          {
            fetcher.movementTarget = routeRoundGoalFrames(
                pitch, fetcher.position, clampToBoards(ball.position));
          }
          break;
        }
      }
      // Played to the spot: the taker lets it roll, sets it on the spot once
      // it stops near it, and fetches it if it drifts.
      if (deadBall.passedToSpot)
      {
        taker->movementTarget = deadBall.spot;
        if (!settled)
        {
          break;
        }
        if (distance(ball.position, deadBall.spot) <= D::SPOT_TOLERANCE_METRES)
        {
          if (distance(taker->position, deadBall.spot) <=
              MatchTuning::Player::RESTART_ARRIVED_METRES)
          {
            placeDeadBallBinturong(taker);
          }
          break;
        }
        deadBall.passedToSpot = false;
      }
      // In the taker's reach, low and slow enough: he picks it up (or
      // catches it); otherwise he goes to it (or, while it is up, waits for
      // it at the spot).
      const bool low = height <= D::PICKUP_MAX_HEIGHT_METRES;
      if (low && speed <= D::PICKUP_MAX_SPEED &&
          distance(taker->position, ball.position) <= D::PICKUP_REACH_METRES)
      {
        deadBall.phase = Phase::CARRIED;
        deadBall.seconds = 0.0F;
        taker->movementTarget = deadBall.spot;
        break;
      }
      taker->movementTarget =
          low ? clampToBoards(ball.position) : deadBall.spot;
      break;
    }
  }
  // Up in the stand or out of sight, he waits for it at the spot.
  if (deadBall.active && (deadBall.phase == Phase::CAUGHT ||
                          deadBall.phase == Phase::OUT_OF_SIGHT))
  {
    taker->movementTarget = deadBall.spot;
  }
  // Round a goal frame, not into it.
  if (deadBall.active)
  {
    taker->movementTarget =
        routeRoundGoalFrames(pitch, taker->position, taker->movementTarget);
  }
}

bool MatchEngine::keptOutBinturong(const MatchPlayer& player,
                                   const MatchPlayer& taker, PitchPoint at,
                                   float slack) const
{
  // Whether the laws keep him out of `at` (to within `slack`): at a penalty
  // everyone but the taker and the keeper facing it out of the area and its
  // arc (Law 14); otherwise the opponents back from the ball (Laws 13, 15,
  // 17), and out of the area at a goal kick (Law 16).
  using P = MatchTuning::Pitch;
  using T = MatchTuning::Timing;
  if (&player == &taker)
  {
    return false;
  }
  const PitchPoint spot = restartSpot();
  const bool right = spot.x > pitch.atX(P::CENTRE);
  const float edge = (right ? pitch.length - P::PENALTY_AREA_DEPTH_METRES
                            : P::PENALTY_AREA_DEPTH_METRES);
  const float slackX = slack;
  const bool inArea = (right ? at.x > edge + slackX : at.x < edge - slackX) &&
                      std::abs(at.y - pitch.atY(P::CENTRE)) <
                          P::PENALTY_AREA_HALF_WIDTH_METRES - slack;
  const float gap = distance(at, spot);
  if (state == MatchState::PENALTY)
  {
    if (player.isGoalkeeper && player.isHomeTeam != taker.isHomeTeam)
    {
      return false;
    }
    return inArea || gap < T::KICK_OPPONENT_DISTANCE_METRES - slack;
  }
  if (player.isHomeTeam == taker.isHomeTeam)
  {
    return false;
  }
  switch (state)
  {
    case MatchState::THROW_IN:
      return gap < T::THROW_OPPONENT_DISTANCE_METRES - slack;
    case MatchState::GOAL_KICK:
      return inArea || gap < T::KICK_OPPONENT_DISTANCE_METRES - slack;
    case MatchState::CORNER_KICK:
    case MatchState::FREE_KICK:
      return gap < T::KICK_OPPONENT_DISTANCE_METRES - slack;
    default:
      return false;
  }
}

void MatchEngine::keepRestartDistanceBinturong(MatchPlayer& player)
{
  // Where he is going, moved to where the laws allow him: out of the area
  // (straight out), then back from the ball, a little past each line.
  using P = MatchTuning::Pitch;
  constexpr float MARGIN = MatchTuning::DeadBall::KEEP_OUT_MARGIN_METRES;
  const MatchPlayer* taker = restartTaker();
  if ((taker == nullptr) ||
      !keptOutBinturong(player, *taker, player.movementTarget, 0.0F))
  {
    return;
  }
  const PitchPoint spot = restartSpot();
  PitchPoint target = player.movementTarget;
  if (state == MatchState::PENALTY || state == MatchState::GOAL_KICK)
  {
    const bool right = spot.x > pitch.atX(P::CENTRE);
    const bool inside =
        (right ? target.x > (pitch.length - P::PENALTY_AREA_DEPTH_METRES)
               : target.x < P::PENALTY_AREA_DEPTH_METRES) &&
        std::abs(target.y - pitch.atY(P::CENTRE)) <
            P::PENALTY_AREA_HALF_WIDTH_METRES;
    if (inside)
    {
      target.x = right ? (pitch.length - P::PENALTY_AREA_DEPTH_METRES) - MARGIN
                       : P::PENALTY_AREA_DEPTH_METRES + MARGIN;
    }
  }
  const float minimum =
      (state == MatchState::THROW_IN
           ? MatchTuning::Timing::THROW_OPPONENT_DISTANCE_METRES
           : MatchTuning::Timing::KICK_OPPONENT_DISTANCE_METRES) +
      MARGIN;
  if (distance(target, spot) < minimum)
  {
    // Straight back from the ball (on it: towards his own goal).
    const Vector2F away =
        distance(target, spot) > EPSILON
            ? metricDirection(spot, target)
            : Vector2F{.x = player.isHomeTeam ? -1.0F : 1.0F, .y = 0.0F};
    const Vector2F offset = {.x = away.x * minimum, .y = away.y * minimum};
    target = PitchPoint{spot.x + offset.x, spot.y + offset.y};
  }
  player.movementTarget = clampToPitch(target);
}

void MatchEngine::updateRestartReadinessBinturong(float dt)
{
  // The ball is back on its spot: is everyone where he is going (or as near
  // as he can get), and where the laws allow him?
  using D = MatchTuning::DeadBall;
  RestartReadiness& readiness = restartReadiness;
  const MatchPlayer* taker = restartTaker();
  if ((taker == nullptr) || state == MatchState::KICK_OFF)
  {
    return;
  }
  readiness.waitSeconds += dt;
  bool everyone = true;
  for (std::size_t slot = 0;
       slot < players.size() && slot < readiness.lastGap.size(); ++slot)
  {
    const MatchPlayer& player = players[slot];
    if (!active(player) || &player == taker)
    {
      continue;
    }
    const float gap = distance(player.position, player.movementTarget);
    if (gap < readiness.lastGap[slot] - 0.01F)
    {
      readiness.stalledSeconds[slot] = 0.0F;
    }
    else
    {
      readiness.stalledSeconds[slot] += dt;
    }
    readiness.lastGap[slot] = gap;
    const bool there =
        gap <= D::READY_METRES ||
        readiness.stalledSeconds[slot] >= D::READY_STALLED_SECONDS;
    // Kept out where he is, unless where he is going is too (pushed back
    // against a line).
    const bool allowed = !keptOutBinturong(player, *taker, player.position,
                                           D::READY_SLACK_METRES) ||
                         keptOutBinturong(player, *taker, player.movementTarget,
                                          D::READY_SLACK_METRES);
    if (!there || !allowed)
    {
      everyone = false;
    }
  }
  if (!everyone)
  {
    readiness.ready = false;
    readiness.readySeconds = 0.0F;
    return;
  }
  if (!readiness.ready)
  {
    // The taker looks up before he goes (straight away in a short match).
    const auto pause = [this](float least, float most)
    { return playHalfMinutes > 0 ? 0.0F : randomFloat(least, most); };
    readiness.ready = true;
    readiness.pauseSeconds =
        state == MatchState::THROW_IN ? pause(D::THROW_IN_PAUSE_MIN_SECONDS,
                                              D::THROW_IN_PAUSE_MAX_SECONDS)
        : state == MatchState::CORNER_KICK
            ? pause(D::CORNER_PAUSE_MIN_SECONDS, D::CORNER_PAUSE_MAX_SECONDS)
        : state == MatchState::PENALTY
            ? pause(D::PENALTY_PAUSE_MIN_SECONDS, D::PENALTY_PAUSE_MAX_SECONDS)
            : pause(D::KICK_PAUSE_MIN_SECONDS, D::KICK_PAUSE_MAX_SECONDS);
  }
  readiness.readySeconds += dt;
}

bool MatchEngine::restartReadyBinturong() const
{
  const RestartReadiness& readiness = restartReadiness;
  return (readiness.ready &&
          readiness.readySeconds >= readiness.pauseSeconds) ||
         readiness.waitSeconds >= MatchTuning::DeadBall::READY_LIMIT_SECONDS;
}

void MatchEngine::setupFreeKickBinturong(bool homeTeam, PitchPoint foulPos)
{
  beginStoppage();
  pendingAdvantage.active = false;
  // The spot is where the foul was; the ball goes on from where play
  // stopped and is played back to it (startDeadBallBinturong).
  const PitchPoint spot{
      std::clamp(foulPos.x, MatchTuning::Pitch::RESTART_END_MARGIN_METRES,
                 pitch.length - MatchTuning::Pitch::RESTART_END_MARGIN_METRES),
      std::clamp(foulPos.y, MatchTuning::Pitch::RESTART_SIDE_MARGIN_METRES,
                 pitch.width - MatchTuning::Pitch::RESTART_SIDE_MARGIN_METRES)};
  const PitchPoint goal =
      pitch.at(homeTeam ? 1.0F : 0.0F, MatchTuning::Pitch::CENTRE);
  const float goalMetres = distance(spot, goal);
  // Free kicks in the attacking third are set pieces taken by the specialist.
  restartIsSetPiece =
      goalMetres <= MatchTuning::SetPiece::CROSSING_FREE_KICK_METRES;
  MatchPlayer* taker =
      chooseRestartTaker(MatchState::FREE_KICK, homeTeam, spot);
  setRestartTaker(taker);
  startDeadBallBinturong(spot);
  if (restartIsSetPiece)
  {
    arrangeSetPiece(homeTeam, spot);
  }
  chooseDeadBallFetcherBinturong(homeTeam);
  state = MatchState::FREE_KICK;
  // Taken once everyone is ready (restartReadyBinturong), not on a
  // countdown; a card, an injury or a substitution still adds its wait.
  setPieceTimer = 0.0F;
  updateTeamPhases();
  MatchEvent& event = logEvent(MatchEventType::FREE_KICK);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = spot;
  if ((taker != nullptr) && (taker->player != nullptr))
  {
    event.primaryPlayerId = taker->player->getId();
  }
}

void MatchEngine::setupPenaltyBinturong(bool homeTeam)
{
  beginStoppage();
  restartIsSetPiece = true;
  pendingAdvantage.active = false;
  const PitchPoint spot = PitchPoint{
      homeTeam ? pitch.length - MatchTuning::Pitch::PENALTY_SPOT_METRES
               : MatchTuning::Pitch::PENALTY_SPOT_METRES,
      pitch.atY(MatchTuning::Pitch::CENTRE)};
  MatchPlayer* taker =
      chooseRestartTaker(MatchState::PENALTY, homeTeam, ball.position);
  setRestartTaker(taker);
  if (taker != nullptr)
  {
    // He decides his kick before he walks up and runs up.
    decidePenalty(*taker);
  }
  startDeadBallBinturong(spot);
  // Everyone else waits outside the area; the keeper walks to his line.
  const float edge =
      homeTeam
          ? (pitch.length - MatchTuning::Pitch::PENALTY_AREA_DEPTH_METRES) -
                MatchTuning::SetPiece::PENALTY_WAIT_OFFSET_METRES
          : MatchTuning::Pitch::PENALTY_AREA_DEPTH_METRES +
                MatchTuning::SetPiece::PENALTY_WAIT_OFFSET_METRES;
  for (auto& player : players)
  {
    if (!active(player) || &player == taker)
    {
      continue;
    }
    if (player.isGoalkeeper && player.isHomeTeam != homeTeam)
    {
      // Law 14: the defending keeper stays on his goal line.
      player.movementTarget = PitchPoint{
          homeTeam ? pitch.length - MatchTuning::Pitch::PLAYER_EDGE_END_METRES
                   : MatchTuning::Pitch::PLAYER_EDGE_END_METRES,
          pitch.atY(MatchTuning::Pitch::CENTRE)};
      continue;
    }
    if (player.isGoalkeeper)
    {
      continue;
    }
    const bool inside =
        homeTeam ? player.position.x > edge : player.position.x < edge;
    if (inside)
    {
      player.movementTarget.x = edge;
    }
  }
  chooseDeadBallFetcherBinturong(homeTeam);
  if (homeTeam)
  {
    ++stats.homePenalties;
  }
  else
  {
    ++stats.awayPenalties;
  }
  ++stoppageLogs[static_cast<std::size_t>(period - 1)].penalties;
  state = MatchState::PENALTY;
  penaltyStrikeAt.reset();
  // Taken once everyone is ready (restartReadyBinturong), not on a
  // countdown; a card, an injury or a substitution still adds its wait.
  setPieceTimer = 0.0F;
  updateTeamPhases();
  MatchEvent& event = logEvent(MatchEventType::PENALTY);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = spot;
  if ((taker != nullptr) && (taker->player != nullptr))
  {
    event.primaryPlayerId = taker->player->getId();
  }
}

void MatchEngine::chooseDeadBallFetcherBinturong(bool homeTeam)
{
  // The fouled side's player nearest the ball, on his feet, goes to it;
  // afterwards he goes on to the restart position he has now.
  std::optional<std::size_t> nearest;
  float best = std::numeric_limits<float>::max();
  for (std::size_t slot = 0; slot < players.size(); ++slot)
  {
    const MatchPlayer& player = players[slot];
    if (!active(player) || player.isHomeTeam != homeTeam || player.fallen)
    {
      continue;
    }
    const float metres = distance(player.position, ball.position);
    if (metres < best)
    {
      best = metres;
      nearest = slot;
    }
  }
  deadBall.fetcherIndex = nearest;
  if (nearest)
  {
    deadBall.fetcherTarget = players[*nearest].movementTarget;
  }
}

void MatchEngine::placeTakerBinturong(MatchPlayer& taker, PitchPoint spot)
{
  taker.position = clampToPitch(spot);
  taker.movementTarget = taker.position;
  taker.velocity = {.x = 0.0F, .y = 0.0F};
  // Anyone standing on the spot steps aside for the taker.
  for (auto& other : players)
  {
    if (!active(other) || &other == &taker)
    {
      continue;
    }
    const float dx = other.position.x - taker.position.x;
    const float dy = other.position.y - taker.position.y;
    const float gap = std::sqrt((dx * dx) + (dy * dy));
    if (gap >= MatchTuning::Player::MINIMUM_BODY_SEPARATION_METRES)
    {
      continue;
    }
    const float towardCentre =
        taker.position.y < pitch.atY(MatchTuning::Pitch::CENTRE) ? 1.0F : -1.0F;
    const Vector2F away = gap > EPSILON
                              ? Vector2F{.x = dx / gap, .y = dy / gap}
                              : Vector2F{.x = 0.0F, .y = towardCentre};
    const float push =
        MatchTuning::Player::MINIMUM_BODY_SEPARATION_METRES - gap;
    other.position = moveBody(other.position,
                              PitchPoint{other.position.x + (away.x * push),
                                         other.position.y + (away.y * push)});
  }
}

void MatchEngine::completeRestartBinturong()
{
  // Restart setup functions own placement and waiting; this function owns the
  // actual release. Preserve the restart state until the kick is executed:
  // passBall uses it for offside exemptions and set-piece classification.
  const MatchState restartState = state;
  MatchPlayer* taker = restartTaker();
  const bool userPass = userRestartPassRequested();
  // Not until any sub coming on is on the pitch (touchline plan stage 5),
  // nor while a player sent off is still on it (stage 6).
  if (substitutionHoldsRestart() || sentOffStillOn())
  {
    return;
  }
  // Not until the ball is back on the spot.
  if (deadBall.active)
  {
    return;
  }
  // A kick-off after a goal waits for everyone to be back (for a while).
  if (restartState == MatchState::KICK_OFF && kickOffWalkIn &&
      !kickOffReadyBinturong() &&
      setPieceTimer > -MatchTuning::Timing::KICKOFF_WALK_IN_LIMIT_SECONDS)
  {
    return;
  }
  // Any other restart waits for everyone to be ready and the taker to look
  // up (unless his controller takes it at once); once he is lining it up,
  // it goes on.
  if (restartState != MatchState::KICK_OFF && !restartLineUp && !userPass &&
      !restartReadyBinturong())
  {
    return;
  }
  // A Binturong body's taker lines the ball up first: he moves until it is
  // inside his comfort gap, out of his dead zone (restart lining-up).
  if ((taker != nullptr) && active(*taker) &&
      linesUpRestart(*taker, restartState) &&
      !lineUpRestart(*taker, restartState, userPass))
  {
    return;
  }
  // One who does not line it up (a throw-in, a keeper) walks to it: the
  // restart waits until he is there.
  const bool walksUp =
      (taker != nullptr) && getBody(taker->isHomeTeam) == BodyId::BINTURONG;
  if (walksUp && active(*taker) && restartState != MatchState::KICK_OFF &&
      !linesUpRestart(*taker, restartState) &&
      distance(taker->position, ball.position) >
          MatchTuning::Player::RESTART_ARRIVED_METRES)
  {
    return;
  }
  // A throw-in from behind the touchline: he stands where he lifted it, the
  // ball over his head in front of him (for a while, then from where he is).
  if (walksUp && active(*taker) && restartState == MatchState::THROW_IN &&
      !ballInPlayAtBinturong(ball.position) &&
      restartReadiness.waitSeconds <
          MatchTuning::DeadBall::THROW_IN_SET_LIMIT_SECONDS)
  {
    if (distance(throwInHold(pitch, taker->position, deadBall.spot),
                 ball.position) > MatchTuning::DeadBall::THROW_IN_SET_METRES)
    {
      return;
    }
  }
  // A penalty against a Binturong keeper (goalkeeper plan step 9): the taker
  // sets off, and the keeper makes his guess then, so he can push off before
  // the strike; the strike comes at the end of the run-up.
  if (restartState == MatchState::PENALTY && (taker != nullptr) &&
      active(*taker) && getBody(!taker->isHomeTeam) == BodyId::BINTURONG)
  {
    const double now = getSimulatedSeconds();
    if (!penaltyStrikeAt)
    {
      penaltyStrikeAt = now + MatchTuning::KeeperDive::PENALTY_RUN_UP_SECONDS;
      if (MatchPlayer* keeper = findGoalkeeper(!taker->isHomeTeam))
      {
        planPenaltyDiveBinturong(*keeper, *penaltyStrikeAt);
      }
      return;
    }
    if (now < *penaltyStrikeAt)
    {
      return;
    }
    penaltyStrikeAt.reset();
  }
  const std::optional<RestartLineUp> linedUp = restartLineUp;
  restartTakerIndex.reset();
  restartLineUp.reset();
  kickOffWalkIn = false;
  if ((taker == nullptr) || !active(*taker))
  {
    clearPossession();
    ball.velocity = {.x = 0.0F, .y = 0.0F};
    ball.velocityZ = 0.0F;
    clearFlightState();
    restartIsSetPiece = false;
    state = MatchState::PLAYING;
    updateTeamPhases();
    return;
  }

  // A Binturong body's taker has walked up to it and takes it from where he
  // stands; an Aardvark body's is lined up exactly on the ball.
  if (linedUp)
  {
    noteRestartKick(*taker, *linedUp, restartState);
  }
  if (restartState != MatchState::KICK_OFF && !linedUp && !walksUp)
  {
    taker->position = ball.position;
    taker->velocity = {.x = 0.0F, .y = 0.0F};
  }
  if (restartState == MatchState::PENALTY)
  {
    if (!linedUp && !walksUp)
    {
      ball.position = taker->position;
    }
    takeShot(*taker, MatchTuning::Shooting::PENALTY_XG);
    state = MatchState::PLAYING;
    restartIsSetPiece = false;
    updateTeamPhases();
    return;
  }

  // Corners, free kicks near goal and throw-ins in the attacking third open
  // a set-piece phase for the side taking them.
  const float restartDepth = taker->isHomeTeam
                                 ? pitch.fractionX(ball.position)
                                 : 1.0F - pitch.fractionX(ball.position);
  if (restartIsSetPiece ||
      (restartState == MatchState::THROW_IN &&
       restartDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START))
  {
    setPiecePhaseRemaining = MatchTuning::SetPiece::SET_PIECE_PHASE_SECONDS;
    setPieceHome = taker->isHomeTeam;
    setPieceBallInBox = false;
    setPieceBoxCleared = false;
  }
  if (linedUp ? linedUp->userAction != MatchInputAction::NONE : userPass)
  {
    const MatchInputAction action =
        linedUp && linedUp->userAction != MatchInputAction::NONE
            ? linedUp->userAction
            : controlInput.action;
    playControlledPass(*taker, action);
    controlInput.action = MatchInputAction::NONE;
    controlActionRemaining = 0.0F;
    ++controlledActions[taker->isHomeTeam ? 0 : 1];
  }
  else if (linedUp)
  {
    playRestart(*taker, linedUp->play);
  }
  else
  {
    takeSetPiece(*taker, restartState);
  }
  // Thrown from behind the touchline: on its way in.
  if (restartState == MatchState::THROW_IN &&
      (getPossessionAttachment() == nullptr) &&
      !ballInPlayAtBinturong(ball.position))
  {
    markThrowEntry(true);
    noteThrowTeam(taker->isHomeTeam);
  }
  if (state == restartState)
  {
    state = MatchState::PLAYING;
  }
  restartIsSetPiece = false;
  updateTeamPhases();
}
