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
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <latch>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/global.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/scout_expertise.h"
#include "model/scouting.h"
#include "model/world_tuning.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;
constexpr LeagueID OWN_LEAGUE = 1;      // Italian League
constexpr LeagueID LOWER_LEAGUE = 6;    // Italian Lower League (same country)
constexpr LeagueID FOREIGN_LEAGUE = 3;  // English League

int uniqueSlot(int offset)
{
  return 300'000 + getpid() % 100'000 * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

TeamID firstTeamOf(const GameController& controller, LeagueID league_id)
{
  auto ids = controller.getLeagueById(league_id)->get().getTeamIDs();
  std::ranges::sort(ids);
  return ids.front();
}

/** Career at the most reputable club of the own league (several scouts). */
std::unique_ptr<GameController> makeCareer(int slot)
{
  Logger::init();
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot, WORLD_SEED);
  TeamID best = firstTeamOf(*controller, OWN_LEAGUE);
  for (const TeamID id :
       controller->getLeagueById(OWN_LEAGUE)->get().getTeamIDs())
  {
    if (controller->getTeamById(id)->get().getReputation() >
        controller->getTeamById(best)->get().getReputation())
      best = id;
  }
  controller->selectManagedTeam(best);
  return controller;
}

std::vector<PlayerID> playersOf(const GameController& controller,
                                TeamID team_id)
{
  std::vector<PlayerID> ids;
  for (const Player& player : controller.getPlayersForTeam(team_id))
    ids.push_back(player.getId());
  std::ranges::sort(ids);
  return ids;
}

std::vector<PlayerID> leaguePlayers(const GameController& controller,
                                    LeagueID league_id)
{
  // Senior squads and academies: the scouting search covers both.
  std::vector<PlayerID> ids;
  for (const TeamID team_id :
       controller.getLeagueById(league_id)->get().getTeamIDs())
  {
    const auto team = playersOf(controller, team_id);
    ids.insert(ids.end(), team.begin(), team.end());
    const auto& academy =
        controller.getTeamById(team_id)->get().getAcademyIDs();
    ids.insert(ids.end(), academy.begin(), academy.end());
  }
  std::ranges::sort(ids);
  return ids;
}

const Player& playerOf(const GameController& controller, PlayerID id)
{
  return controller.getGameData()->getPlayer(id)->get();
}

double trueOverall(const GameController& controller, PlayerID id)
{
  return playerOf(controller, id).getOverall(controller.getStatsConfig());
}

ScoutingSystem& scoutingOf(GameController& controller)
{
  return controller.getGame()->getWorld().getScouting();
}

bool inboxHas(const GameController& controller, const char* title_key,
              PlayerID player_id)
{
  return std::ranges::any_of(controller.getInbox(),
                             [&](const InboxMessage& message)
                             {
                               return message.title_key == title_key &&
                                      message.player_id == player_id;
                             });
}
}  // namespace

TEST(ScoutingTest, ConcurrentConstQueriesKeepSnapshotsAndPriorsStable)
{
  const SlotCleanup slot{uniqueSlot(44)};
  auto controller = makeCareer(slot.slot);
  ScoutingSystem& scouting = scoutingOf(*controller);
  const auto targets =
      playersOf(*controller, firstTeamOf(*controller, FOREIGN_LEAGUE));
  ASSERT_FALSE(targets.empty());
  const PlayerID target = targets.front();
  const auto expected = scouting.row(target);
  ASSERT_TRUE(expected.has_value());
  const auto expected_count = scouting.getScouts().size();
  ASSERT_GT(expected_count, 0U);
  auto state = scouting.getState();
  scouting.restore(
      std::move(state));  // Cold prior cache for the parallel reads.
  std::atomic<bool> consistent{true};
  std::latch start(8);
  std::vector<std::jthread> readers;
  for (int thread = 0; thread < 8; ++thread)
    readers.emplace_back(
        [&]
        {
          start.arrive_and_wait();
          for (int attempt = 0; attempt < 20; ++attempt)
          {
            const auto row = scouting.row(target);
            if (!row.has_value() || row->overall != expected->overall ||
                scouting.getScouts().size() != expected_count ||
                scouting.scoutSummaries().size() != expected_count)
              consistent.store(false);
          }
        });
  readers.clear();
  EXPECT_TRUE(consistent.load());
  const auto& snapshot = scouting.getScouts();
  scouting.setScoutProvider([](TeamID) { return std::vector<ScoutProfile>{}; });
  EXPECT_EQ(snapshot.size(), expected_count);
  EXPECT_TRUE(scouting.getScouts().empty());
}

TEST(ScoutingTest, BaselineKnowledgeFollowsTheRelationToTheClub)
{
  const SlotCleanup slot{uniqueSlot(0)};
  auto controller = makeCareer(slot.slot);
  const TeamID own = controller->getManagedTeam()->get().getId();
  TeamID rival = 0;
  for (const TeamID id :
       controller->getLeagueById(OWN_LEAGUE)->get().getTeamIDs())
  {
    if (id != own) rival = id;
  }

  EXPECT_FLOAT_EQ(
      controller->getScoutingKnowledge(playersOf(*controller, own)[0]),
      ScoutingTuning::OWN_KNOWLEDGE);
  EXPECT_FLOAT_EQ(
      controller->getScoutingKnowledge(playersOf(*controller, rival)[0]),
      ScoutingTuning::SAME_LEAGUE_KNOWLEDGE);
  EXPECT_FLOAT_EQ(controller->getScoutingKnowledge(
                      leaguePlayers(*controller, LOWER_LEAGUE)[0]),
                  ScoutingTuning::SAME_COUNTRY_KNOWLEDGE);
  EXPECT_FLOAT_EQ(controller->getScoutingKnowledge(
                      leaguePlayers(*controller, FOREIGN_LEAGUE)[0]),
                  ScoutingTuning::FOREIGN_KNOWLEDGE);
  EXPECT_GE(controller->getScouts().size(), 2U);
  for (const ScoutProfile& scout : controller->getScouts())
  {
    EXPECT_FALSE(scout.name.empty());
    EXPECT_GE(scout.judging_ability, 1);
    EXPECT_LE(scout.judging_ability, 100);
    const ScoutExpertise expertise = controller->getScoutExpertise(scout.id);
    EXPECT_EQ(expertise.nationality, scout.nationality);
    EXPECT_TRUE(speaks(expertise.languages, nativeLanguage(scout.nationality)));
  }
}

