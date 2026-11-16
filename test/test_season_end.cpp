// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The end of a season: the board's verdict and its targets, the season news
// in order, archived tables, the manager's line-up, and the off-season
// Continue (with Stop) that leads into the next season.

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <string_view>
#include <thread>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/background_task.h"
#include "global/language_manager.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/board.h"
#include "model/calendar.h"
#include "model/competition.h"
#include "model/game.h"
#include "model/holiday.h"
#include "model/season_review.h"
#include "model/world_tuning.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 7'000'000 + (getpid() % 100'000) * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

/** The best-paid club of a top division that has a division below it. */
TeamID favouriteOfATopDivision(const GameController& controller)
{
  const auto data = controller.getGameData();
  for (const auto& league : controller.getLeagues())
  {
    const LeagueID id = league.get().getId();
    if (controller.getLeagueTier(id) != 1) continue;
    if (const bool has_lower = std::ranges::any_of(
            controller.getLeagues(), [id](const auto& other)
            { return other.get().getParentLeagueID() == id; });
        !has_lower)
      continue;
    TeamID best = 0;
    std::int64_t best_wages = -1;
    for (const TeamID team_id : league.get().getTeamIDs())
    {
      const Team& team = controller.getTeamById(team_id)->get();
      const std::int64_t wages =
          team.getFinances().getCurrentWageSpending(*data, team);
      if (wages > best_wages)
      {
        best_wages = wages;
        best = team_id;
      }
    }
    return best;
  }
  return 0;
}

std::unique_ptr<GameController> careerAt(int slot, TeamID& managed)
{
  Logger::init();
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot, WORLD_SEED);
  managed = favouriteOfATopDivision(*controller);
  controller->selectManagedTeam(managed);
  return controller;
}

/**
 * Plays the calendar by results: the managed club wins (or loses) every
 * match through the real pipeline, everybody else gets a fixed score, so a
 * season passes in seconds.
 */
void playUntil(GameController& controller, TeamID managed, bool wins,
               const GameDateValue& stop, bool protect_job = false)
{
  Game& game = *controller.getGame();
  while (game.getCurrentDate() < stop && game.getManagedTeamId() == managed)
  {
    const GameDateValue today = game.getCurrentDate();
    for (const Match& match : controller.getTeamFixtures(managed))
    {
      if (match.getDate() != today || match.isPlayed()) continue;
      const bool home = match.getHomeTeamId() == managed;
      const std::uint8_t ours = wins ? 3 : 0;
      const std::uint8_t theirs = wins ? 0 : 3;
      controller.setMatchResult(today, match.getHomeTeamId(),
                                match.getAwayTeamId(), home ? ours : theirs,
                                home ? theirs : ours);
    }
    const GameDateValue next = SeasonCalendar::addDays(today, 1);
    for (Match& match : game.getCalendar().getMatchesForDateMutable(next))
    {
      if (match.isPlayed() || match.getHomeTeamId() == managed ||
          match.getAwayTeamId() == managed)
        continue;
      const auto home_goals = static_cast<std::uint8_t>(
          (match.getHomeTeamId() * 7U + next.day) % 4U);
      const auto away_goals = static_cast<std::uint8_t>(
          (match.getAwayTeamId() * 3U + next.month) % 3U);
      if (match.isKnockout() && home_goals == away_goals)
        match.setKnockoutResult(
            home_goals, away_goals, true,
            std::make_pair(std::uint8_t{5}, std::uint8_t{4}));
      else
        match.setPlayedResult(home_goals, away_goals);
    }
    // Keeps a losing manager in the job until the season verdict.
    if (protect_job) game.getWorld().adjustBoardConfidence(100.0f);
    controller.advanceDay();
  }
}

int countTitle(const Inbox& inbox, std::string_view key)
{
  return static_cast<int>(
      std::ranges::count(inbox.getMessages(), key, &InboxMessage::title_key));
}

std::optional<std::uint32_t> firstId(const Inbox& inbox, const std::string& key,
                                     const GameDateValue& date)
{
  for (const InboxMessage& message : inbox.getMessages())
    if (message.title_key == key && message.date == date) return message.id;
  return std::nullopt;
}

