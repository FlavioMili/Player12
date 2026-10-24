// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/preseason.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "database/gamedata.h"
#include "database/sqlite_rows.h"
#include "model/calendar.h"
#include "model/club_economy.h"
#include "model/competition.h"
#include "model/inbox.h"
#include "model/league.h"
#include "model/match_report.h"
#include "model/team.h"
#include "model/training.h"

namespace
{
constexpr std::size_t MAX_OPPONENT_OPTIONS = 40;
/** Reputation from which a club draws crowds abroad. [P] */
constexpr std::uint8_t TOUR_REPUTATION = 65;
/** Weeks of payroll left after paying for the camp. [P] */
constexpr int DOMESTIC_CAMP_RESERVE_WEEKS = 20;
constexpr int ABROAD_CAMP_RESERVE_WEEKS = 30;
constexpr std::uint8_t ABROAD_CAMP_REPUTATION = 60;

std::uint16_t seasonOf(const GameDateValue& date)
{
  return SeasonCalendar::seasonStartYear(date);
}

double seasonIncome(const GameData& gamedata, const Team& team)
{
  std::vector<std::uint8_t> reputations;
  if (const auto league = gamedata.getLeague(team.getLeagueId()))
    for (const TeamID team_id : league->get().getTeamIDs())
      if (const auto other = gamedata.getTeam(team_id))
        reputations.push_back(other->get().getReputation());
  return ClubEconomy::expectedIncome(
      makeLeagueEconomy(team.getLeagueId(), reputations), team.getReputation());
}

bool involves(const Match& match, TeamID team_id)
{
  return match.getHomeTeamId() == team_id || match.getAwayTeamId() == team_id;
}

/** The pre-season week (Monday to Sunday) of @p date. */
std::array<GameDateValue, 7> weekOf(const GameDateValue& date)
{
  const GameDateValue monday =
      SeasonCalendar::addDays(date, -SeasonCalendar::dayOfWeek(date));
  std::array<GameDateValue, 7> days;
  for (int offset = 0; offset < 7; ++offset)
    days[static_cast<std::size_t>(offset)] =
        SeasonCalendar::addDays(monday, offset);
  return days;
}

TeamID otherSide(const Match& match, TeamID team_id)
{
  return match.getHomeTeamId() == team_id ? match.getAwayTeamId()
                                          : match.getHomeTeamId();
}

int levelTarget(OpponentLevel level, int own)
{
  using enum OpponentLevel;
  switch (level)
  {
    case Weaker:
      return own - 12;
    case Stronger:
      return own + 12;
    case Similar:
    case COUNT:
      break;
  }
  return own;
}
}  // namespace

namespace Preseason
{
CampQuote campQuote(TrainingCamp camp, double season_income,
                    const GameDateValue& first_friendly)
{
  using enum TrainingCamp;
  CampQuote quote;
  quote.camp = camp;
  quote.start = SeasonCalendar::addDays(first_friendly, 1);
  quote.end = SeasonCalendar::addDays(first_friendly, CAMP_DAYS);
  quote.available = camp != None;
  switch (camp)
  {
    case Domestic:
      quote.cost =
          static_cast<std::int64_t>(std::llround(season_income * 0.004));
      quote.sharpness = 6.0f;
      quote.familiarity = 4.0f;
      quote.morale = 1.0f;
      break;
    case Abroad:
      quote.cost =
          static_cast<std::int64_t>(std::llround(season_income * 0.012));
      quote.sharpness = 9.0f;
      quote.familiarity = 6.0f;
      quote.morale = 3.0f;
      break;
    case None:
    case COUNT:
      break;
  }
  return quote;
}

std::int64_t tourFee(double season_income, std::uint8_t opponent_reputation)
{
  const double gross =
      season_income * 0.004 *
      (0.8 + 0.6 * static_cast<double>(opponent_reputation) / 100.0);
  const double travel = season_income * 0.001;
  return std::max<std::int64_t>(0, std::llround(gross - travel));
}

bool levelMatches(OpponentLevel level, std::uint8_t own, std::uint8_t other)
{
  using enum OpponentLevel;
  const int gap = static_cast<int>(other) - static_cast<int>(own);
  switch (level)
  {
    case Weaker:
      return gap <= -LEVEL_GAP;
    case Stronger:
      return gap >= LEVEL_GAP;
    case Similar:
    case COUNT:
      break;
  }
  return std::abs(gap) < LEVEL_GAP;
}
}  // namespace Preseason

