// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "player.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>

#include "global/global.h"
#include "role_utils.h"
#include "world_tuning.h"

Player::Player(PlayerID new_id, TeamID new_team_id,
               std::string_view new_first_name, std::string_view new_last_name,
               PlayerRole new_role, Language new_nationality, uint32_t new_wage,
               uint32_t new_status, uint8_t new_age, uint8_t new_contract_years,
               uint8_t new_height, Foot new_foot,
               std::map<std::string, float> new_stats)
    : _id(new_id),
      _team_id(new_team_id),
      _wage(new_wage),
      _status(new_status),
      _first_name(new_first_name),
      _last_name(new_last_name),
      _role(new_role),
      _nationality(new_nationality),
      _age(new_age),
      _contract_years(new_contract_years),
      _height(new_height),
      _foot(new_foot),
      _stats(std::move(new_stats))
{
  _transfer_status = (_status & TRANSFER_LISTED_BIT)
                         ? TransferStatus::Listed
                         : TransferStatus::NotListed;
}

uint32_t Player::getId() const { return _id; }

TeamID Player::getTeamId() const { return _team_id; }

void Player::setTeamId(TeamID id) { _team_id = id; }

std::string Player::getName() const { return _first_name + " " + _last_name; }

const std::string& Player::getFirstName() const { return _first_name; }

const std::string& Player::getLastName() const { return _last_name; }

int Player::getAge() const { return _age; }

void Player::setAge(uint8_t new_age)
{
  _age = new_age;
  _cached_market_value.store(0);
}

PlayerRole Player::getRole() const { return _role; }

Language Player::getNationality() const { return _nationality; }

uint32_t Player::getWage() const { return _wage; }

void Player::setWage(uint32_t wage) { _wage = wage; }

uint8_t Player::getContractYears() const { return _contract_years; }

void Player::setContractYears(uint8_t years)
{
  _contract_years = years;
  _cached_market_value.store(0);
}

bool Player::advanceContractYear()
{
  if (_contract_years == 0) return false;
  --_contract_years;
  _cached_market_value.store(0);
  return _contract_years == 0;
}

uint8_t Player::getHeight() const { return _height; }

Foot Player::getFoot() const { return _foot; }

uint32_t Player::getStatus() const { return _status; }

const std::map<std::string, float>& Player::getStats() const { return _stats; }

void Player::setStats(const std::map<std::string, float>& new_stats)
{
  _stats = new_stats;
  _cached_market_value.store(0);
}

double Player::getOverall(const StatsConfig& stats_config) const
{
  double overall = 0.0;
  const auto role_config_it =
      stats_config.role_focus.find(RoleUtils::getBroadCategory(_role));
  if (role_config_it == stats_config.role_focus.end())
  {
    return 0.0;
  }
  const auto& role_config = role_config_it->second;
  const auto& weights = role_config.weights;
  const auto& stat_names = role_config.stats;

  for (size_t i = 0; i < std::min(stat_names.size(), weights.size()); ++i)
  {
    const std::string& stat_name = stat_names[i];
    auto it = _stats.find(stat_name);
    if (it != _stats.end())
    {
      overall += static_cast<double>(it->second) * weights[i];
    }
  }
  return overall;
}

namespace
{
enum class StatGroup
{
  Physical,
  Endurance,
  Technical,
  Craft,
  Goalkeeping
};

StatGroup statGroup(std::string_view stat)
{
  using enum StatGroup;
  if (stat == "Pace" || stat == "Physicality") return Physical;
  if (stat == "Stamina") return Endurance;
  if (stat == "Passing" || stat == "Vision") return Craft;
  if (stat == "Goalkeeping") return Goalkeeping;
  return Technical;
}

/** Fractional change of an attribute over the year to @p age (negative =
 * decline): physical first, technical later, passing and vision last. */
float yearlyChange(StatGroup group, int age, PlayerRole role)
{
  using Tuning = WorldTuning::Development;
  using enum StatGroup;
  const bool late_peak = (role == PlayerRole::GK || role == PlayerRole::CB) &&
                         age < Tuning::LATE_PEAK_ROLE_UNTIL;
  const float role_factor = late_peak ? Tuning::LATE_PEAK_ROLE_FACTOR : 1.0f;
  switch (group)
  {
    case Physical:
      return -Tuning::physicalDecline(age) * role_factor;
    case Endurance:
      return -Tuning::physicalDecline(age) * Tuning::ENDURANCE_FACTOR *
             role_factor;
    case Technical:
      return -Tuning::technicalDecline(age) * role_factor;
    case Craft:
      return Tuning::craftChange(age);
    case Goalkeeping:
      return -Tuning::goalkeepingDecline(age);
  }
  return 0.0f;
}
}  // namespace

