// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Walk-outs and walk-offs (touchline plan, stage 4): nobody is placed at the
// start or the end of a half. At the match's start everyone comes out of the
// tunnel (the officials, then the teams side by side, captains first, then
// the staff), the teams line up on the halfway line facing the dugouts'
// stand and the away side files along the officials and the home line
// shaking hands; then everyone goes to the kick-off as after a goal. At
// half-time everyone walks into the tunnel and the break runs once they are
// all in; the second half's walk-out goes straight to the kick-off.

#include <algorithm>
#include <cmath>
#include <numbers>

#include "model/lineup.h"
#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/match_ground.h"

using namespace MatchEngineInternal;

namespace
{
using W = MatchTuning::WalkOut;

/** Facing the bottom touchline's stand (radians). */
constexpr float FACING_STAND = 0.5f * std::numbers::pi_v<float>;

/** Where a body waits in the tunnel: its file (home left, away right,
 * officials in the middle). */
Vector2F waitSpot(const MatchPitch& pitch, float file)
{
  return {pitch.ground.halfway + file * W::FILE_METRES,
          pitch.ground.standY + W::WAIT_METRES};
}

float metresBetween(Vector2F a, Vector2F b) { return std::hypot(a.x - b.x, a.y - b.y); }
}  // namespace

bool MatchEngine::walksOut() const
{
  return outOfPlayId == OutOfPlayId::BINTURONG && bodyOf(true) == BodyId::BINTURONG &&
         bodyOf(false) == BodyId::BINTURONG;
}

