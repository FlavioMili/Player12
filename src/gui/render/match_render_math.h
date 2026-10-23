// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>

#include "global/types.h"
#include "model/match_tuning.h"

/**
 * Minimal 3D math used by the software-projected match renderer.
 *
 * World space is metric and right-handed: x runs along the pitch length
 * (0..105 m, home attacks +x), y across its width (0..68 m, y = 0 is the near
 * touchline of the broadcast camera) and z points up. Nothing here depends on
 * ImGui, so the projection and clipping rules are unit-testable headless.
 */
namespace RenderMath
{
struct Vec3
{
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  friend constexpr Vec3 operator+(Vec3 a, Vec3 b)
  {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
  }
  friend constexpr Vec3 operator-(Vec3 a, Vec3 b)
  {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
  }
  friend constexpr Vec3 operator*(Vec3 a, float s)
  {
    return {a.x * s, a.y * s, a.z * s};
  }
};

struct Vec4
{
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 0.0f;
};

constexpr float dot(Vec3 a, Vec3 b)
{
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
constexpr Vec3 cross(Vec3 a, Vec3 b)
{
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a)
{
  const float magnitude = length(a);
  return magnitude > 1e-6f ? a * (1.0f / magnitude) : Vec3{};
}
constexpr Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

/** Row-major 4x4 matrix; points are column vectors (`clip = M * p`). */
struct Mat4
{
  std::array<float, 16> m{};

  static constexpr Mat4 identity()
  {
    Mat4 result;
    result.m[0] = 1.0f;
    result.m[5] = 1.0f;
    result.m[10] = 1.0f;
    result.m[15] = 1.0f;
    return result;
  }

  constexpr Vec4 transform(Vec3 p) const
  {
    return {m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3],
            m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
            m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11],
            m[12] * p.x + m[13] * p.y + m[14] * p.z + m[15]};
  }

  friend constexpr Mat4 operator*(const Mat4& a, const Mat4& b)
  {
    Mat4 result;
    for (std::size_t row = 0; row < 4; ++row)
      for (std::size_t column = 0; column < 4; ++column)
      {
        float sum = 0.0f;
        for (std::size_t k = 0; k < 4; ++k)
          sum += a.m[row * 4 + k] * b.m[k * 4 + column];
        result.m[row * 4 + column] = sum;
      }
    return result;
  }
};

/** Right-handed view matrix; the camera looks down its local -Z axis. */
inline Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up)
{
  const Vec3 forward = normalize(target - eye);
  const Vec3 side = normalize(cross(forward, up));
  const Vec3 cameraUp = cross(side, forward);
  Mat4 view = Mat4::identity();
  view.m = {side.x,     side.y,     side.z,     -dot(side, eye),
            cameraUp.x, cameraUp.y, cameraUp.z, -dot(cameraUp, eye),
            -forward.x, -forward.y, -forward.z, dot(forward, eye),
            0.0f,       0.0f,       0.0f,       1.0f};
  return view;
}

