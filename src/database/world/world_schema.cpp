// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "database/world/world_schema.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <deque>
#include <utility>

#include "global/languages.h"
#include "model/player.h"
#include "model/role_utils.h"

using json = nlohmann::json;

namespace World
{
namespace
{
using enum FieldType;

constexpr std::string_view GENERAL = "WORLD_SECTION_GENERAL";
constexpr std::string_view KIT = "WORLD_SECTION_KIT";
constexpr std::string_view STADIUM = "WORLD_SECTION_STADIUM";
constexpr std::string_view ECONOMY = "WORLD_SECTION_ECONOMY";
constexpr std::string_view MATCHES = "WORLD_SECTION_MATCHES";
constexpr std::string_view ATTRIBUTES = "EDITOR_ATTRIBUTES";
constexpr std::string_view PERSONAL = "EDITOR_PERSONAL";
constexpr std::string_view CONTRACT = "EDITOR_CONTRACT";
constexpr double MAX_RATING = 100;
constexpr double MAX_MONEY = 1e12;

FieldSpec field(std::string_view path, FieldType type, std::string_view label,
                std::string_view section, double minimum = 0,
                double maximum = 0, bool required = false)
{
  FieldSpec spec;
  spec.path = path;
  spec.type = type;
  spec.label = label;
  spec.section = section;
  spec.minimum = minimum;
  spec.maximum = maximum;
  spec.required = required;
  return spec;
}

FieldSpec required(FieldSpec spec)
{
  spec.required = true;
  return spec;
}

FieldSpec choice(std::string_view path, ChoiceSet set, std::string_view label,
                 std::string_view section)
{
  FieldSpec spec = field(path, Choice, label, section);
  spec.choices = set;
  return spec;
}

FieldSpec reference(std::string_view path, Kind kind, std::string_view label,
                    std::string_view section)
{
  FieldSpec spec = field(path, Reference, label, section);
  spec.reference = kind;
  return spec;
}

std::vector<FieldSpec> countryFields()
{
  return {
      required(field("name", Text, "WORLD_FIELD_NAME", GENERAL, 1, 64)),
      required(choice("nationality", ChoiceSet::Nationality,
                      "PROFILE_NATIONALITY", GENERAL)),
      choice("region", ChoiceSet::Region, "WORLD_FIELD_REGION", GENERAL),
      field("reputation", Integer, "WORLD_FIELD_FOOTBALL_LEVEL", GENERAL, 1,
            MAX_RATING),
      field("economy", Integer, "WORLD_FIELD_ECONOMY_LEVEL", GENERAL, 1,
            MAX_RATING),
  };
}

std::vector<FieldSpec> cityFields()
{
  return {
      required(field("name", Text, "WORLD_FIELD_NAME", GENERAL, 1, 64)),
      reference("country_id", Kind::Country, "WORLD_FIELD_COUNTRY", GENERAL),
      field("population", Integer, "WORLD_FIELD_POPULATION", GENERAL, 0,
            100'000'000),
  };
}

std::vector<FieldSpec> travelFields()
{
  return {
      required(reference("from", Kind::City, "WORLD_FIELD_FROM", GENERAL)),
      required(reference("to", Kind::City, "WORLD_FIELD_TO", GENERAL)),
      required(field("minutes", Integer, "WORLD_FIELD_TRAVEL_MINUTES", GENERAL,
                     1, 10'000)),
  };
}

std::vector<FieldSpec> leagueFields()
{
  return {
      required(field("name", Text, "WORLD_FIELD_NAME", GENERAL, 1, 64)),
      reference("country_id", Kind::Country, "WORLD_FIELD_COUNTRY", GENERAL),
      reference("parent_league", Kind::League, "WORLD_FIELD_PARENT_LEAGUE",
                GENERAL),
      choice("tiebreak", ChoiceSet::Tiebreak, "WORLD_FIELD_TIEBREAK", GENERAL),
      field("profile.reputation", Integer, "WORLD_FIELD_FOOTBALL_LEVEL",
            ECONOMY, 1, MAX_RATING),
      field("profile.average_revenue_eur", Number, "WORLD_FIELD_REVENUE",
            ECONOMY, 0, MAX_MONEY),
      field("profile.wage_ratio", Number, "WORLD_FIELD_WAGE_RATIO", ECONOMY, 0,
            3),
      field("profile.tv_share", Number, "WORLD_FIELD_TV_SHARE", ECONOMY, 0, 1),
      field("profile.continental_share", Number, "WORLD_FIELD_PRIZE_SHARE",
            ECONOMY, 0, 1),
      field("profile.gate_share", Number, "WORLD_FIELD_GATE_SHARE", ECONOMY, 0,
            1),
      field("profile.commercial_share", Number, "WORLD_FIELD_COMMERCIAL_SHARE",
            ECONOMY, 0, 1),
      field("profile.average_attendance", Number, "WORLD_FIELD_ATTENDANCE",
            ECONOMY, 0, 250'000),
      field("profile.stadium_fill", Number, "WORLD_FIELD_STADIUM_FILL", ECONOMY,
            0, 1),
      choice("profile.domestic_nationality", ChoiceSet::Nationality,
             "WORLD_FIELD_DOMESTIC_NATIONALITY", ECONOMY),
      field("profile.domestic_share", Number, "WORLD_FIELD_DOMESTIC_SHARE",
            ECONOMY, 0, 1),
      field("profile.coach_changes", Number, "WORLD_FIELD_COACH_CHANGES",
            ECONOMY, 0, 3),
      field("profile.shape.level_sd", Number, "WORLD_FIELD_LEVEL_SPREAD",
            MATCHES, 0, 30),
      field("profile.shape.elite_clubs", Integer, "WORLD_FIELD_ELITE_CLUBS",
            MATCHES, 0, 20),
      field("profile.shape.elite_gap", Number, "WORLD_FIELD_ELITE_GAP", MATCHES,
            0, 5),
      field("profile.shape.level_offset", Number, "WORLD_FIELD_LEVEL_OFFSET",
            MATCHES, -30, 30),
      field("profile.match_style.goals", Number, "WORLD_FIELD_GOALS", MATCHES,
            0, 10),
      field("profile.match_style.yellow_cards", Number, "WORLD_FIELD_YELLOWS",
            MATCHES, 0, 15),
      field("profile.match_style.referee_spread", Number,
            "WORLD_FIELD_REFEREE_SPREAD", MATCHES, 0, 5),
      field("profile.match_style.home_edge", Number, "WORLD_FIELD_HOME_EDGE",
            MATCHES, -50, 50),
  };
}

std::vector<FieldSpec> clubFields()
{
  std::vector<FieldSpec> specs = {
      required(field("name", Text, "EDITOR_CLUB_NAME", GENERAL, 1, 64)),
      reference("league_id", Kind::League, "WORLD_FIELD_LEAGUE", GENERAL),
      reference("city_id", Kind::City, "WORLD_FIELD_CITY", GENERAL),
      field("short_name", Text, "WORLD_FIELD_SHORT_NAME", GENERAL, 0, 3),
      field("nickname", Text, "WORLD_FIELD_NICKNAME", GENERAL, 0, 64),
      field("founded", Integer, "WORLD_FIELD_FOUNDED", GENERAL, 1800, 2100),
      field("stadium.name", Text, "WORLD_FIELD_STADIUM_NAME", STADIUM, 0, 96),
      field("stadium.capacity", Integer, "EDITOR_STADIUM_SEATS", STADIUM, 0,
            250'000),
      field("colours.primary", Colour, "WORLD_FIELD_PRIMARY_COLOUR", KIT),
      field("colours.secondary", Colour, "WORLD_FIELD_SECONDARY_COLOUR", KIT),
  };
  for (const auto& [strip, label] : {std::pair{"home", "WORLD_FIELD_HOME_"},
                                     std::pair{"away", "WORLD_FIELD_AWAY_"},
                                     std::pair{"third", "WORLD_FIELD_THIRD_"}})
  {
    for (const char* part : {"shirt", "shorts", "socks", "trim"})
    {
      // Paths and labels must outlive the table; a deque never moves them.
      static std::deque<std::string> interned;
      interned.push_back(std::string("colours.") + strip + "." + part);
      const std::string_view path = interned.back();
      std::string upper = part;
      std::ranges::transform(upper, upper.begin(), [](char c)
                             { return static_cast<char>(std::toupper(c)); });
      interned.push_back(std::string(label) + upper);
      specs.push_back(field(path, Colour, interned.back(), KIT));
    }
  }
  return specs;
}

std::vector<FieldSpec> playerFields()
{
  return {
      required(field("stats", Stats, ATTRIBUTES, ATTRIBUTES)),
      required(field("first_name", Text, "EDITOR_FIRST_NAME", PERSONAL, 1, 64)),
      required(field("last_name", Text, "EDITOR_LAST_NAME", PERSONAL, 1, 64)),
      field("nickname", Text, "WORLD_FIELD_NICKNAME", PERSONAL, 0, 64),
      reference("team_id", Kind::Club, "WORLD_FIELD_CLUB", PERSONAL),
      required(field("age", Integer, "EDITOR_AGE_YEARS", PERSONAL, 15, 60)),
      choice("nationality", ChoiceSet::Nationality, "PROFILE_NATIONALITY",
             PERSONAL),
      required(choice("role", ChoiceSet::Role, "EDITOR_POSITION", PERSONAL)),
      choice("preferred_foot", ChoiceSet::Foot, "PROFILE_FOOT", PERSONAL),
      field("height", Integer, "EDITOR_HEIGHT_CM", PERSONAL, 100, 250),
      field("skin", Integer, "WORLD_FIELD_SKIN", PERSONAL, 0, 7),
      field("wage", Integer, "EDITOR_WAGE_WEEK", CONTRACT, 0, 100'000'000),
      field("contract_years", Integer, "PLAYER_CONTRACT", CONTRACT, 0, 10),
  };
}

std::vector<std::string> nationalities()
{
  std::vector<std::string> values;
  values.reserve(languageToString.size());
  for (const auto& [language, name] : languageToString)
  {
    (void)language;
    values.push_back(name);
  }
  std::ranges::sort(values);
  return values;
}

std::vector<std::string> roles()
{
  std::vector<std::string> values;
  for (auto index = std::to_underlying(PlayerRole::GK);
       index < std::to_underlying(PlayerRole::UNKNOWN); ++index)
  {
    values.push_back(RoleUtils::toString(static_cast<PlayerRole>(index)));
  }
  return values;
}

std::string_view head(std::string_view path)
{
  return path.substr(0, path.find('.'));
}

std::string_view tail(std::string_view path)
{
  const auto dot = path.find('.');
  return dot == std::string_view::npos ? std::string_view()
                                       : path.substr(dot + 1);
}

void collectDiffs(const json& left, const json& right, const std::string& path,
                  std::vector<std::string>& out)
{
  if ((left.is_object() || left.is_null()) &&
      (right.is_object() || right.is_null()) &&
      (left.is_object() || right.is_object()))
  {
    std::vector<std::string> keys;
    const json empty = json::object();
    const json& a_side = left.is_object() ? left : empty;
    const json& b_side = right.is_object() ? right : empty;
    for (const auto& [key, value] : a_side.items())
    {
      (void)value;
      keys.push_back(key);
    }
    for (const auto& [key, value] : b_side.items())
    {
      (void)value;
      if (!a_side.contains(key))
      {
        keys.push_back(key);
      }
    }
    std::ranges::sort(keys);
    for (const auto& key : keys)
    {
      if (path.empty() && key == "id")
      {
        continue;
      }
      const json a = a_side.contains(key) ? a_side.at(key) : json();
      const json b = b_side.contains(key) ? b_side.at(key) : json();
      collectDiffs(a, b, path.empty() ? key : path + "." + key, out);
    }
    return;
  }
  if (left.is_number() && right.is_number())
  {
    if (left.get<double>() != right.get<double>())
    {
      out.push_back(path);
    }
    return;
  }
  if (left != right)
  {
    out.push_back(path);
  }
}
}  // namespace

const std::vector<FieldSpec>& fields(Kind kind)
{
  static const std::array<std::vector<FieldSpec>, 6> TABLES = {
      countryFields(), cityFields(), travelFields(),
      leagueFields(),  clubFields(), playerFields(),
  };
  return TABLES.at(static_cast<std::size_t>(kind));
}

const std::vector<std::string>& choiceValues(ChoiceSet set)
{
  static const std::vector<std::string> NONE;
  static const std::vector<std::string> NATIONALITIES = nationalities();
  static const std::vector<std::string> ROLES = roles();
  static const std::vector<std::string> FEET = {"Left", "Right", "Both"};
  static const std::vector<std::string> REGIONS = {
      "Europe", "EasternEurope", "NorthAmerica", "SouthAmerica"};
  static const std::vector<std::string> TIEBREAKS = {"goal_difference",
                                                     "head_to_head"};
  switch (set)
  {
    case ChoiceSet::Nationality:
      return NATIONALITIES;
    case ChoiceSet::Role:
      return ROLES;
    case ChoiceSet::Foot:
      return FEET;
    case ChoiceSet::Region:
      return REGIONS;
    case ChoiceSet::Tiebreak:
      return TIEBREAKS;
    case ChoiceSet::None:
      break;
  }
  return NONE;
}

const json* get(const json& entity, std::string_view path)
{
  if (!entity.is_object())
  {
    return nullptr;
  }
  const auto found = entity.find(std::string(head(path)));
  if (found == entity.end())
  {
    return nullptr;
  }
  const std::string_view rest = tail(path);
  return rest.empty() ? &*found : get(*found, rest);
}

void set(json& entity, std::string_view path, json value)
{
  if (!entity.is_object())
  {
    entity = json::object();
  }
  const std::string key(head(path));
  const std::string_view rest = tail(path);
  if (rest.empty())
  {
    if (value.is_null())
    {
      entity.erase(key);
    }
    else
    {
      entity[key] = std::move(value);
    }
    return;
  }
  if (value.is_null() && !entity.contains(key))
  {
    return;
  }
  json& child = entity[key];
  set(child, rest, std::move(value));
  if (child.is_object() && child.empty())
  {
    entity.erase(key);
  }
}

std::vector<std::string> differingPaths(const json& left, const json& right)
{
  std::vector<std::string> out;
  collectDiffs(left, right, "", out);
  return out;
}
}  // namespace World
