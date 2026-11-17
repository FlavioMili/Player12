// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <span>

#include "gui/gui_scene.h"
#include "model/match_contact_binturong.h"

/**
 * Contact view: how hard a touch or strike is (the Binturong body's
 * assessContact, contact plan §2) across his reach. The disc is his reach
 * circle, seen from above, coloured by where in it he meets the ball; the
 * inner ring by where the ball comes from; the outer ring by where he sends
 * it. The other conditions are set on the panel and shown as bars. Every
 * colour is read from the engine's own function with the default knobs.
 */
class ContactViewScene : public GUIScene
{
 public:
  explicit ContactViewScene(GUIView* guiView_ptr);

  void onEnter() override;
  void update(float deltaTime) override;
  void render() override;
  SceneID getID() const override;

 private:
  /** What is set on the panel. Angles are compass degrees on the screen
   * (0 up, clockwise). */
  struct Inputs
  {
    /** 0: first touch, 1 + StrikeKind: a strike. */
    int contact = 0;
    float facing = 0.0F;
    bool rightFooted = true;
    float ownSpeed = 0.0F;
    float ownDirection = 0.0F;
    float topSpeed = 8.5F;
    /** Where the ball comes from, and how fast (m/s). */
    float incomingFrom = 0.0F;
    float incomingPace = 12.0F;
    /** The ball's speed up (+) or down (-) as it reaches him (m/s). */
    float verticalSpeed = 0.0F;
    float height = 0.0F;
    float aim = 0.0F;
    float intendedSpeed = 3.0F;
    /** The speed up (+) or down he means it to leave with (m/s). */
    float lift = 0.0F;
    /** 0 a pass to him, 1 a loose ball, 2 a pass meant for someone else
     * (a strike is always his own ball). */
    int expectation = 0;
    float opponentMetres = 6.0F;
    /** Where he meets it, metres from his centre (x right, y up). */
    ImVec2 probe{0.0F, 0.30F};
  };

  [[nodiscard]] bool isTouch() const { return inputs.contact == 0; }
  /** The chosen contact's profile with the default knobs. */
  [[nodiscard]] ContactProfile chosenProfile() const;
  [[nodiscard]] float reach() const;
  /** Whether a first touch cannot meet the ball at `point` (metres): his
   * dead zone behind him (strikes have none). */
  [[nodiscard]] bool inDeadZone(ImVec2 point) const;
  /** How hard it is met at `point` (metres), coming from `from` and sent
   * to `aim` (compass degrees). */
  [[nodiscard]] ContactAssessment assess(ImVec2 point, float from,
                                         float aim) const;
  [[nodiscard]] ContactAssessment assessProbe() const;
  /** The colour of one assessment under the chosen view; in the zones view
   * only `zoneFactors` colour it. */
  [[nodiscard]] ImU32 colourOf(const ContactAssessment& contact,
                               std::span<const int> zoneFactors) const;

  void renderPanel(float width);
  void renderCircle(ImVec2 size);
  void renderBreakdown(float width);
  /** The ball seen from the side: the steepness it arrives at and the one
   * he means. */
  void renderSideView(const ContactAssessment& contact);
  /** The stretch map, editable; it goes into every contact shown here. */
  void renderStretchMap();
  /** The chosen contact's knobs (the sandbox's tuning), editable. */
  void renderKnobs();
  /** The tuning as a whole: what changed, copy, reset, the bench. */
  void renderTuning();
  void openBench();

  Inputs inputs;
  /** The chosen contact's profile, for this frame. */
  ContactProfile profile;
  /** 0 zones, 1 angle job, 2 weight job, 3 + factor index. */
  int view = 0;
  /** What a drag on the circle moves: 0 none, 1 where he meets it, 2 where
   * it comes from, 3 where he sends it, 4 his facing. */
  int dragging = 0;
  int panelTab = 0;
  int inputTab = 0;
  int tuningTab = 0;
  ImFont* regularFont = nullptr;
  ImFont* boldFont = nullptr;
};
