// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Squad statuses given by the manager: the status rules, the book and its
// save round trip, and their effect on playing-time expectations and morale.

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/squad_status.h"
#include "model/world_simulation.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 700'000 + getpid() % 100'000 * 10 + offset;
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

const Player& playerOf(const GameController& controller, PlayerID id)
{
  return controller.getGameData()->getPlayer(id)->get();
}

/** Managed players, best overall first. */
std::vector<PlayerID> squadByOverall(const GameController& controller)
{
  const TeamID club = controller.getManagedTeam()->get().getId();
  std::vector<std::pair<double, PlayerID>> ranked;
  for (const auto& player : controller.getPlayersForTeam(club))
    ranked.emplace_back(player.get().getOverall(controller.getStatsConfig()),
                        player.get().getId());
  std::ranges::sort(ranked, std::greater<>());
  std::vector<PlayerID> ids;
  for (const auto& [overall, id] : ranked) ids.push_back(id);
  return ids;
}
}  // namespace

TEST(SquadStatusTest, StatusesMapOntoPlayingTimeExpectations)
{
  using SquadStatusModel::toSquadRole;
  EXPECT_EQ(toSquadRole(SquadStatus::Star), SquadRole::KeyPlayer);
  EXPECT_EQ(toSquadRole(SquadStatus::Important), SquadRole::FirstTeam);
  EXPECT_EQ(toSquadRole(SquadStatus::Regular), SquadRole::FirstTeam);
  EXPECT_EQ(toSquadRole(SquadStatus::Rotation), SquadRole::Rotation);
  EXPECT_EQ(toSquadRole(SquadStatus::Backup), SquadRole::Backup);
  EXPECT_EQ(toSquadRole(SquadStatus::Prospect), SquadRole::Fringe);
  for (std::size_t status = 0; status < SQUAD_STATUS_COUNT; ++status)
  {
    EXPECT_NE(std::string(
                  SquadStatusModel::nameKey(static_cast<SquadStatus>(status))),
              "");
  }
}

TEST(SquadStatusTest, DeservedStatusFollowsTheAbilityRank)
{
  using SquadStatusModel::deserved;
  EXPECT_EQ(deserved(0, 27), SquadStatus::Star);
  EXPECT_EQ(deserved(2, 27), SquadStatus::Star);
  EXPECT_EQ(deserved(3, 27), SquadStatus::Important);
  EXPECT_EQ(deserved(8, 27), SquadStatus::Regular);
  EXPECT_EQ(deserved(12, 27), SquadStatus::Rotation);
  EXPECT_EQ(deserved(20, 27), SquadStatus::Backup);
  EXPECT_EQ(deserved(20, 19), SquadStatus::Prospect);
  // Rank beats age: a young first choice is not a prospect.
  EXPECT_EQ(deserved(1, 19), SquadStatus::Star);
}

TEST(SquadStatusTest, DemotionBeyondOneLevelCostsMoraleScaledByAmbition)
{
  using enum SquadStatus;
  using SquadStatusModel::moraleOffset;
  EXPECT_EQ(moraleOffset(Star, Rotation, 1.0f), 0.0f)
      << "promotions never hurt";
  EXPECT_EQ(moraleOffset(Important, Star, 1.0f), 0.0f)
      << "one level below is tolerated";
  const float two = moraleOffset(Regular, Star, 1.0f);
  const float four = moraleOffset(Backup, Star, 1.0f);
  EXPECT_LT(two, 0.0f);
  EXPECT_LT(four, two);
  EXPECT_LT(moraleOffset(Backup, Star, 1.5f), four);
  EXPECT_GT(moraleOffset(Backup, Star, 0.8f), four);
  // Playing more than a backup expects is worth at most +5: a clear
  // demotion still hurts the least ambitious player.
  EXPECT_LT(moraleOffset(Backup, Star, 0.5f) + 5.0f, 0.0f);
  // A prospect and a backup share the bottom of the pecking order.
  EXPECT_EQ(moraleOffset(Prospect, Backup, 1.0f), 0.0f);
}

TEST(SquadStatusTest, ExpectationNeverDropsFarBelowTheDeservedStatus)
{
  using SquadStatusModel::expectation;
  EXPECT_EQ(expectation(SquadStatus::Star, SquadStatus::Rotation),
            SquadStatus::Star)
      << "a promotion raises the expectation";
  EXPECT_EQ(expectation(SquadStatus::Important, SquadStatus::Star),
            SquadStatus::Important);
  EXPECT_EQ(expectation(SquadStatus::Backup, SquadStatus::Star),
            SquadStatus::Important);
  EXPECT_EQ(expectation(SquadStatus::Prospect, SquadStatus::Backup),
            SquadStatus::Prospect);
  EXPECT_EQ(expectation(SquadStatus::Prospect, SquadStatus::Regular),
            SquadStatus::Rotation);
}

