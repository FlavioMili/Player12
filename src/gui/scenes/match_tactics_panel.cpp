// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "match_tactics_panel.h"

#include <fmt/printf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>

#include "global/language_manager.h"
#include "gui/scenes/match_scene_tuning.h"
#include "gui/view_models/formation.h"
#include "gui/view_models/match_changes.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/match_engine.h"

namespace
{
constexpr float DIALOG_WIDTH = 900.0f;
constexpr float DIALOG_HEIGHT = 760.0f;
constexpr float SLIDER_LABEL_WIDTH = 170.0f;
constexpr float PREVIEW_MAX_HEIGHT = 180.0f;
constexpr float PREVIEW_ASPECT = 68.0f / 105.0f;
constexpr float PREVIEW_DOT_RADIUS = 6.0f;
constexpr float PREVIEW_LABEL_WIDTH = 84.0f;
constexpr float SHAPE_TOLERANCE = 0.0005f;
/** Below this the familiarity cost of a new shape is not worth a line. */
constexpr float MINIMUM_SHOWN_COST = 0.005f;

float scaled(float value) { return value * Theme::scale(); }

constexpr std::array<const char*, 5> LEVEL_KEYS = {
    "TACTIC_LEVEL_VERY_LOW", "TACTIC_LEVEL_LOW", "TACTIC_LEVEL_MEDIUM",
    "TACTIC_LEVEL_HIGH", "TACTIC_LEVEL_VERY_HIGH"};

const char* levelKey(float value)
{
  const auto level = static_cast<std::size_t>(
      std::clamp(value, 0.0f, 0.999f) * static_cast<float>(LEVEL_KEYS.size()));
  return LEVEL_KEYS[level];
}

bool samePoint(Vector2F left, Vector2F right)
{
  return std::abs(left.x - right.x) <= SHAPE_TOLERANCE &&
         std::abs(left.y - right.y) <= SHAPE_TOLERANCE;
}

bool sameShape(const std::vector<Vector2F>& left,
               const std::vector<Vector2F>& right)
{
  return left.size() == right.size() &&
         std::ranges::equal(left, right, samePoint);
}

const char* shoutLabel(MatchShout shout)
{
  for (const MatchChanges::ShoutInfo& info : MatchChanges::SHOUTS)
    if (info.shout == shout) return LOC(info.labelKey);
  return "";
}
}  // namespace

void MatchTacticsPanel::reset(const Strategy& plan)
{
  applied = plan;
  history.clear();
  draft_sliders = plan.getSliders();
  draft_shape.clear();
}

void MatchTacticsPanel::syncDraft(const TouchlineContext& context)
{
  draft_sliders = applied.getSliders();
  draft_shape = context.engine.getFormation(context.home);
}

bool MatchTacticsPanel::hasChanges(const TouchlineContext& context) const
{
  return !MatchChanges::sameSliders(draft_sliders, applied.getSliders()) ||
         (draft_shape.size() == 10 &&
          !sameShape(draft_shape, context.engine.getFormation(context.home)));
}

bool MatchTacticsPanel::apply(const TouchlineContext& context)
{
  const std::vector<Vector2F> current =
      context.engine.getFormation(context.home);
  const bool newSliders =
      !MatchChanges::sameSliders(draft_sliders, applied.getSliders());
  const bool newShape =
      draft_shape.size() == current.size() && !sameShape(draft_shape, current);
  if (!newSliders && !newShape) return false;
  history.emplace_back(applied, current);
  if (newSliders)
  {
    Strategy next = applied;
    next.setAllSliders(draft_sliders);
    context.engine.setStrategy(context.home, next);
    applied = next;
  }
  if (newShape && !context.engine.setFormation(context.home, draft_shape))
    draft_shape = current;
  context.status = LOC("MATCH_TACTICS_APPLIED");
  context.status_refused = false;
  return true;
}

bool MatchTacticsPanel::undo(const TouchlineContext& context)
{
  if (history.empty()) return false;
  const Snapshot previous = history.back();
  history.pop_back();
  if (!MatchChanges::sameSliders(previous.strategy.getSliders(),
                                 applied.getSliders()))
    context.engine.setStrategy(context.home, previous.strategy);
  applied = previous.strategy;
  if (const std::vector<Vector2F> current =
          context.engine.getFormation(context.home);
      previous.shape.size() == current.size() &&
      !sameShape(previous.shape, current))
    context.engine.setFormation(context.home, previous.shape);
  syncDraft(context);
  context.status = LOC("MATCH_TACTICS_UNDONE");
  context.status_refused = false;
  return true;
}

