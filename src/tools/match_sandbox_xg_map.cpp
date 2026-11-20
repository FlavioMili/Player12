// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_xg_map.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <string>
#include <string_view>

#include "global/paths.h"
#include "gui/gui_view.h"
#include "gui/widgets/theme.h"
#include "model/match_decision_frame.h"
#include "tools/match_sandbox_page_ui.h"
#include "tools/match_sandbox_setup.h"
#include "tools/match_sandbox_typography.h"

namespace
{
/** The xG contours drawn on the map. */
constexpr std::array<float, 4> CONTOURS{0.05F, 0.10F, 0.20F, 0.40F};
/** At most this many cells along the pitch (finer on small pitches). */
constexpr float MAX_CELLS_ALONG = 140.0F;
constexpr float MIN_CELL_METRES = 0.25F;
constexpr float NO_DEFENDER_METRES = 1000.0F;
/** The Laws' centre circle and penalty arc (9.15 m). */
constexpr float CIRCLE_RADIUS_METRES = 9.15F;
constexpr std::array<const char*, 8> GOAL_NAMES{
    "None",   "Full size", "9v9",          "7v7",
    "Futsal", "5-a-side",  "Small framed", "Pop-up"};

/** A colour for an xG: dark blue at none, through green and yellow, to red
 * from 0.4 (a square-root scale, so small chances read apart). */
ImU32 xgColour(float xg)
{
  constexpr float FULL_COLOUR_XG = 0.4F;
  constexpr int MAP_ALPHA = 150;
  const float t =
      std::clamp(std::sqrt(std::max(xg, 0.0F) / FULL_COLOUR_XG), 0.0F, 1.0F);
  constexpr std::array<std::array<float, 3>, 5> STOPS{{{30, 50, 160},
                                                       {20, 150, 200},
                                                       {60, 190, 80},
                                                       {240, 220, 40},
                                                       {220, 50, 40}}};
  const float at = t * static_cast<float>(STOPS.size() - 1);
  const std::size_t low =
      std::min(static_cast<std::size_t>(at), STOPS.size() - 2);
  const float share = at - static_cast<float>(low);
  const auto mix = [&](std::size_t channel)
  {
    return static_cast<int>(
        STOPS.at(low).at(channel) +
        ((STOPS.at(low + 1).at(channel) - STOPS.at(low).at(channel)) * share));
  };
  return IM_COL32(mix(0), mix(1), mix(2), MAP_ALPHA);
}
}  // namespace

XgMapScene::XgMapScene(GUIView* guiView_ptr) : GUIScene(guiView_ptr) {}

XgMapScene::~XgMapScene() = default;

SceneID XgMapScene::getID() const { return SceneID::MATCH_SANDBOX; }

void XgMapScene::onEnter()
{
  auto* atlas = ImGui::GetIO().Fonts;
  const auto font = [&](const char* file, float size)
  {
    const auto config = SandboxTypography::manropeConfig();
    for (auto* existing : atlas->Fonts)
    {
      if (std::string_view(existing->GetDebugName()).find(file) !=
              std::string_view::npos &&
          !existing->Sources.empty() &&
          existing->Sources[0]->ExtraSizeScale == config.ExtraSizeScale)
      {
        return existing;
      }
    }
    return atlas->AddFontFromFileTTF(
        AssetPaths::file((std::string("assets/fonts/") + file).c_str()).c_str(),
        size, &config);
  };
  regularFont = font("Manrope-Regular.ttf", 16);
  boldFont = font("Manrope-Bold.ttf", 14);
}

void XgMapScene::update(float /*deltaTime*/) {}

MatchPitch XgMapScene::pitch() const
{
  if (!trainingPitch)
  {
    return MatchPitch::matchSize();
  }
  return MatchPitch::training(lengthSetting, widthSetting,
                              static_cast<GoalKind>(goalChoice));
}

bool XgMapScene::capybaraScore()
{
  return sandboxBrains()[0] == BrainId::CAPYBARA;
}

