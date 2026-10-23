// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/delegation_scene.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <utility>

#include "controller/game_controller.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"

namespace
{
constexpr float CONTENT_MAX_WIDTH = 920.0f;
constexpr float OWNER_CONTROL_WIDTH = 240.0f;
/** Below this card width the owner control goes under the text. */
constexpr float STACK_BELOW = 560.0f;

constexpr std::array<const char*, 3> PRESET_HELP_KEYS = {
    "DELEGATION_PRESET_HANDS_ON_HELP", "DELEGATION_PRESET_BALANCED_HELP",
    "DELEGATION_PRESET_ASSISTANT_HELP"};
}  // namespace

DelegationScene::DelegationScene(GUIView* parent) : ManagementScene(parent) {}

void DelegationScene::refresh()
{
  const GameController& controller = guiView->getController();
  policy = controller.getDelegation();
  if (const StaffMember* delegate = controller.getDelegate())
    delegate_line =
        fmt::sprintf(LOC(delegate->role == StaffRole::AssistantManager
                             ? "DELEGATION_DELEGATE_ASSISTANT"
                             : "DELEGATION_DELEGATE_COACH"),
                     delegate->name());
  else
    delegate_line = LOC("DELEGATION_DELEGATE_NONE");
}

void DelegationScene::renderContent()
{
  GameController& controller = guiView->getController();
  UI::pageHeader(LOC("DELEGATION_TITLE"), delegate_line.c_str());
  const float width = std::min(ImGui::GetContentRegionAvail().x,
                               CONTENT_MAX_WIDTH * Theme::scale());

  UI::beginAutoHeightCard("delegation_presets", LOC("DELEGATION_PRESETS"),
                          width);
  const std::array<const char*, 3> labels = {
      LOC(delegationPresetKey(DelegationPreset::HandsOn)),
      LOC(delegationPresetKey(DelegationPreset::Balanced)),
      LOC(delegationPresetKey(DelegationPreset::AssistantRuns))};
  const auto matching = policy.matchingPreset();
  if (int selected =
          matching ? static_cast<int>(std::to_underlying(*matching)) : -1;
      UI::segmented("##preset", selected, labels,
                    ImGui::GetContentRegionAvail().x))
  {
    controller.applyDelegationPreset(static_cast<DelegationPreset>(selected));
    refresh();
    showToast(LOC("DELEGATION_SAVED"));
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(Theme::palette().muted, "%s",
                     matching
                         ? LOC(PRESET_HELP_KEYS[static_cast<size_t>(*matching)])
                         : LOC("DELEGATION_PRESET_CUSTOM_HELP"));
  ImGui::PopTextWrapPos();
  UI::endCard();

  UI::beginAutoHeightCard("delegation_duties", LOC("DELEGATION_DUTIES"), width);
  for (std::size_t index = 0; index < DUTY_COUNT; ++index)
  {
    if (index > 0) ImGui::Separator();
    renderDuty(static_cast<Duty>(index), ImGui::GetContentRegionAvail().x);
  }
  UI::endCard();
}

void DelegationScene::renderDuty(Duty duty, float width)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const bool stacked = width < STACK_BELOW * scale;
  const float controlWidth = stacked ? width : OWNER_CONTROL_WIDTH * scale;
  const float textWidth =
      stacked ? width : width - controlWidth - Theme::Space::L * scale;
  ImGui::PushID(static_cast<int>(std::to_underlying(duty)));
  const float startX = ImGui::GetCursorPosX();
  const float startY = ImGui::GetCursorPosY();
  ImGui::BeginGroup();
  ImGui::PushTextWrapPos(startX + textWidth);
  ImGui::TextUnformatted(LOC(dutyKey(duty)));
  {
    Theme::ScopedText small(Theme::Text::SMALL);
    ImGui::TextColored(palette.muted, "%s", LOC(dutyHelpKey(duty)));
  }
  ImGui::PopTextWrapPos();
  ImGui::EndGroup();
  const float textHeight = ImGui::GetItemRectSize().y;

  if (!stacked)
  {
    ImGui::SetCursorPos(ImVec2(
        startX + width - controlWidth,
        startY + std::max(0.0f, (textHeight - UI::buttonHeight()) * 0.5f)));
  }
  const std::array<const char*, 2> owners = {LOC("DELEGATION_OWNER_MANAGER"),
                                             LOC("DELEGATION_OWNER_ASSISTANT")};
  if (int owner = policy.delegated(duty) ? 1 : 0;
      UI::segmented("##owner", owner, owners, controlWidth))
  {
    guiView->getController().setDutyOwner(
        duty, owner == 1 ? DutyOwner::Assistant : DutyOwner::Manager);
    refresh();
    showToast(LOC("DELEGATION_SAVED"));
  }
  if (!stacked)
  {
    const float bottom = startY + textHeight;
    if (ImGui::GetCursorPosY() < bottom)
      ImGui::Dummy(ImVec2(0.0f, bottom - ImGui::GetCursorPosY()));
  }
  ImGui::PopID();
}
