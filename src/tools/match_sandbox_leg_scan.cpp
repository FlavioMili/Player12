// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_leg_scan.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>

#include "model/match_game_xg.h"
#include "model/match_tuning.h"
#include "tools/match_sandbox_game_xg.h"

namespace
{
using RunResult = GameXgLegSweep::RunResult;
using RunView = GameXgLegSweep::RunView;

/** The table's kinds: how they read in the view. */
constexpr std::array<const char*, GameXgLegTable::KINDS> KIND_NAMES{
    "Opponent, standing",  "Opponent, sidestepping",  "Opponent, sprinting",
    "Team-mate, standing", "Team-mate, sidestepping", "Team-mate, sprinting"};
constexpr std::size_t MOVEMENTS = 3;

// Colours.
constexpr ImU32 GRASS = IM_COL32(44, 84, 48, 255);
constexpr ImU32 GRASS_LIGHT = IM_COL32(54, 100, 58, 255);
constexpr ImU32 PANEL = IM_COL32(24, 28, 34, 255);
constexpr ImU32 GRID_LINE = IM_COL32(255, 255, 255, 18);
constexpr ImU32 TEXT = IM_COL32(235, 238, 242, 255);
constexpr ImU32 TEXT_DIM = IM_COL32(170, 178, 190, 255);
constexpr ImU32 GOAL = IM_COL32(255, 255, 255, 230);
constexpr ImU32 AIM_LINE = IM_COL32(255, 255, 255, 120);
constexpr ImU32 SHOOTER = IM_COL32(255, 255, 255, 255);
constexpr ImU32 OPPONENT_BODY = IM_COL32(215, 70, 70, 230);
constexpr ImU32 TEAMMATE_BODY = IM_COL32(70, 130, 225, 230);
constexpr ImU32 GHOST = IM_COL32(255, 255, 255, 170);
constexpr ImU32 BALL = IM_COL32(255, 255, 255, 255);
constexpr ImU32 HIGHLIGHT = IM_COL32(255, 255, 255, 255);
/** How he moves (the table's thresholds): standing, sidestepping,
 * sprinting. */
constexpr ImU32 STANDING_PACE = IM_COL32(160, 175, 200, 255);
constexpr ImU32 SIDESTEP_PACE = IM_COL32(245, 185, 60, 255);
constexpr ImU32 SPRINT_PACE = IM_COL32(240, 80, 60, 255);
/** What came of a run. */
constexpr ImU32 LEG_COLOUR = IM_COL32(255, 95, 60, 255);
constexpr ImU32 PAST_LOW_COLOUR = IM_COL32(90, 220, 120, 255);
constexpr ImU32 TORSO_COLOUR = IM_COL32(185, 125, 245, 255);
constexpr ImU32 PAST_HIGH_COLOUR = IM_COL32(120, 190, 255, 255);
constexpr ImU32 OFF_GRID_COLOUR = IM_COL32(140, 140, 140, 255);
constexpr ImU32 SCATTER_COLOUR = IM_COL32(255, 255, 255, 255);
/** The older runs' alpha, faintest to latest but one. */
constexpr float TRAIL_ALPHA_MIN = 12.0F;
constexpr float TRAIL_ALPHA_MAX = 70.0F;

// Layout (shares of the area) and sizes (scaled pixels unless metres).
constexpr float OVERVIEW_SHARE = 0.56F;
constexpr float CLOSE_SHARE = 0.55F;
constexpr float GAP = 10.0F;
constexpr float HEADER_LINES = 2.0F;
constexpr float LEGEND_LINES = 3.0F;
constexpr float ROOM_METRES = 1.5F;
constexpr float MIN_LENGTH_METRES = 6.0F;
constexpr float MIN_ACROSS_METRES = 3.0F;
constexpr float EASE = 0.15F;
constexpr float CLOSE_HALF_METRES = 1.9F;
constexpr float ARROW_SECONDS = 0.4F;
constexpr float ARROW_HEAD = 7.0F;
constexpr float MIN_BODY_PIXELS = 3.5F;
constexpr float LINE = 1.5F;
constexpr float THICK = 2.5F;
constexpr float DASH = 6.0F;
constexpr float GRID_METRES = 5.0F;
constexpr float HALF = 0.5F;
constexpr float CHANCE_ALPHA_MIN = 25.0F;
constexpr float CHANCE_ALPHA_SPAN = 200.0F;
constexpr float SOLID_SHOTS = 20.0F;
constexpr int OPAQUE = 255;
constexpr float TWICE = 2.0F;
/** The shooter's dot against a body's, and a swatch's foot (lines). */
constexpr float SHOOTER_SIZE = 1.3F;
constexpr float SWATCH_FOOT = 1.5F;

/** An alpha (0 to 255), and a line's colour and thickness. */
struct Alpha
{
  float value = 0.0F;
};
struct Pen
{
  ImU32 colour = 0;
  float thickness = 1.0F;
};

[[nodiscard]] ImU32 withAlpha(ImU32 colour, Alpha alpha)
{
  const auto a = static_cast<ImU32>(std::clamp(alpha.value, 0.0F, 255.0F));
  return (colour & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

[[nodiscard]] ImU32 resultColour(RunResult result)
{
  switch (result)
  {
    case RunResult::LEG:
      return LEG_COLOUR;
    case RunResult::PAST_LOW:
      return PAST_LOW_COLOUR;
    case RunResult::TORSO:
      return TORSO_COLOUR;
    case RunResult::PAST_HIGH:
      return PAST_HIGH_COLOUR;
    case RunResult::SCATTER:
      return SCATTER_COLOUR;
    case RunResult::OFF_GRID:
      break;
  }
  return OFF_GRID_COLOUR;
}

[[nodiscard]] const char* resultName(RunResult result)
{
  switch (result)
  {
    case RunResult::LEG:
      return "LEG: a block, counted";
    case RunResult::PAST_LOW:
      return "past him below his hip: counted, no block";
    case RunResult::TORSO:
      return "met his torso or head: not leg-space, not counted";
    case RunResult::PAST_HIGH:
      return "past him at his hip or over: not counted";
    case RunResult::SCATTER:
      return "shooter's error measured";
    case RunResult::OFF_GRID:
      break;
  }
  return "wider than 1.5 m from him: not counted";
}

[[nodiscard]] float paceOf(const RunView& run)
{
  return std::hypot(run.paceAlong, run.paceAcross);
}

[[nodiscard]] ImU32 paceColour(float pace)
{
  using D = MatchTuning::Decision;
  if (pace >= D::BLOCK_SPRINT_SPEED)
  {
    return SPRINT_PACE;
  }
  return pace >= D::BLOCK_STANDING_SPEED ? SIDESTEP_PACE : STANDING_PACE;
}

[[nodiscard]] ImU32 bodyColour(const RunView& run)
{
  return run.kind >= MOVEMENTS ? TEAMMATE_BODY : OPPONENT_BODY;
}

/** A pitch frame on the screen: along up the screen, across (+ left) to
 * the left. */
struct Frame
{
  float centreX = 0.0F;
  float baseY = 0.0F;
  float alongFrom = 0.0F;
  float acrossAt = 0.0F;
  float ppm = 1.0F;
  [[nodiscard]] ImVec2 at(float along, float across) const
  {
    return {centreX - ((across - acrossAt) * ppm),
            baseY - ((along - alongFrom) * ppm)};
  }
};

void dashedLine(ImDrawList* draw, ImVec2 from, ImVec2 to, Pen pen, float dash)
{
  const float dx = to.x - from.x;
  const float dy = to.y - from.y;
  const float length = std::hypot(dx, dy);
  if (length <= 0.0F || dash <= 0.0F)
  {
    return;
  }
  const auto dashes = static_cast<int>(std::ceil(length / (TWICE * dash)));
  for (int index = 0; index < dashes; ++index)
  {
    const float done = static_cast<float>(index) * TWICE * dash;
    const float end = std::min(done + dash, length);
    draw->AddLine(
        ImVec2(from.x + (dx * done / length), from.y + (dy * done / length)),
        ImVec2(from.x + (dx * end / length), from.y + (dy * end / length)),
        pen.colour, pen.thickness);
  }
}

void arrow(ImDrawList* draw, ImVec2 from, ImVec2 to, Pen pen, float head)
{
  const ImU32 colour = pen.colour;
  const float thickness = pen.thickness;
  const float dx = to.x - from.x;
  const float dy = to.y - from.y;
  const float length = std::hypot(dx, dy);
  if (length < 1.0F)
  {
    return;
  }
  const float ux = dx / length;
  const float uy = dy / length;
  const ImVec2 back(to.x - (ux * head), to.y - (uy * head));
  draw->AddLine(from, back, colour, thickness);
  draw->AddTriangleFilled(
      to, ImVec2(back.x - (uy * head * HALF), back.y + (ux * head * HALF)),
      ImVec2(back.x + (uy * head * HALF), back.y - (ux * head * HALF)), colour);
}

/** A live chance's colour: the leg colour, stronger the higher the chance
 * and the more runs it rests on. */
[[nodiscard]] ImU32 chanceColour(int shots, int blocks)
{
  if (shots <= 0)
  {
    return 0;
  }
  const float chance = static_cast<float>(blocks) / static_cast<float>(shots);
  const float sure = std::min(1.0F, static_cast<float>(shots) / SOLID_SHOTS);
  return withAlpha(LEG_COLOUR,
                   Alpha{(CHANCE_ALPHA_MIN + (CHANCE_ALPHA_SPAN * chance)) *
                         (HALF + (HALF * sure))});
}

void drawRun(ImDrawList* draw, const Frame& frame, const RunView& run,
             float alpha, bool latest, float scale)
{
  const ImU32 result = withAlpha(resultColour(run.result), Alpha{alpha});
  const ImVec2 shooter = frame.at(0.0F, 0.0F);
  draw->AddLine(shooter, frame.at(run.goalAlong, 0.0F), result,
                latest ? THICK * scale : LINE * scale);
  if (run.result == RunResult::SCATTER)
  {
    return;
  }
  const float body = std::max(MatchTuning::Body::RADIUS_METRES * frame.ppm,
                              MIN_BODY_PIXELS * scale);
  const ImVec2 him = frame.at(run.himAlong, run.himAcross);
  draw->AddCircleFilled(him, body, withAlpha(bodyColour(run), Alpha{alpha}));
  if (!latest)
  {
    return;
  }
  // His velocity as it was struck, and where it would have carried him as
  // the ball passed (the ghost), with the gap to the ball's path.
  const ImU32 pace = paceColour(paceOf(run));
  arrow(draw, him,
        frame.at(run.himAlong + (run.paceAlong * ARROW_SECONDS),
                 run.himAcross + (run.paceAcross * ARROW_SECONDS)),
        Pen{.colour = pace, .thickness = THICK * scale}, ARROW_HEAD * scale);
  if (run.gap >= 0.0F)
  {
    const ImVec2 ghost = frame.at(run.projectedAlong, run.projectedAcross);
    draw->AddCircle(ghost, body, GHOST, 0, LINE * scale);
    dashedLine(draw, ghost, frame.at(run.projectedAlong, 0.0F),
               Pen{.colour = GHOST, .thickness = LINE * scale},
               DASH * scale * HALF);
  }
}

void drawOverview(ImDrawList* draw, const std::deque<RunView>& runs,
                  float length, ImVec2 origin, ImVec2 size, float scale)
{
  const RunView& latest = runs.back();
  const float halfGoal = MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES;
  float across = std::max(MIN_ACROSS_METRES, halfGoal + ROOM_METRES);
  across = std::max(across, std::abs(latest.himAcross) + ROOM_METRES);
  const float span = length + (TWICE * ROOM_METRES);
  Frame frame;
  frame.ppm = std::min(size.y / span, size.x / (TWICE * across));
  frame.centreX = origin.x + (size.x * HALF);
  frame.baseY = origin.y + size.y - (ROOM_METRES * frame.ppm);
  draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                      GRASS);
  // A line every 5 m out from the shot.
  const auto lines = static_cast<int>((length + ROOM_METRES) / GRID_METRES);
  for (int index = 1; index <= lines; ++index)
  {
    const float y = frame.at(static_cast<float>(index) * GRID_METRES, 0.0F).y;
    draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + size.x, y), GRID_LINE,
                  1.0F);
  }
  // The goal, square to the aim, round the spot aimed at.
  if (latest.result == RunResult::SCATTER || latest.goalAlong > 0.0F)
  {
    draw->AddLine(frame.at(latest.aimAlong, latest.aimAcross - halfGoal),
                  frame.at(latest.aimAlong, latest.aimAcross + halfGoal), GOAL,
                  THICK * scale);
  }
  const std::size_t count = runs.size();
  for (std::size_t index = 0; index + 1 < count; ++index)
  {
    const float share =
        static_cast<float>(index + 1) / static_cast<float>(count);
    drawRun(draw, frame, runs[index],
            TRAIL_ALPHA_MIN + ((TRAIL_ALPHA_MAX - TRAIL_ALPHA_MIN) * share),
            false, scale);
  }
  dashedLine(draw, frame.at(0.0F, 0.0F),
             frame.at(latest.aimAlong, latest.aimAcross),
             Pen{.colour = AIM_LINE, .thickness = LINE * scale}, DASH * scale);
  drawRun(draw, frame, latest, static_cast<float>(OPAQUE), true, scale);
  draw->AddCircleFilled(frame.at(0.0F, 0.0F),
                        MIN_BODY_PIXELS * scale * SHOOTER_SIZE, SHOOTER);
}

