// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Binturong referee: the officials' judgement - fouls (sanction,
// advantage, free kick or penalty, a denied goal chance), advantage ending,
// offside calls and added time - the version being developed (see
// match_referee.h). It started as an exact copy of the Aardvark referee
// (match_referee_aardvark.cpp). Every function here is a MatchEngine member
// with the "Binturong" suffix, declared in match_referee_binturong_members.h;
// the engine reaches them through the dispatchers in
// match_referee_dispatch.cpp.
//
// Change behaviour freely here. A helper shared with the rest of the engine
// (the offside line and position, MatchRules::decideFoulSanction,
// MatchRules::computeAddedMinutes, what a card does) is shared by every
// referee: copy it into this referee (with the Binturong suffix) before
// changing it. To keep this version for comparison, freeze it and copy it
// to a new version the same way.

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/match_rules.h"
#include "model/player.h"

using namespace MatchEngineInternal;

bool MatchEngine::deniesGoalChanceBinturong(const MatchPlayer& victim,
                                            const MatchPlayer& offender) const
{
  // Only the player in control of the ball can be denied an obvious chance.
  if (getPossessionAttachment() != victim.player) return false;
  const float goalX = victim.isHomeTeam ? 1.0f : 0.0f;
  const PitchPoint goal = pitch.at(goalX, MatchTuning::Pitch::CENTRE);
  if (distance(victim.position, goal) >
          MatchTuning::Discipline::DOGSO_MAX_DISTANCE_METRES ||
      std::abs(victim.position.y - pitch.atY(MatchTuning::Pitch::CENTRE)) >
          MatchTuning::Discipline::DOGSO_MAX_WIDTH_METRES)
  {
    return false;
  }
  // Obvious chance: no covering outfield defender between victim and goal.
  const float direction = victim.isHomeTeam ? 1.0f : -1.0f;
  for (const auto& other : players)
  {
    if (!active(other) || other.isHomeTeam == victim.isHomeTeam ||
        other.isGoalkeeper || &other == &offender)
    {
      continue;
    }
    if ((other.position.x - victim.position.x) * direction > 0.0f) return false;
  }
  return true;
}

void MatchEngine::commitFoulBinturong(MatchPlayer& offender,
                                      MatchPlayer& victim, bool ballWon,
                                      bool reckless)
{
  if (offender.isHomeTeam)
    ++stats.homeFouls;
  else
    ++stats.awayFouls;
  ++statsOf(offender).foulsCommitted;
  ++statsOf(victim).foulsSuffered;

  const PitchPoint foulPosition = victim.position;
  const bool inBox = inPenaltyArea(foulPosition, offender.isHomeTeam);
  const float carrierDepth =
      victim.isHomeTeam ? pitch.fractionX(victim.position)
                        : 1.0f - pitch.fractionX(victim.position);
  const TeamPhase victimPhase = victim.isHomeTeam ? homePhase : awayPhase;
  const bool promisingAttack =
      carrierDepth > MatchTuning::Pitch::CENTRE &&
      (victimPhase == TeamPhase::ATTACKING_TRANSITION ||
       victimPhase == TeamPhase::FINAL_THIRD ||
       openSpaceAhead(victim) >
           MatchTuning::Discipline::ADVANTAGE_MIN_OPENNESS);

  MatchRules::FoulContext context;
  context.severityRoll =
      reckless ? std::max(incidentRoll(), 0.9f) : incidentRoll();
  context.cardRoll = incidentRoll();
  context.strictness = refereeStrictness;
  context.tactical =
      promisingAttack &&
      (victimPhase == TeamPhase::ATTACKING_TRANSITION || !ballWon);
  context.denyingGoalChance = deniesGoalChanceBinturong(victim, offender);
  context.inPenaltyArea = inBox;
  context.offenderAlreadyBooked = offender.yellowCards > 0;
  context.offenderIsAway = !offender.isHomeTeam;
  context.venueBiasScale = matchContext.homeAdvantageScale;
  const MatchRules::FoulSanction sanction =
      MatchRules::decideFoulSanction(context);

  MatchEvent& foulEvent = logEvent(MatchEventType::FOUL, offender);
  foulEvent.secondaryPlayerId = victim.player ? victim.player->getId() : 0;
  foulEvent.position = foulPosition;

  // Advantage: the fouled side kept the ball in a promising attack outside
  // the box, so play continues unless the attack breaks down quickly.
  const bool victimKeepsBall = getPossessionAttachment() == victim.player;
  const bool playAdvantage =
      !ballWon && victimKeepsBall && !inBox && promisingAttack &&
      sanction != MatchRules::FoulSanction::RED &&
      incidentRoll() < MatchTuning::Discipline::ADVANTAGE_PLAY_CHANCE;
  const bool contactInjury =
      incidentRoll() <
      (context.severityRoll >= 1.0f - MatchTuning::Discipline::RECKLESS_SHARE
           ? MatchTuning::Injury::RECKLESS_CONTACT_INJURY_CHANCE
           : MatchTuning::Injury::CONTACT_INJURY_CHANCE);

  applySanction(offender, sanction);
  if (state == MatchState::FULL_TIME) return;

  if (playAdvantage && !contactInjury)
  {
    if (victim.isHomeTeam)
      ++stats.homeAdvantagesPlayed;
    else
      ++stats.awayAdvantagesPlayed;
    MatchEvent& advantage = logEvent(MatchEventType::ADVANTAGE, victim);
    advantage.position = foulPosition;
    pendingAdvantage = {true, victim.isHomeTeam, foulPosition,
                        MatchTuning::Discipline::ADVANTAGE_WINDOW_SECONDS};
    return;
  }

  if (contactInjury && victim.onPitch) injurePlayer(victim, true);
  if (inBox)
    setupPenalty(victim.isHomeTeam);
  else
    setupFreeKick(victim.isHomeTeam, foulPosition);
  if (sanction != MatchRules::FoulSanction::NONE)
    extendRestart(MatchTuning::Timing::CARD_DELAY_SECONDS);
}

