// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <memory>

#include "database/world/world_document.h"

class GUIScene;
class GUIView;

/**
 * Scenes that lead back to the main menu's career flow. Implemented by the
 * main menu; used by the World editor so it does not depend on the menu.
 */
namespace CareerStart
{
/** Main menu that immediately starts a new career in @p slot with @p world
 * (null: the built-in world), showing the usual loading screen. */
std::unique_ptr<GUIScene> startCareer(
    GUIView* view, int slot, std::shared_ptr<const World::Document> world);
/** Main menu with the new-career slot picker open; the chosen slot starts a
 * career in @p world. */
std::unique_ptr<GUIScene> chooseSlot(
    GUIView* view, std::shared_ptr<const World::Document> world);
/** The plain main menu. */
std::unique_ptr<GUIScene> mainMenu(GUIView* view);
}  // namespace CareerStart
