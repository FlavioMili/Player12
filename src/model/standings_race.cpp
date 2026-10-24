// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/standings_race.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <optional>
#include <unordered_map>
#include <utility>

#include "database/gamedata.h"
#include "model/calendar.h"
#include "model/competition.h"
#include "model/continental.h"

namespace
{
using Standings::RaceInput;

// Goal tallies that the remaining games can push without limit.
constexpr int64_t UNBOUNDED = std::numeric_limits<int32_t>::max();

constexpr int8_t UNDECIDED = -1;
constexpr int8_t HOME_WIN = 0;
constexpr int8_t DRAW = 1;
constexpr int8_t AWAY_WIN = 2;

// A club's own result in one of its games (whatYouNeed vectors).
constexpr int8_t OWN_WIN = 0;
constexpr int8_t OWN_DRAW = 1;
constexpr int8_t OWN_LOSS = 2;

constexpr int WIN_POINTS = 3;

// Search limits: one question about one club, and one whole report. When a
// limit is hit the answer is "unknown", which never decides anything.
constexpr size_t QUERY_NODE_LIMIT = 120'000;
constexpr size_t REPORT_NODE_LIMIT = 4'000'000;

enum class Answer : uint8_t
{
  YES,
  NO,
  UNKNOWN
};

/** Remaining results of one club, split by opponents inside the tied group. */
struct Record
{
  int wins = 0;
  int draws = 0;
  int losses = 0;
  int group_wins = 0;
  int group_losses = 0;
  int group_points = 0;

  [[nodiscard]] bool hasGames() const { return wins + draws + losses > 0; }
};

/**
 * Exhaustive search over the remaining results for one club `t`.
 *
 * Both questions are existential: "is there a completion of the season in
 * which at least `need` clubs finish above t, with t taking the given
 * results?" (t's worst case) and "is there one in which at most `limit`
 * clubs finish above t, with t winning every game?" (t's best case). Two
 * facts keep the search small: a club that already has more points than
 * t's final total, or that cannot reach it, is decided whatever happens;
 * games between two such clubs never matter and, in goal-difference
 * leagues, an undecided club's games against them take the outcome that
 * suits the question (under head-to-head they branch, since joining t's
 * points can reorder the mini-league). Clubs that finish
 * level on points with t are compared with the league's tie-break, treating
 * goal margins as free (any score that matches the result).
 */
class RaceSolver
{
 public:
  explicit RaceSolver(const RaceInput& input)
      : rule(input.rule), teams(input.teams), names(input.names)
  {
    const size_t count = input.teams.size();
    names.resize(count);
    index_of.reserve(count);
    for (size_t index = 0; index < count; ++index)
      index_of.emplace(teams[index], index);
    base_points.assign(count, 0);
    goal_diff.assign(count, 0);
    goals_for.assign(count, 0);
    pair_points.assign(count * count, 0);
    pair_diff.assign(count * count, 0);
    fixtures_of.assign(count, {});
    for (const Standings::RaceFixture& fixture : input.fixtures)
    {
      const auto home = index_of.find(fixture.home_id);
      const auto away = index_of.find(fixture.away_id);
      if (home == index_of.end() || away == index_of.end() ||
          home->second == away->second)
        continue;
      const size_t h = home->second;
      const size_t a = away->second;
      if (!fixture.played)
      {
        fixtures_of[h].push_back(remaining.size());
        fixtures_of[a].push_back(remaining.size());
        remaining.push_back({h, a});
        continue;
      }
      const int margin = fixture.home_goals - fixture.away_goals;
      goal_diff[h] += margin;
      goal_diff[a] -= margin;
      goals_for[h] += fixture.home_goals;
      goals_for[a] += fixture.away_goals;
      pair_diff[h * count + a] += margin;
      pair_diff[a * count + h] -= margin;
      int home_points = 0;
      int away_points = 0;
      if (margin > 0)
        home_points = WIN_POINTS;
      else if (margin == 0)
        home_points = 1;
      if (margin < 0)
        away_points = WIN_POINTS;
      else if (margin == 0)
        away_points = 1;
      base_points[h] += home_points;
      base_points[a] += away_points;
      pair_points[h * count + a] += home_points;
      pair_points[a * count + h] += away_points;
    }
    outcome.assign(remaining.size(), UNDECIDED);
    points.assign(count, 0);
    open.assign(count, 0);
    in_group.assign(count, false);
  }

