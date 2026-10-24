// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/lab_season.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <map>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/global.h"
#include "global/runtime_paths.h"
#include "model/game.h"
#include "model/injury.h"
#include "model/national_teams.h"
#include "model/transfer_market.h"
#include "model/youth_academy.h"
#include "tools/lab_runner.h"

namespace Lab
{
namespace
{
class SeasonSimulationError : public std::runtime_error
{
 public:
  using std::runtime_error::runtime_error;
};

constexpr int LAB_SAVE_SLOT = 1;
constexpr int PROGRESS_EVERY_DAYS = 30;
constexpr double POTENTIAL_HIGH = 80.0;
constexpr double POTENTIAL_GOOD = 70.0;
/** Cash below this share of a season's revenue counts as insolvent. */
constexpr double INSOLVENT_REVENUE_SHARE = 0.25;
constexpr std::size_t TOP_PLAYERS = 100;

double meanOf(const std::vector<double>& values)
{
  if (values.empty()) return 0.0;
  double sum = 0.0;
  for (const double value : values) sum += value;
  return sum / static_cast<double>(values.size());
}

/** Squared correlation of two equally long samples (0 when degenerate). */
double rSquared(std::span<const double> x, std::span<const double> y)
{
  const auto n = static_cast<double>(x.size());
  double sx = 0.0;
  double sy = 0.0;
  double sxx = 0.0;
  double syy = 0.0;
  double sxy = 0.0;
  for (std::size_t i = 0; i < x.size(); ++i)
  {
    sx += x[i];
    sy += y[i];
    sxx += x[i] * x[i];
    syy += y[i] * y[i];
    sxy += x[i] * y[i];
  }
  const double cov = sxy - sx * sy / n;
  const double vx = sxx - sx * sx / n;
  const double vy = syy - sy * sy / n;
  if (x.size() < 3 || vx <= 0.0 || vy <= 0.0) return 0.0;
  return cov * cov / (vx * vy);
}

bool inRange(const GameDateValue& date, const GameDateValue& from,
             const GameDateValue& to)
{
  return !(date < from) && !(to < date);
}

bool isSeasonEnd(const GameDateValue& date)
{
  return date.month == 6 && date.day == 30;
}

std::int64_t revenueOf(const FinanceSummary& summary)
{
  const auto amount = [&](FinanceCategory category)
  {
    return std::abs(summary.by_category[static_cast<std::size_t>(
        std::to_underlying(category))]);
  };
  return amount(FinanceCategory::Matchday) +
         amount(FinanceCategory::Broadcasting) +
         amount(FinanceCategory::Sponsorship) +
         amount(FinanceCategory::PrizeMoney);
}

double populationSd(const std::vector<double>& values)
{
  if (values.size() < 2) return 0.0;
  double mean = 0.0;
  for (const double value : values) mean += value;
  mean /= static_cast<double>(values.size());
  double squares = 0.0;
  for (const double value : values) squares += (value - mean) * (value - mean);
  return std::sqrt(squares / static_cast<double>(values.size()));
}

double percentile(std::vector<double> values, double q)
{
  if (values.empty()) return std::nan("");
  std::ranges::sort(values);
  const auto index = static_cast<std::size_t>(
      std::lround(q * static_cast<double>(values.size() - 1)));
  return values[index];
}

std::vector<LeagueID> selectLeagues(const GameController& controller,
                                    std::string_view filter)
{
  std::vector<LeagueID> leagues;
  if (filter == "all" || filter == "top")
  {
    for (const auto& league : controller.getLeagues())
    {
      const LeagueID id = league.get().getId();
      if (filter == "all" || controller.getLeagueTier(id) == 1)
        leagues.push_back(id);
    }
  }
  else
  {
    std::size_t begin = 0;
    while (begin <= filter.size())
    {
      const std::size_t end = std::min(filter.find(',', begin), filter.size());
      const std::string token(filter.substr(begin, end - begin));
      int id = 0;
      try
      {
        id = std::stoi(token);
      }
      catch (const std::exception&)
      {
        throw std::invalid_argument(
            std::format("invalid league id '{}' in --leagues", token));
      }
      if (id <= 0 || id > 255 ||
          !controller.getLeagueById(static_cast<LeagueID>(id)))
        throw std::invalid_argument(std::format("unknown league id {}", id));
      leagues.push_back(static_cast<LeagueID>(id));
      begin = end + 1;
    }
  }
  std::ranges::sort(leagues);
  if (leagues.empty()) throw std::invalid_argument("no league selected");
  return leagues;
}

struct InjuryOnset
{
  InjuryType type = InjuryType::None;
  double days = 0.0;
};

/** Per-league tallies for the league-by-league table. */
struct LeagueTally
{
  std::size_t matches = 0;
  std::size_t home_wins = 0;
  std::size_t draws = 0;
  double goals = 0.0;
  double yellows = 0.0;
  double reds = 0.0;
  std::vector<double> champion_ppg;
  std::vector<double> points_sd;
  /** Clubs promoted into the league last season / relegated again now. */
  std::size_t promoted_in = 0;
  std::size_t promoted_down = 0;
  /** Clubs relegated into the league last season / promoted back now. */
  std::size_t relegated_in = 0;
  std::size_t relegated_up = 0;
  std::size_t manager_changes = 0;
  std::size_t club_seasons = 0;
  double wages = 0.0;
  double revenue = 0.0;
  std::vector<double> net;
};

/** Everything collected over all seasons. */
struct WorldTotals
{
  std::vector<MatchSample> matches;
  std::size_t world_matches = 0;
  double wall_seconds = 0.0;
  std::vector<double> season_seconds;
  // League-seasons.
  std::vector<double> champion_ppg;
  std::vector<double> champion_points;
  std::vector<double> relegated_ppg;
  std::vector<double> relegated_points;
  std::vector<double> scorer_goals;
  std::vector<double> scorer_per_game;
  std::vector<double> points_sd;
  std::vector<double> goal_diff_sd;
  TextTable leagues{
      "League seasons",
      {"Season", "League", "Tier", "Clubs", "Games", "Champion pts",
       "Best relegated pts", "Top scorer goals", "Points SD"},
      {}};
  // Club-seasons.
  std::vector<double> revenue;
  std::vector<double> net;
  std::vector<double> top_wages;
  std::vector<double> top_revenue;
  std::vector<double> lower_wages;
  std::vector<double> lower_revenue;
  std::size_t clubs_negative = 0;
  std::size_t clubs_counted = 0;
  std::vector<double> squad_sizes;
  std::vector<double> top_mean_ages;
  std::vector<double> ages;
  std::vector<double> potentials;
  std::vector<double> overalls;
  std::vector<double> new_players;
  std::vector<double> removed_players;
  std::vector<double> incoming_moves;
  std::vector<double> incoming_fee_moves;
  std::vector<double> incoming_loans;
  std::vector<double> club_injuries;
  std::vector<double> club_players_at_start;
  std::array<std::int64_t, FINANCE_CATEGORY_COUNT> top_mix{};
  // Wage-position regression inputs (pooled league-seasons).
  std::vector<double> wage_x;
  std::vector<double> position_y;
  std::vector<int> wage_tier;
  // Transfers and injuries.
  std::map<TransferKind, std::size_t> transfer_kinds;
  std::size_t fee_moves = 0;
  std::size_t counted_moves = 0;
  std::vector<InjuryOnset> injuries;
  std::map<LeagueID, LeagueTally> by_league;
  // Youth academies and young players.
  std::vector<double> rosters_with_academy;
  std::vector<double> academy_sizes;
  std::size_t u18_professional = 0;
  std::size_t u18_contracts = 0;
  std::array<std::vector<double>, 4> intake_candidates_by_grade;
  std::array<std::vector<double>, 4> intake_signed_by_grade;
  std::array<double, 3> league_minutes{};  // all, tier 1, tier 2
  std::array<double, 3> u21_minutes{};
  double minutes_17_18 = 0.0;
  double minutes_19_20 = 0.0;
  double minutes_21 = 0.0;
  std::size_t starts = 0;
  std::size_t u21_starts = 0;
  std::array<std::vector<double>, 3> development_by_age;  // 16-18, 19-21, 22-24
};

/** Academy grade bucket from the four academy ratings (0 = best). */
std::size_t academyGrade(const AcademyRatings& ratings)
{
  const double mean = (ratings.facilities + ratings.recruitment +
                       ratings.junior_coaching + ratings.head) /
                      4.0;
  if (mean >= 70.0) return 0;
  if (mean >= 55.0) return 1;
  if (mean >= 40.0) return 2;
  return 3;
}

class SeasonRunner
{
 public:
  explicit SeasonRunner(const SeasonOptions& run_options) : options(run_options)
  {
  }

