// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/records_scene.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <array>
#include <format>
#include <utility>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/competition.h"
#include "model/records.h"

namespace
{
constexpr float TWO_COLUMN_MIN_WIDTH = 900.0f;
constexpr size_t PLAYER_LIST_SIZE = 10;

std::string teamName(const GameController& controller, TeamID id)
{
  const auto team = controller.getTeamById(id);
  return team ? team->get().getName() : std::string("–");
}

std::string seasonLabel(uint16_t year)
{
  return std::format("{}/{:02}", year, (year + 1) % 100);
}

std::string yearSpan(uint16_t first, uint16_t last)
{
  if (first == 0) return "–";
  return first == last ? seasonLabel(first)
                       : std::format("{}–{}", first, last + 1);
}
}  // namespace

RecordsScene::RecordsScene(GUIView* parent) : ManagementScene(parent) {}

void RecordsScene::refresh()
{
  GameController& controller = guiView->getController();
  const auto managed = controller.getManagedTeam();
  if (club_id == 0 && managed) club_id = managed->get().getId();
  if (league_id == 0 && managed) league_id = managed->get().getLeagueId();
  const auto gamedata = controller.getGameData();
  const auto known = [&](PlayerID id)
  { return id != 0 && gamedata && gamedata->getPlayer(id) ? id : PlayerID{0}; };

  const auto format = [&](const RecordEntry& entry, bool league)
  {
    RecordLine line;
    line.title = LOC(recordKindKey(entry.kind));
    const std::string club = teamName(controller, entry.team_id);
    const std::string opponent = teamName(controller, entry.opponent_id);
    const std::string score =
        std::format("{}-{}", entry.goals_for, entry.goals_against);
    using enum RecordKind;
    switch (entry.kind)
    {
      case BiggestWin:
      case BiggestDefeat:
      case HighestScoringMatch:
        line.value = score;
        line.holder = league
                          ? fmt::sprintf(LOC("RECORDS_MATCH_LEAGUE"),
                                         club.c_str(), opponent.c_str())
                          : fmt::sprintf(LOC(entry.home ? "RECORDS_MATCH_HOME"
                                                        : "RECORDS_MATCH_AWAY"),
                                         opponent.c_str());
        line.when = Format::date(entry.date);
        break;
      case HighestAttendance:
        line.value = Format::thousands(entry.value);
        line.holder = fmt::sprintf(LOC("RECORDS_MATCH_LEAGUE"), club.c_str(),
                                   opponent.c_str());
        line.when = Format::date(entry.date);
        break;
      case MostGoalsSeason:
        line.value = fmt::sprintf(LOC("RECORDS_VALUE_GOALS"),
                                  static_cast<int>(entry.value));
        line.holder = league ? club : std::string();
        line.when = seasonLabel(entry.season_year);
        break;
      case MostPointsSeason:
        line.value = fmt::sprintf(LOC("RECORDS_VALUE_POINTS"),
                                  static_cast<int>(entry.value));
        line.holder = league ? club : std::string();
        line.when = seasonLabel(entry.season_year);
        break;
      case TopScorerSeason:
        line.value = fmt::sprintf(LOC("RECORDS_VALUE_GOALS"),
                                  static_cast<int>(entry.value));
        line.holder =
            league ? std::format("{} ({})", entry.name, club) : entry.name;
        line.player_id = known(entry.player_id);
        line.when = seasonLabel(entry.season_year);
        break;
      case RecordSigning:
      case RecordSale:
      case COUNT:
        line.value = Format::money(entry.value);
        line.holder =
            fmt::sprintf(LOC(entry.kind == RecordSale ? "RECORDS_SALE_TO"
                                                      : "RECORDS_SIGNING_FROM"),
                         entry.name.c_str(), opponent.c_str());
        if (league && entry.kind == RecordSigning)
          line.holder += "  ·  " + club;
        line.player_id = known(entry.player_id);
        line.when = Format::date(entry.date);
        break;
    }
    return line;
  };

  club_records.clear();
  for (const RecordEntry& entry : controller.getClubRecords(club_id))
    club_records.push_back(format(entry, false));
  league_records.clear();
  for (const RecordEntry& entry : controller.getLeagueRecords(league_id))
    league_records.push_back(format(entry, true));

  const auto playerLine = [&](const ClubPlayerTotal& totals, bool goals_first)
  {
    PlayerLine line;
    line.player_id = known(totals.player_id);
    line.name = totals.name;
    line.span = yearSpan(totals.first_year, totals.last_year);
    line.figure =
        std::to_string(goals_first ? totals.goals : totals.appearances);
    const int count = goals_first ? totals.appearances : totals.goals;
    line.detail = fmt::sprintf(
        Format::plural(goals_first ? "RECORDS_IN_APPS" : "RECORDS_WITH_GOALS",
                       count),
        count);
    return line;
  };
  scorers.clear();
  for (const ClubPlayerTotal& totals :
       controller.getClubTopScorers(club_id, PLAYER_LIST_SIZE))
    scorers.push_back(playerLine(totals, true));
  appearances.clear();
  for (const ClubPlayerTotal& totals :
       controller.getClubMostAppearances(club_id, PLAYER_LIST_SIZE))
    appearances.push_back(playerLine(totals, false));
  legends.clear();
  for (const LegendEntry& legend : controller.getHallOfFame(club_id))
  {
    PlayerLine line = playerLine(legend.totals, false);
    line.figure = std::to_string(legend.totals.appearances);
    line.detail = fmt::sprintf(LOC("RECORDS_LEGEND_LINE"), legend.totals.goals,
                               legend.honours);
    legends.push_back(std::move(line));
  }
  all_time.clear();
  for (const AllTimeRow& row : controller.getAllTimeTable(league_id))
    all_time.emplace_back(row.team_id, teamName(controller, row.team_id),
                          row.played, row.won, row.drawn, row.lost,
                          row.goals_for, row.goals_against, row.points);
}

