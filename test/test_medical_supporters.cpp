// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Playing through pain (aggravation risk), the medical staff's rest and
// minute-limit instructions, the load log behind the load chart, and the
// supporters' mood with its board nudge.

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/calendar.h"
#include "model/game.h"
#include "model/injury.h"
#include "model/medical_centre.h"
#include "model/supporters.h"
#include "model/world_rng.h"
#include "model/world_tuning.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 760'000 + getpid() % 100'000 * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

/** A new career managing the first club. */
TeamID startCareer(GameController& controller, int slot)
{
  Logger::init();
  controller.newGame(slot, WORLD_SEED);
  const TeamID club = controller.getTeams().front().get().getId();
  controller.selectManagedTeam(club);
  return club;
}

std::size_t countTitle(const GameController& controller, const char* key)
{
  return static_cast<std::size_t>(std::ranges::count(
      controller.getInbox(), std::string(key), &InboxMessage::title_key));
}

/** Share of @p trials matches that aggravated an injury with @p days left. */
double aggravationRate(GameData& data, MedicalDesk& desk, Inbox& inbox,
                       PlayerID player_id, std::uint16_t days, int trials)
{
  Player& player = data.getPlayers().at(player_id);
  int aggravated = 0;
  GameDateValue date(2025, 8, 1);
  for (int trial = 0; trial < trials; ++trial)
  {
    PlayerDynamics& dynamics = player.mutableDynamics();
    dynamics.injury = InjuryType::GroinStrain;
    dynamics.injury_days = days;
    dynamics.last_injury_day = 1;
    if (desk.afterMatch(data, date, player_id, 90, false, 0, inbox))
      ++aggravated;
    date = SeasonCalendar::addDays(date, 1);
  }
  return static_cast<double>(aggravated) / trials;
}
}  // namespace

// ---------------------------------------------------------------------------
// Playing through pain
// ---------------------------------------------------------------------------

TEST(PlayingThroughPainTest, AggravationRiskRisesWithSeverity)
{
  using InjuryModel::aggravationChance;
  using InjuryModel::aggravationMultiplier;
  EXPECT_FLOAT_EQ(aggravationMultiplier(InjurySeverity::Minor), 2.0f);
  EXPECT_FLOAT_EQ(aggravationMultiplier(InjurySeverity::Moderate), 3.0f);
  EXPECT_FLOAT_EQ(aggravationMultiplier(InjurySeverity::Major), 4.0f);

  EXPECT_EQ(aggravationChance(0, 90), 0.0)
      << "a fit player has nothing to aggravate";
  EXPECT_EQ(aggravationChance(10, 0), 0.0) << "no minutes, no risk";
  const double minor = aggravationChance(5, 90);
  const double moderate = aggravationChance(20, 90);
  const double major = aggravationChance(60, 90);
  EXPECT_LT(minor, moderate);
  EXPECT_LT(moderate, major);
  EXPECT_LT(aggravationChance(20, 30), moderate) << "fewer minutes, less risk";

  // Two to four times the risk of a fit player over the same minutes.
  const double fit =
      1.0 - std::exp(-WorldTuning::Fitness::MATCH_INJURY_RATE_PER_HOUR * 1.5);
  EXPECT_GT(minor / fit, 1.8);
  EXPECT_LT(major / fit, 4.0);
}

TEST(PlayingThroughPainTest, AnAggravationIsAlwaysASetback)
{
  using enum RngDomain;
  for (std::uint64_t key = 0; key < 200; ++key)
  {
    WorldRng rng = WorldRng::stream(WORLD_SEED, MatchInjury, key);
    const auto left = static_cast<std::uint16_t>(1 + key % 40);
    const Injury injury =
        InjuryModel::aggravate(rng, InjuryType::HamstringStrain, left);
    EXPECT_EQ(injury.type, InjuryType::HamstringStrain);
    EXPECT_GE(injury.days, left + 3);
  }
  WorldRng first = WorldRng::stream(WORLD_SEED, MatchInjury, 7);
  WorldRng second = WorldRng::stream(WORLD_SEED, MatchInjury, 7);
  EXPECT_EQ(InjuryModel::aggravate(first, InjuryType::CalfStrain, 9).days,
            InjuryModel::aggravate(second, InjuryType::CalfStrain, 9).days)
      << "same seed, same layoff";
}

