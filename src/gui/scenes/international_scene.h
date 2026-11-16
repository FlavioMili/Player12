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

#include "gui/scenes/management_scene.h"
#include "gui/widgets/widgets.h"
#include "model/gamedate.h"

/**
 * @brief International football: the continental club competitions
 * (league-phase table with its qualification zones, matchdays, knockout
 * bracket, entrants and association coefficients) and the national teams
 * (the club's call-ups, fixtures, qualifying and finals groups, caps
 * leaders and the nations' ranking).
 */
class InternationalScene : public ManagementScene
{
 public:
  explicit InternationalScene(GUIView* parent);

  void update(float deltaTime) override;
  [[nodiscard]] SceneID getID() const override
  {
    return SceneID::INTERNATIONAL;
  }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::INTERNATIONAL;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  struct CompetitionChoice
  {
    LeagueID id = 0;
    std::string name;
  };

  struct TableLine
  {
    TeamID id = 0;
    uint16_t position = 0;
    std::string name;
    std::string association;
    uint16_t played = 0;
    uint16_t won = 0;
    uint16_t drawn = 0;
    uint16_t lost = 0;
    int goal_difference = 0;
    uint16_t points = 0;
    std::array<UI::Outcome, 5> form{};
    size_t form_count = 0;
  };

  struct TieLine
  {
    std::string first; /*!< Seeded club (plays the second leg at home). */
    std::string second;
    std::string score;  /*!< Aggregate, or "-" before the first leg. */
    std::string detail; /*!< Legs, extra time, penalties or dates. */
    bool ours = false;
    int winner = 0; /*!< 1 first, 2 second, 0 open. */
  };

  struct RoundBlock
  {
    std::string title;
    std::vector<TieLine> ties;
  };

  struct FixtureLine
  {
    GameDateValue date;
    std::string when;
    TeamID home_id = 0;
    TeamID away_id = 0;
    std::string home;
    std::string away;
    std::string score;
    bool played = false;
    bool ours = false;
  };

  struct EntrantLine
  {
    TeamID id = 0;
    std::string name;
    std::string association;
    std::string route;
    std::string coefficient;
    uint8_t pot = 0;
  };

  struct AssociationLine
  {
    std::string name;
    std::string last_season;
    std::string total;
  };

  struct CalledLine
  {
    PlayerID id = 0;
    std::string name;
    std::string nation;
    std::string caps;
    std::string goals;
    bool away = false; /*!< With the national team now (else announced). */
  };

  struct NationFixture
  {
    std::string when;
    std::string home;
    std::string away;
    std::string score;
    std::string competition;
    bool played = false;
  };

  struct GroupLine
  {
    std::string nation;
    uint8_t played = 0;
    uint8_t won = 0;
    uint8_t drawn = 0;
    uint8_t lost = 0;
    int goal_difference = 0;
    uint16_t points = 0;
    bool through = false;
  };

  struct GroupBlock
  {
    std::string title;
    std::vector<GroupLine> rows;
  };

  struct LeaderLine
  {
    PlayerID id = 0;
    std::string name;
    std::string nation;
    uint16_t caps = 0;
    uint16_t goals = 0;
    bool active = false; /*!< Still playing (profile can be opened). */
  };

  struct RankingLine
  {
    std::string nation;
    std::string coach;
    int rating = 0;
  };

  void refreshContinental();
  void refreshNational();
  void renderContinental();
  void renderNational();
  void renderCompetitionPicker();
  void renderLeagueTable(float width);
  void renderEntrants(float width);
  void renderBracket(float width) const;
  void renderMatchday(float width);
  void renderAssociations(float width);
  void renderCalledUp(float width);
  void renderNationFixtures(float width);
  void renderGroups(float width);
  void renderLeaders(float width);
  void renderRanking(float width);

  int tab = 0;
  int fixture_filter = 0; /*!< National fixtures: 0 upcoming, 1 results. */
  size_t competition_index = 0;
  bool competition_chosen = false;
  int matchday = 0; /*!< Selected league-phase matchday, 1-based. */
  bool matchday_chosen = false;
  GameDateValue refreshed_on;

  // Continental
  std::vector<CompetitionChoice> competitions;
  bool drawn = false;
  std::string draw_date;
  std::string stage;
  std::string club_status;
  std::string holder;
  std::string next_date;
  uint8_t direct_places = 0;
  uint8_t playoff_places = 0;
  uint8_t matchdays = 0;
  std::vector<TableLine> table;
  std::vector<EntrantLine> entrants;
  std::vector<RoundBlock> rounds;
  std::vector<std::vector<FixtureLine>> fixtures_by_matchday;
  std::vector<AssociationLine> associations;

  // National teams
  std::vector<CalledLine> called;
  std::vector<NationFixture> upcoming;
  std::vector<NationFixture> results;
  std::string groups_title;
  std::vector<GroupBlock> groups;
  std::vector<std::string> knockout_lines;
  std::vector<LeaderLine> leaders;
  std::vector<RankingLine> ranking;
  std::string next_international;
  std::string cycle;
  std::string champion;
};
