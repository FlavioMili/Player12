// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <sqlite3.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <numeric>
#include <utility>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/player.h"
#include "model/training.h"
#include "model/world_rng.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 200'000 + getpid() % 100'000 * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

std::unique_ptr<GameController> makeWorld(int slot)
{
  Logger::init();
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot, WORLD_SEED);
  return controller;
}

StatsConfig testConfig()
{
  StatsConfig config;
  config.role_focus.try_emplace(
      "Striker", RoleFocus{{"Shooting", "Pace", "Dribbling", "Physicality"},
                           {0.4, 0.2, 0.2, 0.2}});
  config.role_focus.try_emplace(
      "Midfielder", RoleFocus{{"Passing", "Vision", "Stamina", "Dribbling"},
                              {0.3, 0.3, 0.2, 0.2}});
  config.role_focus.try_emplace(
      "Defender", RoleFocus{{"Defending", "Physicality", "Pace", "Vision"},
                            {0.4, 0.3, 0.15, 0.15}});
  config.role_focus.try_emplace(
      "Goalkeeper",
      RoleFocus{{"Goalkeeping", "Vision", "Physicality"}, {0.7, 0.2, 0.1}});
  return config;
}

Player makeStriker(std::uint8_t age)
{
  std::map<std::string, float> stats;
  for (const char* name : TrainingModel::attributeNames()) stats[name] = 50.0f;
  Player player(1, 1, "Test", "Striker", PlayerRole::ST, Language::EN, 1000, 0,
                age, 3, 180, Foot::Right, stats);
  player.setPotential(80.0f);
  return player;
}

float stat(const Player& player, const char* name)
{
  return player.getStats().at(name);
}

TeamTrainingPlan planFor(TrainingPreset preset)
{
  TeamTrainingPlan plan;
  plan.preset = preset;
  plan.slots = TrainingModel::presetMicrocycle(preset);
  plan.week_load = 0.0f;
  for (const TrainingSlot& slot : plan.slots)
    plan.week_load += TrainingModel::sessionLoad(slot, plan.intensity);
  return plan;
}

float averageCondition(const GameData& gamedata, TeamID team_id)
{
  float total = 0.0f;
  int count = 0;
  for (const auto& player : gamedata.getPlayersForTeam(team_id))
  {
    if (player.get().getDynamics().injury_days > 0) continue;
    total += player.get().getDynamics().condition;
    ++count;
  }
  return count > 0 ? total / static_cast<float>(count) : 0.0f;
}
}  // namespace

// ---------------------------------------------------------------------------
// Microcycle
// ---------------------------------------------------------------------------

TEST(TrainingModelTest, MicrocycleFollowsTheFixtureRhythm)
{
  using D = MicrocycleDay;
  // One match a week: MD+1, MD+2, MD-4, MD-3, MD-2, MD-1, match.
  EXPECT_EQ(TrainingModel::dayFor(1, 6, 0), D::MdPlus1);
  EXPECT_EQ(TrainingModel::dayFor(2, 5, 0), D::MdPlus2);
  EXPECT_EQ(TrainingModel::dayFor(3, 4, 0), D::MdMinus4);
  EXPECT_EQ(TrainingModel::dayFor(4, 3, 0), D::MdMinus3);
  EXPECT_EQ(TrainingModel::dayFor(5, 2, 0), D::MdMinus2);
  EXPECT_EQ(TrainingModel::dayFor(6, 1, 0), D::MdMinus1);
  EXPECT_FALSE(TrainingModel::dayFor(7, 0, 0).has_value());
  // Without fixtures the template still cycles with regular rest days.
  int rest_days = 0;
  for (std::int32_t day = 0; day < 28; ++day)
    rest_days += TrainingModel::dayFor(-1, -1, day) == D::MdPlus2 ? 1 : 0;
  EXPECT_EQ(rest_days, 7);

  // Two matches four days apart: recovery and activation only.
  EXPECT_TRUE(TrainingModel::isCongested(1, 3));
  EXPECT_FALSE(TrainingModel::isCongested(2, 5));
  TeamTrainingPlan plan = planFor(TrainingPreset::Fitness);
  const TrainingDay loading = TrainingModel::scheduleDay(plan, 2, 2, 0);
  EXPECT_TRUE(loading.congested);
  EXPECT_TRUE(loading.adjusted);
  EXPECT_EQ(loading.slot.session, SessionType::Recovery);
  EXPECT_EQ(TrainingModel::scheduleDay(plan, 3, 1, 0).slot.session,
            SessionType::MatchPrep);
  plan.auto_congestion = false;
  EXPECT_EQ(TrainingModel::scheduleDay(plan, 2, 2, 0).slot,
            plan.slots[static_cast<std::size_t>(D::MdMinus2)]);
}