TEST(ScoutingTest, FacingAnOpponentTeachesAboutHisPlayers)
{
  const SlotCleanup slot{uniqueSlot(7)};
  auto controller = makeCareer(slot.slot);
  const TeamID own = controller->getManagedTeam()->get().getId();
  controller->advanceToNextManagedFixture();
  const GameDateValue today = controller->getCurrentDate();
  for (const Match& match :
       controller->getGame()->getCalendar().getMatchesForDate(today))
  {
    if (match.getHomeTeamId() != own && match.getAwayTeamId() != own) continue;
    const TeamID opponent = match.getHomeTeamId() == own
                                ? match.getAwayTeamId()
                                : match.getHomeTeamId();
    const PlayerID rival = playersOf(*controller, opponent).front();
    const float before = controller->getScoutingKnowledge(rival);
    ASSERT_TRUE(controller->setMatchResult(today, match.getHomeTeamId(),
                                           match.getAwayTeamId(), 1, 1));
    EXPECT_GT(controller->getScoutingKnowledge(rival), before + 1.0f);
    return;
  }
  FAIL() << "No managed fixture found";
}

// Hidden-value safety: the API the screens use returns estimates for other
// clubs' players and exact values only for the managed squad.
TEST(ScoutingTest, UiApiReturnsEstimatesNotTrueValues)
{
  const SlotCleanup slot{uniqueSlot(1)};
  auto controller = makeCareer(slot.slot);
  const auto foreign = leaguePlayers(*controller, FOREIGN_LEAGUE);

  int differing = 0;
  int attribute_differing = 0;
  for (const PlayerID id : foreign)
  {
    const auto view = controller->getScoutedView(id);
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->own);
    EXPECT_EQ(view->knowledge, 5);
    if (std::abs(view->overall - trueOverall(*controller, id)) > 0.5)
      ++differing;
    const auto& stats = playerOf(*controller, id).getStats();
    ASSERT_EQ(view->attributes.size(), stats.size());
    for (const ScoutedAttribute& attribute : view->attributes)
    {
      if (std::abs(attribute.estimate - stats.at(attribute.name)) > 0.5f)
        ++attribute_differing;
      EXPECT_LE(attribute.low, attribute.estimate);
      EXPECT_GE(attribute.high, attribute.estimate);
    }
    EXPECT_LE(view->potential_low, view->potential_high);
    EXPECT_GE(view->potential_low, view->overall - 1e-3f);
    // Displayed values never flicker between calls.
    const auto again = controller->getScoutedView(id);
    EXPECT_FLOAT_EQ(again->overall, view->overall);
    EXPECT_FLOAT_EQ(again->potential_low, view->potential_low);
    EXPECT_FLOAT_EQ(again->potential_high, view->potential_high);
  }
  EXPECT_GT(differing, static_cast<int>(foreign.size() * 3 / 4));
  EXPECT_GT(attribute_differing,
            static_cast<int>(
                foreign.size() *
                playerOf(*controller, foreign[0]).getStats().size() * 3 / 4));

  // The search sorts by the same estimates the profile shows.
  ScoutSearchFilter filter;
  filter.league_id = FOREIGN_LEAGUE;
  filter.limit = 0;
  const auto rows = controller->searchScoutedPlayers(filter);
  ASSERT_EQ(rows.size(), foreign.size());
  bool true_order_differs = false;
  for (std::size_t index = 0; index < rows.size(); ++index)
  {
    EXPECT_FLOAT_EQ(rows[index].overall,
                    controller->getScoutedView(rows[index].player_id)->overall);
    if (index == 0) continue;
    EXPECT_GE(rows[index - 1].overall, rows[index].overall);
    if (trueOverall(*controller, rows[index - 1].player_id) <
        trueOverall(*controller, rows[index].player_id))
      true_order_differs = true;
  }
  EXPECT_TRUE(true_order_differs);

  // Own players are known exactly.
  const TeamID own = controller->getManagedTeam()->get().getId();
  for (const PlayerID id : playersOf(*controller, own))
  {
    const auto view = controller->getScoutedView(id);
    EXPECT_TRUE(view->own);
    EXPECT_NEAR(view->overall, trueOverall(*controller, id), 1e-3);
    for (const ScoutedAttribute& attribute : view->attributes)
      EXPECT_FLOAT_EQ(attribute.estimate,
                      playerOf(*controller, id).getStats().at(attribute.name));
  }
  // The profile's potential range comes from the same model.
  const auto potential = controller->getPotentialEstimate(foreign[0]);
  EXPECT_FLOAT_EQ(potential.low,
                  controller->getScoutedView(foreign[0])->potential_low);
}

