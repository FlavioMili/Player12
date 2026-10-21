// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "database/runtime_editor.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace RuntimeEditor
{
namespace
{
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

void check(sqlite3* db, int result)
{
  if (result != SQLITE_OK && result != SQLITE_ROW && result != SQLITE_DONE)
  {
    throw Error(sqlite3_errmsg(db));
  }
}

Statement prepare(sqlite3* db, const std::string& sql)
{
  sqlite3_stmt* raw = nullptr;
  const int result = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr);
  Statement statement(raw, &sqlite3_finalize);
  check(db, result);
  return statement;
}

void exec(sqlite3* db, const char* sql)
{
  check(db, sqlite3_exec(db, sql, nullptr, nullptr, nullptr));
}

std::string text(sqlite3_stmt* stmt, int index)
{
  const auto* data = sqlite3_column_text(stmt, index);
  if (data == nullptr)
  {
    return {};
  }
  // SQLite exposes UTF-8 as unsigned bytes; std::string owns the copy.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const auto* chars = reinterpret_cast<const char*>(data);
  return {chars, static_cast<std::size_t>(sqlite3_column_bytes(stmt, index))};
}

Value value(sqlite3_stmt* stmt, int index)
{
  switch (sqlite3_column_type(stmt, index))
  {
    case SQLITE_INTEGER:
      return static_cast<std::int64_t>(sqlite3_column_int64(stmt, index));
    case SQLITE_FLOAT:
      return sqlite3_column_double(stmt, index);
    case SQLITE_TEXT:
      return text(stmt, index);
    case SQLITE_BLOB:
    {
      const int bytes = sqlite3_column_bytes(stmt, index);
      if (bytes == 0)
      {
        return Blob{};
      }
      const auto* data =
          static_cast<const unsigned char*>(sqlite3_column_blob(stmt, index));
      const std::span bytes_view(data, static_cast<std::size_t>(bytes));
      return Blob(bytes_view.begin(), bytes_view.end());
    }
    default:
      return std::monostate{};
  }
}

void bindValue(sqlite3* db, sqlite3_stmt* stmt, int index, const Value& item)
{
  const int result = std::visit(
      [stmt, index](const auto& value) -> int
      {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::monostate>)
        {
          return sqlite3_bind_null(stmt, index);
        }
        else if constexpr (std::is_same_v<T, std::int64_t>)
        {
          return sqlite3_bind_int64(stmt, index, value);
        }
        else if constexpr (std::is_same_v<T, double>)
        {
          if (!std::isfinite(value))
          {
            throw Error("Non-finite number");
          }
          return sqlite3_bind_double(stmt, index, value);
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
          return sqlite3_bind_text64(stmt, index, value.data(), value.size(),
                                     SQLITE_TRANSIENT, SQLITE_UTF8);
        }
        else
        {
          return sqlite3_bind_blob64(
              stmt, index,
              value.empty() ? "" : static_cast<const void*>(value.data()),
              value.size(), SQLITE_TRANSIENT);
        }
      },
      item);
  check(db, result);
}

std::string join(const std::vector<std::string>& names)
{
  std::string result;
  for (const auto& name : names)
  {
    if (!result.empty())
    {
      result += ',';
    }
    result += quote(name);
  }
  return result;
}

Table find(sqlite3* db, const std::string& name)
{
  const auto tables = catalog(db);
  if (const auto table = std::ranges::find(tables, name, &Table::name);
      table != tables.end())
  {
    return *table;
  }
  throw Error("Table no longer exists: " + name);
}

std::multiset<std::string, std::less<>> foreignIssues(sqlite3* db)
{
  // WITHOUT ROWID violations have a NULL row identifier. Preserve their
  // multiplicity so another invalid row cannot hide behind a legacy violation.
  std::multiset<std::string, std::less<>> issues;
  auto stmt = prepare(db, "PRAGMA foreign_key_check");
  for (int rc = sqlite3_step(stmt.get()); rc != SQLITE_DONE;
       rc = sqlite3_step(stmt.get()))
  {
    check(db, rc);
    std::string issue;
    for (int i = 0; i < 4; ++i)
    {
      const auto part = display(value(stmt.get(), i));
      issue += std::format("{}:{}", part.size(), part);
    }
    issues.insert(std::move(issue));
  }
  return issues;
}

