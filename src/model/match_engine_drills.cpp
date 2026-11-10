// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Scenarios and drills: placing players and the ball for a scenario or a
// sandbox drill, and the drill controls - held movement targets, forced
// shots and passes, a ball kept or fed in, players removed before kickoff
// and derived stats set by hand. Nothing here runs in the game's own
// matches.

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/player.h"

using namespace MatchEngineInternal;

std::optional<MatchPlayer*> MatchEngine::placeScenario(
    const MatchScenario& scenario, MatchState scenarioState,
    float scenarioMatchTime, int scenarioHomeScore, int scenarioAwayScore,
    bool resetCondition)
{
  resetLifecycleBall();
  obstacles.clear();
  state = scenarioState;
  transitionSecondsRemaining = 0.0f;
  matchTimeMinutes = scenarioMatchTime;
  period = scenarioMatchTime > MatchTuning::Timing::HALF_TIME_MINUTE ? 2 : 1;
  keepers = {};
  pendingAdvantage = {};
  homeScore = scenarioHomeScore;
  awayScore = scenarioAwayScore;
  slidersStale = true;
  accumulator = 0.0f;
  lastPassDecision = PassDecision{};
  lastScenarioDecision.best.reset();
  lastScenarioDecision.runnerUp.reset();
  lastScenarioDecision.reason.clear();
  lastScenarioDecision.passUtility = -std::numeric_limits<float>::infinity();
  lastScenarioDecision.shotUtility = -std::numeric_limits<float>::infinity();
  lastScenarioDecision.carryUtility = -std::numeric_limits<float>::infinity();
  lastScenarioDecision.shieldUtility = -std::numeric_limits<float>::infinity();

  ball.position = scenario.ballPosition;
  bool carrierFound = false;
  for (const auto& placement : scenario.players)
  {
    const auto found = std::ranges::find_if(
        players,
        [placement](const MatchPlayer& matchPlayer)
        {
          return matchPlayer.player &&
                 matchPlayer.player->getId() == placement.playerId;
        });
    if (found == players.end()) continue;
    MatchPlayer& matchPlayer = *found;
    matchPlayer.position = placement.position;
    matchPlayer.velocity = {0.0f, 0.0f};
    matchPlayer.basePosition = placement.position;
    matchPlayer.movementTarget = placement.position;
    matchPlayer.isTrapping = false;
    matchPlayer.trapTimer = 0.0f;
    matchPlayer.plantedSeconds = 0.0f;
    matchPlayer.collisionPause = 0.0f;
    matchPlayer.stumbleSeconds = 0.0f;
    matchPlayer.fallen = false;
    matchPlayer.gettingUp = false;
    matchPlayer.downSeconds = 0.0f;
    matchPlayer.bigHitSeconds = 0.0f;
    matchPlayer.rollSeconds = 0.0f;
    matchPlayer.isPressing = false;
    matchPlayer.isMakingRun = false;
    if (resetCondition) matchPlayer.stamina = 1.0f;
    matchPlayer.actionCooldown = 0.0f;
    matchPlayer.tackleCooldown = 0.0f;
    matchPlayer.isMakingRun = placement.makingRun;
    matchPlayer.isInjured = false;
    matchPlayer.diveTimer = 0.0f;
    matchPlayer.isDiving = false;
    carrierFound = carrierFound || placement.playerId == scenario.carrierId;
  }
  // A drill may start with the ball loose (no carrier).
  if (scenario.carrierId == 0) return nullptr;
  if (!carrierFound) return std::nullopt;

  const auto carrier = std::ranges::find_if(
      players,
      [&scenario](const MatchPlayer& matchPlayer)
      {
        return matchPlayer.player &&
               matchPlayer.player->getId() == scenario.carrierId;
      });
  if (carrier == players.end()) return std::nullopt;

  assignScenarioOwner(*carrier);
  return &*carrier;
}

