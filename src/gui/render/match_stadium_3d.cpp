// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/render/match_stadium_3d.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

#include "gui/render/match_render_3d_tuning.h"

namespace Stadium3D
{
namespace
{
using Tuning = MatchRender3DTuning;

constexpr float LENGTH = MatchTuning::Pitch::LENGTH_METRES;
constexpr float WIDTH = MatchTuning::Pitch::WIDTH_METRES;
constexpr float HALF_LENGTH = LENGTH * 0.5f;
constexpr float HALF_WIDTH = WIDTH * 0.5f;
constexpr Vec3 UP{0.0f, 0.0f, 1.0f};
constexpr float TWO_PI = 2.0f * std::numbers::pi_v<float>;

/// Floodlit falloff at night (brightest at the centre spot, darker in the
/// corners); daylight is even.
float groundLight(float x, float y, bool day)
{
  if (day) return 1.0f;
  const float dx = (x - HALF_LENGTH) / HALF_LENGTH;
  const float dy = (y - HALF_WIDTH) / HALF_WIDTH;
  return std::max(0.62f, 1.0f - Tuning::Grass::VIGNETTE_LENGTH * dx * dx -
                             Tuning::Grass::VIGNETTE_WIDTH * dy * dy);
}

/// Unit vector towards the sun.
Vec3 sunDirection()
{
  using D = Tuning::Day;
  return RenderMath::normalize({D::SUN_X, D::SUN_Y, D::SUN_Z});
}

/// Daylight on a surface: sky fill plus the sun when it faces it.
float sunlight(Vec3 normal, bool shaded)
{
  using D = Tuning::Day;
  const float sun =
      shaded ? 0.0f : std::max(0.0f, RenderMath::dot(normal, sunDirection()));
  return D::STAND_AMBIENT + D::STAND_DIFFUSE * sun +
         D::STAND_SKY * std::max(0.0f, normal.z);
}

float unitHash(std::uint32_t a, std::uint32_t b, std::uint32_t c)
{
  const std::uint32_t mixed =
      cosmeticHash(a * 0x9e3779b1U ^
                   cosmeticHash(b * 0x85ebca77U ^ (c + Tuning::Crowd::SEED)));
  return static_cast<float>(mixed & 0xFFFFU) / 65536.0f;
}

/// Smooth value noise in [0, 1): bilinear blend of hashed lattice values.
float valueNoise(float x, float y, std::uint32_t salt)
{
  const float cellX = std::floor(x);
  const float cellY = std::floor(y);
  const auto lattice = [salt](float cx, float cy)
  {
    return unitHash(static_cast<std::uint32_t>(static_cast<std::int32_t>(cx)),
                    static_cast<std::uint32_t>(static_cast<std::int32_t>(cy)),
                    salt);
  };
  const auto smooth = [](float t) { return t * t * (3.0f - 2.0f * t); };
  const float fx = smooth(x - cellX);
  const float fy = smooth(y - cellY);
  const float bottom =
      lattice(cellX, cellY) +
      (lattice(cellX + 1.0f, cellY) - lattice(cellX, cellY)) * fx;
  const float top =
      lattice(cellX, cellY + 1.0f) +
      (lattice(cellX + 1.0f, cellY + 1.0f) - lattice(cellX, cellY + 1.0f)) * fx;
  return std::lerp(bottom, top, fy);
}

void addRectangle(std::vector<GroundPolygon>& target, float x0, float y0,
                  float x1, float y1, ImU32 color, bool lit, bool day)
{
  GroundPolygon polygon;
  polygon.count = 4;
  polygon.points[0] = {x0, y0, 0.0f};
  polygon.points[1] = {x1, y0, 0.0f};
  polygon.points[2] = {x1, y1, 0.0f};
  polygon.points[3] = {x0, y1, 0.0f};
  for (std::size_t index = 0; index < 4; ++index)
  {
    polygon.colors[index] =
        lit ? shadeColor(color, groundLight(polygon.points[index].x,
                                            polygon.points[index].y, day))
            : color;
  }
  target.push_back(polygon);
}

void addLine(std::vector<GroundPolygon>& target, float x0, float y0, float x1,
             float y1)
{
  const float halfWidth = Tuning::Markings::LINE_WIDTH * 0.5f;
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const float lineLength = std::sqrt(dx * dx + dy * dy);
  const float ux = dx / lineLength;
  const float uy = dy / lineLength;
  // Extend by half a width so perpendicular lines meet with square corners.
  x0 -= ux * halfWidth;
  y0 -= uy * halfWidth;
  x1 += ux * halfWidth;
  y1 += uy * halfWidth;
  GroundPolygon polygon;
  polygon.count = 4;
  polygon.points[0] = {x0 - uy * halfWidth, y0 + ux * halfWidth, 0.0f};
  polygon.points[1] = {x1 - uy * halfWidth, y1 + ux * halfWidth, 0.0f};
  polygon.points[2] = {x1 + uy * halfWidth, y1 - ux * halfWidth, 0.0f};
  polygon.points[3] = {x0 + uy * halfWidth, y0 - ux * halfWidth, 0.0f};
  polygon.colors.fill(Tuning::Markings::COLOR);
  target.push_back(polygon);
}

void addArc(std::vector<GroundPolygon>& target, float cx, float cy,
            float radius, float startAngle, float endAngle, int segments)
{
  const float inner = radius - Tuning::Markings::LINE_WIDTH * 0.5f;
  const float outer = radius + Tuning::Markings::LINE_WIDTH * 0.5f;
  for (int segment = 0; segment < segments; ++segment)
  {
    const float a0 = startAngle + (endAngle - startAngle) *
                                      static_cast<float>(segment) /
                                      static_cast<float>(segments);
    const float a1 = startAngle + (endAngle - startAngle) *
                                      static_cast<float>(segment + 1) /
                                      static_cast<float>(segments);
    GroundPolygon polygon;
    polygon.count = 4;
    polygon.points[0] = {cx + std::cos(a0) * inner, cy + std::sin(a0) * inner,
                         0.0f};
    polygon.points[1] = {cx + std::cos(a0) * outer, cy + std::sin(a0) * outer,
                         0.0f};
    polygon.points[2] = {cx + std::cos(a1) * outer, cy + std::sin(a1) * outer,
                         0.0f};
    polygon.points[3] = {cx + std::cos(a1) * inner, cy + std::sin(a1) * inner,
                         0.0f};
    polygon.colors.fill(Tuning::Markings::COLOR);
    target.push_back(polygon);
  }
}

void addSpot(std::vector<GroundPolygon>& target, float cx, float cy)
{
  GroundPolygon polygon;
  polygon.count = static_cast<std::uint8_t>(Tuning::Markings::SPOT_SEGMENTS);
  for (std::size_t index = 0; index < polygon.count; ++index)
  {
    const float angle =
        TWO_PI * static_cast<float>(index) / static_cast<float>(polygon.count);
    polygon.points[index] = {
        cx + std::cos(angle) * Tuning::Markings::SPOT_RADIUS,
        cy + std::sin(angle) * Tuning::Markings::SPOT_RADIUS, 0.0f};
  }
  polygon.colors.fill(Tuning::Markings::COLOR);
  target.push_back(polygon);
}

void buildGround(Geometry& geometry, bool day)
{
  const float outside = Tuning::Grass::OUTSIDE_EXTENT;
  addRectangle(geometry.ground, -outside, -outside, LENGTH + outside,
               WIDTH + outside,
               day ? Tuning::Day::OUTSIDE_COLOR : Tuning::Grass::OUTSIDE_COLOR,
               false, day);

  // Surround split in a coarse grid so the lighting gradient carries on.
  constexpr int SURROUND_CELLS = 4;
  const float sx0 = -Tuning::Grass::SURROUND_X;
  const float sy0 = -Tuning::Grass::SURROUND_Y;
  const float sx1 = LENGTH + Tuning::Grass::SURROUND_X;
  const float sy1 = WIDTH + Tuning::Grass::SURROUND_Y;
  for (int column = 0; column < SURROUND_CELLS; ++column)
  {
    for (int row = 0; row < SURROUND_CELLS; ++row)
    {
      const float x0 = sx0 + (sx1 - sx0) * static_cast<float>(column) /
                                 static_cast<float>(SURROUND_CELLS);
      const float x1 = sx0 + (sx1 - sx0) * static_cast<float>(column + 1) /
                                 static_cast<float>(SURROUND_CELLS);
      const float y0 = sy0 + (sy1 - sy0) * static_cast<float>(row) /
                                 static_cast<float>(SURROUND_CELLS);
      const float y1 = sy0 + (sy1 - sy0) * static_cast<float>(row + 1) /
                                 static_cast<float>(SURROUND_CELLS);
      addRectangle(geometry.ground, x0, y0, x1, y1,
                   Tuning::Grass::SURROUND_COLOR, true, day);
    }
  }

  // Mowing stripes across the length; vertex colours carry the floodlight
  // falloff and two octaves of low-frequency mottling.
  using G = Tuning::Grass;
  PitchGrid& grid = geometry.pitch;
  grid.stripes = G::STRIPES;
  grid.columnsPerStripe = G::STRIPE_COLUMNS;
  grid.rows = G::ROWS;
  grid.points.clear();
  grid.colors.clear();
  const auto vertices = static_cast<std::size_t>(
      grid.stripes * grid.columnVertices() * (grid.rows + 1));
  grid.points.reserve(vertices);
  grid.colors.reserve(vertices);
  const float stripeLength = LENGTH / static_cast<float>(grid.stripes);
  for (int stripe = 0; stripe < grid.stripes; ++stripe)
  {
    for (int column = 0; column <= grid.columnsPerStripe; ++column)
    {
      const float x =
          stripeLength * (static_cast<float>(stripe) +
                          static_cast<float>(column) /
                              static_cast<float>(grid.columnsPerStripe));
      for (int row = 0; row <= grid.rows; ++row)
      {
        const float y =
            WIDTH * static_cast<float>(row) / static_cast<float>(grid.rows);
        const float mottle =
            (valueNoise(x / G::NOISE_METRES, y / G::NOISE_METRES, 31U) - 0.5f) *
                G::NOISE_STRENGTH +
            (valueNoise(x / G::FINE_NOISE_METRES, y / G::FINE_NOISE_METRES,
                        37U) -
             0.5f) *
                G::FINE_NOISE_STRENGTH;
        grid.points.push_back({x, y, 0.0f});
        grid.colors.push_back(shadeColor(
            G::PITCH_COLOR, groundLight(x, y, day) * (1.0f + mottle)));
      }
    }
  }
}

/// Irregular blob of worn grass fading out to its rim.
void addWear(std::vector<GroundPolygon>& target, float cx, float cy,
             float radiusX, float radiusY, std::uint8_t alpha,
             std::uint32_t salt)
{
  GroundPolygon polygon;
  constexpr std::size_t RIM = 8;
  polygon.count = static_cast<std::uint8_t>(RIM + 2);
  polygon.points[0] = {cx, cy, 0.0f};
  polygon.colors[0] = withAlpha(Tuning::Grass::WEAR_COLOR, alpha);
  for (std::size_t index = 0; index <= RIM; ++index)
  {
    const std::size_t corner = index % RIM;
    const float angle =
        TWO_PI * static_cast<float>(corner) / static_cast<float>(RIM);
    const float wobble =
        1.0f +
        (unitHash(salt, static_cast<std::uint32_t>(corner), 41U) - 0.5f) * 0.4f;
    polygon.points[index + 1] = {cx + std::cos(angle) * radiusX * wobble,
                                 cy + std::sin(angle) * radiusY * wobble, 0.0f};
    polygon.colors[index + 1] = withAlpha(Tuning::Grass::WEAR_COLOR, 0);
  }
  target.push_back(polygon);
}

void buildWear(Geometry& geometry)
{
  using W = Tuning::Grass;
  auto& wear = geometry.wear;
  std::uint32_t salt = 0;
  // Centre spot: kick-offs scuff a small patch.
  addWear(wear, HALF_LENGTH, HALF_WIDTH, W::SPOT_WEAR_RADIUS * 1.3f,
          W::SPOT_WEAR_RADIUS, W::WEAR_ALPHA, ++salt);
  for (const float goalLine : {0.0f, LENGTH})
  {
    const float inward = goalLine == 0.0f ? 1.0f : -1.0f;
    // The keeper's patch in the goalmouth is the most worn ground there is.
    addWear(wear, goalLine + inward * W::GOALMOUTH_DEPTH, HALF_WIDTH,
            W::GOALMOUTH_DEPTH * 0.8f, W::GOALMOUTH_WIDTH, W::WEAR_ALPHA,
            ++salt);
    addWear(wear, goalLine + inward * Tuning::Markings::PENALTY_SPOT_DISTANCE,
            HALF_WIDTH, W::SPOT_WEAR_RADIUS, W::SPOT_WEAR_RADIUS * 0.8f,
            W::WEAR_ALPHA, ++salt);
    // The goal area takes a beating in front of the six-yard line too.
    addWear(wear, goalLine + inward * Tuning::Markings::GOAL_AREA_DEPTH,
            HALF_WIDTH, W::GOALMOUTH_DEPTH * 1.2f, W::GOALMOUTH_WIDTH * 1.5f,
            W::WEAR_ALPHA / 2, ++salt);
  }
  // The assistant referees' running lines just outside the touchlines, one
  // half each (diagonal system), in several overlapping scuffs.
  for (int scuff = 0; scuff < W::LINESMAN_SCUFFS; ++scuff)
  {
    const float share = (static_cast<float>(scuff) + 0.5f) /
                        static_cast<float>(W::LINESMAN_SCUFFS);
    const float length = HALF_LENGTH / static_cast<float>(W::LINESMAN_SCUFFS);
    addWear(wear, HALF_LENGTH + share * HALF_LENGTH, -W::LINESMAN_OFFSET,
            length * 0.75f, W::LINESMAN_HALF_WIDTH, W::LINESMAN_ALPHA, ++salt);
    addWear(wear, share * HALF_LENGTH, WIDTH + W::LINESMAN_OFFSET,
            length * 0.75f, W::LINESMAN_HALF_WIDTH, W::LINESMAN_ALPHA, ++salt);
  }
}

void buildMarkings(Geometry& geometry)
{
  auto& lines = geometry.markings;
  addLine(lines, 0.0f, 0.0f, LENGTH, 0.0f);
  addLine(lines, 0.0f, WIDTH, LENGTH, WIDTH);
  addLine(lines, 0.0f, 0.0f, 0.0f, WIDTH);
  addLine(lines, LENGTH, 0.0f, LENGTH, WIDTH);
  addLine(lines, HALF_LENGTH, 0.0f, HALF_LENGTH, WIDTH);
  addArc(lines, HALF_LENGTH, HALF_WIDTH, Tuning::Markings::CIRCLE_RADIUS, 0.0f,
         TWO_PI, Tuning::Markings::CIRCLE_SEGMENTS);
  addSpot(lines, HALF_LENGTH, HALF_WIDTH);

  const float arcHalfAngle =
      std::acos((Tuning::Markings::PENALTY_AREA_DEPTH -
                 Tuning::Markings::PENALTY_SPOT_DISTANCE) /
                Tuning::Markings::CIRCLE_RADIUS);
  for (const float goalLine : {0.0f, LENGTH})
  {
    const float inward = goalLine == 0.0f ? 1.0f : -1.0f;
    const auto addBox = [&](float depth, float width)
    {
      const float x = goalLine + inward * depth;
      const float y0 = HALF_WIDTH - width * 0.5f;
      const float y1 = HALF_WIDTH + width * 0.5f;
      addLine(lines, goalLine, y0, x, y0);
      addLine(lines, goalLine, y1, x, y1);
      addLine(lines, x, y0, x, y1);
    };
    addBox(Tuning::Markings::PENALTY_AREA_DEPTH,
           Tuning::Markings::PENALTY_AREA_WIDTH);
    addBox(Tuning::Markings::GOAL_AREA_DEPTH,
           Tuning::Markings::GOAL_AREA_WIDTH);
    const float spotX =
        goalLine + inward * Tuning::Markings::PENALTY_SPOT_DISTANCE;
    addSpot(lines, spotX, HALF_WIDTH);
    const float facing = inward > 0.0f ? 0.0f : std::numbers::pi_v<float>;
    addArc(lines, spotX, HALF_WIDTH, Tuning::Markings::CIRCLE_RADIUS,
           facing - arcHalfAngle, facing + arcHalfAngle,
           Tuning::Markings::PENALTY_ARC_SEGMENTS);
  }

  const float quarter = std::numbers::pi_v<float> * 0.5f;
  const std::array<std::array<float, 3>, 4> corners{{
      {0.0f, 0.0f, 0.0f},
      {LENGTH, 0.0f, quarter},
      {LENGTH, WIDTH, 2.0f * quarter},
      {0.0f, WIDTH, 3.0f * quarter},
  }};
  for (const auto& corner : corners)
  {
    addArc(lines, corner[0], corner[1], Tuning::Markings::CORNER_ARC_RADIUS,
           corner[2], corner[2] + quarter,
           Tuning::Markings::CORNER_ARC_SEGMENTS);
  }
}

struct ProfilePoint
{
  float d = 0.0f;
  float z = 0.0f;
};

/**
 * Ground footprint of one stand section. Every seating row at depth `d`
 * (metres away from the pitch) runs from `left(d)` to `right(d)`. Straight
 * sections move both ends along the stand's outward axis; corner sections
 * fan out from the shared front corner along both neighbouring stands.
 */
struct SectionShape
{
  Vec3 leftOrigin;
  Vec3 leftStep;
  Vec3 rightOrigin;
  Vec3 rightStep;
  /** Horizontal unit vector pointing away from the pitch. */
  Vec3 outward;
  /** Horizontal distance along `outward` per metre of depth. */
  float run = 1.0f;
  Side side = Side::NONE;
  bool awayEnd = false;

