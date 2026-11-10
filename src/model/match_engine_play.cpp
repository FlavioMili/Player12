// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Play mode: the user controls one player (MatchEngine::setControlledPlayer)
// with a stick and buttons. The inputs, recorded and replayed by step; the
// controlled body following the stick; the assisted pass, shot and aim; and
// which player the user switches to. The rest of the match plays on around
// him as usual.

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/player.h"

using namespace MatchEngineInternal;

bool MatchEngine::isControlled(const MatchPlayer& player) const
{
  return controlledIndex && &player == &players[*controlledIndex];
}

void MatchEngine::setPlayHalfMinutes(int minutes)
{
  pendingPlayHalfMinutes =
      minutes == 0
          ? 0
          : std::clamp(minutes, MatchTuning::Timing::MIN_PLAY_HALF_MINUTES,
                       MatchTuning::Timing::MAX_PLAY_HALF_MINUTES);
  const PlayerID player = getControlledPlayer();
  inputLog.resize(inputCursor);
  inputLog.push_back({stepCounter + 1, player, {}, pendingPlayHalfMinutes});
  ++inputRevision;
}

bool MatchEngine::setControlledPlayer(PlayerID playerId)
{
  if (playerId != 0)
  {
    const auto found = std::find_if(
        players.begin(), players.end(), [&](const MatchPlayer& candidate)
        { return active(candidate) && candidate.player->getId() == playerId; });
    if (found == players.end() || found->isGoalkeeper) return false;
  }
  // Scheduled for the next step, like any other input, so a replay applies it
  // at the same moment.
  inputLog.resize(inputCursor);
  inputLog.push_back(
      {stepCounter + 1, playerId, MatchPlayerInput{}, pendingPlayHalfMinutes});
  ++inputRevision;
  return true;
}

PlayerID MatchEngine::getControlledPlayer() const
{
  // A change scheduled for the next step already counts.
  std::optional<PlayerID> scheduled;
  for (std::size_t index = inputCursor;
       index < inputLog.size() && inputLog[index].step <= stepCounter + 1;
       ++index)
    scheduled = inputLog[index].player;
  if (scheduled) return *scheduled;
  return controlledIndex ? players[*controlledIndex].player->getId() : 0;
}

void MatchEngine::submitInput(const MatchPlayerInput& input)
{
  const PlayerID controlled = getControlledPlayer();
  if (controlled == 0) return;
  inputLog.resize(inputCursor);
  inputLog.push_back(
      {stepCounter + 1, controlled, input, pendingPlayHalfMinutes});
  ++inputRevision;
}

void MatchEngine::loadInputReplay(std::vector<MatchInputRecord> log)
{
  std::stable_sort(
      log.begin(), log.end(),
      [](const MatchInputRecord& first, const MatchInputRecord& second)
      { return first.step < second.step; });
  inputLog = std::move(log);
  inputCursor = 0;
  // Records already due (a replay loaded late) are applied on the next step.
  ++inputRevision;
}

void MatchEngine::applyDueInputs()
{
  while (inputCursor < inputLog.size() &&
         inputLog[inputCursor].step <= stepCounter)
  {
    const MatchInputRecord& record = inputLog[inputCursor++];
    playHalfMinutes =
        record.playHalfMinutes == 0
            ? 0
            : std::clamp(record.playHalfMinutes,
                         MatchTuning::Timing::MIN_PLAY_HALF_MINUTES,
                         MatchTuning::Timing::MAX_PLAY_HALF_MINUTES);
    pendingPlayHalfMinutes = playHalfMinutes;
    // An action fires on the press, not while the button stays held.
    const bool pressed = record.input.action != MatchInputAction::NONE &&
                         record.input.action != lastInputAction;
    lastInputAction = record.input.action;
    // A buffered press survives stick updates but not a change of player.
    const bool samePlayer =
        controlledIndex &&
        players[*controlledIndex].player->getId() == record.player;
    const MatchInputAction pending =
        samePlayer ? controlInput.action : MatchInputAction::NONE;
    const float pendingRemaining = samePlayer ? controlActionRemaining : 0.0f;
    controlledIndex.reset();
    controlInput = {};
    controlActionRemaining = 0.0f;
    if (record.player == 0) continue;
    for (std::size_t slot = 0; slot < players.size(); ++slot)
    {
      const MatchPlayer& candidate = players[slot];
      if (active(candidate) && !candidate.isGoalkeeper &&
          candidate.player->getId() == record.player)
      {
        controlledIndex = slot;
        controlledPlayerId = record.player;
        everControlled[candidate.isHomeTeam ? 0 : 1] = true;
        break;
      }
    }
    if (!controlledIndex) continue;
    controlInput = record.input;
    if (pressed)
    {
      controlActionRemaining = MatchTuning::Control::ACTION_BUFFER_SECONDS;
    }
    else
    {
      // A stick update keeps a buffered press alive.
      controlInput.action = pending;
      controlActionRemaining = pendingRemaining;
    }
    // The body keeps its momentum; the AI resumes from where he stands.
    MatchPlayer& player = players[*controlledIndex];
    player.movementTarget = player.position;
  }
}

