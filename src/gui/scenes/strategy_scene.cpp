// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "strategy_scene.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <utility>

#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/lineup.h"
#include "model/player.h"
#include "model/role_utils.h"
#include "model/tactics.h"
#include "model/team.h"

namespace
{
constexpr float PRESET_BUTTON_HEIGHT = 58.0f;
constexpr float SLIDER_LABEL_WIDTH = 170.0f;
constexpr float SUMMARY_MIN_WIDTH = 900.0f;
constexpr float ROLES_TWO_COLUMN_WIDTH = 860.0f;
constexpr float EPSILON = 0.001f;
/** Pitch proportions (length by width) of the previews. */
constexpr float PITCH_ASPECT = 68.0f / 105.0f;
constexpr float SHAPE_EDITOR_MAX_HEIGHT = 420.0f;
constexpr float TOKEN_RADIUS = 9.0f;
constexpr float LINEUP_GOALKEEPER_X = 0.04f;
constexpr float PITCH_LENGTH_METRES = 105.0f;
constexpr float PITCH_WIDTH_METRES = 68.0f;

struct TacticalPreset
{
  const char* nameKey;
  const char* descriptionKey;
  StrategySliders sliders;
};

constexpr std::array<TacticalPreset, 4> PRESETS{{
    {"TACTIC_BALANCED",
     "TACTIC_BALANCED_HELP",
     {0.50f, 0.50f, 0.50f, 0.50f, 0.50f}},
    {"TACTIC_FRONT_FOOT",
     "TACTIC_FRONT_FOOT_HELP",
     {0.82f, 0.72f, 0.76f, 0.68f, 0.64f}},
    {"TACTIC_COUNTER",
     "TACTIC_COUNTER_HELP",
     {0.38f, 0.68f, 0.62f, 0.58f, 0.42f}},
    {"TACTIC_CONTROL",
     "TACTIC_CONTROL_HELP",
     {0.62f, 0.34f, 0.44f, 0.72f, 0.72f}},
}};

struct SliderInfo
{
  const char* key;
  const char* id;
  float StrategySliders::* value;
  const char* helpKey;
};

constexpr std::array<SliderInfo, 5> SLIDERS{{
    {"STRATEGY_PRESSING", "pressing", &StrategySliders::pressing,
     "TACTIC_PRESSING_HELP"},
    {"STRATEGY_RISK_TAKING", "risk", &StrategySliders::riskTaking,
     "TACTIC_RISK_HELP"},
    {"STRATEGY_OFFENSIVE_BIAS", "offensive", &StrategySliders::offensiveBias,
     "TACTIC_OFFENSIVE_HELP"},
    {"STRATEGY_WIDTH_USAGE", "width", &StrategySliders::widthUsage,
     "TACTIC_WIDTH_HELP"},
    {"STRATEGY_COMPACTNESS", "compactness", &StrategySliders::compactness,
     "TACTIC_COMPACTNESS_HELP"},
}};

bool sameSliders(const StrategySliders& left, const StrategySliders& right)
{
  return std::ranges::all_of(
      SLIDERS, [&](const SliderInfo& slider)
      { return std::abs(left.*slider.value - right.*slider.value) < EPSILON; });
}

bool sameSlots(const std::vector<SlotInstruction>& left,
               const std::vector<SlotInstruction>& right)
{
  // Order-insensitive: editing a slot moves it to the back of the list.
  const auto close = [](Vector2F a, Vector2F b)
  { return std::abs(a.x - b.x) < EPSILON && std::abs(a.y - b.y) < EPSILON; };
  return left.size() == right.size() &&
         std::ranges::all_of(
             left,
             [&](const SlotInstruction& a)
             {
               return std::ranges::any_of(
                   right,
                   [&](const SlotInstruction& b)
                   {
                     return close(a.anchor, b.anchor) && a.role == b.role &&
                            a.duty == b.duty &&
                            close(a.possessionOffset, b.possessionOffset);
                   });
             });
}

constexpr std::array<const char*, 5> LEVEL_KEYS = {
    "TACTIC_LEVEL_VERY_LOW", "TACTIC_LEVEL_LOW", "TACTIC_LEVEL_MEDIUM",
    "TACTIC_LEVEL_HIGH", "TACTIC_LEVEL_VERY_HIGH"};

const char* levelKey(float value)
{
  const auto level = static_cast<size_t>(std::clamp(value, 0.0f, 0.999f) *
                                         static_cast<float>(LEVEL_KEYS.size()));
  return LEVEL_KEYS[level];
}

bool tacticSlider(const SliderInfo& slider, float& value)
{
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(LOC(slider.key));
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", LOC(slider.helpKey));
  ImGui::SameLine(SLIDER_LABEL_WIDTH * Theme::scale());
  // The level is written beside the slider, never on top of its handle.
  float levelWidth = 0.0f;
  for (const char* key : LEVEL_KEYS)
    levelWidth = std::max(levelWidth, ImGui::CalcTextSize(LOC(key)).x);
  ImGui::SetNextItemWidth(std::max(
      60.0f * Theme::scale(), ImGui::GetContentRegionAvail().x - levelWidth -
                                  ImGui::GetStyle().ItemSpacing.x));
  const std::string label = std::string("###") + slider.id;
  const bool changed =
      ImGui::SliderFloat(label.c_str(), &value, 0.0f, 1.0f, "");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s (%.0f%%)", LOC(slider.helpKey),
                      static_cast<double>(value * 100.0f));
  ImGui::SameLine();
  ImGui::TextColored(Theme::palette().muted, "%s", LOC(levelKey(value)));
  return changed;
}

