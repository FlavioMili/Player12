// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The ball leaving play and every restart go to the match's out-of-play
// version (MatchEngine::setOutOfPlay). To add one: an OutOfPlayId
// (match_out_of_play.h), its members (match_out_of_play_<name>_members.h,
// included in MatchEngine), its definitions (match_out_of_play_<name>.cpp),
// and a case in each function here.

#include "model/match_engine.h"
#include "model/match_engine_internal.h"

void MatchEngine::checkOutOfBounds()
{
  // A shootout kick (goalkeeper plan step 9): off the pitch it ends the
  // kick, in or out, and nothing restarts.
  if (shootout.live)
  {
    shootoutBallOut();
    return;
  }
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      checkOutOfBoundsBinturong();
      return;
  }
  checkOutOfBoundsAardvark();
}

bool MatchEngine::ballInPlayAt(Vector2F at) const
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      return ballInPlayAtBinturong(at);
  }
  return ballInPlayAtAardvark(at);
}

void MatchEngine::scoreGoal(bool homeTeam)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      scoreGoalBinturong(homeTeam);
      return;
  }
  scoreGoalAardvark(homeTeam);
}

void MatchEngine::kickOffAfterGoal(bool homeKickingOff)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      kickOffAfterGoalBinturong(homeKickingOff);
      return;
  }
  setupKickOff(homeKickingOff);
}

void MatchEngine::updateDeadBall(float dt)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      // The ball is put on the spot at once: nothing to bring back.
      return;
    case OutOfPlayId::BINTURONG:
      updateDeadBallBinturong(dt);
      return;
  }
}

std::optional<float> MatchEngine::frameInPath(float seconds) const
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      // The woodwork is met at the goal line (checkOutOfBounds).
      return std::nullopt;
    case OutOfPlayId::BINTURONG:
      return frameInPathBinturong(seconds);
  }
  return std::nullopt;
}

void MatchEngine::bounceOffFrame()
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      return;
    case OutOfPlayId::BINTURONG:
      bounceOffFrameBinturong();
      return;
  }
}

void MatchEngine::keepRestartDistance(MatchPlayer& player)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      // The restart goes on its countdown, wherever anyone is.
      return;
    case OutOfPlayId::BINTURONG:
      keepRestartDistanceBinturong(player);
      return;
  }
}

void MatchEngine::updateBallInNet(float dt)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      updateBallInNetBinturong(dt);
      return;
  }
  updateBallInNetAardvark(dt);
}

void MatchEngine::setupThrowIn(bool homeTeam)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      setupThrowInBinturong(homeTeam);
      return;
  }
  setupThrowInAardvark(homeTeam);
}

void MatchEngine::setupGoalKick(bool homeTeam)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      setupGoalKickBinturong(homeTeam);
      return;
  }
  setupGoalKickAardvark(homeTeam);
}

void MatchEngine::setupCorner(bool homeTeam, bool topCorner)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      setupCornerBinturong(homeTeam, topCorner);
      return;
  }
  setupCornerAardvark(homeTeam, topCorner);
}

void MatchEngine::setupFreeKick(bool homeTeam, Vector2F foulPos)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      setupFreeKickBinturong(homeTeam, foulPos);
      return;
  }
  setupFreeKickAardvark(homeTeam, foulPos);
}

void MatchEngine::setupPenalty(bool homeTeam)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      setupPenaltyBinturong(homeTeam);
      return;
  }
  setupPenaltyAardvark(homeTeam);
}

void MatchEngine::placeTaker(MatchPlayer& taker, Vector2F spot)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      placeTakerBinturong(taker, spot);
      return;
  }
  placeTakerAardvark(taker, spot);
}

void MatchEngine::completeRestart()
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
        if (possessionId == PossessionId::BINTURONG && !restartLineUp &&
            state != MatchState::KICK_OFF && !userRestartPassRequested())
          if (const MatchPlayer* taker = restartTaker(); taker &&
              MatchEngineInternal::active(*taker) && linesUpRestart(*taker, state) &&
              distance(taker->position, ball.position) >
                  touchKnobs[TouchKnob::COMFORT_GAP])
            return;
      break;
    case OutOfPlayId::BINTURONG:
      completeRestartBinturong();
      return;
  }
  completeRestartAardvark();
}
