// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <vector>

#include "global/types.h"
#include "model/match_point.h"
#include "model/match_tuning.h"

/**
 * Placement of a single player within a deterministic match scenario.
 */
struct MatchScenarioPlayer
{
  std::uint32_t playerId = 0;
  PitchPoint position{MatchTuning::Pitch::MATCH_CENTRE_X_METRES,
                      MatchTuning::Pitch::MATCH_CENTRE_Y_METRES};
  bool makingRun = false;
};

/**
 * A fully-specified, deterministic snapshot of players and ball that the match
 * engine can load. Used by the headless scenario suite to exercise pass, shot,
 * and carry decisions without organic full-match states. The renderer never
 * sees this path; it only consumes the read-only MatchRenderSnapshot.
 */
struct MatchScenario
{
  std::vector<MatchScenarioPlayer> players;
  std::uint32_t carrierId = 0;
  PitchPoint ballPosition{MatchTuning::Pitch::MATCH_CENTRE_X_METRES,
                          MatchTuning::Pitch::MATCH_CENTRE_Y_METRES};
  /** Drills: the carrier, a keeper in his area, starts with it in his
   * hands (Binturong body; goalkeeper plan step 5). */
  bool carrierInHands = false;
};