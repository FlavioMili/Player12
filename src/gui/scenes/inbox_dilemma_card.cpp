// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/inbox_dilemma_card.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/world_rng.h"

namespace
{
/** Below this width (unscaled) the two answers are stacked. */
constexpr float SIDE_BY_SIDE_WIDTH = 520.0f;

std::string playerName(const GameController& controller, PlayerID player_id)
{
  if (player_id == 0) return {};
  const auto player = controller.getGameData()->getPlayer(player_id);
  return player ? player->get().getName() : std::string();
}

int rounded(float value) { return static_cast<int>(std::lround(value)); }
}  // namespace

namespace InboxDilemmaCard
{
std::vector<EffectLine> effectLines(const DilemmaEffects& effects,
                                    const std::string& subject,
                                    const std::string& other)
{
  std::vector<EffectLine> lines;
  const auto add = [&](float value, const char* key, const std::string& name)
  {
    const int points = rounded(value);
    if (points == 0) return;
    lines.emplace_back(name.empty() ? fmt::sprintf(LOC(key), points)
                                    : fmt::sprintf(LOC(key), name, points),
                       points > 0 ? 1 : -1);
  };
  add(effects.morale, "INBOX_DILEMMA_EFFECT_MORALE", subject);
  add(effects.trust, "INBOX_DILEMMA_EFFECT_TRUST", subject);
  add(effects.sharpness, "INBOX_DILEMMA_EFFECT_SHARPNESS", subject);
  add(effects.other_morale, "INBOX_DILEMMA_EFFECT_MORALE", other);
  add(effects.other_trust, "INBOX_DILEMMA_EFFECT_TRUST", other);
  add(effects.squad_morale, "INBOX_DILEMMA_EFFECT_SQUAD", std::string());
  if (effects.money != 0)
  {
    lines.emplace_back(
        fmt::sprintf(LOC(effects.money > 0 ? "INBOX_DILEMMA_EFFECT_INCOME"
                                           : "INBOX_DILEMMA_EFFECT_COST"),
                     Format::money(std::abs(effects.money))),
        effects.money > 0 ? 1 : -1);
  }
  return lines;
}

std::optional<int> render(GameController& controller)
{
  const auto dilemma = controller.getOpenDilemma();
  if (!dilemma) return std::nullopt;
  const Theme::Palette& palette = Theme::palette();
  const std::string subject = playerName(controller, dilemma->subject);
  const std::string other =
      dilemma->kind == StoryKind::TrainingClash
          ? playerName(controller, static_cast<PlayerID>(dilemma->other))
          : std::string();

  // Today included: the moment can still be answered on its last day.
  const int days_left = std::max(
      1, dilemma->expires_day - dayOrdinal(controller.getCurrentDate()) + 1);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(
      palette.faint, "%s",
      fmt::sprintf(Format::plural("INBOX_DILEMMA_EXPIRES", days_left),
                   days_left)
          .c_str());
  ImGui::PopTextWrapPos();

  const bool stacked =
      ImGui::GetContentRegionAvail().x < SIDE_BY_SIDE_WIDTH * Theme::scale();
  std::optional<int> taken;
  if (!ImGui::BeginTable(
          "##dilemma_answers", stacked ? 1 : 2,
          ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX))
    return std::nullopt;
  for (const int option : {0, 1})
  {
    const auto effects = controller.getDilemmaEffects(option);
    if (!effects) continue;
    ImGui::TableNextColumn();
    ImGui::PushID(option);
    const char* label = LOC(Stories::dilemmaOptionKey(dilemma->kind, option));
    for (const EffectLine& line : effectLines(*effects, subject, other))
    {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(line.sign > 0 ? palette.positive : palette.negative,
                         "%s", line.text.c_str());
      ImGui::PopTextWrapPos();
    }
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
    // Neither answer is the "right" one: both get the same weight.
    if (UI::secondaryButton(label) && controller.resolveDilemma(option))
      taken = option;
    ImGui::PopID();
    if (taken.has_value()) break;
  }
  ImGui::EndTable();
  return taken;
}
}  // namespace InboxDilemmaCard
