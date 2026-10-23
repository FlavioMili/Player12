// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/training.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

#include "database/gamedata.h"
#include "global/global.h"
#include "model/player.h"
#include "model/role_utils.h"
#include "model/staff.h"
#include "model/team.h"

namespace
{
using Slot = TrainingSlot;
using S = SessionType;
using I = TrainingIntensity;

constexpr std::size_t STAT_COUNT = 9;
using StatMix = std::array<float, STAT_COUNT>;

// Attribute order shared by every mix.
constexpr std::array<const char*, STAT_COUNT> STAT_NAMES = {
    "Pace",        "Shooting", "Passing", "Dribbling",  "Defending",
    "Physicality", "Stamina",  "Vision",  "Goalkeeping"};
constexpr std::array<bool, STAT_COUNT> PHYSICAL_STAT = {
    true, false, false, false, false, true, true, false, false};

/** Load of a session at normal intensity (endurance day = 1.0). [P] */
constexpr std::array<float, SESSION_TYPE_COUNT> SESSION_LOAD = {
    0.0f, 0.25f, 1.2f, 0.8f, 0.85f, 0.85f, 0.7f, 0.45f};
/** Session intensity multipliers. [P] */
constexpr std::array<float, 3> SLOT_INTENSITY = {0.7f, 1.0f, 1.3f};
/** Squad-wide intensity multipliers. [P] */
constexpr std::array<float, 3> SQUAD_INTENSITY = {0.85f, 1.0f, 1.15f};

// Attributes worked on by each session type (weights per unit load). [P]
constexpr std::array<StatMix, SESSION_TYPE_COUNT> SESSION_MIX = {{
    {},                                       // Rest
    {},                                       // Recovery
    {1.0f, 0, 0, 0, 0, 1.0f, 1.2f, 0, 0},     // Fitness
    {0, 0.5f, 1.0f, 1.0f, 0, 0, 0, 0, 1.0f},  // Technical
    {0.3f, 1.2f, 0, 0.6f, 0, 0, 0, 0.3f, 0},  // Attacking
    {0, 0, 0, 0, 1.2f, 0.6f, 0, 0.3f, 0.6f},  // Defending
    {0, 0, 0.5f, 0, 0.5f, 0, 0, 1.0f, 0},     // Tactical
    {0, 0.2f, 0, 0, 0.2f, 0, 0, 0.3f, 0},     // MatchPrep
}};

constexpr float CONDITION_DRAIN_PER_LOAD = 2.5f;
constexpr float TRAINING_CONDITION_FLOOR = 35.0f;
constexpr float TRAINING_SHARPNESS_CAP = 75.0f;
constexpr float ACUTE_LAMBDA = 2.0f / 8.0f;     // 7-day EWMA
constexpr float CHRONIC_LAMBDA = 2.0f / 29.0f;  // 28-day EWMA
constexpr float SCHEDULE_SHARE = 0.30f;
constexpr float FOCUS_SHARE = 0.35f;
constexpr float FOCUS_EXTRA_LOAD = 1.10f;
constexpr float NON_ROLE_CEILING = 5.0f;
constexpr float FAMILIARITY_DAILY = 0.01f;
constexpr float FAMILIARITY_MATCH = 0.03f;
constexpr float FAMILIARITY_HOLIDAY_DECAY = 0.997f;

std::size_t idx(auto value) { return static_cast<std::size_t>(value); }

float loadOf(Slot slot)
{
  return SESSION_LOAD[idx(slot.session)] * SLOT_INTENSITY[idx(slot.intensity)];
}

float statOf(const Player& player, const char* name, float fallback)
{
  const auto found = player.getStats().find(name);
  return found == player.getStats().end() ? fallback : found->second;
}

const RoleFocus* roleFocus(const StatsConfig& config,
                           const std::string& category)
{
  const auto found = config.role_focus.find(category);
  return found == config.role_focus.end() ? nullptr : &found->second;
}

StatMix roleWeights(const StatsConfig& config, const std::string& category)
{
  StatMix weights{};
  const RoleFocus* focus = roleFocus(config, category);
  if (!focus) return weights;
  const std::size_t count =
      std::min(focus->stats.size(), focus->weights.size());
  for (std::size_t i = 0; i < count; ++i)
  {
    for (std::size_t stat = 0; stat < STAT_COUNT; ++stat)
    {
      if (focus->stats[i] == STAT_NAMES[stat])
        weights[stat] = static_cast<float>(focus->weights[i]);
    }
  }
  return weights;
}

const TeamTrainingPlan& defaultPlan()
{
  static const TeamTrainingPlan PLAN = []
  {
    TeamTrainingPlan plan;
    plan.slots = TrainingModel::presetMicrocycle(TrainingPreset::Balanced);
    plan.last_week_load = TrainingModel::balancedWeekLoad();
    plan.week_load = plan.last_week_load;
    return plan;
  }();
  return PLAN;
}

/** Coaching quality (0-1) relevant to a player under a plan. */
float coachingFor(const TeamTrainingPlan& plan, const StaffEffects& effects,
                  const Player& player)
{
  using enum S;
  if (player.getRole() == PlayerRole::GK)
    return 0.7f * effects.coaching_goalkeeping +
           0.3f * effects.coaching_tactical;
  float weighted = 0.0f;
  float total = 0.0f;
  for (const Slot& slot : plan.slots)
  {
    const float load = loadOf(slot);
    float quality = 0.0f;
    switch (slot.session)
    {
      case Fitness:
        quality = effects.coaching_fitness;
        break;
      case Technical:
        quality = 0.5f * (effects.coaching_attacking + effects.coaching_youth);
        break;
      case Attacking:
        quality = effects.coaching_attacking;
        break;
      case Defending:
        quality = effects.coaching_defending;
        break;
      case Tactical:
      case MatchPrep:
        quality = effects.coaching_tactical;
        break;
      case Rest:
      case Recovery:
      case COUNT:
        continue;
    }
    weighted += load * quality;
    total += load;
  }
  float quality = total > 0.0f ? weighted / total : effects.training_quality;
  // Academy players work with the youth coaches half of the time.
  if (player.getAge() <= 19)
    quality = 0.5f * quality + 0.5f * effects.coaching_youth;
  return quality;
}

std::string joinNames(const std::vector<std::string>& names, std::size_t limit)
{
  std::string joined;
  for (std::size_t i = 0; i < names.size() && i < limit; ++i)
  {
    if (i > 0) joined += ", ";
    joined += names[i];
  }
  if (names.size() > limit) joined += ", ...";
  return joined;
}
}  // namespace

