// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "team_selection_scene.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <utility>

#include "database/gamedata.h"
#include "global/global.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/render/match_kit_colors.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/main_menu_scene.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/board.h"
#include "model/competition.h"

namespace
{
constexpr float LEAGUE_LIST_WIDTH = 260.0f;
constexpr float CARD_WIDTH = 320.0f;
constexpr float CARD_MIN_CONTENT = 1080.0f;
constexpr float CONTENT_MAX_WIDTH = 1480.0f;
// Below this the page scrolls rather than squeezing the club browser.
constexpr float BROWSER_MIN_HEIGHT = 360.0f;
/** Stacked layout: the club table sits beside the league list from here. */
constexpr float STACKED_TABLE_MIN_WIDTH = 480.0f;
constexpr size_t KEY_PLAYERS = 3;
constexpr int STARS = 5;

enum class ClubColumn : ImGuiID
{
  NAME = 1,
  REPUTATION,
  STADIUM,
  AVERAGE,
  EXPECTATION,
  DIFFICULTY,
  BALANCE,
};

constexpr std::array<const char*, 4> DIFFICULTY_KEYS = {
    "TEAM_SELECTION_DIFFICULTY_EASY", "TEAM_SELECTION_DIFFICULTY_NORMAL",
    "TEAM_SELECTION_DIFFICULTY_HARD", "TEAM_SELECTION_DIFFICULTY_VERY_HARD"};

ImVec4 difficultyColor(int difficulty)
{
  const Theme::Palette& palette = Theme::palette();
  switch (difficulty)
  {
    case 0:
      return palette.positive;
    case 1:
      return palette.info;
    case 2:
      return palette.warning;
    default:
      return palette.negative;
  }
}

// Reputation 1-100 as five drawn stars (half stars included).
void reputationStars(uint8_t reputation)
{
  const Theme::Palette& palette = Theme::palette();
  const float size = ImGui::GetTextLineHeight() * 0.8f;
  const float gap = 2.0f * Theme::scale();
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const float top = start.y + (ImGui::GetTextLineHeight() - size) * 0.5f;
  const float value =
      static_cast<float>(reputation) / 100.0f * static_cast<float>(STARS);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  for (int star = 0; star < STARS; ++star)
  {
    const ImVec2 center(
        start.x + size * 0.5f + static_cast<float>(star) * (size + gap),
        top + size * 0.5f);
    std::array<ImVec2, 10> points{};
    for (size_t index = 0; index < points.size(); ++index)
    {
      const float angle = -1.5708f + static_cast<float>(index) * 0.6283f;
      const float radius = (index % 2 == 0 ? 0.5f : 0.22f) * size;
      points[index] = ImVec2(center.x + std::cos(angle) * radius,
                             center.y + std::sin(angle) * radius);
    }
    const float fill = std::clamp(value - static_cast<float>(star), 0.0f, 1.0f);
    drawList->AddConvexPolyFilled(points.data(),
                                  static_cast<int>(points.size()),
                                  Theme::toU32(palette.raised));
    if (fill > 0.0f)
    {
      drawList->PushClipRect(
          ImVec2(center.x - size * 0.5f, top),
          ImVec2(center.x - size * 0.5f + size * fill, top + size), true);
      drawList->AddConvexPolyFilled(points.data(),
                                    static_cast<int>(points.size()),
                                    Theme::toU32(palette.warning));
      drawList->PopClipRect();
    }
  }
  ImGui::Dummy(ImVec2(static_cast<float>(STARS) * (size + gap), size));
}

/** Packed ImGui colour (ABGR) to 0xRRGGBB. */
uint32_t toRgb(ImU32 colour)
{
  return ((colour & 0xFFU) << 16U) | (colour & 0xFF00U) |
         ((colour >> 16U) & 0xFFU);
}
}  // namespace

TeamSelectionScene::TeamSelectionScene(GUIView* parent) : GUIScene(parent) {}

void TeamSelectionScene::onEnter()
{
  loadAvailableLeagues();
  if (!league_entries.empty()) selectLeague(league_entries.front());
}

void TeamSelectionScene::update(float deltaTime) { (void)deltaTime; }