void MatchEngine::integrateControlled(MatchPlayer& player, float dt)
{
  using C = MatchTuning::Control;
  const float topSpeed = currentTopSpeed(player);
  const float fatigue = fuelFatigue(player);
  const MatchPlayer* carrier = findMatchPlayer(getPossessionAttachment());
  const bool jockeying = controlInput.jockey && carrier &&
                         carrier->isHomeTeam != player.isHomeTeam;
  float runSpeed =
      controlInput.sprint
          ? topSpeed
          : std::min(topSpeed, player.maxSpeed * C::JOG_SPEED_SHARE);
  if (jockeying)
    runSpeed = std::min(runSpeed, player.maxSpeed * C::JOCKEY_SPEED_SHARE);
  float magnitude = std::sqrt(controlInput.moveX * controlInput.moveX +
                              controlInput.moveY * controlInput.moveY);
  Vector2F direction{0.0f, 0.0f};
  if (std::isfinite(magnitude) && magnitude > EPSILON)
  {
    direction = {controlInput.moveX / magnitude,
                 controlInput.moveY / magnitude};
    magnitude = std::min(magnitude, 1.0f);
  }
  else
  {
    magnitude = 0.0f;
    // With the stick idle he still meets a pass played to him and, while
    // jockeying, keeps his goal-side spot; he arrives without overshooting.
    if (const auto assist = controlledAssistTarget(player))
    {
      const Vector2F offset = toMetres(
          {assist->x - player.position.x, assist->y - player.position.y});
      const float metres = length(offset);
      if (metres > MatchTuning::Player::ARRIVAL_DEAD_ZONE_METRES &&
          runSpeed > EPSILON)
      {
        direction = {offset.x / metres, offset.y / metres};
        magnitude = std::min(
            1.0f, std::sqrt(2.0f * MatchTuning::Player::ARRIVAL_DECELERATION *
                            metres) /
                      runSpeed);
      }
    }
  }
  const float desiredSpeed = magnitude * runSpeed;
  const Vector2F desired{direction.x * desiredSpeed,
                         direction.y * desiredSpeed};
  moveControlled(player, desired, desiredSpeed, topSpeed, fatigue, dt,
                 jockeying ? carrier : nullptr);
  // When the AI takes him back it picks up the run where his momentum
  // carries him instead of braking to the spot he stands on.
  const Vector2F carry =
      toPitch({player.velocity.x * C::RELEASE_MOMENTUM_SECONDS,
               player.velocity.y * C::RELEASE_MOMENTUM_SECONDS});
  player.movementTarget = {
      clampToPitchX(player.position.x + carry.x),
      clampToPitchY(player.position.y + carry.y)};
}

