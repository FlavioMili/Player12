// Football Management Project — Copyright (c) 2025 - 2026 Flavio Milinanni.
// See LICENSE in the project root.
// Frozen at a95680a; do not tune this implementation.
#include "model/match_engine.h"
#include "model/match_engine_internal.h"

using namespace MatchEngineInternal;

void MatchEngine::setPossessionAardvark(MatchPlayer& player)
{
  // Ownership, with the ball put still at his feet: the old trap, a keeper's
  // hands, a held restart. A contact that sends the ball on from where he met
  // it takes ownership alone (takePossession).
  settleTackle(player.isHomeTeam);
  ball.position = player.position;
  ball.z = 0.0F;
  ball.velocity = {.x = 0.0F, .y = 0.0F};
  ball.velocityZ = 0.0F;
  takePossession(player);
}

void MatchEngine::takePossessionAardvark(MatchPlayer& player)
{
  // Central ownership transition: clear the old flight bookkeeping and update
  // the team transition context as well as the ball pointer. New reception or
  // interception code should use this path so phases and statistics agree.
  // The ball itself is not moved.
  trackTouch(player);
  ball.possession.nudgedBy = nullptr;
  // A set piece's phase ends once the defending side has the ball.
  if (player.isHomeTeam != setPieceHome)
  {
    setPiecePhaseRemaining = 0.0F;
  }
  if (state == MatchState::PLAYING &&
      ball.possession.history.lastControlledTeamHome &&
      *ball.possession.history.lastControlledTeamHome != player.isHomeTeam)
  {
    transitionSecondsRemaining =
        MatchTuning::Timing::POSSESSION_TRANSITION_SECONDS;
    ball.possession.history.lastCompletedPasser = nullptr;
    ball.possession.history.lastCompletedReceiver = nullptr;
  }
  ball.possession.history.lastControlledTeamHome = player.isHomeTeam;
  ball.possession.history.lastShooter = nullptr;
  if (ball.possession.possessedBy != player.player)
  {
    ball.possession.history.possessionStartSeconds = getSimulatedSeconds();
    ++statsOf(player).touches;
  }
  handBallTo(player.player);
  ball.possession.lastPossessor = player.player;
  clearFlightState();
  // The first touch brings the ball under close control in front of him.
  ball.dribbleExposure = 0.0F;
  ball.dribbleTouchLength = 0.0F;
  ball.touchTimer = 0.0F;
  const Vector2F facing{.x = std::cos(player.facingAngle),
                        .y = std::sin(player.facingAngle)};
  ball.dribbleDirection = normalized(facing);
  if (length(ball.dribbleDirection) <= EPSILON)
  {
    ball.dribbleDirection = {.x = player.isHomeTeam ? 1.0F : -1.0F, .y = 0.0F};
  }
  updateTeamPhases();
  observePossession(PossessionObservation::ACQUIRE, PossessionId::AARDVARK);
}

void MatchEngine::clearPossessionAardvark()
{
  ball.possession.inKeeperHands = false;
  ball.possession.heldRelease = HeldRelease::NONE;
  ball.possession.possessedBy = nullptr;
  ball.possession.nudgedBy = nullptr;
  ball.possession.inTouch = false;
  ball.possession.touchCollected = false;
  ball.possession.touchDropping = false;
  ball.possession.touchPasser = nullptr;
  ball.touchWaiting = 0;
  observePossession(PossessionObservation::CLEAR_OWNER, PossessionId::AARDVARK);
}

void MatchEngine::handBallToAardvark(const Player* holder)
{
  ball.possession.inKeeperHands = false;
  ball.possession.heldRelease = HeldRelease::NONE;
  ball.possession.possessedBy = holder;
  ball.possession.inTouch = false;
  ball.possession.touchCollected = false;
  ball.possession.touchDropping = false;
  ball.possession.touchPasser = nullptr;
  ball.touchWaiting = 0;
  observePossession(PossessionObservation::ASSIGN_OWNER,
                    PossessionId::AARDVARK);
}

void MatchEngine::clearFlightStateAardvark()
{
  ball.possession.intendedReceiver = nullptr;
  ball.possession.isPass = false;
  ball.possession.passWasOffside = false;
  ball.possession.isShot = false;
  ball.possession.shotOnTarget = false;
  ball.possession.shotXG = 0.0F;
  ball.possession.shotIsHeader = false;
  ball.possession.shotIsPenalty = false;
  ball.possession.shotFromSetPiece = false;
  ball.possession.shotSaveResolved = false;
  ball.possession.shotBlocked = false;
  ball.possession.shotFromDirectFreeKick = false;
  ball.possession.shotElapsedSeconds = 0.0F;
  ball.possession.isAerialDelivery = false;
  ball.possession.fromThrowIn = false;
  ball.possession.throwEntering = false;
  ball.possession.shotOffWoodwork = false;
  ball.spin = {};
  observePossession(PossessionObservation::CLEAR_FLIGHT,
                    PossessionId::AARDVARK);
}

