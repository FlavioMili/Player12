// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/nav_history.h"

#include <algorithm>

NavEntry NavEntry::ofSection(NavSection section)
{
  NavEntry entry;
  entry.section = section;
  return entry;
}

NavEntry NavEntry::ofPlayer(PlayerID player)
{
  NavEntry entry;
  entry.kind = Kind::PLAYER;
  entry.player = player;
  return entry;
}

NavEntry NavEntry::ofClub(TeamID team)
{
  NavEntry entry;
  entry.kind = Kind::CLUB;
  entry.team = team;
  return entry;
}

NavEntry NavEntry::ofMatchReport(GameDateValue date, TeamID home, TeamID away)
{
  NavEntry entry;
  entry.kind = Kind::MATCH_REPORT;
  entry.team = home;
  entry.away_team = away;
  entry.date = date;
  return entry;
}

NavEntry NavEntry::ofCompare(PlayerID first, PlayerID second)
{
  NavEntry entry;
  entry.kind = Kind::COMPARE;
  entry.player = first;
  entry.second_player = second;
  return entry;
}

bool NavEntry::isDetail() const
{
  using enum Kind;
  switch (kind)
  {
    case PLAYER:
    case MATCH_REPORT:
      return true;
    case COMPARE:
      return player != 0 || second_player != 0;
    case SECTION:
    case CLUB:
      return false;
  }
  return false;
}

NavHistory::NavHistory(std::size_t capacity)
    : limit(std::max<std::size_t>(capacity, 1))
{
  entries.reserve(limit);
}

void NavHistory::visit(const NavEntry& entry)
{
  if (!entries.empty())
  {
    if (entries[cursor] == entry) return;
    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(cursor) + 1,
                  entries.end());
  }
  if (entries.size() == limit) entries.erase(entries.begin());
  entries.push_back(entry);
  cursor = entries.size() - 1;
}

void NavHistory::stepBackTo(const NavEntry& entry)
{
  if (entries.empty())
  {
    entries.push_back(entry);
    return;
  }
  if (entries[cursor] == entry) return;
  for (std::size_t index = cursor; index-- > 0;)
    if (entries[index] == entry)
    {
      cursor = index;
      return;
    }
  // Full: the oldest entry goes, or the newest when the oldest is current.
  if (entries.size() == limit)
  {
    if (cursor > 0)
    {
      entries.erase(entries.begin());
      --cursor;
    }
    else
    {
      entries.pop_back();
    }
  }
  entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(cursor), entry);
}

void NavHistory::clear()
{
  entries.clear();
  cursor = 0;
}

std::optional<NavEntry> NavHistory::current() const
{
  if (entries.empty()) return std::nullopt;
  return entries[cursor];
}
