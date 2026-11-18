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
 * A side's "brain": the code that decides what its players do (where to go,
 * how hard to run, what to do with the ball, set pieces, keeper positioning).
 * The body (physics, execution, contests, rules) is shared by every brain.
 *
 * AARDVARK is the inherited decision-making and BINTURONG its first rework,
 * both kept frozen for comparison; CAPYBARA (generation C, the brain redesign)
 * is the one being developed. Each side of a match picks one before kickoff
 * (MatchEngine::setBrain), so two brains can play each other.
 */
enum class BrainId : std::uint8_t
{
  AARDVARK,
  BINTURONG,
  CAPYBARA,
};

inline constexpr std::array<BrainId, 3> ALL_BRAINS = {
    BrainId::AARDVARK, BrainId::BINTURONG, BrainId::CAPYBARA};

/** Display name ("Aardvark", "Binturong", "Capybara"). */
constexpr std::string_view brainName(BrainId brain)
{
  switch (brain)
  {
    case BrainId::AARDVARK:
      return "Aardvark";
    case BrainId::BINTURONG:
      return "Binturong";
    case BrainId::CAPYBARA:
      return "Capybara";
  }
  return "Unknown";
}

/** Command-line and file key ("aardvark", "binturong", "capybara"). */
constexpr std::string_view brainKey(BrainId brain)
{
  switch (brain)
  {
    case BrainId::AARDVARK:
      return "aardvark";
    case BrainId::BINTURONG:
      return "binturong";
    case BrainId::CAPYBARA:
      return "capybara";
  }
  return "unknown";
}

/** The brain with this key, if any (also the keys used before the versions
 * were renamed: "original" and "new-v1"). */
constexpr std::optional<BrainId> brainFromKey(std::string_view key)
{
  if (key == "original")
  {
    return BrainId::AARDVARK;
  }
  if (key == "new-v1")
  {
    return BrainId::BINTURONG;
  }
  for (const BrainId brain : ALL_BRAINS)
  {
    if (brainKey(brain) == key)
    {
      return brain;
    }
  }
  return std::nullopt;
}

/** "Aardvark v Binturong" (home first). */
inline std::string brainPairName(const std::array<BrainId, 2>& brains)
{
  return std::string(brainName(brains[0])) + " v " +
         std::string(brainName(brains[1]));
}

/**
 * Parses "HOME,AWAY" (e.g. "aardvark,binturong"); a single key sets both
 * sides. Null when a key is unknown.
 */
constexpr std::optional<std::array<BrainId, 2>> parseBrainPair(
    std::string_view text)
{
  const std::size_t comma = text.find(',');
  const auto home = brainFromKey(text.substr(0, comma));
  if (!home)
  {
    return std::nullopt;
  }
  if (comma == std::string_view::npos)
  {
    return std::array{*home, *home};
  }
  const auto away = brainFromKey(text.substr(comma + 1));
  if (!away)
  {
    return std::nullopt;
  }
  return std::array{*home, *away};
}
