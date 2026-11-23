// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_true_view_shapes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <numbers>

#include "gui/widgets/text.h"
#include "model/match_body_shape_binturong.h"
#include "model/match_contact_binturong.h"
#include "model/match_rules.h"

namespace TrueView
{
namespace
{
constexpr float PI = std::numbers::pi_v<float>;
/** Sections round each ring of a stretch map (agreed: 12, 30 degrees each,
 * edges on his facing line, square and the dead zone's 120). */
constexpr int SECTIONS = 12;
/** How far the send cones reach out (m), only to be seen. */
constexpr float CONE_METRES = 0.55F;

// Turns in degrees: every ring runs -HALF_TURN to HALF_TURN round his facing.
constexpr float HALF_TURN = 180.0F;
constexpr float QUARTER_TURN = 90.0F;
constexpr float SECTION_DEGREES = 2.0F * HALF_TURN / SECTIONS;
constexpr float HALF_SECTION = SECTION_DEGREES / 2.0F;
constexpr float HALF = 0.5F;
constexpr float MAX_CHANNEL = 255.0F;

// Line weights (pixels).
constexpr float LINE_FINE = 1.2F;
constexpr float LINE = 1.5F;
constexpr float LINE_BOLD = 2.0F;
constexpr float LINE_HEAVY = 2.5F;
constexpr float LINE_HEAVIEST = 3.0F;

// Shapes on the floor are lifted this far (m), each layer its own height,
// so overlapping ones do not flicker.
constexpr float TACKLE_FILL_LIFT = 0.001F;
/** Personal space; also a ring drawn just above a band it edges. */
constexpr float SPACE_LIFT = 0.002F;
constexpr float TACKLE_RING_LIFT = 0.003F;
/** His base and the dribble rings. */
constexpr float BASE_LIFT = 0.004F;
constexpr float FEET_LIFT = 0.006F;
/** Facing arrows. */
constexpr float ARROW_LIFT = 0.01F;

// Painter: dashed rings, cylinder ribs and side panels.
constexpr int DASHES = 36;
constexpr float DASH_EVERY_DEGREES = 10.0F;
constexpr float DASH_DEGREES = 6.0F;
constexpr float RIB_TOLERANCE = 0.01F;
constexpr float RIB_SHARE = 0.7F;
/** A cylinder spanning less than this is a part one, closed to its axis. */
constexpr float FULL_RING_SPAN = 359.0F;
constexpr int SIDE_PANELS = 24;
constexpr float SIDE_PANEL_DEGREES = 2.0F * HALF_TURN / SIDE_PANELS;

// Power by angle: red at none, green at full.
constexpr float POWER_RED = 0.85F;
constexpr float POWER_GREEN = 0.25F;
constexpr float POWER_BLUE = 0.25F;
constexpr float POWER_SWING = 0.55F;

// Alphas.
constexpr float RING_ALPHA = 0.35F;
constexpr float SECTION_EDGE_ALPHA = 0.25F;
/** Shapes of the techniques not picked. */
constexpr float UNPICKED_ALPHA = 0.25F;
constexpr float BODY_BASE_ALPHA = 0.95F;
constexpr float BODY_EDGE_ALPHA = 0.9F;
/** His sides: in a kit, or plain; full detail, or outline only. */
constexpr float KIT_SIDE_ALPHA = 0.3F;
constexpr float KIT_SIDE_OUTLINE_ALPHA = 0.45F;
constexpr float PLAIN_SIDE_ALPHA = 0.18F;
constexpr float PLAIN_SIDE_OUTLINE_ALPHA = 0.3F;
constexpr float KIT_RING_ALPHA = 0.9F;
constexpr float PLAIN_RING_ALPHA = 0.45F;
constexpr float OUTLINE_FACING_ALPHA = 0.7F;
constexpr float SPACE_ALPHA = 0.8F;
constexpr float PRESSURE_ALPHA = 0.7F;
constexpr float TACKLE_FILL_ALPHA = 0.12F;
/** Every other tackle section is a shade stronger. */
constexpr float TACKLE_STRIPE_ALPHA = 0.05F;
constexpr float SHOT_BLOCK_ALPHA = 0.8F;
constexpr float DEAD_ZONE_ALPHA = 0.75F;
constexpr float FEET_BAND_ALPHA = 0.25F;
constexpr float POWER_ALPHA = 0.75F;
constexpr float MARK_ALPHA = 0.9F;
constexpr float PART_FILL_ALPHA = 0.2F;
constexpr float HEAD_DEAD_ALPHA = 0.55F;
constexpr float HEAD_BAND_ALPHA = 0.7F;
constexpr float STANDING_JUMP_ALPHA = 0.6F;
constexpr float FACING_ALPHA = 0.95F;
constexpr float LABEL_BACK_ALPHA = 0.7F;

// Where each label sits round him (degrees from his facing).
constexpr float TEAM_MATE_BEARING = -135.0F;
constexpr float OPPONENT_BEARING = -150.0F;
constexpr float PRESSURE_BEARING = -165.0F;
constexpr float MEET_BEARING = 30.0F;
constexpr float COLLECT_BEARING = 40.0F;
constexpr float SLIDE_BEARING = 150.0F;
constexpr float STANDING_BEARING = 160.0F;
constexpr float SHOT_BLOCK_BEARING = -60.0F;
constexpr float FEET_REACH_BEARING = -20.0F;
constexpr float COMFORT_BEARING = -40.0F;
constexpr float HEAD_REACH_BEARING = -30.0F;
constexpr float FOREHEAD_BEARING = -50.0F;
constexpr float LOWEST_BEARING = -70.0F;
constexpr float TOP_ONLY_BEARING = 150.0F;
constexpr float JUMP_BEARING = 30.0F;
constexpr float STANDING_JUMP_BEARING = 60.0F;

// Gaps (m): the foot-side arcs and their labels, his height marks.
constexpr float FOOT_SIDE_GAP = 0.05F;
constexpr float FOOT_LABEL_GAP = 0.1F;
constexpr float MARK_GAP = 0.01F;
constexpr float MARK_LABEL_GAP = 0.05F;
/** The standing jump's ring, as a share of his head reach. */
constexpr float STANDING_JUMP_RING = 0.75F;

// Lying: each rounded end in steps, a rib every so many points.
constexpr int LYING_END_STEPS = 18;
constexpr std::size_t LYING_RIB_EVERY = 6;

// Send cones: each edge in steps over the full width.
constexpr int CONE_EDGE_STEPS = 8;
constexpr float CONE_HALF_STEPS = CONE_EDGE_STEPS / 2.0F;

// The facing arrow: its tip past his feet reach, its barbs (m, degrees).
constexpr float ARROW_TIP = 0.45F;
constexpr float ARROW_BARB = 0.3F;
constexpr float ARROW_BARB_DEGREES = 8.0F;

// A shot's leg ghost.
constexpr float THIGH_THICKNESS = 6.0F;
constexpr float SHIN_THICKNESS = 4.0F;
constexpr std::size_t FOOT_CORNERS = 8;

// Labels: a dark box from just right of the point, the text inside it
// (pixels).
constexpr float LABEL_BACK_LEFT = 3.0F;
constexpr float LABEL_BACK_RIGHT = 9.0F;
constexpr float LABEL_TEXT_LEFT = 6.0F;
constexpr float LABEL_ROUNDING = 3.0F;

// The values table.
constexpr float SHAPE_COLUMN_WEIGHT = 1.3F;
constexpr float VALUE_COLUMN_WEIGHT = 0.8F;
constexpr float FROM_COLUMN_WEIGHT = 1.4F;
constexpr float HEADING_FONT_SIZE = 14.0F;
constexpr ImVec4 WIP_TEXT{0.95F, 0.75F, 0.25F, 1.0F};

float radians(float degrees) { return degrees * PI / HALF_TURN; }

ImU32 colour(int r, int g, int b, float alpha)
{
  return IM_COL32(
      r, g, b, static_cast<int>(std::clamp(alpha, 0.0F, 1.0F) * MAX_CHANNEL));
}

/** `tint` with its alpha set. */
ImU32 withAlpha(ImU32 tint, float alpha)
{
  return (tint & ~IM_COL32_A_MASK) |
         (static_cast<ImU32>(std::clamp(alpha, 0.0F, 1.0F) * MAX_CHANNEL)
          << IM_COL32_A_SHIFT);
}

struct Rgb
{
  int r = 0;
  int g = 0;
  int b = 0;
};

// The palette.
constexpr Rgb WHITE{.r = 255, .g = 255, .b = 255};
constexpr Rgb PALE{.r = 240, .g = 240, .b = 240};
constexpr Rgb LINE_GREY{.r = 230, .g = 230, .b = 230};
constexpr Rgb OFF_GREY{.r = 110, .g = 110, .b = 120};
constexpr Rgb LABEL_BACK{.r = 10, .g = 12, .b = 16};
constexpr Rgb BODY_EDGE_BLUE{.r = 150, .g = 175, .b = 210};
constexpr Rgb BODY_SIDE_BLUE{.r = 110, .g = 140, .b = 190};
constexpr Rgb BALL_HIT_YELLOW{.r = 240, .g = 200, .b = 90};
constexpr Rgb TEAM_MATE_GREEN{.r = 120, .g = 210, .b = 140};
constexpr Rgb OPPONENT_ORANGE{.r = 240, .g = 160, .b = 80};
constexpr Rgb PRESSURE_RED{.r = 230, .g = 90, .b = 90};
constexpr Rgb DRIBBLE_CYAN{.r = 80, .g = 210, .b = 230};
constexpr Rgb TACKLE_VIOLET{.r = 200, .g = 120, .b = 220};
constexpr Rgb SHOT_BLOCK_PINK{.r = 230, .g = 110, .b = 170};
constexpr Rgb DEAD_ZONE_GREY{.r = 70, .g = 70, .b = 76};
constexpr Rgb DEAD_EDGE_RED{.r = 235, .g = 70, .b = 70};
constexpr Rgb COMFORT_GREEN{.r = 90, .g = 230, .b = 120};
constexpr Rgb WEAK_FOOT_BLUE{.r = 90, .g = 140, .b = 255};
/** The stronger foot's side, and a stride's shin. */
constexpr Rgb AMBER{.r = 255, .g = 200, .b = 90};
constexpr Rgb THIGH_ORANGE{.r = 240, .g = 150, .b = 70};
constexpr Rgb CHEST_YELLOW{.r = 240, .g = 220, .b = 90};
constexpr Rgb HEAD_TOP_PURPLE{.r = 150, .g = 110, .b = 220};
constexpr Rgb JUMP_BLUE{.r = 120, .g = 200, .b = 255};
constexpr Rgb STRIDE_THIGH_ORANGE{.r = 255, .g = 160, .b = 60};
constexpr Rgb STRIDE_FOOT_YELLOW{.r = 255, .g = 230, .b = 140};
constexpr Rgb BLOCK_THIGH_BLUE{.r = 60, .g = 200, .b = 255};
constexpr Rgb BLOCK_SHIN_BLUE{.r = 110, .g = 220, .b = 255};
constexpr Rgb BLOCK_FOOT_BLUE{.r = 170, .g = 240, .b = 255};

ImU32 colour(Rgb rgb, float alpha)
{
  return colour(rgb.r, rgb.g, rgb.b, alpha);
}

/** 0-1 on the contact view's dark-to-hot scale. */
// Argument order: value then alpha, as the colour helpers take them.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
ImU32 heat(float value, float alpha)
{
  constexpr std::array<ImVec4, 5> STOPS{{{0.14F, 0.17F, 0.26F, 1.0F},
                                         {0.15F, 0.42F, 0.55F, 1.0F},
                                         {0.35F, 0.66F, 0.40F, 1.0F},
                                         {0.94F, 0.78F, 0.24F, 1.0F},
                                         {0.86F, 0.24F, 0.20F, 1.0F}}};
  const float scaled = std::clamp(value, 0.0F, 1.0F) * 4.0F;
  const int index = std::min(static_cast<int>(scaled), 3);
  const float t = scaled - static_cast<float>(index);
  const ImVec4& a = STOPS.at(static_cast<std::size_t>(index));
  const ImVec4& b = STOPS.at(static_cast<std::size_t>(index) + 1);
  return ImGui::GetColorU32(ImVec4(a.x + ((b.x - a.x) * t),
                                   a.y + ((b.y - a.y) * t),
                                   a.z + ((b.z - a.z) * t), alpha));
}

/** A share of power, 1 green down to 0 red. */
// Argument order: share then alpha, as the colour helpers take them.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
ImU32 powerColour(float share, float alpha)
{
  const float s = std::clamp(share, 0.0F, 1.0F);
  return ImGui::GetColorU32(ImVec4(POWER_RED - (POWER_SWING * s),
                                   POWER_GREEN + (POWER_SWING * s), POWER_BLUE,
                                   alpha));
}

/** Rings at a stretch map's distances, 12 sections each, coloured by the
 * stretch at each section's middle; `blocked` sections (out of reach of the
 * part) in `blockedTint`. */
template <typename Blocked>
void stretchRings(const Painter& paint, const StretchMap& map, float reach,
                  float z, float alpha, Blocked blocked, ImU32 blockedTint)
{
  const auto& distances = map.distances;
  for (std::size_t ringIndex = 0; ringIndex + 1 < distances.size(); ++ringIndex)
  {
    const float inner = distances.at(ringIndex) * reach;
    const float outer = distances.at(ringIndex + 1) * reach;
    const float middle = 0.5F * (inner + outer);
    for (int section = 0; section < SECTIONS; ++section)
    {
      const float from =
          -HALF_TURN + (SECTION_DEGREES * static_cast<float>(section));
      const float centre = from + HALF_SECTION;
      const ImU32 tint =
          blocked(centre, middle)
              ? blockedTint
              : heat(map.at(centre, middle / reach), 0.6F * alpha);
      paint.sector(inner, outer, z, from, from + SECTION_DEGREES, tint);
    }
    paint.ring(outer, z, colour(WHITE, RING_ALPHA * alpha), 1.0F);
  }
  for (int section = 0; section < SECTIONS; ++section)
  {
    const float edge =
        -HALF_TURN + (SECTION_DEGREES * static_cast<float>(section));
    paint.line(paint.polar(edge, 0.0F, z), paint.polar(edge, reach, z),
               colour(WHITE, SECTION_EDGE_ALPHA * alpha), 1.0F);
  }
}
}  // namespace

// ---- Painter ---------------------------------------------------------------

Vec3 Painter::polar(float degrees, float radius, float z) const
{
  return polarFrom(centre, degrees, radius, z);
}

// Argument order: degrees then radius, as polar coordinates go.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Vec3 Painter::polarFrom(Vec3 from, float degrees, float radius, float z) const
{
  const float angle = facing + radians(degrees);
  return {.x = from.x + (std::sin(angle) * radius),
          .y = from.y + (std::cos(angle) * radius),
          .z = z};
}

void Painter::line(Vec3 a, Vec3 b, ImU32 tint, float thickness) const
{
  ImVec2 from;
  ImVec2 to;
  if (at(a, from) && at(b, to))
  {
    draw->AddLine(from, to, tint, thickness);
  }
}

void Painter::polyline(const std::vector<Vec3>& points, ImU32 tint,
                       float thickness, bool closed) const
{
  std::vector<ImVec2> screen;
  screen.reserve(points.size());
  for (const Vec3& point : points)
  {
    ImVec2 projected;
    if (!at(point, projected))
    {
      return;
    }
    screen.push_back(projected);
  }
  draw->AddPolyline(screen.data(), static_cast<int>(screen.size()), tint,
                    closed ? ImDrawFlags_Closed : ImDrawFlags_None, thickness);
}

void Painter::fill(const std::vector<Vec3>& points, ImU32 tint) const
{
  std::vector<ImVec2> screen;
  screen.reserve(points.size());
  for (const Vec3& point : points)
  {
    ImVec2 projected;
    if (!at(point, projected))
    {
      return;
    }
    screen.push_back(projected);
  }
  draw->AddConvexPolyFilled(screen.data(), static_cast<int>(screen.size()),
                            tint);
}

// Argument order: angles from and to, then tint, as ImDrawList paths go.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void Painter::arc(float radius, float z, float from, float to, ImU32 tint,
                  float thickness) const
{
  const int steps = std::max(4, static_cast<int>(std::abs(to - from) / 5.0F));
  std::vector<Vec3> points;
  for (int step = 0; step <= steps; ++step)
  {
    points.push_back(polar(from + ((to - from) * static_cast<float>(step) /
                                   static_cast<float>(steps)),
                           radius, z));
  }
  polyline(points, tint, thickness, false);
}

