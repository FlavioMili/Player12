// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "gui/render/match_kit_colors.h"
#include "gui/render/match_render_math.h"

/**
 * Static world-space geometry of the 3D match view: ground, markings, goals,
 * corner flags, advertising boards, stands with crowd, and floodlights.
 *
 * Everything is built once per match (the crowd needs the kit colours) and
 * only re-projected per frame by `MatchRenderer3D`. No RNG is involved: all
 * variation comes from a fixed integer hash so the stadium is identical on
 * every frame and every run.
 */
namespace Stadium3D
{
using RenderMath::Vec3;

/**
 * Convex ground polygon (drawn in fixed order, no depth sorting). Drawn
 * without anti-aliasing it is a fan from the first point, so a centre
 * followed by a closed rim gives a radial gradient.
 */
struct GroundPolygon
{
  std::array<Vec3, 10> points{};
  std::array<ImU32, 10> colors{};
  std::uint8_t count = 0;
};

/**
 * The playing surface as a vertex grid: each mowing stripe owns its columns
 * of vertices (so stripes keep sharp edges) and every vertex carries its
 * floodlit, gently mottled grass colour. The renderer only tints the two
 * stripe directions per frame, since mown grass looks lighter when its
 * blades lean away from the viewer.
 */
struct PitchGrid
{
  int stripes = 0;
  int columnsPerStripe = 0;
  int rows = 0;
  std::vector<Vec3> points;
  std::vector<ImU32> colors;

