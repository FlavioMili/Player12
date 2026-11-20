// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "gui/gui_scene.h"
#include "model/match_engine.h"

struct ImFont;

/**
 * The xG map (training ground plan section 24, T8): a pitch, the match one
 * or a training pitch of any size and goal kind, shaded by the engine's own
 * xG for a shooter attacking the right-hand goal, with contours; or by the
 * core of the chosen brain's shot score (the home side's brain from the
 * sandbox set-up: Capybara's own, else Binturong's). Hovering shows every
 * part of both at that spot. Settings: Shooting, foot or header, a defender at
 * a set distance, the space ahead.
 */
class XgMapScene : public GUIScene
{
 public:
  explicit XgMapScene(GUIView* guiView_ptr);
  ~XgMapScene() override;

  void onEnter() override;
  void update(float deltaTime) override;
  void render() override;
  SceneID getID() const override;

 private:
  /** Both values at one spot, as the settings say. */
  struct Spot
  {
    MatchEngine::XgBreakdown xg;
    /** As struck: a header's discount and the shot's limits applied. */
    float struck = 0.0F;
    /** Binturong's shot score core (Aardvark and Binturong brains). */
    MatchEngine::ShotScoreParts score;
    /** The shot score core the map draws, for the chosen brain. */
    float scoreTotal = 0.0F;
    bool considered = false;
  };
  /** Whether the map draws Capybara's shot score (the chosen brain). */
  [[nodiscard]] static bool capybaraScore();
  [[nodiscard]] MatchPitch pitch() const;
  [[nodiscard]] Spot spotAt(const MatchPitch& on, PitchPoint at) const;
  void rebuild();
  void renderPanel(float width);
  void renderMap(float width, float height);

  // Settings.
  bool trainingPitch = false;
  float lengthSetting = 35.0F;
  float widthSetting = 23.0F;
  int goalChoice = 5;
  float shootingSetting = 50.0F;
  bool header = false;
  bool defender = false;
  float defenderMetres = 2.0F;
  float spaceAhead = 1.0F;
  /** 0: xG; 1: the shot score (its core). */
  int layer = 0;

  // The map, worked out when a setting changes.
  bool dirty = true;
  MatchPitch built{};
  float cell = 1.0F;
  int columns = 0;
  int rows = 0;
  std::vector<Spot> grid;
  std::optional<PitchPoint> hovered;

  ImFont* regularFont = nullptr;
  ImFont* boldFont = nullptr;
};