bool MatchTacticsPanel::render(const TouchlineContext& context)
{
  const bool opening = !popup_opened;
  const std::string title =
      std::format("{}{}", LOC("MATCH_TACTICS_TITLE"), WINDOW_ID);
  if (opening) syncDraft(context);
  if (!Touchline::beginDialog(title.c_str(), popup_opened, DIALOG_WIDTH,
                              DIALOG_HEIGHT))
  {
    close_requested = false;
    return false;
  }
  if (close_requested || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
  {
    close_requested = false;
    popup_opened = false;
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return false;
  }

  ImGui::BeginChild("##tactics_body", ImVec2(0.0f, -Touchline::footerHeight()));
  renderStyle();
  renderInstructions();
  renderFormation(context);
  renderFamiliarity(context);
  ImGui::EndChild();
  renderFooter(context);

  const bool open = popup_opened;
  if (!open) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
  return open;
}

void MatchTacticsPanel::renderStyle()
{
  UI::sectionLabel(LOC("TACTIC_PRESETS"));
  const int active = MatchChanges::detectTacticPreset(draft_sliders);
  for (std::size_t index = 0; index < MatchChanges::TACTIC_PRESETS.size();
       ++index)
  {
    const MatchChanges::TacticPreset& preset =
        MatchChanges::TACTIC_PRESETS[index];
    if (index > 0) UI::sameLineIfFits(UI::buttonWidth(LOC(preset.nameKey)));
    if (UI::toggleButton(LOC(preset.nameKey),
                         active == static_cast<int>(index)))
      draft_sliders = preset.sliders;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", LOC(preset.descriptionKey));
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(
      Theme::palette().faint, "%s",
      active >= 0
          ? LOC(MatchChanges::TACTIC_PRESETS[static_cast<std::size_t>(active)]
                    .descriptionKey)
          : LOC("MATCH_TACTICS_CUSTOM"));
  ImGui::PopTextWrapPos();
}

void MatchTacticsPanel::renderInstructions()
{
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
  UI::sectionLabel(LOC("TACTIC_DETAILS"));
  float levelWidth = 0.0f;
  for (const char* key : LEVEL_KEYS)
    levelWidth = std::max(levelWidth, ImGui::CalcTextSize(LOC(key)).x);
  const float labelWidth = std::min(scaled(SLIDER_LABEL_WIDTH),
                                    ImGui::GetContentRegionAvail().x * 0.35f);
  for (const MatchChanges::SliderInfo& slider : MatchChanges::TACTIC_SLIDERS)
  {
    ImGui::PushID(slider.id);
    const float rowStart = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    UI::textFitted(LOC(slider.key), labelWidth, Theme::palette().text);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", LOC(slider.helpKey));
    ImGui::SameLine(rowStart + labelWidth);
    ImGui::SetNextItemWidth(
        std::max(scaled(60.0f), ImGui::GetContentRegionAvail().x - levelWidth -
                                    ImGui::GetStyle().ItemSpacing.x));
    float& value = draft_sliders.*slider.value;
    ImGui::SliderFloat("##value", &value, 0.0f, 1.0f, "");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s (%.0f%%)", LOC(slider.helpKey),
                        static_cast<double>(value * 100.0f));
    ImGui::SameLine();
    ImGui::TextColored(Theme::palette().muted, "%s", LOC(levelKey(value)));
    ImGui::PopID();
  }
}

void MatchTacticsPanel::renderFormation(const TouchlineContext& context)
{
  // Presets map onto the ten outfield slots; a side with another number of
  // slots keeps its shape.
  if (draft_shape.size() != 10) return;
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
  UI::sectionLabel(LOC("LINEUP_FORMATION"));
  const int active = MatchChanges::detectFormation(draft_shape);
  for (std::size_t index = 0; index < Formation::PRESETS.size(); ++index)
  {
    const Formation::Preset& preset = Formation::PRESETS[index];
    if (index > 0) UI::sameLineIfFits(UI::buttonWidth(preset.name));
    if (UI::toggleButton(preset.name, active == static_cast<int>(index)))
      draft_shape =
          MatchChanges::formationFor(context.engine, context.home, preset);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", LOC("MATCH_TACTICS_FORMATION_HINT"));
  }
  if (active < 0)
  {
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(Theme::palette().muted, "%s",
                       LOC("LINEUP_FORMATION_CUSTOM"));
  }

  // Preview: where each player will stand, with a trail from his spot now.
  const std::vector<Vector2F> current =
      context.engine.getFormation(context.home);
  const float width = ImGui::GetContentRegionAvail().x;
  const float height =
      std::min(width * PREVIEW_ASPECT, scaled(PREVIEW_MAX_HEIGHT));
  const float pitchWidth = height / PREVIEW_ASPECT;
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  const ImVec2 min(cursor.x + (width - pitchWidth) * 0.5f, cursor.y);
  const ImVec2 max(min.x + pitchWidth, min.y + height);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(min, max, MatchSceneTuning::Pitch::GRASS_COLOR,
                          scaled(4.0f));
  const ImU32 line = IM_COL32(255, 255, 255, 110);
  drawList->AddRect(min, max, line, scaled(4.0f), 0, scaled(1.2f));
  drawList->AddLine(ImVec2(min.x + pitchWidth * 0.5f, min.y),
                    ImVec2(min.x + pitchWidth * 0.5f, max.y), line,
                    scaled(1.2f));
  const float radius = scaled(PREVIEW_DOT_RADIUS);
  const float margin = radius * 2.0f;
  const float lineHeight = ImGui::GetTextLineHeight();
  const auto toScreen = [&](Vector2F spot)
  {
    return ImVec2(
        min.x + margin + spot.x * (pitchWidth - 2.0f * margin),
        min.y + margin + spot.y * (height - 2.0f * margin - lineHeight));
  };
  Theme::ScopedText caption(Theme::Text::CAPTION);
  for (const MatchPlayer& player : context.engine.getPlayers())
  {
    if (!player.player || !player.onPitch ||
        player.isHomeTeam != context.home || player.isGoalkeeper ||
        player.formationSlot < 0 ||
        static_cast<std::size_t>(player.formationSlot) >= draft_shape.size())
      continue;
    const auto slot = static_cast<std::size_t>(player.formationSlot);
    const ImVec2 target = toScreen(draft_shape[slot]);
    if (slot < current.size() && !samePoint(current[slot], draft_shape[slot]))
      drawList->AddLine(toScreen(current[slot]), target,
                        IM_COL32(255, 255, 255, 90), scaled(1.5f));
    drawList->AddCircleFilled(target, radius, context.kit);
    drawList->AddCircle(target, radius, IM_COL32(255, 255, 255, 220), 0,
                        scaled(1.2f));
    const std::string& name = player.player->getLastName();
    const float labelWidth = std::min(ImGui::CalcTextSize(name.c_str()).x,
                                      scaled(PREVIEW_LABEL_WIDTH));
    UI::drawTextFitted(
        drawList,
        ImVec2(target.x - labelWidth * 0.5f, target.y + radius + scaled(1.0f)),
        IM_COL32(255, 255, 255, 235), name, scaled(PREVIEW_LABEL_WIDTH));
  }
  ImGui::Dummy(ImVec2(width, height));
}

void MatchTacticsPanel::renderFamiliarity(const TouchlineContext& context)
{
  const Theme::Palette& palette = Theme::palette();
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
  const float familiarity = context.engine.getTacticalFamiliarity(context.home);
  const std::string known =
      fmt::sprintf(LOC("MATCH_TACTICS_FAMILIARITY"),
                   static_cast<int>(std::lround(familiarity * 100.0f)));
  ImGui::TextColored(palette.muted, "%s", known.c_str());
  if (const float cost = MatchChanges::reshapeCost(
          context.engine.getFormation(context.home), draft_shape);
      cost >= MINIMUM_SHOWN_COST)
  {
    const std::string note = fmt::sprintf(
        LOC("MATCH_TACTICS_RESHAPE_COST"),
        static_cast<int>(std::lround(cost * 100.0f)),
        static_cast<int>(std::lround(
            MatchTuning::Touchline::RESHAPE_RECOVERY_SECONDS / 60.0f)));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.warning, "%s", note.c_str());
    ImGui::PopTextWrapPos();
  }
  if (const auto shout = context.engine.getActiveShout(context.home))
  {
    const std::string note = fmt::sprintf(
        LOC("MATCH_TACTICS_SHOUT_IN_FORCE"), shoutLabel(*shout),
        static_cast<int>(std::lround(
            context.engine.getShoutStrength(context.home) * 100.0f)));
    ImGui::TextColored(palette.info, "%s", note.c_str());
  }
}

