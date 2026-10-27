// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "database/world/world_runtime.h"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>

#include "database/world/world_io.h"
#include "global/languages.h"
#include "global/logger.h"
#include "global/paths.h"

using json = nlohmann::json;

namespace World
{
namespace
{
constexpr std::array<std::string_view, 4> REGIONS = {
    "Europe", "EasternEurope", "NorthAmerica", "SouthAmerica"};
/** Reputation lost per division below the top. [P] */
constexpr float TIER_REPUTATION_STEP = 25.0f;
/** Default country level when a world does not give one. */
constexpr float DEFAULT_LEVEL = 50.0f;
/** Revenue and crowds grow exponentially with the economy level, fitted to
 * the built-in profiles (level 50 ~ EUR 10m and 8,000 fans; level 92 ~ EUR
 * 370m). [P] */
constexpr float REVENUE_LEVEL_SCALE = 11.7f;
constexpr float ATTENDANCE_LEVEL_SCALE = 18.0f;

struct State
{
  std::mutex mutex;
  std::shared_ptr<const Document> world;
  std::atomic<std::uint64_t> generation{1};
};

State& state()
{
  static State instance;
  return instance;
}

/** Shortest text that reads back as the same float, as a JSON number. */
json number(float value)
{
  std::array<char, 32> buffer{};
  const auto [end, error] =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  if (error != std::errc{})
  {
    return static_cast<double>(value);
  }
  return json::parse(std::string(buffer.data(), end));
}

float readFloat(const json& object, const char* key, float fallback)
{
  const auto found = object.find(key);
  return found != object.end() && found->is_number() ? found->get<float>()
                                                     : fallback;
}

template <typename Integer>
Integer readInteger(const json& object, const char* key, Integer fallback)
{
  const auto found = object.find(key);
  if (found == object.end() || !found->is_number())
  {
    return fallback;
  }
  return static_cast<Integer>(std::clamp<double>(
      found->get<double>(), 0, std::numeric_limits<Integer>::max()));
}

Language readNationality(const json& object, const char* key, Language fallback)
{
  const auto found = object.find(key);
  if (found == object.end() || !found->is_string())
  {
    return fallback;
  }
  const auto language = stringToLanguage.find(found->get<std::string>());
  return language != stringToLanguage.end() ? language->second : fallback;
}

WorldRegion readRegion(const json& object, const char* key,
                       WorldRegion fallback)
{
  const auto found = object.find(key);
  if (found == object.end() || !found->is_string())
  {
    return fallback;
  }
  const auto region = std::ranges::find(REGIONS, found->get<std::string>());
  return region != REGIONS.end()
             ? static_cast<WorldRegion>(region - REGIONS.begin())
             : fallback;
}

std::int64_t integerField(const json& entity, const char* key)
{
  const auto found = entity.find(key);
  return found != entity.end() && found->is_number_integer()
             ? found->get<std::int64_t>()
             : 0;
}

/** Profile of a league without authored values: country level, minus a step
 * per division, drives reputation, revenue and crowds. */
LeagueProfile synthesise(const Document& world, const json& league)
{
  LeagueProfile profile = DEFAULT_LEAGUE_PROFILE;
  profile.league_id = static_cast<LeagueID>(entityId(league));
  int tier = 0;
  for (std::int64_t parent = integerField(league, "parent_league");
       parent != 0 && tier < 16; ++tier)
  {
    const json* next = world.find(Kind::League, parent);
    parent = next ? integerField(*next, "parent_league") : 0;
  }
  float football = DEFAULT_LEVEL;
  float economy = DEFAULT_LEVEL;
  if (const json* country =
          world.find(Kind::Country, integerField(league, "country_id")))
  {
    football = readFloat(*country, "reputation", DEFAULT_LEVEL);
    economy = readFloat(*country, "economy", football);
    profile.domestic_nationality =
        readNationality(*country, "nationality", profile.domestic_nationality);
    profile.region = readRegion(*country, "region", profile.region);
  }
  const float step = TIER_REPUTATION_STEP * static_cast<float>(tier);
  football = std::clamp(football - step, 1.0f, 99.0f);
  economy = std::clamp(economy - step, 1.0f, 99.0f);
  profile.reputation = static_cast<std::uint8_t>(std::lround(football));
  profile.average_revenue_eur =
      DEFAULT_LEAGUE_PROFILE.average_revenue_eur *
      std::exp((economy - DEFAULT_LEVEL) / REVENUE_LEVEL_SCALE);
  profile.average_attendance =
      DEFAULT_LEAGUE_PROFILE.average_attendance *
      std::exp((economy - DEFAULT_LEVEL) / ATTENDANCE_LEVEL_SCALE);
  return profile;
}
}  // namespace

std::shared_ptr<const Document> defaultWorld()
{
  static const std::shared_ptr<const Document> world = []
  {
    const LoadResult loaded =
        readFile(pathFromUtf8(AssetPaths::defaultWorld()));
    if (!loaded.ok())
    {
      throw std::runtime_error(
          "Built-in world " + AssetPaths::defaultWorld() + " is invalid: " +
          (loaded.issues.empty() ? std::string("unknown error")
                                 : describe(loaded.issues.front())));
    }
    return std::make_shared<const Document>(*loaded.document);
  }();
  return world;
}

std::shared_ptr<const Document> current()
{
  State& shared = state();
  {
    const std::scoped_lock lock(shared.mutex);
    if (shared.world)
    {
      return shared.world;
    }
  }
  install(nullptr);
  const std::scoped_lock lock(shared.mutex);
  return shared.world;
}

void install(std::shared_ptr<const Document> world)
{
  if (!world)
  {
    world = defaultWorld();
  }
  State& shared = state();
  {
    const std::scoped_lock lock(shared.mutex);
    if (shared.world == world)
    {
      return;
    }
    shared.world = world;
  }
  installLeagueProfiles(buildProfiles(*world));
  shared.generation.fetch_add(1);
}

std::uint64_t generation() { return state().generation.load(); }

void storeInSave(sqlite3* db, const Document& world)
{
  const std::string text = write(world);
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db,
                     "INSERT OR REPLACE INTO WorldDocument (id, content) "
                     "VALUES (1, ?);",
                     -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, text.data(), static_cast<int>(text.size()),
                    SQLITE_STATIC);
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE)
  {
    throw std::runtime_error(std::string("WorldDocument: ") +
                             sqlite3_errmsg(db));
  }
}

