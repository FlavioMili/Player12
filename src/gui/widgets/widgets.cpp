// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/widgets/widgets.h"

#include <fmt/printf.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include "global/language_manager.h"
#include "global/number_format.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "model/settings_manager.h"

namespace
{
constexpr float CARD_TITLE_GAP = 6.0f;
constexpr float BAR_HEIGHT = 7.0f;
constexpr float VALUE_COLUMN_WIDTH = 34.0f;
constexpr float FORM_MARKER_SIZE = 18.0f;
constexpr float DIALOG_TEXT_WIDTH = 440.0f;
constexpr float DIALOG_BUTTON_WIDTH = 150.0f;

float scaled(float value) { return value * ImGui::GetStyle().FontScaleDpi; }
}  // namespace

namespace UI
{

bool drawTextFitted(ImDrawList* drawList, ImVec2 position, ImU32 color,
                    std::string_view text, float maxWidth)
{
  const char* begin = text.data();
  if (const char* end = begin + text.size();
      ImGui::CalcTextSize(begin, end).x <= maxWidth)
  {
    drawList->AddText(position, color, begin, end);
    return false;
  }
  constexpr std::string_view ELLIPSIS = "…";
  const float ellipsisWidth =
      ImGui::CalcTextSize(ELLIPSIS.data(), ELLIPSIS.data() + ELLIPSIS.size()).x;
  // Longest prefix (on a UTF-8 boundary) that leaves room for the ellipsis.
  const auto isContinuation = [](char character)
  {
    const auto byte = static_cast<std::byte>(character);
    return (std::to_integer<unsigned char>(byte) & 0xC0U) == 0x80U;
  };
  size_t low = 0;
  size_t high = text.size();
  while (low < high)
  {
    size_t middle = (low + high + 1) / 2;
    while (middle > 0 && middle < text.size() && isContinuation(text[middle]))
      --middle;
    if (middle <= low) break;
    if (ImGui::CalcTextSize(begin, begin + middle).x + ellipsisWidth <=
        maxWidth)
      low = middle;
    else
      high = middle - 1;
  }
  while (low > 0 && low < text.size() && isContinuation(text[low])) --low;
  drawList->AddText(position, color, begin, begin + low);
  const float prefixWidth = ImGui::CalcTextSize(begin, begin + low).x;
  drawList->AddText(ImVec2(position.x + prefixWidth, position.y), color,
                    ELLIPSIS.data(), ELLIPSIS.data() + ELLIPSIS.size());
  return true;
}

void textFitted(std::string_view text, float maxWidth, const ImVec4& color)
{
  // Honour AlignTextToFramePadding() like ImGui::Text does.
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  const ImVec2 start(
      cursor.x,
      cursor.y + ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset);
  const float width = std::max(0.0f, maxWidth);
  const bool cut = drawTextFitted(ImGui::GetWindowDrawList(), start,
                                  Theme::toU32(color), text, width);
  ImGui::Dummy(ImVec2(
      std::min(width,
               ImGui::CalcTextSize(text.data(), text.data() + text.size()).x),
      ImGui::GetTextLineHeight()));
  if (cut && ImGui::IsItemHovered())
    ImGui::SetTooltip("%.*s", static_cast<int>(text.size()), text.data());
}

void pageHeader(const char* title, const char* subtitle)
{
  {
    Theme::ScopedText heading(Theme::Text::HEADING);
    ImGui::TextUnformatted(title);
  }
  if (subtitle != nullptr && *subtitle != '\0')
  {
    Theme::ScopedText small(Theme::Text::SMALL);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::palette().muted);
    ImGui::TextUnformatted(subtitle);
    ImGui::PopStyleColor();
  }
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
}

void sectionLabel(const char* text)
{
  Theme::ScopedText caption(Theme::Text::CAPTION);
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::palette().muted);
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
}

namespace
{
void openCard(const char* id, const char* title, ImVec2 size,
              ImGuiChildFlags extraFlags, ImGuiWindowFlags windowFlags)
{
  ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::palette().surface);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(scaled(Theme::Space::M), scaled(Theme::Space::M)));
  ImGui::BeginChild(id, size,
                    ImGuiChildFlags_Borders |
                        ImGuiChildFlags_AlwaysUseWindowPadding | extraFlags,
                    windowFlags);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  if (title != nullptr && *title != '\0')
  {
    sectionLabel(title);
    ImGui::Dummy(
        ImVec2(0.0f, scaled(CARD_TITLE_GAP) - ImGui::GetStyle().ItemSpacing.y));
  }
}
}  // namespace

void beginCard(const char* id, const char* title, ImVec2 size, bool scroll)
{
  ImGuiWindowFlags flags = ImGuiWindowFlags_None;
  if (!scroll)
    flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  openCard(id, title, size, ImGuiChildFlags_None, flags);
}

