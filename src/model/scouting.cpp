// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/scouting.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <memory>
#include <numbers>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "database/gamedata.h"
#include "global/global.h"
#include "model/club_economy.h"
#include "model/competition.h"
#include "model/inbox.h"
#include "model/role_utils.h"
#include "model/world_generation.h"
#include "model/world_rng.h"
#include "model/world_tuning.h"

namespace
{
using namespace ScoutingTuning;

constexpr std::uint64_t POTENTIAL_KEY = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t SCOUT_KEY = 0x5C0075C0075ULL;
constexpr std::uint64_t COVERAGE_KEY = 0xC0FE12A6EULL;
constexpr int STARTER_RANK = 11;
constexpr float FIT_MARGIN = 3.0f; /*!< [P] Within reach of the starter. */
constexpr std::uint8_t PROSPECT_AGE = 21;
constexpr int PEAK_GROWTH_AGE = 25;
constexpr int NO_GROWTH_AGE = 30; /*!< [P] Estimated ceilings stop here. */
constexpr float FOLLOW_UP_WEIGHT = 4.0f; /*!< [P] Scouts revisit prospects. */
constexpr float PROSPECT_GROWTH_PER_YEAR = 1.4f; /*!< [P] Mean headroom. */

/** Stable (platform independent) hash of an attribute name. */
std::uint64_t nameKey(std::string_view name)
{
  std::uint64_t hash = 0xCBF29CE484222325ULL;
  for (const char c : name)
  {
    hash ^= static_cast<unsigned char>(c);
    hash *= 0x100000001B3ULL;
  }
  return hash;
}

/** Broad position group used for the priors (0 GK, 1 DEF, 2 MID, 3 FWD). */
std::size_t positionGroup(PlayerRole role)
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
    case CDM:
    case CM:
    case CAM:
    case LM:
    case RM:
      return 2;
    default:
      return 3;
  }
}

using ScoutingTeamPrior = std::array<std::vector<float>, 4>;

void accumulatePriorStats(const Player& player,
                          const std::vector<std::string>& names,
                          std::array<std::vector<double>, 4>& sums,
                          std::array<std::vector<int>, 4>& counts,
                          std::vector<double>& all_sums,
                          std::vector<int>& all_counts)
{
  const std::size_t group = positionGroup(player.getRole());
  const auto& stats = player.getStats();
  for (std::size_t stat_index = 0; stat_index < names.size(); ++stat_index)
  {
    const auto stat = stats.find(names[stat_index]);
    if (stat == stats.end()) continue;
    sums[group][stat_index] += static_cast<double>(stat->second);
    ++counts[group][stat_index];
    all_sums[stat_index] += static_cast<double>(stat->second);
    ++all_counts[stat_index];
  }
}

ScoutingTeamPrior buildTeamPrior(const GameData& gamedata, TeamID team_id)
{
  const auto& names = gamedata.getStatsConfig().possible_stats;
  std::array<std::vector<double>, 4> sums;
  std::array<std::vector<int>, 4> counts;
  std::vector<double> all_sums(names.size(), 0.0);
  std::vector<int> all_counts(names.size(), 0);
  for (std::size_t group = 0; group < sums.size(); ++group)
  {
    sums[group].assign(names.size(), 0.0);
    counts[group].assign(names.size(), 0);
  }
  for (const Player& player : gamedata.getPlayersForTeam(team_id))
    accumulatePriorStats(player, names, sums, counts, all_sums, all_counts);

  ScoutingTeamPrior prior;
  for (std::size_t group = 0; group < sums.size(); ++group)
  {
    prior[group].resize(names.size());
    for (std::size_t index = 0; index < names.size(); ++index)
    {
      double mean = 0.5 * static_cast<double>(MAX_STAT_VAL);
      if (counts[group][index] > 0)
        mean = sums[group][index] / counts[group][index];
      else if (all_counts[index] > 0)
        mean = all_sums[index] / all_counts[index];
      prior[group][index] = static_cast<float>(mean);
    }
  }
  return prior;
}

float clampStat(float value)
{
  return std::clamp(value, static_cast<float>(MIN_STAT_VAL),
                    static_cast<float>(MAX_STAT_VAL));
}

std::uint8_t toPercent(float value)
{
  return static_cast<std::uint8_t>(
      std::lround(std::clamp(value, 0.0f, 100.0f)));
}

void post(Inbox& inbox, InboxMessage message) { inbox.add(std::move(message)); }

bool passesPlayerFilters(const GameData& gamedata, TeamID managed_team_id,
                         const Player& player, const ScoutSearchFilter& filter)
{
  const TeamID team_id = player.getTeamId();
  if (managed_team_id != FREE_AGENTS_TEAM_ID && team_id == managed_team_id)
    return false;
  if (filter.role != PlayerRole::UNKNOWN && player.getRole() != filter.role)
    return false;
  if (filter.min_age > 0 && player.getAge() < filter.min_age) return false;
  if (filter.max_age > 0 && player.getAge() > filter.max_age) return false;
  if (filter.free_agents_only && team_id != FREE_AGENTS_TEAM_ID) return false;
  if (filter.league_id == 0) return true;
  const auto team = gamedata.getTeam(team_id);
  return team && team_id != FREE_AGENTS_TEAM_ID &&
         team->get().getLeagueId() == filter.league_id;
}

bool passesEstimatedFilters(const ScoutedPlayerRow& row,
                            const ScoutSearchFilter& filter)
{
  return row.overall >= filter.min_overall &&
         (filter.max_value <= 0 || row.estimated_value <= filter.max_value) &&
         row.knowledge >= filter.min_knowledge &&
         (!filter.focus_matches_only || row.matches_focus);
}

bool betterScoutedRows(const ScoutedPlayerRow& left,
                       const ScoutedPlayerRow& right)
{
  return left.overall != right.overall ? left.overall > right.overall
                                       : left.player_id < right.player_id;
}

std::string gradeLetter(ScoutGrade grade)
{
  using enum ScoutGrade;
  switch (grade)
  {
    case A:
      return "A";
    case B:
      return "B";
    case C:
      break;
  }
  return "C";
}

std::string rangeText(float low, float high)
{
  return std::format("{:.0f}-{:.0f}", static_cast<double>(low),
                     static_cast<double>(high));
}
}  // namespace

const char* scoutGradeKey(ScoutGrade grade)
{
  using enum ScoutGrade;
  switch (grade)
  {
    case A:
      return "SCOUT_GRADE_A";
    case B:
      return "SCOUT_GRADE_B";
    case C:
      break;
  }
  return "SCOUT_GRADE_C";
}

const char* scoutTargetKindKey(ScoutTargetKind kind)
{
  using enum ScoutTargetKind;
  switch (kind)
  {
    case Player:
      return "SCOUT_TARGET_PLAYER";
    case League:
      return "SCOUT_TARGET_LEAGUE";
    case Country:
      return "SCOUT_TARGET_COUNTRY";
    case Region:
      return "SCOUT_TARGET_REGION";
    case FreeAgents:
      break;
  }
  return "SCOUT_TARGET_FREE_AGENTS";
}

const char* scoutAssignErrorKey(ScoutAssignError error)
{
  using enum ScoutAssignError;
  switch (error)
  {
    case None:
      return "SCOUT_ASSIGNMENT_STARTED";
    case NoTeam:
      return "SCOUT_ERROR_NO_TEAM";
    case UnknownScout:
      return "SCOUT_ERROR_UNKNOWN_SCOUT";
    case ScoutBusy:
      return "SCOUT_ERROR_BUSY";
    case InvalidTarget:
      return "SCOUT_ERROR_TARGET";
    case InvalidDuration:
      return "SCOUT_ERROR_DURATION";
    case InsufficientFunds:
      break;
  }
  return "SCOUT_ERROR_FUNDS";
}

ScoutingSystem::ScoutingSystem(std::shared_ptr<GameData> gd)
    : gamedata(std::move(gd))
{
}

void ScoutingSystem::setScoutProvider(ScoutProvider provider)
{
  const std::scoped_lock lock(cache_mutex);
  scout_provider = std::move(provider);
  refreshScouts();
}

void ScoutingSystem::setBudgetProvider(BudgetProvider provider)
{
  budget_provider = std::move(provider);
}

