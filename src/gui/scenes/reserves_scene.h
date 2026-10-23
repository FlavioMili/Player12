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

#include "controller/game_controller.h"
#include "gui/scenes/management_scene.h"
#include "gui/widgets/widgets.h"

/**
 * @brief Under-21 squad: the U21 players with their minutes and contracts
 * (Squad), the country's U21 league with the latest results and the
 * over-age places (League), and the first-team and U18 players who may
 * join the U21s (Candidates). Row actions live in the selected row's detail
 * strip, as on the youth academy screen.
 */
class ReservesScene : public ManagementScene
{
 public:
  explicit ReservesScene(GUIView* parent);

  void update(float deltaTime) override;
  [[nodiscard]] SceneID getID() const override { return SceneID::RESERVES; }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::RESERVES;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  enum class Tab : uint8_t
  {
    SQUAD,
    LEAGUE,
    CANDIDATES
  };

  enum class ListKind : uint8_t
  {
    SQUAD,
    CANDIDATES
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
    std::string origin; /*!< First team or U18 (candidates). */
    std::string terms;  /*!< Detail strip: contract and wage. */
    bool overage = false;
  };

  struct TableLine
  {
    int position = 0;
    std::string club;
    std::string record; /*!< "P  ·  W-D-L". */
    std::string goals;
    int points = 0;
    bool own = false;
  };

  struct ResultLine
  {
    std::string date;
    std::string opponent;
    std::string score;
    UI::Outcome outcome = UI::Outcome::DRAW;
  };

  struct PendingAction
  {
    enum class Kind : uint8_t
    {
      NONE,
      PROMOTE,
      TO_U18,
      TO_U21,
      PROFESSIONAL
    };
    Kind kind = Kind::NONE;
    PlayerID id = 0;
  };

  void renderSquad();
  void renderLeague();
  void renderCandidates();
  void renderQuotaCard(float width) const;
  void renderResultsCard(float width);
  void renderTable(const char* id, const std::vector<Row>& rows, ListKind kind);
  void renderDetail(const Row& row, ListKind kind);
  void runPending();
  Row makeRow(const GameController::YouthPlayerView& view) const;
  void openTab(Tab tab);

  GameController::ReserveOverview overview;
  std::vector<Row> squad_rows;
  std::vector<Row> candidate_rows;
  std::vector<TableLine> table;
  std::vector<ResultLine> results;
  std::vector<UI::Outcome> form;
  std::string subtitle;
  std::string squad_title;
  std::string league_title;
  std::string league_line;
  std::string quota_outfield;
  std::string quota_keepers;
  std::string squad_size;
  GameDateValue cached_date;
  PlayerID selected = 0;
  Tab requested_tab = Tab::SQUAD;
  bool tab_pending = false;
  PendingAction pending;
};