  [[nodiscard]] size_t size() const { return teams.size(); }
  [[nodiscard]] std::optional<size_t> indexOf(TeamID team) const
  {
    const auto it = index_of.find(team);
    if (it == index_of.end()) return std::nullopt;
    return it->second;
  }
  [[nodiscard]] const std::vector<size_t>& fixturesOf(size_t team) const
  {
    return fixtures_of[team];
  }
  [[nodiscard]] std::pair<size_t, size_t> fixture(size_t index) const
  {
    return remaining[index];
  }
  [[nodiscard]] bool exhausted() const
  {
    return report_nodes >= REPORT_NODE_LIMIT;
  }

  /** Can at least `need` clubs finish above t? t's results: `own` (one per
   * game of fixturesOf(t), OWN_*), or every game lost when null. */
  Answer canPushAbove(size_t t, size_t need, const std::vector<int8_t>* own)
  {
    worst_case = true;
    threshold = need;
    return run(t, own);
  }

  /** Can at most `limit` clubs finish above t when t wins every game? */
  Answer canKeepBelow(size_t t, size_t limit)
  {
    worst_case = false;
    threshold = limit;
    return run(t, nullptr);
  }

 private:
  Answer run(size_t t, const std::vector<int8_t>* own)
  {
    target = t;
    std::ranges::fill(outcome, UNDECIDED);
    points = base_points;
    for (size_t club = 0; club < size(); ++club)
      open[club] = static_cast<int>(fixtures_of[club].size());
    const auto& games = fixtures_of[t];
    for (size_t k = 0; k < games.size(); ++k)
    {
      int8_t result = OWN_WIN;
      if (worst_case) result = own != nullptr ? (*own)[k] : OWN_LOSS;
      const bool at_home = remaining[games[k]].first == t;
      int8_t match = AWAY_WIN;
      if (result == OWN_DRAW)
        match = DRAW;
      else if ((result == OWN_WIN) == at_home)
        match = HOME_WIN;
      assign(games[k], match);
    }
    final_points = points[t];
    query_nodes = 0;
    aborted = false;
    if (const bool found = search(0); found) return Answer::YES;
    return aborted ? Answer::UNKNOWN : Answer::NO;
  }

  void assign(size_t index, int8_t result)
  {
    const auto [home, away] = remaining[index];
    outcome[index] = result;
    --open[home];
    --open[away];
    if (result == HOME_WIN)
      points[home] += WIN_POINTS;
    else if (result == AWAY_WIN)
      points[away] += WIN_POINTS;
    else
    {
      ++points[home];
      ++points[away];
    }
  }

  void unassign(size_t index)
  {
    const auto [home, away] = remaining[index];
    const int8_t result = outcome[index];
    outcome[index] = UNDECIDED;
    ++open[home];
    ++open[away];
    if (result == HOME_WIN)
      points[home] -= WIN_POINTS;
    else if (result == AWAY_WIN)
      points[away] -= WIN_POINTS;
    else
    {
      --points[home];
      --points[away];
    }
  }

  /** Neither surely above nor surely below t's final total yet. */
  [[nodiscard]] bool undecided(size_t club) const
  {
    return points[club] <= final_points &&
           points[club] + WIN_POINTS * open[club] >= final_points;
  }

