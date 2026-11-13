// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>

#include "model/match_tuning.h"

/**
 * The touchline furniture as shapes, built from MatchTuning::Pitch and a
 * pitch's size: the tunnel, the dugouts, the technical areas and the gaps
 * they leave in the boards and the stand's front wall. The engine's bodies
 * and dead ball read their own pitch's (MatchPitch::ground); both match
 * views read the match pitch's (MatchGround::Match), which is the same
 * shapes, so all of them agree.
 *
 * Metres: x from the left goal line, y from the top touchline (the same as
 * the engine's points). Everything here is on the
 * bottom touchline (y = width).
 */
namespace MatchGround
{
/** An upright face (or a stretch of one) along x, from `from` to `to`. */
struct Span
{
  float from = 0.0f;
  float to = 0.0f;
};

struct Rect
{
  float x0 = 0.0f;
  float y0 = 0.0f;
  float x1 = 0.0f;
  float y1 = 0.0f;

  [[nodiscard]] constexpr bool contains(float x, float y) const
  {
    return x >= x0 && x <= x1 && y >= y0 && y <= y1;
  }
};

/** The furniture round a pitch of one size. */
struct Layout
{
  constexpr Layout(float lengthMetres, float widthMetres)
      : length(lengthMetres),
        width(widthMetres),
        halfway(0.5f * lengthMetres),
        boardsY(widthMetres + MatchTuning::Pitch::BOARDS_SIDE_METRES),
        standY(widthMetres + MatchTuning::Pitch::STAND_SIDE_METRES),
        tunnel{halfway - 0.5f * MatchTuning::Pitch::TUNNEL_WIDTH_METRES, standY,
               halfway + 0.5f * MatchTuning::Pitch::TUNNEL_WIDTH_METRES,
               standY + MatchTuning::Pitch::TUNNEL_DEPTH_METRES},
        dugouts{{{halfway - MatchTuning::Pitch::DUGOUT_FROM_HALFWAY_METRES -
                      MatchTuning::Pitch::DUGOUT_LENGTH_METRES,
                  standY - MatchTuning::Pitch::DUGOUT_DEPTH_METRES,
                  halfway - MatchTuning::Pitch::DUGOUT_FROM_HALFWAY_METRES, standY},
                 {halfway + MatchTuning::Pitch::DUGOUT_FROM_HALFWAY_METRES,
                  standY - MatchTuning::Pitch::DUGOUT_DEPTH_METRES,
                  halfway + MatchTuning::Pitch::DUGOUT_FROM_HALFWAY_METRES +
                      MatchTuning::Pitch::DUGOUT_LENGTH_METRES,
                  standY}}},
        technicalAreas{
            {{dugouts[0].x0 - MatchTuning::Pitch::TECHNICAL_AREA_SIDE_METRES,
              widthMetres + MatchTuning::Pitch::TECHNICAL_AREA_FROM_TOUCHLINE_METRES,
              dugouts[0].x1 + MatchTuning::Pitch::TECHNICAL_AREA_SIDE_METRES,
              dugouts[0].y0},
             {dugouts[1].x0 - MatchTuning::Pitch::TECHNICAL_AREA_SIDE_METRES,
              widthMetres + MatchTuning::Pitch::TECHNICAL_AREA_FROM_TOUCHLINE_METRES,
              dugouts[1].x1 + MatchTuning::Pitch::TECHNICAL_AREA_SIDE_METRES,
              dugouts[1].y0}}},
        bottomBoards{
            {{-MatchTuning::Pitch::BOARDS_END_METRES, technicalAreas[0].x0},
             {technicalAreas[0].x1, tunnel.x0},
             {tunnel.x1, technicalAreas[1].x0},
             {technicalAreas[1].x1,
              lengthMetres + MatchTuning::Pitch::BOARDS_END_METRES}}},
        bottomStandWall{
            {{-MatchTuning::Pitch::STAND_END_METRES, tunnel.x0},
             {tunnel.x1, lengthMetres + MatchTuning::Pitch::STAND_END_METRES}}},
        fourthOfficialX(halfway),
        fourthOfficialY(widthMetres + MatchTuning::Pitch::FOURTH_OFFICIAL_OUT_METRES)
  {
  }

  /** The pitch's size, and its halfway line (x). */
  float length;
  float width;
  float halfway;
  /** The bottom boards' pitch side and the bottom stand's front wall. */
  float boardsY;
  float standY;
  /** The tunnel: its mouth in the stand's front wall on the halfway line,
   * running back under the stand. */
  Rect tunnel;
  /** The dugouts, home (left) then away: backed onto the stand wall, the
   * front (y0) facing the pitch, open at both ends. */
  std::array<Rect, 2> dugouts;
  /** Each side's technical area: its dugout and 1 m either side of it, out
   * to 1 m from the touchline (y0, the dashed line). */
  std::array<Rect, 2> technicalAreas;
  /** The bottom boards (at boardsY), open in front of the technical areas
   * and the tunnel's walkway. */
  std::array<Span, 4> bottomBoards;
  /** The bottom stand's front wall (at standY), open at the tunnel mouth. */
  std::array<Span, 2> bottomStandWall;
  /** The fourth official's place, between the technical areas. */
  float fourthOfficialX;
  float fourthOfficialY;
};

/** The height of a dugout's roof this far out (y, metres), sloping down
 * from the back to the front. */
[[nodiscard]] constexpr float dugoutRoofHeight(const Rect& dugout, float y)
{
  using P = MatchTuning::Pitch;
  const float share = (y - dugout.y0) / (dugout.y1 - dugout.y0);
  const float clamped = share < 0.0f ? 0.0f : share > 1.0f ? 1.0f : share;
  return P::DUGOUT_ROOF_FRONT_METRES +
         (P::DUGOUT_ROOF_BACK_METRES - P::DUGOUT_ROOF_FRONT_METRES) * clamped;
}

/**
 * The match pitch's furniture (105 x 68 m), for the match views and
 * anything else that draws or reads a match from outside the engine. The
 * engine reads its own pitch's layout, never these.
 */
namespace Match
{
using MatchGround::dugoutRoofHeight;
using MatchGround::Rect;
using MatchGround::Span;

inline constexpr Layout LAYOUT{MatchTuning::Pitch::MATCH_LENGTH_METRES,
                               MatchTuning::Pitch::MATCH_WIDTH_METRES};
inline constexpr float LENGTH = LAYOUT.length;
inline constexpr float WIDTH = LAYOUT.width;
inline constexpr float HALFWAY = LAYOUT.halfway;
inline constexpr float BOARDS_Y = LAYOUT.boardsY;
inline constexpr float STAND_Y = LAYOUT.standY;
inline constexpr Rect TUNNEL = LAYOUT.tunnel;
inline constexpr std::array<Rect, 2> DUGOUTS = LAYOUT.dugouts;
inline constexpr std::array<Rect, 2> TECHNICAL_AREAS = LAYOUT.technicalAreas;
inline constexpr std::array<Span, 4> BOTTOM_BOARDS = LAYOUT.bottomBoards;
inline constexpr std::array<Span, 2> BOTTOM_STAND_WALL = LAYOUT.bottomStandWall;
inline constexpr float FOURTH_OFFICIAL_X = LAYOUT.fourthOfficialX;
inline constexpr float FOURTH_OFFICIAL_Y = LAYOUT.fourthOfficialY;
static_assert(TECHNICAL_AREAS[0].x1 < TUNNEL.x0 &&
                  TUNNEL.x1 < TECHNICAL_AREAS[1].x0,
              "the tunnel's walkway sits between the technical areas");
}  // namespace Match
}  // namespace MatchGround
