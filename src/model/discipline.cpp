// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/discipline.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <unordered_map>

#include "database/gamedata.h"
#include "model/match_report.h"
#include "model/player.h"

namespace
{
uint8_t addMatches(uint8_t current, unsigned extra)
{
  return static_cast<uint8_t>(std::min(current + extra, 255U));
}

struct CardCount
{
  unsigned yellows = 0;
  unsigned reds = 0;
};
}  // namespace

void Discipline::processMatch(const MatchReport& report,
                              const GameData& gamedata)
{
  if (report.match_type == MatchType::FRIENDLY) return;

  for (auto key = active_bans.begin(); key != active_bans.end();)
  {
    DisciplinaryRecord& record = by_player.at(*key);
    if (const auto player = record.scope == report.match_type
                                ? gamedata.getPlayer(record.player_id)
                                : std::nullopt;
        player && (player->get().getTeamId() == report.home_team_id ||
                   player->get().getTeamId() == report.away_team_id))
      --record.ban_matches;
    key = record.ban_matches == 0 ? active_bans.erase(key) : std::next(key);
  }

  std::unordered_map<PlayerID, CardCount> cards;
  for (const PlayerMatchLine& line : report.players)
  {
    if (line.yellow_cards == 0 && line.red_cards == 0) continue;
    cards[line.player_id] = {line.yellow_cards, line.red_cards};
  }
  if (cards.empty())
  {
    using enum MatchEventKind;
    // Engines without per-player lines: fall back to the card events.
    for (const MatchReportEvent& event : report.events)
    {
      if (event.player == 0) continue;
      if (event.kind == YELLOW_CARD) ++cards[event.player].yellows;
      if (event.kind == SECOND_YELLOW)
      {
        ++cards[event.player].yellows;
        ++cards[event.player].reds;
      }
      if (event.kind == RED_CARD) ++cards[event.player].reds;
    }
  }

  for (const auto& [player_id, count] : cards)
  {
    auto [it, inserted] =
        by_player.try_emplace(Key{player_id, report.match_type});
    if (inserted)
    {
      it->second.player_id = player_id;
      it->second.scope = report.match_type;
    }
    book(it->second, count.yellows, count.reds);
    if (it->second.ban_matches > 0) active_bans.insert(it->first);
  }
}

void Discipline::book(DisciplinaryRecord& record, unsigned yellows,
                      unsigned reds) const
{
  if (reds > 0 && yellows >= 2)
  {
    // Sent off for a second caution: the two cautions do not accumulate.
    record.ban_matches =
        addMatches(record.ban_matches, rules.second_yellow_ban);
    yellows -= 2;
  }
  else if (reds > 0)
  {
    record.ban_matches =
        addMatches(record.ban_matches,
                   rules.straight_red_ban +
                       unsigned{rules.repeat_red_extra} * record.season_reds);
  }
  if (reds > 0) record.season_reds = addMatches(record.season_reds, 1);

  const unsigned threshold =
      std::max<unsigned>(rules.yellow_card_threshold, 1U);
  for (unsigned i = 0; i < yellows; ++i)
  {
    ++record.season_yellows;
    if (record.season_yellows % threshold == 0)
      record.ban_matches =
          addMatches(record.ban_matches, rules.yellow_accumulation_ban);
  }
}

bool Discipline::isSuspended(PlayerID player_id, MatchType scope) const
{
  return banMatches(player_id, scope) > 0;
}

uint8_t Discipline::banMatches(PlayerID player_id, MatchType scope) const
{
  const auto it = by_player.find(Key{player_id, scope});
  return it == by_player.end() ? 0 : it->second.ban_matches;
}

std::vector<DisciplinaryRecord> Discipline::suspendedPlayers(
    TeamID team_id, const GameData& gamedata) const
{
  std::vector<DisciplinaryRecord> suspended;
  for (const Key& key : active_bans)
  {
    const DisciplinaryRecord& record = by_player.at(key);
    const auto player = gamedata.getPlayer(record.player_id);
    if (player && player->get().getTeamId() == team_id)
      suspended.push_back(record);
  }
  return suspended;
}

void Discipline::resetSeason()
{
  for (auto& [key, record] : by_player)
  {
    record.season_yellows = 0;
    record.season_reds = 0;
  }
  std::erase_if(by_player, [](const auto& entry)
                { return entry.second.ban_matches == 0; });
}

std::vector<DisciplinaryRecord> Discipline::records() const
{
  std::vector<DisciplinaryRecord> all;
  all.reserve(by_player.size());
  for (const auto& [key, record] : by_player) all.push_back(record);
  return all;
}

void Discipline::restore(const std::vector<DisciplinaryRecord>& stored)
{
  by_player.clear();
  active_bans.clear();
  for (const DisciplinaryRecord& record : stored)
  {
    const Key key{record.player_id, record.scope};
    by_player.insert_or_assign(key, record);
    if (record.ban_matches > 0)
      active_bans.insert(key);
    else
      active_bans.erase(key);
  }
}