TEST(ScoutingTest, EstimatesConvergeWithKnowledgeAndDecayBack)
{
  const SlotCleanup slot{uniqueSlot(2)};
  auto controller = makeCareer(slot.slot);
  ScoutingSystem& scouting = scoutingOf(*controller);
  const auto foreign = leaguePlayers(*controller, FOREIGN_LEAGUE);

  const auto attributeError = [&]
  {
    double total = 0.0;
    for (const PlayerID id : foreign)
    {
      const auto& stats = playerOf(*controller, id).getStats();
      const auto view = scouting.view(id);
      for (const ScoutedAttribute& attribute : view->attributes)
        total += std::abs(attribute.estimate - stats.at(attribute.name));
    }
    return total;
  };
  const auto overallError = [&]
  {
    double total = 0.0;
    for (const PlayerID id : foreign)
      total +=
          std::abs(scouting.view(id)->overall - trueOverall(*controller, id));
    return total;
  };
  const auto potentialWidth = [&]
  {
    double total = 0.0;
    for (const PlayerID id : foreign)
    {
      const auto view = *scouting.view(id);
      total += view.potential_high - view.potential_low;
    }
    return total;
  };

  const double overall_before = overallError();
  const double width_before = potentialWidth();
  double previous = attributeError();
  // With more observation the estimates move steadily towards the truth.
  for (int step = 0; step < 20; ++step)
  {
    for (const PlayerID id : foreign) scouting.observe(id, 10.0f, nullptr);
    const double current = attributeError();
    EXPECT_LE(current, previous + 1e-6);
    previous = current;
  }
  for (const PlayerID id : foreign) EXPECT_GE(scouting.view(id)->knowledge, 80);
  EXPECT_LT(overallError(), overall_before * 0.5);
  EXPECT_LT(potentialWidth(), width_before);

  // Knowledge fades slowly back to the baseline without scouting.
  const PlayerID id = foreign.front();
  const float known = scouting.knowledgeOf(id);
  Inbox inbox;
  GameDateValue date = controller->getCurrentDate();
  for (int day = 0; day < 100; ++day)
  {
    date.nextDay();
    scouting.onDayAdvanced(date, inbox);
  }
  EXPECT_NEAR(scouting.knowledgeOf(id),
              known - 100.0f * ScoutingTuning::DAILY_DECAY, 0.01f);
  for (int day = 0; day < 2000; ++day)
  {
    date.nextDay();
    scouting.onDayAdvanced(date, inbox);
  }
  EXPECT_FLOAT_EQ(scouting.knowledgeOf(id), ScoutingTuning::FOREIGN_KNOWLEDGE);
}

TEST(ScoutingTest, PlayerAssignmentCostsMoneyAndFilesAGradedReport)
{
  const SlotCleanup slot{uniqueSlot(3)};
  auto controller = makeCareer(slot.slot);
  const Team& team = controller->getManagedTeam()->get();
  const PlayerID target = leaguePlayers(*controller, FOREIGN_LEAGUE)[3];
  const ScoutProfile scout = controller->getScouts().front();

  const int64_t cost =
      controller->getScoutAssignmentCost(ScoutTargetKind::Player, target, 10);
  EXPECT_EQ(cost, 10 * ScoutingTuning::DAILY_COST_FOREIGN);
  const int64_t balance = team.getFinances().getBalance();
  ASSERT_EQ(controller->startScoutAssignment(scout.id, ScoutTargetKind::Player,
                                             target, 10),
            ScoutAssignError::None);
  EXPECT_EQ(team.getFinances().getBalance(), balance - cost);
  EXPECT_EQ(team.getFinances().getLedger().back().category,
            FinanceCategory::Staff);
  EXPECT_EQ(team.getFinances().getLedger().back().amount, -cost);

  // Validation.
  EXPECT_EQ(controller->startScoutAssignment(scout.id, ScoutTargetKind::League,
                                             FOREIGN_LEAGUE, 10),
            ScoutAssignError::ScoutBusy);
  const std::uint32_t other = controller->getScouts()[1].id;
  EXPECT_EQ(controller->startScoutAssignment(other, ScoutTargetKind::League,
                                             FOREIGN_LEAGUE, 1),
            ScoutAssignError::InvalidDuration);
  EXPECT_EQ(controller->startScoutAssignment(
                other, ScoutTargetKind::Player,
                playersOf(*controller, team.getId())[0], 10),
            ScoutAssignError::InvalidTarget);
  EXPECT_EQ(controller->startScoutAssignment(other, ScoutTargetKind::Country,
                                             LOWER_LEAGUE, 10),
            ScoutAssignError::InvalidTarget);
  EXPECT_EQ(controller->startScoutAssignment(
                0xFFFFFFF0U, ScoutTargetKind::FreeAgents, 0, 10),
            ScoutAssignError::UnknownScout);

  const float knowledge_before = controller->getScoutingKnowledge(target);
  for (int day = 0; day < 10; ++day) controller->advanceDay();
  EXPECT_GT(controller->getScoutingKnowledge(target), knowledge_before + 25.0f);
  ASSERT_FALSE(controller->getScoutReports().empty());
  const ScoutReport& report = controller->getScoutReports().back();
  EXPECT_EQ(report.player_id, target);
  EXPECT_EQ(report.scout_name, scout.name);
  EXPECT_EQ(report.date, controller->getCurrentDate());
  EXPECT_GT(report.confidence, 0);
  EXPECT_LE(report.potential_low, report.potential_high);
  const int reasons = static_cast<int>(report.fits_need) +
                      static_cast<int>(report.affordable) +
                      static_cast<int>(report.available);
  EXPECT_EQ(report.grade, reasons == 3   ? ScoutGrade::A
                          : reasons == 2 ? ScoutGrade::B
                                         : ScoutGrade::C);
  EXPECT_TRUE(inboxHas(*controller, "SCOUT_MSG_REPORT_TITLE", target));
  EXPECT_EQ(controller->getScoutedView(target)->latest_report_id, report.id);
  EXPECT_EQ(scoutingOf(*controller).activeAssignment(scout.id), nullptr);
}

