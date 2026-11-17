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
#include <memory>
#include <string>

#include "gui/gui_scene.h"
#include "gui/render/match_render_snapshot.h"
#include "tools/match_sandbox_drill.h"
#include "tools/match_sandbox_training.h"

class IMatchRenderer;

/**
 * A training session on the pitch: its drills one after another, the whole
 * squad in each. The panel shows the drill, its controls and the results
 * table, filled in as players finish; Next drill moves on once it is over.
 * Space plays or pauses, R restarts the drill.
 */
class TrainingSessionScene : public GUIScene
{
 public:
  TrainingSessionScene(GUIView* guiView_ptr,
                       std::unique_ptr<TrainingSession> session,
                       std::string club);
  ~TrainingSessionScene() override;

  void onEnter() override;
  void update(float deltaTime) override;
  void render() override;
  SceneID getID() const override;

 private:
  void startDrill(std::size_t index);
  void stepRun(int steps);
  void renderPanel(float width);
  void renderResults(TrainingDrill& drill);
  void renderPitch(float width, float height);

  std::unique_ptr<TrainingSession> session;
  std::string club;
  std::size_t current = 0;
  std::unique_ptr<DrillRun> run;
  bool paused = false;
  float speed = 1.0F;
  float accumulator = 0.0F;
  /** Run the rest of the drill as fast as it goes. */
  bool skipping = false;
  bool showResults = false;
  std::array<int, 2> resultMetrics{0, 1};
  ImFont* regularFont = nullptr;
  ImFont* boldFont = nullptr;

  std::unique_ptr<IMatchRenderer> renderer;
  MatchRenderSnapshot snapshot;
};
