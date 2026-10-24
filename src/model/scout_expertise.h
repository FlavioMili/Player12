// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <utility>
#include <vector>

#include "global/languages.h"
#include "global/types.h"

/**
 * @enum SpokenLanguage
 * @brief Languages a scout can speak (bit positions are persisted).
 */
enum class SpokenLanguage : std::uint8_t
{
  English = 0,
  Italian,
  Spanish,
  French,
  German,
  Portuguese,
  Dutch,
  Russian,
  Swedish,
  Polish,
  Greek,
  Turkish,
  Arabic,
  Japanese,
  Chinese,
  Korean,
  Romanian,
  Croatian,
  Danish,
  Finnish,
  Norwegian,
  Slovak,
  Czech,
  Hungarian,
  Ukrainian,
  COUNT
};

/** Bit set of SpokenLanguage values. */
using LanguageSet = std::uint32_t;

constexpr LanguageSet languageBit(SpokenLanguage language)
{
  return LanguageSet{1} << static_cast<unsigned>(std::to_underlying(language));
}

constexpr bool speaks(LanguageSet languages, SpokenLanguage language)
{
  return (languages & languageBit(language)) != 0;
}

/** Language key of a spoken language ("SCOUT_LANG_ENGLISH"...). */
const char* spokenLanguageKey(SpokenLanguage language);

/** @brief Continents (values are shown, not persisted). */
enum class Continent : std::uint8_t
{
  Europe = 0,
  NorthAmerica,
  SouthAmerica,
  Asia,
  COUNT
};

/** Language key of a continent ("SCOUT_CONTINENT_EUROPE"...). */
const char* continentKey(Continent continent);

/** First language of a nationality. */
SpokenLanguage nativeLanguage(Language nationality);

/** Continent a nationality comes from. */
Continent homeContinent(Language nationality);

/**
 * Country data of a league country, identified by its top league id (the
 * root of the league tree, like ScoutTargetKind::Country).
 */
Language countryNationality(LeagueID country);
SpokenLanguage countryLanguage(LeagueID country);
Continent countryContinent(LeagueID country);

/**
 * @struct ScoutExpertise
 * @brief Where a scout comes from and what he knows.
 *
 * league_days counts days spent working in a league: a generated background
 * (the leagues he played or scouted in before joining) plus every day of
 * assignments for the club. Keys are league ids; country experience is the
 * sum over the leagues of that country.
 */
struct ScoutExpertise
{
  Language nationality = Language::EN;
  LanguageSet languages = 0;
  std::map<LeagueID, std::uint16_t> league_days;
};

/** @brief Kind of a line of the effectiveness breakdown. */
enum class EffectFactorKind : std::uint8_t
{
  Judging,         /*!< subject: judging ability (1-100). */
  HomeCountry,     /*!< subject: country (top league id). */
  KnowsLeague,     /*!< subject: league id. */
  KnowsCountry,    /*!< subject: country (top league id). */
  SpeaksLanguage,  /*!< subject: SpokenLanguage. */
  LanguageBarrier, /*!< subject: SpokenLanguage. */
  FarFromHome      /*!< subject: Continent of the target. */
};

/** @brief One line of the breakdown, e.g. "Speaks Portuguese +10%". */
struct EffectFactor
{
  EffectFactorKind kind = EffectFactorKind::Judging;
  float delta = 0.0f; /*!< Contribution to the multiplier (0.1 = +10%). */
  std::uint32_t subject = 0;
};

/**
 * @struct ScoutEffectiveness
 * @brief How well a scout will do on an assignment.
 *
 * The multiplier scales knowledge gained per observation, the players
 * watched per day and the precision of his judgement.
 */
struct ScoutEffectiveness
{
  float multiplier = 1.0f;
  std::vector<EffectFactor> factors; /*!< Largest effect first. */
};

/** @brief A country covered by an assignment and the scout's history there. */
struct TargetCountry
{
  LeagueID country = 0;
  float weight = 1.0f;            /*!< Share of the assignment (sums to 1). */
  std::uint32_t country_days = 0; /*!< Scout's days in its leagues. */
};

/**
 * @namespace ScoutExpertiseModel
 * @brief Generation of scout backgrounds and the effectiveness model. [P]
 */
namespace ScoutExpertiseModel
{
constexpr float MIN_MULTIPLIER = 0.6f;
constexpr float MAX_MULTIPLIER = 1.5f;
constexpr float JUDGING_PER_POINT = 0.004f; /*!< +-20% across 1-100. */
constexpr float HOME_COUNTRY = 0.20f;
constexpr float KNOWS_LEAGUE = 0.20f;  /*!< After a year there. */
constexpr float KNOWS_COUNTRY = 0.10f; /*!< After a year there. */
constexpr float SPEAKS_LANGUAGE = 0.10f;
constexpr float LANGUAGE_BARRIER = -0.15f;
constexpr float FAR_FROM_HOME = -0.10f; /*!< Another continent. */
constexpr float FULL_EXPERIENCE_DAYS = 365.0f;
constexpr std::uint16_t LANGUAGE_LEARNED_DAYS = 540; /*!< Picked up abroad. */
constexpr std::uint16_t MAX_LEAGUE_DAYS = 9'999;

/**
 * Deterministic background of a scout: languages from his nationality
 * (native plus plausible second languages), usually a spell in his home
 * country's top league and up to two leagues abroad, preferring countries
 * whose language he speaks or that are on his continent.
 * @param countries Top league ids of the world's countries.
 */
ScoutExpertise generate(Language nationality, std::uint64_t world_seed,
                        std::uint32_t scout_id,
                        std::span<const LeagueID> countries);

/**
 * Effectiveness = 1 + judging + home + league/country experience +
 * language +- distance, clamped to [MIN_MULTIPLIER, MAX_MULTIPLIER].
 * Country terms are weighted by the countries' shares of the assignment.
 * @param league Specific league of the assignment (0 = none) and
 * @p league_days the scout's days there.
 */
ScoutEffectiveness compute(const ScoutExpertise& expertise,
                           std::uint8_t judging_ability,
                           std::span<const TargetCountry> countries,
                           LeagueID league, std::uint32_t league_days);
}  // namespace ScoutExpertiseModel