bool MatchEngine::applyScenario(const MatchScenario& scenario,
                                MatchState scenarioState,
                                float scenarioMatchTime, int scenarioHomeScore,
                                int scenarioAwayScore)
{
  if (scenario.players.empty() || state == MatchState::FULL_TIME) return false;
  const std::optional<MatchPlayer*> placed =
      placeScenario(scenario, scenarioState, scenarioMatchTime,
                    scenarioHomeScore, scenarioAwayScore, true);
  if (!placed || !*placed) return false;
  MatchPlayer& carrier = **placed;

  // Make the interpolation baseline equal to the scenario so the snapshot is
  // stable, then evaluate the decision through the normal live path. A
  // carrier under external control (play mode) makes no AI decision: he
  // acts on his controller's input like in a live match.
  captureInterpolationFrame();
  if (!isControlled(carrier)) decideAction(carrier);
  return true;
}

bool MatchEngine::startDrill(const MatchScenario& scenario)
{
  if (scenario.players.empty() || state == MatchState::FULL_TIME) return false;
  // The ball is live at once; nobody decides until the engine steps.
  if (!placeScenario(scenario, MatchState::PLAYING, 0.0f, homeScore, awayScore,
                     false))
    return false;
  if (scenario.carrierInHands)
    if (MatchPlayer* carrier = drillPlayer(scenario.carrierId);
        carrier && getPossessionAttachment() == carrier->player)
      takeInHandsBinturong(*carrier, ball.position, ball.z * MatchTuning::Units::BALL_Z_METRES);
  captureInterpolationFrame();
  return true;
}

bool MatchEngine::removeBeforeKickOff(PlayerID playerId)
{
  if (stepCounter != 0) return false;
  MatchPlayer* player = drillPlayer(playerId);
  if (!player || !player->onPitch) return false;
  // Off the pitch, parked by the dugouts like a player sent off, but with
  // no keeper promoted and no shape changed: he was never in the drill.
  int parked = 0;
  for (const auto& other : players)
    if (other.player && !other.onPitch) ++parked;
  player->onPitch = false;
  player->isGoalkeeper = false;
  player->intent = PlayerIntent::HOLD_SHAPE;
  player->velocity = {0.0f, 0.0f};
  player->position = {MatchTuning::Pitch::CENTRE +
                          (player->isHomeTeam ? -1.0f : 1.0f) *
                              (MatchTuning::Rules::PARKED_PLAYER_OFFSET +
                               static_cast<float>(parked) *
                                   MatchTuning::Rules::PARKED_PLAYER_SPACING),
                      MatchTuning::Pitch::PLAYER_MIN_Y};
  player->movementTarget = player->position;
  player->tacticalTarget = player->position;
  player->targetVelocity = {0.0f, 0.0f};
  player->arriveAtSpeed = false;
  player->routeWaypoints = 0;
  (void)releaseRemovedPlayer(player->player);
  return true;
}

void MatchEngine::setDrillTarget(PlayerID playerId,
                                 std::optional<Vector2F> target, bool urgent,
                                 const std::vector<Vector2F>& route,
                                 float apexOffsetMetres,
                                 float outwardRoomMetres,
                                 std::optional<float> facing, bool atSpeed)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::DRILL_TARGET;
  command.player = playerId;
  command.target = target;
  command.urgent = urgent;
  command.route.assign(route.begin(),
                       route.begin() + std::min<std::ptrdiff_t>(
                                           std::ssize(route), 2));
  command.apexOffset = apexOffsetMetres;
  command.outwardRoom = outwardRoomMetres;
  command.facing = facing;
  command.atSpeed = atSpeed;
  recordCommand(command);
}

std::optional<Vector2F> MatchEngine::getDrillTarget(PlayerID playerId) const
{
  for (const DrillTarget& held : drillTargets)
    if (held.player == playerId) return held.target;
  return std::nullopt;
}

void MatchEngine::forceShot(PlayerID playerId, std::optional<Vector2F> aim,
                            std::optional<float> powerShare, bool firstTime,
                            std::optional<ShotTechnique> technique)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::FORCE_SHOT;
  command.player = playerId;
  command.shotAim = aim;
  command.shotPower = powerShare;
  command.shotFirstTime = firstTime;
  command.shotTechnique = technique;
  recordCommand(command);
}

void MatchEngine::forcePass(PlayerID playerId, PlayerID receiverId,
                            ForcedPassStyle style)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::FORCE_PASS;
  command.player = playerId;
  command.incoming = receiverId;
  command.passStyle = style;
  recordCommand(command);
}

