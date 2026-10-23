// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Season end and off-season of GameController: the off-season Continue,
// past tables, the season summary and the board's targets.

#include <algorithm>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "model/transfer_negotiation.h"

int GameController::advanceToNextEvent(int max_days)
{
  const std::scoped_lock runtime_lock(runtime_mutex);
  if (!editor_session.expired()) return 0;
  last_continue_stop = ContinueStop::None;
  if (!game || max_days <= 0 || !hasSelectedTeam()) return 0;
  continue_stop_requested = false;
  const TeamID managed = game->getManagedTeamId();
  const Inbox& inbox = game->getWorld().getInbox();
  const auto nextId = [&inbox]
  {
    return inbox.getMessages().empty() ? 0 : inbox.getMessages().back().id + 1;
  };
  game->resetSimulationProgress();
  continue_days_started = 0;
  // The length is not known in advance: the progress card shows the days.
  continue_days_total = 0;
  ContinueStop stop = ContinueStop::None;
  int days = 0;
  while (stop == ContinueStop::None)
  {
    if (days >= max_days)
    {
      stop = ContinueStop::DayLimit;
      break;
    }
    if (continue_stop_requested)
    {
      stop = ContinueStop::Requested;
      break;
    }
    const uint32_t first_new = nextId();
    const bool window_was_open = isTransferWindowOpen();
    simulateDay();
    ++days;
    const GameDateValue& today = game->getCurrentDate();
    if (game->getManagedTeamId() != managed)
    {
      stop = ContinueStop::LostJob;
      break;
    }
    if (today.month == 7 && today.day == 1)
    {
      stop = ContinueStop::NewSeason;
      break;
    }
    // Bids for the club's players reach the inbox, one unread a week.
    if (game->getLastCareerEvents().new_offer ||
        game->getLastNationalEvents().new_offer ||
        game->getLastNationalEvents().squad_to_pick)
      stop = ContinueStop::Decision;
    if (stop != ContinueStop::None) break;
    // Only news that needs the manager arrives unread.
    if (std::ranges::any_of(
            inbox.getMessages(), [first_new](const InboxMessage& message)
            { return message.id >= first_new && !message.read; }))
      stop = ContinueStop::Message;
    else if (!window_was_open && isTransferWindowOpen())
      stop = ContinueStop::WindowOpened;
    else if (std::ranges::any_of(
                 getTeamFixtures(managed), [&today](const Match& match)
                 { return !match.isPlayed() && !(match.getDate() < today); }))
      stop = ContinueStop::Fixture;
  }
  last_continue_stop = stop;
  return days;
}

const char* GameController::continueStopKey(ContinueStop stop)
{
  switch (stop)
  {
    case ContinueStop::None:
    case ContinueStop::Fixture:
      return "CONTINUE_STOP_FIXTURE";
    case ContinueStop::Message:
      return "CONTINUE_STOP_MESSAGE";
    case ContinueStop::Decision:
      return "CONTINUE_STOP_DECISION";
    case ContinueStop::WindowOpened:
      return "CONTINUE_STOP_WINDOW";
    case ContinueStop::NewSeason:
      return "CONTINUE_STOP_NEW_SEASON";
    case ContinueStop::LostJob:
      return "CONTINUE_STOP_LOST_JOB";
    case ContinueStop::Requested:
      return "CONTINUE_STOP_REQUESTED";
    case ContinueStop::DayLimit:
      break;
  }
  return "CONTINUE_STOP_DAY_LIMIT";
}

std::vector<uint16_t> GameController::getArchivedSeasons(
    LeagueID league_id) const
{
  return game ? game->getSeasonArchive().seasonsOf(league_id)
              : std::vector<uint16_t>{};
}

const std::vector<StandingRow>* GameController::getArchivedTable(
    uint16_t season, LeagueID league_id) const
{
  return game ? game->getSeasonArchive().table(season, league_id) : nullptr;
}

uint16_t GameController::getSeasonStartYear(uint16_t season) const
{
  return game ? game->getSeasonArchive().startYear(season) : 0;
}

std::optional<std::pair<LeagueID, uint16_t>> GameController::getArchivedPlacing(
    uint16_t season, TeamID team_id) const
{
  if (!game) return std::nullopt;
  return game->getSeasonArchive().placing(season, team_id);
}

const SeasonReview* GameController::getUnseenSeasonReview() const
{
  return game ? game->getSeasonArchive().unseenReview() : nullptr;
}

void GameController::markSeasonReviewSeen(uint16_t season)
{
  if (game) game->getSeasonArchive().markSeen(season);
}

std::optional<GameController::BoardTargets> GameController::getBoardTargets()
    const
{
  const auto club = managedClub();
  if (!game || !club) return std::nullopt;
  const BoardState& board = game->getWorld().getBoardState();
  if (board.team_id != club->get().getId()) return std::nullopt;
  const Team& team = club->get();
  BoardTargets targets;
  targets.league = board.objective;
  targets.target_position = board.target_position;
  const std::vector<StandingRow> table = getStandings(team.getLeagueId());
  for (const StandingRow& row : table)
    if (row.team_id == team.getId() && row.played > 0)
      targets.position = row.position;
  if (targets.position > 0)
    targets.league_grade =
        BoardModel::gradeLeague(board.target_position, targets.position,
                                static_cast<int>(table.size()));

  const Game::CupRun run = game->cupRun(team.getId());
  targets.cup = board.cup_objective;
  targets.cup_still_in = run.still_in;
  targets.cup_grade =
      BoardModel::gradeCup(board.cup_objective, run.rounds_left, run.won,
                           run.still_in || !run.rounds_left);

  const Finances& finances = team.getFinances();
  targets.finances = board.finance_objective;
  targets.start_balance = board.start_balance;
  targets.finance_grade = BoardModel::gradeFinances(
      board.finance_objective, finances.getBalance(), board.start_balance,
      finances.getCurrentWageSpending(*gamedata, team) >
          finances.getWageBudget());

  // During the season young players are still earning their places: short
  // of the target is behind schedule, not yet a failure.
  targets.youth_target = board.youth_target;
  targets.young_regulars = game->youngRegulars(team.getId());
  targets.youth_grade =
      BoardModel::gradeYouth(board.youth_target, targets.young_regulars);
  if (targets.youth_grade == ObjectiveGrade::Failed)
    targets.youth_grade = ObjectiveGrade::Missed;
  // A board of an older save sets these targets at the next season start;
  // until then they are not judged (SeasonVerdictInputs::targets_set).
  if (!board.targets_set)
  {
    targets.cup_grade = ObjectiveGrade::Met;
    targets.finance_grade = ObjectiveGrade::Met;
    targets.youth_grade = ObjectiveGrade::Met;
  }
  return targets;
}
