// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "team.h"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "finances.h"
#include "gamedata.h"

namespace
{
constexpr std::size_t RECENT_FORM_LENGTH = 5;
/** Oldest (season) age at which automatic selection treats a player as an
 * unproven youngster. */
constexpr int YOUNG_MAX_AGE = 20;
/** Youngest (season) age of an experienced player. */
constexpr int EXPERIENCED_MIN_AGE = 23;
/** A youngster starts ahead of an experienced player of his position group
 * only when he is this much better: at similar ability managers trust
 * experience, and U21 players get 2-10% of league minutes. [RR2 3.3] */
constexpr double YOUTH_START_MARGIN = 2.0;

int positionGroup(PlayerRole role)
{
  using enum PlayerRole;
  switch (role)
  {
    case GK:
      return 0;
    case LB:
    case CB:
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
}  // namespace

// Constructor
Team::Team(TeamID team_id, uint8_t team_league_id, std::string_view team_name,
           int64_t initial_balance,
           const std::vector<PlayerID>& initial_player_ids,
           const Strategy& strategy, const Lineup& lineup_data)
    : id(team_id),
      league_id(team_league_id),
      name(team_name),
      player_ids(initial_player_ids),
      team_strategy(strategy),
      lineup(lineup_data),
      finances(initial_balance)
{
}

// Accessors
uint16_t Team::getId() const { return id; }
uint8_t Team::getLeagueId() const { return league_id; }
void Team::setLeagueId(LeagueID new_league_id) { league_id = new_league_id; }
const std::string& Team::getName() const { return name; }

const std::vector<PlayerID>& Team::getPlayerIDs() const { return player_ids; }

const std::vector<PlayerID>& Team::getAcademyIDs() const { return academy_ids; }

void Team::addPlayerID(PlayerID player_id)
{
  std::erase(academy_ids, player_id);
  if (!std::ranges::contains(player_ids, player_id))
  {
    player_ids.push_back(player_id);
  }
}

void Team::addAcademyID(PlayerID player_id)
{
  std::erase(player_ids, player_id);
  if (!std::ranges::contains(academy_ids, player_id))
    academy_ids.push_back(player_id);
}

void Team::setAcademyMember(PlayerID player_id, bool academy)
{
  auto& from = academy ? player_ids : academy_ids;
  if (std::erase(from, player_id) > 0)
    (academy ? academy_ids : player_ids).push_back(player_id);
}

bool Team::removePlayerID(PlayerID player_id)
{
  const bool senior = std::erase(player_ids, player_id) > 0;
  const bool academy = std::erase(academy_ids, player_id) > 0;
  return senior || academy;
}

// Lineup access
Lineup& Team::getLineup() { return lineup; }
const Lineup& Team::getLineup() const { return lineup; }

// Strategy access
Strategy& Team::getStrategy() { return team_strategy; }
const Strategy& Team::getStrategy() const { return team_strategy; }
void Team::setStrategy(const Strategy& strategy) { team_strategy = strategy; }

// Generate best starting XI automatically
void Team::generateStartingXI(const class GameData& gamedata,
                              const StatsConfig& stats_config)
{
  // Academy players only fill in when the senior squad runs short.
  constexpr std::size_t MIN_SENIORS = 14;
  std::vector<PlayerID> available;
  available.reserve(player_ids.size() + academy_ids.size());
  const auto& players = gamedata.getPlayers();
  const auto addAvailable = [&](const std::vector<PlayerID>& ids)
  {
    for (const PlayerID player_id : ids)
    {
      const auto found = players.find(player_id);
      if (found != players.end() && found->second.isAvailable())
        available.push_back(player_id);
    }
  };
  addAvailable(player_ids);
  if (available.size() < MIN_SENIORS) addAvailable(academy_ids);
  lineup.generateStartingXI(gamedata, available, stats_config);

  // Youngsters who are not clearly better than an experienced player of
  // their position group left out of the XI start on the bench instead.
  std::vector<PlayerID> benched;
  for (const Player* starter : lineup.starters())
  {
    if (starter->getAge() > YOUNG_MAX_AGE) continue;
    const double threshold =
        starter->getOverall(stats_config) - YOUTH_START_MARGIN;
    const int group = positionGroup(starter->getRole());
    const bool experienced_option = std::ranges::any_of(
        available,
        [this, &players, &stats_config, threshold, group](PlayerID player_id)
        {
          const Player& other = players.at(player_id);
          return other.getAge() >= EXPERIENCED_MIN_AGE &&
                 positionGroup(other.getRole()) == group &&
                 other.getOverall(stats_config) >= threshold &&
                 !lineup.isStarter(player_id);
        });
    if (experienced_option) benched.push_back(starter->getId());
  }
  if (benched.empty()) return;
  std::erase_if(available, [&](PlayerID player_id)
                { return std::ranges::contains(benched, player_id); });
  lineup.generateStartingXI(gamedata, available, stats_config);
  std::vector<const Player*> reserves = lineup.getReserves();
  for (const PlayerID player_id : benched)
    reserves.push_back(&players.at(player_id));
  lineup.setReserves(reserves);
}

// Finances access
Finances& Team::getFinances() noexcept { return finances; }
const Finances& Team::getFinances() const noexcept { return finances; }

// Club profile
const ClubProfile& Team::getProfile() const noexcept { return profile; }
void Team::setProfile(const ClubProfile& new_profile) { profile = new_profile; }
std::uint8_t Team::getReputation() const noexcept { return profile.reputation; }
std::uint32_t Team::getStadiumCapacity() const noexcept
{
  return profile.stadium_capacity;
}

const std::string& Team::getRecentForm() const noexcept { return recent_form; }

void Team::setRecentForm(std::string_view form)
{
  recent_form = form.substr(0, RECENT_FORM_LENGTH);
}

void Team::pushResult(MatchOutcome outcome)
{
  recent_form.insert(recent_form.begin(), std::to_underlying(outcome));
  if (recent_form.size() > RECENT_FORM_LENGTH)
    recent_form.resize(RECENT_FORM_LENGTH);
}
