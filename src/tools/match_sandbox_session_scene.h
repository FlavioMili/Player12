// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <memory>
#include <string>

#include "gui/gui_scene.h"
#include "gui/render/imatch_renderer.h"
#include "gui/render/match_render_snapshot.h"
#include "tools/match_sandbox_debugger.h"
#include "tools/match_sandbox_inspector.h"
#include "tools/match_sandbox_session.h"
#include "tools/match_sandbox_training.h"

class IMatchRenderer;
class MatchAudio;
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
  ZoneSessionScene(GUIView* guiView_ptr, SessionPlan plan,
                   std::vector<TrainingPlayer> picks);
  ~ZoneSessionScene() override;

  void onEnter() override;
  void update(float deltaTime) override;
  void render() override;
  SceneID getID() const override;

 private:
  void restart();
  void stepSession();
  void renderPanel(float width);
  void renderGround(ImVec2 area);

  SessionPlan plan;
  /** The players picked to train (the first make up the squad). */
  std::vector<TrainingPlayer> picks;
  /** The zone picked (T7e): the 3D cameras follow its ball, and the
   * debugger and its set-up are its. */
  std::size_t watchedZone = 0;
  /** The picked zone's debugger and inspector (they follow the pick). */
  EngineDebugger debugger;
  DebugInspector inspector;
  std::size_t debuggedZone = static_cast<std::size_t>(-1);
  const DrillRun* debuggedRun = nullptr;
  void renderZoneTools(float width);
  void renderViewControls(float scale);
  void renderPlayback(float width, float scale);
  void renderChangingRoom(float width, float scale);
  void renderRestPicker(const std::vector<std::size_t>& playing, float scale);
  void renderZones(float width, float scale);
  void renderSessionPlan(float width, float scale);
  void saveWatchedZone();
  void renderZoneVersions(float scale);
  void renderZoneDebugger(float width, float scale);
  /** What the last "Save this zone" did. */
  std::string saveMessage;
  std::unique_ptr<SessionRun> run;
  std::unique_ptr<MatchAudio> coachAudio;
  /** Why the session couldn't start (its plan's problem), if it couldn't. */
  std::string runError;
  bool paused = false;
  float speed = 1.0F;
  float accumulator = 0.0F;
  bool view3d = false;
  /** The squad player picked for the changing room. */
  std::size_t restPick = 0;
  MatchCameraMode cameraMode = MatchCameraMode::BROADCAST;
  ImFont* regularFont = nullptr;
  ImFont* boldFont = nullptr;
  std::unique_ptr<IMatchRenderer> renderer;
  MatchRenderSnapshot snapshot;
};
