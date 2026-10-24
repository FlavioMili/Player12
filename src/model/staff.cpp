// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/staff.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "database/gamedata.h"
#include "global/global.h"
#include "model/club_economy.h"
#include "model/team.h"
#include "model/world_generation.h"
#include "model/world_rng.h"
#include "model/world_tuning.h"

namespace
{
using Attr = StaffAttribute;

constexpr std::uint64_t MARKET_KEY = 0x4D41524BULL;
constexpr std::uint64_t CLUB_KEY_B = 0;
constexpr std::uint64_t REFRESH_KEY_B = 1;
constexpr std::uint64_t SEASON_KEY_B = 2;
constexpr int RETIREMENT_AGE = 66;
constexpr float NO_STAFF_QUALITY = 0.25f;

/** Relative pay of each role. [P] */
constexpr std::array<float, STAFF_ROLE_COUNT> ROLE_WAGE_WEIGHT = {
    2.0f, 1.2f, 1.2f, 1.1f, 1.0f, 0.7f, 0.8f, 0.9f, 0.6f, 1.0f};

/** Weekly market wage of a rating-50 member with role weight 1. [P] */
constexpr double MARKET_WAGE_BASE = 3000.0;

std::size_t index(StaffRole role)
{
  return static_cast<std::size_t>(std::to_underlying(role));
}

float unit(std::uint8_t value) { return static_cast<float>(value) / 100.0f; }

std::uint8_t clampAttribute(float value)
{
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 5L, 95L));
}

std::uint32_t roundWage(double wage)
{
  return static_cast<std::uint32_t>(std::lround(std::max(250.0, wage) / 50.0) *
                                    50);
}

/** Quality level (1-100) typical of a club with @p reputation. [P] */
float clubLevel(std::uint8_t reputation)
{
  return 20.0f + 0.62f * static_cast<float>(reputation);
}

StaffMember generateMember(StaffRole role, float level, TeamID team_id,
                           Language nationality, WorldRng& rng, StaffID id)
{
  const NamePool& names = NamePool::instance();
  StaffMember member;
  member.id = id;
  member.team_id = team_id;
  member.first_name = names.first_names[static_cast<std::size_t>(
      rng.uniformInt(0, static_cast<int>(names.first_names.size()) - 1))];
  member.last_name = names.last_names[static_cast<std::size_t>(
      rng.uniformInt(0, static_cast<int>(names.last_names.size()) - 1))];
  member.nationality = nationality;
  member.role = role;
  member.age = static_cast<std::uint8_t>(role == StaffRole::Scout ||
                                                 role == StaffRole::YouthCoach
                                             ? rng.uniformInt(28, 58)
                                             : rng.uniformInt(33, 62));
  // Generalists: every attribute sits well below the specialism. [P]
  for (auto& attribute : member.attributes)
    attribute = clampAttribute(level - 18.0f + rng.normal(0.0f, 9.0f));
  for (const Attr key : StaffModel::keyAttributes(role))
  {
    member.attributes[static_cast<std::size_t>(std::to_underlying(key))] =
        clampAttribute(level + 4.0f + rng.normal(0.0f, 7.0f));
  }
  // Coaches also know the game tactically.
  if (role == StaffRole::AttackingCoach || role == StaffRole::DefendingCoach)
  {
    member.attributes[static_cast<std::size_t>(
        std::to_underlying(Attr::Tactical))] =
        clampAttribute(level - 4.0f + rng.normal(0.0f, 7.0f));
  }
  return member;
}

Language drawNationality(WorldRng& rng, LeagueID league_id)
{
  if (rng.chance(0.75)) return leagueProfile(league_id).domestic_nationality;
  return static_cast<Language>(
      rng.uniformInt(0, static_cast<int>(std::to_underlying(Language::US))));
}

StaffMember generateCandidate(WorldRng& rng, StaffID id, StaffRole role)
{
  StaffMember member = generateMember(
      role, rng.uniform(28.0f, 82.0f), FREE_AGENTS_TEAM_ID,
      static_cast<Language>(rng.uniformInt(
          0, static_cast<int>(std::to_underlying(Language::US)))),
      rng, id);
  member.wage = StaffModel::marketWage(member);
  return member;
}

