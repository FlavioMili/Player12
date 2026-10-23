// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "role_utils.h"

#include <array>
#include <cstddef>
#include <utility>

#include "global/language_manager.h"

namespace
{
constexpr std::size_t ROLE_COUNT =
    static_cast<std::size_t>(std::to_underlying(PlayerRole::UNKNOWN));

constexpr std::array<const char*, ROLE_COUNT> SHORT_KEYS = {
    "ROLE_SHORT_GK",  "ROLE_SHORT_CB", "ROLE_SHORT_LB",  "ROLE_SHORT_RB",
    "ROLE_SHORT_CDM", "ROLE_SHORT_CM", "ROLE_SHORT_CAM", "ROLE_SHORT_LM",
    "ROLE_SHORT_RM",  "ROLE_SHORT_LW", "ROLE_SHORT_RW",  "ROLE_SHORT_ST"};

constexpr std::array<const char*, ROLE_COUNT> LONG_KEYS = {
    "ROLE_LONG_GK",  "ROLE_LONG_CB", "ROLE_LONG_LB",  "ROLE_LONG_RB",
    "ROLE_LONG_CDM", "ROLE_LONG_CM", "ROLE_LONG_CAM", "ROLE_LONG_LM",
    "ROLE_LONG_RM",  "ROLE_LONG_LW", "ROLE_LONG_RW",  "ROLE_LONG_ST"};

constexpr const char* NO_ROLE = "-";
}  // namespace

const char* RoleUtils::shortName(PlayerRole role)
{
  const auto index = static_cast<std::size_t>(std::to_underlying(role));
  return index < ROLE_COUNT ? LOC(SHORT_KEYS[index]) : NO_ROLE;
}

const char* RoleUtils::longName(PlayerRole role)
{
  const auto index = static_cast<std::size_t>(std::to_underlying(role));
  return index < ROLE_COUNT ? LOC(LONG_KEYS[index]) : NO_ROLE;
}

std::string RoleUtils::shortNameArg(PlayerRole role)
{
  const auto index = static_cast<std::size_t>(std::to_underlying(role));
  return index < ROLE_COUNT ? std::string("@") + SHORT_KEYS[index]
                            : std::string(NO_ROLE);
}

std::string RoleUtils::toString(PlayerRole role)
{
  switch (role)
  {
    using enum PlayerRole;
    case GK:
      return "GK";
    case CB:
      return "CB";
    case LB:
      return "LB";
    case RB:
      return "RB";
    case CDM:
      return "CDM";
    case CM:
      return "CM";
    case CAM:
      return "CAM";
    case LM:
      return "LM";
    case RM:
      return "RM";
    case LW:
      return "LW";
    case RW:
      return "RW";
    case ST:
      return "ST";
    default:
      return "UNKNOWN";
  }
}

PlayerRole RoleUtils::fromString(std::string_view role_str)
{
  using enum PlayerRole;
  if (role_str == "GK") return GK;
  if (role_str == "CB") return CB;
  if (role_str == "LB") return LB;
  if (role_str == "RB") return RB;
  if (role_str == "CDM") return CDM;
  if (role_str == "CM") return CM;
  if (role_str == "CAM") return CAM;
  if (role_str == "LM") return LM;
  if (role_str == "RM") return RM;
  if (role_str == "LW") return LW;
  if (role_str == "RW") return RW;
  if (role_str == "ST") return ST;

  // Handle legacy strings for backwards compatibility if needed
  if (role_str == "Goalkeeper") return GK;
  if (role_str == "Defender") return CB;
  if (role_str == "Midfielder") return CM;
  if (role_str == "Striker") return ST;

  return UNKNOWN;
}

Vector2F RoleUtils::getBaseCoordinate(PlayerRole role)
{
  // Assume Home team plays from Left (0.0) to Right (1.0).
  // Y goes from Top (0.0) to Bottom (1.0).
  switch (role)
  {
    using enum PlayerRole;
    case GK:
      return {0.05f, 0.50f};
    case CB:
      return {0.15f, 0.50f};
    case LB:
      return {0.15f, 0.15f};
    case RB:
      return {0.15f, 0.85f};
    case CDM:
      return {0.30f, 0.50f};
    case CM:
      return {0.45f, 0.50f};
    case CAM:
      return {0.60f, 0.50f};
    case LM:
      return {0.45f, 0.15f};
    case RM:
      return {0.45f, 0.85f};
    case LW:
      return {0.75f, 0.15f};
    case RW:
      return {0.75f, 0.85f};
    case ST:
      return {0.85f, 0.50f};
    default:
      return {0.50f, 0.50f};
  }
}

std::string RoleUtils::getBroadCategory(PlayerRole role)
{
  switch (role)
  {
    using enum PlayerRole;
    case GK:
      return "Goalkeeper";
    case CB:
    case LB:
    case RB:
      return "Defender";
    case CDM:
    case CM:
    case CAM:
    case LM:
    case RM:
      return "Midfielder";
    case LW:
    case RW:
    case ST:
      return "Striker";
    default:
      return "Unknown";
  }
}