  Vec3 left(ProfilePoint point) const
  {
    return leftOrigin + leftStep * point.d + UP * point.z;
  }
  Vec3 right(ProfilePoint point) const
  {
    return rightOrigin + rightStep * point.d + UP * point.z;
  }
};

class StandBuilder
{
 public:
  StandBuilder(Geometry& target, const MatchKits& matchKits, bool daylightView)
      : geometry(target), kits(matchKits), day(daylightView)
  {
  }

  /// Straight stand split into equal sections along `along`.
  void buildStand(Vec3 origin, Vec3 along, Vec3 outward, float length,
                  int sections, Side side)
  {
    const float sectionLength = length / static_cast<float>(sections);
    for (int index = 0; index < sections; ++index)
    {
      SectionShape shape;
      shape.leftOrigin =
          origin + along * (sectionLength * static_cast<float>(index));
      shape.rightOrigin = shape.leftOrigin + along * sectionLength;
      shape.leftStep = outward;
      shape.rightStep = outward;
      shape.outward = outward;
      shape.side = side;
      shape.awayEnd = side == Side::EAST;
      buildSection(shape);
    }
  }

  /// Corner infill joining two perpendicular stands at their front corner.
  void buildCorner(Vec3 corner, Vec3 outwardA, Vec3 outwardB, Side side)
  {
    SectionShape shape;
    shape.leftOrigin = corner;
    shape.rightOrigin = corner;
    shape.leftStep = outwardA;
    shape.rightStep = outwardB;
    shape.outward = RenderMath::normalize(outwardA + outwardB);
    shape.run = RenderMath::dot(outwardA, shape.outward);
    shape.side = side;
    buildSection(shape);
  }

