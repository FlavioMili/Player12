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
 * The match's "referee": the officials' judgement - how a foul is dealt
 * with (card, advantage, free kick or penalty, denied goal chance), when an
 * advantage ends, whether a pass is given offside and how much time is
 * added. The laws' mechanics are shared by every referee: the offside line
 * and position (the brains read them too), what a card does, the restarts.
 * The per-match strictness is drawn by the handshake (match_handshake.h);
 * the referee applies it.
 *
 * AARDVARK is the inherited referee, kept frozen for comparison; the
 * BINTURONG and later referees are the ones being developed. One referee per
 * match, chosen before kickoff (MatchEngine::setReferee).
 */
enum class RefereeId : std::uint8_t
{
  AARDVARK,
  BINTURONG,
};

inline constexpr std::array<RefereeId, 2> ALL_REFEREES = {RefereeId::AARDVARK,
                                                          RefereeId::BINTURONG};

/** Display name ("Aardvark", "Binturong"). */
constexpr std::string_view refereeName(RefereeId referee)
{
  switch (referee)
  {
    case RefereeId::AARDVARK:
      return "Aardvark";
    case RefereeId::BINTURONG:
      return "Binturong";
  }
  return "Unknown";
}

/** Command-line and file key ("aardvark", "binturong"). */
constexpr std::string_view refereeKey(RefereeId referee)
{
  switch (referee)
  {
    case RefereeId::AARDVARK:
      return "aardvark";
    case RefereeId::BINTURONG:
      return "binturong";
  }
  return "unknown";
}

/** The referee with this key, if any. */
constexpr std::optional<RefereeId> refereeFromKey(std::string_view key)
{
  for (const RefereeId referee : ALL_REFEREES)
  {
    if (refereeKey(referee) == key)
    {
      return referee;
    }
  }
  return std::nullopt;
}