void beginAutoHeightCard(const char* id, const char* title, float width)
{
  openCard(id, title, ImVec2(width, 0.0f), ImGuiChildFlags_AutoResizeY,
           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void endCard() { ImGui::EndChild(); }

TileRow::TileRow(int count, float minimumWidth)
{
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const float available = ImGui::GetContentRegionAvail().x;
  const float minimum = scaled(minimumWidth);
  per_row = std::clamp(static_cast<int>((available + gap) / (minimum + gap)), 1,
                       std::max(1, count));
  // Balance wrapped rows (e.g. 3 + 2 instead of 4 + 1).
  const int rows = (count + per_row - 1) / per_row;
  per_row = (count + rows - 1) / rows;
  tile_width = (available - gap * static_cast<float>(per_row - 1)) /
               static_cast<float>(per_row);
}

void TileRow::next()
{
  if (index % per_row != 0) ImGui::SameLine();
  ++index;
}

float statTileHeight()
{
  using enum Theme::Text;
  const ImGuiStyle& style = ImGui::GetStyle();
  const float dpi = style.FontScaleDpi;
  return (Theme::textSize(CAPTION) + Theme::textSize(DISPLAY) +
          Theme::textSize(SMALL)) *
             dpi +
         2.0f * style.ItemSpacing.y * 0.5f + 2.0f * scaled(Theme::Space::M) +
         scaled(Theme::Space::XS);
}

void statTile(const char* id, const char* caption, const char* value,
              const char* footnote, const ImVec4& valueColor, float width)
{
  using enum Theme::Text;
  beginCard(id, nullptr, ImVec2(width, statTileHeight()));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                      ImVec2(ImGui::GetStyle().ItemSpacing.x,
                             ImGui::GetStyle().ItemSpacing.y * 0.5f));
  {
    Theme::ScopedText captionText(CAPTION);
    textFitted(caption, ImGui::GetContentRegionAvail().x,
               Theme::palette().muted);
  }
  {
    // Long values (club names, objectives) step down a size instead of
    // overflowing the tile; the line height stays that of DISPLAY.
    const float available = ImGui::GetContentRegionAvail().x;
    const float lineStart = ImGui::GetCursorPosY();
    Theme::Text level = DISPLAY;
    for (const Theme::Text candidate : {DISPLAY, HEADING, TITLE, BODY})
    {
      level = candidate;
      Theme::ScopedText probe(candidate);
      if (ImGui::CalcTextSize(value).x <= available) break;
    }
    const float displayHeight =
        Theme::textSize(DISPLAY) * ImGui::GetStyle().FontScaleDpi;
    Theme::ScopedText fitted(level);
    ImGui::SetCursorPosY(lineStart +
                         (displayHeight - ImGui::GetTextLineHeight()) * 0.5f);
    textFitted(value, available, valueColor);
    ImGui::SetCursorPosY(lineStart + displayHeight);
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
  }
  if (footnote != nullptr && *footnote != '\0')
  {
    Theme::ScopedText small(SMALL);
    textFitted(footnote, ImGui::GetContentRegionAvail().x,
               Theme::palette().muted);
  }
  ImGui::PopStyleVar();
  endCard();
}

namespace
{
void barRow(const char* label, float fraction, float labelWidth,
            const ImVec4& color, const char* valueText, float width)
{
  const Theme::Palette& palette = Theme::palette();
  const float lineHeight = ImGui::GetTextLineHeight();
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const float available =
      width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
  // Bare bars (no label, no value) use the whole width.
  const bool hasValue = valueText != nullptr && *valueText != '\0';
  const bool hasLabel = label != nullptr && *label != '\0' && labelWidth > 0.0f;
  const float valueWidth = hasValue
                               ? std::max(scaled(VALUE_COLUMN_WIDTH),
                                          ImGui::CalcTextSize(valueText).x) +
                                     scaled(Theme::Space::S)
                               : 0.0f;
  const float barWidth =
      std::max(scaled(24.0f), available - labelWidth - valueWidth);

  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const bool labelCut =
      hasLabel && drawTextFitted(drawList, start, Theme::toU32(palette.muted),
                                 label, labelWidth - scaled(Theme::Space::S));

  const float barHeight = scaled(BAR_HEIGHT);
  const ImVec2 barMin(start.x + labelWidth,
                      start.y + (lineHeight - barHeight) * 0.5f);
  const ImVec2 barMax(barMin.x + barWidth, barMin.y + barHeight);
  const float clamped = std::clamp(fraction, 0.0f, 1.0f);
  drawList->AddRectFilled(barMin, barMax, Theme::toU32(palette.raised),
                          barHeight * 0.5f);
  if (clamped > 0.0f)
    drawList->AddRectFilled(barMin,
                            ImVec2(barMin.x + barWidth * clamped, barMax.y),
                            Theme::toU32(color), barHeight * 0.5f);
  if (hasValue)
  {
    const float textWidth = ImGui::CalcTextSize(valueText).x;
    drawList->AddText(ImVec2(start.x + available - textWidth, start.y),
                      Theme::toU32(color), valueText);
  }
  ImGui::Dummy(ImVec2(available, lineHeight));
  if (labelCut && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", label);
}
}  // namespace

void attributeBar(const char* label, float value, float labelWidth,
                  float maxValue, float width)
{
  std::array<char, 16> text{};
  const auto formatted = std::format_to_n(
      text.data(), static_cast<std::ptrdiff_t>(text.size() - 1), "{:.0f}",
      static_cast<double>(value));
  text[std::min(static_cast<std::size_t>(formatted.size), text.size() - 1)] =
      '\0';
  barRow(label, value / maxValue, labelWidth,
         Theme::ratingColor(value / maxValue * 100.0f), text.data(), width);
}

void meter(const char* label, float fraction, float labelWidth,
           const ImVec4& color, const char* valueText)
{
  barRow(label, fraction, labelWidth, color, valueText, 0.0f);
}

void ratingChip(double rating)
{
  std::array<char, 16> text{};
  const auto formatted = std::format_to_n(
      text.data(), static_cast<std::ptrdiff_t>(text.size() - 1), "{:.0f}",
      rating);
  text[std::min(static_cast<std::size_t>(formatted.size), text.size() - 1)] =
      '\0';
  const ImVec2 textSize = ImGui::CalcTextSize(text.data());
  const float padX = scaled(6.0f);
  const ImVec2 size(std::max(textSize.x + 2.0f * padX, scaled(32.0f)),
                    textSize.y + scaled(2.0f));
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const ImVec4 color = Theme::ratingColor(rating);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(start, ImVec2(start.x + size.x, start.y + size.y),
                          Theme::toU32(color, 0.18f), scaled(4.0f));
  drawList->AddText(ImVec2(start.x + (size.x - textSize.x) * 0.5f,
                           start.y + (size.y - textSize.y) * 0.5f),
                    Theme::toU32(color), text.data());
  ImGui::Dummy(size);
}

void badge(const char* text, const ImVec4& color)
{
  Theme::ScopedText caption(Theme::Text::CAPTION);
  const ImVec2 textSize = ImGui::CalcTextSize(text);
  const ImVec2 padding(scaled(6.0f), scaled(2.0f));
  const ImVec2 size(textSize.x + 2.0f * padding.x,
                    textSize.y + 2.0f * padding.y);
  const ImVec2 start = ImGui::GetCursorScreenPos();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(start, ImVec2(start.x + size.x, start.y + size.y),
                          Theme::toU32(color, 0.20f), scaled(3.0f));
  drawList->AddText(ImVec2(start.x + padding.x, start.y + padding.y),
                    Theme::toU32(color), text);
  ImGui::Dummy(size);
}

void drawClubBadge(ImDrawList* drawList, ImVec2 min, float height,
                   const char* code, uint32_t primary, uint32_t secondary)
{
  const float h = height;
  const float w = std::round(h * 0.84f);
  // Shield outline: flat top, straight sides, curved point at the bottom.
  const auto shield = [&]()
  {
    drawList->PathLineTo(min);
    drawList->PathLineTo(ImVec2(min.x + w, min.y));
    drawList->PathLineTo(ImVec2(min.x + w, min.y + h * 0.52f));
    drawList->PathBezierQuadraticCurveTo(ImVec2(min.x + w, min.y + h * 0.86f),
                                         ImVec2(min.x + w * 0.5f, min.y + h));
    drawList->PathBezierQuadraticCurveTo(ImVec2(min.x, min.y + h * 0.86f),
                                         ImVec2(min.x, min.y + h * 0.52f));
  };
  shield();
  drawList->PathFillConvex(Theme::toU32(Theme::unpackRgb(primary)));
  // Right half in the second colour (halved kit).
  drawList->PushClipRect(ImVec2(min.x + w * 0.5f, min.y),
                         ImVec2(min.x + w, min.y + h), true);
  shield();
  drawList->PathFillConvex(Theme::toU32(Theme::unpackRgb(secondary)));
  drawList->PopClipRect();
  shield();
  drawList->PathStroke(Theme::toU32(Theme::palette().border),
                       ImDrawFlags_Closed, std::max(1.0f, scaled(1.0f)));
  if (code == nullptr || *code == '\0' || h < scaled(32.0f)) return;
  Theme::ScopedText caption(Theme::Text::CAPTION);
  // The code shrinks to fit inside the shield, or is left out when it would
  // become unreadable.
  ImFont* font = ImGui::GetFont();
  float fontSize = ImGui::GetFontSize();
  ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, code);
  if (const float room = w * 0.78f; textSize.x > room)
  {
    fontSize *= room / textSize.x;
    if (fontSize < scaled(8.0f)) return;
    textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, code);
  }
  const ImVec2 at(std::round(min.x + (w - textSize.x) * 0.5f),
                  std::round(min.y + h * 0.42f - textSize.y * 0.5f));
  drawList->AddText(font, fontSize, ImVec2(at.x + 1.0f, at.y + 1.0f),
                    IM_COL32(0, 0, 0, 170), code);
  drawList->AddText(font, fontSize, at, IM_COL32(255, 255, 255, 255), code);
}

