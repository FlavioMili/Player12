// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "gui/gui_scene.h"
#include "model/match_brain_flow.h"

struct ImFont;

/**
 * The brain flow visualiser (Notes and plans/
 * match_engine_brain_flow_tool_plan.md): the new brain framework as a flow,
 * left to right (situation, reading, goal, options, measures, weights,
 * judgement, pick, body), on a live example scored by Capybara's own code.
 * The default flow has no influence layers; each layer can be switched on
 * to see which parts of the flow it touches, and by how much.
 */
class BrainFlowScene : public GUIScene
{
 public:
  explicit BrainFlowScene(GUIView* guiView_ptr);
  ~BrainFlowScene() override;

  void onEnter() override;
  void update(float deltaTime) override;
  void render() override;
  SceneID getID() const override;

 private:
  /** Scores the example twice: with no layers, and with the layers on. */
  void rebuild();
  void renderPanel(float width);
  void renderFlow(float width, float height);
  void optionsColumn();
  void measuresColumn();
  void weightsColumn();
  void judgementColumn();
  void pickColumn();
  void bodyColumn();
  [[nodiscard]] bool layerOn(BrainFlow::Layer layer) const;
  [[nodiscard]] std::string nameOf(std::uint32_t player) const;

  // Settings.
  BrainFlow::LayerMask layers = 0;
  /** True: scores at the true chances; false: one sample drawn. */
  bool steady = true;
  std::uint32_t rollSeed = 1;

  // The example, scored when a setting changes.
  bool dirty = true;
  std::string error;
  BrainFlow::CarrierRecord base;
  BrainFlow::CarrierRecord layered;
  std::map<std::uint32_t, std::string> names;
  std::string carrierName;
  PitchPoint carrierAt{};

  ImFont* regularFont = nullptr;
  ImFont* boldFont = nullptr;
};
