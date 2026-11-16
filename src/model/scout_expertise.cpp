// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/scout_expertise.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "model/world_rng.h"
#include "model/world_tuning.h"

namespace
{
constexpr std::uint64_t EXPERTISE_KEY = 0x5C0E4BE27ULL;

struct SecondLanguage
{
  SpokenLanguage language;
  double probability;
};

/** Plausible second languages by first language. [P] */
std::span<const SecondLanguage> secondLanguages(SpokenLanguage native)
{
  using enum SpokenLanguage;
  static constexpr std::array<SecondLanguage, 3> ENGLISH = {
      {{Spanish, 0.25}, {French, 0.15}, {German, 0.10}}};
  static constexpr std::array<SecondLanguage, 3> ITALIAN = {
      {{English, 0.55}, {Spanish, 0.35}, {French, 0.20}}};
  static constexpr std::array<SecondLanguage, 4> SPANISH = {
      {{English, 0.50}, {Portuguese, 0.25}, {Italian, 0.20}, {French, 0.15}}};
  static constexpr std::array<SecondLanguage, 4> FRENCH = {
      {{English, 0.55}, {Spanish, 0.30}, {Italian, 0.15}, {German, 0.10}}};
  static constexpr std::array<SecondLanguage, 3> GERMAN = {
      {{English, 0.70}, {Dutch, 0.15}, {French, 0.15}}};
  static constexpr std::array<SecondLanguage, 4> PORTUGUESE = {
      {{Spanish, 0.60}, {English, 0.45}, {French, 0.15}, {Italian, 0.10}}};
  static constexpr std::array<SecondLanguage, 3> DUTCH = {
      {{English, 0.85}, {German, 0.50}, {French, 0.20}}};
  static constexpr std::array<SecondLanguage, 3> RUSSIAN = {
      {{English, 0.35}, {Ukrainian, 0.30}, {German, 0.10}}};
  static constexpr std::array<SecondLanguage, 3> OTHER = {
      {{English, 0.60}, {German, 0.15}, {Spanish, 0.10}}};
  switch (native)
  {
    case English:
      return ENGLISH;
    case Italian:
      return ITALIAN;
    case Spanish:
      return SPANISH;
    case French:
      return FRENCH;
    case German:
      return GERMAN;
    case Portuguese:
      return PORTUGUESE;
    case Dutch:
      return DUTCH;
    case Russian:
      return RUSSIAN;
    default:
      return OTHER;
  }
}

/** Adds @p delta to the factor of the same kind and subject. */
void accumulate(std::vector<EffectFactor>& factors, EffectFactorKind kind,
                std::uint32_t subject, float delta)
{
  if (delta == 0.0f) return;
  const auto found = std::ranges::find_if(
      factors, [&](const EffectFactor& factor)
      { return factor.kind == kind && factor.subject == subject; });
  if (found != factors.end())
    found->delta += delta;
  else
    factors.push_back({kind, delta, subject});
}

float experienceShare(std::uint32_t days)
{
  return std::min(1.0f, static_cast<float>(days) /
                            ScoutExpertiseModel::FULL_EXPERIENCE_DAYS);
}

bool isHome(Language nationality, LeagueID country)
{
  return countryNationality(country) == nationality &&
         countryContinent(country) == homeContinent(nationality);
}
}  // namespace