TEST(ScoutingTest, CoverageAssignmentFollowsTheRecruitmentFocus)
{
  const SlotCleanup slot{uniqueSlot(4)};
  auto controller = makeCareer(slot.slot);
  RecruitmentFocus focus;
  focus.role = PlayerRole::ST;
  focus.min_age = 17;
  focus.max_age = 34;
  const std::uint32_t focus_id = controller->saveRecruitmentFocus(focus);
  ASSERT_NE(focus_id, 0U);
  focus.id = focus_id;
  focus.max_age = 30;
  EXPECT_EQ(controller->saveRecruitmentFocus(focus), focus_id);
  ASSERT_EQ(controller->getRecruitmentFocuses().size(), 1U);
  EXPECT_EQ(controller->getRecruitmentFocuses()[0].max_age, 30);

  const ScoutProfile scout = controller->getScouts().front();
  ASSERT_EQ(controller->startScoutAssignment(scout.id, ScoutTargetKind::League,
                                             FOREIGN_LEAGUE, 30),
            ScoutAssignError::None);
  for (int day = 0; day < 30; ++day) controller->advanceDay();

  const ScoutAssignment& assignment = controller->getScoutAssignments().back();
  EXPECT_TRUE(assignment.finished);
  // Players watched per day scale with the scout's effectiveness.
  const float multiplier =
      controller
          ->getScoutEffectiveness(scout.id, ScoutTargetKind::League,
                                  FOREIGN_LEAGUE)
          .multiplier;
  const auto per_day = static_cast<int>(
      std::lround(static_cast<float>(ScoutingTuning::COVERAGE_PLAYERS_PER_DAY) *
                  multiplier));
  EXPECT_NEAR(assignment.players_observed, 30 * per_day, 30);
  EXPECT_GT(assignment.reports_filed, 0);
  int strikers = 0;
  for (const ScoutReport& report : controller->getScoutReports())
  {
    const Player& player = playerOf(*controller, report.player_id);
    EXPECT_EQ(player.getRole(), PlayerRole::ST);
    EXPECT_LE(player.getAge(), 30);
    ++strikers;
  }
  EXPECT_EQ(strikers, assignment.reports_filed);
  EXPECT_TRUE(std::ranges::any_of(
      controller->getInbox(), [](const InboxMessage& message)
      { return message.title_key == "SCOUT_MSG_DONE_TITLE"; }));

  ScoutSearchFilter filter;
  filter.focus_matches_only = true;
  for (const ScoutedPlayerRow& row : controller->searchScoutedPlayers(filter))
  {
    EXPECT_EQ(row.role, PlayerRole::ST);
    EXPECT_TRUE(row.matches_focus);
  }
  EXPECT_TRUE(controller->removeRecruitmentFocus(focus_id));
  EXPECT_TRUE(controller->getRecruitmentFocuses().empty());
}

TEST(ScoutingTest, ShortlistRaisesAlertsOnStatusChanges)
{
  const SlotCleanup slot{uniqueSlot(5)};
  auto controller = makeCareer(slot.slot);
  const auto foreign = leaguePlayers(*controller, FOREIGN_LEAGUE);
  PlayerID listed_id = 0;
  PlayerID injured_id = 0;
  auto& players = controller->getGameData()->getPlayers();
  for (const PlayerID id : foreign)
  {
    if (const Player& player = players.at(id);
        player.getTransferStatus() == TransferStatus::Listed ||
        player.getDynamics().injury_days > 0 || player.getContractYears() <= 1)
      continue;
    if (listed_id == 0)
      listed_id = id;
    else if (injured_id == 0)
      injured_id = id;
  }
  ASSERT_NE(injured_id, 0U);
  EXPECT_TRUE(controller->addToShortlist(listed_id));
  EXPECT_TRUE(controller->addToShortlist(injured_id));
  EXPECT_FALSE(controller->addToShortlist(listed_id));
  EXPECT_FALSE(controller->addToShortlist(
      playersOf(*controller, controller->getManagedTeam()->get().getId())[0]));
  EXPECT_TRUE(controller->isShortlisted(listed_id));

  players.at(listed_id).setTransferStatus(TransferStatus::Listed);
  players.at(injured_id).mutableDynamics().injury_days = 30;
  controller->advanceDay();
  EXPECT_TRUE(inboxHas(*controller, "SCOUT_ALERT_LISTED_TITLE", listed_id));
  EXPECT_TRUE(inboxHas(*controller, "SCOUT_ALERT_INJURED_TITLE", injured_id));
  const auto alerts = std::ranges::count_if(
      controller->getInbox(), [](const InboxMessage& message)
      { return message.title_key.starts_with("SCOUT_ALERT_"); });
  controller->advanceDay();
  EXPECT_EQ(std::ranges::count_if(
                controller->getInbox(), [](const InboxMessage& message)
                { return message.title_key.starts_with("SCOUT_ALERT_"); }),
            alerts)
      << "An unchanged status must not alert again";

  const auto comparison = controller->compareShortlistWithSquad();
  ASSERT_EQ(comparison.size(), ScoutingSystem::ROLE_COUNT);
  const auto role = static_cast<std::size_t>(
      std::to_underlying(players.at(listed_id).getRole()));
  EXPECT_EQ(comparison[role].role, players.at(listed_id).getRole());
  EXPECT_NE(comparison[role].candidate_id, 0U);
  EXPECT_TRUE(controller->removeFromShortlist(listed_id));
  EXPECT_FALSE(controller->isShortlisted(listed_id));
}

