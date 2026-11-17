// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <cstdint>

#include "model/match_tuning.h"

/**
 * Named tuning values for the 2D tactical match view. Distances are metres
 * on the pitch unless a name says pixels (logical pixels, multiplied by the
 * UI scale), times are real seconds.
 */
struct MatchRender2DTuning final
{
  /** Mown surface: stripes along the length, cross bands for a checker. */
  struct Grass final
  {
    static constexpr int STRIPES = 18;
    static constexpr int BANDS = 8;
    /** Each cell is split so the lighting gradient stays smooth. */
    static constexpr int CELL_SPLIT = 2;
    static constexpr float STRIPE_CONTRAST = 0.055F;
    static constexpr float BAND_CONTRAST = 0.018F;
    static constexpr float NOISE_METRES = 9.0F;
    static constexpr float NOISE_STRENGTH = 0.08F;
    static constexpr float FINE_NOISE_METRES = 2.6F;
    static constexpr float FINE_NOISE_STRENGTH = 0.045F;
    /** Soft overhead light: bright centre, darker touchlines and corners. */
    static constexpr float LIGHT_CENTRE = 1.06F;
    static constexpr float LIGHT_EDGE = 0.86F;
    static constexpr ImU32 PITCH_COLOR = IM_COL32(50, 128, 52, 255);
    static constexpr ImU32 RUN_OFF_COLOR = IM_COL32(40, 106, 44, 255);
    /** Worn goalmouths and spots: darker, browner turf. */
    static constexpr ImU32 WEAR_COLOR = IM_COL32(84, 78, 44, 255);
    static constexpr std::uint8_t WEAR_ALPHA = 70;
    static constexpr int WEAR_SEGMENTS = 12;
  };

  struct Lines final
  {
    static constexpr float WIDTH_METRES = 0.12F;
    static constexpr float MIN_PIXELS = 1.0F;
    static constexpr ImU32 COLOR = IM_COL32(246, 248, 244, 236);
  };

  struct Goal final
  {
    static constexpr float POST_METRES = 0.12F;
    static constexpr float MIN_POST_PIXELS = 2.0F;
    static constexpr int NET_COLUMNS = 4;
    static constexpr int NET_ROWS = 12;
    static constexpr ImU32 POST_COLOR = IM_COL32(252, 252, 252, 255);
    static constexpr ImU32 NET_FILL_COLOR = IM_COL32(12, 18, 22, 120);
    static constexpr ImU32 NET_LINE_COLOR = IM_COL32(230, 236, 240, 88);
    static constexpr ImU32 SHADOW_COLOR = IM_COL32(0, 0, 0, 60);
  };

  /** Stands, boards and run-off around the pitch, at the engine's distances
   * (MatchTuning::Pitch: the boards, the stand's front wall and the rows of
   * crowd shown), so players and the ball off the pitch are drawn where the
   * engine has them. */
  struct Stadium final
  {
    /** The boards' thickness seen from above. */
    static constexpr float BOARD_DEPTH_METRES = 0.3F;
    /** One row of seats: the crowd is drawn a dot a row. */
    static constexpr float ROW_METRES = 0.8F;
    /** Between the boards and the stand: the track behind the boards. */
    static constexpr ImU32 GAP_COLOR = IM_COL32(52, 58, 56, 255);
    static constexpr float CROWD_SIZE_SHARE = 0.62F;
    static constexpr float EMPTY_SEAT_SHARE = 0.1F;
    /** Supporters in club colours: behind each goal, then elsewhere. */
    static constexpr float END_FAN_SHARE = 0.55F;
    static constexpr float SIDE_FAN_SHARE = 0.2F;
    static constexpr float BOARD_LENGTH_METRES = 8.0F;
    static constexpr ImU32 STAND_FRONT_COLOR = IM_COL32(44, 52, 68, 255);
    static constexpr ImU32 STAND_BACK_COLOR = IM_COL32(18, 22, 32, 255);
    static constexpr ImU32 BOARD_COLOR = IM_COL32(22, 26, 34, 255);
    static constexpr std::uint32_t SEED = 20260929U;
    /** The touchline furniture (MatchGround): the dugouts' perspex roofs
     * seen from above, the tunnel, and the technical areas' dashed lines. */
    static constexpr ImU32 DUGOUT_ROOF_COLOR = IM_COL32(120, 138, 152, 255);
    static constexpr ImU32 DUGOUT_EDGE_COLOR = IM_COL32(200, 210, 218, 255);
    static constexpr ImU32 TUNNEL_COLOR = IM_COL32(8, 10, 14, 255);
    static constexpr ImU32 TECHNICAL_LINE_COLOR = IM_COL32(236, 240, 236, 210);
    static constexpr float TECHNICAL_DASH_METRES = 0.6F;
    static constexpr float TECHNICAL_LINE_METRES = 0.1F;
  };