std::optional<Vector2F> MatchEngine::controlledAssistTarget(
    const MatchPlayer& player) const
{
  using C = MatchTuning::Control;
  if (getPossessionState().isPass && !getPossessionAttachment() &&
      getExpectedReceiver() == player.player)
  {
    // The point of the ball's path nearest to him, but never behind a
    // moment's travel of the ball: he steps toward it to meet it.
    const Vector2F ballMetres = toMetres(ball.position);
    const Vector2F self = toMetres(player.position);
    const float speedSquared =
        ball.velocity.x * ball.velocity.x + ball.velocity.y * ball.velocity.y;
    float seconds = C::MEET_PASS_LOOKAHEAD_SECONDS;
    if (speedSquared > EPSILON)
      seconds =
          std::clamp(((self.x - ballMetres.x) * ball.velocity.x +
                      (self.y - ballMetres.y) * ball.velocity.y) /
                         speedSquared,
                     C::MEET_PASS_LOOKAHEAD_SECONDS, C::SWITCH_HORIZON_SECONDS);
    const Vector2F ahead =
        toPitch({ball.velocity.x * seconds, ball.velocity.y * seconds});
    return Vector2F{
        clampToPitchX(ball.position.x + ahead.x),
        clampToPitchY(ball.position.y + ahead.y)};
  }
  if (!controlInput.jockey || !getPossessionAttachment()) return std::nullopt;
  for (const MatchPlayer& carrier : players)
  {
    if (carrier.player != getPossessionAttachment()) continue;
    if (carrier.isHomeTeam == player.isHomeTeam) return std::nullopt;
    // Goal-side of the carrier, between him and the centre of our goal.
    const Vector2F toGoal = metricDirection(
        carrier.position,
        {player.isHomeTeam ? 0.0f : 1.0f, MatchTuning::Pitch::CENTRE});
    const Vector2F gap = toPitch({toGoal.x * C::JOCKEY_CONTAIN_METRES,
                                  toGoal.y * C::JOCKEY_CONTAIN_METRES});
    return Vector2F{carrier.position.x + gap.x, carrier.position.y + gap.y};
  }
  return std::nullopt;
}

Vector2F MatchEngine::controlledAim(const MatchPlayer& carrier) const
{
  // The aim stick, else the run stick, else the way he runs or faces.
  Vector2F aim{controlInput.aimX, controlInput.aimY};
  if (aim.x * aim.x + aim.y * aim.y <= EPSILON)
    aim = {controlInput.moveX, controlInput.moveY};
  if (aim.x * aim.x + aim.y * aim.y <= EPSILON) aim = carrier.velocity;
  if (aim.x * aim.x + aim.y * aim.y <= EPSILON)
    aim = {std::cos(carrier.facingAngle) * MatchTuning::Pitch::LENGTH_METRES,
           std::sin(carrier.facingAngle) * MatchTuning::Pitch::WIDTH_METRES};
  return normalized(aim);
}

bool MatchEngine::performControlledAction(MatchPlayer& carrier)
{
  using C = MatchTuning::Control;
  const MatchInputAction action = controlInput.action;
  if (action != MatchInputAction::PASS &&
      action != MatchInputAction::LOFTED_PASS &&
      action != MatchInputAction::THROUGH_BALL &&
      action != MatchInputAction::SHOOT && action != MatchInputAction::CLEAR)
    return false;
  controlInput.action = MatchInputAction::NONE;
  ++controlledActions[carrier.isHomeTeam ? 0 : 1];
  if (action == MatchInputAction::SHOOT)
  {
    // The stick picks the spot on the goal line; his finishing, the
    // pressure and the power still decide where the ball really goes.
    controlledShot = {};
    controlledShot.active = true;
    controlledShot.power = controlInput.power > 0.0f
                               ? std::clamp(controlInput.power, 0.0f, 1.0f)
                               : C::DEFAULT_POWER;
    Vector2F aim{controlInput.aimX, controlInput.aimY};
    if (aim.x * aim.x + aim.y * aim.y <= EPSILON)
      aim = {controlInput.moveX, controlInput.moveY};
    aim = normalized(aim);
    const float forward = carrier.isHomeTeam ? 1.0f : -1.0f;
    // Only a stick pointing at least roughly at the goal aims the shot.
    constexpr float MIN_GOALWARD_AIM = 0.2f;
    if (aim.x * forward > MIN_GOALWARD_AIM)
    {
      const Vector2F from = toMetres(carrier.position);
      const float goalLine = carrier.isHomeTeam ? PITCH_LENGTH : 0.0f;
      const float crossing = from.y + aim.y * (goalLine - from.x) / aim.x;
      controlledShot.aimMetres =
          crossing - MatchTuning::Pitch::CENTRE * PITCH_WIDTH;
    }
    takeShot(carrier);
    controlledShot = {};
    return true;
  }
  if (action == MatchInputAction::CLEAR)
  {
    clearBall(carrier);
    return true;
  }
  playControlledPass(carrier, action);
  return true;
}

