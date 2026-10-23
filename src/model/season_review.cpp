// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/season_review.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "database/sqlite_rows.h"
#include "model/inbox.h"
#include "model/world_tuning.h"

namespace
{
// Weights of the objectives next to the league finish. [P]
constexpr float CUP_WEIGHT = 0.4f;
constexpr float FINANCE_WEIGHT = 0.6f;
constexpr float YOUTH_WEIGHT = 0.4f;
// Confidence points per unit of season score, and the bounds. [P]
constexpr float CONFIDENCE_PER_POINT = 10.0f;
constexpr float MAX_LOSS = 30.0f;
constexpr float MAX_GAIN = 20.0f;
// Season scores of a delighted and a pleased board. [P]
constexpr float DELIGHTED_SCORE = 1.5f;
constexpr float PLEASED_SCORE = 0.5f;
float gradeScore(ObjectiveGrade grade)
{
  using enum ObjectiveGrade;
  switch (grade)
  {
    case Exceeded:
      return 1.0f;
    case Met:
      return 0.0f;
    case Missed:
      return -1.0f;
    case Failed:
      break;
  }
  return -2.5f;
}

ObjectiveGrade gradeOf(float score)
{
  using enum ObjectiveGrade;
  if (score >= 1.0f) return Exceeded;
  if (score >= 0.0f) return Met;
  if (score >= -1.0f) return Missed;
  return Failed;
}

InboxMessage message(const GameDateValue& date, InboxCategory category,
                     std::string title, std::string body,
                     std::vector<std::string> args, TeamID team_id)
{
  InboxMessage result;
  result.date = date;
  result.category = category;
  result.title_key = std::move(title);
  result.body_key = std::move(body);
  result.args = std::move(args);
  result.team_id = team_id;
  return result;
}
}  // namespace