void MatchEngine::beginWalkOut(bool homeKickingOff, bool lineUp)
{
  // The kick-off's own set-up, without placing anyone: the ball stays
  // where it is (the taker fetches it once everyone is out).
  restartIsSetPiece = false;
  pendingAdvantage.active = false;
  restartTakerIndex.reset();
  restartLineUp.reset();
  deadBall = {};
  clearPossession();
  clearShooterHistory();
  state = MatchState::KICK_OFF;
  kickOffWalkIn = false;
  setPieceTimer = MatchTuning::Timing::KICKOFF_DELAY_SECONDS;
  walk.phase = WalkPhase::OUT;
  walk.seconds = 0.0f;
  walk.lineUp = lineUp;
  walk.homeKicksOff = homeKickingOff;

  // Each side in teamsheet order, its captain first (the vice-captain when
  // he is not on; else the teamsheet's order).
  const auto order = [this](bool home)
  {
    std::vector<std::size_t> slots;
    for (std::size_t slot = 0; slot < players.size(); ++slot)
      if (active(players[slot]) && players[slot].isHomeTeam == home) slots.push_back(slot);
    const SetPieceDesignations& duties = designations[home ? 0 : 1];
    for (const SetPieceDuty duty : {SetPieceDuty::Captain, SetPieceDuty::ViceCaptain})
    {
      const PlayerID id = duties[static_cast<std::size_t>(duty)];
      const auto found = std::ranges::find_if(
          slots, [&](std::size_t slot) { return id != 0 && players[slot].player->getId() == id; });
      if (found == slots.end()) continue;
      std::rotate(slots.begin(), found, found + 1);
      break;
    }
    return slots;
  };
  const std::vector<std::size_t> home = order(true);
  const std::vector<std::size_t> away = order(false);

  // Out one after another: the officials, the teams side by side, the staff.
  walk.playerRelease.assign(players.size(), 0.0f);
  walk.touchlineRelease.assign(touchlineBodies.size(), 0.0f);
  walk.playerSpot.assign(players.size(), {});
  walk.touchlineSpot.assign(touchlineBodies.size(), {});
  float next = 0.0f;
  std::vector<std::size_t> officials;
  for (const TouchlineRole role :
       {TouchlineRole::REFEREE, TouchlineRole::ASSISTANT_REFEREE, TouchlineRole::FOURTH_OFFICIAL})
    for (std::size_t index = 0; index < touchlinePeople.size(); ++index)
      if (touchlinePeople[index].role == role)
      {
        walk.touchlineRelease[index] = next;
        next += W::RELEASE_SECONDS;
        officials.push_back(index);
      }
  for (std::size_t rank = 0; rank < std::max(home.size(), away.size()); ++rank)
  {
    if (rank < home.size()) walk.playerRelease[home[rank]] = next;
    if (rank < away.size()) walk.playerRelease[away[rank]] = next;
    next += W::RELEASE_SECONDS;
  }
  std::array<float, 2> staff{next, next};
  for (std::size_t index = 0; index < touchlinePeople.size(); ++index)
  {
    if (isOfficial(touchlinePeople[index].role)) continue;
    float& side = staff[touchlineBodies[index].isHomeTeam ? 0 : 1];
    walk.touchlineRelease[index] = side;
    side += W::RELEASE_SECONDS;
  }

  // The line-up: the officials in the middle (assistant, referee,
  // assistant, fourth official from the left), each team out from them,
  // its captain nearest.
  const float lineY = pitch.width - W::LINE_IN_METRES;
  for (std::size_t rank = 0; rank < officials.size(); ++rank)
  {
    // From the left: top assistant, referee, bottom assistant, fourth.
    const TouchlinePerson& person = touchlinePeople[officials[rank]];
    const float place = person.role == TouchlineRole::REFEREE           ? 1.0f
                        : person.role == TouchlineRole::FOURTH_OFFICIAL ? 3.0f
                        : person.topLine                                ? 0.0f
                                                                        : 2.0f;
    walk.touchlineSpot[officials[rank]] = {
        pitch.ground.halfway + (place - 1.5f) * W::OFFICIAL_SPACING_METRES, lineY};
  }
  const float teamStart = 1.5f * W::OFFICIAL_SPACING_METRES + W::TEAM_FROM_OFFICIALS_METRES;
  for (std::size_t rank = 0; rank < home.size(); ++rank)
    walk.playerSpot[home[rank]] = {
        pitch.ground.halfway - teamStart - static_cast<float>(rank) * W::PLAYER_SPACING_METRES, lineY};
  for (std::size_t rank = 0; rank < away.size(); ++rank)
    walk.playerSpot[away[rank]] = {
        pitch.ground.halfway + teamStart + static_cast<float>(rank) * W::PLAYER_SPACING_METRES, lineY};

  // The handshake: the away side walks, along the officials then the home
  // line (right to left).
  walk.walkers = away;
  walk.hands.clear();
  for (const std::size_t index : officials) walk.hands.push_back(walk.touchlineSpot[index].x);
  for (const std::size_t slot : home) walk.hands.push_back(walk.playerSpot[slot].x);
  std::ranges::sort(walk.hands, std::greater<>());
  walk.nextHand.assign(players.size(), 0);
  walk.handPause.assign(players.size(), 0.0f);
  walk.through.assign(players.size(), false);

  for (MatchPlayer& player : players)
  {
    if (!active(player)) continue;
    player.intent = PlayerIntent::HOLD_SHAPE;
    player.isPressing = false;
    player.isMakingRun = false;
    player.facingRequested = false;
  }
  for (TouchlinePerson& person : touchlinePeople) person.seated = false;
  updateTeamPhases();
}

void MatchEngine::startInTunnel()
{
  // Before a ball is kicked: everyone starts in the dressing room's end of
  // the tunnel, out of sight.
  const auto wait = [this](MatchPlayer& body, float file)
  {
    body.position = asPoint(waitSpot(pitch, file));
    body.velocity = {0.0f, 0.0f};
    body.movementTarget = body.position;
    body.tacticalTarget = body.position;
    body.facingAngle = -FACING_STAND;
    body.targetAngle = body.facingAngle;
  };
  for (MatchPlayer& player : players)
    if (active(player)) wait(player, player.isHomeTeam ? -1.0f : 1.0f);
  for (std::size_t index = 0; index < touchlineBodies.size(); ++index)
  {
    wait(touchlineBodies[index], isOfficial(touchlinePeople[index].role) ? 0.0f
                                 : touchlineBodies[index].isHomeTeam   ? -1.0f
                                                                       : 1.0f);
    if (index < touchlineStepStart.size())
      touchlineStepStart[index] = touchlineBodies[index].position;
  }
  beginWalkOut(true, true);
}