std::vector<FriendlySlot> PreseasonPlanner::friendlies(
    const Calendar& calendar, TeamID managed_team_id,
    const GameDateValue& today) const
{
  std::vector<FriendlySlot> slots;
  for (const auto& [date, matches] : calendar.getFullCalendar())
  {
    for (const Match& match : matches)
    {
      if (!involves(match, managed_team_id)) continue;
      if (match.getMatchType() != MatchType::FRIENDLY) return slots;
      FriendlySlot slot;
      slot.date = date;
      slot.home = match.getHomeTeamId() == managed_team_id;
      slot.opponent_id = otherSide(match, managed_team_id);
      slot.tour = !slot.home && std::ranges::contains(state.tour_dates, date);
      slot.editable = !match.isPlayed() && today < date;
      slots.push_back(slot);
    }
  }
  return slots;
}

std::optional<GameDateValue> PreseasonPlanner::firstFriendly(
    const Calendar& calendar, TeamID managed_team_id) const
{
  for (const auto& [date, matches] : calendar.getFullCalendar())
    for (const Match& match : matches)
      if (involves(match, managed_team_id))
        return match.getMatchType() == MatchType::FRIENDLY ? std::optional(date)
                                                           : std::nullopt;
  return std::nullopt;
}

std::vector<OpponentOption> PreseasonPlanner::opponents(
    const GameData& gamedata, const Calendar& calendar, TeamID managed_team_id,
    const GameDateValue& date, OpponentLevel level, bool abroad) const
{
  const auto managed = gamedata.getTeam(managed_team_id);
  if (!managed) return {};
  const std::uint8_t own = managed->get().getReputation();
  const LeagueID home_root =
      Competitions::rootLeague(gamedata, managed->get().getLeagueId());
  // Clubs busy that week with anything but one unplayed friendly cannot
  // move (a week's friendlies are spread from Tuesday to Sunday).
  std::unordered_map<TeamID, bool> movable;
  for (const GameDateValue& day : weekOf(date))
    for (const Match& match : calendar.getMatchesForDate(day))
    {
      const bool free =
          match.getMatchType() == MatchType::FRIENDLY && !match.isPlayed();
      for (const TeamID team_id :
           {match.getHomeTeamId(), match.getAwayTeamId()})
      {
        const auto [entry, added] = movable.try_emplace(team_id, free);
        if (!added) entry->second = false;  // Two matches that week.
      }
    }
  std::vector<OpponentOption> options;
  for (const auto& [team_id, team] : gamedata.getTeams())
  {
    if (team_id == managed_team_id || team_id == FREE_AGENTS_TEAM_ID) continue;
    if (const auto found = movable.find(team_id);
        found != movable.end() && !found->second)
      continue;
    const bool foreign =
        Competitions::rootLeague(gamedata, team.getLeagueId()) != home_root;
    if (foreign != abroad ||
        !Preseason::levelMatches(level, own, team.getReputation()))
      continue;
    options.push_back({team_id, team.getReputation(), foreign});
  }
  const int target = levelTarget(level, own);
  std::ranges::sort(options,
                    [target](const OpponentOption& a, const OpponentOption& b)
                    {
                      const int da = std::abs(a.reputation - target);
                      const int db = std::abs(b.reputation - target);
                      return da != db ? da < db : a.team_id < b.team_id;
                    });
  if (options.size() > MAX_OPPONENT_OPTIONS)
    options.resize(MAX_OPPONENT_OPTIONS);
  return options;
}

