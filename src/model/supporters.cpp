// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/supporters.h"

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <sstream>
#include <string>
#include <utility>

#include "database/database_connection.h"
#include "database/gamedata.h"
#include "global/global.h"
#include "model/board.h"
#include "model/calendar.h"
#include "model/club_economy.h"
#include "model/match.h"
#include "model/player.h"
#include "model/stories.h"
#include "model/team.h"
#include "model/transfer_market.h"
#include "model/world_rng.h"
#include "model/world_simulation.h"

namespace
{
// Index points per factor. [P] Results against expectation dominate fan
// sentiment; a derby, the price of a ticket and the departure of a star are
// the classic secondary grievances.
constexpr float RESULTS_PER_POINT = 3.5f; /*!< Per point above expectation. */
constexpr float RESULTS_CAP = 30.0f;
constexpr float DERBY_WIN = 8.0f;
constexpr float DERBY_OLDER_WEIGHT = 0.5f;
constexpr float DERBY_CAP = 12.0f;
constexpr float TICKET_ELASTICITY = 25.0f; /*!< Points per log price ratio. */
constexpr float TICKET_MIN = -12.0f;
constexpr float TICKET_MAX = 6.0f;
constexpr float STAR_SALE = 9.0f;
constexpr float STAR_SALE_CAP = 18.0f;
constexpr float STAR_SIGNING = 5.0f;
constexpr float STAR_SIGNING_CAP = 10.0f;
/** Reasons worth less than this are not mentioned. */
constexpr float REASON_MIN_POINTS = 1.0f;
/** Share of the gap to the facts closed each week. */
constexpr float WEEKLY_SETTLE = 0.5f;
constexpr int DAYS_PER_WEEK = 7;
constexpr int SEASON_START_MONTH = 7;

template <typename Read>
void forEachRow(const DatabaseConnection& db, const char* sql, Read read)
{
  sqlite3_stmt* stmt = db.prepareStatement(sql);
  while (sqlite3_step(stmt) == SQLITE_ROW) read(stmt);
  sqlite3_finalize(stmt);
}
}  // namespace

SupporterMood SupporterModel::evaluate(const SupporterFacts& facts)
{
  SupporterMood mood;
  const auto add = [&mood](SupporterFactor factor, float points)
  {
    if (std::abs(points) >= REASON_MIN_POINTS)
      mood.reasons.push_back({factor, points});
  };

  float results = 0.0f;
  for (const float delta : facts.result_deltas) results += delta;
  add(SupporterFactor::Results,
      std::clamp(results * RESULTS_PER_POINT, -RESULTS_CAP, RESULTS_CAP));

  float derby = 0.0f;
  for (std::size_t index = 0; index < facts.derby_points.size(); ++index)
  {
    const int points = facts.derby_points[index];
    float swing = 0.0f;
    if (points >= 3)
      swing = DERBY_WIN;
    else if (points == 0)
      swing = -DERBY_WIN;
    derby += index == 0 ? swing : swing * DERBY_OLDER_WEIGHT;
  }
  add(SupporterFactor::Derby, std::clamp(derby, -DERBY_CAP, DERBY_CAP));

  if (facts.ticket_ratio > 0.0f)
    add(SupporterFactor::TicketPrice,
        std::clamp(-TICKET_ELASTICITY * std::log(facts.ticket_ratio),
                   TICKET_MIN, TICKET_MAX));
  add(SupporterFactor::StarSale,
      -std::min(STAR_SALE_CAP,
                STAR_SALE * static_cast<float>(std::max(0, facts.star_sales))));
  add(SupporterFactor::StarSigning,
      std::min(STAR_SIGNING_CAP, STAR_SIGNING * static_cast<float>(std::max(
                                                    0, facts.star_signings))));

  float index = NEUTRAL;
  for (const SupporterReason& reason : mood.reasons) index += reason.points;
  mood.index = std::clamp(index, 0.0f, 100.0f);
  std::ranges::stable_sort(mood.reasons, std::greater<>(),
                           [](const SupporterReason& reason)
                           { return std::abs(reason.points); });
  return mood;
}

float SupporterModel::settle(float previous, float target)
{
  return std::clamp(std::lerp(previous, target, WEEKLY_SETTLE), 0.0f, 100.0f);
}

