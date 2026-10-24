// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/render/match_camera_3d.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "gui/render/match_render_3d_tuning.h"

namespace
{
using RenderMath::Vec3;
using Tuning = MatchRender3DTuning;

constexpr float PITCH_LENGTH = MatchTuning::Pitch::LENGTH_METRES;
constexpr float PITCH_WIDTH = MatchTuning::Pitch::WIDTH_METRES;
constexpr float HALF_LENGTH = PITCH_LENGTH * 0.5f;
constexpr float HALF_WIDTH = PITCH_WIDTH * 0.5f;

Vec3 viewDirection(float yaw, float pitch)
{
  return {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw),
          -std::sin(pitch)};
}

Vec3 lookAhead(const MatchCameraFocus& focus)
{
  Vec3 ahead = focus.ballVelocity * Tuning::Camera::LOOKAHEAD_SECONDS;
  ahead.z = 0.0f;
  if (const float aheadLength = RenderMath::length(ahead);
      aheadLength > Tuning::Camera::MAX_LOOKAHEAD)
    ahead = ahead * (Tuning::Camera::MAX_LOOKAHEAD / aheadLength);
  return ahead;
}

/// The chase camera looks where the carrier runs, or along the attack when
/// the ball is loose.
float chaseHeading(const MatchCameraFocus& focus)
{
  if (focus.hasCarrier) return focus.carrierYaw;
  return focus.attackDirection >= 0.0f ? 0.0f : std::numbers::pi_v<float>;
}

/** Horizontal region a low camera eye must stay in. */
struct EyeBounds
{
  float minX = Tuning::Camera::EYE_MIN_X;
  float maxX = Tuning::Camera::EYE_MAX_X;
  float minY = Tuning::Camera::EYE_MIN_Y;
  float maxY = Tuning::Camera::EYE_MAX_Y;
};

/// Keeps a low eye inside the given bounds so no camera ends up inside a
/// stand: the camera first tilts down (keeping its distance), and only moves
/// closer once it is as steep as allowed. Eyes above the roofs are free.
void clampToBowl(Vec3 target, float yaw, float& pitch, float& distance,
                 const EyeBounds& bounds)
{
  const Vec3 flat{std::cos(yaw), std::sin(yaw), 0.0f};
  const auto horizontalReach = [&]()
  {
    float reach = distance * std::cos(pitch);
    const auto limit =
        [&](float origin, float step, float minimum, float maximum)
    {
      if (step > 1e-4f) reach = std::min(reach, (origin - minimum) / step);
      if (step < -1e-4f) reach = std::min(reach, (origin - maximum) / step);
    };
    limit(target.x, flat.x, bounds.minX, bounds.maxX);
    limit(target.y, flat.y, bounds.minY, bounds.maxY);
    return std::max(reach, 0.0f);
  };
  if (target.z + std::sin(pitch) * distance >=
      Tuning::Camera::STAND_CLEAR_HEIGHT)
    return;
  const float reach = horizontalReach();
  if (reach >= distance * std::cos(pitch) - 1e-4f) return;
  pitch = std::min(std::acos(std::clamp(reach / distance, 0.0f, 1.0f)),
                   Tuning::Camera::MAX_CLAMP_PITCH);
  distance = std::max(std::min(distance, reach / std::cos(pitch)), 1.0f);
}

Vec3 clampFreeTarget(Vec3 target)
{
  return {std::clamp(target.x, -Tuning::Free::TARGET_MARGIN,
                     PITCH_LENGTH + Tuning::Free::TARGET_MARGIN),
          std::clamp(target.y, -Tuning::Free::TARGET_MARGIN,
                     PITCH_WIDTH + Tuning::Free::TARGET_MARGIN),
          std::clamp(target.z, 0.0f, Tuning::Free::MAX_TARGET_HEIGHT)};
}
}  // namespace