std::vector<Column> readColumns(sqlite3* db, const std::string& name)
{
  std::vector<Column> result;
  auto columns = prepare(db,
                         "SELECT name,type,\"notnull\",dflt_value,pk,hidden "
                         "FROM pragma_table_xinfo(?)");
  bindValue(db, columns.get(), 1, name);
  for (int cr = sqlite3_step(columns.get()); cr != SQLITE_DONE;
       cr = sqlite3_step(columns.get()))
  {
    check(db, cr);
    Column column;
    column.name = text(columns.get(), 0);
    column.type = text(columns.get(), 1);
    column.required = sqlite3_column_int(columns.get(), 2) != 0;
    if (sqlite3_column_type(columns.get(), 3) != SQLITE_NULL)
    {
      column.default_sql = text(columns.get(), 3);
    }
    column.primary_key = sqlite3_column_int(columns.get(), 4);
    constexpr int HIDDEN_COLUMN_INDEX = 5;
    column.hidden = sqlite3_column_int(columns.get(), HIDDEN_COLUMN_INDEX);
    result.push_back(std::move(column));
  }
  return result;
}

// Natural unique keys survive INSERT OR REPLACE with generated integer IDs.
// Nullable unique keys cannot identify records: SQLite permits repeated NULLs.
std::vector<std::string> naturalKey(sqlite3* db, const Table& table)
{
  auto indexes =
      prepare(db,
              "SELECT name FROM pragma_index_list(?) WHERE \"unique\"=1 AND "
              "partial=0 AND origin='u' ORDER BY seq");
  bindValue(db, indexes.get(), 1, table.name);
  for (int ir = sqlite3_step(indexes.get()); ir != SQLITE_DONE;
       ir = sqlite3_step(indexes.get()))
  {
    check(db, ir);
    auto names =
        prepare(db, "SELECT name FROM pragma_index_info(?) ORDER BY seqno");
    bindValue(db, names.get(), 1, text(indexes.get(), 0));
    std::vector<std::string> natural;
    for (int nr = sqlite3_step(names.get()); nr != SQLITE_DONE;
         nr = sqlite3_step(names.get()))
    {
      check(db, nr);
      const auto name = text(names.get(), 0);
      if (const auto column =
              std::ranges::find(table.columns, name, &Column::name);
          column == table.columns.end() || !column->required)
      {
        natural.clear();
        break;
      }
      natural.push_back(name);
    }
    if (!natural.empty())
    {
      return natural;
    }
  }
  return {};
}

std::vector<std::string> rowIdentity(const Table& table, bool ordinary,
                                     bool without_rowid)
{
  if (ordinary && !without_rowid)
  {
    for (const auto* alias : {"_rowid_", "rowid", "oid"})
    {
      if (std::ranges::none_of(
              table.columns, [alias](const Column& col)
              { return sqlite3_stricmp(col.name.c_str(), alias) == 0; }))
      {
        return {alias};
      }
    }
  }
  return table.persistent_identity;
}

void insertSql(const Table& table, const Change& change, std::string& sql,
               std::vector<Value>& bindings)
{
  sql = "INSERT INTO " + quote(table.name);
  if (change.fields.empty())
  {
    sql += " DEFAULT VALUES";
  }
  else
  {
    std::vector<std::string> names;
    std::string parameters;
    for (const auto& [name, item] : change.fields)
    {
      names.push_back(name);
      if (!parameters.empty())
      {
        parameters += ',';
      }
      parameters += '?';
      bindings.push_back(item);
    }
    sql += " (" + join(names) + ") VALUES (" + parameters + ")";
  }
}

