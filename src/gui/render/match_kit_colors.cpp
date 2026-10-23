// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/render/match_kit_colors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <unordered_map>

#include "database/datagenerator.h"

namespace
{
constexpr ImU32 WHITE = IM_COL32(240, 241, 244, 255);
constexpr ImU32 NAVY = IM_COL32(22, 34, 78, 255);
constexpr ImU32 BLACK = IM_COL32(28, 28, 32, 255);

constexpr std::array<KitColors, 14> HOME_KITS{{
    {IM_COL32(200, 28, 40, 255), WHITE, WHITE, IM_COL32(200, 28, 40, 255)},
    {IM_COL32(28, 72, 190, 255), WHITE, WHITE, IM_COL32(28, 72, 190, 255)},
    {IM_COL32(112, 176, 232, 255), NAVY, WHITE, IM_COL32(112, 176, 232, 255)},
    {IM_COL32(18, 128, 68, 255), WHITE, WHITE, IM_COL32(18, 128, 68, 255)},
    {IM_COL32(246, 204, 36, 255), NAVY, NAVY, IM_COL32(246, 204, 36, 255)},
    {BLACK, WHITE, BLACK, BLACK},
    {WHITE, NAVY, NAVY, WHITE},
    {IM_COL32(242, 118, 28, 255), BLACK, BLACK, IM_COL32(242, 118, 28, 255)},
    {IM_COL32(122, 28, 52, 255), IM_COL32(120, 190, 235, 255), WHITE,
     IM_COL32(122, 28, 52, 255)},
    {IM_COL32(98, 48, 150, 255), IM_COL32(236, 190, 60, 255), WHITE,
     IM_COL32(98, 48, 150, 255)},
    {NAVY, IM_COL32(210, 40, 50, 255), NAVY, NAVY},
    {IM_COL32(0, 138, 140, 255), WHITE, BLACK, IM_COL32(0, 138, 140, 255)},
    {IM_COL32(228, 108, 158, 255), BLACK, BLACK, IM_COL32(228, 108, 158, 255)},
    {IM_COL32(124, 128, 138, 255), IM_COL32(200, 240, 60, 255), BLACK,
     IM_COL32(124, 128, 138, 255)},
}};

/// Change strips tried in order when the preferred away shirt clashes.
constexpr std::array<KitColors, 4> CHANGE_KITS{{
    {WHITE, BLACK, BLACK, WHITE},
    {BLACK, IM_COL32(246, 204, 36, 255), BLACK, BLACK},
    {IM_COL32(246, 204, 36, 255), NAVY, NAVY, IM_COL32(246, 204, 36, 255)},
    {IM_COL32(112, 176, 232, 255), NAVY, NAVY, IM_COL32(112, 176, 232, 255)},
}};

constexpr std::array<KitColors, 6> GOALKEEPER_KITS{{
    {IM_COL32(150, 226, 40, 255), BLACK, BLACK, IM_COL32(150, 226, 40, 255)},
    {IM_COL32(255, 140, 22, 255), BLACK, BLACK, IM_COL32(255, 140, 22, 255)},
    {IM_COL32(222, 42, 164, 255), BLACK, BLACK, IM_COL32(222, 42, 164, 255)},
    {IM_COL32(38, 204, 222, 255), NAVY, NAVY, IM_COL32(38, 204, 222, 255)},
    {IM_COL32(24, 24, 26, 255), IM_COL32(150, 226, 40, 255),
     IM_COL32(24, 24, 26, 255), IM_COL32(24, 24, 26, 255)},
    {IM_COL32(250, 232, 40, 255), BLACK, BLACK, IM_COL32(250, 232, 40, 255)},
}};

bool contrastsWithAll(ImU32 candidate, std::initializer_list<ImU32> others)
{
  return std::ranges::all_of(
      others, [candidate](ImU32 other)
      { return kitColorDistance(candidate, other) >= KIT_CLASH_DISTANCE; });
}

KitColors pickGoalkeeperKit(TeamID team, std::initializer_list<ImU32> avoid)
{
  const std::size_t start =
      cosmeticHash(team + 0x9e37U) % GOALKEEPER_KITS.size();
  for (std::size_t offset = 0; offset < GOALKEEPER_KITS.size(); ++offset)
  {
    const KitColors& kit =
        GOALKEEPER_KITS[(start + offset) % GOALKEEPER_KITS.size()];
    if (contrastsWithAll(kit.shirt, avoid)) return kit;
  }
  return GOALKEEPER_KITS[start];
}

const std::unordered_map<TeamID, ClubIdentity>& clubIdentities()
{
  // The pack is read-only while the game runs: parse it once.
  static const std::unordered_map<TeamID, ClubIdentity> identities =
      DataGenerator::loadClubIdentities();
  return identities;
}

std::optional<ClubColours> packColours(TeamID team)
{
  const ClubIdentity* identity = findClubIdentity(team);
  if (!identity) return std::nullopt;
  return ClubColours{kitColorFromRgb(identity->primary_colour),
                     kitColorFromRgb(identity->secondary_colour)};
}

KitColors clubKit(const ClubColours& colours)
{
  return {colours.primary, colours.secondary, colours.secondary,
          colours.primary};
}
}  // namespace