void MatchEngine::forcePassTo(PlayerID playerId, Vector2F spot,
                              ForcedPassStyle style,
                              std::optional<BallSpin> spin,
                              std::optional<float> arrivalSpeed,
                              bool firstTime)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::FORCE_PASS;
  command.player = playerId;
  command.target = spot;
  command.passStyle = style;
  command.passSpin = spin;
  command.passArrival = arrivalSpeed;
  command.passFirstTime = firstTime;
  recordCommand(command);
}

void MatchEngine::forceTackleIntent(PlayerID playerId, TackleIntent intent)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::FORCE_TACKLE_INTENT;
  command.player = playerId;
  command.tackleIntent = intent;
  recordCommand(command);
}

void MatchEngine::forceHeader(PlayerID playerId, ForcedHeaderAim aim,
                              std::optional<Vector2F> spot,
                              std::optional<Vector2F> goalAim, bool knownEarly)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::FORCE_HEADER;
  command.player = playerId;
  command.headerAim = aim;
  command.headerKnownEarly = knownEarly;
  command.target = spot;
  command.shotAim = goalAim;
  recordCommand(command);
}

bool MatchEngine::passHeldFor(PlayerID playerId) const
{
  return pendingStrike && pendingStrike->kind == PendingStrike::Kind::PASS &&
         pendingStrike->striker && pendingStrike->striker->player &&
         pendingStrike->striker->player->getId() == playerId;
}

void MatchEngine::keepBall(PlayerID playerId, bool keep, float urgency)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::KEEP_BALL;
  command.player = playerId;
  command.urgent = keep;
  command.keepUrgency = urgency;
  recordCommand(command);
}

void MatchEngine::feedBall(Vector2F from, Vector2F velocity, float lift,
                           PlayerID receiverId)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::FEED_BALL;
  command.player = receiverId;
  command.target = from;
  command.feedVelocity = velocity;
  command.feedLift = lift;
  recordCommand(command);
}

MatchPlayer* MatchEngine::drillPlayer(PlayerID playerId)
{
  for (MatchPlayer& player : players)
    if (player.player && player.player->getId() == playerId) return &player;
  return nullptr;
}

void MatchEngine::setDerivedStat(PlayerID playerId, DerivedStat stat,
                                 float rating)
{
  if (MatchPlayer* player = drillPlayer(playerId))
  {
    const auto index = static_cast<std::size_t>(stat);
    player->derived[index] = std::clamp(rating, 0.0f, 1.0f);
    player->derivedOverrides |= 1U << index;
    // What is derived from it follows (a set future stat moves the
    // match-engine-only stats built on it).
    deriveStats(*player);
  }
}

void MatchEngine::holdDrillTarget(MatchPlayer& player, const DrillTarget& held)
{
  player.tacticalTarget = held.target;
  player.targetVelocity = {0.0f, 0.0f};
  player.urgentMovement = held.urgent;
  player.arriveAtSpeed = held.atSpeed;
  // His route: the waypoints in order, then the target.
  player.routeWaypoints = static_cast<std::uint8_t>(held.route.size());
  player.routeApexOffset = held.apexOffset;
  player.routeOutwardRoom = held.outwardRoom;
  player.facingRequested = held.facing.has_value();
  player.facingRequest = held.facing.value_or(0.0f);
  if (!held.route.empty())
  {
    player.movementTarget = held.route[0];
    player.nextTarget = held.route.size() > 1 ? held.route[1] : held.target;
  }
}

void MatchEngine::applyDrillTargets()
{
  for (const DrillTarget& held : drillTargets)
    if (MatchPlayer* player = drillPlayer(held.player);
        player && active(*player))
    {
      holdDrillTarget(*player, held);
      if (held.urgent && drillRules.chaseEffort)
        player->intent = PlayerIntent::CLAIM_LOOSE_BALL;
      player->isPressing = false;
      player->isMakingRun = false;
      // His target is the drill's: not the brain's loose-ball chaser.
      player->chasingLooseBall = false;
      player->steersRound = false;
    }
}
