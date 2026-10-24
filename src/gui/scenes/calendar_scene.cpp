// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/calendar_scene.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

#include "controller/game_controller.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/calendar.h"
#include "model/competition.h"
#include "model/continental.h"
#include "model/inbox.h"

namespace
{
constexpr float TWO_COLUMN_MIN_WIDTH = 900.0f;
constexpr float CELL_MIN_HEIGHT = 54.0f;
constexpr float CELL_MAX_HEIGHT = 92.0f;
constexpr int SEASON_MONTHS = 12;

constexpr std::array<const char*, 12> MONTH_KEYS = {
    "CALENDAR_MONTH_JANUARY",   "CALENDAR_MONTH_FEBRUARY",
    "CALENDAR_MONTH_MARCH",     "CALENDAR_MONTH_APRIL",
    "CALENDAR_MONTH_MAY",       "CALENDAR_MONTH_JUNE",
    "CALENDAR_MONTH_JULY",      "CALENDAR_MONTH_AUGUST",
    "CALENDAR_MONTH_SEPTEMBER", "CALENDAR_MONTH_OCTOBER",
    "CALENDAR_MONTH_NOVEMBER",  "CALENDAR_MONTH_DECEMBER"};
constexpr std::array<const char*, 7> WEEKDAY_KEYS = {
    "CALENDAR_WEEKDAY_MON", "CALENDAR_WEEKDAY_TUE", "CALENDAR_WEEKDAY_WED",
    "CALENDAR_WEEKDAY_THU", "CALENDAR_WEEKDAY_FRI", "CALENDAR_WEEKDAY_SAT",
    "CALENDAR_WEEKDAY_SUN"};

int monthIndex(int year, int month) { return year * 12 + (month - 1); }

std::uint16_t bit(AgendaKind kind)
{
  return static_cast<std::uint16_t>(
      1U << static_cast<unsigned>(std::to_underlying(kind)));
}

/** Colour of a club event (never the accent: decoration only). */
ImVec4 kindColor(AgendaKind kind)
{
  const Theme::Palette& palette = Theme::palette();
  using enum AgendaKind;
  switch (kind)
  {
    case TransferWindowOpens:
    case TransferDeadline:
      return palette.info;
    case BoardReview:
    case ContractReminder:
      return palette.warning;
    case YouthPreview:
    case YouthIntake:
    case YouthDecisionDeadline:
      return palette.positive;
    default:
      return palette.muted;
  }
}

ImVec4 competitionColor(MatchType type)
{
  const Theme::Palette& palette = Theme::palette();
  switch (type)
  {
    case MatchType::CUP:
      return palette.info;
    case MatchType::FRIENDLY:
      return palette.faint;
    default:
      return palette.text;
  }
}

ImVec4 outcomeColor(int outcome)
{
  const Theme::Palette& palette = Theme::palette();
  if (outcome > 0) return palette.positive;
  if (outcome < 0) return palette.negative;
  return palette.muted;
}
}  // namespace

CalendarScene::CalendarScene(GUIView* parent) : ManagementScene(parent) {}

void CalendarScene::update(float /*deltaTime*/)
{
  // Calendar entries are date-driven and rebuilt when the scene is refreshed.
}

