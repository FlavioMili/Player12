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
#include <atomic>
#include <format>
#include <latch>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "database/migrations/migrations.h"
#include "database/save_manager.h"
#include "global/global.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/staff.h"
#include "model/world_rng.h"

TEST(StaffCache, ConcurrentEffectsAndInvalidationKeepSnapshotsStable)
{
  StaffRoster roster;
  StaffMember coach;
  coach.id = 1;
  coach.team_id = 42;
  coach.role = StaffRole::AssistantManager;
  coach.attributes.fill(70);
  coach.wage = 1000;
  coach.contract_years = 2;
  roster.add(coach);
  std::atomic<bool> consistent{true};
  std::latch start(8);
  std::vector<std::jthread> readers;
  for (int thread = 0; thread < 8; ++thread)
    readers.emplace_back(
        [&, thread]
        {
          start.arrive_and_wait();
          for (int attempt = 0; attempt < 500; ++attempt)
          {
            if (thread == 0) roster.invalidate();
            if (roster.effects(42).weekly_payroll != 1000)
              consistent.store(false);
          }
        });
  readers.clear();
  EXPECT_TRUE(consistent.load());
  const auto& snapshot = roster.effects(42);
  roster.clear();
  EXPECT_EQ(snapshot.weekly_payroll, 1000);
  EXPECT_EQ(roster.effects(42).weekly_payroll, 0);
}

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 300'000 + (getpid() % 100'000) * 10 + offset;
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

StaffMember member(StaffRole role, std::uint8_t level)
{
  StaffMember staff;
  staff.role = role;
  staff.attributes.fill(level);
  staff.wage = 1000;
  staff.contract_years = 2;
  return staff;
}

std::vector<StaffMember> department(std::uint8_t level)
{
  std::vector<StaffMember> staff;
  for (std::size_t role = 0; role < STAFF_ROLE_COUNT; ++role)
    staff.push_back(member(static_cast<StaffRole>(role), level));
  return staff;
}

StaffEffects effectsOf(const std::vector<StaffMember>& staff)
{
  std::vector<const StaffMember*> pointers;
  for (const StaffMember& item : staff) pointers.push_back(&item);
  return StaffModel::computeEffects(pointers);
}

double averageRating(const GameController& controller, TeamID team_id)
{
  const auto staff = controller.getStaff(team_id);
  double total = 0.0;
  for (const StaffMember* item : staff) total += StaffModel::rating(*item);
  return staff.empty() ? 0.0 : total / static_cast<double>(staff.size());
}

std::int64_t staffLedger(const Team& team)
{
  std::int64_t total = 0;
  for (const FinanceTransaction& entry : team.getFinances().getLedger())
  {
    if (entry.category == FinanceCategory::Staff) total += entry.amount;
  }
  return total;
}
}  // namespace

TEST(StaffModelTest, BetterStaffGiveBoundedBetterEffects)
{
  const StaffEffects weak = effectsOf(department(30));
  const StaffEffects strong = effectsOf(department(90));
  const StaffEffects none = StaffModel::computeEffects({});

  EXPECT_GT(strong.training_quality, weak.training_quality);
  EXPECT_LT(strong.injury_prevention, weak.injury_prevention);
  EXPECT_LT(strong.layoff_multiplier, weak.layoff_multiplier);
  EXPECT_GT(strong.youth_potential_bonus, weak.youth_potential_bonus);
  EXPECT_GT(strong.scouting_accuracy, weak.scouting_accuracy);
  EXPECT_GT(strong.familiarity_rate, weak.familiarity_rate);
  for (const StaffEffects* effects : {&weak, &strong, &none})
  {
    EXPECT_GE(effects->injury_prevention, 0.88f);
    EXPECT_LE(effects->injury_prevention, 1.08f);
    EXPECT_GE(effects->layoff_multiplier, 0.85f);
    EXPECT_LE(effects->layoff_multiplier, 1.12f);
    EXPECT_GE(effects->youth_potential_bonus, -2.0f);
    EXPECT_LE(effects->youth_potential_bonus, 4.0f);
    EXPECT_GE(effects->scouting_accuracy, 0.0f);
    EXPECT_LE(effects->scouting_accuracy, 1.0f);
  }
  EXPECT_EQ(none.weekly_payroll, 0);
  EXPECT_EQ(strong.weekly_payroll,
            static_cast<std::int64_t>(STAFF_ROLE_COUNT) * 1000);

  // A bigger scouting network sees more; no scouts, little insight.
  std::vector one_scout = {member(StaffRole::Scout, 80)};
  std::vector network(5, member(StaffRole::Scout, 80));
  EXPECT_GT(effectsOf(network).scouting_accuracy,
            effectsOf(one_scout).scouting_accuracy);
  EXPECT_LT(
      effectsOf({member(StaffRole::AssistantManager, 80)}).scouting_accuracy,
      effectsOf(one_scout).scouting_accuracy);

  // Potential estimate noise follows the sourced d' anchors (0.8 - 2.1).
  EXPECT_NEAR(StaffModel::potentialErrorSd(0.0f), 9.0f / 0.8f, 1e-4f);
  EXPECT_NEAR(StaffModel::potentialErrorSd(1.0f), 9.0f / 2.1f, 1e-4f);

  // Better staff cost more.
  StaffMember cheap = member(StaffRole::FitnessCoach, 40);
  StaffMember star = member(StaffRole::FitnessCoach, 85);
  EXPECT_GT(StaffModel::marketWage(star), 3 * StaffModel::marketWage(cheap));
  EXPECT_GE(StaffModel::renewalWage(cheap), cheap.wage);
  EXPECT_EQ(StaffModel::severance(cheap), 52 * 1000);
}

