// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include "model/match_engine.h"
#include "model/match_recorder.h"
#include "tools/match_sandbox_recorder.h"

/** English labels of engine states for the match sandbox's debug windows. */
namespace SandboxNames
{
inline const char* phase(TeamPhase phase)
{
  switch (phase)
  {
    case TeamPhase::STOPPAGE:
      return "Stoppage";
    case TeamPhase::SET_PIECE:
      return "Set piece";
    case TeamPhase::DEFENSIVE_BLOCK:
      return "Defensive block";
    case TeamPhase::DEFENSIVE_TRANSITION:
      return "Defensive transition";
    case TeamPhase::ATTACKING_TRANSITION:
      return "Attacking transition";
    case TeamPhase::POSSESSION:
      return "Possession";
    case TeamPhase::FINAL_THIRD:
      return "Final third";
  }
  return "?";
}

inline const char* intent(PlayerIntent intent)
{
  switch (intent)
  {
    case PlayerIntent::HOLD_SHAPE:
      return "Hold shape";
    case PlayerIntent::CARRY_BALL:
      return "Carry ball";
    case PlayerIntent::OFFER_SUPPORT:
      return "Offer support";
    case PlayerIntent::RECEIVE_PASS:
      return "Receive pass";
    case PlayerIntent::RUN_IN_BEHIND:
      return "Run in behind";
    case PlayerIntent::ATTACK_BOX:
      return "Attack box";
    case PlayerIntent::OVERLAP:
      return "Overlap";
    case PlayerIntent::PRESS_BALL:
      return "Press ball";
    case PlayerIntent::COVER_PRESS:
      return "Cover press";
    case PlayerIntent::BLOCK_PASSING_LANE:
      return "Block passing lane";
    case PlayerIntent::MARK_OPPONENT:
      return "Mark opponent";
    case PlayerIntent::CLAIM_LOOSE_BALL:
      return "Claim loose ball";
    case PlayerIntent::RECOVER_SHAPE:
      return "Recover shape";
    case PlayerIntent::GOALKEEP:
      return "Goalkeep";
  }
  return "?";
}

inline const char* job(SandboxRecorder::Job job)
{
  switch (job)
  {
    case SandboxRecorder::Job::NONE:
      return "no job";
    // The planner's two players nearest the ball, for either side: pressers
    // when the other side has it, the receiver or ball-winner otherwise.
    case SandboxRecorder::Job::PRESSER_1:
      return "to ball 1";
    case SandboxRecorder::Job::PRESSER_2:
      return "to ball 2";
    case SandboxRecorder::Job::RUNNER_1:
      return "runner 1";
    case SandboxRecorder::Job::RUNNER_2:
      return "runner 2";
    case SandboxRecorder::Job::RUNNER_3:
      return "runner 3";
    case SandboxRecorder::Job::MIDFIELD_ARRIVAL:
      return "late midfield arrival";
    case SandboxRecorder::Job::FAR_POST:
      return "far-post runner";
    case SandboxRecorder::Job::OVERLAP:
      return "overlapping full-back";
    case SandboxRecorder::Job::SUPPORTER_1:
      return "supporter 1";
    case SandboxRecorder::Job::SUPPORTER_2:
      return "supporter 2";
    case SandboxRecorder::Job::SUPPORTER_3:
      return "supporter 3";
    case SandboxRecorder::Job::COVER_OUTLET:
      return "cover outlet";
  }
  return "?";
}

inline const char* choice(ScenarioAction action)
{
  switch (action)
  {
    case ScenarioAction::NONE:
      return "none";
    case ScenarioAction::SHOT:
      return "SHOT";
    case ScenarioAction::PASS:
      return "PASS";
    case ScenarioAction::CARRY:
      return "CARRY";
    case ScenarioAction::SHIELD:
      return "SHIELD";
    case ScenarioAction::CLEAR:
      return "CLEAR";
  }
  return "?";
}

inline const char* passIntent(PassIntent intent)
{
  switch (intent)
  {
    case PassIntent::RECYCLE:
      return "recycle";
    case PassIntent::PROGRESSIVE:
      return "progressive";
    case PassIntent::THROUGH_BALL:
      return "through ball";
    case PassIntent::CROSS:
      return "cross";
    case PassIntent::CUTBACK:
      return "cutback";
    case PassIntent::SWITCH_PLAY:
      return "switch";
    case PassIntent::PRESSURE_RELEASE:
      return "pressure release";
    case PassIntent::SET_PIECE:
      return "set piece";
  }
  return "?";
}

inline const char* action(MatchActionKind kind)
{
  switch (kind)
  {
    case MatchActionKind::PASS:
      return "PASS";
    case MatchActionKind::SHOT:
      return "SHOT";
    case MatchActionKind::CLEARANCE:
      return "CLEARANCE";
    case MatchActionKind::KNOCK_DOWN:
      return "KNOCK-DOWN";
    case MatchActionKind::TAKE_ON:
      return "TAKE-ON";
    case MatchActionKind::TACKLE:
      return "TACKLE";
  }
  return "?";
}

inline const char* duel(MatchDuelResult result)
{
  switch (result)
  {
    case MatchDuelResult::NONE:
      return "";
    case MatchDuelResult::WON:
      return "won";
    case MatchDuelResult::POKED_LOOSE:
      return "poked loose";
    case MatchDuelResult::LOST:
      return "lost";
    case MatchDuelResult::FOUL:
      return "foul";
  }
  return "?";
}

}  // namespace SandboxNames
