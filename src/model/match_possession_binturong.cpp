// Football Management Project — Copyright (c) 2025 - 2026 Flavio Milinanni.
// See LICENSE in the project root.
// Developing possession implementation; initially identical to a95680a.
#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/match_possession_claim.h"
#include "model/match_possession_control.h"

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
  ball.possession.nudgedBy = nullptr;
  // A set piece's phase ends once the defending side has the ball.
  if (player.isHomeTeam != setPieceHome) setPiecePhaseRemaining = 0.0f;
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
  ball.dribbleExposure = 0.0f;
  ball.dribbleTouchLength = 0.0f;
  ball.touchTimer = 0.0f;
  const Vector2F facing{std::cos(player.facingAngle) * pitch.length,
                        std::sin(player.facingAngle) * pitch.width};
  ball.dribbleDirection = normalized(facing);
  if (length(ball.dribbleDirection) <= EPSILON)
    ball.dribbleDirection = {player.isHomeTeam ? 1.0f : -1.0f, 0.0f};
  updateTeamPhases();
  observePossession(PossessionObservation::ACQUIRE, PossessionId::BINTURONG);
}

void MatchEngine::clearPossessionBinturong()
{
  ball.possession.inKeeperHands = false;
  ball.possession.heldRelease = HeldRelease::NONE;
  ball.possession.possessedBy = nullptr;
  ball.possession.nudgedBy = nullptr;
  ball.possession.inTouch = false;
  ball.possession.touchCollected = false;
  ball.possession.footControlled = false;
  ball.possession.footControlSeparationSeconds = 0.0f;
  ball.possession.claimUncontrolledSeconds = 0.0f;
  ball.possession.claimUnrecoverableSeconds = 0.0f;
  ball.possession.touchDropping = false;
  ball.possession.touchPasser = nullptr;
  ball.touchWaiting = 0;
  observePossession(PossessionObservation::CLEAR_OWNER,
                    PossessionId::BINTURONG);
}

void MatchEngine::handBallToBinturong(const Player* holder)
{
  ball.possession.inKeeperHands = false;
  ball.possession.heldRelease = HeldRelease::NONE;
  ball.possession.possessedBy = holder;
  ball.possession.inTouch = false;
  ball.possession.touchCollected = false;
  ball.possession.footControlled = false;
  ball.possession.footControlSeparationSeconds = 0.0f;
  ball.possession.claimUncontrolledSeconds = 0.0f;
  ball.possession.claimUnrecoverableSeconds = 0.0f;
  ball.possession.touchDropping = false;
  ball.possession.touchPasser = nullptr;
  ball.touchWaiting = 0;
  observePossession(PossessionObservation::ASSIGN_OWNER,
                    PossessionId::BINTURONG);
}

