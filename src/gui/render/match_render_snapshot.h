// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <vector>

#include "global/types.h"
#include "model/match_events.h"
#include "model/match_layout.h"
#include "model/match_pitch.h"
#include "model/match_state.h"
#include "model/match_tuning.h"

class MatchEngine;
class Player;
struct MatchStats;

/**
 * Read-only normalized world state consumed by renderers.
 *
 * This header does not include the engine: renderers see only the snapshot,
 * and the engine is read in match_render_snapshot.cpp.
 *
 * The snapshot is rebuilt every frame from a `MatchEngine` and carries the
 * previous and current fixed-step positions plus the interpolation fraction,
 * so 2D and 3D renderers can interpolate without touching the
 * simulation. Renderers must never mutate this data or the engine.
 *
 * Positions are copied, but Player pointers and the event/statistics pointers
 * in MatchRenderSnapshot remain borrowed. Consume the snapshot while its
 * source engine and players are alive and stable; it is not an owned replay
 * record or a cross-thread publication format. See
 * docs/development/match-engine.md.
 *
 * Units: positions are metres (x along the pitch's length, home attacking
 * +x; y across its width: MatchRenderSnapshot's pitchLengthMetres and
 * pitchWidthMetres, a match's 105 x 68 m). Ball `currentZ` and `previousZ`
 * are metres, the same as the `*HeightMetres` fields.
 */
struct MatchRenderPlayer
{
  const Player* player = nullptr;
  bool isHomeTeam = false;
  PitchPoint previousPosition{MatchTuning::Pitch::MATCH_CENTRE_X_METRES,
                              MatchTuning::Pitch::MATCH_CENTRE_Y_METRES};
  PitchPoint currentPosition{MatchTuning::Pitch::MATCH_CENTRE_X_METRES,
                             MatchTuning::Pitch::MATCH_CENTRE_Y_METRES};
  PitchPoint movementTarget{MatchTuning::Pitch::MATCH_CENTRE_X_METRES,
                            MatchTuning::Pitch::MATCH_CENTRE_Y_METRES};
  PlayerIntent intent = PlayerIntent::HOLD_SHAPE;
  float previousFacingAngle = 0.0F;
  float currentFacingAngle = 0.0F;
  float stamina = 1.0F;
  bool possessesBall = false;
  /** False once sent off or forced off; such players wait by the bench. */
  bool onPitch = true;
  /** Deep in the tunnel (walk-outs and walk-offs): not drawn. */
  bool hidden = false;
  bool isGoalkeeper = false;
  bool isDiving = false;
  bool isInjured = false;
  int yellowCards = 0;
  float heightMetres = MatchTuning::Units::DEFAULT_PLAYER_HEIGHT_METRES;
  /** His feet's height in a jump (m), at the previous and current step. */
  float previousFeetHeightMetres = 0.0F;
  float currentFeetHeightMetres = 0.0F;
  /** Running speed in metres per simulated second (drives the stride). */
  float speedMetresPerSecond = 0.0F;
  /** Seconds before this player may challenge again; it jumps up when he
   * goes into a tackle (renderers start the tackle pose on the rise). */
  float tackleCooldown = 0.0F;
  /** Down after a collision (the cylinder on its side), and the way his
   * body lies (an angle like the facing angles). */
  bool fallen = false;
  float lyingAngle = 0.0F;
  /** While down: on his front (else on his back), turning over as he rolls. */
  bool onFront = false;
  /** Getting up: how far through (0 lying, 1 up), and the way he will face. */
  float getUpShare = 0.0F;
  float upFacingAngle = 0.0F;
};

/** Someone round the touchline (touchline plan stage 2): a sub, a member of
 * staff or a manager, seated in the dugout or on his feet. */
struct MatchRenderTouchlinePerson
{
  /** A sub's player; staff and managers have none. */
  const Player* player = nullptr;
  bool isHomeTeam = false;
  TouchlineRole role = TouchlineRole::SUBSTITUTE;
  bool seated = false;
  /** Deep in the tunnel (walk-outs and walk-offs): not drawn. */
  bool hidden = false;
  PitchPoint previousPosition{};
  PitchPoint currentPosition{};
  float previousFacingAngle = 0.0F;
  float currentFacingAngle = 0.0F;
  float heightMetres = MatchTuning::Units::DEFAULT_PLAYER_HEIGHT_METRES;
  float speedMetresPerSecond = 0.0F;
  /** Down after a knock, as a player's (MatchRenderPlayer). */
  bool fallen = false;
  float lyingAngle = 0.0F;
  bool onFront = false;
  float getUpShare = 0.0F;
};

