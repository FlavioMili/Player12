// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <latch>
#include <thread>

#include "controller/game_controller.h"
#include "database/datagenerator.h"
#include "database/gamedata.h"
#include "global/global.h"
#include "model/player.h"

TEST(GameControllerIdentity, ConcurrentFirstReadsShareOneStableCache)
{
  const auto identities = DataGenerator::loadClubIdentities();
  ASSERT_FALSE(identities.empty());
  const TeamID team = identities.begin()->first;
  GameController controller;
  std::array<const ClubIdentity*, 8> results{};
  std::latch start(static_cast<std::ptrdiff_t>(results.size()));
  std::vector<std::jthread> readers;
  for (std::size_t index = 0; index < results.size(); ++index)
  {
    readers.emplace_back(
        [&, index]
        {
          start.arrive_and_wait();
          results[index] = controller.getClubIdentity(team);
        });
  }
  readers.clear();  // Join all readers before examining their results.
  ASSERT_NE(results.front(), nullptr);
  for (const ClubIdentity* identity : results)
    EXPECT_EQ(identity, results.front());
  EXPECT_EQ(results.front()->short_name, identities.at(team).short_name);
  EXPECT_EQ(controller.getClubIdentity(team), results.front());
}

class GameDataTest : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    // Use unique IDs for tests to not clash with global state
  }

  GameData gamedata;
};

TEST_F(GameDataTest, TestAddRemovePlayer)
{
  PlayerID pid = 99999;
  TeamID tid = 999;

  Player p(pid, tid, "Test", "Player", PlayerRole::ST, Language::EN, 1000, 0,
           20, 2, 180, Foot::Right, {});

  // Add player
  gamedata.addPlayer(pid, p);

  auto players = gamedata.getPlayersForTeam(tid);
  bool found = std::ranges::any_of(players, [pid](const auto& player_ref)
                                   { return player_ref.get().getId() == pid; });
  EXPECT_TRUE(found) << "Player was not found after addPlayer";

  // Remove player
  EXPECT_TRUE(gamedata.removePlayer(pid));

  players = gamedata.getPlayersForTeam(tid);
  found = std::ranges::any_of(players, [pid](const auto& player_ref)
                              { return player_ref.get().getId() == pid; });
  EXPECT_FALSE(found) << "Player was still found after removePlayer";
}

TEST_F(GameDataTest, TestTransferPlayer)
{
  PlayerID pid = 88888;
  TeamID source_tid = 111;
  TeamID target_tid = 222;

  Player p(pid, source_tid, "Test", "Transfer", PlayerRole::CM, Language::EN,
           1000, 0, 20, 2, 180, Foot::Right, {});
  gamedata.addPlayer(pid, p);

  gamedata.transferPlayer(pid, target_tid);

  auto source_players = gamedata.getPlayersForTeam(source_tid);
  bool found_in_source =
      std::ranges::any_of(source_players, [pid](const auto& player_ref)
                          { return player_ref.get().getId() == pid; });
  EXPECT_FALSE(found_in_source)
      << "Player was still in source team after transfer";

  auto target_players = gamedata.getPlayersForTeam(target_tid);
  bool found_in_target =
      std::ranges::any_of(target_players, [pid](const auto& player_ref)
                          { return player_ref.get().getId() == pid; });
  EXPECT_TRUE(found_in_target)
      << "Player was not in target team after transfer";

  EXPECT_EQ(gamedata.getPlayer(pid)->get().getTeamId(), target_tid);
  gamedata.removePlayer(pid);
}

