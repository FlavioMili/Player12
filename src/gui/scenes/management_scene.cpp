// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/management_scene.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <utility>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/global.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/input_actions.h"
#include "gui/scenes/about_scene.h"
#include "gui/scenes/awards_scene.h"
#include "gui/scenes/calendar_scene.h"
#include "gui/scenes/callup_scene.h"
#include "gui/scenes/club_scene.h"
#include "gui/scenes/data_hub_scene.h"
#include "gui/scenes/delegation_scene.h"
#include "gui/scenes/fixtures_scene.h"
#include "gui/scenes/help_scene.h"
#include "gui/scenes/inbox_scene.h"
#include "gui/scenes/international_scene.h"
#include "gui/scenes/lineup_scene.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/main_menu_scene.h"
#include "gui/scenes/manager_scene.h"
#include "gui/scenes/match_report_scene.h"
#include "gui/scenes/medical_scene.h"
#include "gui/scenes/news_scene.h"
#include "gui/scenes/onboarding_overlay.h"
#include "gui/scenes/opposition_scene.h"
#include "gui/scenes/player_compare_scene.h"
#include "gui/scenes/player_profile_scene.h"
#include "gui/scenes/preseason_scene.h"
#include "gui/scenes/records_scene.h"
#include "gui/scenes/reserves_scene.h"
#include "gui/scenes/roster_scene.h"
#include "gui/scenes/scouting_scene.h"
#include "gui/scenes/settings_scene.h"
#include "gui/scenes/squad_planner_scene.h"
#include "gui/scenes/staff_scene.h"
#include "gui/scenes/standings_scene.h"
#include "gui/scenes/strategy_scene.h"
#include "gui/scenes/timeline_scene.h"
#include "gui/scenes/training_scene.h"
#include "gui/scenes/transfer_market_scene.h"
#include "gui/scenes/youth_scene.h"
#include "gui/view_models/player_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/icons.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/competition.h"
#include "model/inbox.h"
#include "model/role_utils.h"

namespace
{
constexpr float SIDEBAR_WIDTH = 228.0f;
constexpr float SIDEBAR_COLLAPSED_WIDTH = 60.0f;
constexpr float SIDEBAR_COLLAPSE_BELOW = 1150.0f;
constexpr float ICON_SIZE = 16.0f;
constexpr float TOP_BAR_HEIGHT = 60.0f;
constexpr float NAV_ITEM_HEIGHT = 40.0f;
/** Items shrink down to this height before the navigation has to scroll. */
constexpr float NAV_ITEM_MIN_HEIGHT = 22.0f;
constexpr float HUB_TAB_HEIGHT = 36.0f;
/** Height of a hub's screen entry relative to a hub entry. */
constexpr float SUB_ITEM_RATIO = 0.82f;
/** Seconds a collapsed sidebar's flyout stays open after the mouse left. */
constexpr float FLYOUT_GRACE_SECONDS = 0.3f;
constexpr const char* FLYOUT_WINDOW_ID = "##nav_flyout";
constexpr float CLUB_BADGE_SIZE = 38.0f;
constexpr float CONTINUE_MIN_WIDTH = 200.0f;
constexpr float PALETTE_WIDTH = 620.0f;
constexpr float PALETTE_TOP_OFFSET = 70.0f;
constexpr size_t PALETTE_MAX_RESULTS = 14;
constexpr size_t PALETTE_MAX_ACTIONS = 4;
constexpr float TOAST_SECONDS = 3.5f;
constexpr float SAVE_POLL_SECONDS = 1.0f;
constexpr const char* PALETTE_POPUP_ID = "##command_palette";
constexpr const char* MAIN_MENU_CONFIRM_ID = "##confirm_main_menu";
/** Radius of the Back / Forward bubble shown while swiping. */
constexpr float SWIPE_BUBBLE_RADIUS = 22.0f;
/** Gap between the window edge and the bubble once the swipe is complete. */
constexpr float SWIPE_BUBBLE_MARGIN = 14.0f;
/**
 * Sign turning a wheel sample into finger travel for the swipe. SDL reports
 * scrolling to the right as positive x; with natural scrolling the value is
 * inverted and flagged SDL_MOUSEWHEEL_FLIPPED, which is undone first. Fingers
 * moving right then read as positive, which is Back (the previous page slides
 * in from the left, as in a web browser).
 */
constexpr float SWIPE_BACK_SIGN = 1.0f;

/** One screen of the shell (a tab of its hub). */
struct NavScreen
{
  NavSection section;
  const char* label_key;
};

constexpr size_t MAX_HUB_SECTIONS = 8;

/**
 * A sidebar destination: a hub of related screens shown as tabs. Its
 * shortcut and the sidebar open the first screen the manager can use.
 */
struct NavHub
{
  const char* label_key;
  std::string_view action; /*!< Input action that opens the hub. */
  UI::Icon icon;
  std::array<NavSection, MAX_HUB_SECTIONS> sections;
};

// Every screen, in palette order.
constexpr std::array<NavScreen, 30> ALL_NAV = {{
    {NavSection::HOME, "NAV_HOME"},
    {NavSection::INBOX, "NAV_INBOX"},
    {NavSection::NEWS, "NAV_NEWS"},
    {NavSection::SQUAD, "NAV_SQUAD"},
    {NavSection::LINEUP, "NAV_LINEUP"},
    {NavSection::TACTICS, "NAV_TACTICS"},
    {NavSection::SQUAD_PLANNER, "NAV_SQUAD_PLANNER"},
    {NavSection::MEDICAL, "NAV_MEDICAL"},
    {NavSection::COMPARE, "NAV_COMPARE"},
    {NavSection::RESERVES, "NAV_RESERVES"},
    {NavSection::TRAINING, "NAV_TRAINING"},
    {NavSection::PLANNING, "NAV_PLANNING"},
    {NavSection::FIXTURES, "NAV_FIXTURES"},
    {NavSection::STANDINGS, "NAV_STANDINGS"},
    {NavSection::CALENDAR, "NAV_CALENDAR"},
    {NavSection::OPPOSITION, "NAV_OPPOSITION"},
    {NavSection::INTERNATIONAL, "NAV_INTERNATIONAL"},
    {NavSection::CALL_UPS, "NAV_CALL_UPS"},
    {NavSection::DATA_HUB, "NAV_DATA_HUB"},
    {NavSection::TRANSFERS, "NAV_TRANSFERS"},
    {NavSection::SCOUTING, "NAV_SCOUTING"},
    {NavSection::YOUTH, "NAV_YOUTH"},
    {NavSection::CLUB, "NAV_CLUB"},
    {NavSection::FINANCES, "NAV_FINANCES"},
    {NavSection::STAFF, "NAV_STAFF"},
    {NavSection::DELEGATION, "NAV_DELEGATION"},
    {NavSection::MANAGER, "NAV_MANAGER"},
    {NavSection::TIMELINE, "NAV_TIMELINE"},
    {NavSection::AWARDS, "NAV_AWARDS"},
    {NavSection::RECORDS, "NAV_RECORDS"},
}};

constexpr NavSection END = NavSection::NONE;
// clang-format off
constexpr std::array<NavHub, 7> NAV_HUBS = {{
    {"NAV_HOME", Input::Ids::NAV_HOME, UI::Icon::HOME,
     {NavSection::HOME, END, END, END, END, END, END, END}},
    {"NAV_INBOX", Input::Ids::NAV_INBOX, UI::Icon::INBOX,
     {NavSection::INBOX, NavSection::NEWS, END, END, END, END, END, END}},
    {"NAV_SQUAD", Input::Ids::NAV_SQUAD, UI::Icon::SQUAD,
     {NavSection::SQUAD, NavSection::LINEUP, NavSection::TACTICS,
      NavSection::SQUAD_PLANNER, NavSection::MEDICAL, NavSection::COMPARE,
      NavSection::RESERVES, END}},
    {"NAV_TRAINING", Input::Ids::NAV_TRAINING, UI::Icon::TACTICS,
     {NavSection::TRAINING, NavSection::PLANNING, END, END, END, END, END,
      END}},
    {"NAV_HUB_MATCHES", Input::Ids::NAV_MATCHES, UI::Icon::FIXTURES,
     {NavSection::FIXTURES, NavSection::STANDINGS, NavSection::CALENDAR,
      NavSection::OPPOSITION, NavSection::INTERNATIONAL, NavSection::CALL_UPS,
      NavSection::DATA_HUB, END}},
    {"NAV_HUB_RECRUITMENT", Input::Ids::NAV_RECRUITMENT, UI::Icon::TRANSFERS,
     {NavSection::TRANSFERS, NavSection::SCOUTING, NavSection::YOUTH, END,
      END, END, END, END}},
    {"NAV_CLUB", Input::Ids::NAV_CLUB, UI::Icon::CLUB,
     {NavSection::CLUB, NavSection::FINANCES, NavSection::STAFF,
      NavSection::DELEGATION, NavSection::MANAGER, NavSection::TIMELINE,
      NavSection::AWARDS, NavSection::RECORDS}},
}};
// clang-format on

constexpr bool everyScreenHasOneHub()
{
  for (const NavScreen& entry : ALL_NAV)
  {
    int hubs = 0;
    for (const NavHub& hub : NAV_HUBS)
      for (const NavSection section : hub.sections)
        hubs += section == entry.section ? 1 : 0;
    if (hubs != 1) return false;
  }
  return true;
}
static_assert(everyScreenHasOneHub(),
              "every screen belongs to exactly one sidebar hub");

/**
 * Arrow keys walk the current hub's screens while the sidebar has the
 * keyboard: set by a sidebar click or a hub's F-key, cleared by a click
 * anywhere else. Kept across screens (each screen is a new scene).
 */
bool sidebarHasKeyboard = false;

/**
 * The manager coaches a national team: the call-up screen is open (with or
 * without a club). Refreshed by every frame of the shell.
 */
bool nationalCoach = false;

/** Out of work only the manager's own screens and the world stay open. */
bool sectionOpen(NavSection section, bool unemployed)
{
  if (section == NavSection::CALL_UPS) return nationalCoach;
  if (!unemployed) return section != NavSection::NONE;
  return section == NavSection::HOME || section == NavSection::INBOX ||
         section == NavSection::STANDINGS || section == NavSection::MANAGER ||
         section == NavSection::INTERNATIONAL ||
         section == NavSection::AWARDS || section == NavSection::RECORDS ||
         section == NavSection::NEWS || section == NavSection::TIMELINE;
}

const char* labelKeyOf(NavSection section)
{
  const auto found = std::ranges::find(ALL_NAV, section, &NavScreen::section);
  return found != ALL_NAV.end() ? found->label_key : "NAV_HOME";
}

const NavHub* hubOf(NavSection section)
{
  if (section == NavSection::NONE) return nullptr;
  const auto found = std::ranges::find_if(
      NAV_HUBS,
      [section](const NavHub& hub)
      {
        return std::ranges::find(hub.sections, section) != hub.sections.end();
      });
  return found != NAV_HUBS.end() ? &*found : nullptr;
}

/**
 * Label of an action's key ("F3"), cached until a binding changes so the
 * sidebar does not format strings every frame.
 */
const char* shortcutLabel(std::string_view actionId)
{
  struct Cached
  {
    std::string_view id;
    std::string label;
  };
  static std::vector<Cached> cache;
  static std::uint32_t revision = 0;
  const Input::ActionRegistry& registry = Input::registry();
  if (revision != registry.revision())
  {
    cache.clear();
    revision = registry.revision();
  }
  const auto found = std::ranges::find(cache, actionId, &Cached::id);
  if (found != cache.end()) return found->label.c_str();
  const auto action = registry.find(actionId);
  cache.push_back(
      {actionId, action ? registry.label(*action) : std::string("-")});
  return cache.back().label.c_str();
}

/** First screen of a hub the manager can use (NONE: hub hidden). */
NavSection firstOpenSection(const NavHub& hub, bool unemployed)
{
  for (const NavSection section : hub.sections)
    if (sectionOpen(section, unemployed)) return section;
  return NavSection::NONE;
}

/** Screens of a hub the manager can use, in sidebar order. */
size_t openSections(const NavHub& hub, bool unemployed,
                    std::array<NavSection, MAX_HUB_SECTIONS>& out)
{
  size_t count = 0;
  for (const NavSection section : hub.sections)
    if (sectionOpen(section, unemployed)) out[count++] = section;
  return count;
}

/** Unread count shown next to a screen (inbox, new scout reports). */
size_t sectionBadge(const GameController& controller, NavSection section)
{
  if (section == NavSection::INBOX) return controller.getUnreadInboxCount();
  if (section == NavSection::SCOUTING)
    return controller.getUnreadScoutReportCount();
  return 0U;
}

/** Accent pill with a count; returns its width. */
float drawCountPill(ImDrawList* drawList, ImVec2 min, float height,
                    size_t count)
{
  const Theme::Palette& palette = Theme::palette();
  ImGui::PushFont(nullptr, Theme::textSize(Theme::Text::CAPTION));
  const std::string text = count > 99 ? "99+" : std::to_string(count);
  const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
  const float width =
      std::max(textSize.x + 8.0f * Theme::scale(), 18.0f * Theme::scale());
  const float pillHeight = std::min(height, textSize.y + 2.0f * Theme::scale());
  const ImVec2 pillMin(min.x, min.y + (height - pillHeight) * 0.5f);
  drawList->AddRectFilled(pillMin,
                          ImVec2(pillMin.x + width, pillMin.y + pillHeight),
                          Theme::toU32(palette.accent), pillHeight * 0.5f);
  drawList->AddText(ImVec2(pillMin.x + (width - textSize.x) * 0.5f,
                           pillMin.y + (pillHeight - textSize.y) * 0.5f),
                    Theme::toU32(palette.on_accent), text.c_str());
  ImGui::PopFont();
  return width;
}

/** Width of the count pill drawn by drawCountPill(). */
float countPillWidth(size_t count)
{
  ImGui::PushFont(nullptr, Theme::textSize(Theme::Text::CAPTION));
  const std::string text = count > 99 ? "99+" : std::to_string(count);
  const float width =
      std::max(ImGui::CalcTextSize(text.c_str()).x + 8.0f * Theme::scale(),
               18.0f * Theme::scale());
  ImGui::PopFont();
  return width;
}

/**
 * Tabs of the current screen's hub above the page, shown only while the
 * sidebar is collapsed to icons (the full sidebar lists the hub's screens
 * itself). Hubs with one usable screen draw nothing. The current tab is not
 * a button: it has nothing to do. Tabs share the width when their labels do
 * not fit, with ellipsis.
 */
void renderHubTabs(GUIView* view, NavSection current)
{
  const NavHub* hub = hubOf(current);
  if (hub == nullptr) return;
  const GameController& controller = view->getController();
  const bool unemployed = controller.isUnemployed();
  std::array<NavSection, MAX_HUB_SECTIONS> shown{};
  const size_t count = openSections(*hub, unemployed, shown);
  if (count < 2) return;

  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const float height = HUB_TAB_HEIGHT * scale;
  const float padding = Theme::Space::M * scale;
  const float gap = Theme::Space::XS * scale;
  const float available = ImGui::GetContentRegionAvail().x;
  std::array<const char*, MAX_HUB_SECTIONS> labels{};
  std::array<size_t, MAX_HUB_SECTIONS> badges{};
  std::array<float, MAX_HUB_SECTIONS> widths{};
  float total = gap * static_cast<float>(count - 1);
  for (size_t index = 0; index < count; ++index)
  {
    labels[index] = LOC(labelKeyOf(shown[index]));
    badges[index] = sectionBadge(controller, shown[index]);
    widths[index] =
        ImGui::CalcTextSize(labels[index]).x + 2.0f * padding +
        (badges[index] > 0 ? countPillWidth(badges[index]) + gap : 0.0f);
    total += widths[index];
  }
  const float shared = (available - gap * static_cast<float>(count - 1)) /
                       static_cast<float>(count);

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddLine(ImVec2(origin.x, origin.y + height - 1.0f),
                    ImVec2(origin.x + available, origin.y + height - 1.0f),
                    Theme::toU32(palette.border), 1.0f);
  float x = origin.x;
  ImGui::PushID("##hub_tabs");
  for (size_t index = 0; index < count; ++index)
  {
    const NavSection section = shown[index];
    const float width = total > available ? shared : widths[index];
    const ImVec2 min(x, origin.y);
    const ImVec2 max(x + width, origin.y + height);
    ImGui::SetCursorScreenPos(min);
    const bool selected = section == current;
    bool hovered = false;
    if (selected)
    {
      ImGui::Dummy(ImVec2(width, height));
    }
    else
    {
      ImGui::PushID(static_cast<int>(section));
      if (ImGui::InvisibleButton("##tab", ImVec2(width, height)))
        Navigation::open(view, section);
      hovered = ImGui::IsItemHovered();
      ImGui::PopID();
    }
    if (hovered)
      drawList->AddRectFilled(min, ImVec2(max.x, max.y - 1.0f),
                              Theme::toU32(palette.raised), 4.0f * scale,
                              ImDrawFlags_RoundCornersTop);
    if (selected)
      drawList->AddRectFilled(
          ImVec2(min.x + 2.0f * scale, max.y - 3.0f * scale),
          ImVec2(max.x - 2.0f * scale, max.y), Theme::toU32(palette.accent),
          1.5f * scale);
    const char* label = labels[index];
    const float pill =
        badges[index] > 0 ? countPillWidth(badges[index]) + gap : 0.0f;
    const float textRoom = std::max(0.0f, width - 2.0f * padding - pill);
    const ImVec2 textPos(min.x + padding,
                         min.y + (height - ImGui::GetTextLineHeight()) * 0.5f);
    const bool cut = UI::drawTextFitted(
        drawList, textPos,
        Theme::toU32(selected || hovered ? palette.text : palette.muted), label,
        textRoom);
    if (badges[index] > 0)
      drawCountPill(
          drawList,
          ImVec2(textPos.x + std::min(ImGui::CalcTextSize(label).x, textRoom) +
                     gap,
                 min.y),
          height, badges[index]);
    if (cut && (hovered || (selected && ImGui::IsItemHovered())))
      ImGui::SetTooltip("%s", label);
    x += width + gap;
  }
  ImGui::PopID();
  ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height));
  ImGui::Dummy(ImVec2(available, Theme::Space::S * scale));
}