TEST(TrainingModelTest, LoadsAndExposureAreCalibrated)
{
  // The Balanced week reproduces the calibrated 6 x 1.2 training hours.
  float exposure = 0.0f;
  for (const TrainingSlot& slot :
       TrainingModel::presetMicrocycle(TrainingPreset::Balanced))
    exposure +=
        TrainingModel::sessionInjuryExposure(slot, TrainingIntensity::Normal);
  EXPECT_NEAR(exposure, 6.0f, 1e-4f);

  // The loading day (MD-4) is the hardest, MD-1 the lightest training day.
  const Microcycle balanced =
      TrainingModel::presetMicrocycle(TrainingPreset::Balanced);
  const auto load = [&](MicrocycleDay day)
  {
    return TrainingModel::sessionLoad(
        balanced[static_cast<std::size_t>(std::to_underlying(day))],
        TrainingIntensity::Normal);
  };
  EXPECT_GT(load(MicrocycleDay::MdMinus4), load(MicrocycleDay::MdMinus3));
  EXPECT_GT(load(MicrocycleDay::MdMinus2), load(MicrocycleDay::MdMinus1));
  EXPECT_EQ(load(MicrocycleDay::MdPlus2), 0.0f);

  const float light = planFor(TrainingPreset::Light).week_load;
  const float fitness = planFor(TrainingPreset::Fitness).week_load;
  EXPECT_LT(light, 0.6f * TrainingModel::balancedWeekLoad());
  EXPECT_GT(fitness, TrainingModel::balancedWeekLoad());
  EXPECT_GT(TrainingModel::conditionDrain(1.0f, 40.0f),
            TrainingModel::conditionDrain(1.0f, 90.0f));
  EXPECT_GT(TrainingModel::recoveryShare(SessionType::Recovery), 0.0f);
}

TEST(TrainingModelTest, WorkloadSpikesRaiseRiskAndChronicLoadProtects)
{
  PlayerTrainingState steady;
  steady.acute = TrainingModel::typicalDailyLoad();
  steady.chronic = TrainingModel::typicalDailyLoad();
  EXPECT_FLOAT_EQ(TrainingModel::workloadRiskMultiplier(steady), 1.0f);
  EXPECT_EQ(TrainingModel::workloadRisk(steady, 95.0f), WorkloadRisk::Low);

  // A player back from a layoff: detrained, then thrown into full load.
  PlayerTrainingState returning = steady;
  for (int day = 0; day < 21; ++day) TrainingModel::rollDay(returning);
  for (int day = 0; day < 5; ++day)
  {
    returning.pending = 1.6f;
    TrainingModel::rollDay(returning);
  }
  EXPECT_GT(TrainingModel::workloadRatio(returning), 1.5f);
  EXPECT_EQ(TrainingModel::workloadRisk(returning, 95.0f), WorkloadRisk::High);
  const float spike = TrainingModel::workloadRiskMultiplier(returning);
  EXPECT_GT(spike, 1.0f);
  EXPECT_LE(spike, 1.5f);

  // A high, stable chronic load is mildly protective.
  PlayerTrainingState conditioned;
  conditioned.acute = 1.3f * TrainingModel::typicalDailyLoad();
  conditioned.chronic = 1.3f * TrainingModel::typicalDailyLoad();
  EXPECT_FLOAT_EQ(TrainingModel::workloadRiskMultiplier(conditioned), 0.9f);
  EXPECT_EQ(TrainingModel::workloadRisk(steady, 50.0f), WorkloadRisk::High);
}

TEST(TrainingModelTest, DevelopmentMultiplierIsBoundedAndOrdered)
{
  using enum TrainingPreset;
  const float balanced =
      TrainingModel::developmentMultiplier(planFor(Balanced), 20, 0.5f);
  EXPECT_NEAR(balanced, 1.0f, 1e-4f);
  const float light =
      TrainingModel::developmentMultiplier(planFor(Light), 20, 0.5f);
  const float fitness =
      TrainingModel::developmentMultiplier(planFor(Fitness), 20, 0.5f);
  EXPECT_LT(light, balanced);
  EXPECT_GT(fitness, balanced);
  EXPECT_GT(
      TrainingModel::developmentMultiplier(planFor(YouthDevelopment), 19, 0.5f),
      TrainingModel::developmentMultiplier(planFor(YouthDevelopment), 27,
                                           0.5f));
  // Better coaches help, within a narrow band.
  const float poor =
      TrainingModel::developmentMultiplier(planFor(Balanced), 20, 0.0f);
  const float elite =
      TrainingModel::developmentMultiplier(planFor(Balanced), 20, 1.0f);
  EXPECT_NEAR(poor, 0.9f, 1e-4f);
  EXPECT_NEAR(elite, 1.1f, 1e-4f);
}

