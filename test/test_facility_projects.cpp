// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <memory>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/board.h"
#include "model/calendar.h"
#include "model/facility_projects.h"
#include "model/inbox.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 7'000'000 + getpid() % 100'000 * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

std::unique_ptr<GameController> makeCareer(int slot)
{
  Logger::init();
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot, WORLD_SEED);
  controller->selectManagedTeam(controller->getTeams().front().get().getId());
  return controller;
}

ProjectRequestContext healthyClub()
{
  ProjectRequestContext context;
  context.confidence = 70.0f;
  context.balance = 100'000'000;
  context.weekly_payroll = 500'000;
  context.reputation = 70;
  context.current_level = 60;
  return context;
}
}  // namespace

TEST(FacilityProjects, QuotesScaleWithLevelAndSize)
{
  const ProjectQuote training = BoardModel::quoteProject(
      FacilityProjectType::TrainingGround, 60, 30'000, 70, 100'000'000.0, 0);
  EXPECT_EQ(training.amount, 10u);
  EXPECT_EQ(training.days, 180u);
  EXPECT_EQ(training.cost, 9'600'000);
  EXPECT_EQ(BoardModel::quoteProject(FacilityProjectType::TrainingGround, 95,
                                     30'000, 70, 1e8, 0)
                .amount,
            5u);
  EXPECT_EQ(BoardModel::quoteProject(FacilityProjectType::MedicalCentre, 100,
                                     30'000, 70, 1e8, 0)
                .amount,
            0u);
  const ProjectQuote stadium = BoardModel::quoteProject(
      FacilityProjectType::StadiumExpansion, 0, 20'000, 50, 1e8, 6'321);
  EXPECT_EQ(stadium.amount, 6'000u);
  EXPECT_EQ(stadium.disruption, 2'000u);
  EXPECT_EQ(stadium.cost, 6'000 * (2'500 + 60 * 50));
  EXPECT_GT(stadium.days, 240u);
  // At most half the current ground at once.
  EXPECT_EQ(BoardModel::quoteProject(FacilityProjectType::StadiumExpansion, 0,
                                     10'000, 50, 1e8, 50'000)
                .amount,
            5'000u);
}

TEST(FacilityProjects, BoardVerdicts)
{
  ProjectQuote quote;
  quote.type = FacilityProjectType::TrainingGround;
  quote.amount = 10;
  quote.cost = 5'000'000;
  EXPECT_EQ(BoardModel::reviewProject(quote, healthyClub()),
            ProjectVerdict::Approved);
  ProjectRequestContext context = healthyClub();
  context.same_type_running = true;
  EXPECT_EQ(BoardModel::reviewProject(quote, context),
            ProjectVerdict::AlreadyRunning);
  context = healthyClub();
  context.running_projects = BoardModel::MAX_RUNNING_PROJECTS;
  EXPECT_EQ(BoardModel::reviewProject(quote, context),
            ProjectVerdict::TooManyProjects);
  context = healthyClub();
  context.cooling_down = true;
  EXPECT_EQ(BoardModel::reviewProject(quote, context),
            ProjectVerdict::Cooldown);
  context = healthyClub();
  context.confidence = 30.0f;
  EXPECT_EQ(BoardModel::reviewProject(quote, context),
            ProjectVerdict::LowConfidence);
  context = healthyClub();
  context.current_level = 96;
  EXPECT_EQ(BoardModel::reviewProject(quote, context),
            ProjectVerdict::NotNeeded);
  context = healthyClub();
  context.balance = 10'000'000;  // 16 weeks of payroll = 8M
  EXPECT_EQ(BoardModel::reviewProject(quote, context),
            ProjectVerdict::CannotAfford);
  quote.amount = 0;
  EXPECT_EQ(BoardModel::reviewProject(quote, healthyClub()),
            ProjectVerdict::AtMaximum);
  EXPECT_FLOAT_EQ(BoardModel::medicalLayoffMultiplier(50), 1.0f);
  EXPECT_FLOAT_EQ(BoardModel::medicalLayoffMultiplier(20), 1.0f);
  EXPECT_NEAR(BoardModel::medicalLayoffMultiplier(100), 0.85f, 1e-5f);
}

TEST(FacilityProjects, ProjectsRunPayAndComplete)
{
  const SlotCleanup slot{uniqueSlot(0)};
  auto controller = makeCareer(slot.slot);
  Team& club = controller->getManagedTeam()->get();
  const TeamID managed = club.getId();
  // A rich owner: the cash reserve never gets in the way here.
  club.getFinances().record(controller->getCurrentDate(),
                            FinanceCategory::Investment, 500'000'000);
  const uint8_t training_before = club.getProfile().training_facilities;
  const uint32_t capacity_before = club.getProfile().stadium_capacity;

  const ProjectQuote training =
      controller->getProjectQuote(FacilityProjectType::TrainingGround);
  const size_t ledger_before = club.getFinances().getLedger().size();
  const ProjectVerdict first =
      controller->requestFacilityProject(FacilityProjectType::TrainingGround);
  if (training.amount == 0)
  {
    EXPECT_EQ(first, ProjectVerdict::AtMaximum);
    return;
  }
  ASSERT_EQ(first, ProjectVerdict::Approved);
  ASSERT_EQ(club.getFinances().getLedger().size(), ledger_before + 1);
  EXPECT_EQ(club.getFinances().getLedger().back().amount,
            -std::llround(static_cast<double>(training.cost) * 0.25));
  EXPECT_EQ(
      controller->requestFacilityProject(FacilityProjectType::TrainingGround),
      ProjectVerdict::AlreadyRunning);

  const ProjectQuote stadium =
      controller->getProjectQuote(FacilityProjectType::StadiumExpansion, 4'000);
  ASSERT_EQ(controller->requestFacilityProject(
                FacilityProjectType::StadiumExpansion, 4'000),
            ProjectVerdict::Approved);
  EXPECT_EQ(club.getProfile().stadium_capacity,
            capacity_before - stadium.disruption);
  EXPECT_EQ(
      controller->requestFacilityProject(FacilityProjectType::MedicalCentre),
      ProjectVerdict::TooManyProjects);
  ASSERT_EQ(controller->getFacilityProjects().size(), 2u);

  // Round trip mid-works.
  ASSERT_TRUE(controller->saveGame());
  {
    GameController reloaded;
    ASSERT_TRUE(reloaded.loadGame(slot.slot));
    const auto projects = reloaded.getFacilityProjects();
    ASSERT_EQ(projects.size(), 2u);
    EXPECT_EQ(projects[0].cost, training.cost);
    EXPECT_EQ(projects[1].disruption, stadium.disruption);
    EXPECT_EQ(reloaded.getManagedTeam()->get().getProfile().stadium_capacity,
              capacity_before - stadium.disruption);
  }

  // Run the works to their end, day by day, on the projects alone.
  auto* game = controller->getGame();
  FacilityProjects& projects = game->getWorld().getFacilityProjects();
  auto gamedata = controller->getGameData();
  GameDateValue day = controller->getCurrentDate();
  const GameDateValue finish = controller->getFacilityProjects()[1].end;
  while (!(finish < day))
  {
    day = SeasonCalendar::addDays(day, 1);
    projects.onDay(*gamedata, day, managed, game->getWorld().getInbox());
  }
  for (const FacilityProject& project : controller->getFacilityProjects())
  {
    EXPECT_TRUE(project.completed);
    EXPECT_EQ(project.paid, project.cost);
  }
  EXPECT_EQ(
      club.getProfile().training_facilities,
      std::min<int>(100, training_before + static_cast<int>(training.amount)));
  EXPECT_EQ(club.getProfile().stadium_capacity,
            capacity_before + stadium.amount);
  EXPECT_EQ(std::ranges::count_if(
                controller->getInbox(), [](const InboxMessage& message)
                { return message.title_key == "INBOX_PROJECT_DONE_TITLE"; }),
            2);

  // A medical centre shortens layoffs.
  ASSERT_EQ(
      controller->requestFacilityProject(FacilityProjectType::MedicalCentre),
      ProjectVerdict::Approved);
  const GameDateValue medical_end = controller->getFacilityProjects()[0].end;
  projects.onDay(*gamedata, SeasonCalendar::addDays(medical_end, 1), managed,
                 game->getWorld().getInbox());
  EXPECT_EQ(controller->getMedicalLevel(managed), 60u);
  EXPECT_LT(projects.layoffMultiplier(managed), 1.0f);
}

TEST(FacilityProjects, RefusalStartsACooldown)
{
  const SlotCleanup slot{uniqueSlot(1)};
  auto controller = makeCareer(slot.slot);
  Team& club = controller->getManagedTeam()->get();
  // Broke: the board says no and waits before listening again.
  club.getFinances().record(controller->getCurrentDate(),
                            FinanceCategory::Adjustment,
                            -club.getFinances().getBalance());
  EXPECT_EQ(
      controller->requestFacilityProject(FacilityProjectType::MedicalCentre),
      ProjectVerdict::CannotAfford);
  const auto cooldown =
      controller->getProjectCooldown(FacilityProjectType::MedicalCentre);
  ASSERT_TRUE(cooldown.has_value());
  EXPECT_EQ(*cooldown,
            SeasonCalendar::addDays(controller->getCurrentDate(),
                                    BoardModel::PROJECT_COOLDOWN_DAYS));
  EXPECT_EQ(
      controller->requestFacilityProject(FacilityProjectType::MedicalCentre),
      ProjectVerdict::Cooldown);
  EXPECT_FALSE(
      controller->getProjectCooldown(FacilityProjectType::TrainingGround));
}
