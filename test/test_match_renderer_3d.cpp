// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include "controller/game_controller.h"
#include "database/datagenerator.h"
#include "global/language_manager.h"
#include "global/logger.h"
#include "global/paths.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/render/match_camera_3d.h"
#include "gui/render/match_kit_colors.h"
#include "gui/render/match_player_rig.h"
#include "gui/render/match_render_3d_tuning.h"
#include "gui/render/match_render_math.h"
#include "gui/render/match_renderer_2d.h"
#include "gui/render/match_renderer_3d.h"
#include "gui/render/match_stadium_3d.h"
#include "gui/scenes/match_scene.h"
#include "gui/widgets/theme.h"

namespace
{
using RenderMath::Projection;
using RenderMath::ScreenPoint;
using RenderMath::Vec3;
using RenderMath::Vec4;

constexpr RenderMath::ScreenRect TEST_RECT{100.0f, 50.0f, 800.0f, 400.0f};
constexpr float TEST_FOV = 0.8f;
constexpr float TEST_NEAR = 0.5f;

Projection testProjection()
{
  return Projection::make({0.0f, -50.0f, 20.0f}, {10.0f, 5.0f, 0.0f}, TEST_FOV,
                          TEST_NEAR, 500.0f, TEST_RECT);
}

MatchCameraFocus focusAt(float x, float y)
{
  MatchCameraFocus focus;
  focus.ball = {x, y, 0.0f};
  focus.carrier = focus.ball;
  focus.hasCarrier = true;
  focus.carrierYaw = 0.3f;
  return focus;
}
}  // namespace

TEST(MatchRender3DMath, CameraTargetProjectsToViewportCentre)
{
  const Projection projection = testProjection();
  ScreenPoint point;
  ASSERT_TRUE(projection.project({10.0f, 5.0f, 0.0f}, point));
  EXPECT_NEAR(point.x, TEST_RECT.x + TEST_RECT.width * 0.5f, 1e-3f);
  EXPECT_NEAR(point.y, TEST_RECT.y + TEST_RECT.height * 0.5f, 1e-3f);
  EXPECT_NEAR(point.depth, RenderMath::length(Vec3{10.0f, 55.0f, -20.0f}),
              1e-3f);
}

TEST(MatchRender3DMath, AxesProjectWithScreenConventions)
{
  const Projection projection = testProjection();
  ScreenPoint centre;
  ScreenPoint right;
  ScreenPoint above;
  ASSERT_TRUE(projection.project({10.0f, 5.0f, 0.0f}, centre));
  // The camera looks roughly along +y, so +x is to the right on screen.
  ASSERT_TRUE(projection.project({15.0f, 5.0f, 0.0f}, right));
  ASSERT_TRUE(projection.project({10.0f, 5.0f, 5.0f}, above));
  EXPECT_GT(right.x, centre.x);
  EXPECT_LT(above.y, centre.y);
}

TEST(MatchRender3DMath, PointsBehindCameraAreRejected)
{
  const Projection projection = testProjection();
  ScreenPoint point;
  EXPECT_FALSE(projection.project({0.0f, -80.0f, 20.0f}, point));
  EXPECT_FALSE(projection.project(projection.eye, point));
}

TEST(MatchRender3DMath, NearPlaneClippingKeepsOnlyVisiblePart)
{
  const Projection projection = testProjection();
  // A ground quad spanning from behind the camera to in front of it.
  const std::array<Vec4, 4> straddling{projection.toClip({-5.0f, -70.0f, 0.0f}),
                                       projection.toClip({5.0f, -70.0f, 0.0f}),
                                       projection.toClip({5.0f, 10.0f, 0.0f}),
                                       projection.toClip({-5.0f, 10.0f, 0.0f})};
  ASSERT_LT(straddling[0].w, TEST_NEAR);
  ASSERT_GT(straddling[2].w, TEST_NEAR);
  std::array<Vec4, 5> clipped{};
  const std::size_t count =
      RenderMath::clipPolygonToNearPlane(straddling, clipped, TEST_NEAR);
  EXPECT_EQ(count, 4u);
  for (std::size_t index = 0; index < count; ++index)
    EXPECT_GE(clipped[index].w, TEST_NEAR - 1e-4f);

  const std::array<Vec4, 3> behind{projection.toClip({0.0f, -60.0f, 0.0f}),
                                   projection.toClip({2.0f, -60.0f, 0.0f}),
                                   projection.toClip({1.0f, -65.0f, 0.0f})};
  EXPECT_EQ(RenderMath::clipPolygonToNearPlane(behind, clipped, TEST_NEAR), 0u);

  const std::array<Vec4, 3> inFront{projection.toClip({0.0f, 0.0f, 0.0f}),
                                    projection.toClip({2.0f, 0.0f, 0.0f}),
                                    projection.toClip({1.0f, 5.0f, 0.0f})};
  EXPECT_EQ(RenderMath::clipPolygonToNearPlane(inFront, clipped, TEST_NEAR),
            3u);
}

TEST(MatchRender3DMath, PitchMappingAndAngles)
{
  const Vec3 origin = RenderMath::worldFromPitch({0.0f, 0.0f});
  const Vec3 corner = RenderMath::worldFromPitch({1.0f, 1.0f});
  EXPECT_FLOAT_EQ(origin.x, 0.0f);
  EXPECT_FLOAT_EQ(origin.y, MatchTuning::Pitch::WIDTH_METRES);
  EXPECT_FLOAT_EQ(corner.x, MatchTuning::Pitch::LENGTH_METRES);
  EXPECT_FLOAT_EQ(corner.y, 0.0f);

  EXPECT_NEAR(RenderMath::worldYawFromFacing(0.0f), 0.0f, 1e-5f);
  EXPECT_NEAR(RenderMath::worldYawFromFacing(std::numbers::pi_v<float> * 0.5f),
              -std::numbers::pi_v<float> * 0.5f, 1e-5f);
  EXPECT_NEAR(std::abs(RenderMath::lerpAngle(3.0f, -3.0f, 0.5f)),
              std::numbers::pi_v<float>, 1e-4f);
}

TEST(MatchCamera3DTest, DampingIsFrameRateIndependent)
{
  using enum MatchCameraMode;
  for (const MatchCameraMode mode : {BROADCAST, TACTICAL, END})
  {
    MatchCamera3D coarse;
    MatchCamera3D fine;
    coarse.snap(focusAt(20.0f, 20.0f), mode);
    fine.snap(focusAt(20.0f, 20.0f), mode);
    const MatchCameraFocus moved = focusAt(70.0f, 50.0f);
    coarse.update(moved, mode, {}, 1.0f / 30.0f);
    fine.update(moved, mode, {}, 1.0f / 60.0f);
    fine.update(moved, mode, {}, 1.0f / 60.0f);
    EXPECT_NEAR(coarse.eye().x, fine.eye().x, 1e-3f);
    EXPECT_NEAR(coarse.eye().y, fine.eye().y, 1e-3f);
    EXPECT_NEAR(coarse.eye().z, fine.eye().z, 1e-3f);
  }
}

