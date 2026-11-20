// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_scenario.h"

#include <cmath>
#include <format>
#include <fstream>
#include <limits>
#include <optional>
#include <vector>

#include "model/match_engine.h"
#include "model/player.h"
#include "model/role_utils.h"
#include "tools/match_sandbox_names.h"
#include "tools/match_sandbox_recorder.h"
#include "tools/match_sandbox_recording.h"

namespace
{
/** Where an unused dummy (a side short of eleven) is parked. */
constexpr Vector2F PARKED{.x = 0.02F, .y = 0.02F};
/** Dummy slots per side: home 0-10, away 11-21. */
constexpr std::size_t SLOTS_PER_SIDE = 11;
constexpr int SECONDS_PER_MINUTE = 60;

const char* actionEnum(ScenarioAction action)
{
  switch (action)
  {
    case ScenarioAction::NONE:
      return "ScenarioAction::NONE";
    case ScenarioAction::SHOT:
      return "ScenarioAction::SHOT";
    case ScenarioAction::PASS:
      return "ScenarioAction::PASS";
    case ScenarioAction::CARRY:
      return "ScenarioAction::CARRY";
    case ScenarioAction::SHIELD:
      return "ScenarioAction::SHIELD";
    case ScenarioAction::CLEAR:
      return "ScenarioAction::CLEAR";
  }
  return "ScenarioAction::NONE";
}

std::string score(float value)
{
  return std::isfinite(value) ? std::format("{:.2f}", value) : "-";
}
}  // namespace

std::string ScenarioExport::testCase(const MatchEngine& before,
                                     const SandboxRecorder& names,
                                     const MatchDecisionDetail& decision,
                                     std::uint64_t tick, float minute)
{
  // Dummy slots: home 0-10, away 11-21; the keeper takes slot 0 (11).
  struct Placed
  {
    std::size_t slot = 0;
    const MatchPlayer* player = nullptr;
  };
  std::vector<Placed> placed;
  std::optional<std::size_t> carrierSlot;
  std::optional<std::size_t> receiverSlot;
  for (const bool home : {true, false})
  {
    const std::size_t base = home ? 0 : SLOTS_PER_SIDE;
    std::size_t next = 1;
    for (const MatchPlayer& player : before.getPlayers())
    {
      if ((player.player == nullptr) || !player.onPitch ||
          player.isHomeTeam != home)
      {
        continue;
      }
      std::size_t slot = 0;
      if (player.isGoalkeeper)
      {
        slot = base;
      }
      else if (next < SLOTS_PER_SIDE)
      {
        slot = base + next++;
      }
      else
      {
        continue;
      }
      placed.push_back({.slot = slot, .player = &player});
      if (player.player->getId() == decision.player)
      {
        carrierSlot = slot;
      }
    }
  }
  // The best pass's receiver, for the comment.
  PlayerID bestReceiver = 0;
  float bestUtility = -std::numeric_limits<float>::infinity();
  for (const PassCandidateDetail& candidate : decision.candidates)
  {
    if (candidate.excluded == PassExclusion::NONE &&
        candidate.utility.total > bestUtility)
    {
      bestUtility = candidate.utility.total;
      bestReceiver = candidate.receiver;
    }
  }
  for (const Placed& entry : placed)
  {
    if (entry.player->player->getId() == bestReceiver)
    {
      receiverSlot = entry.slot;
    }
  }

  const int seconds = static_cast<int>(std::floor(minute * 60.0F));
  std::string text = std::format(
      "// Exported from the match sandbox at tick {} ({:02}:{:02}).\n", tick,
      seconds / SECONDS_PER_MINUTE, seconds % SECONDS_PER_MINUTE);
  text += std::format(
      "// {} chose {}: pass {}  shot {}  carry {}  shield {} (before noise).\n",
      names.playerName(decision.player), SandboxNames::choice(decision.chosen),
      score(decision.options[0].total), score(decision.options[1].total),
      score(decision.options[2].total), score(decision.options[3].total));
  if (bestReceiver != 0)
  {
    text += std::format("// Best pass: {} (slot {}), utility {}.\n",
                        names.playerName(bestReceiver),
                        receiverSlot ? std::to_string(*receiverSlot) : "?",
                        score(bestUtility));
  }
  text +=
      "// The dummy teams are uniformly rated with fixed roles: this "
      "reproduces the\n// positions, not the real players' attributes or "
      "roles. Write the expectation\n// this moment should meet.\n";
  text += std::format("TEST(MatchScenarioTest, ExportedTick{})\n{{\n", tick);
  text +=
      "  std::vector<std::unique_ptr<Player>> pool;\n"
      "  Team home = createDummyTeam(1, \"Home\", 70, pool);\n"
      "  Team away = createDummyTeam(2, \"Away\", 70, pool);\n"
      "  const Ids ids = collectIds(pool);\n"
      "  const StatsConfig config = createStatsConfig();\n\n"
      "  ScenarioSpec spec;\n";
  std::vector<bool> used(2 * SLOTS_PER_SIDE, false);
  for (const Placed& entry : placed)
  {
    used[entry.slot] = true;
    const MatchPlayer& player = *entry.player;
    text += std::format(
        "  spec.layout[slotId(ids, {})] = {{{:.4f}f, {:.4f}f}};  // {} ({}, "
        "{})\n",
        entry.slot, MatchPitch::matchSize().fractionX(player.position),
        MatchPitch::matchSize().fractionY(player.position),
        player.player->getName(), RoleUtils::toString(player.player->getRole()),
        player.isHomeTeam ? "home" : "away");
    if (player.isMakingRun)
    {
      text +=
          std::format("  spec.makeRuns[slotId(ids, {})] = true;\n", entry.slot);
    }
  }
  for (std::size_t slot = 0; slot < used.size(); ++slot)
  {
    if (!used[slot])
    {
      text += std::format(
          "  spec.layout[slotId(ids, {})] = {{{:.2f}f, {:.2f}f}};  // not on "
          "the pitch\n",
          slot, PARKED.x, PARKED.y);
    }
  }
  text += std::format("  spec.carrierId = slotId(ids, {});\n\n",
                      carrierSlot ? *carrierSlot : 0);
  text += std::format(
      "  MatchEngine engine = buildEngine(home, away, config);\n"
      "  ASSERT_TRUE(engine.applyScenario(buildScenario(spec)));\n"
      "  const ScenarioDecision& decision = engine.getLastScenarioDecision();\n"
      "  // Recorded in the match: {}.\n"
      "  EXPECT_EQ(decision.action, {});\n}}\n",
      SandboxNames::choice(decision.chosen), actionEnum(decision.chosen));
  return text;
}

std::filesystem::path ScenarioExport::save(const std::string& text,
                                           std::uint64_t tick,
                                           std::string& error)
{
  const std::filesystem::path folder = Recordings::folder() / "scenarios";
  std::error_code problem;
  std::filesystem::create_directories(folder, problem);
  const std::filesystem::path path =
      folder / std::format("scenario tick {}.cpp.txt", tick);
  std::ofstream file(path, std::ios::binary);
  if (!file || !(file << text))
  {
    error = "cannot write " + path.string();
    return {};
  }
  return path;
}
