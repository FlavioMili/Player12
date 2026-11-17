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
 * The match's "handshake": how everything the game passes in (lineups,
 * players' stats, strategies, formations, match context, condition,
 * familiarity, team talks) becomes the match engine's own state - the
 * attribute scale (level shift, stretch), the physical limits (top speed,
 * acceleration, braking), slot roles, base positions, referee strictness.
 * It runs at kickoff and whenever an input arrives later (a substitute, a
 * tactics or formation change).
 *
 * AARDVARK is the inherited conversion, kept frozen for comparison; the
 * BINTURONG and later handshakes are the ones being developed. One handshake
 * per match, chosen when the engine is built (the MatchEngine constructor),
 * since the conversion starts there and works from both teams together.
 */
enum class HandshakeId : std::uint8_t
{
  AARDVARK,
  BINTURONG,
};

inline constexpr std::array<HandshakeId, 2> ALL_HANDSHAKES = {
    HandshakeId::AARDVARK, HandshakeId::BINTURONG};

/** Display name ("Aardvark", "Binturong"). */
constexpr std::string_view handshakeName(HandshakeId handshake)
{
  switch (handshake)
  {
    case HandshakeId::AARDVARK:
      return "Aardvark";
    case HandshakeId::BINTURONG:
      return "Binturong";
  }
  return "Unknown";
}

/** Command-line and file key ("aardvark", "binturong"). */
constexpr std::string_view handshakeKey(HandshakeId handshake)
{
  switch (handshake)
  {
    case HandshakeId::AARDVARK:
      return "aardvark";
    case HandshakeId::BINTURONG:
      return "binturong";
  }
  return "unknown";
}

/** The handshake with this key, if any (also the keys used before the
 * versions were renamed: "original" and "new-v1"). */
constexpr std::optional<HandshakeId> handshakeFromKey(std::string_view key)
{
  if (key == "original")
  {
    return HandshakeId::AARDVARK;
  }
  if (key == "new-v1")
  {
    return HandshakeId::BINTURONG;
  }
  for (const HandshakeId handshake : ALL_HANDSHAKES)
  {
    if (handshakeKey(handshake) == key)
    {
      return handshake;
    }
  }
  return std::nullopt;
}