void fillMarket(StaffRoster& roster, WorldRng& rng)
{
  std::array<std::size_t, STAFF_ROLE_COUNT> per_role{};
  std::size_t total = 0;
  for (const StaffMember* member : roster.market())
  {
    ++per_role[index(member->role)];
    ++total;
  }
  // Each role gets an even share of the market; the scarcest role first.
  while (total < StaffModel::MARKET_SIZE)
  {
    const auto scarcest = static_cast<StaffRole>(
        std::distance(per_role.begin(), std::ranges::min_element(per_role)));
    roster.add(generateCandidate(rng, roster.allocateId(), scarcest));
    ++per_role[index(scarcest)];
    ++total;
  }
}

std::vector<TeamID> sortedClubIds(const GameData& gamedata)
{
  std::vector<TeamID> ids;
  for (const auto& [team_id, team] : gamedata.getTeams())
  {
    if (team_id != FREE_AGENTS_TEAM_ID) ids.push_back(team_id);
  }
  std::ranges::sort(ids);
  return ids;
}

std::vector<StaffID> sortedStaffIds(const StaffRoster& roster)
{
  std::vector<StaffID> ids;
  ids.reserve(roster.all().size());
  for (const auto& [id, member] : roster.all()) ids.push_back(id);
  std::ranges::sort(ids);
  return ids;
}

/** Best attribute value among members of the given roles (0 if none). */
struct AreaScan
{
  float best = 0.0f;
  int count = 0;
};

AreaScan scan(std::span<const StaffMember* const> staff, StaffRole role,
              Attr attribute)
{
  AreaScan result;
  for (const StaffMember* member : staff)
  {
    if (member->role != role) continue;
    result.best = std::max(result.best, unit(member->attribute(attribute)));
    ++result.count;
  }
  return result;
}

/** Specialist quality with depth bonus, else 80% of the assistant. */
float area(std::span<const StaffMember* const> staff, StaffRole specialist,
           Attr attribute)
{
  const AreaScan specialists = scan(staff, specialist, attribute);
  const AreaScan assistant =
      scan(staff, StaffRole::AssistantManager, attribute);
  float quality =
      std::max({specialists.best, 0.8f * assistant.best, NO_STAFF_QUALITY});
  if (specialists.count > 1)
    quality += 0.05f * static_cast<float>(specialists.count - 1);
  return std::min(1.0f, quality);
}
}  // namespace

// ---------------------------------------------------------------------------
// StaffRoster
// ---------------------------------------------------------------------------

void StaffRoster::clear()
{
  const std::scoped_lock lock(cache_mutex);
  members.clear();
  effects_cache.clear();
  cache_valid = false;
  next_id = 1;
}

void StaffRoster::add(StaffMember member)
{
  const std::scoped_lock lock(cache_mutex);
  next_id = std::max(next_id, member.id + 1);
  const StaffID id = member.id;
  members.insert_or_assign(id, std::move(member));
  cache_valid = false;
}

bool StaffRoster::remove(StaffID id)
{
  const std::scoped_lock lock(cache_mutex);
  cache_valid = false;
  return members.erase(id) > 0;
}

const StaffMember* StaffRoster::find(StaffID id) const
{
  const auto found = members.find(id);
  return found == members.end() ? nullptr : &found->second;
}

StaffMember* StaffRoster::find(StaffID id)
{
  const auto found = members.find(id);
  return found == members.end() ? nullptr : &found->second;
}

namespace
{
void sortByRoleAndRating(std::vector<const StaffMember*>& staff)
{
  std::ranges::sort(staff,
                    [](const StaffMember* a, const StaffMember* b)
                    {
                      if (a->role != b->role) return a->role < b->role;
                      const auto rating_a = StaffModel::rating(*a);
                      const auto rating_b = StaffModel::rating(*b);
                      return rating_a != rating_b ? rating_a > rating_b
                                                  : a->id < b->id;
                    });
}
}  // namespace

std::vector<const StaffMember*> StaffRoster::clubStaff(TeamID team_id) const
{
  std::vector<const StaffMember*> staff;
  for (const auto& [id, member] : members)
  {
    if (member.team_id == team_id) staff.push_back(&member);
  }
  sortByRoleAndRating(staff);
  return staff;
}

