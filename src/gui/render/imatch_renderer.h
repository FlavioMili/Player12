// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>

#include "gui/render/match_render_snapshot.h"

/** Screen-space rectangle the renderer draws the pitch into. */
struct MatchViewport
{
  float x = 0.0f;
  float y = 0.0f;
  float width = 800.0f;
  float height = 500.0f;
};

/** Camera presets offered by the 3D broadcast renderer. */
enum class MatchCameraMode : std::uint8_t
{
  BROADCAST,     /**< Elevated side-on TV camera tracking the ball. */
  TACTICAL,      /**< High, near top-down view of the whole pitch. */
  END,           /**< Behind the play, looking along the attack. */
  PLAYER_FOLLOW, /**< Low chase camera behind the ball carrier. */
  FREE,          /**< User-driven orbit camera (drag, pan, zoom). */
  DIRECTOR,      /**< TV director cutting between broadcast shots. */
  PLAY,          /**< Play mode: follows the ball and the active player. */
};

/**
 * Mouse input for the 3D camera gathered over the view since the previous
 * frame. Deltas are logical pixels; the renderer turns them into orbit
 * angles and ground movement with the projection the user was looking at.
 */
struct MatchCameraInput
{
  /** Mouse-wheel steps (positive zooms in). */
  float zoomSteps = 0.0f;
  /** Orbit drag in pixels (x turns around the target, y tilts). */
  float orbitX = 0.0f;
  float orbitY = 0.0f;
  /** Pan drag: the ground under `panFrom` moves to `panTo` (pixels). */
  bool pan = false;
  float panFromX = 0.0f;
  float panFromY = 0.0f;
  float panToX = 0.0f;
  float panToY = 0.0f;
  /** Re-target the free camera on the ground point under this pixel. */
  bool retarget = false;
  float retargetX = 0.0f;
  float retargetY = 0.0f;
  /** Free camera orbits the moving ball, keeping the user's angle. */
  bool followBall = false;
  /** Returns the free camera to its default overview. */
  bool reset = false;
};

/** Presentation-only switches passed to a renderer. */
struct MatchRenderOptions
{
  bool showAiDebug = false;
  /** Always label every player instead of only the ball carrier. */
  bool showPlayerNames = false;
  /** Real (unscaled) seconds since the previous rendered frame. */
  float frameSeconds = 0.0f;
  /** Camera mouse input over the viewport since the previous frame. */
  MatchCameraInput cameraInput;
  MatchCameraMode cameraMode = MatchCameraMode::BROADCAST;
  /** Short team labels for the in-view score bug; may be null. */
  const char* homeLabel = nullptr;
  const char* awayLabel = nullptr;
  /** Kick-off in minutes after midnight (venue time); -1 when unknown. */
  int kickoffMinutes = -1;
  /** Daylight presentation regardless of the kick-off time (3D view). */
  bool dayLook = false;
  /** Pitch-control overlay in the team colours (2D view). */
  bool pressureOverlay = false;
  /** Play mode: the human's active footballer (0 = none), kept in view by
   * the play camera. */
  PlayerID activePlayer = 0;
  /** No player figures or their shadows (3D view): the sandbox's true view
   * draws the engine's shapes in their place. */
  bool hidePlayerFigures = false;
};

/**
 * Renderer-independent match presentation boundary.
 *
 * Implementations consume a read-only `MatchRenderSnapshot`, must never call
 * RNG or mutate the engine, and are free to interpolate between the previous
 * and current fixed-step positions using `snapshot.interpolationAlpha`.
 */
class IMatchRenderer
{
 public:
  virtual ~IMatchRenderer() = default;

  virtual void render(const MatchRenderSnapshot& snapshot,
                      const MatchRenderOptions& options,
                      const MatchViewport& viewport) = 0;

  /**
   * Screen position (logical pixels) of a pitch point (metres) at a height
   * in metres, as drawn by the last render(), so overlays can be drawn in
   * the view's projection. False before the first frame or when the point
   * is behind the camera.
   */
  virtual bool projectPitch(PitchPoint pitch, float heightMetres, float& screenX,
                            float& screenY) const = 0;
};
