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

#include "gui/scenes/management_scene.h"
#include "gui/scenes/match_insights_view.h"
#include "model/match_report.h"

/**
 * @brief Post-match report: score, goal and card timeline, team statistics
 * and player ratings for one played fixture; tracked matches of the managed
 * club add an Analysis tab (MatchInsightsView).
 */
class MatchReportScene : public ManagementScene
{
 public:
  MatchReportScene(GUIView* parent, GameDateValue date, TeamID homeId,
                   TeamID awayId);

  void update(float deltaTime) override;
  [[nodiscard]] SceneID getID() const override { return SceneID::MATCH_REPORT; }
  [[nodiscard]] std::optional<NavEntry> historyEntry() const override
  {
    return NavEntry::ofMatchReport(date, home_id, away_id);
  }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::FIXTURES;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  struct EventRow
  {
    std::string minute;
    std::string text;
    std::string assist; /**< Second line under goals (may be empty). */
    MatchEventKind kind;
    bool home;
  };

  static void eventText(const EventRow& event, bool alignRight);

  struct PlayerRow
  {
    PlayerID id = 0;
    std::string name;
    PlayerMatchLine line;
  };

  void renderScore();
  void renderEvents(float width, float height) const;
  void renderStats(float width, float height);
  void renderRatings(const char* id, const std::string& club,
                     const std::vector<PlayerRow>& rows, float width,
                     float height);

  GameDateValue date;
  TeamID home_id;
  TeamID away_id;
  std::optional<MatchReport> report;
  std::string home_name;
  std::string away_name;
  std::string result_note;
  std::vector<EventRow> events;
  std::vector<PlayerRow> home_players;
  std::vector<PlayerRow> away_players;
  MatchInsightsView insights;
  int tab = 0; /*!< 0 overview, 1 analysis. */
};
