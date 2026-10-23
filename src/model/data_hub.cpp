// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/data_hub.h"

#include <algorithm>
#include <map>
#include <nlohmann/json.hpp>
#include <numeric>
#include <unordered_map>
#include <utility>

#include "model/match_engine.h"
#include "model/world_rng.h"

namespace
{
using nlohmann::json;

constexpr std::array<const char*, HUB_METRIC_COUNT> METRIC_KEYS = {
    "HUB_METRIC_GOALS_FOR",       "HUB_METRIC_GOALS_AGAINST",
    "HUB_METRIC_XG_FOR",          "HUB_METRIC_XG_AGAINST",
    "HUB_METRIC_SHOTS_FOR",       "HUB_METRIC_SHOTS_AGAINST",
    "HUB_METRIC_PASS_COMPLETION", "HUB_METRIC_FINISHING",
    "HUB_METRIC_GOALS_PREVENTED", "HUB_METRIC_SET_PIECE_SHARE"};

constexpr std::array<const char*, HUB_METRIC_COUNT> METRIC_HELP_KEYS = {
    "HUB_HELP_GOALS_FOR",       "HUB_HELP_GOALS_AGAINST",
    "HUB_HELP_XG_FOR",          "HUB_HELP_XG_AGAINST",
    "HUB_HELP_SHOTS_FOR",       "HUB_HELP_SHOTS_AGAINST",
    "HUB_HELP_PASS_COMPLETION", "HUB_HELP_FINISHING",
    "HUB_HELP_GOALS_PREVENTED", "HUB_HELP_SET_PIECE_SHARE"};

constexpr std::array<const char*, LEADER_METRIC_COUNT> LEADER_KEYS = {
    "HUB_LEADER_XG", "HUB_LEADER_XA", "HUB_LEADER_PROGRESSIVE",
    "HUB_LEADER_PRESSURES"};

/** Bit flags of a persisted shot (ninth element of its JSON array). */
constexpr int SHOT_HEADER = 1;
constexpr int SHOT_SET_PIECE = 2;
constexpr int SHOT_PENALTY = 4;

/** Per-match totals of one side, the raw material of every metric. */
struct SideTotals
{
  float goals_for = 0.0f;
  float goals_against = 0.0f;
  float xg_for = 0.0f;
  float xg_against = 0.0f;
  float shots_for = 0.0f;
  float shots_against = 0.0f;
  float passes_attempted = 0.0f;
  float passes_completed = 0.0f;
  float on_target_against = 0.0f;
  // Reports that know their set-piece shots only.
  float set_piece_shots = 0.0f;
  float set_piece_base = 0.0f;
  int matches = 0;

  void add(const MatchReport& report, bool home)
  {
    const TeamMatchStats& own = home ? report.home_stats : report.away_stats;
    const TeamMatchStats& other = home ? report.away_stats : report.home_stats;
    goals_for += home ? report.home_goals : report.away_goals;
    goals_against += home ? report.away_goals : report.home_goals;
    xg_for += own.expected_goals;
    xg_against += other.expected_goals;
    shots_for += own.shots;
    shots_against += other.shots;
    passes_attempted += own.passes_attempted;
    passes_completed += own.passes_completed;
    on_target_against += other.shots_on_target;
    if (own.set_pieces_known)
    {
      set_piece_shots += own.set_piece_shots;
      set_piece_base += own.shots;
    }
    ++matches;
  }