SeasonVerdictInputs midTable()
{
  SeasonVerdictInputs inputs;
  inputs.objective = BoardObjective::MidTable;
  inputs.target_position = 15;
  inputs.position = 12;
  inputs.league_size = 20;
  inputs.confidence = 55.0f;
  return inputs;
}
}  // namespace

// ---- The board's verdict (pure rules)
// ------------------------------------------

TEST(SeasonVerdict, TitleAndPromotionDelightTheBoard)
{
  SeasonVerdictInputs champion = midTable();
  champion.position = 1;
  champion.champion = true;
  const SeasonVerdictResult title = SeasonReviewModel::judge(champion);
  EXPECT_EQ(title.verdict, SeasonVerdict::Delighted);
  EXPECT_EQ(title.league, ObjectiveGrade::Exceeded);
  EXPECT_GT(title.confidence, champion.confidence);

  SeasonVerdictInputs promoted = midTable();
  promoted.position = 2;
  promoted.promoted = true;
  EXPECT_EQ(SeasonReviewModel::judge(promoted).verdict,
            SeasonVerdict::Delighted);

  const SeasonVerdictResult fine = SeasonReviewModel::judge(midTable());
  EXPECT_TRUE(fine.verdict == SeasonVerdict::Satisfied ||
              fine.verdict == SeasonVerdict::Pleased);
}

TEST(SeasonVerdict, AMissedTargetWarnsThenSacks)
{
  SeasonVerdictInputs missed = midTable();
  missed.position = 18;
  missed.confidence = 60.0f;
  const SeasonVerdictResult warning = SeasonReviewModel::judge(missed);
  EXPECT_EQ(warning.verdict, SeasonVerdict::Warned);
  EXPECT_LT(warning.confidence, missed.confidence);
  EXPECT_NE(warning.league, ObjectiveGrade::Met);

  // The same season after a warning costs the job.
  missed.warned_last_season = true;
  EXPECT_EQ(SeasonReviewModel::judge(missed).verdict, SeasonVerdict::Sacked);

  // So does a failed season with the board already losing faith.
  SeasonVerdictInputs low = midTable();
  low.position = 19;
  low.confidence = 30.0f;
  EXPECT_EQ(SeasonReviewModel::judge(low).verdict, SeasonVerdict::Sacked);
}

TEST(SeasonVerdict, RelegationSacksUnlessItWasTheFight)
{
  SeasonVerdictInputs dropped = midTable();
  dropped.objective = BoardObjective::TopHalf;
  dropped.target_position = 10;
  dropped.position = 18;
  dropped.relegated = true;
  dropped.confidence = 90.0f;
  EXPECT_EQ(SeasonReviewModel::judge(dropped).verdict, SeasonVerdict::Sacked);

  SeasonVerdictInputs fight = dropped;
  fight.objective = BoardObjective::AvoidRelegation;
  fight.target_position = 17;
  EXPECT_EQ(SeasonReviewModel::judge(fight).verdict, SeasonVerdict::Warned);
}

TEST(SeasonVerdict, LateAppointmentsAreNotJudged)
{
  SeasonVerdictInputs late = midTable();
  late.position = 20;
  late.relegated = true;
  late.objective = BoardObjective::TopHalf;
  late.league_matches = SeasonReviewModel::MIN_MATCHES_TO_JUDGE - 1;
  const SeasonVerdictResult result = SeasonReviewModel::judge(late);
  EXPECT_EQ(result.verdict, SeasonVerdict::Satisfied);
  EXPECT_GE(result.confidence, late.confidence - 15.0f) << "half the swing";
}

TEST(SeasonVerdict, CupFinancesAndYouthMoveTheVerdict)
{
  const float base = SeasonReviewModel::judge(midTable()).confidence;
  SeasonVerdictInputs cup = midTable();
  cup.cup = ObjectiveGrade::Exceeded;
  EXPECT_GT(SeasonReviewModel::judge(cup).confidence, base);
  SeasonVerdictInputs youth = midTable();
  youth.youth = ObjectiveGrade::Failed;
  EXPECT_LT(SeasonReviewModel::judge(youth).confidence, base);

  // Broke books fail a season that met its league target.
  SeasonVerdictInputs broke = midTable();
  broke.finances = ObjectiveGrade::Failed;
  const SeasonVerdictResult result = SeasonReviewModel::judge(broke);
  EXPECT_EQ(result.finances, ObjectiveGrade::Failed);
  EXPECT_GE(static_cast<int>(result.verdict),
            static_cast<int>(SeasonVerdict::Warned));
}

