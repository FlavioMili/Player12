// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

#include "global/paths.h"
#include "model/match_commentary.h"
#include "model/match_rules.h"

using MatchRules::FoulContext;
using MatchRules::FoulSanction;

TEST(MatchRulesTest, AddedTimeGrowsWithStoppagesAndStaysBounded)
{
  const MatchRules::StoppageLog quiet;
  const int quietFirst = MatchRules::computeAddedMinutes(quiet, 1);
  const int quietSecond = MatchRules::computeAddedMinutes(quiet, 2);
  EXPECT_GE(quietFirst, MatchTuning::Stoppage::MIN_ADDED_MINUTES);
  EXPECT_GE(quietSecond, quietFirst);

  MatchRules::StoppageLog busy;
  busy.goals = 2;
  busy.substitutions = 6;
  busy.cards = 3;
  busy.injuries = 1;
  const int busySecond = MatchRules::computeAddedMinutes(busy, 2);
  EXPECT_GT(busySecond, quietSecond);
  EXPECT_GE(busySecond, 5);
  EXPECT_LE(busySecond, 9);

  MatchRules::StoppageLog chaos;
  chaos.goals = 20;
  chaos.injuries = 20;
  EXPECT_EQ(MatchRules::computeAddedMinutes(chaos, 2),
            MatchTuning::Stoppage::MAX_ADDED_MINUTES);
}

TEST(MatchRulesTest, RatingIsEventDrivenAndBounded)
{
  PlayerMatchStats quiet;
  quiet.minutesPlayed = 90.0f;
  EXPECT_FLOAT_EQ(MatchRules::computeMatchRating(quiet, false, false, 0),
                  MatchTuning::Rating::BASELINE);

  PlayerMatchStats scorer = quiet;
  scorer.goals = 2;
  scorer.assists = 1;
  scorer.shots = 4;
  scorer.shotsOnTarget = 3;
  const float scorerRating =
      MatchRules::computeMatchRating(scorer, false, false, 2);
  EXPECT_GT(scorerRating, 8.0f);

  PlayerMatchStats sentOff = quiet;
  sentOff.redCards = 1;
  sentOff.foulsCommitted = 3;
  EXPECT_LT(MatchRules::computeMatchRating(sentOff, false, false, -1), 5.0f);

  PlayerMatchStats cleanSheetKeeper = quiet;
  cleanSheetKeeper.saves = 4;
  PlayerMatchStats beatenKeeper = quiet;
  beatenKeeper.goalsConceded = 4;
  EXPECT_GT(MatchRules::computeMatchRating(cleanSheetKeeper, true, false, 0),
            MatchRules::computeMatchRating(beatenKeeper, true, false, -4));

  PlayerMatchStats extreme = quiet;
  extreme.goals = 12;
  EXPECT_FLOAT_EQ(MatchRules::computeMatchRating(extreme, false, false, 12),
                  MatchTuning::Rating::MAXIMUM);
  PlayerMatchStats disaster = quiet;
  disaster.ownGoals = 5;
  disaster.redCards = 1;
  EXPECT_FLOAT_EQ(MatchRules::computeMatchRating(disaster, false, true, -5),
                  MatchTuning::Rating::MINIMUM);
}

TEST(MatchRulesTest, AerialReachDependsOnHeightAndPhysicality)
{
  EXPECT_FLOAT_EQ(MatchRules::playerHeightMetres(0),
                  MatchTuning::Units::DEFAULT_PLAYER_HEIGHT_METRES);
  EXPECT_FLOAT_EQ(MatchRules::playerHeightMetres(191), 1.91f);

  const float shortWeak = MatchRules::headerReachMetres(1.70f, 0.3f);
  const float tallWeak = MatchRules::headerReachMetres(1.92f, 0.3f);
  const float tallStrong = MatchRules::headerReachMetres(1.92f, 0.9f);
  EXPECT_LT(shortWeak, tallWeak);
  EXPECT_LT(tallWeak, tallStrong);
  EXPECT_GT(tallStrong, 2.3f);
  EXPECT_LT(tallStrong, 3.0f);
  EXPECT_GT(MatchRules::goalkeeperReachMetres(1.90f, 0.7f), tallStrong);

  EXPECT_FLOAT_EQ(MatchRules::aerialDuelStrength(1.70f, 0.5f, 3.2f), 0.0f);
  EXPECT_GT(MatchRules::aerialDuelStrength(1.92f, 0.9f, 2.2f),
            MatchRules::aerialDuelStrength(1.75f, 0.4f, 2.2f));
}

