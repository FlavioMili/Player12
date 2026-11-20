// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Player roles, duties, the in-possession shape, opposition instructions and
// team talks: their effect in the match engine and their persistence.

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "database/database_connection.h"
#include "database/repositories/team_repository.h"
#include "global/logger.h"
#include "global/stats_config.h"
#include "model/match_engine.h"
#include "model/match_pitch.h"
#include "model/player.h"
#include "model/tactics.h"
#include "model/team.h"

namespace
{
// Tests play on a match pitch, placed by fractions of it.
const MatchPitch& PITCH = MatchPitch::matchSize();
}  // namespace

namespace
{
StatsConfig createStatsConfig()
{
  StatsConfig config;
  config.possible_stats = {"Pace",      "Shooting",  "Passing",
                           "Dribbling", "Defending", "Physicality",
                           "Stamina",   "Vision",    "Goalkeeping"};
  config.role_focus["Goalkeeper"] = {
      .stats = {"Goalkeeping", "Vision", "Physicality"},
      .weights = {0.7, 0.2, 0.1}};
  config.role_focus["Defender"] = {
      .stats = {"Defending", "Physicality", "Pace", "Vision"},
      .weights = {0.4, 0.3, 0.15, 0.15}};
  config.role_focus["Midfielder"] = {
      .stats = {"Passing", "Vision", "Stamina", "Dribbling"},
      .weights = {0.3, 0.3, 0.2, 0.2}};
  config.role_focus["Striker"] = {
      .stats = {"Shooting", "Pace", "Dribbling", "Physicality"},
      .weights = {0.4, 0.2, 0.2, 0.2}};
  return config;
}

std::map<std::string, float> statsFor(PlayerRole role, std::uint32_t salt)
{
  const float jitter = static_cast<float>((salt * 2654435761U) % 7U) - 3.0F;
  const auto value = [&](float offset)
  { return std::clamp(62.0F + offset + jitter, 1.0F, 99.0F); };
  std::map<std::string, float> stats = {
      {"Pace", value(0.0F)},      {"Shooting", value(-8.0F)},
      {"Passing", value(0.0F)},   {"Dribbling", value(-4.0F)},
      {"Defending", value(0.0F)}, {"Physicality", value(0.0F)},
      {"Stamina", value(0.0F)},   {"Vision", value(0.0F)},
      {"Goalkeeping", 15.0F}};
  if (role == PlayerRole::GK)
  {
    stats["Goalkeeping"] = value(4.0F);
    stats["Shooting"] = value(-40.0F);
  }
  if (role == PlayerRole::ST)
  {
    stats["Shooting"] = value(8.0F);
    stats["Defending"] = value(-30.0F);
  }
  if (role == PlayerRole::CB)
  {
    stats["Defending"] = value(8.0F);
  }
  return stats;
}

// Lineup order (outfield slot index): LB 0, CB 1, CB 2, RB 3, LM 4, CM 5,
// CM 6, RM 7, ST 8, ST 9.
constexpr std::array<PlayerRole, 11> ROLES = {
    PlayerRole::GK, PlayerRole::LB, PlayerRole::CB, PlayerRole::CB,
    PlayerRole::RB, PlayerRole::LM, PlayerRole::CM, PlayerRole::CM,
    PlayerRole::RM, PlayerRole::ST, PlayerRole::ST};
constexpr std::array<Vector2F, 11> POSITIONS = {{{.x = 0.04F, .y = 0.50F},
                                                 {.x = 0.20F, .y = 0.12F},
                                                 {.x = 0.20F, .y = 0.38F},
                                                 {.x = 0.20F, .y = 0.62F},
                                                 {.x = 0.20F, .y = 0.88F},
                                                 {.x = 0.43F, .y = 0.12F},
                                                 {.x = 0.43F, .y = 0.38F},
                                                 {.x = 0.43F, .y = 0.62F},
                                                 {.x = 0.43F, .y = 0.88F},
                                                 {.x = 0.78F, .y = 0.38F},
                                                 {.x = 0.78F, .y = 0.62F}}};
constexpr std::size_t LEFT_BACK = 1;
constexpr std::size_t LEFT_CENTRAL_MIDFIELDER = 6;
constexpr std::size_t FIRST_STRIKER = 9;

struct Squad
{
  std::vector<std::unique_ptr<Player>> players;
  Lineup lineup;
};

std::unique_ptr<Squad> makeSquad(TeamID team)
{
  auto squad = std::make_unique<Squad>();
  for (std::size_t index = 0; index < ROLES.size(); ++index)
  {
    const auto id = static_cast<PlayerID>((team * 100U) + index);
    squad->players.push_back(std::make_unique<Player>(
        id, team, "Tactic", std::to_string(id), ROLES[index], Language::EN,
        100'000, 0, 26, 3, 180, Foot::Right,
        statsFor(ROLES[index], static_cast<std::uint32_t>(index))));
    if (index == 0)
    {
      squad->lineup.setGoalkeeper(squad->players.back().get());
    }
    else
    {
      squad->lineup.addOutfieldPlayer(squad->players.back().get(),
                                      POSITIONS[index]);
    }
  }
  return squad;
}

PlayerID playerId(TeamID team, std::size_t index)
{
  return static_cast<PlayerID>((team * 100U) + index);
}

Strategy withSlots(
    std::initializer_list<std::pair<std::size_t, SlotInstruction>> slots)
{
  Strategy strategy;
  for (auto [index, slot] : slots)
  {
    slot.anchor = POSITIONS[index];
    strategy.setSlot(slot);
  }
  return strategy;
}

constexpr std::array<std::uint32_t, 4> SEEDS = {101, 202, 303, 404};

/** Plays a whole match, calling @p sample every simulated second. */
template <typename Sample>
void playMatch(const Strategy& home, const Strategy& away, std::uint32_t seed,
               Sample&& sample)
{
  static const StatsConfig config = createStatsConfig();
  const auto homeSquad = makeSquad(1);
  const auto awaySquad = makeSquad(2);
  MatchEngine engine(homeSquad->lineup, awaySquad->lineup, home, away, config,
                     seed);
  while (engine.getState() != MatchState::FULL_TIME)
  {
    engine.advance(1.0F);
    sample(engine);
  }
}

const PlayerMatchStats* statsOf(const MatchEngine& engine, PlayerID id)
{
  return engine.findPlayerStats(id);
}

bool homeHasBall(const MatchEngine& engine)
{
  const Player* carrier = engine.getBall().possessionState().possessedBy;
  return (carrier != nullptr) && carrier->getTeamId() == 1;
}
}  // namespace

