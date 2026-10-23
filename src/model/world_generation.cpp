// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/world_generation.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string_view>

#include "global/global.h"
#include "global/paths.h"
#include "model/role_utils.h"
#include "model/world_rng.h"

namespace
{
constexpr std::array<std::string_view, 9> PROFILE_STATS = {
    "Pace",        "Shooting", "Passing", "Dribbling",  "Defending",
    "Physicality", "Stamina",  "Vision",  "Goalkeeping"};

struct RoleProfile
{
  PlayerRole role;
  std::array<float, PROFILE_STATS.size()> offsets;  // Relative to level.
  float height_mean;
  float left_foot_share;
};

// [P] Attribute shapes per role, in the PROFILE_STATS order: centre-backs
// defend and win duels, wingers are quick dribblers, goalkeepers keep goal.
constexpr std::array<RoleProfile, 12> ROLE_PROFILES = {{
    {PlayerRole::GK,
     {-20.0f, -35.0f, -10.0f, -25.0f, -15.0f, 0.0f, -10.0f, 0.0f, 12.0f},
     187.0f,
     0.20f},
    {PlayerRole::CB,
     {-5.0f, -20.0f, -5.0f, -12.0f, 10.0f, 8.0f, 0.0f, -3.0f, -45.0f},
     187.0f,
     0.20f},
    {PlayerRole::LB,
     {6.0f, -15.0f, 0.0f, 0.0f, 6.0f, 0.0f, 6.0f, -3.0f, -45.0f},
     177.0f,
     0.70f},
    {PlayerRole::RB,
     {6.0f, -15.0f, 0.0f, 0.0f, 6.0f, 0.0f, 6.0f, -3.0f, -45.0f},
     177.0f,
     0.10f},
    {PlayerRole::CDM,
     {-4.0f, -10.0f, 4.0f, -3.0f, 8.0f, 5.0f, 6.0f, 3.0f, -45.0f},
     181.0f,
     0.20f},
    {PlayerRole::CM,
     {-2.0f, -4.0f, 8.0f, 2.0f, 0.0f, 0.0f, 6.0f, 7.0f, -45.0f},
     178.0f,
     0.25f},
    {PlayerRole::CAM,
     {0.0f, 3.0f, 8.0f, 7.0f, -15.0f, -6.0f, 0.0f, 9.0f, -45.0f},
     176.0f,
     0.25f},
    {PlayerRole::LM,
     {7.0f, -2.0f, 4.0f, 5.0f, -8.0f, -4.0f, 7.0f, 2.0f, -45.0f},
     176.0f,
     0.60f},
    {PlayerRole::RM,
     {7.0f, -2.0f, 4.0f, 5.0f, -8.0f, -4.0f, 7.0f, 2.0f, -45.0f},
     176.0f,
     0.15f},
    {PlayerRole::LW,
     {10.0f, 2.0f, 0.0f, 9.0f, -18.0f, -6.0f, 2.0f, 0.0f, -45.0f},
     175.0f,
     0.55f},
    {PlayerRole::RW,
     {10.0f, 2.0f, 0.0f, 9.0f, -18.0f, -6.0f, 2.0f, 0.0f, -45.0f},
     175.0f,
     0.35f},
    {PlayerRole::ST,
     {4.0f, 10.0f, -4.0f, 4.0f, -22.0f, 5.0f, 0.0f, -2.0f, -45.0f},
     183.0f,
     0.25f},
}};

const RoleProfile& roleProfile(PlayerRole role)
{
  const auto found = std::ranges::find(ROLE_PROFILES, role, &RoleProfile::role);
  return found == ROLE_PROFILES.end() ? ROLE_PROFILES[5] : *found;
}

enum class SquadTier : std::uint8_t
{
  Starter,
  Rotation,
  Backup,
  Prospect
};

struct SquadSlot
{
  PlayerRole role;
  SquadTier tier;
};

// Balanced 30-man squad: eleven starters for a 4-4-2 / 4-3-3 shape, depth in
// every line and a few academy prospects.
constexpr std::array<SquadSlot, 30> SQUAD_TEMPLATE = {{
    {PlayerRole::GK, SquadTier::Starter},
    {PlayerRole::GK, SquadTier::Backup},
    {PlayerRole::GK, SquadTier::Prospect},
    {PlayerRole::CB, SquadTier::Starter},
    {PlayerRole::CB, SquadTier::Starter},
    {PlayerRole::CB, SquadTier::Rotation},
    {PlayerRole::CB, SquadTier::Backup},
    {PlayerRole::CB, SquadTier::Prospect},
    {PlayerRole::LB, SquadTier::Starter},
    {PlayerRole::LB, SquadTier::Rotation},
    {PlayerRole::RB, SquadTier::Starter},
    {PlayerRole::RB, SquadTier::Rotation},
    {PlayerRole::CDM, SquadTier::Starter},
    {PlayerRole::CDM, SquadTier::Backup},
    {PlayerRole::CM, SquadTier::Starter},
    {PlayerRole::CM, SquadTier::Starter},
    {PlayerRole::CM, SquadTier::Rotation},
    {PlayerRole::CM, SquadTier::Prospect},
    {PlayerRole::CAM, SquadTier::Rotation},
    {PlayerRole::CAM, SquadTier::Backup},
    {PlayerRole::LM, SquadTier::Rotation},
    {PlayerRole::RM, SquadTier::Rotation},
    {PlayerRole::LW, SquadTier::Starter},
    {PlayerRole::LW, SquadTier::Prospect},
    {PlayerRole::RW, SquadTier::Starter},
    {PlayerRole::RW, SquadTier::Backup},
    {PlayerRole::ST, SquadTier::Starter},
    {PlayerRole::ST, SquadTier::Rotation},
    {PlayerRole::ST, SquadTier::Backup},
    {PlayerRole::ST, SquadTier::Prospect},
}};

// Foreign nationalities weighted by how common they are in European squads.
// [P]
struct NationalityWeight
{
  Language nationality;
  float weight;
};
constexpr std::array<NationalityWeight, 26> EUROPEAN_FOREIGN_POOL = {{
    {Language::BR, 9.0f}, {Language::FR, 8.0f}, {Language::ES, 6.0f},
    {Language::PT, 6.0f}, {Language::DE, 4.0f}, {Language::NL, 4.0f},
    {Language::IT, 3.0f}, {Language::EN, 3.0f}, {Language::BE, 3.0f},
    {Language::HR, 3.0f}, {Language::DK, 2.5f}, {Language::SE, 2.0f},
    {Language::NO, 2.0f}, {Language::CH, 2.0f}, {Language::PL, 2.5f},
    {Language::CZ, 1.5f}, {Language::SK, 1.0f}, {Language::HU, 1.0f},
    {Language::UA, 1.5f}, {Language::RO, 1.5f}, {Language::GR, 1.0f},
    {Language::TR, 1.5f}, {Language::US, 1.5f}, {Language::MX, 1.5f},
    {Language::JP, 1.5f}, {Language::KR, 1.0f},
}};

// Imports of the American leagues: mostly from elsewhere in the Americas
// (Spanish-speaking South Americans carry Spanish names), plus Europeans in
// the MLS. [S: CIES, MLS player pool reports; P weights]
constexpr std::array<NationalityWeight, 12> AMERICAN_FOREIGN_POOL = {{
    {Language::ES, 10.0f},
    {Language::BR, 5.0f},
    {Language::MX, 4.0f},
    {Language::US, 3.0f},
    {Language::EN, 2.5f},
    {Language::FR, 1.5f},
    {Language::DE, 1.5f},
    {Language::PT, 1.0f},
    {Language::IT, 1.0f},
    {Language::JP, 1.0f},
    {Language::KR, 0.8f},
    {Language::IE, 0.7f},
}};

// Standard normal quantile (Abramowitz-Stegun 26.2.23, |error| < 4.5e-4).
double normalQuantile(double p)
{
  const double q = std::clamp(p, 1e-6, 1.0 - 1e-6);
  const double tail = q < 0.5 ? q : 1.0 - q;
  const double t = std::sqrt(-2.0 * std::log(tail));
  const double z =
      t - (2.515517 + 0.802853 * t + 0.010328 * t * t) /
              (1.0 + 1.432788 * t + 0.189269 * t * t + 0.001308 * t * t * t);
  return q < 0.5 ? -z : z;
}

std::uint8_t clampTrait(float value)
{
  return static_cast<std::uint8_t>(
      std::lround(std::clamp(value, 1.0f, 100.0f)));
}

PlayerTraits drawTraits(WorldRng& rng)
{
  PlayerTraits traits;
  traits.professionalism = clampTrait(rng.normal(55.0f, 16.0f));
  traits.ambition = clampTrait(rng.normal(50.0f, 18.0f));
  traits.temperament = clampTrait(rng.normal(55.0f, 15.0f));
  traits.loyalty = clampTrait(rng.normal(50.0f, 18.0f));
  // Skewed: most players are robust, a few are chronically injury prone.
  traits.injury_proneness = static_cast<std::uint8_t>(
      std::lround(std::clamp(rng.lognormal(40.0f, 0.4f), 5.0f, 100.0f)));
  return traits;
}

float potentialHeadroom(int age)
{
  // [P] Mean remaining growth by age: about +3 a season at 19-21 and +1.5
  // at 22-24, ending around 26-27 (peaks 25-27). [S: realism-research-2 4]
  if (age <= 20) return 1.7f * static_cast<float>(26 - age) + 3.0f;
  if (age <= 25) return 1.8f * static_cast<float>(26 - age) + 1.5f;
  return 0.4f * static_cast<float>(std::max(0, 29 - age));
}

float drawPotential(WorldRng& rng, float current, int age)
{
  const float headroom = potentialHeadroom(age);
  const float spread = age >= WorldTuning::Generation::VETERAN_AGE
                           ? 0.5f
                           : 0.45f * headroom + 1.5f;
  return std::min(WorldGeneration::maxPotential(age, current),
                  current + std::max(0.0f, rng.normal(headroom, spread)));
}

NameRegistry& generationRegistry()
{
  // Names of the world being generated; reset by generateClubProfiles().
  static NameRegistry registry;
  return registry;
}

template <std::size_t N>
Language drawForeign(WorldRng& rng,
                     const std::array<NationalityWeight, N>& pool,
                     Language domestic)
{
  std::array<float, N> weights{};
  for (std::size_t i = 0; i < N; ++i)
    weights[i] = pool[i].nationality == domestic ? 0.0f : pool[i].weight;
  return pool[rng.weightedIndex(weights)].nationality;
}

Language drawNationality(WorldRng& rng, const LeagueProfile& league)
{
  if (rng.chance(static_cast<double>(league.domestic_share)))
    return league.domestic_nationality;
  const bool americas = league.region == WorldRegion::NorthAmerica ||
                        league.region == WorldRegion::SouthAmerica;
  return americas ? drawForeign(rng, AMERICAN_FOREIGN_POOL,
                                league.domestic_nationality)
                  : drawForeign(rng, EUROPEAN_FOREIGN_POOL,
                                league.domestic_nationality);
}

std::map<std::string, float> drawStats(WorldRng& rng, PlayerRole role,
                                       float target_overall,
                                       const StatsConfig& stats_config)
{
  const RoleProfile& profile = roleProfile(role);
  std::map<std::string, float> stats;
  for (const std::string& stat_name : stats_config.possible_stats)
  {
    float offset = 0.0f;
    for (std::size_t i = 0; i < PROFILE_STATS.size(); ++i)
    {
      if (PROFILE_STATS[i] == stat_name) offset = profile.offsets[i];
    }
    stats[stat_name] = target_overall + offset + rng.normal(0.0f, 4.5f);
  }
  // Shift every attribute so the role overall hits the target exactly; a
  // few passes absorb the effect of clamping.
  for (int pass = 0; pass < 3; ++pass)
  {
    const auto delta = static_cast<float>(
        static_cast<double>(target_overall) -
        WorldGeneration::overallFor(role, stats, stats_config));
    for (auto& [name, value] : stats)
    {
      value = std::clamp(value + delta, static_cast<float>(MIN_STAT_VAL),
                         static_cast<float>(MAX_STAT_VAL) - 1.0f);
    }
  }
  return stats;
}

std::uint8_t drawHeight(WorldRng& rng, PlayerRole role)
{
  return static_cast<std::uint8_t>(std::lround(std::clamp(
      rng.normal(roleProfile(role).height_mean, 5.5f), 160.0f, 205.0f)));
}

Foot drawFoot(WorldRng& rng, PlayerRole role)
{
  return rng.chance(static_cast<double>(roleProfile(role).left_foot_share))
             ? Foot::Left
             : Foot::Right;
}

int drawAge(WorldRng& rng, SquadTier tier)
{
  using enum SquadTier;
  switch (tier)
  {
    case Starter:
      return std::clamp(static_cast<int>(std::lround(rng.normal(27.0f, 3.0f))),
                        21, 34);
    case Rotation:
      return std::clamp(static_cast<int>(std::lround(rng.normal(26.0f, 4.0f))),
                        19, 35);
    case Backup:
      return rng.chance(0.5)
                 ? std::clamp(
                       static_cast<int>(std::lround(rng.normal(21.0f, 1.5f))),
                       18, 23)
                 : std::clamp(
                       static_cast<int>(std::lround(rng.normal(31.0f, 2.0f))),
                       27, 36);
    case Prospect:
      return rng.uniformInt(17, 20);
  }
  return 25;
}

float tierOffset(SquadTier tier)
{
  using enum SquadTier;
  switch (tier)
  {
    case Starter:
      return 2.0f;
    case Rotation:
      return -3.0f;
    case Backup:
      return -8.0f;
    case Prospect:
      return -14.0f;
  }
  return 0.0f;
}

std::uint8_t drawContractYears(WorldRng& rng, int age)
{
  if (age < 18) return 3;  // Contracts of minors are capped at three years.
  if (age <= 21) return static_cast<std::uint8_t>(rng.uniformInt(3, 5));
  if (age <= 29) return static_cast<std::uint8_t>(rng.uniformInt(1, 5));
  return static_cast<std::uint8_t>(rng.uniformInt(1, 2));
}

std::uint8_t clampRating(float value, float lo)
{
  return static_cast<std::uint8_t>(std::lround(std::clamp(value, lo, 100.0f)));
}

Player makePlayer(WorldRng& rng, PlayerID player_id, TeamID team_id,
                  PlayerRole role, int age, float target_overall,
                  const LeagueProfile& league, const StatsConfig& stats_config,
                  NameRegistry& registry, SquadSurnames& squad)
{
  const Language nationality = drawNationality(rng, league);
  const auto [first, last] =
      WorldGeneration::drawName(rng, nationality, registry, squad);
  const std::uint8_t height = drawHeight(rng, role);
  const Foot foot = drawFoot(rng, role);
  auto stats = drawStats(rng, role, target_overall, stats_config);
  Player player(player_id, team_id, first, last, role, nationality, 0, 0,
                static_cast<std::uint8_t>(age), drawContractYears(rng, age),
                height, foot, std::move(stats));
  player.setTraits(drawTraits(rng));
  player.setPotential(drawPotential(rng, target_overall, age));
  return player;
}

std::uint32_t roundWage(double wage, double minimum)
{
  return static_cast<std::uint32_t>(
      std::lround(std::max(minimum, wage) / 50.0) * 50);
}
}  // namespace