 private:
  Geometry& geometry;
  const MatchKits& kits;
  bool day = false;
  std::uint32_t sectionCounter = 0;

  /// By day: whether the roof (or the stand itself, with the sun behind
  /// it) keeps the sun off this point of the section.
  static bool inRoofShadow(const SectionShape& shape, ProfilePoint point)
  {
    using S = Tuning::Stands;
    const Vec3 sun = sunDirection();
    const float toward = -(sun.x * shape.outward.x + sun.y * shape.outward.y);
    if (toward <= 0.05f) return true;
    const float roofHeight = S::ROOF_FRONT_HEIGHT - S::FASCIA_HEIGHT;
    if (point.z >= roofHeight) return false;
    // Follow the ray towards the sun up to the roof's height: under the
    // roof it is blocked, in front of the fascia it is open sky.
    const float travel =
        toward * (roofHeight - point.z) / std::max(sun.z, 0.1f);
    return point.d - travel / std::max(shape.run, 0.3f) > S::ROOF_FRONT_DEPTH;
  }

  /// Night colours stay as given (floodlit); by day the day colour is lit
  /// by the sun unless the point is in the roof's shadow.
  ImU32 daylit(ImU32 nightColor, ImU32 dayColor, Vec3 normal, bool shaded) const
  {
    return day ? shadeColor(dayColor, sunlight(normal, shaded)) : nightColor;
  }

