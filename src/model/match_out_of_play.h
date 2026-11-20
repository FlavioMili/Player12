// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

/**
 * The match's "out of play": what happens when the ball leaves play and how
 * play restarts - a goal (and who scored it), the woodwork, a corner, goal
 * kick or throw-in, the ball settling in the net, and every restart set up
 * and released (throw-ins, goal kicks, corners, free kicks, penalties, the
 * taker placed). Who takes a restart, where everyone stands for it and what
 * the taker does with the ball are the brains' (match_brain.h); how he
 * strikes it, a direct free kick against the wall included, is his body's
 * (match_body.h); the kick-off is shared.
 *
 * AARDVARK is the inherited version, kept frozen for comparison; BINTURONG
 * and later ones are the ones being developed. One per match, chosen before
 * kickoff (MatchEngine::setOutOfPlay).
 */
enum class OutOfPlayId : std::uint8_t
{
  AARDVARK,
  BINTURONG,
};

inline constexpr std::array<OutOfPlayId, 2> ALL_OUT_OF_PLAY = {
    OutOfPlayId::AARDVARK, OutOfPlayId::BINTURONG};

/** Display name ("Aardvark", "Binturong"). */
constexpr std::string_view outOfPlayName(OutOfPlayId outOfPlay)
{
  switch (outOfPlay)
  {
    case OutOfPlayId::AARDVARK:
      return "Aardvark";
    case OutOfPlayId::BINTURONG:
      return "Binturong";
  }
  return "Unknown";
}

/** Command-line and file key ("aardvark", "binturong"). */
constexpr std::string_view outOfPlayKey(OutOfPlayId outOfPlay)
{
  switch (outOfPlay)
  {
    case OutOfPlayId::AARDVARK:
      return "aardvark";
    case OutOfPlayId::BINTURONG:
      return "binturong";
  }
  return "unknown";
}

/** The out-of-play version with this key, if any. */
constexpr std::optional<OutOfPlayId> outOfPlayFromKey(std::string_view key)
{
  for (const OutOfPlayId version : ALL_OUT_OF_PLAY)
  {
    if (outOfPlayKey(version) == key)
    {
      return version;
    }
  }
  return std::nullopt;
}
