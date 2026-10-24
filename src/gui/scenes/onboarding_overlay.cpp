// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/onboarding_overlay.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <utility>

#include "controller/game_controller.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/delegation.h"
#include "model/inbox.h"
#include "model/settings_manager.h"

namespace
{
/** Section whose tip is on screen during the current visit. */
NavSection shown_tip = NavSection::NONE;

std::uint64_t tipBit(NavSection section)
{
  return std::uint64_t{1} << static_cast<unsigned>(std::to_underlying(section));
}

const char* actionButtonKey(ActionTarget target)
{
  using enum ActionTarget;
  switch (target)
  {
    case Lineup:
      return "NEXT_BUTTON_LINEUP";
    case Inbox:
      return "NEXT_BUTTON_INBOX";
    case Transfers:
      return "NEXT_BUTTON_TRANSFERS";
    case Scouting:
      return "NEXT_BUTTON_SCOUTING";
    case Training:
      return "NEXT_BUTTON_TRAINING";
    case Club:
      return "NEXT_BUTTON_CLUB";
    case Player:
      return "NEXT_BUTTON_PLAYER";
    case Opposition:
      return "NEXT_BUTTON_OPPOSITION";
  }
  return "NEXT_BUTTON_INBOX";
}

ImVec4 priorityColor(int priority)
{
  const Theme::Palette& palette = Theme::palette();
  if (priority >= 80) return palette.negative;
  if (priority >= 55) return palette.warning;
  return palette.info;
}

/**
 * Priority of a next step by shape as well as colour: a triangle for
 * urgent work, a diamond for important, a dot for the rest.
 */
void priorityMarker(ImVec2 centre, int priority)
{
  const float size = 4.5f * Theme::scale();
  const ImU32 color = Theme::toU32(priorityColor(priority));
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  if (priority >= 80)
    drawList->AddTriangleFilled(ImVec2(centre.x, centre.y - size),
                                ImVec2(centre.x + size, centre.y + size * 0.8f),
                                ImVec2(centre.x - size, centre.y + size * 0.8f),
                                color);
  else if (priority >= 55)
    drawList->AddQuadFilled(ImVec2(centre.x, centre.y - size),
                            ImVec2(centre.x + size, centre.y),
                            ImVec2(centre.x, centre.y + size),
                            ImVec2(centre.x - size, centre.y), color);
  else
    drawList->AddCircleFilled(centre, size * 0.75f, color);
}

/** Filled dot for done steps, ring for open ones (no glyph dependency). */
void stepMarker(bool done)
{
  const Theme::Palette& palette = Theme::palette();
  const float size = ImGui::GetTextLineHeight();
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const ImVec2 center(start.x + size * 0.5f, start.y + size * 0.5f);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const float radius = size * 0.3f;
  if (done)
    drawList->AddCircleFilled(center, radius, Theme::toU32(palette.positive));
  else
    drawList->AddCircle(center, radius, Theme::toU32(palette.faint), 0,
                        1.5f * Theme::scale());
  ImGui::Dummy(ImVec2(size, size));
}
}  // namespace

