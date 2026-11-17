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
#include <string>
#include <string_view>

/**
 * A side's "body": how one player's body carries out what his brain chose
 * and what it costs him (movement, fitness, kicking - where he meets the
 * ball, how his strike comes off, a free kick against the wall - dribbling,
 * first touch, goalkeeping, non-contact injuries). Contests between players,
 * the ball's flight, the rules and attribute mapping are shared by every
 * body.
 *
 * AARDVARK is the inherited body, kept frozen for comparison; the BINTURONG
 * and later bodies are the ones being developed. Each side picks one before
 * kickoff (MatchEngine::setBody), independently of its brain (match_brain.h).
 */
enum class BodyId : std::uint8_t
{
  AARDVARK,
  BINTURONG,
};

inline constexpr std::array<BodyId, 2> ALL_BODIES = {BodyId::AARDVARK,
                                                     BodyId::BINTURONG};

/** Display name ("Aardvark", "Binturong"). */
constexpr std::string_view bodyName(BodyId body)
{
  switch (body)
  {
    case BodyId::AARDVARK:
      return "Aardvark";
    case BodyId::BINTURONG:
      return "Binturong";
  }
  return "Unknown";
}

/** Command-line and file key ("aardvark", "binturong"). */
constexpr std::string_view bodyKey(BodyId body)
{
  switch (body)
  {
    case BodyId::AARDVARK:
      return "aardvark";
    case BodyId::BINTURONG:
      return "binturong";
  }
  return "unknown";
}

/** The body with this key, if any (also the keys used before the versions
 * were renamed: "original" and "new-v1"). */
constexpr std::optional<BodyId> bodyFromKey(std::string_view key)
{
  if (key == "original")
  {
    return BodyId::AARDVARK;
  }
  if (key == "new-v1")
  {
    return BodyId::BINTURONG;
  }
  for (const BodyId body : ALL_BODIES)
  {
    if (bodyKey(body) == key)
    {
      return body;
    }
  }
  return std::nullopt;
}

/** "Aardvark v Binturong" (home first). */
inline std::string bodyPairName(const std::array<BodyId, 2>& bodies)
{
  return std::string(bodyName(bodies[0])) + " v " +
         std::string(bodyName(bodies[1]));
}

/**
 * Parses "HOME,AWAY" (e.g. "aardvark,binturong"); a single key sets both
 * sides. Null when a key is unknown.
 */
constexpr std::optional<std::array<BodyId, 2>> parseBodyPair(
    std::string_view text)
{
  const std::size_t comma = text.find(',');
  const auto home = bodyFromKey(text.substr(0, comma));
  if (!home)
  {
    return std::nullopt;
  }
  if (comma == std::string_view::npos)
  {
    return std::array{*home, *home};
  }
  const auto away = bodyFromKey(text.substr(comma + 1));
  if (!away)
  {
    return std::nullopt;
  }
  return std::array{*home, *away};
}
