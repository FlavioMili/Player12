// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gui/scenes/management_scene.h"
#include "gui/widgets/widgets.h"
#include "model/scouting.h"

/**
 * @brief Scouting and recruitment: scouts, assignments, reports, player
 * search on estimated values, shortlist and recruitment focus.
 *
 * The first tab is built around the scouts: clicking one opens his page with
 * his live assignment and the reports he filed so far, his past assignments
 * and their reports, or (when he has done nothing yet) the world picker to
 * send him somewhere, with his expected effectiveness and its reasons.
 *
 * Every figure about another club's player comes from the scouting
 * estimates (GameController::getScoutedView / searchScoutedPlayers), never
 * from the hidden true attributes. Rows are rebuilt on refresh or when a
 * filter changes, never per frame.
 */
class ScoutingScene : public ManagementScene
{
 public:
  /** @brief Tabs of the screen. */
  enum class Tab : uint8_t
  {
    OVERVIEW,
    REPORTS,
    SEARCH,
    SHORTLIST,
    FOCUS
  };

  explicit ScoutingScene(GUIView* parent, Tab initialTab = Tab::OVERVIEW);

  void update(float deltaTime) override;
  [[nodiscard]] SceneID getID() const override { return SceneID::SCOUTING; }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::SCOUTING;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  /** @brief Display-ready estimated player row. */
  struct PlayerLine
  {
    ScoutedPlayerRow row;
    std::string name;
    std::string name_lower;
    std::string club;
    std::string role;
    std::string ability_range;
    std::string potential_text;
    std::string value_text;
  };

  struct ReportLine
  {
    ScoutReport report;
    std::string player_name;
    std::string club;
    std::string date_text;
    std::string potential_text;
    std::string fee_text;
    std::string ability_text; /**< 80% range of the estimated ability. */
    bool fresh = false;       /**< Unread when the scout page opened. */
    bool shortlisted = false;
  };

  struct AssignmentLine
  {
    ScoutAssignment assignment;
    std::string target_text;
    std::string cost_text;
    std::string period_text;
  };

  /** @brief One row of the scouts list. */
  struct ScoutLine
  {
    ScoutSummary summary;
    std::string nationality;
    std::string languages; /**< Short codes, e.g. "IT · EN · ES". */
    std::string judging;
    std::string target;
    float progress = 0.0f;
    int days_left = 0;
  };

  struct CountryNode
  {
    LeagueID id = 0;
    std::string name;
    std::vector<std::pair<LeagueID, std::string>> divisions;
  };

  struct ContinentNode
  {
    Continent continent = Continent::Europe;
    std::string name;
    std::vector<CountryNode> countries;
  };

  struct EffectLine
  {
    std::string text;
    float delta = 0.0f;
  };

  enum class ReportColumn : uint8_t
  {
    PLAYER,
    GRADE,
    ABILITY,
    POTENTIAL,
    FEE,
    DATE
  };

  struct ComparisonLine
  {
    SquadComparisonRow row;
    std::string role;
    std::string own_name;
    std::string candidate_name;
    std::string potential_text;
  };

  enum class SearchColumn : uint8_t
  {
    NAME,
    CLUB,
    ROLE,
    AGE,
    ABILITY,
    POTENTIAL,
    KNOWLEDGE,
    VALUE
  };

  void rebuildReports();
  void rebuildSearch();
  void rebuildShortlist();
  void rebuildScouts();
  void rebuildWorld();
  void rebuildScoutDetail();
  void rebuildScoutReportList();
  void rebuildSendPreview();
  void rebuildPlayerMatches();
  void sortScoutReports();
  void selectScout(uint32_t scoutId);
  ReportLine makeReportLine(const ScoutReport& report) const;
  std::string targetLabel(ScoutTargetKind kind, uint32_t targetId) const;
  std::string effectText(const EffectFactor& factor) const;
  const ScoutLine* selectedScoutLine() const;
  PlayerLine makeLine(const ScoutedPlayerRow& row) const;
  void sortSearch();