void MatchEngine::beginWalkOff()
{
  walk.phase = WalkPhase::OFF;
  walk.seconds = 0.0f;
  // A change under way at the whistle: made in the tunnel instead (one
  // already coming on is made).
  std::erase_if(pendingSubstitutions, [](const PendingSubstitution& pending)
                { return pending.stage == PendingSubstitution::Stage::ENTERING; });
  for (PendingSubstitution& pending : pendingSubstitutions)
    pending.stage = PendingSubstitution::Stage::WAITING;
  for (MatchPlayer& player : players)
  {
    player.facingRequested = false;
    player.intent = PlayerIntent::HOLD_SHAPE;
  }
  for (TouchlinePerson& person : touchlinePeople) person.seated = false;
}

std::pair<bool, std::optional<Vector2F>> MatchEngine::touchlineWalk(std::size_t index) const
{
  const MatchPlayer& body = touchlineBodies[index];
  const TouchlinePerson& person = touchlinePeople[index];
  const float file = isOfficial(person.role) ? 0.0f : body.isHomeTeam ? -1.0f : 1.0f;
  // Sent off: into the tunnel, and he stays in.
  if (person.role == TouchlineRole::SENT_OFF)
  {
    if (deepInTunnel(body.position)) return {true, std::nullopt};
    return {false, waitSpot(pitch, file)};
  }
  switch (walk.phase)
  {
    case WalkPhase::OFF:
      // In: once deep in the tunnel he stays there.
      if (deepInTunnel(body.position)) return {true, std::nullopt};
      return {false, waitSpot(pitch, file)};
    case WalkPhase::IN:
      return {true, std::nullopt};
    case WalkPhase::OUT:
    case WalkPhase::LINED_UP:
    case WalkPhase::HANDSHAKE:
    {
      const float release =
          index < walk.touchlineRelease.size() ? walk.touchlineRelease[index] : 0.0f;
      if (walk.phase == WalkPhase::OUT && walk.seconds < release) return {true, std::nullopt};
      // The officials to the line-up; everyone else to his usual place.
      if (walk.lineUp && isOfficial(person.role) && index < walk.touchlineSpot.size())
        return {false, walk.touchlineSpot[index]};
      return {false, std::nullopt};
    }
    case WalkPhase::NONE:
      break;
  }
  return {false, std::nullopt};
}