std::vector<const StaffMember*> StaffRoster::market() const
{
  std::vector<const StaffMember*> staff = clubStaff(FREE_AGENTS_TEAM_ID);
  std::ranges::stable_sort(
      staff, [](const StaffMember* a, const StaffMember* b)
      { return StaffModel::rating(*a) > StaffModel::rating(*b); });
  return staff;
}

std::size_t StaffRoster::roleCount(TeamID team_id, StaffRole role) const
{
  return static_cast<std::size_t>(std::ranges::count_if(
      members,
      [&](const auto& entry)
      {
        return entry.second.team_id == team_id && entry.second.role == role;
      }));
}

void StaffRoster::rebuildCache() const
{
  // effects() holds cache_mutex through rebuilding and copying the result.
  std::unordered_map<TeamID, std::vector<const StaffMember*>> by_team;
  for (const auto& [id, member] : members)
  {
    if (member.team_id != FREE_AGENTS_TEAM_ID)
      by_team[member.team_id].push_back(&member);
  }
  effects_cache.clear();
  for (const auto& [team_id, staff] : by_team)
    effects_cache.try_emplace(team_id, StaffModel::computeEffects(staff));
  cache_valid = true;
}

StaffEffects StaffRoster::effects(TeamID team_id) const
{
  const std::scoped_lock lock(cache_mutex);
  if (!cache_valid) rebuildCache();
  if (const auto found = effects_cache.find(team_id);
      found != effects_cache.end())
    return found->second;
  static const StaffEffects NO_STAFF = StaffModel::computeEffects({});
  return NO_STAFF;
}

void StaffRoster::restore(std::vector<StaffMember> loaded)
{
  clear();
  for (StaffMember& member : loaded) add(std::move(member));
}

// ---------------------------------------------------------------------------
// StaffModel
// ---------------------------------------------------------------------------

namespace StaffModel
{
std::array<StaffAttribute, 2> keyAttributes(StaffRole role)
{
  using enum StaffRole;
  switch (role)
  {
    case AssistantManager:
      return {Attr::Tactical, Attr::JudgingAbility};
    case AttackingCoach:
      return {Attr::Attacking, Attr::Tactical};
    case DefendingCoach:
      return {Attr::Defending, Attr::Tactical};
    case FitnessCoach:
      return {Attr::Fitness, Attr::SportsScience};
    case GoalkeepingCoach:
      return {Attr::Goalkeeping, Attr::Tactical};
    case YouthCoach:
      return {Attr::YouthDevelopment, Attr::Attacking};
    case Physio:
      return {Attr::Physiotherapy, Attr::SportsScience};
    case SportsScientist:
      return {Attr::SportsScience, Attr::Physiotherapy};
    case Scout:
      return {Attr::JudgingAbility, Attr::JudgingPotential};
    case HeadOfYouth:
    case COUNT:
      break;
  }
  return {Attr::YouthDevelopment, Attr::JudgingPotential};
}

std::uint8_t rating(const StaffMember& member)
{
  const auto keys = keyAttributes(member.role);
  const float value = 0.65f * static_cast<float>(member.attribute(keys[0])) +
                      0.35f * static_cast<float>(member.attribute(keys[1]));
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 1L, 100L));
}

