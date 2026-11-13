// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Binturong handshake: how the game's inputs become the match engine's
// state, the version being developed - the attribute scale (level shift
// and stretch), physical limits, the home lift, slot roles, base positions,
// the match context, referee strictness, condition, familiarity and team
// talks. It started as an exact copy of the Aardvark handshake
// (match_handshake_aardvark.cpp; see match_handshake.h). Every function here
// is a MatchEngine member with the "Binturong" suffix, declared in
// match_handshake_binturong_members.h; the engine reaches them through
// match_handshake_dispatch.cpp.
//
// Change behaviour freely here. To keep this version for comparison, freeze
// it and copy it to a new version.
//
// Changes from the Aardvark:
// - attributeBinturong: attributes are the stat / 100 (clamped to
//   MIN_ATTRIBUTE..1), with no stretch and no level shift.
// - computeLevelBinturong: levelShift 0 and levelPrecision 1: the match's
//   level changes nothing.

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <string_view>

#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/match_rules.h"
#include "model/player.h"

using namespace MatchEngineInternal;

float MatchEngine::drawRefereeStrictnessBinturong() const
{
  using D = MatchTuning::Discipline;
  const float mean = matchContext.refereeStrictnessMean;
  return std::clamp(mean + refereeDraw * matchContext.refereeStrictnessSd,
                    D::MIN_STRICTNESS * mean, D::MAX_STRICTNESS * mean);
}

void MatchEngine::convertMatchContextBinturong(const MatchContext& context)
{
  // Out-of-range or missing values fall back to the neutral defaults.
  const auto finite = [](float value, float fallback)
  { return std::isfinite(value) ? value : fallback; };
  const MatchContext defaults;
  matchContext.goalRateScale =
      std::clamp(finite(context.goalRateScale, 1.0f),
                 MatchTuning::Context::MIN_GOAL_RATE_SCALE,
                 MatchTuning::Context::MAX_GOAL_RATE_SCALE);
  matchContext.refereeStrictnessMean =
      std::clamp(finite(context.refereeStrictnessMean, 1.0f),
                 MatchTuning::Context::MIN_REFEREE_STRICTNESS,
                 MatchTuning::Context::MAX_REFEREE_STRICTNESS);
  matchContext.refereeStrictnessSd = std::clamp(
      finite(context.refereeStrictnessSd, defaults.refereeStrictnessSd), 0.0f,
      MatchTuning::Context::MAX_REFEREE_SD);
  matchContext.homeAdvantageScale =
      std::clamp(finite(context.homeAdvantageScale, 1.0f), 0.0f,
                 MatchTuning::Context::MAX_HOME_ADVANTAGE_SCALE);
  finishingPrecision = std::pow(matchContext.goalRateScale,
                                MatchTuning::Context::FINISHING_EXPONENT);
}

const DerivedRecipes& MatchEngine::derivedRecipesBinturong()
{
  // Binturong's own derived-stat recipes (match_derived_stats.h); the defaults
  // for now. Future game stats (agility, first touch, acceleration) stand in
  // for stats the game does not carry yet: when it does, load them in
  // loadAttributesBinturong and drop their recipes here; the
  // match-engine-only stats built on them need no change.
  static constexpr DerivedRecipes recipes = DEFAULT_DERIVED_RECIPES;
  return recipes;
}

float MatchEngine::convertConditionBinturong(float condition) const
{
  // The whole tank: a Binturong body has no fuel floor.
  return std::clamp(condition, 0.0f, 1.0f);
}

float MatchEngine::convertFamiliarityBinturong(float familiarity) const
{
  return std::clamp(familiarity, 0.0f, 1.0f);
}

float MatchEngine::convertTeamTalkBinturong(float modifier) const
{
  return std::clamp(modifier, -MatchTuning::Touchline::MAX_TEAM_TALK_MODIFIER,
                    MatchTuning::Touchline::MAX_TEAM_TALK_MODIFIER);
}

