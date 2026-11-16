// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "scouting_repository.h"

#include <sqlite3.h>

#include <string>
#include <utility>

#include "model/world_rng.h"

namespace
{
constexpr int REASON_FITS = 0x01;
constexpr int REASON_AFFORDABLE = 0x02;
constexpr int REASON_AVAILABLE = 0x04;

std::string columnText(sqlite3_stmt* stmt, int column)
{
  const unsigned char* text = sqlite3_column_text(stmt, column);
  return text ? reinterpret_cast<const char*>(text) : "";
}

template <typename T>
T columnAs(sqlite3_stmt* stmt, int column)
{
  return static_cast<T>(sqlite3_column_int64(stmt, column));
}

float columnFloat(sqlite3_stmt* stmt, int column)
{
  return static_cast<float>(sqlite3_column_double(stmt, column));
}

/** Runs @p read for every row of @p sql. */
template <typename Read>
void forEachRow(const DatabaseConnection& db, const char* sql, Read read)
{
  sqlite3_stmt* stmt = db.prepareStatement(sql);
  while (sqlite3_step(stmt) == SQLITE_ROW) read(stmt);
  sqlite3_finalize(stmt);
}

/** Executes @p bind + step for every item, reusing one statement. */
template <typename Range, typename Bind>
void insertAll(const DatabaseConnection& db, const char* sql,
               const Range& items, Bind bind)
{
  sqlite3_stmt* stmt = db.prepareStatement(sql);
  for (const auto& item : items)
  {
    bind(stmt, item);
    db.executeStep(stmt);
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
  }
  sqlite3_finalize(stmt);
}
}  // namespace

ScoutingRepository::ScoutingRepository(std::shared_ptr<DatabaseConnection> conn)
    : db_conn(std::move(conn))
{
}

