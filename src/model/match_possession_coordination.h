// Football Management Project — Copyright (c) 2025 - 2026 Flavio Milinanni.
// See LICENSE in the project root.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "global/types.h"

namespace PossessionCoordination
{
inline bool offsideCollector(bool legacyOffside, bool intended,
                             const std::vector<PlayerID>& others, PlayerID id)
{
  return intended ? legacyOffside
                  : std::find(others.begin(), others.end(), id) != others.end();
}
// At most this much arrival time can be traded for a better collection.
inline constexpr float COLLECTION_ADVANTAGE_SECONDS = 0.10F;
inline constexpr float TIE_SECONDS = 0.005F;
struct Candidate
{
  float arrivalSeconds = 0.0F;
  float collectionQuality = 0.0F;
  bool intendedReceiver = false;
  std::uint64_t id = 0;
};

// Current motion is evidence, not a promise that the player can reach a point.
// Positive forward speed into space and running with the delivery improve it.
inline float quality(Vector2F velocity, Vector2F ballVelocity,
                     bool attacksRight, float opponentSpaceMetres)
{
  const float speed = std::hypot(velocity.x, velocity.y);
  const float ballSpeed = std::hypot(ballVelocity.x, ballVelocity.y);
  const float forward =
      std::clamp((attacksRight ? velocity.x : -velocity.x) / 7.0F, 0.0F, 1.0F);
  const float alignment = speed > 1e-5F && ballSpeed > 1e-5F
                              ? std::clamp((velocity.x * ballVelocity.x +
                                            velocity.y * ballVelocity.y) /
                                               (speed * ballSpeed),
                                           0.0F, 1.0F)
                              : 0.0F;
  const float space = std::clamp(opponentSpaceMetres / 8.0F, 0.0F, 1.0F);
  return (0.75F * forward * (0.5F + 0.5F * space)) +
         (0.25F * std::clamp(speed / 7.0F, 0.0F, 1.0F) * alignment);
}

inline float cost(const Candidate& candidate)
{
  return std::floor((candidate.arrivalSeconds -
                     COLLECTION_ADVANTAGE_SECONDS *
                         std::clamp(candidate.collectionQuality, 0.0F, 1.0F)) /
                    TIE_SECONDS);
}
inline bool better(const Candidate& a, const Candidate& b)
{
  const float ac = cost(a), bc = cost(b);
  if (ac != bc)
  {
    return ac < bc;
  }
  if (a.intendedReceiver != b.intendedReceiver)
  {
    return a.intendedReceiver;
  }
  return a.id < b.id;
}

// The input is chronological. Coordinate only contiguous playable contacts of
// one side: never move a contact past an opponent, body collision or shot
// block. If the preferred player declines/misses, the remaining contacts still
// run.
template <typename Contact, std::size_t N, typename Eligible, typename SameTeam,
          typename MakeCandidate>
void prioritise(std::array<Contact, N>& contacts, std::size_t count,
                Eligible eligible, SameTeam sameTeam, MakeCandidate candidate)
{
  for (std::size_t first = 0; first < count;)
  {
    if (!eligible(contacts[first]))
    {
      ++first;
      continue;
    }
    const float deadline = candidate(contacts[first]).arrivalSeconds +
                           COLLECTION_ADVANTAGE_SECONDS;
    std::size_t end = first + 1;
    while (end < count && eligible(contacts[end]) &&
           sameTeam(contacts[first], contacts[end]) &&
           candidate(contacts[end]).arrivalSeconds <= deadline)
    {
      ++end;
    }
    std::stable_sort(contacts.begin() + first, contacts.begin() + end,
                     [&](const Contact& a, const Contact& b)
                     { return better(candidate(a), candidate(b)); });
    first = end;
  }
}
}  // namespace PossessionCoordination
