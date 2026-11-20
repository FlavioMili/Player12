// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_true_view_shapes.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <initializer_list>
#include <numbers>

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

float radians(float degrees) { return degrees * PI / 180.0F; }

ImU32 colour(int r, int g, int b, float alpha)
{
  return IM_COL32(r, g, b,
                  static_cast<int>(std::clamp(alpha, 0.0F, 1.0F) * 255.0F));
}

/** `tint` with its alpha set. */
ImU32 withAlpha(ImU32 tint, float alpha)
{
  return (tint & ~IM_COL32_A_MASK) |
         (static_cast<ImU32>(std::clamp(alpha, 0.0F, 1.0F) * 255.0F)
          << IM_COL32_A_SHIFT);
}

/** 0-1 on the contact view's dark-to-hot scale. */
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
  const ImVec4& a = STOPS[static_cast<std::size_t>(index)];
  const ImVec4& b = STOPS[static_cast<std::size_t>(index) + 1];
  return ImGui::GetColorU32(ImVec4(a.x + ((b.x - a.x) * t),
                                   a.y + ((b.y - a.y) * t),
                                   a.z + ((b.z - a.z) * t), alpha));
}

/** A share of power, 1 green down to 0 red. */
ImU32 powerColour(float share, float alpha)
{
  const float s = std::clamp(share, 0.0F, 1.0F);
  return ImGui::GetColorU32(
      ImVec4(0.85F - (0.55F * s), 0.25F + (0.55F * s), 0.25F, alpha));
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
    const float inner = distances[ringIndex] * reach;
    const float outer = distances[ringIndex + 1] * reach;
    const float middle = 0.5F * (inner + outer);
    for (int section = 0; section < SECTIONS; ++section)
    {
      const float from = -180.0F + (30.0F * static_cast<float>(section));
      const float centre = from + 15.0F;
      const ImU32 tint =
          blocked(centre, middle)
              ? blockedTint
              : heat(map.at(centre, middle / reach), 0.6F * alpha);
      paint.sector(inner, outer, z, from, from + 30.0F, tint);
    }
    paint.ring(outer, z, colour(255, 255, 255, 0.35F * alpha), 1.0F);
  }
  for (int section = 0; section < SECTIONS; ++section)
  {
    const float edge = -180.0F + (30.0F * static_cast<float>(section));
    paint.line(paint.polar(edge, 0.0F, z), paint.polar(edge, reach, z),
               colour(255, 255, 255, 0.25F * alpha), 1.0F);
  }
}
}  // namespace

// ---- Painter ---------------------------------------------------------------

Vec3 Painter::polar(float degrees, float radius, float z) const
{
  return polarFrom(centre, degrees, radius, z);
}

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
  arc(radius, z, -180.0F, 180.0F, tint, thickness);
}