void CalendarScene::refresh()
{
  entries.clear();
  GameController& controller = guiView->getController();
  const auto managed = controller.getManagedTeam();
  if (!managed) return;
  const TeamID club = managed->get().getId();
  today = controller.getCurrentDate();
  season_first = SeasonAgenda::seasonStart(today);

  const auto teamName = [&](TeamID id)
  {
    const auto team = controller.getTeamById(id);
    return team ? team->get().getName() : std::string();
  };
  for (const AgendaEvent& event : controller.getSeasonAgenda())
  {
    Entry entry;
    entry.event = event;
    entry.date = Format::dayMonth(event.date);
    if (event.kind != AgendaKind::Fixture)
    {
      entry.title = LOC(SeasonAgenda::kindKey(event.kind));
      entry.line = entry.title;
      if (event.span_days > 1)
        entry.detail = formatLocalized("CALENDAR_SPAN_DAYS",
                                       {std::to_string(event.span_days)});
      entries.push_back(std::move(entry));
      continue;
    }
    const bool home = event.home_id == club;
    entry.opponent = home ? event.away_id : event.home_id;
    entry.title =
        formatLocalized(home ? "CALENDAR_VS_HOME" : "CALENDAR_VS_AWAY",
                        {teamName(entry.opponent)});
    switch (event.match_type)
    {
      case MatchType::LEAGUE:
      {
        const auto league = controller.getLeagueById(event.competition_id);
        entry.detail = league ? Competitions::leagueName(league->get())
                              : std::string(LOC("CALENDAR_LEAGUE"));
        break;
      }
      case MatchType::CUP:
      {
        const std::string cup = controller.getCupName(event.competition_id);
        entry.detail = cup.empty() ? std::string(LOC("CALENDAR_CUP")) : cup;
        break;
      }
      case MatchType::FRIENDLY:
        entry.detail = LOC("CALENDAR_FRIENDLY");
        break;
      case MatchType::CONTINENTAL:
      {
        const auto* rules = Continental::rules(event.competition_id);
        entry.detail =
            LOC(rules != nullptr ? rules->name_key : "MATCH_TYPE_CONTINENTAL");
        break;
      }
      default:
        entry.detail = LOC("CALENDAR_OTHER_COMPETITION");
        break;
    }
    // League rounds are spread over several days: show the kick-off.
    entry.detail = Format::kickoff(event.kickoff) + "  " + entry.detail;
    if (event.played)
    {
      const int own = home ? event.home_score : event.away_score;
      const int other = home ? event.away_score : event.home_score;
      entry.score = std::format("{}-{}", own, other);
      if (own > other)
        entry.outcome = 1;
      else if (own < other)
        entry.outcome = -1;
      else
        entry.outcome = 0;
    }
    entry.line =
        entry.score.empty() ? entry.title : entry.title + "  " + entry.score;
    entries.push_back(std::move(entry));
  }
  // Keep the shown month when it is still in the season, else show today.
  const int first = monthIndex(season_first.year, season_first.month);
  const int shown = monthIndex(shown_year, shown_month);
  if (shown_month == 0 || shown < first || shown >= first + SEASON_MONTHS)
  {
    showMonth(today.year, today.month);
    selected_day = today.day;
  }
  else
  {
    showMonth(shown_year, shown_month);
  }
}

void CalendarScene::showMonth(int year, int month)
{
  shown_year = year;
  shown_month = month;
  const GameDateValue first(static_cast<uint16_t>(year),
                            static_cast<uint8_t>(month), 1);
  first_weekday = SeasonCalendar::dayOfWeek(first);
  days_in_month = 0;
  for (GameDateValue date = first; date.month == month && days_in_month < 31;
       date = date + 1)
    ++days_in_month;
  days.fill({});
  month_entries.clear();
  for (int day = 1; day <= days_in_month; ++day)
  {
    const GameDateValue date(static_cast<uint16_t>(year),
                             static_cast<uint8_t>(month),
                             static_cast<uint8_t>(day));
    days[static_cast<std::size_t>(day)].international =
        SeasonCalendar::isInternationalBreak(date);
    days[static_cast<std::size_t>(day)].winter =
        SeasonCalendar::isWinterBreak(date);
  }
  for (std::size_t index = 0; index < entries.size(); ++index)
  {
    const AgendaEvent& event = entries[index].event;
    if (event.date.year != year || event.date.month != month) continue;
    month_entries.push_back(index);
    Day& day = days[event.date.day];
    if (event.kind == AgendaKind::Fixture)
      day.fixture = static_cast<int>(index);
    else
      day.kinds |= bit(event.kind);
  }
  month_title = std::format(
      "{} {}", LOC(MONTH_KEYS[static_cast<std::size_t>(month - 1)]), year);
  selected_day = std::clamp(selected_day, 1, days_in_month);
}