void drawPitchBase(ImDrawList* drawList, ImVec2 min, ImVec2 max)
{
  const float scale = Theme::scale();
  drawList->AddRectFilled(min, max, IM_COL32(32, 106, 60, 255), 4.0f * scale);
  const ImU32 line = IM_COL32(255, 255, 255, 110);
  drawList->AddRect(min, max, line, 4.0f * scale, 0, 1.2f * scale);
  const float midX = (min.x + max.x) * 0.5f;
  const float height = max.y - min.y;
  drawList->AddLine(ImVec2(midX, min.y), ImVec2(midX, max.y), line,
                    1.2f * scale);
  drawList->AddCircle(ImVec2(midX, min.y + height * 0.5f), height * 0.16f, line,
                      32, 1.2f * scale);
  const float width = max.x - min.x;
  for (const bool left : {true, false})
  {
    const float edge = left ? min.x : max.x;
    const float box = (left ? 1.0f : -1.0f) * width * 0.157f;
    drawList->AddRect(
        ImVec2(std::min(edge, edge + box), min.y + height * 0.205f),
        ImVec2(std::max(edge, edge + box), min.y + height * 0.795f), line, 0.0f,
        0, 1.2f * scale);
  }
}

// Illustrative shape: the current lineup moved by the instructions (pressing
// and attacking intent push the block up, width spreads it, compactness
// squeezes the lines together).
void shapePreview(const Lineup& lineup, const StrategySliders& sliders,
                  ImVec2 size)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const ImVec2 min = ImGui::GetCursorScreenPos();
  const ImVec2 max(min.x + size.x, min.y + size.y);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawPitchBase(drawList, min, max);

  const auto& outfield = lineup.getOutfieldPlayers();
  float meanX = 0.0f;
  for (const auto& positioned : outfield) meanX += positioned.position.x;
  meanX =
      outfield.empty() ? 0.45f : meanX / static_cast<float>(outfield.size());
  const float shift = 0.14f * (sliders.pressing - 0.5f) +
                      0.10f * (sliders.offensiveBias - 0.5f);
  const float depth = 1.25f - 0.55f * sliders.compactness;
  const float spread = 0.70f + 0.45f * sliders.widthUsage;
  const float radius = 4.5f * scale;
  for (const auto& positioned : outfield)
  {
    const float x = std::clamp(
        meanX + shift + (positioned.position.x - meanX) * depth, 0.04f, 0.96f);
    const float y = std::clamp(0.5f + (positioned.position.y - 0.5f) * spread,
                               0.05f, 0.95f);
    drawList->AddCircleFilled(ImVec2(min.x + x * size.x, min.y + y * size.y),
                              radius, Theme::toU32(palette.accent));
    drawList->AddCircle(ImVec2(min.x + x * size.x, min.y + y * size.y), radius,
                        IM_COL32(255, 255, 255, 200), 0, 1.0f * scale);
  }
  drawList->AddCircleFilled(
      ImVec2(min.x + 0.04f * size.x, min.y + size.y * 0.5f), radius,
      Theme::toU32(palette.warning));
  ImGui::Dummy(size);
}