const std::vector<std::string>& NamePool::firstNames(Language nationality) const
{
  const auto found = first_by_nationality.find(nationality);
  return found == first_by_nationality.end() ? first_names : found->second;
}

const std::vector<std::string>& NamePool::lastNames(Language nationality) const
{
  const auto found = last_by_nationality.find(nationality);
  return found == last_by_nationality.end() ? last_names : found->second;
}

const NamePool& NamePool::instance()
{
  static const NamePool pool = []
  {
    NamePool loaded;
    const auto read = [](const std::string& path)
    {
      std::ifstream file(path);
      if (!file.is_open())
        throw std::runtime_error(std::string("Could not open ") + path);
      return nlohmann::json::parse(file);
    };
    const auto byNationality =
        [](const nlohmann::json& json,
           std::unordered_map<Language, std::vector<std::string>>& out)
    {
      if (!json.contains("by_nationality")) return;
      for (const auto& [name, list] : json.at("by_nationality").items())
      {
        const auto language = stringToLanguage.find(name);
        auto names = list.get<std::vector<std::string>>();
        if (language != stringToLanguage.end() && !names.empty())
          out.try_emplace(language->second, std::move(names));
      }
    };
    const nlohmann::json first = read(AssetPaths::firstNames());
    const nlohmann::json last = read(AssetPaths::lastNames());
    loaded.first_names = first.at("names").get<std::vector<std::string>>();
    loaded.last_names = last.at("names").get<std::vector<std::string>>();
    if (loaded.first_names.empty() || loaded.last_names.empty())
      throw std::runtime_error("Name files must not be empty");
    byNationality(first, loaded.first_by_nationality);
    byNationality(last, loaded.last_by_nationality);
    if (first.contains("excluded_name_hashes"))
    {
      for (const auto& hash : first.at("excluded_name_hashes"))
        loaded.excluded_name_hashes.insert(
            std::stoull(hash.get<std::string>(), nullptr, 16));
    }
    return loaded;
  }();
  return pool;
}