void clubBadge(const char* code, uint32_t primary, uint32_t secondary,
               float height)
{
  const float h = scaled(height);
  const ImVec2 min = ImGui::GetCursorScreenPos();
  drawClubBadge(ImGui::GetWindowDrawList(), min, h, code, primary, secondary);
  ImGui::Dummy(ImVec2(std::round(h * 0.84f), h));
}

void formStrip(std::span<const Outcome> outcomes)
{
  const Theme::Palette& palette = Theme::palette();
  Theme::ScopedText caption(Theme::Text::CAPTION);
  const float size = scaled(FORM_MARKER_SIZE);
  const float gap = scaled(3.0f);
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
  const float top = start.y + std::max(0.0f, (lineHeight - size) * 0.5f);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  float x = start.x;
  using enum Outcome;
  for (const Outcome outcome : outcomes)
  {
    const ImVec4* color = &palette.negative;
    const char* letter = LOC("FORM_LOSS_SHORT");
    if (outcome == WIN)
    {
      color = &palette.positive;
      letter = LOC("FORM_WIN_SHORT");
    }
    else if (outcome == DRAW)
    {
      color = &palette.faint;
      letter = LOC("FORM_DRAW_SHORT");
    }
    drawList->AddRectFilled(ImVec2(x, top), ImVec2(x + size, top + size),
                            Theme::toU32(*color), scaled(3.0f));
    const ImVec2 letterSize = ImGui::CalcTextSize(letter);
    drawList->AddText(
        ImVec2(x + (size - letterSize.x) * 0.5f,
               top + (size - letterSize.y) * 0.5f),
        0.2126f * color->x + 0.7152f * color->y + 0.0722f * color->z > 0.42f
            ? IM_COL32(12, 14, 18, 255)
            : IM_COL32(255, 255, 255, 255),
        letter);
    x += size + gap;
  }
  const auto count = static_cast<float>(outcomes.size());
  ImGui::Dummy(ImVec2(count > 0.0f ? count * (size + gap) - gap : 0.0f, size));
}

void emptyState(const char* title, const char* body)
{
  const float width = ImGui::GetContentRegionAvail().x;
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::L)));
  {
    Theme::ScopedText heading(Theme::Text::TITLE);
    const float titleWidth = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         std::max(0.0f, (width - titleWidth) * 0.5f));
    ImGui::TextUnformatted(title);
  }
  if (body != nullptr && *body != '\0')
  {
    Theme::ScopedText small(Theme::Text::SMALL);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::palette().muted);
    const float wrap = std::min(width, scaled(DIALOG_TEXT_WIDTH));
    const float bodyWidth =
        std::min(wrap, ImGui::CalcTextSize(body, nullptr, false, wrap).x);
    const float indent = std::max(0.0f, (width - bodyWidth) * 0.5f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + bodyWidth + 1.0f);
    ImGui::TextUnformatted(body);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
  }
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::L)));
}

