// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "global/types.h"
#include "model/gamedate.h"
#include "model/match_analysis.h"
#include "model/match_insights.h"
#include "model/match_report.h"

class MatchEngine;

/** @brief Pass and shot involvement of a managed player in one match. */
struct PlayerMatchSnapshot
{
  PlayerID player = 0;
  std::uint8_t minutes = 0;
  std::uint16_t passes_attempted = 0;
  std::uint16_t passes_completed = 0;
  std::uint16_t key_passes = 0;
  std::uint16_t shots = 0;
  float xg = 0.0f;
  std::uint16_t tackles_won = 0;
  std::uint16_t interceptions = 0;
};

/**
 * @brief Detail of a managed match that the persisted report does not keep:
 * shot positions, set pieces and per-player passing. Captured from the live
 * engine when the result is recorded. Index 0 of the arrays is the managed
 * club, 1 the opponent.
 */
struct ManagedMatchSnapshot
{
  GameDateValue date;
  TeamID home_id = 0;
  TeamID away_id = 0;
  bool managed_home = true;
  std::array<std::uint16_t, 2> set_piece_shots{};
  std::array<std::uint16_t, 2> set_piece_goals{};
  std::array<std::uint16_t, 2> headed_shots{};
  std::array<std::uint16_t, 2> corners{};
  std::array<std::uint16_t, 2> progressive_passes{};
  std::array<std::uint16_t, 2> passes_completed{};
  std::vector<ShotRecord> shots;
  std::vector<PlayerMatchSnapshot> players; /*!< Managed side only. */
  /** Touch maps, pass network and the like of both sides (empty for
   * matches recorded before they were tracked). Stored as its own blob. */
  MatchDetail detail;

  /** Everything but the detail (which has its own compact encoding). */
  std::string toJson() const;
  /** nullopt for malformed data. */
  static std::optional<ManagedMatchSnapshot> fromJson(const std::string& json);
};

/** Captures the snapshot of a finished (or running) live match. */
ManagedMatchSnapshot captureSnapshot(const MatchEngine& engine,
                                     const GameDateValue& date, TeamID home_id,
                                     TeamID away_id, bool managed_home);

/** @brief One played match of the analysed club. */
struct TeamTrendPoint
{
  GameDateValue date;
  TeamID opponent = 0;
  bool home = true;
  int goals_for = 0;
  int goals_against = 0;
  float xg_for = 0.0f;
  float xg_against = 0.0f;
  int shots_for = 0;
  int shots_against = 0;
};

/** @brief Metrics compared with the league (per team and match). */
enum class HubMetric : std::uint8_t
{
  GoalsFor = 0,
  GoalsAgainst,
  XgFor,
  XgAgainst,
  ShotsFor,
  ShotsAgainst,
  PassCompletion,
  /** Goals minus xG per match (finishing above or below the chances). */
  Finishing,
  /** Goals a league-average keeper would have conceded from the shots on
   * target faced, minus those conceded, per match. */
  GoalsPrevented,
  /** Share of the shots that came from set pieces. */
  SetPieceShare,
  COUNT
};

inline constexpr std::size_t HUB_METRIC_COUNT =
    static_cast<std::size_t>(std::to_underlying(HubMetric::COUNT));

/** @brief The club's value against the league average and its rank. */
struct MetricComparison
{
  float team = 0.0f;
  float league = 0.0f;
  int rank = 0; /*!< 1 = best in the league (0: unranked). */
  int ranked_teams = 0;
  bool lower_is_better = false;
};

/** @brief Set-piece shots and goals in tracked (snapshot) matches. */
struct SetPieceSummary
{
  int matches = 0;
  int shots_for = 0;
  int goals_for = 0;
  int shots_against = 0;
  int goals_against = 0;
  int all_shots_for = 0; /*!< Every shot in those matches (share base). */
  int corners_for = 0;
};