  /** @p conversion: league goals per shot on target. */
  float value(HubMetric metric, float conversion) const
  {
    const auto n = static_cast<float>(std::max(matches, 1));
    using enum HubMetric;
    switch (metric)
    {
      case GoalsFor:
        return goals_for / n;
      case GoalsAgainst:
        return goals_against / n;
      case XgFor:
        return xg_for / n;
      case XgAgainst:
        return xg_against / n;
      case ShotsFor:
        return shots_for / n;
      case ShotsAgainst:
        return shots_against / n;
      case PassCompletion:
        return passes_attempted > 0.0f
                   ? 100.0f * passes_completed / passes_attempted
                   : 0.0f;
      case Finishing:
        return (goals_for - xg_for) / n;
      case GoalsPrevented:
        return (on_target_against * conversion - goals_against) / n;
      case SetPieceShare:
        return set_piece_base > 0.0f ? 100.0f * set_piece_shots / set_piece_base
                                     : 0.0f;
      case COUNT:
        break;
    }
    return 0.0f;
  }
};

bool lowerIsBetter(HubMetric metric)
{
  using enum HubMetric;
  return metric == GoalsAgainst || metric == XgAgainst ||
         metric == ShotsAgainst;
}

json shotToJson(const ShotRecord& shot)
{
  const int flags = (shot.header ? SHOT_HEADER : 0) |
                    (shot.set_piece ? SHOT_SET_PIECE : 0) |
                    (shot.penalty ? SHOT_PENALTY : 0);
  return json::array({shot.minute, shot.period, shot.home ? 1 : 0, shot.player,
                      shot.x, shot.y, shot.xg, std::to_underlying(shot.outcome),
                      flags});
}

std::optional<ShotRecord> shotFromJson(const json& value)
{
  // Eight elements in saves from before the shot flags.
  if (!value.is_array() || value.size() < 8) return std::nullopt;
  ShotRecord shot;
  shot.minute = value[0].get<float>();
  shot.period = value[1].get<std::uint8_t>();
  shot.home = value[2].get<int>() != 0;
  shot.player = value[3].get<PlayerID>();
  shot.x = std::clamp(value[4].get<float>(), 0.0f, 1.0f);
  shot.y = std::clamp(value[5].get<float>(), 0.0f, 1.0f);
  shot.xg = std::clamp(value[6].get<float>(), 0.0f, 1.0f);
  const int outcome = value[7].get<int>();
  if (outcome < 0 || outcome > std::to_underlying(ShotOutcome::Woodwork))
    return std::nullopt;
  shot.outcome = static_cast<ShotOutcome>(outcome);
  if (value.size() > 8)
  {
    const int flags = value[8].get<int>();
    shot.header = (flags & SHOT_HEADER) != 0;
    shot.set_piece = (flags & SHOT_SET_PIECE) != 0;
    shot.penalty = (flags & SHOT_PENALTY) != 0;
  }
  return shot;
}

template <typename T>
json pairToJson(const std::array<T, 2>& values)
{
  return json::array({values[0], values[1]});
}

template <typename T>
void pairFromJson(const json& object, const char* key, std::array<T, 2>& out)
{
  const auto found = object.find(key);
  if (found == object.end() || !found->is_array() || found->size() != 2) return;
  out[0] = (*found)[0].get<T>();
  out[1] = (*found)[1].get<T>();
}
}  // namespace

std::string ManagedMatchSnapshot::toJson() const
{
  json shots_json = json::array();
  for (const ShotRecord& shot : shots) shots_json.push_back(shotToJson(shot));
  json players_json = json::array();
  for (const PlayerMatchSnapshot& line : players)
  {
    players_json.push_back(
        json::array({line.player, line.minutes, line.passes_attempted,
                     line.passes_completed, line.key_passes, line.shots,
                     line.xg, line.tackles_won, line.interceptions}));
  }
  const json object = {{"date", dateToInt(date)},
                       {"home", home_id},
                       {"away", away_id},
                       {"managed_home", managed_home},
                       {"set_piece_shots", pairToJson(set_piece_shots)},
                       {"set_piece_goals", pairToJson(set_piece_goals)},
                       {"headed_shots", pairToJson(headed_shots)},
                       {"corners", pairToJson(corners)},
                       {"progressive_passes", pairToJson(progressive_passes)},
                       {"passes_completed", pairToJson(passes_completed)},
                       {"shots", std::move(shots_json)},
                       {"players", std::move(players_json)}};
  return object.dump();
}