DialogResult confirmDialog(const char* id, const char* title, const char* body,
                           const char* confirmLabel, const char* cancelLabel)
{
  DialogResult result = DialogResult::NONE;
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                          ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal(id, nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize |
                                  ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoSavedSettings))
    return result;
  {
    Theme::ScopedText heading(Theme::Text::TITLE);
    ImGui::TextUnformatted(title);
  }
  ImGui::PushTextWrapPos(scaled(DIALOG_TEXT_WIDTH));
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::palette().muted);
  ImGui::TextUnformatted(body);
  ImGui::PopStyleColor();
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::S)));
  const ImVec2 buttonSize(scaled(DIALOG_BUTTON_WIDTH), 0.0f);
  if (ImGui::Button(cancelLabel, buttonSize) ||
      ImGui::IsKeyPressed(ImGuiKey_Escape))
  {
    result = DialogResult::CANCEL;
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (primaryButton(confirmLabel, buttonSize))
  {
    result = DialogResult::CONFIRM;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
  return result;
}

void sparkline(const char* id, std::span<const float> values, ImVec2 size,
               const ImVec4& color)
{
  ImGui::PushID(id);
  const ImVec2 start = ImGui::GetCursorScreenPos();
  ImGui::Dummy(size);
  ImGui::PopID();
  if (values.size() < 2) return;
  const auto [minimum, maximum] = std::ranges::minmax_element(values);
  const float low = *minimum;
  const float range = std::max(*maximum - low, 1e-6f);
  const float step = size.x / static_cast<float>(values.size() - 1);
  const float inset = scaled(2.0f);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  ImVec2 previous;
  for (size_t index = 0; index < values.size(); ++index)
  {
    const ImVec2 point(
        start.x + step * static_cast<float>(index),
        start.y + inset +
            (size.y - 2.0f * inset) * (1.0f - (values[index] - low) / range));
    if (index > 0)
      drawList->AddLine(previous, point, Theme::toU32(color), scaled(1.6f));
    previous = point;
  }
  drawList->AddCircleFilled(previous, scaled(2.5f), Theme::toU32(color));
}

void deltaBars(const char* id, std::span<const float> values, ImVec2 size)
{
  const Theme::Palette& palette = Theme::palette();
  ImGui::PushID(id);
  const ImVec2 start = ImGui::GetCursorScreenPos();
  ImGui::Dummy(size);
  ImGui::PopID();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const float middle = start.y + size.y * 0.5f;
  drawList->AddLine(ImVec2(start.x, middle), ImVec2(start.x + size.x, middle),
                    Theme::toU32(palette.border));
  if (values.empty()) return;
  float largest = 1e-6f;
  for (const float value : values) largest = std::max(largest, std::abs(value));
  const float slot = size.x / static_cast<float>(values.size());
  const float barWidth = std::max(2.0f, slot * 0.6f);
  for (size_t index = 0; index < values.size(); ++index)
  {
    const float height = values[index] / largest * (size.y * 0.5f - 1.0f);
    const float x = start.x + slot * (static_cast<float>(index) + 0.5f);
    drawList->AddRectFilled(
        ImVec2(x - barWidth * 0.5f, std::min(middle, middle - height)),
        ImVec2(x + barWidth * 0.5f, std::max(middle, middle - height)),
        Theme::toU32(values[index] >= 0.0f ? palette.positive
                                           : palette.negative),
        scaled(1.5f));
  }
}

void barChart(const char* id, std::span<const BarDatum> bars, float width,
              float labelWidth)
{
  ImGui::PushID(id);
  const Theme::Palette& palette = Theme::palette();
  float maximum = 0.0f;
  for (const BarDatum& bar : bars) maximum = std::max(maximum, bar.value);
  const float lineHeight = ImGui::GetTextLineHeight();
  const float valueWidth = scaled(70.0f);
  const float trackWidth =
      std::max(scaled(20.0f), width - labelWidth - valueWidth);
  const float barHeight = lineHeight * 0.62f;
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  for (const BarDatum& bar : bars)
  {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const bool labelCut =
        drawTextFitted(drawList, start, Theme::toU32(palette.muted), bar.label,
                       labelWidth - scaled(Theme::Space::S));
    const ImVec2 barMin(start.x + labelWidth,
                        start.y + (lineHeight - barHeight) * 0.5f);
    drawList->AddRectFilled(barMin,
                            ImVec2(barMin.x + trackWidth, barMin.y + barHeight),
                            Theme::toU32(palette.raised), scaled(2.0f));
    if (const float fraction = maximum > 0.0f ? bar.value / maximum : 0.0f;
        fraction > 0.0f)
      drawList->AddRectFilled(
          barMin,
          ImVec2(barMin.x + trackWidth * fraction, barMin.y + barHeight),
          Theme::toU32(bar.color), scaled(2.0f));
    drawList->AddText(
        ImVec2(barMin.x + trackWidth + scaled(Theme::Space::S), start.y),
        Theme::toU32(palette.text), bar.valueText.data(),
        bar.valueText.data() + bar.valueText.size());
    ImGui::Dummy(ImVec2(width, lineHeight));
    if (labelCut && ImGui::IsItemHovered())
      ImGui::SetTooltip("%.*s", static_cast<int>(bar.label.size()),
                        bar.label.data());
  }
  ImGui::PopID();
}

bool beginDataTable(const char* id, int columns, ImGuiTableFlags flags,
                    float minWidth, ImVec2 size, int freezeColumns)
{
  const float scaledMinimum = scaled(minWidth);
  const bool scrollX = ImGui::GetContentRegionAvail().x < scaledMinimum;
  if (scrollX) flags |= ImGuiTableFlags_ScrollX;
  if (!ImGui::BeginTable(id, columns, flags, size,
                         scrollX ? scaledMinimum : 0.0f))
    return false;
  if ((flags & ImGuiTableFlags_ScrollY) != 0)
    ImGui::TableSetupScrollFreeze(scrollX ? freezeColumns : 0, 1);
  return true;
}

void staticHeadersRow()
{
  // A regular row (not ImGuiTableRowFlags_Headers) so the labels count
  // towards auto-fit column widths and never get cut.
  ImGui::TableNextRow();
  ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                         ImGui::GetColorU32(ImGuiCol_TableHeaderBg));
  const int columns = ImGui::TableGetColumnCount();
  for (int column = 0; column < columns; ++column)
  {
    if (!ImGui::TableSetColumnIndex(column)) continue;
    ImGui::TextColored(Theme::palette().muted, "%s",
                       ImGui::TableGetColumnName(column));
  }
}