MatchCamera3D::Rig MatchCamera3D::desiredRig(const MatchCameraFocus& focus,
                                             MatchCameraMode mode) const
{
  const Vec3 ahead = lookAhead(focus);
  Rig rig;
  switch (mode)
  {
    case MatchCameraMode::BROADCAST:
    {
      rig.target = {
          std::clamp(focus.ball.x + ahead.x, Tuning::Broadcast::TARGET_MIN_X,
                     Tuning::Broadcast::TARGET_MAX_X),
          std::clamp(HALF_WIDTH + (focus.ball.y + ahead.y - HALF_WIDTH) *
                                      Tuning::Broadcast::TARGET_WIDTH_FOLLOW,
                     Tuning::Broadcast::TARGET_MIN_Y,
                     Tuning::Broadcast::TARGET_MAX_Y),
          0.0f};
      // The gantry camera slides only part of the way along its rail and
      // pans for the rest, like a real main camera.
      const Vec3 eye{HALF_LENGTH + (rig.target.x - HALF_LENGTH) *
                                       Tuning::Broadcast::RAIL_FOLLOW,
                     Tuning::Broadcast::EYE_Y, Tuning::Broadcast::EYE_HEIGHT};
      const Vec3 offset = rig.target - eye;
      rig.distance = RenderMath::length(offset);
      rig.yaw = std::atan2(offset.y, offset.x);
      rig.pitch = std::asin(-offset.z / rig.distance);
      rig.fov = Tuning::Broadcast::FOV * zoomFactor;
      return rig;
    }
    case MatchCameraMode::TACTICAL:
      rig.target = {HALF_LENGTH + (focus.ball.x - HALF_LENGTH) *
                                      Tuning::Tactical::LENGTH_FOLLOW,
                    HALF_WIDTH + Tuning::Tactical::TARGET_Y_OFFSET, 0.0f};
      rig.yaw = std::numbers::pi_v<float> * 0.5f;
      rig.pitch = Tuning::Tactical::PITCH;
      rig.distance = Tuning::Tactical::DISTANCE * zoomFactor;
      rig.fov = Tuning::Tactical::FOV;
      break;
    case MatchCameraMode::END:
    {
      const float direction = focus.attackDirection >= 0.0f ? 1.0f : -1.0f;
      rig.target = {
          std::clamp(focus.ball.x + ahead.x + direction * Tuning::End::LEAD,
                     Tuning::End::TARGET_MIN_X, Tuning::End::TARGET_MAX_X),
          HALF_WIDTH + (focus.ball.y - HALF_WIDTH) * Tuning::End::WIDTH_FOLLOW,
          0.0f};
      rig.yaw = direction > 0.0f ? 0.0f : std::numbers::pi_v<float>;
      rig.pitch = Tuning::End::PITCH;
      rig.distance = Tuning::End::DISTANCE * zoomFactor;
      rig.fov = Tuning::End::FOV;
      break;
    }
    case MatchCameraMode::PLAYER_FOLLOW:
      rig.target = focus.hasCarrier ? focus.carrier : focus.ball;
      rig.target.z = Tuning::Follow::TARGET_HEIGHT;
      rig.yaw = chaseYaw;
      rig.pitch = Tuning::Follow::PITCH;
      rig.distance = Tuning::Follow::DISTANCE * zoomFactor;
      rig.fov = Tuning::Follow::FOV;
      break;
    case MatchCameraMode::FREE:
      rig = freeRig;
      break;
    case MatchCameraMode::DIRECTOR:
      return directorRig(focus);
    case MatchCameraMode::PLAY:
    {
      // Side-on like the main camera but closer and lower, centred between
      // the ball and the active footballer and pulled back as they drift
      // apart, so both stay in the picture.
      const Vec3 subject = focus.hasActive ? focus.active : focus.ball;
      const Vec3 between = subject + (focus.ball - subject) * 0.5f;
      const float spread =
          std::hypot(focus.ball.x - subject.x, focus.ball.y - subject.y);
      rig.target = {
          std::clamp(between.x + ahead.x * Tuning::Play::LOOK_AHEAD_SHARE,
                     Tuning::Play::TARGET_MIN_X, Tuning::Play::TARGET_MAX_X),
          std::clamp(between.y, Tuning::Play::TARGET_MIN_Y,
                     Tuning::Play::TARGET_MAX_Y),
          0.0f};
      rig.yaw = std::numbers::pi_v<float> * 0.5f;
      rig.pitch = Tuning::Play::PITCH;
      rig.distance =
          std::clamp(Tuning::Play::DISTANCE +
                         spread * Tuning::Play::SPREAD_DISTANCE_GAIN,
                     Tuning::Play::MIN_DISTANCE, Tuning::Play::MAX_DISTANCE) *
          zoomFactor;
      rig.fov = Tuning::Play::FOV;
      break;
    }
  }
  // The free camera may also use the gantry positions of the presets (the
  // stand behind the eye is not drawn), so taking over never jumps.
  const EyeBounds bounds =
      mode == MatchCameraMode::FREE
          ? EyeBounds{Tuning::Free::EYE_MIN_X, Tuning::Free::EYE_MAX_X,
                      Tuning::Free::EYE_MIN_Y, Tuning::Free::EYE_MAX_Y}
          : EyeBounds{};
  clampToBowl(rig.target, rig.yaw, rig.pitch, rig.distance, bounds);
  return rig;
}