void Painter::ring(float radius, float z, ImU32 tint, float thickness) const
{
  arc(radius, z, -HALF_TURN, HALF_TURN, tint, thickness);
}

void Painter::dashedRing(float radius, float z, ImU32 tint,
                         float thickness) const
{
  for (int dash = 0; dash < DASHES; ++dash)
  {
    const float from =
        -HALF_TURN + (DASH_EVERY_DEGREES * static_cast<float>(dash));
    arc(radius, z, from, from + DASH_DEGREES, tint, thickness);
  }
}

// Argument order: radii, height, angles, then tint, as ImDrawList paths go.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void Painter::sector(float inner, float outer, float z, float from, float to,
                     ImU32 tint) const
{
  const int steps = std::max(1, static_cast<int>(std::abs(to - from) / 6.0F));
  for (int step = 0; step < steps; ++step)
  {
    const float a = from + ((to - from) * static_cast<float>(step) /
                            static_cast<float>(steps));
    const float b = from + ((to - from) * static_cast<float>(step + 1) /
                            static_cast<float>(steps));
    fill({polar(a, inner, z), polar(b, inner, z), polar(b, outer, z),
          polar(a, outer, z)},
         tint);
  }
}

void Painter::cylinder(float radius, float bottom, float top, float from,
                       float to, ImU32 tint, float thickness) const
{
  arc(radius, bottom, from, to, tint, thickness);
  arc(radius, top, from, to, tint, thickness);
  const int ribs = static_cast<int>(std::floor((to - from + RIB_TOLERANCE) /
                                               SECTION_DEGREES)) +
                   1;
  for (int rib = 0; rib < ribs; ++rib)
  {
    const float degrees = from + (static_cast<float>(rib) * SECTION_DEGREES);
    line(polar(degrees, radius, bottom), polar(degrees, radius, top), tint,
         thickness * RIB_SHARE);
  }
  if (to - from < FULL_RING_SPAN)
  {
    // A part cylinder: closed back to its axis.
    for (const float z : {bottom, top})
    {
      line(polar(0.0F, 0.0F, z), polar(from, radius, z), tint, thickness);
      line(polar(0.0F, 0.0F, z), polar(to, radius, z), tint, thickness);
    }
  }
}

