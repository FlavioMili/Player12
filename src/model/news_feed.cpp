// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/news_feed.h"

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>

#include "database/gamedata.h"
#include "global/global.h"
#include "global/number_format.h"
#include "model/awards.h"
#include "model/calendar.h"
#include "model/competition.h"
#include "model/continental.h"
#include "model/game.h"
#include "model/inbox.h"
#include "model/manager_career.h"
#include "model/records.h"
#include "model/season_history.h"
#include "model/transfer_market.h"

namespace
{
// Story weights: bigger stories lead a day's news.
constexpr std::uint32_t WEIGHT_CHAMPIONS = 900;
constexpr std::uint32_t WEIGHT_SACKING = 700;
constexpr std::uint32_t WEIGHT_RACE = 650;
constexpr std::uint32_t WEIGHT_LEADER = 600;
constexpr std::uint32_t WEIGHT_SEASON_AWARD = 550;
constexpr std::uint32_t WEIGHT_APPOINTMENT = 500;
constexpr std::uint32_t WEIGHT_RECORD = 450;
constexpr std::uint32_t WEIGHT_PROMOTION = 400;
constexpr std::uint32_t WEIGHT_UPSET = 300;
constexpr std::uint32_t WEIGHT_MONTH_AWARD = 250;
constexpr std::uint32_t WEIGHT_TRANSFER = 200;
/** Lower divisions weigh less than the top flight. */
constexpr std::uint32_t TIER_PENALTY = 60;
constexpr std::uint32_t FEE_WEIGHT_UNIT = 1'000'000;

constexpr std::array<std::string_view, 3> TRANSFER_KEYS = {
    "NEWS_TRANSFER_FEE_0", "NEWS_TRANSFER_FEE_1", "NEWS_TRANSFER_FEE_2"};
constexpr std::array<std::string_view, 2> APPOINTED_KEYS = {
    "NEWS_MANAGER_APPOINTED_0", "NEWS_MANAGER_APPOINTED_1"};
constexpr std::array<std::string_view, 2> SACKED_KEYS = {
    "NEWS_MANAGER_SACKED_0", "NEWS_MANAGER_SACKED_1"};
constexpr std::array<std::string_view, 2> LEADER_KEYS = {"NEWS_RACE_LEADER_0",
                                                         "NEWS_RACE_LEADER_1"};
constexpr std::array<std::string_view, 2> UPSET_KEYS = {"NEWS_UPSET_0",
                                                        "NEWS_UPSET_1"};
constexpr std::array<std::string_view, 2> AWARD_KEYS = {"NEWS_AWARD_0",
                                                        "NEWS_AWARD_1"};
constexpr std::string_view DISMISSAL_TITLE = "INBOX_MANAGER_NEWS_TITLE";

bool inWindow(const GameDateValue& date, const GameDateValue& from,
              const GameDateValue& to)
{
  return !(date < from) && !(to < date);
}

template <std::size_t N>
std::string pick(const std::array<std::string_view, N>& keys, NewsKind kind,
                 std::uint32_t first, std::uint32_t second,
                 const GameDateValue& date)
{
  return std::string(keys[NewsFeed::variant(kind, first, second, date, N)]);
}

/** Lookups shared by the story builders. */
class World
{
 public:
  explicit World(const GameData& data) : gamedata(data) {}

  std::string teamName(TeamID id) const
  {
    const auto team = gamedata.getTeam(id);
    return team ? team->get().getName() : std::string();
  }

  std::string playerName(PlayerID id) const
  {
    const auto player = gamedata.getPlayer(id);
    return player ? player->get().getName() : std::string();
  }

  LeagueID leagueOf(TeamID id) const
  {
    const auto team = gamedata.getTeam(id);
    return team ? team->get().getLeagueId() : LeagueID{0};
  }