std::optional<ManagedMatchSnapshot> ManagedMatchSnapshot::fromJson(
    const std::string& text)
{
  const json object = json::parse(text, nullptr, false);
  if (!object.is_object()) return std::nullopt;
  try
  {
    ManagedMatchSnapshot snapshot;
    snapshot.date = dateFromInt(object.at("date").get<int>());
    snapshot.home_id = object.at("home").get<TeamID>();
    snapshot.away_id = object.at("away").get<TeamID>();
    snapshot.managed_home = object.at("managed_home").get<bool>();
    pairFromJson(object, "set_piece_shots", snapshot.set_piece_shots);
    pairFromJson(object, "set_piece_goals", snapshot.set_piece_goals);
    pairFromJson(object, "headed_shots", snapshot.headed_shots);
    pairFromJson(object, "corners", snapshot.corners);
    pairFromJson(object, "progressive_passes", snapshot.progressive_passes);
    pairFromJson(object, "passes_completed", snapshot.passes_completed);
    for (const json& value : object.value("shots", json::array()))
    {
      if (auto shot = shotFromJson(value)) snapshot.shots.push_back(*shot);
    }
    for (const json& value : object.value("players", json::array()))
    {
      if (!value.is_array() || value.size() != 9) continue;
      PlayerMatchSnapshot line;
      line.player = value[0].get<PlayerID>();
      line.minutes = value[1].get<std::uint8_t>();
      line.passes_attempted = value[2].get<std::uint16_t>();
      line.passes_completed = value[3].get<std::uint16_t>();
      line.key_passes = value[4].get<std::uint16_t>();
      line.shots = value[5].get<std::uint16_t>();
      line.xg = value[6].get<float>();
      line.tackles_won = value[7].get<std::uint16_t>();
      line.interceptions = value[8].get<std::uint16_t>();
      snapshot.players.push_back(line);
    }
    return snapshot;
  }
  catch (const json::exception&)
  {
    return std::nullopt;
  }
}

ManagedMatchSnapshot captureSnapshot(const MatchEngine& engine,
                                     const GameDateValue& date, TeamID home_id,
                                     TeamID away_id, bool managed_home)
{
  ManagedMatchSnapshot snapshot;
  snapshot.date = date;
  snapshot.home_id = home_id;
  snapshot.away_id = away_id;
  snapshot.managed_home = managed_home;
  const MatchStats& stats = engine.getStats();
  const auto sides = [managed_home](int home_value, int away_value)
  {
    const auto home = static_cast<std::uint16_t>(std::max(home_value, 0));
    const auto away = static_cast<std::uint16_t>(std::max(away_value, 0));
    return managed_home ? std::array{home, away} : std::array{away, home};
  };
  snapshot.set_piece_shots =
      sides(stats.homeSetPieceShots, stats.awaySetPieceShots);
  snapshot.set_piece_goals =
      sides(stats.homeSetPieceGoals, stats.awaySetPieceGoals);
  snapshot.headed_shots = sides(stats.homeHeadedShots, stats.awayHeadedShots);
  snapshot.corners = sides(stats.homeCorners, stats.awayCorners);
  snapshot.progressive_passes =
      sides(stats.homeProgressivePasses, stats.awayProgressivePasses);
  snapshot.passes_completed =
      sides(stats.homePassesCompleted, stats.awayPassesCompleted);
  snapshot.shots = extractShots(engine.getEvents());
  // The tracker saw the same shots in the same order as the SHOT events.
  if (const std::vector<TrackedShot>& kinds = engine.getTracker().shots();
      kinds.size() == snapshot.shots.size())
  {
    for (std::size_t index = 0; index < kinds.size(); ++index)
    {
      snapshot.shots[index].set_piece = kinds[index].set_piece;
      snapshot.shots[index].penalty = kinds[index].penalty;
    }
  }
  if (engine.getTracker().isEnabled()) snapshot.detail = captureDetail(engine);
  for (const PlayerMatchStats& line : engine.getPlayerStats())
  {
    if (line.isHomeTeam != managed_home || line.minutesPlayed <= 0.0f) continue;
    PlayerMatchSnapshot entry;
    entry.player = line.playerId;
    entry.minutes = static_cast<std::uint8_t>(
        std::clamp(line.minutesPlayed + 0.5f, 0.0f, 255.0f));
    entry.passes_attempted = static_cast<std::uint16_t>(line.passesAttempted);
    entry.passes_completed = static_cast<std::uint16_t>(line.passesCompleted);
    entry.key_passes = static_cast<std::uint16_t>(line.keyPasses);
    entry.shots = static_cast<std::uint16_t>(line.shots);
    entry.xg = line.expectedGoals;
    entry.tackles_won = static_cast<std::uint16_t>(line.tacklesWon);
    entry.interceptions = static_cast<std::uint16_t>(line.interceptions);
    snapshot.players.push_back(entry);
  }
  return snapshot;
}

