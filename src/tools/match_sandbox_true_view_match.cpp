// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_true_view_match.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

#include "gui/render/imatch_renderer.h"
#include "gui/render/match_kit_colors.h"
#include "gui/render/match_render_math.h"
#include "gui/render/match_render_snapshot.h"
#include "gui/widgets/theme.h"
#include "model/match_body_shape_binturong.h"
#include "model/match_derived_stats.h"
#include "model/match_engine.h"
#include "model/player.h"
#include "tools/match_sandbox_page_ui.h"
#include "tools/match_sandbox_text.h"

namespace
{
using RenderMath::Vec3;
using P = MatchTuning::Pitch;

/** A click picks the player whose centre is within this many pixels. */
constexpr float PERCENT = 100.0F;
constexpr float LAYER_COLUMNS_WIDTH = 360.0F;
constexpr float LABEL_TOP = 8.0F;
constexpr float LABEL_LEFT = 6.0F;
constexpr float LABEL_VERTICAL_PADDING = 2.0F;
constexpr float LABEL_RIGHT = 14.0F;
constexpr float LABEL_ROUNDING = 3.0F;
constexpr float LABEL_TEXT_LEFT = 10.0F;
constexpr float LABEL_LINE_GAP = 6.0F;
constexpr float PICK_PIXELS = 40.0F;
/** A stride rolled for a shot shows this long (ball-clock seconds). */
constexpr float LEG_GHOST_SECONDS = 1.5F;

/** An engine angle (a true angle on the ground) as a compass angle in the
 * engine's metric frame. */
float compassOf(float engineAngle)
{
  return std::atan2(std::cos(engineAngle), std::sin(engineAngle));
}

/** "Home Name" for a MatchPlayer or a MatchRenderPlayer. */
template <typename Shown>
std::string nameOf(const Shown& player)
{
  const std::string name = player.player ? player.player->getName() : "?";
  return std::format("{} {}", player.isHomeTeam ? "Home" : "Away", name);
}

/** What he is doing that the shapes alone don't show. */
std::string stateOf(const MatchPlayer& player)
{
  std::string state;
  const auto add = [&state](const std::string& part)
  { state += state.empty() ? part : ", " + part; };
  if (player.fallen)
  {
    add(player.gettingUp
            ? std::format("getting up {:.0f}%", player.getUpShare() * PERCENT)
            : std::format("down {:.1f} s", player.downSeconds));
  }
  if (player.airborne())
  {
    add(std::format("in the air, feet {:.2f} m ({} jump)", player.feetHeight,
                    player.runningJump ? "running" : "standing"));
  }
  if (player.landingSeconds > 0.0F)
  {
    add(std::format("landing {:.2f} s", player.landingSeconds));
  }
  if (player.stumbleSeconds > 0.0F)
  {
    add(std::format("stumbling {:.2f} s", player.stumbleSeconds));
  }
  if (player.collisionPause > 0.0F)
  {
    add(std::format("knocked, coasting {:.2f} s", player.collisionPause));
  }
  if (player.plantedSeconds > 0.0F)
  {
    add(std::format("planted {:.2f} s", player.plantedSeconds));
  }
  return state.empty() ? "on his feet" : state;
}
}  // namespace

