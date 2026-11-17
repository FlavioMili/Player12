// -----------------------------------------------------------------------------
// Football Management Project
// Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
// See the LICENSE file in the project root.
// -----------------------------------------------------------------------------
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "global/types.h"

/** Independent per-match possession implementation. */
enum class PossessionId : std::uint8_t
{
  AARDVARK,
  BINTURONG
};

inline constexpr std::array<PossessionId, 2> ALL_POSSESSIONS = {
    PossessionId::AARDVARK, PossessionId::BINTURONG};

constexpr std::string_view possessionName(PossessionId id)
{
  switch (id)
  {
    case PossessionId::AARDVARK:
      return "Aardvark";
    case PossessionId::BINTURONG:
      return "Binturong";
  }
  return "Unknown";
}

constexpr std::string_view possessionKey(PossessionId id)
{
  switch (id)
  {
    case PossessionId::AARDVARK:
      return "aardvark";
    case PossessionId::BINTURONG:
      return "binturong";
  }
  return "unknown";
}

constexpr std::optional<PossessionId> possessionFromKey(std::string_view key)
{
  if (key == "aardvark")
  {
    return PossessionId::AARDVARK;
  }
  if (key == "binturong")
  {
    return PossessionId::BINTURONG;
  }
  return std::nullopt;
}

/** What existing state actually establishes, not a new control policy.
 * LegacyAttached and RestartOwner deliberately avoid claiming physical foot
 * control/custody where the old owner pointer does not establish it. */
enum class PossessionMode : std::uint8_t
{
  UNCONTROLLED,
  TOUCH_CLAIM,
  FOOT_CONTROL,
  KEEPER_HANDS,
  LEGACY_ATTACHED,
  RESTART_OWNER
};

constexpr std::string_view possessionModeName(PossessionMode mode)
{
  switch (mode)
  {
    case PossessionMode::UNCONTROLLED:
      return "uncontrolled";
    case PossessionMode::TOUCH_CLAIM:
      return "touch-claim";
    case PossessionMode::FOOT_CONTROL:
      return "collected-touch";
    case PossessionMode::KEEPER_HANDS:
      return "keeper-hands";
    case PossessionMode::LEGACY_ATTACHED:
      return "legacy-attached";
    case PossessionMode::RESTART_OWNER:
      return "restart-owner";
  }
  return "unknown";
}

/** Value-only projection: safe to retain across steps and engine copies.
 * IDs are historical identities, not pitch slots. No observer owns a second
 * writable simulation state. Missing physical/deliberate-contact history is
 * left unknown: lastPossessor/kicker cannot reliably reconstruct those facts.
 */
struct PossessionSnapshot
{
  PossessionId implementation = PossessionId::AARDVARK;
  PossessionMode mode = PossessionMode::UNCONTROLLED;
  std::uint64_t tick = 0;
  double seconds = 0.0;
  bool openPlay = false;
  PlayerID legacyOwner = 0;
  PlayerID controller = 0;
  PlayerID claimant = 0;
  PlayerID expectedReceiver = 0;
  std::vector<PlayerID> passOffsidePlayers;
  PlayerID pendingPasser = 0;
  PlayerID legacyLastPossessor = 0;
  PlayerID legacyKicker = 0;
  PlayerID legacyDeflector = 0;
  std::optional<PlayerID> lastPhysicalTouch;
  std::optional<PlayerID> lastDeliberatePlay;
  std::optional<bool> lastControlledTeamHome;
  bool ownerKnown = false;
  bool ownerActive = false;
  bool ownerGoalkeeper = false;
  bool ownerInOwnArea = false;
  bool inTouch = false;
  bool collected = false;
  /** Current Binturong control; absent preserves the Aardvark interpretation.
   */
  std::optional<bool> footControlled;
  float claimUncontrolledSeconds = 0.0F;
  float claimUnrecoverableSeconds = 0.0F;
  bool dropping = false;
  bool inHands = false;
  bool pass = false;
  bool shot = false;
  PlayerID meetFor = 0;
  double pathSince = 0.0;
  float ownerGapMetres = 0.0F;
  bool operator==(const PossessionSnapshot&) const = default;
};

/** Interpret the legacy flags without inventing missing control evidence.
 * Invalid combinations remain available to possessionIssues for diagnosis. */