TEST(ScoutingTest, StateSurvivesSaveAndLoad)
{
  const SlotCleanup slot{uniqueSlot(6)};
  auto controller = makeCareer(slot.slot);
  const auto foreign = leaguePlayers(*controller, FOREIGN_LEAGUE);
  RecruitmentFocus focus;
  focus.role = PlayerRole::CB;
  focus.max_fee = 5'000'000;
  focus.min_ability = 55;
  controller->saveRecruitmentFocus(focus);
  controller->addToShortlist(foreign[5]);
  const std::uint32_t scout_id = controller->getScouts().front().id;
  ASSERT_EQ(controller->startScoutAssignment(scout_id, ScoutTargetKind::Player,
                                             foreign[7], 5),
            ScoutAssignError::None);
  ASSERT_EQ(controller->startScoutAssignment(controller->getScouts()[1].id,
                                             ScoutTargetKind::League,
                                             FOREIGN_LEAGUE, 20),
            ScoutAssignError::None);
  for (int day = 0; day < 6; ++day) controller->advanceDay();
  ASSERT_FALSE(controller->getScoutReports().empty());

  const float knowledge = controller->getScoutingKnowledge(foreign[7]);
  const auto view = *controller->getScoutedView(foreign[7]);
  const auto reports = controller->getScoutReports();
  const auto assignments = controller->getScoutAssignments();
  controller->saveGame();

  auto reloaded = std::make_unique<GameController>();
  ASSERT_TRUE(reloaded->loadGame(slot.slot));
  EXPECT_FLOAT_EQ(reloaded->getScoutingKnowledge(foreign[7]), knowledge);
  const auto view_after = *reloaded->getScoutedView(foreign[7]);
  EXPECT_FLOAT_EQ(view_after.overall, view.overall);
  EXPECT_FLOAT_EQ(view_after.potential_low, view.potential_low);
  ASSERT_EQ(reloaded->getScoutReports().size(), reports.size());
  for (std::size_t index = 0; index < reports.size(); ++index)
  {
    const ScoutReport& a = reports[index];
    const ScoutReport& b = reloaded->getScoutReports()[index];
    EXPECT_EQ(a.id, b.id);
    EXPECT_EQ(a.date, b.date);
    EXPECT_EQ(a.player_id, b.player_id);
    EXPECT_EQ(a.assignment_id, b.assignment_id);
    EXPECT_EQ(a.scout_name, b.scout_name);
    EXPECT_EQ(a.grade, b.grade);
    EXPECT_EQ(a.fits_need, b.fits_need);
    EXPECT_EQ(a.affordable, b.affordable);
    EXPECT_EQ(a.available, b.available);
    EXPECT_FLOAT_EQ(a.overall, b.overall);
    EXPECT_FLOAT_EQ(a.overall_low, b.overall_low);
    EXPECT_FLOAT_EQ(a.overall_high, b.overall_high);
    EXPECT_EQ(a.seen, b.seen);
    EXPECT_EQ(a.estimated_fee, b.estimated_fee);
  }
  ASSERT_EQ(reloaded->getScoutAssignments().size(), assignments.size());
  for (std::size_t index = 0; index < assignments.size(); ++index)
  {
    const ScoutAssignment& a = assignments[index];
    const ScoutAssignment& b = reloaded->getScoutAssignments()[index];
    EXPECT_EQ(a.id, b.id);
    EXPECT_EQ(a.kind, b.kind);
    EXPECT_EQ(a.target_id, b.target_id);
    EXPECT_EQ(a.days_done, b.days_done);
    EXPECT_EQ(a.finished, b.finished);
    EXPECT_EQ(a.cost, b.cost);
  }
  ASSERT_EQ(reloaded->getRecruitmentFocuses().size(), 1U);
  EXPECT_EQ(reloaded->getRecruitmentFocuses()[0].role, PlayerRole::CB);
  EXPECT_EQ(reloaded->getRecruitmentFocuses()[0].max_fee, 5'000'000);
  EXPECT_EQ(reloaded->getRecruitmentFocuses()[0].min_ability, 55);
  EXPECT_TRUE(reloaded->isShortlisted(foreign[5]));

  // Knowledge keeps fading identically after the reload.
  controller->advanceDay();
  reloaded->advanceDay();
  EXPECT_FLOAT_EQ(reloaded->getScoutingKnowledge(foreign[7]),
                  controller->getScoutingKnowledge(foreign[7]));
}

TEST(ScoutingTest, UnscoutedEstimatesAreRealisticAndRangesAreHonest)
{
  const SlotCleanup slot{uniqueSlot(8)};
  auto controller = makeCareer(slot.slot);
  const ScoutingSystem& scouting = scoutingOf(*controller);
  const auto foreign = leaguePlayers(*controller, FOREIGN_LEAGUE);
  ASSERT_GT(foreign.size(), 100u);

  std::vector<double> errors;
  size_t inside = 0;
  for (const PlayerID id : foreign)
  {
    const auto row = *scouting.row(id);
    ASSERT_LT(row.knowledge, ScoutingTuning::RANGE_DISPLAY_KNOWLEDGE);
    EXPECT_LE(row.overall_low, row.overall);
    EXPECT_GE(row.overall_high, row.overall);
    EXPECT_NEAR(0.5f * (row.overall_low + row.overall_high), row.overall, 0.5f)
        << "the estimate is the centre of its range";
    const double truth = trueOverall(*controller, id);
    errors.push_back(std::abs(row.overall - truth));
    if (truth >= row.overall_low - 0.5 && truth <= row.overall_high + 0.5)
      ++inside;
    if (row.age >= 30)
      EXPECT_LE(row.potential_high,
                std::max(row.overall, row.overall_high) + 1e-3f)
          << "veterans have no headroom beyond their current ability";
    const auto view = *scouting.view(id);
    EXPECT_FLOAT_EQ(view.overall_low, row.overall_low);
    EXPECT_FLOAT_EQ(view.overall_high, row.overall_high);
  }
  std::ranges::sort(errors);
  // Barely known players: a few points off is normal, ten is not.
  const double p90 = errors[errors.size() * 9 / 10];
  EXPECT_LT(p90, 8.0);
  EXPECT_GT(p90, 1.0) << "without scouting the estimate is uncertain";
  // The 80% ranges should contain the truth most of the time.
  const double coverage =
      static_cast<double>(inside) / static_cast<double>(foreign.size());
  EXPECT_GT(coverage, 0.65);

  // Cost of one world-wide search (the transfer screen runs two on entry).
  ScoutSearchFilter filter;
  filter.limit = 1000;
  const auto started = std::chrono::steady_clock::now();
  const auto rows = controller->searchScoutedPlayers(filter);
  const double ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - started)
                        .count();
  std::cout << "[scouting-estimates] p90 error=" << p90
            << " range coverage=" << coverage << " search rows=" << rows.size()
            << " ms=" << ms << "\n";
}

