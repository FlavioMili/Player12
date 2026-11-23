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
#include "gui/widgets/text.h"
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
/** The sweep card's third layer: the leg chances (the scan view). */
constexpr int LEGS_SWEEP = 2;
/** Watched slowly: one leg sweep run every this many frames. */
constexpr int WATCH_FRAMES = 20;
/** The leg sweep's shots-a-cell limits. */
constexpr float MIN_LEG_REPEATS = 10.0F;
constexpr float MAX_LEG_REPEATS = 300.0F;
constexpr double SECONDS_PER_MINUTE = 60.0;
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
    : GUIScene(guiView_ptr),
      drills(makeDrills()),
      shotDrill(gameXgDrill(drills)),
      blockDrill(gameXgLegDrill(drills))
{
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
  keeperTable.reset();
  if (*gameTableGoal == GoalKind::NONE)
  {
    gameTableNote = "No goal on this pitch.";
    return;
  }
  std::string problem;
  gameTable = GameXgTable::load(
      GameXg::file(GameXgTable::EMPTY_GOAL, *gameTableGoal), problem);
  keeperTable = GameXgTable::load(
      GameXg::file(GameXgTable::KEEPER, *gameTableGoal), problem);
  const auto about = [](const GameXgTable& table)
  {
    return std::format(
        "measured {} with a {}-footed shooter: {} shots a point, {} points; "
        "left and right {}.",
        table.measured, table.foot, table.repeats, table.samples.size(),
        table.mirrored ? "folded together" : "kept apart");
  };
  const std::string goal = goalKindName(*gameTableGoal);
  gameTableNote =
      (gameTable
           ? "Empty goal: " + about(*gameTable)
           : std::format("No empty-goal map for the {} goal yet.", goal)) +
      "\n" +
      (keeperTable ? "Keeper: " + about(*keeperTable)
                   : std::format("No keeper map for the {} goal yet.", goal));
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

void XgMapScene::updateLegSweep()
{
  if (!legSweep || legSweep->done())
  {
    return;
  }
  if (watchSlowly && ++watchWait < WATCH_FRAMES)
  {
    return;
  }
  watchWait = 0;
  // Measured with Capybara's brain, as the maps are.
  const auto brains = sandboxBrains();
  sandboxBrains() = {BrainId::CAPYBARA, BrainId::CAPYBARA};
  const bool more = legSweep->work(watchSlowly ? 0.0 : SWEEP_SECONDS_PER_FRAME);
  sandboxBrains() = brains;
  if (more || legSweep->stopped())
  {
    return;
  }
  const auto path = GameXg::legFile();
  std::string problem;
  const bool saved = legSweep->table().save(path, problem);
  if (saved)
  {
    // Capybara reads the table from the shared cache: the new one from now.
    GameXg::reload();
  }
  sweepNote = saved ? std::format("Saved {} ({:.0f} s).", path.string(),
                                  legSweep->seconds())
                    : "Not saved: " + problem;
}

void XgMapScene::update(float /*deltaTime*/)
{
  updateLegSweep();
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
  const bool savedTable = table.save(path, problem);
  if (savedTable)
  {
    // The brains read the maps from the shared cache: the new one from now.
    GameXg::reload();
  }
  sweepNote =
      (savedTable ? std::format("Saved {} ({:.0f} s).", path.string(),
                                sweep->seconds())
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
  // off its middle (+ towards the bottom touchline, as the Shot drill);
  // with a keeper, times his factor (as Capybara reads it).
  if (gameTable && on.goal.present())
  {
    const float line = on.length - at.x;
    const float side = at.y - (MIDDLE_SHARE * on.width);
    spot.game = gameTable->value(line, side, shootingSetting);
    if (gameKeeper == 1 && keeperTable)
    {
      spot.game *=
          keeperTable->value(line, side, shootingSetting, goalkeepingSetting);
    }
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
  if (sweepLayer == LEGS_SWEEP)
  {
    renderScan(
        ImVec2(std::max(scale, ImGui::GetContentRegionAvail().x), available.y));
  }
  else
  {
    renderMap(
        ImVec2(std::max(scale, ImGui::GetContentRegionAvail().x), available.y));
  }
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
    UI::textDisabled(
        "Contours at 0.05, 0.10, 0.20 and 0.40. A header shows its xG after "
        "the header discount.");
  }
  else if (layer == SCORE_LAYER)
  {
    UI::textDisabled(
        "Green: the shot's own score is above nothing (worth a shot on its "
        "own; the brain still weighs passing and carrying). Red: below. "
        "Clear: never considered. No role, shout or cutting inside.");
  }
  else
  {
    changed(Page::radio("Empty goal", &gameKeeper, 0));
    ImGui::SameLine();
    changed(Page::radio("Keeper at his set position", &gameKeeper, 1));
    if (gameKeeper == 1)
    {
      ImGui::SetNextItemWidth(CONTROL_WIDTH * scale);
      changed(ImGui::SliderFloat("Goalkeeping", &goalkeepingSetting, 1.0F,
                                 100.0F, "%.0f"));
    }
    UI::textDisabled(
        "Game xG, measured in the Still-ball shot drill (game xG plan): the "
        "chance a still ball struck from the spot goes in, with no keeper, "
        "or times the keeper map's factor for a keeper at his set position "
        "(what Capybara reads). Contours at 0.2, 0.4, 0.6 and 0.8. Shooting "
        "only: no header, defender or pressure yet.");
    if (gameKeeper == 1 && gameTable && !keeperTable)
    {
      ImGui::TextUnformatted(
          "No keeper map for this goal yet: showing the empty goal.");
    }
  }
  ImGui::PopTextWrapPos();
  Page::endCard(scale);
  renderSweep(width);
}

/** The spot's xG, in its parts. */
void XgMapScene::renderXgParts(const Spot& spot) const
{
  using S = MatchTuning::Shooting;
  const MatchEngine::XgBreakdown& x = spot.xg;
  if (!x.goal)
  {
    ImGui::TextUnformatted("No goal on this pitch: xG 0, nobody shoots.");
    return;
  }
  if (!x.inRange)
  {
    UI::text(
        "{:.1f} m from the goal: beyond the fit's {:.0f} m, the floor "
        "{:.3f}.",
        x.metres, S::XG_MAX_DISTANCE_METRES, x.xg);
    return;
  }
  UI::text(
      "{:.1f} m from the goal ({:.1f} m from its line, {:.1f} m off the "
      "middle); "
      "the goal's mouth shows {:.1f} degrees.",
      x.metres, x.lineMetres, x.lateralMetres, x.angle * DEGREES_PER_RADIAN);
  UI::text("Location (the fitted model): {:.3f}", x.location);
  UI::text("x skill {:.2f} (Shooting {:.0f})", x.skillFactor, shootingSetting);
  UI::text("x pressure {:.2f} ({})", x.pressureFactor,
           defender
               ? std::format("a defender {:.1f} m away", defenderMetres).c_str()
               : "nobody near");
  const char* clamp = "";
  if (x.unclamped > S::MAX_OPEN_PLAY_XG)
  {
    clamp = " (capped)";
  }
  else if (x.unclamped < S::MIN_OPEN_PLAY_XG)
  {
    clamp = " (floored)";
  }
  UI::text("= {:.3f}; xG {:.3f}{}", x.unclamped, x.xg, clamp);
}

/** Game xG at the spot: the empty goal's, and the keeper's factor on it. */
void XgMapScene::renderGameXg(const Spot& spot, PitchPoint at) const
{
  if (spot.game < 0.0F || !gameTable)
  {
    UI::textDisabled("Game xG, empty goal: none for this goal yet.");
    return;
  }
  const float line = built.length - at.x;
  const float side = at.y - (MIDDLE_SHARE * built.width);
  const float open = gameTable->value(line, side, shootingSetting);
  ImGui::TextUnformatted(std::format("Game xG, empty goal: {:.3f} (Shooting "
                                     "{:.0f}, "
                                     "{}-footed)",
                                     open, shootingSetting, gameTable->foot)
                             .c_str());
  if (!keeperTable)
  {
    UI::textDisabled("No keeper map for this goal yet.");
    return;
  }
  const float factor =
      keeperTable->value(line, side, shootingSetting, goalkeepingSetting);
  ImGui::TextUnformatted(
      std::format("x keeper at his set position {:.2f} (Goalkeeping "
                  "{:.0f}) = {:.3f}",
                  factor, goalkeepingSetting, open * factor)
          .c_str());
}

/** The shot score at the spot: Capybara's terms or Binturong's parts. */
void XgMapScene::renderShotScore(const Spot& spot, PitchPoint at)
{
  using D = MatchTuning::Decision;
  if (header)
  {
    return;
  }
  const MatchEngine::ShotScoreParts& s = spot.score;
  if (!spot.considered)
  {
    UI::text("Shot score: not considered (xG under {:.3f}).", D::MIN_SHOT_XG);
    return;
  }
  if (capybaraScore())
  {
    // Capybara's own terms (DecisionFrame), as the brain sums them.
    const DecisionFrame::Score shot = capybaraShotCore(built, at, spot.xg.xg);
    ImGui::TextUnformatted(
        std::format("Capybara shot score {:.2f}:", shot.total()).c_str());
    for (const DecisionFrame::Term& term : shot.allTerms())
    {
      ImGui::TextUnformatted(
          std::format("  {}: {:+.2f}", term.name, term.value()).c_str());
    }
    return;
  }
  ImGui::TextUnformatted(
      std::format("Binturong shot score {:.2f}:", s.total).c_str());
  UI::text("  xG above {:.3f}: {:+.2f}", D::BASE_SHOT_THRESHOLD, s.xgTerm);
  UI::text("  have a go (space {:.2f}, range {:.2f}): {:+.2f}", spaceAhead,
           s.rangeEligibility, s.haveAGo);
  UI::text("  final third: {:+.2f}", s.finalThird);
  UI::text("  Shooting: {:+.2f}", s.skill);
  UI::text("  pressure: {:+.2f}", -s.pressure);
  if (s.wideFactor != 1.0F)
  {
    UI::text("  wide angle: x {:.2f} (more than {:.1f} m off the middle)",
             s.wideFactor, D::WIDE_SHOT_WIDTH_METRES);
  }
}

void XgMapScene::renderSpot(float width)
{
  // The spot last pointed at (it stays when the mouse leaves), in its parts.
  namespace Page = SandboxPageUI;
  using S = MatchTuning::Shooting;
  const float scale = Theme::scale();
  Page::beginCard("##xg_map_spot", "AT THE SPOT", width, Page::LIGHT, scale,
                  boldFont);
  ImGui::PushTextWrapPos(0);
  if (!hovered)
  {
    UI::textDisabled(
        "Point at the pitch. The last spot stays, marked, when the mouse "
        "leaves it.");
  }
  else
  {
    const Spot spot = spotAt(built, *hovered);
    UI::text("At {:.1f} m along, {:.1f} m across", hovered->x, hovered->y);
    renderXgParts(spot);
    if (header)
    {
      UI::text(
          "Header: x {:.2f} = {:.3f}; he heads at goal if that reaches {:.2f}: "
          "{}",
          S::HEADER_XG_FACTOR, spot.xg.xg * S::HEADER_XG_FACTOR,
          MatchTuning::Aerial::HEADER_SHOT_MIN_XG,
          spot.considered ? "yes" : "no");
    }
    UI::text("As struck: {:.3f}", spot.struck);
    renderGameXg(spot, *hovered);
    ImGui::Separator();
    renderShotScore(spot, *hovered);
  }
  ImGui::PopTextWrapPos();
  Page::endCard(scale);
}

/** A map sweep running: how far it has got, and a stop button. */
void XgMapScene::renderSweepProgress(float scale)
{
  namespace Page = SandboxPageUI;
  const double share = sweep->runsQueued() > 0
                           ? static_cast<double>(sweep->runsDone()) /
                                 static_cast<double>(sweep->runsQueued())
                           : 0.0;
  ImGui::ProgressBar(static_cast<float>(share),
                     ImVec2(-1.0F, PROGRESS_HEIGHT * scale), "");
  UI::text("{}: {:d} / {:d} runs, {:.0f} s", sweep->phase().c_str(),
           sweep->runsDone(), sweep->runsQueued(), sweep->seconds());
  if (Page::button("Stop", false, scale, boldFont))
  {
    sweep->stop();
    sweepNote = "Stopped: nothing saved.";
  }
}

/** The map sweeps (empty goal or keeper): choose, set and run one. */
void XgMapScene::renderMapSweepControls(float scale)
{
  namespace Page = SandboxPageUI;
  Page::radio("Empty goal", &sweepLayer, 0);
  ImGui::SameLine();
  Page::radio("Keeper", &sweepLayer, 1);
  if (blockDrill != nullptr)
  {
    ImGui::SameLine();
    Page::radio("Legs", &sweepLayer, LEGS_SWEEP);
  }
  ImGui::SetNextItemWidth(CONTROL_WIDTH * scale);
  ImGui::SliderFloat("Shots a point", &sweepRepeats, MIN_SWEEP_REPEATS,
                     MAX_SWEEP_REPEATS, "%.0f");
  if (Page::button("Run the sweep", false, scale, boldFont))
  {
    GameXgSweepSpec spec = gameXgSpec(
        sweepLayer == 1 ? GameXgTable::KEEPER : GameXgTable::EMPTY_GOAL);
    spec.goal = goalKind();
    spec.repeats = static_cast<int>(sweepRepeats);
    sweep = std::make_unique<GameXgSweep>(
        *shotDrill, guiView->getController().getStatsConfig(), spec);
    sweepNote = sweep->problem().empty() ? "" : "Not run: " + sweep->problem();
  }
  UI::textDisabled(
      "{}",
      sweepLayer == 1
          ? "The Still-ball shot drill against a keeper at his set "
            "position, both sides: spots every 1 m to 18 m out (20 m either "
            "side), 2 m to 30 m, 4 m to halfway; Goalkeeping 1, 50 and 100 "
            "by Shooting 1, 50 and 100. Stored as a factor on the empty "
            "goal's map, which must exist. About 6 minutes at 60 shots a "
            "point. Saves to assets/config/game_xg/ when done."
          : "The Still-ball shot drill at an empty goal, both sides: spots "
            "every 1 m to 18 m out (20 m either side), 2 m to 30 m, 4 m to "
            "halfway; Shooting 1, 25, 50, 75 and 100. About 4-5 minutes at "
            "60 shots a point. Saves to assets/config/game_xg/ when done.");
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
  const bool legsRunning = legSweep && !legSweep->done();
  const bool maps = goalKind() != GoalKind::NONE && shotDrill != nullptr;
  if (legsRunning || (!running && sweepLayer == LEGS_SWEEP))
  {
    if (!legsRunning && maps)
    {
      Page::radio("Empty goal", &sweepLayer, 0);
      ImGui::SameLine();
      Page::radio("Keeper", &sweepLayer, 1);
      ImGui::SameLine();
    }
    if (!legsRunning)
    {
      Page::radio("Legs", &sweepLayer, LEGS_SWEEP);
    }
    renderLegSweep(legsRunning);
  }
  else if (running)
  {
    renderSweepProgress(scale);
  }
  else if (maps)
  {
    renderMapSweepControls(scale);
  }
  else if (blockDrill != nullptr)
  {
    // No goal for the maps: the leg chances don't need one.
    Page::radio("Legs", &sweepLayer, LEGS_SWEEP);
  }
  if (!sweepNote.empty())
  {
    ImGui::TextUnformatted(sweepNote.c_str());
  }
  ImGui::PopTextWrapPos();
  Page::endCard(scale);
}

void XgMapScene::renderLegSweep(bool running)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  if (running)
  {
    const long long done = legSweep->runsDone();
    const long long queued = legSweep->runsQueued();
    const double share =
        queued > 0 ? static_cast<double>(done) / static_cast<double>(queued)
                   : 0.0;
    ImGui::ProgressBar(static_cast<float>(share),
                       ImVec2(-1.0F, PROGRESS_HEIGHT * scale), "");
    // Left at the pace so far (watched slowly, the frames set the pace).
    const double left = done > 0 ? legSweep->seconds() *
                                       static_cast<double>(queued - done) /
                                       static_cast<double>(done)
                                 : 0.0;
    const std::string status = std::format(
        "{}: {} / {} runs, {:.0f} s{}",
        legSweep->scattering() ? "Shooter's error" : "Leg chances", done,
        queued, legSweep->seconds(),
        watchSlowly || done == 0 ? std::string()
                                 : std::format(", about {:.0f} min left",
                                               left / SECONDS_PER_MINUTE));
    ImGui::TextUnformatted(status.c_str());
    ImGui::Checkbox("Watch slowly", &watchSlowly);
    if (Page::button("Stop", false, scale, boldFont))
    {
      legSweep->stop();
      sweepNote = "Stopped: nothing saved.";
    }
    return;
  }
  ImGui::SetNextItemWidth(CONTROL_WIDTH * scale);
  ImGui::SliderFloat("Shots a cell", &legRepeats, MIN_LEG_REPEATS,
                     MAX_LEG_REPEATS, "%.0f");
  ImGui::Checkbox("Watch slowly", &watchSlowly);
  if (Page::button("Run the sweep", false, scale, boldFont))
  {
    GameXgLegSweepSpec spec;
    spec.repeats = static_cast<int>(legRepeats);
    legSweep = std::make_unique<GameXgLegSweep>(
        *blockDrill, guiView->getController().getStatsConfig(), spec);
    sweepNote.clear();
  }
  ImGui::PushStyleColor(ImGuiCol_Text,
                        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextUnformatted(
      "The Shot block drill: one defender near a shot's line, an opponent or "
      "a team-mate, standing, sidestepping or sprinting, at Defending and "
      "Tackling 1, 50 and 100, 1 to 25 m out. Counts how often a ball "
      "through his leg-space (below his hip) meets a leg, by how far its "
      "path ran from where he'd have been had he not reacted (0 to 1.5 m); "
      "then the shooter's error at Shooting 1, 50 and 100. Not a map: "
      "Capybara looks these chances up for each body in the way of each "
      "shot. Saves to assets/config/game_xg/legs.json when done.");
  ImGui::PopStyleColor();
}

void XgMapScene::renderScan(ImVec2 area)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##xg_map_scan", "LEG SWEEP: SCAN", area.x, Page::DARK, scale,
                  boldFont);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 size(std::max(scale, ImGui::GetContentRegionAvail().x),
                    std::max(200 * scale, area.y - (56 * scale)));
  ImGui::Dummy(size);
  scanView.draw(legSweep.get(), origin, size, scale);
  Page::endCard(scale);
}

