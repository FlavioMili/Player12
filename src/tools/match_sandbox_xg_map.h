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
#include <string>
#include <vector>

#include "gui/gui_scene.h"
#include "model/match_engine.h"
#include "model/match_game_xg.h"

struct ImFont;
class Drill;
class GameXgSweep;

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
    /** Game xG, layer 1 (the empty goal), from the saved table; below 0
     * when there is none for this goal. */
    float game = -1.0F;
  };
  /** The goal the map is for: the match's, or the training pitch's. */
  [[nodiscard]] GoalKind goalKind() const;
  /** Loads the saved empty-goal table for goalKind() (none if missing). */
  void loadGameTable();
  void renderSweep(float width);
  /** The card for the spot last pointed at. */
  void renderSpot(float width);
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
  /** 0: xG; 1: the shot score (its core); 2: game xG, the empty goal. */
  int layer = 0;

  // Game xG (game xG plan): the saved table for the map's goal, and a
  // sweep that measures it afresh.
  std::optional<GameXgTable> gameTable;
  std::optional<GoalKind> gameTableGoal;
  std::string gameTableNote;
  std::vector<std::unique_ptr<Drill>> drills;
  Drill* shotDrill = nullptr;
  std::unique_ptr<GameXgSweep> sweep;
  float sweepRepeats = 60.0F;
  std::string sweepNote;

  // The map, worked out when a setting changes.
  bool dirty = true;
  MatchPitch built{};
  float cell = 1.0F;
  /** Cell edges fall at origin + k × cell (m): set so the columns end at
   * the goal line and a row is centred on the goal's middle. */
  float originX = 0.0F;
  float originY = 0.0F;
  [[nodiscard]] float edgeX(int column) const
  {
    return originX + (static_cast<float>(column) * cell);
  }
  [[nodiscard]] float edgeY(int row) const
  {
    return originY + (static_cast<float>(row) * cell);
  }
  int columns = 0;
  int rows = 0;
  std::vector<Spot> grid;
  std::optional<PitchPoint> hovered;

  ImFont* regularFont = nullptr;
  ImFont* boldFont = nullptr;
};
