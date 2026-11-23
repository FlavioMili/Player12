// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <format>
#include <string>
#include <utility>

/**
 * @brief Type-safe stand-ins for ImGui's printf-style text calls.
 *
 * ImGui::Text, TextWrapped, TextDisabled, TextColored and SetTooltip take C
 * varargs, which cppcoreguidelines-pro-type-vararg rejects. These take a
 * std::format string instead, checked at compile time, and draw the result
 * exactly as the ImGui call would:
 *
 *   UI::text("{} players", count);              // ImGui::Text
 *   UI::textWrapped("{}", summary);             // ImGui::TextWrapped
 *   UI::textDisabled("{:.1f} m", metres);       // ImGui::TextDisabled
 *   UI::textColoured(colour, "{}", name);       // ImGui::TextColored
 *   UI::tooltip("{}", help);                    // ImGui::SetTooltip
 *
 * Text with no arguments still goes through the format string, so a literal
 * brace is written "{{" or "}}". Plain strings can use
 * ImGui::TextUnformatted directly.
 */
namespace UI
{
namespace TextDetail
{
void plain(const std::string& text);
void wrapped(const std::string& text);
void disabled(const std::string& text);
void coloured(const ImVec4& colour, const std::string& text);
void tooltip(const std::string& text);
}  // namespace TextDetail

/** ImGui::Text. */
template <typename... Args>
void text(std::format_string<Args...> format, Args&&... args)
{
  TextDetail::plain(std::format(format, std::forward<Args>(args)...));
}

/** ImGui::TextWrapped: wraps at the window edge unless a wrap is pushed. */
template <typename... Args>
void textWrapped(std::format_string<Args...> format, Args&&... args)
{
  TextDetail::wrapped(std::format(format, std::forward<Args>(args)...));
}

/** ImGui::TextDisabled: in the style's disabled text colour. */
template <typename... Args>
void textDisabled(std::format_string<Args...> format, Args&&... args)
{
  TextDetail::disabled(std::format(format, std::forward<Args>(args)...));
}

/** ImGui::TextColored. */
template <typename... Args>
void textColoured(const ImVec4& colour, std::format_string<Args...> format,
                  Args&&... args)
{
  TextDetail::coloured(colour,
                       std::format(format, std::forward<Args>(args)...));
}

/** ImGui::SetTooltip: replaces any tooltip already set this frame. */
template <typename... Args>
void tooltip(std::format_string<Args...> format, Args&&... args)
{
  TextDetail::tooltip(std::format(format, std::forward<Args>(args)...));
}
}  // namespace UI