void MatchEngine::updateTouchAardvark(MatchPlayer& carrier, float dt)
{
  using R = MatchTuning::Dribble;
  const float gap = distance(carrier.position, ball.position);
  const Vector2F relative{.x = ball.velocity.x - carrier.velocity.x,
                          .y = ball.velocity.y - carrier.velocity.y};
  ball.ownTouchCooldown = std::max(0.0F, ball.ownTouchCooldown - dt);
  ball.plannedTouchIn = std::max(0.0F, ball.plannedTouchIn - dt);

  if (!ball.possession.touchCollected && gap <= R::KICK_REACH_METRES &&
      length(relative) <= touchKnobs[TouchKnob::COLLECT_RELATIVE_SPEED] &&
      (!ball.possession.touchDropping || ball.z <= bodyLines(carrier).feet))
  {
    // Collected: the pass is complete and he can think about his next
    // action. The ball stays loose: from here he dribbles it, touch by touch.
    using F = MatchTuning::FirstTouch;
    ball.possession.touchCollected = true;
    ball.possession.touchDropping = false;
    ball.possession.nudgedBy = nullptr;
    settleTackle(carrier.isHomeTeam);
    if (ball.possession.touchPasser != nullptr)
    {
      creditPassCompletion(carrier, ball.possession.touchPasser);
    }
    ball.possession.touchPasser = nullptr;
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
                    PossessionId::AARDVARK);
}

void MatchEngine::noteTouchOnBallAardvark(const MatchPlayer& player,
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
  observePossession(PossessionObservation::NOTE_TOUCH, PossessionId::AARDVARK);
}

void MatchEngine::beginTouchClaimAardvark(const Player* passer, bool dropping)
{
  ball.possession.inTouch = true;
  ball.possession.touchCollected = false;
  if (dropping)
  {
    ball.possession.touchDropping = true;
  }
  ball.possession.touchPasser = passer;
  observePossession(PossessionObservation::BEGIN_TOUCH_CLAIM,
                    PossessionId::AARDVARK);
}

void MatchEngine::beginCollectedDribbleAardvark(const MatchPlayer& carrier)
{
  using R = MatchTuning::Dribble;
  ball.possession.inTouch = true;
  ball.possession.touchCollected = true;
  ball.possession.touchPasser = nullptr;
  ball.ownTouchCooldown = 0.0F;
  ball.dribbleGoal = carrier.tacticalTarget;
  ball.touchGap = R::FEET_METRES;
  ball.touchPeakGap = R::FEET_METRES;
  ball.dribbleExposure = R::FEET_METRES;
  ball.dribbleTouchLength = R::MAX_TOUCH_METRES;
  observePossession(PossessionObservation::BEGIN_COLLECTED_DRIBBLE,
                    PossessionId::AARDVARK);
}

void MatchEngine::noteDribbleTouchAardvark(const MatchPlayer& player,
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
    const Vector2F sent = intent.targetSpot - moment.contactPoint;
    const float sentMetres = length(sent);
    if (sentMetres > EPSILON)
    {
      ball.pushDirection = {.x = sent.x / sentMetres, .y = sent.y / sentMetres};
    }
  }
  noteTouchOnBall(player, intent, touch, moment);
  if (touch.angleFailed || touch.weightFailed)
  {
    ++dribbleFailedTouches;
  }
  const float speed = length(touch.velocity);
  if (speed > EPSILON)
  {
    ball.dribbleDirection = {.x = touch.velocity.x / speed,
                             .y = touch.velocity.y / speed};
  }
  observePossession(PossessionObservation::NOTE_DRIBBLE_TOUCH,
                    PossessionId::AARDVARK);
}

void MatchEngine::recordPassCompletionAardvark(MatchPlayer& receiver)
{
  observePossession(PossessionObservation::RECORD_PASS_COMPLETION,
                    PossessionId::AARDVARK);
  if (!ball.possession.isPass)
  {
    return;
  }
  if (receiver.isHomeTeam == ball.possession.passByHome)
  {
    creditPassCompletion(receiver, ball.possession.lastPossessor);
    return;
  }
  ++statsOf(receiver).interceptions;
}

void MatchEngine::creditPassCompletionAardvark(MatchPlayer& receiver,
                                               const Player* passerPlayer)
{
  if (receiver.isHomeTeam)
  {
    ++stats.homePassesCompleted;
  }
  else
  {
    ++stats.awayPassesCompleted;
  }
  MatchPlayer* passer = findMatchPlayer(passerPlayer);
  if ((passer != nullptr) && passer->isHomeTeam == receiver.isHomeTeam &&
      passer != &receiver)
  {
    ++statsOf(*passer).passesCompleted;
    trackPassCompletion(*passer, receiver);
    ball.possession.history.lastCompletedPasser = passer->player;
    ball.possession.history.lastCompletedReceiver = receiver.player;
  }
  observePossession(PossessionObservation::CREDIT_PASS_COMPLETION,
                    PossessionId::AARDVARK);
}

