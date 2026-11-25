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
  kit.clear();
  state = scenarioState;
  transitionSecondsRemaining = 0.0F;
  matchTimeMinutes = scenarioMatchTime;
  period = scenarioMatchTime > MatchTuning::Timing::HALF_TIME_MINUTE ? 2 : 1;
  keepers = {};
  pendingAdvantage = {};
  homeScore = scenarioHomeScore;
  awayScore = scenarioAwayScore;
  slidersStale = true;
  accumulator = 0.0F;
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
    if (found == players.end())
    {
      continue;
    }
    MatchPlayer& matchPlayer = *found;
    matchPlayer.position = placement.position;
    matchPlayer.velocity = {.x = 0.0F, .y = 0.0F};
    if (placement.facing)
    {
      matchPlayer.facingAngle = *placement.facing;
      matchPlayer.targetAngle = *placement.facing;
    }
    matchPlayer.basePosition = placement.position;
    // A drill with no layers gives him no place in a formation either.
    if (!drillRules.layers)
    {
      matchPlayer.formationSlot = -1;
    }
    matchPlayer.movementTarget = placement.position;
    matchPlayer.isTrapping = false;
    matchPlayer.trapTimer = 0.0F;
    matchPlayer.plantedSeconds = 0.0F;
    matchPlayer.collisionPause = 0.0F;
    matchPlayer.stumbleSeconds = 0.0F;
    matchPlayer.fallen = false;
    matchPlayer.gettingUp = false;
    matchPlayer.downSeconds = 0.0F;
    matchPlayer.bigHitSeconds = 0.0F;
    matchPlayer.rollSeconds = 0.0F;
    matchPlayer.isPressing = false;
    matchPlayer.isMakingRun = false;
    if (resetCondition)
    {
      matchPlayer.stamina = 1.0F;
    }
    matchPlayer.actionCooldown = 0.0F;
    matchPlayer.tackleCooldown = 0.0F;
    matchPlayer.isMakingRun = placement.makingRun;
    matchPlayer.isInjured = false;
    matchPlayer.diveTimer = 0.0F;
    matchPlayer.isDiving = false;
    carrierFound = carrierFound || placement.playerId == scenario.carrierId;
  }
  // A drill may start with the ball loose (no carrier).
  if (scenario.carrierId == 0)
  {
    return nullptr;
  }
  if (!carrierFound)
  {
    return std::nullopt;
  }

  const auto carrier = std::ranges::find_if(
      players,
      [&scenario](const MatchPlayer& matchPlayer)
      {
        return matchPlayer.player &&
               matchPlayer.player->getId() == scenario.carrierId;
      });
  if (carrier == players.end())
  {
    return std::nullopt;
  }

  assignScenarioOwner(*carrier);
  return &*carrier;
}

bool MatchEngine::applyScenario(const MatchScenario& scenario,
                                MatchState scenarioState,
                                float scenarioMatchTime, int scenarioHomeScore,
                                int scenarioAwayScore)
{
  if (scenario.players.empty() || state == MatchState::FULL_TIME)
  {
    return false;
  }
  const std::optional<MatchPlayer*> placed =
      placeScenario(scenario, scenarioState, scenarioMatchTime,
                    scenarioHomeScore, scenarioAwayScore, true);
  if (!placed || (*placed == nullptr))
  {
    return false;
  }
  MatchPlayer& carrier = **placed;

  // Make the interpolation baseline equal to the scenario so the snapshot is
  // stable, then evaluate the decision through the normal live path. A
  // carrier under external control (play mode) makes no AI decision: he
  // acts on his controller's input like in a live match.
  captureInterpolationFrame();
  if (!isControlled(carrier))
  {
    decideAction(carrier);
  }
  return true;
}

