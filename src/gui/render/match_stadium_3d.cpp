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
#include "model/match_ground.h"

namespace Stadium3D
{
namespace
{
using Tuning = MatchRender3DTuning;

constexpr float LENGTH = MatchTuning::Pitch::MATCH_LENGTH_METRES;
constexpr float WIDTH = MatchTuning::Pitch::MATCH_WIDTH_METRES;
constexpr float HALF_LENGTH = LENGTH * 0.5F;
constexpr float HALF_WIDTH = WIDTH * 0.5F;
constexpr Vec3 UP{.x = 0.0F, .y = 0.0F, .z = 1.0F};
constexpr float TWO_PI = 2.0F * std::numbers::pi_v<float>;

/// Floodlit falloff at night (brightest at the centre spot, darker in the
/// corners); daylight is even.
float groundLight(float x, float y, bool day)
{
  if (day)
  {
    return 1.0F;
  }
  const float dx = (x - HALF_LENGTH) / HALF_LENGTH;
  const float dy = (y - HALF_WIDTH) / HALF_WIDTH;
  return std::max(0.62F, 1.0F - (Tuning::Grass::VIGNETTE_LENGTH * dx * dx) -
                             (Tuning::Grass::VIGNETTE_WIDTH * dy * dy));
}

/// Unit vector towards the sun.
Vec3 sunDirection()
{
  using D = Tuning::Day;
  return RenderMath::normalize({.x = D::SUN_X, .y = D::SUN_Y, .z = D::SUN_Z});
}

/// Daylight on a surface: sky fill plus the sun when it faces it.
float sunlight(Vec3 normal, bool shaded)
{
  using D = Tuning::Day;
  const float sun =
      shaded ? 0.0F : std::max(0.0F, RenderMath::dot(normal, sunDirection()));
  return D::STAND_AMBIENT + (D::STAND_DIFFUSE * sun) +
         (D::STAND_SKY * std::max(0.0F, normal.z));
}

float unitHash(std::uint32_t a, std::uint32_t b, std::uint32_t c)
{
  const std::uint32_t mixed =
      cosmeticHash((a * 0x9e3779b1U) ^
                   cosmeticHash((b * 0x85ebca77U) ^ (c + Tuning::Crowd::SEED)));
  return static_cast<float>(mixed & 0xFFFFU) / 65536.0F;
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
  const auto smooth = [](float t) { return t * t * (3.0F - 2.0F * t); };
  const float fx = smooth(x - cellX);
  const float fy = smooth(y - cellY);
  const float bottom =
      lattice(cellX, cellY) +
      ((lattice(cellX + 1.0F, cellY) - lattice(cellX, cellY)) * fx);
  const float top =
      lattice(cellX, cellY + 1.0F) +
      ((lattice(cellX + 1.0F, cellY + 1.0F) - lattice(cellX, cellY + 1.0F)) *
       fx);
  return std::lerp(bottom, top, fy);
}

void addRectangle(std::vector<GroundPolygon>& target, float x0, float y0,
                  float x1, float y1, ImU32 color, bool lit, bool day)
{
  GroundPolygon polygon;
  polygon.count = 4;
  polygon.points[0] = {.x = x0, .y = y0, .z = 0.0F};
  polygon.points[1] = {.x = x1, .y = y0, .z = 0.0F};
  polygon.points[2] = {.x = x1, .y = y1, .z = 0.0F};
  polygon.points[3] = {.x = x0, .y = y1, .z = 0.0F};
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
  const float halfWidth = Tuning::Markings::LINE_WIDTH * 0.5F;
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const float lineLength = std::sqrt((dx * dx) + (dy * dy));
  const float ux = dx / lineLength;
  const float uy = dy / lineLength;
  // Extend by half a width so perpendicular lines meet with square corners.
  x0 -= ux * halfWidth;
  y0 -= uy * halfWidth;
  x1 += ux * halfWidth;
  y1 += uy * halfWidth;
  GroundPolygon polygon;
  polygon.count = 4;
  polygon.points[0] = {
      .x = x0 - (uy * halfWidth), .y = y0 + (ux * halfWidth), .z = 0.0F};
  polygon.points[1] = {
      .x = x1 - (uy * halfWidth), .y = y1 + (ux * halfWidth), .z = 0.0F};
  polygon.points[2] = {
      .x = x1 + (uy * halfWidth), .y = y1 - (ux * halfWidth), .z = 0.0F};
  polygon.points[3] = {
      .x = x0 + (uy * halfWidth), .y = y0 - (ux * halfWidth), .z = 0.0F};
  polygon.colors.fill(Tuning::Markings::COLOR);
  target.push_back(polygon);
}

void addArc(std::vector<GroundPolygon>& target, float cx, float cy,
            float radius, float startAngle, float endAngle, int segments)
{
  const float inner = radius - (Tuning::Markings::LINE_WIDTH * 0.5F);
  const float outer = radius + (Tuning::Markings::LINE_WIDTH * 0.5F);
  for (int segment = 0; segment < segments; ++segment)
  {
    const float a0 =
        startAngle + ((endAngle - startAngle) * static_cast<float>(segment) /
                      static_cast<float>(segments));
    const float a1 = startAngle + ((endAngle - startAngle) *
                                   static_cast<float>(segment + 1) /
                                   static_cast<float>(segments));
    GroundPolygon polygon;
    polygon.count = 4;
    polygon.points[0] = {.x = cx + (std::cos(a0) * inner),
                         .y = cy + (std::sin(a0) * inner),
                         .z = 0.0F};
    polygon.points[1] = {.x = cx + (std::cos(a0) * outer),
                         .y = cy + (std::sin(a0) * outer),
                         .z = 0.0F};
    polygon.points[2] = {.x = cx + (std::cos(a1) * outer),
                         .y = cy + (std::sin(a1) * outer),
                         .z = 0.0F};
    polygon.points[3] = {.x = cx + (std::cos(a1) * inner),
                         .y = cy + (std::sin(a1) * inner),
                         .z = 0.0F};
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
        .x = cx + (std::cos(angle) * Tuning::Markings::SPOT_RADIUS),
        .y = cy + (std::sin(angle) * Tuning::Markings::SPOT_RADIUS),
        .z = 0.0F};
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
      const float x0 = sx0 + ((sx1 - sx0) * static_cast<float>(column) /
                              static_cast<float>(SURROUND_CELLS));
      const float x1 = sx0 + ((sx1 - sx0) * static_cast<float>(column + 1) /
                              static_cast<float>(SURROUND_CELLS));
      const float y0 = sy0 + ((sy1 - sy0) * static_cast<float>(row) /
                              static_cast<float>(SURROUND_CELLS));
      const float y1 = sy0 + ((sy1 - sy0) * static_cast<float>(row + 1) /
                              static_cast<float>(SURROUND_CELLS));
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
            ((valueNoise(x / G::NOISE_METRES, y / G::NOISE_METRES, 31U) -
              0.5F) *
             G::NOISE_STRENGTH) +
            ((valueNoise(x / G::FINE_NOISE_METRES, y / G::FINE_NOISE_METRES,
                         37U) -
              0.5F) *
             G::FINE_NOISE_STRENGTH);
        grid.points.push_back({.x = x, .y = y, .z = 0.0F});
        grid.colors.push_back(shadeColor(
            G::PITCH_COLOR, groundLight(x, y, day) * (1.0F + mottle)));
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
  polygon.points[0] = {.x = cx, .y = cy, .z = 0.0F};
  polygon.colors[0] = withAlpha(Tuning::Grass::WEAR_COLOR, alpha);
  for (std::size_t index = 0; index <= RIM; ++index)
  {
    const std::size_t corner = index % RIM;
    const float angle =
        TWO_PI * static_cast<float>(corner) / static_cast<float>(RIM);
    const float wobble =
        1.0F +
        ((unitHash(salt, static_cast<std::uint32_t>(corner), 41U) - 0.5F) *
         0.4F);
    polygon.points[index + 1] = {.x = cx + (std::cos(angle) * radiusX * wobble),
                                 .y = cy + (std::sin(angle) * radiusY * wobble),
                                 .z = 0.0F};
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
  addWear(wear, HALF_LENGTH, HALF_WIDTH, W::SPOT_WEAR_RADIUS * 1.3F,
          W::SPOT_WEAR_RADIUS, W::WEAR_ALPHA, ++salt);
  for (const float goalLine : {0.0F, LENGTH})
  {
    const float inward = goalLine == 0.0F ? 1.0F : -1.0F;
    // The keeper's patch in the goalmouth is the most worn ground there is.
    addWear(wear, goalLine + (inward * W::GOALMOUTH_DEPTH), HALF_WIDTH,
            W::GOALMOUTH_DEPTH * 0.8F, W::GOALMOUTH_WIDTH, W::WEAR_ALPHA,
            ++salt);
    addWear(wear, goalLine + (inward * Tuning::Markings::PENALTY_SPOT_DISTANCE),
            HALF_WIDTH, W::SPOT_WEAR_RADIUS, W::SPOT_WEAR_RADIUS * 0.8F,
            W::WEAR_ALPHA, ++salt);
    // The goal area takes a beating in front of the six-yard line too.
    addWear(wear, goalLine + (inward * Tuning::Markings::GOAL_AREA_DEPTH),
            HALF_WIDTH, W::GOALMOUTH_DEPTH * 1.2F, W::GOALMOUTH_WIDTH * 1.5F,
            W::WEAR_ALPHA / 2, ++salt);
  }
  // The assistant referees' running lines just outside the touchlines, one
  // half each (diagonal system), in several overlapping scuffs.
  for (int scuff = 0; scuff < W::LINESMAN_SCUFFS; ++scuff)
  {
    const float share = (static_cast<float>(scuff) + 0.5F) /
                        static_cast<float>(W::LINESMAN_SCUFFS);
    const float length = HALF_LENGTH / static_cast<float>(W::LINESMAN_SCUFFS);
    addWear(wear, HALF_LENGTH + (share * HALF_LENGTH), -W::LINESMAN_OFFSET,
            length * 0.75F, W::LINESMAN_HALF_WIDTH, W::LINESMAN_ALPHA, ++salt);
    addWear(wear, share * HALF_LENGTH, WIDTH + W::LINESMAN_OFFSET,
            length * 0.75F, W::LINESMAN_HALF_WIDTH, W::LINESMAN_ALPHA, ++salt);
  }
}

void buildMarkings(Geometry& geometry)
{
  auto& lines = geometry.markings;
  // The technical areas' dashed lines (1 m from the touchline, and out to
  // the dugouts); world y runs the other way from the engine's.
  const auto dashed = [&lines](float x0, float y0, float x1, float y1)
  {
    constexpr float DASH = Tuning::Fixtures::DASH;
    const float metres = std::hypot(x1 - x0, y1 - y0);
    const int dashes = std::max(1, static_cast<int>(metres / (2.0F * DASH)));
    for (int dash = 0; dash < dashes; ++dash)
    {
      const float a =
          static_cast<float>(2 * dash) / static_cast<float>((2 * dashes) - 1);
      const float b = static_cast<float>((2 * dash) + 1) /
                      static_cast<float>((2 * dashes) - 1);
      addLine(lines, x0 + ((x1 - x0) * a), WIDTH - (y0 + (y1 - y0) * a),
              x0 + ((x1 - x0) * b), WIDTH - (y0 + (y1 - y0) * b));
    }
  };
  for (const MatchGround::Match::Rect& area :
       MatchGround::Match::TECHNICAL_AREAS)
  {
    dashed(area.x0, area.y0, area.x1, area.y0);
    dashed(area.x0, area.y0, area.x0, area.y1);
    dashed(area.x1, area.y0, area.x1, area.y1);
  }
  addLine(lines, 0.0F, 0.0F, LENGTH, 0.0F);
  addLine(lines, 0.0F, WIDTH, LENGTH, WIDTH);
  addLine(lines, 0.0F, 0.0F, 0.0F, WIDTH);
  addLine(lines, LENGTH, 0.0F, LENGTH, WIDTH);
  addLine(lines, HALF_LENGTH, 0.0F, HALF_LENGTH, WIDTH);
  addArc(lines, HALF_LENGTH, HALF_WIDTH, Tuning::Markings::CIRCLE_RADIUS, 0.0F,
         TWO_PI, Tuning::Markings::CIRCLE_SEGMENTS);
  addSpot(lines, HALF_LENGTH, HALF_WIDTH);

  const float arcHalfAngle =
      std::acos((Tuning::Markings::PENALTY_AREA_DEPTH -
                 Tuning::Markings::PENALTY_SPOT_DISTANCE) /
                Tuning::Markings::CIRCLE_RADIUS);
  for (const float goalLine : {0.0F, LENGTH})
  {
    const float inward = goalLine == 0.0F ? 1.0F : -1.0F;
    const auto addBox = [&](float depth, float width)
    {
      const float x = goalLine + (inward * depth);
      const float y0 = HALF_WIDTH - (width * 0.5F);
      const float y1 = HALF_WIDTH + (width * 0.5F);
      addLine(lines, goalLine, y0, x, y0);
      addLine(lines, goalLine, y1, x, y1);
      addLine(lines, x, y0, x, y1);
    };
    addBox(Tuning::Markings::PENALTY_AREA_DEPTH,
           Tuning::Markings::PENALTY_AREA_WIDTH);
    addBox(Tuning::Markings::GOAL_AREA_DEPTH,
           Tuning::Markings::GOAL_AREA_WIDTH);
    const float spotX =
        goalLine + (inward * Tuning::Markings::PENALTY_SPOT_DISTANCE);
    addSpot(lines, spotX, HALF_WIDTH);
    const float facing = inward > 0.0F ? 0.0F : std::numbers::pi_v<float>;
    addArc(lines, spotX, HALF_WIDTH, Tuning::Markings::CIRCLE_RADIUS,
           facing - arcHalfAngle, facing + arcHalfAngle,
           Tuning::Markings::PENALTY_ARC_SEGMENTS);
  }

  const float quarter = std::numbers::pi_v<float> * 0.5F;
  const std::array<std::array<float, 3>, 4> corners{{
      {0.0F, 0.0F, 0.0F},
      {LENGTH, 0.0F, quarter},
      {LENGTH, WIDTH, 2.0F * quarter},
      {0.0F, WIDTH, 3.0F * quarter},
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
  float d = 0.0F;
  float z = 0.0F;
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
  float run = 1.0F;
  Side side = Side::NONE;
  bool awayEnd = false;

  [[nodiscard]] Vec3 left(ProfilePoint point) const
  {
    return leftOrigin + leftStep * point.d + UP * point.z;
  }
  [[nodiscard]] Vec3 right(ProfilePoint point) const
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
    const float toward =
        -((sun.x * shape.outward.x) + (sun.y * shape.outward.y));
    if (toward <= 0.05F)
    {
      return true;
    }
    const float roofHeight = S::ROOF_FRONT_HEIGHT - S::FASCIA_HEIGHT;
    if (point.z >= roofHeight)
    {
      return false;
    }
    // Follow the ray towards the sun up to the roof's height: under the
    // roof it is blocked, in front of the fascia it is open sky.
    const float travel =
        toward * (roofHeight - point.z) / std::max(sun.z, 0.1F);
    return point.d - travel / std::max(shape.run, 0.3F) > S::ROOF_FRONT_DEPTH;
  }

  /// Night colours stay as given (floodlit); by day the day colour is lit
  /// by the sun unless the point is in the roof's shadow.
  [[nodiscard]] ImU32 daylit(ImU32 nightColor, ImU32 dayColor, Vec3 normal,
                             bool shaded) const
  {
    return day ? shadeColor(dayColor, sunlight(normal, shaded)) : nightColor;
  }

  Face& addProfileFace(const SectionShape& shape, ProfilePoint from,
                       ProfilePoint to, ImU32 nearColor, ImU32 farColor,
                       float inset = 0.0F)
  {
    Face& face = geometry.faces.emplace_back();
    const auto trimmed = [&](ProfilePoint point, bool rightEnd)
    {
      const Vec3 left = shape.left(point);
      const Vec3 right = shape.right(point);
      const float rowLength = RenderMath::length(right - left);
      const float t = rowLength > 2.0F * inset ? inset / rowLength : 0.0F;
      return RenderMath::lerp(left, right, rightEnd ? 1.0F - t : t);
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
            -((sun.x * shape.outward.x) + (sun.y * shape.outward.y));
        toward > 0.05F)
    {
      return;
    }
    const ProfilePoint edge{.d = S::ROOF_FRONT_DEPTH,
                            .z = S::ROOF_FRONT_HEIGHT - S::FASCIA_HEIGHT};
    const auto onGround = [&](Vec3 point)
    {
      return Vec3{.x = point.x - (sun.x * point.z / sun.z),
                  .y = point.y - (sun.y * point.z / sun.z),
                  .z = 0.0F};
    };
    const Vec3 leftFront = shape.left({.d = 0.0F, .z = 0.0F});
    const Vec3 rightFront = shape.right({.d = 0.0F, .z = 0.0F});
    const Vec3 leftEdge = onGround(shape.left(edge));
    const Vec3 rightEdge = onGround(shape.right(edge));
    // Only where the shadow reaches past the front of the stand.
    if (const auto inward =
            [&](Vec3 point)
        {
          return -(((point.x - leftFront.x) * shape.outward.x) +
                   ((point.y - leftFront.y) * shape.outward.y));
        };
        inward(leftEdge) <= 0.1F && inward(rightEdge) <= 0.1F)
    {
      return;
    }
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
                   ImU32 dayNear, ImU32 dayFar, float inset = 0.0F)
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
    {
      teamShare = C::AWAY_END_SHARE;
    }
    else if (shape.side == Side::WEST)
    {
      teamShare = C::HOME_END_SHARE;
    }
    teamShare += C::SHARE_SWING * (teamNoise - 0.5F) * 2.0F;
    teamFan = unitHash(key, 3U, 0U) < teamShare;
    if (teamFan)
    {
      const KitColors& kit = shape.awayEnd ? kits.away : kits.home;
      return unitHash(key, 4U, 0U) < 0.75F ? kit.shirt : kit.trim;
    }
    constexpr std::array<ImU32, 9> NEUTRAL{
        IM_COL32(34, 40, 62, 255),  IM_COL32(40, 40, 46, 255),
        IM_COL32(92, 96, 104, 255), IM_COL32(138, 140, 146, 255),
        IM_COL32(52, 70, 104, 255), IM_COL32(96, 56, 46, 255),
        IM_COL32(70, 78, 56, 255),  IM_COL32(150, 132, 108, 255),
        IM_COL32(120, 36, 40, 255)};
    return NEUTRAL[static_cast<std::size_t>(unitHash(key, 5U, 0U) * 9.0F) %
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
      if (unitHash(key, 9U, 0U) < scarfShare)
      {
        flags |= CrowdFlags::SCARF;
      }
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
    if (!teamFan)
    {
      return;
    }
    float share = C::FLAG_SHARE;
    if (shape.awayEnd)
    {
      share = C::AWAY_END_FLAG_SHARE;
    }
    else if (shape.side == Side::WEST)
    {
      share = C::HOME_END_FLAG_SHARE;
    }
    if (unitHash(key, 11U, 0U) >= share)
    {
      return;
    }
    const KitColors& kit = shape.awayEnd ? kits.away : kits.home;
    CrowdFlag& flag = geometry.flags.emplace_back();
    flag.base = dot.base + UP * C::DOT_HEIGHT;
    flag.along = RenderMath::normalize(row) *
                 (unitHash(key, 12U, 0U) < 0.5F ? 1.0F : -1.0F);
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
      const float d =
          from.d + ((static_cast<float>(row) + 0.5F) * C::ROW_DEPTH);
      const float t = (d - from.d) / (to.d - from.d);
      return ProfilePoint{.d = d, .z = from.z + ((to.z - from.z) * t)};
    };
    std::array<float, 3> nearSum{};
    std::array<float, 3> farSum{};
    float nearCount = 0.0F;
    float farCount = 0.0F;
    const auto accumulate = [](std::array<float, 3>& sum, ImU32 color)
    {
      sum[0] += static_cast<float>((color >> IM_COL32_R_SHIFT) & 0xFFU);
      sum[1] += static_cast<float>((color >> IM_COL32_G_SHIFT) & 0xFFU);
      sum[2] += static_cast<float>((color >> IM_COL32_B_SHIFT) & 0xFFU);
    };
    const auto average = [](const std::array<float, 3>& sum, float count)
    {
      const auto channel = [count](float value)
      { return static_cast<int>((value / count) + 0.5F); };
      return IM_COL32(channel(sum[0]), channel(sum[1]), channel(sum[2]), 255);
    };

    const Vec3 spectatorNormal =
        RenderMath::normalize(shape.outward * -0.8F + UP * 0.6F);
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
              (point.d - from.d) / std::max(to.d - from.d, 1e-3F);
          // Spectators face the pitch: lit by the floodlights at night, by
          // the sun (or only the sky under the roof) by day.
          const float rowLight =
              day ? sunlight(spectatorNormal, inRoofShadow(shape, point))
                  : std::lerp(frontLight, backLight, rowT);
          for (int seat = firstSeat; seat < endSeat; ++seat)
          {
            const std::uint32_t key = (((sectionCounter * 4U + tier) * 128U +
                                        static_cast<std::uint32_t>(row)) *
                                       1024U) +
                                      static_cast<std::uint32_t>(seat);
            if (unitHash(key, 1U, 0U) < C::EMPTY_SEAT_RATIO)
            {
              continue;
            }
            const float jitter =
                (unitHash(key, 2U, 0U) - 0.5F) * C::JITTER * 2.0F;
            const Vec3 base =
                RenderMath::lerp(left, right,
                                 (static_cast<float>(seat) + 0.5F + jitter) /
                                     static_cast<float>(seats));
            const float noiseX =
                (base.x + base.y) / (C::NOISE_SEATS * C::SEAT_SPACING);
            const float noiseY = (point.d + static_cast<float>(tier) * 50.0F) /
                                 (C::NOISE_ROWS * C::ROW_DEPTH);
            bool teamFan = false;
            const ImU32 clothes = crowdClothes(
                shape, key, valueNoise(noiseX, noiseY, 11U), teamFan);
            const ImU32 skin =
                SKIN[static_cast<std::size_t>(unitHash(key, 6U, 0U) * 4.0F) %
                     SKIN.size()];
            const float light =
                rowLight *
                (1.0F +
                 C::LIGHT_SWING *
                     (valueNoise(noiseX * 0.7F, noiseY, 23U) - 0.5F) * 2.0F +
                 C::LIGHT_JITTER * (unitHash(key, 7U, 0U) - 0.5F) * 2.0F);
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
            {
              ++cheersHome;
            }
            if ((dot.flags & CrowdFlags::CHEERS_AWAY) != std::byte{})
            {
              ++cheersAway;
            }
            addFlag(shape, key, dot, right - left, teamFan);
            const ImU32 look = mixColor(dot.body, dot.head, 0.25F);
            accumulate(clumpSum, look);
            if (rowT < 0.5F)
            {
              accumulate(nearSum, look);
              nearCount += 1.0F;
            }
            else
            {
              accumulate(farSum, look);
              farCount += 1.0F;
            }
          }
        }
        clump.dotEnd = static_cast<std::uint32_t>(geometry.crowd.size());
        if (clump.dotEnd == clump.dotBegin)
        {
          continue;
        }
        const auto seatCount = static_cast<float>(endSeat - firstSeat);
        const float centre =
            (static_cast<float>(firstSeat) + seatCount * 0.5F) /
            static_cast<float>(seats);
        clump.base =
            RenderMath::lerp(shape.left(front), shape.right(front), centre);
        const ProfilePoint beyond = rowPoint(endRow);
        clump.top =
            RenderMath::lerp(shape.left(beyond), shape.right(beyond), centre) +
            UP * (C::DOT_HEIGHT * C::CLUMP_TOP_SHARE);
        clump.halfWidth = seatCount * C::SEAT_SPACING * 0.5F;
        clump.color = average(
            clumpSum, static_cast<float>(clump.dotEnd - clump.dotBegin));
        const auto members = static_cast<int>(clump.dotEnd - clump.dotBegin);
        if (cheersHome * 2 > members)
        {
          clump.flags |= CrowdFlags::CHEERS_HOME;
        }
        if (cheersAway * 2 > members)
        {
          clump.flags |= CrowdFlags::CHEERS_AWAY;
        }
        clump.phase = geometry.crowd[clump.dotBegin].phase;
        geometry.clumps.push_back(clump);
      }
    }
    face.clumpEnd = static_cast<std::uint32_t>(geometry.clumps.size());
    face.flagEnd = static_cast<std::uint32_t>(geometry.flags.size());
    const float crowdShare = 1.0F - C::SEAT_SHOW_THROUGH;
    if (nearCount > 0.0F)
    {
      const ImU32 nearColor =
          mixColor(face.colors[0], average(nearSum, nearCount), crowdShare);
      face.colors[0] = face.colors[1] = nearColor;
    }
    if (farCount > 0.0F)
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
    const ProfilePoint frontBottom{.d = 0.0F, .z = 0.0F};
    const ProfilePoint frontTop{.d = 0.0F, .z = S::FRONT_WALL_HEIGHT};
    const ProfilePoint lowerTop{.d = S::LOWER_DEPTH, .z = S::LOWER_TOP};
    const ProfilePoint concourseTop{.d = S::LOWER_DEPTH, .z = S::CONCOURSE_TOP};
    const ProfilePoint upperTop{.d = S::UPPER_DEPTH, .z = S::UPPER_TOP};
    const ProfilePoint backTop{.d = S::UPPER_DEPTH, .z = S::BACK_WALL_TOP};
    const ProfilePoint roofBack{.d = S::ROOF_BACK_DEPTH,
                                .z = S::ROOF_BACK_HEIGHT};
    const ProfilePoint roofFront{.d = S::ROOF_FRONT_DEPTH,
                                 .z = S::ROOF_FRONT_HEIGHT};
    const ProfilePoint fasciaBottom{
        .d = S::ROOF_FRONT_DEPTH, .z = S::ROOF_FRONT_HEIGHT - S::FASCIA_HEIGHT};