TEST(MatchRulesTest, FoulSanctionsFollowTheLaws)
{
  FoulContext careless;
  careless.severityRoll = 0.2f;
  careless.cardRoll = 0.9f;
  EXPECT_EQ(MatchRules::decideFoulSanction(careless), FoulSanction::NONE);

  FoulContext reckless = careless;
  reckless.severityRoll = 0.95f;
  reckless.cardRoll = 0.1f;
  EXPECT_EQ(MatchRules::decideFoulSanction(reckless), FoulSanction::YELLOW);
  reckless.offenderAlreadyBooked = true;
  EXPECT_EQ(MatchRules::decideFoulSanction(reckless),
            FoulSanction::SECOND_YELLOW);

  FoulContext violent = careless;
  violent.severityRoll = 0.9999f;
  EXPECT_EQ(MatchRules::decideFoulSanction(violent), FoulSanction::RED);

  // Denying an obvious goal-scoring opportunity is a sending-off outside the
  // area, but usually "only" a caution inside it (double punishment rule).
  using D = MatchTuning::Discipline;
  static_assert(D::PENALTY_AREA_DOGSO_RED_CHANCE <
                D::TACTICAL_YELLOW_CHANCE * D::HOME_CARD_BIAS);
  FoulContext lastMan = careless;
  lastMan.denyingGoalChance = true;
  lastMan.cardRoll = (D::PENALTY_AREA_DOGSO_RED_CHANCE +
                      D::TACTICAL_YELLOW_CHANCE * D::HOME_CARD_BIAS) *
                     0.5f;
  EXPECT_EQ(MatchRules::decideFoulSanction(lastMan), FoulSanction::RED);
  lastMan.inPenaltyArea = true;
  EXPECT_EQ(MatchRules::decideFoulSanction(lastMan), FoulSanction::YELLOW);

  // A stricter referee cautions for rolls a lenient one lets go.
  FoulContext tactical = careless;
  tactical.tactical = true;
  tactical.cardRoll = D::TACTICAL_YELLOW_CHANCE * D::HOME_CARD_BIAS;
  tactical.strictness = 0.8f;
  EXPECT_EQ(MatchRules::decideFoulSanction(tactical), FoulSanction::NONE);
  tactical.strictness = 1.3f;
  EXPECT_EQ(MatchRules::decideFoulSanction(tactical), FoulSanction::YELLOW);
}

TEST(MatchRulesTest, StaminaDrainFollowsIntensityAndEndurance)
{
  const float walking = MatchRules::staminaDrainPerSecond(0.2f, 0.5f, 0.0f);
  const float running = MatchRules::staminaDrainPerSecond(0.6f, 0.5f, 0.0f);
  const float sprinting = MatchRules::staminaDrainPerSecond(1.0f, 0.5f, 0.0f);
  EXPECT_LT(walking, running);
  EXPECT_LT(running, sprinting);
  EXPECT_GT(MatchRules::staminaDrainPerSecond(0.6f, 0.2f, 0.0f),
            MatchRules::staminaDrainPerSecond(0.6f, 0.9f, 0.0f));
  EXPECT_GT(MatchRules::staminaDrainPerSecond(0.6f, 0.5f, 1.0f), running);
}

