// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Every body function goes to the body of the player's side (the movers in
// integrateMovements are all of one side; spendEnergy is told its side).
// To add a body: a BodyId (match_body.h), its members
// (match_body_<name>_members.h, included in MatchEngine), its definitions
// (match_body_<name>.cpp), and a case in each function here.

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "model/match_engine.h"
#include "model/match_engine_internal.h"

void MatchEngine::integrateMovements(const std::uint8_t* slots,
                                     std::size_t count, float dt, bool walking)
{
  // The movers share a body (integrateBySide): one side's, or both sides'
  // when both have the Aardvark body.
  switch (bodyOf(count == 0 || players[slots[0]].isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      integrateMovementsBinturong(slots, count, dt, walking);
      return;
  }
  integrateMovementsAardvark(slots, count, dt, walking);
}

void MatchEngine::moveControlled(MatchPlayer& player, Vector2F desired,
                                 float desiredSpeed, float topSpeed,
                                 float fatigue, float dt,
                                 const MatchPlayer* faceToward)
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      moveControlledBinturong(player, desired, desiredSpeed, topSpeed, fatigue,
                              dt, faceToward);
      return;
  }
  moveControlledAardvark(player, desired, desiredSpeed, topSpeed, fatigue, dt,
                         faceToward);
}

void MatchEngine::spendEnergy(FitnessLanes& lanes, std::size_t begin,
                              std::size_t end, float dt, bool homeTeam) const
{
  switch (bodyOf(homeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      spendEnergyBinturong(lanes, begin, end, dt, homeTeam);
      return;
  }
  spendEnergyAardvark(lanes, begin, end, dt, homeTeam);
}

float MatchEngine::injuryChance(const MatchPlayer& player) const
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return injuryChanceBinturong(player);
  }
  return injuryChanceAardvark(player);
}

float MatchEngine::fuelFloor(bool homeTeam) const
{
  switch (bodyOf(homeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return 0.0F;
  }
  return MatchTuning::Player::MINIMUM_STAMINA;
}

float MatchEngine::fuelFatigue(const MatchPlayer& player) const
{
  // Aardvark: fully tired at its fuel floor. Binturong: at an empty tank.
  const float floor = fuelFloor(player.isHomeTeam);
  return std::clamp((1.0F - player.stamina) / (1.0F - floor), 0.0F, 1.0F);
}

SpeedBand MatchEngine::speedBand(const MatchPlayer& player,
                                 MovementMode mode) const
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return speedBandBinturong(player, mode);
  }
  return speedBandAardvark(player, mode);
}

SpeedBandInputs MatchEngine::speedBandInputs(const MatchPlayer& player) const
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return speedBandInputsBinturong(player);
  }
  return speedBandInputsAardvark(player);
}

MovementMode MatchEngine::movementModeAt(const MatchPlayer& player,
                                         float speed) const
{
  // Read once: each band starts at the top of the one below it.
  const SpeedBandInputs inputs = speedBandInputs(player);
  const SpeedBandTops tops =
      speedBandTops(inputs.transition, inputs.critical, inputs.top);
  if (speed > tops.run)
  {
    return MovementMode::SPRINT;
  }
  if (speed > tops.jog)
  {
    return MovementMode::RUN;
  }
  if (speed > tops.walk)
  {
    return MovementMode::JOG;
  }
  return MovementMode::WALK;
}

float MatchEngine::currentTopSpeed(const MatchPlayer& player) const
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return currentTopSpeedBinturong(player);
  }
  return currentTopSpeedAardvark(player);
}

float MatchEngine::executionErrorScale(const MatchPlayer& player) const
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return executionErrorScaleBinturong(player);
  }
  return executionErrorScaleAardvark(player);
}