void TeamSelectionScene::render()
{
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(Theme::Space::XL * Theme::scale(),
                             Theme::Space::XL * Theme::scale()));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  // The page scrolls (and is the only scroll surface) when the window is too
  // short for the club browser below the manager card.
  ImGui::Begin("##team_selection", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings);
  ImGui::PopStyleVar(3);
  // Auto-sized cards (the manager card) measure themselves on their first
  // frame: that frame is drawn transparent instead of as empty boxes.
  const bool measuring = !laid_out;
  if (measuring) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
  laid_out = true;

  const float width = std::min(ImGui::GetContentRegionAvail().x,
                               CONTENT_MAX_WIDTH * Theme::scale());
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                                (ImGui::GetWindowWidth() - width) * 0.5f));
  ImGui::BeginGroup();
  // The way out of the club choice: Back (or Escape, unless a field or a
  // list is taking keys) returns to the main menu.
  if (const bool escape = !ImGui::IsAnyItemActive() &&
                          !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
                          ImGui::IsKeyPressed(ImGuiKey_Escape, false);
      UI::secondaryButton(LOC("TEAM_SELECTION_BACK"), ImVec2(0.0f, 0.0f),
                          UI::ButtonSize::COMPACT) ||
      escape)
    backToMenu();
  UI::pageHeader(LOC("TEAM_SELECTION_TITLE"), LOC("TEAM_SELECTION_SUBTITLE"));
  if (manager_panel.render(guiView->getController(), width) ==
      ManagerSetupPanel::Action::START_UNEMPLOYED)
    startUnemployed();
  // Room left under the manager card (the page's scroll does not change it).
  const float room = ImGui::GetContentRegionAvail().y;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const float listWidth = LEAGUE_LIST_WIDTH * Theme::scale();
  if (room < BROWSER_MIN_HEIGHT * Theme::scale())
  {
    renderStackedBrowser(width);
  }
  else
  {
    // The browser fills the window exactly: the page does not scroll and
    // the league list and club table scroll on their own.
    const float height = room;
    const bool showCard = width >= CARD_MIN_CONTENT * Theme::scale();
    const float cardWidth = showCard ? CARD_WIDTH * Theme::scale() : 0.0f;
    renderLeagueList(listWidth, height);
    ImGui::SameLine();
    const float tableWidth =
        width - listWidth - gap - (showCard ? cardWidth + gap : 0.0f);
    ImGui::BeginChild(
        "##club_area", ImVec2(tableWidth, height), ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // Narrow windows: the club card becomes a strip above the table, so the
    // start button stays in view.
    if (!showCard) renderSelectedClubStrip();
    if (selected_league_id.has_value())
      renderClubTable(ImGui::GetContentRegionAvail().y);
    else
      UI::emptyState(LOC("TEAM_SELECTION_SELECT_LEAGUE_FIRST"), nullptr);
    ImGui::EndChild();
    if (showCard)
    {
      ImGui::SameLine();
      renderSelectedClub(cardWidth, height);
    }
  }
  ImGui::EndGroup();
  if (measuring) ImGui::PopStyleVar();
  ImGui::End();
}

void TeamSelectionScene::renderStackedBrowser(float width)
{
  // Too short for the club browser (small window or big UI scale): every
  // card takes its natural height and the page is the only scroll surface.
  // The club strip with the start button comes first.
  renderSelectedClubStrip();
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const float listWidth = LEAGUE_LIST_WIDTH * Theme::scale();
  if (const bool sideBySide =
          width - listWidth - gap >= STACKED_TABLE_MIN_WIDTH * Theme::scale();
      sideBySide)
  {
    ImGui::BeginGroup();
    renderLeagueList(listWidth, 0.0f);
    ImGui::EndGroup();
    ImGui::SameLine();
  }
  else
  {
    renderLeagueList(width, 0.0f);
  }
  ImGui::BeginGroup();
  if (selected_league_id.has_value())
    renderClubTable(0.0f);
  else
    UI::emptyState(LOC("TEAM_SELECTION_SELECT_LEAGUE_FIRST"), nullptr);
  ImGui::EndGroup();
}

void TeamSelectionScene::selectLeague(const LeagueEntry& entry)
{
  if (entry.lower_divisions > 0) expanded_countries.insert(entry.root);
  if (selected_league_id == entry.id) return;
  selected_league_id = entry.id;
  selected_team_id.reset();
  loadAvailableTeams();
}

