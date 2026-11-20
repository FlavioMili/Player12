// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_setup.h"

#include <exception>
#include <format>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "model/game.h"
#include "model/match.h"
#include "model/match_context.h"
#include "model/match_engine.h"
#include "model/player.h"
#include "model/team.h"

using nlohmann::json;

std::array<BrainId, 2>& sandboxBrains()
{
  static std::array<BrainId, 2> brains{BrainId::BINTURONG, BrainId::BINTURONG};
  return brains;
}

std::array<BodyId, 2>& sandboxBodies()
{
  static std::array<BodyId, 2> bodies{BodyId::BINTURONG, BodyId::BINTURONG};
  return bodies;
}

HandshakeId& sandboxHandshake()
{
  static HandshakeId handshake = HandshakeId::BINTURONG;
  return handshake;
}

RefereeId& sandboxReferee()
{
  static RefereeId referee = RefereeId::BINTURONG;
  return referee;
}

std::array<ManagerId, 2>& sandboxManagers()
{
  static std::array<ManagerId, 2> managers{ManagerId::BINTURONG,
                                           ManagerId::BINTURONG};
  return managers;
}

BallPhysicsId& sandboxBallPhysics()
{
  static BallPhysicsId physics = BallPhysicsId::BINTURONG;
  return physics;
}

OutOfPlayId& sandboxOutOfPlay()
{
  static OutOfPlayId version = OutOfPlayId::BINTURONG;
  return version;
}

std::optional<MatchPitch>& sandboxDrillPitch()
{
  static std::optional<MatchPitch> pitch;
  return pitch;
}

float& sandboxHomeAdvantage()
{
  static float homeAdvantage = 0.0F;
  return homeAdvantage;
}

PossessionId& sandboxPossession()
{
  static PossessionId version = PossessionId::BINTURONG;
  return version;
}

void applyHomeAdvantage(MatchEngine& engine, float scale)
{
  // Setting a context redraws the referee, so the default is left alone.
  if (scale == 1.0F)
  {
    return;
  }
  MatchContext context;
  context.homeAdvantageScale = scale;
  engine.setMatchContext(context);
}

MatchSetup MatchSetup::capture(const GameController& controller, TeamID home,
                               TeamID away, std::uint32_t seed,
                               bool fullFamiliarity)
{
  MatchSetup setup;
  setup.worldSeed = controller.getWorldSeed();
  setup.matchSeed = seed;
  const std::array<TeamID, 2> ids{home, away};
  for (std::size_t index = 0; index < ids.size(); ++index)
  {
    const auto team = controller.getTeamById(ids[index]);
    if (!team)
    {
      continue;
    }
    Side& side = setup.sides[index];
    const Lineup& lineup = team->get().getLineup();
    side.team = ids[index];
    side.name = team->get().getName();
    side.strategy = team->get().getStrategy();
    side.designations = lineup.getDesignations();
    // Every player starts with his persistent condition (as carryCondition).
    const auto squad = [&setup](const Player* player)
    {
      if (player)
      {
        setup.conditions.emplace_back(player->getId(),
                                      player->getDynamics().condition / 100.0F);
      }
    };
    if (const Player* keeper = lineup.getGoalkeeper())
    {
      side.goalkeeper = keeper->getId();
      squad(keeper);
    }
    for (const Lineup::PositionedPlayer& slot : lineup.getOutfieldPlayers())
    {
      if (slot.player == nullptr)
      {
        continue;
      }
      side.outfield.emplace_back(slot.player->getId(), slot.position);
      squad(slot.player);
    }
    for (const Player* reserve : lineup.getReserves())
    {
      if (reserve == nullptr)
      {
        continue;
      }
      side.reserves.push_back(reserve->getId());
      squad(reserve);
    }
    setup.familiarity[index] =
        fullFamiliarity ? 1.0F : controller.getTacticalFamiliarity(ids[index]);
  }
  if (const Game* game = controller.getGame())
  {
    if (const auto team = controller.getTeamById(home))
    {
      setup.medicalFlags = MatchdaySquad::medicalFlags(game->getMedical(),
                                                       team->get().getLineup());
    }
  }
  setup.autoSubstitutions = {controller.isDelegated(Duty::Substitutions), true};
  setup.brains = sandboxBrains();
  setup.bodies = sandboxBodies();
  setup.handshake = sandboxHandshake();
  setup.referee = sandboxReferee();
  setup.managers = sandboxManagers();
  setup.ballPhysics = sandboxBallPhysics();
  setup.outOfPlay = sandboxOutOfPlay();
  setup.possession = sandboxPossession();
  setup.homeAdvantage = sandboxHomeAdvantage();
  return setup;
}