void Painter::cylinderSides(float radius, float bottom, float top,
                            ImU32 tint) const
{
  for (int step = 0; step < SIDE_PANELS; ++step)
  {
    const float a =
        -HALF_TURN + (SIDE_PANEL_DEGREES * static_cast<float>(step));
    const float b = a + SIDE_PANEL_DEGREES;
    fill({polar(a, radius, bottom), polar(b, radius, bottom),
          polar(b, radius, top), polar(a, radius, top)},
         tint);
  }
}

// ---- Tuning ----------------------------------------------------------------

Tuning Tuning::from(const FirstTouchKnobs& touch, const StrikeKnobs& strikes,
                    const StretchMap& stretch, StrikeKind strikeKind)
{
  Tuning tuning;
  tuning.feetReach = touch[TouchKnob::FEET_REACH];
  tuning.comfortGap = touch[TouchKnob::COMFORT_GAP];
  tuning.deadAngle = touch[TouchKnob::DEAD_ZONE_ANGLE];
  tuning.pressureRadius = touch[TouchKnob::PRESSURE_RADIUS];
  tuning.feetStretch = stretch;
  // A header's profile keeps the head's own stretch map; every other
  // strike reads the feet's (as the engine sets them).
  tuning.header = strikeContactProfile(strikes, StrikeKind::HEADER, touch);
  tuning.strike = strikeContactProfile(strikes, strikeKind, touch);
  tuning.strike.stretchMap = stretch;
  tuning.strikeKind = strikeKind;
  return tuning;
}

// ---- The body
// ----------------------------------------------------------------

namespace
{
/** A ring of the lying outline: its height, and its radius from his line. */
struct LyingRing
{
  float z = 0.0F;
  float radius = 0.0F;
};

/** One thigh or chest part: its band, reach, and where it sends the ball. */
struct BodyPart
{
  const char* name;
  float bottom;
  float top;
  float reach;
  bool on;
  Technique owner;
  ImU32 tint;
  float sideDegrees;
  float upFrom;
  float upTo;
};

/** Draws one body's shapes, layer by layer, with their labels and entries. */
class BodyDrawer
{
 public:
  BodyDrawer(Painter base, const Body& drawn, const Tuning& knobs,
             const Layers& shown, Technique picked, Detail detail,
             std::vector<Label>* labelsOut, std::vector<Entry>* entriesOut)
      : paint(std::move(base)),
        body(drawn),
        tuning(&knobs),
        layers(shown),
        technique(picked),
        full(detail == Detail::FULL),
        labels(labelsOut),
        entries(entriesOut),
        height(drawn.height),
        feetHeight(drawn.feetHeight),
        lines(bodyLinesOfHeight(drawn.height)),
        // In the air his feet, thigh and chest play nothing (the touch sweep
        // skips airborne players); down, nothing of his plays it.
        groundPartsOn(!drawn.airborne && !drawn.lying),
        thighOn(groundPartsOn && !drawn.thighOff),
        z0(drawn.at.z),
        feetReach(knobs.feetReach),
        comfortGap(knobs.comfortGap),
        deadAngle(knobs.deadAngle)
  {
    paint.centre = drawn.at;
    paint.facing = drawn.facing;
  }

  void draw()
  {
    if (layers.body || !full)
    {
      drawCylinder();
    }
    if (!body.binturong)
    {
      add("Aardvark body", "cylinder only",
          "the true view draws the Binturong body's shapes; this side plays "
          "another body");
      return;
    }
    if (body.goalkeeper)
    {
      add("Goalkeeper", "cylinder only",
          "keepers keep the old trap and their hands; their shapes are a "
          "follow-up");
      return;
    }
    if (!full)
    {
      drawOutline();
      return;
    }
    if (layers.personalSpace && !body.lying)
    {
      drawPersonalSpace();
    }
    if (layers.technique && groundPartsOn)
    {
      drawTechniqueRanges();
    }
    if (layers.feet && !body.lying)
    {
      drawFeet();
    }
    if (layers.technique && groundPartsOn &&
        (technique == Technique::FOOT_STRIKE || technique == Technique::HEADER))
    {
      drawPowerByAngle();
    }
    if (layers.lines && !body.lying)
    {
      drawLines();
    }
    if (layers.thighChest && !body.lying)
    {
      drawThighAndChest();
    }
    if (layers.head && !body.lying)
    {
      drawHead();
      drawJump();
    }
    if (layers.facing && !body.lying)
    {
      drawFacing();
    }
  }

 private:
  static constexpr float RADIUS = 0.5F * MatchTuning::Body::DIAMETER_METRES;

  void label(Vec3 at, std::string text, ImU32 tint) const
  {
    if ((labels != nullptr) && layers.labels)
    {
      labels->push_back({.at = at, .text = std::move(text), .tint = tint});
    }
  }

  void add(std::string what, std::string value, std::string source,
           std::string wip = {}) const
  {
    if (entries != nullptr)
    {
      entries->push_back({.what = std::move(what),
                          .value = std::move(value),
                          .source = std::move(source),
                          .wip = std::move(wip)});
    }
  }

  /** Full strength for the picked technique's parts, faded otherwise. */
  [[nodiscard]] float picked(std::initializer_list<Technique> owners) const
  {
    if (technique == Technique::ALL)
    {
      return 1.0F;
    }
    for (const Technique owner : owners)
    {
      if (owner == technique)
      {
        return 1.0F;
      }
    }
    return UNPICKED_ALPHA;
  }

  // ---- The body cylinder (every body) -------------------------------------

  [[nodiscard]] static ImU32 edgeTint()
  {
    return colour(BODY_EDGE_BLUE, BODY_EDGE_ALPHA);
  }

