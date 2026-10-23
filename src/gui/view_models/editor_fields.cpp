// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------
#include "gui/view_models/editor_fields.h"

#include <algorithm>
#include <limits>

#include "global/global.h"
#include "global/languages.h"
#include "model/role_utils.h"

namespace
{
EditorField text(std::string column, std::string label, std::string section)
{
  EditorField field;
  field.column = std::move(column);
  field.label = std::move(label);
  field.section = std::move(section);
  return field;
}
EditorField number(std::string column, std::string label, std::string section,
                   std::optional<double> minimum, std::optional<double> maximum)
{
  auto field = text(std::move(column), std::move(label), std::move(section));
  field.control = EditorField::Control::Number;
  field.minimum = minimum;
  field.maximum = maximum;
  return field;
}
std::vector<EditorField> playerFields()
{
  constexpr auto BYTE_MAX = std::numeric_limits<std::uint8_t>::max();
  constexpr auto PERSONAL = "EDITOR_PERSONAL";
  constexpr auto CONTRACT = "EDITOR_CONTRACT";
  auto stats = text("stats", "EDITOR_ATTRIBUTES", "EDITOR_ATTRIBUTES");
  stats.control = EditorField::Control::Attributes;
  auto role = text("role", "EDITOR_POSITION", PERSONAL);
  role.control = EditorField::Control::Choice;
  for (auto index = std::to_underlying(PlayerRole::GK);
       index < std::to_underlying(PlayerRole::UNKNOWN); ++index)
  {
    const auto value = RoleUtils::toString(static_cast<PlayerRole>(index));
    role.choices.emplace_back(value, "ROLE_LONG_" + value);
  }
  auto nation = text("nationality", "PROFILE_NATIONALITY", PERSONAL);
  nation.control = EditorField::Control::Choice;
  for (const auto& [language, name] : languageToString)
  {
    (void)language;
    nation.choices.emplace_back(name, "NATION_" + name);
  }
  std::ranges::sort(nation.choices);
  auto foot = text("foot", "PROFILE_FOOT", PERSONAL);
  foot.control = EditorField::Control::Choice;
  foot.choices = {{"Left", "PLAYER_FOOT_LEFT"}, {"Right", "PLAYER_FOOT_RIGHT"}};
  return {
      std::move(stats),
      text("first_name", "EDITOR_FIRST_NAME", PERSONAL),
      text("last_name", "EDITOR_LAST_NAME", PERSONAL),
      number("age", "EDITOR_AGE_YEARS", PERSONAL, 0, BYTE_MAX),
      std::move(nation),
      std::move(role),
      std::move(foot),
      number("height", "EDITOR_HEIGHT_CM", PERSONAL, 0, BYTE_MAX),
      number("wage", "EDITOR_WAGE_WEEK", CONTRACT, 0,
             std::numeric_limits<std::uint32_t>::max()),
      number("contract_years", "PLAYER_CONTRACT", CONTRACT, 0, BYTE_MAX),
  };
}
}  // namespace

std::vector<EditorField> editorFields(std::string_view table)
{
  if (table == "Players")
  {
    return playerFields();
  }
  if (table != "Teams")
  {
    return {};
  }
  constexpr auto CLUB = "EDITOR_CLUB_DETAILS";
  constexpr auto FINANCE = "EDITOR_FINANCES";
  // Integer money controls keep exact 64-bit balances; these bounds must not
  // round through a floating point widget.
  return {
      text("name", "EDITOR_CLUB_NAME", CLUB),
      number("reputation", "EDITOR_REPUTATION", CLUB, 0, MAX_STAT_VAL),
      number("stadium_capacity", "EDITOR_STADIUM_SEATS", CLUB, 0,
             std::numeric_limits<int>::max()),
      number("training_facilities", "EDITOR_TRAINING_FACILITIES", CLUB, 0,
             MAX_STAT_VAL),
      number("youth_facilities", "EDITOR_YOUTH_FACILITIES", CLUB, 0,
             MAX_STAT_VAL),
      number("balance", "EDITOR_BALANCE_EURO", FINANCE, std::nullopt,
             std::nullopt),
      number("transfer_budget", "EDITOR_TRANSFER_EURO", FINANCE, 0,
             std::nullopt),
      number("wage_budget", "EDITOR_BUDGET_WEEK", FINANCE, 0, std::nullopt),
      number("ticket_price", "EDITOR_TICKET_EURO", FINANCE, 0,
             std::numeric_limits<int>::max()),
  };
}

std::string_view editorTable(NavSection section)
{
  using enum NavSection;
  switch (section)
  {
    case SQUAD:
    case LINEUP:
    case YOUTH:
    case RESERVES:
    case SQUAD_PLANNER:
    case MEDICAL:
      return "Players";
    case HOME:
    case CLUB:
    case FINANCES:
      return "Teams";
    default:
      return "";
  }
}

bool hasBuiltinEditor(const std::optional<NavEntry>& context)
{
  if (!context)
  {
    return false;
  }
  using enum NavEntry::Kind;
  switch (context->kind)
  {
    case PLAYER:
    case COMPARE:
    case CLUB:
      return true;
    case SECTION:
      return !editorTable(context->section).empty();
    case MATCH_REPORT:
      return false;
  }
  return false;
}