MainGameScene* careerHub(GUIView* view)
{
  return dynamic_cast<MainGameScene*>(view->getBaseScene());
}

/**
 * Sidebar entry. @p parent marks the hub whose screens are listed below it
 * (drawn in full ink, the selection fill goes to the current screen).
 * @p tooltip false: a collapsed entry shows a flyout instead of a tooltip.
 */
bool navItem(const char* label, const char* shortcut, bool selected,
             UI::Icon icon, bool collapsed, float height, size_t badge = 0,
             bool parent = false, bool tooltip = true)
{
  const Theme::Palette& palette = Theme::palette();
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const ImVec2 size(ImGui::GetContentRegionAvail().x, height);
  ImGui::PushID(label);
  const bool pressed = ImGui::InvisibleButton("##nav", size);
  ImGui::PopID();
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const ImVec2 end(start.x + size.x, start.y + size.y);
  if (selected)
  {
    drawList->AddRectFilled(start, end, Theme::toU32(palette.accent, 0.16f),
                            4.0f * Theme::scale());
    drawList->AddRectFilled(
        start, ImVec2(start.x + 3.0f * Theme::scale(), end.y),
        Theme::toU32(palette.accent), 2.0f * Theme::scale());
  }
  else if (hovered)
  {
    drawList->AddRectFilled(start, end, Theme::toU32(palette.raised),
                            4.0f * Theme::scale());
  }
  const ImU32 color = Theme::toU32(
      selected || parent || hovered ? palette.text : palette.muted);
  const float iconSize = ICON_SIZE * Theme::scale();
  const float iconX =
      collapsed ? start.x + size.x * 0.5f
                : start.x + Theme::Space::M * Theme::scale() + iconSize * 0.5f;
  UI::drawIcon(drawList, icon, ImVec2(iconX, start.y + size.y * 0.5f), iconSize,
               selected || parent ? Theme::toU32(palette.accent) : color);
  if (badge > 0)
  {
    // Unread count pill (top-right of the icon when collapsed).
    if (collapsed)
      drawCountPill(drawList,
                    ImVec2(iconX + 2.0f * Theme::scale(),
                           start.y + 3.0f * Theme::scale()),
                    Theme::textSize(Theme::Text::CAPTION) *
                            ImGui::GetStyle().FontScaleDpi +
                        2.0f * Theme::scale(),
                    badge);
    else
      drawCountPill(drawList,
                    ImVec2(end.x - countPillWidth(badge) -
                               Theme::Space::S * Theme::scale(),
                           start.y),
                    size.y, badge);
    shortcut = nullptr;
  }
  if (collapsed)
  {
    if (hovered && tooltip)
      ImGui::SetTooltip("%s%s%s", label, shortcut ? "   " : "",
                        shortcut ? shortcut : "");
    return pressed;
  }
  const float textY = start.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f;
  const float textX =
      iconX + iconSize * 0.5f + Theme::Space::S * Theme::scale();
  float labelRight = end.x - Theme::Space::S * Theme::scale();
  if (shortcut != nullptr)
  {
    ImGui::PushFont(nullptr, Theme::textSize(Theme::Text::CAPTION));
    const ImVec2 hintSize = ImGui::CalcTextSize(shortcut);
    labelRight -= hintSize.x + Theme::Space::S * Theme::scale();
    drawList->AddText(
        ImVec2(end.x - hintSize.x - Theme::Space::S * Theme::scale(),
               start.y + (size.y - hintSize.y) * 0.5f),
        Theme::toU32(palette.faint), shortcut);
    ImGui::PopFont();
  }
  if (UI::drawTextFitted(drawList, ImVec2(textX, textY), color, label,
                         labelRight - textX) &&
      hovered)
    ImGui::SetTooltip("%s", label);
  return pressed;
}

