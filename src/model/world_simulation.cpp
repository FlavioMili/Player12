// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/world_simulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string_view>
#include <utility>

#include "database/gamedata.h"
#include "database/repositories/inbox_repository.h"
#include "database/repositories/player_repository.h"
#include "database/repositories/scouting_repository.h"
#include "database/repositories/staff_repository.h"
#include "database/repositories/training_repository.h"
#include "database/repositories/world_state_repository.h"
#include "global/global.h"
#include "model/competition.h"
#include "model/injury.h"
#include "model/manager_career.h"
#include "model/match.h"
#include "model/match_report.h"
#include "model/player.h"
#include "model/role_utils.h"
#include "model/season_history.h"
#include "model/staff.h"
#include "model/tactics.h"
#include "model/team.h"
#include "model/world_generation.h"
#include "model/world_rng.h"

namespace
{
using Fitness = WorldTuning::Fitness;

constexpr std::int32_t RECENT_INJURY_DAYS = 60;
constexpr std::size_t MAX_NAMES_IN_MESSAGE = 6;
/** Injuries shorter than this go into the weekly medical digest. [P] */
constexpr std::uint16_t MINOR_INJURY_DAYS = 14;
/** Board confidence lost when cash turns negative / per embargoed month. */
constexpr float CASH_WARNING_CONFIDENCE_HIT = 5.0f;
constexpr float EMBARGO_CONFIDENCE_HIT = 8.0f;

float toUnit(std::uint8_t trait) { return static_cast<float>(trait) / 100.0f; }

// High temperament = composed: morale swings 0.7x-1.3x. [P]
float temperamentScale(const Player& player)
{
  return 1.3f - 0.6f * toUnit(player.getTraits().temperament);
}

float ambitionScale(const Player& player)
{
  return 0.5f + toUnit(player.getTraits().ambition);
}

bool isPhysicalStat(std::string_view stat)
{
  return stat == "Pace" || stat == "Physicality" || stat == "Stamina";
}

float statValue(const Player& player, const char* stat, float fallback)
{
  const auto found = player.getStats().find(stat);
  return found == player.getStats().end() ? fallback : found->second;
}

// Hazard multiplier shared by match and training injuries.
double injuryMultiplier(const Player& player, std::int32_t ordinal)
{
  const PlayerDynamics& dynamics = player.getDynamics();
  // Proneness 50 is average; 10 -> 0.6x, 100 -> 1.5x. [P]
  double multiplier =
      0.5 + static_cast<double>(toUnit(player.getTraits().injury_proneness));
  // Older players get more (mostly muscle) injuries. [S direction, P slope]
  multiplier *= std::exp(0.03 * std::max(0, player.getAge() - 25));
  // Short recovery raises muscle (RR 1.32) and total (RR 1.09) risk; about
  // half of injuries are muscular. [S]
  if (dynamics.last_match_day > 0 && ordinal - dynamics.last_match_day <= 4)
  {
    multiplier *= 0.5 * (Fitness::CONGESTION_MUSCLE_MULTIPLIER +
                         Fitness::CONGESTION_OTHER_MULTIPLIER);
  }
  // A recent injury raises the risk of a recurrence. [S RR 4.8 for the same
  // site; applied to the overall hazard at a fraction]
  if (dynamics.last_injury != InjuryType::None &&
      ordinal - dynamics.last_injury_day <= RECENT_INJURY_DAYS)
    multiplier *= 1.3;
  if (dynamics.condition < 60.0f) multiplier *= 1.2;  // Fatigue. [P]
  return multiplier;
}

std::string joinNames(const std::vector<std::string>& names)
{
  std::string joined;
  for (std::size_t i = 0; i < names.size() && i < MAX_NAMES_IN_MESSAGE; ++i)
  {
    if (i > 0) joined += ", ";
    joined += names[i];
  }
  if (names.size() > MAX_NAMES_IN_MESSAGE) joined += ", ...";
  return joined;
}

std::uint8_t pointsFor(std::uint8_t own, std::uint8_t other)
{
  if (own > other) return 3;
  return own == other ? 1 : 0;
}

MatchOutcome outcomeFor(std::uint8_t own, std::uint8_t other)
{
  using enum MatchOutcome;
  if (own > other) return Win;
  return own == other ? Draw : Loss;
}

float expectedShare(SquadRole role, int age)
{
  using enum SquadRole;
  float share = 0.03f;
  switch (role)
  {
    case KeyPlayer:
      share = 0.85f;
      break;
    case FirstTeam:
      share = 0.70f;
      break;
    case Rotation:
      share = 0.40f;
      break;
    case Backup:
      share = 0.15f;
      break;
    case Fringe:
      break;
  }
  return age <= 19 ? std::min(share, 0.10f) : share;
}

SquadRole roleForRank(std::size_t rank)
{
  using enum SquadRole;
  if (rank < 5) return KeyPlayer;
  if (rank < 11) return FirstTeam;
  if (rank < 16) return Rotation;
  if (rank < 22) return Backup;
  return Fringe;
}

float growthRate(int age)
{
  using Development = WorldTuning::Development;
  if (age <= 19) return Development::GROWTH_RATE_TEEN;
  if (age <= 21) return Development::GROWTH_RATE_20_21;
  if (age <= 23) return Development::GROWTH_RATE_22_23;
  if (age <= 25) return Development::GROWTH_RATE_24_25;
  if (age <= 28) return Development::GROWTH_RATE_26_28;
  return 0.0f;
}

double retirementProbability(const Player& player, bool free_agent,
                             double overall)
{
  const int age = player.getAge();
  // A year without a club ends most careers below the top level: released
  // players drop out of professional football. [P]
  if (free_agent && age < 30) return age < 21 ? 0.15 : 0.35;
  if (age < 30) return 0.0;
  if (age >= 41) return 1.0;
  // Goalkeepers play on about a year and a half longer. [P]
  const double shift = player.getRole() == PlayerRole::GK ? 1.5 : 0.0;
  double probability =
      1.0 / (1.0 + std::exp(-(static_cast<double>(age) - shift - 35.5) / 1.2));
  if (free_agent) probability += 0.5;
  if (overall < 45.0) probability += 0.10;
  return std::clamp(probability, 0.0, 1.0);
}

std::string ordinalText(int position) { return std::to_string(position); }

constexpr std::array<const char*, 4> AREA_KEYS = {
    "POSITION_GROUP_GK", "POSITION_GROUP_DEF", "POSITION_GROUP_MID",
    "POSITION_GROUP_ATT"};
/** Starters per area in a 4-3-3 / 4-4-2 shape. */
constexpr std::array<std::size_t, 4> AREA_STARTERS = {1, 4, 3, 3};

std::size_t areaOf(PlayerRole role)
{
  using enum PlayerRole;
  switch (role)
  {
    case GK:
      return 0;
    case CB:
    case LB:
    case RB:
      return 1;
    case LW:
    case RW:
    case ST:
      return 3;
    default:
      return 2;
  }
}

/** Strength of each area (mean of its likely starters) and its weakest
 * starter, from players sorted by overall (best first). */
struct SquadAreas
{
  std::array<float, 4> score{};
  std::array<PlayerRole, 4> weakest_role{PlayerRole::GK, PlayerRole::CB,
                                         PlayerRole::CM, PlayerRole::ST};
  std::array<float, 4> weakest_overall{};

  std::size_t strongest() const
  {
    return static_cast<std::size_t>(std::ranges::max_element(score) -
                                    score.begin());
  }
  std::size_t weakest() const
  {
    return static_cast<std::size_t>(std::ranges::min_element(score) -
                                    score.begin());
  }
};

SquadAreas squadAreas(
    const std::vector<std::pair<double, const Player*>>& ranked)
{
  SquadAreas areas;
  std::array<std::size_t, 4> counted{};
  std::array<double, 4> totals{};
  for (const auto& [overall, player] : ranked)
  {
    const std::size_t area = areaOf(player->getRole());
    if (counted[area] == AREA_STARTERS[area]) continue;
    ++counted[area];
    totals[area] += overall;
    areas.weakest_role[area] = player->getRole();
    areas.weakest_overall[area] = static_cast<float>(overall);
  }
  for (std::size_t area = 0; area < areas.score.size(); ++area)
  {
    if (counted[area] > 0)
      areas.score[area] =
          static_cast<float>(totals[area] / static_cast<double>(counted[area]));
  }
  return areas;
}

std::string listOrNone(const std::vector<std::string>& names)
{
  return names.empty() ? std::string("@INBOX_NONE") : joinNames(names);
}
}  // namespace

const char* squadRoleKey(SquadRole role)
{
  using enum SquadRole;
  switch (role)
  {
    case KeyPlayer:
      return "SQUAD_ROLE_KEY_PLAYER";
    case FirstTeam:
      return "SQUAD_ROLE_FIRST_TEAM";
    case Rotation:
      return "SQUAD_ROLE_ROTATION";
    case Backup:
      return "SQUAD_ROLE_BACKUP";
    case Fringe:
      break;
  }
  return "SQUAD_ROLE_FRINGE";
}

WorldSimulation::WorldSimulation(std::shared_ptr<GameData> gd)
    : gamedata(std::move(gd)),
      scouting(gamedata),
      interactions(gamedata,
                   [this](PlayerID player_id) { return squadRole(player_id); }),
      stories(gamedata),
      youth(gamedata)
{
}

void WorldSimulation::setStandingsProvider(
    std::function<std::vector<TeamID>(LeagueID)> provider)
{
  standings_provider = std::move(provider);
}

void WorldSimulation::setLoanCheck(std::function<bool(PlayerID)> is_on_loan)
{
  loan_check = std::move(is_on_loan);
  youth.setLoanCheck(loan_check);
}

void WorldSimulation::setLineupGapFiller(
    std::function<void(Team&, const Player&)> filler)
{
  lineup_gap_filler = std::move(filler);
}

void WorldSimulation::setFreeAgentHold(std::function<bool(PlayerID)> is_held)
{
  free_agent_hold = std::move(is_held);
}

void WorldSimulation::setSeasonHistoryProvider(
    std::function<const std::vector<SeasonHistoryEntry>&()> provider)
{
  season_history_provider = std::move(provider);
}

void WorldSimulation::setFixtureOutlookProvider(
    std::function<void(const GameDateValue&, TrainingSystem::FixtureOutlook&)>
        provider)
{
  fixture_outlook_provider = std::move(provider);
}

void WorldSimulation::setInjuryRiskProvider(
    std::function<double(PlayerID, const GameDateValue&)> provider)
{
  injury_risk_provider = std::move(provider);
}

std::uint16_t WorldSimulation::seasonYear(const GameDateValue& date)
{
  return date.month >= 7 ? date.year
                         : static_cast<std::uint16_t>(date.year - 1);
}