float SupporterModel::boardNudge(float index)
{
  const float offset = std::clamp(index, 0.0f, 100.0f) - 50.0f;
  if (std::abs(offset) <= BOARD_DEAD_ZONE) return 0.0f;
  const float beyond = std::abs(offset) - BOARD_DEAD_ZONE;
  const float share = beyond / (50.0f - BOARD_DEAD_ZONE);
  return std::copysign(MAX_BOARD_NUDGE * std::min(share, 1.0f), offset);
}

const char* SupporterModel::moodKey(float index)
{
  if (index < 25.0f) return "SUPPORTERS_MOOD_ANGRY";
  if (index < 45.0f) return "SUPPORTERS_MOOD_RESTLESS";
  if (index < 65.0f) return "SUPPORTERS_MOOD_CONTENT";
  if (index < 85.0f) return "SUPPORTERS_MOOD_HAPPY";
  return "SUPPORTERS_MOOD_DELIGHTED";
}

const char* SupporterModel::reasonKey(const SupporterReason& reason)
{
  using enum SupporterFactor;
  const bool good = reason.points > 0.0f;
  switch (reason.factor)
  {
    case Derby:
      return good ? "SUPPORTERS_REASON_DERBY_WON"
                  : "SUPPORTERS_REASON_DERBY_LOST";
    case TicketPrice:
      return good ? "SUPPORTERS_REASON_TICKETS_CHEAP"
                  : "SUPPORTERS_REASON_TICKETS_DEAR";
    case StarSale:
      return "SUPPORTERS_REASON_STAR_SOLD";
    case StarSigning:
      return "SUPPORTERS_REASON_STAR_SIGNED";
    case Results:
    case COUNT:
      break;
  }
  return good ? "SUPPORTERS_REASON_RESULTS_GOOD"
              : "SUPPORTERS_REASON_RESULTS_BAD";
}

// ---------------------------------------------------------------------------
// Supporters
// ---------------------------------------------------------------------------

float Supporters::update(TeamID new_team_id, std::int32_t day,
                         const SupporterFacts& facts)
{
  SupporterMood target = SupporterModel::evaluate(facts);
  const bool new_club = new_team_id != team_id;
  if (!new_club)
    target.index = SupporterModel::settle(current.index, target.index);
  current = std::move(target);
  team_id = new_team_id;
  updated_day = day;
  return new_club ? 0.0f : SupporterModel::boardNudge(current.index);
}

SupporterFacts Supporters::gather(const GameData& gamedata,
                                  const GameDateValue& date, TeamID team_id,
                                  const BoardState& board,
                                  const Calendar& calendar,
                                  const TransferMarket& transfers,
                                  std::optional<TeamID> rival)
{
  using enum TransferKind;
  SupporterFacts facts;
  const auto team = gamedata.getTeam(team_id);
  if (!team) return facts;

  if (board.team_id == team_id)
    facts.result_deltas.assign(
        board.recent_deltas.begin(),
        board.recent_deltas.begin() +
            std::min<std::size_t>(board.result_count,
                                  board.recent_deltas.size()));

  if (rival.has_value())
  {
    const GameDateValue season_start(SeasonCalendar::seasonStartYear(date),
                                     SEASON_START_MONTH, 1);
    const auto& schedule = calendar.getFullCalendar();
    for (auto day = schedule.lower_bound(season_start);
         day != schedule.end() && !(date < day->first); ++day)
    {
      for (const Match& match : day->second)
      {
        if (!match.isPlayed() || match.getMatchType() == MatchType::FRIENDLY)
          continue;
        const bool home =
            match.getHomeTeamId() == team_id && match.getAwayTeamId() == *rival;
        if (const bool away = match.getAwayTeamId() == team_id &&
                              match.getHomeTeamId() == *rival;
            !home && !away)
          continue;
        const std::optional<TeamID> winner = match.getWinnerId();
        int points = 1;
        if (winner && *winner == team_id)
          points = 3;
        else if (winner)
          points = 0;
        facts.derby_points.insert(facts.derby_points.begin(), points);
      }
    }
  }

  const auto economies = buildLeagueEconomies(gamedata);
  if (const auto economy = economies.find(team->get().getLeagueId());
      economy != economies.end())
  {
    const ClubProfile& profile = team->get().getProfile();
    const double fair = ClubEconomy::fairTicketPrice(economy->second, profile);
    if (fair > 0.0 && profile.ticket_price > 0)
      facts.ticket_ratio =
          static_cast<float>(static_cast<double>(profile.ticket_price) / fair);
  }

  // Stars are measured against today's squad: the best STAR_RANK players.
  const StatsConfig& config = gamedata.getStatsConfig();
  std::vector<double> overalls;
  for (const auto& player : gamedata.getPlayersForTeam(team_id))
    overalls.push_back(player.get().getOverall(config));
  std::ranges::sort(overalls, std::greater<>());
  if (overalls.empty()) return facts;
  const double star_level = overalls
      [std::min<std::size_t>(SupporterModel::STAR_RANK, overalls.size()) - 1];
  const std::int32_t today = dayOrdinal(date);
  for (const TransferRecord& record : transfers.history())
  {
    if (today - dayOrdinal(record.date) > SupporterModel::STAR_WINDOW_DAYS ||
        date < record.date)
      continue;
    const auto player = gamedata.getPlayer(record.player_id);
    if (!player || player->get().getOverall(config) < star_level) continue;
    const bool fee_move = record.kind == Permanent || record.kind == Free ||
                          record.kind == PreContract;
    if (record.from_team == team_id && fee_move &&
        player->get().getTeamId() != team_id)
      ++facts.star_sales;
    else if (record.to_team == team_id && (fee_move || record.kind == Loan) &&
             player->get().getTeamId() == team_id)
      ++facts.star_signings;
  }
  return facts;
}

