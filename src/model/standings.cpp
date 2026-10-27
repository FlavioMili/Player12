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

struct MiniLeagueRecord
{
  int points = 0;
  int goal_difference = 0;
};

using PrimaryKey = std::tuple<uint16_t, int32_t, uint16_t>;

void recordForm(std::string& form, char result)
{
  form.push_back(result);
  if (form.size() > Standings::FORM_LENGTH) form.erase(form.begin());
}

std::vector<StandingRow> makeRows(const League& league,
                                  std::unordered_map<TeamID, size_t>& index_of)
{
  const auto& team_ids = league.getTeamIDs();
  std::vector<StandingRow> rows(team_ids.size());
  index_of.reserve(team_ids.size());
  for (size_t i = 0; i < team_ids.size(); ++i)
  {
    rows[i].team_id = team_ids[i];
    index_of.emplace(team_ids[i], i);
  }
  return rows;
}

bool belongsToLeague(const Match& match, const League& league)
{
  return match.getCompetitionId() == 0 ||
         match.getCompetitionId() == league.getId();
}

void collectMatchResult(const Match& match, const League& league,
                        const std::unordered_map<TeamID, size_t>& index_of,
                        std::vector<PlayedResult>& results)
{
  if (!match.isPlayed() || match.getMatchType() != MatchType::LEAGUE ||
      !belongsToLeague(match, league))
    return;
  const auto home = index_of.find(match.getHomeTeamId());
  const auto away = index_of.find(match.getAwayTeamId());
  if (home == index_of.end() || away == index_of.end()) return;
  results.push_back(
      {home->second, away->second, match.getHomeScore(), match.getAwayScore()});
}

std::vector<PlayedResult> collectResults(
    const League& league, const Calendar& calendar,
    const std::unordered_map<TeamID, size_t>& index_of)
{
  const auto& team_ids = league.getTeamIDs();
  std::vector<PlayedResult> results;
  results.reserve(team_ids.size() * team_ids.size());
  for (const auto& [date, matches] : calendar.getFullCalendar())
  {
    (void)date;
    for (const Match& match : matches)
      collectMatchResult(match, league, index_of, results);
  }
  return results;
}

void addGoals(StandingRow& home, StandingRow& away, const PlayedResult& result)
{
  ++home.played;
  ++away.played;
  home.goals_for = static_cast<uint16_t>(home.goals_for + result.home_goals);
  home.goals_against =
      static_cast<uint16_t>(home.goals_against + result.away_goals);
  away.goals_for = static_cast<uint16_t>(away.goals_for + result.away_goals);
  away.goals_against =
      static_cast<uint16_t>(away.goals_against + result.home_goals);
}