TEST(TrainingModelTest, DirectedGrowthFollowsScheduleAndFocus)
{
  const StatsConfig config = testConfig();
  const TeamTrainingPlan balanced = planFor(TrainingPreset::Balanced);

  // Without a focus the schedule share keeps the overall gain intact.
  Player plain = makeStriker(20);
  const double before = plain.getOverall(config);
  const float left = TrainingModel::applyDirectedGrowth(
      plain, 1.0f, balanced, TrainingFocus::None, config);
  EXPECT_NEAR(left, 0.7f, 1e-5f);
  EXPECT_NEAR(plain.getOverall(config) - before, 0.3, 1e-4);

  // A physical focus pushes pace and stamina (not a striker attribute).
  Player focused = makeStriker(20);
  const float focus_left = TrainingModel::applyDirectedGrowth(
      focused, 1.0f, balanced, TrainingFocus::Physical, config);
  EXPECT_NEAR(focus_left, 0.35f, 1e-5f);
  EXPECT_GT(stat(focused, "Pace"), stat(plain, "Pace"));
  EXPECT_GT(stat(focused, "Stamina"), 50.0f);
  EXPECT_FLOAT_EQ(stat(plain, "Stamina"), 50.0f);

  // After 25 physical attributes no longer grow.
  Player veteran = makeStriker(28);
  const float veteran_left = TrainingModel::applyDirectedGrowth(
      veteran, 1.0f, balanced, TrainingFocus::Physical, config);
  EXPECT_NEAR(veteran_left, 0.7f, 1e-5f);
  EXPECT_FLOAT_EQ(stat(veteran, "Pace"), 50.0f);

  // Attributes outside the role stop at potential + 5.
  Player capped = makeStriker(20);
  auto stats = capped.getStats();
  stats["Passing"] = 85.0f;
  stats["Vision"] = 85.0f;
  capped.setStats(stats);
  EXPECT_NEAR(TrainingModel::applyDirectedGrowth(
                  capped, 1.0f, balanced, TrainingFocus::Playmaking, config),
              0.7f, 1e-5f);
  EXPECT_FLOAT_EQ(stat(capped, "Passing"), 85.0f);

  // Retraining as a defender works on the defender's attributes.
  const auto defender =
      TrainingModel::focusAttributeMix(TrainingFocus::RetrainDefender, config);
  EXPECT_GT(defender[4], 0.0f);  // Defending
  EXPECT_EQ(defender[1], 0.0f);  // Shooting
}

TEST(TrainingModelTest, FamiliarityReactsToTacticChanges)
{
  TacticSnapshot drilled;
  drilled.valid = true;
  drilled.sliders = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
  drilled.outfield = 10;
  for (std::uint8_t i = 0; i < 10; ++i)
    drilled.positions[i] = {0.1f * static_cast<float>(i), 0.5f};
  EXPECT_FLOAT_EQ(TrainingModel::familiarityLoss(drilled, drilled), 0.0f);
  EXPECT_FLOAT_EQ(TrainingModel::familiarityLoss(TacticSnapshot{}, drilled),
                  0.0f);

  TacticSnapshot pressing = drilled;
  pressing.sliders[0] = 1.0f;
  EXPECT_NEAR(TrainingModel::familiarityLoss(drilled, pressing), 0.075f, 1e-5f);

  TacticSnapshot reshaped = drilled;
  reshaped.positions[3] = {0.35f, 0.9f};
  reshaped.positions[4] = {0.45f, 0.1f};
  EXPECT_NEAR(TrainingModel::familiarityLoss(drilled, reshaped), 0.10f, 1e-5f);

  TacticSnapshot overhaul = drilled;
  overhaul.sliders = {0.0f, 1.0f, 0.0f, 1.0f, 0.0f};
  for (std::uint8_t i = 0; i < 10; ++i) overhaul.positions[i].y = 0.9f;
  EXPECT_FLOAT_EQ(TrainingModel::familiarityLoss(drilled, overhaul), 0.6f);

  EXPECT_GT(TrainingModel::familiarityGain(
                {SessionType::Tactical, TrainingIntensity::Normal}),
            TrainingModel::familiarityGain(
                {SessionType::MatchPrep, TrainingIntensity::Normal}));
  EXPECT_EQ(TrainingModel::familiarityGain(
                {SessionType::Fitness, TrainingIntensity::High}),
            0.0f);
}