  /** His sides: in a match his shirt colour tells the sides apart. */
  [[nodiscard]] ImU32 sideTint() const
  {
    return body.kit != 0
               ? withAlpha(body.kit,
                           full ? KIT_SIDE_ALPHA : KIT_SIDE_OUTLINE_ALPHA)
               : colour(BODY_SIDE_BLUE,
                        full ? PLAIN_SIDE_ALPHA : PLAIN_SIDE_OUTLINE_ALPHA);
  }

  void drawCylinder() const
  {
    if (body.lying)
    {
      drawLying();
      return;
    }
    const ImU32 edge = edgeTint();
    const ImU32 side = sideTint();
    if (body.kit != 0)
    {
      paint.sector(0.0F, RADIUS, z0 + BASE_LIFT, -HALF_TURN, HALF_TURN,
                   withAlpha(body.kit, BODY_BASE_ALPHA));
    }
    paint.cylinderSides(RADIUS, z0 + feetHeight, z0 + feetHeight + height,
                        side);
    paint.cylinder(RADIUS, z0 + feetHeight, z0 + feetHeight + height,
                   -HALF_TURN, HALF_TURN, edge, LINE);
    add("Body: an upright cylinder, across",
        std::format("{:.2f} m", MatchTuning::Body::DIAMETER_METRES),
        "Body::DIAMETER_METRES");
    add("Body: tall (his height)", std::format("{:.2f} m", height),
        "player height");
  }

  /** Half his length less the rounded end, lying down. */
  [[nodiscard]] float lyingHalf() const
  {
    return std::max(0.0F, (HALF * height) - RADIUS);
  }

  /** Down: his outline from above, a stadium of the ring's radius at its
   * height. */
  [[nodiscard]] std::vector<Vec3> lyingOutline(const Painter& flat,
                                               LyingRing ring) const
  {
    const float half = lyingHalf();
    const float z = ring.z;
    const float r = ring.radius;
    std::vector<Vec3> points;
    const Vec3 head = flat.polar(0.0F, half, z0);
    const Vec3 feet = flat.polar(HALF_TURN, half, z0);
    for (int step = 0; step <= LYING_END_STEPS; ++step)
    {
      points.push_back(
          flat.polarFrom(head,
                         QUARTER_TURN - (HALF_TURN * static_cast<float>(step) /
                                         static_cast<float>(LYING_END_STEPS)),
                         r, z0 + z));
    }
    for (int step = 0; step <= LYING_END_STEPS; ++step)
    {
      points.push_back(
          flat.polarFrom(feet,
                         -QUARTER_TURN - (HALF_TURN * static_cast<float>(step) /
                                          static_cast<float>(LYING_END_STEPS)),
                         r, z0 + z));
    }
    return points;
  }

  /** Down (collision plan step 4): seen from above a stadium, his length
   * with rounded ends, 0.4 m high. */
  void drawLying() const
  {
    using C = MatchTuning::Collision;
    const ImU32 edge = edgeTint();
    const ImU32 side = sideTint();
    Painter flat = paint;
    flat.facing = body.lyingAngle;
    const float half = lyingHalf();
    const float bodyTop = MatchTuning::Body::DIAMETER_METRES;
    const std::vector<Vec3> bottom =
        lyingOutline(flat, {.z = 0.0F, .radius = RADIUS});
    const std::vector<Vec3> top =
        lyingOutline(flat, {.z = bodyTop, .radius = RADIUS});
    paint.fill(top, side);
    paint.polyline(bottom, edge, LINE, true);
    paint.polyline(top, edge, LINE, true);
    for (std::size_t index = 0; index < bottom.size(); index += LYING_RIB_EVERY)
    {
      paint.line(bottom[index], top[index], edge, 1.0F);
    }
    if (!full)
    {
      return;
    }
    // A ball whose centre comes this close below this height hits him.
    const float ballReach = RADIUS + MatchTuning::Units::BALL_RADIUS_METRES;
    const ImU32 hit = colour(BALL_HIT_YELLOW, 0.8F);
    paint.polyline(lyingOutline(flat, {.z = C::FALLEN_BALL_BELOW_METRES,
                                       .radius = ballReach}),
                   hit, 1.0F, true);
    paint.polyline(lyingOutline(flat, {.z = 0.0F, .radius = ballReach}), hit,
                   1.0F, true);
    label(flat.polar(0.0F, half + ballReach, z0 + C::FALLEN_BALL_BELOW_METRES),
          std::format(
              "ball's centre within {:.2f} m, below {:.2f} m: off his body",
              ballReach, C::FALLEN_BALL_BELOW_METRES),
          hit);
    label(flat.polar(HALF_TURN, half + RADIUS, z0 + bodyTop),
          std::format("lying {:.2f} m long, {:.2f} m high", height, bodyTop),
          colour(BODY_EDGE_BLUE, 1.0F));
    add("Down: his body seen from above",
        std::format("{:.2f} x {:.2f} m, ends rounded", height,
                    MatchTuning::Body::DIAMETER_METRES),
        "lyingBody (match_collisions.cpp): height long, BODY_DIAMETER "
        "across");
    add("Down: the ball comes off him",
        std::format("centre within {:.2f} m, below {:.2f} m", ballReach,
                    C::FALLEN_BALL_BELOW_METRES),
        "Collision::FALLEN_BALL_BELOW_METRES, body radius + ball radius");
    add("Down: off his body",
        std::format("{:.0f}% back, {:.0f}% along kept",
                    C::FALLEN_BOUNCE_BACK_SHARE * 100.0F,
                    C::FALLEN_ALONG_KEPT_SHARE * 100.0F),
        "Collision::FALLEN_BOUNCE_BACK_SHARE, FALLEN_ALONG_KEPT_SHARE");
    add("Down: plays the ball", "never",
        "fallen players are out of the touch sweep");
    add("Personal space", "none", "a fallen player is hurdled, not run into");
  }

  // ---- Outline: his feet reach and facing only ----------------------------

  void drawOutline() const
  {
    if (!groundPartsOn)
    {
      return;
    }
    if (body.kit != 0)
    {
      paint.ring(feetReach, z0 + FEET_LIFT, withAlpha(body.kit, KIT_RING_ALPHA),
                 LINE_HEAVY);
    }
    else
    {
      paint.ring(feetReach, z0 + FEET_LIFT, colour(PALE, PLAIN_RING_ALPHA),
                 LINE_FINE);
    }
    paint.line(paint.polar(0.0F, RADIUS, z0 + ARROW_LIFT),
               paint.polar(0.0F, feetReach, z0 + ARROW_LIFT),
               colour(WHITE, OUTLINE_FACING_ALPHA), LINE);
  }

  // ---- Personal space -----------------------------------------------------

  void drawPersonalSpace() const
  {
    using C = MatchTuning::Collision;
    const float teamMate = C::OVERTAKE_TEAM_MATE_CLEARANCE_METRES;
    const float opponent = C::OVERTAKE_OPPONENT_CLEARANCE_METRES;
    const float pressure = tuning->pressureRadius;
    paint.dashedRing(teamMate, z0 + SPACE_LIFT,
                     colour(TEAM_MATE_GREEN, SPACE_ALPHA));
    paint.dashedRing(opponent, z0 + SPACE_LIFT,
                     colour(OPPONENT_ORANGE, SPACE_ALPHA));
    paint.dashedRing(pressure, z0 + SPACE_LIFT,
                     colour(PRESSURE_RED, PRESSURE_ALPHA));
    label(paint.polar(TEAM_MATE_BEARING, teamMate, z0),
          std::format("team-mate passes {:.1f} m", teamMate),
          colour(TEAM_MATE_GREEN, 1.0F));
    label(paint.polar(OPPONENT_BEARING, opponent, z0),
          std::format("opponent passes {:.1f} m", opponent),
          colour(OPPONENT_ORANGE, 1.0F));
    label(paint.polar(PRESSURE_BEARING, pressure, z0),
          std::format("pressure on his touch {:.1f} m", pressure),
          colour(PRESSURE_RED, 1.0F));
    add("Team-mate clearance (overtaking, way round)",
        std::format("{:.2f} m", teamMate),
        "Collision::OVERTAKE_TEAM_MATE_CLEARANCE_METRES");
    add("Opponent clearance (overtaking, way round)",
        std::format("{:.2f} m", opponent),
        "Collision::OVERTAKE_OPPONENT_CLEARANCE_METRES");
    add("Pressure on a touch (0 at the edge, 1 on top)",
        std::format("{:.2f} m", pressure), "touch knob Pressure radius");
  }

