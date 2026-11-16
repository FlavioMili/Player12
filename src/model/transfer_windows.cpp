// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/transfer_windows.h"

#include <algorithm>

#include "model/world_rng.h"

namespace
{
using TransferWindows::CountryRules;
using TransferWindows::FREE_AGENTS_ANY_TIME;
using TransferWindows::MonthDay;
using TransferWindows::Period;

/**
 * Windows of the 2026-27 season (summer 2026 and January 2027, or January
 * 2026 where the 2027 dates were not out yet). Closing days move by a day or
 * two between years when they fall on a weekend; one representative date is
 * kept. Brazil, Argentina and the USA play calendar-year seasons: their
 * mid-season window maps to the mid-year one, their pre-season window to the
 * start-of-year one.
 *
 * - England: 15 Jun - 1 Sep, 1 Jan - 2 Feb; free agents any time.
 *   premierleague.com/en/news/4664145 ; espn.com/soccer/story/_/id/46939238
 * - Italy: 29 Jun - 1 Sep, 2 Jan - 2 Feb.
 *   onefootball.com/en/news/serie-a-confirms-key-transfer-window-dates-for-
 *   the-20262027-season-42784312 ; ESPN (above)
 * - Spain: 1 Jul - 1 Sep, 2 Jan - 2 Feb.
 *   givemesport.com/transfer-window-dates ; ESPN (above)
 * - Germany: 1 Jul - 31 Aug, 1 Jan - 2 Feb. ESPN and givemesport (above)
 * - France: 15 Jun - 1 Sep, 1 Jan - 2 Feb. ESPN and givemesport (above)
 * - Portugal: 1 Jul - 4 Sep (Liga Portugal communique no. 1, 2026-27),
 *   1 Jan - 2 Feb. ligaportugal.pt/news/26377
 * - Brazil: 20 Jul - 11 Sep, 5 Jan - 3 Mar (CBF).
 *   olympics.com/pt/noticias/janela-transferencias-2026-abre-quando-fecha-
 *   como-funciona
 * - Argentina: 9 Jul - 31 Jul (AFA, extended from 21 Jul), 2 Jan - 27 Jan.
 *   elgrafico.com.ar/articulo/primera-division/101570
 * - USA (MLS): secondary window 13 Jul - 2 Sep, primary 26 Jan - 26 Mar.
 *   mlssoccer.com/news/mls-transfer-windows-key-dates-for-the-2026-season
 * - Mexico (Liga MX): 2 Jul - 11 Sep, 1 Jan - 9 Feb; up to 6 Mar for free
 *   agents (Clausura 2026 rule, about 25 days after the close).
 *   tvazteca.com/aztecadeportes/fecha-mercado-fichajes-liga-mx-apertura-2026
 * - Russia (RPL): 19 Jun - 11 Sep, 23 Jan - 19 Feb; free agents for 14 more
 *   days after each close. sports.ru/football/1117366665-zakrylos-letnee-
 *   transfernoe-okno-v-rpl.html
 *
 * Elsewhere players out of contract can be registered outside the windows
 * (FIFA Regulations on the Status and Transfer of Players, art. 6.1).
 */
constexpr std::array<CountryRules, 11> COUNTRIES = {{
    // England
    {{3, 14}, {{6, 15}, {9, 1}}, {{1, 1}, {2, 2}}, FREE_AGENTS_ANY_TIME},
    // Italy
    {{1, 6}, {{6, 29}, {9, 1}}, {{1, 2}, {2, 2}}, FREE_AGENTS_ANY_TIME},
    // Spain
    {{2, 13}, {{7, 1}, {9, 1}}, {{1, 2}, {2, 2}}, FREE_AGENTS_ANY_TIME},
    // Germany
    {{4, 15}, {{7, 1}, {8, 31}}, {{1, 1}, {2, 2}}, FREE_AGENTS_ANY_TIME},
    // France
    {{5, 16}, {{6, 15}, {9, 1}}, {{1, 1}, {2, 2}}, FREE_AGENTS_ANY_TIME},
    // Portugal
    {{12, 22}, {{7, 1}, {9, 4}}, {{1, 1}, {2, 2}}, FREE_AGENTS_ANY_TIME},
    // Brazil
    {{11, 21}, {{7, 20}, {9, 11}}, {{1, 5}, {3, 3}}, FREE_AGENTS_ANY_TIME},
    // Argentina
    {{10, 20}, {{7, 9}, {7, 31}}, {{1, 2}, {1, 27}}, FREE_AGENTS_ANY_TIME},
    // United States
    {{7, 17}, {{7, 13}, {9, 2}}, {{1, 26}, {3, 26}}, FREE_AGENTS_ANY_TIME},
    // Mexico
    {{9, 19}, {{7, 2}, {9, 11}}, {{1, 1}, {2, 9}}, 25},
    // Russia
    {{8, 18}, {{6, 19}, {9, 11}}, {{1, 23}, {2, 19}}, 14},
}};

/** Leagues added by a data pack: the most common European calendar. */
constexpr CountryRules DEFAULT_RULES = {
    {0, 0}, {{7, 1}, {9, 1}}, {{1, 1}, {2, 2}}, FREE_AGENTS_ANY_TIME};

constexpr int key(MonthDay day) { return day.month * 100 + day.day; }

constexpr bool validPeriod(const Period& period)
{
  return period.opens.month >= 1 && period.closes.month <= 12 &&
         key(period.opens) <= key(period.closes);
}

constexpr bool validTable()
{
  return std::ranges::all_of(COUNTRIES,
                             [](const CountryRules& rules)
                             {
                               return validPeriod(rules.mid_year) &&
                                      validPeriod(rules.start_of_year) &&
                                      key(rules.start_of_year.closes) <
                                          key(rules.mid_year.opens);
                             });
}
static_assert(validTable(),
              "windows must lie within one calendar year, the start-of-year "
              "window before the mid-year one");

int key(const GameDateValue& date) { return date.month * 100 + date.day; }

bool inside(const Period& period, const GameDateValue& date)
{
  const int today = key(date);
  return today >= key(period.opens) && today <= key(period.closes);
}

GameDateValue on(std::uint16_t year, MonthDay day)
{
  return GameDateValue(year, day.month, day.day);
}
}  // namespace