StaffEffects computeEffects(std::span<const StaffMember* const> staff)
{
  using enum StaffRole;
  StaffEffects effects;
  effects.coaching_fitness = area(staff, FitnessCoach, Attr::Fitness);
  effects.coaching_attacking = area(staff, AttackingCoach, Attr::Attacking);
  effects.coaching_defending = area(staff, DefendingCoach, Attr::Defending);
  effects.coaching_goalkeeping =
      area(staff, GoalkeepingCoach, Attr::Goalkeeping);
  effects.coaching_youth =
      std::max(area(staff, YouthCoach, Attr::YouthDevelopment),
               0.9f * scan(staff, HeadOfYouth, Attr::YouthDevelopment).best);
  // Tactics are the assistant's job; specialist coaches help at 90%.
  effects.coaching_tactical =
      std::max({scan(staff, AssistantManager, Attr::Tactical).best,
                0.9f * scan(staff, AttackingCoach, Attr::Tactical).best,
                0.9f * scan(staff, DefendingCoach, Attr::Tactical).best,
                NO_STAFF_QUALITY});
  effects.training_quality =
      (effects.coaching_fitness + effects.coaching_attacking +
       effects.coaching_defending + effects.coaching_tactical) /
      4.0f;

  // Medical: physios treat, sports scientists (and fitness coaches) prevent.
  const AreaScan physios = scan(staff, Physio, Attr::Physiotherapy);
  const float treatment = std::min(
      1.0f, std::max(physios.best, NO_STAFF_QUALITY * 0.8f) +
                0.04f * static_cast<float>(std::max(0, physios.count - 1)));
  const float science =
      std::max({scan(staff, SportsScientist, Attr::SportsScience).best,
                0.7f * scan(staff, FitnessCoach, Attr::SportsScience).best,
                NO_STAFF_QUALITY * 0.8f});
  effects.injury_prevention =
      1.08f - 0.20f * (0.6f * science + 0.4f * effects.coaching_fitness);
  effects.layoff_multiplier =
      1.12f - 0.27f * (0.75f * treatment + 0.25f * science);

  // Academy: the head of youth recruits, youth coaches develop.
  const AreaScan head_youth = scan(staff, HeadOfYouth, Attr::YouthDevelopment);
  const AreaScan head_judging =
      scan(staff, HeadOfYouth, Attr::JudgingPotential);
  const float academy = 0.3f * (head_youth.best + head_judging.best) +
                        0.4f * effects.coaching_youth;
  effects.youth_potential_bonus =
      -2.0f + 6.0f * std::clamp(academy, 0.0f, 1.0f);

  // Scouting: the best scout's eye plus more sources for bigger networks.
  float best_scout = 0.0f;
  int scouts = 0;
  for (const StaffMember* member : staff)
  {
    if (member->role != Scout) continue;
    best_scout = std::max(
        best_scout, 0.5f * (unit(member->attribute(Attr::JudgingAbility)) +
                            unit(member->attribute(Attr::JudgingPotential))));
    ++scouts;
  }
  if (scouts > 0)
  {
    const float depth = std::min(1.0f, static_cast<float>(scouts) / 5.0f);
    effects.scouting_accuracy =
        std::clamp(best_scout * (0.75f + 0.25f * depth), 0.0f, 1.0f);
  }
  else
  {
    const AreaScan assistant =
        scan(staff, StaffRole::AssistantManager, Attr::JudgingPotential);
    effects.scouting_accuracy = 0.4f * assistant.best;
  }
  effects.familiarity_rate = 0.8f + 0.4f * effects.coaching_tactical;

  for (const StaffMember* member : staff)
    effects.weekly_payroll += member->wage;
  return effects;
}

std::uint32_t marketWage(const StaffMember& member)
{
  const double quality = static_cast<double>(rating(member)) / 50.0;
  return roundWage(MARKET_WAGE_BASE *
                   static_cast<double>(ROLE_WAGE_WEIGHT[index(member.role)]) *
                   quality * quality * quality);
}

std::uint32_t renewalWage(const StaffMember& member)
{
  return roundWage(std::max(static_cast<double>(member.wage) * 1.08,
                            static_cast<double>(marketWage(member)) * 0.95));
}

std::int64_t severance(const StaffMember& member)
{
  // Half a year per remaining season, at most a year's wages. [P]
  const std::int64_t weeks = std::min<std::int64_t>(
      52,
      26 * static_cast<std::int64_t>(std::max<int>(1, member.contract_years)));
  return weeks * static_cast<std::int64_t>(member.wage);
}

std::array<std::uint8_t, STAFF_ROLE_COUNT> roleCounts(std::uint8_t reputation)
{
  // Bigger clubs run larger departments (rules-regulations 5.3). [P]
  if (reputation >= 80) return {1, 1, 1, 2, 1, 2, 3, 2, 5, 1};
  if (reputation >= 60) return {1, 1, 1, 1, 1, 1, 2, 1, 3, 1};
  if (reputation >= 40) return {1, 0, 1, 1, 1, 1, 1, 1, 2, 1};
  return {1, 0, 0, 1, 1, 1, 1, 0, 1, 0};
}

