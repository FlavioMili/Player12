// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/render/match_play_overlay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>

#include "gui/widgets/theme.h"

namespace
{
constexpr float LENGTH = MatchTuning::Pitch::LENGTH_METRES;
constexpr float WIDTH = MatchTuning::Pitch::WIDTH_METRES;

constexpr ImU32 MARKER_COLOR = IM_COL32(255, 222, 89, 255);
constexpr ImU32 MARKER_SHADOW = IM_COL32(10, 14, 22, 170);
constexpr ImU32 NEXT_COLOR = IM_COL32(255, 255, 255, 170);
constexpr ImU32 LABEL_BACK = IM_COL32(8, 12, 20, 185);
constexpr ImU32 LABEL_TEXT = IM_COL32(245, 247, 250, 255);
constexpr ImU32 RADAR_BACK = IM_COL32(10, 30, 18, 215);
constexpr ImU32 RADAR_LINE = IM_COL32(255, 255, 255, 80);
constexpr ImU32 BAR_BACK = IM_COL32(8, 12, 20, 200);
constexpr ImU32 POWER_FILL = IM_COL32(255, 205, 64, 255);
constexpr ImU32 OVERPOWER_FILL = IM_COL32(240, 96, 64, 255);
/** Ring radius around the active footballer's feet (metres). */
constexpr float RING_METRES = 1.1f;
constexpr int RING_POINTS = 24;
/** Label anchor above the head (metres). */
constexpr float HEAD_METRES = 2.3f;
/** Radar size: share of the view width, clamped (logical pixels). */
constexpr float RADAR_WIDTH_SHARE = 0.2f;
constexpr float RADAR_MIN_WIDTH = 130.0f;
constexpr float RADAR_MAX_WIDTH = 240.0f;
/** Share of the bar from which the shot gets hard to keep down. */
constexpr float OVERPOWER_FROM = MatchTuning::Control::SHOT_OVERPOWER_FROM;

Vector2F positionOf(const MatchRenderPlayer& player, float alpha)
{
  return lerpRenderPosition(player.previousPosition, player.currentPosition,
                            alpha);
}

bool project(const IMatchRenderer& renderer, Vector2F pitch, float height,
             ImVec2& out)
{
  return renderer.projectPitch(pitch, height, out.x, out.y);
}

/** A ring on the grass around `centre` in the view's projection. */
void drawRing(ImDrawList& drawList, const IMatchRenderer& renderer,
              Vector2F centre, float metres, ImU32 color, float thickness,
              bool dashed)
{
  std::array<ImVec2, RING_POINTS> points{};
  for (int index = 0; index < RING_POINTS; ++index)
  {
    const float angle = static_cast<float>(index) * 2.0f *
                        std::numbers::pi_v<float> /
                        static_cast<float>(RING_POINTS);
    const Vector2F around{centre.x + std::cos(angle) * metres / LENGTH,
                          centre.y + std::sin(angle) * metres / WIDTH};
    if (!project(renderer, around, 0.03f,
                 points[static_cast<std::size_t>(index)]))
      return;
  }
  if (!dashed)
  {
    drawList.AddPolyline(points.data(), RING_POINTS, MARKER_SHADOW,
                         ImDrawFlags_Closed, thickness + 2.0f);
    drawList.AddPolyline(points.data(), RING_POINTS, color, ImDrawFlags_Closed,
                         thickness);
    return;
  }
  for (int index = 0; index < RING_POINTS; index += 2)
    drawList.AddLine(
        points[static_cast<std::size_t>(index)],
        points[static_cast<std::size_t>((index + 1) % RING_POINTS)], color,
        thickness);
}

ImU32 conditionColor(float stamina)
{
  const Theme::Palette& palette = Theme::palette();
  ImVec4 color = palette.negative;
  if (stamina >= 0.75f)
    color = palette.positive;
  else if (stamina >= 0.55f)
    color = palette.warning;
  return Theme::toU32(color);
}

/** Name, condition and (while charging) the power bar above the head. */
void drawLabel(ImDrawList& drawList, const MatchRenderPlayer& player,
               ImVec2 anchor, const MatchPlayOverlayState& state, float scale,
               float minTop)
{
  const char* name = player.player ? player.player->getLastName().c_str() : "";
  ImFont* font = ImGui::GetFont();
  const float fontSize = ImGui::GetFontSize() * 0.9f;
  const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, name);
  const float barWidth = std::max(textSize.x, 52.0f * scale);
  const float barHeight = 4.0f * scale;
  const float powerHeight = 7.0f * scale;
  const float padding = 4.0f * scale;
  const float gap = 3.0f * scale;
  const float block = textSize.y + gap + barHeight +
                      (state.charging ? gap + powerHeight : 0.0f);
  // A small chevron points down at him; the block sits above it.
  const float chevron = 6.0f * scale;
  const ImVec2 tip{anchor.x, anchor.y - 2.0f * scale};
  float top = tip.y - chevron - gap - block - padding;
  float bottom = tip.y - chevron - gap;
  // Near the top of the view the label slides down to stay readable (it
  // then covers his head, so the chevron goes).
  if (top < minTop)
  {
    bottom += minTop - top;
    top = minTop;
  }
  else
  {
    drawList.AddTriangleFilled({tip.x - chevron, tip.y - chevron},
                               {tip.x + chevron, tip.y - chevron}, tip,
                               MARKER_COLOR);
  }
  const float left = anchor.x - barWidth * 0.5f;
  drawList.AddRectFilled({left - padding, top - padding * 0.5f},
                         {left + barWidth + padding, bottom}, LABEL_BACK,
                         4.0f * scale);
  drawList.AddText(font, fontSize, {anchor.x - textSize.x * 0.5f, top},
                   LABEL_TEXT, name);
  float y = top + textSize.y + gap;
  const float stamina = std::clamp(player.stamina, 0.0f, 1.0f);
  drawList.AddRectFilled({left, y}, {left + barWidth, y + barHeight}, BAR_BACK);
  drawList.AddRectFilled({left, y}, {left + barWidth * stamina, y + barHeight},
                         conditionColor(stamina));
  if (!state.charging) return;
  y += barHeight + gap;
  const float power = std::clamp(state.power, 0.0f, 1.0f);
  drawList.AddRectFilled({left, y}, {left + barWidth, y + powerHeight},
                         BAR_BACK);
  drawList.AddRectFilled({left, y}, {left + barWidth * power, y + powerHeight},
                         power > OVERPOWER_FROM ? OVERPOWER_FILL : POWER_FILL);
  // Past this mark the ball gets hard to keep down.
  const float mark = left + barWidth * OVERPOWER_FROM;
  drawList.AddLine({mark, y}, {mark, y + powerHeight}, LABEL_TEXT, scale);
}