struct MatchRenderBall
{
  PitchPoint previousPosition{MatchTuning::Pitch::MATCH_CENTRE_X_METRES,
                              MatchTuning::Pitch::MATCH_CENTRE_Y_METRES};
  PitchPoint currentPosition{MatchTuning::Pitch::MATCH_CENTRE_X_METRES,
                             MatchTuning::Pitch::MATCH_CENTRE_Y_METRES};
  float previousZ = 0.0F;
  float currentZ = 0.0F;
  const Player* possessedBy = nullptr;
  /** Gone beyond the crowd the views show: not drawn. */
  bool outOfSight = false;
  float previousHeightMetres = 0.0F;
  float currentHeightMetres = 0.0F;
  bool isShot = false;
  bool isAerialDelivery = false;
  /** Thrown in from the touchline (until the next touch). */
  bool fromThrowIn = false;
  /**
   * Who last struck the ball and the seconds left in his lockout. The
   * lockout is reset on every kick and only counts down otherwise, so a rise
   * between two snapshots marks the step the ball was struck.
   */
  const Player* kicker = nullptr;
  float kickerLockout = 0.0F;
};

/** One zone of a training session drawn on the ground (training ground plan
 * section 23): its walled pitch's size and goals, placed by its centre on
 * the ground and turned 90 degrees or not. */
struct MatchRenderZone
{
  PitchPoint centre{};
  bool turned = false;
  float lengthMetres = 0.0F;
  float widthMetres = 0.0F;
  PitchGoal goal{};

  /** A point of the zone's pitch (metres on it) on the ground. */
  [[nodiscard]] PitchPoint toGround(PitchPoint local) const
  {
    const float dx = local.x - (0.5F * lengthMetres);
    const float dy = local.y - (0.5F * widthMetres);
    return turned ? PitchPoint{centre.x - dy, centre.y + dx}
                  : PitchPoint{centre.x + dx, centre.y + dy};
  }
  /** A facing angle on the zone's pitch, on the ground. */
  [[nodiscard]] float toGroundAngle(float angle) const
  {
    return turned ? angle + 1.5707963F : angle;
  }
};

struct MatchRenderSnapshot
{
  /** A training session's view of its ground (training ground plan section
   * 23): the pitch above is the ground, everything else is already placed
   * on it, and these are its zones (walls and goals drawn at each) and the
   * other zones' balls. Empty otherwise. */
  std::vector<MatchRenderZone> zones;
  std::vector<MatchRenderBall> extraBalls;
  /** The size of the engine's pitch (metres), what it looks like and what
   * it ends at (MatchPitch), and the kit laid out on it. The match views
   * still draw the match pitch whatever these say; they read them once
   * training pitches are drawn (training ground plan section 18, T2c). */
  float pitchLengthMetres = MatchTuning::Pitch::MATCH_LENGTH_METRES;
  float pitchWidthMetres = MatchTuning::Pitch::MATCH_WIDTH_METRES;
  PitchSurface pitchSurface = PitchSurface::MATCH;
  PitchEdge pitchEdge = PitchEdge::LINES;
  PitchGoal pitchGoal{};
  PitchKit::Layout kit;
  std::vector<MatchRenderPlayer> players;
  std::vector<MatchRenderTouchlinePerson> touchline;
  /** Sides whose strips the views draw (the match screen's own ids, so its
   * colour swatches match); 0: taken from the players' clubs. */
  TeamID homeTeam = 0;
  TeamID awayTeam = 0;
  MatchRenderBall ball;
  MatchState state = MatchState::KICK_OFF;
  TeamPhase homePhase = TeamPhase::SET_PIECE;
  TeamPhase awayPhase = TeamPhase::SET_PIECE;
  float transitionSecondsRemaining = 0.0F;
  /** A restart is set: everyone ready and the taker looking up. */
  bool restartReady = false;
  /** Advantage is being played (the free kick can still be called back). */
  bool advantagePending = false;
  int homeScore = 0;
  int awayScore = 0;
  bool goalScoredByHome = false;
  float goalCelebrationRemaining = 0.0F;
  float goalCelebrationDuration = MatchTuning::Timing::GOAL_CELEBRATION_SECONDS;
  float matchTimeMinutes = 0.0F;
  const std::vector<MatchEvent>* events = nullptr;
  const MatchStats* stats = nullptr;
  float interpolationAlpha = 0.0F;
  /** 1 or 2; added minutes are 0 until announced. */
  int period = 1;
  int addedMinutesFirstHalf = 0;
  int addedMinutesSecondHalf = 0;
  GoalkeeperState homeGoalkeeperState = GoalkeeperState::SET_POSITION;
  GoalkeeperState awayGoalkeeperState = GoalkeeperState::SET_POSITION;
  const std::vector<PlayerMatchStats>* playerStats = nullptr;
};

/**
 * Refills `snapshot` from a live engine, reusing its storage so a view that
 * keeps one snapshot allocates nothing per frame.
 */
void fillMatchRenderSnapshot(const MatchEngine& engine,
                             MatchRenderSnapshot& snapshot);

/** Builds a read-only render snapshot from a live engine. */
MatchRenderSnapshot buildMatchRenderSnapshot(const MatchEngine& engine);

/** Linear interpolation of a 2D point used by renderers. */
inline PitchPoint lerpRenderPosition(PitchPoint first, PitchPoint second,
                                     float alpha)
{
  return PitchPoint{first.x + ((second.x - first.x) * alpha),
                    first.y + ((second.y - first.y) * alpha)};
}
