#pragma once
#include <string>
#include <string_view>

#include "gui/widgets/theme.h"
#include "tools/match_sandbox_page_ui.h"
namespace SandboxAnalysisUI
{
inline ImFont* bold()
{
  for (auto* font : ImGui::GetIO().Fonts->Fonts)
    if (std::string_view(font->GetDebugName()).find("Manrope-Bold.ttf") !=
        std::string_view::npos)
      return font;
  return ImGui::GetFont();
}
inline bool button(const char* label, bool selected = false)
{
  std::string visible(label);
  if (const auto at = visible.find("##"); at != std::string::npos)
    visible.resize(at);
  const float width =
      SandboxPageUI::buttonWidth(visible.c_str(), Theme::scale(), bold());
  if (ImGui::GetCursorScreenPos().x + width >
      ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
    ImGui::NewLine();
  ImGui::PushID(label);
  const bool clicked =
      SandboxPageUI::button(visible.c_str(), selected, Theme::scale(), bold());
  ImGui::PopID();
  return clicked;
}
inline void next(float width = 160)
{
  const float scale = Theme::scale();
  if (ImGui::GetItemRectMax().x + 6 * scale + width * scale <=
      ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
    ImGui::SameLine(0, 6 * scale);
}
}  // namespace SandboxAnalysisUI
