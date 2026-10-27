// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "database/world/world_io.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace World
{
namespace
{
Issue fileIssue(IssueCode code, std::string detail = {})
{
  Issue issue;
  issue.code = code;
  issue.detail = std::move(detail);
  return issue;
}

std::int64_t integer(const json& entity, const char* key)
{
  const auto found = entity.find(key);
  return found != entity.end() && found->is_number_integer()
             ? found->get<std::int64_t>()
             : 0;
}

/** Root league of @p league_id (follows parents, loop safe). */
std::int64_t rootLeague(const std::map<std::int64_t, std::int64_t>& parents,
                        std::int64_t league_id)
{
  std::int64_t current = league_id;
  for (std::size_t steps = 0; steps <= parents.size(); ++steps)
  {
    const auto found = parents.find(current);
    if (found == parents.end() || found->second == 0)
    {
      return current;
    }
    current = found->second;
  }
  return league_id;
}

void writeSection(std::string& out, std::string_view key, json array, Kind kind)
{
  std::stable_sort(
      array.begin(), array.end(),
      [kind](const json& left, const json& right)
      {
        if (kind == Kind::Travel)
        {
          return std::pair(integer(left, "from"), integer(left, "to")) <
                 std::pair(integer(right, "from"), integer(right, "to"));
        }
        return entityId(left) < entityId(right);
      });
  out += ",\n  \"";
  out += key;
  out += "\": [";
  for (std::size_t i = 0; i < array.size(); ++i)
  {
    out += i == 0 ? "\n    " : ",\n    ";
    out += array[i].dump();
  }
  out += array.empty() ? "]" : "\n  ]";
}
}  // namespace

void upgrade(json& root)
{
  if (!root.is_object())
  {
    return;
  }
  json& manifest = root["manifest"];
  if (!manifest.is_object())
  {
    return;
  }
  const int schema = manifest.value("schema", SCHEMA);
  if (schema >= SCHEMA)
  {
    return;
  }
  // Schema 1 (single-file custom databases) had no world layer: every top
  // division becomes a country named after it, with the nationality most of
  // its players have.
  json& leagues = root["leagues"];
  if (!leagues.is_array())
  {
    leagues = json::array();
  }
  std::map<std::int64_t, std::int64_t> parents;
  for (const json& league : leagues)
  {
    parents[entityId(league)] = integer(league, "parent_league");
  }
  std::map<std::int64_t, std::int64_t> club_root;
  if (const auto clubs = root.find("clubs");
      clubs != root.end() && clubs->is_array())
  {
    for (const json& club : *clubs)
    {
      club_root[entityId(club)] =
          rootLeague(parents, integer(club, "league_id"));
    }
  }
  std::map<std::int64_t, std::map<std::string, int>> nationality_votes;
  if (const auto players = root.find("players");
      players != root.end() && players->is_array())
  {
    for (const json& player : *players)
    {
      const auto club = club_root.find(integer(player, "team_id"));
      if (club != club_root.end())
      {
        ++nationality_votes[club->second]
                           [player.value("nationality", std::string())];
      }
    }
  }
  json countries = json::array();
  std::map<std::int64_t, std::int64_t> country_of_root;
  for (const json& league : leagues)
  {
    if (integer(league, "parent_league") != 0)
    {
      continue;
    }
    const std::int64_t id = static_cast<std::int64_t>(countries.size()) + 1;
    std::string nationality = "English";
    int best = 0;
    for (const auto& [name, votes] : nationality_votes[entityId(league)])
    {
      if (!name.empty() && votes > best)
      {
        best = votes;
        nationality = name;
      }
    }
    countries.push_back({{"id", id},
                         {"name", league.value("name", std::string())},
                         {"nationality", nationality}});
    country_of_root[entityId(league)] = id;
  }
  for (json& league : leagues)
  {
    const auto country =
        country_of_root.find(rootLeague(parents, entityId(league)));
    if (country != country_of_root.end() && !league.contains("country_id"))
    {
      league["country_id"] = country->second;
    }
    if (league.contains("parent_league") && league["parent_league"].is_null())
    {
      league.erase("parent_league");
    }
  }
  if (!root.contains("countries"))
  {
    root["countries"] = std::move(countries);
  }
  manifest["schema"] = SCHEMA;
}

LoadResult readText(std::string_view text)
{
  LoadResult result;
  if (text.size() > MAX_FILE_BYTES)
  {
    result.issues.push_back(fileIssue(IssueCode::TooLarge));
    return result;
  }
  json root = json::parse(text, nullptr, false);
  if (root.is_discarded() || !root.is_object())
  {
    result.issues.push_back(fileIssue(IssueCode::NotJson));
    return result;
  }
  const auto manifest = root.find("manifest");
  if (manifest == root.end() || !manifest->is_object())
  {
    result.issues.push_back(fileIssue(IssueCode::NoManifest));
    return result;
  }
  if (const auto schema = manifest->find("schema"); schema != manifest->end())
  {
    if (!schema->is_number_integer() || schema->get<int>() < 1)
    {
      result.issues.push_back(fileIssue(IssueCode::NoManifest, "schema"));
      return result;
    }
    if (schema->get<int>() > SCHEMA)
    {
      result.issues.push_back(
          fileIssue(IssueCode::NewerSchema,
                    manifest->value("min_game_version",
                                    std::to_string(schema->get<int>()))));
      return result;
    }
  }
  upgrade(root);
  result.document.emplace(std::move(root));
  result.issues = validate(*result.document);
  return result;
}

LoadResult readFile(const fs::path& path)
{
  std::error_code error;
  const auto size = fs::file_size(path, error);
  if (error)
  {
    LoadResult result;
    result.issues.push_back(fileIssue(IssueCode::CannotRead));
    return result;
  }
  if (size > MAX_FILE_BYTES)
  {
    LoadResult result;
    result.issues.push_back(fileIssue(IssueCode::TooLarge));
    return result;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open())
  {
    LoadResult result;
    result.issues.push_back(fileIssue(IssueCode::CannotRead));
    return result;
  }
  std::string text(static_cast<std::size_t>(size), '\0');
  file.read(text.data(), static_cast<std::streamsize>(size));
  text.resize(static_cast<std::size_t>(file.gcount()));
  return readText(text);
}

std::string write(const Document& document)
{
  const json& root = document.root();
  std::string out = "{\n  \"manifest\": ";
  out += root.at("manifest").dump();
  for (const Kind kind : KINDS)
  {
    if (document.hasSection(kind))
    {
      writeSection(out, sectionKey(kind), document.section(kind), kind);
    }
  }
  // Name pools and any extra top-level data of mods, in key order.
  for (const auto& [key, value] : root.items())
  {
    const bool known = key == "manifest" ||
                       std::ranges::any_of(KINDS, [&key](Kind kind)
                                           { return sectionKey(kind) == key; });
    if (known)
    {
      continue;
    }
    out += ",\n  ";
    out += json(key).dump();
    out += ": ";
    out += value.dump();
  }
  out += "\n}\n";
  return out;
}

bool writeFile(const Document& document, const fs::path& path,
               std::string* error)
{
  const auto fail = [error](std::string message)
  {
    if (error)
    {
      *error = std::move(message);
    }
    return false;
  };
  std::error_code code;
  if (path.has_parent_path())
  {
    fs::create_directories(path.parent_path(), code);
  }
  fs::path temporary = path;
  temporary += ".tmp";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
      return fail("cannot create " + pathToUtf8(temporary));
    }
    const std::string text = write(document);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file.good())
    {
      return fail("cannot write " + pathToUtf8(temporary));
    }
  }
  fs::rename(temporary, path, code);
  if (code)
  {
    fs::remove(temporary, code);
    return fail("cannot replace " + pathToUtf8(path));
  }
  return true;
}