void drawCloseUp(ImDrawList* draw, const GameXgLegSweep& sweep,
                 const RunView& run, ImVec2 origin, ImVec2 size, float scale)
{
  draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                      GRASS_LIGHT);
  draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                     true);
  Frame frame;
  frame.ppm = std::min(size.x, size.y) / (TWICE * CLOSE_HALF_METRES);
  frame.centreX = origin.x + (size.x * HALF);
  frame.baseY = origin.y + (size.y * HALF);
  frame.alongFrom = run.gap >= 0.0F ? run.projectedAlong : run.himAlong;
  // The offset bands either side of the ball's path, shaded by the chance
  // measured so far at this kind, rating and distance.
  const GameXgLegSweepSpec& spec = sweep.spec();
  const float top = origin.y;
  const float bottom = origin.y + size.y;
  for (std::size_t o = 0; o < spec.offsets.size(); ++o)
  {
    const float step = o + 1 < spec.offsets.size()
                           ? spec.offsets[o + 1] - spec.offsets[o]
                           : spec.offsets[o] - spec.offsets[o - 1];
    const auto [shots, blocks] =
        sweep.tally(run.kind, sweep.currentRating(), run.distanceIndex, o);
    const ImU32 colour = chanceColour(shots, blocks);
    for (const float side : {1.0F, -1.0F})
    {
      const float inner =
          std::max(0.0F, spec.offsets[o] - (step * HALF)) * side;
      const float outer = (spec.offsets[o] + (step * HALF)) * side;
      const float x0 = frame.at(0.0F, inner).x;
      const float x1 = frame.at(0.0F, outer).x;
      if (colour != 0)
      {
        draw->AddRectFilled(ImVec2(std::min(x0, x1), top),
                            ImVec2(std::max(x0, x1), bottom), colour);
      }
      if (run.bin && *run.bin == o)
      {
        draw->AddRect(ImVec2(std::min(x0, x1), top),
                      ImVec2(std::max(x0, x1), bottom), HIGHLIGHT, 0.0F, 0,
                      LINE * scale);
      }
    }
  }
  // The ball's path, the ball as it passed him, and him: the ghost where
  // he'd have been, and where he was as it was struck.
  const ImU32 result = resultColour(run.result);
  draw->AddLine(ImVec2(frame.centreX, top), ImVec2(frame.centreX, bottom),
                result, THICK * scale);
  const float body = MatchTuning::Body::RADIUS_METRES * frame.ppm;
  draw->AddCircleFilled(frame.at(run.himAlong, run.himAcross), body,
                        withAlpha(bodyColour(run), Alpha{TRAIL_ALPHA_MAX}));
  if (run.gap >= 0.0F)
  {
    const ImVec2 ghost = frame.at(run.projectedAlong, run.projectedAcross);
    draw->AddCircle(ghost, body, GHOST, 0, THICK * scale);
    const ImVec2 onPath = frame.at(run.projectedAlong, 0.0F);
    draw->AddLine(ghost, onPath, GHOST, LINE * scale);
    const std::string gap = std::format("{:.2f} m", run.gap);
    draw->AddText(ImVec2(std::min(ghost.x, onPath.x),
                         ghost.y + body + (GAP * scale * HALF)),
                  TEXT, gap.c_str());
    draw->AddCircleFilled(
        onPath,
        std::max(MatchTuning::Units::BALL_RADIUS_METRES * frame.ppm,
                 MIN_BODY_PIXELS * scale),
        BALL);
  }
  draw->PopClipRect();
}