TEST(SeasonVerdict, TargetsTheBoardNeverSetAreNotJudged)
{
  // A board upgraded from an older save has only its league objective this
  // season: broke books, an early cup exit and no young regulars count for
  // nothing, the league finish alone decides.
  SeasonVerdictInputs upgraded = midTable();
  upgraded.cup = ObjectiveGrade::Failed;
  upgraded.finances = ObjectiveGrade::Failed;
  upgraded.youth = ObjectiveGrade::Failed;
  upgraded.targets_set = false;
  const SeasonVerdictResult league_only = SeasonReviewModel::judge(upgraded);
  const SeasonVerdictResult fine = SeasonReviewModel::judge(midTable());
  EXPECT_EQ(league_only.cup, ObjectiveGrade::Met);
  EXPECT_EQ(league_only.finances, ObjectiveGrade::Met);
  EXPECT_EQ(league_only.youth, ObjectiveGrade::Met);
  EXPECT_EQ(league_only.verdict, fine.verdict);
  EXPECT_FLOAT_EQ(league_only.confidence, fine.confidence);
  EXPECT_LT(static_cast<int>(league_only.verdict),
            static_cast<int>(SeasonVerdict::Warned));

  // A missed league target is still judged.
  upgraded.position = 19;
  EXPECT_GE(static_cast<int>(SeasonReviewModel::judge(upgraded).verdict),
            static_cast<int>(SeasonVerdict::Warned));

  // The same season with the targets set warns the manager.
  upgraded.position = 12;
  upgraded.targets_set = true;
  EXPECT_GE(static_cast<int>(SeasonReviewModel::judge(upgraded).verdict),
            static_cast<int>(SeasonVerdict::Warned));
}

// ---- Board targets
// ----------------------------------------------------------------

TEST(BoardTargets, StatureSetsTheTargets)
{
  EXPECT_EQ(BoardModel::cupObjectiveFor(BoardObjective::WinLeague, 1),
            CupObjective::SemiFinal);
  EXPECT_EQ(BoardModel::cupObjectiveFor(BoardObjective::TopFour, 1),
            CupObjective::QuarterFinal);
  EXPECT_EQ(BoardModel::cupObjectiveFor(BoardObjective::WinLeague, 2),
            CupObjective::None);
  EXPECT_EQ(BoardModel::financeObjectiveFor(0.8f, 1'000'000),
            FinanceObjective::BreakEven);
  EXPECT_EQ(BoardModel::financeObjectiveFor(0.2f, -1),
            FinanceObjective::BreakEven);
  EXPECT_EQ(BoardModel::financeObjectiveFor(0.2f, 1'000'000),
            FinanceObjective::WithinWageBudget);
  EXPECT_EQ(BoardModel::youthTargetFor(0.4f), 0);
  EXPECT_EQ(BoardModel::youthTargetFor(0.65f), 2);
  EXPECT_EQ(BoardModel::youthTargetFor(0.9f), 3);
}

