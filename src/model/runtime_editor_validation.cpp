// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/runtime_editor_validation.h"

#include <array>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

#include "global/global.h"
#include "global/languages.h"
#include "model/gamedate.h"
#include "model/manager_career.h"
#include "model/player.h"
#include "model/role_utils.h"
#include "model/squad_numbers.h"
#include "model/staff.h"

namespace EditorValidation
{
namespace
{
constexpr auto BYTE_MAX = std::numeric_limits<std::uint8_t>::max();
constexpr auto PLAYER_MAX = std::numeric_limits<PlayerID>::max();
constexpr auto TEAM_MAX = std::numeric_limits<TeamID>::max();
constexpr auto LEAGUE_MAX = std::numeric_limits<LeagueID>::max();
constexpr int LAST_KICKOFF_MINUTE = 24 * 60 - 1;
constexpr std::size_t TRAIT_FIELDS = 5;
constexpr std::size_t DYNAMICS_FIELDS = 14 + PlayerDynamics::FORM_WINDOW;

struct RangeRule
{
  std::string_view table;
  std::string_view column;
  std::int64_t minimum;
  std::int64_t maximum;
};
constexpr std::array RANGE_RULES = {
    RangeRule{.table = "Players",
              .column = "id",
              .minimum = 1,
              .maximum = PLAYER_MAX},
    RangeRule{.table = "Players",
              .column = "team_id",
              .minimum = 0,
              .maximum = TEAM_MAX},
    RangeRule{
        .table = "Players", .column = "age", .minimum = 0, .maximum = BYTE_MAX},
    RangeRule{.table = "Players",
              .column = "contract_years",
              .minimum = 0,
              .maximum = BYTE_MAX},
    RangeRule{.table = "Players",
              .column = "height",
              .minimum = 0,
              .maximum = BYTE_MAX},
    RangeRule{.table = "Players",
              .column = "wage",
              .minimum = 0,
              .maximum = PLAYER_MAX},
    RangeRule{.table = "Players",
              .column = "status",
              .minimum = 0,
              .maximum = PLAYER_MAX},
    RangeRule{.table = "Players",
              .column = "squad_number",
              .minimum = 0,
              .maximum = SquadNumbers::MAX_NUMBER},
    RangeRule{
        .table = "Teams", .column = "id", .minimum = 1, .maximum = TEAM_MAX},
    RangeRule{.table = "Teams",
              .column = "league_id",
              .minimum = 1,
              .maximum = LEAGUE_MAX},
    RangeRule{.table = "Teams",
              .column = "reputation",
              .minimum = 0,
              .maximum = MAX_STAT_VAL},
    RangeRule{.table = "Teams",
              .column = "training_facilities",
              .minimum = 0,
              .maximum = MAX_STAT_VAL},
    RangeRule{.table = "Teams",
              .column = "youth_facilities",
              .minimum = 0,
              .maximum = MAX_STAT_VAL},
    RangeRule{.table = "Teams",
              .column = "stadium_capacity",
              .minimum = 0,
              .maximum = std::numeric_limits<int>::max()},
    RangeRule{.table = "Teams",
              .column = "ticket_price",
              .minimum = 0,
              .maximum = std::numeric_limits<int>::max()},
    RangeRule{.table = "Leagues",
              .column = "id",
              .minimum = 1,
              .maximum = LEAGUE_MAX},
    RangeRule{.table = "GameState",
              .column = "current_season",
              .minimum = 1,
              .maximum = BYTE_MAX},
    RangeRule{.table = "GameState",
              .column = "managed_team_id",
              .minimum = 0,
              .maximum = TEAM_MAX},
    RangeRule{.table = "Fixtures",
              .column = "match_type",
              .minimum = 0,
              .maximum = static_cast<int>(MatchType::CONTINENTAL)},
    RangeRule{
        .table = "Fixtures", .column = "played", .minimum = 0, .maximum = 1},
    RangeRule{.table = "Fixtures",
              .column = "extra_time",
              .minimum = 0,
              .maximum = 1},
    RangeRule{.table = "Fixtures",
              .column = "kickoff",
              .minimum = 0,
              .maximum = LAST_KICKOFF_MINUTE},
    RangeRule{.table = "ManagerProfile",
              .column = "age",
              .minimum = 0,
              .maximum = BYTE_MAX},
    RangeRule{.table = "ManagerProfile",
              .column = "licence",
              .minimum = 0,
              .maximum = static_cast<int>(CoachingLicence::COUNT) - 1},
    RangeRule{.table = "ManagerProfile",
              .column = "style",
              .minimum = 0,
              .maximum = static_cast<int>(ManagerStyle::COUNT) - 1},
    RangeRule{.table = "ManagerProfile",
              .column = "background",
              .minimum = 0,
              .maximum = static_cast<int>(ManagerBackground::COUNT) - 1},
    RangeRule{
        .table = "Staff", .column = "age", .minimum = 0, .maximum = BYTE_MAX},
    RangeRule{.table = "Staff",
              .column = "role",
              .minimum = 0,
              .maximum = static_cast<int>(StaffRole::COUNT) - 1},
    RangeRule{.table = "Staff",
              .column = "wage",
              .minimum = 0,
              .maximum = PLAYER_MAX},
    RangeRule{.table = "Staff",
              .column = "contract_years",
              .minimum = 0,
              .maximum = BYTE_MAX}};

void validateRange(const RangeRule& rule, const RuntimeEditor::Value& value)
{
  const auto* integer = std::get_if<std::int64_t>(&value);
  if (integer == nullptr || *integer < rule.minimum || *integer > rule.maximum)
  {
    throw std::runtime_error(
        std::string(rule.column) + ": expected integer in " +
        std::to_string(rule.minimum) + ".." + std::to_string(rule.maximum));
  }
}

void validateStats(const std::string* encoded)
{
  if (encoded == nullptr)
  {
    throw std::runtime_error("stats: expected a JSON object");
  }
  const auto stats = nlohmann::json::parse(*encoded);
  if (!stats.is_object())
  {
    throw std::runtime_error("stats: expected a JSON object");
  }
  for (const auto& [name, number] : stats.items())
  {
    if (!number.is_number())
    {
      throw std::runtime_error("stats." + name + ": expected a number");
    }
    const auto value = number.get<double>();
    if (!std::isfinite(value) || value < MIN_STAT_VAL || value > MAX_STAT_VAL)
    {
      throw std::runtime_error("stats." + name +
                               ": outside the engine attribute range");
    }
  }
}

void validateCsv(const std::string& name, const std::string* encoded)
{
  if (encoded == nullptr)
  {
    throw std::runtime_error(name + ": expected encoded numeric values");
  }
  const std::size_t expected =
      name == "traits" ? TRAIT_FIELDS : DYNAMICS_FIELDS;
  std::size_t count = 0;
  std::size_t start = 0;
  while (start < encoded->size())
  {
    const auto end = encoded->find(',', start);
    const auto token =
        encoded->substr(start, end == std::string::npos ? end : end - start);
    const auto value =
        std::get<double>(RuntimeEditor::parse(SQLITE_FLOAT, token));
    const bool ordinal = name == "dynamics" && (count == 7 || count == 8);
    const double minimum =
        ordinal ? std::numeric_limits<std::int32_t>::min() : 0;
    const double maximum = name == "traits" ? MAX_STAT_VAL
                           : ordinal ? std::numeric_limits<std::int32_t>::max()
                           : count == 12
                               ? BYTE_MAX
                               : std::numeric_limits<std::uint16_t>::max();
    if (value < minimum || value > maximum)
    {
      throw std::runtime_error(name +
                               ": numeric value outside its storage range");
    }
    ++count;
    if (end == std::string::npos)
    {
      break;
    }
    start = end + 1;
  }
  if (count != expected)
  {
    throw std::runtime_error(name + ": incorrect number of numeric values");
  }
}

void validatePlayerField(const std::string& name,
                         const RuntimeEditor::Value& value)
{
  const auto* string = std::get_if<std::string>(&value);
  if (name == "stats")
  {
    validateStats(string);
  }
  else if (name == "role")
  {
    if (string == nullptr ||
        RoleUtils::toString(RoleUtils::fromString(*string)) != *string)
    {
      throw std::runtime_error("role: unknown position");
    }
  }
  else if (name == "nationality")
  {
    if (string == nullptr || !stringToLanguage.contains(*string))
    {
      throw std::runtime_error("nationality: unknown language code");
    }
  }
  else if (name == "foot")
  {
    if (string == nullptr || (*string != "Left" && *string != "Right"))
    {
      throw std::runtime_error("foot: expected Left or Right");
    }
  }
  else if (name == "traits" || name == "dynamics")
  {
    validateCsv(name, string);
  }
}

void validateField(const RuntimeEditor::Change& change, const std::string& name,
                   const RuntimeEditor::Value& value)
{
  for (const auto& rule : RANGE_RULES)
  {
    if (rule.table == change.table && rule.column == name)
    {
      validateRange(rule, value);
    }
  }
  if (change.table == "Players")
  {
    validatePlayerField(name, value);
  }
  if ((change.table == "ManagerProfile" || change.table == "Staff") &&
      name == "nationality")
  {
    validateRange({.table = change.table,
                   .column = name,
                   .minimum = 0,
                   .maximum = BYTE_MAX},
                  value);
    if (!languageToString.contains(
            static_cast<Language>(std::get<std::int64_t>(value))))
    {
      throw std::runtime_error("nationality: unknown nationality");
    }
  }
  if (change.table == "Fixtures" &&
      (name == "home_goals" || name == "away_goals" ||
       name == "home_penalties" || name == "away_penalties") &&
      !std::holds_alternative<std::monostate>(value))
  {
    validateRange({.table = change.table,
                   .column = name,
                   .minimum = 0,
                   .maximum = BYTE_MAX},
                  value);
  }
  // These are model-owned date fields, not a heuristic applied to custom
  // tables.
  if ((change.table == "GameState" || change.table == "Fixtures") &&
      name == "game_date")
  {
    const auto* string = std::get_if<std::string>(&value);
    if (string == nullptr)
    {
      throw std::runtime_error(name + ": expected a calendar date");
    }
    (void)GameDateValue::fromString(*string);
  }
}
}  // namespace

void validate(sqlite3* /*db*/,
              const std::vector<RuntimeEditor::Change>& changes)
{
  for (const auto& change : changes)
  {
    if (change.table == "save_meta" || change.table == "schema_migrations")
    {
      throw std::runtime_error("Save-format metadata is managed by the engine");
    }
    if (change.operation != RuntimeEditor::Operation::Update &&
        (change.table == "GameState" || change.table == "WorldState"))
    {
      throw std::runtime_error(
          "Required career records cannot be inserted or deleted");
    }
    for (const auto& [name, value] : change.fields)
    {
      validateField(change, name, value);
    }
  }
}
}  // namespace EditorValidation