const WorldSimulation::FocusStats& WorldSimulation::focusFor(
    const Player& player) const
{
  const std::scoped_lock lock(focus_cache_mutex);
  // Built on first use: the stats configuration is loaded with the save.
  if (focus_by_category.empty())
  {
    for (const auto& [category, focus] : gamedata->getStatsConfig().role_focus)
    {
      FocusStats stats;
      stats.all_weight = 0.0;
      stats.non_physical_weight = 0.0;
      const std::size_t count =
          std::min(focus.stats.size(), focus.weights.size());
      for (std::size_t i = 0; i < count; ++i)
      {
        stats.all.push_back(focus.stats[i]);
        stats.all_weight += focus.weights[i];
        if (!isPhysicalStat(focus.stats[i]))
        {
          stats.non_physical.push_back(focus.stats[i]);
          stats.non_physical_weight += focus.weights[i];
        }
      }
      focus_by_category.try_emplace(category, std::move(stats));
    }
  }
  static const FocusStats EMPTY;
  const auto found =
      focus_by_category.find(RoleUtils::getBroadCategory(player.getRole()));
  return found == focus_by_category.end() ? EMPTY : found->second;
}

void WorldSimulation::post(const GameDateValue& date, InboxCategory category,
                           std::string title_key, std::string body_key,
                           std::vector<std::string> args,
                           std::optional<PlayerID> player_id,
                           std::optional<TeamID> team_id, bool read)
{
  InboxMessage message;
  message.date = date;
  message.category = category;
  message.title_key = std::move(title_key);
  message.body_key = std::move(body_key);
  message.args = std::move(args);
  message.player_id = player_id;
  message.team_id = team_id;
  message.read = read;
  inbox.add(std::move(message));
}

// ---------------------------------------------------------------------------
// Day loop
// ---------------------------------------------------------------------------

void WorldSimulation::onDayAdvanced(const GameDateValue& date,
                                    TeamID managed_team_id)
{
  if (managed_team_id != FREE_AGENTS_TEAM_ID &&
      board.team_id != managed_team_id)
    ensureBoard(date, managed_team_id, true);

  const std::int32_t ordinal = dayOrdinal(date);
  // No training sessions during the June holiday.
  const bool training_period = date.month != 6;
  if (fixture_outlook_provider)
  {
    fixture_outlook.clear();
    fixture_outlook_provider(date, fixture_outlook);
  }
  TrainingSystem::rollWorkloads(*gamedata);
  TrainingSystem::planDay(*gamedata, ordinal,
                          fixture_outlook_provider ? &fixture_outlook : nullptr,
                          training_period);
  processDaily(date, ordinal, managed_team_id);
  if (ordinal % 7 == 0) processWeekly(date, managed_team_id);
  if (date.day == 1)
  {
    processMonthly(date, managed_team_id);
    StaffModel::refreshMarket(*gamedata, ordinal);
  }
  if (training_period && ordinal % 14 == 0 &&
      managed_team_id != FREE_AGENTS_TEAM_ID)
    postTrainingWarning(date, managed_team_id);
  youth.onDayAdvanced(date, managed_team_id, inbox);
  awards.onMonthStart(*gamedata, date, managed_team_id, inbox, board);
  facility_projects.onDay(*gamedata, date, managed_team_id, inbox);
  preseason.onDay(*gamedata, date, managed_team_id, inbox);
  if ((date.month == 1 || date.month == 4) && date.day == 1)
    sendContractNotices(date, managed_team_id);
  refreshAiLineups(managed_team_id);
  dropUnavailableFromAiLineups(managed_team_id);
  if (managed_team_id != FREE_AGENTS_TEAM_ID)
    scouting.setManagedTeam(managed_team_id);
  scouting.onDayAdvanced(date, inbox);
  interactions.onDayAdvanced(date, managed_team_id, inbox);
  StoryDayContext story_context;
  story_context.awards = &awards;
  if (const auto next = fixture_outlook.find(managed_team_id);
      fixture_outlook_provider && next != fixture_outlook.end())
    story_context.days_to_match = next->second;
  stories.onDayAdvanced(date, managed_team_id, interactions, inbox,
                        story_context);
}

void WorldSimulation::processDaily(const GameDateValue& date,
                                   std::int32_t ordinal, TeamID managed_team_id)
{
  // June is the players' holiday: no training load. [P]
  const bool training_period = date.month != 6;
  const std::uint64_t seed = gamedata->getWorldSeed();
  for (auto& [player_id, player] : gamedata->getPlayers())
  {
    PlayerDynamics& dynamics = player.mutableDynamics();

    // Residual fatigue decays exponentially (48 h time constant, slower for
    // veterans, faster for high stamina). [S/P]
    const float stamina = statValue(player, "Stamina", 60.0f);
    const float tau =
        Fitness::FATIGUE_TIME_CONSTANT_HOURS *
        (1.0f + 0.02f * static_cast<float>(std::max(0, player.getAge() - 26))) *
        (1.15f - 0.3f * stamina / 100.0f);
    dynamics.condition =
        100.0f - (100.0f - dynamics.condition) * std::exp(-24.0f / tau);

    if (dynamics.last_match_day != ordinal)
    {
      const float decay = dynamics.injury_days > 0
                              ? 2.0f * Fitness::SHARPNESS_DAILY_DECAY
                              : Fitness::SHARPNESS_DAILY_DECAY;
      dynamics.sharpness =
          std::max(Fitness::SHARPNESS_FLOOR, dynamics.sharpness - decay);
    }

    if (dynamics.injury_days > 0)
    {
      --dynamics.injury_days;
      if (dynamics.injury_days == 0)
      {
        dynamics.injury = InjuryType::None;
        if (player.getTeamId() == managed_team_id)
          week_recoveries.push_back(player.getName());
        else
        {
          lineup_dirty.insert(player.getTeamId());
        }
      }
      continue;
    }

    // Training injuries: ECIS 3.5-4.1 injuries per 1000 training hours [S],
    // scaled by the session's load (the Balanced week averages the
    // calibrated hours), workload spikes and the medical staff.
    if (!training_period || player.getTeamId() == FREE_AGENTS_TEAM_ID) continue;
    const double exposure =
        TrainingSystem::trainPlayer(*gamedata, player, ordinal);
    if (exposure <= 0.0) continue;
    const double hazard =
        Fitness::TRAINING_INJURY_RATE_PER_HOUR *
        Fitness::TRAINING_HOURS_PER_DAY * exposure *
        injuryMultiplier(player, ordinal) *
        (injury_risk_provider ? injury_risk_provider(player_id, date) : 1.0);
    if (WorldRng::hashUniform(seed, RngDomain::TrainingInjury,
                              static_cast<std::uint64_t>(ordinal),
                              player_id) < 1.0 - std::exp(-hazard))
      injure(player, date, ordinal, false, managed_team_id);
  }
}

void WorldSimulation::injure(Player& player, const GameDateValue& date,
                             std::int32_t ordinal, bool in_match,
                             TeamID managed_team_id)
{
  PlayerDynamics& dynamics = player.mutableDynamics();
  if (dynamics.injury_days > 0) return;
  WorldRng rng = WorldRng::stream(
      gamedata->getWorldSeed(),
      in_match ? RngDomain::MatchInjury : RngDomain::TrainingInjury,
      static_cast<std::uint64_t>(ordinal), player.getId() ^ 0xD1A6ULL);
  const bool recent = dynamics.last_injury != InjuryType::None &&
                      ordinal - dynamics.last_injury_day <= RECENT_INJURY_DAYS;
  const Injury injury = InjuryModel::draw(
      rng, in_match ? InjuryContext::Match : InjuryContext::Training,
      dynamics.last_injury, recent);
  // Better physios and sports science shorten the layoff (0.85x-1.12x). [P]
  const float layoff =
      player.getTeamId() == FREE_AGENTS_TEAM_ID
          ? 1.0f
          : gamedata->getStaff().effects(player.getTeamId()).layoff_multiplier *
                facility_projects.layoffMultiplier(player.getTeamId());
  const auto days = static_cast<std::uint16_t>(
      std::max(1L, std::lround(static_cast<float>(injury.days) * layoff)));
  dynamics.injury = injury.type;
  dynamics.injury_days = days;
  dynamics.last_injury = injury.type;
  dynamics.last_injury_day = ordinal;

  // Only ~65% of ACL patients return to the same level: a permanent loss of
  // explosiveness for the rest. [S]
  if (injury.type == InjuryType::KneeAcl && rng.chance(0.35))
  {
    auto stats = player.getStats();
    for (auto& [name, value] : stats)
    {
      if (isPhysicalStat(name)) value *= 0.93f;
    }
    player.setStats(stats);
    player.setPotential(player.getPotential() - 3.0f);
  }

  if (player.getTeamId() == managed_team_id && days < MINOR_INJURY_DAYS)
  {
    week_minor_injuries.push_back(
        std::format("{} ({})", player.getName(), days));
  }
  else if (player.getTeamId() == managed_team_id)
  {
    post(
        date, InboxCategory::Injury, "INBOX_INJURY_TITLE", "INBOX_INJURY_BODY",
        {player.getName(), std::string("@") + InjuryModel::nameKey(injury.type),
         std::to_string(days)},
        player.getId());
  }
  else
  {
    lineup_dirty.insert(player.getTeamId());
  }
}

void WorldSimulation::refreshAiLineups(TeamID managed_team_id)
{
  for (const TeamID team_id : lineup_dirty)
  {
    if (team_id == managed_team_id || team_id == FREE_AGENTS_TEAM_ID) continue;
    if (auto team = gamedata->getTeam(team_id))
    {
      team->get().generateStartingXI(*gamedata, gamedata->getStatsConfig());
      // AI managers give the new eleven roles that suit them.
      Tactics::assignRolesToFit(team->get().getStrategy(),
                                team->get().getLineup());
    }
  }
  lineup_dirty.clear();
}

void WorldSimulation::dropUnavailableFromAiLineups(TeamID managed_team_id)
{
  // Catches line-ups restored from a save or changed by transfers.
  const auto unavailable = [](const Player* player)
  { return player && !player->isAvailable(); };
  for (auto& [team_id, team] : gamedata->getTeams())
  {
    if (team_id == managed_team_id || team_id == FREE_AGENTS_TEAM_ID) continue;
    const Lineup& lineup = team.getLineup();
    const bool needs_change =
        unavailable(lineup.getGoalkeeper()) ||
        std::ranges::any_of(lineup.getOutfieldPlayers(),
                            [&](const auto& positioned)
                            { return unavailable(positioned.player); });
    if (needs_change)
      team.generateStartingXI(*gamedata, gamedata->getStatsConfig());
    // Roles follow the eleven; new worlds and older saves get theirs here.
    const Strategy& strategy = team.getStrategy();
    const bool roles_missing = std::ranges::any_of(
        team.getLineup().getOutfieldPlayers(),
        [&strategy](const auto& positioned)
        { return !strategy.findSlot(positioned.position); });
    if (needs_change || roles_missing)
      Tactics::assignRolesToFit(team.getStrategy(), team.getLineup());
  }
}

// ---------------------------------------------------------------------------
// Weekly: payroll, training, morale
// ---------------------------------------------------------------------------