  LabReport run()
  {
    const auto started = std::chrono::steady_clock::now();
    controller.newGame(LAB_SAVE_SLOT, options.seed);
    controller.setSimulationThreads(options.threads);
    leagues = selectLeagues(controller, options.leagues);
    for (int season = 0; season < options.seasons; ++season) runSeason();
    if (options.vitals) *options.vitals = trend;
    totals.wall_seconds = std::chrono::duration<double>(
                              std::chrono::steady_clock::now() - started)
                              .count();
    return buildReport();
  }

 private:
  const SeasonOptions& options;
  GameController controller;
  std::vector<LeagueID> leagues;
  WorldTotals totals;
  std::unordered_map<PlayerID, std::int32_t> last_injury_day;
  std::unordered_map<TeamID, int> injuries_by_club;
  std::unordered_map<TeamID, std::uint32_t> manager_of_club;
  std::unordered_set<TeamID> promoted_last;
  std::unordered_set<TeamID> relegated_last;
  PlayerID season_first_id = 0;
  std::unordered_map<PlayerID, std::pair<int, double>> start_ability;
  // Soak trend of the running season.
  std::vector<SeasonVitals> trend;
  SeasonVitals vitals;
  std::vector<double> day_ms;
  std::size_t season_wage_index = 0;
  std::size_t honours_before = 0;
  /** Tier and cash of every club on 30 June. */
  std::unordered_map<TeamID, std::pair<int, std::int64_t>> june_clubs;

  bool selected(LeagueID league) const
  {
    return std::ranges::binary_search(leagues, league);
  }

  std::vector<TeamID> clubIds() const
  {
    std::vector<TeamID> ids;
    for (const auto& team : controller.getTeams())
      ids.push_back(team.get().getId());
    std::ranges::sort(ids);
    return ids;
  }

  /** Records injuries that started since the previous call. */
  void pollInjuries(bool count)
  {
    for (const auto& [id, player] : controller.getGameData()->getPlayers())
    {
      const PlayerDynamics& dynamics = player.getDynamics();
      const auto [entry, inserted] =
          last_injury_day.try_emplace(id, dynamics.last_injury_day);
      if (inserted || entry->second == dynamics.last_injury_day) continue;
      entry->second = dynamics.last_injury_day;
      if (!count || dynamics.injury == InjuryType::None ||
          player.getTeamId() == FREE_AGENTS_TEAM_ID)
        continue;
      totals.injuries.push_back(
          {dynamics.last_injury, static_cast<double>(dynamics.injury_days)});
      ++injuries_by_club[player.getTeamId()];
    }
  }

  void runSeason()
  {
    const auto started = std::chrono::steady_clock::now();
    const GameDateValue seasonStart = controller.getCurrentDate();
    const int seasonNumber = controller.getCurrentSeason();
    const auto gamedata = controller.getGameData();

    std::unordered_map<PlayerID, TeamID> startPlayers;
    std::unordered_map<TeamID, int> startSquads;
    season_first_id = gamedata->peekNextPlayerId();
    start_ability.clear();
    const StatsConfig& config = controller.getStatsConfig();
    for (const auto& [id, player] : gamedata->getPlayers())
    {
      startPlayers.emplace(id, player.getTeamId());
      if (player.getTeamId() != FREE_AGENTS_TEAM_ID)
      {
        ++startSquads[player.getTeamId()];
        start_ability.try_emplace(
            id, std::pair{player.getAge(), player.getOverall(config)});
      }
    }
    injuries_by_club.clear();
    pollInjuries(false);
    pollManagers(false);
    vitals = SeasonVitals{};
    vitals.season = seasonNumber;
    day_ms.clear();
    season_wage_index = totals.wage_x.size();
    if (const NationalTeams* nations = controller.getNationalTeams())
      honours_before = nations->getHonours().size();

    int days = 0;
    while (!isSeasonEnd(controller.getCurrentDate()))
    {
      const auto day_started = std::chrono::steady_clock::now();
      controller.advanceDay();
      day_ms.push_back(std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - day_started)
                           .count());
      if (controller.getCurrentDate().day == 1) sampleMonthlyCash();
      pollInjuries(true);
      pollManagers(true);
      if (++days > options.max_days_per_season)
        throw SeasonSimulationError(
            std::format("season {} did not reach 30 June within {} days",
                        seasonNumber, options.max_days_per_season));
      if (options.progress && days % PROGRESS_EVERY_DAYS == 0)
        *options.progress << std::format(
            "[fm_lab] season {}/{} day {} ({})\n", seasonNumber,
            options.seasons, days, controller.getCurrentDate().toString());
    }

    std::map<LeagueID, std::vector<StandingRow>> tables;
    captureSeasonEnd(tables);
    captureClubs();
    captureYouth();
    captureVitalsAtSeasonEnd();

    // 1 July: season transition (prize money, history, retirements, youth).
    controller.advanceDay();
    // Season-end sackings happen at the rollover.
    pollManagers(true);
    const GameDateValue rollover = controller.getCurrentDate();
    if (controller.getCurrentSeason() == seasonNumber)
      throw SeasonSimulationError("season transition did not happen on 1 July");
    captureHistory(seasonNumber, tables);
    captureMovements(seasonNumber, tables);
    captureFinances(seasonStart, rollover);
    captureTransfers(seasonStart, rollover);
    capturePopulation(startPlayers, startSquads);
    totals.season_seconds.push_back(
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      started)
            .count());
    captureVitalsAfterRollover(seasonNumber, seasonStart, rollover,
                               startPlayers);
  }

  /** First day of a month: share of clubs in the red by tier. */
  void sampleMonthlyCash()
  {
    std::array<std::size_t, 2> clubs{};
    std::array<std::size_t, 2> negative{};
    for (const auto& team : controller.getTeams())
    {
      const Team& club = team.get();
      if (club.getId() == FREE_AGENTS_TEAM_ID) continue;
      const std::size_t tier =
          controller.getLeagueTier(club.getLeagueId()) == 1 ? 0 : 1;
      ++clubs[tier];
      if (club.getFinances().getBalance() < 0) ++negative[tier];
    }
    for (std::size_t tier = 0; tier < clubs.size(); ++tier)
      if (clubs[tier] > 0)
        vitals.tiers[tier].peak_negative_share =
            std::max(vitals.tiers[tier].peak_negative_share,
                     static_cast<double>(negative[tier]) /
                         static_cast<double>(clubs[tier]));
  }

