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
 * A side's "AI manager": the touchline decisions the engine makes for a side
 * it manages (MatchEngine::setAutoSubstitutions) - which players to take off
 * and when (injury, fatigue, cards, the score, medical staff limits), who
 * comes on, and the shouts. Carrying out a change (the substitution itself,
 * the shape rebalanced, a forced keeper) is shared by every manager.
 *
 * AARDVARK is the inherited manager, kept frozen for comparison; the
 * BINTURONG and later managers are the ones being developed. Each side picks
 * one before kickoff (MatchEngine::setManager), independently of its brain
 * and body.
 */
enum class ManagerId : std::uint8_t
{
  AARDVARK,
  BINTURONG,
};

inline constexpr std::array<ManagerId, 2> ALL_MANAGERS = {ManagerId::AARDVARK,
                                                          ManagerId::BINTURONG};

/** Display name ("Aardvark", "Binturong"). */
constexpr std::string_view managerName(ManagerId manager)
{
  switch (manager)
  {
    case ManagerId::AARDVARK:
      return "Aardvark";
    case ManagerId::BINTURONG:
      return "Binturong";
  }
  return "Unknown";
}

/** Command-line and file key ("aardvark", "binturong"). */
constexpr std::string_view managerKey(ManagerId manager)
{
  switch (manager)
  {
    case ManagerId::AARDVARK:
      return "aardvark";
    case ManagerId::BINTURONG:
      return "binturong";
  }
  return "unknown";
}

/** The manager with this key, if any. */
constexpr std::optional<ManagerId> managerFromKey(std::string_view key)
{
  for (const ManagerId manager : ALL_MANAGERS)
  {
    if (managerKey(manager) == key)
    {
      return manager;
    }
  }
  return std::nullopt;
}

/** "Aardvark v Binturong" (home first). */
inline std::string managerPairName(const std::array<ManagerId, 2>& managers)
{
  return std::string(managerName(managers[0])) + " v " +
         std::string(managerName(managers[1]));
}

/**
 * Parses "HOME,AWAY" (e.g. "aardvark,binturong"); a single key sets both
 * sides. Null when a key is unknown.
 */
constexpr std::optional<std::array<ManagerId, 2>> parseManagerPair(
    std::string_view text)
{
  const std::size_t comma = text.find(',');
  const auto home = managerFromKey(text.substr(0, comma));
  if (!home)
  {
    return std::nullopt;
  }
  if (comma == std::string_view::npos)
  {
    return std::array{*home, *home};
  }
  const auto away = managerFromKey(text.substr(comma + 1));
  if (!away)
  {
    return std::nullopt;
  }
  return std::array{*home, *away};
}