  bool search(size_t position)
  {
    ++query_nodes;
    if (query_nodes > QUERY_NODE_LIMIT)
    {
      aborted = true;
      return false;
    }
    ++report_nodes;
    if (report_nodes > REPORT_NODE_LIMIT)
    {
      aborted = true;
      return false;
    }
    // Bounds: clubs surely above t, and clubs that might still end above.
    size_t above = 0;
    size_t maybe = 0;
    for (size_t club = 0; club < size(); ++club)
    {
      if (club == target) continue;
      if (points[club] > final_points)
        ++above;
      else if (undecided(club))
        ++maybe;
    }
    if (worst_case)
    {
      if (above >= threshold) return true;
      if (above + maybe < threshold) return false;
    }
    else
    {
      if (above > threshold) return false;
      if (above + maybe <= threshold) return true;
    }

    while (position < remaining.size() && outcome[position] != UNDECIDED)
      ++position;
    if (position == remaining.size()) return leaf();

    const auto [home, away] = remaining[position];
    const bool home_open = undecided(home);
    const bool away_open = undecided(away);
    if (!home_open && !away_open) return search(position + 1);
    if (home_open != away_open && rule == TieBreakRule::GOAL_DIFFERENCE)
    {
      // Only one side still matters: it wins when t's rivals should rise,
      // loses when they should fall. Not under head-to-head, where a club
      // joining t's points can reorder the whole mini-league.
      const bool home_wins = home_open == worst_case;
      assign(position, home_wins ? HOME_WIN : AWAY_WIN);
      const bool found = search(position + 1);
      unassign(position);
      return found;
    }
    for (const int8_t result : branchOrder(home, away))
    {
      assign(position, result);
      const bool found = search(position + 1);
      unassign(position);
      if (found) return true;
      if (aborted) return false;
    }
    return false;
  }

  /** Most promising result first (finds a witness fast). */
  [[nodiscard]] std::array<int8_t, 3> branchOrder(size_t home,
                                                  size_t away) const
  {
    const auto score = [&](int8_t result)
    {
      int home_after = points[home];
      int away_after = points[away];
      if (result == HOME_WIN)
        home_after += WIN_POINTS;
      else if (result == DRAW)
        ++home_after;
      if (result == AWAY_WIN)
        away_after += WIN_POINTS;
      else if (result == DRAW)
        ++away_after;
      const int passed = static_cast<int>(home_after > final_points) +
                         static_cast<int>(away_after > final_points);
      const int level = static_cast<int>(home_after >= final_points) +
                        static_cast<int>(away_after >= final_points);
      return passed * 4 + level;
    };
    std::array<int8_t, 3> order = {HOME_WIN, AWAY_WIN, DRAW};
    std::ranges::stable_sort(order,
                             [&](int8_t left, int8_t right)
                             {
                               return worst_case ? score(left) > score(right)
                                                 : score(left) < score(right);
                             });
    return order;
  }

  bool leaf()
  {
    size_t above = 0;
    group.clear();
    for (size_t club = 0; club < size(); ++club)
    {
      if (club == target) continue;
      if (points[club] > final_points)
        ++above;
      else if (points[club] == final_points)
        group.push_back(club);
    }
    std::ranges::fill(in_group, false);
    in_group[target] = true;
    for (const size_t club : group) in_group[club] = true;
    const Record own = recordOf(target);
    for (const size_t club : group)
    {
      const Record rival = recordOf(club);
      const bool counts = worst_case ? canFinishAbove(club, rival, own)
                                     : !canFinishBelow(club, rival, own);
      if (counts) ++above;
    }
    return worst_case ? above >= threshold : above <= threshold;
  }

  [[nodiscard]] Record recordOf(size_t club) const
  {
    Record record;
    for (const size_t index : fixtures_of[club])
    {
      const auto [home, away] = remaining[index];
      const int8_t result = outcome[index];
      if (result == UNDECIDED) continue;
      const bool at_home = home == club;
      const bool inside = in_group[at_home ? away : home];
      if (result == DRAW)
      {
        ++record.draws;
        if (inside) ++record.group_points;
      }
      else if ((result == HOME_WIN) == at_home)
      {
        ++record.wins;
        if (inside)
        {
          ++record.group_wins;
          record.group_points += WIN_POINTS;
        }
      }
      else
      {
        ++record.losses;
        if (inside) ++record.group_losses;
      }
    }
    return record;
  }