bool TeamAnalytics::hasEnoughMatches() const
{
  return static_cast<int>(trend.size()) >= DataHub::MIN_MATCHES;
}

float PlayerAnalyticsRow::per90(float value, int minutes)
{
  return minutes > 0 ? value * 90.0f / static_cast<float>(minutes) : 0.0f;
}

bool DataHub::hasStatistics(const MatchReport& report)
{
  return report.home_stats.shots + report.away_stats.shots +
             report.home_stats.passes_attempted +
             report.away_stats.passes_attempted >
         0;
}

TeamAnalytics DataHub::buildTeamAnalytics(const DataHubInput& input)
{
  TeamAnalytics analytics;
  std::vector<const MatchReport*> reports;
  for (const MatchReport& report : input.team_reports)
  {
    const bool involved = report.home_team_id == input.team_id ||
                          report.away_team_id == input.team_id;
    if (involved && hasStatistics(report)) reports.push_back(&report);
  }
  std::ranges::sort(reports, {}, [](const MatchReport* report)
                    { return dayOrdinal(report->date); });
  for (const MatchReport* report : reports)
  {
    const bool home = report->home_team_id == input.team_id;
    const TeamMatchStats& own = home ? report->home_stats : report->away_stats;
    const TeamMatchStats& other =
        home ? report->away_stats : report->home_stats;
    analytics.trend.emplace_back(
        report->date, home ? report->away_team_id : report->home_team_id, home,
        home ? report->home_goals : report->away_goals,
        home ? report->away_goals : report->home_goals, own.expected_goals,
        other.expected_goals, own.shots, other.shots);
  }
  for (std::size_t index = 0; index < analytics.trend.size(); ++index)
  {
    const std::size_t first =
        index + 1 >= ROLLING_WINDOW ? index + 1 - ROLLING_WINDOW : 0;
    float xg_for = 0.0f;
    float xg_against = 0.0f;
    for (std::size_t point = first; point <= index; ++point)
    {
      xg_for += analytics.trend[point].xg_for;
      xg_against += analytics.trend[point].xg_against;
    }
    const auto count = static_cast<float>(index - first + 1);
    analytics.rolling_xg_for.push_back(xg_for / count);
    analytics.rolling_xg_against.push_back(xg_against / count);
  }
  float finishing = 0.0f;
  float prevention = 0.0f;
  for (const TeamTrendPoint& point : analytics.trend)
  {
    finishing += static_cast<float>(point.goals_for) - point.xg_for;
    prevention += point.xg_against - static_cast<float>(point.goals_against);
    analytics.cumulative_finishing.push_back(finishing);
    analytics.cumulative_prevention.push_back(prevention);
  }

  // League averages per team and match, and the club's rank among the
  // league's clubs on each metric.
  std::map<TeamID, SideTotals> by_team;
  SideTotals league;
  float league_goals = 0.0f;
  float league_on_target = 0.0f;
  for (const MatchReport& report : input.league_reports)
  {
    if (report.match_type != MatchType::LEAGUE || !hasStatistics(report))
      continue;
    ++analytics.league_matches;
    league_goals += static_cast<float>(report.home_goals + report.away_goals);
    league_on_target += static_cast<float>(report.home_stats.shots_on_target +
                                           report.away_stats.shots_on_target);
    by_team[report.home_team_id].add(report, true);
    by_team[report.away_team_id].add(report, false);
    league.add(report, true);
    league.add(report, false);
  }
  SideTotals team;
  for (const MatchReport* report : reports)
  {
    if (report->match_type == MatchType::LEAGUE)
      team.add(*report, report->home_team_id == input.team_id);
  }
  analytics.team_league_matches = team.matches;
  const float conversion =
      league_on_target > 0.0f ? league_goals / league_on_target : 0.0f;
  analytics.league_conversion = conversion;
  const bool leagueTeamFound = by_team.contains(input.team_id);
  for (std::size_t index = 0; index < HUB_METRIC_COUNT; ++index)
  {
    const auto metric = static_cast<HubMetric>(index);
    MetricComparison& comparison = analytics.metrics[index];
    comparison.lower_is_better = lowerIsBetter(metric);
    comparison.team = team.value(metric, conversion);
    comparison.league = league.value(metric, conversion);
    if (!leagueTeamFound) continue;
    const float own = by_team.at(input.team_id).value(metric, conversion);
    int better = 0;
    for (const auto& [team_id, totals] : by_team)
    {
      const float other = totals.value(metric, conversion);
      if (comparison.lower_is_better ? other < own : other > own) ++better;
    }
    comparison.rank = better + 1;
    comparison.ranked_teams = static_cast<int>(by_team.size());
  }

  for (const ManagedMatchSnapshot& snapshot : input.snapshots)
  {
    ++analytics.tracked_matches;
    SetPieceSummary& set = analytics.set_pieces;
    ++set.matches;
    set.shots_for += snapshot.set_piece_shots[0];
    set.goals_for += snapshot.set_piece_goals[0];
    set.shots_against += snapshot.set_piece_shots[1];
    set.goals_against += snapshot.set_piece_goals[1];
    set.corners_for += snapshot.corners[0];
    for (const ShotRecord& shot : snapshot.shots)
    {
      const bool own = shot.home == snapshot.managed_home;
      if (own) ++set.all_shots_for;
      (own ? analytics.shots_for : analytics.shots_against).push_back(shot);
    }
  }
  return analytics;
}

