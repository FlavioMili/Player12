// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "global/types.h"
#include "model/lineup.h"
#include "model/match_ball_physics.h"
#include "model/match_body.h"
#include "model/match_brain.h"
#include "model/match_handshake.h"
#include "model/match_manager.h"
#include "model/match_out_of_play.h"
#include "model/match_possession.h"
#include "model/match_referee.h"
#include "model/strategy.h"

class GameController;
class MatchEngine;

/**
 * The brains (home, away) new sandbox matches, checks and drills start with:
 * the sandbox's --brains option, else Aardvark for both sides (the window
 * opens on Binturong v Aardvark instead; see match_sandbox_main.cpp).
 */
std::array<BrainId, 2>& sandboxBrains();
/** The same for bodies: the --bodies option, else Aardvark for both (the
 * window: Binturong v Aardvark). */
std::array<BodyId, 2>& sandboxBodies();
/** The handshake new sandbox matches, checks and drills are built with:
 * the --handshake option, else Binturong (--check: Aardvark). */
HandshakeId& sandboxHandshake();
/** The referee, AI managers (home, away), ball physics and out-of-play
 * version new sandbox matches, checks and drills are played with: the
 * --referee, --managers, --ball and --out-of-play options, else Aardvark
 * (the ball: Binturong). */
RefereeId& sandboxReferee();
std::array<ManagerId, 2>& sandboxManagers();
BallPhysicsId& sandboxBallPhysics();
OutOfPlayId& sandboxOutOfPlay();
/** Independent per-match possession; new sandbox matches default Binturong,
 * --check defaults Aardvark unless explicitly selected. */
PossessionId& sandboxPossession();
/** The home advantage new sandbox matches, checks and drills are played
 * with (MatchContext::homeAdvantageScale: 0 a neutral venue, 1 the game's
 * full home edge on attributes, execution and refereeing, up to 2): the
 * --home-advantage option, else 0 (--check: 1, to compare with the
 * inherited engine). */
float& sandboxHomeAdvantage();
/** Gives a match not yet kicked off this home advantage. At 1 (the engine's
 * own default) it leaves the match untouched, random draws included. */
void applyHomeAdvantage(MatchEngine& engine, float scale);

/**
 * Everything a sandbox match is built from (debugger phase 5): both sides'
 * selection and tactics, the seed, tactical familiarity, the players'
 * starting condition, medical limits and the substitution policy. The live
 * match and every replay of its recording are built from it by build(), so
 * they start identically by construction.
 */
struct MatchSetup
{
  /** One side: who plays where, the bench, set-piece takers, tactics. */
  struct Side
  {
    TeamID team = 0;
    std::string name;
    PlayerID goalkeeper = 0;
    std::vector<std::pair<PlayerID, Vector2F>> outfield;
    std::vector<PlayerID> reserves;
    SetPieceDesignations designations{};
    Strategy strategy;
  };

  /** World the players come from (the sandbox's --world-seed). */
  std::uint64_t worldSeed = 0;
  std::uint32_t matchSeed = 0;
  std::array<Side, 2> sides;
  std::array<float, 2> familiarity{1.0f, 1.0f};
  /** Starting condition (0-1) of every player in both matchday squads. */
  std::vector<std::pair<PlayerID, float>> conditions;
  /** Medical staff minute limits of the managed (home) side. */
  std::vector<std::pair<PlayerID, std::uint8_t>> medicalFlags;
  /** Whether the AI makes each side's substitutions. */
  std::array<bool, 2> autoSubstitutions{false, true};
  /** Each side's brain (recordings made before brains existed: Aardvark). */
  std::array<BrainId, 2> brains{BrainId::AARDVARK, BrainId::AARDVARK};
  /** Each side's body (recordings made before bodies existed: Aardvark). */
  std::array<BodyId, 2> bodies{BodyId::AARDVARK, BodyId::AARDVARK};
  /** The match's handshake (older recordings: Aardvark). */
  HandshakeId handshake = HandshakeId::AARDVARK;
  /** The match's referee (older recordings: Aardvark). */
  RefereeId referee = RefereeId::AARDVARK;
  /** Each side's AI manager (older recordings: Aardvark). */
  std::array<ManagerId, 2> managers{ManagerId::AARDVARK, ManagerId::AARDVARK};
  /** The match's ball physics (older recordings: Aardvark). */
  BallPhysicsId ballPhysics = BallPhysicsId::AARDVARK;
  /** The match's out-of-play version (older recordings: Aardvark). */
  OutOfPlayId outOfPlay = OutOfPlayId::AARDVARK;
  /** Missing in old setups/recordings: legacy possession, independent of bodies. */
  PossessionId possession = PossessionId::AARDVARK;
  /** The match's home advantage (older recordings: 1, the full edge). */
  float homeAdvantage = 1.0f;

  /**
   * The match about to be played between two clubs of the current world,
   * the home side managed. `fullFamiliarity` sets both sides to 1.
   */
  [[nodiscard]] static MatchSetup capture(const GameController& controller,
                                          TeamID home, TeamID away,
                                          std::uint32_t seed,
                                          bool fullFamiliarity);

  /** Builds the engine; null (and `error`) when a player is unknown. */
  [[nodiscard]] std::unique_ptr<MatchEngine> build(
      const GameController& controller, std::string& error) const;
};

/** JSON form of a setup and of the records a recording holds. */
namespace SandboxJson
{
nlohmann::json toJson(const MatchSetup& setup);
bool fromJson(const nlohmann::json& json, MatchSetup& setup,
              std::string& error);
nlohmann::json toJson(const Strategy& strategy);
Strategy strategyFromJson(const nlohmann::json& json);
}  // namespace SandboxJson