MatchCamera3D::Rig MatchCamera3D::directorRig(
    const MatchCameraFocus& focus) const
{
  using D = Tuning::Director;
  using enum MatchDirectorShot;
  const auto aimFrom = [](Vec3 eye, Vec3 target, float fov)
  {
    Rig rig;
    rig.target = target;
    const Vec3 offset = target - eye;
    rig.distance = std::max(RenderMath::length(offset), 1.0f);
    rig.yaw = std::atan2(offset.y, offset.x);
    rig.pitch = std::asin(std::clamp(-offset.z / rig.distance, -1.0f, 1.0f));
    rig.fov = fov;
    return rig;
  };
  switch (shot)
  {
    case BROADCAST:
      break;
    case REVERSE_ANGLE:
    {
      // The main camera's framing mirrored into the opposite stand.
      Rig rig = desiredRig(focus, MatchCameraMode::BROADCAST);
      const Vec3 eye{HALF_LENGTH + (rig.target.x - HALF_LENGTH) *
                                       Tuning::Broadcast::RAIL_FOLLOW,
                     PITCH_WIDTH - Tuning::Broadcast::EYE_Y,
                     Tuning::Broadcast::EYE_HEIGHT};
      return aimFrom(eye, rig.target, rig.fov);
    }
    case CLOSE_UP:
    {
      // Tight and low from the main camera's side of the pitch.
      Rig rig;
      rig.target = focus.hasCelebration ? focus.celebration : focus.ball;
      rig.target.z = D::CLOSE_UP_HEIGHT;
      rig.yaw = std::numbers::pi_v<float> * 0.5f +
                (rig.target.x < HALF_LENGTH ? D::CLOSE_UP_YAW_OFFSET
                                            : -D::CLOSE_UP_YAW_OFFSET);
      rig.pitch = D::CLOSE_UP_PITCH;
      rig.distance = D::CLOSE_UP_DISTANCE * zoomFactor;
      rig.fov = D::CLOSE_UP_FOV;
      clampToBowl(rig.target, rig.yaw, rig.pitch, rig.distance, EyeBounds{});
      return rig;
    }
    case GOAL_LINE:
    {
      // Low on the goal line beside the posts, looking out at the shooter.
      const float outward = goalLineX < HALF_LENGTH ? -1.0f : 1.0f;
      const Vec3 eye{goalLineX + outward * D::GOAL_LINE_BACK,
                     HALF_WIDTH - D::GOAL_LINE_SIDE, D::GOAL_LINE_HEIGHT};
      const Vec3 goalMouth{goalLineX, HALF_WIDTH, 1.0f};
      Vec3 target =
          RenderMath::lerp(goalMouth, focus.ball, D::GOAL_LINE_BALL_SHARE);
      target.z = 1.0f;
      return aimFrom(eye, target, D::GOAL_LINE_FOV);
    }
  }
  return desiredRig(focus, MatchCameraMode::BROADCAST);
}