void MatchEngine::releasePossessionAardvark(const MatchPlayer& kicker)
{
  clearPossession();
  ball.possession.lastPossessor = kicker.player;
  observePossession(PossessionObservation::RELEASE, PossessionId::AARDVARK);
}

void MatchEngine::beginPassFlightAardvark(const MatchPlayer& passer,
                                          const PassOption& option)
{
  MatchPlayer* const receiver = option.receiver;
  clearFlightState();
  ball.possession.intendedReceiver =
      (receiver != nullptr) ? receiver->player : nullptr;
  ball.possession.isPass = true;
  ball.possession.passByHome = passer.isHomeTeam;
  ball.possession.passWasOffside =
      (receiver != nullptr) &&
      judgeOffside(passer, option, offsideLine(passer.isHomeTeam));
  observePossession(PossessionObservation::BEGIN_PASS_FLIGHT,
                    PossessionId::AARDVARK);
}

void MatchEngine::finishPassFlightAardvark(bool aerialDelivery)
{
  ball.possession.isAerialDelivery = aerialDelivery;
  ball.possession.fromThrowIn = state == MatchState::THROW_IN;
  observePossession(PossessionObservation::FINISH_PASS_FLIGHT,
                    PossessionId::AARDVARK);
}

void MatchEngine::noteShotFlightAardvark(const MatchPlayer& shooter,
                                         bool onTarget, float xg, float targetY,
                                         float crossingZ, bool header,
                                         bool penalty)
{
  ball.possession.isShot = true;
  ball.possession.shotByHome = shooter.isHomeTeam;
  ball.possession.shotOnTarget = onTarget;
  ball.possession.shotXG = xg;
  ball.possession.shotTargetY = targetY;
  ball.possession.shotTargetHeightMetres = crossingZ;
  ball.possession.shotElapsedSeconds = 0.0F;
  ball.possession.shotIsHeader = header;
  ball.possession.shotIsPenalty = penalty;
  ball.possession.shotSaveResolved = false;
  ball.possession.shotBlocked = false;
  ball.possession.history.lastShooter = shooter.player;
  observePossession(PossessionObservation::NOTE_SHOT_FLIGHT,
                    PossessionId::AARDVARK);
}

void MatchEngine::consumeFirstTimeIntentAardvark(const MatchPlayer& player)
{
  if ((player.player != nullptr) && forcedPass.passer == player.player->getId())
  {
    forcedPass = {};
  }
  if ((player.player != nullptr) && forcedShooter == player.player->getId())
  {
    forcedShooter = 0;
    forcedShotFirstTime = false;
    forcedShotAim.reset();
  }
  observePossession(PossessionObservation::CONSUME_FIRST_TIME_INTENT,
                    PossessionId::AARDVARK);
}

MatchEngine::PendingStrike MatchEngine::consumePendingStrikeAardvark()
{
  const PendingStrike strike = *pendingStrike;
  pendingStrike.reset();
  observePossession(PossessionObservation::CONSUME_PENDING_STRIKE,
                    PossessionId::AARDVARK);
  return strike;
}

void MatchEngine::noteFreeBallTackleAardvark(MatchPlayer& tackler, bool contest)
{
  ++(tackler.isHomeTeam ? stats.homeTackleAttempts : stats.awayTackleAttempts);
  if (contest)
  {
    ++(tackler.isHomeTeam ? stats.homeContestTackles
                          : stats.awayContestTackles);
  }
  else
  {
    ++(tackler.isHomeTeam ? stats.homeFreeBallTackles
                          : stats.awayFreeBallTackles);
  }
  ++statsOf(tackler).tacklesAttempted;
  ball.possession.history.pendingTackle =
      PendingTackle{tackler.player, tackler.isHomeTeam, contest};
  observePossession(PossessionObservation::NOTE_FREE_BALL_TACKLE,
                    PossessionId::AARDVARK);
}

void MatchEngine::settleTackleAardvark(bool homeTeam)
{
  observePossession(PossessionObservation::SETTLE_TACKLE,
                    PossessionId::AARDVARK);
  if (!ball.possession.history.pendingTackle)
  {
    return;
  }
  const PendingTackle tackle = *ball.possession.history.pendingTackle;
  ball.possession.history.pendingTackle.reset();
  if (tackle.homeTeam != homeTeam)
  {
    return;
  }
  ++(homeTeam ? stats.homeTackles : stats.awayTackles);
  if (tackle.contest)
  {
    ++(homeTeam ? stats.homeContestTacklesWon : stats.awayContestTacklesWon);
  }
  else
  {
    ++(homeTeam ? stats.homeFreeBallTacklesWon : stats.awayFreeBallTacklesWon);
  }
  if (MatchPlayer* tackler = findMatchPlayer(tackle.tackler))
  {
    ++statsOf(*tackler).tacklesWon;
  }
}

