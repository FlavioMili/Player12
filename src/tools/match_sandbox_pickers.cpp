// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_pickers.h"

#include <imgui.h>

#include <optional>

#include "gui/widgets/theme.h"
#include "tools/match_sandbox_setup.h"
#include "tools/match_sandbox_text.h"

namespace SandboxPickers
{
namespace
{
constexpr ImVec4 SELECTED_TEXT{0.608F, 0.816F, 0.420F, 1.0F};
constexpr float OPTION_HEIGHT = 32.0F;
constexpr float LABEL_COLUMN_WEIGHT = 2.0F;

template <typename Version>
struct VersionChoices
{
  Version aardvark{};
  Version binturong{};
  /** Only the brain has a third version so far. */
  std::optional<Version> capybara;
};

template <typename Version>
void versionRow(const char* label, Version& current,
                VersionChoices<Version> choices, const char* description)
{
  ImGui::PushID(label);
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  if (ImGui::IsItemHovered())
  {
    SandboxText::tooltipText(description);
  }
  const auto option = [&](const char* name, Version version)
  {
    ImGui::TableNextColumn();
    const bool selected = current == version;
    if (selected)
    {
      ImGui::PushStyleColor(ImGuiCol_Text, SELECTED_TEXT);
    }
    if (ImGui::Selectable(name, selected, 0,
                          ImVec2(0.0F, OPTION_HEIGHT * Theme::scale())))
    {
      current = version;
    }
    if (selected)
    {
      ImGui::PopStyleColor();
    }
  };
  option("Aardvark", choices.aardvark);
  option("Binturong", choices.binturong);
  if (choices.capybara)
  {
    option("Capybara", *choices.capybara);
  }
  else
  {
    ImGui::TableNextColumn();
  }
  ImGui::PopID();
}
}  // namespace

void engineVersionsTable()
{
  if (!ImGui::BeginTable(
          "##engine_versions", 4,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
  {
    return;
  }
  ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch,
                          LABEL_COLUMN_WEIGHT);
  ImGui::TableSetupColumn("Aardvark", ImGuiTableColumnFlags_WidthStretch, 1.0F);
  ImGui::TableSetupColumn("Binturong", ImGuiTableColumnFlags_WidthStretch,
                          1.0F);
  ImGui::TableSetupColumn("Capybara", ImGuiTableColumnFlags_WidthStretch, 1.0F);
  ImGui::TableHeadersRow();
  for (std::size_t side = 0; side < 2; ++side)
  {
    versionRow(side == 0 ? "Home brain" : "Away brain",
               sandboxBrains().at(side),
               {.aardvark = BrainId::AARDVARK,
                .binturong = BrainId::BINTURONG,
                .capybara = BrainId::CAPYBARA},
               "Player decision-making.");
    versionRow(side == 0 ? "Home body" : "Away body", sandboxBodies().at(side),
               {.aardvark = BodyId::AARDVARK, .binturong = BodyId::BINTURONG},
               "Movement, fitness, kicking, first touch and goalkeeping.");
    versionRow(
        side == 0 ? "Home manager" : "Away manager", sandboxManagers().at(side),
        {.aardvark = ManagerId::AARDVARK, .binturong = ManagerId::BINTURONG},
        "AI touchline decisions, substitutions and shouts.");
  }
  versionRow(
      "Handshake", sandboxHandshake(),
      {.aardvark = HandshakeId::AARDVARK, .binturong = HandshakeId::BINTURONG},
      "How game attributes become match inputs.");
  versionRow(
      "Referee", sandboxReferee(),
      {.aardvark = RefereeId::AARDVARK, .binturong = RefereeId::BINTURONG},
      "Fouls, cards, advantage, offside and added time.");
  versionRow("Ball physics", sandboxBallPhysics(),
             {.aardvark = BallPhysicsId::AARDVARK,
              .binturong = BallPhysicsId::BINTURONG},
             "Flight, drag, bounces, rolling and swerve.");
  versionRow(
      "Out of play", sandboxOutOfPlay(),
      {.aardvark = OutOfPlayId::AARDVARK, .binturong = OutOfPlayId::BINTURONG},
      "Goals, boundaries and restart rules.");
  versionRow(
      "Possession", sandboxPossession(),
      {.aardvark = PossessionId::AARDVARK,
       .binturong = PossessionId::BINTURONG},
      "Possession system, independent of the selected brains and bodies.");
  ImGui::EndTable();
}

}  // namespace SandboxPickers