void MatchTrueView::renderSettings(const MatchEngine& engine)
{
  const float width = ImGui::GetContentRegionAvail().x;
  ImGui::PushItemWidth(-1.0F);
  renderPickedPlayers(engine);
  ImGui::PushTextWrapPos(0);
  SandboxText::disabledText(
      "Click a player: A. Ctrl+click: B. Double-click: look there.");
  ImGui::PopTextWrapPos();

  ImGui::SeparatorText("Technique");
  int techniqueIndex = static_cast<int>(technique);
  if (SandboxPageUI::combo("##technique", &techniqueIndex,
                           TrueView::TECHNIQUE_NAMES.data(),
                           static_cast<int>(TrueView::TECHNIQUE_NAMES.size())))
  {
    technique = static_cast<TrueView::Technique>(techniqueIndex);
  }
  if (technique == TrueView::Technique::FOOT_STRIKE)
  {
    std::array<const char*, STRIKE_KIND_COUNT> kinds{};
    for (std::size_t kind = 0; kind < STRIKE_KIND_COUNT; ++kind)
    {
      kinds.at(kind) = STRIKE_KIND_NAMES.at(kind).data();
    }
    SandboxPageUI::combo("##strike", &strikeKind, kinds.data(),
                         static_cast<int>(kinds.size()));
  }
  ImGui::PopItemWidth();

  ImGui::SeparatorText("Layers (A and B)");
  const float half = width * 0.5F;
  ImGui::Checkbox("Body lines", &layers.lines);
  if (width >= LAYER_COLUMNS_WIDTH * Theme::scale())
  {
    ImGui::SameLine(half);
  }
  ImGui::Checkbox("Feet", &layers.feet);
  ImGui::Checkbox("Thigh, chest", &layers.thighChest);
  if (width >= LAYER_COLUMNS_WIDTH * Theme::scale())
  {
    ImGui::SameLine(half);
  }
  ImGui::Checkbox("Send cones", &layers.sendCones);
  ImGui::Checkbox("Head", &layers.head);
  if (width >= LAYER_COLUMNS_WIDTH * Theme::scale())
  {
    ImGui::SameLine(half);
  }
  ImGui::Checkbox("Technique", &layers.technique);
  ImGui::Checkbox("Personal space", &layers.personalSpace);
  if (width >= LAYER_COLUMNS_WIDTH * Theme::scale())
  {
    ImGui::SameLine(half);
  }
  ImGui::Checkbox("Facing, foot", &layers.facing);
  ImGui::Checkbox("Labels", &layers.labels);

  if (ImGui::TreeNode("Notes"))
  {
    ImGui::PushTextWrapPos(0.0F);
    SandboxText::disabledText(
        "Every player: his body cylinder and feet reach; A and B: everything. "
        "Goalkeepers and Aardvark-body sides: cylinders only. The shapes are "
        "drawn over the stadium and each other (no depth sorting yet).");
    SandboxText::disabledText(
        "Drawn in the engine's own frame, so its handedness is the engine's. "
        "To check: the engine's \"his right\" is clockwise in its pitch "
        "frame, which the 3D view mirrors, so the weaker-foot side may show "
        "on his physical right here.");
    ImGui::PopTextWrapPos();
    ImGui::TreePop();
  }
  if (picked.at(0) != 0 &&
      ImGui::TreeNode("A's shapes, and where they come from"))
  {
    TrueView::renderTable(entries);
    ImGui::TreePop();
  }
}

PlayerID MatchTrueView::playerAt(ImVec2 pixel) const
{
  PlayerID best = 0;
  float bestDistance = PICK_PIXELS * Theme::scale();
  for (const auto& [id, at] : onScreen)
  {
    const float distance = std::hypot(at.x - pixel.x, at.y - pixel.y);
    if (distance < bestDistance)
    {
      bestDistance = distance;
      best = id;
    }
  }
  return best;
}

PlayerID MatchTrueView::drawOverlay(const IMatchRenderer& renderer,
                                    const MatchEngine& engine,
                                    const MatchRenderSnapshot& snapshot,
                                    bool clicked, bool ctrl, ImVec2 pixel)
{
  const TrueView::Tuning tuning = TrueView::Tuning::from(
      engine.getFirstTouchKnobs(), engine.getStrikeKnobs(),
      engine.getStretchMap(), static_cast<StrikeKind>(strikeKind));
  TrueView::Painter paint;
  paint.draw = ImGui::GetWindowDrawList();
  // The engine's metric frame, through the view's own projection.
  paint.project = [&renderer](Vec3 point, ImVec2& out)
  {
    float x = 0.0F;
    float y = 0.0F;
    if (!renderer.projectPitch(PitchPoint{point.x, point.y}, point.z, x, y))
    {
      return false;
    }
    out = {x, y};
    return true;
  };
  const OverlayBodies prepared = prepareBodies(paint, engine, snapshot);
  const auto& bodies = prepared.bodies;
  const auto& drawn = prepared.drawn;
  const auto& players = engine.getPlayers();
  // A click picks before drawing, so the pick shows at once.
  PlayerID clickedPlayer = 0;
  if (clicked)
  {
    clickedPlayer = playerAt(pixel);
    if (clickedPlayer != 0)
    {
      picked.at(ctrl ? 1 : 0) = clickedPlayer;
    }
  }
  // Everyone in outline, then the two picked players over them.
  std::vector<TrueView::Label> labels;
  for (std::size_t index = 0; index < players.size(); ++index)
  {
    if (drawn[index] && pickedSlot(players[index]) < 0)
    {
      TrueView::drawBody(paint, bodies[index], tuning, layers, technique,
                         TrueView::Detail::OUTLINE, nullptr, nullptr);
    }
  }
  for (std::size_t index = 0; index < players.size(); ++index)
  {
    if (drawn[index] && pickedSlot(players[index]) >= 0)
    {
      TrueView::drawBody(paint, bodies[index], tuning, layers, technique,
                         TrueView::Detail::FULL, &labels,
                         pickedSlot(players[index]) == 0 ? &entries : nullptr);
    }
  }
  drawLegGhosts(paint, engine, prepared, labels);
  TrueView::drawLabels(paint, labels);

  drawPlayerLabels(paint, engine, snapshot);
  return clickedPlayer;
}

