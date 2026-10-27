// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "database/datagenerator.h"
#include "database/world/world_runtime.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
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
/** Section of the installed world (kept alive by the shared pointer). */
struct WorldSection
{
  std::shared_ptr<const World::Document> world = World::current();
  const json& items;
  explicit WorldSection(World::Kind kind) : items(world->section(kind)) {}
};

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

KitPalette parseKitPalette(const json& value)
{
  KitPalette kit;
  if (!value.is_object() || !value.contains("shirt")) return kit;
  const auto rgb = [&value](const char* key, std::uint32_t fallback)
  { return parseColour(value.value(key, json()), fallback); };
  kit.shirt = rgb("shirt", 0);
  kit.shorts = rgb("shorts", kit.shirt);
  kit.socks = rgb("socks", kit.shorts);
  kit.trim = rgb("trim", kit.shorts);
  kit.valid = true;
  return kit;
}

/** Squared RGB distance between two 0xRRGGBB colours. */
int colourDistanceSquared(std::uint32_t first, std::uint32_t second)
{
  const auto channel = [](std::uint32_t rgb, int shift)
  { return static_cast<int>((rgb >> shift) & 0xFFU); };
  const int dr = channel(first, 16) - channel(second, 16);
  const int dg = channel(first, 8) - channel(second, 8);
  const int db = channel(first, 0) - channel(second, 0);
  return dr * dr + dg * dg + db * db;
}

/** The candidate colour that reads best against the shirt (for the badge). */
std::uint32_t contrastingColour(std::uint32_t shirt,
                                std::initializer_list<std::uint32_t> options)
{
  std::uint32_t best = shirt;
  int bestDistance = -1;
  for (const std::uint32_t option : options)
  {
    const int distance = colourDistanceSquared(shirt, option);
    if (distance > bestDistance)
    {
      bestDistance = distance;
      best = option;
    }
  }
  return best;
}

/**
 * Plausible skin tone (0 lightest .. 7 darkest) for a player with no authored
 * value, from his nationality with a small deterministic spread so mixed
 * squads vary. Exact tones come from the optional "skin" field instead.
 */
std::uint8_t defaultSkinTone(Language nationality, PlayerID id)
{
  const auto jitter = [id](int base, int spread)
  {
    const std::uint32_t hash =
        static_cast<std::uint32_t>(id) * 2654435761U;
    const int span = spread * 2 + 1;
    const int delta =
        static_cast<int>((hash >> 27U) % static_cast<std::uint32_t>(span)) -
        spread;
    return static_cast<std::uint8_t>(std::clamp(base + delta, 0, 7));
  };
  using L = Language;
  switch (nationality)
  {
    // Northern, Central and Eastern Europe: fair.
    case L::SE: case L::NO: case L::DK: case L::FI: case L::IS:
    case L::FO: case L::EE: case L::LV: case L::LT:
    case L::EN: case L::SC: case L::WA: case L::NI: case L::IE:
    case L::NL: case L::DE: case L::AT: case L::CH: case L::BE:
    case L::LX: case L::PL: case L::CZ: case L::SK: case L::HU:
    case L::SI: case L::RU: case L::UA: case L::BY: case L::AU:
    case L::NZ:
      return jitter(0, 1);
    // Southern Europe, the Balkans, Turkey and Israel: olive.
    case L::IT: case L::ES: case L::PT: case L::GR: case L::TR:
    case L::FR: case L::IL: case L::CY: case L::MT: case L::RO:
    case L::BU: case L::AL: case L::MD: case L::KV: case L::RS:
    case L::HR: case L::BA: case L::ME: case L::MO:
    case L::GE: case L::AM: case L::AR:
      return jitter(2, 1);
    // North Africa, the Middle East and Central Asia.
    case L::MA: case L::DZ: case L::TN: case L::LY: case L::EG:
    case L::MR: case L::SA: case L::AE: case L::JO: case L::LB:
    case L::SY: case L::PS: case L::IQ: case L::IR: case L::AF:
    case L::YE: case L::KZ: case L::UZ: case L::KG: case L::TJ:
    case L::TM: case L::AZ:
      return jitter(3, 1);
    // Latin America: a broad mix.
    case L::MX: case L::BR: case L::CO: case L::EC: case L::PE:
    case L::PY: case L::CL: case L::VE: case L::UY: case L::BO:
    case L::AN: case L::CR: case L::PA: case L::GT: case L::HN:
    case L::SV: case L::NC: case L::BZ: case L::CU: case L::DO:
    case L::PR: case L::SR: case L::US: case L::CA:
      return jitter(3, 2);
    // East Asia.
    case L::JP: case L::KR: case L::CN:
      return jitter(1, 1);
    // South and South-East Asia.
    case L::TH: case L::PH: case L::MY: case L::ID: case L::SG:
    case L::BD:
      return jitter(4, 1);
    // Sub-Saharan Africa and the African diaspora.
    case L::SN: case L::CI: case L::CM: case L::GH: case L::NG:
    case L::ML: case L::GN: case L::BF: case L::CG: case L::GM:
    case L::GW: case L::AO: case L::CV: case L::TD: case L::GQ:
    case L::GA: case L::ZM: case L::KM: case L::SL: case L::KE:
    case L::NA: case L::NR: case L::BI: case L::CF: case L::ZW:
    case L::TZ: case L::RW: case L::SO: case L::MU: case L::MG:
    case L::MW: case L::MZ: case L::UG: case L::ZA:
    case L::JM: case L::TT: case L::BB: case L::HT: case L::CW:
    case L::GP: case L::MQ: case L::GF: case L::KY: case L::BM:
    case L::GD: case L::LC: case L::VC: case L::KN:
      return jitter(6, 1);
    default:
      return jitter(2, 2);
  }
}
constexpr std::uint32_t DEFAULT_WAGE = 5'000;
constexpr std::uint8_t DEFAULT_CONTRACT_YEARS = 2;
constexpr std::uint8_t DEFAULT_HEIGHT = 180;
constexpr float DEFAULT_RATING = 50.0F;