namespace GuidanceUI
{
std::optional<OnboardingTask> taskForSection(NavSection section)
{
  using enum NavSection;
  using enum OnboardingTask;
  switch (section)
  {
    case TACTICS:
      return ReviewTactics;
    case SQUAD:
      return CheckSquad;
    case CLUB:
      return BoardObjective;
    case TRAINING:
      return SetTraining;
    case TRANSFERS:
    case SCOUTING:
      return ExploreMarket;
    default:
      break;
  }
  return std::nullopt;
}

NavSection sectionForTask(OnboardingTask task)
{
  using enum NavSection;
  using enum OnboardingTask;
  switch (task)
  {
    case ReviewTactics:
      return TACTICS;
    case CheckSquad:
      return SQUAD;
    case BoardObjective:
      return CLUB;
    case SetTraining:
      return TRAINING;
    case ExploreMarket:
      return SCOUTING;
    case PlayFirstMatch:
    case COUNT:
      break;
  }
  return FIXTURES;
}

void noteVisit(GameController& controller, NavSection section)
{
  if (const auto task = taskForSection(section))
    controller.completeOnboardingTask(*task);
}

void openAction(GUIView* view, const NextAction& action)
{
  using enum ActionTarget;
  switch (action.target)
  {
    case Lineup:
      Navigation::open(view, NavSection::LINEUP);
      return;
    case Inbox:
      Navigation::open(view, NavSection::INBOX);
      return;
    case Transfers:
      Navigation::open(view, NavSection::TRANSFERS);
      return;
    case Scouting:
      Navigation::open(view, NavSection::SCOUTING);
      return;
    case Training:
      Navigation::open(view, NavSection::TRAINING);
      return;
    case Club:
      Navigation::open(view, NavSection::CLUB);
      return;
    case Player:
      if (action.ref != 0) Navigation::openPlayer(view, action.ref);
      return;
    case Opposition:
      Navigation::open(view, NavSection::OPPOSITION);
      return;
  }
}

std::string text(const AnalysisLine& line)
{
  return formatLocalized(line.key, line.args);
}

const char* tipKey(NavSection section)
{
  using enum NavSection;
  switch (section)
  {
    case HOME:
      return "TIP_HOME";
    case INBOX:
      return "TIP_INBOX";
    case CLUB:
      return "TIP_CLUB";
    case SQUAD:
      return "TIP_SQUAD";
    case LINEUP:
      return "TIP_LINEUP";
    case TACTICS:
      return "TIP_TACTICS";
    case FIXTURES:
      return "TIP_FIXTURES";
    case STANDINGS:
      return "TIP_STANDINGS";
    case TRANSFERS:
      return "TIP_TRANSFERS";
    case FINANCES:
      return "TIP_FINANCES";
    case SCOUTING:
      return "TIP_SCOUTING";
    case TRAINING:
      return "TIP_TRAINING";
    case STAFF:
      return "TIP_STAFF";
    case YOUTH:
      return "TIP_YOUTH";
    case DELEGATION:
      return "TIP_DELEGATION";
    case DATA_HUB:
      return "TIP_DATA_HUB";
    case OPPOSITION:
      return "TIP_OPPOSITION";
    case MANAGER:
      return "TIP_MANAGER";
    case MEDICAL:
      return "TIP_MEDICAL";
    case CALENDAR:
      return "TIP_CALENDAR";
    case SQUAD_PLANNER:
      return "TIP_SQUAD_PLANNER";
    case COMPARE:
      return "TIP_COMPARE";
    case INTERNATIONAL:
      return "TIP_INTERNATIONAL";
    case AWARDS:
      return "TIP_AWARDS";
    case RECORDS:
      return "TIP_RECORDS";
    case PLANNING:
      return "TIP_PLANNING";
    default:
      break;
  }
  return nullptr;
}

void renderScreenTip(NavSection section)
{
  const char* key = tipKey(section);
  Settings& settings = SettingsManager::instance()->get();
  if (key == nullptr || !settings.screen_tips)
  {
    // A visit elsewhere ends the tip's visit: it does not come back.
    shown_tip = NavSection::NONE;
    return;
  }
  if (shown_tip != section)
  {
    if ((settings.screen_tips_seen & tipBit(section)) != 0)
    {
      shown_tip = NavSection::NONE;
      return;
    }
    // Seen from now on; it stays visible for the rest of this visit.
    settings.screen_tips_seen |= tipBit(section);
    SettingsManager::instance()->save();
    shown_tip = section;
  }

  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const char* gotIt = LOC("TIP_GOT_IT");
  const char* turnOff = LOC("TIP_TURN_OFF");
  const float buttons = UI::buttonWidth(gotIt, UI::ButtonSize::COMPACT) +
                        UI::buttonWidth(turnOff, UI::ButtonSize::COMPACT) +
                        ImGui::GetStyle().ItemSpacing.x;
  const float width = ImGui::GetContentRegionAvail().x;
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const float padding = Theme::Space::S * scale;
  const float textWidth = std::max(
      width - buttons - 3.0f * padding - ImGui::GetStyle().ItemSpacing.x,
      width * 0.4f);
  const float textHeight =
      ImGui::CalcTextSize(LOC(key), nullptr, false, textWidth).y;
  const float rowHeight =
      std::max(textHeight, UI::buttonHeight(UI::ButtonSize::COMPACT)) +
      2.0f * padding;
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(start, ImVec2(start.x + width, start.y + rowHeight),
                          Theme::toU32(palette.raised), 6.0f * scale);
  drawList->AddRectFilled(start,
                          ImVec2(start.x + 3.0f * scale, start.y + rowHeight),
                          Theme::toU32(palette.info), 2.0f * scale);

  ImGui::PushID("screen_tip");
  ImGui::SetCursorScreenPos(ImVec2(start.x + 2.0f * padding,
                                   start.y + (rowHeight - textHeight) * 0.5f));
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textWidth);
  ImGui::TextUnformatted(LOC(key));
  ImGui::PopTextWrapPos();
  const float buttonY =
      start.y + (rowHeight - UI::buttonHeight(UI::ButtonSize::COMPACT)) * 0.5f;
  ImGui::SetCursorScreenPos(
      ImVec2(start.x + width - buttons - padding, buttonY));
  if (UI::secondaryButton(gotIt, ImVec2(0.0f, 0.0f), UI::ButtonSize::COMPACT))
    shown_tip = NavSection::NONE;
  ImGui::SameLine();
  if (UI::secondaryButton(turnOff, ImVec2(0.0f, 0.0f), UI::ButtonSize::COMPACT))
  {
    settings.screen_tips = false;
    SettingsManager::instance()->save();
    shown_tip = NavSection::NONE;
  }
  ImGui::PopID();
  ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + rowHeight));
  ImGui::Dummy(ImVec2(width, Theme::Space::S * scale));
}
}  // namespace GuidanceUI