namespace SeasonReviewModel
{
SeasonVerdictResult judge(const SeasonVerdictInputs& inputs)
{
  using enum SeasonVerdict;
  SeasonVerdictResult result;

  // League: places against the target, a quarter of the table per unit.
  const float quarter =
      std::max(1.0f, static_cast<float>(inputs.league_size) / 4.0f);
  float league = std::clamp(
      static_cast<float>(inputs.target_position - inputs.position) / quarter,
      -2.0f, 2.0f);
  if (inputs.champion) league = 2.0f;
  if (inputs.promoted) league = std::max(league, 1.5f);
  if (inputs.relegated) league = -2.0f;
  result.league = gradeOf(league);
  // Targets the board never set are neither met nor failed.
  if (inputs.targets_set)
  {
    result.cup = inputs.cup;
    result.finances = inputs.finances;
    result.youth = inputs.youth;
  }

  const float score = league + CUP_WEIGHT * gradeScore(result.cup) +
                      FINANCE_WEIGHT * gradeScore(result.finances) +
                      YOUTH_WEIGHT * gradeScore(result.youth);
  float delta = std::clamp(CONFIDENCE_PER_POINT * score, -MAX_LOSS, MAX_GAIN);

  // A manager appointed late is given time: half the swing, no sanction.
  const bool judged = inputs.league_matches >= MIN_MATCHES_TO_JUDGE;
  if (!judged) delta *= 0.5f;
  result.confidence = std::clamp(inputs.confidence + delta, 0.0f, 100.0f);

  if (!judged)
  {
    result.verdict = score >= PLEASED_SCORE ? Pleased : Satisfied;
    return result;
  }

  const bool failed =
      league < 0.0f ||
      (result.finances == ObjectiveGrade::Failed && league < 1.0f);
  if (const bool unexpected_drop =
          inputs.relegated &&
          inputs.objective != BoardObjective::AvoidRelegation;
      failed && (result.confidence < SACK_CONFIDENCE || unexpected_drop ||
                 (inputs.warned_last_season && league < 0.0f)))
    result.verdict = Sacked;
  else if (failed || result.confidence < WorldTuning::Board::WARNING_THRESHOLD)
    result.verdict = Warned;
  else if (score >= DELIGHTED_SCORE)
    result.verdict = Delighted;
  else if (score >= PLEASED_SCORE)
    result.verdict = Pleased;
  else
    result.verdict = Satisfied;
  return result;
}

const char* verdictKey(SeasonVerdict verdict)
{
  using enum SeasonVerdict;
  switch (verdict)
  {
    case Delighted:
      return "SEASON_VERDICT_DELIGHTED";
    case Pleased:
      return "SEASON_VERDICT_PLEASED";
    case Satisfied:
      return "SEASON_VERDICT_SATISFIED";
    case Warned:
      return "SEASON_VERDICT_WARNED";
    case Sacked:
      break;
  }
  return "SEASON_VERDICT_SACKED";
}

const char* gradeKey(ObjectiveGrade grade)
{
  using enum ObjectiveGrade;
  switch (grade)
  {
    case Exceeded:
      return "SEASON_GRADE_EXCEEDED";
    case Met:
      return "SEASON_GRADE_MET";
    case Missed:
      return "SEASON_GRADE_MISSED";
    case Failed:
      break;
  }
  return "SEASON_GRADE_FAILED";
}

void postEvents(Inbox& inbox, const GameDateValue& date,
                const SeasonReview& review, const EventNames& names)
{
  using enum SeasonVerdict;
  const TeamID club = review.team_id;
  const std::string position = std::to_string(review.position);
  if (review.champion)
    inbox.add(message(date, InboxCategory::General,
                      "INBOX_SEASON_CHAMPIONS_TITLE",
                      "INBOX_SEASON_CHAMPIONS_BODY",
                      {names.club, names.league, std::to_string(review.points),
                       std::to_string(review.played)},
                      club));
  if (review.promoted)
    inbox.add(
        message(date, InboxCategory::General, "INBOX_SEASON_PROMOTED_TITLE",
                "INBOX_SEASON_PROMOTED_BODY",
                {names.club, position, names.league, names.next_league}, club));
  else if (review.relegated)
    inbox.add(
        message(date, InboxCategory::General, "INBOX_SEASON_RELEGATED_TITLE",
                "INBOX_SEASON_RELEGATED_BODY",
                {names.club, position, names.league, names.next_league}, club));
  else if (names.relegation_places > 0 &&
           (review.objective == BoardObjective::AvoidRelegation ||
            review.position + names.relegation_places + SURVIVAL_MARGIN >
                review.league_size))
    inbox.add(message(date, InboxCategory::General,
                      "INBOX_SEASON_SURVIVED_TITLE",
                      "INBOX_SEASON_SURVIVED_BODY",
                      {names.club, position, names.league}, club));
  if (review.cup_won)
    inbox.add(message(date, InboxCategory::General,
                      "INBOX_SEASON_CUP_WON_TITLE", "INBOX_SEASON_CUP_WON_BODY",
                      {names.club, names.cup}, club));
  if (review.continental_id != 0 && !names.continental.empty())
    inbox.add(message(date, InboxCategory::General,
                      "INBOX_SEASON_CONTINENTAL_TITLE",
                      "INBOX_SEASON_CONTINENTAL_BODY",
                      {names.club, "@" + names.continental}, club));

  std::string body = "INBOX_SEASON_REVIEW_";
  switch (review.result.verdict)
  {
    case Delighted:
      body += "DELIGHTED_BODY";
      break;
    case Pleased:
      body += "PLEASED_BODY";
      break;
    case Satisfied:
      body += "SATISFIED_BODY";
      break;
    case Warned:
      body += "WARNED_BODY";
      break;
    case Sacked:
      body += "SACKED_BODY";
      break;
  }
  inbox.add(message(
      date, InboxCategory::Board, "INBOX_SEASON_REVIEW_TITLE", std::move(body),
      {position, std::to_string(review.target_position),
       formatMoney(review.prize_money),
       std::to_string(std::lround(review.result.confidence)),
       std::string("@") + BoardModel::objectiveKey(review.objective)},
      club));
}
}  // namespace SeasonReviewModel

// ---------------------------------------------------------------------------
// Archive
// ---------------------------------------------------------------------------

void SeasonArchive::recordTables(
    std::uint16_t season, std::uint16_t start_year,
    const std::map<LeagueID, std::vector<StandingRow>>& final_tables)
{
  for (const auto& [league_id, rows] : final_tables)
  {
    if (rows.empty()) continue;
    tables[{season, league_id}] = rows;
  }
  start_years[season] = start_year;
  unsaved_seasons.insert(season);
}

