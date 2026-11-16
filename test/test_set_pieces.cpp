// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Captain, vice-captain and set-piece takers: suitability scores, the
// designated-or-automatic resolution and the save round trip.

#include <gtest/gtest.h>
#include <unistd.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/lineup.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

Player makePlayer(PlayerID id, PlayerRole role, float shooting, float passing,
                  float physicality, Foot foot = Foot::Right,
                  uint8_t height = 180)
{
  const std::map<std::string, float> stats = {
      {"Pace", 60.0f},       {"Shooting", shooting},
      {"Passing", passing},  {"Dribbling", 60.0f},
      {"Defending", 60.0f},  {"Physicality", physicality},
      {"Stamina", 60.0f},    {"Vision", passing},
      {"Goalkeeping", 20.0f}};
  return Player(id, 1, "Test", std::to_string(id), role, Language::EN, 1000, 0,
                25, 3, height, foot, stats);
}

int uniqueSlot(int offset)
{
  return 700'000 + (getpid() % 100'000) * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};
}  // namespace

TEST(SetPiecesTest, ScoresFollowTheRelevantAttributes)
{
  const Player striker = makePlayer(1, PlayerRole::ST, 85, 60, 60);
  const Player playmaker = makePlayer(2, PlayerRole::CM, 60, 88, 55);
  const Player keeper = makePlayer(3, PlayerRole::GK, 90, 90, 90);
  const Player tall =
      makePlayer(4, PlayerRole::CB, 40, 50, 85, Foot::Right, 194);
  const Player lefty = makePlayer(5, PlayerRole::LM, 60, 80, 55, Foot::Left);
  const Player righty = makePlayer(6, PlayerRole::RM, 60, 80, 55, Foot::Right);

  using SetPieces::score;
  EXPECT_GT(score(SetPieceDuty::Penalties, striker),
            score(SetPieceDuty::Penalties, playmaker));
  EXPECT_GT(score(SetPieceDuty::CornersLeft, playmaker),
            score(SetPieceDuty::CornersLeft, striker));
  EXPECT_GT(score(SetPieceDuty::LongThrows, tall),
            score(SetPieceDuty::LongThrows, playmaker));
  EXPECT_GT(score(SetPieceDuty::CornersLeft, lefty),
            score(SetPieceDuty::CornersLeft, righty));
  EXPECT_GT(score(SetPieceDuty::CornersRight, righty),
            score(SetPieceDuty::CornersRight, lefty));
  for (std::size_t duty = 0; duty < SET_PIECE_DUTY_COUNT; ++duty)
    EXPECT_EQ(score(static_cast<SetPieceDuty>(duty), keeper), 0.0f)
        << "goalkeepers take no set pieces";
  EXPECT_EQ(score(SetPieceDuty::Captain, striker), 0.0f);

  const std::vector candidates = {&keeper, &striker, &playmaker};
  EXPECT_EQ(SetPieces::best(SetPieceDuty::Penalties, candidates), &striker);
  EXPECT_EQ(SetPieces::best(SetPieceDuty::Captain, candidates), nullptr);
}

