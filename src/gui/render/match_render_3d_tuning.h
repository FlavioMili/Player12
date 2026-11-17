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
 * Named tuning values for the 3D broadcast match view. Distances are metres,
 * angles radians, rates per real second.
 */
struct MatchRender3DTuning final
{
  struct Camera final
  {
    static constexpr float MAX_FRAME_SECONDS = 0.1F;
    static constexpr float TARGET_RATE = 3.2F;
    static constexpr float ANGLE_RATE = 2.6F;
    static constexpr float DISTANCE_RATE = 2.6F;
    static constexpr float FOV_RATE = 3.0F;
    static constexpr float ZOOM_STEP = 0.9F;
    static constexpr float MIN_ZOOM = 0.5F;
    static constexpr float MAX_ZOOM = 1.8F;
    static constexpr float MIN_EYE_HEIGHT = 2.0F;
    static constexpr float LOOKAHEAD_SECONDS = 0.35F;
    static constexpr float MAX_LOOKAHEAD = 9.0F;
    static constexpr float NEAR_PLANE = 0.5F;
    static constexpr float FAR_PLANE = 900.0F;
    /** Eye stays inside the bowl (in front of every stand). */
    static constexpr float EYE_MIN_X = -7.0F;
    static constexpr float EYE_MAX_X = 112.0F;
    static constexpr float EYE_MIN_Y = -5.0F;
    static constexpr float EYE_MAX_Y = 73.0F;
    static constexpr float STAND_CLEAR_HEIGHT = 40.0F;
    /** Steepest pitch used to keep a low camera out of the stands. */
    static constexpr float MAX_CLAMP_PITCH = 1.25F;
    /** A new attacking side must hold the ball this long to turn the end
     * camera. */
    static constexpr float ATTACK_SWITCH_SECONDS = 2.0F;
    /**
     * A frame advancing the match clock by more than this (s), or moving
     * the ball further than JUMP_METRES, is a playback jump: the camera
     * cuts to the new framing instead of easing towards it. Both stay above
     * what 30x playback covers in one slow frame.
     */
    static constexpr float JUMP_SIM_SECONDS = 4.0F;
    static constexpr float JUMP_METRES = 30.0F;
  };

  /** Official pitch markings (Laws of the Game), metres. */
  struct Markings final
  {
    /** The Laws cap line width at 12 cm. */
    static constexpr float LINE_WIDTH = 0.12F;
    static constexpr float PENALTY_AREA_DEPTH = 16.5F;
    static constexpr float PENALTY_AREA_WIDTH = 40.32F;
    static constexpr float GOAL_AREA_DEPTH = 5.5F;
    static constexpr float GOAL_AREA_WIDTH = 18.32F;
    static constexpr float PENALTY_SPOT_DISTANCE = 11.0F;
    static constexpr float CIRCLE_RADIUS = 9.15F;
    static constexpr float CORNER_ARC_RADIUS = 1.0F;
    static constexpr float SPOT_RADIUS = 0.22F;
    static constexpr int CIRCLE_SEGMENTS = 56;
    static constexpr int PENALTY_ARC_SEGMENTS = 18;
    static constexpr int CORNER_ARC_SEGMENTS = 6;
    static constexpr int SPOT_SEGMENTS = 8;
    static constexpr ImU32 COLOR = IM_COL32(242, 244, 240, 228);
  };

  struct Grass final
  {
    static constexpr int STRIPES = 18;
    static constexpr float SURROUND_X = 9.0F;
    static constexpr float SURROUND_Y = 7.0F;
    static constexpr float OUTSIDE_EXTENT = 140.0F;
    /** Grid resolution of the playing surface (columns per stripe, rows). */
    static constexpr int STRIPE_COLUMNS = 2;
    static constexpr int ROWS = 16;
    /**
     * Stripe brightness swing: blades leaning away from the camera look
     * lighter, so the pattern is strongest looking along the mowing
     * direction (across the pitch) and flips when looking the other way.
     */
    static constexpr float STRIPE_CONTRAST = 0.055F;
    static constexpr float STRIPE_SIDE_SHARE = 0.4F;
    static constexpr float NOISE_METRES = 7.0F;
    static constexpr float NOISE_STRENGTH = 0.09F;
    static constexpr float FINE_NOISE_METRES = 2.2F;
    static constexpr float FINE_NOISE_STRENGTH = 0.05F;
    /**
     * Worn patches: scuffed turf over soil. Darker than the grass, so the
     * blend never reads as a light spot on the darker stripes.
     */
    static constexpr ImU32 WEAR_COLOR = IM_COL32(78, 70, 40, 255);
    static constexpr std::uint8_t WEAR_ALPHA = 78;
    static constexpr float SPOT_WEAR_RADIUS = 1.3F;
    static constexpr float GOALMOUTH_DEPTH = 2.4F;
    static constexpr float GOALMOUTH_WIDTH = 3.4F;
    /** Assistant referees' paths along the touchlines. */
    static constexpr int LINESMAN_SCUFFS = 5;
    static constexpr float LINESMAN_OFFSET =
        MatchTuning::Officials::ASSISTANT_OUT_METRES;
    static constexpr float LINESMAN_HALF_WIDTH = 0.55F;
    static constexpr std::uint8_t LINESMAN_ALPHA = 46;
    static constexpr float VIGNETTE_LENGTH = 0.16F;
    static constexpr float VIGNETTE_WIDTH = 0.20F;
    static constexpr ImU32 PITCH_COLOR = IM_COL32(52, 128, 50, 255);
    static constexpr ImU32 SURROUND_COLOR = IM_COL32(40, 104, 44, 255);
    static constexpr ImU32 OUTSIDE_COLOR = IM_COL32(20, 24, 26, 255);
  };

  struct Goal final
  {
    static constexpr float WIDTH = 7.32F;
    static constexpr float HEIGHT = 2.44F;
    static constexpr float POST_SIZE = MatchTuning::Pitch::GOAL_POST_METRES;
    static constexpr float ROOF_DEPTH = 1.0F;
    static constexpr float ROOF_DROP = 0.25F;
    static constexpr float BASE_DEPTH =
        MatchTuning::Pitch::GOAL_NET_DEPTH_METRES;
    static constexpr int NET_ROOF_ROWS = 3;
    static constexpr int NET_BACK_ROWS = 7;
    static constexpr int NET_COLUMNS = 16;
    static constexpr int NET_SIDE_COLUMNS = 5;
    static constexpr ImU32 POST_COLOR = IM_COL32(248, 248, 250, 255);
    static constexpr ImU32 NET_FILL_COLOR = IM_COL32(235, 240, 245, 34);
    static constexpr ImU32 NET_LINE_COLOR = IM_COL32(236, 240, 246, 92);
    static constexpr ImU32 SUPPORT_COLOR = IM_COL32(70, 74, 82, 255);
  };