void SeasonArchive::recordReview(SeasonReview review)
{
  std::erase_if(season_reviews, [&review](const SeasonReview& existing)
                { return existing.season == review.season; });
  season_reviews.push_back(std::move(review));
  std::ranges::sort(season_reviews, {}, &SeasonReview::season);
  reviews_dirty = true;
}

std::vector<std::uint16_t> SeasonArchive::seasonsOf(LeagueID league_id) const
{
  std::vector<std::uint16_t> seasons;
  for (const auto& [key, rows] : tables)
    if (key.second == league_id) seasons.push_back(key.first);
  std::ranges::sort(seasons, std::greater<>{});
  return seasons;
}

const std::vector<StandingRow>* SeasonArchive::table(std::uint16_t season,
                                                     LeagueID league_id) const
{
  const auto found = tables.find({season, league_id});
  return found == tables.end() ? nullptr : &found->second;
}

bool SeasonArchive::hasSeason(std::uint16_t season) const
{
  return start_years.contains(season);
}

std::uint16_t SeasonArchive::startYear(std::uint16_t season) const
{
  const auto found = start_years.find(season);
  return found == start_years.end() ? 0 : found->second;
}

std::optional<std::pair<LeagueID, std::uint16_t>> SeasonArchive::placing(
    std::uint16_t season, TeamID team_id) const
{
  for (auto it = tables.lower_bound({season, 0});
       it != tables.end() && it->first.first == season; ++it)
    for (const StandingRow& row : it->second)
      if (row.team_id == team_id)
        return std::make_pair(it->first.second, row.position);
  return std::nullopt;
}

SeasonReview* SeasonArchive::review(std::uint16_t season)
{
  const auto found =
      std::ranges::find(season_reviews, season, &SeasonReview::season);
  return found == season_reviews.end() ? nullptr : std::to_address(found);
}

const SeasonReview* SeasonArchive::review(std::uint16_t season) const
{
  const auto found =
      std::ranges::find(season_reviews, season, &SeasonReview::season);
  return found == season_reviews.end() ? nullptr : std::to_address(found);
}

const SeasonReview* SeasonArchive::unseenReview() const
{
  if (season_reviews.empty() || season_reviews.back().seen) return nullptr;
  return &season_reviews.back();
}

void SeasonArchive::markSeen(std::uint16_t season)
{
  if (SeasonReview* found = review(season); found && !found->seen)
  {
    found->seen = true;
    reviews_dirty = true;
  }
}