/** OpenGL-style perspective; clip.w equals the view-space depth. */
inline Mat4 perspective(float verticalFovRadians, float aspect, float nearPlane,
                        float farPlane)
{
  const float focal = 1.0f / std::tan(verticalFovRadians * 0.5f);
  Mat4 projection;
  projection.m[0] = focal / aspect;
  projection.m[5] = focal;
  projection.m[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
  projection.m[11] = 2.0f * farPlane * nearPlane / (nearPlane - farPlane);
  projection.m[14] = -1.0f;
  return projection;
}

/** Screen rectangle in pixels (top-left origin, y down). */
struct ScreenRect
{
  float x = 0.0f;
  float y = 0.0f;
  float width = 1.0f;
  float height = 1.0f;
};

/** Projected point: pixel position plus view-space depth in metres. */
struct ScreenPoint
{
  float x = 0.0f;
  float y = 0.0f;
  float depth = 0.0f;
};

/** Everything needed to project world points for one frame. */
struct Projection
{
  Mat4 viewProjection = Mat4::identity();
  Vec3 eye;
  Vec3 forward{0.0f, 1.0f, 0.0f};
  /** Camera right and up axes in world space (for un-projecting). */
  Vec3 side{1.0f, 0.0f, 0.0f};
  Vec3 up{0.0f, 0.0f, 1.0f};
  ScreenRect rect;
  float nearPlane = 0.5f;
  /** Pixels per metre at one metre depth (for sizing billboards). */
  float focalPixels = 1.0f;

  static Projection make(Vec3 eye, Vec3 target, float verticalFovRadians,
                         float nearPlane, float farPlane, ScreenRect rect)
  {
    Projection projection;
    projection.eye = eye;
    projection.forward = normalize(target - eye);
    projection.side = normalize(cross(projection.forward, {0.0f, 0.0f, 1.0f}));
    projection.up = cross(projection.side, projection.forward);
    projection.rect = rect;
    projection.nearPlane = nearPlane;
    const float aspect = rect.width / (rect.height > 0.0f ? rect.height : 1.0f);
    projection.viewProjection =
        perspective(verticalFovRadians, aspect, nearPlane, farPlane) *
        lookAt(eye, target, {0.0f, 0.0f, 1.0f});
    projection.focalPixels =
        rect.height * 0.5f / std::tan(verticalFovRadians * 0.5f);
    return projection;
  }

  Vec4 toClip(Vec3 point) const { return viewProjection.transform(point); }

  /** Converts a clip-space point with w >= nearPlane to pixels. */
  ScreenPoint clipToScreen(Vec4 clip) const
  {
    const float inverseW = 1.0f / clip.w;
    return {rect.x + (clip.x * inverseW * 0.5f + 0.5f) * rect.width,
            rect.y + (0.5f - clip.y * inverseW * 0.5f) * rect.height, clip.w};
  }

  /** Projects a point; returns false when it lies behind the near plane. */
  bool project(Vec3 point, ScreenPoint& out) const
  {
    const Vec4 clip = toClip(point);
    if (clip.w < nearPlane) return false;
    out = clipToScreen(clip);
    return true;
  }

  float depth(Vec3 point) const { return dot(point - eye, forward); }

  /** World-space direction (not normalised) of the ray through a pixel. */
  Vec3 rayThrough(float pixelX, float pixelY) const
  {
    const float right = (pixelX - (rect.x + rect.width * 0.5f)) / focalPixels;
    const float upward = ((rect.y + rect.height * 0.5f) - pixelY) / focalPixels;
    return forward + side * right + up * upward;
  }

  /**
   * Intersects the ray through a pixel with the horizontal plane at
   * `planeHeight`; false when the ray points away from it (sky pixels).
   */
  bool groundPointAt(float pixelX, float pixelY, Vec3& out,
                     float planeHeight = 0.0f) const
  {
    const Vec3 direction = rayThrough(pixelX, pixelY);
    if (direction.z > -1e-4f) return false;
    const float distance = (planeHeight - eye.z) / direction.z;
    if (distance <= 0.0f) return false;
    out = eye + direction * distance;
    return true;
  }
};

/**
 * Clips a convex polygon against the near plane (w >= nearPlane) using
 * Sutherland-Hodgman in homogeneous clip space. `wOf(vertex)` returns the
 * clip-space w and `interpolate(a, b, t)` blends two vertices (including any
 * attributes). `output` needs room for `input.size() + 1` vertices. Returns
 * the number of vertices written (0 when entirely behind the camera).
 */
template <typename Vertex, typename WOf, typename Interpolate>
std::size_t clipConvexToNearPlane(std::span<const Vertex> input,
                                  std::span<Vertex> output, float nearPlane,
                                  WOf wOf, Interpolate interpolate)
{
  std::size_t written = 0;
  const std::size_t count = input.size();
  for (std::size_t index = 0; index < count; ++index)
  {
    const Vertex& current = input[index];
    const Vertex& next = input[(index + 1) % count];
    const float currentDistance = wOf(current) - nearPlane;
    const float nextDistance = wOf(next) - nearPlane;
    if (currentDistance >= 0.0f && written < output.size())
      output[written++] = current;
    if ((currentDistance >= 0.0f) != (nextDistance >= 0.0f) &&
        written < output.size())
    {
      output[written++] = interpolate(
          current, next, currentDistance / (currentDistance - nextDistance));
    }
  }
  return written;
}

/** Linear blend of two clip-space points. */
constexpr Vec4 lerp(Vec4 a, Vec4 b, float t)
{
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
          a.w + (b.w - a.w) * t};
}