void MatchEngine::initializePlayersBinturong(const Lineup& lineup,
                                             bool isHomeTeam)
{
  const auto addPlayer = [&](const Player* player, Vector2F slot,
                             std::vector<MatchPlayer>& destination)
  {
    if (!player) return;
    // Lineup coordinates (own goal at x = 0) onto this pitch: the away
    // side is mirrored.
    const PitchPoint position =
        pitch.at(isHomeTeam ? slot.x : 1.0f - slot.x, slot.y);

    MatchPlayer matchPlayer;
    matchPlayer.player = player;
    matchPlayer.isHomeTeam = isHomeTeam;
    matchPlayer.position = position;
    matchPlayer.basePosition = position;
    matchPlayer.movementTarget = position;
    matchPlayer.facingAngle = isHomeTeam ? 0.0f : std::numbers::pi_v<float>;
    matchPlayer.targetAngle = matchPlayer.facingAngle;
    loadAttributesBinturong(matchPlayer, player);
    matchPlayer.statsIndex = addPlayerStats(matchPlayer, true);
    destination.push_back(matchPlayer);
  };

  addPlayer(
      lineup.getGoalkeeper(),
      {MatchTuning::Pitch::LINEUP_GOALKEEPER_X, MatchTuning::Pitch::CENTRE},
      players);
  if (lineup.getGoalkeeper() && !players.empty())
    players.back().isGoalkeeper = true;
  auto& formation = formations[isHomeTeam ? 0 : 1];
  for (const auto& positioned : lineup.getOutfieldPlayers())
  {
    const std::size_t before = players.size();
    addPlayer(positioned.player, positioned.position, players);
    if (players.size() > before)
      players.back().formationSlot = static_cast<std::int8_t>(formation.size());
    formation.push_back(positioned.position);
  }
}

void MatchEngine::loadAttributesBinturong(MatchPlayer& matchPlayer,
                                          const Player* player) const
{
  // Cache the named career stats once, after match-level normalization. The
  // hot movement/decision loops read these fields, never a string-keyed map.
  // A new simulation attribute needs an explicit consumer as well as a field
  // here; adding a name to stats_config.json alone creates no new behavior.
  matchPlayer.pace = attributeBinturong(player, "Pace");
  matchPlayer.shooting = attributeBinturong(player, "Shooting");
  matchPlayer.passing = attributeBinturong(player, "Passing");
  matchPlayer.dribbling = attributeBinturong(player, "Dribbling");
  matchPlayer.defending = attributeBinturong(player, "Defending");
  matchPlayer.goalkeeping = attributeBinturong(player, "Goalkeeping");
  matchPlayer.physicality = attributeBinturong(player, "Physicality");
  matchPlayer.endurance = attributeBinturong(player, "Stamina");
  matchPlayer.vision = attributeBinturong(player, "Vision");
  if (matchPlayer.isHomeTeam)
  {
    const float BONUS = MatchTuning::Rules::HOME_ATTRIBUTE_BONUS *
                        matchContext.homeAdvantageScale;
    for (float* value : {&matchPlayer.shooting, &matchPlayer.passing,
                         &matchPlayer.dribbling, &matchPlayer.defending,
                         &matchPlayer.goalkeeping, &matchPlayer.vision})
      *value = std::min(1.0f, *value + BONUS);
  }
  matchPlayer.heightMetres =
      player ? MatchRules::playerHeightMetres(player->getHeight())
             : MatchTuning::Units::DEFAULT_PLAYER_HEIGHT_METRES;
  using P = MatchTuning::Player;
  // Match-engine-only physical values, worked out here rather than as
  // derived stats. Top speed across the stat scale (fatigue plan 4.1): 7.0 m/s at Pace 1
  // (25 km/h, a Sunday-league sprint) to 10.4 at 99 (37.4 km/h, the EPL
  // record).
  constexpr float TOP_SPEED_AT_1 = 7.0f;
  constexpr float TOP_SPEED_AT_99 = 10.4f;
  matchPlayer.maxSpeed =
      TOP_SPEED_AT_1 + (TOP_SPEED_AT_99 - TOP_SPEED_AT_1) *
                           (matchPlayer.pace - STAT_1_RATING) /
                           (STAT_99_RATING - STAT_1_RATING);
  // Acceleration is a derived stat (the Binturong body reads it there).
  matchPlayer.braking =
      P::BRAKING_BASE + matchPlayer.physicality * P::BRAKING_PHYSICALITY;
}

void MatchEngine::placeFormationBinturong(bool homeTeam)
{
  const auto& formation = formations[homeTeam ? 0 : 1];
  int sideSize = 0;
  int onPitch = 0;
  for (const MatchPlayer& player : players)
  {
    if (!player.player || player.isHomeTeam != homeTeam) continue;
    ++sideSize;
    if (active(player)) ++onPitch;
  }
  const float drop = static_cast<float>(sideSize - onPitch) *
                     MatchTuning::Rules::SHORT_HANDED_DROP;
  for (MatchPlayer& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam ||
        player.isGoalkeeper || player.formationSlot < 0 ||
        static_cast<std::size_t>(player.formationSlot) >= formation.size())
      continue;
    const Vector2F spot =
        formation[static_cast<std::size_t>(player.formationSlot)];
    const float x = std::clamp(spot.x - drop, MatchTuning::Pitch::PLAYER_MIN_X,
                               MatchTuning::Pitch::PLAYER_MAX_X);
    player.basePosition = pitch.at(homeTeam ? x : 1.0f - x, spot.y);
  }
}

