// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <format>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "model/runtime_editor_validation.h"

namespace
{
std::shared_ptr<DatabaseConnection> clone(
    const std::shared_ptr<DatabaseConnection>& source)
{
  auto result = SaveManager::createWorkingCopy();
  RuntimeEditor::copy(source->getRaw(), result->getRaw());
  return result;
}

constexpr double FLOAT_ROUNDTRIP_TOLERANCE =
    4 * static_cast<double>(std::numeric_limits<float>::epsilon());

bool equivalentJson(const nlohmann::json& a, const nlohmann::json& b)
{
  if (a == b)
  {
    return true;
  }
  if (a.is_number() && b.is_number() &&
      (a.is_number_float() || b.is_number_float()))
  {
    const auto left = a.get<double>();
    const auto right = b.get<double>();
    return std::abs(left - right) <=
           FLOAT_ROUNDTRIP_TOLERANCE * std::max(1.0, std::abs(left));
  }
  if (a.type() != b.type() || a.size() != b.size())
  {
    return false;
  }
  if (a.is_object())
  {
    return std::ranges::all_of(a.items(),
                               [&b](const auto& item)
                               {
                                 return b.contains(item.key()) &&
                                        equivalentJson(item.value(),
                                                       b.at(item.key()));
                               });
  }
  if (a.is_array())
  {
    return std::ranges::equal(a, b, equivalentJson);
  }
  return false;
}

bool equivalent(const RuntimeEditor::Value& a, const RuntimeEditor::Value& b)
{
  if (a == b)
  {
    return true;
  }
  if (std::holds_alternative<std::int64_t>(a) &&
      std::holds_alternative<std::int64_t>(b))
  {
    return false;
  }
  const auto number =
      [](const RuntimeEditor::Value& value) -> std::optional<double>
  {
    if (const auto* integer = std::get_if<std::int64_t>(&value))
    {
      return static_cast<double>(*integer);
    }
    if (const auto* real = std::get_if<double>(&value))
    {
      return *real;
    }
    return std::nullopt;
  };
  if (const auto left = number(a), right = number(b);
      left.has_value() && right.has_value())
  {
    return std::abs(*left - *right) <=
           FLOAT_ROUNDTRIP_TOLERANCE * std::max(1.0, std::abs(*left));
  }
  const auto* left = std::get_if<std::string>(&a);
  const auto* right = std::get_if<std::string>(&b);
  if ((left == nullptr) || (right == nullptr))
  {
    return false;
  }
  const auto lj = nlohmann::json::parse(*left, nullptr, false);
  const auto rj = nlohmann::json::parse(*right, nullptr, false);
  return !lj.is_discarded() && !rj.is_discarded() && equivalentJson(lj, rj);
}

std::map<std::string, RuntimeEditor::Value, std::less<>> resultingKey(
    const RuntimeEditor::Table& table, const RuntimeEditor::Change& change)
{
  std::map<std::string, RuntimeEditor::Value, std::less<>> key;
  // Updates can change identity fields. Match on the resulting stable key.
  for (const auto& name : table.persistent_identity)
  {
    if (const auto edited = change.fields.find(name);
        edited != change.fields.end())
    {
      key[name] = edited->second;
    }
    else if (const auto col = std::ranges::find(table.columns, name,
                                                &RuntimeEditor::Column::name);
             col != table.columns.end() &&
             change.original.values.size() == table.columns.size())
    {
      key[name] = change.original.values.at(
          static_cast<std::size_t>(col - table.columns.begin()));
    }
  }
  if (key.empty())
  {
    // For inserts with generated keys, submitted fields identify the row.
    key = change.fields;
  }
  return key;
}

void verifyRoundtrip(sqlite3* expected, sqlite3* normalized,
                     const std::vector<RuntimeEditor::Change>& changes)
{
  const auto tables = RuntimeEditor::catalog(expected);
  for (const auto& change : changes)
  {
    const auto table =
        std::ranges::find(tables, change.table, &RuntimeEditor::Table::name);
    if (table == tables.end())
    {
      throw RuntimeEditor::Error("Edited table disappeared");
    }
    const auto key = resultingKey(*table, change);
    if (change.operation == RuntimeEditor::Operation::Delete)
    {
      // Validate the final batch state; a later insert may reuse this key.
      if (!key.empty() &&
          !RuntimeEditor::read(expected, change.table, 0, "", 1, key)
               .rows.empty())
      {
        continue;
      }
      if (key.empty() ||
          !RuntimeEditor::read(normalized, change.table, 0, "", 1, key)
               .rows.empty())
      {
        throw RuntimeEditor::Error(change.table +
                                   ": runtime would recreate this record");
      }
      continue;
    }
    const auto before =
        RuntimeEditor::read(expected, change.table, 0, "", 2, key);
    const auto after =
        RuntimeEditor::read(normalized, change.table, 0, "", 2, key);
    if (before.rows.size() != 1 || after.rows.size() != 1)
    {
      throw RuntimeEditor::Error(
          change.table + ": record cannot be represented by the runtime");
    }
    for (const auto& [name, item] : change.fields)
    {
      (void)item;
      const auto col =
          std::ranges::find(table->columns, name, &RuntimeEditor::Column::name);
      const auto index = static_cast<std::size_t>(col - table->columns.begin());
      if (!equivalent(before.rows.front().values.at(index),
                      after.rows.front().values.at(index)))
      {
        throw RuntimeEditor::Error(
            change.table + "." + name +
            ": runtime would normalize or discard this value");
      }
    }
  }
}
}  // namespace