TEST(MatchRulesTest, ChallengeTimingAndAngleDecideTackles)
{
  MatchRules::TackleContext closeControl;
  closeControl.exposure = 0.0f;
  MatchRules::TackleContext exposed = closeControl;
  exposed.exposure = 1.0f;
  // A challenge while the ball is away from the attacker's foot wins it more
  // often and catches the man less often.
  EXPECT_GT(MatchRules::tackleWinChance(exposed),
            MatchRules::tackleWinChance(closeControl) + 0.2f);
  EXPECT_LT(MatchRules::tackleFoulPropensity(exposed),
            MatchRules::tackleFoulPropensity(closeControl));

  MatchRules::TackleContext fromBehind = closeControl;
  fromBehind.fromBehind = true;
  MatchRules::TackleContext slidingFromBehind = fromBehind;
  slidingFromBehind.sliding = true;
  EXPECT_LT(MatchRules::tackleWinChance(fromBehind),
            MatchRules::tackleWinChance(closeControl));
  EXPECT_GT(MatchRules::tackleFoulPropensity(fromBehind),
            MatchRules::tackleFoulPropensity(closeControl) * 2.0f);
  EXPECT_GT(MatchRules::tackleFoulPropensity(slidingFromBehind),
            MatchRules::tackleFoulPropensity(fromBehind));

  // Skill and strength matter, and a strong player shielding is hard to rob.
  MatchRules::TackleContext skilledDefender = closeControl;
  skilledDefender.defending = 0.9f;
  skilledDefender.dribbling = 0.3f;
  MatchRules::TackleContext skilledDribbler = closeControl;
  skilledDribbler.defending = 0.3f;
  skilledDribbler.dribbling = 0.9f;
  EXPECT_GT(MatchRules::tackleWinChance(skilledDefender),
            MatchRules::tackleWinChance(skilledDribbler));
  MatchRules::TackleContext shielding = closeControl;
  shielding.shielding = true;
  shielding.carrierPhysicality = 0.9f;
  EXPECT_LT(MatchRules::tackleWinChance(shielding),
            MatchRules::tackleWinChance(closeControl));

  // A careful defender in his own box or on a booking fouls far less.
  MatchRules::TackleContext inBox = closeControl;
  inBox.inPenaltyArea = true;
  MatchRules::TackleContext booked = closeControl;
  booked.defenderBooked = true;
  EXPECT_LT(MatchRules::tackleFoulPropensity(inBox),
            MatchRules::tackleFoulPropensity(closeControl) * 0.2f);
  EXPECT_LT(MatchRules::tackleFoulPropensity(booked),
            MatchRules::tackleFoulPropensity(closeControl));
  for (const auto* context : {&closeControl, &exposed, &slidingFromBehind})
  {
    EXPECT_GE(MatchRules::tackleWinChance(*context),
              MatchTuning::Defending::MIN_WIN_CHANCE);
    EXPECT_LE(MatchRules::tackleWinChance(*context),
              MatchTuning::Defending::MAX_WIN_CHANCE);
  }
}

namespace
{
nlohmann::json languageFile(const char* file)
{
  std::ifstream stream(AssetPaths::root() + "assets/lang/" + file);
  return nlohmann::json::parse(stream);
}
}  // namespace

TEST(MatchCommentaryTest, EveryEventHasALineInBothLanguages)
{
  const nlohmann::json english = languageFile("English.json");
  const nlohmann::json italian = languageFile("Italian.json");
  for (int type = 0;
       type <=
       static_cast<int>(std::to_underlying(MatchEventType::PENALTY_SHOOTOUT));
       ++type)
  {
    for (int detail = 0;
         detail <=
         static_cast<int>(std::to_underlying(MatchEventDetail::MISSED));
         ++detail)
    {
      MatchEvent event;
      event.type = static_cast<MatchEventType>(type);
      event.detail = static_cast<MatchEventDetail>(detail);
      for (const int variant : {0, 1, 2, 3})
      {
        event.primaryPlayerId = variant % 2 == 0 ? PlayerID{0} : PlayerID{7};
        event.period = variant + 1;
        event.homeShootout = variant >= 2 ? 3 : 0;
        const char* key = MatchCommentary::key(event);
        ASSERT_TRUE(english.contains(key)) << key;
        ASSERT_TRUE(italian.contains(key)) << key;
        // No line may leak an internal side name.
        for (const nlohmann::json* language : {&english, &italian})
        {
          const std::string line = (*language)[key].get<std::string>();
          EXPECT_EQ(line.find(" home"), std::string::npos) << line;
          EXPECT_EQ(line.find(" away"), std::string::npos) << line;
        }
      }
    }
  }
  EXPECT_TRUE(english.contains("MATCH_TEAM_HOME"));
  EXPECT_TRUE(italian.contains("MATCH_TEAM_AWAY"));
}

