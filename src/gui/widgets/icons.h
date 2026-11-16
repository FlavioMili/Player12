// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <cstdint>

/**
 * @brief Small vector icons drawn with ImDrawList (no icon font needed).
 */
namespace UI
{

/** @brief Available icons. */
enum class Icon : uint8_t
{
  HOME,
  SQUAD,
  LINEUP,
  TACTICS,
  FIXTURES,
  STANDINGS,
  TRANSFERS,
  FINANCES,
  SAVE,
  SETTINGS,
  EXIT,
  SEARCH,
  EDIT,
  INBOX,
  CLUB
};

/**
 * @brief Draws an icon centred on a point.
 * @param drawList Target draw list.
 * @param icon Icon to draw.
 * @param center Centre in screen coordinates.
 * @param size Icon box size in pixels.
 * @param color Stroke colour.
 */
void drawIcon(ImDrawList* drawList, Icon icon, ImVec2 center, float size,
              ImU32 color);

}  // namespace UI
