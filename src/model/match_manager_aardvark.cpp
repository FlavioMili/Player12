// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Aardvark AI manager: a managed side's touchline as inherited with the
// engine - substitutions (injuries, medical limits, fatigue, cards, the
// score), who comes on, and the late shouts. Kept frozen so the managers
// being developed can be compared with it (see match_manager.h). Every
// function here is a MatchEngine member with the "Aardvark" suffix, declared
// in match_manager_aardvark_members.h; the engine reaches them through
// match_manager_dispatch.cpp.
//
// Do not change behaviour here.

#include <algorithm>
#include <array>
#include <limits>

#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/medical_centre.h"
#include "model/player.h"
#include "model/role_utils.h"

using namespace MatchEngineInternal;

void MatchEngine::runAiSubstitutionsForAardvark(bool homeTeam)
{
  // Injured players leave at the first stoppage: replaced when possible, or
  // the team continues a player short.
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam || !player.isInjured)
    {
      continue;
    }
    const bool autoManaged =
        homeTeam ? homeAutoSubstitutions : awayAutoSubstitutions;
    if (!autoManaged)
    {
      continue;
    }
    // Treatment and the walk off hold up the restart.
    extendRestart(MatchTuning::Timing::INJURY_DELAY_SECONDS);
    const Player* replacement =
        canSubstitute(homeTeam)
            ? chooseReplacementAardvark(homeTeam, player.player->getRole(),
                                        player.isGoalkeeper)
            : nullptr;
    if ((replacement == nullptr) && player.isGoalkeeper &&
        canSubstitute(homeTeam))
    {
      replacement =
          chooseReplacementAardvark(homeTeam, player.player->getRole(), false);
    }
    if ((replacement == nullptr) ||
        !performSubstitution(player, replacement, SubstitutionReason::INJURY))
    {
      removeFromPitch(player);
    }
  }

  if (!medicalFlags.empty())
  {
    runMedicalSubstitutionsAardvark(homeTeam);
  }
  if (!(homeTeam ? homeAutoSubstitutions : awayAutoSubstitutions) ||
      state == MatchState::PENALTY || period < 2 ||
      matchTimeMinutes < MatchTuning::Substitution::EARLIEST_TACTICAL_MINUTE)
  {
    return;
  }

  const int goalDifference =
      homeTeam ? homeScore - awayScore : awayScore - homeScore;
  const float minute = matchTimeMinutes;
  const float fatigueThreshold =
      MatchTuning::Substitution::FATIGUE_THRESHOLD +
      (minute >= MatchTuning::Substitution::LATE_GAME_MINUTE
           ? MatchTuning::Substitution::FATIGUE_THRESHOLD_LATE_GAIN
           : 0.0F);
  struct Need
  {
    MatchPlayer* player = nullptr;
    float score = 0.0F;
    SubstitutionReason reason = SubstitutionReason::FATIGUE;
    int wantedGroup = 2;
  };
  std::array<Need, 3> picks{};
  const float minimumNeed =
      MatchTuning::Substitution::MINIMUM_NEED *
      (minute >= MatchTuning::Substitution::LATE_GAME_MINUTE
           ? MatchTuning::Substitution::LATE_NEED_SHARE
           : 1.0F);
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam || player.isGoalkeeper)
    {
      continue;
    }
    const PlayerRole role = player.player->getRole();
    Need need{.player = &player,
              .score = 0.0F,
              .reason = SubstitutionReason::FATIGUE,
              .wantedGroup = roleGroup(role)};
    need.score =
        std::max(0.0F, fatigueThreshold - player.stamina) *
            MatchTuning::Substitution::FATIGUE_NEED_SCALE *
            (isAttackingRole(role)
                 ? MatchTuning::Substitution::ATTACKER_FATIGUE_NEED_FACTOR
                 : 1.0F) +
        (minute - MatchTuning::Substitution::EARLIEST_TACTICAL_MINUTE) *
            MatchTuning::Substitution::MINUTE_NEED_GAIN *
            (1.0F - player.stamina);
    if (isAttackingRole(role) &&
        minute >= MatchTuning::Substitution::ATTACKER_ROTATION_MINUTE)
    {
      need.score +=
          (minute - MatchTuning::Substitution::ATTACKER_ROTATION_MINUTE) *
          MatchTuning::Substitution::ATTACKER_ROTATION_NEED_PER_MINUTE *
          (1.0F - player.stamina);
    }
    if (player.yellowCards > 0 && !isAttackingRole(role) &&
        minute >= MatchTuning::Substitution::CARD_RISK_MINUTE)
    {
      need.score += MatchTuning::Substitution::CARD_RISK_NEED;
      need.reason = SubstitutionReason::CARD_RISK;
    }
    if (goalDifference < 0 && isDefensiveRole(role) &&
        minute >= MatchTuning::Substitution::TRAILING_CHASE_MINUTE)
    {
      need.score += MatchTuning::Substitution::TACTICAL_NEED;
      need.reason = SubstitutionReason::TACTICAL;
      need.wantedGroup = 3;
    }
    else if (goalDifference > 0 && isAttackingRole(role) &&
             minute >= MatchTuning::Substitution::LEADING_PROTECT_MINUTE)
    {
      need.score += MatchTuning::Substitution::TACTICAL_NEED;
      need.reason = SubstitutionReason::TACTICAL;
      need.wantedGroup = 1;
    }
    if (need.score < minimumNeed * MatchTuning::Substitution::WINDOW_NEED_SHARE)
    {
      continue;
    }
    for (std::size_t slot = 0; slot < picks.size(); ++slot)
    {
      if (need.score <= picks[slot].score)
      {
        continue;
      }
      for (std::size_t shifted = picks.size() - 1; shifted > slot; --shifted)
      {
        picks[shifted] = picks[shifted - 1];
      }
      picks[slot] = need;
      break;
    }
  }

  // Keep one change in reserve for late injuries until the final minutes.
  const int reserve =
      minute < MatchTuning::Substitution::LATE_GAME_MINUTE ? 1 : 0;
  // The clearest need opens a window; a second or third change joins it at
  // a lower need, as managers make double changes to save windows.
  if (picks[0].score < minimumNeed)
  {
    return;
  }
  for (const Need& need : picks)
  {
    if ((need.player == nullptr) || !canSubstitute(homeTeam))
    {
      break;
    }
    if (getSubstitutionsUsed(homeTeam) >= maxSubstitutions() - reserve)
    {
      break;
    }
    const PlayerRole wantedRole = need.wantedGroup == 3 ? PlayerRole::ST
                                  : need.wantedGroup == 1
                                      ? PlayerRole::CB
                                      : need.player->player->getRole();
    const Player* replacement =
        chooseReplacementAardvark(homeTeam, wantedRole, false);
    if (replacement == nullptr)
    {
      break;
    }
    performSubstitution(*need.player, replacement, need.reason);
  }
}

