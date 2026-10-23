// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "controller/game_controller.h"
#include "gui/scenes/management_scene.h"

/**
 * @brief Pre-match opposition report of the next fixture: form, averages
 * against the league, likely XI (scouted estimates only), key players,
 * strengths and weaknesses, suggested counter-tactics with their reasons
 * and individual instructions against opposing players.
 */
class OppositionScene : public ManagementScene
{
 public:
  explicit OppositionScene(GUIView* parent);

  void update(float /*deltaTime*/) override
  {
    // Rendering reads current state; there is no per-frame update work.
  }
  [[nodiscard]] SceneID getID() const override { return SceneID::OPPOSITION; }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::OPPOSITION;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  void renderSummary() const;
  void renderLikelyXi(float width);
  void renderKeyPlayers(float width);
  void renderProfile(float width) const;
  void renderCounters(float width);

  std::optional<GameController::NextFixture> fixture;
  std::string opponent_name;
  OppositionReport report;
  std::vector<OppositionInstruction> instructions; /*!< Per likely XI row. */
  std::vector<bool> applied;                       /*!< Per counter. */
};
