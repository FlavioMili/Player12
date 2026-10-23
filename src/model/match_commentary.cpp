// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/match_commentary.h"

#include <array>
#include <string_view>
#include <utility>

#include "global/language_manager.h"

namespace
{
std::string playerName(const MatchCommentaryNames& names, PlayerID id)
{
  if (id == PlayerID{} || !names.player) return {};
  return names.player(id);
}

std::string teamName(const MatchCommentaryNames& names, bool home)
{
  const std::string& name = home ? names.homeTeam : names.awayTeam;
  if (!name.empty()) return name;
  return LOC(home ? "MATCH_TEAM_HOME" : "MATCH_TEAM_AWAY");
}
}  // namespace

const char* MatchCommentary::key(const MatchEvent& event)
{
  using enum MatchEventType;
  switch (event.type)
  {
    case KICK_OFF:
      return "MATCH_COMMENT_KICK_OFF";
    case GOAL:
      return "MATCH_COMMENT_GOAL";
    case OWN_GOAL:
      return "MATCH_COMMENT_OWN_GOAL";
    case SHOT:
      return event.detail == MatchEventDetail::HEADER ? "MATCH_COMMENT_HEADER"
                                                      : "MATCH_COMMENT_SHOT";
    case SAVE:
      if (event.detail == MatchEventDetail::PARRIED)
        return "MATCH_COMMENT_SAVE_PARRY";
      if (event.detail == MatchEventDetail::TIPPED_BEHIND)
        return "MATCH_COMMENT_SAVE_TIP";
      return "MATCH_COMMENT_SAVE";
    case SHOT_BLOCKED:
      return event.detail == MatchEventDetail::WALL ? "MATCH_COMMENT_WALL"
                                                    : "MATCH_COMMENT_BLOCK";
    case SHOT_OFF_TARGET:
      return "MATCH_COMMENT_OFF_TARGET";
    case WOODWORK:
      return event.primaryPlayerId != PlayerID{}
                 ? "MATCH_COMMENT_WOODWORK"
                 : "MATCH_COMMENT_WOODWORK_BALL";
    case FOUL:
      return "MATCH_COMMENT_FOUL";
    case ADVANTAGE:
      return "MATCH_COMMENT_ADVANTAGE";
    case YELLOW_CARD:
      return "MATCH_COMMENT_YELLOW";
    case SECOND_YELLOW:
      return "MATCH_COMMENT_SECOND_YELLOW";
    case RED_CARD:
      return "MATCH_COMMENT_RED";
    case INJURY:
      return event.detail == MatchEventDetail::CONTACT
                 ? "MATCH_COMMENT_INJURY_CONTACT"
                 : "MATCH_COMMENT_INJURY";
    case SUBSTITUTION:
      return "MATCH_COMMENT_SUBSTITUTION";
    case OFFSIDE:
      return "MATCH_COMMENT_OFFSIDE";
    case CORNER:
      return "MATCH_COMMENT_CORNER";
    case FREE_KICK:
      return "MATCH_COMMENT_FREE_KICK";
    case PENALTY:
      return "MATCH_COMMENT_PENALTY";
    case PENALTY_MISSED:
      return "MATCH_COMMENT_PENALTY_MISSED";
    case THROW_IN:
      return "MATCH_COMMENT_THROW_IN";
    case GOAL_KICK:
      return "MATCH_COMMENT_GOAL_KICK";
    case ADDED_TIME:
      return "MATCH_COMMENT_ADDED_TIME";
    case HALF_TIME:
      if (event.period == 2) return "MATCH_COMMENT_END_OF_NORMAL_TIME";
      if (event.period >= 3) return "MATCH_COMMENT_EXTRA_TIME_HALF_TIME";
      return "MATCH_COMMENT_HALF_TIME";
    case SECOND_HALF:
      if (event.period == 3) return "MATCH_COMMENT_EXTRA_TIME";
      if (event.period >= 4) return "MATCH_COMMENT_EXTRA_TIME_SECOND_HALF";
      return "MATCH_COMMENT_SECOND_HALF";
    case FULL_TIME:
      return event.homeShootout + event.awayShootout > 0
                 ? "MATCH_COMMENT_FULL_TIME_PENALTIES"
                 : "MATCH_COMMENT_FULL_TIME";
    case PENALTY_SHOOTOUT:
      if (event.detail == MatchEventDetail::SCORED)
        return "MATCH_COMMENT_SHOOTOUT_SCORED";
      if (event.detail == MatchEventDetail::SAVED)
        return "MATCH_COMMENT_SHOOTOUT_SAVED";
      if (event.detail == MatchEventDetail::MISSED)
        return "MATCH_COMMENT_SHOOTOUT_MISSED";
      return "MATCH_COMMENT_SHOOTOUT";
    case INFO:
      return event.detail == MatchEventDetail::ABANDONED
                 ? "MATCH_COMMENT_ABANDONED"
                 : "MATCH_COMMENT_INFO";
  }
  return "MATCH_COMMENT_INFO";
}

std::string MatchCommentary::describe(const MatchEvent& event,
                                      const MatchCommentaryNames& names)
{
  return fill(LOC(key(event)), event, names);
}

std::string MatchCommentary::fill(std::string_view pattern,
                                  const MatchEvent& event,
                                  const MatchCommentaryNames& names)
{
  const std::array<std::pair<std::string_view, std::string>, 10> fields{{
      {"{player}", playerName(names, event.primaryPlayerId)},
      {"{other}", playerName(names, event.secondaryPlayerId)},
      {"{team}", teamName(names, event.isHomeTeam)},
      {"{home}", teamName(names, true)},
      {"{away}", teamName(names, false)},
      {"{homeScore}", std::to_string(event.homeScore)},
      {"{awayScore}", std::to_string(event.awayScore)},
      {"{minutes}", std::to_string(event.minutes)},
      {"{homePens}", std::to_string(event.homeShootout)},
      {"{awayPens}", std::to_string(event.awayShootout)},
  }};
  std::string line;
  line.reserve(pattern.size() + 32);
  for (std::size_t index = 0; index < pattern.size();)
  {
    bool replaced = false;
    if (pattern[index] == '{')
    {
      for (const auto& [placeholder, value] : fields)
      {
        if (pattern.substr(index, placeholder.size()) != placeholder) continue;
        line += value;
        index += placeholder.size();
        replaced = true;
        break;
      }
    }
    if (!replaced) line += pattern[index++];
  }
  return line;
}