bool MatchEngine::startDrill(const MatchScenario& scenario)
{
  if (scenario.players.empty() || state == MatchState::FULL_TIME)
  {
    return false;
  }
  // The ball is live at once; nobody decides until the engine steps.
  if (!placeScenario(scenario, MatchState::PLAYING, 0.0F, homeScore, awayScore,
                     false))
  {
    return false;
  }
  if (scenario.carrierInHands)
  {
    if (MatchPlayer* carrier = drillPlayer(scenario.carrierId);
        (carrier != nullptr) && getPossessionAttachment() == carrier->player)
    {
      takeInHandsBinturong(*carrier, ball.position, ball.z);
    }
  }
  captureInterpolationFrame();
  return true;
}

bool MatchEngine::removeBeforeKickOff(PlayerID playerId)
{
  if (stepCounter != 0)
  {
    return false;
  }
  MatchPlayer* player = drillPlayer(playerId);
  if ((player == nullptr) || !player->onPitch)
  {
    return false;
  }
  // Off the pitch, parked by the dugouts like a player sent off, but with
  // no keeper promoted and no shape changed: he was never in the drill.
  int parked = 0;
  for (const auto& other : players)
  {
    if ((other.player != nullptr) && !other.onPitch)
    {
      ++parked;
    }
  }
  player->onPitch = false;
  player->isGoalkeeper = false;
  player->intent = PlayerIntent::HOLD_SHAPE;
  player->velocity = {.x = 0.0F, .y = 0.0F};
  player->position =
      PitchPoint{pitch.atX(MatchTuning::Pitch::CENTRE) +
                     ((player->isHomeTeam ? -1.0F : 1.0F) *
                      (MatchTuning::Rules::PARKED_PLAYER_OFFSET_METRES +
                       static_cast<float>(parked) *
                           MatchTuning::Rules::PARKED_PLAYER_SPACING_METRES)),
                 MatchTuning::Pitch::PLAYER_EDGE_SIDE_METRES};
  player->movementTarget = player->position;
  player->tacticalTarget = player->position;
  player->targetVelocity = {.x = 0.0F, .y = 0.0F};
  player->arriveAtSpeed = false;
  player->routeWaypoints = 0;
  (void)releaseRemovedPlayer(player->player);
  return true;
}

void MatchEngine::setDrillTarget(PlayerID playerId,
                                 std::optional<PitchPoint> target, bool urgent,
                                 const std::vector<PitchPoint>& route,
                                 float apexOffsetMetres,
                                 float outwardRoomMetres,
                                 std::optional<float> facing, bool atSpeed)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::DRILL_TARGET;
  command.player = playerId;
  command.target = target;
  command.urgent = urgent;
  command.route.assign(
      route.begin(),
      route.begin() + std::min<std::ptrdiff_t>(std::ssize(route), 2));
  command.apexOffset = apexOffsetMetres;
  command.outwardRoom = outwardRoomMetres;
  command.facing = facing;
  command.atSpeed = atSpeed;
  recordCommand(command);
}

std::optional<PitchPoint> MatchEngine::getDrillTarget(PlayerID playerId) const
{
  for (const DrillTarget& held : drillTargets)
  {
    if (held.player == playerId)
    {
      return held.target;
    }
  }
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

bool MatchEngine::strikeStillBall(PlayerID playerId)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::STRIKE_STILL_BALL;
  command.player = playerId;
  return recordCommand(command);
}