void Supporters::onDayEnd(const GameData& gamedata, const GameDateValue& date,
                          TeamID managed_team_id, const Calendar& calendar,
                          const TransferMarket& transfers,
                          WorldSimulation& world)
{
  if (managed_team_id == FREE_AGENTS_TEAM_ID) return;
  const std::int32_t today = dayOrdinal(date);
  if (team_id == managed_team_id &&
      (today % DAYS_PER_WEEK != 0 || updated_day == today))
    return;
  const BoardState& board = world.getBoardState();
  const SupporterFacts facts =
      gather(gamedata, date, managed_team_id, board, calendar, transfers,
             world.getStories().rivalOf(managed_team_id));
  const float nudge = update(managed_team_id, today, facts);
  if (nudge != 0.0f && board.team_id == managed_team_id && !board.dismissed)
    world.adjustBoardConfidence(nudge);
}

void Supporters::load(const std::shared_ptr<DatabaseConnection>& db_conn)
{
  *this = Supporters();
  forEachRow(
      *db_conn,
      "SELECT team_id, mood, updated_day, reasons FROM SupporterMood "
      "WHERE id = 1;",
      [this](sqlite3_stmt* stmt)
      {
        team_id = static_cast<TeamID>(sqlite3_column_int(stmt, 0));
        current.index = std::clamp(
            static_cast<float>(sqlite3_column_double(stmt, 1)), 0.0f, 100.0f);
        updated_day = sqlite3_column_int(stmt, 2);
        const auto* text =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        std::istringstream reasons(text ? text : "");
        int factor = 0;
        float points = 0.0f;
        char separator = 0;
        while (reasons >> factor >> separator >> points)
        {
          if (factor < 0 || factor >= static_cast<int>(std::to_underlying(
                                          SupporterFactor::COUNT)))
            continue;  // Written by a newer version.
          current.reasons.push_back(
              {static_cast<SupporterFactor>(factor), points});
        }
      });
}

void Supporters::save(const std::shared_ptr<DatabaseConnection>& db_conn) const
{
  const DatabaseConnection& db = *db_conn;
  sqlite3_exec(db.getRaw(), "DELETE FROM SupporterMood;", nullptr, nullptr,
               nullptr);
  if (team_id == 0) return;
  std::string reasons;
  for (const SupporterReason& reason : current.reasons)
  {
    if (!reasons.empty()) reasons += ' ';
    reasons += std::format("{}:{:.2f}",
                           static_cast<int>(std::to_underlying(reason.factor)),
                           reason.points);
  }
  sqlite3_stmt* stmt = db.prepareStatement(
      "INSERT INTO SupporterMood (id, team_id, mood, updated_day, reasons) "
      "VALUES (1, ?, ?, ?, ?);");
  sqlite3_bind_int(stmt, 1, team_id);
  sqlite3_bind_double(stmt, 2, current.index);
  sqlite3_bind_int(stmt, 3, updated_day);
  sqlite3_bind_text(stmt, 4, reasons.c_str(), -1, SQLITE_TRANSIENT);
  db.executeStep(stmt);
  sqlite3_finalize(stmt);
}