namespace GuidanceUI
{
void renderReclaimNotice(const GUIView* view)
{
  GameController& controller = view->getController();
  const auto duty = controller.getReclaimedDuty();
  if (!duty) return;
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const std::string text =
      fmt::sprintf(LOC("RECLAIM_NOTICE"), LOC(dutyKey(*duty)));
  const char* undo = LOC("RECLAIM_UNDO");
  const char* ok = LOC("RECLAIM_OK");
  const float buttons = UI::buttonWidth(undo, UI::ButtonSize::COMPACT) +
                        UI::buttonWidth(ok, UI::ButtonSize::COMPACT) +
                        ImGui::GetStyle().ItemSpacing.x;
  const float width = ImGui::GetContentRegionAvail().x;
  const float padding = Theme::Space::S * scale;
  const float rowHeight =
      UI::buttonHeight(UI::ButtonSize::COMPACT) + 2.0f * padding;
  const ImVec2 start = ImGui::GetCursorScreenPos();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(start, ImVec2(start.x + width, start.y + rowHeight),
                          Theme::toU32(palette.raised), 6.0f * scale);
  drawList->AddRectFilled(start,
                          ImVec2(start.x + 3.0f * scale, start.y + rowHeight),
                          Theme::toU32(palette.warning), 2.0f * scale);
  ImGui::PushID("reclaim_notice");
  ImGui::SetCursorScreenPos(
      ImVec2(start.x + 2.0f * padding,
             start.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f));
  UI::textFitted(text, width - buttons - 4.0f * padding, palette.text);
  ImGui::SetCursorScreenPos(
      ImVec2(start.x + width - buttons - padding, start.y + padding));
  if (UI::secondaryButton(undo, ImVec2(0.0f, 0.0f), UI::ButtonSize::COMPACT))
    controller.undoReclaimedDuty();
  ImGui::SameLine();
  if (UI::secondaryButton(ok, ImVec2(0.0f, 0.0f), UI::ButtonSize::COMPACT))
    controller.dismissReclaimedDuty();
  ImGui::PopID();
  ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + rowHeight));
  ImGui::Dummy(ImVec2(width, Theme::Space::S * scale));
}
}  // namespace GuidanceUI

void NextStepsCard::refresh(const GameController& controller)
{
  rows.clear();
  checklist = controller.getOnboarding();
  refreshed = true;
  for (NextAction& action : controller.getNextActions(MAX_ROWS))
  {
    Row row;
    row.title = GuidanceUI::text(action.title);
    row.reason = GuidanceUI::text(action.reason);
    row.action = std::move(action);
    rows.push_back(std::move(row));
  }
}

