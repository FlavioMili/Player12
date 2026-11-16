// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/awards_scene.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <set>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/awards.h"
#include "model/calendar.h"
#include "model/competition.h"

namespace
{
constexpr float TWO_COLUMN_MIN_WIDTH = 900.0f;
constexpr size_t RACE_SIZE = 5;
constexpr size_t YOUNG_RACE_SIZE = 3;
constexpr std::array<const char*, 12> MONTH_KEYS = {
    "MONTH_JAN", "MONTH_FEB", "MONTH_MAR", "MONTH_APR",
    "MONTH_MAY", "MONTH_JUN", "MONTH_JUL", "MONTH_AUG",
    "MONTH_SEP", "MONTH_OCT", "MONTH_NOV", "MONTH_DEC"};

std::string seasonLabel(uint16_t year)
{
  return std::format("{}/{:02}", year, (year + 1) % 100);
}

std::string teamName(const GameController& controller, TeamID id)
{
  const auto team = controller.getTeamById(id);
  return team ? team->get().getName() : std::string("–");
}

std::string figureFor(const GameController& controller,
                      const AwardRecord& record)
{
  using enum AwardType;
  switch (record.type)
  {
    case GoldenBoot:
      return fmt::sprintf(LOC("AWARDS_FIGURE_GOALS"),
                          static_cast<int>(record.value));
    case GoldenGlove:
      return fmt::sprintf(LOC("AWARDS_FIGURE_CLEAN_SHEETS"),
                          static_cast<int>(record.value));
    case ManagerOfMonth:
    case ManagerOfSeason:
      return fmt::sprintf(LOC("AWARDS_FIGURE_MANAGER"),
                          static_cast<double>(record.value), record.count);
    case GoalOfMonth:
      return fmt::sprintf(LOC("AWARDS_FIGURE_GOAL"), record.count,
                          teamName(controller, record.opponent_id).c_str());
    case PlayerOfMonth:
    case YoungPlayerOfMonth:
    case PlayerOfSeason:
    case YoungPlayerOfSeason:
    case TeamOfSeason:
    case COUNT:
      break;
  }
  return fmt::sprintf(LOC("AWARDS_FIGURE_RATING"),
                      static_cast<double>(record.value), record.count);
}

/** Name as a link to the profile while the player is still in the game. */
void nameLink(GUIView* view, PlayerID player_id, const std::string& name,
              float width)
{
  const Theme::Palette& palette = Theme::palette();
  if (player_id == 0)
  {
    UI::textFitted(name, width, palette.text);
    return;
  }
  ImGui::PushID(static_cast<int>(player_id));
  if (UI::link(name.c_str(), "##player"))
    Navigation::openPlayer(view, player_id);
  ImGui::PopID();
}
}  // namespace

AwardsScene::AwardsScene(GUIView* parent) : ManagementScene(parent) {}