std::unique_ptr<MatchEngine> MatchSetup::build(const GameController& controller,
                                               std::string& error) const
{
  if (possession != PossessionId::AARDVARK &&
      possession != PossessionId::BINTURONG)
  {
    error = "setup: invalid possession version";
    return nullptr;
  }
  const auto data = controller.getGameData();
  if (!data)
  {
    error = "no world loaded";
    return nullptr;
  }
  const auto find = [&](PlayerID id) -> const Player*
  {
    const auto player = data->getPlayer(id);
    if (!player)
    {
      error = std::format("player {} is not in this world (world seed {}?)", id,
                          worldSeed);
    }
    return player ? &player->get() : nullptr;
  };
  std::array<Lineup, 2> lineups;
  for (std::size_t index = 0; index < sides.size(); ++index)
  {
    const Side& side = sides[index];
    Lineup& lineup = lineups[index];
    const Player* keeper = find(side.goalkeeper);
    if (keeper == nullptr)
    {
      return nullptr;
    }
    lineup.setGoalkeeper(keeper);
    for (const auto& [id, position] : side.outfield)
    {
      const Player* player = find(id);
      if (player == nullptr)
      {
        return nullptr;
      }
      lineup.addOutfieldPlayer(player, position);
    }
    std::vector<const Player*> reserves;
    for (const PlayerID id : side.reserves)
    {
      const Player* player = find(id);
      if (player == nullptr)
      {
        return nullptr;
      }
      reserves.push_back(player);
    }
    lineup.setReserves(reserves);
    lineup.setDesignations(side.designations);
  }

  auto engine = std::make_unique<MatchEngine>(
      lineups[0], lineups[1], sides[0].strategy, sides[1].strategy,
      controller.getStatsConfig(), matchSeed, handshake, possession);
  applyHomeAdvantage(*engine, homeAdvantage);
  engine->setTeamNames(sides[0].name, sides[1].name);
  for (const auto& [id, condition] : conditions)
  {
    engine->setPlayerCondition(id, condition);
  }
  for (const auto& [id, flags] : medicalFlags)
  {
    engine->setMedicalFlags(id, flags);
  }
  engine->setTacticalFamiliarity(true, familiarity[0]);
  engine->setTacticalFamiliarity(false, familiarity[1]);
  engine->setAutoSubstitutions(autoSubstitutions[0], autoSubstitutions[1]);
  engine->setBrain(true, brains[0]);
  engine->setBrain(false, brains[1]);
  engine->setBody(true, bodies[0]);
  engine->setBody(false, bodies[1]);
  engine->setReferee(referee);
  engine->setManager(true, managers[0]);
  engine->setManager(false, managers[1]);
  engine->setBallPhysics(ballPhysics);
  engine->setOutOfPlay(outOfPlay);
  return engine;
}

// --- JSON ------------------------------------------------------------------

json SandboxJson::toJson(const Strategy& strategy)
{
  const StrategySliders sliders = strategy.getSliders();
  json slots = json::array();
  for (const SlotInstruction& slot : strategy.getSlotInstructions())
  {
    slots.push_back(
        {{"anchor", {slot.anchor.x, slot.anchor.y}},
         {"role", static_cast<int>(slot.role)},
         {"duty", static_cast<int>(slot.duty)},
         {"offset", {slot.possessionOffset.x, slot.possessionOffset.y}}});
  }
  json orders = json::array();
  for (const PlayerInstruction& order : strategy.getOppositionOrders())
  {
    orders.push_back({{"player", order.player},
                      {"instruction", static_cast<int>(order.instruction)}});
  }
  return {{"sliders",
           {sliders.pressing, sliders.riskTaking, sliders.offensiveBias,
            sliders.widthUsage, sliders.compactness}},
          {"keeper_role", static_cast<int>(strategy.getKeeperRole())},
          {"slots", slots},
          {"opposition_orders", orders}};
}