TEST(SquadStatusTest, BookOnlyCountsWhileThePlayerStaysAtTheClub)
{
  SquadStatusBook book;
  book.set(7, 3, SquadStatus::Star);
  EXPECT_EQ(book.get(7, 3), SquadStatus::Star);
  EXPECT_EQ(book.get(7, 4), std::nullopt) << "the player moved on";
  book.set(8, 3, SquadStatus::Backup);
  book.prune([](PlayerID id) { return id == 7 ? TeamID{4} : TeamID{3}; });
  EXPECT_EQ(book.size(), 1u);
  EXPECT_EQ(book.get(8, 3), SquadStatus::Backup);
  book.set(8, 3, std::nullopt);
  EXPECT_EQ(book.size(), 0u);
}

TEST(SquadStatusTest, ControllerValidatesAndStatusesSurviveSaveAndLoad)
{
  const SlotCleanup slot{uniqueSlot(1)};
  auto controller = makeCareer(slot.slot);
  const std::vector<PlayerID> squad = squadByOverall(*controller);
  ASSERT_GE(squad.size(), 16u);
  const PlayerID star = squad.back();
  const PlayerID benched = squad.front();

  EXPECT_EQ(controller->getSquadStatus(star), std::nullopt);
  EXPECT_EQ(controller->getDeservedSquadStatus(benched), SquadStatus::Star);
  ASSERT_TRUE(controller->setSquadStatus(star, SquadStatus::Star));
  ASSERT_TRUE(controller->setSquadStatus(benched, SquadStatus::Rotation));
  EXPECT_EQ(controller->getSquadRole(star), SquadRole::KeyPlayer)
      << "the status replaces the ability-rank expectation";
  EXPECT_EQ(controller->getSquadRole(benched), SquadRole::FirstTeam)
      << "a demoted star still expects the minutes of an important player";

  // Other clubs' players and over-age prospects are refused.
  const TeamID other = controller->getTeams().back().get().getId();
  const PlayerID foreign =
      controller->getPlayersForTeam(other).front().get().getId();
  EXPECT_FALSE(controller->setSquadStatus(foreign, SquadStatus::Star));
  const auto veteran =
      std::ranges::find_if(squad, [&](PlayerID id)
                           { return playerOf(*controller, id).getAge() > 21; });
  ASSERT_NE(veteran, squad.end());
  EXPECT_FALSE(controller->setSquadStatus(*veteran, SquadStatus::Prospect));

  controller->saveGame();
  auto reloaded = std::make_unique<GameController>();
  ASSERT_TRUE(reloaded->loadGame(slot.slot));
  EXPECT_EQ(reloaded->getSquadStatus(star), SquadStatus::Star);
  EXPECT_EQ(reloaded->getSquadStatus(benched), SquadStatus::Rotation);
  EXPECT_EQ(reloaded->getSquadRole(star), SquadRole::KeyPlayer);

  ASSERT_TRUE(reloaded->setSquadStatus(star, std::nullopt));
  EXPECT_EQ(reloaded->getSquadStatus(star), std::nullopt);
  EXPECT_NE(reloaded->getSquadRole(star), SquadRole::KeyPlayer)
      << "without a status the rank decides again";
}

TEST(SquadStatusTest, DemotingTheBestPlayerLowersHisMorale)
{
  // Two identical careers; in one the best player is made a backup. Even
  // though he keeps playing, a few weeks later his morale is clearly lower.
  const SlotCleanup first{uniqueSlot(2)};
  const SlotCleanup second{uniqueSlot(3)};
  auto baseline = makeCareer(first.slot);
  auto demoted = makeCareer(second.slot);
  const PlayerID best = squadByOverall(*baseline).front();
  ASSERT_EQ(best, squadByOverall(*demoted).front());
  // He has been playing like a key player so far in both careers.
  baseline->getGameData()
      ->getPlayers()
      .at(best)
      .mutableDynamics()
      .playing_share = 0.85f;
  demoted->getGameData()
      ->getPlayers()
      .at(best)
      .mutableDynamics()
      .playing_share = 0.85f;
  ASSERT_TRUE(demoted->setSquadStatus(best, SquadStatus::Backup));
  for (int day = 0; day < 28; ++day)
  {
    baseline->advanceDay();
    demoted->advanceDay();
  }
  const float kept = playerOf(*baseline, best).getDynamics().morale;
  const float hurt = playerOf(*demoted, best).getDynamics().morale;
  EXPECT_LT(hurt, kept - 1.0f);
}