/** Lineup-coordinate shift of a role with the ball (as the engine plays it). */
Vector2F roleShift(const SlotInstruction& slot)
{
  const RoleProfile profile = Tactics::profile(slot.role, slot.duty);
  const float lateral = slot.anchor.y - 0.5f;
  float flank = 0.0f;
  if (!(std::abs(lateral) < 0.05f)) flank = lateral > 0.0f ? 1.0f : -1.0f;
  return {profile.possessionAdvanceMetres / PITCH_LENGTH_METRES,
          flank * profile.possessionWidthMetres / PITCH_WIDTH_METRES};
}

/** Spot of a slot with the ball: formation, role and shape shift. */
Vector2F possessionSpot(const SlotInstruction& slot)
{
  const Vector2F shift = roleShift(slot);
  return {std::clamp(slot.anchor.x + slot.possessionOffset.x + shift.x, 0.02f,
                     0.98f),
          std::clamp(slot.anchor.y + slot.possessionOffset.y + shift.y, 0.03f,
                     0.97f)};
}

/** Wraps the next item of a flow onto a new line past @p right. */
void flowItem(float itemWidth, float right, bool first)
{
  if (first) return;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  if (ImGui::GetItemRectMax().x + gap + itemWidth <= right)
    ImGui::SameLine(0.0f, gap);
}

float statValue(const Player& player, std::string_view name)
{
  const auto& stats = player.getStats();
  const auto found = stats.find(std::string(name));
  return found == stats.end() ? 0.0f : found->second;
}
}  // namespace

StrategyScene::StrategyScene(GUIView* parent) : ManagementScene(parent) {}

void StrategyScene::update(float deltaTime) { (void)deltaTime; }

void StrategyScene::renderContent()
{
  UI::pageHeader(LOC("TACTIC_IDENTITY"), LOC("TACTIC_IDENTITY_HELP"));
  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  if (available >= SUMMARY_MIN_WIDTH * Theme::scale())
  {
    const float summaryWidth = std::floor((available - gap) * 0.34f);
    renderInstructions(available - gap - summaryWidth);
    ImGui::SameLine();
    renderSummary(summaryWidth);
  }
  else
  {
    renderInstructions(available);
    renderSummary(available);
  }
  renderRoles(available);
}

Strategy* StrategyScene::clubStrategy() const
{
  const auto managed = guiView->getController().getManagedTeam();
  return managed ? &managed->get().getStrategy() : nullptr;
}

const Lineup* StrategyScene::clubLineup() const
{
  const auto managed = guiView->getController().getManagedTeam();
  return managed ? &managed->get().getLineup() : nullptr;
}