TEST(MatchCommentaryTest, FillsTeamsPlayersAndScore)
{
  const nlohmann::json english = languageFile("English.json");
  const nlohmann::json italian = languageFile("Italian.json");
  MatchCommentaryNames names;
  names.homeTeam = "Acaya";
  names.awayTeam = "Foggia";
  names.player = [](PlayerID id)
  { return id == 9 ? std::string("Rossi") : std::string("Bianchi"); };

  MatchEvent corner;
  corner.type = MatchEventType::CORNER;
  corner.hasTeam = true;
  corner.isHomeTeam = false;
  corner.primaryPlayerId = 9;
  EXPECT_EQ(MatchCommentary::fill(
                english[MatchCommentary::key(corner)].get<std::string>(),
                corner, names),
            "Corner for Foggia, taken by Rossi");
  EXPECT_EQ(MatchCommentary::fill(
                italian[MatchCommentary::key(corner)].get<std::string>(),
                corner, names),
            "Calcio d'angolo per Foggia, batte Rossi");

  MatchEvent goal;
  goal.type = MatchEventType::GOAL;
  goal.hasTeam = true;
  goal.isHomeTeam = true;
  goal.primaryPlayerId = 9;
  goal.homeScore = 2;
  goal.awayScore = 1;
  EXPECT_EQ(
      MatchCommentary::fill(
          english[MatchCommentary::key(goal)].get<std::string>(), goal, names),
      "GOAL! Rossi scores for Acaya (2-1)");

  MatchEvent added;
  added.type = MatchEventType::ADDED_TIME;
  added.minutes = 4;
  EXPECT_EQ(MatchCommentary::fill("{minutes} + {unknown}", added, names),
            "4 + {unknown}");
}

TEST(MatchRulesTest, ShootoutIsDecidedWhenOneSideCannotCatchUp)
{
  // Best of five: 3-0 after three kicks each cannot be caught.
  EXPECT_TRUE(MatchRules::shootoutDecided(3, 3, 0, 3));
  EXPECT_FALSE(MatchRules::shootoutDecided(3, 3, 1, 3));
  // 4-1 with the trailing side having two kicks left: 1 + 2 < 4.
  EXPECT_TRUE(MatchRules::shootoutDecided(4, 4, 1, 3));
  // 4-2 with two kicks left can still be levelled.
  EXPECT_FALSE(MatchRules::shootoutDecided(4, 4, 2, 3));
  // Level after five each: sudden death, decided only on equal kicks.
  EXPECT_FALSE(MatchRules::shootoutDecided(4, 5, 4, 5));
  EXPECT_FALSE(MatchRules::shootoutDecided(5, 6, 4, 5));
  EXPECT_TRUE(MatchRules::shootoutDecided(5, 6, 4, 6));
  EXPECT_FALSE(MatchRules::shootoutDecided(6, 7, 6, 7));
}

TEST(MatchRulesTest, PeriodsRunThroughExtraTime)
{
  EXPECT_FLOAT_EQ(MatchRules::periodStartMinute(1), 0.0f);
  EXPECT_FLOAT_EQ(MatchRules::periodEndMinute(2), 90.0f);
  EXPECT_FLOAT_EQ(MatchRules::periodStartMinute(3), 90.0f);
  EXPECT_FLOAT_EQ(MatchRules::periodEndMinute(3), 105.0f);
  EXPECT_FLOAT_EQ(MatchRules::periodStartMinute(4), 105.0f);
  EXPECT_FLOAT_EQ(MatchRules::periodEndMinute(4), 120.0f);
  MatchRules::StoppageLog quiet;
  EXPECT_GE(MatchRules::computeAddedMinutes(quiet, 3), 1);
  EXPECT_LE(MatchRules::computeAddedMinutes(quiet, 4),
            MatchRules::computeAddedMinutes(quiet, 2));
}

TEST(MatchRulesTest, KeeperHandlingRequiresPhysicalHorizontalAndVerticalReach)
{
  const float height = 1.85f;
  const float skill = 0.8f;
  const float shoulder = height * 0.8f;
  const float ceiling = MatchRules::goalkeeperReachMetres(height, skill);
  const float standing =
      MatchRules::goalkeeperHandlingRadiusMetres(height, skill, shoulder);
  EXPECT_GT(standing, 0.7f);
  EXPECT_LT(standing, 0.9f);
  EXPECT_LT(MatchRules::goalkeeperHandlingRadiusMetres(
                height, skill, (shoulder + ceiling) * 0.5f),
            standing);
  EXPECT_FLOAT_EQ(
      MatchRules::goalkeeperHandlingRadiusMetres(height, skill, ceiling), 0.0f);
  EXPECT_FLOAT_EQ(
      MatchRules::goalkeeperHandlingRadiusMetres(height, skill, ceiling + 0.1f),
      0.0f);
  EXPECT_LT(MatchRules::goalkeeperHandlingRadiusMetres(1.70f, skill, 1.0f),
            MatchRules::goalkeeperHandlingRadiusMetres(2.00f, skill, 1.0f));
}
