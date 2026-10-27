// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <algorithm>
#include <format>
#include <string>

#include "global/language_manager.h"
#include "global/stats_config.h"
#include "gui/view_models/player_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/player.h"
#include "model/role_utils.h"

namespace PlayerUI
{
/**
 * @brief Compact player card used beside tables (squad, lineup, subs).
 * @param id Stable ImGui id.
 * @param player Player to show, or nullptr for a prompt.
 * @param statsConfig Rating weights.
 * @param comparison Optional player to compare against (shows deltas).
 * @param height Card height (0 = fill, negative = fit the content).
 */
inline void detailPanel(const char* id, const Player* player,
                        const StatsConfig& statsConfig,
                        const Player* comparison = nullptr, float height = 0.0f)
{
  const Theme::Palette& palette = Theme::palette();
  const float dpi = ImGui::GetStyle().FontScaleDpi;
  // A negative height sizes the card to its content (inside a scrolling
  // parent), 0 fills the remaining space.
  if (height < 0.0f)
    UI::beginAutoHeightCard(id, nullptr);
  else
    UI::beginCard(id, nullptr, ImVec2(0.0f, height), true);
  if (!player)
  {
    ImGui::PushStyleColor(ImGuiCol_Text, palette.muted);
    ImGui::TextWrapped("%s", LOC("PLAYER_SELECT_PROMPT"));
    ImGui::PopStyleColor();
    UI::endCard();
    return;
  }

  const double overall = player->getOverall(statsConfig);
  UI::ratingChip(overall);
  ImGui::SameLine();
  {
    Theme::ScopedText title(Theme::Text::TITLE);
    const std::string& nickname = player->getNickname();
    const std::string display_name =
        nickname.empty() ? player->getName()
                         : player->getFirstName() + " \"" + nickname + "\" " +
                               player->getLastName();
    ImGui::TextUnformatted(display_name.c_str());
  }
  ImGui::PushStyleColor(ImGuiCol_Text, palette.muted);
  const Foot foot_value = player->getFoot();
  ImGui::TextWrapped(
      "%s  ·  %s %d  ·  %d cm  ·  %s", RoleUtils::longName(player->getRole()),
      LOC("PLAYER_AGE"), player->getAge(), player->getHeight(),
      foot_value == Foot::Left ? LOC("PLAYER_FOOT_LEFT")
      : foot_value == Foot::Both ? LOC("PLAYER_FOOT_BOTH")
                                 : LOC("PLAYER_FOOT_RIGHT"));
  ImGui::PopStyleColor();

  const float keyWidth = 130.0f * dpi;
  const std::string wage = Format::money(player->getWage());
  UI::keyValue(LOC("PLAYER_WEEKLY_WAGE"), wage.c_str(), keyWidth);
  const std::string contract = std::to_string(player->getContractYears());
  ImGui::PushStyleColor(ImGuiCol_Text, player->getContractYears() <= 1
                                           ? palette.negative
                                           : palette.text);
  UI::keyValue(LOC("PLAYER_CONTRACT"), contract.c_str(), keyWidth);
  ImGui::PopStyleColor();

  if (comparison)
  {
    const double delta = overall - comparison->getOverall(statsConfig);
    ImGui::TextColored(delta >= 0.0 ? palette.positive : palette.negative,
                       "%s: %+.1f", LOC("PLAYER_OVERALL_CHANGE"), delta);
    if (player->getRole() == comparison->getRole())
    {
      ImGui::TextColored(palette.positive, "%s", LOC("PLAYER_ROLE_MATCH"));
    }
    else
    {
      ImGui::TextColored(palette.warning, "%s: %s -> %s",
                         LOC("PLAYER_ROLE_CHANGE"),
                         RoleUtils::shortName(comparison->getRole()),
                         RoleUtils::shortName(player->getRole()));
    }
  }

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * dpi));
  UI::sectionLabel(LOC("PLAYER_ATTRIBUTES"));
  const float labelWidth = 100.0f * dpi;
  for (const auto& [name, value] : player->getStats())
  {
    const std::string label = PlayerView::statLabel(name);
    if (!comparison)
    {
      UI::attributeBar(label.c_str(), value, labelWidth);
      continue;
    }
    const auto other = comparison->getStats().find(name);
    const float delta =
        value - (other != comparison->getStats().end() ? other->second : 0.0f);
    const std::string deltaText = std::format("{:+.0f}", delta);
    const float deltaWidth = ImGui::CalcTextSize("+00").x;
    const float rowStart = ImGui::GetCursorPosX();
    const float available = ImGui::GetContentRegionAvail().x;
    UI::attributeBar(label.c_str(), value, labelWidth, 100.0f,
                     available - deltaWidth - Theme::Space::S * dpi);
    ImGui::SameLine(rowStart + available - deltaWidth);
    ImGui::TextColored(delta >= 0.0f ? palette.positive : palette.negative,
                       "%s", deltaText.c_str());
  }
  UI::endCard();
}
}  // namespace PlayerUI