  struct CornerFlag final
  {
    static constexpr float POLE_HEIGHT = 1.6F;
    static constexpr float POLE_WIDTH = 0.05F;
    static constexpr float FLAG_LENGTH = 0.5F;
    static constexpr float FLAG_HEIGHT = 0.35F;
    static constexpr float WAVE_SPEED = 2.4F;
    static constexpr float WAVE_AMPLITUDE = 0.12F;
    static constexpr ImU32 POLE_COLOR = IM_COL32(240, 240, 240, 255);
    static constexpr ImU32 FLAG_COLOR = IM_COL32(250, 214, 40, 255);
  };

  struct Boards final
  {
    static constexpr float SIDE_OFFSET = MatchTuning::Pitch::BOARDS_SIDE_METRES;
    static constexpr float END_OFFSET = MatchTuning::Pitch::BOARDS_END_METRES;
    static constexpr float HEIGHT = MatchTuning::Pitch::BOARDS_HEIGHT_METRES;
    static constexpr float LENGTH = 8.0F;
    static constexpr float GAP = 0.15F;
    static constexpr float TEXT_HEIGHT_RATIO = 0.62F;
    static constexpr float TEXT_WIDTH_RATIO = 0.86F;
    /**
     * Lettering is baked at a size close to its projected height (steps of
     * FONT_STEP from MIN_FONT_SIZE) so glyphs are never heavily minified;
     * below MIN_TEXT_PIXELS a clean colour-block logo replaces it.
     */
    static constexpr float MIN_TEXT_PIXELS = 9.0F;
    static constexpr float MIN_FONT_SIZE = 9.0F;
    static constexpr float MAX_FONT_SIZE = 72.0F;
    static constexpr float FONT_STEP = 1.25F;
    /** Colour-block logo: bar height and a darker end mark. */
    static constexpr float LOGO_BAR_HEIGHT = 0.42F;
    static constexpr float LOGO_MARK_SHARE = 0.18F;
    static constexpr float ROTATE_SECONDS = 14.0F;
    static constexpr ImU32 BACK_COLOR = IM_COL32(34, 36, 42, 255);
  };

  /** The touchline furniture's look (its shapes are MatchGround's). */
  struct Fixtures final
  {
    static constexpr float PANEL = 0.05F;
    static constexpr float POST = 0.08F;
    static constexpr float PORTAL = 0.4F;
    static constexpr float BENCH_INSET = 0.3F;
    static constexpr float BENCH_DEPTH = 0.5F;
    static constexpr float BENCH_HEIGHT = 0.45F;
    /** The technical areas' dashed line: dash and gap, metres. */
    static constexpr float DASH = 0.6F;
    static constexpr ImU32 PERSPEX_COLOR = IM_COL32(170, 196, 214, 110);
    static constexpr ImU32 DUGOUT_BACK_COLOR = IM_COL32(52, 58, 70, 255);
    static constexpr ImU32 BENCH_COLOR = IM_COL32(30, 34, 44, 255);
    static constexpr ImU32 POST_COLOR = IM_COL32(200, 204, 210, 255);
    static constexpr ImU32 TUNNEL_COLOR = IM_COL32(6, 8, 12, 255);
    static constexpr ImU32 PORTAL_COLOR = IM_COL32(88, 92, 102, 255);
  };

  /** The people round the touchline: a plain figure (metres at 1.80 m
   * tall, scaled by height). Subs in a training top of their side's
   * colour, staff in dark tracksuits, managers in suits. */
  struct TouchlineFigure final
  {
    static constexpr float SEAT_HEIGHT = 0.47F;
    static constexpr float KNEE_FORWARD = 0.45F;
    static constexpr float ANKLE_HEIGHT = 0.08F;
    static constexpr float FOOT_LENGTH = 0.22F;
    static constexpr float FOOT_HALF = 0.045F;
    static constexpr float LIMB_HALF = 0.07F;
    static constexpr float ARM_HALF = 0.05F;
    static constexpr float ARM_LENGTH = 0.55F;
    static constexpr float TORSO_LENGTH = 0.52F;
    static constexpr float TORSO_HALF = 0.18F;
    static constexpr float TORSO_DEPTH = 0.11F;
    static constexpr float SHOULDER_SPREAD = 0.22F;
    static constexpr float NECK_LENGTH = 0.06F;
    static constexpr float LYING_HEIGHT = 0.15F;
    /** A walking stride: how fast the legs swing (rad/s), how far, and the
     * speed (m/s) at which it is full. */
    static constexpr float STRIDE_RATE = 5.0F;
    static constexpr float STRIDE_RATE_PER_SPEED = 1.4F;
    static constexpr float STRIDE_REACH = 0.25F;
    static constexpr float FULL_STRIDE_SPEED = 1.5F;
    /** Running (the officials): the stride grows past a walk by this per
     * m/s, up to this much more. */
    static constexpr float RUN_REACH_PER_SPEED = 0.05F;
    static constexpr float RUN_REACH_MOST = 0.25F;
    static constexpr ImU32 OFFICIAL_SHORTS_COLOR = IM_COL32(18, 18, 20, 255);
    static constexpr float SUB_TOP_SHADE = 0.85F;
    static constexpr std::uint32_t STAFF_LOOK_SEED = 900000U;
    static constexpr ImU32 TRACKSUIT_COLOR = IM_COL32(36, 40, 52, 255);
    static constexpr ImU32 SUIT_COLOR = IM_COL32(26, 26, 30, 255);
    /** A training pitch's serving coach: a bright training top. */
    static constexpr ImU32 COACH_TOP_COLOR = IM_COL32(236, 128, 32, 255);
    static constexpr ImU32 SHOE_COLOR = IM_COL32(20, 20, 22, 255);
  };