std::string recordMutationSql(const Table& table, const Change& change,
                              std::vector<Value>& bindings)
{
  std::string sql;
  if (change.original.identity.size() != table.identity.size() ||
      change.original.values.size() != table.columns.size())
  {
    throw Error("Invalid original record");
  }
  sql = change.operation == Operation::Delete ? "DELETE FROM " : "UPDATE ";
  sql += quote(table.name);
  if (change.operation == Operation::Update)
  {
    if (change.fields.empty())
    {
      return {};
    }
    sql += " SET ";
    for (const auto& [name, item] : change.fields)
    {
      if (!bindings.empty())
      {
        sql += ',';
      }
      sql += quote(name) + "=?";
      bindings.push_back(item);
    }
  }
  sql += " WHERE ";
  for (std::size_t i = 0; i < table.identity.size(); ++i)
  {
    if (i != 0)
    {
      sql += " AND ";
    }
    sql += quote(table.identity.at(i)) + " IS ?";
    bindings.push_back(change.original.identity.at(i));
  }
  // Full original values detect edits by another writer, including NULL/BLOB.
  for (std::size_t i = 0; i < table.columns.size(); ++i)
  {
    sql += " AND " + quote(table.columns.at(i).name) + " IS ?";
    bindings.push_back(change.original.values.at(i));
  }
  return sql;
}

void mutate(sqlite3* db, const Change& change)
{
  const Table table = find(db, change.table);
  if (!table.writable)
  {
    throw Error("Read-only table: " + table.name);
  }
  for (const auto& [name, item] : change.fields)
  {
    (void)item;
    const auto column = std::ranges::find(table.columns, name, &Column::name);
    if (column == table.columns.end() || column->hidden != 0)
    {
      throw Error("Column is missing or generated: " + name);
    }
  }
  std::string sql;
  std::vector<Value> bindings;
  if (change.operation == Operation::Insert)
  {
    insertSql(table, change, sql, bindings);
  }
  else
  {
    sql = recordMutationSql(table, change, bindings);
    if (sql.empty())
    {
      return;
    }
  }
  auto stmt = prepare(db, sql);
  for (std::size_t i = 0; i < bindings.size(); ++i)
  {
    bindValue(db, stmt.get(), static_cast<int>(i + 1), bindings.at(i));
  }
  check(db, sqlite3_step(stmt.get()));
  if (sqlite3_changes(db) != 1)
  {
    throw Error("Record changed or no longer exists");
  }
}
}  // namespace

std::string quote(const std::string& identifier)
{
  if (identifier.contains('\0'))
  {
    throw Error("Invalid identifier");
  }
  std::string result = "\"";
  for (char letter : identifier)
  {
    result += letter;
    if (letter == '"')
    {
      result += letter;
    }
  }
  return result + "\"";
}

std::string schema(sqlite3* db)
{
  auto stmt = prepare(db,
                      "SELECT type,name,tbl_name,coalesce(sql,'') FROM "
                      "sqlite_schema ORDER BY type,name");
  std::string result;
  for (int rc = sqlite3_step(stmt.get()); rc != SQLITE_DONE;
       rc = sqlite3_step(stmt.get()))
  {
    check(db, rc);
    for (int i = 0; i < 4; ++i)
    {
      const auto part = text(stmt.get(), i);
      result += std::format("{}:{}", part.size(), part);
    }
  }
  return result;
}