bool ScoutingRepository::load(ScoutingState& state) const
{
  state = ScoutingState{};
  bool found = false;
  forEachRow(*db_conn,
             "SELECT team_id, next_assignment_id, next_report_id, "
             "next_focus_id FROM ScoutingState WHERE id = 1;",
             [&state, &found](sqlite3_stmt* stmt)
             {
               found = true;
               state.team_id = columnAs<TeamID>(stmt, 0);
               state.next_assignment_id = columnAs<std::uint32_t>(stmt, 1);
               state.next_report_id = columnAs<std::uint32_t>(stmt, 2);
               state.next_focus_id = columnAs<std::uint32_t>(stmt, 3);
             });
  if (!found) return false;

  forEachRow(*db_conn,
             "SELECT player_id, knowledge, judging_ability, judging_potential, "
             "last_report_day FROM ScoutingKnowledge;",
             [&state](sqlite3_stmt* stmt)
             {
               ScoutKnowledge entry;
               entry.knowledge = columnFloat(stmt, 1);
               entry.judging_ability = columnAs<std::uint8_t>(stmt, 2);
               entry.judging_potential = columnAs<std::uint8_t>(stmt, 3);
               entry.last_report_day = columnAs<std::int32_t>(stmt, 4);
               state.knowledge.emplace(columnAs<PlayerID>(stmt, 0), entry);
             });

  forEachRow(*db_conn,
             "SELECT id, scout_id, kind, target_id, start_date, duration_days, "
             "days_done, cost, players_observed, reports_filed, finished "
             "FROM ScoutAssignments ORDER BY id;",
             [&state](sqlite3_stmt* stmt)
             {
               ScoutAssignment assignment;
               assignment.id = columnAs<std::uint32_t>(stmt, 0);
               assignment.scout_id = columnAs<std::uint32_t>(stmt, 1);
               assignment.kind = columnAs<ScoutTargetKind>(stmt, 2);
               assignment.target_id = columnAs<std::uint32_t>(stmt, 3);
               assignment.start_date =
                   dateFromInt(columnAs<std::int32_t>(stmt, 4));
               assignment.duration_days = columnAs<std::uint16_t>(stmt, 5);
               assignment.days_done = columnAs<std::uint16_t>(stmt, 6);
               assignment.cost = columnAs<std::int64_t>(stmt, 7);
               assignment.players_observed = columnAs<std::uint16_t>(stmt, 8);
               assignment.reports_filed = columnAs<std::uint16_t>(stmt, 9);
               assignment.finished = sqlite3_column_int(stmt, 10) != 0;
               state.assignments.push_back(assignment);
             });

  forEachRow(*db_conn,
             "SELECT id, game_date, player_id, assignment_id, scout_id, "
             "scout_name, knowledge, confidence, overall, potential_low, "
             "potential_high, estimated_fee, grade, reasons, overall_low, "
             "overall_high, seen FROM ScoutReports ORDER BY id;",
             [&state](sqlite3_stmt* stmt)
             {
               ScoutReport report;
               report.id = columnAs<std::uint32_t>(stmt, 0);
               report.date = dateFromInt(columnAs<std::int32_t>(stmt, 1));
               report.player_id = columnAs<PlayerID>(stmt, 2);
               report.assignment_id = columnAs<std::uint32_t>(stmt, 3);
               report.scout_id = columnAs<std::uint32_t>(stmt, 4);
               report.scout_name = columnText(stmt, 5);
               report.knowledge = columnAs<std::uint8_t>(stmt, 6);
               report.confidence = columnAs<std::uint8_t>(stmt, 7);
               report.overall = columnFloat(stmt, 8);
               report.potential_low = columnFloat(stmt, 9);
               report.potential_high = columnFloat(stmt, 10);
               report.estimated_fee = columnAs<std::int64_t>(stmt, 11);
               report.grade = columnAs<ScoutGrade>(stmt, 12);
               const int reasons = sqlite3_column_int(stmt, 13);
               report.fits_need = (reasons & REASON_FITS) != 0;
               report.affordable = (reasons & REASON_AFFORDABLE) != 0;
               report.available = (reasons & REASON_AVAILABLE) != 0;
               report.overall_low = columnFloat(stmt, 14);
               report.overall_high = columnFloat(stmt, 15);
               if (report.overall_high <= 0.0f)
               {
                 // Reports from before ranges were recorded.
                 report.overall_low = report.overall;
                 report.overall_high = report.overall;
               }
               report.seen = sqlite3_column_int(stmt, 16) != 0;
               state.reports.push_back(std::move(report));
             });

  forEachRow(*db_conn,
             "SELECT id, role, min_age, max_age, max_fee, max_wage, "
             "min_ability FROM RecruitmentFocus ORDER BY id;",
             [&state](sqlite3_stmt* stmt)
             {
               RecruitmentFocus focus;
               focus.id = columnAs<std::uint32_t>(stmt, 0);
               focus.role = columnAs<PlayerRole>(stmt, 1);
               focus.min_age = columnAs<std::uint8_t>(stmt, 2);
               focus.max_age = columnAs<std::uint8_t>(stmt, 3);
               focus.max_fee = columnAs<std::int64_t>(stmt, 4);
               focus.max_wage = columnAs<std::uint32_t>(stmt, 5);
               focus.min_ability = columnAs<std::uint8_t>(stmt, 6);
               state.focuses.push_back(focus);
             });

  forEachRow(*db_conn,
             "SELECT player_id, added_date, last_team, last_flags "
             "FROM ScoutShortlist ORDER BY rowid;",
             [&state](sqlite3_stmt* stmt)
             {
               ShortlistEntry entry;
               entry.player_id = columnAs<PlayerID>(stmt, 0);
               entry.added = dateFromInt(columnAs<std::int32_t>(stmt, 1));
               entry.last_team = columnAs<TeamID>(stmt, 2);
               entry.last_flags = columnAs<std::uint8_t>(stmt, 3);
               state.shortlist.push_back(entry);
             });

  forEachRow(*db_conn,
             "SELECT scout_id, nationality, languages FROM ScoutExpertise;",
             [&state](sqlite3_stmt* stmt)
             {
               ScoutExpertise& expertise =
                   state.expertise[columnAs<std::uint32_t>(stmt, 0)];
               expertise.nationality = columnAs<Language>(stmt, 1);
               expertise.languages = columnAs<LanguageSet>(stmt, 2);
             });
  forEachRow(*db_conn,
             "SELECT scout_id, league_id, days FROM ScoutLeagueExperience;",
             [&state](sqlite3_stmt* stmt)
             {
               const auto scout =
                   state.expertise.find(columnAs<std::uint32_t>(stmt, 0));
               if (scout == state.expertise.end()) return;
               scout->second.league_days[columnAs<LeagueID>(stmt, 1)] =
                   columnAs<std::uint16_t>(stmt, 2);
             });
  return true;
}