TEST(MatchCamera3DTest, DirectorCutsForGoalsShotsAndAttacks)
{
  using D = MatchRender3DTuning::Director;
  constexpr float STEP = 1.0f / 60.0f;
  MatchCamera3D camera;
  MatchCameraFocus focus = focusAt(52.5f, 34.0f);
  focus.livePlay = true;
  camera.snap(focus, MatchCameraMode::DIRECTOR);
  const auto run = [&](const MatchCameraFocus& at, float seconds)
  {
    for (int step = 0; static_cast<float>(step) * STEP < seconds; ++step)
      camera.update(at, MatchCameraMode::DIRECTOR, {}, STEP);
  };
  run(focus, 5.0f);
  EXPECT_EQ(camera.directorShot(), MatchDirectorShot::BROADCAST);
  EXPECT_EQ(camera.directorCuts(), 0);

  // A goal cuts (not glides) straight to the close-up of the scorer.
  MatchCameraFocus goal = focus;
  goal.goalCelebration = true;
  goal.celebration = {100.0f, 60.0f, 0.0f};
  goal.hasCelebration = true;
  camera.update(goal, MatchCameraMode::DIRECTOR, {}, STEP);
  EXPECT_EQ(camera.directorShot(), MatchDirectorShot::CLOSE_UP);
  EXPECT_NEAR(camera.target().x, 100.0f, 0.5f);
  EXPECT_LT(camera.distance(), D::CLOSE_UP_DISTANCE + 0.5f);
  run(goal, 3.0f);
  EXPECT_EQ(camera.directorShot(), MatchDirectorShot::CLOSE_UP);
  // Once the celebration is over the main camera is back.
  camera.update(focus, MatchCameraMode::DIRECTOR, {}, STEP);
  EXPECT_EQ(camera.directorShot(), MatchDirectorShot::BROADCAST);
  EXPECT_EQ(camera.directorCuts(), 2);

  // A shot at goal: the goal-line camera, held briefly after it is over.
  MatchCameraFocus shot = focusAt(88.0f, 30.0f);
  shot.livePlay = true;
  shot.hasCarrier = false;
  shot.shotInFlight = true;
  shot.ballVelocity = {25.0f, 1.0f, 0.0f};
  camera.update(shot, MatchCameraMode::DIRECTOR, {}, STEP);
  EXPECT_EQ(camera.directorShot(), MatchDirectorShot::GOAL_LINE);
  const Vec3 goalLineEye = camera.eye();
  EXPECT_NEAR(goalLineEye.x, 105.0f + D::GOAL_LINE_BACK, 0.5f);
  EXPECT_LT(goalLineEye.z, D::GOAL_LINE_HEIGHT + 0.5f);
  MatchCameraFocus loose = shot;
  loose.shotInFlight = false;
  run(loose, D::GOAL_LINE_HOLD * 0.5f);
  EXPECT_EQ(camera.directorShot(), MatchDirectorShot::GOAL_LINE);
  run(loose, D::GOAL_LINE_HOLD);
  EXPECT_EQ(camera.directorShot(), MatchDirectorShot::BROADCAST);
  // A second shot inside the cooldown stays on the main camera.
  camera.update(shot, MatchCameraMode::DIRECTOR, {}, STEP);
  EXPECT_EQ(camera.directorShot(), MatchDirectorShot::BROADCAST);

  // An attack building in the final third earns one reverse angle, held for
  // a while and not repeated within its cooldown.
  MatchCameraFocus attack = focusAt(80.0f, 30.0f);
  attack.livePlay = true;
  run(attack, D::MIN_SHOT_SECONDS + D::ATTACK_BUILD_SECONDS + 0.5f);
  EXPECT_EQ(camera.directorShot(), MatchDirectorShot::REVERSE_ANGLE);
  EXPECT_GT(camera.eye().y, 68.0f);
  run(attack, D::REVERSE_HOLD + 0.1f);
  EXPECT_EQ(camera.directorShot(), MatchDirectorShot::BROADCAST);
  // The reverse angle came about MIN_SHOT_SECONDS into the attack.
  run(attack,
      D::REVERSE_COOLDOWN - D::REVERSE_HOLD - D::MIN_SHOT_SECONDS - 1.5f);
  EXPECT_EQ(camera.directorShot(), MatchDirectorShot::BROADCAST);
  run(attack, 3.0f);
  EXPECT_EQ(camera.directorShot(), MatchDirectorShot::REVERSE_ANGLE);
  const int cutsSoFar = camera.directorCuts();

  // Reduced motion keeps the main camera, goals and shots included.
  MatchCamera3D calm;
  MatchCameraFocus calmGoal = goal;
  calmGoal.reducedMotion = true;
  calm.snap(focus, MatchCameraMode::DIRECTOR);
  for (int frame = 0; frame < 300; ++frame)
  {
    calm.update(calmGoal, MatchCameraMode::DIRECTOR, {}, STEP);
    MatchCameraFocus calmShot = shot;
    calmShot.reducedMotion = true;
    calm.update(calmShot, MatchCameraMode::DIRECTOR, {}, STEP);
  }
  EXPECT_EQ(calm.directorShot(), MatchDirectorShot::BROADCAST);
  EXPECT_EQ(calm.directorCuts(), 0);
  EXPECT_EQ(cutsSoFar, 7);
}

TEST(MatchCamera3DTest, PresetsConvergeAndStayOutOfTheStands)
{
  MatchCamera3D camera;
  const MatchCameraFocus focus = focusAt(52.5f, 34.0f);
  camera.snap(focus, MatchCameraMode::TACTICAL);
  for (int frame = 0; frame < 600; ++frame)
    camera.update(focus, MatchCameraMode::BROADCAST, {}, 1.0f / 60.0f);
  EXPECT_NEAR(camera.eye().y, MatchRender3DTuning::Broadcast::EYE_Y, 0.05f);
  EXPECT_NEAR(camera.eye().z, MatchRender3DTuning::Broadcast::EYE_HEIGHT,
              0.05f);

  // Switching to the end camera swings round behind the attack.
  for (int frame = 0; frame < 180; ++frame)
    camera.update(focus, MatchCameraMode::END, {}, 1.0f / 60.0f);
  EXPECT_LT(camera.eye().x, camera.target().x - 10.0f);
  EXPECT_NEAR(camera.eye().y, camera.target().y, 0.5f);

  using CameraTuning = MatchRender3DTuning::Camera;
  for (const float ballX : {0.0f, 10.0f, 52.5f, 95.0f, 105.0f})
  {
    for (const float direction : {1.0f, -1.0f})
    {
      MatchCameraFocus endFocus = focusAt(ballX, 10.0f);
      endFocus.attackDirection = direction;
      MatchCamera3D endCamera;
      endCamera.snap(endFocus, MatchCameraMode::END);
      EXPECT_GE(endCamera.eye().x, CameraTuning::EYE_MIN_X - 1e-3f);
      EXPECT_LE(endCamera.eye().x, CameraTuning::EYE_MAX_X + 1e-3f);
      EXPECT_GE(endCamera.eye().z, CameraTuning::MIN_EYE_HEIGHT);
    }
  }

  MatchCamera3D zoomed;
  MatchCameraControl zoomIn;
  zoomIn.zoomSteps = 100.0f;
  zoomed.update(focus, MatchCameraMode::BROADCAST, zoomIn, 0.016f);
  EXPECT_FLOAT_EQ(zoomed.zoom(), CameraTuning::MIN_ZOOM);
  MatchCameraControl zoomOut;
  zoomOut.zoomSteps = -100.0f;
  zoomed.update(focus, MatchCameraMode::BROADCAST, zoomOut, 0.016f);
  EXPECT_FLOAT_EQ(zoomed.zoom(), CameraTuning::MAX_ZOOM);
}

TEST(MatchRender3DMath, GroundPickingInvertsProjection)
{
  const Projection projection = testProjection();
  for (const Vec3 ground : {Vec3{10.0f, 5.0f, 0.0f}, Vec3{-20.0f, 30.0f, 0.0f},
                            Vec3{40.0f, -10.0f, 0.0f}})
  {
    ScreenPoint screen;
    ASSERT_TRUE(projection.project(ground, screen));
    Vec3 picked;
    ASSERT_TRUE(projection.groundPointAt(screen.x, screen.y, picked));
    EXPECT_NEAR(picked.x, ground.x, 1e-2f);
    EXPECT_NEAR(picked.y, ground.y, 1e-2f);
    EXPECT_NEAR(picked.z, 0.0f, 1e-4f);
  }
  // The top edge of this view looks above the horizon: nothing to pick.
  Vec3 sky;
  EXPECT_FALSE(
      projection.groundPointAt(TEST_RECT.x, TEST_RECT.y - 400.0f, sky));
}

TEST(MatchCamera3DTest, FreeCameraTakesOverFromThePoseOnScreen)
{
  using enum MatchCameraMode;
  const MatchCameraFocus focus = focusAt(40.0f, 30.0f);
  MatchCamera3D camera;
  camera.snap(focus, BROADCAST);
  for (int frame = 0; frame < 120; ++frame)
    camera.update(focus, BROADCAST, {}, 1.0f / 60.0f);
  const Vec3 before = camera.eye();
  const float fovBefore = camera.verticalFov();
  camera.update(focus, FREE, {}, 1.0f / 60.0f);
  EXPECT_NEAR(camera.eye().x, before.x, 1e-3f);
  EXPECT_NEAR(camera.eye().y, before.y, 1e-3f);
  EXPECT_NEAR(camera.eye().z, before.z, 1e-3f);
  EXPECT_NEAR(camera.verticalFov(), fovBefore, 1e-5f);

  // Orbiting turns around the same target.
  MatchCameraControl orbit;
  orbit.orbitYaw = 0.6f;
  camera.update(focus, FREE, orbit, 1.0f / 60.0f);
  for (int frame = 0; frame < 120; ++frame)
    camera.update(focus, FREE, {}, 1.0f / 60.0f);
  const Vec3 target = camera.target();
  EXPECT_NEAR(RenderMath::length(camera.eye() - target),
              RenderMath::length(before - target), 0.05f);
  EXPECT_GT(RenderMath::length(camera.eye() - before), 10.0f);
}