void drawTable(ImDrawList* draw, const GameXgLegSweep& sweep,
               const RunView* run, ImVec2 origin, ImVec2 size, float scale)
{
  const GameXgLegSweepSpec& spec = sweep.spec();
  const std::size_t kind = sweep.currentKind();
  const std::size_t rating = sweep.currentRating();
  const float line = ImGui::GetTextLineHeight();
  const std::string title = std::format(
      "{}{}", KIND_NAMES.at(kind),
      kind >= MOVEMENTS ? ""
                        : std::format(", Defending and Tackling {:.0f}",
                                      spec.ratings.at(rating)));
  draw->AddText(origin, TEXT, title.c_str());
  const float labels = ImGui::CalcTextSize("25 m").x + (GAP * scale * HALF);
  const ImVec2 at(origin.x + labels, origin.y + line + (GAP * scale * HALF));
  const float width = size.x - labels;
  const float height = size.y - (TWICE * line) - (GAP * scale);
  const auto columns = static_cast<float>(spec.offsets.size());
  const auto rows = static_cast<float>(spec.distances.size());
  const float cellW = width / columns;
  const float cellH = height / rows;
  draw->AddRectFilled(at, ImVec2(at.x + width, at.y + height), PANEL);
  for (std::size_t d = 0; d < spec.distances.size(); ++d)
  {
    // Nearest at the bottom, as the overview has it.
    const float y = at.y + height - (static_cast<float>(d + 1) * cellH);
    if (d % 2 == 0)
    {
      const std::string label = std::format("{:.0f} m", spec.distances[d]);
      draw->AddText(ImVec2(origin.x, y + ((cellH - line) * HALF)), TEXT_DIM,
                    label.c_str());
    }
    for (std::size_t o = 0; o < spec.offsets.size(); ++o)
    {
      const auto [shots, blocks] = sweep.tally(kind, rating, d, o);
      const float x = at.x + (static_cast<float>(o) * cellW);
      if (const ImU32 colour = chanceColour(shots, blocks); colour != 0)
      {
        draw->AddRectFilled(ImVec2(x + 1.0F, y + 1.0F),
                            ImVec2(x + cellW - 1.0F, y + cellH - 1.0F), colour);
      }
      if ((run != nullptr) && run->bin && *run->bin == o &&
          run->distanceIndex == d && run->kind == kind)
      {
        draw->AddRect(ImVec2(x, y), ImVec2(x + cellW, y + cellH), HIGHLIGHT,
                      0.0F, 0, LINE * scale);
      }
    }
  }
  const std::string axis = std::format(
      "off his projection: 0 to {:.1f} m  (shade: leg chance so far)",
      spec.offsets.back());
  draw->AddText(ImVec2(at.x, at.y + height + (GAP * scale * HALF)), TEXT_DIM,
                axis.c_str());
}