TEST(PlayingThroughPainTest, MatchesAggravateInjuriesMoreTheWorseTheyAre)
{
  const SlotCleanup slot{uniqueSlot(0)};
  GameController controller;
  const TeamID club = startCareer(controller, slot.slot);
  GameData& data = *controller.getGameData();
  const PlayerID player_id =
      controller.getPlayersForTeam(club)[2].get().getId();
  MedicalDesk desk;
  Inbox inbox;

  constexpr int TRIALS = 3000;
  const double minor = aggravationRate(data, desk, inbox, player_id, 5, TRIALS);
  const double major =
      aggravationRate(data, desk, inbox, player_id, 45, TRIALS);
  EXPECT_NEAR(minor, InjuryModel::aggravationChance(5, 90), 0.02);
  EXPECT_NEAR(major, InjuryModel::aggravationChance(45, 90), 0.025);
  EXPECT_GT(major, minor * 1.5);
}

TEST(PlayingThroughPainTest, AggravationExtendsTheLayoffAndTellsTheManager)
{
  const SlotCleanup slot{uniqueSlot(1)};
  GameController controller;
  const TeamID club = startCareer(controller, slot.slot);
  Game& game = *controller.getGame();
  GameData& data = *controller.getGameData();
  const auto& squad = controller.getPlayersForTeam(club);
  const PlayerID injured_id = squad[4].get().getId();
  const PlayerID fit_id = squad[5].get().getId();
  PlayerDynamics& dynamics = data.getPlayers().at(injured_id).mutableDynamics();
  dynamics.injury = InjuryType::AnkleSprain;
  dynamics.injury_days = 6;
  dynamics.last_injury_day = 1;
  Inbox& inbox = game.getWorld().getInbox();
  const GameDateValue date = controller.getCurrentDate();

  // The engine hurting a player who was already injured is an aggravation.
  EXPECT_TRUE(game.getMedical().afterMatch(data, date, injured_id, 70, true,
                                           club, inbox));
  EXPECT_GT(dynamics.injury_days, 6);
  EXPECT_EQ(dynamics.injury, InjuryType::AnkleSprain);
  EXPECT_EQ(dynamics.last_injury_day, dayOrdinal(date));
  EXPECT_EQ(countTitle(controller, "MEDICAL_AGGRAVATED_TITLE"), 1u);

  // Once per day: hurt in this very match means nothing to aggravate.
  const std::uint16_t after = dynamics.injury_days;
  EXPECT_FALSE(game.getMedical().afterMatch(data, date, injured_id, 90, true,
                                            club, inbox));
  EXPECT_EQ(dynamics.injury_days, after);
  EXPECT_FALSE(
      game.getMedical().afterMatch(data, date, fit_id, 90, true, club, inbox))
      << "fit players are left to the normal injury model";
}

// ---------------------------------------------------------------------------
// Medical instructions and the load log
// ---------------------------------------------------------------------------

TEST(MedicalDeskTest, FlagsAreSetAndCleared)
{
  MedicalDesk desk;
  EXPECT_EQ(desk.flags(42), MEDICAL_FLAG_NONE);
  desk.setFlag(42, MEDICAL_FLAG_REST, true);
  desk.setFlag(42, MEDICAL_FLAG_LIMIT_MINUTES, true);
  EXPECT_TRUE(desk.isRested(42));
  EXPECT_TRUE(desk.hasFlag(42, MEDICAL_FLAG_LIMIT_MINUTES));
  desk.setFlag(42, MEDICAL_FLAG_REST, false);
  EXPECT_FALSE(desk.isRested(42));
  EXPECT_EQ(desk.flags(42), MEDICAL_FLAG_LIMIT_MINUTES);

  EXPECT_FALSE(MedicalCentre::substitutionDue(MEDICAL_FLAG_LIMIT_MINUTES, 59));
  EXPECT_TRUE(MedicalCentre::substitutionDue(MEDICAL_FLAG_LIMIT_MINUTES, 60));
  EXPECT_FALSE(MedicalCentre::substitutionDue(MEDICAL_FLAG_REST, 75));
}