std::vector<StaffMember> generateClubStaff(const Team& team,
                                           const LeagueEconomy& economy,
                                           std::uint64_t world_seed,
                                           StaffRoster& roster)
{
  WorldRng rng =
      WorldRng::stream(world_seed, RngDomain::Staff, team.getId(), CLUB_KEY_B);
  const std::uint8_t reputation = team.getReputation();
  const float level = clubLevel(reputation);
  const auto counts = roleCounts(reputation);
  std::vector<StaffMember> staff;
  for (std::size_t role = 0; role < STAFF_ROLE_COUNT; ++role)
  {
    for (std::uint8_t i = 0; i < counts[role]; ++i)
    {
      StaffMember member = generateMember(
          static_cast<StaffRole>(role), level, team.getId(),
          drawNationality(rng, team.getLeagueId()), rng, roster.allocateId());
      member.contract_years = static_cast<std::uint8_t>(rng.uniformInt(1, 4));
      staff.push_back(std::move(member));
    }
  }

  // Wages add up to the football-staff share of non-player wages.
  const double weekly_target =
      ClubEconomy::monthlyStaffCosts(economy, reputation) * 12.0 / 52.0 *
      FOOTBALL_STAFF_SHARE;
  double total_share = 0.0;
  std::vector<double> shares;
  shares.reserve(staff.size());
  for (const StaffMember& member : staff)
  {
    const double quality = static_cast<double>(rating(member)) / 60.0;
    shares.push_back(static_cast<double>(ROLE_WAGE_WEIGHT[index(member.role)]) *
                     quality * quality);
    total_share += shares.back();
  }
  for (std::size_t i = 0; i < staff.size(); ++i)
    staff[i].wage = roundWage(weekly_target * shares[i] / total_share);
  return staff;
}

void generateWorld(GameData& gamedata)
{
  StaffRoster& roster = gamedata.getStaff();
  roster.clear();
  const auto economies = buildLeagueEconomies(gamedata);
  for (const TeamID team_id : sortedClubIds(gamedata))
  {
    const Team& team = gamedata.getTeam(team_id)->get();
    const auto economy = economies.find(team.getLeagueId());
    if (economy == economies.end()) continue;
    for (StaffMember& member : generateClubStaff(
             team, economy->second, gamedata.getWorldSeed(), roster))
      roster.add(std::move(member));
  }
  WorldRng rng =
      WorldRng::stream(gamedata.getWorldSeed(), RngDomain::Staff, MARKET_KEY);
  fillMarket(roster, rng);
}

void refreshMarket(GameData& gamedata, std::int32_t ordinal)
{
  StaffRoster& roster = gamedata.getStaff();
  WorldRng rng =
      WorldRng::stream(gamedata.getWorldSeed(), RngDomain::Staff,
                       static_cast<std::uint64_t>(ordinal), REFRESH_KEY_B);
  // About one candidate in eight finds work elsewhere each month. [P]
  for (const StaffID id : sortedStaffIds(roster))
  {
    const StaffMember* member = roster.find(id);
    if (member->team_id == FREE_AGENTS_TEAM_ID && rng.chance(0.12))
      roster.remove(id);
  }
  fillMarket(roster, rng);
}

std::vector<std::string> seasonEnd(GameData& gamedata, TeamID managed_team_id,
                                   std::uint16_t year)
{
  StaffRoster& roster = gamedata.getStaff();
  WorldRng rng = WorldRng::stream(gamedata.getWorldSeed(), RngDomain::Staff,
                                  year, SEASON_KEY_B);
  std::vector<std::string> departed;
  for (const StaffID id : sortedStaffIds(roster))
  {
    StaffMember& member = *roster.find(id);
    member.age = static_cast<std::uint8_t>(std::min(99, member.age + 1));
    if (member.age >= RETIREMENT_AGE && rng.chance(0.5))
    {
      if (member.team_id == managed_team_id) departed.push_back(member.name());
      roster.remove(id);
      continue;
    }
    if (member.team_id == FREE_AGENTS_TEAM_ID || member.contract_years == 0)
      continue;
    if (--member.contract_years > 0) continue;
    if (member.team_id == managed_team_id)
    {
      departed.push_back(member.name());
    }
    else if (rng.chance(0.8))
    {
      member.contract_years = static_cast<std::uint8_t>(rng.uniformInt(2, 4));
      member.wage = roundWage(static_cast<double>(member.wage) * 1.05);
      continue;
    }
    member.team_id = FREE_AGENTS_TEAM_ID;
    member.wage = marketWage(member);
  }

  // AI clubs refill their departments with the best affordable candidates.
  for (const TeamID team_id : sortedClubIds(gamedata))
  {
    if (team_id == managed_team_id) continue;
    const std::uint8_t reputation =
        gamedata.getTeam(team_id)->get().getReputation();
    const auto counts = roleCounts(reputation);
    const float ceiling = clubLevel(reputation) + 10.0f;
    for (std::size_t role = 0; role < STAFF_ROLE_COUNT; ++role)
    {
      std::size_t have =
          roster.roleCount(team_id, static_cast<StaffRole>(role));
      while (have < counts[role])
      {
        StaffMember* pick = nullptr;
        for (const StaffMember* candidate : roster.market())
        {
          if (index(candidate->role) != role ||
              static_cast<float>(rating(*candidate)) > ceiling)
            continue;
          pick = roster.find(candidate->id);
          break;
        }
        if (!pick) break;
        pick->team_id = team_id;
        pick->wage = marketWage(*pick);
        pick->contract_years = static_cast<std::uint8_t>(rng.uniformInt(2, 3));
        roster.invalidate();
        ++have;
      }
    }
  }
  fillMarket(roster, rng);
  roster.invalidate();
  return departed;
}