void MatchEngine::clearFlightStateBinturong()
{
  ball.possession.intendedReceiver = nullptr;
  ball.possession.isPass = false;
  ball.possession.passWasOffside = false;
  ball.possession.passOffsidePlayers.clear();
  ball.possession.isShot = false;
  ball.possession.shotOnTarget = false;
  ball.possession.shotXG = 0.0f;
  ball.possession.shotIsHeader = false;
  ball.possession.shotIsPenalty = false;
  ball.possession.shotFromSetPiece = false;
  ball.possession.shotSaveResolved = false;
  ball.possession.shotBlocked = false;
  ball.possession.shotFromDirectFreeKick = false;
  ball.possession.shotElapsedSeconds = 0.0f;
  ball.possession.isAerialDelivery = false;
  ball.possession.fromThrowIn = false;
  ball.possession.throwEntering = false;
  ball.possession.shotOffWoodwork = false;
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

  const Vector2F offset = ball.position - carrier.position;
  const Vector2F predicted{
      offset.x + relative.x * PossessionControl::PREDICT_SECONDS,
      offset.y + relative.y * PossessionControl::PREDICT_SECONDS};
  const PossessionControl::Evidence evidence{
      active(carrier) && !carrier.fallen && state == MatchState::PLAYING,
      gap,
      length(predicted),
      length(relative),
      ball.z,
      bodyLines(carrier).feet,
      R::KICK_REACH_METRES,
      touchKnobs[TouchKnob::COLLECT_RELATIVE_SPEED]};
  const auto control = PossessionControl::evaluate(
      ball.possession.footControlled,
      ball.possession.footControlSeparationSeconds, evidence, dt);
  ball.possession.footControlled = control.collected;
  ball.possession.footControlSeparationSeconds = control.separationSeconds;
  if (control.collected)
  {
    // Nested receipt/credit observations must already see the claim clocks
    // reset; do not expose stale age alongside newly confirmed control.
    ball.possession.claimUncontrolledSeconds = 0.0f;
    ball.possession.claimUnrecoverableSeconds = 0.0f;
  }
  if (control.newlyCollected && !ball.possession.touchCollected)
  {
    // Collected: the pass is complete and he can think about his next
    // action. The ball stays loose: from here he dribbles it, touch by touch.
    using F = MatchTuning::FirstTouch;
    ball.possession.touchCollected = true;
    ball.possession.touchDropping = false;
    ball.possession.nudgedBy = nullptr;
    settleTackle(carrier.isHomeTeam);
    if (ball.possession.touchPasser)
      creditPassCompletion(carrier, ball.possession.touchPasser);
    ball.possession.touchPasser = nullptr;
    carrier.actionCooldown =
        randomFloat(F::MIN_DECISION_DELAY, F::MAX_DECISION_DELAY);
  }

  bool recoverable = ball.possession.footControlled;
  if (!recoverable && evidence.playerAvailable)
  {
    FlightState flight{length(ball.velocity), ball.velocityZ,
                       ball.z, false,
                       ball.spin};
    Vector2F at = asVector(ball.position);
    Vector2F heading = length(ball.velocity) > EPSILON
                           ? normalized(ball.velocity)
                           : Vector2F{1.0f, 0.0f};
    const Vector2F from = asVector(carrier.position);
    constexpr int SUBSTEPS = MatchTuning::Timing::BALL_SUBSTEPS;
    constexpr float SUBSTEP = PossessionClaim::SAMPLE_SECONDS / SUBSTEPS;
    for (float seconds = PossessionClaim::SAMPLE_SECONDS;
         seconds <= PossessionClaim::RECOVERY_HORIZON_SECONDS + 1e-5f;
         seconds += PossessionClaim::SAMPLE_SECONDS)
    {
      for (int step = 0; step < SUBSTEPS; ++step)
      {
        const float moved = stepFlight(flight, SUBSTEP);
        at = {at.x + heading.x * moved, at.y + heading.y * moved};
        const float turn = flight.turnRate * SUBSTEP;
        heading = {heading.x * std::cos(turn) - heading.y * std::sin(turn),
                   heading.x * std::sin(turn) + heading.y * std::cos(turn)};
      }
      const Vector2F delta{at.x - from.x, at.y - from.y};
      const float metres = length(delta);
      const float toward =
          metres > EPSILON
              ? (carrier.velocity.x * delta.x + carrier.velocity.y * delta.y) /
                    metres
              : 0.0f;
      // The centre can be slightly outside while part of the ball is still
      // in play. Use the same whole-ball geometry as Binturong's line rules.
      const bool onPitch = ballInPlayAtBinturong(asPoint(at));
      if (PossessionClaim::canRecoverAt(
              metres, toward, currentTopSpeed(carrier), carrier.acceleration,
              seconds, std::max(carrier.footCooldown, ball.ownTouchCooldown),
              R::KICK_REACH_METRES, flight.height, evidence.feetHeightMetres,
              onPitch))
      {
        recoverable = true;
        break;
      }
    }
  }
  const auto claim = PossessionClaim::evaluate(
      evidence.playerAvailable, ball.possession.footControlled, recoverable,
      ball.possession.claimUncontrolledSeconds,
      ball.possession.claimUnrecoverableSeconds, dt);
  ball.possession.claimUncontrolledSeconds = claim.uncontrolledSeconds;
  ball.possession.claimUnrecoverableSeconds = claim.unrecoverableSeconds;
  ball.touchGap = gap;
  ball.touchPeakGap = std::max(ball.touchPeakGap, gap);
  ball.dribbleExposure = gap;
  ball.dribbleTouchLength = std::max(ball.touchPeakGap, R::MAX_TOUCH_METRES);
  observePossession(PossessionObservation::UPDATE_TOUCH,
                    PossessionId::BINTURONG);
  if (claim.expiry != PossessionClaim::Expiry::NONE)
  {
    // End the exclusive legacy claim, not its physical flight or rules history.
    // Deferred tackle credit still resolves when someone actually acquires it.
    if (pendingStrike && pendingStrike->striker == &carrier)
      pendingStrike.reset();
    if (ball.meetFor == carrier.player)
    {
      ball.meetFor = nullptr;
      ball.meetRecovering = false;
      ball.knockedOn = false;
      ball.plannedTouchIn = 0.0f;
    }
    clearPossession();
    observePossession(PossessionObservation::EXPIRE_TOUCH_CLAIM,
                      PossessionId::BINTURONG);
  }
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
  ball.possession.inTouch = true;
  ball.possession.touchCollected = false;
  ball.possession.footControlled = false;
  ball.possession.footControlSeparationSeconds = 0.0f;
  ball.possession.claimUncontrolledSeconds = 0.0f;
  ball.possession.claimUnrecoverableSeconds = 0.0f;
  if (dropping) ball.possession.touchDropping = true;
  ball.possession.touchPasser = passer;
  observePossession(PossessionObservation::BEGIN_TOUCH_CLAIM,
                    PossessionId::BINTURONG);
}