TEST(MedicalDeskTest, TheAssistantLeavesRestedPlayersOut)
{
  const SlotCleanup slot{uniqueSlot(2)};
  GameController controller;
  const TeamID club = startCareer(controller, slot.slot);
  Game& game = *controller.getGame();
  const Lineup& lineup = controller.getTeamById(club)->get().getLineup();
  ASSERT_FALSE(lineup.getOutfieldPlayers().empty());
  const PlayerID rested = lineup.getOutfieldPlayers().front().player->getId();
  EXPECT_TRUE(controller.previewLineupFix(club, MatchType::LEAGUE).empty());

  game.getMedical().setFlag(rested, MEDICAL_FLAG_REST, true);
  // Rest is an instruction, not a rule: the player stays eligible.
  EXPECT_TRUE(
      controller.getIneligibleSelections(club, MatchType::LEAGUE).empty());
  const auto preview = controller.previewLineupFix(club, MatchType::LEAGUE);
  ASSERT_EQ(preview.size(), 1u);
  EXPECT_EQ(preview.front().first, rested);
  EXPECT_NE(preview.front().second, 0u);

  EXPECT_EQ(controller.autoFixLineup(club, MatchType::LEAGUE), 1u);
  const auto starters = lineup.starters();
  EXPECT_FALSE(std::ranges::any_of(starters, [rested](const Player* player)
                                   { return player->getId() == rested; }));
  EXPECT_FALSE(std::ranges::any_of(lineup.getReserves(),
                                   [rested](const Player* player)
                                   { return player->getId() == rested; }));

  // Lifting the instruction brings him back in place of his stand-in.
  game.getMedical().setFlag(rested, MEDICAL_FLAG_REST, false);
  const auto recall = controller.previewLineupFix(club, MatchType::LEAGUE);
  ASSERT_EQ(recall.size(), 1u);
  EXPECT_EQ(recall.front().first, preview.front().second);
  EXPECT_EQ(recall.front().second, rested);
  EXPECT_EQ(controller.autoFixLineup(club, MatchType::LEAGUE), 1u);
  EXPECT_TRUE(lineup.isStarter(rested));
  EXPECT_TRUE(lineup.getStandIns().empty());
  EXPECT_TRUE(controller.previewLineupFix(club, MatchType::LEAGUE).empty());
}

TEST(MedicalDeskTest, LoadChartKeepsFourWeeksOfDailyLoad)
{
  const SlotCleanup slot{uniqueSlot(3)};
  GameController controller;
  const TeamID club = startCareer(controller, slot.slot);
  const Game& game = *controller.getGame();
  const PlayerID player_id =
      controller.getPlayersForTeam(club)[6].get().getId();

  for (int day = 0; day < 10; ++day) controller.advanceDay();
  const std::int32_t today = dayOrdinal(controller.getCurrentDate());
  auto chart = game.getMedical().loadChart(player_id, today - 1);
  EXPECT_EQ(chart.back().day, today - 1);
  EXPECT_EQ(chart.front().day, today - MedicalCentre::LOAD_CHART_DAYS);
  EXPECT_TRUE(std::ranges::is_sorted(chart, {}, &LoadChartDay::day));
  EXPECT_EQ(std::ranges::count_if(chart, &LoadChartDay::recorded), 10);
  EXPECT_FALSE(game.getMedical().loadChart(player_id, today)[27].recorded)
      << "today is logged once the day is over";
  EXPECT_TRUE(std::ranges::any_of(chart, [](const LoadChartDay& day)
                                  { return day.recorded && day.load > 0.0f; }))
      << "pre-season training shows up";
  for (const LoadChartDay& day : chart)
  {
    if (!day.recorded) continue;
    EXPECT_GE(day.load, 0.0f);
    EXPECT_GT(day.ratio, 0.0f);
  }

  // The log is a ring: older days drop out after four weeks.
  for (int day = 0; day < 30; ++day) controller.advanceDay();
  const std::int32_t later = dayOrdinal(controller.getCurrentDate());
  chart = game.getMedical().loadChart(player_id, later - 1);
  EXPECT_EQ(std::ranges::count_if(chart, &LoadChartDay::recorded),
            MedicalCentre::LOAD_CHART_DAYS);
}