// ---------------------------------------------------------------------------
// TrainingRegistry
// ---------------------------------------------------------------------------

TeamTrainingPlan& TrainingRegistry::plan(TeamID team_id)
{
  if (const auto found = team_plans.find(team_id); found != team_plans.end())
    return found->second;
  return team_plans.try_emplace(team_id, defaultPlan()).first->second;
}

const TeamTrainingPlan* TrainingRegistry::findPlan(TeamID team_id) const
{
  const auto found = team_plans.find(team_id);
  return found == team_plans.end() ? nullptr : &found->second;
}

PlayerTrainingState& TrainingRegistry::player(PlayerID player_id)
{
  if (const auto found = player_states.find(player_id);
      found != player_states.end())
    return found->second;
  PlayerTrainingState state;
  state.acute = TrainingModel::typicalDailyLoad();
  state.chronic = state.acute;
  return player_states.try_emplace(player_id, state).first->second;
}

const PlayerTrainingState* TrainingRegistry::findPlayer(
    PlayerID player_id) const
{
  const auto found = player_states.find(player_id);
  return found == player_states.end() ? nullptr : &found->second;
}

void TrainingRegistry::restore(
    std::unordered_map<TeamID, TeamTrainingPlan> plans,
    std::unordered_map<PlayerID, PlayerTrainingState> players)
{
  team_plans = std::move(plans);
  player_states = std::move(players);
}

void TrainingRegistry::clear()
{
  team_plans.clear();
  player_states.clear();
}

// ---------------------------------------------------------------------------
// TrainingModel
// ---------------------------------------------------------------------------

