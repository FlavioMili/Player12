// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <memory>

#include "gui/gui_scene.h"
#include "gui/render/imatch_renderer.h"
#include "gui/render/match_render_snapshot.h"
#include "gui/scenes/match_play_controller.h"
#include "tools/match_sandbox_debugger.h"
#include "tools/match_sandbox_drill.h"
#include "tools/match_sandbox_inspector.h"
#include "tools/match_sandbox_review.h"

class IMatchRenderer;
struct StatsConfig;

/**
 * Watch mode of a drill: its settings and results beside the pitch, played
 * back step by step (each step runs the drill's script), restarted at will;
 * the review, debugger and inspector windows work on the run as on a match.
 * Space plays or pauses, R restarts. When you run the drill, the keyboard
 * or a gamepad steers the runner through the Play controls (Esc pauses).
 */
class DrillScene : public GUIScene
{
 public:
  /** `prepare`: called on each run's engine once the drill has set it up
   * (the technique bench sets its tuning there). */
  DrillScene(GUIView* guiView_ptr, Drill& drill,
             std::function<void(MatchEngine&)> prepare = {});
  ~DrillScene() override;

  void onEnter() override;
  void onExit() override;
  void handleEvent(const SDL_Event& event) override;
  void update(float deltaTime) override;
  void render() override;
  SceneID getID() const override;

 private:
  void restart();
  void stepRun(int steps);
  void renderPanel(float width);
  void renderControls(float width);
  void renderViewControls(float scale);
  void renderPitch(ImVec2 area);
  void renderDebugTools();
  /** Keyboard navigation off while you steer (arrows, Space would move it). */
  void suspendKeyboardNavigation(bool suspend);
  /** Screen directions of the pitch at the runner, for the stick. */
  void updateBasis();

  Drill& drill;
  std::function<void(MatchEngine&)> prepareRun;
  std::unique_ptr<DrillRun> run;
  bool paused = false;
  float speed = 1.0F;
  float accumulator = 0.0F;
  bool newSeedEachRun = false;
  int analysisTab = 0;
  /** The pitch drawn in 3D (broadcast camera) instead of 2D. */
  bool view3d = false;
  MatchCameraMode cameraMode = MatchCameraMode::BROADCAST;

  std::unique_ptr<IMatchRenderer> renderer;
  MatchRenderSnapshot snapshot;
  EngineDebugger debugger;
  MatchReview review;
  DebugInspector inspector;

  MatchPlayController play;
  PlayScreenBasis basis;
  bool navSuspended = false;
  ImFont* regularFont = nullptr;
  ImFont* boldFont = nullptr;
};
