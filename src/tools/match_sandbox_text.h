#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <memory>
#include <string_view>

namespace SandboxText
{
/** Unformatted equivalents retain ImGui's wrapping, color and tooltip flags. */
inline void wrappedText(const char* text)
{
  const bool needWrap =
      ImGui::GetCurrentContext()->CurrentWindow->DC.TextWrapPos < 0.0F;
  if (needWrap)
  {
    ImGui::PushTextWrapPos(0.0F);
  }
  ImGui::TextUnformatted(text);
  if (needWrap)
  {
    ImGui::PopTextWrapPos();
  }
}

inline void disabledText(const char* text)
{
  ImGui::PushStyleColor(ImGuiCol_Text,
                        ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
}

inline void tooltipText(const char* text)
{
  if (ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePrevious,
                            ImGuiWindowFlags_None))
  {
    ImGui::TextUnformatted(text);
    ImGui::EndTooltip();
  }
}

inline void coloredText(const ImVec4& color, const char* text)
{
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
}

/** Preserve the formatted TreeNode overload's ID independently of its label. */
inline bool treeNode(const char* id, const char* label,
                     ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None)
{
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window->SkipItems)
  {
    return false;
  }
  return ImGui::TreeNodeBehavior(
      window->GetID(id), flags, label,
      std::to_address(std::string_view(label).end()));
}

/** Unformatted BulletText, following the pinned ImGui widget's layout. */
inline void bulletText(const char* text)
{
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window->SkipItems)
  {
    return;
  }
  const ImGuiContext& context = *ImGui::GetCurrentContext();
  const ImGuiStyle& style = context.Style;
  const ImVec2 labelSize = ImGui::CalcTextSize(text, nullptr, false);
  const float padding = style.FramePadding.x * 2.0F;
  const ImVec2 totalSize(
      context.FontSize + (labelSize.x > 0.0F ? labelSize.x + padding : 0.0F),
      labelSize.y);
  ImVec2 position = window->DC.CursorPos;
  position.y += window->DC.CurrLineTextBaseOffset;
  ImGui::ItemSize(totalSize, 0.0F);
  const ImRect bounds(
      position, ImVec2(position.x + totalSize.x, position.y + totalSize.y));
  if (!ImGui::ItemAdd(bounds, 0))
  {
    return;
  }
  const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
  const float halfFont = context.FontSize * 0.5F;
  ImGui::RenderBullet(window->DrawList,
                      ImVec2(bounds.Min.x + style.FramePadding.x + halfFont,
                             bounds.Min.y + halfFont),
                      color);
  ImGui::RenderText(
      ImVec2(bounds.Min.x + context.FontSize + padding, bounds.Min.y), text,
      nullptr, false);
}
}  // namespace SandboxText