  // ---- Technique ranges on the floor --------------------------------------

  void drawTechniqueRanges() const
  {
    if (technique == Technique::DRIBBLE || technique == Technique::ALL)
    {
      drawDribbleRange();
    }
    if (technique == Technique::TACKLE || technique == Technique::ALL)
    {
      drawTackleRange();
    }
    if (technique == Technique::SHOT_BLOCK || technique == Technique::ALL)
    {
      drawShotBlock();
    }
  }

  void drawDribbleRange() const
  {
    const float alpha = technique == Technique::DRIBBLE ? 1.0F : 0.45F;
    const float meet = dribbleMeetGapOf(comfortGap, body.dribbling);
    const float collect = MatchTuning::Dribble::KICK_REACH_METRES;
    paint.ring(meet, z0 + BASE_LIFT, colour(DRIBBLE_CYAN, alpha), LINE_BOLD);
    paint.dashedRing(collect, z0 + BASE_LIFT, colour(DRIBBLE_CYAN, alpha),
                     LINE);
    label(paint.polar(MEET_BEARING, meet, z0),
          std::format("dribble meet {:.2f} m", meet),
          colour(DRIBBLE_CYAN, alpha));
    label(paint.polar(COLLECT_BEARING, collect, z0),
          std::format("collects within {:.2f} m", collect),
          colour(DRIBBLE_CYAN, alpha));
    add("Dribble touch: where he meets it", std::format("{:.2f} m", meet),
        "dribbleMeetGapOf: comfort gap (Dribbling 99) to kick reach (1)");
    add("Dribble: collects it within", std::format("{:.2f} m", collect),
        "Dribble::KICK_REACH_METRES");
  }

  void drawTackleRange() const
  {
    const float alpha = technique == Technique::TACKLE ? 1.0F : 0.4F;
    const float standing = standingTackleReachOf(body.defending);
    const float slide = MatchTuning::Defending::SLIDE_TACKLE_DISTANCE_METRES;
    for (int section = 0; section < SECTIONS; ++section)
    {
      const float from =
          -HALF_TURN + (SECTION_DEGREES * static_cast<float>(section));
      paint.sector(standing, slide, z0 + TACKLE_FILL_LIFT, from,
                   from + SECTION_DEGREES,
                   colour(TACKLE_VIOLET,
                          (TACKLE_FILL_ALPHA * alpha) +
                              (section % 2 == 0 ? TACKLE_STRIPE_ALPHA : 0.0F)));
    }
    paint.ring(standing, z0 + TACKLE_RING_LIFT, colour(TACKLE_VIOLET, alpha),
               LINE);
    paint.ring(slide, z0 + TACKLE_RING_LIFT, colour(TACKLE_VIOLET, alpha),
               LINE);
    label(paint.polar(SLIDE_BEARING, slide, z0),
          std::format("slide tackle roll {:.1f} m [WIP]", slide),
          colour(TACKLE_VIOLET, alpha));
    label(paint.polar(STANDING_BEARING, standing, z0),
          std::format("standing range {:.2f} m [WIP]", standing),
          colour(TACKLE_VIOLET, alpha));
    add("Slide tackle (a roll from here out)", std::format("{:.2f} m", slide),
        "Defending::SLIDE_TACKLE_DISTANCE_METRES",
        "old dice roll; slides are only a flag (no movement), untouched by "
        "the tackling plan");
    add("Standing tackle range (old roll)", std::format("{:.2f} m", standing),
        "standingTackleReachOf: 1.3 m + 0.3 m x Defending",
        "no standing roll inside it with the ball at his feet: a contest "
        "touch on the feet "
        "reach instead (tackling plan); here it is the slide's inner edge");
  }

  /** Shot block plan §3c: his torso, solid from the hip to the top of his
   * head; below it, his legs only where a stride rolled for a shot has them
   * (drawn as ghosts while one is recent). */
  void drawShotBlock() const
  {
    const float alpha = technique == Technique::SHOT_BLOCK ? 1.0F : 0.3F;
    const float torso = RADIUS + MatchTuning::Units::BALL_RADIUS_METRES;
    paint.cylinder(torso, z0 + feetHeight + lines.chest,
                   z0 + feetHeight + height, -HALF_TURN, HALF_TURN,
                   colour(SHOT_BLOCK_PINK, alpha * SHOT_BLOCK_ALPHA), 1.0F);
    label(paint.polar(SHOT_BLOCK_BEARING, torso, z0 + feetHeight + lines.chest),
          std::format("shot block: torso {:.2f} m, hip {:.2f} m to head [WIP]",
                      torso, lines.chest),
          colour(SHOT_BLOCK_PINK, alpha));
    add("Shot block: torso", std::format("{:.2f} m, hip to head", torso),
        "Body::RADIUS_METRES + ball radius; hip = chest line",
        "shot block plan: where the ball goes off him is still the old "
        "bounce");
    add("Shot block: legs", "where his stride has them",
        "legPoseOf (MatchTuning::Stride), rolled once per shot path",
        "natural positions only; no deliberate leg until the brain step");
  }

  // ---- Feet ---------------------------------------------------------------

  void drawFeet() const
  {
    const float alpha = picked({Technique::FIRST_TOUCH, Technique::FOOT_STRIKE,
                                Technique::DRIBBLE, Technique::TACKLE});
    const float z = z0 + FEET_LIFT;
    if (groundPartsOn)
    {
      drawFeetOnGround(alpha, z);
    }
    else
    {
      const ImU32 offTint = colour(OFF_GREY, 0.35F);
      paint.ring(feetReach, z, offTint, LINE);
      label(paint.polar(FEET_REACH_BEARING, feetReach, z0),
            "feet: off in the air", offTint);
    }
    add("Feet reach (touches and foot strikes)",
        std::format("{:.2f} m", feetReach), "touch knob Feet reach",
        groundPartsOn ? "" : "off: in the air");
    add("Feet: comfort gap (best technique)",
        std::format("{:.2f} m", comfortGap), "touch knob Comfort gap");
    add("Feet: dead zone, beyond the comfort gap",
        std::format("> {:.0f} deg off facing", deadAngle),
        "touch knob Dead zone angle");
    add("Feet: stretch (ring colours)", "0-1, 12 sections",
        "stretch map (contact view), at each section's middle");
    add("Feet band: up to his feet line", std::format("{:.2f} m", lines.feet),
        "Ball::FEET_LINE_HEIGHT_SHARE x height (his knee)");
    if (layers.facing && groundPartsOn)
    {
      drawFootSides(alpha, z);
    }
  }

  void drawFeetOnGround(float alpha, float z) const
  {
    const float gap = comfortGap;
    const float dead = deadAngle;
    stretchRings(
        paint, tuning->feetStretch, feetReach, z, alpha,
        [gap, dead](float centre, float middle)
        { return middle > gap && std::abs(centre) > dead; },
        colour(DEAD_ZONE_GREY, DEAD_ZONE_ALPHA * alpha));
    // The dead zone's edge, exactly.
    const ImU32 deadEdge = colour(DEAD_EDGE_RED, alpha);
    for (const float side : {-1.0F, 1.0F})
    {
      paint.line(paint.polar(side * deadAngle, comfortGap, z),
                 paint.polar(side * deadAngle, feetReach, z), deadEdge,
                 LINE_BOLD);
      paint.arc(comfortGap, z, side * deadAngle, side * HALF_TURN, deadEdge,
                LINE_BOLD);
    }
    paint.ring(comfortGap, z + SPACE_LIFT, colour(COMFORT_GREEN, alpha),
               LINE_HEAVY);
    paint.ring(feetReach, z + SPACE_LIFT, colour(PALE, alpha), LINE_HEAVY);
    // The band his feet play: up to his feet line.
    paint.cylinder(feetReach, z0, z0 + lines.feet, -HALF_TURN, HALF_TURN,
                   colour(PALE, FEET_BAND_ALPHA * alpha), 1.0F);
    label(paint.polar(FEET_REACH_BEARING, feetReach, z0),
          std::format("feet reach {:.2f} m", feetReach), colour(PALE, alpha));
    label(paint.polar(COMFORT_BEARING, comfortGap, z0),
          std::format("comfort gap {:.2f} m (best technique)", comfortGap),
          colour(COMFORT_GREEN, alpha));
    label(paint.polar(HALF_TURN, HALF * (comfortGap + feetReach), z0),
          std::format("dead zone: beyond {:.0f} deg", deadAngle), deadEdge);
  }

