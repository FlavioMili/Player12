// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "match_touchline.h"

#include <fmt/printf.h>

#include <algorithm>

#include "global/language_manager.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/lineup.h"
#include "model/match_engine.h"
#include "model/settings_manager.h"

namespace Touchline
{
namespace
{
constexpr float VIEWPORT_FRACTION = 0.94f;
/** Condition above which a player is fresh, and below which he is spent. */
constexpr float FRESH_CONDITION = 0.75f;
constexpr float SPENT_CONDITION = 0.6f;
}  // namespace

bool beginDialog(const char* title, bool& opened, float width, float height)
{
  // Opened once: re-opening every frame would close any other dialog (the
  // half-time talk) that opens meanwhile.
  if (!opened)
  {
    ImGui::OpenPopup(title);
    opened = true;
  }
  // Always fits the window: the body scrolls as one surface and the actions
  // stay pinned at the bottom.
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowSize(
      ImVec2(std::min(width * Theme::scale(),
                      viewport->WorkSize.x * VIEWPORT_FRACTION),
             std::min(height * Theme::scale(),
                      viewport->WorkSize.y * VIEWPORT_FRACTION)),
      ImGuiCond_Always);
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal(title, nullptr,
                             ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoScrollWithMouse))
    return true;
  // Closed by another dialog taking its place.
  opened = false;
  return false;
}

float footerHeight()
{
  const ImGuiStyle& style = ImGui::GetStyle();
  return UI::buttonHeight() + ImGui::GetTextLineHeightWithSpacing() +
         style.ItemSpacing.y * 3.0f + Theme::scale();
}

void pauseSetting()
{
  if (Settings& settings = SettingsManager::instance()->get(); ImGui::Checkbox(
          LOC("MATCH_PAUSE_FOR_CHANGES"), &settings.pause_for_match_changes))
    SettingsManager::instance()->save();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", LOC("MATCH_PAUSE_FOR_CHANGES_HINT"));
}

bool pausesMatch()
{
  return SettingsManager::instance()->get().pause_for_match_changes;
}

std::string refusalText(MatchChanges::Refusal refusal,
                        const MatchEngine& engine, const std::string& outName,
                        const std::string& inName)
{
  using enum MatchChanges::Refusal;
  const char* key = MatchChanges::refusalKey(refusal);
  switch (refusal)
  {
    case NONE:
      return {};
    case LIMIT:
      return fmt::sprintf(LOC(key), MatchChanges::maxSubstitutions(engine));
    case WINDOWS:
      return fmt::sprintf(LOC(key), MatchChanges::maxWindows(engine));
    case NOT_ON_PITCH:
      return fmt::sprintf(LOC(key), outName.c_str());
    case NOT_ON_BENCH:
    case ALREADY_PLAYED:
      return fmt::sprintf(LOC(key), inName.c_str());
    case ALREADY_PLANNED:
    case MATCH_OVER:
    case NOT_NOW:
      return LOC(key);
  }
  return LOC(key);
}

std::string playerName(const Lineup& lineup, PlayerID player)
{
  for (const Player* starter : lineup.starters())
    if (starter->getId() == player) return starter->getName();
  for (const Player* reserve : lineup.getReserves())
    if (reserve && reserve->getId() == player) return reserve->getName();
  return {};
}

ImVec4 conditionColor(float condition)
{
  const Theme::Palette& palette = Theme::palette();
  if (condition >= FRESH_CONDITION) return palette.positive;
  if (condition >= SPENT_CONDITION) return palette.warning;
  return palette.negative;
}
}  // namespace Touchline
