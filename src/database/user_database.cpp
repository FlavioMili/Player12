// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "database/user_database.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

#include "global/languages.h"
#include "global/runtime_paths.h"
#include "model/role_utils.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
constexpr const char* ACTIVE_RECORD = "active.json";

void addIssue(std::vector<UserDatabase::Issue>& issues,
              const std::string& section, long long index, bool is_error,
              std::string message)
{
  issues.push_back({section, index, is_error, std::move(message)});
}

bool isColourString(const json& value)
{
  if (!value.is_string()) return false;
  const std::string& text = value.get_ref<const std::string&>();
  if (text.size() != 7 || text[0] != '#') return false;
  return std::all_of(text.begin() + 1, text.end(),
                     [](char c)
                     { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
}

long long asInt(const json& value) { return value.get<long long>(); }

bool validKitStrip(const json& strip, std::vector<UserDatabase::Issue>& issues,
                   const std::string& section, long long index,
                   const std::string& field)
{
  static constexpr const char* PARTS[] = {"shirt", "shorts", "socks", "trim"};
  if (!strip.is_object()) return false;
  bool ok = true;
  for (const char* part : PARTS)
  {
    if (!strip.contains(part)) continue;
    if (!isColourString(strip.at(part)))
    {
      addIssue(issues, section, index, true,
               field + "." + part + " must be \"#RRGGBB\"");
      ok = false;
    }
  }
  return ok;
}

std::string sanitiseFileStem(std::string text)
{
  for (char& c : text)
  {
    const bool keep =
        std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' ||
        c == '_' || c == '.' || c == ' ';
    if (!keep) c = '_';
  }
  // Trim leading/trailing blanks and dots (unsafe on some platforms).
  const auto notBlank = [](char c) { return c != ' ' && c != '.'; };
  text.erase(text.begin(),
             std::find_if(text.begin(), text.end(), notBlank));
  text.erase(std::find_if(text.rbegin(), text.rend(), notBlank).base(),
             text.end());
  std::replace(text.begin(), text.end(), ' ', '_');
  if (text.empty()) text = "database";
  return text;
}
}  // namespace

UserDatabase::LoadResult UserDatabase::loadFile(const fs::path& path)
{
  LoadResult result;
  auto& issues = result.issues;
  std::ifstream file(path);
  if (!file.is_open())
  {
    addIssue(issues, "file", -1, true, "cannot open file");
    return result;
  }
  json root = json::parse(file, nullptr, false);
  if (root.is_discarded() || !root.is_object())
  {
    addIssue(issues, "file", -1, true, "not a JSON object");
    return result;
  }

  // Manifest (required).
  if (!root.contains("manifest") || !root.at("manifest").is_object())
  {
    addIssue(issues, "manifest", -1, true, "missing \"manifest\" object");
    return result;
  }
  const json& manifest = root.at("manifest");
  auto& out_manifest = result.content.manifest;
  if (!manifest.contains("name") || !manifest.at("name").is_string() ||
      manifest.at("name").get_ref<const std::string&>().empty())
    addIssue(issues, "manifest", -1, true, "\"name\" must be a non-empty string");
  else
    out_manifest.name = manifest.at("name").get<std::string>();
  if (!manifest.contains("schema") || !manifest.at("schema").is_number_integer())
    addIssue(issues, "manifest", -1, true, "\"schema\" must be an integer");
  else
  {
    out_manifest.schema = manifest.at("schema").get<int>();
    if (out_manifest.schema != CURRENT_SCHEMA)
      addIssue(issues, "manifest", -1, true,
               "\"schema\" must be " + std::to_string(CURRENT_SCHEMA));
  }
  if (manifest.contains("version"))
  {
    if (!manifest.at("version").is_string())
      addIssue(issues, "manifest", -1, true, "\"version\" must be a string");
    else
      out_manifest.version = manifest.at("version").get<std::string>();
  }
  if (manifest.contains("author"))
  {
    if (!manifest.at("author").is_string())
      addIssue(issues, "manifest", -1, true, "\"author\" must be a string");
    else
      out_manifest.author = manifest.at("author").get<std::string>();
  }
  if (manifest.contains("min_game_version"))
  {
    if (!manifest.at("min_game_version").is_string())
      addIssue(issues, "manifest", -1, true,
               "\"min_game_version\" must be a string");
    else
      out_manifest.min_game_version =
          manifest.at("min_game_version").get<std::string>();
  }

  // Leagues (required, non-empty). Two passes so parent ids resolve.
  if (!root.contains("leagues") || !root.at("leagues").is_array() ||
      root.at("leagues").empty())
  {
    addIssue(issues, "leagues", -1, true,
             "\"leagues\" must be a non-empty array");
    return result;
  }
  const json& leagues = root.at("leagues");
  std::set<long long> league_ids;
  std::set<std::string> league_names;
  for (std::size_t i = 0; i < leagues.size(); ++i)
  {
    const json& item = leagues.at(i);
    const auto idx = static_cast<long long>(i);
    if (!item.is_object())
    {
      addIssue(issues, "leagues", idx, true, "must be an object");
      continue;
    }
    if (!item.contains("id") || !item.at("id").is_number_integer() ||
        asInt(item.at("id")) <= 0)
      addIssue(issues, "leagues", idx, true, "\"id\" must be a positive integer");
    else if (!league_ids.insert(asInt(item.at("id"))).second)
      addIssue(issues, "leagues", idx, true, "duplicate league id");
    if (!item.contains("name") || !item.at("name").is_string() ||
        item.at("name").get_ref<const std::string&>().empty())
      addIssue(issues, "leagues", idx, true,
               "\"name\" must be a non-empty string");
    else if (!league_names.insert(item.at("name").get<std::string>()).second)
      addIssue(issues, "leagues", idx, true, "duplicate league name");
    if (item.contains("tiebreak") && item.at("tiebreak").is_string())
    {
      const std::string& rule =
          item.at("tiebreak").get_ref<const std::string&>();
      if (rule != "head_to_head" && rule != "goal_difference" && !rule.empty())
        addIssue(issues, "leagues", idx, false,
                 "unknown \"tiebreak\" (uses goal difference)");
    }
  }
  for (std::size_t i = 0; i < leagues.size(); ++i)
  {
    const json& item = leagues.at(i);
    if (!item.is_object() || !item.contains("parent_league")) continue;
    if (!item.at("parent_league").is_number_integer() ||
        league_ids.count(asInt(item.at("parent_league"))) == 0)
      addIssue(issues, "leagues", static_cast<long long>(i), true,
               "\"parent_league\" must reference a league id in this file");
  }
  result.content.leagues = leagues;

  // Clubs (required, non-empty).
  if (!root.contains("clubs") || !root.at("clubs").is_array() ||
      root.at("clubs").empty())
  {
    addIssue(issues, "clubs", -1, true, "\"clubs\" must be a non-empty array");
    return result;
  }
  const json& clubs = root.at("clubs");
  std::set<long long> club_ids;
  std::set<std::string> club_names;
  for (std::size_t i = 0; i < clubs.size(); ++i)
  {
    const json& item = clubs.at(i);
    const auto idx = static_cast<long long>(i);
    if (!item.is_object())
    {
      addIssue(issues, "clubs", idx, true, "must be an object");
      continue;
    }
    if (!item.contains("id") || !item.at("id").is_number_integer() ||
        asInt(item.at("id")) <= 0)
      addIssue(issues, "clubs", idx, true, "\"id\" must be a positive integer");
    else if (!club_ids.insert(asInt(item.at("id"))).second)
      addIssue(issues, "clubs", idx, true, "duplicate club id");
    if (!item.contains("name") || !item.at("name").is_string() ||
        item.at("name").get_ref<const std::string&>().empty())
      addIssue(issues, "clubs", idx, true,
               "\"name\" must be a non-empty string");
    else if (!club_names.insert(item.at("name").get<std::string>()).second)
      addIssue(issues, "clubs", idx, true, "duplicate club name");
    if (!item.contains("league_id") ||
        !item.at("league_id").is_number_integer() ||
        league_ids.count(asInt(item.at("league_id"))) == 0)
      addIssue(issues, "clubs", idx, true,
               "\"league_id\" must reference a league id in this file");
    if (const auto colours = item.find("colours");
        colours != item.end() && colours->is_object())
    {
      for (const char* key : {"primary", "secondary"})
      {
        if (colours->contains(key) && !isColourString(colours->at(key)))
          addIssue(issues, "clubs", idx, true,
                   std::string("colours.") + key + " must be \"#RRGGBB\"");
      }
      for (const char* key : {"home", "away", "third"})
      {
        if (colours->contains(key))
          validKitStrip(colours->at(key), issues, "clubs", idx,
                        std::string("colours.") + key);
      }
    }
    if (const auto stadium = item.find("stadium");
        stadium != item.end() && !stadium->is_object())
      addIssue(issues, "clubs", idx, true, "\"stadium\" must be an object");
    for (const char* key : {"nickname", "short_name"})
    {
      if (item.contains(key) && !item.at(key).is_string())
        addIssue(issues, "clubs", idx, true,
                 std::string("\"") + key + "\" must be a string");
    }
  }
  result.content.clubs = clubs;

  // Players (required array; may be empty, squads are then generated).
  if (!root.contains("players") || !root.at("players").is_array())
  {
    addIssue(issues, "players", -1, true, "\"players\" must be an array");
    return result;
  }
  const json& players = root.at("players");
  std::set<long long> player_ids;
  for (std::size_t i = 0; i < players.size(); ++i)
  {
    const json& item = players.at(i);
    const auto idx = static_cast<long long>(i);
    if (!item.is_object())
    {
      addIssue(issues, "players", idx, true, "must be an object");
      continue;
    }
    if (!item.contains("id") || !item.at("id").is_number_integer() ||
        asInt(item.at("id")) <= 0)
      addIssue(issues, "players", idx, true, "\"id\" must be a positive integer");
    else if (!player_ids.insert(asInt(item.at("id"))).second)
      addIssue(issues, "players", idx, true, "duplicate player id");
    if (!item.contains("team_id") || !item.at("team_id").is_number_integer() ||
        club_ids.count(asInt(item.at("team_id"))) == 0)
      addIssue(issues, "players", idx, true,
               "\"team_id\" must reference a club id in this file");
    for (const char* key : {"first_name", "last_name"})
    {
      if (!item.contains(key) || !item.at(key).is_string() ||
          item.at(key).get_ref<const std::string&>().empty())
        addIssue(issues, "players", idx, true,
                 std::string("\"") + key + "\" must be a non-empty string");
    }
    if (!item.contains("age") || !item.at("age").is_number_integer() ||
        asInt(item.at("age")) < 15 || asInt(item.at("age")) > 60)
      addIssue(issues, "players", idx, true, "\"age\" must be 15-60");
    if (!item.contains("role") || !item.at("role").is_string() ||
        RoleUtils::fromString(
            item.at("role").get_ref<const std::string&>()) ==
            PlayerRole::UNKNOWN)
      addIssue(issues, "players", idx, true, "\"role\" is not a known role");
    if (item.contains("nationality") && item.at("nationality").is_string() &&
        stringToLanguage.count(
            item.at("nationality").get_ref<const std::string&>()) == 0)
      addIssue(issues, "players", idx, false,
               "unknown \"nationality\" (uses English)");
    if (!item.contains("stats") || !item.at("stats").is_object())
      addIssue(issues, "players", idx, true, "\"stats\" must be an object");
    else
    {
      for (const auto& [stat, value] : item.at("stats").items())
      {
        if (!value.is_number())
        {
          addIssue(issues, "players", idx, true,
                   "stat \"" + stat + "\" must be a number");
          break;
        }
        const double number = value.get<double>();
        if (number < 0.0 || number > 100.0)
        {
          addIssue(issues, "players", idx, false,
                   "stat \"" + stat + "\" is outside 0-100");
          break;
        }
      }
    }
    if (item.contains("preferred_foot"))
    {
      if (!item.at("preferred_foot").is_string())
        addIssue(issues, "players", idx, true,
                 "\"preferred_foot\" must be a string");
      else
      {
        const std::string& foot =
            item.at("preferred_foot").get_ref<const std::string&>();
        if (foot != "Left" && foot != "Right" && foot != "Both")
          addIssue(issues, "players", idx, true,
                   "\"preferred_foot\" must be Left, Right or Both");
      }
    }
    if (item.contains("height") &&
        (!item.at("height").is_number_integer() ||
         asInt(item.at("height")) < 100 || asInt(item.at("height")) > 250))
      addIssue(issues, "players", idx, true, "\"height\" must be 100-250 cm");
    if (item.contains("skin") &&
        (!item.at("skin").is_number_integer() ||
         asInt(item.at("skin")) < 0 || asInt(item.at("skin")) > 7))
      addIssue(issues, "players", idx, true, "\"skin\" must be 0-7");
    if (item.contains("nickname") && !item.at("nickname").is_string())
      addIssue(issues, "players", idx, true, "\"nickname\" must be a string");
    for (const char* key : {"wage", "contract_years", "status"})
    {
      if (item.contains(key) && !item.at(key).is_number_unsigned() &&
          !item.at(key).is_number_integer())
        addIssue(issues, "players", idx, true,
                 std::string("\"") + key + "\" must be a number");
    }
  }
  result.content.players = players;

  // Names (optional).
  bool has_names = false;
  for (const char* key : {"first_names", "last_names"})
  {
    if (!root.contains(key)) continue;
    const json& names = root.at(key);
    if (!names.is_object() || !names.contains("names") ||
        !names.at("names").is_array() || names.at("names").empty())
    {
      addIssue(issues, key, -1, true,
               std::string("\"") + key + "\" needs a non-empty \"names\" array");
      continue;
    }
    bool names_ok = true;
    for (const auto& name : names.at("names"))
    {
      if (!name.is_string() || name.get_ref<const std::string&>().empty())
      {
        names_ok = false;
        break;
      }
    }
    if (!names_ok)
    {
      addIssue(issues, key, -1, true, "\"names\" must hold non-empty strings");
      continue;
    }
    if (key == std::string("first_names"))
      result.content.first_names = names;
    else
      result.content.last_names = names;
    has_names = true;
  }
  result.content.has_names = has_names;

  result.ok = std::none_of(issues.begin(), issues.end(),
                           [](const Issue& issue) { return issue.is_error; });
  return result;
}

fs::path UserDatabase::databasesDir()
{
  return RuntimePaths::root() / "databases";
}

bool UserDatabase::importFile(const fs::path& source,
                              std::vector<Issue>& issues,
                              std::string& imported_name)
{
  const LoadResult loaded = loadFile(source);
  issues = loaded.issues;
  if (!loaded.ok) return false;
  std::error_code error;
  fs::create_directories(databasesDir(), error);
  if (error) return false;
  std::string stem = sanitiseFileStem(source.stem().string());
  fs::path dest = databasesDir() / (stem + FILE_EXTENSION);
  for (int suffix = 2; fs::exists(dest); ++suffix)
    dest = databasesDir() / (stem + "-" + std::to_string(suffix) +
                             FILE_EXTENSION);
  fs::copy_file(source, dest, error);
  if (error) return false;
  imported_name = dest.filename().string();
  setActive(imported_name);
  return true;
}

void UserDatabase::setActive(const std::string& file_name)
{
  std::error_code error;
  fs::create_directories(databasesDir(), error);
  if (error) return;
  std::ofstream record(databasesDir() / ACTIVE_RECORD);
  if (!record.is_open()) return;
  json active;
  active["file"] = file_name;
  record << active.dump();
}

void UserDatabase::clearActive()
{
  std::error_code error;
  fs::remove(databasesDir() / ACTIVE_RECORD, error);
}

std::optional<std::string> UserDatabase::activeName()
{
  std::ifstream record(databasesDir() / ACTIVE_RECORD);
  if (!record.is_open()) return std::nullopt;
  const json active = json::parse(record, nullptr, false);
  if (active.is_discarded() || !active.is_object() || !active.contains("file") ||
      !active.at("file").is_string())
    return std::nullopt;
  const std::string name = active.at("file").get<std::string>();
  std::error_code error;
  if (!fs::exists(databasesDir() / name, error)) return std::nullopt;
  return name;
}

std::optional<UserDatabase::Content> UserDatabase::activeContent()
{
  // Reloads when the active file changes on disk; generation reads the
  // sections several times per world, so the cache matters.
  static std::string cached_name;
  static fs::file_time_type cached_mtime{};
  static std::optional<Content> cached;
  const std::optional<std::string> name = activeName();
  if (!name)
  {
    cached.reset();
    cached_name.clear();
    return std::nullopt;
  }
  const fs::path path = databasesDir() / *name;
  std::error_code error;
  const fs::file_time_type mtime = fs::last_write_time(path, error);
  if (error)
  {
    cached.reset();
    cached_name.clear();
    return std::nullopt;
  }
  if (!cached || cached_name != *name || cached_mtime != mtime)
  {
    const LoadResult loaded = loadFile(path);
    if (!loaded.ok)
    {
      cached.reset();
      cached_name.clear();
      return std::nullopt;
    }
    cached = loaded.content;
    cached_name = *name;
    cached_mtime = mtime;
  }
  return cached;
}

std::optional<json> UserDatabase::activeLeagues()
{
  const auto content = activeContent();
  if (!content) return std::nullopt;
  return content->leagues;
}

std::optional<json> UserDatabase::activeClubs()
{
  const auto content = activeContent();
  if (!content) return std::nullopt;
  return content->clubs;
}

std::optional<json> UserDatabase::activePlayers()
{
  const auto content = activeContent();
  if (!content) return std::nullopt;
  return content->players;
}

std::optional<UserDatabase::Manifest> UserDatabase::activeManifest()
{
  const auto content = activeContent();
  if (!content) return std::nullopt;
  return content->manifest;
}