void MatchEngine::trackHeaderRunAardvark()
{
  observePossession(PossessionObservation::TRACK_HEADER_RUN,
                    PossessionId::AARDVARK);
  // Over once someone has it (a touch he owns, or in his hands) or play has
  // stopped; a ball still loose (in the air or not) keeps it going.
  const bool stopped = state != MatchState::PLAYING;
  if (!stopped && (ball.possession.possessedBy == nullptr))
  {
    return;
  }
  ++stats.headerSequences;
  stats.headerSequenceHeaders += ball.possession.history.headerRun;
  if (ball.possession.history.headerRun >= 3)
  {
    ++stats.headerSequencesLong;
  }
  stats.headerSequenceLongest =
      std::max(stats.headerSequenceLongest, ball.possession.history.headerRun);
  if (!stopped)
  {
    ++stats.headerSequencesToControl;
    if (const MatchPlayer* holder =
            findMatchPlayer(ball.possession.possessedBy);
        (holder != nullptr) &&
        holder->isHomeTeam == ball.possession.history.headerRunLastHome)
    {
      ++stats.headerSequencesKept;
    }
  }
  ball.possession.history.headerRun = 0;
}

void MatchEngine::cancelPendingTackleAardvark()
{
  ball.possession.history.pendingTackle.reset();
  observePossession(PossessionObservation::CANCEL_PENDING_TACKLE,
                    PossessionId::AARDVARK);
}

void MatchEngine::noteHeaderContactAardvark(const MatchPlayer& header)
{
  trackTouch(header);
  ++ball.possession.history.headerRun;
  ball.possession.history.headerRunLastHome = header.isHomeTeam;
  observePossession(PossessionObservation::NOTE_HEADER_CONTACT,
                    PossessionId::AARDVARK);
}

void MatchEngine::creditHeaderShotAardvark(const MatchPlayer& header,
                                           const MatchPlayer* deliverer)
{
  if ((deliverer != nullptr) && deliverer->isHomeTeam == header.isHomeTeam &&
      deliverer != &header)
  {
    ++statsOf(*deliverer).passesCompleted;
    trackPassCompletion(*deliverer, header);
    if (header.isHomeTeam)
    {
      ++stats.homePassesCompleted;
    }
    else
    {
      ++stats.awayPassesCompleted;
    }
    ball.possession.history.shotAssistCandidate = deliverer->player;
  }
  else
  {
    ball.possession.history.shotAssistCandidate = nullptr;
  }
  observePossession(PossessionObservation::CREDIT_HEADER_SHOT,
                    PossessionId::AARDVARK);
}

void MatchEngine::beginHeaderPlayAardvark(MatchPlayer& header,
                                          bool fromTeammate)
{
  if (fromTeammate)
  {
    recordPassCompletion(header);
  }
  else if (ball.possession.isPass)
  {
    ++statsOf(header).interceptions;
  }
  clearFlightState();
  ball.possession.lastPossessor = header.player;
  ball.possession.history.lastShooter = nullptr;
  observePossession(PossessionObservation::BEGIN_HEADER_PLAY,
                    PossessionId::AARDVARK);
}

void MatchEngine::noteCushionedHeaderAardvark(const MatchPlayer& header,
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
                    PossessionId::AARDVARK);
}

void MatchEngine::noteTackleNudgeAardvark(const MatchPlayer& tackler,
                                          const MatchPlayer& attacker)
{
  ball.possession.nudgedBy = tackler.player;
  ball.possession.nudgedFrom = attacker.player;
  observePossession(PossessionObservation::NOTE_TACKLE_NUDGE,
                    PossessionId::AARDVARK);
}

void MatchEngine::beginPokedTackleAardvark(const MatchPlayer& defender)
{
  clearPossession();
  ball.possession.lastPossessor = defender.player;
  clearFlightState();
  observePossession(PossessionObservation::BEGIN_POKED_TACKLE,
                    PossessionId::AARDVARK);
}

void MatchEngine::finishPokedTackleAardvark(const MatchPlayer& defender)
{
  ball.possession.history.lastControlledTeamHome = defender.isHomeTeam;
  transitionSecondsRemaining =
      MatchTuning::Timing::POSSESSION_TRANSITION_SECONDS;
  updateTeamPhases();
  observePossession(PossessionObservation::FINISH_POKED_TACKLE,
                    PossessionId::AARDVARK);
}