TEST(BoardTargets, GradesFollowTheRun)
{
  using BoardModel::gradeCup;
  // Semi-final target: rounds left after the furthest round played.
  EXPECT_EQ(gradeCup(CupObjective::SemiFinal, 1, false, false),
            ObjectiveGrade::Met);
  EXPECT_EQ(gradeCup(CupObjective::SemiFinal, 0, false, false),
            ObjectiveGrade::Exceeded);
  EXPECT_EQ(gradeCup(CupObjective::SemiFinal, 2, false, false),
            ObjectiveGrade::Missed);
  EXPECT_EQ(gradeCup(CupObjective::SemiFinal, 4, false, false),
            ObjectiveGrade::Failed);
  EXPECT_EQ(gradeCup(CupObjective::SemiFinal, 4, false, true),
            ObjectiveGrade::Met)
      << "still in the cup is on track";
  EXPECT_EQ(gradeCup(CupObjective::None, 3, false, false), ObjectiveGrade::Met);
  EXPECT_EQ(gradeCup(CupObjective::QuarterFinal, 0, true, false),
            ObjectiveGrade::Exceeded);

  using BoardModel::gradeFinances;
  EXPECT_EQ(gradeFinances(FinanceObjective::WithinWageBudget, 5, 10, false),
            ObjectiveGrade::Met);
  EXPECT_EQ(gradeFinances(FinanceObjective::WithinWageBudget, 5, 10, true),
            ObjectiveGrade::Missed);
  EXPECT_EQ(gradeFinances(FinanceObjective::WithinWageBudget, -5, 10, false),
            ObjectiveGrade::Failed);
  EXPECT_EQ(
      gradeFinances(FinanceObjective::BreakEven, 9'000'000, 10'000'000, false),
      ObjectiveGrade::Missed);
  EXPECT_EQ(
      gradeFinances(FinanceObjective::BreakEven, 12'500'000, 10'000'000, false),
      ObjectiveGrade::Exceeded);
  EXPECT_EQ(gradeFinances(FinanceObjective::BreakEven, -1'000, -5'000, false),
            ObjectiveGrade::Met)
      << "a club in the red that reduced its debt broke even";

  EXPECT_EQ(BoardModel::gradeYouth(2, 0), ObjectiveGrade::Failed);
  EXPECT_EQ(BoardModel::gradeYouth(2, 1), ObjectiveGrade::Missed);
  EXPECT_EQ(BoardModel::gradeYouth(2, 2), ObjectiveGrade::Met);
  EXPECT_EQ(BoardModel::gradeYouth(2, 4), ObjectiveGrade::Exceeded);
  EXPECT_EQ(BoardModel::gradeYouth(0, 0), ObjectiveGrade::Met);
  EXPECT_EQ(BoardModel::gradeLeague(10, 10, 20), ObjectiveGrade::Met);
  EXPECT_EQ(BoardModel::gradeLeague(10, 4, 20), ObjectiveGrade::Exceeded);
  EXPECT_EQ(BoardModel::gradeLeague(10, 13, 20), ObjectiveGrade::Missed);
  EXPECT_EQ(BoardModel::gradeLeague(10, 20, 20), ObjectiveGrade::Failed);
}

TEST(BoardTargets, ConfidenceAdjustmentsAreClamped)
{
  BoardState state;
  state.confidence = 50.0f;
  BoardModel::adjustConfidence(state, 70.0f);
  EXPECT_FLOAT_EQ(state.confidence, 100.0f);
  BoardModel::adjustConfidence(state, -250.0f);
  EXPECT_FLOAT_EQ(state.confidence, 0.0f);
}

TEST(BoardTargets, SummerReviewsWatchOnlyTheMoney)
{
  BoardState state;
  state.target_position = 10;
  state.league_matches = 38;
  state.confidence = 5.0f;
  state.low_reviews = WorldTuning::Board::DISMISSAL_REVIEWS;
  // Bottom of the table, in the red: in summer that warns, never dismisses.
  EXPECT_EQ(BoardModel::monthlyReview(state, 20, 20, true, true, false),
            BoardReviewOutcome::Warning);
  EXPECT_FALSE(state.dismissed);
  // A healthy summer lets confidence recover; the table does not count.
  const float before = state.confidence;
  BoardModel::monthlyReview(state, 20, 20, false, false, false);
  EXPECT_GT(state.confidence, before);
}

// ---- Archive persistence
// ------------------------------------------------------------