bool PreseasonPlanner::setFriendly(Calendar& calendar, const GameData& gamedata,
                                   TeamID managed_team_id,
                                   const GameDateValue& date,
                                   TeamID opponent_id, bool home, bool tour,
                                   const GameDateValue& today)
{
  if (!(today < date) || opponent_id == managed_team_id ||
      opponent_id == FREE_AGENTS_TEAM_ID || !gamedata.getTeam(opponent_id))
    return false;
  if (const auto day_it = calendar.getFullCalendar().find(date);
      day_it == calendar.getFullCalendar().end())
    return false;
  std::vector<Match>& day = calendar.getMatchesForDateMutable(date);
  const auto managed_it =
      std::ranges::find_if(day, [&](const Match& match)
                           { return involves(match, managed_team_id); });
  if (managed_it == day.end()) return false;
  Match& current = *managed_it;
  if (current.getMatchType() != MatchType::FRIENDLY || current.isPlayed())
    return false;
  // The opponent's own match of that week (friendlies run Tuesday-Sunday).
  std::optional<std::pair<GameDateValue, std::size_t>> opponent_slot;
  for (const GameDateValue& other_day : weekOf(date))
  {
    const std::vector<Match>& matches = calendar.getMatchesForDate(other_day);
    for (std::size_t index = 0; index < matches.size(); ++index)
    {
      const Match& match = matches[index];
      if (!involves(match, opponent_id)) continue;
      if (opponent_slot || match.getMatchType() != MatchType::FRIENDLY ||
          match.isPlayed() || !(today < other_day))
        return false;
      opponent_slot.emplace(other_day, index);
    }
  }
  Match* opponent_match = opponent_slot
                              ? &calendar.getMatchesForDateMutable(
                                    opponent_slot->first)[opponent_slot->second]
                              : nullptr;
  // Only competitive-free weeks: the date must still be pre-season.
  if (const auto slots = friendlies(calendar, managed_team_id, today);
      !std::ranges::contains(slots, date, &FriendlySlot::date))
    return false;

  const auto managed = gamedata.getTeam(managed_team_id);
  const auto opponent = gamedata.getTeam(opponent_id);
  const bool abroad =
      Competitions::rootLeague(gamedata, managed->get().getLeagueId()) !=
      Competitions::rootLeague(gamedata, opponent->get().getLeagueId());
  const TeamID previous = otherSide(current, managed_team_id);
  Match chosen(home ? managed_team_id : opponent_id,
               home ? opponent_id : managed_team_id, date, MatchType::FRIENDLY);
  chosen.setKickoff(current.getScheduledKickoff());
  if (opponent_id != previous && opponent_match)
  {
    // The two left-over clubs meet instead (the old opponent at home) on
    // the day the new opponent was to play, so every club still plays once
    // that week and the days keep their number of matches.
    const TeamID left_over = otherSide(*opponent_match, opponent_id);
    Match replacement(previous, left_over, opponent_match->getDate(),
                      MatchType::FRIENDLY);
    replacement.setKickoff(opponent_match->getScheduledKickoff());
    *opponent_match = replacement;
  }
  current = chosen;

  if (state.season_year != seasonOf(date)) onSeasonStart(seasonOf(date));
  std::erase(state.tour_dates, date);
  if (tour && !home && abroad) state.tour_dates.push_back(date);
  return true;
}

std::vector<FriendlySuggestion> PreseasonPlanner::suggest(
    const GameData& gamedata, const Calendar& calendar, TeamID managed_team_id,
    const GameDateValue& today) const
{
  const auto managed = gamedata.getTeam(managed_team_id);
  if (!managed) return {};
  constexpr std::array<OpponentLevel, 4> LEVELS = {
      OpponentLevel::Weaker, OpponentLevel::Similar, OpponentLevel::Stronger,
      OpponentLevel::Similar};
  constexpr std::array<bool, 4> HOME = {true, true, false, true};
  const bool tours = managed->get().getReputation() >= TOUR_REPUTATION;
  std::vector<FriendlySuggestion> plan;
  std::vector<TeamID> used;
  std::size_t index = 0;
  for (const FriendlySlot& slot : friendlies(calendar, managed_team_id, today))
  {
    const std::size_t pattern = index++ % LEVELS.size();
    if (!slot.editable)
    {
      used.push_back(slot.opponent_id);
      continue;
    }
    const bool abroad = tours && !HOME[pattern];
    std::optional<TeamID> pick;
    for (const bool foreign : {abroad, false})
    {
      for (const OpponentOption& option :
           opponents(gamedata, calendar, managed_team_id, slot.date,
                     LEVELS[pattern], foreign))
        if (!std::ranges::contains(used, option.team_id))
        {
          pick = option.team_id;
          break;
        }
      if (pick.has_value()) break;
    }
    if (!pick.has_value()) pick = slot.opponent_id;
    used.push_back(*pick);
    plan.push_back({slot.date, *pick, HOME[pattern]});
  }
  return plan;
}