std::shared_ptr<const Document> fromSave(sqlite3* db)
{
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT content FROM WorldDocument WHERE id = 1;",
                         -1, &stmt, nullptr) != SQLITE_OK)
  {
    sqlite3_finalize(stmt);
    return nullptr;
  }
  std::string text;
  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    if (const auto* data = sqlite3_column_text(stmt, 0))
    {
      text.assign(reinterpret_cast<const char*>(data),
                  static_cast<std::size_t>(sqlite3_column_bytes(stmt, 0)));
    }
  }
  sqlite3_finalize(stmt);
  if (text.empty())
  {
    return nullptr;
  }
  LoadResult loaded = readText(text);
  if (!loaded.document)
  {
    Logger::warn(
        "The world stored in the save cannot be read; using the "
        "built-in world.");
    return nullptr;
  }
  return std::make_shared<const Document>(std::move(*loaded.document));
}

json profileToJson(const LeagueProfile& profile)
{
  const auto nationality = languageToString.find(profile.domestic_nationality);
  return {
      {"reputation", profile.reputation},
      {"average_revenue_eur", number(profile.average_revenue_eur)},
      {"tv_share", number(profile.tv_share)},
      {"continental_share", number(profile.continental_share)},
      {"gate_share", number(profile.gate_share)},
      {"commercial_share", number(profile.commercial_share)},
      {"wage_ratio", number(profile.wage_ratio)},
      {"average_attendance", number(profile.average_attendance)},
      {"domestic_nationality", nationality != languageToString.end()
                                   ? nationality->second
                                   : std::string("English")},
      {"domestic_share", number(profile.domestic_share)},
      {"region",
       std::string(REGIONS.at(static_cast<std::size_t>(profile.region)))},
      {"stadium_fill", number(profile.stadium_fill)},
      {"shape",
       {{"level_sd", number(profile.shape.level_sd)},
        {"elite_clubs", profile.shape.elite_clubs},
        {"elite_gap", number(profile.shape.elite_gap)},
        {"level_offset", number(profile.shape.level_offset)}}},
      {"coach_changes", number(profile.coach_changes)},
      {"match_style",
       {{"goals", number(profile.match_style.goals)},
        {"yellow_cards", number(profile.match_style.yellow_cards)},
        {"referee_spread", number(profile.match_style.referee_spread)},
        {"home_edge", number(profile.match_style.home_edge)}}},
  };
}

