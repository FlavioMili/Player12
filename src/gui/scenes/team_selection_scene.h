// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once
#include <imgui.h>

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "gui/gui_scene.h"
#include "gui/scenes/manager_scene.h"
#include "model/league.h"
#include "model/team.h"

/**
 * @brief Scene for selecting a team.
 */
class TeamSelectionScene : public GUIScene
{
 public:
  /**
   * @brief Constructs a new TeamSelectionScene.
   * @param parent Pointer to the GUIView.
   */
  explicit TeamSelectionScene(GUIView* parent);

  /**
   * @brief Destroys the TeamSelectionScene.
   */
  ~TeamSelectionScene() override = default;

  /**
   * @brief Called when entering the scene.
   */
  void onEnter() override;

  /**
   * @brief Updates scene logic.
   * @param deltaTime Time elapsed since last update.
   */
  void update(float deltaTime) override;

  /**
   * @brief Renders the scene.
   */
  void render() override;

  /**
   * @brief Gets the ID of this scene.
   * @return The SceneID (TEAM_SELECTION).
   */
  [[nodiscard]] SceneID getID() const override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;
  /** @brief Cached, display-ready summary of one selectable club. */
  struct ClubSummary
  {
    TeamID id = 0;
    std::string name;
    uint8_t reputation = 0;
    uint32_t stadium = 0;
    size_t squad_size = 0;
    float average_overall = 0.0f;
    float best_overall = 0.0f;
    int64_t balance = 0;
    int64_t weekly_wages = 0;
    int expected_position = 0;
    const char* objective_key = "";
    int difficulty = 0;   /**< 0 easy .. 3 very hard. */
    std::string code;     /**< Short code shown on the badge. */
    uint32_t primary = 0; /**< Kit colours, 0xRRGGBB. */
    uint32_t secondary = 0;
    std::string nickname; /**< Empty when the pack has none. */
    std::string founded;  /**< Year, empty when unknown. */
    std::string balance_text;
    std::string wages_text;
    std::string stadium_text;
    std::vector<std::pair<std::string, float>> key_players;
  };

  /** @brief One entry of the league list, grouped by country. */
  struct LeagueEntry
  {
    LeagueID id = 0;
    LeagueID root = 0; /**< Top division of the country. */
    std::string name;
    uint8_t tier = 1;
    size_t lower_divisions = 0; /**< Set on the top division only. */
  };

  void loadAvailableLeagues();
  void loadAvailableTeams();
  void sortClubs();
  /** @brief Short windows: natural-height cards, only the page scrolls. */
  void renderStackedBrowser(float width);
  /** @brief Height 0 = natural height (no inner scrolling). */
  void renderLeagueList(float width, float height);
  /** @brief Height 0 = every row at natural height (no inner scrolling). */
  void renderClubTable(float height);
  /** @brief Club card beside the table; the start button sits at its top. */
  void renderSelectedClub(float width, float height);
  /** @brief One-row club card above the table for narrow windows. */
  void renderSelectedClubStrip();
  /** @brief Badge, name, nickname and reputation of a club in @p width. */
  static void renderClubIdentity(const ClubSummary& club, float badgeHeight,
                                 float width);
  [[nodiscard]] const ClubSummary* selectedClub() const;
  void selectLeague(const LeagueEntry& entry);
  void startCareer(TeamID teamId);
  /** @brief Starts the career without a club (the Job Centre opens). */
  void startUnemployed();
  /** @brief Leaves for the main menu (Back button, Escape). */
  void backToMenu();

  std::vector<LeagueEntry> league_entries;
  std::vector<std::reference_wrapper<const Team>> available_teams;
  std::optional<LeagueID> selected_league_id;
  std::optional<TeamID> selected_team_id;
  /** Countries (top division IDs) whose lower divisions are listed. */
  std::set<LeagueID> expanded_countries;
  bool laid_out = false; /**< False until the first frame has been drawn. */
  std::vector<ClubSummary> club_summaries;
  ImGuiID sort_column = 0;
  bool sort_ascending = false;
  ManagerSetupPanel manager_panel;
};