namespace
{
constexpr LeagueID ITALY = 1;
constexpr LeagueID ENGLAND = 3;
constexpr LeagueID BRAZIL = 11;
constexpr std::array<LeagueID, 11> WORLD_COUNTRIES = {1, 2, 3,  4,  5, 7,
                                                      8, 9, 10, 11, 12};

float multiplierFor(const ScoutExpertise& expertise, std::uint8_t judging,
                    LeagueID country, LeagueID league = 0)
{
  std::uint32_t country_days = 0;
  for (const auto& [id, days] : expertise.league_days)
    if (id == country) country_days += days;
  const std::array<TargetCountry, 1> targets = {
      TargetCountry{country, 1.0f, country_days}};
  std::uint32_t league_days = 0;
  if (const auto found = expertise.league_days.find(league);
      found != expertise.league_days.end())
    league_days = found->second;
  return ScoutExpertiseModel::compute(expertise, judging, targets, league,
                                      league_days)
      .multiplier;
}

bool hasFactor(const ScoutEffectiveness& effectiveness, EffectFactorKind kind)
{
  return std::ranges::any_of(effectiveness.factors,
                             [kind](const EffectFactor& factor)
                             { return factor.kind == kind; });
}
}  // namespace

TEST(ScoutExpertiseTest, HomeLanguageAndExperienceRaiseEffectiveness)
{
  ScoutExpertise italian;
  italian.nationality = Language::IT;
  italian.languages = languageBit(SpokenLanguage::Italian);

  const float neutral = multiplierFor(italian, 50, ENGLAND);
  const float home = multiplierFor(italian, 50, ITALY);
  EXPECT_GT(home, 1.0f);
  EXPECT_LT(neutral, 1.0f) << "no English: language barrier";

  ScoutExpertise bilingual = italian;
  bilingual.languages |= languageBit(SpokenLanguage::English);
  const float speaks_english = multiplierFor(bilingual, 50, ENGLAND);
  EXPECT_GT(speaks_english, neutral);
  EXPECT_GT(speaks_english, 1.0f);

  ScoutExpertise experienced = bilingual;
  experienced.league_days[ENGLAND] = 400;
  const float knows_league = multiplierFor(experienced, 50, ENGLAND, ENGLAND);
  EXPECT_GT(knows_league, speaks_english);

  // Judging ability matters, far from home and without the language hurts.
  EXPECT_GT(multiplierFor(italian, 90, ITALY), home);
  EXPECT_LT(multiplierFor(italian, 50, BRAZIL), neutral);

  // Bounded both ways.
  EXPECT_FLOAT_EQ(multiplierFor(italian, 1, BRAZIL),
                  ScoutExpertiseModel::MIN_MULTIPLIER);
  experienced.league_days[ITALY] = 2000;
  EXPECT_LE(multiplierFor(experienced, 100, ENGLAND, ENGLAND),
            ScoutExpertiseModel::MAX_MULTIPLIER);
  EXPECT_LE(multiplierFor(experienced, 100, ITALY, ITALY),
            ScoutExpertiseModel::MAX_MULTIPLIER);

  // The breakdown names the reasons.
  const std::array<TargetCountry, 1> england = {
      TargetCountry{ENGLAND, 1.0f, 400}};
  const ScoutEffectiveness breakdown =
      ScoutExpertiseModel::compute(experienced, 70, england, ENGLAND, 400);
  EXPECT_TRUE(hasFactor(breakdown, EffectFactorKind::KnowsLeague));
  EXPECT_TRUE(hasFactor(breakdown, EffectFactorKind::SpeaksLanguage));
  EXPECT_TRUE(hasFactor(breakdown, EffectFactorKind::Judging));
  EXPECT_FALSE(hasFactor(breakdown, EffectFactorKind::FarFromHome));
  float total = 1.0f;
  for (const EffectFactor& factor : breakdown.factors) total += factor.delta;
  EXPECT_NEAR(breakdown.multiplier,
              std::clamp(total, ScoutExpertiseModel::MIN_MULTIPLIER,
                         ScoutExpertiseModel::MAX_MULTIPLIER),
              1e-5f);
}

TEST(ScoutExpertiseTest, BackgroundsArePlausibleAndDeterministic)
{
  int italians_with_english = 0;
  int brazilians_with_spanish = 0;
  int italians_with_home_spell = 0;
  constexpr int SCOUTS = 300;
  for (std::uint32_t id = 1; id <= SCOUTS; ++id)
  {
    const ScoutExpertise italian =
        ScoutExpertiseModel::generate(Language::IT, 42, id, WORLD_COUNTRIES);
    const ScoutExpertise again =
        ScoutExpertiseModel::generate(Language::IT, 42, id, WORLD_COUNTRIES);
    EXPECT_EQ(italian.languages, again.languages);
    EXPECT_EQ(italian.league_days, again.league_days);
    EXPECT_TRUE(speaks(italian.languages, SpokenLanguage::Italian));
    EXPECT_LE(italian.league_days.size(), 3u);
    if (speaks(italian.languages, SpokenLanguage::English))
      ++italians_with_english;
    if (italian.league_days.contains(ITALY)) ++italians_with_home_spell;

    const ScoutExpertise brazilian =
        ScoutExpertiseModel::generate(Language::BR, 42, id, WORLD_COUNTRIES);
    EXPECT_TRUE(speaks(brazilian.languages, SpokenLanguage::Portuguese));
    if (speaks(brazilian.languages, SpokenLanguage::Spanish))
      ++brazilians_with_spanish;
  }
  EXPECT_GT(italians_with_english, SCOUTS * 2 / 5);
  EXPECT_LT(italians_with_english, SCOUTS * 9 / 10);
  EXPECT_GT(brazilians_with_spanish, SCOUTS / 2);
  EXPECT_GT(italians_with_home_spell, SCOUTS * 7 / 10);
}

