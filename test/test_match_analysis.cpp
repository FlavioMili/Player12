// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

#include "model/match_analysis.h"
#include "model/match_engine.h"

namespace
{
MatchEvent shot(bool home, PlayerID player, float x, float y, float xg,
                float minute = 20.0f)
{
  MatchEvent event;
  event.type = MatchEventType::SHOT;
  event.hasTeam = true;
  event.isHomeTeam = home;
  event.primaryPlayerId = player;
  event.position = {x, y};
  event.xg = xg;
  event.timeMinute = minute;
  return event;
}

MatchEvent outcome(MatchEventType type, bool home, PlayerID player)
{
  MatchEvent event;
  event.type = type;
  event.hasTeam = true;
  event.isHomeTeam = home;
  event.primaryPlayerId = player;
  return event;
}

PlayerMatchStats line(PlayerID id, bool home, float rating, float minutes = 45)
{
  PlayerMatchStats stats;
  stats.playerId = id;
  stats.isHomeTeam = home;
  stats.minutesPlayed = minutes;
  stats.rating = rating;
  return stats;
}

bool suggests(const MatchAnalysis& analysis, AnalysisSuggestion::Kind kind)
{
  return std::ranges::any_of(analysis.suggestions,
                             [kind](const AnalysisSuggestion& suggestion)
                             { return suggestion.kind == kind; });
}

std::string nameOf(PlayerID id) { return std::format("P{}", id); }

/** A first half in which the away side attacks down its left flank. */
struct Scenario
{
  std::vector<MatchEvent> events;
  MatchStats stats;
  std::vector<PlayerMatchStats> players;
  std::vector<std::pair<PlayerID, float>> conditions;

  Scenario()
  {
    using enum MatchEventType;
    // Away attacks towards x = 0; away's left flank is high y in raw
    // coordinates (mirrored to low y in its attacking frame).
    for (int index = 0; index < 8; ++index)
    {
      events.push_back(shot(false, 20, 0.12f, 0.85f, 0.125f));
      events.push_back(outcome(SAVE, true, 1));
    }
    for (int index = 0; index < 2; ++index)
    {
      events.push_back(shot(true, 9, 0.88f, 0.5f, 0.05f));
      events.push_back(outcome(SHOT_OFF_TARGET, true, 9));
    }
    stats.homeShots = 2;
    stats.awayShots = 8;
    stats.homeShotXG = 0.1f;
    stats.awayShotXG = 1.0f;
    stats.homePossession = 60.0f;
    stats.awayPossession = 40.0f;
    stats.homePassesAttempted = 200;
    stats.homePassesCompleted = 170;
    stats.awayPassesAttempted = 150;
    stats.awayPassesCompleted = 120;
    stats.homeTackleAttempts = 6;
    stats.homeTackles = 3;
    players = {line(9, true, 5.3f), line(4, true, 7.8f), line(20, false, 7.0f)};
    conditions = {{9, 0.9f}, {7, 0.5f}};
  }

  AnalysisInput input(bool managed_home = true, float minute = 45.0f)
  {
    AnalysisInput in;
    in.events = events;
    in.stats = &stats;
    in.players = players;
    in.conditions = conditions;
    in.managed_home = managed_home;
    in.minute = minute;
    in.substitutions_left = 5;
    in.name_of = nameOf;
    return in;
  }
};
}  // namespace