TEST(MatchCamera3DTest, FreeCameraClampsPitchZoomAndTarget)
{
  using enum MatchCameraMode;
  using Free = MatchRender3DTuning::Free;
  const MatchCameraFocus focus = focusAt(52.5f, 34.0f);
  MatchCamera3D camera;
  camera.snap(focus, FREE);

  MatchCameraControl under;
  under.orbitPitch = -10.0f;
  camera.update(focus, FREE, under, 1.0f / 60.0f);
  EXPECT_GE(camera.freePitch(), Free::MIN_PITCH - 1e-5f);
  for (int frame = 0; frame < 240; ++frame)
    camera.update(focus, FREE, {}, 1.0f / 60.0f);
  EXPECT_GE(camera.eye().z, MatchRender3DTuning::Camera::MIN_EYE_HEIGHT);

  MatchCameraControl over;
  over.orbitPitch = 10.0f;
  camera.update(focus, FREE, over, 1.0f / 60.0f);
  EXPECT_LE(camera.freePitch(), Free::MAX_PITCH + 1e-5f);

  MatchCameraControl zoomIn;
  zoomIn.zoomSteps = 200.0f;
  camera.update(focus, FREE, zoomIn, 1.0f / 60.0f);
  EXPECT_FLOAT_EQ(camera.freeDistance(), Free::MIN_DISTANCE);
  MatchCameraControl zoomOut;
  zoomOut.zoomSteps = -200.0f;
  camera.update(focus, FREE, zoomOut, 1.0f / 60.0f);
  EXPECT_FLOAT_EQ(camera.freeDistance(), Free::MAX_DISTANCE);

  // Panning far away stops at the pitch surrounds.
  MatchCameraControl pan;
  pan.pan = {-500.0f, 900.0f, 0.0f};
  camera.update(focus, FREE, pan, 1.0f / 60.0f);
  EXPECT_FLOAT_EQ(camera.freeTarget().x, -Free::TARGET_MARGIN);
  EXPECT_FLOAT_EQ(camera.freeTarget().y,
                  MatchTuning::Pitch::WIDTH_METRES + Free::TARGET_MARGIN);

  // Low, long views never end up inside a stand.
  MatchCameraControl low;
  low.orbitPitch = -10.0f;
  low.reset = true;
  camera.update(focus, FREE, low, 1.0f / 60.0f);
  for (int frame = 0; frame < 600; ++frame)
    camera.update(focus, FREE, zoomOut, 1.0f / 60.0f);
  if (const Vec3 eye = camera.eye();
      eye.z < MatchRender3DTuning::Camera::STAND_CLEAR_HEIGHT)
  {
    EXPECT_GE(eye.x, Free::EYE_MIN_X - 0.1f);
    EXPECT_LE(eye.x, Free::EYE_MAX_X + 0.1f);
    EXPECT_GE(eye.y, Free::EYE_MIN_Y - 0.1f);
    EXPECT_LE(eye.y, Free::EYE_MAX_Y + 0.1f);
  }

  // Re-targeting and following the ball move the orbit centre.
  MatchCameraControl retarget;
  retarget.retarget = true;
  retarget.retargetPoint = {80.0f, 20.0f, 0.0f};
  camera.update(focus, FREE, retarget, 1.0f / 60.0f);
  EXPECT_FLOAT_EQ(camera.freeTarget().x, 80.0f);
  MatchCameraControl follow;
  follow.followBall = true;
  camera.update(focusAt(12.0f, 50.0f), FREE, follow, 1.0f / 60.0f);
  EXPECT_FLOAT_EQ(camera.freeTarget().x, 12.0f);
  EXPECT_FLOAT_EQ(camera.freeTarget().y, 50.0f);
}

TEST(MatchCamera3DTest, FreeCameraIsFrameRateIndependent)
{
  using enum MatchCameraMode;
  const MatchCameraFocus focus = focusAt(30.0f, 20.0f);
  MatchCamera3D coarse;
  MatchCamera3D fine;
  coarse.snap(focus, FREE);
  fine.snap(focus, FREE);
  MatchCameraControl input;
  input.orbitYaw = 0.8f;
  input.orbitPitch = 0.3f;
  input.zoomSteps = 2.0f;
  input.pan = {6.0f, -4.0f, 0.0f};
  // The same drag lands in one 30 fps frame or the first of two 60 fps ones.
  coarse.update(focus, FREE, input, 1.0f / 30.0f);
  fine.update(focus, FREE, input, 1.0f / 60.0f);
  fine.update(focus, FREE, {}, 1.0f / 60.0f);
  EXPECT_NEAR(coarse.eye().x, fine.eye().x, 1e-3f);
  EXPECT_NEAR(coarse.eye().y, fine.eye().y, 1e-3f);
  EXPECT_NEAR(coarse.eye().z, fine.eye().z, 1e-3f);
}

TEST(MatchRender3DProportions, WorldIsBuiltAtRealScale)
{
  using T = MatchRender3DTuning;
  EXPECT_FLOAT_EQ(MatchTuning::Pitch::LENGTH_METRES, 105.0f);
  EXPECT_FLOAT_EQ(MatchTuning::Pitch::WIDTH_METRES, 68.0f);
  EXPECT_FLOAT_EQ(T::Goal::WIDTH, 7.32f);
  EXPECT_FLOAT_EQ(T::Goal::HEIGHT, 2.44f);
  EXPECT_FLOAT_EQ(T::Markings::PENALTY_AREA_DEPTH, 16.5f);
  EXPECT_FLOAT_EQ(T::Markings::CIRCLE_RADIUS, 9.15f);
  EXPECT_LE(T::Markings::LINE_WIDTH, 0.12f);
  EXPECT_FLOAT_EQ(T::Ball::RADIUS * 2.0f, 0.22f);
  EXPECT_LE(T::Ball::MAX_BOOST, 2.0f);

  // The reference footballer stands 1.80 m with ~0.5 m across the arms.
  using P = T::Player;
  const float headTop =
      (P::TORSO_BASE + P::TORSO_LENGTH + P::NECK_LENGTH + P::HEAD_RADIUS) *
      P::SCALE;
  EXPECT_NEAR(headTop, P::REFERENCE_HEIGHT_METRES, 0.01f);
  EXPECT_NEAR(P::HIP_HEIGHT - P::THIGH_LENGTH - P::SHIN_LENGTH,
              P::BOOT_HALF_HEIGHT, 0.01f);
  const float span = 2.0f * (P::SHOULDER_SPREAD + P::UPPER_ARM_TOP) * P::SCALE;
  EXPECT_GE(span, 0.45f);
  EXPECT_LE(span, 0.56f);
  EXPECT_GE(2.0f * P::CHEST_HALF_WIDTH * P::SCALE, 0.34f);
  EXPECT_LE(2.0f * P::CHEST_HALF_WIDTH * P::SCALE, 0.44f);
  EXPECT_LE(P::MIN_HEIGHT_METRES, 1.70f);
  EXPECT_GE(P::MAX_HEIGHT_METRES, 1.95f);

  // Broadcast fields of view stay in the TV range (about 20-35 degrees).
  constexpr float DEGREES = 180.0f / std::numbers::pi_v<float>;
  EXPECT_GE(T::Broadcast::FOV * DEGREES, 20.0f);
  EXPECT_LE(T::Broadcast::FOV * DEGREES, 35.0f);
}

TEST(MatchRender3DProportions, PlayerHeightOnScreenIsBroadcastSized)
{
  // A 1.80 m player at the centre spot, seen by the settled broadcast
  // camera, is neither a giant nor an ant, at 720p and at 1440p.
  MatchCamera3D camera;
  camera.snap(focusAt(52.5f, 34.0f), MatchCameraMode::BROADCAST);
  for (const float height : {720.0f, 1440.0f})
  {
    const RenderMath::ScreenRect rect{0.0f, 0.0f, height * 16.0f / 9.0f,
                                      height};
    const Projection projection =
        Projection::make(camera.eye(), camera.target(), camera.verticalFov(),
                         MatchRender3DTuning::Camera::NEAR_PLANE,
                         MatchRender3DTuning::Camera::FAR_PLANE, rect);
    ScreenPoint feet;
    ScreenPoint head;
    ASSERT_TRUE(projection.project({52.5f, 34.0f, 0.0f}, feet));
    ASSERT_TRUE(projection.project({52.5f, 34.0f, 1.8f}, head));
    const float pixels = feet.y - head.y;
    std::cout << "[match-3d] 1.80 m player at the centre spot: " << pixels
              << " px of " << height << '\n';
    EXPECT_GT(pixels / height, 0.035f);
    EXPECT_LT(pixels / height, 0.09f);

    // The goal mouth at the far end reads as 7.32 x 2.44 m (3:1).
    ScreenPoint postBottom;
    ScreenPoint postTop;
    ASSERT_TRUE(projection.project({0.0f, 34.0f, 0.0f}, postBottom));
    ASSERT_TRUE(projection.project({0.0f, 34.0f, 2.44f}, postTop));
    EXPECT_GT(postBottom.y - postTop.y, pixels * 0.6f);
  }
}

TEST(MatchKitColorsTest, KitsNeverClash)
{
  for (TeamID home = 1; home <= 80; ++home)
  {
    for (TeamID away = 1; away <= 80; ++away)
    {
      const MatchKits kits = chooseMatchKits(home, away);
      EXPECT_GE(kitColorDistance(kits.home.shirt, kits.away.shirt),
                KIT_CLASH_DISTANCE)
          << home << " vs " << away;
      for (const ImU32 keeper :
           {kits.homeGoalkeeper.shirt, kits.awayGoalkeeper.shirt})
      {
        EXPECT_GE(kitColorDistance(keeper, kits.home.shirt),
                  KIT_CLASH_DISTANCE);
        EXPECT_GE(kitColorDistance(keeper, kits.away.shirt),
                  KIT_CLASH_DISTANCE);
      }
      EXPECT_GE(kitColorDistance(kits.homeGoalkeeper.shirt,
                                 kits.awayGoalkeeper.shirt),
                KIT_CLASH_DISTANCE);
    }
  }
  const MatchKits first = chooseMatchKits(7, 12);
  const MatchKits second = chooseMatchKits(7, 12);
  EXPECT_EQ(first.home.shirt, second.home.shirt);
  EXPECT_EQ(first.away.shirt, second.away.shirt);
}

