// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The managed club's season end: its numbers, the board's verdict, the news
// and the line-up gaps left by players who leave.

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>

#include "database/gamedata.h"
#include "model/competition.h"
#include "model/continental.h"
#include "model/game.h"
#include "model/league.h"
#include "model/player.h"
#include "model/team.h"

namespace
{
/** Appearances that make a young player a regular. [P] */
constexpr std::uint16_t YOUNG_REGULAR_APPEARANCES = 10;
constexpr std::uint8_t YOUNG_REGULAR_AGE = 21;
}  // namespace

int Game::youngRegulars(TeamID team_id) const
{
  int count = 0;
  for (const auto& player : gamedata->getPlayersForTeam(team_id))
    if (player.get().getAge() <= YOUNG_REGULAR_AGE &&
        player.get().getDynamics().season_appearances >=
            YOUNG_REGULAR_APPEARANCES)
      ++count;
  return count;
}

Game::CupRun Game::cupRun(TeamID team_id) const
{
  CupRun run;
  const auto team = gamedata->getTeam(team_id);
  if (!team) return run;
  const Competitions::CupStatus status = Competitions::cupStatus(
      calendar, *gamedata,
      Competitions::rootLeague(*gamedata, team->get().getLeagueId()));
  int furthest = 0;
  for (const Competitions::CupRound& round : status.rounds)
    for (const Match& tie : round.ties)
      if (tie.getHomeTeamId() == team_id || tie.getAwayTeamId() == team_id)
        furthest = std::max<int>(furthest, round.stage);
  if (furthest > 0) run.rounds_left = status.total_rounds - furthest;
  run.won = status.winner.has_value() && *status.winner == team_id;
  run.still_in = !status.winner.has_value() && !status.rounds.empty() &&
                 std::ranges::contains(status.remaining, team_id);
  return run;
}

void Game::fillLineupGap(Team& team, const Player& departed)
{
  const Lineup before = team.getLineup();
  transfers.removeFromLineup(team, departed);
  for (const auto& [replaced, replacement] :
       MatchdaySquad::replacements(before, team.getLineup()))
  {
    // Only starting places are news; the bench simply loses a name.
    if (!before.isStarter(replaced)) continue;
    const auto player = gamedata->getPlayer(replacement);
    lineup_repairs.emplace_back(departed.getName(),
                                player ? player->get().getName() : "");
  }
}

void Game::postLineupRepairs()
{
  if (lineup_repairs.empty() || managed_team_id == FREE_AGENTS_TEAM_ID) return;
  const auto join = [](std::string& list, std::string_view name)
  {
    if (name.empty()) return;
    if (!list.empty()) list += ", ";
    list += name;
  };
  std::string departed;
  std::string replacements;
  for (const auto& [gone, replacement] : lineup_repairs)
  {
    join(departed, gone);
    join(replacements, replacement);
  }
  InboxMessage message;
  message.date = currentDate;
  message.category = InboxCategory::General;
  message.title_key = "INBOX_LINEUP_GAPS_TITLE";
  message.body_key = replacements.empty() ? "INBOX_LINEUP_GAPS_OPEN_BODY"
                                          : "INBOX_LINEUP_GAPS_BODY";
  message.args = {departed, replacements};
  message.team_id = managed_team_id;
  world.getInbox().add(std::move(message));
  lineup_repairs.clear();
}

void Game::beginSeasonReview(
    const std::map<LeagueID, std::vector<StandingRow>>& final_tables,
    std::uint16_t start_year)
{
  closing_review.reset();
  const BoardState& board = world.getBoardState();
  const auto team = gamedata->getTeam(managed_team_id);
  if (managed_team_id == FREE_AGENTS_TEAM_ID || !team ||
      board.team_id != managed_team_id)
    return;
  const Team& club = team->get();
  SeasonReview review;
  review.season = current_season;
  review.start_year = start_year;
  review.team_id = managed_team_id;
  review.league_id = club.getLeagueId();
  if (const auto table = final_tables.find(review.league_id);
      table != final_tables.end())
  {
    review.league_size = static_cast<std::uint8_t>(
        std::min<std::size_t>(table->second.size(), 255));
    for (const StandingRow& row : table->second)
    {
      if (row.team_id != managed_team_id) continue;
      review.position =
          static_cast<std::uint8_t>(std::min<int>(row.position, 255));
      review.played = row.played;
      review.won = row.won;
      review.drawn = row.drawn;
      review.lost = row.lost;
      review.goals_for = row.goals_for;
      review.goals_against = row.goals_against;
      review.points = row.points;
    }
  }
  review.target_position = board.target_position;
  review.objective = board.objective;
  review.cup_objective = board.cup_objective;
  review.finance_objective = board.finance_objective;
  review.youth_target = board.youth_target;
  review.young_regulars =
      static_cast<std::uint8_t>(std::min(youngRegulars(managed_team_id), 255));
  // League scorers are read while the club is still in this season's league.
  for (const PlayerSeasonStats& stats :
       competitions.getTopScorers(MatchType::LEAGUE, review.league_id,
                                  std::numeric_limits<std::size_t>::max()))
  {
    if (stats.team_id != managed_team_id) continue;
    if (const auto player = gamedata->getPlayer(stats.player_id))
    {
      review.top_scorer = player->get().getName();
      review.top_scorer_goals = stats.goals;
    }
    break;
  }
  review.prize_money = world.getSeasonPrizeMoney();
  review.balance = club.getFinances().getBalance();
  review.confidence_before = board.confidence;
  closing_review = std::move(review);
}