/** The authored attributes plus every configured one it leaves out, at the
 * mean of those given. */
std::map<std::string, float> completeStats(const json& authored,
                                           const StatsConfig& config)
{
  std::map<std::string, float> stats;
  float total = 0;
  for (const auto& [name, value] : authored.items())
  {
    if (!value.is_number()) continue;
    const float rating = std::clamp(value.get<float>(), 0.0F, 100.0F);
    stats.emplace(name, rating);
    total += rating;
  }
  const float mean =
      stats.empty() ? DEFAULT_RATING : total / static_cast<float>(stats.size());
  for (const auto& name : config.possible_stats) stats.try_emplace(name, mean);
  return stats;
}
}  // namespace

std::vector<League> DataGenerator::generateLeagues()
{
  const WorldSection data(World::Kind::League);
  std::vector<League> leagues;

  for (const auto& item : data.items)
  {
    uint8_t id = item.at("id").get<uint8_t>();
    std::string name = item.at("name").get<std::string>();

    std::optional<uint8_t> parent = std::nullopt;
    if (const auto found = item.find("parent_league");
        found != item.end() && found->is_number_integer() &&
        found->get<int>() != 0)
    {
      parent = found->get<uint8_t>();
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
  for (const json& item : WorldSection(World::Kind::Club).items)
  {
    teams.emplace_back(item.at("id").get<uint16_t>(),
                       item.at("league_id").get<uint8_t>(),
                       item.at("name").get<std::string>(),
                       item.value<int64_t>("balance", 50'000'000));
  }
  return teams;
}

const std::unordered_map<TeamID, ClubIdentity>&
DataGenerator::clubIdentities()
{
  static World::PerWorld<std::unordered_map<TeamID, ClubIdentity>> cache;
  return cache.get(&loadClubIdentities);
}

std::unordered_map<TeamID, ClubIdentity> DataGenerator::loadClubIdentities()
{
  std::unordered_map<TeamID, ClubIdentity> identities;
  for (const json& item : WorldSection(World::Kind::Club).items)
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
          identity.home_kit =
              parseKitPalette(colours->value("home", json()));
          identity.away_kit =
              parseKitPalette(colours->value("away", json()));
          identity.third_kit =
              parseKitPalette(colours->value("third", json()));
          // The badge and UI theme follow the home strip: the shirt colour
          // leads, with the most contrasting piece as the second tone.
          if (identity.home_kit.valid)
          {
            identity.primary_colour = identity.home_kit.shirt;
            identity.secondary_colour = contrastingColour(
                identity.home_kit.shirt,
                {identity.home_kit.trim, identity.home_kit.shorts,
                 identity.home_kit.socks, identity.secondary_colour});
          }
        }
        if (const auto stadium = item.find("stadium"); stadium != item.end())
        {
          identity.stadium_name = stadium->value("name", std::string());
          identity.stadium_capacity =
              stadium->value<std::uint32_t>("capacity", 0);
        }
        identities.emplace(item.at("id").get<TeamID>(), std::move(identity));
      }
  return identities;
}

