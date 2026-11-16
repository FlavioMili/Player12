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
 * @brief Records book: club records with the club's all-time scorers and
 * appearance makers, league records, the all-time league table and the
 * club's hall of fame.
 */
class RecordsScene : public ManagementScene
{
 public:
  /** @brief Tabs of the records book. */
  enum class Tab : int
  {
    CLUB = 0,
    LEAGUE,
    ALL_TIME,
    HALL_OF_FAME
  };

  explicit RecordsScene(GUIView* parent);

  void update(float /*deltaTime*/) override
  {
    // Rendering reads current state; there is no per-frame update work.
  }
  [[nodiscard]] SceneID getID() const override { return SceneID::RECORDS; }

  /** @brief Shows a tab (e.g. from another screen). */
  void showTab(Tab tab)
  {
    active_tab = tab;
    refresh();
  }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::RECORDS;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  /** @brief One formatted record. */
  struct RecordLine
  {
    std::string title;
    std::string value;
    std::string holder;
    std::string when;
    PlayerID player_id = 0;
  };

  /** @brief One formatted player total. */
  struct PlayerLine
  {
    PlayerID player_id = 0; /**< 0 once he left the database. */
    std::string name;
    std::string span;
    std::string figure;
    std::string detail;
  };

  /** @brief One formatted all-time table row. */
  struct TableLine
  {
    TeamID team_id = 0;
    std::string club;
    uint32_t played = 0;
    uint32_t won = 0;
    uint32_t drawn = 0;
    uint32_t lost = 0;
    uint32_t goals_for = 0;
    uint32_t goals_against = 0;
    uint32_t points = 0;
  };

  void renderClubSelector();
  void renderLeagueSelector();
  void renderRecords(const char* id, const char* title,
                     const std::vector<RecordLine>& lines, float width);
  void renderPlayers(const char* id, const char* title,
                     const std::vector<PlayerLine>& lines, float width);
  void renderAllTime();
  void renderHallOfFame();

  Tab active_tab = Tab::CLUB;
  TeamID club_id = 0;
  LeagueID league_id = 0;
  std::vector<RecordLine> club_records;
  std::vector<RecordLine> league_records;
  std::vector<PlayerLine> scorers;
  std::vector<PlayerLine> appearances;
  std::vector<PlayerLine> legends;
  std::vector<TableLine> all_time;
};