TEST(ScoutingTest, ScoutSummariesGroupScoutsByWhatTheyAreDoing)
{
  const SlotCleanup slot{uniqueSlot(9)};
  auto controller = makeCareer(slot.slot);
  const auto summaries = controller->getScoutSummaries();
  ASSERT_GE(summaries.size(), 2u);
  for (const ScoutSummary& summary : summaries)
    EXPECT_EQ(summary.status, ScoutStatus::IdleNew);

  const std::uint32_t busy = summaries[0].profile.id;
  const std::uint32_t fresh = summaries[1].profile.id;
  const PlayerID target = leaguePlayers(*controller, FOREIGN_LEAGUE)[2];
  ASSERT_EQ(controller->startScoutAssignment(busy, ScoutTargetKind::Player,
                                             target, 3),
            ScoutAssignError::None);
  const auto statusOf = [&](std::uint32_t id)
  {
    for (const ScoutSummary& summary : controller->getScoutSummaries())
      if (summary.profile.id == id) return summary;
    return ScoutSummary{};
  };
  EXPECT_EQ(statusOf(busy).status, ScoutStatus::OnAssignment);
  EXPECT_NE(statusOf(busy).active_assignment_id, 0u);
  EXPECT_EQ(statusOf(fresh).status, ScoutStatus::IdleNew);

  for (int day = 0; day < 3; ++day) controller->advanceDay();
  const ScoutSummary done = statusOf(busy);
  EXPECT_EQ(done.status, ScoutStatus::IdleWithHistory);
  EXPECT_EQ(done.finished_assignments, 1u);
  EXPECT_EQ(done.total_reports, 1u);
  EXPECT_EQ(done.unread_reports, 1u);
  EXPECT_EQ(controller->getUnreadScoutReportCount(), 1u);
  EXPECT_TRUE(controller->markScoutReportsSeen(busy));
  EXPECT_EQ(controller->getUnreadScoutReportCount(), 0u);
  EXPECT_EQ(statusOf(busy).unread_reports, 0u);
  EXPECT_FALSE(controller->markScoutReportsSeen(busy));
  EXPECT_EQ(statusOf(fresh).status, ScoutStatus::IdleNew);
}

TEST(ScoutingTest, ExperienceAccruesAndSurvivesSaveAndLoad)
{
  const SlotCleanup slot{uniqueSlot(10)};
  auto controller = makeCareer(slot.slot);
  const std::uint32_t scout = controller->getScouts().front().id;
  const auto daysIn = [](const ScoutExpertise& expertise, LeagueID league)
  {
    const auto found = expertise.league_days.find(league);
    return found == expertise.league_days.end() ? 0 : int{found->second};
  };
  const ScoutExpertise before = controller->getScoutExpertise(scout);
  const float effectiveness_before =
      controller
          ->getScoutEffectiveness(scout, ScoutTargetKind::League,
                                  FOREIGN_LEAGUE)
          .multiplier;
  ASSERT_EQ(controller->startScoutAssignment(scout, ScoutTargetKind::League,
                                             FOREIGN_LEAGUE, 20),
            ScoutAssignError::None);
  for (int day = 0; day < 20; ++day) controller->advanceDay();

  const ScoutExpertise after = controller->getScoutExpertise(scout);
  EXPECT_EQ(daysIn(after, FOREIGN_LEAGUE), daysIn(before, FOREIGN_LEAGUE) + 20);
  EXPECT_EQ(after.languages, before.languages);
  const ScoutEffectiveness effectiveness = controller->getScoutEffectiveness(
      scout, ScoutTargetKind::League, FOREIGN_LEAGUE);
  if (effectiveness_before < ScoutExpertiseModel::MAX_MULTIPLIER)
    EXPECT_GT(effectiveness.multiplier, effectiveness_before);
  EXPECT_TRUE(hasFactor(effectiveness, EffectFactorKind::KnowsLeague));

  controller->saveGame();
  auto reloaded = std::make_unique<GameController>();
  ASSERT_TRUE(reloaded->loadGame(slot.slot));
  const ScoutExpertise restored = reloaded->getScoutExpertise(scout);
  EXPECT_EQ(restored.nationality, after.nationality);
  EXPECT_EQ(restored.languages, after.languages);
  EXPECT_EQ(restored.league_days, after.league_days);
  EXPECT_FLOAT_EQ(reloaded
                      ->getScoutEffectiveness(scout, ScoutTargetKind::League,
                                              FOREIGN_LEAGUE)
                      .multiplier,
                  effectiveness.multiplier);
  // Unread reports stay unread across the reload.
  EXPECT_EQ(reloaded->getUnreadScoutReportCount(),
            controller->getUnreadScoutReportCount());
}

TEST(ScoutingTest, ContinentAssignmentsCoverEveryCountryOnIt)
{
  const SlotCleanup slot{uniqueSlot(11)};
  auto controller = makeCareer(slot.slot);
  const std::uint32_t scout = controller->getScouts().front().id;
  const auto south_america =
      static_cast<std::uint32_t>(std::to_underlying(Continent::SouthAmerica));
  EXPECT_EQ(controller->getScoutAssignmentCost(ScoutTargetKind::Region,
                                               south_america, 10),
            10 * ScoutingTuning::DAILY_COST_FOREIGN);
  EXPECT_EQ(controller->getScoutAssignmentCost(
                ScoutTargetKind::Region,
                static_cast<std::uint32_t>(Continent::COUNT), 10),
            0);
  ASSERT_EQ(controller->startScoutAssignment(scout, ScoutTargetKind::Region,
                                             south_america, 10),
            ScoutAssignError::None);
  for (int day = 0; day < 10; ++day) controller->advanceDay();
  const ScoutExpertise expertise = controller->getScoutExpertise(scout);
  const ScoutingSystem& scouting = scoutingOf(*controller);
  bool worked_there = false;
  for (const auto& [league, days] : expertise.league_days)
    if (countryContinent(scouting.countryOf(league)) == Continent::SouthAmerica)
      worked_there = true;
  EXPECT_TRUE(worked_there);
}

