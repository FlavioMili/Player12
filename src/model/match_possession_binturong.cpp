// Football Management Project — Copyright (c) 2025 - 2026 Flavio Milinanni.
// See LICENSE in the project root.
// Developing possession implementation; initially identical to a95680a.
#include "model/match_engine.h"
#include "model/match_engine_internal.h"

using namespace MatchEngineInternal;

void MatchEngine::setPossessionBinturong(MatchPlayer& player)
{
  // Ownership, with the ball put still at his feet: the old trap, a keeper's
  // hands, a held restart. A contact that sends the ball on from where he met
  // it takes ownership alone (takePossession).
  settleTackle(player.isHomeTeam);
  ball.position = player.position;
  ball.z = 0.0f;
  ball.velocity = {0.0f, 0.0f};
  ball.velocityZ = 0.0f;
  takePossession(player);
}

void MatchEngine::takePossessionBinturong(MatchPlayer& player)
{
  // Central ownership transition: clear the old flight bookkeeping and update
  // the team transition context as well as the ball pointer. New reception or
  // interception code should use this path so phases and statistics agree.
  // The ball itself is not moved.
  trackTouch(player);
  ball.nudgedBy = nullptr;
  // A set piece's phase ends once the defending side has the ball.
  if (player.isHomeTeam != setPieceHome) setPiecePhaseRemaining = 0.0f;
  if (state == MatchState::PLAYING && lastControlledTeamHome &&
      *lastControlledTeamHome != player.isHomeTeam)
  {
    transitionSecondsRemaining =
        MatchTuning::Timing::POSSESSION_TRANSITION_SECONDS;
    lastCompletedPasser = nullptr;
    lastCompletedReceiver = nullptr;
  }
  lastControlledTeamHome = player.isHomeTeam;
  lastShooter = nullptr;
  if (ball.possessedBy != player.player)
  {
    possessionStartSeconds = getSimulatedSeconds();
    ++statsOf(player).touches;
  }
  handBallTo(player.player);
  ball.lastPossessor = player.player;
  clearFlightState();
  // The first touch brings the ball under close control in front of him.
  ball.dribbleExposure = 0.0f;
  ball.dribbleTouchLength = 0.0f;
  ball.touchTimer = 0.0f;
  const Vector2F facing{std::cos(player.facingAngle) * PITCH_LENGTH,
                        std::sin(player.facingAngle) * PITCH_WIDTH};
  ball.dribbleDirection = normalized(facing);
  if (length(ball.dribbleDirection) <= EPSILON)
    ball.dribbleDirection = {player.isHomeTeam ? 1.0f : -1.0f, 0.0f};
  updateTeamPhases();
  observePossession(PossessionObservation::ACQUIRE, PossessionId::BINTURONG);
}

void MatchEngine::clearPossessionBinturong()
{
  ball.inKeeperHands = false;
  ball.heldRelease = HeldRelease::NONE;
  ball.possessedBy = nullptr;
  ball.nudgedBy = nullptr;
  ball.inTouch = false;
  ball.touchCollected = false;
  ball.touchDropping = false;
  ball.touchPasser = nullptr;
  ball.touchWaiting = 0;
  observePossession(PossessionObservation::CLEAR_OWNER,
                    PossessionId::BINTURONG);
}

void MatchEngine::handBallToBinturong(const Player* holder)
{
  ball.inKeeperHands = false;
  ball.heldRelease = HeldRelease::NONE;
  ball.possessedBy = holder;
  ball.inTouch = false;
  ball.touchCollected = false;
  ball.touchDropping = false;
  ball.touchPasser = nullptr;
  ball.touchWaiting = 0;
  observePossession(PossessionObservation::ASSIGN_OWNER,
                    PossessionId::BINTURONG);
}

void MatchEngine::clearFlightStateBinturong()
{
  ball.intendedReceiver = nullptr;
  ball.isPass = false;
  ball.passWasOffside = false;
  ball.isShot = false;
  ball.shotOnTarget = false;
  ball.shotXG = 0.0f;
  ball.shotIsHeader = false;
  ball.shotIsPenalty = false;
  ball.shotFromSetPiece = false;
  ball.shotSaveResolved = false;
  ball.shotBlocked = false;
  ball.shotFromDirectFreeKick = false;
  ball.shotElapsedSeconds = 0.0f;
  ball.isAerialDelivery = false;
  ball.fromThrowIn = false;
  ball.throwEntering = false;
  ball.shotOffWoodwork = false;
  ball.spin = {};
  observePossession(PossessionObservation::CLEAR_FLIGHT,
                    PossessionId::BINTURONG);
}