TEST(TrainingModelTest, AssistantAdviceReflectsTheSquad)
{
  AdviceInputs fine;
  const auto calm = TrainingModel::advise(fine);
  ASSERT_EQ(calm.size(), 1u);
  EXPECT_EQ(calm.front().key, "ADVICE_ALL_GOOD");

  AdviceInputs tired;
  tired.average_condition = 74.0f;
  tired.tired_players = 9;
  tired.matches_next_week = 2;
  tired.auto_congestion = false;
  tired.missing_roles = {"STAFF_ROLE_PHYSIO"};
  const auto advice = TrainingModel::advise(tired);
  EXPECT_EQ(advice.front().key, "ADVICE_FATIGUED_LIGHT");
  EXPECT_EQ(advice.front().severity, AdviceSeverity::Warning);
  const auto has = [&](const char* key)
  {
    return std::ranges::any_of(
        advice, [&](const TrainingAdvice& item) { return item.key == key; });
  };
  EXPECT_TRUE(has("ADVICE_CONGESTION_AUTO"));
  EXPECT_TRUE(has("ADVICE_MISSING_STAFF"));
  EXPECT_FALSE(has("ADVICE_ALL_GOOD"));

  tired.preset = TrainingPreset::Light;
  EXPECT_EQ(TrainingModel::advise(tired).front().key, "ADVICE_FATIGUED_ROTATE");

  AdviceInputs spiking;
  spiking.spike_players = {"A. One", "B. Two"};
  spiking.familiarity = 30.0f;
  const auto warnings = TrainingModel::advise(spiking);
  EXPECT_EQ(warnings.front().key, "ADVICE_WORKLOAD_SPIKE");
  EXPECT_EQ(warnings.front().args.front(), "2");
  EXPECT_EQ(warnings[1].key, "ADVICE_FAMILIARITY_LOW");
}

// ---------------------------------------------------------------------------
// Live world
// ---------------------------------------------------------------------------

TEST(TrainingWorldTest, ScheduleShapesConditionFamiliarityAndDevelopment)
{
  const SlotCleanup slot{uniqueSlot(0)};
  auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID managed = controller->getTeams().front().get().getId();
  const TeamID other = controller->getTeams().back().get().getId();
  controller->selectManagedTeam(managed);
  ASSERT_NE(controller->getTrainingPlan(), nullptr);

  ASSERT_TRUE(controller->setTrainingPreset(TrainingPreset::Light));
  ASSERT_TRUE(controller->setTrainingIntensity(TrainingIntensity::Low));
  TeamTrainingPlan& hard = gamedata->getTraining().plan(other);
  hard.preset = TrainingPreset::Fitness;
  hard.slots = TrainingModel::presetMicrocycle(TrainingPreset::Fitness);
  hard.intensity = TrainingIntensity::High;

  float light_condition = 0.0f;
  float hard_condition = 0.0f;
  constexpr int DAYS = 21;
  for (int day = 0; day < DAYS; ++day)
  {
    controller->advanceDay();
    light_condition += averageCondition(*gamedata, managed);
    hard_condition += averageCondition(*gamedata, other);
  }
  EXPECT_GT(light_condition / DAYS, hard_condition / DAYS + 2.0f);
  EXPECT_LT(gamedata->getTraining().findPlan(managed)->last_week_load,
            gamedata->getTraining().findPlan(other)->last_week_load);

  // Development: the light schedule trains less than the hard one.
  const Player& young = [&]() -> const Player&
  {
    for (const auto& player : gamedata->getPlayersForTeam(managed))
      if (player.get().getAge() <= 21) return player.get();
    return gamedata->getPlayersForTeam(managed).front().get();
  }();
  const float light_quality = TrainingSystem::trainingQuality(*gamedata, young);
  ASSERT_TRUE(controller->setTrainingPreset(TrainingPreset::Fitness));
  gamedata->getTraining().plan(managed).week_load =
      gamedata->getTraining().findPlan(other)->last_week_load;
  EXPECT_GT(TrainingSystem::trainingQuality(*gamedata, young), light_quality);

  // Familiarity: a new tactic costs familiarity, tactical work rebuilds it.
  const float drilled = controller->getTacticalFamiliarity(managed);
  EXPECT_GT(drilled, 0.6f);
  StrategySliders sliders;
  sliders.pressing = 1.0f;
  sliders.riskTaking = 0.0f;
  sliders.offensiveBias = 1.0f;
  sliders.widthUsage = 0.0f;
  controller->getManagedTeam()->get().getStrategy().setAllSliders(sliders);
  controller->advanceDay();
  const float after_change = controller->getTacticalFamiliarity(managed);
  EXPECT_LT(after_change, drilled * 0.8f);
  ASSERT_TRUE(controller->setTrainingPreset(TrainingPreset::Defending));
  for (int day = 0; day < 21; ++day) controller->advanceDay();
  EXPECT_GT(controller->getTacticalFamiliarity(managed), after_change + 0.1f);

  // Workload, trend and preview are exposed for the Training screen.
  const auto preview = controller->getTrainingWeekPreview();
  ASSERT_EQ(preview.size(), 7u);
  EXPECT_EQ(preview.front().date, controller->getCurrentDate());
  const PlayerID first =
      gamedata->getPlayersForTeam(managed).front().get().getId();
  ASSERT_TRUE(
      controller->setPlayerTrainingFocus(first, TrainingFocus::Physical));
  EXPECT_EQ(controller->getPlayerWorkload(first).focus,
            TrainingFocus::Physical);
  EXPECT_GT(controller->getPlayerWorkload(first).chronic, 0.0f);
  const PlayerID foreign =
      gamedata->getPlayersForTeam(other).front().get().getId();
  EXPECT_FALSE(
      controller->setPlayerTrainingFocus(foreign, TrainingFocus::Physical));
  EXPECT_FALSE(controller->getTrainingAdvice().empty());
}

