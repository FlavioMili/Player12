// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "controller/game_controller.h"
#include "gui/scenes/management_scene.h"

/**
 * @brief Club overview: board objective and confidence, stadium and ticket
 * pricing, facilities and the competition roll of honour.
 */
class ClubScene : public ManagementScene
{
 public:
  explicit ClubScene(GUIView* parent);

  void update(float deltaTime) override;
  [[nodiscard]] SceneID getID() const override { return SceneID::CLUB; }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::CLUB;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  /** @brief One formatted roll-of-honour line. */
  struct HistoryRow
  {
    std::string season;
    std::string competition;
    std::string champion;
    std::string runner_up;
    std::string top_scorer;
    std::string movements;
  };

  void renderBoard(float width);
  void renderTargets(const GameController::BoardTargets& targets,
                     float keyWidth) const;
  void renderStadium(float width);
  void renderSupporters(float width);
  void renderHistory() const;

  /** @brief One reason behind the supporters' mood, localised. */
  struct SupporterLine
  {
    std::string text;
    float points = 0.0f;
  };

  std::vector<HistoryRow> history;
  std::optional<GameController::BoardTargets> board_targets;
  std::vector<float> confidence_trend;
  int league_position = 0;
  int ticket_price_input = 0;
  uint32_t fair_ticket_price = 0;
  uint32_t last_attendance = 0;
  /** Weekly supporters' mood; unset until the first verdict. */
  std::optional<float> supporters_index;
  std::vector<SupporterLine> supporter_reasons;
};