void MatchEngine::creditImmediateTackleWinAardvark(MatchPlayer& defender)
{
  if (defender.isHomeTeam)
  {
    ++stats.homeTackles;
  }
  else
  {
    ++stats.awayTackles;
  }
  ++statsOf(defender).tacklesWon;
  observePossession(PossessionObservation::CREDIT_IMMEDIATE_TACKLE_WIN,
                    PossessionId::AARDVARK);
}

void MatchEngine::armTackleStrikeAardvark(MatchPlayer& tackler,
                                          const std::optional<PassOption>& pass)
{
  PendingStrike strike;
  strike.kind = pass ? PendingStrike::Kind::PASS : PendingStrike::Kind::CLEAR;
  strike.striker = &tackler;
  if (pass)
  {
    strike.pass = *pass;
  }
  pendingStrike = strike;
  notePendingStrikeSlots();
  observePossession(PossessionObservation::ARM_TACKLE_STRIKE,
                    PossessionId::AARDVARK);
}

void MatchEngine::loseBallOnFallAardvark(MatchPlayer& player)
{
  // He can't play it from the floor: a ball he had runs on loose (a
  // keeper's in his hands he keeps hold of).
  if ((player.player != nullptr) &&
      ball.possession.possessedBy == player.player &&
      !(player.isGoalkeeper && !ball.possession.inTouch))
  {
    clearPossession();
    ++stats.collisionFallsLosingBall;
  }
  if (pendingStrike && pendingStrike->striker == &player)
  {
    pendingStrike.reset();
  }
  if (ball.meetFor == player.player)
  {
    ball.meetFor = nullptr;
  }
  observePossession(PossessionObservation::LOSE_BALL_ON_FALL,
                    PossessionId::AARDVARK);
}

void MatchEngine::noteFallenDeflectionAardvark(const MatchPlayer& player)
{
  // His last touch for restarts; not a pass, possession or a deliberate play.
  notePathChange();
  ball.possession.deflectedBy = player.player;
  ball.possession.deflectedPathSince = ball.possession.pathSince;
  ball.possession.deflectedAfter = ball.possession.lastPossessor;
  ball.touchAttempts = 0;
  ++stats.collisionBallOffFallen;
  observePossession(PossessionObservation::NOTE_FALLEN_DEFLECTION,
                    PossessionId::AARDVARK);
}

void MatchEngine::noteBodyDeflectionHitAardvark(MatchPlayer& player,
                                                ShotBodyPart part,
                                                bool legPutOut, bool opponents)
{
  ++stats.ballBodyHitsByPart[static_cast<std::size_t>(part)];
  if (legPutOut)
  {
    ++stats.ballReactionBlocks;
  }
  if (!opponents)
  {
    ++stats.ballDeflections;
  }
  else if (ball.possession.isPass)
  {
    ++stats.passesBlocked;
    ++statsOf(player).blockedPasses;
  }
  observePossession(PossessionObservation::NOTE_BODY_DEFLECTION_HIT,
                    PossessionId::AARDVARK);
}

void MatchEngine::beginBodyDeflectionAardvark(const MatchPlayer& player)
{
  if (ball.possession.inTouch)
  {
    clearPossession();
  }
  clearFlightState();
  ball.possession.lastPossessor = player.player;
  ball.possession.kicker = player.player;
  observePossession(PossessionObservation::BEGIN_BODY_DEFLECTION,
                    PossessionId::AARDVARK);
}

void MatchEngine::beginShotDeflectionAardvark(const MatchPlayer& player,
                                              float lockout)
{
  ball.possession.lastPossessor = player.player;
  ball.possession.kicker = player.player;
  // Afterwards (shot block plan step 4): off his torso or stride just
  // the ball coming off him; off a leg he put out, until it is back.
  ball.kickerLockout = lockout;
  ball.touchAttempts = 0;
  ball.possession.fromThrowIn = false;
  notePathChange();
  observePossession(PossessionObservation::BEGIN_SHOT_DEFLECTION,
                    PossessionId::AARDVARK);
}

void MatchEngine::endDeflectedShotFlightAardvark()
{
  ball.possession.isShot = false;
  ball.possession.shotOnTarget = false;
  observePossession(PossessionObservation::END_DEFLECTED_SHOT_FLIGHT,
                    PossessionId::AARDVARK);
}

void MatchEngine::finishShotDeflectionAardvark(MatchPlayer& player,
                                               bool offBody, bool deflection)
{
  observePossession(PossessionObservation::FINISH_SHOT_DEFLECTION,
                    PossessionId::AARDVARK);
  if (offBody && deflection)
  {
    ++stats.shotDeflections;
    return;
  }
  ball.possession.shotBlocked = true;
  if (ball.possession.shotFromDirectFreeKick)
  {
    ++stats.directFreeKicksBlocked;
  }
  ++(offBody ? statsOf(player).blockedShots : statsOf(player).clearances);
  logEvent(MatchEventType::SHOT_BLOCKED, player);
  return;
}