  struct Stands final
  {
    static constexpr float SIDE_FRONT = MatchTuning::Pitch::STAND_SIDE_METRES;
    static constexpr float END_FRONT = MatchTuning::Pitch::STAND_END_METRES;
    static constexpr int SIDE_SECTIONS = 9;
    static constexpr int END_SECTIONS = 6;
    static constexpr float FRONT_WALL_HEIGHT =
        MatchTuning::Pitch::STAND_WALL_HEIGHT_METRES;
    static constexpr float LOWER_DEPTH =
        MatchTuning::Pitch::LOWER_TIER_DEPTH_METRES;
    static constexpr float LOWER_TOP =
        MatchTuning::Pitch::LOWER_TIER_TOP_METRES;
    static constexpr float CONCOURSE_TOP = 14.5F;
    static constexpr float UPPER_DEPTH = 38.0F;
    static constexpr float UPPER_TOP = 30.0F;
    static constexpr float BACK_WALL_TOP = 34.0F;
    static constexpr float ROOF_FRONT_DEPTH = 13.0F;
    static constexpr float ROOF_BACK_DEPTH = 38.0F;
    static constexpr float ROOF_FRONT_HEIGHT = 31.5F;
    static constexpr float ROOF_BACK_HEIGHT = 34.0F;
    static constexpr float FASCIA_HEIGHT = 1.8F;
    static constexpr float WINDOW_INSET = 1.1F;
    static constexpr ImU32 CONCRETE_COLOR = IM_COL32(78, 82, 92, 255);
    static constexpr ImU32 WALL_COLOR = IM_COL32(40, 44, 54, 255);
    static constexpr ImU32 SEAT_COLOR = IM_COL32(40, 52, 88, 255);
    static constexpr ImU32 ROOF_UNDER_COLOR = IM_COL32(30, 33, 40, 255);
    static constexpr ImU32 ROOF_TOP_COLOR = IM_COL32(84, 90, 102, 255);
    static constexpr ImU32 FASCIA_COLOR = IM_COL32(24, 30, 52, 255);
    static constexpr ImU32 WINDOW_COLOR = IM_COL32(176, 150, 104, 255);
    static constexpr float BACK_ROW_LIGHT = 0.5F;
  };

  /**
   * Spectators at real seat pitch. Seats are grouped into clumps
   * (CLUMP_SEATS x CLUMP_ROWS) that stand in for their members once a seat
   * shrinks below CLUMP_PIXELS on screen, and every spectator's colour fades
   * towards its clump average as it gets smaller (a cheap mip-map), so the
   * stands read as a crowd instead of pixel noise at any distance.
   */
  struct Crowd final
  {
    static constexpr float SEAT_SPACING = 0.55F;
    static constexpr float ROW_DEPTH = 0.85F;
    static constexpr float DOT_WIDTH = 0.44F;
    static constexpr float DOT_HEIGHT = 0.82F;
    static constexpr float JITTER = 0.12F;
    static constexpr float EMPTY_SEAT_RATIO = 0.08F;
    static constexpr float HOME_SHARE = 0.34F;
    /** The home end (west) is packed with the club's own supporters. */
    static constexpr float HOME_END_SHARE = 0.62F;
    static constexpr float SCARF_SHARE = 0.25F;
    static constexpr float HOME_END_SCARF_SHARE = 0.5F;
    static constexpr float NEUTRAL_CHEER_SHARE = 0.6F;
    static constexpr float FLAG_SHARE = 0.004F;
    static constexpr float HOME_END_FLAG_SHARE = 0.013F;
    static constexpr float AWAY_END_FLAG_SHARE = 0.007F;
    /** Flag pole, cloth size (metres) and waving. */
    static constexpr float POLE_HEIGHT = 1.5F;
    static constexpr float FLAG_LENGTH = 1.25F;
    static constexpr float FLAG_HEIGHT = 0.8F;
    static constexpr float FLAG_WAVE = 0.18F;
    static constexpr float FLAG_WAVE_SPEED = 3.2F;
    /** Flags smaller than this on screen (px) are left to the crowd tint. */
    static constexpr float FLAG_MIN_PIXELS = 2.5F;
    /** Idle sway and goal hop as shares of a spectator's height. */
    static constexpr float SWAY = 0.035F;
    static constexpr float SWAY_SPEED = 1.3F;
    static constexpr float HOP = 0.3F;
    static constexpr float HOP_SPEED = 8.5F;
    /** Rising out of the seats for a shot: height share and rates (1/s). */
    static constexpr float RISE = 0.16F;
    static constexpr float RISE_RATE = 5.0F;
    static constexpr float SETTLE_RATE = 1.2F;
    /** Raised arms reach this share of the height above the head. */
    static constexpr float ARM_REACH = 0.42F;
    static constexpr float ARM_WIDTH_SHARE = 0.16F;
    static constexpr float SCARF_WIDTH_SHARE = 1.9F;
    static constexpr float SCARF_HEIGHT_SHARE = 0.11F;
    /** Shoulders taper to this share of the body width. */
    static constexpr float SHOULDER_SHARE = 0.78F;
    static constexpr float SHOULDER_DROP = 0.14F;
    static constexpr ImU32 POLE_COLOR = IM_COL32(58, 52, 44, 255);
    static constexpr float AWAY_END_SHARE = 0.55F;
    /** Low-frequency swing of the team share (fan blocks, quieter areas). */
    static constexpr float SHARE_SWING = 0.3F;
    static constexpr float NOISE_SEATS = 9.0F;
    static constexpr float NOISE_ROWS = 5.0F;
    static constexpr float LIGHT_SWING = 0.14F;
    static constexpr float LIGHT_JITTER = 0.04F;
    static constexpr int CLUMP_SEATS = 4;
    static constexpr int CLUMP_ROWS = 2;
    /** Clump quads reach this share of a spectator above the next row. */
    static constexpr float CLUMP_TOP_SHARE = 0.6F;
    /** Seat pitch on screen below which clumps replace spectators. */
    static constexpr float CLUMP_PIXELS = 3.2F;
    /** Clump width below which the stand face alone shows the crowd. */
    static constexpr float MIN_CLUMP_PIXELS = 1.4F;
    /** Seat pitch at which a spectator shows its own colour fully. */
    static constexpr float FULL_DETAIL_PIXELS = 14.0F;
    /** Spectators wider than this get a separate head block. */
    static constexpr float HEAD_DETAIL_PIXELS = 5.0F;
    static constexpr float BODY_SHARE = 0.66F;
    static constexpr float NECK_GAP = 0.04F;
    static constexpr float HEAD_WIDTH_SHARE = 0.56F;
    /** Seat colour showing through the crowd on the stand faces. */
    static constexpr float SEAT_SHOW_THROUGH = 0.25F;
    static constexpr std::uint32_t SEED = 20260927U;
  };

  struct Floodlight final
  {
    static constexpr float CORNER_OFFSET_X = 32.0F;
    static constexpr float CORNER_OFFSET_Y = 30.0F;
    static constexpr float MAST_HEIGHT = 58.0F;
    static constexpr float MAST_SIZE = 1.4F;
    static constexpr float HEAD_WIDTH = 11.0F;
    static constexpr float HEAD_HEIGHT = 6.0F;
    static constexpr float HEAD_TILT = 0.55F;
    static constexpr float GLOW_RADIUS = 9.0F;
    static constexpr ImU32 MAST_COLOR = IM_COL32(92, 96, 106, 255);
    static constexpr ImU32 LAMP_COLOR = IM_COL32(255, 252, 232, 255);
    static constexpr ImU32 GLOW_COLOR = IM_COL32(255, 246, 214, 255);
  };