void TeamSelectionScene::renderLeagueList(float width, float height)
{
  const Theme::Palette& palette = Theme::palette();
  // Height 0: the card takes the height of its rows (stacked layout).
  if (height > 0.0f)
    UI::beginCard("##leagues", LOC("TEAM_SELECTION_LEAGUES_CAPTION"),
                  ImVec2(width, height), true);
  else
    UI::beginAutoHeightCard("##leagues", LOC("TEAM_SELECTION_LEAGUES_CAPTION"),
                            width);
  // One row per country (its top division); the arrow lists the lower
  // divisions underneath, indented. Compact rows: every country fits.
  const float scale = Theme::scale();
  const float rowHeight = ImGui::GetTextLineHeight() + 10.0f * scale;
  const float arrowSize = ImGui::GetTextLineHeight() * 0.4f;
  const float nameStart = rowHeight + Theme::Space::XS * scale;
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 2.0f * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
  for (const LeagueEntry& entry : league_entries)
  {
    const bool expanded = expanded_countries.contains(entry.root);
    if (entry.tier > 1 && !expanded) continue;
    ImGui::PushID(static_cast<int>(entry.id));
    const float rowStart = ImGui::GetCursorPosX();
    if (entry.lower_divisions > 0)
    {
      if (ImGui::InvisibleButton("##expand", ImVec2(rowHeight, rowHeight)))
      {
        if (expanded)
          expanded_countries.erase(entry.root);
        else
          expanded_countries.insert(entry.root);
      }
      const ImVec2 min = ImGui::GetItemRectMin();
      const ImVec2 center(min.x + rowHeight * 0.5f, min.y + rowHeight * 0.5f);
      ImDrawList* drawList = ImGui::GetWindowDrawList();
      if (ImGui::IsItemHovered())
        drawList->AddRectFilled(min, ImGui::GetItemRectMax(),
                                Theme::toU32(palette.raised),
                                Theme::Space::XS * scale);
      const ImU32 arrow = Theme::toU32(palette.muted);
      if (expanded)
        drawList->AddTriangleFilled(
            ImVec2(center.x - arrowSize, center.y - arrowSize * 0.5f),
            ImVec2(center.x + arrowSize, center.y - arrowSize * 0.5f),
            ImVec2(center.x, center.y + arrowSize * 0.7f), arrow);
      else
        drawList->AddTriangleFilled(
            ImVec2(center.x - arrowSize * 0.5f, center.y - arrowSize),
            ImVec2(center.x - arrowSize * 0.5f, center.y + arrowSize),
            ImVec2(center.x + arrowSize * 0.7f, center.y), arrow);
      ImGui::SameLine(0.0f, 0.0f);
    }
    const float indent = nameStart + static_cast<float>(entry.tier - 1) *
                                         Theme::Space::L * scale;
    ImGui::SetCursorPosX(rowStart + indent);
    ImGui::PushStyleColor(ImGuiCol_Text,
                          entry.tier == 1 ? palette.text : palette.muted);
    if (ImGui::Selectable(entry.name.c_str(), selected_league_id == entry.id, 0,
                          ImVec2(0.0f, rowHeight)))
      selectLeague(entry);
    ImGui::PopStyleColor();
    ImGui::PopID();
  }
  ImGui::PopStyleVar(2);
  UI::endCard();
}

