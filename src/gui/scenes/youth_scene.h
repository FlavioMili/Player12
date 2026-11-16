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
#include <span>
#include <string>
#include <vector>

#include "controller/game_controller.h"
#include "gui/scenes/management_scene.h"
#include "gui/widgets/widgets.h"

/**
 * @brief Youth academy: academy ratings, the head of youth development,
 * board requests and the U18 league (Overview), the U18 squad with its
 * contracts, the intake trialists with their offers (Intake) and the
 * youngsters' progress over time (Development).
 */
class YouthScene : public ManagementScene
{
 public:
  explicit YouthScene(GUIView* parent);

  void update(float deltaTime) override;
  [[nodiscard]] SceneID getID() const override { return SceneID::YOUTH; }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::YOUTH;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  enum class Tab : uint8_t
  {
    OVERVIEW,
    SQUAD,
    INTAKE,
    DEVELOPMENT
  };

  /** @brief Which list a table shows (decides columns and actions). */
  enum class ListKind : uint8_t
  {
    SQUAD,
    ELIGIBLE,
    INTAKE,
    DEVELOPMENT
  };

  /** @brief One player line, pre-formatted at refresh time. */
  struct Row
  {
    GameController::YouthPlayerView view;
    std::string role;
    std::string ability;
    std::string potential;
    std::string contract;
    std::string rating;
    std::string nationality;
    std::string height;
    std::string wage;
    std::string offer_label;
    std::string growth_text;
    std::string summary; /*!< Detail strip: personality, nation, height. */
    std::string terms;   /*!< Detail strip: contract and wage. */
    std::string chart_caption;
    std::string chart_from;
    std::string chart_to;
    float growth = 0.0f;
  };

  struct ResultLine
  {
    std::string date;
    std::string opponent;
    std::string score;
    UI::Outcome outcome = UI::Outcome::DRAW;
  };

  struct UpgradeLine
  {
    UpgradeQuote quote;
    std::string level;
    std::string cost;
    std::string time;
    std::string target;
    std::string ready; /*!< Completion date of a running project. */
    float progress = 0.0f;
  };

  /** @brief A button press, executed once the page is drawn. */
  struct PendingAction
  {
    enum class Kind : uint8_t
    {
      NONE,
      SIGN,
      LET_GO,
      PROFESSIONAL,
      PROMOTE,
      DEMOTE,
      LOAN,
      RELEASE,
      UPGRADE,
      TO_U21
    };
    Kind kind = Kind::NONE;
    PlayerID id = 0;
    AcademyUpgrade upgrade = AcademyUpgrade::None;
  };

  void renderOverview();
  void renderSquad();
  void renderIntake();
  void renderDevelopment();
  void renderIntakeCard(float width);
  void renderHeadCard(float width);
  void renderBoardCard(float width);
  void renderLeagueCard(float width);
  void renderTable(const char* id, const std::vector<Row>& rows, ListKind kind);
  void renderDetail(const Row& row, ListKind kind);
  void renderChart(const Row& row, float width) const;
  void renderConfirm();
  void runPending();
  Row makeRow(const GameController::YouthPlayerView& view) const;
  void openTab(Tab tab);

  GameController::AcademyOverview overview;
  std::vector<Row> squad_rows;
  std::vector<Row> eligible_rows;
  std::vector<Row> intake_rows;
  std::vector<Row> development_rows;
  std::array<UpgradeLine, 2> upgrades;
  std::vector<ResultLine> results;
  std::vector<UI::Outcome> form;
  std::vector<UI::BarDatum> improvers;
  std::vector<std::string> improver_values;
  std::string head_rating;
  std::string intake_line;
  std::string deadline_line;
  std::string preview_line;
  std::string league_line;
  std::string league_record;
  std::string league_goals;
  std::string intake_tab_label;
  std::string squad_title;
  GameDateValue cached_date;

  Tab requested_tab = Tab::OVERVIEW;
  bool tab_pending = false;
  bool first_refresh = true;
  PlayerID selected = 0;
  PlayerID chart_player = 0;

  PlayerID confirm_player = 0;
  bool confirm_release = false; /*!< Release a signed player (else let a
                                     trialist go). */
  bool confirm_requested = false;
  std::string confirm_text;
  PendingAction pending;
};