const char* spokenLanguageKey(SpokenLanguage language)
{
  static constexpr std::array<const char*,
                              static_cast<std::size_t>(
                                  std::to_underlying(SpokenLanguage::COUNT))>
      KEYS = {
          "SCOUT_LANG_ENGLISH",  "SCOUT_LANG_ITALIAN",  "SCOUT_LANG_SPANISH",
          "SCOUT_LANG_FRENCH",   "SCOUT_LANG_GERMAN",   "SCOUT_LANG_PORTUGUESE",
          "SCOUT_LANG_DUTCH",    "SCOUT_LANG_RUSSIAN",  "SCOUT_LANG_SWEDISH",
          "SCOUT_LANG_POLISH",   "SCOUT_LANG_GREEK",    "SCOUT_LANG_TURKISH",
          "SCOUT_LANG_ARABIC",   "SCOUT_LANG_JAPANESE", "SCOUT_LANG_CHINESE",
          "SCOUT_LANG_KOREAN",   "SCOUT_LANG_ROMANIAN", "SCOUT_LANG_CROATIAN",
          "SCOUT_LANG_DANISH",   "SCOUT_LANG_FINNISH",  "SCOUT_LANG_NORWEGIAN",
          "SCOUT_LANG_SLOVAK",   "SCOUT_LANG_CZECH",    "SCOUT_LANG_HUNGARIAN",
          "SCOUT_LANG_UKRAINIAN"};
  const auto index = static_cast<std::size_t>(std::to_underlying(language));
  return index < KEYS.size() ? KEYS[index] : KEYS[0];
}

const char* continentKey(Continent continent)
{
  using enum Continent;
  switch (continent)
  {
    case Europe:
      return "SCOUT_CONTINENT_EUROPE";
    case NorthAmerica:
      return "SCOUT_CONTINENT_NORTH_AMERICA";
    case SouthAmerica:
      return "SCOUT_CONTINENT_SOUTH_AMERICA";
    case Asia:
    case COUNT:
      break;
  }
  return "SCOUT_CONTINENT_ASIA";
}

SpokenLanguage nativeLanguage(Language nationality)
{
  using enum Language;
  using enum SpokenLanguage;
  switch (nationality)
  {
    case EN:
    case IE:
    case US:
      return English;
    case IT:
      return Italian;
    case ES:
    case MX:
      return Spanish;
    case FR:
    case BE:
      return French;
    case DE:
    case CH:
      return German;
    case PT:
    case BR:
      return Portuguese;
    case NL:
      return Dutch;
    case RU:
      return Russian;
    case SE:
      return Swedish;
    case PL:
      return Polish;
    case GR:
      return Greek;
    case TR:
      return Turkish;
    case AR:
      return Arabic;
    case JP:
      return Japanese;
    case CN:
      return Chinese;
    case KR:
      return Korean;
    case RO:
      return Romanian;
    case HR:
      return Croatian;
    case DK:
      return Danish;
    case FI:
      return Finnish;
    case NO:
      return Norwegian;
    case SK:
      return Slovak;
    case CZ:
      return Czech;
    case HU:
      return Hungarian;
    case UA:
      return Ukrainian;
  }
  return English;
}

Continent homeContinent(Language nationality)
{
  using enum Continent;
  using enum Language;
  switch (nationality)
  {
    case US:
    case MX:
      return NorthAmerica;
    case BR:
      return SouthAmerica;
    case CN:
    case JP:
    case KR:
    case AR:
      return Asia;
    default:
      return Europe;
  }
}

Language countryNationality(LeagueID country)
{
  return leagueProfile(country).domestic_nationality;
}

SpokenLanguage countryLanguage(LeagueID country)
{
  return nativeLanguage(countryNationality(country));
}

Continent countryContinent(LeagueID country)
{
  // leagues.json has no country field and the South American league of id
  // 10 shares the Spanish nationality code, so its continent is explicit.
  if (constexpr LeagueID PAMPAS_LEAGUE = 10; country == PAMPAS_LEAGUE)
    return Continent::SouthAmerica;
  return homeContinent(countryNationality(country));
}