/**
 * One screen of the expanded hub, indented under the hub's label with a
 * guide line; the current screen gets the selection fill.
 */
bool subNavItem(const char* label, bool selected, float height, size_t badge)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const ImVec2 size(ImGui::GetContentRegionAvail().x, height);
  ImGui::PushID(label);
  const bool pressed = ImGui::InvisibleButton("##sub", size);
  ImGui::PopID();
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const float iconSize = ICON_SIZE * scale;
  const float guideX = start.x + Theme::Space::M * scale + iconSize * 0.5f;
  const float textX = guideX + iconSize * 0.5f + Theme::Space::S * scale;
  const ImVec2 end(start.x + size.x, start.y + size.y);
  const float spacing = ImGui::GetStyle().ItemSpacing.y;
  drawList->AddLine(ImVec2(guideX, start.y - spacing), ImVec2(guideX, end.y),
                    Theme::toU32(palette.border), 1.0f * scale);
  if (selected)
  {
    drawList->AddRectFilled(ImVec2(textX - Theme::Space::S * scale, start.y),
                            end, Theme::toU32(palette.accent, 0.16f),
                            4.0f * scale);
    drawList->AddRectFilled(ImVec2(guideX - 1.5f * scale, start.y),
                            ImVec2(guideX + 1.5f * scale, end.y),
                            Theme::toU32(palette.accent), 1.5f * scale);
  }
  else if (hovered)
  {
    drawList->AddRectFilled(ImVec2(textX - Theme::Space::S * scale, start.y),
                            end, Theme::toU32(palette.raised), 4.0f * scale);
  }
  float labelRight = end.x - Theme::Space::S * scale;
  if (badge > 0)
  {
    const float pill = countPillWidth(badge);
    drawCountPill(drawList, ImVec2(labelRight - pill, start.y), size.y, badge);
    labelRight -= pill + Theme::Space::XS * scale;
  }
  const ImU32 color =
      Theme::toU32(selected || hovered ? palette.text : palette.muted);
  if (UI::drawTextFitted(
          drawList,
          ImVec2(textX, start.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f),
          color, label, labelRight - textX) &&
      hovered)
    ImGui::SetTooltip("%s", label);
  return pressed;
}

/** Icon-only sidebar action (label and shortcut in the tooltip). */
bool footerIcon(const char* label, const char* shortcut, UI::Icon icon,
                float width, float height)
{
  const Theme::Palette& palette = Theme::palette();
  const ImVec2 start = ImGui::GetCursorScreenPos();
  ImGui::PushID(label);
  const bool pressed =
      ImGui::InvisibleButton("##footer", ImVec2(width, height));
  ImGui::PopID();
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  if (hovered)
    drawList->AddRectFilled(start, ImVec2(start.x + width, start.y + height),
                            Theme::toU32(palette.raised),
                            4.0f * Theme::scale());
  const float iconSize = std::min(18.0f * Theme::scale(), height * 0.7f);
  UI::drawIcon(drawList, icon,
               ImVec2(start.x + width * 0.5f, start.y + height * 0.5f),
               iconSize, Theme::toU32(hovered ? palette.text : palette.muted));
  if (hovered)
  {
    if (shortcut != nullptr)
      ImGui::SetTooltip("%s  (%s)", label, shortcut);
    else
      ImGui::SetTooltip("%s", label);
  }
  return pressed;
}

std::string clubInitials(const std::string& name)
{
  std::string initials;
  bool atWordStart = true;
  for (const char character : name)
  {
    if (character == ' ')
    {
      atWordStart = true;
      continue;
    }
    if (atWordStart && initials.size() < 2) initials.push_back(character);
    atWordStart = false;
  }
  return initials.empty() ? std::string("?") : initials;
}

int paletteHistoryCallback(ImGuiInputTextCallbackData* data)
{
  auto* selection = static_cast<int*>(data->UserData);
  if (data->EventKey == ImGuiKey_UpArrow)
    --*selection;
  else if (data->EventKey == ImGuiKey_DownArrow)
    ++*selection;
  return 0;
}
}  // namespace