void Player::agePlayer()
{
  ++_age;
  _cached_market_value.store(0);

  // Professional players look after themselves: 0.8x-1.2x decline. [P]
  const float care =
      1.2f - 0.4f * static_cast<float>(_traits.professionalism) / 100.0f;
  for (auto& [stat_name, value] : _stats)
  {
    float change = yearlyChange(statGroup(stat_name), _age, _role);
    if (change < 0.0f) change *= care;
    value =
        std::clamp(value * (1.0f + change), static_cast<float>(MIN_STAT_VAL),
                   static_cast<float>(MAX_STAT_VAL));
  }
}

void Player::train(const std::vector<std::string>& focus_stats, float amount)
{
  for (const std::string& stat_name : focus_stats)
  {
    const auto it = _stats.find(stat_name);
    if (it == _stats.end()) continue;
    it->second =
        std::clamp(it->second + amount, static_cast<float>(MIN_STAT_VAL),
                   static_cast<float>(MAX_STAT_VAL));
  }
  _cached_market_value.store(0);
}

float Player::getPotential() const { return _potential; }

void Player::setPotential(float potential)
{
  _potential = std::clamp(potential, static_cast<float>(MIN_STAT_VAL),
                          static_cast<float>(MAX_STAT_VAL));
  _cached_market_value.store(0);
}

const PlayerTraits& Player::getTraits() const { return _traits; }

void Player::setTraits(const PlayerTraits& traits) { _traits = traits; }

const PlayerDynamics& Player::getDynamics() const { return _dynamics; }

PlayerDynamics& Player::mutableDynamics() { return _dynamics; }

bool Player::isAvailable() const { return _dynamics.injury_days == 0; }

float Player::getForm() const
{
  if (_dynamics.rating_count == 0) return 0.0f;
  float total = 0.0f;
  for (std::size_t i = 0; i < _dynamics.rating_count; ++i)
    total += _dynamics.recent_ratings[i];
  return total / static_cast<float>(_dynamics.rating_count);
}

void Player::pushMatchRating(float rating)
{
  auto& ratings = _dynamics.recent_ratings;
  std::shift_right(ratings.begin(), ratings.end(), 1);
  ratings[0] = std::clamp(rating, 1.0f, 10.0f);
  if (_dynamics.rating_count < ratings.size()) ++_dynamics.rating_count;
}

// ---------------- Market Logic ----------------

uint32_t Player::getMarketValue() const { return _cached_market_value.load(); }

double Player::valueFor(double overall, double potential, int age,
                        double contract_years)
{
  // ln(value) = ability term + h(age) + potential premium + c(contract).
  // Anchors [P]: overall 65 at 25 with a long contract ~ EUR 2M, each overall
  // point ~ +21%; h(age) = -0.012 (age - 25)^2 (inverted U, age^2 sign from
  // Mueller et al. 2017). Remaining contract length is the dominant fee
  // driver (CIES), but even an expiring deal keeps some value:
  // c(y) = ln(1 - 0.55 exp(-y / 1.5)) (x0.72 at one year, x0.86 at two).
  // The premium for unrealised potential fades out between 22 and 25 so a
  // birthday never costs a young player a big step in value.
  const double age_offset = static_cast<double>(age) - 25.0;
  double log_value = std::log(2'000'000.0) + 0.19 * (overall - 65.0) -
                     0.012 * age_offset * age_offset;
  const double potential_weight =
      std::clamp((25.0 - static_cast<double>(age)) / 3.0, 0.0, 1.0);
  if (potential > overall)
    log_value += 0.05 * potential_weight * (potential - overall);
  const double years = std::max(contract_years, 0.5);
  log_value += std::log(1.0 - 0.55 * std::exp(-years / 1.5));

  constexpr double MAX_VALUE = 250'000'000.0;
  constexpr double MIN_VALUE = 10'000.0;
  return std::clamp(std::exp(log_value), MIN_VALUE, MAX_VALUE);
}

void Player::updateMarketValue(const StatsConfig& stats_config) const
{
  _cached_market_value.store(static_cast<uint32_t>(
      valueFor(getOverall(stats_config), static_cast<double>(_potential), _age,
               static_cast<double>(_contract_years))));
}

void Player::setTransferStatus(TransferStatus status)
{
  _transfer_status = status;
  if (status == TransferStatus::Listed)
  {
    _status |= TRANSFER_LISTED_BIT;
  }
  else
  {
    _status &= ~TRANSFER_LISTED_BIT;
  }
}

TransferStatus Player::getTransferStatus() const { return _transfer_status; }

void Player::setAcademyPlayer(bool academy)
{
  if (academy)
    _status |= ACADEMY_BIT;
  else
    _status &= ~ACADEMY_BIT;
}

bool Player::isAcademyPlayer() const { return (_status & ACADEMY_BIT) != 0; }