void drawLegend(ImDrawList* draw, ImVec2 at, float scale)
{
  const float line = ImGui::GetTextLineHeight();
  const float swatch = line * HALF;
  struct Item
  {
    ImU32 colour;
    const char* name;
  };
  const std::array<std::array<Item, 4>, 3> rows{{
      {{{.colour = LEG_COLOUR, .name = "leg block"},
        {.colour = PAST_LOW_COLOUR, .name = "past, low"},
        {.colour = TORSO_COLOUR, .name = "torso or head"},
        {.colour = PAST_HIGH_COLOUR, .name = "past, high"}}},
      {{{.colour = STANDING_PACE, .name = "standing"},
        {.colour = SIDESTEP_PACE, .name = "sidestepping"},
        {.colour = SPRINT_PACE, .name = "sprinting"},
        {.colour = OFF_GRID_COLOUR, .name = "too wide"}}},
      {{{.colour = OPPONENT_BODY, .name = "opponent"},
        {.colour = TEAMMATE_BODY, .name = "team-mate"},
        {.colour = GHOST, .name = "where he'd have been"},
        {.colour = AIM_LINE, .name = "aim"}}},
  }};
  float y = at.y;
  for (const auto& row : rows)
  {
    float x = at.x;
    for (const Item& item : row)
    {
      draw->AddRectFilled(ImVec2(x, y + (swatch * HALF)),
                          ImVec2(x + swatch, y + (swatch * SWATCH_FOOT)),
                          item.colour);
      x += swatch + (GAP * scale * HALF);
      draw->AddText(ImVec2(x, y), TEXT_DIM, item.name);
      x += ImGui::CalcTextSize(item.name).x + GAP * scale;
    }
    y += line;
  }
}