std::vector<Table> catalog(sqlite3* db)
{
  std::vector<Table> tables;
  auto list = prepare(db, "PRAGMA main.table_list");
  for (int rc = sqlite3_step(list.get()); rc != SQLITE_DONE;
       rc = sqlite3_step(list.get()))
  {
    check(db, rc);
    Table table;
    table.name = text(list.get(), 1);
    const auto type = text(list.get(), 2);
    if (table.name.starts_with("sqlite_") || type == "shadow")
    {
      continue;
    }
    const bool ordinary = type == "table";
    const bool without_rowid = sqlite3_column_int(list.get(), 4) != 0;
    auto definition =
        prepare(db, "SELECT coalesce(sql,'') FROM sqlite_schema WHERE name=?");
    bindValue(db, definition.get(), 1, table.name);
    check(db, sqlite3_step(definition.get()));
    table.definition = text(definition.get(), 0);
    table.columns = readColumns(db, table.name);
    auto keys = table.columns;
    std::ranges::sort(keys, {}, &Column::primary_key);
    for (const auto& key : keys)
    {
      if (key.primary_key != 0)
      {
        table.persistent_identity.push_back(key.name);
      }
    }
    table.identity = rowIdentity(table, ordinary, without_rowid);
    if (auto natural = naturalKey(db, table); !natural.empty())
    {
      table.persistent_identity = std::move(natural);
    }
    if (table.persistent_identity.empty())
    {
      table.persistent_identity = table.identity;
    }
    auto foreign = prepare(
        db,
        R"(SELECT "from","table","to" FROM pragma_foreign_key_list(?) ORDER BY id,seq)");
    bindValue(db, foreign.get(), 1, table.name);
    for (int fr = sqlite3_step(foreign.get()); fr != SQLITE_DONE;
         fr = sqlite3_step(foreign.get()))
    {
      check(db, fr);
      table.relations.push_back({
          .column = text(foreign.get(), 0),
          .table = text(foreign.get(), 1),
          .target = text(foreign.get(), 2),
      });
    }
    table.writable = ordinary && !table.identity.empty();
    tables.push_back(std::move(table));
  }
  std::ranges::sort(tables, {}, &Table::name);
  return tables;
}

int schemaVersion(sqlite3* db)
{
  auto stmt = prepare(db, "PRAGMA main.schema_version");
  check(db, sqlite3_step(stmt.get()));
  return sqlite3_column_int(stmt.get(), 0);
}

Page read(sqlite3* db, const std::string& name, int offset,
          const std::string& search, int limit,
          const std::map<std::string, Value, std::less<>>& filter)
{
  constexpr int MAX_PAGE_SIZE = 1000;
  if (offset < 0 || limit < 1 || limit > MAX_PAGE_SIZE)
  {
    throw Error("Invalid page size");
  }
  Page page;
  page.table = find(db, name);
  page.schema = schema(db);
  std::vector<std::string> columns;
  columns.reserve(page.table.columns.size());
  for (const auto& col : page.table.columns)
  {
    columns.push_back(col.name);
  }
  std::string sql = "SELECT " + join(columns);
  if (!page.table.identity.empty())
  {
    sql += "," + join(page.table.identity);
  }
  sql += " FROM " + quote(name) + " WHERE 1=1";
  std::vector<Value> parameters;
  for (const auto& [column, item] : filter)
  {
    if (std::ranges::find(columns, column) == columns.end())
    {
      throw Error("Unknown filter column: " + column);
    }
    sql += " AND " + quote(column) + " IS ?";
    parameters.push_back(item);
  }
  if (!search.empty())
  {
    sql += " AND (";
    for (std::size_t i = 0; i < columns.size(); ++i)
    {
      if (i != 0)
      {
        sql += " OR ";
      }
      sql +=
          "instr(lower(CAST(" + quote(columns.at(i)) + " AS TEXT)),lower(?))>0";
      parameters.emplace_back(search);
    }
    sql += ')';
  }
  if (!page.table.identity.empty())
  {
    sql += " ORDER BY " + join(page.table.identity);
  }
  sql += " LIMIT ? OFFSET ?";
  parameters.emplace_back(static_cast<std::int64_t>(limit + 1));
  parameters.emplace_back(static_cast<std::int64_t>(offset));
  auto stmt = prepare(db, sql);
  for (std::size_t i = 0; i < parameters.size(); ++i)
  {
    bindValue(db, stmt.get(), static_cast<int>(i + 1), parameters.at(i));
  }
  for (int rc = sqlite3_step(stmt.get()); rc != SQLITE_DONE;
       rc = sqlite3_step(stmt.get()))
  {
    check(db, rc);
    if (page.rows.size() == static_cast<std::size_t>(limit))
    {
      page.more = true;
      break;
    }
    Row row;
    for (std::size_t i = 0; i < columns.size(); ++i)
    {
      row.values.push_back(value(stmt.get(), static_cast<int>(i)));
    }
    for (std::size_t i = 0; i < page.table.identity.size(); ++i)
    {
      row.identity.push_back(
          value(stmt.get(), static_cast<int>(columns.size() + i)));
    }
    page.rows.push_back(std::move(row));
  }
  return page;
}

