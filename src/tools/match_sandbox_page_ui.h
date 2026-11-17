// Football Management Project — see LICENSE in the project root.
#pragma once

#include <imgui.h>

#include <algorithm>
#include <utility>

#include "tools/match_sandbox_typography.h"

namespace SandboxPageUI
{
// Combo popups inherit WindowPadding.y from their page, often zero for columns.
// Reserve list padding independently of the compact page containers.
template <typename... Args>
inline bool beginCombo(Args&&... args)
{
  ImGui::PushStyleVarY(ImGuiStyleVar_WindowPadding,
                       8.0F * ImGui::GetStyle().FontScaleDpi);
  const bool open = ImGui::BeginCombo(std::forward<Args>(args)...);
  ImGui::PopStyleVar();
  return open;
}
template <typename... Args>
inline bool combo(Args&&... args)
{
  ImGui::PushStyleVarY(ImGuiStyleVar_WindowPadding,
                       8.0F * ImGui::GetStyle().FontScaleDpi);
  const bool changed = ImGui::Combo(std::forward<Args>(args)...);
  ImGui::PopStyleVar();
  return changed;
}

inline constexpr ImU32 PAGE = IM_COL32(22, 19, 16, 255);
inline constexpr ImU32 LIGHT = IM_COL32(42, 35, 28, 255);
inline constexpr ImU32 MID = IM_COL32(31, 26, 21, 255);
inline constexpr ImU32 DARK = IM_COL32(15, 13, 11, 255);
inline constexpr ImU32 CREAM = IM_COL32(240, 234, 226, 255);
inline constexpr ImU32 MUTED = IM_COL32(160, 149, 138, 255);
inline constexpr ImU32 LIME = IM_COL32(155, 208, 107, 255);
inline constexpr ImU32 INK = IM_COL32(18, 19, 21, 255);
inline constexpr ImU32 RULE = IM_COL32(58, 50, 41, 255);

// Two visible identity colours, following the track/trim treatment in
// player-profile-2 SPEC section 4.2. Cream/lime are the sandbox identity pair;
// a near-black or brown rail disappears into these neutral surfaces.
inline constexpr ImU32 SCROLL_TRACK = CREAM;
inline constexpr ImU32 SCROLL_GRAB = LIME;
// Mix(trim, white, 0.15), rounded to 8-bit channels.
inline constexpr ImU32 SCROLL_HOVER = IM_COL32(170, 215, 129, 255);

inline float activityPageWidth(float available, float scale, float gutter)
{
  for (float width : {1152.0F, 1072.0F, 886.0F, 806.0F, 660.0F})
  {
    if (available >= width * scale + gutter)
    {
      return (width * scale) + gutter;
    }
  }
  return available;
}

inline float toolsPageWidth(float available, float scale, float gutter)
{
  for (float width : {1332.0F, 886.0F, 806.0F, 440.0F})
  {
    if (available >= width * scale + gutter)
    {
      return (width * scale) + gutter;
    }
  }
  return available;
}

inline float matchPageWidth(float available, float scale, float gutter)
{
  for (float width : {1326.0F, 1206.0F, 1062.0F, 660.0F})
  {
    if (available >= width * scale + gutter)
    {
      return (width * scale) + gutter;
    }
  }
  return available;
}

struct ActivityLayout
{
  bool columns;
  float details, catalogue, page;
  ActivityLayout(float available, float scale)
  {
    const float usable = std::max(scale, available - (6 * scale));
    columns = usable >= 1072 * scale;
    details = columns ? (usable >= 1152 * scale ? 886 : 806) * scale : usable;
    if (!columns)
    {
      for (float width : {886.0F, 806.0F, 660.0F})
      {
        if (usable >= width * scale)
        {
          details = width * scale;
          break;
        }
      }
    }
    catalogue = columns ? 260 * scale : details;
    page = columns ? catalogue + (6 * scale) + details : details;
  }
};

// Scoped to the sandbox launch pages. Existing match/run overlays retain their
// own theme and controls; no global appearance settings are changed.
struct Style
{
  Style(ImFont* regular, float scale)
  {
    ImGui::PushFont(regular, 16);
    const struct
    {
      ImGuiCol id;
      ImU32 value;
    } colors[] = {
        {.id = ImGuiCol_Text, .value = CREAM},
        {.id = ImGuiCol_TextDisabled, .value = MUTED},
        {.id = ImGuiCol_ChildBg, .value = PAGE},
        {.id = ImGuiCol_PopupBg, .value = MID},
        {.id = ImGuiCol_Border, .value = IM_COL32(92, 81, 68, 255)},
        {.id = ImGuiCol_FrameBg, .value = DARK},
        {.id = ImGuiCol_FrameBgHovered, .value = MID},
        {.id = ImGuiCol_FrameBgActive, .value = IM_COL32(46, 39, 32, 255)},
        {.id = ImGuiCol_CheckMark, .value = INK},
        {.id = ImGuiCol_CheckboxSelectedBg, .value = LIME},
        {.id = ImGuiCol_SliderGrab, .value = LIME},
        {.id = ImGuiCol_SliderGrabActive,
         .value = IM_COL32(170, 215, 130, 255)},
        {.id = ImGuiCol_Button, .value = DARK},
        {.id = ImGuiCol_ButtonHovered, .value = MID},
        {.id = ImGuiCol_ButtonActive, .value = IM_COL32(46, 39, 32, 255)},
        {.id = ImGuiCol_Header, .value = IM_COL32(46, 39, 32, 255)},
        {.id = ImGuiCol_HeaderHovered, .value = MID},
        {.id = ImGuiCol_HeaderActive, .value = MID},
        {.id = ImGuiCol_Separator, .value = RULE},
        {.id = ImGuiCol_TableHeaderBg, .value = MID},
        {.id = ImGuiCol_TableBorderStrong, .value = RULE},
        {.id = ImGuiCol_TableBorderLight, .value = RULE},
        {.id = ImGuiCol_TableRowBg, .value = IM_COL32(0, 0, 0, 0)},
        {.id = ImGuiCol_TableRowBgAlt, .value = IM_COL32(255, 255, 255, 5)},
        {.id = ImGuiCol_ScrollbarBg, .value = SCROLL_TRACK},
        {.id = ImGuiCol_ScrollbarGrab, .value = SCROLL_GRAB},
        {.id = ImGuiCol_ScrollbarGrabHovered, .value = SCROLL_HOVER},
        {.id = ImGuiCol_ScrollbarGrabActive, .value = SCROLL_HOVER}};
    for (const auto& color : colors)
    {
      ImGui::PushStyleColor(color.id, color.value);
      ++colorCount;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(8 * scale, 6 * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(6 * scale, 6 * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                        ImVec2(6 * scale, 6 * scale));
  }
  ~Style()
  {
    ImGui::PopStyleVar(10);
    ImGui::PopStyleColor(colorCount);
    ImGui::PopFont();
  }
  Style(const Style&) = delete;
  Style& operator=(const Style&) = delete;

 private:
  int colorCount = 0;
};

inline void tracked(ImDrawList* draw, ImVec2 pos, ImFont* bold,
                    const char* label, float size, float scale,
                    ImU32 color = CREAM, float spacing = 1.5F)
{
  ImGui::PushFont(bold, size);
  for (const char* c = label; *c != 0; ++c)
  {
    draw->AddText(pos, color, c, c + 1);
    pos.x += ImGui::CalcTextSize(c, c + 1).x + spacing * scale;
  }
  ImGui::PopFont();
}

inline void fade(ImDrawList* draw, ImVec2 a, ImVec2 b, float scale)
{
  const ImU32 clear = IM_COL32(22, 19, 16, 0);
  const float f = 6 * scale;
  draw->AddRectFilledMultiColor(a, ImVec2(b.x, a.y + f), PAGE, PAGE, clear,
                                clear);
  draw->AddRectFilledMultiColor(ImVec2(a.x, b.y - f), b, clear, clear, PAGE,
                                PAGE);
  draw->AddRectFilledMultiColor(a, ImVec2(a.x + f, b.y), PAGE, clear, clear,
                                PAGE);
  draw->AddRectFilledMultiColor(ImVec2(b.x - f, a.y), b, clear, PAGE, PAGE,
                                clear);
}

inline void header(const char* title, const char* subtitle, float width,
                   float scale, ImFont* bold, ImFont* regular,
                   const char* context = nullptr, bool fitText = false)
{
  float height = 160 * scale, contextY = 116 * scale, subtitleY = 82 * scale;
  if (fitText)
  {
    ImGui::PushFont(bold, 42);
    subtitleY = std::max(
        subtitleY,
        (18 * scale) +
            ImGui::CalcTextSize(title, nullptr, false, width - (32 * scale)).y +
            (12 * scale));
    ImGui::PopFont();
    ImGui::PushFont(regular, 17);
    const float subtitleHeight =
        ImGui::CalcTextSize(subtitle, nullptr, false, width - (32 * scale)).y;
    ImGui::PopFont();
    height = std::max(height, subtitleY + subtitleHeight + (16 * scale));
    if ((context != nullptr) && (*context != 0))
    {
      contextY = std::max(contextY, subtitleY + subtitleHeight + (8 * scale));
      ImGui::PushFont(regular, 16);
      height = std::max(height, contextY +
                                    ImGui::CalcTextSize(context, nullptr, false,
                                                        width - (32 * scale))
                                        .y +
                                    (16 * scale));
      ImGui::PopFont();
    }
  }
  const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + width, a.y + height);
  auto* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(a, b, LIGHT);
  draw->PushClipRect(a, b, true);
  ImGui::PushFont(bold, 42);
  draw->AddText(bold, ImGui::GetFontSize(),
                ImVec2(a.x + (16 * scale), a.y + (18 * scale)), CREAM, title,
                nullptr, fitText ? width - (32 * scale) : 0);
  ImGui::PopFont();
  ImGui::PushFont(regular, 17);
  draw->AddText(regular, ImGui::GetFontSize(),
                ImVec2(a.x + (16 * scale), a.y + subtitleY), CREAM, subtitle,
                nullptr, width - (32 * scale));
  ImGui::PopFont();
  if ((context != nullptr) && (*context != 0))
  {
    ImGui::PushFont(regular, 16);
    draw->AddText(regular, ImGui::GetFontSize(),
                  ImVec2(a.x + (16 * scale), a.y + contextY), MUTED, context,
                  nullptr, width - (32 * scale));
    ImGui::PopFont();
  }
  fade(draw, a, b, scale);
  draw->AddRectFilled(a, ImVec2(b.x, a.y + (6 * scale)), LIME);
  draw->PopClipRect();
  ImGui::Dummy(ImVec2(width, height));
}

inline void beginCard(const char* id, const char* title, float width,
                      ImU32 fill, float scale, ImFont* bold)
{
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(16 * scale, 12 * scale));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, fill);
  ImGui::BeginChild(
      id, ImVec2(width, 0),
      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize |
          ImGuiChildFlags_AlwaysUseWindowPadding,
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
  tracked(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), bold, title,
          16, scale);
  ImGui::Dummy(ImVec2(0, 26 * scale));
}