bool CalendarScene::isToday(int day) const
{
  return today.year == shown_year && today.month == shown_month &&
         today.day == day;
}

void CalendarScene::renderContent()
{
  UI::pageHeader(LOC("CALENDAR_TITLE"), LOC("CALENDAR_SUBTITLE"));
  if (!guiView->getController().getManagedTeam() || shown_month == 0)
  {
    UI::emptyState(LOC("CALENDAR_NO_CLUB"), nullptr);
    return;
  }
  renderToolbar();
  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const bool twoColumns = available >= TWO_COLUMN_MIN_WIDTH * Theme::scale();
  const float left =
      twoColumns ? std::floor((available - gap) * 0.62f) : available;
  renderGrid(left);
  if (twoColumns) ImGui::SameLine();
  ImGui::BeginGroup();
  const float right = twoColumns ? available - gap - left : available;
  renderDay(right);
  renderAgenda(right);
  ImGui::EndGroup();
}

void CalendarScene::renderToolbar()
{
  const int first = monthIndex(season_first.year, season_first.month);
  const int shown = monthIndex(shown_year, shown_month);
  const auto go = [this](int index)
  {
    showMonth(index / 12, index % 12 + 1);
    selected_day = 1;
  };
  ImGui::BeginDisabled(shown <= first);
  if (UI::secondaryButton(LOC("CALENDAR_PREVIOUS"))) go(shown - 1);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(shown >= first + SEASON_MONTHS - 1);
  if (UI::secondaryButton(LOC("CALENDAR_NEXT"))) go(shown + 1);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(shown_year == today.year && shown_month == today.month);
  if (UI::secondaryButton(LOC("CALENDAR_TODAY")))
  {
    showMonth(today.year, today.month);
    selected_day = today.day;
  }
  ImGui::EndDisabled();
  {
    Theme::ScopedText title(Theme::Text::TITLE);
    const float width = ImGui::CalcTextSize(month_title.c_str()).x;
    UI::sameLineIfFits(width, Theme::Space::L * Theme::scale());
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                         (UI::buttonHeight() - ImGui::GetTextLineHeight()) *
                             0.5f);
    ImGui::TextUnformatted(month_title.c_str());
  }
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
}