void RecordsScene::renderContent()
{
  UI::pageHeader(LOC("RECORDS_TITLE"), LOC("RECORDS_SUBTITLE"));
  const std::array<const char*, 4> tabs = {
      LOC("RECORDS_TAB_CLUB"), LOC("RECORDS_TAB_LEAGUE"),
      LOC("RECORDS_TAB_ALL_TIME"), LOC("RECORDS_TAB_HALL_OF_FAME")};
  if (auto tab = std::to_underlying(active_tab); UI::segmented(
          "##records_tab", tab, tabs,
          std::min(620.0f * Theme::scale(), ImGui::GetContentRegionAvail().x)))
    active_tab = static_cast<Tab>(tab);
  const char* awards = LOC("RECORDS_OPEN_AWARDS");
  UI::sameLineIfFits(UI::buttonWidth(awards));
  if (UI::secondaryButton(awards))
    Navigation::open(guiView, NavSection::AWARDS);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));

  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const bool twoColumns = available >= TWO_COLUMN_MIN_WIDTH * Theme::scale();
  const float half =
      twoColumns ? std::floor((available - gap) * 0.5f) : available;
  using enum Tab;
  switch (active_tab)
  {
    case CLUB:
      renderClubSelector();
      renderRecords("club_records", LOC("RECORDS_CLUB_CARD"), club_records,
                    available);
      renderPlayers("club_scorers", LOC("RECORDS_TOP_SCORERS"), scorers, half);
      if (twoColumns) ImGui::SameLine();
      renderPlayers("club_apps", LOC("RECORDS_MOST_APPEARANCES"), appearances,
                    twoColumns ? available - gap - half : available);
      break;
    case LEAGUE:
      renderLeagueSelector();
      renderRecords("league_records", LOC("RECORDS_LEAGUE_CARD"),
                    league_records, available);
      break;
    case ALL_TIME:
      renderLeagueSelector();
      renderAllTime();
      break;
    case HALL_OF_FAME:
      renderClubSelector();
      renderHallOfFame();
      break;
  }
}