namespace Navigation
{

void open(GUIView* view, NavSection section)
{
  switch (section)
  {
    case NavSection::HOME:
    case NavSection::FINANCES:
      if (MainGameScene* hub = careerHub(view))
        hub->showPage(section == NavSection::HOME
                          ? MainGameScene::Page::OVERVIEW
                          : MainGameScene::Page::FINANCES);
      view->navigateTo(nullptr);
      return;
    case NavSection::INBOX:
      view->navigateTo(std::make_unique<InboxScene>(view));
      return;
    case NavSection::CLUB:
      view->navigateTo(std::make_unique<ClubScene>(view));
      return;
    case NavSection::SQUAD:
      view->navigateTo(std::make_unique<RosterScene>(view));
      return;
    case NavSection::LINEUP:
      view->navigateTo(std::make_unique<LineupScene>(view));
      return;
    case NavSection::TACTICS:
      view->navigateTo(std::make_unique<StrategyScene>(view));
      return;
    case NavSection::FIXTURES:
      view->navigateTo(std::make_unique<FixturesScene>(view));
      return;
    case NavSection::STANDINGS:
      view->navigateTo(std::make_unique<StandingsScene>(view));
      return;
    case NavSection::TRANSFERS:
      view->navigateTo(std::make_unique<TransferMarketScene>(view));
      return;
    case NavSection::SCOUTING:
      view->navigateTo(std::make_unique<ScoutingScene>(view));
      return;
    case NavSection::TRAINING:
      view->navigateTo(std::make_unique<TrainingScene>(view));
      return;
    case NavSection::STAFF:
      view->navigateTo(std::make_unique<StaffScene>(view));
      return;
    case NavSection::YOUTH:
      view->navigateTo(std::make_unique<YouthScene>(view));
      return;
    case NavSection::MANAGER:
      view->navigateTo(std::make_unique<ManagerScene>(view));
      return;
    case NavSection::MEDICAL:
      view->navigateTo(std::make_unique<MedicalScene>(view));
      return;
    case NavSection::CALENDAR:
      view->navigateTo(std::make_unique<CalendarScene>(view));
      return;
    case NavSection::SQUAD_PLANNER:
      view->navigateTo(std::make_unique<SquadPlannerScene>(view));
      return;
    case NavSection::COMPARE:
      view->navigateTo(std::make_unique<PlayerCompareScene>(view, PlayerID{}));
      return;
    case NavSection::DELEGATION:
      view->navigateTo(std::make_unique<DelegationScene>(view));
      return;
    case NavSection::DATA_HUB:
      view->navigateTo(std::make_unique<DataHubScene>(view));
      return;
    case NavSection::OPPOSITION:
      view->navigateTo(std::make_unique<OppositionScene>(view));
      return;
    case NavSection::INTERNATIONAL:
      view->navigateTo(std::make_unique<InternationalScene>(view));
      return;
    case NavSection::AWARDS:
      view->navigateTo(std::make_unique<AwardsScene>(view));
      return;
    case NavSection::RECORDS:
      view->navigateTo(std::make_unique<RecordsScene>(view));
      return;
    case NavSection::PLANNING:
      view->navigateTo(std::make_unique<PreseasonScene>(view));
      return;
    case NavSection::RESERVES:
      view->navigateTo(std::make_unique<ReservesScene>(view));
      return;
    case NavSection::CALL_UPS:
      view->navigateTo(std::make_unique<CallUpScene>(view));
      return;
    case NavSection::NEWS:
      view->navigateTo(std::make_unique<NewsScene>(view));
      return;
    case NavSection::TIMELINE:
      view->navigateTo(std::make_unique<TimelineScene>(view));
      return;
    case NavSection::NONE:
      return;
  }
}

void openPlayer(GUIView* view, PlayerID playerId)
{
  view->overlayScene(std::make_unique<PlayerProfileScene>(view, playerId));
}

void openMatchReport(GUIView* view, GameDateValue date, TeamID homeId,
                     TeamID awayId)
{
  view->overlayScene(
      std::make_unique<MatchReportScene>(view, date, homeId, awayId));
}

void openClub(GUIView* view, TeamID teamId)
{
  // The managed club's list is the Squad screen (one history entry for it).
  if (const auto managed = view->getController().getManagedTeam();
      managed && managed->get().getId() == teamId)
  {
    open(view, NavSection::SQUAD);
    return;
  }
  view->navigateTo(std::make_unique<RosterScene>(view, teamId));
}

void openCompare(GUIView* view, PlayerID first, PlayerID second)
{
  view->overlayScene(std::make_unique<PlayerCompareScene>(view, first, second));
}

bool canOpen(const GUIView* view, const NavEntry& entry)
{
  const GameController& controller = view->getController();
  const bool namesPlayers = entry.kind == NavEntry::Kind::PLAYER ||
                            entry.kind == NavEntry::Kind::COMPARE;
  const std::shared_ptr<GameData> data =
      namesPlayers ? controller.getGameData() : nullptr;
  const GameData* players = data.get();
  const auto playerKnown = [players](PlayerID id)
  { return id == 0 || (players != nullptr && players->getPlayer(id)); };
  const bool unemployed = controller.isUnemployed();
  switch (entry.kind)
  {
    case NavEntry::Kind::SECTION:
      return sectionOpen(entry.section, unemployed);
    case NavEntry::Kind::PLAYER:
      return entry.player != 0 && playerKnown(entry.player);
    case NavEntry::Kind::CLUB:
      return controller.getTeamById(entry.team).has_value();
    case NavEntry::Kind::MATCH_REPORT:
      return controller.getTeamById(entry.team).has_value() &&
             controller.getTeamById(entry.away_team).has_value();
    case NavEntry::Kind::COMPARE:
      return sectionOpen(NavSection::COMPARE, unemployed) &&
             playerKnown(entry.player) && playerKnown(entry.second_player);
  }
  return false;
}

namespace
{
/** History entries the manager can still open. */
auto opensIn(GUIView* view)
{
  return [view](const NavEntry& entry) { return canOpen(view, entry); };
}

/**
 * A career screen is shown (not a match, the club choice or a menu) and no
 * Continue is under way: a screen opened while the hub is closing the ones
 * above it would hold the simulation back.
 */
bool onCareerScreen(GUIView* view)
{
  if (const MainGameScene* hub = careerHub(view);
      hub != nullptr && hub->isContinuing())
    return false;
  const GUIScene* top = view->getTopScene();
  return top != nullptr && top->historyEntry().has_value();
}

/**
 * Shows a history entry. Going back to the screen right beneath the top one
 * just closes the top one, so the screen beneath keeps its state (filters,
 * selection, scroll). Going forward re-opens a detail screen above the
 * current one, where it was opened the first time; otherwise the entry
 * replaces what is shown.
 */
void showEntry(GUIView* view, const NavEntry& entry, bool forward)
{
  if (!forward)
    if (const GUIScene* below = view->getSceneBelowTop();
        below != nullptr && below->historyEntry() == entry)
    {
      view->popScene();
      view->markHistoryStep();
      return;
    }
  const bool above = forward && entry.isDetail();
  const auto place = [view, above](std::unique_ptr<GUIScene> scene)
  {
    if (above)
      view->overlayScene(std::move(scene));
    else
      view->navigateTo(std::move(scene));
  };
  switch (entry.kind)
  {
    case NavEntry::Kind::SECTION:
      open(view, entry.section);
      break;
    case NavEntry::Kind::CLUB:
      openClub(view, entry.team);
      break;
    case NavEntry::Kind::PLAYER:
      place(std::make_unique<PlayerProfileScene>(view, entry.player));
      break;
    case NavEntry::Kind::MATCH_REPORT:
      place(std::make_unique<MatchReportScene>(view, entry.date, entry.team,
                                               entry.away_team));
      break;
    case NavEntry::Kind::COMPARE:
      place(std::make_unique<PlayerCompareScene>(view, entry.player,
                                                 entry.second_player));
      break;
  }
  view->markHistoryStep();
}
}  // namespace

void back(GUIView* view)
{
  if (!onCareerScreen(view)) return;
  const auto valid = opensIn(view);
  if (const std::optional<NavEntry> target = view->navHistory().back(valid))
  {
    showEntry(view, *target, false);
    return;
  }
  // Nothing earlier to go to: close the top screen, as a step back onto the
  // screen beneath so Forward can bring the closed one back.
  if (view->getOverlayDepth() == 0) return;
  const GUIScene* below = view->getSceneBelowTop();
  const std::optional<NavEntry> revealed =
      below != nullptr ? below->historyEntry() : std::nullopt;
  view->popScene();
  if (revealed)
  {
    view->navHistory().stepBackTo(*revealed);
    view->markHistoryStep();
  }
}

void forward(GUIView* view)
{
  if (!onCareerScreen(view)) return;
  const auto valid = opensIn(view);
  if (const std::optional<NavEntry> target = view->navHistory().forward(valid))
    showEntry(view, *target, true);
}

void close(GUIView* view)
{
  if (!onCareerScreen(view)) return;
  const NavEntry home = NavEntry::ofSection(NavSection::HOME);
  const bool atBase = view->getOverlayDepth() == 0;
  // Beneath a hub page (Finances) is Home; Home itself has nothing beneath.
  if (atBase && view->getTopScene()->historyEntry() == home) return;
  const GUIScene* below = view->getSceneBelowTop();
  const std::optional<NavEntry> beneath =
      atBase ? std::optional<NavEntry>(home)
             : (below != nullptr ? below->historyEntry() : std::nullopt);
  const auto valid = opensIn(view);
  if (const std::optional<NavEntry> previous =
          view->navHistory().peekBack(valid);
      previous && beneath == previous)
  {
    back(view);
    return;
  }
  if (atBase)
    open(view, NavSection::HOME);
  else
    view->popScene();
}

bool canGoBack(GUIView* view)
{
  if (!onCareerScreen(view)) return false;
  const auto valid = opensIn(view);
  return view->getOverlayDepth() > 0 ||
         view->navHistory().peekBack(valid).has_value();
}

bool canGoForward(GUIView* view)
{
  if (!onCareerScreen(view)) return false;
  const auto valid = opensIn(view);
  return view->navHistory().peekForward(valid).has_value();
}

}  // namespace Navigation

ManagementScene::ManagementScene(GUIView* guiViewPtr) : GUIScene(guiViewPtr) {}

void ManagementScene::onEnter()
{
  GuidanceUI::noteVisit(guiView->getController(), navSection());
  refresh();
}

void ManagementScene::onResume() { refresh(); }

std::optional<NavEntry> ManagementScene::historyEntry() const
{
  const NavSection section = navSection();
  if (section == NavSection::NONE) return std::nullopt;
  return NavEntry::ofSection(section);
}

namespace
{
/**
 * The sideways swipe is left to the page while it cannot mean Back or
 * Forward: text is being edited, a widget is held, a dialog or menu is open,
 * or the mouse is over something that scrolls sideways itself. Reads the
 * previous frame's state (events arrive between frames).
 */
bool swipeBelongsToPage()
{
  const ImGuiContext* context = ImGui::GetCurrentContext();
  if (context == nullptr) return true;
  if (context->IO.WantTextInput || context->ActiveId != 0 ||
      context->OpenPopupStack.Size > 0)
    return true;
  // Only surfaces meant to scroll sideways count, not a clipped wide item.
  for (const ImGuiWindow* window = context->HoveredWindow; window != nullptr;
       window = window->ParentWindow)
    if (window->ScrollbarX ||
        (window->Flags & ImGuiWindowFlags_HorizontalScrollbar) != 0)
      return true;
  return false;
}
}  // namespace

void ManagementScene::handleEvent(const SDL_Event& event)
{
  // Nothing to step to while Continue is under way (see Navigation::back).
  if (const MainGameScene* hub = careerHub(guiView);
      hub != nullptr && hub->isContinuing())
    return;
  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
  {
    if (event.button.button == SDL_BUTTON_X1)
      pending_history_step = SwipeGesture::Step::BACK;
    else if (event.button.button == SDL_BUTTON_X2)
      pending_history_step = SwipeGesture::Step::FORWARD;
    return;
  }
  if (event.type != SDL_EVENT_MOUSE_WHEEL) return;
  SwipeGesture& swipe = guiView->swipeGesture();
  const Uint64 timestamp =
      event.wheel.timestamp != 0 ? event.wheel.timestamp : SDL_GetTicksNS();
  if (swipeBelongsToPage())
  {
    swipe.suppress(timestamp);
    return;
  }
  const float unflip =
      event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
  const SwipeGesture::Step step =
      swipe.feed(event.wheel.x * unflip * SWIPE_BACK_SIGN,
                 event.wheel.y * unflip, timestamp);
  if (step == SwipeGesture::Step::NONE) return;
  // With nowhere to go the bubble just disappears instead of completing.
  const bool possible = step == SwipeGesture::Step::BACK
                            ? Navigation::canGoBack(guiView)
                            : Navigation::canGoForward(guiView);
  if (possible)
    pending_history_step = step;
  else
    swipe.suppress(timestamp);
}

void ManagementScene::showToast(std::string message, bool isError)
{
  toast_message = std::move(message);
  toast_is_error = isError;
  toast_seconds = TOAST_SECONDS;
}

void ManagementScene::syncClubAccent()
{
  const auto managed = guiView->getController().getManagedTeam();
  Theme::setClub(managed ? std::optional<TeamID>(managed->get().getId())
                         : std::nullopt);
}