const ClubIdentity* findClubIdentity(TeamID team)
{
  const auto& identities = clubIdentities();
  const auto found = identities.find(team);
  return found != identities.end() ? &found->second : nullptr;
}

float kitColorDistance(ImU32 first, ImU32 second)
{
  const auto channel = [](ImU32 color, int shift)
  { return static_cast<float>((color >> shift) & 0xFFU); };
  const float redA = channel(first, IM_COL32_R_SHIFT);
  const float redB = channel(second, IM_COL32_R_SHIFT);
  const float meanRed = (redA + redB) * 0.5f;
  const float dr = redA - redB;
  const float dg =
      channel(first, IM_COL32_G_SHIFT) - channel(second, IM_COL32_G_SHIFT);
  const float db =
      channel(first, IM_COL32_B_SHIFT) - channel(second, IM_COL32_B_SHIFT);
  return std::sqrt((2.0f + meanRed / 256.0f) * dr * dr + 4.0f * dg * dg +
                   (2.0f + (255.0f - meanRed) / 256.0f) * db * db);
}

ImU32 kitNumberColor(const KitColors& kit)
{
  if (kitColorDistance(kit.trim, kit.shirt) >= KIT_CLASH_DISTANCE)
    return kit.trim;
  constexpr ImU32 LIGHT = IM_COL32(246, 247, 250, 255);
  constexpr ImU32 DARK = IM_COL32(18, 20, 26, 255);
  return kitColorDistance(LIGHT, kit.shirt) >= kitColorDistance(DARK, kit.shirt)
             ? LIGHT
             : DARK;
}

ImU32 goalkeeperGloveColor(const KitColors& kit)
{
  constexpr std::array<ImU32, 4> GLOVES{
      IM_COL32(236, 240, 232, 255), IM_COL32(170, 230, 40, 255),
      IM_COL32(250, 120, 30, 255), IM_COL32(30, 32, 36, 255)};
  ImU32 best = GLOVES[0];
  float bestDistance = -1.0f;
  for (const ImU32 glove : GLOVES)
  {
    const float distance = kitColorDistance(glove, kit.shirt);
    if (distance > bestDistance)
    {
      bestDistance = distance;
      best = glove;
    }
  }
  return best;
}

MatchKits chooseMatchKits(TeamID homeTeam, TeamID awayTeam)
{
  const std::optional<ClubColours> home = packColours(homeTeam);
  const std::optional<ClubColours> away = packColours(awayTeam);
  return chooseMatchKits(homeTeam, awayTeam, home ? &*home : nullptr,
                         away ? &*away : nullptr);
}

MatchKits chooseMatchKits(TeamID homeTeam, TeamID awayTeam,
                          const ClubColours* homeColours,
                          const ClubColours* awayColours)
{
  MatchKits kits;
  kits.home = homeColours
                  ? clubKit(*homeColours)
                  : HOME_KITS[cosmeticHash(homeTeam) % HOME_KITS.size()];
  kits.away = awayColours
                  ? clubKit(*awayColours)
                  : HOME_KITS[cosmeticHash(awayTeam) % HOME_KITS.size()];
  // A club's own reversed colours come before any generic change strip.
  if (const bool clash = homeTeam == awayTeam ||
                         !contrastsWithAll(kits.away.shirt, {kits.home.shirt});
      clash && awayColours && homeTeam != awayTeam &&
      contrastsWithAll(awayColours->secondary, {kits.home.shirt}))
  {
    kits.away = {awayColours->secondary, awayColours->primary,
                 awayColours->primary, awayColours->secondary};
  }
  else if (clash)
  {
    for (const KitColors& change : CHANGE_KITS)
    {
      if (contrastsWithAll(change.shirt, {kits.home.shirt}))
      {
        kits.away = change;
        break;
      }
    }
  }
  kits.homeGoalkeeper =
      pickGoalkeeperKit(homeTeam, {kits.home.shirt, kits.away.shirt});
  kits.awayGoalkeeper = pickGoalkeeperKit(
      awayTeam, {kits.home.shirt, kits.away.shirt, kits.homeGoalkeeper.shirt});
  return kits;
}
