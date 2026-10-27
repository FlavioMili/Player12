// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "database/world/world_combine.h"

#include <algorithm>
#include <optional>
#include <set>
#include <unordered_set>
#include <utility>

#include "database/world/world_schema.h"

using json = nlohmann::json;

namespace World
{
namespace
{
constexpr std::size_t slot(Kind kind) { return static_cast<std::size_t>(kind); }

std::optional<std::int64_t> intField(const json& entity, std::string_view key)
{
  if (!entity.is_object())
  {
    return std::nullopt;
  }
  const auto found = entity.find(std::string(key));
  if (found == entity.end() || !found->is_number_integer())
  {
    return std::nullopt;
  }
  return found->get<std::int64_t>();
}

std::string textField(const json& entity, std::string_view key)
{
  if (!entity.is_object())
  {
    return {};
  }
  const auto found = entity.find(std::string(key));
  return found != entity.end() && found->is_string() ? found->get<std::string>()
                                                     : std::string();
}

std::string countryKey(const Document& document, std::optional<std::int64_t> id)
{
  if (!id)
  {
    return {};
  }
  const json* country = document.find(Kind::Country, *id);
  return country != nullptr ? normalise(textField(*country, "name"))
                            : std::string();
}

/** Country id an entity belongs to (0 when it has none or is a country). */
std::int64_t countryOf(Kind kind, const json& entity, const Document& document)
{
  switch (kind)
  {
    case Kind::City:
    case Kind::League:
      return intField(entity, "country_id").value_or(0);
    case Kind::Club:
    {
      const auto league = intField(entity, "league_id");
      const json* found =
          league ? document.find(Kind::League, *league) : nullptr;
      return found != nullptr ? intField(*found, "country_id").value_or(0) : 0;
    }
    default:
      return 0;
  }
}

std::string playerKey(const json& entity, std::optional<int> season)
{
  const std::int64_t age = intField(entity, "age").value_or(0);
  const std::int64_t birth = season ? *season - age : age;
  return normalise(textField(entity, "first_name") + " " +
                   textField(entity, "last_name")) +
         "|" + textField(entity, "nationality") + "|" + std::to_string(birth);
}

std::string keyFor(Kind kind, const json& entity, const Document& document,
                   std::optional<int> season)
{
  switch (kind)
  {
    case Kind::Language:
    case Kind::Confederation:
    case Kind::Region:
    case Kind::Country:
      return normalise(textField(entity, "name"));
    case Kind::City:
    case Kind::League:
      return countryKey(document, intField(entity, "country_id")) + "|" +
             normalise(textField(entity, "name"));
    case Kind::Club:
    {
      const auto league = intField(entity, "league_id");
      const json* found =
          league ? document.find(Kind::League, *league) : nullptr;
      const std::string country =
          found != nullptr
              ? countryKey(document, intField(*found, "country_id"))
              : std::string();
      return country + "|" + normalise(textField(entity, "name"));
    }
    case Kind::Travel:
    {
      const std::int64_t from = intField(entity, "from").value_or(-1);
      const std::int64_t to = intField(entity, "to").value_or(-1);
      return std::to_string(std::min(from, to)) + "-" +
             std::to_string(std::max(from, to));
    }
    case Kind::Player:
      return playerKey(entity, season);
    case Kind::COUNT:
      break;
  }
  return {};
}

enum class Pass : std::uint8_t
{
  OtherKinds,
  SameKind,
};

/** Rewrites reference fields to combined ids; unknown targets are dropped
 * (a player's club becomes 0, a free agent). */
void rewriteReferences(Kind kind, json& entity, const CombineResult& result,
                       Pass pass)
{
  for (const FieldSpec& spec : fields(kind))
  {
    if (spec.type != FieldType::Reference || !spec.reference)
    {
      continue;
    }
    if ((*spec.reference == kind) != (pass == Pass::SameKind))
    {
      continue;
    }
    const json* value = get(entity, spec.path);
    if (value == nullptr || !value->is_number_integer())
    {
      continue;
    }
    const auto& map = result.id_map[slot(*spec.reference)];
    const auto found = map.find(value->get<std::int64_t>());
    if (found != map.end())
    {
      set(entity, spec.path, found->second);
    }
    else if (spec.path == "team_id")
    {
      set(entity, spec.path, 0);
    }
    else
    {
      set(entity, spec.path, json());
    }
  }
}

const std::set<std::string_view>& clubTokens()
{
  static const std::set<std::string_view> tokens = {
      "fc", "cf", "ac", "sc", "afc", "ssc", "as",  "us",
      "cd", "sv", "fk", "sk", "if",  "bk",  "cfc", "rc"};
  return tokens;
}

std::string stripClubTokens(std::string_view name)
{
  std::string out;
  std::size_t start = 0;
  while (start <= name.size())
  {
    std::size_t end = name.find(' ', start);
    if (end == std::string_view::npos)
    {
      end = name.size();
    }
    const std::string_view word = name.substr(start, end - start);
    if (!word.empty() && !clubTokens().contains(word))
    {
      if (!out.empty())
      {
        out += ' ';
      }
      out += word;
    }
    start = end + 1;
  }
  return out;
}

bool withinDistance(std::string_view first, std::string_view second,
                    std::size_t limit)
{
  if (first.size() > second.size())
  {
    std::swap(first, second);
  }
  if (second.size() - first.size() > limit)
  {
    return false;
  }
  std::vector<std::size_t> previous(first.size() + 1);
  std::vector<std::size_t> current(first.size() + 1);
  for (std::size_t i = 0; i <= first.size(); ++i)
  {
    previous[i] = i;
  }
  for (std::size_t row = 1; row <= second.size(); ++row)
  {
    current[0] = row;
    std::size_t best = current[0];
    for (std::size_t col = 1; col <= first.size(); ++col)
    {
      const std::size_t cost = first[col - 1] == second[row - 1] ? 0 : 1;
      current[col] = std::min(
          {previous[col] + 1, current[col - 1] + 1, previous[col - 1] + cost});
      best = std::min(best, current[col]);
    }
    if (best > limit)
    {
      return false;
    }
    std::swap(previous, current);
  }
  return previous[first.size()] <= limit;
}

bool lookAlikePrepared(std::string_view first, std::string_view first_stripped,
                       std::string_view second,
                       std::string_view second_stripped)
{
  if (first == second)
  {
    return false;
  }
  if (!first_stripped.empty() && first_stripped == second_stripped)
  {
    return true;
  }
  constexpr std::size_t MIN_LENGTH = 8;
  return first.size() >= MIN_LENGTH && second.size() >= MIN_LENGTH &&
         withinDistance(first, second, 2);
}

struct Record
{
  json entity;
  std::int64_t old_id = -1;
  std::int64_t target = -1;  ///< Combined id; for travel a row position.
  bool matched = false;
};

struct Candidate
{
  std::int64_t id;
  std::string name;
  std::string stripped;
};

void flagLookAlikes(Kind kind, const Document& document,
                    const std::vector<std::int64_t>& base_ids,
                    const std::vector<std::int64_t>& new_ids,
                    CombineResult& result)
{
  std::unordered_map<std::int64_t, std::vector<Candidate>> buckets;
  for (const std::int64_t id : base_ids)
  {
    const json* entity = document.find(kind, id);
    if (entity == nullptr)
    {
      continue;
    }
    std::string name = normalise(entityName(kind, *entity));
    std::string stripped = stripClubTokens(name);
    buckets[countryOf(kind, *entity, document)].push_back(
        {id, std::move(name), std::move(stripped)});
  }
  for (const std::int64_t id : new_ids)
  {
    const json* entity = document.find(kind, id);
    if (entity == nullptr)
    {
      continue;
    }
    const auto bucket = buckets.find(countryOf(kind, *entity, document));
    if (bucket == buckets.end())
    {
      continue;
    }
    const std::string name = normalise(entityName(kind, *entity));
    const std::string stripped = stripClubTokens(name);
    for (const Candidate& other : bucket->second)
    {
      if (lookAlikePrepared(name, stripped, other.name, other.stripped))
      {
        result.possible_duplicates.push_back({kind, other.id, id});
      }
    }
  }
}

void combineKind(Kind kind, const Document& base, const Document& incoming,
                 std::optional<int> base_season,
                 std::optional<int> incoming_season, CombineResult& result)
{
  Document& combined = result.document;
  const bool travel = kind == Kind::Travel;
  const json& base_rows = combined.section(kind);
  const std::size_t base_count = base_rows.size();

  // identity -> id (travel: row position) over what the combined document has.
  std::unordered_map<std::string, std::int64_t> known;
  known.reserve(base_count + incoming.count(kind));
  std::unordered_set<std::int64_t> used;
  std::vector<std::int64_t> base_ids;
  for (std::size_t i = 0; i < base_count; ++i)
  {
    const json& entity = base_rows[i];
    const std::int64_t id = entityId(entity);
    if (!travel && id < 0)
    {
      continue;
    }
    known.try_emplace(keyFor(kind, entity, combined, base_season),
                      travel ? static_cast<std::int64_t>(i) : id);
    if (!travel)
    {
      used.insert(id);
      base_ids.push_back(id);
    }
  }

  const std::int64_t shift = base.highestId(kind);
  std::int64_t cursor = minId(kind);
  const auto allocate = [&](std::int64_t old_id) -> std::optional<std::int64_t>
  {
    const std::int64_t shifted = old_id + shift;
    if (old_id >= 0 && shifted >= minId(kind) && shifted <= maxId(kind) &&
        !used.contains(shifted))
    {
      return shifted;
    }
    while (cursor <= maxId(kind) && used.contains(cursor))
    {
      ++cursor;
    }
    if (cursor > maxId(kind))
    {
      return std::nullopt;
    }
    return cursor;
  };

  // Pass 1: decide the combined id of every incoming entity.
  const json& rows = incoming.section(kind);
  std::vector<Record> records;
  records.reserve(rows.size());
  std::vector<std::size_t> fresh;  // records that are new, in order
  std::unordered_map<std::int64_t, std::size_t> fresh_by_id;
  for (const json& row : rows)
  {
    Record record;
    record.entity = row;
    record.old_id = entityId(row);
    rewriteReferences(kind, record.entity, result, Pass::OtherKinds);
    if (travel &&
        (!intField(record.entity, "from") || !intField(record.entity, "to")))
    {
      continue;  // an end of the route was not part of the incoming world
    }
    const std::string key =
        keyFor(kind, record.entity, combined, incoming_season);
    const auto match = known.find(key);
    if (match != known.end())
    {
      record.matched = true;
      record.target = match->second;
    }
    else if (travel)
    {
      record.target = static_cast<std::int64_t>(base_count + fresh.size());
      known.emplace(key, record.target);
      fresh.push_back(records.size());
    }
    else
    {
      // No free identifier left in the kind's range: the entity cannot be
      // represented in the combined world, so it is left out.
      const auto id = allocate(record.old_id);
      if (!id)
      {
        continue;
      }
      used.insert(*id);
      record.target = *id;
      known.emplace(key, *id);
      fresh_by_id.emplace(*id, records.size());
      fresh.push_back(records.size());
    }
    if (!travel && record.old_id >= 0)
    {
      result.id_map[slot(kind)][record.old_id] = record.target;
    }
    records.push_back(std::move(record));
  }

  // Pass 2: same-kind references (parent leagues), then merge or add.
  std::vector<json> additions;
  std::vector<std::int64_t> new_ids;
  for (Record& record : records)
  {
    rewriteReferences(kind, record.entity, result, Pass::SameKind);
    if (!travel)
    {
      record.entity["id"] = record.target;
    }
    const json* existing = nullptr;
    if (record.matched)
    {
      if (travel)
      {
        const auto position = static_cast<std::size_t>(record.target);
        existing = position < base_count
                       ? &base_rows[position]
                       : &records[fresh[position - base_count]].entity;
        if (intField(*existing, "from") == intField(record.entity, "to") &&
            intField(*existing, "to") == intField(record.entity, "from"))
        {
          const json from = record.entity["from"];
          record.entity["from"] = record.entity["to"];
          record.entity["to"] = from;
        }
      }
      else
      {
        existing = combined.find(kind, record.target);
        if (existing == nullptr)
        {
          existing = &records[fresh_by_id.at(record.target)].entity;
        }
      }
      const std::vector<std::string> differing =
          differingPaths(*existing, record.entity);
      if (differing.empty())
      {
        ++result.merged[slot(kind)];
        continue;
      }
      Conflict conflict;
      conflict.kind = kind;
      conflict.base_id = record.target;
      conflict.incoming = record.entity;
      conflict.base_source = base.manifest().name;
      conflict.incoming_source = incoming.manifest().name;
      conflict.fields = differing;
      result.conflicts.push_back(std::move(conflict));
      continue;
    }
    ++result.added[slot(kind)];
    if (!travel)
    {
      new_ids.push_back(record.target);
    }
    additions.push_back(record.entity);
  }

  json& section = combined.section(kind);
  for (json& entity : additions)
  {
    section.push_back(std::move(entity));
  }
  combined.invalidate();

  if (kind != Kind::Player && kind != Kind::Travel)
  {
    flagLookAlikes(kind, combined, base_ids, new_ids, result);
  }
}

void unionInto(json& target, const json& extra)
{
  if (target.is_array() && extra.is_array())
  {
    std::set<std::string> seen;
    for (const json& item : target)
    {
      seen.insert(item.dump());
    }
    for (const json& item : extra)
    {
      if (seen.insert(item.dump()).second)
      {
        target.push_back(item);
      }
    }
  }
  else if (target.is_object() && extra.is_object())
  {
    for (const auto& [key, value] : extra.items())
    {
      if (target.contains(key))
      {
        unionInto(target[key], value);
      }
      else
      {
        target[key] = value;
      }
    }
  }
}

void combineNames(const Document& incoming, Document& combined)
{
  for (const bool first : {true, false})
  {
    const json& extra = incoming.names(first);
    if (extra.is_null())
    {
      continue;
    }
    json pool = combined.names(first);
    if (pool.is_null())
    {
      pool = extra;
    }
    else
    {
      unionInto(pool, extra);
    }
    combined.setNames(first, std::move(pool));
  }
}

bool isKnownKey(const std::string& key)
{
  if (key == "manifest" || key == "first_names" || key == "last_names")
  {
    return true;
  }
  return std::ranges::any_of(
      KINDS, [&key](Kind kind) { return key == sectionKey(kind); });
}

void replaceEntity(Document& document, const Conflict& conflict)
{
  if (conflict.kind == Kind::Travel)
  {
    json& rows = document.section(Kind::Travel);
    const auto position = static_cast<std::size_t>(conflict.base_id);
    if (conflict.base_id >= 0 && position < rows.size())
    {
      rows[position] = conflict.incoming;
    }
    return;
  }
  if (json* entity = document.find(conflict.kind, conflict.base_id))
  {
    *entity = conflict.incoming;
    (*entity)["id"] = conflict.base_id;
    document.invalidate();
  }
}
}  // namespace

std::string identityKey(Kind kind, const json& entity, const Document& document)
{
  return keyFor(kind, entity, document, document.manifest().season);
}

bool lookAlike(std::string_view first, std::string_view second)
{
  return lookAlikePrepared(first, stripClubTokens(first), second,
                           stripClubTokens(second));
}

CombineResult combine(const Document& base, const Document& incoming)
{
  CombineResult result;
  result.document = base;
  const std::optional<int> base_season = base.manifest().season;
  const std::optional<int> incoming_season = incoming.manifest().season;
  // Birth years are only comparable when both worlds say what year they are.
  const bool use_season = base_season && incoming_season;
  for (const Kind kind : KINDS)
  {
    combineKind(kind, base, incoming, use_season ? base_season : std::nullopt,
                use_season ? incoming_season : std::nullopt, result);
  }
  combineNames(incoming, result.document);
  for (const auto& [key, value] : incoming.root().items())
  {
    if (isKnownKey(key) || result.document.root().contains(key))
    {
      continue;
    }
    // Unknown keys travel with the world; only fill what the base lacks.
    Document& document = result.document;
    json root = document.root();
    root[key] = value;
    document = Document(std::move(root));
  }
  return result;
}

void resolve(Document& document, const Conflict& conflict, Choice choice)
{
  switch (choice)
  {
    case Choice::KeepBase:
      return;
    case Choice::KeepIncoming:
      replaceEntity(document, conflict);
      return;
    case Choice::KeepBoth:
    {
      if (conflict.kind == Kind::Travel)
      {
        return;
      }
      const auto id = document.freeId(conflict.kind);
      if (!id)
      {
        return;
      }
      json entity = conflict.incoming;
      entity["id"] = *id;
      document.add(conflict.kind, std::move(entity));
      return;
    }
  }
}

void resolveFields(Document& document, const Conflict& conflict,
                   const std::vector<std::string>& incoming_paths)
{
  json* entity = nullptr;
  if (conflict.kind == Kind::Travel)
  {
    json& rows = document.section(Kind::Travel);
    const auto position = static_cast<std::size_t>(conflict.base_id);
    if (conflict.base_id >= 0 && position < rows.size())
    {
      entity = &rows[position];
    }
  }
  else
  {
    entity = document.find(conflict.kind, conflict.base_id);
  }
  if (entity == nullptr)
  {
    return;
  }
  for (const std::string& path : incoming_paths)
  {
    const json* value = get(conflict.incoming, path);
    set(*entity, path, value != nullptr ? *value : json());
  }
  document.invalidate();
}

void resolveAll(Document& document, std::vector<Conflict>& conflicts,
                const std::string& source)
{
  for (const Conflict& conflict : conflicts)
  {
    resolve(document, conflict,
            conflict.incoming_source == source ? Choice::KeepIncoming
                                               : Choice::KeepBase);
  }
  conflicts.clear();
}

void mergeInto(Document& document, Kind kind, std::int64_t keep_id,
               std::int64_t drop_id)
{
  if (kind == Kind::Travel || keep_id == drop_id)
  {
    return;
  }
  for (const Kind other : KINDS)
  {
    for (const FieldSpec& spec : fields(other))
    {
      if (spec.type != FieldType::Reference || spec.reference != kind)
      {
        continue;
      }
      for (json& entity : document.section(other))
      {
        const json* value = get(entity, spec.path);
        if (value != nullptr && value->is_number_integer() &&
            value->get<std::int64_t>() == drop_id)
        {
          set(entity, spec.path, keep_id);
        }
      }
    }
  }
  document.remove(kind, drop_id);
  if (kind == Kind::City)
  {
    // A route between two merged cities would now start and end in the same
    // place.
    json& rows = document.section(Kind::Travel);
    json kept = json::array();
    for (const json& row : rows)
    {
      const auto from = intField(row, "from");
      if (!from || from != intField(row, "to"))
      {
        kept.push_back(row);
      }
    }
    rows = std::move(kept);
  }
  document.invalidate();
}
}  // namespace World
