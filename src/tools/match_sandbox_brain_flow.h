// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "gui/gui_scene.h"
#include "model/match_brain_flow.h"

struct ImFont;

/** The brain flow pitch on the screen, for one frame. */
struct BrainPitchFrame
{
  ImVec2 origin;
  float width = 1.0F;
  float height = 1.0F;

  /** A point given as fractions of the pitch (across, down). */
  [[nodiscard]] ImVec2 screen(float fx, float fy) const;
  [[nodiscard]] ImVec2 screen(PitchPoint at) const;
};

/**
 * The brain flow visualiser (Notes and plans/
 * match_engine_brain_flow_tool_plan.md): the new brain framework as a flow,
 * top to bottom (situation, reading, goal, options, measures, weights,
 * judgement, pick, body), on a live example scored by Capybara's own code.
 * The default flow has no influence layers; each layer can be switched on
 * to see which parts of the flow it touches, and by how much.
 */
class BrainFlowScene : public GUIScene
{
 public:
  /** The example's players, in this order: the carrier, three team-mates
   * (left midfielder, right winger, striker), then five opponents
   * (keeper, presser, midfielder, two centre-backs). */
  static constexpr std::size_t EXAMPLE_PLAYERS = 9;
  /** Pitch fractions (x along the length, the home side attacking to the
   * right; y across) of each example player. */
  using Spots = std::array<std::array<float, 2>, EXAMPLE_PLAYERS>;
  /** The example's set-up: where everyone stands, each layer's settings
   * (they count only while that layer is on), the carrier's attributes. */
  struct ExampleSettings
  {
    Spots spots{};
    // Team instructions: the team sliders, 0 to 1.
    float attacking = 0.0F;
    float risk = 0.0F;
    // Role & duty: indexes into the tool's lists.
    int carrierRole = 0;
    int carrierDuty = 0;
    int strikerRole = 0;
    // Shout: an index into the tool's list (0 is none).
    int shout = 0;
    // Opposition orders.
    bool tightMarkStriker = false;
    bool weakFootCarrier = false;
    // Game state.
    int minute = 0;
    int ourGoals = 0;
    int theirGoals = 0;
    // Familiarity & mood: familiarity with the tactics, 0 to 1.
    float familiarity = 0.0F;
    // The carrier's attributes, 1 to 100.
    float decisions = 0.0F;
    float passing = 0.0F;
    float shooting = 0.0F;
    float dribbling = 0.0F;
    float pace = 0.0F;
    float vision = 0.0F;
  };

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
  /** The settings of one layer, under its switch while it is on. */
  void renderLayerSettings(BrainFlow::Layer layer);
  void renderExample(float width);
  /** The example on a small pitch: players to drag, the options drawn. */
  void renderPitch(float width);
  [[nodiscard]] ImVec2 spotOf(const BrainPitchFrame& frame,
                              std::size_t index) const;
  void dragPlayers(const BrainPitchFrame& frame);
  void drawOptionLines(ImDrawList* draw, const BrainPitchFrame& frame) const;
  std::optional<std::size_t> drawPlayers(ImDrawList* draw,
                                         const BrainPitchFrame& frame) const;
  void drawOffBall(ImDrawList* draw, const BrainPitchFrame& frame) const;
  void renderFlow(float width, float height);
  /** The situation tree as rows of cards: pick one at each level. */
  void situationStage();
  void optionsStage();
  void measuresStage();
  void weightsStage();
  void judgementStage();
  void pickStage();
  void bodyStage();
  /** One player's off-ball frame at the example's moment (step O5). */
  void offBallStage();
  [[nodiscard]] bool layerOn(BrainFlow::Layer layer) const;
  [[nodiscard]] std::string nameOf(std::uint32_t player) const;

  // Settings.
  BrainFlow::LayerMask layers = 0;
  ExampleSettings settings;
  int preset = 0;
  /** Whose off-ball frame to show (an index into the example players). */
  int offBallPlayer = 1;
  /** True: scores at the true chances; false: one sample drawn. */
  bool steady = true;
  std::uint32_t rollSeed = 1;
  /** The example player being dragged on the pitch. */
  std::optional<std::size_t> dragging;
  /** The selected situation card at each level of the tree (indices into
   * the situation table); empty until first drawn, then the path to the
   * decision frame. */
  std::vector<std::size_t> situationPath;

  // The example, scored when a setting changes.
  bool dirty = true;
  std::string error;
  BrainFlow::CarrierRecord base;
  BrainFlow::CarrierRecord layered;
  /** The off-ball frame of offBallPlayer, all layers on. */
  BrainFlow::OffBallRecord offBall;
  std::map<std::uint32_t, std::string> names;
  /** The example players' ids, in EXAMPLE_PLAYERS order. */
  std::vector<std::uint32_t> exampleIds;
  std::string carrierName;
  PitchPoint carrierAt{};

  ImFont* regularFont = nullptr;
  ImFont* boldFont = nullptr;
};
