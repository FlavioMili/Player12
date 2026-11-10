// Football Management Project — Copyright (c) 2025 - 2026 Flavio Milinanni.
// See LICENSE in the project root.
#include <stdexcept>

#include "model/match_engine.h"

bool MatchEngine::setPossessionVersion(PossessionId version)
{
  if (version != PossessionId::AARDVARK && version != PossessionId::BINTURONG)
    return false;
  if (stepCounter != 0 && version != possessionId) return false;
  possessionId = version;
  return true;
}

void MatchEngine::setPossession(MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return setPossessionAardvark(player);
    case PossessionId::BINTURONG:
      return setPossessionBinturong(player);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::takePossession(MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return takePossessionAardvark(player);
    case PossessionId::BINTURONG:
      return takePossessionBinturong(player);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::clearPossession()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return clearPossessionAardvark();
    case PossessionId::BINTURONG:
      return clearPossessionBinturong();
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::handBallTo(const Player* holder)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return handBallToAardvark(holder);
    case PossessionId::BINTURONG:
      return handBallToBinturong(holder);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::clearFlightState()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return clearFlightStateAardvark();
    case PossessionId::BINTURONG:
      return clearFlightStateBinturong();
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::updateTouch(MatchPlayer& carrier, float dt)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return updateTouchAardvark(carrier, dt);
    case PossessionId::BINTURONG:
      return updateTouchBinturong(carrier, dt);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteTouchOnBall(const MatchPlayer& player,
                                  const TouchIntent& intent,
                                  const TouchResult& touch,
                                  const TouchMoment& moment)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return noteTouchOnBallAardvark(player, intent, touch, moment);
    case PossessionId::BINTURONG:
      return noteTouchOnBallBinturong(player, intent, touch, moment);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginTouchClaim(const Player* passer, bool dropping)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return beginTouchClaimAardvark(passer, dropping);
    case PossessionId::BINTURONG:
      return beginTouchClaimBinturong(passer, dropping);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginCollectedDribble(const MatchPlayer& carrier)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return beginCollectedDribbleAardvark(carrier);
    case PossessionId::BINTURONG:
      return beginCollectedDribbleBinturong(carrier);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteDribbleTouch(const MatchPlayer& player,
                                   const TouchIntent& intent,
                                   const TouchResult& touch,
                                   const TouchMoment& moment)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return noteDribbleTouchAardvark(player, intent, touch, moment);
    case PossessionId::BINTURONG:
      return noteDribbleTouchBinturong(player, intent, touch, moment);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::recordPassCompletion(MatchPlayer& receiver)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return recordPassCompletionAardvark(receiver);
    case PossessionId::BINTURONG:
      return recordPassCompletionBinturong(receiver);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::creditPassCompletion(MatchPlayer& receiver,
                                       const Player* passerPlayer)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return creditPassCompletionAardvark(receiver, passerPlayer);
    case PossessionId::BINTURONG:
      return creditPassCompletionBinturong(receiver, passerPlayer);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::releasePossession(const MatchPlayer& kicker)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return releasePossessionAardvark(kicker);
    case PossessionId::BINTURONG:
      return releasePossessionBinturong(kicker);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginPassFlight(const MatchPlayer& passer,
                                  const PassOption& option)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return beginPassFlightAardvark(passer, option);
    case PossessionId::BINTURONG:
      return beginPassFlightBinturong(passer, option);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::finishPassFlight(bool aerialDelivery)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return finishPassFlightAardvark(aerialDelivery);
    case PossessionId::BINTURONG:
      return finishPassFlightBinturong(aerialDelivery);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteShotFlight(const MatchPlayer& shooter, bool onTarget,
                                 float xg, float targetY, float crossingZ,
                                 bool header, bool penalty)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return noteShotFlightAardvark(shooter, onTarget, xg, targetY, crossingZ,
                                    header, penalty);
    case PossessionId::BINTURONG:
      return noteShotFlightBinturong(shooter, onTarget, xg, targetY, crossingZ,
                                     header, penalty);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::consumeFirstTimeIntent(const MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return consumeFirstTimeIntentAardvark(player);
    case PossessionId::BINTURONG:
      return consumeFirstTimeIntentBinturong(player);
  }
  throw std::logic_error("Unknown possession implementation");
}

MatchEngine::PendingStrike MatchEngine::consumePendingStrike()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return consumePendingStrikeAardvark();
    case PossessionId::BINTURONG:
      return consumePendingStrikeBinturong();
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteFreeBallTackle(MatchPlayer& tackler, bool contest)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return noteFreeBallTackleAardvark(tackler, contest);
    case PossessionId::BINTURONG:
      return noteFreeBallTackleBinturong(tackler, contest);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::settleTackle(bool homeTeam)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return settleTackleAardvark(homeTeam);
    case PossessionId::BINTURONG:
      return settleTackleBinturong(homeTeam);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::trackHeaderRun()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return trackHeaderRunAardvark();
    case PossessionId::BINTURONG:
      return trackHeaderRunBinturong();
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::cancelPendingTackle()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return cancelPendingTackleAardvark();
    case PossessionId::BINTURONG:
      return cancelPendingTackleBinturong();
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteHeaderContact(const MatchPlayer& header)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return noteHeaderContactAardvark(header);
    case PossessionId::BINTURONG:
      return noteHeaderContactBinturong(header);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::creditHeaderShot(const MatchPlayer& header,
                                   const MatchPlayer* deliverer)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return creditHeaderShotAardvark(header, deliverer);
    case PossessionId::BINTURONG:
      return creditHeaderShotBinturong(header, deliverer);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginHeaderPlay(MatchPlayer& header, bool fromTeammate)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return beginHeaderPlayAardvark(header, fromTeammate);
    case PossessionId::BINTURONG:
      return beginHeaderPlayBinturong(header, fromTeammate);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteCushionedHeader(const MatchPlayer& header,
                                      const Player* passer,
                                      const TouchMoment& moment,
                                      float launchSpeed)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return noteCushionedHeaderAardvark(header, passer, moment, launchSpeed);
    case PossessionId::BINTURONG:
      return noteCushionedHeaderBinturong(header, passer, moment, launchSpeed);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteTackleNudge(const MatchPlayer& tackler,
                                  const MatchPlayer& attacker)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return noteTackleNudgeAardvark(tackler, attacker);
    case PossessionId::BINTURONG:
      return noteTackleNudgeBinturong(tackler, attacker);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginPokedTackle(const MatchPlayer& defender)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return beginPokedTackleAardvark(defender);
    case PossessionId::BINTURONG:
      return beginPokedTackleBinturong(defender);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::finishPokedTackle(const MatchPlayer& defender)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return finishPokedTackleAardvark(defender);
    case PossessionId::BINTURONG:
      return finishPokedTackleBinturong(defender);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::creditImmediateTackleWin(MatchPlayer& defender)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return creditImmediateTackleWinAardvark(defender);
    case PossessionId::BINTURONG:
      return creditImmediateTackleWinBinturong(defender);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::armTackleStrike(MatchPlayer& tackler,
                                  const std::optional<PassOption>& pass)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return armTackleStrikeAardvark(tackler, pass);
    case PossessionId::BINTURONG:
      return armTackleStrikeBinturong(tackler, pass);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::loseBallOnFall(MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return loseBallOnFallAardvark(player);
    case PossessionId::BINTURONG:
      return loseBallOnFallBinturong(player);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteFallenDeflection(const MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return noteFallenDeflectionAardvark(player);
    case PossessionId::BINTURONG:
      return noteFallenDeflectionBinturong(player);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteBodyDeflectionHit(MatchPlayer& player, ShotBodyPart part,
                                        bool legPutOut, bool opponents)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return noteBodyDeflectionHitAardvark(player, part, legPutOut, opponents);
    case PossessionId::BINTURONG:
      return noteBodyDeflectionHitBinturong(player, part, legPutOut, opponents);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginBodyDeflection(const MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return beginBodyDeflectionAardvark(player);
    case PossessionId::BINTURONG:
      return beginBodyDeflectionBinturong(player);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginShotDeflection(const MatchPlayer& player, float lockout)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return beginShotDeflectionAardvark(player, lockout);
    case PossessionId::BINTURONG:
      return beginShotDeflectionBinturong(player, lockout);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::endDeflectedShotFlight()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return endDeflectedShotFlightAardvark();
    case PossessionId::BINTURONG:
      return endDeflectedShotFlightBinturong();
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::finishShotDeflection(MatchPlayer& player, bool offBody,
                                       bool deflection)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return finishShotDeflectionAardvark(player, offBody, deflection);
    case PossessionId::BINTURONG:
      return finishShotDeflectionBinturong(player, offBody, deflection);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteFailedTouch(const MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return noteFailedTouchAardvark(player);
    case PossessionId::BINTURONG:
      return noteFailedTouchBinturong(player);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginKeeperHold(Point3F from)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return beginKeeperHoldAardvark(from);
    case PossessionId::BINTURONG:
      return beginKeeperHoldBinturong(from);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::rebaseKeeperHold(Point3F from)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return rebaseKeeperHoldAardvark(from);
    case PossessionId::BINTURONG:
      return rebaseKeeperHoldBinturong(from);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::endKeeperHold()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return endKeeperHoldAardvark();
    case PossessionId::BINTURONG:
      return endKeeperHoldBinturong();
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteKeeperDistribution(bool lofted, bool overarm)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return noteKeeperDistributionAardvark(lofted, overarm);
    case PossessionId::BINTURONG:
      return noteKeeperDistributionBinturong(lofted, overarm);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginKeeperHandRebound(const MatchPlayer& keeper)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return beginKeeperHandReboundAardvark(keeper);
    case PossessionId::BINTURONG:
      return beginKeeperHandReboundBinturong(keeper);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteKeeperShotResolved()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return noteKeeperShotResolvedAardvark();
    case PossessionId::BINTURONG:
      return noteKeeperShotResolvedBinturong();
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::finishKeeperParry(const MatchPlayer& keeper)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return finishKeeperParryAardvark(keeper);
    case PossessionId::BINTURONG:
      return finishKeeperParryBinturong(keeper);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::creditKeeperSave(MatchPlayer& keeper)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return creditKeeperSaveAardvark(keeper);
    case PossessionId::BINTURONG:
      return creditKeeperSaveBinturong(keeper);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::completeKeeperSave(const MatchPlayer& goalkeeper)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return completeKeeperSaveAardvark(goalkeeper);
    case PossessionId::BINTURONG:
      return completeKeeperSaveBinturong(goalkeeper);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginLegacyKeeperPunch(const MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return beginLegacyKeeperPunchAardvark(player);
    case PossessionId::BINTURONG:
      return beginLegacyKeeperPunchBinturong(player);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::finishLegacyKeeperPunch(const MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return finishLegacyKeeperPunchAardvark(player);
    case PossessionId::BINTURONG:
      return finishLegacyKeeperPunchBinturong(player);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::rearmShotAfterBounce(float targetY, float height)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return rearmShotAfterBounceAardvark(targetY, height);
    case PossessionId::BINTURONG:
      return rearmShotAfterBounceBinturong(targetY, height);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::endSpentKeeperShot()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return endSpentKeeperShotAardvark();
    case PossessionId::BINTURONG:
      return endSpentKeeperShotBinturong();
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::assignRestartOwner(const Player* owner)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return assignRestartOwnerAardvark(owner);
    case PossessionId::BINTURONG:
      return assignRestartOwnerBinturong(owner);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::markThrowEntry(bool entering)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return markThrowEntryAardvark(entering);
    case PossessionId::BINTURONG:
      return markThrowEntryBinturong(entering);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteWoodworkContact(bool shot)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return noteWoodworkContactAardvark(shot);
    case PossessionId::BINTURONG:
      return noteWoodworkContactBinturong(shot);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::markWallStoppedShot()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return markWallStoppedShotAardvark();
    case PossessionId::BINTURONG:
      return markWallStoppedShotBinturong();
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::creditWallBlocker(MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return creditWallBlockerAardvark(player);
    case PossessionId::BINTURONG:
      return creditWallBlockerBinturong(player);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteShotSetPieceOrigin(bool setPiece)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return noteShotSetPieceOriginAardvark(setPiece);
    case PossessionId::BINTURONG:
      return noteShotSetPieceOriginBinturong(setPiece);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteDirectFreeKickShot()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return noteDirectFreeKickShotAardvark();
    case PossessionId::BINTURONG:
      return noteDirectFreeKickShotBinturong();
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::resetShootoutBall()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return resetShootoutBallAardvark();
    case PossessionId::BINTURONG:
      return resetShootoutBallBinturong();
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::resetRuleContactAttempts()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return resetRuleContactAttemptsAardvark();
    case PossessionId::BINTURONG:
      return resetRuleContactAttemptsBinturong();
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::resetLifecycleBall()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return resetLifecycleBallAardvark();
    case PossessionId::BINTURONG:
      return resetLifecycleBallBinturong();
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::assignScenarioOwner(const MatchPlayer& carrier)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return assignScenarioOwnerAardvark(carrier);
    case PossessionId::BINTURONG:
      return assignScenarioOwnerBinturong(carrier);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginFedBall()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return beginFedBallAardvark();
    case PossessionId::BINTURONG:
      return beginFedBallBinturong();
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::finishFedBall(const MatchPlayer* receiver)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return finishFedBallAardvark(receiver);
    case PossessionId::BINTURONG:
      return finishFedBallBinturong(receiver);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::rememberPhaseTeam(bool homeTeam)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return rememberPhaseTeamAardvark(homeTeam);
    case PossessionId::BINTURONG:
      return rememberPhaseTeamBinturong(homeTeam);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteCarrierHistory(const MatchPlayer& carrier)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return noteCarrierHistoryAardvark(carrier);
    case PossessionId::BINTURONG:
      return noteCarrierHistoryBinturong(carrier);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::substitutePossession(bool hadPossession, PlayerID outgoingId,
                                       const Player* inPlayer)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return substitutePossessionAardvark(hadPossession, outgoingId, inPlayer);
    case PossessionId::BINTURONG:
      return substitutePossessionBinturong(hadPossession, outgoingId, inPlayer);
  }
  throw std::logic_error("Unknown possession implementation");
}

bool MatchEngine::releaseRemovedPlayer(const Player* player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return releaseRemovedPlayerAardvark(player);
    case PossessionId::BINTURONG:
      return releaseRemovedPlayerBinturong(player);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::clearShooterHistory()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return clearShooterHistoryAardvark();
    case PossessionId::BINTURONG:
      return clearShooterHistoryBinturong();
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::setPassFlightTarget(Vector2F target)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return setPassFlightTargetAardvark(target);
    case PossessionId::BINTURONG:
      return setPassFlightTargetBinturong(target);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteGoalSide(bool homeTeam)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return noteGoalSideAardvark(homeTeam);
    case PossessionId::BINTURONG:
      return noteGoalSideBinturong(homeTeam);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::prepareShotAssist(const MatchPlayer& shooter, bool header)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return prepareShotAssistAardvark(shooter, header);
    case PossessionId::BINTURONG:
      return prepareShotAssistBinturong(shooter, header);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteContactActor(const Player* player, float lockout,
                                   bool resetAttempts)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return noteContactActorAardvark(player, lockout, resetAttempts);
    case PossessionId::BINTURONG:
      return noteContactActorBinturong(player, lockout, resetAttempts);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::notePathChange()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return notePathChangeAardvark();
    case PossessionId::BINTURONG:
      return notePathChangeBinturong();
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::advanceShotFlightClock(float dt)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return advanceShotFlightClockAardvark(dt);
    case PossessionId::BINTURONG:
      return advanceShotFlightClockBinturong(dt);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::setBallMotion(const BallMotion& motion)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return setBallMotionAardvark(motion);
    case PossessionId::BINTURONG:
      return setBallMotionBinturong(motion);
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::endAerialDelivery()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return endAerialDeliveryAardvark();
    case PossessionId::BINTURONG:
      return endAerialDeliveryBinturong();
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteThrowTeam(bool homeTeam)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
      return noteThrowTeamAardvark(homeTeam);
    case PossessionId::BINTURONG:
      return noteThrowTeamBinturong(homeTeam);
  }
  throw std::logic_error("Unknown possession implementation");
}