void CalendarScene::renderGrid(float width)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  UI::beginAutoHeightCard("calendar_grid", nullptr, width);
  const float inner = ImGui::GetContentRegionAvail().x;
  const float spacing = 3.0f * scale;
  const float cellWidth = std::floor((inner - 6.0f * spacing) / 7.0f);
  const float cellHeight = std::clamp(
      cellWidth * 0.62f, CELL_MIN_HEIGHT * scale, CELL_MAX_HEIGHT * scale);
  ImDrawList* drawList = ImGui::GetWindowDrawList();

  // Weekday header.
  {
    Theme::ScopedText caption(Theme::Text::CAPTION);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    for (int column = 0; column < 7; ++column)
    {
      const char* label = LOC(WEEKDAY_KEYS[static_cast<std::size_t>(column)]);
      UI::drawTextFitted(
          drawList,
          ImVec2(start.x + static_cast<float>(column) * (cellWidth + spacing) +
                     4.0f * scale,
                 start.y),
          Theme::toU32(palette.muted), label, cellWidth - 4.0f * scale);
    }
    ImGui::Dummy(ImVec2(inner, ImGui::GetTextLineHeightWithSpacing()));
  }

  const int rows = (first_weekday + days_in_month + 6) / 7;
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float rounding = 4.0f * scale;
  const float lineHeight = ImGui::GetTextLineHeight();
  for (int slot = 0; slot < rows * 7; ++slot)
  {
    const int day = slot - first_weekday + 1;
    const int row = slot / 7;
    const int column = slot % 7;
    const ImVec2 min(
        origin.x + static_cast<float>(column) * (cellWidth + spacing),
        origin.y + static_cast<float>(row) * (cellHeight + spacing));
    const ImVec2 max(min.x + cellWidth, min.y + cellHeight);
    if (day < 1 || day > days_in_month)
    {
      drawList->AddRectFilled(min, max, Theme::toU32(palette.background, 0.5f),
                              rounding);
      continue;
    }
    const Day& info = days[static_cast<std::size_t>(day)];
    ImGui::SetCursorScreenPos(min);
    ImGui::PushID(day);
    const bool pressed =
        ImGui::InvisibleButton("##day", ImVec2(cellWidth, cellHeight));
    const bool hovered = ImGui::IsItemHovered();
    const bool doubleClicked =
        hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    ImGui::PopID();
    if (pressed) selected_day = day;

    ImVec4 fill = hovered ? palette.raised : palette.surface;
    drawList->AddRectFilled(min, max, Theme::toU32(fill), rounding);
    if (info.international)
      drawList->AddRectFilled(min, max, Theme::toU32(palette.info, 0.10f),
                              rounding);
    else if (info.winter)
      drawList->AddRectFilled(min, max, Theme::toU32(palette.faint, 0.14f),
                              rounding);
    drawList->AddRect(
        min, max,
        Theme::toU32(selected_day == day ? palette.accent : palette.border),
        rounding, 0, selected_day == day ? 2.0f * scale : 1.0f);

    // Day number (today in a filled pill).
    const std::string number = std::to_string(day);
    const ImVec2 numberPos(min.x + 5.0f * scale, min.y + 3.0f * scale);
    if (isToday(day))
    {
      const ImVec2 size = ImGui::CalcTextSize(number.c_str());
      drawList->AddRectFilled(
          ImVec2(numberPos.x - 3.0f * scale, numberPos.y - 1.0f * scale),
          ImVec2(numberPos.x + size.x + 3.0f * scale,
                 numberPos.y + size.y + 1.0f * scale),
          Theme::toU32(palette.accent), 3.0f * scale);
      drawList->AddText(numberPos, Theme::toU32(palette.on_accent),
                        number.c_str());
    }
    else
    {
      drawList->AddText(numberPos, Theme::toU32(palette.muted), number.c_str());
    }

    // Club events as dots in the top-right corner.
    float dotX = max.x - 7.0f * scale;
    for (auto kind = static_cast<int>(
             std::to_underlying(AgendaKind::YouthDecisionDeadline));
         kind >= 0; --kind)
    {
      if ((info.kinds & bit(static_cast<AgendaKind>(kind))) == 0) continue;
      drawList->AddCircleFilled(
          ImVec2(dotX, min.y + 9.0f * scale), 3.5f * scale,
          Theme::toU32(kindColor(static_cast<AgendaKind>(kind))));
      dotX -= 10.0f * scale;
    }

    // Fixture chip along the bottom of the cell.
    if (info.fixture >= 0)
    {
      const Entry& entry = entries[static_cast<std::size_t>(info.fixture)];
      const float chipHeight = lineHeight + 4.0f * scale;
      const ImVec2 chipMin(min.x + 3.0f * scale,
                           max.y - chipHeight - 3.0f * scale);
      const ImVec2 chipMax(max.x - 3.0f * scale, max.y - 3.0f * scale);
      drawList->AddRectFilled(chipMin, chipMax, Theme::toU32(palette.raised),
                              3.0f * scale);
      drawList->AddRectFilled(
          chipMin, ImVec2(chipMin.x + 3.0f * scale, chipMax.y),
          Theme::toU32(competitionColor(entry.event.match_type)), 1.5f * scale);
      float right = chipMax.x - 4.0f * scale;
      if (!entry.score.empty())
      {
        const ImVec2 size = ImGui::CalcTextSize(entry.score.c_str());
        right -= size.x;
        drawList->AddText(ImVec2(right, chipMin.y + 2.0f * scale),
                          Theme::toU32(outcomeColor(entry.outcome)),
                          entry.score.c_str());
        right -= 4.0f * scale;
      }
      const float textX = chipMin.x + 7.0f * scale;
      UI::drawTextFitted(drawList, ImVec2(textX, chipMin.y + 2.0f * scale),
                         Theme::toU32(palette.text), entry.title,
                         std::max(0.0f, right - textX));
      if (doubleClicked)
      {
        if (entry.event.played)
          Navigation::openMatchReport(guiView, entry.event.date,
                                      entry.event.home_id, entry.event.away_id);
        else
          Navigation::openClub(guiView, entry.opponent);
      }
    }

    if (hovered && (info.fixture >= 0 || info.kinds != 0 ||
                    info.international || info.winter))
    {
      ImGui::BeginTooltip();
      for (const std::size_t index : month_entries)
        if (entries[index].event.date.day == day)
          ImGui::TextUnformatted(entries[index].title.c_str());
      if (info.international)
        ImGui::TextColored(palette.muted, "%s",
                           LOC("AGENDA_INTERNATIONAL_BREAK"));
      else if (info.winter)
        ImGui::TextColored(palette.muted, "%s", LOC("AGENDA_WINTER_BREAK"));
      if (info.fixture >= 0)
        ImGui::TextColored(palette.faint, "%s", LOC("CALENDAR_CELL_HINT"));
      ImGui::EndTooltip();
    }
  }
  ImGui::SetCursorScreenPos(origin);
  ImGui::Dummy(ImVec2(
      inner, static_cast<float>(rows) * (cellHeight + spacing) - spacing));

  // Legend.
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
  const auto legend = [&](const ImVec4& color, const char* key, bool first)
  {
    const char* label = LOC(key);
    const float itemWidth = 14.0f * scale + ImGui::CalcTextSize(label).x;
    if (!first) UI::sameLineIfFits(itemWidth, Theme::Space::L * scale);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    drawList->AddCircleFilled(
        ImVec2(at.x + 4.0f * scale, at.y + lineHeight * 0.5f), 4.0f * scale,
        Theme::toU32(color));
    ImGui::SetCursorScreenPos(ImVec2(at.x + 12.0f * scale, at.y));
    ImGui::TextColored(palette.muted, "%s", label);
  };
  {
    Theme::ScopedText small(Theme::Text::SMALL);
    legend(palette.text, "CALENDAR_LEGEND_LEAGUE", true);
    legend(palette.info, "CALENDAR_LEGEND_CUP", false);
    legend(palette.faint, "CALENDAR_LEGEND_FRIENDLY", false);
    legend(kindColor(AgendaKind::TransferDeadline), "CALENDAR_LEGEND_TRANSFERS",
           false);
    legend(kindColor(AgendaKind::BoardReview), "CALENDAR_LEGEND_CLUB", false);
    legend(kindColor(AgendaKind::YouthIntake), "CALENDAR_LEGEND_YOUTH", false);
  }
  UI::endCard();
}