  Face& addProfileFace(const SectionShape& shape, ProfilePoint from,
                       ProfilePoint to, ImU32 nearColor, ImU32 farColor,
                       float inset = 0.0f)
  {
    Face& face = geometry.faces.emplace_back();
    const auto trimmed = [&](ProfilePoint point, bool rightEnd)
    {
      const Vec3 left = shape.left(point);
      const Vec3 right = shape.right(point);
      const float rowLength = RenderMath::length(right - left);
      const float t = rowLength > 2.0f * inset ? inset / rowLength : 0.0f;
      return RenderMath::lerp(left, right, rightEnd ? 1.0f - t : t);
    };
    face.corners = {trimmed(from, false), trimmed(from, true),
                    trimmed(to, true), trimmed(to, false)};
    face.colors = {nearColor, nearColor, farColor, farColor};
    face.normal = RenderMath::normalize(shape.outward * (-(to.z - from.z)) +
                                        UP * ((to.d - from.d) * shape.run));
    return face;
  }

  /// By day a stand with the sun behind it throws its roof's shadow onto
  /// the ground in front: a dark band from the stand to the shadow of the
  /// fascia, with a soft edge.
  void addRoofShadow(const SectionShape& shape)
  {
    using S = Tuning::Stands;
    using D = Tuning::Day;
    const Vec3 sun = sunDirection();
    if (const float toward =
            -(sun.x * shape.outward.x + sun.y * shape.outward.y);
        toward > 0.05f)
      return;
    const ProfilePoint edge{S::ROOF_FRONT_DEPTH,
                            S::ROOF_FRONT_HEIGHT - S::FASCIA_HEIGHT};
    const auto onGround = [&](Vec3 point)
    {
      return Vec3{point.x - sun.x * point.z / sun.z,
                  point.y - sun.y * point.z / sun.z, 0.0f};
    };
    const Vec3 leftFront = shape.left({0.0f, 0.0f});
    const Vec3 rightFront = shape.right({0.0f, 0.0f});
    const Vec3 leftEdge = onGround(shape.left(edge));
    const Vec3 rightEdge = onGround(shape.right(edge));
    // Only where the shadow reaches past the front of the stand.
    if (const auto inward =
            [&](Vec3 point)
        {
          return -((point.x - leftFront.x) * shape.outward.x +
                   (point.y - leftFront.y) * shape.outward.y);
        };
        inward(leftEdge) <= 0.1f && inward(rightEdge) <= 0.1f)
      return;
    const Vec3 soft = shape.outward * D::STAND_SHADOW_PENUMBRA;
    const ImU32 dark = IM_COL32(0, 0, 0, D::STAND_SHADOW_ALPHA);
    const ImU32 clear = IM_COL32(0, 0, 0, 0);
    GroundPolygon core;
    core.count = 4;
    core.points[0] = leftFront;
    core.points[1] = rightFront;
    core.points[2] = rightEdge + soft;
    core.points[3] = leftEdge + soft;
    core.colors.fill(dark);
    geometry.standShadows.push_back(core);
    GroundPolygon fringe;
    fringe.count = 4;
    fringe.points[0] = leftEdge + soft;
    fringe.points[1] = rightEdge + soft;
    fringe.points[2] = rightEdge - soft;
    fringe.points[3] = leftEdge - soft;
    fringe.colors[0] = dark;
    fringe.colors[1] = dark;
    fringe.colors[2] = clear;
    fringe.colors[3] = clear;
    geometry.standShadows.push_back(fringe);
    geometry.shadowAreas.emplace_back(
        std::array<Vec3, 4>{leftFront, rightFront, rightEdge, leftEdge});
  }

  /// A profile face in night and day colours (`dayNear`/`dayFar` are the
  /// unlit day colours).
  Face& addLitFace(const SectionShape& shape, ProfilePoint from,
                   ProfilePoint to, ImU32 nightNear, ImU32 nightFar,
                   ImU32 dayNear, ImU32 dayFar, float inset = 0.0f)
  {
    Face& face = addProfileFace(shape, from, to, nightNear, nightFar, inset);
    if (day)
    {
      const ImU32 nearColor =
          daylit(nightNear, dayNear, face.normal, inRoofShadow(shape, from));
      const ImU32 farColor =
          daylit(nightFar, dayFar, face.normal, inRoofShadow(shape, to));
      face.colors = {nearColor, nearColor, farColor, farColor};
    }
    return face;
  }

  /// Fans in team colours gather in low-frequency blocks; everyone else
  /// wears muted everyday clothes.
  ImU32 crowdClothes(const SectionShape& shape, std::uint32_t key,
                     float teamNoise, bool& teamFan) const
  {
    using C = Tuning::Crowd;
    float teamShare = C::HOME_SHARE;
    if (shape.awayEnd)
      teamShare = C::AWAY_END_SHARE;
    else if (shape.side == Side::WEST)
      teamShare = C::HOME_END_SHARE;
    teamShare += C::SHARE_SWING * (teamNoise - 0.5f) * 2.0f;
    teamFan = unitHash(key, 3U, 0U) < teamShare;
    if (teamFan)
    {
      const KitColors& kit = shape.awayEnd ? kits.away : kits.home;
      return unitHash(key, 4U, 0U) < 0.75f ? kit.shirt : kit.trim;
    }
    constexpr std::array<ImU32, 9> NEUTRAL{
        IM_COL32(34, 40, 62, 255),  IM_COL32(40, 40, 46, 255),
        IM_COL32(92, 96, 104, 255), IM_COL32(138, 140, 146, 255),
        IM_COL32(52, 70, 104, 255), IM_COL32(96, 56, 46, 255),
        IM_COL32(70, 78, 56, 255),  IM_COL32(150, 132, 108, 255),
        IM_COL32(120, 36, 40, 255)};
    return NEUTRAL[static_cast<std::size_t>(unitHash(key, 5U, 0U) * 9.0f) %
                   NEUTRAL.size()];
  }