void Painter::dashedRing(float radius, float z, ImU32 tint,
                         float thickness) const
{
  for (int dash = 0; dash < 36; ++dash)
  {
    const float from = -180.0F + (10.0F * static_cast<float>(dash));
    arc(radius, z, from, from + 6.0F, tint, thickness);
  }
}

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
  for (float degrees = from; degrees <= to + 0.01F; degrees += 30.0F)
  {
    line(polar(degrees, radius, bottom), polar(degrees, radius, top), tint,
         thickness * 0.7F);
  }
  if (to - from < 359.0F)
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
  for (int step = 0; step < 24; ++step)
  {
    const float a = -180.0F + (15.0F * static_cast<float>(step));
    fill({polar(a, radius, bottom), polar(a + 15.0F, radius, bottom),
          polar(a + 15.0F, radius, top), polar(a, radius, top)},
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

void drawBody(const Painter& base, const Body& body, const Tuning& tuning,
              const Layers& layers, Technique technique, Detail detail,
              std::vector<Label>* labels, std::vector<Entry>* entries)
{
  using A = MatchTuning::Aerial;
  using B = MatchTuning::Ball;
  using C = MatchTuning::Collision;
  Painter paint = base;
  paint.centre = body.at;
  paint.facing = body.facing;
  const bool full = detail == Detail::FULL;
  const float height = body.height;
  const float feetHeight = body.feetHeight;
  const float radius = 0.5F * MatchTuning::Body::DIAMETER_METRES;
  const BodyLines lines = bodyLinesOfHeight(height);
  // In the air his feet, thigh and chest play nothing (the touch sweep
  // skips airborne players); down, nothing of his plays it.
  const bool groundPartsOn = !body.airborne && !body.lying;
  const bool thighOn = groundPartsOn && !body.thighOff;
  const float z0 = body.at.z;

  const auto label = [&](Vec3 at, std::string text, ImU32 tint)
  {
    if (labels && layers.labels)
    {
      labels->push_back({.at = at, .text = std::move(text), .tint = tint});
    }
  };
  const auto add = [&](std::string what, std::string value, std::string source,
                       std::string wip = {})
  {
    if (entries)
    {
      entries->push_back({.what = std::move(what),
                          .value = std::move(value),
                          .source = std::move(source),
                          .wip = std::move(wip)});
    }
  };
  const auto picked = [&](std::initializer_list<Technique> owners)
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
    return 0.25F;
  };
  const ImU32 offTint = colour(110, 110, 120, 0.35F);
  const float feetReach = tuning.feetReach;
  const float comfortGap = tuning.comfortGap;
  const float deadAngle = tuning.deadAngle;

  // ---- The body cylinder (every body) ---------------------------------------
  if (layers.body || !full)
  {
    const ImU32 edge = colour(150, 175, 210, 0.9F);
    // In a match his shirt colour tells the sides apart.
    const ImU32 side = body.kit != 0
                           ? withAlpha(body.kit, full ? 0.3F : 0.45F)
                           : colour(110, 140, 190, full ? 0.18F : 0.3F);
    if (!body.lying)
    {
      if (body.kit != 0)
      {
        paint.sector(0.0F, radius, z0 + 0.004F, -180.0F, 180.0F,
                     withAlpha(body.kit, 0.95F));
      }
      paint.cylinderSides(radius, z0 + feetHeight, z0 + feetHeight + height,
                          side);
      paint.cylinder(radius, z0 + feetHeight, z0 + feetHeight + height, -180.0F,
                     180.0F, edge, 1.5F);
      add("Body: an upright cylinder, across",
          std::format("{:.2f} m", MatchTuning::Body::DIAMETER_METRES),
          "Body::DIAMETER_METRES");
      add("Body: tall (his height)", std::format("{:.2f} m", height),
          "player height");
    }
    else
    {
      // Down (collision plan step 4): seen from above a stadium, his length
      // with rounded ends, 0.4 m high.
      Painter flat = paint;
      flat.facing = body.lyingAngle;
      const float half = std::max(0.0F, (0.5F * height) - radius);
      const auto outline = [&](float z, float r)
      {
        std::vector<Vec3> points;
        const Vec3 head = flat.polar(0.0F, half, z0);
        const Vec3 feet = flat.polar(180.0F, half, z0);
        for (int step = 0; step <= 18; ++step)
        {
          points.push_back(flat.polarFrom(
              head, 90.0F - (180.0F * static_cast<float>(step) / 18.0F), r,
              z0 + z));
        }
        for (int step = 0; step <= 18; ++step)
        {
          points.push_back(flat.polarFrom(
              feet, -90.0F - (180.0F * static_cast<float>(step) / 18.0F), r,
              z0 + z));
        }
        return points;
      };
      const float bodyTop = MatchTuning::Body::DIAMETER_METRES;
      const std::vector<Vec3> bottom = outline(0.0F, radius);
      const std::vector<Vec3> top = outline(bodyTop, radius);
      paint.fill(top, side);
      paint.polyline(bottom, edge, 1.5F, true);
      paint.polyline(top, edge, 1.5F, true);
      for (std::size_t index = 0; index < bottom.size(); index += 6)
      {
        paint.line(bottom[index], top[index], edge, 1.0F);
      }
      if (full)
      {
        // A ball whose centre comes this close below this height hits him.
        const float ballReach = radius + MatchTuning::Units::BALL_RADIUS_METRES;
        const ImU32 hit = colour(240, 200, 90, 0.8F);
        paint.polyline(outline(C::FALLEN_BALL_BELOW_METRES, ballReach), hit,
                       1.0F, true);
        paint.polyline(outline(0.0F, ballReach), hit, 1.0F, true);
        label(flat.polar(0.0F, half + ballReach,
                         z0 + C::FALLEN_BALL_BELOW_METRES),
              std::format(
                  "ball's centre within {:.2f} m, below {:.2f} m: off his body",
                  ballReach, C::FALLEN_BALL_BELOW_METRES),
              hit);
        label(
            flat.polar(180.0F, half + radius, z0 + bodyTop),
            std::format("lying {:.2f} m long, {:.2f} m high", height, bodyTop),
            colour(150, 175, 210, 1.0F));
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
        add("Personal space", "none",
            "a fallen player is hurdled, not run into");
      }
    }
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

  // ---- Outline: his feet reach and facing only
  // ----------------------------------
  if (!full)
  {
    if (groundPartsOn)
    {
      if (body.kit != 0)
      {
        paint.ring(feetReach, z0 + 0.006F, withAlpha(body.kit, 0.9F), 2.5F);
      }
      else
      {
        paint.ring(feetReach, z0 + 0.006F, colour(240, 240, 240, 0.45F), 1.2F);
      }
      paint.line(paint.polar(0.0F, radius, z0 + 0.01F),
                 paint.polar(0.0F, feetReach, z0 + 0.01F),
                 colour(255, 255, 255, 0.7F), 1.5F);
    }
    return;
  }

  // ---- Personal space
  // ----------------------------------------------------------
  if (layers.personalSpace && !body.lying)
  {
    const float teamMate = C::OVERTAKE_TEAM_MATE_CLEARANCE_METRES;
    const float opponent = C::OVERTAKE_OPPONENT_CLEARANCE_METRES;
    const float pressure = tuning.pressureRadius;
    paint.dashedRing(teamMate, z0 + 0.002F, colour(120, 210, 140, 0.8F));
    paint.dashedRing(opponent, z0 + 0.002F, colour(240, 160, 80, 0.8F));
    paint.dashedRing(pressure, z0 + 0.002F, colour(230, 90, 90, 0.7F));
    label(paint.polar(-135.0F, teamMate, z0),
          std::format("team-mate passes {:.1f} m", teamMate),
          colour(120, 210, 140, 1.0F));
    label(paint.polar(-150.0F, opponent, z0),
          std::format("opponent passes {:.1f} m", opponent),
          colour(240, 160, 80, 1.0F));
    label(paint.polar(-165.0F, pressure, z0),
          std::format("pressure on his touch {:.1f} m", pressure),
          colour(230, 90, 90, 1.0F));
    add("Team-mate clearance (overtaking, way round)",
        std::format("{:.2f} m", teamMate),
        "Collision::OVERTAKE_TEAM_MATE_CLEARANCE_METRES");
    add("Opponent clearance (overtaking, way round)",
        std::format("{:.2f} m", opponent),
        "Collision::OVERTAKE_OPPONENT_CLEARANCE_METRES");
    add("Pressure on a touch (0 at the edge, 1 on top)",
        std::format("{:.2f} m", pressure), "touch knob Pressure radius");
  }

  // ---- Technique ranges on the floor
  // -------------------------------------------
  if (layers.technique && groundPartsOn)
  {
    if (technique == Technique::DRIBBLE || technique == Technique::ALL)
    {
      const float alpha = technique == Technique::DRIBBLE ? 1.0F : 0.45F;
      const float meet = dribbleMeetGapOf(comfortGap, body.dribbling);
      const float collect = MatchTuning::Dribble::KICK_REACH_METRES;
      paint.ring(meet, z0 + 0.004F, colour(80, 210, 230, alpha), 2.0F);
      paint.dashedRing(collect, z0 + 0.004F, colour(80, 210, 230, alpha), 1.5F);
      label(paint.polar(30.0F, meet, z0),
            std::format("dribble meet {:.2f} m", meet),
            colour(80, 210, 230, alpha));
      label(paint.polar(40.0F, collect, z0),
            std::format("collects within {:.2f} m", collect),
            colour(80, 210, 230, alpha));
      add("Dribble touch: where he meets it", std::format("{:.2f} m", meet),
          "dribbleMeetGapOf: comfort gap (Dribbling 99) to kick reach (1)");
      add("Dribble: collects it within", std::format("{:.2f} m", collect),
          "Dribble::KICK_REACH_METRES");
    }
    if (technique == Technique::TACKLE || technique == Technique::ALL)
    {
      const float alpha = technique == Technique::TACKLE ? 1.0F : 0.4F;
      const float standing = standingTackleReachOf(body.defending);
      const float slide = MatchTuning::Defending::SLIDE_TACKLE_DISTANCE_METRES;
      for (int section = 0; section < SECTIONS; ++section)
      {
        const float from = -180.0F + (30.0F * static_cast<float>(section));
        paint.sector(
            standing, slide, z0 + 0.001F, from, from + 30.0F,
            colour(200, 120, 220,
                   (0.12F * alpha) + (section % 2 == 0 ? 0.05F : 0.0F)));
      }
      paint.ring(standing, z0 + 0.003F, colour(200, 120, 220, alpha), 1.5F);
      paint.ring(slide, z0 + 0.003F, colour(200, 120, 220, alpha), 1.5F);
      label(paint.polar(150.0F, slide, z0),
            std::format("slide tackle roll {:.1f} m [WIP]", slide),
            colour(200, 120, 220, alpha));
      label(paint.polar(160.0F, standing, z0),
            std::format("standing range {:.2f} m [WIP]", standing),
            colour(200, 120, 220, alpha));
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
    if (technique == Technique::SHOT_BLOCK || technique == Technique::ALL)
    {
      // Shot block plan §3c: his torso, solid from the hip to the top of
      // his head; below it, his legs only where a stride rolled for a shot
      // has them (drawn as ghosts while one is recent).
      const float alpha = technique == Technique::SHOT_BLOCK ? 1.0F : 0.3F;
      const float torso = radius + MatchTuning::Units::BALL_RADIUS_METRES;
      paint.cylinder(torso, z0 + feetHeight + lines.chest,
                     z0 + feetHeight + height, -180.0F, 180.0F,
                     colour(230, 110, 170, alpha * 0.8F), 1.0F);
      label(
          paint.polar(-60.0F, torso, z0 + feetHeight + lines.chest),
          std::format("shot block: torso {:.2f} m, hip {:.2f} m to head [WIP]",
                      torso, lines.chest),
          colour(230, 110, 170, alpha));
      add("Shot block: torso", std::format("{:.2f} m, hip to head", torso),
          "Body::RADIUS_METRES + ball radius; hip = chest line",
          "shot block plan: where the ball goes off him is still the old "
          "bounce");
      add("Shot block: legs", "where his stride has them",
          "legPoseOf (MatchTuning::Stride), rolled once per shot path",
          "natural positions only; no deliberate leg until the brain step");
    }
  }

  // ---- Feet
  // --------------------------------------------------------------------
  if (layers.feet && !body.lying)
  {
    const float alpha = picked({Technique::FIRST_TOUCH, Technique::FOOT_STRIKE,
                                Technique::DRIBBLE, Technique::TACKLE});
    const float z = z0 + 0.006F;
    if (groundPartsOn)
    {
      stretchRings(
          paint, tuning.feetStretch, feetReach, z, alpha,
          [&](float centre, float middle)
          { return middle > comfortGap && std::abs(centre) > deadAngle; },
          colour(70, 70, 76, 0.75F * alpha));
      // The dead zone's edge, exactly.
      const ImU32 deadEdge = colour(235, 70, 70, alpha);
      for (const float side : {-1.0F, 1.0F})
      {
        paint.line(paint.polar(side * deadAngle, comfortGap, z),
                   paint.polar(side * deadAngle, feetReach, z), deadEdge, 2.0F);
        paint.arc(comfortGap, z, side * deadAngle, side * 180.0F, deadEdge,
                  2.0F);
      }
      paint.ring(comfortGap, z + 0.002F, colour(90, 230, 120, alpha), 2.5F);
      paint.ring(feetReach, z + 0.002F, colour(240, 240, 240, alpha), 2.5F);
      // The band his feet play: up to his feet line.
      paint.cylinder(feetReach, z0, z0 + lines.feet, -180.0F, 180.0F,
                     colour(240, 240, 240, 0.25F * alpha), 1.0F);
      label(paint.polar(-20.0F, feetReach, z0),
            std::format("feet reach {:.2f} m", feetReach),
            colour(240, 240, 240, alpha));
      label(paint.polar(-40.0F, comfortGap, z0),
            std::format("comfort gap {:.2f} m (best technique)", comfortGap),
            colour(90, 230, 120, alpha));
      label(paint.polar(180.0F, 0.5F * (comfortGap + feetReach), z0),
            std::format("dead zone: beyond {:.0f} deg", deadAngle), deadEdge);
    }
    else
    {
      paint.ring(feetReach, z, offTint, 1.5F);
      label(paint.polar(-20.0F, feetReach, z0), "feet: off in the air",
            offTint);
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

    // His feet: the weaker foot's side of his facing line.
    if (layers.facing && groundPartsOn)
    {
      const float weakSide = body.rightFooted ? -1.0F : 1.0F;
      paint.arc(feetReach + 0.05F, z, 0.0F, weakSide * 180.0F,
                colour(90, 140, 255, alpha), 3.0F);
      paint.arc(feetReach + 0.05F, z, 0.0F, -weakSide * 180.0F,
                colour(255, 200, 90, alpha), 3.0F);
      label(paint.polar(weakSide * 90.0F, feetReach + 0.1F, z0),
            "weaker foot side", colour(90, 140, 255, 1.0F));
      label(paint.polar(-weakSide * 90.0F, feetReach + 0.1F, z0),
            body.rightFooted ? "right foot side" : "left foot side",
            colour(255, 200, 90, 1.0F));
      add("Weaker foot: a ball on this side of his facing line",
          body.rightFooted ? "his left" : "his right",
          "engine: the side of his facing line (+ his right, clockwise in the "
          "engine's frame)");
    }
  }

  // ---- Power by angle
  // ----------------------------------------------------------
  if (layers.technique && groundPartsOn &&
      (technique == Technique::FOOT_STRIKE || technique == Technique::HEADER))
  {
    const bool header = technique == Technique::HEADER;
    const ContactProfile& profile = header ? tuning.header : tuning.strike;
    const float inner =
        header ? A::HEAD_REACH_METRES + 0.05F : feetReach + 0.12F;
    const float outer = inner + 0.15F;
    const float z =
        header ? z0 + headBandOf(height, feetHeight).forehead : z0 + 0.008F;
    // Across his body (inward) is his weaker foot's side for the strong foot.
    const float inwardSide = body.rightFooted ? -1.0F : 1.0F;
    for (int section = 0; section < SECTIONS; ++section)
    {
      const float from = -180.0F + (30.0F * static_cast<float>(section));
      const float centre = from + 15.0F;
      const bool inward = centre * inwardSide > 0.0F;
      const float share = powerByAngleShare(
          std::abs(centre),
          inward ? profile.powerInwardSquare : profile.powerOutwardSquare,
          profile.powerBehind);
      paint.sector(inner, outer, z, from, from + 30.0F,
                   powerColour(share, 0.75F));
    }
    label(paint.polar(0.0F, outer, z), "power 100%", powerColour(1.0F, 1.0F));
    label(paint.polar(inwardSide * 90.0F, outer, z),
          std::format("inward {:.0f}%", profile.powerInwardSquare * 100.0F),
          powerColour(profile.powerInwardSquare, 1.0F));
    label(paint.polar(-inwardSide * 90.0F, outer, z),
          std::format("outward {:.0f}%", profile.powerOutwardSquare * 100.0F),
          powerColour(profile.powerOutwardSquare, 1.0F));
    label(paint.polar(180.0F, outer, z),
          std::format("behind {:.0f}%", profile.powerBehind * 100.0F),
          powerColour(profile.powerBehind, 1.0F));
    const std::string kind =
        header ? "Header"
               : std::string(STRIKE_KIND_NAMES[static_cast<std::size_t>(
                     tuning.strikeKind)]);
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

  // ---- His lines
  // -----------------------------------------------------------------
  if (layers.lines && !body.lying)
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
      paint.ring(radius + 0.01F, z0 + feetHeight + mark.z,
                 colour(255, 255, 255, 0.9F), 2.0F);
      label(paint.polar(90.0F, radius + 0.05F, z0 + feetHeight + mark.z),
            std::format("{} {:.2f} m ({:.2f} x h)", mark.name, mark.z,
                        mark.z / height),
            colour(230, 230, 230, 1.0F));
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

  // ---- Thigh and chest
  // ---------------------------------------------------------
  if (layers.thighChest && !body.lying)
  {
    struct Part
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
    const std::array<Part, 2> parts{{
        {.name = "thigh",
         .bottom = lines.feet,
         .top = lines.chest,
         .reach = B::THIGH_REACH_METRES,
         .on = thighOn,
         .owner = Technique::THIGH,
         .tint = colour(240, 150, 70, 1.0F),
         .sideDegrees = B::THIGH_MAX_SIDE_DEGREES,
         .upFrom = B::THIGH_MIN_UP_DEGREES,
         .upTo = B::THIGH_MAX_UP_DEGREES},
        {.name = "chest",
         .bottom = lines.chest,
         .top = lines.head,
         .reach = B::CHEST_REACH_METRES,
         .on = groundPartsOn,
         .owner = Technique::CHEST,
         .tint = colour(240, 220, 90, 1.0F),
         .sideDegrees = B::CHEST_MAX_SIDE_DEGREES,
         .upFrom = 0.0F,
         .upTo = B::CHEST_MAX_UP_DEGREES},
    }};
    for (const Part& part : parts)
    {
      const float alpha = picked({part.owner});
      const ImU32 tint = part.on ? withAlpha(part.tint, alpha) : offTint;
      const float bottom = z0 + feetHeight + part.bottom;
      const float top = z0 + feetHeight + part.top;
      // In front of him only: a half cylinder from his centre.
      paint.cylinder(part.reach, bottom, top, -90.0F, 90.0F, tint, 1.5F);
      if (part.on)
      {
        paint.sector(0.0F, part.reach, bottom, -90.0F, 90.0F,
                     withAlpha(part.tint, 0.2F * alpha));
      }
      label(paint.polar(0.0F, part.reach, 0.5F * (bottom + top)),
            part.on
                ? std::format("{} {:.2f} m, in front", part.name, part.reach)
                : std::format("{}: off", part.name),
            tint);
      if (layers.sendCones && part.on)
      {
        // Where it can send the ball: up to sideDegrees either side, from
        // upFrom to upTo above flat.
        const Vec3 from = paint.polar(0.0F, part.reach, 0.5F * (bottom + top));
        const auto direction = [&](float side, float up)
        {
          return paint.polarFrom(
              from, side, std::cos(radians(up)) * CONE_METRES,
              from.z + (std::sin(radians(up)) * CONE_METRES));
        };
        std::vector<Vec3> edge;
        for (int step = 0; step <= 8; ++step)
        {
          edge.push_back(direction(
              -part.sideDegrees +
                  (part.sideDegrees * static_cast<float>(step) / 4.0F),
              part.upFrom));
        }
        for (int step = 0; step <= 8; ++step)
        {
          edge.push_back(direction(
              part.sideDegrees -
                  (part.sideDegrees * static_cast<float>(step) / 4.0F),
              part.upTo));
        }
        paint.polyline(edge, tint, 1.5F, true);
        for (const float side : {-part.sideDegrees, part.sideDegrees})
        {
          for (const float up : {part.upFrom, part.upTo})
          {
            paint.line(from, direction(side, up), tint, 1.0F);
          }
        }
      }
    }
    add("Thigh: reach, in front only",
        std::format("{:.2f} m", B::THIGH_REACH_METRES),
        "Ball::THIGH_REACH_METRES",
        thighOn ? ""
                : (groundPartsOn ? "off: moving faster than its limit"
                                 : "off now"));
    add("Thigh: only nearly stopped",
        std::format("<= {:.1f} m/s", B::THIGH_MAX_SPEED),
        "Ball::THIGH_MAX_SPEED", "placeholder (chest and thigh plan step 4)");
    add("Chest: reach, in front only",
        std::format("{:.2f} m", B::CHEST_REACH_METRES),
        "Ball::CHEST_REACH_METRES", groundPartsOn ? "" : "off now");
    if (layers.sendCones)
    {
      add("Chest: sends it",
          std::format("+/-{:.0f} deg, 0-{:.0f} deg up",
                      B::CHEST_MAX_SIDE_DEGREES, B::CHEST_MAX_UP_DEGREES),
          "Ball::CHEST_MAX_SIDE_DEGREES, CHEST_MAX_UP_DEGREES",
          "placeholders (step 3)");
      add("Thigh: sends it",
          std::format("+/-{:.0f} deg, {:.0f}-{:.0f} deg up",
                      B::THIGH_MAX_SIDE_DEGREES, B::THIGH_MIN_UP_DEGREES,
                      B::THIGH_MAX_UP_DEGREES),
          "Ball::THIGH_MAX_SIDE_DEGREES, THIGH_MIN/MAX_UP_DEGREES",
          "placeholders (step 4)");
    }
  }

  // ---- Head
  // --------------------------------------------------------------------------
  if (layers.head && !body.lying)
  {
    const float alpha = picked({Technique::HEADER});
    const HeadBand band = headBandOf(height, feetHeight);
    const float forehead = z0 + band.forehead;
    const float headReach = A::HEAD_REACH_METRES;
    const float headComfort = tuning.header.comfortGap;
    // Beyond his comfort gap and 60 degrees round, only the top of his
    // head reaches it (a ball at or above it).
    stretchRings(
        paint, tuning.header.stretchMap, headReach, forehead, alpha,
        [&](float centre, float middle)
        {
          return middle > headComfort &&
                 std::abs(centre) > A::HEAD_DEAD_ZONE_DEGREES;
        },
        colour(150, 110, 220, 0.55F * alpha));
    paint.ring(headComfort, forehead + 0.002F, colour(90, 230, 120, alpha),
               2.0F);
    paint.cylinder(headReach, z0 + band.lowest, z0 + band.top, -180.0F, 180.0F,
                   colour(240, 240, 240, 0.7F * alpha), 1.2F);
    for (const float side : {-1.0F, 1.0F})
    {
      paint.line(
          paint.polar(side * A::HEAD_DEAD_ZONE_DEGREES, headComfort, forehead),
          paint.polar(side * A::HEAD_DEAD_ZONE_DEGREES, headReach, forehead),
          colour(150, 110, 220, alpha), 2.0F);
    }
    label(paint.polar(-30.0F, headReach, z0 + band.top),
          std::format("head reach {:.2f} m", headReach),
          colour(240, 240, 240, alpha));
    label(paint.polar(-50.0F, headReach, forehead),
          std::format("forehead {:.2f} m", band.forehead),
          colour(240, 240, 240, alpha));
    label(paint.polar(-70.0F, headReach, z0 + band.lowest),
          std::format("lowest (duck) {:.2f} m", band.lowest),
          colour(240, 240, 240, alpha));
    label(paint.polar(150.0F, headReach, forehead), "top of head only",
          colour(150, 110, 220, 1.0F));
    add("Head: reach", std::format("{:.2f} m", headReach),
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

    // His jump's top: Jumping reach (running), a share of it standing.
    const ImU32 ceiling = colour(120, 200, 255, alpha);
    paint.dashedRing(headReach, z0 + body.jumpingReach, ceiling, 1.5F);
    paint.dashedRing(headReach * 0.75F, z0 + height + body.standingRise,
                     colour(120, 200, 255, 0.6F * alpha), 1.0F);
    label(
        paint.polar(30.0F, headReach, z0 + body.jumpingReach),
        std::format("Jumping reach {:.2f} m (running jump)", body.jumpingReach),
        ceiling);
    label(
        paint.polar(60.0F, headReach * 0.75F, z0 + height + body.standingRise),
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

  // ---- Facing
  // ------------------------------------------------------------------------
  if (layers.facing && !body.lying)
  {
    const ImU32 tint = colour(255, 255, 255, 0.95F);
    const float length = std::max(feetReach, 1.0F);
    const Vec3 tip = paint.polar(0.0F, length + 0.45F, z0 + 0.01F);
    paint.line(paint.polar(0.0F, radius, z0 + 0.01F), tip, tint, 2.5F);
    paint.line(tip, paint.polar(-8.0F, length + 0.3F, z0 + 0.01F), tint, 2.5F);
    paint.line(tip, paint.polar(8.0F, length + 0.3F, z0 + 0.01F), tint, 2.5F);
    label(tip, "facing", tint);
  }
}

void drawLegGhost(const Painter& paint, const Body& body, const ShotLegs& legs,
                  float fade, float ageSeconds, std::vector<Label>* labels)
{
  // The pose's offsets are in the engine's metric frame, as a match's
  // painter is; from his centre, lifted with him in a jump.
  using S = MatchTuning::Stride;
  const float scale = body.height / S::REFERENCE_HEIGHT_METRES;
  const auto at = [&](Point3F offset)
  {
    return Vec3{.x = body.at.x + offset.x,
                .y = body.at.y + offset.y,
                .z = body.at.z + body.feetHeight + offset.z};
  };
  // A stride in orange; a leg put in the way in cyan.
  const LegPose& pose = legs.pose;
  const bool block = legs.blocking;
  const ImU32 thigh = block ? colour(60, 200, 255, 0.9F * fade)
                            : colour(255, 160, 60, 0.9F * fade);
  const ImU32 shin = block ? colour(110, 220, 255, 0.9F * fade)
                           : colour(255, 200, 90, 0.9F * fade);
  const ImU32 foot = block ? colour(170, 240, 255, 0.9F * fade)
                           : colour(255, 230, 140, 0.9F * fade);
  for (const LegPose::Leg& leg : pose.legs)
  {
    const Vector2F forward = leg.footFacing;
    const Vector2F side{.x = -forward.y, .y = forward.x};
    paint.line(at(leg.hip), at(leg.knee), thigh, 6.0F);
    paint.line(at(leg.knee), at(leg.ankle), shin, 4.0F);
    // The booted foot: a box from the heel behind the ankle, sole at its rise.
    const float heel = -S::HEEL_BEHIND_ANKLE * scale;
    const float toe = (S::FOOT_LENGTH - S::HEEL_BEHIND_ANKLE) * scale;
    const float halfWidth = 0.5F * S::FOOT_WIDTH * scale;
    const float sole = leg.ankle.z - (S::FOOT_HEIGHT * scale);
    std::array<Vec3, 8> corners{};
    std::size_t corner = 0;
    for (const float z : {sole, leg.ankle.z})
    {
      for (const float along : {heel, toe})
      {
        for (const float across : {-halfWidth, halfWidth})
        {
          corners[corner++] =
              at({.x = leg.ankle.x + (forward.x * along) + (side.x * across),
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
          paint.line(corners[a], corners[a | bit], foot, 1.5F);
        }
      }
    }
  }
  paint.line(at(pose.legs[0].hip), at(pose.legs[1].hip), thigh, 3.0F);
  if (labels != nullptr)
  {
    labels->push_back(
        {.at = at({.x = 0.0F,
                   .y = 0.0F,
                   .z = 0.5F * (pose.legs[0].hip.z + pose.legs[0].ankle.z)}),
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
        ImVec2(at.x + 3.0F, at.y - (extent.y * 0.5F) - 1.0F),
        ImVec2(at.x + extent.x + 9.0F, at.y + (extent.y * 0.5F) + 1.0F),
        colour(10, 12, 16, 0.7F), 3.0F);
    paint.draw->AddText(ImVec2(at.x + 6.0F, at.y - (extent.y * 0.5F)),
                        text.tint, text.text.c_str());
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
  ImGui::TableSetupColumn("Shape", ImGuiTableColumnFlags_WidthStretch, 1.3F);
  ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.8F);
  ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthStretch, 1.4F);
  if (headingFont != nullptr)
  {
    ImGui::PushFont(headingFont, 14);
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
    ImGui::TextDisabled("%s", entry.source.c_str());
    if (!entry.wip.empty())
    {
      ImGui::TextColored(ImVec4(0.95F, 0.75F, 0.25F, 1.0F), "WIP: %s",
                         entry.wip.c_str());
    }
    ImGui::PopTextWrapPos();
  }
  ImGui::EndTable();
}
}  // namespace TrueView