  LeagueID countryOfLeague(LeagueID league_id) const
  {
    if (league_id == 0 || Continental::rules(league_id) != nullptr) return 0;
    if (const auto cached = roots.find(league_id); cached != roots.end())
      return cached->second;
    const LeagueID root = gamedata.getLeague(league_id)
                              ? Competitions::rootLeague(gamedata, league_id)
                              : LeagueID{0};
    roots.emplace(league_id, root);
    return root;
  }

  std::uint8_t tier(LeagueID league_id) const
  {
    return gamedata.getLeague(league_id)
               ? Competitions::leagueTier(gamedata, league_id)
               : std::uint8_t{1};
  }

  int reputation(TeamID id) const
  {
    const auto team = gamedata.getTeam(id);
    return team ? team->get().getReputation() : 0;
  }

  /** Name argument of a competition ("@KEY" when it is translated). */
  std::string competitionArg(const NewsCompetition& competition) const
  {
    if (competition.type == MatchType::CONTINENTAL)
    {
      const auto* rules = Continental::rules(competition.id);
      return rules != nullptr ? std::string("@") + rules->name_key
                              : std::string();
    }
    if (competition.type == MatchType::CUP)
      return Competitions::cupName(gamedata, competition.id);
    const auto league = gamedata.getLeague(competition.id);
    return league ? Competitions::leagueNameArg(league->get()) : std::string();
  }

  std::uint32_t tierWeight(std::uint32_t base, LeagueID league_id) const
  {
    const std::uint32_t penalty = TIER_PENALTY * (tier(league_id) - 1U);
    return base > penalty ? base - penalty : 1U;
  }