  /** The people round the touchline and the officials from above: their
   * body only (the engine's, 0.4 m across), as they can't play the ball
   * (user). Subs in their side's colours, staff in dark tracksuits ringed
   * with them, managers in suits. Players off the pitch are drawn at this
   * size too. */
  struct Touchline final
  {
    static constexpr float RADIUS_METRES =
        0.5F * MatchTuning::Body::DIAMETER_METRES;
    static constexpr float MIN_RADIUS_PIXELS = 2.5F;
    static constexpr float SUB_SHADE = 0.8F;
    static constexpr ImU32 STAFF_COLOR = IM_COL32(34, 38, 48, 255);
    static constexpr ImU32 SUIT_COLOR = IM_COL32(22, 22, 26, 255);
    /** A training pitch's serving coach: a bright training top. */
    static constexpr ImU32 COACH_COLOR = IM_COL32(236, 128, 32, 255);
    static constexpr ImU32 OUTLINE_COLOR = IM_COL32(10, 12, 16, 220);
  };

  /** Top-down kit tokens: a player on the pitch as the reach he plays the
   * ball within (the engine's outfield first-touch reach; user). */
  struct Token final
  {
    static constexpr float RADIUS_METRES =
        MatchTuning::Ball::OUTFIELD_CONTROL_RADIUS_METRES;
    static constexpr float SHIRT_SHARE = 0.74F;
    static constexpr float OUTLINE_SHARE = 0.12F;
    static constexpr float WEDGE_REACH = 1.62F;
    static constexpr float WEDGE_HALF_ANGLE = 0.42F;
    static constexpr float NUMBER_SHARE = 1.1F;
    /** Numbers smaller than this (pixels at scale 1) are not printed. */
    static constexpr float MIN_NUMBER_PIXELS = 7.5F;
    static constexpr float CARRIER_RING_SHARE = 1.5F;
    static constexpr float HOVER_RING_SHARE = 1.34F;
    static constexpr float PULSE_SPEED = 5.0F;
    static constexpr float SHADOW_OFFSET_SHARE = 0.22F;
    /** Players off the pitch (sent off, injured) wait faded by the bench. */
    static constexpr float OFF_PITCH_ALPHA = 0.45F;
    static constexpr ImU32 OUTLINE_COLOR = IM_COL32(10, 14, 20, 225);
    static constexpr ImU32 LIGHT_OUTLINE_COLOR = IM_COL32(245, 247, 250, 235);
    static constexpr ImU32 WEDGE_COLOR = IM_COL32(248, 250, 252, 235);
    static constexpr ImU32 SHADOW_COLOR = IM_COL32(0, 0, 0, 70);
    static constexpr ImU32 CARRIER_COLOR = IM_COL32(255, 214, 64, 255);
    static constexpr ImU32 HOVER_COLOR = IM_COL32(255, 255, 255, 230);
    static constexpr ImU32 CARD_COLOR = IM_COL32(250, 214, 40, 255);
    static constexpr ImU32 LABEL_BACK_COLOR = IM_COL32(6, 10, 20, 170);
    static constexpr ImU32 LABEL_TEXT_COLOR = IM_COL32(245, 247, 250, 255);
  };