TEST(MatchKitColorsTest, ClubsWearTheirOwnColoursWithoutClashes)
{
  // Every pairing of data-pack clubs stays readable.
  std::vector<TeamID> clubs;
  for (TeamID team = 1; team < 2000; ++team)
    if (findClubIdentity(team)) clubs.push_back(team);
  ASSERT_FALSE(clubs.empty());
  for (const TeamID home : clubs)
  {
    const ClubIdentity* identity = findClubIdentity(home);
    const MatchKits alone = chooseMatchKits(home, 0);
    EXPECT_EQ(alone.home.shirt, kitColorFromRgb(identity->primary_colour));
    EXPECT_EQ(alone.home.trim, kitColorFromRgb(identity->secondary_colour));
    for (const TeamID away : clubs)
    {
      if (away == home) continue;
      const MatchKits kits = chooseMatchKits(home, away);
      EXPECT_GE(kitColorDistance(kits.home.shirt, kits.away.shirt),
                KIT_CLASH_DISTANCE)
          << home << " vs " << away;
      for (const ImU32 keeper :
           {kits.homeGoalkeeper.shirt, kits.awayGoalkeeper.shirt})
      {
        EXPECT_GE(kitColorDistance(keeper, kits.home.shirt), KIT_CLASH_DISTANCE)
            << home << " vs " << away;
        EXPECT_GE(kitColorDistance(keeper, kits.away.shirt), KIT_CLASH_DISTANCE)
            << home << " vs " << away;
      }
    }
  }

  // A clash sends the away side to its reversed colours first.
  const ClubColours red{IM_COL32(200, 20, 30, 255),
                        IM_COL32(250, 250, 250, 255)};
  const ClubColours crimson{IM_COL32(190, 30, 40, 255),
                            IM_COL32(20, 30, 90, 255)};
  const MatchKits clash = chooseMatchKits(1, 2, &red, &crimson);
  EXPECT_EQ(clash.home.shirt, red.primary);
  EXPECT_EQ(clash.away.shirt, crimson.secondary);
}

TEST(MatchRender3DMath, ShirtNumbersAreClassicAndUnique)
{
  using enum PlayerRole;
  // A 4-4-2 with two centre backs and two central midfielders.
  RenderMath::ShirtNumbers numbers;
  const std::array<PlayerRole, 11> starters{GK, RB, CB, CB, LB, RM,
                                            CM, CM, LM, ST, ST};
  std::vector<int> taken;
  for (const PlayerRole role : starters)
    taken.push_back(numbers.take(role, true));
  EXPECT_EQ(taken[0], 1);
  EXPECT_EQ(taken[1], 2);
  EXPECT_EQ(taken[2], 4);
  EXPECT_EQ(taken[3], 5);
  EXPECT_EQ(taken[4], 3);
  EXPECT_EQ(taken[5], 7);
  EXPECT_EQ(taken[6], 8);
  EXPECT_EQ(taken[9], 9);
  // Substitutes get squad numbers above the starting eleven.
  taken.push_back(numbers.take(ST, false));
  taken.push_back(numbers.take(GK, false));
  EXPECT_EQ(taken[11], 12);
  EXPECT_EQ(taken[12], 13);
  std::vector<int> sorted = taken;
  std::ranges::sort(sorted);
  EXPECT_EQ(std::adjacent_find(sorted.begin(), sorted.end()), sorted.end());
  for (std::size_t index = 0; index < 11; ++index)
  {
    EXPECT_GE(taken[index], 1);
    EXPECT_LE(taken[index], 11);
  }

  // Eleven players of one role still share out 1-11 without repeats.
  RenderMath::ShirtNumbers crowded;
  std::vector<int> same;
  for (int index = 0; index < 11; ++index)
    same.push_back(crowded.take(CB, true));
  std::ranges::sort(same);
  for (int index = 0; index < 11; ++index) EXPECT_EQ(same[index], index + 1);
}

TEST(MatchKitColorsTest, IntegerBlendsMatchTheFloatOnes)
{
  const std::array<ImU32, 4> colors{
      IM_COL32(200, 28, 40, 255), IM_COL32(12, 240, 99, 128),
      IM_COL32(255, 255, 255, 255), IM_COL32(0, 0, 0, 0)};
  const auto near = [](ImU32 a, ImU32 b)
  {
    return std::ranges::all_of(std::array{0, 8, 16, 24},
                               [a, b](int shift)
                               {
                                 const int difference =
                                     static_cast<int>((a >> shift) & 0xFFU) -
                                     static_cast<int>((b >> shift) & 0xFFU);
                                 return std::abs(difference) <= 1;
                               });
  };
  for (const ImU32 first : colors)
  {
    for (const ImU32 second : colors)
    {
      for (const std::uint32_t t : {0U, 64U, 128U, 200U, 256U})
      {
        EXPECT_TRUE(
            near(mixColor256(first, second, t),
                 mixColor(first, second, static_cast<float>(t) / 256.0f)))
            << std::hex << first << ' ' << second << ' ' << t;
      }
    }
    for (const std::uint32_t factor : {0U, 128U, 184U, 256U, 400U})
    {
      EXPECT_TRUE(near(shadeColor256(first, factor),
                       shadeColor(first, static_cast<float>(factor) / 256.0f)))
          << std::hex << first << ' ' << factor;
    }
  }
}

TEST(MatchKitColorsTest, NumbersAndGlovesStandOut)
{
  for (TeamID home = 1; home <= 80; ++home)
  {
    for (TeamID away = 1; away <= 80; away += 7)
    {
      const MatchKits kits = chooseMatchKits(home, away);
      for (const KitColors& kit :
           {kits.home, kits.away, kits.homeGoalkeeper, kits.awayGoalkeeper})
      {
        EXPECT_GE(kitColorDistance(kitNumberColor(kit), kit.shirt),
                  KIT_CLASH_DISTANCE)
            << home << " vs " << away;
      }
      for (const KitColors& keeper : {kits.homeGoalkeeper, kits.awayGoalkeeper})
      {
        EXPECT_GE(kitColorDistance(goalkeeperGloveColor(keeper), keeper.shirt),
                  KIT_CLASH_DISTANCE)
            << home << " vs " << away;
      }
    }
  }
}

TEST(MatchRenderer3DRig, TwoBoneIkReachesTargetsAndBendsTowardsThePole)
{
  constexpr float UPPER = 0.46f;
  constexpr float LOWER = 0.42f;
  const Vec3 hip{0.0f, 0.0f, 0.93f};
  const Vec3 pole{1.0f, 0.0f, 0.0f};
  for (const Vec3 target : {Vec3{0.2f, 0.1f, 0.12f}, Vec3{-0.3f, 0.0f, 0.2f},
                            Vec3{0.5f, -0.1f, 0.6f}})
  {
    const PlayerRig::TwoBone leg =
        PlayerRig::solveTwoBone(hip, target, UPPER, LOWER, pole);
    EXPECT_TRUE(leg.reached);
    EXPECT_NEAR(RenderMath::length(leg.middle - hip), UPPER, 1e-4f);
    EXPECT_NEAR(RenderMath::length(leg.end - leg.middle), LOWER, 1e-4f);
    EXPECT_NEAR(RenderMath::length(leg.end - target), 0.0f, 1e-4f);
    // The knee bends forward, never backwards.
    const Vec3 halfway = (hip + target) * 0.5f;
    EXPECT_GT(RenderMath::dot(leg.middle - halfway, pole), 0.0f);
  }
  // Out of reach: the leg points straight at the target.
  const PlayerRig::TwoBone stretched =
      PlayerRig::solveTwoBone(hip, {0.0f, 0.0f, -2.0f}, UPPER, LOWER, pole);
  EXPECT_FALSE(stretched.reached);
  EXPECT_NEAR(RenderMath::length(stretched.end - hip), UPPER + LOWER, 1e-3f);
  EXPECT_LT(stretched.end.z, hip.z);
}