TEST(StaffWorldTest, GenerationFollowsReputationAndIsDeterministic)
{
  const SlotCleanup slot_a{uniqueSlot(0)};
  const SlotCleanup slot_b{uniqueSlot(1)};
  const auto controller = makeWorld(slot_a.slot);
  const auto twin = makeWorld(slot_b.slot);

  const auto& teams = controller->getTeams();
  const auto [lowest, highest] = std::ranges::minmax_element(
      teams, {}, [](const auto& team) { return team.get().getReputation(); });
  const TeamID small = lowest->get().getId();
  const TeamID big = highest->get().getId();
  EXPECT_GT(controller->getStaff(big).size(),
            controller->getStaff(small).size());
  EXPECT_GT(averageRating(*controller, big),
            averageRating(*controller, small) + 10.0);
  EXPECT_GT(controller->getStaffEffects(big).weekly_payroll,
            controller->getStaffEffects(small).weekly_payroll);
  EXPECT_GT(controller->getScoutingAccuracy(big),
            controller->getScoutingAccuracy(small));
  for (const auto& team : teams)
  {
    const auto staff = controller->getStaff(team.get().getId());
    ASSERT_TRUE(std::ranges::any_of(
        staff, [](const StaffMember* item)
        { return item->role == StaffRole::AssistantManager; }));
  }
  EXPECT_EQ(controller->getStaffMarket().size(), StaffModel::MARKET_SIZE);

  const auto staff_a = controller->getStaff(big);
  const auto staff_b = twin->getStaff(big);
  ASSERT_EQ(staff_a.size(), staff_b.size());
  for (std::size_t i = 0; i < staff_a.size(); ++i)
  {
    EXPECT_EQ(staff_a[i]->name(), staff_b[i]->name());
    EXPECT_EQ(staff_a[i]->attributes, staff_b[i]->attributes);
    EXPECT_EQ(staff_a[i]->wage, staff_b[i]->wage);
  }
}

