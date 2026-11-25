// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/widgets/text.h"

#include <imgui_internal.h>

// Each mirrors its ImGui counterpart (imgui_widgets.cpp, imgui.cpp), with the
// text already formatted: ImGui::TextV draws through the same TextEx path as
// TextUnformatted.
namespace UI::TextDetail
{
void plain(const std::string& text) { ImGui::TextUnformatted(text.c_str()); }

void wrapped(const std::string& text)
{
  // Keeps a wrap position the caller already pushed, as TextWrappedV does.
  const bool needWrap = ImGui::GetCurrentWindow()->DC.TextWrapPos < 0.0F;
  if (needWrap)
  {
    ImGui::PushTextWrapPos(0.0F);
  }
  plain(text);
  if (needWrap)
  {
    ImGui::PopTextWrapPos();
  }
}

void disabled(const std::string& text)
{
  ImGui::PushStyleColor(ImGuiCol_Text,
                        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  plain(text);
  ImGui::PopStyleColor();
}

void coloured(const ImVec4& colour, const std::string& text)
{
  ImGui::PushStyleColor(ImGuiCol_Text, colour);
  plain(text);
  ImGui::PopStyleColor();
}

void tooltip(const std::string& text)
{
  if (!ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePrevious,
                             ImGuiWindowFlags_None))
  {
    return;
  }
  plain(text);
  ImGui::EndTooltip();
}
}  // namespace UI::TextDetail