TEST(MatchRenderer3DRig, PoseSelectionFollowsTheEngineState)
{
  using PlayerRig::Action;
  using PlayerRig::Event;
  PlayerRig::ActionInput input;
  EXPECT_EQ(PlayerRig::selectAction(input), Action::IDLE);
  const std::array<std::pair<float, Action>, 5> gaits{{{0.2f, Action::IDLE},
                                                       {1.4f, Action::WALK},
                                                       {3.8f, Action::JOG},
                                                       {5.5f, Action::RUN},
                                                       {8.0f, Action::SPRINT}}};
  for (const auto& [speed, action] : gaits)
  {
    input.speed = speed;
    EXPECT_EQ(PlayerRig::selectAction(input), action) << speed;
  }
  // A sharp turn at low speed.
  input.speed = 2.0f;
  input.turnRate = 4.0f;
  EXPECT_EQ(PlayerRig::selectAction(input), Action::TURN);
  input.turnRate = 0.0f;

  const std::array<std::pair<Event, Action>, 7> events{
      {{Event::PASS, Action::PASS},
       {Event::SHOT, Action::SHOT},
       {Event::CROSS, Action::CROSS},
       {Event::HEADER, Action::HEADER},
       {Event::TACKLE, Action::TACKLE},
       {Event::SLIDE, Action::SLIDE},
       {Event::THROW, Action::THROW_IN}}};
  for (const auto& [event, action] : events)
  {
    input.event = event;
    EXPECT_EQ(PlayerRig::selectAction(input), action);
  }
  input.event = Event::NONE;
  input.throwIn = true;
  EXPECT_EQ(PlayerRig::selectAction(input), Action::THROW_IN);
  input.throwIn = false;

  // Keepers: a dive beats everything, then holding, then the set stance.
  input.goalkeeper = true;
  input.keeperSet = true;
  input.speed = 0.5f;
  EXPECT_EQ(PlayerRig::selectAction(input), Action::KEEPER_SET);
  input.holding = true;
  EXPECT_EQ(PlayerRig::selectAction(input), Action::KEEPER_HOLD);
  input.diving = true;
  input.event = Event::PASS;
  EXPECT_EQ(PlayerRig::selectAction(input), Action::KEEPER_DIVE);

  // Goal reactions win over the gait but not over a ball action.
  PlayerRig::ActionInput outfield;
  outfield.speed = 6.0f;
  outfield.mood = PlayerRig::Mood::CELEBRATE;
  EXPECT_EQ(PlayerRig::selectAction(outfield), Action::CELEBRATE);
  outfield.mood = PlayerRig::Mood::DEJECTED;
  EXPECT_EQ(PlayerRig::selectAction(outfield), Action::DEJECTED);
  outfield.event = Event::HEADER;
  EXPECT_EQ(PlayerRig::selectAction(outfield), Action::HEADER);
}

TEST(MatchRenderer3DRig, PlantedFootStaysPutThroughTheStance)
{
  // A player crossing the pitch at a walk, a jog and a sprint at 60 frames
  // per second: every foot on the ground keeps its exact spot until it
  // lifts, the legs alternate, and walking always keeps a foot down.
  using P = MatchRender3DTuning::Player;
  using R = MatchRender3DTuning::Rig;
  constexpr float DT = 1.0f / 60.0f;
  constexpr float PI = std::numbers::pi_v<float>;
  for (const float speed : {1.4f, 4.0f, 7.5f})
  {
    std::array<PlayerRig::FootState, 2> feet{};
    std::array<Vec3, 2> last{};
    std::array<bool, 2> wasDown{};
    std::array<int, 2> landings{};
    const float cycle = P::STRIDE_BASE_METRES + P::STRIDE_PER_SPEED * speed;
    const float duty = PlayerRig::dutyFactor(speed);
    float phase = 0.0f;
    Vec3 root{10.0f, 30.0f, 0.0f};
    int stanceFrames = 0;
    int flightFrames = 0;
    float worstSlide = 0.0f;
    float worstReach = 0.0f;
    for (int frame = 0; frame < 900; ++frame)
    {
      const float step = speed * DT;
      root.x += step;
      phase = std::fmod(phase + 2.0f * PI * step / cycle, 2.0f * PI);
      bool anyDown = false;
      for (std::size_t leg = 0; leg < 2; ++leg)
      {
        PlayerRig::StrideInput input;
        input.rest =
            root +
            Vec3{0.0f, (leg == 0 ? 1.0f : -1.0f) * R::STANCE_WIDTH, 0.0f};
        input.forward = {1.0f, 0.0f, 0.0f};
        input.legPhase = std::fmod(phase + (leg == 0 ? 0.0f : PI), 2.0f * PI);
        input.duty = duty;
        input.stanceLength = duty * cycle;
        input.frontReach = R::FRONT_REACH;
        input.liftHeight = R::WALK_LIFT;
        input.maxDrift = R::MAX_DRIFT;
        input.deltaSeconds = DT;
        const Vec3 foot = PlayerRig::stepFoot(feet[leg], input);
        if (feet[leg].inStance)
        {
          anyDown = true;
          if (wasDown[leg])
          {
            worstSlide =
                std::max(worstSlide, RenderMath::length(foot - last[leg]));
            ++stanceFrames;
          }
          else
          {
            ++landings[leg];
          }
          EXPECT_FLOAT_EQ(foot.z, 0.0f);
          worstReach = std::max(worstReach, std::abs(foot.x - root.x));
        }
        last[leg] = foot;
        wasDown[leg] = feet[leg].inStance;
      }
      if (!anyDown) ++flightFrames;
    }
    EXPECT_GT(stanceFrames, 150) << speed;
    EXPECT_LT(worstSlide, 1e-4f) << speed;
    // Both legs keep stepping, the same number of times give or take one.
    EXPECT_GT(landings[0], 3) << speed;
    EXPECT_LE(std::abs(landings[0] - landings[1]), 1) << speed;
    // A planted foot never ends up further from the body than a stride.
    EXPECT_LT(worstReach, duty * cycle + 0.05f) << speed;
    if (speed < 2.0f)
      EXPECT_EQ(flightFrames, 0) << "walking keeps a foot down";
    else if (speed > 7.0f)
      EXPECT_GT(flightFrames, 0) << "sprinting has flight phases";
  }
}

TEST(MatchRenderer3DRig, StandingFeetStayDownUntilTheBodyDrifts)
{
  using R = MatchRender3DTuning::Rig;
  constexpr float DT = 1.0f / 60.0f;
  std::array<PlayerRig::FootState, 2> feet{};
  Vec3 root{50.0f, 30.0f, 0.0f};
  const auto update = [&]
  {
    for (std::size_t leg = 0; leg < 2; ++leg)
    {
      PlayerRig::StrideInput input;
      input.rest =
          root + Vec3{0.0f, (leg == 0 ? 1.0f : -1.0f) * R::STANCE_WIDTH, 0.0f};
      input.idle = true;
      input.idleStepMetres = R::IDLE_STEP_METRES;
      input.stepDuration = R::IDLE_STEP_SECONDS;
      input.otherStepping = feet[1 - leg].stepSeconds > 0.0f;
      input.deltaSeconds = DT;
      PlayerRig::stepFoot(feet[leg], input);
    }
  };
  update();
  const std::array<Vec3, 2> start{feet[0].position, feet[1].position};
  // Small sway of the body: the boots do not move at all.
  for (int frame = 0; frame < 30; ++frame)
  {
    root.y += 0.1f / 30.0f;
    update();
  }
  EXPECT_FLOAT_EQ(feet[0].position.x, start[0].x);
  EXPECT_FLOAT_EQ(feet[0].position.y, start[0].y);
  EXPECT_FLOAT_EQ(feet[1].position.y, start[1].y);
  // A real shuffle: the feet step one at a time and end under the hips.
  bool bothStepping = false;
  for (int frame = 0; frame < 120; ++frame)
  {
    if (frame < 40) root.y += 0.5f / 40.0f;
    update();
    bothStepping = bothStepping ||
                   (feet[0].stepSeconds > 0.0f && feet[1].stepSeconds > 0.0f);
  }
  EXPECT_FALSE(bothStepping);
  for (std::size_t leg = 0; leg < 2; ++leg)
  {
    const float restY = root.y + (leg == 0 ? 1.0f : -1.0f) * R::STANCE_WIDTH;
    EXPECT_TRUE(feet[leg].inStance);
    EXPECT_NEAR(feet[leg].position.y, restY, R::IDLE_STEP_METRES);
  }
}

TEST(MatchRenderer3DRig, HeelRollsOntoTheToesInsteadOfSliding)
{
  // A planted foot left behind the hip lifts its heel around the toe; the
  // toe stays exactly where it was.
  const Vec3 hip{0.0f, 0.0f, 0.9f};
  const Vec3 forward{1.0f, 0.0f, 0.0f};
  constexpr float TOE = 0.15f;
  constexpr float REACH = 0.875f;
  Vec3 ankle{-0.55f, 0.0f, 0.085f};
  const Vec3 toe = ankle + forward * TOE;
  const float heel =
      PlayerRig::rollOntoToes(ankle, hip, forward, TOE, REACH, 1.05f);
  EXPECT_GT(heel, 0.1f);
  EXPECT_NEAR(RenderMath::length(ankle - toe), TOE, 1e-4f);
  EXPECT_LE(RenderMath::length(ankle - hip), REACH + 1e-3f);
  // Within reach nothing changes.
  Vec3 under{0.05f, 0.0f, 0.085f};
  EXPECT_FLOAT_EQ(
      PlayerRig::rollOntoToes(under, hip, forward, TOE, REACH, 1.05f), 0.0f);
  EXPECT_FLOAT_EQ(under.x, 0.05f);
}

