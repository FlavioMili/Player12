// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "database/world/world_document.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <utility>

#include "global/types.h"
#include "model/world_generation.h"

using json = nlohmann::json;

namespace World
{
namespace
{
constexpr std::size_t slot(Kind kind) { return static_cast<std::size_t>(kind); }

const json& nullJson()
{
  static const json value;
  return value;
}

const json& emptyArray()
{
  static const json value = json::array();
  return value;
}

std::string text(const json& entity, const char* key)
{
  const auto found = entity.find(key);
  return found != entity.end() && found->is_string() ? found->get<std::string>()
                                                     : std::string();
}
}  // namespace

std::string_view sectionKey(Kind kind)
{
  switch (kind)
  {
    case Kind::Language:
      return "languages";
    case Kind::Confederation:
      return "confederations";
    case Kind::Region:
      return "regions";
    case Kind::Country:
      return "countries";
    case Kind::City:
      return "cities";
    case Kind::Travel:
      return "travel";
    case Kind::League:
      return "leagues";
    case Kind::Club:
      return "clubs";
    case Kind::Player:
      return "players";
    case Kind::COUNT:
      break;
  }
  return "";
}

std::string_view kindLabel(Kind kind)
{
  switch (kind)
  {
    case Kind::Language:
      return "WORLD_KIND_LANGUAGES";
    case Kind::Confederation:
      return "WORLD_KIND_CONFEDERATIONS";
    case Kind::Region:
      return "WORLD_KIND_REGIONS";
    case Kind::Country:
      return "WORLD_KIND_COUNTRIES";
    case Kind::City:
      return "WORLD_KIND_CITIES";
    case Kind::Travel:
      return "WORLD_KIND_TRAVEL";
    case Kind::League:
      return "WORLD_KIND_LEAGUES";
    case Kind::Club:
      return "WORLD_KIND_CLUBS";
    case Kind::Player:
      return "WORLD_KIND_PLAYERS";
    case Kind::COUNT:
      break;
  }
  return "";
}

std::int64_t minId(Kind kind) { return kind == Kind::Travel ? 0 : 1; }

std::int64_t maxId(Kind kind)
{
  switch (kind)
  {
    case Kind::Language:
      return std::numeric_limits<LanguageID>::max();
    case Kind::Confederation:
      return std::numeric_limits<ConfederationID>::max();
    case Kind::Region:
      return std::numeric_limits<RegionID>::max();
    case Kind::Country:
      return std::numeric_limits<CountryID>::max();
    case Kind::League:
      // 250 and above are the continental competitions.
      return 249;
    case Kind::City:
      return std::numeric_limits<std::uint16_t>::max();
    case Kind::Club:
      return std::numeric_limits<TeamID>::max();
    case Kind::Player:
      return std::numeric_limits<std::int32_t>::max();
    case Kind::Travel:
    case Kind::COUNT:
      break;
  }
  return 0;
}

std::string_view parentKey(Kind kind)
{
  switch (kind)
  {
    case Kind::City:
    case Kind::League:
      return "country_id";
    case Kind::Club:
      return "league_id";
    case Kind::Player:
      return "team_id";
    default:
      return "";
  }
}

std::optional<Kind> parentKind(Kind kind)
{
  switch (kind)
  {
    case Kind::City:
    case Kind::League:
      return Kind::Country;
    case Kind::Club:
      return Kind::League;
    case Kind::Player:
      return Kind::Club;
    default:
      return std::nullopt;
  }
}

std::string entityName(Kind kind, const json& entity)
{
  if (!entity.is_object())
  {
    return {};
  }
  if (kind == Kind::Player)
  {
    std::string name = text(entity, "first_name");
    const std::string last = text(entity, "last_name");
    if (!name.empty() && !last.empty())
    {
      name += ' ';
    }
    return name + last;
  }
  if (kind == Kind::Travel)
  {
    const auto id = [&entity](const char* key)
    {
      const auto found = entity.find(key);
      return found != entity.end() && found->is_number_integer()
                 ? std::to_string(found->get<std::int64_t>())
                 : std::string("?");
    };
    return id("from") + " - " + id("to");
  }
  return text(entity, "name");
}

std::int64_t entityId(const json& entity)
{
  if (!entity.is_object())
  {
    return -1;
  }
  const auto found = entity.find("id");
  return found != entity.end() && found->is_number_integer()
             ? found->get<std::int64_t>()
             : -1;
}

std::string normalise(std::string_view name)
{
  const std::string folded = NamePool::normalizeName(name);
  std::string out;
  out.reserve(folded.size());
  bool space = false;
  for (const char c : folded)
  {
    const auto byte = static_cast<unsigned char>(c);
    if (byte >= 0x80 || std::isalnum(byte) != 0)
    {
      if (space && !out.empty())
      {
        out += ' ';
      }
      space = false;
      out += c;
    }
    else if (c == ' ' || c == '-' || c == '_' || c == '/')
    {
      space = true;
    }
  }
  return out;
}

Document::Document() : Document(json::object()) {}

Document::Document(json root) : data(std::move(root))
{
  if (!data.is_object())
  {
    data = json::object();
  }
  if (!data.contains("manifest") || !data["manifest"].is_object())
  {
    data["manifest"] = json::object();
  }
}

Document Document::empty(std::string name)
{
  Document document;
  Manifest manifest;
  manifest.name = std::move(name);
  document.setManifest(manifest);
  return document;
}

Manifest Document::manifest() const
{
  const json& item = data.at("manifest");
  Manifest manifest;
  manifest.name = text(item, "name");
  manifest.author = text(item, "author");
  manifest.version = text(item, "version");
  manifest.description = text(item, "description");
  manifest.min_game_version = text(item, "min_game_version");
  if (const auto schema = item.find("schema");
      schema != item.end() && schema->is_number_integer())
  {
    manifest.schema = schema->get<int>();
  }
  if (const auto season = item.find("season");
      season != item.end() && season->is_number_integer())
  {
    manifest.season = season->get<int>();
  }
  return manifest;
}

void Document::setManifest(const Manifest& manifest)
{
  json& item = data["manifest"];
  const auto set = [&item](const char* key, const std::string& value)
  {
    if (value.empty())
    {
      item.erase(key);
    }
    else
    {
      item[key] = value;
    }
  };
  set("name", manifest.name);
  set("author", manifest.author);
  set("version", manifest.version);
  set("description", manifest.description);
  set("min_game_version", manifest.min_game_version);
  item["schema"] = manifest.schema;
  if (manifest.season)
  {
    item["season"] = *manifest.season;
  }
  else
  {
    item.erase("season");
  }
}

json& Document::section(Kind kind)
{
  json& array = data[std::string(sectionKey(kind))];
  if (!array.is_array())
  {
    array = json::array();
  }
  return array;
}

const json& Document::section(Kind kind) const
{
  const auto found = data.find(std::string(sectionKey(kind)));
  return found != data.end() && found->is_array() ? *found : emptyArray();
}

std::size_t Document::count(Kind kind) const { return section(kind).size(); }

bool Document::hasSection(Kind kind) const
{
  return data.contains(std::string(sectionKey(kind)));
}

const json& Document::names(bool first) const
{
  const auto found = data.find(first ? "first_names" : "last_names");
  return found != data.end() ? *found : nullJson();
}

void Document::setNames(bool first, json pool)
{
  const char* key = first ? "first_names" : "last_names";
  if (pool.is_null())
  {
    data.erase(key);
  }
  else
  {
    data[key] = std::move(pool);
  }
}

const Document::Index& Document::index(Kind kind) const
{
  auto& cached = indexes.at(slot(kind));
  if (!cached)
  {
    Index built;
    const json& array = section(kind);
    built.reserve(array.size());
    for (std::size_t i = 0; i < array.size(); ++i)
    {
      if (const std::int64_t id = entityId(array[i]); id >= 0)
      {
        built.try_emplace(id, i);
      }
    }
    cached = std::move(built);
  }
  return *cached;
}

std::optional<std::size_t> Document::indexOf(Kind kind, std::int64_t id) const
{
  const Index& lookup = index(kind);
  const auto found = lookup.find(id);
  if (found == lookup.end())
  {
    return std::nullopt;
  }
  return found->second;
}

json* Document::find(Kind kind, std::int64_t id)
{
  const auto position = indexOf(kind, id);
  return position ? &section(kind)[*position] : nullptr;
}

const json* Document::find(Kind kind, std::int64_t id) const
{
  const auto position = indexOf(kind, id);
  return position ? &section(kind)[*position] : nullptr;
}

std::int64_t Document::highestId(Kind kind) const
{
  std::int64_t highest = 0;
  for (const auto& [id, position] : index(kind))
  {
    (void)position;
    highest = std::max(highest, id);
  }
  return highest;
}

std::optional<std::int64_t> Document::freeId(Kind kind) const
{
  if (kind == Kind::Travel)
  {
    return std::nullopt;
  }
  const std::int64_t next = std::max(highestId(kind) + 1, minId(kind));
  if (next <= maxId(kind))
  {
    return next;
  }
  const Index& lookup = index(kind);
  for (std::int64_t id = minId(kind); id <= maxId(kind); ++id)
  {
    if (!lookup.contains(id))
    {
      return id;
    }
  }
  return std::nullopt;
}

json& Document::add(Kind kind, json entity)
{
  json& array = section(kind);
  array.push_back(std::move(entity));
  invalidate();
  return array.back();
}

bool Document::remove(Kind kind, std::int64_t id)
{
  const auto position = indexOf(kind, id);
  if (!position)
  {
    return false;
  }
  json& array = section(kind);
  array.erase(array.begin() + static_cast<std::ptrdiff_t>(*position));
  invalidate();
  return true;
}

void Document::invalidate() const
{
  for (auto& cached : indexes)
  {
    cached.reset();
  }
}

void Document::setSection(Kind kind, json array)
{
  data[std::string(sectionKey(kind))] =
      array.is_array() ? std::move(array) : json::array();
  invalidate();
}

Document Document::withoutPlayers() const
{
  json copy = data;
  copy.erase(std::string(sectionKey(Kind::Player)));
  return Document(std::move(copy));
}
}  // namespace World