TEST(MatchAnalysisTest, ShotsAreMirroredIntoAttackingFramesWithOutcomes)
{
  using enum MatchEventType;
  std::vector events = {shot(true, 9, 0.9f, 0.2f, 0.3f),
                        outcome(GOAL, true, 9),
                        shot(false, 20, 0.1f, 0.2f, 0.1f),
                        outcome(WOODWORK, false, 20),
                        outcome(SAVE, true, 1),
                        shot(false, 21, 0.2f, 0.5f, 0.05f),
                        outcome(SHOT_BLOCKED, true, 4)};
  const std::vector<ShotRecord> shots = extractShots(events);
  ASSERT_EQ(shots.size(), 3U);
  EXPECT_EQ(shots[0].outcome, ShotOutcome::Goal);
  EXPECT_FLOAT_EQ(shots[0].x, 0.9f);
  EXPECT_EQ(flankOf(shots[0]), Flank::Left);
  // The away shot from raw y = 0.2 is on the away side's right.
  EXPECT_FLOAT_EQ(shots[1].x, 0.9f);
  EXPECT_FLOAT_EQ(shots[1].y, 0.8f);
  EXPECT_EQ(flankOf(shots[1]), Flank::Right);
  EXPECT_EQ(shots[1].outcome, ShotOutcome::Saved);  // Rebound saved.
  EXPECT_EQ(shots[2].outcome, ShotOutcome::Blocked);
}

TEST(MatchAnalysisTest, FlankPatternAndSuggestionsCarryTheirEvidence)
{
  Scenario scenario;
  const MatchAnalysis analysis = analyseMatch(scenario.input());
  EXPECT_TRUE(analysis.enough_data);
  EXPECT_TRUE(analysis.tentative);  // Ten shots: early signs only.
  EXPECT_EQ(analysis.sample.key, "ANALYSIS_SAMPLE_TENTATIVE");
  EXPECT_EQ(analysis.own.shots, 2);
  EXPECT_EQ(analysis.opponent.shots, 8);
  // All of the away xG came down its left, i.e. the managed side's right.
  EXPECT_NEAR(analysis.opponent.flank_xg[0], 1.0f, 1e-4f);
  ASSERT_TRUE(suggests(analysis, AnalysisSuggestion::Kind::ProtectFlank));
  const auto protect = std::ranges::find(analysis.suggestions,
                                         AnalysisSuggestion::Kind::ProtectFlank,
                                         &AnalysisSuggestion::kind);
  EXPECT_EQ(protect->action.args.front(), "@ANALYSIS_FLANK_RIGHT");
  EXPECT_EQ(protect->reason.args.front(), "100%");
  // The tired player is the first change, with his condition as reason.
  ASSERT_TRUE(suggests(analysis, AnalysisSuggestion::Kind::SubstituteTired));
  EXPECT_EQ(analysis.suggestions.front().kind,
            AnalysisSuggestion::Kind::SubstituteTired);
  EXPECT_EQ(analysis.suggestions.front().player, 7U);
  EXPECT_EQ(analysis.suggestions.front().reason.args[1], "50%");
  EXPECT_LE(analysis.suggestions.size(),
            static_cast<std::size_t>(MatchAnalysisRules::MAX_SUGGESTIONS));
  // Lots of the ball, few chances.
  EXPECT_TRUE(
      suggests(analysis, AnalysisSuggestion::Kind::CreateMore) ||
      analysis.suggestions.size() ==
          static_cast<std::size_t>(MatchAnalysisRules::MAX_SUGGESTIONS));
}

TEST(MatchAnalysisTest, RatingOutliersAndFatigueAreNoted)
{
  Scenario scenario;
  const MatchAnalysis analysis = analyseMatch(scenario.input());
  const auto has = [&](PlayerNote::Kind kind, PlayerID id)
  {
    return std::ranges::any_of(
        analysis.notes, [&](const PlayerNote& note)
        { return note.kind == kind && note.player == id; });
  };
  EXPECT_TRUE(has(PlayerNote::Kind::Standout, 4));
  EXPECT_TRUE(has(PlayerNote::Kind::Struggling, 9));
  EXPECT_TRUE(has(PlayerNote::Kind::Tired, 7));
  EXPECT_FALSE(has(PlayerNote::Kind::Standout, 20));  // Opponent.
}