std::vector<ScoutProfile> ScoutingSystem::defaultScouts(
    const GameData& gamedata, TeamID team_id)
{
  std::vector<ScoutProfile> result;
  const auto team = gamedata.getTeam(team_id);
  if (team_id == FREE_AGENTS_TEAM_ID || !team) return result;
  const std::uint64_t seed = gamedata.getWorldSeed();
  if (const StaffRoster& staff = gamedata.getStaff(); !staff.empty())
  {
    for (const StaffMember* member : staff.clubStaff(team_id))
    {
      if (member->role != StaffRole::Scout) continue;
      result.emplace_back(member->id, member->name(),
                          member->attribute(StaffAttribute::JudgingAbility),
                          member->attribute(StaffAttribute::JudgingPotential),
                          member->nationality);
    }
    return result;
  }

  // [P] Richer, more reputable clubs employ more and better scouts.
  const float reputation = team->get().getReputation();
  const int count = 2 + static_cast<int>(reputation) / 34;
  const float mean_judging = 25.0f + 0.55f * reputation;
  const Language domestic =
      leagueProfile(team->get().getLeagueId()).domestic_nationality;
  const NamePool& names = NamePool::instance();
  for (int index = 0; index < count; ++index)
  {
    WorldRng rng =
        WorldRng::stream(seed, RngDomain::Scouting, mixHash(SCOUT_KEY, team_id),
                         static_cast<std::uint64_t>(index));
    ScoutProfile scout;
    scout.id = static_cast<std::uint32_t>(index + 1);
    if (!names.first_names.empty() && !names.last_names.empty())
    {
      scout.name = names.first_names[static_cast<std::size_t>(rng.uniformInt(
                       0, static_cast<int>(names.first_names.size()) - 1))] +
                   " " +
                   names.last_names[static_cast<std::size_t>(rng.uniformInt(
                       0, static_cast<int>(names.last_names.size()) - 1))];
    }
    const auto judging = [&]
    {
      return toPercent(
          std::clamp(rng.normal(mean_judging, 9.0f), 10.0f, 95.0f));
    };
    scout.judging_ability = judging();
    scout.judging_potential = judging();
    scout.nationality = domestic;
    result.push_back(std::move(scout));
  }
  return result;
}

void ScoutingSystem::setManagedTeam(TeamID team_id)
{
  const std::scoped_lock lock(cache_mutex);
  if (team_id == state.team_id) return;
  state = ScoutingState{};
  state.team_id = team_id;
  generated.clear();
  refreshScouts();
  ensureExpertise();
}

void ScoutingSystem::refreshScouts() const
{
  const std::scoped_lock lock(cache_mutex);
  scouts = scout_provider ? scout_provider(state.team_id)
                          : defaultScouts(*gamedata, state.team_id);
}

std::vector<ScoutProfile> ScoutingSystem::getScouts() const
{
  const std::scoped_lock lock(cache_mutex);
  refreshScouts();
  return scouts;
}

const ScoutProfile* ScoutingSystem::findScout(std::uint32_t scout_id) const
{
  const std::scoped_lock lock(cache_mutex);
  const auto found = std::ranges::find(scouts, scout_id, &ScoutProfile::id);
  return found == scouts.end() ? nullptr : std::to_address(found);
}

// ---------------------------------------------------------------------------
// Knowledge
// ---------------------------------------------------------------------------

LeagueID ScoutingSystem::countryOf(LeagueID league_id) const
{
  LeagueID current = league_id;
  // Leagues form a shallow tree; the bound guards against malformed data.
  for (int depth = 0; depth < 8; ++depth)
  {
    const auto league = gamedata->getLeague(current);
    if (!league) return current;
    const auto parent = league->get().getParentLeagueID();
    if (!parent.has_value()) return current;
    current = *parent;
  }
  return current;
}

float ScoutingSystem::baselineKnowledge(const Player& player) const
{
  const TeamID team_id = player.getTeamId();
  if (state.team_id != FREE_AGENTS_TEAM_ID && team_id == state.team_id)
    return OWN_KNOWLEDGE;
  if (team_id == FREE_AGENTS_TEAM_ID) return FREE_AGENT_KNOWLEDGE;
  const auto own = gamedata->getTeam(state.team_id);
  const auto theirs = gamedata->getTeam(team_id);
  if (!own || !theirs) return FOREIGN_KNOWLEDGE;
  const LeagueID own_league = own->get().getLeagueId();
  const LeagueID their_league = theirs->get().getLeagueId();
  if (own_league == their_league) return SAME_LEAGUE_KNOWLEDGE;
  if (countryOf(own_league) == countryOf(their_league))
    return SAME_COUNTRY_KNOWLEDGE;
  return FOREIGN_KNOWLEDGE;
}

float ScoutingSystem::knowledgeOf(PlayerID player_id) const
{
  const auto player = gamedata->getPlayer(player_id);
  if (!player) return 0.0f;
  const float baseline = baselineKnowledge(player->get());
  if (baseline >= OWN_KNOWLEDGE) return OWN_KNOWLEDGE;
  const auto entry = state.knowledge.find(player_id);
  return entry == state.knowledge.end()
             ? baseline
             : std::max(baseline, entry->second.knowledge);
}

void ScoutingSystem::observe(PlayerID player_id, float amount,
                             const ScoutProfile* scout)
{
  const float current = knowledgeOf(player_id);
  if (current >= MAX_SCOUTED_KNOWLEDGE || amount <= 0.0f) return;
  const auto [slot, inserted] = state.knowledge.try_emplace(player_id);
  ScoutKnowledge& entry = slot->second;
  if (inserted)
  {
    // Until a scout watches him, the department as a whole judges him.
    const auto [ability, potential] = departmentJudging();
    entry.judging_ability = toPercent(ability);
    entry.judging_potential = toPercent(potential);
  }
  entry.knowledge =
      std::min(MAX_SCOUTED_KNOWLEDGE,
               current + amount * (1.0f - current / MAX_SCOUTED_KNOWLEDGE));
  if (scout != nullptr)
  {
    // The sharpest eye that has seen the player sets the precision.
    entry.judging_ability =
        std::max(entry.judging_ability, scout->judging_ability);
    entry.judging_potential =
        std::max(entry.judging_potential, scout->judging_potential);
  }
}

bool ScoutingSystem::isForeign(const Player& player) const
{
  const auto own = gamedata->getTeam(state.team_id);
  const auto theirs = gamedata->getTeam(player.getTeamId());
  if (!own || !theirs || player.getTeamId() == FREE_AGENTS_TEAM_ID)
    return false;
  return countryOf(own->get().getLeagueId()) !=
         countryOf(theirs->get().getLeagueId());
}

// ---------------------------------------------------------------------------
// Estimates
// ---------------------------------------------------------------------------

std::pair<float, float> ScoutingSystem::departmentJudging() const
{
  const std::scoped_lock lock(cache_mutex);
  if (scouts.empty())
  {
    // Without scouts the club relies on what the staff can tell.
    if (gamedata->getStaff().empty()) return {40.0f, 40.0f};
    const float judging =
        100.0f * gamedata->getStaff().effects(state.team_id).scouting_accuracy;
    return {judging, judging};
  }
  float ability = 0.0f;
  float potential = 0.0f;
  for (const ScoutProfile& scout : scouts)
  {
    ability += scout.judging_ability;
    potential += scout.judging_potential;
  }
  const auto count = static_cast<float>(scouts.size());
  return {ability / count, potential / count};
}

ScoutingSystem::Noise ScoutingSystem::noiseFor(const Player& player,
                                               float knowledge) const
{
  std::pair<float, float> judging = departmentJudging();
  if (const auto entry = state.knowledge.find(player.getId());
      entry != state.knowledge.end())
  {
    judging = {entry->second.judging_ability, entry->second.judging_potential};
  }
  const float ja = judging.first / 100.0f;
  const float jp = judging.second / 100.0f;
  const float k = std::clamp(knowledge / 100.0f, 0.0f, 1.0f);

  Noise noise;
  if (knowledge < OWN_KNOWLEDGE)
  {
    noise.attribute_sd = (1.25f - 0.5f * ja) *
                         std::lerp(ATTRIBUTE_SD_UNSEEN, ATTRIBUTE_SD_FLOOR, k);
  }
  const float d_max = std::lerp(D_PRIME_COACH_EYE, D_PRIME_COMBINED, jp);
  const float d_prime = std::lerp(D_PRIME_SINGLE_SOURCE, d_max, k);
  noise.potential_sd = POTENTIAL_POPULATION_SD / d_prime;
  return noise;
}