 private:
  const GameData& gamedata;
  mutable std::unordered_map<LeagueID, LeagueID> roots;
};

NewsItem story(NewsKind kind, const GameDateValue& date, std::string key,
               std::vector<std::string> args)
{
  NewsItem item;
  item.kind = kind;
  item.date = date;
  item.headline_key = std::move(key);
  item.args = std::move(args);
  return item;
}

// ---- Transfers -------------------------------------------------------------

void addTransfers(const NewsSources& sources, const World& world,
                  const GameDateValue& from, const GameDateValue& to,
                  std::vector<NewsItem>& out)
{
  using enum TransferKind;
  using enum NewsKind;
  for (const TransferRecord& record : sources.transfers)
  {
    if (!inWindow(record.date, from, to)) continue;
    const bool ours =
        sources.managed_team != 0 && (record.to_team == sources.managed_team ||
                                      record.from_team == sources.managed_team);
    const bool fee_move =
        record.kind == Permanent || record.kind == PreContract;
    std::string key;
    if (fee_move && record.fee >= NewsFeed::MAJOR_FEE)
      key = pick(TRANSFER_KEYS, Transfer, record.player_id, record.to_team,
                 record.date);
    else if (!ours)
      continue;
    else if (record.kind == Permanent && record.fee > 0)
      key = pick(TRANSFER_KEYS, Transfer, record.player_id, record.to_team,
                 record.date);
    else if (record.kind == Free ||
             (record.kind == Permanent && record.fee == 0))
      key = "NEWS_TRANSFER_FREE";
    else if (record.kind == Loan)
      key = "NEWS_TRANSFER_LOAN";
    else if (record.kind == PreContract)
      key = "NEWS_TRANSFER_PRECONTRACT";
    else
      continue;  // Loan returns and releases are not news.
    const std::string player = world.playerName(record.player_id);
    const std::string buyer = world.teamName(record.to_team);
    if (player.empty() || buyer.empty()) continue;
    std::string seller = world.teamName(record.from_team);
    NewsItem item = story(Transfer, record.date, std::move(key),
                          {player, buyer, seller, formatMoney(record.fee)});
    item.player_id = record.player_id;
    item.team_id = record.to_team;
    item.other_team_id = seller.empty() ? TeamID{0} : record.from_team;
    const LeagueID league = world.leagueOf(record.to_team);
    item.country = world.countryOfLeague(league);
    item.competition = {MatchType::LEAGUE, league};
    item.weight = world.tierWeight(WEIGHT_TRANSFER, league) +
                  record.fee / FEE_WEIGHT_UNIT;
    out.push_back(std::move(item));
  }
}

// ---- Managers --------------------------------------------------------------

void addManagerStory(const World& world, NewsKind kind,
                     const GameDateValue& date, std::string key,
                     const std::string& name, TeamID team_id,
                     std::uint32_t weight, std::vector<NewsItem>& out)
{
  const std::string club = world.teamName(team_id);
  if (club.empty() || name.empty()) return;
  NewsItem item = story(kind, date, std::move(key), {name, club});
  item.team_id = team_id;
  const LeagueID league = world.leagueOf(team_id);
  item.country = world.countryOfLeague(league);
  item.competition = {MatchType::LEAGUE, league};
  item.weight = world.tierWeight(weight, league);
  out.push_back(std::move(item));
}

void addManagers(const NewsSources& sources, const World& world,
                 const GameDateValue& from, const GameDateValue& to,
                 std::vector<NewsItem>& out)
{
  using enum DepartureReason;
  using enum NewsKind;
  // The human manager's own spells.
  for (const ManagerStint& stint : sources.stints)
  {
    if (inWindow(stint.start, from, to))
      addManagerStory(
          world, NewsKind::Manager, stint.start,
          pick(APPOINTED_KEYS, Manager, stint.team_id, 1, stint.start),
          sources.manager_name, stint.team_id, WEIGHT_APPOINTMENT, out);
    if (stint.reason == Current || stint.reason == Moved ||
        !inWindow(stint.end, from, to))
      continue;
    std::string key;
    if (stint.reason == Sacked)
      key = pick(SACKED_KEYS, Manager, stint.team_id, 2, stint.end);
    else if (stint.reason == Resigned)
      key = "NEWS_MANAGER_RESIGNED";
    else
      key = "NEWS_MANAGER_LEFT";
    addManagerStory(world, Manager, stint.end, std::move(key),
                    sources.manager_name, stint.team_id, WEIGHT_SACKING, out);
  }

  // Dismissals the inbox reported (they name the departed manager).
  std::vector<std::pair<TeamID, GameDateValue>> reported;
  for (const InboxMessage& message : sources.inbox)
  {
    if (message.title_key != DISMISSAL_TITLE || !message.team_id.has_value() ||
        message.args.empty() || !inWindow(message.date, from, to))
      continue;
    reported.emplace_back(*message.team_id, message.date);
    addManagerStory(
        world, Manager, message.date,
        pick(SACKED_KEYS, Manager, *message.team_id, 3, message.date),
        message.args[0], *message.team_id, WEIGHT_SACKING, out);
  }

  // Open jobs the inbox did not report: the club is looking for someone.
  for (const Vacancy& vacancy : sources.vacancies)
  {
    if (!inWindow(vacancy.opened, from, to) ||
        vacancy.team_id == sources.managed_team ||
        std::ranges::contains(reported,
                              std::pair{vacancy.team_id, vacancy.opened}))
      continue;
    const std::string club = world.teamName(vacancy.team_id);
    if (club.empty()) continue;
    NewsItem item =
        story(Manager, vacancy.opened, "NEWS_MANAGER_VACANCY", {club});
    item.team_id = vacancy.team_id;
    const LeagueID league = world.leagueOf(vacancy.team_id);
    item.country = world.countryOfLeague(league);
    item.competition = {MatchType::LEAGUE, league};
    item.weight = world.tierWeight(WEIGHT_SACKING, league);
    out.push_back(std::move(item));
  }

  // Computer managers appointed during the save.
  for (const AiManager& manager : sources.managers)
  {
    if (manager.team_id == 0 || !inWindow(manager.appointed, from, to))
      continue;
    addManagerStory(world, Manager, manager.appointed,
                    pick(APPOINTED_KEYS, Manager, manager.team_id, manager.id,
                         manager.appointed),
                    manager.name(), manager.team_id, WEIGHT_APPOINTMENT, out);
  }
}

// ---- Results: upsets, leaders, title races ---------------------------------

std::string scoreline(int winner_goals, int loser_goals)
{
  return std::format("{}-{}", winner_goals, loser_goals);
}

void addUpsets(const NewsSources& sources, const World& world,
               const GameDateValue& from, const GameDateValue& to,
               std::vector<NewsItem>& out)
{
  // The biggest upset of each competition and day.
  std::map<std::tuple<GameDateValue, MatchType, LeagueID>,
           std::pair<int, const Match*>>
      best;
  for (const auto& [date, matches] : sources.calendar->getFullCalendar())
  {
    if (!inWindow(date, from, to)) continue;
    for (const Match& match : matches)
    {
      if (!match.isPlayed() || match.getMatchType() == MatchType::FRIENDLY)
        continue;
      const std::optional<TeamID> winner = match.getWinnerId();
      if (!winner.has_value()) continue;
      const TeamID loser = *winner == match.getHomeTeamId()
                               ? match.getAwayTeamId()
                               : match.getHomeTeamId();
      const int gap = world.reputation(loser) - world.reputation(*winner);
      if (gap < NewsFeed::UPSET_REPUTATION_GAP) continue;
      auto& slot = best[{date, match.getMatchType(), match.getCompetitionId()}];
      if (slot.second == nullptr || gap > slot.first) slot = {gap, &match};
    }
  }
  for (const auto& [key, entry] : best)
  {
    const Match& match = *entry.second;
    const TeamID winner = *match.getWinnerId();
    const bool home_won = winner == match.getHomeTeamId();
    const TeamID loser =
        home_won ? match.getAwayTeamId() : match.getHomeTeamId();
    const NewsCompetition competition{match.getMatchType(),
                                      match.getCompetitionId()};
    const int winner_goals =
        home_won ? match.getHomeScore() : match.getAwayScore();
    const int loser_goals =
        home_won ? match.getAwayScore() : match.getHomeScore();
    std::string headline_key =
        match.getMatchType() == MatchType::CUP && match.isKnockout()
            ? std::string("NEWS_UPSET_CUP")
            : pick(UPSET_KEYS, NewsKind::Upset, winner, loser, match.getDate());
    NewsItem item =
        story(NewsKind::Upset, match.getDate(), std::move(headline_key),
              {world.teamName(winner), world.teamName(loser),
               scoreline(winner_goals, loser_goals),
               world.competitionArg(competition)});
    item.team_id = winner;
    item.other_team_id = loser;
    item.competition = competition;
    item.country = match.getMatchType() == MatchType::CONTINENTAL
                       ? LeagueID{0}
                       : world.countryOfLeague(match.getCompetitionId());
    item.has_match = true;
    item.match_date = match.getDate();
    item.home_id = match.getHomeTeamId();
    item.away_id = match.getAwayTeamId();
    item.weight = WEIGHT_UPSET + static_cast<std::uint32_t>(entry.first);
    out.push_back(std::move(item));
  }
}

struct TableRow
{
  int points = 0;
  int played = 0;
};

/** Strict leader on points (0 when the top is shared). */
std::pair<TeamID, TeamID> topTwo(const std::map<TeamID, TableRow>& table)
{
  TeamID first = 0;
  TeamID second = 0;
  for (const auto& [team, row] : table)
  {
    if (first == 0 || row.points > table.at(first).points)
    {
      second = first;
      first = team;
    }
    else if (second == 0 || row.points > table.at(second).points)
    {
      second = team;
    }
  }
  return {first, second};
}

void addRaces(const NewsSources& sources, const World& world,
              const GameDateValue& from, const GameDateValue& to,
              std::vector<NewsItem>& out)
{
  // Points of every league after each matchday, in date order.
  std::map<LeagueID, std::vector<const Match*>> by_league;
  for (const auto& [date, matches] : sources.calendar->getFullCalendar())
  {
    if (to < date) break;
    for (const Match& match : matches)
      if (match.isPlayed() && match.getMatchType() == MatchType::LEAGUE)
        by_league[match.getCompetitionId()].push_back(&match);
  }
  for (const auto& [league_id, matches] : by_league)
  {
    const auto league = sources.gamedata->getLeague(league_id);
    if (!league) continue;
    const std::size_t clubs = league->get().getTeamIDs().size();
    if (clubs < 2) continue;
    const auto season_games = static_cast<int>((clubs - 1) * 2);
    std::map<TeamID, TableRow> table;
    for (const TeamID team : league->get().getTeamIDs()) table[team];
    const NewsCompetition competition{MatchType::LEAGUE, league_id};
    const std::string league_arg = world.competitionArg(competition);
    const LeagueID country = world.countryOfLeague(league_id);
    TeamID last_leader = 0;
    for (std::size_t index = 0; index < matches.size(); ++index)
    {
      const Match& match = *matches[index];
      TableRow& home = table[match.getHomeTeamId()];
      TableRow& away = table[match.getAwayTeamId()];
      ++home.played;
      ++away.played;
      if (match.getHomeScore() > match.getAwayScore())
        home.points += 3;
      else if (match.getHomeScore() < match.getAwayScore())
        away.points += 3;
      else
      {
        ++home.points;
        ++away.points;
      }
      if (const bool day_done =
              index + 1 == matches.size() ||
              !(matches[index + 1]->getDate() == match.getDate());
          !day_done)
        continue;
      const auto [first, second] = topTwo(table);
      if (const bool strict =
              first != 0 &&
              (second == 0 || table[first].points > table[second].points);
          !strict)
        continue;
      if (last_leader != 0 && first != last_leader &&
          table[first].played >= NewsFeed::LEADER_MIN_PLAYED &&
          inWindow(match.getDate(), from, to))
      {
        NewsItem item = story(
            NewsKind::Race, match.getDate(),
            pick(LEADER_KEYS, NewsKind::Race, first, league_id,
                 match.getDate()),
            {world.teamName(first), league_arg, world.teamName(last_leader)});
        item.team_id = first;
        item.other_team_id = last_leader;
        item.country = country;
        item.competition = competition;
        item.weight = world.tierWeight(WEIGHT_LEADER, league_id);
        out.push_back(std::move(item));
      }
      last_leader = first;
    }

    // The run-in: the top two close together with a few games to go.
    if (matches.empty() || !inWindow(matches.back()->getDate(), from, to))
      continue;
    const auto [first, second] = topTwo(table);
    if (first == 0 || second == 0) continue;
    const int games_left = season_games - table[first].played;
    const int gap = table[first].points - table[second].points;
    if (games_left <= 0 || games_left > NewsFeed::RACE_GAMES_LEFT ||
        gap > NewsFeed::RACE_POINTS_GAP)
      continue;
    // Level teams are named in ID order so the story never depends on
    // the order of the table's map.
    NewsItem item =
        story(NewsKind::Race, matches.back()->getDate(),
              gap == 0 ? "NEWS_RACE_LEVEL" : "NEWS_RACE_TIGHT",
              {world.teamName(first), world.teamName(second),
               std::to_string(gap), std::to_string(games_left), league_arg});
    item.team_id = first;
    item.other_team_id = second;
    item.country = country;
    item.competition = competition;
    item.weight = world.tierWeight(WEIGHT_RACE, league_id);
    out.push_back(std::move(item));
  }
}

// ---- Season verdicts -------------------------------------------------------

void addVerdicts(const NewsSources& sources, const World& world,
                 const GameDateValue& from, const GameDateValue& to,
                 std::vector<NewsItem>& out)
{
  for (const SeasonHistoryEntry& entry : sources.history)
  {
    const GameDateValue date(static_cast<std::uint16_t>(entry.start_year + 1U),
                             6, 30);
    if (!inWindow(date, from, to)) continue;
    const NewsCompetition competition{entry.competition_type,
                                      entry.competition_id};
    std::string name = world.competitionArg(competition);
    if (name.empty()) name = entry.competition_name;
    const LeagueID country = entry.competition_type == MatchType::CONTINENTAL
                                 ? LeagueID{0}
                                 : world.countryOfLeague(entry.competition_id);
    const bool league = entry.competition_type == MatchType::LEAGUE;
    const auto add = [&](const char* key, TeamID team, std::uint32_t weight)
    {
      const std::string club = world.teamName(team);
      if (club.empty()) return;
      NewsItem item = story(NewsKind::Race, date, key, {club, name});
      item.team_id = team;
      item.country = country;
      item.competition = competition;
      item.weight =
          league ? world.tierWeight(weight, entry.competition_id) : weight;
      out.push_back(std::move(item));
    };
    if (entry.champion_id != 0)
      add(league ? "NEWS_RACE_CHAMPIONS" : "NEWS_RACE_CUP", entry.champion_id,
          WEIGHT_CHAMPIONS);
    for (const TeamID team : entry.promoted)
      add("NEWS_RACE_PROMOTED", team, WEIGHT_PROMOTION);
    for (const TeamID team : entry.relegated)
      add("NEWS_RACE_RELEGATED", team, WEIGHT_PROMOTION);
  }
}

// ---- Records ---------------------------------------------------------------

const char* recordKey(RecordKind kind)
{
  using enum RecordKind;
  switch (kind)
  {
    case BiggestWin:
      return "NEWS_RECORD_BIGGEST_WIN";
    case HighestScoringMatch:
      return "NEWS_RECORD_HIGHEST_SCORING";
    case HighestAttendance:
      return "NEWS_RECORD_ATTENDANCE";
    case MostGoalsSeason:
      return "NEWS_RECORD_GOALS_SEASON";
    case MostPointsSeason:
      return "NEWS_RECORD_POINTS_SEASON";
    case TopScorerSeason:
      return "NEWS_RECORD_TOP_SCORER";
    case RecordSigning:
      return "NEWS_RECORD_SIGNING";
    case RecordSale:
      return "NEWS_RECORD_SALE";
    case BiggestDefeat:
    case COUNT:
      break;
  }
  return nullptr;
}

void addRecords(const NewsSources& sources, const World& world,
                const GameDateValue& from, const GameDateValue& to,
                std::vector<NewsItem>& out)
{
  using enum RecordKind;
  // In a save's first season every record is a first: none is news.
  const std::uint16_t first_season =
      sources.world_start.month >= 7
          ? sources.world_start.year
          : static_cast<std::uint16_t>(sources.world_start.year - 1U);
  for (const RecordEntry& record : sources.records)
  {
    const char* key = recordKey(record.kind);
    if (key == nullptr || record.season_year <= first_season ||
        !inWindow(record.date, from, to))
      continue;
    if (record.scope == RecordScope::Club &&
        record.scope_id != sources.managed_team)
      continue;
    const bool league_scope = record.scope == RecordScope::League;
    const NewsCompetition competition{
        MatchType::LEAGUE, league_scope ? static_cast<LeagueID>(record.scope_id)
                                        : world.leagueOf(record.team_id)};
    const std::string holder = league_scope ? world.competitionArg(competition)
                                            : world.teamName(record.team_id);
    std::string figure;
    switch (record.kind)
    {
      case BiggestWin:
        figure = scoreline(record.goals_for, record.goals_against);
        break;
      case RecordSigning:
      case RecordSale:
        figure = formatMoney(record.value);
        break;
      case HighestAttendance:
        figure = NumberFormat::grouped(record.value);
        break;
      default:
        figure = std::to_string(record.value);
        break;
    }
    const bool player_record = record.kind == TopScorerSeason ||
                               record.kind == RecordSigning ||
                               record.kind == RecordSale;
    const std::string subject =
        player_record ? record.name : world.teamName(record.team_id);
    if (subject.empty() || holder.empty()) continue;
    NewsItem item =
        story(NewsKind::Record, record.date, key,
              {subject, holder, figure, world.teamName(record.team_id),
               world.teamName(record.opponent_id)});
    item.player_id = player_record ? record.player_id : PlayerID{0};
    item.team_id = record.team_id;
    item.other_team_id = record.opponent_id;
    item.country = world.countryOfLeague(competition.id);
    item.competition = competition;
    if (const bool match_record = record.kind == BiggestWin ||
                                  record.kind == HighestScoringMatch ||
                                  record.kind == HighestAttendance;
        match_record && record.opponent_id != 0)
    {
      item.has_match = true;
      item.match_date = record.date;
      item.home_id = record.home ? record.team_id : record.opponent_id;
      item.away_id = record.home ? record.opponent_id : record.team_id;
    }
    item.weight = world.tierWeight(WEIGHT_RECORD, competition.id);
    out.push_back(std::move(item));
  }
}

// ---- Awards ----------------------------------------------------------------

GameDateValue awardDate(const AwardRecord& record)
{
  // Monthly awards are given on the 1st of the next month, season awards
  // on 1 June.
  if (record.month == 0)
    return GameDateValue(static_cast<std::uint16_t>(record.season_year + 1U), 6,
                         1);
  const std::uint16_t year =
      record.month >= 7 ? record.season_year
                        : static_cast<std::uint16_t>(record.season_year + 1U);
  return record.month == 12
             ? GameDateValue(static_cast<std::uint16_t>(year + 1U), 1, 1)
             : GameDateValue(year, static_cast<std::uint8_t>(record.month + 1U),
                             1);
}

void addAwards(const NewsSources& sources, const World& world,
               const GameDateValue& from, const GameDateValue& to,
               std::vector<NewsItem>& out)
{
  for (const AwardRecord& record : sources.awards)
  {
    // The Team of the Season is a list, and the goal is shown elsewhere.
    if (record.type == AwardType::TeamOfSeason ||
        record.type == AwardType::GoalOfMonth || record.name.empty())
      continue;
    const GameDateValue date = awardDate(record);
    if (!inWindow(date, from, to)) continue;
    const NewsCompetition competition{MatchType::LEAGUE, record.league_id};
    NewsItem item = story(NewsKind::Award, date,
                          pick(AWARD_KEYS, NewsKind::Award, record.player_id,
                               static_cast<std::uint32_t>(record.type), date),
                          {record.name, world.competitionArg(competition),
                           std::string("@") + awardTypeKey(record.type),
                           world.teamName(record.team_id)});
    item.player_id = record.player_id;
    item.team_id = record.team_id;
    item.country = world.countryOfLeague(record.league_id);
    item.competition = competition;
    item.weight = world.tierWeight(
        record.month == 0 ? WEIGHT_SEASON_AWARD : WEIGHT_MONTH_AWARD,
        record.league_id);
    out.push_back(std::move(item));
  }
}
}  // namespace