TEST(MatchRenderer3DRig, KickPutsTheAnkleOnTheBallAtContact)
{
  using PlayerRig::Event;
  const PlayerRig::KickTiming timing;
  const Vec3 contact{30.0f, 20.0f, 0.16f};
  const Vec3 direction{0.0f, 1.0f, 0.0f};
  const Vec3 across{1.0f, 0.0f, 0.0f};
  for (const Event event : {Event::PASS, Event::SHOT, Event::CROSS})
  {
    float weight = 0.0f;
    // At the strike the ankle is on the ball and owns the leg fully.
    const Vec3 strike = PlayerRig::kickAnkle(event, 0.0f, contact, direction,
                                             across, timing, weight);
    EXPECT_FLOAT_EQ(weight, 1.0f);
    EXPECT_NEAR(RenderMath::length(strike - contact), 0.0f, 1e-5f);
    // Then it follows through along the ball's path and up.
    const Vec3 through = PlayerRig::kickAnkle(
        event, timing.contactHold + timing.followThrough * 0.9f, contact,
        direction, across, timing, weight);
    EXPECT_FLOAT_EQ(weight, 1.0f);
    EXPECT_GT(RenderMath::dot(through - contact, direction), 0.2f);
    EXPECT_GT(through.z, contact.z);
    // And hands the leg back to the gait.
    PlayerRig::kickAnkle(event, timing.total() - 0.01f, contact, direction,
                         across, timing, weight);
    EXPECT_LT(weight, 0.05f);
    PlayerRig::kickAnkle(event, timing.total() + 0.1f, contact, direction,
                         across, timing, weight);
    EXPECT_FLOAT_EQ(weight, 0.0f);
  }
  // Shots follow through higher than passes.
  float weight = 0.0f;
  const float late = timing.contactHold + timing.followThrough;
  EXPECT_GT(PlayerRig::kickAnkle(Event::SHOT, late, contact, direction, across,
                                 timing, weight)
                .z,
            PlayerRig::kickAnkle(Event::PASS, late, contact, direction, across,
                                 timing, weight)
                .z);
}

TEST(MatchRenderer3DStadium, DaylightLightsTheStandsAndShadesUnderTheRoof)
{
  const auto luminance = [](ImU32 color)
  {
    return 0.3f * static_cast<float>((color >> IM_COL32_R_SHIFT) & 0xFFU) +
           0.59f * static_cast<float>((color >> IM_COL32_G_SHIFT) & 0xFFU) +
           0.11f * static_cast<float>((color >> IM_COL32_B_SHIFT) & 0xFFU);
  };
  const MatchKits kits = chooseMatchKits(1, 2);
  Stadium3D::Geometry night;
  night.build(kits, false);
  Stadium3D::Geometry day;
  day.build(kits, true);
  ASSERT_EQ(night.faces.size(), day.faces.size());
  ASSERT_EQ(night.crowd.size(), day.crowd.size());
  // Concrete, walls, roofs and masts (not the crowd, not the lamps) are no
  // longer in their floodlit night colours by day.
  float nightFaces = 0.0f;
  float dayFaces = 0.0f;
  for (std::size_t index = 0; index < day.faces.size(); ++index)
  {
    const Stadium3D::Face& face = night.faces[index];
    if (face.glow != 0U || face.clumpEnd != face.clumpBegin) continue;
    nightFaces += luminance(face.colors[0]);
    dayFaces += luminance(day.faces[index].colors[0]);
  }
  EXPECT_GT(dayFaces, nightFaces * 1.3f);
  // The far (north) stand faces the afternoon sun: its crowd is brighter by
  // day than under the floodlights.
  float nightCrowd = 0.0f;
  float dayCrowd = 0.0f;
  for (std::size_t index = 0; index < day.crowd.size(); ++index)
  {
    if (day.crowd[index].base.y < MatchTuning::Pitch::WIDTH_METRES + 8.0f)
      continue;
    nightCrowd += luminance(night.crowd[index].body);
    dayCrowd += luminance(day.crowd[index].body);
  }
  EXPECT_GT(nightCrowd, 0.0f);
  EXPECT_GT(dayCrowd, nightCrowd);
  // Lamps are off by day.
  for (const Stadium3D::Face& face : day.faces) EXPECT_EQ(face.glow, 0U);

  // Only daylight casts the roofs' shadows: along the main stand's
  // touchline, never over the centre circle.
  EXPECT_TRUE(night.standShadows.empty());
  EXPECT_FALSE(night.inStandShadow({60.0f, 1.0f, 0.0f}));
  EXPECT_FALSE(day.standShadows.empty());
  EXPECT_TRUE(day.inStandShadow({60.0f, 1.0f, 0.0f}));
  EXPECT_FALSE(day.inStandShadow({52.5f, 34.0f, 0.0f}));
}

class MatchRenderer3DSceneTest : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    Logger::init();
    ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::EN));
    controller = std::make_unique<GameController>();
    controller->newGame(98);
  }

  std::unique_ptr<GameController> controller;
};

