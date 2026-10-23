// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/widgets/icons.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace
{
constexpr float PI = std::numbers::pi_v<float>;

void arrow(ImDrawList* drawList, ImVec2 from, ImVec2 to, float head,
           ImU32 color, float thickness)
{
  drawList->AddLine(from, to, color, thickness);
  const float direction = to.x > from.x ? -1.0f : 1.0f;
  drawList->AddLine(to, ImVec2(to.x + direction * head, to.y - head), color,
                    thickness);
  drawList->AddLine(to, ImVec2(to.x + direction * head, to.y + head), color,
                    thickness);
}
}  // namespace

namespace UI
{

void drawIcon(ImDrawList* drawList, Icon icon, ImVec2 c, float s, ImU32 color)
{
  const float h = s * 0.5f;
  const float t = std::max(1.3f, s / 11.0f);
  switch (icon)
  {
    case Icon::HOME:
    {
      const std::array<ImVec2, 3> roof = {ImVec2(c.x - h, c.y - 0.02f * s),
                                          ImVec2(c.x, c.y - h),
                                          ImVec2(c.x + h, c.y - 0.02f * s)};
      drawList->AddPolyline(roof.data(), static_cast<int>(roof.size()), color,
                            ImDrawFlags_None, t);
      drawList->AddRect(ImVec2(c.x - 0.33f * s, c.y - 0.12f * s),
                        ImVec2(c.x + 0.33f * s, c.y + h), color, 0.0f, 0, t);
      drawList->AddRectFilled(ImVec2(c.x - 0.08f * s, c.y + 0.12f * s),
                              ImVec2(c.x + 0.08f * s, c.y + h), color);
      break;
    }
    case Icon::SQUAD:
    {
      drawList->AddCircle(ImVec2(c.x - 0.15f * s, c.y - 0.2f * s), 0.14f * s,
                          color, 16, t);
      drawList->AddCircle(ImVec2(c.x + 0.22f * s, c.y - 0.16f * s), 0.11f * s,
                          color, 16, t);
      drawList->PathArcTo(ImVec2(c.x - 0.15f * s, c.y + 0.45f * s), 0.3f * s,
                          PI * 1.05f, PI * 1.95f, 16);
      drawList->PathStroke(color, ImDrawFlags_None, t);
      drawList->PathArcTo(ImVec2(c.x + 0.24f * s, c.y + 0.42f * s), 0.22f * s,
                          PI * 1.1f, PI * 1.95f, 12);
      drawList->PathStroke(color, ImDrawFlags_None, t);
      break;
    }
    case Icon::LINEUP:
    {
      drawList->AddRect(ImVec2(c.x - h, c.y - 0.36f * s),
                        ImVec2(c.x + h, c.y + 0.36f * s), color, 2.0f, 0, t);
      drawList->AddLine(ImVec2(c.x, c.y - 0.36f * s),
                        ImVec2(c.x, c.y + 0.36f * s), color, t * 0.8f);
      drawList->AddCircleFilled(ImVec2(c.x - 0.28f * s, c.y), 0.07f * s, color);
      drawList->AddCircleFilled(ImVec2(c.x + 0.24f * s, c.y - 0.16f * s),
                                0.07f * s, color);
      drawList->AddCircleFilled(ImVec2(c.x + 0.24f * s, c.y + 0.16f * s),
                                0.07f * s, color);
      break;
    }
    case Icon::TACTICS:
    {
      const std::array<float, 3> knobs = {0.25f, 0.65f, 0.4f};
      for (size_t row = 0; row < knobs.size(); ++row)
      {
        const float y = c.y + (static_cast<float>(row) - 1.0f) * 0.32f * s;
        drawList->AddLine(ImVec2(c.x - h, y), ImVec2(c.x + h, y), color,
                          t * 0.85f);
        drawList->AddCircleFilled(ImVec2(c.x - h + knobs[row] * s, y), 0.1f * s,
                                  color);
      }
      break;
    }
    case Icon::FIXTURES:
    {
      drawList->AddRect(ImVec2(c.x - 0.42f * s, c.y - 0.34f * s),
                        ImVec2(c.x + 0.42f * s, c.y + 0.44f * s), color, 2.0f,
                        0, t);
      drawList->AddLine(ImVec2(c.x - 0.42f * s, c.y - 0.12f * s),
                        ImVec2(c.x + 0.42f * s, c.y - 0.12f * s), color, t);
      drawList->AddLine(ImVec2(c.x - 0.2f * s, c.y - h),
                        ImVec2(c.x - 0.2f * s, c.y - 0.26f * s), color, t);
      drawList->AddLine(ImVec2(c.x + 0.2f * s, c.y - h),
                        ImVec2(c.x + 0.2f * s, c.y - 0.26f * s), color, t);
      for (int row = 0; row < 2; ++row)
        for (int column = 0; column < 3; ++column)
          drawList->AddCircleFilled(
              ImVec2(c.x + (static_cast<float>(column) - 1.0f) * 0.22f * s,
                     c.y + (0.06f + 0.2f * static_cast<float>(row)) * s),
              0.05f * s, color);
      break;
    }
    case Icon::STANDINGS:
    {
      const std::array<float, 3> heights = {0.55f, 0.9f, 0.38f};
      for (size_t bar = 0; bar < heights.size(); ++bar)
      {
        const float left = c.x - h + static_cast<float>(bar) * 0.34f * s;
        drawList->AddRectFilled(ImVec2(left, c.y + h - heights[bar] * s),
                                ImVec2(left + 0.26f * s, c.y + h), color, 1.5f);
      }
      break;
    }
    case Icon::TRANSFERS:
    {
      arrow(drawList, ImVec2(c.x - h, c.y - 0.2f * s),
            ImVec2(c.x + h, c.y - 0.2f * s), 0.18f * s, color, t);
      arrow(drawList, ImVec2(c.x + h, c.y + 0.2f * s),
            ImVec2(c.x - h, c.y + 0.2f * s), 0.18f * s, color, t);
      break;
    }
    case Icon::FINANCES:
    {
      drawList->AddCircle(c, 0.44f * s, color, 24, t);
      const float fontSize = s * 0.62f;
      const char* symbol = "€";
      const ImVec2 textSize =
          ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, symbol);
      drawList->AddText(
          ImGui::GetFont(), fontSize,
          ImVec2(c.x - textSize.x * 0.5f, c.y - textSize.y * 0.5f), color,
          symbol);
      break;
    }
    case Icon::SAVE:
    {
      drawList->AddRect(ImVec2(c.x - 0.42f * s, c.y - 0.42f * s),
                        ImVec2(c.x + 0.42f * s, c.y + 0.42f * s), color, 2.0f,
                        0, t);
      drawList->AddRectFilled(ImVec2(c.x - 0.22f * s, c.y - 0.42f * s),
                              ImVec2(c.x + 0.18f * s, c.y - 0.12f * s), color);
      drawList->AddRect(ImVec2(c.x - 0.26f * s, c.y + 0.06f * s),
                        ImVec2(c.x + 0.26f * s, c.y + 0.42f * s), color, 0.0f,
                        0, t * 0.8f);
      break;
    }
    case Icon::SETTINGS:
    {
      drawList->AddCircle(c, 0.2f * s, color, 16, t);
      for (int tooth = 0; tooth < 8; ++tooth)
      {
        const float angle = static_cast<float>(tooth) * PI * 0.25f;
        drawList->AddLine(ImVec2(c.x + std::cos(angle) * 0.3f * s,
                                 c.y + std::sin(angle) * 0.3f * s),
                          ImVec2(c.x + std::cos(angle) * 0.46f * s,
                                 c.y + std::sin(angle) * 0.46f * s),
                          color, t * 1.3f);
      }
      break;
    }
    case Icon::EXIT:
    {
      const std::array<ImVec2, 4> door = {
          ImVec2(c.x + 0.05f * s, c.y - 0.42f * s),
          ImVec2(c.x - 0.42f * s, c.y - 0.42f * s),
          ImVec2(c.x - 0.42f * s, c.y + 0.42f * s),
          ImVec2(c.x + 0.05f * s, c.y + 0.42f * s)};
      drawList->AddPolyline(door.data(), static_cast<int>(door.size()), color,
                            ImDrawFlags_None, t);
      arrow(drawList, ImVec2(c.x - 0.12f * s, c.y), ImVec2(c.x + h, c.y),
            0.16f * s, color, t);
      break;
    }
    case Icon::INBOX:
    {
      drawList->AddRect(ImVec2(c.x - h, c.y - 0.34f * s),
                        ImVec2(c.x + h, c.y + 0.34f * s), color, 2.0f, 0, t);
      const std::array<ImVec2, 3> flap = {ImVec2(c.x - h, c.y - 0.32f * s),
                                          ImVec2(c.x, c.y + 0.04f * s),
                                          ImVec2(c.x + h, c.y - 0.32f * s)};
      drawList->AddPolyline(flap.data(), static_cast<int>(flap.size()), color,
                            ImDrawFlags_None, t);
      break;
    }
    case Icon::CLUB:
    {
      const std::array<ImVec2, 6> shield = {
          ImVec2(c.x - 0.4f * s, c.y - 0.4f * s),
          ImVec2(c.x + 0.4f * s, c.y - 0.4f * s),
          ImVec2(c.x + 0.4f * s, c.y + 0.02f * s),
          ImVec2(c.x, c.y + h),
          ImVec2(c.x - 0.4f * s, c.y + 0.02f * s),
          ImVec2(c.x - 0.4f * s, c.y - 0.4f * s)};
      drawList->AddPolyline(shield.data(), static_cast<int>(shield.size()),
                            color, ImDrawFlags_None, t);
      drawList->AddLine(ImVec2(c.x, c.y - 0.4f * s), ImVec2(c.x, c.y + h),
                        color, t * 0.8f);
      break;
    }
    case Icon::EDIT:
    {
      const std::array<ImVec2, 5> pencil = {
          ImVec2(c.x - .4f * s, c.y + .4f * s),
          ImVec2(c.x - .32f * s, c.y + .12f * s),
          ImVec2(c.x + .25f * s, c.y - .45f * s),
          ImVec2(c.x + .45f * s, c.y - .25f * s),
          ImVec2(c.x - .12f * s, c.y + .32f * s)};
      drawList->AddPolyline(pencil.data(), static_cast<int>(pencil.size()),
                            color, ImDrawFlags_Closed, t);
      break;
    }
    case Icon::SEARCH:
    {
      drawList->AddCircle(ImVec2(c.x - 0.08f * s, c.y - 0.08f * s), 0.28f * s,
                          color, 20, t);
      drawList->AddLine(ImVec2(c.x + 0.13f * s, c.y + 0.13f * s),
                        ImVec2(c.x + 0.42f * s, c.y + 0.42f * s), color,
                        t * 1.3f);
      break;
    }
  }
}

}  // namespace UI