bool MatchCamera3D::direct(const MatchCameraFocus& focus, float deltaSeconds)
{
  using D = Tuning::Director;
  shotSeconds += deltaSeconds;
  sinceReverse += deltaSeconds;
  sinceGoalLine += deltaSeconds;
  // An attack building in the final third earns a reverse angle.
  const float towardsGoal = focus.attackDirection >= 0.0f
                                ? PITCH_LENGTH - focus.ball.x
                                : focus.ball.x;
  const bool finalThird = towardsGoal < D::FINAL_THIRD_METRES;
  attackSeconds = focus.livePlay && focus.hasCarrier && finalThird
                      ? attackSeconds + deltaSeconds
                      : 0.0f;

  MatchDirectorShot next = shot;
  if (focus.reducedMotion)
  {
    next = MatchDirectorShot::BROADCAST;
  }
  else if (focus.goalCelebration)
  {
    next = MatchDirectorShot::CLOSE_UP;
  }
  else if (shot == MatchDirectorShot::CLOSE_UP)
  {
    next = MatchDirectorShot::BROADCAST;
  }
  else if (focus.shotInFlight && shot != MatchDirectorShot::GOAL_LINE &&
           sinceGoalLine >= D::GOAL_LINE_COOLDOWN)
  {
    // The goal the ball is flying at, when it is close enough to matter.
    const float heading = focus.ballVelocity.x;
    float goal = 0.0f;
    if (std::abs(heading) > 1.0f)
    {
      if (heading > 0.0f) goal = PITCH_LENGTH;
    }
    else if (focus.attackDirection >= 0.0f)
    {
      goal = PITCH_LENGTH;
    }
    if (std::abs(goal - focus.ball.x) < D::SHOT_RANGE_METRES)
    {
      next = MatchDirectorShot::GOAL_LINE;
      goalLineX = goal;
    }
  }
  else if (shot == MatchDirectorShot::GOAL_LINE)
  {
    if (!focus.shotInFlight && shotSeconds >= D::GOAL_LINE_HOLD)
      next = MatchDirectorShot::BROADCAST;
  }
  else if (shot == MatchDirectorShot::REVERSE_ANGLE)
  {
    if (shotSeconds >= D::REVERSE_HOLD ||
        (!finalThird && shotSeconds >= D::MIN_SHOT_SECONDS))
      next = MatchDirectorShot::BROADCAST;
  }
  else if (shotSeconds >= D::MIN_SHOT_SECONDS &&
           sinceReverse >= D::REVERSE_COOLDOWN &&
           attackSeconds >= D::ATTACK_BUILD_SECONDS)
  {
    next = MatchDirectorShot::REVERSE_ANGLE;
  }
  if (next == shot) return false;
  shot = next;
  shotSeconds = 0.0f;
  if (next == MatchDirectorShot::REVERSE_ANGLE) sinceReverse = 0.0f;
  if (next == MatchDirectorShot::GOAL_LINE) sinceGoalLine = 0.0f;
  ++cuts;
  return true;
}