  void renderOverview();
  void renderScoutList(float width, float height);
  void renderScoutDetail(float height);
  void renderScoutHeader(const ScoutLine& line) const;
  void renderActiveAssignment(const ScoutLine& line);
  void renderScoutHistory();
  void renderScoutReports(const char* id, float height);
  void renderSendFlow(const ScoutLine& line);
  void renderWorldPicker(float width, float height);
  void renderSendSummary(const ScoutLine& line);
  void renderReports();
  void renderSearch();
  void renderSearchFilters();
  void renderPlayerTable(const char* id, std::vector<PlayerLine>& lines,
                         float height, bool sortable);
  void renderShortlist();
  void renderComparison();
  void renderFocus();
  void renderFocusEditor();
  void renderPlayerActions();

  void selectPlayer(PlayerID playerId, std::string_view name);
  void sendScout(ScoutTargetKind kind, uint32_t targetId);
  bool playerCell(PlayerID playerId, const std::string& name, bool selected);

  std::vector<ScoutProfile> scouts;
  std::vector<ReportLine> reports;
  std::vector<PlayerLine> search_rows;
  std::vector<PlayerLine> shortlist_rows;
  std::vector<ComparisonLine> comparison;
  std::vector<std::pair<LeagueID, std::string>> leagues;

  // Scouts page.
  std::vector<ScoutLine> scout_lines;
  std::vector<ContinentNode> world;
  uint32_t selected_scout = 0;
  ScoutExpertise selected_expertise;
  std::string languages_full;
  std::vector<std::string> expertise_lines;
  std::vector<ReportLine> scout_reports;
  std::vector<AssignmentLine> scout_history;
  uint32_t history_assignment = 0; /**< Past assignment shown, 0 = none. */
  std::vector<uint32_t> fresh_reports;
  ReportColumn report_sort = ReportColumn::DATE;
  bool report_sort_ascending = false;

  // Send flow of the selected scout.
  bool send_open = false;
  ScoutTargetKind send_kind = ScoutTargetKind::League;
  uint32_t send_target = 0;
  int send_days = 30;
  bool send_dirty = true;
  int64_t send_cost = 0;
  ScoutEffectiveness send_effect;
  std::vector<EffectLine> effect_lines;
  std::string send_target_text;
  std::array<char, 48> player_query{};
  std::vector<std::pair<PlayerID, std::string>> player_matches;
  /** Effectiveness of the selected scout per picker entry (kind, id). */
  std::unordered_map<uint64_t, float> picker_multipliers;

  // Search filters.
  ScoutSearchFilter filter;
  int filter_role = 0;   /**< 0 = any, else PlayerRole + 1. */
  int filter_league = 0; /**< 0 = all, 1 = free agents, else leagues[i-2]. */
  int filter_min_age = 16;
  int filter_max_age = 40;
  int filter_min_overall = 0;
  int filter_max_value_m = 0; /**< Millions, 0 = any. */
  int filter_min_knowledge = 0;
  std::array<char, 48> name_query{};
  /** Time left before an edited name filter re-runs the search. */
  float name_settle_seconds = 0.0f;
  bool search_dirty = true;
  SearchColumn sort_column = SearchColumn::ABILITY;
  bool sort_ascending = false;
  /** Columns the player table showed last frame (for the column picker). */
  UI::ColumnMask player_table_mask = ~UI::ColumnMask{0};

  // Focus editor.
  RecruitmentFocus focus_draft;
  int focus_role = 0;
  int focus_min_age = 17;
  int focus_max_age = 30;
  int focus_min_ability = 0;
  int64_t focus_max_fee = 0;
  int64_t focus_max_wage = 0;

  PlayerID selected_player = 0;
  std::string selected_name;
  Tab requested_tab;
  bool tab_pending = true;
};
