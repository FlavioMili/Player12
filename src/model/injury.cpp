// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/injury.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>

#include "model/world_rng.h"
#include "model/world_tuning.h"

namespace
{
struct InjuryProfile
{
  InjuryType type;
  const char* key;
  float weight;  // Relative frequency in percent.
  float median_days;
  float p10_days;
  float p90_days;
  bool muscle;
};

// Weights are percent of all injuries and sum to 100. [S] Layoffs (median,
// P10-P90) of Ekstrand et al. 2020; hamstrings at 24% (Ekstrand 2023). [P]
// The remaining "other" share is spread over contusions, back spasms,
// fractures and dislocations, and a few diagnoses were scaled up so the mix
// sums to 100 (minor injuries ~45%, ECIS reports 42%).
constexpr std::array<InjuryProfile, static_cast<std::size_t>(
                                        std::to_underlying(InjuryType::COUNT)) -
                                        1>
    PROFILES = {{
        {InjuryType::HamstringStrain, "INJURY_HAMSTRING_STRAIN", 20.0f, 13.0f,
         4.0f, 36.0f, true},
        {InjuryType::HamstringTightness, "INJURY_HAMSTRING_TIGHTNESS", 4.0f,
         5.0f, 2.0f, 11.0f, true},
        {InjuryType::GroinStrain, "INJURY_GROIN_STRAIN", 10.0f, 8.0f, 2.0f,
         27.0f, true},
        {InjuryType::AnkleSprain, "INJURY_ANKLE_SPRAIN", 8.0f, 8.0f, 2.0f,
         32.0f, false},
        {InjuryType::QuadricepsStrain, "INJURY_QUADRICEPS_STRAIN", 4.6f, 13.0f,
         4.0f, 41.0f, true},
        {InjuryType::CalfStrain, "INJURY_CALF_STRAIN", 4.1f, 13.0f, 4.0f, 35.0f,
         true},
        {InjuryType::KneeMcl, "INJURY_KNEE_MCL", 4.8f, 16.0f, 3.0f, 56.0f,
         false},
        {InjuryType::ThighContusion, "INJURY_THIGH_CONTUSION", 6.0f, 4.0f, 1.0f,
         12.0f, false},
        {InjuryType::AchillesTendinopathy, "INJURY_ACHILLES", 1.9f, 6.0f, 2.0f,
         42.0f, false},
        {InjuryType::Concussion, "INJURY_CONCUSSION", 2.0f, 5.0f, 2.0f, 14.0f,
         false},
        {InjuryType::KneeCartilage, "INJURY_KNEE_CARTILAGE", 1.1f, 22.0f, 4.0f,
         134.0f, false},
        {InjuryType::KneeMeniscus, "INJURY_KNEE_MENISCUS", 0.6f, 36.0f, 8.0f,
         128.0f, false},
        {InjuryType::KneeAcl, "INJURY_KNEE_ACL", 0.9f, 205.0f, 129.0f, 292.0f,
         false},
        {InjuryType::BackSpasm, "INJURY_BACK_SPASM", 8.0f, 6.0f, 2.0f, 20.0f,
         false},
        {InjuryType::FootContusion, "INJURY_FOOT_CONTUSION", 22.0f, 3.0f, 1.0f,
         9.0f, false},
        {InjuryType::BrokenMetatarsal, "INJURY_BROKEN_METATARSAL", 1.0f, 60.0f,
         40.0f, 100.0f, false},
        {InjuryType::ShoulderDislocation, "INJURY_SHOULDER_DISLOCATION", 1.0f,
         25.0f, 10.0f, 50.0f, false},
    }};

const InjuryProfile* findProfile(InjuryType type)
{
  const auto found = std::ranges::find(PROFILES, type, &InjuryProfile::type);
  return found == PROFILES.end() ? nullptr : std::to_address(found);
}

// ACL match/training rate ratio is ~20. [S]
constexpr float ACL_TRAINING_FACTOR = 1.0f / 20.0f;
// P10-P90 of a normal spans 2 * 1.2816 standard deviations.
constexpr float P10_P90_Z_SPAN = 2.563f;
// Days an aggravation adds at least to the injury that was carried. [P]
constexpr float AGGRAVATION_MIN_SETBACK = 3.0f;
}  // namespace

namespace InjuryModel
{
const char* nameKey(InjuryType type)
{
  const InjuryProfile* profile = findProfile(type);
  return profile ? profile->key : "INJURY_NONE";
}

bool isMuscle(InjuryType type)
{
  const InjuryProfile* profile = findProfile(type);
  return profile && profile->muscle;
}

InjurySeverity severity(std::uint16_t days)
{
  using enum InjurySeverity;
  if (days <= 7) return Minor;
  if (days <= 28) return Moderate;
  return Major;
}

Injury draw(WorldRng& rng, InjuryContext context, InjuryType previous,
            bool recent_previous)
{
  std::array<float, PROFILES.size()> weights{};
  for (std::size_t i = 0; i < PROFILES.size(); ++i)
  {
    float weight = PROFILES[i].weight;
    if (context == InjuryContext::Training &&
        PROFILES[i].type == InjuryType::KneeAcl)
      weight *= ACL_TRAINING_FACTOR;
    if (previous != InjuryType::None && PROFILES[i].type == previous)
    {
      weight *= static_cast<float>(
          recent_previous ? WorldTuning::Fitness::RECENT_REINJURY_MULTIPLIER
                          : WorldTuning::Fitness::PRIOR_REINJURY_MULTIPLIER);
    }
    weights[i] = weight;
  }

  const InjuryProfile& profile = PROFILES[rng.weightedIndex(weights)];
  const float sigma =
      (std::log(profile.p90_days) - std::log(profile.p10_days)) /
      P10_P90_Z_SPAN;
  float days = rng.lognormal(profile.median_days, sigma);
  if (profile.type == previous && recent_previous)
    days *= WorldTuning::Fitness::REINJURY_DURATION_MULTIPLIER;
  days = std::clamp(days, 1.0f, 400.0f);
  return {profile.type, static_cast<std::uint16_t>(std::lround(days))};
}

float aggravationMultiplier(InjurySeverity severity)
{
  using enum InjurySeverity;
  switch (severity)
  {
    case Moderate:
      return 3.0f;
    case Major:
      return 4.0f;
    case Minor:
      break;
  }
  return 2.0f;
}

double aggravationChance(std::uint16_t days_left, int minutes)
{
  if (days_left == 0 || minutes <= 0) return 0.0;
  const double hazard =
      WorldTuning::Fitness::MATCH_INJURY_RATE_PER_HOUR *
      static_cast<double>(minutes) / 60.0 *
      static_cast<double>(aggravationMultiplier(severity(days_left)));
  return 1.0 - std::exp(-hazard);
}

Injury aggravate(WorldRng& rng, InjuryType type, std::uint16_t days_left)
{
  const InjuryProfile* profile = findProfile(type);
  if (!profile) return {type, days_left};
  const float sigma =
      (std::log(profile->p90_days) - std::log(profile->p10_days)) /
      P10_P90_Z_SPAN;
  const float drawn = rng.lognormal(profile->median_days, sigma) *
                      WorldTuning::Fitness::REINJURY_DURATION_MULTIPLIER;
  // Never shorter than what was left, and always a real setback.
  const float days = std::clamp(
      std::max(drawn, static_cast<float>(days_left) + AGGRAVATION_MIN_SETBACK),
      1.0f, 400.0f);
  return {type, static_cast<std::uint16_t>(std::lround(days))};
}
}  // namespace InjuryModel