void StrategyScene::renderInstructions(float width)
{
  // Sized to content: never clips the sliders, the page scrolls if needed.
  UI::beginAutoHeightCard("tactic_setup", LOC("TACTIC_PRESETS"), width);
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const float buttonWidth =
      (ImGui::GetContentRegionAvail().x - gap * (PRESETS.size() - 1)) /
      static_cast<float>(PRESETS.size());
  for (size_t index = 0; index < PRESETS.size(); ++index)
  {
    if (index > 0) ImGui::SameLine();
    ImGui::PushID(static_cast<int>(index));
    if (const bool active = selected_preset == static_cast<int>(index);
        UI::toggleButton(
            LOC(PRESETS[index].nameKey), active,
            ImVec2(buttonWidth, PRESET_BUTTON_HEIGHT * Theme::scale())))
    {
      current_sliders = PRESETS[index].sliders;
      selected_preset = static_cast<int>(index);
      applySliders();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip("%s", LOC(PRESETS[index].descriptionKey));
    ImGui::PopID();
  }

  ImGui::Spacing();
  const char* description =
      selected_preset >= 0
          ? LOC(PRESETS[static_cast<size_t>(selected_preset)].descriptionKey)
          : LOC("TACTIC_CUSTOM_HELP");
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(Theme::palette().muted, "%s", description);
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  ImGui::SeparatorText(LOC("TACTIC_DETAILS"));

  bool customized = false;
  for (const SliderInfo& slider : SLIDERS)
    customized |= tacticSlider(slider, current_sliders.*slider.value);
  if (customized)
  {
    selected_preset = -1;
    applySliders();
  }

  ImGui::Spacing();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(Theme::palette().faint, "%s", LOC("TACTIC_LIVE_HELP"));
  ImGui::PopTextWrapPos();
  ImGui::BeginDisabled(!changedSinceEntry());
  if (UI::secondaryButton(LOC("TACTIC_REVERT"))) revert();
  ImGui::EndDisabled();
  UI::endCard();
}

void StrategyScene::renderSummary(float width) const
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("tactic_summary", LOC("TACTIC_SUMMARY"), width);
  if (const Lineup* lineup = clubLineup())
  {
    const float previewWidth = ImGui::GetContentRegionAvail().x;
    shapePreview(*lineup, current_sliders,
                 ImVec2(previewWidth, previewWidth / 1.55f));
    ImGui::TextColored(palette.faint, "%s", LOC("TACTIC_SHAPE_PREVIEW"));
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  }
  const float labelWidth = 140.0f * Theme::scale();
  for (const SliderInfo& slider : SLIDERS)
  {
    const float value = current_sliders.*slider.value;
    UI::meter(LOC(slider.key), value, labelWidth, palette.accent,
              LOC(levelKey(value)));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", LOC(slider.helpKey));
  }
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.faint, "%s", LOC("TACTIC_SUMMARY_HELP"));
  ImGui::PopTextWrapPos();
  UI::endCard();
}

void StrategyScene::renderRoles(float width)
{
  const Lineup* lineup = clubLineup();
  if (!lineup) return;
  const auto slots = static_cast<int>(lineup->getOutfieldPlayers().size());
  selected_slot = std::clamp(selected_slot, KEEPER_SLOT, slots - 1);
  UI::beginAutoHeightCard("tactic_roles", LOC("TACTIC_ROLES"), width);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(Theme::palette().muted, "%s", LOC("TACTIC_ROLES_HELP"));
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  const float inner = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x * 2.0f;
  if (inner >= ROLES_TWO_COLUMN_WIDTH * Theme::scale())
  {
    // Shape editor and the XI on the left, the selected slot on the right.
    const float left = std::floor((inner - gap) * 0.56f);
    ImGui::BeginGroup();
    renderShapeEditor(*lineup, left);
    renderShapePresets(*lineup, left);
    ImGui::EndGroup();
    ImGui::SameLine(0.0f, gap);
    ImGui::BeginGroup();
    ImGui::PushItemWidth(inner - left - gap);
    renderSlotDetails(*lineup);
    ImGui::Spacing();
    renderSlotList(*lineup);
    ImGui::PopItemWidth();
    ImGui::EndGroup();
  }
  else
  {
    renderShapeEditor(*lineup, inner);
    renderShapePresets(*lineup, inner);
    ImGui::Spacing();
    ImGui::PushItemWidth(inner);
    renderSlotDetails(*lineup);
    ImGui::Spacing();
    renderSlotList(*lineup);
    ImGui::PopItemWidth();
  }
  UI::endCard();
}

SlotInstruction StrategyScene::slotAt(const Lineup& lineup, int slot) const
{
  const auto& outfield = lineup.getOutfieldPlayers();
  SlotInstruction instruction;
  if (slot < 0 || slot >= static_cast<int>(outfield.size())) return instruction;
  instruction.anchor = outfield[static_cast<std::size_t>(slot)].position;
  if (const Strategy* strategy = clubStrategy())
  {
    if (const SlotInstruction* stored = strategy->findSlot(instruction.anchor))
    {
      instruction.role = stored->role;
      instruction.duty = stored->duty;
      instruction.possessionOffset = stored->possessionOffset;
    }
  }
  // A role of another position group (the slot moved) plays Standard.
  if (!Tactics::allows(Tactics::familyForSlot(instruction.anchor),
                       instruction.role))
    instruction.role = TacticalRole::Standard;
  return instruction;
}

void StrategyScene::storeSlot(const SlotInstruction& instruction) const
{
  if (Strategy* strategy = clubStrategy()) strategy->setSlot(instruction);
}

