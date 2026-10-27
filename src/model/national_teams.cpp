// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/national_teams.h"

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <random>
#include <utility>

#include "database/database_connection.h"
#include "database/gamedata.h"
#include "global/global.h"
#include "global/language_manager.h"
#include "model/calendar.h"
#include "model/competition.h"
#include "model/lineup.h"
#include "model/match_scheduler.h"
#include "model/player.h"
#include "model/team.h"
#include "model/world_generation.h"
#include "model/world_rng.h"
#include "model/world_simulation.h"
#include "model/world_tuning.h"

using International::Competition;
using International::Confederation;
using International::Fixture;
using International::Group;
using International::Stage;

namespace
{
constexpr size_t MIN_GOALKEEPERS = 2;
constexpr size_t RATING_SAMPLE = 23;
constexpr double RATING_BASE = 1000.0;
constexpr double RATING_PER_OVERALL = 12.0;
constexpr double ELO_SCALE = 400.0;
constexpr double HOME_ADVANTAGE = 100.0;
constexpr int ANNOUNCE_DAYS = 7;
constexpr int FINALS_DRAW_DAYS = 21;
constexpr size_t MATCH_RESERVES = 9;
constexpr size_t MIN_PLAYERS_TO_PLAY = 7;
using International::MIN_SQUAD_AGE;
constexpr uint32_t GROUP_DRAW_SALT = 0x6E'61'74;
constexpr uint32_t FRIENDLY_SALT = 0x66'72'69;
constexpr uint32_t MATCH_SEED_SALT = 0x6D'61'74;
constexpr uint32_t COACH_SALT = 0x63'6F'61;
// Finals schedule, days after the Monday players report: three group
// matchdays, then the knockout rounds.
constexpr std::array<int, 3> FINALS_GROUP_DAYS = {3, 7, 11};
constexpr std::array<int, 3> FINALS_KNOCKOUT_DAYS = {16, 20, 25};
// Travel after a window. [P] Round trips and time zones between regions.
constexpr uint16_t SAME_REGION_KM = 2'000;
constexpr int TRAVEL_RISK_DAYS = 7;
constexpr double RISK_PER_TIME_ZONE = 0.015;
constexpr double RISK_PER_KM = 1.0 / 200'000.0;
constexpr float CONDITION_LOSS_BASE = 2.0f;
constexpr float CONDITION_LOSS_PER_KM = 1.0f / 2'000.0f;
constexpr float MIN_RETURN_CONDITION = 40.0f;
constexpr float WINNER_MORALE = 10.0f;
constexpr float RUNNER_UP_MORALE = 5.0f;

enum class RoleGroup : uint8_t
{
  Goalkeeper,
  Defender,
  Midfielder,
  Forward
};

RoleGroup roleGroup(PlayerRole role)
{
  using enum RoleGroup;
  using enum PlayerRole;
  switch (role)
  {
    case GK:
      return Goalkeeper;
    case CB:
    case LB:
    case RB:
      return Defender;
    case CDM:
    case CM:
    case CAM:
    case LM:
    case RM:
      return Midfielder;
    default:
      return Forward;
  }
}

GameDateValue plusDays(const GameDateValue& date, int days)
{
  return SeasonCalendar::addDays(date, days);
}

/** International matches never fall on the season changeover day. */
GameDateValue avoidChangeover(GameDateValue date)
{
  if (date.month == SeasonCalendar::SEASON_START_MONTH && date.day == 1)
    return plusDays(date, 1);
  return date;
}

bool between(const GameDateValue& date, const GameDateValue& first,
             const GameDateValue& last)
{
  return !(date < first) && !(last < date);
}

Confederation clubConfederation(const GameData& gamedata, TeamID team_id,
                                Confederation fallback)
{
  const auto team = gamedata.getTeam(team_id);
  if (!team || team_id == FREE_AGENTS_TEAM_ID) return fallback;
  switch (leagueProfile(team->get().getLeagueId()).region)
  {
    case WorldRegion::NorthAmerica:
    case WorldRegion::SouthAmerica:
      return Confederation::Americas;
    default:
      return Confederation::Europe;
  }
}

std::pair<uint16_t, uint8_t> tripBetween(Confederation from, Confederation to)
{
  if (from == to) return {SAME_REGION_KM, 1};
  const auto pair = [&](Confederation a, Confederation b)
  { return (from == a && to == b) || (from == b && to == a); };
  if (pair(Confederation::Europe, Confederation::Americas)) return {16'000, 6};
  if (pair(Confederation::Europe, Confederation::Asia)) return {14'000, 7};
  return {24'000, 12};
}

std::string nationArg(Language nation)
{
  return "@" + International::teamNameKey(nation);
}

template <typename AddFixture>
void pairFriendlyNations(std::vector<Language>& nations,
                         const GameDateValue& day, std::mt19937& rng,
                         AddFixture&& add_fixture,
                         std::vector<Language>* leftovers)
{
  std::ranges::shuffle(nations, rng);
  size_t index = 0;
  for (; index + 1 < nations.size(); index += 2)
  {
    Fixture fixture;
    fixture.date = day;
    fixture.home = nations[index];
    fixture.away = nations[index + 1];
    fixture.competition = Competition::Friendly;
    add_fixture(fixture);
  }
  if (index < nations.size() && leftovers)
  {
    leftovers->push_back(nations[index]);
  }
}

void recordAppearance(
    const PlayerMatchLine& line, bool finals_match, const GameDateValue& today,
    GameData& gamedata,
    std::unordered_map<PlayerID, International::Record>& records)
{
  if (line.minutes == 0)
  {
    return;
  }
  const auto player = gamedata.getPlayer(line.player_id);
  if (!player)
  {
    return;
  }
  International::Record& record = records[line.player_id];
  record.nation = player->get().getNationality();
  record.name = player->get().getName();
  ++record.caps;
  record.goals = static_cast<uint16_t>(record.goals + line.goals);
  if (finals_match)
  {
    ++record.finals_caps;
  }
  record.last_cap = today;
  record.last_cap_age = static_cast<uint8_t>(player->get().getAge());
}

template <typename LineupFor>
void addNationalMatchInputs(const std::vector<size_t>& fixture_indices,
                            std::vector<Fixture>& fixtures,
                            const GameDateValue& today,
                            const LineupFor& lineup_for,
                            std::vector<size_t>& simulated,
                            std::vector<MatchSimulationInput>& inputs)
{
  for (const size_t index : fixture_indices)
  {
    Fixture& fixture = fixtures[index];
    MatchSimulationInput input;
    input.home_id = static_cast<TeamID>(NationalTeams::TEAM_ID_BASE +
                                        std::to_underlying(fixture.home));
    input.away_id = static_cast<TeamID>(NationalTeams::TEAM_ID_BASE +
                                        std::to_underlying(fixture.away));
    input.seed = Competitions::mixSeed(static_cast<uint32_t>(dayOrdinal(today)),
                                       fixture.id, MATCH_SEED_SALT);
    input.home_lineup = lineup_for(fixture.home);
    input.away_lineup = lineup_for(fixture.away);
    if (input.home_lineup.starters().size() < MIN_PLAYERS_TO_PLAY ||
        input.away_lineup.starters().size() < MIN_PLAYERS_TO_PLAY)
    {
      fixture.played = true;
      continue;
    }
    input.knockout.required = fixture.stage != Stage::Group;
    simulated.push_back(index);
    inputs.push_back(std::move(input));
  }
}

Lineup nationalLineup(const std::vector<PlayerID>& squad_players,
                      GameData& gamedata, const StatsConfig& config)
{
  Lineup lineup;
  std::vector<PlayerID> available;
  for (const PlayerID player_id : squad_players)
  {
    if (const auto player = gamedata.getPlayer(player_id);
        player && player->get().isAvailable())
    {
      available.push_back(player_id);
    }
  }
  lineup.generateStartingXI(gamedata, available, config);

  std::vector<const Player*> bench;
  const Player* keeper = nullptr;
  std::vector<const Player*> outfield;
  for (const Player* reserve : lineup.getReserves())
  {
    if (reserve->getRole() == PlayerRole::GK)
    {
      if (keeper == nullptr)
      {
        keeper = reserve;
      }
    }
    else
    {
      outfield.push_back(reserve);
    }
  }
  std::ranges::stable_sort(
      outfield, [&config](const Player* a, const Player* b)
      { return a->getOverall(config) > b->getOverall(config); });
  if (keeper != nullptr)
  {
    bench.push_back(keeper);
  }
  for (const Player* player : outfield)
  {
    if (bench.size() < MATCH_RESERVES)
    {
      bench.push_back(player);
    }
  }
  lineup.setReserves(bench);
  return lineup;
}

void extendFinalsSquadUntil(NationalTeams::Squad& squad,
                            const NationalTeams::Finals& tournament,
                            const std::vector<Fixture>& fixtures)
{
  for (const Fixture& fixture : fixtures)
  {
    if (fixture.competition == tournament.competition &&
        !(fixture.date < squad.start) && squad.until < fixture.date)
    {
      squad.until = fixture.date;
    }
  }
  squad.until = std::max(
      squad.until, plusDays(squad.start, FINALS_KNOCKOUT_DAYS.back()),
      [](const GameDateValue& a, const GameDateValue& b) { return a < b; });
}

template <typename PickSquad>
bool replaceUnavailablePlayers(NationalTeams::Squad& squad,
                               const GameDateValue& today, GameData& gamedata,
                               const PickSquad& pick_squad)
{
  std::vector<PlayerID> removed;
  std::erase_if(squad.players,
                [&gamedata, &removed](PlayerID player_id)
                {
                  const auto player = gamedata.getPlayer(player_id);
                  const bool gone = !player || !player->get().isAvailable();
                  if (gone && player)
                  {
                    removed.push_back(player_id);
                  }
                  return gone;
                });
  const bool changed = !removed.empty();
  for (const PlayerID player_id : removed)
  {
    const PlayerRole role = gamedata.getPlayer(player_id)->get().getRole();
    std::vector<PlayerID> exclude = squad.players;
    exclude.insert(exclude.end(), removed.begin(), removed.end());
    for (const PlayerID candidate :
         pick_squad(squad.nation, International::FINALS_SQUAD, today, exclude))
    {
      if (roleGroup(gamedata.getPlayer(candidate)->get().getRole()) ==
          roleGroup(role))
      {
        squad.players.push_back(candidate);
        break;
      }
    }
  }
  return changed;
}

void boostFinalists(NationalTeams::Finals const& entry,
                    const std::vector<NationalTeams::Squad>& squads,
                    GameData& gamedata)
{
  for (const NationalTeams::Squad& squad : squads)
  {
    if (squad.nation != *entry.winner && squad.nation != *entry.runner_up)
    {
      continue;
    }
    const float boost =
        squad.nation == *entry.winner ? WINNER_MORALE : RUNNER_UP_MORALE;
    for (const PlayerID player_id : squad.players)
    {
      if (const auto player = gamedata.getPlayers().find(player_id);
          player != gamedata.getPlayers().end())
      {
        PlayerDynamics& dynamics = player->second.mutableDynamics();
        dynamics.morale = std::min(100.0f, dynamics.morale + boost);
      }
    }
  }
}

template <typename Ranking, typename HasFixture, typename FinalsFor,
          typename AddFixture>
void scheduleWindowFriendlies(const std::vector<GameDateValue>& match_days,
                              Ranking&& ranking, HasFixture&& has_fixture,
                              FinalsFor&& finals_for, AddFixture&& add_fixture)
{
  for (const GameDateValue& day : match_days)
  {
    std::map<Confederation, std::vector<Language>> idle;
    for (const Language nation : ranking())
    {
      if (has_fixture(nation, day) || finals_for(nation, day))
      {
        continue;
      }
      idle[International::confederationOf(nation)].push_back(nation);
    }
    std::mt19937 rng(Competitions::mixSeed(
        static_cast<uint32_t>(dayOrdinal(day)), FRIENDLY_SALT, 0));
    std::vector<Language> leftovers;
    for (auto& [confederation, nations] : idle)
    {
      pairFriendlyNations(nations, day, rng, add_fixture, &leftovers);
    }
    pairFriendlyNations(leftovers, day, rng, add_fixture, nullptr);
  }
}

void collectManagedCallups(const NationalTeams::Squad& squad,
                           TeamID managed_team_id, GameData& gamedata,
                           std::vector<std::string>& called)
{
  for (const PlayerID player_id : squad.players)
  {
    const auto player = gamedata.getPlayer(player_id);
    if (player && player->get().getTeamId() == managed_team_id &&
        managed_team_id != FREE_AGENTS_TEAM_ID)
    {
      called.push_back(player->get().getName() + " (@" +
                       International::teamNameKey(squad.nation) + ")");
    }
  }
}

void processReleasedSquad(
    const NationalTeams::Squad& squad, const GameDateValue& today,
    TeamID managed_team_id, GameData& gamedata,
    std::unordered_map<PlayerID, NationalTeams::Travel>& travel,
    std::map<TeamID, int64_t>& compensation,
    std::vector<std::string>& returning)
{
  const Confederation home = International::confederationOf(squad.nation);
  for (const PlayerID player_id : squad.players)
  {
    const auto player_ref = gamedata.getPlayers().find(player_id);
    if (player_ref == gamedata.getPlayers().end())
    {
      continue;
    }
    Player& player = player_ref->second;
    const TeamID club = player.getTeamId();
    const auto [km, zones] =
        tripBetween(clubConfederation(gamedata, club, home), home);
    travel[player_id] = {.returned = today, .km = km, .time_zones = zones};
    PlayerDynamics& dynamics = player.mutableDynamics();
    dynamics.condition =
        std::max(std::min(dynamics.condition, 100.0f) -
                     (CONDITION_LOSS_BASE + (CONDITION_LOSS_PER_KM * km)),
                 MIN_RETURN_CONDITION);
    if (squad.finals && club != FREE_AGENTS_TEAM_ID)
    {
      compensation[club] +=
          NationalTeams::COMPENSATION_PER_DAY *
          (dayOrdinal(squad.until) - dayOrdinal(squad.start) + 2);
    }
    if (club == managed_team_id && managed_team_id != FREE_AGENTS_TEAM_ID)
    {
      returning.push_back(player.getName());
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// International helpers
// ---------------------------------------------------------------------------

const char* International::confederationKey(Confederation confederation)
{
  using enum Confederation;
  switch (confederation)
  {
    case Europe:
      return "INTL_CONF_EUROPE";
    case Americas:
      return "INTL_CONF_AMERICAS";
    case Asia:
      return "INTL_CONF_ASIA";
  }
  return "INTL_CONF_EUROPE";
}

Confederation International::confederationOf(Language nation)
{
  using enum Language;
  using enum Confederation;
  switch (nation)
  {
    case MX:
    case BR:
    case US:
      return Americas;
    case CN:
    case JP:
    case KR:
    case AR:
      return Asia;
    default:
      return Europe;
  }
}

std::string International::teamNameKey(Language nation)
{
  const auto name = languageToString.find(nation);
  return "NT_" + (name == languageToString.end() ? std::string("English")
                                                 : name->second);
}

const char* International::competitionKey(Competition competition)
{
  using enum Competition;
  switch (competition)
  {
    case Friendly:
      return "INTL_FRIENDLY";
    case WorldQualifier:
      return "INTL_WORLD_QUALIFIER";
    case ContinentalQualifier:
      return "INTL_CONTINENTAL_QUALIFIER";
    case NationsLeague:
      return "INTL_NATIONS_LEAGUE";
    case NationsLeagueFinals:
      return "INTL_NATIONS_LEAGUE_FINALS";
    case WorldFinals:
      return "INTL_WORLD_FINALS";
    case ContinentalFinals:
      return "INTL_CONTINENTAL_FINALS";
  }
  return "INTL_FRIENDLY";
}

const char* International::callUpResultKey(CallUpResult result)
{
  using enum CallUpResult;
  switch (result)
  {
    case Ok:
      return "CALLUP_RESULT_OK";
    case NoSquad:
      return "CALLUP_RESULT_NO_SQUAD";
    case Locked:
      return "CALLUP_RESULT_LOCKED";
    case TooMany:
      return "CALLUP_RESULT_TOO_MANY";
    case TooFew:
      return "CALLUP_RESULT_TOO_FEW";
    case NeedGoalkeepers:
      return "CALLUP_RESULT_GOALKEEPERS";
    case Ineligible:
      return "CALLUP_RESULT_INELIGIBLE";
    case Duplicate:
      break;
  }
  return "CALLUP_RESULT_DUPLICATE";
}

bool International::isFinals(Competition competition)
{
  using enum Competition;
  return competition == WorldFinals || competition == ContinentalFinals ||
         competition == NationsLeagueFinals;
}

const char* International::stageKey(Stage stage)
{
  using enum Stage;
  switch (stage)
  {
    case Group:
      return "INTL_STAGE_GROUP";
    case QuarterFinal:
      return "CONT_ROUND_QUARTER_FINAL";
    case SemiFinal:
      return "CONT_ROUND_SEMI_FINAL";
    case Final:
      return "CONT_ROUND_FINAL";
  }
  return "INTL_STAGE_GROUP";
}

std::optional<Language> International::winnerOf(const Fixture& fixture)
{
  if (!fixture.played) return std::nullopt;
  if (fixture.home_goals != fixture.away_goals)
    return fixture.home_goals > fixture.away_goals ? fixture.home
                                                   : fixture.away;
  if (fixture.penalties && fixture.home_penalties != fixture.away_penalties)
    return fixture.home_penalties > fixture.away_penalties ? fixture.home
                                                           : fixture.away;
  return std::nullopt;
}

std::vector<International::GroupRow> International::groupTable(
    const Group& group, const std::vector<Fixture>& all,
    const std::map<Language, double>& rating)
{
  std::map<Language, GroupRow> rows;
  for (const Language nation : group.members) rows[nation].nation = nation;
  for (const Fixture& fixture : all)
  {
    if (!fixture.played || fixture.competition != group.competition ||
        fixture.group != group.index || fixture.stage != Stage::Group)
      continue;
    const auto home = rows.find(fixture.home);
    const auto away = rows.find(fixture.away);
    if (home == rows.end() || away == rows.end()) continue;
    const auto add = [](GroupRow& row, uint8_t scored, uint8_t conceded)
    {
      ++row.played;
      row.goals_for = static_cast<uint16_t>(row.goals_for + scored);
      row.goals_against = static_cast<uint16_t>(row.goals_against + conceded);
      if (scored > conceded)
      {
        ++row.won;
        row.points = static_cast<uint16_t>(row.points + 3);
      }
      else if (scored == conceded)
      {
        ++row.drawn;
        row.points = static_cast<uint16_t>(row.points + 1);
      }
      else
      {
        ++row.lost;
      }
    };
    add(home->second, fixture.home_goals, fixture.away_goals);
    add(away->second, fixture.away_goals, fixture.home_goals);
  }
  std::vector<GroupRow> table;
  for (const auto& [nation, row] : rows) table.push_back(row);
  const auto ratingOf = [&rating](Language nation)
  {
    const auto it = rating.find(nation);
    return it == rating.end() ? 0.0 : it->second;
  };
  std::ranges::sort(table,
                    [&](const GroupRow& a, const GroupRow& b)
                    {
                      if (a.points != b.points) return a.points > b.points;
                      if (a.goalDifference() != b.goalDifference())
                        return a.goalDifference() > b.goalDifference();
                      if (a.goals_for != b.goals_for)
                        return a.goals_for > b.goals_for;
                      if (ratingOf(a.nation) != ratingOf(b.nation))
                        return ratingOf(a.nation) > ratingOf(b.nation);
                      return a.nation < b.nation;
                    });
  return table;
}

bool International::canSwitchNation(const Record& record,
                                    const GameDateValue& today)
{
  constexpr uint16_t MAX_CAPS = 3;
  constexpr uint8_t MAX_AGE = 21;
  constexpr int WAIT_DAYS = 3 * 365;
  if (record.caps == 0) return true;
  return record.caps <= MAX_CAPS && record.last_cap_age < MAX_AGE &&
         record.finals_caps == 0 &&
         dayOrdinal(today) - dayOrdinal(record.last_cap) >= WAIT_DAYS;
}

std::vector<std::vector<std::pair<size_t, size_t>>> International::roundRobin(
    size_t teams, bool double_round)
{
  std::vector<std::vector<std::pair<size_t, size_t>>> rounds;
  if (teams < 2) return rounds;
  std::vector<size_t> order(teams);
  for (size_t i = 0; i < teams; ++i) order[i] = i;
  const size_t bye = teams;
  if (teams % 2 != 0) order.push_back(bye);
  const size_t count = order.size();
  for (size_t round = 0; round + 1 < count; ++round)
  {
    std::vector<std::pair<size_t, size_t>> pairs;
    for (size_t i = 0; i < count / 2; ++i)
    {
      size_t home = order[i];
      size_t away = order[count - 1 - i];
      if (home == bye || away == bye) continue;
      if ((round + i) % 2 == 1) std::swap(home, away);
      pairs.emplace_back(home, away);
    }
    rounds.push_back(std::move(pairs));
    std::rotate(order.begin() + 1, order.end() - 1, order.end());
  }
  if (double_round)
  {
    const size_t first_half = rounds.size();
    for (size_t round = 0; round < first_half; ++round)
    {
      std::vector<std::pair<size_t, size_t>> pairs;
      for (const auto& [home, away] : rounds[round])
        pairs.emplace_back(away, home);
      rounds.push_back(std::move(pairs));
    }
  }
  return rounds;
}

size_t International::finalsSize(size_t pool)
{
  if (pool >= 24) return 16;
  if (pool >= 12) return 8;
  if (pool >= 6) return 4;
  return 0;
}

double International::selectionScore(const Player& player, uint16_t caps,
                                     const StatsConfig& config)
{
  constexpr double FORM_PIVOT = 6.5;
  constexpr double FORM_WEIGHT = 2.0;
  constexpr double CONDITION_PIVOT = 85.0;
  constexpr double CONDITION_WEIGHT = 0.1;
  constexpr double CAPS_CAP = 50.0;
  constexpr double CAPS_WEIGHT = 1.0 / 25.0;
  double score = player.getOverall(config);
  if (const double form = player.getForm(); form > 0.0)
    score += (form - FORM_PIVOT) * FORM_WEIGHT;
  score +=
      (static_cast<double>(player.getDynamics().condition) - CONDITION_PIVOT) *
      CONDITION_WEIGHT;
  score += std::min<double>(caps, CAPS_CAP) * CAPS_WEIGHT;
  return score;
}

std::vector<PlayerID> International::selectSquad(
    const std::vector<const Player*>& eligible, size_t size,
    const StatsConfig& config,
    const std::unordered_map<PlayerID, uint16_t>& caps)
{
  constexpr size_t GOALKEEPERS = 3;
  constexpr size_t MAX_GOALKEEPERS = 4;
  std::vector<std::pair<double, const Player*>> ranked;
  ranked.reserve(eligible.size());
  for (const Player* player : eligible)
  {
    const auto it = caps.find(player->getId());
    ranked.emplace_back(
        selectionScore(*player, it != caps.end() ? it->second : 0, config),
        player);
  }
  std::ranges::sort(ranked,
                    [](const auto& a, const auto& b)
                    {
                      if (a.first != b.first) return a.first > b.first;
                      return a.second->getId() < b.second->getId();
                    });
  // Quotas: 3 goalkeepers, the rest ~35% defenders, ~35% midfielders.
  const size_t outfield = size > GOALKEEPERS ? size - GOALKEEPERS : 0;
  const size_t defenders = (outfield * 35 + 50) / 100;
  const size_t midfielders = (outfield * 35 + 50) / 100;
  const std::array<size_t, 4> quota = {GOALKEEPERS, defenders, midfielders,
                                       outfield - defenders - midfielders};
  std::array<size_t, 4> picked_by_group{};
  std::vector<PlayerID> squad;
  std::vector<uint8_t> used(ranked.size(), 0);
  for (size_t index = 0; index < ranked.size(); ++index)
  {
    if (squad.size() >= size)
    {
      break;
    }
    const auto group =
        std::to_underlying(roleGroup(ranked[index].second->getRole()));
    if (picked_by_group[group] >= quota[group]) continue;
    ++picked_by_group[group];
    used[index] = 1;
    squad.push_back(ranked[index].second->getId());
  }
  for (size_t index = 0; index < ranked.size(); ++index)
  {
    if (squad.size() >= size)
    {
      break;
    }
    if (used[index]) continue;
    const auto group =
        std::to_underlying(roleGroup(ranked[index].second->getRole()));
    if (group == 0 && picked_by_group[0] >= MAX_GOALKEEPERS) continue;
    ++picked_by_group[group];
    squad.push_back(ranked[index].second->getId());
  }
  return squad;
}

// ---------------------------------------------------------------------------
// NationalTeams
// ---------------------------------------------------------------------------

NationalTeams::NationalTeams(std::shared_ptr<GameData> game_data)
    : gamedata(std::move(game_data))
{
}

const NationalTeams::Team* NationalTeams::getTeam(Language nation) const
{
  const auto it = std::ranges::find(teams, nation, &Team::nation);
  return it == teams.end() ? nullptr : std::to_address(it);
}

std::vector<Language> NationalTeams::ranking() const
{
  std::vector<const Team*> ordered;
  for (const Team& team : teams) ordered.push_back(&team);
  std::ranges::sort(ordered,
                    [](const Team* a, const Team* b)
                    {
                      if (a->rating != b->rating) return a->rating > b->rating;
                      return a->nation < b->nation;
                    });
  std::vector<Language> nations;
  for (const Team* team : ordered) nations.push_back(team->nation);
  return nations;
}

const International::Record* NationalTeams::getRecord(PlayerID player_id) const
{
  const auto it = records.find(player_id);
  return it == records.end() ? nullptr : &it->second;
}

std::vector<std::pair<PlayerID, International::Record>>
NationalTeams::capsLeaders(size_t limit) const
{
  std::vector<std::pair<PlayerID, International::Record>> leaders(
      records.begin(), records.end());
  std::ranges::sort(leaders,
                    [](const auto& a, const auto& b)
                    {
                      if (a.second.caps != b.second.caps)
                        return a.second.caps > b.second.caps;
                      if (a.second.goals != b.second.goals)
                        return a.second.goals > b.second.goals;
                      return a.first < b.first;
                    });
  if (leaders.size() > limit) leaders.resize(limit);
  return leaders;
}

std::vector<International::GroupRow> NationalTeams::table(
    const Group& group) const
{
  std::map<Language, double> rating;
  for (const Team& team : teams) rating[team.nation] = team.rating;
  return International::groupTable(group, fixtures, rating);
}

bool NationalTeams::isOnDuty(PlayerID player_id,
                             const GameDateValue& date) const
{
  const auto it = duty.find(player_id);
  return it != duty.end() && between(date, it->second.first, it->second.second);
}

std::optional<Language> NationalTeams::dutyNation(
    PlayerID player_id, const GameDateValue& date) const
{
  for (const Squad& squad : squads)
  {
    if (!between(date, squad.announced, squad.until)) continue;
    if (std::ranges::contains(squad.players, player_id)) return squad.nation;
  }
  return std::nullopt;
}

double NationalTeams::injuryRiskMultiplier(PlayerID player_id,
                                           const GameDateValue& date) const
{
  const auto it = travel.find(player_id);
  if (it == travel.end()) return 1.0;
  const int days = dayOrdinal(date) - dayOrdinal(it->second.returned);
  if (days < 0 || days >= TRAVEL_RISK_DAYS) return 1.0;
  const double fade = 1.0 - static_cast<double>(days) / TRAVEL_RISK_DAYS;
  return 1.0 + (RISK_PER_TIME_ZONE * it->second.time_zones +
                RISK_PER_KM * it->second.km) *
                   fade;
}

bool NationalTeams::isEligibleFor(const Player& player, Language nation,
                                  const GameDateValue& date) const
{
  if (player.getNationality() != nation) return false;
  const auto record = records.find(player.getId());
  return record == records.end() || record->second.nation == nation ||
         International::canSwitchNation(record->second, date);
}

const NationalTeams::Squad* NationalTeams::squadOf(
    Language nation, const GameDateValue& date) const
{
  const Squad* found = nullptr;
  for (const Squad& squad : squads)
  {
    if (squad.nation != nation || squad.until < date) continue;
    if (!found || squad.start < found->start) found = &squad;
  }
  return found;
}

std::vector<PlayerID> NationalTeams::eligiblePool(
    Language nation, const GameDateValue& date) const
{
  std::vector<PlayerID> ids;
  for (const auto& [id, player] : gamedata->getPlayers())
  {
    if (player.getNationality() == nation && player.getAge() >= MIN_SQUAD_AGE &&
        player.getTeamId() != FREE_AGENTS_TEAM_ID &&
        isEligibleFor(player, nation, date))
      ids.push_back(id);
  }
  std::ranges::sort(ids);
  return ids;
}

International::CallUpResult NationalTeams::validateSquad(
    Language nation, const std::vector<PlayerID>& players,
    const GameDateValue& today) const
{
  using International::CallUpResult;
  using enum International::CallUpResult;
  const Squad* squad = squadOf(nation, today);
  if (!squad) return NoSquad;
  if (!(today < squad->start)) return Locked;
  if (players.size() > squadLimit(*squad)) return TooMany;
  if (players.size() < International::MIN_CALL_UPS) return TooFew;
  std::vector<PlayerID> sorted = players;
  std::ranges::sort(sorted);
  if (std::ranges::adjacent_find(sorted) != sorted.end()) return Duplicate;
  size_t goalkeepers = 0;
  for (const PlayerID player_id : players)
  {
    const auto player = gamedata->getPlayer(player_id);
    if (!player || player->get().getAge() < MIN_SQUAD_AGE ||
        player->get().getTeamId() == FREE_AGENTS_TEAM_ID ||
        !player->get().isAvailable() ||
        !isEligibleFor(player->get(), nation, today))
      return CallUpResult::Ineligible;
    if (player->get().getRole() == PlayerRole::GK) ++goalkeepers;
  }
  if (goalkeepers < International::MIN_CALL_UP_GOALKEEPERS)
    return CallUpResult::NeedGoalkeepers;
  return CallUpResult::Ok;
}

International::CallUpResult NationalTeams::setSquad(
    Language nation, std::vector<PlayerID> players, const GameDateValue& today)
{
  const International::CallUpResult verdict =
      validateSquad(nation, players, today);
  if (verdict != International::CallUpResult::Ok) return verdict;
  for (Squad& squad : squads)
  {
    if (&squad != squadOf(nation, today)) continue;
    squad.players = std::move(players);
    break;
  }
  rebuildDutyIndex();
  return verdict;
}

void NationalTeams::setCoach(Language nation, std::string name)
{
  const auto it = std::ranges::find(teams, nation, &Team::nation);
  if (it != teams.end()) it->coach = std::move(name);
}

double NationalTeams::expectedScore(Language home, Language away,
                                    bool neutral) const
{
  const Team* home_team = getTeam(home);
  const Team* away_team = getTeam(away);
  if (!home_team || !away_team) return 0.5;
  const double advantage = neutral ? 0.0 : HOME_ADVANTAGE;
  return 1.0 / (1.0 + std::pow(10.0, (away_team->rating - home_team->rating -
                                      advantage) /
                                         ELO_SCALE));
}

void NationalTeams::refreshNations()
{
  const StatsConfig& config = gamedata->getStatsConfig();
  std::map<Language, std::vector<double>> overall_by_nation;
  std::map<Language, size_t> keepers;
  for (const auto& [id, player] : gamedata->getPlayers())
  {
    overall_by_nation[player.getNationality()].push_back(
        player.getOverall(config));
    if (player.getRole() == PlayerRole::GK) ++keepers[player.getNationality()];
  }
  const NamePool& names = NamePool::instance();
  const uint64_t seed = gamedata->getWorldSeed();
  const auto seed32 = static_cast<uint32_t>(seed ^ (seed >> 32U));
  for (auto& [nation, overalls] : overall_by_nation)
  {
    if (overalls.size() < MIN_POOL || keepers[nation] < MIN_GOALKEEPERS)
      continue;
    if (getTeam(nation)) continue;
    std::ranges::sort(overalls, std::greater<>{});
    const size_t sample = std::min(RATING_SAMPLE, overalls.size());
    double total = 0.0;
    for (size_t i = 0; i < sample; ++i) total += overalls[i];
    Team team;
    team.nation = nation;
    team.rating =
        RATING_BASE + RATING_PER_OVERALL * total / static_cast<double>(sample);
    const auto& first = names.firstNames(nation);
    if (const auto& last = names.lastNames(nation);
        !first.empty() && !last.empty())
    {
      const uint32_t hash =
          Competitions::mixSeed(seed32, std::to_underlying(nation), COACH_SALT);
      team.coach =
          first[hash % first.size()] + " " + last[(hash / 7U) % last.size()];
    }
    teams.push_back(std::move(team));
  }
  std::ranges::sort(teams, {}, &Team::nation);
}

std::vector<Language> NationalTeams::pool(
    std::optional<Confederation> confederation) const
{
  std::vector<Language> nations;
  for (const Language nation : ranking())
    if (!confederation ||
        International::confederationOf(nation) == *confederation)
      nations.push_back(nation);
  return nations;
}

uint32_t NationalTeams::addFixture(Fixture fixture)
{
  fixture.id = next_fixture_id++;
  fixture.date = avoidChangeover(fixture.date);
  fixtures.push_back(fixture);
  return fixture.id;
}

bool NationalTeams::hasFixture(Language nation, const GameDateValue& date) const
{
  return std::ranges::any_of(
      fixtures,
      [&](const Fixture& fixture)
      {
        return fixture.date == date &&
               (fixture.home == nation || fixture.away == nation);
      });
}

void NationalTeams::scheduleGroup(const Group& group,
                                  const std::vector<GameDateValue>& slots,
                                  bool neutral)
{
  if (slots.empty() || group.members.size() < 2) return;
  auto rounds = International::roundRobin(group.members.size(), true);
  if (rounds.size() > slots.size())
    rounds = International::roundRobin(group.members.size(), false);
  for (size_t round = 0; round < rounds.size() && round < slots.size(); ++round)
  {
    const size_t slot = round * slots.size() / rounds.size();
    for (const auto& [home, away] : rounds[round])
    {
      Fixture fixture;
      fixture.date = slots[slot];
      fixture.home = group.members[home];
      fixture.away = group.members[away];
      fixture.competition = group.competition;
      fixture.group = group.index;
      fixture.neutral = neutral;
      addFixture(fixture);
    }
  }
}

void NationalTeams::planQualifiers(uint16_t season_year, uint16_t finals_year,
                                   const std::vector<GameDateValue>& slots,
                                   const GameDateValue& summer_start)
{
  const bool world = finals_year % 4 == 2;
  std::vector<std::optional<Confederation>> scopes;
  if (world)
    scopes.emplace_back(std::nullopt);
  else
    for (const Confederation confederation :
         {Confederation::Europe, Confederation::Americas, Confederation::Asia})
      scopes.emplace_back(confederation);

  uint8_t next_group = 1;
  for (const auto& scope : scopes)
  {
    const std::vector<Language> nations = pool(scope);
    const size_t size = International::finalsSize(nations.size());
    if (size == 0) continue;
    Finals entry;
    entry.competition =
        world ? Competition::WorldFinals : Competition::ContinentalFinals;
    entry.year = finals_year;
    entry.confederation = scope.value_or(Confederation::Europe);
    entry.size = static_cast<uint8_t>(size);
    entry.start = summer_start;
    finals.push_back(entry);

    // Groups drawn from pots by rating; the top two qualify.
    const size_t group_count = size / 2;
    std::vector<Group> drawn(group_count);
    for (size_t g = 0; g < group_count; ++g)
    {
      drawn[g].competition = world ? Competition::WorldQualifier
                                   : Competition::ContinentalQualifier;
      drawn[g].index = static_cast<uint8_t>(next_group + g);
    }
    std::mt19937 rng(Competitions::mixSeed(
        season_year, std::to_underlying(entry.confederation), GROUP_DRAW_SALT));
    for (size_t first = 0; first < nations.size(); first += group_count)
    {
      std::vector<Language> pot(
          nations.begin() + static_cast<std::ptrdiff_t>(first),
          nations.begin() + static_cast<std::ptrdiff_t>(
                                std::min(first + group_count, nations.size())));
      std::ranges::shuffle(pot, rng);
      for (size_t i = 0; i < pot.size(); ++i)
        drawn[i].members.push_back(pot[i]);
    }
    for (const Group& group : drawn)
    {
      scheduleGroup(group, slots, false);
      groups.push_back(group);
    }
    next_group = static_cast<uint8_t>(next_group + group_count);
  }
}

void NationalTeams::planNationsLeague(uint16_t season_year,
                                      const std::vector<GameDateValue>& slots,
                                      const GameDateValue& summer_start)
{
  constexpr size_t GROUP_SIZE = 4;
  constexpr size_t MIN_GROUP = 3;
  const std::vector<Language> nations = pool(std::nullopt);
  std::vector<Group> drawn;
  for (size_t first = 0; first < nations.size(); first += GROUP_SIZE)
  {
    Group group;
    group.competition = Competition::NationsLeague;
    group.index = static_cast<uint8_t>(drawn.size() + 1);
    for (size_t i = first; i < std::min(first + GROUP_SIZE, nations.size());
         ++i)
      group.members.push_back(nations[i]);
    if (group.members.size() < MIN_GROUP && !drawn.empty())
      drawn.back().members.insert(drawn.back().members.end(),
                                  group.members.begin(), group.members.end());
    else
      drawn.push_back(std::move(group));
  }
  std::mt19937 rng(Competitions::mixSeed(season_year, 0x4E4C, GROUP_DRAW_SALT));
  for (Group& group : drawn)
  {
    std::ranges::shuffle(group.members, rng);
    scheduleGroup(group, slots, false);
    groups.push_back(group);
  }
  if (nations.size() >= 4)
  {
    Finals entry;
    entry.competition = Competition::NationsLeagueFinals;
    entry.year = static_cast<uint16_t>(season_year + 1);
    entry.size = 4;
    entry.start = summer_start;
    finals.push_back(entry);
  }
}

void NationalTeams::planSeason(uint16_t season_year, const GameDateValue& today,
                               WorldSimulation* world)
{
  if (planned_season == season_year) return;
  planned_season = season_year;
  refreshNations();
  // Older cycles: keep last season's results for reference, nothing older.
  constexpr int KEEP_DAYS = 400;
  std::erase_if(
      fixtures, [today](const Fixture& fixture)
      { return dayOrdinal(today) - dayOrdinal(fixture.date) > KEEP_DAYS; });
  std::erase_if(finals, [season_year](const Finals& entry)
                { return entry.year < season_year; });
  groups.clear();

  const auto windows = SeasonCalendar::internationalWindows(season_year);
  std::vector<GameDateValue> slots;
  std::vector<GameDateValue> autumn;
  GameDateValue summer_start;
  for (const auto& window : windows)
  {
    if (window.summer)
    {
      summer_start = window.start;
      continue;
    }
    for (const GameDateValue& day : window.match_days)
    {
      if (day < today) continue;
      slots.push_back(day);
      if (window.start.month >= 9) autumn.push_back(day);
    }
  }
  if (const auto finals_year = static_cast<uint16_t>(season_year + 1);
      finals_year % 2 == 0)
    planQualifiers(season_year, finals_year, slots, summer_start);
  else
    planNationsLeague(season_year, autumn, summer_start);

  if (world && !groups.empty())
  {
    const Competition competition = groups.front().competition;
    post(*world, today, "INBOX_INTL_DRAW_TITLE", "INBOX_INTL_DRAW_BODY",
         {std::string("@") + International::competitionKey(competition),
          std::to_string(groups.size())},
         true);
  }
}

std::vector<PlayerID> NationalTeams::pickSquad(
    Language nation, size_t size, const GameDateValue& date,
    const std::vector<PlayerID>& exclude) const
{
  std::vector<const Player*> eligible;
  std::unordered_map<PlayerID, uint16_t> caps;
  for (const auto& [id, player] : gamedata->getPlayers())
  {
    if (player.getNationality() != nation || !player.isAvailable() ||
        player.getTeamId() == FREE_AGENTS_TEAM_ID ||
        player.getAge() < MIN_SQUAD_AGE || std::ranges::contains(exclude, id) ||
        !isEligibleFor(player, nation, date))
      continue;
    eligible.push_back(&player);
    if (const auto record = records.find(id); record != records.end())
      caps[id] = record->second.caps;
  }
  std::ranges::sort(eligible, {}, &Player::getId);
  return International::selectSquad(eligible, size, gamedata->getStatsConfig(),
                                    caps);
}

const NationalTeams::Finals* NationalTeams::finalsFor(
    Language nation, const GameDateValue& date) const
{
  for (const Finals& entry : finals)
  {
    if (!entry.drawn || !std::ranges::contains(entry.qualified, nation))
      continue;
    if (between(date, entry.start, plusDays(entry.start, 40))) return &entry;
  }
  return nullptr;
}

void NationalTeams::rebuildDutyIndex()
{
  duty.clear();
  for (const Squad& squad : squads)
    for (const PlayerID player_id : squad.players)
    {
      auto [it, inserted] =
          duty.try_emplace(player_id, squad.start, squad.until);
      if (!inserted && it->second.second < squad.until)
        it->second = {squad.start, squad.until};
    }
}

void NationalTeams::post(WorldSimulation& world, const GameDateValue& date,
                         std::string title_key, std::string body_key,
                         std::vector<std::string> args, bool read) const
{
  InboxMessage message;
  message.date = date;
  message.category = InboxCategory::Match;
  message.title_key = std::move(title_key);
  message.body_key = std::move(body_key);
  message.args = std::move(args);
  message.read = read;
  world.getInbox().add(std::move(message));
}

void NationalTeams::announceWindow(const GameDateValue& today,
                                   const std::vector<GameDateValue>& match_days,
                                   const GameDateValue& start,
                                   const GameDateValue& end,
                                   WorldSimulation& world,
                                   TeamID managed_team_id)
{
  // Friendlies for every nation without a match, within the confederation
  // first.
  scheduleWindowFriendlies(
      match_days, [this] { return ranking(); },
      [this](Language nation, const GameDateValue& day)
      { return hasFixture(nation, day); },
      [this](Language nation, const GameDateValue& day)
      { return finalsFor(nation, day); },
      [this](const Fixture& fixture) { addFixture(fixture); });

  std::vector<std::string> called;
  for (const Team& team : teams)
  {
    const Finals* tournament = finalsFor(team.nation, start);
    if (const bool plays =
            tournament ||
            std::ranges::any_of(fixtures,
                                [&](const Fixture& fixture)
                                {
                                  return between(fixture.date, start, end) &&
                                         (fixture.home == team.nation ||
                                          fixture.away == team.nation);
                                });
        !plays)
      continue;
    if (std::ranges::any_of(
            squads, [&](const Squad& squad)
            { return squad.nation == team.nation && !(squad.until < start); }))
      continue;
    Squad squad;
    squad.nation = team.nation;
    squad.announced = today;
    squad.start = start;
    squad.until = end;
    squad.finals = tournament != nullptr;
    if (tournament) extendFinalsSquadUntil(squad, *tournament, fixtures);
    squad.players = pickSquad(
        team.nation,
        tournament ? International::FINALS_SQUAD : International::WINDOW_SQUAD,
        today, {});
    collectManagedCallups(squad, managed_team_id, *gamedata, called);
    squads.push_back(std::move(squad));
  }
  rebuildDutyIndex();
  if (!called.empty())
  {
    std::ranges::sort(called);
    std::string list;
    for (const std::string& entry : called)
      list += (list.empty() ? "" : "\n") + entry;
    post(world, today, "INBOX_INTL_CALLUP_TITLE", "INBOX_INTL_CALLUP_BODY",
         {std::to_string(called.size()), list, start.toString()}, false);
  }
}

void NationalTeams::replaceInjured(const GameDateValue& today)
{
  bool changed = false;
  for (Squad& squad : squads)
  {
    if (!(squad.start == today)) continue;
    changed =
        replaceUnavailablePlayers(
            squad, today, *gamedata,
            [this](Language nation, size_t size, const GameDateValue& date,
                   const std::vector<PlayerID>& exclude)
            { return pickSquad(nation, size, date, exclude); }) ||
        changed;
  }
  if (changed) rebuildDutyIndex();
}

void NationalTeams::updateRatings(const Fixture& fixture)
{
  using enum Competition;
  auto home = std::ranges::find(teams, fixture.home, &Team::nation);
  auto away = std::ranges::find(teams, fixture.away, &Team::nation);
  if (home == teams.end() || away == teams.end()) return;
  double k = 20.0;
  switch (fixture.competition)
  {
    case Friendly:
      k = 20.0;
      break;
    case NationsLeague:
      k = 30.0;
      break;
    case WorldQualifier:
    case ContinentalQualifier:
      k = 40.0;
      break;
    default:
      k = 60.0;
      break;
  }
  const double advantage = fixture.neutral ? 0.0 : HOME_ADVANTAGE;
  const double expected =
      1.0 / (1.0 + std::pow(10.0, (away->rating - home->rating - advantage) /
                                      ELO_SCALE));
  double actual = 0.5;
  if (fixture.home_goals > fixture.away_goals) actual = 1.0;
  if (fixture.home_goals < fixture.away_goals) actual = 0.0;
  const int margin = std::abs(fixture.home_goals - fixture.away_goals);
  double weight = 1.75;
  if (margin <= 1)
    weight = 1.0;
  else if (margin == 2)
    weight = 1.5;
  const double change = k * weight * (actual - expected);
  home->rating += change;
  away->rating -= change;
}

void NationalTeams::playMatches(const GameDateValue& today,
                                MatchScheduler& scheduler,
                                WorldSimulation& world, TeamID managed_team_id)
{
  std::vector<size_t> today_fixtures;
  for (size_t index = 0; index < fixtures.size(); ++index)
    if (fixtures[index].date == today && !fixtures[index].played)
      today_fixtures.push_back(index);
  if (today_fixtures.empty()) return;

  const StatsConfig& config = gamedata->getStatsConfig();
  const auto squadFor = [&](Language nation) -> const Squad*
  {
    for (const Squad& squad : squads)
      if (squad.nation == nation && between(today, squad.start, squad.until))
        return &squad;
    // Loaded in the middle of a window: pick the squad now.
    Squad squad;
    squad.nation = nation;
    squad.announced = today;
    squad.start = today;
    squad.until = today;
    squad.players = pickSquad(nation, International::WINDOW_SQUAD, today, {});
    squads.push_back(std::move(squad));
    rebuildDutyIndex();
    return &squads.back();
  };
  // Resolve every squad first: squadFor may append to the vector.
  for (const size_t index : today_fixtures)
  {
    squadFor(fixtures[index].home);
    squadFor(fixtures[index].away);
  }
  const auto lineupFor = [&](Language nation)
  { return nationalLineup(squadFor(nation)->players, *gamedata, config); };

  std::vector<size_t> simulated;
  std::vector<MatchSimulationInput> inputs;
  addNationalMatchInputs(today_fixtures, fixtures, today, lineupFor, simulated,
                         inputs);
  if (inputs.empty()) return;
  std::vector<MatchSimulationResult> results = scheduler.run(inputs, config);
  for (size_t i = 0; i < simulated.size(); ++i)
  {
    Fixture& fixture = fixtures[simulated[i]];
    MatchSimulationResult& result = results[i];
    fixture.played = true;
    fixture.home_goals = result.home_goals;
    fixture.away_goals = result.away_goals;
    fixture.extra_time = result.extra_time;
    if (result.penalties)
    {
      fixture.penalties = true;
      fixture.home_penalties = result.penalties->first;
      fixture.away_penalties = result.penalties->second;
    }
    const bool finals_match =
        International::isFinals(fixture.competition) &&
        fixture.competition != Competition::NationsLeagueFinals;
    for (const PlayerMatchLine& line : result.report.players)
      recordAppearance(line, finals_match, today, *gamedata, records);
    for (const PlayerMatchConsequence& consequence : result.consequences)
      world.applyMatchConsequences(today, consequence, managed_team_id);
    if (result_sink)
      result_sink(fixture,
                  expectedScore(fixture.home, fixture.away, fixture.neutral));
    updateRatings(fixture);
  }
}

void NationalTeams::drawFinals(Finals& entry, const GameDateValue& today,
                               WorldSimulation& world)
{
  entry.drawn = true;
  std::map<Language, double> rating;
  for (const Team& team : teams) rating[team.nation] = team.rating;
  const auto byRating = [&rating](std::vector<Language>& nations)
  {
    std::ranges::sort(nations,
                      [&rating](Language a, Language b)
                      {
                        if (rating[a] != rating[b])
                          return rating[a] > rating[b];
                        return a < b;
                      });
  };

  std::vector<Language> qualified;
  std::vector<International::GroupRow> runners_up;
  Competition qualifier = Competition::NationsLeague;
  if (entry.competition == Competition::WorldFinals)
    qualifier = Competition::WorldQualifier;
  else if (entry.competition == Competition::ContinentalFinals)
    qualifier = Competition::ContinentalQualifier;
  const size_t advancing =
      entry.competition == Competition::NationsLeagueFinals ? 1 : 2;
  for (const Group& group : groups)
  {
    if (group.competition != qualifier || group.members.empty()) continue;
    if (entry.competition == Competition::ContinentalFinals &&
        International::confederationOf(group.members.front()) !=
            entry.confederation)
      continue;
    if (entry.competition == Competition::NationsLeagueFinals &&
        qualified.size() >= entry.size)
      break;
    const auto rows = table(group);
    for (size_t i = 0; i < rows.size(); ++i)
    {
      if (i < advancing)
        qualified.push_back(rows[i].nation);
      else if (i == advancing)
        runners_up.push_back(rows[i]);
    }
  }
  std::ranges::sort(runners_up,
                    [](const auto& a, const auto& b)
                    {
                      if (a.points != b.points) return a.points > b.points;
                      return a.goalDifference() > b.goalDifference();
                    });
  for (const auto& row : runners_up)
    if (qualified.size() < entry.size) qualified.push_back(row.nation);
  // Without qualifiers (a career started late in the cycle) the best-rated
  // nations of the pool take the remaining places.
  for (const Language nation :
       pool(entry.competition == Competition::ContinentalFinals
                ? std::optional(entry.confederation)
                : std::nullopt))
    if (qualified.size() < entry.size &&
        !std::ranges::contains(qualified, nation))
      qualified.push_back(nation);
  if (qualified.size() > entry.size) qualified.resize(entry.size);
  if (qualified.size() < 4) return;
  byRating(qualified);
  entry.qualified = qualified;

  const auto summer = [&]
  {
    for (const auto& window : SeasonCalendar::internationalWindows(
             SeasonCalendar::seasonStartYear(entry.start)))
      if (window.summer) return window.match_days;
    return std::vector<GameDateValue>{};
  }();

  if (entry.competition == Competition::NationsLeagueFinals)
  {
    const GameDateValue day =
        summer.empty() ? plusDays(entry.start, 4) : summer.front();
    for (const auto& [home, away] : {std::pair{qualified[0], qualified[3]},
                                     std::pair{qualified[1], qualified[2]}})
    {
      Fixture fixture;
      fixture.date = day;
      fixture.home = home;
      fixture.away = away;
      fixture.competition = entry.competition;
      fixture.stage = Stage::SemiFinal;
      fixture.neutral = true;
      addFixture(fixture);
    }
  }
  else
  {
    const size_t group_count = qualified.size() / 4;
    std::mt19937 rng(Competitions::mixSeed(
        entry.year, std::to_underlying(entry.competition),
        GROUP_DRAW_SALT + std::to_underlying(entry.confederation)));
    entry.groups.assign(group_count, Group{});
    for (size_t g = 0; g < group_count; ++g)
    {
      entry.groups[g].competition = entry.competition;
      entry.groups[g].index = static_cast<uint8_t>(g + 1);
    }
    for (size_t pot = 0; pot < 4; ++pot)
    {
      std::vector<Language> members(
          qualified.begin() + static_cast<std::ptrdiff_t>(pot * group_count),
          qualified.begin() +
              static_cast<std::ptrdiff_t>((pot + 1) * group_count));
      std::ranges::shuffle(members, rng);
      for (size_t g = 0; g < group_count; ++g)
        entry.groups[g].members.push_back(members[g]);
    }
    std::vector<GameDateValue> days;
    for (const int offset : FINALS_GROUP_DAYS)
      days.push_back(plusDays(entry.start, offset));
    for (const Group& group : entry.groups) scheduleGroup(group, days, true);
  }

  std::string names;
  for (const Language nation : qualified)
    names += (names.empty() ? "@" : ", @") + International::teamNameKey(nation);
  post(world, today, "INBOX_INTL_FINALS_DRAW_TITLE",
       "INBOX_INTL_FINALS_DRAW_BODY",
       {std::string("@") + International::competitionKey(entry.competition),
        names, entry.start.toString()},
       true);
  if (finals_sink) finals_sink(entry, false);
}

void NationalTeams::progressFinals(Finals& entry, const GameDateValue& today,
                                   WorldSimulation& world)
{
  if (entry.winner || entry.qualified.empty()) return;
  const GameDateValue last_day = plusDays(entry.start, 40);
  std::vector<const Fixture*> matches;
  for (const Fixture& fixture : fixtures)
  {
    if (fixture.competition != entry.competition ||
        !between(fixture.date, entry.start, last_day) ||
        !std::ranges::contains(entry.qualified, fixture.home))
      continue;
    matches.push_back(&fixture);
  }
  if (matches.empty() ||
      std::ranges::any_of(matches, [](const Fixture* f) { return !f->played; }))
    return;

  const auto releaseNation = [&](Language nation)
  {
    for (Squad& squad : squads)
      if (squad.nation == nation && squad.finals && today < squad.until)
        squad.until = today;
  };
  const bool league_finals =
      entry.competition == Competition::NationsLeagueFinals;
  std::vector<GameDateValue> knockout_days;
  if (league_finals)
  {
    for (const auto& window : SeasonCalendar::internationalWindows(
             SeasonCalendar::seasonStartYear(entry.start)))
      if (window.summer) knockout_days = window.match_days;
    if (knockout_days.size() < 2)
      knockout_days = {plusDays(entry.start, 4), plusDays(entry.start, 7)};
  }
  else
  {
    for (const int offset : FINALS_KNOCKOUT_DAYS)
      knockout_days.push_back(plusDays(entry.start, offset));
  }

  std::vector<const Fixture*> knockouts;
  for (const Fixture* fixture : matches)
    if (fixture->stage != Stage::Group) knockouts.push_back(fixture);

  std::vector<std::pair<Language, Language>> next;
  Stage next_stage = Stage::Final;
  size_t round_index = 0;
  if (knockouts.empty())
  {
    // Group stage over: winners and runners-up cross over.
    std::vector<std::vector<International::GroupRow>> tables;
    for (const Group& group : entry.groups) tables.push_back(table(group));
    for (const auto& group_table : tables)
      for (size_t i = 2; i < group_table.size(); ++i)
        releaseNation(group_table[i].nation);
    if (tables.size() == 1)
    {
      next.emplace_back(tables[0][0].nation, tables[0][1].nation);
    }
    else
    {
      for (size_t g = 0; g + 1 < tables.size(); g += 2)
      {
        next.emplace_back(tables[g][0].nation, tables[g + 1][1].nation);
        next.emplace_back(tables[g + 1][0].nation, tables[g][1].nation);
      }
    }
    if (next.size() >= 4)
      next_stage = Stage::QuarterFinal;
    else if (next.size() == 2)
      next_stage = Stage::SemiFinal;
    else
      next_stage = Stage::Final;
  }
  else
  {
    const Stage latest =
        std::ranges::max(knockouts, {}, &Fixture::stage)->stage;
    std::vector<Language> winners;
    for (const Fixture* fixture : knockouts)
    {
      if (fixture->stage != latest) continue;
      const auto winner = International::winnerOf(*fixture);
      if (!winner) return;
      winners.push_back(*winner);
      releaseNation(*winner == fixture->home ? fixture->away : fixture->home);
    }
    if (latest == Stage::Final)
    {
      const Fixture* final_match = *std::ranges::find_if(
          knockouts, [](const Fixture* f) { return f->stage == Stage::Final; });
      entry.winner = winners.front();
      entry.runner_up = *entry.winner == final_match->home ? final_match->away
                                                           : final_match->home;
      releaseNation(*entry.winner);
      honours.push_back(
          {entry.year, entry.competition, *entry.winner, *entry.runner_up});
      boostFinalists(entry, squads, *gamedata);
      post(world, today, "INBOX_INTL_WINNER_TITLE", "INBOX_INTL_WINNER_BODY",
           {std::string("@") + International::competitionKey(entry.competition),
            nationArg(*entry.winner), nationArg(*entry.runner_up)},
           true);
      if (finals_sink) finals_sink(entry, true);
      return;
    }
    for (size_t i = 0; i + 1 < winners.size(); i += 2)
      next.emplace_back(winners[i], winners[i + 1]);
    next_stage = static_cast<Stage>(std::to_underlying(latest) + 1);
    const auto rounds_played = static_cast<size_t>(
        std::to_underlying(latest) -
        std::to_underlying(
            std::ranges::min(knockouts, {}, &Fixture::stage)->stage));
    round_index = rounds_played + 1;
  }
  if (next.empty()) return;
  const GameDateValue day =
      knockout_days[std::min(round_index, knockout_days.size() - 1)];
  for (const auto& [home, away] : next)
  {
    Fixture fixture;
    fixture.date = today < day ? day : plusDays(today, 2);
    fixture.home = home;
    fixture.away = away;
    fixture.competition = entry.competition;
    fixture.stage = next_stage;
    fixture.neutral = true;
    addFixture(fixture);
  }
}

void NationalTeams::releasePlayers(const GameDateValue& today,
                                   WorldSimulation& world,
                                   TeamID managed_team_id)
{
  std::vector<std::string> returning;
  std::map<TeamID, int64_t> compensation;
  bool changed = false;
  for (const Squad& squad : squads)
  {
    if (!(squad.until < today)) continue;
    changed = true;
    processReleasedSquad(squad, today, managed_team_id, *gamedata, travel,
                         compensation, returning);
  }
  if (!changed) return;
  std::erase_if(squads,
                [&today](const Squad& squad) { return squad.until < today; });
  rebuildDutyIndex();
  for (const auto& [club, amount] : compensation)
    if (auto team = gamedata->getTeam(club))
      team->get().getFinances().record(today, FinanceCategory::PrizeMoney,
                                       amount);
  // Travel records only matter for a week.
  std::erase_if(travel,
                [&today](const auto& entry)
                {
                  return dayOrdinal(today) - dayOrdinal(entry.second.returned) >
                         TRAVEL_RISK_DAYS;
                });
  if (!returning.empty())
  {
    std::ranges::sort(returning);
    std::string list;
    for (const std::string& name : returning)
      list += (list.empty() ? "" : ", ") + name;
    post(world, today, "INBOX_INTL_RETURN_TITLE", "INBOX_INTL_RETURN_BODY",
         {std::to_string(returning.size()), list}, true);
  }
}

void NationalTeams::onDay(const GameDateValue& today, MatchScheduler& scheduler,
                          WorldSimulation& world, TeamID managed_team_id)
{
  const uint16_t season_year = SeasonCalendar::seasonStartYear(today);
  planSeason(season_year, today, &world);
  releasePlayers(today, world, managed_team_id);
  for (Finals& entry : finals)
    if (!entry.drawn && !(today < plusDays(entry.start, -FINALS_DRAW_DAYS)))
      drawFinals(entry, today, world);
  for (const auto& window : SeasonCalendar::internationalWindows(season_year))
  {
    if (today == plusDays(window.start, -ANNOUNCE_DAYS))
      announceWindow(today, window.match_days, window.start, window.end, world,
                     managed_team_id);
    if (today == window.start) replaceInjured(today);
  }
  playMatches(today, scheduler, world, managed_team_id);
  for (Finals& entry : finals)
    if (entry.drawn) progressFinals(entry, today, world);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

std::string NationalTeams::serialize() const
{
  using nlohmann::json;
  const auto nation = [](Language value) { return std::to_underlying(value); };
  json root;
  root["planned_season"] = planned_season;
  root["next_fixture_id"] = next_fixture_id;
  json teams_json = json::array();
  for (const Team& team : teams)
    teams_json.push_back({{"nation", nation(team.nation)},
                          {"coach", team.coach},
                          {"rating", team.rating}});
  root["teams"] = std::move(teams_json);
  json fixtures_json = json::array();
  for (const Fixture& f : fixtures)
    fixtures_json.push_back({f.id, f.date.toString(), nation(f.home),
                             nation(f.away), std::to_underlying(f.competition),
                             f.group, std::to_underlying(f.stage), f.neutral,
                             f.played, f.home_goals, f.away_goals, f.extra_time,
                             f.penalties, f.home_penalties, f.away_penalties});
  root["fixtures"] = std::move(fixtures_json);
  const auto groupJson = [&nation](const Group& group)
  {
    json members = json::array();
    for (const Language member : group.members)
      members.push_back(nation(member));
    return json{{"competition", std::to_underlying(group.competition)},
                {"index", group.index},
                {"members", std::move(members)}};
  };
  json groups_json = json::array();
  for (const Group& group : groups) groups_json.push_back(groupJson(group));
  root["groups"] = std::move(groups_json);
  json finals_json = json::array();
  for (const Finals& entry : finals)
  {
    json qualified = json::array();
    for (const Language member : entry.qualified)
      qualified.push_back(nation(member));
    json entry_groups = json::array();
    for (const Group& group : entry.groups)
      entry_groups.push_back(groupJson(group));
    finals_json.push_back(
        {{"competition", std::to_underlying(entry.competition)},
         {"year", entry.year},
         {"confederation", std::to_underlying(entry.confederation)},
         {"size", entry.size},
         {"start", entry.start.toString()},
         {"drawn", entry.drawn},
         {"qualified", std::move(qualified)},
         {"groups", std::move(entry_groups)},
         {"winner", entry.winner ? nation(*entry.winner) : -1},
         {"runner_up", entry.runner_up ? nation(*entry.runner_up) : -1}});
  }
  root["finals"] = std::move(finals_json);
  json squads_json = json::array();
  for (const Squad& squad : squads)
    squads_json.push_back({{"nation", nation(squad.nation)},
                           {"players", squad.players},
                           {"announced", squad.announced.toString()},
                           {"start", squad.start.toString()},
                           {"until", squad.until.toString()},
                           {"finals", squad.finals}});
  root["squads"] = std::move(squads_json);
  json honours_json = json::array();
  for (const Honour& honour : honours)
    honours_json.push_back({honour.year, std::to_underlying(honour.competition),
                            nation(honour.winner), nation(honour.runner_up)});
  root["honours"] = std::move(honours_json);
  std::vector<PlayerID> record_ids;
  for (const auto& [id, record] : records) record_ids.push_back(id);
  std::ranges::sort(record_ids);
  json records_json = json::array();
  for (const PlayerID id : record_ids)
  {
    const International::Record& r = records.at(id);
    records_json.push_back({id, nation(r.nation), r.name, r.caps, r.goals,
                            r.finals_caps, r.last_cap_age,
                            r.last_cap.toString()});
  }
  root["records"] = std::move(records_json);
  std::vector<PlayerID> travel_ids;
  for (const auto& [id, trip] : travel) travel_ids.push_back(id);
  std::ranges::sort(travel_ids);
  json travel_json = json::array();
  for (const PlayerID id : travel_ids)
  {
    const Travel& trip = travel.at(id);
    travel_json.push_back(
        {id, trip.returned.toString(), trip.km, trip.time_zones});
  }
  root["travel"] = std::move(travel_json);
  return root.dump();
}

void NationalTeams::deserialize(const std::string& data)
{
  using nlohmann::json;
  teams.clear();
  fixtures.clear();
  groups.clear();
  finals.clear();
  squads.clear();
  honours.clear();
  records.clear();
  travel.clear();
  duty.clear();
  planned_season = 0;
  next_fixture_id = 1;
  if (data.empty()) return;
  const json root = json::parse(data, nullptr, false);
  if (root.is_discarded() || !root.is_object()) return;
  const auto nation = [](const json& value)
  { return static_cast<Language>(value.get<int>()); };
  const auto date = [](const json& value)
  { return GameDateValue::fromString(value.get<std::string>()); };
  planned_season = root.value("planned_season", uint16_t{0});
  next_fixture_id = root.value("next_fixture_id", uint32_t{1});
  for (const json& item : root.value("teams", json::array()))
    teams.emplace_back(nation(item.at("nation")),
                       item.value("coach", std::string()),
                       item.value("rating", 1500.0));
  for (const json& f : root.value("fixtures", json::array()))
  {
    if (!f.is_array() || f.size() < 15) continue;
    Fixture fixture;
    fixture.id = f[0].get<uint32_t>();
    fixture.date = date(f[1]);
    fixture.home = nation(f[2]);
    fixture.away = nation(f[3]);
    fixture.competition = static_cast<Competition>(f[4].get<int>());
    fixture.group = f[5].get<uint8_t>();
    fixture.stage = static_cast<Stage>(f[6].get<int>());
    fixture.neutral = f[7].get<bool>();
    fixture.played = f[8].get<bool>();
    fixture.home_goals = f[9].get<uint8_t>();
    fixture.away_goals = f[10].get<uint8_t>();
    fixture.extra_time = f[11].get<bool>();
    fixture.penalties = f[12].get<bool>();
    fixture.home_penalties = f[13].get<uint8_t>();
    fixture.away_penalties = f[14].get<uint8_t>();
    fixtures.push_back(fixture);
  }
  const auto groupFrom = [&nation](const json& item)
  {
    Group group;
    group.competition = static_cast<Competition>(item.value("competition", 0));
    group.index = item.value("index", uint8_t{1});
    for (const json& member : item.value("members", json::array()))
      group.members.push_back(nation(member));
    return group;
  };
  for (const json& item : root.value("groups", json::array()))
    groups.push_back(groupFrom(item));
  for (const json& item : root.value("finals", json::array()))
  {
    Finals entry;
    entry.competition = static_cast<Competition>(item.value("competition", 0));
    entry.year = item.value("year", uint16_t{0});
    entry.confederation =
        static_cast<Confederation>(item.value("confederation", 0));
    entry.size = item.value("size", uint8_t{0});
    entry.start = GameDateValue::fromString(item.value("start", std::string()));
    entry.drawn = item.value("drawn", false);
    for (const json& member : item.value("qualified", json::array()))
      entry.qualified.push_back(nation(member));
    for (const json& group : item.value("groups", json::array()))
      entry.groups.push_back(groupFrom(group));
    if (const int winner = item.value("winner", -1); winner >= 0)
      entry.winner = static_cast<Language>(winner);
    if (const int runner_up = item.value("runner_up", -1); runner_up >= 0)
      entry.runner_up = static_cast<Language>(runner_up);
    finals.push_back(std::move(entry));
  }
  for (const json& item : root.value("squads", json::array()))
  {
    Squad squad;
    squad.nation = nation(item.at("nation"));
    squad.players = item.value("players", std::vector<PlayerID>{});
    squad.announced =
        GameDateValue::fromString(item.value("announced", std::string()));
    squad.start = GameDateValue::fromString(item.value("start", std::string()));
    squad.until = GameDateValue::fromString(item.value("until", std::string()));
    squad.finals = item.value("finals", false);
    squads.push_back(std::move(squad));
  }
  for (const json& h : root.value("honours", json::array()))
    if (h.is_array() && h.size() >= 4)
      honours.push_back({h[0].get<uint16_t>(),
                         static_cast<Competition>(h[1].get<int>()),
                         nation(h[2]), nation(h[3])});
  for (const json& r : root.value("records", json::array()))
  {
    if (!r.is_array() || r.size() < 8) continue;
    International::Record record;
    record.nation = nation(r[1]);
    record.name = r[2].get<std::string>();
    record.caps = r[3].get<uint16_t>();
    record.goals = r[4].get<uint16_t>();
    record.finals_caps = r[5].get<uint16_t>();
    record.last_cap_age = r[6].get<uint8_t>();
    record.last_cap = date(r[7]);
    records[r[0].get<PlayerID>()] = std::move(record);
  }
  for (const json& t : root.value("travel", json::array()))
    if (t.is_array() && t.size() >= 4)
      travel[t[0].get<PlayerID>()] = {date(t[1]), t[2].get<uint16_t>(),
                                      t[3].get<uint8_t>()};
  rebuildDutyIndex();
}

void NationalTeams::load(const DatabaseConnection& db)
{
  std::string data;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db.getRaw(),
                         "SELECT data FROM InternationalState WHERE id = 1;",
                         -1, &stmt, nullptr) == SQLITE_OK &&
      sqlite3_step(stmt) == SQLITE_ROW)
  {
    if (const auto* text = sqlite3_column_text(stmt, 0))
      data = reinterpret_cast<const char*>(text);
  }
  sqlite3_finalize(stmt);
  deserialize(data);
}

void NationalTeams::save(const DatabaseConnection& db) const
{
  sqlite3_stmt* stmt = db.prepareStatement(
      "INSERT OR REPLACE INTO InternationalState (id, data) VALUES (1, ?);");
  const std::string data = serialize();
  sqlite3_bind_text(stmt, 1, data.c_str(), -1, SQLITE_TRANSIENT);
  db.executeStep(stmt);
  sqlite3_finalize(stmt);
}