void MatchEngine::updateWalk(float dt)
{
  walk.seconds += dt;
  rememberStepStart();
  std::array<std::uint8_t, 32> batch{};
  std::size_t batched = 0;
  const bool lineUp = walk.lineUp;
  for (std::size_t slot = 0; slot < players.size() && slot < batch.size(); ++slot)
  {
    MatchPlayer& player = players[slot];
    if (!active(player)) continue;
    const Vector2F at = asVector(player.position);
    const float file = player.isHomeTeam ? -1.0f : 1.0f;
    Vector2F target = at;
    bool waits = false;
    bool facesStand = false;
    switch (walk.phase)
    {
      case WalkPhase::OFF:
        waits = deepInTunnel(player.position);
        target = waitSpot(pitch, file);
        break;
      case WalkPhase::IN:
        waits = true;
        break;
      case WalkPhase::OUT:
        waits = walk.seconds < walk.playerRelease[slot];
        target = lineUp ? walk.playerSpot[slot] : asVector(kickOffPosition(player));
        facesStand = lineUp && metresBetween(at, target) <= W::ARRIVED_METRES;
        break;
      case WalkPhase::LINED_UP:
        target = walk.playerSpot[slot];
        facesStand = true;
        break;
      case WalkPhase::HANDSHAKE:
      {
        target = walk.playerSpot[slot];
        facesStand = true;
        const auto walker = std::ranges::find(walk.walkers, slot);
        if (walker == walk.walkers.end()) break;
        const auto rank = static_cast<float>(walker - walk.walkers.begin());
        if (walk.seconds < rank * W::HANDSHAKE_START_SECONDS) break;
        facesStand = false;
        if (walk.through[slot])
        {
          target = asVector(kickOffPosition(player));
          break;
        }
        // Along the lane in front of the line, a hand at a time.
        const float laneY = walk.playerSpot[slot].y + W::HANDSHAKE_LANE_METRES;
        std::size_t& hand = walk.nextHand[slot];
        target = {walk.hands[hand], laneY};
        if (metresBetween(at, target) <= W::ARRIVED_METRES)
        {
          walk.handPause[slot] += dt;
          if (walk.handPause[slot] >= W::HANDSHAKE_PAUSE_SECONDS)
          {
            walk.handPause[slot] = 0.0f;
            if (++hand >= walk.hands.size()) walk.through[slot] = true;
          }
        }
        break;
      }
      case WalkPhase::NONE:
        break;
    }
    if (waits)
    {
      player.velocity = {0.0f, 0.0f};
      continue;
    }
    player.movementTarget = asPoint(routeViaTunnel(at, target));
    player.tacticalTarget = player.movementTarget;
    player.targetVelocity = {0.0f, 0.0f};
    player.routeWaypoints = 0;
    player.arriveAtSpeed = false;
    player.urgentMovement = false;
    player.facingRequested = facesStand;
    player.facingRequest = FACING_STAND;
    if (player.fallen)
      fallenBinturong(player, dt);
    else if (player.stumbleSeconds > 0.0f)
      stumbleBinturong(player, dt);
    else if (player.collisionPause > 0.0f)
      coastBinturong(player, dt);
    else
      batch[batched++] = static_cast<std::uint8_t>(slot);
  }
  movePlayers(batch.data(), batched, dt, true);
  updateTouchline(dt);
  collidePlayers(dt);

  // On to the next phase.
  const bool timedOut = walk.seconds >= W::PHASE_LIMIT_SECONDS;
  switch (walk.phase)
  {
    case WalkPhase::OFF:
    {
      bool allIn = std::ranges::all_of(players, [this](const MatchPlayer& player)
                                       { return !active(player) || deepInTunnel(player.position); });
      allIn = allIn && std::ranges::all_of(touchlineBodies, [this](const MatchPlayer& body)
                                           { return deepInTunnel(body.position); });
      if (allIn || timedOut)
      {
        walk.phase = WalkPhase::IN;
        walk.seconds = 0.0f;
      }
      break;
    }
    case WalkPhase::OUT:
    {
      if (walk.lineUp)
      {
        // Everyone on his line-up spot (the officials too).
        bool lined = true;
        for (std::size_t slot = 0; slot < players.size(); ++slot)
          if (active(players[slot]) &&
              metresBetween(asVector(players[slot].position), walk.playerSpot[slot]) >
                  W::ARRIVED_METRES)
            lined = false;
        for (std::size_t index = 0; index < touchlineBodies.size(); ++index)
          if (isOfficial(touchlinePeople[index].role) &&
              metresBetween(asVector(touchlineBodies[index].position), walk.touchlineSpot[index]) >
                  W::ARRIVED_METRES)
            lined = false;
        if (lined || timedOut)
        {
          walk.phase = WalkPhase::LINED_UP;
          walk.seconds = 0.0f;
        }
        break;
      }
      // Second half: on once every player is out through the boards.
      const bool out = std::ranges::all_of(
          players, [this](const MatchPlayer& player)
          { return player.position.y < pitch.ground.boardsY - 0.5f; });
      if (out || timedOut) finishWalkOut();
      break;
    }
    case WalkPhase::LINED_UP:
      if (walk.seconds >= W::LINED_UP_SECONDS)
      {
        walk.phase = WalkPhase::HANDSHAKE;
        walk.seconds = 0.0f;
      }
      break;
    case WalkPhase::HANDSHAKE:
    {
      const bool done = std::ranges::all_of(walk.walkers, [this](std::size_t slot)
                                            { return !active(players[slot]) || walk.through[slot]; });
      if (done || timedOut) finishWalkOut();
      break;
    }
    case WalkPhase::IN:
    case WalkPhase::NONE:
      break;
  }
}

