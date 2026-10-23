// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once
#include <imgui.h>

#include "gui/scenes/management_scene.h"
#include "model/lineup.h"
#include "model/strategy.h"

/**
 * @brief The managed club's tactics: style and instructions, player roles
 * and duties, and the shape with the ball.
 *
 * Every change applies to the club's tactic in memory straight away (it is
 * written with the next save), so leaving the screen by any route never
 * loses an edit; Revert restores the tactic as it was on arrival.
 */
class StrategyScene : public ManagementScene
{
 public:
  /**
   * @brief Constructs a new StrategyScene.
   * @param parent Pointer to the GUIView.
   */
  explicit StrategyScene(GUIView* parent);

  /**
   * @brief Destroys the StrategyScene.
   */
  ~StrategyScene() override = default;

  /**
   * @brief Updates scene logic.
   * @param deltaTime Time elapsed since last update.
   */
  void update(float deltaTime) override;

  /**
   * @brief Gets the ID of this scene.
   * @return The SceneID (STRATEGY).
   */
  [[nodiscard]] SceneID getID() const override;

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::TACTICS;
  }
  void refresh() override { loadStrategy(); }

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  /** Selection on the roles card: the goalkeeper or an outfield slot. */
  static constexpr int KEEPER_SLOT = -1;

  void renderInstructions(float width);
  void renderSummary(float width) const;
  void renderRoles(float width);
  void renderShapeEditor(const Lineup& lineup, float width);
  void renderSlotList(const Lineup& lineup);
  void renderSlotDetails(const Lineup& lineup);
  void renderShapePresets(const Lineup& lineup, float width);

  /** The club's tactic and lineup (nullptr without a managed club). */
  [[nodiscard]] Strategy* clubStrategy() const;
  [[nodiscard]] const Lineup* clubLineup() const;
  /** Instruction of an outfield slot (a Standard one when none is stored). */
  [[nodiscard]] SlotInstruction slotAt(const Lineup& lineup, int slot) const;
  void storeSlot(const SlotInstruction& instruction) const;
  void setRole(const Lineup& lineup, TacticalRole role);
  void setDuty(const Lineup& lineup, RoleDuty duty);
  void applyShape(const Lineup& lineup, PossessionShape shape);
  void suggestRoles(const Lineup& lineup);
  void applySliders() const;

  /** Whether the tactic differs from the one on arrival. */
  [[nodiscard]] bool changedSinceEntry() const;
  void revert();
  void loadStrategy();

  StrategySliders current_sliders;
  /** The tactic as it was when the screen was opened (Revert). */
  Strategy entry_strategy;
  int selected_preset = -1;
  int selected_slot = 0;
  /** Outfield slot dragged on the shape editor (-1 none). */
  int dragging_slot = -1;
};
