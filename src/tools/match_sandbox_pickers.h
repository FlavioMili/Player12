// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include "model/match_ball_physics.h"
#include "model/match_body.h"
#include "model/match_brain.h"
#include "model/match_manager.h"
#include "model/match_out_of_play.h"
#include "model/match_possession.h"
#include "model/match_referee.h"

/**
 * The sandbox's brain, body, AI manager, referee, ball physics and out of
 * play combos, shared by the match setup and the drills so both explain the
 * choice the same way. Each is the combo alone (no label), @p width wide, with its
 * tooltip; @p id is the ImGui id.
 */
namespace SandboxPickers
{
void brain(const char* id, BrainId& brain, float width);
void body(const char* id, BodyId& body, float width);
void manager(const char* id, ManagerId& manager, float width);
void referee(const char* id, RefereeId& referee, float width);
void ballPhysics(const char* id, BallPhysicsId& physics, float width);
void outOfPlay(const char* id, OutOfPlayId& version, float width);
void possession(const char* id, PossessionId& version, float width);
}  // namespace SandboxPickers
