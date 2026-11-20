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

#include "controller/game_controller.h"
#include "global/paths.h"
#include "gui/gui_view.h"
#include "gui/widgets/theme.h"
#include "model/match_decision_frame.h"
#include "tools/match_sandbox_drill.h"
#include "tools/match_sandbox_game_xg.h"
#include "tools/match_sandbox_page_ui.h"
#include "tools/match_sandbox_setup.h"
#include "tools/match_sandbox_typography.h"

namespace
{
/** The xG contours drawn on the map, and the game xG (empty goal) ones. */
constexpr std::array<float, 4> CONTOURS{0.05F, 0.10F, 0.20F, 0.40F};
constexpr std::array<float, 4> GAME_CONTOURS{0.20F, 0.40F, 0.60F, 0.80F};
/** The layers: the fitted xG, the shot score, game xG (empty goal). */
constexpr int XG_LAYER = 0;
constexpr int SCORE_LAYER = 1;
constexpr int GAME_LAYER = 2;
/** The fitted xG reaches full colour at 0.4; an empty goal's chances at 1. */
constexpr float FITTED_FULL_COLOUR = 0.4F;
constexpr float GAME_FULL_COLOUR = 1.0F;
/** The goal's middle, as a share of the pitch's width. */
constexpr float MIDDLE_SHARE = 0.5F;
/** The sweep: real time per frame (s), and its shots-a-point limits. */
constexpr double SWEEP_SECONDS_PER_FRAME = 0.012;
constexpr float MIN_SWEEP_REPEATS = 20.0F;
constexpr float MAX_SWEEP_REPEATS = 500.0F;
constexpr float PROGRESS_HEIGHT = 8.0F;
/** The sweep's spots on the map: size (m, with a floor in pixels) and the
 * waiting and measured colours (the one being measured is lime). */
constexpr float SWEEP_DOT_METRES = 0.15F;
constexpr float MIN_SWEEP_DOT_RADIUS = 1.5F;
constexpr ImU32 SWEEP_DOT_WAITING = IM_COL32(255, 255, 255, 60);
constexpr ImU32 SWEEP_DOT_DONE = IM_COL32(255, 255, 255, 220);
/** The game map's foot flag in the pitch's corner: inset and padding
 * (scaled pixels). */
constexpr float FLAG_INSET = 12.0F;
constexpr float FLAG_PADDING = 5.0F;
/** At most this many cells along the pitch (finer on small pitches). */
constexpr float MAX_CELLS_ALONG = 140.0F;
constexpr float MIN_CELL_METRES = 0.25F;
constexpr float NO_DEFENDER_METRES = 1000.0F;
/** The Laws' centre circle and penalty arc (9.15 m). */
constexpr float CIRCLE_RADIUS_METRES = 9.15F;
constexpr float DEGREES_PER_RADIAN = 180.0F / std::numbers::pi_v<float>;
constexpr float QUARTER_TURN = std::numbers::pi_v<float> / 2.0F;
constexpr int PENALTY_ARC_SEGMENTS = 24;
/** Each value is taken at the middle of its cell. */
constexpr float CELL_MIDDLE = 0.5F;
/** Controls (scaled pixels) and the training pitch's size limits (m). */
constexpr float CONTROL_WIDTH = 220.0F;
constexpr float DEFENDER_CONTROL_WIDTH = 160.0F;
constexpr float MIN_LENGTH_METRES = 10.0F;
constexpr float MAX_LENGTH_METRES = 80.0F;
constexpr float MIN_WIDTH_METRES = 8.0F;
constexpr float MAX_WIDTH_METRES = 60.0F;
constexpr float MIN_DEFENDER_METRES = 0.3F;
constexpr float MAX_DEFENDER_METRES = 9.0F;
/** Drawing: contour lines, the penalty spot, the walls (times a line), the
 * ring at the mouse (pixels). */
constexpr float CONTOUR_THICKNESS = 1.5F;
constexpr float MIN_SPOT_RADIUS = 1.5F;
constexpr float SPOT_RADIUS_METRES = 0.2F;
constexpr float WALL_LINES = 2.0F;
constexpr float MOUSE_RING_RADIUS = 5.0F;
constexpr float MOUSE_RING_THICKNESS = 2.0F;
constexpr std::array<const char*, 8> GOAL_NAMES{
    "None",   "Full size", "9v9",          "7v7",
    "Futsal", "5-a-side",  "Small framed", "Pop-up"};

/** A colour for an xG: dark blue at none, through green and yellow, to red
 * from `full` (a square-root scale, so small chances read apart). */
ImU32 xgColour(float xg, float full = FITTED_FULL_COLOUR)
{
  constexpr int MAP_ALPHA = 150;
  const float t = std::clamp(std::sqrt(std::max(xg, 0.0F) / full), 0.0F, 1.0F);
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

XgMapScene::XgMapScene(GUIView* guiView_ptr)
    : GUIScene(guiView_ptr), drills(makeDrills())
{
  shotDrill = gameXgDrill(drills);
}

XgMapScene::~XgMapScene() = default;

GoalKind XgMapScene::goalKind() const
{
  return trainingPitch ? static_cast<GoalKind>(goalChoice) : GoalKind::FULL;
}

void XgMapScene::loadGameTable()
{
  gameTableGoal = goalKind();
  gameTable.reset();
  if (*gameTableGoal == GoalKind::NONE)
  {
    gameTableNote = "No goal on this pitch.";
    return;
  }
  const auto path = GameXg::file(GameXgTable::EMPTY_GOAL, *gameTableGoal);
  std::string problem;
  gameTable = GameXgTable::load(path, problem);
  gameTableNote =
      gameTable ? std::format(
                      "Measured {} with a {}-footed shooter: {} shots a "
                      "point, {} points; left and right {}.",
                      gameTable->measured, gameTable->foot, gameTable->repeats,
                      gameTable->samples.size(),
                      gameTable->mirrored ? "folded together" : "kept apart")
                : std::format("No game map for the {} goal yet: run the sweep.",
                              goalKindName(*gameTableGoal));
}

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
  regularFont = font("Manrope-Regular.ttf", SandboxPageUI::REGULAR_FONT_SIZE);
  boldFont = font("Manrope-Bold.ttf", SandboxPageUI::BOLD_FONT_SIZE);
}

void XgMapScene::update(float /*deltaTime*/)
{
  if (!sweep || sweep->done())
  {
    return;
  }
  // Measured with Capybara's brain, as fm_lab game-xg does: the map is for
  // Capybara (the set-up's brains are put back after each slice).
  const auto brains = sandboxBrains();
  sandboxBrains() = {BrainId::CAPYBARA, BrainId::CAPYBARA};
  const bool more = sweep->work(SWEEP_SECONDS_PER_FRAME);
  sandboxBrains() = brains;
  if (more || sweep->stopped())
  {
    return;
  }
  const GameXgTable& table = sweep->table();
  const auto path = GameXg::file(table.layer, table.goal);
  std::string problem;
  sweepNote =
      (table.save(path, problem) ? std::format("Saved {} ({:.0f} s).",
                                               path.string(), sweep->seconds())
                                 : "Not saved: " + problem) +
      std::format(
          " Left and right {} ({:.0f}% of mirrored pairs differed; "
          "pooled difference {:+.1f} standard errors).",
          table.mirrored ? "agree: folded together" : "differ: both kept",
          100.0 * sweep->asymmetricShare(), sweep->asymmetricZ());
  loadGameTable();
  dirty = true;
}

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
  // Game xG, the empty goal: metres out from the right-hand goal's line and
  // off its middle (+ towards the bottom touchline, as the Shot drill).
  if (gameTable && on.goal.present())
  {
    spot.game = gameTable->value(
        on.length - at.x, at.y - (MIDDLE_SHARE * on.width), shootingSetting);
  }
  return spot;
}

