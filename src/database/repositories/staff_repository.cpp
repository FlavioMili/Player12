// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "database/repositories/staff_repository.h"

#include <sqlite3.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "database/repositories/text_encoding.h"

namespace
{
std::string columnText(sqlite3_stmt* stmt, int column)
{
  const unsigned char* text = sqlite3_column_text(stmt, column);
  return text ? reinterpret_cast<const char*>(text) : "";
}

void encodeAttributes(const StaffMember& member, std::string& text)
{
  text.clear();
  for (std::size_t i = 0; i < member.attributes.size(); ++i)
  {
    if (i > 0) text += ',';
    TextEncoding::appendInt(text, int{member.attributes[i]});
  }
}

/** A Staff row as stored, to skip members that did not change. */
struct StoredMember
{
  TeamID team_id = 0;
  std::string first_name;
  std::string last_name;
  int nationality = 0;
  int age = 0;
  int role = 0;
  std::string attributes;
  std::int64_t wage = 0;
  int contract_years = 0;
  bool seen = false;
};

void decodeAttributes(std::string_view text, StaffMember& member)
{
  std::size_t index = 0;
  const char* cursor = text.data();
  const char* end = text.data() + text.size();
  while (cursor < end && index < member.attributes.size())
  {
    int value = 0;
    const auto [next, error] = std::from_chars(cursor, end, value);
    if (error != std::errc()) break;
    member.attributes[index++] =
        static_cast<std::uint8_t>(std::clamp(value, 1, 100));
    cursor = next < end && *next == ',' ? next + 1 : next;
  }
}
}  // namespace

StaffRepository::StaffRepository(std::shared_ptr<DatabaseConnection> conn)
    : db_conn(std::move(conn))
{
}

std::vector<StaffMember> StaffRepository::loadAll() const
{
  std::vector<StaffMember> staff;
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "SELECT id, team_id, first_name, last_name, nationality, age, role, "
      "attributes, wage, contract_years FROM Staff ORDER BY id;");
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    StaffMember member;
    member.id = static_cast<StaffID>(sqlite3_column_int64(stmt, 0));
    member.team_id = static_cast<TeamID>(sqlite3_column_int(stmt, 1));
    member.first_name = columnText(stmt, 2);
    member.last_name = columnText(stmt, 3);
    member.nationality = static_cast<Language>(sqlite3_column_int(stmt, 4));
    member.age = static_cast<std::uint8_t>(sqlite3_column_int(stmt, 5));
    const int role = sqlite3_column_int(stmt, 6);
    if (role < 0 || role >= static_cast<int>(STAFF_ROLE_COUNT)) continue;
    member.role = static_cast<StaffRole>(role);
    decodeAttributes(columnText(stmt, 7), member);
    member.wage = static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 8));
    member.contract_years =
        static_cast<std::uint8_t>(sqlite3_column_int(stmt, 9));
    staff.push_back(std::move(member));
  }
  sqlite3_finalize(stmt);
  return staff;
}

void StaffRepository::replaceAll(const StaffRoster& roster) const
{
  // Staff changes rarely (hires, contracts, ageing): only changed, new and
  // departed members are written.
  std::unordered_map<StaffID, StoredMember> stored;
  sqlite3_stmt* select = db_conn->prepareStatement(
      "SELECT id, team_id, first_name, last_name, nationality, age, role, "
      "attributes, wage, contract_years FROM Staff;");
  while (sqlite3_step(select) == SQLITE_ROW)
  {
    StoredMember row;
    row.team_id = static_cast<TeamID>(sqlite3_column_int(select, 1));
    row.first_name = columnText(select, 2);
    row.last_name = columnText(select, 3);
    row.nationality = sqlite3_column_int(select, 4);
    row.age = sqlite3_column_int(select, 5);
    row.role = sqlite3_column_int(select, 6);
    row.attributes = columnText(select, 7);
    row.wage = sqlite3_column_int64(select, 8);
    row.contract_years = sqlite3_column_int(select, 9);
    stored.try_emplace(static_cast<StaffID>(sqlite3_column_int64(select, 0)),
                       std::move(row));
  }
  sqlite3_finalize(select);

  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "INSERT OR REPLACE INTO Staff (id, team_id, first_name, last_name, "
      "nationality, age, role, attributes, wage, contract_years) VALUES "
      "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
  std::string attributes;
  for (const auto& [id, member] : roster.all())
  {
    encodeAttributes(member, attributes);
    if (const auto found = stored.find(id); found != stored.end())
    {
      StoredMember& row = found->second;
      row.seen = true;
      if (row.team_id == member.team_id &&
          row.first_name == member.first_name &&
          row.last_name == member.last_name &&
          row.nationality ==
              static_cast<int>(std::to_underlying(member.nationality)) &&
          row.age == member.age &&
          row.role == static_cast<int>(std::to_underlying(member.role)) &&
          row.attributes == attributes && row.wage == member.wage &&
          row.contract_years == member.contract_years)
        continue;
    }
    sqlite3_bind_int64(stmt, 1, id);
    sqlite3_bind_int(stmt, 2, member.team_id);
    sqlite3_bind_text(stmt, 3, member.first_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, member.last_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5,
                     static_cast<int>(std::to_underlying(member.nationality)));
    sqlite3_bind_int(stmt, 6, member.age);
    sqlite3_bind_int(stmt, 7,
                     static_cast<int>(std::to_underlying(member.role)));
    sqlite3_bind_text(stmt, 8, attributes.c_str(),
                      static_cast<int>(attributes.size()), SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 9, member.wage);
    sqlite3_bind_int(stmt, 10, member.contract_years);
    db_conn->executeStep(stmt);
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);

  sqlite3_stmt* remove =
      db_conn->prepareStatement("DELETE FROM Staff WHERE id = ?;");
  for (const auto& [id, row] : stored)
  {
    if (row.seen) continue;
    sqlite3_bind_int64(remove, 1, id);
    db_conn->executeStep(remove);
    sqlite3_reset(remove);
  }
  sqlite3_finalize(remove);
}