TEST(SeasonArchive, TablesAndReviewsSurviveASave)
{
  const SlotCleanup slot{uniqueSlot(1)};
  TeamID managed = 0;
  auto controller = careerAt(slot.slot, managed);
  ASSERT_NE(managed, 0);
  const LeagueID league = controller->getTeamById(managed)->get().getLeagueId();
  SeasonArchive& archive = controller->getGame()->getSeasonArchive();
  std::vector<StandingRow> rows(3);
  for (std::size_t index = 0; index < rows.size(); ++index)
  {
    rows[index].team_id = static_cast<TeamID>(managed + index);
    rows[index].position = static_cast<std::uint16_t>(index + 1);
    rows[index].played = 38;
    rows[index].points = static_cast<std::uint16_t>(80 - 10 * index);
  }
  archive.recordTables(1, 2025, {{league, rows}});
  SeasonReview review;
  review.season = 1;
  review.start_year = 2025;
  review.team_id = managed;
  review.league_id = league;
  review.position = 1;
  review.champion = true;
  review.cup_objective = CupObjective::SemiFinal;
  review.youth_target = 2;
  review.result.verdict = SeasonVerdict::Delighted;
  review.result.cup = ObjectiveGrade::Missed;
  review.next_objective = BoardObjective::WinLeague;
  review.next_finances = FinanceObjective::BreakEven;
  archive.recordReview(review);
  ASSERT_TRUE(controller->saveGame());

  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  ASSERT_EQ(reloaded.getArchivedSeasons(league), std::vector<std::uint16_t>{1});
  const auto* table = reloaded.getArchivedTable(1, league);
  ASSERT_NE(table, nullptr);
  ASSERT_EQ(table->size(), 3u);
  EXPECT_EQ(table->front().team_id, managed);
  EXPECT_EQ(table->back().points, 60);
  EXPECT_EQ(reloaded.getSeasonStartYear(1), 2025);
  const auto placing = reloaded.getArchivedPlacing(1, managed);
  ASSERT_TRUE(placing.has_value());
  EXPECT_EQ(placing->first, league);
  EXPECT_EQ(placing->second, 1);
  const SeasonReview* loaded = reloaded.getUnseenSeasonReview();
  ASSERT_NE(loaded, nullptr);
  EXPECT_TRUE(loaded->champion);
  EXPECT_EQ(loaded->cup_objective, CupObjective::SemiFinal);
  EXPECT_EQ(loaded->youth_target, 2);
  EXPECT_EQ(loaded->result.verdict, SeasonVerdict::Delighted);
  EXPECT_EQ(loaded->result.cup, ObjectiveGrade::Missed);
  EXPECT_EQ(loaded->next_objective, BoardObjective::WinLeague);
  EXPECT_EQ(loaded->next_finances, FinanceObjective::BreakEven);

  // Shown once: the flag is saved too.
  reloaded.markSeasonReviewSeen(1);
  EXPECT_EQ(reloaded.getUnseenSeasonReview(), nullptr);
  ASSERT_TRUE(reloaded.saveGame());
  GameController again;
  ASSERT_TRUE(again.loadGame(slot.slot));
  EXPECT_EQ(again.getUnseenSeasonReview(), nullptr);
  EXPECT_NE(again.getGame()->getSeasonArchive().review(1), nullptr);
}

// ---- Board targets in a career
// ------------------------------------------------------

TEST(SeasonEndFlow, TheBoardSetsEveryTargetWhenTheJobStarts)
{
  const SlotCleanup slot{uniqueSlot(2)};
  TeamID managed = 0;
  auto controller = careerAt(slot.slot, managed);
  const auto targets = controller->getBoardTargets();
  ASSERT_TRUE(targets.has_value());
  const BoardState& board = controller->getBoardState();
  EXPECT_EQ(targets->league, board.objective);
  EXPECT_EQ(targets->cup, board.cup_objective);
  EXPECT_NE(board.cup_objective, CupObjective::None)
      << "the favourite of a top division is expected to go far in the cup";
  EXPECT_EQ(targets->start_balance, board.start_balance);
  EXPECT_TRUE(board.targets_set) << "new careers are judged on every target";
  EXPECT_EQ(board.start_balance,
            controller->getTeamById(managed)->get().getFinances().getBalance());
  // The welcome names every target.
  bool welcomed = false;
  for (const InboxMessage& message :
       controller->getGame()->getWorld().getInbox().getMessages())
    if (message.title_key == "INBOX_BOARD_WELCOME_TITLE")
    {
      ASSERT_GE(message.args.size(), 10u);
      EXPECT_EQ(message.args[7], std::string("@") + BoardModel::cupObjectiveKey(
                                                        board.cup_objective));
      welcomed = true;
    }
  EXPECT_TRUE(welcomed);
}

// ---- A whole season
// -----------------------------------------------------------------

/**
 * A title season: the news comes after promotion and relegation are
 * applied and in order, the summary waits to be shown, the table is
 * archived, confidence carries into the new season, an expired contract
 * leaves only its own gap in the line-up, and the off-season Continue
 * jumps from event to event into July.
 */
