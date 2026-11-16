// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <climits>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

/**
 * @brief Reusable, theme-aware widgets for management screens.
 *
 * All widgets draw with the current Theme palette, never allocate per call
 * beyond ImGui's own buffers, and keep ImGui's ID/layout semantics so they can
 * be mixed freely with regular ImGui calls.
 *
 * Quick reference (sizes are unscaled; widgets multiply by Theme::scale()):
 *
 *   Buttons (min height UI::Size::BUTTON = 36 px, COMPACT = 28 px for rows)
 *     if (UI::primaryButton("Submit offer")) ...          // accent, main CTA
 *     if (UI::secondaryButton("Cancel")) ...               // neutral
 *     if (UI::dangerButton("Release")) ...                 // destructive
 *     UI::secondaryButton("Hire", {}, UI::ButtonSize::COMPACT);  // in tables
 *     size.x: 0 = fit label (>= BUTTON_MIN_WIDTH), -FLT_MIN = fill width.
 *
 *   Money and negotiation
 *     int64_t fee = 14'400'000;
 *     const UI::MoneyChip chips[] = {{"Asking", asking}, {"Value", value}};
 *     if (UI::moneyInput("##fee", fee, {.minimum = 0, .chips = chips})) ...
 *       Shows "€14.4M"; accepts "15m", "850k", "1,200,000"; the -/+ buttons
 *       step 5% (Shift 10%, Ctrl 1%).
 *     int upfront = 2;  // index into the labels
 *     const char* parts[] = {"20%", "40%", "60%", "80%", "100%"};
 *     UI::segmented("##upfront", upfront, parts);
 *     UI::summaryRow("Transfer fee", "€14.4M");
 *     UI::summaryRow("Total cost", "€21.0M", nullptr, true);   // emphasis
 *     UI::budgetImpact("Transfer budget", budget, budget - fee, "Over budget");
 *
 *   Filters and tabs
 *     if (UI::toggleButton("Defenders", filter == DEF)) filter = DEF;
 *
 *   Layout (one scroll surface per page: the page scrolls, cards size to
 *   content; never nest scrolling tables inside fixed-height cards)
 *     UI::beginAutoHeightCard("id", "TITLE", width); ... UI::endCard();
 *     A fixed-height card with a ScrollY table is only for layouts that fill
 *     the window exactly (the page itself must not scroll then).
 *
 *   Tables (no horizontal scrolling: low-priority columns hide instead)
 *     static const UI::Column COLUMNS[] = {
 *         {"Name", 0.0f, 0},        // width 0 = stretch; priority 0 = keep
 *         {"Age", 44.0f, 3},        // hides first
 *         {"Wage", 84.0f, 2}};
 *     const UI::ColumnMask mask =
 *         UI::fitColumns(COLUMNS, ImGui::GetContentRegionAvail().x);
 *     if (UI::beginResponsiveTable("t", COLUMNS, mask, flags)) {
 *       ImGui::TableNextRow();
 *       ImGui::TableNextColumn(); ...           // column 0 always visible
 *       if (UI::cell(mask, 1)) ImGui::Text(...);
 *       ImGui::EndTable();
 *     }
 *     Row actions go into a detail strip under the selected row: end the
 *     table, draw the strip, reopen the same id with UI::TableHeader::NONE.
 */