TEST(StaffWorldTest, HiringReleasingAndPayrollUseTheLedger)
{
  const SlotCleanup slot{uniqueSlot(2)};
  auto controller = makeWorld(slot.slot);
  const TeamID managed = controller->getTeams().front().get().getId();
  EXPECT_EQ(controller->hireStaff(1, 2),
            GameController::StaffActionResult::NoClub);
  controller->selectManagedTeam(managed);
  const Team& club = controller->getTeamById(managed)->get();

  // Release the fitness coaches: severance is booked immediately.
  const float before = controller->getStaffEffects(managed).coaching_fitness;
  std::int64_t severance = 0;
  for (const StaffMember* item : controller->getStaff(managed))
  {
    if (item->role != StaffRole::FitnessCoach) continue;
    const std::int64_t expected = StaffModel::severance(*item);
    severance += expected;
    const std::int64_t staff_before = staffLedger(club);
    ASSERT_EQ(controller->releaseStaff(item->id),
              GameController::StaffActionResult::Ok);
    EXPECT_EQ(staffLedger(club), staff_before - expected);
    EXPECT_EQ(item->team_id, FREE_AGENTS_TEAM_ID);
  }
  ASSERT_GT(severance, 0);
  EXPECT_EQ(controller->getGameData()->getStaff().roleCount(
                managed, StaffRole::FitnessCoach),
            0u);
  EXPECT_LT(controller->getStaffEffects(managed).coaching_fitness, before);
  EXPECT_EQ(club.getFinances().getBalance(), club.getFinances().ledgerTotal());

  // Hire the best fitness coach on the market.
  const StaffMember* best = nullptr;
  for (const StaffMember* candidate : controller->getStaffMarket())
  {
    if (candidate->role == StaffRole::FitnessCoach)
    {
      best = candidate;
      break;
    }
  }
  ASSERT_NE(best, nullptr);
  const StaffID hired = best->id;
  const std::uint32_t wage = controller->getStaffWageDemand(hired);
  EXPECT_EQ(controller->hireStaff(hired, 0),
            GameController::StaffActionResult::InvalidTerms);
  ASSERT_EQ(controller->hireStaff(hired, 3),
            GameController::StaffActionResult::Ok);
  EXPECT_EQ(controller->hireStaff(hired, 3),
            GameController::StaffActionResult::NotAvailable);
  const StaffMember* signed_member =
      controller->getGameData()->getStaff().find(hired);
  EXPECT_EQ(signed_member->team_id, managed);
  EXPECT_EQ(signed_member->wage, wage);
  EXPECT_EQ(signed_member->contract_years, 3);

  // Contract extension at the renewal wage.
  EXPECT_EQ(controller->extendStaffContract(hired, 3),
            GameController::StaffActionResult::InvalidTerms);
  ASSERT_EQ(controller->extendStaffContract(hired, 4),
            GameController::StaffActionResult::Ok);
  EXPECT_EQ(signed_member->contract_years, 4);
  EXPECT_GE(signed_member->wage, wage);

  // Role limits: the assistant manager post is single.
  const StaffMember* assistant = nullptr;
  for (const StaffMember* candidate : controller->getStaffMarket())
  {
    if (candidate->role == StaffRole::AssistantManager)
    {
      assistant = candidate;
      break;
    }
  }
  ASSERT_NE(assistant, nullptr);
  EXPECT_EQ(controller->hireStaff(assistant->id, 2),
            GameController::StaffActionResult::RoleFull);

  // The monthly staff cost carries the football staff payroll.
  const std::int64_t payroll =
      controller->getStaffEffects(managed).weekly_payroll;
  EXPECT_GT(payroll, 0);
  while (controller->getCurrentDate().day != 1) controller->advanceDay();
  const FinanceSummary month = controller->getFinanceSummary(
      managed, controller->getCurrentDate(), controller->getCurrentDate());
  EXPECT_LE(month.by_category[static_cast<std::size_t>(FinanceCategory::Staff)],
            -payroll * 52 / 12);

  // Hired staff are persisted.
  controller->saveGame();
  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(slot.slot));
  const StaffMember* reloaded =
      controller->getGameData()->getStaff().find(hired);
  ASSERT_NE(reloaded, nullptr);
  EXPECT_EQ(reloaded->team_id, managed);
  EXPECT_EQ(reloaded->contract_years, 4);
  EXPECT_EQ(
      controller->getStaffMarket().size() +
          [&]
          {
            std::size_t employed = 0;
            for (const auto& team : controller->getTeams())
              employed += controller->getStaff(team.get().getId()).size();
            return employed;
          }(),
      controller->getGameData()->getStaff().all().size());
}

TEST(StaffWorldTest, ContractsRunOutAndAiClubsRefill)
{
  const SlotCleanup slot{uniqueSlot(3)};
  const auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID managed = controller->getTeams().front().get().getId();
  // The most reputable other club can afford any candidate it needs.
  TeamID ai = 0;
  for (const auto& team : controller->getTeams())
  {
    const TeamID id = team.get().getId();
    if (id != managed &&
        (ai == 0 || team.get().getReputation() >
                        controller->getTeamById(ai)->get().getReputation()))
      ai = id;
  }
  StaffRoster& roster = gamedata->getStaff();
  for (const StaffMember* item : roster.clubStaff(managed))
    roster.find(item->id)->contract_years = 1;
  for (const StaffMember* item : roster.clubStaff(ai))
    roster.find(item->id)->contract_years = 1;
  const std::size_t ai_before = roster.clubStaff(ai).size();

  const auto departed = StaffModel::seasonEnd(*gamedata, managed, 2026);
  EXPECT_FALSE(departed.empty());
  EXPECT_TRUE(roster.clubStaff(managed).empty());
  // AI clubs mostly renew and fill every vacancy from the market.
  EXPECT_EQ(roster.clubStaff(ai).size(), ai_before);
  EXPECT_EQ(roster.market().size() >= StaffModel::MARKET_SIZE, true);

  // A legacy save without staff gets the same staff regenerated.
  const SlotCleanup legacy{uniqueSlot(4)};
  auto original = makeWorld(legacy.slot);
  const auto first_staff = original->getStaff(managed);
  const std::string first_name = first_staff.front()->name();
  const StaffID first_id = first_staff.front()->id;
  original->saveGame();
  original.reset();
  sqlite3* db = nullptr;
  ASSERT_EQ(sqlite3_open(RuntimePaths::savePath(legacy.slot).c_str(), &db),
            SQLITE_OK);
  ASSERT_EQ(sqlite3_exec(db,
                         "DROP TABLE Staff; DROP TABLE TeamTraining; DROP "
                         "TABLE PlayerTraining;",
                         nullptr, nullptr, nullptr),
            SQLITE_OK);
  sqlite3_close(db);
  GameController migrated;
  ASSERT_TRUE(migrated.loadGame(legacy.slot));
  ASSERT_FALSE(migrated.getStaff(managed).empty());
  EXPECT_EQ(migrated.getStaff(managed).front()->name(), first_name);
  EXPECT_EQ(migrated.getStaff(managed).front()->id, first_id);
  EXPECT_NE(migrated.getGameData()->getTraining().findPlan(managed), nullptr);
}

