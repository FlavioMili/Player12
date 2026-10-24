// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/view_models/player_view.h"

#include <algorithm>
#include <cctype>

#include "controller/game_controller.h"
#include "global/language_manager.h"
#include "gui/widgets/format.h"
#include "model/player.h"
#include "model/role_utils.h"

namespace
{
struct GroupFocus
{
  PlayerView::PositionGroup group;
  const char* focus_key;
};

// Keys match the role_focus categories in assets/config/stats_config.json.
constexpr std::array<GroupFocus, 4> GROUP_FOCUS = {{
    {PlayerView::PositionGroup::GOALKEEPER, "Goalkeeper"},
    {PlayerView::PositionGroup::DEFENDER, "Defender"},
    {PlayerView::PositionGroup::MIDFIELDER, "Midfielder"},
    {PlayerView::PositionGroup::FORWARD, "Striker"},
}};
}  // namespace

namespace PlayerView
{

PlayerRow makeRow(const GameController& controller, const Player& player)
{
  PlayerRow row;
  row.id = player.getId();
  row.team_id = player.getTeamId();
  row.name = player.getName();
  row.name_lower = toLower(row.name);
  row.role_id = player.getRole();
  row.role = RoleUtils::shortName(row.role_id);
  row.group = groupOf(row.role_id);
  row.age = player.getAge();
  row.squad_number = player.getSquadNumber();
  row.overall =
      static_cast<float>(player.getOverall(controller.getStatsConfig()));
  row.wage = player.getWage();
  row.contract_years = player.getContractYears();
  row.market_value = controller.getPlayerMarketValue(row.id);
  row.listed = controller.isPlayerListed(row.id);
  const PlayerDynamics& dynamics = player.getDynamics();
  row.condition = dynamics.condition;
  row.morale = dynamics.morale;
  row.form = player.getForm();
  row.injury_days = dynamics.injury_days;
  row.suspension = controller.getSuspensionMatches(row.id, MatchType::LEAGUE);
  row.value_text = Format::money(row.market_value);
  row.wage_text = Format::money(row.wage);
  return row;
}

PositionGroup groupOf(PlayerRole role)
{
  using enum PlayerRole;
  using enum PositionGroup;
  switch (role)
  {
    case GK:
      return GOALKEEPER;
    case CB:
    case LB:
    case RB:
      return DEFENDER;
    case LW:
    case RW:
    case ST:
      return FORWARD;
    default:
      return MIDFIELDER;
  }
}

const char* groupKey(PositionGroup group)
{
  using enum PositionGroup;
  switch (group)
  {
    case GOALKEEPER:
      return "POSITION_GROUP_GK";
    case DEFENDER:
      return "POSITION_GROUP_DEF";
    case MIDFIELDER:
      return "POSITION_GROUP_MID";
    case FORWARD:
      return "POSITION_GROUP_ATT";
  }
  return "POSITION_GROUP_MID";
}

std::vector<RoleFit> roleFits(const Player& player, const StatsConfig& config)
{
  std::vector<RoleFit> fits;
  fits.reserve(GROUP_FOCUS.size());
  const auto& stats = player.getStats();
  for (const GroupFocus& focus : GROUP_FOCUS)
  {
    const auto config_it = config.role_focus.find(focus.focus_key);
    if (config_it == config.role_focus.end()) continue;
    const RoleFocus& role = config_it->second;
    double rating = 0.0;
    for (size_t index = 0;
         index < std::min(role.stats.size(), role.weights.size()); ++index)
    {
      if (const auto stat = stats.find(role.stats[index]); stat != stats.end())
        rating += static_cast<double>(stat->second) * role.weights[index];
    }
    fits.push_back({focus.group, static_cast<float>(rating)});
  }
  std::ranges::sort(fits, [](const RoleFit& left, const RoleFit& right)
                    { return left.rating > right.rating; });
  return fits;
}

std::string statLabel(std::string_view statName)
{
  const std::string key = "STAT_" + std::string(statName);
  const char* localized = LOC(key.c_str());
  // LOC hands back the key itself when no translation exists.
  return key == localized ? std::string(statName) : std::string(localized);
}

std::string toLower(std::string_view text)
{
  std::string lower(text);
  std::ranges::transform(
      lower, lower.begin(), [](unsigned char character)
      { return static_cast<char>(std::tolower(character)); });
  return lower;
}

}  // namespace PlayerView