  /// Who a spectator cheers for and whether a scarf comes out.
  static std::byte supporterFlags(const SectionShape& shape, std::uint32_t key,
                                  bool teamFan)
  {
    using C = Tuning::Crowd;
    std::byte flags{};
    if (teamFan)
    {
      flags |= shape.awayEnd ? CrowdFlags::AWAY_FAN | CrowdFlags::CHEERS_AWAY
                             : CrowdFlags::HOME_FAN | CrowdFlags::CHEERS_HOME;
      const float scarfShare =
          shape.side == Side::WEST ? C::HOME_END_SCARF_SHARE : C::SCARF_SHARE;
      if (unitHash(key, 9U, 0U) < scarfShare) flags |= CrowdFlags::SCARF;
    }
    else if (!shape.awayEnd && unitHash(key, 10U, 0U) < C::NEUTRAL_CHEER_SHARE)
    {
      // Most of the neutral-looking crowd is local too.
      flags |= CrowdFlags::CHEERS_HOME;
    }
    return flags;
  }

  /// A few supporters wave a flag in their colours (most in the home end).
  void addFlag(const SectionShape& shape, std::uint32_t key,
               const CrowdDot& dot, Vec3 row, bool teamFan)
  {
    using C = Tuning::Crowd;
    if (!teamFan) return;
    float share = C::FLAG_SHARE;
    if (shape.awayEnd)
      share = C::AWAY_END_FLAG_SHARE;
    else if (shape.side == Side::WEST)
      share = C::HOME_END_FLAG_SHARE;
    if (unitHash(key, 11U, 0U) >= share) return;
    const KitColors& kit = shape.awayEnd ? kits.away : kits.home;
    CrowdFlag& flag = geometry.flags.emplace_back();
    flag.base = dot.base + UP * C::DOT_HEIGHT;
    flag.along = RenderMath::normalize(row) *
                 (unitHash(key, 12U, 0U) < 0.5f ? 1.0f : -1.0f);
    flag.primary = kit.shirt;
    flag.secondary = kit.trim;
    flag.flags = dot.flags;
    flag.phase = dot.phase;
  }

  /// Fills a tier with spectators grouped in clumps (back rows first) and
  /// tints the tier face with the crowd's average colour, so the stand looks
  /// full even where no spectator is drawn.
  void addCrowd(Face& face, const SectionShape& shape, std::uint32_t tier,
                ProfilePoint from, ProfilePoint to, float frontLight,
                float backLight)
  {
    using C = Tuning::Crowd;
    constexpr std::array<ImU32, 4> SKIN{
        IM_COL32(236, 196, 164, 255), IM_COL32(204, 150, 112, 255),
        IM_COL32(150, 100, 70, 255), IM_COL32(96, 64, 44, 255)};
    face.clumpBegin = static_cast<std::uint32_t>(geometry.clumps.size());
    face.flagBegin = static_cast<std::uint32_t>(geometry.flags.size());
    const auto rows = static_cast<int>((to.d - from.d) / C::ROW_DEPTH);
    const auto rowPoint = [&](int row)
    {
      const float d = from.d + (static_cast<float>(row) + 0.5f) * C::ROW_DEPTH;
      const float t = (d - from.d) / (to.d - from.d);
      return ProfilePoint{d, from.z + (to.z - from.z) * t};
    };
    std::array<float, 3> nearSum{};
    std::array<float, 3> farSum{};
    float nearCount = 0.0f;
    float farCount = 0.0f;
    const auto accumulate = [](std::array<float, 3>& sum, ImU32 color)
    {
      sum[0] += static_cast<float>((color >> IM_COL32_R_SHIFT) & 0xFFU);
      sum[1] += static_cast<float>((color >> IM_COL32_G_SHIFT) & 0xFFU);
      sum[2] += static_cast<float>((color >> IM_COL32_B_SHIFT) & 0xFFU);
    };
    const auto average = [](const std::array<float, 3>& sum, float count)
    {
      const auto channel = [count](float value)
      { return static_cast<int>(value / count + 0.5f); };
      return IM_COL32(channel(sum[0]), channel(sum[1]), channel(sum[2]), 255);
    };

    const Vec3 spectatorNormal =
        RenderMath::normalize(shape.outward * -0.8f + UP * 0.6f);
    const int lastPair = rows > 0 ? (rows - 1) / C::CLUMP_ROWS : -1;
    for (int pair = lastPair; pair >= 0; --pair)
    {
      const int firstRow = pair * C::CLUMP_ROWS;
      const int endRow = std::min(firstRow + C::CLUMP_ROWS, rows);
      const ProfilePoint front = rowPoint(firstRow);
      const float rowLength =
          RenderMath::length(shape.right(front) - shape.left(front));
      const auto seats = static_cast<int>(rowLength / C::SEAT_SPACING);
      for (int firstSeat = 0; firstSeat < seats; firstSeat += C::CLUMP_SEATS)
      {
        const int endSeat = std::min(firstSeat + C::CLUMP_SEATS, seats);
        CrowdClump clump;
        clump.dotBegin = static_cast<std::uint32_t>(geometry.crowd.size());
        int cheersHome = 0;
        int cheersAway = 0;
        std::array<float, 3> clumpSum{};
        for (int row = endRow - 1; row >= firstRow; --row)
        {
          const ProfilePoint point = rowPoint(row);
          const Vec3 left = shape.left(point);
          const Vec3 right = shape.right(point);
          const float rowT =
              (point.d - from.d) / std::max(to.d - from.d, 1e-3f);
          // Spectators face the pitch: lit by the floodlights at night, by
          // the sun (or only the sky under the roof) by day.
          const float rowLight =
              day ? sunlight(spectatorNormal, inRoofShadow(shape, point))
                  : std::lerp(frontLight, backLight, rowT);
          for (int seat = firstSeat; seat < endSeat; ++seat)
          {
            const std::uint32_t key = ((sectionCounter * 4U + tier) * 128U +
                                       static_cast<std::uint32_t>(row)) *
                                          1024U +
                                      static_cast<std::uint32_t>(seat);
            if (unitHash(key, 1U, 0U) < C::EMPTY_SEAT_RATIO) continue;
            const float jitter =
                (unitHash(key, 2U, 0U) - 0.5f) * C::JITTER * 2.0f;
            const Vec3 base =
                RenderMath::lerp(left, right,
                                 (static_cast<float>(seat) + 0.5f + jitter) /
                                     static_cast<float>(seats));
            const float noiseX =
                (base.x + base.y) / (C::NOISE_SEATS * C::SEAT_SPACING);
            const float noiseY = (point.d + static_cast<float>(tier) * 50.0f) /
                                 (C::NOISE_ROWS * C::ROW_DEPTH);
            bool teamFan = false;
            const ImU32 clothes = crowdClothes(
                shape, key, valueNoise(noiseX, noiseY, 11U), teamFan);
            const ImU32 skin =
                SKIN[static_cast<std::size_t>(unitHash(key, 6U, 0U) * 4.0f) %
                     SKIN.size()];
            const float light =
                rowLight *
                (1.0f +
                 C::LIGHT_SWING *
                     (valueNoise(noiseX * 0.7f, noiseY, 23U) - 0.5f) * 2.0f +
                 C::LIGHT_JITTER * (unitHash(key, 7U, 0U) - 0.5f) * 2.0f);
            CrowdDot& dot = geometry.crowd.emplace_back();
            dot.base = base;
            dot.body = shadeColor(clothes, light);
            dot.head = shadeColor(skin, light);
            dot.phase = static_cast<std::uint8_t>(
                static_cast<std::size_t>(unitHash(key, 8U, 0U) *
                                         static_cast<float>(CROWD_PHASES)) %
                CROWD_PHASES);
            dot.flags = supporterFlags(shape, key, teamFan);
            if ((dot.flags & CrowdFlags::CHEERS_HOME) != std::byte{})
              ++cheersHome;
            if ((dot.flags & CrowdFlags::CHEERS_AWAY) != std::byte{})
              ++cheersAway;
            addFlag(shape, key, dot, right - left, teamFan);
            const ImU32 look = mixColor(dot.body, dot.head, 0.25f);
            accumulate(clumpSum, look);
            if (rowT < 0.5f)
            {
              accumulate(nearSum, look);
              nearCount += 1.0f;
            }
            else
            {
              accumulate(farSum, look);
              farCount += 1.0f;
            }
          }
        }
        clump.dotEnd = static_cast<std::uint32_t>(geometry.crowd.size());
        if (clump.dotEnd == clump.dotBegin) continue;
        const auto seatCount = static_cast<float>(endSeat - firstSeat);
        const float centre =
            (static_cast<float>(firstSeat) + seatCount * 0.5f) /
            static_cast<float>(seats);
        clump.base =
            RenderMath::lerp(shape.left(front), shape.right(front), centre);
        const ProfilePoint beyond = rowPoint(endRow);
        clump.top =
            RenderMath::lerp(shape.left(beyond), shape.right(beyond), centre) +
            UP * (C::DOT_HEIGHT * C::CLUMP_TOP_SHARE);
        clump.halfWidth = seatCount * C::SEAT_SPACING * 0.5f;
        clump.color = average(
            clumpSum, static_cast<float>(clump.dotEnd - clump.dotBegin));
        const auto members = static_cast<int>(clump.dotEnd - clump.dotBegin);
        if (cheersHome * 2 > members) clump.flags |= CrowdFlags::CHEERS_HOME;
        if (cheersAway * 2 > members) clump.flags |= CrowdFlags::CHEERS_AWAY;
        clump.phase = geometry.crowd[clump.dotBegin].phase;
        geometry.clumps.push_back(clump);
      }
    }
    face.clumpEnd = static_cast<std::uint32_t>(geometry.clumps.size());
    face.flagEnd = static_cast<std::uint32_t>(geometry.flags.size());
    const float crowdShare = 1.0f - C::SEAT_SHOW_THROUGH;
    if (nearCount > 0.0f)
    {
      const ImU32 nearColor =
          mixColor(face.colors[0], average(nearSum, nearCount), crowdShare);
      face.colors[0] = face.colors[1] = nearColor;
    }
    if (farCount > 0.0f)
    {
      const ImU32 farColor =
          mixColor(face.colors[3], average(farSum, farCount), crowdShare);
      face.colors[2] = face.colors[3] = farColor;
    }
  }