float potentialErrorSd(float scouting_accuracy)
{
  const float d_prime = 0.8f + 1.3f * std::clamp(scouting_accuracy, 0.0f, 1.0f);
  return WorldTuning::Youth::POTENTIAL_STDDEV / d_prime;
}

const char* roleKey(StaffRole role)
{
  using enum StaffRole;
  switch (role)
  {
    case AssistantManager:
      return "STAFF_ROLE_ASSISTANT";
    case AttackingCoach:
      return "STAFF_ROLE_ATTACKING_COACH";
    case DefendingCoach:
      return "STAFF_ROLE_DEFENDING_COACH";
    case FitnessCoach:
      return "STAFF_ROLE_FITNESS_COACH";
    case GoalkeepingCoach:
      return "STAFF_ROLE_GK_COACH";
    case YouthCoach:
      return "STAFF_ROLE_YOUTH_COACH";
    case Physio:
      return "STAFF_ROLE_PHYSIO";
    case SportsScientist:
      return "STAFF_ROLE_SPORTS_SCIENTIST";
    case Scout:
      return "STAFF_ROLE_SCOUT";
    case HeadOfYouth:
    case COUNT:
      break;
  }
  return "STAFF_ROLE_HEAD_OF_YOUTH";
}

const char* roleDescriptionKey(StaffRole role)
{
  using enum StaffRole;
  switch (role)
  {
    case AssistantManager:
      return "STAFF_DUTY_ASSISTANT";
    case AttackingCoach:
      return "STAFF_DUTY_ATTACKING_COACH";
    case DefendingCoach:
      return "STAFF_DUTY_DEFENDING_COACH";
    case FitnessCoach:
      return "STAFF_DUTY_FITNESS_COACH";
    case GoalkeepingCoach:
      return "STAFF_DUTY_GK_COACH";
    case YouthCoach:
      return "STAFF_DUTY_YOUTH_COACH";
    case Physio:
      return "STAFF_DUTY_PHYSIO";
    case SportsScientist:
      return "STAFF_DUTY_SPORTS_SCIENTIST";
    case Scout:
      return "STAFF_DUTY_SCOUT";
    case HeadOfYouth:
    case COUNT:
      break;
  }
  return "STAFF_DUTY_HEAD_OF_YOUTH";
}

const char* attributeKey(StaffAttribute attribute)
{
  using enum StaffAttribute;
  switch (attribute)
  {
    case Attacking:
      return "STAFF_ATTR_ATTACKING";
    case Defending:
      return "STAFF_ATTR_DEFENDING";
    case Fitness:
      return "STAFF_ATTR_FITNESS";
    case Goalkeeping:
      return "STAFF_ATTR_GOALKEEPING";
    case Tactical:
      return "STAFF_ATTR_TACTICAL";
    case YouthDevelopment:
      return "STAFF_ATTR_YOUTH";
    case Physiotherapy:
      return "STAFF_ATTR_PHYSIO";
    case SportsScience:
      return "STAFF_ATTR_SPORTS_SCIENCE";
    case JudgingAbility:
      return "STAFF_ATTR_JUDGING_ABILITY";
    case JudgingPotential:
    case COUNT:
      break;
  }
  return "STAFF_ATTR_JUDGING_POTENTIAL";
}
}  // namespace StaffModel