void SeasonArchive::load(const DatabaseConnection& db)
{
  using namespace SqliteRows;
  tables.clear();
  start_years.clear();
  season_reviews.clear();
  unsaved_seasons.clear();
  reviews_dirty = false;
  forEach(
      db,
      "SELECT season, start_year, league_id, position, team_id, played, "
      "won, drawn, lost, goals_for, goals_against, points FROM "
      "SeasonTables ORDER BY season, league_id, position;",
      [this](sqlite3_stmt* stmt)
      {
        const auto season = column<std::uint16_t>(stmt, 0);
        start_years[season] = column<std::uint16_t>(stmt, 1);
        StandingRow row;
        row.position = column<std::uint16_t>(stmt, 3);
        row.team_id = column<TeamID>(stmt, 4);
        row.played = column<std::uint16_t>(stmt, 5);
        row.won = column<std::uint16_t>(stmt, 6);
        row.drawn = column<std::uint16_t>(stmt, 7);
        row.lost = column<std::uint16_t>(stmt, 8);
        row.goals_for = column<std::uint16_t>(stmt, 9);
        row.goals_against = column<std::uint16_t>(stmt, 10);
        row.goal_difference = row.goals_for - row.goals_against;
        row.points = column<std::uint16_t>(stmt, 11);
        tables[{season, column<LeagueID>(stmt, 2)}].push_back(std::move(row));
      });
  forEach(
      db,
      "SELECT season, start_year, team_id, league_id, next_league_id, "
      "position, league_size, target_position, objective, played, won, "
      "drawn, lost, goals_for, goals_against, points, champion, promoted, "
      "relegated, cup_won, continental_id, top_scorer, top_scorer_goals, "
      "prize_money, balance, confidence_before, verdict, grade_league, "
      "grade_cup, grade_finances, grade_youth, confidence_after, "
      "next_objective, next_target, seen, cup_objective, finance_objective, "
      "youth_target, young_regulars, next_cup, next_finances, next_youth "
      "FROM SeasonReviews ORDER BY season;",
      [this](sqlite3_stmt* stmt)
      {
        const auto grade = [stmt](int index)
        {
          return static_cast<ObjectiveGrade>(
              std::clamp(column<int>(stmt, index), 0, 3));
        };
        const auto objective = [stmt](int index)
        {
          return static_cast<BoardObjective>(
              std::clamp(column<int>(stmt, index), 0, 4));
        };
        SeasonReview review;
        review.season = column<std::uint16_t>(stmt, 0);
        review.start_year = column<std::uint16_t>(stmt, 1);
        review.team_id = column<TeamID>(stmt, 2);
        review.league_id = column<LeagueID>(stmt, 3);
        review.next_league_id = column<LeagueID>(stmt, 4);
        review.position = column<std::uint8_t>(stmt, 5);
        review.league_size = column<std::uint8_t>(stmt, 6);
        review.target_position = column<std::uint8_t>(stmt, 7);
        review.objective = objective(8);
        review.played = column<std::uint16_t>(stmt, 9);
        review.won = column<std::uint16_t>(stmt, 10);
        review.drawn = column<std::uint16_t>(stmt, 11);
        review.lost = column<std::uint16_t>(stmt, 12);
        review.goals_for = column<std::uint16_t>(stmt, 13);
        review.goals_against = column<std::uint16_t>(stmt, 14);
        review.points = column<std::uint16_t>(stmt, 15);
        review.champion = column<int>(stmt, 16) != 0;
        review.promoted = column<int>(stmt, 17) != 0;
        review.relegated = column<int>(stmt, 18) != 0;
        review.cup_won = column<int>(stmt, 19) != 0;
        review.continental_id = column<LeagueID>(stmt, 20);
        review.top_scorer = columnText(stmt, 21);
        review.top_scorer_goals = column<std::uint16_t>(stmt, 22);
        review.prize_money = column<std::int64_t>(stmt, 23);
        review.balance = column<std::int64_t>(stmt, 24);
        review.confidence_before = columnFloat(stmt, 25);
        review.result.verdict =
            static_cast<SeasonVerdict>(std::clamp(column<int>(stmt, 26), 0, 4));
        review.result.league = grade(27);
        review.result.cup = grade(28);
        review.result.finances = grade(29);
        review.result.youth = grade(30);
        review.result.confidence = columnFloat(stmt, 31);
        if (sqlite3_column_type(stmt, 32) != SQLITE_NULL)
          review.next_objective = objective(32);
        review.next_target = column<std::uint8_t>(stmt, 33);
        review.seen = column<int>(stmt, 34) != 0;
        const auto cup = [stmt](int index)
        {
          return static_cast<CupObjective>(
              std::clamp(column<int>(stmt, index), 0, 4));
        };
        const auto finances = [stmt](int index)
        {
          return static_cast<FinanceObjective>(
              std::clamp(column<int>(stmt, index), 0, 1));
        };
        review.cup_objective = cup(35);
        review.finance_objective = finances(36);
        review.youth_target = column<std::uint8_t>(stmt, 37);
        review.young_regulars = column<std::uint8_t>(stmt, 38);
        review.next_cup = cup(39);
        review.next_finances = finances(40);
        review.next_youth = column<std::uint8_t>(stmt, 41);
        season_reviews.push_back(std::move(review));
      });
}