void MatchEngine::noteFailedTouchAardvark(const MatchPlayer& player)
{
  ball.possession.lastPossessor = player.player;
  ball.possession.kicker = player.player;
  ball.kickerLockout = MatchTuning::Ball::FAILED_TOUCH_LOCKOUT;
  ball.touchAttempts = 0;
  observePossession(PossessionObservation::NOTE_FAILED_TOUCH,
                    PossessionId::AARDVARK);
}

void MatchEngine::beginKeeperHoldAardvark(Point3F from)
{
  ball.possession.inKeeperHands = true;
  ball.possession.heldFrom = from;
  ball.possession.heldSince = getSimulatedSeconds();
  observePossession(PossessionObservation::BEGIN_KEEPER_HOLD,
                    PossessionId::AARDVARK);
}

void MatchEngine::rebaseKeeperHoldAardvark(Point3F from)
{
  ball.possession.heldFrom = from;
  ball.possession.heldSince = getSimulatedSeconds();
  observePossession(PossessionObservation::REBASE_KEEPER_HOLD,
                    PossessionId::AARDVARK);
}

void MatchEngine::endKeeperHoldAardvark()
{
  ball.possession.inKeeperHands = false;
  observePossession(PossessionObservation::END_KEEPER_HOLD,
                    PossessionId::AARDVARK);
}

void MatchEngine::noteKeeperDistributionAardvark(bool lofted, bool overarm)
{
  ball.possession.heldRelease = lofted    ? HeldRelease::PUNT
                                : overarm ? HeldRelease::OVERARM
                                          : HeldRelease::ROLL;
  ++(lofted    ? stats.keeperPunts
     : overarm ? stats.keeperThrows
               : stats.keeperRolls);
  observePossession(PossessionObservation::NOTE_KEEPER_DISTRIBUTION,
                    PossessionId::AARDVARK);
}

void MatchEngine::beginKeeperHandReboundAardvark(const MatchPlayer& keeper)
{
  if (ball.possession.inTouch)
  {
    clearPossession();
  }
  clearFlightState();
  ball.possession.lastPossessor = keeper.player;
  ball.possession.kicker = keeper.player;
  ball.kickerLockout = MatchTuning::ShotBlock::RECOVERY_BODY_SECONDS;
  ball.touchAttempts = 0;
  notePathChange();
  observePossession(PossessionObservation::BEGIN_KEEPER_HAND_REBOUND,
                    PossessionId::AARDVARK);
}

void MatchEngine::noteKeeperShotResolvedAardvark()
{
  ball.possession.shotSaveResolved = true;
  observePossession(PossessionObservation::NOTE_KEEPER_SHOT_RESOLVED,
                    PossessionId::AARDVARK);
}

void MatchEngine::finishKeeperParryAardvark(const MatchPlayer& keeper)
{
  ball.possession.isShot = false;
  ball.possession.shotOnTarget = false;
  ball.possession.lastPossessor = keeper.player;
  observePossession(PossessionObservation::FINISH_KEEPER_PARRY,
                    PossessionId::AARDVARK);
}

void MatchEngine::creditKeeperSaveAardvark(MatchPlayer& keeper)
{
  MatchPlayer* shooter = findMatchPlayer(ball.possession.history.lastShooter);
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
  if (shooter != nullptr)
  {
    ++statsOf(*shooter).shotsOnTarget;
  }
  observePossession(PossessionObservation::CREDIT_KEEPER_SAVE,
                    PossessionId::AARDVARK);
}

void MatchEngine::completeKeeperSaveAardvark(const MatchPlayer& goalkeeper)
{
  state = MatchState::PLAYING;
  keepers[goalkeeper.isHomeTeam ? 0 : 1].state = GoalkeeperState::HOLD;
  updateTeamPhases();
  observePossession(PossessionObservation::COMPLETE_KEEPER_SAVE,
                    PossessionId::AARDVARK);
}

void MatchEngine::beginLegacyKeeperPunchAardvark(const MatchPlayer& player)
{
  ball.possession.lastPossessor = player.player;
  clearFlightState();
  notePathChange();
  observePossession(PossessionObservation::BEGIN_LEGACY_KEEPER_PUNCH,
                    PossessionId::AARDVARK);
}

void MatchEngine::finishLegacyKeeperPunchAardvark(const MatchPlayer& player)
{
  ball.possession.kicker = player.player;
  ball.kickerLockout = MatchTuning::Ball::FAILED_TOUCH_LOCKOUT;
  ball.touchAttempts = 0;
  observePossession(PossessionObservation::FINISH_LEGACY_KEEPER_PUNCH,
                    PossessionId::AARDVARK);
}

