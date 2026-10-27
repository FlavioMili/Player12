// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/set_pieces_dialog.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <utility>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/inbox.h"

namespace
{
constexpr const char* POPUP_ID = "##set_pieces";
constexpr float DIALOG_WIDTH = 560.0f;

std::string nameOf(const GameController& controller, PlayerID id)
{
  const auto data = controller.getGameData();
  const auto player = data && id != 0 ? data->getPlayer(id) : std::nullopt;
  return player ? player->get().getName() : std::string();
}
}  // namespace

std::string SetPiecesDialog::captainSummary(const GameController& controller)
{
  const std::string captain = nameOf(
      controller, controller.getEffectiveSetPieceTaker(SetPieceDuty::Captain));
  return captain.empty()
             ? std::string()
             : formatLocalized("SET_PIECES_CAPTAIN_SUMMARY", {captain});
}

void SetPiecesDialog::rebuild(const GameController& controller)
{
  const auto managed = controller.getManagedTeam();
  for (auto& list : choices) list.clear();
  if (!managed) return;
  const Lineup& lineup = managed->get().getLineup();
  const std::vector<const Player*> xi = lineup.starters();

  // Leadership: the dressing room's standing first, then experience.
  std::vector<const Player*> leaders;
  for (const LeaderInfo& leader : controller.getDressingRoom().leaders)
    for (const Player* player : xi)
      if (player->getId() == leader.player_id) leaders.push_back(player);
  std::vector<const Player*> rest = xi;
  std::ranges::sort(rest, [](const Player* a, const Player* b)
                    { return a->getAge() > b->getAge(); });
  for (const Player* player : rest)
    if (std::ranges::find(leaders, player) == leaders.end())
      leaders.push_back(player);

  for (std::size_t index = 0; index < SET_PIECE_DUTY_COUNT; ++index)
  {
    const auto duty = static_cast<SetPieceDuty>(index);
    std::vector<Choice>& list = choices[index];
    if (SetPieces::isLeadership(duty))
    {
      for (const Player* player : leaders)
        list.emplace_back(
            player->getId(),
            std::format("{}  \u{B7}  {}", player->getName(), player->getAge()));
      // What "automatic" means: the top of the hierarchy (the next one for
      // the vice-captain, after whoever captains).
      const PlayerID captain =
          controller.getEffectiveSetPieceTaker(SetPieceDuty::Captain);
      PlayerID pick = leaders.empty() ? 0 : leaders.front()->getId();
      if (duty == SetPieceDuty::ViceCaptain)
      {
        pick = 0;
        for (const Player* player : leaders)
          if (player->getId() != captain)
          {
            pick = player->getId();
            break;
          }
      }
      automatic[index] =
          formatLocalized("SET_PIECES_AUTOMATIC", {nameOf(controller, pick)});
    }
    else
    {
      std::vector<std::pair<float, const Player*>> ranked;
      for (const Player* player : xi)
        if (const float value = SetPieces::score(duty, *player); value > 0.0f)
          ranked.emplace_back(value, player);
      std::ranges::sort(ranked, [](const auto& a, const auto& b)
                        { return a.first > b.first; });
      for (const auto& [value, player] : ranked)
        list.emplace_back(
            player->getId(),
            std::format("{}  \u{B7}  {:.0f}", player->getName(), value));
      const Player* best = SetPieces::best(duty, xi);
      automatic[index] = formatLocalized(
          "SET_PIECES_AUTOMATIC",
          {best ? best->getName() : std::string(LOC("SET_PIECES_NOBODY"))});
    }
    const PlayerID designated = lineup.getDesignated(duty);
    const bool starts = lineup.isStarter(designated);
    if (designated == 0)
      current[index] = automatic[index];
    else if (starts)
      current[index] = nameOf(controller, designated);
    else
      current[index] = formatLocalized("SET_PIECES_NOT_STARTING",
                                       {nameOf(controller, designated)});
  }
}

bool SetPiecesDialog::render(GameController& controller)
{
  if (open_requested)
  {
    open_requested = false;
    rebuild(controller);
    ImGui::OpenPopup(POPUP_ID);
  }
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float width =
      std::min(DIALOG_WIDTH * Theme::scale(), viewport->WorkSize.x * 0.92f);
  ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal(POPUP_ID, nullptr,
                              ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_AlwaysAutoResize))
    return false;
  const Theme::Palette& palette = Theme::palette();
  {
    Theme::ScopedText title(Theme::Text::TITLE);
    ImGui::TextUnformatted(LOC("SET_PIECES_TITLE"));
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.muted, "%s", LOC("SET_PIECES_HELP"));
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));

  bool changed = false;
  const float labelWidth = std::min(170.0f * Theme::scale(),
                                    ImGui::GetContentRegionAvail().x * 0.4f);
  for (std::size_t index = 0; index < SET_PIECE_DUTY_COUNT; ++index)
  {
    const auto duty = static_cast<SetPieceDuty>(index);
    if (index ==
        static_cast<std::size_t>(std::to_underlying(SetPieceDuty::Penalties)))
    {
      ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
      UI::sectionLabel(LOC("SET_PIECES_TAKERS"));
    }
    ImGui::PushID(static_cast<int>(index));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(palette.muted, "%s", LOC(SetPieces::dutyKey(duty)));
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##taker", current[index].c_str(),
                          ImGuiComboFlags_HeightLarge))
    {
      const PlayerID designated = controller.getSetPieceDesignation(duty);
      if (ImGui::Selectable(automatic[index].c_str(), designated == 0))
        changed |= controller.setSetPieceDesignation(duty, 0);
      for (const Choice& choice : choices[index])
        if (ImGui::Selectable(choice.label.c_str(), designated == choice.id))
          changed |= controller.setSetPieceDesignation(duty, choice.id);
      ImGui::EndCombo();
    }
    ImGui::PopID();
  }
  if (changed) rebuild(controller);

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));
  if (UI::secondaryButton(LOC("SET_PIECES_AUTO_PICK")))
  {
    controller.autoPickSetPieces();
    rebuild(controller);
    changed = true;
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    ImGui::SetTooltip("%s", LOC("SET_PIECES_AUTO_PICK_HELP"));
  ImGui::SameLine();
  if (UI::secondaryButton(LOC("SET_PIECES_RESET")))
  {
    for (std::size_t index = 0; index < SET_PIECE_DUTY_COUNT; ++index)
      controller.setSetPieceDesignation(static_cast<SetPieceDuty>(index), 0);
    rebuild(controller);
    changed = true;
  }
  const char* done = LOC("SET_PIECES_DONE");
  ImGui::SameLine(ImGui::GetContentRegionMax().x - UI::buttonWidth(done));
  if (UI::primaryButton(done) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
      close_requested)
    ImGui::CloseCurrentPopup();
  close_requested = false;
  ImGui::EndPopup();
  return changed;
}
