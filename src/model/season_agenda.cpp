// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/season_agenda.h"

#include <algorithm>
#include <array>
#include <utility>

#include "model/calendar.h"
#include "model/match.h"
#include "model/transfer_windows.h"
#include "model/youth_academy.h"

namespace
{
/** Board reviews run on the 1st of each month from September to May. */
constexpr int FIRST_REVIEW_MONTH = 9;
constexpr int LAST_REVIEW_MONTH = 5;
/** Contract expiry reminders go out on these days (month, day). */
constexpr std::array<std::pair<int, int>, 2> CONTRACT_REMINDERS = {
    {{1, 1}, {4, 1}}};

bool sameDay(const GameDateValue& a, int month, int day)
{
  return a.month == month && a.day == day;
}

/** Days from @p from while @p inside holds (at most @p limit). */
template <typename Predicate>
int spanFrom(const GameDateValue& from, Predicate inside, int limit)
{
  int days = 0;
  GameDateValue date = from;
  while (days < limit && inside(date))
  {
    ++days;
    date = date + 1;
  }
  return days;
}
}  // namespace

const char* SeasonAgenda::kindKey(AgendaKind kind)
{
  using enum AgendaKind;
  switch (kind)
  {
    case SeasonStart:
      return "AGENDA_SEASON_START";
    case TransferWindowOpens:
      return "AGENDA_WINDOW_OPENS";
    case TransferDeadline:
      return "AGENDA_TRANSFER_DEADLINE";
    case InternationalBreak:
      return "AGENDA_INTERNATIONAL_BREAK";
    case WinterBreak:
      return "AGENDA_WINTER_BREAK";
    case BoardReview:
      return "AGENDA_BOARD_REVIEW";
    case ContractReminder:
      return "AGENDA_CONTRACT_REMINDER";
    case YouthPreview:
      return "AGENDA_YOUTH_PREVIEW";
    case YouthIntake:
      return "AGENDA_YOUTH_INTAKE";
    case YouthDecisionDeadline:
      return "AGENDA_YOUTH_DEADLINE";
    case Fixture:
      break;
  }
  return "AGENDA_FIXTURE";
}

GameDateValue SeasonAgenda::seasonStart(const GameDateValue& date)
{
  return GameDateValue(SeasonCalendar::seasonStartYear(date),
                       SeasonCalendar::SEASON_START_MONTH, 1);
}

std::vector<AgendaEvent> SeasonAgenda::build(std::uint16_t season_start_year,
                                             const std::vector<Match>& fixtures,
                                             bool board_reviews,
                                             LeagueID league)
{
  std::vector<AgendaEvent> events;
  const GameDateValue first(season_start_year,
                            SeasonCalendar::SEASON_START_MONTH, 1);
  const GameDateValue next_season(
      static_cast<std::uint16_t>(season_start_year + 1),
      SeasonCalendar::SEASON_START_MONTH, 1);
  const auto add =
      [&events](const GameDateValue& date, AgendaKind kind, int span = 1)
  {
    AgendaEvent event;
    event.date = date;
    event.kind = kind;
    event.span_days = span;
    events.push_back(event);
  };
  const GameDateValue youth_deadline =
      GameDateValue(static_cast<std::uint16_t>(season_start_year + 1),
                    YouthModel::INTAKE_MONTH, YouthModel::INTAKE_DAY) +
      static_cast<size_t>(YouthModel::DECISION_DAYS);

  add(first, AgendaKind::SeasonStart);
  const auto window_on = [league](const GameDateValue& date)
  { return TransferWindows::isOpen(league, date); };
  bool window_open = window_on(first - 1);
  bool international = SeasonCalendar::isInternationalBreak(first - 1);
  bool winter = SeasonCalendar::isWinterBreak(first - 1);
  for (GameDateValue date = first; date < next_season; date = date + 1)
  {
    const bool open = window_on(date);
    if (open && !window_open) add(date, AgendaKind::TransferWindowOpens);
    if (TransferWindows::isDeadlineDay(league, date))
      add(date, AgendaKind::TransferDeadline);
    window_open = open;

    const bool on_break = SeasonCalendar::isInternationalBreak(date);
    if (on_break && !international)
      add(date, AgendaKind::InternationalBreak,
          spanFrom(date, SeasonCalendar::isInternationalBreak, 31));
    international = on_break;

    const bool resting = SeasonCalendar::isWinterBreak(date);
    if (resting && !winter)
      add(date, AgendaKind::WinterBreak,
          spanFrom(date, SeasonCalendar::isWinterBreak, 31));
    winter = resting;

    if (board_reviews && date.day == 1 &&
        (date.month >= FIRST_REVIEW_MONTH || date.month <= LAST_REVIEW_MONTH))
      add(date, AgendaKind::BoardReview);
    for (const auto& [month, day] : CONTRACT_REMINDERS)
      if (sameDay(date, month, day)) add(date, AgendaKind::ContractReminder);
    if (sameDay(date, YouthModel::PREVIEW_MONTH, YouthModel::PREVIEW_DAY))
      add(date, AgendaKind::YouthPreview);
    if (sameDay(date, YouthModel::INTAKE_MONTH, YouthModel::INTAKE_DAY))
      add(date, AgendaKind::YouthIntake);
    if (date == youth_deadline) add(date, AgendaKind::YouthDecisionDeadline);
  }

  for (const Match& match : fixtures)
  {
    if (match.getDate() < first || !(match.getDate() < next_season)) continue;
    AgendaEvent event;
    event.date = match.getDate();
    event.kind = AgendaKind::Fixture;
    event.home_id = match.getHomeTeamId();
    event.away_id = match.getAwayTeamId();
    event.match_type = match.getMatchType();
    event.competition_id = match.getCompetitionId();
    event.played = match.isPlayed();
    event.home_score = match.getHomeScore();
    event.away_score = match.getAwayScore();
    event.kickoff = match.getKickoff();
    events.push_back(event);
  }
  std::ranges::stable_sort(events,
                           [](const AgendaEvent& a, const AgendaEvent& b)
                           {
                             if (a.date < b.date) return true;
                             if (b.date < a.date) return false;
                             return a.kind < b.kind;
                           });
  return events;
}