namespace TrainingModel
{
Microcycle presetMicrocycle(TrainingPreset preset)
{
  // Days: MD+1, MD+2, MD-4, MD-3, MD-2, MD-1 (rules-regulations 5.1).
  switch (preset)
  {
    case TrainingPreset::Fitness:
      return {Slot{S::Recovery, I::Normal}, Slot{S::Rest, I::Normal},
              Slot{S::Fitness, I::High},    Slot{S::Fitness, I::Normal},
              Slot{S::Tactical, I::Normal}, Slot{S::MatchPrep, I::Low}};
    case TrainingPreset::Attacking:
      return {Slot{S::Recovery, I::Low},     Slot{S::Rest, I::Normal},
              Slot{S::Fitness, I::Normal},   Slot{S::Attacking, I::High},
              Slot{S::Technical, I::Normal}, Slot{S::MatchPrep, I::Low}};
    case TrainingPreset::Defending:
      return {Slot{S::Recovery, I::Low},    Slot{S::Rest, I::Normal},
              Slot{S::Fitness, I::Normal},  Slot{S::Defending, I::High},
              Slot{S::Tactical, I::Normal}, Slot{S::MatchPrep, I::Low}};
    case TrainingPreset::YouthDevelopment:
      return {Slot{S::Recovery, I::Low},     Slot{S::Rest, I::Normal},
              Slot{S::Technical, I::High},   Slot{S::Fitness, I::Normal},
              Slot{S::Technical, I::Normal}, Slot{S::MatchPrep, I::Low}};
    case TrainingPreset::Light:
      return {Slot{S::Recovery, I::Low},    Slot{S::Rest, I::Normal},
              Slot{S::Technical, I::Low},   Slot{S::Tactical, I::Low},
              Slot{S::Recovery, I::Normal}, Slot{S::MatchPrep, I::Low}};
    case TrainingPreset::Balanced:
    case TrainingPreset::Custom:
    case TrainingPreset::COUNT:
      break;
  }
  return {Slot{S::Recovery, I::Low},    Slot{S::Rest, I::Normal},
          Slot{S::Fitness, I::High},    Slot{S::Technical, I::Normal},
          Slot{S::Tactical, I::Normal}, Slot{S::MatchPrep, I::Low}};
}

float sessionLoad(TrainingSlot slot, TrainingIntensity squad_intensity)
{
  return loadOf(slot) * SQUAD_INTENSITY[idx(squad_intensity)];
}

float balancedWeekLoad()
{
  float total = 0.0f;
  for (const Slot& slot : presetMicrocycle(TrainingPreset::Balanced))
    total += loadOf(slot);
  return total;
}

float typicalDailyLoad() { return (balancedWeekLoad() + MATCH_LOAD) / 7.0f; }

float sessionInjuryExposure(TrainingSlot slot,
                            TrainingIntensity squad_intensity)
{
  const float mean_day =
      balancedWeekLoad() / static_cast<float>(MICROCYCLE_DAYS);
  return sessionLoad(slot, squad_intensity) / mean_day;
}

std::optional<MicrocycleDay> dayFor(int days_since_last, int days_to_next,
                                    std::int32_t ordinal)
{
  using enum MicrocycleDay;
  if (days_to_next == 0) return std::nullopt;
  if (days_to_next == 1) return MdMinus1;
  if (days_since_last == 1) return MdPlus1;
  if (days_to_next >= 2 && days_to_next <= 4)
    return static_cast<MicrocycleDay>(6 - days_to_next);
  if (days_since_last == 2) return MdPlus2;
  // Long gaps (breaks, pre-season): four-day blocks of rest, speed,
  // endurance and strength.
  const int horizon =
      days_to_next >= 5 ? days_to_next : 5 + ((ordinal % 4) + 4) % 4;
  switch ((horizon - 5) % 4)
  {
    case 0:
      return MicrocycleDay::MdPlus2;
    case 1:
      return MicrocycleDay::MdMinus2;
    case 2:
      return MicrocycleDay::MdMinus3;
    default:
      return MicrocycleDay::MdMinus4;
  }
}

bool isCongested(int days_since_last, int days_to_next)
{
  return days_since_last >= 1 && days_to_next >= 1 &&
         days_since_last + days_to_next <= 4;
}

TrainingDay scheduleDay(const TeamTrainingPlan& plan, int days_since_last,
                        int days_to_next, std::int32_t ordinal)
{
  TrainingDay result;
  const auto day = dayFor(days_since_last, days_to_next, ordinal);
  if (!day)
  {
    result.match_day = true;
    result.slot = Slot{S::Rest, I::Low};
    return result;
  }
  result.day = *day;
  result.slot = plan.slots[idx(*day)];
  result.congested = isCongested(days_since_last, days_to_next);
  // Two matches in a week: recovery and activation only, no loading days.
  if (result.congested && plan.auto_congestion)
  {
    const Slot adjusted = *day == MicrocycleDay::MdMinus1
                              ? Slot{S::MatchPrep, I::Low}
                              : Slot{S::Recovery, I::Low};
    result.adjusted = adjusted != result.slot;
    result.slot = adjusted;
  }
  return result;
}

float conditionDrain(float load, float stamina)
{
  return load * CONDITION_DRAIN_PER_LOAD *
         (1.15f - 0.3f * std::clamp(stamina, 0.0f, 100.0f) / 100.0f);
}

float recoveryShare(SessionType session)
{
  if (session == S::Recovery) return 0.08f;
  if (session == S::Rest) return 0.05f;
  return 0.0f;
}

float sharpnessGain(TrainingSlot slot)
{
  using enum S;
  float gain = 0.0f;
  switch (slot.session)
  {
    case MatchPrep:
      gain = 0.45f;
      break;
    case Tactical:
      gain = 0.35f;
      break;
    case Technical:
    case Attacking:
    case Defending:
      gain = 0.25f;
      break;
    case Fitness:
      gain = 0.15f;
      break;
    case Rest:
    case Recovery:
    case COUNT:
      break;
  }
  return gain * SLOT_INTENSITY[idx(slot.intensity)];
}

float workloadRatio(const PlayerTrainingState& state)
{
  if (state.chronic <= 0.0f) return 1.0f;
  return state.acute / std::max(state.chronic, 0.2f);
}

float workloadRiskMultiplier(const PlayerTrainingState& state)
{
  const float ratio = workloadRatio(state);
  if (ratio > 1.3f) return std::min(1.5f, 1.0f + 0.5f * (ratio - 1.3f));
  // A high, stable chronic load protects (training-injury prevention
  // paradox), kept deliberately small. [S direction, P size]
  if (state.chronic > 1.1f * typicalDailyLoad() && ratio >= 0.8f) return 0.9f;
  return 1.0f;
}

WorkloadRisk workloadRisk(const PlayerTrainingState& state, float condition)
{
  using enum WorkloadRisk;
  const float ratio = workloadRatio(state);
  if (ratio > 1.5f || condition < 55.0f) return High;
  if (ratio > 1.3f || condition < 70.0f) return Moderate;
  return Low;
}

void rollDay(PlayerTrainingState& state)
{
  state.acute += ACUTE_LAMBDA * (state.pending - state.acute);
  state.chronic += CHRONIC_LAMBDA * (state.pending - state.chronic);
  state.pending = 0.0f;
}

float developmentMultiplier(const TeamTrainingPlan& plan, int age,
                            float coaching_quality)
{
  // More training volume develops a little faster (bounded). [P]
  const float volume = plan.week_load / balancedWeekLoad();
  float multiplier = std::clamp(0.8f + 0.2f * volume, 0.8f, 1.12f);
  // Coaching spread kept narrower than the manager effect. [P, world-realism
  // 8: no robust estimate for skills coaches]
  multiplier *= 0.9f + 0.2f * std::clamp(coaching_quality, 0.0f, 1.0f);
  if (plan.preset == TrainingPreset::YouthDevelopment)
  {
    if (age <= 21)
      multiplier *= 1.10f;
    else if (age >= 24)
      multiplier *= 0.95f;
  }
  return multiplier;
}

std::array<float, 9> scheduleAttributeMix(const TeamTrainingPlan& plan)
{
  StatMix mix{};
  for (const Slot& slot : plan.slots)
  {
    const float load = loadOf(slot);
    for (std::size_t stat = 0; stat < STAT_COUNT; ++stat)
      mix[stat] += load * SESSION_MIX[idx(slot.session)][stat];
  }
  if (const float total = std::accumulate(mix.begin(), mix.end(), 0.0f);
      total > 0.0f)
  {
    for (float& value : mix) value /= total;
  }
  return mix;
}

std::array<float, 9> focusAttributeMix(TrainingFocus focus,
                                       const StatsConfig& config)
{
  using enum TrainingFocus;
  switch (focus)
  {
    case Physical:
      return {1, 0, 0, 0, 0, 1, 1, 0, 0};
    case Attacking:
      return {0, 1, 0, 1, 0, 0, 0, 0, 0};
    case Playmaking:
      return {0, 0, 1, 0, 0, 0, 0, 1, 0};
    case Defending:
      return {0, 0, 0, 0, 1, 1, 0, 0, 0};
    case Goalkeeping:
      return {0, 0, 0, 0, 0, 0, 0, 0, 1};
    case RetrainStriker:
      return roleWeights(config, RoleUtils::getBroadCategory(PlayerRole::ST));
    case RetrainMidfielder:
      return roleWeights(config, RoleUtils::getBroadCategory(PlayerRole::CM));
    case RetrainDefender:
      return roleWeights(config, RoleUtils::getBroadCategory(PlayerRole::CB));
    case None:
    case COUNT:
      break;
  }
  return {};
}

const std::array<const char*, 9>& attributeNames() { return STAT_NAMES; }

float applyDirectedGrowth(Player& player, float growth,
                          const TeamTrainingPlan& plan, TrainingFocus focus,
                          const StatsConfig& config)
{
  if (growth <= 0.0f) return growth;
  const bool physical_growth = player.getAge() <= 25;
  const StatMix role =
      roleWeights(config, RoleUtils::getBroadCategory(player.getRole()));
  auto stats = player.getStats();
  float remainder = growth;
  const auto eligible = [&](std::size_t stat)
  {
    return (physical_growth || !PHYSICAL_STAT[stat]) &&
           stats.contains(STAT_NAMES[stat]);
  };
  const auto add = [&](std::size_t stat, float amount)
  {
    float& value = stats[STAT_NAMES[stat]];
    value = std::clamp(value + amount, static_cast<float>(MIN_STAT_VAL),
                       static_cast<float>(MAX_STAT_VAL));
  };

  // Schedule share: the drilled attributes of the role, overall-preserving.
  const StatMix schedule = scheduleAttributeMix(plan);
  float schedule_weight = 0.0f;
  for (std::size_t stat = 0; stat < STAT_COUNT; ++stat)
  {
    if (eligible(stat)) schedule_weight += schedule[stat] * role[stat];
  }
  if (schedule_weight > 0.0f)
  {
    const float share = growth * SCHEDULE_SHARE;
    for (std::size_t stat = 0; stat < STAT_COUNT; ++stat)
    {
      if (!eligible(stat) || role[stat] <= 0.0f) continue;
      add(stat,
          std::min(3.0f * share, share * schedule[stat] / schedule_weight));
    }
    remainder -= share;
  }

  // Individual focus: any attribute; outside the role it stops at the
  // player's potential + 5 and does not raise the role rating.
  if (focus != TrainingFocus::None)
  {
    const StatMix wanted = focusAttributeMix(focus, config);
    const float ceiling = player.getPotential() + NON_ROLE_CEILING;
    StatMix weights{};
    float total = 0.0f;
    for (std::size_t stat = 0; stat < STAT_COUNT; ++stat)
    {
      if (wanted[stat] <= 0.0f || !eligible(stat)) continue;
      if (role[stat] <= 0.0f && stats[STAT_NAMES[stat]] >= ceiling) continue;
      weights[stat] = wanted[stat];
      total += wanted[stat];
    }
    if (total > 0.0f)
    {
      const float share = growth * FOCUS_SHARE;
      for (std::size_t stat = 0; stat < STAT_COUNT; ++stat)
      {
        if (weights[stat] > 0.0f) add(stat, share * weights[stat] / total);
      }
      remainder -= share;
    }
  }
  player.setStats(stats);
  return remainder;
}

TacticSnapshot snapshotOf(const Team& team)
{
  TacticSnapshot snapshot;
  const StrategySliders sliders = team.getStrategy().getSliders();
  snapshot.sliders = {sliders.pressing, sliders.riskTaking,
                      sliders.offensiveBias, sliders.widthUsage,
                      sliders.compactness};
  for (const auto& positioned : team.getLineup().getOutfieldPlayers())
  {
    if (snapshot.outfield >= TacticSnapshot::MAX_POSITIONS) break;
    snapshot.positions[snapshot.outfield++] = positioned.position;
  }
  // Insertion sort: at most ten positions, independent of lineup order.
  const auto before = [](const Vector2F& a, const Vector2F& b)
  { return a.x != b.x ? a.x < b.x : a.y < b.y; };
  for (std::size_t i = 1; i < snapshot.outfield; ++i)
  {
    const Vector2F value = snapshot.positions[i];
    std::size_t j = i;
    for (; j > 0 && before(value, snapshot.positions[j - 1]); --j)
      snapshot.positions[j] = snapshot.positions[j - 1];
    snapshot.positions[j] = value;
  }
  snapshot.valid = true;
  return snapshot;
}

float familiarityLoss(const TacticSnapshot& before, const TacticSnapshot& after)
{
  if (!before.valid || !after.valid) return 0.0f;
  float slider_change = 0.0f;
  for (std::size_t i = 0; i < before.sliders.size(); ++i)
    slider_change += std::abs(after.sliders[i] - before.sliders[i]);
  // Players whose position has no counterpart in the old shape.
  int moved = std::abs(static_cast<int>(after.outfield) -
                       static_cast<int>(before.outfield));
  for (std::uint8_t i = 0; i < after.outfield; ++i)
  {
    float nearest = 1.0f;
    for (std::uint8_t j = 0; j < before.outfield; ++j)
    {
      nearest = std::min(
          nearest, std::hypot(after.positions[i].x - before.positions[j].x,
                              after.positions[i].y - before.positions[j].y));
    }
    if (nearest > 0.06f) ++moved;
  }
  // A full swing of one slider costs 15%, each re-positioned player 5%. [P]
  const float loss = 0.15f * slider_change + 0.05f * static_cast<float>(moved);
  return loss < 0.005f ? 0.0f : std::min(0.6f, loss);
}

float familiarityGain(TrainingSlot slot)
{
  using enum S;
  float gain = 0.0f;
  switch (slot.session)
  {
    case Tactical:
      gain = 0.06f;
      break;
    case MatchPrep:
      gain = 0.04f;
      break;
    case Attacking:
    case Defending:
      gain = 0.015f;
      break;
    case Technical:
      gain = 0.01f;
      break;
    case Rest:
    case Recovery:
    case Fitness:
    case COUNT:
      break;
  }
  return gain * SLOT_INTENSITY[idx(slot.intensity)];
}

std::vector<TrainingAdvice> advise(const AdviceInputs& inputs)
{
  std::vector<TrainingAdvice> advice;
  const auto push = [&advice](AdviceSeverity severity, const char* key,
                              std::vector<std::string> args)
  { advice.emplace_back(severity, key, std::move(args)); };

  if (!inputs.spike_players.empty())
  {
    push(AdviceSeverity::Warning, "ADVICE_WORKLOAD_SPIKE",
         {std::to_string(inputs.spike_players.size()),
          joinNames(inputs.spike_players, 4)});
  }
  if (inputs.average_condition < 82.0f || inputs.tired_players >= 5)
  {
    push(AdviceSeverity::Warning,
         inputs.preset == TrainingPreset::Light ? "ADVICE_FATIGUED_ROTATE"
                                                : "ADVICE_FATIGUED_LIGHT",
         {std::to_string(std::lround(inputs.average_condition)),
          std::to_string(inputs.tired_players)});
  }
  if (inputs.familiarity < 45.0f)
  {
    push(AdviceSeverity::Warning, "ADVICE_FAMILIARITY_LOW",
         {std::to_string(std::lround(inputs.familiarity))});
  }
  else if (inputs.familiarity < 65.0f)
  {
    push(AdviceSeverity::Suggestion, "ADVICE_FAMILIARITY_BUILD",
         {std::to_string(std::lround(inputs.familiarity))});
  }
  if (inputs.matches_next_week >= 2)
  {
    push(inputs.auto_congestion ? AdviceSeverity::Info
                                : AdviceSeverity::Suggestion,
         inputs.auto_congestion ? "ADVICE_CONGESTION_HANDLED"
                                : "ADVICE_CONGESTION_AUTO",
         {std::to_string(inputs.matches_next_week)});
  }
  if (inputs.injured_players >= 4)
  {
    push(AdviceSeverity::Suggestion,
         inputs.intensity == TrainingIntensity::High
             ? "ADVICE_INJURIES_INTENSITY"
             : "ADVICE_INJURIES_MEDICAL",
         {std::to_string(inputs.injured_players)});
  }
  if (inputs.young_prospects >= 5 &&
      inputs.preset != TrainingPreset::YouthDevelopment)
  {
    push(AdviceSeverity::Suggestion, "ADVICE_YOUTH_PRESET",
         {std::to_string(inputs.young_prospects)});
  }
  if (inputs.preset == TrainingPreset::Light &&
      inputs.average_condition > 92.0f && inputs.matches_next_week <= 1)
  {
    push(AdviceSeverity::Suggestion, "ADVICE_LIGHT_TOO_EASY", {});
  }
  for (std::size_t i = 0; i < inputs.missing_roles.size() && i < 2; ++i)
  {
    push(AdviceSeverity::Suggestion, "ADVICE_MISSING_STAFF",
         {"@" + inputs.missing_roles[i]});
  }
  if (advice.empty()) push(AdviceSeverity::Info, "ADVICE_ALL_GOOD", {});
  std::ranges::stable_sort(advice,
                           [](const TrainingAdvice& a, const TrainingAdvice& b)
                           { return a.severity > b.severity; });
  return advice;
}

const char* sessionKey(SessionType session)
{
  using enum S;
  switch (session)
  {
    case Rest:
      return "TRAINING_SESSION_REST";
    case Recovery:
      return "TRAINING_SESSION_RECOVERY";
    case Fitness:
      return "TRAINING_SESSION_FITNESS";
    case Technical:
      return "TRAINING_SESSION_TECHNICAL";
    case Attacking:
      return "TRAINING_SESSION_ATTACKING";
    case Defending:
      return "TRAINING_SESSION_DEFENDING";
    case Tactical:
      return "TRAINING_SESSION_TACTICAL";
    case MatchPrep:
    case COUNT:
      break;
  }
  return "TRAINING_SESSION_MATCH_PREP";
}

const char* intensityKey(TrainingIntensity intensity)
{
  using enum TrainingIntensity;
  switch (intensity)
  {
    case Low:
      return "TRAINING_INTENSITY_LOW";
    case High:
      return "TRAINING_INTENSITY_HIGH";
    case Normal:
    case COUNT:
      break;
  }
  return "TRAINING_INTENSITY_NORMAL";
}

const char* presetKey(TrainingPreset preset)
{
  using enum TrainingPreset;
  switch (preset)
  {
    case Fitness:
      return "TRAINING_PRESET_FITNESS";
    case Attacking:
      return "TRAINING_PRESET_ATTACKING";
    case Defending:
      return "TRAINING_PRESET_DEFENDING";
    case YouthDevelopment:
      return "TRAINING_PRESET_YOUTH";
    case Light:
      return "TRAINING_PRESET_LIGHT";
    case Custom:
      return "TRAINING_PRESET_CUSTOM";
    case Balanced:
    case COUNT:
      break;
  }
  return "TRAINING_PRESET_BALANCED";
}

const char* focusKey(TrainingFocus focus)
{
  using enum TrainingFocus;
  switch (focus)
  {
    case Physical:
      return "TRAINING_FOCUS_PHYSICAL";
    case Attacking:
      return "TRAINING_FOCUS_ATTACKING";
    case Playmaking:
      return "TRAINING_FOCUS_PLAYMAKING";
    case Defending:
      return "TRAINING_FOCUS_DEFENDING";
    case Goalkeeping:
      return "TRAINING_FOCUS_GOALKEEPING";
    case RetrainStriker:
      return "TRAINING_FOCUS_RETRAIN_ST";
    case RetrainMidfielder:
      return "TRAINING_FOCUS_RETRAIN_MID";
    case RetrainDefender:
      return "TRAINING_FOCUS_RETRAIN_DEF";
    case None:
    case COUNT:
      break;
  }
  return "TRAINING_FOCUS_NONE";
}

const char* dayKey(MicrocycleDay day)
{
  using enum MicrocycleDay;
  switch (day)
  {
    case MdPlus1:
      return "TRAINING_DAY_MD_PLUS_1";
    case MdPlus2:
      return "TRAINING_DAY_MD_PLUS_2";
    case MdMinus4:
      return "TRAINING_DAY_MD_MINUS_4";
    case MdMinus3:
      return "TRAINING_DAY_MD_MINUS_3";
    case MdMinus2:
      return "TRAINING_DAY_MD_MINUS_2";
    case MdMinus1:
    case COUNT:
      break;
  }
  return "TRAINING_DAY_MD_MINUS_1";
}

const char* riskKey(WorkloadRisk risk)
{
  using enum WorkloadRisk;
  switch (risk)
  {
    case Moderate:
      return "TRAINING_RISK_MODERATE";
    case High:
      return "TRAINING_RISK_HIGH";
    case Low:
      break;
  }
  return "TRAINING_RISK_LOW";
}
}  // namespace TrainingModel

