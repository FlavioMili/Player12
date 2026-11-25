// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Every conversion goes to the match's handshake (chosen in the MatchEngine
// constructor). To add a handshake: a HandshakeId (match_handshake.h), its
// members (match_handshake_<name>_members.h, included in MatchEngine), its
// definitions (match_handshake_<name>.cpp), and a case in each function here.

#include "model/match_engine.h"

void MatchEngine::computeLevel(const Lineup& home, const Lineup& away)
{
  switch (handshakeId)
  {
    case HandshakeId::AARDVARK:
      break;
    case HandshakeId::BINTURONG:
      computeLevelBinturong(home, away);
      return;
  }
  computeLevelAardvark(home, away);
}

float MatchEngine::rawAttribute(const Player* player,
                                std::string_view name) const
{
  switch (handshakeId)
  {
    case HandshakeId::AARDVARK:
      break;
    case HandshakeId::BINTURONG:
      return rawAttributeBinturong(player, name);
  }
  return rawAttributeAardvark(player, name);
}

float MatchEngine::attribute(const Player* player, std::string_view name) const
{
  switch (handshakeId)
  {
    case HandshakeId::AARDVARK:
      break;
    case HandshakeId::BINTURONG:
      return attributeBinturong(player, name);
  }
  return attributeAardvark(player, name);
}

void MatchEngine::loadAttributes(MatchPlayer& matchPlayer,
                                 const Player* player) const
{
  switch (handshakeId)
  {
    case HandshakeId::AARDVARK:
      loadAttributesAardvark(matchPlayer, player);
      break;
    case HandshakeId::BINTURONG:
      loadAttributesBinturong(matchPlayer, player);
      break;
  }
  deriveStats(matchPlayer);
}

void MatchEngine::initializePlayers(const Lineup& lineup, bool isHomeTeam)
{
  switch (handshakeId)
  {
    case HandshakeId::AARDVARK:
      initializePlayersAardvark(lineup, isHomeTeam);
      break;
    case HandshakeId::BINTURONG:
      initializePlayersBinturong(lineup, isHomeTeam);
      break;
  }
  // The handshakes load their players directly; derive the side's stats.
  for (MatchPlayer& player : players)
  {
    if (player.isHomeTeam == isHomeTeam)
    {
      deriveStats(player);
    }
  }
}

const DerivedRecipes& MatchEngine::derivedRecipes() const
{
  switch (handshakeId)
  {
    case HandshakeId::AARDVARK:
      break;
    case HandshakeId::BINTURONG:
      return derivedRecipesBinturong();
  }
  // The Aardvark handshake has no recipes of its own.
  return DEFAULT_DERIVED_RECIPES;
}

void MatchEngine::deriveStats(MatchPlayer& matchPlayer) const
{
  // Pure arithmetic on the loaded attributes: no random numbers (his own
  // deviations are hashed from his id), so it cannot change a match that
  // does not read the results.
  const std::array<float, BASE_ATTRIBUTE_COUNT> base{
      matchPlayer.pace,      matchPlayer.shooting,  matchPlayer.passing,
      matchPlayer.dribbling, matchPlayer.defending, matchPlayer.physicality,
      matchPlayer.endurance, matchPlayer.vision,    matchPlayer.goalkeeping};
  // In order: the future game stats come first, so each is ready before a
  // match-engine-only recipe reads it. A stat a drill has set keeps its
  // value (and what is derived from it follows).
  const DerivedRecipes& recipes = derivedRecipes();
  for (std::size_t stat = 0; stat < DERIVED_STAT_COUNT; ++stat)
  {
    if ((matchPlayer.derivedOverrides & (std::uint64_t{1} << stat)) != 0u)
    {
      continue;
    }
    float rating = 0.0F;
    for (std::size_t index = 0; index < BASE_ATTRIBUTE_COUNT; ++index)
    {
      rating += recipes[stat].weights[index] * base[index];
    }
    for (std::size_t future = 0; future < DERIVED_STAT_COUNT; ++future)
    {
      rating +=
          recipes[stat].futureWeights[future] * matchPlayer.derived[future];
    }
    // His own deviation (a drill's playerless slot has none).
    if (recipes[stat].spread > 0.0F && (matchPlayer.player != nullptr))
    {
      rating += recipes[stat].spread *
                playerStatDeviation(matchPlayer.player->getId(),
                                    DERIVED_STATS[stat].name);
    }
    if (recipes[stat].onHeight())
    {
      rating =
          derivedRatingOnHeight(static_cast<DerivedStat>(stat), recipes[stat],
                                rating, matchPlayer.heightMetres);
    }
    matchPlayer.derived[stat] = std::clamp(rating, 0.0F, 1.0F);
  }
}

void MatchEngine::resolveTactics(bool homeTeam)
{
  switch (handshakeId)
  {
    case HandshakeId::AARDVARK:
      break;
    case HandshakeId::BINTURONG:
      resolveTacticsBinturong(homeTeam);
      return;
  }
  resolveTacticsAardvark(homeTeam);
}

void MatchEngine::placeFormation(bool homeTeam)
{
  switch (handshakeId)
  {
    case HandshakeId::AARDVARK:
      break;
    case HandshakeId::BINTURONG:
      placeFormationBinturong(homeTeam);
      return;
  }
  placeFormationAardvark(homeTeam);
}

float MatchEngine::drawRefereeStrictness() const
{
  switch (handshakeId)
  {
    case HandshakeId::AARDVARK:
      break;
    case HandshakeId::BINTURONG:
      return drawRefereeStrictnessBinturong();
  }
  return drawRefereeStrictnessAardvark();
}

void MatchEngine::computeUnderdogShares()
{
  switch (handshakeId)
  {
    case HandshakeId::AARDVARK:
      break;
    case HandshakeId::BINTURONG:
      computeUnderdogSharesBinturong();
      return;
  }
  computeUnderdogSharesAardvark();
}

void MatchEngine::convertMatchContext(const MatchContext& context)
{
  switch (handshakeId)
  {
    case HandshakeId::AARDVARK:
      break;
    case HandshakeId::BINTURONG:
      convertMatchContextBinturong(context);
      return;
  }
  convertMatchContextAardvark(context);
}

float MatchEngine::convertCondition(float condition) const
{
  switch (handshakeId)
  {
    case HandshakeId::AARDVARK:
      break;
    case HandshakeId::BINTURONG:
      return convertConditionBinturong(condition);
  }
  return convertConditionAardvark(condition);
}

float MatchEngine::convertFamiliarity(float familiarity) const
{
  switch (handshakeId)
  {
    case HandshakeId::AARDVARK:
      break;
    case HandshakeId::BINTURONG:
      return convertFamiliarityBinturong(familiarity);
  }
  return convertFamiliarityAardvark(familiarity);
}

float MatchEngine::convertTeamTalk(float modifier) const
{
  switch (handshakeId)
  {
    case HandshakeId::AARDVARK:
      break;
    case HandshakeId::BINTURONG:
      return convertTeamTalkBinturong(modifier);
  }
  return convertTeamTalkAardvark(modifier);
}