const char* newsKindKey(NewsKind kind)
{
  using enum NewsKind;
  switch (kind)
  {
    case Transfer:
      return "NEWS_KIND_TRANSFER";
    case Manager:
      return "NEWS_KIND_MANAGER";
    case Race:
      return "NEWS_KIND_RACE";
    case Upset:
      return "NEWS_KIND_UPSET";
    case Record:
      return "NEWS_KIND_RECORD";
    case Award:
      return "NEWS_KIND_AWARD";
    case COUNT:
      break;
  }
  return "NEWS_KIND_TRANSFER";
}

std::string NewsItem::headline() const
{
  return formatLocalized(headline_key, args);
}

bool NewsFilter::matches(const NewsItem& item) const
{
  return (!country.has_value() || item.country == *country) &&
         (!competition.has_value() || item.competition == *competition) &&
         (!kind.has_value() || item.kind == *kind);
}

std::size_t NewsFeed::variant(NewsKind kind, std::uint32_t first,
                              std::uint32_t second, const GameDateValue& date,
                              std::size_t count)
{
  if (count <= 1) return 0;
  const std::uint32_t day = static_cast<std::uint32_t>(date.year) * 10'000U +
                            static_cast<std::uint32_t>(date.month) * 100U +
                            date.day;
  const std::uint32_t hash = Competitions::mixSeed(
      Competitions::mixSeed(static_cast<std::uint32_t>(kind), first, second),
      day, 0x4E45'5753U);
  return hash % count;
}

std::vector<NewsItem> NewsFeed::build(const NewsSources& sources,
                                      const GameDateValue& from,
                                      const GameDateValue& to)
{
  std::vector<NewsItem> items;
  if (sources.gamedata == nullptr) return items;
  const GameDateValue start =
      from < sources.world_start ? sources.world_start : from;
  if (to < start) return items;
  const World world(*sources.gamedata);
  addTransfers(sources, world, start, to, items);
  addManagers(sources, world, start, to, items);
  if (sources.calendar != nullptr)
  {
    addUpsets(sources, world, start, to, items);
    addRaces(sources, world, start, to, items);
  }
  addVerdicts(sources, world, start, to, items);
  addRecords(sources, world, start, to, items);
  addAwards(sources, world, start, to, items);

  // Newest first; a total order so equal days never depend on the input.
  std::ranges::sort(items,
                    [](const NewsItem& left, const NewsItem& right)
                    {
                      if (!(left.date == right.date))
                        return right.date < left.date;
                      return std::tuple{right.weight,       left.kind,
                                        left.team_id,       left.player_id,
                                        left.other_team_id, left.headline_key,
                                        left.args} <
                             std::tuple{left.weight,         right.kind,
                                        right.team_id,       right.player_id,
                                        right.other_team_id, right.headline_key,
                                        right.args};
                    });
  return items;
}

std::vector<NewsItem> NewsFeed::forGame(const Game& game,
                                        const GameData& gamedata)
{
  const GameDateValue today = game.getCurrentDate();
  const std::uint16_t season_year = SeasonCalendar::seasonStartYear(today);
  const int seasons_played = std::max(1, game.getCurrentSeason());
  const RecordBook& book = game.getWorld().getRecords();
  std::vector<RecordEntry> records;
  for (const auto& [league_id, league] : gamedata.getLeagues())
  {
    std::vector<RecordEntry> league_records = book.leagueRecords(league_id);
    records.insert(records.end(), league_records.begin(), league_records.end());
  }
  const TeamID managed = game.getManagedTeamId();
  if (managed != FREE_AGENTS_TEAM_ID)
  {
    std::vector<RecordEntry> club_records = book.clubRecords(managed);
    records.insert(records.end(), club_records.begin(), club_records.end());
  }

  NewsSources sources;
  sources.gamedata = &gamedata;
  sources.calendar = &game.getCalendar();
  sources.transfers = game.getTransfers().history();
  sources.stints = game.getCareer().getStints();
  if (game.getCareer().hasProfile())
    sources.manager_name = game.getCareer().getProfile().name();
  sources.managers = game.getCareer().getAiManagers();
  sources.vacancies = game.getCareer().getVacancies();
  sources.inbox = game.getWorld().getInbox().getMessages();
  sources.awards = game.getWorld().getAwards().history();
  sources.records = records;
  sources.history = game.getCompetitions().getSeasonHistory();
  sources.managed_team = managed;
  sources.world_start = GameDateValue(
      static_cast<std::uint16_t>(season_year - (seasons_played - 1)), 7, 1);
  return build(
      sources,
      GameDateValue(static_cast<std::uint16_t>(season_year - 1U), 7, 1), today);
}

std::vector<std::size_t> NewsFeed::filter(std::span<const NewsItem> items,
                                          const NewsFilter& filter)
{
  std::vector<std::size_t> kept;
  kept.reserve(items.size());
  for (std::size_t index = 0; index < items.size(); ++index)
    if (filter.matches(items[index])) kept.push_back(index);
  return kept;
}