namespace
{
ImVec2 buttonExtent(const char* label, ImVec2 size, ButtonSize buttonSize)
{
  const float height = buttonHeight(buttonSize);
  if (size.x == 0.0f) size.x = buttonWidth(label, buttonSize);
  if (size.y == 0.0f) size.y = height;
  return size;
}

bool styledButton(const char* label, ImVec2 size, ButtonSize buttonSize,
                  const ImVec4& fill, const ImVec4& text)
{
  const ImVec4 hover(std::min(1.0f, fill.x * 1.12f + 0.03f),
                     std::min(1.0f, fill.y * 1.12f + 0.03f),
                     std::min(1.0f, fill.z * 1.12f + 0.03f), 1.0f);
  const ImVec4 active(fill.x * 0.82f, fill.y * 0.82f, fill.z * 0.82f, 1.0f);
  const ImVec2 extent = buttonExtent(label, size, buttonSize);
  // The label is centred in the button: derive the vertical padding from the
  // button's height, or a short (COMPACT) button using the regular style's
  // larger FramePadding.y leaves the text clipped toward the top.
  const float verticalPadding =
      std::max(2.0f, (extent.y - ImGui::GetTextLineHeight()) * 0.5f);
  ImGui::PushStyleColor(ImGuiCol_Button, fill);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
  ImGui::PushStyleColor(ImGuiCol_Text, text);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                      ImVec2(scaled(Size::BUTTON_PADDING_X), verticalPadding));
  const bool pressed = ImGui::Button(label, extent);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(4);
  return pressed;
}

ImVec4 readableOn(const ImVec4& fill)
{
  const float luminance =
      0.2126f * fill.x + 0.7152f * fill.y + 0.0722f * fill.z;
  return luminance > 0.45f ? ImVec4(0.03f, 0.04f, 0.05f, 1.0f)
                           : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

int64_t roundToSignificant(double value)
{
  if (value <= 0.0) return 0;
  // Three significant figures keep steps readable (e.g. €14.4M, €950K);
  // amounts under 1000 keep every digit.
  const double magnitude = std::pow(10.0, std::floor(std::log10(value)) - 2.0);
  if (magnitude < 1.0) return static_cast<int64_t>(std::llround(value));
  return static_cast<int64_t>(
      static_cast<double>(std::llround(value / magnitude)) * magnitude);
}
}  // namespace

int64_t stepMoney(int64_t value, bool up, double fraction)
{
  const auto current = static_cast<double>(value);
  // Every press moves by at least 1, so small amounts never get stuck.
  if (up)
    return std::max<int64_t>(value + 1,
                             roundToSignificant(current * (1.0 + fraction)));
  return std::min<int64_t>(value - 1,
                           roundToSignificant(current * (1.0 - fraction)));
}

float buttonHeight(ButtonSize buttonSize)
{
  return scaled(buttonSize == ButtonSize::COMPACT ? Size::BUTTON_COMPACT
                                                  : Size::BUTTON);
}

bool primaryButton(const char* label, ImVec2 size, ButtonSize buttonSize)
{
  const ImVec4& accent = Theme::palette().accent;
  return styledButton(label, size, buttonSize, accent,
                      Theme::palette().on_accent);
}

bool secondaryButton(const char* label, ImVec2 size, ButtonSize buttonSize)
{
  const Theme::Palette& palette = Theme::palette();
  return styledButton(label, size, buttonSize, palette.raised, palette.text);
}

bool dangerButton(const char* label, ImVec2 size, ButtonSize buttonSize)
{
  const ImVec4& negative = Theme::palette().negative;
  return styledButton(label, size, buttonSize, negative, readableOn(negative));
}

bool parseMoney(std::string_view text, int64_t& value)
{
  const NumberFormat::MoneyParse parsed = NumberFormat::parseMoney(text);
  if (!parsed.value.has_value()) return false;
  value = *parsed.value;
  return true;
}

MoneyPreview moneyPreview(std::string_view text,
                          const MoneyInputOptions& options)
{
  const NumberFormat::MoneyParse parsed = NumberFormat::parseMoney(text);
  if (!parsed.value.has_value())
    return {LOC(NumberFormat::errorKey(parsed.error)), true};
  const int64_t clamped =
      std::clamp(*parsed.value, options.minimum, options.maximum);
  if (clamped != *parsed.value)
    return {fmt::sprintf(LOC("WIDGET_MONEY_PREVIEW_LIMITED"),
                         Format::moneyFull(clamped)),
            false};
  return {fmt::sprintf(LOC("WIDGET_MONEY_PREVIEW"), Format::moneyFull(clamped)),
          false};
}