  /**
   * Low-poly footballer proportions in metres for the 1.80 m reference
   * (top of the head at 1.80 m, shoulders about 0.5 m across the arms); a
   * player is scaled by his real height. Animation is driven by ground
   * speed: stride length grows with speed, the leg phase advances with the
   * distance actually covered on screen, and slow players idle. Swing
   * amplitudes come from the Gait shapes.
   */
  struct Player final
  {
    static constexpr float SCALE = 1.0F;
    static constexpr float REFERENCE_HEIGHT_METRES = 1.80F;
    static constexpr float MIN_HEIGHT_METRES = 1.62F;
    static constexpr float MAX_HEIGHT_METRES = 2.02F;
    static constexpr float HIP_HEIGHT = 0.93F;
    static constexpr float HIP_SPREAD = 0.095F;
    static constexpr float THIGH_LENGTH = 0.46F;
    static constexpr float SHIN_LENGTH = 0.42F;
    /**
     * Tapered limbs (half widths, metres): thighs narrow from the hip to the
     * knee, calves swell below the knee and thin to the ankle, arms thin
     * from the shoulder to the wrist.
     */
    static constexpr float THIGH_TOP = 0.088F;
    static constexpr float THIGH_MIDDLE = 0.072F;
    static constexpr float THIGH_BOTTOM = 0.056F;
    static constexpr float CALF_TOP = 0.06F;
    static constexpr float ANKLE_HALF_WIDTH = 0.038F;
    /** Shorts legs cover this share of the thigh and flare at the hem. */
    static constexpr float SHORTS_LEG_SHARE = 0.42F;
    static constexpr float SHORTS_LEG_TOP = 0.1F;
    static constexpr float SHORTS_LEG_HEM = 0.094F;
    /** The turned-down sock band ends this share of the way to the ankle. */
    static constexpr float SOCK_BAND_SHARE = 0.12F;
    static constexpr float BOOT_HALF_LENGTH = 0.13F;
    static constexpr float BOOT_HALF_WIDTH = 0.05F;
    static constexpr float BOOT_HALF_HEIGHT = 0.045F;
    static constexpr float BOOT_TOE_WIDTH = 0.04F;
    static constexpr float BOOT_TOE_HEIGHT = 0.03F;
    static constexpr float BOOT_FORWARD = 0.05F;
    static constexpr float SHORTS_HEIGHT = 0.86F;
    static constexpr float SHORTS_HALF_DEPTH = 0.115F;
    static constexpr float SHORTS_HALF_WIDTH = 0.17F;
    static constexpr float SHORTS_HALF_HEIGHT = 0.13F;
    static constexpr float TORSO_BASE = 0.98F;
    static constexpr float TORSO_LENGTH = 0.52F;
    static constexpr float WAIST_HALF_DEPTH = 0.1F;
    static constexpr float WAIST_HALF_WIDTH = 0.15F;
    static constexpr float CHEST_HALF_DEPTH = 0.11F;
    static constexpr float CHEST_HALF_WIDTH = 0.19F;
    /** Up close the torso widens through the ribs into the shoulders. */
    static constexpr float RIB_SHARE = 0.55F;
    static constexpr float RIB_HALF_WIDTH = 0.165F;
    static constexpr float COLLAR_HALF_SIZE = 0.075F;
    static constexpr float COLLAR_HALF_HEIGHT = 0.022F;
    static constexpr float SHOULDER_SPREAD = 0.215F;
    static constexpr float SHOULDER_DROP = 0.05F;
    static constexpr float UPPER_ARM_LENGTH = 0.3F;
    static constexpr float UPPER_ARM_TOP = 0.05F;
    static constexpr float UPPER_ARM_MIDDLE = 0.043F;
    static constexpr float ELBOW_HALF_WIDTH = 0.036F;
    static constexpr float WRIST_HALF_WIDTH = 0.028F;
    /** Short sleeves end this share of the way to the elbow. */
    static constexpr float SLEEVE_SHARE = 0.55F;
    static constexpr float SLEEVE_HEM = 0.052F;
    static constexpr float FOREARM_LENGTH = 0.27F;
    static constexpr float NECK_LENGTH = 0.19F;
    static constexpr float NECK_HALF_WIDTH = 0.052F;
    static constexpr float HEAD_RADIUS = 0.11F;
    static constexpr float ELBOW_BEND = 0.35F;
    /**
     * Rounded bodies up close: above this projected height the torso and
     * limbs become prisms (TORSO_SIDES, LIMB_SIDES) instead of boxes.
     */
    static constexpr float ROUND_MIN_PIXELS = 80.0F;
    static constexpr int TORSO_SIDES = 8;
    static constexpr int LIMB_SIDES = 6;
    /** The sole under the boot, seen on raised feet up close. */
    static constexpr float SOLE_HALF_HEIGHT = 0.012F;
    /**
     * One stride cycle (two steps) covers BASE + PER_SPEED * speed metres:
     * ~1.55 m walking, ~2.5 m jogging, ~3.75 m sprinting (0.9-2 cycles/s).
     */
    static constexpr float STRIDE_BASE_METRES = 1.05F;
    static constexpr float STRIDE_PER_SPEED = 0.36F;
    /** Below this ground speed (m/s) the legs settle into an idle stance. */
    static constexpr float IDLE_SPEED = 0.35F;
    static constexpr float FULL_STRIDE_SPEED = 8.0F;
    static constexpr float STRIDE_RATE = 7.0F;
    /** Cap per rendered frame so fast playback never aliases the legs. */
    static constexpr float MAX_CYCLES_PER_FRAME = 0.3F;
    static constexpr float TELEPORT_METRES = 12.0F;
    static constexpr float CULL_MARGIN_PIXELS = 80.0F;
    /** Below this projected height a team-coloured disc marks the feet. */
    static constexpr float MARKER_MAX_PIXELS = 34.0F;
    static constexpr float MARKER_RADIUS = 0.42F;
    static constexpr std::uint8_t MARKER_ALPHA = 150;
    static constexpr ImU32 SOLE_COLOR = IM_COL32(222, 222, 214, 255);
    /** Hands (keeper gloves) are only modelled above this projected height. */
    static constexpr float HAND_MIN_PIXELS = 44.0F;
    static constexpr float HAND_HALF_SIZE = 0.045F;
    static constexpr float GLOVE_HALF_SIZE = 0.06F;
  };

