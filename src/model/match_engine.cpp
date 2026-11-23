// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/match_engine.h"

#include "model/match_possession_control.h"
#include "model/match_possession_coordination.h"
#include "model/match_restart_approach.h"

// Implementation map (search these function names to find each subsystem):
// - Construction/loadAttributes: borrow career players, cache match attributes.
// - simulateStepBody: order commands, movement, actions, physics and rules.
// - refreshTacticalTargets: coordinate each team's off-ball intentions.
// - integrateMovements/integrateControlled: turn intentions into body motion.
// - decideAction/evaluatePassOption: compare actions before executing one.
// - updateBall/resolveLooseBall: integrate flight and resolve actual contacts.
// - setup*/completeRestart: own dead-ball placement, delay and legal restart.
// - performSubstitution/removeFromPitch: maintain slots and player statistics.
// See docs/development/match-engine.md and player-behavior.md for the
// rationale.

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <span>
#include <sstream>
#include <stdexcept>

#include "model/match_body_shape_binturong.h"
#include "model/match_commentary.h"
#include "model/match_contact_binturong.h"
#include "model/match_engine_internal.h"
#include "model/match_recorder.h"
#include "model/medical_centre.h"
#include "model/player.h"
#include "model/role_utils.h"

using namespace MatchEngineInternal;

namespace
{
std::uint32_t makeNonBlockingMatchSeed()
{
  static std::atomic<std::uint32_t> sequence{0};
  const auto clockValue = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const std::uint64_t mixed =
      clockValue ^ (static_cast<std::uint64_t>(++sequence) << 32U);
  return static_cast<std::uint32_t>(mixed ^ (mixed >> 32U));
}
}  // namespace

std::string_view goalkeeperStateName(GoalkeeperState state)
{
  switch (state)
  {
    case GoalkeeperState::SET_POSITION:
      return "set_position";
    case GoalkeeperState::SWEEP:
      return "sweep";
    case GoalkeeperState::RUSH:
      return "rush";
    case GoalkeeperState::CLAIM:
      return "claim";
    case GoalkeeperState::DIVE:
      return "dive";
    case GoalkeeperState::HOLD:
      return "hold";
    case GoalkeeperState::DISTRIBUTE:
      return "distribute";
    case GoalkeeperState::RECOVER:
      return "recover";
  }
  return "unknown";
}

MatchEngine::MatchEngine(const Lineup& home_lineup, const Lineup& away_lineup,
                         const Strategy& home_strat, const Strategy& away_strat,
                         const StatsConfig& config)
    : MatchEngine(home_lineup, away_lineup, home_strat, away_strat, config,
                  makeNonBlockingMatchSeed())
{
}

MatchEngine::MatchEngine(const Lineup& home_lineup, const Lineup& away_lineup,
                         const Strategy& home_strat, const Strategy& away_strat,
                         const StatsConfig& config, uint32_t seed,
                         HandshakeId handshake, PossessionId possession,
                         const MatchPitch& pitchToPlay)
    : statsConfig(config),
      homeStrategy(home_strat),
      awayStrategy(away_strat),
      pitch(pitchToPlay),
      handshakeId(handshake),
      possessionId(possession),
      rng(seed),
      incidentRng(static_cast<std::uint32_t>(splitMix64(seed))),
      matchSeed(seed)
{
  if (possession != PossessionId::AARDVARK &&
      possession != PossessionId::BINTURONG)
  {
    throw std::invalid_argument("Unknown possession implementation");
  }
  players.reserve(22);
  playerStats.reserve(36);
  events.reserve(256);
  computeLevel(home_lineup, away_lineup);
  initializePlayers(home_lineup, true);
  initializePlayers(away_lineup, false);
  slotAnchors = formations;
  resolveTactics(true);
  resolveTactics(false);
  homeBench = home_lineup.getReserves();
  awayBench = away_lineup.getReserves();
  designations = {home_lineup.getDesignations(), away_lineup.getDesignations()};
  for (const MatchPlayer& player : players)
  {
    squad.push_back(player.player);
  }
  squad.insert(squad.end(), homeBench.begin(), homeBench.end());
  squad.insert(squad.end(), awayBench.begin(), awayBench.end());
  std::erase(homeBench, nullptr);
  std::erase(awayBench, nullptr);

  refreshTargetBlends();
  computeUnderdogShares();
  refreshEffectiveSliders();
  assignMarks(true);
  assignMarks(false);
  refereeDraw = gaussian(incidentRng);
  refereeStrictness = drawRefereeStrictness();

  setupKickOff(true);
  updateTeamPhases();
  logEvent(MatchEventType::KICK_OFF);
  describePendingEvents();
}

void MatchEngine::setMatchContext(const MatchContext& context)
{
  if (stepCounter != 0)
  {
    return;
  }
  convertMatchContext(context);
  refereeStrictness = drawRefereeStrictness();
  // The home crowd's lift is part of the loaded attributes.
  for (MatchPlayer& player : players)
  {
    if ((player.player != nullptr) && player.isHomeTeam)
    {
      loadAttributes(player, player.player);
    }
  }
  ++inputRevision;
}

std::size_t MatchEngine::addPlayerStats(const MatchPlayer& matchPlayer,
                                        bool started)
{
  PlayerMatchStats entry;
  entry.playerId =
      (matchPlayer.player != nullptr) ? matchPlayer.player->getId() : 0;
  entry.isHomeTeam = matchPlayer.isHomeTeam;
  entry.role = (matchPlayer.player != nullptr) ? matchPlayer.player->getRole()
                                               : PlayerRole::UNKNOWN;
  entry.started = started;
  entry.substitutedOn = !started;
  entry.condition = matchPlayer.stamina;
  playerStats.push_back(entry);
  return playerStats.size() - 1;
}

PlayerMatchStats& MatchEngine::statsOf(const MatchPlayer& matchPlayer)
{
  return playerStats[matchPlayer.statsIndex];
}

const PlayerMatchStats* MatchEngine::findPlayerStats(PlayerID playerId) const
{
  const auto found = std::ranges::find_if(
      playerStats, [playerId](const PlayerMatchStats& entry)
      { return entry.playerId == playerId; });
  return found == playerStats.end() ? nullptr : &*found;
}

std::optional<float> MatchEngine::getPlayerCondition(PlayerID playerId) const
{
  for (const auto& player : players)
  {
    if ((player.player != nullptr) && player.player->getId() == playerId)
    {
      return player.stamina;
    }
  }
  if (const PlayerMatchStats* entry = findPlayerStats(playerId))
  {
    return entry->condition;
  }
  return std::nullopt;
}

bool MatchEngine::setPlayerCondition(PlayerID playerId, float condition)
{
  if (state != MatchState::KICK_OFF || elapsedMatchMinutes > 0.0F)
  {
    return false;
  }
  const float clamped = convertCondition(condition);
  for (auto& player : players)
  {
    if ((player.player != nullptr) && player.player->getId() == playerId)
    {
      player.stamina = clamped;
      statsOf(player).condition = player.stamina;
      ++inputRevision;
      return true;
    }
  }
  for (const auto* bench : {&homeBench, &awayBench})
  {
    if (std::ranges::none_of(*bench, [playerId](const Player* candidate)
                             { return candidate->getId() == playerId; }))
    {
      continue;
    }
    std::erase_if(benchConditions, [playerId](const auto& entry)
                  { return entry.first == playerId; });
    benchConditions.emplace_back(playerId, clamped);
    ++inputRevision;
    return true;
  }
  return false;
}

void MatchEngine::setTacticalFamiliarity(bool homeTeam, float familiarity)
{
  if (!std::isfinite(familiarity))
  {
    return;
  }
  (homeTeam ? homeFamiliarity : awayFamiliarity) =
      convertFamiliarity(familiarity);
  refreshTargetBlends();
  ++inputRevision;
}

void MatchEngine::setBrain(bool homeTeam, BrainId brain)
{
  brains[homeTeam ? 0 : 1] = brain;
  refreshTargetBlends();
}

void MatchEngine::refreshTargetBlends()
{
  for (std::size_t team = 0; team < 2; ++team)
  {
    const MovementStyle& style = movementStyle(team == 0);
    if (!style.smoothsTargets)
    {
      targetBlends[team * 2] = 1.0F;
      targetBlends[(team * 2) + 1] = 1.0F;
      continue;
    }
    const float familiarity = effectiveFamiliarity(team);
    const float response =
        1.0F - (style.familiarityResponseLoss * (1.0F - familiarity));
    targetBlends[team * 2] = 1.0F - std::exp(-style.tacticalResponsePerSecond *
                                             response * stepSeconds());
    targetBlends[(team * 2) + 1] =
        1.0F -
        std::exp(-style.urgentResponsePerSecond * response * stepSeconds());
  }
}

void MatchEngine::setStepTicks(std::uint32_t ticks)
{
  ticks = std::max(ticks, 1U);
  if (ticks == stepTicks)
  {
    return;
  }
  stepTicks = ticks;
  refreshTargetBlends();
}

float MatchEngine::stepSeconds() const
{
  return MatchTuning::Timing::FIXED_STEP_SECONDS *
         static_cast<float>(stepTicks);
}

bool MatchEngine::periodElapsed(std::uint64_t ticks) const
{
  if (stepCounter < stepTicks)
  {
    return true;
  }
  return stepCounter / ticks != (stepCounter - stepTicks) / ticks;
}

float MatchEngine::familiarityOf(const MatchPlayer& player) const
{
  return effectiveFamiliarity(player.isHomeTeam ? 0 : 1);
}

float MatchEngine::effectiveFamiliarity(std::size_t team) const
{
  // No tactics asked of him in a drill with no layers: nothing unfamiliar.
  if (!drillRules.layers)
  {
    return 1.0F;
  }
  const float drilled = team == 0 ? homeFamiliarity : awayFamiliarity;
  if (reshapeSecondsRemaining[team] <= 0.0F)
  {
    return drilled;
  }
  return std::max(
      0.0F,
      drilled - (reshapeFamiliarityCost[team] * reshapeSecondsRemaining[team] /
                 MatchTuning::Touchline::RESHAPE_RECOVERY_SECONDS));
}

void MatchEngine::setStrategy(bool homeTeam, const Strategy& strategy)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::STRATEGY;
  command.homeTeam = homeTeam;
  command.strategy = strategy;
  recordCommand(command);
}

void MatchEngine::applyShout(bool homeTeam, MatchShout shout)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::SHOUT;
  command.homeTeam = homeTeam;
  command.shout = shout;
  recordCommand(command);
}

void MatchEngine::startShout(bool homeTeam, MatchShout shout)
{
  ShoutState& current = shouts[homeTeam ? 0 : 1];
  current.shout = shout;
  current.remainingSeconds = MatchTuning::Touchline::SHOUT_DURATION_SECONDS;
  current.impact = 1.0F / (1.0F + current.repeats);
  current.repeats += 1.0F;
  refreshEffectiveSliders();
  ++inputRevision;
}

float MatchEngine::getShoutStrength(bool homeTeam) const
{
  if (!drillRules.layers)
  {
    return 0.0F;
  }
  const ShoutState& shout = shouts[homeTeam ? 0 : 1];
  return std::clamp(shout.remainingSeconds /
                        MatchTuning::Touchline::SHOUT_DURATION_SECONDS,
                    0.0F, 1.0F) *
         shout.impact;
}

std::optional<MatchShout> MatchEngine::getActiveShout(bool homeTeam) const
{
  const ShoutState& shout = shouts[homeTeam ? 0 : 1];
  if (shout.remainingSeconds <= 0.0F)
  {
    return std::nullopt;
  }
  return shout.shout;
}

std::vector<Vector2F> MatchEngine::getFormation(bool homeTeam) const
{
  return formations[homeTeam ? 0 : 1];
}

bool MatchEngine::setFormation(bool homeTeam, std::span<const Vector2F> shape)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::FORMATION;
  command.homeTeam = homeTeam;
  command.formation.assign(shape.begin(), shape.end());
  return recordCommand(command);
}

std::optional<std::size_t> MatchEngine::getFormationSlot(
    PlayerID playerId) const
{
  for (const MatchPlayer& player : players)
  {
    if (active(player) && !player.isGoalkeeper && player.formationSlot >= 0 &&
        player.player->getId() == playerId)
    {
      return static_cast<std::size_t>(player.formationSlot);
    }
  }
  return std::nullopt;
}

bool MatchEngine::movePlayerToSlot(PlayerID playerId, std::size_t slot)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::MOVE_TO_SLOT;
  command.player = playerId;
  command.slot = slot;
  for (const MatchPlayer& player : players)
  {
    if ((player.player != nullptr) && player.player->getId() == playerId)
    {
      command.homeTeam = player.isHomeTeam;
    }
  }
  return recordCommand(command);
}

void MatchEngine::loadCommandReplay(std::vector<MatchCommandRecord> log)
{
  std::stable_sort(
      log.begin(), log.end(),
      [](const MatchCommandRecord& first, const MatchCommandRecord& second)
      { return first.step < second.step; });
  commandLog = std::move(log);
  commandCursor = 0;
  ++inputRevision;
}

void MatchEngine::continueReplay(std::vector<MatchCommandRecord> commands,
                                 std::vector<MatchInputRecord> inputs)
{
  // The cursors keep marking what this copy has applied: the logs it holds
  // are a prefix of the complete ones.
  if (commands.size() < commandCursor || inputs.size() < inputCursor)
  {
    return;
  }
  commandLog = std::move(commands);
  inputLog = std::move(inputs);
  ++inputRevision;
}

bool MatchEngine::recordCommand(const MatchCommandRecord& command)
{
  if (!executeCommand(command))
  {
    return false;
  }
  // A live change ends any replay still pending.
  commandLog.resize(commandCursor);
  commandLog.push_back(command);
  commandLog.back().step = stepCounter;
  commandCursor = commandLog.size();
  ++inputRevision;
  return true;
}

void MatchEngine::applyDueCommands()
{
  while (commandCursor < commandLog.size() &&
         commandLog[commandCursor].step <= stepCounter)
  {
    executeCommand(commandLog[commandCursor++]);
  }
}

bool MatchEngine::executeCommand(const MatchCommandRecord& command)
{
  const std::size_t team = command.homeTeam ? 0 : 1;
  switch (command.type)
  {
    case MatchCommandType::DRILL_TARGET:
    {
      MatchPlayer* player = drillPlayer(command.player);
      if (player == nullptr)
      {
        return false;
      }
      std::erase_if(drillTargets, [&command](const DrillTarget& held)
                    { return held.player == command.player; });
      if (command.target)
      {
        drillTargets.push_back({.player = command.player,
                                .target = *command.target,
                                .urgent = command.urgent,
                                .route = command.route,
                                .apexOffset = command.apexOffset,
                                .outwardRoom = command.outwardRoom,
                                .facing = command.facing,
                                .atSpeed = command.atSpeed});
        // Taken at once, not at the next planner refresh (by his touches
        // too, when he is dribbling).
        holdDrillTarget(*player, drillTargets.back());
        if (hasTouchClaim() && getPossessionAttachment() == player->player)
        {
          ball.dribbleGoal = *command.target;
        }
      }
      ++inputRevision;
      return true;
    }
    case MatchCommandType::FORCE_SHOT:
      if (drillPlayer(command.player) == nullptr)
      {
        return false;
      }
      forcedShooter = command.player;
      forcedShotAim = command.shotAim;
      forcedShotPower = command.shotPower;
      forcedShotFirstTime = command.shotFirstTime;
      forcedShotTechnique = command.shotTechnique;
      ++inputRevision;
      return true;
    case MatchCommandType::STRIKE_STILL_BALL:
    {
      MatchPlayer* shooter = drillPlayer(command.player);
      if ((shooter == nullptr) ||
          getPossessionAttachment() != shooter->player)
      {
        return false;
      }
      // The ball lies still where the drill put it: struck there and now,
      // as a dead ball (no sweep meet to wait for, no touch before), at the
      // spot his brain picks.
      strikesWaitForSweep = false;
      strikeAt.reset();
      takeShot(*shooter);
      ++inputRevision;
      return true;
    }
    case MatchCommandType::FORCE_PASS:
      // To a team-mate, or (no team-mate) to a spot.
      if ((drillPlayer(command.player) == nullptr) ||
          (command.incoming != 0 ? (drillPlayer(command.incoming) == nullptr)
                                 : !command.target))
      {
        return false;
      }
      forcedPass = {
          .passer = command.player,
          .receiver = command.incoming,
          .style = command.passStyle,
          .spot = command.incoming != 0 ? std::nullopt : command.target,
          .spin = command.passSpin,
          .arrivalSpeed = command.passArrival,
          .firstTime = command.passFirstTime};
      ++inputRevision;
      return true;
    case MatchCommandType::FORCE_HEADER:
      if ((drillPlayer(command.player) == nullptr) ||
          (command.headerAim == ForcedHeaderAim::SPOT && !command.target) ||
          (command.headerAim == ForcedHeaderAim::GOAL && !command.shotAim))
      {
        return false;
      }
      forcedHeader =
          ForcedHeader{.player = command.player,
                       .aim = command.headerAim,
                       .spot = command.target.value_or(PitchPoint{}),
                       .goalAim = command.shotAim.value_or(Vector2F{}),
                       .knownEarly = command.headerKnownEarly};
      lastHeadPart.reset();
      ++inputRevision;
      return true;
    case MatchCommandType::FORCE_TACKLE_INTENT:
      if (drillPlayer(command.player) == nullptr)
      {
        return false;
      }
      std::erase_if(forcedTackleIntents, [&](const auto& forced)
                    { return forced.first == command.player; });
      if (command.tackleIntent != TackleIntent::BRAIN)
      {
        forcedTackleIntents.emplace_back(command.player, command.tackleIntent);
      }
      ++inputRevision;
      return true;
    case MatchCommandType::KEEP_BALL:
      if (drillPlayer(command.player) == nullptr)
      {
        return false;
      }
      std::erase(ballKeepers, command.player);
      std::erase_if(ballKeeperUrgency, [&](const auto& keeper)
                    { return keeper.first == command.player; });
      if (command.urgent)
      {
        ballKeepers.push_back(command.player);
        ballKeeperUrgency.emplace_back(command.player, command.keepUrgency);
      }
      ++inputRevision;
      return true;
    case MatchCommandType::FEED_BALL:
    {
      MatchPlayer* receiver =
          command.player != 0 ? drillPlayer(command.player) : nullptr;
      if (state != MatchState::PLAYING || !command.target ||
          (command.player != 0 && (receiver == nullptr)))
      {
        return false;
      }
      // A fresh ball in flight: nobody owns it or last played it.
      beginFedBall();
      ball.position = PitchPoint{
          std::clamp(command.target->x,
                     MatchTuning::Pitch::BALL_INSIDE_END_METRES,
                     pitch.length - MatchTuning::Pitch::BALL_INSIDE_END_METRES),
          std::clamp(
              command.target->y, MatchTuning::Pitch::BALL_INSIDE_SIDE_METRES,
              pitch.width - MatchTuning::Pitch::BALL_INSIDE_SIDE_METRES)};
      ball.z = 0.0F;
      notePathChange();
      ball.velocity = command.feedVelocity;
      ball.velocityZ = command.feedLift;
      ball.spin = {};
      finishFedBall(receiver);
      ++inputRevision;
      return true;
    }
    case MatchCommandType::TEAM_TALK:
      if (command.talkHalf < 1 || command.talkHalf > 2 ||
          !std::isfinite(command.talkModifier))
      {
        return false;
      }
      teamTalks[team][static_cast<std::size_t>(command.talkHalf - 1)] =
          convertTeamTalk(command.talkModifier);
      ++inputRevision;
      return true;
    case MatchCommandType::STRATEGY:
      (command.homeTeam ? homeStrategy : awayStrategy) = command.strategy;
      resolveTactics(command.homeTeam);
      refreshEffectiveSliders();
      ++inputRevision;
      return true;
    case MatchCommandType::SHOUT:
      startShout(command.homeTeam, command.shout);
      return true;
    case MatchCommandType::FORMATION:
    {
      auto& formation = formations[team];
      if (command.formation.size() != formation.size() ||
          state == MatchState::FULL_TIME)
      {
        return false;
      }
      using Pitch = MatchTuning::Pitch;
      float moved = 0.0F;
      std::vector<Vector2F> shape;
      shape.reserve(formation.size());
      for (std::size_t slot = 0; slot < formation.size(); ++slot)
      {
        // Formation slots are lineup coordinates (fractions, own goal at
        // x = 0): clamped as fractions, measured as points on the pitch.
        const Vector2F wanted = command.formation[slot];
        if (!std::isfinite(wanted.x) || !std::isfinite(wanted.y))
        {
          return false;
        }
        const Vector2F kept{.x = std::clamp(wanted.x, Pitch::PLAYER_MIN_X_SHARE,
                                            Pitch::PLAYER_MAX_X_SHARE),
                            .y = std::clamp(wanted.y, Pitch::PLAYER_MIN_Y_SHARE,
                                            Pitch::PLAYER_MAX_Y_SHARE)};
        shape.push_back(kept);
        moved += distance(pitch.at(kept.x, kept.y),
                          pitch.at(formation[slot].x, formation[slot].y));
      }
      formation = std::move(shape);
      using T = MatchTuning::Touchline;
      const float cost = std::min(
          T::MAX_RESHAPE_FAMILIARITY_COST,
          moved /
              static_cast<float>(std::max<std::size_t>(formation.size(), 1)) *
              T::RESHAPE_FAMILIARITY_PER_METRE);
      if (cost > 0.0F)
      {
        // A second change before the first has sunk in adds to what is left.
        reshapeFamiliarityCost[team] =
            std::min(T::MAX_RESHAPE_FAMILIARITY_COST,
                     cost + (reshapeFamiliarityCost[team] *
                             reshapeSecondsRemaining[team] /
                             T::RESHAPE_RECOVERY_SECONDS));
        reshapeSecondsRemaining[team] = T::RESHAPE_RECOVERY_SECONDS;
        refreshTargetBlends();
      }
      placeFormation(command.homeTeam);
      ++inputRevision;
      return true;
    }
    case MatchCommandType::MOVE_TO_SLOT:
    {
      if (!command.slot || *command.slot >= formations[team].size() ||
          state == MatchState::FULL_TIME)
      {
        return false;
      }
      const auto mover = std::ranges::find_if(
          players,
          [&](const MatchPlayer& player)
          {
            return active(player) && !player.isGoalkeeper &&
                   player.formationSlot >= 0 &&
                   player.isHomeTeam == command.homeTeam &&
                   player.player->getId() == command.player;
          });
      if (mover == players.end())
      {
        return false;
      }
      const auto slot = static_cast<std::int8_t>(*command.slot);
      for (MatchPlayer& occupant : players)
      {
        if (active(occupant) && !occupant.isGoalkeeper &&
            occupant.isHomeTeam == command.homeTeam &&
            occupant.formationSlot == slot)
        {
          occupant.formationSlot = mover->formationSlot;
        }
      }
      mover->formationSlot = slot;
      placeFormation(command.homeTeam);
      ++inputRevision;
      return true;
    }
    case MatchCommandType::SUBSTITUTION:
    {
      const auto& bench = command.homeTeam ? homeBench : awayBench;
      const auto incoming = std::ranges::find_if(
          bench, [&](const Player* player)
          { return player && player->getId() == command.incoming; });
      if (incoming == bench.end() ||
          (command.slot && *command.slot >= formations[team].size()))
      {
        return false;
      }
      const Player* inPlayer = *incoming;
      const auto outgoing = std::ranges::find_if(
          players,
          [&](const MatchPlayer& player)
          {
            return active(player) && player.player->getId() == command.player;
          });
      if (outgoing == players.end() ||
          outgoing->isHomeTeam != command.homeTeam ||
          !canSubstitute(command.homeTeam))
      {
        return false;
      }
      // A change made while the ball is live stops play for it; several
      // changes made together share that stoppage (and window). On foot
      // (touchline plan stage 5) it waits for the next stoppage instead.
      if (state == MatchState::PLAYING &&
          manualSubstitutionStep != stepCounter && !walksOut())
      {
        beginStoppage();
        manualSubstitutionStep = stepCounter;
      }
      if (!canSubstitute(outgoing->isHomeTeam))
      {
        return false;
      }
      ++inputRevision;
      const bool wasKeeper = outgoing->isGoalkeeper;
      const std::int8_t wasSlot = outgoing->formationSlot;
      const bool changed =
          performSubstitution(*outgoing, inPlayer, SubstitutionReason::MANUAL);
      if (changed && command.slot && !wasKeeper && wasSlot >= 0)
      {
        MatchCommandRecord move;
        move.type = MatchCommandType::MOVE_TO_SLOT;
        move.homeTeam = command.homeTeam;
        move.player = inPlayer->getId();
        move.slot = command.slot;
        // On foot, once he is on.
        bool underWay = false;
        for (PendingSubstitution& pending : pendingSubstitutions)
        {
          if (pending.incoming == inPlayer)
          {
            pending.moveToSlot = move;
            underWay = true;
          }
        }
        if (!underWay)
        {
          executeCommand(move);
        }
      }
      describePendingEvents();
      return changed;
    }
  }
  return false;
}

StrategySliders MatchEngine::getEffectiveSliders(bool homeTeam) const
{
  return effectiveSliders[homeTeam ? 0 : 1];
}

void MatchEngine::refreshEffectiveSliders()
{
  effectiveSliders = {computeEffectiveSliders(true),
                      computeEffectiveSliders(false)};
}

StrategySliders MatchEngine::computeEffectiveSliders(bool homeTeam) const
{
  // The manager's tactics, as the team's mentality bends them, then nudged
  // by the touchline shout.
  StrategySliders sliders =
      (homeTeam ? homeStrategy : awayStrategy).getSliders();
  // A drill with no layers plays the base tactics as they are.
  if (!drillRules.layers)
  {
    return sliders;
  }
  applyTeamMentality(sliders, homeTeam);
  return shoutSliders(sliders, homeTeam);
}

StrategySliders MatchEngine::shoutSliders(StrategySliders sliders,
                                          bool homeTeam) const
{
  const auto clampSlider = [](float& value)
  { value = std::clamp(value, 0.0F, 1.0F); };
  const float strength = getShoutStrength(homeTeam);
  if (strength <= 0.0F)
  {
    return sliders;
  }
  const float step = MatchTuning::Touchline::SHOUT_SLIDER_STEP * strength;
  switch (shouts[homeTeam ? 0 : 1].shout)
  {
    case MatchShout::PUSH_HIGHER:
      sliders.offensiveBias += step;
      sliders.pressing += step * 0.5F;
      break;
    case MatchShout::DROP_DEEPER:
      sliders.offensiveBias -= step;
      sliders.pressing -= step * 0.75F;
      sliders.compactness += step * 0.75F;
      break;
    case MatchShout::PRESS_MORE:
      sliders.pressing += step * 1.25F;
      break;
    case MatchShout::CALM_DOWN:
      sliders.riskTaking -= step;
      sliders.pressing -= step * 0.25F;
      break;
    case MatchShout::WORK_BALL_INTO_BOX:
      sliders.riskTaking -= step * 0.25F;
      break;
    case MatchShout::SHOOT_ON_SIGHT:
      sliders.riskTaking += step * 0.5F;
      break;
    case MatchShout::ENCOURAGE:
      break;
    case MatchShout::STAND_OFF:
      sliders.pressing -= step * 1.25F;
      sliders.compactness += step * 0.5F;
      break;
    case MatchShout::DEMAND_MORE:
      sliders.pressing += step * 0.5F;
      sliders.offensiveBias += step * 0.5F;
      break;
    case MatchShout::HIT_ON_COUNTER:
      sliders.pressing -= step * 0.5F;
      sliders.compactness += step * 0.5F;
      sliders.riskTaking += step;
      break;
    case MatchShout::KEEP_POSSESSION:
      sliders.riskTaking -= step;
      sliders.offensiveBias -= step * 0.5F;
      sliders.widthUsage += step * 0.5F;
      break;
  }
  clampSlider(sliders.pressing);
  clampSlider(sliders.riskTaking);
  clampSlider(sliders.offensiveBias);
  clampSlider(sliders.widthUsage);
  clampSlider(sliders.compactness);
  return sliders;
}

float MatchEngine::shoutWorkRate(bool homeTeam) const
{
  const MatchShout shout = shouts[homeTeam ? 0 : 1].shout;
  return shout == MatchShout::ENCOURAGE || shout == MatchShout::DEMAND_MORE
             ? MatchTuning::Touchline::ENCOURAGE_WORK_RATE *
                   getShoutStrength(homeTeam)
             : 0.0F;
}

void MatchEngine::setTeamTalkModifier(bool homeTeam, int half, float modifier)
{
  // Logged like every touchline change, so a replay of the command log
  // reproduces a match a team talk played its part in.
  MatchCommandRecord command;
  command.type = MatchCommandType::TEAM_TALK;
  command.homeTeam = homeTeam;
  command.talkHalf = half;
  command.talkModifier = modifier;
  recordCommand(command);
}

float MatchEngine::getTeamTalkModifier(bool homeTeam, int half) const
{
  if (half < 1 || half > 2)
  {
    return 0.0F;
  }
  return teamTalks[homeTeam ? 0 : 1][static_cast<std::size_t>(half - 1)];
}

float MatchEngine::talkOf(bool homeTeam) const
{
  // A talk lifts (or deflates) the side for the first minutes of its half
  // and has worn off well before the break; extra time has none. A drill
  // with no layers has none either.
  if (period < 1 || period > 2 || !drillRules.layers)
  {
    return 0.0F;
  }
  const float talk =
      teamTalks[homeTeam ? 0 : 1][static_cast<std::size_t>(period - 1)];
  if (talk == 0.0F)
  {
    return 0.0F;
  }
  namespace T = TacticsTuning;
  const float minutes =
      matchTimeMinutes -
      (period == 2 ? MatchTuning::Timing::HALF_TIME_MINUTE : 0.0F);
  return talk *
         std::clamp((T::TALK_FADE_END_MINUTES - minutes) /
                        (T::TALK_FADE_END_MINUTES - T::TALK_FULL_MINUTES),
                    0.0F, 1.0F);
}

float MatchEngine::teamEdge(bool homeTeam) const
{
  // Home, team talk and numbers are all layers.
  if (!drillRules.layers)
  {
    return 0.0F;
  }
  using R = MatchTuning::Rules;
  int balance = 0;
  for (const auto& player : players)
  {
    if (active(player))
    {
      balance += player.isHomeTeam == homeTeam ? 1 : -1;
    }
  }
  balance = std::clamp(balance, -R::MAX_NUMERICAL_EDGE_PLAYERS,
                       R::MAX_NUMERICAL_EDGE_PLAYERS);
  return (homeTeam ? R::HOME_EXECUTION_BONUS * matchContext.homeAdvantageScale
                   : 0.0F) +
         talkOf(homeTeam) +
         (static_cast<float>(balance) * R::NUMERICAL_EDGE_PER_PLAYER);
}

void MatchEngine::refreshRatings()
{
  for (auto& entry : playerStats)
  {
    const int goalDifference =
        entry.isHomeTeam ? homeScore - awayScore : awayScore - homeScore;
    const bool goalkeeper = entry.role == PlayerRole::GK;
    const bool defender = entry.role == PlayerRole::CB ||
                          entry.role == PlayerRole::LB ||
                          entry.role == PlayerRole::RB;
    entry.rating = MatchRules::computeMatchRating(entry, goalkeeper, defender,
                                                  goalDifference);
  }
}

namespace
{
/** Counter-based noise keyed by (seed, tick, salt, key), uniform in [-1, 1). */
float counterNoise(std::uint32_t seed, std::uint64_t tick, std::uint32_t salt,
                   std::uint32_t key)
{
  const std::uint64_t mixed =
      splitMix64((static_cast<std::uint64_t>(seed) << 32U) ^ tick ^
                 (static_cast<std::uint64_t>(salt) << 48U) ^
                 (static_cast<std::uint64_t>(key) << 16U));
  return (static_cast<float>(mixed >> 40U) / static_cast<float>(1U << 23U)) -
         1.0F;
}
}  // namespace

float MatchEngine::hashNoise(std::uint32_t salt, std::uint32_t key) const
{
  return counterNoise(matchSeed, stepCounter, salt, key);
}

float MatchEngine::epochNoise(std::uint32_t salt, std::uint32_t key,
                              std::uint32_t epochSteps) const
{
  return counterNoise(matchSeed, stepCounter / std::max(epochSteps, 1U), salt,
                      key);
}

void MatchEngine::update(float deltaTime)
{
  lastUpdateStepCount = 0;
  if (state == MatchState::FULL_TIME || !std::isfinite(deltaTime) ||
      deltaTime <= 0.0F)
  {
    return;
  }

  constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
  accumulator +=
      std::min(deltaTime, MatchTuning::Timing::MAX_FRAME_DELTA_SECONDS);
  while (accumulator + EPSILON >= STEP && state != MatchState::FULL_TIME &&
         lastUpdateStepCount < MatchTuning::Timing::MAX_FIXED_STEPS_PER_UPDATE)
  {
    simulateStep(STEP);
    accumulator -= STEP;
    ++lastUpdateStepCount;
  }
  if (lastUpdateStepCount == MatchTuning::Timing::MAX_FIXED_STEPS_PER_UPDATE &&
      accumulator >= STEP)
  {
    droppedSimulationSteps +=
        static_cast<std::uint64_t>(std::floor(accumulator / STEP));
    accumulator = std::fmod(accumulator, STEP);
  }
}

float MatchEngine::advance(float seconds)
{
  lastUpdateStepCount = 0;
  if (state == MatchState::FULL_TIME || !std::isfinite(seconds) ||
      seconds <= 0.0F)
  {
    return 0.0F;
  }
  constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
  const auto steps =
      static_cast<std::int64_t>(std::ceil((seconds / STEP) - EPSILON));
  std::int64_t done = 0;
  for (; done < steps && state != MatchState::FULL_TIME; ++done)
  {
    simulateStep(STEP);
  }
  lastUpdateStepCount = static_cast<int>(
      std::min<std::int64_t>(done, std::numeric_limits<int>::max()));
  return static_cast<float>(done) * STEP;
}

void MatchEngine::simulateToEnd() { simulateToEnd(MatchFidelity::FULL); }

void MatchEngine::simulateToEnd(MatchFidelity fidelity)
{
  const bool background =
      fidelity == MatchFidelity::BACKGROUND && !controlledIndex;
  // Nobody reads the analytics of an unwatched fixture.
  if (background)
  {
    tracker.setEnabled(false);
  }
  // A match lasts at most ~7,200 simulated seconds (both halves with the
  // maximum added time and overrun); the bound only guards against bugs.
  constexpr std::int64_t MAX_STEPS = 120'000;
  for (std::int64_t step = 0;
       step < MAX_STEPS && state != MatchState::FULL_TIME; ++step)
  {
    if (background)
    {
      setStepTicks(backgroundStepTicks());
    }
    simulateStep(stepSeconds());
  }
  setStepTicks(1U);
}

std::uint32_t MatchEngine::backgroundStepTicks() const
{
  switch (state)
  {
    case MatchState::THROW_IN:
    case MatchState::GOAL_KICK:
    case MatchState::CORNER_KICK:
    case MatchState::FREE_KICK:
    case MatchState::PENALTY:
      break;
    default:
      return 1U;
  }
  // Land on the restart instead of overshooting its delay.
  const float remaining = std::ceil(
      (setPieceTimer / MatchTuning::Timing::FIXED_STEP_SECONDS) - 0.001F);
  if (!(remaining > 1.0F))
  {
    return 1U;
  }
  return std::min(static_cast<std::uint32_t>(remaining),
                  MatchTuning::Timing::BACKGROUND_STOPPAGE_TICKS);
}

double MatchEngine::getSimulatedSeconds() const
{
  return static_cast<double>(stepCounter) * STEP_SECONDS;
}

void MatchEngine::setPlaybackMode(MatchPlaybackMode mode)
{
  playbackMode = mode;
  scheduledHighlight.reset();
}

void MatchEngine::setPlaybackSpeed(float simulatedSecondsPerWallSecond)
{
  if (!std::isfinite(simulatedSecondsPerWallSecond))
  {
    return;
  }
  playbackSpeed = std::clamp(simulatedSecondsPerWallSecond,
                             MatchTuning::Playback::MIN_SPEED,
                             MatchTuning::Playback::MAX_SPEED);
}

void MatchEngine::setHighlightPlaybackSpeed(float simulatedSecondsPerWallSecond)
{
  if (!std::isfinite(simulatedSecondsPerWallSecond))
  {
    return;
  }
  highlightSpeed = std::clamp(simulatedSecondsPerWallSecond,
                              MatchTuning::Playback::MIN_SPEED,
                              MatchTuning::Playback::MAX_SPEED);
}

bool MatchEngine::advancePlayback(float wallSeconds)
{
  if (state == MatchState::FULL_TIME || !std::isfinite(wallSeconds) ||
      wallSeconds <= 0.0F)
  {
    return false;
  }
  if (playbackMode == MatchPlaybackMode::FULL_MATCH)
  {
    update(wallSeconds * playbackSpeed);
    return false;
  }

  // A manual change alters the future, so an old prediction is void.
  if (scheduledHighlight && scheduledRevision != inputRevision)
  {
    scheduledHighlight.reset();
  }
  const double now = getSimulatedSeconds();
  if (scheduledHighlight && now >= scheduledHighlight->endSeconds)
  {
    scheduledHighlight.reset();
  }
  if (!scheduledHighlight)
  {
    scheduledHighlight = predictNextHighlight();
    scheduledRevision = inputRevision;
    const double target = scheduledHighlight ? scheduledHighlight->startSeconds
                                             : now + PREDICTION_HORIZON;
    if (target - now < STEP_SECONDS)
    {
      return false;
    }
    // The skip never jumps across a break (half-time, the breaks around
    // extra time, the shootout): it stops where one begins so the viewer
    // sees it, and the next call looks ahead again from there. The steps
    // are the same as without the stop, so the match does not change.
    const auto steps = static_cast<std::int64_t>(std::ceil(
        ((target - now) / STEP_SECONDS) - static_cast<double>(EPSILON)));
    const auto inBreak = [this]
    {
      return state == MatchState::HALF_TIME ||
             state == MatchState::PENALTY_SHOOTOUT;
    };
    std::int64_t done = 0;
    while (done < steps && state != MatchState::FULL_TIME)
    {
      const bool wasInBreak = inBreak();
      simulateStep(MatchTuning::Timing::FIXED_STEP_SECONDS);
      ++done;
      if (!wasInBreak && inBreak())
      {
        scheduledHighlight.reset();
        break;
      }
    }
    lastUpdateStepCount = static_cast<int>(
        std::min<std::int64_t>(done, std::numeric_limits<int>::max()));
    accumulator = 0.0F;
    return true;
  }
  update(wallSeconds * highlightSpeed);
  return false;
}

bool MatchEngine::isInHighlight() const
{
  const double now = getSimulatedSeconds();
  if (scheduledHighlight)
  {
    return now >= scheduledHighlight->startSeconds &&
           now <= scheduledHighlight->endSeconds;
  }
  return !highlights.empty() && now >= highlights.back().startSeconds &&
         now <= highlights.back().endSeconds;
}

std::optional<MatchHighlight> MatchEngine::predictNextHighlight(
    float horizonSeconds) const
{
  if (state == MatchState::FULL_TIME || !(horizonSeconds > 0.0F))
  {
    return std::nullopt;
  }
  MatchEngine lookahead(*this);
  lookahead.tracker.setEnabled(false);
  const std::uint64_t triggersBefore = lookahead.highlightTriggerCount;
  const auto steps = static_cast<std::int64_t>(
      std::ceil(horizonSeconds / MatchTuning::Timing::FIXED_STEP_SECONDS));
  // When the ball last came back into play before the trigger: a highlight
  // never opens on players standing at a dead ball.
  std::optional<double> lastRestart;
  for (std::int64_t step = 0;
       step < steps && lookahead.state != MatchState::FULL_TIME &&
       lookahead.highlightTriggerCount == triggersBefore;
       ++step)
  {
    const bool wasLive = lookahead.state == MatchState::PLAYING;
    lookahead.simulateStep(MatchTuning::Timing::FIXED_STEP_SECONDS);
    if (!wasLive && lookahead.state == MatchState::PLAYING)
    {
      lastRestart = lookahead.getSimulatedSeconds();
    }
  }
  if (lookahead.highlightTriggerCount == triggersBefore)
  {
    return std::nullopt;
  }
  MatchHighlight highlight;
  highlight.type = lookahead.lastTriggerType;
  highlight.triggerSeconds = lookahead.lastTriggerSeconds;
  highlight.triggerMinute = lookahead.lastTriggerMinute;
  highlight.startSeconds = std::max(getSimulatedSeconds(),
                                    highlight.triggerSeconds - HIGHLIGHT_LEAD);
  // Play resumed inside the build-up (a restart, a set piece): open just
  // before the ball is played rather than during the wait for it.
  if (lastRestart)
  {
    highlight.startSeconds =
        std::max(highlight.startSeconds,
                 *lastRestart - MatchTuning::Playback::RESTART_LEAD_SECONDS);
  }
  highlight.endSeconds = highlight.triggerSeconds + HIGHLIGHT_TAIL;
  return highlight;
}

void MatchEngine::captureInterpolationFrame()
{
  previousPlayerPositions.resize(players.size());
  previousPlayerFacingAngles.resize(players.size());
  previousPlayerFeetHeights.resize(players.size());
  for (std::size_t index = 0; index < players.size(); ++index)
  {
    previousPlayerPositions[index] = players[index].position;
    previousPlayerFacingAngles[index] = players[index].facingAngle;
    previousPlayerFeetHeights[index] = players[index].feetHeight;
  }
  previousTouchlinePositions.resize(touchlineBodies.size());
  previousTouchlineFacingAngles.resize(touchlineBodies.size());
  for (std::size_t index = 0; index < touchlineBodies.size(); ++index)
  {
    previousTouchlinePositions[index] = touchlineBodies[index].position;
    previousTouchlineFacingAngles[index] = touchlineBodies[index].facingAngle;
  }
  previousBallPosition = ball.position;
  previousBallZ = ball.z;
}

float MatchEngine::getInterpolationAlpha() const
{
  return std::clamp(accumulator / MatchTuning::Timing::FIXED_STEP_SECONDS, 0.0F,
                    1.0F);
}

void MatchEngine::advanceClock(float dt)
{
  const float clockScale = matchClockRate();
  const float clockDelta =
      dt * clockScale / MatchTuning::Timing::SECONDS_PER_MINUTE;
  matchTimeMinutes += clockDelta;
  elapsedMatchMinutes += clockDelta;
  accumulatePlayerLoad(dt);
}

void MatchEngine::simulateStep(float dt)
{
  simulateStepBody(dt);
  describePendingEvents();
  if (MatchRecorder* recorder = getRecorder())
  {
    recorder->onStepEnd(*this);
  }
}

void MatchEngine::rebindPendingStrike()
{
  // An engine copy (a rewind's saved state) keeps the held strike's
  // pointers into the engine it was copied from: point them at this one's
  // players again, by their slots.
  if (!pendingStrike)
  {
    return;
  }
  PendingStrike& strike = *pendingStrike;
  strike.striker = strike.strikerSlot < players.size()
                       ? &players[strike.strikerSlot]
                       : nullptr;
  if (strike.receiverSlot)
  {
    strike.pass.receiver = *strike.receiverSlot < players.size()
                               ? &players[*strike.receiverSlot]
                               : nullptr;
  }
}

void MatchEngine::notePendingStrikeSlots()
{
  if (!pendingStrike)
  {
    return;
  }
  PendingStrike& strike = *pendingStrike;
  const auto slotOf =
      [this](const MatchPlayer* player) -> std::optional<std::size_t>
  {
    if (!player || players.empty() || player < players.data() ||
        player >= players.data() + players.size())
    {
      return std::nullopt;
    }
    return static_cast<std::size_t>(player - players.data());
  };
  strike.strikerSlot = slotOf(strike.striker).value_or(players.size());
  strike.receiverSlot = slotOf(strike.pass.receiver);
}

void MatchEngine::simulateStepBody(float dt)
{
  rebindPendingStrike();
  // This is the authoritative tick order. First apply external changes and
  // age timers, then dispatch exactly one match-state branch. In live play,
  // move bodies before resolving touches/actions, and advance a newly kicked
  // ball afterwards. Reordering these stages changes contacts and seeded RNG
  // consumption, even if each individual stage still looks correct.
  // Replayed touchline changes happen between the same two steps as live.
  if (commandCursor < commandLog.size())
  {
    applyDueCommands();
  }
  // The bodies are chosen before the first step: then the touchline fills,
  // and a match that starts at its kick-off starts in the tunnel.
  if (!touchlineSetUp)
  {
    setUpTouchline();
  }
  if (!walk.started)
  {
    walk.started = true;
    if (walksOut() && state == MatchState::KICK_OFF && period == 1)
    {
      startInTunnel();
    }
  }
  // Substitutions on foot move on (touchline plan stage 5).
  updateSubstitutions(dt);
  captureInterpolationFrame();
  if (getPossessionHistory().headerRun > 0)
  {
    trackHeaderRun();
  }
  stepCounter += stepTicks;
  if (inputCursor < inputLog.size())
  {
    applyDueInputs();
  }
  // A substituted or dismissed player hands control back (play mode).
  if (controlledIndex)
  {
    const MatchPlayer& controlled = players[*controlledIndex];
    if (!active(controlled) || controlled.isGoalkeeper ||
        controlled.player->getId() != controlledPlayerId)
    {
      controlledIndex.reset();
    }
  }
  controlActionRemaining = std::max(0.0F, controlActionRemaining - dt);
  if (controlActionRemaining <= 0.0F)
  {
    controlInput.action = MatchInputAction::NONE;
  }
  for (auto& player : players)
  {
    player.tackleCooldown = std::max(0.0F, player.tackleCooldown - dt);
    player.footCooldown = std::max(0.0F, player.footCooldown - dt);
    player.actionCooldown = std::max(0.0F, player.actionCooldown - dt);
    player.trapTimer = std::max(0.0F, player.trapTimer - dt);
    player.plantedSeconds = std::max(0.0F, player.plantedSeconds - dt);
    player.diveTimer = std::max(0.0F, player.diveTimer - dt);
    player.isTrapping = player.trapTimer > 0.0F;
    player.isDiving = player.diveTimer > 0.0F;
  }
  ball.kickerLockout = std::max(0.0F, ball.kickerLockout - dt);
  // The sliders only move with a fading shout or with the clock while a side
  // leads; otherwise they stay as last computed.
  const bool shouting =
      shouts[0].remainingSeconds > 0.0F || shouts[1].remainingSeconds > 0.0F;
  for (auto& shout : shouts)
  {
    shout.remainingSeconds = std::max(0.0F, shout.remainingSeconds - dt);
    shout.repeats = std::max(
        0.0F, shout.repeats -
                  (dt / MatchTuning::Touchline::SHOUT_REPEAT_FADE_SECONDS));
  }
  if (reshapeSecondsRemaining[0] > 0.0F || reshapeSecondsRemaining[1] > 0.0F)
  {
    for (float& remaining : reshapeSecondsRemaining)
    {
      remaining = std::max(0.0F, remaining - dt);
    }
    refreshTargetBlends();
  }
  if (shouting || homeScore != awayScore || slidersStale)
  {
    refreshEffectiveSliders();
    slidersStale = false;
  }
  if (state == MatchState::PLAYING)
  {
    transitionSecondsRemaining =
        std::max(0.0F, transitionSecondsRemaining - dt);
  }
  updateTeamPhases();

  // Dead-ball and break states have their own movement/clock rules. Their
  // early returns prevent celebrations, breaks and shootouts from also
  // executing open-play decisions or the ordinary period-ending logic.
  if (state == MatchState::GOAL)
  {
    // The scored ball settles in the net while the teams celebrate and walk
    // back; the clock keeps running until the kick-off.
    if (shortStoppages())
    {
      goalCelebrationRemaining = std::min(
          goalCelebrationRemaining, MatchTuning::Timing::PLAY_STOPPAGE_SECONDS);
    }
    goalCelebrationRemaining = std::max(0.0F, goalCelebrationRemaining - dt);
    if (restartsByServe())
    {
      // A training pitch: everyone holds, the ball stays in the net, and
      // the coach serves to the side that conceded.
      holdForServe(dt);
      advanceClock(dt);
      settleBallInNet(dt);
      if (goalCelebrationRemaining <= 0.0F)
      {
        beginServe(!goalScoredByHome, 0.0F);
      }
      return;
    }
    updateRestartMovement(dt);
    advanceClock(dt);
    if (getPossessionAttachment() == nullptr)
    {
      updateBallInNet(dt);
    }
    if (goalCelebrationRemaining <= 0.0F)
    {
      kickOffAfterGoal(!goalScoredByHome);
    }
    return;
  }

  if (state == MatchState::SERVE)
  {
    updateServe(dt);
    return;
  }

  if (state == MatchState::PENALTY_SHOOTOUT)
  {
    updateShootout(dt);
    return;
  }

  if (state == MatchState::HALF_TIME)
  {
    runAiSubstitutions();
    if (state == MatchState::FULL_TIME)
    {
      return;
    }
    // Walking in (touchline plan stage 4): the break runs once all are in.
    if (walk.phase == WalkPhase::OFF)
    {
      updateWalk(dt);
      return;
    }
    if (playHalfMinutes > 0)
    {
      setPieceTimer =
          std::min(setPieceTimer, MatchTuning::Timing::PLAY_STOPPAGE_SECONDS);
    }
    setPieceTimer -= dt;
    if (setPieceTimer <= 0.0F)
    {
      ++period;
      matchTimeMinutes = MatchRules::periodStartMinute(period);
      // The home side kicks off the first half of each game, the away side
      // the second. Out of the tunnel, or placed.
      if (walk.phase == WalkPhase::IN)
      {
        beginWalkOut(period % 2 == 1, false);
      }
      else
      {
        setupKickOff(period % 2 == 1);
      }
      logEvent(MatchEventType::SECOND_HALF);
    }
    return;
  }

  if (state != MatchState::PLAYING)
  {
    runAiSubstitutions();
    if (state == MatchState::FULL_TIME)
    {
      return;
    }
    // Walking out (touchline plan stage 4): the kick-off waits for it.
    if (walk.phase != WalkPhase::NONE)
    {
      updateWalk(dt);
      return;
    }
    if (state != MatchState::KICK_OFF || kickOffWalkIn)
    {
      updateDeadBall(dt);
      updateRestartMovement(dt);
    }
    else if (MatchPlayer* taker = restartTaker();
             (taker != nullptr) && restartLineUp)
    {
      // At a kick-off only the taker moves: he lines the ball up.
      steerRestartLineUp(*taker, dt);
      taker->targetVelocity = {.x = 0.0F, .y = 0.0F};
      taker->routeWaypoints = 0;
      taker->arriveAtSpeed = false;
      const auto slot = static_cast<std::uint8_t>(slotOf(*taker));
      movePlayers(&slot, 1, dt, true);
    }
    if (stateClockRuns(state))
    {
      advanceClock(dt);
    }
    const float restartRate = matchClockRate();
    if (playHalfMinutes > 0)
    {
      setPieceTimer =
          std::min(setPieceTimer,
                   MatchTuning::Timing::PLAY_STOPPAGE_SECONDS * restartRate);
    }
    setPieceTimer -= dt * restartRate;
    // Once his time is up (or his controller asks), and every step while
    // he lines the ball up.
    if (userRestartPassRequested() || setPieceTimer <= 0.0F || restartLineUp)
    {
      completeRestart();
    }
  }
  else
  {
    updateMovement(dt);
    advanceClock(dt);
    stats.ballInPlayMinutes +=
        dt * (matchClockRate()) / MatchTuning::Timing::SECONDS_PER_MINUTE;
    resolvePossessionAndActions(dt);
    decideFirstTimePass();
    trackPressures();
    if (((getPossessionAttachment() == nullptr) || hasTouchClaim()) &&
        state == MatchState::PLAYING)
    {
      updateBall(dt);
    }
    // A strike he never got to in the sweep is dropped: he decides again.
    // One decided for his next meet waits for it while it holds, and one
    // he is turning to while he still can, and one he means to play first
    // time while the ball still comes to him.
    if (pendingStrike && (pendingStrike->atMeet || pendingStrike->turning ||
                          pendingStrike->firstTime))
    {
      ageHeldStrike(dt);
    }
    else
    {
      pendingStrike.reset();
    }
    // Possession belongs to the side in control, including its passes in
    // flight and the loose balls it last played.
    if (getPossessionHistory().lastControlledTeamHome)
    {
      (*getPossessionHistory().lastControlledTeamHome
           ? homePossessionMinutes
           : awayPossessionMinutes) += dt;
      stats.homePossession = homePossessionMinutes /
                             (homePossessionMinutes + awayPossessionMinutes) *
                             MatchTuning::Statistics::PERCENT_SCALE;
      stats.awayPossession =
          MatchTuning::Statistics::PERCENT_SCALE - stats.homePossession;
    }
    updatePendingAdvantage(dt);
    setPiecePhaseRemaining = std::max(0.0F, setPiecePhaseRemaining - dt);
    if (setPiecePhaseRemaining > 0.0F)
    {
      if (inPenaltyArea(ball.position, !setPieceHome))
      {
        setPieceBallInBox = true;
      }
      else if (setPieceBallInBox)
      {
        setPieceBoxCleared = true;
      }
    }
    injuryCheckTimer += dt / MatchTuning::Timing::SECONDS_PER_MINUTE;
    if (injuryCheckTimer >= MatchTuning::Injury::CHECK_INTERVAL_MINUTES)
    {
      injuryCheckTimer -= MatchTuning::Injury::CHECK_INTERVAL_MINUTES;
      checkInjuries();
    }
  }
  if (state == MatchState::FULL_TIME)
  {
    return;
  }

  ratingRefreshTimer += dt;
  if (ratingRefreshTimer >= MatchTuning::Timing::RATING_REFRESH_SECONDS)
  {
    ratingRefreshTimer -= MatchTuning::Timing::RATING_REFRESH_SECONDS;
    refreshRatings();
  }
  updateMatchClock();
}

bool MatchEngine::isInAddedTime() const
{
  return state != MatchState::HALF_TIME && state != MatchState::FULL_TIME &&
         state != MatchState::PENALTY_SHOOTOUT &&
         matchTimeMinutes >= MatchRules::periodEndMinute(period);
}

void MatchEngine::updateMatchClock()
{
  if (!drillRules.periods)
  {
    return;
  }
  if (state == MatchState::HALF_TIME || state == MatchState::FULL_TIME ||
      state == MatchState::GOAL || state == MatchState::PENALTY_SHOOTOUT)
  {
    return;
  }
  const auto half = static_cast<std::size_t>(period - 1);
  const float regulationEnd = MatchRules::periodEndMinute(period);
  if (matchTimeMinutes < regulationEnd)
  {
    return;
  }
  if (addedMinutes[half] == 0)
  {
    announceAddedTime();
  }

  // The announced added time is a minimum: stoppages during it extend it.
  const int required = std::max(addedMinutes[half], addedMinutesDue());
  const float end = regulationEnd + static_cast<float>(required);
  if (matchTimeMinutes < end)
  {
    return;
  }
  const bool dangerousPhase = state == MatchState::PENALTY ||
                              state == MatchState::CORNER_KICK ||
                              getPossessionState().isShot;
  if (dangerousPhase &&
      matchTimeMinutes < end + MatchTuning::Stoppage::MAX_OVERRUN_MINUTES)
  {
    return;
  }
  endPeriod();
}

void MatchEngine::announceAddedTime()
{
  const auto half = static_cast<std::size_t>(period - 1);
  addedMinutes[half] = addedMinutesDue();
  MatchEvent& event = logEvent(MatchEventType::ADDED_TIME);
  event.minutes = addedMinutes[half];
  event.addedMinute = 0.0F;
}

void MatchEngine::endPeriod()
{
  restartTakerIndex.reset();
  restartLineUp.reset();
  clearPossession();
  ball.velocity = {.x = 0.0F, .y = 0.0F};
  ball.velocityZ = 0.0F;
  clearFlightState();
  pendingAdvantage.active = false;
  if (abandoned)
  {
    finishMatch();
    return;
  }
  if (period == 1)
  {
    beginBreak(MatchTuning::Timing::HALF_TIME_PAUSE_SECONDS,
               MatchTuning::Fatigue::HALF_TIME_RECOVERY);
    return;
  }
  if (period == 3)
  {
    beginBreak(MatchTuning::Timing::EXTRA_TIME_HALF_TIME_SECONDS,
               MatchTuning::Fatigue::EXTRA_TIME_HALF_TIME_RECOVERY);
    return;
  }
  if (knockout.required && tieLevel() && !shootout.started)
  {
    if (period == 2 && knockout.extraTime)
    {
      extraTimeReached = true;
      beginBreak(MatchTuning::Timing::EXTRA_TIME_BREAK_SECONDS,
                 MatchTuning::Fatigue::EXTRA_TIME_BREAK_RECOVERY);
      return;
    }
    startShootout();
    return;
  }
  finishMatch();
}

void MatchEngine::beginBreak(float seconds, float recovery)
{
  state = MatchState::HALF_TIME;
  setPieceTimer = seconds;
  beginStoppage();
  // Into the tunnel on foot (touchline plan stage 4), else stopped where
  // they are.
  const bool walkingOff = walksOut();
  for (auto& player : players)
  {
    if (!active(player))
    {
      continue;
    }
    player.stamina = std::min(1.0F, player.stamina + recovery);
    player.sprintReserve = 1.0F;
    player.fastBurstDebt = 0.0F;
    if (!walkingOff)
    {
      player.velocity = {.x = 0.0F, .y = 0.0F};
    }
  }
  if (walkingOff)
  {
    beginWalkOff();
  }
  updateTeamPhases();
  logEvent(MatchEventType::HALF_TIME);
}

bool MatchEngine::tieLevel() const
{
  return homeScore + knockout.homeAggregate ==
         awayScore + knockout.awayAggregate;
}

void MatchEngine::setKnockout(const MatchRules::Knockout& rules)
{
  if (stepCounter != 0)
  {
    return;
  }
  knockout = rules;
}

std::optional<bool> MatchEngine::getTieWinnerHome() const
{
  if (!knockout.required || state != MatchState::FULL_TIME)
  {
    return std::nullopt;
  }
  const int home = homeScore + knockout.homeAggregate;
  const int away = awayScore + knockout.awayAggregate;
  if (home != away)
  {
    return home > away;
  }
  if (shootout.goals[0] != shootout.goals[1])
  {
    return shootout.goals[0] > shootout.goals[1];
  }
  return std::nullopt;
}

int MatchEngine::maxSubstitutions() const
{
  return MatchTuning::Rules::MAX_SUBSTITUTIONS_PER_TEAM +
         (extraTimeReached ? MatchTuning::Rules::EXTRA_TIME_SUBSTITUTIONS : 0);
}

void MatchEngine::startShootout()
{
  shootout.started = true;
  shootout.homeFirst = randomFloat(0.0F, 1.0F) < 0.5F;
  // Kicking order: the designated taker (or the best one), then the others
  // on the pitch by penalty ability, the goalkeeper last.
  const auto ability = [](const MatchPlayer& player)
  {
    return (player.shooting * 0.75F) + (player.passing * 0.15F) +
           (player.vision * 0.10F);
  };
  for (const bool home : {true, false})
  {
    const MatchPlayer* first = dutyTaker(home, SetPieceDuty::Penalties);
    std::vector<const MatchPlayer*> kickers;
    for (const MatchPlayer& player : players)
    {
      if (active(player) && player.isHomeTeam == home)
      {
        kickers.push_back(&player);
      }
    }
    std::ranges::stable_sort(
        kickers,
        [&](const MatchPlayer* left, const MatchPlayer* right)
        {
          const auto rank = [&](const MatchPlayer* player)
          {
            return player == first        ? 0
                   : player->isGoalkeeper ? 3
                   : player->isInjured    ? 2
                                          : 1;
          };
          if (rank(left) != rank(right))
          {
            return rank(left) < rank(right);
          }
          return ability(*left) > ability(*right);
        });
    std::vector<PlayerID>& order = shootout.order[home ? 0 : 1];
    order.clear();
    for (const MatchPlayer* kicker : kickers)
    {
      order.push_back(kicker->player->getId());
    }
  }

  // Everyone waits by the halfway line; each side shoots at the goal it
  // attacked, facing the other side's keeper.
  state = MatchState::PENALTY_SHOOTOUT;
  shootout.inFlight = false;
  shootout.timer = MatchTuning::Timing::SHOOTOUT_START_SECONDS;
  restartTakerIndex.reset();
  restartLineUp.reset();
  resetLifecycleBall();
  deadBall = {};
  std::array<int, 2> placed{};
  for (MatchPlayer& player : players)
  {
    if (!active(player))
    {
      continue;
    }
    const std::size_t side = player.isHomeTeam ? 0 : 1;
    player.velocity = {.x = 0.0F, .y = 0.0F};
    if (player.isGoalkeeper)
    {
      keepers[side].state = GoalkeeperState::SET_POSITION;
      keepers[side].dive = {};
      player.position = PitchPoint{
          player.isHomeTeam
              ? MatchTuning::Pitch::PLAYER_EDGE_END_METRES
              : pitch.length - MatchTuning::Pitch::PLAYER_EDGE_END_METRES,
          pitch.atY(MatchTuning::Pitch::CENTRE)};
    }
    else
    {
      player.position =
          pitch.at(player.isHomeTeam ? 0.48F : 0.52F,
                   0.32F + (0.04F * static_cast<float>(placed[side]++)));
    }
    player.movementTarget = player.position;
    player.tacticalTarget = player.position;
    player.targetVelocity = {.x = 0.0F, .y = 0.0F};
    player.routeWaypoints = 0;
    player.arriveAtSpeed = false;
  }
  updateTeamPhases();
  logEvent(MatchEventType::PENALTY_SHOOTOUT);
}

void MatchEngine::beginShootoutKick()
{
  const int taken = shootout.kicks[0] + shootout.kicks[1];
  const bool home = (taken % 2 == 0) == shootout.homeFirst;
  const std::size_t side = home ? 0 : 1;
  MatchPlayer* taker = nullptr;
  if (const std::vector<PlayerID>& order = shootout.order[side]; !order.empty())
  {
    const PlayerID id =
        order[static_cast<std::size_t>(shootout.kicks[side]) % order.size()];
    for (MatchPlayer& player : players)
    {
      if (active(player) && player.player->getId() == id)
      {
        taker = &player;
      }
    }
  }
  shootout.kickerHome = home;
  shootout.taker = (taker != nullptr) ? taker->player->getId() : 0;
  if (taker == nullptr)
  {
    finishShootoutKick(MatchEventDetail::MISSED);
    return;
  }
  if (MatchPlayer* keeper = findGoalkeeper(!home))
  {
    keeper->position = PitchPoint{
        home ? pitch.length - MatchTuning::Pitch::PLAYER_EDGE_END_METRES
             : MatchTuning::Pitch::PLAYER_EDGE_END_METRES,
        pitch.atY(MatchTuning::Pitch::CENTRE)};
    keeper->velocity = {.x = 0.0F, .y = 0.0F};
    keeper->movementTarget = keeper->position;
    // On his line, on his feet, for each kick.
    keepers[home ? 1 : 0].dive = {};
    keepers[home ? 1 : 0].state = GoalkeeperState::SET_POSITION;
    keeper->isDiving = false;
    keeper->diveTimer = 0.0F;
  }
  // Against a Binturong keeper the kick is an in-match penalty (goalkeeper
  // plan step 9): the match's record kept aside until it is over.
  shootout.live = getBody(!home) == BodyId::BINTURONG;
  shootout.outcome.reset();
  if (shootout.live)
  {
    shootout.eventsBefore = events.size();
    shootout.statsBefore = stats;
    shootout.playerStatsBefore = playerStats;
    shootout.trackerBefore = tracker;
  }
  const PitchPoint spot{
      home ? pitch.length - MatchTuning::Pitch::PENALTY_SPOT_METRES
           : MatchTuning::Pitch::PENALTY_SPOT_METRES,
      pitch.atY(MatchTuning::Pitch::CENTRE)};
  taker->position = spot;
  taker->velocity = {.x = 0.0F, .y = 0.0F};
  taker->movementTarget = spot;
  resetShootoutBall();
  ball.position = spot;
  assignRestartOwner(taker->player);
  // He decides his kick before his run-up.
  decidePenalty(*taker);
  shootout.inFlight = true;
  if (shootout.live)
  {
    // He sets off; the keeper guesses now and the strike comes at the end
    // of the run-up (goalkeeper plan step 9).
    shootout.struck = false;
    shootout.strikeAt =
        getSimulatedSeconds() + MatchTuning::KeeperDive::PENALTY_RUN_UP_SECONDS;
    if (MatchPlayer* keeper = findGoalkeeper(!home))
    {
      planPenaltyDiveBinturong(*keeper, shootout.strikeAt);
    }
    return;
  }
  strikeShot(*taker, MatchTuning::Shooting::PENALTY_XG, false, true);
  planGoalkeeperDive(!home, true);
}

void MatchEngine::updateShootout(float dt)
{
  if (!shootout.inFlight)
  {
    shootout.timer -= dt;
    if (shootout.timer <= 0.0F)
    {
      beginShootoutKick();
    }
    return;
  }
  MatchPlayer* keeper = findGoalkeeper(!shootout.kickerHome);
  if (keeper != nullptr)
  {
    diveGoalkeeper(*keeper, dt);
  }
  if (shootout.live)
  {
    // As an in-match penalty (goalkeeper plan step 9): the open-play ball,
    // his body, hands and dive. It ends when it is in or out (the lines,
    // shootoutBallOut), held, no longer a shot (off him: saved; else
    // missed), or spent. No rebounds. Struck at the end of the run-up.
    if (!shootout.struck)
    {
      if (getSimulatedSeconds() < shootout.strikeAt)
      {
        return;
      }
      shootout.struck = true;
      MatchPlayer* taker = nullptr;
      for (MatchPlayer& player : players)
      {
        if (active(player) && player.player->getId() == shootout.taker)
        {
          taker = &player;
        }
      }
      if (taker != nullptr)
      {
        strikeShot(*taker, MatchTuning::Shooting::PENALTY_XG, false, true);
        planGoalkeeperDive(!shootout.kickerHome, true);
      }
      return;
    }
    state = MatchState::PLAYING;
    updateBall(dt);
    if (!shootout.outcome && state == MatchState::PLAYING)
    {
      const bool offKeeper = (keeper != nullptr) &&
                             getLastPossessionAttachment() == keeper->player;
      if (getPossessionAttachment() != nullptr)
      {
        shootout.outcome =
            offKeeper ? MatchEventDetail::SAVED : MatchEventDetail::MISSED;
      }
      else if (!getPossessionState().isShot)
      {
        shootout.outcome =
            offKeeper ? MatchEventDetail::SAVED : MatchEventDetail::MISSED;
      }
      else if (getPossessionState().shotElapsedSeconds >=
                   MatchTuning::Timing::SHOOTOUT_MAX_FLIGHT_SECONDS ||
               (getPossessionState().shotElapsedSeconds > 0.3F &&
                length(ball.velocity) < MatchTuning::Ball::DEAD_SHOT_SPEED))
      {
        shootout.outcome =
            offKeeper ? MatchEventDetail::SAVED : MatchEventDetail::MISSED;
      }
    }
    state = MatchState::PENALTY_SHOOTOUT;
    if (!shootout.outcome)
    {
      return;
    }
    // The match's record as it was: a shootout kick is not match play.
    stats = shootout.statsBefore;
    playerStats = shootout.playerStatsBefore;
    tracker = shootout.trackerBefore;
    if (events.size() > shootout.eventsBefore)
    {
      events.resize(shootout.eventsBefore);
    }
    undescribedEvents = std::min(undescribedEvents, events.size());
    shootout.live = false;
    finishShootoutKick(*shootout.outcome);
    updateTeamPhases();
    return;
  }
  const float lineX = shootout.kickerHome ? pitch.atX(1.0F) : 0.0F;
  const int substeps =
      MatchTuning::Timing::BALL_SUBSTEPS * static_cast<int>(stepTicks);
  const float substep = dt / static_cast<float>(substeps);
  for (int index = 0; index < substeps; ++index)
  {
    substepBallPosition = ball.position;
    substepBallZ = ball.z;
    integrateBall(substep);
    advanceShotFlightClock(substep);
    if ((keeper != nullptr) && !getPossessionState().shotSaveResolved)
    {
      const float before = substepBallPosition.x - keeper->position.x;
      const float after = ball.position.x - keeper->position.x;
      if ((before > 0.0F) != (after > 0.0F) || std::abs(after) <= EPSILON)
      {
        noteKeeperShotResolved();
        if (getPossessionState().shotOnTarget && attemptSave(*keeper).saved)
        {
          finishShootoutKick(MatchEventDetail::SAVED);
          return;
        }
      }
    }
    const bool crossed = shootout.kickerHome ? ball.position.x >= lineX
                                             : ball.position.x <= lineX;
    if (crossed)
    {
      // In when it crosses the line inside the frame (the woodwork misses).
      const float span = ball.position.x - substepBallPosition.x;
      const float t =
          std::abs(span) > EPSILON
              ? std::clamp((lineX - substepBallPosition.x) / span, 0.0F, 1.0F)
              : 1.0F;
      const float lateralMetres =
          substepBallPosition.y +
          ((ball.position.y - substepBallPosition.y) * t) -
          pitch.atY(MatchTuning::Pitch::CENTRE);
      const float heightMetres = substepBallZ + ((ball.z - substepBallZ) * t);
      const bool inside =
          std::abs(lateralMetres) <
              pitch.goal.halfWidth - MatchTuning::Units::BALL_RADIUS_METRES &&
          heightMetres <
              pitch.goal.crossbar - MatchTuning::Units::BALL_RADIUS_METRES;
      finishShootoutKick(inside ? MatchEventDetail::SCORED
                                : MatchEventDetail::MISSED);
      return;
    }
    if (getPossessionState().shotElapsedSeconds >=
            MatchTuning::Timing::SHOOTOUT_MAX_FLIGHT_SECONDS ||
        (getPossessionState().shotElapsedSeconds > 0.3F &&
         length(ball.velocity) < MatchTuning::Ball::DEAD_SHOT_SPEED))
    {
      finishShootoutKick(MatchEventDetail::MISSED);
      return;
    }
  }
}

void MatchEngine::shootoutBallOut()
{
  // Off the pitch: in when it crossed the goal line inside the frame (the
  // woodwork already played in the sweep), otherwise out. Play stops.
  if (ballInPlayAt(ball.position))
  {
    return;
  }
  const float lineX = shootout.kickerHome ? pitch.atX(1.0F) : 0.0F;
  const bool crossed =
      shootout.kickerHome ? ball.position.x >= lineX : ball.position.x <= lineX;
  bool inside = false;
  if (crossed)
  {
    const float span = ball.position.x - substepBallPosition.x;
    const float t =
        std::abs(span) > EPSILON
            ? std::clamp((lineX - substepBallPosition.x) / span, 0.0F, 1.0F)
            : 1.0F;
    const float lateralMetres =
        substepBallPosition.y +
        ((ball.position.y - substepBallPosition.y) * t) -
        pitch.atY(MatchTuning::Pitch::CENTRE);
    const float heightMetres = substepBallZ + ((ball.z - substepBallZ) * t);
    inside =
        std::abs(lateralMetres) <
            pitch.goal.halfWidth - MatchTuning::Units::BALL_RADIUS_METRES &&
        heightMetres <
            pitch.goal.crossbar - MatchTuning::Units::BALL_RADIUS_METRES;
  }
  shootout.outcome =
      inside ? MatchEventDetail::SCORED : MatchEventDetail::MISSED;
  state = MatchState::PENALTY_SHOOTOUT;
}

void MatchEngine::finishShootoutKick(MatchEventDetail outcome)
{
  const std::size_t side = shootout.kickerHome ? 0 : 1;
  ++shootout.kicks[side];
  if (outcome == MatchEventDetail::SCORED)
  {
    ++shootout.goals[side];
  }
  ++stats.shootoutKicks;
  if (outcome == MatchEventDetail::SCORED)
  {
    ++stats.shootoutGoals;
  }
  if (outcome == MatchEventDetail::SAVED)
  {
    ++stats.shootoutSaves;
  }
  shootout.inFlight = false;
  shootout.timer = MatchTuning::Timing::SHOOTOUT_KICK_INTERVAL_SECONDS;
  clearFlightState();
  clearPossession();
  ball.velocity = {.x = 0.0F, .y = 0.0F};
  ball.velocityZ = 0.0F;
  keepers[shootout.kickerHome ? 1 : 0].state = GoalkeeperState::SET_POSITION;
  MatchPlayer* taker = nullptr;
  for (MatchPlayer& player : players)
  {
    if (active(player) && player.player->getId() == shootout.taker)
    {
      taker = &player;
    }
  }
  MatchEvent& event = (taker != nullptr)
                          ? logEvent(MatchEventType::PENALTY_SHOOTOUT, *taker)
                          : logEvent(MatchEventType::PENALTY_SHOOTOUT);
  event.detail = outcome;
  event.hasTeam = true;
  event.isHomeTeam = shootout.kickerHome;
  if (taker != nullptr)
  {
    // The taker walks back to his team-mates.
    taker->position =
        pitch.at(taker->isHomeTeam ? 0.47F : 0.53F, MatchTuning::Pitch::CENTRE);
    taker->movementTarget = taker->position;
  }
  if (MatchRules::shootoutDecided(shootout.goals[0], shootout.kicks[0],
                                  shootout.goals[1], shootout.kicks[1]))
  {
    finishMatch();
  }
}

void MatchEngine::finishMatch()
{
  state = MatchState::FULL_TIME;
  for (const auto& player : players)
  {
    if (active(player))
    {
      playerStats[player.statsIndex].condition = player.stamina;
    }
  }
  refreshRatings();
  updateTeamPhases();
  logEvent(MatchEventType::FULL_TIME);
}

void MatchEngine::updateTeamPhases()
{
  if (state == MatchState::HALF_TIME || state == MatchState::FULL_TIME)
  {
    homePhase = TeamPhase::STOPPAGE;
    awayPhase = TeamPhase::STOPPAGE;
    return;
  }
  if (state != MatchState::PLAYING)
  {
    homePhase = TeamPhase::SET_PIECE;
    awayPhase = TeamPhase::SET_PIECE;
    transitionSecondsRemaining = 0.0F;
    if (const MatchPlayer* restartOwner = findMatchPlayer(getRestartOwner()))
    {
      rememberPhaseTeam(restartOwner->isHomeTeam);
    }
    return;
  }

  const MatchPlayer* carrier = findMatchPlayer(getPossessionAttachment());
  if ((carrier != nullptr) && !getPossessionHistory().lastControlledTeamHome)
  {
    rememberPhaseTeam(carrier->isHomeTeam);
  }

  const auto setTransitionPhases = [&](bool attackingHome)
  {
    homePhase = attackingHome ? TeamPhase::ATTACKING_TRANSITION
                              : TeamPhase::DEFENSIVE_TRANSITION;
    awayPhase = attackingHome ? TeamPhase::DEFENSIVE_TRANSITION
                              : TeamPhase::ATTACKING_TRANSITION;
  };
  if (transitionSecondsRemaining > 0.0F &&
      getPossessionHistory().lastControlledTeamHome)
  {
    setTransitionPhases(*getPossessionHistory().lastControlledTeamHome);
    return;
  }

  const auto setControlledPhases = [&](bool attackingHome, float ballX)
  {
    const bool finalThird =
        attackingHome
            ? ballX >= pitch.atX(MatchTuning::Rules::HOME_FINAL_THIRD_START)
            : ballX <= pitch.atX(MatchTuning::Rules::AWAY_FINAL_THIRD_START);
    const TeamPhase attackingPhase =
        finalThird ? TeamPhase::FINAL_THIRD : TeamPhase::POSSESSION;
    homePhase = attackingHome ? attackingPhase : TeamPhase::DEFENSIVE_BLOCK;
    awayPhase = attackingHome ? TeamPhase::DEFENSIVE_BLOCK : attackingPhase;
  };
  if (carrier != nullptr)
  {
    setControlledPhases(carrier->isHomeTeam, carrier->position.x);
    return;
  }

  // An intentional pass or shot is still part of the attacking team's
  // controlled phase. Treating every ball flight as a transition made both
  // teams repeatedly abandon their coordinated support and defensive shape.
  if ((getPossessionState().isPass || getPossessionState().isShot) &&
      getPossessionHistory().lastControlledTeamHome)
  {
    setControlledPhases(*getPossessionHistory().lastControlledTeamHome,
                        ball.position.x);
    return;
  }

  if (!getPossessionHistory().lastControlledTeamHome)
  {
    homePhase = TeamPhase::DEFENSIVE_TRANSITION;
    awayPhase = TeamPhase::DEFENSIVE_TRANSITION;
    return;
  }

  setTransitionPhases(*getPossessionHistory().lastControlledTeamHome);
}

void MatchEngine::updateMovement(float dt)
{
  // Jitter is observed over unbroken open play: a stoppage starts afresh.
  if (stepCounter != lastJitterStep + stepTicks)
  {
    for (JitterTrack& track : jitterTracks)
    {
      track.planned = false;
      track.moving = false;
    }
  }
  lastJitterStep = stepCounter;

  // Wait dead zone plan step 1 (observed only): the intent each waiting
  // player held, to see whether this step's brain or chase changes it.
  struct HeldIntent
  {
    PitchPoint target{};
    Vector2F run{};
    bool asked = false;
    float facing = 0.0F;
  };
  std::array<HeldIntent, 32> held{};
  const std::uint32_t waiting = ball.touchWaiting;
  const PitchPoint heldMeetPoint = ball.meetPoint;
  const float heldMeetPace = ball.meetPace;
  for (std::size_t slot = 0; slot < players.size() && slot < held.size();
       ++slot)
  {
    if ((waiting & (1U << slot)) != 0U)
    {
      const MatchPlayer& player = players[slot];
      held[slot] = {.target = player.tacticalTarget,
                    .run = player.targetVelocity,
                    .asked = player.facingRequested,
                    .facing = player.facingRequest};
    }
  }

  // Off-ball targets refresh at the tactical rate, and immediately when the
  // ball changes hands or starts/ends a flight; bodies move every step.
  const std::uint8_t flags = static_cast<std::uint8_t>(
      (getPossessionState().isPass ? 1U : 0U) |
      (getPossessionState().isShot ? 2U : 0U) |
      (getPossessionHistory().lastControlledTeamHome
           ? (*getPossessionHistory().lastControlledTeamHome ? 4U : 8U)
           : 0U) |
      (hasTouchClaim() ? 16U : 0U));
  const bool scheduled =
      periodElapsed(MatchTuning::Timing::TACTICAL_REFRESH_STEPS);
  const bool forced =
      getPossessionAttachment() != tacticalOwner || flags != tacticalFlags;
  if (scheduled || forced)
  {
    tacticalOwner = getPossessionAttachment();
    tacticalFlags = flags;
    if (forced)
    {
      ++planEpoch;
    }
    // A player the plan switches into pressing the ball starts a pressure.
    std::array<PlayerIntent, 32> previousIntents{};
    const std::size_t planned =
        std::min(players.size(), previousIntents.size());
    for (std::size_t slot = 0; slot < planned; ++slot)
    {
      previousIntents[slot] = players[slot].intent;
    }
    // Each side's brain plans its own players: marks first, from one
    // snapshot of the play, then the targets.
    if (periodElapsed(MatchTuning::Shape::MARK_REFRESH_STEPS))
    {
      assignMarks(true);
      assignMarks(false);
    }
    refreshTacticalTargets(dt, true);
    refreshTacticalTargets(dt, false);
    // Drills hold some players' targets over the plan.
    if (!drillTargets.empty())
    {
      applyDrillTargets();
    }
    // Then each runner's overtake, on the targets as they now stand, in
    // slot order (a plan reads the routes of those planned before it).
    for (std::size_t slot = 0; slot < players.size(); ++slot)
    {
      planOvertake(slot);
    }
    // Then a loose-ball chaser's way round anyone in his way.
    for (std::size_t slot = 0; slot < players.size(); ++slot)
    {
      planWayRound(slot);
    }
    // Where the owner of a claimed ball means to take it: the chase below
    // overrides his movement, his touches aim here.
    if (hasTouchClaim())
    {
      if (const MatchPlayer* owner = findMatchPlayer(getPossessionAttachment()))
      {
        ball.dribbleGoal = owner->tacticalTarget;
      }
    }
    for (std::size_t slot = 0; slot < planned; ++slot)
    {
      MatchPlayer& player = players[slot];
      if (player.intent == PlayerIntent::PRESS_BALL &&
          previousIntents[slot] != PlayerIntent::PRESS_BALL)
      {
        ++statsOf(player).pressures;
      }
    }
    observePlanJitter(scheduled && !forced);
  }
  else
  {
    // Between refreshes only the keepers re-read the play.
    const MatchPlayer* holder = findMatchPlayer(getPossessionAttachment());
    for (auto& player : players)
    {
      if (!active(player) || !player.isGoalkeeper || keeperInDive(player))
      {
        continue;
      }
      if (!(getPossessionState().isPass &&
            getExpectedReceiver() == player.player))
      {
        player.tacticalTarget = goalkeeperTarget(player, holder);
        player.routeWaypoints = 0;
      }
    }
  }

  if (hasTouchClaim())
  {
    chaseTouchedBall();
  }
  else
  {
    ball.meetFor = nullptr;
  }

  waitIntentChanged = 0;
  for (std::size_t slot = 0; slot < players.size() && slot < held.size();
       ++slot)
  {
    if ((waiting & (1U << slot)) == 0U)
    {
      continue;
    }
    const MatchPlayer& player = players[slot];
    const HeldIntent& was = held[slot];
    bool changed = player.tacticalTarget.x != was.target.x ||
                   player.tacticalTarget.y != was.target.y ||
                   player.targetVelocity.x != was.run.x ||
                   player.targetVelocity.y != was.run.y ||
                   player.facingRequested != was.asked ||
                   (was.asked && player.facingRequest != was.facing);
    // His own ball: the meet he runs to is his intent too.
    if (player.player == getPossessionAttachment())
    {
      changed = changed || ball.meetPoint.x != heldMeetPoint.x ||
                ball.meetPoint.y != heldMeetPoint.y ||
                ball.meetPace != heldMeetPace;
    }
    if (changed)
    {
      waitIntentChanged |= 1U << slot;
    }
  }

  // Every target was read from the same snapshot of the play; now the bodies
  // move: diving keepers, players coasting after a collision, the
  // controlled player and a batch of the rest.
  rememberStepStart();
  std::array<std::uint8_t, 32> batch;
  std::size_t batched = 0;
  for (std::size_t slot = 0; slot < players.size() && slot < batch.size();
       ++slot)
  {
    MatchPlayer& player = players[slot];
    if (!active(player))
    {
      continue;
    }
    if (player.isGoalkeeper && keeperInDive(player))
    {
      diveGoalkeeper(player, dt);
    }
    else if (player.fallen)
    {
      fallenBinturong(player, dt);
    }
    else if (player.stumbleSeconds > 0.0F)
    {
      stumbleBinturong(player, dt);
    }
    else if (player.collisionPause > 0.0F)
    {
      coastBinturong(player, dt);
    }
    else if (controlledIndex && slot == *controlledIndex)
    {
      integrateControlled(player, dt);
    }
    else
    {
      batch[batched++] = static_cast<std::uint8_t>(slot);
    }
  }
  // Wait dead zone plan step 3: a player waiting on the ball (Binturong
  // brain and body) whose move this step would leave it unplayable carries
  // on instead as his wait read predicted. Before the bumps, which then see
  // only final moves.
  struct Waiter
  {
    std::uint8_t slot = 0;
    MatchPlayer before;
  };
  std::array<Waiter, 4> waiters{};
  std::size_t waiterCount = 0;
  for (std::size_t index = 0; index < batched && waiting != 0U; ++index)
  {
    const std::uint8_t slot = batch[index];
    const MatchPlayer& player = players[slot];
    if ((waiting & (1U << slot)) == 0U || player.isGoalkeeper ||
        !brainTraits(player.isHomeTeam).waitsForTouches ||
        getBody(player.isHomeTeam) != BodyId::BINTURONG ||
        waiterCount >= waiters.size())
    {
      continue;
    }
    waiters[waiterCount++] = {.slot = slot, .before = player};
  }
  movePlayers(batch.data(), batched, dt, false);
  for (std::size_t index = 0; index < waiterCount; ++index)
  {
    const Waiter& waiter = waiters[index];
    MatchPlayer& player = players[waiter.slot];
    if (waitPlayableNextSubstep(player, dt))
    {
      continue;
    }
    const MatchPlayer moved = player;
    const MatchPlayer& before = waiter.before;
    // As the read had him: the facing asked of him when he chose to wait.
    MatchPlayer reader = before;
    reader.facingRequested = held[waiter.slot].asked;
    reader.facingRequest = held[waiter.slot].facing;
    const float facing = predictedFacingBinturong(reader, dt);
    player = before;
    const Vector2F step = {.x = before.velocity.x * dt,
                           .y = before.velocity.y * dt};
    const PitchPoint wanted{before.position.x + step.x,
                            before.position.y + step.y};
    player.position = moveBody(before.position, wanted);
    if (player.position.x != wanted.x)
    {
      player.velocity.x = 0.0F;
    }
    if (player.position.y != wanted.y)
    {
      player.velocity.y = 0.0F;
    }
    player.facingAngle = facing;
    player.targetAngle = facing;
    ++firstTouchWaitLosses.carriedOn;
    if (!waitPlayableNextSubstep(player, dt))
    {
      // Not saved either way: the brain's move stands.
      player = moved;
      ++firstTouchWaitLosses.carriedOnStillLost;
    }
  }
  updateTouchline(dt);
  collidePlayers(dt);
  observeRunJitter(dt);
}

// ---------------------------------------------------------------------------
// Roles, opposition instructions and team talks
// ---------------------------------------------------------------------------

namespace
{
const RoleProfile NO_ROLE{};
}  // namespace

TacticalRole MatchEngine::getSlotRole(bool homeTeam, std::size_t slot) const
{
  const auto& tactics = slotTactics[homeTeam ? 0 : 1];
  return slot < tactics.size() ? tactics[slot].role : TacticalRole::Standard;
}

const RoleProfile& MatchEngine::roleProfileOf(const MatchPlayer& player) const
{
  const std::size_t side = player.isHomeTeam ? 0 : 1;
  if (player.isGoalkeeper)
  {
    return keeperProfiles[side];
  }
  if (player.formationSlot < 0)
  {
    return NO_ROLE;
  }
  const auto slot =
      static_cast<std::size_t>(static_cast<int>(player.formationSlot));
  if (slot >= slotTactics[side].size())
  {
    return NO_ROLE;
  }
  return slotTactics[side][slot].profile;
}

OppositionInstruction MatchEngine::instructionAgainst(
    const MatchPlayer& target) const
{
  const auto& orders = oppositionOrders[target.isHomeTeam ? 1 : 0];
  if (orders.empty() || (target.player == nullptr))
  {
    return OppositionInstruction::None;
  }
  const PlayerID id = target.player->getId();
  for (const PlayerInstruction& order : orders)
  {
    if (order.player == id)
    {
      return order.instruction;
    }
  }
  return OppositionInstruction::None;
}

float MatchEngine::weakFootPressure(const MatchPlayer& carrier) const
{
  if (instructionAgainst(carrier) != OppositionInstruction::ShowWeakFoot)
  {
    return 0.0F;
  }
  using D = MatchTuning::Decision;
  return std::clamp(
      (D::PRESSURE_RADIUS_METRES - nearestOpponentDistance(carrier)) /
          D::PRESSURE_RADIUS_METRES,
      0.0F, 1.0F);
}

float MatchEngine::doubleUpPenalty(const MatchPlayer& carrier,
                                   const MatchPlayer& defender) const
{
  if (instructionAgainst(carrier) != OppositionInstruction::DoubleUp)
  {
    return 0.0F;
  }
  for (const MatchPlayer& other : players)
  {
    if (&other == &defender || !active(other) ||
        other.isHomeTeam == carrier.isHomeTeam || other.isGoalkeeper)
    {
      continue;
    }
    if (distance(other.position, carrier.position) <=
        TacticsTuning::DOUBLE_UP_REACH_METRES)
    {
      return TacticsTuning::DOUBLE_UP_TAKE_ON_PENALTY;
    }
  }
  return 0.0F;
}

float MatchEngine::talkSwing(bool homeTeam) const
{
  return std::clamp(talkOf(homeTeam) / TacticsTuning::TALK_REFERENCE, -1.0F,
                    1.0F);
}

float MatchEngine::getTeamTalkEffect(bool homeTeam) const
{
  return talkOf(homeTeam);
}

void MatchEngine::movePlayers(const std::uint8_t* slots, std::size_t count,
                              float dt, bool walking)
{
  // Each side moves with its own body. A player's movement reads only his
  // own state, so moving the sides one after the other changes nothing.
  std::array<std::array<std::uint8_t, 32>, 2> sides{};
  std::array<std::size_t, 2> counts{};
  for (std::size_t index = 0; index < count; ++index)
  {
    const std::size_t side = players[slots[index]].isHomeTeam ? 0 : 1;
    if (counts[side] < sides[side].size())
    {
      sides[side][counts[side]++] = slots[index];
    }
  }
  // Training dummies (plan section 21): a player whose straight line runs
  // into one heads for its nearest edge for this step instead, and his own
  // target is put back after (unless his body changed it). A player on a
  // route (a drill's) follows his route; the keep-out still holds him.
  std::array<std::optional<PitchPoint>, 32> heldTargets{};
  std::array<PitchPoint, 32> steeredTargets{};
  // And one who runs into a dummy bounces off it: his velocity before the
  // move is what went into it.
  std::array<Vector2F, 32> velocitiesBefore{};
  if (!kit.dummies.empty())
  {
    for (std::size_t index = 0; index < count && index < heldTargets.size();
         ++index)
    {
      MatchPlayer& player = players[slots[index]];
      velocitiesBefore[index] = player.velocity;
      if (player.routeWaypoints > 0)
      {
        continue;
      }
      if (const std::optional<PitchPoint> steer =
              steerRoundDummies(player.position, player.movementTarget))
      {
        heldTargets[index] = player.movementTarget;
        steeredTargets[index] = *steer;
        player.movementTarget = *steer;
      }
    }
  }
  for (std::size_t side = 0; side < 2; ++side)
  {
    if (counts[side] > 0)
    {
      integrateMovements(sides[side].data(), counts[side], dt, walking);
    }
  }
  if (kit.dummies.empty())
  {
    return;
  }
  for (std::size_t index = 0; index < count && index < heldTargets.size();
       ++index)
  {
    MatchPlayer& player = players[slots[index]];
    bounceOffDummies(player, velocitiesBefore[index]);
    if (!heldTargets[index])
    {
      continue;
    }
    if (player.movementTarget.x == steeredTargets[index].x &&
        player.movementTarget.y == steeredTargets[index].y)
    {
      player.movementTarget = *heldTargets[index];
    }
  }
}

void MatchEngine::updateRestartMovement(float dt)
{
  // During a stoppage players walk to their restart positions (set-piece
  // slots, the restart spot or their last tactical target).
  const MatchPlayer* taker = restartTaker();
  rememberStepStart();
  std::array<std::uint8_t, 32> batch;
  std::size_t batched = 0;
  for (std::size_t slot = 0; slot < players.size() && slot < batch.size();
       ++slot)
  {
    MatchPlayer& player = players[slot];
    if (!active(player))
    {
      continue;
    }
    // A keeper set to dive for a penalty (goalkeeper plan step 9): his body
    // is the dive's.
    if (player.isGoalkeeper && keepers[player.isHomeTeam ? 0 : 1].dive.active)
    {
      diveGoalkeeper(player, dt);
      continue;
    }
    // The player fetching the ball for a free kick or penalty goes to it.
    const bool fetching = deadBall.active && deadBall.fetcherIndex == slot;
    if (player.isGoalkeeper && &player != taker && !fetching)
    {
      keepers[player.isHomeTeam ? 0 : 1].state = GoalkeeperState::SET_POSITION;
      if (!restartIsSetPiece && state != MatchState::GOAL)
      {
        player.movementTarget = goalkeeperTarget(player, nullptr);
      }
    }
    if (!fetching)
    {
      updateRestartSupport(player, taker);
    }
    // Not a player going off or a sub coming on (touchline plan stage 5):
    // the laws' distances are for those in play.
    const bool changingOver = std::ranges::any_of(
        pendingSubstitutions,
        [slot](const PendingSubstitution& pending)
        {
          return pending.slot == slot &&
                 pending.stage != PendingSubstitution::Stage::WAITING;
        });
    if (!fetching && &player != taker && !changingOver)
    {
      keepRestartDistance(player);
    }
    // While the ball is on its way back, everyone else waits on the pitch.
    if (deadBall.active && &player != taker && !fetching)
    {
      player.movementTarget = clampToPitch(player.movementTarget);
    }
    // A taker who lines the ball up needs room to walk up to it: everyone
    // else keeps clear of it (restart lining-up).
    if ((taker != nullptr) && &player != taker && !fetching &&
        linesUpRestart(*taker, state))
    {
      constexpr float ROOM = MatchTuning::Dribble::RESTART_ROOM_METRES;
      const Vector2F away = player.movementTarget - restartSpot();
      const float metres = length(away);
      if (metres < ROOM)
      {
        // Straight out from the ball (on it: back towards his own goal).
        const Vector2F out =
            metres > EPSILON
                ? Vector2F{.x = away.x / metres, .y = away.y / metres}
                : Vector2F{.x = player.isHomeTeam ? -1.0F : 1.0F, .y = 0.0F};
        const Vector2F step = {.x = out.x * ROOM, .y = out.y * ROOM};
        player.movementTarget =
            PitchPoint{clampToPitchX(restartSpot().x + step.x),
                       clampToPitchY(restartSpot().y + step.y)};
      }
    }
    // Going off for a sub (touchline plan stage 5): to his exit, and only
    // that.
    if (const PendingSubstitution* leaving = leavingFrom(slot))
    {
      player.movementTarget = PitchPoint{leaving->exit.x, leaving->exit.y};
    }
    player.intent = player.isGoalkeeper ? PlayerIntent::GOALKEEP
                                        : PlayerIntent::RECOVER_SHAPE;
    // His restart spot is where he is going: his tactical target too, as
    // the brain reads it when play restarts (still, and with no route).
    player.tacticalTarget = player.movementTarget;
    player.targetVelocity = {.x = 0.0F, .y = 0.0F};
    player.routeWaypoints = 0;
    player.arriveAtSpeed = false;
    player.facingRequested = false;
    if (&player == taker && possessionId == PossessionId::BINTURONG &&
        outOfPlayId == OutOfPlayId::AARDVARK && !restartLineUp &&
        linesUpRestart(player, state))
    {
      player.movementTarget = asPoint(RestartApproach::target(
          pitch, asVector(player.position), asVector(ball.position),
          MatchTuning::Player::ARRIVAL_DEAD_ZONE_METRES));
      player.tacticalTarget = player.movementTarget;
    }
    if (&player == taker && restartLineUp)
    {
      steerRestartLineUp(player, dt);
    }
    if (player.fallen)
    {
      fallenBinturong(player, dt);
    }
    else if (player.stumbleSeconds > 0.0F)
    {
      stumbleBinturong(player, dt);
    }
    else if (player.collisionPause > 0.0F)
    {
      coastBinturong(player, dt);
    }
    else
    {
      batch[batched++] = static_cast<std::uint8_t>(slot);
    }
  }
  movePlayers(batch.data(), batched, dt, true);
  updateTouchline(dt);
  collidePlayers(dt);
}

void MatchEngine::separatePlayers()
{
  // Resolve body contact after movement: players cannot overlap. Players who
  // left the pitch are static obstacles. Pairs are swept along the length of
  // the pitch so only neighbours within a body width are examined. Pairs who
  // both have the Binturong body meet by the collision plan instead
  // (collidePlayers); a pair with an Aardvark body in it is separated here.
  if (bodyOf(true) == BodyId::BINTURONG && bodyOf(false) == BodyId::BINTURONG)
  {
    return;
  }
  constexpr float MINIMUM = MatchTuning::Player::MINIMUM_BODY_SEPARATION_METRES;
  const float MINIMUM_X = MINIMUM;
  // The order persists between steps: players move little per step, so the
  // insertion sort is nearly linear. Ties break on the slot index, so the
  // result does not depend on the previous order.
  auto& order = separationOrder;
  const std::size_t count = std::min(players.size(), order.size());
  // Nobody moved since a pass that found no contact: nothing to resolve.
  bool unchanged = separationClear && separationCount == count;
  for (std::size_t index = 0; index < count && unchanged; ++index)
  {
    unchanged = separationPositions[index].x == players[index].position.x &&
                separationPositions[index].y == players[index].position.y;
  }
  if (unchanged)
  {
    return;
  }
  if (separationCount != count)
  {
    separationCount = count;
    for (std::size_t index = 0; index < count; ++index)
    {
      order[index] = static_cast<std::uint8_t>(index);
    }
  }
  std::array<float, 32> xs;
  for (std::size_t index = 0; index < count; ++index)
  {
    xs[index] = players[index].position.x;
  }
  separationClear = true;
  insertionSort(order, count,
                [&xs](std::uint8_t first, std::uint8_t second)
                {
                  if (xs[first] != xs[second])
                  {
                    return xs[first] < xs[second];
                  }
                  return first < second;
                });
  const auto push = [&xs, this](MatchPlayer& player, Vector2F offset)
  {
    player.position.x = clampToBoardsX(player.position.x + offset.x);
    player.position.y = clampToBoardsY(player.position.y + offset.y);
    player.velocity.x *= MatchTuning::Player::BODY_COLLISION_VELOCITY_RETAINED;
    player.velocity.y *= MatchTuning::Player::BODY_COLLISION_VELOCITY_RETAINED;
    xs[slotOf(player)] = player.position.x;
  };
  for (std::size_t firstIndex = 0; firstIndex < count; ++firstIndex)
  {
    MatchPlayer& first = players[order[firstIndex]];
    for (std::size_t secondIndex = firstIndex + 1; secondIndex < count;
         ++secondIndex)
    {
      if (xs[order[secondIndex]] - xs[order[firstIndex]] >= MINIMUM_X)
      {
        break;
      }
      MatchPlayer& second = players[order[secondIndex]];
      if (collidesByMomentum(first, second))
      {
        continue;
      }
      const bool firstMoves = first.onPitch;
      const bool secondMoves = second.onPitch;
      if (!firstMoves && !secondMoves)
      {
        continue;
      }
      Vector2F separationMetres = second.position - first.position;
      const float squared = (separationMetres.x * separationMetres.x) +
                            (separationMetres.y * separationMetres.y);
      if (squared >= MINIMUM * MINIMUM)
      {
        continue;
      }
      float separationLengthMetres = std::sqrt(squared);
      if (separationLengthMetres <= EPSILON)
      {
        separationMetres = {.x = 0.0F,
                            .y = order[firstIndex] % 2 == 0 ? 1.0F : -1.0F};
        separationLengthMetres = 1.0F;
      }
      const float share = firstMoves && secondMoves
                              ? MatchTuning::Player::BODY_SEPARATION_SHARE
                              : 1.0F;
      const float correctionMetres = (MINIMUM - separationLengthMetres) * share;
      const Vector2F correction = {
          .x = separationMetres.x / separationLengthMetres * correctionMetres,
          .y = separationMetres.y / separationLengthMetres * correctionMetres};
      if (firstMoves)
      {
        push(first, Vector2F{.x = -correction.x, .y = -correction.y});
      }
      if (secondMoves)
      {
        push(second, correction);
      }
      separationClear = false;
    }
  }
  for (std::size_t index = 0; index < count; ++index)
  {
    separationPositions[index] = players[index].position;
  }
}

void MatchEngine::accumulatePlayerLoad(float dt)
{
  // Measure actual travelled distance, then update the slow condition pool,
  // the fast sprint reserve and report totals together. Match-clock minutes
  // can be accelerated in Play, but physical exertion uses simulated seconds.
  // The final pass rejects restart teleports before applying load or fatigue.
  using L = MatchTuning::Load;
  using F = MatchTuning::Fatigue;
  const float clockScale = matchClockRate();
  const float clockDelta =
      dt * clockScale / MatchTuning::Timing::SECONDS_PER_MINUTE;
  for (const auto& player : players)
  {
    if (active(player))
    {
      statsOf(player).minutesPlayed += clockDelta;
    }
  }
  // Distance, intensity and energy are accounted over two steps at a time.
  loadSeconds += dt;
  if (loadAnchors.size() != players.size())
  {
    loadAnchors.resize(players.size());
    for (std::size_t index = 0; index < players.size(); ++index)
    {
      loadAnchors[index] = index < previousPlayerPositions.size()
                               ? previousPlayerPositions[index]
                               : players[index].position;
    }
  }
  if (!periodElapsed(MatchTuning::Timing::TACTICAL_REFRESH_STEPS))
  {
    return;
  }
  dt = loadSeconds;
  loadSeconds = 0.0F;
  // Gathered into arrays so the energy model (each side's body) runs as
  // straight-line (vectorisable) code; the statistics are then updated per
  // player.
  constexpr std::size_t LANES = FitnessLanes::LANES;
  FitnessLanes lanes;
  auto& [fromX, fromY, toX, toY, topSpeed, endurance, pressingEffort, sideDrain,
         stamina, reserve, stepMetres, nextStamina, nextReserve, stepEnergy,
         stepAnaerobicEnergy, stepBrakingEnergy, stepBrakingAnaerobicEnergy,
         fastDebt, nextFastDebt] = lanes;
  std::array<std::uint8_t, LANES> slots;
  std::size_t count = 0;
  std::size_t homeLanes = 0;
  for (std::size_t index = 0; index < players.size() && index < LANES; ++index)
  {
    MatchPlayer& player = players[index];
    const PitchPoint previous = loadAnchors[index];
    loadAnchors[index] = player.position;
    if (!active(player))
    {
      continue;
    }
    // A pressing side pays for it: the players hunting the ball most, and
    // the whole side while out of possession.
    const float pressing = getEffectiveSliders(player.isHomeTeam).pressing;
    const bool outOfPossession =
        getPossessionHistory().lastControlledTeamHome &&
        *getPossessionHistory().lastControlledTeamHome != player.isHomeTeam;
    const std::size_t lane = count++;
    // The home side's slots come first, so its lanes do too.
    if (player.isHomeTeam)
    {
      homeLanes = count;
    }
    slots[lane] = static_cast<std::uint8_t>(index);
    fromX[lane] = previous.x;
    fromY[lane] = previous.y;
    // A slide or roll on the floor is not running.
    if (player.fallen)
    {
      fromX[lane] = player.position.x;
      fromY[lane] = player.position.y;
    }
    toX[lane] = player.position.x;
    toY[lane] = player.position.y;
    topSpeed[lane] = player.maxSpeed;
    endurance[lane] = player.endurance;
    pressingEffort[lane] = player.isPressing ? pressing : 0.0F;
    sideDrain[lane] =
        outOfPossession ? std::max(0.0F, pressing - F::TEAM_PRESSING_NEUTRAL) *
                              F::TEAM_PRESSING_DRAIN
                        : 0.0F;
    stamina[lane] = player.stamina;
    reserve[lane] = player.sprintReserve;
    enduranceLanes.criticalSpeed[lane] = derivedPhysicalValue(
        DerivedStat::ENDURANCE, player.derivedStat(DerivedStat::ENDURANCE));
    enduranceLanes.burstMetres[lane] =
        derivedPhysicalValue(DerivedStat::BURST_CAPACITY,
                             player.derivedStat(DerivedStat::BURST_CAPACITY));
    enduranceLanes.recoverySeconds[lane] =
        derivedPhysicalValue(DerivedStat::RECOVERY_RATE,
                             player.derivedStat(DerivedStat::RECOVERY_RATE));
    enduranceLanes.fastRecoverySeconds[lane] =
        derivedPhysicalValue(DerivedStat::FAST_RECOVERY,
                             player.derivedStat(DerivedStat::FAST_RECOVERY));
    fastDebt[lane] = player.fastBurstDebt;
    enduranceLanes.fuelSeconds[lane] =
        derivedPhysicalValue(DerivedStat::FUEL_CAPACITY,
                             player.derivedStat(DerivedStat::FUEL_CAPACITY)) *
        MatchTuning::Timing::SECONDS_PER_MINUTE;
    enduranceLanes.transitionSpeed[lane] =
        MatchRules::walkRunTransitionSpeed(player.heightMetres);
    enduranceLanes.previousSpeed[lane] = player.loadSpeed;
    // Moving at an angle to a body orientation asked of him costs more per
    // metre: the eight directions' figure, blended by his angle.
    float directionCost = 1.0F;
    const Vector2F moved = player.position - previous;
    const float movedMetres = length(moved);
    if (player.facingRequested && movedMetres > EPSILON)
    {
      const Vector2F body = normalized({.x = std::cos(player.facingAngle),
                                        .y = std::sin(player.facingAngle)});
      const float alignment = std::clamp(
          (body.x * moved.x + body.y * moved.y) / movedMetres, -1.0F, 1.0F);
      directionCost = directionValue(MatchTuning::Player::DIRECTION_ENERGY,
                                     directionMix(std::acos(alignment)));
    }
    enduranceLanes.directionCost[lane] = directionCost;
  }
  spendEnergy(lanes, 0, homeLanes, dt, true);
  spendEnergy(lanes, homeLanes, count, dt, false);
  for (std::size_t lane = 0; lane < count; ++lane)
  {
    MatchPlayer& player = players[slots[lane]];
    const float metres = stepMetres[lane];
    // The 15-minute spell of the clock (observed only; extra time left out).
    const auto spell = [this]() -> std::size_t
    {
      const auto quarter = static_cast<std::size_t>(std::max(
          0.0F, (matchTimeMinutes - (period == 2 ? 45.0F : 0.0F)) / 15.0F));
      return (period == 2 ? 3U : 0U) + std::min<std::size_t>(quarter, 2U);
    };
    LoadProfileStats* profile = period <= 2 && !player.isGoalkeeper
                                    ? &loadProfile[player.isHomeTeam ? 0 : 1]
                                    : nullptr;
    if (profile != nullptr)
    {
      profile->playerSeconds[spell()] += dt;
    }
    // Restart repositioning teleports are not running.
    if (metres > MatchTuning::Player::MAX_COUNTED_STEP_SPEED * dt)
    {
      player.loadSpeed = length(player.velocity);
      continue;
    }
    const float speed = metres / dt;
    player.loadSpeed = speed;
    if (profile != nullptr)
    {
      profile->metres[spell()] += metres;
      if (speed >= L::HIGH_INTENSITY_SPEED)
      {
        profile->highIntensityMetres[spell()] += metres;
      }
    }
    PlayerMatchStats& entry = statsOf(player);
    entry.distanceMetres += metres;
    entry.metabolicEnergy += lanes.stepEnergy[lane];
    entry.anaerobicEnergy += lanes.stepAnaerobicEnergy[lane];
    entry.brakingEnergy += stepBrakingEnergy[lane];
    entry.brakingAnaerobicEnergy += stepBrakingAnaerobicEnergy[lane];
    static_assert(std::tuple_size_v<decltype(entry.modeSeconds)> ==
                  MOVEMENT_MODE_COUNT);
    const auto mode = static_cast<std::size_t>(movementModeAt(player, speed));
    entry.modeSeconds[mode] += dt;
    entry.modeMetres[mode] += metres;
    if (period == 2)
    {
      entry.secondHalfDistanceMetres += metres;
    }
    // Running speed, not the displacement from body contact.
    if (speed > entry.topSpeed)
    {
      entry.topSpeed =
          std::max(entry.topSpeed, std::min(speed, length(player.velocity)));
    }
    if (speed < L::JOG_SPEED)
    {
      entry.walkMetres += metres;
    }
    else if (speed < L::RUN_SPEED)
    {
      entry.jogMetres += metres;
    }
    else if (speed < L::HIGH_INTENSITY_SPEED)
    {
      entry.runMetres += metres;
    }
    if (speed >= L::RUN_SPEED)
    {
      // The 5-minute window of the half (added time left out).
      const float halfMinutes = matchTimeMinutes - (period == 2 ? 45.0F : 0.0F);
      const auto window =
          static_cast<std::size_t>(std::max(0.0F, halfMinutes) / 5.0F);
      constexpr std::size_t PER_HALF =
          PlayerMatchStats::FIVE_MINUTE_WINDOWS / 2;
      if (period <= 2 && window < PER_HALF)
      {
        entry.runningByFive[(period == 2 ? PER_HALF : 0) + window] += metres;
      }
    }
    if (speed >= L::HIGH_INTENSITY_SPEED)
    {
      entry.highIntensityMetres += metres;
      if (period == 2)
      {
        entry.secondHalfHighIntensityMetres += metres;
      }
    }
    if (speed >= L::SPRINT_SPEED)
    {
      entry.sprintMetres += metres;
      if (!player.isSprinting)
      {
        ++entry.sprints;
      }
      player.isSprinting = true;
    }
    else if (speed < L::SPRINT_EXIT_SPEED)
    {
      player.isSprinting = false;
    }
    player.stamina = nextStamina[lane];
    player.sprintReserve = nextReserve[lane];
    player.fastBurstDebt = nextFastDebt[lane];
    if (player.stamina <= 0.0F && entry.emptyAtMinute < 0.0F)
    {
      entry.emptyAtMinute = elapsedMatchMinutes;
    }
  }
}

void MatchEngine::resolvePossessionAndActions(float dt)
{
  // Owning the ball is not an unconditional right to act: first move the
  // dribble touch, then allow challenges, then check touch reach/cooldowns.
  // A tackle may transfer ownership or stop play, so return immediately when
  // that happens instead of letting the former carrier act with a stale ball.
  MatchPlayer* carrier = findMatchPlayer(getPossessionAttachment());
  if (carrier == nullptr)
  {
    return;
  }
  noteCarrierHistory(*carrier);
  if (hasTouchClaim())
  {
    // A claimed ball rolls free: he runs onto it and plays it touch by
    // touch (in the ball's sweep); he can be challenged meanwhile.
    updateTouch(*carrier, dt);
    // Claim expiry may release the owner. Do not challenge or act using the
    // former carrier later in this step (Aardvark retains its claim policy).
    if (getPossessionAttachment() != carrier->player)
    {
      return;
    }
  }
  else if (!updateDribble(*carrier, dt))
  {
    return;
  }

  // The controlled player tackles only on the button, when in reach.
  if (controlledIndex && controlInput.action != MatchInputAction::NONE)
  {
    MatchPlayer& controlled = players[*controlledIndex];
    const bool sliding = controlInput.action == MatchInputAction::SLIDE_TACKLE;
    if ((sliding || controlInput.action == MatchInputAction::TACKLE) &&
        active(controlled) && controlled.isHomeTeam != carrier->isHomeTeam &&
        controlled.tackleCooldown <= 0.0F && !controlled.isInjured &&
        !controlled.fallen)
    {
      using D = MatchTuning::Defending;
      const float reach = sliding ? D::SLIDE_TACKLE_DISTANCE_METRES
                                  : standingTackleReachOf(controlled.defending);
      // A loose dribbled ball is played by touching it, not by a standing
      // tackle: a free ball (tackling plan step 1) or a contest (step 2,
      // a Binturong body only).
      const bool touchedNotRolled =
          !sliding && hasTouchClaim() &&
          (!ballInReach(*carrier) ||
           getBody(controlled.isHomeTeam) == BodyId::BINTURONG);
      if (!touchedNotRolled &&
          distance(controlled.position, ball.position) <= reach)
      {
        controlInput.action = MatchInputAction::NONE;
        ++controlledActions[controlled.isHomeTeam ? 0 : 1];
        attemptTackle(*carrier, controlled, sliding);
        if (getPossessionAttachment() != carrier->player ||
            state != MatchState::PLAYING)
        {
          return;
        }
      }
    }
  }

  // A keeper holding the ball in his hands cannot be challenged.
  const bool inHands = carrier->isGoalkeeper &&
                       inPenaltyArea(carrier->position, carrier->isHomeTeam);
  if (!inHands)
  {
    if (const Challenge challenge = chooseChallenge(*carrier, dt);
        challenge.defender)
    {
      attemptTackle(*carrier, *challenge.defender, challenge.sliding);
      if (getPossessionAttachment() != carrier->player ||
          state != MatchState::PLAYING)
      {
        return;
      }
    }
  }

  // Nobody plays a ball that is still rolling to him: he acts once he has
  // collected it, and only with it at his feet.
  if (hasTouchClaim() && getConfirmedController() != carrier->player)
  {
    return;
  }

  // The AI waits out its decision time after a touch; a controlled player
  // can play the ball as soon as it is under control.
  const bool controlled = isControlled(*carrier);
  // A brain that decides for its meet, dribbling on a Binturong body,
  // decides for his next meet with the ball as soon as his decision time is
  // up (dribble meet plan 2a); a strike so decided waits for that meet.
  const bool forMeet =
      !controlled && hasTouchClaim() && ball.meetFor == carrier->player &&
      ball.meetSeconds <= MatchTuning::Dribble::MEET_DECISION_HORIZON_SECONDS &&
      brainTraits(carrier->isHomeTeam).decidesForMeet &&
      getBody(carrier->isHomeTeam) == BodyId::BINTURONG;
  if (pendingStrike && (pendingStrike->atMeet || pendingStrike->turning) &&
      pendingStrike->striker == carrier)
  {
    return;
  }
  if ((carrier->actionCooldown <= 0.0F ||
       (controlled && carrier->trapTimer <= 0.0F)) &&
      (forMeet ||
       ball.dribbleExposure <= MatchTuning::Dribble::KICK_REACH_METRES))
  {
    // A loose ball at his feet is struck where he next meets it in the
    // ball's sweep, not where it lies now.
    strikesWaitForSweep = hasTouchClaim();
    if (controlled)
    {
      performControlledAction(*carrier);
    }
    else if (!ballKeepers.empty() && (carrier->player != nullptr) &&
             std::ranges::find(ballKeepers, carrier->player->getId()) !=
                 ballKeepers.end())
    {
      // A drill's dribbler: he carries it where the drill sends him.
    }
    else if (forcedShooter != 0 && (carrier->player != nullptr) &&
             carrier->player->getId() == forcedShooter)
    {
      // A drill's forced shot: the AI's own shot, from where he stands.
      forcedShooter = 0;
      takeShot(*carrier);
    }
    else if (forcedPass.passer != 0 && (carrier->player != nullptr) &&
             carrier->player->getId() == forcedPass.passer)
    {
      // A drill's forced pass: the AI's own pass to the team-mate named.
      // (One meant first time he did not get to play so: after his touch.)
      const ForcedPass pass = forcedPass;
      forcedPass = {};
      if (const auto chosen = forcedPassOption(*carrier, pass))
      {
        passBall(*carrier, chosen->first, chosen->second);
      }
    }
    else if (forMeet)
    {
      decideActionAtMeet(*carrier);
    }
    else
    {
      decideAction(*carrier);
    }
    strikesWaitForSweep = false;
  }
}

std::optional<std::pair<MatchEngine::PassOption, bool>>
MatchEngine::forcedPassOption(MatchPlayer& carrier, const ForcedPass& pass)
{
  // A chip is lofted, a driven pass along the ground's side of the choice.
  const bool lofted = pass.style == ForcedPassStyle::LOFTED ||
                      pass.style == ForcedPassStyle::CHIP;
  const std::optional<PassTechnique> technique =
      pass.style == ForcedPassStyle::DRIVEN
          ? std::optional(PassTechnique::DRIVEN)
      : pass.style == ForcedPassStyle::CHIP ? std::optional(PassTechnique::CHIP)
                                            : std::nullopt;
  if (pass.spot)
  {
    // To a spot with nobody there: weighted as for a man standing on it,
    // and a lofted one lands on it.
    PassOption option;
    option.targetPoint = *pass.spot;
    option.passDistance = distance(carrier.position, *pass.spot);
    option.spin = pass.spin;
    option.arrivalSpeed = pass.arrivalSpeed;
    option.lofted =
        lofted ||
        (pass.style == ForcedPassStyle::AS_CHOSEN &&
         option.passDistance > MatchTuning::Passing::LOFTED_DISTANCE_METRES);
    option.technique = technique;
    return std::pair{option, lofted};
  }
  if (MatchPlayer* receiver = drillPlayer(pass.receiver);
      (receiver != nullptr) && active(*receiver))
  {
    PassOption option = evaluatePassOption(carrier, *receiver);
    if (pass.style == ForcedPassStyle::GROUND ||
        pass.style == ForcedPassStyle::DRIVEN)
    {
      option.lofted = false;
    }
    option.technique = technique;
    return std::pair{option, lofted};
  }
  return std::nullopt;
}

float MatchEngine::dribbleExposureShare() const
{
  using R = MatchTuning::Dribble;
  const float range = ball.dribbleTouchLength - R::FEET_METRES;
  if (range <= EPSILON)
  {
    return 0.0F;
  }
  return std::clamp((ball.dribbleExposure - R::FEET_METRES) / range, 0.0F,
                    1.0F);
}

// ---------------------------------------------------------------------------
// Contests: each player's body gives its half (takeOnMake, tackleResist, ...
// in the bodies); the contest settles them, rolls once and plays it out.
// ---------------------------------------------------------------------------

namespace
{
/** Appends a half's terms to a duel's breakdown, negated for the half that
 * counts against the chance. */
void addHalf(ScoreBreakdown& into, const ScoreBreakdown& half, float sign)
{
  for (const ScoreTerm& term : half.terms)
  {
    into.add(term.name, term.source, sign * term.value);
  }
}
}  // namespace

bool MatchEngine::attemptTakeOn(MatchPlayer& carrier, MatchPlayer& defender)
{
  using R = MatchTuning::Dribble;
  // A feint, a change of direction or a burst of pace against a jockeying
  // defender: the carrier's body against the defender's, plus the team edge,
  // a human defender jockeying (Play mode) and a second man doubling up.
  ScoreBreakdown makeTerms;
  ScoreBreakdown resistTerms;
  const bool detail = wantsDetail();
  const float make = takeOnMake(carrier, detail ? &makeTerms : nullptr);
  const float resist = takeOnResist(defender, detail ? &resistTerms : nullptr);
  const float edge =
      (teamEdge(carrier.isHomeTeam) - teamEdge(defender.isHomeTeam)) *
      MatchTuning::Rules::DUEL_EDGE_WEIGHT * R::TAKE_ON_SKILL;
  const float jockey = controlInput.jockey && isControlled(defender)
                           ? MatchTuning::Control::JOCKEY_TAKE_ON_PENALTY
                           : 0.0F;
  const float doubleUp = doubleUpPenalty(carrier, defender);
  const float success =
      std::clamp(R::TAKE_ON_BASE + make - resist + edge - jockey - doubleUp,
                 R::MIN_TAKE_ON, R::MAX_TAKE_ON);
  const float takeOnRoll = randomFloat(0.0F, 1.0F);
  const bool beaten = takeOnRoll < success;
  ++(carrier.isHomeTeam ? stats.homeTakeOns : stats.awayTakeOns);
  if (beaten)
  {
    ++(carrier.isHomeTeam ? stats.homeTakeOnsWon : stats.awayTakeOnsWon);
  }
  if (detail)
  {
    MatchDuelDetail duel;
    duel.kind = MatchDuelDetail::Kind::TAKE_ON;
    duel.player = (carrier.player != nullptr) ? carrier.player->getId() : 0;
    duel.opponent = (defender.player != nullptr) ? defender.player->getId() : 0;
    duel.homeTeam = carrier.isHomeTeam;
    duel.chance.total = success;
    duel.probability = success;
    duel.roll = takeOnRoll;
    duel.chance.add("base", TermSource::SITUATION, R::TAKE_ON_BASE);
    addHalf(duel.chance, makeTerms, 1.0F);
    addHalf(duel.chance, resistTerms, -1.0F);
    duel.chance.add("team edge (home, numbers, talk)", TermSource::SITUATION,
                    edge);
    duel.chance.add("human defender jockeying", TermSource::SITUATION, -jockey);
    duel.chance.add("doubled up on (order)", TermSource::INSTRUCTION,
                    -doubleUp);
    if (const float clamp = duel.chance.residual(); std::abs(clamp) > 1e-6F)
    {
      duel.chance.add("clamped to its range", TermSource::SITUATION, clamp);
    }
    if (MatchRecorder* recorder = getRecorder())
    {
      recorder->onDuelDetail(*this, duel);
    }
  }
  if (MatchRecorder* recorder = getRecorder())
  {
    MatchActionRecord action;
    action.kind = MatchActionKind::TAKE_ON;
    action.player = (carrier.player != nullptr) ? carrier.player->getId() : 0;
    action.homeTeam = carrier.isHomeTeam;
    action.target = (defender.player != nullptr) ? defender.player->getId() : 0;
    action.from = carrier.position;
    action.to = defender.position;
    action.estimate = success;
    action.result = beaten ? MatchDuelResult::WON : MatchDuelResult::LOST;
    recorder->onAction(*this, action);
  }
  if (!beaten)
  {
    // Read and stopped: the defender takes the ball (a clean challenge).
    attemptTackle(carrier, defender, false);
    return getPossessionAttachment() == carrier.player;
  }
  // Beaten: the defender is wrong-footed (his body) and the ball is pushed
  // past him.
  recoverFromTakeOn(defender);
  const Vector2F forward{.x = carrier.isHomeTeam ? 1.0F : -1.0F, .y = 0.0F};
  const Vector2F toDefender =
      metricDirection(carrier.position, defender.position);
  const float side = toDefender.y >= 0.0F ? -1.0F : 1.0F;
  const float angle = side * R::TAKE_ON_ANGLE_RADIANS;
  ball.dribbleDirection = normalized(
      {.x = (forward.x * std::cos(angle)) - (forward.y * std::sin(angle)),
       .y = (forward.x * std::sin(angle)) + (forward.y * std::cos(angle))});
  ball.dribbleTouchLength = R::TAKE_ON_TOUCH_METRES;
  ball.dribbleExposure = R::TAKE_ON_TOUCH_METRES;
  ball.touchTimer = R::MAX_TOUCH_INTERVAL;
  if (hasTouchClaim())
  {
    // A loose ball (Binturong body) is pushed past him for real, into the
    // space he runs on to.
    const float speed = length(carrier.velocity);
    const float push = groundLaunchSpeed(
        R::TAKE_ON_TOUCH_METRES + (speed * R::LOOSE_LEAD_SECONDS),
        speed * R::LOOSE_ARRIVAL_SHARE);
    notePathChange();
    ball.velocity = {.x = ball.dribbleDirection.x * push,
                     .y = ball.dribbleDirection.y * push};
    // A take-on is a real dribble launch too. Retain its intermediate speed
    // for motion diagnostics: drag can make both step endpoints slower.
    if (possessionId == PossessionId::BINTURONG)
    {
      ball.touchLaunchSpeed = push;
    }
    ball.touchAttempts = 0;
    ball.ownTouchCooldown = R::LOOSE_TOUCH_INTERVAL;
    ++dribbleTouches;
  }
  return true;
}

void MatchEngine::attemptTackle(MatchPlayer& carrier, MatchPlayer& defender,
                                bool sliding)
{
  // Timing matters: a challenge while the ball is away from the attacker's
  // foot wins it cleanly; one into close control or from behind is a foul
  // risk.
  const float exposure = dribbleExposureShare();
  const Vector2F motion = normalized(carrier.velocity);
  const Vector2F toDefender =
      metricDirection(carrier.position, defender.position);
  const bool fromBehind =
      motion.x * toDefender.x + motion.y * toDefender.y < -0.3F;
  const bool shielding =
      length(carrier.velocity) < MatchTuning::Dribble::CLOSE_CONTROL_SPEED;
  const StrategySliders defenderStrategy =
      getEffectiveSliders(defender.isHomeTeam);
  // A booked player picks his challenges more carefully.
  const float caution = defender.yellowCards > 0
                            ? MatchTuning::Discipline::BOOKED_PLAYER_CAUTION
                            : 1.0F;
  defender.tackleCooldown =
      randomFloat(MatchTuning::Defending::MIN_TACKLE_COOLDOWN,
                  MatchTuning::Defending::MAX_TACKLE_COOLDOWN) *
      (MatchTuning::Defending::TACKLE_COOLDOWN_BASE_MULTIPLIER -
       defenderStrategy.pressing *
           MatchTuning::Defending::PRESSING_COOLDOWN_REDUCTION) /
      caution;

  if (defender.isHomeTeam)
  {
    ++stats.homeTackleAttempts;
  }
  else
  {
    ++stats.awayTackleAttempts;
  }
  ++statsOf(defender).tacklesAttempted;

  // The tackler's body against the carrier's; the rules settle the rest.
  ScoreBreakdown makeTerms;
  ScoreBreakdown resistTerms;
  ScoreBreakdown techniqueTerms;
  const bool detail = wantsDetail();
  MatchRules::TackleContext context;
  context.make = tackleMake(defender, detail ? &makeTerms : nullptr);
  context.resist =
      tackleResist(carrier, shielding, detail ? &resistTerms : nullptr);
  context.technique =
      tackleTechnique(defender, detail ? &techniqueTerms : nullptr);
  context.defenderEdge =
      teamEdge(defender.isHomeTeam) * MatchTuning::Rules::DUEL_EDGE_WEIGHT;
  context.carrierEdge =
      teamEdge(carrier.isHomeTeam) * MatchTuning::Rules::DUEL_EDGE_WEIGHT;
  context.pressing = defenderStrategy.pressing;
  context.riskTaking = defenderStrategy.riskTaking;
  context.exposure = exposure;
  context.sliding = sliding;
  context.fromBehind = fromBehind;
  context.inPenaltyArea = inPenaltyArea(carrier.position, defender.isHomeTeam);
  context.defenderBooked = defender.yellowCards > 0;
  const float winChance = MatchRules::tackleWinChance(context);
  const float foulPropensity = MatchRules::tackleFoulPropensity(context);
  const auto report = [&](MatchDuelResult result)
  {
    MatchRecorder* recorder = getRecorder();
    if (!recorder)
    {
      return;
    }
    MatchActionRecord action;
    action.kind = MatchActionKind::TACKLE;
    action.player = defender.player ? defender.player->getId() : 0;
    action.homeTeam = defender.isHomeTeam;
    action.target = carrier.player ? carrier.player->getId() : 0;
    action.from = defender.position;
    action.to = carrier.position;
    action.lofted = sliding;
    action.estimate = winChance;
    action.foulPropensity = foulPropensity;
    action.result = result;
    recorder->onAction(*this, action);
  };
  // Debugger detail: both chances broken down, with the rolls against them.
  const auto reportDetail =
      [&](float winRoll, float foulRoll, float foulThreshold)
  {
    if (!detail)
    {
      return;
    }
    using D = MatchTuning::Defending;
    MatchDuelDetail duel;
    duel.kind = MatchDuelDetail::Kind::TACKLE;
    duel.player = defender.player ? defender.player->getId() : 0;
    duel.opponent = carrier.player ? carrier.player->getId() : 0;
    duel.homeTeam = defender.isHomeTeam;
    duel.probability = winChance;
    duel.roll = winRoll;
    duel.foulThreshold = foulThreshold;
    duel.foulRoll = foulRoll;
    ScoreBreakdown& win = duel.chance;
    win.total = winChance;
    win.add("base", TermSource::SITUATION, D::BASE_WIN_CHANCE);
    addHalf(win, makeTerms, 1.0F);
    addHalf(win, resistTerms, -1.0F);
    win.add("team edge (home, numbers, talk)", TermSource::SITUATION,
            (context.defenderEdge * D::DEFENDING_WIN_BONUS) -
                (context.carrierEdge * D::DRIBBLING_WIN_PENALTY));
    win.add("pressing instruction", TermSource::INSTRUCTION,
            context.pressing * D::PRESSING_WIN_EFFECT);
    win.add("ball away from his feet", TermSource::SITUATION,
            std::clamp(exposure, 0.0F, 1.0F) * D::EXPOSURE_WIN_BONUS);
    win.add("sliding", TermSource::SITUATION,
            sliding ? -D::SLIDE_WIN_PENALTY : 0.0F);
    win.add("from behind", TermSource::SITUATION,
            fromBehind ? -D::FROM_BEHIND_WIN_PENALTY : 0.0F);
    if (const float clamp = win.residual(); std::abs(clamp) > 1e-6F)
    {
      win.add("clamped to its range", TermSource::SITUATION, clamp);
    }

    // The propensity is a sum scaled by factors; each factor is noted as
    // what it adds.
    ScoreBreakdown& foul = duel.foul;
    foul.total = foulPropensity;
    float product = D::BASE_FOUL_CHANCE;
    foul.add("base", TermSource::SITUATION, D::BASE_FOUL_CHANCE);
    const auto part = [&](const char* name, TermSource source, float value)
    {
      foul.add(name, source, value);
      product += value;
    };
    part("risk-taking instruction", TermSource::INSTRUCTION,
         context.riskTaking * D::RISK_FOUL_BONUS);
    part("pressing instruction", TermSource::INSTRUCTION,
         context.pressing * D::PRESSING_FOUL_BONUS);
    for (const ScoreTerm& term : techniqueTerms.terms)
    {
      part(term.name, term.source, term.value);
    }
    part("team edge (home, numbers, talk)", TermSource::SITUATION,
         -context.defenderEdge * D::TECHNIQUE_FOUL_BONUS);
    const auto factor = [&](const char* name, TermSource source, float by)
    {
      foul.add(name, source, product * (by - 1.0F));
      product *= by;
    };
    factor("already booked", TermSource::GAME_STATE,
           context.defenderBooked
               ? MatchTuning::Discipline::BOOKED_PLAYER_CAUTION
               : 1.0F);
    factor("in his own penalty area", TermSource::LOCATION,
           context.inPenaltyArea ? D::PENALTY_AREA_FOUL_SCALE : 1.0F);
    factor("from behind", TermSource::SITUATION,
           fromBehind ? D::FROM_BEHIND_FOUL_FACTOR : 1.0F);
    factor("sliding", TermSource::SITUATION,
           sliding ? D::SLIDE_FOUL_FACTOR : 1.0F);
    factor("ball away from the man", TermSource::SITUATION,
           1.0F - (std::clamp(exposure, 0.0F, 1.0F) * D::EXPOSED_FOUL_RELIEF));
    std::erase_if(win.terms,
                  [](const ScoreTerm& term) { return term.value == 0.0F; });
    std::erase_if(foul.terms,
                  [](const ScoreTerm& term) { return term.value == 0.0F; });
    if (MatchRecorder* recorder = getRecorder())
    {
      recorder->onDuelDetail(*this, duel);
    }
  };
  const float winRoll = randomFloat(0.0F, 1.0F);
  if (winRoll < winChance)
  {
    ++(defender.isHomeTeam ? stats.homeTackleDuelsWon
                           : stats.awayTackleDuelsWon);
    // Even a challenge that reaches the ball can be late or through the man.
    const float foulRoll = incidentRoll();
    reportDetail(
        winRoll, foulRoll,
        foulPropensity * MatchTuning::Defending::WINNING_TACKLE_FOUL_SHARE);
    if (whistles(foulRoll <
                 foulPropensity *
                     MatchTuning::Defending::WINNING_TACKLE_FOUL_SHARE))
    {
      report(MatchDuelResult::FOUL);
      commitFoul(defender, carrier, true, sliding && fromBehind);
      return;
    }
    creditImmediateTackleWin(defender);
    if (randomFloat(0.0F, 1.0F) < MatchTuning::Defending::POKE_LOOSE_CHANCE)
    {
      report(MatchDuelResult::POKED_LOOSE);
      // The challenge knocks the ball away rather than winning it cleanly.
      const float angle =
          randomFloat(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);
      const float speed = randomFloat(MatchTuning::Defending::MIN_POKE_SPEED,
                                      MatchTuning::Defending::MAX_POKE_SPEED);
      beginPokedTackle(defender);
      ball.z = 0.0F;
      notePathChange();
      ball.velocity = {.x = std::cos(angle) * speed,
                       .y = std::sin(angle) * speed};
      ball.velocityZ = 0.0F;
      noteContactActor(defender.player,
                       MatchTuning::Passing::KICKER_LOCKOUT_SECONDS, false);
      carrier.trapTimer = MatchTuning::Ball::FAILED_TRAP_TIME;
      finishPokedTackle(defender);
      return;
    }
    report(MatchDuelResult::WON);
    setPossession(defender);
    defender.actionCooldown =
        randomFloat(MatchTuning::Defending::MIN_RECOVERY_COOLDOWN,
                    MatchTuning::Defending::MAX_RECOVERY_COOLDOWN);
    return;
  }

  const float foulRoll = incidentRoll();
  reportDetail(winRoll, foulRoll, foulPropensity);
  if (!whistles(foulRoll < foulPropensity))
  {
    report(MatchDuelResult::LOST);
    return;
  }
  report(MatchDuelResult::FOUL);
  commitFoul(defender, carrier, false, sliding && fromBehind);
}

void MatchEngine::applySanction(MatchPlayer& offender,
                                MatchRules::FoulSanction sanction)
{
  if (sanction == MatchRules::FoulSanction::NONE ||
      (offender.player == nullptr))
  {
    return;
  }
  PlayerMatchStats& entry = statsOf(offender);
  ++stoppageLogs[static_cast<std::size_t>(period - 1)].cards;
  if (sanction == MatchRules::FoulSanction::YELLOW)
  {
    ++offender.yellowCards;
    ++entry.yellowCards;
    if (offender.isHomeTeam)
    {
      ++stats.homeYellowCards;
    }
    else
    {
      ++stats.awayYellowCards;
    }
    logEvent(MatchEventType::YELLOW_CARD, offender);
    return;
  }

  if (sanction == MatchRules::FoulSanction::SECOND_YELLOW)
  {
    ++offender.yellowCards;
    ++entry.yellowCards;
    if (offender.isHomeTeam)
    {
      ++stats.homeYellowCards;
    }
    else
    {
      ++stats.awayYellowCards;
    }
    logEvent(MatchEventType::SECOND_YELLOW, offender);
  }
  else
  {
    logEvent(MatchEventType::RED_CARD, offender);
  }
  ++entry.redCards;
  entry.sentOff = true;
  // A sending-off always stops play; open the stoppage before any forced
  // goalkeeper change so it shares the restart's substitution window.
  beginStoppage();
  if (offender.isHomeTeam)
  {
    ++stats.homeRedCards;
  }
  else
  {
    ++stats.awayRedCards;
  }
  // With walk-outs he walks to the tunnel (touchline plan stage 6).
  removeFromPitch(offender, walksOut());
}

void MatchEngine::decideAction(MatchPlayer& carrier)
{
  executeBallAction(carrier, chooseBallAction(carrier));
}

void MatchEngine::executeBallAction(MatchPlayer& carrier,
                                    const BallAction& action)
{
  switch (action.kind)
  {
    case ScenarioAction::SHOT:
      takeShot(carrier);
      return;
    case ScenarioAction::CARRY:
    {
      carrier.actionCooldown = action.holdSeconds;
      // His brain decides whether to take a defender on.
      if (state != MatchState::PLAYING)
      {
        return;
      }
      if (MatchPlayer* defender = chooseTakeOn(carrier))
      {
        attemptTakeOn(carrier, *defender);
      }
      return;
    }
    case ScenarioAction::CLEAR:
      clearBall(carrier);
      return;
    case ScenarioAction::SHIELD:
      carrier.actionCooldown = action.holdSeconds;
      return;
    case ScenarioAction::PASS:
      if (action.pass)
      {
        passBall(carrier, *action.pass);
      }
      return;
    case ScenarioAction::NONE:
      return;
  }
}

void MatchEngine::passBall(MatchPlayer& passer, const PassOption& option,
                           bool forceLofted)
{
  // No receiver: a drill's pass to a spot (forcePassTo), with nobody there.
  MatchPlayer* const receiver = option.receiver;
  if ((receiver != nullptr) && (receiver->player == nullptr))
  {
    return;
  }
  PitchPoint target = option.targetPoint;
  const float passDistance = option.passDistance;
  if (passDistance <= EPSILON)
  {
    return;
  }
  if (holdStrikeForSweep({.kind = PendingStrike::Kind::PASS,
                          .striker = &passer,
                          .pass = option,
                          .forceLofted = forceLofted,
                          .shotAim = {},
                          .receiverSlot = {}}))
  {
    return;
  }
  const bool lofted = forceLofted || option.lofted;

  beginPassFlight(passer, option);
  lastPassDecision = {
      .passerId = (passer.player != nullptr) ? passer.player->getId() : 0,
      .receiverId = (receiver != nullptr) ? receiver->player->getId() : 0,
      .targetPoint = target,
      .intent =
          state == MatchState::PLAYING ? option.intent : PassIntent::SET_PIECE,
      .utility = option.utility,
      .progression = option.progression,
      .laneRisk = option.laneRisk,
      .completionProbability = option.completionProbability};
  const bool delivery = option.intent == PassIntent::CROSS ||
                        state == MatchState::CORNER_KICK ||
                        (state == MatchState::FREE_KICK && lofted);
  // The passer's body strikes it: where it really goes, how hard, how high.
  const Kick kick =
      kickPass(passer, receiver, target, passDistance, lofted, delivery,
               option.spin, option.arrivalSpeed, option.technique);
  target = kick.target;
  setPassFlightTarget(target);
  launchBall(passer, releaseFrom(passer, ReleaseKind::PASS), target, kick.speed,
             kick.verticalSpeed, kick.spin);
  finishPassFlight(lofted && delivery);
  if (getRecorder() != nullptr)
  {
    MatchActionRecord action;
    action.kind = MatchActionKind::PASS;
    action.target = (receiver != nullptr) ? receiver->player->getId() : 0;
    action.lofted = lofted;
    action.passIntent = lastPassDecision.intent;
    action.estimate = option.completionProbability;
    reportKick(action, passer, target);
  }
  if (passer.isGoalkeeper)
  {
    GoalkeeperControl& control = keepers[passer.isHomeTeam ? 0 : 1];
    control.state = GoalkeeperState::DISTRIBUTE;
    control.timer = 0.0F;
  }

  if (receiver != nullptr)
  {
    afterPassReleased(passer, *receiver);
  }

  if (passer.isHomeTeam)
  {
    ++stats.homePassesAttempted;
  }
  else
  {
    ++stats.awayPassesAttempted;
  }
  ++statsOf(passer).passesAttempted;
  trackPassRelease(passer);
  if (state == MatchState::PLAYING)
  {
    int* progressivePasses = passer.isHomeTeam ? &stats.homeProgressivePasses
                                               : &stats.awayProgressivePasses;
    int* throughBalls =
        passer.isHomeTeam ? &stats.homeThroughBalls : &stats.awayThroughBalls;
    int* crosses = passer.isHomeTeam ? &stats.homeCrosses : &stats.awayCrosses;
    int* cutbacks =
        passer.isHomeTeam ? &stats.homeCutbacks : &stats.awayCutbacks;
    int* switchesOfPlay = passer.isHomeTeam ? &stats.homeSwitchesOfPlay
                                            : &stats.awaySwitchesOfPlay;
    if (option.intent == PassIntent::PROGRESSIVE)
    {
      ++(*progressivePasses);
    }
    else if (option.intent == PassIntent::THROUGH_BALL)
    {
      ++(*throughBalls);
    }
    else if (option.intent == PassIntent::CROSS)
    {
      ++(*crosses);
    }
    else if (option.intent == PassIntent::CUTBACK)
    {
      ++(*cutbacks);
    }
    else if (option.intent == PassIntent::SWITCH_PLAY)
    {
      ++(*switchesOfPlay);
    }
  }
  passer.actionCooldown = recoverySeconds(passer, Recovery::PASS);
}

MatchEngine::ShotAim MatchEngine::pickShotAim(MatchPlayer& shooter, bool header,
                                              bool penalty, bool freeKick)
{
  ShotAim aim = chooseShotAim(shooter, header, penalty, freeKick);
  if (forcedShotAim)
  {
    // A drill's spot in the goal (forceShot).
    aim.aimY = forcedShotAim->x;
    aim.aimZ = forcedShotAim->y;
    forcedShotAim.reset();
  }
  return aim;
}

float MatchEngine::strikeShot(MatchPlayer& shooter, float forcedXG, bool header,
                              bool penalty, bool freeKick,
                              std::optional<ShotAim> heldAim)
{
  const float goalX = pitch.atX(shooter.isHomeTeam ? 1.0F : 0.0F);
  const PitchPoint origin = strikePoint(shooter);
  const float metres = distance(
      origin, PitchPoint{goalX, pitch.atY(MatchTuning::Pitch::CENTRE)});
  float xg = forcedXG >= 0.0F ? forcedXG : estimateShotXG(shooter);
  if (header && forcedXG < 0.0F)
  {
    xg *= MatchTuning::Shooting::HEADER_XG_FACTOR;
  }
  xg = std::clamp(xg, MatchTuning::Shooting::MIN_GOAL_PROBABILITY,
                  forcedXG >= 0.0F ? MatchTuning::Shooting::MAX_SET_PIECE_XG
                                   : MatchTuning::Shooting::MAX_OPEN_PLAY_XG);

  // The brain picks the spot; the shooter's body then misses it by an error
  // that grows with distance, pressure, weak technique, fatigue and headers.
  const float HALF_GOAL = pitch.goal.halfWidth;
  const ShotAim shotAim =
      heldAim ? *heldAim : pickShotAim(shooter, header, penalty, freeKick);
  float aimY = shotAim.aimY;
  float aimZ = shotAim.aimZ;
  const float inset = shotAim.inset;
  float spread = shotSpread(shooter, header, penalty, freeKick, metres);
  // Play mode: the human's aim and power (no effect on AI shots).
  if (controlledShot.active)
  {
    shapeControlledShot(shooter, HALF_GOAL, inset, aimY, aimZ, spread);
  }
  // Play mode's power, or a drill's share of his power (as a scale on the
  // usual share).
  float powerScale = controlledShot.active
                         ? MatchTuning::Control::SHOT_SPEED_MIN_SCALE +
                               (MatchTuning::Control::SHOT_SPEED_POWER_GAIN *
                                controlledShot.power)
                         : 1.0F;
  if (forcedShotPower)
  {
    powerScale = *forcedShotPower / MatchTuning::Shooting::DEFAULT_POWER_SHARE;
    forcedShotPower.reset();
  }
  // How it is struck: a drill's technique, else driven (the brain doesn't
  // choose yet).
  const std::optional<ShotTechnique> technique =
      header ? std::nullopt : forcedShotTechnique;
  forcedShotTechnique.reset();
  const ShotStrike strike = strikeShotBody(shooter, aimY, aimZ, spread, header,
                                           powerScale, technique);
  const float crossingY = strike.crossingY;
  const float crossingZ = strike.crossingZ;
  const bool onTarget =
      std::abs(crossingY) <
          HALF_GOAL - MatchTuning::Units::BALL_RADIUS_METRES &&
      crossingZ < pitch.goal.crossbar - MatchTuning::Units::BALL_RADIUS_METRES;
  const float targetY = pitch.atY(MatchTuning::Pitch::CENTRE) + crossingY;

  const float speed = strike.speed;
  const PitchPoint goalPoint{goalX, targetY};
  // His body releases it: where from, and how high.
  const BallRelease from = releaseFrom(
      shooter, header ? ReleaseKind::HEADED_SHOT : ReleaseKind::SHOT);
  const float startHeight = from.heightMetres;
  const float goalDistance = distance(origin, goalPoint);
  const int blockBand = noteBlockPrediction(shooter, header, penalty, freeKick);
  clearFlightState();
  launchBall(shooter, from, strike.launchToward.value_or(goalPoint), speed,
             strike.launchSet ? strike.verticalSpeed
                              : loftVerticalSpeed(goalDistance, speed,
                                                  startHeight, crossingZ),
             strike.spin);
  noteShotFlight(shooter, onTarget, xg, targetY, crossingZ, header, penalty);
  // Where it leaves from (for the lab: its path is from here to the
  // crossing).
  ball.possession.shotFrom = ball.position;
  ball.possession.shotBlockBand = blockBand;
  return xg;
}

int MatchEngine::noteBlockPrediction(const MatchPlayer& shooter, bool header,
                                     bool penalty, bool freeKick)
{
  if (header || penalty || freeKick ||
      brainOf(shooter.isHomeTeam) != BrainId::CAPYBARA)
  {
    return -1;
  }
  const float stopped = std::clamp(
      1.0F - bodiesPassCapybara(shooter, shooter.position), 0.0F, 1.0F);
  const std::size_t band =
      std::min(MatchStats::BLOCK_BANDS - 1,
               static_cast<std::size_t>(
                   stopped * static_cast<float>(MatchStats::BLOCK_BANDS)));
  ++stats.blockBandShots.at(band);
  stats.blockBandPredicted.at(band) += stopped;
  return static_cast<int>(band);
}

void MatchEngine::takeShot(MatchPlayer& shooter, float forcedXG, bool header,
                           bool freeKick, std::optional<ShotAim> aim)
{
  if (!header && !freeKick && strikesWaitForSweep)
  {
    // Held for the sweep. The Binturong body picks his spot now, as he
    // decides (his body turns to it while it is held); the Aardvark body
    // when it is struck.
    PendingStrike held{.kind = PendingStrike::Kind::SHOT,
                       .striker = &shooter,
                       .pass = {},
                       .forceLofted = false,
                       .forcedXG = forcedXG,
                       .shotAim = {},
                       .receiverSlot = {}};
    if (getBody(shooter.isHomeTeam) == BodyId::BINTURONG)
    {
      held.shotAim = pickShotAim(shooter, false, false, false);
    }
    if (holdStrikeForSweep(held))
    {
      return;
    }
    // Not held after all: struck now (at the spot just picked, if any).
    aim = held.shotAim;
  }
  const bool penalty = state == MatchState::PENALTY;
  const float xg =
      strikeShot(shooter, forcedXG, header, penalty, freeKick, aim);

  PlayerMatchStats& shooterStats = statsOf(shooter);
  ++shooterStats.shots;
  shooterStats.expectedGoals += xg;
  const bool insideBox = inPenaltyArea(shooter.position, !shooter.isHomeTeam);
  const bool setPiece =
      penalty || restartIsSetPiece ||
      (setPiecePhaseRemaining > 0.0F && shooter.isHomeTeam == setPieceHome);
  noteShotSetPieceOrigin(setPiece);
  if (shooter.isHomeTeam)
  {
    ++stats.homeShots;
    stats.homeShotXG += xg;
    if (insideBox)
    {
      ++stats.homeShotsInsideBox;
    }
    if (header)
    {
      ++stats.homeHeadedShots;
    }
    if (setPiece)
    {
      ++stats.homeSetPieceShots;
    }
  }
  else
  {
    ++stats.awayShots;
    stats.awayShotXG += xg;
    if (insideBox)
    {
      ++stats.awayShotsInsideBox;
    }
    if (header)
    {
      ++stats.awayHeadedShots;
    }
    if (setPiece)
    {
      ++stats.awaySetPieceShots;
    }
  }

  // Credit the pass that created the chance.
  prepareShotAssist(shooter, header);
  if (MatchPlayer* creator =
          findMatchPlayer(getPossessionHistory().shotAssistCandidate);
      (creator != nullptr) && creator->isHomeTeam == shooter.isHomeTeam)
  {
    ++statsOf(*creator).keyPasses;
  }
  trackShot(shooter, xg, setPiece, header, penalty);

  MatchEvent& event = logEvent(MatchEventType::SHOT, shooter);
  if (header)
  {
    event.detail = MatchEventDetail::HEADER;
  }
  event.xg = xg;
  event.position = shooter.position;
  if (getRecorder() != nullptr)
  {
    // Aimed where the ball's flight crosses the goal line.
    const float goalX = pitch.atX(shooter.isHomeTeam ? 1.0F : 0.0F);
    const Vector2F velocity = ball.velocity;
    PitchPoint aim{goalX, ball.position.y};
    if (std::abs(velocity.x) > EPSILON)
    {
      aim.y += velocity.y * (goalX - ball.position.x) / velocity.x;
    }
    MatchActionRecord action;
    action.kind = MatchActionKind::SHOT;
    action.header = header;
    action.estimate = xg;
    reportKick(action, shooter, aim);
  }
  shooter.actionCooldown = recoverySeconds(shooter, Recovery::SHOT);
  planGoalkeeperDive(!shooter.isHomeTeam, penalty);
}

void MatchEngine::launchBall(const MatchPlayer& kicker, const BallRelease& from,
                             PitchPoint target, float horizontalSpeed,
                             float verticalSpeed, BallSpin spin)
{
  // Where and how high it leaves are his body's (releaseFrom); this only
  // sets it off.
  const Vector2F direction = metricDirection(from.position, target);
  sendBallOff(
      kicker, from.position, from.z,
      {.x = direction.x * horizontalSpeed, .y = direction.y * horizontalSpeed},
      verticalSpeed, spin, MatchTuning::Passing::KICKER_LOCKOUT_SECONDS);
  releasePossession(kicker);
}

bool MatchEngine::wantsDetail() const
{
  const MatchRecorder* recorder = getRecorder();
  return (recorder != nullptr) && recorder->wantsDetail();
}

void MatchEngine::reportKick(MatchActionRecord& action,
                             const MatchPlayer& kicker, PitchPoint target) const
{
  MatchRecorder* recorder = getRecorder();
  if (recorder == nullptr)
  {
    return;
  }
  action.player = (kicker.player != nullptr) ? kicker.player->getId() : 0;
  action.homeTeam = kicker.isHomeTeam;
  action.from = ball.position;
  action.to = target;
  action.speed = length(ball.velocity);
  action.verticalSpeed = ball.velocityZ;
  action.curve = ball.spin.side * MatchTuning::Ball::TURN_PER_SIDESPIN;
  recorder->onAction(*this, action);
}

void MatchEngine::planPenaltyDiveBinturong(MatchPlayer& keeper, double strikeAt)
{
  // From 11 m he guesses blind (goalkeeper plan step 9): a side, low or
  // high, pushing off around the strike (a little before it at best;
  // earlier or later the poorer he is); or he stays, and reacts as to any
  // shot. Planned as the taker sets off, so he can go before the strike.
  using D = MatchTuning::KeeperDive;
  GoalkeeperControl& control = keepers[keeper.isHomeTeam ? 0 : 1];
  control.penaltyGuessed = true;
  if (keeper.fallen || control.dive.active)
  {
    return;
  }
  control.state = GoalkeeperState::DIVE;
  control.timer = 0.0F;
  const float guess = choosePenaltyGuess(keeper);
  if (guess == 0.0F)
  {
    return;
  }
  const Vector2F side = normalized({.x = 0.0F, .y = guess});
  const float height = randomFloat(0.0F, 1.0F) < D::PENALTY_HIGH_CHANCE
                           ? D::PENALTY_HIGH_METRES
                           : D::PENALTY_LOW_METRES;
  const float spread =
      alongStatScale(keeper.goalkeeping, D::PENALTY_LEAD_SPREAD_POOR,
                     D::PENALTY_LEAD_SPREAD_BEST);
  const float lead =
      D::PENALTY_LEAD_SECONDS + (spread * randomFloat(-1.0F, 1.0F));
  control.dive.decidedFor = getPossessionState().pathSince;
  planKeeperDiveBinturong(keeper, control.dive,
                          strikeAt - static_cast<double>(lead),
                          Point3F{.x = side.x * D::PENALTY_SIDE_METRES,
                                  .y = side.y * D::PENALTY_SIDE_METRES,
                                  .z = height});
}

void MatchEngine::planGoalkeeperDive(bool defendingHome, bool penalty)
{
  MatchPlayer* keeper = findGoalkeeper(defendingHome);
  // Down on the floor (a collision), he can't dive.
  if ((keeper == nullptr) || keeper->fallen)
  {
    return;
  }
  GoalkeeperControl& control = keepers[defendingHome ? 0 : 1];
  if (getBody(defendingHome) == BodyId::BINTURONG)
  {
    // Goalkeeper plan step 3: set for it; he reacts and dives (or not) in
    // diveGoalkeeper. Already in a dive, he is committed to it. A penalty's
    // guess was made as the taker set off (step 9); with no run-up, now.
    const bool guessed = control.penaltyGuessed;
    control.penaltyGuessed = false;
    if (control.dive.active)
    {
      return;
    }
    control.state = GoalkeeperState::DIVE;
    control.timer = 0.0F;
    if (penalty && !guessed)
    {
      planPenaltyDiveBinturong(*keeper, getSimulatedSeconds());
    }
    control.penaltyGuessed = false;
    return;
  }
  control.state = GoalkeeperState::DIVE;
  control.timer = 0.0F;
  control.lateralVelocity = 0.0F;
  if (penalty)
  {
    // From 11 m the keeper must commit at the kick.
    const float guess = choosePenaltyGuess(*keeper);
    control.diveTargetY =
        pitch.atY(MatchTuning::Pitch::CENTRE) +
        (guess * MatchTuning::Goalkeeper::PENALTY_DIVE_METRES);
    control.reactionRemaining = 0.0F;
    return;
  }

  readShot(*keeper, control);
}

void MatchEngine::updateBall(float dt)
{
  // Integrate short flight segments so a fast ball cannot jump through a
  // keeper or a line between player ticks. Keep this resolution order: shot
  // save at the keeper's plane, goal/out-of-bounds, then other loose contacts.
  // Every resolver may change state or possession, ending the remaining flight.
  // The sub-step length is the same at every fidelity.
  const int substeps =
      MatchTuning::Timing::BALL_SUBSTEPS * static_cast<int>(stepTicks);
  const float substep = dt / static_cast<float>(substeps);
  // A sub-step is run in stretches: a contact part way through one ends it
  // there, and the ball goes on through the rest of the sub-step with what
  // the contact gave it (a few at most: the toucher is then kept off it).
  constexpr int MAX_STRETCHES = 4;
  // The ball's clock: each sub-step at its own time (the commit rule).
  struct ClockRuns
  {
    bool& running;
    ~ClockRuns() { running = false; }
  } clockRuns{ballClockRunning};
  ballClockRunning = true;
  const double stepStart = getSimulatedSeconds();
  for (int index = 0; index < substeps; ++index)
  {
    substepsLeftInStep = substeps - 1 - index;
    ballStepSeconds = dt;
    ballClockSeconds = stepStart + static_cast<double>(index) * substep;
    float remaining = substep;
    for (int stretch = 0; stretch < MAX_STRETCHES && remaining > EPSILON;
         ++stretch)
    {
      // A touched ball (owned but rolling free) keeps moving like a loose
      // one.
      if (((getPossessionAttachment() != nullptr) && !hasTouchClaim()) ||
          state != MatchState::PLAYING)
      {
        return;
      }
      substepStretchFromShare = 1.0F - remaining / substep;
      const std::optional<float> along = moveBallStretch(remaining);
      if (!along)
      {
        break;
      }
      remaining *= 1.0F - *along;
    }
  }
}

MatchEngine::BallMotion MatchEngine::ballMotion() const
{
  return {.position = ball.position,
          .z = ball.z,
          .velocity = ball.velocity,
          .velocityZ = ball.velocityZ,
          .spin = ball.spin,
          .isAerialDelivery = getPossessionState().isAerialDelivery};
}

std::optional<float> MatchEngine::moveBallStretch(float seconds)
{
  // A fallen body in the ball's way (collision plan step 6) ends the
  // stretch where it reaches him: everything else is read up to there, and
  // if nothing played it first it comes off him and goes on through the
  // rest of the stretch. The woodwork in its way does the same.
  const auto body = fallenBodyInPath(seconds);
  const std::optional<float> frame = frameInPath(seconds);
  // A training dummy too (training ground plan section 21), if it is met
  // first.
  const auto dummy = dummyInPath(seconds);
  const bool frameFirst = frame && (!body || *frame <= body->second);
  float share = frameFirst ? *frame : body ? body->second : 1.0F;
  const bool dummyFirst = dummy && ((!body && !frame) || dummy->second < share);
  if (dummyFirst)
  {
    share = dummy->second;
  }
  if (const std::optional<float> along = moveBallPart(seconds * share))
  {
    return *along * share;
  }
  if ((!body && !frame && !dummy) || state != MatchState::PLAYING ||
      ((getPossessionAttachment() != nullptr) && !hasTouchClaim()))
  {
    return std::nullopt;
  }
  if (dummyFirst)
  {
    bounceOffDummy(dummy->first);
  }
  else if (frameFirst)
  {
    bounceOffFrame();
  }
  else
  {
    bounceOffFallen(players[body->first]);
  }
  return share;
}

std::optional<float> MatchEngine::moveBallPart(float seconds)
{
  // Where the ball would be by the end of the stretch about to happen: the
  // keeper's plane, the lines and the sweep read the path to there, and a
  // contact on the way is played where the ball reaches it.
  substepBallPosition = ball.position;
  substepBallZ = ball.z;
  substepStartMotion = ballMotion();
  substepStretchSeconds = seconds;
  integrateBall(seconds);
  const BallMotion predicted = ballMotion();
  contactAlong.reset();
  // The sweep plays a contact where the ball reaches it on the path read
  // here; if the keeper or the woodwork has sent it another way, it plays
  // it where it now is (as an Aardvark body does).
  const auto sweep = [&](bool beforeLine)
  {
    substepEndMotion = ballMotion();
    const BallMotion& now = substepEndMotion;
    substepPathKnown =
        now.position.x == predicted.position.x &&
        now.position.y == predicted.position.y && now.z == predicted.z &&
        now.velocity.x == predicted.velocity.x &&
        now.velocity.y == predicted.velocity.y &&
        now.velocityZ == predicted.velocityZ && now.spin == predicted.spin;
    resolveLooseBall(beforeLine);
  };

  // A shot that has died (smothered, spent or trickling) is a loose ball;
  // against a Binturong body it stays the shot until someone controls it,
  // played like any loose ball once slow (shot block plan step 4).
  if (getPossessionState().isShot &&
      getPossessionState().shotElapsedSeconds > 0.3F &&
      getBody(!getPossessionState().shotByHome) != BodyId::BINTURONG &&
      length(ball.velocity) < MatchTuning::Ball::DEAD_SHOT_SPEED)
  {
    endSpentKeeperShot();
  }
  if (getPossessionState().isShot)
  {
    advanceShotFlightClock(seconds);
    MatchPlayer* keeper = findGoalkeeper(!getPossessionState().shotByHome);
    if ((keeper != nullptr) && getBody(keeper->isHomeTeam) == BodyId::AARDVARK)
    {
      // Credit launch-tick flight before the first body movement update.
      // Later ticks age this clock in diveGoalkeeperAardvark instead.
      auto& control = keepers[keeper->isHomeTeam ? 0 : 1];
      if (control.state == GoalkeeperState::DIVE && control.timer == 0.0F)
      {
        control.reactionRemaining =
            std::max(0.0F, control.reactionRemaining - seconds);
      }
    }
    // A Binturong keeper has no save roll: his body and hands meet it in
    // the sweep (goalkeeper plan steps 1 and 2).
    if (!getPossessionState().shotSaveResolved && (keeper != nullptr) &&
        !keeper->fallen && getBody(keeper->isHomeTeam) != BodyId::BINTURONG)
    {
      // Resolve the save when the ball reaches the keeper's plane.
      const float before = substepBallPosition.x - keeper->position.x;
      const float after = ball.position.x - keeper->position.x;
      if ((before > 0.0F) != (after > 0.0F) || std::abs(after) <= EPSILON)
      {
        resolveShotAtGoalkeeper(*keeper);
      }
      if ((getPossessionAttachment() != nullptr) ||
          state != MatchState::PLAYING)
      {
        return std::nullopt;
      }
    }
  }
  // Heading out of play: a Binturong body that meets it before the line
  // plays it there (the pitch is convex, so a contact point still on it
  // comes before the line); otherwise it is out. A walled pitch sends it
  // back off the walls instead.
  if (pitch.edge == PitchEdge::WALLS)
  {
    bounceOffWalls();
  }
  const bool headingOut = !ballInPlayAt(ball.position);
  if (headingOut)
  {
    sweep(true);
    if (contactAlong || state != MatchState::PLAYING ||
        ((getPossessionAttachment() != nullptr) && !hasTouchClaim()))
    {
      return contactAlong;
    }
  }
  checkOutOfBounds();
  if (state != MatchState::PLAYING)
  {
    return std::nullopt;
  }
  // A throw-in still on its way in: nobody plays it off the pitch.
  if (getPossessionState().throwEntering)
  {
    return contactAlong;
  }
  sweep(false);
  return contactAlong;
}

void MatchEngine::resolveShotAtGoalkeeper(MatchPlayer& keeper)
{
  noteKeeperShotResolved();
  if (!getPossessionState().shotOnTarget)
  {
    return;
  }
  const SaveAttempt attempt = attemptSave(keeper);
  if (!attempt.saved)
  {
    return;
  }
  const float stretch = attempt.stretch;
  const float speedExcess = attempt.speedExcess;
  const float crossingHeight = attempt.crossingHeight;

  if (holdsShot(keeper, stretch, speedExcess))
  {
    makeSave(keeper);
    return;
  }
  parryShot(keeper, crossingHeight > MatchTuning::Goalkeeper::TIP_OVER_METRES ||
                        stretch > MatchTuning::Goalkeeper::TIP_AROUND_STRETCH);
}

void MatchEngine::parryShot(MatchPlayer& keeper, bool overTheBar)
{
  creditKeeperSave(keeper);
  keeper.diveTimer = MatchTuning::Goalkeeper::RECOVER_TIME_SECONDS;
  keeper.isDiving = true;
  MatchEvent& event = logEvent(MatchEventType::SAVE, keeper);
  event.detail =
      overTheBar ? MatchEventDetail::TIPPED_BEHIND : MatchEventDetail::PARRIED;
  event.secondaryPlayerId = (getPossessionHistory().lastShooter != nullptr)
                                ? getPossessionHistory().lastShooter->getId()
                                : 0;
  finishKeeperParry(keeper);
  if (overTheBar)
  {
    setupCorner(!keeper.isHomeTeam,
                ball.position.y < pitch.atY(MatchTuning::Pitch::CENTRE));
    return;
  }
  // The ball spills back into the box where a rebound can be contested.
  const float outward = keeper.isHomeTeam ? 1.0F : -1.0F;
  const float speed =
      length(ball.velocity) * MatchTuning::Goalkeeper::PARRY_SPEED_SHARE;
  const Spill spill = parrySpill(keeper, speed);
  notePathChange();
  ball.velocity = spill.velocity;
  ball.velocityZ = spill.verticalSpeed;
  ball.position.x = keeper.position.x + pitch.atX(outward * 0.004F);
  noteContactActor(keeper.player, MatchTuning::Passing::KICKER_LOCKOUT_SECONDS,
                   true);
}

void MatchEngine::resolveLooseBall(bool beforeLine)
{
  // A claimed ball rolls free: its owner plays it on once he has collected
  // it (until then updateTouch collects it), and opponents can play it.
  if ((getPossessionAttachment() != nullptr) && !hasTouchClaim())
  {
    return;
  }
  const MatchPlayer* touchOwner =
      hasTouchClaim() ? findMatchPlayer(getPossessionAttachment()) : nullptr;
  using B = MatchTuning::Ball;
  // While his foot could be on it (within his reach, not behind him) it is
  // his: an opponent has to tackle him for it (chooseChallenge), not just
  // touch it away.
  const float ownerGap = (touchOwner != nullptr)
                             ? distance(touchOwner->position, ball.position)
                             : 0.0F;
  const bool ownerHasIt = (touchOwner != nullptr) && ballInReach(*touchOwner);
  if (ownerHasIt && !isTouchCollected() &&
      possessionId == PossessionId::AARDVARK)
  {
    observePossessionGate(*touchOwner,
                          PossessionContactGate::OWNER_NOT_COLLECTED);
    return;
  }

  const float endHeight = ball.z;
  // Each player's head line is his own (a Binturong body's, from his
  // height: chest and thigh control plan step 1).
  const auto [lowestHead, highestHead] = headLineRange();
  // Above every head line, only a head plays it; one that doesn't still
  // meets his body below (shot block plan §3i).
  const bool bodiesOnly =
      !getPossessionState().isShot && endHeight > highestHead;
  if (bodiesOnly)
  {
    // In the air it is claimed or contested at the line's far side, as
    // ever; a lone side's header is met where it passes his head.
    if (!beforeLine && (resolveAerialContest() || headInSweep()))
    {
      return;
    }
    if (state != MatchState::PLAYING ||
        ((getPossessionAttachment() != nullptr) && !hasTouchClaim()))
    {
      return;
    }
  }
  // Above someone's head line on this stretch: his head may meet it.
  if (!getPossessionState().isShot && !beforeLine &&
      std::max(substepBallZ, endHeight) > lowestHead && headInSweep())
  {
    return;
  }

  // Swept contact over the last sub-step: every player whose reach the ball
  // passed through is a candidate, taken in the order the ball met them.
  const Vector2F start = asVector(substepBallPosition);
  const Vector2F end = asVector(ball.position);
  const Vector2F segment{.x = end.x - start.x, .y = end.y - start.y};
  const float segmentSquared =
      (segment.x * segment.x) + (segment.y * segment.y);
  const float startHeight = substepBallZ;
  const float ballSpeed = length(ball.velocity);
  // A ball that has slowed down can be collected by anyone again, including
  // players it flew past earlier.
  if ((!getPossessionState().isShot ||
       getBody(!getPossessionState().shotByHome) == BodyId::BINTURONG) &&
      ballSpeed <= B::MAX_HEAVY_TOUCH_SPEED)
  {
    ball.touchAttempts = 0;
  }
  // Handling is allowed in the own area, except for a team-mate's pass.
  const auto handsAllowed = [this](const MatchPlayer& player)
  {
    return player.isGoalkeeper &&
           inPenaltyArea(player.position, player.isHomeTeam) &&
           !(getPossessionState().isPass &&
             getPossessionState().passByHome == player.isHomeTeam);
  };
  struct Contact
  {
    std::size_t slot = 0;
    float along = 0.0F;
    float gap = 0.0F;
    float height = 0.0F;
    float reach = 0.0F;
    ShotBodyPart part = ShotBodyPart::TORSO;
    /** A shot meeting a Binturong body: where and how (shot block plan). */
    ShotBodyHit hit{};
    /** A shot he plays like any loose ball (shot block plan step 4). */
    bool asLoose = false;
    /** Any other ball meeting his body, not played (pass block plan). */
    bool body = false;
    /** A shot meeting his body (a keeper's in his area too: goalkeeper
     * plan step 1). */
    bool shotBody = false;
  };
  std::array<Contact, 32> contacts{};
  std::size_t contactCount = 0;
  // Nobody further than the largest reach from the segment's bounding box can
  // touch the ball; the margin keeps the exact test below authoritative.
  // (The feet reach is a knob, which may be set beyond a keeper's.)
  // (A shot: a leg put out across its line, the foot and the ball beyond
  // the ankle's reach, with its miss: shot block plan §3d.)
  // (Any moving ball meets a Binturong body too: pass block plan.)
  const float shotLegBound = MatchTuning::ShotBlock::REACH_METRES +
                             (MatchTuning::ShotBlock::RUSHED_ERROR_FACTOR *
                              MatchTuning::ShotBlock::ERROR_METRES_POOR) +
                             MatchTuning::Stride::FOOT_LENGTH +
                             MatchTuning::Units::BALL_RADIUS_METRES;
  // Pass block plan: where each player's body meets a ball he doesn't play
  // (not a shot); those with a touch on it meet it only if the touch is no
  // attempt after all (bodyInstead below).
  std::array<std::optional<ShotBodyHit>, 32> bodyHits{};
  std::uint32_t touchCandidates = 0;
  std::uint32_t bodyOnlyIfDeclined = 0;
  // Within reach but in his dead zone (for the lost-wait split).
  std::uint32_t behindReach = 0;
  const MatchPlayer* lastPlayedBy =
      findMatchPlayer(getLastPossessionAttachment());
  // A ball his head can meet on this stretch is his head's (headInSweep,
  // which may let it come closer first): not his body's.
  const int substepsPerStepHere =
      MatchTuning::Timing::BALL_SUBSTEPS * static_cast<int>(stepTicks);
  const float stepShareHere = 1.0F - (static_cast<float>(substepsLeftInStep) /
                                      static_cast<float>(substepsPerStepHere));
  const auto headHasIt = [&](const MatchPlayer& player, std::size_t slot)
  {
    if (player.isGoalkeeper || player.trapTimer > 0.0F ||
        (possessionId == PossessionId::AARDVARK && hasTouchClaim() &&
         getPossessionAttachment() == player.player))
    {
      return false;
    }
    const PitchPoint was = slot < previousPlayerPositions.size()
                               ? previousPlayerPositions[slot]
                               : player.position;
    const Vector2F at = asVector(
        PitchPoint{was.x + ((player.position.x - was.x) * stepShareHere),
                   was.y + ((player.position.y - was.y) * stepShareHere)});
    const float along = segmentSquared > EPSILON
                            ? std::clamp(((at.x - start.x) * segment.x +
                                          (at.y - start.y) * segment.y) /
                                             segmentSquared,
                                         0.0F, 1.0F)
                            : 0.0F;
    const Vector2F toBall{.x = start.x + (segment.x * along) - at.x,
                          .y = start.y + (segment.y * along) - at.y};
    const float gap = length(toBall);
    return headMeetBinturong(
               player,
               gap > EPSILON
                   ? Vector2F{.x = toBall.x / gap, .y = toBall.y / gap}
                   : Vector2F{},
               gap, startHeight + ((endHeight - startHeight) * along),
               feetAtBinturong(player,
                               ballStepSeconds * (1.0F - stepShareHere)))
        .has_value();
  };
  const float maxReach =
      std::max({B::GOALKEEPER_CONTROL_RADIUS_METRES,
                touchKnobs[TouchKnob::FEET_REACH], shotLegBound});
  const float reachBound = maxReach + 0.01F;
  const float boundMinX = std::min(start.x, end.x) - reachBound;
  const float boundMaxX = std::max(start.x, end.x) + reachBound;
  const float boundMinY = std::min(start.y, end.y) - reachBound;
  const float boundMaxY = std::max(start.y, end.y) + reachBound;
  for (std::size_t slot = 0; slot < players.size() && slot < contacts.size();
       ++slot)
  {
    const MatchPlayer& player = players[slot];
    const Vector2F position = asVector(player.position);
    // (A diving keeper reaches well beyond it: goalkeeper plan step 3.)
    const bool diving = keeperDiving(player);
    if ((position.x < boundMinX || position.x > boundMaxX ||
         position.y < boundMinY || position.y > boundMaxY) &&
        !diving)
    {
      continue;
    }
    // A shot locks no Binturong player out (shot block plan step 4): once
    // it has slowed, or (a team-mate) if it is off target, he plays it like
    // any loose ball. Otherwise it meets his body (step 1; a keeper too, in
    // his area or out of it: goalkeeper plan step 1; a team-mate too): in
    // the way whatever his feet are doing, on the ground or in a jump.
    // (Slow as it comes at him: against his own motion, so he plays a ball
    // he is running with rather than bodying it on.)
    const bool binturong = getBody(player.isHomeTeam) == BodyId::BINTURONG;
    const float comingAt =
        length(Vector2F{.x = ball.velocity.x - player.velocity.x,
                        .y = ball.velocity.y - player.velocity.y});
    const bool shotAsLoose =
        getPossessionState().isShot && binturong &&
        (comingAt <= B::MAX_HEAVY_TOUCH_SPEED ||
         (player.isHomeTeam == getPossessionState().shotByHome &&
          !getPossessionState().shotOnTarget));
    // (A keeper in a dive meets even a slow one with his body only.)
    const bool shotIntoBody =
        getPossessionState().isShot && (!shotAsLoose || diving) && binturong;
    // Any other moving ball (pass block plan): it meets his torso and his
    // stride (an opponent's, also a leg put out once he has reacted)
    // whatever he is doing, unless it is his own ball, it has just left his
    // foot, it is his hands' (a keeper in his area) or it comes at him slow
    // enough to play like any loose ball (as a shot does).
    // (A keeper in a dive: his body, whatever his hands may do.)
    if (!getPossessionState().isShot && binturong && active(player) &&
        !player.fallen &&
        (!handsAllowed(player) || diving ||
         !keeperCanGatherBinturong(player)) &&
        comingAt > B::MAX_HEAVY_TOUCH_SPEED &&
        !((touchOwner != nullptr) && (ownerHasIt || &player == touchOwner)) &&
        !(ball.kickerLockout > 0.0F &&
          getLegacyContactActor() == player.player) &&
        !headHasIt(player, slot))
    {
      const float bodyAlong =
          segmentSquared > EPSILON
              ? std::clamp(((position.x - start.x) * segment.x +
                            (position.y - start.y) * segment.y) /
                               segmentSquared,
                           0.0F, 1.0F)
              : 0.0F;
      const float bodyGap =
          std::hypot(position.x - (start.x + segment.x * bodyAlong),
                     position.y - (start.y + segment.y * bodyAlong));
      const bool teammate = (lastPlayedBy != nullptr) &&
                            lastPlayedBy->isHomeTeam == player.isHomeTeam;
      // (Met on his head where his head could head it: his head's.)
      const auto headCanHeadAt = [&](const ShotBodyHit& hit)
      {
        if (hit.part != ShotBodyPart::HEAD || player.isGoalkeeper ||
            player.trapTimer > 0.0F)
        {
          return false;
        }
        const auto [fromShare, toShare] = stretchStepSharesBinturong();
        const float share = fromShare + ((toShare - fromShare) * hit.share);
        const PitchPoint was = slot < previousPlayerPositions.size()
                                   ? previousPlayerPositions[slot]
                                   : player.position;
        const Vector2F him =
            asVector(PitchPoint{was.x + ((player.position.x - was.x) * share),
                                was.y + ((player.position.y - was.y) * share)});
        const Vector2F toBall{.x = start.x + (segment.x * hit.share) - him.x,
                              .y = start.y + (segment.y * hit.share) - him.y};
        const float gap = length(toBall);
        return headMeetBinturong(
                   player,
                   gap > EPSILON
                       ? Vector2F{.x = toBall.x / gap, .y = toBall.y / gap}
                       : Vector2F{},
                   gap, startHeight + ((endHeight - startHeight) * hit.share),
                   feetAtBinturong(player, ballStepSeconds * (1.0F - share)))
            .has_value();
      };
      if (bodyGap <= shotLegBound || diving)
      {
        if (const auto hit = shotMeetsBodyBinturong(
                player, slot, {.x = start.x, .y = start.y, .z = startHeight},
                {.x = end.x, .y = end.y, .z = endHeight}, !teammate);
            hit && !headCanHeadAt(*hit) &&
            (!beforeLine ||
             ballInPlayAt(asPoint({.x = start.x + (segment.x * hit->share),
                                   .y = start.y + (segment.y * hit->share)}))))
        {
          bodyHits[slot] = *hit;
        }
      }
      // Meant for him, or a ball he is letting come closer to play: only if
      // his touch turns out no attempt.
      const bool intendedHere =
          getPossessionState().isPass &&
          getExpectedReceiver() == player.player &&
          getPossessionState().passByHome == player.isHomeTeam;
      if (intendedHere || (ball.touchWaiting & (1U << slot)) != 0U)
      {
        bodyOnlyIfDeclined |= 1U << slot;
      }
    }
    // Above every head line: his body only (his head has had its chance),
    // the receiver's too.
    if (bodiesOnly)
    {
      bodyOnlyIfDeclined &= ~(1U << slot);
      continue;
    }
    // In the air (a jump), only his head plays it (headInSweep).
    // In a dive, a keeper plays nothing: only his body meets it.
    if (!active(player) || (player.trapTimer > 0.0F && !shotIntoBody) ||
        (player.airborne() && !shotIntoBody) || player.fallen ||
        (diving && !shotIntoBody))
    {
      if (active(player))
      {
        observePossessionGate(player, PossessionContactGate::PHYSICAL_RECOVERY);
      }
      continue;
    }
    if (touchOwner != nullptr)
    {
      // He plays his own ball once collected; in his reach nobody else
      // does, but an opponent contesting it (tackling plan step 2); a
      // team-mate takes it over only once it is well away from him.
      if (&player == touchOwner)
      {
        if (!isTouchCollected())
        {
          continue;
        }
      }
      else if (!(possessionId == PossessionId::BINTURONG &&
                 player.isHomeTeam == touchOwner->isHomeTeam) &&
               ((ownerHasIt && !contestsBall(player, *touchOwner)) ||
                (player.isHomeTeam == touchOwner->isHomeTeam &&
                 ownerGap <= touchKnobs[TouchKnob::TEAMMATE_DISTANCE])))
      {
        observePossessionGate(
            player, ownerHasIt ? PossessionContactGate::OWNER_PROTECTED
                               : PossessionContactGate::FRIENDLY_CLAIM);
        continue;
      }
    }
    if (ball.kickerLockout > 0.0F && getLegacyContactActor() == player.player)
    {
      observePossessionGate(player, PossessionContactGate::KICKER_LOCKOUT);
      continue;
    }
    // (A shot meets his body even after his touch had its go: a keeper's
    // hands that left an on-target shot to his plane.)
    if ((ball.touchAttempts & (1U << slot)) != 0U && !shotIntoBody)
    {
      observePossessionGate(player, PossessionContactGate::ALREADY_ATTEMPTED);
      continue;
    }
    // His foot is committed from a contest swing.
    if (&player != touchOwner && player.footCooldown > 1e-4F && !shotIntoBody)
    {
      observePossessionGate(player, PossessionContactGate::FOOT_RECOVERING);
      continue;
    }
    // Team-mates let a pass run to its receiver, and (an Aardvark body)
    // never meet their own side's shot.
    if (possessionId == PossessionId::AARDVARK && getPossessionState().isPass &&
        player.isHomeTeam == getPossessionState().passByHome &&
        player.player != getExpectedReceiver() &&
        ballSpeed > B::MAX_HEAVY_TOUCH_SPEED)
    {
      observePossessionGate(player, PossessionContactGate::FRIENDLY_PASS);
      continue;
    }
    if (getPossessionState().isShot &&
        player.isHomeTeam == getPossessionState().shotByHome && !binturong)
    {
      continue;
    }

    const float along = segmentSquared > EPSILON
                            ? std::clamp(((position.x - start.x) * segment.x +
                                          (position.y - start.y) * segment.y) /
                                             segmentSquared,
                                         0.0F, 1.0F)
                            : 0.0F;
    // Before the line, only a Binturong body meeting it on the pitch.
    if (beforeLine)
    {
      const PitchPoint point = asPoint({.x = start.x + (segment.x * along),
                                        .y = start.y + (segment.y * along)});
      if (getBody(player.isHomeTeam) != BodyId::BINTURONG ||
          !ballInPlayAt(point))
      {
        continue;
      }
    }
    const float gapX = position.x - (start.x + segment.x * along);
    const float gapY = position.y - (start.y + segment.y * along);
    const float gapSquared = (gapX * gapX) + (gapY * gapY);
    // Cheap reject beyond the largest reach before any square root.
    if (gapSquared > maxReach * maxReach)
    {
      continue;
    }
    const float gap = std::sqrt(gapSquared);
    const float height = startHeight + ((endHeight - startHeight) * along);
    const bool hands = handsAllowed(player);
    // Above his shoulders a Binturong keeper's hands are his arms' reach
    // (headInSweep; goalkeeper plan step 7), not this reach circle.
    if (hands && binturong && !shotIntoBody &&
        height > MatchTuning::Goalkeeper::SHOULDER_HEIGHT_SHARE *
                     player.heightMetres)
    {
      continue;
    }
    // Moving faster than a walk, or not yet bent to it, his hands can't
    // gather it (goalkeeper plan step 8): his body meets it.
    if (hands && binturong && !shotIntoBody &&
        !keeperCanGatherBinturong(player))
    {
      observePossessionGate(player, PossessionContactGate::KEEPER_NOT_READY);
      continue;
    }
    if (shotIntoBody)
    {
      // His torso, or a leg where his stride has it (shot block plan §3c),
      // or a keeper's arm or hand (goalkeeper plan step 2): met where the
      // ball reaches it on this stretch, or not at all. Only the shooter's
      // opponents put a leg out to block it; a team-mate's body is in the
      // way by accident (game xG plan step 3, agreed 2026-11-20).
      const bool shootersTeamMate =
          player.isHomeTeam == getPossessionState().shotByHome;
      if (const auto hit = shotMeetsBodyBinturong(
              player, slot, {.x = start.x, .y = start.y, .z = startHeight},
              {.x = end.x, .y = end.y, .z = endHeight}, !shootersTeamMate))
      {
        contacts[contactCount++] = {
            .slot = slot,
            .along = hit->share,
            .gap = 0.0F,
            .height = startHeight + ((endHeight - startHeight) * hit->share),
            .reach = 1.0F,
            .part = hit->part,
            .hit = *hit,
            .asLoose = false,
            .body = false,
            .shotBody = true};
      }
      else if (!player.airborne() && !diving)
      {
        checkShotThroughTorsoBinturong(
            player, slot, {.x = start.x, .y = start.y, .z = startHeight},
            {.x = end.x, .y = end.y, .z = endHeight});
      }
      continue;
    }
    float reach = 0.0F;
    if (getPossessionState().isShot && !shotAsLoose)
    {
      const bool reachable =
          hands ? height <= MatchRules::goalkeeperReachMetres(
                                player.heightMetres, player.goalkeeping)
                : height <= MatchRules::headerReachMetres(player.heightMetres,
                                                          player.physicality);
      if (reachable)
      {
        reach = hands ? MatchRules::goalkeeperHandlingRadiusMetres(
                            player.heightMetres, player.goalkeeping, height,
                            player.isDiving)
                      : MatchTuning::Defending::BLOCK_DISTANCE_METRES;
      }
    }
    else if (hands)
    {
      reach = MatchRules::goalkeeperHandlingRadiusMetres(
          player.heightMetres, player.goalkeeping, height, player.isDiving);
    }
    else
    {
      const BodyLines lines = bodyLines(player);
      // Above a Binturong body's head line it is his head's (headInSweep).
      if (height > lines.head && binturong)
      {
        continue;
      }
      if (height <= lines.feet)
      {
        reach = feetReach(player);
      }
      else if (!binturong)
      {
        reach = B::CHEST_CONTROL_RADIUS_METRES;
      }
      else
      {
        // His thigh or chest: right at him, and only in front of him (chest
        // and thigh control plan step 2).
        const bool thigh = height <= lines.chest;
        // Moving, his thigh can't play it (step 4): it goes on, perhaps to
        // his feet.
        if (thigh && length(player.velocity) > B::THIGH_MAX_SPEED)
        {
          continue;
        }
        reach = thigh ? B::THIGH_REACH_METRES : B::CHEST_REACH_METRES;
        const Vector2F facing = normalized({.x = std::cos(player.facingAngle),
                                            .y = std::sin(player.facingAngle)});
        if (gapX * facing.x + gapY * facing.y > 0.0F)
        {
          continue;
        }
      }
      // His body may not reach a ball behind him at all.
      if (ballBehindReach(player, gapX, gapY, gap))
      {
        if (reach > 0.0F && gap <= reach)
        {
          behindReach |= 1U << slot;
        }
        observePossessionGate(player, PossessionContactGate::BEHIND_REACH);
        continue;
      }
    }
    if (reach <= 0.0F || gap > reach)
    {
      observePossessionGate(player, PossessionContactGate::OUT_OF_REACH);
      continue;
    }
    contacts[contactCount++] = {.slot = slot,
                                .along = along,
                                .gap = gap,
                                .height = height,
                                .reach = reach,
                                .part = ShotBodyPart::TORSO,
                                .hit = {},
                                .asLoose = shotAsLoose};
    touchCandidates |= 1U << slot;
  }
  // Bodies in the way that no touch of theirs plays it first.
  for (std::size_t slot = 0;
       slot < bodyHits.size() && contactCount < contacts.size(); ++slot)
  {
    const std::uint32_t bit = 1U << slot;
    if (!bodyHits[slot] || (touchCandidates & bit) != 0U ||
        (bodyOnlyIfDeclined & bit) != 0U)
    {
      continue;
    }
    const ShotBodyHit& hit = *bodyHits[slot];
    contacts[contactCount++] = {
        .slot = slot,
        .along = hit.share,
        .gap = 0.0F,
        .height = startHeight + ((endHeight - startHeight) * hit.share),
        .reach = 1.0F,
        .part = hit.part,
        .hit = hit,
        .asLoose = false,
        .body = true};
  }
  // A player who let the ball come closer and now finds it out of his reach
  // waited too long (counted for the sandbox checks; should never happen).
  // Read once, after the line.
  if (!beforeLine && ball.touchWaiting != 0U)
  {
    std::uint32_t inReach = 0;
    for (std::size_t index = 0; index < contactCount; ++index)
    {
      inReach |= 1U << contacts[index].slot;
    }
    const std::uint32_t lost = ball.touchWaiting & ~inReach;
    firstTouchWaitsMissed += static_cast<std::uint32_t>(std::popcount(lost));
    for (std::size_t slot = 0;
         slot < players.size() && slot < waitChosenStep.size(); ++slot)
    {
      if ((lost & (1U << slot)) != 0U)
      {
        splitLostWait(slot, (behindReach & (1U << slot)) != 0U);
      }
    }
    ball.touchWaiting &= inReach;
  }
  insertionSort(contacts, contactCount,
                [](const Contact& first, const Contact& second)
                {
                  if (first.along != second.along)
                  {
                    return first.along < second.along;
                  }
                  return first.slot < second.slot;
                });

  if (possessionId == PossessionId::BINTURONG)
  {
    PossessionCoordination::prioritise(
        contacts, contactCount,
        [](const Contact& c) { return !c.body && !c.shotBody; },
        [&](const Contact& a, const Contact& b)
        { return players[a.slot].isHomeTeam == players[b.slot].isHomeTeam; },
        [&](const Contact& c)
        {
          const MatchPlayer& p = players[c.slot];
          float space = 8.0F;
          for (const MatchPlayer& rival : players)
          {
            if (active(rival) && rival.isHomeTeam != p.isHomeTeam)
            {
              space = std::min(space, distance(p.position, rival.position));
            }
          }
          return PossessionCoordination::Candidate{
              .arrivalSeconds = c.along * substepStretchSeconds,
              .collectionQuality = PossessionCoordination::quality(
                  p.velocity, ball.velocity, p.isHomeTeam, space),
              .intendedReceiver =
                  getPossessionState().isPass &&
                  p.player == getExpectedReceiver() &&
                  p.isHomeTeam == getPossessionState().passByHome,
              .id = p.player ? p.player->getId() : 0};
        });
  }

  for (std::size_t index = 0; index < contactCount; ++index)
  {
    const Contact& contact = contacts[index];
    MatchPlayer& player = players[contact.slot];
    const bool hands = handsAllowed(player);
    // A contest (tackling plan step 2): an opponent at the owner's ball
    // inside both reaches, with his feet. Inside the owner's reach that is
    // the only way an opponent plays it.
    const bool contest = (touchOwner != nullptr) && &player != touchOwner &&
                         ownerHasIt && !hands &&
                         contestsBall(player, *touchOwner) &&
                         contact.height <= bodyLines(player).feet;
    if ((touchOwner != nullptr) && &player != touchOwner && ownerHasIt &&
        !contest && player.isHomeTeam != touchOwner->isHomeTeam)
    {
      continue;
    }
    const int substepsPerStep =
        MatchTuning::Timing::BALL_SUBSTEPS * static_cast<int>(stepTicks);
    // A Binturong body meets the ball where it reaches him: it travels
    // under its physics that share of the stretch, he reads it and plays it
    // there, and it goes on for the rest of the stretch with what his
    // contact gave it. Anyone else plays it where the stretch ends.
    const bool atContact =
        substepPathKnown && getBody(player.isHomeTeam) == BodyId::BINTURONG;
    if (atContact)
    {
      setBallMotion(substepStartMotion);
      integrateBall(substepStretchSeconds * contact.along);
      contactAlong = contact.along;
    }
    else
    {
      setBallMotion(substepEndMotion);
      contactAlong.reset();
    }
    const TouchMoment moment{
        .contactPoint =
            atContact ? ball.position
                      : asPoint({.x = start.x + (segment.x * contact.along),
                                 .y = start.y + (segment.y * contact.along)}),
        .gap = contact.gap,
        .reach = contact.reach,
        .height = atContact ? ball.z : contact.height,
        .substepsLeft = substepsLeftInStep,
        .substepSeconds = ballStepSeconds / static_cast<float>(substepsPerStep),
        .stepSeconds = ballStepSeconds};
    // Pass block plan: a ball he doesn't play meets his body where it gets
    // there, and comes off it.
    if (contact.body)
    {
      if (!atContact)
      {
        continue;
      }
      // (Counted: after his touch was no attempt, marked by a gap of -1.)
      if (contact.gap < 0.0F)
      {
        ++stats.ballBodyAfterNoTouch;
      }
      ballMeetsBodyBinturong(player, contact.slot, contact.hit);
      return;
    }
    // His touch was no attempt after all: if its path meets his body
    // further on, it meets it there.
    const auto bodyInstead = [&]
    {
      const std::size_t slot = contact.slot;
      if (!bodyHits[slot] || contactCount >= contacts.size())
      {
        return;
      }
      Contact body = contact;
      body.along = std::max(contact.along, bodyHits[slot]->share);
      body.height = startHeight + (endHeight - startHeight) * body.along;
      body.hit = *bodyHits[slot];
      body.part = body.hit.part;
      body.asLoose = false;
      body.body = true;
      body.gap = -1.0F;
      bodyHits[slot].reset();
      std::size_t at = index + 1;
      while (at < contactCount && contacts[at].along <= body.along)
      {
        ++at;
      }
      for (std::size_t move = contactCount; move > at; --move)
      {
        contacts[move] = contacts[move - 1];
      }
      contacts[at] = body;
      ++contactCount;
    };
    // Tackling plan step 5: in a contest he goes in only when his brain
    // says so; holding, he leaves it (no swing, no cooldown).
    if (contest && !goesIntoContest(player, *touchOwner, moment.contactPoint))
    {
      continue;
    }
    // His own ball: he plays it on when it is due a touch (letting it come
    // closer first if his brain reads that it will), or lets it run.
    if (&player == touchOwner)
    {
      const std::uint32_t bit = 1U << contact.slot;
      // He plays it first: an opponent contesting it, the ball inside his
      // reach too, swung at nothing (tackling plan step 2, rows 3 and 4).
      const auto contestersSwingAtNothing = [&]
      {
        if (!ownerHasIt)
        {
          return;
        }
        for (std::size_t other = 0; other < contactCount; ++other)
        {
          MatchPlayer& rival = players[contacts[other].slot];
          if (&rival != &player && contestsBall(rival, player) &&
              rival.footCooldown <= 1e-4F &&
              contacts[other].height <= bodyLines(rival).feet &&
              goesIntoContest(rival, player, ball.position))
          {
            swingAtNothing(rival, dribbleTouchIntervalBinturong(rival), false);
          }
        }
      };
      // A shot meets the ball at foot height or not at all (no volleys
      // yet): above it, it is dropped, he plays the ball as he would, and
      // decides again.
      if (pendingStrike && pendingStrike->striker == &player &&
          pendingStrike->kind == PendingStrike::Kind::SHOT &&
          moment.height > bodyLines(player).feet)
      {
        pendingStrike.reset();
        ++strikeContacts.byKind[static_cast<std::size_t>(StrikeKind::SHOT)]
              .droppedOffGround;
      }
      // A pass, shot or clearance he chose at the step's start: struck here
      // (a pass he is turning to, once he has turned or his time is up;
      // until then he keeps the ball as he would).
      if (pendingStrike && pendingStrike->striker == &player &&
          (!pendingStrike->turning || turnedToPassBinturong(player)))
      {
        ball.touchWaiting &= ~bit;
        contestersSwingAtNothing();
        playPendingStrike(moment);
        return;
      }
      if (!dribbleTouchDue(player, moment))
      {
        ball.touchWaiting &= ~bit;
        continue;
      }
      if (!dribbleTouchNow(player, moment))
      {
        ball.touchWaiting |= bit;
        noteWaitChosen(contact.slot, moment.stepSeconds);
        continue;
      }
      ball.touchWaiting &= ~bit;
      contestersSwingAtNothing();
      playDribbleTouch(player, moment);
      return;
    }
    // With the new first touch he may let the ball come closer before he
    // plays it; until he does, it is no attempt.
    if ((!getPossessionState().isShot || contact.asLoose) &&
        playsFirstTouch(player, hands, contact.height) &&
        !touchNow(player, moment))
    {
      observePossessionGate(player, PossessionContactGate::WAIT_FOR_CLOSER);
      ball.touchWaiting |= 1U << contact.slot;
      noteWaitChosen(contact.slot, moment.stepSeconds);
      continue;
    }
    // The commit rule: a feet first touch on a ball he receives, a loose
    // one or an opponent's outside his reach needs it on its path long
    // enough to have committed to; on a path that has just changed he
    // misses it cleanly. Not an interception (an opponent's pass), a
    // contest or his own dribble.
    if (!hands && !player.isGoalkeeper &&
        (!getPossessionState().isShot || contact.asLoose) && !contest &&
        &player != touchOwner &&
        getBody(player.isHomeTeam) == BodyId::BINTURONG &&
        contact.height <= bodyLines(player).feet &&
        !(getPossessionState().isPass &&
          getPossessionState().passByHome != player.isHomeTeam) &&
        ballNow() - getPossessionState().pathSince <
            MatchTuning::Dribble::FIRST_TOUCH_COMMIT_SECONDS - 1e-6)
    {
      observePossessionGate(player,
                            PossessionContactGate::CONTACT_NOT_COMMITTED);
      const std::uint32_t bit = 1U << contact.slot;
      ball.touchWaiting &= ~bit;
      if ((ball.commitMisses & bit) == 0U)
      {
        ball.commitMisses |= bit;
        ++(player.isHomeTeam ? stats.homeCommitMisses : stats.awayCommitMisses);
      }
      bodyInstead();
      continue;
    }
    ball.touchWaiting &= ~(1U << contact.slot);
    ball.touchAttempts |= 1U << contact.slot;
    const float stretch = contact.gap / std::max(contact.reach, EPSILON);

    if (getPossessionState().isShot && !contact.asLoose)
    {
      // Saves on target are resolved at the keeper's plane; here a keeper
      // gathers a wide shot near him, or an outfielder throws his body in
      // (a Binturong keeper's body is a body like any other, in his area
      // too: goalkeeper plan step 1).
      if (player.isGoalkeeper && !contact.shotBody &&
          (hands || getBody(player.isHomeTeam) != BodyId::BINTURONG))
      {
        if (!getPossessionState().shotOnTarget)
        {
          const PitchPoint wasAt = ball.position;
          setPossession(player);
          if (hands)
          {
            takeInHandsBinturong(player, wasAt, contact.height);
          }
          player.actionCooldown = keeperHoldSeconds(player);
          return;
        }
        continue;
      }
      if (!choosesToBlock(player) ||
          !blocksShot(player, contact.gap, contact.reach))
      {
        continue;
      }
      const bool offBody = getBody(player.isHomeTeam) == BodyId::BINTURONG &&
                           (!hands || contact.shotBody);
      // Off his own keeper's body in his area: a save if it no longer goes
      // in, never a block (goalkeeper plan step 1).
      const bool keeperBody = offBody && hands;
      const bool wasOnTarget = getPossessionState().shotOnTarget;
      // Off a team-mate, or off anyone once it has been blocked, it is a
      // deflection, not a block (one blocked shot per shot).
      const bool deflection =
          player.isHomeTeam == getPossessionState().shotByHome ||
          (offBody && getPossessionState().shotBlocked);
      const ShotLegs* legs = contact.slot < shotLegPoses.size()
                                 ? &shotLegPoses[contact.slot]
                                 : nullptr;
      const bool legPutOut =
          (legs != nullptr) && legs->blocking && isLegPart(contact.part);
      if (offBody && !deflection && !keeperBody)
      {
        ++stats.shotBlocksByPart[static_cast<std::size_t>(contact.part)];
        if (legPutOut)
        {
          ++stats.shotReactionBlocks;
        }
      }
      const float lockout =
          !offBody    ? MatchTuning::Passing::KICKER_LOCKOUT_SECONDS
          : legPutOut ? (legs->outShare * legs->legSeconds) +
                            MatchTuning::ShotBlock::RECOVERY_LEG_EXTRA_SECONDS
                      : MatchTuning::ShotBlock::RECOVERY_BODY_SECONDS;
      beginShotDeflection(player, lockout);
      if (offBody)
      {
        // Off his own keeper's hand (goalkeeper plan step 4): held in both
        // hands within his Handling limit, else a parry, the face turned.
        ShotBodyHit hit = contact.hit;
        if (keeperBody &&
            player.isHomeTeam != getPossessionState().shotByHome &&
            hit.part == ShotBodyPart::HAND &&
            keeperHandsHoldShotBinturong(player, hit))
        {
          ++stats.keeperBodyHitsByPart[static_cast<std::size_t>(hit.part)];
          if (wasOnTarget)
          {
            makeSave(player);
          }
          else
          {
            const PitchPoint wasAt = ball.position;
            const float wasMetres = ball.z;
            setPossession(player);
            takeInHandsBinturong(player, wasAt, wasMetres);
            player.actionCooldown = keeperHoldSeconds(player);
          }
          // No old dive freeze; in a dive he keeps it until he is up.
          const KeeperDive& dive = keepers[player.isHomeTeam ? 0 : 1].dive;
          if (dive.active)
          {
            const double up =
                dive.from +
                static_cast<double>(dive.stretchSeconds + dive.fallSeconds +
                                    dive.downSeconds);
            player.actionCooldown +=
                std::max(0.0F, static_cast<float>(up - getSimulatedSeconds()));
          }
          else
          {
            player.isDiving = false;
            player.diveTimer = 0.0F;
          }
          return;
        }
        // Off the part it met (step 3); still going in, still the shot,
        // otherwise a loose ball (step 4). No second kick after it.
        bounceShotOffBodyBinturong(player, hit);
        if (!shotStaysOnGoalBinturong())
        {
          endDeflectedShotFlight();
        }
      }
      else
      {
        endDeflectedShotFlight();
        ball.velocity.x *= MatchTuning::Defending::DEFLECTION_SPEED_FACTOR;
        ball.velocity.y +=
            randomFloat(-MatchTuning::Defending::MAX_DEFLECTION_Y_SPEED,
                        MatchTuning::Defending::MAX_DEFLECTION_Y_SPEED);
        if (choosesToPutBlockBehind(player))
        {
          clearBehind(player);
        }
      }
      if (keeperBody && player.isHomeTeam != getPossessionState().shotByHome)
      {
        ++stats.keeperBodyHitsByPart[static_cast<std::size_t>(contact.part)];
        if (getPossessionState().isShot)
        {
          ++stats.keeperBodyStillOnGoal;
        }
        if (wasOnTarget && !getPossessionState().isShot)
        {
          creditKeeperSave(player);
          MatchEvent& event = logEvent(MatchEventType::SAVE, player);
          event.detail = MatchEventDetail::PARRIED;
          event.secondaryPlayerId =
              (getPossessionHistory().lastShooter != nullptr)
                  ? getPossessionHistory().lastShooter->getId()
                  : 0;
        }
        return;
      }
      finishShotDeflection(player, offBody, deflection);
      return;
    }

    // Anyone other than the intended receiver has to stretch and react to
    // get a touch: close balls are met, fast wide ones fly past.
    const bool intended =
        getPossessionState().isPass && getExpectedReceiver() == player.player;
    if ((!intended || possessionId == PossessionId::BINTURONG) && !hands &&
        ballSpeed > B::MAX_HEAVY_TOUCH_SPEED &&
        !reachesBall(player, ballSpeed, stretch))
    {
      // Row 6: he went for it and missed; his foot is committed.
      if (contest)
      {
        player.footCooldown = dribbleTouchIntervalBinturong(player);
      }
      bodyInstead();
      continue;
    }
    if (!hands && !player.isGoalkeeper &&
        getBody(player.isHomeTeam) == BodyId::BINTURONG)
    {
      const BodyLines lines = bodyLines(player);
      const std::size_t part = contact.height <= lines.feet    ? 0
                               : contact.height <= lines.chest ? 1
                                                               : 2;
      const bool receiver =
          intended && getPossessionState().passByHome == player.isHomeTeam;
      ++(receiver ? bodyContacts.receiver : bodyContacts.other)[part];
    }
    // An opponent getting to a touched ball ends the claim, whatever his own
    // touch then does with it: a tackle if the owner had it under control,
    // on a free ball (tackling plan step 1) or contested inside his reach
    // (step 2: both feet's cooldowns); before that, the ball he was
    // receiving.
    // A free-ball tackle (not a contest) by a Binturong outfielder at feet
    // height is played with its intent (tackling plan step 3).
    const MatchPlayer* freeBallFrom = nullptr;
    // A contest that meets the carrier's foot too is a block (step 4).
    MatchPlayer* blockOwner = nullptr;
    bool blockCommitted = false;
    if (hasTouchClaim())
    {
      if (possessionId == PossessionId::BINTURONG && (touchOwner != nullptr) &&
          touchOwner->isHomeTeam == player.isHomeTeam)
      {
        if (pendingStrike && pendingStrike->striker == touchOwner)
        {
          pendingStrike.reset();
        }
        if (ball.meetFor == touchOwner->player)
        {
          ball.meetFor = nullptr;
          ball.meetRecovering = false;
          ball.knockedOn = false;
          ball.plannedTouchIn = 0.0F;
        }
      }
      if ((touchOwner != nullptr) &&
          touchOwner->isHomeTeam != player.isHomeTeam && isTouchCollected())
      {
        noteFreeBallTackle(player, contest);
        if (contest)
        {
          if (MatchPlayer* owner = findMatchPlayer(touchOwner->player))
          {
            const ContestMeet meet = commitContestTouch(player, *owner);
            if (meet != ContestMeet::UNOPPOSED)
            {
              blockOwner = owner;
              blockCommitted = meet == ContestMeet::BLOCK_COMMITTED;
            }
          }
        }
        else if (!hands && !player.isGoalkeeper &&
                 getBody(player.isHomeTeam) == BodyId::BINTURONG &&
                 contact.height <= bodyLines(player).feet)
        {
          freeBallFrom = touchOwner;
        }
      }
      clearPossession();
    }
    if (blockOwner != nullptr)
    {
      playBlock(player, *blockOwner, moment, blockCommitted);
      return;
    }
    if ((freeBallFrom != nullptr) &&
        playTackleIntent(player, *freeBallFrom, moment))
    {
      return;
    }

    // A defender meeting a cross close to his own goal line often plays safe
    // and puts it behind; an Aardvark body now and then turns it into his
    // own net (tryOwnGoalTouch).
    const float fromOwnGoal =
        pitch.fromOwnGoalLine(player.position, player.isHomeTeam);
    const bool delivery =
        getPossessionState().isPass &&
        getPossessionState().passByHome != player.isHomeTeam &&
        !player.isGoalkeeper &&
        (lastPassDecision.intent == PassIntent::CROSS ||
         lastPassDecision.intent == PassIntent::CUTBACK ||
         lastPassDecision.intent == PassIntent::SET_PIECE);
    if (delivery &&
        fromOwnGoal < MatchTuning::SetPiece::CROSS_CLEARANCE_WITHIN_METRES)
    {
      ++statsOf(player).interceptions;
      if (tryOwnGoalTouch(player))
      {
        return;
      }
      if (choosesToClearCrossBehind(player))
      {
        clearFlightState();
        clearBehind(player);
        return;
      }
      --statsOf(player).interceptions;
    }

    // The receiver of an offside pass is penalised when he plays it.
    const auto whistledOffside = [&]
    {
      if (!getPossessionState().isPass ||
          player.isHomeTeam != getPossessionState().passByHome ||
          !(possessionId == PossessionId::BINTURONG
                ? PossessionCoordination::offsideCollector(
                      getPossessionState().passWasOffside, intended,
                      getPossessionState().passOffsidePlayers,
                      player.player->getId())
                : getPossessionState().passWasOffside && intended))
      {
        return false;
      }
      if (player.isHomeTeam)
      {
        ++stats.homeOffsides;
      }
      else
      {
        ++stats.awayOffsides;
      }
      MatchEvent& event = logEvent(MatchEventType::OFFSIDE, player);
      event.position = player.position;
      setupFreeKick(!player.isHomeTeam, player.position);
      return true;
    };

    // A pass he means to play first time: struck here, instead of a touch.
    if (playsFirstTime(player, hands, moment.height))
    {
      if (whistledOffside())
      {
        return;
      }
      playFirstTimePass(player, moment);
      return;
    }

    if (playsFirstTouch(player, hands, contact.height))
    {
      // The new first touch: no success roll; the ball is played on from
      // where he met it, and what happens next follows from where it goes.
      if (whistledOffside())
      {
        return;
      }
      playFirstTouch(player, moment);
      return;
    }

    const float skill = touchSkill(player, hands);
    if (!controlsBall(player, skill, ballSpeed, contact.height))
    {
      // A heavy first touch: the ball is deadened but pops a metre or two
      // away from the player instead of rolling on at full pace.
      player.isTrapping = true;
      player.trapTimer = B::FAILED_TRAP_TIME;
      notePathChange();
      ball.velocity = heavyTouchVelocity(player, ball.velocity);
      ball.velocityZ = 0.0F;
      ball.z = 0.0F;
      ball.spin = {};
      noteFailedTouch(player);
      return;
    }

    if (whistledOffside())
    {
      return;
    }

    recordPassCompletion(player);
    const PitchPoint wasAt = ball.position;
    setPossession(player);
    settleOnBall(player, skill, hands);
    if (hands)
    {
      takeInHandsBinturong(player, wasAt, contact.height);
    }
    return;
  }
  // Nobody played it: it ran the whole stretch.
  setBallMotion(substepEndMotion);
  contactAlong.reset();
}

bool MatchEngine::resolveAerialContest()
{
  const float heightMetres = ball.z;
  // A goalkeeper in his own box claims first when he can reach the ball (a
  // Binturong keeper's hands meet it in the sweep instead: goalkeeper plan
  // step 7).
  for (auto& player : players)
  {
    if (!active(player) || !player.isGoalkeeper || player.fallen ||
        keeperDiving(player) ||
        getBody(player.isHomeTeam) == BodyId::BINTURONG ||
        (ball.kickerLockout > 0.0F &&
         getLegacyContactActor() == player.player) ||
        !inPenaltyArea(player.position, player.isHomeTeam) ||
        distance(player.position, ball.position) >
            MatchRules::goalkeeperHandlingRadiusMetres(
                player.heightMetres, player.goalkeeping, heightMetres,
                player.isDiving) ||
        heightMetres > MatchRules::goalkeeperReachMetres(player.heightMetres,
                                                         player.goalkeeping))
    {
      continue;
    }
    if (getPossessionState().isPass &&
        getPossessionState().passByHome == player.isHomeTeam)
    {
      continue;
    }
    int crowding = 0;
    for (const auto& other : players)
    {
      if (active(other) && other.isHomeTeam != player.isHomeTeam &&
          distance(other.position, ball.position) <
              MatchTuning::Aerial::DUEL_RADIUS_METRES)
      {
        ++crowding;
      }
    }
    const float holdChance = claimChance(player, crowding);
    GoalkeeperControl& control = keepers[player.isHomeTeam ? 0 : 1];
    control.state = GoalkeeperState::CLAIM;
    control.timer = 0.0F;
    ++(player.isHomeTeam ? stats.homeKeeperClaimAttempts
                         : stats.awayKeeperClaimAttempts);
    if (randomFloat(0.0F, 1.0F) < holdChance)
    {
      ++(player.isHomeTeam ? stats.homeKeeperClaims : stats.awayKeeperClaims);
      recordPassCompletion(player);
      const PitchPoint wasAt = ball.position;
      setPossession(player);
      takeInHandsBinturong(player, wasAt, heightMetres);
      player.actionCooldown =
          randomFloat(MatchTuning::Ball::MIN_KEEPER_HOLD_SECONDS,
                      MatchTuning::Ball::MAX_KEEPER_HOLD_SECONDS);
      return true;
    }
    // Punched clear under pressure.
    ++statsOf(player).clearances;
    const float outward = player.isHomeTeam ? 1.0F : -1.0F;
    beginLegacyKeeperPunch(player);
    ball.velocity = {.x = outward *
                          MatchTuning::Aerial::HEADER_CLEARANCE_SPEED *
                          randomFloat(0.5F, 0.9F),
                     .y = randomFloat(-5.0F, 5.0F)};
    ball.velocityZ = MatchTuning::Aerial::HEADER_LIFT;
    finishLegacyKeeperPunch(player);
    return true;
  }

  // Otherwise everyone who can reach the ball contests it in the air.
  std::array<MatchPlayer*, 6> candidates{};
  std::array<float, 6> strengths{};
  std::size_t count = 0;
  float totalStrength = 0.0F;
  bool homeInvolved = false;
  bool awayInvolved = false;
  for (auto& player : players)
  {
    if (!active(player) || player.isGoalkeeper || count == candidates.size() ||
        player.trapTimer > 0.0F || player.fallen ||
        (ball.kickerLockout > 0.0F &&
         getLegacyContactActor() == player.player) ||
        (hasTouchClaim() && getPossessionAttachment() == player.player))
    {
      continue;
    }
    if (distance(player.position, ball.position) >
        MatchTuning::Aerial::DUEL_RADIUS_METRES)
    {
      continue;
    }
    // Defenders are goal-side and facing the ball, so they win the first
    // contact more often than the attackers they mark.
    const bool defendingDelivery =
        getPossessionState().isPass &&
        getPossessionState().passByHome != player.isHomeTeam;
    const float strength =
        aerialStrength(player, heightMetres) *
        (defendingDelivery ? MatchTuning::Aerial::DEFENDER_DUEL_ADVANTAGE
                           : 1.0F);
    if (strength <= 0.0F)
    {
      continue;
    }
    candidates[count] = &player;
    strengths[count] = strength;
    totalStrength += strength;
    ++count;
    (player.isHomeTeam ? homeInvolved : awayInvolved) = true;
  }
  const bool contested = homeInvolved && awayInvolved;
  // On Binturong bodies he meets it with his head where it passes through
  // his head's reach (headInSweep), not here, challenged or not, a cross
  // as well as a loose ball (aerial challenge plan step 3). Mixed bodies
  // keep this contest.
  bool allBinturong = count > 0;
  for (std::size_t index = 0; index < count; ++index)
  {
    allBinturong = allBinturong &&
                   getBody(candidates[index]->isHomeTeam) == BodyId::BINTURONG;
  }
  if (count == 0 || allBinturong)
  {
    return false;
  }

  float pick = randomFloat(0.0F, totalStrength);
  std::size_t winnerIndex = count - 1;
  for (std::size_t index = 0; index < count; ++index)
  {
    pick -= strengths[index];
    if (pick <= 0.0F)
    {
      winnerIndex = index;
      break;
    }
  }
  MatchPlayer& winner = *candidates[winnerIndex];
  if (contested)
  {
    for (std::size_t index = 0; index < count; ++index)
    {
      MatchPlayer& candidate = *candidates[index];
      if (&candidate == &winner)
      {
        ++statsOf(candidate).aerialDuelsWon;
      }
      else if (candidate.isHomeTeam != winner.isHomeTeam)
      {
        ++statsOf(candidate).aerialDuelsLost;
      }
    }
    ++stats.aerialDuelsContested;
    if (winner.isHomeTeam)
    {
      ++stats.homeAerialDuelsWon;
    }
    else
    {
      ++stats.awayAerialDuelsWon;
    }

    // Pushing or holding in the air: the defender's foul in his own box is a
    // penalty; an attacker's push gives the defence a free kick.
    if (whistles(incidentRoll() < MatchTuning::Aerial::DUEL_FOUL_CHANCE))
    {
      MatchPlayer* opponent = nullptr;
      for (std::size_t index = 0; index < count; ++index)
      {
        if (candidates[index]->isHomeTeam != winner.isHomeTeam)
        {
          opponent = candidates[index];
          break;
        }
      }
      if (opponent != nullptr)
      {
        const bool attackerFouls = incidentRoll() < 0.6F;
        const bool winnerAttacking =
            getPossessionState().passByHome == winner.isHomeTeam;
        MatchPlayer& attacker = winnerAttacking ? winner : *opponent;
        MatchPlayer& defender = winnerAttacking ? *opponent : winner;
        clearFlightState();
        ball.velocity = {.x = 0.0F, .y = 0.0F};
        ball.velocityZ = 0.0F;
        if (attackerFouls)
        {
          commitFoul(attacker, defender, true, false);
        }
        else
        {
          commitFoul(defender, attacker, true, false);
        }
        return true;
      }
    }
  }
  // The duel's header is met where the ball is, his reach the duel's
  // (until the duel's own plan).
  lastHeadPart.reset();
  strikeAt = TouchMoment{.contactPoint = ball.position,
                         .gap = distance(winner.position, ball.position),
                         .reach = MatchTuning::Aerial::DUEL_RADIUS_METRES,
                         .height = heightMetres,
                         .substepsLeft = substepsLeftInStep,
                         .substepSeconds = 0.0F,
                         .stepSeconds = ballStepSeconds};
  headBall(winner);
  strikeAt.reset();
  return true;
}

bool MatchEngine::headInSweep()
{
  // The first Binturong body on the ball's path over the last stretch whose
  // head it passed within reach of (headMeetBinturong) meets it there. With
  // an opponent's head reaching it at about the same moment, the duel
  // decides which of them (aerial challenge plan step 3).
  using A = MatchTuning::Aerial;
  constexpr float HEAD_REACH = A::HEAD_REACH_METRES;
  const Vector2F start = asVector(substepBallPosition);
  const Vector2F end = asVector(ball.position);
  const Vector2F segment{.x = end.x - start.x, .y = end.y - start.y};
  const float segmentSquared =
      (segment.x * segment.x) + (segment.y * segment.y);
  const float startHeight = substepBallZ;
  const float endHeight = ball.z;
  struct Meet
  {
    std::size_t slot = 0;
    float along = 0.0F;
    float gap = 0.0F;
    HeadPart part = HeadPart::FOREHEAD;
  };
  // Players have moved to where they are at the step's end; the ball's
  // sub-steps fill the step. Each is read where he was at this sub-step's
  // end: part-way along his move, and in a jump his feet back along it.
  const int substepsPerStep =
      MatchTuning::Timing::BALL_SUBSTEPS * static_cast<int>(stepTicks);
  const float stepShare = 1.0F - (static_cast<float>(substepsLeftInStep) /
                                  static_cast<float>(substepsPerStep));
  const float secondsBefore = ballStepSeconds * (1.0F - stepShare);
  const float substepSeconds =
      ballStepSeconds / static_cast<float>(substepsPerStep);
  struct Header
  {
    std::size_t slot = 0;
    Vector2F position{};
    float feet = 0.0F;
  };
  std::array<Header, 32> headers{};
  std::size_t headerCount = 0;
  for (std::size_t slot = 0;
       slot < players.size() && headerCount < headers.size(); ++slot)
  {
    const MatchPlayer& player = players[slot];
    // Not his own touched ball (a cushion dropping to him).
    if (!active(player) || player.isGoalkeeper || player.trapTimer > 0.0F ||
        player.fallen ||
        (ball.kickerLockout > 0.0F &&
         getLegacyContactActor() == player.player) ||
        (possessionId == PossessionId::AARDVARK && hasTouchClaim() &&
         getPossessionAttachment() == player.player) ||
        getBody(player.isHomeTeam) != BodyId::BINTURONG)
    {
      continue;
    }
    const PitchPoint was = slot < previousPlayerPositions.size()
                               ? previousPlayerPositions[slot]
                               : player.position;
    headers[headerCount++] = {
        .slot = slot,
        .position = asVector(
            PitchPoint{was.x + ((player.position.x - was.x) * stepShare),
                       was.y + ((player.position.y - was.y) * stepShare)}),
        .feet = feetAtBinturong(player, secondsBefore)};
  }
  // Where on this stretch it passes closest to his head, and whether he
  // can head it there.
  const auto meetNow = [&](const Header& header) -> std::optional<Meet>
  {
    const MatchPlayer& player = players[header.slot];
    const float along =
        segmentSquared > EPSILON
            ? std::clamp(((header.position.x - start.x) * segment.x +
                          (header.position.y - start.y) * segment.y) /
                             segmentSquared,
                         0.0F, 1.0F)
            : 0.0F;
    const Vector2F toBall{
        .x = start.x + (segment.x * along) - header.position.x,
        .y = start.y + (segment.y * along) - header.position.y};
    const float gap = length(toBall);
    if (gap > HEAD_REACH)
    {
      return std::nullopt;
    }
    const float height = startHeight + ((endHeight - startHeight) * along);
    const std::optional<HeadPart> part = headMeetBinturong(
        player,
        gap > EPSILON ? Vector2F{.x = toBall.x / gap, .y = toBall.y / gap}
                      : Vector2F{},
        gap, height, header.feet);
    if (!part)
    {
      return std::nullopt;
    }
    return Meet{.slot = header.slot, .along = along, .gap = gap, .part = *part};
  };
  // Whether he can head it `seconds` after the stretch's end, it and he
  // carrying on as they are (its height from its flight, without drag).
  const auto meetsLater = [&](const Header& header, float seconds)
  {
    const MatchPlayer& player = players[header.slot];
    const Vector2F velocity{
        .x = substepEndMotion.velocity.x - player.velocity.x,
        .y = substepEndMotion.velocity.y - player.velocity.y};
    const Vector2F later{
        .x = end.x - header.position.x + (velocity.x * seconds),
        .y = end.y - header.position.y + (velocity.y * seconds)};
    const float laterGap = length(later);
    const float laterHeight =
        endHeight + (substepEndMotion.velocityZ * seconds) -
        (0.5F * MatchTuning::Ball::GRAVITY * seconds * seconds);
    return headMeetBinturong(
               player,
               laterGap > EPSILON
                   ? Vector2F{.x = later.x / laterGap, .y = later.y / laterGap}
                   : Vector2F{},
               laterGap, laterHeight,
               feetAtBinturong(player, std::max(secondsBefore - seconds, 0.0F)))
        .has_value();
  };
  // Whether an opponent's head could get to it within `seconds` after the
  // stretch's end (or on this stretch): then he can't wait for it.
  const auto opponentFirst = [&](const MatchPlayer& player, float seconds)
  {
    for (std::size_t index = 0; index < headerCount; ++index)
    {
      const Header& other = headers[index];
      if (players[other.slot].isHomeTeam != player.isHomeTeam &&
          (meetNow(other) || meetsLater(other, seconds)))
      {
        return true;
      }
    }
    return false;
  };
  std::optional<Meet> first;
  std::array<Meet, 32> playableMeets{};
  std::size_t playableCount = 0;
  for (std::size_t index = 0; index < headerCount; ++index)
  {
    const Header& header = headers[index];
    const MatchPlayer& player = players[header.slot];
    const std::optional<Meet> meet = meetNow(header);
    if (!meet || (possessionId == PossessionId::AARDVARK && first &&
                  meet->along >= first->along))
    {
      continue;
    }
    // Still closing on him at the stretch's end: he lets it come closer to
    // the middle of his head, if a sub-step on (or where it passes closest,
    // if sooner) it will still be his to head, and no opponent's head could
    // get to it by then. Otherwise he heads it now.
    if (meet->along >= 1.0F)
    {
      const Vector2F velocity{
          .x = substepEndMotion.velocity.x - player.velocity.x,
          .y = substepEndMotion.velocity.y - player.velocity.y};
      const float speedSquared =
          (velocity.x * velocity.x) + (velocity.y * velocity.y);
      const float closing = -(((end.x - header.position.x) * velocity.x) +
                              ((end.y - header.position.y) * velocity.y));
      const float wait = speedSquared > EPSILON
                             ? std::min(closing / speedSquared, substepSeconds)
                             : 0.0F;
      if (closing > 0.0F && wait > 0.0F && meetsLater(header, wait) &&
          !opponentFirst(player, wait))
      {
        continue;
      }
    }
    if (possessionId == PossessionId::BINTURONG)
    {
      playableMeets[playableCount++] = *meet;
    }
    if (!first || meet->along < first->along)
    {
      first = meet;
    }
  }

  if (possessionId == PossessionId::BINTURONG && first)
  {
    const bool home = players[first->slot].isHomeTeam;
    float opponentAlong = 2.0F;
    for (std::size_t i = 0; i < playableCount; ++i)
    {
      if (players[playableMeets[i].slot].isHomeTeam != home)
      {
        opponentAlong = std::min(opponentAlong, playableMeets[i].along);
      }
    }
    const auto candidate = [&](const Meet& meet)
    {
      const MatchPlayer& p = players[meet.slot];
      float space = 8.0F;
      for (const MatchPlayer& rival : players)
      {
        if (active(rival) && rival.isHomeTeam != home)
        {
          space = std::min(space, distance(p.position, rival.position));
        }
      }
      return PossessionCoordination::Candidate{
          .arrivalSeconds = meet.along * substepStretchSeconds,
          .collectionQuality = PossessionCoordination::quality(
              p.velocity, ball.velocity, home, space),
          .intendedReceiver = getPossessionState().isPass &&
                              getPossessionState().passByHome == home &&
                              getExpectedReceiver() == p.player,
          .id = p.player ? p.player->getId() : 0};
    };
    for (std::size_t i = 0; i < playableCount; ++i)
    {
      const Meet& meet = playableMeets[i];
      if (players[meet.slot].isHomeTeam == home &&
          meet.along <= opponentAlong &&
          PossessionCoordination::better(candidate(meet), candidate(*first)))
      {
        first = meet;
      }
    }
  }

  // A keeper's hands in his area (goalkeeper plan step 7): where the
  // stretch first comes within his arms, where he is now (part-way along his
  // move, his feet back along a jump).
  struct KeeperMeet
  {
    std::size_t slot = 0;
    Vector2F position{};
    float feet = 0.0F;
    KeeperReach reach;
  };
  std::optional<KeeperMeet> keeperMeet;
  for (std::size_t slot = 0; slot < players.size(); ++slot)
  {
    const MatchPlayer& keeper = players[slot];
    if (!keeperMayClaimBinturong(keeper))
    {
      continue;
    }
    const PitchPoint was = slot < previousPlayerPositions.size()
                               ? previousPlayerPositions[slot]
                               : keeper.position;
    const Vector2F at =
        asVector(PitchPoint{was.x + ((keeper.position.x - was.x) * stepShare),
                            was.y + ((keeper.position.y - was.y) * stepShare)});
    const float feet = feetAtBinturong(keeper, secondsBefore);
    const Point3F from{.x = start.x, .y = start.y, .z = startHeight};
    const Point3F to{.x = end.x, .y = end.y, .z = endHeight};
    std::optional<KeeperReach> reach =
        keeperReachBinturong(keeper, at, feet, from, to);
    if (!reach)
    {
      continue;
    }
    // As a header lets it come closer: he lets it come into both hands if it
    // is on its way there and no opponent's head gets to it first; else he
    // takes it where it first reaches a hand.
    constexpr float BOTH_LOOK_SECONDS = 0.3F;  // placeholder
    if (reach->bothAlong && *reach->bothAlong > reach->along)
    {
      const bool opponentSooner =
          first && players[first->slot].isHomeTeam != keeper.isHomeTeam &&
          first->along < *reach->bothAlong;
      if (!opponentSooner)
      {
        reach->along = *reach->bothAlong;
        reach->bothHands = true;
        const Point3F ballAt{.x = from.x + ((to.x - from.x) * reach->along),
                             .y = from.y + ((to.y - from.y) * reach->along),
                             .z = from.z + ((to.z - from.z) * reach->along)};
        reach->gap = std::hypot(ballAt.x - at.x, ballAt.y - at.y);
        reach->stretch = keeperStretchBinturong(
            keeper, at, {.x = ballAt.x, .y = ballAt.y, .z = ballAt.z - feet});
      }
    }
    else if (!reach->bothAlong)
    {
      const Point3F later{
          .x = end.x + (substepEndMotion.velocity.x * BOTH_LOOK_SECONDS),
          .y = end.y + (substepEndMotion.velocity.y * BOTH_LOOK_SECONDS),
          .z = endHeight + (substepEndMotion.velocityZ * BOTH_LOOK_SECONDS) -
               (0.5F * MatchTuning::Ball::GRAVITY * BOTH_LOOK_SECONDS *
                BOTH_LOOK_SECONDS)};
      if (const auto ahead = keeperReachBinturong(keeper, at, feet, to, later);
          ahead && ahead->bothAlong &&
          !opponentFirst(keeper, BOTH_LOOK_SECONDS))
      {
        continue;
      }
    }
    if (!keeperMeet || reach->along < keeperMeet->reach.along)
    {
      keeperMeet = KeeperMeet{
          .slot = slot, .position = at, .feet = feet, .reach = *reach};
    }
  }
  if (!first && !keeperMeet)
  {
    return false;
  }

  // The duel. Opponents whose heads (or a keeper's hands) it reaches within
  // the close call of the first: by strength (with a keeper in it, his
  // Handling against their Heading: user 2026-11-08), how far above the
  // ball each reaches, and holding the spot against attacking it, with some
  // chance.
  const auto keeperStrength = [&](const KeeperMeet& meet, float height)
  {
    const MatchPlayer& keeper = players[meet.slot];
    const float handling =
        std::clamp((keeper.derivedStat(DerivedStat::HANDLING) - STAT_1_RATING) /
                       (STAT_99_RATING - STAT_1_RATING),
                   0.0F, 1.0F);
    const float above = meet.reach.handTop - height;
    return (0.35F + handling) *
           (1.0F +
            std::clamp(above / A::DUEL_HEIGHT_MARGIN_METRES, 0.0F, 1.0F));
  };
  const auto headingOf = [&](const MatchPlayer& player)
  {
    return std::clamp(
        (player.derivedStat(DerivedStat::HEADING) - STAT_1_RATING) /
            (STAT_99_RATING - STAT_1_RATING),
        0.0F, 1.0F);
  };
  const auto keeperWins = [&](const KeeperMeet& meet)
  {
    MatchPlayer& keeper = players[meet.slot];
    if (substepPathKnown)
    {
      setBallMotion(substepStartMotion);
      integrateBall(substepStretchSeconds * meet.reach.along);
      contactAlong = meet.reach.along;
    }
    else
    {
      setBallMotion(substepEndMotion);
      contactAlong.reset();
    }
    keeperClaimBinturong(keeper, meet.reach);
    return true;
  };
  if (keeperMeet && (!first || keeperMeet->reach.along < first->along))
  {
    // His hands first: an opponent's head reaching it within the close call
    // contests it.
    const MatchPlayer& keeper = players[keeperMeet->slot];
    const float height =
        startHeight + ((endHeight - startHeight) * keeperMeet->reach.along);
    const float keeperSeconds = keeperMeet->reach.along * substepStretchSeconds;
    const float after =
        keeperSeconds + A::DUEL_CLOSE_CALL_SECONDS - substepStretchSeconds;
    float total = keeperStrength(*keeperMeet, height);
    const float own = total;
    std::array<std::pair<std::size_t, float>, 8> rivals{};
    std::size_t rivalCount = 0;
    for (std::size_t index = 0;
         index < headerCount && rivalCount < rivals.size(); ++index)
    {
      const Header& header = headers[index];
      const MatchPlayer& rival = players[header.slot];
      if (rival.isHomeTeam == keeper.isHomeTeam)
      {
        continue;
      }
      const std::optional<Meet> meet = meetNow(header);
      if ((meet &&
           (meet->along - keeperMeet->reach.along) * substepStretchSeconds <=
               A::DUEL_CLOSE_CALL_SECONDS) ||
          (!meet && after > 0.0F && meetsLater(header, after)))
      {
        const float above = rival.heightMetres + header.feet - height;
        const float strength =
            (0.35F + headingOf(rival)) *
            (1.0F +
             std::clamp(above / A::DUEL_HEIGHT_MARGIN_METRES, 0.0F, 1.0F)) *
            ((!rival.airborne() || !rival.runningJump) ? A::DUEL_HOLD_ADVANTAGE
                                                       : 1.0F);
        rivals[rivalCount++] = {index, strength};
        total += strength;
      }
    }
    float pick = randomFloat(0.0F, total) - own;
    if (pick <= 0.0F)
    {
      return keeperWins(*keeperMeet);
    }
    for (std::size_t index = 0; index < rivalCount; ++index)
    {
      pick -= rivals[index].second;
      if (pick <= 0.0F || index + 1 == rivalCount)
      {
        const Header& rival = headers[rivals[index].first];
        const float gap =
            length({.x = start.x + (segment.x * keeperMeet->reach.along) -
                         rival.position.x,
                    .y = start.y + (segment.y * keeperMeet->reach.along) -
                         rival.position.y});
        first = Meet{.slot = rival.slot,
                     .along = keeperMeet->reach.along,
                     .gap = std::min(gap, HEAD_REACH),
                     .part = meetNow(rival).value_or(Meet{}).part};
        break;
      }
    }
    // An opponent's head wins it: no further duel.
    keeperMeet.reset();
  }
  else if (keeperMeet && first &&
           players[keeperMeet->slot].isHomeTeam !=
               players[first->slot].isHomeTeam &&
           (keeperMeet->reach.along - first->along) * substepStretchSeconds <=
               A::DUEL_CLOSE_CALL_SECONDS)
  {
    // A head first, the opposing keeper's hands within the close call: his
    // Handling against that head's Heading.
    const MatchPlayer& header = players[first->slot];
    const float height =
        startHeight + ((endHeight - startHeight) * first->along);
    const float headerFeet = feetAtBinturong(header, secondsBefore);
    const float above = header.heightMetres + headerFeet - height;
    const float headerStrength =
        (0.35F + headingOf(header)) *
        (1.0F + std::clamp(above / A::DUEL_HEIGHT_MARGIN_METRES, 0.0F, 1.0F)) *
        ((!header.airborne() || !header.runningJump) ? A::DUEL_HOLD_ADVANTAGE
                                                     : 1.0F);
    const float keeper = keeperStrength(*keeperMeet, height);
    if (randomFloat(0.0F, headerStrength + keeper) >= headerStrength)
    {
      return keeperWins(*keeperMeet);
    }
    keeperMeet.reset();
  }
  if (!first)
  {
    return false;
  }

  const MatchPlayer& leader = players[first->slot];
  const float meetHeight =
      startHeight + ((endHeight - startHeight) * first->along);
  const float leaderSeconds = first->along * substepStretchSeconds;
  std::array<std::size_t, 8> close{};
  std::size_t closeCount = 0;
  for (std::size_t index = 0; index < headerCount && closeCount < close.size();
       ++index)
  {
    const Header& header = headers[index];
    if (players[header.slot].isHomeTeam == leader.isHomeTeam)
    {
      continue;
    }
    const std::optional<Meet> meet = meetNow(header);
    const float after =
        leaderSeconds + A::DUEL_CLOSE_CALL_SECONDS - substepStretchSeconds;
    if ((meet && (meet->along - first->along) * substepStretchSeconds <=
                     A::DUEL_CLOSE_CALL_SECONDS) ||
        (!meet && after > 0.0F && meetsLater(header, after)))
    {
      close[closeCount++] = index;
    }
  }
  if (closeCount > 0)
  {
    const auto strength = [&](const Header& header)
    {
      const MatchPlayer& player = players[header.slot];
      const float above = player.heightMetres + header.feet - meetHeight;
      const bool holding = !player.airborne() || !player.runningJump;
      return (0.35F + std::clamp(player.physicality, 0.0F, 1.0F)) *
             (1.0F +
              std::clamp(above / A::DUEL_HEIGHT_MARGIN_METRES, 0.0F, 1.0F)) *
             (holding ? A::DUEL_HOLD_ADVANTAGE : 1.0F);
    };
    const auto candidates = std::span(headers).first(headerCount);
    const auto leaderHeader =
        std::ranges::find(candidates, first->slot, &Header::slot);
    if (leaderHeader == candidates.end())
    {
      return false;
    }
    float total = strength(*leaderHeader);
    for (std::size_t index = 0; index < closeCount; ++index)
    {
      total += strength(headers[close[index]]);
    }
    float pick = randomFloat(0.0F, total) - strength(*leaderHeader);
    for (std::size_t index = 0; index < closeCount && pick > 0.0F; ++index)
    {
      const Header& rival = headers[close[index]];
      pick -= strength(rival);
      if (pick <= 0.0F)
      {
        // He wins it where they meet it.
        const float gap = length(
            {.x = start.x + (segment.x * first->along) - rival.position.x,
             .y = start.y + (segment.y * first->along) - rival.position.y});
        first = Meet{.slot = rival.slot,
                     .along = first->along,
                     .gap = std::min(gap, HEAD_REACH),
                     .part = meetNow(rival).value_or(*first).part};
      }
    }
  }

  // Met where it reaches him, as a foot meets it (resolveLooseBall): the
  // ball travels that share of the stretch, he heads it there, and it goes
  // on for the rest with what his head gave it.
  MatchPlayer& player = players[first->slot];
  if (substepPathKnown)
  {
    setBallMotion(substepStartMotion);
    integrateBall(substepStretchSeconds * first->along);
    contactAlong = first->along;
  }
  else
  {
    setBallMotion(substepEndMotion);
    contactAlong.reset();
  }
  // A duel if an opponent who could have got his head to it is close: it is
  // counted, and a push or a hold in it may be a foul.
  std::array<MatchPlayer*, 8> beaten{};
  std::size_t beatenCount = 0;
  const Vector2F meetAt = asVector(ball.position);
  for (std::size_t index = 0;
       index < headerCount && beatenCount < beaten.size(); ++index)
  {
    MatchPlayer& other = players[headers[index].slot];
    if (other.isHomeTeam != player.isHomeTeam &&
        length({.x = headers[index].position.x - meetAt.x,
                .y = headers[index].position.y - meetAt.y}) <=
            A::DUEL_RADIUS_METRES &&
        jumpingReachBinturong(other) >= meetHeight)
    {
      beaten[beatenCount++] = &other;
    }
  }
  if (beatenCount > 0)
  {
    ++statsOf(player).aerialDuelsWon;
    for (std::size_t index = 0; index < beatenCount; ++index)
    {
      ++statsOf(*beaten[index]).aerialDuelsLost;
    }
    ++stats.aerialDuelsContested;
    ++(player.isHomeTeam ? stats.homeAerialDuelsWon : stats.awayAerialDuelsWon);
    // Pushing or holding in the air: the defender's foul in his own box is a
    // penalty; an attacker's push gives the defence a free kick.
    if (whistles(incidentRoll() < A::DUEL_FOUL_CHANCE))
    {
      const bool attackerFouls = incidentRoll() < 0.6F;
      const bool winnerAttacking =
          getPossessionState().passByHome == player.isHomeTeam;
      MatchPlayer& attacker = winnerAttacking ? player : *beaten[0];
      MatchPlayer& defender = winnerAttacking ? *beaten[0] : player;
      clearFlightState();
      ball.velocity = {.x = 0.0F, .y = 0.0F};
      ball.velocityZ = 0.0F;
      contactAlong.reset();
      if (attackerFouls)
      {
        commitFoul(attacker, defender, true, false);
      }
      else
      {
        commitFoul(defender, attacker, true, false);
      }
      return true;
    }
  }
  lastHeadPart = first->part;
  strikeAt = TouchMoment{
      .contactPoint = ball.position,
      .gap = first->gap,
      .reach = HEAD_REACH,
      .height = ball.z,
      .substepsLeft = substepsLeftInStep,
      .substepSeconds = ballStepSeconds / static_cast<float>(substepsPerStep),
      .stepSeconds = ballStepSeconds};
  headBall(player);
  strikeAt.reset();
  return true;
}

void MatchEngine::clearBehind(MatchPlayer& defender, bool headed)
{
  // Knock the ball over the own goal line, wide of the posts.
  const PitchPoint target = chooseClearBehindTarget(defender);
  const BallRelease from = releaseFrom(defender, ReleaseKind::CLEAR_BEHIND);
  // Headed: as hard as his head sends it, at the same elevation.
  const auto [speed, lift] =
      headed ? headerPace(defender, HeaderPace::FULL, target,
                          MatchTuning::Aerial::HEADER_CLEARANCE_SPEED,
                          MatchTuning::Aerial::HEADER_LIFT * 0.5F)
             : std::pair{MatchTuning::Aerial::HEADER_CLEARANCE_SPEED,
                         MatchTuning::Aerial::HEADER_LIFT * 0.5F};
  const Kick kick =
      strikeAs(defender, headed ? StrikeKind::HEADER : StrikeKind::CLEARANCE,
               target, speed, lift);
  launchBall(defender, from, kick.target, kick.speed, kick.verticalSpeed,
             BallSpin{});
  clearShooterHistory();
}

void MatchEngine::clearBall(MatchPlayer& defender)
{
  if (holdStrikeForSweep({.kind = PendingStrike::Kind::CLEAR,
                          .striker = &defender,
                          .pass = {},
                          .shotAim = {},
                          .receiverSlot = {}}))
  {
    return;
  }
  trackTouch(defender);
  const ClearanceAim aim = chooseClearanceAim(defender);
  const PitchPoint target = aim.target;
  // Lofted over the distance his body reckons it travels from where it
  // releases it.
  const BallRelease from = releaseFrom(defender, ReleaseKind::CLEARANCE);
  const float clearanceMetres = clearanceDistance(defender, from.position, aim);
  const float speed = clearanceSpeed(defender);
  ++statsOf(defender).clearances;
  clearFlightState();
  const Kick kick =
      strikeAs(defender, StrikeKind::CLEARANCE, target, speed,
               loftVerticalSpeed(clearanceMetres, speed, 0.0F, 0.0F));
  launchBall(defender, from, kick.target, kick.speed, kick.verticalSpeed,
             BallSpin{});
  if (getRecorder() != nullptr)
  {
    MatchActionRecord action;
    action.kind = MatchActionKind::CLEARANCE;
    action.lofted = true;
    reportKick(action, defender, kick.target);
  }
  clearShooterHistory();
  defender.actionCooldown = recoverySeconds(defender, Recovery::PASS);
}

void MatchEngine::headBall(MatchPlayer& header)
{
  noteHeaderContact(header);
  const bool fromTeammate =
      getPossessionState().isPass &&
      getPossessionState().passByHome == header.isHomeTeam;
  const MatchPlayer* deliverer = findMatchPlayer(getLastPossessionAttachment());
  if (fromTeammate && (possessionId == PossessionId::BINTURONG
                           ? PossessionCoordination::offsideCollector(
                                 getPossessionState().passWasOffside,
                                 getExpectedReceiver() == header.player,
                                 getPossessionState().passOffsidePlayers,
                                 header.player->getId())
                           : getPossessionState().passWasOffside &&
                                 getExpectedReceiver() == header.player))
  {
    ++(header.isHomeTeam ? stats.homeOffsides : stats.awayOffsides);
    MatchEvent& event = logEvent(MatchEventType::OFFSIDE, header);
    event.position = header.position;
    setupFreeKick(!header.isHomeTeam, header.position);
    return;
  }

  // A drill's forced header (forceHeader) stands in for the brain's choice.
  std::optional<ForcedHeader> forced;
  if (forcedHeader && (header.player != nullptr) &&
      forcedHeader->player == header.player->getId())
  {
    forced = forcedHeader;
    forcedHeader.reset();
  }
  const HeaderAttempt attempt =
      !forced ? chooseHeaderAttempt(header, fromTeammate)
      : forced->aim == ForcedHeaderAim::GOAL ? HeaderAttempt::SHOOT
      : forced->aim == ForcedHeaderAim::KEEP ? HeaderAttempt::CUSHION
                                             : HeaderAttempt::PLAY;
  if (forced && forced->aim == ForcedHeaderAim::GOAL)
  {
    forcedShotAim = forced->goalAim;
  }
  if (attempt == HeaderAttempt::SHOOT)
  {
    creditHeaderShot(header, deliverer);
    meetWithHead(header);
    takeShot(header, -1.0F, true);
    return;
  }

  if (attempt == HeaderAttempt::CUSHION &&
      getBody(header.isHomeTeam) == BodyId::BINTURONG)
  {
    // Cushioned with his head as softly as it can, owned as it drops, and
    // collected once it is down (heading technique plan step 6).
    cushionHeaderBinturong(header);
    return;
  }
  if (attempt == HeaderAttempt::CUSHION)
  {
    // A team-mate's lofted ball away from goal is cushioned down and kept.
    recordPassCompletion(header);
    setPossession(header);
    settleOnBall(header, header.dribbling, false);
    return;
  }
  beginHeaderPlay(header, fromTeammate);
  meetWithHead(header);
  if (!forced && !fromTeammate && tryOwnGoalTouch(header))
  {
    return;
  }
  HeaderPlay play;
  if (forced)
  {
    play.aim = forced->spot;
  }
  else
  {
    play = chooseHeaderPlay(header, fromTeammate);
  }
  if (play.kind == HeaderPlay::Kind::BEHIND)
  {
    ++statsOf(header).clearances;
    if (play.speed > 0.0F)
    {
      // Headed out of play the way he chose.
      const BallRelease from = releaseFrom(header, ReleaseKind::HEADER);
      const auto [speed, lift] =
          headerPace(header, HeaderPace::FULL, play.aim, play.speed, play.lift);
      const Kick kick =
          strikeAs(header, StrikeKind::HEADER, play.aim, speed, lift);
      launchBall(header, from, kick.target, kick.speed, kick.verticalSpeed,
                 BallSpin{});
    }
    else
    {
      clearBehind(header, true);
    }
  }
  else if (play.kind == HeaderPlay::Kind::CLEAR)
  {
    ++statsOf(header).clearances;
    // Many defensive headers are half-clearances that drop around the edge
    // of the box, where the second ball is fought for.
    const auto [speed, lift] = headerPace(header, HeaderPace::CLEAR, play.aim,
                                          headerClearanceSpeed(header),
                                          MatchTuning::Aerial::HEADER_LIFT);
    const BallRelease from = releaseFrom(header, ReleaseKind::HEADER);
    const Kick kick =
        strikeAs(header, StrikeKind::HEADER, play.aim, speed, lift);
    launchBall(header, from, kick.target, kick.speed, kick.verticalSpeed,
               BallSpin{});
    if (getRecorder() != nullptr)
    {
      MatchActionRecord action;
      action.kind = MatchActionKind::CLEARANCE;
      action.header = true;
      reportKick(action, header, kick.target);
    }
  }
  else
  {
    const PitchPoint knockDown = knockDownPoint(header, play.aim);
    const BallRelease from = releaseFrom(header, ReleaseKind::HEADER);
    const auto [speed, lift] =
        headerPace(header, HeaderPace::TO_SPOT, knockDown,
                   MatchTuning::Aerial::HEADER_PASS_SPEED,
                   MatchTuning::Aerial::HEADER_LIFT * 0.5F);
    const Kick kick =
        strikeAs(header, StrikeKind::HEADER, knockDown, speed, lift);
    launchBall(header, from, kick.target, kick.speed, kick.verticalSpeed,
               BallSpin{});
    if (getRecorder() != nullptr)
    {
      MatchActionRecord action;
      action.kind = MatchActionKind::KNOCK_DOWN;
      action.header = true;
      action.target =
          (play.target != nullptr) && (play.target->player != nullptr)
              ? play.target->player->getId()
              : 0;
      reportKick(action, header, kick.target);
    }
  }
}

bool MatchEngine::contestsBall(const MatchPlayer& player,
                               const MatchPlayer& owner) const
{
  return player.isHomeTeam != owner.isHomeTeam && !player.isGoalkeeper &&
         getBody(player.isHomeTeam) == BodyId::BINTURONG &&
         getBody(owner.isHomeTeam) == BodyId::BINTURONG && hasTouchClaim() &&
         isTouchCollected();
}

void MatchEngine::swingAtNothing(MatchPlayer& player, float seconds,
                                 bool carrier)
{
  player.footCooldown = std::max(player.footCooldown, seconds);
  if (carrier)
  {
    ++(player.isHomeTeam ? stats.homeCarrierAirSwings
                         : stats.awayCarrierAirSwings);
  }
  else
  {
    ++(player.isHomeTeam ? stats.homeTacklerAirSwings
                         : stats.awayTacklerAirSwings);
  }
}

MatchEngine::ContestMeet MatchEngine::commitContestTouch(MatchPlayer& tackler,
                                                         MatchPlayer& owner)
{
  // The step 2 table as step 4 leaves it. The tackler swung: his cooldown.
  // The carrier's foot still recovering (row 1): his recovery carries on.
  // Free, with his planned touch within his commit moment: 0.1-0.2 s away
  // he swings at nothing (row 2), within 0.1 s both feet meet it (row 3, a
  // block). Free and not yet due (row 5): his foot reacts, a block.
  tackler.footCooldown = dribbleTouchIntervalBinturong(tackler);
  const bool recovering =
      ball.ownTouchCooldown > 1e-4F || owner.footCooldown > 1e-4F;
  if (recovering)
  {
    owner.footCooldown = std::max(owner.footCooldown, ball.ownTouchCooldown);
    return ContestMeet::UNOPPOSED;
  }
  using R = MatchTuning::Dribble;
  const bool planned = ball.meetFor == owner.player;
  if (planned && ball.meetSeconds <= R::BLOCK_SECONDS)
  {
    return ContestMeet::BLOCK_COMMITTED;
  }
  if (planned && ball.meetSeconds <= R::COMMIT_SECONDS)
  {
    swingAtNothing(
        owner,
        std::max(0.0F, ball.meetSeconds) + dribbleTouchIntervalBinturong(owner),
        true);
    return ContestMeet::UNOPPOSED;
  }
  return ContestMeet::BLOCK_REACTING;
}

float MatchEngine::ballStretch(const MatchPlayer& player,
                               PitchPoint point) const
{
  // The stretch map: where the ball is off his facing and how far out in
  // his reach.
  const Vector2F toBall = point - player.position;
  const float gap = length(toBall);
  const Vector2F facing = normalized(
      {.x = std::cos(player.facingAngle), .y = std::sin(player.facingAngle)});
  const float off = gap > EPSILON
                        ? std::acos(std::clamp(
                              (toBall.x * facing.x + toBall.y * facing.y) / gap,
                              -1.0F, 1.0F)) *
                              180.0F / std::numbers::pi_v<float>
                        : 0.0F;
  return std::clamp(
      contactStretchMap.at(off, gap / std::max(feetReach(player), EPSILON)),
      0.0F, 1.0F);
}

float MatchEngine::blockPushBinturong(const MatchPlayer& player,
                                      PitchPoint contact, bool reacting) const
{
  // By his Tackling; less at a stretch (the stretch map, where the ball is
  // off his facing and how far out in his reach), less the faster he is
  // running through it, less a foot stuck out in reaction.
  using D = MatchTuning::Defending;
  const float tackling =
      std::clamp((player.derivedStat(DerivedStat::TACKLING) - STAT_1_RATING) /
                     (STAT_99_RATING - STAT_1_RATING),
                 0.0F, 1.0F);
  float push = D::BLOCK_PUSH_POOR +
               ((D::BLOCK_PUSH_BEST - D::BLOCK_PUSH_POOR) * tackling);
  push *= 1.0F - D::BLOCK_STRETCH_LOSS * ballStretch(player, contact);
  push *= 1.0F - D::BLOCK_SPEED_LOSS *
                     std::clamp(length(player.velocity) /
                                    std::max(currentTopSpeed(player), EPSILON),
                                0.0F, 1.0F);
  if (reacting)
  {
    push *= D::BLOCK_REACTING_SHARE;
  }
  return push;
}

void MatchEngine::playBlock(MatchPlayer& tackler, MatchPlayer& owner,
                            const TouchMoment& moment, bool committed)
{
  using D = MatchTuning::Defending;
  const PitchPoint contact = moment.contactPoint;
  // The tackler's push: the way his intent (the brain's choice) sends it,
  // at the strength his technique gives, through the contact model with his
  // Tackling (its errors on line and pace). Win: towards the nudge's spot;
  // clear: towards his clearance; pass: towards his pass (none on: clear);
  // control: his first touch's way.
  PitchPoint target = contact;
  switch (tackleIntentOf(tackler, owner, contact, false))
  {
    case TackleIntent::BRAIN:
    case TackleIntent::CONTROL:
      target = chooseFirstTouch(tackler, moment).targetSpot;
      break;
    case TackleIntent::WIN:
      target = tackleNudgeSpotBinturong(tackler, owner, contact);
      break;
    case TackleIntent::PASS:
      if (const auto pass = choosePassTarget(tackler))
      {
        target = pass->targetPoint;
        break;
      }
      [[fallthrough]];
    case TackleIntent::CLEAR:
      target = chooseClearanceAim(tackler).target;
      break;
  }
  Vector2F aim = metricDirection(contact, target);
  if (length(aim) <= EPSILON)
  {
    aim = metricDirection(tackler.position, owner.position);
  }
  ContactRequest request;
  request.kind = ContactRequest::Kind::BLOCK;
  request.aim = aim;
  request.speed = blockPushBinturong(tackler, contact, false);
  const Vector2F tacklerPush =
      contactBinturong(tackler, request, moment).outcome.velocity;
  // The line between them (from the carrier to the tackler): the squeeze is
  // along it, the escape across it.
  Vector2F line = metricDirection(owner.position, tackler.position);
  if (length(line) <= EPSILON)
  {
    line = {.x = 1.0F, .y = 0.0F};
  }
  const auto alongOf = [&](Vector2F v)
  { return (v.x * line.x) + (v.y * line.y); };
  // A foot through the ball pushes it away from his own body, never back
  // towards him (no drag-backs in a block): along the line the tackler's
  // push only goes towards the carrier, the carrier's towards the tackler.
  const float tacklerAlongRaw = alongOf(tacklerPush);
  const float tacklerAlong = std::min(tacklerAlongRaw, 0.0F);
  Vector2F across{.x = tacklerPush.x - (line.x * tacklerAlongRaw),
                  .y = tacklerPush.y - (line.y * tacklerAlongRaw)};
  float ownerAlong = 0.0F;
  if (committed)
  {
    // His planned touch, the way it was to send it, through his contact.
    TouchMoment his = moment;
    his.gap = distance(owner.position, contact);
    his.reach = feetReach(owner);
    ContactRequest planned;
    planned.kind = ContactRequest::Kind::BLOCK;
    planned.aim = ball.meetOut;
    planned.speed = blockPushBinturong(owner, contact, false);
    const Vector2F ownerPush =
        contactBinturong(owner, planned, his).outcome.velocity;
    const float ownerAlongRaw = alongOf(ownerPush);
    ownerAlong = std::max(ownerAlongRaw, 0.0F);
    across.x += ownerPush.x - line.x * ownerAlongRaw;
    across.y += ownerPush.y - line.y * ownerAlongRaw;
  }
  else if (tacklerAlong < 0.0F)
  {
    // A wall: it holds the ball against a push towards him, no more.
    ownerAlong =
        std::min(blockPushBinturong(owner, contact, true), -tacklerAlong);
  }
  // Opposed along the line: the stronger wins by the difference, and pace
  // is lost; a strong, near-equal squeeze pops it up.
  float along = tacklerAlong + ownerAlong;
  float lift = 0.0F;
  if (tacklerAlong * ownerAlong < 0.0F)
  {
    const float squeezed =
        std::min(std::abs(tacklerAlong), std::abs(ownerAlong));
    const float stronger =
        std::max(std::abs(tacklerAlong), std::abs(ownerAlong));
    if (squeezed >= D::BLOCK_POP_MIN &&
        std::abs(along) <= D::BLOCK_POP_EQUAL * stronger)
    {
      lift = D::BLOCK_POP_SHARE * squeezed;
      ++stats.blocksPoppedUp;
    }
    along *= 1.0F - D::BLOCK_SQUEEZE_LOSS;
  }
  const Vector2F velocity{.x = (line.x * along) + across.x,
                          .y = (line.y * along) + across.y};
  sendBallOff(
      tackler,
      PitchPoint{
          std::clamp(contact.x, MatchTuning::Pitch::BALL_INSIDE_END_METRES,
                     pitch.length - MatchTuning::Pitch::BALL_INSIDE_END_METRES),
          std::clamp(
              contact.y, MatchTuning::Pitch::BALL_INSIDE_SIDE_METRES,
              pitch.width - MatchTuning::Pitch::BALL_INSIDE_SIDE_METRES)},
      moment.height, velocity, lift, BallSpin{}, 0.0F);
  // Loose: nobody's. Both feet are committed.
  clearPossession();
  owner.footCooldown =
      std::max(owner.footCooldown, dribbleTouchIntervalBinturong(owner));
  // At full stretch or flat out, a little longer to get his balance back
  // (tackling plan step 6): no stumble state, just more recovery.
  const auto offBalance = [&](const MatchPlayer& player)
  {
    const float speedShare = std::clamp(
        length(player.velocity) / std::max(currentTopSpeed(player), EPSILON),
        0.0F, 1.0F);
    return D::BLOCK_RECOVERY_SECONDS *
           std::max(ballStretch(player, contact), speedShare);
  };
  tackler.footCooldown += offBalance(tackler);
  owner.footCooldown += offBalance(owner);
  ++(tackler.isHomeTeam ? stats.homeBlocks : stats.awayBlocks);
  if (committed)
  {
    ++stats.blocksCommitted;
  }
}

bool MatchEngine::possessionConsistent() const
{
  return !hasTouchClaim() ||
         (getPossessionAttachment() != nullptr && state == MatchState::PLAYING);
}

TouchIntent MatchEngine::defaultTouchIntent(const MatchPlayer& player) const
{
  TouchIntent intent;
  const Vector2F ahead = {.x = (player.isHomeTeam ? 1.0F : -1.0F) *
                               touchKnobs[TouchKnob::SET_OFFSET],
                          .y = 0.0F};
  intent.targetSpot = PitchPoint{
      std::clamp(player.position.x + ahead.x,
                 MatchTuning::Pitch::BALL_INSIDE_END_METRES,
                 pitch.length - MatchTuning::Pitch::BALL_INSIDE_END_METRES),
      player.position.y};
  return intent;
}

TackleIntent MatchEngine::tackleIntentOf(const MatchPlayer& tackler,
                                         const MatchPlayer& attacker,
                                         PitchPoint contact, bool freeBall)
{
  for (const auto& [id, forced] : forcedTackleIntents)
  {
    if ((tackler.player != nullptr) && tackler.player->getId() == id)
    {
      return forced;
    }
  }
  const TackleIntent intent =
      chooseTackleIntent(tackler, attacker, contact, freeBall);
  ++(intent == TackleIntent::WIN     ? stats.intentsWin
     : intent == TackleIntent::CLEAR ? stats.intentsClear
                                     : stats.intentsControl);
  return intent;
}

bool MatchEngine::playTackleIntent(MatchPlayer& tackler,
                                   const MatchPlayer& attacker,
                                   const TouchMoment& moment)
{
  const TackleIntent intent =
      tackleIntentOf(tackler, attacker, moment.contactPoint, true);
  switch (intent)
  {
    case TackleIntent::BRAIN:
    case TackleIntent::CONTROL:
      return false;
    case TackleIntent::WIN:
    {
      // His touch, weighted to stop at the nudge's spot; how true it goes
      // is the touch's, as for any first touch.
      TouchIntent nudge = chooseFirstTouch(tackler, moment);
      nudge.kind = TouchIntent::Kind::SET;
      nudge.targetSpot =
          tackleNudgeSpotBinturong(tackler, attacker, moment.contactPoint);
      nudge.arrivalSpeed = 0.0F;
      nudge.liftSpeed = 0.0F;
      lastNudgeSpot = nudge.targetSpot;
      ++nudges;
      playFirstTouch(tackler, moment, &nudge);
      noteTackleNudge(tackler, attacker);
      return true;
    }
    case TackleIntent::CLEAR:
    case TackleIntent::PASS:
    {
      // Struck where he meets it, as a first-time pass is. No pass on: he
      // clears it.
      takePossession(tackler);
      std::optional<PassOption> pass;
      if (intent == TackleIntent::PASS)
      {
        pass = choosePassTarget(tackler);
      }
      armTackleStrike(tackler, pass);
      playPendingStrike(moment);
      return true;
    }
  }
  return false;
}

void MatchEngine::playFirstTouch(MatchPlayer& player, const TouchMoment& moment,
                                 const TouchIntent* forced)
{
  // A Binturong body's ball above his feet is his thigh's or chest's (chest
  // and thigh control plan steps 3 and 4).
  if (getBody(player.isHomeTeam) == BodyId::BINTURONG)
  {
    const BodyLines lines = bodyLines(player);
    if (moment.height > lines.feet)
    {
      playBodyTouchBinturong(player, moment, moment.height <= lines.chest);
      return;
    }
  }
  const PitchPoint contactPoint = moment.contactPoint;
  // A team-mate's pass counts as completed only once the ball is collected;
  // an opponent's pass is intercepted at the touch.
  const bool receivingPass =
      getPossessionState().isPass &&
      getPossessionState().passByHome == player.isHomeTeam;
  const Player* passer = getLastPossessionAttachment();
  if (!receivingPass)
  {
    recordPassCompletion(player);
  }
  const TouchIntent intent =
      (forced != nullptr) ? *forced : chooseFirstTouch(player, moment);
  // Read before setPossession clears the flight (whose pass it was).
  const TouchResult touch = firstTouchBinturong(player, intent, moment);

  // He owns it from the touch (phases, transitions, touches), and the ball
  // carries on from where he met it.
  takePossession(player);
  // It keeps the height he met it at: the touch changes only its velocity.
  sendBallOff(player,
              PossessionControl::retainingTouchPosition(pitch, possessionId,
                                                        contactPoint),
              moment.height, touch.velocity, touch.verticalSpeed, BallSpin{},
              0.0F);
  beginTouchClaim(receivingPass ? passer : nullptr, false);
  noteTouchOnBall(player, intent, touch, moment);
  ball.dribbleTouchLength =
      std::max(ball.touchGap, MatchTuning::Dribble::MAX_TOUCH_METRES);
}

void MatchEngine::sendBallOff(const MatchPlayer& kicker, PitchPoint position,
                              float z, Vector2F velocity, float velocityZ,
                              BallSpin spin, float kickerLockout)
{
  ball.position = position;
  ball.sentFrom = position;
  ball.sentHeightMetres = z;
  ball.z = z;
  ball.velocity = velocity;
  ball.velocityZ = velocityZ;
  ball.spin = spin;
  noteContactActor(kicker.player, kickerLockout, true);
  notePathChange();
}

std::optional<std::pair<ShotLegs, float>> MatchEngine::recentShotLegs(
    std::size_t slot, float maxAgeSeconds) const
{
  if (slot >= shotLegPoses.size() || shotLegPoses[slot].rolledAt < 0.0)
  {
    return std::nullopt;
  }
  // (Met during this step: its ball clock runs ahead of the step's.)
  const float age = std::max(
      static_cast<float>(ballNow() - shotLegPoses[slot].rolledAt), 0.0F);
  if (age > maxAgeSeconds)
  {
    return std::nullopt;
  }
  return std::pair{shotLegPoses[slot], age};
}

double MatchEngine::ballNow() const
{
  return ballClockRunning ? ballClockSeconds : getSimulatedSeconds();
}

bool MatchEngine::holdStrikeForSweep(const PendingStrike& strike)
{
  // Only while he decides on a loose ball at his feet: a dead ball, a ball
  // in a keeper's hands or a header is struck at once.
  if (!strikesWaitForSweep || (strike.striker == nullptr) ||
      state != MatchState::PLAYING || !hasTouchClaim() ||
      getConfirmedController() != strike.striker->player ||
      getPossessionAttachment() != strike.striker->player)
  {
    return false;
  }
  pendingStrike = strike;
  notePendingStrikeSlots();
  if (decidingAtMeet)
  {
    pendingStrike->atMeet = true;
    pendingStrike->meetPoint = ball.meetPoint;
  }
  else if ((strike.kind == PendingStrike::Kind::PASS ||
            strike.kind == PendingStrike::Kind::SHOT) &&
           getBody(strike.striker->isHomeTeam) == BodyId::BINTURONG)
  {
    // Turning to pass or shoot: he turns his body to it first, for as long
    // as he has before an opponent could get to the ball.
    pendingStrike->turning = true;
    pendingStrike->turnSeconds = std::min(
        opponentArrivalSecondsBinturong(*strike.striker, ball.position),
        MatchTuning::Dribble::TURN_TO_PASS_MAX_SECONDS);
  }
  return true;
}

void MatchEngine::ageHeldStrike(float dt)
{
  if (!pendingStrike)
  {
    return;
  }
  using R = MatchTuning::Dribble;
  PendingStrike& strike = *pendingStrike;
  strike.heldSeconds += dt;
  if (strike.firstTime)
  {
    // Held while the pass still comes to him (re-read each step).
    if ((strike.striker == nullptr) || state != MatchState::PLAYING ||
        (getPossessionAttachment() != nullptr) ||
        !getPossessionState().isPass ||
        getExpectedReceiver() != strike.striker->player)
    {
      pendingStrike.reset();
    }
    return;
  }
  if (strike.turning)
  {
    // Held while he turns: until he loses it, or well past his time.
    if ((strike.striker == nullptr) || state != MatchState::PLAYING ||
        !hasTouchClaim() ||
        getPossessionAttachment() != strike.striker->player ||
        strike.heldSeconds > strike.turnSeconds + R::MEET_DECISION_LATE_SECONDS)
    {
      pendingStrike.reset();
    }
    return;
  }
  const bool holds = strike.atMeet && (strike.striker != nullptr) &&
                     state == MatchState::PLAYING && hasTouchClaim() &&
                     getPossessionAttachment() == strike.striker->player &&
                     ball.meetFor == strike.striker->player &&
                     strike.heldSeconds <= R::MEET_DECISION_HORIZON_SECONDS +
                                               R::MEET_DECISION_LATE_SECONDS &&
                     distance(ball.meetPoint, strike.meetPoint) <=
                         R::MEET_DECISION_DRIFT_METRES;
  if (!holds)
  {
    pendingStrike.reset();
  }
}

void MatchEngine::decideActionAtMeet(MatchPlayer& carrier)
{
  // Read the play as it will be at the meet, then put everything back
  // before acting on it: a read only, nobody moves.
  struct Saved
  {
    PitchPoint position;
  };
  std::array<Saved, 32> saved{};
  const std::size_t count = std::min(players.size(), saved.size());
  for (std::size_t slot = 0; slot < count; ++slot)
  {
    saved[slot] = {players[slot].position};
  }
  const PitchPoint ballPosition = ball.position;
  const float ballZ = ball.z;
  const float seconds = std::max(0.0F, ball.meetSeconds);
  const float readShare = MatchTuning::Dribble::MEET_READ_TARGET_SHARE *
                          std::clamp(carrier.vision, 0.0F, 1.0F);
  for (std::size_t slot = 0; slot < count; ++slot)
  {
    MatchPlayer& player = players[slot];
    if (&player == &carrier || !active(player))
    {
      continue;
    }
    // On as he is going, and toward where he means to go (as far as he
    // could get by then), blended.
    const Vector2F rolled = {.x = player.velocity.x * seconds,
                             .y = player.velocity.y * seconds};
    const Vector2F toTarget = player.tacticalTarget - player.position;
    const float targetMetres = length(toTarget);
    const float reach =
        std::min(targetMetres, currentTopSpeed(player) * seconds);
    const Vector2F towards =
        targetMetres > EPSILON
            ? Vector2F{.x = toTarget.x / targetMetres * reach,
                       .y = toTarget.y / targetMetres * reach}
            : Vector2F{.x = 0.0F, .y = 0.0F};
    player.position =
        PitchPoint{clampToBoardsX(player.position.x + rolled.x +
                                  ((towards.x - rolled.x) * readShare)),
                   clampToBoardsY(player.position.y + rolled.y +
                                  ((towards.y - rolled.y) * readShare))};
  }
  carrier.position = ball.meetPoint;
  ball.position = ball.meetPoint;
  ball.z = 0.0F;
  const BallAction action = chooseBallAction(carrier);
  for (std::size_t slot = 0; slot < count; ++slot)
  {
    players[slot].position = saved[slot].position;
  }
  ball.position = ballPosition;
  ball.z = ballZ;
  decidingAtMeet = true;
  executeBallAction(carrier, action);
  decidingAtMeet = false;
}

std::optional<MatchEngine::FirstTimeMeet> MatchEngine::firstTimeMeet(
    const MatchPlayer& player) const
{
  // The ball on under its physics (its curl too), him on as he is going, to
  // the first moment it is within his feet's reach.
  constexpr float DT = MatchTuning::Timing::FIXED_STEP_SECONDS /
                       static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
  constexpr float HORIZON =
      MatchTuning::Passing::FIRST_TIME_DECISION_HORIZON_SECONDS;
  const float speed = length(ball.velocity);
  if (speed <= EPSILON)
  {
    return std::nullopt;
  }
  Vector2F heading{.x = ball.velocity.x / speed, .y = ball.velocity.y / speed};
  FlightState flight{.horizontalSpeed = speed,
                     .verticalSpeed = ball.velocityZ,
                     .height = ball.z,
                     .bounced = false,
                     .spin = ball.spin};
  Vector2F at = asVector(ball.position);
  const Vector2F him = asVector(player.position);
  const float reach = feetReach(player);
  for (float seconds = 0.0F; seconds <= HORIZON + 0.5F * DT; seconds += DT)
  {
    const Vector2F he{.x = him.x + (player.velocity.x * seconds),
                      .y = him.y + (player.velocity.y * seconds)};
    if (std::hypot(at.x - he.x, at.y - he.y) <= reach)
    {
      return FirstTimeMeet{.seconds = seconds,
                           .heightMetres = flight.height,
                           .point = asPoint(at)};
    }
    if (flight.horizontalSpeed <= 0.0F)
    {
      return std::nullopt;
    }
    const float moved = stepFlight(flight, DT);
    at = {.x = at.x + (heading.x * moved), .y = at.y + (heading.y * moved)};
    const float turn = flight.turnRate * DT;
    heading = {
        .x = (heading.x * std::cos(turn)) - (heading.y * std::sin(turn)),
        .y = (heading.x * std::sin(turn)) + (heading.y * std::cos(turn))};
  }
  return std::nullopt;
}

void MatchEngine::decideFirstTimePass()
{
  // Drills only until the brain judges it: a forced pass, or a forced shot,
  // marked first time.
  const bool mine = pendingStrike && pendingStrike->firstTime;
  if (pendingStrike && !mine)
  {
    return;
  }
  const auto drop = [&]
  {
    if (mine)
    {
      pendingStrike.reset();
    }
  };
  const bool shot =
      !forcedPass.firstTime && forcedShooter != 0 && forcedShotFirstTime;
  MatchPlayer* player = forcedPass.firstTime ? drillPlayer(forcedPass.passer)
                        : shot               ? drillPlayer(forcedShooter)
                                             : nullptr;
  if ((player == nullptr) || !active(*player) || state != MatchState::PLAYING ||
      getBody(player->isHomeTeam) != BodyId::BINTURONG ||
      isControlled(*player) || (getPossessionAttachment() != nullptr) ||
      !getPossessionState().isPass || getExpectedReceiver() != player->player)
  {
    drop();
    return;
  }
  // Within the horizon, and along the ground when he gets to it.
  const std::optional<FirstTimeMeet> meet = firstTimeMeet(*player);
  if (!meet || meet->heightMetres >
                   MatchTuning::Passing::FIRST_TIME_MAX_BALL_HEIGHT_METRES)
  {
    drop();
    return;
  }
  PendingStrike strike;
  if (shot)
  {
    // The spot is kept from one reading to the next (a drill's forced spot,
    // else his pick); the forced spot is used up when it is struck.
    strike = {.kind = PendingStrike::Kind::SHOT,
              .striker = player,
              .pass = {},
              .shotAim = {},
              .receiverSlot = {}};
    if (mine && pendingStrike->shotAim)
    {
      strike.shotAim = pendingStrike->shotAim;
    }
    else if (forcedShotAim)
    {
      strike.shotAim = ShotAim{
          .aimY = forcedShotAim->x, .aimZ = forcedShotAim->y, .inset = 0.0F};
    }
    else
    {
      strike.shotAim = chooseShotAim(*player, false, false, false);
    }
  }
  else
  {
    const auto chosen = forcedPassOption(*player, forcedPass);
    if (!chosen)
    {
      drop();
      return;
    }
    strike = {.kind = PendingStrike::Kind::PASS,
              .striker = player,
              .pass = chosen->first,
              .forceLofted = chosen->second,
              .shotAim = {},
              .receiverSlot = {}};
  }
  strike.firstTime = true;
  strike.heldSeconds = mine ? pendingStrike->heldSeconds : 0.0F;
  strike.meetPoint = meet->point;
  pendingStrike = strike;
  notePendingStrikeSlots();
}

bool MatchEngine::playsFirstTime(const MatchPlayer& player, bool hands,
                                 float heightMetres) const
{
  return pendingStrike && pendingStrike->firstTime &&
         pendingStrike->striker == &player && !hands &&
         getPossessionState().isPass &&
         getExpectedReceiver() == player.player &&
         heightMetres <=
             MatchTuning::Passing::FIRST_TIME_MAX_BALL_HEIGHT_METRES;
}

void MatchEngine::playFirstTimePass(MatchPlayer& player,
                                    const TouchMoment& moment)
{
  // The pass to him is completed as he plays it on. He owns the ball for
  // the strike (phases, his touch), and it is struck from where he met it,
  // moving as it was: the strike reads its pace and line.
  recordPassCompletion(player);
  // Taking it clears the flight; its spin and delivery stay for the strike.
  const BallMotion motion = ballMotion();
  takePossession(player);
  setBallMotion(motion);
  consumeFirstTimeIntent(player);
  playPendingStrike(moment);
}

void MatchEngine::playPendingStrike(const TouchMoment& moment)
{
  const PendingStrike strike = consumePendingStrike();
  MatchPlayer& striker = *strike.striker;
  // He meets it where it has got to on its path (the sweep has moved it
  // there), as a touch does: the strike is assessed at that moment and the
  // ball leaves from there.
  strikeAt = moment;
  switch (strike.kind)
  {
    case PendingStrike::Kind::PASS:
      passBall(striker, strike.pass, strike.forceLofted);
      break;
    case PendingStrike::Kind::SHOT:
      takeShot(striker, strike.forcedXG, false, false, strike.shotAim);
      break;
    case PendingStrike::Kind::CLEAR:
      clearBall(striker);
      break;
  }
  strikeAt.reset();
}

MatchEngine::DribbleAim MatchEngine::dribbleAim(
    const MatchPlayer& player, std::optional<PitchPoint> from) const
{
  DribbleAim aim;
  aim.direction = ball.dribbleDirection;
  if (isControlled(player))
  {
    // At the pace the stick asks for, along the aim if he has one (his
    // touches can go one way while he runs another), else along the stick.
    const float push = std::hypot(controlInput.moveX, controlInput.moveY);
    if (push <= EPSILON)
    {
      return aim;
    }
    const float aimed = std::hypot(controlInput.aimX, controlInput.aimY);
    aim.direction = aimed > EPSILON ? Vector2F{.x = controlInput.aimX / aimed,
                                               .y = controlInput.aimY / aimed}
                                    : Vector2F{.x = controlInput.moveX / push,
                                               .y = controlInput.moveY / push};
    aim.speed =
        currentTopSpeed(player) * std::min(push, 1.0F) *
        (controlInput.sprint ? 1.0F : MatchTuning::Control::JOG_SPEED_SHARE);
    return aim;
  }
  // Toward his plan's target (on a route, along his line through its
  // waypoints), at the pace his brain would run there. A Binturong body
  // aims from where he plays it (dribble meet plan).
  float metres = 0.0F;
  if (from && getBody(player.isHomeTeam) == BodyId::BINTURONG)
  {
    metres = length(ball.dribbleGoal - *from);
    aim.metres = metres;
    const std::optional<Vector2F> out = dribbleOutFromBinturong(player, *from);
    if (!out)
    {
      return aim;
    }
    aim.direction = *out;
  }
  else
  {
    const PitchPoint goal =
        player.routeWaypoints > 0 ? player.routeAim : ball.dribbleGoal;
    const Vector2F offset = goal - player.position;
    metres = length(offset);
    aim.metres = metres;
    if (metres <= MatchTuning::Player::ARRIVAL_DEAD_ZONE_METRES)
    {
      return aim;
    }
    aim.direction = {.x = offset.x / metres, .y = offset.y / metres};
  }
  const MovementStyle& style = movementStyle(player.isHomeTeam);
  if (style.usesModes)
  {
    // A carry's mode, at the top of its band. A Binturong brain on a
    // Binturong body picks the pace from the situation (dribble meet plan
    // 2c), Dribbling capping how much of his pace he can use.
    aim.speed = speedBand(player, style.intentModes[static_cast<std::size_t>(
                                      PlayerIntent::CARRY_BALL)])
                    .high;
    if (getBody(player.isHomeTeam) == BodyId::BINTURONG)
    {
      if (const std::optional<float> pace = chooseCarryPace(player))
      {
        aim.speed = *pace;
      }
      else
      {
        aim.speed += std::max(0.0F, currentTopSpeed(player) - aim.speed) *
                     std::clamp(player.dribbling, 0.0F, 1.0F);
      }
    }
    return aim;
  }
  const float share = std::max(
      style.intentSpeedShares[static_cast<std::size_t>(
          PlayerIntent::CARRY_BALL)],
      std::min(style.maxUrgencyShare, metres / style.urgencyDistanceMetres));
  aim.speed = std::min(currentTopSpeed(player), player.maxSpeed * share);
  return aim;
}

float MatchEngine::dribblePushMetres(const MatchPlayer& player, float pace,
                                     std::optional<float> toGoal) const
{
  using R = MatchTuning::Dribble;
  // Into his stride: where he will be a moment later at the pace he wants
  // (a better dribbler keeps it closer).
  float metres = touchKnobs[TouchKnob::SET_OFFSET];
  if (pace >= R::CLOSE_CONTROL_SPEED)
  {
    metres = pace * R::LOOSE_LEAD_SECONDS * (1.3F - player.dribbling) +
             R::LOOSE_LEAD_EXTRA_METRES;
  }
  // Never past where he is going.
  if (toGoal)
  {
    metres =
        std::min(metres, std::max(*toGoal, touchKnobs[TouchKnob::SET_OFFSET]));
  }
  return metres;
}

bool MatchEngine::dribbleTouchDue(const MatchPlayer& player,
                                  const TouchMoment& moment) const
{
  using R = MatchTuning::Dribble;
  // (A cooldown in whole steps counts down to a hair above zero.)
  // (His foot cooldown is only ever from a contest: tackling plan step 2.)
  if (ball.ownTouchCooldown > 1e-4F || player.footCooldown > 1e-4F ||
      moment.height > bodyLines(player).feet)
  {
    return false;
  }
  const DribbleAim aim = dribbleAim(player, moment.contactPoint);
  // Just out of a stop (a sharp reversal), he plays the stopped ball on once
  // he faces near enough the way he sends it: a better dribbler before he
  // has turned to it.
  if (getBody(player.isHomeTeam) == BodyId::BINTURONG &&
      player.sinceStopSeconds < R::REVERSAL_HOLD_SECONDS &&
      length(ball.velocity) <= MatchTuning::Ball::STOP_SPEED &&
      aim.speed >= R::LOOSE_START_SPEED)
  {
    const Vector2F facing = normalized(
        {.x = std::cos(player.facingAngle), .y = std::sin(player.facingAngle)});
    const float allowed =
        (R::REVERSAL_TOUCH_DEGREES_POOR +
         (R::REVERSAL_TOUCH_DEGREES_BEST - R::REVERSAL_TOUCH_DEGREES_POOR) *
             std::clamp(player.dribbling, 0.0F, 1.0F)) *
        std::numbers::pi_v<float> / 180.0F;
    if (facing.x * aim.direction.x + facing.y * aim.direction.y <
        std::cos(allowed))
    {
      return false;
    }
  }
  // Before he means to play it again (dribble meet plan 3d), only if it
  // needs it now.
  if (ball.plannedTouchIn > 1e-4F &&
      getBody(player.isHomeTeam) == BodyId::BINTURONG)
  {
    return dribbleTouchNeededEarlyBinturong(player, moment, aim.speed);
  }
  const Vector2F offset = ball.position - player.position;
  const Vector2F relative{.x = ball.velocity.x - player.velocity.x,
                          .y = ball.velocity.y - player.velocity.y};
  // He is catching it up, or it is coming back to him: play it on.
  if (offset.x * relative.x + offset.y * relative.y < 0.0F &&
      length(relative) > MatchTuning::Ball::STOP_SPEED)
  {
    return true;
  }
  // A ball by him stays put unless he wants to go somewhere.
  if (aim.speed < R::LOOSE_START_SPEED)
  {
    return false;
  }
  const float ballSpeed = length(ball.velocity);
  if (ballSpeed <= MatchTuning::Ball::STOP_SPEED)
  {
    return true;
  }
  // Heading off the way he wants to go: turn it.
  const float along =
      (ball.velocity.x * aim.direction.x + ball.velocity.y * aim.direction.y) /
      ballSpeed;
  return along < std::cos(R::LOOSE_REDIRECT_RADIANS);
}

namespace
{
/** His run along a push of `metres` at `pace`, braking as his body will for
 * a turn ahead (the cornering cap, sqrt(turn speed^2 + 2 x braking x the
 * distance still to it)): his average speed over it, and his speed at its
 * end. */
std::pair<float, float> carriedPace(float toTurn, float turnSpeed,
                                    float braking, float pace, float metres)
{
  const auto speedAt = [&](float along)
  {
    if (toTurn >= std::numeric_limits<float>::max())
    {
      return pace;
    }
    const float left = std::max(0.0F, toTurn - along);
    return std::min(
        pace, std::sqrt((turnSpeed * turnSpeed) + (2.0F * braking * left)));
  };
  if (metres <= EPSILON)
  {
    return {pace, speedAt(0.0F)};
  }
  constexpr int STEPS = 12;
  float seconds = 0.0F;
  for (int step = 0; step < STEPS; ++step)
  {
    const float along = metres * (static_cast<float>(step) + 0.5F) / STEPS;
    seconds += metres / STEPS / std::max(speedAt(along), 0.1F);
  }
  return {metres / seconds, speedAt(metres)};
}
}  // namespace

TouchIntent MatchEngine::dribbleTouchIntent(const MatchPlayer& player,
                                            const TouchMoment& moment) const
{
  using R = MatchTuning::Dribble;
  DribbleAim aim = dribbleAim(player, moment.contactPoint);
  const float running = length(player.velocity);
  const bool meetPlanned = getBody(player.isHomeTeam) == BodyId::BINTURONG;
  DribbleTurnAhead turnAhead;
  float speedingUp = 0.0F;
  const bool intentPush =
      meetPlanned && brainTraits(player.isHomeTeam).pushesByIntent;
  float touchSeconds = 0.0F;
  if (meetPlanned)
  {
    // He turns it as far as he means to: his run in was planned for it
    // (dribble meet plan), and how hard the turn is is the contact's. The
    // sharper the turn off his run, the shorter he pushes it (he has the
    // turn still to make).
    if (running >= R::CLOSE_CONTROL_SPEED)
    {
      const float along = (player.velocity.x * aim.direction.x +
                           player.velocity.y * aim.direction.y) /
                          running;
      aim.speed *= std::max(R::LOOSE_TURN_MIN_PACE, along);
    }
    // How long until he wants it again: a Binturong brain's intent (dribble
    // meet plan 3c); otherwise the lead it pushes it by.
    // The end of his run: a turn on his route, or a drill's goal (a
    // match's carry target moves on with him; it is a way, not an end).
    turnAhead = dribbleTurnAheadBinturong(player, moment.contactPoint);
    const bool drilled =
        (player.player != nullptr) &&
        std::ranges::find(ballKeepers, player.player->getId()) !=
            ballKeepers.end();
    const float toEnd =
        std::min(turnAhead.toTurn, drilled && aim.metres
                                       ? *aim.metres
                                       : std::numeric_limits<float>::max());
    const std::optional<float> planned =
        intentPush ? chooseTouchSeconds(player, aim.speed, toEnd)
                   : std::nullopt;
    touchSeconds =
        planned ? *planned
                : std::max(dribbleTouchIntervalBinturong(player),
                           R::LOOSE_LEAD_SECONDS * (1.3F - player.dribbling));
    // No faster than he can be running on average over the push: from his
    // speed its new way now, accelerating as his legs allow, until he wants
    // it again. Speeding up, it rolls at that average: running onto it, he
    // meets it (speedingUp: the share of the push he is still speeding up).
    const float startSpeed =
        std::max(0.0F, (player.velocity.x * aim.direction.x) +
                           (player.velocity.y * aim.direction.y));
    const float reachable = dribbleReachablePaceBinturong(
        player, startSpeed, aim.speed, touchSeconds, &speedingUp);
    aim.speed = std::min(aim.speed, reachable);
  }
  else if (running >= R::CLOSE_CONTROL_SPEED)
  {
    // At pace he turns the ball only as far as he can follow it: at most
    // LOOSE_MAX_TURN_RADIANS off his run per touch (later touches carry
    // the turn on), easing off the more he still has to turn.
    const Vector2F run{.x = player.velocity.x / running,
                       .y = player.velocity.y / running};
    const float wanted =
        std::atan2((run.x * aim.direction.y) - (run.y * aim.direction.x),
                   (run.x * aim.direction.x) + (run.y * aim.direction.y));
    const float turn = std::clamp(wanted, -R::LOOSE_MAX_TURN_RADIANS,
                                  R::LOOSE_MAX_TURN_RADIANS);
    aim.direction = {.x = (run.x * std::cos(turn)) - (run.y * std::sin(turn)),
                     .y = (run.x * std::sin(turn)) + (run.y * std::cos(turn))};
    aim.speed *= std::max(R::LOOSE_TURN_MIN_PACE, std::cos(wanted));
  }
  TouchIntent intent;
  // A Binturong brain pushes it as far as he runs until he wants it again,
  // held short where he wouldn't get there first (dribble meet plan 3c); a
  // long one is a knock into space, chased at his full pace.
  const std::optional<float> pushed =
      intentPush
          ? choosePushMetres(player, moment.contactPoint, aim.direction,
                             moment.gap, aim.speed, touchSeconds, aim.metres)
          : std::nullopt;
  float metres = std::min(
      pushed ? *pushed : dribblePushMetres(player, aim.speed, aim.metres),
      std::max(turnAhead.furthest, touchKnobs[TouchKnob::SET_OFFSET]));
  // On a route, aimed where his line will be when it gets there: into a
  // bend ahead, angled into it (how true it goes is the contact's).
  if (meetPlanned)
  {
    if (const std::optional<Vector2F> along =
            dribbleOutAlongBinturong(player, moment.contactPoint, metres))
    {
      aim.direction = *along;
    }
  }
  intent.knockOn = intentPush && metres >= R::PUSH_CHASE_METRES;
  intent.plannedSeconds = intentPush ? touchSeconds : 0.0F;
  // Into his stride at pace, arriving a little slower than him so he gains
  // on it; else a set touch.
  if (aim.speed >= R::CLOSE_CONTROL_SPEED)
  {
    intent.kind = TouchIntent::Kind::INTO_STRIDE;
  }
  intent.arrivalSpeed = aim.speed * R::LOOSE_ARRIVAL_SHARE;
  if (intent.kind == TouchIntent::Kind::INTO_STRIDE)
  {
    // He runs at the ball's pace, so it is weighted to roll at about the
    // pace he will run over the push, however long (a little slower, so he
    // gains on it): the arrival speed at which launch and arrival average
    // that pace, under the real rolling physics. A Binturong body's pace
    // over it allows for his braking for a turn ahead on his route, and it
    // gets there no faster than he will then be running.
    const auto [carried, atEnd] =
        meetPlanned ? carriedPace(turnAhead.toTurn, turnAhead.turnSpeed,
                                  turnAhead.braking, aim.speed, metres)
                    : std::pair{aim.speed, aim.speed};
    // The slack that lets him gain on it is for a push of some length: a
    // Binturong body's short one is weighted nearer his own pace. Speeding
    // up, he gains on it anyway: none for the share of the push he is still
    // speeding up.
    const float slack =
        meetPlanned
            ? std::clamp(metres / R::LOOSE_MEAN_PACE_FULL_METRES, 0.0F, 1.0F)
            : 1.0F;
    float mean = carried * (1.0F - (1.0F - R::LOOSE_MEAN_PACE_SHARE) * slack *
                                       (1.0F - speedingUp));
    // A push by intent rolls no faster than he will run until he means to
    // play it again (his pace then, braking for a turn ahead included), less
    // what lets him close up by then to the gap he likes to play it at: a
    // ball any quicker gets away from him.
    if (intentPush && touchSeconds > EPSILON)
    {
      const float hisPace =
          carriedPace(turnAhead.toTurn, turnAhead.turnSpeed, turnAhead.braking,
                      aim.speed, aim.speed * touchSeconds)
              .first;
      const float closing =
          std::max(0.0F, moment.gap - dribbleMeetGapBinturong(player)) /
          touchSeconds;
      mean = std::min(mean, std::max(hisPace - closing, R::LOOSE_START_SPEED));
    }
    float low = 0.0F;
    float high = mean;
    for (int iteration = 0; iteration < 16; ++iteration)
    {
      const float arrival = 0.5F * (low + high);
      if ((groundLaunchSpeed(metres, arrival) + arrival) * 0.5F > mean)
      {
        high = arrival;
      }
      else
      {
        low = arrival;
      }
    }
    intent.arrivalSpeed = std::min(low, atEnd);
  }
  // Pushed as far as a turn on his route, it gets there rolling no faster
  // than he can take the turn with it: a softer touch ahead of a cone.
  if (metres >= turnAhead.toTurn)
  {
    intent.arrivalSpeed = std::min(intent.arrivalSpeed, turnAhead.turnSpeed);
  }
  const Vector2F ahead = {.x = aim.direction.x * metres,
                          .y = aim.direction.y * metres};
  intent.targetSpot = PitchPoint{
      std::clamp(moment.contactPoint.x + ahead.x,
                 MatchTuning::Pitch::BALL_INSIDE_END_METRES,
                 pitch.length - MatchTuning::Pitch::BALL_INSIDE_END_METRES),
      std::clamp(moment.contactPoint.y + ahead.y,
                 MatchTuning::Pitch::BALL_INSIDE_SIDE_METRES,
                 pitch.width - MatchTuning::Pitch::BALL_INSIDE_SIDE_METRES)};
  // At a stop on his route (a sharp reversal), his touch stops it: all its
  // pace off, where he meets it.
  if (meetPlanned && turnAhead.stop &&
      turnAhead.toTurn <= R::STOP_TOUCH_METRES &&
      length(ball.velocity) > MatchTuning::Ball::STOP_SPEED)
  {
    intent.kind = TouchIntent::Kind::SET;
    intent.targetSpot = moment.contactPoint;
    intent.arrivalSpeed = 0.0F;
    intent.knockOn = false;
    intent.plannedSeconds = 0.0F;
  }
  // Pressed, as a first touch reads it.
  intent.pressure = contactPressure(nearestOpponentDistance(player),
                                    touchKnobs[TouchKnob::PRESSURE_RADIUS]);
  return intent;
}

void MatchEngine::playDribbleTouch(MatchPlayer& player,
                                   const TouchMoment& moment)
{
  // His own ball, played on from where he meets it: no possession change,
  // just a new roll.
  const TouchIntent intent = dribbleTouchIntent(player, moment);
  const TouchResult touch = dribbleTouchBinturong(player, intent, moment);
  ++dribbleTouches;
  dribbleContactGapSum += moment.gap;
  trackTouch(player);
  // It keeps the height he met it at: the touch changes only its velocity.
  sendBallOff(player,
              PossessionControl::retainingTouchPosition(pitch, possessionId,
                                                        moment.contactPoint),
              moment.height, touch.velocity, touch.verticalSpeed, BallSpin{},
              0.0F);
  noteDribbleTouch(player, intent, touch, moment);
}

bool MatchEngine::waitPlayableNextSubstep(const MatchPlayer& player,
                                          float dt) const
{
  // The ball's first sub-step of this step, read with its own physics
  // (spin turns it too little in one sub-step to matter).
  const int substeps =
      MatchTuning::Timing::BALL_SUBSTEPS * static_cast<int>(stepTicks);
  const float substep = dt / static_cast<float>(std::max(substeps, 1));
  const float speed = length(ball.velocity);
  const Vector2F heading =
      speed > EPSILON
          ? Vector2F{.x = ball.velocity.x / speed, .y = ball.velocity.y / speed}
          : Vector2F{.x = 0.0F, .y = 0.0F};
  FlightState flight{.horizontalSpeed = speed,
                     .verticalSpeed = ball.velocityZ,
                     .height = ball.z,
                     .bounced = false,
                     .spin = ball.spin};
  const float startHeight = flight.height;
  const float rolled = stepFlight(flight, substep);
  // Off his feet the whole way: not a wait this is about.
  const float feet = bodyLines(player).feet;
  if (startHeight > feet && flight.height > feet)
  {
    return true;
  }
  const Vector2F start = asVector(ball.position);
  const Vector2F segment{.x = heading.x * rolled, .y = heading.y * rolled};
  const Vector2F him = asVector(player.position);
  const float segmentSquared =
      (segment.x * segment.x) + (segment.y * segment.y);
  const float along = segmentSquared > EPSILON
                          ? std::clamp(((him.x - start.x) * segment.x +
                                        (him.y - start.y) * segment.y) /
                                           segmentSquared,
                                       0.0F, 1.0F)
                          : 0.0F;
  const float gapX = him.x - (start.x + segment.x * along);
  const float gapY = him.y - (start.y + segment.y * along);
  const float gap = std::sqrt((gapX * gapX) + (gapY * gapY));
  if (gap > feetReach(player))
  {
    return false;
  }
  return !ballBehindReach(player, gapX, gapY, gap);
}

void MatchEngine::noteWaitChosen(std::size_t slot, float stepSeconds)
{
  if (slot >= waitChosenStep.size())
  {
    return;
  }
  const MatchPlayer& player = players[slot];
  waitChosenStep[slot] = stepCounter;
  if (getBody(player.isHomeTeam) == BodyId::BINTURONG)
  {
    waitPredictedFacing[slot] = predictedFacingBinturong(player, stepSeconds);
  }
}

void MatchEngine::splitLostWait(std::size_t slot, bool inDeadZone)
{
  FirstTouchWaitLosses& split = firstTouchWaitLosses;
  const MatchPlayer& player = players[slot];
  ++(inDeadZone ? split.deadZone : split.outOfReach);
  const bool changed = (waitIntentChanged & (1U << slot)) != 0U;
  if (changed)
  {
    ++split.intentChanged;
  }
  else
  {
    ++split.unchanged;
  }
  for (const CollisionHit& hit : lastCollisions)
  {
    if (hit.sides[0].player == player.player->getId() ||
        hit.sides[1].player == player.player->getId())
    {
      ++split.bumped;
      if (!changed)
      {
        ++split.unchangedBumped;
      }
      break;
    }
  }
  if (waitChosenStep[slot] == stepCounter)
  {
    ++split.sameStep;
    return;
  }
  if (getBody(player.isHomeTeam) != BodyId::BINTURONG)
  {
    return;
  }
  // Wrapped to (-pi, pi].
  const auto onPitch = [this](float angle)
  { return std::atan2(std::sin(angle), std::cos(angle)); };
  float error =
      onPitch(player.facingAngle) - onPitch(waitPredictedFacing[slot]);
  if (error > std::numbers::pi_v<float>)
  {
    error -= TWO_PI;
  }
  else if (error < -std::numbers::pi_v<float>)
  {
    error += TWO_PI;
  }
  split.facingErrorSum += std::abs(error);
  split.facingErrorMax = std::max(split.facingErrorMax, std::abs(error));
  ++split.facingSamples;
  if (!changed)
  {
    split.unchangedFacingErrorSum += std::abs(error);
    split.unchangedFacingErrorMax =
        std::max(split.unchangedFacingErrorMax, std::abs(error));
    ++split.unchangedFacingSamples;
  }
}

void MatchEngine::chaseTouchedBall()
{
  // He runs to the earliest point of the ball's roll he can reach, read
  // with the same rolling physics the ball obeys. A human steers himself.
  const bool hadPlan =
      (ball.meetFor != nullptr) && ball.meetFor == getPossessionAttachment();
  ball.meetFor = nullptr;
  MatchPlayer* owner = findMatchPlayer(getPossessionAttachment());
  if ((owner == nullptr) || !active(*owner) || isControlled(*owner))
  {
    return;
  }
  // A collected ball on a Binturong body: he plans where he meets it and
  // what he does there, and runs to it that way (dribble meet plan). His
  // run to it starts where his last touch left him, or here.
  if (isTouchCollected() && getBody(owner->isHomeTeam) == BodyId::BINTURONG)
  {
    if (!hadPlan)
    {
      ball.meetLegStart = owner->position;
      ball.meetRecovering = false;
      ball.knockedOn = false;
      ball.plannedTouchIn = 0.0F;
    }
    planDribbleMeetBinturong(*owner);
    return;
  }
  using F = MatchTuning::FirstTouch;
  constexpr int SUBSTEPS = MatchTuning::Timing::BALL_SUBSTEPS;
  const float topSpeed = std::max(currentTopSpeed(*owner), EPSILON);
  const float speed = length(ball.velocity);
  const Vector2F heading =
      speed > EPSILON
          ? Vector2F{.x = ball.velocity.x / speed, .y = ball.velocity.y / speed}
          : Vector2F{.x = 0.0F, .y = 0.0F};
  FlightState flight{.horizontalSpeed = speed,
                     .verticalSpeed = 0.0F,
                     .height = 0.0F,
                     .bounced = false};
  float travelled = 0.0F;
  PitchPoint meet = ball.position;
  for (float elapsed = F::CHASE_STEP_SECONDS;
       elapsed <= F::CHASE_HORIZON_SECONDS + EPSILON;
       elapsed += F::CHASE_STEP_SECONDS)
  {
    for (int step = 0; step < SUBSTEPS && flight.horizontalSpeed > 0.0F; ++step)
    {
      travelled += stepFlight(
          flight, F::CHASE_STEP_SECONDS / static_cast<float>(SUBSTEPS));
    }
    const Vector2F rolled = {.x = heading.x * travelled,
                             .y = heading.y * travelled};
    meet = PitchPoint{ball.position.x + rolled.x, ball.position.y + rolled.y};
    if (distance(owner->position, meet) -
            MatchTuning::Dribble::KICK_REACH_METRES <=
        topSpeed * elapsed)
    {
      break;
    }
  }
  // After a win-it nudge he steps across: he meets it from the attacker's
  // side, his body between them (tackling plan step 6).
  if (getPossessionState().nudgedBy == owner->player)
  {
    if (const MatchPlayer* attacker =
            findMatchPlayer(getPossessionState().nudgedFrom))
    {
      const Vector2F across = metricDirection(meet, attacker->position);
      const Vector2F shift = {
          .x = across.x * MatchTuning::Defending::STEP_ACROSS_METRES,
          .y = across.y * MatchTuning::Defending::STEP_ACROSS_METRES};
      meet = PitchPoint{
          std::clamp(meet.x + shift.x,
                     MatchTuning::Pitch::BALL_INSIDE_END_METRES,
                     pitch.length - MatchTuning::Pitch::BALL_INSIDE_END_METRES),
          std::clamp(
              meet.y + shift.y, MatchTuning::Pitch::BALL_INSIDE_SIDE_METRES,
              pitch.width - MatchTuning::Pitch::BALL_INSIDE_SIDE_METRES)};
    }
  }
  // His body chases the ball; a route he has stays, for his touches (and
  // is moved on as he passes its waypoints).
  owner->tacticalTarget = meet;
  // Running with it, not pulling up at the point: a moving target, so his
  // run matches the ball's (a still ball: he stops on it).
  owner->targetVelocity = ball.velocity;
  owner->intent = PlayerIntent::RECEIVE_PASS;
  owner->urgentMovement = true;
}

void MatchEngine::makeSave(MatchPlayer& goalkeeper)
{
  creditKeeperSave(goalkeeper);
  const PlayerID shooterId = (getPossessionHistory().lastShooter != nullptr)
                                 ? getPossessionHistory().lastShooter->getId()
                                 : 0;
  goalkeeper.isDiving = true;
  goalkeeper.diveTimer = MatchTuning::Ball::SAVE_DIVE_TIME;
  const PitchPoint wasAt = ball.position;
  const float wasMetres = ball.z;
  setPossession(goalkeeper);
  takeInHandsBinturong(goalkeeper, wasAt, wasMetres);
  goalkeeper.actionCooldown = keeperHoldSeconds(goalkeeper);
  completeKeeperSave(goalkeeper);
  MatchEvent& event = logEvent(MatchEventType::SAVE, goalkeeper);
  event.secondaryPlayerId = shooterId;
}

void MatchEngine::extendRestart(float seconds)
{
  if (state != MatchState::PLAYING && state != MatchState::GOAL &&
      state != MatchState::HALF_TIME && state != MatchState::FULL_TIME)
  {
    setPieceTimer += seconds;
  }
}

void MatchEngine::beginStoppage()
{
  // Play has stopped: a pending advantage lapses. Several stoppage causes in
  // the same step (a red card and its free kick) form one stoppage, so they
  // share one substitution window.
  pendingAdvantage.active = false;
  setPiecePhaseRemaining = 0.0F;
  // A free-ball tackle not yet followed by control is not won.
  cancelPendingTackle();
  if (stoppageSequence != 0 && lastStoppageStep == stepCounter)
  {
    return;
  }
  lastStoppageStep = stepCounter;
  ++stoppageSequence;
}

void MatchEngine::setupKickOff(bool homeKickingOff)
{
  // The goal already opened this break in play. Changes during its
  // celebration and the following kick-off share one substitution window.
  if (state != MatchState::GOAL)
  {
    beginStoppage();
  }
  restartIsSetPiece = false;
  pendingAdvantage.active = false;
  resetPositions();
  // At kick-off every player must be in their own half. The normal tactical
  // positions deliberately span more of the pitch and are restored through
  // regular movement once play starts.
  for (auto& player : players)
  {
    if (!active(player))
    {
      continue;
    }
    player.position = kickOffPosition(player);
    player.movementTarget = player.position;
  }
  resetLifecycleBall();
  deadBall = {};
  clearShooterHistory();
  const PitchPoint centre =
      pitch.at(MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE);
  MatchPlayer* taker =
      chooseRestartTaker(MatchState::KICK_OFF, homeKickingOff, centre);
  setRestartTaker(taker);
  if (taker != nullptr)
  {
    taker->position =
        PitchPoint{pitch.atX(MatchTuning::Pitch::CENTRE) +
                       ((homeKickingOff ? -1.0F : 1.0F) *
                        MatchTuning::Pitch::KICKOFF_TAKER_BEHIND_METRES),
                   pitch.atY(MatchTuning::Pitch::CENTRE)};
    taker->movementTarget = taker->position;
    assignRestartOwner(taker->player);
  }
  state = MatchState::KICK_OFF;
  kickOffWalkIn = false;
  setPieceTimer = MatchTuning::Timing::KICKOFF_DELAY_SECONDS;
  updateTeamPhases();
}

PitchPoint MatchEngine::kickOffPosition(const MatchPlayer& player) const
{
  // Worked out in fractions of the pitch (the formation's frame).
  const float base = pitch.fractionX(player.basePosition);
  const float x =
      player.isHomeTeam
          ? MatchTuning::Pitch::KICKOFF_FORMATION_INSET +
                (base * MatchTuning::Pitch::KICKOFF_FORMATION_SCALE)
          : (1.0F - MatchTuning::Pitch::KICKOFF_FORMATION_INSET) -
                ((1.0F - base) * MatchTuning::Pitch::KICKOFF_FORMATION_SCALE);
  return pitch.at(x, pitch.fractionY(player.basePosition));
}

MatchPlayer* MatchEngine::restartTaker()
{
  return restartTakerIndex && *restartTakerIndex < players.size()
             ? &players[*restartTakerIndex]
             : nullptr;
}

void MatchEngine::setRestartTaker(MatchPlayer* taker)
{
  restartLineUp.reset();
  if (taker != nullptr)
  {
    restartTakerIndex = slotOf(*taker);
  }
  else
  {
    restartTakerIndex.reset();
  }
}

std::size_t MatchEngine::slotOf(const MatchPlayer& player) const
{
  return static_cast<std::size_t>(&player - players.data());
}

void MatchEngine::arrangeSetPiece(bool attackingHome, PitchPoint ballPosition)
{
  // The attacking side's brain sends its players into the box; the
  // defending side's brain then picks up the attackers it sees there.
  const SetPieceAttackers attackers =
      arrangeSetPieceAttack(attackingHome, ballPosition);
  arrangeSetPieceDefence(!attackingHome, ballPosition, attackers);
}

bool MatchEngine::userRestartPassRequested() const
{
  if (playHalfMinutes == 0 || !controlledIndex.has_value() ||
      !restartTakerIndex.has_value())
  {
    return false;
  }
  switch (state)
  {
    case MatchState::KICK_OFF:
    case MatchState::THROW_IN:
    case MatchState::GOAL_KICK:
    case MatchState::CORNER_KICK:
    case MatchState::FREE_KICK:
      break;
    default:
      return false;
  }
  if (*restartTakerIndex >= players.size())
  {
    return false;
  }
  if (players.at(*controlledIndex).isHomeTeam !=
      players.at(*restartTakerIndex).isHomeTeam)
  {
    return false;
  }
  return controlInput.action == MatchInputAction::PASS ||
         controlInput.action == MatchInputAction::LOFTED_PASS ||
         controlInput.action == MatchInputAction::THROUGH_BALL;
}

void MatchEngine::takeSetPiece(MatchPlayer& taker, MatchState restartState)
{
  playRestart(taker, chooseRestartPlay(taker, restartState));
}

void MatchEngine::playRestart(MatchPlayer& taker, const RestartPlay& play)
{
  switch (play.kind)
  {
    case RestartPlay::Kind::DIRECT_SHOT:
      takeDirectFreeKick(taker);
      return;
    case RestartPlay::Kind::PASS:
      passBall(taker, *play.pass, play.lofted);
      return;
    case RestartPlay::Kind::HOLD:
      setPossession(taker);
      return;
  }
}

bool MatchEngine::linesUpRestart(const MatchPlayer& taker,
                                 MatchState restart) const
{
  // The dead zone is the Binturong body's, and an outfielder's (a keeper
  // reaches all round); a throw-in is taken with the hands.
  return getBody(taker.isHomeTeam) == BodyId::BINTURONG &&
         !taker.isGoalkeeper && restart != MatchState::THROW_IN;
}

bool MatchEngine::lineUpRestart(MatchPlayer& taker, MatchState restart,
                                bool userPass)
{
  if (!restartLineUp)
  {
    // His time is up: he decides what to do with it, then lines it up.
    RestartLineUp lineUp;
    const PitchPoint goal =
        pitch.at(taker.isHomeTeam ? 1.0F : 0.0F, MatchTuning::Pitch::CENTRE);
    if (userPass)
    {
      lineUp.userAction = controlInput.action;
      controlInput.action = MatchInputAction::NONE;
      controlActionRemaining = 0.0F;
    }
    else if (restart == MatchState::PENALTY)
    {
      lineUp.play.kind = RestartPlay::Kind::DIRECT_SHOT;
      lineUp.line = metricDirection(ball.position, goal);
    }
    else
    {
      lineUp.play = chooseRestartPlay(taker, restart);
      if (lineUp.play.kind == RestartPlay::Kind::PASS && lineUp.play.pass)
      {
        lineUp.line =
            metricDirection(ball.position, lineUp.play.pass->targetPoint);
      }
      else if (lineUp.play.kind == RestartPlay::Kind::DIRECT_SHOT)
      {
        lineUp.line = metricDirection(ball.position, goal);
      }
    }
    restartLineUp = lineUp;
  }
  const RestartLineUp& lineUp = *restartLineUp;
  // Kept, not kicked: nothing to line up.
  if (lineUp.userAction == MatchInputAction::NONE &&
      lineUp.play.kind == RestartPlay::Kind::HOLD)
  {
    return true;
  }
  // In his comfort gap: he walks right up to a dead ball (inside it the ball
  // is never in his dead zone, and he is not stretching for it).
  const Vector2F fromBall = taker.position - ball.position;
  if (length(fromBall) > touchKnobs[TouchKnob::COMFORT_GAP])
  {
    return false;
  }
  // Turned to his line, or out of time to turn further.
  if (!lineUp.line ||
      lineUp.seconds >= MatchTuning::Dribble::TURN_TO_PASS_MAX_SECONDS)
  {
    return true;
  }
  const Vector2F facing = normalized(
      {.x = std::cos(taker.facingAngle), .y = std::sin(taker.facingAngle)});
  return facing.x * lineUp.line->x + facing.y * lineUp.line->y >=
         std::cos(MatchTuning::Dribble::TURN_TO_PASS_READY_DEGREES *
                  std::numbers::pi_v<float> / 180.0F);
}

void MatchEngine::steerRestartLineUp(MatchPlayer& taker, float dt)
{
  // To just behind the ball on his line (inside his comfort gap), facing
  // along it; with no line (a controller's pass), to the ball the way he
  // comes at it.
  RestartLineUp& lineUp = *restartLineUp;
  lineUp.seconds += dt;
  if (possessionId == PossessionId::BINTURONG &&
      outOfPlayId == OutOfPlayId::AARDVARK &&
      taker.position.y >= pitch.ground.boardsY &&
      ball.position.y < pitch.ground.boardsY)
  {
    // A controller's queued restart is retained while he approaches too.
    taker.movementTarget = asPoint(RestartApproach::target(
        pitch, asVector(taker.position), asVector(ball.position),
        MatchTuning::Player::ARRIVAL_DEAD_ZONE_METRES));
    taker.tacticalTarget = taker.movementTarget;
    taker.facingRequested = false;
    return;
  }
  const Vector2F line =
      lineUp.line.value_or(metricDirection(taker.position, ball.position));
  const float back = touchKnobs[TouchKnob::COMFORT_GAP] *
                     MatchTuning::Dribble::RESTART_SPOT_COMFORT_SHARE;
  const Vector2F behind = {.x = -line.x * back, .y = -line.y * back};
  const PitchPoint spot = clampToPitch(
      PitchPoint{ball.position.x + behind.x, ball.position.y + behind.y});
  // A walk counts as arrived anywhere within the arrival dead zone of its
  // target, which is wider than the comfort gap: he aims that far past
  // his spot, the way he comes at it, so he pulls up on it.
  const Vector2F toSpot = spot - taker.position;
  const float metres = length(toSpot);
  const float through =
      MatchTuning::Player::ARRIVAL_DEAD_ZONE_METRES / std::max(metres, EPSILON);
  taker.movementTarget =
      metres > 0.01F ? clampToPitch(PitchPoint{
                           spot.x + ((spot.x - taker.position.x) * through),
                           spot.y + ((spot.y - taker.position.y) * through)})
                     : spot;
  taker.tacticalTarget = taker.movementTarget;
  if (lineUp.line)
  {
    taker.facingRequested = true;
    taker.facingRequest = std::atan2(line.y, line.x);
  }
}

void MatchEngine::noteRestartKick(const MatchPlayer& taker,
                                  const RestartLineUp& lineUp,
                                  MatchState restart)
{
  // Kept, not kicked: nothing to count.
  if (lineUp.userAction == MatchInputAction::NONE &&
      lineUp.play.kind == RestartPlay::Kind::HOLD)
  {
    return;
  }
  RestartLineUpStats& tally = restartLineUpStats;
  const auto index = static_cast<std::size_t>(restart);
  if (index < tally.byRestart.size())
  {
    ++tally.byRestart[index];
  }
  ++tally.kicked;
  const Vector2F fromBall = taker.position - ball.position;
  const float gap = length(fromBall);
  if (gap > touchKnobs[TouchKnob::COMFORT_GAP])
  {
    ++tally.outsideComfort;
  }
  if (ballBehindReach(taker, fromBall.x, fromBall.y, gap))
  {
    ++tally.inDeadZone;
  }
  if (lineUp.line)
  {
    const Vector2F facing = normalized(
        {.x = std::cos(taker.facingAngle), .y = std::sin(taker.facingAngle)});
    const bool turned =
        facing.x * lineUp.line->x + facing.y * lineUp.line->y >=
        std::cos(MatchTuning::Dribble::TURN_TO_PASS_READY_DEGREES *
                 std::numbers::pi_v<float> / 180.0F);
    ++(turned ? tally.turned : tally.outOfTime);
  }
  tally.secondsSum += lineUp.seconds;
  tally.secondsMax = std::max(tally.secondsMax, lineUp.seconds);
  tally.gapSum += gap;
  tally.gapMax = std::max(tally.gapMax, gap);
}

void MatchEngine::resetPositions()
{
  for (auto& player : players)
  {
    if (!active(player))
    {
      continue;
    }
    player.position = player.basePosition;
    player.movementTarget = player.basePosition;
    player.velocity = {.x = 0.0F, .y = 0.0F};
    player.intent = PlayerIntent::HOLD_SHAPE;
    player.isPressing = false;
    player.isMakingRun = false;
    player.actionCooldown = 0.0F;
    // Placed for the kick-off: on his feet.
    if (player.fallen)
    {
      standUp(player);
    }
    player.stumbleSeconds = 0.0F;
    player.collisionPause = 0.0F;
  }
}

MatchPlayer* MatchEngine::findClosestPlayer(PitchPoint position, bool homeTeam,
                                            bool includeGoalkeeper)
{
  MatchPlayer* closest = nullptr;
  float bestDistance = std::numeric_limits<float>::max();
  for (auto& player : players)
  {
    if (player.isHomeTeam != homeTeam || !active(player) ||
        (!includeGoalkeeper && player.isGoalkeeper))
    {
      continue;
    }
    const float candidateDistance = distance(position, player.position);
    if (candidateDistance < bestDistance)
    {
      bestDistance = candidateDistance;
      closest = &player;
    }
  }
  return closest;
}

MatchPlayer* MatchEngine::findGoalkeeper(bool homeTeam)
{
  const auto goalkeeper =
      std::ranges::find_if(players,
                           [homeTeam](const MatchPlayer& player)
                           {
                             return player.isHomeTeam == homeTeam &&
                                    active(player) && player.isGoalkeeper;
                           });
  return goalkeeper == players.end() ? nullptr : &*goalkeeper;
}

MatchPlayer* MatchEngine::findMatchPlayer(const Player* player)
{
  if (player == nullptr)
  {
    return nullptr;
  }
  const auto found =
      std::ranges::find_if(players, [player](const MatchPlayer& matchPlayer)
                           { return matchPlayer.player == player; });
  return found == players.end() ? nullptr : &*found;
}

float MatchEngine::nearestOpponentDistance(const MatchPlayer& player) const
{
  float bestDistance = std::numeric_limits<float>::max();
  for (const auto& opponent : players)
  {
    if (opponent.isHomeTeam != player.isHomeTeam && active(opponent))
    {
      bestDistance =
          std::min(bestDistance, distance(player.position, opponent.position));
    }
  }
  return bestDistance;
}

float MatchEngine::openSpaceAhead(const MatchPlayer& carrier) const
{
  constexpr float OPEN_SPACE_RADIUS_METRES = 13.5F;
  // Probed 16.8 m ahead of him, kept 2.1 m off the goal lines; across, in
  // three lanes at shares of the width (on purpose).
  constexpr float PROBE_AHEAD_METRES = 16.8F;
  constexpr float PROBE_END_MARGIN_METRES = 2.1F;
  const float direction = carrier.isHomeTeam ? 1.0F : -1.0F;
  const float probeDepth = std::clamp(
      carrier.position.x + (direction * PROBE_AHEAD_METRES),
      PROBE_END_MARGIN_METRES, pitch.length - PROBE_END_MARGIN_METRES);
  constexpr float CHANNELS[3] = {0.25F, 0.50F, 0.75F};
  float openness = 0.0F;
  for (const float channelY : CHANNELS)
  {
    const PitchPoint probe{probeDepth, pitch.atY(channelY)};
    float nearest = std::numeric_limits<float>::max();
    for (const auto& opponent : players)
    {
      if (opponent.isHomeTeam == carrier.isHomeTeam || !active(opponent))
      {
        continue;
      }
      nearest = std::min(nearest, distance(probe, opponent.position));
    }
    openness += std::clamp(nearest / OPEN_SPACE_RADIUS_METRES, 0.0F, 1.0F);
  }
  return openness / 3.0F;
}

float MatchEngine::passingLaneRisk(const MatchPlayer& passer,
                                   const MatchPlayer& receiver) const
{
  const Vector2F segment = receiver.position - passer.position;
  const float segmentLengthSquared =
      (segment.x * segment.x) + (segment.y * segment.y);
  if (segmentLengthSquared <= EPSILON)
  {
    return 1.0F;
  }

  float combinedSafety = 1.0F;
  for (const auto& opponent : players)
  {
    if (!active(opponent) || opponent.isHomeTeam == passer.isHomeTeam)
    {
      continue;
    }
    const Vector2F fromPasser = opponent.position - passer.position;
    const float projection =
        std::clamp((fromPasser.x * segment.x + fromPasser.y * segment.y) /
                       segmentLengthSquared,
                   0.0F, 1.0F);
    if (projection < MatchTuning::Passing::LANE_START_MARGIN ||
        projection > MatchTuning::Passing::LANE_END_MARGIN)
    {
      continue;
    }

    const float laneDistance =
        length({.x = fromPasser.x - (segment.x * projection),
                .y = fromPasser.y - (segment.y * projection)});
    const float interceptionRadius =
        MatchTuning::Passing::BASE_INTERCEPTION_RADIUS_METRES +
        (opponent.defending *
         MatchTuning::Passing::DEFENDING_INTERCEPTION_METRES) +
        (opponent.pace * MatchTuning::Passing::PACE_INTERCEPTION_METRES);
    const float individualRisk = std::clamp(
        1.0F - (laneDistance / std::max(interceptionRadius, EPSILON)), 0.0F,
        1.0F);
    combinedSafety *=
        1.0F -
        individualRisk * (MatchTuning::Passing::BASE_INTERCEPTION_RISK +
                          projection * MatchTuning::Passing::LATE_LANE_RISK);
  }
  return std::clamp(1.0F - combinedSafety, 0.0F, 1.0F);
}

MatchEngine::XgBreakdown MatchEngine::xgBreakdown(const MatchPitch& pitch,
                                                  PitchPoint at,
                                                  bool attackingRight,
                                                  float shooting,
                                                  float nearestOpponentMetres)
{
  // Logistic expected-goals model on distance, visible goal angle, distance
  // to the goal line and lateral offset, scaled by technique and pressure.
  using S = MatchTuning::Shooting;
  XgBreakdown parts;
  // No goal on the pitch (a training pitch without goals): no chance at all,
  // so nobody shoots at a bare wall (training ground plan section 20).
  if (!pitch.goal.present())
  {
    parts.goal = false;
    parts.xg = 0.0F;
    return parts;
  }
  const float goalX = pitch.atX(attackingRight ? 1.0F : 0.0F);
  parts.lineMetres = std::abs(goalX - at.x);
  parts.lateralMetres = std::abs(pitch.atY(MatchTuning::Pitch::CENTRE) - at.y);
  parts.metres = std::sqrt((parts.lineMetres * parts.lineMetres) +
                           (parts.lateralMetres * parts.lateralMetres));
  // The fitted surface is only valid within shooting range.
  if (parts.metres > S::XG_MAX_DISTANCE_METRES)
  {
    parts.inRange = false;
    parts.xg = S::MIN_OPEN_PLAY_XG;
    return parts;
  }

  const Vector2F toTop = Vector2F{
      .x = goalX - at.x,
      .y = pitch.atY(MatchTuning::Pitch::CENTRE) - pitch.goal.halfWidth - at.y};
  const Vector2F toBottom = Vector2F{
      .x = goalX - at.x,
      .y = pitch.atY(MatchTuning::Pitch::CENTRE) + pitch.goal.halfWidth - at.y};
  const float denominator = length(toTop) * length(toBottom);
  const float cosine =
      denominator > EPSILON
          ? std::clamp(
                (toTop.x * toBottom.x + toTop.y * toBottom.y) / denominator,
                -1.0F, 1.0F)
          : 1.0F;
  parts.angle = std::acos(cosine);
  const float lineMetres = parts.lineMetres;
  const float lateralMetres = parts.lateralMetres;
  const float angle = parts.angle;
  parts.logit = S::XG_INTERCEPT + S::XG_ANGLE * angle +
                S::XG_DISTANCE * parts.metres + S::XG_LINE * lineMetres +
                S::XG_LATERAL * lateralMetres +
                S::XG_LINE_SQUARED * lineMetres * lineMetres +
                S::XG_LATERAL_SQUARED * lateralMetres * lateralMetres +
                S::XG_ANGLE_LINE * angle * lineMetres;
  parts.location = 1.0F / (1.0F + std::exp(-parts.logit));
  parts.pressure =
      std::clamp((S::PRESSURE_RADIUS_METRES - nearestOpponentMetres) /
                     S::PRESSURE_RADIUS_METRES,
                 0.0F, 1.0F);
  parts.skillFactor =
      S::BASE_SKILL_FACTOR + shooting * S::SHOOTING_SKILL_FACTOR;
  parts.pressureFactor = 1.0F - parts.pressure * S::PRESSURE_PENALTY;
  parts.unclamped = parts.location * parts.skillFactor * parts.pressureFactor;
  parts.xg =
      std::clamp(parts.unclamped, S::MIN_OPEN_PLAY_XG, S::MAX_OPEN_PLAY_XG);
  return parts;
}

float MatchEngine::estimateShotXG(const MatchPlayer& shooter) const
{
  // Out of range or without a goal, the opponents don't matter: they are
  // only looked at within it (as before the breakdown was split out).
  if (!pitch.goal.present())
  {
    return 0.0F;
  }
  const float goalX = pitch.atX(shooter.isHomeTeam ? 1.0F : 0.0F);
  const float lineMetres = std::abs(goalX - shooter.position.x);
  const float lateralMetres =
      std::abs(pitch.atY(MatchTuning::Pitch::CENTRE) - shooter.position.y);
  if (std::sqrt((lineMetres * lineMetres) + (lateralMetres * lateralMetres)) >
      MatchTuning::Shooting::XG_MAX_DISTANCE_METRES)
  {
    return MatchTuning::Shooting::MIN_OPEN_PLAY_XG;
  }
  return xgBreakdown(pitch, shooter.position, shooter.isHomeTeam,
                     shooter.shooting, nearestOpponentDistance(shooter))
      .xg;
}

float MatchEngine::offsideLine(bool attackingHome) const
{
  float nearestGoalDefender = attackingHome
                                  ? -std::numeric_limits<float>::infinity()
                                  : std::numeric_limits<float>::infinity();
  float secondNearestGoalDefender = nearestGoalDefender;
  std::size_t defenderCount = 0;
  for (const auto& player : players)
  {
    if (player.isHomeTeam == attackingHome || !active(player))
    {
      continue;
    }
    ++defenderCount;
    const float position = player.position.x;
    if (attackingHome)
    {
      if (position >= nearestGoalDefender)
      {
        secondNearestGoalDefender = nearestGoalDefender;
        nearestGoalDefender = position;
      }
      else if (position > secondNearestGoalDefender)
      {
        secondNearestGoalDefender = position;
      }
    }
    else
    {
      if (position <= nearestGoalDefender)
      {
        secondNearestGoalDefender = nearestGoalDefender;
        nearestGoalDefender = position;
      }
      else if (position < secondNearestGoalDefender)
      {
        secondNearestGoalDefender = position;
      }
    }
  }
  if (defenderCount < 2)
  {
    return pitch.atX(attackingHome ? 1.0F : 0.0F);
  }
  return secondNearestGoalDefender;
}

bool MatchEngine::isOffside(const MatchPlayer& receiver, bool attackingHome,
                            float defenderLine, float lineTolerance) const
{
  if (!offsideCalled())
  {
    return false;
  }
  // The margins and the tolerance are metres.
  const float margin =
      MatchTuning::Passing::OFFSIDE_MARGIN_METRES + lineTolerance;
  const float ballMargin = MatchTuning::Passing::OFFSIDE_MARGIN_METRES;
  const float halfway = pitch.atX(MatchTuning::Pitch::CENTRE);

  if (attackingHome)
  {
    return receiver.position.x > halfway &&
           receiver.position.x > ball.position.x + ballMargin &&
           receiver.position.x > defenderLine + margin;
  }

  return receiver.position.x < halfway &&
         receiver.position.x < ball.position.x - ballMargin &&
         receiver.position.x < defenderLine - margin;
}

float MatchEngine::randomFloat(float minimum, float maximum)
{
  return minimum + ((maximum - minimum) * uniform01(rng));
}

float MatchEngine::incidentRoll() { return uniform01(incidentRng); }

bool MatchEngine::isHomePlayer(const Player* player) const
{
  if (player == nullptr)
  {
    return false;
  }
  const auto found =
      std::ranges::find_if(players, [player](const MatchPlayer& matchPlayer)
                           { return matchPlayer.player == player; });
  return found != players.end() && found->isHomeTeam;
}

int MatchEngine::getSubstitutionsUsed(bool homeTeam) const
{
  return homeTeam ? stats.homeSubstitutions : stats.awaySubstitutions;
}

int MatchEngine::getSubstitutionWindowsUsed(bool homeTeam) const
{
  return homeTeam ? homeSubstitutionWindows : awaySubstitutionWindows;
}

bool MatchEngine::canSubstitute(bool homeTeam) const
{
  if (state == MatchState::FULL_TIME || state == MatchState::PENALTY_SHOOTOUT)
  {
    return false;
  }
  // Changes under way count (touchline plan stage 5) until they are made.
  int underWay = 0;
  for (const PendingSubstitution& pending : pendingSubstitutions)
  {
    if (pending.stage != PendingSubstitution::Stage::ENTERING &&
        pending.slot < players.size() &&
        players[pending.slot].isHomeTeam == homeTeam)
    {
      ++underWay;
    }
  }
  if (getSubstitutionsUsed(homeTeam) + underWay >= maxSubstitutions())
  {
    return false;
  }
  // Half-time changes and further changes in an already used stoppage do
  // not consume a new window.
  const std::uint32_t lastWindow =
      homeTeam ? homeLastWindowStoppage : awayLastWindowStoppage;
  const bool sameStoppage =
      lastWindow == stoppageSequence &&
      (state != MatchState::PLAYING || manualSubstitutionStep == stepCounter);
  const int windows =
      MatchTuning::Substitution::MAX_WINDOWS +
      (extraTimeReached ? MatchTuning::Rules::EXTRA_TIME_SUBSTITUTIONS : 0);
  return state == MatchState::HALF_TIME ||
         getSubstitutionWindowsUsed(homeTeam) < windows || sameStoppage;
}

void MatchEngine::setAutoSubstitutions(bool home, bool away)
{
  if (home != homeAutoSubstitutions || away != awayAutoSubstitutions)
  {
    ++inputRevision;
  }
  homeAutoSubstitutions = home;
  awayAutoSubstitutions = away;
}

bool MatchEngine::substitutePlayer(uint32_t outPlayerId, const Player* inPlayer,
                                   std::optional<std::size_t> slot)
{
  if ((inPlayer == nullptr) || state == MatchState::FULL_TIME)
  {
    return false;
  }
  MatchCommandRecord command;
  command.type = MatchCommandType::SUBSTITUTION;
  command.homeTeam = std::ranges::find(homeBench, inPlayer) != homeBench.end();
  command.player = outPlayerId;
  command.incoming = inPlayer->getId();
  command.slot = slot;
  return recordCommand(command);
}

bool MatchEngine::applySubstitution(MatchPlayer& outgoing,
                                    const Player* inPlayer,
                                    SubstitutionReason reason)
{
  if ((inPlayer == nullptr) || !active(outgoing) ||
      !canSubstitute(outgoing.isHomeTeam))
  {
    return false;
  }
  const bool homeTeam = outgoing.isHomeTeam;
  if (state != MatchState::HALF_TIME)
  {
    std::uint32_t& lastWindow =
        homeTeam ? homeLastWindowStoppage : awayLastWindowStoppage;
    if (lastWindow != stoppageSequence)
    {
      lastWindow = stoppageSequence;
      ++(homeTeam ? homeSubstitutionWindows : awaySubstitutionWindows);
    }
    ++stoppageLogs[static_cast<std::size_t>(period - 1)].substitutions;
    extendRestart(MatchTuning::Timing::SUBSTITUTION_DELAY_SECONDS);
  }
  ++(homeTeam ? stats.homeSubstitutions : stats.awaySubstitutions);
  auto& bench = homeTeam ? homeBench : awayBench;
  std::erase(bench, inPlayer);
  leaveTouchline(inPlayer);

  PlayerMatchStats& leaving = statsOf(outgoing);
  leaving.substitutedOff = true;
  leaving.condition = outgoing.stamina;

  const bool hadPossession = getPossessionAttachment() == outgoing.player;
  const PlayerID outgoingId = outgoing.player->getId();
  if (std::ranges::find(squad, inPlayer) == squad.end())
  {
    squad.push_back(inPlayer);
  }
  const bool incomingKeeper = inPlayer->getRole() == PlayerRole::GK;
  if (incomingKeeper && !outgoing.isGoalkeeper)
  {
    // A replacement keeper takes over from any emergency keeper.
    for (auto& teammate : players)
    {
      if (teammate.isHomeTeam != homeTeam || !teammate.isGoalkeeper)
      {
        continue;
      }
      teammate.isGoalkeeper = false;
      // An emergency keeper goes back out to the slot the new one frees.
      if (active(teammate) && outgoing.formationSlot >= 0)
      {
        teammate.formationSlot = outgoing.formationSlot;
        placeFormation(homeTeam);
      }
    }
    outgoing.basePosition = PitchPoint{
        homeTeam ? MatchTuning::Pitch::LINEUP_GOALKEEPER_METRES
                 : pitch.length - MatchTuning::Pitch::LINEUP_GOALKEEPER_METRES,
        pitch.atY(MatchTuning::Pitch::CENTRE)};
    outgoing.isGoalkeeper = true;
  }
  outgoing.player = inPlayer;
  loadAttributes(outgoing, inPlayer);
  const auto benchCondition =
      std::ranges::find_if(benchConditions, [inPlayer](const auto& entry)
                           { return entry.first == inPlayer->getId(); });
  outgoing.stamina =
      benchCondition != benchConditions.end() ? benchCondition->second : 1.0F;
  outgoing.sprintReserve = 1.0F;
  outgoing.fastBurstDebt = 0.0F;
  outgoing.isInjured = false;
  outgoing.yellowCards = 0;
  outgoing.actionCooldown = MatchTuning::Player::SUBSTITUTION_SETTLE_SECONDS;
  outgoing.statsIndex = addPlayerStats(outgoing, false);
  substitutePossession(hadPossession, outgoingId, inPlayer);

  substitutions.push_back({.timeMinute = matchTimeMinutes,
                           .period = period,
                           .isHomeTeam = homeTeam,
                           .outgoingPlayerId = outgoingId,
                           .incomingPlayerId = inPlayer->getId(),
                           .reason = reason});
  MatchEvent& event = logEvent(MatchEventType::SUBSTITUTION, outgoing);
  event.secondaryPlayerId = outgoingId;
  return true;
}

void MatchEngine::runAiSubstitutions()
{
  if (homeLastAiReviewStoppage != stoppageSequence)
  {
    homeLastAiReviewStoppage = stoppageSequence;
    runAiSubstitutionsFor(true);
    if (homeAutoSubstitutions && drillRules.layers)
    {
      runAiTouchline(true);
    }
  }
  if (awayLastAiReviewStoppage != stoppageSequence)
  {
    awayLastAiReviewStoppage = stoppageSequence;
    runAiSubstitutionsFor(false);
    if (awayAutoSubstitutions && drillRules.layers)
    {
      runAiTouchline(false);
    }
  }
}

void MatchEngine::setMedicalFlags(PlayerID playerId, std::uint8_t flags)
{
  const auto found =
      std::ranges::find_if(medicalFlags, [playerId](const auto& entry)
                           { return entry.first == playerId; });
  if (found != medicalFlags.end())
  {
    found->second = flags;
  }
  else if (flags != 0)
  {
    medicalFlags.emplace_back(playerId, flags);
  }
}

void MatchEngine::checkInjuries()
{
  for (auto& player : players)
  {
    if (!active(player) || player.isInjured)
    {
      continue;
    }
    if (incidentRoll() < injuryChance(player))
    {
      injurePlayer(player, false);
    }
  }
}

void MatchEngine::injurePlayer(MatchPlayer& player, bool fromContact)
{
  if (!active(player) || player.isInjured)
  {
    return;
  }
  player.isInjured = true;
  statsOf(player).injured = true;
  ++(player.isHomeTeam ? stats.homeInjuries : stats.awayInjuries);
  ++stoppageLogs[static_cast<std::size_t>(period - 1)].injuries;
  MatchEvent& event = logEvent(MatchEventType::INJURY, player);
  if (fromContact)
  {
    event.detail = MatchEventDetail::CONTACT;
  }
  event.position = player.position;
  // A player who cannot run no longer carries the ball forward.
  if (getPossessionAttachment() == player.player)
  {
    player.actionCooldown = 0.0F;
  }
}

void MatchEngine::removeFromPitch(MatchPlayer& player, bool walksOff)
{
  if (!active(player))
  {
    return;
  }
  const bool homeTeam = player.isHomeTeam;
  const PitchPoint vacated = player.basePosition;
  const std::int8_t vacatedSlot = player.formationSlot;
  const bool wasKeeper = player.isGoalkeeper;
  statsOf(player).condition = player.stamina;
  if (releaseRemovedPlayer(player.player))
  {
    // Walking off, it runs on as it was (else it stops where he was).
    if (!walksOff)
    {
      ball.velocity = {.x = 0.0F, .y = 0.0F};
    }
  }
  // Walking off (stage 6): his body walks on as a touchline body; the
  // slot's record waits out of sight in the tunnel.
  if (walksOff)
  {
    sendToTunnel(player);
  }
  // Park beside the dugouts, clear of play and of each other.
  int parked = 0;
  for (const auto& other : players)
  {
    if ((other.player != nullptr) && !other.onPitch)
    {
      ++parked;
    }
  }
  player.onPitch = false;
  player.isGoalkeeper = false;
  player.isPressing = false;
  player.isMakingRun = false;
  player.intent = PlayerIntent::HOLD_SHAPE;
  player.velocity = {.x = 0.0F, .y = 0.0F};
  player.position =
      walksOff
          ? PitchPoint{pitch.atX(MatchTuning::Pitch::CENTRE),
                       pitch.ground.standY + MatchTuning::WalkOut::WAIT_METRES}
          : PitchPoint{
                pitch.atX(MatchTuning::Pitch::CENTRE) +
                    ((homeTeam ? -1.0F : 1.0F) *
                     (MatchTuning::Rules::PARKED_PLAYER_OFFSET_METRES +
                      static_cast<float>(parked) *
                          MatchTuning::Rules::PARKED_PLAYER_SPACING_METRES)),
                MatchTuning::Pitch::PLAYER_EDGE_SIDE_METRES};
  player.movementTarget = player.position;
  if (walksOff)
  {
    const auto slot = slotOf(player);
    if (slot < previousPlayerPositions.size())
    {
      previousPlayerPositions[slot] = player.position;
    }
    if (slot < stepStartPositions.size())
    {
      stepStartPositions[slot] = player.position;
    }
  }
  if (restartTaker() == &player)
  {
    MatchPlayer* replacement =
        findClosestPlayer(ball.position, homeTeam, false);
    setRestartTaker(replacement);
    if (walksOff)
    {
      // The new taker goes to the ball; one in his hands drops.
      if (deadBall.active &&
          (deadBall.phase == DeadBallReturn::Phase::CARRIED ||
           deadBall.phase == DeadBallReturn::Phase::LIFTED))
      {
        deadBall.phase = DeadBallReturn::Phase::LOOSE;
        deadBall.seconds = 0.0F;
      }
    }
    else if ((replacement != nullptr) && state != MatchState::PLAYING)
    {
      handBallTo(replacement->player);
    }
  }
  if (walksOff && deadBall.fetcherIndex == slotOf(player))
  {
    deadBall.fetcherIndex.reset();
  }
  if (wasKeeper)
  {
    ensureGoalkeeper(homeTeam);
  }
  std::int8_t slotToRebalance = vacatedSlot;
  if (wasKeeper)
  {
    slotToRebalance = -1;
  }
  rebalanceShape(homeTeam, vacated, slotToRebalance);

  // Law 3: a match cannot continue with fewer than seven players a side.
  const auto remaining = std::ranges::count_if(
      players, [homeTeam](const MatchPlayer& other)
      { return active(other) && other.isHomeTeam == homeTeam; });
  if (drillRules.minimumPlayers &&
      remaining < MatchTuning::Rules::MINIMUM_PLAYERS && !abandoned)
  {
    abandoned = true;
    logEvent(MatchEventType::INFO).detail = MatchEventDetail::ABANDONED;
    period = std::max(period, 2);
    endPeriod();
  }
}

void MatchEngine::rebalanceShape(bool homeTeam, PitchPoint vacatedBase,
                                 std::int8_t vacatedSlot)
{
  // A side reduced to ten fills a defensive hole with the nearest midfielder
  // and drops a little deeper overall.
  const float ownDepth = homeTeam ? pitch.fractionX(vacatedBase)
                                  : 1.0F - pitch.fractionX(vacatedBase);
  if (ownDepth < MatchTuning::Rules::DEFENSIVE_SLOT_DEPTH)
  {
    MatchPlayer* cover = nullptr;
    float best = std::numeric_limits<float>::max();
    for (auto& player : players)
    {
      if (!active(player) || player.isHomeTeam != homeTeam ||
          player.isGoalkeeper)
      {
        continue;
      }
      const float depth = homeTeam
                              ? pitch.fractionX(player.basePosition)
                              : 1.0F - pitch.fractionX(player.basePosition);
      if (depth <= ownDepth + EPSILON)
      {
        continue;
      }
      const float d = distance(player.basePosition, vacatedBase);
      if (d < best)
      {
        best = d;
        cover = &player;
      }
    }
    if (cover != nullptr)
    {
      cover->basePosition = vacatedBase;
      cover->formationSlot = vacatedSlot;
    }
  }
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam || player.isGoalkeeper)
    {
      continue;
    }
    player.basePosition.x = clampToPitchX(
        player.basePosition.x +
        ((homeTeam ? -1.0F : 1.0F) * MatchTuning::Rules::SHORT_HANDED_DROP));
  }
}

void MatchEngine::ensureGoalkeeper(bool homeTeam)
{
  if (findGoalkeeper(homeTeam) != nullptr)
  {
    return;
  }
  // A keeper already on his way on (touchline plan stage 5).
  for (const PendingSubstitution& pending : pendingSubstitutions)
  {
    if ((pending.incoming != nullptr) &&
        pending.incoming->getRole() == PlayerRole::GK &&
        pending.slot < players.size() &&
        players[pending.slot].isHomeTeam == homeTeam)
    {
      return;
    }
  }
  const bool autoManaged =
      homeTeam ? homeAutoSubstitutions : awayAutoSubstitutions;
  if (autoManaged && canSubstitute(homeTeam))
  {
    if (const Player* keeper =
            chooseReplacement(homeTeam, PlayerRole::GK, true))
    {
      // Sacrifice the most advanced outfield player for the new keeper.
      MatchPlayer* sacrificed = nullptr;
      float mostAdvanced = -std::numeric_limits<float>::infinity();
      for (auto& player : players)
      {
        if (!active(player) || player.isHomeTeam != homeTeam)
        {
          continue;
        }
        const float depth = homeTeam
                                ? pitch.fractionX(player.basePosition)
                                : 1.0F - pitch.fractionX(player.basePosition);
        if (depth > mostAdvanced)
        {
          mostAdvanced = depth;
          sacrificed = &player;
        }
      }
      if ((sacrificed != nullptr) &&
          performSubstitution(*sacrificed, keeper,
                              SubstitutionReason::GOALKEEPER_REPLACEMENT))
      {
        return;
      }
    }
  }
  // Otherwise the outfield player best with his hands goes in goal.
  MatchPlayer* emergency = nullptr;
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam)
    {
      continue;
    }
    if ((emergency == nullptr) || player.goalkeeping > emergency->goalkeeping)
    {
      emergency = &player;
    }
  }
  if (emergency == nullptr)
  {
    return;
  }
  emergency->isGoalkeeper = true;
  emergency->basePosition = PitchPoint{
      homeTeam ? MatchTuning::Pitch::LINEUP_GOALKEEPER_METRES
               : pitch.length - MatchTuning::Pitch::LINEUP_GOALKEEPER_METRES,
      pitch.atY(MatchTuning::Pitch::CENTRE)};
}

MatchEvent& MatchEngine::logEvent(MatchEventType type)
{
  if (events.size() >= MatchTuning::Timing::MAX_EVENTS)
  {
    events.erase(events.begin());
  }
  MatchEvent& event = events.emplace_back();
  undescribedEvents = std::min(undescribedEvents + 1, events.size());
  event.timeMinute = matchTimeMinutes;
  event.type = type;
  event.homeScore = homeScore;
  event.awayScore = awayScore;
  event.homeShootout = shootout.goals[0];
  event.awayShootout = shootout.goals[1];
  event.period = period;
  event.addedMinute =
      std::max(0.0F, matchTimeMinutes - MatchRules::periodEndMinute(period));
  event.position = ball.position;
  if (isHighlightTrigger(type))
  {
    recordHighlight(type);
  }
  return event;
}

void MatchEngine::setTeamNames(std::string homeTeam, std::string awayTeam)
{
  teamNames = {std::move(homeTeam), std::move(awayTeam)};
}

std::string MatchEngine::squadPlayerName(PlayerID playerId) const
{
  for (const Player* player : squad)
  {
    if (player->getId() == playerId)
    {
      return player->getName();
    }
  }
  return {};
}

void MatchEngine::describePendingEvents()
{
  if (undescribedEvents == 0)
  {
    return;
  }
  const MatchCommentaryNames names{.homeTeam = teamNames[0],
                                   .awayTeam = teamNames[1],
                                   .player = [this](PlayerID playerId)
                                   { return squadPlayerName(playerId); }};
  for (std::size_t index = events.size() - undescribedEvents;
       index < events.size(); ++index)
  {
    events[index].description = MatchCommentary::describe(events[index], names);
  }
  undescribedEvents = 0;
}

void MatchEngine::recordHighlight(MatchEventType type)
{
  const auto priority = [](MatchEventType candidate)
  {
    switch (candidate)
    {
      case MatchEventType::GOAL:
      case MatchEventType::OWN_GOAL:
        return 4;
      case MatchEventType::PENALTY:
      case MatchEventType::RED_CARD:
      case MatchEventType::SECOND_YELLOW:
        return 3;
      case MatchEventType::SHOT:
        return 2;
      default:
        return 1;
    }
  };
  const double now = getSimulatedSeconds();
  ++highlightTriggerCount;
  lastTriggerType = type;
  lastTriggerSeconds = now;
  lastTriggerMinute = matchTimeMinutes;
  const double start = std::max(0.0, now - HIGHLIGHT_LEAD);
  const double end = now + HIGHLIGHT_TAIL;
  if (!highlights.empty() &&
      start <= highlights.back().endSeconds + HIGHLIGHT_MERGE_GAP)
  {
    MatchHighlight& merged = highlights.back();
    merged.endSeconds = std::max(merged.endSeconds, end);
    if (priority(type) >= priority(merged.type))
    {
      merged.type = type;
      merged.triggerSeconds = now;
      merged.triggerMinute = matchTimeMinutes;
    }
    return;
  }
  highlights.push_back({.type = type,
                        .startSeconds = start,
                        .triggerSeconds = now,
                        .endSeconds = end,
                        .triggerMinute = matchTimeMinutes});
}

MatchEvent& MatchEngine::logEvent(MatchEventType type, const MatchPlayer& actor)
{
  MatchEvent& event = logEvent(type);
  event.hasTeam = true;
  event.isHomeTeam = actor.isHomeTeam;
  event.primaryPlayerId = (actor.player != nullptr) ? actor.player->getId() : 0;
  event.position = actor.position;
  return event;
}

namespace
{
std::string jsonFloat(float value)
{
  return std::isfinite(value) ? std::to_string(value) : std::string("null");
}

std::string jsonEscape(std::string_view value)
{
  std::string escaped;
  escaped.reserve(value.size());
  for (const char c : value)
  {
    switch (c)
    {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20U)
        {
          escaped += "\\u00";
          constexpr char HEX[] = "0123456789abcdef";
          escaped += HEX[(c >> 4U) & 0x0fU];
          escaped += HEX[c & 0x0fU];
        }
        else
        {
          escaped += c;
        }
    }
  }
  return escaped;
}
}  // namespace

std::string MatchEngine::getDebugSnapshotJson() const
{
  std::ostringstream output;
  output << std::fixed << std::setprecision(4);
  output << "{\"time_minute\":" << matchTimeMinutes << ",\"state\":\""
         << stateName(state) << "\",\"score\":{\"home\":" << homeScore
         << ",\"away\":" << awayScore << "},\"team_phase\":{\"home\":\""
         << teamPhaseName(homePhase) << "\",\"away\":\""
         << teamPhaseName(awayPhase) << '"'
         << ",\"transition_seconds_remaining\":" << transitionSecondsRemaining
         << "},\"performance\":{\"last_update_steps\":" << lastUpdateStepCount
         << ",\"dropped_simulation_steps\":" << droppedSimulationSteps
         << "},\"last_pass\":";
  if (lastPassDecision.receiverId == 0)
  {
    output << "null";
  }
  else
  {
    output << "{\"passer\":" << lastPassDecision.passerId
           << ",\"receiver\":" << lastPassDecision.receiverId
           << ",\"intent\":\"" << passIntentName(lastPassDecision.intent)
           << "\",\"target_x\":"
           << pitch.fractionX(lastPassDecision.targetPoint)
           << ",\"target_y\":" << pitch.fractionY(lastPassDecision.targetPoint)
           << ",\"utility\":" << lastPassDecision.utility
           << ",\"progression\":" << lastPassDecision.progression
           << ",\"lane_risk\":" << lastPassDecision.laneRisk
           << ",\"completion_probability\":"
           << lastPassDecision.completionProbability << '}';
  }
  output << ",\"decision\":{\"reason\":\""
         << jsonEscape(lastScenarioDecision.reason) << "\",\"action\":\""
         << scenarioActionName(lastScenarioDecision.action)
         << "\",\"analysis\":{\"pass\":"
         << jsonFloat(lastScenarioDecision.passUtility)
         << ",\"shot\":" << jsonFloat(lastScenarioDecision.shotUtility)
         << ",\"carry\":" << jsonFloat(lastScenarioDecision.carryUtility)
         << ",\"shield\":" << jsonFloat(lastScenarioDecision.shieldUtility)
         << "},\"best\":";
  if (lastScenarioDecision.best)
  {
    output << "{\"receiver\":" << lastScenarioDecision.best->receiverId
           << ",\"intent\":\""
           << passIntentName(lastScenarioDecision.best->intent)
           << "\",\"utility\":" << lastScenarioDecision.best->utility
           << ",\"progression\":" << lastScenarioDecision.best->progression
           << ",\"lane_risk\":" << lastScenarioDecision.best->laneRisk
           << ",\"completion_probability\":"
           << lastScenarioDecision.best->completionProbability << '}';
  }
  else
  {
    output << "null";
  }
  output << ",\"rejected\":";
  if (lastScenarioDecision.runnerUp)
  {
    output << "{\"receiver\":" << lastScenarioDecision.runnerUp->receiverId
           << ",\"intent\":\""
           << passIntentName(lastScenarioDecision.runnerUp->intent)
           << "\",\"utility\":" << lastScenarioDecision.runnerUp->utility
           << ",\"progression\":" << lastScenarioDecision.runnerUp->progression
           << ",\"lane_risk\":" << lastScenarioDecision.runnerUp->laneRisk
           << ",\"completion_probability\":"
           << lastScenarioDecision.runnerUp->completionProbability << '}';
  }
  else
  {
    output << "null";
  }
  output << "},\"ball\":{\"x\":" << pitch.fractionX(ball.position)
         << ",\"y\":" << pitch.fractionY(ball.position)
         << ",\"z_metres\":" << ball.z << ",\"possessed_by\":";
  if (getPossessionAttachment() != nullptr)
  {
    output << getPossessionAttachment()->getId();
  }
  else
  {
    output << "null";
  }
  output << ",\"intended_receiver\":";
  if (getExpectedReceiver() != nullptr)
  {
    output << getExpectedReceiver()->getId();
  }
  else
  {
    output << "null";
  }
  output << "},\"stats\":{\"home_shots\":" << stats.homeShots
         << ",\"away_shots\":" << stats.awayShots
         << ",\"home_xg\":" << stats.homeShotXG
         << ",\"away_xg\":" << stats.awayShotXG
         << ",\"home_possession\":" << stats.homePossession
         << ",\"away_possession\":" << stats.awayPossession
         << ",\"home_substitutions\":" << stats.homeSubstitutions
         << ",\"away_substitutions\":" << stats.awaySubstitutions
         << ",\"home_passes_attempted\":" << stats.homePassesAttempted
         << ",\"away_passes_attempted\":" << stats.awayPassesAttempted
         << ",\"home_passes_completed\":" << stats.homePassesCompleted
         << ",\"away_passes_completed\":" << stats.awayPassesCompleted
         << ",\"home_progressive_passes\":" << stats.homeProgressivePasses
         << ",\"away_progressive_passes\":" << stats.awayProgressivePasses
         << ",\"home_through_balls\":" << stats.homeThroughBalls
         << ",\"away_through_balls\":" << stats.awayThroughBalls
         << ",\"home_crosses\":" << stats.homeCrosses
         << ",\"away_crosses\":" << stats.awayCrosses
         << ",\"home_cutbacks\":" << stats.homeCutbacks
         << ",\"away_cutbacks\":" << stats.awayCutbacks
         << ",\"home_switches_of_play\":" << stats.homeSwitchesOfPlay
         << ",\"away_switches_of_play\":" << stats.awaySwitchesOfPlay
         << ",\"home_tackles\":" << stats.homeTackles
         << ",\"away_tackles\":" << stats.awayTackles
         << ",\"home_offsides\":" << stats.homeOffsides
         << ",\"away_offsides\":" << stats.awayOffsides
         << ",\"home_fouls\":" << stats.homeFouls
         << ",\"away_fouls\":" << stats.awayFouls
         << ",\"home_yellow_cards\":" << stats.homeYellowCards
         << ",\"away_yellow_cards\":" << stats.awayYellowCards
         << ",\"home_red_cards\":" << stats.homeRedCards
         << ",\"away_red_cards\":" << stats.awayRedCards
         << ",\"home_corners\":" << stats.homeCorners
         << ",\"away_corners\":" << stats.awayCorners
         << ",\"home_injuries\":" << stats.homeInjuries
         << ",\"away_injuries\":" << stats.awayInjuries
         << "},\"clock\":{\"period\":" << period
         << ",\"added_first_half\":" << addedMinutes[0]
         << ",\"added_second_half\":" << addedMinutes[1]
         << ",\"referee_strictness\":" << refereeStrictness
         << "},\"goalkeepers\":{\"home\":\""
         << goalkeeperStateName(keepers[0].state) << "\",\"away\":\""
         << goalkeeperStateName(keepers[1].state) << "\"},\"players\":[";
  for (size_t index = 0; index < players.size(); ++index)
  {
    const auto& player = players[index];
    if (index > 0)
    {
      output << ',';
    }
    output << "{\"id\":"
           << ((player.player != nullptr) ? player.player->getId() : 0)
           << ",\"name\":"
           << std::quoted((player.player != nullptr) ? player.player->getName()
                                                     : "")
           << ",\"home\":" << (player.isHomeTeam ? "true" : "false")
           << ",\"x\":" << pitch.fractionX(player.position)
           << ",\"y\":" << pitch.fractionY(player.position)
           << ",\"target_x\":" << pitch.fractionX(player.movementTarget)
           << ",\"target_y\":" << pitch.fractionY(player.movementTarget)
           << ",\"intent\":\"" << intentName(player.intent)
           << "\",\"stamina\":" << player.stamina
           << ",\"pressing\":" << (player.isPressing ? "true" : "false")
           << ",\"making_run\":" << (player.isMakingRun ? "true" : "false")
           << ",\"on_pitch\":" << (player.onPitch ? "true" : "false")
           << ",\"goalkeeper\":" << (player.isGoalkeeper ? "true" : "false")
           << ",\"yellow_cards\":" << player.yellowCards
           << ",\"rating\":" << playerStats[player.statsIndex].rating << '}';
  }
  output << "]}";
  return output.str();
}

bool MatchEngine::writeDebugSnapshot(std::string_view path) const
{
  std::ofstream output{std::string(path)};
  if (!output.is_open())
  {
    return false;
  }
  output << getDebugSnapshotJson() << '\n';
  return output.good();
}