void ManagementScene::render()
{
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  MainGameScene* hub = careerHub(guiView);
  const bool advancing = hub != nullptr && hub->isAdvancing();
  if (!advancing) nationalCoach = guiView->getController().hasNationalJob();
  // While advancing, the frozen frame drawn by GUIView shows through.
  ImGui::Begin("##management_shell", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoScrollWithMouse |
                   (advancing && guiView->getBackdrop() != nullptr
                        ? ImGuiWindowFlags_NoBackground
                        : ImGuiWindowFlags_None));
  ImGui::PopStyleVar(3);

  if (advancing)
  {
    pending_history_step = SwipeGesture::Step::NONE;
    hub->renderContinueOverlay();
    ImGui::End();
    return;
  }

  // Game state is only read once no simulation runs in the background.
  if (hub != nullptr) hub->refreshIfStale();
  syncClubAccent();
  toast_seconds = std::max(0.0f, toast_seconds - ImGui::GetIO().DeltaTime);
  save_poll_seconds -= ImGui::GetIO().DeltaTime;
  if (save_poll_seconds <= 0.0f)
  {
    save_poll_seconds = SAVE_POLL_SECONDS;
    pollSaveStatus();
  }
  handleShortcuts();

  const bool collapsed = ImGui::GetMainViewport()->WorkSize.x <
                         SIDEBAR_COLLAPSE_BELOW * Theme::scale();
  renderSidebar(collapsed);
  ImGui::SameLine(0.0f, 0.0f);
  ImGui::BeginGroup();
  renderTopBar(TOP_BAR_HEIGHT * Theme::scale());
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(Theme::Space::XL * Theme::scale(),
                             Theme::Space::L * Theme::scale()));
  ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f),
                    ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleVar();
  // Detail screens (profile, match report) keep their Back button only: no
  // hub tabs and no tip of the section they were opened from.
  const bool detailScreen =
      getID() == SceneID::PLAYER_PROFILE || getID() == SceneID::MATCH_REPORT;
  if (!detailScreen && collapsed) renderHubTabs(guiView, navSection());
  GuidanceUI::renderReclaimNotice(guiView);
  if (!detailScreen) GuidanceUI::renderScreenTip(navSection());
  renderContent();
  ImGui::EndChild();
  ImGui::EndGroup();

  renderSwipeIndicator();
  renderPalette();
  renderMainMenuConfirm();
  if (const auto plan = holiday_dialog.render(guiView->getController()))
    if (hub != nullptr) hub->requestHoliday(*plan);
  if (hub != nullptr)
    if (const float fade = hub->continueFadeOut(); fade > 0.0f)
    {
      ImGui::GetForegroundDrawList()->AddRectFilled(
          viewport->WorkPos,
          ImVec2(viewport->WorkPos.x + viewport->WorkSize.x,
                 viewport->WorkPos.y + viewport->WorkSize.y),
          IM_COL32(0, 0, 0, static_cast<int>(120.0f * fade)));
    }
  ImGui::End();
}

void ManagementScene::renderSidebar(bool collapsed)
{
  const float width =
      (collapsed ? SIDEBAR_COLLAPSED_WIDTH : SIDEBAR_WIDTH) * Theme::scale();
  const Theme::Palette& palette = Theme::palette();
  GameController& controller = guiView->getController();
  ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.sidebar);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(Theme::Space::S * Theme::scale(),
                             Theme::Space::L * Theme::scale()));
  ImGui::PushStyleVar(
      ImGuiStyleVar_ItemSpacing,
      ImVec2(Theme::Space::S * Theme::scale(), 2.0f * Theme::scale()));
  ImGui::BeginChild("##sidebar", ImVec2(width, 0.0f),
                    ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_NoScrollbar);

  // Every hub should be visible without scrolling: on short windows (large
  // UI scales) the items shrink towards a compact height. The navigation
  // still scrolls (mouse wheel) as a last resort, so the footer actions
  // stay pinned and reachable at any window height and UI scale.
  const float spacing = ImGui::GetStyle().ItemSpacing.y;
  const bool unemployed = controller.isUnemployed();
  const size_t itemCount = static_cast<size_t>(std::ranges::count_if(
      NAV_HUBS, [unemployed](const NavHub& hub)
      { return firstOpenSection(hub, unemployed) != NavSection::NONE; }));
  // The current hub lists its screens under its label (one hub expanded at
  // a time: the one being worked in). Collapsed, a flyout lists them.
  const NavHub* currentHub = hubOf(navSection());
  std::array<NavSection, MAX_HUB_SECTIONS> subSections{};
  size_t subCount = currentHub != nullptr
                        ? openSections(*currentHub, unemployed, subSections)
                        : 0;
  if (collapsed || subCount < 2) subCount = 0;
  const float subRows = SUB_ITEM_RATIO * static_cast<float>(subCount);
  const float clubBlock =
      controller.getManagedTeam()
          ? (CLUB_BADGE_SIZE + Theme::Space::M) * Theme::scale() + spacing
          : 0.0f;
  const auto fitHeight = [&](size_t footerRows)
  {
    const float fixedHeight =
        clubBlock + 2.0f * Theme::Space::S * Theme::scale() + 3.0f * spacing;
    const auto rows = static_cast<float>(itemCount + footerRows + subCount);
    return (ImGui::GetContentRegionAvail().y - fixedHeight - rows * spacing) /
           (static_cast<float>(itemCount + footerRows) + subRows);
  };
  // Short windows: Save / Settings / Main menu share one row of icons.
  const bool compactFooter =
      !collapsed && fitHeight(3) < NAV_ITEM_MIN_HEIGHT * Theme::scale();
  const size_t footerRows = compactFooter ? 1 : 3;
  const float itemHeight =
      std::clamp(fitHeight(footerRows), NAV_ITEM_MIN_HEIGHT * Theme::scale(),
                 NAV_ITEM_HEIGHT * Theme::scale());
  const float footerHeight =
      static_cast<float>(footerRows) * (itemHeight + spacing) +
      Theme::Space::S * Theme::scale();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::BeginChild(
      "##sidebar_nav",
      ImVec2(0.0f, std::max(itemHeight,
                            ImGui::GetContentRegionAvail().y - footerHeight)),
      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
  ImGui::PopStyleVar();

  // Club identity block.
  if (const auto managed = controller.getManagedTeam())
  {
    const Team& club = managed->get();
    const float badgeSize = CLUB_BADGE_SIZE * Theme::scale();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 badgeMin(
        collapsed
            ? start.x + (ImGui::GetContentRegionAvail().x - badgeSize) * 0.5f
            : start.x + Theme::Space::XS * Theme::scale(),
        start.y);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (const ClubIdentity* identity = controller.getClubIdentity(club.getId()))
    {
      // Kit-coloured shield with the club's short code.
      UI::drawClubBadge(drawList,
                        ImVec2(badgeMin.x + badgeSize * 0.08f, badgeMin.y),
                        badgeSize, identity->short_name.c_str(),
                        identity->primary_colour, identity->secondary_colour);
    }
    else
    {
      drawList->AddRectFilled(
          badgeMin, ImVec2(badgeMin.x + badgeSize, badgeMin.y + badgeSize),
          Theme::toU32(palette.accent), 8.0f * Theme::scale());
      const std::string initials = clubInitials(club.getName());
      Theme::ScopedText heading(Theme::Text::TITLE);
      const ImVec2 initialsSize = ImGui::CalcTextSize(initials.c_str());
      drawList->AddText(
          ImVec2(badgeMin.x + (badgeSize - initialsSize.x) * 0.5f,
                 badgeMin.y + (badgeSize - initialsSize.y) * 0.5f),
          Theme::toU32(palette.on_accent), initials.c_str());
    }
    const float textX =
        badgeMin.x + badgeSize + Theme::Space::S * Theme::scale();
    const float textWidth = collapsed
                                ? 0.0f
                                : start.x + ImGui::GetContentRegionAvail().x -
                                      textX - 2.0f * Theme::scale();
    bool cut = false;
    if (textWidth > 0.0f)
    {
      cut |= UI::drawTextFitted(
          drawList, ImVec2(textX, start.y + 1.0f * Theme::scale()),
          Theme::toU32(palette.text), club.getName(), textWidth);
      if (const auto league = controller.getLeagueById(club.getLeagueId()))
      {
        ImGui::PushFont(nullptr, Theme::textSize(Theme::Text::SMALL));
        cut |= UI::drawTextFitted(
            drawList,
            ImVec2(textX, start.y + badgeSize - ImGui::GetTextLineHeight()),
            Theme::toU32(palette.muted),
            Competitions::leagueName(league->get()), textWidth);
        ImGui::PopFont();
      }
    }
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, badgeSize));
    if ((cut || collapsed) && ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", club.getName().c_str());
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::M * Theme::scale() -
                                  ImGui::GetStyle().ItemSpacing.y));
  }

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));
  const float subHeight = itemHeight * SUB_ITEM_RATIO;
  const NavHub* hoveredHub = nullptr;
  ImVec2 hoveredAnchor;
  for (const NavHub& hub : NAV_HUBS)
  {
    const NavSection first = firstOpenSection(hub, unemployed);
    if (first == NavSection::NONE) continue;
    std::array<NavSection, MAX_HUB_SECTIONS> sections{};
    const size_t count = openSections(hub, unemployed, sections);
    size_t badge = 0U;
    for (size_t index = 0; index < count; ++index)
      badge += sectionBadge(controller, sections[index]);
    const bool current = currentHub == &hub;
    const bool expanded = current && subCount > 0;
    // Collapsed: hubs with several screens open a flyout on hover.
    const bool flyout = collapsed && count > 1;
    if (navItem(LOC(hub.label_key), shortcutLabel(hub.action),
                current && !expanded, hub.icon, collapsed, itemHeight, badge,
                expanded, !flyout))
    {
      sidebarHasKeyboard = true;
      Navigation::open(guiView, first);
    }
    if (flyout && ImGui::IsItemHovered())
    {
      hoveredHub = &hub;
      hoveredAnchor =
          ImVec2(ImGui::GetItemRectMax().x, ImGui::GetItemRectMin().y);
    }
    if (!expanded) continue;
    for (size_t index = 0; index < subCount; ++index)
    {
      const NavSection section = subSections[index];
      if (subNavItem(LOC(labelKeyOf(section)), section == navSection(),
                     subHeight, sectionBadge(controller, section)) &&
          section != navSection())
      {
        sidebarHasKeyboard = true;
        Navigation::open(guiView, section);
      }
    }
  }
  sidebar_nav_overflow = ImGui::GetScrollMaxY() > 0.0f;

  ImGui::EndChild();
  if (hoveredHub != nullptr)
  {
    flyout_hub = static_cast<int>(hoveredHub - NAV_HUBS.data());
    flyout_anchor = hoveredAnchor;
    flyout_grace = FLYOUT_GRACE_SECONDS;
  }

  // Footer actions pinned to the bottom of the sidebar.
  ImGui::Separator();
  const float footerWidth =
      compactFooter ? (ImGui::GetContentRegionAvail().x - 2.0f * spacing) / 3.0f
                    : 0.0f;
  const auto footerAction =
      [&](const char* label, const char* shortcut, UI::Icon icon)
  {
    if (!compactFooter)
      return navItem(label, shortcut, false, icon, collapsed, itemHeight);
    return footerIcon(label, shortcut, icon, footerWidth, itemHeight);
  };
  if (footerAction(LOC("MAIN_GAME_SAVE_GAME"),
                   shortcutLabel(Input::Ids::CAREER_SAVE), UI::Icon::SAVE))
  {
    controller.saveGame();
    showToast(LOC("DASHBOARD_SAVED"));
  }
  if (compactFooter) ImGui::SameLine(0.0f, spacing);
  if (footerAction(LOC("MENU_SETTINGS"), nullptr, UI::Icon::SETTINGS))
    // Stacked above the current screen, so closing Settings returns to it.
    guiView->overlayScene(std::make_unique<SettingsScene>(guiView, true));
  if (compactFooter) ImGui::SameLine(0.0f, spacing);
  if (footerAction(LOC("MAIN_GAME_MAIN_MENU"), nullptr, UI::Icon::EXIT))
    main_menu_confirm_requested = true;

  // A click anywhere else hands the keyboard back to the page.
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
      !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
    sidebarHasKeyboard = false;
  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
  if (collapsed) renderSidebarFlyout();
}