float ScoutingSystem::standardNormal(PlayerID player_id,
                                     std::uint64_t key) const
{
  // Fixed per (world, club, player, key): the estimate only moves when the
  // noise scale changes, so it converges instead of flickering.
  const std::uint64_t seed = gamedata->getWorldSeed();
  const std::uint64_t subject = mixHash(player_id, state.team_id);
  double u1 = WorldRng::hashUniform(seed, RngDomain::Scouting, subject, key);
  const double u2 = WorldRng::hashUniform(seed, RngDomain::Scouting, subject,
                                          mixHash(key, POTENTIAL_KEY));
  u1 = std::max(u1, 1e-12);
  return static_cast<float>(std::sqrt(-2.0 * std::log(u1)) *
                            std::cos(2.0 * std::numbers::pi * u2));
}

const RoleFocus* ScoutingSystem::focusFor(PlayerRole role) const
{
  const auto& focus = gamedata->getStatsConfig().role_focus;
  const auto found = focus.find(RoleUtils::getBroadCategory(role));
  return found == focus.end() ? nullptr : &found->second;
}

float ScoutingSystem::priorMean(TeamID team_id, std::size_t group,
                                std::size_t requested_index) const
{
  const std::scoped_lock lock(cache_mutex);
  auto found = priors.find(team_id);
  if (found == priors.end())
    found =
        priors.try_emplace(team_id, buildTeamPrior(*gamedata, team_id)).first;
  return found->second[group][requested_index];
}

ScoutingSystem::Estimate ScoutingSystem::estimateStat(const Player& player,
                                                      const std::string& stat,
                                                      float value,
                                                      float noise_sd) const
{
  if (noise_sd <= 0.0f) return {value, 0.0f};
  // The scout's best guess regresses his noisy observation towards what is
  // typical for the player's position at his club (a normal prior), so
  // barely known players look ordinary instead of spuriously brilliant.
  const auto& names = gamedata->getStatsConfig().possible_stats;
  float prior = 0.5f * static_cast<float>(MAX_STAT_VAL);
  for (std::size_t index = 0; index < names.size(); ++index)
  {
    if (names[index] != stat) continue;
    prior =
        priorMean(player.getTeamId(), positionGroup(player.getRole()), index);
    break;
  }
  const float tau2 = ATTRIBUTE_POPULATION_SD * ATTRIBUTE_POPULATION_SD;
  const float reliability = tau2 / (tau2 + noise_sd * noise_sd);
  const float observed =
      value + standardNormal(player.getId(), nameKey(stat)) * noise_sd;
  return {clampStat(std::lerp(prior, observed, reliability)),
          std::sqrt(reliability) * noise_sd};
}

float ScoutingSystem::estimatedOverall(const Player& player, float attribute_sd,
                                       float* overall_sd) const
{
  const RoleFocus* focus = focusFor(player.getRole());
  if (focus == nullptr)
  {
    if (overall_sd) *overall_sd = 0.0f;
    return 0.0f;
  }
  const auto& stats = player.getStats();
  double overall = 0.0;
  double variance = 0.0;
  const std::size_t count =
      std::min(focus->stats.size(), focus->weights.size());
  for (std::size_t index = 0; index < count; ++index)
  {
    const auto stat = stats.find(focus->stats[index]);
    if (stat == stats.end()) continue;
    const Estimate estimate =
        estimateStat(player, stat->first, stat->second, attribute_sd);
    const double weight = focus->weights[index];
    overall += static_cast<double>(estimate.value) * weight;
    variance += weight * weight * static_cast<double>(estimate.sd) *
                static_cast<double>(estimate.sd);
  }
  if (overall_sd) *overall_sd = static_cast<float>(std::sqrt(variance));
  return static_cast<float>(overall);
}

std::int64_t ScoutingSystem::estimatedValue(const Player& player,
                                            float estimated_overall,
                                            float estimated_potential)
{
  // The market value model applied to the estimates, so neither the true
  // ability nor the true potential leaks into the value.
  return static_cast<std::int64_t>(std::llround(
      Player::valueFor(estimated_overall, estimated_potential, player.getAge(),
                       static_cast<double>(player.getContractYears()))));
}

std::int64_t ScoutingSystem::estimatedFee(const Player& player,
                                          const ScoutedPlayerRow& row) const
{
  if (player.getTeamId() == FREE_AGENTS_TEAM_ID) return 0;
  auto fee = static_cast<double>(row.estimated_value);
  if (!row.listed) fee *= static_cast<double>(UNLISTED_FEE_PREMIUM);
  if (row.contract_years <= 1)
    fee *= static_cast<double>(EXPIRING_FEE_DISCOUNT);
  return static_cast<std::int64_t>(std::llround(fee));
}

ScoutedPlayerRow ScoutingSystem::makeRow(const Player& player) const
{
  ScoutedPlayerRow row;
  row.player_id = player.getId();
  row.team_id = player.getTeamId();
  row.role = player.getRole();
  row.age = static_cast<std::uint8_t>(player.getAge());
  row.wage = player.getWage();
  row.contract_years = player.getContractYears();
  row.listed = player.getTransferStatus() == TransferStatus::Listed;
  row.injured = player.getDynamics().injury_days > 0;

  const float knowledge = knowledgeOf(player.getId());
  row.knowledge = toPercent(knowledge);
  const Noise noise = noiseFor(player, knowledge);
  float overall_sd = 0.0f;
  row.overall = estimatedOverall(player, noise.attribute_sd, &overall_sd);
  row.overall_low = std::max(0.0f, row.overall - RANGE_Z * overall_sd);
  row.overall_high = std::min(static_cast<float>(MAX_STAT_VAL),
                              row.overall + RANGE_Z * overall_sd);
  // Potential regresses towards the growth typical for his age. [P]
  const float prior =
      row.overall +
      PROSPECT_GROWTH_PER_YEAR *
          static_cast<float>(std::max(0, PEAK_GROWTH_AGE - player.getAge()));
  const float tau2 = POTENTIAL_POPULATION_SD * POTENTIAL_POPULATION_SD;
  const float reliability =
      tau2 / (tau2 + noise.potential_sd * noise.potential_sd);
  const float observed =
      player.getPotential() +
      standardNormal(player.getId(), POTENTIAL_KEY) * noise.potential_sd;
  const float centre = std::lerp(prior, observed, reliability);
  const float half_width =
      RANGE_Z * std::sqrt(reliability) * noise.potential_sd;
  row.potential_low = std::max(row.overall, centre - half_width);
  row.potential_high = std::clamp(centre + half_width, row.potential_low,
                                  static_cast<float>(MAX_STAT_VAL) - 1.0f);
  row.potential_low = std::min(row.potential_low, row.potential_high);
  // Past the growth years a player has no headroom left: the ceiling
  // shrinks to what his current ability might be. [P]
  const float headroom =
      std::clamp(static_cast<float>(NO_GROWTH_AGE - player.getAge()) /
                     static_cast<float>(NO_GROWTH_AGE - PEAK_GROWTH_AGE),
                 0.0f, 1.0f);
  if (const float ceiling = std::max(row.overall, row.overall_high);
      row.potential_high > ceiling)
    row.potential_high = ceiling + headroom * (row.potential_high - ceiling);
  row.potential_low = std::min(row.potential_low, row.potential_high);
  row.estimated_value = estimatedValue(
      player, row.overall, 0.5f * (row.potential_low + row.potential_high));
  row.matches_focus = anyFocusMatches(row);
  return row;
}

std::optional<ScoutedPlayerRow> ScoutingSystem::row(PlayerID player_id) const
{
  const auto player = gamedata->getPlayer(player_id);
  if (!player) return std::nullopt;
  ScoutedPlayerRow result = makeRow(player->get());
  result.shortlisted = isShortlisted(player_id);
  return result;
}