void NextStepsCard::render(GUIView* view, float width)
{
  if (!refreshed) refresh(view->getController());
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  UI::beginAutoHeightCard("next_steps", LOC("NEXT_STEPS_TITLE"), width);

  if (checklist.isVisible())
  {
    const std::string progress =
        fmt::sprintf(LOC("ONBOARDING_PROGRESS"), checklist.doneCount(),
                     ONBOARDING_TASK_COUNT);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(palette.muted, "%s", progress.c_str());
    const char* hide = LOC("ONBOARDING_HIDE");
    ImGui::SameLine(ImGui::GetContentRegionMax().x -
                    UI::buttonWidth(hide, UI::ButtonSize::COMPACT));
    if (UI::secondaryButton(hide, ImVec2(0.0f, 0.0f), UI::ButtonSize::COMPACT))
    {
      view->getController().dismissOnboarding();
      checklist.dismiss();
    }
    for (std::size_t index = 0; index < ONBOARDING_TASK_COUNT; ++index)
    {
      const auto task = static_cast<OnboardingTask>(index);
      const char* label = LOC(onboardingTaskKey(task));
      const float itemWidth = ImGui::GetTextLineHeight() +
                              ImGui::GetStyle().ItemSpacing.x +
                              ImGui::CalcTextSize(label).x;
      if (index > 0) UI::sameLineIfFits(itemWidth, Theme::Space::L * scale);
      ImGui::BeginGroup();
      stepMarker(checklist.isDone(task));
      ImGui::SameLine();
      ImGui::PushID(static_cast<int>(index));
      if (UI::link(label, "step"))
        Navigation::open(view, GuidanceUI::sectionForTask(task));
      ImGui::PopID();
      ImGui::EndGroup();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", LOC(onboardingTaskHelpKey(task)));
    }
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
    ImGui::Separator();
  }

  if (rows.empty())
    ImGui::TextColored(palette.muted, "%s", LOC("NEXT_STEPS_NONE"));
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  for (std::size_t index = 0; index < rows.size(); ++index)
  {
    const Row& row = rows[index];
    ImGui::PushID(static_cast<int>(index));
    const char* button = LOC(actionButtonKey(row.action.target));
    const float buttonWidth = UI::buttonWidth(button, UI::ButtonSize::COMPACT);
    const float right = ImGui::GetContentRegionAvail().x;
    const float dot = 10.0f * scale;
    const float textWidth =
        std::max(right - buttonWidth - dot - 2.0f * spacing, 60.0f * scale);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float lineHeight = ImGui::GetTextLineHeight();
    priorityMarker(ImVec2(start.x + dot * 0.4f, start.y + lineHeight * 0.5f),
                   row.action.priority);
    ImGui::SetCursorScreenPos(ImVec2(start.x + dot, start.y));
    ImGui::BeginGroup();
    UI::textFitted(row.title, textWidth, palette.text);
    {
      Theme::ScopedText small(Theme::Text::SMALL);
      UI::textFitted(row.reason, textWidth, palette.muted);
    }
    ImGui::EndGroup();
    const float blockHeight = ImGui::GetItemRectSize().y;
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(
        start.x + right - buttonWidth,
        start.y +
            (blockHeight - UI::buttonHeight(UI::ButtonSize::COMPACT)) * 0.5f));
    if (UI::secondaryButton(button, ImVec2(0.0f, 0.0f),
                            UI::ButtonSize::COMPACT))
    {
      const NextAction action = row.action;
      ImGui::PopID();
      UI::endCard();
      GuidanceUI::openAction(view, action);
      return;
    }
    ImGui::SetCursorScreenPos(ImVec2(
        start.x,
        start.y +
            std::max(blockHeight, UI::buttonHeight(UI::ButtonSize::COMPACT))));
    ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));
    ImGui::PopID();
  }
  if (UI::link(LOC("NEXT_STEPS_DELEGATION"), "delegation"))
    Navigation::open(view, NavSection::DELEGATION);
  UI::endCard();
}