  struct Ball final
  {
    /** Drawn larger than life (0.22 m) so it stays readable. */
    static constexpr float RADIUS_METRES = 0.36F;
    static constexpr float MIN_RADIUS_PIXELS = 2.5F;
    /** A lifted ball is drawn this share of its height up the screen. */
    static constexpr float LIFT_SHARE = 0.5F;
    /** Apparent growth per metre of height (closer to the eye). */
    static constexpr float GROWTH_PER_METRE = 0.035F;
    static constexpr float SHADOW_GROWTH = 0.16F;
    static constexpr float SHADOW_FADE = 0.3F;
    static constexpr std::uint8_t SHADOW_ALPHA = 120;
    /** Sun from the top left: the shadow slides this far per metre. */
    static constexpr float SHADOW_SLIDE = 0.22F;
    static constexpr float TRAIL_SECONDS = 0.22F;
    static constexpr float TRAIL_MIN_SPEED = 7.0F;
    static constexpr int TRAIL_SAMPLES = 10;
    /** A drawn ball moving further in one frame has jumped (a restart or
     * a playback skip): trails and paths are dropped. */
    static constexpr float JUMP_METRES = 20.0F;
    static constexpr ImU32 COLOR = IM_COL32(250, 250, 252, 255);
    static constexpr ImU32 OUTLINE_COLOR = IM_COL32(20, 24, 30, 200);
    static constexpr ImU32 PATCH_COLOR = IM_COL32(36, 40, 50, 255);
    static constexpr ImU32 TRAIL_COLOR = IM_COL32(255, 255, 255, 120);
  };

  /** Pass and shot paths shown briefly after the ball was struck. */
  struct Flight final
  {
    static constexpr int SAMPLES = 64;
    static constexpr float SAMPLE_SECONDS = 1.0F / 30.0F;
    static constexpr float FADE_SECONDS = 1.3F;
    /** Below this speed (m/s) a loose ball has stopped travelling. */
    static constexpr float STOP_SPEED = 2.5F;
    static constexpr float WIDTH_METRES = 0.16F;
    static constexpr ImU32 PASS_COLOR = IM_COL32(255, 255, 255, 150);
    static constexpr ImU32 SHOT_COLOR = IM_COL32(255, 132, 84, 210);
  };

  /** Offside flash along the defending line at the moment of the pass. */
  struct Offside final
  {
    static constexpr float SECONDS = 2.6F;
    static constexpr float DASH_METRES = 1.6F;
    static constexpr float PULSE_SPEED = 7.0F;
    static constexpr ImU32 COLOR = IM_COL32(255, 226, 64, 230);
  };

  /** The referee's signals: his chip grows to a player's size to show a
   * whistle (red: play stopped; green: restart), a card, play-on for an
   * advantage or an arrow the way the awarded side attacks; and the
   * free-kick spot marked on the grass. Timings are wall-clock seconds
   * (design choices, not researched). */
  struct RefereeSignal final
  {
    static constexpr float GROW_SECONDS = 0.25F;
    /** Each signal is held at least this long, whatever the match speed. */
    static constexpr float STOP_SECONDS = 1.5F;
    static constexpr float CARD_SECONDS = 1.3F;
    static constexpr float PLAY_ON_MIN_SECONDS = 1.0F;
    static constexpr float ARROW_MIN_SECONDS = 1.0F;
    static constexpr float GO_SECONDS = 0.8F;
    /** The grown chip is never smaller than this (pixels at scale 1). */
    static constexpr float MIN_GROWN_PIXELS = 11.0F;
    static constexpr ImU32 CHIP_COLOR = IM_COL32(24, 26, 32, 235);
    static constexpr ImU32 STOP_COLOR = IM_COL32(232, 64, 56, 255);
    static constexpr ImU32 GO_COLOR = IM_COL32(72, 204, 96, 255);
    static constexpr ImU32 YELLOW_CARD_COLOR = IM_COL32(245, 200, 40, 255);
    static constexpr ImU32 RED_CARD_COLOR = IM_COL32(220, 50, 50, 255);
    /** Free-kick spot ring (metres), and its fade in and out. */
    static constexpr float MARKER_RADIUS_METRES = 1.0F;
    static constexpr float MARKER_FADE_SECONDS = 0.5F;
    /** The foul spot while advantage is played: drawn fainter. */
    static constexpr float ADVANTAGE_MARKER_SHARE = 0.45F;
  };

  /** Pitch-control overlay: who would reach each spot first. */
  struct Pressure final
  {
    static constexpr int COLUMNS = 21;
    static constexpr int ROWS = 14;
    /** Influence falls to half at this distance (metres). */
    static constexpr float REACH_METRES = 7.0F;
    /** Players are projected this far ahead along their run (s). */
    static constexpr float LOOKAHEAD_SECONDS = 0.5F;
    static constexpr float RESPONSE_RATE = 4.0F;
    static constexpr float MAX_ALPHA = 118.0F;
  };
};
