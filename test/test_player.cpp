// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <atomic>
#include <latch>
#include <thread>
#include <vector>

#include "global/stats_config.h"
#include "model/player.h"

TEST(PlayerTest, ConcurrentPriceRefreshAndCopiesKeepIndependentCaches)
{
  StatsConfig config;
  config.role_focus["Striker"] = {{"Speed", "Shooting"}, {0.5, 0.5}};
  Player player(1, 10, "Price", "Test", PlayerRole::ST, Language::EN, 1000, 0,
                25, 3, 180, Foot::Right,
                {{"Speed", 80.0f}, {"Shooting", 90.0f}});
  player.updateMarketValue(config);
  const auto expected = player.getMarketValue();
  ASSERT_GT(expected, 0U);
  const Player& shared = player;
  std::atomic<bool> consistent{true};
  std::latch start(8);
  std::vector<std::jthread> readers;
  for (int thread = 0; thread < 8; ++thread)
    readers.emplace_back(
        [&]
        {
          start.arrive_and_wait();
          for (int attempt = 0; attempt < 500; ++attempt)
          {
            shared.updateMarketValue(config);
            const Player copy = shared;
            if (shared.getMarketValue() != expected ||
                copy.getMarketValue() != expected)
              consistent.store(false);
          }
        });
  readers.clear();
  EXPECT_TRUE(consistent.load());
  Player copy = player;
  copy.setAge(30);
  EXPECT_EQ(copy.getMarketValue(), 0U);
  EXPECT_EQ(player.getMarketValue(), expected);
}

TEST(PlayerTest, ConstructorAndGetters)
{
  std::map<std::string, float> stats = {{"Speed", 80.0f}, {"Passing", 70.0f}};
  Player p(1, 10, "John", "Doe", PlayerRole::CM, Language::EN, 1000, 1, 25, 3,
           180, Foot::Right, stats);

  EXPECT_EQ(p.getId(), 1);
  EXPECT_EQ(p.getTeamId(), 10);
  EXPECT_EQ(p.getName(), "John Doe");
  EXPECT_EQ(p.getAge(), 25);
  EXPECT_EQ(p.getRole(), PlayerRole::CM);
  EXPECT_EQ(p.getStats().at("Speed"), 80.0f);
}

TEST(PlayerTest, AgePlayerGrowth)
{
  std::map<std::string, float> stats = {{"Speed", 80.0f}};
  // Young player
  Player p(1, 10, "Young", "Gun", PlayerRole::ST, Language::EN, 1000, 1, 20, 3,
           180, Foot::Right, stats);

  // If age < PLAYER_AGE_FACTOR_DECLINE_AGE (31.5), stats don't decay.
  p.agePlayer();
  EXPECT_EQ(p.getAge(), 21);
  EXPECT_EQ(p.getStats().at("Speed"), 80.0f);
}

TEST(PlayerTest, AgePlayerDecline)
{
  std::map<std::string, float> stats = {{"Speed", 80.0f}};
  // Old player
  Player p(1, 10, "Old", "Guard", PlayerRole::ST, Language::EN, 1000, 1, 35, 3,
           180, Foot::Right, stats);

  p.agePlayer();
  EXPECT_EQ(p.getAge(), 36);
  // Should decline
  EXPECT_LT(p.getStats().at("Speed"), 80.0f);
}

TEST(PlayerTest, GetOverall)
{
  std::map<std::string, float> stats = {{"Speed", 80.0f}, {"Shooting", 90.0f}};
  Player p(1, 10, "Striker", "Man", PlayerRole::ST, Language::EN, 1000, 1, 25,
           3, 180, Foot::Right, stats);

  StatsConfig config;
  RoleFocus roleFocus;
  roleFocus.stats = {"Speed", "Shooting"};
  roleFocus.weights = {0.5, 0.5};
  config.role_focus["Striker"] = roleFocus;

  double overall = p.getOverall(config);
  EXPECT_DOUBLE_EQ(overall, 85.0);
}

TEST(PlayerTest, Train)
{
  std::map<std::string, float> stats = {{"Speed", 50.0f}};
  Player p(1, 10, "Trainee", "Boy", PlayerRole::ST, Language::EN, 1000, 1, 20,
           3, 180, Foot::Right, stats);

  // Train speed
  // Training is random, but it should increase or stay same (if random factor
  // is 0, unlikely but possible). Actually random_factor is [0, 1]. age_factor
  // for 20 is positive. So it should strictly increase unless rng hits 0.0
  // exactly. We can loop a few times to ensure increase.

  float initial_speed = p.getStats().at("Speed");
  for (int i = 0; i < 10; ++i)
  {
    p.train({"Speed"});
  }

  EXPECT_GT(p.getStats().at("Speed"), initial_speed);
}