MatchPlayer* MatchEngine::controlledPassReceiver(MatchPlayer& carrier,
                                                 Vector2F aim, bool throughBall)
{
  using C = MatchTuning::Control;
  const std::size_t assist = std::min<std::size_t>(
      controlInput.passAssist, C::ASSIST_MIN_ALIGNMENT.size() - 1);
  const float minAlignment = C::ASSIST_MIN_ALIGNMENT[assist];
  const float completionWeight = C::ASSIST_COMPLETION_WEIGHT[assist];
  const float forward = carrier.isHomeTeam ? 1.0f : -1.0f;
  MatchPlayer* receiver = nullptr;
  float bestScore = -std::numeric_limits<float>::infinity();
  for (auto& candidate : players)
  {
    if (&candidate == &carrier || candidate.isHomeTeam != carrier.isHomeTeam ||
        !active(candidate) || (throughBall && candidate.isGoalkeeper))
      continue;
    const Vector2F offset =
        toMetres({candidate.position.x - carrier.position.x,
                  candidate.position.y - carrier.position.y});
    const float separation = length(offset);
    if (separation <= EPSILON) continue;
    const float alignment = (offset.x * aim.x + offset.y * aim.y) / separation;
    if (alignment < minAlignment) continue;
    // Mild assistance: among the team-mates near the aim, the one the ball
    // is likelier to reach. The aim still dominates.
    float score = alignment - separation * C::PASS_DISTANCE_WEIGHT;
    if (completionWeight > 0.0f)
      score += completionWeight *
               evaluatePassOption(carrier, candidate).completionProbability;
    if (throughBall)
      score += std::max(0.0f, offset.x * forward) * C::THROUGH_PROGRESS_WEIGHT +
               (candidate.isMakingRun ? C::THROUGH_RUNNER_BONUS : 0.0f);
    if (score > bestScore)
    {
      bestScore = score;
      receiver = &candidate;
    }
  }
  return receiver;
}

void MatchEngine::playControlledPass(MatchPlayer& carrier,
                                     MatchInputAction action)
{
  using C = MatchTuning::Control;
  using Pitch = MatchTuning::Pitch;
  const Vector2F aim = controlledAim(carrier);
  const bool through = action == MatchInputAction::THROUGH_BALL;
  const bool lofted = action == MatchInputAction::LOFTED_PASS;
  const float power = controlInput.power > 0.0f
                          ? std::clamp(controlInput.power, 0.0f, 1.0f)
                          : C::DEFAULT_POWER;
  const float forward = carrier.isHomeTeam ? 1.0f : -1.0f;
  if (MatchPlayer* receiver = controlledPassReceiver(carrier, aim, through))
  {
    PassOption option = evaluatePassOption(carrier, *receiver);
    if (through)
    {
      // Into the space ahead of his run; a standing receiver gets it ahead
      // of him between the stick and the goal.
      Vector2F run = normalized(receiver->velocity);
      if (length(receiver->velocity) <
          MatchTuning::Player::MOVEMENT_FACING_THRESHOLD)
        run = normalized({aim.x + forward, aim.y});
      const float lead =
          C::THROUGH_LEAD_MIN_METRES +
          (C::THROUGH_LEAD_MAX_METRES - C::THROUGH_LEAD_MIN_METRES) * power;
      const Vector2F reach = toPitch({run.x * lead, run.y * lead});
      option.targetPoint = {
          clampToPitchX(receiver->position.x + reach.x),
          clampToPitchY(receiver->position.y + reach.y)};
      option.passDistance = distance(carrier.position, option.targetPoint);
      option.intent = PassIntent::THROUGH_BALL;
      option.lofted = false;
    }
    passBall(carrier, option, lofted);
    return;
  }
  // Nobody near the aim: the ball is played into space along the stick, for
  // the team-mate nearest the spot to chase. It is still the passer's
  // technique that decides how true it runs.
  const float metres =
      lofted
          ? C::SPACE_LOFT_MIN_METRES +
                (C::SPACE_LOFT_MAX_METRES - C::SPACE_LOFT_MIN_METRES) * power
          : C::SPACE_PASS_MIN_METRES +
                (C::SPACE_PASS_MAX_METRES - C::SPACE_PASS_MIN_METRES) * power;
  const Vector2F reach = toPitch({aim.x * metres, aim.y * metres});
  const Vector2F target{clampToPitchX(carrier.position.x + reach.x),
                        clampToPitchY(carrier.position.y + reach.y)};
  MatchPlayer* chaser = nullptr;
  float nearest = std::numeric_limits<float>::infinity();
  for (auto& candidate : players)
  {
    if (&candidate == &carrier || candidate.isHomeTeam != carrier.isHomeTeam ||
        !active(candidate) || candidate.isGoalkeeper)
      continue;
    const float metresAway = distance(candidate.position, target);
    if (metresAway < nearest)
    {
      nearest = metresAway;
      chaser = &candidate;
    }
  }
  if (!chaser)
  {
    clearBall(carrier);
    return;
  }
  PassOption option = evaluatePassOption(carrier, *chaser);
  option.targetPoint = target;
  option.passDistance = distance(carrier.position, target);
  option.progression = (target.x - carrier.position.x) * forward;
  option.intent =
      option.progression >= MatchTuning::Passing::PROGRESSIVE_PASS_MINIMUM
          ? PassIntent::PROGRESSIVE
          : PassIntent::RECYCLE;
  option.lofted = lofted || option.passDistance >
                                MatchTuning::Passing::LOFTED_DISTANCE_METRES;
  passBall(carrier, option, lofted);
}