void AwardsScene::refresh()
{
  GameController& controller = guiView->getController();
  if (const auto managed = controller.getManagedTeam();
      league_id == 0 && managed)
    league_id = managed->get().getLeagueId();
  const uint16_t now =
      SeasonCalendar::seasonStartYear(controller.getCurrentDate());
  std::set years{now};
  for (const AwardRecord& record : controller.getAwardHistory())
    if (record.league_id == league_id) years.insert(record.season_year);
  seasons.assign(years.rbegin(), years.rend());
  if (season_year == 0 || !years.contains(season_year)) season_year = now;
  current_season = season_year == now;

  const auto gamedata = controller.getGameData();
  const auto winnerOf = [&](const AwardRecord& record)
  {
    Winner winner;
    winner.award = LOC(record.type == AwardType::TeamOfSeason
                           ? Awards::slotKey(record.slot)
                           : awardTypeKey(record.type));
    winner.player_id = record.player_id != 0 && gamedata &&
                               gamedata->getPlayer(record.player_id)
                           ? record.player_id
                           : 0;
    winner.name = record.name;
    winner.club = record.player_id != 0 ? teamName(controller, record.team_id)
                                        : std::string();
    winner.figure = figureFor(controller, record);
    return winner;
  };

  season_winners.clear();
  team_of_season.clear();
  months.clear();
  for (const AwardRecord& record :
       controller.getLeagueAwards(league_id, season_year))
  {
    if (record.month == 0)
    {
      (record.type == AwardType::TeamOfSeason ? team_of_season : season_winners)
          .push_back(winnerOf(record));
      continue;
    }
    if (const auto label = std::string(LOC(MONTH_KEYS[record.month - 1U]));
        months.empty() || months.back().month != label)
    {
      months.emplace_back();
      months.back().month = label;
    }
    MonthRow& row = months.back();
    switch (record.type)
    {
      case AwardType::PlayerOfMonth:
        row.player = winnerOf(record);
        break;
      case AwardType::YoungPlayerOfMonth:
        row.young = winnerOf(record);
        break;
      case AwardType::ManagerOfMonth:
        row.manager = winnerOf(record);
        break;
      default:
        row.goal = winnerOf(record);
        break;
    }
  }
  std::ranges::reverse(months);

  race.clear();
  young_race.clear();
  if (!current_season) return;
  const auto raceRows = [&](bool young, size_t limit)
  {
    std::vector<RaceRow> rows;
    const std::vector<AwardPlayerTally> tallies =
        controller.getAwardRace(league_id, young, limit);
    uint16_t most = 0;
    for (const auto& [key, tally] :
         controller.getGame()->getWorld().getAwards().seasonTallies())
      if (key.first == league_id) most = std::max(most, tally.minutes);
    const auto needed = static_cast<uint16_t>(most * Awards::SEASON_MIN_SHARE);
    for (const AwardPlayerTally& tally : tallies)
    {
      RaceRow row;
      row.player_id = tally.player_id;
      const auto player = gamedata->getPlayer(tally.player_id);
      row.name = player ? player->get().getName() : std::string("–");
      row.club = teamName(controller, tally.team_id);
      row.rating = std::format("{:.2f}", tally.averageRating());
      row.contribution = fmt::sprintf(LOC("AWARDS_RACE_LINE"), tally.goals,
                                      tally.assists, tally.minutes);
      row.qualified = tally.minutes >= needed;
      rows.push_back(std::move(row));
    }
    return rows;
  };
  race = raceRows(false, RACE_SIZE);
  young_race = raceRows(true, YOUNG_RACE_SIZE);
}

void AwardsScene::renderContent()
{
  UI::pageHeader(LOC("AWARDS_TITLE"), LOC("AWARDS_SUBTITLE"));
  renderSelectors();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));

  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const bool twoColumns = available >= TWO_COLUMN_MIN_WIDTH * Theme::scale();
  const float half =
      twoColumns ? std::floor((available - gap) * 0.5f) : available;
  if (current_season)
  {
    renderRace(half);
    if (twoColumns) ImGui::SameLine();
    renderSeason(twoColumns ? available - gap - half : available);
  }
  else
  {
    renderSeason(half);
    if (twoColumns) ImGui::SameLine();
    renderTeamOfSeason(twoColumns ? available - gap - half : available);
  }
  if (current_season && !team_of_season.empty()) renderTeamOfSeason(available);
  renderMonths();
}

void AwardsScene::renderSelectors()
{
  const GameController& controller = guiView->getController();
  const auto league = controller.getLeagueById(league_id);
  ImGui::SetNextItemWidth(
      std::min(280.0f * Theme::scale(), ImGui::GetContentRegionAvail().x));
  if (ImGui::BeginCombo(
          "##awards_league",
          league ? Competitions::leagueName(league->get()).c_str() : "",
          ImGuiComboFlags_HeightLarge))
  {
    for (const auto& leagueRef : controller.getLeagues())
    {
      const League& option = leagueRef.get();
      if (ImGui::Selectable(option.getName().c_str(),
                            option.getId() == league_id))
      {
        league_id = option.getId();
        season_year = 0;
        refresh();
      }
    }
    ImGui::EndCombo();
  }
  const float comboWidth = 140.0f * Theme::scale();
  UI::sameLineIfFits(comboWidth);
  ImGui::SetNextItemWidth(comboWidth);
  if (ImGui::BeginCombo("##awards_season", seasonLabel(season_year).c_str()))
  {
    // refresh() rebuilds `seasons`: it runs after the loop.
    std::optional<uint16_t> picked;
    for (const uint16_t year : seasons)
      if (ImGui::Selectable(seasonLabel(year).c_str(), year == season_year))
        picked = year;
    ImGui::EndCombo();
    if (picked.has_value())
    {
      season_year = *picked;
      refresh();
    }
  }
  const char* records = LOC("AWARDS_OPEN_RECORDS");
  UI::sameLineIfFits(UI::buttonWidth(records));
  if (UI::secondaryButton(records))
    Navigation::open(guiView, NavSection::RECORDS);
}

