// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/calendar.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <climits>
#include <fstream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "database/gamedata.h"
#include "global/global.h"
#include "global/logger.h"
#include "global/paths.h"
#include "model/competition.h"
#include "model/league.h"
#include "model/world_tuning.h"

namespace
{
// Proleptic Gregorian day numbers (days since 1970-01-01), after H. Hinnant.
int toDayNumber(int year, int month, int day)
{
  year -= month <= 2 ? 1 : 0;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const int year_of_era = year - era * 400;
  const int day_of_year =
      (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  const int day_of_era =
      year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return era * 146'097 + day_of_era - 719'468;
}

int toDayNumber(const GameDateValue& date)
{
  return toDayNumber(date.year, date.month, date.day);
}

GameDateValue fromDayNumber(int day_number)
{
  day_number += 719'468;
  const int era =
      (day_number >= 0 ? day_number : day_number - 146'096) / 146'097;
  const int day_of_era = day_number - era * 146'097;
  const int year_of_era = (day_of_era - day_of_era / 1'460 +
                           day_of_era / 36'524 - day_of_era / 146'096) /
                          365;
  const int day_of_year =
      day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  const int month_index = (5 * day_of_year + 2) / 153;
  const int day = day_of_year - (153 * month_index + 2) / 5 + 1;
  const int month = month_index < 10 ? month_index + 3 : month_index - 9;
  const int year = year_of_era + era * 400 + (month <= 2 ? 1 : 0);
  return GameDateValue(static_cast<uint16_t>(year), static_cast<uint8_t>(month),
                       static_cast<uint8_t>(day));
}

int weekdayOf(int day_number)
{
  // 1970-01-01 was a Thursday (3 when Monday is 0).
  return ((day_number + 3) % 7 + 7) % 7;
}

int firstWeekdayOnOrAfter(int day_number, int weekday)
{
  return day_number + (weekday - weekdayOf(day_number) + 7) % 7;
}

int lastWeekdayOnOrBefore(int day_number, int weekday)
{
  return day_number - (weekdayOf(day_number) - weekday + 7) % 7;
}

int nthSaturday(int year, int month, int nth)
{
  return firstWeekdayOnOrAfter(toDayNumber(year, month, 1),
                               SeasonCalendar::SATURDAY) +
         7 * (nth - 1);
}

struct MonthDay
{
  int month;
  int day;
  int year_offset;
};

// Preferred cup midweeks, spread from late August to late April.
constexpr std::array<MonthDay, 8> CUP_MIDWEEK_TARGETS = {{{8, 27, 0},
                                                          {9, 24, 0},
                                                          {10, 29, 0},
                                                          {12, 3, 0},
                                                          {1, 14, 1},
                                                          {2, 11, 1},
                                                          {3, 4, 1},
                                                          {4, 22, 1}}};

// International windows, anchored on the nth Saturday of a month. Players
// report on the Monday five days before it; a window ends on a Tuesday.
// From 2026 September and October form one 16-day window with four
// matches, and November moves to the second Saturday (FIFA 2026-2030).
struct WindowRule
{
  int month;
  int nth_saturday;
  int year_offset;
  int days_after;  // Last day of duty, counted from the Saturday.
  std::array<int, 4> match_offsets;
  uint8_t matches;
  bool summer;
  uint16_t first_season;
  uint16_t last_season;
};
constexpr uint16_t DOUBLE_WINDOW_SEASON = 2026;
constexpr uint16_t NO_LAST_SEASON = 0xFFFF;
constexpr int WINDOW_DAYS_BEFORE_SATURDAY = 5;
constexpr std::array<WindowRule, 7> INTERNATIONAL_WINDOWS = {{
    {9, 2, 0, 3, {-1, 2, 0, 0}, 2, false, 0, DOUBLE_WINDOW_SEASON - 1},
    {10, 2, 0, 3, {-1, 2, 0, 0}, 2, false, 0, DOUBLE_WINDOW_SEASON - 1},
    {11, 3, 0, 3, {-1, 2, 0, 0}, 2, false, 0, DOUBLE_WINDOW_SEASON - 1},
    {9,
     4,
     0,
     10,
     {-1, 2, 6, 9},
     4,
     false,
     DOUBLE_WINDOW_SEASON,
     NO_LAST_SEASON},
    {11,
     2,
     0,
     3,
     {-1, 2, 0, 0},
     2,
     false,
     DOUBLE_WINDOW_SEASON,
     NO_LAST_SEASON},
    {3, 4, 1, 3, {-1, 2, 0, 0}, 2, false, 0, NO_LAST_SEASON},
    // Always three weeks after the last league Saturday (24 May at the
    // latest), so it never overlaps the domestic season or the finals.
    {6, 2, 1, 3, {-1, 2, 0, 0}, 2, true, 0, NO_LAST_SEASON},
}};

// Continental club weeks (Tuesday targets): eight league-phase matchdays,
// then two legs each of the play-off, round of 16, quarter- and semi-finals.
constexpr std::array<MonthDay, SeasonCalendar::CONTINENTAL_WEEKS>
    CONTINENTAL_TARGETS = {{{9, 16, 0},
                            {9, 30, 0},
                            {10, 21, 0},
                            {11, 4, 0},
                            {11, 25, 0},
                            {12, 9, 0},
                            {1, 20, 1},
                            {1, 27, 1},
                            {2, 10, 1},
                            {2, 17, 1},
                            {3, 3, 1},
                            {3, 10, 1},
                            {4, 7, 1},
                            {4, 14, 1},
                            {4, 28, 1},
                            {5, 5, 1}}};
constexpr int CONTINENTAL_DAYS_PER_WEEK = 3;  // Tuesday to Thursday.

std::vector<int> continentalWeekDays(uint16_t season_year)
{
  std::vector<int> weeks;
  weeks.reserve(CONTINENTAL_TARGETS.size());
  const auto blocked = [](int tuesday)
  {
    for (int offset = 0; offset < CONTINENTAL_DAYS_PER_WEEK; ++offset)
      if (SeasonCalendar::isBlackout(fromDayNumber(tuesday + offset)))
        return true;
    return false;
  };
  for (const MonthDay& target : CONTINENTAL_TARGETS)
  {
    int day = firstWeekdayOnOrAfter(
        toDayNumber(season_year + target.year_offset, target.month, target.day),
        SeasonCalendar::TUESDAY);
    while (blocked(day) || (!weeks.empty() && day <= weeks.back())) day += 7;
    weeks.push_back(day);
  }
  return weeks;
}

bool inContinentalWeek(int day)
{
  const std::vector<int> weeks =
      continentalWeekDays(SeasonCalendar::seasonStartYear(fromDayNumber(day)));
  return std::ranges::any_of(
      weeks, [day](int tuesday)
      { return day >= tuesday && day < tuesday + CONTINENTAL_DAYS_PER_WEEK; });
}

std::vector<int> cupMidweekCandidates(uint16_t season_year)
{
  std::vector<int> days;
  days.reserve(CUP_MIDWEEK_TARGETS.size());
  for (const MonthDay& target : CUP_MIDWEEK_TARGETS)
  {
    int day = firstWeekdayOnOrAfter(
        toDayNumber(season_year + target.year_offset, target.month, target.day),
        SeasonCalendar::WEDNESDAY);
    while (SeasonCalendar::isBlackout(fromDayNumber(day)) ||
           inContinentalWeek(day))
      day += 7;
    days.push_back(day);
  }
  return days;
}

/** Picks @p count evenly spread elements of @p candidates (count <= size). */
std::vector<int> spreadPick(const std::vector<int>& candidates, size_t count)
{
  std::vector<int> picked;
  if (count == 0 || candidates.empty()) return picked;
  picked.reserve(count);
  for (size_t i = 0; i < count; ++i)
  {
    const size_t index = ((2 * i + 1) * candidates.size()) / (2 * count);
    picked.push_back(candidates[std::min(index, candidates.size() - 1)]);
  }
  return picked;
}
}  // namespace

// ---------------- SeasonCalendar ----------------

uint8_t SeasonCalendar::dayOfWeek(const GameDateValue& date)
{
  return static_cast<uint8_t>(weekdayOf(toDayNumber(date)));
}

GameDateValue SeasonCalendar::addDays(const GameDateValue& date, int days)
{
  return fromDayNumber(toDayNumber(date) + days);
}

uint16_t SeasonCalendar::seasonStartYear(const GameDateValue& date)
{
  return date.month >= SEASON_START_MONTH
             ? date.year
             : static_cast<uint16_t>(date.year - 1);
}

GameDateValue SeasonCalendar::leagueStart(uint16_t season_year)
{
  return fromDayNumber(
      firstWeekdayOnOrAfter(toDayNumber(season_year, 8, 15), SATURDAY));
}

GameDateValue SeasonCalendar::leagueEnd(uint16_t season_year)
{
  return fromDayNumber(
      lastWeekdayOnOrBefore(toDayNumber(season_year + 1, 5, 24), SATURDAY));
}

std::vector<SeasonCalendar::InternationalWindow>
SeasonCalendar::internationalWindows(uint16_t season_year)
{
  std::vector<InternationalWindow> windows;
  for (const WindowRule& rule : INTERNATIONAL_WINDOWS)
  {
    if (season_year < rule.first_season || season_year > rule.last_season)
      continue;
    const int saturday = nthSaturday(season_year + rule.year_offset, rule.month,
                                     rule.nth_saturday);
    InternationalWindow window;
    window.start = fromDayNumber(saturday - WINDOW_DAYS_BEFORE_SATURDAY);
    window.end = fromDayNumber(saturday + rule.days_after);
    window.summer = rule.summer;
    for (uint8_t match = 0; match < rule.matches; ++match)
      window.match_days.push_back(
          fromDayNumber(saturday + rule.match_offsets[match]));
    windows.push_back(std::move(window));
  }
  std::ranges::sort(windows, [](const InternationalWindow& left,
                                const InternationalWindow& right)
                    { return left.start < right.start; });
  return windows;
}

bool SeasonCalendar::isInternationalBreak(const GameDateValue& date)
{
  const int season_year = seasonStartYear(date);
  const int day = toDayNumber(date);
  return std::ranges::any_of(
      INTERNATIONAL_WINDOWS,
      [&](const WindowRule& rule)
      {
        if (season_year < rule.first_season || season_year > rule.last_season)
          return false;
        const int saturday = nthSaturday(season_year + rule.year_offset,
                                         rule.month, rule.nth_saturday);
        return day >= saturday - WINDOW_DAYS_BEFORE_SATURDAY &&
               day <= saturday + rule.days_after;
      });
}

std::vector<GameDateValue> SeasonCalendar::continentalWeeks(
    uint16_t season_year)
{
  std::vector<GameDateValue> weeks;
  for (const int day : continentalWeekDays(season_year))
    weeks.push_back(fromDayNumber(day));
  return weeks;
}

bool SeasonCalendar::isContinentalWeek(const GameDateValue& date)
{
  return inContinentalWeek(toDayNumber(date));
}

bool SeasonCalendar::isWinterBreak(const GameDateValue& date)
{
  constexpr uint8_t BREAK_START_DAY = 22;  // 22 December
  constexpr uint8_t BREAK_END_DAY = 8;     // 8 January
  return (date.month == 12 && date.day >= BREAK_START_DAY) ||
         (date.month == 1 && date.day <= BREAK_END_DAY);
}

bool SeasonCalendar::isBlackout(const GameDateValue& date)
{
  return isWinterBreak(date) || isInternationalBreak(date);
}

std::vector<GameDateValue> SeasonCalendar::cupRoundDates(uint16_t season_year,
                                                         size_t rounds)
{
  std::vector<GameDateValue> dates;
  if (rounds == 0) return dates;
  rounds = std::min(rounds, MAX_CUP_ROUNDS + 1);
  const std::vector<int> midweeks = cupMidweekCandidates(season_year);
  const size_t early_rounds = rounds - 1;
  dates.reserve(rounds);
  if (early_rounds == 1)
  {
    dates.push_back(fromDayNumber(midweeks.back()));
  }
  else if (early_rounds > 1)
  {
    const size_t last = midweeks.size() - 1;
    for (size_t i = 0; i < early_rounds; ++i)
    {
      const size_t index =
          (i * last + (early_rounds - 1) / 2) / (early_rounds - 1);
      dates.push_back(fromDayNumber(midweeks[std::min(index, last)]));
    }
  }
  dates.push_back(addDays(leagueEnd(season_year), 7));
  return dates;
}

std::vector<GameDateValue> SeasonCalendar::leagueRoundDates(
    uint16_t season_year, size_t rounds, uint8_t variant)
{
  const int first = toDayNumber(leagueStart(season_year));
  const int last = toDayNumber(leagueEnd(season_year));

  std::vector<int> weekends;
  for (int day = first; day <= last; day += 7)
  {
    if (!isBlackout(fromDayNumber(day)) && !isBlackout(fromDayNumber(day + 1)))
      weekends.push_back(day);
  }

  std::vector<int> picked;
  if (rounds <= weekends.size())
  {
    picked = spreadPick(weekends, rounds);
  }
  else
  {
    const std::vector<int> cup_days = cupMidweekCandidates(season_year);
    std::vector<int> midweeks;
    for (int day = first + 4; day < last; day += 7)
    {
      if (!isBlackout(fromDayNumber(day)) &&
          !std::ranges::contains(cup_days, day) && !inContinentalWeek(day))
        midweeks.push_back(day);
    }
    const size_t needed = rounds - weekends.size();
    picked = weekends;
    std::vector<int> pool = midweeks;
    if (variant > 0)
    {
      // Weeks the top divisions leave free first, then up to two weekends
      // after the top flight has finished; shared weeks only as a last
      // resort.
      constexpr int LATE_WEEKENDS = 2;
      const std::vector<int> top_flight =
          spreadPick(midweeks, std::min(needed, midweeks.size()));
      std::erase_if(pool, [&top_flight](int day)
                    { return std::ranges::contains(top_flight, day); });
      for (int week = 1; week <= LATE_WEEKENDS && pool.size() < needed; ++week)
      {
        const int saturday = last + 7 * week;
        if (!isBlackout(fromDayNumber(saturday)) &&
            !isBlackout(fromDayNumber(saturday + 1)))
          pool.push_back(saturday);
      }
      if (pool.size() < needed)
      {
        const std::vector<int> shared =
            spreadPick(top_flight, needed - pool.size());
        pool.insert(pool.end(), shared.begin(), shared.end());
      }
      std::ranges::sort(pool);
    }
    const std::vector<int> extra =
        spreadPick(pool, std::min(needed, pool.size()));
    picked.insert(picked.end(), extra.begin(), extra.end());
    // Oversized leagues spill into June, skipping the cup final weekend.
    for (int day = last + 14; picked.size() < rounds; day += 7)
      if (!std::ranges::contains(picked, day)) picked.push_back(day);
    std::ranges::sort(picked);
  }

  std::vector<GameDateValue> dates;
  dates.reserve(picked.size());
  for (int day : picked) dates.push_back(fromDayNumber(day));
  return dates;
}

std::vector<GameDateValue> SeasonCalendar::friendlyDates(
    const GameDateValue& from, size_t count)
{
  std::vector<GameDateValue> dates;
  const int latest = toDayNumber(leagueStart(seasonStartYear(from))) - 7;
  for (int day = firstWeekdayOnOrAfter(toDayNumber(from) + 7, SATURDAY);
       day <= latest && dates.size() < count; day += 7)
  {
    dates.push_back(fromDayNumber(day));
  }
  return dates;
}

int SeasonCalendar::restDays(MatchType earlier, MatchType later)
{
  using enum MatchType;
  return earlier == LEAGUE && later != LEAGUE && later != FRIENDLY
             ? MIN_REST_BEFORE_TIE
             : MIN_REST_DAYS;
}

GameDateValue SeasonCalendar::nextFreeMidweek(const GameDateValue& date)
{
  int day = firstWeekdayOnOrAfter(toDayNumber(date) + 1, WEDNESDAY);
  while (isBlackout(fromDayNumber(day)) || inContinentalWeek(day)) day += 7;
  return fromDayNumber(day);
}

// ---------------- Round days ----------------

namespace
{
using json = nlohmann::json;

constexpr uint32_t ROUND_DAY_SALT = 3;
constexpr int NEVER = INT_MIN / 2;
constexpr uint16_t minutesOf(int hour, int minute)
{
  return static_cast<uint16_t>(hour * 60 + minute);
}

/** One day of a league round: its share of the matches and kick-offs. */
struct DaySlot
{
  uint8_t weekday = 0;
  uint16_t weight = 0;
  std::vector<uint16_t> kickoffs;  // Minutes after midnight, ascending.
};

/** How a league spreads a weekend round and a midweek round. */
struct RoundPattern
{
  std::vector<DaySlot> weekend;
  std::vector<DaySlot> midweek;
};

constexpr std::array<uint8_t, 4> WEEKEND_DAYS = {
    SeasonCalendar::FRIDAY, SeasonCalendar::SATURDAY, SeasonCalendar::SUNDAY,
    SeasonCalendar::MONDAY};
constexpr std::array<uint8_t, 3> MIDWEEK_DAYS = {SeasonCalendar::TUESDAY,
                                                 SeasonCalendar::WEDNESDAY,
                                                 SeasonCalendar::THURSDAY};

/** Days from the round's anchor (Saturday or Wednesday) to @p weekday. */
int anchorOffset(uint8_t weekday, bool weekend)
{
  if (!weekend) return weekday - SeasonCalendar::WEDNESDAY;
  return weekday == SeasonCalendar::MONDAY ? 2
                                           : weekday - SeasonCalendar::SATURDAY;
}

std::optional<uint8_t> parseWeekday(std::string_view name)
{
  constexpr std::array<std::string_view, 7> NAMES = {"mon", "tue", "wed", "thu",
                                                     "fri", "sat", "sun"};
  for (size_t day = 0; day < NAMES.size(); ++day)
    if (NAMES[day] == name) return static_cast<uint8_t>(day);
  return std::nullopt;
}

/** "HH:MM" (24-hour clock) to minutes after midnight. */
std::optional<uint16_t> parseKickoff(std::string_view text)
{
  if (text.size() != 5 || text[2] != ':') return std::nullopt;
  int hour = 0;
  int minute = 0;
  const auto [hour_end, hour_error] =
      std::from_chars(text.data(), text.data() + 2, hour);
  const auto [minute_end, minute_error] =
      std::from_chars(text.data() + 3, text.data() + 5, minute);
  if (hour_error != std::errc{} || hour_end != text.data() + 2 ||
      minute_error != std::errc{} || minute_end != text.data() + 5 ||
      hour > 23 || minute > 59)
    return std::nullopt;
  return minutesOf(hour, minute);
}

std::vector<DaySlot> parseSlots(const json& list,
                                std::span<const uint8_t> allowed)
{
  constexpr uint64_t MAX_WEIGHT = 1000;
  std::vector<DaySlot> slots;
  if (!list.is_array()) return slots;
  for (const json& item : list)
  {
    if (!item.is_object()) continue;
    const auto day = item.find("day");
    const auto matches = item.find("matches");
    if (day == item.end() || !day->is_string() || matches == item.end() ||
        !matches->is_number_unsigned())
      continue;
    const auto weekday = parseWeekday(day->get<std::string>());
    if (!weekday || !std::ranges::contains(allowed, *weekday) ||
        std::ranges::contains(slots, *weekday, &DaySlot::weekday))
      continue;
    DaySlot slot;
    slot.weekday = *weekday;
    slot.weight =
        static_cast<uint16_t>(std::min(matches->get<uint64_t>(), MAX_WEIGHT));
    if (const auto kickoffs = item.find("kickoffs");
        kickoffs != item.end() && kickoffs->is_array())
    {
      for (const json& time : *kickoffs)
        if (time.is_string())
          if (const auto minutes = parseKickoff(time.get<std::string>());
              minutes.has_value())
            slot.kickoffs.push_back(*minutes);
    }
    std::ranges::sort(slot.kickoffs);
    slots.push_back(std::move(slot));
  }
  return slots;
}

/** Day patterns of the data pack (round_days in leagues.json) by league. */
std::unordered_map<LeagueID, RoundPattern> loadRoundPatterns()
{
  std::unordered_map<LeagueID, RoundPattern> patterns;
  std::ifstream file(AssetPaths::leagues());
  if (!file) return patterns;
  const json data = json::parse(file, nullptr, false);
  if (!data.is_array()) return patterns;
  for (const json& league : data)
  {
    if (!league.is_object()) continue;
    const auto id = league.find("id");
    const auto days = league.find("round_days");
    if (id == league.end() || !id->is_number_unsigned() ||
        days == league.end() || !days->is_object())
      continue;
    RoundPattern pattern;
    if (const auto weekend = days->find("weekend"); weekend != days->end())
      pattern.weekend = parseSlots(*weekend, WEEKEND_DAYS);
    if (const auto midweek = days->find("midweek"); midweek != days->end())
      pattern.midweek = parseSlots(*midweek, MIDWEEK_DAYS);
    patterns.try_emplace(static_cast<LeagueID>(id->get<unsigned>()),
                         std::move(pattern));
  }
  return patterns;
}

/** Leagues without a pattern: top flights play mostly at the weekend, lower
 * divisions mostly on Friday and Monday. */
RoundPattern defaultPattern(uint8_t tier)
{
  constexpr uint16_t LUNCH = minutesOf(12, 30);
  constexpr uint16_t AFTERNOON = minutesOf(15, 0);
  constexpr uint16_t EARLY_EVENING = minutesOf(18, 0);
  constexpr uint16_t EVENING = minutesOf(20, 45);
  RoundPattern pattern;
  if (tier <= 1)
    pattern.weekend = {
        {SeasonCalendar::FRIDAY, 1, {EVENING}},
        {SeasonCalendar::SATURDAY, 4, {AFTERNOON, EARLY_EVENING, EVENING}},
        {SeasonCalendar::SUNDAY, 4, {LUNCH, AFTERNOON, EARLY_EVENING, EVENING}},
        {SeasonCalendar::MONDAY, 1, {EVENING}}};
  else
    pattern.weekend = {{SeasonCalendar::FRIDAY, 4, {EVENING}},
                       {SeasonCalendar::SATURDAY, 1, {AFTERNOON}},
                       {SeasonCalendar::SUNDAY, 1, {AFTERNOON}},
                       {SeasonCalendar::MONDAY, 4, {EVENING}}};
  pattern.midweek = {{SeasonCalendar::TUESDAY, 1, {EARLY_EVENING, EVENING}},
                     {SeasonCalendar::WEDNESDAY, 1, {EARLY_EVENING, EVENING}}};
  return pattern;
}

/** A day of one league round and the matches it receives. */
struct RoundDay
{
  int day = 0;
  uint8_t weekday = 0;
  uint16_t weight = 0;
  bool reserve = false;  // Not in the pattern: only for crowded windows.
  std::vector<uint16_t> kickoffs;
  size_t quota = 0;
  std::vector<size_t> matches;  // Indices into the round's pairs.
};

/**
 * The days a round starting from @p anchor uses: those of the league's
 * pattern that are @p usable. The matches meant for an unusable day go to
 * the remaining day with the fewest (a free weekend or midweek day when
 * there is one), so the round keeps its spread. Other usable days of the
 * weekend (Friday-Monday) or midweek (Tuesday-Thursday) stay as reserve.
 */
template <typename Usable>
std::vector<RoundDay> roundDays(int anchor, const RoundPattern& pattern,
                                const Usable& usable)
{
  const bool weekend = weekdayOf(anchor) == SeasonCalendar::SATURDAY;
  const std::vector<DaySlot>& slots =
      weekend ? pattern.weekend : pattern.midweek;
  std::vector<RoundDay> days;
  const auto add = [&](uint8_t weekday)
  {
    RoundDay day;
    day.day = anchor + anchorOffset(weekday, weekend);
    day.weekday = weekday;
    if (const auto slot = std::ranges::find(slots, weekday, &DaySlot::weekday);
        slot != slots.end())
    {
      day.weight = slot->weight;
      day.kickoffs = slot->kickoffs;
    }
    days.push_back(std::move(day));
  };
  if (weekend)
  {
    // Thursday is a reserve day for crowded weekends (e.g. no Monday before
    // an international window).
    add(SeasonCalendar::THURSDAY);
    for (const uint8_t weekday : WEEKEND_DAYS) add(weekday);
  }
  else
    for (const uint8_t weekday : MIDWEEK_DAYS) add(weekday);
  std::ranges::sort(days, {}, &RoundDay::day);

  // The anchor itself is always playable (leagueRoundDates checked it).
  const auto playable = [&](const RoundDay& day)
  { return day.day == anchor || usable(day.day); };
  std::vector<RoundDay> kept;
  for (const RoundDay& day : days)
    if (playable(day)) kept.push_back(day);
  for (const RoundDay& dropped : days)
  {
    if (playable(dropped) || dropped.weight == 0) continue;
    // The weekend's Thursday only takes matches when balancing asks for it.
    RoundDay* target = nullptr;
    const auto rank = [&](const RoundDay& day)
    {
      return std::tuple{day.weight, std::abs(day.day - dropped.day),
                        std::abs(day.day - anchor)};
    };
    for (RoundDay& day : kept)
      if (!(weekend && day.weekday == SeasonCalendar::THURSDAY) &&
          (!target || rank(day) < rank(*target)))
        target = &day;
    if (!target) continue;
    target->weight = static_cast<uint16_t>(target->weight + dropped.weight);
  }
  if (std::ranges::all_of(kept,
                          [](const RoundDay& day) { return day.weight == 0; }))
    for (RoundDay& day : kept) day.weight = 1;
  for (RoundDay& day : kept) day.reserve = day.weight == 0;
  return kept;
}

/** Splits @p matches over the days by weight (largest remainder). */
void setQuotas(std::vector<RoundDay>& days, size_t matches)
{
  if (days.empty()) return;
  size_t total = 0;
  for (const RoundDay& day : days) total += day.weight;
  if (total == 0)
  {
    const size_t each = matches / days.size();
    const size_t extra = matches % days.size();
    for (size_t i = 0; i < days.size(); ++i)
      days[i].quota = each + (i < extra ? 1 : 0);
    return;
  }
  std::vector<std::pair<size_t, size_t>> remainders;  // (remainder, index)
  size_t given = 0;
  for (size_t i = 0; i < days.size(); ++i)
  {
    days[i].quota = matches * days[i].weight / total;
    given += days[i].quota;
    remainders.emplace_back(matches * days[i].weight % total, i);
  }
  std::ranges::stable_sort(remainders, std::greater{},
                           &std::pair<size_t, size_t>::first);
  for (size_t i = 0; given < matches; ++i, ++given)
    ++days[remainders[i % remainders.size()].second].quota;
}

/** A round of one league, planned before its fixtures get their days. */
struct PlannedRound
{
  size_t league = 0;  // Index into the planned leagues.
  bool top_flight = false;
  size_t moves_left = 0;  // Matches that may leave their pattern day.
  std::vector<RoundDay> days;
};

/** Matches a top flight may move off its pattern days in one round (when a
 * lower division lost a day, e.g. before a cup or midweek round). */
constexpr size_t TOP_FLIGHT_MOVES = 5;

/** League matches in one day below which nobody plays on a reserve day. */
constexpr size_t CROWDED_DAY = 40;

/** Continental matches expected on each continental matchday (league phase:
 * 288 matches over eight Tuesday-Thursday weeks). */
constexpr size_t CONTINENTAL_DAY_MATCHES = 12;

/**
 * Evens out the busiest days of the world. The rounds of every window move
 * single matches from a day that is at least two matches busier (all
 * leagues together) to a quieter day of their own round, never putting more
 * than 70% of a round on one day. Lower divisions do most of it; a top
 * flight moves at most TOP_FLIGHT_MOVES matches. Reserve days only take
 * matches from days busier than @p comfortable (e.g. both tiers in the same
 * midweek).
 */
void balanceRounds(const std::vector<PlannedRound*>& window,
                   std::unordered_map<int, size_t>& load, size_t comfortable)
{
  const auto cap = [](const PlannedRound& round)
  {
    size_t matches = 0;
    for (const RoundDay& day : round.days) matches += day.quota;
    return (matches * 7 + 9) / 10;
  };
  for (;;)
  {
    PlannedRound* mover = nullptr;
    RoundDay* from = nullptr;
    RoundDay* to = nullptr;
    size_t best_gap = 1;
    for (PlannedRound* round : window)
    {
      if (round->moves_left == 0) continue;
      const size_t limit = cap(*round);
      for (RoundDay& busy : round->days)
      {
        if (busy.quota == 0) continue;
        for (RoundDay& quiet : round->days)
        {
          if (&quiet == &busy || quiet.quota >= limit ||
              load[busy.day] < load[quiet.day] + 2 ||
              (quiet.reserve && load[busy.day] <= comfortable))
            continue;
          const size_t gap = load[busy.day] - load[quiet.day];
          if (gap > best_gap || (gap == best_gap && mover &&
                                 mover->top_flight && !round->top_flight))
          {
            best_gap = gap;
            mover = round;
            from = &busy;
            to = &quiet;
          }
        }
      }
    }
    if (!mover) return;
    --mover->moves_left;
    --from->quota;
    --load[from->day];
    ++to->quota;
    ++load[to->day];
  }
}

/** Kick-off of the @p index-th of @p count matches on a day: the listed
 * times spread over the matches (the last one is the evening match). */
uint16_t kickoffOf(const RoundDay& day, size_t index, size_t count)
{
  if (day.kickoffs.empty())
    return day.weekday == SeasonCalendar::SATURDAY ||
                   day.weekday == SeasonCalendar::SUNDAY
               ? minutesOf(15, 0)
               : minutesOf(20, 45);
  const size_t size = day.kickoffs.size();
  size_t slot = 0;
  if (count > size)
    slot = index * size / count;
  else
    slot = count == 1 ? size - 1 : index * (size - 1) / (count - 1);
  return day.kickoffs[std::min(slot, size - 1)];
}

}  // namespace

// ---------------- Calendar ----------------

void Calendar::generate(const class GameData& gamedata,
                        const GameDateValue& startDate)
{
  schedule.clear();
  rest_check_pending = false;
  const uint16_t season_year = SeasonCalendar::seasonStartYear(startDate);
  generateFriendlies(gamedata, startDate, SeasonCalendar::PRESEASON_FRIENDLIES);
  generateSeasonFixtures(gamedata, season_year);
  // Cup ties pick the midweek day that rests both clubs; league fixtures
  // move within their round when no day does.
  Competitions::scheduleCupFirstRounds(*this, gamedata, season_year);
  protectRest(startDate);
}

void Calendar::addMatch(const Match& match)
{
  if (match.getMatchType() == MatchType::CUP ||
      match.getMatchType() == MatchType::CONTINENTAL)
    rest_check_pending = true;
  schedule[match.getDate()].push_back(match);
}

const std::map<GameDateValue, std::vector<Match>>& Calendar::getFullCalendar()
    const
{
  return schedule;
}

const std::vector<Match>& Calendar::getMatchesForDate(
    const GameDateValue& date) const
{
  static const std::vector<Match> no_matches;
  auto it = schedule.find(date);
  if (it != schedule.end())
  {
    return it->second;
  }
  return no_matches;
}

std::vector<Match>& Calendar::getMatchesForDateMutable(
    const GameDateValue& date)
{
  return schedule[date];
}

const Match* Calendar::findMatch(const GameDateValue& date, TeamID home_id,
                                 TeamID away_id) const
{
  const auto it = schedule.find(date);
  if (it == schedule.end()) return nullptr;
  const auto match =
      std::ranges::find_if(it->second,
                           [&](const Match& candidate)
                           {
                             return candidate.getHomeTeamId() == home_id &&
                                    candidate.getAwayTeamId() == away_id;
                           });
  return match == it->second.end() ? nullptr : std::to_address(match);
}

Match* Calendar::findMatch(const GameDateValue& date, TeamID home_id,
                           TeamID away_id)
{
  const auto date_it = schedule.find(date);
  if (date_it == schedule.end()) return nullptr;
  const auto match =
      std::ranges::find_if(date_it->second,
                           [home_id, away_id](const Match& candidate)
                           {
                             return candidate.getHomeTeamId() == home_id &&
                                    candidate.getAwayTeamId() == away_id;
                           });
  return match == date_it->second.end() ? nullptr : std::to_address(match);
}

std::vector<Match> Calendar::getTeamFixtures(TeamID team_id) const
{
  std::vector<Match> fixtures;
  for (const auto& [date, matches] : schedule)
  {
    for (const Match& match : matches)
    {
      if (match.getHomeTeamId() == team_id || match.getAwayTeamId() == team_id)
        fixtures.push_back(match);
    }
  }
  return fixtures;
}

void Calendar::generateSeasonFixtures(const class GameData& gamedata,
                                      uint16_t season_year)
{
  // Top flights first, so the lower divisions can fill the quieter days.
  std::vector<std::pair<uint8_t, LeagueID>> league_order;
  league_order.reserve(gamedata.getLeagues().size());
  for (const auto& [id, league] : gamedata.getLeagues())
    league_order.emplace_back(Competitions::leagueTier(gamedata, id), id);
  std::ranges::sort(league_order);

  const std::unordered_map<LeagueID, RoundPattern> patterns =
      loadRoundPatterns();
  std::vector<int> continental_days;
  for (const int tuesday : continentalWeekDays(season_year))
    for (int offset = 0; offset < CONTINENTAL_DAYS_PER_WEEK; ++offset)
      continental_days.push_back(tuesday + offset);

  // 1. Pairings, round dates and the days every round may use.
  struct PlannedLeague
  {
    LeagueID id = 0;
    std::vector<std::vector<std::pair<TeamID, TeamID>>> first_half;
    std::vector<PlannedRound> rounds;
  };
  std::vector<PlannedLeague> plans;
  for (const auto& [tier, league_id] : league_order)
  {
    const League& league = gamedata.getLeagues().at(league_id);
    std::vector<TeamID> team_ids = league.getTeamIDs();

    if (team_ids.size() < 2)
    {
      Logger::warn("Not enough teams to generate fixtures in: " +
                   league.getName());
      continue;
    }

    const auto found = patterns.find(league_id);
    RoundPattern pattern =
        found != patterns.end() ? found->second : defaultPattern(tier);
    const RoundPattern fallback = defaultPattern(tier);
    if (pattern.weekend.empty()) pattern.weekend = fallback.weekend;
    if (pattern.midweek.empty()) pattern.midweek = fallback.midweek;

    std::ranges::sort(team_ids);
    std::mt19937 rng(Competitions::mixSeed(season_year, league_id, 1));
    std::ranges::shuffle(team_ids, rng);
    if (team_ids.size() % 2 != 0)
    {
      team_ids.push_back(FREE_AGENTS_TEAM_ID);  // Bye for round robin
    }

    const size_t num_teams = team_ids.size();
    const size_t half_rounds = num_teams - 1;
    const std::vector<GameDateValue> round_dates =
        SeasonCalendar::leagueRoundDates(season_year, half_rounds * 2,
                                         tier <= 1 ? 0 : 1);

    PlannedLeague plan;
    plan.id = league_id;
    // Circle method; venues flip every round so teams alternate home/away.
    plan.first_half.resize(half_rounds);
    for (size_t round = 0; round < half_rounds; ++round)
    {
      for (size_t i = 0; i < num_teams / 2; ++i)
      {
        TeamID home_id = team_ids[i];
        TeamID away_id = team_ids[num_teams - 1 - i];
        if (home_id == FREE_AGENTS_TEAM_ID || away_id == FREE_AGENTS_TEAM_ID)
          continue;
        if (round % 2 == 1) std::swap(home_id, away_id);
        plan.first_half[round].emplace_back(home_id, away_id);
      }
      const TeamID last_id = team_ids.back();
      team_ids.pop_back();
      team_ids.insert(team_ids.begin() + 1, last_id);
    }

    // Days a round may not use: breaks, the days next to a continental
    // matchday for top flights (their clubs play in Europe) and a Monday
    // followed by a midweek round.
    const size_t rounds = std::min(half_rounds * 2, round_dates.size());
    plan.rounds.resize(rounds);
    for (size_t round = 0; round < rounds; ++round)
    {
      const int anchor = toDayNumber(round_dates[round]);
      const bool weekend = weekdayOf(anchor) == SeasonCalendar::SATURDAY;
      const int next_start = round + 1 < rounds
                                 ? toDayNumber(round_dates[round + 1]) - 1
                                 : INT_MAX / 2;
      const auto usable =
          [weekend, anchor, tier, next_start, &continental_days](int day)
      {
        if (SeasonCalendar::isBlackout(fromDayNumber(day))) return false;
        // Only lower divisions (whose clubs rarely play in Europe) bring a
        // weekend match forward to Thursday.
        if (weekend && day < anchor - 1 && tier <= 1) return false;
        if (weekend && next_start - day < SeasonCalendar::MIN_REST_DAYS)
          return false;
        return tier > 1 || std::ranges::none_of(
                               continental_days, [day](int continental)
                               { return std::abs(continental - day) <= 1; });
      };
      PlannedRound& planned = plan.rounds[round];
      planned.league = plans.size();
      const size_t matches = plan.first_half[round % half_rounds].size();
      planned.top_flight = tier <= 1;
      planned.moves_left = tier <= 1 ? TOP_FLIGHT_MOVES : matches * 4;
      planned.days = roundDays(anchor, pattern, usable);
      setQuotas(planned.days, matches);
    }
    plans.push_back(std::move(plan));
  }

  // 2. The lower divisions even out the busiest days of each round window.
  std::unordered_map<int, size_t> load;  // Planned league matches per day.
  std::map<int, std::vector<PlannedRound*>> windows;  // By first day.
  size_t round_matches = 0;  // One round of every league.
  for (PlannedLeague& plan : plans)
  {
    if (!plan.first_half.empty())
      round_matches += plan.first_half.front().size();
    for (PlannedRound& round : plan.rounds)
    {
      for (const RoundDay& day : round.days) load[day.day] += day.quota;
      windows[round.days.front().day - weekdayOf(round.days.front().day)]
          .push_back(&round);
    }
  }
  // Continental matchdays carry their own matches (drawn later).
  for (const int day : continental_days) load[day] += CONTINENTAL_DAY_MATCHES;
  // Reserve days only relieve days that are crowded for the whole world.
  const size_t comfortable =
      std::max(round_matches / WEEKEND_DAYS.size(), CROWDED_DAY);
  for (auto& [week, window] : windows) balanceRounds(window, load, comfortable);

  // 3. Every match gets a day of its round, resting both clubs.
  for (PlannedLeague& plan : plans)
  {
    const size_t half_rounds = plan.first_half.size();
    const size_t rounds = plan.rounds.size();
    std::unordered_map<TeamID, int> last_played;
    const auto lastPlayed = [&last_played](TeamID team)
    {
      const auto it = last_played.find(team);
      return it == last_played.end() ? NEVER : it->second;
    };
    for (size_t round = 0; round < rounds; ++round)
    {
      const bool second_half = round >= half_rounds;
      const auto& pairs = plan.first_half[round % half_rounds];
      std::vector<RoundDay>& days = plan.rounds[round].days;

      // The next round must still find a planned day for every club.
      int next_latest = INT_MAX / 2;
      if (round + 1 < rounds)
      {
        next_latest = plan.rounds[round + 1].days.back().day;
        for (const RoundDay& day : plan.rounds[round + 1].days)
          if (day.quota > 0) next_latest = day.day;
      }
      const auto fits = [&pairs, &lastPlayed, next_latest](size_t pair, int day)
      {
        return day - lastPlayed(pairs[pair].first) >=
                   SeasonCalendar::MIN_REST_DAYS &&
               day - lastPlayed(pairs[pair].second) >=
                   SeasonCalendar::MIN_REST_DAYS &&
               next_latest - day >= SeasonCalendar::MIN_REST_DAYS;
      };

      // Most constrained matches pick first; otherwise a seeded order.
      std::vector<size_t> order(pairs.size());
      std::iota(order.begin(), order.end(), size_t{0});
      std::mt19937 day_rng(Competitions::mixSeed(
          season_year,
          (static_cast<uint32_t>(plan.id) << 8U) | static_cast<uint32_t>(round),
          ROUND_DAY_SALT));
      std::ranges::shuffle(order, day_rng);
      std::vector<size_t> options(pairs.size(), 0);
      for (size_t pair = 0; pair < pairs.size(); ++pair)
        for (const RoundDay& day : days)
          if (fits(pair, day.day)) ++options[pair];
      std::ranges::stable_sort(
          order, {}, [&options](size_t pair) { return options[pair]; });

      const auto left = [](const RoundDay& day)
      {
        return static_cast<long>(day.quota) -
               static_cast<long>(day.matches.size());
      };
      for (const size_t pair : order)
      {
        // Days given matches by the plan first; reserve days only if the
        // clubs cannot play on any of them.
        RoundDay* chosen = nullptr;
        const bool planned_day = std::ranges::any_of(
            days, [&](const RoundDay& day)
            { return day.quota > 0 && fits(pair, day.day); });
        for (RoundDay& day : days)
        {
          if (!fits(pair, day.day) || (planned_day && day.quota == 0)) continue;
          if (!chosen || left(day) > left(*chosen) ||
              (left(day) == left(*chosen) && load[day.day] < load[chosen->day]))
            chosen = &day;
        }
        if (!chosen)
        {
          // Cannot happen with the stock calendar: keep the longest rest.
          const auto rest = [&](const RoundDay& day)
          {
            return std::min(day.day - lastPlayed(pairs[pair].first),
                            day.day - lastPlayed(pairs[pair].second));
          };
          chosen = std::to_address(std::ranges::max_element(days, {}, rest));
          Logger::warn("No rested day for a league fixture on " +
                       fromDayNumber(chosen->day).toString());
        }
        chosen->matches.push_back(pair);
        last_played[pairs[pair].first] = chosen->day;
        last_played[pairs[pair].second] = chosen->day;
      }

      const auto stage = static_cast<uint8_t>(round + 1);
      for (const RoundDay& day : days)
      {
        const GameDateValue date = fromDayNumber(day.day);
        for (size_t index = 0; index < day.matches.size(); ++index)
        {
          const auto [home_id, away_id] = pairs[day.matches[index]];
          Match match = second_half ? Match(away_id, home_id, date,
                                            MatchType::LEAGUE, plan.id, stage)
                                    : Match(home_id, away_id, date,
                                            MatchType::LEAGUE, plan.id, stage);
          match.setKickoff(kickoffOf(day, index, day.matches.size()));
          addMatch(match);
        }
      }
    }
  }
}

size_t Calendar::protectRest(const GameDateValue& after)
{
  using enum MatchType;
  if (!rest_check_pending) return 0;
  rest_check_pending = false;

  // Competitive fixtures of every club, by day.
  struct Busy
  {
    int day = 0;
    MatchType type = LEAGUE;
  };
  std::unordered_map<TeamID, std::vector<Busy>> busy;
  struct LeagueFixture
  {
    GameDateValue date;
    TeamID home_id;
    TeamID away_id;
    LeagueID competition;
    uint8_t stage;
  };
  std::vector<LeagueFixture> league_fixtures;
  const int first = toDayNumber(after);
  for (const auto& [date, matches] : schedule)
  {
    const int day = toDayNumber(date);
    for (const Match& match : matches)
    {
      if (match.getMatchType() == FRIENDLY) continue;
      busy[match.getHomeTeamId()].push_back({day, match.getMatchType()});
      busy[match.getAwayTeamId()].push_back({day, match.getMatchType()});
      if (match.getMatchType() == LEAGUE && !match.isPlayed() && day > first)
        league_fixtures.push_back({date, match.getHomeTeamId(),
                                   match.getAwayTeamId(),
                                   match.getCompetitionId(), match.getStage()});
    }
  }

  // Whether a club could play its league fixture of @p from on @p day.
  const auto rested = [&busy](TeamID team, int from, int day)
  {
    bool skipped = false;
    for (const Busy& other : busy[team])
    {
      if (!skipped && other.day == from && other.type == LEAGUE)
      {
        skipped = true;
        continue;
      }
      if (other.day == day) return false;
      if (other.day < day &&
          day - other.day < SeasonCalendar::restDays(other.type, LEAGUE))
        return false;
      if (other.day > day &&
          other.day - day < SeasonCalendar::restDays(LEAGUE, other.type))
        return false;
    }
    return true;
  };

  size_t moved = 0;
  // Whether the round of a Thursday fixture continues at the weekend.
  const auto weekendRoundAfter =
      [this](const GameDateValue& thursday, LeagueID competition, uint8_t stage)
  {
    for (int ahead = 1; ahead <= 4; ++ahead)
      for (const Match& match :
           getMatchesForDate(SeasonCalendar::addDays(thursday, ahead)))
        if (match.getMatchType() == LEAGUE &&
            match.getCompetitionId() == competition &&
            match.getStage() == stage)
          return true;
    return false;
  };
  // Leagues that bring weekend matches forward to Thursday (lower
  // divisions) may also move a fixture there.
  std::set<LeagueID> thursday_leagues;
  for (const auto& [date, matches] : schedule)
  {
    if (weekdayOf(toDayNumber(date)) != SeasonCalendar::THURSDAY) continue;
    for (const Match& match : matches)
      if (match.getMatchType() == LEAGUE &&
          !thursday_leagues.contains(match.getCompetitionId()) &&
          weekendRoundAfter(date, match.getCompetitionId(), match.getStage()))
        thursday_leagues.insert(match.getCompetitionId());
  }
  for (const auto& [date, home_id, away_id, competition, stage] :
       league_fixtures)
  {
    const int day = toDayNumber(date);
    if (rested(home_id, day, day) && rested(away_id, day, day)) continue;

    // Other days of the same round: Friday-Monday or Tuesday-Thursday.
    const auto weekday = static_cast<uint8_t>(weekdayOf(day));
    bool midweek = weekday >= SeasonCalendar::TUESDAY &&
                   weekday <= SeasonCalendar::THURSDAY;
    int window_start = midweek ? day - (weekday - SeasonCalendar::TUESDAY)
                               : day - anchorOffset(weekday, true) - 1;
    int window_days = midweek ? 3 : 4;
    if (!midweek && thursday_leagues.contains(competition))
    {
      --window_start;
      ++window_days;
    }
    if (weekday == SeasonCalendar::THURSDAY &&
        weekendRoundAfter(date, competition, stage))
    {
      // A weekend match brought forward: Thursday to Monday.
      window_start = day;
      window_days = 5;
    }
    std::optional<int> target;
    for (int candidate = window_start; candidate < window_start + window_days;
         ++candidate)
    {
      if (candidate == day || candidate <= first ||
          SeasonCalendar::isBlackout(fromDayNumber(candidate)) ||
          !rested(home_id, day, candidate) || !rested(away_id, day, candidate))
        continue;
      // The quietest day (matches of every competition), then the nearest.
      const auto load = [this](int candidate_day)
      {
        const auto found = schedule.find(fromDayNumber(candidate_day));
        return found == schedule.end() ? size_t{0} : found->second.size();
      };
      if (!target.has_value() ||
          std::pair{load(candidate), std::abs(candidate - day)} <
              std::pair{load(*target), std::abs(*target - day)})
        target = candidate;
    }
    if (!target.has_value()) continue;

    std::vector<Match>& from_day = schedule[date];
    const auto it =
        std::ranges::find_if(from_day,
                             [&](const Match& match)
                             {
                               return match.getHomeTeamId() == home_id &&
                                      match.getAwayTeamId() == away_id;
                             });
    if (it == from_day.end()) continue;
    const GameDateValue to = fromDayNumber(*target);
    Match match(home_id, away_id, to, LEAGUE, it->getCompetitionId(),
                it->getStage());
    match.setKickoff(it->getScheduledKickoff());
    from_day.erase(it);
    if (from_day.empty()) schedule.erase(date);
    schedule[to].push_back(match);
    for (const TeamID team : {home_id, away_id})
    {
      std::vector<Busy>& days = busy[team];
      if (const auto entry = std::ranges::find_if(
              days, [day](const Busy& other)
              { return other.day == day && other.type == LEAGUE; });
          entry != days.end())
        entry->day = *target;
      std::ranges::sort(days, {}, &Busy::day);
    }
    ++moved;
  }
  return moved;
}

void Calendar::generateFriendlies(const class GameData& gamedata,
                                  const GameDateValue& startDate,
                                  size_t numFriendlies)
{
  // Pre-season tours stay in the club's region (same country or nearby):
  // clubs are paired within their region, and leftovers of odd-sized
  // regions play each other.
  std::map<WorldRegion, std::vector<TeamID>> regions;
  for (const auto& [id, team] : gamedata.getTeams())
  {
    if (id != FREE_AGENTS_TEAM_ID)
      regions[leagueProfile(team.getLeagueId()).region].push_back(id);
  }
  for (auto& [region, team_ids] : regions) std::ranges::sort(team_ids);

  // Each pre-season week's friendlies are spread from Tuesday to Sunday
  // (evenings on weekdays, afternoons at the weekend): a club plays once a
  // week, so two friendlies are always at least two days apart.
  struct FriendlyDay
  {
    int offset;  // From the week's Saturday.
    uint16_t kickoff;
  };
  constexpr std::array<FriendlyDay, 6> FRIENDLY_DAYS = {
      {{-4, minutesOf(19, 30)},
       {-3, minutesOf(20, 0)},
       {-2, minutesOf(19, 0)},
       {-1, minutesOf(20, 0)},
       {0, minutesOf(17, 0)},
       {1, minutesOf(18, 0)}}};
  const uint16_t season_year = SeasonCalendar::seasonStartYear(startDate);
  const std::vector<GameDateValue> dates =
      SeasonCalendar::friendlyDates(startDate, numFriendlies);
  for (size_t round = 0; round < dates.size(); ++round)
  {
    std::mt19937 rng(
        Competitions::mixSeed(season_year, static_cast<uint32_t>(round), 2));
    size_t pair_index = round;
    const auto add = [&](TeamID home_id, TeamID away_id)
    {
      const FriendlyDay& day =
          FRIENDLY_DAYS[pair_index++ % FRIENDLY_DAYS.size()];
      Match match(home_id, away_id,
                  SeasonCalendar::addDays(dates[round], day.offset),
                  MatchType::FRIENDLY);
      match.setKickoff(day.kickoff);
      addMatch(match);
    };
    std::vector<TeamID> leftovers;
    for (auto& [region, team_ids] : regions)
    {
      std::ranges::shuffle(team_ids, rng);
      size_t i = 0;
      for (; i + 1 < team_ids.size(); i += 2) add(team_ids[i], team_ids[i + 1]);
      if (i < team_ids.size()) leftovers.push_back(team_ids[i]);
    }
    for (size_t i = 0; i + 1 < leftovers.size(); i += 2)
      add(leftovers[i], leftovers[i + 1]);
  }
}