  [[nodiscard]] bool nameBefore(size_t left, size_t right) const
  {
    if (names[left] != names[right]) return names[left] < names[right];
    return teams[left] < teams[right];
  }

  /** Played head-to-head points and goal difference inside `members`. */
  [[nodiscard]] std::pair<int, int> miniLeague(
      size_t club, const std::vector<size_t>& members) const
  {
    int mini_points = 0;
    int mini_diff = 0;
    for (const size_t other : members)
    {
      if (other == club) continue;
      mini_points += pair_points[club * size() + other];
      mini_diff += pair_diff[club * size() + other];
    }
    return {mini_points, mini_diff};
  }

  /**
   * GOAL_DIFFERENCE rule, x and t level on points, goal difference and
   * goals: the head-to-head mini-league among every club level on all three
   * decides. Returns whether x ends up above t; nullopt when another club
   * that still plays could join that level (the caller then assumes the
   * outcome that suits the question).
   */
  [[nodiscard]] std::optional<bool> exactLevelAbove(size_t x, int64_t diff,
                                                    int64_t scored) const
  {
    // A club that could still have finished on these points (in another
    // completion) might have joined the level: undecided.
    for (size_t other = 0; other < size(); ++other)
    {
      if (other == target || other == x || in_group[other]) continue;
      const int most = base_points[other] +
                       WIN_POINTS * static_cast<int>(fixtures_of[other].size());
      if (base_points[other] <= final_points && final_points <= most &&
          !fixtures_of[other].empty())
        return std::nullopt;
    }
    std::vector members = {target, x};
    for (const size_t other : group)
    {
      if (other == x) continue;
      const Record record = recordOf(other);
      if (!record.hasGames())
      {
        if (goal_diff[other] == diff && goals_for[other] == scored)
          members.push_back(other);
        continue;
      }
      const int64_t low =
          record.losses > 0 ? -UNBOUNDED : goal_diff[other] + record.wins;
      const int64_t high =
          record.wins > 0 ? UNBOUNDED : goal_diff[other] - record.losses;
      if (low <= diff && diff <= high) return std::nullopt;
    }
    const auto [x_points, x_diff] = miniLeague(x, members);
    const auto [t_points, t_diff] = miniLeague(target, members);
    if (x_points != t_points) return x_points > t_points;
    if (x_diff != t_diff) return x_diff > t_diff;
    return nameBefore(x, target);
  }

  /** Group head-to-head points (played and remaining results). */
  [[nodiscard]] int groupPoints(size_t club, const Record& record) const
  {
    int total = record.group_points;
    for (size_t other = 0; other < size(); ++other)
      if (other != club && in_group[other])
        total += pair_points[club * size() + other];
    return total;
  }

  [[nodiscard]] int groupDiff(size_t club) const
  {
    int total = 0;
    for (size_t other = 0; other < size(); ++other)
      if (other != club && in_group[other])
        total += pair_diff[club * size() + other];
    return total;
  }

  /** t's worst case: can x (level on points) be ranked above t? t takes
   * the smallest margins it can (heavy defeats, narrow wins, 0-0 draws). */
  [[nodiscard]] bool canFinishAbove(size_t x, const Record& rx,
                                    const Record& rt) const
  {
    const auto t = target;
    if (rule == TieBreakRule::HEAD_TO_HEAD)
    {
      const int x_points = groupPoints(x, rx);
      if (const int t_points = groupPoints(t, rt); x_points != t_points)
        return x_points > t_points;
      if (rt.group_losses > 0 || rx.group_wins > 0) return true;
      const int64_t x_mini = groupDiff(x) - rx.group_losses;
      const int64_t t_mini = groupDiff(t) + rt.group_wins;
      if (x_mini != t_mini) return x_mini > t_mini;
    }
    if (rt.losses > 0 || rx.wins > 0) return true;
    const int64_t x_diff = goal_diff[x] - rx.losses;
    const int64_t t_diff = goal_diff[t] + rt.wins;
    if (x_diff != t_diff) return x_diff > t_diff;
    if (rx.hasGames()) return true;
    const int64_t t_scored = goals_for[t] + rt.wins;
    if (goals_for[x] != t_scored) return goals_for[x] > t_scored;
    if (rule == TieBreakRule::HEAD_TO_HEAD) return nameBefore(x, t);
    return exactLevelAbove(x, t_diff, t_scored).value_or(true);
  }