TEST_F(GameDataTest, ExpiredContractReleasesPlayerToFreeAgents)
{
  constexpr PlayerID PLAYER_ID = 77777;
  constexpr TeamID SOURCE_TEAM_ID = 333;
  gamedata.addTeam(FREE_AGENTS_TEAM_ID,
                   Team(FREE_AGENTS_TEAM_ID, 0, "Free agents", 0));
  gamedata.addTeam(SOURCE_TEAM_ID,
                   Team(SOURCE_TEAM_ID, 1, "Source", 1'000'000));
  Player player(PLAYER_ID, SOURCE_TEAM_ID, "End", "Contract", PlayerRole::CM,
                Language::EN, 1'000, 0, 27, 1, 180, Foot::Right, {});
  gamedata.addPlayer(PLAYER_ID, player);
  gamedata.getTeams().at(SOURCE_TEAM_ID).addPlayerID(PLAYER_ID);

  const std::vector<PlayerID> released =
      gamedata.advanceContractsAndReleasePlayers();

  EXPECT_TRUE(std::ranges::contains(released, PLAYER_ID));
  ASSERT_TRUE(gamedata.getPlayer(PLAYER_ID).has_value());
  EXPECT_EQ(gamedata.getPlayer(PLAYER_ID)->get().getTeamId(),
            FREE_AGENTS_TEAM_ID);
  EXPECT_EQ(gamedata.getPlayer(PLAYER_ID)->get().getContractYears(), 0);
  EXPECT_FALSE(std::ranges::contains(
      gamedata.getTeams().at(SOURCE_TEAM_ID).getPlayerIDs(), PLAYER_ID));
  EXPECT_TRUE(std::ranges::contains(
      gamedata.getTeams().at(FREE_AGENTS_TEAM_ID).getPlayerIDs(), PLAYER_ID));
}

TEST_F(GameDataTest, RemovedPlayerLeavesNoLineupReference)
{
  static constexpr TeamID CLUB_ID = 444;
  static constexpr TeamID OTHER_ID = 445;
  gamedata.addTeam(CLUB_ID, Team(CLUB_ID, 1, "Club", 1'000'000));
  gamedata.addTeam(OTHER_ID, Team(OTHER_ID, 1, "Other", 1'000'000));
  const auto add = [this](PlayerID id, PlayerRole role)
  {
    gamedata.addPlayer(
        id, Player(id, CLUB_ID, "Squad", "Member", role, Language::EN, 1'000, 0,
                   17, 2, 180, Foot::Right, {}));
    gamedata.getTeams().at(CLUB_ID).addPlayerID(id);
    return &gamedata.getPlayer(id)->get();
  };
  const Player* keeper = add(66601, PlayerRole::GK);
  const Player* starter = add(66602, PlayerRole::ST);
  const Player* benched = add(66603, PlayerRole::CM);
  const Player* spare = add(66604, PlayerRole::CB);

  Lineup& lineup = gamedata.getTeams().at(CLUB_ID).getLineup();
  lineup.setGoalkeeper(keeper);
  lineup.addOutfieldPlayer(starter, {0.8f, 0.5f});
  lineup.setReserves({benched, spare});
  lineup.setDesignated(SetPieceDuty::Penalties, benched->getId());
  lineup.setDesignated(SetPieceDuty::Captain, starter->getId());
  // A stale reference in another club's line-up (as the free agents keep
  // for players who signed elsewhere).
  gamedata.getTeams().at(OTHER_ID).getLineup().setReserves({benched});

  // A benched player: no pointer may survive anywhere.
  ASSERT_TRUE(gamedata.removePlayer(benched->getId()));
  EXPECT_FALSE(std::ranges::contains(lineup.getReserves(), benched));
  EXPECT_TRUE(std::ranges::contains(lineup.getReserves(), spare));
  EXPECT_FALSE(std::ranges::contains(
      gamedata.getTeams().at(OTHER_ID).getLineup().getReserves(), benched));
  EXPECT_EQ(lineup.getDesignated(SetPieceDuty::Penalties), PlayerID{});
  EXPECT_EQ(lineup.getDesignated(SetPieceDuty::Captain), starter->getId());

  // Starters and the goalkeeper are dropped too.
  const PlayerID starter_id = starter->getId();
  ASSERT_TRUE(gamedata.removePlayer(starter_id));
  EXPECT_TRUE(lineup.getOutfieldPlayers().empty());
  EXPECT_EQ(lineup.getDesignated(SetPieceDuty::Captain), PlayerID{});
  ASSERT_TRUE(gamedata.removePlayer(keeper->getId()));
  EXPECT_EQ(lineup.getGoalkeeper(), nullptr);
  EXPECT_EQ(lineup.getReserves().size(), 1u);
  EXPECT_FALSE(lineup.removePlayer(starter_id));
}
