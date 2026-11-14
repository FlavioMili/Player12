// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>

#include "model/match_ground.h"
#include "model/match_point.h"
#include "model/match_tuning.h"

/** What a pitch looks like and has round it: a match pitch (lines, goals,
 * furniture, stadium) or a training pitch (plain grass, nothing round it). */
enum class PitchSurface : std::uint8_t
{
  MATCH,
  TRAINING
};

/** The goals a pitch can have at its ends (training ground plan section
 * 19): none, or one of these kinds, the same at both ends. */
enum class GoalKind : std::uint8_t
{
  NONE,
  FULL,
  YOUTH_9V9,
  YOUTH_7V7,
  FUTSAL,
  FIVE_A_SIDE,
  SMALL_FRAMED,
  POP_UP
};

/** A goal's size (metres): the same at both ends of a pitch. A match's is
 * the Laws' (Law 1: 7.32 x 2.44 m), with its posts and net. */
struct PitchGoal
{
  GoalKind kind = GoalKind::FULL;
  [[nodiscard]] constexpr bool present() const { return kind != GoalKind::NONE; }
  /** From the centre to the inside of a post. */
  float halfWidth = MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES;
  /** Up to the underside of the crossbar. */
  float crossbar = MatchTuning::Units::CROSSBAR_HEIGHT_METRES;
  /** A post's (and the bar's) thickness. */
  float post = MatchTuning::Pitch::GOAL_POST_METRES;
  /** How far the net goes back behind the goal line. */
  float netDepth = MatchTuning::Pitch::GOAL_NET_DEPTH_METRES;

  /** A goal of this kind (width x height agreed with the user, plan section
   * 19: full size, Law 1; futsal; FA youth formats; 5-a-side common; small
   * framed and pop-up our own estimates). Posts and net scale with the
   * height from the full-size goal's; a pop-up goal has thin poles and a
   * shallow net that is its frame (not researched). */
  [[nodiscard]] static constexpr PitchGoal ofKind(GoalKind kind)
  {
    const auto sized = [kind](float width, float height)
    {
      constexpr float FULL_HEIGHT = MatchTuning::Units::CROSSBAR_HEIGHT_METRES;
      PitchGoal goal;
      goal.kind = kind;
      goal.halfWidth = 0.5f * width;
      goal.crossbar = height;
      goal.post = MatchTuning::Pitch::GOAL_POST_METRES * height / FULL_HEIGHT;
      goal.netDepth = MatchTuning::Pitch::GOAL_NET_DEPTH_METRES * height / FULL_HEIGHT;
      return goal;
    };
    switch (kind)
    {
      case GoalKind::NONE:
      {
        PitchGoal none;
        none.kind = GoalKind::NONE;
        return none;
      }
      case GoalKind::FULL: return PitchGoal{};
      case GoalKind::YOUTH_9V9: return sized(4.88f, 2.13f);
      case GoalKind::YOUTH_7V7: return sized(3.66f, 1.83f);
      case GoalKind::FUTSAL: return sized(3.0f, 2.0f);
      case GoalKind::FIVE_A_SIDE: return sized(3.66f, 1.22f);
      case GoalKind::SMALL_FRAMED: return sized(1.8f, 1.2f);
      case GoalKind::POP_UP:
      {
        PitchGoal goal = sized(1.2f, 0.8f);
        goal.post = 0.03f;
        goal.netDepth = 0.6f;
        return goal;
      }
    }
    return PitchGoal{};
  }
};

/** A goal kind's name for menus and the lab ("pop-up"...). */
[[nodiscard]] constexpr const char* goalKindName(GoalKind kind)
{
  switch (kind)
  {
    case GoalKind::NONE: return "none";
    case GoalKind::FULL: return "full";
    case GoalKind::YOUTH_9V9: return "9v9";
    case GoalKind::YOUTH_7V7: return "7v7";
    case GoalKind::FUTSAL: return "futsal";
    case GoalKind::FIVE_A_SIDE: return "5-a-side";
    case GoalKind::SMALL_FRAMED: return "small";
    case GoalKind::POP_UP: return "pop-up";
  }
  return "none";
}

/** What the pitch ends at: lines (the ball goes out of play) or hard walls
 * (unlimited height; the ball bounces back and players can't cross). */
enum class PitchEdge : std::uint8_t
{
  LINES,
  WALLS
};

/**
 * The size of the pitch one engine plays on, and the values built from it.
 * Each MatchEngine holds its own; a match's is 105 x 68 m
 * (MatchTuning::Pitch::MATCH_LENGTH_METRES / MATCH_WIDTH_METRES).
 *
 * Positions are in metres from the top left corner (x along the length, y
 * across the width); the ball's height is in metres; velocities and
 * `_METRES` tuning are metres.
 */
