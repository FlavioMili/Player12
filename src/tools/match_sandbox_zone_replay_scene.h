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
#include "tools/match_sandbox_debugger.h"
#include "tools/match_sandbox_inspector.h"
#include "tools/match_sandbox_review.h"
#include "tools/match_sandbox_zone_recording.h"

struct ImFont;

/**
 * A saved training-session zone played again (training ground plan section
 * 23, T7f): the zone alone on its own pitch, rebuilt from the recording on
 * the current engine, with the rewind and the debugger as in a drill, and
 * where the current engine first plays it differently.
 */
class ZoneReplayScene : public GUIScene
{
 public:
  ZoneReplayScene(GUIView* guiView_ptr, ZoneRecording recording);
  ~ZoneReplayScene() override;

  void onEnter() override;
  void update(float deltaTime) override;
  void render() override;
  SceneID getID() const override;

 private:
  void renderPanel(float width);

  ZoneRecording recording;
  std::unique_ptr<ZoneRecordings::Replay> replay;
  MatchReview review;
  EngineDebugger debugger;
  DebugInspector inspector;
  bool paused = true;
  ImFont* regularFont = nullptr;
  ImFont* boldFont = nullptr;
};