bool moneyInput(const char* id, int64_t& value,
                const MoneyInputOptions& options)
{
  // Only the field being edited needs a text buffer of its own.
  static ImGuiID editing = 0;
  static int64_t pending = 0;
  static bool pendingValid = false;
  static bool edited = false;
  static MoneyPreview preview;
  // A refused entry keeps its message under the field until it is edited
  // again, so the player sees why the amount did not change.
  static ImGuiID rejected = 0;
  static std::string rejectedText;
  static int rejectedFrame = 0;

  ImGui::PushID(id);
  bool changed = false;
  // The field and its step buttons share the regular button height.
  const ImVec2 padding = ImGui::GetStyle().FramePadding;
  ImGui::PushStyleVar(
      ImGuiStyleVar_FramePadding,
      ImVec2(
          padding.x,
          std::max(padding.y, (buttonHeight() - ImGui::GetFontSize()) * 0.5f)));
  const float stepWidth = ImGui::GetFrameHeight();
  const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
  const float width =
      (options.width > 0.0f ? options.width : ImGui::GetContentRegionAvail().x);
  const auto clampValue = [&options](int64_t candidate)
  { return std::clamp(candidate, options.minimum, options.maximum); };
  const ImGuiIO& io = ImGui::GetIO();
  double stepFraction = 0.05;
  if (io.KeyCtrl) stepFraction = 0.01;
  if (io.KeyShift) stepFraction = 0.10;
  const char* stepHelp = LOC("WIDGET_MONEY_STEP_HELP");
  const float rowStartX = ImGui::GetCursorPosX();

  if (ImGui::Button("-", ImVec2(stepWidth, 0.0f)))
  {
    value = clampValue(stepMoney(value, false, stepFraction));
    changed = true;
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    ImGui::SetTooltip("%s", stepHelp);
  ImGui::SameLine(0.0f, gap);

  std::array<char, 48> buffer{};
  const std::string shown = Format::moneyFull(value);
  const auto formatted = std::format_to_n(
      buffer.data(), static_cast<std::ptrdiff_t>(buffer.size() - 1), "{}",
      shown);
  buffer[std::min(static_cast<std::size_t>(formatted.size),
                  buffer.size() - 1)] = '\0';
  ImGui::SetNextItemWidth(
      std::max(scaled(80.0f), width - 2.0f * (stepWidth + gap)));
  ImGui::InputText("##money", buffer.data(), buffer.size(),
                   ImGuiInputTextFlags_AutoSelectAll);
  const ImGuiID fieldId = ImGui::GetItemID();
  const int frame = ImGui::GetFrameCount();
  // A message left by a dialog that has since closed is dropped.
  if (rejected == fieldId && rejectedFrame < frame - 1) rejected = 0;
  if (ImGui::IsItemActivated())
  {
    editing = fieldId;
    pendingValid = false;
    edited = false;
    if (rejected == fieldId) rejected = 0;
  }
  if (ImGui::IsItemEdited() && editing == fieldId)
  {
    pendingValid = parseMoney(buffer.data(), pending);
    preview = moneyPreview(buffer.data(), options);
    edited = true;
  }
  if (ImGui::IsItemDeactivated() && editing == fieldId)
  {
    if (pendingValid && clampValue(pending) != value)
    {
      value = clampValue(pending);
      changed = true;
    }
    else if (!pendingValid && edited)
    {
      rejected = fieldId;
      rejectedText = preview.text;
    }
    editing = 0;
    pendingValid = false;
    edited = false;
  }
  if (rejected == fieldId) rejectedFrame = frame;
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) &&
      !ImGui::IsItemActive())
    ImGui::SetTooltip("%s", LOC("WIDGET_MONEY_INPUT_HELP"));
  ImGui::SameLine(0.0f, gap);
  if (ImGui::Button("+", ImVec2(stepWidth, 0.0f)))
  {
    value = clampValue(stepMoney(value, true, stepFraction));
    changed = true;
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    ImGui::SetTooltip("%s", stepHelp);
  ImGui::PopStyleVar();
  if (changed && rejected == fieldId) rejected = 0;

  // Live reading of the typed text (or why it was refused) under the field.
  if (const bool typing = editing == fieldId && edited;
      typing || rejected == fieldId)
  {
    const Theme::Palette& palette = Theme::palette();
    const bool error = !typing || preview.error;
    Theme::ScopedText caption(Theme::Text::SMALL);
    ImGui::PushTextWrapPos(rowStartX + width);
    ImGui::TextColored(error ? palette.negative : palette.muted, "%s",
                       typing ? preview.text.c_str() : rejectedText.c_str());
    ImGui::PopTextWrapPos();
  }

  for (size_t index = 0; index < options.chips.size(); ++index)
  {
    const MoneyChip& chip = options.chips[index];
    if (index > 0) sameLineIfFits(buttonWidth(chip.label, ButtonSize::COMPACT));
    ImGui::PushID(static_cast<int>(index));
    // A chip already matching the value has nothing to do: shown disabled.
    const bool active = clampValue(chip.value) == value;
    ImGui::BeginDisabled(active);
    if (secondaryButton(chip.label, ImVec2(0.0f, 0.0f), ButtonSize::COMPACT))
    {
      value = clampValue(chip.value);
      changed = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort |
                             ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("%s", Format::moneyFull(chip.value).c_str());
    ImGui::PopID();
  }
  ImGui::PopID();
  return changed;
}

bool toggleButton(const char* label, bool active, ImVec2 size,
                  ButtonSize buttonSize)
{
  const Theme::Palette& palette = Theme::palette();
  const ImVec4 fill =
      active ? ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive) : palette.raised;
  const bool pressed = styledButton(label, size, buttonSize, fill,
                                    active ? palette.text : palette.muted);
  if (active)
  {
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(min.x + scaled(6.0f), max.y - scaled(2.5f)),
        ImVec2(max.x - scaled(6.0f), max.y), Theme::toU32(palette.accent),
        scaled(1.5f));
  }
  return pressed;
}

bool segmented(const char* id, int& selected,
               std::span<const char* const> labels, float width)
{
  if (labels.empty()) return false;
  ImGui::PushID(id);
  const float gap = scaled(2.0f);
  float natural = 0.0f;
  for (const char* label : labels)
    natural = std::max(natural, ImGui::CalcTextSize(label, nullptr, true).x +
                                    2.0f * scaled(Theme::Space::M));
  const auto count = static_cast<float>(labels.size());
  const float segmentWidth =
      width > 0.0f ? (width - gap * (count - 1.0f)) / count : natural;
  bool changed = false;
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
  for (size_t index = 0; index < labels.size(); ++index)
  {
    if (index > 0) ImGui::SameLine();
    const bool active = static_cast<int>(index) == selected;
    ImGui::PushID(static_cast<int>(index));
    if (toggleButton(labels[index], active, ImVec2(segmentWidth, 0.0f)) &&
        !active)
    {
      selected = static_cast<int>(index);
      changed = true;
    }
    ImGui::PopID();
  }
  ImGui::PopStyleVar();
  ImGui::PopID();
  return changed;
}

void summaryRow(const char* label, const char* value, const ImVec4* valueColor,
                bool emphasis)
{
  const Theme::Palette& palette = Theme::palette();
  if (emphasis)
  {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(
        start, ImVec2(start.x + ImGui::GetContentRegionAvail().x, start.y),
        Theme::toU32(palette.border), scaled(1.0f));
    ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
  }
  const Theme::Text level = emphasis ? Theme::Text::TITLE : Theme::Text::BODY;
  Theme::ScopedText text(level);
  const float available = ImGui::GetContentRegionAvail().x;
  const float valueWidth = ImGui::CalcTextSize(value).x;
  textFitted(label, available - valueWidth - scaled(Theme::Space::M),
             emphasis ? palette.text : palette.muted);
  ImGui::SameLine();
  textRightColored(valueColor != nullptr ? *valueColor : palette.text, value);
}