std::vector<PlayerAnalyticsRow> DataHub::buildPlayerAnalytics(
    const DataHubInput& input)
{
  std::unordered_map<PlayerID, std::size_t> index_of;
  std::vector<PlayerAnalyticsRow> rows;
  const auto rowFor = [&](PlayerID player) -> PlayerAnalyticsRow&
  {
    const auto [found, inserted] = index_of.try_emplace(player, rows.size());
    if (inserted)
    {
      PlayerAnalyticsRow& row = rows.emplace_back();
      row.player = player;
    }
    return rows[found->second];
  };

  std::vector<const MatchReport*> reports;
  for (const MatchReport& report : input.team_reports)
    reports.push_back(&report);
  std::ranges::sort(reports, {}, [](const MatchReport* report)
                    { return dayOrdinal(report->date); });
  for (const MatchReport* report : reports)
  {
    for (const PlayerMatchLine& line : report->players)
    {
      if (line.team_id != input.team_id || line.minutes == 0) continue;
      PlayerAnalyticsRow& row = rowFor(line.player_id);
      ++row.appearances;
      row.minutes += line.minutes;
      row.goals += line.goals;
      row.assists += line.assists;
      if (line.rating > 0.0f)
      {
        ++row.rated_matches;
        row.average_rating += line.rating;
        row.ratings.push_back(line.rating);
      }
    }
  }
  for (PlayerAnalyticsRow& row : rows)
  {
    if (row.rated_matches == 0) continue;
    row.average_rating /= static_cast<float>(row.rated_matches);
    const std::size_t recent = std::min<std::size_t>(3, row.ratings.size());
    const float recent_mean =
        std::accumulate(row.ratings.end() - static_cast<std::ptrdiff_t>(recent),
                        row.ratings.end(), 0.0f) /
        static_cast<float>(recent);
    row.rating_trend =
        row.ratings.size() > recent ? recent_mean - row.average_rating : 0.0f;
    if (row.ratings.size() > RATING_HISTORY)
      row.ratings.erase(
          row.ratings.begin(),
          row.ratings.end() - static_cast<std::ptrdiff_t>(RATING_HISTORY));
  }

  // Tracked matches: per-player passing and shooting. The pass share is
  // the player's completed passes over the team's in the same matches.
  std::unordered_map<PlayerID, int> team_completed;
  for (const ManagedMatchSnapshot& snapshot : input.snapshots)
  {
    int completed = 0;
    for (const PlayerMatchSnapshot& line : snapshot.players)
      completed += line.passes_completed;
    for (const PlayerMatchSnapshot& line : snapshot.players)
    {
      PlayerAnalyticsRow& row = rowFor(line.player);
      row.tracked_minutes += line.minutes;
      row.shots += line.shots;
      row.xg += line.xg;
      row.passes_attempted += line.passes_attempted;
      row.passes_completed += line.passes_completed;
      row.key_passes += line.key_passes;
      team_completed[line.player] += completed;
    }
    const MatchDetail& detail = snapshot.detail;
    if (detail.empty()) continue;
    for (const PlayerMatchSnapshot& line : snapshot.players)
    {
      const auto index = detail.indexOf(line.player);
      if (!index.has_value()) continue;
      const DetailPlayer& tracked = detail.players[*index];
      PlayerAnalyticsRow& row = rowFor(line.player);
      row.detail_minutes += line.minutes;
      row.xa += tracked.expected_assists;
      row.progressive_passes += tracked.progressive_passes;
      row.pressures += tracked.pressures;
      row.touches += tracked.touches;
    }
  }
  for (PlayerAnalyticsRow& row : rows)
  {
    const auto found = team_completed.find(row.player);
    if (found != team_completed.end() && found->second > 0)
      row.pass_share = static_cast<float>(row.passes_completed) /
                       static_cast<float>(found->second);
  }
  std::erase_if(rows, [](const PlayerAnalyticsRow& row)
                { return row.appearances == 0 && row.tracked_minutes == 0; });
  std::ranges::stable_sort(rows, std::greater{}, &PlayerAnalyticsRow::minutes);
  return rows;
}

