// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Every decision the body asks for goes to the brain of the side taking it
// (MatchEngine::setBrain). To add a brain: a BrainId (match_brain.h), its
// members (match_brain_<name>_members.h, included in MatchEngine), its
// definitions (match_brain_<name>.cpp), and a case in each function here.

#include "model/match_engine.h"

const MatchEngine::MovementStyle& MatchEngine::movementStyle(
    bool homeTeam) const
{
  switch (brainOf(homeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return movementStyleBinturong();
  }
  return movementStyleAardvark();
}

const MatchEngine::BrainTraits& MatchEngine::brainTraits(bool homeTeam) const
{
  switch (brainOf(homeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return brainTraitsBinturong();
  }
  // The Aardvark brain decides none of these: the engine's defaults.
  static constexpr BrainTraits none{};
  return none;
}

std::optional<float> MatchEngine::chooseCarryPace(const MatchPlayer& player) const
{
  switch (brainOf(player.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return carryPaceBinturong(player);
  }
  return std::nullopt;
}

std::optional<float> MatchEngine::chooseTouchSeconds(const MatchPlayer& player,
                                                     float pace, float toEnd) const
{
  switch (brainOf(player.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return chooseTouchSecondsBinturong(player, pace, toEnd);
  }
  return std::nullopt;
}

std::optional<float> MatchEngine::choosePushMetres(
    const MatchPlayer& player, Vector2F from, Vector2F direction, float startGap,
    float pace, float seconds, std::optional<float> toGoal) const
{
  switch (brainOf(player.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return pushMetresBinturong(player, from, direction, startGap, pace,
                                 seconds, toGoal);
  }
  return std::nullopt;
}

std::optional<MatchEngine::PassOption> MatchEngine::choosePassTarget(
    MatchPlayer& passer)
{
  switch (brainOf(passer.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return choosePassTargetBinturong(passer);
  }
  return choosePassTargetAardvark(passer);
}

void MatchEngine::refreshTacticalTargets(float dt, bool planHome)
{
  switch (brainOf(planHome))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      refreshTacticalTargetsBinturong(dt, planHome);
      return;
  }
  refreshTacticalTargetsAardvark(dt, planHome);
}

void MatchEngine::assignMarks(bool home)
{
  switch (brainOf(home))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      assignMarksBinturong(home);
      return;
  }
  assignMarksAardvark(home);
}

Vector2F MatchEngine::goalkeeperTarget(MatchPlayer& keeper,
                                       const MatchPlayer* carrier)
{
  switch (brainOf(keeper.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return goalkeeperTargetBinturong(keeper, carrier);
  }
  return goalkeeperTargetAardvark(keeper, carrier);
}

MatchEngine::BallAction MatchEngine::chooseBallAction(MatchPlayer& carrier)
{
  switch (brainOf(carrier.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return chooseBallActionBinturong(carrier);
  }
  return chooseBallActionAardvark(carrier);
}

MatchPlayer* MatchEngine::chooseTakeOn(MatchPlayer& carrier)
{
  switch (brainOf(carrier.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return chooseTakeOnBinturong(carrier);
  }
  return chooseTakeOnAardvark(carrier);
}

void MatchEngine::planOvertake(std::size_t slot)
{
  switch (brainOf(players[slot].isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      planOvertakeBinturong(slot);
      return;
  }
  planOvertakeAardvark(slot);
}

void MatchEngine::afterPassReleased(const MatchPlayer& passer,
                                    const MatchPlayer& receiver)
{
  switch (brainOf(passer.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      afterPassReleasedBinturong(passer, receiver);
      return;
  }
  afterPassReleasedAardvark(passer, receiver);
}

MatchEngine::PassOption MatchEngine::evaluatePassOption(
    MatchPlayer& passer, MatchPlayer& receiver,
    PassCandidateDetail* detail) const
{
  switch (brainOf(passer.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return evaluatePassOptionBinturong(passer, receiver, detail);
  }
  return evaluatePassOptionAardvark(passer, receiver, detail);
}

MatchEngine::ShotAim MatchEngine::chooseShotAim(const MatchPlayer& shooter,
                                                bool header, bool penalty,
                                                bool freeKick)
{
  switch (brainOf(shooter.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return chooseShotAimBinturong(shooter, header, penalty, freeKick);
  }
  return chooseShotAimAardvark(shooter, header, penalty, freeKick);
}

float MatchEngine::choosePenaltyGuess(const MatchPlayer& keeper)
{
  switch (brainOf(keeper.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return choosePenaltyGuessBinturong(keeper);
  }
  return choosePenaltyGuessAardvark(keeper);
}

MatchEngine::ClearanceAim MatchEngine::chooseClearanceAim(
    const MatchPlayer& defender)
{
  switch (brainOf(defender.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return chooseClearanceAimBinturong(defender);
  }
  return chooseClearanceAimAardvark(defender);
}

Vector2F MatchEngine::chooseClearBehindTarget(const MatchPlayer& defender)
{
  switch (brainOf(defender.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return chooseClearBehindTargetBinturong(defender);
  }
  return chooseClearBehindTargetAardvark(defender);
}

MatchEngine::HeaderAttempt MatchEngine::chooseHeaderAttempt(
    const MatchPlayer& header, bool fromTeammate)
{
  switch (brainOf(header.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return chooseHeaderAttemptBinturong(header, fromTeammate);
  }
  return chooseHeaderAttemptAardvark(header, fromTeammate);
}

MatchEngine::HeaderPlay MatchEngine::chooseHeaderPlay(const MatchPlayer& header,
                                                      bool fromTeammate)
{
  switch (brainOf(header.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return chooseHeaderPlayBinturong(header, fromTeammate);
  }
  return chooseHeaderPlayAardvark(header, fromTeammate);
}

bool MatchEngine::touchNow(const MatchPlayer& player, const TouchMoment& moment)
{
  // The Aardvark brain has no first-touch decisions: it plays the ball the
  // moment it comes into reach.
  switch (brainOf(player.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return touchNowBinturong(player, moment);
  }
  return true;
}

bool MatchEngine::dribbleTouchNow(const MatchPlayer& player,
                                  const TouchMoment& moment)
{
  // His own ball, due a touch (dribbleTouchDue). The Aardvark brain has no
  // touch timing: he plays it the moment it is due.
  switch (brainOf(player.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return dribbleTouchNowBinturong(player, moment);
  }
  return true;
}

TouchIntent MatchEngine::chooseFirstTouch(const MatchPlayer& player,
                                          const TouchMoment& moment)
{
  switch (brainOf(player.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return chooseFirstTouchBinturong(player, moment);
  }
  return defaultTouchIntent(player);
}

MatchEngine::Challenge MatchEngine::chooseChallenge(const MatchPlayer& carrier,
                                                    float dt)
{
  // The defending side decides whether to go in.
  switch (brainOf(!carrier.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return chooseChallengeBinturong(carrier, dt);
  }
  return chooseChallengeAardvark(carrier, dt);
}

bool MatchEngine::goesIntoContest(MatchPlayer& defender, const MatchPlayer& owner,
                                  Vector2F contact)
{
  // Decided once a step: held, he holds the whole step.
  if (defender.goInStep != stepCounter)
  {
    defender.goInStep = stepCounter;
    switch (brainOf(defender.isHomeTeam))
    {
      case BrainId::AARDVARK:
        defender.goingIn = true;
        break;
      case BrainId::BINTURONG:
        defender.goingIn = goesIntoContestBinturong(defender, owner, contact);
        break;
    }
    if (defender.goingIn)
      ++(defender.isHomeTeam ? stats.homeGoIns : stats.awayGoIns);
    else
      ++(defender.isHomeTeam ? stats.homeHolds : stats.awayHolds);
  }
  return defender.goingIn;
}

TackleIntent MatchEngine::chooseTackleIntent(const MatchPlayer& tackler,
                                             const MatchPlayer& attacker,
                                             Vector2F contact, bool freeBall)
{
  switch (brainOf(tackler.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return chooseTackleIntentBinturong(tackler, attacker, contact, freeBall);
  }
  return TackleIntent::CONTROL;
}

bool MatchEngine::choosesToBlock(const MatchPlayer& player)
{
  switch (brainOf(player.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return choosesToBlockBinturong(player);
  }
  return choosesToBlockAardvark(player);
}

bool MatchEngine::choosesToPutBlockBehind(const MatchPlayer& player)
{
  switch (brainOf(player.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return choosesToPutBlockBehindBinturong(player);
  }
  return choosesToPutBlockBehindAardvark(player);
}

bool MatchEngine::choosesToClearCrossBehind(const MatchPlayer& player)
{
  switch (brainOf(player.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return choosesToClearCrossBehindBinturong(player);
  }
  return choosesToClearCrossBehindAardvark(player);
}

MatchPlayer* MatchEngine::chooseRestartTaker(MatchState restart, bool homeTeam,
                                             Vector2F spot, bool topCorner)
{
  switch (brainOf(homeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return chooseRestartTakerBinturong(restart, homeTeam, spot, topCorner);
  }
  return chooseRestartTakerAardvark(restart, homeTeam, spot, topCorner);
}

MatchEngine::RestartPlay MatchEngine::chooseRestartPlay(MatchPlayer& taker,
                                                        MatchState restartState)
{
  switch (brainOf(taker.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return chooseRestartPlayBinturong(taker, restartState);
  }
  return chooseRestartPlayAardvark(taker, restartState);
}

void MatchEngine::applyTeamMentality(StrategySliders& sliders,
                                     bool homeTeam) const
{
  switch (brainOf(homeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      applyTeamMentalityBinturong(sliders, homeTeam);
      return;
  }
  applyTeamMentalityAardvark(sliders, homeTeam);
}

MatchEngine::SetPieceAttackers MatchEngine::arrangeSetPieceAttack(
    bool attackingHome, Vector2F ballPosition)
{
  switch (brainOf(attackingHome))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return arrangeSetPieceAttackBinturong(attackingHome, ballPosition);
  }
  return arrangeSetPieceAttackAardvark(attackingHome, ballPosition);
}

void MatchEngine::arrangeSetPieceDefence(bool defendingHome,
                                         Vector2F ballPosition,
                                         const SetPieceAttackers& attackers)
{
  switch (brainOf(defendingHome))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      arrangeSetPieceDefenceBinturong(defendingHome, ballPosition, attackers);
      return;
  }
  arrangeSetPieceDefenceAardvark(defendingHome, ballPosition, attackers);
}

MatchPlayer* MatchEngine::dutyTaker(bool homeTeam, SetPieceDuty duty)
{
  switch (brainOf(homeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return dutyTakerBinturong(homeTeam, duty);
  }
  return dutyTakerAardvark(homeTeam, duty);
}

void MatchEngine::updateRestartSupport(MatchPlayer& player,
                                       const MatchPlayer* taker)
{
  switch (brainOf(player.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      updateRestartSupportBinturong(player, taker);
      return;
  }
  updateRestartSupportAardvark(player, taker);
}

float MatchEngine::chooseGetUp(const MatchPlayer& player)
{
  // Nobody round the touchline has a brain or a ball to win.
  if (onTouchline(player)) return touchlineGetUpSeconds(player);
  switch (brainOf(player.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return chooseGetUpBinturong(player);
  }
  return chooseGetUpAardvark(player);
}

void MatchEngine::planWayRound(std::size_t slot)
{
  switch (brainOf(players[slot].isHomeTeam))
  {
    case BrainId::AARDVARK:
      // No way round: he runs at it.
      players[slot].wayRoundOf = -1;
      players[slot].giveWaySpeed = -1.0f;
      return;
    case BrainId::BINTURONG:
      planWayRoundBinturong(slot);
      return;
  }
}
