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

/**
 * @brief National-team call-ups of the head coach: the announced squad of
 * the coming window (editable until the players report), the players the
 * nation may call up with ability, form, condition and caps in the
 * assistant's order, and the nation's next matches.
 */
class CallUpScene : public ManagementScene
{
 public:
  explicit CallUpScene(GUIView* parent);

  void update(float deltaTime) override;
  [[nodiscard]] SceneID getID() const override { return SceneID::CALL_UPS; }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::CALL_UPS;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  /** @brief One player line, pre-formatted at refresh time. */
  struct Row
  {
    GameController::CallUpCandidate candidate;
    std::string role;
    std::string club;
    std::string form;
    std::string caps; /*!< "caps (goals)". */
    int group = 0;    /*!< 0 GK, 1 DEF, 2 MID, 3 FWD. */
  };

  struct FixtureLine
  {
    std::string date;
    std::string opponent;
    std::string competition;
  };

  void renderTiles() const;
  void renderSquad(float width);
  void renderCandidates(float width);
  void renderFixtures(float width) const;
  void renderTable(const char* id, const std::vector<const Row*>& rows,
                   bool squad);
  void renderDetail(const Row& row, bool squad);
  void rebuildLists();
  void confirm();

  GameController::CallUpView view;
  std::vector<Row> rows;
  /** The squad being edited (player ids), saved by confirm(). */
  std::vector<PlayerID> working;
  std::vector<const Row*> squad_rows;
  std::vector<const Row*> candidate_rows;
  std::vector<FixtureLine> fixtures;
  std::string subtitle;
  std::string status_value;
  std::string status_note;
  std::string squad_value;
  std::string keepers_value;
  std::string squad_title;
  GameDateValue cached_date;
  PlayerID selected = 0;
  int filter = 0; /*!< 0 all, then position groups. */
  size_t keepers = 0;
  bool dirty = false;
  bool lists_stale = false;
  bool confirm_requested = false;
};