std::optional<ScoutedPlayerView> ScoutingSystem::view(PlayerID player_id) const
{
  const auto player_ref = gamedata->getPlayer(player_id);
  if (!player_ref) return std::nullopt;
  const Player& player = player_ref->get();
  const ScoutedPlayerRow summary = makeRow(player);

  ScoutedPlayerView result;
  result.player_id = player_id;
  result.own = state.team_id != FREE_AGENTS_TEAM_ID &&
               player.getTeamId() == state.team_id;
  result.knowledge = summary.knowledge;
  result.overall = summary.overall;
  result.potential_low = summary.potential_low;
  result.potential_high = summary.potential_high;
  result.estimated_value = summary.estimated_value;
  result.overall_low = summary.overall_low;
  result.overall_high = summary.overall_high;

  const Noise noise = noiseFor(player, knowledgeOf(player_id));

  result.attributes.reserve(player.getStats().size());
  for (const auto& [name, value] : player.getStats())
  {
    const Estimate estimate =
        estimateStat(player, name, value, noise.attribute_sd);
    ScoutedAttribute attribute;
    attribute.name = name;
    attribute.estimate = estimate.value;
    attribute.low = clampStat(estimate.value - RANGE_Z * estimate.sd);
    attribute.high = clampStat(estimate.value + RANGE_Z * estimate.sd);
    result.attributes.push_back(std::move(attribute));
  }
  for (auto report = state.reports.rbegin(); report != state.reports.rend();
       ++report)
  {
    if (report->player_id != player_id) continue;
    result.latest_report_id = report->id;
    break;
  }
  return result;
}

bool ScoutingSystem::matchesFocus(const RecruitmentFocus& focus,
                                  const ScoutedPlayerRow& row)
{
  if (focus.role != PlayerRole::UNKNOWN && focus.role != row.role) return false;
  if (row.age < focus.min_age || row.age > focus.max_age) return false;
  if (row.overall < static_cast<float>(focus.min_ability)) return false;
  if (focus.max_fee > 0 && row.team_id != FREE_AGENTS_TEAM_ID &&
      row.estimated_value > focus.max_fee)
    return false;
  if (focus.max_wage > 0 && static_cast<float>(row.wage) * EXPECTED_WAGE_RAISE >
                                static_cast<float>(focus.max_wage))
    return false;
  return true;
}

bool ScoutingSystem::anyFocusMatches(const ScoutedPlayerRow& row) const
{
  return std::ranges::any_of(state.focuses,
                             [&row](const RecruitmentFocus& focus)
                             { return matchesFocus(focus, row); });
}

std::vector<ScoutedPlayerRow> ScoutingSystem::search(
    const ScoutSearchFilter& filter) const
{
  std::unordered_set<PlayerID> shortlisted;
  shortlisted.reserve(state.shortlist.size());
  for (const ShortlistEntry& entry : state.shortlist)
    shortlisted.insert(entry.player_id);

  std::vector<ScoutedPlayerRow> rows;
  for (const Player& player : gamedata->getPlayersVector())
  {
    // Cheap public filters first; estimates only for the survivors.
    if (!passesPlayerFilters(*gamedata, state.team_id, player, filter))
      continue;
    ScoutedPlayerRow row = makeRow(player);
    if (!passesEstimatedFilters(row, filter)) continue;
    row.shortlisted = shortlisted.contains(row.player_id);
    rows.push_back(row);
  }
  if (filter.limit > 0 && rows.size() > filter.limit)
  {
    std::ranges::partial_sort(
        rows, rows.begin() + static_cast<std::ptrdiff_t>(filter.limit),
        betterScoutedRows);
    rows.resize(filter.limit);
  }
  else
  {
    std::ranges::sort(rows, betterScoutedRows);
  }
  return rows;
}

// ---------------------------------------------------------------------------
// Assignments
// ---------------------------------------------------------------------------

const ScoutAssignment* ScoutingSystem::activeAssignment(
    std::uint32_t scout_id) const
{
  for (const ScoutAssignment& assignment : state.assignments)
  {
    if (!assignment.finished && assignment.scout_id == scout_id)
      return &assignment;
  }
  return nullptr;
}

bool ScoutingSystem::isForeignTarget(ScoutTargetKind kind,
                                     std::uint32_t target_id) const
{
  using enum ScoutTargetKind;
  const auto own = gamedata->getTeam(state.team_id);
  if (!own) return false;
  const LeagueID own_country = countryOf(own->get().getLeagueId());
  switch (kind)
  {
    case Player:
      if (const auto player = gamedata->getPlayer(target_id))
        return isForeign(player->get());
      return false;
    case League:
    case Country:
      return countryOf(static_cast<LeagueID>(target_id)) != own_country;
    case Region:
      return true;  // A tour of a continent is always a long trip.
    case FreeAgents:
      break;
  }
  return false;
}

std::int64_t ScoutingSystem::assignmentCost(ScoutTargetKind kind,
                                            std::uint32_t target_id,
                                            std::uint16_t days) const
{
  using enum ScoutTargetKind;
  switch (kind)
  {
    case Player:
    {
      if (const auto player = gamedata->getPlayer(target_id);
          !player || (state.team_id != FREE_AGENTS_TEAM_ID &&
                      player->get().getTeamId() == state.team_id))
        return 0;
      break;
    }
    case League:
      if (target_id > 0xFF ||
          !gamedata->getLeague(static_cast<LeagueID>(target_id)))
        return 0;
      break;
    case Country:
    {
      if (target_id > 0xFF) return 0;
      if (const auto league =
              gamedata->getLeague(static_cast<LeagueID>(target_id));
          !league || league->get().getParentLeagueID().has_value())
        return 0;
      break;
    }
    case Region:
      if (target_id >= static_cast<std::uint32_t>(
                           std::to_underlying(Continent::COUNT)) ||
          std::ranges::none_of(
              worldCountries(),
              [target_id](LeagueID country)
              {
                return static_cast<std::uint32_t>(std::to_underlying(
                           countryContinent(country))) == target_id;
              }))
        return 0;
      break;
    case FreeAgents:
      return DAILY_COST_FREE_AGENTS * days;
  }
  return (isForeignTarget(kind, target_id) ? DAILY_COST_FOREIGN
                                           : DAILY_COST_DOMESTIC) *
         days;
}

ScoutAssignError ScoutingSystem::startAssignment(const GameDateValue& date,
                                                 std::uint32_t scout_id,
                                                 ScoutTargetKind kind,
                                                 std::uint32_t target_id,
                                                 std::uint16_t days)
{
  using enum ScoutAssignError;
  const std::scoped_lock lock(cache_mutex);
  const auto team = gamedata->getTeam(state.team_id);
  if (state.team_id == FREE_AGENTS_TEAM_ID || !team) return NoTeam;
  refreshScouts();
  ensureExpertise();
  if (findScout(scout_id) == nullptr) return UnknownScout;
  if (activeAssignment(scout_id) != nullptr) return ScoutBusy;
  if (days < MIN_DURATION_DAYS || days > MAX_DURATION_DAYS)
    return InvalidDuration;
  const std::int64_t cost = assignmentCost(kind, target_id, days);
  if (cost <= 0) return InvalidTarget;
  Finances& finances = team->get().getFinances();
  if (finances.getBalance() < cost) return InsufficientFunds;

  finances.record(date, FinanceCategory::Staff, -cost);
  ScoutAssignment assignment;
  assignment.id = state.next_assignment_id++;
  assignment.scout_id = scout_id;
  assignment.kind = kind;
  assignment.target_id = target_id;
  assignment.start_date = date;
  assignment.duration_days = days;
  assignment.cost = cost;
  state.assignments.push_back(assignment);
  return None;
}

bool ScoutingSystem::cancelAssignment(std::uint32_t assignment_id)
{
  for (ScoutAssignment& assignment : state.assignments)
  {
    if (assignment.id != assignment_id || assignment.finished) continue;
    assignment.finished = true;
    pruneHistory();
    return true;
  }
  return false;
}

std::vector<PlayerID> ScoutingSystem::coverage(
    const ScoutAssignment& assignment) const
{
  std::vector<PlayerID> players;
  const auto addTeam = [&](TeamID team_id)
  {
    if (state.team_id != FREE_AGENTS_TEAM_ID && team_id == state.team_id)
      return;
    for (const Player& player : gamedata->getPlayersForTeam(team_id))
      players.push_back(player.getId());
  };
  const auto addLeagueTeams = [&addTeam](const League& league)
  {
    for (const TeamID team_id : league.getTeamIDs()) addTeam(team_id);
  };
  switch (assignment.kind)
  {
    case ScoutTargetKind::Player:
      break;
    case ScoutTargetKind::League:
      if (const auto league =
              gamedata->getLeague(static_cast<LeagueID>(assignment.target_id)))
        addLeagueTeams(league->get());
      break;
    case ScoutTargetKind::Country:
      for (const League& league : gamedata->getLeaguesVector())
      {
        if (countryOf(league.getId()) !=
            static_cast<LeagueID>(assignment.target_id))
          continue;
        addLeagueTeams(league);
      }
      break;
    case ScoutTargetKind::Region:
      for (const League& league : gamedata->getLeaguesVector())
      {
        if (static_cast<std::uint32_t>(std::to_underlying(countryContinent(
                countryOf(league.getId())))) != assignment.target_id)
          continue;
        addLeagueTeams(league);
      }
      break;
    case ScoutTargetKind::FreeAgents:
      addTeam(FREE_AGENTS_TEAM_ID);
      break;
  }
  return players;
}

