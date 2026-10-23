// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <string>
#include <vector>

#include "controller/game_controller.h"
#include "gui/scenes/management_scene.h"

/**
 * @brief Data hub: the managed club's season in numbers. Team tab: xG
 * trend, finishing and goalkeeping against the chances, league comparison
 * with ranks, shot map, set pieces and player leaderboards; Players tab:
 * per-90 table with rating trends, pass involvement, expected assists,
 * progressive passes and pressures. Every figure states its definition and
 * sample size.
 */
class DataHubScene : public ManagementScene
{
 public:
  explicit DataHubScene(GUIView* parent);

  void update(float /*deltaTime*/) override
  {
    // Rendering reads current state; there is no per-frame update work.
  }
  [[nodiscard]] SceneID getID() const override { return SceneID::DATA_HUB; }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::DATA_HUB;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  struct PlayerRow
  {
    PlayerID id = 0;
    std::string name;
    std::string role;
    PlayerAnalyticsRow stats;
  };

  void renderTeam();
  void renderPlayers();
  void renderTrendChart(float width);
  void renderShotMap(float width);
  void renderSetPieces(float width);
  void renderComparison(float width);
  void renderPerformance(float width);
  void renderLeaders(float width);

  /** A leaderboard line, formatted once in refresh(). */
  struct LeaderRow
  {
    PlayerID id = 0;
    std::string name;
    std::string short_name;
    std::string value;
    std::string tooltip;
  };

  GameController::DataHubView hub;
  std::vector<PlayerRow> players;
  std::vector<std::string> opponent_names;       /*!< Per trend point. */
  std::vector<std::string> performance_tooltips; /*!< Per trend point. */
  float performance_top = 0.5f;
  std::array<std::vector<LeaderRow>, LEADER_METRIC_COUNT> leaders;
  std::string leaders_note;
  int tab = 0;
  int shot_side = 0; /*!< 0 = shots for, 1 = shots against. */
};
