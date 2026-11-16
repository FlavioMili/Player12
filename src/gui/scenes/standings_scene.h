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

#include "gui/scenes/draw_ceremony_dialog.h"
#include "gui/scenes/management_scene.h"
#include "gui/view_models/competition_view.h"
#include "model/competition.h"
#include "model/standings_race.h"

/**
 * @brief Competitions: the full table of any league (form, promotion and
 * relegation zones, top scorers) or the draw of any national cup.
 */
class StandingsScene : public ManagementScene
{
 public:
  explicit StandingsScene(GUIView* parent);

  void update(float deltaTime) override;
  [[nodiscard]] SceneID getID() const override { return SceneID::STANDINGS; }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::STANDINGS;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;
  friend class GameFlowTest_ManagementScreensMidSeason_Test;
  /** @brief One formatted top-scorer line. */
  struct ScorerRow
  {
    PlayerID id = 0;
    std::string name;
    std::string club;
    uint16_t goals = 0;
    uint16_t assists = 0;
  };

  void renderCompetitionSelector();
  void renderSeasonSelector();
  void renderHighlights();
  void renderTable(float width, float height);
  void renderStandingRow(size_t index, UI::ColumnMask mask, TeamID clubId,
                         size_t continental) const;
  void renderScorers(float width, float height);
  void renderCup();
  void renderCupSummary(TeamID clubId) const;
  void renderCupDraw(TeamID clubId);
  void renderCupTie(const Match& match, size_t id, TeamID clubId) const;
  void renderCupBracketRows(TeamID clubId) const;
  /** Clinched and lost places of every club, and the managed club's run-in. */
  void refreshRace();
  void renderNeeds() const;
  /** Loads an archived final table and its top scorer. */
  void refreshPastSeason(uint16_t season);
  [[nodiscard]] std::string seasonLabel(uint16_t season) const;

  LeagueID league_id = 0;
  bool league_chosen = false;
  bool showing_cup = false;
  LeagueID cup_id = 0;
  std::vector<CompetitionView::StandingRow> table;
  CompetitionView::Zones zones;
  /** Deciding places of the shown league (continental places included). */
  Standings::RacePlaces places;
  /** Mathematically decided status of each table row (current season). */
  std::vector<Standings::Clinch> clinch;
  /** What the managed club needs from its own games, ready to show. */
  std::vector<std::string> need_lines;
  /** Columns the table showed last frame (for the column picker). */
  UI::ColumnMask table_mask = ~UI::ColumnMask{0};
  std::vector<ScorerRow> scorers;
  std::optional<Competitions::CupStatus> cup;
  std::vector<std::pair<LeagueID, std::string>> cup_names;
  /** Archived seasons of the shown league, newest first. */
  std::vector<uint16_t> past_seasons;
  /** The archived season shown (nullopt: the current table). */
  std::optional<uint16_t> past_season;
  /** Ceremony of the cup's latest draw. */
  DrawCeremonyDialog draw_dialog;
};