void MatchEngine::updateTouchBinturong(MatchPlayer& carrier, float dt)
{
  using R = MatchTuning::Dribble;
  const float gap = distance(carrier.position, ball.position);
  const Vector2F relative{ball.velocity.x - carrier.velocity.x,
                          ball.velocity.y - carrier.velocity.y};
  ball.ownTouchCooldown = std::max(0.0f, ball.ownTouchCooldown - dt);
  ball.plannedTouchIn = std::max(0.0f, ball.plannedTouchIn - dt);

  if (!ball.touchCollected && gap <= R::KICK_REACH_METRES &&
      length(relative) <= touchKnobs[TouchKnob::COLLECT_RELATIVE_SPEED] &&
      (!ball.touchDropping ||
       ball.z * MatchTuning::Units::BALL_Z_METRES <= bodyLines(carrier).feet))
  {
    // Collected: the pass is complete and he can think about his next
    // action. The ball stays loose: from here he dribbles it, touch by touch.
    using F = MatchTuning::FirstTouch;
    ball.touchCollected = true;
    ball.touchDropping = false;
    ball.nudgedBy = nullptr;
    settleTackle(carrier.isHomeTeam);
    if (ball.touchPasser) creditPassCompletion(carrier, ball.touchPasser);
    ball.touchPasser = nullptr;
    carrier.actionCooldown =
        randomFloat(F::MIN_DECISION_DELAY, F::MAX_DECISION_DELAY);
  }

  // Defenders read how far it is from his foot. He keeps the claim however
  // far it runs: only an opponent playing it, or play stopping, ends it.
  ball.touchGap = gap;
  ball.touchPeakGap = std::max(ball.touchPeakGap, gap);
  ball.dribbleExposure = gap;
  ball.dribbleTouchLength = std::max(ball.touchPeakGap, R::MAX_TOUCH_METRES);
  observePossession(PossessionObservation::UPDATE_TOUCH,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteTouchOnBallBinturong(const MatchPlayer& player,
                                           const TouchIntent& intent,
                                           const TouchResult& touch,
                                           const TouchMoment& moment)
{
  ball.touchKind = intent.kind;
  ball.touchContactGap = moment.gap;
  ball.touchContactHeight = moment.height;
  ball.touchAngleError = touch.angleError;
  ball.touchWeightError = touch.weightError;
  ball.touchAngleDifficulty = touch.angleDifficulty;
  ball.touchWeightDifficulty = touch.weightDifficulty;
  ball.touchAngleFailed = touch.angleFailed;
  ball.touchWeightFailed = touch.weightFailed;
  ball.touchTurn = touch.turn;
  ball.touchOutward = touch.outward;
  ball.touchPaceChange = touch.paceChange;
  ball.touchLaunchSpeed = length(touch.velocity);
  const float gap = distance(player.position, ball.position);
  ball.touchGap = gap;
  ball.touchPeakGap = gap;
  ball.dribbleExposure = gap;
  observePossession(PossessionObservation::NOTE_TOUCH, PossessionId::BINTURONG);
}

void MatchEngine::beginTouchClaimBinturong(const Player* passer, bool dropping)
{
  ball.inTouch = true;
  ball.touchCollected = false;
  if (dropping) ball.touchDropping = true;
  ball.touchPasser = passer;
  observePossession(PossessionObservation::BEGIN_TOUCH_CLAIM,
                    PossessionId::BINTURONG);
}

void MatchEngine::beginCollectedDribbleBinturong(const MatchPlayer& carrier)
{
  using R = MatchTuning::Dribble;
  ball.inTouch = true;
  ball.touchCollected = true;
  ball.touchPasser = nullptr;
  ball.ownTouchCooldown = 0.0f;
  ball.dribbleGoal = carrier.tacticalTarget;
  ball.touchGap = R::FEET_METRES;
  ball.touchPeakGap = R::FEET_METRES;
  ball.dribbleExposure = R::FEET_METRES;
  ball.dribbleTouchLength = R::MAX_TOUCH_METRES;
  observePossession(PossessionObservation::BEGIN_COLLECTED_DRIBBLE,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteDribbleTouchBinturong(const MatchPlayer& player,
                                            const TouchIntent& intent,
                                            const TouchResult& touch,
                                            const TouchMoment& moment)
{
  // The quickest he can play it again: a Binturong body's by his
  // Dribbling (dribble meet plan 3b).
  ball.ownTouchCooldown = getBody(player.isHomeTeam) == BodyId::BINTURONG
                              ? dribbleTouchIntervalBinturong(player)
                              : MatchTuning::Dribble::LOOSE_TOUCH_INTERVAL;
  // His run to the next meet starts here; gone wrong, he collects it first.
  ball.meetLegStart = player.position;
  ball.meetRecovering = touch.angleFailed || touch.weightFailed;
  ball.knockedOn = intent.knockOn;
  // When he means to play it again, and the way he sent it (dribble meet
  // plan 3d).
  ball.plannedTouchIn = intent.plannedSeconds;
  {
    const Vector2F sent =
        toMetres({intent.targetSpot.x - moment.contactPoint.x,
                  intent.targetSpot.y - moment.contactPoint.y});
    const float sentMetres = length(sent);
    if (sentMetres > EPSILON)
      ball.pushDirection = {sent.x / sentMetres, sent.y / sentMetres};
  }
  noteTouchOnBall(player, intent, touch, moment);
  if (touch.angleFailed || touch.weightFailed) ++dribbleFailedTouches;
  const float speed = length(touch.velocity);
  if (speed > EPSILON)
    ball.dribbleDirection = {touch.velocity.x / speed,
                             touch.velocity.y / speed};
  observePossession(PossessionObservation::NOTE_DRIBBLE_TOUCH,
                    PossessionId::BINTURONG);
}

void MatchEngine::recordPassCompletionBinturong(MatchPlayer& receiver)
{
  observePossession(PossessionObservation::RECORD_PASS_COMPLETION,
                    PossessionId::BINTURONG);
  if (!ball.isPass) return;
  if (receiver.isHomeTeam == ball.passByHome)
  {
    creditPassCompletion(receiver, ball.lastPossessor);
    return;
  }
  ++statsOf(receiver).interceptions;
}

void MatchEngine::creditPassCompletionBinturong(MatchPlayer& receiver,
                                                const Player* passerPlayer)
{
  if (receiver.isHomeTeam)
    ++stats.homePassesCompleted;
  else
    ++stats.awayPassesCompleted;
  MatchPlayer* passer = findMatchPlayer(passerPlayer);
  if (passer && passer->isHomeTeam == receiver.isHomeTeam &&
      passer != &receiver)
  {
    ++statsOf(*passer).passesCompleted;
    trackPassCompletion(*passer, receiver);
    lastCompletedPasser = passer->player;
    lastCompletedReceiver = receiver.player;
  }
  observePossession(PossessionObservation::CREDIT_PASS_COMPLETION,
                    PossessionId::BINTURONG);
}

void MatchEngine::releasePossessionBinturong(const MatchPlayer& kicker)
{
  clearPossession();
  ball.lastPossessor = kicker.player;
  observePossession(PossessionObservation::RELEASE, PossessionId::BINTURONG);
}

void MatchEngine::beginPassFlightBinturong(const MatchPlayer& passer,
                                           const PassOption& option)
{
  MatchPlayer* const receiver = option.receiver;
  clearFlightState();
  ball.intendedReceiver = receiver ? receiver->player : nullptr;
  ball.isPass = true;
  ball.passByHome = passer.isHomeTeam;
  ball.passWasOffside =
      receiver && judgeOffside(passer, option, offsideLine(passer.isHomeTeam));
  observePossession(PossessionObservation::BEGIN_PASS_FLIGHT,
                    PossessionId::BINTURONG);
}

void MatchEngine::finishPassFlightBinturong(bool aerialDelivery)
{
  ball.isAerialDelivery = aerialDelivery;
  ball.fromThrowIn = state == MatchState::THROW_IN;
  observePossession(PossessionObservation::FINISH_PASS_FLIGHT,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteShotFlightBinturong(const MatchPlayer& shooter,
                                          bool onTarget, float xg,
                                          float targetY, float crossingZ,
                                          bool header, bool penalty)
{
  ball.isShot = true;
  ball.shotByHome = shooter.isHomeTeam;
  ball.shotOnTarget = onTarget;
  ball.shotXG = xg;
  ball.shotTargetY = targetY;
  ball.shotTargetHeightMetres = crossingZ;
  ball.shotElapsedSeconds = 0.0f;
  ball.shotIsHeader = header;
  ball.shotIsPenalty = penalty;
  ball.shotSaveResolved = false;
  ball.shotBlocked = false;
  lastShooter = shooter.player;
  observePossession(PossessionObservation::NOTE_SHOT_FLIGHT,
                    PossessionId::BINTURONG);
}

void MatchEngine::consumeFirstTimeIntentBinturong(const MatchPlayer& player)
{
  if (player.player && forcedPass.passer == player.player->getId())
    forcedPass = {};
  if (player.player && forcedShooter == player.player->getId())
  {
    forcedShooter = 0;
    forcedShotFirstTime = false;
    forcedShotAim.reset();
  }
  observePossession(PossessionObservation::CONSUME_FIRST_TIME_INTENT,
                    PossessionId::BINTURONG);
}

MatchEngine::PendingStrike MatchEngine::consumePendingStrikeBinturong()
{
  const PendingStrike strike = *pendingStrike;
  pendingStrike.reset();
  observePossession(PossessionObservation::CONSUME_PENDING_STRIKE,
                    PossessionId::BINTURONG);
  return strike;
}

void MatchEngine::noteFreeBallTackleBinturong(MatchPlayer& tackler,
                                              bool contest)
{
  ++(tackler.isHomeTeam ? stats.homeTackleAttempts : stats.awayTackleAttempts);
  if (contest)
    ++(tackler.isHomeTeam ? stats.homeContestTackles
                          : stats.awayContestTackles);
  else
    ++(tackler.isHomeTeam ? stats.homeFreeBallTackles
                          : stats.awayFreeBallTackles);
  ++statsOf(tackler).tacklesAttempted;
  pendingTackle = PendingTackle{tackler.player, tackler.isHomeTeam, contest};
  observePossession(PossessionObservation::NOTE_FREE_BALL_TACKLE,
                    PossessionId::BINTURONG);
}

void MatchEngine::settleTackleBinturong(bool homeTeam)
{
  observePossession(PossessionObservation::SETTLE_TACKLE,
                    PossessionId::BINTURONG);
  if (!pendingTackle) return;
  const PendingTackle tackle = *pendingTackle;
  pendingTackle.reset();
  if (tackle.homeTeam != homeTeam) return;
  ++(homeTeam ? stats.homeTackles : stats.awayTackles);
  if (tackle.contest)
    ++(homeTeam ? stats.homeContestTacklesWon : stats.awayContestTacklesWon);
  else
    ++(homeTeam ? stats.homeFreeBallTacklesWon : stats.awayFreeBallTacklesWon);
  if (MatchPlayer* tackler = findMatchPlayer(tackle.tackler))
    ++statsOf(*tackler).tacklesWon;
}

void MatchEngine::trackHeaderRunBinturong()
{
  observePossession(PossessionObservation::TRACK_HEADER_RUN,
                    PossessionId::BINTURONG);
  // Over once someone has it (a touch he owns, or in his hands) or play has
  // stopped; a ball still loose (in the air or not) keeps it going.
  const bool stopped = state != MatchState::PLAYING;
  if (!stopped && !ball.possessedBy) return;
  ++stats.headerSequences;
  stats.headerSequenceHeaders += headerRun;
  if (headerRun >= 3) ++stats.headerSequencesLong;
  stats.headerSequenceLongest =
      std::max(stats.headerSequenceLongest, headerRun);
  if (!stopped)
  {
    ++stats.headerSequencesToControl;
    if (const MatchPlayer* holder = findMatchPlayer(ball.possessedBy);
        holder && holder->isHomeTeam == headerRunLastHome)
      ++stats.headerSequencesKept;
  }
  headerRun = 0;
}

void MatchEngine::cancelPendingTackleBinturong()
{
  pendingTackle.reset();
  observePossession(PossessionObservation::CANCEL_PENDING_TACKLE,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteHeaderContactBinturong(const MatchPlayer& header)
{
  trackTouch(header);
  ++headerRun;
  headerRunLastHome = header.isHomeTeam;
  observePossession(PossessionObservation::NOTE_HEADER_CONTACT,
                    PossessionId::BINTURONG);
}

void MatchEngine::creditHeaderShotBinturong(const MatchPlayer& header,
                                            const MatchPlayer* deliverer)
{
  if (deliverer && deliverer->isHomeTeam == header.isHomeTeam &&
      deliverer != &header)
  {
    ++statsOf(*deliverer).passesCompleted;
    trackPassCompletion(*deliverer, header);
    if (header.isHomeTeam)
      ++stats.homePassesCompleted;
    else
      ++stats.awayPassesCompleted;
    shotAssistCandidate = deliverer->player;
  }
  else
  {
    shotAssistCandidate = nullptr;
  }
  observePossession(PossessionObservation::CREDIT_HEADER_SHOT,
                    PossessionId::BINTURONG);
}

void MatchEngine::beginHeaderPlayBinturong(MatchPlayer& header,
                                           bool fromTeammate)
{
  if (fromTeammate)
    recordPassCompletion(header);
  else if (ball.isPass)
    ++statsOf(header).interceptions;
  clearFlightState();
  ball.lastPossessor = header.player;
  lastShooter = nullptr;
  observePossession(PossessionObservation::BEGIN_HEADER_PLAY,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteCushionedHeaderBinturong(const MatchPlayer& header,
                                               const Player* passer,
                                               const TouchMoment& moment,
                                               float launchSpeed)
{
  beginTouchClaim(passer, true);
  ball.touchContactGap = moment.gap;
  ball.touchContactHeight = moment.height;
  ball.touchLaunchSpeed = launchSpeed;
  const float gap = distance(header.position, ball.position);
  ball.touchGap = gap;
  ball.touchPeakGap = gap;
  ball.dribbleExposure = gap;
  ball.dribbleTouchLength =
      std::max(gap, MatchTuning::Dribble::MAX_TOUCH_METRES);
  observePossession(PossessionObservation::NOTE_CUSHIONED_HEADER,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteTackleNudgeBinturong(const MatchPlayer& tackler,
                                           const MatchPlayer& attacker)
{
  ball.nudgedBy = tackler.player;
  ball.nudgedFrom = attacker.player;
  observePossession(PossessionObservation::NOTE_TACKLE_NUDGE,
                    PossessionId::BINTURONG);
}

void MatchEngine::beginPokedTackleBinturong(const MatchPlayer& defender)
{
  clearPossession();
  ball.lastPossessor = defender.player;
  clearFlightState();
  observePossession(PossessionObservation::BEGIN_POKED_TACKLE,
                    PossessionId::BINTURONG);
}

void MatchEngine::finishPokedTackleBinturong(const MatchPlayer& defender)
{
  lastControlledTeamHome = defender.isHomeTeam;
  transitionSecondsRemaining =
      MatchTuning::Timing::POSSESSION_TRANSITION_SECONDS;
  updateTeamPhases();
  observePossession(PossessionObservation::FINISH_POKED_TACKLE,
                    PossessionId::BINTURONG);
}

void MatchEngine::creditImmediateTackleWinBinturong(MatchPlayer& defender)
{
  if (defender.isHomeTeam)
    ++stats.homeTackles;
  else
    ++stats.awayTackles;
  ++statsOf(defender).tacklesWon;
  observePossession(PossessionObservation::CREDIT_IMMEDIATE_TACKLE_WIN,
                    PossessionId::BINTURONG);
}

void MatchEngine::armTackleStrikeBinturong(
    MatchPlayer& tackler, const std::optional<PassOption>& pass)
{
  PendingStrike strike;
  strike.kind = pass ? PendingStrike::Kind::PASS : PendingStrike::Kind::CLEAR;
  strike.striker = &tackler;
  if (pass) strike.pass = *pass;
  pendingStrike = strike;
  notePendingStrikeSlots();
  observePossession(PossessionObservation::ARM_TACKLE_STRIKE,
                    PossessionId::BINTURONG);
}

void MatchEngine::loseBallOnFallBinturong(MatchPlayer& player)
{
  // He can't play it from the floor: a ball he had runs on loose (a
  // keeper's in his hands he keeps hold of).
  if (player.player && ball.possessedBy == player.player &&
      !(player.isGoalkeeper && !ball.inTouch))
  {
    clearPossession();
    ++stats.collisionFallsLosingBall;
  }
  if (pendingStrike && pendingStrike->striker == &player) pendingStrike.reset();
  if (ball.meetFor == player.player) ball.meetFor = nullptr;
  observePossession(PossessionObservation::LOSE_BALL_ON_FALL,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteFallenDeflectionBinturong(const MatchPlayer& player)
{
  // His last touch for restarts; not a pass, possession or a deliberate play.
  notePathChange();
  ball.deflectedBy = player.player;
  ball.deflectedPathSince = ball.pathSince;
  ball.deflectedAfter = ball.lastPossessor;
  ball.touchAttempts = 0;
  ++stats.collisionBallOffFallen;
  observePossession(PossessionObservation::NOTE_FALLEN_DEFLECTION,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteBodyDeflectionHitBinturong(MatchPlayer& player,
                                                 ShotBodyPart part,
                                                 bool legPutOut, bool opponents)
{
  ++stats.ballBodyHitsByPart[static_cast<std::size_t>(part)];
  if (legPutOut) ++stats.ballReactionBlocks;
  if (!opponents)
    ++stats.ballDeflections;
  else if (ball.isPass)
  {
    ++stats.passesBlocked;
    ++statsOf(player).blockedPasses;
  }
  observePossession(PossessionObservation::NOTE_BODY_DEFLECTION_HIT,
                    PossessionId::BINTURONG);
}

void MatchEngine::beginBodyDeflectionBinturong(const MatchPlayer& player)
{
  if (ball.inTouch) clearPossession();
  clearFlightState();
  ball.lastPossessor = player.player;
  ball.kicker = player.player;
  observePossession(PossessionObservation::BEGIN_BODY_DEFLECTION,
                    PossessionId::BINTURONG);
}

void MatchEngine::beginShotDeflectionBinturong(const MatchPlayer& player,
                                               float lockout)
{
  ball.lastPossessor = player.player;
  ball.kicker = player.player;
  // Afterwards (shot block plan step 4): off his torso or stride just
  // the ball coming off him; off a leg he put out, until it is back.
  ball.kickerLockout = lockout;
  ball.touchAttempts = 0;
  ball.fromThrowIn = false;
  notePathChange();
  observePossession(PossessionObservation::BEGIN_SHOT_DEFLECTION,
                    PossessionId::BINTURONG);
}

void MatchEngine::endDeflectedShotFlightBinturong()
{
  ball.isShot = false;
  ball.shotOnTarget = false;
  observePossession(PossessionObservation::END_DEFLECTED_SHOT_FLIGHT,
                    PossessionId::BINTURONG);
}

void MatchEngine::finishShotDeflectionBinturong(MatchPlayer& player,
                                                bool offBody, bool deflection)
{
  observePossession(PossessionObservation::FINISH_SHOT_DEFLECTION,
                    PossessionId::BINTURONG);
  if (offBody && deflection)
  {
    ++stats.shotDeflections;
    return;
  }
  ball.shotBlocked = true;
  if (ball.shotFromDirectFreeKick) ++stats.directFreeKicksBlocked;
  ++(offBody ? statsOf(player).blockedShots : statsOf(player).clearances);
  logEvent(MatchEventType::SHOT_BLOCKED, player);
  return;
}

void MatchEngine::noteFailedTouchBinturong(const MatchPlayer& player)
{
  ball.lastPossessor = player.player;
  ball.kicker = player.player;
  ball.kickerLockout = MatchTuning::Ball::FAILED_TOUCH_LOCKOUT;
  ball.touchAttempts = 0;
  observePossession(PossessionObservation::NOTE_FAILED_TOUCH,
                    PossessionId::BINTURONG);
}

void MatchEngine::beginKeeperHoldBinturong(Point3F from)
{
  ball.inKeeperHands = true;
  ball.heldFrom = from;
  ball.heldSince = getSimulatedSeconds();
  observePossession(PossessionObservation::BEGIN_KEEPER_HOLD,
                    PossessionId::BINTURONG);
}

void MatchEngine::rebaseKeeperHoldBinturong(Point3F from)
{
  ball.heldFrom = from;
  ball.heldSince = getSimulatedSeconds();
  observePossession(PossessionObservation::REBASE_KEEPER_HOLD,
                    PossessionId::BINTURONG);
}

void MatchEngine::endKeeperHoldBinturong()
{
  ball.inKeeperHands = false;
  observePossession(PossessionObservation::END_KEEPER_HOLD,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteKeeperDistributionBinturong(bool lofted, bool overarm)
{
  ball.heldRelease = lofted    ? HeldRelease::PUNT
                     : overarm ? HeldRelease::OVERARM
                               : HeldRelease::ROLL;
  ++(lofted    ? stats.keeperPunts
     : overarm ? stats.keeperThrows
               : stats.keeperRolls);
  observePossession(PossessionObservation::NOTE_KEEPER_DISTRIBUTION,
                    PossessionId::BINTURONG);
}

void MatchEngine::beginKeeperHandReboundBinturong(const MatchPlayer& keeper)
{
  if (ball.inTouch) clearPossession();
  clearFlightState();
  ball.lastPossessor = keeper.player;
  ball.kicker = keeper.player;
  ball.kickerLockout = MatchTuning::ShotBlock::RECOVERY_BODY_SECONDS;
  ball.touchAttempts = 0;
  notePathChange();
  observePossession(PossessionObservation::BEGIN_KEEPER_HAND_REBOUND,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteKeeperShotResolvedBinturong()
{
  ball.shotSaveResolved = true;
  observePossession(PossessionObservation::NOTE_KEEPER_SHOT_RESOLVED,
                    PossessionId::BINTURONG);
}

void MatchEngine::finishKeeperParryBinturong(const MatchPlayer& keeper)
{
  ball.isShot = false;
  ball.shotOnTarget = false;
  ball.lastPossessor = keeper.player;
  observePossession(PossessionObservation::FINISH_KEEPER_PARRY,
                    PossessionId::BINTURONG);
}

void MatchEngine::creditKeeperSaveBinturong(MatchPlayer& keeper)
{
  MatchPlayer* shooter = findMatchPlayer(lastShooter);
  if (keeper.isHomeTeam)
  {
    ++stats.homeSaves;
    ++stats.awayOnTarget;
  }
  else
  {
    ++stats.awaySaves;
    ++stats.homeOnTarget;
  }
  ++statsOf(keeper).saves;
  if (shooter) ++statsOf(*shooter).shotsOnTarget;
  observePossession(PossessionObservation::CREDIT_KEEPER_SAVE,
                    PossessionId::BINTURONG);
}

void MatchEngine::completeKeeperSaveBinturong(const MatchPlayer& goalkeeper)
{
  state = MatchState::PLAYING;
  keepers[goalkeeper.isHomeTeam ? 0 : 1].state = GoalkeeperState::HOLD;
  updateTeamPhases();
  observePossession(PossessionObservation::COMPLETE_KEEPER_SAVE,
                    PossessionId::BINTURONG);
}

void MatchEngine::beginLegacyKeeperPunchBinturong(const MatchPlayer& player)
{
  ball.lastPossessor = player.player;
  clearFlightState();
  notePathChange();
  observePossession(PossessionObservation::BEGIN_LEGACY_KEEPER_PUNCH,
                    PossessionId::BINTURONG);
}

void MatchEngine::finishLegacyKeeperPunchBinturong(const MatchPlayer& player)
{
  ball.kicker = player.player;
  ball.kickerLockout = MatchTuning::Ball::FAILED_TOUCH_LOCKOUT;
  ball.touchAttempts = 0;
  observePossession(PossessionObservation::FINISH_LEGACY_KEEPER_PUNCH,
                    PossessionId::BINTURONG);
}

void MatchEngine::rearmShotAfterBounceBinturong(float targetY, float height)
{
  ball.shotOnTarget = true;
  ball.shotSaveResolved = false;
  ball.shotTargetY = targetY;
  ball.shotTargetHeightMetres = height;
  observePossession(PossessionObservation::REARM_SHOT_AFTER_BOUNCE,
                    PossessionId::BINTURONG);
}

void MatchEngine::endSpentKeeperShotBinturong()
{
  ball.isShot = false;
  ball.shotOnTarget = false;
  ball.touchAttempts = 0;
  observePossession(PossessionObservation::END_SPENT_KEEPER_SHOT,
                    PossessionId::BINTURONG);
}

void MatchEngine::assignRestartOwnerBinturong(const Player* owner)
{
  handBallTo(owner);
  ball.lastPossessor = ball.possessedBy;
  observePossession(PossessionObservation::ASSIGN_RESTART_OWNER,
                    PossessionId::BINTURONG);
}

void MatchEngine::markThrowEntryBinturong(bool entering)
{
  ball.throwEntering = entering;
  observePossession(PossessionObservation::MARK_THROW_ENTRY,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteWoodworkContactBinturong(bool shot)
{
  notePathChange();
  ball.touchAttempts = 0;
  if (shot)
  {
    ball.shotOffWoodwork = true;
    ball.woodworkPathSince = ball.pathSince;
    ball.woodworkLastPossessor = ball.lastPossessor;
    ball.woodworkDeflectedBy = ball.deflectedBy;
  }
  observePossession(PossessionObservation::NOTE_WOODWORK_CONTACT,
                    PossessionId::BINTURONG);
}

void MatchEngine::markWallStoppedShotBinturong()
{
  ball.isShot = false;
  ball.shotOnTarget = false;
  ball.shotSaveResolved = true;
  observePossession(PossessionObservation::MARK_WALL_STOPPED_SHOT,
                    PossessionId::BINTURONG);
}

void MatchEngine::creditWallBlockerBinturong(MatchPlayer& player)
{
  ball.lastPossessor = player.player;
  ball.kicker = player.player;
  ball.kickerLockout = MatchTuning::Passing::KICKER_LOCKOUT_SECONDS;
  ++statsOf(player).clearances;
  logEvent(MatchEventType::SHOT_BLOCKED, player).detail =
      MatchEventDetail::WALL;
  observePossession(PossessionObservation::CREDIT_WALL_BLOCKER,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteShotSetPieceOriginBinturong(bool setPiece)
{
  ball.shotFromSetPiece = setPiece;
  observePossession(PossessionObservation::NOTE_SHOT_SET_PIECE_ORIGIN,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteDirectFreeKickShotBinturong()
{
  ball.shotFromDirectFreeKick = true;
  observePossession(PossessionObservation::NOTE_DIRECT_FREE_KICK_SHOT,
                    PossessionId::BINTURONG);
}

void MatchEngine::resetShootoutBallBinturong()
{
  ball = MatchBall{};
  observePossession(PossessionObservation::RESET_SHOOTOUT_BALL,
                    PossessionId::BINTURONG);
}

void MatchEngine::resetRuleContactAttemptsBinturong()
{
  ball.touchAttempts = 0;
  observePossession(PossessionObservation::RESET_RULE_CONTACT_ATTEMPTS,
                    PossessionId::BINTURONG);
}

void MatchEngine::resetLifecycleBallBinturong()
{
  ball = MatchBall{};
  observePossession(PossessionObservation::RESET_LIFECYCLE_BALL,
                    PossessionId::BINTURONG);
}

void MatchEngine::assignScenarioOwnerBinturong(const MatchPlayer& carrier)
{
  handBallTo(carrier.player);
  ball.lastPossessor = carrier.player;
  if (carrier.isHomeTeam)
  {
    homePhase = TeamPhase::POSSESSION;
    awayPhase = TeamPhase::DEFENSIVE_BLOCK;
  }
  else
  {
    awayPhase = TeamPhase::POSSESSION;
    homePhase = TeamPhase::DEFENSIVE_BLOCK;
  }
  observePossession(PossessionObservation::ASSIGN_SCENARIO_OWNER,
                    PossessionId::BINTURONG);
}

void MatchEngine::beginFedBallBinturong()
{
  clearPossession();
  clearFlightState();
  observePossession(PossessionObservation::BEGIN_FED_BALL,
                    PossessionId::BINTURONG);
}

void MatchEngine::finishFedBallBinturong(const MatchPlayer* receiver)
{
  ball.lastPossessor = nullptr;
  ball.kicker = nullptr;
  ball.kickerLockout = 0.0f;
  ball.touchAttempts = 0;
  if (receiver)
  {
    ball.isPass = true;
    ball.intendedReceiver = receiver->player;
    ball.passByHome = receiver->isHomeTeam;
    ball.passTarget = receiver->position;
  }
  observePossession(PossessionObservation::FINISH_FED_BALL,
                    PossessionId::BINTURONG);
}

void MatchEngine::rememberPhaseTeamBinturong(bool homeTeam)
{
  lastControlledTeamHome = homeTeam;
  observePossession(PossessionObservation::REMEMBER_PHASE_TEAM,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteCarrierHistoryBinturong(const MatchPlayer& carrier)
{
  ball.lastPossessor = carrier.player;
  observePossession(PossessionObservation::NOTE_CARRIER_HISTORY,
                    PossessionId::BINTURONG);
}

void MatchEngine::substitutePossessionBinturong(bool hadPossession,
                                                PlayerID outgoingId,
                                                const Player* inPlayer)
{
  if (hadPossession) handBallTo(inPlayer);
  if (ball.lastPossessor && ball.lastPossessor->getId() == outgoingId)
    ball.lastPossessor = inPlayer;
  observePossession(PossessionObservation::SUBSTITUTE_POSSESSION,
                    PossessionId::BINTURONG);
}

bool MatchEngine::releaseRemovedPlayerBinturong(const Player* player)
{
  const bool owned = ball.possessedBy == player;
  if (owned) clearPossession();
  observePossession(PossessionObservation::RELEASE_REMOVED_PLAYER,
                    PossessionId::BINTURONG);
  return owned;
}

void MatchEngine::clearShooterHistoryBinturong()
{
  lastShooter = nullptr;
  observePossession(PossessionObservation::CLEAR_SHOOTER_HISTORY,
                    PossessionId::BINTURONG);
}

void MatchEngine::setPassFlightTargetBinturong(Vector2F target)
{
  ball.passTarget = {std::clamp(target.x, 0.0f, 1.0f),
                     std::clamp(target.y, 0.0f, 1.0f)};
  observePossession(PossessionObservation::SET_PASS_FLIGHT_TARGET,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteGoalSideBinturong(bool homeTeam)
{
  ball.shotByHome = homeTeam;
  observePossession(PossessionObservation::NOTE_GOAL_SIDE,
                    PossessionId::BINTURONG);
}

void MatchEngine::prepareShotAssistBinturong(const MatchPlayer& shooter,
                                             bool header)
{
  if (!header)
  {
    shotAssistCandidate =
        lastCompletedReceiver == shooter.player ? lastCompletedPasser : nullptr;
  }
  observePossession(PossessionObservation::PREPARE_SHOT_ASSIST,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteContactActorBinturong(const Player* player, float lockout,
                                            bool resetAttempts)
{
  ball.kicker = player;
  ball.kickerLockout = lockout;
  if (resetAttempts) ball.touchAttempts = 0;
  observePossession(PossessionObservation::NOTE_CONTACT_ACTOR,
                    PossessionId::BINTURONG);
}

void MatchEngine::notePathChangeBinturong()
{
  ball.pathSince = ballNow();
  ball.commitMisses = 0;
  observePossession(PossessionObservation::NOTE_PATH_CHANGE,
                    PossessionId::BINTURONG);
}

void MatchEngine::advanceShotFlightClockBinturong(float dt)
{
  ball.shotElapsedSeconds += dt;
  observePossession(PossessionObservation::ADVANCE_SHOT_FLIGHT_CLOCK,
                    PossessionId::BINTURONG);
}

void MatchEngine::setBallMotionBinturong(const BallMotion& motion)
{
  ball.position = motion.position;
  ball.z = motion.z;
  ball.velocity = motion.velocity;
  ball.velocityZ = motion.velocityZ;
  ball.spin = motion.spin;
  ball.isAerialDelivery = motion.isAerialDelivery;
}

void MatchEngine::endAerialDeliveryBinturong()
{
  ball.isAerialDelivery = false;
}

void MatchEngine::noteThrowTeamBinturong(bool homeTeam)
{
  ball.throwByHome = homeTeam;
  observePossession(PossessionObservation::NOTE_THROW_TEAM,
                    PossessionId::BINTURONG);
}
