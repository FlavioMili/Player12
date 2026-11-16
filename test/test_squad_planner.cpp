// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Squad planner: depth chart tiers, needs, age profile, contract calendar and
// the next-season projection (expiring contracts leave, players age).

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/squad_planner.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

PlannerPlayer make(PlayerID id, PlayerRole role, int age, float overall,
                   int contract_years = 3, float potential = 0.0f)
{
  PlannerPlayer player;
  player.id = id;
  player.role = role;
  player.age = age;
  player.overall = overall;
  player.potential = potential > 0.0f ? potential : overall;
  player.contract_years = contract_years;
  return player;
}

const GroupDepth& group(const SquadPlan& plan, PlannerGroup which)
{
  return plan.groups[static_cast<std::size_t>(std::to_underlying(which))];
}

bool hasNeed(const SquadPlan& plan, PlannerGroup which, NeedKind kind)
{
  return std::ranges::any_of(
      plan.needs, [&](const PlannerNeed& need)
      { return need.group == which && need.kind == kind; });
}

/** A healthy 25-man squad: every group at its target depth. */
std::vector<PlannerPlayer> healthySquad()
{
  std::vector<PlannerPlayer> squad;
  PlayerID id = 1;
  const auto add = [&](PlayerRole role, int count)
  {
    for (int index = 0; index < count; ++index)
      squad.push_back(make(id++, role, 24 + index, 70.0f - index));
  };
  add(PlayerRole::GK, 3);
  add(PlayerRole::CB, 5);
  add(PlayerRole::LB, 2);
  add(PlayerRole::RB, 2);
  add(PlayerRole::CM, 6);
  add(PlayerRole::LW, 4);
  add(PlayerRole::ST, 3);
  return squad;
}
}  // namespace

TEST(SquadPlannerTest, GroupsMatchTheClubsRecruitmentTargets)
{
  EXPECT_EQ(SquadPlanner::groupOf(PlayerRole::GK), PlannerGroup::Goalkeeper);
  EXPECT_EQ(SquadPlanner::groupOf(PlayerRole::CDM), PlannerGroup::Midfield);
  EXPECT_EQ(SquadPlanner::groupOf(PlayerRole::CAM), PlannerGroup::Midfield);
  EXPECT_EQ(SquadPlanner::groupOf(PlayerRole::RM), PlannerGroup::Wide);
  EXPECT_EQ(SquadPlanner::groupOf(PlayerRole::RW), PlannerGroup::Wide);
  int target = 0;
  int starters = 0;
  for (std::size_t index = 0; index < PLANNER_GROUP_COUNT; ++index)
  {
    target += SquadPlanner::targetDepth(static_cast<PlannerGroup>(index));
    starters += SquadPlanner::defaultStarters(static_cast<PlannerGroup>(index));
  }
  EXPECT_EQ(target, 25);
  EXPECT_EQ(starters, 11);
}

TEST(SquadPlannerTest, DepthTiersPreferTheSelectedXiThenAbility)
{
  using enum PlayerRole;
  std::vector squad = {make(1, ST, 27, 80.0f), make(2, ST, 29, 75.0f),
                       make(3, ST, 19, 60.0f), make(4, ST, 26, 65.0f)};
  squad[1].in_xi = true;  // The manager starts the weaker striker.
  const SquadPlan plan = SquadPlanner::build(squad, 0);
  const GroupDepth& strikers = group(plan, PlannerGroup::Striker);
  ASSERT_EQ(strikers.players.size(), 4u);
  EXPECT_EQ(strikers.players[0].player.id, 2u);
  EXPECT_EQ(strikers.players[0].tier, DepthTier::FirstChoice);
  EXPECT_EQ(strikers.players[1].player.id, 1u);
  EXPECT_EQ(strikers.players[1].tier, DepthTier::Backup);
  EXPECT_EQ(strikers.players[2].player.id, 4u);
  EXPECT_EQ(strikers.players[3].tier, DepthTier::Prospect);
  EXPECT_EQ(strikers.surplus, 1);
  EXPECT_FALSE(hasNeed(plan, PlannerGroup::Striker, NeedKind::Surplus))
      << "one spare striker is not worth a sale";
}

TEST(SquadPlannerTest, NeedsFlagMissingWeakAgeingAndSurplusGroups)
{
  std::vector<PlannerPlayer> squad = healthySquad();
  // Only one goalkeeper, ageing: two missing and a succession.
  std::erase_if(squad, [](const PlannerPlayer& player)
                { return player.role == PlayerRole::GK; });
  squad.push_back(make(100, PlayerRole::GK, 34, 70.0f));
  // Left backs far weaker than the rest of the XI.
  for (PlannerPlayer& player : squad)
    if (player.role == PlayerRole::LB) player.overall = 55.0f;
  // Three extra wingers.
  for (PlayerID id = 200; id < 203; ++id)
    squad.push_back(make(id, PlayerRole::RW, 23, 62.0f));
  const SquadPlan plan = SquadPlanner::build(squad, 0);
  EXPECT_EQ(group(plan, PlannerGroup::Goalkeeper).missing, 2);
  EXPECT_TRUE(hasNeed(plan, PlannerGroup::Goalkeeper, NeedKind::Missing));
  EXPECT_TRUE(hasNeed(plan, PlannerGroup::Goalkeeper, NeedKind::Succession));
  EXPECT_TRUE(hasNeed(plan, PlannerGroup::LeftBack, NeedKind::Upgrade));
  EXPECT_TRUE(hasNeed(plan, PlannerGroup::Wide, NeedKind::Surplus));
  EXPECT_FALSE(hasNeed(plan, PlannerGroup::CentreBack, NeedKind::Missing));
  EXPECT_EQ(plan.needs.front().kind, NeedKind::Missing)
      << "missing depth is the most pressing";
}

