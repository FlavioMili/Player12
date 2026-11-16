// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/records.h"

#include <algorithm>
#include <utility>

#include "database/gamedata.h"
#include "database/sqlite_rows.h"
#include "model/match_report.h"
#include "model/player.h"
#include "model/team.h"

namespace
{
/** Version stored in RecordMeta; a save without it is rebuilt once. */
constexpr int BOOK_VERSION = 1;

std::uint16_t seasonOf(const GameDateValue& date)
{
  return static_cast<std::uint16_t>(date.month >= 7 ? date.year
                                                    : date.year - 1);
}

/** Goals per player of a report (player lines, else goal events). */
std::vector<std::pair<PlayerID, std::uint16_t>> scorers(
    const MatchReport& report)
{
  std::vector<std::pair<PlayerID, std::uint16_t>> goals;
  for (const PlayerMatchLine& line : report.players)
    if (line.goals > 0) goals.emplace_back(line.player_id, line.goals);
  if (!goals.empty()) return goals;
  for (const MatchReportEvent& event : report.events)
  {
    if (event.kind != MatchEventKind::GOAL || event.player == 0) continue;
    const auto found = std::ranges::find(
        goals, event.player, &std::pair<PlayerID, std::uint16_t>::first);
    if (found == goals.end())
      goals.emplace_back(event.player, 1);
    else
      ++found->second;
  }
  return goals;
}

std::string playerName(const GameData& gamedata, PlayerID player_id)
{
  const auto player = gamedata.getPlayer(player_id);
  return player ? player->get().getName() : std::string();
}
}  // namespace

const char* recordKindKey(RecordKind kind)
{
  using enum RecordKind;
  switch (kind)
  {
    case BiggestWin:
      return "RECORD_BIGGEST_WIN";
    case BiggestDefeat:
      return "RECORD_BIGGEST_DEFEAT";
    case HighestScoringMatch:
      return "RECORD_HIGHEST_SCORING";
    case HighestAttendance:
      return "RECORD_ATTENDANCE";
    case MostGoalsSeason:
      return "RECORD_MOST_GOALS_SEASON";
    case MostPointsSeason:
      return "RECORD_MOST_POINTS_SEASON";
    case TopScorerSeason:
      return "RECORD_TOP_SCORER_SEASON";
    case RecordSigning:
      return "RECORD_SIGNING";
    case RecordSale:
    case COUNT:
      break;
  }
  return "RECORD_SALE";
}

namespace Records
{
bool beats(const RecordEntry& challenger, const RecordEntry& holder)
{
  if (challenger.value != holder.value) return challenger.value > holder.value;
  const bool margin = challenger.kind == RecordKind::BiggestWin ||
                      challenger.kind == RecordKind::BiggestDefeat;
  return margin && challenger.value2 > holder.value2;
}

bool isLegend(const ClubPlayerTotal& totals, int honours)
{
  return totals.appearances >= LEGEND_APPEARANCES ||
         totals.goals >= LEGEND_GOALS ||
         (totals.appearances >= LEGEND_HONOURS_APPEARANCES &&
          honours >= LEGEND_HONOURS);
}

int legendScore(const ClubPlayerTotal& totals, int honours)
{
  return totals.appearances + 2 * totals.goals + 30 * honours;
}
}  // namespace Records

void RecordBook::offer(RecordEntry entry)
{
  const RecordKey key{entry.scope, entry.scope_id, entry.kind};
  const auto found = records.find(key);
  if (found == records.end())
    records.try_emplace(key, std::move(entry));
  else if (Records::beats(entry, found->second))
    found->second = std::move(entry);
}