void apply(sqlite3* db, const std::vector<Change>& changes,
           const std::vector<Validator>& validators, bool defer_relations)
{
  const auto fingerprint = schema(db);
  for (const auto& change : changes)
  {
    if (change.schema != fingerprint)
    {
      throw Error("Schema changed; reopen the record");
    }
  }
  const auto before = foreignIssues(db);
  exec(db, "SAVEPOINT runtime_editor");
  try
  {
    for (const auto& change : changes)
    {
      mutate(db, change);
    }
    if (const auto after = foreignIssues(db);
        !defer_relations && !std::ranges::includes(before, after))
    {
      throw Error("Changes introduce a foreign key violation");
    }
    for (const auto& validator : validators)
    {
      validator(db, changes);
    }
    exec(db, "RELEASE runtime_editor");
  }
  catch (...)
  {
    sqlite3_exec(db, "ROLLBACK TO runtime_editor; RELEASE runtime_editor",
                 nullptr, nullptr, nullptr);
    throw;
  }
}

void copy(sqlite3* source, sqlite3* destination)
{
  if (source == destination)
  {
    throw Error("Cannot copy a database onto itself");
  }
  sqlite3_backup* backup =
      sqlite3_backup_init(destination, "main", source, "main");
  if (backup == nullptr)
  {
    throw Error(sqlite3_errmsg(destination));
  }
  const int result = sqlite3_backup_step(backup, -1);
  const int finished = sqlite3_backup_finish(backup);
  check(destination, result);
  check(destination, finished);
}

int storageType(const Value& item)
{
  constexpr std::array TYPES = {
      SQLITE_NULL, SQLITE_INTEGER, SQLITE_FLOAT, SQLITE_TEXT, SQLITE_BLOB,
  };
  return TYPES.at(item.index());
}

std::string display(const Value& item)
{
  if (const auto* integer = std::get_if<std::int64_t>(&item))
  {
    return std::to_string(*integer);
  }
  if (const auto* number = std::get_if<double>(&item))
  {
    constexpr std::size_t NUMBER_BUFFER_SIZE = 64;
    std::array<char, NUMBER_BUFFER_SIZE> buffer{};
    const auto converted = std::to_chars(
        buffer.data(), buffer.end(), *number, std::chars_format::general,
        std::numeric_limits<double>::max_digits10);
    if (converted.ec != std::errc())
    {
      throw Error("Cannot format number");
    }
    return {buffer.data(), converted.ptr};
  }
  if (const auto* string = std::get_if<std::string>(&item))
  {
    return *string;
  }
  if (const auto* blob = std::get_if<Blob>(&item))
  {
    constexpr unsigned HEX_MASK = 0xFU;
    constexpr std::string_view HEX = "0123456789abcdef";
    std::string result;
    result.reserve(blob->size() * 2);
    for (auto byte : *blob)
    {
      const auto raw = static_cast<unsigned>(byte);
      result += HEX.at(raw >> 4U);
      result += HEX.at(raw & HEX_MASK);
    }
    return result;
  }
  return "NULL";
}

Value parse(int type, const std::string& input)
{
  if (type == SQLITE_NULL)
  {
    return std::monostate{};
  }
  if (type == SQLITE_TEXT)
  {
    return input;
  }
  if (type == SQLITE_INTEGER)
  {
    std::int64_t result = 0;
    if (const auto parsed =
            std::from_chars(input.data(), std::to_address(input.end()), result);
        parsed.ec != std::errc() || parsed.ptr != std::to_address(input.end()))
    {
      throw Error("Expected a 64-bit integer");
    }
    return result;
  }
  if (type == SQLITE_FLOAT)
  {
    double result = 0;
    if (const auto parsed =
            std::from_chars(input.data(), std::to_address(input.end()), result);
        parsed.ec != std::errc() ||
        parsed.ptr != std::to_address(input.end()) || !std::isfinite(result))
    {
      throw Error("Expected a finite number");
    }
    return result;
  }
  if (type == SQLITE_BLOB)
  {
    if (input.size() % 2 != 0)
    {
      throw Error("Expected hexadecimal byte pairs");
    }
    Blob result;
    for (std::size_t i = 0; i < input.size(); i += 2)
    {
      unsigned byte = 0;
      const auto pair = input.substr(i, 2);
      if (const auto parsed = std::from_chars(
              pair.data(), std::to_address(pair.end()), byte, 16);
          parsed.ec != std::errc() || parsed.ptr != std::to_address(pair.end()))
      {
        throw Error("Expected hexadecimal byte pairs");
      }
      result.push_back(static_cast<unsigned char>(byte));
    }
    return result;
  }
  throw Error("Unknown storage type");
}