  /** Population, ability, cash and reputation on 30 June. */
  void captureVitalsAtSeasonEnd()
  {
    const StatsConfig& config = controller.getStatsConfig();
    std::vector<double> overalls;
    std::vector<double> potentials;
    std::vector<double> free_agent_ages;
    std::array<std::vector<double>, 2> tier_overalls;
    std::unordered_map<TeamID, std::size_t> tier_of;
    june_clubs.clear();
    for (const auto& team : controller.getTeams())
    {
      const Team& club = team.get();
      if (club.getId() == FREE_AGENTS_TEAM_ID) continue;
      const int tier = controller.getLeagueTier(club.getLeagueId());
      tier_of.emplace(club.getId(), tier == 1 ? 0 : 1);
      june_clubs.try_emplace(club.getId(),
                             std::pair{tier, club.getFinances().getBalance()});
    }
    for (const auto& [id, player] : controller.getGameData()->getPlayers())
    {
      ++vitals.players;
      if (player.getTeamId() == FREE_AGENTS_TEAM_ID)
      {
        ++vitals.free_agents;
        free_agent_ages.push_back(player.getAge());
        continue;
      }
      const int age = player.getAge();
      std::size_t band = 4;
      if (age <= 18)
        band = 0;
      else if (age <= 23)
        band = 1;
      else if (age <= 29)
        band = 2;
      else if (age <= 33)
        band = 3;
      ++vitals.age_bands[band];
      if (player.isAcademyPlayer())
      {
        ++vitals.academy;
        continue;
      }
      ++vitals.club_seniors;
      const double overall = player.getOverall(config);
      overalls.push_back(overall);
      potentials.push_back(static_cast<double>(player.getPotential()));
      if (const auto tier = tier_of.find(player.getTeamId());
          tier != tier_of.end())
        tier_overalls[tier->second].push_back(overall);
    }
    vitals.free_agent_mean_age = meanOf(free_agent_ages);
    vitals.mean_overall = meanOf(overalls);
    vitals.mean_potential = meanOf(potentials);
    vitals.p90_overall = overalls.empty() ? 0.0 : percentile(overalls, 0.9);
    std::ranges::sort(overalls, std::greater<>{});
    overalls.resize(std::min(overalls.size(), TOP_PLAYERS));
    vitals.top100_overall = meanOf(overalls);

    std::array<std::vector<double>, 2> cash;
    std::array<std::vector<double>, 2> reputation;
    for (const auto& team : controller.getTeams())
    {
      const Team& club = team.get();
      if (club.getId() == FREE_AGENTS_TEAM_ID) continue;
      const std::size_t tier = tier_of.at(club.getId());
      cash[tier].push_back(
          static_cast<double>(club.getFinances().getBalance()));
      reputation[tier].push_back(club.getReputation());
    }
    for (std::size_t tier = 0; tier < cash.size(); ++tier)
    {
      TierVitals& row = vitals.tiers[tier];
      row.clubs = cash[tier].size();
      row.median_cash = cash[tier].empty() ? 0.0 : percentile(cash[tier], 0.5);
      row.negative_share =
          cash[tier].empty()
              ? 0.0
              : static_cast<double>(std::ranges::count_if(
                    cash[tier], [](double value) { return value < 0.0; })) /
                    static_cast<double>(cash[tier].size());
      row.mean_reputation = meanOf(reputation[tier]);
      row.reputation_sd = populationSd(reputation[tier]);
      row.mean_overall = meanOf(tier_overalls[tier]);
    }

    const ContinentalCompetitions& continental =
        controller.getGame()->getCompetitions().getContinental();
    for (const auto& season : continental.getSeasons())
    {
      ++vitals.continental_competitions;
      if (season.winner_id != 0) ++vitals.continental_completed;
    }
  }