void AwardsScene::renderWinner(const Winner& winner, float width)
{
  const Theme::Palette& palette = Theme::palette();
  const float labelWidth = std::min(190.0f * Theme::scale(), width * 0.4f);
  const float start = ImGui::GetCursorPosX();
  ImGui::AlignTextToFramePadding();
  UI::textFitted(winner.award, labelWidth - Theme::Space::S * Theme::scale(),
                 palette.muted);
  ImGui::SameLine(start + labelWidth);
  ImGui::BeginGroup();
  const float valueWidth = std::max(0.0f, width - labelWidth);
  nameLink(guiView, winner.player_id, winner.name, valueWidth);
  std::string detail = winner.figure;
  if (!winner.club.empty()) detail = winner.club + "  ·  " + detail;
  UI::textFitted(detail, valueWidth, palette.faint);
  ImGui::EndGroup();
}

void AwardsScene::renderRace(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("awards_race", LOC("AWARDS_RACE"), width);
  if (race.empty())
  {
    UI::emptyState(LOC("AWARDS_RACE_EMPTY_TITLE"),
                   LOC("AWARDS_RACE_EMPTY_BODY"));
    UI::endCard();
    return;
  }
  const auto rows = [&](const std::vector<RaceRow>& lines, const char* id)
  {
    static const std::array<UI::Column, 4> COLUMNS = {{
        {"AWARDS_COL_PLAYER", 0.0f, 0},
        {"AWARDS_COL_CLUB", 150.0f, 2},
        {"AWARDS_COL_OUTPUT", 150.0f, 3},
        {"AWARDS_COL_RATING", 60.0f, 0},
    }};
    std::array<UI::Column, 4> columns = COLUMNS;
    for (UI::Column& column : columns) column.label = LOC(column.label);
    const UI::ColumnMask mask =
        UI::fitColumns(columns, ImGui::GetContentRegionAvail().x, 140.0f);
    if (!UI::beginResponsiveTable(
            id, columns, mask,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
      return;
    for (const RaceRow& row : lines)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      // Links draw in the link colour: both are muted for the unqualified.
      if (!row.qualified)
      {
        ImGui::PushStyleColor(ImGuiCol_Text, palette.muted);
        ImGui::PushStyleColor(ImGuiCol_TextLink, palette.muted);
      }
      nameLink(guiView, row.player_id, row.name,
               ImGui::GetContentRegionAvail().x);
      if (!row.qualified)
      {
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", LOC("AWARDS_RACE_UNQUALIFIED"));
      }
      if (UI::cell(mask, 1))
        UI::textFitted(row.club, ImGui::GetContentRegionAvail().x,
                       palette.muted);
      if (UI::cell(mask, 2))
        UI::textFitted(row.contribution, ImGui::GetContentRegionAvail().x,
                       palette.faint);
      ImGui::TableNextColumn();
      UI::textRight(row.rating.c_str());
    }
    ImGui::EndTable();
  };
  UI::sectionLabel(LOC("AWARDS_RACE_PLAYER"));
  rows(race, "race_all");
  if (!young_race.empty())
  {
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));
    UI::sectionLabel(LOC("AWARDS_RACE_YOUNG"));
    rows(young_race, "race_young");
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.faint, "%s", LOC("AWARDS_RACE_NOTE"));
  ImGui::PopTextWrapPos();
  UI::endCard();
}

