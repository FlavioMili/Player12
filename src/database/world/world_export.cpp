// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "database/world/world_export.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include "database/datagenerator.h"
#include "database/gamedata.h"
#include "database/world/world_runtime.h"
#include "global/languages.h"
#include "global/nationality.h"
#include "model/role_utils.h"

using json = nlohmann::json;

namespace World
{
namespace
{
std::string hex(std::uint32_t colour)
{
  return std::format("#{:06X}", colour & 0xFFFFFFU);
}

json kitJson(const KitPalette& kit)
{
  return {{"shirt", hex(kit.shirt)},
          {"shorts", hex(kit.shorts)},
          {"socks", hex(kit.socks)},
          {"trim", hex(kit.trim)}};
}

/** Identity fields of a club the source world does not describe. */
void addIdentity(json& club, const ClubIdentity& identity)
{
  if (!identity.short_name.empty())
  {
    club["short_name"] = identity.short_name;
  }
  if (!identity.nickname.empty())
  {
    club["nickname"] = identity.nickname;
  }
  if (identity.founded != 0)
  {
    club["founded"] = identity.founded;
  }
  json colours = {{"primary", hex(identity.primary_colour)},
                  {"secondary", hex(identity.secondary_colour)}};
  const std::pair<const char*, const KitPalette*> kits[] = {
      {"home", &identity.home_kit},
      {"away", &identity.away_kit},
      {"third", &identity.third_kit}};
  for (const auto& [key, kit] : kits)
  {
    if (kit->valid)
    {
      colours[key] = kitJson(*kit);
    }
  }
  club["colours"] = std::move(colours);
  if (!identity.stadium_name.empty())
  {
    club["stadium"] = {{"name", identity.stadium_name},
                       {"capacity", identity.stadium_capacity}};
  }
}

std::string footName(Foot foot)
{
  return foot == Foot::Left ? "Left" : foot == Foot::Both ? "Both" : "Right";
}

json statsJson(const Player& player)
{
  json stats = json::object();
  for (const auto& [name, value] : player.getStats())
  {
    stats[name] = std::clamp(static_cast<int>(std::lround(value)), 0, 100);
  }
  return stats;
}

}  // namespace

Document exportCareer(const GameData& data, const Document& source,
                      std::string_view label, std::string_view date, int season)
{
  Document out = source;
  Manifest manifest = source.manifest();
  const std::string base = label.empty() ? manifest.name : std::string(label);
  manifest.name = date.empty() ? base : base + " – " + std::string(date);
  manifest.version = "1.0";
  manifest.schema = SCHEMA;
  if (season > 0)
  {
    manifest.season = season;
  }
  out.setManifest(manifest);

  // Competitions: the career's own names, parents and tiebreaks, with the
  // economy profile each one plays with.
  std::vector<const League*> leagues;
  for (const auto& [id, league] : data.getLeagues())
  {
    if (id <= maxId(Kind::League))
    {
      leagues.push_back(&league);
    }
  }
  std::ranges::sort(leagues, {}, &League::getId);
  json league_rows = json::array();
  for (const League* league : leagues)
  {
    const json* known = source.find(Kind::League, league->getId());
    json row = known != nullptr ? *known : json::object();
    row["id"] = league->getId();
    row["name"] = league->getName();
    row.erase("parent_league");
    if (const auto parent = league->getParentLeagueID())
    {
      row["parent_league"] = *parent;
    }
    row["tiebreak"] = league->getTieBreakRule() == TieBreakRule::HEAD_TO_HEAD
                          ? "head_to_head"
                          : "goal_difference";
    row["profile"] = profileToJson(leagueProfile(league->getId()));
    league_rows.push_back(std::move(row));
  }
  out.setSection(Kind::League, std::move(league_rows));

  // Clubs: current names and competitions over the source's identity.
  std::vector<const Team*> teams;
  for (const auto& [id, team] : data.getTeams())
  {
    if (id != FREE_AGENTS_TEAM_ID)
    {
      teams.push_back(&team);
    }
  }
  std::ranges::sort(teams, {}, &Team::getId);
  const auto& identities = DataGenerator::clubIdentities();
  json club_rows = json::array();
  for (const Team* team : teams)
  {
    const json* known = source.find(Kind::Club, team->getId());
    json row = known != nullptr ? *known : json::object();
    row["id"] = team->getId();
    row["name"] = team->getName();
    row["league_id"] = team->getLeagueId();
    // The club's current stature, so a new career from this world starts
    // with the same pecking order.
    if (team->getReputation() > 0)
    {
      row["reputation"] = team->getReputation();
    }
    if (known == nullptr)
    {
      if (const auto identity = identities.find(team->getId());
          identity != identities.end())
      {
        addIdentity(row, identity->second);
      }
    }
    club_rows.push_back(std::move(row));
  }
  out.setSection(Kind::Club, std::move(club_rows));

  // Players: everyone alive in the career, free agents included.
  std::vector<const Player*> players;
  for (const auto& entry : data.getPlayers())
  {
    players.push_back(&entry.second);
  }
  std::ranges::sort(players, {}, &Player::getId);
  const auto& tones = DataGenerator::playerSkinTones();
  json player_rows = json::array();
  for (const Player* player : players)
  {
    const json* known = source.find(Kind::Player, player->getId());
    json row = known != nullptr ? *known : json::object();
    row["id"] = player->getId();
    row["first_name"] = player->getFirstName();
    row["last_name"] = player->getLastName();
    row.erase("nickname");
    if (!player->getNickname().empty())
    {
      row["nickname"] = player->getNickname();
    }
    row["age"] = player->getAge();
    row["role"] = RoleUtils::toString(player->getRole());
    if (const std::string name = nationalityName(player->getNationality());
        !name.empty())
    {
      row["nationality"] = name;
    }
    row["preferred_foot"] = footName(player->getFoot());
    row["height"] = player->getHeight();
    row["wage"] = player->getWage();
    row["contract_years"] = player->getContractYears();
    row["stats"] = statsJson(*player);
    row.erase("team_id");
    if (player->getTeamId() != FREE_AGENTS_TEAM_ID)
    {
      row["team_id"] = player->getTeamId();
    }
    if (!row.contains("skin"))
    {
      if (const auto tone = tones.find(player->getId()); tone != tones.end())
      {
        row["skin"] = tone->second;
      }
    }
    player_rows.push_back(std::move(row));
  }
  out.setSection(Kind::Player, std::move(player_rows));
  return out;
}
}  // namespace World