void MatchEngine::shapeControlledShot(const MatchPlayer& shooter,
                                      float halfGoal, float inset, float& aimY,
                                      float& aimZ, float& spread) const
{
  using C = MatchTuning::Control;
  if (!controlledShot.active || !isControlled(shooter)) return;
  if (controlledShot.aimMetres)
  {
    const float inside = std::max(0.0f, halfGoal - inset);
    aimY = std::clamp(*controlledShot.aimMetres, -inside, inside);
  }
  // A full bar is hit hard but is harder to keep down and on target.
  const float excess =
      std::clamp((controlledShot.power - C::SHOT_OVERPOWER_FROM) /
                     (1.0f - C::SHOT_OVERPOWER_FROM),
                 0.0f, 1.0f);
  aimZ += excess * C::SHOT_OVERPOWER_LIFT_METRES;
  spread *= 1.0f + excess * C::SHOT_OVERPOWER_SPREAD;
}

float MatchEngine::switchSeconds(const MatchPlayer& candidate) const
{
  using C = MatchTuning::Control;
  // Where the ball goes: with its carrier on his run, or rolling to a stop.
  const MatchPlayer* carrier = nullptr;
  if (getPossessionAttachment())
    for (const MatchPlayer& player : players)
      if (player.player == getPossessionAttachment()) carrier = &player;
  const Vector2F start = toMetres(carrier ? carrier->position : ball.position);
  const Vector2F velocity = carrier ? carrier->velocity : ball.velocity;
  const float speed = length(velocity);
  const Vector2F heading =
      speed > EPSILON ? Vector2F{velocity.x / speed, velocity.y / speed}
                      : Vector2F{0.0f, 0.0f};
  const Vector2F self = toMetres(candidate.position);
  const float topSpeed = std::max(currentTopSpeed(candidate), 1.0f);
  const float stopSeconds = carrier ? std::numeric_limits<float>::infinity()
                                    : speed / C::SWITCH_BALL_DECELERATION;
  constexpr int SAMPLES = static_cast<int>(
      C::SWITCH_HORIZON_SECONDS / C::SWITCH_SAMPLE_SECONDS + 0.5f);
  float best = std::numeric_limits<float>::infinity();
  for (int sample = 0; sample <= SAMPLES; ++sample)
  {
    const float seconds = static_cast<float>(sample) * C::SWITCH_SAMPLE_SECONDS;
    const float rolling = std::min(seconds, stopSeconds);
    const float travelled =
        carrier ? speed * seconds
                : speed * rolling -
                      0.5f * C::SWITCH_BALL_DECELERATION * rolling * rolling;
    const Vector2F point{
        std::clamp(start.x + heading.x * travelled, 0.0f, PITCH_LENGTH),
        std::clamp(start.y + heading.y * travelled, 0.0f, PITCH_WIDTH)};
    const float reach = C::SWITCH_REACTION_SECONDS +
                        length({point.x - self.x, point.y - self.y}) / topSpeed;
    best = std::min(best, std::max(seconds, reach));
    // Reachable in time here: no later point can be reached sooner.
    if (reach <= seconds) break;
  }
  // Against a carrier, a defender already goal-side is the one to engage.
  if (carrier && carrier->isHomeTeam != candidate.isHomeTeam &&
      (candidate.isHomeTeam ? candidate.position.x < carrier->position.x
                            : candidate.position.x > carrier->position.x))
    best -= C::SWITCH_GOAL_SIDE_BONUS_SECONDS;
  return best;
}