TEST_F(MatchRenderer3DSceneTest, SwitchesViewsAndCapturesFrames)
{
  // GUIView is only used as the controller gateway for the scene; it is never
  // initialised and outlives the SDL/ImGui objects created below.
  GUIView view(*controller);
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
  SDL_Window* window = SDL_CreateWindow("3D match view test", 1280, 800, 0);
  ASSERT_NE(window, nullptr);
  SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
  ASSERT_NE(renderer, nullptr);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  const std::string fontPath = AssetPaths::font();
  ASSERT_NE(ImGui::GetIO().Fonts->AddFontFromFileTTF(fontPath.c_str(), 20.0f),
            nullptr);
  // The real theme, so HUD buttons and accents look as in the game.
  Theme::apply(Theme::Appearance{}, 1.0f);
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  {
    const auto teams = controller->getTeams();
    ASSERT_GE(teams.size(), 2u);
    controller->selectManagedTeam(teams[0].get().getId());
    MatchScene scene(&view, teams[0].get().getId(), teams[1].get().getId());
    scene.onEnter();
    ASSERT_NE(scene.engine, nullptr);
    // Camera controls are exercised during normal playback. Highlight
    // skipping can move past the pre-match modal's expiry on some seeds,
    // hiding the input blocker instead of preparing the view explicitly.
    scene.setHighlightsOnly(false);

    const auto frame = [&](float seconds)
    {
      ImGui_ImplSDLRenderer3_NewFrame();
      ImGui_ImplSDL3_NewFrame();
      // Synthetic input uses the same deterministic clock as the scene.
      // Wall-clock frame times can otherwise turn separate clicks into a
      // double-click on fast CI runners, toggling focus instead of orbiting.
      ImGui::GetIO().DeltaTime = seconds;
      ImGui::NewFrame();
      scene.update(seconds);
      scene.render();
      ImGui::Render();
      SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
      SDL_RenderClear(renderer);
      ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    };
    const auto press = [&](SDL_Keycode key)
    {
      SDL_Event event{.key = {}};
      event.key.type = SDL_EVENT_KEY_DOWN;
      event.key.key = key;
      scene.handleEvent(event);
    };
    // Starts every run from the defaults (the scene remembers the last
    // presentation for the session).
    scene.setSidePanelsHidden(false);
    const auto capture = [&](const std::string& name)
    {
      const auto path = RuntimePaths::capturePath(name.c_str());
      std::filesystem::remove(path);
      SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
      ASSERT_NE(surface, nullptr);
      EXPECT_TRUE(SDL_SaveBMP(surface, path.string().c_str()))
          << path.string() << ": " << SDL_GetError();
      SDL_DestroySurface(surface);
      SDL_RenderPresent(renderer);
      ASSERT_TRUE(std::filesystem::exists(path));
      EXPECT_GT(std::filesystem::file_size(path), 1'000u);
      std::cout << "[match-3d] captured " << path.string() << '\n';
    };

    constexpr float FRAME_SECONDS = 1.0f / 60.0f;
    const MatchViewMode initialView = scene.view_mode;
    press(SDLK_V);
    EXPECT_NE(scene.view_mode, initialView);
    if (scene.view_mode != MatchViewMode::BROADCAST_3D) press(SDLK_V);
    ASSERT_EQ(scene.view_mode, MatchViewMode::BROADCAST_3D);
    for (int index = 0; index < 30; ++index) frame(FRAME_SECONDS);

    // The managed club's pre-match talk is modal and correctly prevents
    // pitch drags. Dismiss it through its real keyboard action before
    // capturing the view and asserting that camera gestures work.
    ASSERT_TRUE(scene.team_talk.isOpen());
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
    frame(FRAME_SECONDS);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
    frame(FRAME_SECONDS);
    ASSERT_FALSE(scene.team_talk.isOpen());
    ASSERT_FALSE(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));

    // Broadcast-camera CPU cost of building the 3D draw lists.
    press(SDLK_1);
    std::vector<float> samples;
    samples.reserve(120);
    for (int index = 0; index < 120; ++index)
    {
      frame(FRAME_SECONDS);
      samples.push_back(scene.last_render_milliseconds);
    }
    std::ranges::sort(samples);
    const float median = samples[samples.size() / 2];
    const float p95 = samples[samples.size() * 95 / 100];
    std::cout << "[match-3d] broadcast render CPU median " << median
              << " ms, p95 " << p95 << " ms, draw list "
              << ImGui::GetDrawData()->TotalVtxCount << " vertices\n";
    RecordProperty("render_3d_median_microseconds",
                   static_cast<int>(median * 1000.0f));
    EXPECT_LT(median, 12.0f);
    capture("match_3d_broadcast.bmp");

    const std::array<std::pair<SDL_Keycode, const char*>, 3> cameras{{
        {SDLK_2, "match_3d_tactical.bmp"},
        {SDLK_3, "match_3d_end.bmp"},
        {SDLK_4, "match_3d_follow.bmp"},
    }};
    for (const auto& [key, name] : cameras)
    {
      press(key);
      for (int index = 0; index < 80; ++index) frame(FRAME_SECONDS);
      capture(name);
    }
    EXPECT_EQ(scene.camera_mode, MatchCameraMode::PLAYER_FOLLOW);

    // Zoom extremes: fully out on the tactical camera, fully in on broadcast.
    press(SDLK_2);
    scene.pending_zoom_steps = -30.0f;
    for (int index = 0; index < 80; ++index) frame(FRAME_SECONDS);
    capture("match_3d_tactical_zoom_out.bmp");
    press(SDLK_1);
    scene.pending_zoom_steps = 30.0f;
    for (int index = 0; index < 80; ++index) frame(FRAME_SECONDS);
    capture("match_3d_broadcast_zoom_in.bmp");
    scene.pending_zoom_steps = -30.0f;
    for (int index = 0; index < 80; ++index) frame(FRAME_SECONDS);
    capture("match_3d_broadcast_zoom_out.bmp");

    scene.show_player_names = true;
    for (int index = 0; index < 60; ++index) frame(FRAME_SECONDS);
    capture("match_3d_names.bmp");
    scene.show_player_names = false;

    // Pitch focus: the view takes the whole window under an overlay HUD.
    const auto medianRenderMilliseconds = [&](int frames)
    {
      std::vector<float> timings;
      timings.reserve(static_cast<std::size_t>(frames));
      for (int index = 0; index < frames; ++index)
      {
        frame(FRAME_SECONDS);
        timings.push_back(scene.last_render_milliseconds);
      }
      std::ranges::sort(timings);
      return timings[timings.size() / 2];
    };
    press(SDLK_1);
    // Back to the default broadcast framing after the zoom checks.
    scene.pending_zoom_steps =
        std::log(1.0f / MatchRender3DTuning::Camera::MAX_ZOOM) /
        std::log(MatchRender3DTuning::Camera::ZOOM_STEP);
    press(SDLK_F);
    EXPECT_TRUE(scene.pitch_focus);
    const float focusMedian = medianRenderMilliseconds(90);
    std::cout << "[match-3d] focus 1280x800 broadcast render CPU median "
              << focusMedian << " ms, draw list "
              << ImGui::GetDrawData()->TotalVtxCount << " vertices\n";
    capture("match_3d_focus_broadcast.bmp");
    press(SDLK_2);
    for (int index = 0; index < 80; ++index) frame(FRAME_SECONDS);
    std::cout << "[match-3d] focus 1280x800 tactical render CPU median "
              << medianRenderMilliseconds(60) << " ms, draw list "
              << ImGui::GetDrawData()->TotalVtxCount << " vertices\n";
    capture("match_3d_focus_tactical.bmp");

    // A left drag over the view hands the camera to the free orbit camera.
    press(SDLK_1);
    for (int index = 0; index < 60; ++index) frame(FRAME_SECONDS);
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 centre(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.6f);
    io.AddMousePosEvent(centre.x, centre.y);
    frame(FRAME_SECONDS);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame(FRAME_SECONDS);
    for (int step = 1; step <= 20; ++step)
    {
      io.AddMousePosEvent(centre.x + static_cast<float>(step) * 12.0f,
                          centre.y - static_cast<float>(step) * 4.0f);
      frame(FRAME_SECONDS);
    }
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame(FRAME_SECONDS);
    EXPECT_EQ(scene.camera_mode, MatchCameraMode::FREE);
    for (int index = 0; index < 60; ++index) frame(FRAME_SECONDS);
    capture("match_3d_focus_free_orbit.bmp");
    // Wheel zoom and a right-drag pan over the view.
    io.AddMouseWheelEvent(0.0f, 4.0f);
    frame(FRAME_SECONDS);
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
    frame(FRAME_SECONDS);
    for (int step = 1; step <= 15; ++step)
    {
      io.AddMousePosEvent(centre.x + 240.0f - static_cast<float>(step) * 10.0f,
                          centre.y - 80.0f + static_cast<float>(step) * 6.0f);
      frame(FRAME_SECONDS);
    }
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
    for (int index = 0; index < 60; ++index) frame(FRAME_SECONDS);
    capture("match_3d_focus_free_pan_zoom.bmp");
    press(SDLK_B);
    EXPECT_TRUE(scene.free_follow_ball);
    press(SDLK_R);
    for (int index = 0; index < 90; ++index) frame(FRAME_SECONDS);
    std::cout << "[match-3d] focus 1280x800 free overview render CPU median "
              << medianRenderMilliseconds(60) << " ms, draw list "
              << ImGui::GetDrawData()->TotalVtxCount << " vertices\n";
    capture("match_3d_focus_free_reset.bmp");
    io.AddMousePosEvent(-1000.0f, -1000.0f);

    // A HiDPI-sized window (2560x1440 pixels at scale 1).
    SDL_SetWindowSize(window, 2560, 1440);
    SDL_PumpEvents();
    press(SDLK_1);
    for (int index = 0; index < 90; ++index) frame(FRAME_SECONDS);
    const float largeMedian = medianRenderMilliseconds(90);
    std::cout << "[match-3d] focus 2560x1440 broadcast render CPU median "
              << largeMedian << " ms, draw list "
              << ImGui::GetDrawData()->TotalVtxCount << " vertices\n";
    RecordProperty("render_3d_focus_1440p_median_microseconds",
                   static_cast<int>(largeMedian * 1000.0f));
    capture("match_3d_focus_1440p_broadcast.bmp");
    press(SDLK_2);
    for (int index = 0; index < 90; ++index) frame(FRAME_SECONDS);
    std::cout << "[match-3d] focus 2560x1440 tactical render CPU median "
              << medianRenderMilliseconds(60) << " ms, draw list "
              << ImGui::GetDrawData()->TotalVtxCount << " vertices\n";
    capture("match_3d_focus_1440p_tactical.bmp");
    press(SDLK_4);
    for (int index = 0; index < 90; ++index) frame(FRAME_SECONDS);
    capture("match_3d_focus_1440p_follow.bmp");
    press(SDLK_ESCAPE);
    EXPECT_FALSE(scene.pitch_focus);
    press(SDLK_1);
    for (int index = 0; index < 60; ++index) frame(FRAME_SECONDS);
    capture("match_3d_1440p_panels.bmp");
    scene.setSidePanelsHidden(true);
    for (int index = 0; index < 30; ++index) frame(FRAME_SECONDS);
    capture("match_3d_1440p_panels_hidden.bmp");
    scene.setSidePanelsHidden(false);

    // A 720p window in pitch focus (the smallest common full-screen size).
    SDL_SetWindowSize(window, 1280, 720);
    SDL_PumpEvents();
    press(SDLK_F);
    press(SDLK_1);
    for (int index = 0; index < 90; ++index) frame(FRAME_SECONDS);
    std::cout << "[match-3d] focus 1280x720 broadcast render CPU median "
              << medianRenderMilliseconds(90) << " ms, draw list "
              << ImGui::GetDrawData()->TotalVtxCount << " vertices\n";
    capture("match_3d_focus_720p_broadcast.bmp");
    press(SDLK_4);
    for (int index = 0; index < 90; ++index) frame(FRAME_SECONDS);
    std::cout << "[match-3d] focus 1280x720 follow render CPU median "
              << medianRenderMilliseconds(60) << " ms, draw list "
              << ImGui::GetDrawData()->TotalVtxCount << " vertices\n";
    capture("match_3d_focus_720p_follow.bmp");

    // Play on headless until the first goal, then watch the celebration
    // (full-match playback: highlights would skip it).
    press(SDLK_1);
    scene.setHighlightsOnly(false);
    bool scored = false;
    for (int step = 0; step < 60 * 100 && !scored; ++step)
    {
      scene.engine->advance(1.0f);
      scored = scene.engine->getState() == MatchState::GOAL;
    }
    if (scored)
    {
      for (int index = 0; index < 20; ++index) frame(FRAME_SECONDS);
      EXPECT_EQ(scene.engine->getState(), MatchState::GOAL);
      capture("match_3d_goal_sting.bmp");
      std::cout << "[match-3d] focus 1280x720 goal celebration render CPU "
                   "median "
                << medianRenderMilliseconds(60) << " ms, draw list "
                << ImGui::GetDrawData()->TotalVtxCount << " vertices\n";
      capture("match_3d_goal_celebration.bmp");
      // The TV director cuts to a close-up of the celebration.
      scene.camera_mode = MatchCameraMode::DIRECTOR;
      for (int index = 0; index < 30; ++index) frame(FRAME_SECONDS);
      if (scene.engine->getState() == MatchState::GOAL)
        capture("match_3d_director_goal.bmp");
      press(SDLK_3);
      for (int index = 0; index < 60; ++index) frame(FRAME_SECONDS);
      capture("match_3d_goal_end.bmp");
    }
    else
    {
      std::cout << "[match-3d] no goal in this match; celebration not shown\n";
    }
    press(SDLK_ESCAPE);
    SDL_SetWindowSize(window, 1280, 800);
    SDL_PumpEvents();

    press(SDLK_V);
    EXPECT_EQ(scene.view_mode, MatchViewMode::PITCH_2D);
    frame(FRAME_SECONDS);
    capture("match_2d_after_toggle.bmp");

    // The 2D tactical view: cost and captures with panels, then in pitch
    // focus at 1440p and 720p, in open play after any celebration.
    for (int step = 0;
         step < 600 && scene.engine->getState() != MatchState::PLAYING; ++step)
      scene.engine->advance(0.5f);
    scene.engine->advance(8.0f);
    for (int index = 0; index < 30; ++index) frame(FRAME_SECONDS);
    std::cout << "[match-2d] 1280x800 panels render CPU median "
              << medianRenderMilliseconds(90) << " ms, draw list "
              << ImGui::GetDrawData()->TotalVtxCount << " vertices\n";
    capture("match_2d_panels.bmp");
    SDL_SetWindowSize(window, 2560, 1440);
    SDL_PumpEvents();
    press(SDLK_F);
    for (int index = 0; index < 30; ++index) frame(FRAME_SECONDS);
    const float focus2d = medianRenderMilliseconds(90);
    std::cout << "[match-2d] focus 2560x1440 render CPU median " << focus2d
              << " ms, draw list " << ImGui::GetDrawData()->TotalVtxCount
              << " vertices\n";
    RecordProperty("render_2d_focus_1440p_median_microseconds",
                   static_cast<int>(focus2d * 1000.0f));
    EXPECT_LT(focus2d, 12.0f);
    capture("match_2d_focus_1440p.bmp");
    SDL_SetWindowSize(window, 1280, 720);
    SDL_PumpEvents();
    for (int index = 0; index < 30; ++index) frame(FRAME_SECONDS);
    std::cout << "[match-2d] focus 1280x720 render CPU median "
              << medianRenderMilliseconds(90) << " ms, draw list "
              << ImGui::GetDrawData()->TotalVtxCount << " vertices\n";
    capture("match_2d_focus_720p.bmp");
    press(SDLK_ESCAPE);
    SDL_SetWindowSize(window, 1280, 800);
    SDL_PumpEvents();

    // Presentation options the scene does not expose yet, rendered straight
    // through the renderers: the daylight preset and, in 2D, the pressure
    // overlay with an offside flash.
    const auto direct = [&](IMatchRenderer& target,
                            const MatchRenderOptions& options, int frames,
                            const std::vector<MatchEvent>* events)
    {
      std::vector<float> timings;
      timings.reserve(static_cast<std::size_t>(frames));
      for (int index = 0; index < frames; ++index)
      {
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        scene.engine->advance(FRAME_SECONDS);
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(display);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
        ImGui::Begin(
            "direct", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
        MatchRenderSnapshot snapshot = buildMatchRenderSnapshot(*scene.engine);
        if (events && index > 0) snapshot.events = events;
        // The 2D pitch keeps its proportions inside an apron.
        const MatchViewport pitch =
            options.pressureOverlay
                ? computeMatchViewport(40.0f, 40.0f, display.x - 80.0f,
                                       display.y - 80.0f)
                : MatchViewport{0.0f, 0.0f, display.x, display.y};
        const auto started = std::chrono::steady_clock::now();
        target.render(snapshot, options, pitch);
        timings.push_back(std::chrono::duration<float, std::milli>(
                              std::chrono::steady_clock::now() - started)
                              .count());
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
      }
      std::ranges::sort(timings);
      return timings[timings.size() / 2];
    };
    MatchRenderer3D dayRenderer;
    MatchRenderOptions dayOptions;
    dayOptions.frameSeconds = FRAME_SECONDS;
    dayOptions.dayLook = true;
    dayOptions.cameraMode = MatchCameraMode::BROADCAST;
    std::cout << "[match-3d] day look 1280x800 broadcast render CPU median "
              << direct(dayRenderer, dayOptions, 90, nullptr)
              << " ms (renderer only), draw list "
              << ImGui::GetDrawData()->TotalVtxCount << " vertices\n";
    capture("match_3d_day_broadcast.bmp");
    dayOptions.cameraMode = MatchCameraMode::PLAYER_FOLLOW;
    direct(dayRenderer, dayOptions, 90, nullptr);
    capture("match_3d_day_follow.bmp");

    // Night and day at 720p and at 2560x1440 with the HiDPI scale of 2,
    // broadcast and follow cameras, straight through the renderer.
    const auto lookAround = [&](const std::string& prefix, bool day)
    {
      MatchRenderer3D lookRenderer;
      MatchRenderOptions options;
      options.frameSeconds = FRAME_SECONDS;
      options.dayLook = day;
      options.cameraMode = MatchCameraMode::BROADCAST;
      const std::string name = "match_3d_" + prefix + (day ? "_day" : "_night");
      const float broadcast = direct(lookRenderer, options, 60, nullptr);
      std::cout << "[match-3d] " << name << " broadcast render CPU median "
                << broadcast << " ms (renderer only), draw list "
                << ImGui::GetDrawData()->TotalVtxCount << " vertices\n";
      capture(name + "_broadcast.bmp");
      options.cameraMode = MatchCameraMode::PLAYER_FOLLOW;
      const float follow = direct(lookRenderer, options, 60, nullptr);
      std::cout << "[match-3d] " << name << " follow render CPU median "
                << follow << " ms (renderer only), draw list "
                << ImGui::GetDrawData()->TotalVtxCount << " vertices\n";
      capture(name + "_follow.bmp");
      return broadcast;
    };
    SDL_SetWindowSize(window, 1280, 720);
    SDL_PumpEvents();
    lookAround("720p", false);
    lookAround("720p", true);
    SDL_SetWindowSize(window, 2560, 1440);
    SDL_PumpEvents();
    Theme::apply(Theme::Appearance{}, 2.0f);
    const float hidpiNight = lookAround("1440p2x", false);
    const float hidpiDay = lookAround("1440p2x", true);
    RecordProperty("render_3d_1440p2x_night_median_microseconds",
                   static_cast<int>(hidpiNight * 1000.0f));
    RecordProperty("render_3d_1440p2x_day_median_microseconds",
                   static_cast<int>(hidpiDay * 1000.0f));
    EXPECT_LT(hidpiNight, 12.0f);
    EXPECT_LT(hidpiDay, 12.0f);
    // The next goal at 1440p, by night and in a second renderer by day.
    bool scoredAgain = false;
    for (int step = 0; step < 60 * 100 && !scoredAgain; ++step)
    {
      scene.engine->advance(1.0f);
      scoredAgain = scene.engine->getState() == MatchState::GOAL;
    }
    if (scoredAgain)
    {
      MatchRenderer3D goalRenderer;
      MatchRenderOptions goalOptions;
      goalOptions.frameSeconds = FRAME_SECONDS;
      goalOptions.cameraMode = MatchCameraMode::BROADCAST;
      direct(goalRenderer, goalOptions, 30, nullptr);
      capture("match_3d_1440p2x_night_goal.bmp");
      goalOptions.cameraMode = MatchCameraMode::DIRECTOR;
      direct(goalRenderer, goalOptions, 40, nullptr);
      if (scene.engine->getState() == MatchState::GOAL)
        capture("match_3d_1440p2x_night_goal_director.bmp");
    }
    Theme::apply(Theme::Appearance{}, 1.0f);
    SDL_SetWindowSize(window, 1280, 800);
    SDL_PumpEvents();

    MatchRenderer2D overlayRenderer;
    MatchRenderOptions overlayOptions;
    overlayOptions.frameSeconds = FRAME_SECONDS;
    overlayOptions.pressureOverlay = true;
    std::vector<MatchEvent> offsideEvents = scene.engine->getEvents();
    MatchEvent& offside = offsideEvents.emplace_back();
    offside.type = MatchEventType::OFFSIDE;
    offside.hasTeam = true;
    offside.position = {0.7f, 0.4f};
    std::cout << "[match-2d] pressure overlay 1280x800 render CPU median "
              << direct(overlayRenderer, overlayOptions, 40, &offsideEvents)
              << " ms (renderer only), draw list "
              << ImGui::GetDrawData()->TotalVtxCount << " vertices\n";
    capture("match_2d_pressure_offside.bmp");
  }

  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
}