TEST(MedicalDeskTest, LoadZonesFollowTheWorkloadRatio)
{
  EXPECT_EQ(MedicalCentre::loadBand(1.0f), RiskBand::Low);
  EXPECT_EQ(MedicalCentre::loadBand(0.8f), RiskBand::Low);
  EXPECT_EQ(MedicalCentre::loadBand(1.3f), RiskBand::Low);
  EXPECT_EQ(MedicalCentre::loadBand(1.4f), RiskBand::Moderate);
  EXPECT_EQ(MedicalCentre::loadBand(0.6f), RiskBand::Moderate);
  EXPECT_EQ(MedicalCentre::loadBand(1.5f), RiskBand::High);
}

TEST(MedicalDeskTest, StaffWarnAboutAnInjuredStarterOnceAWeek)
{
  const SlotCleanup slot{uniqueSlot(4)};
  GameController controller;
  const TeamID club = startCareer(controller, slot.slot);
  controller.setAssistantFixesLineup(false);
  const Lineup& lineup = controller.getTeamById(club)->get().getLineup();
  ASSERT_FALSE(lineup.getOutfieldPlayers().empty());
  const PlayerID starter = lineup.getOutfieldPlayers().front().player->getId();
  PlayerDynamics& dynamics =
      controller.getGameData()->getPlayers().at(starter).mutableDynamics();
  dynamics.injury = InjuryType::BackSpasm;
  dynamics.injury_days = 30;

  const std::string name =
      controller.getGameData()->getPlayer(starter)->get().getName();
  const auto warnings = [&]
  {
    return std::ranges::count_if(
        controller.getInbox(),
        [&name](const InboxMessage& message)
        {
          return message.title_key == "MEDICAL_WARN_CARRYING_TITLE" &&
                 !message.args.empty() && message.args.front().contains(name);
        });
  };

  controller.advanceDay();
  EXPECT_EQ(warnings(), 1);
  for (int day = 0; day < 5; ++day) controller.advanceDay();
  EXPECT_EQ(warnings(), 1) << "no repeat within the week";
}

// ---------------------------------------------------------------------------
// Supporters
// ---------------------------------------------------------------------------

TEST(SupporterModelTest, ALosingRunLowersTheMoodAndWinningRaisesIt)
{
  SupporterFacts losing;
  losing.result_deltas.assign(8, -1.2f);
  SupporterFacts winning;
  winning.result_deltas.assign(8, 1.2f);
  const SupporterMood low = SupporterModel::evaluate(losing);
  const SupporterMood high = SupporterModel::evaluate(winning);
  const SupporterMood neutral = SupporterModel::evaluate({});
  EXPECT_FLOAT_EQ(neutral.index, SupporterModel::NEUTRAL);
  EXPECT_TRUE(neutral.reasons.empty());
  EXPECT_LT(low.index, neutral.index - 20.0f);
  EXPECT_GT(high.index, neutral.index + 20.0f);
  ASSERT_FALSE(low.reasons.empty());
  EXPECT_EQ(low.reasons.front().factor, SupporterFactor::Results);
  EXPECT_STREQ(SupporterModel::reasonKey(low.reasons.front()),
               "SUPPORTERS_REASON_RESULTS_BAD");
  EXPECT_STREQ(SupporterModel::reasonKey(high.reasons.front()),
               "SUPPORTERS_REASON_RESULTS_GOOD");
}

