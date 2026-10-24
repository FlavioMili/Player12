// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "database/datagenerator.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "gamedata.h"
#include "global/languages.h"
#include "global/logger.h"
#include "global/paths.h"
#include "global/stats_config.h"
#include "model/club_economy.h"
#include "model/league.h"
#include "model/player.h"
#include "model/role_utils.h"
#include "model/team.h"
#include "model/world_generation.h"
#include "model/world_rng.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
/** Calls @p visit for every element of every .json array in @p directory. */
template <typename Visitor>
void forEachPackItem(const std::string& directory, Visitor&& visit)
{
  for (const auto& entry : fs::directory_iterator(directory))
  {
    if (!entry.is_regular_file() || entry.path().extension() != ".json")
      continue;
    std::ifstream f(entry.path());
    for (const auto& item : json::parse(f)) visit(item);
  }
}

/** Parses "#RRGGBB" into 0xRRGGBB; keeps @p fallback when malformed. */
std::uint32_t parseColour(const json& value, std::uint32_t fallback)
{
  if (!value.is_string()) return fallback;
  const auto& text = value.get_ref<const std::string&>();
  std::uint32_t rgb = 0;
  if (text.size() != 7 || text[0] != '#') return fallback;
  const auto [end, error] =
      std::from_chars(text.data() + 1, text.data() + text.size(), rgb, 16);
  return error == std::errc{} && end == text.data() + text.size() ? rgb
                                                                  : fallback;
}
}  // namespace

std::vector<League> DataGenerator::generateLeagues()
{
  std::ifstream f(AssetPaths::leagues());
  json data = json::parse(f);

  std::vector<League> leagues;

  for (const auto& item : data)
  {
    uint8_t id = item.at("id").get<uint8_t>();
    std::string name = item.at("name").get<std::string>();

    std::optional<uint8_t> parent = std::nullopt;
    if (item.contains("parent_league"))
    {
      parent = item.at("parent_league").get<uint8_t>();
    }

    const TieBreakRule tie_break =
        item.value("tiebreak", std::string()) == "head_to_head"
            ? TieBreakRule::HEAD_TO_HEAD
            : TieBreakRule::GOAL_DIFFERENCE;
    leagues.emplace_back(id, name, std::vector<uint16_t>{}, parent, tie_break);
  }
  return leagues;
}

std::vector<Team> DataGenerator::generateTeams()
{
  std::vector<Team> teams;
  forEachPackItem(AssetPaths::teamsDir(),
                  [&teams](const json& item)
                  {
                    teams.emplace_back(
                        item.at("id").get<uint16_t>(),
                        item.at("league_id").get<uint8_t>(),
                        item.at("name").get<std::string>(),
                        item.value<int64_t>("balance", 50'000'000));
                  });
  return teams;
}

std::unordered_map<TeamID, ClubIdentity> DataGenerator::loadClubIdentities()
{
  std::unordered_map<TeamID, ClubIdentity> identities;
  forEachPackItem(
      AssetPaths::teamsDir(),
      [&identities](const json& item)
      {
        ClubIdentity identity;
        identity.short_name = item.value("short_name", std::string());
        identity.nickname = item.value("nickname", std::string());
        identity.founded = item.value<std::uint16_t>("founded", 0);
        if (const auto colours = item.find("colours"); colours != item.end())
        {
          identity.primary_colour = parseColour(
              colours->value("primary", json()), identity.primary_colour);
          identity.secondary_colour = parseColour(
              colours->value("secondary", json()), identity.secondary_colour);
        }
        if (const auto stadium = item.find("stadium"); stadium != item.end())
        {
          identity.stadium_name = stadium->value("name", std::string());
          identity.stadium_capacity =
              stadium->value<std::uint32_t>("capacity", 0);
        }
        identities.emplace(item.at("id").get<TeamID>(), std::move(identity));
      });
  return identities;
}

std::unordered_map<std::string, std::string> DataGenerator::loadClubArticles()
{
  std::unordered_map<std::string, std::string> articles;
  forEachPackItem(AssetPaths::teamsDir(),
                  [&articles](const json& item)
                  {
                    std::string article =
                        item.value("article_it", std::string());
                    if (!article.empty())
                      articles.emplace(item.at("name").get<std::string>(),
                                       std::move(article));
                  });
  return articles;
}

std::vector<Player> DataGenerator::generatePlayers(const GameData& gamedata)
{
  const StatsConfig& stats_config = gamedata.getStatsConfig();
  std::vector<Player> players;
  std::map<TeamID, std::size_t> player_counts;
  std::map<TeamID, std::int64_t> player_wages;
  PlayerID next_player_id = 50'000;

  // Load pre-defined players from JSON and count them
  for (const auto& entry : fs::directory_iterator(AssetPaths::playersDir()))
  {
    if (entry.is_regular_file() && entry.path().extension() == ".json")
    {
      std::ifstream f(entry.path());
      json data = json::parse(f);
      for (const auto& item : data)
      {
        TeamID team_id = item.at("team_id").get<uint16_t>();
        player_counts[team_id]++;

        auto it =
            stringToLanguage.find(item.at("nationality").get<std::string>());
        Language nationality =
            (it != stringToLanguage.end()) ? it->second : Language::EN;
        Foot foot = (item.at("preferred_foot").get<std::string>() == "Left")
                        ? Foot::Left
                        : Foot::Right;

        Player& player = players.emplace_back(
            item.at("id").get<uint32_t>(), team_id,
            item.at("first_name").get<std::string>(),
            item.at("last_name").get<std::string>(),
            RoleUtils::fromString(item.at("role").get<std::string>()),
            nationality, item.at("wage").get<uint32_t>(),
            item.value("status", 0), item.at("age").get<uint8_t>(),
            item.at("contract_years").get<uint8_t>(),
            item.at("height").get<uint8_t>(), foot,
            item.at("stats").get<std::map<std::string, float>>());
        WorldRng rng = WorldRng::stream(gamedata.getWorldSeed(),
                                        RngDomain::Generation, player.getId());
        WorldGeneration::initializeHiddenAttributes(player, rng, stats_config);
        player_wages[team_id] += player.getWage();
        next_player_id =
            std::max(next_player_id, item.at("id").get<PlayerID>() + 1U);
      }
    }
  }

  // Complete every club's 30-man squad around its reputation-based level.
  const auto economies = buildLeagueEconomies(gamedata);
  const auto& teams = gamedata.getTeamsVector();
  Logger::debug(
      std::format("Ensuring player rosters for {} teams.", teams.size()));
  // Sorted so that player ids do not depend on hash-map iteration order.
  std::vector<std::reference_wrapper<const Team>> ordered(teams.begin(),
                                                          teams.end());
  std::ranges::sort(ordered, {},
                    [](const auto& team) { return team.get().getId(); });
  size_t generated_players = 0;
  for (const auto& team_ref : ordered)
  {
    const Team& team = team_ref.get();
    const auto economy = economies.find(team.getLeagueId());
    if (team.getId() == FREE_AGENTS_TEAM_ID || economy == economies.end())
      continue;
    auto squad = WorldGeneration::generateSquad(
        team, economy->second, player_counts[team.getId()],
        player_wages[team.getId()], next_player_id, gamedata.getWorldSeed(),
        stats_config);
    generated_players += squad.size();
    std::ranges::move(squad, std::back_inserter(players));
  }
  Logger::debug(std::format("Generated {} players to complete all rosters.",
                            generated_players));
  return players;
}
