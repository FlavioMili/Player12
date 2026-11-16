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
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gui/scenes/management_scene.h"
#include "gui/scenes/mentoring_dialog.h"
#include "model/board.h"
#include "model/facility_projects.h"
#include "model/preseason.h"

/**
 * @brief Club planning: the pre-season (friendlies, tour and training camp),
 * facility projects funded by the board and the mentoring groups.
 */
class PreseasonScene : public ManagementScene
{
 public:
  explicit PreseasonScene(GUIView* parent);

  void update(float /*deltaTime*/) override
  {
    // Pre-season planning is driven by explicit user actions and the calendar.
  }
  [[nodiscard]] SceneID getID() const override { return SceneID::PLANNING; }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::PLANNING;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  /** @brief One formatted friendly. */
  struct FriendlyLine
  {
    FriendlySlot slot;
    std::string date;
    std::string opponent;
    std::string suggestion; /**< The assistant's pick for this date. */
  };

  /** @brief One project type with its quote and state. */
  struct ProjectLine
  {
    FacilityProjectType type = FacilityProjectType::TrainingGround;
    std::string name;
    std::string level;
    std::string quote;
    std::optional<FacilityProject> running;
    std::string status;
    bool can_request = false;
  };

  void renderPreseason(float width);
  void renderFriendlyEditor(const FriendlyLine& line);
  void renderCamp();
  void renderProjects(float width);
  void renderMentoring(float width);
  void loadOpponents();

  std::vector<FriendlyLine> friendlies;
  int selected_friendly = -1;
  int level = static_cast<int>(std::to_underlying(OpponentLevel::Similar));
  int region = 0; /**< 0 domestic, 1 abroad. */
  int venue = 0;  /**< 0 home, 1 away. */
  bool tour = false;
  std::vector<OpponentOption> opponents;
  std::vector<std::string> opponent_labels;
  int opponent_index = -1;
  int tour_fee_for = -1; /**< Opponent index tour_fee_text belongs to. */
  std::string tour_fee_text;
  int camp_choice = 0;
  std::array<CampQuote, 3> camp_quotes;
  bool camp_open = false;
  std::string camp_status;
  std::string camp_suggestion;

  std::vector<ProjectLine> projects;
  std::vector<std::string> completed_projects;
  int seats_choice = 1;
  std::array<std::string, 3> seat_labels;
  std::string project_message;
  bool project_message_error = false;

  std::vector<std::string> mentoring_lines;
  MentoringDialog mentoring_dialog;
};