  /** His feet: the weaker foot's side of his facing line. */
  void drawFootSides(float alpha, float z) const
  {
    const float weakSide = body.rightFooted ? -1.0F : 1.0F;
    paint.arc(feetReach + FOOT_SIDE_GAP, z, 0.0F, weakSide * HALF_TURN,
              colour(WEAK_FOOT_BLUE, alpha), LINE_HEAVIEST);
    paint.arc(feetReach + FOOT_SIDE_GAP, z, 0.0F, -weakSide * HALF_TURN,
              colour(AMBER, alpha), LINE_HEAVIEST);
    label(paint.polar(weakSide * QUARTER_TURN, feetReach + FOOT_LABEL_GAP, z0),
          "weaker foot side", colour(WEAK_FOOT_BLUE, 1.0F));
    label(paint.polar(-weakSide * QUARTER_TURN, feetReach + FOOT_LABEL_GAP, z0),
          body.rightFooted ? "right foot side" : "left foot side",
          colour(AMBER, 1.0F));
    add("Weaker foot: a ball on this side of his facing line",
        body.rightFooted ? "his left" : "his right",
        "engine: the side of his facing line (+ his right, clockwise in the "
        "engine's frame)");
  }

  // ---- Power by angle -----------------------------------------------------

  void drawPowerByAngle() const
  {
    using A = MatchTuning::Aerial;
    const bool header = technique == Technique::HEADER;
    const ContactProfile& profile = header ? tuning->header : tuning->strike;
    const float inner =
        header ? A::HEAD_REACH_METRES + 0.05F : feetReach + 0.12F;
    const float outer = inner + 0.15F;
    const float z =
        header ? z0 + headBandOf(height, feetHeight).forehead : z0 + 0.008F;
    // Across his body (inward) is his weaker foot's side for the strong foot.
    const float inwardSide = body.rightFooted ? -1.0F : 1.0F;
    for (int section = 0; section < SECTIONS; ++section)
    {
      const float from =
          -HALF_TURN + (SECTION_DEGREES * static_cast<float>(section));
      const float centre = from + HALF_SECTION;
      const bool inward = centre * inwardSide > 0.0F;
      const float share = powerByAngleShare(
          std::abs(centre),
          inward ? profile.powerInwardSquare : profile.powerOutwardSquare,
          profile.powerBehind);
      paint.sector(inner, outer, z, from, from + SECTION_DEGREES,
                   powerColour(share, POWER_ALPHA));
    }
    label(paint.polar(0.0F, outer, z), "power 100%", powerColour(1.0F, 1.0F));
    label(paint.polar(inwardSide * QUARTER_TURN, outer, z),
          std::format("inward {:.0f}%", profile.powerInwardSquare * 100.0F),
          powerColour(profile.powerInwardSquare, 1.0F));
    label(paint.polar(-inwardSide * QUARTER_TURN, outer, z),
          std::format("outward {:.0f}%", profile.powerOutwardSquare * 100.0F),
          powerColour(profile.powerOutwardSquare, 1.0F));
    label(paint.polar(HALF_TURN, outer, z),
          std::format("behind {:.0f}%", profile.powerBehind * 100.0F),
          powerColour(profile.powerBehind, 1.0F));
    const std::string kind =
        header ? "Header"
               : std::string(STRIKE_KIND_NAMES.at(
                     static_cast<std::size_t>(tuning->strikeKind)));
    add(kind + ": power left square inward / outward / behind",
        std::format(
            "{:.0f}% / {:.0f}% / {:.0f}%", profile.powerInwardSquare * 100.0F,
            profile.powerOutwardSquare * 100.0F, profile.powerBehind * 100.0F),
        "strike knobs Power inward/outward square, Power behind "
        "(powerByAngleShare)",
        "first guesses (shooting technique plan step 4); drawn as if met in "
        "front of him: "
        "the engine picks inward or outward with turnsOutward");
  }

  // ---- His lines ----------------------------------------------------------

  void drawLines() const
  {
    struct Line
    {
      float z;
      const char* name;
    };
    const std::array<Line, 4> marks{{
        {.z = lines.feet, .name = "feet line (knee)"},
        {.z = lines.chest, .name = "thigh / chest line"},
        {.z = lines.head, .name = "head line (shoulders)"},
        {.z = height, .name = "top of his head"},
    }};
    for (const Line& mark : marks)
    {
      paint.ring(RADIUS + MARK_GAP, z0 + feetHeight + mark.z,
                 colour(WHITE, MARK_ALPHA), LINE_BOLD);
      label(paint.polar(QUARTER_TURN, RADIUS + MARK_LABEL_GAP,
                        z0 + feetHeight + mark.z),
            std::format("{} {:.2f} m ({:.2f} x h)", mark.name, mark.z,
                        mark.z / height),
            colour(LINE_GREY, 1.0F));
    }
    add("Thigh / chest line", std::format("{:.2f} m", lines.chest),
        "Ball::CHEST_LINE_HEIGHT_SHARE x height (top of a raised thigh)");
    add("Head line", std::format("{:.2f} m", lines.head),
        "Ball::HEAD_LINE_HEIGHT_SHARE x height (top of his shoulders)");
    if (feetHeight > 0.0F)
    {
      add("Every line, in the air", std::format("+{:.2f} m", feetHeight),
          "his feet's height");
    }
  }

  // ---- Thigh and chest ----------------------------------------------------

  void drawThighAndChest() const
  {
    using B = MatchTuning::Ball;
    const std::array<BodyPart, 2> parts{{
        {.name = "thigh",
         .bottom = lines.feet,
         .top = lines.chest,
         .reach = B::THIGH_REACH_METRES,
         .on = thighOn,
         .owner = Technique::THIGH,
         .tint = colour(THIGH_ORANGE, 1.0F),
         .sideDegrees = B::THIGH_MAX_SIDE_DEGREES,
         .upFrom = B::THIGH_MIN_UP_DEGREES,
         .upTo = B::THIGH_MAX_UP_DEGREES},
        {.name = "chest",
         .bottom = lines.chest,
         .top = lines.head,
         .reach = B::CHEST_REACH_METRES,
         .on = groundPartsOn,
         .owner = Technique::CHEST,
         .tint = colour(CHEST_YELLOW, 1.0F),
         .sideDegrees = B::CHEST_MAX_SIDE_DEGREES,
         .upFrom = 0.0F,
         .upTo = B::CHEST_MAX_UP_DEGREES},
    }};
    for (const BodyPart& part : parts)
    {
      drawPart(part);
    }
    addThighAndChestEntries();
  }

  void drawPart(const BodyPart& part) const
  {
    const float alpha = picked({part.owner});
    const ImU32 tint =
        part.on ? withAlpha(part.tint, alpha) : colour(OFF_GREY, 0.35F);
    const float bottom = z0 + feetHeight + part.bottom;
    const float top = z0 + feetHeight + part.top;
    // In front of him only: a half cylinder from his centre.
    paint.cylinder(part.reach, bottom, top, -QUARTER_TURN, QUARTER_TURN, tint,
                   LINE);
    if (part.on)
    {
      paint.sector(0.0F, part.reach, bottom, -QUARTER_TURN, QUARTER_TURN,
                   withAlpha(part.tint, PART_FILL_ALPHA * alpha));
    }
    label(paint.polar(0.0F, part.reach, HALF * (bottom + top)),
          part.on ? std::format("{} {:.2f} m, in front", part.name, part.reach)
                  : std::format("{}: off", part.name),
          tint);
    if (layers.sendCones && part.on)
    {
      drawSendCone(part, tint,
                   paint.polar(0.0F, part.reach, HALF * (bottom + top)));
    }
  }