void ScoutingRepository::save(const ScoutingState& state) const
{
  for (const char* table :
       {"ScoutingState", "ScoutingKnowledge", "ScoutAssignments",
        "ScoutReports", "RecruitmentFocus", "ScoutShortlist", "ScoutExpertise",
        "ScoutLeagueExperience"})
  {
    sqlite3_exec(db_conn->getRaw(),
                 (std::string("DELETE FROM ") + table + ";").c_str(), nullptr,
                 nullptr, nullptr);
  }

  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "INSERT INTO ScoutingState (id, team_id, next_assignment_id, "
      "next_report_id, next_focus_id) VALUES (1, ?, ?, ?, ?);");
  sqlite3_bind_int(stmt, 1, state.team_id);
  sqlite3_bind_int64(stmt, 2, state.next_assignment_id);
  sqlite3_bind_int64(stmt, 3, state.next_report_id);
  sqlite3_bind_int64(stmt, 4, state.next_focus_id);
  db_conn->executeStep(stmt);
  sqlite3_finalize(stmt);

  insertAll(*db_conn,
            "INSERT INTO ScoutingKnowledge (player_id, knowledge, "
            "judging_ability, judging_potential, last_report_day) "
            "VALUES (?, ?, ?, ?, ?);",
            state.knowledge,
            [](sqlite3_stmt* insert, const auto& item)
            {
              const auto& [player_id, entry] = item;
              sqlite3_bind_int64(insert, 1, player_id);
              sqlite3_bind_double(insert, 2, entry.knowledge);
              sqlite3_bind_int(insert, 3, entry.judging_ability);
              sqlite3_bind_int(insert, 4, entry.judging_potential);
              sqlite3_bind_int(insert, 5, entry.last_report_day);
            });

  insertAll(*db_conn,
            "INSERT INTO ScoutAssignments (id, scout_id, kind, target_id, "
            "start_date, duration_days, days_done, cost, players_observed, "
            "reports_filed, finished) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
            "?);",
            state.assignments,
            [](sqlite3_stmt* insert, const ScoutAssignment& assignment)
            {
              sqlite3_bind_int64(insert, 1, assignment.id);
              sqlite3_bind_int64(insert, 2, assignment.scout_id);
              sqlite3_bind_int(
                  insert, 3,
                  static_cast<int>(std::to_underlying(assignment.kind)));
              sqlite3_bind_int64(insert, 4, assignment.target_id);
              sqlite3_bind_int(insert, 5, dateToInt(assignment.start_date));
              sqlite3_bind_int(insert, 6, assignment.duration_days);
              sqlite3_bind_int(insert, 7, assignment.days_done);
              sqlite3_bind_int64(insert, 8, assignment.cost);
              sqlite3_bind_int(insert, 9, assignment.players_observed);
              sqlite3_bind_int(insert, 10, assignment.reports_filed);
              sqlite3_bind_int(insert, 11, assignment.finished ? 1 : 0);
            });

  insertAll(*db_conn,
            "INSERT INTO ScoutReports (id, game_date, player_id, "
            "assignment_id, scout_id, scout_name, knowledge, confidence, "
            "overall, potential_low, potential_high, estimated_fee, grade, "
            "reasons, overall_low, overall_high, seen) VALUES (?, ?, ?, ?, ?, "
            "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
            state.reports,
            [](sqlite3_stmt* insert, const ScoutReport& report)
            {
              const int reasons = (report.fits_need ? REASON_FITS : 0) |
                                  (report.affordable ? REASON_AFFORDABLE : 0) |
                                  (report.available ? REASON_AVAILABLE : 0);
              sqlite3_bind_int64(insert, 1, report.id);
              sqlite3_bind_int(insert, 2, dateToInt(report.date));
              sqlite3_bind_int64(insert, 3, report.player_id);
              sqlite3_bind_int64(insert, 4, report.assignment_id);
              sqlite3_bind_int64(insert, 5, report.scout_id);
              sqlite3_bind_text(insert, 6, report.scout_name.c_str(), -1,
                                SQLITE_TRANSIENT);
              sqlite3_bind_int(insert, 7, report.knowledge);
              sqlite3_bind_int(insert, 8, report.confidence);
              sqlite3_bind_double(insert, 9, report.overall);
              sqlite3_bind_double(insert, 10, report.potential_low);
              sqlite3_bind_double(insert, 11, report.potential_high);
              sqlite3_bind_int64(insert, 12, report.estimated_fee);
              sqlite3_bind_int(
                  insert, 13,
                  static_cast<int>(std::to_underlying(report.grade)));
              sqlite3_bind_int(insert, 14, reasons);
              sqlite3_bind_double(insert, 15, report.overall_low);
              sqlite3_bind_double(insert, 16, report.overall_high);
              sqlite3_bind_int(insert, 17, report.seen ? 1 : 0);
            });

  insertAll(*db_conn,
            "INSERT INTO RecruitmentFocus (id, role, min_age, max_age, "
            "max_fee, max_wage, min_ability) VALUES (?, ?, ?, ?, ?, ?, ?);",
            state.focuses,
            [](sqlite3_stmt* insert, const RecruitmentFocus& focus)
            {
              sqlite3_bind_int64(insert, 1, focus.id);
              sqlite3_bind_int(
                  insert, 2, static_cast<int>(std::to_underlying(focus.role)));
              sqlite3_bind_int(insert, 3, focus.min_age);
              sqlite3_bind_int(insert, 4, focus.max_age);
              sqlite3_bind_int64(insert, 5, focus.max_fee);
              sqlite3_bind_int64(insert, 6, focus.max_wage);
              sqlite3_bind_int(insert, 7, focus.min_ability);
            });

  insertAll(*db_conn,
            "INSERT INTO ScoutShortlist (player_id, added_date, last_team, "
            "last_flags) VALUES (?, ?, ?, ?);",
            state.shortlist,
            [](sqlite3_stmt* insert, const ShortlistEntry& entry)
            {
              sqlite3_bind_int64(insert, 1, entry.player_id);
              sqlite3_bind_int(insert, 2, dateToInt(entry.added));
              sqlite3_bind_int(insert, 3, entry.last_team);
              sqlite3_bind_int(insert, 4, entry.last_flags);
            });

  insertAll(*db_conn,
            "INSERT INTO ScoutExpertise (scout_id, nationality, languages) "
            "VALUES (?, ?, ?);",
            state.expertise,
            [](sqlite3_stmt* insert, const auto& item)
            {
              const auto& [scout_id, expertise] = item;
              sqlite3_bind_int64(insert, 1, scout_id);
              sqlite3_bind_int(insert, 2,
                               static_cast<int>(expertise.nationality));
              sqlite3_bind_int64(insert, 3, expertise.languages);
            });
  sqlite3_stmt* experience = db_conn->prepareStatement(
      "INSERT INTO ScoutLeagueExperience (scout_id, league_id, days) VALUES "
      "(?, ?, ?);");
  for (const auto& [scout_id, expertise] : state.expertise)
  {
    for (const auto& [league_id, days] : expertise.league_days)
    {
      sqlite3_bind_int64(experience, 1, scout_id);
      sqlite3_bind_int(experience, 2, league_id);
      sqlite3_bind_int(experience, 3, days);
      db_conn->executeStep(experience);
      sqlite3_reset(experience);
    }
  }
  sqlite3_finalize(experience);
}