std::string NamePool::normalizeName(std::string_view name)
{
  // Lower-case ASCII spelling of U+00C0..U+017F.
  static constexpr std::array<const char*, 192> LATIN_FOLD = {
      "a", "a", "a", "a",  "a",  "a",  "ae", "c", "e",  "e",  "e",  "e",  "i",
      "i", "i", "i", "d",  "n",  "o",  "o",  "o", "o",  "o",  "",   "o",  "u",
      "u", "u", "u", "y",  "th", "ss", "a",  "a", "a",  "a",  "a",  "a",  "ae",
      "c", "e", "e", "e",  "e",  "i",  "i",  "i", "i",  "d",  "n",  "o",  "o",
      "o", "o", "o", "",   "o",  "u",  "u",  "u", "u",  "y",  "th", "y",  "a",
      "a", "a", "a", "a",  "a",  "c",  "c",  "c", "c",  "c",  "c",  "c",  "c",
      "d", "d", "d", "d",  "e",  "e",  "e",  "e", "e",  "e",  "e",  "e",  "e",
      "e", "g", "g", "g",  "g",  "g",  "g",  "g", "g",  "h",  "h",  "h",  "h",
      "i", "i", "i", "i",  "i",  "i",  "i",  "i", "i",  "i",  "ij", "ij", "j",
      "j", "k", "k", "k",  "l",  "l",  "l",  "l", "l",  "l",  "l",  "l",  "l",
      "l", "n", "n", "n",  "n",  "n",  "n",  "n", "ng", "ng", "o",  "o",  "o",
      "o", "o", "o", "oe", "oe", "r",  "r",  "r", "r",  "r",  "r",  "s",  "s",
      "s", "s", "s", "s",  "s",  "s",  "t",  "t", "t",  "t",  "t",  "t",  "u",
      "u", "u", "u", "u",  "u",  "u",  "u",  "u", "u",  "u",  "u",  "w",  "w",
      "y", "y", "y", "z",  "z",  "z",  "z",  "z", "z",  "s",
  };
  std::string out;
  out.reserve(name.size());
  bool pending_space = false;
  const auto append = [&](std::string_view text)
  {
    if (pending_space && !out.empty()) out += ' ';
    pending_space = false;
    out += text;
  };
  for (std::size_t i = 0; i < name.size();)
  {
    const auto byte = static_cast<unsigned char>(name[i]);
    if (byte < 0x80)
    {
      if (byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r')
        pending_space = true;
      else
      {
        const char lower = static_cast<char>(std::tolower(byte));
        append(std::string_view(&lower, 1));
      }
      ++i;
      continue;
    }
    // Two-byte sequences cover the Latin supplements; anything else is
    // kept as it is.
    std::size_t length = 2;
    if (byte >= 0xF0)
      length = 4;
    else if (byte >= 0xE0)
      length = 3;
    if ((byte & 0xE0) == 0xC0 && i + 1 < name.size())
    {
      const char32_t code =
          (static_cast<char32_t>(byte & 0x1F) << 6) |
          static_cast<char32_t>(static_cast<unsigned char>(name[i + 1]) & 0x3F);
      if (code == 0xA0)
      {
        pending_space = true;
        i += 2;
        continue;
      }
      const char* folded = nullptr;
      if (code >= 0xC0 && code < 0xC0 + LATIN_FOLD.size())
        folded = LATIN_FOLD[code - 0xC0];
      else if (code == 0x218 || code == 0x219)
        folded = "s";
      else if (code == 0x21A || code == 0x21B)
        folded = "t";
      if (folded && *folded)
      {
        append(folded);
        i += 2;
        continue;
      }
    }
    append(name.substr(i, std::min(length, name.size() - i)));
    i += length;
  }
  return out;
}

std::uint64_t NamePool::nameHash(std::string_view name)
{
  constexpr std::string_view SALT = "football-management/names/v1:";
  std::uint64_t hash = 0xCBF29CE484222325ULL;
  const auto feed = [&hash](std::string_view text)
  {
    for (const char c : text)
    {
      hash ^= static_cast<unsigned char>(c);
      hash *= 0x100000001B3ULL;
    }
  };
  feed(SALT);
  feed(normalizeName(name));
  return hash;
}

bool NamePool::isExcluded(std::string_view full_name) const
{
  return !excluded_name_hashes.empty() &&
         excluded_name_hashes.contains(nameHash(full_name));
}

bool NameRegistry::isAvailable(const std::string& full_name) const
{
  return !names.contains(full_name) &&
         !NamePool::instance().isExcluded(full_name);
}

void NameRegistry::claim(const std::string& full_name)
{
  names.insert(full_name);
}

bool SquadSurnames::allows(const std::string& last_name) const
{
  const auto found = counts.find(last_name);
  return found == counts.end() || (found->second == 1 && !repeated);
}

void SquadSurnames::add(const std::string& last_name)
{
  if (++counts[last_name] > 1) repeated = true;
}

namespace WorldGeneration
{
std::pair<std::string, std::string> drawName(WorldRng& rng,
                                             Language nationality,
                                             NameRegistry& registry,
                                             SquadSurnames& squad)
{
  const NamePool& pool = NamePool::instance();
  const std::vector<std::string>& firsts = pool.firstNames(nationality);
  const std::vector<std::string>& lasts = pool.lastNames(nationality);
  const auto pick = [&rng](const std::vector<std::string>& names)
  {
    return names[static_cast<std::size_t>(
        rng.uniformInt(0, static_cast<int>(names.size()) - 1))];
  };
  // Iberian and Brazilian players commonly carry two surnames.
  const char* joiner =
      nationality == Language::ES || nationality == Language::PT ||
              nationality == Language::BR || nationality == Language::MX
          ? " "
          : "-";
  std::string first;
  std::string last;
  for (int attempt = 0; attempt < 2 * WorldTuning::Generation::NAME_ATTEMPTS;
       ++attempt)
  {
    first = pick(firsts);
    last = pick(lasts);
    if (attempt >= WorldTuning::Generation::NAME_ATTEMPTS)
    {
      const std::string second = pick(lasts);
      if (second != last) last += joiner + second;
    }
    if (registry.isAvailable(first + " " + last) && squad.allows(last)) break;
  }
  // A reserved name is never handed out, even when every attempt failed.
  while (pool.isExcluded(first + " " + last))
  {
    first = pick(firsts);
    last = pick(lasts) + joiner + pick(lasts);
  }
  registry.claim(first + " " + last);
  squad.add(last);
  return {std::move(first), std::move(last)};
}

float maxPotential(int age, float overall)
{
  if (age >= WorldTuning::Generation::VETERAN_AGE)
    return std::min(static_cast<float>(MAX_STAT_VAL) - 1.0f,
                    overall + WorldTuning::Generation::VETERAN_HEADROOM);
  return static_cast<float>(MAX_STAT_VAL) - 1.0f;
}

float teamLevel(std::uint8_t reputation)
{
  return WorldTuning::Generation::TEAM_LEVEL_BASE +
         WorldTuning::Generation::TEAM_LEVEL_SLOPE *
             static_cast<float>(reputation);
}

std::vector<float> levelOffsets(const LeagueShape& shape, std::size_t clubs)
{
  std::vector offsets(clubs, 0.0f);
  if (clubs < 2) return offsets;
  // Normal scores by rank, the elite pulled clear, then rescaled to the
  // league's spread around a zero mean.
  std::vector<double> scores(clubs);
  for (std::size_t rank = 0; rank < clubs; ++rank)
  {
    scores[rank] = normalQuantile(1.0 - (static_cast<double>(rank) + 0.5) /
                                            static_cast<double>(clubs));
    if (rank < shape.elite_clubs)
      scores[rank] += static_cast<double>(shape.elite_gap);
  }
  double mean = 0.0;
  for (const double score : scores) mean += score;
  mean /= static_cast<double>(clubs);
  double squares = 0.0;
  for (const double score : scores) squares += (score - mean) * (score - mean);
  const double sd = std::sqrt(squares / static_cast<double>(clubs));
  for (std::size_t rank = 0; rank < clubs; ++rank)
    offsets[rank] = static_cast<float>(static_cast<double>(shape.level_sd) *
                                       (scores[rank] - mean) / sd);
  return offsets;
}

float reputationCentre(const LeagueProfile& league, std::size_t clubs)
{
  using Generation = WorldTuning::Generation;
  const std::vector<float> offsets = levelOffsets(league.shape, clubs);
  const float top = offsets.empty() ? 0.0f : offsets.front();
  return std::min(static_cast<float>(league.reputation),
                  99.0f - Generation::REPUTATION_CAP_MARGIN -
                      Generation::REPUTATION_PER_LEVEL * top);
}

float clubLevel(const LeagueEconomy& economy, std::uint8_t reputation)
{
  using Generation = WorldTuning::Generation;
  const LeagueProfile& league = *economy.profile;
  const std::size_t clubs = std::max<std::size_t>(economy.clubs, 2);
  const float level =
      teamLevel(league.reputation) + league.shape.level_offset +
      (static_cast<float>(reputation) - reputationCentre(league, clubs)) /
          Generation::REPUTATION_PER_LEVEL;
  return std::min(level, Generation::MAX_CLUB_LEVEL);
}

double overallFor(PlayerRole role, const std::map<std::string, float>& stats,
                  const StatsConfig& stats_config)
{
  const auto focus =
      stats_config.role_focus.find(RoleUtils::getBroadCategory(role));
  if (focus == stats_config.role_focus.end()) return 0.0;
  double overall = 0.0;
  const std::size_t count =
      std::min(focus->second.stats.size(), focus->second.weights.size());
  for (std::size_t i = 0; i < count; ++i)
  {
    const auto stat = stats.find(focus->second.stats[i]);
    if (stat != stats.end())
      overall += static_cast<double>(stat->second) * focus->second.weights[i];
  }
  return overall;
}

void generateClubProfiles(std::unordered_map<TeamID, Team>& teams,
                          std::uint64_t world_seed, bool assign_opening_balance)
{
  using Generation = WorldTuning::Generation;
  if (assign_opening_balance) generationRegistry().clear();
  std::map<LeagueID, std::vector<TeamID>> by_league;
  for (const auto& [team_id, team] : teams)
  {
    if (team_id != FREE_AGENTS_TEAM_ID)
      by_league[team.getLeagueId()].push_back(team_id);
  }

  for (auto& [league_id, team_ids] : by_league)
  {
    std::ranges::sort(team_ids);
    WorldRng rng =
        WorldRng::stream(world_seed, RngDomain::Generation, league_id);
    rng.shuffle(std::span<TeamID>(team_ids));
    const LeagueProfile& league = leagueProfile(league_id);

    // The league's shape ranks the (shuffled) clubs; reputation, and with it
    // revenue, follows the first-team level they are generated at.
    const std::vector<float> offsets =
        levelOffsets(league.shape, team_ids.size());
    const float centre = reputationCentre(league, team_ids.size());
    std::vector<std::uint8_t> reputations;
    reputations.reserve(team_ids.size());
    for (std::size_t i = 0; i < team_ids.size(); ++i)
    {
      ClubProfile profile;
      profile.reputation =
          clampRating(centre + Generation::REPUTATION_PER_LEVEL * offsets[i] +
                          rng.normal(0.0f, Generation::CLUB_REPUTATION_NOISE),
                      15.0f);
      profile.training_facilities = clampRating(
          static_cast<float>(profile.reputation) + rng.normal(0.0f, 8.0f),
          10.0f);
      profile.youth_facilities = clampRating(
          static_cast<float>(profile.reputation) + rng.normal(0.0f, 10.0f),
          10.0f);
      teams.at(team_ids[i]).setProfile(profile);
      reputations.push_back(profile.reputation);
    }

    const LeagueEconomy economy = makeLeagueEconomy(league_id, reputations);
    for (const TeamID team_id : team_ids)
    {
      Team& team = teams.at(team_id);
      ClubProfile profile = team.getProfile();
      const double demand =
          std::min(1.0, ClubEconomy::baseDemand(economy, profile.reputation));
      const double capacity =
          static_cast<double>(league.average_attendance) *
          std::exp(0.045 * (static_cast<double>(profile.reputation) -
                            static_cast<double>(economy.mean_reputation))) /
          demand * static_cast<double>(rng.uniform(0.9f, 1.15f));
      profile.stadium_capacity = static_cast<std::uint32_t>(
          std::lround(std::clamp(capacity, 2'500.0, 99'000.0) / 100.0) * 100);
      profile.ticket_price = static_cast<std::uint32_t>(
          std::lround(ClubEconomy::fairTicketPrice(economy, profile)));
      team.setProfile(profile);
      if (!assign_opening_balance) continue;

      const double income =
          ClubEconomy::expectedIncome(economy, profile.reputation);
      const double opening = income * static_cast<double>(rng.uniform(
                                          Generation::OPENING_BALANCE_MIN,
                                          Generation::OPENING_BALANCE_MAX));
      team.getFinances() = Finances(
          static_cast<std::int64_t>(std::llround(opening / 1000.0)) * 1000);
    }
  }
}

std::vector<Player> generateSquad(const Team& team,
                                  const LeagueEconomy& economy,
                                  std::size_t existing_players,
                                  std::int64_t existing_weekly_wages,
                                  PlayerID& next_player_id,
                                  std::uint64_t world_seed,
                                  const StatsConfig& stats_config)
{
  std::vector<Player> players;
  if (existing_players >= SQUAD_TEMPLATE.size()) return players;
  WorldRng rng = WorldRng::stream(world_seed, RngDomain::Generation, 0x5C0AD,
                                  team.getId());
  const float level = clubLevel(economy, team.getReputation());

  std::vector<double> wage_indices;
  SquadSurnames surnames;
  players.reserve(SQUAD_TEMPLATE.size() - existing_players);
  wage_indices.reserve(SQUAD_TEMPLATE.size() - existing_players);
  for (std::size_t slot = existing_players; slot < SQUAD_TEMPLATE.size();
       ++slot)
  {
    const SquadSlot& squad_slot = SQUAD_TEMPLATE[slot];
    const int age = drawAge(rng, squad_slot.tier);
    const float target =
        std::clamp(level + tierOffset(squad_slot.tier) + rng.normal(0.0f, 2.5f),
                   20.0f, 95.0f);
    players.push_back(makePlayer(rng, next_player_id++, team.getId(),
                                 squad_slot.role, age, target, *economy.profile,
                                 stats_config, generationRegistry(), surnames));
    wage_indices.push_back(ClubEconomy::wageIndex(target, age) *
                           static_cast<double>(rng.uniform(0.85f, 1.15f)));
  }

  // Scale wages so the payroll matches the league's player wage share of the
  // club's expected income (+-10%).
  const double income =
      ClubEconomy::expectedIncome(economy, team.getReputation());
  const double target_payroll =
      income * ClubEconomy::playerWageShare(economy) / 52.0 *
          static_cast<double>(rng.uniform(0.9f, 1.1f)) -
      static_cast<double>(existing_weekly_wages);
  double index_total = 0.0;
  for (const double index : wage_indices) index_total += index;
  const double scale =
      index_total > 0.0 ? std::max(0.0, target_payroll) / index_total : 0.0;
  for (std::size_t i = 0; i < players.size(); ++i)
    players[i].setWage(roundWage(wage_indices[i] * scale, 300.0));
  return players;
}

void initializeHiddenAttributes(Player& player, WorldRng& rng,
                                const StatsConfig& stats_config)
{
  // A predefined player keeps his name; generated players must not repeat
  // it.
  generationRegistry().claim(player.getName());
  player.setTraits(drawTraits(rng));
  player.setPotential(
      drawPotential(rng, static_cast<float>(player.getOverall(stats_config)),
                    player.getAge()));
}

void applyOpeningBudgets(Team& team, const LeagueEconomy& economy,
                         std::int64_t weekly_payroll)
{
  Finances& finances = team.getFinances();
  const double income =
      ClubEconomy::expectedIncome(economy, team.getReputation());
  finances.setWageBudget(
      ClubEconomy::seasonWageBudget(economy, income, weekly_payroll));
  finances.setTransferBudget(
      ClubEconomy::seasonTransferBudget(finances.getBalance(), income));
}
}  // namespace WorldGeneration