void CalendarScene::renderDay(float width)
{
  const Theme::Palette& palette = Theme::palette();
  const GameDateValue date(static_cast<uint16_t>(shown_year),
                           static_cast<uint8_t>(shown_month),
                           static_cast<uint8_t>(selected_day));
  const std::string title = Format::date(date);
  UI::beginAutoHeightCard("calendar_day", title.c_str(), width);
  const Day& info = days[static_cast<std::size_t>(selected_day)];
  bool any = false;
  for (const std::size_t index : month_entries)
  {
    const Entry& entry = entries[index];
    if (entry.event.date.day != selected_day) continue;
    any = true;
    ImGui::PushID(static_cast<int>(index));
    if (entry.event.kind == AgendaKind::Fixture)
    {
      UI::textFitted(entry.title, ImGui::GetContentRegionAvail().x,
                     palette.text);
      ImGui::TextColored(palette.muted, "%s", entry.detail.c_str());
      if (!entry.score.empty())
      {
        ImGui::SameLine();
        ImGui::TextColored(outcomeColor(entry.outcome), "%s",
                           entry.score.c_str());
      }
      if (entry.event.played)
      {
        if (UI::primaryButton(LOC("CALENDAR_OPEN_REPORT")))
          Navigation::openMatchReport(guiView, entry.event.date,
                                      entry.event.home_id, entry.event.away_id);
      }
      else if (UI::secondaryButton(LOC("CALENDAR_OPEN_OPPONENT")))
      {
        Navigation::openClub(guiView, entry.opponent);
      }
    }
    else
    {
      const ImVec2 at = ImGui::GetCursorScreenPos();
      const float lineHeight = ImGui::GetTextLineHeight();
      ImGui::GetWindowDrawList()->AddCircleFilled(
          ImVec2(at.x + 4.0f * Theme::scale(), at.y + lineHeight * 0.5f),
          4.0f * Theme::scale(), Theme::toU32(kindColor(entry.event.kind)));
      ImGui::SetCursorScreenPos(ImVec2(at.x + 14.0f * Theme::scale(), at.y));
      ImGui::TextUnformatted(entry.title.c_str());
      if (!entry.detail.empty())
      {
        ImGui::SameLine();
        ImGui::TextColored(palette.muted, "%s", entry.detail.c_str());
      }
    }
    ImGui::PopID();
  }
  if (!any && (info.international || info.winter))
  {
    ImGui::TextColored(palette.muted, "%s",
                       LOC(info.international ? "AGENDA_INTERNATIONAL_BREAK"
                                              : "AGENDA_WINTER_BREAK"));
    any = true;
  }
  if (!any) ImGui::TextColored(palette.faint, "%s", LOC("CALENDAR_DAY_EMPTY"));
  UI::endCard();
}