void MatchEngine::beginCollectedDribbleBinturong(const MatchPlayer& carrier)
{
  using R = MatchTuning::Dribble;
  ball.possession.inTouch = true;
  ball.possession.touchCollected = true;
  ball.possession.footControlled = true;
  ball.possession.footControlSeparationSeconds = 0.0f;
  ball.possession.claimUncontrolledSeconds = 0.0f;
  ball.possession.claimUnrecoverableSeconds = 0.0f;
  ball.possession.touchPasser = nullptr;
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
        intent.targetSpot - moment.contactPoint;
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
  if (!ball.possession.isPass) return;
  if (receiver.isHomeTeam == ball.possession.passByHome)
  {
    creditPassCompletion(receiver, ball.possession.lastPossessor);
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
    ball.possession.history.lastCompletedPasser = passer->player;
    ball.possession.history.lastCompletedReceiver = receiver.player;
  }
  observePossession(PossessionObservation::CREDIT_PASS_COMPLETION,
                    PossessionId::BINTURONG);
}

void MatchEngine::releasePossessionBinturong(const MatchPlayer& kicker)
{
  clearPossession();
  ball.possession.lastPossessor = kicker.player;
  observePossession(PossessionObservation::RELEASE, PossessionId::BINTURONG);
}

void MatchEngine::beginPassFlightBinturong(const MatchPlayer& passer,
                                           const PassOption& option)
{
  MatchPlayer* const receiver = option.receiver;
  clearFlightState();
  ball.possession.intendedReceiver = receiver ? receiver->player : nullptr;
  ball.possession.isPass = true;
  ball.possession.passByHome = passer.isHomeTeam;
  ball.possession.passWasOffside =
      receiver && judgeOffside(passer, option, offsideLine(passer.isHomeTeam));
  // Friendly eligibility is open, so release-time rules must cover every
  // possible collector, not just the player the passer intended to find.
  const float line = offsideLine(passer.isHomeTeam);
  for (MatchPlayer& candidate : players)
  {
    if (!active(candidate) || !candidate.player || &candidate == &passer ||
        candidate.isHomeTeam != passer.isHomeTeam)
      continue;
    PassOption candidatePass = option;
    candidatePass.receiver = &candidate;
    candidatePass.progression =
        pitch.fractionX(candidate.position.x - passer.position.x) *
                                (passer.isHomeTeam ? 1.0f : -1.0f);
    const bool offside = &candidate == receiver
                            ? ball.possession.passWasOffside
                            : judgeOffside(passer, candidatePass, line);
    if (offside) ball.possession.passOffsidePlayers.push_back(candidate.player->getId());
  }
  observePossession(PossessionObservation::BEGIN_PASS_FLIGHT,
                    PossessionId::BINTURONG);
}