void MatchEngine::updatePendingAdvantageBinturong(float dt)
{
  if (!pendingAdvantage.active) return;
  const MatchPlayer* carrier = findMatchPlayer(getPossessionAttachment());
  if (carrier && carrier->isHomeTeam != pendingAdvantage.fouledTeamHome)
  {
    // The advantage did not materialise: back for the original free kick.
    pendingAdvantage.active = false;
    setupFreeKick(pendingAdvantage.fouledTeamHome, pendingAdvantage.position);
    return;
  }
  pendingAdvantage.secondsRemaining -= dt;
  if (pendingAdvantage.secondsRemaining <= 0.0f)
    pendingAdvantage.active = false;
}

bool MatchEngine::judgeOffsideBinturong(const MatchPlayer& passer,
                                        const PassOption& option,
                                        float defenderLine) const
{
  const MatchPlayer& receiver = *option.receiver;
  bool offside =
      state != MatchState::KICK_OFF && state != MatchState::THROW_IN &&
      state != MatchState::GOAL_KICK && state != MatchState::CORNER_KICK &&
      isOffside(receiver, passer.isHomeTeam, defenderLine);
  if (drillRules.offside && !offside && state == MatchState::PLAYING &&
      option.progression > 0.0f &&
      (receiver.isMakingRun || isAttackingRole(receiver.player->getRole())))
  {
    // A runner level with the line at the moment of the pass has often
    // timed his run a fraction early: the tighter, the likelier.
    const float gap = (defenderLine - receiver.position.x) *
                      (passer.isHomeTeam ? 1.0f : -1.0f);
    const bool opponentHalf =
        passer.isHomeTeam
            ? receiver.position.x > pitch.atX(MatchTuning::Pitch::CENTRE)
            : receiver.position.x < pitch.atX(MatchTuning::Pitch::CENTRE);
    if (opponentHalf && gap >= 0.0f &&
        gap < MatchTuning::Passing::OFFSIDE_TIMING_WINDOW_METRES)
    {
      const float chance =
          MatchTuning::Passing::OFFSIDE_TIMING_CHANCE *
          (1.0f - gap / MatchTuning::Passing::OFFSIDE_TIMING_WINDOW_METRES);
      offside =
          (hashNoise(receiver.player->getId(), 0x5eedU) + 1.0f) * 0.5f < chance;
    }
  }
  return offside;
}

int MatchEngine::addedMinutesDueBinturong() const
{
  const auto half = static_cast<std::size_t>(period - 1);
  return MatchRules::computeAddedMinutes(stoppageLogs[half], period);
}
