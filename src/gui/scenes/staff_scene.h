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
#include <vector>

#include "gui/scenes/management_scene.h"
#include "model/staff.h"

/**
 * @brief Backroom staff: current coaches, medics and scouts with ratings and
 * responsibilities, their impact on the squad, contract actions and a
 * filterable staff market.
 */
class StaffScene : public ManagementScene
{
 public:
  explicit StaffScene(GUIView* parent);

  void update(float deltaTime) override;
  [[nodiscard]] SceneID getID() const override { return SceneID::STAFF; }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::STAFF;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  /** @brief One staff line, pre-formatted. */
  struct StaffRow
  {
    StaffID id = 0;
    StaffRole role = StaffRole::AssistantManager;
    std::string name;
    std::string attributes; /*!< Key attributes, e.g. "Fitness 71 · ...". */
    int age = 0;
    int rating = 0;
    std::string wage;
    std::string demand; /*!< Market or renewal wage. */
    int contract_years = 0;
    std::string contract;    /*!< Remaining seasons, formatted. */
    std::string extend_help; /*!< Terms of a one-season extension. */
    std::string hire_label;  /*!< Market rows: "Hire for <demand> / week". */
    bool role_full = false;  /*!< Market rows: the club's quota is used. */
  };

  /** @brief Who covers an area and how well. */
  struct Responsibility
  {
    const char* area_key = "";
    float quality = 0.0f;
    std::string holder;
  };

  void renderStaff(float width);
  void renderImpact(float width) const;
  void renderMarket();
  void renderFilters();
  /** Rows grouped by department, each group with a header and a table. */
  void renderGroups(const char* id, const std::vector<StaffRow>& rows,
                    bool market);
  void renderDetail(const StaffRow& row, bool market);
  void renderReleaseConfirm();
  void rebuildMarket();
  void showResult(int result, const char* success_key);

  /** @brief A button press, executed once the tables are drawn. */
  struct PendingAction
  {
    enum class Kind : uint8_t
    {
      NONE,
      HIRE,
      EXTEND,
      RELEASE
    };
    Kind kind = Kind::NONE;
    StaffID id = 0;
    uint8_t years = 0;
  };

  std::vector<StaffRow> staff_rows;
  std::vector<StaffRow> market_rows;
  std::vector<Responsibility> responsibilities;
  StaffEffects effects;
  int role_filter = -1; /*!< -1: every role. */
  int min_rating = 0;
  int hire_years = 2;
  StaffID selected = 0; /*!< Row whose detail strip is open (0: none). */
  StaffID release_candidate = 0;
  std::string release_text;
  bool release_requested = false;
  PendingAction pending;
};