namespace
{
/** Capybara's shot score core at a spot, with no layers. */
DecisionFrame::Score capybaraShotCore(const MatchPitch& on, PitchPoint at,
                                      float estimate)
{
  DecisionFrame::Score shot;
  MatchEngine::addShotCoreCapybara(shot,
                                   MatchEngine::neutralCarrierWeightsCapybara(),
                                   on, at, true, estimate);
  return shot;
}
}  // namespace

XgMapScene::Spot XgMapScene::spotAt(const MatchPitch& on, PitchPoint at) const
{
  using S = MatchTuning::Shooting;
  using D = MatchTuning::Decision;
  Spot spot;
  const float shooting = shootingSetting / 100.0F;
  const float opponent = defender ? defenderMetres : NO_DEFENDER_METRES;
  // The brains' estimate, in its parts.
  spot.xg = MatchEngine::xgBreakdown(on, at, true, shooting, opponent);
  const float estimate = spot.xg.xg;
  // As struck (strikeShot): a header's discount, then the shot's limits.
  spot.struck = std::clamp(header ? estimate * S::HEADER_XG_FACTOR : estimate,
                           S::MIN_GOAL_PROBABILITY, S::MAX_OPEN_PLAY_XG);
  // The shot score's core, with the decision's own pressure (its radius).
  const float pressure = std::clamp(
      (D::PRESSURE_RADIUS_METRES - opponent) / D::PRESSURE_RADIUS_METRES, 0.0F,
      1.0F);
  spot.score = MatchEngine::shotScoreParts(on, at, true, estimate, spaceAhead,
                                           shooting, pressure, 0.0F);
  spot.scoreTotal = capybaraScore() ? capybaraShotCore(on, at, estimate).total()
                                    : spot.score.total;
  spot.considered = header ? estimate * S::HEADER_XG_FACTOR >=
                                 MatchTuning::Aerial::HEADER_SHOT_MIN_XG
                           : estimate >= D::MIN_SHOT_XG;
  return spot;
}

void XgMapScene::rebuild()
{
  built = pitch();
  cell = std::max(MIN_CELL_METRES, built.length / MAX_CELLS_ALONG);
  columns = static_cast<int>(std::ceil(built.length / cell));
  rows = static_cast<int>(std::ceil(built.width / cell));
  grid.assign(
      static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows),
      Spot{});
  for (int row = 0; row < rows; ++row)
  {
    for (int column = 0; column < columns; ++column)
    {
      grid[(static_cast<std::size_t>(row) * static_cast<std::size_t>(columns)) +
           static_cast<std::size_t>(column)] =
          spotAt(built, PitchPoint{(static_cast<float>(column) + 0.5F) * cell,
                                   (static_cast<float>(row) + 0.5F) * cell});
    }
  }
  dirty = false;
}