namespace
{
constexpr ImU32 MAP_WHITE = IM_COL32(240, 240, 235, 230);
/** Room round the half: a margin of grass all round (metres). */
constexpr float MAP_MARGIN = 3.0F;
/** Both sides' margins. */
constexpr float MAP_MARGINS = 2.0F * MAP_MARGIN;

/** A match pitch's markings in the attacking half. */
void drawMatchMarkings(ImDrawList* draw, const XgMapFrame& frame,
                       const MatchPitch& built, float line)
{
  using M = MatchTuning::Pitch;
  const float half = frame.half;
  const float middle = 0.5F * built.width;
  const auto box = [&](float deep, float halfWide)
  {
    draw->AddRect(
        frame.screen(PitchPoint{built.length, middle - halfWide}),
        frame.screen(PitchPoint{built.length - deep, middle + halfWide}),
        MAP_WHITE, 0.0F, 0, line);
  };
  draw->AddRect(frame.screen(PitchPoint{built.length, 0.0F}),
                frame.screen(PitchPoint{half, built.width}), MAP_WHITE, 0.0F, 0,
                line);
  box(M::PENALTY_AREA_DEPTH_METRES, M::PENALTY_AREA_HALF_WIDTH_METRES);
  box(MatchTuning::DeadBall::GOAL_AREA_DEPTH_METRES,
      M::GOAL_AREA_HALF_WIDTH_METRES);
  const ImVec2 spot =
      frame.screen(PitchPoint{built.length - M::PENALTY_SPOT_METRES, middle});
  draw->AddCircleFilled(
      spot, std::max(MIN_SPOT_RADIUS, SPOT_RADIUS_METRES * frame.ppm),
      MAP_WHITE);
  // The arc outside the penalty area and the halfway circle (9.15 m).
  const float radius = CIRCLE_RADIUS_METRES * frame.ppm;
  const float beyond = M::PENALTY_AREA_DEPTH_METRES - M::PENALTY_SPOT_METRES;
  const float arcHalf =
      std::acos(std::clamp(beyond / CIRCLE_RADIUS_METRES, -1.0F, 1.0F));
  draw->PathArcTo(spot, radius, QUARTER_TURN - arcHalf, QUARTER_TURN + arcHalf,
                  PENALTY_ARC_SEGMENTS);
  draw->PathStroke(MAP_WHITE, 0, line);
  constexpr int ARC_SEGMENTS = 32;
  draw->PathArcTo(frame.screen(PitchPoint{half, middle}), radius,
                  std::numbers::pi_v<float>, 2 * std::numbers::pi_v<float>,
                  ARC_SEGMENTS);
  draw->PathStroke(MAP_WHITE, 0, line);
}

/** A training pitch's walls, open at the goal mouth; the halfway line
 * faint. */
void drawTrainingWalls(ImDrawList* draw, const XgMapFrame& frame,
                       const MatchPitch& built, float line)
{
  constexpr ImU32 WALL = IM_COL32(20, 26, 30, 240);
  const float half = frame.half;
  const float middle = 0.5F * built.width;
  const float gap =
      built.goal.present() ? built.goal.halfWidth + built.goal.post : 0.0F;
  draw->AddLine(frame.screen(PitchPoint{built.length, 0.0F}),
                frame.screen(PitchPoint{half, 0.0F}), WALL, WALL_LINES * line);
  draw->AddLine(frame.screen(PitchPoint{built.length, built.width}),
                frame.screen(PitchPoint{half, built.width}), WALL,
                WALL_LINES * line);
  draw->AddLine(frame.screen(PitchPoint{built.length, 0.0F}),
                frame.screen(PitchPoint{built.length, middle - gap}), WALL,
                WALL_LINES * line);
  draw->AddLine(frame.screen(PitchPoint{built.length, middle + gap}),
                frame.screen(PitchPoint{built.length, built.width}), WALL,
                WALL_LINES * line);
  draw->AddLine(frame.screen(PitchPoint{half, 0.0F}),
                frame.screen(PitchPoint{half, built.width}),
                IM_COL32(240, 240, 235, 120), line);
}

/** The goal: its net behind the line, its frame. */
void drawMapGoal(ImDrawList* draw, const XgMapFrame& frame,
                 const MatchPitch& built)
{
  if (!built.goal.present())
  {
    return;
  }
  const float middle = 0.5F * built.width;
  const float halfWide = built.goal.halfWidth;
  draw->AddRectFilled(
      frame.screen(
          PitchPoint{built.length + built.goal.netDepth, middle - halfWide}),
      frame.screen(PitchPoint{built.length, middle + halfWide}),
      IM_COL32(230, 230, 230, 60));
  const float post = std::max(2.0F, built.goal.post * frame.ppm);
  draw->AddLine(frame.screen(PitchPoint{built.length, middle - halfWide}),
                frame.screen(PitchPoint{built.length, middle + halfWide}),
                IM_COL32(250, 250, 250, 255), post);
}

/** The game map's shooter, in the corner: its left-right lean is his foot. */
void drawShooterFlag(ImDrawList* draw, ImVec2 origin, const std::string& foot,
                     float scale)
{
  namespace Page = SandboxPageUI;
  const std::string flag =
      std::format("{}-FOOTED SHOOTER", foot == "left" ? "LEFT" : "RIGHT");
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
}  // namespace

ImVec2 XgMapFrame::screen(PitchPoint at) const
{
  return {left + (at.y * ppm), top + ((length - at.x) * ppm)};
}

const XgMapScene::Spot& XgMapScene::spotOf(int row, int column) const
{
  return grid[(static_cast<std::size_t>(row) *
               static_cast<std::size_t>(columns)) +
              static_cast<std::size_t>(column)];
}

float XgMapScene::spotValue(const Spot& spot) const
{
  if (layer == GAME_LAYER)
  {
    return std::max(spot.game, 0.0F);
  }
  return header ? spot.xg.xg * MatchTuning::Shooting::HEADER_XG_FACTOR
                : spot.xg.xg;
}

ImU32 XgMapScene::cellColour(const Spot& spot, bool gameShown) const
{
  if (layer == XG_LAYER)
  {
    return xgColour(spotValue(spot));
  }
  if (layer == GAME_LAYER)
  {
    return gameShown ? xgColour(spotValue(spot), GAME_FULL_COLOUR) : 0;
  }
  if (!spot.considered)
  {
    return 0;
  }
  const float total =
      header
          ? (spotValue(spot) - MatchTuning::Aerial::HEADER_SHOT_MIN_XG) * 20.0F
          : spot.scoreTotal;
  const int alpha =
      static_cast<int>(40 + (120 * std::min(1.0F, std::abs(total) / 3.0F)));
  return total > 0.0F ? IM_COL32(40, 200, 80, alpha)
                      : IM_COL32(220, 60, 50, alpha);
}

/** A cell's corners on the pitch, cut to the half. */
std::array<PitchPoint, 2> XgMapScene::cellCorners(int row, int column,
                                                  float half) const
{
  return {PitchPoint{std::max(edgeX(column), half), std::max(edgeY(row), 0.0F)},
          PitchPoint{std::min(edgeX(column + 1), built.length),
                     std::min(edgeY(row + 1), built.width)}};
}

/** The map in cells, and a hairline grid at the cells' edges. */
void XgMapScene::drawMapCells(ImDrawList* draw, const XgMapFrame& frame,
                              bool gameShown) const
{
  const float half = frame.half;
  const int firstColumn = static_cast<int>((half - originX) / cell);
  for (int row = 0; row < rows; ++row)
  {
    for (int column = firstColumn; column < columns; ++column)
    {
      const ImU32 colour = cellColour(spotOf(row, column), gameShown);
      if (colour == 0)
      {
        continue;
      }
      const auto [from, to] = cellCorners(row, column, half);
      draw->AddRectFilled(frame.screen(PitchPoint{to.x, from.y}),
                          frame.screen(PitchPoint{from.x, to.y}), colour);
    }
  }
  constexpr ImU32 GRID = IM_COL32(255, 255, 255, 22);
  for (int column = firstColumn; column <= columns; ++column)
  {
    const float x = std::clamp(edgeX(column), half, built.length);
    draw->AddLine(frame.screen(PitchPoint{x, 0.0F}),
                  frame.screen(PitchPoint{x, built.width}), GRID, 1.0F);
  }
  for (int row = 0; row <= rows; ++row)
  {
    const float y = std::clamp(edgeY(row), 0.0F, built.width);
    draw->AddLine(frame.screen(PitchPoint{half, y}),
                  frame.screen(PitchPoint{built.length, y}), GRID, 1.0F);
  }
}

/** Contours: an edge between two cells either side of a contour value. */
void XgMapScene::drawContours(ImDrawList* draw, const XgMapFrame& frame) const
{
  const auto& levels = layer == XG_LAYER ? CONTOURS : GAME_CONTOURS;
  const int firstColumn = static_cast<int>((frame.half - originX) / cell);
  for (int row = 0; row < rows; ++row)
  {
    for (int column = firstColumn; column < columns; ++column)
    {
      const float here = spotValue(spotOf(row, column));
      const auto [from, to] = cellCorners(row, column, frame.half);
      for (const float level : levels)
      {
        if (column + 1 < columns &&
            (here < level) != (spotValue(spotOf(row, column + 1)) < level))
        {
          draw->AddLine(frame.screen(PitchPoint{to.x, from.y}),
                        frame.screen(PitchPoint{to.x, to.y}),
                        IM_COL32(255, 255, 255, 210), CONTOUR_THICKNESS);
        }
        if (row + 1 < rows &&
            (here < level) != (spotValue(spotOf(row + 1, column)) < level))
        {
          draw->AddLine(frame.screen(PitchPoint{from.x, to.y}),
                        frame.screen(PitchPoint{to.x, to.y}),
                        IM_COL32(255, 255, 255, 210), CONTOUR_THICKNESS);
        }
      }
    }
  }
}

/** A sweep in progress for this goal: a dot at each spot it measures
 * (faint: waiting; lime: being measured; white: measured). */
void XgMapScene::drawSweepDots(ImDrawList* draw, const XgMapFrame& frame) const
{
  namespace Page = SandboxPageUI;
  if (!sweep || sweep->done() || sweep->spec().goal != goalKind())
  {
    return;
  }
  const float dot =
      std::max(MIN_SWEEP_DOT_RADIUS, SWEEP_DOT_METRES * frame.ppm);
  const float middle = MIDDLE_SHARE * built.width;
  for (const GameXgSweep::SweepSpot& spot : sweep->spots())
  {
    const PitchPoint at{built.length - spot.line, middle + spot.side};
    if (at.x < frame.half || at.y < 0.0F || at.y > built.width)
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
    draw->AddCircleFilled(frame.screen(at), dot, colour);
  }
}

/** The spot under the mouse; the last one stays when the mouse leaves (the
 * panel's card reads it), unless it is off a pitch made smaller since. */
void XgMapScene::trackHover(const XgMapFrame& frame, bool hovering)
{
  if (hovered && (hovered->x < frame.half || hovered->x > built.length ||
                  hovered->y > built.width))
  {
    hovered.reset();
  }
  if (!hovering)
  {
    return;
  }
  const ImVec2 mouse = ImGui::GetMousePos();
  const PitchPoint at{built.length - ((mouse.y - frame.top) / frame.ppm),
                      (mouse.x - frame.left) / frame.ppm};
  if (at.x >= frame.half && at.x <= built.length && at.y >= 0.0F &&
      at.y <= built.width)
  {
    hovered = at;
  }
}

void XgMapScene::renderMap(ImVec2 area)
{
  // The attacking half only, turned so its goal is at the top: plain grass,
  // its lines (a match pitch's markings, or a training pitch's walls), its
  // goal, the map in cells with a hairline grid, and the spot under the
  // mouse.
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##xg_map_pitch", "ATTACKING HALF", area.x, Page::DARK, scale,
                  boldFont);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 size(std::max(scale, ImGui::GetContentRegionAvail().x),
                    std::max(200 * scale, area.y - (56 * scale)));
  ImGui::InvisibleButton("##xg_map_area", size);
  const bool hovering = ImGui::IsItemHovered();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                     true);

  XgMapFrame frame;
  frame.half = MIDDLE_SHARE * built.length;
  frame.length = built.length;
  const float depth = built.goal.present() ? built.goal.netDepth : 0.0F;
  // Room round it: the goal's net above, a margin of grass all round.
  frame.ppm = std::min(size.x / (built.width + MAP_MARGINS),
                       size.y / (frame.half + depth + MAP_MARGINS));
  frame.left = origin.x + (MIDDLE_SHARE * (size.x - built.width * frame.ppm));
  frame.top = origin.y +
              (MIDDLE_SHARE * (size.y - (frame.half + depth) * frame.ppm)) +
              (depth * frame.ppm);

  // Plain grass, a shade darker round the pitch.
  draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                      IM_COL32(52, 96, 52, 255));
  draw->AddRectFilled(frame.screen(PitchPoint{built.length, 0.0F}),
                      frame.screen(PitchPoint{frame.half, built.width}),
                      IM_COL32(62, 116, 62, 255));
  const bool gameShown = layer == GAME_LAYER && gameTable.has_value();
  drawMapCells(draw, frame, gameShown);
  if (layer == XG_LAYER || gameShown)
  {
    drawContours(draw, frame);
  }
  // The lines: a match pitch's markings, or a training pitch's walls.
  const float line = std::max(1.0F, 0.12F * frame.ppm);
  if (built.surface == PitchSurface::MATCH)
  {
    drawMatchMarkings(draw, frame, built, line);
  }
  else
  {
    drawTrainingWalls(draw, frame, built, line);
  }
  drawMapGoal(draw, frame, built);
  drawSweepDots(draw, frame);
  trackHover(frame, hovering);
  if (hovered)
  {
    draw->AddCircle(frame.screen(*hovered), MOUSE_RING_RADIUS * scale,
                    IM_COL32(255, 255, 255, 230), 0, MOUSE_RING_THICKNESS);
  }
  if (gameShown)
  {
    drawShooterFlag(draw, origin, gameTable->foot, scale);
  }
  draw->PopClipRect();
  Page::endCard(scale);
}