[[nodiscard]] std::string describe(const RunView& run)
{
  if (run.result == RunResult::SCATTER)
  {
    return std::format(
        "Shooter's error, Shooting {:.0f}: struck from {:.0f} m with nobody "
        "in the way. The estimate spreads its aim lines by these.",
        run.rating, run.goalAlong);
  }
  const std::string where =
      run.gap >= 0.0F
          ? std::format(
                "passed {:.2f} m from where he'd have been, {:.2f} m "
                "up (hip {:.2f} m)",
                run.gap, std::max(run.height, 0.0F), run.hip)
          : std::string("never got level with him");
  return std::format(
      "{}{}  |  {:.0f} m out, set {:.1f} m off the line, {:.1f} m/s\n{}: {}",
      KIND_NAMES.at(run.kind),
      run.kind >= MOVEMENTS ? "" : std::format(", rating {:.0f}", run.rating),
      run.distance, run.offset, paceOf(run), where, resultName(run.result));
}
}  // namespace

void LegScanView::draw(const GameXgLegSweep* sweep, ImVec2 origin, ImVec2 size,
                       float scale)
{
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const float line = ImGui::GetTextLineHeight();
  const float gap = GAP * scale;
  draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                     true);
  const RunView* latest = (sweep != nullptr) && !sweep->recentRuns().empty()
                              ? &sweep->recentRuns().back()
                              : nullptr;
  const std::string header =
      latest != nullptr
          ? describe(*latest)
          : std::string(
                "Run the Legs sweep to watch it measure: each Shot "
                "block run's shot, the defender and what came of it.");
  draw->AddText(origin, TEXT, header.c_str());
  const float bodyTop = origin.y + (HEADER_LINES * line) + gap;
  const float legendHeight = LEGEND_LINES * line;
  const ImVec2 overviewAt(origin.x, bodyTop);
  const ImVec2 overviewSize(
      size.x * OVERVIEW_SHARE,
      std::max(line, origin.y + size.y - bodyTop - legendHeight - gap));
  drawLegend(draw,
             ImVec2(origin.x, overviewAt.y + overviewSize.y + (gap * HALF)),
             scale);
  if (latest == nullptr)
  {
    draw->AddRectFilled(
        overviewAt,
        ImVec2(overviewAt.x + overviewSize.x, overviewAt.y + overviewSize.y),
        GRASS);
    draw->PopClipRect();
    return;
  }
  const float target = std::max(MIN_LENGTH_METRES, latest->goalAlong);
  shownLength = shownLength <= 0.0F
                    ? target
                    : shownLength + ((target - shownLength) * EASE);
  drawOverview(draw, sweep->recentRuns(), shownLength, overviewAt, overviewSize,
               scale);
  const float rightX = overviewAt.x + overviewSize.x + gap;
  const float rightW = origin.x + size.x - rightX;
  const float rightH = origin.y + size.y - bodyTop;
  if (latest->result != RunResult::SCATTER)
  {
    const float closeSide = std::min(rightW, rightH * CLOSE_SHARE);
    drawCloseUp(draw, *sweep, *latest,
                ImVec2(rightX + ((rightW - closeSide) * HALF), bodyTop),
                ImVec2(closeSide, closeSide), scale);
    const float tableTop = bodyTop + closeSide + gap;
    drawTable(draw, *sweep, latest, ImVec2(rightX, tableTop),
              ImVec2(rightW, origin.y + size.y - tableTop), scale);
  }
  draw->PopClipRect();
}