// ---------------------------------------------------------------------------
// TrainingSystem
// ---------------------------------------------------------------------------

namespace TrainingSystem
{
void planDay(GameData& gamedata, std::int32_t ordinal,
             const FixtureOutlook* outlook, bool training_period)
{
  TrainingRegistry& registry = gamedata.getTraining();
  const StaffRoster& staff = gamedata.getStaff();
  for (const auto& [team_id, team] : gamedata.getTeams())
  {
    if (team_id == FREE_AGENTS_TEAM_ID) continue;
    TeamTrainingPlan& plan = registry.plan(team_id);
    const TacticSnapshot snapshot = TrainingModel::snapshotOf(team);
    plan.familiarity *=
        1.0f - TrainingModel::familiarityLoss(plan.tactic, snapshot);
    plan.tactic = snapshot;

    if (!training_period)
    {
      plan.today = TrainingDay{};
      plan.today.slot = TrainingSlot{S::Rest, I::Low};
      plan.familiarity *= FAMILIARITY_HOLIDAY_DECAY;
      continue;
    }
    const int since =
        plan.last_match_day > 0 ? ordinal - plan.last_match_day : -1;
    int next = -1;
    if (outlook)
    {
      if (const auto found = outlook->find(team_id); found != outlook->end())
        next = found->second;
    }
    else if (since >= 1 && since <= 6)
    {
      next = 7 - since;  // No calendar: assume a weekly rhythm.
    }
    plan.today = TrainingModel::scheduleDay(plan, since, next, ordinal);
    float gain = FAMILIARITY_DAILY;
    if (!plan.today.match_day)
    {
      plan.week_load +=
          TrainingModel::sessionLoad(plan.today.slot, plan.intensity);
      gain += TrainingModel::familiarityGain(plan.today.slot);
    }
    plan.familiarity = std::clamp(
        plan.familiarity + (100.0f - plan.familiarity) * gain *
                               staff.effects(team_id).familiarity_rate,
        0.0f, 100.0f);
  }
}

void rollWorkloads(GameData& gamedata)
{
  TrainingRegistry& registry = gamedata.getTraining();
  for (const auto& [player_id, player] : gamedata.getPlayers())
    TrainingModel::rollDay(registry.player(player_id));
}

double trainPlayer(GameData& gamedata, Player& player, std::int32_t ordinal)
{
  using enum S;
  const TeamID team_id = player.getTeamId();
  if (team_id == FREE_AGENTS_TEAM_ID) return 0.0;
  TrainingRegistry& registry = gamedata.getTraining();
  const TeamTrainingPlan* plan = registry.findPlan(team_id);
  if (!plan || plan->today.match_day) return 0.0;

  PlayerDynamics& dynamics = player.mutableDynamics();
  TrainingSlot slot = plan->today.slot;
  if (dynamics.last_match_day > 0 &&
      dynamics.last_match_day != plan->last_match_day)
  {
    const std::int32_t since_played = ordinal - dynamics.last_match_day;
    if (since_played == 1)
    {
      // Played outside the club's rhythm: recovery, then a day off.
      slot = TrainingSlot{Recovery, I::Low};
    }
    else if (since_played == 2)
    {
      slot = TrainingSlot{Rest, I::Low};
    }
    else if (plan->today.day == MicrocycleDay::MdPlus1 &&
             !plan->today.adjusted && plan->last_match_day > 0)
    {
      // MD+1: starters recover, the others do a compensation session.
      slot = TrainingSlot{Fitness, I::Normal};
    }
  }

  PlayerTrainingState& state = registry.player(player.getId());
  const float focus_factor =
      state.focus != TrainingFocus::None && slot.session != S::Rest
          ? FOCUS_EXTRA_LOAD
          : 1.0f;
  const float load =
      TrainingModel::sessionLoad(slot, plan->intensity) * focus_factor;
  state.pending += load;

  if (dynamics.condition > TRAINING_CONDITION_FLOOR)
  {
    dynamics.condition = std::max(
        TRAINING_CONDITION_FLOOR,
        dynamics.condition - TrainingModel::conditionDrain(
                                 load, statOf(player, "Stamina", 60.0f)));
  }
  dynamics.condition += (100.0f - dynamics.condition) *
                        TrainingModel::recoveryShare(slot.session);
  const float sharpness_cap =
      std::max(dynamics.sharpness, TRAINING_SHARPNESS_CAP);
  dynamics.sharpness = std::min(
      sharpness_cap, dynamics.sharpness + TrainingModel::sharpnessGain(slot));

  return static_cast<double>(
      TrainingModel::sessionInjuryExposure(slot, plan->intensity) *
      focus_factor * TrainingModel::workloadRiskMultiplier(state) *
      gamedata.getStaff().effects(team_id).injury_prevention);
}

double recordMatchLoad(GameData& gamedata, const Player& player,
                       std::uint8_t minutes)
{
  PlayerTrainingState& state = gamedata.getTraining().player(player.getId());
  state.pending +=
      TrainingModel::MATCH_LOAD * static_cast<float>(minutes) / 90.0f;
  float multiplier = TrainingModel::workloadRiskMultiplier(state);
  if (player.getTeamId() != FREE_AGENTS_TEAM_ID)
    multiplier *=
        gamedata.getStaff().effects(player.getTeamId()).injury_prevention;
  return static_cast<double>(multiplier);
}

void recordTeamMatch(GameData& gamedata, TeamID team_id, std::int32_t ordinal)
{
  if (team_id == FREE_AGENTS_TEAM_ID) return;
  TeamTrainingPlan& plan = gamedata.getTraining().plan(team_id);
  if (plan.last_match_day == ordinal) return;
  plan.last_match_day = ordinal;
  plan.familiarity = std::min(
      100.0f, plan.familiarity +
                  (100.0f - plan.familiarity) * FAMILIARITY_MATCH *
                      gamedata.getStaff().effects(team_id).familiarity_rate);
}

float trainingQuality(const GameData& gamedata, const Player& player)
{
  const TeamID team_id = player.getTeamId();
  const auto team = gamedata.getTeam(team_id);
  // Free agents train alone. [P]
  if (team_id == FREE_AGENTS_TEAM_ID || !team) return 0.7f;
  const float facilities =
      0.8f +
      0.4f * static_cast<float>(team->get().getProfile().training_facilities) /
          100.0f;
  const TeamTrainingPlan* found = gamedata.getTraining().findPlan(team_id);
  const TeamTrainingPlan& plan = found ? *found : defaultPlan();
  const float coaching =
      coachingFor(plan, gamedata.getStaff().effects(team_id), player);
  return std::clamp(facilities * TrainingModel::developmentMultiplier(
                                     plan, player.getAge(), coaching),
                    0.7f, 1.3f);
}

float directGrowth(GameData& gamedata, Player& player, float growth)
{
  if (player.getTeamId() == FREE_AGENTS_TEAM_ID) return growth;
  TrainingRegistry& registry = gamedata.getTraining();
  const TeamTrainingPlan* found = registry.findPlan(player.getTeamId());
  return TrainingModel::applyDirectedGrowth(
      player, growth, found ? *found : defaultPlan(),
      registry.player(player.getId()).focus, gamedata.getStatsConfig());
}

void recordWeeklyGrowth(GameData& gamedata, PlayerID player_id, float delta)
{
  PlayerTrainingState& state = gamedata.getTraining().player(player_id);
  state.trend = 0.75f * state.trend + 0.25f * delta;
}

void endWeek(GameData& gamedata)
{
  for (const auto& [team_id, team] : gamedata.getTeams())
  {
    if (team_id == FREE_AGENTS_TEAM_ID) continue;
    TeamTrainingPlan& plan = gamedata.getTraining().plan(team_id);
    plan.last_week_load = plan.week_load;
    plan.week_load = 0.0f;
  }
}

AdviceInputs adviceInputs(const GameData& gamedata, TeamID team_id,
                          int matches_next_week)
{
  AdviceInputs inputs;
  inputs.matches_next_week = matches_next_week;
  const TrainingRegistry& registry = gamedata.getTraining();
  if (const TeamTrainingPlan* plan = registry.findPlan(team_id))
  {
    inputs.preset = plan->preset;
    inputs.intensity = plan->intensity;
    inputs.auto_congestion = plan->auto_congestion;
    inputs.familiarity = plan->familiarity;
  }
  const StatsConfig& config = gamedata.getStatsConfig();
  float condition_total = 0.0f;
  int fit = 0;
  for (const auto& player_ref : gamedata.getPlayersForTeam(team_id))
  {
    const Player& player = player_ref.get();
    const PlayerDynamics& dynamics = player.getDynamics();
    if (dynamics.injury_days > 0)
    {
      ++inputs.injured_players;
      continue;
    }
    condition_total += dynamics.condition;
    ++fit;
    if (dynamics.condition < 75.0f) ++inputs.tired_players;
    if (const PlayerTrainingState* state = registry.findPlayer(player.getId());
        state && TrainingModel::workloadRatio(*state) > 1.5f)
      inputs.spike_players.push_back(player.getName());
    if (player.getAge() <= 21 &&
        player.getPotential() - static_cast<float>(player.getOverall(config)) >=
            8.0f)
      ++inputs.young_prospects;
  }
  if (fit > 0)
    inputs.average_condition = condition_total / static_cast<float>(fit);
  std::ranges::sort(inputs.spike_players);

  const StaffRoster& staff = gamedata.getStaff();
  for (const StaffRole role :
       {StaffRole::AssistantManager, StaffRole::FitnessCoach, StaffRole::Physio,
        StaffRole::GoalkeepingCoach})
  {
    if (staff.roleCount(team_id, role) == 0)
      inputs.missing_roles.emplace_back(StaffModel::roleKey(role));
  }
  return inputs;
}

float tacticalFamiliarity(const GameData& gamedata, TeamID team_id)
{
  const TeamTrainingPlan* plan = gamedata.getTraining().findPlan(team_id);
  return (plan ? plan->familiarity : defaultPlan().familiarity) / 100.0f;
}
}  // namespace TrainingSystem