void CalendarScene::renderAgenda(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("calendar_agenda", LOC("CALENDAR_AGENDA"), width);
  if (month_entries.empty())
  {
    UI::emptyState(LOC("CALENDAR_AGENDA_EMPTY"), nullptr);
    UI::endCard();
    return;
  }
  static const std::array<UI::Column, 3> COLUMN_KEYS = {{
      {"CALENDAR_COL_DATE", 64.0f, 0},
      {"CALENDAR_COL_EVENT", 0.0f, 0},
      {"CALENDAR_COL_DETAIL", 150.0f, 1},
  }};
  std::array<UI::Column, 3> columns = COLUMN_KEYS;
  for (UI::Column& column : columns) column.label = LOC(column.label);
  if (const UI::ColumnMask mask =
          UI::fitColumns(columns, ImGui::GetContentRegionAvail().x, 120.0f);
      UI::beginResponsiveTable(
          "agenda", columns, mask,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
  {
    for (const std::size_t index : month_entries)
    {
      const Entry& entry = entries[index];
      ImGui::PushID(static_cast<int>(index));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (const bool isSelected = entry.event.date.day == selected_day;
          ImGui::Selectable(entry.date.c_str(), isSelected,
                            ImGuiSelectableFlags_SpanAllColumns))
        selected_day = entry.event.date.day;
      if (UI::cell(mask, 1))
      {
        const ImVec4 color = entry.event.kind == AgendaKind::Fixture
                                 ? palette.text
                                 : kindColor(entry.event.kind);
        UI::textFitted(entry.line, ImGui::GetContentRegionAvail().x, color);
      }
      if (UI::cell(mask, 2))
        UI::textFitted(entry.detail, ImGui::GetContentRegionAvail().x,
                       palette.muted);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  UI::endCard();
}