  void buildSection(const SectionShape& shape)
  {
    using S = Tuning::Stands;
    const ImU32 seats = S::SEAT_COLOR;
    const ProfilePoint frontBottom{0.0f, 0.0f};
    const ProfilePoint frontTop{0.0f, S::FRONT_WALL_HEIGHT};
    const ProfilePoint lowerTop{S::LOWER_DEPTH, S::LOWER_TOP};
    const ProfilePoint concourseTop{S::LOWER_DEPTH, S::CONCOURSE_TOP};
    const ProfilePoint upperTop{S::UPPER_DEPTH, S::UPPER_TOP};
    const ProfilePoint backTop{S::UPPER_DEPTH, S::BACK_WALL_TOP};
    const ProfilePoint roofBack{S::ROOF_BACK_DEPTH, S::ROOF_BACK_HEIGHT};
    const ProfilePoint roofFront{S::ROOF_FRONT_DEPTH, S::ROOF_FRONT_HEIGHT};
    const ProfilePoint fasciaBottom{S::ROOF_FRONT_DEPTH,
                                    S::ROOF_FRONT_HEIGHT - S::FASCIA_HEIGHT};

    Section section;
    section.side = shape.side;
    section.faceBegin = static_cast<std::uint32_t>(geometry.faces.size());
    section.centre = RenderMath::lerp(shape.left(concourseTop),
                                      shape.right(concourseTop), 0.5f);

    // Faces are stored back to front as seen from the pitch.
    using D = Tuning::Day;
    addLitFace(shape, upperTop, backTop, S::WALL_COLOR, S::WALL_COLOR,
               D::WALL_COLOR, D::WALL_COLOR);
    addLitFace(shape, roofBack, roofFront, S::ROOF_UNDER_COLOR,
               shadeColor(S::ROOF_UNDER_COLOR, 1.4f), D::ROOF_UNDER_COLOR,
               shadeColor(D::ROOF_UNDER_COLOR, 1.2f));
    Face& upper = addLitFace(
        shape, concourseTop, upperTop, shadeColor(seats, 0.9f),
        shadeColor(seats, S::BACK_ROW_LIGHT), D::SEAT_COLOR, D::SEAT_COLOR);
    addCrowd(upper, shape, 1U, concourseTop, upperTop, 0.9f, S::BACK_ROW_LIGHT);
    addLitFace(shape, lowerTop, concourseTop, S::WALL_COLOR, S::WALL_COLOR,
               D::WALL_COLOR, D::WALL_COLOR);
    addLitFace(shape, {S::LOWER_DEPTH, S::LOWER_TOP + S::WINDOW_INSET},
               {S::LOWER_DEPTH, S::CONCOURSE_TOP - S::WINDOW_INSET},
               shadeColor(S::WINDOW_COLOR, 0.7f), S::WINDOW_COLOR,
               shadeColor(D::WINDOW_COLOR, 0.8f), D::WINDOW_COLOR,
               S::WINDOW_INSET);
    Face& lower =
        addLitFace(shape, frontTop, lowerTop, shadeColor(seats, 1.1f),
                   shadeColor(seats, 0.8f), D::SEAT_COLOR, D::SEAT_COLOR);
    addCrowd(lower, shape, 0U, frontTop, lowerTop, 1.05f, 0.78f);
    if (shape.side == Side::WEST)
    {
      // The home end's front wall carries a banner in the club colours.
      const ImU32 banner =
          sectionCounter % 2U == 0U ? kits.home.shirt : kits.home.trim;
      addLitFace(shape, frontBottom, frontTop, shadeColor(banner, 0.8f), banner,
                 shadeColor(banner, 0.85f), banner);
    }
    else
    {
      addLitFace(shape, frontBottom, frontTop, S::CONCRETE_COLOR,
                 shadeColor(S::CONCRETE_COLOR, 1.2f), D::CONCRETE_COLOR,
                 shadeColor(D::CONCRETE_COLOR, 1.08f));
    }
    addLitFace(shape, fasciaBottom, roofFront, S::FASCIA_COLOR,
               shadeColor(S::FASCIA_COLOR, 1.3f), D::FASCIA_COLOR,
               shadeColor(D::FASCIA_COLOR, 1.15f));
    addLitFace(shape, roofFront, roofBack, S::ROOF_TOP_COLOR,
               shadeColor(S::ROOF_TOP_COLOR, 0.85f), D::ROOF_TOP_COLOR,
               shadeColor(D::ROOF_TOP_COLOR, 0.92f));
    if (day) addRoofShadow(shape);

    section.faceEnd = static_cast<std::uint32_t>(geometry.faces.size());
    geometry.sections.push_back(section);
    ++sectionCounter;
  }
};

void buildFloodlights(Geometry& geometry, bool day)
{
  using F = Tuning::Floodlight;
  const std::array<std::array<float, 2>, 4> positions{{
      {-F::CORNER_OFFSET_X, -F::CORNER_OFFSET_Y},
      {LENGTH + F::CORNER_OFFSET_X, -F::CORNER_OFFSET_Y},
      {-F::CORNER_OFFSET_X, WIDTH + F::CORNER_OFFSET_Y},
      {LENGTH + F::CORNER_OFFSET_X, WIDTH + F::CORNER_OFFSET_Y},
  }};
  for (const auto& position : positions)
  {
    Section section;
    section.side = Side::NONE;
    section.faceBegin = static_cast<std::uint32_t>(geometry.faces.size());
    section.centre = {position[0], position[1], F::MAST_HEIGHT * 0.5f};
    const float half = F::MAST_SIZE * 0.5f;
    const Box mast =
        makeAxisBox({position[0] - half, position[1] - half, 0.0f},
                    {position[0] + half, position[1] + half, F::MAST_HEIGHT},
                    F::MAST_COLOR);
    for (std::size_t faceIndex = 0; faceIndex < BOX_FACES.size() - 2;
         ++faceIndex)
    {
      Face& face = geometry.faces.emplace_back();
      for (std::size_t corner = 0; corner < 4; ++corner)
      {
        face.corners[corner] = mast.corners[BOX_FACES[faceIndex][corner]];
        face.colors[corner] = shadeColor(
            F::MAST_COLOR, face.corners[corner].z > 1.0f ? 1.0f : 0.6f);
      }
      face.normal = RenderMath::normalize(
          RenderMath::cross(face.corners[2] - face.corners[0],
                            face.corners[3] - face.corners[1]));
      if (day)
      {
        for (std::size_t corner = 0; corner < 4; ++corner)
        {
          face.colors[corner] =
              shadeColor(Tuning::Day::MAST_COLOR,
                         sunlight(face.normal, false) *
                             (face.corners[corner].z > 1.0f ? 1.0f : 0.75f));
        }
      }
    }

    const Vec3 toCentre = RenderMath::normalize(
        Vec3{HALF_LENGTH - position[0], HALF_WIDTH - position[1], 0.0f});
    const Vec3 normal = RenderMath::normalize(
        toCentre * std::cos(F::HEAD_TILT) - UP * std::sin(F::HEAD_TILT));
    const Vec3 right = RenderMath::normalize(RenderMath::cross(UP, normal));
    const Vec3 panelUp = RenderMath::cross(normal, right);
    const Vec3 centre{position[0], position[1],
                      F::MAST_HEIGHT + F::HEAD_HEIGHT * 0.5f};
    const Vec3 halfRight = right * (F::HEAD_WIDTH * 0.5f);
    const Vec3 halfUp = panelUp * (F::HEAD_HEIGHT * 0.5f);
    Face& back = geometry.faces.emplace_back();
    back.corners = {centre - halfRight - halfUp, centre - halfRight + halfUp,
                    centre + halfRight + halfUp, centre + halfRight - halfUp};
    back.normal = normal * -1.0f;
    back.colors.fill(
        day ? shadeColor(Tuning::Day::MAST_COLOR, sunlight(back.normal, false))
            : shadeColor(F::MAST_COLOR, 0.7f));
    Face& front = geometry.faces.emplace_back();
    front.corners = back.corners;
    front.normal = normal;
    if (day)
    {
      // Lamps off: a grey panel of unlit lenses.
      const ImU32 lens =
          shadeColor(Tuning::Day::LAMP_OFF_COLOR, sunlight(normal, false));
      front.colors = {lens, shadeColor(lens, 0.9f), shadeColor(lens, 0.9f),
                      lens};
    }
    else
    {
      front.colors = {F::LAMP_COLOR, shadeColor(F::LAMP_COLOR, 0.92f),
                      shadeColor(F::LAMP_COLOR, 0.92f), F::LAMP_COLOR};
      front.glow = F::GLOW_COLOR;
    }
    section.faceEnd = static_cast<std::uint32_t>(geometry.faces.size());
    geometry.sections.push_back(section);
  }
}

void buildBoards(Geometry& geometry)
{
  using B = Tuning::Boards;
  struct Run
  {
    Vec3 start;
    Vec3 end;
    Vec3 normal;
  };
  const std::array<Run, 4> runs{{
      {{-B::END_OFFSET + 2.0f, -B::SIDE_OFFSET, 0.0f},
       {LENGTH + B::END_OFFSET - 2.0f, -B::SIDE_OFFSET, 0.0f},
       {0.0f, 1.0f, 0.0f}},
      {{-B::END_OFFSET + 2.0f, WIDTH + B::SIDE_OFFSET, 0.0f},
       {LENGTH + B::END_OFFSET - 2.0f, WIDTH + B::SIDE_OFFSET, 0.0f},
       {0.0f, -1.0f, 0.0f}},
      {{-B::END_OFFSET, 2.0f, 0.0f},
       {-B::END_OFFSET, WIDTH - 2.0f, 0.0f},
       {1.0f, 0.0f, 0.0f}},
      {{LENGTH + B::END_OFFSET, 2.0f, 0.0f},
       {LENGTH + B::END_OFFSET, WIDTH - 2.0f, 0.0f},
       {-1.0f, 0.0f, 0.0f}},
  }};
  std::uint8_t sponsor = 0;
  for (const Run& run : runs)
  {
    const Vec3 span = run.end - run.start;
    const float runLength = RenderMath::length(span);
    const Vec3 direction = span * (1.0f / runLength);
    const auto count = static_cast<int>(runLength / (B::LENGTH + B::GAP));
    const float used = static_cast<float>(count) * (B::LENGTH + B::GAP);
    const Vec3 right = RenderMath::cross(run.normal * -1.0f, UP);
    for (int index = 0; index < count; ++index)
    {
      const float along =
          (runLength - used) * 0.5f +
          (static_cast<float>(index) + 0.5f) * (B::LENGTH + B::GAP);
      const Vec3 centre = run.start + direction * along;
      AdBoard& board = geometry.boards.emplace_back();
      board.origin = centre - right * (B::LENGTH * 0.5f);
      board.right = right;
      board.normal = run.normal;
      board.sponsor = static_cast<std::uint8_t>(sponsor++ % SPONSORS.size());
    }
    sponsor = static_cast<std::uint8_t>(sponsor + 3);
  }
}

void buildGoals(Geometry& geometry)
{
  using G = Tuning::Goal;
  const float yLeft = HALF_WIDTH - G::WIDTH * 0.5f;
  const float yRight = HALF_WIDTH + G::WIDTH * 0.5f;
  const float post = G::POST_SIZE;
  const float half = post * 0.5f;
  for (std::size_t index = 0; index < geometry.goals.size(); ++index)
  {
    const float line = index == 0 ? 0.0f : LENGTH;
    const float out = index == 0 ? -1.0f : 1.0f;
    Goal& goal = geometry.goals[index];
    goal.frame[0] =
        makeAxisBox({line - half, yLeft - post, 0.0f},
                    {line + half, yLeft, G::HEIGHT + post}, G::POST_COLOR);
    goal.frame[1] = makeAxisBox({line - half, yRight, 0.0f},
                                {line + half, yRight + post, G::HEIGHT + post},
                                G::POST_COLOR);
    goal.frame[2] =
        makeAxisBox({line - half, yLeft, G::HEIGHT},
                    {line + half, yRight, G::HEIGHT + post}, G::POST_COLOR);

    const float roofX = line + out * G::ROOF_DEPTH;
    const float baseX = line + out * G::BASE_DEPTH;
    const float roofZ = G::HEIGHT - G::ROOF_DROP;
    const auto rows = [](int value)
    { return static_cast<std::uint8_t>(value); };
    goal.nets[0] = {
        {Vec3{line, yLeft, G::HEIGHT}, Vec3{line, yRight, G::HEIGHT},
         Vec3{roofX, yRight, roofZ}, Vec3{roofX, yLeft, roofZ}},
        rows(G::NET_ROOF_ROWS),
        rows(G::NET_COLUMNS)};
    goal.nets[1] = {{Vec3{roofX, yLeft, roofZ}, Vec3{roofX, yRight, roofZ},
                     Vec3{baseX, yRight, 0.0f}, Vec3{baseX, yLeft, 0.0f}},
                    rows(G::NET_BACK_ROWS),
                    rows(G::NET_COLUMNS)};
    goal.nets[2] = {{Vec3{line, yLeft, 0.0f}, Vec3{line, yLeft, G::HEIGHT},
                     Vec3{roofX, yLeft, roofZ}, Vec3{baseX, yLeft, 0.0f}},
                    rows(G::NET_SIDE_COLUMNS),
                    rows(G::NET_BACK_ROWS)};
    goal.nets[3] = {{Vec3{line, yRight, 0.0f}, Vec3{line, yRight, G::HEIGHT},
                     Vec3{roofX, yRight, roofZ}, Vec3{baseX, yRight, 0.0f}},
                    rows(G::NET_SIDE_COLUMNS),
                    rows(G::NET_BACK_ROWS)};
    goal.supports[0] = {Vec3{roofX, yLeft, roofZ}, Vec3{roofX, yRight, roofZ}};
    goal.supports[1] = {Vec3{baseX, yLeft, 0.0f}, Vec3{baseX, yRight, 0.0f}};
  }
}
}  // namespace

Box makeAxisBox(Vec3 minimum, Vec3 maximum, ImU32 color)
{
  Box box;
  for (std::size_t corner = 0; corner < box.corners.size(); ++corner)
  {
    box.corners[corner] = {(corner & 1U) != 0U ? maximum.x : minimum.x,
                           (corner & 2U) != 0U ? maximum.y : minimum.y,
                           (corner & 4U) != 0U ? maximum.z : minimum.z};
  }
  box.color = color;
  return box;
}

bool Geometry::inStandShadow(Vec3 point) const
{
  for (const std::array<Vec3, 4>& area : shadowAreas)
  {
    // Convex quad: inside when on the same side of all four edges.
    float sign = 0.0f;
    bool inside = true;
    for (std::size_t index = 0; index < area.size(); ++index)
    {
      const Vec3& a = area[index];
      const Vec3& b = area[(index + 1) % area.size()];
      const float side =
          (b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x);
      if (std::abs(side) < 1e-6f) continue;
      if (sign == 0.0f) sign = side;
      inside = side * sign > 0.0f;
      if (!inside) break;
    }
    if (inside) return true;
  }
  return false;
}

void Geometry::build(const MatchKits& kits, bool daylightView)
{
  using enum Side;
  day = daylightView;
  standShadows.clear();
  shadowAreas.clear();
  ground.clear();
  wear.clear();
  markings.clear();
  faces.clear();
  sections.clear();
  crowd.clear();
  clumps.clear();
  flags.clear();
  boards.clear();

  buildGround(*this, day);
  buildWear(*this);
  buildMarkings(*this);

  using S = Tuning::Stands;
  // Long stands reach the end stands' front line so the four corner infills
  // close the bowl without gaps.
  const float x0 = -S::END_FRONT;
  const float x1 = LENGTH + S::END_FRONT;
  const float y0 = -S::SIDE_FRONT;
  const float y1 = WIDTH + S::SIDE_FRONT;
  const Vec3 south{0.0f, -1.0f, 0.0f};
  const Vec3 north{0.0f, 1.0f, 0.0f};
  const Vec3 west{-1.0f, 0.0f, 0.0f};
  const Vec3 east{1.0f, 0.0f, 0.0f};
  StandBuilder stands(*this, kits, day);
  stands.buildStand({x0, y0, 0.0f}, east, south, x1 - x0, S::SIDE_SECTIONS,
                    SOUTH);
  stands.buildStand({x1, y1, 0.0f}, west, north, x1 - x0, S::SIDE_SECTIONS,
                    NORTH);
  stands.buildStand({x0, y1, 0.0f}, south, west, y1 - y0, S::END_SECTIONS,
                    WEST);
  stands.buildStand({x1, y0, 0.0f}, north, east, y1 - y0, S::END_SECTIONS,
                    EAST);
  stands.buildCorner({x0, y0, 0.0f}, west, south, SOUTH);
  stands.buildCorner({x1, y0, 0.0f}, south, east, SOUTH);
  stands.buildCorner({x1, y1, 0.0f}, east, north, NORTH);
  stands.buildCorner({x0, y1, 0.0f}, north, west, NORTH);
  buildFloodlights(*this, day);
  buildBoards(*this);
  buildGoals(*this);
  cornerFlags = {Vec3{0.0f, 0.0f, 0.0f}, Vec3{LENGTH, 0.0f, 0.0f},
                 Vec3{0.0f, WIDTH, 0.0f}, Vec3{LENGTH, WIDTH, 0.0f}};
}
}  // namespace Stadium3D