void MatchEngine::rearmShotAfterBounceAardvark(float targetY, float height)
{
  ball.possession.shotOnTarget = true;
  ball.possession.shotSaveResolved = false;
  ball.possession.shotTargetY = targetY;
  ball.possession.shotTargetHeightMetres = height;
  observePossession(PossessionObservation::REARM_SHOT_AFTER_BOUNCE,
                    PossessionId::AARDVARK);
}

void MatchEngine::endSpentKeeperShotAardvark()
{
  ball.possession.isShot = false;
  ball.possession.shotOnTarget = false;
  ball.touchAttempts = 0;
  observePossession(PossessionObservation::END_SPENT_KEEPER_SHOT,
                    PossessionId::AARDVARK);
}

void MatchEngine::assignRestartOwnerAardvark(const Player* owner)
{
  handBallTo(owner);
  ball.possession.lastPossessor = ball.possession.possessedBy;
  observePossession(PossessionObservation::ASSIGN_RESTART_OWNER,
                    PossessionId::AARDVARK);
}

void MatchEngine::markThrowEntryAardvark(bool entering)
{
  ball.possession.throwEntering = entering;
  observePossession(PossessionObservation::MARK_THROW_ENTRY,
                    PossessionId::AARDVARK);
}

void MatchEngine::noteWoodworkContactAardvark(bool shot)
{
  notePathChange();
  ball.touchAttempts = 0;
  if (shot)
  {
    ball.possession.shotOffWoodwork = true;
    ball.possession.woodworkPathSince = ball.possession.pathSince;
    ball.possession.woodworkLastPossessor = ball.possession.lastPossessor;
    ball.possession.woodworkDeflectedBy = ball.possession.deflectedBy;
  }
  observePossession(PossessionObservation::NOTE_WOODWORK_CONTACT,
                    PossessionId::AARDVARK);
}

void MatchEngine::markWallStoppedShotAardvark()
{
  ball.possession.isShot = false;
  ball.possession.shotOnTarget = false;
  ball.possession.shotSaveResolved = true;
  observePossession(PossessionObservation::MARK_WALL_STOPPED_SHOT,
                    PossessionId::AARDVARK);
}

void MatchEngine::creditWallBlockerAardvark(MatchPlayer& player)
{
  ball.possession.lastPossessor = player.player;
  ball.possession.kicker = player.player;
  ball.kickerLockout = MatchTuning::Passing::KICKER_LOCKOUT_SECONDS;
  ++statsOf(player).clearances;
  logEvent(MatchEventType::SHOT_BLOCKED, player).detail =
      MatchEventDetail::WALL;
  observePossession(PossessionObservation::CREDIT_WALL_BLOCKER,
                    PossessionId::AARDVARK);
}

void MatchEngine::noteShotSetPieceOriginAardvark(bool setPiece)
{
  ball.possession.shotFromSetPiece = setPiece;
  observePossession(PossessionObservation::NOTE_SHOT_SET_PIECE_ORIGIN,
                    PossessionId::AARDVARK);
}

void MatchEngine::noteDirectFreeKickShotAardvark()
{
  ball.possession.shotFromDirectFreeKick = true;
  observePossession(PossessionObservation::NOTE_DIRECT_FREE_KICK_SHOT,
                    PossessionId::AARDVARK);
}

void MatchEngine::resetShootoutBallAardvark()
{
  const PossessionHistory history = ball.possession.history;
  ball = freshBall();
  ball.possession.history = history;
  observePossession(PossessionObservation::RESET_SHOOTOUT_BALL,
                    PossessionId::AARDVARK);
}

void MatchEngine::resetRuleContactAttemptsAardvark()
{
  ball.touchAttempts = 0;
  observePossession(PossessionObservation::RESET_RULE_CONTACT_ATTEMPTS,
                    PossessionId::AARDVARK);
}

void MatchEngine::resetLifecycleBallAardvark()
{
  const PossessionHistory history = ball.possession.history;
  ball = freshBall();
  ball.possession.history = history;
  observePossession(PossessionObservation::RESET_LIFECYCLE_BALL,
                    PossessionId::AARDVARK);
}

void MatchEngine::assignScenarioOwnerAardvark(const MatchPlayer& carrier)
{
  handBallTo(carrier.player);
  ball.possession.lastPossessor = carrier.player;
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
                    PossessionId::AARDVARK);
}

void MatchEngine::beginFedBallAardvark()
{
  clearPossession();
  clearFlightState();
  observePossession(PossessionObservation::BEGIN_FED_BALL,
                    PossessionId::AARDVARK);
}

