// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/holiday.h"

#include <algorithm>

#include "database/sqlite_rows.h"
#include "model/calendar.h"
#include "model/transfer_windows.h"

namespace
{
bool hasBigBid(const HolidayPreferences& rules,
               const std::vector<HolidayOffer>& offers)
{
  if (!rules.stop_big_bid) return false;
  for (const HolidayOffer& offer : offers)
  {
    if (offer.loan) continue;
    const bool big =
        rules.big_bid_threshold > 0
            ? static_cast<std::int64_t>(offer.fee) >= rules.big_bid_threshold
            : offer.key_player;
    if (big) return true;
  }
  return false;
}

bool hasKeyPlayerInjury(const HolidayPreferences& rules,
                        const std::vector<HolidayInjury>& injuries)
{
  if (!rules.stop_key_injury) return false;
  for (const HolidayInjury& injury : injuries)
    if (injury.key_player && injury.days >= Holiday::KEY_INJURY_DAYS)
      return true;
  return false;
}
}  // namespace

namespace Holiday
{
GameDateValue windowEndDate(LeagueID league, const GameDateValue& today)
{
  const GameDateValue opening = TransferWindows::nextOpening(league, today);
  const std::optional<GameDateValue> deadline =
      TransferWindows::windowEnd(league, opening);
  return SeasonCalendar::addDays(deadline.value_or(opening), 1);
}

std::optional<GameDateValue> targetDate(
    const HolidayPlan& plan, const GameDateValue& today,
    const std::optional<GameDateValue>& next_match, LeagueID league)
{
  switch (plan.mode)
  {
    using enum HolidayMode;
    case UntilDate:
      if (!(today < plan.until)) return std::nullopt;
      return plan.until;
    case NextMatch:
      if (!next_match || !(today < *next_match)) return std::nullopt;
      return next_match;
    case WindowEnd:
      return windowEndDate(league, today);
    case NextDecision:
    case COUNT:
      break;
  }
  return std::nullopt;
}

std::optional<HolidayStop> checkStop(const HolidayPlan& plan,
                                     const HolidayDay& day)
{
  using enum HolidayStop;
  const HolidayPreferences& rules = plan.preferences;
  if (day.dismissed) return Dismissed;
  if (rules.stop_sacking_warning && day.board_warning) return SackingWarning;
  if (hasBigBid(rules, day.new_offers)) return BigBid;
  if (hasKeyPlayerInjury(rules, day.new_injuries)) return KeyPlayerInjured;
  if (rules.stop_injury_crisis && day.injured > day.injured_at_start &&
      day.injured >= static_cast<int>(rules.injury_crisis_count))
    return InjuryCrisis;
  using enum HolidayMode;
  if (plan.mode == NextDecision && day.new_decision) return Decision;
  return std::nullopt;
}

const char* stopKey(HolidayStop stop)
{
  using enum HolidayStop;
  switch (stop)
  {
    case Completed:
      return "HOLIDAY_STOP_COMPLETED";
    case Dismissed:
      return "HOLIDAY_STOP_DISMISSED";
    case SackingWarning:
      return "HOLIDAY_STOP_WARNING";
    case BigBid:
      return "HOLIDAY_STOP_BIG_BID";
    case KeyPlayerInjured:
      return "HOLIDAY_STOP_KEY_INJURY";
    case InjuryCrisis:
      return "HOLIDAY_STOP_INJURY_CRISIS";
    case Decision:
      return "HOLIDAY_STOP_DECISION";
    case Interrupted:
      return "HOLIDAY_STOP_INTERRUPTED";
    case DayLimit:
      break;
  }
  return "HOLIDAY_STOP_DAY_LIMIT";
}

HolidayPreferences loadPreferences(
    const std::shared_ptr<DatabaseConnection>& db_conn)
{
  using namespace SqliteRows;
  HolidayPreferences preferences;
  forEach(*db_conn,
          "SELECT stop_big_bid, big_bid_threshold, stop_sacking_warning, "
          "stop_injury_crisis, injury_crisis_count, stop_key_injury, "
          "assistant_lineup, assistant_training, assistant_inbox FROM "
          "HolidayPreferences WHERE id = 1;",
          [&preferences](sqlite3_stmt* stmt)
          {
            preferences.stop_big_bid = column<int>(stmt, 0) != 0;
            preferences.big_bid_threshold = column<std::int64_t>(stmt, 1);
            preferences.stop_sacking_warning = column<int>(stmt, 2) != 0;
            preferences.stop_injury_crisis = column<int>(stmt, 3) != 0;
            preferences.injury_crisis_count = static_cast<std::uint8_t>(
                std::clamp(column<int>(stmt, 4), 1, 30));
            preferences.stop_key_injury = column<int>(stmt, 5) != 0;
            preferences.assistant_lineup = column<int>(stmt, 6) != 0;
            preferences.assistant_training = column<int>(stmt, 7) != 0;
            preferences.assistant_inbox = column<int>(stmt, 8) != 0;
          });
  return preferences;
}

void savePreferences(const std::shared_ptr<DatabaseConnection>& db_conn,
                     const HolidayPreferences& preferences)
{
  using namespace SqliteRows;
  const DatabaseConnection& db = *db_conn;
  clearTable(db, "HolidayPreferences");
  sqlite3_stmt* stmt = db.prepareStatement(
      "INSERT INTO HolidayPreferences (id, stop_big_bid, big_bid_threshold, "
      "stop_sacking_warning, stop_injury_crisis, injury_crisis_count, "
      "stop_key_injury, assistant_lineup, assistant_training, "
      "assistant_inbox) VALUES (1, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
  sqlite3_bind_int(stmt, 1, preferences.stop_big_bid ? 1 : 0);
  sqlite3_bind_int64(stmt, 2, preferences.big_bid_threshold);
  sqlite3_bind_int(stmt, 3, preferences.stop_sacking_warning ? 1 : 0);
  sqlite3_bind_int(stmt, 4, preferences.stop_injury_crisis ? 1 : 0);
  sqlite3_bind_int(stmt, 5, preferences.injury_crisis_count);
  sqlite3_bind_int(stmt, 6, preferences.stop_key_injury ? 1 : 0);
  sqlite3_bind_int(stmt, 7, preferences.assistant_lineup ? 1 : 0);
  sqlite3_bind_int(stmt, 8, preferences.assistant_training ? 1 : 0);
  sqlite3_bind_int(stmt, 9, preferences.assistant_inbox ? 1 : 0);
  db.executeStep(stmt);
  sqlite3_finalize(stmt);
}
}  // namespace Holiday
