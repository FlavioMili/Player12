// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <memory>

#include "gui/gui_scene.h"
#include "gui/render/match_render_snapshot.h"
#include "gui/render/imatch_renderer.h"
#include "tools/match_sandbox_session.h"
#include "tools/match_sandbox_training.h"

class IMatchRenderer;
struct ImFont;

/**
 * A training session on the ground (training ground plan section 23, T7b):
 * every zone of the current exercise drawn in its place, all playing at
 * once, in 2D or 3D. The panel shows the exercise, the playback controls
 * and each zone's report. Space plays or pauses, R restarts the session.
 */
class ZoneSessionScene : public GUIScene
{
 public:
  ZoneSessionScene(GUIView* guiView_ptr, const SessionPlan& plan,
                   std::vector<TrainingPlayer> picks);
  ~ZoneSessionScene() override;

  void onEnter() override;
  void update(float deltaTime) override;
  void render() override;
  SceneID getID() const override;

 private:
  void restart();
  void renderPanel(float width);
  void renderGround(float width, float height);

  SessionPlan plan;
  /** The players picked to train (the first make up the squad). */
  std::vector<TrainingPlayer> picks;
  /** The zone the 3D cameras follow (its ball is the one they track). */
  std::size_t watchedZone = 0;
  std::unique_ptr<SessionRun> run;
  bool paused = false;
  float speed = 1.0f;
  float accumulator = 0.0f;
  bool view3d = false;
  /** The squad player picked for the changing room. */
  std::size_t restPick = 0;
  MatchCameraMode cameraMode = MatchCameraMode::BROADCAST;
  ImFont* regularFont = nullptr;
  ImFont* boldFont = nullptr;
  std::unique_ptr<IMatchRenderer> renderer;
  MatchRenderSnapshot snapshot;
};