    Section section;
    section.side = shape.side;
    section.faceBegin = static_cast<std::uint32_t>(geometry.faces.size());
    section.centre = RenderMath::lerp(shape.left(concourseTop),
                                      shape.right(concourseTop), 0.5F);

    // Faces are stored back to front as seen from the pitch.
    using D = Tuning::Day;
    addLitFace(shape, upperTop, backTop, S::WALL_COLOR, S::WALL_COLOR,
               D::WALL_COLOR, D::WALL_COLOR);
    addLitFace(shape, roofBack, roofFront, S::ROOF_UNDER_COLOR,
               shadeColor(S::ROOF_UNDER_COLOR, 1.4F), D::ROOF_UNDER_COLOR,
               shadeColor(D::ROOF_UNDER_COLOR, 1.2F));
    Face& upper = addLitFace(
        shape, concourseTop, upperTop, shadeColor(seats, 0.9F),
        shadeColor(seats, S::BACK_ROW_LIGHT), D::SEAT_COLOR, D::SEAT_COLOR);
    addCrowd(upper, shape, 1U, concourseTop, upperTop, 0.9F, S::BACK_ROW_LIGHT);
    addLitFace(shape, lowerTop, concourseTop, S::WALL_COLOR, S::WALL_COLOR,
               D::WALL_COLOR, D::WALL_COLOR);
    addLitFace(
        shape, {.d = S::LOWER_DEPTH, .z = S::LOWER_TOP + S::WINDOW_INSET},
        {.d = S::LOWER_DEPTH, .z = S::CONCOURSE_TOP - S::WINDOW_INSET},
        shadeColor(S::WINDOW_COLOR, 0.7F), S::WINDOW_COLOR,
        shadeColor(D::WINDOW_COLOR, 0.8F), D::WINDOW_COLOR, S::WINDOW_INSET);
    Face& lower =
        addLitFace(shape, frontTop, lowerTop, shadeColor(seats, 1.1F),
                   shadeColor(seats, 0.8F), D::SEAT_COLOR, D::SEAT_COLOR);
    addCrowd(lower, shape, 0U, frontTop, lowerTop, 1.05F, 0.78F);
    if (shape.side == Side::WEST)
    {
      // The home end's front wall carries a banner in the club colours.
      const ImU32 banner =
          sectionCounter % 2U == 0U ? kits.home.shirt : kits.home.trim;
      addLitFace(shape, frontBottom, frontTop, shadeColor(banner, 0.8F), banner,
                 shadeColor(banner, 0.85F), banner);
    }
    else
    {
      addLitFace(shape, frontBottom, frontTop, S::CONCRETE_COLOR,
                 shadeColor(S::CONCRETE_COLOR, 1.2F), D::CONCRETE_COLOR,
                 shadeColor(D::CONCRETE_COLOR, 1.08F));
    }
    addLitFace(shape, fasciaBottom, roofFront, S::FASCIA_COLOR,
               shadeColor(S::FASCIA_COLOR, 1.3F), D::FASCIA_COLOR,
               shadeColor(D::FASCIA_COLOR, 1.15F));
    addLitFace(shape, roofFront, roofBack, S::ROOF_TOP_COLOR,
               shadeColor(S::ROOF_TOP_COLOR, 0.85F), D::ROOF_TOP_COLOR,
               shadeColor(D::ROOF_TOP_COLOR, 0.92F));
    if (day)
    {
      addRoofShadow(shape);
    }

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
    section.centre = {
        .x = position[0], .y = position[1], .z = F::MAST_HEIGHT * 0.5F};
    const float half = F::MAST_SIZE * 0.5F;
    const Box mast = makeAxisBox(
        {.x = position[0] - half, .y = position[1] - half, .z = 0.0F},
        {.x = position[0] + half, .y = position[1] + half, .z = F::MAST_HEIGHT},
        F::MAST_COLOR);
    for (std::size_t faceIndex = 0; faceIndex < BOX_FACES.size() - 2;
         ++faceIndex)
    {
      Face& face = geometry.faces.emplace_back();
      for (std::size_t corner = 0; corner < 4; ++corner)
      {
        face.corners[corner] = mast.corners[BOX_FACES[faceIndex][corner]];
        face.colors[corner] = shadeColor(
            F::MAST_COLOR, face.corners[corner].z > 1.0F ? 1.0F : 0.6F);
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
                             (face.corners[corner].z > 1.0F ? 1.0F : 0.75F));
        }
      }
    }

    const Vec3 toCentre =
        RenderMath::normalize(Vec3{.x = HALF_LENGTH - position[0],
                                   .y = HALF_WIDTH - position[1],
                                   .z = 0.0F});
    const Vec3 normal = RenderMath::normalize(
        toCentre * std::cos(F::HEAD_TILT) - UP * std::sin(F::HEAD_TILT));
    const Vec3 right = RenderMath::normalize(RenderMath::cross(UP, normal));
    const Vec3 panelUp = RenderMath::cross(normal, right);
    const Vec3 centre{.x = position[0],
                      .y = position[1],
                      .z = F::MAST_HEIGHT + (F::HEAD_HEIGHT * 0.5F)};
    const Vec3 halfRight = right * (F::HEAD_WIDTH * 0.5F);
    const Vec3 halfUp = panelUp * (F::HEAD_HEIGHT * 0.5F);
    Face& back = geometry.faces.emplace_back();
    back.corners = {centre - halfRight - halfUp, centre - halfRight + halfUp,
                    centre + halfRight + halfUp, centre + halfRight - halfUp};
    back.normal = normal * -1.0F;
    back.colors.fill(
        day ? shadeColor(Tuning::Day::MAST_COLOR, sunlight(back.normal, false))
            : shadeColor(F::MAST_COLOR, 0.7F));
    Face& front = geometry.faces.emplace_back();
    front.corners = back.corners;
    front.normal = normal;
    if (day)
    {
      // Lamps off: a grey panel of unlit lenses.
      const ImU32 lens =
          shadeColor(Tuning::Day::LAMP_OFF_COLOR, sunlight(normal, false));
      front.colors = {lens, shadeColor(lens, 0.9F), shadeColor(lens, 0.9F),
                      lens};
    }
    else
    {
      front.colors = {F::LAMP_COLOR, shadeColor(F::LAMP_COLOR, 0.92F),
                      shadeColor(F::LAMP_COLOR, 0.92F), F::LAMP_COLOR};
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
  const std::array<Run, 3> runs{{
      {.start = {.x = -B::END_OFFSET + 2.0F,
                 .y = WIDTH + B::SIDE_OFFSET,
                 .z = 0.0F},
       .end = {.x = LENGTH + B::END_OFFSET - 2.0F,
               .y = WIDTH + B::SIDE_OFFSET,
               .z = 0.0F},
       .normal = {.x = 0.0F, .y = -1.0F, .z = 0.0F}},
      {.start = {.x = -B::END_OFFSET, .y = 2.0F, .z = 0.0F},
       .end = {.x = -B::END_OFFSET, .y = WIDTH - 2.0F, .z = 0.0F},
       .normal = {.x = 1.0F, .y = 0.0F, .z = 0.0F}},
      {.start = {.x = LENGTH + B::END_OFFSET, .y = 2.0F, .z = 0.0F},
       .end = {.x = LENGTH + B::END_OFFSET, .y = WIDTH - 2.0F, .z = 0.0F},
       .normal = {.x = -1.0F, .y = 0.0F, .z = 0.0F}},
  }};
  std::uint8_t sponsor = 0;
  for (const Run& run : runs)
  {
    const Vec3 span = run.end - run.start;
    const float runLength = RenderMath::length(span);
    const Vec3 direction = span * (1.0F / runLength);
    const auto count = static_cast<int>(runLength / (B::LENGTH + B::GAP));
    const float used = static_cast<float>(count) * (B::LENGTH + B::GAP);
    const Vec3 right = RenderMath::cross(run.normal * -1.0F, UP);
    for (int index = 0; index < count; ++index)
    {
      const float along =
          ((runLength - used) * 0.5F) +
          ((static_cast<float>(index) + 0.5F) * (B::LENGTH + B::GAP));
      const Vec3 centre = run.start + direction * along;
      AdBoard& board = geometry.boards.emplace_back();
      board.origin = centre - right * (B::LENGTH * 0.5F);
      board.right = right;
      board.normal = run.normal;
      board.length = B::LENGTH;
      board.sponsor = static_cast<std::uint8_t>(sponsor++ % SPONSORS.size());
    }
    sponsor = static_cast<std::uint8_t>(sponsor + 3);
  }

  // The near side (the engine's bottom touchline): open at the technical
  // areas and the tunnel, each stretch filled edge to edge.
  const Vec3 right{.x = 1.0F, .y = 0.0F, .z = 0.0F};
  for (const MatchGround::Match::Span& span : MatchGround::Match::BOTTOM_BOARDS)
  {
    const float from = std::max(span.from, -B::END_OFFSET + 2.0F);
    const float to = std::min(span.to, LENGTH + B::END_OFFSET - 2.0F);
    const int count = std::max(
        1, static_cast<int>(std::lround((to - from) / (B::LENGTH + B::GAP))));
    const float pitch = (to - from) / static_cast<float>(count);
    for (int index = 0; index < count; ++index)
    {
      AdBoard& board = geometry.boards.emplace_back();
      board.origin = {
          .x = from + (pitch * static_cast<float>(index)) + (B::GAP * 0.5F),
          .y = WIDTH - MatchGround::Match::BOARDS_Y,
          .z = 0.0F};
      board.right = right;
      board.normal = {.x = 0.0F, .y = 1.0F, .z = 0.0F};
      board.length = pitch - B::GAP;
      board.sponsor = static_cast<std::uint8_t>(sponsor++ % SPONSORS.size());
    }
  }
}

