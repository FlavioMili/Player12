// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "model/match_point.h"

/**
 * The kit laid out on an engine's pitch: cones, lines and marked areas
 * (training ground plan sections 4 and 5.2). Each has a name so a drill's
 * rules can refer to it. The engine holds them and the views draw them;
 * nothing collides with them yet (solid kit comes with the dummies, T5).
 *
 * Points are metres on the engine's pitch. Colours are packed as ImGui's
 * IM_COL32 (0xAABBGGRR), so the views pass them straight through.
 */
namespace PitchKit
{
/** A flat marker disc, or a tall cone. */
enum class ConeKind : std::uint8_t
{
  FLAT,
  TALL
};

/** A cone's size (m). Not researched: common equipment ranges, not a
 * standard (plan section 18). A flat disc is about 20 cm across and 5 cm
 * high; a tall cone about 23 cm high. */
inline constexpr float CONE_RADIUS_METRES = 0.1f;
inline constexpr float FLAT_CONE_HEIGHT_METRES = 0.05f;
inline constexpr float TALL_CONE_HEIGHT_METRES = 0.23f;

struct Cone
{
  std::string name;
  PitchPoint at{};
  ConeKind kind = ConeKind::FLAT;
  std::uint32_t colour = 0xFF0080FFu;
};

/** A line on the grass (a painted or rope line), from one point to
 * another. */
struct Line
{
  std::string name;
  PitchPoint from{};
  PitchPoint to{};
  std::uint32_t colour = 0xFFFFFFFFu;
};

/** A marked rectangle (an end zone, a channel, a box), corner to corner. */
struct Area
{
  std::string name;
  PitchPoint min{};
  PitchPoint max{};
  std::uint32_t colour = 0xFFFFFFFFu;
};

/** Everything laid out on one pitch. */
struct Layout
{
  std::vector<Cone> cones;
  std::vector<Line> lines;
  std::vector<Area> areas;

  [[nodiscard]] bool empty() const
  {
    return cones.empty() && lines.empty() && areas.empty();
  }
  void clear()
  {
    cones.clear();
    lines.clear();
    areas.clear();
  }
};
}  // namespace PitchKit