namespace UI
{

/** @brief Result of a modal confirmation dialog. */
enum class DialogResult : uint8_t
{
  NONE,
  CONFIRM,
  CANCEL
};

/** @brief Match outcome for form strips. */
enum class Outcome : uint8_t
{
  WIN,
  DRAW,
  LOSS
};

/** @brief One bar in a horizontal bar chart. */
struct BarDatum
{
  std::string_view label;
  float value = 0.0f;
  ImVec4 color;
  std::string_view valueText;
};

/**
 * @brief Draws text cut to maxWidth with a trailing ellipsis when needed.
 * @return True when the text had to be shortened.
 */
bool drawTextFitted(ImDrawList* drawList, ImVec2 position, ImU32 color,
                    std::string_view text, float maxWidth);

/**
 * @brief Text item limited to maxWidth; shows the full text as a tooltip
 * when it had to be shortened.
 */
void textFitted(std::string_view text, float maxWidth, const ImVec4& color);

/** @brief Large page title with an optional muted subtitle. */
void pageHeader(const char* title, const char* subtitle = nullptr);

/** @brief Small uppercase section label. */
void sectionLabel(const char* text);

/**
 * @brief Starts a bordered card. Always pair with endCard().
 * @param id Stable ImGui id.
 * @param title Optional caption drawn at the top of the card.
 * @param size Card size (0 = fill available on that axis).
 * @param scroll Allow the card body to scroll.
 */
void beginCard(const char* id, const char* title, ImVec2 size,
               bool scroll = false);

/**
 * @brief Starts a card that grows to fit its content (never scrolls).
 * @param width Card width (0 = the full available width).
 */
void beginAutoHeightCard(const char* id, const char* title, float width = 0.0f);

/** @brief Ends a card started with beginCard() or beginAutoHeightCard(). */
void endCard();

/** @brief Figure tile: caption, big value and an optional footnote. */
void statTile(const char* id, const char* caption, const char* value,
              const char* footnote, const ImVec4& valueColor, float width);

/**
 * @brief Lays out a row of equal tiles that wraps onto more lines when the
 * region is too narrow for them all (small windows, large UI scale).
 */
class TileRow
{
 public:
  explicit TileRow(int count, float minimumWidth = 175.0f);
  /** @brief Width for each tile. */
  [[nodiscard]] float width() const { return tile_width; }
  /** @brief Call before each tile; keeps it on the row or wraps. */
  void next();