void budgetImpact(const char* label, int64_t current, int64_t after,
                  const char* reasonWhenNegative)
{
  const Theme::Palette& palette = Theme::palette();
  const bool negative = after < 0;
  // The font has no arrow glyph: the gap between the amounts gets a drawn
  // arrow instead.
  constexpr const char* ARROW_GAP = "      ";
  const std::string afterText = Format::money(after);
  const std::string change = Format::money(current) + ARROW_GAP + afterText;
  const ImVec4& tone = negative ? palette.negative : palette.text;
  summaryRow(label, change.c_str(), &tone);
  {
    Theme::ScopedText text(Theme::Text::BODY);
    const ImVec2 valueMax = ImGui::GetItemRectMax();
    const float afterWidth = ImGui::CalcTextSize(afterText.c_str()).x;
    const float gapWidth = ImGui::CalcTextSize(ARROW_GAP).x;
    const float inset = gapWidth * 0.25f;
    const float right = valueMax.x - afterWidth - inset;
    const float left = valueMax.x - afterWidth - gapWidth + inset;
    const float y =
        ImGui::GetItemRectMin().y + ImGui::GetTextLineHeight() * 0.5f;
    const float head = scaled(3.5f);
    const ImU32 color = Theme::toU32(palette.muted);
    ImDrawList* arrowList = ImGui::GetWindowDrawList();
    arrowList->AddLine(ImVec2(left, y), ImVec2(right, y), color, scaled(1.5f));
    arrowList->AddTriangleFilled(ImVec2(right + scaled(1.0f), y),
                                 ImVec2(right - head, y - head),
                                 ImVec2(right - head, y + head), color);
  }

  // Track: the full bar is the current amount, the kept part is filled.
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = scaled(6.0f);
  const ImVec2 start = ImGui::GetCursorScreenPos();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(start, ImVec2(start.x + width, start.y + height),
                          Theme::toU32(palette.raised), height * 0.5f);
  const auto base = static_cast<double>(std::max<int64_t>(current, 1));
  const float kept =
      std::clamp(static_cast<float>(
                     static_cast<double>(std::max<int64_t>(after, 0)) / base),
                 0.0f, 1.0f);
  if (kept > 0.0f)
    drawList->AddRectFilled(start,
                            ImVec2(start.x + width * kept, start.y + height),
                            Theme::toU32(palette.positive), height * 0.5f);
  if (after < current)
    drawList->AddRectFilled(
        ImVec2(start.x + width * kept, start.y),
        ImVec2(start.x + width, start.y + height),
        Theme::toU32(negative ? palette.negative : palette.warning, 0.55f),
        height * 0.5f);
  ImGui::Dummy(ImVec2(width, height));
  if (negative && reasonWhenNegative != nullptr && *reasonWhenNegative != '\0')
  {
    Theme::ScopedText small(Theme::Text::SMALL);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.negative, "%s", reasonWhenNegative);
    ImGui::PopTextWrapPos();
  }
}

ColumnMask fitColumns(std::span<const Column> columns, float availableWidth,
                      float stretchMinimum, ColumnMask hidden)
{
  // User-hidden columns stay out at any width; must-stay columns ignore it.
  ColumnMask mask = 0;
  for (size_t index = 0; index < columns.size(); ++index)
    if (columns[index].priority == 0 ||
        (hidden & (ColumnMask{1} << index)) == 0)
      mask |= ColumnMask{1} << index;
  const float cellPadding = 2.0f * ImGui::GetStyle().CellPadding.x;
  const auto required = [&](ColumnMask candidate)
  {
    float total = 0.0f;
    bool stretch = false;
    for (size_t index = 0; index < columns.size(); ++index)
    {
      if ((candidate & (ColumnMask{1} << index)) == 0) continue;
      if (columns[index].width > 0.0f)
        total += scaled(columns[index].width) + cellPadding;
      else
        stretch = true;
    }
    return total + (stretch ? scaled(stretchMinimum) + cellPadding : 0.0f);
  };
  // Drop the highest priority number first (last column first on ties).
  while (required(mask) > availableWidth)
  {
    int victim = -1;
    for (size_t index = 0; index < columns.size(); ++index)
    {
      if ((mask & (ColumnMask{1} << index)) == 0 ||
          columns[index].priority == 0)
        continue;
      if (victim < 0 || columns[index].priority >=
                            columns[static_cast<size_t>(victim)].priority)
        victim = static_cast<int>(index);
    }
    if (victim < 0) break;
    mask &= ~(ColumnMask{1} << static_cast<unsigned>(victim));
  }
  return mask;
}

ColumnMask hiddenColumns(std::string_view table,
                         std::span<const Column> columns)
{
  const auto& views = SettingsManager::instance()->get().hidden_columns;
  const auto view = views.find(table);
  if (view == views.end()) return 0;
  ColumnMask mask = 0;
  for (size_t index = 0; index < columns.size(); ++index)
    if (columns[index].priority != 0 &&
        std::ranges::find(view->second, columns[index].label) !=
            view->second.end())
      mask |= ColumnMask{1} << index;
  return mask;
}