TEST(MatchAnalysisTest, EarlyInTheMatchOnlyFatigueIsRead)
{
  Scenario scenario;
  scenario.events.resize(2);  // One shot.
  scenario.stats.homeShots = 0;
  scenario.stats.awayShots = 1;
  scenario.stats.awayShotXG = 0.2f;
  scenario.conditions.clear();
  const MatchAnalysis analysis = analyseMatch(scenario.input(true, 12.0f));
  EXPECT_FALSE(analysis.enough_data);
  ASSERT_EQ(analysis.suggestions.size(), 1U);
  EXPECT_EQ(analysis.suggestions.front().kind,
            AnalysisSuggestion::Kind::KeepGoing);
  EXPECT_EQ(analysis.suggestions.front().action.key,
            "ANALYSIS_SUGGEST_TOO_EARLY");
}

TEST(MatchAnalysisTest, FullTimeNeverSuggestsSubstitutions)
{
  Scenario scenario;
  AnalysisInput input = scenario.input(true, 94.0f);
  input.full_time = true;
  const MatchAnalysis analysis = analyseMatch(input);
  EXPECT_FALSE(suggests(analysis, AnalysisSuggestion::Kind::SubstituteTired));
  EXPECT_FALSE(
      suggests(analysis, AnalysisSuggestion::Kind::SubstituteStruggling));
  EXPECT_TRUE(analysis.full_time);
}

TEST(MatchAnalysisTest, AwaySideSeesTheMirrorImage)
{
  Scenario scenario;
  const MatchAnalysis analysis = analyseMatch(scenario.input(false));
  EXPECT_EQ(analysis.own.shots, 8);
  EXPECT_NEAR(analysis.own.xg, 1.0f, 1e-4f);
  EXPECT_TRUE(suggests(analysis, AnalysisSuggestion::Kind::AttackFlank));
}

TEST(MatchAnalysisTest, PatternsNeedShotsNotJustMinutes)
{
  // Late in the match but only six shots: no pattern is claimed.
  Scenario scenario;
  scenario.events.erase(scenario.events.begin(), scenario.events.begin() + 8);
  scenario.stats.awayShots = 4;
  scenario.stats.awayShotXG = 0.5f;
  scenario.conditions.clear();
  MatchAnalysis analysis = analyseMatch(scenario.input(true, 75.0f));
  EXPECT_FALSE(analysis.enough_data);
  EXPECT_EQ(analysis.sample.key, "ANALYSIS_SAMPLE_TOO_SMALL");
  EXPECT_EQ(analysis.sample.args.front(), "6");
  EXPECT_FALSE(suggests(analysis, AnalysisSuggestion::Kind::ProtectFlank));
  EXPECT_FALSE(suggests(analysis, AnalysisSuggestion::Kind::CreateMore));
  EXPECT_FALSE(suggests(analysis, AnalysisSuggestion::Kind::PressHigher));
  EXPECT_TRUE(
      std::ranges::none_of(analysis.observations, [](const AnalysisLine& line)
                           { return line.key == "ANALYSIS_OBS_THEIR_FLANK"; }));

  // A flank pattern also needs enough shots from that side.
  Scenario lopsided;
  lopsided.events.clear();
  for (int index = 0; index < 4; ++index)
    lopsided.events.push_back(shot(false, 20, 0.1f, 0.85f, 0.3f));
  for (int index = 0; index < 8; ++index)
    lopsided.events.push_back(shot(true, 9, 0.9f, 0.5f, 0.05f));
  lopsided.stats.awayShots = 4;
  lopsided.stats.awayShotXG = 1.2f;
  lopsided.stats.homeShots = 8;
  lopsided.stats.homeShotXG = 0.4f;
  analysis = analyseMatch(lopsided.input());
  EXPECT_TRUE(analysis.enough_data);
  EXPECT_FALSE(suggests(analysis, AnalysisSuggestion::Kind::ProtectFlank));

  // Plenty of shots: no longer tentative.
  Scenario busy;
  for (int index = 0; index < 8; ++index)
    busy.events.push_back(shot(true, 9, 0.9f, 0.5f, 0.05f));
  busy.stats.homeShots = 10;
  analysis = analyseMatch(busy.input());
  EXPECT_FALSE(analysis.tentative);
  EXPECT_EQ(analysis.sample.key, "ANALYSIS_SAMPLE_OK");
}
