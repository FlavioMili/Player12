// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "gui/scenes/management_scene.h"
#include "model/medical_centre.h"

/**
 * @brief Medical centre: injured players with their expected return and
 * recurrence risk, the injury risk of the available squad from workload
 * and freshness, and what the physios and doctors contribute.
 */
class MedicalScene : public ManagementScene
{
 public:
  explicit MedicalScene(GUIView* parent);

  void update(float deltaTime) override;
  [[nodiscard]] SceneID getID() const override { return SceneID::MEDICAL; }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::MEDICAL;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  /** @brief One injured player, pre-formatted. */
  struct InjuryLine
  {
    PlayerID id = 0;
    std::string name;
    std::string role;
    std::string injury;
    std::string days;
    std::string back; /*!< Expected return date range. */
    RiskBand reinjury = RiskBand::Low;
    InjurySeverity severity = InjurySeverity::Minor;
  };

  /** @brief One available player's fitness and risk, pre-formatted. */
  struct RiskLine
  {
    PlayerID id = 0;
    std::string name;
    std::string role;
    RiskBand band = RiskBand::Low;
    float condition = 0.0f;
    float sharpness = 0.0f;
    std::string load;    /*!< "1.34 · rising". */
    int load_trend = 0;  /*!< -1 falling, 0 stable, 1 rising. */
    std::string reasons; /*!< Why the risk is raised. */
    bool returning = false;
    std::byte flags{}; /*!< MedicalFlag bits. */
  };

  /** @brief A physio or doctor. */
  struct StaffLine
  {
    std::string name;
    std::string role;
    int rating = 0;
  };

  void renderInjured(float width);
  void renderRisk(float width);
  void renderStaff(float width);
  /** Load chart and medical instructions of the selected player. */
  void renderPlayerLoad(float width);
  void renderLoadChart(float height);
  /** Selects @p id for the load card and reads his chart and flags. */
  void select(PlayerID id);
  /** Name cell of both tables: selects the player for the load card. */
  void nameCell(PlayerID id, const std::string& name, const std::string& role,
                bool returning);
  void setFlag(std::byte flag, bool enabled);

  std::vector<InjuryLine> injured;
  std::vector<RiskLine> risks;
  std::vector<StaffLine> staff;
  std::string recovery_effect;   /*!< e.g. "-8% layoff length". */
  std::string prevention_effect; /*!< e.g. "-5% injury risk". */
  bool recovery_good = false;
  bool prevention_good = false;
  float average_condition = 0.0f;
  float average_sharpness = 0.0f;
  int high_risk = 0;
  int days_lost = 0;
  bool show_all = false; /*!< Risk table: everyone or only raised risk. */

  /** Player shown in the load card (0: none) and his cached data. */
  PlayerID selected = 0;
  std::string selected_name;
  std::byte selected_flags{};
  std::int32_t chart_today = 0;
  std::array<LoadChartDay, MedicalCentre::LOAD_CHART_DAYS> chart{};
  bool chart_empty = true;
};