Extensions::Extensions(sqlite3* active, sqlite3* canonical)
{
  const auto base = catalog(canonical);
  for (auto& table : catalog(active))
  {
    if (!table.writable)
    {
      continue;
    }
    const auto known = std::ranges::find(base, table.name, &Table::name);
    if (known == base.end())
    {
      continue;  // Unknown tables are never rewritten.
    }
    ExtraTable extra;
    extra.table = std::move(table);
    for (const auto& col : extra.table.columns)
    {
      if (col.hidden == 0 &&
          std::ranges::find(known->columns, col.name, &Column::name) ==
              known->columns.end())
      {
        extra.columns.push_back(col.name);
      }
    }
    if (!extra.columns.empty())
    {
      tables.push_back(std::move(extra));
    }
  }
}

void Extensions::capture(sqlite3* db)
{
  for (auto& extra : tables)
  {
    extra.rows.clear();
    auto stmt = prepare(db, "SELECT " + join(extra.columns) + "," +
                                join(extra.table.persistent_identity) +
                                " FROM " + quote(extra.table.name));
    for (int rc = sqlite3_step(stmt.get()); rc != SQLITE_DONE;
         rc = sqlite3_step(stmt.get()))
    {
      check(db, rc);
      Row row;
      for (std::size_t i = 0; i < extra.columns.size(); ++i)
      {
        row.values.push_back(value(stmt.get(), static_cast<int>(i)));
      }
      for (std::size_t i = 0; i < extra.table.persistent_identity.size(); ++i)
      {
        row.identity.push_back(
            value(stmt.get(), static_cast<int>(extra.columns.size() + i)));
      }
      extra.rows.push_back(std::move(row));
    }
  }
}

void Extensions::restore(sqlite3* db) const
{
  for (const auto& extra : tables)
  {
    std::string sql = "UPDATE " + quote(extra.table.name) + " SET ";
    for (std::size_t i = 0; i < extra.columns.size(); ++i)
    {
      if (i != 0)
      {
        sql += ',';
      }
      sql += quote(extra.columns.at(i)) + "=?";
    }
    sql += " WHERE ";
    for (std::size_t i = 0; i < extra.table.persistent_identity.size(); ++i)
    {
      if (i != 0)
      {
        sql += " AND ";
      }
      sql += quote(extra.table.persistent_identity.at(i)) + " IS ?";
    }
    auto stmt = prepare(db, sql);
    for (const auto& row : extra.rows)
    {
      int index = 1;
      for (const auto& item : row.values)
      {
        bindValue(db, stmt.get(), index++, item);
      }
      for (const auto& item : row.identity)
      {
        bindValue(db, stmt.get(), index++, item);
      }
      check(db, sqlite3_step(stmt.get()));
      check(db, sqlite3_reset(stmt.get()));
    }
  }
}