void TeamSelectionScene::renderClubTable(float height)
{
  const Theme::Palette& palette = Theme::palette();
  // Height 0: every club row, no inner scrolling (stacked layout).
  const bool fill = height > 0.0f;
  if (fill)
    UI::beginCard("##clubs", LOC("TEAM_SELECTION_CLUBS_CAPTION"),
                  ImVec2(0.0f, std::max(height, 200.0f * Theme::scale())));
  else
    UI::beginAutoHeightCard("##clubs", LOC("TEAM_SELECTION_CLUBS_CAPTION"),
                            0.0f);
  const ImGuiTableFlags flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
      ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit |
      (fill ? ImGuiTableFlags_ScrollY : ImGuiTableFlags_None);
  // Narrow tables drop stadium and balance (both are on the club card),
  // then the expectation: columns hide instead of scrolling sideways.
  const float tableWidth = ImGui::GetContentRegionAvail().x;
  const bool compact = tableWidth < 900.0f * Theme::scale();
  const bool showExpectation = tableWidth >= 600.0f * Theme::scale();
  if (const int columnCount = (compact ? 4 : 6) + (showExpectation ? 1 : 0);
      ImGui::BeginTable("TeamsTable", columnCount, flags))
  {
    if (fill) ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(LOC("MAIN_GAME_TEAM"),
                            ImGuiTableColumnFlags_WidthStretch, 0.0f,
                            std::to_underlying(ClubColumn::NAME));
    // Explicit widths: the table is laid out correctly from its first frame.
    const auto column = [](const char* label, ImGuiTableColumnFlags extra,
                           float minimum, ClubColumn id)
    {
      ImGui::TableSetupColumn(
          label, ImGuiTableColumnFlags_WidthFixed | extra,
          std::max(ImGui::CalcTextSize(label).x + 20.0f * Theme::scale(),
                   minimum * Theme::scale()),
          std::to_underlying(id));
    };
    column(LOC("TEAM_SELECTION_REPUTATION"),
           ImGuiTableColumnFlags_DefaultSort |
               ImGuiTableColumnFlags_PreferSortDescending,
           ImGui::GetTextLineHeight() * 5.0f / Theme::scale() + 14.0f,
           ClubColumn::REPUTATION);
    if (!compact)
      column(LOC("TEAM_SELECTION_STADIUM"),
             ImGuiTableColumnFlags_PreferSortDescending, 70.0f,
             ClubColumn::STADIUM);
    column(LOC("TEAM_SELECTION_AVERAGE_SHORT"),
           ImGuiTableColumnFlags_PreferSortDescending, 50.0f,
           ClubColumn::AVERAGE);
    float objectiveWidth = 0.0f;
    for (const ClubSummary& club : club_summaries)
      objectiveWidth = std::max(objectiveWidth,
                                ImGui::CalcTextSize(LOC(club.objective_key)).x);
    if (showExpectation)
      column(LOC("TEAM_SELECTION_EXPECTATION"), 0,
             objectiveWidth / Theme::scale(), ClubColumn::EXPECTATION);
    column(LOC("TEAM_SELECTION_DIFFICULTY"), 0, 90.0f, ClubColumn::DIFFICULTY);
    if (!compact)
      column(LOC("TEAM_SELECTION_BALANCE"),
             ImGuiTableColumnFlags_PreferSortDescending, 70.0f,
             ClubColumn::BALANCE);
    ImGui::TableHeadersRow();
    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
        specs != nullptr && specs->SpecsCount > 0)
    {
      const ImGuiTableColumnSortSpecs& spec = specs->Specs[0];
      if (const bool ascending =
              spec.SortDirection == ImGuiSortDirection_Ascending;
          specs->SpecsDirty || spec.ColumnUserID != sort_column ||
          ascending != sort_ascending)
      {
        sort_column = spec.ColumnUserID;
        sort_ascending = ascending;
        sortClubs();
      }
      specs->SpecsDirty = false;
    }

    for (const ClubSummary& club : club_summaries)
    {
      ImGui::TableNextRow(ImGuiTableRowFlags_None,
                          ImGui::GetTextLineHeight() + 8.0f * Theme::scale());
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(club.id));
      // Colour swatch drawn in front of a real, named selectable so the row
      // is findable by label (automation, screen readers of the item tree).
      const float rowStart = ImGui::GetCursorPosX();
      UI::clubBadge(nullptr, club.primary, club.secondary, 18.0f);
      ImGui::SameLine();
      ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rowStart));
      if (ImGui::Selectable(club.name.c_str(), selected_team_id == club.id,
                            ImGuiSelectableFlags_SpanAllColumns |
                                ImGuiSelectableFlags_AllowDoubleClick))
      {
        selected_team_id = club.id;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
          startCareer(club.id);
      }
      ImGui::PopID();
      ImGui::TableNextColumn();
      reputationStars(club.reputation);
      if (!compact)
      {
        ImGui::TableNextColumn();
        UI::textRight(club.stadium_text.c_str());
      }
      ImGui::TableNextColumn();
      UI::ratingChip(club.average_overall);
      if (showExpectation)
      {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(LOC(club.objective_key));
      }
      ImGui::TableNextColumn();
      ImGui::TextColored(
          difficultyColor(club.difficulty), "%s",
          LOC(DIFFICULTY_KEYS[static_cast<size_t>(club.difficulty)]));
      if (!compact)
      {
        ImGui::TableNextColumn();
        UI::textRightColored(club.balance < 0 ? palette.negative : palette.text,
                             club.balance_text.c_str());
      }
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