void WorldSimulation::processWeekly(const GameDateValue& date,
                                    TeamID managed_team_id)
{
  const std::int32_t ordinal = dayOrdinal(date);
  for (auto& [team_id, team] : gamedata->getTeams())
  {
    if (team_id == FREE_AGENTS_TEAM_ID) continue;
    if (const std::int64_t payroll =
            team.getFinances().getCurrentWageSpending(*gamedata, team);
        payroll > 0)
      team.getFinances().record(date, FinanceCategory::Wages, -payroll);
    updateWeeklyMorale(team);
  }
  reviewCash(date, managed_team_id, false);
  postMedicalDigest(date, managed_team_id);

  // Training: T = 0.7-1.3 from facilities, coaches, the week's volume and
  // the preset; free agents train alone. [P]
  const StatsConfig& config = gamedata->getStatsConfig();
  mentoring.onWeek(*gamedata);
  for (auto& [player_id, player] : gamedata->getPlayers())
  {
    const auto before = static_cast<float>(player.getOverall(config));
    developPlayer(player,
                  TrainingSystem::trainingQuality(*gamedata, player) *
                      youth.developmentMultiplier(player) *
                      mentoring.developmentMultiplier(*gamedata, player_id),
                  ordinal);
    TrainingSystem::recordWeeklyGrowth(
        *gamedata, player_id,
        static_cast<float>(player.getOverall(config)) - before);
    PlayerDynamics& dynamics = player.mutableDynamics();
    dynamics.week_minutes = 0;
    if (dynamics.transfer_interest_weeks > 0)
      --dynamics.transfer_interest_weeks;
  }
  TrainingSystem::endWeek(*gamedata);
}

void WorldSimulation::developPlayer(Player& player, float training_quality,
                                    std::int32_t ordinal)
{
  const PlayerDynamics& dynamics = player.getDynamics();
  const int age = player.getAge();
  const float rate = growthRate(age);
  if (rate <= 0.0f || dynamics.injury_days > 0) return;
  const StatsConfig& config = gamedata->getStatsConfig();
  const auto overall = static_cast<float>(player.getOverall(config));
  const float gap = player.getPotential() - overall;
  if (gap <= 0.0f) return;

  // dCA = r(age) * gap * T * M * P (world-realism 9.1), weekly slice. [P]
  const float minutes_factor =
      0.6f + 0.4f * std::min(1.0f, dynamics.playing_share / 0.6f);
  const float professionalism =
      0.8f + 0.4f * toUnit(player.getTraits().professionalism);
  const float morale = 0.9f + 0.2f * dynamics.morale / 100.0f;
  WorldRng rng =
      WorldRng::stream(gamedata->getWorldSeed(), RngDomain::Development,
                       static_cast<std::uint64_t>(ordinal), player.getId());
  const float noise = std::max(0.0f, 1.0f + rng.normal(0.0f, 0.3f));
  const float growth = rate / 52.0f * gap * training_quality * minutes_factor *
                       professionalism * morale * noise;
  if (growth <= 0.0f) return;

  // Physical attributes stop improving after ~25 (peak speed 25.7). [S]
  const FocusStats& focus = focusFor(player);
  const bool physical_growth = age <= 25;
  const auto& stats = physical_growth ? focus.all : focus.non_physical;
  const double weight =
      physical_growth ? focus.all_weight : focus.non_physical_weight;
  if (stats.empty() || weight <= 0.0) return;
  // Part of the growth follows the schedule and the individual focus.
  const float role_growth =
      TrainingSystem::directGrowth(*gamedata, player, growth);
  player.train(stats,
               static_cast<float>(static_cast<double>(role_growth) / weight));
}

void WorldSimulation::updateWeeklyMorale(Team& team)
{
  using Morale = WorldTuning::Morale;
  const StatsConfig& config = gamedata->getStatsConfig();
  auto& players = gamedata->getPlayers();

  struct Ranked
  {
    Player* player;
    double overall;
  };
  std::vector<Ranked> ranked;
  ranked.reserve(team.getPlayerIDs().size());
  double wage_index_total = 0.0;
  std::int64_t payroll = 0;
  std::uint16_t max_week_minutes = 0;
  for (const PlayerID player_id : team.getPlayerIDs())
  {
    const auto found = players.find(player_id);
    if (found == players.end()) continue;
    Player& player = found->second;
    const double overall = player.getOverall(config);
    ranked.push_back({&player, overall});
    wage_index_total += ClubEconomy::wageIndex(overall, player.getAge());
    payroll += player.getWage();
    max_week_minutes =
        std::max(max_week_minutes, player.getDynamics().week_minutes);
  }
  // Ties by id: the squad list's order differs after a reload.
  std::ranges::sort(ranked,
                    [](const Ranked& a, const Ranked& b)
                    {
                      return a.overall != b.overall
                                 ? a.overall > b.overall
                                 : a.player->getId() < b.player->getId();
                    });
  // Matches this week: the most used player played every minute of them.
  const int matches = (max_week_minutes + 89) / 90;
  const double wage_scale =
      wage_index_total > 0.0 ? static_cast<double>(payroll) / wage_index_total
                             : 0.0;

  for (std::size_t rank = 0; rank < ranked.size(); ++rank)
  {
    Player& player = *ranked[rank].player;
    PlayerDynamics& dynamics = player.mutableDynamics();
    const float ambition = ambitionScale(player);

    if (matches > 0)
    {
      const float share = static_cast<float>(dynamics.week_minutes) /
                          (90.0f * static_cast<float>(matches));
      dynamics.playing_share = 0.7f * dynamics.playing_share + 0.3f * share;
    }
    // A status given by the manager sets the minutes the player expects; a
    // status well below his standing in the squad hurts on its own.
    const std::optional<SquadStatus> status =
        team.getId() == board.team_id
            ? squad_statuses.get(player.getId(), team.getId())
            : std::nullopt;
    const SquadStatus deserved =
        SquadStatusModel::deserved(rank, player.getAge());
    const float expected = expectedShare(
        status ? SquadStatusModel::toSquadRole(
                     SquadStatusModel::expectation(*status, deserved))
               : roleForRank(rank),
        player.getAge());
    const float playing_term =
        std::min(5.0f, (dynamics.playing_share - expected) * 40.0f * ambition) +
        (status ? SquadStatusModel::moraleOffset(*status, deserved, ambition)
                : 0.0f);

    float wage_term = 0.0f;
    if (const double fair =
            wage_scale *
            ClubEconomy::wageIndex(ranked[rank].overall, player.getAge());
        fair > 0.0 && player.getWage() > 0)
    {
      wage_term =
          std::clamp(
              static_cast<float>(
                  std::log(static_cast<double>(player.getWage()) / fair) *
                  15.0),
              -12.0f, 6.0f) *
          ambition;
    }
    float transfer_term = 0.0f;
    if (dynamics.transfer_interest_weeks > 0)
    {
      transfer_term = -8.0f * toUnit(player.getTraits().ambition) *
                      (1.2f - toUnit(player.getTraits().loyalty));
    }
    if (player.getTransferStatus() == TransferStatus::Listed)
      transfer_term -= 4.0f * toUnit(player.getTraits().loyalty);

    const float target = Morale::NEUTRAL + playing_term + wage_term +
                         transfer_term +
                         interactions.moraleTargetOffset(player.getId());
    dynamics.morale = std::clamp(Morale::PERSISTENCE * dynamics.morale +
                                     (1.0f - Morale::PERSISTENCE) * target,
                                 0.0f, 100.0f);
  }
}

// ---------------------------------------------------------------------------
// Monthly: revenue, costs, summaries, board
// ---------------------------------------------------------------------------

void WorldSimulation::processMonthly(const GameDateValue& date,
                                     TeamID managed_team_id)
{
  const auto economies = buildLeagueEconomies(*gamedata);
  // July is the rollover month: the parachutes of a season are paid from
  // August to June.
  const auto parachute = date.month == 7 ? std::unordered_map<TeamID, double>{}
                                         : parachutes(economies);
  for (auto& [team_id, team] : gamedata->getTeams())
  {
    if (team_id == FREE_AGENTS_TEAM_ID) continue;
    const auto economy_it = economies.find(team.getLeagueId());
    if (economy_it == economies.end()) continue;
    const LeagueEconomy& economy = economy_it->second;
    const ClubProfile& profile = team.getProfile();
    Finances& finances = team.getFinances();
    const auto round = [](double value)
    { return static_cast<std::int64_t>(std::llround(value)); };
    finances.record(date, FinanceCategory::Broadcasting,
                    round(ClubEconomy::monthlyBroadcasting(economy)));
    if (const auto paid = parachute.find(team_id); paid != parachute.end())
      finances.record(
          date, FinanceCategory::Broadcasting,
          round(paid->second / WorldTuning::Finance::PARACHUTE_INSTALMENTS));
    finances.record(
        date, FinanceCategory::Sponsorship,
        round(ClubEconomy::monthlySponsorship(economy, profile.reputation)));
    // Football staff are paid their contracts; the other non-playing staff
    // cost the rest of the non-player wage share.
    const double staff_costs =
        ClubEconomy::monthlyStaffCosts(economy, profile.reputation) *
            (1.0 - StaffModel::FOOTBALL_STAFF_SHARE) +
        static_cast<double>(
            gamedata->getStaff().effects(team_id).weekly_payroll) *
            52.0 / 12.0;
    finances.record(date, FinanceCategory::Staff, -round(staff_costs));
    finances.record(
        date, FinanceCategory::Facilities,
        -round(ClubEconomy::monthlyFacilityCosts(economy, profile)));
    if (team_id != managed_team_id) rescueByOwner(date, team);
  }
  // The summer belongs to the market; after it the unsigned move on.
  if (date.month < 6 || date.month > 8) clearFreeAgentPool(date);

  const auto managed = gamedata->getTeam(managed_team_id);
  if (managed_team_id == FREE_AGENTS_TEAM_ID || !managed) return;
  const Team& team = managed->get();
  const Finances& finances = team.getFinances();
  reviewCash(date, managed_team_id, true);

  // Summary of the previous month (excluding today's postings).
  const GameDateValue month_end = date - 1;
  const GameDateValue month_start(month_end.year, month_end.month, 1);
  const FinanceSummary summary = finances.summarize(month_start, month_end);
  post(date, InboxCategory::Finance, "INBOX_FINANCE_TITLE",
       "INBOX_FINANCE_BODY",
       {std::format("{:02}/{}", month_end.month, month_end.year),
        formatMoney(summary.income), formatMoney(summary.expenses),
        formatMoney(summary.net()), formatMoney(finances.getBalance())},
       std::nullopt, managed_team_id, finances.getBalance() >= 0);

  // Monthly board review. From June to August there is no league to judge:
  // the board watches the money, and the season itself is judged when it
  // ends (Game::endSeason).
  const bool in_season = date.month < 6 || date.month > 8;
  if (board.team_id != managed_team_id ||
      (in_season && board.league_matches == 0))
    return;
  const std::vector<TeamID> table = standings(team.getLeagueId());
  const auto found = std::ranges::find(table, managed_team_id);
  const int position =
      found == table.end()
          ? 0
          : static_cast<int>(std::distance(table.begin(), found)) + 1;
  const int league_size = table.empty() ? 20 : static_cast<int>(table.size());
  const BoardReviewOutcome outcome = BoardModel::monthlyReview(
      board, position, league_size, finances.getBalance() < 0,
      finances.getCurrentWageSpending(*gamedata, team) >
          finances.getWageBudget(),
      in_season);
  // A dismissal is announced once, by the career, when the job ends; on
  // 1 July the season verdict speaks for the board.
  if (outcome == BoardReviewOutcome::Warning &&
      !(date.month == 7 && date.day == 1))
  {
    post(date, InboxCategory::Board, "INBOX_BOARD_WARNING_TITLE",
         "INBOX_BOARD_WARNING_BODY",
         {std::to_string(std::lround(board.confidence)),
          std::string("@") + BoardModel::objectiveKey(board.objective)},
         std::nullopt, managed_team_id);
  }
}