void RecordBook::onMatchPlayed(const GameData& gamedata,
                               const MatchReport& report)
{
  if (report.match_type == MatchType::FRIENDLY) return;
  const std::uint16_t year = seasonOf(report.date);
  const bool season_open = !season_teams.empty() || !season_scorers.empty();
  // A season that was never closed (old saves) is settled on first contact.
  if (season_open && year > season_year) closeSeason(gamedata, season_year);
  if (!season_open || year > season_year) season_year = year;
  const bool league = report.match_type == MatchType::LEAGUE;
  LeagueID league_id = league ? report.competition_id : 0;
  if (league && league_id == 0)
    if (const auto home = gamedata.getTeam(report.home_team_id))
      league_id = home->get().getLeagueId();

  const auto side = [&report, year](bool home)
  {
    RecordEntry entry;
    entry.team_id = home ? report.home_team_id : report.away_team_id;
    entry.opponent_id = home ? report.away_team_id : report.home_team_id;
    entry.goals_for = home ? report.home_goals : report.away_goals;
    entry.goals_against = home ? report.away_goals : report.home_goals;
    entry.home = home;
    entry.date = report.date;
    entry.season_year = year;
    return entry;
  };
  const auto offerBoth = [this, league, league_id](RecordEntry entry)
  {
    entry.scope = RecordScope::Club;
    entry.scope_id = entry.team_id;
    offer(entry);
    if (!league) return;
    entry.scope = RecordScope::League;
    entry.scope_id = league_id;
    offer(std::move(entry));
  };
  for (const bool home : {true, false})
  {
    RecordEntry entry = side(home);
    const int own = entry.goals_for;
    const int other = entry.goals_against;
    if (own != other)
    {
      entry.kind =
          own > other ? RecordKind::BiggestWin : RecordKind::BiggestDefeat;
      entry.value = std::abs(own - other);
      entry.value2 = own > other ? own : other;
      // League records keep the winner's side only.
      if (own > other)
        offerBoth(entry);
      else
      {
        entry.scope = RecordScope::Club;
        entry.scope_id = entry.team_id;
        offer(entry);
      }
    }
    RecordEntry total = side(home);
    total.kind = RecordKind::HighestScoringMatch;
    total.value = own + other;
    total.scope = RecordScope::Club;
    total.scope_id = total.team_id;
    offer(total);
  }
  if (league)
  {
    RecordEntry total = side(true);
    total.kind = RecordKind::HighestScoringMatch;
    total.value = report.home_goals + report.away_goals;
    total.scope = RecordScope::League;
    total.scope_id = league_id;
    offer(std::move(total));
  }
  if (report.attendance > 0)
  {
    RecordEntry crowd = side(true);
    crowd.kind = RecordKind::HighestAttendance;
    crowd.value = report.attendance;
    offerBoth(std::move(crowd));
  }

  const auto goals = scorers(report);
  for (const PlayerMatchLine& line : report.players)
  {
    if (line.minutes == 0) continue;
    ClubPlayerTotal& totals = club_players[{line.team_id, line.player_id}];
    totals.team_id = line.team_id;
    totals.player_id = line.player_id;
    if (const std::string name = playerName(gamedata, line.player_id);
        !name.empty())
      totals.name = name;
    ++totals.appearances;
    if (totals.first_year == 0) totals.first_year = year;
    totals.last_year = year;
  }
  for (const auto& [player_id, count] : goals)
  {
    const auto line = std::ranges::find(report.players, player_id,
                                        &PlayerMatchLine::player_id);
    TeamID team_id = line != report.players.end() ? line->team_id : 0;
    if (team_id == 0)
      if (const auto player = gamedata.getPlayer(player_id))
        team_id = player->get().getTeamId();
    if (team_id != report.home_team_id && team_id != report.away_team_id)
      continue;
    ClubPlayerTotal& totals = club_players[{team_id, player_id}];
    totals.team_id = team_id;
    totals.player_id = player_id;
    if (totals.name.empty()) totals.name = playerName(gamedata, player_id);
    totals.goals = static_cast<std::uint16_t>(totals.goals + count);
    if (totals.first_year == 0) totals.first_year = year;
    totals.last_year = year;
    season_scorers[{team_id, player_id}] = static_cast<std::uint16_t>(
        season_scorers[{team_id, player_id}] + count);
  }

  if (!league) return;
  for (const bool home : {true, false})
  {
    const TeamID team_id = home ? report.home_team_id : report.away_team_id;
    const int own = home ? report.home_goals : report.away_goals;
    const int other = home ? report.away_goals : report.home_goals;
    std::uint32_t points = 0;
    if (own > other)
      points = 3;
    else if (own == other)
      points = 1;
    AllTimeRow& row = all_time[{league_id, team_id}];
    row.league_id = league_id;
    row.team_id = team_id;
    ++row.played;
    row.won += own > other ? 1 : 0;
    row.drawn += own == other ? 1 : 0;
    row.lost += own < other ? 1 : 0;
    row.goals_for += static_cast<std::uint32_t>(own);
    row.goals_against += static_cast<std::uint32_t>(other);
    row.points += points;
    SeasonTeam& season = season_teams[team_id];
    season.league_id = league_id;
    ++season.played;
    season.goals_for = static_cast<std::uint16_t>(season.goals_for + own);
    season.points = static_cast<std::uint16_t>(season.points + points);
  }
}