const TeamSelectionScene::ClubSummary* TeamSelectionScene::selectedClub() const
{
  const auto selected =
      std::ranges::find_if(club_summaries, [this](const ClubSummary& club)
                           { return selected_team_id == club.id; });
  return selected == club_summaries.end() ? nullptr : std::to_address(selected);
}

void TeamSelectionScene::renderClubIdentity(const ClubSummary& club,
                                            float badgeHeight, float width)
{
  const Theme::Palette& palette = Theme::palette();
  const float start = ImGui::GetCursorScreenPos().x;
  UI::clubBadge(club.code.c_str(), club.primary, club.secondary, badgeHeight);
  ImGui::SameLine();
  const float textWidth =
      std::max(0.0f, start + width - ImGui::GetCursorScreenPos().x);
  ImGui::BeginGroup();
  {
    Theme::ScopedText title(Theme::Text::HEADING);
    UI::textFitted(club.name, textWidth, palette.text);
  }
  if (!club.nickname.empty())
    UI::textFitted(club.nickname, textWidth, palette.muted);
  reputationStars(club.reputation);
  ImGui::EndGroup();
}

void TeamSelectionScene::renderSelectedClub(float width, float height)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginCard("##selected_club", nullptr, ImVec2(width, height), true);
  const ClubSummary* selected = selectedClub();
  if (selected == nullptr)
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.muted, "%s", LOC("TEAM_SELECTION_CHOOSE_CLUB"));
    ImGui::PopTextWrapPos();
    UI::endCard();
    return;
  }
  const ClubSummary& club = *selected;
  renderClubIdentity(club, 48.0f, ImGui::GetContentRegionAvail().x);
  // The primary action comes right after the club's name: it stays visible
  // however short the card is.
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  if (UI::primaryButton(LOC("TEAM_SELECTION_CONFIRM"), ImVec2(-FLT_MIN, 0.0f)))
    startCareer(club.id);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));

  const float keyWidth = 150.0f * Theme::scale();
  UI::keyValue(LOC("TEAM_SELECTION_EXPECTATION"), LOC(club.objective_key),
               keyWidth);
  const std::string difficulty =
      LOC(DIFFICULTY_KEYS[static_cast<size_t>(club.difficulty)]);
  ImGui::PushStyleColor(ImGuiCol_Text, difficultyColor(club.difficulty));
  UI::keyValue(LOC("TEAM_SELECTION_DIFFICULTY"), difficulty.c_str(), keyWidth);
  ImGui::PopStyleColor();
  UI::keyValue(LOC("TEAM_SELECTION_STADIUM"), club.stadium_text.c_str(),
               keyWidth);
  if (!club.founded.empty())
    UI::keyValue(LOC("CLUB_FOUNDED"), club.founded.c_str(), keyWidth);
  UI::keyValue(LOC("TEAM_SELECTION_BALANCE"), club.balance_text.c_str(),
               keyWidth);
  UI::keyValue(LOC("TEAM_SELECTION_WAGES"), club.wages_text.c_str(), keyWidth);
  const std::string squad = std::to_string(club.squad_size);
  UI::keyValue(LOC("ROSTER_SUMMARY_SQUAD"), squad.c_str(), keyWidth);

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));
  UI::sectionLabel(LOC("TEAM_SELECTION_STRENGTH"));
  const float labelWidth = 120.0f * Theme::scale();
  UI::attributeBar(LOC("ROSTER_SUMMARY_AVG_OVR"), club.average_overall,
                   labelWidth);
  UI::attributeBar(LOC("TEAM_SELECTION_BEST_PLAYER"), club.best_overall,
                   labelWidth);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));
  UI::sectionLabel(LOC("DASHBOARD_KEY_PLAYERS"));
  for (const auto& [name, overall] : club.key_players)
  {
    UI::ratingChip(overall);
    ImGui::SameLine();
    UI::textFitted(name, ImGui::GetContentRegionAvail().x, palette.text);
  }
  UI::endCard();
}

