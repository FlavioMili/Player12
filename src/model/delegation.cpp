// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/delegation.h"

#include <utility>

namespace
{
constexpr DutyOwner M = DutyOwner::Manager;
constexpr DutyOwner A = DutyOwner::Assistant;

// Owners per preset, in Duty order.
constexpr std::array<std::array<DutyOwner, DUTY_COUNT>,
                     static_cast<std::size_t>(
                         std::to_underlying(DelegationPreset::COUNT))>
    PRESETS = {{
        // Lineup, subs, training, renewals, scouting, friendlies, youth
        {M, M, M, M, M, M, M},  // Hands-on
        {A, M, A, M, M, A, M},  // Balanced
        {A, A, A, A, A, A, A},  // Assistant runs daily operations
    }};

constexpr std::array<const char*, DUTY_COUNT> DUTY_KEYS = {
    "DELEGATION_DUTY_LINEUP",   "DELEGATION_DUTY_SUBS",
    "DELEGATION_DUTY_TRAINING", "DELEGATION_DUTY_RENEWALS",
    "DELEGATION_DUTY_SCOUTING", "DELEGATION_DUTY_FRIENDLIES",
    "DELEGATION_DUTY_YOUTH"};

constexpr std::array<const char*, DUTY_COUNT> DUTY_HELP_KEYS = {
    "DELEGATION_HELP_LINEUP",   "DELEGATION_HELP_SUBS",
    "DELEGATION_HELP_TRAINING", "DELEGATION_HELP_RENEWALS",
    "DELEGATION_HELP_SCOUTING", "DELEGATION_HELP_FRIENDLIES",
    "DELEGATION_HELP_YOUTH"};

constexpr std::array<const char*, static_cast<std::size_t>(std::to_underlying(
                                      DelegationPreset::COUNT))>
    PRESET_KEYS = {"DELEGATION_PRESET_HANDS_ON", "DELEGATION_PRESET_BALANCED",
                   "DELEGATION_PRESET_ASSISTANT"};
}  // namespace

DelegationPolicy::DelegationPolicy()
    : duties(PRESETS[static_cast<std::size_t>(
          std::to_underlying(DelegationPreset::Balanced))])
{
}

DelegationPolicy DelegationPolicy::preset(DelegationPreset preset)
{
  DelegationPolicy policy;
  policy.apply(preset);
  return policy;
}

DutyOwner DelegationPolicy::owner(Duty duty) const
{
  const auto index = static_cast<std::size_t>(std::to_underlying(duty));
  return index < DUTY_COUNT ? duties[index] : DutyOwner::Manager;
}

bool DelegationPolicy::set(Duty duty, DutyOwner owner)
{
  const auto index = static_cast<std::size_t>(std::to_underlying(duty));
  if (index >= DUTY_COUNT) return false;
  duties[index] = owner;
  return true;
}

void DelegationPolicy::apply(DelegationPreset preset)
{
  const auto index = static_cast<std::size_t>(std::to_underlying(preset));
  if (index >= PRESETS.size()) return;
  duties = PRESETS[index];
}

std::optional<DelegationPreset> DelegationPolicy::matchingPreset() const
{
  for (std::size_t index = 0; index < PRESETS.size(); ++index)
  {
    if (duties == PRESETS[index]) return static_cast<DelegationPreset>(index);
  }
  return std::nullopt;
}

const char* dutyKey(Duty duty)
{
  const auto index = static_cast<std::size_t>(std::to_underlying(duty));
  return index < DUTY_COUNT ? DUTY_KEYS[index] : "";
}

const char* dutyHelpKey(Duty duty)
{
  const auto index = static_cast<std::size_t>(std::to_underlying(duty));
  return index < DUTY_COUNT ? DUTY_HELP_KEYS[index] : "";
}

const char* delegationPresetKey(DelegationPreset preset)
{
  const auto index = static_cast<std::size_t>(std::to_underlying(preset));
  return index < PRESET_KEYS.size() ? PRESET_KEYS[index] : "";
}