  /** Where a part can send the ball: up to sideDegrees either side, from
   * upFrom to upTo above flat. */
  void drawSendCone(const BodyPart& part, ImU32 tint, Vec3 from) const
  {
    const auto direction = [&](float side, float up)
    {
      return paint.polarFrom(from, side, std::cos(radians(up)) * CONE_METRES,
                             from.z + (std::sin(radians(up)) * CONE_METRES));
    };
    std::vector<Vec3> edge;
    for (int step = 0; step <= CONE_EDGE_STEPS; ++step)
    {
      edge.push_back(direction(
          -part.sideDegrees +
              (part.sideDegrees * static_cast<float>(step) / CONE_HALF_STEPS),
          part.upFrom));
    }
    for (int step = 0; step <= CONE_EDGE_STEPS; ++step)
    {
      edge.push_back(direction(
          part.sideDegrees -
              (part.sideDegrees * static_cast<float>(step) / CONE_HALF_STEPS),
          part.upTo));
    }
    paint.polyline(edge, tint, LINE, true);
    for (const float side : {-part.sideDegrees, part.sideDegrees})
    {
      for (const float up : {part.upFrom, part.upTo})
      {
        paint.line(from, direction(side, up), tint, 1.0F);
      }
    }
  }

  void addThighAndChestEntries() const
  {
    using B = MatchTuning::Ball;
    const char* thighOff =
        groundPartsOn ? "off: moving faster than its limit" : "off now";
    add("Thigh: reach, in front only",
        std::format("{:.2f} m", B::THIGH_REACH_METRES),
        "Ball::THIGH_REACH_METRES", thighOn ? "" : thighOff);
    add("Thigh: only nearly stopped",
        std::format("<= {:.1f} m/s", B::THIGH_MAX_SPEED),
        "Ball::THIGH_MAX_SPEED", "placeholder (chest and thigh plan step 4)");
    add("Chest: reach, in front only",
        std::format("{:.2f} m", B::CHEST_REACH_METRES),
        "Ball::CHEST_REACH_METRES", groundPartsOn ? "" : "off now");
    if (!layers.sendCones)
    {
      return;
    }
    add("Chest: sends it",
        std::format("+/-{:.0f} deg, 0-{:.0f} deg up", B::CHEST_MAX_SIDE_DEGREES,
                    B::CHEST_MAX_UP_DEGREES),
        "Ball::CHEST_MAX_SIDE_DEGREES, CHEST_MAX_UP_DEGREES",
        "placeholders (step 3)");
    add("Thigh: sends it",
        std::format("+/-{:.0f} deg, {:.0f}-{:.0f} deg up",
                    B::THIGH_MAX_SIDE_DEGREES, B::THIGH_MIN_UP_DEGREES,
                    B::THIGH_MAX_UP_DEGREES),
        "Ball::THIGH_MAX_SIDE_DEGREES, THIGH_MIN/MAX_UP_DEGREES",
        "placeholders (step 4)");
  }

  // ---- Head ---------------------------------------------------------------

  void drawHead() const
  {
    using A = MatchTuning::Aerial;
    const float alpha = picked({Technique::HEADER});
    const HeadBand band = headBandOf(height, feetHeight);
    const float forehead = z0 + band.forehead;
    const float headReach = A::HEAD_REACH_METRES;
    const float headComfort = tuning->header.comfortGap;
    // Beyond his comfort gap and 60 degrees round, only the top of his
    // head reaches it (a ball at or above it).
    stretchRings(
        paint, tuning->header.stretchMap, headReach, forehead, alpha,
        [headComfort](float centre, float middle)
        {
          return middle > headComfort &&
                 std::abs(centre) > A::HEAD_DEAD_ZONE_DEGREES;
        },
        colour(HEAD_TOP_PURPLE, HEAD_DEAD_ALPHA * alpha));
    paint.ring(headComfort, forehead + SPACE_LIFT, colour(COMFORT_GREEN, alpha),
               LINE_BOLD);
    paint.cylinder(headReach, z0 + band.lowest, z0 + band.top, -HALF_TURN,
                   HALF_TURN, colour(PALE, HEAD_BAND_ALPHA * alpha), LINE_FINE);
    for (const float side : {-1.0F, 1.0F})
    {
      paint.line(
          paint.polar(side * A::HEAD_DEAD_ZONE_DEGREES, headComfort, forehead),
          paint.polar(side * A::HEAD_DEAD_ZONE_DEGREES, headReach, forehead),
          colour(HEAD_TOP_PURPLE, alpha), LINE_BOLD);
    }
    label(paint.polar(HEAD_REACH_BEARING, headReach, z0 + band.top),
          std::format("head reach {:.2f} m", headReach), colour(PALE, alpha));
    label(paint.polar(FOREHEAD_BEARING, headReach, forehead),
          std::format("forehead {:.2f} m", band.forehead), colour(PALE, alpha));
    label(paint.polar(LOWEST_BEARING, headReach, z0 + band.lowest),
          std::format("lowest (duck) {:.2f} m", band.lowest),
          colour(PALE, alpha));
    label(paint.polar(TOP_ONLY_BEARING, headReach, forehead),
          "top of head only", colour(HEAD_TOP_PURPLE, 1.0F));
    addHeadEntries(band, headComfort);
  }

  void addHeadEntries(const HeadBand& band, float headComfort) const
  {
    using A = MatchTuning::Aerial;
    add("Head: reach", std::format("{:.2f} m", A::HEAD_REACH_METRES),
        "Aerial::HEAD_REACH_METRES", "placeholder (heading plan step 4)");
    add("Head: comfort gap (best technique)",
        std::format("{:.2f} m", headComfort), "Aerial::HEAD_COMFORT_GAP_METRES",
        "placeholder");
    add("Head: forehead only within",
        std::format("{:.0f} deg of facing", A::HEAD_DEAD_ZONE_DEGREES),
        "Aerial::HEAD_DEAD_ZONE_DEGREES (beyond: top of head only)",
        "placeholder");
    add("Head: from (a duck, never below his head line)",
        std::format("{:.2f} m", band.lowest),
        "headBandOf: forehead - Aerial::DUCK_HEIGHT_SHARE x height");
    add("Head: forehead", std::format("{:.2f} m", band.forehead),
        "top - Aerial::HEAD_OFFSET_METRES");
    add("Head: top", std::format("{:.2f} m", band.top),
        "height + his feet's height");
    add("Head: stretch (ring colours)", "0-1, 12 sections",
        "Aerial::HEAD_STRETCH_* (the header's profile)", "placeholders");
  }

  /** His jump's top: Jumping reach (running), a share of it standing. */
  void drawJump() const
  {
    using A = MatchTuning::Aerial;
    const float alpha = picked({Technique::HEADER});
    const float headReach = A::HEAD_REACH_METRES;
    const ImU32 ceiling = colour(JUMP_BLUE, alpha);
    paint.dashedRing(headReach, z0 + body.jumpingReach, ceiling, LINE);
    paint.dashedRing(headReach * STANDING_JUMP_RING,
                     z0 + height + body.standingRise,
                     colour(JUMP_BLUE, STANDING_JUMP_ALPHA * alpha), 1.0F);
    label(
        paint.polar(JUMP_BEARING, headReach, z0 + body.jumpingReach),
        std::format("Jumping reach {:.2f} m (running jump)", body.jumpingReach),
        ceiling);
    label(paint.polar(STANDING_JUMP_BEARING, headReach * STANDING_JUMP_RING,
                      z0 + height + body.standingRise),
          std::format("standing jump {:.2f} m", height + body.standingRise),
          ceiling);
    add("Jumping reach (top of head, running jump)",
        std::format("{:.2f} m", body.jumpingReach),
        "Jumping reach: height + jump (0.25 m at 1 to 0.70 m at 99)",
        "stand-in blend of Pace and Physicality until the game carries it");
    add("Standing jump rises",
        std::format("{:.2f} m ({:.0f}%)", body.standingRise,
                    A::STANDING_JUMP_SHARE * 100.0F),
        "jumpRiseOf, Aerial::STANDING_JUMP_SHARE", "placeholder");
    add("Running jump needs",
        std::format(">= {:.1f} m/s", A::RUNNING_JUMP_MIN_SPEED),
        "Aerial::RUNNING_JUMP_MIN_SPEED", "placeholder");
  }