/// The dugouts (a back panel, a perspex front and roof, a bench, front
/// posts) and the tunnel mouth in the near stand's front wall. World y
/// runs the other way from the engine's (MatchGround) y.
void buildFixtures(Geometry& geometry)
{
  using F = Tuning::Fixtures;
  using P = MatchTuning::Pitch;
  const auto worldY = [](float groundY) { return WIDTH - groundY; };
  for (const MatchGround::Match::Rect& dugout : MatchGround::Match::DUGOUTS)
  {
    const float front = worldY(dugout.y0);
    const float back = worldY(dugout.y1);
    geometry.fixtures.push_back(makeAxisBox(
        {.x = dugout.x0, .y = back, .z = 0.0F},
        {.x = dugout.x1, .y = back + F::PANEL, .z = P::DUGOUT_ROOF_BACK_METRES},
        F::DUGOUT_BACK_COLOR));
    geometry.fixtures.push_back(makeAxisBox(
        {.x = dugout.x0 + F::BENCH_INSET, .y = back + F::PANEL, .z = 0.0F},
        {.x = dugout.x1 - F::BENCH_INSET,
         .y = back + F::PANEL + F::BENCH_DEPTH,
         .z = F::BENCH_HEIGHT},
        F::BENCH_COLOR));
    geometry.fixtures.push_back(makeAxisBox(
        {.x = dugout.x0, .y = front - F::PANEL, .z = 0.0F},
        {.x = dugout.x1, .y = front, .z = P::DUGOUT_FRONT_HEIGHT_METRES},
        F::PERSPEX_COLOR));
    for (const float x : {dugout.x0, dugout.x1 - F::POST})
    {
      geometry.fixtures.push_back(makeAxisBox(
          {.x = x, .y = front - F::POST, .z = 0.0F},
          {.x = x + F::POST, .y = front, .z = P::DUGOUT_ROOF_FRONT_METRES},
          F::POST_COLOR));
    }
    // The roof, sloping down to the front.
    Box roof = makeAxisBox({.x = dugout.x0, .y = back, .z = 0.0F},
                           {.x = dugout.x1, .y = front, .z = F::PANEL},
                           F::PERSPEX_COLOR);
    for (Vec3& corner : roof.corners)
    {
      corner.z +=
          MatchGround::Match::dugoutRoofHeight(dugout, WIDTH - corner.y);
    }
    geometry.fixtures.push_back(roof);
  }
  const MatchGround::Match::Rect& tunnel = MatchGround::Match::TUNNEL;
  const float wall = worldY(tunnel.y0);
  const float top = P::TUNNEL_HEIGHT_METRES;
  geometry.fixtures.push_back(makeAxisBox(
      {.x = tunnel.x0, .y = wall - F::PANEL, .z = 0.0F},
      {.x = tunnel.x1, .y = wall + F::PANEL, .z = top}, F::TUNNEL_COLOR));
  for (const float x : {tunnel.x0 - F::PORTAL, tunnel.x1})
  {
    geometry.fixtures.push_back(makeAxisBox(
        {.x = x, .y = wall - F::PORTAL, .z = 0.0F},
        {.x = x + F::PORTAL, .y = wall + F::PANEL, .z = top + F::PORTAL},
        F::PORTAL_COLOR));
  }
  geometry.fixtures.push_back(
      makeAxisBox({.x = tunnel.x0, .y = wall - F::PORTAL, .z = top},
                  {.x = tunnel.x1, .y = wall + F::PANEL, .z = top + F::PORTAL},
                  F::PORTAL_COLOR));
}

