// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

/**
 * @brief Recurring jobs the manager may hand to the backroom staff.
 *
 * Values are persisted (table DelegatedDuties); append new duties at the end.
 */
enum class Duty : std::uint8_t
{
  LineupFixes = 0,  /*!< Replace injured/suspended players before kick-off. */
  Substitutions,    /*!< In-match changes of the managed side. */
  TrainingSchedule, /*!< Squad intensity and congestion weeks. */
  ContractRenewals, /*!< Final-year renewals of players outside the core. */
  ScoutingAssignments, /*!< Idle scouts are sent out. */
  Friendlies,          /*!< Pre-season friendly opponents. */
  YouthContracts,      /*!< Contracts for the youth intake trialists. */
  COUNT
};

inline constexpr std::size_t DUTY_COUNT =
    static_cast<std::size_t>(std::to_underlying(Duty::COUNT));

/** @brief Who takes care of a duty (values are persisted). */
enum class DutyOwner : std::uint8_t
{
  Manager = 0,
  Assistant
};

/** @brief Named delegation profiles offered on the delegation screen. */
enum class DelegationPreset : std::uint8_t
{
  HandsOn = 0,   /*!< The manager decides everything that can be decided. */
  Balanced,      /*!< Matchday safety nets delegated, squad business kept. */
  AssistantRuns, /*!< The assistant runs the daily operations. */
  COUNT
};

/**
 * @brief Who handles each duty of the managed club.
 *
 * Other systems read the policy through GameController::isDelegated(); the
 * policy itself holds no game logic. When the manager changes something by
 * hand that the assistant owns, GameController takes the duty back for him.
 */
class DelegationPolicy
{
 public:
  /** Balanced preset. */
  DelegationPolicy();

  /** The owners of a preset. */
  static DelegationPolicy preset(DelegationPreset preset);

  DutyOwner owner(Duty duty) const;
  bool delegated(Duty duty) const
  {
    return owner(duty) == DutyOwner::Assistant;
  }
  /** Changes one duty; false for an unknown duty. */
  bool set(Duty duty, DutyOwner owner);
  /** Replaces every duty with the preset's owners. */
  void apply(DelegationPreset preset);
  /** The preset this policy matches exactly, if any. */
  std::optional<DelegationPreset> matchingPreset() const;

  const std::array<DutyOwner, DUTY_COUNT>& owners() const { return duties; }

  bool operator==(const DelegationPolicy&) const = default;

 private:
  std::array<DutyOwner, DUTY_COUNT> duties{};
};

/** Language key naming a duty ("DELEGATION_DUTY_LINEUP"...). */
const char* dutyKey(Duty duty);
/** Language key explaining what the assistant does for a duty. */
const char* dutyHelpKey(Duty duty);
/** Language key naming a preset. */
const char* delegationPresetKey(DelegationPreset preset);