  // ---- Facing -------------------------------------------------------------

  void drawFacing() const
  {
    const ImU32 tint = colour(WHITE, FACING_ALPHA);
    const float length = std::max(feetReach, 1.0F);
    const Vec3 tip = paint.polar(0.0F, length + ARROW_TIP, z0 + ARROW_LIFT);
    paint.line(paint.polar(0.0F, RADIUS, z0 + ARROW_LIFT), tip, tint,
               LINE_HEAVY);
    for (const float barb : {-ARROW_BARB_DEGREES, ARROW_BARB_DEGREES})
    {
      paint.line(tip, paint.polar(barb, length + ARROW_BARB, z0 + ARROW_LIFT),
                 tint, LINE_HEAVY);
    }
    label(tip, "facing", tint);
  }

  Painter paint;
  Body body;
  const Tuning* tuning;
  Layers layers;
  Technique technique;
  bool full;
  std::vector<Label>* labels;
  std::vector<Entry>* entries;
  float height;
  float feetHeight;
  BodyLines lines;
  bool groundPartsOn;
  bool thighOn;
  float z0;
  float feetReach;
  float comfortGap;
  float deadAngle;
};
}  // namespace

void drawBody(const Painter& base, const Body& body, const Tuning& tuning,
              const Layers& layers, Technique technique, Detail detail,
              std::vector<Label>* labels, std::vector<Entry>* entries)
{
  BodyDrawer(base, body, tuning, layers, technique, detail, labels, entries)
      .draw();
}

namespace
{
/** A pose offset (the engine's metric frame) from his centre, lifted with
 * him in a jump. */
Vec3 legPoint(const Body& body, Point3F offset)
{
  return Vec3{.x = body.at.x + offset.x,
              .y = body.at.y + offset.y,
              .z = body.at.z + body.feetHeight + offset.z};
}

/** The booted foot: a box from the heel behind the ankle, sole at its rise. */
void drawBootedFoot(const Painter& paint, const Body& body,
                    const LegPose::Leg& leg, ImU32 foot)
{
  using S = MatchTuning::Stride;
  const float scale = body.height / S::REFERENCE_HEIGHT_METRES;
  const Vector2F forward = leg.footFacing;
  const Vector2F side{.x = -forward.y, .y = forward.x};
  const float heel = -S::HEEL_BEHIND_ANKLE * scale;
  const float toe = (S::FOOT_LENGTH - S::HEEL_BEHIND_ANKLE) * scale;
  const float halfWidth = 0.5F * S::FOOT_WIDTH * scale;
  const float sole = leg.ankle.z - (S::FOOT_HEIGHT * scale);
  std::array<Vec3, FOOT_CORNERS> corners{};
  std::size_t corner = 0;
  for (const float z : {sole, leg.ankle.z})
  {
    for (const float along : {heel, toe})
    {
      for (const float across : {-halfWidth, halfWidth})
      {
        corners.at(corner++) = legPoint(
            body, {.x = leg.ankle.x + (forward.x * along) + (side.x * across),
                   .y = leg.ankle.y + (forward.y * along) + (side.y * across),
                   .z = z});
      }
    }
  }
  // Edges between corners differing in one of the three choices.
  for (std::size_t a = 0; a < corners.size(); ++a)
  {
    for (const std::size_t bit : {1U, 2U, 4U})
    {
      if ((a & bit) == 0U)
      {
        paint.line(corners.at(a), corners.at(a | bit), foot, LINE);
      }
    }
  }
}
}  // namespace

// Argument order: fade then age, both on every call.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
void drawLegGhost(const Painter& paint, const Body& body, const ShotLegs& legs,
                  float fade, float ageSeconds, std::vector<Label>* labels)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
  // The pose's offsets are in the engine's metric frame, as a match's
  // painter is; from his centre, lifted with him in a jump.
  const auto at = [&body](Point3F offset) { return legPoint(body, offset); };
  // A stride in orange; a leg put in the way in cyan.
  const LegPose& pose = legs.pose;
  const bool block = legs.blocking;
  const ImU32 thigh = block ? colour(BLOCK_THIGH_BLUE, 0.9F * fade)
                            : colour(STRIDE_THIGH_ORANGE, 0.9F * fade);
  const ImU32 shin =
      block ? colour(BLOCK_SHIN_BLUE, 0.9F * fade) : colour(AMBER, 0.9F * fade);
  const ImU32 foot = block ? colour(BLOCK_FOOT_BLUE, 0.9F * fade)
                           : colour(STRIDE_FOOT_YELLOW, 0.9F * fade);
  for (const LegPose::Leg& leg : pose.legs)
  {
    paint.line(at(leg.hip), at(leg.knee), thigh, THIGH_THICKNESS);
    paint.line(at(leg.knee), at(leg.ankle), shin, SHIN_THICKNESS);
    drawBootedFoot(paint, body, leg, foot);
  }
  paint.line(at(pose.legs[0].hip), at(pose.legs[1].hip), thigh, LINE_HEAVIEST);
  if (labels != nullptr)
  {
    labels->push_back(
        {.at = at({.x = 0.0F,
                   .y = 0.0F,
                   .z = HALF * (pose.legs[0].hip.z + pose.legs[0].ankle.z)}),
         .text = block ? std::format(
                             "reaction block, leg {:.0f}% out, miss {:.2f} m, "
                             "{:.2f} s ago",
                             100.0F * legs.outShare, legs.error, ageSeconds)
                       : std::format(
                             "stride for a shot ({}), {:.2f} s ago",
                             legs.reacted ? legs.noBlock : "not yet reacted",
                             ageSeconds),
         .tint = thigh});
  }
}

void drawLabels(const Painter& paint, const std::vector<Label>& labels)
{
  for (const Label& text : labels)
  {
    ImVec2 at;
    if (!paint.at(text.at, at))
    {
      continue;
    }
    const ImVec2 extent = ImGui::CalcTextSize(text.text.c_str());
    paint.draw->AddRectFilled(
        ImVec2(at.x + LABEL_BACK_LEFT, at.y - (extent.y * HALF) - 1.0F),
        ImVec2(at.x + extent.x + LABEL_BACK_RIGHT,
               at.y + (extent.y * HALF) + 1.0F),
        colour(LABEL_BACK, LABEL_BACK_ALPHA), LABEL_ROUNDING);
    paint.draw->AddText(
        ImVec2(at.x + LABEL_TEXT_LEFT, at.y - (extent.y * HALF)), text.tint,
        text.text.c_str());
  }
}

void renderTable(const std::vector<Entry>& entries, ImFont* headingFont)
{
  if (!ImGui::BeginTable(
          "##entries", 3,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
              ((headingFont != nullptr) ? ImGuiTableFlags_BordersInnerV : 0) |
              ImGuiTableFlags_SizingStretchProp))
  {
    return;
  }
  ImGui::TableSetupColumn("Shape", ImGuiTableColumnFlags_WidthStretch,
                          SHAPE_COLUMN_WEIGHT);
  ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch,
                          VALUE_COLUMN_WEIGHT);
  ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthStretch,
                          FROM_COLUMN_WEIGHT);
  if (headingFont != nullptr)
  {
    ImGui::PushFont(headingFont, HEADING_FONT_SIZE);
  }
  ImGui::TableHeadersRow();
  if (headingFont != nullptr)
  {
    ImGui::PopFont();
  }
  for (const Entry& entry : entries)
  {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(entry.what.c_str());
    ImGui::PopTextWrapPos();
    ImGui::TableNextColumn();
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(entry.value.c_str());
    ImGui::PopTextWrapPos();
    ImGui::TableNextColumn();
    ImGui::PushTextWrapPos(0.0F);
    UI::textDisabled("{}", entry.source.c_str());
    if (!entry.wip.empty())
    {
      UI::textColoured(WIP_TEXT, "WIP: {}", entry.wip.c_str());
    }
    ImGui::PopTextWrapPos();
  }
  ImGui::EndTable();
}
}  // namespace TrueView