Strategy SandboxJson::strategyFromJson(const json& value)
{
  Strategy strategy;
  const auto& sliders = value.at("sliders");
  strategy.setAllSliders({.pressing = sliders.at(0).get<float>(),
                          .riskTaking = sliders.at(1).get<float>(),
                          .offensiveBias = sliders.at(2).get<float>(),
                          .widthUsage = sliders.at(3).get<float>(),
                          .compactness = sliders.at(4).get<float>()});
  strategy.setKeeperRole(
      static_cast<TacticalRole>(value.at("keeper_role").get<int>()));
  std::vector<SlotInstruction> slots;
  for (const auto& item : value.at("slots"))
  {
    SlotInstruction slot;
    slot.anchor = {.x = item.at("anchor").at(0).get<float>(),
                   .y = item.at("anchor").at(1).get<float>()};
    slot.role = static_cast<TacticalRole>(item.at("role").get<int>());
    slot.duty = static_cast<RoleDuty>(item.at("duty").get<int>());
    slot.possessionOffset = {.x = item.at("offset").at(0).get<float>(),
                             .y = item.at("offset").at(1).get<float>()};
    slots.push_back(slot);
  }
  strategy.setSlotInstructions(std::move(slots));
  std::vector<PlayerInstruction> orders;
  for (const auto& item : value.at("opposition_orders"))
  {
    orders.push_back({.player = item.at("player").get<PlayerID>(),
                      .instruction = static_cast<OppositionInstruction>(
                          item.at("instruction").get<int>())});
  }
  strategy.setOppositionOrders(std::move(orders));
  return strategy;
}

json SandboxJson::toJson(const MatchSetup& setup)
{
  json sides = json::array();
  for (const MatchSetup::Side& side : setup.sides)
  {
    json outfield = json::array();
    for (const auto& [id, position] : side.outfield)
    {
      outfield.push_back({id, position.x, position.y});
    }
    sides.push_back({{"team", side.team},
                     {"name", side.name},
                     {"goalkeeper", side.goalkeeper},
                     {"outfield", outfield},
                     {"reserves", side.reserves},
                     {"designations", side.designations},
                     {"strategy", toJson(side.strategy)}});
  }
  json conditions = json::array();
  for (const auto& [id, condition] : setup.conditions)
  {
    conditions.push_back({id, condition});
  }
  json medical = json::array();
  for (const auto& [id, flags] : setup.medicalFlags)
  {
    medical.push_back({id, flags});
  }
  return {{"world_seed", setup.worldSeed},
          {"match_seed", setup.matchSeed},
          {"sides", sides},
          {"familiarity", setup.familiarity},
          {"conditions", conditions},
          {"medical_flags", medical},
          {"auto_substitutions", setup.autoSubstitutions},
          {"brains", {brainKey(setup.brains[0]), brainKey(setup.brains[1])}},
          {"bodies", {bodyKey(setup.bodies[0]), bodyKey(setup.bodies[1])}},
          {"handshake", handshakeKey(setup.handshake)},
          {"referee", refereeKey(setup.referee)},
          {"managers",
           {managerKey(setup.managers[0]), managerKey(setup.managers[1])}},
          {"ball_physics", ballPhysicsKey(setup.ballPhysics)},
          {"out_of_play", outOfPlayKey(setup.outOfPlay)},
          {"possession", possessionKey(setup.possession)},
          {"home_advantage", setup.homeAdvantage}};
}