struct MatchPitch final
{
  constexpr MatchPitch() : MatchPitch(MatchTuning::Pitch::MATCH_LENGTH_METRES,
                                      MatchTuning::Pitch::MATCH_WIDTH_METRES)
  {
  }
  constexpr MatchPitch(float lengthMetres, float widthMetres,
                       PitchSurface surfaceKind = PitchSurface::MATCH,
                       PitchEdge edgeKind = PitchEdge::LINES)
      : length(lengthMetres),
        width(widthMetres),
        inverseLength(1.0f / lengthMetres),
        inverseWidth(1.0f / widthMetres),
        bodyMinX(edgeKind == PitchEdge::WALLS
                     ? MatchTuning::Body::RADIUS_METRES
                     : -(MatchTuning::Pitch::BOARDS_END_METRES -
                         MatchTuning::Body::RADIUS_METRES)),
        bodyMaxX(lengthMetres - bodyMinX),
        bodyMinY(edgeKind == PitchEdge::WALLS
                     ? MatchTuning::Body::RADIUS_METRES
                     : -(MatchTuning::Pitch::BOARDS_SIDE_METRES -
                         MatchTuning::Body::RADIUS_METRES)),
        bodyMaxY(widthMetres - bodyMinY),
        ground(lengthMetres, widthMetres),
        surface(surfaceKind),
        edge(edgeKind)
  {
  }

  /** A training pitch of this size: plain grass, walled all round
   * (training ground plan section 18). */
  [[nodiscard]] static constexpr MatchPitch training(
      float lengthMetres, float widthMetres, GoalKind goals = GoalKind::NONE)
  {
    MatchPitch pitch(lengthMetres, widthMetres, PitchSurface::TRAINING,
                     PitchEdge::WALLS);
    pitch.goal = PitchGoal::ofKind(goals);
    return pitch;
  }

  /** Metres, goal line to goal line and touchline to touchline. */
  float length;
  float width;
  /** 1 / length and 1 / width: metres to pitch fractions. */
  float inverseLength;
  float inverseWidth;
  /** Inside the boards, his body touching them (metres): where a player
   * may mean to go to fetch a ball (MatchEngine::clampToBoards). His body
   * can go further, through the gaps in the boards (moveBody). On a walled
   * pitch: inside the walls, and no further. */
  float bodyMinX;
  float bodyMaxX;
  float bodyMinY;
  float bodyMaxY;
  /** The furniture round this pitch (metres): tunnel, dugouts, technical
   * areas, boards and the stand's front wall. */
  MatchGround::Layout ground;
  /** What it looks like, and what it ends at. */
  PitchSurface surface;
  PitchEdge edge;
  /** The goal at each end (a match's by default). */
  PitchGoal goal{};

  // ---------------------------------------------------------------------
  // Point units (training ground T1 stage 2). A PitchPoint's coordinates
  // and the ball's height are metres. Tuning given as a fraction of the
  // pitch goes through `at`/`atX`/`atY` (and back through `fraction`);
  // stage 4 decided each of those (plan section 17).
  // ---------------------------------------------------------------------

  /** A match pitch (MATCH_LENGTH_METRES x MATCH_WIDTH_METRES), for code with
   * no engine at hand: the views and tools read engine points through it
   * (training ground plan section 15, choice 8). */
  [[nodiscard]] static const MatchPitch& matchSize()
  {
    static const MatchPitch pitch{};
    return pitch;
  }

  /** The point `fx` of the length and `fy` of the width from the top left
   * corner. */
  [[nodiscard]] constexpr PitchPoint at(float fx, float fy) const
  {
    return PitchPoint{fx * length, fy * width};
  }
  /** A fraction of the length (or of the width) in metres: an x (y)
   * coordinate, or a step of that size. */
  [[nodiscard]] constexpr float atX(float fraction) const
  {
    return fraction * length;
  }
  [[nodiscard]] constexpr float atY(float fraction) const
  {
    return fraction * width;
  }
  /** A point's x (y) as a fraction of the length (width); also a coordinate
   * or a step in metres. */
  [[nodiscard]] constexpr float fractionX(PitchPoint point) const
  {
    return point.x / length;
  }
  [[nodiscard]] constexpr float fractionY(PitchPoint point) const
  {
    return point.y / width;
  }
  [[nodiscard]] constexpr float fractionX(float units) const
  {
    return units / length;
  }
  [[nodiscard]] constexpr float fractionY(float units) const
  {
    return units / width;
  }
  /** The point as fractions (x of the length, y of the width). */
  [[nodiscard]] constexpr Vector2F fractions(PitchPoint point) const
  {
    return {point.x / length, point.y / width};
  }
  /** The x coordinate seen from the other end (1 - x as a fraction). */
  [[nodiscard]] constexpr float mirrorX(float units) const
  {
    return length - units;
  }
  /** The y coordinate seen from the other touchline (1 - y as a fraction). */
  [[nodiscard]] constexpr float mirrorY(float units) const
  {
    return width - units;
  }
  /** Metres from the goal line a team defends (the home side defends
   * x = 0), along the length. */
  [[nodiscard]] constexpr float fromOwnGoalLine(PitchPoint point,
                                                bool homeTeam) const
  {
    return homeTeam ? point.x : length - point.x;
  }
  /** Metres to the goal line a team attacks, along the length. */
  [[nodiscard]] constexpr float toGoalLine(PitchPoint point,
                                           bool homeTeam) const
  {
    return homeTeam ? length - point.x : point.x;
  }
};