// --- Substitutions on foot (stage 5) ----------------------------------------

bool MatchEngine::performSubstitution(MatchPlayer& outgoing, const Player* inPlayer,
                                      SubstitutionReason reason)
{
  // Without walk-outs, or in the tunnel at half-time: at once.
  const bool inTunnel = state == MatchState::HALF_TIME && walk.phase == WalkPhase::IN;
  if (!walksOut() || inTunnel)
  {
    MatchPlayer before = outgoing;
    const bool done = applySubstitution(outgoing, inPlayer, reason);
    if (done && walksOut()) benchSubstitutedPlayer(before);
    return done;
  }
  if (!inPlayer || !active(outgoing) || !canSubstitute(outgoing.isHomeTeam)) return false;
  const std::size_t slot = slotOf(outgoing);
  for (const PendingSubstitution& pending : pendingSubstitutions)
    if (pending.slot == slot || pending.incoming == inPlayer) return false;
  // He is no longer on the bench to be picked; the change is under way.
  std::erase(outgoing.isHomeTeam ? homeBench : awayBench, inPlayer);
  PendingSubstitution& pending = pendingSubstitutions.emplace_back();
  pending.slot = slot;
  pending.incoming = inPlayer;
  pending.reason = reason;
  pending.stage = PendingSubstitution::Stage::WAITING;
  updateSubstitutions(0.0f);
  return true;
}

Vector2F MatchEngine::substituteWaitSpot(bool home, const Player* incoming) const
{
  // On the touchline at halfway, his side of the fourth official, 1 m in
  // from the edge of his technical area (agreed). Several waiting (user):
  // a queue along the touchline away from halfway, in the order the
  // changes were made, closing up as each goes on.
  std::size_t place = 0;
  for (const PendingSubstitution& pending : pendingSubstitutions)
  {
    if (pending.incoming == incoming) break;
    if (pending.stage != PendingSubstitution::Stage::ENTERING &&
        pending.slot < players.size() && players[pending.slot].isHomeTeam == home)
      ++place;
  }
  const float along = W::SUB_QUEUE_SPACING_METRES * static_cast<float>(place);
  const float x = home ? pitch.ground.technicalAreas[0].x1 + W::SUB_FROM_AREA_METRES - along
                       : pitch.ground.technicalAreas[1].x0 - W::SUB_FROM_AREA_METRES + along;
  return {x, pitch.width + W::SUB_OUT_METRES};
}

const PendingSubstitution* MatchEngine::leavingFrom(std::size_t slot) const
{
  for (const PendingSubstitution& pending : pendingSubstitutions)
    if (pending.slot == slot && pending.stage == PendingSubstitution::Stage::LEAVING)
      return &pending;
  return nullptr;
}

const PendingSubstitution* MatchEngine::comingOn(const Player* player) const
{
  for (const PendingSubstitution& pending : pendingSubstitutions)
    if (pending.incoming == player && pending.stage != PendingSubstitution::Stage::ENTERING)
      return &pending;
  return nullptr;
}

bool MatchEngine::substitutionHoldsRestart() const
{
  // Any change under way at a stoppage: until its sub is on.
  return std::ranges::any_of(pendingSubstitutions, [this](const PendingSubstitution& pending)
                             { return pending.stage != PendingSubstitution::Stage::WAITING ||
                                      state != MatchState::PLAYING; });
}

