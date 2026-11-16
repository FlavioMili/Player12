// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/scouting_scene.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/global.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/scenes/scouting_scene_internal.h"
#include "gui/view_models/player_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/competition.h"
#include "model/role_utils.h"

namespace
{
constexpr float FILTER_LABEL_WIDTH = 240.0f;
constexpr size_t SEARCH_LIMIT = 250;
constexpr int ROLE_COUNT =
    static_cast<int>(std::to_underlying(PlayerRole::UNKNOWN));
/** Saved column view shared by the search and shortlist tables. */
constexpr const char* PLAYER_TABLE_KEY = "scouting";

// Player tables (search and shortlist); unscaled widths, the highest
// priority numbers hide first. Sort ids follow ScoutingScene::SearchColumn.
const std::array<UI::Column, 9>& playerColumns()
{
  constexpr ImGuiTableColumnFlags DESCENDING =
      ImGuiTableColumnFlags_PreferSortDescending;
  static const std::array<UI::Column, 9> columns = {{
      {"SCOUTING_COL_PLAYER", 0.0f, 0, ImGuiTableColumnFlags_None, 0},
      {"SCOUTING_COL_CLUB", 150.0f, 3, ImGuiTableColumnFlags_None, 1},
      {"SCOUTING_COL_ROLE", 56.0f, 2, ImGuiTableColumnFlags_None, 2},
      {"SCOUTING_COL_AGE", 44.0f, 4, ImGuiTableColumnFlags_None, 3},
      {"SCOUTING_COL_ABILITY", 64.0f, 0,
       DESCENDING | ImGuiTableColumnFlags_DefaultSort, 4},
      {"SCOUTING_COL_POTENTIAL", 76.0f, 1, DESCENDING, 5},
      {"SCOUTING_COL_KNOWLEDGE", 84.0f, 5, DESCENDING, 6},
      {"SCOUTING_COL_VALUE", 88.0f, 3, DESCENDING, 7},
      {"SCOUTING_COL_STATUS", 170.0f, 6, ImGuiTableColumnFlags_NoSort, 8},
  }};
  return columns;
}

/** Column picker at the right end of the current line (or the next). */
void playerColumnPicker(UI::ColumnMask fitted)
{
  if (const float width =
          UI::buttonWidth(LOC("TABLE_COLUMNS"), UI::ButtonSize::COMPACT);
      UI::sameLineIfFits(width))
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         ImGui::GetContentRegionAvail().x - width);
  UI::columnPicker(PLAYER_TABLE_KEY, playerColumns(), fitted);
}
}  // namespace

namespace ScoutingUi
{
namespace
{
constexpr float KNOWLEDGE_BAR_WIDTH = 64.0f;
}  // namespace

float dpi() { return ImGui::GetStyle().FontScaleDpi; }

std::string lower(std::string text)
{
  std::ranges::transform(text, text.begin(), [](unsigned char c)
                         { return static_cast<char>(std::tolower(c)); });
  return text;
}

std::string rangeText(float low, float high)
{
  return std::format("{:.0f}–{:.0f}", static_cast<double>(low),
                     static_cast<double>(high));
}

ImVec4 gradeColor(ScoutGrade grade)
{
  using enum ScoutGrade;
  const Theme::Palette& palette = Theme::palette();
  switch (grade)
  {
    case A:
      return palette.positive;
    case B:
      return palette.warning;
    case C:
      break;
  }
  return palette.muted;
}

const char* gradeLabel(ScoutGrade grade)
{
  using enum ScoutGrade;
  switch (grade)
  {
    case A:
      return "A";
    case B:
      return "B";
    case C:
      break;
  }
  return "C";
}

/** Thin bar showing how well the club knows a player. */
void knowledgeBar(uint8_t knowledge)
{
  const Theme::Palette& palette = Theme::palette();
  const float width = KNOWLEDGE_BAR_WIDTH * dpi();
  const float height = 6.0f * dpi();
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const float offset = (ImGui::GetTextLineHeight() - height) * 0.5f;
  const ImVec2 top(start.x, start.y + offset);
  ImVec4 color;
  if (knowledge < 35)
    color = palette.negative;
  else if (knowledge < 65)
    color = palette.warning;
  else
    color = palette.positive;
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(top, ImVec2(top.x + width, top.y + height),
                          Theme::toU32(palette.raised), height * 0.5f);
  drawList->AddRectFilled(
      top,
      ImVec2(top.x + width * static_cast<float>(knowledge) / 100.0f,
             top.y + height),
      Theme::toU32(color), height * 0.5f);
  ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "%s", fmt::sprintf(LOC("SCOUTING_KNOWLEDGE_HINT"), knowledge).c_str());
}

}  // namespace ScoutingUi