void StrategyScene::renderShapeEditor(const Lineup& lineup, float width)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  UI::sectionLabel(LOC("TACTIC_SHAPE_EDITOR"));
  const float height =
      std::min(width * PITCH_ASPECT, SHAPE_EDITOR_MAX_HEIGHT * scale);
  const float pitchWidth = height / PITCH_ASPECT;
  const ImVec2 min = ImGui::GetCursorScreenPos();
  const ImVec2 size(pitchWidth, height);
  const ImVec2 max(min.x + size.x, min.y + size.y);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawPitchBase(drawList, min, max);
  const auto toScreen = [&](Vector2F point)
  { return ImVec2(min.x + point.x * size.x, min.y + point.y * size.y); };
  const float radius = TOKEN_RADIUS * scale;
  const ImU32 ring = IM_COL32(255, 255, 255, 150);
  const ImU32 link = IM_COL32(255, 255, 255, 90);
  const ImU32 accent = Theme::toU32(palette.accent);
  const ImU32 selection = Theme::toU32(palette.warning);

  const auto& outfield = lineup.getOutfieldPlayers();
  for (int slot = 0; slot < static_cast<int>(outfield.size()); ++slot)
  {
    const auto& positioned = outfield[static_cast<std::size_t>(slot)];
    if (!positioned.player) continue;
    SlotInstruction instruction = slotAt(lineup, slot);
    const ImVec2 base = toScreen(instruction.anchor);
    ImVec2 spot = toScreen(possessionSpot(instruction));

    // Drag the filled token to set the spot with the ball.
    ImGui::SetCursorScreenPos(ImVec2(spot.x - radius, spot.y - radius));
    ImGui::PushID(slot);
    ImGui::InvisibleButton("##slot", ImVec2(radius * 2.0f, radius * 2.0f));
    if (ImGui::IsItemActivated())
    {
      selected_slot = slot;
      dragging_slot = slot;
    }
    if (ImGui::IsItemActive() && dragging_slot == slot &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f))
    {
      const ImVec2 mouse = ImGui::GetIO().MousePos;
      const Vector2F wanted{
          std::clamp((mouse.x - min.x) / size.x, 0.02f, 0.98f),
          std::clamp((mouse.y - min.y) / size.y, 0.03f, 0.97f)};
      const Vector2F shift = roleShift(instruction);
      instruction.possessionOffset = {
          wanted.x - instruction.anchor.x - shift.x,
          wanted.y - instruction.anchor.y - shift.y};
      storeSlot(instruction);
      spot = toScreen(possessionSpot(instruction));
    }
    if (ImGui::IsItemDeactivated()) dragging_slot = -1;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && dragging_slot < 0)
      ImGui::SetTooltip(
          "%s\n%s  ·  %s\n%s", positioned.player->getName().c_str(),
          LOC(Tactics::roleKey(instruction.role)),
          LOC(Tactics::dutyKey(instruction.duty)), LOC("TACTIC_DRAG_HINT"));
    ImGui::PopID();

    drawList->AddCircle(base, radius * 0.7f, ring, 0, 1.2f * scale);
    drawList->AddLine(base, spot, link, 1.5f * scale);
    drawList->AddCircleFilled(spot, radius, accent);
    drawList->AddCircle(
        spot, radius,
        slot == selected_slot ? selection : IM_COL32(255, 255, 255, 220), 0,
        (slot == selected_slot ? 2.5f : 1.2f) * scale);
    const std::string number = std::to_string(slot + 1);
    const ImVec2 textSize = ImGui::CalcTextSize(number.c_str());
    drawList->AddText(
        ImVec2(spot.x - textSize.x * 0.5f, spot.y - textSize.y * 0.5f),
        Theme::toU32(palette.on_accent), number.c_str());
  }

  // The keeper takes roles only (no shape).
  if (lineup.getGoalkeeper())
  {
    const ImVec2 keeper = toScreen({LINEUP_GOALKEEPER_X, 0.5f});
    ImGui::SetCursorScreenPos(ImVec2(keeper.x - radius, keeper.y - radius));
    if (ImGui::InvisibleButton("##keeper",
                               ImVec2(radius * 2.0f, radius * 2.0f)))
      selected_slot = KEEPER_SLOT;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip("%s", lineup.getGoalkeeper()->getName().c_str());
    drawList->AddCircleFilled(keeper, radius, Theme::toU32(palette.raised));
    drawList->AddCircle(
        keeper, radius,
        selected_slot == KEEPER_SLOT ? selection : IM_COL32(255, 255, 255, 220),
        0, (selected_slot == KEEPER_SLOT ? 2.5f : 1.2f) * scale);
  }
  ImGui::SetCursorScreenPos(min);
  ImGui::Dummy(size);
  ImGui::PushTextWrapPos(min.x + width - ImGui::GetWindowPos().x);
  ImGui::TextColored(palette.faint, "%s", LOC("TACTIC_SHAPE_EDITOR_HELP"));
  ImGui::PopTextWrapPos();
}