void MatchEngine::computeUnderdogSharesBinturong()
{
  // Mean outfield quality of the starting elevens, without the home lift.
  std::array<float, 2> quality{};
  std::array<int, 2> counted{};
  for (const MatchPlayer& player : players)
  {
    if (!player.player || player.isGoalkeeper) continue;
    const std::size_t side = player.isHomeTeam ? 0 : 1;
    float sum = 0.0f;
    for (const std::string_view name :
         {"Pace", "Shooting", "Passing", "Dribbling", "Defending",
          "Physicality", "Vision"})
      sum += attributeBinturong(player.player, name);
    quality[side] += sum / 7.0f;
    ++counted[side];
  }
  if (counted[0] == 0 || counted[1] == 0) return;
  const float gap = quality[1] / static_cast<float>(counted[1]) -
                    quality[0] / static_cast<float>(counted[0]);
  using T = MatchTuning::Touchline;
  const auto share = [](float deficit)
  {
    return std::clamp((deficit - T::UNDERDOG_GAP_START) / T::UNDERDOG_GAP_RANGE,
                      0.0f, 1.0f);
  };
  underdogShares = {share(gap), share(-gap)};
}

void MatchEngine::resolveTacticsBinturong(bool homeTeam)
{
  // Compile stored slot instructions into match-local numeric profiles. The
  // anchor identifies a formation job, not its current occupant: substitutes
  // inherit that job. Incompatible roles fall back to Standard. Keep this
  // resolution outside the per-player physics loops when adding new roles.
  const std::size_t side = homeTeam ? 0 : 1;
  const Strategy& strategy = homeTeam ? homeStrategy : awayStrategy;
  const std::vector<Vector2F>& anchors = slotAnchors[side];
  for (std::size_t slot = 0; slot < slotTactics[side].size(); ++slot)
  {
    SlotTactic tactic;
    const SlotInstruction* instruction =
        slot < anchors.size() ? strategy.findSlot(anchors[slot]) : nullptr;
    if (instruction)
    {
      const RoleFamily family = Tactics::familyForSlot(anchors[slot]);
      tactic.role = Tactics::allows(family, instruction->role)
                        ? instruction->role
                        : TacticalRole::Standard;
      tactic.profile = Tactics::profile(tactic.role, instruction->duty);
      tactic.possessionOffset =
          PitchOffset{pitch.atX(instruction->possessionOffset.x),
                      pitch.atY(instruction->possessionOffset.y)};
    }
    slotTactics[side][slot] = tactic;
  }
  keeperProfiles[side] =
      Tactics::profile(strategy.getKeeperRole(), RoleDuty::Support);
  oppositionOrders[side] = strategy.getOppositionOrders();
}

float MatchEngine::rawAttributeBinturong(const Player* player,
                                         std::string_view name) const
{
  const auto stat = player->getStats().find(std::string(name));
  if (stat != player->getStats().end())
    return stat->second / MatchTuning::Player::RATING_SCALE;
  return static_cast<float>(player->getOverall(statsConfig)) /
         MatchTuning::Player::RATING_SCALE;
}

float MatchEngine::attributeBinturong(const Player* player,
                                      std::string_view name) const
{
  // Binturong: a straight line. A stat of 60 is 0.60 in every match: no stretch
  // around a typical professional, no shift toward the match's level.
  if (!player) return MatchTuning::Player::DEFAULT_ATTRIBUTE;
  return std::clamp(rawAttributeBinturong(player, name),
                    MatchTuning::Player::MIN_ATTRIBUTE, 1.0f);
}

void MatchEngine::computeLevelBinturong(const Lineup& /*home*/,
                                        const Lineup& /*away*/)
{
  // Binturong: the match's level changes nothing. No shift toward a reference
  // level (attributes are what the stats say, see attributeBinturong) and no
  // finishing precision by level (finishing comes from Shooting).
  levelShift = 0.0f;
  levelPrecision = 1.0f;
}