/** Arrow on the view's edge pointing at an active footballer out of view. */
void drawOffscreenArrow(ImDrawList& drawList, ImVec2 target, ImVec2 viewMin,
                        ImVec2 viewMax, float scale)
{
  const ImVec2 centre{(viewMin.x + viewMax.x) * 0.5f,
                      (viewMin.y + viewMax.y) * 0.5f};
  float dx = target.x - centre.x;
  float dy = target.y - centre.y;
  const float length = std::hypot(dx, dy);
  if (length < 1.0f) return;
  dx /= length;
  dy /= length;
  const float inset = 22.0f * scale;
  const float halfWidth =
      std::max(1.0f, (viewMax.x - viewMin.x) * 0.5f - inset);
  const float halfHeight =
      std::max(1.0f, (viewMax.y - viewMin.y) * 0.5f - inset);
  // Where the ray from the centre leaves the inset rectangle.
  const float reach =
      std::min(std::abs(dx) > 1e-4f ? halfWidth / std::abs(dx) : 1e9f,
               std::abs(dy) > 1e-4f ? halfHeight / std::abs(dy) : 1e9f);
  const ImVec2 tip{centre.x + dx * reach, centre.y + dy * reach};
  const float size = 12.0f * scale;
  const ImVec2 back{tip.x - dx * size * 1.6f, tip.y - dy * size * 1.6f};
  const ImVec2 side{-dy * size, dx * size};
  drawList.AddTriangleFilled({back.x + side.x + dx, back.y + side.y + dy}, tip,
                             {back.x - side.x + dx, back.y - side.y + dy},
                             MARKER_SHADOW);
  drawList.AddTriangleFilled({back.x + side.x, back.y + side.y}, tip,
                             {back.x - side.x, back.y - side.y}, MARKER_COLOR);
}