std::array<float, ScoutingSystem::ROLE_COUNT> ScoutingSystem::ownBestByRole()
    const
{
  std::array<float, ROLE_COUNT> best{};
  if (state.team_id == FREE_AGENTS_TEAM_ID) return best;
  const StatsConfig& config = gamedata->getStatsConfig();
  for (const Player& player : gamedata->getPlayersForTeam(state.team_id))
  {
    const auto role =
        static_cast<std::size_t>(std::to_underlying(player.getRole()));
    if (role >= ROLE_COUNT) continue;
    best[role] =
        std::max(best[role], static_cast<float>(player.getOverall(config)));
  }
  return best;
}

std::vector<float> ScoutingSystem::coverageWeights(
    const std::vector<PlayerID>& candidates,
    const std::array<float, ROLE_COUNT>& best_own) const
{
  std::vector<float> weights;
  weights.reserve(candidates.size());
  for (const PlayerID player_id : candidates)
  {
    const auto player = gamedata->getPlayer(player_id);
    const ScoutedPlayerRow estimate = makeRow(player->get());
    float weight = 1.0f;
    if (estimate.matches_focus)
      weight += 9.0f;
    else if (state.focuses.empty())
    {
      const auto role =
          static_cast<std::size_t>(std::to_underlying(estimate.role));
      if (role < ROLE_COUNT && estimate.overall >= best_own[role] - 2.0f)
        weight += 2.0f;
    }
    if (estimate.listed || estimate.contract_years <= 1) weight += 1.0f;
    if (estimate.knowledge >= 80)
      weight *= 0.3f;
    else if (estimate.knowledge < REPORT_KNOWLEDGE &&
             state.knowledge.contains(player_id))
      weight *= FOLLOW_UP_WEIGHT;
    weights.push_back(weight);
  }
  return weights;
}

void ScoutingSystem::observeCoveragePick(ScoutAssignment& assignment,
                                         PlayerID player_id,
                                         const ScoutProfile& scout,
                                         float multiplier,
                                         std::vector<LeagueID>& leagues_seen)
{
  observe(player_id, COVERAGE_OBSERVATION_GAIN * multiplier, &scout);
  ++assignment.players_observed;
  if (const auto team =
          gamedata->getTeam(gamedata->getPlayer(player_id)->get().getTeamId());
      team && team->get().getId() != FREE_AGENTS_TEAM_ID &&
      std::ranges::find(leagues_seen, team->get().getLeagueId()) ==
          leagues_seen.end())
    leagues_seen.push_back(team->get().getLeagueId());
}

void ScoutingSystem::fileCoverageReport(ScoutAssignment& assignment,
                                        PlayerID player_id,
                                        const ScoutProfile& scout,
                                        const GameDateValue& date,
                                        std::int32_t ordinal,
                                        int& reports_today, Inbox& inbox)
{
  const auto entry = state.knowledge.find(player_id);
  if (entry == state.knowledge.end() ||
      entry->second.knowledge < REPORT_KNOWLEDGE ||
      reports_today >= MAX_REPORTS_PER_ASSIGNMENT_DAY)
    return;
  if (entry->second.last_report_day != 0 &&
      ordinal - entry->second.last_report_day < REPORT_COOLDOWN_DAYS)
    return;
  const Player& player = gamedata->getPlayer(player_id)->get();
  if (!state.focuses.empty() && !anyFocusMatches(makeRow(player))) return;
  const ScoutReport& report =
      fileReport(date, player, scout, ordinal, assignment.id);
  ++assignment.reports_filed;
  ++reports_today;
  if (report.grade != ScoutGrade::A) return;
  post(inbox,
       InboxMessage{
           .date = date,
           .category = InboxCategory::Transfer,
           .title_key = "SCOUT_MSG_RECOMMEND_TITLE",
           .body_key = "SCOUT_MSG_RECOMMEND_BODY",
           .args = {scout.name, player.getName(),
                    RoleUtils::shortNameArg(player.getRole()),
                    std::to_string(player.getAge()),
                    std::format("{:.0f}", static_cast<double>(report.overall)),
                    rangeText(report.potential_low, report.potential_high),
                    formatMoney(report.estimated_fee),
                    std::to_string(report.confidence)},
           .player_id = player_id,
           .team_id = player.getTeamId()});
}

void ScoutingSystem::coverageDay(ScoutAssignment& assignment,
                                 const ScoutProfile& base_scout,
                                 const GameDateValue& date,
                                 std::int32_t ordinal, Inbox& inbox)
{
  // Regional expertise decides how many players he gets to see, how much he
  // learns from each and how sharp his judgement is.
  const float multiplier =
      effectiveness(base_scout.id, assignment.kind, assignment.target_id)
          .multiplier;
  const ScoutProfile scout = effectiveScout(base_scout, multiplier);
  const std::vector<PlayerID> candidates = coverage(assignment);
  if (candidates.empty()) return;
  const auto best_own = ownBestByRole();
  std::vector<float> weights = coverageWeights(candidates, best_own);

  WorldRng rng = WorldRng::stream(gamedata->getWorldSeed(), RngDomain::Scouting,
                                  mixHash(COVERAGE_KEY, assignment.id),
                                  static_cast<std::uint64_t>(ordinal));
  int reports_today = 0;
  const int per_day = std::max(
      1, static_cast<int>(std::lround(
             static_cast<float>(COVERAGE_PLAYERS_PER_DAY) * multiplier)));
  const int picks = std::min<int>(per_day, static_cast<int>(candidates.size()));
  std::vector<LeagueID> leagues_seen;
  for (int pick = 0; pick < picks; ++pick)
  {
    const std::size_t index = rng.weightedIndex(weights);
    if (weights[index] <= 0.0f) break;
    weights[index] = 0.0f;
    const PlayerID player_id = candidates[index];
    observeCoveragePick(assignment, player_id, scout, multiplier, leagues_seen);
    fileCoverageReport(assignment, player_id, scout, date, ordinal,
                       reports_today, inbox);
  }
  for (const LeagueID league_id : leagues_seen)
    addExperience(base_scout.id, league_id);
}