void MatchEngine::runMedicalSubstitutionsAardvark(bool homeTeam)
{
  if (!(homeTeam ? homeAutoSubstitutions : awayAutoSubstitutions) ||
      state == MatchState::PENALTY)
  {
    return;
  }
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam || player.isGoalkeeper)
    {
      continue;
    }
    const PlayerID id = player.player->getId();
    const auto entry =
        std::ranges::find_if(medicalFlags, [id](const auto& flagged)
                             { return flagged.first == id; });
    // The limit counts his own minutes on the pitch: a flagged substitute
    // starts from nothing when he comes on.
    if (entry == medicalFlags.end() ||
        !MedicalCentre::substitutionDue(
            entry->second, static_cast<int>(statsOf(player).minutesPlayed)))
    {
      continue;
    }
    if (!canSubstitute(homeTeam))
    {
      return;
    }
    // Like for like, as for a tired player.
    if (const Player* replacement = chooseReplacementAardvark(
            homeTeam, player.player->getRole(), false))
    {
      performSubstitution(player, replacement, SubstitutionReason::FATIGUE);
    }
  }
}

const Player* MatchEngine::chooseReplacementAardvark(bool homeTeam,
                                                     PlayerRole role,
                                                     bool wantGoalkeeper) const
{
  const auto& bench = homeTeam ? homeBench : awayBench;
  const Player* best = nullptr;
  float bestScore = -std::numeric_limits<float>::infinity();
  for (const Player* candidate : bench)
  {
    if (candidate == nullptr)
    {
      continue;
    }
    const bool keeper = candidate->getRole() == PlayerRole::GK;
    if (keeper != wantGoalkeeper)
    {
      continue;
    }
    const float fit = roleGroup(candidate->getRole()) == roleGroup(role)
                          ? MatchTuning::Substitution::ROLE_FIT_BONUS
                          : 0.0F;
    const float score =
        fit + (static_cast<float>(candidate->getOverall(statsConfig)) /
               MatchTuning::Player::RATING_SCALE);
    if (score > bestScore)
    {
      bestScore = score;
      best = candidate;
    }
  }
  return best;
}

void MatchEngine::runAiTouchlineAardvark(bool homeTeam)
{
  using T = MatchTuning::Touchline;
  if (period < 2 || getShoutStrength(homeTeam) > 0.0F)
  {
    return;
  }
  const int lead = homeTeam ? homeScore - awayScore : awayScore - homeScore;
  if (lead < 0 && matchTimeMinutes >= T::AI_SHOOT_ON_SIGHT_MINUTE)
  {
    startShout(homeTeam, MatchShout::SHOOT_ON_SIGHT);
  }
  else if (lead < 0 && matchTimeMinutes >= T::AI_CHASE_MINUTE)
  {
    startShout(homeTeam, MatchShout::PUSH_HIGHER);
  }
  else if (lead > 0 && matchTimeMinutes >= T::AI_PROTECT_MINUTE)
  {
    startShout(homeTeam, MatchShout::DROP_DEEPER);
  }
}