void TeamSelectionScene::renderSelectedClubStrip()
{
  const ClubSummary* selected = selectedClub();
  if (selected == nullptr) return;
  const ClubSummary& club = *selected;
  UI::beginAutoHeightCard("##selected_club_strip", nullptr, 0.0f);
  const char* confirm = LOC("TEAM_SELECTION_CONFIRM");
  const float button = UI::buttonWidth(confirm);
  const float identityWidth =
      std::max(ImGui::GetContentRegionAvail().x - button -
                   ImGui::GetStyle().ItemSpacing.x,
               ImGui::GetContentRegionAvail().x * 0.5f);
  ImGui::BeginGroup();
  renderClubIdentity(club, 40.0f, identityWidth);
  ImGui::EndGroup();
  if (UI::sameLineIfFits(button))
    ImGui::SetCursorPosX(
        ImGui::GetCursorPosX() +
        std::max(0.0f, ImGui::GetContentRegionAvail().x - button));
  if (UI::primaryButton(confirm)) startCareer(club.id);
  // The facts the compact table leaves out.
  const std::string facts = std::format(
      "{}: {}    {}: {}    {}: {}", LOC("TEAM_SELECTION_EXPECTATION"),
      LOC(club.objective_key), LOC("TEAM_SELECTION_BALANCE"), club.balance_text,
      LOC("TEAM_SELECTION_WAGES"), club.wages_text);
  UI::textFitted(facts, ImGui::GetContentRegionAvail().x,
                 Theme::palette().muted);
  UI::endCard();
}

void TeamSelectionScene::startCareer(TeamID teamId)
{
  guiView->getController().createManager(manager_panel.setup());
  guiView->getController().selectManagedTeam(teamId);
  // A new career opens on Home with the welcome tour.
  if (auto* hub = dynamic_cast<MainGameScene*>(guiView->getBaseScene()))
    hub->offerWelcome();
  guiView->popScene();
}

void TeamSelectionScene::backToMenu()
{
  // The new save stays in its slot as "not started": loading it brings the
  // player back here.
  changeScene(std::make_unique<MainMenuScene>(guiView));
}

void TeamSelectionScene::startUnemployed()
{
  guiView->getController().createManager(manager_panel.setup());
  guiView->popScene();
}

void TeamSelectionScene::loadAvailableLeagues()
{
  const auto& controller = guiView->getController();
  league_entries.clear();
  const auto data = controller.getGameData();
  if (!data) return;
  // Grouped by country, top division first.
  for (const LeagueID root : Competitions::countryRoots(*data))
  {
    const size_t top = league_entries.size();
    for (const LeagueID id : Competitions::countryLeagues(*data, root))
    {
      const auto league = controller.getLeagueById(id);
      if (!league) continue;
      league_entries.push_back({.id = id,
                                .root = root,
                                .name = Competitions::leagueName(league->get()),
                                .tier = controller.getLeagueTier(id)});
    }
    if (league_entries.size() > top)
      league_entries[top].lower_divisions = league_entries.size() - top - 1;
  }
}