TEST(SquadPlannerTest, ListedPlayersDoNotCountAsDepth)
{
  std::vector<PlannerPlayer> squad = healthySquad();
  for (PlannerPlayer& player : squad)
    if (player.role == PlayerRole::GK && player.id == 1) player.listed = true;
  const SquadPlan plan = SquadPlanner::build(squad, 0);
  EXPECT_EQ(group(plan, PlannerGroup::Goalkeeper).missing, 1);
}

TEST(SquadPlannerTest, NextSeasonDropsExpiringContractsAndAgesTheSquad)
{
  std::vector<PlannerPlayer> squad = healthySquad();
  squad[3].contract_years = 1;  // A centre back leaves.
  squad[0] = make(1, PlayerRole::GK, 18, 55.0f, 3, 75.0f);  // Young keeper.
  squad[5] = make(6, PlayerRole::CB, 33, 72.0f, 2);         // Veteran.
  const SquadPlan now = SquadPlanner::build(squad, 0);
  const SquadPlan next = SquadPlanner::build(squad, 1);

  EXPECT_EQ(now.squad_size, 25);
  EXPECT_EQ(next.squad_size, 24);
  ASSERT_EQ(next.departures.size(), 1u);
  EXPECT_EQ(next.departures.front(), squad[3].id);
  EXPECT_EQ(group(next, PlannerGroup::CentreBack).missing, 1);
  EXPECT_NEAR(next.average_age - now.average_age, 1.0f, 0.2f);

  const auto find = [](const SquadPlan& plan, PlayerID id) -> const DepthEntry*
  {
    for (const GroupDepth& depth : plan.groups)
      for (const DepthEntry& entry : depth.players)
        if (entry.player.id == id) return &entry;
    return nullptr;
  };
  const DepthEntry* keeper = find(next, 1);
  ASSERT_NE(keeper, nullptr);
  EXPECT_EQ(keeper->player.age, 19);
  EXPECT_GT(keeper->player.overall, 55.0f) << "young players grow";
  EXPECT_LT(keeper->player.overall, 75.0f) << "but not past the estimate";
  const DepthEntry* veteran = find(next, 6);
  ASSERT_NE(veteran, nullptr);
  EXPECT_LT(veteran->player.overall, 72.0f) << "veterans decline";
  EXPECT_TRUE(veteran->ageing);
  EXPECT_TRUE(veteran->expiring) << "two seasons left now, one next season";

  // The contract calendar shifts by a season and never lists an empty year.
  ASSERT_FALSE(now.expiries.empty());
  EXPECT_EQ(now.expiries.front().years_left, 1);
  for (const ContractExpiry& expiry : next.expiries)
    EXPECT_FALSE(expiry.players.empty());
  int banded = 0;
  for (const int count : next.age_bands) banded += count;
  EXPECT_EQ(banded, next.squad_size);
}

TEST(SquadPlannerTest, ControllerPlanCoversTheManagedSquad)
{
  Logger::init();
  const int slot = 700'000 + getpid() % 100'000 * 10 + 5;
  struct Cleanup
  {
    explicit Cleanup(int saveSlot) : slot(saveSlot) {}
    Cleanup(const Cleanup&) = delete;
    Cleanup& operator=(const Cleanup&) = delete;

    int slot;
    ~Cleanup() { RuntimePaths::removeSave(slot); }
  };
  const Cleanup cleanup{slot};
  GameController controller;
  controller.newGame(slot, WORLD_SEED);
  const TeamID club = controller.getTeams().front().get().getId();
  controller.selectManagedTeam(club);
  const SquadPlan plan = controller.getSquadPlan(0);
  EXPECT_EQ(plan.squad_size,
            static_cast<int>(controller.getPlayersForTeam(club).size()));
  int first_choices = 0;
  for (const GroupDepth& depth : plan.groups)
    for (const DepthEntry& entry : depth.players)
      if (entry.tier == DepthTier::FirstChoice) ++first_choices;
  EXPECT_EQ(first_choices, 11) << "the selected XI fills the first choices";
  const SquadPlan next = controller.getSquadPlan(1);
  EXPECT_EQ(next.squad_size + static_cast<int>(next.departures.size()),
            plan.squad_size);
}