  /**
   * Walk, jog and sprint gaits blended by ground speed (m/s): swing
   * amplitudes (radians), lean, bob (metres) and elbow bend of each.
   */
  struct Gait final
  {
    struct Shape
    {
      float speed;
      float thigh;
      float knee;
      float arm;
      float elbow;
      float lean;
      float bob;
    };
    static constexpr Shape WALK{.speed = 1.4F,
                                .thigh = 0.34F,
                                .knee = 0.42F,
                                .arm = 0.28F,
                                .elbow = 0.2F,
                                .lean = 0.03F,
                                .bob = 0.018F};
    static constexpr Shape JOG{.speed = 4.0F,
                               .thigh = 0.52F,
                               .knee = 0.95F,
                               .arm = 0.55F,
                               .elbow = 0.95F,
                               .lean = 0.1F,
                               .bob = 0.042F};
    static constexpr Shape SPRINT{.speed = 7.5F,
                                  .thigh = 0.78F,
                                  .knee = 1.45F,
                                  .arm = 0.95F,
                                  .elbow = 1.3F,
                                  .lean = 0.26F,
                                  .bob = 0.06F};
    /** Speed (m/s) is smoothed at this rate before choosing the blend. */
    static constexpr float SPEED_RATE = 5.0F;
  };

  /**
   * Whole-body poses layered over the run cycle. Leans are radians, rates
   * per real second; accelerations come from the simulated ground speed.
   */
  struct Pose final
  {
    /** Forward lean per m/s^2 of acceleration (negative when braking). */
    static constexpr float ACCEL_LEAN = 0.05F;
    static constexpr float MAX_ACCEL_LEAN = 0.22F;
    static constexpr float MAX_BRAKE_LEAN = 0.18F;
    /** Braking harder than this sits the hips down over the planted feet. */
    static constexpr float STOP_DECELERATION = 3.0F;
    static constexpr float STOP_HIP_DROP = 0.05F;
    /** Time constant of the acceleration estimate (1/s). */
    static constexpr float ACCEL_SMOOTHING = 8.0F;
    /** Sideways lean into turns per rad/s of turn rate and m/s of speed. */
    static constexpr float TURN_LEAN = 0.035F;
    static constexpr float MAX_TURN_LEAN = 0.3F;
    static constexpr float TURN_SMOOTHING = 8.0F;
    /** Turn rates above this (rad/s) are snaps, not running turns. */
    static constexpr float MAX_TURN_RATE = 9.0F;
    /** The shoulders lead a turn: twist per rad/s of turn rate, capped. */
    static constexpr float TWIST_PER_TURN_RATE = 0.07F;
    static constexpr float MAX_TWIST = 0.45F;
    /** Longest simulated step one frame may account for (s). */
    static constexpr float MAX_SIM_SECONDS = 0.25F;
  };

  /**
   * Kicks and headers, started on the step the engine strikes the ball: the
   * kicking ankle is on the ball at that moment, then follows through
   * (simulated seconds).
   */
  struct Kick final
  {
    static constexpr float CONTACT_HOLD_SECONDS = 0.05F;
    static constexpr float FOLLOW_THROUGH_SECONDS = 0.26F;
    static constexpr float RECOVER_SECONDS = 0.26F;
    static constexpr float HEADER_SECONDS = 0.45F;
    static constexpr float HEADER_NOD_SECONDS = 0.14F;
    /** Balls above this height (m) at the touch are headed. */
    static constexpr float HEADER_HEIGHT = 1.35F;
    /** A ball leaving higher than this (m) after one step is lofted. */
    static constexpr float LOFT_HEIGHT = 0.6F;
    /** Ball jumps longer than this (m) in one step are restarts. */
    static constexpr float TELEPORT_METRES = 6.0F;
    /** A ball this far to one side (m) is struck with that foot. */
    static constexpr float WRONG_FOOT_METRES = 0.2F;
    /** The instep meets the ball: the ankle sits behind and above it. */
    static constexpr float CONTACT_BEHIND = 0.12F;
    static constexpr float CONTACT_ABOVE = 0.05F;
    static constexpr float TORSO_LEAN = -0.16F;
    static constexpr float PASS_LEAN = 0.08F;
    static constexpr float ARM_ABDUCTION = 0.8F;
    static constexpr float HEADER_NOD = 0.45F;
  };

  /**
   * Procedural skeleton: foot planting, the pelvis over the planted feet,
   * jumps, tackles and throw-ins, and the level of detail.
   */
  struct Rig final
  {
    /** Ankle above the grass with the boot flat. */
    static constexpr float ANKLE_HEIGHT = 0.085F;
    /** Ankle to the ball of the foot: the heel lifts around this point. */
    static constexpr float TOE_LENGTH = 0.15F;
    static constexpr float MAX_HEEL_ANGLE = 1.05F;
    /** Toes drop by this share while a foot swings through the air. */
    static constexpr float SWING_TOE_DROP = 0.35F;
    /** The pelvis sinks at most this much to keep a front foot planted. */
    static constexpr float MAX_PELVIS_DROP = 0.09F;
    /** Running crouch at full stride (m). */
    static constexpr float RUN_CROUCH = 0.05F;
    /** Half the distance between the feet at rest (m, per 1.80 m). */
    static constexpr float STANCE_WIDTH = 0.11F;
    /** A foot is put down at most this far ahead of its hip. */
    static constexpr float FRONT_REACH = 0.32F;
    /** Swing foot clearance walking and at full stride. */
    static constexpr float WALK_LIFT = 0.07F;
    static constexpr float SPRINT_LIFT = 0.32F;
    /** A planted foot this far off its gait spot is put down again. */
    static constexpr float MAX_DRIFT = 0.55F;
    /** Standing: a foot steps once the body is this far off it. */
    static constexpr float IDLE_STEP_METRES = 0.22F;
    static constexpr float IDLE_STEP_SECONDS = 0.22F;
    /** Smoothed gait speed below which a player counts as standing. */
    static constexpr float IDLE_GAIT_SPEED = 0.6F;
    /** Direction of travel smoothing (1/s). */
    static constexpr float DIRECTION_RATE = 10.0F;
    /** Below this projected height a player is a few flat strokes. */
    static constexpr float FAR_PIXELS = 30.0F;
    /** Jumping for a high ball within this distance (m), up to MAX_JUMP. */
    static constexpr float JUMP_REACH = 1.6F;
    static constexpr float MAX_JUMP = 0.5F;
    static constexpr float JUMP_RATE = 9.0F;
    static constexpr float HEADER_MIN_BALL = 1.3F;
    static constexpr float HEADER_MAX_BALL = 3.4F;
    /** In the air the feet rise this share of the jump and gather in. */
    static constexpr float JUMP_TUCK = 0.85F;
    static constexpr float JUMP_GATHER = 0.4F;
    /** A cooldown rise above this (s) marks a challenge. */
    static constexpr float TACKLE_COOLDOWN_RISE = 0.5F;
    static constexpr float TACKLE_SECONDS = 0.5F;
    static constexpr float TACKLE_LEAN = 0.35F;
    static constexpr float TACKLE_HIP_DROP = 0.14F;
    /** Challenges from further out (m) or faster (m/s) go to ground. */
    static constexpr float SLIDE_DISTANCE = 1.5F;
    static constexpr float SLIDE_SPEED = 4.2F;
    static constexpr float SLIDE_SECONDS = 1.1F;
    static constexpr float SLIDE_PELVIS = 0.3F;
    static constexpr float SLIDE_LEAN = -0.75F;
    static constexpr float SLIDE_LEG_REACH = 0.85F;
    static constexpr float SLIDE_TUCK_REACH = 0.25F;
    /** A ball further than LUNGE_FREE (m) at a strike or block pulls the
     * body towards it by up to LUNGE_MAX. */
    static constexpr float LUNGE_FREE = 0.45F;
    static constexpr float LUNGE_MAX = 0.45F;
    /** Throw-ins: the taker holds the ball overhead near the spot. */
    static constexpr float THROW_PICKUP_METRES = 1.6F;
    static constexpr float THROW_SECONDS = 0.45F;
    static constexpr float THROW_WHIP_SECONDS = 0.16F;
    static constexpr float THROW_GRIP_UP = 0.2F;
    static constexpr float THROW_GRIP_BACK = -0.12F;
    static constexpr float THROW_RELEASE_FORWARD = 0.38F;
    /** Half the gap between the hands on a held ball. */
    static constexpr float HAND_GRIP_HALF = 0.1F;
    static constexpr float KEEPER_GRIP_DOWN = 0.12F;
    static constexpr float KEEPER_GRIP_FORWARD = 0.24F;
    /** Builds vary between these width factors. */
    static constexpr float MIN_BULK = 0.94F;
    static constexpr float MAX_BULK = 1.07F;
  };