constexpr PossessionSnapshot interpretLegacyPossession(PossessionSnapshot state)
{
  state.controller = 0;
  state.claimant = state.inTouch ? state.legacyOwner : 0;
  state.mode = PossessionMode::UNCONTROLLED;
  if (state.legacyOwner == 0)
  {
    return state;
  }
  if (!state.openPlay)
  {
    state.mode = PossessionMode::RESTART_OWNER;
  }
  else if (state.inHands)
  {
    state.mode = PossessionMode::KEEPER_HANDS;
    state.controller = state.legacyOwner;
  }
  else if (state.inTouch)
  {
    const bool controlled = state.footControlled.value_or(state.collected);
    state.mode =
        controlled ? PossessionMode::FOOT_CONTROL : PossessionMode::TOUCH_CLAIM;
    if (controlled)
    {
      state.controller = state.legacyOwner;
    }
  }
  else
  {
    state.mode = PossessionMode::LEGACY_ATTACHED;
  }
  return state;
}

/** Reports legacy inconsistencies without asserting, repairing or changing
 * eligibility. Call on completed steps; helper-boundary states can be partial.
 */
enum class PossessionIssue : std::uint32_t
{
  TOUCH_WITHOUT_OWNER = 1U << 0,
  TOUCH_OUTSIDE_OPEN_PLAY = 1U << 1,
  COLLECTION_WITHOUT_TOUCH = 1U << 2,
  DROPPING_WITHOUT_TOUCH = 1U << 3,
  HANDS_WITHOUT_OWNER = 1U << 4,
  HANDS_AND_TOUCH = 1U << 5,
  OWNER_UNKNOWN = 1U << 6,
  OWNER_INACTIVE = 1U << 7,
  HANDS_NOT_KEEPER = 1U << 8,
  HANDS_OUTSIDE_AREA = 1U << 9,
  MEET_OWNER_MISMATCH = 1U << 10,
  PASS_AND_SHOT = 1U << 11,
};

constexpr std::uint32_t possessionIssues(const PossessionSnapshot& state)
{
  std::uint32_t issues = 0;
  const auto add = [&](bool failed, PossessionIssue issue)
  {
    if (failed)
    {
      issues |= static_cast<std::uint32_t>(issue);
    }
  };
  add(state.inTouch && state.legacyOwner == 0,
      PossessionIssue::TOUCH_WITHOUT_OWNER);
  add(state.inTouch && !state.openPlay,
      PossessionIssue::TOUCH_OUTSIDE_OPEN_PLAY);
  add(state.collected && !state.inTouch,
      PossessionIssue::COLLECTION_WITHOUT_TOUCH);
  add(state.dropping && !state.inTouch,
      PossessionIssue::DROPPING_WITHOUT_TOUCH);
  add(state.inHands && state.legacyOwner == 0,
      PossessionIssue::HANDS_WITHOUT_OWNER);
  add(state.inHands && state.inTouch, PossessionIssue::HANDS_AND_TOUCH);
  add(state.legacyOwner != 0 && !state.ownerKnown,
      PossessionIssue::OWNER_UNKNOWN);
  add(state.legacyOwner != 0 && state.ownerKnown && !state.ownerActive,
      PossessionIssue::OWNER_INACTIVE);
  add(state.inHands && state.ownerKnown && !state.ownerGoalkeeper,
      PossessionIssue::HANDS_NOT_KEEPER);
  add(state.inHands && state.ownerKnown && !state.ownerInOwnArea,
      PossessionIssue::HANDS_OUTSIDE_AREA);
  add(state.meetFor != 0 &&
          (!state.inTouch || state.meetFor != state.legacyOwner),
      PossessionIssue::MEET_OWNER_MISMATCH);
  add(state.pass && state.shot, PossessionIssue::PASS_AND_SHOT);
  return issues;
}

/** Stage 1 observations identify existing helper calls, not an invented
 * complete contact event stream. Direct writes remain visible in step samples.
 */