TEST(ScoutingTest, ReportsOfOlderSavesAreMigratedAsRead)
{
  const SlotCleanup slot{uniqueSlot(12)};
  auto controller = makeCareer(slot.slot);
  const std::uint32_t scout = controller->getScouts().front().id;
  ASSERT_EQ(controller->startScoutAssignment(
                scout, ScoutTargetKind::Player,
                leaguePlayers(*controller, FOREIGN_LEAGUE)[4], 3),
            ScoutAssignError::None);
  for (int day = 0; day < 3; ++day) controller->advanceDay();
  ASSERT_EQ(controller->getUnreadScoutReportCount(), 1u);
  const float overall = controller->getScoutReports().back().overall;
  controller->saveGame();
  controller.reset();

  // A save written before the report details existed.
  sqlite3* db = nullptr;
  ASSERT_EQ(sqlite3_open(RuntimePaths::savePath(slot.slot).c_str(), &db),
            SQLITE_OK);
  const char* legacy_schema =
      "BEGIN; CREATE TABLE LegacyScoutReports AS SELECT "
      "id, game_date, player_id, assignment_id, scout_id, scout_name, "
      "knowledge, confidence, overall, potential_low, potential_high, "
      "estimated_fee, grade, reasons FROM ScoutReports; "
      "DROP TABLE ScoutReports; ALTER TABLE LegacyScoutReports RENAME TO "
      "ScoutReports; COMMIT;";
  ASSERT_EQ(sqlite3_exec(db, legacy_schema, nullptr, nullptr, nullptr),
            SQLITE_OK)
      << sqlite3_errmsg(db);
  sqlite3_close(db);

  auto reloaded = std::make_unique<GameController>();
  ASSERT_TRUE(reloaded->loadGame(slot.slot));
  ASSERT_EQ(reloaded->getScoutReports().size(), 1u);
  const ScoutReport& report = reloaded->getScoutReports().back();
  EXPECT_TRUE(report.seen);
  EXPECT_EQ(reloaded->getUnreadScoutReportCount(), 0u);
  EXPECT_FLOAT_EQ(report.overall_low, overall);
  EXPECT_FLOAT_EQ(report.overall_high, overall);
}

TEST(ScoutingTest, MarketValueChangesGraduallyWithAgeAndContract)
{
  // A 29-year-old entering the last year of his deal: one season older and
  // one contract year shorter costs him at most ~30% of his value.
  const double before = Player::valueFor(72.0, 72.0, 29, 2.0);
  const double after = Player::valueFor(72.0, 72.0, 30, 1.0);
  EXPECT_LT(after, before);
  EXPECT_GE(after / before, 0.70);
  // Running down the final year keeps a real price.
  EXPECT_GE(Player::valueFor(72.0, 72.0, 30, 0.5) / after, 0.75);

  // The potential premium fades between 22 and 25: no birthday cliff.
  double previous = Player::valueFor(66.0, 78.0, 21, 4.0);
  for (int age = 22; age <= 26; ++age)
  {
    const double value = Player::valueFor(66.0, 78.0, age, 4.0);
    EXPECT_GE(value / previous, 0.75) << "age " << age;
    previous = value;
  }
  EXPECT_GT(Player::valueFor(66.0, 78.0, 22, 4.0),
            Player::valueFor(66.0, 66.0, 22, 4.0));
  EXPECT_DOUBLE_EQ(Player::valueFor(66.0, 78.0, 25, 4.0),
                   Player::valueFor(66.0, 66.0, 25, 4.0));

  // Own players are valued with the same model.
  const SlotCleanup slot{uniqueSlot(13)};
  const auto controller = makeCareer(slot.slot);
  const auto& config = controller->getStatsConfig();
  const TeamID club = firstTeamOf(*controller, OWN_LEAGUE);
  for (const PlayerID id : playersOf(*controller, club))
  {
    const Player& player = playerOf(*controller, id);
    player.updateMarketValue(config);
    EXPECT_EQ(player.getMarketValue(),
              static_cast<std::uint32_t>(Player::valueFor(
                  player.getOverall(config),
                  static_cast<double>(player.getPotential()), player.getAge(),
                  static_cast<double>(player.getContractYears()))));
  }
}

TEST(ScoutingTest, AffordabilityUsesTheCashAwareBudget)
{
  const SlotCleanup slot{uniqueSlot(14)};
  auto controller = makeCareer(slot.slot);
  auto gamedata = controller->getGameData();
  Team& team =
      gamedata->getTeams().at(controller->getManagedTeam()->get().getId());
  Finances& finances = team.getFinances();
  // The board would sanction any fee, but the cash only covers the payroll
  // reserve: nothing a scout finds is affordable.
  finances.setTransferBudget(500'000'000);
  finances.setWageBudget(finances.getCurrentWageSpending(*gamedata, team) +
                         5'000'000);
  const std::int64_t reserve = WorldTuning::Finance::CASH_RESERVE_WEEKS *
                               finances.getCurrentWageSpending(*gamedata, team);
  const PlayerID target = leaguePlayers(*controller, FOREIGN_LEAGUE)[3];
  ASSERT_EQ(
      controller->startScoutAssignment(controller->getScouts().front().id,
                                       ScoutTargetKind::Player, target, 10),
      ScoutAssignError::None);
  finances.record(controller->getCurrentDate(), FinanceCategory::Investment,
                  reserve - finances.getBalance());
  for (int day = 0; day < 10; ++day)
  {
    // Keep the cash at the reserve while the days' costs and income land.
    controller->advanceDay();
    finances.record(controller->getCurrentDate(), FinanceCategory::Investment,
                    reserve - finances.getBalance());
  }
  ASSERT_FALSE(controller->getScoutReports().empty());
  const ScoutReport& report = controller->getScoutReports().back();
  ASSERT_EQ(report.player_id, target);
  ASSERT_GT(report.estimated_fee, 0);
  EXPECT_LE(report.estimated_fee, finances.getTransferBudget());
  EXPECT_EQ(controller->transferBudgetForTeam(team.getId()), 0u);
  EXPECT_FALSE(report.affordable);
}
