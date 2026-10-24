// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "gui/scenes/management_scene.h"
#include "model/squad_planner.h"
#include "model/squad_status.h"

/**
 * @brief Squad planner: depth chart per position group (first choice,
 * backup, prospect) with the manager's squad statuses, recruitment needs,
 * age profile and contract calendar, for this season or projected to the
 * next one (expiring contracts gone, everyone a year older).
 */
class SquadPlannerScene : public ManagementScene
{
 public:
  explicit SquadPlannerScene(GUIView* parent);

  void update(float deltaTime) override;
  [[nodiscard]] SceneID getID() const override
  {
    return SceneID::SQUAD_PLANNER;
  }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::SQUAD_PLANNER;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  /** @brief One player in the depth chart, pre-formatted. */
  struct Row
  {
    PlayerID id = 0;
    std::string name;
    std::string role;
    std::string age;
    std::string overall;
    std::string contract;
    std::string status; /*!< Given status, or the expected one in muted. */
    DepthTier tier = DepthTier::Backup;
    float overall_value = 0.0f;
    int age_value = 0;
    std::optional<SquadStatus> given;
    SquadStatus deserved = SquadStatus::Regular;
    bool expiring = false;
    bool ageing = false;
    bool injured = false;
  };

  /** @brief One position group of the depth chart. */
  struct Group
  {
    PlannerGroup group = PlannerGroup::Goalkeeper;
    std::string header; /*!< "GOALKEEPERS · 3 / 3". */
    int missing = 0;
    std::vector<Row> rows;
  };

  /** @brief A suggested need, pre-formatted. */
  struct Need
  {
    NeedKind kind = NeedKind::Missing;
    PlannerGroup group = PlannerGroup::Goalkeeper;
    std::string text;
    float min_ability = 0.0f; /*!< For a recruitment focus. */
  };

  /** @brief Everything shown for one season. */
  struct View
  {
    std::array<Group, PLANNER_GROUP_COUNT> groups;
    std::vector<Need> needs;
    std::array<int, AGE_BAND_COUNT> age_bands{};
    std::array<std::string, AGE_BAND_COUNT> age_counts;
    std::vector<std::pair<std::string, std::string>>
        expiries;           /*!< Year, names. */
    std::string departures; /*!< Names of the players who leave. */
    std::string squad_size;
    std::string average_age;
    int expiring = 0;
  };

  void build(View& view, int season_offset) const;
  void renderDepth(float width);
  void renderGroup(const Group& group);
  void renderDetail(const Row& row);
  void renderNeeds(float width);
  void renderProfile(float width);
  void addFocus(const Need& need);

  std::array<View, 2> views; /*!< This season, next season. */
  int season = 0;
  PlayerID selected = 0;
  bool stale = false; /*!< A status changed: rebuild after this frame. */
  std::uint16_t season_year = 0; /*!< Calendar year the season ends in. */
};