CampQuote PreseasonPlanner::campQuote(const GameData& gamedata,
                                      const Calendar& calendar,
                                      TeamID managed_team_id,
                                      TrainingCamp camp) const
{
  const auto managed = gamedata.getTeam(managed_team_id);
  const auto first = firstFriendly(calendar, managed_team_id);
  if (!managed || !first) return CampQuote();
  return Preseason::campQuote(camp, seasonIncome(gamedata, managed->get()),
                              *first);
}

TrainingCamp PreseasonPlanner::suggestCamp(const GameData& gamedata,
                                           const Calendar& calendar,
                                           TeamID managed_team_id,
                                           const GameDateValue& today) const
{
  const auto managed = gamedata.getTeam(managed_team_id);
  if (!managed) return TrainingCamp::None;
  const Team& club = managed->get();
  const std::int64_t balance = club.getFinances().getBalance();
  const std::int64_t payroll =
      club.getFinances().getCurrentWageSpending(gamedata, club);
  const auto affordable = [&](TrainingCamp camp, int weeks)
  {
    const CampQuote quote =
        campQuote(gamedata, calendar, managed_team_id, camp);
    return quote.available && today < quote.start &&
           balance - quote.cost >= weeks * payroll;
  };
  if (club.getReputation() >= ABROAD_CAMP_REPUTATION &&
      affordable(TrainingCamp::Abroad, ABROAD_CAMP_RESERVE_WEEKS))
    return TrainingCamp::Abroad;
  if (affordable(TrainingCamp::Domestic, DOMESTIC_CAMP_RESERVE_WEEKS))
    return TrainingCamp::Domestic;
  return TrainingCamp::None;
}

bool PreseasonPlanner::bookCamp(GameData& gamedata, const Calendar& calendar,
                                TeamID managed_team_id, TrainingCamp camp,
                                const GameDateValue& today)
{
  const auto managed = gamedata.getTeam(managed_team_id);
  const auto first = firstFriendly(calendar, managed_team_id);
  if (!managed || !first || camp >= TrainingCamp::COUNT) return false;
  const CampQuote quote = campQuote(gamedata, calendar, managed_team_id, camp);
  if (!(today < quote.start)) return false;
  if (state.season_year != seasonOf(*first)) onSeasonStart(seasonOf(*first));
  if (state.camp == camp) return true;
  Finances& finances = managed->get().getFinances();
  if (state.camp != TrainingCamp::None && state.camp_cost > 0)
    finances.record(today, FinanceCategory::Facilities, state.camp_cost);
  state.camp = camp;
  state.camp_cost = quote.cost;
  state.camp_end = quote.end;
  if (quote.cost > 0)
    finances.record(today, FinanceCategory::Facilities, -quote.cost);
  return true;
}

void PreseasonPlanner::onDay(GameData& gamedata, const GameDateValue& date,
                             TeamID managed_team_id, Inbox& inbox)
{
  if (state.camp == TrainingCamp::None || state.camp_applied ||
      managed_team_id == FREE_AGENTS_TEAM_ID || !(state.camp_end == date))
    return;
  if (const auto managed = gamedata.getTeam(managed_team_id); !managed) return;
  // Only the effect sizes are needed; the dates come from the booking.
  const CampQuote quote = Preseason::campQuote(state.camp, 0.0, date);
  state.camp_applied = true;
  for (const auto& player_ref : gamedata.getPlayersForTeam(managed_team_id))
  {
    auto found = gamedata.getPlayers().find(player_ref.get().getId());
    if (found == gamedata.getPlayers().end()) continue;
    PlayerDynamics& dynamics = found->second.mutableDynamics();
    if (dynamics.injury_days > 0) continue;
    dynamics.sharpness = std::min(100.0f, dynamics.sharpness + quote.sharpness);
    dynamics.morale = std::min(100.0f, dynamics.morale + quote.morale);
  }
  TeamTrainingPlan& plan = gamedata.getTraining().plan(managed_team_id);
  plan.familiarity = std::min(100.0f, plan.familiarity + quote.familiarity);
  InboxMessage message;
  message.date = date;
  message.category = InboxCategory::General;
  message.title_key = "INBOX_CAMP_DONE_TITLE";
  message.body_key = "INBOX_CAMP_DONE_BODY";
  message.args = {std::to_string(std::lround(quote.sharpness)),
                  std::to_string(std::lround(quote.familiarity))};
  message.team_id = managed_team_id;
  inbox.add(std::move(message));
}