ScoutExpertise ScoutExpertiseModel::generate(
    Language nationality, std::uint64_t world_seed, std::uint32_t scout_id,
    std::span<const LeagueID> countries)
{
  WorldRng rng = WorldRng::stream(world_seed, RngDomain::Scouting,
                                  EXPERTISE_KEY, scout_id);
  ScoutExpertise expertise;
  expertise.nationality = nationality;
  const SpokenLanguage native = nativeLanguage(nationality);
  expertise.languages = languageBit(native);
  for (const SecondLanguage& second : secondLanguages(native))
  {
    if (second.language != native && rng.chance(second.probability))
      expertise.languages |= languageBit(second.language);
  }

  // Most scouts played or scouted at home before joining. [P]
  std::vector<LeagueID> abroad;
  for (const LeagueID country : countries)
  {
    if (!isHome(nationality, country))
      abroad.push_back(country);
    else if (!expertise.league_days.contains(country) && rng.chance(0.8))
      expertise.league_days[country] =
          static_cast<std::uint16_t>(rng.uniformInt(365, 1800));
  }

  // Up to two spells abroad, where he speaks the language or close to home.
  const int spells = rng.uniformInt(0, 2);
  const Continent continent = homeContinent(nationality);
  for (int spell = 0; spell < spells && !abroad.empty(); ++spell)
  {
    std::vector<float> weights;
    weights.reserve(abroad.size());
    for (const LeagueID country : abroad)
    {
      float weight = 1.0f;
      if (speaks(expertise.languages, countryLanguage(country))) weight *= 3.0f;
      if (countryContinent(country) == continent) weight *= 2.0f;
      weights.push_back(weight);
    }
    const std::size_t pick = rng.weightedIndex(weights);
    const LeagueID country = abroad[pick];
    abroad.erase(abroad.begin() + static_cast<std::ptrdiff_t>(pick));
    const auto days = static_cast<std::uint16_t>(rng.uniformInt(180, 1100));
    expertise.league_days[country] = days;
    if (days >= LANGUAGE_LEARNED_DAYS)
      expertise.languages |= languageBit(countryLanguage(country));
  }
  return expertise;
}

ScoutEffectiveness ScoutExpertiseModel::compute(
    const ScoutExpertise& expertise, std::uint8_t judging_ability,
    std::span<const TargetCountry> countries, LeagueID league,
    std::uint32_t league_days)
{
  using enum EffectFactorKind;
  ScoutEffectiveness result;
  auto& factors = result.factors;
  accumulate(factors, Judging, judging_ability,
             JUDGING_PER_POINT * (static_cast<float>(judging_ability) - 50.0f));
  if (league != 0)
    accumulate(factors, KnowsLeague, league,
               KNOWS_LEAGUE * experienceShare(league_days));
  const Continent continent = homeContinent(expertise.nationality);
  for (const TargetCountry& target : countries)
  {
    if (isHome(expertise.nationality, target.country))
    {
      accumulate(factors, HomeCountry, target.country,
                 HOME_COUNTRY * target.weight);
      continue;
    }
    accumulate(
        factors, KnowsCountry, target.country,
        KNOWS_COUNTRY * target.weight * experienceShare(target.country_days));
    if (const SpokenLanguage language = countryLanguage(target.country);
        speaks(expertise.languages, language))
      accumulate(factors, SpeaksLanguage,
                 static_cast<std::uint32_t>(std::to_underlying(language)),
                 SPEAKS_LANGUAGE * target.weight);
    else
      accumulate(factors, LanguageBarrier,
                 static_cast<std::uint32_t>(std::to_underlying(language)),
                 LANGUAGE_BARRIER * target.weight);
    const Continent target_continent = countryContinent(target.country);
    if (target_continent != continent)
      accumulate(
          factors, FarFromHome,
          static_cast<std::uint32_t>(std::to_underlying(target_continent)),
          FAR_FROM_HOME * target.weight);
  }
  std::erase_if(factors, [](const EffectFactor& factor)
                { return std::abs(factor.delta) < 0.005f; });
  std::ranges::sort(factors, [](const EffectFactor& a, const EffectFactor& b)
                    { return std::abs(a.delta) > std::abs(b.delta); });
  float total = 1.0f;
  for (const EffectFactor& factor : factors) total += factor.delta;
  result.multiplier = std::clamp(total, MIN_MULTIPLIER, MAX_MULTIPLIER);
  return result;
}