TEST(TacticsTest, SlotsAreClassifiedByPosition)
{
  EXPECT_EQ(Tactics::familyForSlot({0.20F, 0.12F}), RoleFamily::FullBack);
  EXPECT_EQ(Tactics::familyForSlot({0.20F, 0.38F}), RoleFamily::CentreBack);
  EXPECT_EQ(Tactics::familyForSlot({0.38F, 0.50F}), RoleFamily::Holding);
  EXPECT_EQ(Tactics::familyForSlot({0.43F, 0.38F}), RoleFamily::Central);
  EXPECT_EQ(Tactics::familyForSlot({0.43F, 0.88F}), RoleFamily::Wide);
  EXPECT_EQ(Tactics::familyForSlot({0.58F, 0.50F}), RoleFamily::Attacking);
  EXPECT_EQ(Tactics::familyForSlot({0.72F, 0.14F}), RoleFamily::Wide);
  EXPECT_EQ(Tactics::familyForSlot({0.78F, 0.38F}), RoleFamily::Striker);
  // Every family offers Standard first, and roles stay in their family.
  for (int family = 0;
       family < static_cast<int>(std::to_underlying(RoleFamily::COUNT));
       ++family)
  {
    const auto roles = Tactics::rolesFor(static_cast<RoleFamily>(family));
    ASSERT_FALSE(roles.empty());
    EXPECT_EQ(roles.front(), TacticalRole::Standard);
  }
  EXPECT_TRUE(Tactics::allows(RoleFamily::Striker, TacticalRole::FalseNine));
  EXPECT_FALSE(Tactics::allows(RoleFamily::CentreBack, TacticalRole::Poacher));
}