std::span<const CountryRules> TransferWindows::countries() { return COUNTRIES; }

const CountryRules& TransferWindows::rulesFor(LeagueID league)
{
  const auto found = std::ranges::find_if(
      COUNTRIES, [league](const CountryRules& rules)
      { return std::ranges::contains(rules.leagues, league); });
  return found != COUNTRIES.end() ? *found : DEFAULT_RULES;
}

bool TransferWindows::isOpen(LeagueID league, const GameDateValue& date)
{
  const CountryRules& rules = rulesFor(league);
  return inside(rules.mid_year, date) || inside(rules.start_of_year, date);
}

bool TransferWindows::isOpenAnywhere(const GameDateValue& date)
{
  return std::ranges::any_of(COUNTRIES,
                             [&date](const CountryRules& rules)
                             {
                               return inside(rules.mid_year, date) ||
                                      inside(rules.start_of_year, date);
                             }) ||
         inside(DEFAULT_RULES.mid_year, date) ||
         inside(DEFAULT_RULES.start_of_year, date);
}

bool TransferWindows::isStartOfYearWindow(LeagueID league,
                                          const GameDateValue& date)
{
  return inside(rulesFor(league).start_of_year, date);
}

std::optional<GameDateValue> TransferWindows::windowEnd(
    LeagueID league, const GameDateValue& date)
{
  const CountryRules& rules = rulesFor(league);
  for (const Period* period : {&rules.mid_year, &rules.start_of_year})
    if (inside(*period, date)) return on(date.year, period->closes);
  return std::nullopt;
}

bool TransferWindows::isDeadlineDay(LeagueID league, const GameDateValue& date)
{
  const std::optional<GameDateValue> end = windowEnd(league, date);
  return end && *end == date;
}

GameDateValue TransferWindows::nextOpening(LeagueID league,
                                           const GameDateValue& date)
{
  if (isOpen(league, date)) return date;
  const CountryRules& rules = rulesFor(league);
  const int today = key(date);
  // The start-of-year window comes first in the calendar year.
  if (today < key(rules.start_of_year.opens))
    return on(date.year, rules.start_of_year.opens);
  if (today < key(rules.mid_year.opens))
    return on(date.year, rules.mid_year.opens);
  return on(static_cast<std::uint16_t>(date.year + 1),
            rules.start_of_year.opens);
}

bool TransferWindows::canSignFreeAgent(LeagueID league,
                                       const GameDateValue& date)
{
  const CountryRules& rules = rulesFor(league);
  if (rules.free_agent_days == FREE_AGENTS_ANY_TIME || isOpen(league, date))
    return true;
  // Days since the latest close before today.
  const int today = key(date);
  GameDateValue closed =
      on(static_cast<std::uint16_t>(date.year - 1), rules.mid_year.closes);
  if (today > key(rules.mid_year.closes))
    closed = on(date.year, rules.mid_year.closes);
  else if (today > key(rules.start_of_year.closes))
    closed = on(date.year, rules.start_of_year.closes);
  return dayOrdinal(date) - dayOrdinal(closed) <= rules.free_agent_days;
}