  /** t's best case: can x (level on points) be ranked below t? t wins every
   * game, as heavily as needed; x takes narrow wins and heavy defeats. */
  [[nodiscard]] bool canFinishBelow(size_t x, const Record& rx,
                                    const Record& rt) const
  {
    const auto t = target;
    if (rule == TieBreakRule::HEAD_TO_HEAD)
    {
      const int x_points = groupPoints(x, rx);
      if (const int t_points = groupPoints(t, rt); x_points != t_points)
        return x_points < t_points;
      if (rt.group_wins > 0 || rx.group_losses > 0) return true;
      const int64_t x_mini = groupDiff(x) + rx.group_wins;
      const int64_t t_mini = groupDiff(t) - rt.group_losses;
      if (x_mini != t_mini) return x_mini < t_mini;
    }
    if (rt.wins > 0 || rx.losses > 0) return true;
    const int64_t x_diff = goal_diff[x] + rx.wins;
    const int64_t t_diff = goal_diff[t] - rt.losses;
    if (x_diff != t_diff) return x_diff < t_diff;
    if (rt.hasGames()) return true;
    const int64_t x_scored = goals_for[x] + rx.wins;
    if (x_scored != goals_for[t]) return x_scored < goals_for[t];
    if (rule == TieBreakRule::HEAD_TO_HEAD) return nameBefore(t, x);
    const auto above = exactLevelAbove(x, t_diff, x_scored);
    return above ? !*above : true;
  }

  TieBreakRule rule;
  std::vector<TeamID> teams;
  std::vector<std::string> names;
  std::unordered_map<TeamID, size_t> index_of;
  std::vector<int> base_points;
  std::vector<int64_t> goal_diff;
  std::vector<int64_t> goals_for;
  std::vector<int> pair_points; /**< [i * n + j]: points i took from j. */
  std::vector<int> pair_diff;   /**< [i * n + j]: goal difference i vs j. */
  std::vector<std::pair<size_t, size_t>> remaining;
  std::vector<std::vector<size_t>> fixtures_of;