TEST(TacticsTest, EveryRoleChangesTheEngineProfile)
{
  const RoleProfile standard =
      Tactics::profile(TacticalRole::Standard, RoleDuty::Support);
  EXPECT_FLOAT_EQ(standard.runBias, 0.0F);
  EXPECT_FLOAT_EQ(standard.possessionAdvanceMetres, 0.0F);
  const auto differs = [&](const RoleProfile& profile)
  {
    return profile.possessionAdvanceMetres != 0.0F ||
           profile.possessionWidthMetres != 0.0F ||
           profile.defensiveAdvanceMetres != 0.0F || profile.runBias != 0.0F ||
           profile.passDaring != 0.0F || profile.targetBias != 0.0F ||
           profile.pressBias != 0.0F || profile.shotBias != 0.0F ||
           profile.cutInside != 0.0F || profile.keeperDepthMetres != 0.0F ||
           profile.keeperSweep != 0.0F;
  };
  for (int role = 1;
       role < static_cast<int>(std::to_underlying(TacticalRole::COUNT)); ++role)
  {
    EXPECT_TRUE(differs(
        Tactics::profile(static_cast<TacticalRole>(role), RoleDuty::Support)))
        << Tactics::roleKey(static_cast<TacticalRole>(role));
  }
  EXPECT_GT(Tactics::profile(TacticalRole::Standard, RoleDuty::Attack)
                .possessionAdvanceMetres,
            Tactics::profile(TacticalRole::Standard, RoleDuty::Defend)
                .possessionAdvanceMetres);
  EXPECT_GT(Tactics::profile(TacticalRole::SweeperKeeper, RoleDuty::Support)
                .keeperDepthMetres,
            Tactics::profile(TacticalRole::LineKeeper, RoleDuty::Support)
                .keeperDepthMetres);
}

TEST(TacticsTest, SlotInstructionsMatchByAnchor)
{
  Strategy strategy;
  strategy.setSlot({.anchor = {.x = 0.20F, .y = 0.12F},
                    .role = TacticalRole::InsideFullBack,
                    .duty = RoleDuty::Attack,
                    .possessionOffset = {.x = 0.1F, .y = 0.2F}});
  ASSERT_NE(strategy.findSlot({0.21F, 0.13F}), nullptr);
  EXPECT_EQ(strategy.findSlot({0.21F, 0.13F})->role,
            TacticalRole::InsideFullBack);
  EXPECT_EQ(strategy.findSlot({0.43F, 0.12F}), nullptr);
  // Re-setting the same slot replaces it.
  strategy.setSlot({.anchor = {.x = 0.20F, .y = 0.12F},
                    .role = TacticalRole::Standard,
                    .duty = RoleDuty::Defend,
                    .possessionOffset = {.x = 0.0F, .y = 0.0F}});
  EXPECT_EQ(strategy.getSlotInstructions().size(), 1U);
  EXPECT_EQ(strategy.findSlot({0.20F, 0.12F})->duty, RoleDuty::Defend);
  // A goalkeeper role outside the goalkeeper family is refused.
  strategy.setKeeperRole(TacticalRole::Poacher);
  EXPECT_EQ(strategy.getKeeperRole(), TacticalRole::Standard);
}

TEST(TacticsTest, AiPicksRolesThatFitThePlayers)
{
  const auto squad = makeSquad(1);
  // A sharp finisher up front, a passer in midfield, a quick keeper.
  auto stats = squad->players[FIRST_STRIKER]->getStats();
  stats["Shooting"] = 92.0F;
  stats["Pace"] = 88.0F;
  stats["Physicality"] = 40.0F;
  squad->players[FIRST_STRIKER]->setStats(stats);
  stats = squad->players[LEFT_CENTRAL_MIDFIELDER]->getStats();
  stats["Passing"] = 90.0F;
  stats["Vision"] = 90.0F;
  stats["Dribbling"] = 80.0F;
  squad->players[LEFT_CENTRAL_MIDFIELDER]->setStats(stats);

  Strategy strategy;
  Tactics::assignRolesToFit(strategy, squad->lineup);
  const SlotInstruction* striker = strategy.findSlot(POSITIONS[FIRST_STRIKER]);
  ASSERT_NE(striker, nullptr);
  EXPECT_EQ(striker->role, TacticalRole::Poacher);
  const SlotInstruction* midfielder =
      strategy.findSlot(POSITIONS[LEFT_CENTRAL_MIDFIELDER]);
  ASSERT_NE(midfielder, nullptr);
  EXPECT_EQ(midfielder->role, TacticalRole::Playmaker);
  for (std::size_t index = 1; index < POSITIONS.size(); ++index)
  {
    const SlotInstruction* slot = strategy.findSlot(POSITIONS[index]);
    ASSERT_NE(slot, nullptr) << index;
    EXPECT_TRUE(
        Tactics::allows(Tactics::familyForSlot(POSITIONS[index]), slot->role))
        << index;
  }
}