void MatchEngine::updateSubstitutions(float dt)
{
  if (pendingSubstitutions.empty()) return;
  // Wholly over (or inside) the lines: his body's edge past them.
  const float edge = 0.5f * MatchTuning::Body::DIAMETER_METRES;
  const auto outside = [this, edge](Vector2F at)
  {
    return at.x < -edge || at.x > pitch.length + edge || at.y < -edge || at.y > pitch.width + edge;
  };
  const auto inside = [this, edge](Vector2F at)
  {
    return at.x > edge && at.x < pitch.length - edge && at.y > edge && at.y < pitch.width - edge;
  };
  for (std::size_t index = 0; index < pendingSubstitutions.size();)
  {
    PendingSubstitution& pending = pendingSubstitutions[index];
    MatchPlayer& player = players[pending.slot];
    const bool home = player.isHomeTeam;
    // Asked for in play and still waiting at half-time: made in the tunnel.
    if (pending.stage == PendingSubstitution::Stage::WAITING &&
        state == MatchState::HALF_TIME && walk.phase == WalkPhase::IN)
    {
      const PendingSubstitution made = pending;
      pendingSubstitutions.erase(pendingSubstitutions.begin() +
                                 static_cast<std::ptrdiff_t>(index));
      const MatchPlayer before = player;
      if (active(player) && applySubstitution(player, made.incoming, made.reason))
      {
        benchSubstitutedPlayer(before);
        if (made.moveToSlot) executeCommand(*made.moveToSlot);
      }
      else
      {
        (home ? homeBench : awayBench).push_back(made.incoming);
      }
      continue;
    }
    // Play has stopped: off he goes, at the nearest point of the boundary.
    if (pending.stage == PendingSubstitution::Stage::WAITING && state != MatchState::PLAYING &&
        state != MatchState::HALF_TIME && walk.phase == WalkPhase::NONE)
    {
      if (!active(player) || !canSubstitute(home))
      {
        // No longer possible (sent off meanwhile, or no window left): the
        // sub stays on the bench.
        (home ? homeBench : awayBench).push_back(pending.incoming);
        pendingSubstitutions.erase(pendingSubstitutions.begin() +
                                   static_cast<std::ptrdiff_t>(index));
        continue;
      }
      pending.exit = nearestExit(asVector(player.position));
      pending.stage = PendingSubstitution::Stage::LEAVING;
      if (deadBall.fetcherIndex == pending.slot) deadBall.fetcherIndex.reset();
      // He takes no restart: another does.
      if (restartTaker() == &player)
      {
        MatchPlayer* taker = nullptr;
        float best = std::numeric_limits<float>::max();
        for (MatchPlayer& other : players)
        {
          if (!active(other) || other.isHomeTeam != home || &other == &player ||
              leavingFrom(slotOf(other)))
            continue;
          const float gap = distance(other.position, ball.position);
          if (gap < best)
          {
            best = gap;
            taker = &other;
          }
        }
        setRestartTaker(taker);
        if (deadBall.active && (deadBall.phase == DeadBallReturn::Phase::CARRIED ||
                                deadBall.phase == DeadBallReturn::Phase::LIFTED))
        {
          // Out of his hands: it drops where he held it.
          deadBall.phase = DeadBallReturn::Phase::LOOSE;
          deadBall.seconds = 0.0f;
        }
        else if (getPossessionAttachment() == player.player)
        {
          clearPossession();
        }
      }
    }
    // Sent off on his way: the change lapses; the sub sits down again.
    if (pending.stage != PendingSubstitution::Stage::WAITING && !active(player))
    {
      if (pending.stage == PendingSubstitution::Stage::LEAVING)
        (home ? homeBench : awayBench).push_back(pending.incoming);
      pendingSubstitutions.erase(pendingSubstitutions.begin() +
                                 static_cast<std::ptrdiff_t>(index));
      continue;
    }
    // Over the line, the sub at his spot: the sub takes his slot (each
    // keeps his own body).
    MatchPlayer* sub = nullptr;
    for (MatchPlayer& body : touchlineBodies)
      if (body.player == pending.incoming) sub = &body;
    const Vector2F waitSpot = substituteWaitSpot(home, pending.incoming);
    // Safety net (user): a change holding up play over a minute is done at
    // once, the player going off put over the line and the sub on his spot.
    if (pending.stage != PendingSubstitution::Stage::WAITING) pending.seconds += dt;
    const bool overdue = pending.seconds >= W::SUBSTITUTION_LIMIT_SECONDS;
    const auto put = [this](MatchPlayer& body, PitchPoint at)
    {
      if (body.fallen) standUp(body);
      body.position = at;
      body.velocity = {0.0f, 0.0f};
      body.stumbleSeconds = 0.0f;
      body.collisionPause = 0.0f;
    };
    if (overdue && pending.stage == PendingSubstitution::Stage::LEAVING)
    {
      put(player, asPoint(pending.exit));
      if (sub) put(*sub, asPoint(waitSpot));
    }
    const bool subReady =
        !sub || std::hypot(sub->position.x - waitSpot.x,
                           sub->position.y - waitSpot.y) <= 1.0f;
    if (pending.stage == PendingSubstitution::Stage::LEAVING && subReady &&
        outside(asVector(player.position)))
    {
      const MatchPlayer leaving = player;
      pending.stage = PendingSubstitution::Stage::ENTERING;
      if (sub)
      {
        player.position = sub->position;
        player.velocity = sub->velocity;
        player.facingAngle = sub->facingAngle;
        player.targetAngle = sub->targetAngle;
        player.movementTarget = sub->position;
        player.tacticalTarget = sub->position;
        if (player.fallen) standUp(player);
        player.stumbleSeconds = 0.0f;
        player.collisionPause = 0.0f;
        const std::size_t slot = pending.slot;
        if (slot < previousPlayerPositions.size())
          previousPlayerPositions[slot] = player.position;
        if (slot < stepStartPositions.size()) stepStartPositions[slot] = player.position;
      }
      if (!applySubstitution(player, pending.incoming, pending.reason))
      {
        // Refused at the last (should not happen): he stays on.
        player = leaving;
        (home ? homeBench : awayBench).push_back(pending.incoming);
        pendingSubstitutions.erase(pendingSubstitutions.begin() +
                                   static_cast<std::ptrdiff_t>(index));
        continue;
      }
      benchSubstitutedPlayer(leaving);
      // On to the pitch: to his place in the shape (at a kick-off, his
      // kick-off position); a restart's own targets take over from there.
      player.movementTarget =
          state == MatchState::KICK_OFF ? kickOffPosition(player) : player.basePosition;
      player.tacticalTarget = player.movementTarget;
      PendingSubstitution& entering = pendingSubstitutions[index];
      if (entering.moveToSlot)
      {
        const MatchCommandRecord move = *entering.moveToSlot;
        entering.moveToSlot.reset();
        executeCommand(move);
      }
    }
    // Overdue coming on: put on his place.
    if (pendingSubstitutions[index].stage == PendingSubstitution::Stage::ENTERING &&
        pendingSubstitutions[index].seconds >= W::SUBSTITUTION_LIMIT_SECONDS)
    {
      MatchPlayer& coming = players[pendingSubstitutions[index].slot];
      put(coming, clampToPitch(coming.movementTarget));
    }
    // On the pitch: the change is made.
    if (pendingSubstitutions[index].stage == PendingSubstitution::Stage::ENTERING &&
        inside(asVector(players[pendingSubstitutions[index].slot].position)))
    {
      pendingSubstitutions.erase(pendingSubstitutions.begin() +
                                 static_cast<std::ptrdiff_t>(index));
      continue;
    }
    ++index;
  }
}