void SeasonArchive::save(const DatabaseConnection& db) const
{
  using namespace SqliteRows;
  if (!unsaved_seasons.empty())
  {
    sqlite3_stmt* stmt = db.prepareStatement(
        "INSERT OR REPLACE INTO SeasonTables (season, start_year, league_id, "
        "position, team_id, played, won, drawn, lost, goals_for, "
        "goals_against, points) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    for (const std::uint16_t season : unsaved_seasons)
      for (auto it = tables.lower_bound({season, 0});
           it != tables.end() && it->first.first == season; ++it)
        for (const StandingRow& row : it->second)
        {
          sqlite3_bind_int(stmt, 1, season);
          sqlite3_bind_int(stmt, 2, startYear(season));
          sqlite3_bind_int(stmt, 3, it->first.second);
          sqlite3_bind_int(stmt, 4, row.position);
          sqlite3_bind_int(stmt, 5, row.team_id);
          sqlite3_bind_int(stmt, 6, row.played);
          sqlite3_bind_int(stmt, 7, row.won);
          sqlite3_bind_int(stmt, 8, row.drawn);
          sqlite3_bind_int(stmt, 9, row.lost);
          sqlite3_bind_int(stmt, 10, row.goals_for);
          sqlite3_bind_int(stmt, 11, row.goals_against);
          sqlite3_bind_int(stmt, 12, row.points);
          db.executeStep(stmt);
          sqlite3_reset(stmt);
          sqlite3_clear_bindings(stmt);
        }
    sqlite3_finalize(stmt);
  }
  if (!reviews_dirty) return;
  clearTable(db, "SeasonReviews");
  insertAll(
      db,
      "INSERT INTO SeasonReviews (season, start_year, team_id, league_id, "
      "next_league_id, position, league_size, target_position, objective, "
      "played, won, drawn, lost, goals_for, goals_against, points, champion, "
      "promoted, relegated, cup_won, continental_id, top_scorer, "
      "top_scorer_goals, prize_money, balance, confidence_before, verdict, "
      "grade_league, grade_cup, grade_finances, grade_youth, "
      "confidence_after, next_objective, next_target, seen, cup_objective, "
      "finance_objective, youth_target, young_regulars, next_cup, "
      "next_finances, next_youth) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
      "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
      "?, ?, ?, ?, ?, ?);",
      season_reviews,
      [](sqlite3_stmt* stmt, const SeasonReview& review)
      {
        const auto integer = [stmt](int index, std::int64_t value)
        { sqlite3_bind_int64(stmt, index, value); };
        integer(1, review.season);
        integer(2, review.start_year);
        integer(3, review.team_id);
        integer(4, review.league_id);
        integer(5, review.next_league_id);
        integer(6, review.position);
        integer(7, review.league_size);
        integer(8, review.target_position);
        integer(9, static_cast<int>(std::to_underlying(review.objective)));
        integer(10, review.played);
        integer(11, review.won);
        integer(12, review.drawn);
        integer(13, review.lost);
        integer(14, review.goals_for);
        integer(15, review.goals_against);
        integer(16, review.points);
        integer(17, review.champion ? 1 : 0);
        integer(18, review.promoted ? 1 : 0);
        integer(19, review.relegated ? 1 : 0);
        integer(20, review.cup_won ? 1 : 0);
        integer(21, review.continental_id);
        bindText(stmt, 22, review.top_scorer);
        integer(23, review.top_scorer_goals);
        integer(24, review.prize_money);
        integer(25, review.balance);
        sqlite3_bind_double(stmt, 26,
                            static_cast<double>(review.confidence_before));
        integer(27,
                static_cast<int>(std::to_underlying(review.result.verdict)));
        integer(28, static_cast<int>(std::to_underlying(review.result.league)));
        integer(29, static_cast<int>(std::to_underlying(review.result.cup)));
        integer(30,
                static_cast<int>(std::to_underlying(review.result.finances)));
        integer(31, static_cast<int>(std::to_underlying(review.result.youth)));
        sqlite3_bind_double(stmt, 32,
                            static_cast<double>(review.result.confidence));
        if (review.next_objective)
          integer(33,
                  static_cast<int>(std::to_underlying(*review.next_objective)));
        else
          sqlite3_bind_null(stmt, 33);
        integer(34, review.next_target);
        integer(35, review.seen ? 1 : 0);
        integer(36, static_cast<int>(std::to_underlying(review.cup_objective)));
        integer(37,
                static_cast<int>(std::to_underlying(review.finance_objective)));
        integer(38, review.youth_target);
        integer(39, review.young_regulars);
        integer(40, static_cast<int>(std::to_underlying(review.next_cup)));
        integer(41, static_cast<int>(std::to_underlying(review.next_finances)));
        integer(42, review.next_youth);
      });
}

void SeasonArchive::onSaved()
{
  unsaved_seasons.clear();
  reviews_dirty = false;
}