void RecordBook::onTransfer(const GameData& gamedata, const GameDateValue& date,
                            PlayerID player_id, TeamID from_team_id,
                            TeamID to_team_id, std::uint32_t fee)
{
  if (fee == 0) return;
  RecordEntry entry;
  entry.value = fee;
  entry.player_id = player_id;
  entry.name = playerName(gamedata, player_id);
  entry.date = date;
  entry.season_year = seasonOf(date);
  if (to_team_id != FREE_AGENTS_TEAM_ID && gamedata.getTeam(to_team_id))
  {
    RecordEntry signing = entry;
    signing.kind = RecordKind::RecordSigning;
    signing.team_id = to_team_id;
    signing.opponent_id = from_team_id;
    signing.scope = RecordScope::Club;
    signing.scope_id = to_team_id;
    offer(signing);
    signing.scope = RecordScope::League;
    signing.scope_id = gamedata.getTeam(to_team_id)->get().getLeagueId();
    offer(std::move(signing));
  }
  if (from_team_id != FREE_AGENTS_TEAM_ID && gamedata.getTeam(from_team_id))
  {
    RecordEntry sale = entry;
    sale.kind = RecordKind::RecordSale;
    sale.team_id = from_team_id;
    sale.opponent_id = to_team_id;
    sale.scope = RecordScope::Club;
    sale.scope_id = from_team_id;
    offer(std::move(sale));
  }
}

void RecordBook::closeSeason(const GameData& gamedata, std::uint16_t year)
{
  using enum RecordKind;
  for (const auto& [team_id, season] : season_teams)
  {
    RecordEntry entry;
    entry.team_id = team_id;
    entry.season_year = year;
    entry.kind = MostGoalsSeason;
    entry.value = season.goals_for;
    entry.value2 = season.played;
    for (const RecordKind kind : {MostGoalsSeason, MostPointsSeason})
    {
      entry.kind = kind;
      entry.value = kind == MostGoalsSeason ? season.goals_for : season.points;
      entry.scope = RecordScope::Club;
      entry.scope_id = team_id;
      offer(entry);
      entry.scope = RecordScope::League;
      entry.scope_id = season.league_id;
      offer(entry);
    }
  }
  for (const auto& [key, goals] : season_scorers)
  {
    const auto [team_id, player_id] = key;
    RecordEntry entry;
    entry.kind = TopScorerSeason;
    entry.team_id = team_id;
    entry.player_id = player_id;
    entry.value = goals;
    entry.season_year = year;
    const auto totals = club_players.find(key);
    entry.name = totals != club_players.end() ? totals->second.name
                                              : playerName(gamedata, player_id);
    entry.scope = RecordScope::Club;
    entry.scope_id = team_id;
    offer(entry);
    if (const auto team = gamedata.getTeam(team_id))
    {
      entry.scope = RecordScope::League;
      entry.scope_id = team->get().getLeagueId();
      offer(std::move(entry));
    }
  }
  season_teams.clear();
  season_scorers.clear();
  season_year = static_cast<std::uint16_t>(year + 1);
}