std::optional<PitchPoint> MatchEngine::keeperSetPosition(
    PlayerID keeperId, PitchPoint threat) const
{
  for (const MatchPlayer& player : players)
  {
    if ((player.player != nullptr) && player.player->getId() == keeperId)
    {
      return keeperSetPositionCapybara(player, threat);
    }
  }
  return std::nullopt;
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

void MatchEngine::forcePassTo(PlayerID playerId, PitchPoint spot,
                              ForcedPassStyle style,
                              std::optional<BallSpin> spin,
                              std::optional<float> arrivalSpeed, bool firstTime)
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
                              std::optional<PitchPoint> spot,
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
         (pendingStrike->striker != nullptr) &&
         (pendingStrike->striker->player != nullptr) &&
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

void MatchEngine::feedBall(PitchPoint from, Vector2F velocity, float lift,
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
  {
    if ((player.player != nullptr) && player.player->getId() == playerId)
    {
      return &player;
    }
  }
  return nullptr;
}

void MatchEngine::setDerivedStat(PlayerID playerId, DerivedStat stat,
                                 float rating)
{
  if (MatchPlayer* player = drillPlayer(playerId))
  {
    const auto index = static_cast<std::size_t>(stat);
    player->derived[index] = std::clamp(rating, 0.0F, 1.0F);
    player->derivedOverrides |= std::uint64_t{1} << index;
    // What is derived from it follows (a set future stat moves the
    // match-engine-only stats built on it).
    deriveStats(*player);
  }
}

void MatchEngine::holdDrillTarget(MatchPlayer& player, const DrillTarget& held)
{
  player.tacticalTarget = held.target;
  player.targetVelocity = {.x = 0.0F, .y = 0.0F};
  player.urgentMovement = held.urgent;
  player.arriveAtSpeed = held.atSpeed;
  // His route: the waypoints in order, then the target.
  player.routeWaypoints = static_cast<std::uint8_t>(held.route.size());
  player.routeApexOffset = held.apexOffset;
  player.routeOutwardRoom = held.outwardRoom;
  player.facingRequested = held.facing.has_value();
  player.facingRequest = held.facing.value_or(0.0F);
  if (!held.route.empty())
  {
    player.movementTarget = held.route[0];
    player.nextTarget = held.route.size() > 1 ? held.route[1] : held.target;
  }
}

void MatchEngine::applyDrillTargets()
{
  for (const DrillTarget& held : drillTargets)
  {
    if (MatchPlayer* player = drillPlayer(held.player);
        (player != nullptr) && active(*player))
    {
      holdDrillTarget(*player, held);
      if (held.urgent && drillRules.chaseEffort)
      {
        player->intent = PlayerIntent::CLAIM_LOOSE_BALL;
      }
      player->isPressing = false;
      player->isMakingRun = false;
      // His target is the drill's: not the brain's loose-ball chaser.
      player->chasingLooseBall = false;
      player->steersRound = false;
    }
  }
}

std::optional<MatchEngine::CarriedPlayer> MatchEngine::carriedPlayer(
    PlayerID playerId) const
{
  for (const MatchPlayer& player : players)
  {
    if ((player.player == nullptr) || player.player->getId() != playerId ||
        !player.onPitch)
    {
      continue;
    }
    CarriedPlayer carried;
    carried.id = playerId;
    carried.position = player.position;
    carried.velocity = player.velocity;
    carried.facing = player.facingAngle;
    carried.stamina = player.stamina;
    carried.sprintReserve = player.sprintReserve;
    carried.fastBurstDebt = player.fastBurstDebt;
    carried.loadSpeed = player.loadSpeed;
    return carried;
  }
  return std::nullopt;
}

bool MatchEngine::carryPlayerIn(const CarriedPlayer& carried)
{
  MatchPlayer* player = drillPlayer(carried.id);
  if ((player == nullptr) || !player->onPitch)
  {
    return false;
  }
  // Where he is and how he moves (kept inside this pitch), and his
  // condition; his targets start where he is, for his brain to replan.
  player->position = moveBody(player->position, carried.position);
  player->velocity = carried.velocity;
  player->facingAngle = carried.facing;
  player->targetAngle = carried.facing;
  player->movementTarget = player->position;
  player->tacticalTarget = player->position;
  player->stamina = carried.stamina;
  player->sprintReserve = carried.sprintReserve;
  player->fastBurstDebt = carried.fastBurstDebt;
  player->loadSpeed = carried.loadSpeed;
  statsOf(*player).condition = player->stamina;
  captureInterpolationFrame();
  return true;
}

bool MatchEngine::takeOffForDrill(PlayerID playerId)
{
  MatchPlayer* player = drillPlayer(playerId);
  if ((player == nullptr) || !active(*player))
  {
    return false;
  }
  removeFromPitch(*player, false);
  return true;
}