TEST(SupporterModelTest, ReasonsCoverDerbiesTicketsAndStars)
{
  SupporterFacts facts;
  facts.derby_points = {0, 3};  // Lost the latest derby, won the earlier one.
  facts.ticket_ratio = 1.6f;
  facts.star_sales = 1;
  facts.star_signings = 1;
  const SupporterMood mood = SupporterModel::evaluate(facts);
  const auto points = [&mood](SupporterFactor factor)
  {
    const auto found =
        std::ranges::find(mood.reasons, factor, &SupporterReason::factor);
    return found == mood.reasons.end() ? 0.0f : found->points;
  };
  EXPECT_LT(points(SupporterFactor::Derby), 0.0f)
      << "the latest derby weighs most";
  EXPECT_LT(points(SupporterFactor::TicketPrice), 0.0f);
  EXPECT_FLOAT_EQ(points(SupporterFactor::StarSale), -9.0f);
  EXPECT_FLOAT_EQ(points(SupporterFactor::StarSigning), 5.0f);
  EXPECT_TRUE(std::ranges::is_sorted(mood.reasons, std::greater<>(),
                                     [](const SupporterReason& reason)
                                     { return std::abs(reason.points); }));

  SupporterFacts cheap;
  cheap.ticket_ratio = 0.5f;
  const SupporterMood bargain = SupporterModel::evaluate(cheap);
  ASSERT_EQ(bargain.reasons.size(), 1u);
  EXPECT_LE(bargain.reasons.front().points, 6.0f)
      << "cheap tickets help a little";
  EXPECT_STREQ(SupporterModel::reasonKey(bargain.reasons.front()),
               "SUPPORTERS_REASON_TICKETS_CHEAP");
}

TEST(SupporterModelTest, IndexAndBoardNudgeStayInBounds)
{
  SupporterFacts worst;
  worst.result_deltas.assign(8, -3.0f);
  worst.derby_points = {0, 0, 0};
  worst.ticket_ratio = 5.0f;
  worst.star_sales = 5;
  SupporterFacts best;
  best.result_deltas.assign(8, 3.0f);
  best.derby_points = {3, 3, 3};
  best.ticket_ratio = 0.1f;
  best.star_signings = 5;
  EXPECT_GE(SupporterModel::evaluate(worst).index, 0.0f);
  EXPECT_EQ(SupporterModel::evaluate(worst).index, 0.0f);
  EXPECT_LE(SupporterModel::evaluate(best).index, 100.0f);

  float previous = -1.0f;
  for (int index = 0; index <= 100; ++index)
  {
    const float nudge = SupporterModel::boardNudge(static_cast<float>(index));
    EXPECT_LE(std::abs(nudge), SupporterModel::MAX_BOARD_NUDGE);
    EXPECT_GE(nudge, previous) << "monotonic at " << index;
    previous = nudge;
  }
  EXPECT_EQ(SupporterModel::boardNudge(50.0f), 0.0f);
  EXPECT_EQ(SupporterModel::boardNudge(60.0f), 0.0f) << "dead zone";
  EXPECT_FLOAT_EQ(SupporterModel::boardNudge(0.0f),
                  -SupporterModel::MAX_BOARD_NUDGE);
  EXPECT_FLOAT_EQ(SupporterModel::boardNudge(100.0f),
                  SupporterModel::MAX_BOARD_NUDGE);
  EXPECT_FLOAT_EQ(SupporterModel::boardNudge(150.0f),
                  SupporterModel::MAX_BOARD_NUDGE);
}