void MatchEngine::benchSubstitutedPlayer(const MatchPlayer& body)
{
  using T = MatchTuning::TouchlinePeople;
  const bool home = body.isHomeTeam;
  const MatchGround::Rect& dugout = pitch.ground.dugouts[home ? 0 : 1];
  // The first free seat from the dugout's halfway end.
  const float end = home ? dugout.x1 : dugout.x0;
  const float inward = home ? -1.0f : 1.0f;
  float seatX = end + inward * T::FIRST_SEAT_METRES;
  for (int seat = 0; seat < 64; ++seat)
  {
    seatX = end + inward * (T::FIRST_SEAT_METRES + T::SEAT_SPACING_METRES * static_cast<float>(seat));
    bool taken = false;
    for (std::size_t index = 0; index < touchlinePeople.size(); ++index)
      if (touchlineBodies[index].isHomeTeam == home &&
          touchlinePeople[index].role != TouchlineRole::MANAGER &&
          !isOfficial(touchlinePeople[index].role) &&
          std::abs(touchlinePeople[index].seat.x - seatX) < 0.1f)
        taken = true;
    if (!taken) break;
  }
  MatchPlayer& bench = touchlineBodies.emplace_back(body);
  bench.onPitch = false;
  bench.isGoalkeeper = false;
  bench.intent = PlayerIntent::HOLD_SHAPE;
  bench.facingRequested = false;
  TouchlinePerson& person = touchlinePeople.emplace_back();
  person.role = TouchlineRole::SUBSTITUTE;
  person.seat = asPoint({seatX, pitch.ground.standY - MatchTuning::Body::RADIUS_METRES});
  person.seated = false;
  // Off at the boundary: round the pitch's edge to his technical area. In
  // the tunnel (a half-time change): out with the staff.
  person.walkingRound = !deepInTunnel(body.position);
  previousTouchlinePositions.push_back(body.position);
  previousTouchlineFacingAngles.push_back(body.facingAngle);
  touchlineStepStart.push_back(body.position);
}

