// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/squad_numbers.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <utility>
#include <vector>

namespace
{
using Taken = std::bitset<SquadNumbers::MAX_NUMBER + 1>;

/** Positions with their own traditional numbers. */
enum class Slot : std::uint8_t
{
  Goalkeeper,
  RightBack,
  LeftBack,
  CentreBack,
  Holding,
  Centre,
  Playmaker,
  RightWing,
  LeftWing,
  Striker,
  COUNT
};

Slot slotOf(PlayerRole role)
{
  using enum Slot;
  using enum PlayerRole;
  switch (role)
  {
    case GK:
      return Goalkeeper;
    case RB:
      return RightBack;
    case LB:
      return LeftBack;
    case CB:
      return CentreBack;
    case CDM:
      return Holding;
    case CAM:
      return Playmaker;
    case RM:
    case RW:
      return RightWing;
    case LM:
    case LW:
      return LeftWing;
    case ST:
      return Striker;
    case CM:
    case UNKNOWN:
      break;
  }
  return Centre;
}

/** Regular places of each slot in a squad numbered from scratch (11). */
constexpr std::array<int,
                     static_cast<std::size_t>(std::to_underlying(Slot::COUNT))>
    REGULARS = {1, 1, 1, 2, 1, 1, 1, 1, 1, 1};

/** Traditional numbers of each slot, most fitting first (0: none). */
constexpr std::array<std::array<int, 2>,
                     static_cast<std::size_t>(std::to_underlying(Slot::COUNT))>
    TRADITIONAL = {{{1, 0},
                    {2, 0},
                    {3, 0},
                    {4, 5},
                    {6, 0},
                    {8, 6},
                    {10, 8},
                    {7, 0},
                    {11, 0},
                    {9, 10}}};

bool claim(Taken& taken, int number)
{
  if (!SquadNumbers::isValid(number) ||
      taken.test(static_cast<std::size_t>(number)))
    return false;
  taken.set(static_cast<std::size_t>(number));
  return true;
}

/** First free number in [first, last]; 0 when all are worn. */
int firstFree(const Taken& taken, int first, int last)
{
  for (int number = first; number <= last; ++number)
    if (!taken.test(static_cast<std::size_t>(number))) return number;
  return 0;
}

/** A squad number for a player without a traditional one. */
int squadNumber(const Taken& taken, const SquadNumbers::Entry& entry)
{
  using SquadNumbers::MAX_NUMBER;
  if (slotOf(entry.role) == Slot::Goalkeeper)
  {
    for (const int number : {12, 13, 25, 31, 30})
      if (!taken.test(static_cast<std::size_t>(number))) return number;
  }
  int number = 0;
  if (entry.age <= SquadNumbers::YOUNG_AGE)
  {
    number = firstFree(taken, 30, MAX_NUMBER);
    if (number == 0) number = firstFree(taken, 12, 29);
  }
  else
  {
    number = firstFree(taken, 14, 29);
    if (number == 0) number = firstFree(taken, 12, 13);
    if (number == 0) number = firstFree(taken, 30, MAX_NUMBER);
  }
  if (number == 0) number = firstFree(taken, 1, 11);
  return number;
}

/** Better player first; the id breaks ties so the order is total. */
bool better(const SquadNumbers::Entry& a, const SquadNumbers::Entry& b)
{
  if (a.overall != b.overall) return a.overall > b.overall;
  return a.id < b.id;
}
}  // namespace

void SquadNumbers::assign(std::span<Entry> squad)
{
  std::vector<Entry*> order;
  order.reserve(squad.size());
  for (Entry& entry : squad) order.push_back(&entry);
  std::ranges::sort(
      order, [](const Entry* a, const Entry* b) { return better(*a, *b); });

  // Valid numbers stay; on a clash the better player keeps his.
  Taken taken;
  bool numbered = false;
  for (Entry* entry : order)
  {
    if (claim(taken, entry->number))
    {
      numbered = true;
      continue;
    }
    if (isValid(entry->number) && entry->preferred == 0)
      entry->preferred = entry->number;
    entry->number = 0;
  }

  // A squad numbered from scratch serves its regulars first: the best
  // players of each position up to the places of a usual eleven.
  std::vector<Entry*> waiting;
  waiting.reserve(order.size());
  std::vector<Entry*> others;
  std::array<int, static_cast<std::size_t>(std::to_underlying(Slot::COUNT))>
      filled{};
  for (Entry* entry : order)
  {
    if (entry->number != 0) continue;
    const auto slot =
        static_cast<std::size_t>(std::to_underlying(slotOf(entry->role)));
    if (numbered || filled[slot] < REGULARS[slot])
    {
      ++filled[slot];
      waiting.push_back(entry);
    }
    else
    {
      others.push_back(entry);
    }
  }
  waiting.insert(waiting.end(), others.begin(), others.end());

  for (Entry* entry : waiting)
  {
    if (claim(taken, entry->preferred))
    {
      entry->number = static_cast<std::uint8_t>(entry->preferred);
      continue;
    }
    int number = 0;
    for (const int candidate : TRADITIONAL[static_cast<std::size_t>(
             std::to_underlying(slotOf(entry->role)))])
    {
      if (candidate != 0 && !taken.test(static_cast<std::size_t>(candidate)))
      {
        number = candidate;
        break;
      }
    }
    if (number == 0) number = squadNumber(taken, *entry);
    if (number != 0) taken.set(static_cast<std::size_t>(number));
    entry->number = static_cast<std::uint8_t>(number);
  }
}