/** @brief Team analytics of the data hub. */
struct TeamAnalytics
{
  /** Played matches with engine statistics, oldest first. */
  std::vector<TeamTrendPoint> trend;
  /** Rolling mean over DataHub::ROLLING_WINDOW matches, per trend point. */
  std::vector<float> rolling_xg_for;
  std::vector<float> rolling_xg_against;
  std::array<MetricComparison, HUB_METRIC_COUNT> metrics{};
  int league_matches = 0; /*!< League fixtures behind the league averages. */
  /** The club's own league matches behind its values in metrics. */
  int team_league_matches = 0;
  SetPieceSummary set_pieces;
  /** Shots of tracked matches (attacking frames). */
  std::vector<ShotRecord> shots_for;
  std::vector<ShotRecord> shots_against;
  int tracked_matches = 0;
  /** Per trend point, running totals: goals minus xG (finishing) and xG
   * against minus goals conceded (defence and goalkeeping). */
  std::vector<float> cumulative_finishing;
  std::vector<float> cumulative_prevention;
  /** League goals per shot on target behind GoalsPrevented. */
  float league_conversion = 0.0f;

  bool hasEnoughMatches() const;
};

/** @brief One player of the analysed club. */
struct PlayerAnalyticsRow
{
  PlayerID player = 0;
  int appearances = 0;
  int minutes = 0;
  int goals = 0;
  int assists = 0;
  int rated_matches = 0;
  float average_rating = 0.0f;
  /** Latest ratings, oldest first (at most DataHub::RATING_HISTORY). */
  std::vector<float> ratings;
  /** Mean of the last three ratings minus the season mean. */
  float rating_trend = 0.0f;
  // Tracked matches only (snapshots).
  int tracked_minutes = 0;
  int shots = 0;
  float xg = 0.0f;
  int passes_attempted = 0;
  int passes_completed = 0;
  int key_passes = 0;
  /** Share of the team's completed passes while tracked (network proxy). */
  float pass_share = 0.0f;
  // Tracked matches with detail only (touch maps and passing network).
  int detail_minutes = 0;
  float xa = 0.0f;
  int progressive_passes = 0;
  int pressures = 0;
  int touches = 0;

  /** Per 90 minutes; 0 when there are no minutes. */
  static float per90(float value, int minutes);
};

/** @brief Everything the data hub aggregates. */
struct DataHubInput
{
  TeamID team_id = 0;
  /** The club's reports this season (any order). */
  std::span<const MatchReport> team_reports;
  /** Reports of the club's league this season (league averages). */
  std::span<const MatchReport> league_reports;
  /** Tracked matches of the club (managed club only). */
  std::span<const ManagedMatchSnapshot> snapshots;
};

namespace DataHub
{
/** Matches needed before trends and comparisons are shown. */
inline constexpr int MIN_MATCHES = 3;
inline constexpr std::size_t ROLLING_WINDOW = 5;
inline constexpr std::size_t RATING_HISTORY = 10;

TeamAnalytics buildTeamAnalytics(const DataHubInput& input);
/** Players with at least one appearance, most minutes first. */
std::vector<PlayerAnalyticsRow> buildPlayerAnalytics(const DataHubInput& input);

/** True when a report carries engine statistics (not a bare score). */
bool hasStatistics(const MatchReport& report);
}  // namespace DataHub

/** @brief Player leaderboards of the data hub. */
enum class LeaderMetric : std::uint8_t
{
  ExpectedGoals = 0,
  ExpectedAssists,
  ProgressivePasses,
  Pressures,
  COUNT
};

inline constexpr std::size_t LEADER_METRIC_COUNT =
    static_cast<std::size_t>(std::to_underlying(LeaderMetric::COUNT));

namespace DataHub
{
inline constexpr std::size_t LEADERS = 5;
/** Minutes before a player enters a leaderboard. */
inline constexpr int LEADER_MIN_MINUTES = 90;
/** Season total of a leaderboard metric. */
float leaderValue(const PlayerAnalyticsRow& row, LeaderMetric metric);
/** Minutes behind a leaderboard metric (its per-90 base). */
int leaderMinutes(const PlayerAnalyticsRow& row, LeaderMetric metric);
/** Indices into @p rows of the top players by season total, best first. */
std::vector<std::size_t> leaders(std::span<const PlayerAnalyticsRow> rows,
                                 LeaderMetric metric);
}  // namespace DataHub

const char* leaderMetricKey(LeaderMetric metric);

/** Language key of a metric's name / definition. */
const char* hubMetricKey(HubMetric metric);
const char* hubMetricHelpKey(HubMetric metric);