float DataHub::leaderValue(const PlayerAnalyticsRow& row, LeaderMetric metric)
{
  using enum LeaderMetric;
  switch (metric)
  {
    case ExpectedGoals:
      return row.xg;
    case ExpectedAssists:
      return row.xa;
    case ProgressivePasses:
      return static_cast<float>(row.progressive_passes);
    case Pressures:
      return static_cast<float>(row.pressures);
    case COUNT:
      break;
  }
  return 0.0f;
}

int DataHub::leaderMinutes(const PlayerAnalyticsRow& row, LeaderMetric metric)
{
  return metric == LeaderMetric::ExpectedGoals ? row.tracked_minutes
                                               : row.detail_minutes;
}

std::vector<std::size_t> DataHub::leaders(
    std::span<const PlayerAnalyticsRow> rows, LeaderMetric metric)
{
  std::vector<std::size_t> order;
  for (std::size_t index = 0; index < rows.size(); ++index)
    if (leaderMinutes(rows[index], metric) >= LEADER_MIN_MINUTES &&
        leaderValue(rows[index], metric) > 0.0f)
      order.push_back(index);
  std::ranges::stable_sort(order, std::greater{}, [&](std::size_t index)
                           { return leaderValue(rows[index], metric); });
  if (order.size() > LEADERS) order.resize(LEADERS);
  return order;
}

const char* leaderMetricKey(LeaderMetric metric)
{
  const auto index = std::to_underlying(metric);
  return index < LEADER_METRIC_COUNT ? LEADER_KEYS[index] : "";
}

const char* hubMetricKey(HubMetric metric)
{
  const auto index = std::to_underlying(metric);
  return index < HUB_METRIC_COUNT ? METRIC_KEYS[index] : "";
}

const char* hubMetricHelpKey(HubMetric metric)
{
  const auto index = std::to_underlying(metric);
  return index < HUB_METRIC_COUNT ? METRIC_HELP_KEYS[index] : "";
}