TEST(SeasonEndFlow, ATitleSeasonEndsWithItsNewsSummaryAndArchive)
{
  const SlotCleanup slot{uniqueSlot(3)};
  TeamID managed = 0;
  auto controller = careerAt(slot.slot, managed);
  Game& game = *controller->getGame();
  const LeagueID league = controller->getTeamById(managed)->get().getLeagueId();
  const int start_year = game.getCurrentDate().month >= 7
                             ? game.getCurrentDate().year
                             : game.getCurrentDate().year - 1;

  // After the club's last fixture, Continue jumps instead of crawling.
  const GameDateValue season_end(static_cast<std::uint16_t>(start_year + 1), 7,
                                 1);
  GameDateValue last_fixture = game.getCurrentDate();
  for (const Match& match : controller->getTeamFixtures(managed))
    if (match.getDate() < season_end && last_fixture < match.getDate())
      last_fixture = match.getDate();
  playUntil(*controller, managed, true,
            SeasonCalendar::addDays(last_fixture, 1));
  ASSERT_EQ(game.getManagedTeamId(), managed);
  const GameDateValue last_day = SeasonCalendar::addDays(season_end, -1);
  ASSERT_LT(game.getCurrentDate(), last_day);
  int calls = 0;
  const GameDateValue jump_start = game.getCurrentDate();
  while (game.getCurrentDate() < last_day && calls < 40)
  {
    // Capped at 30 June here so the line-up can be compared across 1 July.
    const int days = controller->advanceToNextEvent(
        dayOrdinal(last_day) - dayOrdinal(game.getCurrentDate()));
    ++calls;
    ASSERT_GT(days, 0);
    ASSERT_NE(controller->getLastContinueStop(), ContinueStop::None);
    if (game.getCurrentDate() < last_day)
      EXPECT_NE(controller->getLastContinueStop(), ContinueStop::DayLimit);
  }
  ASSERT_EQ(game.getCurrentDate(), last_day);
  const int jumped = dayOrdinal(last_day) - dayOrdinal(jump_start);
  EXPECT_LT(calls, jumped) << "one click per day in the off-season";

  // One starter's contract runs out tonight.
  Team& club = controller->getGameData()->getTeam(managed)->get();
  const std::vector<const Player*> starters = club.getLineup().starters();
  ASSERT_GE(starters.size(), 11u);
  const PlayerID leaving = starters.back()->getId();
  std::vector<PlayerID> staying;
  for (const Player* player : starters)
    if (player->getId() != leaving) staying.push_back(player->getId());
  controller->getGameData()->getPlayers().at(leaving).setContractYears(1);

  // The last Continue of the season stops on the new one.
  EXPECT_EQ(controller->advanceToNextEvent(), 1);
  ASSERT_EQ(game.getCurrentDate(), season_end);
  EXPECT_EQ(controller->getLastContinueStop(), ContinueStop::NewSeason);

  // The season news, in order, after the clubs moved.
  const Inbox& inbox = game.getWorld().getInbox();
  const auto title = firstId(inbox, "INBOX_SEASON_CHAMPIONS_TITLE", season_end);
  const auto review_message =
      firstId(inbox, "INBOX_SEASON_REVIEW_TITLE", season_end);
  ASSERT_TRUE(title.has_value());
  ASSERT_TRUE(review_message.has_value());
  EXPECT_LT(*title, *review_message);
  EXPECT_EQ(countTitle(inbox, "INBOX_SEASON_RELEGATED_TITLE"), 0);

  // The summary waits to be shown, with next season's objectives.
  const SeasonReview* review = controller->getUnseenSeasonReview();
  ASSERT_NE(review, nullptr);
  EXPECT_EQ(review->season, 1);
  EXPECT_EQ(review->team_id, managed);
  EXPECT_TRUE(review->champion);
  EXPECT_EQ(review->position, 1);
  EXPECT_EQ(review->lost, 0);
  EXPECT_EQ(review->result.league, ObjectiveGrade::Exceeded);
  EXPECT_TRUE(review->result.verdict == SeasonVerdict::Delighted ||
              review->result.verdict == SeasonVerdict::Pleased);
  ASSERT_TRUE(review->next_objective.has_value());
  EXPECT_EQ(*review->next_objective, controller->getBoardState().objective);
  // Confidence carries over: the verdict's figure, not a reset.
  EXPECT_NEAR(controller->getBoardState().confidence, review->result.confidence,
              0.01f);
  EXPECT_GT(controller->getBoardState().confidence, 70.0f);

  // The final table is archived, the club on top.
  const auto* table = controller->getArchivedTable(1, league);
  ASSERT_NE(table, nullptr);
  ASSERT_FALSE(table->empty());
  EXPECT_EQ(table->front().team_id, managed);
  EXPECT_EQ(controller->getArchivedSeasons(league),
            std::vector<std::uint16_t>{1});

  // The expired contract left one gap, filled; everybody else still starts.
  EXPECT_FALSE(club.getLineup().isStarter(leaving));
  // (The assistant still covers anyone injured for the next friendly.)
  for (const PlayerID player_id : staying)
    if (std::ranges::contains(club.getPlayerIDs(), player_id) &&
        controller->getGameData()->getPlayer(player_id)->get().isAvailable())
      EXPECT_TRUE(club.getLineup().isStarter(player_id)) << player_id;
  EXPECT_GE(club.getLineup().starters().size(), 11u);
  EXPECT_EQ(countTitle(inbox, "INBOX_LINEUP_GAPS_TITLE"), 1);
}