void AwardsScene::renderSeason(float width)
{
  UI::beginAutoHeightCard("awards_season", LOC("AWARDS_SEASON"), width);
  if (season_winners.empty())
  {
    UI::emptyState(LOC("AWARDS_SEASON_EMPTY_TITLE"),
                   LOC("AWARDS_SEASON_EMPTY_BODY"));
  }
  else
  {
    // One player can win several awards: rows are scoped by position.
    for (size_t index = 0; index < season_winners.size(); ++index)
    {
      ImGui::PushID(static_cast<int>(index));
      renderWinner(season_winners[index], ImGui::GetContentRegionAvail().x);
      ImGui::PopID();
    }
  }
  UI::endCard();
}

void AwardsScene::renderTeamOfSeason(float width)
{
  UI::beginAutoHeightCard("awards_team", LOC("AWARD_TEAM_OF_SEASON"), width);
  if (team_of_season.empty())
  {
    UI::emptyState(LOC("AWARDS_TEAM_EMPTY_TITLE"),
                   LOC("AWARDS_SEASON_EMPTY_BODY"));
  }
  else
  {
    for (size_t index = 0; index < team_of_season.size(); ++index)
    {
      ImGui::PushID(static_cast<int>(index));
      renderWinner(team_of_season[index], ImGui::GetContentRegionAvail().x);
      ImGui::PopID();
    }
  }
  UI::endCard();
}

void AwardsScene::renderMonths()
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("awards_months", LOC("AWARDS_MONTHLY"));
  if (months.empty())
  {
    UI::emptyState(LOC("AWARDS_MONTHLY_EMPTY_TITLE"),
                   LOC("AWARDS_MONTHLY_EMPTY_BODY"));
    UI::endCard();
    return;
  }
  static const std::array<UI::Column, 5> COLUMNS = {{
      {"AWARDS_COL_MONTH", 60.0f, 0},
      {"AWARD_PLAYER_OF_MONTH", 0.0f, 0},
      {"AWARD_MANAGER_OF_MONTH", 200.0f, 1},
      {"AWARD_YOUNG_PLAYER_OF_MONTH", 200.0f, 2},
      {"AWARD_GOAL_OF_MONTH", 220.0f, 3},
  }};
  std::array<UI::Column, 5> columns = COLUMNS;
  for (UI::Column& column : columns) column.label = LOC(column.label);
  const UI::ColumnMask mask =
      UI::fitColumns(columns, ImGui::GetContentRegionAvail().x, 180.0f);
  // The same player can win several awards of one month: cells are scoped
  // by column.
  const auto winnerCell = [&](const Winner& winner, int column)
  {
    const float width = ImGui::GetContentRegionAvail().x;
    if (winner.name.empty())
    {
      ImGui::TextColored(palette.faint, "%s", LOC("AWARDS_NOT_AWARDED"));
      return;
    }
    ImGui::PushID(column);
    nameLink(guiView, winner.player_id, winner.name, width);
    ImGui::PopID();
    const std::string detail = winner.club.empty()
                                   ? winner.figure
                                   : winner.club + "  ·  " + winner.figure;
    UI::textFitted(detail, width, palette.faint);
  };
  if (UI::beginResponsiveTable(
          "monthly", columns, mask,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
  {
    for (size_t index = 0; index < months.size(); ++index)
    {
      const MonthRow& row = months[index];
      ImGui::PushID(static_cast<int>(index));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextColored(palette.muted, "%s", row.month.c_str());
      ImGui::TableNextColumn();
      winnerCell(row.player, 1);
      if (UI::cell(mask, 2)) winnerCell(row.manager, 2);
      if (UI::cell(mask, 3)) winnerCell(row.young, 3);
      if (UI::cell(mask, 4)) winnerCell(row.goal, 4);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.faint, "%s", LOC("AWARDS_GOAL_NOTE"));
  ImGui::PopTextWrapPos();
  UI::endCard();
}