 private:
  int per_row = 1;
  int index = 0;
  float tile_width = 0.0f;
};

/** @brief Height used by statTile(), for layout calculations. */
float statTileHeight();

/**
 * @brief Labelled horizontal bar coloured with the rating scale.
 * @param width Total row width; 0 uses the available width.
 */
void attributeBar(const char* label, float value, float labelWidth,
                  float maxValue = 100.0f, float width = 0.0f);

/** @brief Labelled bar with an explicit colour and value text (0-1). */
void meter(const char* label, float fraction, float labelWidth,
           const ImVec4& color, const char* valueText);

/** @brief Rounded rating chip, coloured by the rating scale. */
void ratingChip(double rating);

/** @brief Small coloured badge with text. */
void badge(const char* text, const ImVec4& color);

/**
 * @brief Two-colour club shield (kit colours 0xRRGGBB) with the short code
 * drawn inside when the shield is tall enough to read it.
 * @param height Unscaled shield height; the width follows (0.84 ratio).
 */
void clubBadge(const char* code, uint32_t primary, uint32_t secondary,
               float height);

/** @brief clubBadge() drawn at a position (height in pixels), no layout. */
void drawClubBadge(ImDrawList* drawList, ImVec2 min, float height,
                   const char* code, uint32_t primary, uint32_t secondary);

/** @brief Row of W/D/L markers (latest last). */
void formStrip(std::span<const Outcome> outcomes);

/** @brief Muted placeholder shown when a list has nothing to display. */
void emptyState(const char* title, const char* body);

/**
 * @brief Modal confirmation. Open with ImGui::OpenPopup(id) beforehand.
 * @return CONFIRM or CANCEL on the frame a button is pressed, else NONE.
 */
DialogResult confirmDialog(const char* id, const char* title, const char* body,
                           const char* confirmLabel, const char* cancelLabel);

/** @brief Line chart of a series in a fixed box. */
void sparkline(const char* id, std::span<const float> values, ImVec2 size,
               const ImVec4& color);

/** @brief Bars above/below a zero line (positive green, negative red). */
void deltaBars(const char* id, std::span<const float> values, ImVec2 size);

/** @brief Horizontal bars with labels and values, scaled to the maximum. */
void barChart(const char* id, std::span<const BarDatum> bars, float width,
              float labelWidth);

/**
 * @brief Begins a data table that scrolls horizontally only when the region
 * is narrower than minWidth, freezing the header row (and freezeColumns
 * leading columns while scrolling). Pair with ImGui::EndTable() when true.
 */
bool beginDataTable(const char* id, int columns, ImGuiTableFlags flags,
                    float minWidth, ImVec2 size, int freezeColumns = 1);

/**
 * @brief Header row of a non-sortable table: plain labels without the hover
 * highlight that suggests a clickable header.
 */
void staticHeadersRow();

/** @brief Design-system sizes in unscaled pixels. */
namespace Size
{
constexpr float BUTTON = 36.0f;           /**< Regular button height. */
constexpr float BUTTON_COMPACT = 28.0f;   /**< Buttons inside table rows. */
constexpr float BUTTON_PADDING_X = 16.0f; /**< Horizontal label padding. */
constexpr float BUTTON_MIN_WIDTH = 96.0f; /**< Minimum regular width. */
}  // namespace Size

/** @brief Button height class. */
enum class ButtonSize : uint8_t
{
  REGULAR,
  COMPACT
};

/** @brief Accent-filled call-to-action button. */
bool primaryButton(const char* label, ImVec2 size = ImVec2(0, 0),
                   ButtonSize buttonSize = ButtonSize::REGULAR);

/** @brief Neutral button for secondary actions. */
bool secondaryButton(const char* label, ImVec2 size = ImVec2(0, 0),
                     ButtonSize buttonSize = ButtonSize::REGULAR);

/** @brief Red button for destructive actions (release, cancel contract). */
bool dangerButton(const char* label, ImVec2 size = ImVec2(0, 0),
                  ButtonSize buttonSize = ButtonSize::REGULAR);

/** @brief Scaled height of a button class. */
float buttonHeight(ButtonSize buttonSize = ButtonSize::REGULAR);

/** @brief A quick-pick value offered under a money input. */
struct MoneyChip
{
  const char* label;
  int64_t value;
};

/** @brief Options of moneyInput(). */
struct MoneyInputOptions
{
  int64_t minimum = 0;
  int64_t maximum = INT64_MAX;
  std::span<const MoneyChip> chips = {};
  float width = 0.0f; /**< 0 = available width. */
};

/**
 * @brief Currency field showing the full amount; typing accepts plain
 * numbers, either decimal mark and k/M/B (mila/mln/mld) units, with the
 * reading or the reason for refusing it shown under the field. -/+ step 5%
 * (Shift 10%, Ctrl 1%), chips set preset values.
 * @return True on the frame the value changed.
 */
bool moneyInput(const char* id, int64_t& value,
                const MoneyInputOptions& options = {});

/**
 * @brief Next value of a money input's -/+ button: `fraction` up or down,
 * rounded to three significant figures, and always at least 1 away.
 */
int64_t stepMoney(int64_t value, bool up, double fraction);

/**
 * @brief Parses "14.4M", "1,5M", "850k", "€1,200,000", "€ 2,25 mln" (false
 * when invalid or ambiguous; see NumberFormat::parseMoney()).
 */
bool parseMoney(std::string_view text, int64_t& value);

/** @brief Line shown under a money input while typing. */
struct MoneyPreview
{
  std::string text; /**< "Amount: €1,500,000" or why the text is refused. */
  bool error = false;
};

/** @brief What moneyInput() shows for the typed @p text. */
MoneyPreview moneyPreview(std::string_view text,
                          const MoneyInputOptions& options = {});

/**
 * @brief Filter/tab button: neutral when off, selection fill with an accent
 * underline when on (one segment of UI::segmented()).
 */
bool toggleButton(const char* label, bool active, ImVec2 size = ImVec2(0, 0),
                  ButtonSize buttonSize = ButtonSize::REGULAR);

/**
 * @brief Segmented control for small enumerations (joined buttons).
 * @param selected Index of the active segment, updated on click.
 * @return True when the selection changed.
 */
bool segmented(const char* id, int& selected,
               std::span<const char* const> labels, float width = 0.0f);

/**
 * @brief Label on the left, value right-aligned; emphasis draws a rule above
 * and a larger value (totals).
 */
void summaryRow(const char* label, const char* value,
                const ImVec4* valueColor = nullptr, bool emphasis = false);

/**
 * @brief Budget before/after bar: "€20.0M -> €5.6M" (drawn arrow). The after
 * part turns red below zero and the reason is shown underneath.
 */
void budgetImpact(const char* label, int64_t current, int64_t after,
                  const char* reasonWhenNegative = nullptr);

/** @brief One column of a responsive table. */
struct Column
{
  const char* label;
  float width = 0.0f; /**< Unscaled fixed width; 0 = stretch. */
  int priority = 0;   /**< 0 = always shown; higher hides first. */
  ImGuiTableColumnFlags flags = ImGuiTableColumnFlags_None;
  ImGuiID user_id = 0; /**< Sort id (0 = the column index). */
};

/** @brief Visible-column mask produced by fitColumns(). */
using ColumnMask = uint32_t;

/**
 * @brief Chooses the columns that fit the available width, dropping the
 * highest priority numbers first (never horizontal scrolling).
 * @param stretchMinimum Unscaled minimum width left for stretch columns.
 * @param hidden Columns the user hid (hiddenColumns()): never shown, however
 * wide the table. Priority-0 columns ignore it and always stay.
 */
ColumnMask fitColumns(std::span<const Column> columns, float availableWidth,
                      float stretchMinimum = 160.0f, ColumnMask hidden = 0);

/**
 * @brief Columns the user hid in a table, saved in the settings per table.
 * @param table Stable table key.
 * @param columns The table's columns with their untranslated label keys
 * (the keys identify the columns in the settings).
 */
ColumnMask hiddenColumns(std::string_view table,
                         std::span<const Column> columns);

/**
 * @brief "Columns" button opening a checklist of the table's columns; the
 * choice is saved in the settings at once. Priority-0 columns are listed
 * but locked; columns that are on but squeezed out by the window width are
 * marked as such.
 * @param columns Columns with untranslated label keys.
 * @param fitted Visible mask from fitColumns().
 * @return True when the choice changed this frame.
 */
bool columnPicker(std::string_view table, std::span<const Column> columns,
                  ColumnMask fitted);

/** @brief Header row emitted by beginResponsiveTable(). */
enum class TableHeader : uint8_t
{
  STATIC,   /**< Plain labels (non-sortable tables). */
  SORTABLE, /**< ImGui header row with sort arrows. */
  NONE      /**< No header: continuation of a split table. */
};

/**
 * @brief BeginTable + setup of the visible columns and the header row. Pair
 * with ImGui::EndTable() when it returns true. Re-opening the same id with
 * TableHeader::NONE continues a table split around an inline detail strip
 * (column widths stay shared).
 */
bool beginResponsiveTable(const char* id, std::span<const Column> columns,
                          ColumnMask mask, ImGuiTableFlags flags,
                          TableHeader header = TableHeader::STATIC,
                          ImVec2 size = ImVec2(0, 0));

/** @brief Moves to the next cell when column index is visible. */
bool cell(ColumnMask mask, int index);

/** @brief Selectable text that looks like a link; returns true on click. */
bool link(const char* label, const char* id);

/** @brief Muted key on the left, value on the right of the current row. */
void keyValue(const char* key, const char* value, float keyWidth);

/**
 * @brief SameLine() only when an item of the given width still fits.
 * @param spacing Gap before the item (negative = style item spacing).
 * @return true when the item stays on the current line.
 */
bool sameLineIfFits(float nextWidth, float spacing = -1.0f);

/** @brief Width of a button with this label (at least the minimum width). */
float buttonWidth(const char* label,
                  ButtonSize buttonSize = ButtonSize::REGULAR);

/** @brief Text right-aligned in the current table cell or content region. */
void textRight(const char* text);

/** @brief Coloured text right-aligned in the current cell. */
void textRightColored(const ImVec4& color, const char* text);

/** @brief Strictly-weak three-way comparison helper for table sorting. */
template <typename T>
int compare(const T& left, const T& right)
{
  if constexpr (std::is_same_v<T, bool>)
  {
    if (left == right) return 0;
    return left ? 1 : -1;
  }
  else
  {
    if (left < right) return -1;
    if (right < left) return 1;
    return 0;
  }
}

}  // namespace UI