inline void endCard(float scale)
{
  const auto a = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
  fade(ImGui::GetWindowDrawList(), a, ImVec2(a.x + size.x, a.y + size.y),
       scale);
  ImGui::EndChild();
}

inline bool choice(const char* label, bool selected, float scale, ImFont* bold)
{
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  const bool clicked = ImGui::InvisibleButton(label, ImVec2(width, 36 * scale),
                                              ImGuiButtonFlags_EnableNav);
  auto* draw = ImGui::GetWindowDrawList();
  if (selected || ImGui::IsItemHovered())
  {
    draw->AddRectFilled(a, ImVec2(a.x + width, a.y + (36 * scale)),
                        selected ? IM_COL32(46, 39, 32, 255) : MID);
  }
  if (selected)
  {
    draw->AddRectFilled(a, ImVec2(a.x + (3 * scale), a.y + (36 * scale)),
                        CREAM);
  }
  if (ImGui::IsItemFocused())
  {
    draw->AddRect(a, ImVec2(a.x + width, a.y + (36 * scale)), LIME);
  }
  draw->PushClipRect(a, ImVec2(a.x + width, a.y + (36 * scale)), true);
  SandboxTypography::label(draw, selected ? bold : ImGui::GetFont(), 16, label,
                           a, ImVec2(a.x + width, a.y + (36 * scale)), CREAM, 0,
                           false, 12 * scale);
  draw->PopClipRect();
  return clicked;
}