std::unordered_map<TeamID, double> WorldSimulation::parachutes(
    const std::unordered_map<LeagueID, LeagueEconomy>& economies,
    std::vector<TeamID>* relegated_now) const
{
  std::unordered_map<TeamID, double> paid;
  if (!season_history_provider) return paid;
  const std::vector<SeasonHistoryEntry>& history = season_history_provider();
  std::uint16_t latest = 0;
  for (const SeasonHistoryEntry& entry : history)
    latest = std::max(latest, entry.season);
  for (const SeasonHistoryEntry& entry : history)
  {
    const int seasons_ago = static_cast<int>(latest) - entry.season;
    if (entry.competition_type != MatchType::LEAGUE || seasons_ago < 0 ||
        seasons_ago >=
            static_cast<int>(WorldTuning::Finance::PARACHUTE_SHARES.size()) ||
        Competitions::leagueTier(*gamedata, entry.competition_id) != 1)
      continue;
    const auto economy = economies.find(entry.competition_id);
    for (const TeamID team_id : entry.relegated)
    {
      if (const auto team = gamedata->getTeam(team_id);
          !team ||
          Competitions::leagueTier(*gamedata, team->get().getLeagueId()) == 1)
        continue;
      if (seasons_ago == 0 && relegated_now) relegated_now->push_back(team_id);
      if (economy == economies.end()) continue;
      double& amount = paid[team_id];
      amount = std::max(
          amount, ClubEconomy::parachutePayment(economy->second, seasons_ago));
    }
  }
  return paid;
}

void WorldSimulation::applyRelegationWageCuts(
    const std::vector<TeamID>& relegated, TeamID managed_team_id)
{
  const auto keep =
      static_cast<double>(1.0f - WorldTuning::Finance::RELEGATION_WAGE_CUT);
  for (const TeamID team_id : relegated)
  {
    if (team_id == managed_team_id) continue;
    const auto team = gamedata->getTeam(team_id);
    if (!team) continue;
    for (const PlayerID player_id : team->get().getPlayerIDs())
    {
      // Loanees are paid under their parent club's contract; scholars
      // have no clause.
      if (loan_check && loan_check(player_id)) continue;
      Player& player = gamedata->getPlayers().at(player_id);
      if (player.isAcademyPlayer()) continue;
      player.setWage(static_cast<std::uint32_t>(
          std::llround(static_cast<double>(player.getWage()) * keep)));
    }
  }
}