bool MatchEngine::updateDribble(MatchPlayer& carrier, float dt)
{
  switch (bodyOf(carrier.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return updateDribbleBinturong(carrier, dt);
  }
  return updateDribbleAardvark(carrier, dt);
}

MatchEngine::Kick MatchEngine::kickPass(const MatchPlayer& passer,
                                        const MatchPlayer* receiver,
                                        PitchPoint target, float passDistance,
                                        bool lofted, bool delivery,
                                        std::optional<BallSpin> spin,
                                        std::optional<float> arrivalSpeed,
                                        std::optional<PassTechnique> technique)
{
  switch (bodyOf(passer.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return kickPassBinturong(passer, receiver, target, passDistance, lofted,
                               delivery, spin, arrivalSpeed, technique);
  }
  return kickPassAardvark(passer, receiver, target, passDistance, lofted,
                          delivery, spin, arrivalSpeed);
}

float MatchEngine::recoverySeconds(const MatchPlayer& player, Recovery after)
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return recoverySecondsBinturong(player, after);
  }
  return recoverySecondsAardvark(player, after);
}

float MatchEngine::shotSpread(const MatchPlayer& shooter, bool header,
                              bool penalty, bool freeKick, float metres) const
{
  switch (bodyOf(shooter.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return shotSpreadBinturong(shooter, header, penalty, freeKick, metres);
  }
  return shotSpreadAardvark(shooter, header, penalty, freeKick, metres);
}

MatchEngine::ShotStrike MatchEngine::strikeShotBody(
    const MatchPlayer& shooter, float aimY, float aimZ, float spread,
    bool header, float powerScale, std::optional<ShotTechnique> technique)
{
  switch (bodyOf(shooter.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return strikeShotBodyBinturong(shooter, aimY, aimZ, spread, header,
                                     powerScale, technique);
  }
  return strikeShotBodyAardvark(shooter, aimY, aimZ, spread, header,
                                powerScale);
}

float MatchEngine::clearanceSpeed(const MatchPlayer& defender)
{
  switch (bodyOf(defender.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return clearanceSpeedBinturong(defender);
  }
  return clearanceSpeedAardvark(defender);
}

float MatchEngine::headerClearanceSpeed(const MatchPlayer& header)
{
  switch (bodyOf(header.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return headerClearanceSpeedBinturong(header);
  }
  return headerClearanceSpeedAardvark(header);
}

std::pair<float, float> MatchEngine::headerPace(const MatchPlayer& header,
                                                HeaderPace purpose,
                                                PitchPoint target, float speed,
                                                float lift) const
{
  switch (bodyOf(header.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return headerPaceBinturong(header, purpose, target, speed, lift);
  }
  return headerPaceAardvark(header, purpose, target, speed, lift);
}

PitchPoint MatchEngine::knockDownPoint(const MatchPlayer& header,
                                       PitchPoint aim)
{
  switch (bodyOf(header.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return knockDownPointBinturong(header, aim);
  }
  return knockDownPointAardvark(header, aim);
}

void MatchEngine::settleOnBall(MatchPlayer& player, float touchSkill,
                               bool hands)
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      settleOnBallBinturong(player, touchSkill, hands);
      return;
  }
  settleOnBallAardvark(player, touchSkill, hands);
}

float MatchEngine::keeperHoldSeconds(const MatchPlayer& keeper)
{
  switch (bodyOf(keeper.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return keeperHoldSecondsBinturong(keeper);
  }
  return keeperHoldSecondsAardvark(keeper);
}

bool MatchEngine::blocksShot(const MatchPlayer& player, float gap, float reach)
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return blocksShotBinturong(player, gap, reach);
  }
  return blocksShotAardvark(player, gap, reach);
}

bool MatchEngine::reachesBall(const MatchPlayer& player, float ballSpeed,
                              float stretch)
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return reachesBallBinturong(player, ballSpeed, stretch);
  }
  return reachesBallAardvark(player, ballSpeed, stretch);
}

float MatchEngine::touchSkill(const MatchPlayer& player, bool hands) const
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return touchSkillBinturong(player, hands);
  }
  return touchSkillAardvark(player, hands);
}

bool MatchEngine::controlsBall(const MatchPlayer& player, float skill,
                               float ballSpeed, float heightMetres)
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return controlsBallBinturong(player, skill, ballSpeed, heightMetres);
  }
  return controlsBallAardvark(player, skill, ballSpeed, heightMetres);
}

