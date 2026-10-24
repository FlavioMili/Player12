// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/mentoring_dialog.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/mentoring.h"
#include "model/youth_academy.h"

namespace
{
constexpr const char* POPUP_ID = "###mentoring_dialog";
constexpr float DIALOG_WIDTH = 640.0f;
constexpr float VIEWPORT_WIDTH_SHARE = 0.94f;
constexpr float VIEWPORT_HEIGHT_SHARE = 0.9f;

float scaled(float value) { return value * Theme::scale(); }
}  // namespace

void MentoringDialog::open(GameController& controller)
{
  message.clear();
  rebuild(controller);
  open_requested = true;
}

void MentoringDialog::report(const char* key, bool error)
{
  message = LOC(key);
  message_error = error;
}

void MentoringDialog::rebuild(GameController& controller)
{
  groups.clear();
  mentor_options.clear();
  mentee_options.clear();
  const auto team = controller.getManagedTeam();
  if (!team) return;
  const auto gamedata = controller.getGameData();
  const auto member = [&](PlayerID id)
  {
    Member result;
    result.id = id;
    if (const auto player = gamedata->getPlayer(id))
    {
      result.name = player->get().getName();
      result.detail = fmt::sprintf(
          LOC("MENTORING_MEMBER_DETAIL"), player->get().getAge(),
          LOC(YouthModel::personalityKey(player->get().getTraits())));
    }
    return result;
  };
  for (const MentoringGroup& group : controller.getMentoringGroups())
  {
    GroupView view;
    view.id = group.id;
    view.mentor = member(group.mentor_id);
    for (const MenteeState& state : group.mentees)
    {
      Member mentee = member(state.player_id);
      mentee.shift =
          fmt::sprintf(LOC("MENTORING_SHIFT"),
                       static_cast<double>(state.professionalism_shift),
                       static_cast<double>(state.temperament_shift));
      view.mentees.push_back(std::move(mentee));
    }
    if (const auto mentor = gamedata->getPlayer(group.mentor_id))
      view.effect =
          fmt::sprintf(LOC("MENTORING_GROUP_EFFECT"),
                       (Mentoring::developmentMultiplier(
                            mentor->get().getTraits().professionalism) -
                        1.0f) *
                           100.0f);
    groups.push_back(std::move(view));
  }
  for (const auto& player : controller.getPlayersForTeam(team->get().getId()))
  {
    const PlayerID id = player.get().getId();
    if (const bool grouped = std::ranges::any_of(
            groups,
            [id](const GroupView& group)
            {
              return group.mentor.id == id ||
                     std::ranges::contains(group.mentees, id, &Member::id);
            });
        grouped)
      continue;
    const std::string label = fmt::sprintf(
        "%s (%d, %s)", player.get().getName().c_str(), player.get().getAge(),
        LOC(YouthModel::personalityKey(player.get().getTraits())));
    if (player.get().getAge() >= Mentoring::MENTOR_MIN_AGE)
      mentor_options.emplace_back(id, label);
    if (player.get().getAge() <= Mentoring::MENTEE_MAX_AGE)
      mentee_options.emplace_back(id, label);
  }
  const auto byName = [](const auto& a, const auto& b)
  { return a.second < b.second; };
  std::ranges::sort(mentor_options, byName);
  std::ranges::sort(mentee_options, byName);
}

bool MentoringDialog::render(GameController& controller)
{
  if (open_requested)
  {
    open_requested = false;
    ImGui::OpenPopup(POPUP_ID);
  }
  visible = ImGui::IsPopupOpen(POPUP_ID);
  if (!visible) return false;

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float width = std::min(scaled(DIALOG_WIDTH),
                               viewport->WorkSize.x * VIEWPORT_WIDTH_SHARE);
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(width, 0.0f),
      ImVec2(width, viewport->WorkSize.y * VIEWPORT_HEIGHT_SHARE));
  if (!ImGui::BeginPopupModal(POPUP_ID, nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize |
                                  ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoSavedSettings))
  {
    visible = false;
    return false;
  }
  const Theme::Palette& palette = Theme::palette();
  {
    Theme::ScopedText heading(Theme::Text::TITLE);
    ImGui::TextUnformatted(LOC("MENTORING_TITLE"));
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.muted, "%s", LOC("MENTORING_EXPLAIN"));
  ImGui::PopTextWrapPos();
  ImGui::Separator();

  bool changed = false;
  if (groups.empty())
    ImGui::TextColored(palette.faint, "%s", LOC("MENTORING_NO_GROUPS"));
  for (const GroupView& group : groups)
  {
    ImGui::PushID(static_cast<int>(group.id));
    if (renderGroup(controller, group))
    {
      changed = true;
      ImGui::PopID();
      break;
    }
    ImGui::PopID();
  }
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::S)));
  if (!changed) changed = renderNewGroup(controller);

  if (!message.empty())
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(message_error ? palette.negative : palette.positive,
                       "%s", message.c_str());
    ImGui::PopTextWrapPos();
  }
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::S)));
  if (const ImVec2 closeSize(
          std::min(scaled(150.0f), ImGui::GetContentRegionAvail().x), 0.0f);
      UI::primaryButton(LOC("TALK_CLOSE"), closeSize) ||
      ImGui::IsKeyPressed(ImGuiKey_Escape, false))
  {
    ImGui::CloseCurrentPopup();
    visible = false;
  }
  ImGui::EndPopup();
  if (changed) rebuild(controller);
  return changed;
}