void WorldSimulation::rescueByOwner(const GameDateValue& date, Team& team) const
{
  using enum OwnerType;
  using Finance = WorldTuning::Finance;
  Finances& finances = team.getFinances();
  const std::int64_t rescue = ClubEconomy::ownerRescue(
      finances.getBalance(), finances.getCurrentWageSpending(*gamedata, team));
  if (rescue <= 0) return;
  float chance = 0.0f;
  switch (const ClubVision vision = ManagerMarketModel::clubVision(
              gamedata->getWorldSeed(), team.getId(), team.getReputation(),
              team.getProfile().youth_facilities, 0, 0);
          vision.owner)
  {
    case ImpatientBenefactor:
      chance = Finance::BENEFACTOR_RESCUE_CHANCE;
      break;
    case Ambitious:
      chance = Finance::AMBITIOUS_RESCUE_CHANCE;
      break;
    case Patient:
      chance = Finance::PATIENT_RESCUE_CHANCE;
      break;
    case MemberOwned:
      break;
  }
  if (WorldRng::hashUniform(gamedata->getWorldSeed(), RngDomain::Transfers,
                            static_cast<std::uint64_t>(dayOrdinal(date)),
                            mixHash(team.getId(), 0x4F57'4E52ULL)) <
      static_cast<double>(chance))
    finances.record(date, FinanceCategory::Investment, rescue);
}

void WorldSimulation::clearFreeAgentPool(const GameDateValue& date)
{
  using FreeAgents = WorldTuning::FreeAgents;
  const auto pool = gamedata->getTeam(FREE_AGENTS_TEAM_ID);
  if (!pool) return;
  const StatsConfig& config = gamedata->getStatsConfig();
  // Reference: the weakest quarter of second-tier senior players.
  std::vector<double> second_tier;
  for (const auto& [team_id, team] : gamedata->getTeams())
  {
    if (team_id == FREE_AGENTS_TEAM_ID ||
        Competitions::leagueTier(*gamedata, team.getLeagueId()) == 1)
      continue;
    for (const PlayerID player_id : team.getPlayerIDs())
    {
      const Player& player = gamedata->getPlayers().at(player_id);
      if (!player.isAcademyPlayer())
        second_tier.push_back(player.getOverall(config));
    }
  }
  if (second_tier.empty()) return;
  const auto quantile =
      second_tier.begin() +
      static_cast<std::ptrdiff_t>(FreeAgents::LEVEL_QUANTILE *
                                  static_cast<double>(second_tier.size() - 1));
  std::ranges::nth_element(second_tier, quantile);
  const double level = *quantile;

  const std::uint64_t seed = gamedata->getWorldSeed();
  const auto ordinal = static_cast<std::uint64_t>(dayOrdinal(date));
  std::vector<PlayerID> leaving;
  for (const PlayerID player_id : pool->get().getPlayerIDs())
  {
    const Player& player = gamedata->getPlayers().at(player_id);
    if (free_agent_hold && free_agent_hold(player_id)) continue;
    const int age = player.getAge();
    double chance = FreeAgents::EXIT_CHANCE_VETERAN;
    if (age <= 21)
      chance = FreeAgents::EXIT_CHANCE_YOUNG;
    else if (age <= 27)
      chance = FreeAgents::EXIT_CHANCE_PRIME;
    else if (age <= 31)
      chance = FreeAgents::EXIT_CHANCE_LATE;
    const double overall = player.getOverall(config);
    if (const double margin = FreeAgents::LEVEL_MARGIN;
        overall >= level + margin)
      chance *= FreeAgents::ABOVE_LEVEL_FACTOR;
    else if (overall < level - margin)
      chance *= FreeAgents::BELOW_LEVEL_FACTOR;
    if (age <= 21 && static_cast<double>(player.getPotential()) >=
                         level + FreeAgents::PROSPECT_POTENTIAL_MARGIN)
      chance *= FreeAgents::PROSPECT_FACTOR;
    if (WorldRng::hashUniform(seed, RngDomain::Retirement, ordinal, player_id) <
        chance)
      leaving.push_back(player_id);
  }
  std::ranges::sort(leaving);
  for (const PlayerID player_id : leaving)
  {
    pool->get().removePlayerID(player_id);
    gamedata->removePlayer(player_id);
  }
}

void WorldSimulation::ensureBoard(const GameDateValue& date,
                                  TeamID managed_team_id, bool new_season,
                                  bool post_objective)
{
  const auto managed = gamedata->getTeam(managed_team_id);
  if (!managed) return;
  const Team& team = managed->get();

  // Expected finish = rank of the wage bill in the league. [S Szymanski]
  std::vector<std::pair<std::int64_t, TeamID>> wage_bills;
  for (const auto& [team_id, other] : gamedata->getTeams())
  {
    if (team_id == FREE_AGENTS_TEAM_ID ||
        other.getLeagueId() != team.getLeagueId())
      continue;
    wage_bills.emplace_back(
        other.getFinances().getCurrentWageSpending(*gamedata, other), team_id);
  }
  std::ranges::sort(wage_bills, std::greater<>{});
  const auto rank = std::ranges::find(wage_bills, managed_team_id,
                                      &std::pair<std::int64_t, TeamID>::second);
  const int expected =
      static_cast<int>(std::distance(wage_bills.begin(), rank)) + 1;
  const auto league_size = static_cast<int>(wage_bills.size());

  if (const bool same_club = board.team_id == managed_team_id; !same_club)
  {
    board = BoardState{};
    board.team_id = managed_team_id;
    board.confidence = WorldTuning::Board::INITIAL_CONFIDENCE;
  }
  else if (new_season)
  {
    // Confidence carries over: the season verdict has already moved it.
    board.low_reviews = 0;
  }
  board.season_year = seasonYear(date);
  board.expected_position = static_cast<std::uint8_t>(expected);
  board.objective = BoardModel::objectiveFor(expected, league_size);
  board.target_position =
      BoardModel::targetPosition(board.objective, league_size);
  // The other targets follow the club's stature and its owners' vision.
  const Finances& finances = team.getFinances();
  const ClubVision vision = ManagerMarketModel::clubVision(
      gamedata->getWorldSeed(), managed_team_id, team.getReputation(),
      team.getProfile().youth_facilities, finances.getBalance(), 0);
  board.cup_objective = BoardModel::cupObjectiveFor(
      board.objective, Competitions::leagueTier(*gamedata, team.getLeagueId()));
  board.finance_objective = BoardModel::financeObjectiveFor(
      vision.tight_budget, finances.getBalance());
  board.youth_target = BoardModel::youthTargetFor(vision.youth_focus);
  board.start_balance = finances.getBalance();
  board.targets_set = true;
  board.league_matches = 0;
  board.result_count = 0;
  board.recent_deltas = {};

  if (!post_objective) return;
  post(date, InboxCategory::Board, "INBOX_BOARD_OBJECTIVE_TITLE",
       "INBOX_BOARD_OBJECTIVES_BODY",
       {std::string("@") + BoardModel::objectiveKey(board.objective),
        ordinalText(expected), team.getName(),
        std::string("@") + BoardModel::cupObjectiveKey(board.cup_objective),
        std::string("@") +
            BoardModel::financeObjectiveKey(board.finance_objective),
        std::string("@") + BoardModel::youthTargetKey(board.youth_target)},
       std::nullopt, managed_team_id);
}

void WorldSimulation::adjustBoardConfidence(float delta)
{
  if (board.team_id == FREE_AGENTS_TEAM_ID) return;
  BoardModel::adjustConfidence(board, delta);
}

// ---------------------------------------------------------------------------
// Taking charge, cash review and digests
// ---------------------------------------------------------------------------

void WorldSimulation::onManagedTeamSelected(
    const GameDateValue& date, TeamID team_id,
    const std::vector<UpcomingFixture>& schedule)
{
  youth.ensureReady();
  const auto team = gamedata->getTeam(team_id);
  if (!team || team_id == FREE_AGENTS_TEAM_ID || board.team_id == team_id)
    return;
  // A new job starts with a clean desk.
  if (board.team_id != FREE_AGENTS_TEAM_ID) inbox.restore({});
  ensureBoard(date, team_id, true, false);
  cash_warning_sent = false;
  transfer_embargo = false;
  week_recoveries.clear();
  week_minor_injuries.clear();
  scouting.setManagedTeam(team_id);

  const Team& club = team->get();
  const Finances& finances = club.getFinances();
  post(date, InboxCategory::Board, "INBOX_BOARD_WELCOME_TITLE",
       "INBOX_BOARD_WELCOME_BODY",
       {club.getName(),
        std::string("@") + BoardModel::objectiveKey(board.objective),
        ordinalText(board.expected_position),
        formatMoney(finances.getTransferBudget()),
        formatMoney(finances.getWageBudget()),
        formatMoney(finances.getCurrentWageSpending(*gamedata, club)),
        formatMoney(finances.getBalance()),
        std::string("@") + BoardModel::cupObjectiveKey(board.cup_objective),
        std::string("@") +
            BoardModel::financeObjectiveKey(board.finance_objective),
        std::string("@") + BoardModel::youthTargetKey(board.youth_target)},
       std::nullopt, team_id);
  postSquadReport(date, club);
  postPreseasonSchedule(date, club, schedule);
  postScoutSuggestion(date, club);
}

void WorldSimulation::onManagerLeft()
{
  board = BoardState{};
  // The camp and tour were booked for the club he leaves.
  preseason.clear();
  cash_warning_sent = false;
  transfer_embargo = false;
  week_recoveries.clear();
  week_minor_injuries.clear();
  scouting.setManagedTeam(FREE_AGENTS_TEAM_ID);
}

bool WorldSimulation::isTransferEmbargoed(TeamID team_id) const
{
  if (!transfer_embargo || team_id != board.team_id) return false;
  const auto team = gamedata->getTeam(team_id);
  return team && team->get().getFinances().getBalance() < 0;
}

void WorldSimulation::reviewCash(const GameDateValue& date,
                                 TeamID managed_team_id, bool monthly)
{
  if (managed_team_id == FREE_AGENTS_TEAM_ID ||
      board.team_id != managed_team_id)
    return;
  const auto team = gamedata->getTeam(managed_team_id);
  if (!team) return;
  const std::int64_t balance = team->get().getFinances().getBalance();
  if (balance >= 0)
  {
    if (transfer_embargo)
    {
      post(date, InboxCategory::Board, "INBOX_BOARD_EMBARGO_LIFTED_TITLE",
           "INBOX_BOARD_EMBARGO_LIFTED_BODY", {formatMoney(balance)},
           std::nullopt, managed_team_id);
    }
    cash_warning_sent = false;
    transfer_embargo = false;
    return;
  }
  // First a warning; a monthly review that still finds the club in the red
  // freezes transfer spending until the balance recovers.
  if (!cash_warning_sent)
  {
    cash_warning_sent = true;
    board.confidence =
        std::max(0.0f, board.confidence - CASH_WARNING_CONFIDENCE_HIT);
    post(date, InboxCategory::Board, "INBOX_BOARD_CASH_WARNING_TITLE",
         "INBOX_BOARD_CASH_WARNING_BODY", {formatMoney(balance)}, std::nullopt,
         managed_team_id);
    return;
  }
  if (!monthly) return;
  board.confidence = std::max(0.0f, board.confidence - EMBARGO_CONFIDENCE_HIT);
  if (transfer_embargo) return;
  transfer_embargo = true;
  post(date, InboxCategory::Board, "INBOX_BOARD_EMBARGO_TITLE",
       "INBOX_BOARD_EMBARGO_BODY", {formatMoney(balance)}, std::nullopt,
       managed_team_id);
}

void WorldSimulation::postMedicalDigest(const GameDateValue& date,
                                        TeamID managed_team_id)
{
  if (managed_team_id != FREE_AGENTS_TEAM_ID &&
      (!week_recoveries.empty() || !week_minor_injuries.empty()))
  {
    post(date, InboxCategory::Injury, "INBOX_MEDICAL_TITLE",
         "INBOX_MEDICAL_BODY",
         {listOrNone(week_recoveries), listOrNone(week_minor_injuries)},
         std::nullopt, managed_team_id);
  }
  week_recoveries.clear();
  week_minor_injuries.clear();
}

void WorldSimulation::postSquadReport(const GameDateValue& date,
                                      const Team& team)
{
  const StatsConfig& config = gamedata->getStatsConfig();
  std::vector<std::pair<double, const Player*>> ranked;
  std::vector<std::string> injured;
  std::vector<std::string> unsettled;
  int expiring = 0;
  double total_age = 0.0;
  for (const auto& player_ref : gamedata->getPlayersForTeam(team.getId()))
  {
    const Player& player = player_ref.get();
    ranked.emplace_back(player.getOverall(config), &player);
    total_age += player.getAge();
    if (player.getDynamics().injury_days > 0)
      injured.push_back(std::format("{} ({})", player.getName(),
                                    player.getDynamics().injury_days));
    if (player.getDynamics().morale < 40.0f)
      unsettled.push_back(player.getName());
    if (player.getContractYears() <= 1) ++expiring;
  }
  if (ranked.empty()) return;
  std::ranges::sort(
      ranked, [](const auto& a, const auto& b) { return a.first > b.first; });

  std::vector<std::string> key_players;
  for (std::size_t i = 0; i < ranked.size() && i < 3; ++i)
  {
    key_players.push_back(
        std::format("{} ({}, {})", ranked[i].second->getName(),
                    RoleUtils::shortName(ranked[i].second->getRole()),
                    std::lround(ranked[i].first)));
  }
  const SquadAreas areas = squadAreas(ranked);
  const std::size_t strongest = areas.strongest();
  const std::size_t weakest = areas.weakest();
  post(date, InboxCategory::General, "INBOX_SQUAD_REPORT_TITLE",
       "INBOX_SQUAD_REPORT_BODY",
       {std::to_string(ranked.size()),
        std::format("{:.1f}", total_age / static_cast<double>(ranked.size())),
        joinNames(key_players), std::string("@") + AREA_KEYS[strongest],
        std::to_string(std::lround(areas.score[strongest])),
        std::string("@") + AREA_KEYS[weakest],
        std::to_string(std::lround(areas.score[weakest])), listOrNone(injured),
        listOrNone(unsettled), std::to_string(expiring)},
       std::nullopt, team.getId());
}

void WorldSimulation::postPreseasonSchedule(
    const GameDateValue& date, const Team& team,
    const std::vector<UpcomingFixture>& schedule)
{
  // "vs" marks a home match, "@" an away one (a lone "@" is not read as a
  // language key by formatLocalized()).
  const auto venue = [](const UpcomingFixture& fixture)
  { return std::string(fixture.home ? "vs" : "@"); };
  const auto opponentName = [&](const UpcomingFixture& fixture)
  {
    const auto other = gamedata->getTeam(fixture.opponent_id);
    return other ? other->get().getName() : std::string(FREE_AGENTS_NAME_ARG);
  };
  std::string friendlies;
  const UpcomingFixture* opener = nullptr;
  for (const UpcomingFixture& fixture : schedule)
  {
    if (fixture.type != MatchType::FRIENDLY)
    {
      opener = &fixture;
      break;
    }
    if (!friendlies.empty()) friendlies += '\n';
    friendlies +=
        std::format("{:02}/{:02}  {} {}", fixture.date.day, fixture.date.month,
                    venue(fixture), opponentName(fixture));
  }
  if (friendlies.empty() && !opener) return;
  if (friendlies.empty()) friendlies = "@INBOX_NONE";
  if (opener)
  {
    post(date, InboxCategory::General, "INBOX_PRESEASON_TITLE",
         "INBOX_PRESEASON_BODY",
         {friendlies,
          std::format("{:02}/{:02}/{}", opener->date.day, opener->date.month,
                      opener->date.year),
          venue(*opener), opponentName(*opener)},
         std::nullopt, team.getId());
    return;
  }
  post(date, InboxCategory::General, "INBOX_PRESEASON_TITLE",
       "INBOX_PRESEASON_BODY_NO_START", {friendlies}, std::nullopt,
       team.getId());
}

void WorldSimulation::postScoutSuggestion(const GameDateValue& date,
                                          const Team& team)
{
  const StatsConfig& config = gamedata->getStatsConfig();
  std::vector<std::pair<double, const Player*>> ranked;
  for (const auto& player_ref : gamedata->getPlayersForTeam(team.getId()))
    ranked.emplace_back(player_ref.get().getOverall(config), &player_ref.get());
  std::ranges::sort(
      ranked, [](const auto& a, const auto& b) { return a.first > b.first; });
  const SquadAreas areas = squadAreas(ranked);
  const std::size_t weakest = areas.weakest();

  const Finances& finances = team.getFinances();
  const std::int64_t budget = ClubEconomy::availableTransferBudget(
      finances.getTransferBudget(), finances.getBalance(),
      finances.getCurrentWageSpending(*gamedata, team), 0);
  ScoutSearchFilter filter;
  filter.role = areas.weakest_role[weakest];
  filter.min_overall = areas.weakest_overall[weakest] + 1.0f;
  filter.max_age = 30;
  filter.limit = 60;
  // A zero value limit means "any value": without money only free agents.
  filter.max_value = budget;
  filter.free_agents_only = budget <= 0;

  const ScoutedPlayerRow* best = nullptr;
  const std::vector<ScoutedPlayerRow> rows = scouting.search(filter);
  for (const ScoutedPlayerRow& row : rows)
  {
    if (row.team_id == team.getId()) continue;
    if (!best || row.overall > best->overall ||
        (row.overall == best->overall &&
         row.estimated_value < best->estimated_value))
      best = &row;
  }
  const std::string area = std::string("@") + AREA_KEYS[weakest];
  if (!best)
  {
    post(date, InboxCategory::Transfer, "INBOX_SCOUT_SUGGESTION_TITLE",
         "INBOX_SCOUT_SUGGESTION_NONE_BODY",
         {area, RoleUtils::shortNameArg(filter.role)}, std::nullopt,
         team.getId());
    return;
  }
  const auto player = gamedata->getPlayer(best->player_id);
  if (!player) return;
  const auto club = gamedata->getTeam(best->team_id);
  post(date, InboxCategory::Transfer, "INBOX_SCOUT_SUGGESTION_TITLE",
       "INBOX_SCOUT_SUGGESTION_BODY",
       {player->get().getName(), RoleUtils::shortNameArg(best->role),
        std::to_string(best->age),
        club && best->team_id != FREE_AGENTS_TEAM_ID
            ? club->get().getName()
            : std::string("@INBOX_FREE_AGENT"),
        area, std::to_string(std::lround(best->overall)),
        formatMoney(best->estimated_value)},
       best->player_id, best->team_id);
}

// ---------------------------------------------------------------------------
// Matches
// ---------------------------------------------------------------------------

bool WorldSimulation::applyMatchConsequences(
    const GameDateValue& date, const PlayerMatchConsequence& consequence,
    TeamID managed_team_id)
{
  auto& players = gamedata->getPlayers();
  const auto found = players.find(consequence.player_id);
  if (found == players.end() || consequence.minutes_played == 0) return false;
  Player& player = found->second;
  PlayerDynamics& dynamics = player.mutableDynamics();
  const std::int32_t ordinal = dayOrdinal(date);
  if (dynamics.last_match_day == ordinal) return false;

  const auto minutes = static_cast<float>(consequence.minutes_played);
  const float stamina = statValue(player, "Stamina", 60.0f);
  const double workload_multiplier = TrainingSystem::recordMatchLoad(
      *gamedata, player, consequence.minutes_played);
  if (consequence.end_condition >= 0.0f)
  {
    dynamics.condition = std::clamp(consequence.end_condition, 0.0f, 100.0f);
  }
  else
  {
    const float drain = minutes / 90.0f *
                        (Fitness::MATCH_DRAIN_BASE -
                         Fitness::MATCH_DRAIN_STAMINA_SLOPE * stamina);
    dynamics.condition = std::max(20.0f, dynamics.condition - drain);
  }
  dynamics.sharpness =
      std::min(100.0f, dynamics.sharpness +
                           Fitness::SHARPNESS_GAIN_PER_90 * minutes / 90.0f);
  dynamics.season_appearances = static_cast<std::uint16_t>(
      std::min(65535, dynamics.season_appearances + 1));
  dynamics.season_minutes = static_cast<std::uint16_t>(
      std::min(65535, dynamics.season_minutes + consequence.minutes_played));
  dynamics.week_minutes = static_cast<std::uint16_t>(
      std::min(65535, dynamics.week_minutes + consequence.minutes_played));

  bool injured = consequence.injured;
  if (consequence.end_condition < 0.0f && dynamics.injury_days == 0)
  {
    // ECIS: 21-27.5 injuries per 1000 match hours. [S]
    const double hazard =
        Fitness::MATCH_INJURY_RATE_PER_HOUR * static_cast<double>(minutes) /
        60.0 * workload_multiplier * injuryMultiplier(player, ordinal) *
        (injury_risk_provider ? injury_risk_provider(player.getId(), date)
                              : 1.0);
    injured =
        WorldRng::hashUniform(gamedata->getWorldSeed(), RngDomain::MatchInjury,
                              static_cast<std::uint64_t>(ordinal),
                              player.getId()) < 1.0 - std::exp(-hazard);
  }
  dynamics.last_match_day = ordinal;
  if (injured) injure(player, date, ordinal, true, managed_team_id);
  return true;
}

float WorldSimulation::lineupStrength(TeamID team_id) const
{
  const auto team = gamedata->getTeam(team_id);
  if (!team) return 50.0f;
  const StatsConfig& config = gamedata->getStatsConfig();
  const Lineup& lineup = team->get().getLineup();
  double total = 0.0;
  int count = 0;
  if (const Player* goalkeeper = lineup.getGoalkeeper())
  {
    total += goalkeeper->getOverall(config);
    ++count;
  }
  for (const auto& positioned : lineup.getOutfieldPlayers())
  {
    if (!positioned.player) continue;
    total += positioned.player->getOverall(config);
    ++count;
  }
  return count > 0 ? static_cast<float>(total / count) : 50.0f;
}

void WorldSimulation::onMatchPlayed(const Match& match, MatchReport& report,
                                    TeamID managed_team_id)
{
  const GameDateValue& date = match.getDate();
  const TeamID home_id = match.getHomeTeamId();
  const TeamID away_id = match.getAwayTeamId();
  const auto home_team = gamedata->getTeam(home_id);
  const auto away_team = gamedata->getTeam(away_id);
  if (!home_team || !away_team) return;
  if (home_id == managed_team_id) scouting.onManagedMatch(away_id);
  if (away_id == managed_team_id) scouting.onManagedMatch(home_id);

  if (report.players.empty())
  {
    report.addLineupAppearances(home_team->get().getLineup(), home_id);
    report.addLineupAppearances(away_team->get().getLineup(), away_id);
  }

  const std::uint8_t home_goals = match.getHomeScore();
  const std::uint8_t away_goals = match.getAwayScore();
  const bool competitive = match.getMatchType() != MatchType::FRIENDLY;
  const float home_strength = lineupStrength(home_id);
  const float away_strength = lineupStrength(away_id);
  const float home_points = pointsFor(home_goals, away_goals);
  const float away_points = pointsFor(away_goals, home_goals);
  const float home_expected =
      BoardModel::expectedPoints(home_strength, away_strength, true);
  const float away_expected =
      BoardModel::expectedPoints(away_strength, home_strength, false);

  const StatsConfig& config = gamedata->getStatsConfig();
  const std::int32_t ordinal = dayOrdinal(date);
  TrainingSystem::recordTeamMatch(*gamedata, home_id, ordinal);
  TrainingSystem::recordTeamMatch(*gamedata, away_id, ordinal);
  auto& players = gamedata->getPlayers();
  for (PlayerMatchLine& line : report.players)
  {
    if (line.minutes == 0) continue;
    applyMatchConsequences(date, {line.player_id, line.minutes, -1.0f, false},
                           managed_team_id);
    const auto found = players.find(line.player_id);
    if (found == players.end()) continue;
    Player& player = found->second;
    if (line.rating <= 0.0f)
    {
      const bool home = line.team_id == home_id;
      const float points = home ? home_points : away_points;
      const float strength = home ? home_strength : away_strength;
      WorldRng rng =
          WorldRng::stream(gamedata->getWorldSeed(), RngDomain::MatchRating,
                           static_cast<std::uint64_t>(ordinal), line.player_id);
      // [P] Result, relative ability and noise; cameos stay close to 6.
      float rating =
          line.minutes < 30
              ? rng.normal(6.2f, 0.3f)
              : 6.4f + 0.35f * (points - 1.0f) +
                    std::clamp((static_cast<float>(player.getOverall(config)) -
                                strength) /
                                   12.0f,
                               -0.6f, 0.6f) +
                    rng.normal(0.0f, 0.55f);
      line.rating = std::round(std::clamp(rating, 3.5f, 9.8f) * 10.0f) / 10.0f;
    }
    player.pushMatchRating(line.rating);
  }

  if (competitive)
  {
    // Morale follows results relative to expectation; unused squad members
    // feel it half as much. Deliberately small: results show no momentum.
    const auto moraleForTeam =
        [&](const Team& team, float points, float expected)
    {
      for (const PlayerID player_id : team.getPlayerIDs())
      {
        const auto found = players.find(player_id);
        if (found == players.end()) continue;
        Player& player = found->second;
        const bool played = player.getDynamics().last_match_day == ordinal;
        const float delta = WorldTuning::Morale::RESULT_WEIGHT *
                            (points - expected) * temperamentScale(player) *
                            (played ? 1.0f : 0.5f);
        PlayerDynamics& dynamics = player.mutableDynamics();
        dynamics.morale = std::clamp(dynamics.morale + delta, 0.0f, 100.0f);
      }
    };
    moraleForTeam(home_team->get(), home_points, home_expected);
    moraleForTeam(away_team->get(), away_points, away_expected);
    home_team->get().pushResult(outcomeFor(home_goals, away_goals));
    away_team->get().pushResult(outcomeFor(away_goals, home_goals));
  }

  recordGateReceipts(match);
  report.attendance = lastHomeAttendance(home_id);

  if (home_id == managed_team_id || away_id == managed_team_id)
  {
    const bool home = home_id == managed_team_id;
    const std::uint8_t own = home ? home_goals : away_goals;
    const std::uint8_t other = home ? away_goals : home_goals;
    const char* body = "INBOX_MATCH_DRAW_BODY";
    if (own > other)
      body = "INBOX_MATCH_WIN_BODY";
    else if (own < other)
      body = "INBOX_MATCH_LOSS_BODY";
    // The manager saw the result on the match screen: filed as read.
    post(date, InboxCategory::Match, "INBOX_MATCH_TITLE", body,
         {home_team->get().getName(), std::to_string(home_goals),
          std::to_string(away_goals), away_team->get().getName(),
          std::to_string(report.attendance)},
         std::nullopt, home ? away_id : home_id, true);
    if (match.getMatchType() == MatchType::LEAGUE &&
        board.team_id == managed_team_id)
    {
      BoardModel::recordMatch(board, home ? home_points : away_points,
                              home ? home_expected : away_expected);
    }
  }
  interactions.onMatchPlayed(report, managed_team_id);
  if (const auto managed = gamedata->getTeam(managed_team_id))
    stories.onMatchPlayed(report, managed_team_id,
                          managed->get().getRecentForm(), inbox);
  awards.onMatchPlayed(*gamedata, report, home_expected, away_expected);
  records.onMatchPlayed(*gamedata, report);
  preseason.onMatchPlayed(*gamedata, report, managed_team_id);
  refreshAiLineups(managed_team_id);
}

std::vector<TeamID> WorldSimulation::pointsOrder(LeagueID league_id) const
{
  const auto league = gamedata->getLeague(league_id);
  if (!league) return {};
  std::vector<TeamID> order = league->get().getTeamIDs();
  std::ranges::sort(order,
                    [&](TeamID a, TeamID b)
                    {
                      const auto points_a = league->get().getPoints(a);
                      const auto points_b = league->get().getPoints(b);
                      return points_a != points_b ? points_a > points_b : a < b;
                    });
  return order;
}

std::vector<TeamID> WorldSimulation::standings(LeagueID league_id) const
{
  if (standings_provider)
  {
    std::vector<TeamID> order = standings_provider(league_id);
    if (!order.empty()) return order;
  }
  return pointsOrder(league_id);
}

int WorldSimulation::leaguePosition(TeamID team_id) const
{
  const auto team = gamedata->getTeam(team_id);
  if (!team) return 0;
  const std::vector<TeamID> order = pointsOrder(team->get().getLeagueId());
  const auto found = std::ranges::find(order, team_id);
  return found == order.end()
             ? 0
             : static_cast<int>(std::distance(order.begin(), found)) + 1;
}

double WorldSimulation::sportingSuccess(const Team& team) const
{
  double position_term = 0.0;
  if (const auto league = gamedata->getLeague(team.getLeagueId()))
  {
    const auto& leaderboard = league->get().getLeaderboard();
    const bool started = std::ranges::any_of(
        leaderboard, [](const auto& entry) { return entry.second > 0; });
    const auto clubs = static_cast<double>(league->get().getTeamIDs().size());
    if (started && clubs > 1.0)
    {
      const auto position = static_cast<double>(leaguePosition(team.getId()));
      position_term = (clubs + 1.0 - 2.0 * position) / (clubs - 1.0);
    }
  }
  double form_term = 0.0;
  if (const std::string& form = team.getRecentForm(); !form.empty())
  {
    double points = 0.0;
    for (const char result : form)
    {
      if (result == std::to_underlying(MatchOutcome::Win)) points += 3.0;
      if (result == std::to_underlying(MatchOutcome::Draw)) points += 1.0;
    }
    form_term = std::clamp(
        (points / static_cast<double>(form.size()) - 1.35) / 1.65, -1.0, 1.0);
  }
  // [P] League position and recent results drive demand.
  return 0.6 * position_term + 0.4 * form_term;
}

void WorldSimulation::recordGateReceipts(const Match& match)
{
  const auto home_team = gamedata->getTeam(match.getHomeTeamId());
  const auto away_team = gamedata->getTeam(match.getAwayTeamId());
  if (!home_team || !away_team || match.getHomeTeamId() == FREE_AGENTS_TEAM_ID)
    return;
  Team& home = home_team->get();
  std::vector<std::uint8_t> reputations;
  if (const auto league = gamedata->getLeague(home.getLeagueId()))
  {
    for (const TeamID team_id : league->get().getTeamIDs())
    {
      if (const auto team = gamedata->getTeam(team_id))
        reputations.push_back(team->get().getReputation());
    }
  }
  const LeagueEconomy economy =
      makeLeagueEconomy(home.getLeagueId(), reputations);
  const std::uint32_t crowd = ClubEconomy::attendance(
      economy, home.getProfile(), sportingSuccess(home),
      away_team->get().getReputation(), match.getMatchType());
  last_attendance[home.getId()] = crowd;
  const auto revenue =
      static_cast<std::int64_t>(crowd) *
      static_cast<std::int64_t>(home.getProfile().ticket_price);
  if (revenue > 0)
    home.getFinances().record(match.getDate(), FinanceCategory::Matchday,
                              revenue);
}

std::uint32_t WorldSimulation::lastHomeAttendance(TeamID team_id) const
{
  const auto found = last_attendance.find(team_id);
  return found == last_attendance.end() ? 0 : found->second;
}

SquadRole WorldSimulation::squadRole(PlayerID player_id) const
{
  const auto player = gamedata->getPlayer(player_id);
  if (!player) return SquadRole::Fringe;
  const StatsConfig& config = gamedata->getStatsConfig();
  const double overall = player->get().getOverall(config);
  std::size_t rank = 0;
  for (const auto& other :
       gamedata->getPlayersForTeam(player->get().getTeamId()))
  {
    const double other_overall = other.get().getOverall(config);
    if (other_overall > overall ||
        (other_overall == overall && other.get().getId() < player_id))
      ++rank;
  }
  // Only the managed club's statuses count (a former club's are stale).
  if (const auto status = player->get().getTeamId() == board.team_id
                              ? squad_statuses.get(player_id, board.team_id)
                              : std::nullopt)
    return SquadStatusModel::toSquadRole(SquadStatusModel::expectation(
        *status, SquadStatusModel::deserved(rank, player->get().getAge())));
  return roleForRank(rank);
}

// ---------------------------------------------------------------------------
// Transfers
// ---------------------------------------------------------------------------

void WorldSimulation::onTransferBid(const GameDateValue& date,
                                    PlayerID player_id, TeamID bidder_id,
                                    std::uint32_t amount,
                                    TeamID managed_team_id)
{
  auto& players = gamedata->getPlayers();
  const auto found = players.find(player_id);
  const auto bidder = gamedata->getTeam(bidder_id);
  if (found == players.end() || !bidder) return;
  Player& player = found->second;
  // Interest from a clearly bigger club unsettles ambitious players. [P]
  if (const auto current = gamedata->getTeam(player.getTeamId());
      current &&
      bidder->get().getReputation() > current->get().getReputation() + 5 &&
      player.getTraits().ambition > 55)
    player.mutableDynamics().transfer_interest_weeks = 3;

  if (player.getTeamId() == managed_team_id)
  {
    post(date, InboxCategory::Transfer, "INBOX_BID_TITLE", "INBOX_BID_BODY",
         {player.getName(), bidder->get().getName(), formatMoney(amount)},
         player_id, bidder_id);
    stories.onTransferBid(date, player_id, bidder_id,
                          squadRole(player_id) == SquadRole::KeyPlayer,
                          managed_team_id, inbox);
  }
}

void WorldSimulation::onTransferCompleted(const GameDateValue& date,
                                          PlayerID player_id,
                                          TeamID from_team_id,
                                          TeamID to_team_id, std::uint32_t fee,
                                          TeamID managed_team_id)
{
  auto& players = gamedata->getPlayers();
  const auto found = players.find(player_id);
  if (found == players.end()) return;
  Player& player = found->second;
  PlayerDynamics& dynamics = player.mutableDynamics();
  dynamics.transfer_interest_weeks = 0;
  dynamics.playing_share = 0.0f;
  gamedata->getTraining().player(player_id).focus = TrainingFocus::None;
  dynamics.morale = std::max(dynamics.morale, 65.0f);  // New start. [P]
  interactions.onTransferCompleted(date, player_id, from_team_id, to_team_id,
                                   managed_team_id, inbox);
  records.onTransfer(*gamedata, date, player_id, from_team_id, to_team_id, fee);
  if (from_team_id == managed_team_id) stories.onPlayerLeft(player_id);

  const auto name_of = [&](TeamID team_id)
  {
    const auto team = team_id == FREE_AGENTS_TEAM_ID
                          ? std::nullopt
                          : gamedata->getTeam(team_id);
    return team ? team->get().getName() : std::string(FREE_AGENTS_NAME_ARG);
  };
  // Before a club is chosen (the new world's opening market) there is
  // nobody to tell.
  if (managed_team_id == FREE_AGENTS_TEAM_ID) return;
  if (to_team_id == managed_team_id)
  {
    post(date, InboxCategory::Transfer, "INBOX_SIGNING_TITLE",
         "INBOX_SIGNING_BODY",
         {player.getName(), name_of(from_team_id), formatMoney(fee)}, player_id,
         from_team_id);
  }
  else if (from_team_id == managed_team_id)
  {
    post(date, InboxCategory::Transfer, "INBOX_SALE_TITLE", "INBOX_SALE_BODY",
         {player.getName(), name_of(to_team_id), formatMoney(fee)}, player_id,
         to_team_id);
  }
}

// ---------------------------------------------------------------------------
// Seasonal events
// ---------------------------------------------------------------------------

void WorldSimulation::sendContractNotices(const GameDateValue& date,
                                          TeamID managed_team_id)
{
  if (managed_team_id == FREE_AGENTS_TEAM_ID) return;
  std::vector<std::string> names;
  for (const auto& player : gamedata->getPlayersForTeam(managed_team_id))
  {
    if (player.get().getContractYears() <= 1)
      names.push_back(player.get().getName());
  }
  if (names.empty()) return;
  std::ranges::sort(names);
  post(date, InboxCategory::Contract, "INBOX_CONTRACTS_TITLE",
       "INBOX_CONTRACTS_BODY", {std::to_string(names.size()), joinNames(names)},
       std::nullopt, managed_team_id);
}

void WorldSimulation::onSeasonEnd(const GameDateValue& date,
                                  TeamID managed_team_id)
{
  awardPrizeMoney(date, managed_team_id);
  records.closeSeason(*gamedata, seasonYear(date - 1));

  if (managed_team_id != FREE_AGENTS_TEAM_ID)
  {
    std::vector<std::string> leaving;
    for (const auto& player : gamedata->getPlayersForTeam(managed_team_id))
    {
      if (player.get().getContractYears() == 1)
        leaving.push_back(player.get().getName());
    }
    if (!leaving.empty())
    {
      std::ranges::sort(leaving);
      post(date, InboxCategory::Contract, "INBOX_RELEASED_TITLE",
           "INBOX_RELEASED_BODY",
           {std::to_string(leaving.size()), joinNames(leaving)}, std::nullopt,
           managed_team_id);
    }
  }
  renewAiContracts(managed_team_id);
  trimAiSquads(managed_team_id);
  youth.onSeasonEnd(date, managed_team_id);
  // Veterans' ceilings follow their decline: no hidden growth after ~29.
  const StatsConfig& config = gamedata->getStatsConfig();
  for (auto& [player_id, player] : gamedata->getPlayers())
  {
    const float cap = WorldGeneration::maxPotential(
        player.getAge(), static_cast<float>(player.getOverall(config)));
    if (player.getPotential() > cap) player.setPotential(cap);
  }
  retirePlayers(date, managed_team_id);

  const std::vector<std::string> departed =
      StaffModel::seasonEnd(*gamedata, managed_team_id, date.year);
  if (!departed.empty() && managed_team_id != FREE_AGENTS_TEAM_ID)
  {
    post(date, InboxCategory::Contract, "INBOX_STAFF_LEFT_TITLE",
         "INBOX_STAFF_LEFT_BODY",
         {std::to_string(departed.size()), joinNames(departed)}, std::nullopt,
         managed_team_id);
  }
}

void WorldSimulation::postTrainingWarning(const GameDateValue& date,
                                          TeamID managed_team_id)
{
  // The assistant reports only problems; the Training screen has the rest.
  const std::vector<TrainingAdvice> advice =
      TrainingModel::advise(TrainingSystem::adviceInputs(
          *gamedata, managed_team_id,
          fixture_outlook.contains(managed_team_id) ? 1 : 0));
  if (advice.front().severity != AdviceSeverity::Warning) return;
  post(date, InboxCategory::General, "INBOX_ASSISTANT_TITLE",
       advice.front().key, advice.front().args, std::nullopt, managed_team_id);
}

void WorldSimulation::awardPrizeMoney(const GameDateValue& date,
                                      TeamID managed_team_id)
{
  season_prize_money = 0;
  const auto economies = buildLeagueEconomies(*gamedata);
  for (const auto& [league_id, league] : gamedata->getLeagues())
  {
    const auto economy = economies.find(league_id);
    if (economy == economies.end()) continue;
    const std::vector<TeamID> order = standings(league_id);
    const std::vector<std::int64_t> merit =
        ClubEconomy::prizeMoney(economy->second, order.size());

    // Clubs ordered by reputation give the expected finish. [P]
    std::vector<TeamID> by_reputation = order;
    std::ranges::sort(
        by_reputation,
        [&](TeamID a, TeamID b)
        {
          const auto rep_a = gamedata->getTeam(a)->get().getReputation();
          const auto rep_b = gamedata->getTeam(b)->get().getReputation();
          return rep_a != rep_b ? rep_a > rep_b : a < b;
        });

    for (std::size_t position = 0; position < order.size(); ++position)
    {
      const auto team_ref = gamedata->getTeam(order[position]);
      if (!team_ref) continue;
      Team& team = team_ref->get();
      // Merit money is part of the league's broadcasting deal. Continental
      // prize money is paid by the competitions as it is earned.
      if (merit[position] > 0)
        team.getFinances().record(date, FinanceCategory::Broadcasting,
                                  merit[position]);

      // Reputation moves with performance against expectation. [P]
      const auto expected = static_cast<double>(
          std::distance(by_reputation.begin(),
                        std::ranges::find(by_reputation, team.getId())));
      ClubProfile profile = team.getProfile();
      const double delta = std::clamp(
          0.4 * (expected - static_cast<double>(position)), -4.0, 4.0);
      profile.reputation = static_cast<std::uint8_t>(std::clamp(
          static_cast<int>(std::lround(profile.reputation + delta)), 15, 99));
      team.setProfile(profile);

      // The board judges the season once promotion and relegation are
      // known (Game::endSeason); its review quotes this money.
      if (team.getId() == managed_team_id) season_prize_money = merit[position];
    }
  }
}

void WorldSimulation::renewAiContracts(TeamID managed_team_id)
{
  const StatsConfig& config = gamedata->getStatsConfig();
  const std::uint64_t seed = gamedata->getWorldSeed();
  for (const auto& [team_id, team] : gamedata->getTeams())
  {
    if (team_id == FREE_AGENTS_TEAM_ID || team_id == managed_team_id) continue;
    std::vector<std::pair<double, PlayerID>> ranked;
    for (const PlayerID player_id : team.getPlayerIDs())
    {
      if (const auto player = gamedata->getPlayer(player_id))
        ranked.emplace_back(player->get().getOverall(config), player_id);
    }
    std::ranges::sort(ranked, std::greater<>{});
    // A club already paying more than its board allows renews without a
    // raise.
    const bool over_budget =
        team.getFinances().getCurrentWageSpending(*gamedata, team) >
        team.getFinances().getWageBudget();
    // AI clubs keep most of their matchday squad and let the rest go. [P]
    for (std::size_t rank = 0; rank < ranked.size() && rank < 22; ++rank)
    {
      Player& player = gamedata->getPlayers().at(ranked[rank].second);
      if (player.getContractYears() != 1 || player.getAge() > 32 ||
          (loan_check && loan_check(player.getId())))
        continue;
      if (WorldRng::hashUniform(seed, RngDomain::Transfers, player.getId(),
                                static_cast<std::uint64_t>(player.getAge())) >
          0.85)
        continue;
      int years = 1;
      if (player.getAge() < 24)
        years = 4;
      else if (player.getAge() < 29)
        years = 3;
      else if (player.getAge() < 31)
        years = 2;
      player.setContractYears(static_cast<std::uint8_t>(1 + years));
      if (!over_budget)
        player.setWage(player.getWage() + player.getWage() / 20);
    }
  }
}

void WorldSimulation::trimAiSquads(TeamID managed_team_id)
{
  using Youth = WorldTuning::Youth;
  const StatsConfig& config = gamedata->getStatsConfig();
  std::vector<std::pair<double, Player*>> ranked;
  for (auto& [team_id, team] : gamedata->getTeams())
  {
    if (team_id == FREE_AGENTS_TEAM_ID || team_id == managed_team_id) continue;
    ranked.clear();
    for (const PlayerID player_id : team.getPlayerIDs())
    {
      Player& player = gamedata->getPlayers().at(player_id);
      if (!player.isAcademyPlayer() && !(loan_check && loan_check(player_id)))
        ranked.emplace_back(player.getOverall(config), &player);
    }
    if (ranked.size() <= Youth::AI_SQUAD_TARGET) continue;
    std::ranges::sort(
        ranked, [](const auto& a, const auto& b) { return a.first > b.first; });
    // Beyond the matchday squad the weakest players are not kept, except
    // prospects with room to grow: their contracts run out this summer.
    std::size_t surplus = ranked.size() - Youth::AI_SQUAD_TARGET;
    for (std::size_t rank = ranked.size(); rank > 22 && surplus > 0; --rank)
    {
      const auto& [overall, player] = ranked[rank - 1];
      if (player->getAge() <= 20 &&
          static_cast<double>(player->getPotential()) > overall + 8.0)
        continue;
      player->setContractYears(1);
      --surplus;
    }
  }
}

void WorldSimulation::retirePlayers(const GameDateValue& date,
                                    TeamID managed_team_id)
{
  const StatsConfig& config = gamedata->getStatsConfig();
  const std::uint64_t seed = gamedata->getWorldSeed();
  std::vector<PlayerID> retiring;
  for (const auto& [player_id, player] : gamedata->getPlayers())
  {
    const double probability =
        retirementProbability(player, player.getTeamId() == FREE_AGENTS_TEAM_ID,
                              player.getOverall(config));
    if (probability > 0.0 &&
        WorldRng::hashUniform(seed, RngDomain::Retirement, date.year,
                              player_id) < probability)
      retiring.push_back(player_id);
  }
  std::ranges::sort(retiring);

  std::vector<TeamID> affected;
  for (const PlayerID player_id : retiring)
  {
    const Player& player = gamedata->getPlayers().at(player_id);
    const TeamID team_id = player.getTeamId();
    if (auto team = gamedata->getTeam(team_id))
    {
      team->get().removePlayerID(player_id);
      if (!std::ranges::contains(affected, team_id))
        affected.push_back(team_id);
    }
    if (team_id == managed_team_id && team_id != FREE_AGENTS_TEAM_ID)
    {
      post(date, InboxCategory::General, "INBOX_RETIRED_TITLE",
           "INBOX_RETIRED_BODY",
           {player.getName(), std::to_string(player.getAge())}, std::nullopt,
           team_id);
    }
  }
  // Line-ups hold raw pointers, so they are fixed before the players are
  // erased: the manager's keeps his selection with only the gaps filled,
  // the others (the free agents' one too) are rebuilt.
  for (const TeamID team_id : affected)
  {
    Team& team = gamedata->getTeam(team_id)->get();
    if (team_id != managed_team_id || !lineup_gap_filler)
    {
      team.generateStartingXI(*gamedata, config);
      continue;
    }
    for (const PlayerID player_id : retiring)
      if (const Player& player = gamedata->getPlayers().at(player_id);
          player.getTeamId() == team_id)
        lineup_gap_filler(team, player);
  }
  for (const PlayerID player_id : retiring) gamedata->removePlayer(player_id);
}

void WorldSimulation::onSeasonStart(const GameDateValue& date,
                                    TeamID managed_team_id)
{
  const auto economies = buildLeagueEconomies(*gamedata);
  std::vector<TeamID> relegated;
  const auto parachute = parachutes(economies, &relegated);
  applyRelegationWageCuts(relegated, managed_team_id);
  for (auto& [team_id, team] : gamedata->getTeams())
  {
    if (team_id == FREE_AGENTS_TEAM_ID) continue;
    const auto economy = economies.find(team.getLeagueId());
    if (economy == economies.end()) continue;
    Finances& finances = team.getFinances();
    const auto paid = parachute.find(team_id);
    const double income =
        ClubEconomy::expectedIncome(economy->second, team.getReputation()) +
        (paid == parachute.end() ? 0.0 : paid->second);
    const std::int64_t payroll =
        finances.getCurrentWageSpending(*gamedata, team);
    finances.setWageBudget(
        ClubEconomy::seasonWageBudget(economy->second, income, payroll));
    finances.setTransferBudget(
        ClubEconomy::seasonTransferBudget(finances.getBalance(), income));
    if (team_id != managed_team_id)
    {
      ClubProfile profile = team.getProfile();
      profile.ticket_price = static_cast<std::uint32_t>(
          std::lround(ClubEconomy::fairTicketPrice(economy->second, profile)));
      team.setProfile(profile);
    }
  }
  for (auto& [player_id, player] : gamedata->getPlayers())
  {
    PlayerDynamics& dynamics = player.mutableDynamics();
    dynamics.season_appearances = 0;
    dynamics.season_minutes = 0;
  }
  stories.onSeasonStart();
  preseason.onSeasonStart(seasonYear(date));
  youth.onSeasonStart(date, managed_team_id, inbox);
  if (managed_team_id != FREE_AGENTS_TEAM_ID)
    ensureBoard(date, managed_team_id, true);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void WorldSimulation::load(const std::shared_ptr<DatabaseConnection>& db_conn)
{
  inbox.restore(InboxRepository(db_conn).loadAll());
  inbox.setView(InboxRepository(db_conn).loadView());
  WorldStateRepository(db_conn).loadBoard(board);
  // The cash review follows from the ledger: warned while in the red, and
  // embargoed if the club was already in the red before the latest monthly
  // review and still after it.
  cash_warning_sent = false;
  transfer_embargo = false;
  if (const auto team = gamedata->getTeam(board.team_id);
      team && board.team_id != FREE_AGENTS_TEAM_ID)
  {
    const Finances& finances = team->get().getFinances();
    if (finances.getBalance() < 0 && !finances.getLedger().empty())
    {
      const GameDateValue& last = finances.getLedger().back().date;
      const GameDateValue review(last.year, last.month, 1);
      cash_warning_sent = true;
      transfer_embargo =
          finances.balanceAt(review - 1) < 0 && finances.balanceAt(review) < 0;
    }
  }
  ScoutingState scouting_state;
  ScoutingRepository(db_conn).load(scouting_state);
  scouting.restore(std::move(scouting_state));
  youth.load(db_conn);
  interactions.load(db_conn);
  stories.load(db_conn);
  squad_statuses.load(*db_conn);
  awards.load(db_conn);
  records.load(db_conn, *gamedata);
  facility_projects.load(db_conn);
  preseason.load(db_conn);
  mentoring.load(db_conn);
  holiday_preferences = Holiday::loadPreferences(db_conn);
}

void WorldSimulation::save(
    const std::shared_ptr<DatabaseConnection>& db_conn) const
{
  InboxRepository(db_conn).replaceAll(inbox.getMessages());
  InboxRepository(db_conn).saveView(inbox.getView());
  ScoutingRepository(db_conn).save(scouting.getState());
  youth.save(db_conn);
  interactions.save(db_conn);
  stories.save(db_conn);
  squad_statuses.save(*db_conn);
  awards.save(db_conn);
  records.save(db_conn);
  facility_projects.save(db_conn);
  preseason.save(db_conn);
  mentoring.save(db_conn);
  Holiday::savePreferences(db_conn, holiday_preferences);
  WorldStateRepository world_repo(db_conn);
  world_repo.saveBoard(board);
  world_repo.saveWorldState(gamedata->getWorldSeed(),
                            gamedata->peekNextPlayerId(),
                            gamedata->getStaff().peekNextId());
  PlayerRepository player_repo(db_conn);
  for (const PlayerID player_id : gamedata->getRemovedPlayerIds())
    player_repo.deletePlayer(player_id);
  StaffRepository(db_conn).replaceAll(gamedata->getStaff());
  gamedata->getTraining().prunePlayers(gamedata->getPlayers());
  TrainingRepository(db_conn).replaceAll(gamedata->getTraining());
}

void WorldSimulation::onSaved()
{
  for (auto& [team_id, team] : gamedata->getTeams())
    team.getFinances().markPersisted();
  gamedata->clearRemovedPlayerIds();
}