const ScoutReport& ScoutingSystem::fileReport(const GameDateValue& date,
                                              const Player& player,
                                              const ScoutProfile& scout,
                                              std::int32_t ordinal,
                                              std::uint32_t assignment_id)
{
  const ScoutedPlayerRow estimate = makeRow(player);
  ScoutReport report;
  report.id = state.next_report_id++;
  report.date = date;
  report.player_id = player.getId();
  report.assignment_id = assignment_id;
  report.scout_id = scout.id;
  report.scout_name = scout.name;
  report.knowledge = estimate.knowledge;
  report.confidence = toPercent(
      static_cast<float>(estimate.knowledge) *
      (0.6f + 0.4f * static_cast<float>(scout.judging_ability) / 100.0f));
  report.overall = estimate.overall;
  report.overall_low = estimate.overall_low;
  report.overall_high = estimate.overall_high;
  report.potential_low = estimate.potential_low;
  report.potential_high = estimate.potential_high;
  report.estimated_fee = estimatedFee(player, estimate);

  const auto best_own = ownBestByRole();
  const auto role = static_cast<std::size_t>(std::to_underlying(estimate.role));
  const float own_best = role < ROLE_COUNT ? best_own[role] : 0.0f;
  // He fits when he would compete for a place now (or, if young, soon) and
  // matches one of the manager's stated needs.
  const float potential_mid =
      0.5f * (estimate.potential_low + estimate.potential_high);
  const bool good_enough =
      own_best <= 0.0f || estimate.overall >= own_best - FIT_MARGIN ||
      (estimate.age <= PROSPECT_AGE && potential_mid >= own_best);
  report.fits_need =
      good_enough && (state.focuses.empty() || estimate.matches_focus);

  if (const auto team = gamedata->getTeam(state.team_id))
  {
    const Finances& finances = team->get().getFinances();
    const std::int64_t payroll =
        finances.getCurrentWageSpending(*gamedata, team->get());
    const std::int64_t wage_room = finances.getWageBudget() - payroll;
    const std::int64_t budget = budget_provider
                                    ? budget_provider(state.team_id)
                                    : ClubEconomy::availableTransferBudget(
                                          finances.getTransferBudget(),
                                          finances.getBalance(), payroll, 0);
    const auto expected_wage = static_cast<std::int64_t>(
        static_cast<float>(estimate.wage) * EXPECTED_WAGE_RAISE);
    report.affordable =
        report.estimated_fee <= budget && expected_wage <= wage_room;
  }

  bool available = estimate.team_id == FREE_AGENTS_TEAM_ID || estimate.listed ||
                   estimate.contract_years <= 1;
  if (!available)
  {
    // A club lets fringe players go; its starters are hard to prise away.
    const StatsConfig& config = gamedata->getStatsConfig();
    const double overall = player.getOverall(config);
    int better = 0;
    for (const Player& mate : gamedata->getPlayersForTeam(player.getTeamId()))
    {
      if (mate.getId() != player.getId() && mate.getOverall(config) > overall)
        ++better;
    }
    available = better >= STARTER_RANK;
  }
  report.available = available;

  if (const int score = static_cast<int>(report.fits_need) +
                        static_cast<int>(report.affordable) +
                        static_cast<int>(report.available);
      score == 3)
    report.grade = ScoutGrade::A;
  else if (score == 2)
    report.grade = ScoutGrade::B;
  else
    report.grade = ScoutGrade::C;
  state.knowledge[player.getId()].last_report_day = ordinal;
  state.reports.push_back(std::move(report));
  return state.reports.back();
}

void ScoutingSystem::progressAssignment(ScoutAssignment& assignment,
                                        const GameDateValue& date,
                                        std::int32_t ordinal, Inbox& inbox)
{
  const ScoutProfile* scout = findScout(assignment.scout_id);
  if (scout == nullptr)
  {
    assignment.finished = true;  // The scout left the club.
    return;
  }
  ++assignment.days_done;
  if (assignment.kind == ScoutTargetKind::Player)
  {
    const auto player = gamedata->getPlayer(assignment.target_id);
    if (!player)
    {
      assignment.finished = true;  // Retired.
      return;
    }
    const float multiplier =
        effectiveness(scout->id, assignment.kind, assignment.target_id)
            .multiplier;
    const ScoutProfile effective = effectiveScout(*scout, multiplier);
    observe(assignment.target_id, PLAYER_ASSIGNMENT_DAILY_GAIN * multiplier,
            &effective);
    assignment.players_observed = 1;
    if (const auto team = gamedata->getTeam(player->get().getTeamId());
        team && team->get().getId() != FREE_AGENTS_TEAM_ID)
      addExperience(scout->id, team->get().getLeagueId());
  }
  else
  {
    coverageDay(assignment, *scout, date, ordinal, inbox);
  }
  if (assignment.days_done >= assignment.duration_days)
    finishAssignment(assignment, date, inbox);
}

void ScoutingSystem::finishAssignment(ScoutAssignment& assignment,
                                      const GameDateValue& date, Inbox& inbox)
{
  assignment.finished = true;
  const ScoutProfile* scout = findScout(assignment.scout_id);
  if (scout == nullptr) return;
  if (assignment.kind == ScoutTargetKind::Player)
  {
    finishPlayerAssignment(assignment, *scout, date, inbox);
    return;
  }
  finishCoverageAssignment(assignment, *scout, date, inbox);
}

void ScoutingSystem::finishPlayerAssignment(ScoutAssignment& assignment,
                                            const ScoutProfile& scout,
                                            const GameDateValue& date,
                                            Inbox& inbox)
{
  const auto player_ref = gamedata->getPlayer(assignment.target_id);
  if (!player_ref) return;
  const Player& player = player_ref->get();
  const ScoutProfile effective = effectiveScout(
      scout, effectiveness(scout.id, assignment.kind, assignment.target_id)
                 .multiplier);
  const ScoutReport& report =
      fileReport(date, player, effective, dayOrdinal(date), assignment.id);
  assignment.reports_filed = 1;
  InboxMessage message{
      .date = date,
      .category = InboxCategory::Transfer,
      .title_key = "SCOUT_MSG_REPORT_TITLE",
      .body_key = "SCOUT_MSG_REPORT_BODY",
      .args = {player.getName(), gradeLetter(report.grade), scout.name,
               RoleUtils::shortNameArg(player.getRole()),
               std::to_string(player.getAge()),
               std::format("{:.0f}", static_cast<double>(report.overall)),
               rangeText(report.potential_low, report.potential_high),
               formatMoney(report.estimated_fee),
               std::to_string(report.confidence),
               std::string("@") + scoutGradeKey(report.grade),
               report.fits_need ? "@SCOUT_REASON_FITS" : "@SCOUT_REASON_NO_FIT",
               report.affordable ? "@SCOUT_REASON_AFFORDABLE"
                                 : "@SCOUT_REASON_EXPENSIVE",
               report.available ? "@SCOUT_REASON_AVAILABLE"
                                : "@SCOUT_REASON_UNAVAILABLE"},
      .player_id = player.getId(),
      .team_id = player.getTeamId()};
  post(inbox, std::move(message));
}

void ScoutingSystem::finishCoverageAssignment(const ScoutAssignment& assignment,
                                              const ScoutProfile& scout,
                                              const GameDateValue& date,
                                              Inbox& inbox)
{
  std::vector<const ScoutReport*> filed;
  for (const ScoutReport& report : state.reports)
  {
    if (report.assignment_id == assignment.id) filed.push_back(&report);
  }
  std::ranges::sort(filed,
                    [](const ScoutReport* a, const ScoutReport* b)
                    {
                      return a->grade != b->grade ? a->grade < b->grade
                                                  : a->overall > b->overall;
                    });
  std::string best;
  for (std::size_t index = 0; index < std::min<std::size_t>(3, filed.size());
       ++index)
  {
    const auto player = gamedata->getPlayer(filed[index]->player_id);
    if (!player) continue;
    if (!best.empty()) best += ", ";
    best += std::format("{} ({}, {:.0f})", player->get().getName(),
                        gradeLetter(filed[index]->grade),
                        static_cast<double>(filed[index]->overall));
  }
  std::string target_name;
  if (assignment.kind == ScoutTargetKind::League ||
      assignment.kind == ScoutTargetKind::Country)
  {
    if (const auto league =
            gamedata->getLeague(static_cast<LeagueID>(assignment.target_id)))
      target_name = Competitions::leagueNameArg(league->get());
  }
  else if (assignment.kind == ScoutTargetKind::Region)
  {
    target_name = std::string("@") +
                  continentKey(static_cast<Continent>(assignment.target_id));
  }
  post(inbox,
       InboxMessage{
           .date = date,
           .category = InboxCategory::Transfer,
           .title_key = "SCOUT_MSG_DONE_TITLE",
           .body_key = "SCOUT_MSG_DONE_BODY",
           .args = {scout.name,
                    std::string("@") + scoutTargetKindKey(assignment.kind),
                    target_name, std::to_string(assignment.days_done),
                    std::to_string(assignment.players_observed),
                    std::to_string(assignment.reports_filed),
                    best.empty() ? "@SCOUT_MSG_NO_RECOMMENDATIONS" : best},
           .player_id = std::nullopt,
           .team_id = std::nullopt});
}

// ---------------------------------------------------------------------------
// Daily processing
// ---------------------------------------------------------------------------