TEST(TacticsTest, PossessionShapesMoveTheRightSlots)
{
  const Vector2F leftBack = POSITIONS[LEFT_BACK];
  const Vector2F rightBack = POSITIONS[4];
  const Vector2F striker = POSITIONS[FIRST_STRIKER];
  EXPECT_GT(
      Tactics::possessionOffset(PossessionShape::FullBacksPush, leftBack).x,
      0.1F);
  EXPECT_FLOAT_EQ(
      Tactics::possessionOffset(PossessionShape::FullBacksPush, striker).x,
      0.0F);
  // Back three: the left back tucks in, the right back pushes on.
  const Vector2F tuck =
      Tactics::possessionOffset(PossessionShape::BuildWithThree, leftBack);
  const Vector2F push =
      Tactics::possessionOffset(PossessionShape::BuildWithThree, rightBack);
  EXPECT_GT(tuck.y, 0.05F);
  EXPECT_GT(push.x, tuck.x + 0.1F);
  EXPECT_FLOAT_EQ(
      Tactics::possessionOffset(PossessionShape::KeepShape, leftBack).x, 0.0F);
}

TEST(TacticsTest, AttackingFullBackPlaysHigherThanDefendingOne)
{
  const auto homeSquad = makeSquad(1);
  const auto awaySquad = makeSquad(2);
  const StatsConfig config = createStatsConfig();
  MatchScenario scenario;
  scenario.carrierId = playerId(1, LEFT_CENTRAL_MIDFIELDER);
  scenario.ballPosition = PITCH.at(POSITIONS[LEFT_CENTRAL_MIDFIELDER].x,
                                   POSITIONS[LEFT_CENTRAL_MIDFIELDER].y);
  for (size_t index = 0; index < POSITIONS.size(); ++index)
  {
    scenario.players.push_back(
        {.playerId = playerId(1, index),
         .position = PITCH.at(POSITIONS[index].x, POSITIONS[index].y),
         .makingRun = false});
    scenario.players.push_back({.playerId = playerId(2, index),
                                .position = PITCH.at(1.0F - POSITIONS[index].x,
                                                     1.0F - POSITIONS[index].y),
                                .makingRun = false});
  }
  const auto targetX = [&](RoleDuty duty)
  {
    const Strategy home =
        withSlots({{LEFT_BACK,
                    {.anchor = {},
                     .role = TacticalRole::Standard,
                     .duty = duty,
                     .possessionOffset = {.x = 0.0F, .y = 0.0F}}}});
    MatchEngine engine(homeSquad->lineup, awaySquad->lineup, home, Strategy{},
                       config, 101);
    engine.setControlledPlayer(scenario.carrierId);
    engine.advance(0.1F);
    EXPECT_TRUE(engine.applyScenario(scenario));
    engine.advance(0.1F);
    for (const MatchPlayer& player : engine.getPlayers())
    {
      if (player.player->getId() == playerId(1, LEFT_BACK))
      {
        return PITCH.fractionX(player.tacticalTarget);
      }
    }
    ADD_FAILURE() << "Full-back missing from scenario";
    return 0.0F;
  };
  // Compare duties in the same possession, rather than two whole matches
  // whose shots, turnovers and possession samples diverge with the duty.
  const float attacking = targetX(RoleDuty::Attack);
  const float defending = targetX(RoleDuty::Defend);
  EXPECT_GT(attacking, defending + 0.04F);
}