void PreseasonPlanner::onMatchPlayed(GameData& gamedata,
                                     const MatchReport& report,
                                     TeamID managed_team_id)
{
  if (report.match_type != MatchType::FRIENDLY ||
      report.away_team_id != managed_team_id ||
      !std::ranges::contains(state.tour_dates, report.date))
    return;
  const auto managed = gamedata.getTeam(managed_team_id);
  const auto opponent = gamedata.getTeam(report.home_team_id);
  if (!managed || !opponent) return;
  Team& club = managed->get();
  if (const std::int64_t fee = Preseason::tourFee(
          seasonIncome(gamedata, club), opponent->get().getReputation());
      fee > 0)
    club.getFinances().record(report.date, FinanceCategory::Matchday, fee);
  ++state.tour_matches;
  if (state.tour_matches >= Preseason::TOUR_MATCHES_FOR_REPUTATION &&
      !state.tour_reputation)
  {
    state.tour_reputation = true;
    ClubProfile profile = club.getProfile();
    profile.reputation =
        static_cast<std::uint8_t>(std::min(100, profile.reputation + 1));
    club.setProfile(profile);
  }
}

void PreseasonPlanner::onSeasonStart(std::uint16_t season_year)
{
  state = PreseasonState();
  state.season_year = season_year;
}

void PreseasonPlanner::clear() { state = PreseasonState(); }

void PreseasonPlanner::load(const std::shared_ptr<DatabaseConnection>& db_conn)
{
  using namespace SqliteRows;
  clear();
  forEach(*db_conn,
          "SELECT season_year, camp, camp_cost, camp_applied, tour_matches, "
          "tour_reputation, camp_end FROM PreseasonPlan WHERE id = 1;",
          [this](sqlite3_stmt* stmt)
          {
            state.season_year = column<std::uint16_t>(stmt, 0);
            const auto camp = column<std::uint8_t>(stmt, 1);
            state.camp = camp < std::to_underlying(TrainingCamp::COUNT)
                             ? static_cast<TrainingCamp>(camp)
                             : TrainingCamp::None;
            state.camp_cost = column<std::int64_t>(stmt, 2);
            state.camp_applied = column<int>(stmt, 3) != 0;
            state.tour_matches = column<std::uint8_t>(stmt, 4);
            state.tour_reputation = column<int>(stmt, 5) != 0;
            if (const std::string end = columnText(stmt, 6); !end.empty())
              state.camp_end = GameDateValue::fromString(end);
          });
  forEach(*db_conn, "SELECT match_date FROM PreseasonTours ORDER BY rowid;",
          [this](sqlite3_stmt* stmt)
          {
            state.tour_dates.push_back(
                GameDateValue::fromString(columnText(stmt, 0)));
          });
}

void PreseasonPlanner::save(
    const std::shared_ptr<DatabaseConnection>& db_conn) const
{
  using namespace SqliteRows;
  const DatabaseConnection& db = *db_conn;
  clearTable(db, "PreseasonPlan");
  clearTable(db, "PreseasonTours");
  sqlite3_stmt* stmt = db.prepareStatement(
      "INSERT INTO PreseasonPlan (id, season_year, camp, camp_cost, "
      "camp_applied, tour_matches, tour_reputation, camp_end) VALUES (1, ?, "
      "?, ?, ?, ?, ?, ?);");
  sqlite3_bind_int(stmt, 1, state.season_year);
  sqlite3_bind_int(stmt, 2, static_cast<int>(std::to_underlying(state.camp)));
  sqlite3_bind_int64(stmt, 3, state.camp_cost);
  sqlite3_bind_int(stmt, 4, state.camp_applied ? 1 : 0);
  sqlite3_bind_int(stmt, 5, state.tour_matches);
  sqlite3_bind_int(stmt, 6, state.tour_reputation ? 1 : 0);
  const std::string camp_end = state.camp == TrainingCamp::None
                                   ? std::string()
                                   : state.camp_end.toString();
  bindText(stmt, 7, camp_end);
  db.executeStep(stmt);
  sqlite3_finalize(stmt);
  insertAll(db, "INSERT INTO PreseasonTours (match_date) VALUES (?);",
            state.tour_dates, [](sqlite3_stmt* row, const GameDateValue& date)
            { bindText(row, 1, date.toString()); });
}