void Game::judgeSeason(std::span<const SeasonHistoryEntry> finished)
{
  if (!closing_review) return;
  SeasonReview& review = *closing_review;
  const auto team = gamedata->getTeam(review.team_id);
  if (!team) return;
  const Team& club = team->get();
  const BoardState& board = world.getBoardState();
  const LeagueID root = Competitions::rootLeague(*gamedata, review.league_id);
  for (const SeasonHistoryEntry& entry : finished)
  {
    if (entry.competition_type == MatchType::CUP &&
        entry.competition_id == root)
      review.cup_won = entry.champion_id == review.team_id;
    if (entry.competition_type != MatchType::LEAGUE ||
        entry.competition_id != review.league_id)
      continue;
    review.champion = entry.champion_id == review.team_id;
    review.promoted = std::ranges::contains(entry.promoted, review.team_id);
    review.relegated = std::ranges::contains(entry.relegated, review.team_id);
  }
  review.next_league_id = club.getLeagueId();
  for (const auto& [competition_id, entrants] :
       competitions.getContinental().getQualified())
    if (std::ranges::any_of(entrants, [&](const auto& entrant)
                            { return entrant.team_id == review.team_id; }))
      review.continental_id = competition_id;

  const Finances& finances = club.getFinances();
  const CupRun run = cupRun(review.team_id);
  SeasonVerdictInputs inputs;
  inputs.objective = review.objective;
  inputs.target_position = review.target_position;
  inputs.position = review.position;
  inputs.league_size = review.league_size;
  inputs.champion = review.champion;
  inputs.promoted = review.promoted;
  inputs.relegated = review.relegated;
  inputs.cup = BoardModel::gradeCup(review.cup_objective, run.rounds_left,
                                    review.cup_won, false);
  inputs.finances = BoardModel::gradeFinances(
      review.finance_objective, finances.getBalance(), board.start_balance,
      finances.getCurrentWageSpending(*gamedata, club) >
          finances.getWageBudget());
  inputs.youth =
      BoardModel::gradeYouth(review.youth_target, review.young_regulars);
  inputs.targets_set = board.targets_set;
  inputs.league_matches = board.league_matches;
  inputs.confidence = board.confidence;
  const SeasonReview* previous =
      season_archive.review(static_cast<std::uint16_t>(review.season - 1));
  inputs.warned_last_season = previous && previous->team_id == review.team_id &&
                              previous->result.verdict == SeasonVerdict::Warned;
  review.result = SeasonReviewModel::judge(inputs);
  world.adjustBoardConfidence(review.result.confidence - board.confidence);

  SeasonReviewModel::EventNames names;
  names.club = club.getName();
  if (const auto league = gamedata->getLeague(review.league_id))
    names.league = Competitions::leagueNameArg(league->get());
  if (const auto league = gamedata->getLeague(review.next_league_id))
    names.next_league = Competitions::leagueNameArg(league->get());
  names.cup = Competitions::cupName(*gamedata, root);
  if (const Continental::CompetitionRules* rules =
          Continental::rules(review.continental_id))
    names.continental = rules->name_key;
  names.relegation_places = 0;
  for (const auto& [league_id, league] : gamedata->getLeagues())
    if (league.getParentLeagueID() == review.league_id)
      names.relegation_places = static_cast<int>(Competitions::PROMOTION_SLOTS);
  SeasonReviewModel::postEvents(world.getInbox(), currentDate, review, names);
}

void Game::finishSeasonReview()
{
  if (!closing_review) return;
  SeasonReview review = std::move(*closing_review);
  closing_review.reset();
  if (const BoardState& board = world.getBoardState();
      managed_team_id == review.team_id && board.team_id == review.team_id)
  {
    review.next_objective = board.objective;
    review.next_target = board.target_position;
    review.next_cup = board.cup_objective;
    review.next_finances = board.finance_objective;
    review.next_youth = board.youth_target;
  }
  season_archive.recordReview(std::move(review));
}