void StrategyScene::renderShapePresets(const Lineup& lineup, float width)
{
  UI::sectionLabel(LOC("TACTIC_SHAPE_PRESETS"));
  const float right = ImGui::GetCursorScreenPos().x + width;
  const auto& outfield = lineup.getOutfieldPlayers();
  bool first = true;
  for (int index = 0;
       index < static_cast<int>(std::to_underlying(PossessionShape::COUNT));
       ++index)
  {
    const auto shape = static_cast<PossessionShape>(index);
    // Active while every slot sits where the shape puts it.
    bool active = !outfield.empty();
    for (int slot = 0; slot < static_cast<int>(outfield.size()); ++slot)
    {
      const SlotInstruction instruction = slotAt(lineup, slot);
      const Vector2F wanted =
          Tactics::possessionOffset(shape, instruction.anchor);
      if (!(std::abs(instruction.possessionOffset.x - wanted.x) < EPSILON &&
            std::abs(instruction.possessionOffset.y - wanted.y) < EPSILON))
      {
        active = false;
        break;
      }
    }
    const char* label = LOC(Tactics::possessionShapeKey(shape));
    flowItem(UI::buttonWidth(label, UI::ButtonSize::COMPACT), right, first);
    first = false;
    ImGui::PushID(index);
    if (UI::toggleButton(label, active, ImVec2(0.0f, 0.0f),
                         UI::ButtonSize::COMPACT))
      applyShape(lineup, shape);
    ImGui::PopID();
  }
  const char* suggest = LOC("TACTIC_SUGGEST_ROLES");
  flowItem(UI::buttonWidth(suggest, UI::ButtonSize::COMPACT), right, first);
  if (UI::secondaryButton(suggest, ImVec2(0.0f, 0.0f), UI::ButtonSize::COMPACT))
    suggestRoles(lineup);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("%s", LOC("TACTIC_SUGGEST_ROLES_HELP"));
}