/** Near-plane clip of plain clip-space points (see clipConvexToNearPlane). */
inline std::size_t clipPolygonToNearPlane(std::span<const Vec4> input,
                                          std::span<Vec4> output,
                                          float nearPlane)
{
  return clipConvexToNearPlane(
      input, output, nearPlane, [](const Vec4& vertex) { return vertex.w; },
      [](const Vec4& a, const Vec4& b, float t) { return lerp(a, b, t); });
}

/** Wraps an angle to [-pi, pi]. */
inline float wrapAngle(float angle)
{
  constexpr float TWO_PI = 2.0f * std::numbers::pi_v<float>;
  angle = std::fmod(angle + std::numbers::pi_v<float>, TWO_PI);
  if (angle < 0.0f) angle += TWO_PI;
  return angle - std::numbers::pi_v<float>;
}

/** Interpolates two angles along the shortest arc. */
inline float lerpAngle(float from, float to, float t)
{
  return from + wrapAngle(to - from) * t;
}

/** Frame-rate independent exponential smoothing factor. */
inline float dampingFactor(float ratePerSecond, float deltaSeconds)
{
  return 1.0f - std::exp(-ratePerSecond * deltaSeconds);
}

/**
 * Hands out shirt numbers for one team in a match. Starters get the classic
 * number of their role when it is free (keeper 1, full backs 2 and 3,
 * centre backs 4 and 5, holding midfielder 6, wide right 7, central 8,
 * striker 9, playmaker 10, wide left 11), else the lowest free number up to
 * 11; substitutes take 12 upwards. A number is never given out twice.
 */
class ShirtNumbers
{
 public:
  int take(PlayerRole role, bool starter)
  {
    if (starter)
    {
      for (const int number : preferred(role))
        if (number > 0 && claim(number)) return number;
      for (int number = 1; number <= 11; ++number)
        if (claim(number)) return number;
    }
    for (int number = 12; number < MAX_NUMBER; ++number)
      if (claim(number)) return number;
    return 0;
  }

 private:
  static constexpr int MAX_NUMBER = 64;
  std::uint64_t used = 0;

  bool claim(int number)
  {
    const std::uint64_t bit = std::uint64_t{1} << number;
    if ((used & bit) != 0U) return false;
    used |= bit;
    return true;
  }

  static std::array<int, 3> preferred(PlayerRole role)
  {
    using enum PlayerRole;
    switch (role)
    {
      case GK:
        return {1, 0, 0};
      case RB:
        return {2, 3, 0};
      case LB:
        return {3, 2, 0};
      case CB:
        return {4, 5, 6};
      case CDM:
        return {6, 4, 8};
      case CM:
        return {8, 6, 10};
      case CAM:
        return {10, 8, 7};
      case RM:
      case RW:
        return {7, 11, 0};
      case LM:
      case LW:
        return {11, 7, 0};
      case ST:
        return {9, 10, 11};
      case UNKNOWN:
        break;
    }
    return {0, 0, 0};
  }
};

/** Maps normalised engine coordinates to metric world space (z = 0). */
constexpr Vec3 worldFromPitch(Vector2F normalized, float heightMetres = 0.0f)
{
  return {normalized.x * MatchTuning::Pitch::LENGTH_METRES,
          (1.0f - normalized.y) * MatchTuning::Pitch::WIDTH_METRES,
          heightMetres};
}

/**
 * Converts an engine facing angle (measured in normalised pitch space) to a
 * world yaw around +z, accounting for the anisotropic pitch scale and the
 * flipped y axis.
 */
inline float worldYawFromFacing(float facingAngle)
{
  return std::atan2(-std::sin(facingAngle) * MatchTuning::Pitch::WIDTH_METRES,
                    std::cos(facingAngle) * MatchTuning::Pitch::LENGTH_METRES);
}
}  // namespace RenderMath
