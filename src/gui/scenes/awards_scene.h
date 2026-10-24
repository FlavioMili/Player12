// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gui/scenes/management_scene.h"

/**
 * @brief League honours: the running award race, the season awards and the
 * Team of the Season, and every monthly winner of a league and season.
 */
class AwardsScene : public ManagementScene
{
 public:
  explicit AwardsScene(GUIView* parent);

  void update(float /*deltaTime*/) override
  {
    // Rendering reads current state; there is no per-frame update work.
  }
  [[nodiscard]] SceneID getID() const override { return SceneID::AWARDS; }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::AWARDS;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  /** @brief A winner line: who, for whom and the figure. */
  struct Winner
  {
    std::string award;
    PlayerID player_id = 0; /**< 0 for manager awards. */
    std::string name;
    std::string club;
    std::string figure;
  };

  /** @brief One month of the monthly awards table. */
  struct MonthRow
  {
    std::string month;
    Winner player;
    Winner young;
    Winner manager;
    Winner goal;
  };

  /** @brief One line of the award race. */
  struct RaceRow
  {
    PlayerID player_id = 0;
    std::string name;
    std::string club;
    std::string rating;
    std::string contribution;
    bool qualified = false;
  };

  void renderSelectors();
  void renderRace(float width);
  void renderSeason(float width);
  void renderTeamOfSeason(float width);
  void renderMonths();
  void renderWinner(const Winner& winner, float width);

  LeagueID league_id = 0;
  uint16_t season_year = 0;
  std::vector<uint16_t> seasons;
  bool current_season = true;
  std::vector<Winner> season_winners;
  std::vector<Winner> team_of_season;
  std::vector<MonthRow> months;
  std::vector<RaceRow> race;
  std::vector<RaceRow> young_race;
};