void StrategyScene::renderSlotDetails(const Lineup& lineup)
{
  const Theme::Palette& palette = Theme::palette();
  const bool keeper = selected_slot == KEEPER_SLOT;
  const Player* player = nullptr;
  if (keeper)
    player = lineup.getGoalkeeper();
  else if (selected_slot >= 0 &&
           selected_slot < static_cast<int>(lineup.getOutfieldPlayers().size()))
    player =
        lineup.getOutfieldPlayers()[static_cast<std::size_t>(selected_slot)]
            .player;
  const Strategy* strategy = clubStrategy();
  if (!player || !strategy)
  {
    ImGui::TextColored(palette.muted, "%s", LOC("TACTIC_SELECT_SLOT"));
    return;
  }
  const SlotInstruction instruction =
      keeper ? SlotInstruction{} : slotAt(lineup, selected_slot);
  const RoleFamily family = keeper ? RoleFamily::Goalkeeper
                                   : Tactics::familyForSlot(instruction.anchor);
  const TacticalRole role =
      keeper ? strategy->getKeeperRole() : instruction.role;

  UI::sectionLabel(LOC(Tactics::familyKey(family)));
  ImGui::TextUnformatted(
      keeper ? player->getName().c_str()
             : std::format("{}. {}", selected_slot + 1, player->getName())
                   .c_str());

  // Role choice: one button per role of the position group.
  const float right = ImGui::GetCursorScreenPos().x + ImGui::CalcItemWidth();
  bool first = true;
  for (const TacticalRole option : Tactics::rolesFor(family))
  {
    const char* label = LOC(Tactics::roleKey(option));
    flowItem(UI::buttonWidth(label, UI::ButtonSize::COMPACT), right, first);
    first = false;
    ImGui::PushID(static_cast<int>(std::to_underlying(option)));
    if (UI::toggleButton(label, option == role, ImVec2(0.0f, 0.0f),
                         UI::ButtonSize::COMPACT))
      setRole(lineup, option);
    ImGui::PopID();
  }
  if (Tactics::hasDuty(family))
  {
    ImGui::Spacing();
    std::array<const char*,
               static_cast<std::size_t>(std::to_underlying(RoleDuty::COUNT))>
        duties{};
    for (std::size_t index = 0; index < duties.size(); ++index)
      duties[index] = LOC(Tactics::dutyKey(static_cast<RoleDuty>(index)));
    auto duty = static_cast<int>(std::to_underlying(instruction.duty));
    if (UI::segmented("##duty", duty, duties, ImGui::CalcItemWidth()))
      setDuty(lineup, static_cast<RoleDuty>(duty));
  }
  ImGui::Spacing();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::CalcItemWidth());
  ImGui::TextColored(palette.muted, "%s",
                     LOC(Tactics::roleDescriptionKey(role)));
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  ImGui::TextColored(palette.faint, "%s", LOC("TACTIC_KEY_ATTRIBUTES"));
  const float labelWidth = 120.0f * Theme::scale();
  for (const std::string_view name : Tactics::keyAttributes(family, role))
  {
    const std::string key = "STAT_" + std::string(name);
    UI::attributeBar(LOC(key.c_str()), statValue(*player, name), labelWidth,
                     100.0f, ImGui::CalcItemWidth());
  }
  if (!keeper)
  {
    const bool moved = std::abs(instruction.possessionOffset.x) > EPSILON ||
                       std::abs(instruction.possessionOffset.y) > EPSILON;
    ImGui::BeginDisabled(!moved);
    if (UI::secondaryButton(LOC("TACTIC_RESET_SPOT"), ImVec2(0.0f, 0.0f),
                            UI::ButtonSize::COMPACT))
    {
      SlotInstruction reset = instruction;
      reset.possessionOffset = {0.0f, 0.0f};
      storeSlot(reset);
    }
    ImGui::EndDisabled();
  }
}

void StrategyScene::renderSlotList(const Lineup& lineup)
{
  const Theme::Palette& palette = Theme::palette();
  UI::sectionLabel(LOC("TACTIC_XI_ROLES"));
  const Strategy* strategy = clubStrategy();
  const float width = ImGui::CalcItemWidth();
  const auto row = [&](int slot, const Player& player, const char* position,
                       TacticalRole role, const char* duty)
  {
    ImGui::PushID(slot + 1);
    const std::string label =
        slot == KEEPER_SLOT
            ? std::format("{}  {}", position, player.getName())
            : std::format("{}. {}  {}", slot + 1, position, player.getName());
    if (ImGui::Selectable(label.c_str(), selected_slot == slot, 0,
                          ImVec2(width, 0.0f)))
      selected_slot = slot;
    // Role and duty right-aligned on the row when they fit, else on hover.
    const std::string detail =
        duty ? std::format("{} · {}", LOC(Tactics::roleKey(role)), duty)
             : std::string(LOC(Tactics::roleKey(role)));
    const float detailWidth = ImGui::CalcTextSize(detail.c_str()).x;
    const float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
    const ImVec2 rowMin = ImGui::GetItemRectMin();
    const ImVec2 rowMax = ImGui::GetItemRectMax();
    if (labelWidth + detailWidth + 3.0f * ImGui::GetStyle().ItemSpacing.x <
        width)
    {
      ImGui::GetWindowDrawList()->AddText(
          ImVec2(rowMax.x - detailWidth,
                 rowMin.y +
                     (rowMax.y - rowMin.y - ImGui::GetTextLineHeight()) * 0.5f),
          Theme::toU32(palette.muted), detail.c_str());
    }
    else if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip("%s", detail.c_str());
    }
    ImGui::PopID();
  };
  if (const Player* goalkeeper = lineup.getGoalkeeper(); goalkeeper && strategy)
    row(KEEPER_SLOT, *goalkeeper, RoleUtils::shortName(PlayerRole::GK),
        strategy->getKeeperRole(), nullptr);
  const auto& outfield = lineup.getOutfieldPlayers();
  for (int slot = 0; slot < static_cast<int>(outfield.size()); ++slot)
  {
    const Player* player = outfield[static_cast<std::size_t>(slot)].player;
    if (!player) continue;
    const SlotInstruction instruction = slotAt(lineup, slot);
    row(slot, *player, RoleUtils::shortName(player->getRole()),
        instruction.role, LOC(Tactics::dutyKey(instruction.duty)));
  }
}

