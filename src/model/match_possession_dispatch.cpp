// Football Management Project — Copyright (c) 2025 - 2026 Flavio Milinanni.
// See LICENSE in the project root.
#include <stdexcept>

#include "model/match_engine.h"

bool MatchEngine::setPossessionVersion(PossessionId version)
{
  if (version != PossessionId::AARDVARK && version != PossessionId::BINTURONG)
  {
    return false;
  }
  if (stepCounter != 0 && version != possessionId)
  {
    return false;
  }
  possessionId = version;
  return true;
}

void MatchEngine::setPossession(MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      setPossessionAardvark(player);
      return;
    }
    case PossessionId::BINTURONG:
    {
      setPossessionBinturong(player);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::takePossession(MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      takePossessionAardvark(player);
      return;
    }
    case PossessionId::BINTURONG:
    {
      takePossessionBinturong(player);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::clearPossession()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      clearPossessionAardvark();
      return;
    }
    case PossessionId::BINTURONG:
    {
      clearPossessionBinturong();
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::handBallTo(const Player* holder)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      handBallToAardvark(holder);
      return;
    }
    case PossessionId::BINTURONG:
    {
      handBallToBinturong(holder);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::clearFlightState()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      clearFlightStateAardvark();
      return;
    }
    case PossessionId::BINTURONG:
    {
      clearFlightStateBinturong();
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::updateTouch(MatchPlayer& carrier, float dt)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      updateTouchAardvark(carrier, dt);
      return;
    }
    case PossessionId::BINTURONG:
    {
      updateTouchBinturong(carrier, dt);
      return;
    }
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
    {
      noteTouchOnBallAardvark(player, intent, touch, moment);
      return;
    }
    case PossessionId::BINTURONG:
    {
      noteTouchOnBallBinturong(player, intent, touch, moment);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginTouchClaim(const Player* passer, bool dropping)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      beginTouchClaimAardvark(passer, dropping);
      return;
    }
    case PossessionId::BINTURONG:
    {
      beginTouchClaimBinturong(passer, dropping);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginCollectedDribble(const MatchPlayer& carrier)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      beginCollectedDribbleAardvark(carrier);
      return;
    }
    case PossessionId::BINTURONG:
    {
      beginCollectedDribbleBinturong(carrier);
      return;
    }
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
    {
      noteDribbleTouchAardvark(player, intent, touch, moment);
      return;
    }
    case PossessionId::BINTURONG:
    {
      noteDribbleTouchBinturong(player, intent, touch, moment);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::recordPassCompletion(MatchPlayer& receiver)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      recordPassCompletionAardvark(receiver);
      return;
    }
    case PossessionId::BINTURONG:
    {
      recordPassCompletionBinturong(receiver);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::creditPassCompletion(MatchPlayer& receiver,
                                       const Player* passerPlayer)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      creditPassCompletionAardvark(receiver, passerPlayer);
      return;
    }
    case PossessionId::BINTURONG:
    {
      creditPassCompletionBinturong(receiver, passerPlayer);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::releasePossession(const MatchPlayer& kicker)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      releasePossessionAardvark(kicker);
      return;
    }
    case PossessionId::BINTURONG:
    {
      releasePossessionBinturong(kicker);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginPassFlight(const MatchPlayer& passer,
                                  const PassOption& option)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      beginPassFlightAardvark(passer, option);
      return;
    }
    case PossessionId::BINTURONG:
    {
      beginPassFlightBinturong(passer, option);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::finishPassFlight(bool aerialDelivery)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      finishPassFlightAardvark(aerialDelivery);
      return;
    }
    case PossessionId::BINTURONG:
    {
      finishPassFlightBinturong(aerialDelivery);
      return;
    }
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
    {
      noteShotFlightAardvark(shooter, onTarget, xg, targetY, crossingZ, header,
                             penalty);
      return;
    }
    case PossessionId::BINTURONG:
    {
      noteShotFlightBinturong(shooter, onTarget, xg, targetY, crossingZ, header,
                              penalty);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::consumeFirstTimeIntent(const MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      consumeFirstTimeIntentAardvark(player);
      return;
    }
    case PossessionId::BINTURONG:
    {
      consumeFirstTimeIntentBinturong(player);
      return;
    }
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
    {
      noteFreeBallTackleAardvark(tackler, contest);
      return;
    }
    case PossessionId::BINTURONG:
    {
      noteFreeBallTackleBinturong(tackler, contest);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::settleTackle(bool homeTeam)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      settleTackleAardvark(homeTeam);
      return;
    }
    case PossessionId::BINTURONG:
    {
      settleTackleBinturong(homeTeam);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::trackHeaderRun()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      trackHeaderRunAardvark();
      return;
    }
    case PossessionId::BINTURONG:
    {
      trackHeaderRunBinturong();
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::cancelPendingTackle()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      cancelPendingTackleAardvark();
      return;
    }
    case PossessionId::BINTURONG:
    {
      cancelPendingTackleBinturong();
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteHeaderContact(const MatchPlayer& header)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      noteHeaderContactAardvark(header);
      return;
    }
    case PossessionId::BINTURONG:
    {
      noteHeaderContactBinturong(header);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::creditHeaderShot(const MatchPlayer& header,
                                   const MatchPlayer* deliverer)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      creditHeaderShotAardvark(header, deliverer);
      return;
    }
    case PossessionId::BINTURONG:
    {
      creditHeaderShotBinturong(header, deliverer);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginHeaderPlay(MatchPlayer& header, bool fromTeammate)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      beginHeaderPlayAardvark(header, fromTeammate);
      return;
    }
    case PossessionId::BINTURONG:
    {
      beginHeaderPlayBinturong(header, fromTeammate);
      return;
    }
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
    {
      noteCushionedHeaderAardvark(header, passer, moment, launchSpeed);
      return;
    }
    case PossessionId::BINTURONG:
    {
      noteCushionedHeaderBinturong(header, passer, moment, launchSpeed);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteTackleNudge(const MatchPlayer& tackler,
                                  const MatchPlayer& attacker)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      noteTackleNudgeAardvark(tackler, attacker);
      return;
    }
    case PossessionId::BINTURONG:
    {
      noteTackleNudgeBinturong(tackler, attacker);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginPokedTackle(const MatchPlayer& defender)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      beginPokedTackleAardvark(defender);
      return;
    }
    case PossessionId::BINTURONG:
    {
      beginPokedTackleBinturong(defender);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::finishPokedTackle(const MatchPlayer& defender)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      finishPokedTackleAardvark(defender);
      return;
    }
    case PossessionId::BINTURONG:
    {
      finishPokedTackleBinturong(defender);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::creditImmediateTackleWin(MatchPlayer& defender)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      creditImmediateTackleWinAardvark(defender);
      return;
    }
    case PossessionId::BINTURONG:
    {
      creditImmediateTackleWinBinturong(defender);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::armTackleStrike(MatchPlayer& tackler,
                                  const std::optional<PassOption>& pass)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      armTackleStrikeAardvark(tackler, pass);
      return;
    }
    case PossessionId::BINTURONG:
    {
      armTackleStrikeBinturong(tackler, pass);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::loseBallOnFall(MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      loseBallOnFallAardvark(player);
      return;
    }
    case PossessionId::BINTURONG:
    {
      loseBallOnFallBinturong(player);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteFallenDeflection(const MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      noteFallenDeflectionAardvark(player);
      return;
    }
    case PossessionId::BINTURONG:
    {
      noteFallenDeflectionBinturong(player);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteBodyDeflectionHit(MatchPlayer& player, ShotBodyPart part,
                                        bool legPutOut, bool opponents)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      noteBodyDeflectionHitAardvark(player, part, legPutOut, opponents);
      return;
    }
    case PossessionId::BINTURONG:
    {
      noteBodyDeflectionHitBinturong(player, part, legPutOut, opponents);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginBodyDeflection(const MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      beginBodyDeflectionAardvark(player);
      return;
    }
    case PossessionId::BINTURONG:
    {
      beginBodyDeflectionBinturong(player);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginShotDeflection(const MatchPlayer& player, float lockout)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      beginShotDeflectionAardvark(player, lockout);
      return;
    }
    case PossessionId::BINTURONG:
    {
      beginShotDeflectionBinturong(player, lockout);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::endDeflectedShotFlight()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      endDeflectedShotFlightAardvark();
      return;
    }
    case PossessionId::BINTURONG:
    {
      endDeflectedShotFlightBinturong();
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::finishShotDeflection(MatchPlayer& player, bool offBody,
                                       bool deflection)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      finishShotDeflectionAardvark(player, offBody, deflection);
      return;
    }
    case PossessionId::BINTURONG:
    {
      finishShotDeflectionBinturong(player, offBody, deflection);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteFailedTouch(const MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      noteFailedTouchAardvark(player);
      return;
    }
    case PossessionId::BINTURONG:
    {
      noteFailedTouchBinturong(player);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginKeeperHold(Point3F from)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      beginKeeperHoldAardvark(from);
      return;
    }
    case PossessionId::BINTURONG:
    {
      beginKeeperHoldBinturong(from);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::rebaseKeeperHold(Point3F from)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      rebaseKeeperHoldAardvark(from);
      return;
    }
    case PossessionId::BINTURONG:
    {
      rebaseKeeperHoldBinturong(from);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::endKeeperHold()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      endKeeperHoldAardvark();
      return;
    }
    case PossessionId::BINTURONG:
    {
      endKeeperHoldBinturong();
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteKeeperDistribution(bool lofted, bool overarm)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      noteKeeperDistributionAardvark(lofted, overarm);
      return;
    }
    case PossessionId::BINTURONG:
    {
      noteKeeperDistributionBinturong(lofted, overarm);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginKeeperHandRebound(const MatchPlayer& keeper)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      beginKeeperHandReboundAardvark(keeper);
      return;
    }
    case PossessionId::BINTURONG:
    {
      beginKeeperHandReboundBinturong(keeper);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteKeeperShotResolved()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      noteKeeperShotResolvedAardvark();
      return;
    }
    case PossessionId::BINTURONG:
    {
      noteKeeperShotResolvedBinturong();
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::finishKeeperParry(const MatchPlayer& keeper)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      finishKeeperParryAardvark(keeper);
      return;
    }
    case PossessionId::BINTURONG:
    {
      finishKeeperParryBinturong(keeper);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::creditKeeperSave(MatchPlayer& keeper)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      creditKeeperSaveAardvark(keeper);
      return;
    }
    case PossessionId::BINTURONG:
    {
      creditKeeperSaveBinturong(keeper);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::completeKeeperSave(const MatchPlayer& goalkeeper)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      completeKeeperSaveAardvark(goalkeeper);
      return;
    }
    case PossessionId::BINTURONG:
    {
      completeKeeperSaveBinturong(goalkeeper);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginLegacyKeeperPunch(const MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      beginLegacyKeeperPunchAardvark(player);
      return;
    }
    case PossessionId::BINTURONG:
    {
      beginLegacyKeeperPunchBinturong(player);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::finishLegacyKeeperPunch(const MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      finishLegacyKeeperPunchAardvark(player);
      return;
    }
    case PossessionId::BINTURONG:
    {
      finishLegacyKeeperPunchBinturong(player);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::rearmShotAfterBounce(float targetY, float height)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      rearmShotAfterBounceAardvark(targetY, height);
      return;
    }
    case PossessionId::BINTURONG:
    {
      rearmShotAfterBounceBinturong(targetY, height);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::endSpentKeeperShot()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      endSpentKeeperShotAardvark();
      return;
    }
    case PossessionId::BINTURONG:
    {
      endSpentKeeperShotBinturong();
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::assignRestartOwner(const Player* owner)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      assignRestartOwnerAardvark(owner);
      return;
    }
    case PossessionId::BINTURONG:
    {
      assignRestartOwnerBinturong(owner);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::markThrowEntry(bool entering)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      markThrowEntryAardvark(entering);
      return;
    }
    case PossessionId::BINTURONG:
    {
      markThrowEntryBinturong(entering);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteWoodworkContact(bool shot)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      noteWoodworkContactAardvark(shot);
      return;
    }
    case PossessionId::BINTURONG:
    {
      noteWoodworkContactBinturong(shot);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::markWallStoppedShot()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      markWallStoppedShotAardvark();
      return;
    }
    case PossessionId::BINTURONG:
    {
      markWallStoppedShotBinturong();
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::creditWallBlocker(MatchPlayer& player)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      creditWallBlockerAardvark(player);
      return;
    }
    case PossessionId::BINTURONG:
    {
      creditWallBlockerBinturong(player);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteShotSetPieceOrigin(bool setPiece)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      noteShotSetPieceOriginAardvark(setPiece);
      return;
    }
    case PossessionId::BINTURONG:
    {
      noteShotSetPieceOriginBinturong(setPiece);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteDirectFreeKickShot()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      noteDirectFreeKickShotAardvark();
      return;
    }
    case PossessionId::BINTURONG:
    {
      noteDirectFreeKickShotBinturong();
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::resetShootoutBall()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      resetShootoutBallAardvark();
      return;
    }
    case PossessionId::BINTURONG:
    {
      resetShootoutBallBinturong();
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::resetRuleContactAttempts()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      resetRuleContactAttemptsAardvark();
      return;
    }
    case PossessionId::BINTURONG:
    {
      resetRuleContactAttemptsBinturong();
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::resetLifecycleBall()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      resetLifecycleBallAardvark();
      return;
    }
    case PossessionId::BINTURONG:
    {
      resetLifecycleBallBinturong();
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::assignScenarioOwner(const MatchPlayer& carrier)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      assignScenarioOwnerAardvark(carrier);
      return;
    }
    case PossessionId::BINTURONG:
    {
      assignScenarioOwnerBinturong(carrier);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::beginFedBall()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      beginFedBallAardvark();
      return;
    }
    case PossessionId::BINTURONG:
    {
      beginFedBallBinturong();
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::finishFedBall(const MatchPlayer* receiver)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      finishFedBallAardvark(receiver);
      return;
    }
    case PossessionId::BINTURONG:
    {
      finishFedBallBinturong(receiver);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::rememberPhaseTeam(bool homeTeam)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      rememberPhaseTeamAardvark(homeTeam);
      return;
    }
    case PossessionId::BINTURONG:
    {
      rememberPhaseTeamBinturong(homeTeam);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteCarrierHistory(const MatchPlayer& carrier)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      noteCarrierHistoryAardvark(carrier);
      return;
    }
    case PossessionId::BINTURONG:
    {
      noteCarrierHistoryBinturong(carrier);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::substitutePossession(bool hadPossession, PlayerID outgoingId,
                                       const Player* inPlayer)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      substitutePossessionAardvark(hadPossession, outgoingId, inPlayer);
      return;
    }
    case PossessionId::BINTURONG:
    {
      substitutePossessionBinturong(hadPossession, outgoingId, inPlayer);
      return;
    }
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
    {
      clearShooterHistoryAardvark();
      return;
    }
    case PossessionId::BINTURONG:
    {
      clearShooterHistoryBinturong();
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::setPassFlightTarget(PitchPoint target)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      setPassFlightTargetAardvark(target);
      return;
    }
    case PossessionId::BINTURONG:
    {
      setPassFlightTargetBinturong(target);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteGoalSide(bool homeTeam)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      noteGoalSideAardvark(homeTeam);
      return;
    }
    case PossessionId::BINTURONG:
    {
      noteGoalSideBinturong(homeTeam);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::prepareShotAssist(const MatchPlayer& shooter, bool header)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      prepareShotAssistAardvark(shooter, header);
      return;
    }
    case PossessionId::BINTURONG:
    {
      prepareShotAssistBinturong(shooter, header);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteContactActor(const Player* player, float lockout,
                                   bool resetAttempts)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      noteContactActorAardvark(player, lockout, resetAttempts);
      return;
    }
    case PossessionId::BINTURONG:
    {
      noteContactActorBinturong(player, lockout, resetAttempts);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::notePathChange()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      notePathChangeAardvark();
      return;
    }
    case PossessionId::BINTURONG:
    {
      notePathChangeBinturong();
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::advanceShotFlightClock(float dt)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      advanceShotFlightClockAardvark(dt);
      return;
    }
    case PossessionId::BINTURONG:
    {
      advanceShotFlightClockBinturong(dt);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::setBallMotion(const BallMotion& motion)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      setBallMotionAardvark(motion);
      return;
    }
    case PossessionId::BINTURONG:
    {
      setBallMotionBinturong(motion);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::endAerialDelivery()
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      endAerialDeliveryAardvark();
      return;
    }
    case PossessionId::BINTURONG:
    {
      endAerialDeliveryBinturong();
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}

void MatchEngine::noteThrowTeam(bool homeTeam)
{
  switch (possessionId)
  {
    case PossessionId::AARDVARK:
    {
      noteThrowTeamAardvark(homeTeam);
      return;
    }
    case PossessionId::BINTURONG:
    {
      noteThrowTeamBinturong(homeTeam);
      return;
    }
  }
  throw std::logic_error("Unknown possession implementation");
}
