// -----------------------------------------------------------------------------
// Football Management Project
// Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
// See the LICENSE file in the project root.
// -----------------------------------------------------------------------------
#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/match_recorder.h"

using namespace MatchEngineInternal;

PossessionSnapshot MatchEngine::getPossessionSnapshot() const
{
  const auto id = [](const Player* player) -> PlayerID
  { return player ? player->getId() : 0; };
  PossessionSnapshot view;
  // No new implementation is dispatched in Stage 1, regardless of body/brain.
  view.implementation = PossessionId::AARDVARK;
  view.tick = stepCounter;
  view.seconds = ballNow();
  view.openPlay = state == MatchState::PLAYING;
  view.legacyOwner = id(ball.possessedBy);
  view.expectedReceiver = id(ball.intendedReceiver);
  view.pendingPasser = id(ball.touchPasser);
  view.legacyLastPossessor = id(ball.lastPossessor);
  view.legacyKicker = id(ball.kicker);
  view.legacyDeflector = id(ball.deflectedBy);
  view.lastControlledTeamHome = lastControlledTeamHome;
  view.inTouch = ball.inTouch;
  view.collected = ball.touchCollected;
  view.dropping = ball.touchDropping;
  view.inHands = ball.inKeeperHands;
  view.pass = ball.isPass;
  view.shot = ball.isShot;
  view.meetFor = id(ball.meetFor);
  view.pathSince = ball.pathSince;
  if (ball.possessedBy)
    for (const MatchPlayer& player : players)
      if (player.player == ball.possessedBy)
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

void MatchEngine::observePossession(PossessionObservation observation)
{
  if (MatchRecorder* recorder = getRecorder();
      recorder && recorder->wantsPossession())
    recorder->onPossessionObservation(*this, observation,
                                      getPossessionSnapshot());
}

void MatchEngine::observePossessionGate(const MatchPlayer& player,
                                        PossessionContactGate gate)
{
  if (MatchRecorder* recorder = getRecorder();
      recorder && recorder->wantsPossession())
    recorder->onPossessionContactGate(*this, player, gate);
}
