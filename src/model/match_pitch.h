// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include "model/match_ground.h"
#include "model/match_point.h"
#include "model/match_tuning.h"

/**
 * The size of the pitch one engine plays on, and the values built from it.
 * Each MatchEngine holds its own; a match's is 105 x 68 m
 * (MatchTuning::Pitch::MATCH_LENGTH_METRES / MATCH_WIDTH_METRES).
 *
 * Positions are still fractions of this pitch (x of its length, y of its
 * width); velocities and `_METRES` tuning are metres.
 */
struct MatchPitch final
{
  constexpr MatchPitch() : MatchPitch(MatchTuning::Pitch::MATCH_LENGTH_METRES,
                                      MatchTuning::Pitch::MATCH_WIDTH_METRES)
  {
  }
  constexpr MatchPitch(float lengthMetres, float widthMetres)
      : length(lengthMetres),
        width(widthMetres),
        inverseLength(1.0f / lengthMetres),
        inverseWidth(1.0f / widthMetres),
        bodyMinX(-(MatchTuning::Pitch::BOARDS_END_METRES -
                   MatchTuning::Body::RADIUS_METRES) /
                 lengthMetres),
        bodyMaxX(1.0f - bodyMinX),
        bodyMinY(-(MatchTuning::Pitch::BOARDS_SIDE_METRES -
                   MatchTuning::Body::RADIUS_METRES) /
                 widthMetres),
        bodyMaxY(1.0f - bodyMinY),
        ground(lengthMetres, widthMetres)
  {
  }

  /** Metres, goal line to goal line and touchline to touchline. */
  float length;
  float width;
  /** 1 / length and 1 / width: metres to pitch fractions. */
  float inverseLength;
  float inverseWidth;
  /** Inside the boards, his body touching them (pitch fractions): where a
   * player may mean to go to fetch a ball (MatchEngine::clampToBoards). His
   * body can go further, through the gaps in the boards (moveBody). */
  float bodyMinX;
  float bodyMaxX;
  float bodyMinY;
  float bodyMaxY;
  /** The furniture round this pitch (metres): tunnel, dugouts, technical
   * areas, boards and the stand's front wall. */
  MatchGround::Layout ground;

  // ---------------------------------------------------------------------
  // Point units (training ground T1 stage 2). A PitchPoint's coordinates
  // and the ball's height are in point units: for now fractions of the
  // length and the width, and the height in pitch lengths of
  // BALL_Z_METRES. Stage 2b makes them metres by changing only these. Code
  // that means a fraction of the pitch, or metres, goes through them; code
  // that only adds, subtracts or compares points needs neither. Each keeps
  // the exact float operations of the code it replaced, so that stage 2a
  // plays bit for bit the same.
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
    return PitchPoint{fx, fy};
  }
  /** A fraction of the length (or of the width) in point units: an x (y)
   * coordinate, or a step of that size. */
  [[nodiscard]] constexpr float atX(float fraction) const { return fraction; }
  [[nodiscard]] constexpr float atY(float fraction) const { return fraction; }
  /** A point's x (y) as a fraction of the length (width); also a coordinate
   * or a step already in point units. */
  [[nodiscard]] constexpr float fractionX(PitchPoint point) const
  {
    return point.x;
  }
  [[nodiscard]] constexpr float fractionY(PitchPoint point) const
  {
    return point.y;
  }
  [[nodiscard]] constexpr float fractionX(float units) const { return units; }
  [[nodiscard]] constexpr float fractionY(float units) const { return units; }
  /** The point as fractions (x of the length, y of the width). */
  [[nodiscard]] constexpr Vector2F fractions(PitchPoint point) const
  {
    return {point.x, point.y};
  }
  /** Metres along the length (width) in point units. */
  [[nodiscard]] constexpr float fromMetresX(float metres) const
  {
    return metres / length;
  }
  [[nodiscard]] constexpr float fromMetresY(float metres) const
  {
    return metres / width;
  }
  /** The same, through the inverse (the form some code had). */
  [[nodiscard]] constexpr float fromMetresXByInverse(float metres) const
  {
    return metres * inverseLength;
  }
  [[nodiscard]] constexpr float fromMetresYByInverse(float metres) const
  {
    return metres * inverseWidth;
  }
  /** A coordinate or a step in point units, in metres. */
  [[nodiscard]] constexpr float metresX(float units) const
  {
    return units * length;
  }
  [[nodiscard]] constexpr float metresY(float units) const
  {
    return units * width;
  }
  [[nodiscard]] constexpr float metresX(PitchPoint point) const
  {
    return point.x * length;
  }
  [[nodiscard]] constexpr float metresY(PitchPoint point) const
  {
    return point.y * width;
  }
  /** The x coordinate seen from the other end (1 - x as a fraction). */
  [[nodiscard]] constexpr float mirrorX(float units) const
  {
    return 1.0f - units;
  }
  /** The y coordinate seen from the other touchline (1 - y as a fraction). */
  [[nodiscard]] constexpr float mirrorY(float units) const
  {
    return 1.0f - units;
  }

  /** A height in point units (pitch lengths of BALL_Z_METRES) in metres. */
  [[nodiscard]] static constexpr float heightMetres(float z)
  {
    return z * MatchTuning::Units::BALL_Z_METRES;
  }
  /** A height in metres in point units. */
  [[nodiscard]] static constexpr float heightUnits(float metres)
  {
    return metres / MatchTuning::Units::BALL_Z_METRES;
  }
  /** A height given in pitch lengths of BALL_Z_METRES (the old unit, as
   * some tuning is), in point units. */
  [[nodiscard]] static constexpr float atZ(float lengths) { return lengths; }
  /** A height in point units as pitch lengths of BALL_Z_METRES. */
  [[nodiscard]] static constexpr float fractionZ(float z) { return z; }
};