void ScoutingSystem::onDayAdvanced(const GameDateValue& date, Inbox& inbox)
{
  const std::scoped_lock lock(cache_mutex);
  if (state.team_id == FREE_AGENTS_TEAM_ID) return;
  const std::int32_t ordinal = dayOrdinal(date);
  priors.clear();  // Squads and attributes change from day to day.
  refreshScouts();
  ensureExpertise();

  for (auto entry = state.knowledge.begin(); entry != state.knowledge.end();)
  {
    const auto player = gamedata->getPlayer(entry->first);
    if (!player)
    {
      entry = state.knowledge.erase(entry);
      continue;
    }
    const float baseline = baselineKnowledge(player->get());
    entry->second.knowledge =
        std::max(baseline, entry->second.knowledge - DAILY_DECAY);
    const bool report_recent =
        entry->second.last_report_day != 0 &&
        ordinal - entry->second.last_report_day < REPORT_COOLDOWN_DAYS;
    if ((entry->second.knowledge <= baseline && !report_recent) ||
        baseline >= OWN_KNOWLEDGE)
      entry = state.knowledge.erase(entry);
    else
      ++entry;
  }

  for (ScoutAssignment& assignment : state.assignments)
  {
    if (!assignment.finished)
      progressAssignment(assignment, date, ordinal, inbox);
  }
  checkShortlist(date, inbox);
  pruneHistory();
}

void ScoutingSystem::onManagedMatch(TeamID opponent_id)
{
  if (state.team_id == FREE_AGENTS_TEAM_ID || opponent_id == state.team_id)
    return;
  for (const Player& player : gamedata->getPlayersForTeam(opponent_id))
    observe(player.getId(), MATCH_OBSERVATION_GAIN, nullptr);
}

void ScoutingSystem::pruneHistory()
{
  auto finished = static_cast<std::size_t>(
      std::ranges::count_if(state.assignments, &ScoutAssignment::finished));
  for (auto it = state.assignments.begin();
       it != state.assignments.end() && finished > MAX_FINISHED_ASSIGNMENTS;)
  {
    if (it->finished)
    {
      it = state.assignments.erase(it);
      --finished;
    }
    else
    {
      ++it;
    }
  }
  if (state.reports.size() > MAX_REPORTS)
    state.reports.erase(
        state.reports.begin(),
        state.reports.begin() +
            static_cast<std::ptrdiff_t>(state.reports.size() - MAX_REPORTS));
}

// ---------------------------------------------------------------------------
// Recruitment focus
// ---------------------------------------------------------------------------

std::uint32_t ScoutingSystem::upsertFocus(RecruitmentFocus focus)
{
  if (focus.min_age > focus.max_age || focus.max_fee < 0) return 0;
  if (focus.id == 0)
  {
    if (state.focuses.size() >= MAX_FOCUSES) return 0;
    focus.id = state.next_focus_id++;
    state.focuses.push_back(focus);
    return focus.id;
  }
  const auto found =
      std::ranges::find(state.focuses, focus.id, &RecruitmentFocus::id);
  if (found == state.focuses.end()) return 0;
  *found = focus;
  return focus.id;
}

bool ScoutingSystem::removeFocus(std::uint32_t focus_id)
{
  return std::erase_if(state.focuses, [focus_id](const RecruitmentFocus& focus)
                       { return focus.id == focus_id; }) > 0;
}

// ---------------------------------------------------------------------------
// Shortlist
// ---------------------------------------------------------------------------

std::uint8_t ScoutingSystem::shortlistFlags(const Player& player) const
{
  std::byte flags{0};
  if (player.getTransferStatus() == TransferStatus::Listed)
    flags |= std::byte{std::to_underlying(ShortlistFlag::Listed)};
  if (player.getTeamId() != FREE_AGENTS_TEAM_ID &&
      player.getContractYears() <= 1)
    flags |= std::byte{std::to_underlying(ShortlistFlag::Expiring)};
  if (player.getDynamics().injury_days > 0)
    flags |= std::byte{std::to_underlying(ShortlistFlag::Injured)};
  return std::to_integer<std::uint8_t>(flags);
}

bool ScoutingSystem::isShortlisted(PlayerID player_id) const
{
  return std::ranges::find(state.shortlist, player_id,
                           &ShortlistEntry::player_id) != state.shortlist.end();
}

bool ScoutingSystem::addToShortlist(const GameDateValue& date,
                                    PlayerID player_id)
{
  const auto player = gamedata->getPlayer(player_id);
  if (!player || state.team_id == FREE_AGENTS_TEAM_ID ||
      player->get().getTeamId() == state.team_id || isShortlisted(player_id))
    return false;
  state.shortlist.emplace_back(player_id, date, player->get().getTeamId(),
                               shortlistFlags(player->get()));
  return true;
}

bool ScoutingSystem::removeFromShortlist(PlayerID player_id)
{
  return std::erase_if(state.shortlist, [player_id](const ShortlistEntry& entry)
                       { return entry.player_id == player_id; }) > 0;
}

void ScoutingSystem::checkShortlist(const GameDateValue& date, Inbox& inbox)
{
  for (auto entry = state.shortlist.begin(); entry != state.shortlist.end();)
  {
    const auto player_ref = gamedata->getPlayer(entry->player_id);
    if (!player_ref || player_ref->get().getTeamId() == state.team_id)
    {
      // Retired or signed by us: nothing left to track.
      entry = state.shortlist.erase(entry);
      continue;
    }
    const Player& player = player_ref->get();
    const std::byte flags = static_cast<std::byte>(shortlistFlags(player));
    const std::byte raised = flags & ~static_cast<std::byte>(entry->last_flags);
    const std::string name = player.getName();
    if ((raised & std::byte{std::to_underlying(ShortlistFlag::Listed)}) !=
        std::byte{0})
      post(inbox, InboxMessage{.date = date,
                               .category = InboxCategory::Transfer,
                               .title_key = "SCOUT_ALERT_LISTED_TITLE",
                               .body_key = "SCOUT_ALERT_LISTED_BODY",
                               .args = {name},
                               .player_id = player.getId(),
                               .team_id = player.getTeamId()});
    if ((raised & std::byte{std::to_underlying(ShortlistFlag::Expiring)}) !=
        std::byte{0})
      post(inbox, InboxMessage{.date = date,
                               .category = InboxCategory::Contract,
                               .title_key = "SCOUT_ALERT_EXPIRING_TITLE",
                               .body_key = "SCOUT_ALERT_EXPIRING_BODY",
                               .args = {name},
                               .player_id = player.getId(),
                               .team_id = player.getTeamId()});
    if ((raised & std::byte{std::to_underlying(ShortlistFlag::Injured)}) !=
        std::byte{0})
      post(inbox,
           InboxMessage{
               .date = date,
               .category = InboxCategory::Injury,
               .title_key = "SCOUT_ALERT_INJURED_TITLE",
               .body_key = "SCOUT_ALERT_INJURED_BODY",
               .args = {name, std::to_string(player.getDynamics().injury_days)},
               .player_id = player.getId(),
               .team_id = player.getTeamId()});
    if (player.getTeamId() != entry->last_team)
    {
      std::string club = "@TRANSFER_FREE_AGENT_LABEL";
      if (const auto team = gamedata->getTeam(player.getTeamId());
          team && player.getTeamId() != FREE_AGENTS_TEAM_ID)
        club = team->get().getName();
      post(inbox, InboxMessage{.date = date,
                               .category = InboxCategory::Transfer,
                               .title_key = "SCOUT_ALERT_MOVED_TITLE",
                               .body_key = "SCOUT_ALERT_MOVED_BODY",
                               .args = {name, club},
                               .player_id = player.getId(),
                               .team_id = player.getTeamId()});
    }
    entry->last_flags = std::to_integer<std::uint8_t>(flags);
    entry->last_team = player.getTeamId();
    ++entry;
  }
}

std::vector<SquadComparisonRow> ScoutingSystem::compareWithSquad() const
{
  std::vector<SquadComparisonRow> rows(ROLE_COUNT);
  for (std::size_t role = 0; role < ROLE_COUNT; ++role)
    rows[role].role = static_cast<PlayerRole>(role);
  if (state.team_id == FREE_AGENTS_TEAM_ID) return rows;
  const StatsConfig& config = gamedata->getStatsConfig();
  for (const Player& player : gamedata->getPlayersForTeam(state.team_id))
  {
    const auto role =
        static_cast<std::size_t>(std::to_underlying(player.getRole()));
    if (role >= ROLE_COUNT) continue;
    SquadComparisonRow& row = rows[role];
    ++row.own_count;
    const auto overall = static_cast<float>(player.getOverall(config));
    if (row.own_best_id == 0 || overall > row.own_best_overall)
    {
      row.own_best_id = player.getId();
      row.own_best_overall = overall;
    }
  }
  for (const ShortlistEntry& entry : state.shortlist)
  {
    const auto player = gamedata->getPlayer(entry.player_id);
    if (!player) continue;
    const ScoutedPlayerRow estimate = makeRow(player->get());
    const auto role =
        static_cast<std::size_t>(std::to_underlying(estimate.role));
    if (role >= ROLE_COUNT) continue;
    SquadComparisonRow& row = rows[role];
    if (row.candidate_id != 0 && estimate.overall <= row.candidate_overall)
      continue;
    row.candidate_id = estimate.player_id;
    row.candidate_overall = estimate.overall;
    row.candidate_potential_low = estimate.potential_low;
    row.candidate_potential_high = estimate.potential_high;
    row.candidate_knowledge = estimate.knowledge;
  }
  return rows;
}