const std::unordered_map<std::string, std::string>&
DataGenerator::clubArticles()
{
  static World::PerWorld<std::unordered_map<std::string, std::string>> cache;
  return cache.get(
      []
      {
        std::unordered_map<std::string, std::string> articles;
        for (const json& item : WorldSection(World::Kind::Club).items)
        {
          std::string article = item.value("article_it", std::string());
          if (!article.empty())
            articles.emplace(item.at("name").get<std::string>(),
                             std::move(article));
        }
        return articles;
      });
}

const std::unordered_map<PlayerID, std::uint8_t>& DataGenerator::playerSkinTones()
{
  static World::PerWorld<std::unordered_map<PlayerID, std::uint8_t>> cache;
  return cache.get(
      []
      {
        std::unordered_map<PlayerID, std::uint8_t> tones;
        for (const json& item : WorldSection(World::Kind::Player).items)
        {
          const PlayerID id = item.at("id").get<PlayerID>();
          if (const auto skin = item.find("skin");
              skin != item.end() && skin->is_number_integer())
          {
            tones[id] = static_cast<std::uint8_t>(
                std::clamp(skin->get<int>(), 0, 7));
            continue;
          }
          const auto found =
              stringToLanguage.find(item.value("nationality", std::string()));
          tones[id] = defaultSkinTone(
              found != stringToLanguage.end() ? found->second : Language::EN,
              id);
        }
        return tones;
      });
}

std::vector<Player> DataGenerator::generatePlayers(const GameData& gamedata)
{
  const StatsConfig& stats_config = gamedata.getStatsConfig();
  std::vector<Player> players;
  std::map<TeamID, std::size_t> player_counts;
  std::map<TeamID, std::int64_t> player_wages;
  PlayerID next_player_id = 50'000;

  // Players authored by the world; optional fields fall back to plain
  // defaults and missing attributes to the mean of the given ones, so
  // partial worlds (e.g. only names, positions and a few ratings) still play.
  const WorldSection authored(World::Kind::Player);
  for (const json& item : authored.items)
  {
    TeamID team_id = item.value<TeamID>("team_id", FREE_AGENTS_TEAM_ID);
    if (!gamedata.getTeam(team_id).has_value()) team_id = FREE_AGENTS_TEAM_ID;
    player_counts[team_id]++;

    const auto it =
        stringToLanguage.find(item.value("nationality", std::string()));
    const Language nationality =
        (it != stringToLanguage.end()) ? it->second : Language::EN;
    const std::string foot_text = item.value("preferred_foot", "Right");
    // "Both" is stored; where a single side is required it behaves as
    // Right, matching the pre-Both fallback.
    const Foot foot = foot_text == "Left"   ? Foot::Left
                      : foot_text == "Both" ? Foot::Both
                                            : Foot::Right;
    Player& player = players.emplace_back(
        item.at("id").get<uint32_t>(), team_id,
        item.at("first_name").get<std::string>(),
        item.at("last_name").get<std::string>(),
        RoleUtils::fromString(item.at("role").get<std::string>()), nationality,
        item.value<uint32_t>("wage", DEFAULT_WAGE), item.value("status", 0),
        item.at("age").get<uint8_t>(),
        item.value<uint8_t>("contract_years", DEFAULT_CONTRACT_YEARS),
        item.value<uint8_t>("height", DEFAULT_HEIGHT), foot,
        completeStats(item.at("stats"), stats_config));
    player.setNickname(item.value("nickname", std::string()));
    WorldRng rng = WorldRng::stream(gamedata.getWorldSeed(),
                                    RngDomain::Generation, player.getId());
    WorldGeneration::initializeHiddenAttributes(player, rng, stats_config);
    player_wages[team_id] += player.getWage();
    next_player_id =
        std::max(next_player_id, item.at("id").get<PlayerID>() + 1U);
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