PlayerID MatchEngine::suggestActivePlayer(bool homeTeam, PlayerID current) const
{
  const auto eligible = [homeTeam](const MatchPlayer& player)
  {
    return active(player) && !player.isGoalkeeper &&
           player.isHomeTeam == homeTeam;
  };
  const MatchPlayer* currentPlayer = nullptr;
  for (const MatchPlayer& player : players)
  {
    if (!eligible(player)) continue;
    if (player.player->getId() == current) currentPlayer = &player;
    // The side's carrier, or the receiver of its pass, is the man.
    if (player.player == getPossessionAttachment() ||
        (!getPossessionAttachment() && getPossessionState().isPass && getPossessionState().passByHome == homeTeam &&
         player.player == getExpectedReceiver()))
      return player.player->getId();
  }
  // The side's keeper has the ball: nobody to chase it, stay with the man.
  if (currentPlayer && getPossessionAttachment())
    for (const MatchPlayer& player : players)
      if (player.player == getPossessionAttachment() && player.isHomeTeam == homeTeam)
        return current;
  PlayerID best = 0;
  float bestSeconds = std::numeric_limits<float>::infinity();
  for (const MatchPlayer& player : players)
  {
    if (!eligible(player)) continue;
    float seconds = switchSeconds(player);
    if (&player == currentPlayer)
      seconds -= MatchTuning::Control::SWITCH_CURRENT_PREFERENCE_SECONDS;
    if (seconds < bestSeconds)
    {
      bestSeconds = seconds;
      best = player.player->getId();
    }
  }
  return best;
}

PlayerID MatchEngine::nextSwitchCandidate(bool homeTeam, PlayerID current,
                                          PlayerID skip) const
{
  PlayerID best = 0;
  PlayerID skipped = 0;
  float bestSeconds = std::numeric_limits<float>::infinity();
  float skippedSeconds = std::numeric_limits<float>::infinity();
  for (const MatchPlayer& player : players)
  {
    if (!active(player) || player.isGoalkeeper ||
        player.isHomeTeam != homeTeam || player.player->getId() == current)
      continue;
    const float seconds = switchSeconds(player);
    if (player.player->getId() == skip)
    {
      skipped = skip;
      skippedSeconds = seconds;
    }
    else if (seconds < bestSeconds)
    {
      bestSeconds = seconds;
      best = player.player->getId();
    }
  }
  // Only the skipped player is left: take him rather than nobody.
  return best != 0 || skippedSeconds == std::numeric_limits<float>::infinity()
             ? best
             : skipped;
}

bool MatchEngine::stepsWithin(float deltaSeconds) const
{
  if (state == MatchState::FULL_TIME || !std::isfinite(deltaSeconds) ||
      deltaSeconds <= 0.0f)
    return false;
  return accumulator +
             std::min(deltaSeconds,
                      MatchTuning::Timing::MAX_FRAME_DELTA_SECONDS) +
             EPSILON >=
         MatchTuning::Timing::FIXED_STEP_SECONDS;
}