void RecordsScene::renderClubSelector()
{
  const GameController& controller = guiView->getController();
  ImGui::SetNextItemWidth(
      std::min(280.0f * Theme::scale(), ImGui::GetContentRegionAvail().x));
  if (ImGui::BeginCombo("##records_club", teamName(controller, club_id).c_str(),
                        ImGuiComboFlags_HeightLarge))
  {
    for (const auto& leagueRef : controller.getLeagues())
    {
      ImGui::SeparatorText(Competitions::leagueName(leagueRef.get()).c_str());
      for (const auto& team :
           controller.getTeamsInLeague(leagueRef.get().getId()))
        if (ImGui::Selectable(team.get().getName().c_str(),
                              team.get().getId() == club_id))
        {
          club_id = team.get().getId();
          refresh();
        }
    }
    ImGui::EndCombo();
  }
}

void RecordsScene::renderLeagueSelector()
{
  const GameController& controller = guiView->getController();
  const auto league = controller.getLeagueById(league_id);
  ImGui::SetNextItemWidth(
      std::min(280.0f * Theme::scale(), ImGui::GetContentRegionAvail().x));
  if (ImGui::BeginCombo(
          "##records_league",
          league ? Competitions::leagueName(league->get()).c_str() : "",
          ImGuiComboFlags_HeightLarge))
  {
    for (const auto& leagueRef : controller.getLeagues())
      if (ImGui::Selectable(Competitions::leagueName(leagueRef.get()).c_str(),
                            leagueRef.get().getId() == league_id))
      {
        league_id = leagueRef.get().getId();
        refresh();
      }
    ImGui::EndCombo();
  }
}

