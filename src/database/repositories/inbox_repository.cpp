// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "inbox_repository.h"

#include <sqlite3.h>

#include <nlohmann/json.hpp>
#include <string>
#include <utility>

#include "model/world_rng.h"

namespace
{
std::string columnText(sqlite3_stmt* stmt, int column)
{
  const unsigned char* text = sqlite3_column_text(stmt, column);
  return text ? reinterpret_cast<const char*>(text) : "";
}
}  // namespace

InboxRepository::InboxRepository(std::shared_ptr<DatabaseConnection> conn)
    : db_conn(std::move(conn))
{
}

std::vector<InboxMessage> InboxRepository::loadAll() const
{
  std::vector<InboxMessage> messages;
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "SELECT id, game_date, category, title_key, body_key, args, is_read, "
      "player_id, team_id FROM InboxMessages ORDER BY id;");
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    InboxMessage message;
    message.id = static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 0));
    message.date = dateFromInt(sqlite3_column_int(stmt, 1));
    message.category = static_cast<InboxCategory>(sqlite3_column_int(stmt, 2));
    message.title_key = columnText(stmt, 3);
    message.body_key = columnText(stmt, 4);
    if (const auto args =
            nlohmann::json::parse(columnText(stmt, 5), nullptr, false);
        args.is_array())
    {
      for (const auto& arg : args)
      {
        if (arg.is_string()) message.args.push_back(arg.get<std::string>());
      }
    }
    message.read = sqlite3_column_int(stmt, 6) != 0;
    if (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
      message.player_id = static_cast<PlayerID>(sqlite3_column_int64(stmt, 7));
    if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
      message.team_id = static_cast<TeamID>(sqlite3_column_int(stmt, 8));
    messages.push_back(std::move(message));
  }
  sqlite3_finalize(stmt);
  return messages;
}

void InboxRepository::replaceAll(
    const std::vector<InboxMessage>& messages) const
{
  sqlite3_exec(db_conn->getRaw(), "DELETE FROM InboxMessages;", nullptr,
               nullptr, nullptr);
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "INSERT INTO InboxMessages (id, game_date, category, title_key, "
      "body_key, args, is_read, player_id, team_id) VALUES "
      "(?, ?, ?, ?, ?, ?, ?, ?, ?);");
  for (const InboxMessage& message : messages)
  {
    const std::string args = nlohmann::json(message.args).dump();
    sqlite3_bind_int64(stmt, 1, message.id);
    sqlite3_bind_int(stmt, 2, dateToInt(message.date));
    sqlite3_bind_int(stmt, 3,
                     static_cast<int>(std::to_underlying(message.category)));
    sqlite3_bind_text(stmt, 4, message.title_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, message.body_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, args.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, message.read ? 1 : 0);
    if (message.player_id.has_value())
      sqlite3_bind_int64(stmt, 8, *message.player_id);
    else
      sqlite3_bind_null(stmt, 8);
    if (message.team_id.has_value())
      sqlite3_bind_int(stmt, 9, *message.team_id);
    else
      sqlite3_bind_null(stmt, 9);
    db_conn->executeStep(stmt);
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
  }
  sqlite3_finalize(stmt);
}

InboxView InboxRepository::loadView() const
{
  InboxView view;
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "SELECT tab, category, unread_only, followed_only, player_id, team_id "
      "FROM InboxView WHERE id = 1;");
  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    const int tab = sqlite3_column_int(stmt, 0);
    const int category = sqlite3_column_int(stmt, 1);
    view.tab = static_cast<std::int8_t>(tab >= -1 && tab <= 1 ? tab : -1);
    view.category = static_cast<std::int8_t>(
        category >= 0 && category < static_cast<int>(std::to_underlying(
                                        InboxCategory::COUNT))
            ? category
            : -1);
    view.unread_only = sqlite3_column_int(stmt, 2) != 0;
    view.followed_only = sqlite3_column_int(stmt, 3) != 0;
    if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
      view.player_id = static_cast<PlayerID>(sqlite3_column_int64(stmt, 4));
    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
      view.team_id = static_cast<TeamID>(sqlite3_column_int(stmt, 5));
  }
  sqlite3_finalize(stmt);
  return view;
}

void InboxRepository::saveView(const InboxView& view) const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "INSERT OR REPLACE INTO InboxView (id, tab, category, unread_only, "
      "followed_only, player_id, team_id) VALUES (1, ?, ?, ?, ?, ?, ?);");
  sqlite3_bind_int(stmt, 1, view.tab);
  sqlite3_bind_int(stmt, 2, view.category);
  sqlite3_bind_int(stmt, 3, view.unread_only ? 1 : 0);
  sqlite3_bind_int(stmt, 4, view.followed_only ? 1 : 0);
  if (view.player_id.has_value())
    sqlite3_bind_int64(stmt, 5, *view.player_id);
  else
    sqlite3_bind_null(stmt, 5);
  if (view.team_id.has_value())
    sqlite3_bind_int(stmt, 6, *view.team_id);
  else
    sqlite3_bind_null(stmt, 6);
  db_conn->executeStep(stmt);
  sqlite3_finalize(stmt);
}