bool MentoringDialog::renderGroup(GameController& controller,
                                  const GroupView& group)
{
  const Theme::Palette& palette = Theme::palette();
  const float available = ImGui::GetContentRegionAvail().x;
  UI::sectionLabel(
      fmt::sprintf(LOC("MENTORING_GROUP_OF"), group.mentor.name.c_str())
          .c_str());
  UI::textFitted(group.mentor.detail + "  ·  " + group.effect, available,
                 palette.muted);
  const float removeWidth =
      UI::buttonWidth(LOC("MENTORING_REMOVE"), UI::ButtonSize::COMPACT);
  for (const Member& mentee : group.mentees)
  {
    ImGui::PushID(static_cast<int>(mentee.id));
    const float textWidth =
        std::max(0.0f, available - removeWidth - scaled(Theme::Space::S));
    const float startX = ImGui::GetCursorPosX();
    ImGui::BeginGroup();
    UI::textFitted(mentee.name + "  ·  " + mentee.detail, textWidth,
                   palette.text);
    UI::textFitted(mentee.shift, textWidth, palette.faint);
    ImGui::EndGroup();
    ImGui::SameLine(startX + available - removeWidth);
    const bool removed =
        UI::secondaryButton(LOC("MENTORING_REMOVE"), ImVec2(removeWidth, 0.0f),
                            UI::ButtonSize::COMPACT);
    ImGui::PopID();
    if (removed)
    {
      const MentoringError error = controller.removeMentee(group.id, mentee.id);
      report(error == MentoringError::None ? "MENTORING_REMOVED"
                                           : mentoringErrorKey(error),
             error != MentoringError::None);
      return true;
    }
  }
  bool changed = false;
  if (group.mentees.size() < Mentoring::MAX_MENTEES && !mentee_options.empty())
  {
    ImGui::SetNextItemWidth(std::min(scaled(320.0f), available));
    if (ImGui::BeginCombo("##add_mentee", LOC("MENTORING_ADD_MENTEE"),
                          ImGuiComboFlags_HeightLarge))
    {
      for (const auto& [id, label] : mentee_options)
        if (ImGui::Selectable(label.c_str(), false))
        {
          const MentoringError error = controller.addMentee(group.id, id);
          report(error == MentoringError::None ? "MENTORING_ADDED"
                                               : mentoringErrorKey(error),
                 error != MentoringError::None);
          changed = true;
        }
      ImGui::EndCombo();
    }
    UI::sameLineIfFits(
        UI::buttonWidth(LOC("MENTORING_DISSOLVE"), UI::ButtonSize::COMPACT));
  }
  if (!changed && UI::secondaryButton(LOC("MENTORING_DISSOLVE"), ImVec2(0, 0),
                                      UI::ButtonSize::COMPACT))
  {
    controller.dissolveMentoringGroup(group.id);
    report("MENTORING_DISSOLVED", false);
    changed = true;
  }
  ImGui::Separator();
  return changed;
}

bool MentoringDialog::renderNewGroup(GameController& controller)
{
  const Theme::Palette& palette = Theme::palette();
  UI::sectionLabel(LOC("MENTORING_NEW_GROUP"));
  if (mentor_options.empty())
  {
    ImGui::TextColored(palette.faint, "%s", LOC("MENTORING_NO_MENTORS"));
    return false;
  }
  bool changed = false;
  ImGui::SetNextItemWidth(
      std::min(scaled(360.0f), ImGui::GetContentRegionAvail().x));
  if (ImGui::BeginCombo("##new_mentor", LOC("MENTORING_PICK_MENTOR"),
                        ImGuiComboFlags_HeightLarge))
  {
    for (const auto& [id, label] : mentor_options)
      if (ImGui::Selectable(label.c_str(), false))
      {
        const MentoringError error = controller.createMentoringGroup(id);
        report(error == MentoringError::None ? "MENTORING_CREATED"
                                             : mentoringErrorKey(error),
               error != MentoringError::None);
        changed = true;
      }
    ImGui::EndCombo();
  }
  return changed;
}
