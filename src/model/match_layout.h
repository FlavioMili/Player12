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
 * cones, lines and areas are drawn only; dummies are solid (T5).
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

/** A training dummy (plan section 21): a free-kick mannequin, a junior
 * one, or an agility pole. */
enum class DummyKind : std::uint8_t
{
  MANNEQUIN,
  JUNIOR_MANNEQUIN,
  POLE
};

/** A dummy's solid shape: an upright cylinder of `radius` with a domed top
 * (a half-sphere of the same radius), `height` tall overall (metres). */
struct DummyShape
{
  float radius = 0.0f;
  float height = 0.0f;
};

/** The sizes, from makers' listings (plan section 21): a senior mannequin
 * 175-183 cm tall and 45-52 cm wide, a junior 115-122 cm and 33-34 cm, a
 * pole 150-172 cm and 25-34 mm; the middles of the ranges. */
[[nodiscard]] constexpr DummyShape dummyShape(DummyKind kind)
{
  switch (kind)
  {
    case DummyKind::MANNEQUIN:
      return {0.25f, 1.8f};
    case DummyKind::JUNIOR_MANNEQUIN:
      return {0.17f, 1.2f};
    case DummyKind::POLE:
      return {0.015f, 1.6f};
  }
  return {0.25f, 1.8f};
}

/** Solid kit: the ball bounces off it and players go round it (plan
 * section 21). Static. `facing` is the way its front faces (a true angle,
 * as a player's), for how it is drawn. */
struct Dummy
{
  std::string name;
  PitchPoint at{};
  DummyKind kind = DummyKind::MANNEQUIN;
  float facing = 0.0f;
  std::uint32_t colour = 0xFF1EC8FFu;
};

/** Everything laid out on one pitch. */
struct Layout
{
  std::vector<Cone> cones;
  std::vector<Line> lines;
  std::vector<Area> areas;
  std::vector<Dummy> dummies;

  [[nodiscard]] bool empty() const
  {
    return cones.empty() && lines.empty() && areas.empty() && dummies.empty();
  }
  void clear()
  {
    cones.clear();
    lines.clear();
    areas.clear();
    dummies.clear();
  }
};
}  // namespace PitchKit