/**
 * A favourite that goes down is sacked at the season end, once: the holiday
 * that spans the last day of the season stops there with one sacking
 * message, and the off-season Continue stops on the lost job.
 */
TEST(SeasonEndFlow, RelegationEndsTheJobOnceAndStopsTheHoliday)
{
  const SlotCleanup slot{uniqueSlot(4)};
  TeamID managed = 0;
  auto controller = careerAt(slot.slot, managed);
  Game& game = *controller->getGame();
  ASSERT_NE(controller->getBoardState().objective,
            BoardObjective::AvoidRelegation);
  const int start_year = game.getCurrentDate().month >= 7
                             ? game.getCurrentDate().year
                             : game.getCurrentDate().year - 1;
  const GameDateValue season_end(static_cast<std::uint16_t>(start_year + 1), 7,
                                 1);
  playUntil(*controller, managed, false,
            SeasonCalendar::addDays(season_end, -6), true);
  ASSERT_EQ(game.getManagedTeamId(), managed) << "sacked before the verdict";
  ASSERT_TRUE(controller->saveGame());

  // Only the lost job may call the manager back.
  HolidayPlan plan;
  plan.mode = HolidayMode::UntilDate;
  plan.until = SeasonCalendar::addDays(season_end, 20);
  plan.preferences.stop_big_bid = false;
  plan.preferences.stop_sacking_warning = false;
  plan.preferences.stop_injury_crisis = false;
  plan.preferences.stop_key_injury = false;
  controller->goOnHoliday(plan);
  EXPECT_EQ(controller->getHolidaySummary().reason, HolidayStop::Dismissed);
  EXPECT_EQ(game.getCurrentDate(), season_end);
  EXPECT_TRUE(controller->isUnemployed());
  const Inbox& inbox = game.getWorld().getInbox();
  EXPECT_EQ(countTitle(inbox, "INBOX_MANAGER_SACKED_TITLE") +
                countTitle(inbox, "INBOX_MANAGER_EXPIRED_TITLE"),
            1);
  EXPECT_EQ(countTitle(inbox, "INBOX_BOARD_DISMISSED_TITLE"), 0);
  EXPECT_EQ(countTitle(inbox, "INBOX_SEASON_RELEGATED_TITLE"), 1);
  const auto relegated =
      firstId(inbox, "INBOX_SEASON_RELEGATED_TITLE", season_end);
  const auto verdict = firstId(inbox, "INBOX_SEASON_REVIEW_TITLE", season_end);
  ASSERT_TRUE(relegated && verdict);
  EXPECT_LT(*relegated, *verdict);
  const SeasonReview* review = controller->getUnseenSeasonReview();
  ASSERT_NE(review, nullptr);
  EXPECT_TRUE(review->relegated);
  EXPECT_EQ(review->result.verdict, SeasonVerdict::Sacked);
  EXPECT_FALSE(review->next_objective.has_value());

  // The off-season Continue from the same day ends on the lost job.
  GameController again;
  ASSERT_TRUE(again.loadGame(slot.slot));
  for (int calls = 0; calls < 20 && again.getCurrentDate() < season_end;
       ++calls)
    ASSERT_GT(again.advanceToNextEvent(), 0);
  EXPECT_EQ(again.getCurrentDate(), season_end);
  EXPECT_EQ(again.getLastContinueStop(), ContinueStop::LostJob);
}