void recordOutcome(StandingRow& home, StandingRow& away,
                   const PlayedResult& result)
{
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

void applyResults(std::vector<StandingRow>& rows,
                  const std::vector<PlayedResult>& results)
{
  for (const PlayedResult& result : results)
  {
    StandingRow& home = rows[result.home];
    StandingRow& away = rows[result.away];
    addGoals(home, away, result);
    recordOutcome(home, away, result);
  }
}

void calculateTotals(std::vector<StandingRow>& rows)
{
  for (StandingRow& row : rows)
  {
    row.points = static_cast<uint16_t>(row.won * Standings::POINTS_FOR_WIN +
                                       row.drawn * Standings::POINTS_FOR_DRAW);
    row.goal_difference = static_cast<int32_t>(row.goals_for) -
                          static_cast<int32_t>(row.goals_against);
  }
}

PrimaryKey primaryKey(const StandingRow& row, bool head_to_head_first)
{
  return head_to_head_first
             ? PrimaryKey(row.points, int32_t{0}, uint16_t{0})
             : PrimaryKey(row.points, row.goal_difference, row.goals_for);
}

std::string_view nameOf(TeamID id, const GameData& gamedata)
{
  const auto team = gamedata.getTeam(id);
  return team ? std::string_view(team->get().getName()) : std::string_view();
}

using MiniLeague = std::unordered_map<TeamID, MiniLeagueRecord>;

void addMiniLeagueResult(MiniLeague& group, const PlayedResult& result,
                         const std::vector<TeamID>& team_ids)
{
  const auto home = group.find(team_ids[result.home]);
  const auto away = group.find(team_ids[result.away]);
  if (home == group.end() || away == group.end()) return;
  const int margin = result.home_goals - result.away_goals;
  home->second.goal_difference += margin;
  away->second.goal_difference -= margin;
  if (margin > 0)
    home->second.points += Standings::POINTS_FOR_WIN;
  else if (margin < 0)
    away->second.points += Standings::POINTS_FOR_WIN;
  else
  {
    home->second.points += Standings::POINTS_FOR_DRAW;
    away->second.points += Standings::POINTS_FOR_DRAW;
  }
}

MiniLeague buildMiniLeague(const std::vector<StandingRow>& rows, size_t begin,
                           size_t end, const std::vector<PlayedResult>& results,
                           const std::vector<TeamID>& team_ids)
{
  MiniLeague group;
  for (size_t i = begin; i < end; ++i) group[rows[i].team_id] = {};
  for (const PlayedResult& result : results)
    addMiniLeagueResult(group, result, team_ids);
  return group;
}

bool betterMiniLeague(const StandingRow& left, const StandingRow& right,
                      const MiniLeague& group, bool head_to_head_first,
                      const GameData& gamedata)
{
  const MiniLeagueRecord& left_h2h = group.at(left.team_id);
  const MiniLeagueRecord& right_h2h = group.at(right.team_id);
  if (left_h2h.points != right_h2h.points)
    return left_h2h.points > right_h2h.points;
  if (left_h2h.goal_difference != right_h2h.goal_difference)
    return left_h2h.goal_difference > right_h2h.goal_difference;
  if (head_to_head_first && left.goal_difference != right.goal_difference)
    return left.goal_difference > right.goal_difference;
  if (head_to_head_first && left.goals_for != right.goals_for)
    return left.goals_for > right.goals_for;
  const auto left_name = nameOf(left.team_id, gamedata);
  const auto right_name = nameOf(right.team_id, gamedata);
  if (left_name != right_name) return left_name < right_name;
  return left.team_id < right.team_id;
}

void sortMiniLeagueGroup(std::vector<StandingRow>& rows, size_t begin,
                         size_t end, const std::vector<PlayedResult>& results,
                         const std::vector<TeamID>& team_ids,
                         bool head_to_head_first, const GameData& gamedata)
{
  const MiniLeague group = buildMiniLeague(rows, begin, end, results, team_ids);
  const auto first = rows.begin() + static_cast<std::ptrdiff_t>(begin);
  const auto last = rows.begin() + static_cast<std::ptrdiff_t>(end);
  std::sort(first, last,
            [&](const StandingRow& left, const StandingRow& right)
            {
              return betterMiniLeague(left, right, group, head_to_head_first,
                                      gamedata);
            });
}

void resolvePrimaryKeyGroups(std::vector<StandingRow>& rows,
                             const std::vector<PlayedResult>& results,
                             const std::vector<TeamID>& team_ids,
                             bool head_to_head_first, const GameData& gamedata)
{
  size_t begin = 0;
  while (begin < rows.size())
  {
    size_t end = begin + 1;
    while (end < rows.size() && primaryKey(rows[end], head_to_head_first) ==
                                    primaryKey(rows[begin], head_to_head_first))
      ++end;
    if (end - begin > 1)
      sortMiniLeagueGroup(rows, begin, end, results, team_ids,
                          head_to_head_first, gamedata);
    begin = end;
  }
}

void assignPositions(std::vector<StandingRow>& rows)
{
  for (size_t i = 0; i < rows.size(); ++i)
    rows[i].position = static_cast<uint16_t>(i + 1);
}
}  // namespace

std::vector<StandingRow> Standings::compute(const League& league,
                                            const Calendar& calendar,
                                            const GameData& gamedata)
{
  std::unordered_map<TeamID, size_t> index_of;
  std::vector<StandingRow> rows = makeRows(league, index_of);
  const std::vector<PlayedResult> results =
      collectResults(league, calendar, index_of);
  applyResults(rows, results);
  calculateTotals(rows);

  const bool head_to_head_first =
      league.getTieBreakRule() == TieBreakRule::HEAD_TO_HEAD;
  std::ranges::sort(
      rows,
      [head_to_head_first](const StandingRow& left, const StandingRow& right)
      {
        return primaryKey(left, head_to_head_first) >
               primaryKey(right, head_to_head_first);
      });
  resolvePrimaryKeyGroups(rows, results, league.getTeamIDs(),
                          head_to_head_first, gamedata);
  assignPositions(rows);
  return rows;
}
