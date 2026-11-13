// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Aardvark handshake: how the game's inputs become the match engine's
// state, as inherited with the engine - the attribute scale (level shift
// and stretch), physical limits, the home lift, slot roles, base positions,
// the match context, referee strictness, condition, familiarity and team
// talks. Kept frozen so the handshakes being developed can be compared with
// it (see match_handshake.h). Every function here is a MatchEngine member
// with the "Aardvark" suffix, declared in match_handshake_aardvark_members.h;
// the engine reaches them through match_handshake_dispatch.cpp.
//
// Do not change behaviour here.

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

namespace
{
/** Attributes are stretched around a typical professional level (see
 * MatchTuning::Player::ATTRIBUTE_PIVOT). */
float stretchAttribute(float raw)
{
  using P = MatchTuning::Player;
  return std::clamp(
      P::ATTRIBUTE_PIVOT +
          P::ATTRIBUTE_SATURATION *
              std::tanh((raw - P::ATTRIBUTE_PIVOT) * P::ATTRIBUTE_CONTRAST /
                        P::ATTRIBUTE_SATURATION),
      P::MIN_ATTRIBUTE, 1.0f);
}
}  // namespace

float MatchEngine::drawRefereeStrictnessAardvark() const
{
  using D = MatchTuning::Discipline;
  const float mean = matchContext.refereeStrictnessMean;
  return std::clamp(mean + refereeDraw * matchContext.refereeStrictnessSd,
                    D::MIN_STRICTNESS * mean, D::MAX_STRICTNESS * mean);
}

void MatchEngine::convertMatchContextAardvark(const MatchContext& context)
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

float MatchEngine::convertConditionAardvark(float condition) const
{
  return std::clamp(condition, MatchTuning::Player::MINIMUM_STAMINA, 1.0f);
}

float MatchEngine::convertFamiliarityAardvark(float familiarity) const
{
  return std::clamp(familiarity, 0.0f, 1.0f);
}

float MatchEngine::convertTeamTalkAardvark(float modifier) const
{
  return std::clamp(modifier, -MatchTuning::Touchline::MAX_TEAM_TALK_MODIFIER,
                    MatchTuning::Touchline::MAX_TEAM_TALK_MODIFIER);
}

void MatchEngine::initializePlayersAardvark(const Lineup& lineup,
                                            bool isHomeTeam)
{
  const auto addPlayer = [&](const Player* player, Vector2F slot,
                             std::vector<MatchPlayer>& destination)
  {
    if (!player) return;
    // Lineup coordinates (own goal at x = 0) onto this pitch: the away
    // side is mirrored.
    PitchPoint position{slot.x, slot.y};
    if (!isHomeTeam) position.x = 1.0f - position.x;

    MatchPlayer matchPlayer;
    matchPlayer.player = player;
    matchPlayer.isHomeTeam = isHomeTeam;
    matchPlayer.position = position;
    matchPlayer.basePosition = position;
    matchPlayer.movementTarget = position;
    matchPlayer.facingAngle = isHomeTeam ? 0.0f : std::numbers::pi_v<float>;
    matchPlayer.targetAngle = matchPlayer.facingAngle;
    loadAttributesAardvark(matchPlayer, player);
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

void MatchEngine::loadAttributesAardvark(MatchPlayer& matchPlayer,
                                         const Player* player) const
{
  // Cache the named career stats once, after match-level normalization. The
  // hot movement/decision loops read these fields, never a string-keyed map.
  // A new simulation attribute needs an explicit consumer as well as a field
  // here; adding a name to stats_config.json alone creates no new behavior.
  matchPlayer.pace = attributeAardvark(player, "Pace");
  matchPlayer.shooting = attributeAardvark(player, "Shooting");
  matchPlayer.passing = attributeAardvark(player, "Passing");
  matchPlayer.dribbling = attributeAardvark(player, "Dribbling");
  matchPlayer.defending = attributeAardvark(player, "Defending");
  matchPlayer.goalkeeping = attributeAardvark(player, "Goalkeeping");
  matchPlayer.physicality = attributeAardvark(player, "Physicality");
  matchPlayer.endurance = attributeAardvark(player, "Stamina");
  matchPlayer.vision = attributeAardvark(player, "Vision");
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
  matchPlayer.maxSpeed =
      P::TOP_SPEED_BASE + matchPlayer.pace * P::TOP_SPEED_PACE;
  matchPlayer.acceleration =
      P::ACCELERATION_BASE + matchPlayer.pace * P::ACCELERATION_PACE +
      matchPlayer.physicality * P::ACCELERATION_PHYSICALITY;
  matchPlayer.braking =
      P::BRAKING_BASE + matchPlayer.physicality * P::BRAKING_PHYSICALITY;
}

void MatchEngine::placeFormationAardvark(bool homeTeam)
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
    player.basePosition = PitchPoint{homeTeam ? x : 1.0f - x, spot.y};
  }
}

void MatchEngine::computeUnderdogSharesAardvark()
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
      sum += attributeAardvark(player.player, name);
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

void MatchEngine::resolveTacticsAardvark(bool homeTeam)
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
      tactic.possessionOffset = PitchOffset{instruction->possessionOffset.x,
                                              instruction->possessionOffset.y};
    }
    slotTactics[side][slot] = tactic;
  }
  keeperProfiles[side] =
      Tactics::profile(strategy.getKeeperRole(), RoleDuty::Support);
  oppositionOrders[side] = strategy.getOppositionOrders();
}

float MatchEngine::rawAttributeAardvark(const Player* player,
                                        std::string_view name) const
{
  const auto stat = player->getStats().find(std::string(name));
  if (stat != player->getStats().end())
    return stat->second / MatchTuning::Player::RATING_SCALE;
  return static_cast<float>(player->getOverall(statsConfig)) /
         MatchTuning::Player::RATING_SCALE;
}

float MatchEngine::attributeAardvark(const Player* player,
                                     std::string_view name) const
{
  if (!player) return MatchTuning::Player::DEFAULT_ATTRIBUTE;
  return stretchAttribute(rawAttributeAardvark(player, name) + levelShift);
}

void MatchEngine::computeLevelAardvark(const Lineup& home, const Lineup& away)
{
  // Mean raw outfield quality of both elevens.
  float sum = 0.0f;
  int counted = 0;
  for (const Lineup* lineup : {&home, &away})
  {
    for (const auto& positioned : lineup->getOutfieldPlayers())
    {
      if (!positioned.player) continue;
      for (const std::string_view name :
           {"Pace", "Shooting", "Passing", "Dribbling", "Defending",
            "Physicality", "Vision"})
        sum += rawAttributeAardvark(positioned.player, name);
      counted += 7;
    }
  }
  if (counted == 0) return;
  using P = MatchTuning::Player;
  const float level = sum / static_cast<float>(counted);
  levelShift = P::LEVEL_NORMALISATION * (P::REFERENCE_LEVEL - level);
  const float fromReference = level - P::REFERENCE_LEVEL;
  levelPrecision = std::max(
      P::MIN_LEVEL_PRECISION,
      1.0f + fromReference * (fromReference < 0.0f ? P::LEVEL_FINISHING_GAIN
                                                   : P::ELITE_FINISHING_GAIN));
}