std::shared_ptr<GameController::EditorSession> GameController::beginEditor()
{
  if (const std::unique_lock lock(runtime_mutex, std::try_to_lock);
      lock.owns_lock() && game && editor_session.expired())
  {
    game->saveGame();
    auto session = std::make_shared<EditorSession>();
    session->database = clone(db_conn);
    session->source = db_conn;
    session->source_changes = sqlite3_total_changes(db_conn->getRaw());
    session->source_schema = RuntimeEditor::schema(db_conn->getRaw());
    editor_session = session;
    return session;
  }
  throw RuntimeEditor::Error("Career is busy or an editor is already open");
}

void GameController::cancelEditor()
{
  const std::scoped_lock lock(runtime_mutex);
  editor_session.reset();
}

void GameController::registerEditorValidator(RuntimeEditor::Validator validator)
{
  const std::scoped_lock lock(runtime_mutex);
  editor_validators.push_back(std::move(validator));
}

void GameController::replaceEditorDatabase(
    std::shared_ptr<DatabaseConnection> connection)
{
  auto data = std::make_shared<GameData>();
  auto replacement = std::make_unique<Game>(data, connection);
  replacement->setSimulationThreads(game->getSimulationThreads());
  auto listings = data->loadAllTransferListings();
  for (auto& [id, listing] : listings)
  {
    if (const auto player = data->getPlayer(id))
    {
      listing.seller_team_id = player->get().getTeamId();
      data->getPlayers().at(id).setTransferStatus(TransferStatus::Listed);
    }
  }
  game = std::move(replacement);
  gamedata = std::move(data);
  db_conn = std::move(connection);
  transfer_listings = std::move(listings);
  ++editor_revision;
}

bool GameController::applyEditor(
    const std::shared_ptr<EditorSession>& session,
    const std::vector<RuntimeEditor::Change>& changes, std::string& error)
{
  const std::scoped_lock lock(runtime_mutex);
  error.clear();
  try
  {
    if (!session || !db_conn)
    {
      throw RuntimeEditor::Error("No active editor session");
    }
    const auto active = editor_session.lock();
    const auto source = session->source;
    if (active.get() != session.get() || source.get() != db_conn.get() ||
        sqlite3_total_changes(db_conn->getRaw()) != session->source_changes ||
        RuntimeEditor::schema(db_conn->getRaw()) != session->source_schema)
    {
      throw RuntimeEditor::Error("Career changed; reopen the editor");
    }
    if (changes.empty())
    {
      throw RuntimeEditor::Error("No changes to apply");
    }
    auto candidate = clone(source);
    auto validators = editor_validators;
    validators.emplace(validators.begin(), EditorValidation::validate);
    RuntimeEditor::apply(candidate->getRaw(), changes, validators);
    auto expected = clone(candidate);
    // A private reconstruction and save detect lossy casts, invalid payloads,
    // model normalization and unsupported values before publishing anything.
    auto data = std::make_shared<GameData>();
    auto probe = std::make_unique<Game>(data, candidate);
    probe->saveGame();
    verifyRoundtrip(expected->getRaw(), candidate->getRaw(), changes);
    probe.reset();
    if (sqlite3_exec(expected->getRaw(),
                     "UPDATE save_meta SET edited=1 WHERE id=1", nullptr,
                     nullptr, nullptr) != SQLITE_OK)
    {
      throw RuntimeEditor::Error(sqlite3_errmsg(expected->getRaw()));
    }
    const auto backup = clone(db_conn);
    replaceEditorDatabase(expected);
    editor_backup = backup;
    editor_session.reset();
    Logger::info(std::format("Runtime editor applied {} record changes",
                             changes.size()));
    return true;
  }
  // Feature validators, database loaders and JSON codecs have distinct
  // exception types. Contain them here so a rejected draft never escapes into
  // the game loop.
  // A runtime replacement crosses independently implemented loaders/codecs.
  // Preserve the live career even when one throws an unfamiliar exception type.
  catch (const std::exception& failure)
  {
    error = failure.what();
    return false;
  }
}

bool GameController::undoEditor(std::string& error)
{
  const std::scoped_lock lock(runtime_mutex);
  error.clear();
  if (!editor_backup)
  {
    error = "No editor recovery snapshot";
    return false;
  }
  try
  {
    auto previous = clone(db_conn);
    replaceEditorDatabase(clone(editor_backup));
    editor_backup = std::move(previous);
    editor_session.reset();
    return true;
  }
  // Feature validators, database loaders and JSON codecs have distinct
  // exception types. Contain them here so a rejected draft never escapes into
  // the game loop.
  // A runtime replacement crosses independently implemented loaders/codecs.
  // Preserve the live career even when one throws an unfamiliar exception type.
  catch (const std::exception& failure)
  {
    error = failure.what();
    return false;
  }
}