void ScoutingSystem::restore(ScoutingState restored)
{
  const std::scoped_lock lock(cache_mutex);
  state = std::move(restored);
  priors.clear();
  generated.clear();
  refreshScouts();
  ensureExpertise();
}

// ---------------------------------------------------------------------------
// Scouts: status, expertise and effectiveness
// ---------------------------------------------------------------------------

ScoutStatus ScoutingSystem::scoutStatus(std::uint32_t scout_id) const
{
  if (activeAssignment(scout_id) != nullptr) return ScoutStatus::OnAssignment;
  const bool history =
      std::ranges::any_of(state.assignments,
                          [scout_id](const ScoutAssignment& assignment)
                          { return assignment.scout_id == scout_id; }) ||
      std::ranges::any_of(state.reports, [scout_id](const ScoutReport& report)
                          { return report.scout_id == scout_id; });
  return history ? ScoutStatus::IdleWithHistory : ScoutStatus::IdleNew;
}

std::vector<ScoutSummary> ScoutingSystem::scoutSummaries() const
{
  const std::scoped_lock lock(cache_mutex);
  refreshScouts();
  std::vector<ScoutSummary> summaries;
  summaries.reserve(scouts.size());
  for (const ScoutProfile& scout : scouts)
  {
    ScoutSummary summary;
    summary.profile = scout;
    summary.status = scoutStatus(scout.id);
    if (const ScoutAssignment* active = activeAssignment(scout.id))
      summary.active_assignment_id = active->id;
    for (const ScoutAssignment& assignment : state.assignments)
    {
      if (assignment.scout_id == scout.id && assignment.finished)
        ++summary.finished_assignments;
    }
    for (const ScoutReport& report : state.reports)
    {
      if (report.scout_id != scout.id) continue;
      ++summary.total_reports;
      if (!report.seen) ++summary.unread_reports;
    }
    summaries.push_back(std::move(summary));
  }
  return summaries;
}

std::vector<LeagueID> ScoutingSystem::worldCountries() const
{
  std::vector<LeagueID> countries;
  for (const League& league : gamedata->getLeaguesVector())
  {
    if (!league.getParentLeagueID().has_value())
      countries.push_back(league.getId());
  }
  std::ranges::sort(countries);
  return countries;
}

ScoutExpertise ScoutingSystem::expertiseOf(std::uint32_t scout_id) const
{
  const std::scoped_lock lock(cache_mutex);
  if (const auto stored = state.expertise.find(scout_id);
      stored != state.expertise.end())
    return stored->second;
  if (const auto cached = generated.find(scout_id); cached != generated.end())
    return cached->second;
  // Uses the cached roster: refreshing here would invalidate references to
  // scouts held by the daily processing.
  const ScoutProfile* scout = findScout(scout_id);
  if (scout == nullptr) return {};
  const std::vector<LeagueID> countries = worldCountries();
  return generated
      .try_emplace(scout_id, ScoutExpertiseModel::generate(
                                 scout->nationality, gamedata->getWorldSeed(),
                                 scout_id, countries))
      .first->second;
}

void ScoutingSystem::ensureExpertise()
{
  const std::scoped_lock lock(cache_mutex);
  if (state.team_id == FREE_AGENTS_TEAM_ID) return;
  for (const ScoutProfile& scout : scouts)
  {
    if (!state.expertise.contains(scout.id))
      state.expertise.try_emplace(scout.id, expertiseOf(scout.id));
  }
}

void ScoutingSystem::addExperience(std::uint32_t scout_id, LeagueID league_id)
{
  if (!state.expertise.contains(scout_id))
    state.expertise.try_emplace(scout_id, expertiseOf(scout_id));
  std::uint16_t& days = state.expertise[scout_id].league_days[league_id];
  days = static_cast<std::uint16_t>(
      std::min<int>(days + 1, ScoutExpertiseModel::MAX_LEAGUE_DAYS));
}

std::vector<TargetCountry> ScoutingSystem::targetCountries(
    const ScoutExpertise& expertise, ScoutTargetKind kind,
    std::uint32_t target_id, LeagueID* league) const
{
  using enum ScoutTargetKind;
  *league = 0;
  std::vector<LeagueID> countries;
  switch (kind)
  {
    case Player:
      if (const auto player = gamedata->getPlayer(target_id))
      {
        const TeamID team_id = player->get().getTeamId();
        if (const auto team = gamedata->getTeam(team_id);
            team && team_id != FREE_AGENTS_TEAM_ID)
        {
          *league = team->get().getLeagueId();
          countries.push_back(countryOf(*league));
        }
      }
      break;
    case League:
      *league = static_cast<LeagueID>(target_id);
      countries.push_back(countryOf(*league));
      break;
    case Country:
      countries.push_back(static_cast<LeagueID>(target_id));
      break;
    case Region:
      for (const LeagueID country : worldCountries())
      {
        if (static_cast<std::uint32_t>(
                std::to_underlying(countryContinent(country))) == target_id)
          countries.push_back(country);
      }
      break;
    case FreeAgents:
      break;
  }
  std::vector<TargetCountry> targets;
  targets.reserve(countries.size());
  for (const LeagueID country : countries)
  {
    TargetCountry target;
    target.country = country;
    target.weight = 1.0f / static_cast<float>(countries.size());
    for (const auto& [league_id, days] : expertise.league_days)
    {
      if (countryOf(league_id) == country) target.country_days += days;
    }
    targets.push_back(target);
  }
  return targets;
}

ScoutEffectiveness ScoutingSystem::effectiveness(std::uint32_t scout_id,
                                                 ScoutTargetKind kind,
                                                 std::uint32_t target_id) const
{
  const std::scoped_lock lock(cache_mutex);
  const ScoutProfile* scout = findScout(scout_id);
  if (scout == nullptr) return {};
  const ScoutExpertise expertise = expertiseOf(scout_id);
  LeagueID league = 0;
  const std::vector<TargetCountry> countries =
      targetCountries(expertise, kind, target_id, &league);
  std::uint32_t league_days = 0;
  if (const auto found = expertise.league_days.find(league);
      league != 0 && found != expertise.league_days.end())
    league_days = found->second;
  return ScoutExpertiseModel::compute(expertise, scout->judging_ability,
                                      countries, league, league_days);
}

ScoutProfile ScoutingSystem::effectiveScout(const ScoutProfile& scout,
                                            float multiplier)
{
  // [P] Every 0.1 of effectiveness is worth 5 points of judging.
  const auto adjust = [multiplier](std::uint8_t judging)
  {
    return toPercent(static_cast<float>(judging) + 50.0f * (multiplier - 1.0f));
  };
  ScoutProfile effective = scout;
  effective.judging_ability =
      std::max<std::uint8_t>(1, adjust(scout.judging_ability));
  effective.judging_potential =
      std::max<std::uint8_t>(1, adjust(scout.judging_potential));
  return effective;
}

std::size_t ScoutingSystem::unreadReports() const
{
  return static_cast<std::size_t>(std::ranges::count_if(
      state.reports, [](const ScoutReport& report) { return !report.seen; }));
}

std::size_t ScoutingSystem::unreadReports(std::uint32_t scout_id) const
{
  return static_cast<std::size_t>(std::ranges::count_if(
      state.reports, [scout_id](const ScoutReport& report)
      { return !report.seen && report.scout_id == scout_id; }));
}

bool ScoutingSystem::markReportsSeen(std::uint32_t scout_id)
{
  bool changed = false;
  for (ScoutReport& report : state.reports)
  {
    if (report.scout_id != scout_id || report.seen) continue;
    report.seen = true;
    changed = true;
  }
  return changed;
}
