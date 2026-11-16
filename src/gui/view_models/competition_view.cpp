// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/view_models/competition_view.h"

#include <algorithm>

#include "controller/game_controller.h"
#include "model/competition.h"
#include "model/game.h"
#include "model/standings.h"

namespace
{
std::string teamName(const GameController& controller, TeamID id)
{
  const auto team = controller.getTeamById(id);
  return team ? team->get().getName() : std::string();
}

CompetitionView::FixtureRow makeRow(const GameController& controller,
                                    const GameDateValue& date,
                                    const Match& match, int round)
{
  CompetitionView::FixtureRow row;
  row.date = date;
  row.home_id = match.getHomeTeamId();
  row.away_id = match.getAwayTeamId();
  row.home_name = teamName(controller, row.home_id);
  row.away_name = teamName(controller, row.away_id);
  row.type = match.getMatchType();
  row.played = match.isPlayed();
  row.home_score = match.getHomeScore();
  row.away_score = match.getAwayScore();
  row.round = round;
  row.kickoff = match.getKickoff();
  return row;
}

void pushForm(CompetitionView::StandingRow& row, UI::Outcome outcome)
{
  if (row.form_count < CompetitionView::FORM_LENGTH)
  {
    row.form[row.form_count++] = outcome;
    return;
  }
  std::shift_left(row.form.begin(), row.form.end(), 1);
  row.form.back() = outcome;
}
}  // namespace

namespace CompetitionView
{

std::vector<StandingRow> buildStandings(const GameController& controller,
                                        LeagueID leagueId)
{
  // Table order, points and tie-breakers come from the competitions model.
  std::vector<StandingRow> rows;
  using enum UI::Outcome;
  const std::vector<::StandingRow> table = controller.getStandings(leagueId);
  rows.reserve(table.size());
  for (const ::StandingRow& source : table)
  {
    StandingRow& row = rows.emplace_back();
    row.team_id = source.team_id;
    row.name = teamName(controller, source.team_id);
    row.played = source.played;
    row.won = source.won;
    row.drawn = source.drawn;
    row.lost = source.lost;
    row.goals_for = source.goals_for;
    row.goals_against = source.goals_against;
    row.points = source.points;
    for (const char result : source.form)
    {
      if (result == 'W')
        pushForm(row, WIN);
      else if (result == 'D')
        pushForm(row, DRAW);
      else if (result == 'L')
        pushForm(row, LOSS);
    }
  }
  return rows;
}

Zones zonesFor(const GameController& controller, LeagueID leagueId)
{
  // Mirrors Competitions::computeLeagueMovements: each parent/child pair
  // exchanges min(3, child size / 4, parent size / (4 * children)) clubs.
  Zones zones;
  const auto league = controller.getLeagueById(leagueId);
  if (!league) return zones;
  const size_t size = league->get().getTeamIDs().size();
  const auto slotsBetween =
      [&controller](const League& parent, const League& child)
  {
    size_t children = 0;
    for (const auto& other : controller.getLeagues())
      if (other.get().getParentLeagueID() == parent.getId()) ++children;
    return std::min(
        {Competitions::PROMOTION_SLOTS, child.getTeamIDs().size() / 4,
         parent.getTeamIDs().size() / (4 * std::max<size_t>(1, children))});
  };
  if (const auto parentId = league->get().getParentLeagueID();
      parentId.has_value())
    if (const auto parent = controller.getLeagueById(*parentId))
      zones.promotion = slotsBetween(parent->get(), league->get());
  for (const auto& other : controller.getLeagues())
    if (other.get().getParentLeagueID() == leagueId)
      zones.relegation += slotsBetween(league->get(), other.get());
  zones.relegation = std::min(zones.relegation, size);
  return zones;
}

std::vector<FixtureRow> buildLeagueFixtures(const GameController& controller,
                                            LeagueID leagueId)
{
  std::vector<FixtureRow> rows;
  const Game* game = controller.getGame();
  if (!game) return rows;
  for (const auto& [date, matches] : game->getCalendar().getFullCalendar())
    for (const Match& match : matches)
      if (match.getMatchType() == MatchType::LEAGUE &&
          match.getCompetitionId() == leagueId)
        rows.push_back(makeRow(controller, date, match, match.getStage()));
  // Rounds split over a weekend stay together.
  std::ranges::stable_sort(rows, {}, &FixtureRow::round);
  return rows;
}

std::vector<FixtureRow> buildClubFixtures(const GameController& controller,
                                          TeamID teamId)
{
  std::vector<FixtureRow> rows;
  int leagueMatchday = 0;
  for (const Match& match : controller.getTeamFixtures(teamId))
  {
    const int round =
        match.getMatchType() == MatchType::LEAGUE ? ++leagueMatchday : 0;
    rows.push_back(makeRow(controller, match.getDate(), match, round));
  }
  return rows;
}

std::optional<FixtureRow> nextFixture(const GameController& controller,
                                      TeamID teamId)
{
  const Game* game = controller.getGame();
  if (!game) return std::nullopt;
  const GameDateValue today = controller.getCurrentDate();
  for (const auto& [date, matches] : game->getCalendar().getFullCalendar())
  {
    if (date < today) continue;
    for (const Match& match : matches)
    {
      if (!match.isPlayed() &&
          (match.getHomeTeamId() == teamId || match.getAwayTeamId() == teamId))
        return makeRow(controller, date, match, 0);
    }
  }
  return std::nullopt;
}

const char* matchTypeKey(MatchType type)
{
  using enum MatchType;
  switch (type)
  {
    case LEAGUE:
      return "MATCH_TYPE_LEAGUE";
    case FRIENDLY:
      return "MATCH_TYPE_FRIENDLY";
    case CUP:
      return "MATCH_TYPE_CUP";
    case CONTINENTAL:
      return "MATCH_TYPE_CONTINENTAL";
  }
  return "MATCH_TYPE_LEAGUE";
}

UI::Outcome outcomeFor(const FixtureRow& fixture, TeamID teamId)
{
  using enum UI::Outcome;
  const bool home = fixture.home_id == teamId;
  const int scored = home ? fixture.home_score : fixture.away_score;
  const int conceded = home ? fixture.away_score : fixture.home_score;
  if (scored > conceded) return WIN;
  if (scored < conceded) return LOSS;
  return DRAW;
}

}  // namespace CompetitionView