void buildGoals(Geometry& geometry)
{
  using G = Tuning::Goal;
  const float yLeft = HALF_WIDTH - (G::WIDTH * 0.5F);
  const float yRight = HALF_WIDTH + (G::WIDTH * 0.5F);
  const float post = G::POST_SIZE;
  const float half = post * 0.5F;
  for (std::size_t index = 0; index < geometry.goals.size(); ++index)
  {
    const float line = index == 0 ? 0.0F : LENGTH;
    const float out = index == 0 ? -1.0F : 1.0F;
    Goal& goal = geometry.goals[index];
    goal.frame[0] = makeAxisBox(
        {.x = line - half, .y = yLeft - post, .z = 0.0F},
        {.x = line + half, .y = yLeft, .z = G::HEIGHT + post}, G::POST_COLOR);
    goal.frame[1] = makeAxisBox(
        {.x = line - half, .y = yRight, .z = 0.0F},
        {.x = line + half, .y = yRight + post, .z = G::HEIGHT + post},
        G::POST_COLOR);
    goal.frame[2] = makeAxisBox(
        {.x = line - half, .y = yLeft, .z = G::HEIGHT},
        {.x = line + half, .y = yRight, .z = G::HEIGHT + post}, G::POST_COLOR);

    const float roofX = line + (out * G::ROOF_DEPTH);
    const float baseX = line + (out * G::BASE_DEPTH);
    const float roofZ = G::HEIGHT - G::ROOF_DROP;
    const auto rows = [](int value)
    { return static_cast<std::uint8_t>(value); };
    goal.nets[0] = {.corners = {Vec3{.x = line, .y = yLeft, .z = G::HEIGHT},
                                Vec3{.x = line, .y = yRight, .z = G::HEIGHT},
                                Vec3{.x = roofX, .y = yRight, .z = roofZ},
                                Vec3{.x = roofX, .y = yLeft, .z = roofZ}},
                    .rows = rows(G::NET_ROOF_ROWS),
                    .columns = rows(G::NET_COLUMNS)};
    goal.nets[1] = {.corners = {Vec3{.x = roofX, .y = yLeft, .z = roofZ},
                                Vec3{.x = roofX, .y = yRight, .z = roofZ},
                                Vec3{.x = baseX, .y = yRight, .z = 0.0F},
                                Vec3{.x = baseX, .y = yLeft, .z = 0.0F}},
                    .rows = rows(G::NET_BACK_ROWS),
                    .columns = rows(G::NET_COLUMNS)};
    goal.nets[2] = {.corners = {Vec3{.x = line, .y = yLeft, .z = 0.0F},
                                Vec3{.x = line, .y = yLeft, .z = G::HEIGHT},
                                Vec3{.x = roofX, .y = yLeft, .z = roofZ},
                                Vec3{.x = baseX, .y = yLeft, .z = 0.0F}},
                    .rows = rows(G::NET_SIDE_COLUMNS),
                    .columns = rows(G::NET_BACK_ROWS)};
    goal.nets[3] = {.corners = {Vec3{.x = line, .y = yRight, .z = 0.0F},
                                Vec3{.x = line, .y = yRight, .z = G::HEIGHT},
                                Vec3{.x = roofX, .y = yRight, .z = roofZ},
                                Vec3{.x = baseX, .y = yRight, .z = 0.0F}},
                    .rows = rows(G::NET_SIDE_COLUMNS),
                    .columns = rows(G::NET_BACK_ROWS)};
    goal.supports[0] = {Vec3{.x = roofX, .y = yLeft, .z = roofZ},
                        Vec3{.x = roofX, .y = yRight, .z = roofZ}};
    goal.supports[1] = {Vec3{.x = baseX, .y = yLeft, .z = 0.0F},
                        Vec3{.x = baseX, .y = yRight, .z = 0.0F}};
  }
}
}  // namespace