  // Per-query state.
  std::vector<int8_t> outcome;
  std::vector<int> points;
  std::vector<int> open;
  std::vector<bool> in_group;
  std::vector<size_t> group;
  size_t target = 0;
  int final_points = 0;
  bool worst_case = true;
  size_t threshold = 0;
  size_t query_nodes = 0;
  size_t report_nodes = 0;
  bool aborted = false;
};

using enum Standings::Race;
constexpr std::array<Standings::Race, Standings::RACE_COUNT> RACES = {
    TITLE, PROMOTION, PLAY_OFF, CONTINENTAL_TOP, CONTINENTAL, SURVIVAL};

Standings::ClinchReport reportFor(RaceSolver& solver, size_t club, TeamID team,
                                  const Standings::RacePlaces& places)
{
  using enum Answer;
  using enum Standings::RaceState;
  Standings::ClinchReport report;
  report.team_id = team;
  for (const Standings::Race race : RACES)
  {
    auto& state = report.races[static_cast<size_t>(std::to_underlying(race))];
    const uint16_t cutoff = Standings::raceCutoff(race, places, solver.size());
    if (cutoff == 0)
    {
      state = NONE;
      continue;
    }
    state = OPEN;
    if (solver.canPushAbove(club, cutoff, nullptr) == NO)
      state = SECURED;
    else if (solver.canKeepBelow(club, cutoff - 1u) == NO)
      state = LOST;
  }
  const auto secured = [&](Standings::Race race)
  {
    return report.races[static_cast<size_t>(std::to_underlying(race))] ==
           SECURED;
  };
  using Standings::Clinch;
  using Standings::Race;
  if (secured(Race::TITLE))
    report.status = Clinch::CHAMPION;
  else if (secured(Race::PROMOTION))
    report.status = Clinch::PROMOTED;
  else if (secured(Race::PLAY_OFF))
    report.status = Clinch::PLAY_OFF;
  else if (secured(Race::CONTINENTAL_TOP))
    report.status = Clinch::CONTINENTAL_TOP;
  else if (secured(Race::CONTINENTAL))
    report.status = Clinch::CONTINENTAL;
  else if (secured(Race::SURVIVAL))
    report.status = Clinch::SAFE;
  else if (report.races[static_cast<size_t>(
               std::to_underlying(Race::SURVIVAL))] == LOST)
    report.status = Clinch::RELEGATED;
  return report;
}

/** Points of a vector of own results. */
int ownPoints(const std::vector<int8_t>& results)
{
  int total = 0;
  for (const int8_t result : results)
    if (result == OWN_WIN)
      total += WIN_POINTS;
    else if (result == OWN_DRAW)
      ++total;
  return total;
}

/**
 * Does taking at least `minimum` points guarantee a top-`cutoff` finish,
 * whichever games bring them? Dropping a result never helps the club, so
 * only the result sets worth minimum..minimum+2 points need checking: any
 * richer set can be worsened step by step (draw to defeat, win to draw or
 * defeat) into one of them.
 */
Answer guarantees(RaceSolver& solver, size_t club, uint16_t cutoff, int minimum)
{
  using enum Answer;
  const size_t games = solver.fixturesOf(club).size();
  size_t combinations = 1;
  for (size_t game = 0; game < games; ++game) combinations *= 3;
  std::vector results(games, OWN_LOSS);
  bool unknown = false;
  for (size_t code = 0; code < combinations; ++code)
  {
    size_t rest = code;
    for (size_t game = 0; game < games; ++game)
    {
      results[game] = static_cast<int8_t>(rest % 3);
      rest /= 3;
    }
    if (const int total = ownPoints(results);
        total < minimum || total > minimum + 2)
      continue;
    const Answer answer = solver.canPushAbove(club, cutoff, &results);
    if (answer == YES) return NO;
    if (answer == UNKNOWN) unknown = true;
  }
  return unknown ? UNKNOWN : YES;
}
}  // namespace

uint16_t Standings::raceCutoff(Race race, const RacePlaces& places,
                               size_t clubs)
{
  uint16_t cutoff = 0;
  switch (race)
  {
    case Race::TITLE:
      cutoff = 1;
      break;
    case Race::PROMOTION:
      cutoff = places.promotion;
      break;
    case Race::PLAY_OFF:
      cutoff = places.play_off > 0
                   ? static_cast<uint16_t>(places.promotion + places.play_off)
                   : uint16_t{0};
      break;
    case Race::CONTINENTAL_TOP:
      cutoff = places.continental_top;
      break;
    case Race::CONTINENTAL:
      cutoff = places.continental > places.continental_top ? places.continental
                                                           : uint16_t{0};
      break;
    case Race::SURVIVAL:
      cutoff = places.relegation > 0 && places.relegation < clubs
                   ? static_cast<uint16_t>(clubs - places.relegation)
                   : uint16_t{0};
      break;
  }
  // A race every club is part of decides nothing.
  return cutoff < clubs ? cutoff : uint16_t{0};
}

Standings::ClinchReport Standings::clinchReport(const RaceInput& input,
                                                const RacePlaces& places,
                                                TeamID team)
{
  RaceSolver solver(input);
  const auto club = solver.indexOf(team);
  if (!club.has_value())
  {
    ClinchReport none;
    none.team_id = team;
    return none;
  }
  return reportFor(solver, *club, team, places);
}

Standings::Clinch Standings::clinchStatus(const RaceInput& input,
                                          const RacePlaces& places, TeamID team)
{
  return clinchReport(input, places, team).status;
}