enum class PossessionObservation : std::uint8_t
{
  ASSIGN_OWNER,
  ACQUIRE,
  CLEAR_OWNER,
  CLEAR_FLIGHT,
  UPDATE_TOUCH,
  NOTE_TOUCH,
  BEGIN_TOUCH_CLAIM,
  BEGIN_COLLECTED_DRIBBLE,
  NOTE_DRIBBLE_TOUCH,
  RECORD_PASS_COMPLETION,
  CREDIT_PASS_COMPLETION,
  RELEASE,
  BEGIN_PASS_FLIGHT,
  FINISH_PASS_FLIGHT,
  NOTE_SHOT_FLIGHT,
  CONSUME_FIRST_TIME_INTENT,
  CONSUME_PENDING_STRIKE,
  NOTE_FREE_BALL_TACKLE,
  SETTLE_TACKLE,
  TRACK_HEADER_RUN,
  CANCEL_PENDING_TACKLE,
  NOTE_HEADER_CONTACT,
  CREDIT_HEADER_SHOT,
  BEGIN_HEADER_PLAY,
  NOTE_CUSHIONED_HEADER,
  NOTE_TACKLE_NUDGE,
  BEGIN_POKED_TACKLE,
  FINISH_POKED_TACKLE,
  CREDIT_IMMEDIATE_TACKLE_WIN,
  ARM_TACKLE_STRIKE,
  LOSE_BALL_ON_FALL,
  NOTE_FALLEN_DEFLECTION,
  NOTE_BODY_DEFLECTION_HIT,
  BEGIN_BODY_DEFLECTION,
  BEGIN_SHOT_DEFLECTION,
  END_DEFLECTED_SHOT_FLIGHT,
  FINISH_SHOT_DEFLECTION,
  NOTE_FAILED_TOUCH,
  BEGIN_KEEPER_HOLD,
  REBASE_KEEPER_HOLD,
  END_KEEPER_HOLD,
  NOTE_KEEPER_DISTRIBUTION,
  BEGIN_KEEPER_HAND_REBOUND,
  NOTE_KEEPER_SHOT_RESOLVED,
  FINISH_KEEPER_PARRY,
  CREDIT_KEEPER_SAVE,
  COMPLETE_KEEPER_SAVE,
  BEGIN_LEGACY_KEEPER_PUNCH,
  FINISH_LEGACY_KEEPER_PUNCH,
  REARM_SHOT_AFTER_BOUNCE,
  END_SPENT_KEEPER_SHOT,
  ASSIGN_RESTART_OWNER,
  MARK_THROW_ENTRY,
  NOTE_WOODWORK_CONTACT,
  MARK_WALL_STOPPED_SHOT,
  CREDIT_WALL_BLOCKER,
  NOTE_SHOT_SET_PIECE_ORIGIN,
  NOTE_DIRECT_FREE_KICK_SHOT,
  RESET_SHOOTOUT_BALL,
  RESET_RULE_CONTACT_ATTEMPTS,
  RESET_LIFECYCLE_BALL,
  ASSIGN_SCENARIO_OWNER,
  BEGIN_FED_BALL,
  FINISH_FED_BALL,
  REMEMBER_PHASE_TEAM,
  NOTE_CARRIER_HISTORY,
  SUBSTITUTE_POSSESSION,
  RELEASE_REMOVED_PLAYER,
  CLEAR_SHOOTER_HISTORY,
  SET_PASS_FLIGHT_TARGET,
  NOTE_GOAL_SIDE,
  PREPARE_SHOT_ASSIST,
  NOTE_CONTACT_ACTOR,
  NOTE_PATH_CHANGE,
  ADVANCE_SHOT_FLIGHT_CLOCK,
  NOTE_THROW_TEAM,
  EXPIRE_TOUCH_CLAIM,
  COUNT
};

enum class PossessionContactGate : std::uint8_t
{
  OWNER_NOT_COLLECTED,
  OWNER_PROTECTED,
  FRIENDLY_CLAIM,
  FRIENDLY_PASS,
  KICKER_LOCKOUT,
  ALREADY_ATTEMPTED,
  FOOT_RECOVERING,
  WAIT_FOR_CLOSER,
  CONTACT_NOT_COMMITTED,
  KEEPER_NOT_READY,
  OUT_OF_REACH,
  BEHIND_REACH,
  PHYSICAL_RECOVERY
};

constexpr std::string_view possessionGateName(PossessionContactGate gate)
{
  switch (gate)
  {
    case PossessionContactGate::OWNER_NOT_COLLECTED:
      return "owner-not-collected";
    case PossessionContactGate::OWNER_PROTECTED:
      return "owner-in-reach";
    case PossessionContactGate::FRIENDLY_CLAIM:
      return "friendly-claim-distance";
    case PossessionContactGate::FRIENDLY_PASS:
      return "friendly-pass-intended-for-another";
    case PossessionContactGate::KICKER_LOCKOUT:
      return "kicker-lockout";
    case PossessionContactGate::ALREADY_ATTEMPTED:
      return "already-attempted";
    case PossessionContactGate::FOOT_RECOVERING:
      return "foot-recovering";
    case PossessionContactGate::WAIT_FOR_CLOSER:
      return "wait-for-closer";
    case PossessionContactGate::CONTACT_NOT_COMMITTED:
      return "path-too-new-to-commit";
    case PossessionContactGate::KEEPER_NOT_READY:
      return "keeper-gather-not-ready";
    case PossessionContactGate::OUT_OF_REACH:
      return "outside-contact-reach";
    case PossessionContactGate::BEHIND_REACH:
      return "behind-contact-reach";
    case PossessionContactGate::PHYSICAL_RECOVERY:
      return "trapping-airborne-fallen-or-diving";
  }
  return "unknown";
}
