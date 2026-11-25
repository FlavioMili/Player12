// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

// The true view's shapes (Notes and plans/match_engine_true_view_plan.md):
// one player as the Binturong body models him, drawn through any
// projection. The true view scene draws one player on a grid; the match
// true view window draws every player of a match, two of them in full.

#include <imgui.h>

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "gui/render/match_render_math.h"
#include "model/match_engine.h"
#include "model/match_first_touch_knobs.h"
#include "model/match_strike_knobs.h"

namespace TrueView
{
using RenderMath::Vec3;

/** The technique whose range is picked out (ALL: every one, faintly). */
enum class Technique : int
{
  ALL,
  FIRST_TOUCH,
  FOOT_STRIKE,
  DRIBBLE,
  HEADER,
  CHEST,
  THIGH,
  TACKLE,
  SHOT_BLOCK,
};
inline constexpr std::array<const char*, 9> TECHNIQUE_NAMES{
    "All (faint)", "First touch", "Foot strike", "Dribble touch", "Header",
    "Chest",       "Thigh",       "Tackle",      "Shot block"};

struct Layers
{
  bool body = true;
  bool lines = true;
  bool feet = true;
  bool thighChest = true;
  bool sendCones = false;
  bool head = true;
  bool technique = true;
  bool personalSpace = true;
  bool facing = true;
  bool labels = true;
};

/** One shape drawn, for a table: what, how big, where the number comes
 * from, and whether that part of the engine is still work in progress. */
struct Entry
{
  std::string what;
  std::string value;
  std::string source;
  std::string wip;
};

/** A label to draw last, over everything. */
struct Label
{
  Vec3 at;
  std::string text;
  ImU32 tint;
};

/**
 * Draws through a projection. Its space: x and y across the ground, z up, a
 * direction at compass angle a (radians) being (sin a, cos a); "his right"
 * is clockwise. The scene uses metres round the origin; a match uses the
 * engine's own metric frame, so its handedness is the engine's.
 */
struct Painter
{
  using Project = std::function<bool(Vec3, ImVec2&)>;

  ImDrawList* draw = nullptr;
  Project project;
  /** Whose shapes: his centre on the ground, and his facing (compass). */
  Vec3 centre{};
  float facing = 0.0F;

  bool at(Vec3 point, ImVec2& out) const { return project(point, out); }
  /** `degrees` round from his facing (+ to his right), `radius` out from
   * `from` (his centre unless given), at height `z`. */
  [[nodiscard]] Vec3 polar(float degrees, float radius, float z) const;
  [[nodiscard]] Vec3 polarFrom(Vec3 from, float degrees, float radius,
                               float z) const;
  void line(Vec3 a, Vec3 b, ImU32 tint, float thickness = 1.5F) const;
  void polyline(const std::vector<Vec3>& points, ImU32 tint, float thickness,
                bool closed) const;
  void fill(const std::vector<Vec3>& points, ImU32 tint) const;
  void arc(float radius, float z, float from, float to, ImU32 tint,
           float thickness = 1.5F) const;
  void ring(float radius, float z, ImU32 tint, float thickness = 1.5F) const;
  void dashedRing(float radius, float z, ImU32 tint,
                  float thickness = 1.5F) const;
  void sector(float inner, float outer, float z, float from, float to,
              ImU32 tint) const;
  void cylinder(float radius, float bottom, float top, float from, float to,
                ImU32 tint, float thickness = 1.2F) const;
  void cylinderSides(float radius, float bottom, float top, ImU32 tint) const;
};

/** The tuning the shapes read: the touch knobs, the stretch map and the
 * strike knobs (a match's own, or the sandbox's). */
struct Tuning
{
  float feetReach = 1.0F;
  float comfortGap = 0.3F;
  float deadAngle = 120.0F;
  float pressureRadius = 1.5F;
  StretchMap feetStretch;
  ContactProfile header;
  /** The strike whose power by angle is shown. */
  ContactProfile strike;
  StrikeKind strikeKind = StrikeKind::GROUND_PASS;

  static Tuning from(const FirstTouchKnobs& touch, const StrikeKnobs& strikes,
                     const StretchMap& stretch, StrikeKind strikeKind);
};

/** One player at one moment. */
struct Body
{
  /** In the painter's space, his centre on the ground. */
  Vec3 at{};
  /** Compass radians in the painter's space. */
  float facing = 0.0F;
  float height = 1.8F;
  float feetHeight = 0.0F;
  bool airborne = false;
  bool lying = false;
  /** Down: the way his body lies (compass radians). */
  float lyingAngle = 0.0F;
  /** Faster than his thigh allows (or on a running jump's run-up). */
  bool thighOff = false;
  bool rightFooted = true;
  /** Ratings, 0-1. */
  float dribbling = 0.5F;
  float defending = 0.5F;
  float physicality = 0.5F;
  /** The top of his head at the top of a running jump (m), and how far his
   * feet rise in a standing one. */
  float jumpingReach = 2.2F;
  float standingRise = 0.3F;
  /** False: an Aardvark body (only its cylinder is drawn). */
  bool binturong = true;
  /** A goalkeeper keeps the old trap and his hands: only his cylinder is
   * drawn (his shapes are a follow-up). */
  bool goalkeeper = false;
  /** His shirt colour (a match: tells the sides apart on the floor and his
   * cylinder); 0: none. */
  ImU32 kit = 0;
};

/** How much of him to draw: his cylinder and feet reach, or everything. */
enum class Detail
{
  OUTLINE,
  FULL,
};

/** Draws `body`; collects labels and table rows when given. */
void drawBody(const Painter& base, const Body& body, const Tuning& tuning,
              const Layers& layers, Technique technique, Detail detail,
              std::vector<Label>* labels, std::vector<Entry>* entries);

/** Draws his legs as a shot met them (`legs`: a stride rolled for it,
 * shot block plan §3c, or a leg put across its line, §3d) on `body` as a
 * ghost: thighs and shins as rods, the feet as boxes, faded by `fade`
 * (0-1); labels it with its `ageSeconds` when given labels. */
void drawLegGhost(const Painter& paint, const Body& body, const ShotLegs& legs,
                  float fade, float ageSeconds, std::vector<Label>* labels);

/** Draws labels over everything, each on a dark chip. */
void drawLabels(const Painter& paint, const std::vector<Label>& labels);

/** The entries as a table (Shape, Value, From, with WIP notes). */
void renderTable(const std::vector<Entry>& entries,
                 ImFont* headingFont = nullptr);
}  // namespace TrueView