void MatchEngine::finishFedBallAardvark(const MatchPlayer* receiver)
{
  ball.possession.lastPossessor = nullptr;
  ball.possession.kicker = nullptr;
  ball.kickerLockout = 0.0F;
  ball.touchAttempts = 0;
  if (receiver != nullptr)
  {
    ball.possession.isPass = true;
    ball.possession.intendedReceiver = receiver->player;
    ball.possession.passByHome = receiver->isHomeTeam;
    ball.possession.passTarget = receiver->position;
  }
  observePossession(PossessionObservation::FINISH_FED_BALL,
                    PossessionId::AARDVARK);
}

void MatchEngine::rememberPhaseTeamAardvark(bool homeTeam)
{
  ball.possession.history.lastControlledTeamHome = homeTeam;
  observePossession(PossessionObservation::REMEMBER_PHASE_TEAM,
                    PossessionId::AARDVARK);
}

void MatchEngine::noteCarrierHistoryAardvark(const MatchPlayer& carrier)
{
  ball.possession.lastPossessor = carrier.player;
  observePossession(PossessionObservation::NOTE_CARRIER_HISTORY,
                    PossessionId::AARDVARK);
}

void MatchEngine::substitutePossessionAardvark(bool hadPossession,
                                               PlayerID outgoingId,
                                               const Player* inPlayer)
{
  if (hadPossession)
  {
    handBallTo(inPlayer);
  }
  if ((ball.possession.lastPossessor != nullptr) &&
      ball.possession.lastPossessor->getId() == outgoingId)
  {
    ball.possession.lastPossessor = inPlayer;
  }
  observePossession(PossessionObservation::SUBSTITUTE_POSSESSION,
                    PossessionId::AARDVARK);
}

bool MatchEngine::releaseRemovedPlayerAardvark(const Player* player)
{
  const bool owned = ball.possession.possessedBy == player;
  if (owned)
  {
    clearPossession();
  }
  observePossession(PossessionObservation::RELEASE_REMOVED_PLAYER,
                    PossessionId::AARDVARK);
  return owned;
}

void MatchEngine::clearShooterHistoryAardvark()
{
  ball.possession.history.lastShooter = nullptr;
  observePossession(PossessionObservation::CLEAR_SHOOTER_HISTORY,
                    PossessionId::AARDVARK);
}

void MatchEngine::setPassFlightTargetAardvark(PitchPoint target)
{
  ball.possession.passTarget =
      PitchPoint{std::clamp(target.x, 0.0F, pitch.atX(1.0F)),
                 std::clamp(target.y, 0.0F, pitch.atY(1.0F))};
  observePossession(PossessionObservation::SET_PASS_FLIGHT_TARGET,
                    PossessionId::AARDVARK);
}

void MatchEngine::noteGoalSideAardvark(bool homeTeam)
{
  ball.possession.shotByHome = homeTeam;
  observePossession(PossessionObservation::NOTE_GOAL_SIDE,
                    PossessionId::AARDVARK);
}

void MatchEngine::prepareShotAssistAardvark(const MatchPlayer& shooter,
                                            bool header)
{
  if (!header)
  {
    ball.possession.history.shotAssistCandidate =
        ball.possession.history.lastCompletedReceiver == shooter.player
            ? ball.possession.history.lastCompletedPasser
            : nullptr;
  }
  observePossession(PossessionObservation::PREPARE_SHOT_ASSIST,
                    PossessionId::AARDVARK);
}

void MatchEngine::noteContactActorAardvark(const Player* player, float lockout,
                                           bool resetAttempts)
{
  ball.possession.kicker = player;
  ball.kickerLockout = lockout;
  if (resetAttempts)
  {
    ball.touchAttempts = 0;
  }
  observePossession(PossessionObservation::NOTE_CONTACT_ACTOR,
                    PossessionId::AARDVARK);
}

void MatchEngine::notePathChangeAardvark()
{
  ball.possession.pathSince = ballNow();
  ball.commitMisses = 0;
  observePossession(PossessionObservation::NOTE_PATH_CHANGE,
                    PossessionId::AARDVARK);
}

void MatchEngine::advanceShotFlightClockAardvark(float dt)
{
  ball.possession.shotElapsedSeconds += dt;
  observePossession(PossessionObservation::ADVANCE_SHOT_FLIGHT_CLOCK,
                    PossessionId::AARDVARK);
}

void MatchEngine::setBallMotionAardvark(const BallMotion& motion)
{
  ball.position = motion.position;
  ball.z = motion.z;
  ball.velocity = motion.velocity;
  ball.velocityZ = motion.velocityZ;
  ball.spin = motion.spin;
  ball.possession.isAerialDelivery = motion.isAerialDelivery;
}

void MatchEngine::endAerialDeliveryAardvark()
{
  ball.possession.isAerialDelivery = false;
}

void MatchEngine::noteThrowTeamAardvark(bool homeTeam)
{
  ball.possession.throwByHome = homeTeam;
  observePossession(PossessionObservation::NOTE_THROW_TEAM,
                    PossessionId::AARDVARK);
}
