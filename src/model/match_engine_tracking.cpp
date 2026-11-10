// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The match engine's side of the analytics tracker: small hooks called from
// the simulation, kept apart from the engine's core so the recording stays
// easy to audit and costs nothing when the tracker is off.

#include <cmath>

#include "model/match_engine.h"

namespace
{
float metresBetween(Vector2F first, Vector2F second)
{
  const float dx = (first.x - second.x) * MatchTuning::Pitch::LENGTH_METRES;
  const float dy = (first.y - second.y) * MatchTuning::Pitch::WIDTH_METRES;
  return std::sqrt(dx * dx + dy * dy);
}
}  // namespace

void MatchEngine::trackTouch(const MatchPlayer& player)
{
  if (!tracker.isEnabled() || player.player == nullptr) return;
  tracker.touch(player.statsIndex, player.isHomeTeam, player.position,
                matchTimeMinutes, period);
}

void MatchEngine::trackPassRelease(const MatchPlayer& passer)
{
  if (!tracker.isEnabled()) return;
  tracker.passReleased(passer.position, state == MatchState::PLAYING);
}

void MatchEngine::trackPassCompletion(const MatchPlayer& passer,
                                      const MatchPlayer& receiver)
{
  if (!tracker.isEnabled()) return;
  tracker.passCompleted(passer.statsIndex, receiver.statsIndex,
                        passer.isHomeTeam, receiver.position);
}

void MatchEngine::trackShot(const MatchPlayer& shooter, float xg, bool setPiece,
                            bool header, bool penalty)
{
  if (!tracker.isEnabled()) return;
  const MatchPlayer* creator = findMatchPlayer(getPossessionHistory().shotAssistCandidate);
  const bool credited = creator != nullptr && creator != &shooter &&
                        creator->isHomeTeam == shooter.isHomeTeam;
  tracker.shot(credited ? creator->statsIndex : MatchTracking::MAX_PLAYERS, xg,
               {setPiece, header, penalty});
}

void MatchEngine::trackPressures()
{
  if (!tracker.isEnabled() || state != MatchState::PLAYING) return;
  const MatchPlayer* carrier = findMatchPlayer(getPossessionAttachment());
  if (carrier == nullptr) return;
  // The closest pressing opponent applies the pressure.
  const MatchPlayer* presser = nullptr;
  float closest = MatchTracking::PRESSURE_RADIUS_METRES;
  for (const MatchPlayer& player : players)
  {
    if (!player.isPressing || !player.onPitch || player.player == nullptr ||
        player.isHomeTeam == carrier->isHomeTeam)
      continue;
    const float metres = metresBetween(player.position, carrier->position);
    if (metres <= closest)
    {
      closest = metres;
      presser = &player;
    }
  }
  if (presser != nullptr)
    tracker.pressure(presser->statsIndex, getPossessionHistory().possessionStartSeconds);
}