void MatchTacticsPanel::renderFooter(const TouchlineContext& context)
{
  const Theme::Palette& palette = Theme::palette();
  ImGui::Separator();
  const float settingWidth =
      ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
      ImGui::CalcTextSize(LOC("MATCH_PAUSE_FOR_CHANGES")).x;
  const float rowStart = ImGui::GetCursorPosX();
  const float available = ImGui::GetContentRegionAvail().x;
  ImGui::AlignTextToFramePadding();
  UI::textFitted(context.status,
                 std::max(0.0f, available - settingWidth -
                                    ImGui::GetStyle().ItemSpacing.x),
                 context.status_refused ? palette.negative : palette.positive);
  ImGui::SameLine(rowStart + std::max(0.0f, available - settingWidth));
  Touchline::pauseSetting();

  const bool changed = hasChanges(context);
  ImGui::BeginDisabled(!changed);
  if (UI::primaryButton(LOC("MATCH_TACTICS_APPLY"))) apply(context);
  ImGui::SameLine();
  if (UI::secondaryButton(LOC("TACTIC_REVERT"))) syncDraft(context);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!canUndo());
  if (UI::secondaryButton(LOC("MATCH_TACTICS_UNDO"))) undo(context);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (UI::secondaryButton(LOC("SUBSTITUTION_CLOSE"))) popup_opened = false;
}