  int columnVertices() const { return columnsPerStripe + 1; }
  std::size_t index(int stripe, int column, int row) const
  {
    return static_cast<std::size_t>(
        (stripe * columnVertices() + column) * (rows + 1) + row);
  }
};

/** Planar convex face of a stand or floodlight with per-corner colours. */
struct Face
{
  std::array<Vec3, 4> corners{};
  std::array<ImU32, 4> colors{};
  Vec3 normal;
  /** Crowd clumps drawn right after this face when it is visible. */
  std::uint32_t clumpBegin = 0;
  std::uint32_t clumpEnd = 0;
  /** Supporters' flags waved over this face's crowd. */
  std::uint32_t flagBegin = 0;
  std::uint32_t flagEnd = 0;
  /** Non-zero for floodlight heads: colour of the halo drawn on top. */
  ImU32 glow = 0;
};

/** Idle-motion phases a spectator can be in (a small per-frame table). */
inline constexpr std::size_t CROWD_PHASES = 16;

/** Allegiance and props of a spectator (bit flags). */
namespace CrowdFlags
{
inline constexpr std::byte HOME_FAN = std::byte{1} << 0U;
inline constexpr std::byte AWAY_FAN = std::byte{1} << 1U;
/** Jumps up when the home or the away side scores. */
inline constexpr std::byte CHEERS_HOME = std::byte{1} << 2U;
inline constexpr std::byte CHEERS_AWAY = std::byte{1} << 3U;
/** Holds a scarf in the team colours over the head when celebrating. */
inline constexpr std::byte SCARF = std::byte{1} << 4U;
}  // namespace CrowdFlags

/** One spectator: a small upright billboard on a tier. */
struct CrowdDot
{
  Vec3 base;
  ImU32 body = 0;
  ImU32 head = 0;
  std::byte flags{};
  /** Index into the per-frame motion tables (< CROWD_PHASES). */
  std::uint8_t phase = 0;
};

/** A supporter's flag on a pole, waved above the heads. */
struct CrowdFlag
{
  Vec3 base;  /**< Where the pole leaves the fan's hands. */
  Vec3 along; /**< Unit vector along the row (the cloth streams this way). */
  ImU32 primary = 0;
  ImU32 secondary = 0;
  std::byte flags{};
  std::uint8_t phase = 0;
};

/**
 * A few neighbouring spectators (up to CLUMP_SEATS x CLUMP_ROWS) drawn as
 * one quad when they are far away. The quad runs up the tier from the front
 * row to the heads of the next clump's front row, so clumps tile the stand
 * without gaps from any angle. Members are stored back row first so nearer
 * rows overlap the ones behind them.
 */
struct CrowdClump
{
  Vec3 base; /**< Bottom centre on the front row. */
  Vec3 top;  /**< Top centre, over the row behind the clump. */
  float halfWidth = 0.0f;
  /** Average colour of the members (the far-away look). */
  ImU32 color = 0;
  std::uint32_t dotBegin = 0;
  std::uint32_t dotEnd = 0;
  /** CHEERS_* bits held by most members (the far clump hops with them). */
  std::byte flags{};
  std::uint8_t phase = 0;
};

/** Which stand a section belongs to; used to hide the stand behind the eye. */
enum class Side : std::uint8_t
{
  SOUTH,
  NORTH,
  WEST,
  EAST,
  NONE,
};

/** Depth-sorted unit: faces are drawn in stored order (back to front). */
struct Section
{
  std::uint32_t faceBegin = 0;
  std::uint32_t faceEnd = 0;
  Vec3 centre;
  Side side = Side::NONE;
};

/** Pitch-facing advertising board. */
struct AdBoard
{
  Vec3 origin; /**< Bottom-left corner as seen from the pitch. */
  Vec3 right;  /**< Unit vector along the board, left to right. */
  Vec3 normal; /**< Unit vector facing the pitch. */
  std::uint8_t sponsor = 0;
};

/** Oriented box given by its eight corners (bit 0 = +a, 1 = +b, 2 = +c). */
struct Box
{
  std::array<Vec3, 8> corners{};
  ImU32 color = 0;
};

/** Bilinear net panel with its mesh resolution. */
struct NetPanel
{
  std::array<Vec3, 4> corners{};
  std::uint8_t rows = 1;
  std::uint8_t columns = 1;
};

struct Goal
{
  std::array<Box, 3> frame{}; /**< Two posts and the crossbar. */
  std::array<NetPanel, 4> nets{};
  std::array<std::array<Vec3, 2>, 2> supports{};
};

struct SponsorStyle
{
  const char* text = "";
  ImU32 background = 0;
  ImU32 foreground = 0;
};

/** Original, fictional advertisers shown on the boards. */
inline constexpr std::array<SponsorStyle, 8> SPONSORS{{
    {"KESTRELINE", IM_COL32(18, 70, 150, 255), IM_COL32(255, 255, 255, 255)},
    {"VOLTRIX", IM_COL32(236, 196, 30, 255), IM_COL32(20, 20, 24, 255)},
    {"ORBANO TELECOM", IM_COL32(200, 30, 48, 255),
     IM_COL32(255, 255, 255, 255)},
    {"PINEFORGE", IM_COL32(16, 110, 70, 255), IM_COL32(240, 250, 240, 255)},
    {"AQUALYNE", IM_COL32(20, 170, 200, 255), IM_COL32(10, 30, 60, 255)},
    {"SOLENTA", IM_COL32(245, 245, 245, 255), IM_COL32(220, 70, 20, 255)},
    {"BRIGHTWELL", IM_COL32(30, 30, 36, 255), IM_COL32(250, 210, 60, 255)},
    {"TORVANA", IM_COL32(120, 40, 150, 255), IM_COL32(255, 255, 255, 255)},
}};

/** Face index table for `Box` corners; each face winds CCW from outside. */
inline constexpr std::array<std::array<std::uint8_t, 4>, 6> BOX_FACES{{
    {0, 4, 6, 2},
    {1, 3, 7, 5},
    {0, 1, 5, 4},
    {2, 6, 7, 3},
    {0, 2, 3, 1},
    {4, 5, 7, 6},
}};

/** Builds an axis-aligned box from its minimum and maximum corners. */
Box makeAxisBox(Vec3 minimum, Vec3 maximum, ImU32 color);

struct Geometry
{
  std::vector<GroundPolygon> ground;
  PitchGrid pitch;
  /** Soft worn patches (goalmouths, spots, centre) over the grass. */
  std::vector<GroundPolygon> wear;
  std::vector<GroundPolygon> markings;
  std::vector<Face> faces;
  std::vector<Section> sections;
  std::vector<CrowdDot> crowd;
  std::vector<CrowdClump> clumps;
  std::vector<CrowdFlag> flags;
  std::vector<AdBoard> boards;
  std::array<Goal, 2> goals{};
  std::array<Vec3, 4> cornerFlags{};
  /** Daylight build: sunlit and shaded stands, lamps off. */
  bool day = false;
  /** By day: the roofs' shadows on the ground (drawn over the grass) and
   * the same areas as plain quads for testing who stands in them. */
  std::vector<GroundPolygon> standShadows;
  std::vector<std::array<Vec3, 4>> shadowAreas;

  /**
   * Rebuilds every static element; the crowd wears the given kits. The
   * lighting is baked into the colours: floodlights at night, the sun (with
   * the roofs' shadows) by day.
   */
  void build(const MatchKits& kits, bool daylightView = false);
  /** Whether a ground point lies in a stand's shadow (always false at
   * night). */
  bool inStandShadow(Vec3 point) const;
};
}  // namespace Stadium3D