void XgMapScene::render()
{
  namespace Page = SandboxPageUI;
  const auto* viewport = ImGui::GetMainViewport();
  const float scale = Theme::scale();
  // The sandbox pages' theme for every control on the page.
  const Page::Style style(regularFont, scale);
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, Page::PAGE);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(24 * scale, 24 * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGui::Begin("##xg_map", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();
  if (dirty)
  {
    rebuild();
  }
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float gutter = (6 * scale) + ImGui::GetStyle().ScrollbarSize;
  const float panel = std::min(420 * scale, available.x * 0.4F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::BeginChild("##xg_map_controls", ImVec2(panel + gutter, available.y),
                    ImGuiChildFlags_AlwaysUseWindowPadding);
  renderPanel(panel);
  ImGui::EndChild();
  ImGui::SameLine(0, 6 * scale);
  renderMap(std::max(scale, ImGui::GetContentRegionAvail().x), available.y);
  ImGui::PopStyleVar();
  ImGui::End();
}

void XgMapScene::renderPanel(float width)
{
  namespace Page = SandboxPageUI;
  using S = MatchTuning::Shooting;
  using D = MatchTuning::Decision;
  const float scale = Theme::scale();
  Page::header(
      "Tools", "xG map", width, scale, boldFont, regularFont,
      "The engine's own xG for a shooter attacking the right-hand goal, "
      "and the core of the chosen brain's shot score (the home side's "
      "brain in the set-up: Capybara's own, else Binturong's).",
      true);
  Page::beginCard("##xg_map_settings", "SETTINGS", width, Page::DARK, scale,
                  boldFont);
  if (Page::button("Back to tools", false, scale, boldFont))
  {
    guiView->popScene();
  }
  const auto changed = [this](bool moved)
  {
    if (moved)
    {
      dirty = true;
    }
  };
  int pitchKind = trainingPitch ? 1 : 0;
  changed(ImGui::RadioButton("Match pitch", &pitchKind, 0));
  ImGui::SameLine();
  changed(ImGui::RadioButton("Training pitch", &pitchKind, 1));
  trainingPitch = pitchKind == 1;
  if (trainingPitch)
  {
    ImGui::SetNextItemWidth(220 * scale);
    changed(
        ImGui::SliderFloat("Length (m)", &lengthSetting, 10.0F, 80.0F, "%.0f"));
    ImGui::SetNextItemWidth(220 * scale);
    changed(
        ImGui::SliderFloat("Width (m)", &widthSetting, 8.0F, 60.0F, "%.0f"));
    ImGui::SetNextItemWidth(220 * scale);
    if (SandboxPageUI::beginCombo(
            "Goals", GOAL_NAMES.at(static_cast<std::size_t>(goalChoice))))
    {
      for (int index = 0; index < static_cast<int>(GOAL_NAMES.size()); ++index)
      {
        if (ImGui::Selectable(GOAL_NAMES.at(static_cast<std::size_t>(index)),
                              index == goalChoice))
        {
          goalChoice = index;
          dirty = true;
        }
      }
      ImGui::EndCombo();
    }
  }
  ImGui::SetNextItemWidth(220 * scale);
  changed(
      ImGui::SliderFloat("Shooting", &shootingSetting, 1.0F, 100.0F, "%.0f"));
  changed(ImGui::Checkbox("Header", &header));
  changed(ImGui::Checkbox("A defender", &defender));
  if (defender)
  {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160 * scale);
    changed(
        ImGui::SliderFloat("away (m)", &defenderMetres, 0.3F, 9.0F, "%.1f"));
  }
  ImGui::SetNextItemWidth(220 * scale);
  changed(ImGui::SliderFloat("Space ahead", &spaceAhead, 0.0F, 1.0F, "%.2f"));
  ImGui::TextUnformatted("Show");
  ImGui::SameLine();
  ImGui::RadioButton("xG", &layer, 0);
  ImGui::SameLine();
  ImGui::RadioButton("Shot score", &layer, 1);
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled(layer == 0
                          ? "Contours at 0.05, 0.10, 0.20 and 0.40. A header "
                            "shows its xG after the "
                            "header discount."
                          : "Green: the shot's own score is above nothing "
                            "(worth a shot on its own; "
                            "the brain still weighs passing and carrying). "
                            "Red: below. Clear: never "
                            "considered. No role, shout or cutting inside.");
  ImGui::PopTextWrapPos();
  Page::endCard(scale);

  // The spot under the mouse, in its parts.
  Page::beginCard("##xg_map_spot", "AT THE MOUSE", width, Page::LIGHT, scale,
                  boldFont);
  ImGui::PushTextWrapPos(0);
  if (!hovered)
  {
    ImGui::TextDisabled("Point at the pitch.");
  }
  else
  {
    const Spot spot = spotAt(built, *hovered);
    const MatchEngine::XgBreakdown& x = spot.xg;
    ImGui::Text("At %.1f m along, %.1f m across", hovered->x, hovered->y);
    if (!x.goal)
    {
      ImGui::TextUnformatted("No goal on this pitch: xG 0, nobody shoots.");
    }
    else if (!x.inRange)
    {
      ImGui::Text(
          "%.1f m from the goal: beyond the fit's %.0f m, the floor %.3f.",
          x.metres, S::XG_MAX_DISTANCE_METRES, x.xg);
    }
    else
    {
      ImGui::Text(
          "%.1f m from the goal (%.1f m from its line, %.1f m off the middle); "
          "the goal's mouth shows %.1f degrees.",
          x.metres, x.lineMetres, x.lateralMetres, x.angle * 57.29578F);
      ImGui::Text("Location (the fitted model): %.3f", x.location);
      ImGui::Text("x skill %.2f (Shooting %.0f)", x.skillFactor,
                  shootingSetting);
      ImGui::Text(
          "x pressure %.2f (%s)", x.pressureFactor,
          defender
              ? std::format("a defender {:.1f} m away", defenderMetres).c_str()
              : "nobody near");
      ImGui::Text("= %.3f; xG %.3f%s", x.unclamped, x.xg,
                  x.unclamped > S::MAX_OPEN_PLAY_XG   ? " (capped)"
                  : x.unclamped < S::MIN_OPEN_PLAY_XG ? " (floored)"
                                                      : "");
    }
    if (header)
    {
      ImGui::Text(
          "Header: x %.2f = %.3f; he heads at goal if that reaches %.2f: %s",
          S::HEADER_XG_FACTOR, x.xg * S::HEADER_XG_FACTOR,
          MatchTuning::Aerial::HEADER_SHOT_MIN_XG,
          spot.considered ? "yes" : "no");
    }
    ImGui::Text("As struck: %.3f", spot.struck);
    ImGui::Separator();
    const MatchEngine::ShotScoreParts& s = spot.score;
    if (!header && !spot.considered)
    {
      ImGui::Text("Shot score: not considered (xG under %.3f).",
                  D::MIN_SHOT_XG);
    }
    else if (!header && capybaraScore())
    {
      // Capybara's own terms (DecisionFrame), as the brain sums them.
      const DecisionFrame::Score shot =
          capybaraShotCore(built, *hovered, spot.xg.xg);
      ImGui::TextUnformatted(
          std::format("Capybara shot score {:.2f}:", shot.total()).c_str());
      for (const DecisionFrame::Term& term : shot.allTerms())
      {
        ImGui::TextUnformatted(
            std::format("  {}: {:+.2f}", term.name, term.value()).c_str());
      }
    }
    else if (!header)
    {
      ImGui::TextUnformatted(
          std::format("Binturong shot score {:.2f}:", s.total).c_str());
      ImGui::Text("  xG above %.3f: %+.2f", D::BASE_SHOT_THRESHOLD, s.xgTerm);
      ImGui::Text("  have a go (space %.2f, range %.2f): %+.2f", spaceAhead,
                  s.rangeEligibility, s.haveAGo);
      ImGui::Text("  final third: %+.2f", s.finalThird);
      ImGui::Text("  Shooting: %+.2f", s.skill);
      ImGui::Text("  pressure: %+.2f", -s.pressure);
      if (s.wideFactor != 1.0F)
      {
        ImGui::Text("  wide angle: x %.2f (more than %.1f m off the middle)",
                    s.wideFactor, D::WIDE_SHOT_WIDTH_METRES);
      }
    }
  }
  ImGui::PopTextWrapPos();
  Page::endCard(scale);
}

void XgMapScene::renderMap(float width, float height)
{
  // The attacking half only, turned so its goal is at the top: plain grass,
  // its lines (a match pitch's markings, or a training pitch's walls), its
  // goal, the map in cells with a hairline grid, and the spot under the
  // mouse.
  namespace Page = SandboxPageUI;
  using M = MatchTuning::Pitch;
  const float scale = Theme::scale();
  Page::beginCard("##xg_map_pitch", "ATTACKING HALF", width, Page::DARK, scale,
                  boldFont);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 size(std::max(scale, ImGui::GetContentRegionAvail().x),
                    std::max(200 * scale, height - (56 * scale)));
  ImGui::InvisibleButton("##xg_map_area", size);
  const bool hovering = ImGui::IsItemHovered();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                     true);

  const float half = 0.5F * built.length;
  const float depth = built.goal.present() ? built.goal.netDepth : 0.0F;
  // Room round it: the goal's net above, a margin of grass all round.
  constexpr float MARGIN = 3.0F;
  const float ppm = std::min(size.x / (built.width + 2.0F * MARGIN),
                             size.y / (half + depth + 2.0F * MARGIN));
  const float left = origin.x + (0.5F * (size.x - built.width * ppm));
  const float top =
      origin.y + (0.5F * (size.y - (half + depth) * ppm)) + (depth * ppm);
  // A pitch point (x towards the goal at x = length) on the screen.
  const auto screen = [&](float x, float y)
  { return ImVec2(left + (y * ppm), top + ((built.length - x) * ppm)); };

  // Plain grass, a shade darker round the pitch.
  draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                      IM_COL32(52, 96, 52, 255));
  draw->AddRectFilled(screen(built.length, 0.0F), screen(half, built.width),
                      IM_COL32(62, 116, 62, 255));

  // The map.
  const auto valueOf = [&](const Spot& spot)
  {
    return header ? spot.xg.xg * MatchTuning::Shooting::HEADER_XG_FACTOR
                  : spot.xg.xg;
  };
  const int firstColumn = static_cast<int>(half / cell);
  for (int row = 0; row < rows; ++row)
  {
    for (int column = firstColumn; column < columns; ++column)
    {
      const Spot& spot = grid[(static_cast<std::size_t>(row) *
                               static_cast<std::size_t>(columns)) +
                              static_cast<std::size_t>(column)];
      ImU32 colour = 0;
      if (layer == 0)
      {
        colour = xgColour(valueOf(spot));
      }
      else if (spot.considered)
      {
        const float total =
            header ? (valueOf(spot) - MatchTuning::Aerial::HEADER_SHOT_MIN_XG) *
                         20.0F
                   : spot.scoreTotal;
        const int alpha = static_cast<int>(
            40 + (120 * std::min(1.0F, std::abs(total) / 3.0F)));
        colour = total > 0.0F ? IM_COL32(40, 200, 80, alpha)
                              : IM_COL32(220, 60, 50, alpha);
      }
      if (colour == 0)
      {
        continue;
      }
      const float x0 = std::max(static_cast<float>(column) * cell, half);
      const float y0 = static_cast<float>(row) * cell;
      const float x1 =
          std::min(static_cast<float>(column + 1) * cell, built.length);
      const float y1 = std::min(y0 + cell, built.width);
      draw->AddRectFilled(screen(x1, y0), screen(x0, y1), colour);
    }
  }
  // A hairline grid at the cells' edges.
  constexpr ImU32 GRID = IM_COL32(255, 255, 255, 22);
  for (int column = firstColumn; column <= columns; ++column)
  {
    const float x =
        std::clamp(static_cast<float>(column) * cell, half, built.length);
    draw->AddLine(screen(x, 0.0F), screen(x, built.width), GRID, 1.0F);
  }
  for (int row = 0; row <= rows; ++row)
  {
    const float y = std::min(static_cast<float>(row) * cell, built.width);
    draw->AddLine(screen(half, y), screen(built.length, y), GRID, 1.0F);
  }
  // Contours: an edge between two cells either side of a contour value.
  if (layer == 0)
  {
    for (int row = 0; row < rows; ++row)
    {
      for (int column = firstColumn; column < columns; ++column)
      {
        const float here = valueOf(grid[(static_cast<std::size_t>(row) *
                                         static_cast<std::size_t>(columns)) +
                                        static_cast<std::size_t>(column)]);
        const float x1 =
            std::min(static_cast<float>(column + 1) * cell, built.length);
        const float y1 =
            std::min(static_cast<float>(row + 1) * cell, built.width);
        for (const float level : CONTOURS)
        {
          if (column + 1 < columns &&
              (here < level) !=
                  (valueOf(grid[(static_cast<std::size_t>(row) *
                                 static_cast<std::size_t>(columns)) +
                                static_cast<std::size_t>(column) + 1]) < level))
          {
            draw->AddLine(screen(x1, y1 - cell), screen(x1, y1),
                          IM_COL32(255, 255, 255, 210), 1.5F);
          }
          if (row + 1 < rows &&
              (here < level) !=
                  (valueOf(grid[((static_cast<std::size_t>(row) + 1) *
                                 static_cast<std::size_t>(columns)) +
                                static_cast<std::size_t>(column)]) < level))
          {
            draw->AddLine(screen(x1 - cell, y1), screen(x1, y1),
                          IM_COL32(255, 255, 255, 210), 1.5F);
          }
        }
      }
    }
  }

  // The lines: a match pitch's markings, or a training pitch's walls.
  const float line = std::max(1.0F, 0.12F * ppm);
  constexpr ImU32 WHITE = IM_COL32(240, 240, 235, 230);
  const float middle = 0.5F * built.width;
  const auto box = [&](float deep, float halfWide)
  {
    draw->AddRect(screen(built.length, middle - halfWide),
                  screen(built.length - deep, middle + halfWide), WHITE, 0.0F,
                  0, line);
  };
  if (built.surface == PitchSurface::MATCH)
  {
    draw->AddRect(screen(built.length, 0.0F), screen(half, built.width), WHITE,
                  0.0F, 0, line);
    box(M::PENALTY_AREA_DEPTH_METRES, M::PENALTY_AREA_HALF_WIDTH_METRES);
    box(MatchTuning::DeadBall::GOAL_AREA_DEPTH_METRES,
        M::GOAL_AREA_HALF_WIDTH_METRES);
    const ImVec2 spot = screen(built.length - M::PENALTY_SPOT_METRES, middle);
    draw->AddCircleFilled(spot, std::max(1.5F, 0.2F * ppm), WHITE);
    // The arc outside the penalty area and the halfway circle (9.15 m).
    const float radius = CIRCLE_RADIUS_METRES * ppm;
    const float beyond = M::PENALTY_AREA_DEPTH_METRES - M::PENALTY_SPOT_METRES;
    const float arcHalf =
        std::acos(std::clamp(beyond / CIRCLE_RADIUS_METRES, -1.0F, 1.0F));
    draw->PathArcTo(spot, radius, 1.5707963F - arcHalf, 1.5707963F + arcHalf,
                    24);
    draw->PathStroke(WHITE, 0, line);
    constexpr int ARC_SEGMENTS = 32;
    draw->PathArcTo(screen(half, middle), radius, std::numbers::pi_v<float>,
                    2 * std::numbers::pi_v<float>, ARC_SEGMENTS);
    draw->PathStroke(WHITE, 0, line);
  }
  else
  {
    // The walls, open at the goal mouth; the halfway line faint.
    constexpr ImU32 WALL = IM_COL32(20, 26, 30, 240);
    const float gap =
        built.goal.present() ? built.goal.halfWidth + built.goal.post : 0.0F;
    draw->AddLine(screen(built.length, 0.0F), screen(half, 0.0F), WALL,
                  2.0F * line);
    draw->AddLine(screen(built.length, built.width), screen(half, built.width),
                  WALL, 2.0F * line);
    draw->AddLine(screen(built.length, 0.0F),
                  screen(built.length, middle - gap), WALL, 2.0F * line);
    draw->AddLine(screen(built.length, middle + gap),
                  screen(built.length, built.width), WALL, 2.0F * line);
    draw->AddLine(screen(half, 0.0F), screen(half, built.width),
                  IM_COL32(240, 240, 235, 120), line);
  }
  // The goal: its net behind the line, its frame.
  if (built.goal.present())
  {
    const float halfWide = built.goal.halfWidth;
    draw->AddRectFilled(screen(built.length + depth, middle - halfWide),
                        screen(built.length, middle + halfWide),
                        IM_COL32(230, 230, 230, 60));
    const float post = std::max(2.0F, built.goal.post * ppm);
    draw->AddLine(screen(built.length, middle - halfWide),
                  screen(built.length, middle + halfWide),
                  IM_COL32(250, 250, 250, 255), post);
  }

  // The spot under the mouse.
  hovered.reset();
  if (hovering)
  {
    const ImVec2 mouse = ImGui::GetMousePos();
    const PitchPoint at{built.length - ((mouse.y - top) / ppm),
                        (mouse.x - left) / ppm};
    if (at.x >= half && at.x <= built.length && at.y >= 0.0F &&
        at.y <= built.width)
    {
      hovered = at;
      draw->AddCircle(mouse, 5.0F * scale, IM_COL32(255, 255, 255, 230), 0,
                      2.0F);
    }
  }
  draw->PopClipRect();
  Page::endCard(scale);
}