TEST(TacticsTest, InPossessionShapeMovesTheSlotWithTheBallOnly)
{
  // The left back tucks in with the ball and plays as a full-back without
  // it.
  const StatsConfig config = createStatsConfig();
  const auto homeSquad = makeSquad(1);
  const auto awaySquad = makeSquad(2);
  const auto targetY = [&](bool shifted, bool withBall)
  {
    const Strategy home = withSlots(
        {{LEFT_BACK,
          {.anchor = {},
           .role = TacticalRole::Standard,
           .duty = RoleDuty::Support,
           .possessionOffset = shifted ? Vector2F{.x = 0.0F, .y = 0.18F}
                                       : Vector2F{.x = 0.0F, .y = 0.0F}}}});
    MatchEngine engine(homeSquad->lineup, awaySquad->lineup, home, Strategy{},
                       config, SEEDS[0]);
    MatchScenario scenario;
    scenario.carrierId = playerId(withBall ? 1 : 2, LEFT_CENTRAL_MIDFIELDER);
    scenario.ballPosition = PITCH.at(POSITIONS[LEFT_CENTRAL_MIDFIELDER].x,
                                     POSITIONS[LEFT_CENTRAL_MIDFIELDER].y);
    for (size_t index = 0; index < POSITIONS.size(); ++index)
    {
      scenario.players.push_back(
          {.playerId = playerId(1, index),
           .position = PITCH.at(POSITIONS[index].x, POSITIONS[index].y),
           .makingRun = false});
      scenario.players.push_back(
          {.playerId = playerId(2, index),
           .position =
               PITCH.at(1.0F - POSITIONS[index].x, 1.0F - POSITIONS[index].y),
           .makingRun = false});
    }
    engine.setControlledPlayer(scenario.carrierId);
    engine.advance(0.1F);
    EXPECT_TRUE(engine.applyScenario(scenario));
    engine.advance(0.1F);
    for (const MatchPlayer& player : engine.getPlayers())
    {
      if (player.player->getId() == playerId(1, LEFT_BACK))
      {
        return PITCH.fractionY(player.tacticalTarget);
      }
    }
    ADD_FAILURE() << "Full-back missing from scenario";
    return 0.0F;
  };
  // Comparing whole-match positions also measures different possessions,
  // turnovers and opponent responses. Hold those fixed to test the shape.
  EXPECT_GT(targetY(true, true), targetY(false, true) + 0.06F);
  EXPECT_FLOAT_EQ(targetY(true, false), targetY(false, false));
  // Without the ball the formation is the one set on the lineup.
  const Strategy shifted =
      withSlots({{LEFT_BACK,
                  {.anchor = {},
                   .role = TacticalRole::Standard,
                   .duty = RoleDuty::Support,
                   .possessionOffset = {.x = 0.0F, .y = 0.18F}}}});
  const MatchEngine engine(homeSquad->lineup, awaySquad->lineup, shifted,
                           Strategy{}, config, SEEDS[0]);
  const std::vector<Vector2F> formation = engine.getFormation(true);
  ASSERT_EQ(formation.size(), POSITIONS.size() - 1);
  EXPECT_FLOAT_EQ(formation[0].x, POSITIONS[LEFT_BACK].x);
  EXPECT_FLOAT_EQ(formation[0].y, POSITIONS[LEFT_BACK].y);
}

TEST(TacticsTest, PressingForwardsPressMore)
{
  const auto strikerPressures = [](TacticalRole role)
  {
    const Strategy home = withSlots({{FIRST_STRIKER,
                                      {.anchor = {},
                                       .role = role,
                                       .duty = RoleDuty::Support,
                                       .possessionOffset = {}}},
                                     {FIRST_STRIKER + 1,
                                      {.anchor = {},
                                       .role = role,
                                       .duty = RoleDuty::Support,
                                       .possessionOffset = {}}}});
    int pressures = 0;
    for (const std::uint32_t seed : SEEDS)
    {
      playMatch(
          home, Strategy{}, seed,
          [&pressures](const MatchEngine& engine)
          {
            if (engine.getState() != MatchState::FULL_TIME)
            {
              return;
            }
            for (const std::size_t index : {FIRST_STRIKER, FIRST_STRIKER + 1})
            {
              if (const PlayerMatchStats* stats =
                      statsOf(engine, playerId(1, index)))
              {
                pressures += stats->pressures;
              }
            }
          });
    }
    return pressures;
  };
  const int standard = strikerPressures(TacticalRole::Standard);
  const int pressing = strikerPressures(TacticalRole::PressingForward);
  EXPECT_GT(standard, 0);
  EXPECT_GT(pressing, standard * 1.15)
      << "pressing " << pressing << " standard " << standard;
}

