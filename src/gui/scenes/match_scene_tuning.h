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

/** Named tuning values of the live match scene and its 2D presentation. */
struct MatchSceneTuning final
{
  static constexpr float CELEBRATION_RADIANS_PER_PERIOD = 6.2831853F;

  /** Unscaled HUD sizes; multiplied by Theme::scale() at use. */
  struct Scoreboard final
  {
    static constexpr float HEIGHT = 100.0F;
    static constexpr float TRACK_HEIGHT = 4.0F;
    static constexpr float TIMELINE_MARKER_SIZE = 9.0F;
    static constexpr float NAME_GAP = 22.0F;
    static constexpr float KIT_SWATCH_SIZE = 12.0F;
    static constexpr float PERCENT_SCALE = 100.0F;
  };

  struct Controls final
  {
    static constexpr float PAUSE_BUTTON_WIDTH = 120.0F;
    static constexpr float SUBSTITUTION_BUTTON_WIDTH = 170.0F;
    static constexpr float DEBUG_BUTTON_WIDTH = 150.0F;
    static constexpr float AI_DEBUG_BUTTON_WIDTH = 130.0F;
    /** Playback speeds offered: simulated seconds per real second (1x is
     * real time, the engine allows up to 30x). */
    static constexpr std::array<float, 6> SPEED_STEPS = {1.0F, 2.0F,  4.0F,
                                                         8.0F, 16.0F, 30.0F};
    static constexpr float SPEED_BUTTON_GAP = 2.0F;
    static constexpr float SKIP_INDICATOR_SECONDS = 1.5F;
    /** Longest frame time played right after a highlight skip. */
    static constexpr float MAX_DELTA_AFTER_SKIP = 1.0F / 30.0F;
    static constexpr float VIEW_BUTTON_WIDTH = 130.0F;
  };

  struct Panel final
  {
    /** Statistics and events sit beside the pitch from this content width. */
    static constexpr float SIDE_PANEL_MIN_CONTENT_WIDTH = 980.0F;
    static constexpr float SIDE_PANEL_WIDTH_RATIO = 0.27F;
    static constexpr float SIDE_PANEL_MIN_WIDTH = 290.0F;
    static constexpr float SIDE_PANEL_MAX_WIDTH = 460.0F;
    static constexpr float STATISTICS_HEIGHT_RATIO = 0.5F;
    /** A sandbox match's panel: wider (its debug log), always beside the
     * pitch, its statistics and events at fixed heights in a scrolling
     * column. */
    static constexpr float SANDBOX_PANEL_WIDTH_RATIO = 0.36F;
    static constexpr float SANDBOX_PANEL_MAX_WIDTH = 720.0F;
    static constexpr float SANDBOX_STATISTICS_HEIGHT = 300.0F;
    static constexpr float SANDBOX_EVENTS_HEIGHT = 260.0F;
    static constexpr float MINUTE_COLUMN_WIDTH = 52.0F;
    static constexpr float EVENT_ICON_SIZE = 14.0F;
    static constexpr float LINEUP_GATE_WIDTH = 640.0F;
    static constexpr float LINEUP_GATE_TOP_RATIO = 0.15F;
  };

  struct View final
  {
    static constexpr float MIN_HEIGHT = 240.0F;
    static constexpr float RENDER_TIME_SMOOTHING = 0.1F;
  };

  struct Pitch final
  {
    static constexpr float WIDTH = 800.0F;
    static constexpr float HEIGHT = 500.0F;
    static constexpr float MIN_SCALE = 0.5F;
    /** Lets the pitch fill large (1440p/4K) match views. */
    static constexpr float MAX_SCALE = 3.0F;
    static constexpr float CENTRE_RATIO = 0.5F;
    static constexpr float LINE_THICKNESS = 2.0F;
    static constexpr float RECTANGLE_ROUNDING = 0.0F;
    static constexpr float CENTRE_CIRCLE_RADIUS_RATIO = 0.15F;
    static constexpr int CENTRE_CIRCLE_SEGMENTS = 32;
    static constexpr float CENTRE_SPOT_RADIUS = 4.0F;
    static constexpr float PENALTY_BOX_WIDTH_RATIO = 0.15F;
    static constexpr float PENALTY_BOX_HEIGHT_RATIO = 0.5F;
    static constexpr float GOAL_BOX_WIDTH_RATIO = 0.05F;
    static constexpr float GOAL_BOX_HEIGHT_RATIO = 0.25F;
    static constexpr float CORNER_RADIUS_RATIO = 0.03F;
    static constexpr int CORNER_ARC_SEGMENTS = 10;
    static constexpr float BOTTOM_RIGHT_ARC_END_MULTIPLIER = 1.5F;
    static constexpr int MOWING_STRIPE_COUNT = 10;
    static constexpr int MOWING_COLOR_PERIOD = 2;
    static constexpr float GOAL_DEPTH = 15.0F;
    static constexpr float GOAL_WIDTH_RATIO = 0.12F;
    static constexpr float PENALTY_SPOT_RADIUS = 3.0F;
    static constexpr float PENALTY_SPOT_X_RATIO = 0.115F;
    static constexpr ImU32 GRASS_COLOR = IM_COL32(34, 139, 34, 255);
    static constexpr ImU32 ALTERNATE_GRASS_COLOR = IM_COL32(30, 126, 32, 255);
    static constexpr ImU32 LINE_COLOR = IM_COL32(255, 255, 255, 200);
    static constexpr ImU32 GOAL_COLOR = IM_COL32(225, 230, 235, 230);
  };