  /** Goalkeeper set stance and dives. */
  struct Keeper final
  {
    /** The set stance is taken while the ball is this close (m). */
    static constexpr float SET_DISTANCE = 26.0F;
    static constexpr float SET_HIP_DROP = 0.14F;
    /** Feet planted wider in the set stance (m). */
    static constexpr float SET_FOOT_SPREAD = 0.12F;
    static constexpr float SET_ARM_FORWARD = 0.55F;
    static constexpr float SET_ARM_SPREAD = 0.4F;
    static constexpr float SET_LEAN = 0.22F;
    /** Body roll of a full-stretch dive and the height of the flight. */
    static constexpr float DIVE_ROLL = 1.3F;
    static constexpr float DIVE_LIFT = 0.42F;
    static constexpr float DIVE_RATE = 12.0F;
    /** Down after a collision, lying on his side: the pelvis this far from
     * standing hip height (m; near the ground). */
    static constexpr float FALLEN_LIFT = -0.75F;
  };

  /** Players after a goal (simulated seconds since the goal). */
  struct Celebration final
  {
    static constexpr float PLAYER_SECONDS = 14.0F;
    static constexpr float CROWD_SECONDS = 16.0F;
    static constexpr float FADE_SECONDS = 5.0F;
    static constexpr float ARMS_UP = 2.75F;
    static constexpr float ARMS_SPREAD = 0.35F;
    static constexpr float WINGS_SPREAD = 1.45F;
    static constexpr float HOP = 0.12F;
    static constexpr float HOP_SPEED = 7.0F;
    static constexpr float DEJECTED_LEAN = 0.32F;
  };

  /** Shirt numbers (and names) on the players' backs. */
  struct Number final
  {
    /** Printed digit height and where it sits up the back (metres). */
    static constexpr float HEIGHT = 0.27F;
    static constexpr float CENTRE_UP = 0.3F;
    static constexpr float NAME_HEIGHT = 0.065F;
    static constexpr float NAME_GAP = 0.02F;
    /** Smallest projected digit (px) worth drawing; names need more. */
    static constexpr float MIN_PIXELS = 5.0F;
    static constexpr float NAME_MIN_PIXELS = 7.5F;
    static constexpr float MIN_FONT_SIZE = 6.0F;
    static constexpr float MAX_FONT_SIZE = 96.0F;
    static constexpr float FONT_STEP = 1.25F;
    /** The back must face the camera at least this much (cosine). */
    static constexpr float MIN_FACING = 0.2F;
    /** Stroke thickness of the heavy digits as a share of their height. */
    static constexpr float WEIGHT = 0.045F;
  };

  struct Ball final
  {
    /** Visual spin: panels turn with the ground covered, capped per frame
     * so fast balls never strobe. */
    static constexpr float SPIN_MAX_PER_FRAME = 0.7F;
    /** Real radius (0.22 m ball); boosted only when far for readability. */
    static constexpr float RADIUS = 0.11F;
    static constexpr float BOOST_START_DEPTH = 30.0F;
    static constexpr float BOOST_FULL_DEPTH = 110.0F;
    static constexpr float MAX_BOOST = 1.7F;
    static constexpr float MIN_PIXELS = 2.2F;
    static constexpr float SHADOW_RADIUS = 0.16F;
    static constexpr float SHADOW_GROWTH = 0.12F;
    static constexpr float SHADOW_FADE = 0.25F;
    static constexpr ImU32 COLOR = IM_COL32(244, 245, 248, 255);
    static constexpr ImU32 SHADE_COLOR = IM_COL32(150, 156, 170, 255);
    static constexpr ImU32 PATCH_COLOR = IM_COL32(40, 44, 56, 255);
  };