using namespace ScoutingUi;

namespace
{
std::string roleLabel(int index)
{
  return index == 0 ? std::string(LOC("SCOUTING_ANY"))
                    : RoleUtils::shortName(static_cast<PlayerRole>(index - 1));
}

bool roleCombo(const char* id, int* index)
{
  bool changed = false;
  if (ImGui::BeginCombo(id, roleLabel(*index).c_str()))
  {
    for (int option = 0; option <= ROLE_COUNT; ++option)
    {
      if (ImGui::Selectable(roleLabel(option).c_str(), option == *index))
      {
        *index = option;
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

constexpr float NAME_SEARCH_DELAY_SECONDS = 0.35f;

void labelled(const char* label)
{
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::SameLine(FILTER_LABEL_WIDTH * dpi());
}
}  // namespace

ScoutingScene::ScoutingScene(GUIView* parent, Tab initialTab)
    : ManagementScene(parent), requested_tab(initialTab)
{
}

void ScoutingScene::update(float /*deltaTime*/)
{
  // Scouting has no frame-by-frame state; refresh() and user actions rebuild
  // its cached rows when data changes.
}

void ScoutingScene::refresh()
{
  const GameController& controller = guiView->getController();
  scouts = controller.getScouts();
  leagues.clear();
  for (const League& league : controller.getLeagues())
    leagues.emplace_back(league.getId(), Competitions::leagueName(league));
  std::ranges::sort(leagues, {}, &std::pair<LeagueID, std::string>::second);
  rebuildWorld();
  rebuildReports();
  rebuildShortlist();
  rebuildScouts();
  search_dirty = true;
}

void ScoutingScene::rebuildReports()
{
  const GameController& controller = guiView->getController();
  reports.clear();
  const auto& source = controller.getScoutReports();
  reports.reserve(source.size());
  for (auto it = source.rbegin(); it != source.rend(); ++it)
    reports.push_back(makeReportLine(*it));
}

ScoutingScene::ReportLine ScoutingScene::makeReportLine(
    const ScoutReport& report) const
{
  const GameController& controller = guiView->getController();
  ReportLine line{
      report,
      {},
      {},
      Format::date(report.date),
      rangeText(report.potential_low, report.potential_high),
      Format::money(report.estimated_fee),
      rangeText(report.overall_low, report.overall_high),
      std::ranges::find(fresh_reports, report.id) != fresh_reports.end()};
  if (const auto player = controller.getGameData()->getPlayer(report.player_id))
  {
    line.player_name = player->get().getName();
    const TeamID teamId = player->get().getTeamId();
    const auto team = controller.getTeamById(teamId);
    line.club = teamId == FREE_AGENTS_TEAM_ID || !team
                    ? std::string(LOC("TRANSFER_FREE_AGENT_LABEL"))
                    : team->get().getName();
  }
  return line;
}

ScoutingScene::PlayerLine ScoutingScene::makeLine(
    const ScoutedPlayerRow& row) const
{
  const GameController& controller = guiView->getController();
  PlayerLine line;
  line.row = row;
  if (const auto player = controller.getGameData()->getPlayer(row.player_id))
    line.name = player->get().getName();
  line.name_lower = lower(line.name);
  const auto team = controller.getTeamById(row.team_id);
  line.club = row.team_id == FREE_AGENTS_TEAM_ID || !team
                  ? std::string(LOC("TRANSFER_FREE_AGENT_LABEL"))
                  : team->get().getName();
  line.role = RoleUtils::shortName(row.role);
  if (const auto view = controller.getScoutedView(row.player_id))
    line.ability_range = rangeText(view->overall_low, view->overall_high);
  line.potential_text = rangeText(row.potential_low, row.potential_high);
  line.value_text = row.team_id == FREE_AGENTS_TEAM_ID
                        ? std::string(LOC("TRANSFER_FREE_AGENT_LABEL"))
                        : Format::money(row.estimated_value);
  return line;
}

void ScoutingScene::rebuildSearch()
{
  search_dirty = false;
  filter.role = filter_role == 0 ? PlayerRole::UNKNOWN
                                 : static_cast<PlayerRole>(filter_role - 1);
  filter.free_agents_only = filter_league == 1;
  filter.league_id =
      filter_league >= 2 &&
              static_cast<size_t>(filter_league - 2) < leagues.size()
          ? leagues[static_cast<size_t>(filter_league - 2)].first
          : 0;
  filter.min_age = static_cast<uint8_t>(filter_min_age);
  filter.max_age = static_cast<uint8_t>(filter_max_age);
  filter.min_overall = static_cast<float>(filter_min_overall);
  filter.max_value = static_cast<int64_t>(filter_max_value_m) * 1'000'000;
  filter.min_knowledge = static_cast<uint8_t>(filter_min_knowledge);
  const std::string query = lower(name_query.data());
  filter.limit = query.empty() ? SEARCH_LIMIT : 0;

  search_rows.clear();
  for (const ScoutedPlayerRow& row :
       guiView->getController().searchScoutedPlayers(filter))
  {
    if (!query.empty())
    {
      const auto player =
          guiView->getController().getGameData()->getPlayer(row.player_id);
      if (!player || !lower(player->get().getName()).contains(query)) continue;
    }
    search_rows.push_back(makeLine(row));
    if (search_rows.size() >= SEARCH_LIMIT) break;
  }
  sortSearch();
}

void ScoutingScene::sortSearch()
{
  const auto key = [this](const PlayerLine& a, const PlayerLine& b)
  {
    using enum SearchColumn;
    switch (sort_column)
    {
      case NAME:
        return UI::compare(a.name_lower, b.name_lower);
      case CLUB:
        return UI::compare(a.club, b.club);
      case ROLE:
        return UI::compare(a.row.role, b.row.role);
      case AGE:
        return UI::compare(a.row.age, b.row.age);
      case ABILITY:
        return UI::compare(a.row.overall, b.row.overall);
      case POTENTIAL:
        return UI::compare(a.row.potential_high, b.row.potential_high);
      case KNOWLEDGE:
        return UI::compare(a.row.knowledge, b.row.knowledge);
      case VALUE:
        return UI::compare(a.row.estimated_value, b.row.estimated_value);
    }
    return 0;
  };
  std::ranges::stable_sort(search_rows,
                           [&](const PlayerLine& a, const PlayerLine& b)
                           {
                             const int order = key(a, b);
                             return sort_ascending ? order < 0 : order > 0;
                           });
}

void ScoutingScene::rebuildShortlist()
{
  const GameController& controller = guiView->getController();
  shortlist_rows.clear();
  for (const ShortlistEntry& entry : controller.getShortlist())
  {
    if (const auto row = controller.getScoutedRow(entry.player_id))
      shortlist_rows.push_back(makeLine(*row));
  }
  std::ranges::sort(shortlist_rows, [](const PlayerLine& a, const PlayerLine& b)
                    { return a.row.overall > b.row.overall; });

  comparison.clear();
  for (const SquadComparisonRow& row : controller.compareShortlistWithSquad())
  {
    ComparisonLine line{row, RoleUtils::shortName(row.role), {}, {}, {}};
    const auto name = [&](PlayerID id)
    {
      const auto player = controller.getGameData()->getPlayer(id);
      return player ? player->get().getName() : std::string();
    };
    if (row.own_best_id != 0) line.own_name = name(row.own_best_id);
    if (row.candidate_id != 0)
    {
      line.candidate_name = name(row.candidate_id);
      line.potential_text =
          rangeText(row.candidate_potential_low, row.candidate_potential_high);
    }
    comparison.push_back(std::move(line));
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void ScoutingScene::renderContent()
{
  if (GameController& controller = guiView->getController();
      !controller.getManagedTeam())
  {
    UI::emptyState(LOC("DASHBOARD_CHOOSE_CLUB"), nullptr);
    return;
  }
  if (search_dirty) rebuildSearch();
  const std::string subtitle = fmt::sprintf(
      LOC("SCOUTING_SUBTITLE"), static_cast<int>(scouts.size()),
      static_cast<int>(std::ranges::count_if(
          scout_lines, [](const ScoutLine& line)
          { return line.summary.status == ScoutStatus::OnAssignment; })),
      static_cast<int>(reports.size()),
      static_cast<int>(shortlist_rows.size()));
  UI::pageHeader(LOC("SCOUTING_TITLE"), subtitle.c_str());

  if (ImGui::BeginTabBar("ScoutingTabs"))
  {
    const auto tab = [this](Tab which, const char* labelKey, auto&& body)
    {
      const ImGuiTabItemFlags flags = tab_pending && requested_tab == which
                                          ? ImGuiTabItemFlags_SetSelected
                                          : ImGuiTabItemFlags_None;
      if (ImGui::BeginTabItem(LOC(labelKey), nullptr, flags))
      {
        body();
        ImGui::EndTabItem();
      }
    };
    tab(Tab::OVERVIEW, "SCOUTING_TAB_OVERVIEW", [this] { renderOverview(); });
    tab(Tab::REPORTS, "SCOUTING_TAB_REPORTS", [this] { renderReports(); });
    tab(Tab::SEARCH, "SCOUTING_TAB_SEARCH", [this] { renderSearch(); });
    tab(Tab::SHORTLIST, "SCOUTING_TAB_SHORTLIST",
        [this] { renderShortlist(); });
    tab(Tab::FOCUS, "SCOUTING_TAB_FOCUS", [this] { renderFocus(); });
    tab_pending = false;
    ImGui::EndTabBar();
  }
}

bool ScoutingScene::playerCell(PlayerID playerId, const std::string& name,
                               bool selected)
{
  ImGui::PushID(static_cast<int>(playerId));
  const bool clicked =
      ImGui::Selectable(name.c_str(), selected,
                        ImGuiSelectableFlags_SpanAllColumns |
                            ImGuiSelectableFlags_AllowDoubleClick |
                            ImGuiSelectableFlags_AllowOverlap);
  if (clicked)
  {
    selectPlayer(playerId, name);
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
      Navigation::openPlayer(guiView, playerId);
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    ImGui::SetTooltip("%s", LOC("SCOUTING_OPEN_PROFILE_HINT"));
  ImGui::PopID();
  return clicked;
}

void ScoutingScene::selectPlayer(PlayerID playerId, std::string_view name)
{
  selected_player = playerId;
  selected_name = name;
}

void ScoutingScene::renderPlayerActions()
{
  const Theme::Palette& palette = Theme::palette();
  GameController& controller = guiView->getController();
  if (selected_player == 0 ||
      !controller.getGameData()->getPlayer(selected_player))
  {
    ImGui::TextColored(palette.muted, "%s", LOC("SCOUTING_ACTIONS_NONE"));
    return;
  }
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(
      fmt::sprintf(LOC("SCOUTING_SELECTED"), selected_name.c_str()).c_str());
  ImGui::SameLine();
  if (ImGui::Button(LOC("SCOUTING_OPEN_PROFILE")))
    Navigation::openPlayer(guiView, selected_player);
  ImGui::SameLine();
  if (controller.isShortlisted(selected_player))
  {
    if (ImGui::Button(LOC("SCOUTING_REMOVE_SHORTLIST")) &&
        controller.removeFromShortlist(selected_player))
    {
      showToast(LOC("SCOUTING_SHORTLIST_REMOVED"));
      rebuildShortlist();
      search_dirty = true;
    }
  }
  else if (ImGui::Button(LOC("SCOUTING_ADD_SHORTLIST")))
  {
    if (controller.addToShortlist(selected_player))
    {
      showToast(LOC("SCOUTING_SHORTLIST_ADDED"));
      rebuildShortlist();
      search_dirty = true;
    }
    else
    {
      showToast(LOC("SCOUTING_SHORTLIST_REFUSED"), true);
    }
  }
  ImGui::SameLine();
  const auto days = static_cast<uint16_t>(send_days);
  const int64_t cost = controller.getScoutAssignmentCost(
      ScoutTargetKind::Player, selected_player, days);
  const std::string label = fmt::sprintf(LOC("SCOUTING_SCOUT_PLAYER"), days,
                                         Format::money(cost).c_str());
  ImGui::BeginDisabled(cost <= 0);
  if (ImGui::Button(label.c_str()))
    sendScout(ScoutTargetKind::Player, selected_player);
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("%s", LOC("SCOUTING_SCOUT_PLAYER_HINT"));
}

void ScoutingScene::renderReports()
{
  const Theme::Palette& palette = Theme::palette();
  renderPlayerActions();
  if (reports.empty())
  {
    UI::emptyState(LOC("SCOUTING_REPORTS_EMPTY_TITLE"),
                   LOC("SCOUTING_REPORTS_EMPTY_BODY"));
    return;
  }
  if (!UI::beginDataTable(
          "ScoutReports", 9,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
          900.0f, ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
    return;
  ImGui::TableSetupColumn(LOC("SCOUTING_COL_PLAYER"),
                          ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn(LOC("SCOUTING_COL_CLUB"));
  ImGui::TableSetupColumn(LOC("SCOUTING_COL_DATE"));
  ImGui::TableSetupColumn(LOC("SCOUTING_COL_SCOUT"));
  ImGui::TableSetupColumn(LOC("SCOUTING_COL_GRADE"));
  ImGui::TableSetupColumn(LOC("SCOUTING_COL_ABILITY"));
  ImGui::TableSetupColumn(LOC("SCOUTING_COL_POTENTIAL"));
  ImGui::TableSetupColumn(LOC("SCOUTING_COL_FEE"));
  ImGui::TableSetupColumn(LOC("SCOUTING_COL_REASONS"));
  ImGui::TableHeadersRow();
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(reports.size()));
  while (clipper.Step())
  {
    for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
    {
      const ReportLine& line = reports[static_cast<size_t>(index)];
      const ScoutReport& report = line.report;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(report.id));
      playerCell(report.player_id, line.player_name,
                 selected_player == report.player_id);
      ImGui::PopID();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(line.club.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(line.date_text.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(report.scout_name.c_str());
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", fmt::sprintf(LOC("SCOUTING_CONFIDENCE_HINT"),
                                             report.confidence)
                                    .c_str());
      ImGui::TableNextColumn();
      UI::badge(gradeLabel(report.grade), gradeColor(report.grade));
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", LOC(scoutGradeKey(report.grade)));
      ImGui::TableNextColumn();
      UI::ratingChip(report.overall);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(line.potential_text.c_str());
      ImGui::TableNextColumn();
      UI::textRight(line.fee_text.c_str());
      ImGui::TableNextColumn();
      const auto reason = [&](bool ok, const char* key)
      {
        UI::badge(LOC(key), ok ? palette.positive : palette.faint);
        ImGui::SameLine();
      };
      reason(report.fits_need, "SCOUTING_REASON_FITS_SHORT");
      reason(report.affordable, "SCOUTING_REASON_AFFORDABLE_SHORT");
      reason(report.available, "SCOUTING_REASON_AVAILABLE_SHORT");
      ImGui::NewLine();
    }
  }
  ImGui::EndTable();
}

void ScoutingScene::renderSearchFilters()
{
  const float width = ImGui::GetContentRegionAvail().x;
  const float field =
      std::max(120.0f * dpi(), (width - 5.0f * Theme::Space::M * dpi()) / 6.0f);
  bool changed = false;
  ImGui::SetNextItemWidth(field);
  // The world search runs once the name has settled, not on every key.
  if (ImGui::InputTextWithHint("##name", LOC("SCOUTING_FILTER_NAME"),
                               name_query.data(), name_query.size()))
  {
    name_settle_seconds = NAME_SEARCH_DELAY_SECONDS;
  }
  else if (name_settle_seconds > 0.0f)
  {
    name_settle_seconds -= ImGui::GetIO().DeltaTime;
    if (name_settle_seconds <= 0.0f || ImGui::IsItemDeactivated())
    {
      name_settle_seconds = 0.0f;
      changed = true;
    }
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(field * 0.6f);
  changed |= roleCombo("##role", &filter_role);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(field);
  std::string leagueLabel;
  if (filter_league == 0)
    leagueLabel = LOC("SCOUTING_ALL_LEAGUES");
  else if (filter_league == 1)
    leagueLabel = LOC("SCOUTING_TARGET_FREE_AGENTS");
  else
    leagueLabel = leagues[static_cast<size_t>(filter_league - 2)].second;
  if (ImGui::BeginCombo("##league", leagueLabel.c_str()))
  {
    if (ImGui::Selectable(LOC("SCOUTING_ALL_LEAGUES"), filter_league == 0))
    {
      filter_league = 0;
      changed = true;
    }
    if (ImGui::Selectable(LOC("SCOUTING_TARGET_FREE_AGENTS"),
                          filter_league == 1))
    {
      filter_league = 1;
      changed = true;
    }
    for (size_t index = 0; index < leagues.size(); ++index)
    {
      if (ImGui::Selectable(leagues[index].second.c_str(),
                            filter_league == static_cast<int>(index) + 2))
      {
        filter_league = static_cast<int>(index) + 2;
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(field);
  // Sliders re-run the search once released, not on every dragged frame.
  const auto released = [] { return ImGui::IsItemDeactivatedAfterEdit(); };
  ImGui::DragIntRange2("##age", &filter_min_age, &filter_max_age, 0.2f, 15, 45,
                       LOC("SCOUTING_FILTER_AGE_MIN"),
                       LOC("SCOUTING_FILTER_AGE_MAX"));
  changed |= released();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(field);
  ImGui::SliderInt("##min_overall", &filter_min_overall, 0, 90,
                   LOC("SCOUTING_FILTER_MIN_ABILITY"));
  changed |= released();
  ImGui::SetNextItemWidth(field);
  ImGui::SliderInt("##max_value", &filter_max_value_m, 0, 150,
                   filter_max_value_m == 0 ? LOC("SCOUTING_FILTER_ANY_VALUE")
                                           : LOC("SCOUTING_FILTER_MAX_VALUE"));
  changed |= released();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(field);
  ImGui::SliderInt("##min_knowledge", &filter_min_knowledge, 0, 95,
                   LOC("SCOUTING_FILTER_MIN_KNOWLEDGE"));
  changed |= released();
  ImGui::SameLine();
  changed |=
      ImGui::Checkbox(LOC("SCOUTING_FILTER_FOCUS"), &filter.focus_matches_only);
  ImGui::SameLine();
  ImGui::TextDisabled("%s", fmt::sprintf(LOC("SCOUTING_RESULTS"),
                                         static_cast<int>(search_rows.size()))
                                .c_str());
  if (changed) search_dirty = true;
}

void ScoutingScene::renderSearch()
{
  renderSearchFilters();
  renderPlayerActions();
  playerColumnPicker(player_table_mask);
  if (search_rows.empty())
  {
    UI::emptyState(LOC("SCOUTING_SEARCH_EMPTY_TITLE"),
                   LOC("SCOUTING_SEARCH_EMPTY_BODY"));
    return;
  }
  renderPlayerTable("ScoutSearch", search_rows,
                    ImGui::GetContentRegionAvail().y, true);
}

void ScoutingScene::renderPlayerTable(const char* id,
                                      std::vector<PlayerLine>& lines,
                                      float height, bool sortable)
{
  const Theme::Palette& palette = Theme::palette();
  // Low-priority and user-hidden columns drop out instead of scrolling
  // sideways.
  std::array<UI::Column, 9> columns = playerColumns();
  const UI::ColumnMask hidden = UI::hiddenColumns(PLAYER_TABLE_KEY, columns);
  for (UI::Column& column : columns) column.label = LOC(column.label);
  const UI::ColumnMask mask =
      UI::fitColumns(columns, ImGui::GetContentRegionAvail().x, 160.0f, hidden);
  player_table_mask = mask;
  ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_BordersInnerH |
                          ImGuiTableFlags_ScrollY;
  if (sortable) flags |= ImGuiTableFlags_Sortable;
  if (!UI::beginResponsiveTable(
          id, columns, mask, flags,
          sortable ? UI::TableHeader::SORTABLE : UI::TableHeader::STATIC,
          ImVec2(0.0f, height)))
    return;
  // Follow the header every frame: the table keeps its sort across visits
  // and column changes while the scene starts from its own default.
  if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
      sortable && specs != nullptr && specs->SpecsCount > 0)
  {
    const auto column = static_cast<SearchColumn>(specs->Specs[0].ColumnUserID);
    if (const bool ascending =
            specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
        specs->SpecsDirty || column != sort_column ||
        ascending != sort_ascending)
    {
      sort_column = column;
      sort_ascending = ascending;
      sortSearch();
    }
    specs->SpecsDirty = false;
  }

  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(lines.size()));
  while (clipper.Step())
  {
    for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
    {
      const PlayerLine& line = lines[static_cast<size_t>(index)];
      const ScoutedPlayerRow& row = line.row;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      playerCell(row.player_id, line.name, selected_player == row.player_id);
      if (UI::cell(mask, 1)) ImGui::TextUnformatted(line.club.c_str());
      if (UI::cell(mask, 2)) ImGui::TextUnformatted(line.role.c_str());
      if (UI::cell(mask, 3)) ImGui::Text("%d", row.age);
      if (UI::cell(mask, 4))
      {
        UI::ratingChip(row.overall);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", fmt::sprintf(LOC("SCOUTING_ESTIMATE_HINT"),
                                               line.ability_range.c_str())
                                      .c_str());
      }
      if (UI::cell(mask, 5))
        ImGui::TextColored(
            Theme::ratingColor(
                static_cast<double>(row.potential_low + row.potential_high) *
                0.5),
            "%s", line.potential_text.c_str());
      if (UI::cell(mask, 6)) knowledgeBar(row.knowledge);
      if (UI::cell(mask, 7)) UI::textRight(line.value_text.c_str());
      if (!UI::cell(mask, 8)) continue;
      const auto badge = [](const char* key, const ImVec4& color)
      {
        UI::badge(LOC(key), color);
        ImGui::SameLine();
      };
      if (row.shortlisted) badge("SCOUTING_BADGE_SHORTLISTED", palette.accent);
      if (row.matches_focus) badge("SCOUTING_BADGE_FOCUS", palette.info);
      if (row.listed) badge("SCOUTING_BADGE_LISTED", palette.positive);
      if (row.contract_years <= 1 && row.team_id != FREE_AGENTS_TEAM_ID)
        badge("SCOUTING_BADGE_EXPIRING", palette.warning);
      if (row.injured) badge("SCOUTING_BADGE_INJURED", palette.negative);
      ImGui::NewLine();
    }
  }
  ImGui::EndTable();
}

void ScoutingScene::renderShortlist()
{
  renderPlayerActions();
  playerColumnPicker(player_table_mask);
  if (shortlist_rows.empty())
  {
    UI::emptyState(LOC("SCOUTING_SHORTLIST_EMPTY_TITLE"),
                   LOC("SCOUTING_SHORTLIST_EMPTY_BODY"));
    return;
  }
  const float height = ImGui::GetContentRegionAvail().y;
  renderPlayerTable("ScoutShortlist", shortlist_rows,
                    std::max(160.0f * dpi(), height * 0.45f), false);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::M * dpi()));
  renderComparison();
}

void ScoutingScene::renderComparison()
{
  const Theme::Palette& palette = Theme::palette();
  UI::sectionLabel(LOC("SCOUTING_COMPARE"));
  if (!UI::beginDataTable(
          "ScoutCompare", 6,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
          700.0f, ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
    return;
  ImGui::TableSetupColumn(LOC("SCOUTING_COL_ROLE"));
  ImGui::TableSetupColumn(LOC("SCOUTING_COL_MY_DEPTH"));
  ImGui::TableSetupColumn(LOC("SCOUTING_COL_MY_BEST"),
                          ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn(LOC("SCOUTING_COL_CANDIDATE"),
                          ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn(LOC("SCOUTING_COL_POTENTIAL"));
  ImGui::TableSetupColumn(LOC("SCOUTING_COL_DELTA"));
  ImGui::TableHeadersRow();
  for (const ComparisonLine& line : comparison)
  {
    const SquadComparisonRow& row = line.row;
    ImGui::PushID(static_cast<int>(std::to_underlying(row.role)));
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(line.role.c_str());
    ImGui::TableNextColumn();
    ImGui::TextColored(row.own_count < 2 ? palette.warning : palette.text, "%d",
                       row.own_count);
    ImGui::TableNextColumn();
    if (row.own_best_id != 0)
    {
      UI::ratingChip(row.own_best_overall);
      ImGui::SameLine();
      if (UI::link(line.own_name.c_str(), "own"))
        Navigation::openPlayer(guiView, row.own_best_id);
    }
    else
    {
      ImGui::TextColored(palette.faint, "–");
    }
    ImGui::TableNextColumn();
    if (row.candidate_id != 0)
    {
      UI::ratingChip(row.candidate_overall);
      ImGui::SameLine();
      if (UI::link(line.candidate_name.c_str(), "candidate"))
        Navigation::openPlayer(guiView, row.candidate_id);
      ImGui::SameLine();
      knowledgeBar(row.candidate_knowledge);
    }
    else
    {
      ImGui::TextColored(palette.faint, "–");
    }
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(line.potential_text.c_str());
    ImGui::TableNextColumn();
    if (row.candidate_id != 0)
    {
      const float delta = row.candidate_overall - row.own_best_overall;
      ImGui::TextColored(delta >= 0.0f ? palette.positive : palette.negative,
                         "%+.0f", static_cast<double>(delta));
    }
    ImGui::PopID();
  }
  ImGui::EndTable();
}

void ScoutingScene::renderFocus()
{
  const Theme::Palette& palette = Theme::palette();
  GameController& controller = guiView->getController();
  renderFocusEditor();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::M * dpi()));
  UI::sectionLabel(LOC("SCOUTING_FOCUS_LIST"));
  const auto& focuses = controller.getRecruitmentFocuses();
  if (focuses.empty())
  {
    UI::emptyState(LOC("SCOUTING_FOCUS_EMPTY_TITLE"),
                   LOC("SCOUTING_FOCUS_EMPTY_BODY"));
    return;
  }
  const auto limit = [](int64_t value)
  {
    return value > 0 ? Format::money(value)
                     : std::string(LOC("SCOUTING_NO_LIMIT"));
  };
  std::optional<uint32_t> removed;
  for (const RecruitmentFocus& focus : focuses)
  {
    ImGui::PushID(static_cast<int>(focus.id));
    const std::string summary = fmt::sprintf(
        LOC("SCOUTING_FOCUS_SUMMARY"),
        roleLabel(focus.role == PlayerRole::UNKNOWN
                      ? 0
                      : static_cast<int>(std::to_underlying(focus.role)) + 1)
            .c_str(),
        focus.min_age, focus.max_age, focus.min_ability,
        limit(focus.max_fee).c_str(), limit(focus.max_wage).c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(
        focus_draft.id == focus.id ? palette.accent : palette.text, "%s",
        summary.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(LOC("SCOUTING_FOCUS_EDIT")))
    {
      focus_draft = focus;
      focus_role = focus.role == PlayerRole::UNKNOWN
                       ? 0
                       : static_cast<int>(std::to_underlying(focus.role)) + 1;
      focus_min_age = focus.min_age;
      focus_max_age = focus.max_age;
      focus_min_ability = focus.min_ability;
      focus_max_fee = focus.max_fee;
      focus_max_wage = focus.max_wage;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(LOC("SCOUTING_FOCUS_REMOVE"))) removed = focus.id;
    ImGui::PopID();
  }
  if (removed && controller.removeRecruitmentFocus(*removed))
  {
    if (focus_draft.id == *removed) focus_draft = RecruitmentFocus{};
    search_dirty = true;
  }
}

void ScoutingScene::renderFocusEditor()
{
  GameController& controller = guiView->getController();
  UI::beginAutoHeightCard("scouting_focus_editor",
                          focus_draft.id == 0 ? LOC("SCOUTING_FOCUS_NEW")
                                              : LOC("SCOUTING_FOCUS_EDITOR"));
  const float field =
      std::min(320.0f * dpi(),
               ImGui::GetContentRegionAvail().x - FILTER_LABEL_WIDTH * dpi());
  labelled(LOC("SCOUTING_FILTER_ROLE"));
  ImGui::SetNextItemWidth(field);
  roleCombo("##focus_role", &focus_role);
  labelled(LOC("SCOUTING_FILTER_AGE"));
  ImGui::SetNextItemWidth(field);
  ImGui::DragIntRange2("##focus_age", &focus_min_age, &focus_max_age, 0.2f, 15,
                       45, LOC("SCOUTING_FILTER_AGE_MIN"),
                       LOC("SCOUTING_FILTER_AGE_MAX"));
  labelled(LOC("SCOUTING_FOCUS_MIN_ABILITY"));
  ImGui::SetNextItemWidth(field);
  ImGui::SliderInt("##focus_ability", &focus_min_ability, 0, 90);
  UI::MoneyInputOptions money;
  money.width = field;
  labelled(LOC("SCOUTING_FOCUS_MAX_FEE"));
  UI::moneyInput("##focus_fee", focus_max_fee, money);
  labelled(LOC("SCOUTING_FOCUS_MAX_WAGE"));
  money.maximum = std::numeric_limits<uint32_t>::max();
  UI::moneyInput("##focus_wage", focus_max_wage, money);

  if (UI::primaryButton(LOC("SCOUTING_FOCUS_SAVE")))
  {
    RecruitmentFocus focus = focus_draft;
    focus.role = focus_role == 0 ? PlayerRole::UNKNOWN
                                 : static_cast<PlayerRole>(focus_role - 1);
    focus.min_age = static_cast<uint8_t>(focus_min_age);
    focus.max_age = static_cast<uint8_t>(focus_max_age);
    focus.min_ability = static_cast<uint8_t>(focus_min_ability);
    focus.max_fee = focus_max_fee;
    focus.max_wage = static_cast<uint32_t>(focus_max_wage);
    if (controller.saveRecruitmentFocus(focus) != 0)
    {
      showToast(LOC("SCOUTING_SAVED_FOCUS"));
      focus_draft = RecruitmentFocus{};
      search_dirty = true;
    }
    else
    {
      showToast(fmt::sprintf(LOC("SCOUTING_FOCUS_FULL"),
                             static_cast<int>(ScoutingTuning::MAX_FOCUSES)),
                true);
    }
  }
  if (focus_draft.id != 0)
  {
    ImGui::SameLine();
    if (ImGui::Button(LOC("SCOUTING_FOCUS_CANCEL")))
      focus_draft = RecruitmentFocus{};
  }
  UI::endCard();
}