std::vector<Standings::ClinchReport> Standings::clinchReports(
    const RaceInput& input, const RacePlaces& places)
{
  RaceSolver solver(input);
  std::vector<ClinchReport> reports;
  reports.reserve(solver.size());
  for (size_t club = 0; club < solver.size(); ++club)
    reports.push_back(reportFor(solver, club, input.teams[club], places));
  return reports;
}

std::vector<Standings::RaceNeed> Standings::whatYouNeed(
    const RaceInput& input, const RacePlaces& places, TeamID team)
{
  std::vector<RaceNeed> needs;
  RaceSolver solver(input);
  const auto club = solver.indexOf(team);
  if (!club.has_value()) return needs;
  const auto& games = solver.fixturesOf(*club);
  if (games.empty() || games.size() > MAX_NEED_GAMES) return needs;
  const int most = WIN_POINTS * static_cast<int>(games.size());
  const auto [home, away] = solver.fixture(games.front());
  const bool at_home = home == *club;
  const TeamID next = input.teams[at_home ? away : home];

  const ClinchReport report = reportFor(solver, *club, team, places);
  for (const Race race : RACES)
  {
    if (report.races[static_cast<size_t>(std::to_underlying(race))] !=
        RaceState::OPEN)
      continue;
    const uint16_t cutoff = raceCutoff(race, places, solver.size());
    RaceNeed need;
    need.race = race;
    need.games_left = static_cast<uint16_t>(games.size());
    need.next_opponent = next;
    need.next_at_home = at_home;
    const Answer best = guarantees(solver, *club, cutoff, most);
    if (best == Answer::UNKNOWN) continue;
    if (best == Answer::NO)
    {
      need.kind = RaceNeed::Kind::NEEDS_HELP;
      needs.push_back(need);
      continue;
    }
    // Smallest total that still guarantees the place (monotone in points).
    int low = 1;
    int high = most;
    bool unknown = false;
    while (low < high)
    {
      const int middle = std::midpoint(low, high);
      const Answer answer = guarantees(solver, *club, cutoff, middle);
      if (answer == Answer::UNKNOWN)
      {
        unknown = true;
        break;
      }
      if (answer == Answer::YES)
        high = middle;
      else
        low = middle + 1;
    }
    if (unknown) continue;
    // most - 1 points cannot be taken (it would need a draw on top of all
    // wins), so it means winning every game.
    need.points = static_cast<uint16_t>(low == most - 1 ? most : low);
    need.kind = RaceNeed::Kind::IN_HANDS;
    needs.push_back(need);
  }
  if (solver.exhausted()) needs.clear();
  return needs;
}

Standings::RaceInput Standings::raceInput(const League& league,
                                          const Calendar& calendar,
                                          const GameData& gamedata)
{
  RaceInput input;
  input.rule = league.getTieBreakRule();
  input.teams = league.getTeamIDs();
  input.names.reserve(input.teams.size());
  for (const TeamID id : input.teams)
  {
    const auto team = gamedata.getTeam(id);
    input.names.push_back(team ? team->get().getName() : std::string());
  }
  const auto member = [&input](TeamID id)
  { return std::ranges::find(input.teams, id) != input.teams.end(); };
  for (const auto& [date, matches] : calendar.getFullCalendar())
  {
    for (const Match& match : matches)
    {
      if (match.getMatchType() != MatchType::LEAGUE) continue;
      if (match.getCompetitionId() != 0 &&
          match.getCompetitionId() != league.getId())
        continue;
      if (!member(match.getHomeTeamId()) || !member(match.getAwayTeamId()))
        continue;
      RaceFixture fixture;
      fixture.home_id = match.getHomeTeamId();
      fixture.away_id = match.getAwayTeamId();
      fixture.played = match.isPlayed();
      fixture.home_goals = match.getHomeScore();
      fixture.away_goals = match.getAwayScore();
      input.fixtures.push_back(fixture);
    }
  }
  return input;
}