namespace
{
using Json = nlohmann::json;
Json missingMembers(const Json& original, const Json& normalized)
{
  Json result = Json::object();
  if (!original.is_object() || !normalized.is_object())
  {
    return result;
  }
  for (const auto& [key, item] : original.items())
  {
    if (!normalized.contains(key))
    {
      result.emplace(key, item);
    }
    else if (item.is_object() && normalized.at(key).is_object())
    {
      auto nested = missingMembers(item, normalized.at(key));
      if (!nested.empty())
      {
        result.emplace(key, std::move(nested));
      }
    }
  }
  return result;
}

void mergeMissing(Json& current, const Json& missing)
{
  if (!current.is_object() || !missing.is_object())
  {
    return;
  }
  for (const auto& [key, item] : missing.items())
  {
    if (!current.contains(key))
    {
      current.emplace(key, item);
    }
    else if (item.is_object())
    {
      mergeMissing(current.at(key), item);
    }
  }
}

std::string predicate(const std::vector<std::string>& keys)
{
  std::string result;
  for (const auto& key : keys)
  {
    if (!result.empty())
    {
      result += " AND ";
    }
    result += quote(key) + " IS ?";
  }
  return result;
}
}  // namespace

Payloads::Payloads(sqlite3* original, sqlite3* normalized)
{
  for (const auto& table : catalog(original))
  {
    if (!table.writable || table.persistent_identity.empty())
    {
      continue;
    }
    for (const auto& column : table.columns)
    {
      if (column.hidden != 0)
      {
        continue;
      }
      captureColumn(original, normalized, table, column);
    }
  }
}

void Payloads::captureColumn(sqlite3* original, sqlite3* normalized,
                             const Table& table, const Column& column)
{
  const auto field = quote(column.name);
  const auto selection = std::format(
      "SELECT {0},{1} FROM {2} WHERE typeof({0})='text' "
      "AND substr(ltrim({0}),1,1)='{{'",
      field, join(table.persistent_identity), quote(table.name));
  auto source = prepare(original, selection);
  auto target =
      prepare(normalized, "SELECT " + field + " FROM " + quote(table.name) +
                              " WHERE " + predicate(table.persistent_identity));
  for (int rc = sqlite3_step(source.get()); rc != SQLITE_DONE;
       rc = sqlite3_step(source.get()))
  {
    check(original, rc);
    Payload payload;
    payload.table = table.name;
    payload.column = column.name;
    payload.keys = table.persistent_identity;
    for (std::size_t i = 0; i < payload.keys.size(); ++i)
    {
      payload.identity.push_back(value(source.get(), static_cast<int>(i + 1)));
      bindValue(normalized, target.get(), static_cast<int>(i + 1),
                payload.identity.back());
    }
    const int tr = sqlite3_step(target.get());
    check(normalized, tr);
    const auto output = tr == SQLITE_ROW
                            ? Json::parse(text(target.get(), 0), nullptr, false)
                            : Json();
    check(normalized, sqlite3_reset(target.get()));
    const auto input = Json::parse(text(source.get(), 0), nullptr, false);
    if (const auto missing = missingMembers(input, output); !missing.empty())
    {
      payload.missing = missing.dump();
      payloads.push_back(std::move(payload));
    }
  }
}

void Payloads::restore(sqlite3* db) const
{
  for (const auto& payload : payloads)
  {
    auto stmt = prepare(db, "SELECT " + quote(payload.column) + " FROM " +
                                quote(payload.table) + " WHERE " +
                                predicate(payload.keys));
    for (std::size_t i = 0; i < payload.identity.size(); ++i)
    {
      bindValue(db, stmt.get(), static_cast<int>(i + 1),
                payload.identity.at(i));
    }
    const int rc = sqlite3_step(stmt.get());
    check(db, rc);
    if (rc == SQLITE_DONE)
    {
      continue;
    }
    auto current = Json::parse(text(stmt.get(), 0), nullptr, false);
    if (!current.is_object())
    {
      continue;
    }
    mergeMissing(current, Json::parse(payload.missing));
    stmt.reset();
    auto update = prepare(db, "UPDATE " + quote(payload.table) + " SET " +
                                  quote(payload.column) + "=? WHERE " +
                                  predicate(payload.keys));
    bindValue(db, update.get(), 1, current.dump());
    for (std::size_t i = 0; i < payload.identity.size(); ++i)
    {
      bindValue(db, update.get(), static_cast<int>(i + 2),
                payload.identity.at(i));
    }
    check(db, sqlite3_step(update.get()));
  }
}
}  // namespace RuntimeEditor
