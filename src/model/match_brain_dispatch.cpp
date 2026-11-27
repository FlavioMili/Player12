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
    case BrainId::CAPYBARA:
      return movementStyleCapybara();
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
    case BrainId::CAPYBARA:
      return brainTraitsCapybara();
  }
  // The Aardvark brain decides none of these: the engine's defaults.
  static constexpr BrainTraits none{};
  return none;
}

std::optional<float> MatchEngine::chooseCarryPace(
    const MatchPlayer& player) const
{
  switch (brainOf(player.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return carryPaceBinturong(player);
    case BrainId::CAPYBARA:
      return carryPaceCapybara(player);
  }
  return std::nullopt;
}

std::optional<float> MatchEngine::chooseTouchSeconds(const MatchPlayer& player,
                                                     float pace,
                                                     float toEnd) const
{
  switch (brainOf(player.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return chooseTouchSecondsBinturong(player, pace, toEnd);
    case BrainId::CAPYBARA:
      return chooseTouchSecondsCapybara(player, pace, toEnd);
  }
  return std::nullopt;
}

std::optional<float> MatchEngine::choosePushMetres(
    const MatchPlayer& player, PitchPoint from, Vector2F direction,
    float startGap, float pace, float seconds,
    std::optional<float> toGoal) const
{
  switch (brainOf(player.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return pushMetresBinturong(player, from, direction, startGap, pace,
                                 seconds, toGoal);
    case BrainId::CAPYBARA:
      return pushMetresCapybara(player, from, direction, startGap, pace,
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
    case BrainId::CAPYBARA:
      return choosePassTargetCapybara(passer);
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
    case BrainId::CAPYBARA:
      refreshTacticalTargetsCapybara(dt, planHome);
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
    case BrainId::CAPYBARA:
      assignMarksCapybara(home);
      return;
  }
  assignMarksAardvark(home);
}

PitchPoint MatchEngine::goalkeeperTarget(MatchPlayer& keeper,
                                         const MatchPlayer* carrier)
{
  switch (brainOf(keeper.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return goalkeeperTargetBinturong(keeper, carrier);
    case BrainId::CAPYBARA:
      return goalkeeperTargetCapybara(keeper, carrier);
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
    case BrainId::CAPYBARA:
      return chooseBallActionCapybara(carrier);
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
    case BrainId::CAPYBARA:
      return chooseTakeOnCapybara(carrier);
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
    case BrainId::CAPYBARA:
      planOvertakeCapybara(slot);
      return;
  }
  planOvertakeAardvark(slot);
}

void MatchEngine::afterPassReleased(const MatchPlayer& passer,
                                    const MatchPlayer& receiver)
{
  noteRunAtPass(passer);
  switch (brainOf(passer.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      afterPassReleasedBinturong(passer, receiver);
      return;
    case BrainId::CAPYBARA:
      afterPassReleasedCapybara(passer, receiver);
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
    case BrainId::CAPYBARA:
      return evaluatePassOptionCapybara(passer, receiver, detail);
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
    case BrainId::CAPYBARA:
      return chooseShotAimCapybara(shooter, header, penalty, freeKick);
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
    case BrainId::CAPYBARA:
      return choosePenaltyGuessCapybara(keeper);
  }
  return choosePenaltyGuessAardvark(keeper);
}

void MatchEngine::decidePenalty(const MatchPlayer& taker)
{
  // Only Capybara decides before the run-up; the others aim at the strike.
  if (brainOf(taker.isHomeTeam) == BrainId::CAPYBARA)
  {
    decidePenaltyCapybara(taker);
  }
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
    case BrainId::CAPYBARA:
      return chooseClearanceAimCapybara(defender);
  }
  return chooseClearanceAimAardvark(defender);
}

PitchPoint MatchEngine::chooseClearBehindTarget(const MatchPlayer& defender)
{
  switch (brainOf(defender.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return chooseClearBehindTargetBinturong(defender);
    case BrainId::CAPYBARA:
      return chooseClearBehindTargetCapybara(defender);
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
    case BrainId::CAPYBARA:
      return chooseHeaderAttemptCapybara(header, fromTeammate);
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
    case BrainId::CAPYBARA:
      return chooseHeaderPlayCapybara(header, fromTeammate);
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
    case BrainId::CAPYBARA:
      return touchNowCapybara(player, moment);
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
    case BrainId::CAPYBARA:
      return dribbleTouchNowCapybara(player, moment);
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
    case BrainId::CAPYBARA:
      return chooseFirstTouchCapybara(player, moment);
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
    case BrainId::CAPYBARA:
      return chooseChallengeCapybara(carrier, dt);
  }
  return chooseChallengeAardvark(carrier, dt);
}

bool MatchEngine::goesIntoContest(MatchPlayer& defender,
                                  const MatchPlayer& owner, PitchPoint contact)
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
      case BrainId::CAPYBARA:
        defender.goingIn = goesIntoContestCapybara(defender, owner, contact);
        break;
    }
    if (defender.goingIn)
    {
      ++(defender.isHomeTeam ? stats.homeGoIns : stats.awayGoIns);
    }
    else
    {
      ++(defender.isHomeTeam ? stats.homeHolds : stats.awayHolds);
    }
  }
  return defender.goingIn;
}

std::optional<TackleTouch> MatchEngine::chooseTackleTouch(
    MatchPlayer& tackler, const MatchPlayer& owner, const TouchMoment& moment,
    bool contest)
{
  switch (brainOf(tackler.isHomeTeam))
  {
    case BrainId::AARDVARK:
    case BrainId::BINTURONG:
      break;
    case BrainId::CAPYBARA:
      return chooseTackleTouchCapybara(tackler, owner, moment, contest);
  }
  return std::nullopt;
}

TackleIntent MatchEngine::chooseTackleIntent(const MatchPlayer& tackler,
                                             const MatchPlayer& attacker,
                                             PitchPoint contact, bool freeBall)
{
  switch (brainOf(tackler.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return chooseTackleIntentBinturong(tackler, attacker, contact, freeBall);
    case BrainId::CAPYBARA:
      return chooseTackleIntentCapybara(tackler, attacker, contact, freeBall);
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
    case BrainId::CAPYBARA:
      return choosesToBlockCapybara(player);
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
    case BrainId::CAPYBARA:
      return choosesToPutBlockBehindCapybara(player);
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
    case BrainId::CAPYBARA:
      return choosesToClearCrossBehindCapybara(player);
  }
  return choosesToClearCrossBehindAardvark(player);
}

MatchPlayer* MatchEngine::chooseRestartTaker(MatchState restart, bool homeTeam,
                                             PitchPoint spot, bool topCorner)
{
  switch (brainOf(homeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return chooseRestartTakerBinturong(restart, homeTeam, spot, topCorner);
    case BrainId::CAPYBARA:
      return chooseRestartTakerCapybara(restart, homeTeam, spot, topCorner);
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
    case BrainId::CAPYBARA:
      return chooseRestartPlayCapybara(taker, restartState);
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
    case BrainId::CAPYBARA:
      applyTeamMentalityCapybara(sliders, homeTeam);
      return;
  }
  applyTeamMentalityAardvark(sliders, homeTeam);
}

MatchEngine::SetPieceAttackers MatchEngine::arrangeSetPieceAttack(
    bool attackingHome, PitchPoint ballPosition)
{
  switch (brainOf(attackingHome))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return arrangeSetPieceAttackBinturong(attackingHome, ballPosition);
    case BrainId::CAPYBARA:
      return arrangeSetPieceAttackCapybara(attackingHome, ballPosition);
  }
  return arrangeSetPieceAttackAardvark(attackingHome, ballPosition);
}

void MatchEngine::arrangeSetPieceDefence(bool defendingHome,
                                         PitchPoint ballPosition,
                                         const SetPieceAttackers& attackers)
{
  switch (brainOf(defendingHome))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      arrangeSetPieceDefenceBinturong(defendingHome, ballPosition, attackers);
      return;
    case BrainId::CAPYBARA:
      arrangeSetPieceDefenceCapybara(defendingHome, ballPosition, attackers);
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
    case BrainId::CAPYBARA:
      return dutyTakerCapybara(homeTeam, duty);
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
    case BrainId::CAPYBARA:
      updateRestartSupportCapybara(player, taker);
      return;
  }
  updateRestartSupportAardvark(player, taker);
}

float MatchEngine::chooseGetUp(const MatchPlayer& player)
{
  // Nobody round the touchline has a brain or a ball to win.
  if (onTouchline(player))
  {
    return touchlineGetUpSeconds(player);
  }
  switch (brainOf(player.isHomeTeam))
  {
    case BrainId::AARDVARK:
      break;
    case BrainId::BINTURONG:
      return chooseGetUpBinturong(player);
    case BrainId::CAPYBARA:
      return chooseGetUpCapybara(player);
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
      players[slot].giveWaySpeed = -1.0F;
      return;
    case BrainId::BINTURONG:
      planWayRoundBinturong(slot);
      return;
    case BrainId::CAPYBARA:
      planWayRoundCapybara(slot);
      return;
  }
}