// ---- Stop
// ----------------------------------------------------------------------------

TEST(ContinueStop, StopEndsAHolidayAfterTheCurrentDay)
{
  const SlotCleanup slot{uniqueSlot(5)};
  TeamID managed = 0;
  auto controller = careerAt(slot.slot, managed);
  // A request left over from an earlier run does not cut the next one short.
  controller->requestContinueStop();
  HolidayPlan plan;
  plan.mode = HolidayMode::UntilDate;
  plan.until = SeasonCalendar::addDays(controller->getCurrentDate(), 60);
  plan.preferences.stop_big_bid = false;
  plan.preferences.stop_injury_crisis = false;
  plan.preferences.stop_key_injury = false;
  const GameDateValue start = controller->getCurrentDate();
  std::jthread worker;
  auto run = launchBackgroundTask(
      worker, [&] { return controller->goOnHoliday(plan); });
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (controller->getContinueProgress().days_done < 1 &&
         std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  controller->requestContinueStop();
  const int days = run.get();
  EXPECT_GE(days, 1);
  EXPECT_LT(days, 60);
  EXPECT_EQ(controller->getHolidaySummary().reason, HolidayStop::Interrupted);
  EXPECT_EQ(dayOrdinal(controller->getCurrentDate()) - dayOrdinal(start), days)
      << "the day in flight is completed, then the holiday stops";
}

TEST(ContinueStop, EveryReasonAndVerdictHasText)
{
  ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::EN));
  const auto translated = [](const char* key)
  { return std::string(LOC(key)) != key; };
  for (const ContinueStop stop :
       {ContinueStop::Fixture, ContinueStop::Message, ContinueStop::Decision,
        ContinueStop::WindowOpened, ContinueStop::NewSeason,
        ContinueStop::LostJob, ContinueStop::Requested, ContinueStop::DayLimit})
    EXPECT_TRUE(translated(GameController::continueStopKey(stop)))
        << GameController::continueStopKey(stop);
  for (const SeasonVerdict verdict :
       {SeasonVerdict::Delighted, SeasonVerdict::Pleased,
        SeasonVerdict::Satisfied, SeasonVerdict::Warned, SeasonVerdict::Sacked})
    EXPECT_TRUE(translated(SeasonReviewModel::verdictKey(verdict)));
  for (const CupObjective cup :
       {CupObjective::None, CupObjective::QuarterFinal, CupObjective::SemiFinal,
        CupObjective::Final, CupObjective::Win})
    EXPECT_TRUE(translated(BoardModel::cupObjectiveKey(cup)));
  EXPECT_TRUE(translated(Holiday::stopKey(HolidayStop::Interrupted)));
}

TEST(ContinueStop, StopEndsAContinueToTheNextFixture)
{
  const SlotCleanup slot{uniqueSlot(6)};
  TeamID managed = 0;
  auto controller = careerAt(slot.slot, managed);
  // Far enough from the next fixture that the run takes several days.
  GameDateValue next = controller->getCurrentDate();
  for (const Match& match : controller->getTeamFixtures(managed))
    if (!match.isPlayed() && controller->getCurrentDate() < match.getDate())
    {
      next = match.getDate();
      break;
    }
  if (dayOrdinal(next) - dayOrdinal(controller->getCurrentDate()) < 3)
    GTEST_SKIP() << "the first fixture is too close to stop on the way";
  std::jthread worker;
  auto run = launchBackgroundTask(
      worker, [&] { return controller->advanceToNextManagedFixture(); });
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (controller->getContinueProgress().days_done < 1 &&
         std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  controller->requestContinueStop();
  run.get();
  EXPECT_LT(controller->getCurrentDate(), next);
  EXPECT_EQ(controller->getLastContinueStop(), ContinueStop::Requested);
}