TEST(TrainingWorldTest, PlansFocusAndWorkloadSurviveSaveAndLoad)
{
  const SlotCleanup slot{uniqueSlot(1)};
  auto controller = makeWorld(slot.slot);
  const TeamID managed = controller->getTeams().front().get().getId();
  controller->selectManagedTeam(managed);
  // The default (balanced) delegation lets the assistant set intensity and
  // congestion every day; this test owns the schedule.
  ASSERT_TRUE(
      controller->setDutyOwner(Duty::TrainingSchedule, DutyOwner::Manager));
  const PlayerID player_id =
      controller->getPlayersForTeam(managed).front().get().getId();
  ASSERT_TRUE(controller->setTrainingPreset(TrainingPreset::Attacking));
  ASSERT_TRUE(controller->setTrainingSlot(
      MicrocycleDay::MdPlus2, {SessionType::Tactical, TrainingIntensity::Low}));
  ASSERT_TRUE(controller->setTrainingIntensity(TrainingIntensity::High));
  ASSERT_TRUE(controller->setCongestionAutoAdjust(false));
  ASSERT_TRUE(
      controller->setPlayerTrainingFocus(player_id, TrainingFocus::Playmaking));
  for (int day = 0; day < 5; ++day) controller->advanceDay();
  const TeamTrainingPlan saved = *controller->getTrainingPlan();
  const auto saved_workload = controller->getPlayerWorkload(player_id);
  EXPECT_EQ(saved.preset, TrainingPreset::Custom);
  controller->saveGame();

  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(slot.slot));
  const TeamTrainingPlan* loaded = controller->getTrainingPlan();
  ASSERT_NE(loaded, nullptr);
  EXPECT_EQ(loaded->preset, TrainingPreset::Custom);
  EXPECT_EQ(loaded->slots, saved.slots);
  EXPECT_EQ(loaded->intensity, TrainingIntensity::High);
  EXPECT_FALSE(loaded->auto_congestion);
  EXPECT_FLOAT_EQ(loaded->familiarity, saved.familiarity);
  EXPECT_FLOAT_EQ(loaded->week_load, saved.week_load);
  EXPECT_EQ(loaded->last_match_day, saved.last_match_day);
  EXPECT_TRUE(loaded->tactic.valid);
  EXPECT_EQ(loaded->tactic.outfield, saved.tactic.outfield);
  const auto workload = controller->getPlayerWorkload(player_id);
  EXPECT_EQ(workload.focus, TrainingFocus::Playmaking);
  EXPECT_FLOAT_EQ(workload.acute, saved_workload.acute);
  EXPECT_FLOAT_EQ(workload.chronic, saved_workload.chronic);
  EXPECT_FLOAT_EQ(workload.trend, saved_workload.trend);
}
