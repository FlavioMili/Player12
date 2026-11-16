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
#include "model/training.h"

/**
 * @brief Training ground: the weekly microcycle, preset and intensity, the
 * coming seven days, the assistant's advice and every player's focus,
 * workload and development trend.
 */
class TrainingScene : public ManagementScene
{
 public:
  explicit TrainingScene(GUIView* parent);

  void update(float deltaTime) override;
  [[nodiscard]] SceneID getID() const override { return SceneID::TRAINING; }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::TRAINING;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  /** @brief One player line of the individual training table. */
  struct PlayerRow
  {
    PlayerID id = 0;
    std::string name;
    std::string role;
    int age = 0;
    float condition = 100.0f;
    float ratio = 1.0f;
    WorkloadRisk risk = WorkloadRisk::Low;
    float trend = 0.0f;
    TrainingFocus focus = TrainingFocus::None;
    bool injured = false;
    bool veteran = false; /*!< Old enough to decline at the season end. */
    std::string condition_text;
    std::string ratio_text;
  };

  /** @brief One day of the coming week, pre-formatted. */
  struct DayCell
  {
    std::string date;
    std::string title;
    std::string detail;
    bool match_day = false;
    bool adjusted = false;
    float load = 0.0f;
  };

  /** @brief One assistant recommendation, pre-formatted. */
  struct AdviceLine
  {
    AdviceSeverity severity = AdviceSeverity::Info;
    std::string text;
  };

  void renderControls();
  void renderMicrocycle(float width);
  void renderSlot(int day, bool compact);
  void renderWeek() const;
  void renderAdvice(float width) const;
  void renderPlayers();
  void sortRows();

  TeamTrainingPlan plan;
  bool has_plan = false;
  std::vector<PlayerRow> rows;
  std::vector<DayCell> week;
  std::vector<AdviceLine> advice;
  float average_condition = 0.0f;
  float familiarity = 0.0f;
  float effectiveness = 0.0f;
  int at_risk = 0;
  int sort_column = 0;
  bool sort_ascending = true;
};
