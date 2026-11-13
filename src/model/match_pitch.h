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
  constexpr MatchPitch(float lengthMetres, float widthMetres)
      : length(lengthMetres),
        width(widthMetres),
        inverseLength(1.0f / lengthMetres),
        inverseWidth(1.0f / widthMetres),
        bodyMinX(-(MatchTuning::Pitch::BOARDS_END_METRES -
                   MatchTuning::Body::RADIUS_METRES)),
        bodyMaxX(lengthMetres - bodyMinX),
        bodyMinY(-(MatchTuning::Pitch::BOARDS_SIDE_METRES -
                   MatchTuning::Body::RADIUS_METRES)),
        bodyMaxY(widthMetres - bodyMinY),
        ground(lengthMetres, widthMetres)
  {
  }

  /** Metres, goal line to goal line and touchline to touchline. */
  float length;
  float width;
  /** 1 / length and 1 / width: metres to pitch fractions. */
  float inverseLength;
  float inverseWidth;
  /** Inside the boards, his body touching them (metres): where a player
   * may mean to go to fetch a ball (MatchEngine::clampToBoards). His body
   * can go further, through the gaps in the boards (moveBody). */
  float bodyMinX;
  float bodyMaxX;
  float bodyMinY;
  float bodyMaxY;
  /** The furniture round this pitch (metres): tunnel, dugouts, technical
   * areas, boards and the stand's front wall. */
  MatchGround::Layout ground;

  // ---------------------------------------------------------------------
  // Point units (training ground T1 stage 2). A PitchPoint's coordinates
  // and the ball's height are metres. Tuning given as a fraction of the
  // pitch goes through `at`/`atX`/`atY` (and back through `fraction`);
  // stage 4 decides each of those.
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
};