/** The whole pitch at the bottom centre with every player and the ball. */
void drawRadar(ImDrawList& drawList, const MatchRenderSnapshot& snapshot,
               const MatchPlayOverlayState& state, ImVec2 viewMin,
               ImVec2 viewMax, float scale)
{
  const float viewWidth = viewMax.x - viewMin.x;
  const float width =
      std::clamp(viewWidth * RADAR_WIDTH_SHARE, RADAR_MIN_WIDTH * scale,
                 RADAR_MAX_WIDTH * scale);
  const float height = width * WIDTH / LENGTH;
  const float margin = 12.0f * scale;
  if (width + 2.0f * margin > viewWidth ||
      height + 2.0f * margin > viewMax.y - viewMin.y)
    return;
  // Bottom centre, clear of the event ticker (left) and the HUD (top).
  const ImVec2 origin{(viewMin.x + viewMax.x - width) * 0.5f,
                      viewMax.y - margin - height};
  const auto at = [&](Vector2F pitch)
  { return ImVec2{origin.x + pitch.x * width, origin.y + pitch.y * height}; };
  drawList.AddRectFilled(origin, {origin.x + width, origin.y + height},
                         RADAR_BACK, 4.0f * scale);
  drawList.AddRect(origin, {origin.x + width, origin.y + height}, RADAR_LINE,
                   4.0f * scale);
  drawList.AddLine({origin.x + width * 0.5f, origin.y},
                   {origin.x + width * 0.5f, origin.y + height}, RADAR_LINE);
  drawList.AddCircle({origin.x + width * 0.5f, origin.y + height * 0.5f},
                     height * 9.15f / WIDTH, RADAR_LINE, 16);
  // Penalty areas: 16.5 m deep, 40.3 m wide.
  const float boxDepth = 16.5f / LENGTH * width;
  const float boxHalf = 20.15f / WIDTH * height;
  const float middle = origin.y + height * 0.5f;
  drawList.AddRect({origin.x, middle - boxHalf},
                   {origin.x + boxDepth, middle + boxHalf}, RADAR_LINE);
  drawList.AddRect({origin.x + width - boxDepth, middle - boxHalf},
                   {origin.x + width, middle + boxHalf}, RADAR_LINE);

  const float alpha = snapshot.interpolationAlpha;
  const float dot = 3.0f * scale;
  for (const MatchRenderPlayer& player : snapshot.players)
  {
    if (!player.onPitch || !player.player) continue;
    const ImVec2 point = at(positionOf(player, alpha));
    const PlayerID id = player.player->getId();
    if (id == state.activePlayer) continue;
    drawList.AddCircleFilled(
        point, dot, player.isHomeTeam ? state.homeColor : state.awayColor);
    if (id == state.nextSwitch)
      drawList.AddCircle(point, dot + 2.0f * scale, NEXT_COLOR, 12, scale);
  }
  for (const MatchRenderPlayer& player : snapshot.players)
  {
    if (!player.onPitch || !player.player ||
        player.player->getId() != state.activePlayer)
      continue;
    const ImVec2 point = at(positionOf(player, alpha));
    drawList.AddCircleFilled(
        point, dot + 2.0f * scale,
        player.isHomeTeam ? state.homeColor : state.awayColor);
    drawList.AddCircle(point, dot + 2.5f * scale, MARKER_COLOR, 14,
                       1.5f * scale);
  }
  const ImVec2 ball = at(lerpRenderPosition(
      snapshot.ball.previousPosition, snapshot.ball.currentPosition, alpha));
  drawList.AddCircleFilled(ball, 2.5f * scale, MARKER_SHADOW);
  drawList.AddCircleFilled(ball, 1.8f * scale, IM_COL32(255, 255, 255, 255));
}
}  // namespace

void drawMatchPlayOverlay(ImDrawList& drawList, const IMatchRenderer& renderer,
                          const MatchRenderSnapshot& snapshot,
                          const MatchPlayOverlayState& state, ImVec2 viewMin,
                          ImVec2 viewMax)
{
  const float scale = std::max(Theme::scale(), 0.5f);
  const float alpha = snapshot.interpolationAlpha;
  drawList.PushClipRect(viewMin, viewMax, true);
  const MatchRenderPlayer* active = nullptr;
  for (const MatchRenderPlayer& player : snapshot.players)
  {
    if (!player.onPitch || !player.player) continue;
    const PlayerID id = player.player->getId();
    if (id == state.activePlayer) active = &player;
    if (id == state.nextSwitch)
      drawRing(drawList, renderer, positionOf(player, alpha),
               RING_METRES * 0.9f, NEXT_COLOR, 1.5f * scale, true);
  }
  if (active)
  {
    const Vector2F position = positionOf(*active, alpha);
    drawRing(drawList, renderer, position, RING_METRES, MARKER_COLOR,
             2.5f * scale, false);
    ImVec2 feet;
    ImVec2 head;
    const bool visible = project(renderer, position, 0.0f, feet);
    const float margin = 6.0f * scale;
    const bool inside = visible && feet.x >= viewMin.x + margin &&
                        feet.x <= viewMax.x - margin &&
                        feet.y >= viewMin.y + margin &&
                        feet.y <= viewMax.y - margin;
    if (inside)
    {
      if (!project(renderer, position, HEAD_METRES, head) ||
          std::abs(head.y - feet.y) < 8.0f * scale)
        // A top-down view: the label sits just above the token.
        head = {feet.x, feet.y - 16.0f * scale};
      drawLabel(drawList, *active, head, state, scale,
                viewMin.y + 4.0f * scale);
    }
    else if (visible)
    {
      drawOffscreenArrow(drawList, feet, viewMin, viewMax, scale);
    }
  }
  drawRadar(drawList, snapshot, state, viewMin, viewMax, scale);
  drawList.PopClipRect();
}