void StrategyScene::setRole(const Lineup& lineup, TacticalRole role)
{
  if (selected_slot == KEEPER_SLOT)
  {
    if (Strategy* strategy = clubStrategy()) strategy->setKeeperRole(role);
    return;
  }
  SlotInstruction instruction = slotAt(lineup, selected_slot);
  instruction.role = role;
  storeSlot(instruction);
}

void StrategyScene::setDuty(const Lineup& lineup, RoleDuty duty)
{
  if (selected_slot == KEEPER_SLOT) return;
  SlotInstruction instruction = slotAt(lineup, selected_slot);
  instruction.duty = duty;
  storeSlot(instruction);
}

void StrategyScene::applyShape(const Lineup& lineup, PossessionShape shape)
{
  for (int slot = 0;
       slot < static_cast<int>(lineup.getOutfieldPlayers().size()); ++slot)
  {
    SlotInstruction instruction = slotAt(lineup, slot);
    instruction.possessionOffset =
        Tactics::possessionOffset(shape, instruction.anchor);
    storeSlot(instruction);
  }
}

void StrategyScene::suggestRoles(const Lineup& lineup)
{
  // The assistant's picks keep the shape the manager drew.
  const auto& outfield = lineup.getOutfieldPlayers();
  for (int slot = 0; slot < static_cast<int>(outfield.size()); ++slot)
  {
    const Player* player = outfield[static_cast<std::size_t>(slot)].player;
    if (!player) continue;
    SlotInstruction instruction = slotAt(lineup, slot);
    const RoleFamily family = Tactics::familyForSlot(instruction.anchor);
    instruction.role = Tactics::suggestedRole(*player, family);
    instruction.duty = Tactics::suggestedDuty(*player, family);
    storeSlot(instruction);
  }
  if (const Player* goalkeeper = lineup.getGoalkeeper())
    if (Strategy* strategy = clubStrategy())
      strategy->setKeeperRole(
          Tactics::suggestedRole(*goalkeeper, RoleFamily::Goalkeeper));
}

void StrategyScene::applySliders() const
{
  if (Strategy* strategy = clubStrategy())
    strategy->setAllSliders(current_sliders);
}

bool StrategyScene::changedSinceEntry() const
{
  const Strategy* strategy = clubStrategy();
  if (!strategy) return false;
  return !sameSliders(strategy->getSliders(), entry_strategy.getSliders()) ||
         strategy->getKeeperRole() != entry_strategy.getKeeperRole() ||
         !sameSlots(strategy->getSlotInstructions(),
                    entry_strategy.getSlotInstructions());
}

void StrategyScene::revert()
{
  Strategy* strategy = clubStrategy();
  if (!strategy) return;
  strategy->setAllSliders(entry_strategy.getSliders());
  strategy->setKeeperRole(entry_strategy.getKeeperRole());
  strategy->setSlotInstructions(entry_strategy.getSlotInstructions());
  loadStrategy();
}

void StrategyScene::loadStrategy()
{
  if (const Strategy* strategy = clubStrategy())
  {
    entry_strategy = *strategy;
    current_sliders = strategy->getSliders();
  }
  dragging_slot = -1;
  selected_preset = -1;
  for (size_t index = 0; index < PRESETS.size(); ++index)
  {
    if (sameSliders(current_sliders, PRESETS[index].sliders))
    {
      selected_preset = static_cast<int>(index);
      break;
    }
  }
}

SceneID StrategyScene::getID() const { return SceneID::STRATEGY; }