bool columnPicker(std::string_view table, std::span<const Column> columns,
                  ColumnMask fitted)
{
  const Theme::Palette& palette = Theme::palette();
  ImGui::PushID(table.data(), table.data() + table.size());
  if (secondaryButton(LOC("TABLE_COLUMNS"), ImVec2(0.0f, 0.0f),
                      ButtonSize::COMPACT))
    ImGui::OpenPopup("##columns");
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    ImGui::SetTooltip("%s", LOC("TABLE_COLUMNS_HINT"));
  bool changed = false;
  if (ImGui::BeginPopup("##columns"))
  {
    ColumnMask hidden = hiddenColumns(table, columns);
    ImGui::TextColored(palette.muted, "%s", LOC("TABLE_COLUMNS_TITLE"));
    for (size_t index = 0; index < columns.size(); ++index)
    {
      const Column& column = columns[index];
      const ColumnMask bit = ColumnMask{1} << index;
      const bool locked = column.priority == 0;
      bool shown = locked || (hidden & bit) == 0;
      ImGui::PushID(static_cast<int>(index));
      ImGui::BeginDisabled(locked);
      if (ImGui::Checkbox(LOC(column.label), &shown))
      {
        hidden = shown ? hidden & ~bit : hidden | bit;
        changed = true;
      }
      ImGui::EndDisabled();
      if (locked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled |
                                         ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", LOC("TABLE_COLUMN_LOCKED"));
      if (!locked && shown && (fitted & bit) == 0)
      {
        ImGui::SameLine();
        ImGui::TextColored(palette.faint, "%s", LOC("TABLE_COLUMN_NO_ROOM"));
      }
      ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(hidden == 0);
    if (secondaryButton(LOC("TABLE_COLUMNS_RESET"), ImVec2(0.0f, 0.0f),
                        ButtonSize::COMPACT))
    {
      hidden = 0;
      changed = true;
    }
    ImGui::EndDisabled();
    if (changed)
    {
      SettingsManager* settings = SettingsManager::instance();
      auto& views = settings->get().hidden_columns;
      std::vector<std::string> keys;
      for (size_t index = 0; index < columns.size(); ++index)
        if ((hidden & (ColumnMask{1} << index)) != 0)
          keys.emplace_back(columns[index].label);
      if (keys.empty())
      {
        if (const auto view = views.find(table); view != views.end())
          views.erase(view);
      }
      else
      {
        views.insert_or_assign(std::string(table), std::move(keys));
      }
      settings->save();
    }
    ImGui::EndPopup();
  }
  ImGui::PopID();
  return changed;
}

bool beginResponsiveTable(const char* id, std::span<const Column> columns,
                          ColumnMask mask, ImGuiTableFlags flags,
                          TableHeader header, ImVec2 size)
{
  const int visible = std::popcount(mask);
  if (visible == 0) return false;
  // One table state per column set: ImGui keeps per-column state (order,
  // widths, sort) that must not leak between different visible sets.
  std::array<char, 96> name{};
  const auto formatted = std::format_to_n(
      name.data(), static_cast<std::ptrdiff_t>(name.size() - 1),
      "{}##cols{:08x}", id, mask);
  name[std::min(static_cast<std::size_t>(formatted.size), name.size() - 1)] =
      '\0';
  if (!ImGui::BeginTable(name.data(), visible, flags, size)) return false;
  for (size_t index = 0; index < columns.size(); ++index)
  {
    if ((mask & (ColumnMask{1} << index)) == 0) continue;
    const Column& column = columns[index];
    ImGuiTableColumnFlags columnFlags = column.flags;
    columnFlags |= column.width > 0.0f ? ImGuiTableColumnFlags_WidthFixed
                                       : ImGuiTableColumnFlags_WidthStretch;
    ImGui::TableSetupColumn(
        column.label, columnFlags,
        column.width > 0.0f ? scaled(column.width) : 0.0f,
        column.user_id != 0 ? column.user_id : static_cast<ImGuiID>(index));
  }
  if ((flags & ImGuiTableFlags_ScrollY) != 0)
    ImGui::TableSetupScrollFreeze(0, 1);
  if (header == TableHeader::STATIC)
    staticHeadersRow();
  else if (header == TableHeader::SORTABLE)
    ImGui::TableHeadersRow();
  return true;
}

bool cell(ColumnMask mask, int index)
{
  if ((mask & (ColumnMask{1} << static_cast<unsigned>(index))) == 0)
    return false;
  ImGui::TableNextColumn();
  return true;
}

bool link(const char* label, const char* id)
{
  ImGui::PushID(id);
  const ImVec2 size = ImGui::CalcTextSize(label, nullptr, true);
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton("##link", size);
  const bool hovered = ImGui::IsItemHovered();
  ImGui::PopID();
  const ImVec4 color =
      ImGui::GetStyleColorVec4(hovered ? ImGuiCol_Text : ImGuiCol_TextLink);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const char* hidden = std::strstr(label, "##");
  drawList->AddText(start, Theme::toU32(color), label, hidden);
  if (hovered)
  {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    drawList->AddLine(ImVec2(start.x, start.y + size.y - 1.0f),
                      ImVec2(start.x + size.x, start.y + size.y - 1.0f),
                      Theme::toU32(color), 1.0f);
  }
  return clicked;
}

void keyValue(const char* key, const char* value, float keyWidth)
{
  const float startX = ImGui::GetCursorPosX();
  const float available = ImGui::GetContentRegionAvail().x;
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float keyTextWidth = ImGui::CalcTextSize(key).x;
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::palette().muted);
  ImGui::TextUnformatted(key);
  ImGui::PopStyleColor();
  // Long (translated) labels push the value right; in narrow cards the value
  // moves below the label, and it always wraps instead of being clipped.
  const float valueX = std::max(keyWidth, keyTextWidth + 2.0f * spacing);
  if (const float valueWidth = ImGui::CalcTextSize(value).x;
      valueX + std::min(valueWidth, available * 0.35f) <= available)
  {
    ImGui::SameLine(startX + valueX);
  }
  else
  {
    ImGui::SetCursorPosX(startX + spacing * 2.0f);
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(value);
  ImGui::PopTextWrapPos();
}

bool sameLineIfFits(float nextWidth, float spacing)
{
  const float lineEnd =
      ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
  const float gap = spacing < 0.0f ? ImGui::GetStyle().ItemSpacing.x : spacing;
  if (ImGui::GetItemRectMax().x + gap + nextWidth > lineEnd) return false;
  ImGui::SameLine(0.0f, gap);
  return true;
}

float buttonWidth(const char* label, ButtonSize buttonSize)
{
  const float natural = ImGui::CalcTextSize(label, nullptr, true).x +
                        2.0f * scaled(Size::BUTTON_PADDING_X);
  return buttonSize == ButtonSize::COMPACT
             ? natural
             : std::max(natural, scaled(Size::BUTTON_MIN_WIDTH));
}

void textRight(const char* text)
{
  const float width = ImGui::CalcTextSize(text).x;
  if (const float available = ImGui::GetContentRegionAvail().x;
      available > width)
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - width);
  ImGui::TextUnformatted(text);
}

void textRightColored(const ImVec4& color, const char* text)
{
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  textRight(text);
  ImGui::PopStyleColor();
}

}  // namespace UI