void MatchCamera3D::steerFree(const MatchCameraFocus& focus,
                              const MatchCameraControl& control)
{
  using F = Tuning::Free;
  if (!freeActive || control.reset)
  {
    // Taking over keeps the pose on screen; a reset (or a free camera
    // selected before any other) starts from the default overview.
    if (!initialized || control.reset)
    {
      freeRig.target =
          control.followBall ? focus.ball : Vec3{HALF_LENGTH, HALF_WIDTH, 0.0f};
      freeRig.yaw = F::DEFAULT_YAW;
      freeRig.pitch = F::DEFAULT_PITCH;
      freeRig.distance = F::DEFAULT_DISTANCE;
      freeRig.fov = F::FOV;
    }
    else
    {
      freeRig = current;
    }
    freeActive = true;
  }

  if (control.zoomSteps != 0.0f)
    freeRig.distance *= std::pow(Tuning::Camera::ZOOM_STEP, control.zoomSteps);
  freeRig.yaw = RenderMath::wrapAngle(freeRig.yaw + control.orbitYaw);
  freeRig.pitch += control.orbitPitch;
  if (control.retarget)
    freeRig.target = control.retargetPoint;
  else if (control.followBall)
    freeRig.target = focus.ball;
  else
    freeRig.target = freeRig.target + control.pan;

  freeRig.target = clampFreeTarget(freeRig.target);
  freeRig.distance =
      std::clamp(freeRig.distance, F::MIN_DISTANCE, F::MAX_DISTANCE);
  // Never under the pitch: the eye stays above the minimum eye height.
  const float lowest = std::asin(std::clamp(
      Tuning::Camera::MIN_EYE_HEIGHT / freeRig.distance, 0.0f, 1.0f));
  freeRig.pitch =
      std::clamp(freeRig.pitch, std::max(F::MIN_PITCH, lowest), F::MAX_PITCH);
}

void MatchCamera3D::snap(const MatchCameraFocus& focus, MatchCameraMode mode)
{
  chaseYaw = chaseHeading(focus);
  if (mode == MatchCameraMode::FREE)
    steerFree(focus, {});
  else
    freeActive = false;
  current = desiredRig(focus, mode);
  initialized = true;
}

void MatchCamera3D::update(const MatchCameraFocus& focus, MatchCameraMode mode,
                           const MatchCameraControl& control,
                           float deltaSeconds)
{
  if (mode != MatchCameraMode::FREE)
  {
    freeActive = false;
    if (control.zoomSteps != 0.0f)
    {
      zoomFactor = std::clamp(
          zoomFactor * std::pow(Tuning::Camera::ZOOM_STEP, control.zoomSteps),
          Tuning::Camera::MIN_ZOOM, Tuning::Camera::MAX_ZOOM);
    }
  }
  const float dt =
      std::clamp(deltaSeconds, 0.0f, Tuning::Camera::MAX_FRAME_SECONDS);
  if (mode == MatchCameraMode::DIRECTOR)
  {
    // A cut jumps straight to the new shot, like a vision mixer.
    if (direct(focus, dt) && initialized)
    {
      chaseYaw = chaseHeading(focus);
      current = desiredRig(focus, mode);
      return;
    }
  }
  else
  {
    shot = MatchDirectorShot::BROADCAST;
    shotSeconds = 0.0f;
    attackSeconds = 0.0f;
  }
  if (!initialized)
  {
    snap(focus, mode);
    return;
  }
  if (mode == MatchCameraMode::FREE) steerFree(focus, control);

  chaseYaw = RenderMath::lerpAngle(
      chaseYaw, chaseHeading(focus),
      RenderMath::dampingFactor(Tuning::Follow::YAW_RATE, dt));

  const Rig desired = desiredRig(focus, mode);
  const bool freeMode = mode == MatchCameraMode::FREE;
  const auto blend = [freeMode, dt](float presetRate)
  {
    return RenderMath::dampingFactor(
        freeMode ? Tuning::Free::RESPONSE_RATE : presetRate, dt);
  };
  const float targetBlend = blend(Tuning::Camera::TARGET_RATE);
  const float angleBlend = blend(Tuning::Camera::ANGLE_RATE);
  current.target =
      RenderMath::lerp(current.target, desired.target, targetBlend);
  current.yaw = RenderMath::wrapAngle(
      RenderMath::lerpAngle(current.yaw, desired.yaw, angleBlend));
  current.pitch += (desired.pitch - current.pitch) * angleBlend;
  current.distance += (desired.distance - current.distance) *
                      blend(Tuning::Camera::DISTANCE_RATE);
  current.fov += (desired.fov - current.fov) * blend(Tuning::Camera::FOV_RATE);
}

Vec3 MatchCamera3D::eye() const
{
  Vec3 position = current.target -
                  viewDirection(current.yaw, current.pitch) * current.distance;
  position.z = std::max(position.z, Tuning::Camera::MIN_EYE_HEIGHT);
  return position;
}