std::vector<RecordEntry> RecordBook::clubRecords(TeamID team_id) const
{
  std::vector<RecordEntry> result;
  for (std::uint8_t kind = 0; kind < std::to_underlying(RecordKind::COUNT);
       ++kind)
  {
    const auto found = records.find(
        {RecordScope::Club, team_id, static_cast<RecordKind>(kind)});
    if (found != records.end()) result.push_back(found->second);
  }
  return result;
}

std::vector<RecordEntry> RecordBook::leagueRecords(LeagueID league_id) const
{
  std::vector<RecordEntry> result;
  for (std::uint8_t kind = 0; kind < std::to_underlying(RecordKind::COUNT);
       ++kind)
  {
    const auto found = records.find(
        {RecordScope::League, league_id, static_cast<RecordKind>(kind)});
    if (found != records.end()) result.push_back(found->second);
  }
  return result;
}

std::vector<ClubPlayerTotal> RecordBook::topScorers(TeamID team_id,
                                                    std::size_t limit) const
{
  std::vector<ClubPlayerTotal> result;
  for (auto it = club_players.lower_bound({team_id, 0});
       it != club_players.end() && it->first.first == team_id; ++it)
    if (it->second.goals > 0) result.push_back(it->second);
  std::ranges::sort(
      result,
      [](const ClubPlayerTotal& a, const ClubPlayerTotal& b)
      {
        return std::tuple(-static_cast<int>(a.goals),
                          static_cast<int>(a.appearances), a.player_id) <
               std::tuple(-static_cast<int>(b.goals),
                          static_cast<int>(b.appearances), b.player_id);
      });
  if (result.size() > limit) result.resize(limit);
  return result;
}

std::vector<ClubPlayerTotal> RecordBook::mostAppearances(
    TeamID team_id, std::size_t limit) const
{
  std::vector<ClubPlayerTotal> result;
  for (auto it = club_players.lower_bound({team_id, 0});
       it != club_players.end() && it->first.first == team_id; ++it)
    result.push_back(it->second);
  std::ranges::sort(
      result,
      [](const ClubPlayerTotal& a, const ClubPlayerTotal& b)
      {
        return std::tuple(-static_cast<int>(a.appearances),
                          -static_cast<int>(a.goals), a.player_id) <
               std::tuple(-static_cast<int>(b.appearances),
                          -static_cast<int>(b.goals), b.player_id);
      });
  if (result.size() > limit) result.resize(limit);
  return result;
}

std::vector<AllTimeRow> RecordBook::allTimeTable(LeagueID league_id) const
{
  std::vector<AllTimeRow> result;
  for (auto it = all_time.lower_bound({league_id, 0});
       it != all_time.end() && it->first.first == league_id; ++it)
    result.push_back(it->second);
  std::ranges::sort(result,
                    [](const AllTimeRow& a, const AllTimeRow& b)
                    {
                      const auto key = [](const AllTimeRow& row)
                      {
                        return std::tuple(
                            -static_cast<std::int64_t>(row.points),
                            -(static_cast<std::int64_t>(row.goals_for) -
                              static_cast<std::int64_t>(row.goals_against)),
                            -static_cast<std::int64_t>(row.goals_for),
                            row.team_id);
                      };
                      return key(a) < key(b);
                    });
  return result;
}