void ManagementScene::renderSidebarFlyout()
{
  if (flyout_hub < 0) return;
  const NavHub& hub = NAV_HUBS[static_cast<size_t>(flyout_hub)];
  const GameController& controller = guiView->getController();
  std::array<NavSection, MAX_HUB_SECTIONS> sections{};
  const size_t count = openSections(hub, controller.isUnemployed(), sections);
  if (count < 2)
  {
    flyout_hub = -1;
    return;
  }
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  ImGui::SetNextWindowPos(flyout_anchor);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(Theme::Space::S * scale, Theme::Space::S * scale));
  ImGui::Begin(FLYOUT_WINDOW_ID, nullptr,
               ImGuiWindowFlags_NoDecoration |
                   ImGuiWindowFlags_AlwaysAutoResize |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoFocusOnAppearing |
                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
  ImGui::PopStyleVar();
  {
    Theme::ScopedText caption(Theme::Text::CAPTION);
    ImGui::TextColored(palette.muted, "%s   %s", LOC(hub.label_key),
                       shortcutLabel(hub.action));
  }
  float width = 0.0f;
  for (size_t index = 0; index < count; ++index)
    width = std::max(width,
                     ImGui::CalcTextSize(LOC(labelKeyOf(sections[index]))).x);
  width += countPillWidth(99) + 2.0f * Theme::Space::M * scale;
  bool chosen = false;
  for (size_t index = 0; index < count; ++index)
  {
    const NavSection section = sections[index];
    const size_t badge = sectionBadge(controller, section);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    if (ImGui::Selectable(LOC(labelKeyOf(section)), section == navSection(), 0,
                          ImVec2(width, NAV_ITEM_MIN_HEIGHT * scale)) &&
        section != navSection())
    {
      sidebarHasKeyboard = true;
      chosen = true;
      Navigation::open(guiView, section);
    }
    if (badge > 0)
      drawCountPill(ImGui::GetWindowDrawList(),
                    ImVec2(start.x + width - countPillWidth(badge), start.y),
                    NAV_ITEM_MIN_HEIGHT * scale, badge);
  }
  const bool hovered = ImGui::IsWindowHovered();
  ImGui::End();
  if (hovered) flyout_grace = FLYOUT_GRACE_SECONDS;
  flyout_grace -= ImGui::GetIO().DeltaTime;
  if (chosen || flyout_grace <= 0.0f) flyout_hub = -1;
}

void ManagementScene::renderTopBar(float height)
{
  const Theme::Palette& palette = Theme::palette();
  GameController& controller = guiView->getController();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(Theme::Space::XL * Theme::scale(), 0.0f));
  ImGui::BeginChild("##topbar", ImVec2(0.0f, height),
                    ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_NoScrollbar);
  ImGui::PopStyleVar();
  ImDrawList* drawList = ImGui::GetWindowDrawList();

  const ImGuiStyle& style = ImGui::GetStyle();
  const float frameHeight = ImGui::GetFrameHeight();
  const float centeredY = (height - frameHeight) * 0.5f;
  // Text sharing a line with framed widgets is baseline-aligned by ImGui.
  const float textY = centeredY;
  const float gap = Theme::Space::L * Theme::scale();

  // Right cluster (date, balance, continue) is measured first so the left
  // side can shrink on narrow windows instead of overlapping it.
  MainGameScene* hub = careerHub(guiView);
  const std::string dateText = Format::date(controller.getCurrentDate());
  std::string balanceText;
  bool negativeBalance = false;
  if (const auto managed = controller.getManagedTeam())
  {
    const int64_t balance = managed->get().getFinances().getBalance();
    negativeBalance = balance < 0;
    balanceText = Format::money(balance);
  }
  const std::string continueText =
      hub != nullptr ? hub->continueLabel() : std::string();
  const float continueWidth =
      continueText.empty()
          ? 0.0f
          : std::max(CONTINUE_MIN_WIDTH * Theme::scale(),
                     ImGui::CalcTextSize(continueText.c_str()).x +
                         4.0f * style.FramePadding.x);
  const float dateWidth = ImGui::CalcTextSize(dateText.c_str()).x;
  const float balanceWidth = ImGui::CalcTextSize(balanceText.c_str()).x;
  // Right edge from the space actually available this frame, clamped to the
  // visible viewport. GetWindowContentRegionMax() can be stale and oversized on
  // a scene's first frames, which pushed the right-hand cluster off-screen.
  const float leftEdge = ImGui::GetCursorPosX();
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float viewportRight =
      viewport ? viewport->WorkPos.x + viewport->WorkSize.x : 0.0f;
  const float localViewportRight = viewportRight - ImGui::GetWindowPos().x -
                                   ImGui::GetStyle().WindowPadding.x;
  const float rightEdge = std::min(leftEdge + ImGui::GetContentRegionAvail().x,
                                   std::max(leftEdge, localViewportRight));

  // Back and Forward walk the history like a browser's; each is disabled
  // while there is nothing to go to.
  const std::string backLabel =
      std::string("‹  ") + LOC("NAV_BACK") + "###shell_back";
  const bool canGoBack = Navigation::canGoBack(guiView);
  const bool canGoForward = Navigation::canGoForward(guiView);
  const float forwardWidth = UI::buttonHeight();
  const float backWidth = UI::buttonWidth(backLabel.c_str()) + forwardWidth +
                          3.0f * style.ItemSpacing.x + UI::buttonHeight();
  const float fullSearchWidth = 300.0f * Theme::scale();
  const float compactSearchWidth = frameHeight + 2.0f * Theme::scale();
  const float balanceBlock = balanceWidth + gap;
  // The calendar button next to Continue opens the holiday planner.
  const float holidayWidth =
      hub != nullptr && continueWidth > 0.0f && controller.hasSelectedTeam()
          ? UI::buttonHeight()
          : 0.0f;
  const float continueBlock =
      continueWidth > 0.0f
          ? continueWidth + gap +
                (holidayWidth > 0.0f ? holidayWidth + style.ItemSpacing.x
                                     : 0.0f)
          : 0.0f;
  const float available = rightEdge - leftEdge - backWidth - continueBlock -
                          balanceBlock - style.ItemSpacing.x;
  const bool showDate = available - dateWidth - gap >= compactSearchWidth;
  // The save indicator is the first thing to go when space runs out.
  const float saveWidth = save_label.empty()
                              ? 0.0f
                              : ImGui::CalcTextSize(save_label.c_str()).x + gap;
  const bool showSave =
      showDate && saveWidth > 0.0f &&
      available - dateWidth - gap - saveWidth >= fullSearchWidth;
  const float dateBlock =
      showDate ? dateWidth + gap + (showSave ? saveWidth : 0.0f) : 0.0f;
  const float searchRoom = available - dateBlock;
  const bool fullSearch = searchRoom >= fullSearchWidth;
  const float searchWidth = fullSearch ? fullSearchWidth : compactSearchWidth;

  {
    const ImGuiHoveredFlags hint =
        ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal;
    ImGui::SetCursorPosY((height - UI::buttonHeight()) * 0.5f);
    ImGui::BeginDisabled(!canGoBack);
    const bool backPressed = UI::secondaryButton(backLabel.c_str());
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(hint))
      ImGui::SetTooltip("%s", LOC("NAV_BACK_HINT"));
    ImGui::SameLine();
    ImGui::SetCursorPosY((height - UI::buttonHeight()) * 0.5f);
    ImGui::BeginDisabled(!canGoForward);
    const bool forwardPressed = UI::secondaryButton(
        "›###shell_forward", ImVec2(forwardWidth, UI::buttonHeight()));
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(hint))
      ImGui::SetTooltip("%s", LOC("NAV_FORWARD_HINT"));
    if (backPressed)
      Navigation::back(guiView);
    else if (forwardPressed)
      Navigation::forward(guiView);
    ImGui::SameLine();
    ImGui::SetCursorPosY(centeredY);
  }

  const auto editPos = ImGui::GetCursorScreenPos();
  if (UI::secondaryButton("###shell_edit",
                          ImVec2(UI::buttonHeight(), UI::buttonHeight())))
  {
    if (const auto context = editorContext())
      guiView->openEditor(*context);
    else
      guiView->openEditor(historyEntry());
  }
  UI::drawIcon(drawList, UI::Icon::EDIT,
               ImVec2(editPos.x + UI::buttonHeight() * .5f,
                      editPos.y + UI::buttonHeight() * .5f),
               16 * Theme::scale(), ImGui::GetColorU32(palette.text));
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", LOC("EDITOR_OPEN"));
  ImGui::SameLine();
  ImGui::SetCursorPosY(centeredY);

  // Search launcher styled as an input field with a drawn magnifier.
  {
    const std::string searchLabel =
        fullSearch
            ? std::format("      {}###shell_search", LOC("PALETTE_LAUNCHER"))
            : std::string("###shell_search");
    ImGui::PushStyleColor(ImGuiCol_Button, palette.surface);
    ImGui::PushStyleColor(ImGuiCol_Text, palette.faint);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    const ImVec2 searchPos = ImGui::GetCursorScreenPos();
    if (ImGui::Button(searchLabel.c_str(), ImVec2(searchWidth, 0.0f)))
      openPalette();
    if (!fullSearch && ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", LOC("PALETTE_LAUNCHER"));
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    const float iconSize = 14.0f * Theme::scale();
    UI::drawIcon(
        drawList, UI::Icon::SEARCH,
        ImVec2(fullSearch ? searchPos.x + style.FramePadding.x + iconSize * 0.5f
                          : searchPos.x + searchWidth * 0.5f,
               searchPos.y + frameHeight * 0.5f),
        iconSize, Theme::toU32(palette.faint));
  }
  // The right-hand cluster is right-aligned but never placed left of the
  // search field. Anchoring it here (not to the transient toast on the same
  // line) keeps the Play Match button inside the window when a toast shows.
  const float searchEndX = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;

  if (toast_seconds > 0.0f && !toast_message.empty())
  {
    const float alpha =
        Theme::reducedMotion() ? 1.0f : std::min(1.0f, toast_seconds / 0.4f);
    ImGui::SameLine(0.0f, gap);
    ImGui::SetCursorPosY(textY);
    const float toastRoom = rightEdge - dateBlock - balanceBlock -
                            continueBlock - gap - ImGui::GetCursorPosX();
    const ImVec4& tone = toast_is_error ? palette.negative : palette.positive;
    const ImVec2 clipMin = ImGui::GetCursorScreenPos();
    ImGui::PushClipRect(
        clipMin,
        ImVec2(clipMin.x + std::max(0.0f, toastRoom), clipMin.y + frameHeight),
        true);
    ImGui::TextColored(ImVec4(tone.x, tone.y, tone.z, alpha), "%s",
                       toast_message.c_str());
    ImGui::PopClipRect();
  }

  const float clusterWidth = dateBlock + balanceWidth + continueBlock;
  ImGui::SameLine();
  const float clusterX = std::max(searchEndX, rightEdge - clusterWidth);
  ImGui::SetCursorPos(ImVec2(clusterX, textY));
  if (showSave)
  {
    ImGui::TextColored(save_failed ? palette.negative : palette.faint, "%s",
                       save_label.c_str());
    ImGui::SameLine(0.0f, gap);
    ImGui::SetCursorPosY(textY);
  }
  if (showDate)
  {
    ImGui::TextColored(palette.muted, "%s", dateText.c_str());
    ImGui::SameLine(0.0f, gap);
    ImGui::SetCursorPosY(textY);
  }
  ImGui::TextColored(negativeBalance ? palette.negative : palette.text, "%s",
                     balanceText.c_str());
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", LOC("FINANCE_CASH"));
  if (hub != nullptr && continueWidth > 0.0f)
  {
    ImGui::SameLine(0.0f, gap);
    ImGui::SetCursorPosY((height - UI::buttonHeight()) * 0.5f);
    const std::string label = continueText + "###shell_continue";
    if (UI::primaryButton(label.c_str(), ImVec2(continueWidth, 0.0f)))
      hub->requestContinue();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", LOC("SHELL_CONTINUE_TOOLTIP"));
    if (holidayWidth > 0.0f)
    {
      ImGui::SameLine();
      ImGui::SetCursorPosY((height - UI::buttonHeight()) * 0.5f);
      if (UI::secondaryButton("###shell_holiday",
                              ImVec2(holidayWidth, UI::buttonHeight())))
        holiday_dialog.open(controller);
      const ImVec2 min = ImGui::GetItemRectMin();
      const ImVec2 max = ImGui::GetItemRectMax();
      UI::drawIcon(drawList, UI::Icon::FIXTURES,
                   ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f),
                   ICON_SIZE * Theme::scale(), Theme::toU32(palette.text));
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", LOC("SHELL_HOLIDAY_TOOLTIP"));
    }
  }
  ImGui::EndChild();
  const ImVec2 barMin = ImGui::GetItemRectMin();
  const ImVec2 barMax = ImGui::GetItemRectMax();
  ImGui::GetWindowDrawList()->AddRectFilled(
      ImVec2(barMin.x, barMax.y - 1.0f), barMax, Theme::toU32(palette.border));
}