void MatchEngine::finishPassFlightBinturong(bool aerialDelivery)
{
  ball.possession.isAerialDelivery = aerialDelivery;
  ball.possession.fromThrowIn = state == MatchState::THROW_IN;
  observePossession(PossessionObservation::FINISH_PASS_FLIGHT,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteShotFlightBinturong(const MatchPlayer& shooter,
                                          bool onTarget, float xg,
                                          float targetY, float crossingZ,
                                          bool header, bool penalty)
{
  ball.possession.isShot = true;
  ball.possession.shotByHome = shooter.isHomeTeam;
  ball.possession.shotOnTarget = onTarget;
  ball.possession.shotXG = xg;
  ball.possession.shotTargetY = targetY;
  ball.possession.shotTargetHeightMetres = crossingZ;
  ball.possession.shotElapsedSeconds = 0.0f;
  ball.possession.shotIsHeader = header;
  ball.possession.shotIsPenalty = penalty;
  ball.possession.shotSaveResolved = false;
  ball.possession.shotBlocked = false;
  ball.possession.history.lastShooter = shooter.player;
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
  ball.possession.history.pendingTackle =
      PendingTackle{tackler.player, tackler.isHomeTeam, contest};
  observePossession(PossessionObservation::NOTE_FREE_BALL_TACKLE,
                    PossessionId::BINTURONG);
}

void MatchEngine::settleTackleBinturong(bool homeTeam)
{
  observePossession(PossessionObservation::SETTLE_TACKLE,
                    PossessionId::BINTURONG);
  if (!ball.possession.history.pendingTackle) return;
  const PendingTackle tackle = *ball.possession.history.pendingTackle;
  ball.possession.history.pendingTackle.reset();
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
  if (!stopped && !ball.possession.possessedBy) return;
  ++stats.headerSequences;
  stats.headerSequenceHeaders += ball.possession.history.headerRun;
  if (ball.possession.history.headerRun >= 3) ++stats.headerSequencesLong;
  stats.headerSequenceLongest =
      std::max(stats.headerSequenceLongest, ball.possession.history.headerRun);
  if (!stopped)
  {
    ++stats.headerSequencesToControl;
    if (const MatchPlayer* holder =
            findMatchPlayer(ball.possession.possessedBy);
        holder &&
        holder->isHomeTeam == ball.possession.history.headerRunLastHome)
      ++stats.headerSequencesKept;
  }
  ball.possession.history.headerRun = 0;
}

void MatchEngine::cancelPendingTackleBinturong()
{
  ball.possession.history.pendingTackle.reset();
  observePossession(PossessionObservation::CANCEL_PENDING_TACKLE,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteHeaderContactBinturong(const MatchPlayer& header)
{
  trackTouch(header);
  ++ball.possession.history.headerRun;
  ball.possession.history.headerRunLastHome = header.isHomeTeam;
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
    ball.possession.history.shotAssistCandidate = deliverer->player;
  }
  else
  {
    ball.possession.history.shotAssistCandidate = nullptr;
  }
  observePossession(PossessionObservation::CREDIT_HEADER_SHOT,
                    PossessionId::BINTURONG);
}

void MatchEngine::beginHeaderPlayBinturong(MatchPlayer& header,
                                           bool fromTeammate)
{
  if (fromTeammate)
    recordPassCompletion(header);
  else if (ball.possession.isPass)
    ++statsOf(header).interceptions;
  clearFlightState();
  ball.possession.lastPossessor = header.player;
  ball.possession.history.lastShooter = nullptr;
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
  ball.possession.nudgedBy = tackler.player;
  ball.possession.nudgedFrom = attacker.player;
  observePossession(PossessionObservation::NOTE_TACKLE_NUDGE,
                    PossessionId::BINTURONG);
}

void MatchEngine::beginPokedTackleBinturong(const MatchPlayer& defender)
{
  clearPossession();
  ball.possession.lastPossessor = defender.player;
  clearFlightState();
  observePossession(PossessionObservation::BEGIN_POKED_TACKLE,
                    PossessionId::BINTURONG);
}

void MatchEngine::finishPokedTackleBinturong(const MatchPlayer& defender)
{
  ball.possession.history.lastControlledTeamHome = defender.isHomeTeam;
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
  if (player.player && ball.possession.possessedBy == player.player &&
      !(player.isGoalkeeper && !ball.possession.inTouch))
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
  ball.possession.deflectedBy = player.player;
  ball.possession.deflectedPathSince = ball.possession.pathSince;
  ball.possession.deflectedAfter = ball.possession.lastPossessor;
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
  else if (ball.possession.isPass)
  {
    ++stats.passesBlocked;
    ++statsOf(player).blockedPasses;
  }
  observePossession(PossessionObservation::NOTE_BODY_DEFLECTION_HIT,
                    PossessionId::BINTURONG);
}

void MatchEngine::beginBodyDeflectionBinturong(const MatchPlayer& player)
{
  if (ball.possession.inTouch) clearPossession();
  clearFlightState();
  ball.possession.lastPossessor = player.player;
  ball.possession.kicker = player.player;
  observePossession(PossessionObservation::BEGIN_BODY_DEFLECTION,
                    PossessionId::BINTURONG);
}

void MatchEngine::beginShotDeflectionBinturong(const MatchPlayer& player,
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
                    PossessionId::BINTURONG);
}

void MatchEngine::endDeflectedShotFlightBinturong()
{
  ball.possession.isShot = false;
  ball.possession.shotOnTarget = false;
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
  ball.possession.shotBlocked = true;
  if (ball.possession.shotFromDirectFreeKick) ++stats.directFreeKicksBlocked;
  ++(offBody ? statsOf(player).blockedShots : statsOf(player).clearances);
  logEvent(MatchEventType::SHOT_BLOCKED, player);
  return;
}

void MatchEngine::noteFailedTouchBinturong(const MatchPlayer& player)
{
  ball.possession.lastPossessor = player.player;
  ball.possession.kicker = player.player;
  ball.kickerLockout = MatchTuning::Ball::FAILED_TOUCH_LOCKOUT;
  ball.touchAttempts = 0;
  observePossession(PossessionObservation::NOTE_FAILED_TOUCH,
                    PossessionId::BINTURONG);
}

void MatchEngine::beginKeeperHoldBinturong(Point3F from)
{
  ball.possession.footControlled = false;
  ball.possession.footControlSeparationSeconds = 0.0f;
  ball.possession.claimUncontrolledSeconds = 0.0f;
  ball.possession.claimUnrecoverableSeconds = 0.0f;
  ball.possession.inKeeperHands = true;
  ball.possession.heldFrom = from;
  ball.possession.heldSince = getSimulatedSeconds();
  observePossession(PossessionObservation::BEGIN_KEEPER_HOLD,
                    PossessionId::BINTURONG);
}

void MatchEngine::rebaseKeeperHoldBinturong(Point3F from)
{
  ball.possession.heldFrom = from;
  ball.possession.heldSince = getSimulatedSeconds();
  observePossession(PossessionObservation::REBASE_KEEPER_HOLD,
                    PossessionId::BINTURONG);
}

void MatchEngine::endKeeperHoldBinturong()
{
  ball.possession.inKeeperHands = false;
  observePossession(PossessionObservation::END_KEEPER_HOLD,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteKeeperDistributionBinturong(bool lofted, bool overarm)
{
  ball.possession.heldRelease = lofted    ? HeldRelease::PUNT
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
  if (ball.possession.inTouch) clearPossession();
  clearFlightState();
  ball.possession.lastPossessor = keeper.player;
  ball.possession.kicker = keeper.player;
  ball.kickerLockout = MatchTuning::ShotBlock::RECOVERY_BODY_SECONDS;
  ball.touchAttempts = 0;
  notePathChange();
  observePossession(PossessionObservation::BEGIN_KEEPER_HAND_REBOUND,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteKeeperShotResolvedBinturong()
{
  ball.possession.shotSaveResolved = true;
  observePossession(PossessionObservation::NOTE_KEEPER_SHOT_RESOLVED,
                    PossessionId::BINTURONG);
}

void MatchEngine::finishKeeperParryBinturong(const MatchPlayer& keeper)
{
  ball.possession.isShot = false;
  ball.possession.shotOnTarget = false;
  ball.possession.lastPossessor = keeper.player;
  observePossession(PossessionObservation::FINISH_KEEPER_PARRY,
                    PossessionId::BINTURONG);
}

void MatchEngine::creditKeeperSaveBinturong(MatchPlayer& keeper)
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
  ball.possession.lastPossessor = player.player;
  clearFlightState();
  notePathChange();
  observePossession(PossessionObservation::BEGIN_LEGACY_KEEPER_PUNCH,
                    PossessionId::BINTURONG);
}

void MatchEngine::finishLegacyKeeperPunchBinturong(const MatchPlayer& player)
{
  ball.possession.kicker = player.player;
  ball.kickerLockout = MatchTuning::Ball::FAILED_TOUCH_LOCKOUT;
  ball.touchAttempts = 0;
  observePossession(PossessionObservation::FINISH_LEGACY_KEEPER_PUNCH,
                    PossessionId::BINTURONG);
}

void MatchEngine::rearmShotAfterBounceBinturong(float targetY, float height)
{
  ball.possession.shotOnTarget = true;
  ball.possession.shotSaveResolved = false;
  ball.possession.shotTargetY = targetY;
  ball.possession.shotTargetHeightMetres = height;
  observePossession(PossessionObservation::REARM_SHOT_AFTER_BOUNCE,
                    PossessionId::BINTURONG);
}

void MatchEngine::endSpentKeeperShotBinturong()
{
  ball.possession.isShot = false;
  ball.possession.shotOnTarget = false;
  ball.touchAttempts = 0;
  observePossession(PossessionObservation::END_SPENT_KEEPER_SHOT,
                    PossessionId::BINTURONG);
}

void MatchEngine::assignRestartOwnerBinturong(const Player* owner)
{
  handBallTo(owner);
  ball.possession.lastPossessor = ball.possession.possessedBy;
  observePossession(PossessionObservation::ASSIGN_RESTART_OWNER,
                    PossessionId::BINTURONG);
}

void MatchEngine::markThrowEntryBinturong(bool entering)
{
  ball.possession.throwEntering = entering;
  observePossession(PossessionObservation::MARK_THROW_ENTRY,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteWoodworkContactBinturong(bool shot)
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
                    PossessionId::BINTURONG);
}

void MatchEngine::markWallStoppedShotBinturong()
{
  ball.possession.isShot = false;
  ball.possession.shotOnTarget = false;
  ball.possession.shotSaveResolved = true;
  observePossession(PossessionObservation::MARK_WALL_STOPPED_SHOT,
                    PossessionId::BINTURONG);
}

void MatchEngine::creditWallBlockerBinturong(MatchPlayer& player)
{
  ball.possession.lastPossessor = player.player;
  ball.possession.kicker = player.player;
  ball.kickerLockout = MatchTuning::Passing::KICKER_LOCKOUT_SECONDS;
  ++statsOf(player).clearances;
  logEvent(MatchEventType::SHOT_BLOCKED, player).detail =
      MatchEventDetail::WALL;
  observePossession(PossessionObservation::CREDIT_WALL_BLOCKER,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteShotSetPieceOriginBinturong(bool setPiece)
{
  ball.possession.shotFromSetPiece = setPiece;
  observePossession(PossessionObservation::NOTE_SHOT_SET_PIECE_ORIGIN,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteDirectFreeKickShotBinturong()
{
  ball.possession.shotFromDirectFreeKick = true;
  observePossession(PossessionObservation::NOTE_DIRECT_FREE_KICK_SHOT,
                    PossessionId::BINTURONG);
}

void MatchEngine::resetShootoutBallBinturong()
{
  const PossessionHistory history = ball.possession.history;
  ball = freshBall();
  ball.possession.history = history;
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
  const PossessionHistory history = ball.possession.history;
  ball = freshBall();
  ball.possession.history = history;
  observePossession(PossessionObservation::RESET_LIFECYCLE_BALL,
                    PossessionId::BINTURONG);
}

void MatchEngine::assignScenarioOwnerBinturong(const MatchPlayer& carrier)
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
  ball.possession.lastPossessor = nullptr;
  ball.possession.kicker = nullptr;
  ball.kickerLockout = 0.0f;
  ball.touchAttempts = 0;
  if (receiver)
  {
    ball.possession.isPass = true;
    ball.possession.intendedReceiver = receiver->player;
    ball.possession.passByHome = receiver->isHomeTeam;
    ball.possession.passTarget = receiver->position;
  }
  observePossession(PossessionObservation::FINISH_FED_BALL,
                    PossessionId::BINTURONG);
}

void MatchEngine::rememberPhaseTeamBinturong(bool homeTeam)
{
  ball.possession.history.lastControlledTeamHome = homeTeam;
  observePossession(PossessionObservation::REMEMBER_PHASE_TEAM,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteCarrierHistoryBinturong(const MatchPlayer& carrier)
{
  ball.possession.lastPossessor = carrier.player;
  observePossession(PossessionObservation::NOTE_CARRIER_HISTORY,
                    PossessionId::BINTURONG);
}

void MatchEngine::substitutePossessionBinturong(bool hadPossession,
                                                PlayerID outgoingId,
                                                const Player* inPlayer)
{
  if (hadPossession) handBallTo(inPlayer);
  if (ball.possession.lastPossessor &&
      ball.possession.lastPossessor->getId() == outgoingId)
    ball.possession.lastPossessor = inPlayer;
  observePossession(PossessionObservation::SUBSTITUTE_POSSESSION,
                    PossessionId::BINTURONG);
}

bool MatchEngine::releaseRemovedPlayerBinturong(const Player* player)
{
  const bool owned = ball.possession.possessedBy == player;
  if (owned) clearPossession();
  observePossession(PossessionObservation::RELEASE_REMOVED_PLAYER,
                    PossessionId::BINTURONG);
  return owned;
}

void MatchEngine::clearShooterHistoryBinturong()
{
  ball.possession.history.lastShooter = nullptr;
  observePossession(PossessionObservation::CLEAR_SHOOTER_HISTORY,
                    PossessionId::BINTURONG);
}

void MatchEngine::setPassFlightTargetBinturong(PitchPoint target)
{
  ball.possession.passTarget =
      PitchPoint{std::clamp(target.x, 0.0f, pitch.atX(1.0f)),
                 std::clamp(target.y, 0.0f, pitch.atY(1.0f))};
  observePossession(PossessionObservation::SET_PASS_FLIGHT_TARGET,
                    PossessionId::BINTURONG);
}

void MatchEngine::noteGoalSideBinturong(bool homeTeam)
{
  ball.possession.shotByHome = homeTeam;
  observePossession(PossessionObservation::NOTE_GOAL_SIDE,
                    PossessionId::BINTURONG);
}

void MatchEngine::prepareShotAssistBinturong(const MatchPlayer& shooter,
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
                    PossessionId::BINTURONG);
}

void MatchEngine::noteContactActorBinturong(const Player* player, float lockout,
                                            bool resetAttempts)
{
  ball.possession.kicker = player;
  ball.kickerLockout = lockout;
  if (resetAttempts) ball.touchAttempts = 0;
  observePossession(PossessionObservation::NOTE_CONTACT_ACTOR,
                    PossessionId::BINTURONG);
}

void MatchEngine::notePathChangeBinturong()
{
  ball.possession.pathSince = ballNow();
  ball.commitMisses = 0;
  observePossession(PossessionObservation::NOTE_PATH_CHANGE,
                    PossessionId::BINTURONG);
}

void MatchEngine::advanceShotFlightClockBinturong(float dt)
{
  ball.possession.shotElapsedSeconds += dt;
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
  ball.possession.isAerialDelivery = motion.isAerialDelivery;
}

void MatchEngine::endAerialDeliveryBinturong()
{
  ball.possession.isAerialDelivery = false;
}

void MatchEngine::noteThrowTeamBinturong(bool homeTeam)
{
  ball.possession.throwByHome = homeTeam;
  observePossession(PossessionObservation::NOTE_THROW_TEAM,
                    PossessionId::BINTURONG);
}