TEST(TacticsTest, TightlyMarkedPlayerGetsFewerTouches)
{
  constexpr std::uint32_t MATCH_SAMPLE_SIZE = 64;
  const PlayerID target = playerId(2, LEFT_CENTRAL_MIDFIELDER);
  const auto touches = [&](bool marked)
  {
    Strategy home;
    if (marked)
    {
      home.setOppositionOrders(
          {{.player = target,
            .instruction = OppositionInstruction::TightMark}});
    }
    int total = 0;
    // Marking changes subsequent passes, shots and possession, so paired
    // full matches diverge even with the same seed. Use a representative
    // sample, including the original four seeds, to measure touch reduction
    // rather than the score and possession noise of a handful of games.
    for (std::uint32_t sample = 1; sample <= MATCH_SAMPLE_SIZE; ++sample)
    {
      playMatch(home, Strategy{}, sample * SEEDS.front(),
                [&total, target](const MatchEngine& engine)
                {
                  if (engine.getState() != MatchState::FULL_TIME)
                  {
                    return;
                  }
                  if (const PlayerMatchStats* stats = statsOf(engine, target))
                  {
                    total += stats->touches;
                  }
                });
    }
    return total;
  };
  const int free = touches(false);
  const int marked = touches(true);
  EXPECT_GT(free, 0);
  EXPECT_LT(marked, free * 0.9) << "marked " << marked << " free " << free;
}

TEST(TacticsTest, OppositionOrdersOnlyFindTheirMan)
{
  // Orders against players who are not on the pitch change nothing.
  Strategy home;
  home.setOppositionOrders(
      {{.player = 999'999, .instruction = OppositionInstruction::Press},
       {.player = 0, .instruction = OppositionInstruction::TightMark}});
  EXPECT_EQ(home.getOppositionOrders().size(), 1U);
  int withOrders = 0;
  int without = 0;
  playMatch(home, Strategy{}, SEEDS[1],
            [&withOrders](const MatchEngine& engine)
            {
              if (engine.getState() == MatchState::FULL_TIME)
              {
                withOrders =
                    engine.getHomeScore() * 100 + engine.getAwayScore();
              }
            });
  playMatch(Strategy{}, Strategy{}, SEEDS[1],
            [&without](const MatchEngine& engine)
            {
              if (engine.getState() == MatchState::FULL_TIME)
              {
                without = engine.getHomeScore() * 100 + engine.getAwayScore();
              }
            });
  EXPECT_EQ(withOrders, without);
}

TEST(TacticsTest, TeamTalkEffectFadesAfterTheStartOfEachHalf)
{
  static const StatsConfig config = createStatsConfig();
  const auto homeSquad = makeSquad(1);
  const auto awaySquad = makeSquad(2);
  MatchEngine engine(homeSquad->lineup, awaySquad->lineup, Strategy{},
                     Strategy{}, config, 77);
  engine.setTeamTalkModifier(true, 1, TacticsTuning::TALK_REFERENCE);
  const auto advanceTo = [&](float minute)
  {
    while (engine.getState() != MatchState::FULL_TIME &&
           (engine.getMatchTimeMinutes() < minute ||
            (minute > 45.0F && engine.getPeriod() < 2)))
    {
      engine.advance(5.0F);
    }
  };
  EXPECT_FLOAT_EQ(engine.getTeamTalkEffect(true),
                  TacticsTuning::TALK_REFERENCE);
  EXPECT_FLOAT_EQ(engine.getTeamTalkEffect(false), 0.0F);
  advanceTo(5.0F);
  EXPECT_FLOAT_EQ(engine.getTeamTalkEffect(true),
                  TacticsTuning::TALK_REFERENCE);
  advanceTo(15.0F);
  const float fading = engine.getTeamTalkEffect(true);
  EXPECT_GT(fading, 0.0F);
  EXPECT_LT(fading, TacticsTuning::TALK_REFERENCE);
  advanceTo(25.0F);
  EXPECT_FLOAT_EQ(engine.getTeamTalkEffect(true), 0.0F);
  // The second half needs its own talk; a poor one deflates the side.
  advanceTo(47.0F);
  ASSERT_EQ(engine.getPeriod(), 2);
  EXPECT_FLOAT_EQ(engine.getTeamTalkEffect(true), 0.0F);
  engine.setTeamTalkModifier(true, 2, -TacticsTuning::TALK_REFERENCE);
  EXPECT_FLOAT_EQ(engine.getTeamTalkEffect(true),
                  -TacticsTuning::TALK_REFERENCE);
  advanceTo(70.0F);
  EXPECT_FLOAT_EQ(engine.getTeamTalkEffect(true), 0.0F);
  EXPECT_FLOAT_EQ(engine.getTeamTalkModifier(true, 2),
                  -TacticsTuning::TALK_REFERENCE);
}

class TacticsPersistenceTest : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    Logger::init();
    connection = std::make_shared<DatabaseConnection>(":memory:");
    connection->initialize();
  }
  std::shared_ptr<DatabaseConnection> connection;
};