  struct Shadow final
  {
    static constexpr float CONTACT_RADIUS = 0.42F;
    static constexpr float BLADE_HALF_WIDTH = 0.2F;
    static constexpr int SEGMENTS = 10;
    static constexpr ImU32 CONTACT_COLOR = IM_COL32(0, 0, 0, 120);
    /** Darkness of one floodlight blade at an equal share of the light. */
    static constexpr std::uint8_t BLADE_ALPHA = 36;
    /**
     * Up close the four blades would read as a hard "X": they fade to
     * CLOSE_SHARE of their darkness between these projected player heights.
     */
    static constexpr float CLOSE_FADE_START_PIXELS = 70.0F;
    static constexpr float CLOSE_FADE_FULL_PIXELS = 190.0F;
    static constexpr float CLOSE_SHARE = 0.5F;
    /** Soft profile: a ring at this share of the radius keeps this share of
     * the centre darkness (a flatter core, a long feathered edge). */
    static constexpr float CORE_RADIUS_SHARE = 0.5F;
    static constexpr float CORE_ALPHA_SHARE = 0.62F;
    /** Daylight: one sun shadow per player instead of four blades. */
    static constexpr std::uint8_t SUN_ALPHA = 92;
    static constexpr float SUN_LENGTH_SHARE = 0.95F;
    static constexpr float MIN_LAMP_SHARE = 0.45F;
    static constexpr float MAX_LAMP_SHARE = 1.8F;
    static constexpr float MIN_BLADE = 0.8F;
    static constexpr float MAX_BLADE = 2.6F;
    /** Soft edge added around each blade (metres). */
    static constexpr float PENUMBRA = 0.2F;
    static constexpr float RING_RADIUS = 0.62F;
    static constexpr int RING_SEGMENTS = 20;
    static constexpr float RING_THICKNESS = 2.2F;
    static constexpr float RING_PULSE = 0.08F;
    static constexpr float RING_PULSE_SPEED = 5.0F;
    static constexpr ImU32 RING_COLOR = IM_COL32(255, 214, 64, 235);
  };

  struct Light final
  {
    static constexpr float AMBIENT = 0.52F;
    static constexpr float DIFFUSE = 0.55F;
    static constexpr float SKY = 0.12F;
    static constexpr float DIRECTION_X = 0.3F;
    static constexpr float DIRECTION_Y = -0.62F;
    static constexpr float DIRECTION_Z = 0.72F;
  };

  /**
   * Daylight preset: an afternoon sun high over the main stand's left
   * shoulder, a brighter sward and a blue sky. Chosen from the kick-off time
   * (DAY_FROM_MINUTES to DAY_UNTIL_MINUTES) or by the view's day toggle.
   */
  struct Day final
  {
    static constexpr int DAY_FROM_MINUTES = 10 * 60;
    static constexpr int DAY_UNTIL_MINUTES = (17 * 60) + 30;
    static constexpr float AMBIENT = 0.58F;
    static constexpr float DIFFUSE = 0.62F;
    static constexpr float SKY = 0.1F;
    /** Towards the sun (normalised on use). */
    static constexpr float SUN_X = -0.45F;
    static constexpr float SUN_Y = -0.55F;
    static constexpr float SUN_Z = 0.7F;
    /** The sward by day (even light, no floodlight falloff). */
    static constexpr float GRASS_LIGHT = 1.06F;
    /** Stands by day: sky fill, sun and the sky from above. */
    static constexpr float STAND_AMBIENT = 0.56F;
    static constexpr float STAND_DIFFUSE = 0.6F;
    static constexpr float STAND_SKY = 0.12F;
    /** Daylight colours of the stands (lit by the sun on build). */
    static constexpr ImU32 CONCRETE_COLOR = IM_COL32(168, 166, 158, 255);
    static constexpr ImU32 WALL_COLOR = IM_COL32(112, 114, 120, 255);
    static constexpr ImU32 SEAT_COLOR = IM_COL32(48, 66, 118, 255);
    static constexpr ImU32 ROOF_UNDER_COLOR = IM_COL32(96, 100, 108, 255);
    static constexpr ImU32 ROOF_TOP_COLOR = IM_COL32(200, 202, 208, 255);
    static constexpr ImU32 FASCIA_COLOR = IM_COL32(38, 50, 88, 255);
    static constexpr ImU32 WINDOW_COLOR = IM_COL32(70, 84, 98, 255);
    static constexpr ImU32 MAST_COLOR = IM_COL32(156, 160, 168, 255);
    static constexpr ImU32 LAMP_OFF_COLOR = IM_COL32(196, 200, 206, 255);
    static constexpr ImU32 OUTSIDE_COLOR = IM_COL32(86, 90, 88, 255);
    /** Roof shadows on the ground: darkness and soft edge (m). */
    static constexpr std::uint8_t STAND_SHADOW_ALPHA = 92;
    static constexpr float STAND_SHADOW_PENUMBRA = 0.9F;
    /** Players in a stand's shadow lose the sun but keep the sky. */
    static constexpr float SHADE_AMBIENT = 0.66F;
    static constexpr ImU32 TOP_COLOR = IM_COL32(58, 118, 196, 255);
    static constexpr ImU32 MIDDLE_COLOR = IM_COL32(116, 170, 226, 255);
    static constexpr ImU32 HORIZON_COLOR = IM_COL32(200, 222, 236, 255);
    static constexpr ImU32 VIGNETTE_COLOR = IM_COL32(0, 0, 0, 48);
  };

  struct Sky final
  {
    static constexpr float HORIZON_DISTANCE = 1000.0F;
    static constexpr ImU32 TOP_COLOR = IM_COL32(6, 10, 26, 255);
    static constexpr ImU32 MIDDLE_COLOR = IM_COL32(22, 34, 72, 255);
    static constexpr ImU32 HORIZON_COLOR = IM_COL32(86, 78, 112, 255);
    static constexpr ImU32 GROUND_COLOR = IM_COL32(14, 16, 20, 255);
    static constexpr float VIGNETTE_EDGE = 0.16F;
    static constexpr ImU32 VIGNETTE_COLOR = IM_COL32(0, 0, 0, 96);
  };

  /** Lower-third goal sting; sizes in font heights (HiDPI safe). */
  struct Sting final
  {
    static constexpr float SECONDS = 10.0F;
    static constexpr float FADE_IN_SECONDS = 0.35F;
    static constexpr float FADE_OUT_SECONDS = 0.6F;
    static constexpr float SLIDE_EM = 2.5F;
    static constexpr float TITLE_SCALE = 2.6F;
    static constexpr float PADDING_EM = 0.9F;
    static constexpr float BAR_EM = 0.75F;
    static constexpr float ROUNDING_EM = 0.3F;
    /** Flashes per second of the goal call. */
    static constexpr float FLASH_HZ = 4.0F;
    /** Gap below the sting as a share of the view height. */
    static constexpr float BOTTOM_SHARE = 0.08F;
    static constexpr ImU32 PANEL_COLOR = IM_COL32(8, 13, 23, 214);
    static constexpr ImU32 CALL_COLOR = IM_COL32(255, 214, 77, 255);
    static constexpr ImU32 TEXT_COLOR = IM_COL32(245, 247, 250, 255);
    static constexpr ImU32 DETAIL_COLOR = IM_COL32(184, 191, 204, 255);
  };

  /** Net ripple after a goal. */
  struct Ripple final
  {
    static constexpr float SECONDS = 2.2F;
    static constexpr float AMPLITUDE = 0.32F;
    static constexpr float DECAY = 2.2F;
    static constexpr float WAVE_NUMBER = 3.2F;
    static constexpr float WAVE_SPEED = 9.0F;
    /** Metres over which the push fades from the impact point. */
    static constexpr float REACH = 2.4F;
    static constexpr int SEGMENTS = 6;
  };