// --- Sendings-off on foot (stage 6) ------------------------------------------

void MatchEngine::sendToTunnel(const MatchPlayer& body)
{
  // He walks from where he is, as he is moving: off at the nearest point of
  // the boundary (`seat`), then round the pitch's edge to the tunnel.
  MatchPlayer& off = touchlineBodies.emplace_back(body);
  off.onPitch = false;
  off.isGoalkeeper = false;
  off.isPressing = false;
  off.isMakingRun = false;
  off.facingRequested = false;
  off.intent = PlayerIntent::HOLD_SHAPE;
  TouchlinePerson& person = touchlinePeople.emplace_back();
  person.role = TouchlineRole::SENT_OFF;
  person.seat = asPoint(nearestExit(asVector(body.position)));
  person.walkingRound = true;
  previousTouchlinePositions.push_back(body.position);
  previousTouchlineFacingAngles.push_back(body.facingAngle);
  touchlineStepStart.push_back(body.position);
}

bool MatchEngine::sentOffStillOn() const
{
  // Until his body is wholly over a line.
  const float edge = MatchTuning::Body::RADIUS_METRES;
  for (std::size_t index = 0; index < touchlinePeople.size(); ++index)
  {
    if (touchlinePeople[index].role != TouchlineRole::SENT_OFF) continue;
    const Vector2F at = asVector(touchlineBodies[index].position);
    if (at.x > -edge && at.x < pitch.length + edge && at.y > -edge && at.y < pitch.width + edge)
      return true;
  }
  return false;
}

void MatchEngine::finishWalkOut()
{
  walk.phase = WalkPhase::NONE;
  walk.seconds = 0.0f;
  for (MatchPlayer& player : players) player.facingRequested = false;
  // As after a goal: everyone to his kick-off position, the taker to the
  // ball, the kick-off once all are there (kickOffReadyBinturong).
  kickOffAfterGoalBinturong(walk.homeKicksOff);
}