TEST_F(TacticsPersistenceTest, RolesAndShapeSurviveASave)
{
  TeamRepository repository(connection);
  Team team(1, 1, "Roles", 1'000'000);
  team.getStrategy().setKeeperRole(TacticalRole::SweeperKeeper);
  team.getStrategy().setSlot({.anchor = {.x = 0.20F, .y = 0.12F},
                              .role = TacticalRole::InsideFullBack,
                              .duty = RoleDuty::Attack,
                              .possessionOffset = {.x = 0.05F, .y = 0.15F}});
  team.getStrategy().setSlot({.anchor = {.x = 0.78F, .y = 0.38F},
                              .role = TacticalRole::FalseNine,
                              .duty = RoleDuty::Defend,
                              .possessionOffset = {.x = -0.1F, .y = 0.0F}});
  // Opposition orders belong to the club's plan, not to the saved tactic.
  team.getStrategy().setOppositionOrders(
      {{.player = 42, .instruction = OppositionInstruction::DoubleUp}});
  repository.insertTeamWithId(team);

  const auto teams = repository.loadAllTeams();
  const auto loaded = std::ranges::find_if(
      teams, [](const Team& candidate) { return candidate.getId() == 1; });
  ASSERT_NE(loaded, teams.end());
  const Strategy& strategy = loaded->getStrategy();
  EXPECT_EQ(strategy.getKeeperRole(), TacticalRole::SweeperKeeper);
  ASSERT_EQ(strategy.getSlotInstructions().size(), 2U);
  const SlotInstruction* back = strategy.findSlot({.x = 0.20F, .y = 0.12F});
  ASSERT_NE(back, nullptr);
  EXPECT_EQ(back->role, TacticalRole::InsideFullBack);
  EXPECT_EQ(back->duty, RoleDuty::Attack);
  EXPECT_FLOAT_EQ(back->possessionOffset.x, 0.05F);
  EXPECT_FLOAT_EQ(back->possessionOffset.y, 0.15F);
  const SlotInstruction* nine = strategy.findSlot({.x = 0.78F, .y = 0.38F});
  ASSERT_NE(nine, nullptr);
  EXPECT_EQ(nine->role, TacticalRole::FalseNine);
  EXPECT_EQ(nine->duty, RoleDuty::Defend);
  EXPECT_FLOAT_EQ(nine->possessionOffset.x, -0.1F);
  EXPECT_TRUE(strategy.getOppositionOrders().empty());
}

TEST_F(TacticsPersistenceTest, OlderSavesGetStandardRoles)
{
  TeamRepository repository(connection);
  Team team(1, 1, "Old", 1'000'000);
  repository.insertTeamWithId(team);
  // A tactic written before roles existed (and one with damaged entries).
  for (const char* json :
       {R"({"compactness":0.6,"offensive_bias":0.4,"pressing":0.7,)"
        R"("risk_taking":0.3,"width_usage":0.55})",
        R"({"pressing":0.7,"keeper_role":99,"slots":[{"x":0.2,"y":0.12,)"
        R"("role":200,"duty":9},5]})"})
  {
    sqlite3_stmt* statement = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(connection->getRaw(),
                                 "UPDATE Teams SET strategy = ? WHERE id = 1",
                                 -1, &statement, nullptr),
              SQLITE_OK);
    sqlite3_bind_text(statement, 1, json, -1, SQLITE_TRANSIENT);
    EXPECT_EQ(sqlite3_step(statement), SQLITE_DONE);
    sqlite3_finalize(statement);

    const auto teams = repository.loadAllTeams();
    const auto loaded = std::ranges::find_if(
        teams, [](const Team& candidate) { return candidate.getId() == 1; });
    ASSERT_NE(loaded, teams.end());
    const Strategy& strategy = loaded->getStrategy();
    EXPECT_FLOAT_EQ(strategy.getSliders().pressing, 0.7F) << json;
    EXPECT_EQ(strategy.getKeeperRole(), TacticalRole::Standard) << json;
    for (const SlotInstruction& slot : strategy.getSlotInstructions())
    {
      EXPECT_EQ(slot.role, TacticalRole::Standard) << json;
      EXPECT_EQ(slot.duty, RoleDuty::Support) << json;
    }
  }
}