  struct Stadium final
  {
    static constexpr float STAND_HEIGHT = 22.0F;
    static constexpr float ROOF_HEIGHT = 8.0F;
    static constexpr int CROWD_SEED = 20260816;
    static constexpr float CROWD_DOT_RADIUS = 2.2F;
    static constexpr float CROWD_DOT_STEP = 7.0F;
    static constexpr ImU32 SURROUND_COLOR = IM_COL32(24, 72, 28, 255);
    static constexpr ImU32 APRON_COLOR = IM_COL32(58, 104, 58, 255);
    static constexpr ImU32 STAND_COLOR = IM_COL32(38, 48, 66, 255);
    static constexpr ImU32 STAND_SHADOW_COLOR = IM_COL32(20, 27, 40, 160);
    static constexpr ImU32 ROOF_COLOR = IM_COL32(70, 74, 84, 255);
    static constexpr ImU32 CROWD_A_COLOR = IM_COL32(240, 200, 90, 230);
    static constexpr ImU32 CROWD_B_COLOR = IM_COL32(90, 150, 235, 230);
    static constexpr ImU32 CROWD_C_COLOR = IM_COL32(235, 235, 240, 225);
    static constexpr int CROWD_COLOR_COUNT = 3;
  };

  struct GoalFrame final
  {
    static constexpr float POST_THICKNESS = 5.0F;
    static constexpr float NET_LINE_THICKNESS = 1.0F;
    static constexpr int NET_HORIZONTAL_LINES = 3;
    static constexpr int NET_VERTICAL_LINES = 5;
    static constexpr float NET_MESH_ALPHA = 66;
    static constexpr float GOAL_GLOW_RADIUS = 30.0F;
    static constexpr ImU32 POST_COLOR = IM_COL32(250, 250, 250, 255);
    static constexpr ImU32 NET_COLOR = IM_COL32(235, 240, 245, 66);
    static constexpr ImU32 NET_GLOW_COLOR = IM_COL32(120, 255, 190, 90);
  };

  struct Marker final
  {
    static constexpr float PLAYER_RADIUS = 8.0F;
    static constexpr float PLAYER_OUTLINE_RADIUS = 9.5F;
    static constexpr float PLAYER_SHADOW_RADIUS = 9.0F;
    static constexpr float PLAYER_SHADOW_OFFSET = 2.0F;
    static constexpr float POSSESSION_RING_RADIUS = 12.0F;
    static constexpr float POSSESSION_RING_THICKNESS = 2.5F;
    static constexpr float DIRECTION_LENGTH = 14.0F;
    static constexpr float DIRECTION_THICKNESS = 2.0F;
    static constexpr float HOVER_RADIUS = 14.0F;
    static constexpr float LABEL_X_OFFSET = 10.0F;
    static constexpr float LABEL_Y_OFFSET = -5.0F;
    static constexpr float BALL_RADIUS = 4.0F;
    static constexpr float BALL_HEIGHT_SCALE = 0.9F;
    static constexpr float BALL_SHADOW_SCALE = 0.6F;
    static constexpr ImU32 BALL_SHADOW_COLOR = IM_COL32(0, 0, 0, 70);
    static constexpr ImU32 DIRECTION_COLOR = IM_COL32(255, 255, 100, 255);
    static constexpr ImU32 LABEL_COLOR = IM_COL32(255, 255, 255, 255);
    static constexpr ImU32 BALL_COLOR = IM_COL32(255, 255, 255, 255);
    static constexpr ImU32 PLAYER_OUTLINE_COLOR = IM_COL32(245, 245, 245, 230);
    static constexpr ImU32 PLAYER_SHADOW_COLOR = IM_COL32(0, 0, 0, 90);
    static constexpr ImU32 POSSESSION_RING_COLOR = IM_COL32(255, 215, 64, 255);
    static constexpr ImU32 DEBUG_TARGET_COLOR = IM_COL32(255, 215, 64, 170);
    static constexpr ImU32 DEBUG_RUN_COLOR = IM_COL32(80, 230, 150, 210);
    static constexpr ImU32 DEBUG_PRESS_COLOR = IM_COL32(255, 110, 90, 210);
    static constexpr ImU32 DEBUG_SUPPORT_COLOR = IM_COL32(90, 180, 255, 190);
    static constexpr float DEBUG_TARGET_RADIUS = 3.0F;
    static constexpr float DEBUG_LINE_THICKNESS = 1.5F;
  };

  struct Events final
  {
    static constexpr float PANEL_HEIGHT = 170.0F;
    static constexpr float LATEST_SCROLL_RATIO = 1.0F;
  };

  struct Celebration final
  {
    static constexpr float BANNER_SCALE = 2.6F;
    static constexpr float SCORE_SCALE = 1.4F;
    static constexpr float SCORE_OFFSET_RATIO = 0.4F;
    static constexpr float FLASH_MIN_ALPHA = 34;
    static constexpr float FLASH_MAX_ALPHA = 120;
    static constexpr float PULSE_PERIOD_SECONDS = 0.45F;
    static constexpr ImU32 BANNER_TEXT_COLOR = IM_COL32(255, 255, 80, 255);
    static constexpr ImU32 SCORE_TEXT_COLOR = IM_COL32(255, 255, 255, 255);
    static constexpr ImU32 FLASH_COLOR = IM_COL32(255, 255, 150, 34);
  };

  struct Performance final
  {
    static constexpr float SLOW_SCENE_ENTRY_MILLISECONDS = 100.0F;
    static constexpr float SLOW_UPDATE_MILLISECONDS = 12.0F;
  };

  struct Substitutions final
  {
    static constexpr float MODAL_WIDTH = 820.0F;
    static constexpr float MODAL_HEIGHT = 700.0F;
    static constexpr float MODAL_VIEWPORT_FRACTION = 0.94F;
    static constexpr float ACTION_BUTTON_WIDTH = 190.0F;
  };
};