inline float buttonWidth(const char* label, float scale, ImFont* bold)
{
  ImGui::PushFont(bold, 14);
  const auto bounds =
      SandboxTypography::labelBounds(bold, ImGui::GetFontSize(), label, scale);
  ImGui::PopFont();
  return bounds.max.x - bounds.min.x + (20 * scale);
}

inline bool button(const char* label, bool primary, float scale, ImFont* bold)
{
  const float width = buttonWidth(label, scale, bold);
  ImGui::PushFont(bold, 14);
  ImGui::PushStyleColor(ImGuiCol_Button, primary ? LIME : DARK);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        primary ? IM_COL32(170, 215, 130, 255) : MID);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        primary ? IM_COL32(130, 185, 85, 255) : MID);
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
  const bool pressed = ImGui::Button(label, ImVec2(width, 32 * scale));
  const ImVec2 a = ImGui::GetItemRectMin();
  ImGui::PopStyleColor(4);
  SandboxTypography::label(
      ImGui::GetWindowDrawList(), bold, 14, label, a, ImGui::GetItemRectMax(),
      ImGui::GetColorU32(ImGui::ColorConvertU32ToFloat4(primary ? INK : CREAM)),
      scale);
  ImGui::PopFont();
  return pressed;
}
}  // namespace SandboxPageUI
