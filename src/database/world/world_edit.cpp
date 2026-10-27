// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "database/world/world_edit.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <unordered_set>

#include "database/world/world_schema.h"

using json = nlohmann::json;

namespace World
{
namespace
{
constexpr std::array<const char*, 9> STANDARD_STATS = {
    "Pace",        "Shooting", "Passing", "Dribbling",  "Defending",
    "Physicality", "Stamina",  "Vision",  "Goalkeeping"};
constexpr int DEFAULT_STAT = 50;

std::int64_t integer(const json& entity, const char* key)
{
  const auto found = entity.find(key);
  return found != entity.end() && found->is_number_integer()
             ? found->get<std::int64_t>()
             : 0;
}

bool hasName(const Document& document, Kind kind, const std::string& name)
{
  const std::string wanted = normalise(name);
  return std::ranges::any_of(
      document.section(kind), [&](const json& entity)
      { return normalise(entityName(kind, entity)) == wanted; });
}

std::string uniqueName(const Document& document, Kind kind,
                       const std::string& base, int first = 2)
{
  std::string name = base;
  for (int n = first; hasName(document, kind, name); ++n)
  {
    name = base + " (" + std::to_string(n) + ")";
  }
  return name;
}

/** First three-letter code from @p seed's letters (padded with X) that no
 * club uses; falls back to scanning AAA..ZZZ. */
std::string uniqueShortName(const Document& document, const std::string& seed)
{
  std::unordered_set<std::string> used;
  for (const json& club : document.section(Kind::Club))
  {
    std::string code = club.value("short_name", std::string());
    std::ranges::transform(
        code, code.begin(),
        [](char c)
        {
          return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        });
    used.insert(code);
  }
  std::string letters;
  for (const char c : seed)
  {
    if (std::isalpha(static_cast<unsigned char>(c)) != 0)
    {
      letters += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
  }
  while (letters.size() < 3)
  {
    letters += 'X';
  }
  std::string code = letters.substr(0, 3);
  for (std::size_t i = 3; used.contains(code) && i < letters.size(); ++i)
  {
    code = {letters[0], letters[1], letters[i]};
  }
  for (int n = 0; used.contains(code) && n < 26 * 26 * 26; ++n)
  {
    code = {static_cast<char>('A' + n / 676),
            static_cast<char>('A' + (n / 26) % 26),
            static_cast<char>('A' + n % 26)};
  }
  return code;
}

void clearReference(json& section, const char* key, std::int64_t id,
                    std::size_t& counter)
{
  for (json& entity : section)
  {
    if (integer(entity, key) != id)
    {
      continue;
    }
    entity.erase(key);
    ++counter;
  }
}

void clearReferences(Document& document, Kind kind, std::int64_t id,
                     RemoveImpact& result)
{
  switch (kind)
  {
    case Kind::Country:
      clearReference(document.section(Kind::City), "country_id", id,
                     result.cities);
      clearReference(document.section(Kind::League), "country_id", id,
                     result.leagues);
      break;
    case Kind::City:
      clearReference(document.section(Kind::Club), "city_id", id, result.clubs);
      break;
    case Kind::League:
      clearReference(document.section(Kind::League), "parent_league", id,
                     result.leagues);
      clearReference(document.section(Kind::Club), "league_id", id,
                     result.clubs);
      break;
    case Kind::Club:
      clearReference(document.section(Kind::Player), "team_id", id,
                     result.players);
      break;
    case Kind::Travel:
    case Kind::Player:
    case Kind::COUNT:
      break;
  }
}

RemoveImpact apply(Document& document, Kind kind, std::int64_t id)
{
  RemoveImpact result;
  clearReferences(document, kind, id, result);
  if (kind == Kind::City)
  {
    json& travel = document.section(Kind::Travel);
    const auto before = travel.size();
    std::erase_if(
        travel.get_ref<json::array_t&>(), [id](const json& row)
        { return integer(row, "from") == id || integer(row, "to") == id; });
    result.travel = before - travel.size();
  }
  return result;
}
}  // namespace

std::int64_t duplicate(Document& document, Kind kind, std::int64_t id)
{
  if (kind == Kind::Travel)
  {
    json& travel = document.section(kind);
    if (id < 0 || static_cast<std::size_t>(id) >= travel.size())
    {
      return -1;
    }
    json row = travel.at(static_cast<std::size_t>(id));
    travel.push_back(std::move(row));
    return static_cast<std::int64_t>(travel.size()) - 1;
  }
  const json* source = document.find(kind, id);
  const auto fresh = document.freeId(kind);
  if (source == nullptr || !fresh)
  {
    return -1;
  }
  json copy = *source;
  copy["id"] = *fresh;
  if (kind != Kind::Player)
  {
    const std::string base = entityName(kind, copy);
    copy["name"] = uniqueName(document, kind, base);
  }
  if (kind == Kind::Club)
  {
    copy["short_name"] = uniqueShortName(document, copy.value("name", ""));
  }
  document.add(kind, std::move(copy));
  return *fresh;
}

std::int64_t create(Document& document, Kind kind,
                    std::optional<std::int64_t> parent)
{
  const std::int64_t parent_id = parent.value_or(0);
  if (kind == Kind::Travel)
  {
    const json& cities = document.section(Kind::City);
    if (cities.size() < 2)
    {
      return -1;
    }
    const std::int64_t from =
        parent_id != 0 && document.find(Kind::City, parent_id)
            ? parent_id
            : integer(cities.front(), "id");
    std::int64_t to = from;
    for (const json& city : cities)
    {
      if (const auto candidate = integer(city, "id"); candidate != from)
      {
        to = candidate;
        break;
      }
    }
    document.section(kind).push_back(
        {{"from", from}, {"to", to}, {"minutes", 60}});
    return static_cast<std::int64_t>(document.section(kind).size()) - 1;
  }
  const auto fresh = document.freeId(kind);
  if (!fresh)
  {
    return -1;
  }
  json entity = {{"id", *fresh}};
  switch (kind)
  {
    case Kind::Country:
      entity["name"] = uniqueName(document, kind, "New country");
      entity["nationality"] = "English";
      entity["region"] = "Europe";
      entity["reputation"] = DEFAULT_STAT;
      entity["economy"] = DEFAULT_STAT;
      break;
    case Kind::City:
      entity["name"] = uniqueName(document, kind, "New city");
      if (parent_id != 0)
      {
        entity["country_id"] = parent_id;
      }
      break;
    case Kind::League:
      entity["name"] = uniqueName(document, kind, "New league");
      if (parent_id != 0)
      {
        entity["country_id"] = parent_id;
      }
      entity["tiebreak"] = "goal_difference";
      break;
    case Kind::Club:
    {
      const std::string name = uniqueName(document, kind, "New club");
      entity["name"] = name;
      entity["short_name"] = uniqueShortName(document, name);
      if (parent_id != 0)
      {
        entity["league_id"] = parent_id;
      }
      entity["colours"] = {{"primary", "#C8102E"}, {"secondary", "#FFFFFF"}};
      break;
    }
    case Kind::Player:
    {
      entity["first_name"] = "New";
      entity["last_name"] = "Player";
      entity["age"] = 22;
      entity["nationality"] = "English";
      entity["role"] = "CM";
      if (parent_id != 0)
      {
        entity["team_id"] = parent_id;
      }
      json stats = json::object();
      for (const char* stat : STANDARD_STATS)
      {
        stats[stat] = DEFAULT_STAT;
      }
      entity["stats"] = std::move(stats);
      break;
    }
    case Kind::Travel:
    case Kind::COUNT:
      return -1;
  }
  document.add(kind, std::move(entity));
  return *fresh;
}

RemoveImpact impact(const Document& document, Kind kind, std::int64_t id)
{
  const auto count = [&](Kind section, const char* key)
  {
    return static_cast<std::size_t>(
        std::ranges::count_if(document.section(section), [&](const json& entity)
                              { return integer(entity, key) == id; }));
  };
  RemoveImpact result;
  switch (kind)
  {
    case Kind::Country:
      result.cities = count(Kind::City, "country_id");
      result.leagues = count(Kind::League, "country_id");
      break;
    case Kind::City:
      result.clubs = count(Kind::Club, "city_id");
      result.travel = count(Kind::Travel, "from") + count(Kind::Travel, "to");
      break;
    case Kind::League:
      result.leagues = count(Kind::League, "parent_league");
      result.clubs = count(Kind::Club, "league_id");
      break;
    case Kind::Club:
      result.players = count(Kind::Player, "team_id");
      break;
    case Kind::Travel:
    case Kind::Player:
    case Kind::COUNT:
      break;
  }
  return result;
}

void removeEntity(Document& document, Kind kind, std::int64_t id)
{
  if (kind == Kind::Travel)
  {
    json& travel = document.section(kind);
    if (id >= 0 && static_cast<std::size_t>(id) < travel.size())
    {
      travel.erase(static_cast<std::size_t>(id));
    }
    return;
  }
  apply(document, kind, id);
  document.remove(kind, id);
  document.invalidate();
}
}  // namespace World