  struct Hud final
  {
    static constexpr float MARGIN = 12.0F;
    static constexpr float PADDING = 7.0F;
    static constexpr float CHIP_WIDTH = 6.0F;
    static constexpr float ROUNDING = 4.0F;
    static constexpr float LABEL_OFFSET = 6.0F;
    static constexpr float LABEL_PADDING = 3.0F;
    static constexpr float HOVER_PADDING = 6.0F;
    static constexpr ImU32 PANEL_COLOR = IM_COL32(8, 12, 24, 210);
    static constexpr ImU32 SCORE_PANEL_COLOR = IM_COL32(250, 250, 252, 240);
    static constexpr ImU32 SCORE_TEXT_COLOR = IM_COL32(12, 16, 28, 255);
    static constexpr ImU32 TEXT_COLOR = IM_COL32(245, 247, 250, 255);
    static constexpr ImU32 LABEL_BACK_COLOR = IM_COL32(6, 10, 20, 150);
    static constexpr ImU32 DEBUG_TARGET_COLOR = IM_COL32(255, 215, 64, 170);
  };

  struct Broadcast final
  {
    static constexpr float EYE_Y = -31.0F;
    static constexpr float EYE_HEIGHT = 21.0F;
    static constexpr float RAIL_FOLLOW = 0.55F;
    static constexpr float TARGET_WIDTH_FOLLOW = 0.5F;
    static constexpr float TARGET_MIN_X = 12.0F;
    static constexpr float TARGET_MAX_X = 93.0F;
    static constexpr float TARGET_MIN_Y = 14.0F;
    static constexpr float TARGET_MAX_Y = 54.0F;
    /** About 29 degrees: TV framing with a little more context. */
    static constexpr float FOV = 0.5F;
  };

  struct Tactical final
  {
    static constexpr float LENGTH_FOLLOW = 0.15F;
    static constexpr float TARGET_Y_OFFSET = 1.5F;
    static constexpr float PITCH = 1.08F;
    /** Far enough for the whole pitch in a 16:10 or 16:9 view. */
    static constexpr float DISTANCE = 114.0F;
    static constexpr float FOV = 0.66F;
  };

  struct End final
  {
    static constexpr float LEAD = 12.0F;
    static constexpr float WIDTH_FOLLOW = 0.6F;
    static constexpr float TARGET_MIN_X = 8.0F;
    static constexpr float TARGET_MAX_X = 97.0F;
    static constexpr float PITCH = 0.36F;
    static constexpr float DISTANCE = 40.0F;
    static constexpr float FOV = 0.78F;
  };

  /** User orbit camera: angles radians, distances metres. */
  struct Free final
  {
    static constexpr float DEFAULT_YAW = 1.5707963F;
    static constexpr float DEFAULT_PITCH = 0.5F;
    static constexpr float DEFAULT_DISTANCE = 78.0F;
    static constexpr float FOV = 0.52F;
    /** Pitch never dips below ~4 degrees nor goes past ~83 degrees. */
    static constexpr float MIN_PITCH = 0.07F;
    static constexpr float MAX_PITCH = 1.45F;
    static constexpr float MIN_DISTANCE = 6.0F;
    static constexpr float MAX_DISTANCE = 170.0F;
    /** The target stays over the pitch and its surrounds. */
    static constexpr float TARGET_MARGIN = 8.0F;
    static constexpr float MAX_TARGET_HEIGHT = 3.0F;
    /** A low eye stays within the stadium (the gantry rows included). */
    static constexpr float EYE_MIN_X = -34.0F;
    static constexpr float EYE_MAX_X = 139.0F;
    static constexpr float EYE_MIN_Y = -34.0F;
    static constexpr float EYE_MAX_Y = 102.0F;
    /** Orbit radians per logical pixel dragged. */
    static constexpr float ORBIT_RADIANS_PER_PIXEL = 0.0065F;
    /** How quickly the view catches up with the user's input (1/s). */
    static constexpr float RESPONSE_RATE = 14.0F;
  };

  /** TV director: holds, cooldowns (real seconds) and its extra shots. */
  struct Director final
  {
    static constexpr float MIN_SHOT_SECONDS = 3.0F;
    static constexpr float FINAL_THIRD_METRES = 35.0F;
    static constexpr float ATTACK_BUILD_SECONDS = 2.5F;
    static constexpr float REVERSE_HOLD = 5.0F;
    static constexpr float REVERSE_COOLDOWN = 25.0F;
    static constexpr float SHOT_RANGE_METRES = 38.0F;
    static constexpr float GOAL_LINE_HOLD = 2.2F;
    static constexpr float GOAL_LINE_COOLDOWN = 10.0F;
    static constexpr float GOAL_LINE_BACK = 1.5F;
    static constexpr float GOAL_LINE_SIDE = 15.0F;
    static constexpr float GOAL_LINE_HEIGHT = 2.6F;
    static constexpr float GOAL_LINE_BALL_SHARE = 0.4F;
    static constexpr float GOAL_LINE_FOV = 0.74F;
    static constexpr float CLOSE_UP_HEIGHT = 1.15F;
    static constexpr float CLOSE_UP_DISTANCE = 11.0F;
    static constexpr float CLOSE_UP_PITCH = 0.2F;
    static constexpr float CLOSE_UP_YAW_OFFSET = 0.3F;
    static constexpr float CLOSE_UP_FOV = 0.56F;
  };

  struct Follow final
  {
    static constexpr float TARGET_HEIGHT = 1.0F;
    static constexpr float YAW_RATE = 1.6F;
    static constexpr float PITCH = 0.3F;
    static constexpr float DISTANCE = 15.0F;
    static constexpr float FOV = 0.84F;
  };

  /** Play-mode camera: the ball and the active footballer in view. */
  struct Play final
  {
    static constexpr float PITCH = 0.6F;
    static constexpr float DISTANCE = 40.0F;
    /** Metres pulled back per metre between ball and active player. */
    static constexpr float SPREAD_DISTANCE_GAIN = 0.75F;
    static constexpr float MIN_DISTANCE = 34.0F;
    static constexpr float MAX_DISTANCE = 72.0F;
    static constexpr float LOOK_AHEAD_SHARE = 0.5F;
    static constexpr float TARGET_MIN_X = 10.0F;
    static constexpr float TARGET_MAX_X = 95.0F;
    static constexpr float TARGET_MIN_Y = 10.0F;
    static constexpr float TARGET_MAX_Y = 58.0F;
    static constexpr float FOV = 0.6F;
  };
};
