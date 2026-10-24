// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <cmath>
#include <cstdint>

#include "global/types.h"

struct ClubIdentity;

/** Colours of one strip, packed as `IM_COL32`. */
struct KitColors
{
  ImU32 shirt = 0;
  ImU32 trim = 0;
  ImU32 shorts = 0;
  ImU32 socks = 0;
};

/** The four strips worn in a match. */
struct MatchKits
{
  KitColors home;
  KitColors away;
  KitColors homeGoalkeeper;
  KitColors awayGoalkeeper;
};

/** A club's own colours, packed as `IM_COL32`. */
struct ClubColours
{
  ImU32 primary = 0;
  ImU32 secondary = 0;
};

/**
 * Picks the strips of a match. Clubs with colours in the data pack wear them
 * (primary shirt, secondary trim and shorts); others get a deterministic
 * original strip from their id. The away side switches to its reversed
 * colours, then to a change strip, when its shirt is too close to the home
 * shirt, and both goalkeepers wear colours distinct from every outfield
 * shirt and from each other. Every view (2D, 3D, HUD swatches) uses this so
 * a club always looks the same.
 */
MatchKits chooseMatchKits(TeamID homeTeam, TeamID awayTeam);

/** Same, with explicit club colours (null: the hashed original strip). */
MatchKits chooseMatchKits(TeamID homeTeam, TeamID awayTeam,
                          const ClubColours* homeColours,
                          const ClubColours* awayColours);

/**
 * The club's data-pack identity (short name, colours, stadium), read once
 * per process and cached; null for clubs the pack does not describe.
 */
const ClubIdentity* findClubIdentity(TeamID team);

/** Converts a 0xRRGGBB colour to an opaque `IM_COL32`. */
constexpr ImU32 kitColorFromRgb(std::uint32_t rgb)
{
  return IM_COL32((rgb >> 16U) & 0xFFU, (rgb >> 8U) & 0xFFU, rgb & 0xFFU, 255);
}

/** Perceptual ("redmean") RGB distance between two packed colours. */
float kitColorDistance(ImU32 first, ImU32 second);

/** Minimum `kitColorDistance` treated as a readable contrast. */
inline constexpr float KIT_CLASH_DISTANCE = 190.0f;

/**
 * Colour of the numbers and names printed on a shirt: the trim when it
 * contrasts with the shirt, otherwise white or near-black, whichever
 * stands out more.
 */
ImU32 kitNumberColor(const KitColors& kit);

/** Goalkeeper gloves: the stock colour that stands out most on the shirt. */
ImU32 goalkeeperGloveColor(const KitColors& kit);

/** Scales the RGB channels of a packed colour, keeping its alpha. */
inline ImU32 shadeColor(ImU32 color, float factor)
{
  const auto scale = [factor](ImU32 value, int shift)
  {
    const float scaled =
        static_cast<float>((value >> shift) & 0xFFU) * factor + 0.5f;
    float bounded = scaled;
    if (scaled < 0.0f)
      bounded = 0.0f;
    else if (scaled > 255.0f)
      bounded = 255.0f;
    return static_cast<ImU32>(bounded) << shift;
  };
  return scale(color, IM_COL32_R_SHIFT) | scale(color, IM_COL32_G_SHIFT) |
         scale(color, IM_COL32_B_SHIFT) | (color & IM_COL32_A_MASK);
}

/** Linear blend of two packed colours (alpha included). */
inline ImU32 mixColor(ImU32 first, ImU32 second, float t)
{
  const auto blend = [t](ImU32 a, ImU32 b, int shift)
  {
    const auto from = static_cast<float>((a >> shift) & 0xFFU);
    const auto to = static_cast<float>((b >> shift) & 0xFFU);
    return static_cast<ImU32>(std::lerp(from, to, t) + 0.5f) << shift;
  };
  return blend(first, second, IM_COL32_R_SHIFT) |
         blend(first, second, IM_COL32_G_SHIFT) |
         blend(first, second, IM_COL32_B_SHIFT) |
         blend(first, second, IM_COL32_A_SHIFT);
}

/**
 * Integer versions for hot loops: blend with `t256` in 0..256 and scale
 * the RGB channels by `factor256` / 256 (at most 2x, clamped at 255).
 */
constexpr ImU32 mixColor256(ImU32 first, ImU32 second, std::uint32_t t256)
{
  const std::uint32_t keep = 256U - t256;
  const std::uint32_t evenChannels =
      (((first & 0x00FF00FFU) * keep + (second & 0x00FF00FFU) * t256) >> 8U) &
      0x00FF00FFU;
  const std::uint32_t oddChannels = ((((first >> 8U) & 0x00FF00FFU) * keep +
                                      ((second >> 8U) & 0x00FF00FFU) * t256) >>
                                     8U) &
                                    0x00FF00FFU;
  return evenChannels | (oddChannels << 8U);
}

constexpr ImU32 shadeColor256(ImU32 color, std::uint32_t factor256)
{
  const auto channel = [&](int shift)
  {
    const std::uint32_t value = (((color >> shift) & 0xFFU) * factor256) >> 8U;
    return (value > 255U ? 255U : value) << shift;
  };
  return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) |
         channel(IM_COL32_B_SHIFT) | (color & IM_COL32_A_MASK);
}

/** Replaces the alpha channel of a packed colour. */
constexpr ImU32 withAlpha(ImU32 color, std::uint8_t alpha)
{
  return (color & ~IM_COL32_A_MASK) |
         (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

/** Small deterministic integer hash shared by the 3D view's cosmetics. */
constexpr std::uint32_t cosmeticHash(std::uint32_t value)
{
  value ^= value >> 16;
  value *= 0x7feb352dU;
  value ^= value >> 15;
  value *= 0x846ca68bU;
  value ^= value >> 16;
  return value;
}
