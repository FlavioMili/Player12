// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_pickers.h"

#include <imgui.h>

namespace SandboxPickers
{
void brain(const char* id, BrainId& brain, float width)
{
  ImGui::SetNextItemWidth(width);
  if (ImGui::BeginCombo(id, brainName(brain).data()))
  {
    for (const BrainId option : ALL_BRAINS)
      if (ImGui::Selectable(brainName(option).data(), option == brain))
        brain = option;
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "The decision-making this side plays with: Aardvark is the inherited "
        "engine, kept as it was; Binturong is the version being developed. "
        "Physics and rules are the same for both.");
}

void body(const char* id, BodyId& body, float width)
{
  ImGui::SetNextItemWidth(width);
  if (ImGui::BeginCombo(id, bodyName(body).data()))
  {
    for (const BodyId option : ALL_BODIES)
      if (ImGui::Selectable(bodyName(option).data(), option == body))
        body = option;
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "How this side's players' bodies carry out what their brain chose: "
        "movement, fitness, kicking, first touch, goalkeeping, injuries. "
        "Aardvark is the inherited engine; Binturong is being developed. "
        "Contests between players, the ball and the rules are the same for "
        "both.");
}

void manager(const char* id, ManagerId& manager, float width)
{
  ImGui::SetNextItemWidth(width);
  if (ImGui::BeginCombo(id, managerName(manager).data()))
  {
    for (const ManagerId option : ALL_MANAGERS)
      if (ImGui::Selectable(managerName(option).data(), option == manager))
        manager = option;
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "The AI manager running this side's touchline when the engine "
        "manages it: substitutions (injuries, fatigue, cards, the score), who "
        "comes on, and the late shouts. Aardvark is the inherited engine; "
        "Binturong is being developed.");
}

void referee(const char* id, RefereeId& referee, float width)
{
  ImGui::SetNextItemWidth(width);
  if (ImGui::BeginCombo(id, refereeName(referee).data()))
  {
    for (const RefereeId option : ALL_REFEREES)
      if (ImGui::Selectable(refereeName(option).data(), option == referee))
        referee = option;
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "The match's referee: how fouls are dealt with (cards, advantage, "
        "free kick or penalty), offside calls and added time. Aardvark is the "
        "inherited engine; Binturong is being developed.");
}

void ballPhysics(const char* id, BallPhysicsId& physics, float width)
{
  ImGui::SetNextItemWidth(width);
  if (ImGui::BeginCombo(id, ballPhysicsName(physics).data()))
  {
    for (const BallPhysicsId option : ALL_BALL_PHYSICS)
      if (ImGui::Selectable(ballPhysicsName(option).data(), option == physics))
        physics = option;
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "How the ball moves on its own once struck: flight, drag, bounces, "
        "rolling and swerve, and the kicks worked out from them. Brains and "
        "bodies predict with the same physics. Aardvark is the inherited "
        "engine; Binturong is being developed.");
}

void outOfPlay(const char* id, OutOfPlayId& version, float width)
{
  ImGui::SetNextItemWidth(width);
  if (ImGui::BeginCombo(id, outOfPlayName(version).data()))
  {
    for (const OutOfPlayId option : ALL_OUT_OF_PLAY)
      if (ImGui::Selectable(outOfPlayName(option).data(), option == version))
        version = option;
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "What happens when the ball leaves play and how play restarts: goals, "
        "the woodwork, corners, goal kicks, throw-ins, and every restart set "
        "up and taken. Who takes it and what he does with it are the brain's. "
        "Aardvark is the inherited engine; Binturong is being developed.");
}
void possession(const char* id, PossessionId& version, float width)
{
  ImGui::SetNextItemWidth(width);
  if (ImGui::BeginCombo(id, possessionName(version).data()))
  {
    for (const PossessionId option : ALL_POSSESSIONS)
      if (ImGui::Selectable(possessionName(option).data(), option == version))
        version = option;
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The possession system for the whole match, independent "
                      "of the brains and bodies. Aardvark preserves the baseline; "
                      "Binturong is being developed. Both currently play identically.");
}
}  // namespace SandboxPickers