  /** Finances, transfers, competitions and save cost of the season. */
  void captureVitalsAfterRollover(
      int seasonNumber, const GameDateValue& from, const GameDateValue& to,
      const std::unordered_map<PlayerID, TeamID>& startPlayers)
  {
    using enum TransferKind;
    std::array<double, 2> wages{};
    std::array<double, 2> revenue{};
    std::array<std::size_t, 2> insolvent{};
    for (const auto& [team_id, state] : june_clubs)
    {
      const auto [tier, balance] = state;
      const std::size_t slot = tier == 1 ? 0 : 1;
      const FinanceSummary summary =
          controller.getFinanceSummary(team_id, from, to);
      const auto club_revenue = static_cast<double>(revenueOf(summary));
      wages[slot] += static_cast<double>(
          std::abs(summary.by_category[static_cast<std::size_t>(
              std::to_underlying(FinanceCategory::Wages))]));
      revenue[slot] += club_revenue;
      vitals.tiers[slot].owner_investment +=
          static_cast<double>(summary.by_category[static_cast<std::size_t>(
              std::to_underlying(FinanceCategory::Investment))]);
      if (static_cast<double>(balance) <
          -INSOLVENT_REVENUE_SHARE * club_revenue)
        ++insolvent[slot];
    }
    for (std::size_t slot = 0; slot < wages.size(); ++slot)
    {
      TierVitals& row = vitals.tiers[slot];
      row.wage_revenue =
          revenue[slot] > 0.0 ? wages[slot] / revenue[slot] : 0.0;
      if (row.clubs > 0)
      {
        row.insolvent_share = static_cast<double>(insolvent[slot]) /
                              static_cast<double>(row.clubs);
        row.mean_revenue = revenue[slot] / static_cast<double>(row.clubs);
      }
    }

    const std::size_t count = totals.wage_x.size() - season_wage_index;
    vitals.wage_position_r2 = rSquared(
        std::span(totals.wage_x).subspan(season_wage_index, count),
        std::span(totals.position_y).subspan(season_wage_index, count));
    std::vector<double> top_x;
    std::vector<double> top_y;
    for (std::size_t i = season_wage_index; i < totals.wage_x.size(); ++i)
    {
      if (totals.wage_tier[i] != 1) continue;
      top_x.push_back(totals.wage_x[i]);
      top_y.push_back(totals.position_y[i]);
    }
    vitals.wage_position_r2_top = rSquared(top_x, top_y);

    std::vector<double> fees;
    for (const TransferRecord& record :
         controller.getGame()->getTransfers().history())
    {
      if (!inRange(record.date, from, to)) continue;
      if (record.kind != Release && record.kind != LoanReturn) ++vitals.moves;
      if (record.kind == Permanent && record.fee > 0)
        fees.push_back(static_cast<double>(record.fee));
    }
    vitals.fee_moves = fees.size();
    vitals.mean_fee = meanOf(fees);
    vitals.median_fee = fees.empty() ? 0.0 : percentile(fees, 0.5);
    vitals.max_fee = fees.empty() ? 0.0 : *std::ranges::max_element(fees);

    for (const SeasonHistoryEntry& entry : controller.getSeasonHistory())
    {
      if (entry.season != seasonNumber || entry.champion_id == 0) continue;
      if (entry.competition_type == MatchType::LEAGUE)
      {
        ++vitals.leagues_completed;
        vitals.promoted += entry.promoted.size();
        vitals.relegated += entry.relegated.size();
      }
      else if (entry.competition_type == MatchType::CUP)
        ++vitals.cups_completed;
    }
    if (const NationalTeams* nations = controller.getNationalTeams())
      vitals.national_honours = nations->getHonours().size() - honours_before;

    std::size_t present = 0;
    for (const auto& [id, player] : controller.getGameData()->getPlayers())
    {
      if (startPlayers.contains(id))
        ++present;
      else
        ++vitals.intake;
    }
    vitals.removed = startPlayers.size() - present;

    if (!day_ms.empty())
    {
      vitals.day_ms_mean = meanOf(day_ms);
      vitals.day_ms_p95 = percentile(day_ms, 0.95);
      vitals.day_ms_max = *std::ranges::max_element(day_ms);
    }
    vitals.season_seconds = totals.season_seconds.back();

    if (controller.saveGame())
    {
      vitals.save_ms = controller.getSaveStatus().timings.total_ms;
      std::error_code error;
      const auto bytes = std::filesystem::file_size(
          RuntimePaths::savePath(LAB_SAVE_SLOT), error);
      if (!error) vitals.save_mb = static_cast<double>(bytes) / 1e6;
    }
    if (options.reload)
    {
      const auto started = std::chrono::steady_clock::now();
      if (!controller.loadGame(LAB_SAVE_SLOT))
        throw SeasonSimulationError(
            std::format("the save after season {} did not load", seasonNumber));
      vitals.load_ms = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - started)
                           .count();
      controller.setSimulationThreads(options.threads);
    }
    trend.push_back(vitals);
    // One JSON line per season keeps a partial run readable.
    if (options.progress)
      *options.progress << "[fm_lab] vitals " << toJson(vitals).dump() << "\n"
                        << std::flush;
  }

  static nlohmann::json toJson(const SeasonVitals& v)
  {
    nlohmann::json tiers = nlohmann::json::array();
    for (const TierVitals& t : v.tiers)
      tiers.push_back({{"clubs", t.clubs},
                       {"median_cash", t.median_cash},
                       {"negative_share", t.negative_share},
                       {"peak_negative_share", t.peak_negative_share},
                       {"insolvent_share", t.insolvent_share},
                       {"wage_revenue", t.wage_revenue},
                       {"owner_investment", t.owner_investment},
                       {"mean_revenue", t.mean_revenue},
                       {"mean_reputation", t.mean_reputation},
                       {"reputation_sd", t.reputation_sd},
                       {"mean_overall", t.mean_overall}});
    return {{"season", v.season},
            {"players", v.players},
            {"club_seniors", v.club_seniors},
            {"academy", v.academy},
            {"free_agents", v.free_agents},
            {"free_agent_mean_age", v.free_agent_mean_age},
            {"age_bands", v.age_bands},
            {"intake", v.intake},
            {"removed", v.removed},
            {"mean_overall", v.mean_overall},
            {"p90_overall", v.p90_overall},
            {"top100_overall", v.top100_overall},
            {"mean_potential", v.mean_potential},
            {"tiers", tiers},
            {"wage_position_r2", v.wage_position_r2},
            {"wage_position_r2_top", v.wage_position_r2_top},
            {"fee_moves", v.fee_moves},
            {"median_fee", v.median_fee},
            {"mean_fee", v.mean_fee},
            {"max_fee", v.max_fee},
            {"moves", v.moves},
            {"leagues_completed", v.leagues_completed},
            {"promoted", v.promoted},
            {"relegated", v.relegated},
            {"cups_completed", v.cups_completed},
            {"continental_completed", v.continental_completed},
            {"continental_competitions", v.continental_competitions},
            {"national_honours", v.national_honours},
            {"save_mb", v.save_mb},
            {"save_ms", v.save_ms},
            {"load_ms", v.load_ms},
            {"day_ms_mean", v.day_ms_mean},
            {"day_ms_p95", v.day_ms_p95},
            {"day_ms_max", v.day_ms_max},
            {"season_seconds", v.season_seconds}};
  }

  TextTable trendTable() const
  {
    TextTable table{
        "Soak trend (one column per season; population and cash "
        "on 30 June, flows from 1 July to the rollover)",
        {"Measure"},
        {}};
    for (const SeasonVitals& season : trend)
      table.header.push_back(std::format("S{}", season.season));
    const auto row = [&](const char* name, auto&& value)
    {
      std::vector<std::string> cells{name};
      for (const SeasonVitals& season : trend) cells.push_back(value(season));
      table.rows.push_back(std::move(cells));
    };
    const auto number = [](double value, int digits)
    { return std::format("{:.{}f}", value, digits); };
    const auto percent = [](double value)
    { return std::format("{:.1f}%", 100.0 * value); };
    const auto millions = [](double value)
    { return std::format("{:.1f}", value / 1e6); };
    row("Players (all)",
        [](const SeasonVitals& v) { return std::to_string(v.players); });
    row("Senior club players",
        [](const SeasonVitals& v) { return std::to_string(v.club_seniors); });
    row("Academy players",
        [](const SeasonVitals& v) { return std::to_string(v.academy); });
    row("Free agents",
        [](const SeasonVitals& v) { return std::to_string(v.free_agents); });
    row("Free-agent mean age", [&number](const SeasonVitals& v)
        { return number(v.free_agent_mean_age, 1); });
    static constexpr std::array<const char*, 5> BANDS = {
        "Club players <= 18", "Club players 19-23", "Club players 24-29",
        "Club players 30-33", "Club players 34+"};
    for (std::size_t band = 0; band < BANDS.size(); ++band)
      row(BANDS[band], [band](const SeasonVitals& v)
          { return std::to_string(v.age_bands[band]); });
    row("New players (intake)",
        [](const SeasonVitals& v) { return std::to_string(v.intake); });
    row("Removed players (left the world)",
        [](const SeasonVitals& v) { return std::to_string(v.removed); });
    row("Mean overall, senior club players",
        [&number](const SeasonVitals& v) { return number(v.mean_overall, 2); });
    row("P90 overall",
        [&number](const SeasonVitals& v) { return number(v.p90_overall, 2); });
    row("Top-100 mean overall", [&number](const SeasonVitals& v)
        { return number(v.top100_overall, 2); });
    row("Mean potential", [&number](const SeasonVitals& v)
        { return number(v.mean_potential, 2); });
    static constexpr std::array<const char*, 2> TIERS = {"tier 1", "tier 2"};
    for (std::size_t tier = 0; tier < TIERS.size(); ++tier)
    {
      const auto label = [&](const char* what)
      { return std::format("{}, {}", what, TIERS[tier]); };
      row(label("Mean overall").c_str(), [&number, tier](const SeasonVitals& v)
          { return number(v.tiers[tier].mean_overall, 2); });
      row(label("Mean revenue (M)").c_str(),
          [&millions, tier](const SeasonVitals& v)
          { return millions(v.tiers[tier].mean_revenue); });
      row(label("Wages / revenue").c_str(),
          [&percent, tier](const SeasonVitals& v)
          { return percent(v.tiers[tier].wage_revenue); });
      row(label("Median cash (M)").c_str(),
          [&millions, tier](const SeasonVitals& v)
          { return millions(v.tiers[tier].median_cash); });
      row(label("Negative cash, 30 June").c_str(),
          [&percent, tier](const SeasonVitals& v)
          { return percent(v.tiers[tier].negative_share); });
      row(label("Negative cash, worst month").c_str(),
          [&percent, tier](const SeasonVitals& v)
          { return percent(v.tiers[tier].peak_negative_share); });
      row(label("Insolvent (cash < -25% revenue)").c_str(),
          [&percent, tier](const SeasonVitals& v)
          { return percent(v.tiers[tier].insolvent_share); });
      row(label("Owner investment (M)").c_str(),
          [&millions, tier](const SeasonVitals& v)
          { return millions(v.tiers[tier].owner_investment); });
      row(label("Mean reputation").c_str(),
          [&number, tier](const SeasonVitals& v)
          { return number(v.tiers[tier].mean_reputation, 1); });
      row(label("Reputation SD").c_str(), [&number, tier](const SeasonVitals& v)
          { return number(v.tiers[tier].reputation_sd, 1); });
    }
    row("Wage-position R2", [&number](const SeasonVitals& v)
        { return number(v.wage_position_r2, 3); });
    row("Wage-position R2, tier 1", [&number](const SeasonVitals& v)
        { return number(v.wage_position_r2_top, 3); });
    row("Moves (excl. releases, loan returns)",
        [](const SeasonVitals& v) { return std::to_string(v.moves); });
    row("Fee transfers",
        [](const SeasonVitals& v) { return std::to_string(v.fee_moves); });
    row("Median fee (M)", [&number](const SeasonVitals& v)
        { return number(v.median_fee / 1e6, 2); });
    row("Mean fee (M)", [&number](const SeasonVitals& v)
        { return number(v.mean_fee / 1e6, 2); });
    row("Top fee (M)", [&number](const SeasonVitals& v)
        { return number(v.max_fee / 1e6, 1); });
    row("Leagues completed", [](const SeasonVitals& v)
        { return std::to_string(v.leagues_completed); });
    row("Clubs promoted / relegated", [](const SeasonVitals& v)
        { return std::format("{} / {}", v.promoted, v.relegated); });
    row("Cups completed",
        [](const SeasonVitals& v) { return std::to_string(v.cups_completed); });
    row("Continental competitions won / held",
        [](const SeasonVitals& v)
        {
          return std::format("{} / {}", v.continental_completed,
                             v.continental_competitions);
        });
    row("National-team honours", [](const SeasonVitals& v)
        { return std::to_string(v.national_honours); });
    row("Save size (MB)",
        [&number](const SeasonVitals& v) { return number(v.save_mb, 1); });
    row("Save time (ms)",
        [&number](const SeasonVitals& v) { return number(v.save_ms, 0); });
    row("Load time (ms)",
        [&number](const SeasonVitals& v) { return number(v.load_ms, 0); });
    row("Continue day mean (ms)",
        [&number](const SeasonVitals& v) { return number(v.day_ms_mean, 0); });
    row("Continue day P95 (ms)",
        [&number](const SeasonVitals& v) { return number(v.day_ms_p95, 0); });
    row("Continue day max (ms)",
        [&number](const SeasonVitals& v) { return number(v.day_ms_max, 0); });
    row("Season wall time (s)", [&number](const SeasonVitals& v)
        { return number(v.season_seconds, 0); });
    return table;
  }

  void captureSeasonEnd(std::map<LeagueID, std::vector<StandingRow>>& tables)
  {
    for (const auto& [date, matches] :
         controller.getGame()->getCalendar().getFullCalendar())
    {
      for (const Match& match : matches)
      {
        if (!match.isPlayed()) continue;
        ++totals.world_matches;
        if (match.getMatchType() != MatchType::LEAGUE ||
            !selected(match.getCompetitionId()))
          continue;
        if (const auto report = controller.getMatchReport(
                date, match.getHomeTeamId(), match.getAwayTeamId()))
        {
          totals.matches.push_back(sampleFromReport(*report));
          tallyMatch(match.getCompetitionId(), totals.matches.back());
          captureMinutes(*report, match.getCompetitionId());
        }
      }
    }
    for (const LeagueID league : leagues)
    {
      std::vector<StandingRow> rows = controller.getStandings(league);
      if (rows.empty() || rows.front().played == 0) continue;
      const double games = rows.front().played;
      totals.champion_points.push_back(rows.front().points);
      totals.champion_ppg.push_back(rows.front().points / games);
      std::vector<double> ppg;
      std::vector<double> goalDiff;
      for (const StandingRow& row : rows)
      {
        const double played = std::max<double>(row.played, 1.0);
        ppg.push_back(row.points / played);
        goalDiff.push_back(row.goal_difference / played);
      }
      totals.points_sd.push_back(populationSd(ppg));
      totals.by_league[league].champion_ppg.push_back(rows.front().points /
                                                      games);
      totals.by_league[league].points_sd.push_back(populationSd(ppg));
      totals.goal_diff_sd.push_back(populationSd(goalDiff));
      if (const auto scorers =
              controller.getTopScorers(MatchType::LEAGUE, league, 1);
          !scorers.empty())
      {
        totals.scorer_goals.push_back(scorers.front().goals);
        totals.scorer_per_game.push_back(scorers.front().goals / games);
      }
      // Wage-to-position inputs: ln(wage / league mean), -ln(p/(N+1-p)).
      std::vector<double> wages;
      for (const StandingRow& row : rows)
        wages.push_back(static_cast<double>(std::max<std::int64_t>(
            controller.getWeeklyWageBill(row.team_id), 1)));
      double meanWage = 0.0;
      for (const double wage : wages) meanWage += wage;
      meanWage /= static_cast<double>(wages.size());
      const auto clubs = static_cast<double>(rows.size());
      for (std::size_t i = 0; i < rows.size(); ++i)
      {
        const double position = rows[i].position;
        totals.wage_x.push_back(std::log(wages[i] / meanWage));
        totals.position_y.push_back(
            -std::log(position / (clubs + 1.0 - position)));
        totals.wage_tier.push_back(controller.getLeagueTier(league));
      }
      tables.try_emplace(league, std::move(rows));
    }
  }

  void captureClubs()
  {
    const StatsConfig& config = controller.getStatsConfig();
    for (const auto& team : controller.getTeams())
    {
      const Team& club = team.get();
      if (!selected(club.getLeagueId())) continue;
      const auto& squad = controller.getPlayersForTeam(club.getId());
      totals.squad_sizes.push_back(static_cast<double>(
          std::ranges::count_if(squad, [](const Player& player)
                                { return !player.isAcademyPlayer(); })));
      double ageSum = 0.0;
      std::size_t seniors = 0;
      for (const auto& player : squad)
      {
        // Academy players are not part of the senior squad.
        if (player.get().isAcademyPlayer()) continue;
        ++seniors;
        const double age = player.get().getAge();
        ageSum += age;
        totals.ages.push_back(age);
        totals.potentials.push_back(
            static_cast<double>(player.get().getPotential()));
        totals.overalls.push_back(player.get().getOverall(config));
      }
      if (seniors > 0 && controller.getLeagueTier(club.getLeagueId()) == 1)
        totals.top_mean_ages.push_back(ageSum / static_cast<double>(seniors));
    }
  }

  /** League minutes by age (age at season end). */
  void captureMinutes(const MatchReport& report, LeagueID league)
  {
    const int tier = controller.getLeagueTier(league);
    const auto& players = controller.getGameData()->getPlayers();
    for (const PlayerMatchLine& line : report.players)
    {
      const auto found = players.find(line.player_id);
      if (found == players.end() || line.minutes == 0) continue;
      const int age = found->second.getAge();
      const double minutes = line.minutes;
      const bool young = age < 21;
      for (const std::size_t slot :
           {std::size_t{0}, static_cast<std::size_t>(tier == 1 ? 1 : 2)})
      {
        totals.league_minutes[slot] += minutes;
        if (young) totals.u21_minutes[slot] += minutes;
      }
      if (age <= 18)
        totals.minutes_17_18 += minutes;
      else if (young)
        totals.minutes_19_20 += minutes;
      else if (age == 21)
        totals.minutes_21 += minutes;
      if (line.started)
      {
        ++totals.starts;
        if (young) ++totals.u21_starts;
      }
    }
  }

  /** Academy sizes, contracts, intake by academy grade and development. */
  void captureYouth()
  {
    const auto gamedata = controller.getGameData();
    const YouthAcademy& academy = controller.getGame()->getWorld().getYouth();
    const StatsConfig& config = controller.getStatsConfig();
    const auto year =
        static_cast<std::uint16_t>(controller.getCurrentDate().year);
    for (const auto& team : controller.getTeams())
    {
      const Team& club = team.get();
      if (!selected(club.getLeagueId())) continue;
      const auto squad = academy.members(club.getId(), YouthStatus::Squad);
      std::size_t signed_now = 0;
      for (const YouthRecord* youth : squad)
      {
        ++totals.u18_contracts;
        if (youth->contract == YouthContract::Professional)
          ++totals.u18_professional;
        if (youth->player_id >= season_first_id) ++signed_now;
      }
      totals.academy_sizes.push_back(static_cast<double>(squad.size()));
      const auto seniors = std::ranges::count_if(
          controller.getPlayersForTeam(club.getId()),
          [](const Player& player) { return !player.isAcademyPlayer(); });
      totals.rosters_with_academy.push_back(static_cast<double>(seniors) +
                                            static_cast<double>(squad.size()));
      const std::size_t grade = academyGrade(academy.ratings(club.getId()));
      totals.intake_signed_by_grade[grade].push_back(
          static_cast<double>(signed_now));
      totals.intake_candidates_by_grade[grade].push_back(
          static_cast<double>(academy.preview(club.getId(), year).size));
    }
    for (const auto& [id, before] : start_ability)
    {
      const auto player = gamedata->getPlayer(id);
      if (!player || player->get().getTeamId() == FREE_AGENTS_TEAM_ID ||
          !selected(gamedata->getTeam(player->get().getTeamId())
                        ->get()
                        .getLeagueId()))
        continue;
      const auto [age, overall] = before;
      const double delta = player->get().getOverall(config) - overall;
      if (age >= 16 && age <= 18)
        totals.development_by_age[0].push_back(delta);
      else if (age >= 19 && age <= 21)
        totals.development_by_age[1].push_back(delta);
      else if (age >= 22 && age <= 24)
        totals.development_by_age[2].push_back(delta);
    }
  }

  /** Counts managers leaving AI clubs (sackings and other departures). */
  void pollManagers(bool count)
  {
    std::unordered_map<TeamID, std::uint32_t> current;
    for (const AiManager& manager :
         controller.getGame()->getCareer().getAiManagers())
      if (manager.team_id != FREE_AGENTS_TEAM_ID)
        current.emplace(manager.team_id, manager.id);
    if (count)
    {
      for (const auto& [team_id, manager_id] : manager_of_club)
      {
        if (const auto now = current.find(team_id);
            now != current.end() && now->second == manager_id)
          continue;
        const auto team = controller.getGameData()->getTeam(team_id);
        if (team && selected(team->get().getLeagueId()))
          ++totals.by_league[team->get().getLeagueId()].manager_changes;
      }
    }
    manager_of_club = std::move(current);
  }

  void tallyMatch(LeagueID league, const MatchSample& sample)
  {
    LeagueTally& tally = totals.by_league[league];
    ++tally.matches;
    if (sample.goals[0] > sample.goals[1])
      ++tally.home_wins;
    else if (sample.goals[0] == sample.goals[1])
      ++tally.draws;
    tally.goals += sample.goals[0] + sample.goals[1];
    tally.yellows += sample.yellows;
    tally.reds += sample.reds;
  }

  /** Promoted clubs going straight down and relegated clubs coming back. */
  void captureMovements(
      int seasonNumber,
      const std::map<LeagueID, std::vector<StandingRow>>& tables)
  {
    std::unordered_set<TeamID> promoted_now;
    std::unordered_set<TeamID> relegated_now;
    for (const SeasonHistoryEntry& entry : controller.getSeasonHistory())
    {
      if (entry.season != seasonNumber ||
          entry.competition_type != MatchType::LEAGUE)
        continue;
      promoted_now.insert(entry.promoted.begin(), entry.promoted.end());
      relegated_now.insert(entry.relegated.begin(), entry.relegated.end());
      const auto table = tables.find(entry.competition_id);
      if (table == tables.end()) continue;
      LeagueTally& tally = totals.by_league[entry.competition_id];
      for (const StandingRow& row : table->second)
      {
        if (promoted_last.contains(row.team_id))
        {
          ++tally.promoted_in;
          if (std::ranges::contains(entry.relegated, row.team_id))
            ++tally.promoted_down;
        }
        if (relegated_last.contains(row.team_id))
        {
          ++tally.relegated_in;
          if (std::ranges::contains(entry.promoted, row.team_id))
            ++tally.relegated_up;
        }
      }
      tally.club_seasons += table->second.size();
    }
    promoted_last = std::move(promoted_now);
    relegated_last = std::move(relegated_now);
  }

  void captureHistory(
      int seasonNumber,
      const std::map<LeagueID, std::vector<StandingRow>>& tables)
  {
    for (const SeasonHistoryEntry& entry : controller.getSeasonHistory())
    {
      if (entry.season != seasonNumber ||
          entry.competition_type != MatchType::LEAGUE)
        continue;
      const auto table = tables.find(entry.competition_id);
      if (table == tables.end()) continue;
      const std::vector<StandingRow>& rows = table->second;
      std::optional<StandingRow> bestRelegated;
      for (const StandingRow& row : rows)
        if (std::ranges::contains(entry.relegated, row.team_id) &&
            (!bestRelegated || row.position < bestRelegated->position))
          bestRelegated = row;
      if (bestRelegated && bestRelegated->played > 0)
      {
        totals.relegated_points.push_back(bestRelegated->points);
        totals.relegated_ppg.push_back(
            static_cast<double>(bestRelegated->points) / bestRelegated->played);
      }
      totals.leagues.rows.push_back(
          {std::to_string(seasonNumber), entry.competition_name,
           std::to_string(controller.getLeagueTier(entry.competition_id)),
           std::to_string(rows.size()), std::to_string(rows.front().played),
           std::to_string(rows.front().points),
           bestRelegated ? std::to_string(bestRelegated->points) : "-",
           std::to_string(entry.top_scorer_goals),
           std::format("{:.1f}", populationSd(
                                     [&]
                                     {
                                       std::vector<double> points;
                                       for (const StandingRow& row : rows)
                                         points.push_back(row.points);
                                       return points;
                                     }()))});
    }
  }

  void captureFinances(const GameDateValue& from, const GameDateValue& to)
  {
    for (const auto& team : controller.getTeams())
    {
      const Team& club = team.get();
      if (!selected(club.getLeagueId())) continue;
      const FinanceSummary summary =
          controller.getFinanceSummary(club.getId(), from, to);
      const auto category = [&](FinanceCategory c)
      {
        return summary
            .by_category[static_cast<std::size_t>(std::to_underlying(c))];
      };
      const auto revenue = static_cast<double>(revenueOf(summary));
      const auto wages =
          static_cast<double>(std::abs(category(FinanceCategory::Wages)));
      totals.revenue.push_back(revenue);
      LeagueTally& tally = totals.by_league[club.getLeagueId()];
      tally.wages += wages;
      tally.revenue += revenue;
      tally.net.push_back(static_cast<double>(
          summary.net() - category(FinanceCategory::OpeningBalance)));
      totals.net.push_back(static_cast<double>(
          summary.net() - category(FinanceCategory::OpeningBalance)));
      if (controller.getLeagueTier(club.getLeagueId()) == 1)
      {
        totals.top_wages.push_back(wages);
        totals.top_revenue.push_back(revenue);
        for (std::size_t c = 0; c < FINANCE_CATEGORY_COUNT; ++c)
          totals.top_mix[c] += std::abs(summary.by_category[c]);
      }
      else
      {
        totals.lower_wages.push_back(wages);
        totals.lower_revenue.push_back(revenue);
      }
      ++totals.clubs_counted;
      if (club.getFinances().getBalance() < 0) ++totals.clubs_negative;
    }
  }

  void captureTransfers(const GameDateValue& from, const GameDateValue& to)
  {
    using enum TransferKind;
    std::unordered_map<TeamID, int> moves;
    std::unordered_map<TeamID, int> feeMoves;
    std::unordered_map<TeamID, int> loans;
    for (const TransferRecord& record :
         controller.getGame()->getTransfers().history())
    {
      if (!inRange(record.date, from, to)) continue;
      ++totals.transfer_kinds[record.kind];
      if (record.kind == Release || record.kind == LoanReturn) continue;
      ++totals.counted_moves;
      ++moves[record.to_team];
      if (const bool fee = record.kind == Permanent && record.fee > 0; fee)
      {
        ++totals.fee_moves;
        ++feeMoves[record.to_team];
      }
      if (record.kind == Loan) ++loans[record.to_team];
    }
    for (const TeamID club : clubIds())
    {
      totals.incoming_moves.push_back(moves[club]);
      totals.incoming_fee_moves.push_back(feeMoves[club]);
      totals.incoming_loans.push_back(loans[club]);
    }
  }

  void capturePopulation(
      const std::unordered_map<PlayerID, TeamID>& startPlayers,
      const std::unordered_map<TeamID, int>& startSquads)
  {
    std::unordered_map<TeamID, int> added;
    std::unordered_map<TeamID, int> removed;
    std::unordered_set<PlayerID> present;
    for (const auto& [id, player] : controller.getGameData()->getPlayers())
    {
      present.insert(id);
      if (!startPlayers.contains(id)) ++added[player.getTeamId()];
    }
    for (const auto& [id, team] : startPlayers)
      if (!present.contains(id)) ++removed[team];
    for (const TeamID club : clubIds())
    {
      totals.new_players.push_back(added[club]);
      totals.removed_players.push_back(removed[club]);
      const auto squad = startSquads.find(club);
      if (squad != startSquads.end() && squad->second > 0)
      {
        totals.club_injuries.push_back(injuries_by_club[club]);
        totals.club_players_at_start.push_back(squad->second);
      }
    }
  }

  LabReport buildReport()
  {
    std::map<std::string, Estimate> metrics = matchMetrics(totals.matches);
    metrics["champion_ppg"] = meanEstimate(totals.champion_ppg);
    metrics["champion_points"] = meanEstimate(totals.champion_points);
    metrics["best_relegated_ppg"] = meanEstimate(totals.relegated_ppg);
    metrics["best_relegated_points"] = meanEstimate(totals.relegated_points);
    metrics["top_scorer_goals"] = meanEstimate(totals.scorer_goals);
    metrics["top_scorer_goals_per_game"] = meanEstimate(totals.scorer_per_game);
    metrics["points_sd_ppg"] = meanEstimate(totals.points_sd);
    metrics["goal_diff_sd_per_game"] = meanEstimate(totals.goal_diff_sd);
    metrics["median_club_revenue"] = medianEstimate(totals.revenue);
    metrics["median_club_net"] = medianEstimate(totals.net);
    metrics["wage_revenue_top"] =
        ratioEstimate(totals.top_wages, totals.top_revenue);
    metrics["wage_revenue_lower"] =
        ratioEstimate(totals.lower_wages, totals.lower_revenue);
    metrics["negative_cash_share"] =
        proportionEstimate(totals.clubs_negative, totals.clubs_counted);
    metrics["fee_transfer_share"] =
        proportionEstimate(totals.fee_moves, totals.counted_moves);
    metrics["transfers_per_club"] = meanEstimate(totals.incoming_moves);
    metrics["fee_moves_per_club"] = meanEstimate(totals.incoming_fee_moves);
    metrics["loans_per_club"] = meanEstimate(totals.incoming_loans);
    metrics["squad_size"] = meanEstimate(totals.squad_sizes);
    metrics["player_age_mean"] = meanEstimate(totals.ages);
    metrics["mean_age_top_squads"] = meanEstimate(totals.top_mean_ages);
    metrics["new_players_per_club"] = meanEstimate(totals.new_players);
    metrics["removed_players_per_club"] = meanEstimate(totals.removed_players);
    metrics["injuries_per_player_season"] =
        ratioEstimate(totals.club_injuries, totals.club_players_at_start);

    std::size_t hamstrings = 0;
    std::map<InjuryType, std::vector<double>> layoffs;
    for (const InjuryOnset& onset : totals.injuries)
    {
      if (onset.type == InjuryType::HamstringStrain ||
          onset.type == InjuryType::HamstringTightness)
        ++hamstrings;
      layoffs[onset.type].push_back(onset.days);
    }
    metrics["hamstring_share"] =
        proportionEstimate(hamstrings, totals.injuries.size());
    const auto median = [&](InjuryType type)
    {
      const auto found = layoffs.find(type);
      return found == layoffs.end() ? Estimate{}
                                    : medianEstimate(found->second);
    };
    metrics["hamstring_layoff_median"] = median(InjuryType::HamstringStrain);
    metrics["ankle_layoff_median"] = median(InjuryType::AnkleSprain);
    metrics["acl_layoff_median"] = median(InjuryType::KneeAcl);

    const std::vector<double>& x = totals.wage_x;
    const std::vector<double>& y = totals.position_y;
    metrics["wage_position_r2"] = bootstrapEstimate(
        x.size(),
        [&x, &y](std::span<const std::size_t> indices)
        {
          const auto n = static_cast<double>(indices.size());
          double sx = 0.0;
          double sy = 0.0;
          double sxx = 0.0;
          double syy = 0.0;
          double sxy = 0.0;
          for (const std::size_t i : indices)
          {
            sx += x[i];
            sy += y[i];
            sxx += x[i] * x[i];
            syy += y[i] * y[i];
            sxy += x[i] * y[i];
          }
          const double cov = sxy - sx * sy / n;
          const double vx = sxx - sx * sx / n;
          const double vy = syy - sy * sy / n;
          if (vx <= 0.0 || vy <= 0.0) return std::nan("");
          return cov * cov / (vx * vy);
        },
        0x3A6E5ULL);

    if (totals.world_matches > 0)
    {
      Estimate speed;
      speed.high = 1000.0 * totals.wall_seconds /
                   static_cast<double>(totals.world_matches);
      speed.low = speed.high;
      speed.value = speed.low;
      speed.n = totals.world_matches;
      metrics["ms_per_world_match"] = speed;
    }

    LabReport report;
    report.mode = "season";
    report.seed = options.seed;
    report.samples = static_cast<std::size_t>(options.seasons);
    report.threads = options.threads;
    report.wall_seconds = totals.wall_seconds;
    report.ms_per_match = totals.world_matches == 0
                              ? 0.0
                              : 1000.0 * totals.wall_seconds /
                                    static_cast<double>(totals.world_matches);
    report.seconds_per_season =
        totals.wall_seconds / std::max(options.seasons, 1);
    report.parameters["leagues"] = options.leagues;
    report.parameters["league_ids"] = [&]
    {
      std::string ids;
      for (const LeagueID league : leagues)
        ids += std::format("{}{}", ids.empty() ? "" : ",", league);
      return ids;
    }();
    report.parameters["world_matches_played"] =
        std::to_string(totals.world_matches);
    report.parameters["league_matches_measured"] =
        std::to_string(totals.matches.size());
    static constexpr std::array SCOPES = {Scope::Match, Scope::World};
    report.rows = evaluateTargets(metrics, SCOPES);
    report.tables.push_back(trendTable());
    report.tables.push_back(totals.leagues);
    report.tables.push_back(leagueTable());
    for (TextTable& table : matchTables(totals.matches))
      report.tables.push_back(std::move(table));
    report.tables.push_back(revenueMixTable());
    report.tables.push_back(populationTable());
    report.tables.push_back(youthTable());
    report.tables.push_back(transferTable());
    report.tables.push_back(injuryTable(layoffs));
    report.notes.emplace_back(
        "Season mode measures league matches from structured match reports; "
        "engine-only metrics (penalties, substitutions, distance, game "
        "states...) come from `fm_lab matches`.");
    report.notes.emplace_back(
        "AI-only world (no managed club); every club is run by the AI.");
    return report;
  }

  TextTable leagueTable() const
  {
    TextTable table{
        "League by league (all measured seasons; promoted/relegated need two "
        "or more seasons; finances are for the clubs in the league at the "
        "season end)",
        {"League", "Tier", "Matches", "Goals/m", "H/D/A %", "Yellows/m",
         "Reds/m", "Champion ppg", "Points SD/g", "Promoted down",
         "Relegated back up", "Manager changes/club-season", "Wages/revenue",
         "Median net (M)"},
        {}};
    const auto mean = [](const std::vector<double>& values)
    {
      if (values.empty()) return std::string("-");
      double sum = 0.0;
      for (const double value : values) sum += value;
      return std::format("{:.2f}", sum / static_cast<double>(values.size()));
    };
    const auto share = [](std::size_t part, std::size_t whole)
    {
      return whole == 0 ? std::string("-")
                        : std::format("{}/{} ({:.0f}%)", part, whole,
                                      100.0 * static_cast<double>(part) /
                                          static_cast<double>(whole));
    };
    for (const auto& [league, tally] : totals.by_league)
    {
      if (tally.matches == 0) continue;
      const auto games = static_cast<double>(tally.matches);
      const auto found = controller.getLeagueById(league);
      const std::size_t away_wins =
          tally.matches - tally.home_wins - tally.draws;
      const std::size_t club_seasons =
          tally.club_seasons > 0 ? tally.club_seasons : tally.net.size();
      table.rows.push_back(
          {found ? found->get().getName() : std::to_string(league),
           std::to_string(controller.getLeagueTier(league)),
           std::to_string(tally.matches),
           std::format("{:.2f}", tally.goals / games),
           std::format("{:.1f}/{:.1f}/{:.1f}",
                       100.0 * static_cast<double>(tally.home_wins) / games,
                       100.0 * static_cast<double>(tally.draws) / games,
                       100.0 * static_cast<double>(away_wins) / games),
           std::format("{:.2f}", tally.yellows / games),
           std::format("{:.3f}", tally.reds / games), mean(tally.champion_ppg),
           mean(tally.points_sd), share(tally.promoted_down, tally.promoted_in),
           share(tally.relegated_up, tally.relegated_in),
           club_seasons == 0
               ? std::string("-")
               : std::format("{:.2f}",
                             static_cast<double>(tally.manager_changes) /
                                 static_cast<double>(club_seasons)),
           tally.revenue > 0.0
               ? std::format("{:.0f}%", 100.0 * tally.wages / tally.revenue)
               : std::string("-"),
           tally.net.empty()
               ? std::string("-")
               : std::format("{:.1f}", percentile(tally.net, 0.5) / 1e6)});
    }
    return table;
  }

  TextTable revenueMixTable() const
  {
    TextTable table{
        "Top-division revenue mix (CT-W22 reference: TV 46 / gate "
        "14 / commercial 32 / prize+other 8%)",
        {"Category", "Share of revenue"},
        {}};
    const auto amount = [&](FinanceCategory c)
    {
      return static_cast<double>(
          totals.top_mix[static_cast<std::size_t>(std::to_underlying(c))]);
    };
    const double revenue = amount(FinanceCategory::Matchday) +
                           amount(FinanceCategory::Broadcasting) +
                           amount(FinanceCategory::Sponsorship) +
                           amount(FinanceCategory::PrizeMoney);
    const auto row = [&](const char* name, FinanceCategory c)
    {
      table.rows.push_back(
          {name, revenue > 0.0
                     ? std::format("{:.1f}%", 100.0 * amount(c) / revenue)
                     : "-"});
    };
    row("Broadcasting", FinanceCategory::Broadcasting);
    row("Matchday", FinanceCategory::Matchday);
    row("Sponsorship", FinanceCategory::Sponsorship);
    row("Prize money", FinanceCategory::PrizeMoney);
    return table;
  }

  TextTable populationTable() const
  {
    TextTable table{
        "Player population (selected leagues, season end)",
        {"Measure", "P10", "P25", "P50", "P75", "P90", ">= 70", ">= 80"},
        {}};
    const auto row = [&](const char* name, const std::vector<double>& values)
    {
      const auto atLeast = [&](double threshold)
      {
        if (values.empty()) return std::string("-");
        const auto count = std::ranges::count_if(
            values, [&](double value) { return value >= threshold; });
        return std::format("{:.1f}%", 100.0 * static_cast<double>(count) /
                                          static_cast<double>(values.size()));
      };
      table.rows.push_back({name,
                            std::format("{:.1f}", percentile(values, 0.1)),
                            std::format("{:.1f}", percentile(values, 0.25)),
                            std::format("{:.1f}", percentile(values, 0.5)),
                            std::format("{:.1f}", percentile(values, 0.75)),
                            std::format("{:.1f}", percentile(values, 0.9)),
                            atLeast(POTENTIAL_GOOD), atLeast(POTENTIAL_HIGH)});
    };
    row("Potential", totals.potentials);
    row("Overall", totals.overalls);
    row("Age", totals.ages);
    return table;
  }

  TextTable youthTable() const
  {
    TextTable table{
        "Senior squads, academies and young players (selected "
        "leagues, season end)",
        {"Measure", "Value", "n"},
        {}};
    const auto mean = [&](const char* name, const std::vector<double>& values)
    {
      const Estimate estimate = meanEstimate(values);
      table.rows.push_back(
          {name,
           estimate.valid()
               ? std::format("{:.2f} [{:.2f}, {:.2f}]", estimate.value,
                             estimate.low, estimate.high)
               : "-",
           std::to_string(values.size())});
    };
    const auto share = [&](const char* name, double part, double whole)
    {
      table.rows.push_back(
          {name,
           whole > 0.0 ? std::format("{:.1f}%", 100.0 * part / whole) : "-",
           std::format("{:.0f}", whole)});
    };
    mean("Senior squad size", totals.squad_sizes);
    mean("Club players incl. academy", totals.rosters_with_academy);
    mean("U18 academy squad size", totals.academy_sizes);
    mean("Mean senior age, top divisions", totals.top_mean_ages);
    share("U18 academy players on professional contracts",
          static_cast<double>(totals.u18_professional),
          static_cast<double>(totals.u18_contracts));
    static constexpr std::array<const char*, 4> GRADES = {
        "grade >= 70", "grade 55-69", "grade 40-54", "grade < 40"};
    for (std::size_t grade = 0; grade < GRADES.size(); ++grade)
    {
      mean(std::format("Intake candidates, academy {}", GRADES[grade]).c_str(),
           totals.intake_candidates_by_grade[grade]);
      mean(std::format("Intake signed, academy {}", GRADES[grade]).c_str(),
           totals.intake_signed_by_grade[grade]);
    }
    share("League minutes to U21 players", totals.u21_minutes[0],
          totals.league_minutes[0]);
    share("League minutes to U21 players, tier 1", totals.u21_minutes[1],
          totals.league_minutes[1]);
    share("League minutes to U21 players, tier 2", totals.u21_minutes[2],
          totals.league_minutes[2]);
    share("League minutes to players aged 18 or younger", totals.minutes_17_18,
          totals.league_minutes[0]);
    share("League minutes to players aged 19-20", totals.minutes_19_20,
          totals.league_minutes[0]);
    share("League minutes to players aged 21", totals.minutes_21,
          totals.league_minutes[0]);
    share("League starts by U21 players",
          static_cast<double>(totals.u21_starts),
          static_cast<double>(totals.starts));
    mean("Overall change over the season, age 16-18",
         totals.development_by_age[0]);
    mean("Overall change over the season, age 19-21",
         totals.development_by_age[1]);
    mean("Overall change over the season, age 22-24",
         totals.development_by_age[2]);
    return table;
  }

  TextTable transferTable() const
  {
    using enum TransferKind;
    TextTable table{
        "Transfer records by kind (all clubs)", {"Kind", "Moves"}, {}};
    static constexpr std::array<std::pair<TransferKind, const char*>, 6> KINDS =
        {{{Permanent, "Permanent"},
          {Free, "Free"},
          {Loan, "Loan"},
          {LoanReturn, "Loan return"},
          {PreContract, "Pre-contract"},
          {Release, "Release"}}};
    for (const auto& [kind, name] : KINDS)
    {
      const auto found = totals.transfer_kinds.find(kind);
      table.rows.push_back(
          {name, std::to_string(found == totals.transfer_kinds.end()
                                    ? 0
                                    : found->second)});
    }
    return table;
  }

  TextTable injuryTable(
      const std::map<InjuryType, std::vector<double>>& layoffs) const
  {
    TextTable table{"Injury onsets by type (club players)",
                    {"Type", "Injuries", "Share", "Median layoff (days)"},
                    {}};
    for (const auto& [type, days] : layoffs)
    {
      table.rows.push_back(
          {InjuryModel::nameKey(type), std::to_string(days.size()),
           std::format("{:.1f}%",
                       100.0 * static_cast<double>(days.size()) /
                           static_cast<double>(totals.injuries.size())),
           std::format("{:.0f}", medianEstimate(days).value)});
    }
    return table;
  }
};
}  // namespace

LabReport runSeasons(const SeasonOptions& options)
{
  if (options.seasons < 1)
    throw std::invalid_argument("--seasons must be >= 1");
  SeasonRunner runner(options);
  return runner.run();
}
}  // namespace Lab
