// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/standings.h"

#include <algorithm>
#include <tuple>
#include <unordered_map>

#include "database/gamedata.h"
#include "model/calendar.h"
#include "model/league.h"

namespace
{
struct PlayedResult
{
  size_t home;
  size_t away;
  uint8_t home_goals;
  uint8_t away_goals;
};

void recordForm(std::string& form, char result)
{
  form.push_back(result);
  if (form.size() > Standings::FORM_LENGTH) form.erase(form.begin());
}

struct MiniLeagueRecord
{
  int points = 0;
  int goal_difference = 0;
};
}  // namespace

std::vector<StandingRow> Standings::compute(const League& league,
                                            const Calendar& calendar,
                                            const GameData& gamedata)
{
  const auto& team_ids = league.getTeamIDs();
  std::vector<StandingRow> rows(team_ids.size());
  std::unordered_map<TeamID, size_t> index_of;
  index_of.reserve(team_ids.size());
  for (size_t i = 0; i < team_ids.size(); ++i)
  {
    rows[i].team_id = team_ids[i];
    index_of.emplace(team_ids[i], i);
  }

  std::vector<PlayedResult> results;
  results.reserve(team_ids.size() * team_ids.size());
  for (const auto& [date, matches] : calendar.getFullCalendar())
  {
    for (const Match& match : matches)
    {
      if (!match.isPlayed() || match.getMatchType() != MatchType::LEAGUE)
        continue;
      if (match.getCompetitionId() != 0 &&
          match.getCompetitionId() != league.getId())
        continue;
      const auto home = index_of.find(match.getHomeTeamId());
      const auto away = index_of.find(match.getAwayTeamId());
      if (home == index_of.end() || away == index_of.end()) continue;
      results.emplace_back(home->second, away->second, match.getHomeScore(),
                           match.getAwayScore());
    }
  }

  for (const PlayedResult& result : results)
  {
    StandingRow& home = rows[result.home];
    StandingRow& away = rows[result.away];
    ++home.played;
    ++away.played;
    home.goals_for = static_cast<uint16_t>(home.goals_for + result.home_goals);
    home.goals_against =
        static_cast<uint16_t>(home.goals_against + result.away_goals);
    away.goals_for = static_cast<uint16_t>(away.goals_for + result.away_goals);
    away.goals_against =
        static_cast<uint16_t>(away.goals_against + result.home_goals);
    if (result.home_goals > result.away_goals)
    {
      ++home.won;
      ++away.lost;
      recordForm(home.form, 'W');
      recordForm(away.form, 'L');
    }
    else if (result.home_goals < result.away_goals)
    {
      ++away.won;
      ++home.lost;
      recordForm(home.form, 'L');
      recordForm(away.form, 'W');
    }
    else
    {
      ++home.drawn;
      ++away.drawn;
      recordForm(home.form, 'D');
      recordForm(away.form, 'D');
    }
  }

  for (StandingRow& row : rows)
  {
    row.points = static_cast<uint16_t>(row.won * POINTS_FOR_WIN +
                                       row.drawn * POINTS_FOR_DRAW);
    row.goal_difference = static_cast<int32_t>(row.goals_for) -
                          static_cast<int32_t>(row.goals_against);
  }

  const auto nameOf = [&gamedata](TeamID id)
  {
    const auto team = gamedata.getTeam(id);
    return team ? std::string_view(team->get().getName()) : std::string_view();
  };

  const bool head_to_head_first =
      league.getTieBreakRule() == TieBreakRule::HEAD_TO_HEAD;
  const auto primaryKey = [head_to_head_first](const StandingRow& row)
  {
    return head_to_head_first
               ? std::tuple(row.points, int32_t{0}, uint16_t{0})
               : std::tuple(row.points, row.goal_difference, row.goals_for);
  };
  std::ranges::sort(rows, [&](const StandingRow& left, const StandingRow& right)
                    { return primaryKey(left) > primaryKey(right); });

  // Resolve each group level on the primary key with a head-to-head
  // mini-league among its members.
  for (size_t begin = 0; begin < rows.size();)
  {
    size_t end = begin + 1;
    while (end < rows.size() &&
           primaryKey(rows[end]) == primaryKey(rows[begin]))
      ++end;
    if (end - begin > 1)
    {
      std::unordered_map<TeamID, MiniLeagueRecord> group;
      for (size_t i = begin; i < end; ++i) group[rows[i].team_id] = {};
      for (const PlayedResult& result : results)
      {
        const auto home = group.find(team_ids[result.home]);
        const auto away = group.find(team_ids[result.away]);
        if (home == group.end() || away == group.end()) continue;
        const int margin = result.home_goals - result.away_goals;
        home->second.goal_difference += margin;
        away->second.goal_difference -= margin;
        if (margin > 0)
          home->second.points += POINTS_FOR_WIN;
        else if (margin < 0)
          away->second.points += POINTS_FOR_WIN;
        else
        {
          home->second.points += POINTS_FOR_DRAW;
          away->second.points += POINTS_FOR_DRAW;
        }
      }
      const auto first = rows.begin() + static_cast<std::ptrdiff_t>(begin);
      const auto last = rows.begin() + static_cast<std::ptrdiff_t>(end);
      std::sort(first, last,
                [&](const StandingRow& left, const StandingRow& right)
                {
                  const MiniLeagueRecord& left_h2h = group.at(left.team_id);
                  const MiniLeagueRecord& right_h2h = group.at(right.team_id);
                  if (left_h2h.points != right_h2h.points)
                    return left_h2h.points > right_h2h.points;
                  if (left_h2h.goal_difference != right_h2h.goal_difference)
                    return left_h2h.goal_difference > right_h2h.goal_difference;
                  if (head_to_head_first)
                  {
                    if (left.goal_difference != right.goal_difference)
                      return left.goal_difference > right.goal_difference;
                    if (left.goals_for != right.goals_for)
                      return left.goals_for > right.goals_for;
                  }
                  const auto left_name = nameOf(left.team_id);
                  if (const auto right_name = nameOf(right.team_id);
                      left_name != right_name)
                    return left_name < right_name;
                  return left.team_id < right.team_id;
                });
    }
    begin = end;
  }

  for (size_t i = 0; i < rows.size(); ++i)
    rows[i].position = static_cast<uint16_t>(i + 1);
  return rows;
}
