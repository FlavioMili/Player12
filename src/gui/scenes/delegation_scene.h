// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <string>

#include "gui/scenes/management_scene.h"
#include "model/delegation.h"

/**
 * @brief One place to decide who handles which recurring duty: the manager
 * or the assistant, with three presets (hands-on, balanced, assistant runs
 * the daily operations).
 */
class DelegationScene : public ManagementScene
{
 public:
  explicit DelegationScene(GUIView* parent);

  void update(float /*deltaTime*/) override
  {
    // Rendering reads current state; there is no per-frame update work.
  }
  [[nodiscard]] SceneID getID() const override { return SceneID::DELEGATION; }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::DELEGATION;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  void renderDuty(Duty duty, float width);

  DelegationPolicy policy;
  std::string delegate_line;
};