void RecordsScene::renderRecords(const char* id, const char* title,
                                 const std::vector<RecordLine>& lines,
                                 float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard(id, title, width);
  if (lines.empty())
  {
    UI::emptyState(LOC("RECORDS_EMPTY_TITLE"), LOC("RECORDS_EMPTY_BODY"));
    UI::endCard();
    return;
  }
  static const std::array<UI::Column, 4> COLUMNS = {{
      {"RECORDS_COL_RECORD", 200.0f, 0},
      {"RECORDS_COL_VALUE", 110.0f, 0},
      {"RECORDS_COL_HOLDER", 0.0f, 0},
      {"RECORDS_COL_WHEN", 110.0f, 1},
  }};
  std::array<UI::Column, 4> columns = COLUMNS;
  for (UI::Column& column : columns) column.label = LOC(column.label);
  if (const UI::ColumnMask mask =
          UI::fitColumns(columns, ImGui::GetContentRegionAvail().x, 140.0f);
      UI::beginResponsiveTable(
          id, columns, mask,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
  {
    for (size_t index = 0; index < lines.size(); ++index)
    {
      const RecordLine& line = lines[index];
      ImGui::PushID(static_cast<int>(index));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      UI::textFitted(line.title, ImGui::GetContentRegionAvail().x,
                     palette.muted);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(line.value.c_str());
      ImGui::TableNextColumn();
      if (line.player_id != 0)
      {
        if (UI::link(line.holder.c_str(), "##holder"))
          Navigation::openPlayer(guiView, line.player_id);
      }
      else
      {
        UI::textFitted(line.holder, ImGui::GetContentRegionAvail().x,
                       palette.text);
      }
      if (UI::cell(mask, 3))
        ImGui::TextColored(palette.faint, "%s", line.when.c_str());
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

void RecordsScene::renderPlayers(const char* id, const char* title,
                                 const std::vector<PlayerLine>& lines,
                                 float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard(id, title, width);
  if (lines.empty())
  {
    UI::emptyState(LOC("RECORDS_PLAYERS_EMPTY_TITLE"),
                   LOC("RECORDS_PLAYERS_EMPTY_BODY"));
    UI::endCard();
    return;
  }
  static const std::array<UI::Column, 4> COLUMNS = {{
      {"RECORDS_COL_PLAYER", 0.0f, 0},
      {"RECORDS_COL_YEARS", 100.0f, 2},
      {"RECORDS_COL_DETAIL", 110.0f, 1},
      {"RECORDS_COL_TOTAL", 56.0f, 0},
  }};
  std::array<UI::Column, 4> columns = COLUMNS;
  for (UI::Column& column : columns) column.label = LOC(column.label);
  if (const UI::ColumnMask mask =
          UI::fitColumns(columns, ImGui::GetContentRegionAvail().x, 130.0f);
      UI::beginResponsiveTable(
          id, columns, mask,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
  {
    for (size_t index = 0; index < lines.size(); ++index)
    {
      const PlayerLine& line = lines[index];
      ImGui::PushID(static_cast<int>(index));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (line.player_id != 0)
      {
        if (UI::link(line.name.c_str(), "##player"))
          Navigation::openPlayer(guiView, line.player_id);
      }
      else
      {
        UI::textFitted(line.name, ImGui::GetContentRegionAvail().x,
                       palette.text);
      }
      if (UI::cell(mask, 1))
        ImGui::TextColored(palette.faint, "%s", line.span.c_str());
      if (UI::cell(mask, 2))
        UI::textFitted(line.detail, ImGui::GetContentRegionAvail().x,
                       palette.muted);
      ImGui::TableNextColumn();
      UI::textRight(line.figure.c_str());
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

void RecordsScene::renderAllTime()
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("records_all_time", LOC("RECORDS_ALL_TIME_CARD"));
  if (all_time.empty())
  {
    UI::emptyState(LOC("RECORDS_EMPTY_TITLE"), LOC("RECORDS_ALL_TIME_EMPTY"));
    UI::endCard();
    return;
  }
  static const std::array<UI::Column, 10> COLUMNS = {{
      {"#", 32.0f, 0},
      {"RECORDS_COL_CLUB", 0.0f, 0},
      {"TABLE_COL_PLAYED", 44.0f, 0},
      {"TABLE_COL_WON", 44.0f, 2},
      {"TABLE_COL_DRAWN", 44.0f, 2},
      {"TABLE_COL_LOST", 44.0f, 2},
      {"TABLE_COL_GF", 50.0f, 3},
      {"TABLE_COL_GA", 50.0f, 3},
      {"TABLE_COL_GD", 50.0f, 1},
      {"RECORDS_COL_POINTS", 52.0f, 0},
  }};
  std::array<UI::Column, 10> columns = COLUMNS;
  for (size_t index = 1; index < columns.size(); ++index)
    columns[index].label = LOC(columns[index].label);
  const UI::ColumnMask mask =
      UI::fitColumns(columns, ImGui::GetContentRegionAvail().x, 150.0f);
  const TeamID managed =
      guiView->getController().getManagedTeam()
          ? guiView->getController().getManagedTeam()->get().getId()
          : 0;
  if (UI::beginResponsiveTable(
          "all_time", columns, mask,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
  {
    for (size_t index = 0; index < all_time.size(); ++index)
    {
      const TableLine& row = all_time[index];
      ImGui::TableNextRow();
      if (row.team_id == managed)
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                               Theme::toU32(palette.accent, 0.16f));
      ImGui::TableNextColumn();
      ImGui::TextColored(palette.muted, "%zu", index + 1);
      ImGui::TableNextColumn();
      UI::textFitted(row.club, ImGui::GetContentRegionAvail().x, palette.text);
      const auto number = [&](int column, int64_t value)
      {
        if (UI::cell(mask, column))
          UI::textRight(std::to_string(value).c_str());
      };
      number(2, row.played);
      number(3, row.won);
      number(4, row.drawn);
      number(5, row.lost);
      number(6, row.goals_for);
      number(7, row.goals_against);
      if (UI::cell(mask, 8))
        UI::textRight(Format::signedInt(static_cast<int>(row.goals_for) -
                                        static_cast<int>(row.goals_against))
                          .c_str());
      ImGui::TableNextColumn();
      UI::textRight(std::to_string(row.points).c_str());
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

void RecordsScene::renderHallOfFame()
{
  if (legends.empty())
  {
    UI::beginAutoHeightCard("records_legends", LOC("RECORDS_HALL_OF_FAME"));
    UI::emptyState(LOC("RECORDS_LEGENDS_EMPTY_TITLE"),
                   LOC("RECORDS_LEGENDS_CRITERIA"));
    UI::endCard();
    return;
  }
  renderPlayers("records_legends", LOC("RECORDS_HALL_OF_FAME"), legends, 0.0f);
  ImGui::TextColored(Theme::palette().faint, "%s",
                     LOC("RECORDS_LEGENDS_CRITERIA"));
}