Standings::RacePlaces Standings::racePlaces(
    const GameData& gamedata, const ContinentalCompetitions* continental,
    LeagueID league_id)
{
  RacePlaces places;
  const auto league = gamedata.getLeague(league_id);
  if (!league) return places;
  const size_t clubs = league->get().getTeamIDs().size();

  // Promotion and relegation exactly as Competitions::computeLeagueMovements
  // swaps clubs between a division and the one(s) below it.
  const auto childrenOf = [&gamedata](LeagueID parent)
  {
    std::vector<LeagueID> children;
    for (const auto& [id, other] : gamedata.getLeagues())
      if (other.getParentLeagueID() == parent) children.push_back(id);
    return children;
  };
  const auto slotsBetween = [&](const League& parent, const League& child)
  {
    const size_t children =
        std::max<size_t>(1, childrenOf(parent.getId()).size());
    return std::min({Competitions::PROMOTION_SLOTS,
                     child.getTeamIDs().size() / 4,
                     parent.getTeamIDs().size() / (4 * children)});
  };
  const auto parent_id = league->get().getParentLeagueID();
  if (parent_id.has_value())
    if (const auto parent = gamedata.getLeague(*parent_id))
      places.promotion =
          static_cast<uint16_t>(slotsBetween(parent->get(), league->get()));
  size_t relegation = 0;
  for (const LeagueID child_id : childrenOf(league_id))
    if (const auto child = gamedata.getLeague(child_id))
      relegation += slotsBetween(league->get(), child->get());
  places.relegation = static_cast<uint16_t>(std::min(relegation, clubs));

  // Continental places: top divisions only, from the association ranking.
  const auto continent = Continental::continentOf(league_id);
  if (parent_id.has_value() || continental == nullptr || !continent.has_value())
    return places;
  const auto ranking = continental->associationRanking(*continent);
  const auto mine = std::ranges::find(
      ranking, league_id,
      &ContinentalCompetitions::AssociationCoefficient::association);
  if (mine == ranking.end()) return places;
  std::vector<size_t> sizes;
  for (const auto& entry : ranking)
  {
    const auto top = gamedata.getLeague(entry.association);
    sizes.push_back(top ? top->get().getTeamIDs().size() : 0);
  }
  // The ranking is updated with this season's results before qualifying,
  // so assume the association may still drop one place.
  const auto current = static_cast<size_t>(mine - ranking.begin());
  const auto placesAt = [&](size_t rank) -> std::pair<uint16_t, uint16_t>
  {
    std::vector<size_t> taken(sizes.size(), 0);
    uint16_t top = 0;
    uint16_t total = 0;
    bool first = true;
    for (const auto& rules : Continental::COMPETITIONS)
    {
      if (rules.continent != *continent) continue;
      std::vector<uint8_t> capacity;
      for (size_t index = 0; index < sizes.size(); ++index)
        capacity.push_back(static_cast<uint8_t>(std::min<size_t>(
            sizes[index] - std::min(sizes[index], taken[index]), 0xFF)));
      const std::vector<uint8_t> granted =
          Continental::allocatePlaces(rules, sizes.size(), capacity);
      for (size_t index = 0; index < granted.size(); ++index)
        taken[index] += granted[index];
      const uint8_t own = rank < granted.size() ? granted[rank] : uint8_t{0};
      if (first) top = own;
      first = false;
      total = static_cast<uint16_t>(total + own);
      // The lowest competition gives one place to the domestic cup winner,
      // who may come from outside the league's qualifying places.
      bool lower = false;
      bool higher = false;
      for (const auto& other : Continental::COMPETITIONS)
      {
        if (other.continent != rules.continent || other.id == rules.id)
          continue;
        lower = lower || other.tier > rules.tier;
        higher = higher || other.tier < rules.tier;
      }
      if (!lower && higher && own > 0) --total;
    }
    return {top, total};
  };
  auto [top, total] = placesAt(current);
  if (current + 1 < ranking.size())
  {
    const auto [lower_top, lower_total] = placesAt(current + 1);
    top = std::min(top, lower_top);
    total = std::min(total, lower_total);
  }
  places.continental_top = top;
  places.continental = std::max(total, top);
  return places;
}
