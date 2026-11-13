// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_pickers.h"

#include <imgui.h>

#include "gui/widgets/theme.h"
#include "tools/match_sandbox_setup.h"

namespace SandboxPickers
{
namespace
{
template <typename Version>
void versionRow(const char* label, Version& current, Version aardvark,
                Version binturong, const char* description)
{
  ImGui::PushID(label);
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", description);
  const auto option = [&](const char* name, Version version)
  {
    ImGui::TableNextColumn();
    const bool selected = current == version;
    if (selected)
      ImGui::PushStyleColor(ImGuiCol_Text,
                            ImVec4(0.608f, 0.816f, 0.420f, 1.0f));
    if (ImGui::Selectable(name, selected, 0,
                          ImVec2(0.0f, 32.0f * Theme::scale())))
      current = version;
    if (selected) ImGui::PopStyleColor();
  };
  option("Aardvark", aardvark);
  option("Binturong", binturong);
  ImGui::PopID();
}
}  // namespace

void engineVersionsTable()
{
  if (!ImGui::BeginTable(
          "##engine_versions", 3,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    return;
  ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 2.0f);
  ImGui::TableSetupColumn("Aardvark", ImGuiTableColumnFlags_WidthStretch, 1.0f);
  ImGui::TableSetupColumn("Binturong", ImGuiTableColumnFlags_WidthStretch,
                          1.0f);
  ImGui::TableHeadersRow();
  for (std::size_t side = 0; side < 2; ++side)
  {
    versionRow(side == 0 ? "Home brain" : "Away brain", sandboxBrains()[side],
               BrainId::AARDVARK, BrainId::BINTURONG,
               "Player decision-making.");
    versionRow(side == 0 ? "Home body" : "Away body", sandboxBodies()[side],
               BodyId::AARDVARK, BodyId::BINTURONG,
               "Movement, fitness, kicking, first touch and goalkeeping.");
    versionRow(side == 0 ? "Home manager" : "Away manager",
               sandboxManagers()[side], ManagerId::AARDVARK,
               ManagerId::BINTURONG,
               "AI touchline decisions, substitutions and shouts.");
  }
  versionRow("Handshake", sandboxHandshake(), HandshakeId::AARDVARK,
             HandshakeId::BINTURONG,
             "How game attributes become match inputs.");
  versionRow("Referee", sandboxReferee(), RefereeId::AARDVARK,
             RefereeId::BINTURONG,
             "Fouls, cards, advantage, offside and added time.");
  versionRow("Ball physics", sandboxBallPhysics(), BallPhysicsId::AARDVARK,
             BallPhysicsId::BINTURONG,
             "Flight, drag, bounces, rolling and swerve.");
  versionRow("Out of play", sandboxOutOfPlay(), OutOfPlayId::AARDVARK,
             OutOfPlayId::BINTURONG, "Goals, boundaries and restart rules.");
  versionRow(
      "Possession", sandboxPossession(), PossessionId::AARDVARK,
      PossessionId::BINTURONG,
      "Possession system, independent of the selected brains and bodies.");
  ImGui::EndTable();
}

}  // namespace SandboxPickers