TEST(StaffPersistenceTest, DepartedStaffIdsAreNotReusedAfterReload)
{
  const SlotCleanup slot{uniqueSlot(5)};
  auto controller = makeWorld(slot.slot);
  StaffRoster& roster = controller->getGameData()->getStaff();
  // The newest candidate finds work elsewhere, as in the monthly refresh.
  StaffID newest = 0;
  for (const auto& [id, staff] : roster.all()) newest = std::max(newest, id);
  ASSERT_EQ(roster.find(newest)->team_id, FREE_AGENTS_TEAM_ID);
  ASSERT_TRUE(roster.remove(newest));
  const StaffID next = roster.peekNextId();
  ASSERT_GT(next, newest);
  ASSERT_TRUE(controller->saveGame());

  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  StaffRoster& restored = reloaded.getGameData()->getStaff();
  EXPECT_EQ(restored.peekNextId(), next);

  // The next market refresh brings the same new faces with fresh ids.
  const std::int32_t ordinal = dayOrdinal(GameDateValue(2025, 8, 1));
  StaffModel::refreshMarket(*controller->getGameData(), ordinal);
  StaffModel::refreshMarket(*reloaded.getGameData(), ordinal);
  const auto snapshot = [](const StaffRoster& staff)
  {
    std::map<StaffID, std::string> result;
    for (const auto& [id, member] : staff.all())
      result.try_emplace(
          id, std::format("{}/{}/{}", member.name(),
                          static_cast<int>(std::to_underlying(member.role)),
                          member.team_id));
    return result;
  };
  const auto uninterrupted = snapshot(roster);
  EXPECT_EQ(snapshot(restored), uninterrupted);
  EXPECT_FALSE(restored.find(newest));
  EXPECT_GT(restored.peekNextId(), next) << "the refresh hired new staff";

  // Saves from before the counter was stored derive it from the live ids.
  ASSERT_TRUE(reloaded.saveGame());
  sqlite3* db = nullptr;
  ASSERT_EQ(sqlite3_open(RuntimePaths::savePath(slot.slot).c_str(), &db),
            SQLITE_OK);
  ASSERT_EQ(sqlite3_exec(db,
                         "ALTER TABLE WorldState DROP COLUMN next_staff_id; "
                         "ALTER TABLE Fixtures DROP COLUMN kickoff; "
                         "DELETE FROM schema_migrations WHERE number >= 7; "
                         "UPDATE save_meta SET schema_version = 6;",
                         nullptr, nullptr, nullptr),
            SQLITE_OK);
  sqlite3_close(db);
  GameController legacy;
  ASSERT_TRUE(legacy.loadGame(slot.slot));
  StaffID highest = 0;
  for (const auto& [id, member] : legacy.getGameData()->getStaff().all())
    highest = std::max(highest, id);
  EXPECT_EQ(legacy.getGameData()->getStaff().peekNextId(), highest + 1);
  EXPECT_EQ(
      SaveManager::inspect(RuntimePaths::savePath(slot.slot)).schema_version,
      6);
  ASSERT_TRUE(legacy.saveGame());
  EXPECT_EQ(
      SaveManager::inspect(RuntimePaths::savePath(slot.slot)).schema_version,
      Migrations::currentSchemaVersion());
  // Also drops the pre-upgrade copy the load kept.
  SaveManager::deleteSave(RuntimePaths::savePath(slot.slot));
}