std::vector<LegendEntry> RecordBook::hallOfFame(
    TeamID team_id, const std::function<int(PlayerID, TeamID)>& honours) const
{
  std::vector<LegendEntry> result;
  for (auto it = club_players.lower_bound({team_id, 0});
       it != club_players.end() && it->first.first == team_id; ++it)
  {
    const int won = honours ? honours(it->second.player_id, team_id) : 0;
    if (!Records::isLegend(it->second, won)) continue;
    result.emplace_back(it->second, won, Records::legendScore(it->second, won));
  }
  std::ranges::sort(result,
                    [](const LegendEntry& a, const LegendEntry& b)
                    {
                      return std::tuple(-a.score, a.totals.player_id) <
                             std::tuple(-b.score, b.totals.player_id);
                    });
  if (result.size() > Records::HALL_OF_FAME_SIZE)
    result.resize(Records::HALL_OF_FAME_SIZE);
  return result;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void RecordBook::clear()
{
  records.clear();
  club_players.clear();
  all_time.clear();
  season_teams.clear();
  season_scorers.clear();
  season_year = 0;
}

void RecordBook::rebuild(const std::shared_ptr<DatabaseConnection>& db_conn,
                         const GameData& gamedata)
{
  using namespace SqliteRows;
  // Stored reports in date order; seasons close as the dates move on.
  forEach(*db_conn,
          "SELECT game_date, home_team_id, away_team_id, match_type, "
          "competition_id, home_goals, away_goals, attendance, events, players "
          "FROM MatchReports ORDER BY game_date;",
          [this, &gamedata](sqlite3_stmt* stmt)
          {
            MatchReport report;
            try
            {
              report.date = GameDateValue::fromString(columnText(stmt, 0));
            }
            catch (const std::exception&)
            {
              return;
            }
            report.home_team_id = column<TeamID>(stmt, 1);
            report.away_team_id = column<TeamID>(stmt, 2);
            report.match_type = static_cast<MatchType>(column<int>(stmt, 3));
            report.competition_id = column<LeagueID>(stmt, 4);
            report.home_goals = column<std::uint8_t>(stmt, 5);
            report.away_goals = column<std::uint8_t>(stmt, 6);
            report.attendance = column<std::uint32_t>(stmt, 7);
            report.eventsFromJson(columnText(stmt, 8));
            report.playersFromJson(columnText(stmt, 9));
            onMatchPlayed(gamedata, report);
          });
  forEach(*db_conn,
          "SELECT player_id, game_date, from_team_id, to_team_id, fee FROM "
          "TransferHistory WHERE kind = 0 AND fee > 0 ORDER BY seq;",
          [this, &gamedata](sqlite3_stmt* stmt)
          {
            const auto packed = column<std::uint32_t>(stmt, 1);
            const GameDateValue date(
                static_cast<std::uint16_t>(packed / 10000),
                static_cast<std::uint8_t>(packed / 100 % 100),
                static_cast<std::uint8_t>(packed % 100));
            onTransfer(gamedata, date, column<PlayerID>(stmt, 0),
                       column<TeamID>(stmt, 2), column<TeamID>(stmt, 3),
                       column<std::uint32_t>(stmt, 4));
          });
}

void RecordBook::load(const std::shared_ptr<DatabaseConnection>& db_conn,
                      const GameData& gamedata)
{
  using namespace SqliteRows;
  clear();
  const DatabaseConnection& db = *db_conn;
  bool stored = false;
  forEach(db, "SELECT season_year FROM RecordMeta WHERE id = 1;",
          [this, &stored](sqlite3_stmt* stmt)
          {
            stored = true;
            season_year = column<std::uint16_t>(stmt, 0);
          });
  if (!stored)
  {
    rebuild(db_conn, gamedata);
    return;
  }
  forEach(db,
          "SELECT scope, scope_id, kind, value, value2, team_id, opponent_id, "
          "player_id, name, match_date, season_year, goals_for, goals_against, "
          "home FROM RecordEntries;",
          [this](sqlite3_stmt* stmt)
          {
            const auto kind = column<std::uint8_t>(stmt, 2);
            if (kind >= std::to_underlying(RecordKind::COUNT)) return;
            RecordEntry entry;
            entry.scope = column<int>(stmt, 0) == 0 ? RecordScope::Club
                                                    : RecordScope::League;
            entry.scope_id = column<std::uint32_t>(stmt, 1);
            entry.kind = static_cast<RecordKind>(kind);
            entry.value = column<std::int64_t>(stmt, 3);
            entry.value2 = column<std::int32_t>(stmt, 4);
            entry.team_id = column<TeamID>(stmt, 5);
            entry.opponent_id = column<TeamID>(stmt, 6);
            entry.player_id = column<PlayerID>(stmt, 7);
            entry.name = columnText(stmt, 8);
            if (const std::string date = columnText(stmt, 9); !date.empty())
              entry.date = GameDateValue::fromString(date);
            entry.season_year = column<std::uint16_t>(stmt, 10);
            entry.goals_for = column<std::uint8_t>(stmt, 11);
            entry.goals_against = column<std::uint8_t>(stmt, 12);
            entry.home = column<int>(stmt, 13) != 0;
            records[{entry.scope, entry.scope_id, entry.kind}] =
                std::move(entry);
          });
  forEach(db,
          "SELECT team_id, player_id, name, appearances, goals, first_year, "
          "last_year FROM ClubPlayerTotals;",
          [this](sqlite3_stmt* stmt)
          {
            ClubPlayerTotal totals;
            totals.team_id = column<TeamID>(stmt, 0);
            totals.player_id = column<PlayerID>(stmt, 1);
            totals.name = columnText(stmt, 2);
            totals.appearances = column<std::uint16_t>(stmt, 3);
            totals.goals = column<std::uint16_t>(stmt, 4);
            totals.first_year = column<std::uint16_t>(stmt, 5);
            totals.last_year = column<std::uint16_t>(stmt, 6);
            club_players[{totals.team_id, totals.player_id}] =
                std::move(totals);
          });
  forEach(db,
          "SELECT league_id, team_id, played, won, drawn, lost, goals_for, "
          "goals_against, points FROM AllTimeTable;",
          [this](sqlite3_stmt* stmt)
          {
            AllTimeRow row;
            row.league_id = column<LeagueID>(stmt, 0);
            row.team_id = column<TeamID>(stmt, 1);
            row.played = column<std::uint32_t>(stmt, 2);
            row.won = column<std::uint32_t>(stmt, 3);
            row.drawn = column<std::uint32_t>(stmt, 4);
            row.lost = column<std::uint32_t>(stmt, 5);
            row.goals_for = column<std::uint32_t>(stmt, 6);
            row.goals_against = column<std::uint32_t>(stmt, 7);
            row.points = column<std::uint32_t>(stmt, 8);
            all_time[{row.league_id, row.team_id}] = row;
          });
  forEach(db,
          "SELECT team_id, league_id, played, goals_for, points FROM "
          "RecordSeason;",
          [this](sqlite3_stmt* stmt)
          {
            SeasonTeam& season = season_teams[column<TeamID>(stmt, 0)];
            season.league_id = column<LeagueID>(stmt, 1);
            season.played = column<std::uint16_t>(stmt, 2);
            season.goals_for = column<std::uint16_t>(stmt, 3);
            season.points = column<std::uint16_t>(stmt, 4);
          });
  forEach(
      db, "SELECT team_id, player_id, goals FROM RecordSeasonScorers;",
      [this](sqlite3_stmt* stmt)
      {
        season_scorers[{column<TeamID>(stmt, 0), column<PlayerID>(stmt, 1)}] =
            column<std::uint16_t>(stmt, 2);
      });
}

void RecordBook::save(const std::shared_ptr<DatabaseConnection>& db_conn) const
{
  using namespace SqliteRows;
  const DatabaseConnection& db = *db_conn;
  for (const char* table :
       {"RecordMeta", "RecordEntries", "ClubPlayerTotals", "AllTimeTable",
        "RecordSeason", "RecordSeasonScorers"})
    clearTable(db, table);
  sqlite3_stmt* meta = db.prepareStatement(
      "INSERT INTO RecordMeta (id, version, season_year) VALUES (1, ?, ?);");
  sqlite3_bind_int(meta, 1, BOOK_VERSION);
  sqlite3_bind_int(meta, 2, season_year);
  db.executeStep(meta);
  sqlite3_finalize(meta);
  insertAll(db,
            "INSERT INTO RecordEntries (scope, scope_id, kind, value, value2, "
            "team_id, opponent_id, player_id, name, match_date, season_year, "
            "goals_for, goals_against, home) VALUES (?, ?, ?, ?, ?, ?, ?, ?, "
            "?, ?, ?, ?, ?, ?);",
            records,
            [](sqlite3_stmt* row, const auto& item)
            {
              const RecordEntry& entry = item.second;
              sqlite3_bind_int(
                  row, 1, static_cast<int>(std::to_underlying(entry.scope)));
              sqlite3_bind_int64(row, 2, entry.scope_id);
              sqlite3_bind_int(
                  row, 3, static_cast<int>(std::to_underlying(entry.kind)));
              sqlite3_bind_int64(row, 4, entry.value);
              sqlite3_bind_int(row, 5, entry.value2);
              sqlite3_bind_int(row, 6, entry.team_id);
              sqlite3_bind_int(row, 7, entry.opponent_id);
              sqlite3_bind_int64(row, 8, entry.player_id);
              bindText(row, 9, entry.name);
              bindText(row, 10, entry.date.toString());
              sqlite3_bind_int(row, 11, entry.season_year);
              sqlite3_bind_int(row, 12, entry.goals_for);
              sqlite3_bind_int(row, 13, entry.goals_against);
              sqlite3_bind_int(row, 14, entry.home ? 1 : 0);
            });
  insertAll(db,
            "INSERT INTO ClubPlayerTotals (team_id, player_id, name, "
            "appearances, goals, first_year, last_year) VALUES (?, ?, ?, ?, ?, "
            "?, ?);",
            club_players,
            [](sqlite3_stmt* row, const auto& item)
            {
              const ClubPlayerTotal& totals = item.second;
              sqlite3_bind_int(row, 1, totals.team_id);
              sqlite3_bind_int64(row, 2, totals.player_id);
              bindText(row, 3, totals.name);
              sqlite3_bind_int(row, 4, totals.appearances);
              sqlite3_bind_int(row, 5, totals.goals);
              sqlite3_bind_int(row, 6, totals.first_year);
              sqlite3_bind_int(row, 7, totals.last_year);
            });
  insertAll(db,
            "INSERT INTO AllTimeTable (league_id, team_id, played, won, drawn, "
            "lost, goals_for, goals_against, points) VALUES (?, ?, ?, ?, ?, ?, "
            "?, ?, ?);",
            all_time,
            [](sqlite3_stmt* row, const auto& item)
            {
              const AllTimeRow& line = item.second;
              sqlite3_bind_int(row, 1, static_cast<int>(line.league_id));
              sqlite3_bind_int(row, 2, line.team_id);
              sqlite3_bind_int64(row, 3, line.played);
              sqlite3_bind_int64(row, 4, line.won);
              sqlite3_bind_int64(row, 5, line.drawn);
              sqlite3_bind_int64(row, 6, line.lost);
              sqlite3_bind_int64(row, 7, line.goals_for);
              sqlite3_bind_int64(row, 8, line.goals_against);
              sqlite3_bind_int64(row, 9, line.points);
            });
  insertAll(db,
            "INSERT INTO RecordSeason (team_id, league_id, played, goals_for, "
            "points) VALUES (?, ?, ?, ?, ?);",
            season_teams,
            [](sqlite3_stmt* row, const auto& item)
            {
              sqlite3_bind_int(row, 1, item.first);
              sqlite3_bind_int(row, 2, static_cast<int>(item.second.league_id));
              sqlite3_bind_int(row, 3, item.second.played);
              sqlite3_bind_int(row, 4, item.second.goals_for);
              sqlite3_bind_int(row, 5, item.second.points);
            });
  insertAll(db,
            "INSERT INTO RecordSeasonScorers (team_id, player_id, goals) "
            "VALUES (?, ?, ?);",
            season_scorers,
            [](sqlite3_stmt* row, const auto& item)
            {
              sqlite3_bind_int(row, 1, item.first.first);
              sqlite3_bind_int64(row, 2, item.first.second);
              sqlite3_bind_int(row, 3, item.second);
            });
}
