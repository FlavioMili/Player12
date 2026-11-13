// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include "model/match_ground.h"
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
};