Box makeAxisBox(Vec3 minimum, Vec3 maximum, ImU32 color)
{
  Box box;
  for (std::size_t corner = 0; corner < box.corners.size(); ++corner)
  {
    box.corners[corner] = {.x = (corner & 1U) != 0U ? maximum.x : minimum.x,
                           .y = (corner & 2U) != 0U ? maximum.y : minimum.y,
                           .z = (corner & 4U) != 0U ? maximum.z : minimum.z};
  }
  box.color = color;
  return box;
}

bool Geometry::inStandShadow(Vec3 point) const
{
  for (const std::array<Vec3, 4>& area : shadowAreas)
  {
    // Convex quad: inside when on the same side of all four edges.
    float sign = 0.0F;
    bool inside = true;
    for (std::size_t index = 0; index < area.size(); ++index)
    {
      const Vec3& a = area[index];
      const Vec3& b = area[(index + 1) % area.size()];
      const float side =
          ((b.x - a.x) * (point.y - a.y)) - ((b.y - a.y) * (point.x - a.x));
      if (std::abs(side) < 1e-6F)
      {
        continue;
      }
      if (sign == 0.0F)
      {
        sign = side;
      }
      inside = side * sign > 0.0F;
      if (!inside)
      {
        break;
      }
    }
    if (inside)
    {
      return true;
    }
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
  fixtures.clear();

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
  const Vec3 south{.x = 0.0F, .y = -1.0F, .z = 0.0F};
  const Vec3 north{.x = 0.0F, .y = 1.0F, .z = 0.0F};
  const Vec3 west{.x = -1.0F, .y = 0.0F, .z = 0.0F};
  const Vec3 east{.x = 1.0F, .y = 0.0F, .z = 0.0F};
  StandBuilder stands(*this, kits, day);
  stands.buildStand({.x = x0, .y = y0, .z = 0.0F}, east, south, x1 - x0,
                    S::SIDE_SECTIONS, SOUTH);
  stands.buildStand({.x = x1, .y = y1, .z = 0.0F}, west, north, x1 - x0,
                    S::SIDE_SECTIONS, NORTH);
  stands.buildStand({.x = x0, .y = y1, .z = 0.0F}, south, west, y1 - y0,
                    S::END_SECTIONS, WEST);
  stands.buildStand({.x = x1, .y = y0, .z = 0.0F}, north, east, y1 - y0,
                    S::END_SECTIONS, EAST);
  stands.buildCorner({.x = x0, .y = y0, .z = 0.0F}, west, south, SOUTH);
  stands.buildCorner({.x = x1, .y = y0, .z = 0.0F}, south, east, SOUTH);
  stands.buildCorner({.x = x1, .y = y1, .z = 0.0F}, east, north, NORTH);
  stands.buildCorner({.x = x0, .y = y1, .z = 0.0F}, north, west, NORTH);
  buildFloodlights(*this, day);
  buildBoards(*this);
  buildFixtures(*this);
  buildGoals(*this);
  cornerFlags = {Vec3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                 Vec3{.x = LENGTH, .y = 0.0F, .z = 0.0F},
                 Vec3{.x = 0.0F, .y = WIDTH, .z = 0.0F},
                 Vec3{.x = LENGTH, .y = WIDTH, .z = 0.0F}};
}
}  // namespace Stadium3D