bool SandboxJson::fromJson(const json& value, MatchSetup& setup,
                           std::string& error)
{
  try
  {
    setup = MatchSetup{};
    setup.worldSeed = value.at("world_seed").get<std::uint64_t>();
    setup.matchSeed = value.at("match_seed").get<std::uint32_t>();
    const auto& sides = value.at("sides");
    for (std::size_t index = 0; index < setup.sides.size(); ++index)
    {
      const auto& item = sides.at(index);
      MatchSetup::Side& side = setup.sides[index];
      side.team = item.at("team").get<TeamID>();
      side.name = item.at("name").get<std::string>();
      side.goalkeeper = item.at("goalkeeper").get<PlayerID>();
      for (const auto& slot : item.at("outfield"))
      {
        side.outfield.emplace_back(slot.at(0).get<PlayerID>(),
                                   Vector2F{.x = slot.at(1).get<float>(),
                                            .y = slot.at(2).get<float>()});
      }
      side.reserves = item.at("reserves").get<std::vector<PlayerID>>();
      side.designations = item.at("designations").get<SetPieceDesignations>();
      side.strategy = strategyFromJson(item.at("strategy"));
    }
    setup.familiarity = value.at("familiarity").get<std::array<float, 2>>();
    for (const auto& item : value.at("conditions"))
    {
      setup.conditions.emplace_back(item.at(0).get<PlayerID>(),
                                    item.at(1).get<float>());
    }
    for (const auto& item : value.at("medical_flags"))
    {
      setup.medicalFlags.emplace_back(item.at(0).get<PlayerID>(),
                                      item.at(1).get<std::uint8_t>());
    }
    setup.autoSubstitutions =
        value.at("auto_substitutions").get<std::array<bool, 2>>();
    // Older setups have no brains: both sides played the Aardvark brain.
    if (const auto brains = value.find("brains"); brains != value.end())
    {
      for (std::size_t index = 0; index < setup.brains.size(); ++index)
      {
        const auto key = brains->at(index).get<std::string>();
        const auto brain = brainFromKey(key);
        if (!brain)
        {
          error = std::format("setup: unknown brain '{}'", key);
          return false;
        }
        setup.brains[index] = *brain;
      }
    }
    // Likewise older setups have no bodies: both sides played the Aardvark.
    if (const auto bodies = value.find("bodies"); bodies != value.end())
    {
      for (std::size_t index = 0; index < setup.bodies.size(); ++index)
      {
        const auto key = bodies->at(index).get<std::string>();
        const auto body = bodyFromKey(key);
        if (!body)
        {
          error = std::format("setup: unknown body '{}'", key);
          return false;
        }
        setup.bodies[index] = *body;
      }
    }
    // And no handshake: they were built with the Aardvark.
    if (const auto handshake = value.find("handshake");
        handshake != value.end())
    {
      const auto key = handshake->get<std::string>();
      const auto converted = handshakeFromKey(key);
      if (!converted)
      {
        error = std::format("setup: unknown handshake '{}'", key);
        return false;
      }
      setup.handshake = *converted;
    }
    // Nor a referee, AI managers, ball physics or out of play: all were the
    // Aardvark.
    if (const auto referee = value.find("referee"); referee != value.end())
    {
      const auto key = referee->get<std::string>();
      const auto converted = refereeFromKey(key);
      if (!converted)
      {
        error = std::format("setup: unknown referee '{}'", key);
        return false;
      }
      setup.referee = *converted;
    }
    if (const auto managers = value.find("managers"); managers != value.end())
    {
      for (std::size_t index = 0; index < setup.managers.size(); ++index)
      {
        const auto key = managers->at(index).get<std::string>();
        const auto manager = managerFromKey(key);
        if (!manager)
        {
          error = std::format("setup: unknown manager '{}'", key);
          return false;
        }
        setup.managers[index] = *manager;
      }
    }
    if (const auto physics = value.find("ball_physics"); physics != value.end())
    {
      const auto key = physics->get<std::string>();
      const auto converted = ballPhysicsFromKey(key);
      if (!converted)
      {
        error = std::format("setup: unknown ball physics '{}'", key);
        return false;
      }
      setup.ballPhysics = *converted;
    }
    if (const auto version = value.find("out_of_play"); version != value.end())
    {
      const auto key = version->get<std::string>();
      const auto converted = outOfPlayFromKey(key);
      if (!converted)
      {
        error = std::format("setup: unknown out of play '{}'", key);
        return false;
      }
      setup.outOfPlay = *converted;
    }
    if (const auto version = value.find("possession"); version != value.end())
    {
      const auto key = version->get<std::string>();
      const auto converted = possessionFromKey(key);
      if (!converted)
      {
        error = std::format("setup: unknown possession '{}'", key);
        return false;
      }
      setup.possession = *converted;
    }
    // Nor a home advantage: they were played with the full edge.
    if (const auto home = value.find("home_advantage"); home != value.end())
    {
      setup.homeAdvantage = home->get<float>();
    }
    return true;
  }
  catch (const std::exception& problem)
  {
    error = std::string("setup: ") + problem.what();
    return false;
  }
}
