// -----------------------------------------------------------------------------
// Football Management Project
// Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
// See the LICENSE file in the project root.
// -----------------------------------------------------------------------------
#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/match_recorder.h"

using namespace MatchEngineInternal;

const Player* MatchEngine::getPossessionAttachment() const
{
  return getPossessionState().possessedBy;
}

const Player* MatchEngine::getTouchClaimant() const
{
  return getPossessionState().inTouch ? getPossessionState().possessedBy
                                      : nullptr;
}

const Player* MatchEngine::getConfirmedController() const
{
  if (state != MatchState::PLAYING) return nullptr;
  const bool foot = possessionId == PossessionId::BINTURONG
                        ? getPossessionState().footControlled
                        : getPossessionState().touchCollected;
  return getPossessionState().inKeeperHands ||
                 (getPossessionState().inTouch && foot)
             ? getPossessionState().possessedBy
             : nullptr;
}

const Player* MatchEngine::getRestartOwner() const
{
  return state != MatchState::PLAYING ? getPossessionState().possessedBy
                                      : nullptr;
}

const Player* MatchEngine::getExpectedReceiver() const
{
  return getPossessionState().intendedReceiver;
}

// Preserve individual legacy facts, including inconsistent helper-boundary
// combinations. These do not infer eligibility or a complete contact history.
bool MatchEngine::hasTouchClaim() const { return getPossessionState().inTouch; }
bool MatchEngine::isTouchCollected() const
{
  return getPossessionState().touchCollected;
}
bool MatchEngine::isTouchDropping() const
{
  return getPossessionState().touchDropping;
}
bool MatchEngine::isKeeperHoldingBall() const
{
  return getPossessionState().inKeeperHands;
}
const Player* MatchEngine::getLastPossessionAttachment() const
{
  return getPossessionState().lastPossessor;
}
const Player* MatchEngine::getLegacyContactActor() const
{
  return getPossessionState().kicker;
}
const Player* MatchEngine::getPendingPasser() const
{
  return getPossessionState().touchPasser;
}

PossessionSnapshot MatchEngine::getPossessionSnapshot() const
{
  const auto id = [](const Player* player) -> PlayerID
  { return player ? player->getId() : 0; };
  PossessionSnapshot view;
  view.implementation = possessionId;
  view.tick = stepCounter;
  view.seconds = ballNow();
  view.openPlay = state == MatchState::PLAYING;
  view.legacyOwner = id(getPossessionState().possessedBy);
  view.expectedReceiver = id(getPossessionState().intendedReceiver);
  view.pendingPasser = id(getPossessionState().touchPasser);
  view.legacyLastPossessor = id(getPossessionState().lastPossessor);
  view.legacyKicker = id(getPossessionState().kicker);
  view.legacyDeflector = id(getPossessionState().deflectedBy);
  view.lastControlledTeamHome = getPossessionHistory().lastControlledTeamHome;
  view.inTouch = getPossessionState().inTouch;
  view.collected = getPossessionState().touchCollected;
  view.claimUncontrolledSeconds = getPossessionState().claimUncontrolledSeconds;
  view.claimUnrecoverableSeconds =
      getPossessionState().claimUnrecoverableSeconds;
  if (possessionId == PossessionId::BINTURONG)
    view.footControlled = getPossessionState().footControlled;
  view.dropping = getPossessionState().touchDropping;
  view.inHands = getPossessionState().inKeeperHands;
  view.pass = getPossessionState().isPass;
  view.shot = getPossessionState().isShot;
  view.meetFor = id(ball.meetFor);
  view.pathSince = getPossessionState().pathSince;
  if (getPossessionState().possessedBy)
    for (const MatchPlayer& player : players)
      if (player.player == getPossessionState().possessedBy)
      {
        view.ownerKnown = true;
        view.ownerActive = active(player);
        view.ownerGoalkeeper = player.isGoalkeeper;
        view.ownerInOwnArea = inPenaltyArea(player.position, player.isHomeTeam);
        view.ownerGapMetres = distance(player.position, ball.position);
        break;
      }
  return interpretLegacyPossession(view);
}

std::uint32_t MatchEngine::getPossessionIssues() const
{
  return possessionIssues(getPossessionSnapshot());
}

void MatchEngine::observePossession(PossessionObservation observation,
                                    PossessionId executedVersion)
{
  if (MatchRecorder* recorder = getRecorder();
      recorder && recorder->wantsPossession())
    recorder->onPossessionObservation(*this, observation,
                                      getPossessionSnapshot(), executedVersion);
}

void MatchEngine::observePossessionGate(const MatchPlayer& player,
                                        PossessionContactGate gate)
{
  if (MatchRecorder* recorder = getRecorder();
      recorder && recorder->wantsPossession())
    recorder->onPossessionContactGate(*this, player, gate);
}