TEST(SetPiecesTest, DesignatedStarterTakesItElseTheBestStarter)
{
  const Player keeper = makePlayer(1, PlayerRole::GK, 20, 40, 60);
  const Player striker = makePlayer(2, PlayerRole::ST, 85, 60, 60);
  const Player midfielder = makePlayer(3, PlayerRole::CM, 70, 80, 60);
  const Player substitute = makePlayer(4, PlayerRole::ST, 90, 70, 60);
  Lineup lineup;
  lineup.setGoalkeeper(&keeper);
  lineup.addOutfieldPlayer(&striker, {0.8f, 0.5f});
  lineup.addOutfieldPlayer(&midfielder, {0.5f, 0.5f});
  lineup.setReserves({&substitute});

  EXPECT_EQ(lineup.effectiveTaker(SetPieceDuty::Penalties), &striker);
  lineup.setDesignated(SetPieceDuty::Penalties, midfielder.getId());
  EXPECT_EQ(lineup.effectiveTaker(SetPieceDuty::Penalties), &midfielder);
  // A designated player on the bench does not take it.
  lineup.setDesignated(SetPieceDuty::Penalties, substitute.getId());
  EXPECT_EQ(lineup.effectiveTaker(SetPieceDuty::Penalties), &striker);
  // A goalkeeper designated for corners is ignored.
  lineup.setDesignated(SetPieceDuty::CornersLeft, keeper.getId());
  EXPECT_EQ(lineup.effectiveTaker(SetPieceDuty::CornersLeft), &midfielder);

  // Captaincy: automatic (nullptr) until designated; the vice stands in.
  EXPECT_EQ(lineup.effectiveTaker(SetPieceDuty::Captain), nullptr);
  lineup.setDesignated(SetPieceDuty::Captain, substitute.getId());
  lineup.setDesignated(SetPieceDuty::ViceCaptain, striker.getId());
  EXPECT_EQ(lineup.effectiveTaker(SetPieceDuty::Captain), &striker);
  lineup.setDesignated(SetPieceDuty::Captain, striker.getId());
  EXPECT_EQ(lineup.getDesignated(SetPieceDuty::ViceCaptain), PlayerID{})
      << "the captain cannot also be his own vice";
}

TEST(SetPiecesTest, ControllerDesignationsAutoPickAndSaveRoundTrip)
{
  Logger::init();
  const SlotCleanup slot{uniqueSlot(6)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  const TeamID club = controller.getTeams().front().get().getId();
  controller.selectManagedTeam(club);
  const Lineup& lineup = controller.getManagedTeam()->get().getLineup();
  ASSERT_EQ(lineup.starters().size(), 11u);

  for (std::size_t duty = 0; duty < SET_PIECE_DUTY_COUNT; ++duty)
  {
    const PlayerID taker =
        controller.getEffectiveSetPieceTaker(static_cast<SetPieceDuty>(duty));
    EXPECT_TRUE(lineup.isStarter(taker))
        << SetPieces::dutyKey(static_cast<SetPieceDuty>(duty));
  }
  EXPECT_NE(controller.getEffectiveSetPieceTaker(SetPieceDuty::Captain),
            controller.getEffectiveSetPieceTaker(SetPieceDuty::ViceCaptain));

  // Goalkeepers and other clubs' players are refused.
  ASSERT_NE(lineup.getGoalkeeper(), nullptr);
  EXPECT_FALSE(controller.setSetPieceDesignation(
      SetPieceDuty::Penalties, lineup.getGoalkeeper()->getId()));
  const TeamID other = controller.getTeams().back().get().getId();
  EXPECT_FALSE(controller.setSetPieceDesignation(
      SetPieceDuty::Captain,
      controller.getPlayersForTeam(other).front().get().getId()));

  controller.autoPickSetPieces();
  SetPieceDesignations picked = lineup.getDesignations();
  for (const PlayerID id : picked) EXPECT_NE(id, PlayerID{});
  // The manager overrides the penalty taker with another starter.
  const PlayerID chosen = lineup.starters().back()->getId();
  ASSERT_TRUE(
      controller.setSetPieceDesignation(SetPieceDuty::Penalties, chosen));
  picked[static_cast<std::size_t>(
      std::to_underlying(SetPieceDuty::Penalties))] = chosen;
  EXPECT_EQ(controller.getEffectiveSetPieceTaker(SetPieceDuty::Penalties),
            chosen);

  controller.saveGame();
  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  EXPECT_EQ(reloaded.getManagedTeam()->get().getLineup().getDesignations(),
            picked);
  EXPECT_EQ(reloaded.getEffectiveSetPieceTaker(SetPieceDuty::Penalties),
            chosen);
}