TEST(SupporterModelTest, MoodMovesHalfwayEachWeekAndANewClubStartsFresh)
{
  Supporters supporters;
  SupporterFacts losing;
  losing.result_deltas.assign(8, -1.5f);
  EXPECT_EQ(supporters.update(7, 100, losing), 0.0f) << "no nudge on day one";
  const float start = supporters.mood().index;
  EXPECT_FLOAT_EQ(start, SupporterModel::evaluate(losing).index);

  SupporterFacts happy;
  happy.result_deltas.assign(8, 1.5f);
  const float target = SupporterModel::evaluate(happy).index;
  const float nudge = supporters.update(7, 107, happy);
  EXPECT_FLOAT_EQ(supporters.mood().index, start + 0.5f * (target - start));
  EXPECT_FLOAT_EQ(nudge, SupporterModel::boardNudge(supporters.mood().index));
  EXPECT_EQ(supporters.updatedOn(), 107);

  EXPECT_EQ(supporters.update(9, 114, losing), 0.0f);
  EXPECT_EQ(supporters.team(), 9);
  EXPECT_FLOAT_EQ(supporters.mood().index, start);
}

TEST(SupporterModelTest, ACareerGetsAWeeklyMoodThatMovesTheBoard)
{
  const SlotCleanup slot{uniqueSlot(5)};
  GameController controller;
  const TeamID club = startCareer(controller, slot.slot);
  const Game& game = *controller.getGame();
  controller.advanceDay();
  EXPECT_EQ(game.getSupporters().team(), club)
      << "a new club gets a mood at once";
  const std::int32_t first = game.getSupporters().updatedOn();
  for (int day = 0; day < 14; ++day) controller.advanceDay();
  EXPECT_GT(game.getSupporters().updatedOn(), first);
  EXPECT_EQ(game.getSupporters().updatedOn() % 7, 0);
  const float index = game.getSupporters().mood().index;
  EXPECT_GE(index, 0.0f);
  EXPECT_LE(index, 100.0f);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

TEST(MedicalSupportersPersistenceTest, FlagsLoadLogAndMoodSurviveAReload)
{
  const SlotCleanup slot{uniqueSlot(6)};
  GameController controller;
  const TeamID club = startCareer(controller, slot.slot);
  const auto& squad = controller.getPlayersForTeam(club);
  const PlayerID rested = squad[1].get().getId();
  const PlayerID limited = squad[2].get().getId();
  controller.getGame()->getMedical().setFlag(rested, MEDICAL_FLAG_REST, true);
  controller.getGame()->getMedical().setFlag(limited,
                                             MEDICAL_FLAG_LIMIT_MINUTES, true);
  for (int day = 0; day < 9; ++day) controller.advanceDay();
  const std::int32_t today = dayOrdinal(controller.getCurrentDate());
  const auto chart =
      controller.getGame()->getMedical().loadChart(limited, today - 1);
  const Supporters supporters = controller.getGame()->getSupporters();
  ASSERT_EQ(supporters.team(), club);
  ASSERT_TRUE(controller.saveGame());

  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  const Game& game = *reloaded.getGame();
  EXPECT_TRUE(game.getMedical().isRested(rested));
  EXPECT_EQ(game.getMedical().flags(limited), MEDICAL_FLAG_LIMIT_MINUTES);
  const auto restored = game.getMedical().loadChart(limited, today - 1);
  for (std::size_t day = 0; day < chart.size(); ++day)
  {
    EXPECT_EQ(restored[day].recorded, chart[day].recorded) << day;
    EXPECT_NEAR(restored[day].load, chart[day].load, 1e-4f) << day;
    EXPECT_NEAR(restored[day].ratio, chart[day].ratio, 1e-4f) << day;
  }
  EXPECT_EQ(game.getSupporters().team(), supporters.team());
  EXPECT_EQ(game.getSupporters().updatedOn(), supporters.updatedOn());
  EXPECT_NEAR(game.getSupporters().mood().index, supporters.mood().index,
              1e-4f);
  ASSERT_EQ(game.getSupporters().mood().reasons.size(),
            supporters.mood().reasons.size());
  for (std::size_t index = 0; index < supporters.mood().reasons.size(); ++index)
  {
    EXPECT_EQ(game.getSupporters().mood().reasons[index].factor,
              supporters.mood().reasons[index].factor);
    EXPECT_NEAR(game.getSupporters().mood().reasons[index].points,
                supporters.mood().reasons[index].points, 0.01f);
  }
}