Vector2F MatchEngine::heavyTouchVelocity(const MatchPlayer& player,
                                         Vector2F velocity)
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return heavyTouchVelocityBinturong(player, velocity);
  }
  return heavyTouchVelocityAardvark(player, velocity);
}

bool MatchEngine::tryOwnGoalTouch(MatchPlayer& defender)
{
  switch (bodyOf(defender.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      // No roll: an own goal comes only from his header or touch going
      // wrong in the contact model (2026-10-26).
      return false;
  }
  return tryOwnGoalTouchAardvark(defender);
}

void MatchEngine::diveGoalkeeper(MatchPlayer& keeper, float dt)
{
  switch (bodyOf(keeper.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      diveGoalkeeperBinturong(keeper, dt);
      return;
  }
  diveGoalkeeperAardvark(keeper, dt);
}

void MatchEngine::readShot(const MatchPlayer& keeper,
                           GoalkeeperControl& control)
{
  switch (bodyOf(keeper.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      readShotBinturong(keeper, control);
      return;
  }
  readShotAardvark(keeper, control);
}

MatchEngine::SaveAttempt MatchEngine::attemptSave(const MatchPlayer& keeper)
{
  switch (bodyOf(keeper.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return attemptSaveBinturong(keeper);
  }
  return attemptSaveAardvark(keeper);
}

bool MatchEngine::holdsShot(const MatchPlayer& keeper, float stretch,
                            float speedExcess)
{
  switch (bodyOf(keeper.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return holdsShotBinturong(keeper, stretch, speedExcess);
  }
  return holdsShotAardvark(keeper, stretch, speedExcess);
}

MatchEngine::Spill MatchEngine::parrySpill(const MatchPlayer& keeper,
                                           float speed)
{
  switch (bodyOf(keeper.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return parrySpillBinturong(keeper, speed);
  }
  return parrySpillAardvark(keeper, speed);
}

// Contest halves: each goes to the body of the player it describes, so a
// duel between two sides can meet two different bodies.

float MatchEngine::takeOnMake(const MatchPlayer& carrier,
                              ScoreBreakdown* terms) const
{
  switch (bodyOf(carrier.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return takeOnMakeBinturong(carrier, terms);
  }
  return takeOnMakeAardvark(carrier, terms);
}

float MatchEngine::takeOnResist(const MatchPlayer& defender,
                                ScoreBreakdown* terms) const
{
  switch (bodyOf(defender.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return takeOnResistBinturong(defender, terms);
  }
  return takeOnResistAardvark(defender, terms);
}

float MatchEngine::tackleMake(const MatchPlayer& tackler,
                              ScoreBreakdown* terms) const
{
  switch (bodyOf(tackler.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return tackleMakeBinturong(tackler, terms);
  }
  return tackleMakeAardvark(tackler, terms);
}

float MatchEngine::tackleResist(const MatchPlayer& carrier, bool shielding,
                                ScoreBreakdown* terms) const
{
  switch (bodyOf(carrier.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return tackleResistBinturong(carrier, shielding, terms);
  }
  return tackleResistAardvark(carrier, shielding, terms);
}

float MatchEngine::tackleTechnique(const MatchPlayer& tackler,
                                   ScoreBreakdown* terms) const
{
  switch (bodyOf(tackler.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return tackleTechniqueBinturong(tackler, terms);
  }
  return tackleTechniqueAardvark(tackler, terms);
}

float MatchEngine::aerialStrength(const MatchPlayer& player,
                                  float ballHeightMetres) const
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return aerialStrengthBinturong(player, ballHeightMetres);
  }
  return aerialStrengthAardvark(player, ballHeightMetres);
}

float MatchEngine::claimChance(const MatchPlayer& keeper, int crowding) const
{
  switch (bodyOf(keeper.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return claimChanceBinturong(keeper, crowding);
  }
  return claimChanceAardvark(keeper, crowding);
}

void MatchEngine::recoverFromTakeOn(MatchPlayer& defender)
{
  switch (bodyOf(defender.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      recoverFromTakeOnBinturong(defender);
      return;
  }
  recoverFromTakeOnAardvark(defender);
}

bool MatchEngine::playsFirstTouch(const MatchPlayer& player, bool hands,
                                  float heightMetres) const
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return playsFirstTouchBinturong(player, hands, heightMetres);
  }
  return playsFirstTouchAardvark(player, hands, heightMetres);
}

float MatchEngine::feetReach(const MatchPlayer& player) const
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return feetReachBinturong(player);
  }
  return feetReachAardvark(player);
}

BodyLines MatchEngine::bodyLines(const MatchPlayer& player) const
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return bodyLinesBinturong(player);
  }
  return bodyLinesAardvark(player);
}

std::pair<float, float> MatchEngine::headLineRange() const
{
  // Aardvark bodies all have the fixed head line, the control ceiling, which
  // is also the answer with nobody on the pitch.
  if (!anyBinturongBody())
  {
    return {MatchTuning::Aerial::CONTROL_CEILING_METRES,
            MatchTuning::Aerial::CONTROL_CEILING_METRES};
  }
  float lowest = std::numeric_limits<float>::max();
  float highest = 0.0F;
  for (const MatchPlayer& player : players)
  {
    if (!MatchEngineInternal::active(player) || player.isGoalkeeper)
    {
      continue;
    }
    const float head = bodyLines(player).head;
    lowest = std::min(lowest, head);
    highest = std::max(highest, head);
  }
  if (highest <= 0.0F)
  {
    return {MatchTuning::Aerial::CONTROL_CEILING_METRES,
            MatchTuning::Aerial::CONTROL_CEILING_METRES};
  }
  return {lowest, highest};
}

float MatchEngine::lowestHeadLine() const { return headLineRange().first; }

bool MatchEngine::ballBehindReach(const MatchPlayer& player, float gapX,
                                  float gapY, float gap,
                                  float turnSeconds) const
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return ballBehindReachBinturong(player, gapX, gapY, gap, turnSeconds);
  }
  return ballBehindReachAardvark(player, gapX, gapY, gap, turnSeconds);
}

bool MatchEngine::ballInReach(const MatchPlayer& player) const
{
  const float gapX = player.position.x - ball.position.x;
  const float gapY = player.position.y - ball.position.y;
  const float gap = std::hypot(gapX, gapY);
  return gap <= feetReach(player) && !ballBehindReach(player, gapX, gapY, gap);
}

PitchPoint MatchEngine::strikePoint(const MatchPlayer& striker) const
{
  switch (bodyOf(striker.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return strikePointBinturong(striker);
  }
  return strikePointAardvark(striker);
}

MatchEngine::Kick MatchEngine::strikeAs(const MatchPlayer& striker,
                                        StrikeKind kind, PitchPoint target,
                                        float speed, float lift)
{
  switch (bodyOf(striker.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return strikeAsBinturong(striker, kind, target, speed, lift);
  }
  return strikeAsAardvark(striker, kind, target, speed, lift);
}

float MatchEngine::clearanceDistance(const MatchPlayer& defender,
                                     PitchPoint origin,
                                     const ClearanceAim& aim) const
{
  switch (bodyOf(defender.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return clearanceDistanceBinturong(defender, origin, aim);
  }
  return clearanceDistanceAardvark(defender, origin, aim);
}

void MatchEngine::takeDirectFreeKick(MatchPlayer& taker)
{
  switch (bodyOf(taker.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      takeDirectFreeKickBinturong(taker);
      return;
  }
  takeDirectFreeKickAardvark(taker);
}

MatchEngine::BallRelease MatchEngine::releaseFrom(const MatchPlayer& kicker,
                                                  ReleaseKind kind) const
{
  switch (bodyOf(kicker.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return releaseFromBinturong(kicker, kind);
  }
  return releaseFromAardvark(kicker, kind);
}

void MatchEngine::meetWithHead(const MatchPlayer& header)
{
  switch (bodyOf(header.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      meetWithHeadBinturong(header);
      return;
  }
  meetWithHeadAardvark(header);
}

bool MatchEngine::followsRoutes(const MatchPlayer& player) const
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::AARDVARK:
      break;
    case BodyId::BINTURONG:
      return followsRoutesBinturong(player);
  }
  return followsRoutesAardvark(player);
}