namespace
{
std::string shortName(std::string_view name)
{
  std::string code(name.substr(0, 3));
  std::ranges::transform(
      code, code.begin(),
      [](char c)
      {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      });
  return code;
}
}  // namespace

Document templateDocument()
{
  Document document = Document::empty("My World");
  Manifest manifest = document.manifest();
  manifest.author = "Me";
  manifest.version = "1.0";
  manifest.description = "A small league to start from.";
  document.setManifest(manifest);
  document.add(Kind::Country, {{"id", 1},
                               {"name", "Freeland"},
                               {"demonym", "English"},
                               {"language", "English"},
                               {"reputation", 60},
                               {"economy", 60}});
  const std::array<std::pair<const char*, const char*>, 4> clubs = {{
      {"Northport", "#C8102E"},
      {"Southvale", "#1D428A"},
      {"Eastbrook", "#FDB913"},
      {"Westfield", "#00843D"},
  }};
  document.add(Kind::League, {{"id", 1},
                              {"name", "Freeland League"},
                              {"country_id", 1},
                              {"tiebreak", "goal_difference"}});
  for (std::size_t i = 0; i < clubs.size(); ++i)
  {
    const auto id = static_cast<std::int64_t>(i) + 1;
    document.add(Kind::City, {{"id", id},
                              {"name", clubs[i].first},
                              {"country_id", 1},
                              {"population", 50'000 * id}});
    document.add(
        Kind::Club,
        {{"id", 100 + id},
         {"league_id", 1},
         {"city_id", id},
         {"name", clubs[i].first},
         {"short_name", shortName(clubs[i].first)},
         {"colours", {{"primary", clubs[i].second}, {"secondary", "#FFFFFF"}}},
         {"stadium",
          {{"name", std::string(clubs[i].first) + " Park"},
           {"capacity", 12'000}}}});
  }
  document.add(Kind::Travel, {{"from", 1}, {"to", 2}, {"minutes", 90}});
  document.add(Kind::Player, {{"id", 1},
                              {"team_id", 101},
                              {"first_name", "Sam"},
                              {"last_name", "Example"},
                              {"age", 24},
                              {"nationality", "English"},
                              {"role", "ST"},
                              {"preferred_foot", "Right"},
                              {"height", 182},
                              {"stats",
                               {{"Pace", 70},
                                {"Shooting", 72},
                                {"Passing", 60},
                                {"Dribbling", 66},
                                {"Defending", 35},
                                {"Physicality", 64},
                                {"Stamina", 70},
                                {"Vision", 58},
                                {"Goalkeeping", 10}}}});
  return document;
}

fs::path pathFromUtf8(std::string_view text)
{
  return fs::path(std::u8string(text.begin(), text.end()));
}

std::string pathToUtf8(const fs::path& path)
{
  const std::u8string text = path.u8string();
  return {text.begin(), text.end()};
}
}  // namespace World