void MatchTrueView::renderPickedPlayers(const MatchEngine& engine)
{
  ImGui::SeparatorText("Players in full");
  for (std::size_t slot = 0; slot < picked.size(); ++slot)
  {
    renderPlayerPicker(engine, slot);
  }
}

void MatchTrueView::renderPlayerPicker(const MatchEngine& engine,
                                       std::size_t slot)
{
  ImGui::PushID(static_cast<int>(slot));
  std::string current = "None";
  for (const MatchPlayer& player : engine.getPlayers())
  {
    if ((player.player != nullptr) && player.player->getId() == picked.at(slot))
    {
      current = nameOf(player);
    }
  }
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(slot == 0 ? "A" : "B");
  ImGui::SameLine();
  if (SandboxPageUI::beginCombo("##pick", current.c_str()))
  {
    if (ImGui::Selectable("None", picked.at(slot) == 0))
    {
      picked.at(slot) = 0;
    }
    for (const MatchPlayer& player : engine.getPlayers())
    {
      if ((player.player == nullptr) || !player.onPitch)
      {
        continue;
      }
      const PlayerID id = player.player->getId();
      if (ImGui::Selectable(nameOf(player).c_str(), picked.at(slot) == id))
      {
        picked.at(slot) = id;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
}

MatchTrueView::OverlayBodies MatchTrueView::prepareBodies(
    const TrueView::Painter& paint, const MatchEngine& engine,
    const MatchRenderSnapshot& snapshot)
{
  const float alpha = snapshot.interpolationAlpha;
  const auto& players = engine.getPlayers();
  // The strips the views draw, so the sides read as they do in 2D and 3D.
  const MatchKits kits = chooseMatchKits(snapshot.homeTeam, snapshot.awayTeam);
  onScreen.clear();
  entries.clear();
  OverlayBodies prepared{.bodies = std::vector<TrueView::Body>(players.size()),
                         .drawn = std::vector<bool>(players.size(), false)};
  auto& bodies = prepared.bodies;
  auto& drawn = prepared.drawn;
  for (std::size_t index = 0;
       index < snapshot.players.size() && index < players.size(); ++index)
  {
    const MatchRenderPlayer& shown = snapshot.players[index];
    const MatchPlayer& player = players[index];
    if (!shown.onPitch)
    {
      continue;
    }
    const PitchPoint at = lerpRenderPosition(shown.previousPosition,
                                             shown.currentPosition, alpha);
    TrueView::Body& body = bodies[index];
    body.at = {.x = at.x, .y = at.y, .z = 0.0F};
    body.facing = compassOf(RenderMath::lerpAngle(
        shown.previousFacingAngle, shown.currentFacingAngle, alpha));
    body.height = player.heightMetres;
    body.feetHeight = std::max(0.0F, shown.previousFeetHeightMetres +
                                         ((shown.currentFeetHeightMetres -
                                           shown.previousFeetHeightMetres) *
                                          alpha));
    body.airborne = player.airborne();
    body.lying = player.fallen;
    body.lyingAngle = compassOf(player.lyingAngle);
    body.thighOff =
        shown.speedMetresPerSecond > MatchTuning::Ball::THIGH_MAX_SPEED;
    body.rightFooted =
        (player.player == nullptr) || player.player->getFoot() == Foot::Right;
    body.dribbling = player.dribbling;
    body.defending = player.defending;
    body.physicality = player.physicality;
    body.jumpingReach =
        derivedPhysicalValue(DerivedStat::JUMPING_REACH,
                             player.derivedStat(DerivedStat::JUMPING_REACH));
    body.standingRise = jumpRiseOf(body.jumpingReach, body.height, false);
    body.binturong = engine.getBody(player.isHomeTeam) == BodyId::BINTURONG;
    body.goalkeeper = player.isGoalkeeper;
    const auto& outfieldKit = player.isHomeTeam ? kits.home : kits.away;
    const auto& keeperKit =
        player.isHomeTeam ? kits.homeGoalkeeper : kits.awayGoalkeeper;
    body.kit = (player.isGoalkeeper ? keeperKit : outfieldKit).shirt;
    drawn[index] = true;
    const PlayerID id = (player.player != nullptr) ? player.player->getId() : 0;
    ImVec2 centre;
    if (id != 0 &&
        paint.at({.x = body.at.x, .y = body.at.y, .z = 1.0F}, centre))
    {
      onScreen.emplace_back(id, centre);
    }
  }

  return prepared;
}

int MatchTrueView::pickedSlot(const MatchPlayer& player) const
{
  const PlayerID id = (player.player != nullptr) ? player.player->getId() : 0;
  if (id == 0)
  {
    return -1;
  }
  if (id == picked.at(0))
  {
    return 0;
  }
  if (id == picked.at(1))
  {
    return 1;
  }
  return -1;
}

void MatchTrueView::drawLegGhosts(const TrueView::Painter& paint,
                                  const MatchEngine& engine,
                                  const OverlayBodies& prepared,
                                  std::vector<TrueView::Label>& labels) const
{
  const auto& players = engine.getPlayers();
  const auto& drawn = prepared.drawn;
  const auto& bodies = prepared.bodies;
  // Anyone a shot passed lately: his legs as it met them, the stride rolled
  // for it (shot block plan §3c) or a leg put across its line (§3d),
  // fading out.
  for (std::size_t index = 0; index < players.size(); ++index)
  {
    if (!drawn[index])
    {
      continue;
    }
    const auto recent = engine.recentShotLegs(index, LEG_GHOST_SECONDS);
    if (!recent)
    {
      continue;
    }
    TrueView::drawLegGhost(paint, bodies[index], recent->first,
                           1.0F - (recent->second / LEG_GHOST_SECONDS),
                           recent->second, layers.labels ? &labels : nullptr);
  }
}

void MatchTrueView::drawPlayerLabels(const TrueView::Painter& paint,
                                     const MatchEngine& engine,
                                     const MatchRenderSnapshot& snapshot) const
{
  const auto& players = engine.getPlayers();
  // Who they are and what they are doing, top left of the view.
  const ImVec2 clipMin = paint.draw->GetClipRectMin();
  float line = clipMin.y + LABEL_TOP;
  for (std::size_t slot = 0; slot < picked.size(); ++slot)
  {
    for (std::size_t index = 0;
         index < players.size() && index < snapshot.players.size(); ++index)
    {
      const MatchPlayer& player = players[index];
      if (picked.at(slot) == 0 || (player.player == nullptr) ||
          player.player->getId() != picked.at(slot))
      {
        continue;
      }
      const std::string text =
          std::format("{}: {} - {}", slot == 0 ? "A" : "B",
                      nameOf(snapshot.players[index]), stateOf(player));
      const ImVec2 extent = ImGui::CalcTextSize(text.c_str());
      paint.draw->AddRectFilled(
          ImVec2(clipMin.x + LABEL_LEFT, line - LABEL_VERTICAL_PADDING),
          ImVec2(clipMin.x + extent.x + LABEL_RIGHT,
                 line + extent.y + LABEL_VERTICAL_PADDING),
          IM_COL32(10, 12, 16, 190), LABEL_ROUNDING);
      paint.draw->AddText(ImVec2(clipMin.x + LABEL_TEXT_LEFT, line),
                          IM_COL32(240, 240, 240, 255), text.c_str());
      line += extent.y + LABEL_LINE_GAP;
    }
  }
}