void TeamSelectionScene::loadAvailableTeams()
{
  if (!selected_league_id.has_value()) return;
  const auto& controller = guiView->getController();
  const auto& statsConfig = controller.getStatsConfig();
  available_teams.clear();
  for (const auto& teamRef : controller.getTeams())
  {
    const Team& team = teamRef.get();
    if (team.getLeagueId() == selected_league_id.value() &&
        team.getId() != FREE_AGENTS_TEAM_ID)
      available_teams.push_back(teamRef);
  }

  club_summaries.clear();
  club_summaries.reserve(available_teams.size());
  for (const auto& teamRef : available_teams)
  {
    const Team& team = teamRef.get();
    ClubSummary summary;
    std::string venue;
    summary.id = team.getId();
    summary.name = team.getName();
    summary.reputation = team.getReputation();
    summary.stadium = team.getStadiumCapacity();
    summary.balance = team.getFinances().getBalance();
    if (const ClubIdentity* identity = controller.getClubIdentity(team.getId()))
    {
      summary.code = identity->short_name;
      summary.primary = identity->primary_colour;
      summary.secondary = identity->secondary_colour;
      summary.nickname = identity->nickname;
      if (identity->founded > 0)
        summary.founded = std::to_string(identity->founded);
      venue = identity->stadium_name;
    }
    else
    {
      const KitColors kit = chooseMatchKits(team.getId(), 0).home;
      summary.primary = toRgb(kit.shirt);
      summary.secondary = toRgb(kit.trim);
    }
    std::vector<std::pair<std::string, float>> players;
    double total = 0.0;
    for (const auto& playerRef : controller.getPlayersForTeam(team.getId()))
    {
      const Player& player = playerRef.get();
      const auto overall = static_cast<float>(player.getOverall(statsConfig));
      total += static_cast<double>(overall);
      summary.best_overall = std::max(summary.best_overall, overall);
      summary.weekly_wages += player.getWage();
      players.emplace_back(player.getName(), overall);
      ++summary.squad_size;
    }
    summary.average_overall =
        summary.squad_size > 0
            ? static_cast<float>(total /
                                 static_cast<double>(summary.squad_size))
            : 0.0f;
    const size_t keyCount = std::min(KEY_PLAYERS, players.size());
    std::ranges::partial_sort(
        players, players.begin() + static_cast<std::ptrdiff_t>(keyCount),
        [](const auto& left, const auto& right)
        { return left.second > right.second; });
    players.resize(keyCount);
    summary.key_players = std::move(players);
    summary.balance_text = Format::money(summary.balance);
    summary.wages_text = Format::money(summary.weekly_wages);
    summary.stadium_text =
        venue.empty()
            ? Format::thousands(summary.stadium)
            : std::format("{} ({})", venue, Format::thousands(summary.stadium));
    club_summaries.push_back(std::move(summary));
  }

  // Board expectation follows the wage-bill rank (as the board model does);
  // difficulty compares squad strength with the rest of the league.
  const auto size = static_cast<int>(club_summaries.size());
  std::vector<size_t> order(club_summaries.size());
  for (size_t index = 0; index < order.size(); ++index) order[index] = index;
  std::ranges::sort(order,
                    [this](size_t left, size_t right)
                    {
                      return club_summaries[left].weekly_wages >
                             club_summaries[right].weekly_wages;
                    });
  for (size_t rank = 0; rank < order.size(); ++rank)
  {
    ClubSummary& club = club_summaries[order[rank]];
    club.expected_position = static_cast<int>(rank) + 1;
    club.objective_key = BoardModel::objectiveKey(
        BoardModel::objectiveFor(club.expected_position, size));
  }
  std::ranges::sort(order,
                    [this](size_t left, size_t right)
                    {
                      return club_summaries[left].average_overall >
                             club_summaries[right].average_overall;
                    });
  for (size_t rank = 0; rank < order.size(); ++rank)
  {
    const float percentile =
        size > 1 ? static_cast<float>(rank) / static_cast<float>(size - 1)
                 : 0.0f;
    int difficulty = 3;
    if (percentile < 0.25f)
      difficulty = 0;
    else if (percentile < 0.6f)
      difficulty = 1;
    else if (percentile < 0.85f)
      difficulty = 2;
    club_summaries[order[rank]].difficulty = difficulty;
  }

  if (sort_column == 0)
  {
    sort_column = std::to_underlying(ClubColumn::REPUTATION);
    sort_ascending = false;
  }
  sortClubs();
  if (!club_summaries.empty() && !selected_team_id.has_value())
    selected_team_id = club_summaries.front().id;
}

void TeamSelectionScene::sortClubs()
{
  using enum ClubColumn;
  const auto column = static_cast<ClubColumn>(sort_column);
  std::ranges::stable_sort(
      club_summaries,
      [this, column](const ClubSummary& a, const ClubSummary& b)
      {
        int comparison = 0;
        switch (column)
        {
          case NAME:
            comparison = a.name.compare(b.name);
            break;
          case REPUTATION:
            comparison = UI::compare(a.reputation, b.reputation);
            break;
          case STADIUM:
            comparison = UI::compare(a.stadium, b.stadium);
            break;
          case AVERAGE:
            comparison = UI::compare(a.average_overall, b.average_overall);
            break;
          case EXPECTATION:
            comparison = UI::compare(a.expected_position, b.expected_position);
            break;
          case DIFFICULTY:
            comparison = UI::compare(a.difficulty, b.difficulty);
            break;
          case BALANCE:
            comparison = UI::compare(a.balance, b.balance);
            break;
        }
        if (comparison == 0) comparison = a.name.compare(b.name);
        return sort_ascending ? comparison < 0 : comparison > 0;
      });
}

SceneID TeamSelectionScene::getID() const { return SceneID::TEAM_SELECTION; }