void XgMapScene::rebuild()
{
  if (gameTableGoal != goalKind())
  {
    loadGameTable();
  }
  built = pitch();
  cell = std::max(MIN_CELL_METRES, built.length / MAX_CELLS_ALONG);
  // Cells anchored on the goal: the columns end exactly at its line, and a
  // whole row is centred on its middle (part cells fall at the far edges).
  columns = static_cast<int>(std::ceil(built.length / cell));
  originX = built.length - (static_cast<float>(columns) * cell);
  const float middleRowEdge =
      (MIDDLE_SHARE * built.width) - (CELL_MIDDLE * cell);
  originY = middleRowEdge - (std::ceil(middleRowEdge / cell) * cell);
  rows = static_cast<int>(std::ceil((built.width - originY) / cell));
  grid.assign(
      static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows),
      Spot{});
  for (int row = 0; row < rows; ++row)
  {
    for (int column = 0; column < columns; ++column)
    {
      // Each value at its cell's middle, kept on the pitch for part cells.
      grid[(static_cast<std::size_t>(row) * static_cast<std::size_t>(columns)) +
           static_cast<std::size_t>(column)] =
          spotAt(built,
                 PitchPoint{std::clamp(edgeX(column) + (CELL_MIDDLE * cell),
                                       0.0F, built.length),
                            std::clamp(edgeY(row) + (CELL_MIDDLE * cell), 0.0F,
                                       built.width)});
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
                      ImVec2(SandboxPageUI::PAGE_PADDING * scale,
                             SandboxPageUI::PAGE_PADDING * scale));
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
  const float gutter =
      (SandboxPageUI::PANEL_GAP * scale) + ImGui::GetStyle().ScrollbarSize;
  const float panel = std::min(420 * scale, available.x * 0.4F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::BeginChild("##xg_map_controls", ImVec2(panel + gutter, available.y),
                    ImGuiChildFlags_AlwaysUseWindowPadding);
  renderPanel(panel);
  ImGui::EndChild();
  ImGui::SameLine(0, SandboxPageUI::PANEL_GAP * scale);
  renderMap(std::max(scale, ImGui::GetContentRegionAvail().x), available.y);
  ImGui::PopStyleVar();
  ImGui::End();
}

void XgMapScene::renderPanel(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::header(
      "Tools", "xG map", width, scale, boldFont, regularFont,
      "The engine's own xG for a shooter attacking the right-hand goal, "
      "and the core of the chosen brain's shot score (the home side's "
      "brain in the set-up: Capybara's own, else Binturong's).",
      true);
  // First, so it is in view while the mouse is on the pitch.
  renderSpot(width);
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
  changed(Page::radio("Match pitch", &pitchKind, 0));
  ImGui::SameLine();
  changed(Page::radio("Training pitch", &pitchKind, 1));
  trainingPitch = pitchKind == 1;
  if (trainingPitch)
  {
    ImGui::SetNextItemWidth(CONTROL_WIDTH * scale);
    changed(ImGui::SliderFloat("Length (m)", &lengthSetting, MIN_LENGTH_METRES,
                               MAX_LENGTH_METRES, "%.0f"));
    ImGui::SetNextItemWidth(CONTROL_WIDTH * scale);
    changed(ImGui::SliderFloat("Width (m)", &widthSetting, MIN_WIDTH_METRES,
                               MAX_WIDTH_METRES, "%.0f"));
    ImGui::SetNextItemWidth(CONTROL_WIDTH * scale);
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
  ImGui::SetNextItemWidth(CONTROL_WIDTH * scale);
  changed(
      ImGui::SliderFloat("Shooting", &shootingSetting, 1.0F, 100.0F, "%.0f"));
  changed(ImGui::Checkbox("Header", &header));
  changed(ImGui::Checkbox("A defender", &defender));
  if (defender)
  {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(DEFENDER_CONTROL_WIDTH * scale);
    changed(ImGui::SliderFloat("away (m)", &defenderMetres, MIN_DEFENDER_METRES,
                               MAX_DEFENDER_METRES, "%.1f"));
  }
  ImGui::SetNextItemWidth(CONTROL_WIDTH * scale);
  changed(ImGui::SliderFloat("Space ahead", &spaceAhead, 0.0F, 1.0F, "%.2f"));
  ImGui::TextUnformatted("Show");
  ImGui::SameLine();
  Page::radio("xG", &layer, XG_LAYER);
  ImGui::SameLine();
  Page::radio("Shot score", &layer, SCORE_LAYER);
  ImGui::SameLine();
  Page::radio("Game xG", &layer, GAME_LAYER);
  ImGui::PushTextWrapPos(0);
  if (layer == XG_LAYER)
  {
    ImGui::TextDisabled(
        "Contours at 0.05, 0.10, 0.20 and 0.40. A header shows its xG after "
        "the header discount.");
  }
  else if (layer == SCORE_LAYER)
  {
    ImGui::TextDisabled(
        "Green: the shot's own score is above nothing (worth a shot on its "
        "own; the brain still weighs passing and carrying). Red: below. "
        "Clear: never considered. No role, shout or cutting inside.");
  }
  else
  {
    ImGui::TextDisabled(
        "Game xG, layer 1: the chance a shot goes in with no keeper and "
        "nobody in the way, measured in the Shot drill (game xG plan). "
        "Contours at 0.2, 0.4, 0.6 and 0.8. Shooting only: no header, "
        "defender or pressure yet. Capybara doesn't use it until the keeper "
        "layer exists.");
  }
  ImGui::PopTextWrapPos();
  Page::endCard(scale);
  renderSweep(width);
}

void XgMapScene::renderSpot(float width)
{
  // The spot last pointed at (it stays when the mouse leaves), in its parts.
  namespace Page = SandboxPageUI;
  using S = MatchTuning::Shooting;
  using D = MatchTuning::Decision;
  const float scale = Theme::scale();
  Page::beginCard("##xg_map_spot", "AT THE SPOT", width, Page::LIGHT, scale,
                  boldFont);
  ImGui::PushTextWrapPos(0);
  if (!hovered)
  {
    ImGui::TextDisabled(
        "Point at the pitch. The last spot stays, marked, when the mouse "
        "leaves it.");
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
          x.metres, x.lineMetres, x.lateralMetres,
          x.angle * DEGREES_PER_RADIAN);
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
    if (spot.game >= 0.0F)
    {
      ImGui::Text("Game xG, empty goal: %.3f (Shooting %.0f, %s-footed)",
                  spot.game, shootingSetting, gameTable->foot.c_str());
    }
    else
    {
      ImGui::TextDisabled("Game xG, empty goal: none for this goal yet.");
    }
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

void XgMapScene::renderSweep(float width)
{
  // The game xG sweep for the map's goal: run, watch, and it saves itself.
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##xg_map_sweep", "GAME XG SWEEP", width, Page::DARK, scale,
                  boldFont);
  ImGui::PushTextWrapPos(0);
  ImGui::TextUnformatted(gameTableNote.c_str());
  const bool running = sweep && !sweep->done();
  if (running)
  {
    const double share = sweep->runsQueued() > 0
                             ? static_cast<double>(sweep->runsDone()) /
                                   static_cast<double>(sweep->runsQueued())
                             : 0.0;
    ImGui::ProgressBar(static_cast<float>(share),
                       ImVec2(-1.0F, PROGRESS_HEIGHT * scale), "");
    ImGui::Text("%s: %lld / %lld runs, %.0f s", sweep->phase().c_str(),
                sweep->runsDone(), sweep->runsQueued(), sweep->seconds());
    if (Page::button("Stop", false, scale, boldFont))
    {
      sweep->stop();
      sweepNote = "Stopped: nothing saved.";
    }
  }
  else if (goalKind() != GoalKind::NONE && shotDrill != nullptr)
  {
    ImGui::SetNextItemWidth(CONTROL_WIDTH * scale);
    ImGui::SliderFloat("Shots a point", &sweepRepeats, MIN_SWEEP_REPEATS,
                       MAX_SWEEP_REPEATS, "%.0f");
    if (Page::button("Run the sweep", false, scale, boldFont))
    {
      GameXgSweepSpec spec;
      spec.goal = goalKind();
      spec.repeats = static_cast<int>(sweepRepeats);
      sweep = std::make_unique<GameXgSweep>(
          *shotDrill, guiView->getController().getStatsConfig(), spec);
      sweepNote.clear();
    }
    ImGui::TextDisabled(
        "The Shot drill at an empty goal, both sides: spots every 1 m to "
        "18 m out (20 m either side), 2 m to 30 m, 4 m beyond; Shooting 1, "
        "25, 50, 75 and 100. About 4-5 minutes at 60 shots a point. Saves "
        "to assets/config/game_xg/ when done.");
  }
  if (!sweepNote.empty())
  {
    ImGui::TextUnformatted(sweepNote.c_str());
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
    if (layer == GAME_LAYER)
    {
      return std::max(spot.game, 0.0F);
    }
    return header ? spot.xg.xg * MatchTuning::Shooting::HEADER_XG_FACTOR
                  : spot.xg.xg;
  };
  const bool gameShown = layer == GAME_LAYER && gameTable.has_value();
  const int firstColumn = static_cast<int>((half - originX) / cell);
  for (int row = 0; row < rows; ++row)
  {
    for (int column = firstColumn; column < columns; ++column)
    {
      const Spot& spot = grid[(static_cast<std::size_t>(row) *
                               static_cast<std::size_t>(columns)) +
                              static_cast<std::size_t>(column)];
      ImU32 colour = 0;
      if (layer == XG_LAYER)
      {
        colour = xgColour(valueOf(spot));
      }
      else if (layer == GAME_LAYER)
      {
        colour = gameShown ? xgColour(valueOf(spot), GAME_FULL_COLOUR) : 0;
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
      const float x0 = std::max(edgeX(column), half);
      const float y0 = std::max(edgeY(row), 0.0F);
      const float x1 = std::min(edgeX(column + 1), built.length);
      const float y1 = std::min(edgeY(row + 1), built.width);
      draw->AddRectFilled(screen(x1, y0), screen(x0, y1), colour);
    }
  }
  // A hairline grid at the cells' edges.
  constexpr ImU32 GRID = IM_COL32(255, 255, 255, 22);
  for (int column = firstColumn; column <= columns; ++column)
  {
    const float x = std::clamp(edgeX(column), half, built.length);
    draw->AddLine(screen(x, 0.0F), screen(x, built.width), GRID, 1.0F);
  }
  for (int row = 0; row <= rows; ++row)
  {
    const float y = std::clamp(edgeY(row), 0.0F, built.width);
    draw->AddLine(screen(half, y), screen(built.length, y), GRID, 1.0F);
  }
  // Contours: an edge between two cells either side of a contour value.
  if (layer == XG_LAYER || gameShown)
  {
    const auto& levels = layer == XG_LAYER ? CONTOURS : GAME_CONTOURS;
    for (int row = 0; row < rows; ++row)
    {
      for (int column = firstColumn; column < columns; ++column)
      {
        const float here = valueOf(grid[(static_cast<std::size_t>(row) *
                                         static_cast<std::size_t>(columns)) +
                                        static_cast<std::size_t>(column)]);
        const float x0 = std::max(edgeX(column), half);
        const float y0 = std::max(edgeY(row), 0.0F);
        const float x1 = std::min(edgeX(column + 1), built.length);
        const float y1 = std::min(edgeY(row + 1), built.width);
        for (const float level : levels)
        {
          if (column + 1 < columns &&
              (here < level) !=
                  (valueOf(grid[(static_cast<std::size_t>(row) *
                                 static_cast<std::size_t>(columns)) +
                                static_cast<std::size_t>(column) + 1]) < level))
          {
            draw->AddLine(screen(x1, y0), screen(x1, y1),
                          IM_COL32(255, 255, 255, 210), CONTOUR_THICKNESS);
          }
          if (row + 1 < rows &&
              (here < level) !=
                  (valueOf(grid[((static_cast<std::size_t>(row) + 1) *
                                 static_cast<std::size_t>(columns)) +
                                static_cast<std::size_t>(column)]) < level))
          {
            draw->AddLine(screen(x0, y1), screen(x1, y1),
                          IM_COL32(255, 255, 255, 210), CONTOUR_THICKNESS);
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
    draw->AddCircleFilled(
        spot, std::max(MIN_SPOT_RADIUS, SPOT_RADIUS_METRES * ppm), WHITE);
    // The arc outside the penalty area and the halfway circle (9.15 m).
    const float radius = CIRCLE_RADIUS_METRES * ppm;
    const float beyond = M::PENALTY_AREA_DEPTH_METRES - M::PENALTY_SPOT_METRES;
    const float arcHalf =
        std::acos(std::clamp(beyond / CIRCLE_RADIUS_METRES, -1.0F, 1.0F));
    draw->PathArcTo(spot, radius, QUARTER_TURN - arcHalf,
                    QUARTER_TURN + arcHalf, PENALTY_ARC_SEGMENTS);
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
                  WALL_LINES * line);
    draw->AddLine(screen(built.length, built.width), screen(half, built.width),
                  WALL, WALL_LINES * line);
    draw->AddLine(screen(built.length, 0.0F),
                  screen(built.length, middle - gap), WALL, WALL_LINES * line);
    draw->AddLine(screen(built.length, middle + gap),
                  screen(built.length, built.width), WALL, WALL_LINES * line);
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

  // A sweep in progress for this goal: a dot at each spot it measures
  // (faint: waiting; lime: being measured; white: measured).
  if (sweep && !sweep->done() && sweep->spec().goal == goalKind())
  {
    const float dot = std::max(MIN_SWEEP_DOT_RADIUS, SWEEP_DOT_METRES * ppm);
    const float middle = MIDDLE_SHARE * built.width;
    for (const GameXgSweep::SweepSpot& spot : sweep->spots())
    {
      const PitchPoint at{built.length - spot.line, middle + spot.side};
      if (at.x < half || at.y < 0.0F || at.y > built.width)
      {
        continue;
      }
      ImU32 colour = Page::LIME;
      if (spot.state == GameXgSweep::SpotState::MEASURED)
      {
        colour = SWEEP_DOT_DONE;
      }
      else if (spot.state == GameXgSweep::SpotState::WAITING)
      {
        colour = SWEEP_DOT_WAITING;
      }
      draw->AddCircleFilled(screen(at.x, at.y), dot, colour);
    }
  }

  // The spot under the mouse; the last one stays when the mouse leaves (the
  // panel's card reads it), unless it is off a pitch made smaller since.
  if (hovered && (hovered->x < half || hovered->x > built.length ||
                  hovered->y > built.width))
  {
    hovered.reset();
  }
  if (hovering)
  {
    const ImVec2 mouse = ImGui::GetMousePos();
    const PitchPoint at{built.length - ((mouse.y - top) / ppm),
                        (mouse.x - left) / ppm};
    if (at.x >= half && at.x <= built.length && at.y >= 0.0F &&
        at.y <= built.width)
    {
      hovered = at;
    }
  }
  if (hovered)
  {
    draw->AddCircle(screen(hovered->x, hovered->y), MOUSE_RING_RADIUS * scale,
                    IM_COL32(255, 255, 255, 230), 0, MOUSE_RING_THICKNESS);
  }
  // The game map's shooter, in the corner: its left-right lean is his foot.
  if (gameShown)
  {
    const std::string flag = std::format(
        "{}-FOOTED SHOOTER", gameTable->foot == "left" ? "LEFT" : "RIGHT");
    const ImVec2 at(origin.x + (FLAG_INSET * scale),
                    origin.y + (FLAG_INSET * scale));
    const ImVec2 size = ImGui::CalcTextSize(flag.c_str());
    draw->AddRectFilled(
        ImVec2(at.x - (FLAG_PADDING * scale), at.y - (FLAG_PADDING * scale)),
        ImVec2(at.x + size.x + (FLAG_PADDING * scale),
               at.y + size.y + (FLAG_PADDING * scale)),
        Page::DARK);
    draw->AddText(at, Page::LIME, flag.c_str());
  }
  draw->PopClipRect();
  Page::endCard(scale);
}
