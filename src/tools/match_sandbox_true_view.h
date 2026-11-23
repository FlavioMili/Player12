// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <string>
#include <vector>

#include "gui/gui_scene.h"
#include "tools/match_sandbox_true_view_shapes.h"

/** A pose's moment: its caption, and how far up he is (-1: not getting
 * up). */
struct TrueViewMoment
{
  std::string caption;
  float getUpShare = -1.0F;
};

/** How far his running and standing jumps rise (metres). */
struct JumpRises
{
  float running = 0.0F;
  float standing = 0.0F;
};

/**
 * True view (Notes and plans/match_engine_true_view_plan.md): one player in
 * 3D as the Binturong body models him, with no human figure on top. The
 * body cylinder, the lines between feet, thigh, chest and head, each
 * part's reach with its comfort gap, dead zone and stretch (rings of 12
 * sections), each technique's own range, jumping, falling and getting up,
 * and the personal space the brain keeps. Every shape is read from the
 * engine's functions and the sandbox's tuning (the contact view's knobs).
 */
class TrueViewScene : public GUIScene
{
 public:
  explicit TrueViewScene(GUIView* guiView_ptr);

  void onEnter() override;
  void update(float deltaTime) override;
  void render() override;
  SceneID getID() const override;

  /** What he is doing. */
  enum class Pose : int
  {
    STANDING,
    STANDING_JUMP,
    RUNNING_JUMP,
    FALLEN,
    GETTING_UP,
    STUMBLING,
    PLANTED,
  };

 private:
  /** The player, on the stat scale (1-99) as the panel sets him. */
  struct Stats
  {
    int heightCentimetres = 180;
    /** The jump on top of his height (Jumping reach's blend). */
    int jump = 50;
    int strength = 50;
    int agility = 50;
    int dribbling = 50;
    int defending = 50;
    /** Only the old shot-block rule reads it. */
    int physicality = 50;
    bool rightFooted = true;
    /** Compass degrees, 0 up the screen's far side. */
    float facing = 0.0F;
  };

  struct Camera
  {
    float yaw = -2.3F;
    float pitch = 0.45F;
    float distance = 5.5F;
    float targetHeight = 0.8F;
  };

  void renderPanel(float width);
  void renderViewButtons(float width, float scale);
  void renderDetailTabs(float width, float scale);
  void renderPlayerControls(float width, float scale);
  void renderPoseControls(float width, float scale);
  void renderTechniqueControls(float width, float scale);
  void renderLayerToggles(float width, float scale);
  void renderCanvas(ImVec2 size);
  void orbitCamera();
  [[nodiscard]] float cycleOf(float total) const;
  TrueViewMoment jumpMoment(TrueView::Body& body, float rise) const;
  float rolledMetres();
  TrueViewMoment fallenMoment(TrueView::Body& body);
  TrueViewMoment gettingUpMoment(TrueView::Body& body);
  TrueViewMoment poseMoment(TrueView::Body& body, const JumpRises& rises);
  [[nodiscard]] TrueView::Body statsBody() const;
  void renderTable(float width);

  Stats stats;
  TrueView::Layers layers;
  Camera camera;
  Pose pose = Pose::STANDING;
  TrueView::Technique technique = TrueView::Technique::ALL;
  /** The strike whose power by angle is shown (StrikeKind). */
  int strikeKind = 0;
  /** Faster than his thigh allows (it is then off). */
  bool moving = false;
  /** Fallen: how he lies against his facing (degrees), and whether he is
   * rolling about. */
  float lyingOffset = 0.0F;
  bool fellOnFront = true;
  bool rolling = false;
  /** The animation's clock (s) and whether it runs. */
  float clock = 0.0F;
  bool playing = true;
  float playbackSpeed = 0.5F;
  /** This frame's shapes, for the table. */
  std::vector<TrueView::Entry> entries;
  enum class ControlTab
  {
    PLAYER,
    POSE,
    TECHNIQUE,
    LAYERS
  };
  ControlTab controlTab = ControlTab::PLAYER;
  bool showValues = false;
  ImFont* regularFont = nullptr;
  ImFont* boldFont = nullptr;
};
