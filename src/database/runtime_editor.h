// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

/** Schema-driven data editing. No football schema or SQL supplied by the UI.
 * The caller owns the connection and serializes access. SQLite constraints
 * and registered validators run in the same savepoint as the mutations. */
namespace RuntimeEditor
{
/** A rejected edit or SQLite operation. UI/controller boundaries translate
 * this exception to a visible error while preserving the current career. */
class Error : public std::runtime_error
{
 public:
  using std::runtime_error::runtime_error;
};

using Blob = std::vector<unsigned char>;
using Value =
    std::variant<std::monostate, std::int64_t, double, std::string, Blob>;

struct Column
{
  std::string name;
  std::string type;
  std::optional<std::string> default_sql;
  bool required = false;
  int primary_key = 0;
  int hidden = 0;  ///< Nonzero: engine-managed/generated, never written.
  bool operator==(const Column&) const = default;
};

struct Relation
{
  std::string column;
  std::string table;
  std::string target;
};

struct Table
{
  std::string name;
  std::string definition;
  std::vector<Column> columns;
  std::vector<Relation> relations;
  std::vector<std::string> identity;  ///< Unshadowed rowid or primary key.
  std::vector<std::string> persistent_identity;  ///< Prefer natural unique key.
  bool writable = false;
};

struct Row
{
  std::vector<Value> values;
  std::vector<Value> identity;
};

struct Page
{
  Table table;
  std::vector<Row> rows;
  std::string schema;  ///< Complete schema fingerprint, not just a version.
  bool more = false;
};

enum class Operation
{
  Update,
  Insert,
  Delete
};
struct Change
{
  std::string table;
  std::string schema;
  Operation operation = Operation::Update;
  Row original;
  std::map<std::string, Value, std::less<>>
      fields;  ///< Omitted inserts use defaults.
};

using Validator = std::function<void(sqlite3*, const std::vector<Change>&)>;

std::string quote(const std::string& identifier);
std::string schema(sqlite3* db);
int schemaVersion(sqlite3* db);
std::vector<Table> catalog(sqlite3* db);
Page read(sqlite3* db, const std::string& name, int offset = 0,
          const std::string& search = "", int limit = 50,
          const std::map<std::string, Value, std::less<>>& filter = {});
/** Throws on stale schema/record, invalid types, constraints or validation.
 * All changes roll back together. Existing legacy FK violations may remain;
 * newly introduced violations are rejected. */
void apply(sqlite3* db, const std::vector<Change>& changes,
           const std::vector<Validator>& validators = {},
           bool defer_relations = false);
void copy(sqlite3* source, sqlite3* destination);
std::string display(const Value& item);
Value parse(int type, const std::string& input);
int storageType(const Value& item);

/** Preserves extra columns while existing repositories rewrite known tables.
 * The canonical schema is discovered from this build's schema, not a fixed
 * editor allowlist. Restore matches stable keys and never resurrects rows. */
class Extensions
{
 public:
  Extensions(sqlite3* active, sqlite3* canonical);
  void capture(sqlite3* db);
  void restore(sqlite3* db) const;
  bool empty() const { return tables.empty(); }

 private:
  struct ExtraTable
  {
    Table table;
    std::vector<std::string> columns;
    std::vector<Row> rows;
  };
  std::vector<ExtraTable> tables;
};

/** Learns JSON object members discarded by a runtime serialization roundtrip.
 * Keeps only those members, so normal gameplay can still remove owned keys. */
class Payloads
{
 public:
  Payloads() = default;
  Payloads(sqlite3* original, sqlite3* normalized);
  void restore(sqlite3* db) const;

 private:
  struct Payload
  {
    std::string table;
    std::string column;
    std::vector<std::string> keys;
    std::vector<Value> identity;
    std::string missing;
  };
  void captureColumn(sqlite3* original, sqlite3* normalized, const Table& table,
                     const Column& column);
  std::vector<Payload> payloads;
};
}  // namespace RuntimeEditor