void ManagementScene::pollSaveStatus()
{
  const GameController::SaveStatusInfo status =
      guiView->getController().getSaveStatus();
  // A failure is announced once per career session, not once per screen.
  static int announced_failure_after = -1;
  save_failed = !status.ok;
  if (!status.ok)
  {
    save_label = LOC("SAVE_STATUS_FAILED");
    if (announced_failure_after != status.successful_saves)
    {
      announced_failure_after = status.successful_saves;
      showToast(
          formatLocalized(status.error.langKey(),
                          {std::to_string(status.error.found_version),
                           std::to_string(status.error.supported_version)}),
          true);
    }
    return;
  }
  announced_failure_after = -1;
  if (status.successful_saves == 0 || status.game_date.empty())
  {
    save_label.clear();
    return;
  }
  save_label = std::format(
      "{} \u00B7 {}",
      LOC(status.autosave ? "SAVE_AUTOSAVED" : "SAVE_STATUS_OK"),
      Format::dayMonth(GameDateValue::fromString(status.game_date)));
}

void ManagementScene::handleShortcuts()
{
  // A side button or swipe from this frame's events; dropped below when a
  // dialog or text field has the input.
  const SwipeGesture::Step historyStep =
      std::exchange(pending_history_step, SwipeGesture::Step::NONE);
  const Input::ActionRegistry& keys = Input::registry();
  if (keys.pressed(Input::Ids::NAV_PALETTE)) openPalette();
  if (keys.pressed(Input::Ids::CAREER_SAVE))
  {
    guiView->getController().saveGame();
    showToast(LOC("DASHBOARD_SAVED"));
  }

  const ImGuiIO& io = ImGui::GetIO();
  if (io.WantTextInput || ImGui::IsAnyItemActive() ||
      ImGui::IsPopupOpen(
          "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
    return;

  // F1-F7 open the sidebar hubs (their first screen).
  const bool unemployed = guiView->getController().isUnemployed();
  for (const NavHub& hub : NAV_HUBS)
  {
    const NavSection first = firstOpenSection(hub, unemployed);
    if (first != NavSection::NONE && keys.pressed(hub.action))
    {
      sidebarHasKeyboard = true;
      Navigation::open(guiView, first);
      return;
    }
  }
  // Up/Down walk the current hub's screens while the sidebar has the
  // keyboard (claimed so keyboard navigation does not move as well).
  if (sidebarHasKeyboard)
    if (const NavHub* hub = hubOf(navSection()))
    {
      const ImGuiInputFlags route =
          ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_Repeat;
      const int step = ImGui::Shortcut(ImGuiKey_DownArrow, route) ? 1
                       : ImGui::Shortcut(ImGuiKey_UpArrow, route) ? -1
                                                                  : 0;
      std::array<NavSection, MAX_HUB_SECTIONS> sections{};
      const auto count =
          static_cast<int>(openSections(*hub, unemployed, sections));
      const auto current = static_cast<int>(
          std::ranges::find(sections.begin(), sections.begin() + count,
                            navSection()) -
          sections.begin());
      const int next = current + step;
      if (step != 0 && current < count && next >= 0 && next < count)
      {
        Navigation::open(guiView, sections[static_cast<size_t>(next)]);
        return;
      }
    }
  // Space / Enter continue only while keyboard navigation is not driving a
  // focused widget, so they never double as widget activation.
  if (keys.pressed(Input::Ids::CAREER_HELP))
  {
    guiView->navigateTo(std::make_unique<HelpScene>(guiView, true));
    return;
  }
  if (!io.NavVisible && keys.pressed(Input::Ids::CAREER_CONTINUE))
  {
    if (MainGameScene* hub = careerHub(guiView)) hub->requestContinue();
    return;
  }
  // Escape is claimed through the shortcut router so keyboard navigation
  // does not also treat it as "cancel" and light up a focus frame.
  if (keys.pressed(Input::Ids::NAV_CLOSE))
  {
    Navigation::close(guiView);
    return;
  }
  if (keys.pressed(Input::Ids::NAV_BACK) ||
      historyStep == SwipeGesture::Step::BACK)
    Navigation::back(guiView);
  else if (keys.pressed(Input::Ids::NAV_FORWARD) ||
           historyStep == SwipeGesture::Step::FORWARD)
    Navigation::forward(guiView);
}

void ManagementScene::renderSwipeIndicator()
{
  const float progress = guiView->swipeGesture().progress(SDL_GetTicksNS());
  if (progress == 0.0f) return;
  const bool back = progress > 0.0f;
  const float amount = std::fabs(progress);
  const bool available =
      amount >= 1.0f || (back ? Navigation::canGoBack(guiView)
                              : Navigation::canGoForward(guiView));
  const float scale = Theme::scale();
  const float radius = SWIPE_BUBBLE_RADIUS * scale;
  // The bubble slides in from the edge as the fingers travel.
  const float travel = amount * (2.0f * radius + SWIPE_BUBBLE_MARGIN * scale);
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float left = viewport->WorkPos.x;
  const float right = viewport->WorkPos.x + viewport->WorkSize.x;
  const ImVec2 centre(back ? left - radius + travel : right + radius - travel,
                      viewport->WorkPos.y + viewport->WorkSize.y * 0.5f);
  const float alpha = std::min(1.0f, amount * 1.5f);
  // A complete swipe has taken its step (see handleEvent()).
  const bool armed = amount >= 1.0f;
  const Theme::Palette& palette = Theme::palette();
  ImDrawList* drawList = ImGui::GetForegroundDrawList();
  drawList->AddCircleFilled(centre, radius,
                            Theme::toU32(palette.raised, alpha));
  drawList->AddCircle(centre, radius, Theme::toU32(palette.border, alpha), 0,
                      1.0f * scale);
  const ImU32 ink = Theme::toU32(armed       ? palette.accent
                                 : available ? palette.text
                                             : palette.faint,
                                 alpha);
  const float arm = radius * 0.36f;
  const float pointing = back ? -1.0f : 1.0f;
  const ImVec2 tip(centre.x + pointing * arm * 0.5f, centre.y);
  const float thickness = 2.5f * scale;
  drawList->AddLine(tip, ImVec2(tip.x - pointing * arm, centre.y - arm), ink,
                    thickness);
  drawList->AddLine(tip, ImVec2(tip.x - pointing * arm, centre.y + arm), ink,
                    thickness);
}

void ManagementScene::openPalette()
{
  palette_requested = true;
  palette_focus_input = true;
  palette_query.fill('\0');
  palette_selection = 0;
}

void ManagementScene::buildPaletteIndex()
{
  palette_entries.clear();
  GameController& controller = guiView->getController();
  // Pending work first: shown while the query is empty.
  palette_actions = controller.getNextActions(PALETTE_MAX_ACTIONS);
  for (size_t index = 0; index < palette_actions.size(); ++index)
  {
    PaletteEntry item{PaletteEntry::Kind::ACTION,
                      static_cast<uint32_t>(index),
                      GuidanceUI::text(palette_actions[index].title),
                      {},
                      std::string(LOC("PALETTE_KIND_NEXT_STEP"))};
    item.label_lower = PlayerView::toLower(item.label);
    palette_entries.push_back(std::move(item));
  }
  const bool unemployed = controller.isUnemployed();
  for (const NavScreen& entry : ALL_NAV)
  {
    if (!sectionOpen(entry.section, unemployed)) continue;
    // A hub's first screen shows its shortcut, the others their hub.
    const NavHub* hub = hubOf(entry.section);
    std::string detail = LOC("PALETTE_KIND_SCREEN");
    if (hub != nullptr && firstOpenSection(*hub, unemployed) == entry.section)
      detail += std::format("  ·  {}", shortcutLabel(hub->action));
    else if (hub != nullptr)
      detail += std::format("  ·  {}", LOC(hub->label_key));
    PaletteEntry item{PaletteEntry::Kind::SECTION,
                      static_cast<uint32_t>(entry.section),
                      LOC(entry.label_key),
                      {},
                      std::move(detail)};
    item.label_lower = PlayerView::toLower(item.label);
    palette_entries.push_back(std::move(item));
  }
  {
    PaletteEntry about{PaletteEntry::Kind::ABOUT,
                       0,
                       LOC("PALETTE_ABOUT"),
                       {},
                       std::string(LOC("PALETTE_KIND_INFO"))};
    about.label_lower = PlayerView::toLower(about.label);
    palette_entries.push_back(std::move(about));
    PaletteEntry help{PaletteEntry::Kind::HELP,
                      0,
                      LOC("PALETTE_HELP"),
                      {},
                      std::format("{}  ·  {}", LOC("PALETTE_KIND_HELP"),
                                  shortcutLabel(Input::Ids::CAREER_HELP))};
    help.label_lower = PlayerView::toLower(help.label);
    palette_entries.push_back(std::move(help));
    const auto terms = Glossary::terms();
    for (size_t index = 0; index < terms.size(); ++index)
    {
      PaletteEntry term{PaletteEntry::Kind::HELP,
                        static_cast<uint32_t>(index + 1),
                        LOC(terms[index].term_key),
                        {},
                        std::string(LOC("PALETTE_KIND_GLOSSARY"))};
      term.label_lower = PlayerView::toLower(term.label);
      palette_entries.push_back(std::move(term));
    }
  }
  for (const auto& teamRef : controller.getTeams())
  {
    const Team& team = teamRef.get();
    if (team.getId() == FREE_AGENTS_TEAM_ID) continue;
    const auto league = controller.getLeagueById(team.getLeagueId());
    PaletteEntry item{
        PaletteEntry::Kind::CLUB,
        team.getId(),
        team.getName(),
        {},
        std::format(
            "{}  ·  {}", LOC("PALETTE_KIND_CLUB"),
            league ? Competitions::leagueName(league->get()) : std::string())};
    item.label_lower = PlayerView::toLower(item.label);
    palette_entries.push_back(std::move(item));
  }
  const auto data = controller.getGameData();
  if (!data) return;
  palette_entries.reserve(palette_entries.size() + data->getPlayers().size());
  for (const auto& playerRef : data->getPlayersVector())
  {
    const Player& player = playerRef.get();
    const auto team = controller.getTeamById(player.getTeamId());
    PaletteEntry item{
        PaletteEntry::Kind::PLAYER,
        player.getId(),
        player.getName(),
        {},
        std::format("{}  ·  {}", RoleUtils::shortName(player.getRole()),
                    team && player.getTeamId() != FREE_AGENTS_TEAM_ID
                        ? team->get().getName()
                        : std::string(LOC("TRANSFER_FREE_AGENT_LABEL")))};
    item.label_lower = PlayerView::toLower(item.label);
    palette_entries.push_back(std::move(item));
  }
}

void ManagementScene::filterPalette()
{
  palette_filtered_query = palette_query.data();
  palette_matches.clear();
  palette_selection = 0;
  const std::string query = PlayerView::toLower(palette_filtered_query);
  if (query.empty())
  {
    for (size_t index = 0; index < palette_entries.size(); ++index)
      if (palette_entries[index].kind == PaletteEntry::Kind::ACTION ||
          palette_entries[index].kind == PaletteEntry::Kind::SECTION ||
          (palette_entries[index].kind == PaletteEntry::Kind::HELP &&
           palette_entries[index].id == 0))
        palette_matches.push_back(index);
    return;
  }
  // Prefix matches (on any word) rank above plain substring matches.
  std::vector<size_t> substring;
  for (size_t index = 0; index < palette_entries.size(); ++index)
  {
    const std::string& label = palette_entries[index].label_lower;
    const size_t found = label.find(query);
    if (found == std::string::npos) continue;
    const bool wordStart = found == 0 || label[found - 1] == ' ';
    if (wordStart)
    {
      palette_matches.push_back(index);
      if (palette_matches.size() >= PALETTE_MAX_RESULTS) return;
    }
    else if (substring.size() < PALETTE_MAX_RESULTS)
      substring.push_back(index);
  }
  for (const size_t index : substring)
  {
    if (palette_matches.size() >= PALETTE_MAX_RESULTS) break;
    palette_matches.push_back(index);
  }
}

void ManagementScene::activatePaletteEntry(const PaletteEntry& entry)
{
  switch (entry.kind)
  {
    case PaletteEntry::Kind::SECTION:
      Navigation::open(guiView, static_cast<NavSection>(entry.id));
      break;
    case PaletteEntry::Kind::CLUB:
      Navigation::openClub(guiView, static_cast<TeamID>(entry.id));
      break;
    case PaletteEntry::Kind::PLAYER:
      Navigation::openPlayer(guiView, entry.id);
      break;
    case PaletteEntry::Kind::ACTION:
      if (entry.id < palette_actions.size())
        GuidanceUI::openAction(guiView, palette_actions[entry.id]);
      break;
    case PaletteEntry::Kind::ABOUT:
      guiView->navigateTo(std::make_unique<AboutScene>(guiView, true));
      break;
    case PaletteEntry::Kind::HELP:
      guiView->navigateTo(std::make_unique<HelpScene>(
          guiView, true,
          entry.id == 0 ? std::nullopt
                        : std::optional<std::size_t>(entry.id - 1)));
      break;
  }
}

void ManagementScene::renderPalette()
{
  if (palette_requested)
  {
    palette_requested = false;
    buildPaletteIndex();
    filterPalette();
    ImGui::OpenPopup(PALETTE_POPUP_ID);
  }
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
             viewport->WorkPos.y + PALETTE_TOP_OFFSET * Theme::scale()),
      ImGuiCond_Always, ImVec2(0.5f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(PALETTE_WIDTH * Theme::scale(), 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(Theme::Space::M * Theme::scale(),
                             Theme::Space::M * Theme::scale()));
  const bool open = ImGui::BeginPopup(
      PALETTE_POPUP_ID,
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::PopStyleVar();
  if (!open)
  {
    if (!palette_entries.empty())
    {
      palette_entries.clear();
      palette_entries.shrink_to_fit();
      palette_matches.clear();
    }
    return;
  }

  if (palette_focus_input)
  {
    ImGui::SetKeyboardFocusHere();
    palette_focus_input = false;
  }
  ImGui::SetNextItemWidth(-FLT_MIN);
  const bool submitted =
      ImGui::InputTextWithHint("##palette_query", LOC("PALETTE_HINT"),
                               palette_query.data(), palette_query.size(),
                               ImGuiInputTextFlags_EnterReturnsTrue |
                                   ImGuiInputTextFlags_CallbackHistory,
                               paletteHistoryCallback, &palette_selection);
  if (palette_filtered_query != palette_query.data()) filterPalette();
  const int matchCount = static_cast<int>(palette_matches.size());
  if (matchCount > 0)
    palette_selection = std::clamp(palette_selection, 0, matchCount - 1);

  const PaletteEntry* chosen = nullptr;
  if (submitted && matchCount > 0)
    chosen = &palette_entries[palette_matches[static_cast<size_t>(
        palette_selection)]];
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();

  ImGui::Dummy(ImVec2(0.0f, 2.0f * Theme::scale()));
  if (matchCount == 0)
  {
    ImGui::TextColored(Theme::palette().muted, "%s", LOC("PALETTE_NO_MATCH"));
  }
  for (int row = 0; row < matchCount; ++row)
  {
    const PaletteEntry& entry =
        palette_entries[palette_matches[static_cast<size_t>(row)]];
    ImGui::PushID(row);
    const bool selected = row == palette_selection;
    if (ImGui::Selectable("##palette_row", selected, 0,
                          ImVec2(0.0f, ImGui::GetTextLineHeight())))
      chosen = &entry;
    ImGui::SameLine(Theme::Space::M * Theme::scale());
    ImGui::TextUnformatted(entry.label.c_str());
    ImGui::SameLine();
    {
      Theme::ScopedText small(Theme::Text::SMALL);
      UI::textRightColored(Theme::palette().faint, entry.detail.c_str());
    }
    ImGui::PopID();
  }
  if (chosen != nullptr)
  {
    const PaletteEntry selectedEntry = *chosen;
    ImGui::CloseCurrentPopup();
    activatePaletteEntry(selectedEntry);
  }
  ImGui::EndPopup();
}

void ManagementScene::renderMainMenuConfirm()
{
  if (main_menu_confirm_requested)
  {
    main_menu_confirm_requested = false;
    ImGui::OpenPopup(MAIN_MENU_CONFIRM_ID);
  }
  if (UI::confirmDialog(MAIN_MENU_CONFIRM_ID, LOC("SHELL_MAIN_MENU_TITLE"),
                        LOC("SHELL_MAIN_MENU_BODY"), LOC("MAIN_GAME_MAIN_MENU"),
                        LOC("SETTINGS_CANCEL")) == UI::DialogResult::CONFIRM)
    changeScene(std::make_unique<MainMenuScene>(guiView));
}