LeagueProfile profileFromJson(const json& object, const LeagueProfile& base)
{
  if (!object.is_object())
  {
    return base;
  }
  LeagueProfile profile = base;
  profile.reputation = readInteger(object, "reputation", profile.reputation);
  profile.average_revenue_eur =
      readFloat(object, "average_revenue_eur", profile.average_revenue_eur);
  profile.tv_share = readFloat(object, "tv_share", profile.tv_share);
  profile.continental_share =
      readFloat(object, "continental_share", profile.continental_share);
  profile.gate_share = readFloat(object, "gate_share", profile.gate_share);
  profile.commercial_share =
      readFloat(object, "commercial_share", profile.commercial_share);
  profile.wage_ratio = readFloat(object, "wage_ratio", profile.wage_ratio);
  profile.average_attendance =
      readFloat(object, "average_attendance", profile.average_attendance);
  profile.domestic_nationality = readNationality(object, "domestic_nationality",
                                                 profile.domestic_nationality);
  profile.domestic_share =
      readFloat(object, "domestic_share", profile.domestic_share);
  profile.region = readRegion(object, "region", profile.region);
  profile.stadium_fill =
      readFloat(object, "stadium_fill", profile.stadium_fill);
  profile.coach_changes =
      readFloat(object, "coach_changes", profile.coach_changes);
  if (const auto shape = object.find("shape");
      shape != object.end() && shape->is_object())
  {
    profile.shape.level_sd =
        readFloat(*shape, "level_sd", profile.shape.level_sd);
    profile.shape.elite_clubs =
        readInteger(*shape, "elite_clubs", profile.shape.elite_clubs);
    profile.shape.elite_gap =
        readFloat(*shape, "elite_gap", profile.shape.elite_gap);
    profile.shape.level_offset =
        readFloat(*shape, "level_offset", profile.shape.level_offset);
  }
  if (const auto style = object.find("match_style");
      style != object.end() && style->is_object())
  {
    profile.match_style.goals =
        readFloat(*style, "goals", profile.match_style.goals);
    profile.match_style.yellow_cards =
        readFloat(*style, "yellow_cards", profile.match_style.yellow_cards);
    profile.match_style.referee_spread =
        readFloat(*style, "referee_spread", profile.match_style.referee_spread);
    profile.match_style.home_edge =
        readFloat(*style, "home_edge", profile.match_style.home_edge);
  }
  return profile;
}

std::vector<LeagueProfile> buildProfiles(const Document& world)
{
  std::vector<LeagueProfile> profiles;
  for (const json& league : world.section(Kind::League))
  {
    const std::int64_t id = entityId(league);
    if (id < minId(Kind::League) || id > maxId(Kind::League))
    {
      continue;
    }
    LeagueProfile profile = synthesise(world, league);
    if (const auto authored = league.find("profile"); authored != league.end())
    {
      profile = profileFromJson(*authored, profile);
    }
    profile.league_id = static_cast<LeagueID>(id);
    profiles.push_back(profile);
  }
  return profiles;
}
}  // namespace World
