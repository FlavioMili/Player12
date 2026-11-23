// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/continental.h"

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <nlohmann/json.hpp>
#include <numeric>
#include <random>
#include <set>
#include <utility>

#include "database/database_connection.h"
#include "database/gamedata.h"
#include "global/global.h"
#include "model/calendar.h"
#include "model/competition.h"
#include "model/league.h"
#include "model/match.h"
#include "model/team.h"
#include "model/world_rng.h"
#include "model/world_tuning.h"

using Continental::CompetitionRules;
using Continental::Round;

namespace
{
constexpr uint32_t LEAGUE_DRAW_SALT = 0xC0'17'01;
constexpr uint32_t KNOCKOUT_DRAW_SALT = 0xC0'17'10;
constexpr uint8_t RELAXED_ASSOCIATION_LIMIT = 3;
constexpr uint8_t NO_ASSOCIATION_LIMIT = 0xFF;
constexpr size_t DRAW_ATTEMPTS = 40;
constexpr size_t DRAW_STEP_BUDGET = 60'000;
constexpr size_t MATCHDAY_ATTEMPTS = 200;
constexpr int DRAW_EARLIEST_DAY = 25;  // League-phase draw: Friday >= 25 Aug.
constexpr int LATEST_START_DAYS = 3;   // Draw at least 3 days before MD1.
constexpr uint8_t MIN_CLUBS = 12;
// Coefficient priors for seasons before the career. [P] Chosen so that the
// strongest leagues start near the real top association values (~18-20
// points a season) and elite clubs near ~100 over five seasons.
constexpr double ASSOCIATION_PRIOR_SLOPE = 0.45;
constexpr double ASSOCIATION_PRIOR_FLOOR = 2.0;
constexpr double CLUB_PRIOR_SLOPE = 0.5;
constexpr double PRIOR_REPUTATION_BASE = 50.0;
constexpr double ASSOCIATION_SHARE = 0.2;
constexpr double ROUND_BONUS = 1.0;

GameDateValue plusDays(const GameDateValue& date, int days)
{
  return SeasonCalendar::addDays(date, days);
}

// ---------------- Swiss draw ----------------

struct DrawState
{
  size_t pots = 0;
  size_t pot_size = 0;
  std::vector<uint32_t> association;               // By team index (pot-major).
  std::vector<std::vector<uint8_t>> met;           // met[a][b]
  std::vector<std::map<uint32_t, uint8_t>> faced;  // Opponents per association.
  // host[p][q][i] = j: team i of pot p hosts team j of pot q.
  std::vector<std::vector<std::vector<int>>> host;
  std::vector<std::vector<std::vector<uint8_t>>> taken;
  uint8_t limit = 2;
  bool allow_own = false;

  size_t index(size_t pot, size_t member) const
  {
    return pot * pot_size + member;
  }

  bool allowed(size_t p, size_t q, size_t i, size_t j) const
  {
    if (taken[p][q][j]) return false;
    const size_t a = index(p, i);
    const size_t b = index(q, j);
    if (a == b || met[a][b]) return false;
    if (association[a] == association[b] && !allow_own) return false;
    if (limit != NO_ASSOCIATION_LIMIT)
    {
      const auto count = [&](size_t team, uint32_t other)
      {
        const auto it = faced[team].find(other);
        return it == faced[team].end() ? 0 : it->second;
      };
      if (count(a, association[b]) >= limit ||
          count(b, association[a]) >= limit)
        return false;
    }
    return true;
  }

  void place(size_t p, size_t q, size_t i, size_t j, bool on)
  {
    const size_t a = index(p, i);
    const size_t b = index(q, j);
    host[p][q][i] = on ? static_cast<int>(j) : -1;
    taken[p][q][j] = on ? 1 : 0;
    met[a][b] = on ? 1 : 0;
    met[b][a] = on ? 1 : 0;
    const int delta = on ? 1 : -1;
    faced[a][association[b]] =
        static_cast<uint8_t>(faced[a][association[b]] + delta);
    faced[b][association[a]] =
        static_cast<uint8_t>(faced[b][association[a]] + delta);
  }
};

struct DrawSlot
{
  size_t p;
  size_t q;
  size_t i;
};

bool rowsFeasibleAfterPlacement(const DrawState& state, const DrawSlot& slot)
{
  for (size_t i = slot.i + 1; i < state.pot_size; ++i)
  {
    bool any = false;
    for (size_t j = 0; j < state.pot_size && !any; ++j)
    {
      any = state.allowed(slot.p, slot.q, i, j);
    }
    if (!any)
    {
      return false;
    }
  }
  return true;
}

void preparePairingCandidates(const DrawState& state, const DrawSlot& slot,
                              std::vector<size_t>& candidates, size_t& cursor,
                              std::mt19937& rng)
{
  candidates.clear();
  for (size_t j = 0; j < state.pot_size; ++j)
  {
    if (state.allowed(slot.p, slot.q, slot.i, j))
    {
      candidates.push_back(j);
    }
  }
  std::ranges::shuffle(candidates, rng);
  cursor = 0;
}

bool tryPlacePairing(DrawState& state, const DrawSlot& slot,
                     const std::vector<size_t>& candidates, size_t& cursor)
{
  while (cursor < candidates.size())
  {
    const size_t candidate = candidates[cursor++];
    if (!state.allowed(slot.p, slot.q, slot.i, candidate))
    {
      continue;
    }
    state.place(slot.p, slot.q, slot.i, candidate, true);
    if (rowsFeasibleAfterPlacement(state, slot))
    {
      return true;
    }
    state.place(slot.p, slot.q, slot.i, candidate, false);
  }
  return false;
}

/** Randomised depth-first search over every (pot, pot, row) slot. */
bool searchPairings(DrawState& state, const std::vector<DrawSlot>& slots,
                    std::mt19937& rng)
{
  std::vector<std::vector<size_t>> candidates(slots.size());
  std::vector<size_t> cursor(slots.size(), 0);
  size_t depth = 0;
  size_t steps = 0;
  preparePairingCandidates(state, slots[0], candidates[0], cursor[0], rng);
  while (true)
  {
    if (++steps > DRAW_STEP_BUDGET)
    {
      return false;
    }
    const DrawSlot& slot = slots[depth];
    if (state.host[slot.p][slot.q][slot.i] >= 0)
    {
      state.place(slot.p, slot.q, slot.i,
                  static_cast<size_t>(state.host[slot.p][slot.q][slot.i]),
                  false);
    }
    if (tryPlacePairing(state, slot, candidates[depth], cursor[depth]))
    {
      if (++depth == slots.size())
      {
        return true;
      }
      preparePairingCandidates(state, slots[depth], candidates[depth],
                               cursor[depth], rng);
      continue;
    }
    if (depth == 0)
    {
      return false;
    }
    --depth;
  }
}

std::vector<size_t> matchdayOptions(
    size_t team, const std::vector<std::vector<size_t>>& incident,
    const std::vector<uint8_t>& matchday, const std::vector<uint8_t>& busy,
    const std::vector<std::pair<size_t, size_t>>& edges, std::mt19937& rng)
{
  std::vector<size_t> list;
  for (const size_t edge : incident[team])
  {
    if (matchday[edge] != 0)
    {
      continue;
    }
    const size_t other =
        edges[edge].first == team ? edges[edge].second : edges[edge].first;
    if (busy[other] == 0u)
    {
      list.push_back(edge);
    }
  }
  std::ranges::shuffle(list, rng);
  return list;
}

std::optional<size_t> pickMatchdayTeam(
    size_t teams, const std::vector<std::vector<size_t>>& incident,
    const std::vector<uint8_t>& matchday, const std::vector<uint8_t>& busy,
    const std::vector<std::pair<size_t, size_t>>& edges)
{
  std::optional<size_t> best;
  size_t best_count = SIZE_MAX;
  for (size_t team = 0; team < teams; ++team)
  {
    if (busy[team] != 0u)
    {
      continue;
    }
    size_t count = 0;
    for (const size_t edge : incident[team])
    {
      if (matchday[edge] != 0)
      {
        continue;
      }
      const size_t other =
          edges[edge].first == team ? edges[edge].second : edges[edge].first;
      if (busy[other] == 0u)
      {
        ++count;
      }
    }
    if (count < best_count)
    {
      best_count = count;
      best = team;
    }
  }
  return best;
}

bool tryAssignMatchdayRound(size_t teams, size_t round,
                            const std::vector<std::vector<size_t>>& incident,
                            const std::vector<std::pair<size_t, size_t>>& edges,
                            std::vector<uint8_t>& matchday, std::mt19937& rng)
{
  std::vector<uint8_t> busy(teams, 0);
  std::vector<size_t> chosen;
  std::vector<std::vector<size_t>> stack;
  size_t steps = 0;
  bool matched = false;
  while (++steps < DRAW_STEP_BUDGET)
  {
    const std::optional<size_t> team =
        pickMatchdayTeam(teams, incident, matchday, busy, edges);
    if (!team.has_value())
    {
      matched = true;
      break;
    }
    stack.push_back(
        matchdayOptions(*team, incident, matchday, busy, edges, rng));
    bool advanced = false;
    while (!stack.empty())
    {
      auto& list = stack.back();
      if (!list.empty())
      {
        const size_t edge = list.back();
        list.pop_back();
        busy[edges[edge].first] = 1;
        busy[edges[edge].second] = 1;
        chosen.push_back(edge);
        advanced = true;
        break;
      }
      stack.pop_back();
      if (chosen.empty())
      {
        break;
      }
      const size_t undo = chosen.back();
      chosen.pop_back();
      busy[edges[undo].first] = 0;
      busy[edges[undo].second] = 0;
    }
    if (!advanced)
    {
      break;
    }
  }
  if (!matched)
  {
    return false;
  }
  for (const size_t edge : chosen)
  {
    matchday[edge] = static_cast<uint8_t>(round);
  }
  return true;
}

/** Splits the pairings into rounds where every team plays once. */
std::vector<uint8_t> assignMatchdays(
    size_t teams, const std::vector<std::pair<size_t, size_t>>& edges,
    size_t rounds, std::mt19937& rng)
{
  std::vector<uint8_t> matchday(edges.size(), 0);
  std::vector<std::vector<size_t>> incident(teams);
  for (size_t e = 0; e < edges.size(); ++e)
  {
    incident[edges[e].first].push_back(e);
    incident[edges[e].second].push_back(e);
  }
  for (size_t attempt = 0; attempt < MATCHDAY_ATTEMPTS; ++attempt)
  {
    std::ranges::fill(matchday, 0);
    bool ok = true;
    for (size_t round = 1; round <= rounds && ok; ++round)
    {
      // Perfect matching on the unassigned edges: always extend the team
      // with the fewest options first, random among its options.
      if (!tryAssignMatchdayRound(teams, round, incident, edges, matchday, rng))
      {
        ok = false;
        break;
      }
    }
    if (ok) return matchday;
  }
  // Documented fallback (never observed with the release formats): greedy
  // rounds, anything left over is played in an extra round.
  std::ranges::fill(matchday, 0);
  std::vector busy(rounds + 2, std::vector<uint8_t>(teams, 0));
  for (size_t e = 0; e < edges.size(); ++e)
  {
    for (size_t round = 1; round <= rounds + 1; ++round)
    {
      if (busy[round][edges[e].first] || busy[round][edges[e].second]) continue;
      busy[round][edges[e].first] = 1;
      busy[round][edges[e].second] = 1;
      matchday[e] = static_cast<uint8_t>(round);
      break;
    }
  }
  return matchday;
}

std::string joinNames(const std::vector<std::string>& names)
{
  std::string joined;
  for (const std::string& name : names)
  {
    if (!joined.empty()) joined += ", ";
    joined += name;
  }
  return joined;
}

int64_t roundFee(const Continental::Prizes& prizes, Round round)
{
  using enum Round;
  switch (round)
  {
    case LeaguePhase:
      return prizes.participation;
    case Playoff:
      return prizes.playoff;
    case RoundOf16:
      return prizes.round_of_16;
    case QuarterFinal:
      return prizes.quarter_final;
    case SemiFinal:
      return prizes.semi_final;
    case Final:
      return prizes.final_fee;
  }
  return 0;
}

/** Week index (into SeasonCalendar::continentalWeeks) of a round's first
 * leg. */
size_t knockoutWeek(Round round, Round first_knockout)
{
  using enum Round;
  switch (round)
  {
    case Playoff:
      return first_knockout == RoundOf16 ? 8 : 10;
    case RoundOf16:
      return 10;
    case QuarterFinal:
      return 12;
    default:
      return 14;
  }
}

nlohmann::json coefficientsJson(
    const std::array<double, ContinentalCompetitions::COEFFICIENT_SEASONS>&
        seasons)
{
  nlohmann::json array = nlohmann::json::array();
  for (const double value : seasons) array.push_back(value);
  return array;
}

std::array<double, ContinentalCompetitions::COEFFICIENT_SEASONS>
coefficientsFrom(const nlohmann::json& json)
{
  std::array<double, ContinentalCompetitions::COEFFICIENT_SEASONS> seasons{};
  for (size_t i = 0; i < seasons.size() && i < json.size(); ++i)
    seasons[i] = json[i].get<double>();
  return seasons;
}

using PotEntrant = ContinentalCompetitions::Entrant;

void recordPotPlacement(PotEntrant entrant, size_t pot,
                        std::vector<std::map<LeagueID, size_t>>& counts,
                        std::vector<size_t>& filled,
                        std::vector<PotEntrant>& placed)
{
  ++counts[pot][entrant.association];
  ++filled[pot];
  entrant.pot = static_cast<uint8_t>(pot);
  placed.push_back(entrant);
}

void fillPotsByAssociation(std::vector<PotEntrant>& waiting,
                           std::vector<PotEntrant>& placed,
                           std::vector<std::map<LeagueID, size_t>>& counts,
                           std::vector<size_t>& filled, size_t pot_size,
                           size_t per_association)
{
  for (size_t pot = 0; pot < filled.size(); ++pot)
  {
    for (auto it = waiting.begin();
         it != waiting.end() && filled[pot] < pot_size;)
    {
      if (counts[pot][it->association] >= per_association)
      {
        ++it;
        continue;
      }
      recordPotPlacement(*it, pot, counts, filled, placed);
      it = waiting.erase(it);
    }
  }
}

void placeOverflowEntrants(const std::vector<PotEntrant>& waiting,
                           std::vector<PotEntrant>& placed,
                           std::vector<std::map<LeagueID, size_t>>& counts,
                           std::vector<size_t>& filled, size_t pots,
                           size_t pot_size, size_t per_association)
{
  for (const PotEntrant& entrant : waiting)
  {
    size_t room = pots - 1;
    for (size_t pot = 0; pot < pots; ++pot)
    {
      if (filled[pot] < pot_size)
      {
        room = pot;
        break;
      }
    }
    bool swapped = false;
    for (size_t index = 0; index < placed.size() && !swapped; ++index)
    {
      const PotEntrant other = placed[index];
      const size_t pot = other.pot;
      if (pot == room || other.association == entrant.association ||
          counts[pot][entrant.association] >= per_association ||
          counts[room][other.association] >= per_association)
      {
        continue;
      }
      --counts[pot][other.association];
      ++counts[pot][entrant.association];
      placed[index] = entrant;
      placed[index].pot = static_cast<uint8_t>(pot);
      recordPotPlacement(other, room, counts, filled, placed);
      swapped = true;
    }
    if (!swapped)
    {
      recordPotPlacement(entrant, room, counts, filled, placed);
    }
  }
}
}  // namespace

// ---------------------------------------------------------------------------
// Continental (rules and algorithms)
// ---------------------------------------------------------------------------

const CompetitionRules* Continental::rules(LeagueID competition_id)
{
  for (const CompetitionRules& candidate : COMPETITIONS)
    if (candidate.id == competition_id) return &candidate;
  return nullptr;
}

std::optional<Continental::Continent> Continental::continentOf(
    LeagueID league_id)
{
  const LeagueProfile& profile = leagueProfile(league_id);
  if (profile.league_id != league_id) return std::nullopt;
  switch (profile.region)
  {
    case WorldRegion::Europe:
    case WorldRegion::EasternEurope:
      return Continent::Europe;
    case WorldRegion::NorthAmerica:
    case WorldRegion::SouthAmerica:
      return Continent::Americas;
  }
  return std::nullopt;
}

uint8_t Continental::directPlaces(uint8_t clubs) { return clubs >= 24 ? 8 : 4; }

uint8_t Continental::playoffPlaces(uint8_t clubs)
{
  return clubs >= 24 ? 16 : 8;
}

Round Continental::firstKnockoutRound(uint8_t clubs)
{
  return clubs >= 24 ? Round::RoundOf16 : Round::QuarterFinal;
}

uint8_t Continental::stageCode(Round round, uint8_t leg)
{
  return static_cast<uint8_t>(KNOCKOUT_STAGE_BASE +
                              2 * std::to_underlying(round) + (leg - 1));
}

Round Continental::roundOf(uint8_t stage)
{
  if (stage < KNOCKOUT_STAGE_BASE + 2) return Round::LeaguePhase;
  return static_cast<Round>(std::min<int>((stage - KNOCKOUT_STAGE_BASE) / 2,
                                          std::to_underlying(Round::Final)));
}

uint8_t Continental::legOf(uint8_t stage)
{
  return roundOf(stage) == Round::LeaguePhase
             ? 1
             : static_cast<uint8_t>((stage - KNOCKOUT_STAGE_BASE) % 2 + 1);
}

const char* Continental::roundKey(Round round)
{
  using enum Round;
  switch (round)
  {
    case LeaguePhase:
      return "CONT_ROUND_LEAGUE_PHASE";
    case Playoff:
      return "CONT_ROUND_PLAYOFF";
    case RoundOf16:
      return "CONT_ROUND_OF_16";
    case QuarterFinal:
      return "CONT_ROUND_QUARTER_FINAL";
    case SemiFinal:
      return "CONT_ROUND_SEMI_FINAL";
    case Final:
      return "CONT_ROUND_FINAL";
  }
  return "CONT_ROUND_LEAGUE_PHASE";
}

std::vector<uint8_t> Continental::allocatePlaces(
    const CompetitionRules& rules, size_t associations,
    const std::vector<uint8_t>& capacity)
{
  std::vector<uint8_t> places(associations, 0);
  const auto room = [&](size_t rank)
  { return rank < capacity.size() ? capacity[rank] : uint8_t{0}; };
  size_t total = 0;
  for (size_t rank = 0; rank < associations && total < rules.clubs; ++rank)
  {
    const uint8_t base =
        rules.base_places[std::min(rank, rules.base_places.size() - 1)];
    places[rank] = static_cast<uint8_t>(
        std::min<size_t>({base, room(rank), rules.clubs - total}));
    total += places[rank];
  }
  // Free places go one at a time to the best-ranked associations.
  bool progress = true;
  while (total < rules.clubs && progress)
  {
    progress = false;
    for (size_t rank = 0; rank < associations && total < rules.clubs; ++rank)
    {
      if (places[rank] >= room(rank)) continue;
      ++places[rank];
      ++total;
      progress = true;
    }
  }
  return places;
}

std::vector<Continental::LeagueFixture> Continental::drawLeaguePhase(
    const std::vector<DrawTeam>& teams, uint8_t pots, uint32_t seed)
{
  std::vector<LeagueFixture> fixtures;
  if (pots == 0 || teams.size() % pots != 0) return fixtures;
  const size_t pot_size = teams.size() / pots;
  if (pot_size < 3) return fixtures;

  // Pot-major order, team ID order inside a pot.
  std::vector<DrawTeam> ordered = teams;
  std::ranges::sort(ordered,
                    [](const DrawTeam& left, const DrawTeam& right)
                    {
                      if (left.pot != right.pot) return left.pot < right.pot;
                      return left.team_id < right.team_id;
                    });
  for (size_t index = 0; index < ordered.size(); ++index)
    if (ordered[index].pot != index / pot_size) return fixtures;

  std::vector<DrawSlot> slots;
  for (size_t p = 0; p < pots; ++p)
    for (size_t q = 0; q < pots; ++q)
      for (size_t i = 0; i < pot_size; ++i) slots.emplace_back(p, q, i);

  std::mt19937 rng(seed);
  struct Level
  {
    uint8_t limit;
    bool allow_own;
  };
  constexpr std::array<Level, 4> LEVELS = {{{2, false},
                                            {RELAXED_ASSOCIATION_LIMIT, false},
                                            {NO_ASSOCIATION_LIMIT, false},
                                            {NO_ASSOCIATION_LIMIT, true}}};
  std::optional<DrawState> solved;
  for (const Level& level : LEVELS)
  {
    for (size_t attempt = 0; attempt < DRAW_ATTEMPTS && !solved; ++attempt)
    {
      DrawState state;
      state.pots = pots;
      state.pot_size = pot_size;
      state.limit = level.limit;
      state.allow_own = level.allow_own;
      for (const DrawTeam& team : ordered)
        state.association.push_back(team.association);
      state.met.assign(ordered.size(), std::vector<uint8_t>(ordered.size(), 0));
      state.faced.assign(ordered.size(), {});
      state.host.assign(pots, std::vector<std::vector<int>>(
                                  pots, std::vector<int>(pot_size, -1)));
      state.taken.assign(pots, std::vector<std::vector<uint8_t>>(
                                   pots, std::vector<uint8_t>(pot_size, 0)));
      if (searchPairings(state, slots, rng)) solved = std::move(state);
    }
    if (solved) break;
  }
  if (!solved) return fixtures;

  std::vector<std::pair<size_t, size_t>> edges;
  for (size_t p = 0; p < pots; ++p)
    for (size_t q = 0; q < pots; ++q)
      for (size_t i = 0; i < pot_size; ++i)
        edges.emplace_back(
            solved->index(p, i),
            solved->index(q, static_cast<size_t>(solved->host[p][q][i])));
  const std::vector<uint8_t> matchdays =
      assignMatchdays(ordered.size(), edges, size_t{2} * pots, rng);
  fixtures.reserve(edges.size());
  for (size_t e = 0; e < edges.size(); ++e)
    fixtures.push_back({ordered[edges[e].first].team_id,
                        ordered[edges[e].second].team_id, matchdays[e]});
  std::ranges::sort(fixtures,
                    [](const LeagueFixture& left, const LeagueFixture& right)
                    {
                      if (left.matchday != right.matchday)
                        return left.matchday < right.matchday;
                      return left.home_id < right.home_id;
                    });
  return fixtures;
}

std::vector<StandingRow> Continental::leaguePhaseTable(
    const std::vector<TeamID>& entrants,
    const std::vector<const Match*>& matches,
    const std::function<double(TeamID)>& coefficient)
{
  struct Row
  {
    StandingRow row;
    uint16_t away_goals = 0;
    uint16_t away_wins = 0;
    double coefficient = 0.0;
  };
  std::map<TeamID, Row> rows;
  for (const TeamID team_id : entrants)
  {
    Row& row = rows[team_id];
    row.row.team_id = team_id;
    row.coefficient = coefficient ? coefficient(team_id) : 0.0;
  }
  for (const Match* match : matches)
  {
    if (!match->isPlayed() || roundOf(match->getStage()) != Round::LeaguePhase)
      continue;
    const auto home = rows.find(match->getHomeTeamId());
    const auto away = rows.find(match->getAwayTeamId());
    if (home == rows.end() || away == rows.end()) continue;
    const uint8_t hg = match->getHomeScore();
    const uint8_t ag = match->getAwayScore();
    const auto record =
        [](Row& row, uint8_t scored, uint8_t conceded, bool at_home)
    {
      StandingRow& r = row.row;
      ++r.played;
      r.goals_for = static_cast<uint16_t>(r.goals_for + scored);
      r.goals_against = static_cast<uint16_t>(r.goals_against + conceded);
      if (!at_home)
        row.away_goals = static_cast<uint16_t>(row.away_goals + scored);
      char outcome = 'D';
      if (scored > conceded)
      {
        ++r.won;
        r.points = static_cast<uint16_t>(r.points + Standings::POINTS_FOR_WIN);
        if (!at_home) ++row.away_wins;
        outcome = 'W';
      }
      else if (scored == conceded)
      {
        ++r.drawn;
        r.points = static_cast<uint16_t>(r.points + Standings::POINTS_FOR_DRAW);
      }
      else
      {
        ++r.lost;
        outcome = 'L';
      }
      r.form.push_back(outcome);
      if (r.form.size() > Standings::FORM_LENGTH) r.form.erase(r.form.begin());
    };
    record(home->second, hg, ag, true);
    record(away->second, ag, hg, false);
  }
  std::vector<Row> ordered;
  ordered.reserve(rows.size());
  for (auto& [team_id, row] : rows)
  {
    row.row.goal_difference = static_cast<int32_t>(row.row.goals_for) -
                              static_cast<int32_t>(row.row.goals_against);
    ordered.push_back(row);
  }
  std::ranges::sort(ordered,
                    [](const Row& left, const Row& right)
                    {
                      const StandingRow& l = left.row;
                      const StandingRow& r = right.row;
                      if (l.points != r.points) return l.points > r.points;
                      if (l.goal_difference != r.goal_difference)
                        return l.goal_difference > r.goal_difference;
                      if (l.goals_for != r.goals_for)
                        return l.goals_for > r.goals_for;
                      if (left.away_goals != right.away_goals)
                        return left.away_goals > right.away_goals;
                      if (l.won != r.won) return l.won > r.won;
                      if (left.away_wins != right.away_wins)
                        return left.away_wins > right.away_wins;
                      if (left.coefficient != right.coefficient)
                        return left.coefficient > right.coefficient;
                      return l.team_id < r.team_id;
                    });
  std::vector<StandingRow> table;
  table.reserve(ordered.size());
  for (size_t index = 0; index < ordered.size(); ++index)
  {
    ordered[index].row.position = static_cast<uint16_t>(index + 1);
    table.push_back(std::move(ordered[index].row));
  }
  return table;
}

// ---------------------------------------------------------------------------
// ContinentalCompetitions
// ---------------------------------------------------------------------------

double ContinentalCompetitions::AssociationCoefficient::total() const
{
  return std::accumulate(seasons.begin(), seasons.end(), 0.0);
}

ContinentalCompetitions::ContinentalCompetitions(
    std::shared_ptr<GameData> game_data)
    : gamedata(std::move(game_data))
{
}

ContinentalCompetitions::Season* ContinentalCompetitions::findSeason(
    LeagueID competition_id)
{
  const auto it =
      std::ranges::find(seasons, competition_id, &Season::competition_id);
  return it == seasons.end() ? nullptr : std::to_address(it);
}

const ContinentalCompetitions::Season* ContinentalCompetitions::getSeason(
    LeagueID competition_id) const
{
  const auto it =
      std::ranges::find(seasons, competition_id, &Season::competition_id);
  return it == seasons.end() ? nullptr : std::to_address(it);
}

const std::vector<StandingRow>& ContinentalCompetitions::getTable(
    LeagueID competition_id) const
{
  static const std::vector<StandingRow> empty;
  const auto it = tables.find(competition_id);
  return it == tables.end() ? empty : it->second;
}

std::optional<LeagueID> ContinentalCompetitions::competitionOf(
    TeamID team_id) const
{
  for (const Season& season : seasons)
    if (std::ranges::contains(season.entrants, team_id, &Entrant::team_id))
      return season.competition_id;
  return std::nullopt;
}

std::string ContinentalCompetitions::teamName(TeamID team_id) const
{
  const auto team = gamedata->getTeam(team_id);
  return team ? team->get().getName() : std::string();
}

ContinentalCompetitions::AssociationCoefficient
ContinentalCompetitions::associationCoefficient(LeagueID association) const
{
  if (const auto it = associations.find(association); it != associations.end())
    return it->second;
  AssociationCoefficient prior;
  prior.association = association;
  const double reputation = leagueProfile(association).reputation;
  prior.seasons.fill(
      std::max(ASSOCIATION_PRIOR_FLOOR,
               (reputation - PRIOR_REPUTATION_BASE) * ASSOCIATION_PRIOR_SLOPE));
  return prior;
}

double ContinentalCompetitions::clubCoefficient(TeamID team_id) const
{
  const auto team = gamedata->getTeam(team_id);
  if (!team) return 0.0;
  double own = 0.0;
  if (const auto it = clubs.find(team_id); it != clubs.end())
  {
    own = std::accumulate(it->second.begin(), it->second.end(), 0.0);
  }
  else
  {
    own = static_cast<double>(COEFFICIENT_SEASONS) *
          std::max(0.0, (team->get().getReputation() - PRIOR_REPUTATION_BASE) *
                            CLUB_PRIOR_SLOPE);
  }
  const LeagueID association =
      Competitions::rootLeague(*gamedata, team->get().getLeagueId());
  return std::max(
      own, ASSOCIATION_SHARE * associationCoefficient(association).total());
}

std::vector<LeagueID> ContinentalCompetitions::rankedAssociations(
    Continental::Continent continent) const
{
  std::vector<std::pair<double, LeagueID>> ranked;
  for (const LeagueID root : Competitions::countryRoots(*gamedata))
  {
    if (Continental::continentOf(root) != continent) continue;
    ranked.emplace_back(associationCoefficient(root).total(), root);
  }
  std::ranges::sort(
      ranked,
      [](const auto& left, const auto& right)
      {
        if (left.first != right.first) return left.first > right.first;
        const auto rep_left = leagueProfile(left.second).reputation;
        if (const auto rep_right = leagueProfile(right.second).reputation;
            rep_left != rep_right)
          return rep_left > rep_right;
        return left.second < right.second;
      });
  std::vector<LeagueID> order;
  order.reserve(ranked.size());
  for (const auto& [value, root] : ranked) order.push_back(root);
  return order;
}

std::vector<ContinentalCompetitions::AssociationCoefficient>
ContinentalCompetitions::associationRanking(
    Continental::Continent continent) const
{
  std::vector<AssociationCoefficient> ranking;
  for (const LeagueID root : rankedAssociations(continent))
    ranking.push_back(associationCoefficient(root));
  return ranking;
}

std::vector<TeamID> ContinentalCompetitions::topDivisionClubs(
    LeagueID association) const
{
  const auto league = gamedata->getLeague(association);
  if (!league) return {};
  std::vector<TeamID> teams = league->get().getTeamIDs();
  std::ranges::sort(teams,
                    [this](TeamID left, TeamID right)
                    {
                      const auto rep_left =
                          gamedata->getTeam(left)->get().getReputation();
                      if (const auto rep_right =
                              gamedata->getTeam(right)->get().getReputation();
                          rep_left != rep_right)
                        return rep_left > rep_right;
                      return left < right;
                    });
  return teams;
}

bool ContinentalCompetitions::isLowestTier(const CompetitionRules& rules) const
{
  bool lower_exists = false;
  bool higher_exists = false;
  for (const CompetitionRules& other : Continental::COMPETITIONS)
  {
    if (other.continent != rules.continent || other.id == rules.id) continue;
    if (other.tier > rules.tier) lower_exists = true;
    if (other.tier < rules.tier) higher_exists = true;
  }
  return !lower_exists && higher_exists;
}

std::vector<ContinentalCompetitions::Entrant>
ContinentalCompetitions::qualifyByReputation(const CompetitionRules& rules,
                                             std::vector<TeamID>& taken) const
{
  std::unordered_map<LeagueID, std::vector<StandingRow>> by_reputation;
  for (const LeagueID root : rankedAssociations(rules.continent))
  {
    auto& rows = by_reputation[root];
    for (const TeamID team_id : topDivisionClubs(root))
    {
      StandingRow row;
      row.team_id = team_id;
      rows.push_back(row);
    }
  }
  std::vector<Entrant> entrants = qualify(rules, by_reputation, {}, taken);
  for (Entrant& entrant : entrants) entrant.league_position = 0;
  return entrants;
}

std::vector<ContinentalCompetitions::Entrant> ContinentalCompetitions::qualify(
    const CompetitionRules& rules,
    const std::unordered_map<LeagueID, std::vector<StandingRow>>& final_tables,
    const std::map<LeagueID, TeamID>& cup_winners,
    std::vector<TeamID>& taken) const
{
  const std::vector<LeagueID> ranked = rankedAssociations(rules.continent);
  std::vector<uint8_t> capacity;
  for (const LeagueID root : ranked)
  {
    size_t free = 0;
    if (const auto table = final_tables.find(root); table != final_tables.end())
      for (const StandingRow& row : table->second)
        if (!std::ranges::contains(taken, row.team_id)) ++free;
    capacity.push_back(static_cast<uint8_t>(std::min<size_t>(free, 0xFF)));
  }
  const std::vector<uint8_t> places =
      Continental::allocatePlaces(rules, ranked.size(), capacity);
  const bool cup_route = isLowestTier(rules);

  std::vector<Entrant> entrants;
  for (size_t rank = 0; rank < ranked.size(); ++rank)
  {
    const LeagueID root = ranked[rank];
    size_t wanted = places[rank];
    const auto add = [&](TeamID team_id, uint8_t position, bool cup)
    {
      Entrant entrant;
      entrant.team_id = team_id;
      entrant.association = root;
      entrant.league_position = position;
      entrant.cup_winner = cup;
      entrants.push_back(entrant);
      taken.push_back(team_id);
      --wanted;
    };
    // The cup winner takes the association's first place here unless it
    // already qualified; otherwise the place passes down the table.
    if (cup_route && wanted > 0)
    {
      if (const auto cup = cup_winners.find(root);
          cup != cup_winners.end() && cup->second != 0 &&
          gamedata->getTeam(cup->second) &&
          !std::ranges::contains(taken, cup->second))
        add(cup->second, 0, true);
    }
    const auto table = final_tables.find(root);
    if (table == final_tables.end()) continue;
    for (size_t index = 0; index < table->second.size() && wanted > 0; ++index)
    {
      const TeamID team_id = table->second[index].team_id;
      if (std::ranges::contains(taken, team_id) || !gamedata->getTeam(team_id))
        continue;
      add(team_id, static_cast<uint8_t>(index + 1), false);
    }
  }
  return entrants;
}

void ContinentalCompetitions::assignPots(Season& season) const
{
  for (Entrant& entrant : season.entrants)
    entrant.coefficient = clubCoefficient(entrant.team_id);
  std::ranges::sort(season.entrants,
                    [](const Entrant& left, const Entrant& right)
                    {
                      if (left.coefficient != right.coefficient)
                        return left.coefficient > right.coefficient;
                      return left.team_id < right.team_id;
                    });
  const size_t pots = std::max<size_t>(1, season.matches / 2);
  const size_t pot_size = std::max<size_t>(1, season.entrants.size() / pots);
  // Clubs meet two pot-mates each and never one of their association, so a
  // pot, the last one included, holds at most half its clubs from one
  // association; the others move down to the next pot with room
  // (coefficient order otherwise).
  const size_t per_association = std::max<size_t>(1, pot_size / 2);
  std::vector<std::map<LeagueID, size_t>> counts(pots);
  std::vector<size_t> filled(pots, 0);
  std::vector<Entrant> waiting = season.entrants;
  std::vector<Entrant> placed;
  placed.reserve(season.entrants.size());
  fillPotsByAssociation(waiting, placed, counts, filled, pot_size,
                        per_association);
  // A club no pot with room could take (its association is full there)
  // swaps with a club of another association in a full pot that still has
  // room for it; that club moves to the pot with room.
  placeOverflowEntrants(waiting, placed, counts, filled, pots, pot_size,
                        per_association);
  std::ranges::sort(placed,
                    [](const Entrant& left, const Entrant& right)
                    {
                      if (left.pot != right.pot) return left.pot < right.pot;
                      if (left.coefficient != right.coefficient)
                        return left.coefficient > right.coefficient;
                      return left.team_id < right.team_id;
                    });
  season.entrants = std::move(placed);
}

void ContinentalCompetitions::startSeason(uint16_t season_year,
                                          const GameDateValue& today)
{
  if (std::ranges::contains(seasons, season_year, &Season::season_year)) return;
  if (const std::vector<GameDateValue> weeks =
          SeasonCalendar::continentalWeeks(season_year);
      weeks.empty() || !(today < plusDays(weeks.front(), -LATEST_START_DAYS)))
    return;
  std::erase_if(seasons, [season_year](const Season& season)
                { return season.season_year < season_year; });
  tables.clear();

  const bool computed = qualified_season == season_year;
  std::vector<TeamID> taken;
  const GameDateValue earliest(season_year, 8, DRAW_EARLIEST_DAY);
  GameDateValue draw_date = earliest;
  while (SeasonCalendar::dayOfWeek(draw_date) != 4)
    draw_date = plusDays(draw_date, 1);
  if (!(today < draw_date)) draw_date = plusDays(today, 1);

  for (const CompetitionRules& rules : Continental::COMPETITIONS)
  {
    std::vector<Entrant> entrants;
    if (computed)
    {
      if (const auto it = qualified.find(rules.id); it != qualified.end())
        entrants = it->second;
    }
    else
    {
      entrants = qualifyByReputation(rules, taken);
    }
    // Too few clubs for the full format: scale it down (36/8 -> 24/6 ->
    // 12/4) rather than invent clubs.
    uint8_t size = rules.clubs;
    uint8_t matches = rules.matches;
    while (entrants.size() < size && size > MIN_CLUBS)
    {
      size = size > 24 ? 24 : MIN_CLUBS;
      matches = size == 24 ? 6 : 4;
    }
    if (entrants.size() < size) continue;
    Season season;
    season.competition_id = rules.id;
    season.season_year = season_year;
    season.clubs = size;
    season.matches = matches;
    season.draw_date = draw_date;
    season.entrants = std::move(entrants);
    assignPots(season);
    // The lowest coefficients drop out, then the pots are drawn up again.
    std::ranges::sort(season.entrants,
                      [](const Entrant& left, const Entrant& right)
                      {
                        if (left.coefficient != right.coefficient)
                          return left.coefficient > right.coefficient;
                        return left.team_id < right.team_id;
                      });
    season.entrants.resize(size);
    assignPots(season);
    seasons.push_back(std::move(season));
  }
  results_dirty = true;
}

std::vector<const Match*> ContinentalCompetitions::competitionMatches(
    const Calendar& calendar, LeagueID competition_id) const
{
  std::vector<const Match*> matches;
  for (const auto& [date, day] : calendar.getFullCalendar())
    for (const Match& match : day)
      if (match.getMatchType() == MatchType::CONTINENTAL &&
          match.getCompetitionId() == competition_id)
        matches.push_back(&match);
  return matches;
}

const Match* ContinentalCompetitions::findLeg(const Calendar& calendar,
                                              LeagueID competition_id,
                                              uint8_t stage, TeamID home_id,
                                              TeamID away_id) const
{
  for (const auto& [date, day] : calendar.getFullCalendar())
    for (const Match& match : day)
      if (match.getMatchType() == MatchType::CONTINENTAL &&
          match.getCompetitionId() == competition_id &&
          match.getStage() == stage && match.getHomeTeamId() == home_id &&
          match.getAwayTeamId() == away_id)
        return &match;
  return nullptr;
}

void ContinentalCompetitions::payPrize(TeamID team_id,
                                       const GameDateValue& date,
                                       int64_t amount)
{
  if (amount <= 0) return;
  if (auto team = gamedata->getTeam(team_id))
    team->get().getFinances().record(date, FinanceCategory::PrizeMoney, amount);
}

void ContinentalCompetitions::postNews(const GameDateValue& date,
                                       std::string title_key,
                                       std::string body_key,
                                       std::vector<std::string> args,
                                       const std::vector<TeamID>& involved,
                                       bool headline) const
{
  if (!news) return;
  InboxMessage message;
  message.date = date;
  message.category = InboxCategory::Match;
  message.title_key = std::move(title_key);
  message.body_key = std::move(body_key);
  message.args = std::move(args);
  message.read = headline;
  news(std::move(message), involved, headline);
}

void ContinentalCompetitions::drawLeaguePhase(Calendar& calendar,
                                              Season& season,
                                              const GameDateValue& today)
{
  const CompetitionRules* rules = Continental::rules(season.competition_id);
  if (!rules) return;
  std::vector<Continental::DrawTeam> teams;
  teams.reserve(season.entrants.size());
  for (const Entrant& entrant : season.entrants)
    teams.push_back({entrant.team_id, entrant.association, entrant.pot});
  const auto pots = static_cast<uint8_t>(season.matches / 2);
  const auto draw = [&]
  {
    return Continental::drawLeaguePhase(
        teams, pots,
        Competitions::mixSeed(season.season_year, season.competition_id,
                              LEAGUE_DRAW_SALT));
  };
  std::vector<Continental::LeagueFixture> fixtures = draw();
  if (fixtures.empty() && pots > 0)
  {
    // Pots the association rule could not fill evenly: draw from pots by
    // coefficient alone (the entrants are kept in coefficient order).
    const size_t pot_size = std::max<size_t>(1, season.entrants.size() / pots);
    teams.clear();
    for (size_t index = 0; index < season.entrants.size(); ++index)
    {
      Entrant& entrant = season.entrants[index];
      entrant.pot =
          static_cast<uint8_t>(std::min<size_t>(index / pot_size, pots - 1u));
      teams.push_back({entrant.team_id, entrant.association, entrant.pot});
    }
    fixtures = draw();
  }
  // Without fixtures the season is not drawn (and no fee is paid), so the
  // competition never waits for matches that do not exist.
  if (fixtures.empty()) return;
  const std::vector<GameDateValue> weeks =
      SeasonCalendar::continentalWeeks(season.season_year);
  const size_t last_week = Continental::KNOCKOUT_STAGE_BASE / 2 - 1;
  std::map<uint8_t, size_t> per_matchday;
  // Domestic fixtures already in the calendar (normally none in these
  // weeks; older saves generated their calendar before them).
  std::set<std::pair<TeamID, int32_t>> busy;
  for (const auto& [date, matches] : calendar.getFullCalendar())
    for (const Match& match : matches)
      if (match.getMatchType() != MatchType::CONTINENTAL)
      {
        busy.emplace(match.getHomeTeamId(), dayOrdinal(date));
        busy.emplace(match.getAwayTeamId(), dayOrdinal(date));
      }
  const auto clear =
      [&busy](TeamID home, TeamID away, const GameDateValue& date)
  {
    const int32_t day = dayOrdinal(date);
    for (int32_t near = day - 1; near <= day + 1; ++near)
      if (busy.contains({home, near}) || busy.contains({away, near}))
        return false;
    return true;
  };
  for (const Continental::LeagueFixture& fixture : fixtures)
  {
    // Matchdays spread over the eight league-phase weeks.
    // Matchday 0 (left over by the fallback schedule) joins the extra round.
    const uint8_t matchday = fixture.matchday == 0
                                 ? static_cast<uint8_t>(season.matches + 1u)
                                 : fixture.matchday;
    const bool extra_round = matchday > season.matches;
    const size_t week =
        season.matches > 1 && !extra_round
            ? (size_t{matchday - 1u} * last_week + (season.matches - 1u) / 2) /
                  (season.matches - 1u)
            : last_week;
    const size_t half = per_matchday[matchday]++ % 2;
    int offset = rules->weekday_offsets[half];
    if (extra_round) offset = 3;
    for (const int candidate : {offset, 0, 1, 2})
    {
      if (clear(fixture.home_id, fixture.away_id,
                plusDays(weeks[week], candidate)))
      {
        offset = candidate;
        break;
      }
    }
    calendar.addMatch(
        Match(fixture.home_id, fixture.away_id, plusDays(weeks[week], offset),
              MatchType::CONTINENTAL, season.competition_id, matchday));
  }
  season.drawn = true;
  season.draws.push_back({today, Round::LeaguePhase});
  for (const Entrant& entrant : season.entrants)
    payPrize(entrant.team_id, today, rules->prizes.participation);

  const std::string competition = std::string("@") + rules->name_key;
  for (const Entrant& entrant : season.entrants)
  {
    std::vector<std::string> home;
    std::vector<std::string> away;
    for (const Continental::LeagueFixture& fixture : fixtures)
    {
      if (fixture.home_id == entrant.team_id)
        home.push_back(teamName(fixture.away_id));
      else if (fixture.away_id == entrant.team_id)
        away.push_back(teamName(fixture.home_id));
    }
    postNews(today, "INBOX_CONT_DRAW_TITLE", "INBOX_CONT_DRAW_BODY",
             {competition, teamName(entrant.team_id), joinNames(home),
              joinNames(away), plusDays(weeks.front(), 0).toString()},
             {entrant.team_id}, false);
  }
  results_dirty = true;
}

ContinentalCompetitions::TieScore ContinentalCompetitions::tieScore(
    const Calendar& calendar, const Season& season, const Tie& tie) const
{
  TieScore score;
  if (tie.round == Round::Final)
  {
    score.second_leg = findLeg(calendar, season.competition_id,
                               Continental::stageCode(Round::Final, 1),
                               tie.seeded_id, tie.unseeded_id);
    if (score.second_leg && score.second_leg->isPlayed())
    {
      score.seeded_goals = score.second_leg->getHomeScore();
      score.unseeded_goals = score.second_leg->getAwayScore();
    }
    return score;
  }
  score.first_leg = findLeg(calendar, season.competition_id,
                            Continental::stageCode(tie.round, 1),
                            tie.unseeded_id, tie.seeded_id);
  score.second_leg = findLeg(calendar, season.competition_id,
                             Continental::stageCode(tie.round, 2),
                             tie.seeded_id, tie.unseeded_id);
  if (score.first_leg && score.first_leg->isPlayed())
  {
    score.seeded_goals = score.first_leg->getAwayScore();
    score.unseeded_goals = score.first_leg->getHomeScore();
  }
  if (score.second_leg && score.second_leg->isPlayed())
  {
    score.seeded_goals = static_cast<uint16_t>(
        score.seeded_goals + score.second_leg->getHomeScore());
    score.unseeded_goals = static_cast<uint16_t>(
        score.unseeded_goals + score.second_leg->getAwayScore());
  }
  return score;
}

void ContinentalCompetitions::onResult(const Match& match)
{
  if (match.getMatchType() != MatchType::CONTINENTAL || !match.isPlayed())
    return;
  results_dirty = true;
  const CompetitionRules* rules = Continental::rules(match.getCompetitionId());
  if (!rules || Continental::roundOf(match.getStage()) != Round::LeaguePhase)
    return;
  const GameDateValue& date = match.getDate();
  if (match.getHomeScore() == match.getAwayScore())
  {
    payPrize(match.getHomeTeamId(), date, rules->prizes.draw);
    payPrize(match.getAwayTeamId(), date, rules->prizes.draw);
  }
  else if (const auto winner = match.getWinnerId(); winner.has_value())
  {
    payPrize(*winner, date, rules->prizes.win);
  }
}

bool ContinentalCompetitions::needsExtraTime(const Calendar& calendar,
                                             const Match& match) const
{
  if (!match.isPlayed() || match.wentToExtraTime()) return false;
  const std::optional<int> lead = deciderLead(calendar, match);
  return lead && match.getHomeScore() + *lead == match.getAwayScore();
}

std::optional<int> ContinentalCompetitions::deciderLead(
    const Calendar& calendar, const Match& match) const
{
  const auto aggregate = deciderAggregate(calendar, match);
  if (!aggregate) return std::nullopt;
  return aggregate->first - aggregate->second;
}

std::optional<std::pair<int, int>> ContinentalCompetitions::deciderAggregate(
    const Calendar& calendar, const Match& match) const
{
  if (match.getMatchType() != MatchType::CONTINENTAL) return std::nullopt;
  const Round round = Continental::roundOf(match.getStage());
  if (round == Round::LeaguePhase) return std::nullopt;
  if (round == Round::Final) return std::pair(0, 0);
  if (Continental::legOf(match.getStage()) != 2) return std::nullopt;
  // The home side of the second leg was the away side of the first.
  const Match* first = findLeg(calendar, match.getCompetitionId(),
                               Continental::stageCode(round, 1),
                               match.getAwayTeamId(), match.getHomeTeamId());
  if (!first || !first->isPlayed()) return std::nullopt;
  return std::pair(static_cast<int>(first->getAwayScore()),
                   static_cast<int>(first->getHomeScore()));
}

bool ContinentalCompetitions::resolveDecider(const Calendar& calendar,
                                             Match& match)
{
  if (!needsExtraTime(calendar, match)) return false;
  const auto home = gamedata->getTeam(match.getHomeTeamId());
  const auto away = gamedata->getTeam(match.getAwayTeamId());
  if (!home || !away) return false;
  const Competitions::KnockoutResolution resolution =
      Competitions::resolveDrawnKnockout(home->get(), away->get(),
                                         gamedata->getStatsConfig(),
                                         match.getSeed());
  std::optional<std::pair<uint8_t, uint8_t>> shootout;
  if (resolution.penalties)
    shootout.emplace(resolution.home_penalties, resolution.away_penalties);
  match.setKnockoutResult(
      static_cast<uint8_t>(match.getHomeScore() + resolution.home_extra_goals),
      static_cast<uint8_t>(match.getAwayScore() + resolution.away_extra_goals),
      true, shootout);
  results_dirty = true;
  return true;
}

void ContinentalCompetitions::afterMatchday(Calendar& calendar,
                                            const GameDateValue& today)
{
  bool drew = false;
  for (Season& season : seasons)
  {
    if (!season.drawn && !(today < season.draw_date))
    {
      drawLeaguePhase(calendar, season, today);
      drew = true;
    }
  }
  if (!results_dirty && !drew) return;
  results_dirty = false;
  refresh(calendar);
  for (Season& season : seasons)
    if (season.drawn && season.winner_id == 0)
      advanceKnockouts(calendar, season, today);
}

void ContinentalCompetitions::refresh(const Calendar& calendar)
{
  tables.clear();
  for (const Season& season : seasons)
  {
    if (!season.drawn) continue;
    std::vector<TeamID> ids;
    std::map<TeamID, double> coefficients;
    for (const Entrant& entrant : season.entrants)
    {
      ids.push_back(entrant.team_id);
      coefficients[entrant.team_id] = entrant.coefficient;
    }
    tables[season.competition_id] = Continental::leaguePhaseTable(
        ids, competitionMatches(calendar, season.competition_id),
        [&coefficients](TeamID team_id) { return coefficients[team_id]; });
  }
}

void ContinentalCompetitions::advanceKnockouts(Calendar& calendar,
                                               Season& season,
                                               const GameDateValue& today)
{
  const CompetitionRules* rules = Continental::rules(season.competition_id);
  if (!rules) return;
  const std::vector<StandingRow>& table = getTable(season.competition_id);
  const std::string competition = std::string("@") + rules->name_key;

  if (!season.league_phase_complete)
  {
    const std::vector<const Match*> matches =
        competitionMatches(calendar, season.competition_id);
    const size_t expected = size_t{season.clubs} * season.matches / 2;
    if (const auto played = static_cast<size_t>(std::ranges::count_if(
            matches,
            [](const Match* match)
            {
              return Continental::roundOf(match->getStage()) ==
                         Round::LeaguePhase &&
                     match->isPlayed();
            }));
        played < expected || table.size() < season.clubs)
      return;
    season.league_phase_complete = true;
    const uint8_t direct = Continental::directPlaces(season.clubs);
    const uint8_t playoff = Continental::playoffPlaces(season.clubs);
    for (const StandingRow& row : table)
    {
      payPrize(row.team_id, today,
               rules->prizes.ranking_step * (season.clubs - row.position + 1));
      const char* outcome = "CONT_OUTCOME_OUT";
      if (row.position <= direct)
        outcome = "CONT_OUTCOME_DIRECT";
      else if (row.position <= direct + playoff)
        outcome = "CONT_OUTCOME_PLAYOFF";
      postNews(today, "INBOX_CONT_LEAGUE_END_TITLE",
               "INBOX_CONT_LEAGUE_END_BODY",
               {competition, teamName(row.team_id),
                std::to_string(row.position), std::string("@") + outcome},
               {row.team_id}, false);
    }
    std::vector<TeamID> seeded;
    std::vector<TeamID> unseeded;
    for (size_t index = direct; index < size_t{direct} + playoff; ++index)
      (index < size_t{direct} + playoff / 2 ? seeded : unseeded)
          .push_back(table[index].team_id);
    drawKnockoutRound(calendar, season, Round::Playoff, std::move(seeded),
                      std::move(unseeded), today);
    return;
  }

  for (Tie& tie : season.ties)
  {
    if (tie.winner_id != 0) continue;
    const TieScore score = tieScore(calendar, season, tie);
    if (!score.second_leg || !score.second_leg->isPlayed()) continue;
    if (tie.round != Round::Final &&
        (!score.first_leg || !score.first_leg->isPlayed()))
      continue;
    if (score.seeded_goals != score.unseeded_goals)
    {
      tie.winner_id = score.seeded_goals > score.unseeded_goals
                          ? tie.seeded_id
                          : tie.unseeded_id;
    }
    else if (score.second_leg->wentToPenalties() &&
             score.second_leg->getHomePenalties() !=
                 score.second_leg->getAwayPenalties())
    {
      // The deciding match is hosted by the seeded club (also the final's
      // nominal home side); the leg score itself may differ.
      tie.winner_id = score.second_leg->getHomePenalties() >
                              score.second_leg->getAwayPenalties()
                          ? tie.seeded_id
                          : tie.unseeded_id;
    }
  }
  if (season.ties.empty()) return;
  const Round latest = season.ties.back().round;
  std::vector<TeamID> winners;
  for (const Tie& tie : season.ties)
  {
    if (tie.round != latest) continue;
    if (tie.winner_id == 0) return;
    winners.push_back(tie.winner_id);
  }

  if (latest == Round::Final)
  {
    const Tie& final_tie = season.ties.back();
    season.winner_id = final_tie.winner_id;
    season.runner_up_id = final_tie.winner_id == final_tie.seeded_id
                              ? final_tie.unseeded_id
                              : final_tie.seeded_id;
    payPrize(season.winner_id, today, rules->prizes.winner_bonus);
    postNews(today, "INBOX_CONT_WINNER_TITLE", "INBOX_CONT_WINNER_BODY",
             {competition, teamName(season.winner_id),
              teamName(season.runner_up_id)},
             {season.winner_id, season.runner_up_id}, true);
    return;
  }

  const auto position = [&table](TeamID team_id)
  {
    const auto it = std::ranges::find(table, team_id, &StandingRow::team_id);
    return it == table.end() ? uint16_t{0xFFFF} : it->position;
  };
  std::ranges::sort(winners, [&position](TeamID left, TeamID right)
                    { return position(left) < position(right); });
  if (latest == Round::Playoff)
  {
    std::vector<TeamID> seeded;
    for (size_t index = 0; index < Continental::directPlaces(season.clubs) &&
                           index < table.size();
         ++index)
      seeded.push_back(table[index].team_id);
    drawKnockoutRound(calendar, season,
                      Continental::firstKnockoutRound(season.clubs),
                      std::move(seeded), std::move(winners), today);
    return;
  }
  const auto next = static_cast<Round>(std::to_underlying(latest) + 1);
  drawKnockoutRound(calendar, season, next, std::move(winners), {}, today);
}

void ContinentalCompetitions::drawKnockoutRound(Calendar& calendar,
                                                Season& season, Round round,
                                                std::vector<TeamID> seeded,
                                                std::vector<TeamID> unseeded,
                                                const GameDateValue& today)
{
  const CompetitionRules* rules = Continental::rules(season.competition_id);
  if (!rules) return;
  std::mt19937 rng(
      Competitions::mixSeed(season.season_year, season.competition_id,
                            KNOCKOUT_DRAW_SALT + std::to_underlying(round)));
  const auto association = [this](TeamID team_id)
  {
    const auto team = gamedata->getTeam(team_id);
    return team ? Competitions::rootLeague(*gamedata, team->get().getLeagueId())
                : LeagueID{0};
  };
  const auto& table = getTable(season.competition_id);
  const auto position = [&table](TeamID team_id)
  {
    const auto it = std::ranges::find(table, team_id, &StandingRow::team_id);
    return it == table.end() ? uint16_t{0xFFFF} : it->position;
  };

  std::vector<std::pair<TeamID, TeamID>> pairs;  // (seeded, unseeded)
  if (!unseeded.empty())
  {
    // Seeded against unseeded, avoiding clubs of the same association
    // when a draw allows it.
    constexpr int ASSOCIATION_TRIES = 64;
    std::vector<TeamID> best = unseeded;
    size_t best_clashes = SIZE_MAX;
    for (int attempt = 0; attempt < ASSOCIATION_TRIES && best_clashes > 0;
         ++attempt)
    {
      std::ranges::shuffle(unseeded, rng);
      size_t clashes = 0;
      for (size_t i = 0; i < seeded.size() && i < unseeded.size(); ++i)
        if (association(seeded[i]) == association(unseeded[i])) ++clashes;
      if (clashes < best_clashes)
      {
        best_clashes = clashes;
        best = unseeded;
      }
    }
    for (size_t i = 0; i < seeded.size() && i < best.size(); ++i)
      pairs.emplace_back(seeded[i], best[i]);
  }
  else
  {
    // Open draw; the club that finished higher plays the second leg at home.
    std::ranges::shuffle(seeded, rng);
    for (size_t i = 0; i + 1 < seeded.size(); i += 2)
    {
      TeamID first = seeded[i];
      TeamID second = seeded[i + 1];
      if (position(second) < position(first)) std::swap(first, second);
      pairs.emplace_back(first, second);
    }
  }

  const std::vector<GameDateValue> weeks =
      SeasonCalendar::continentalWeeks(season.season_year);
  const Round first_knockout = Continental::firstKnockoutRound(season.clubs);
  GameDateValue first_date;
  GameDateValue second_date;
  const std::string competition = std::string("@") + rules->name_key;
  for (size_t index = 0; index < pairs.size(); ++index)
  {
    const auto [seeded_id, unseeded_id] = pairs[index];
    Tie tie;
    tie.round = round;
    tie.seeded_id = seeded_id;
    tie.unseeded_id = unseeded_id;
    season.ties.push_back(tie);
    if (round == Round::Final)
    {
      first_date = plusDays(SeasonCalendar::leagueEnd(season.season_year),
                            rules->final_offset);
      if (!(today < first_date)) first_date = plusDays(today, 3);
      second_date = first_date;
      calendar.addMatch(Match(seeded_id, unseeded_id, first_date,
                              MatchType::CONTINENTAL, season.competition_id,
                              Continental::stageCode(Round::Final, 1)));
    }
    else
    {
      const size_t week = knockoutWeek(round, first_knockout);
      const int offset = rules->weekday_offsets[index % 2];
      first_date = plusDays(weeks[week], offset);
      second_date = plusDays(weeks[week + 1], offset);
      // A late draw (older saves) keeps the one-week gap after today.
      while (!(today < first_date))
      {
        first_date = plusDays(first_date, 7);
        second_date = plusDays(second_date, 7);
      }
      calendar.addMatch(Match(unseeded_id, seeded_id, first_date,
                              MatchType::CONTINENTAL, season.competition_id,
                              Continental::stageCode(round, 1)));
      calendar.addMatch(Match(seeded_id, unseeded_id, second_date,
                              MatchType::CONTINENTAL, season.competition_id,
                              Continental::stageCode(round, 2)));
    }
    payPrize(seeded_id, today, roundFee(rules->prizes, round));
    payPrize(unseeded_id, today, roundFee(rules->prizes, round));
    for (const auto& [team_id, opponent_id] :
         {std::pair{seeded_id, unseeded_id}, std::pair{unseeded_id, seeded_id}})
    {
      postNews(today,
               round == Round::Final ? "INBOX_CONT_FINAL_TITLE"
                                     : "INBOX_CONT_KO_DRAW_TITLE",
               round == Round::Final ? "INBOX_CONT_FINAL_BODY"
                                     : "INBOX_CONT_KO_DRAW_BODY",
               {competition, std::string("@") + Continental::roundKey(round),
                teamName(team_id), teamName(opponent_id), first_date.toString(),
                second_date.toString()},
               {team_id}, false);
    }
  }
  season.draws.push_back({today, round});
  results_dirty = true;
}

void ContinentalCompetitions::closeSeason(
    const Calendar& calendar, uint16_t season_year,
    const std::unordered_map<LeagueID, std::vector<StandingRow>>& final_tables,
    const std::map<LeagueID, TeamID>& cup_winners)
{
  if (last_closed_season >= season_year) return;
  last_closed_season = season_year;
  refresh(calendar);

  // Coefficient points: 2 per win, 1 per draw (shoot-outs count as draws),
  // plus participation, ranking, knockout and round bonuses.
  std::map<TeamID, double> points;
  std::map<LeagueID, double> association_points;
  std::map<LeagueID, int> association_clubs;
  for (const Season& season : seasons)
  {
    if (season.season_year != season_year || !season.drawn) continue;
    const CompetitionRules* rules = Continental::rules(season.competition_id);
    if (!rules) continue;
    for (const Match* match :
         competitionMatches(calendar, season.competition_id))
    {
      if (!match->isPlayed()) continue;
      const int home = match->getHomeScore();
      const int away = match->getAwayScore();
      if (home == away)
      {
        points[match->getHomeTeamId()] += 1.0;
        points[match->getAwayTeamId()] += 1.0;
      }
      else
      {
        points[home > away ? match->getHomeTeamId() : match->getAwayTeamId()] +=
            2.0;
      }
    }
    const auto& table = getTable(season.competition_id);
    for (const StandingRow& row : table)
    {
      points[row.team_id] +=
          rules->participation_bonus +
          (season.clubs > 1
               ? rules->ranking_bonus * (season.clubs - row.position) /
                     (season.clubs - 1.0)
               : 0.0);
    }
    const Round first_knockout = Continental::firstKnockoutRound(season.clubs);
    std::map<TeamID, Round> furthest;
    for (const Tie& tie : season.ties)
    {
      if (tie.round == Round::Playoff) continue;
      for (const TeamID team_id : {tie.seeded_id, tie.unseeded_id})
        furthest[team_id] = std::max(furthest[team_id], tie.round);
    }
    for (const auto& [team_id, round] : furthest)
    {
      points[team_id] += rules->knockout_bonus +
                         ROUND_BONUS * (std::to_underlying(round) -
                                        std::to_underlying(first_knockout));
    }
    if (season.winner_id != 0) points[season.winner_id] += ROUND_BONUS;
    for (const Entrant& entrant : season.entrants)
    {
      association_points[entrant.association] += points[entrant.team_id];
      ++association_clubs[entrant.association];
    }
  }

  const auto shift =
      [](std::array<double, COEFFICIENT_SEASONS>& history, double value)
  {
    std::shift_right(history.begin(), history.end(), 1);
    history.front() = value;
  };
  for (const LeagueID root : Competitions::countryRoots(*gamedata))
  {
    if (!Continental::continentOf(root)) continue;
    AssociationCoefficient coefficient = associationCoefficient(root);
    const int entered = association_clubs[root];
    shift(coefficient.seasons,
          entered > 0 ? association_points[root] / entered : 0.0);
    associations[root] = coefficient;
  }
  std::vector<TeamID> recorded;
  for (const auto& [team_id, history] : clubs) recorded.push_back(team_id);
  for (const auto& [team_id, value] : points)
    if (!std::ranges::contains(recorded, team_id)) recorded.push_back(team_id);
  for (const TeamID team_id : recorded)
  {
    auto it = clubs.find(team_id);
    if (it == clubs.end())
    {
      const auto team = gamedata->getTeam(team_id);
      if (!team) continue;
      std::array<double, COEFFICIENT_SEASONS> prior{};
      prior.fill(
          std::max(0.0, (team->get().getReputation() - PRIOR_REPUTATION_BASE) *
                            CLUB_PRIOR_SLOPE));
      it = clubs.try_emplace(team_id, prior).first;
    }
    const auto value = points.find(team_id);
    shift(it->second, value == points.end() ? 0.0 : value->second);
  }

  // Next season's clubs from the final tables, with the updated ranking.
  qualified.clear();
  qualified_season = static_cast<uint16_t>(season_year + 1);
  std::vector<TeamID> taken;
  const GameDateValue today(static_cast<uint16_t>(season_year + 1),
                            SeasonCalendar::SEASON_START_MONTH, 1);
  for (const CompetitionRules& rules : Continental::COMPETITIONS)
  {
    std::vector<Entrant> entrants =
        qualify(rules, final_tables, cup_winners, taken);
    for (const Entrant& entrant : entrants)
    {
      postNews(today, "INBOX_CONT_QUALIFIED_TITLE",
               entrant.cup_winner ? "INBOX_CONT_QUALIFIED_CUP_BODY"
                                  : "INBOX_CONT_QUALIFIED_BODY",
               {std::string("@") + rules.name_key, teamName(entrant.team_id),
                std::to_string(entrant.league_position)},
               {entrant.team_id}, false);
    }
    qualified[rules.id] = std::move(entrants);
  }
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

std::string ContinentalCompetitions::serialize() const
{
  using nlohmann::json;
  json root;
  root["last_closed_season"] = last_closed_season;
  root["qualified_season"] = qualified_season;
  const auto entrantJson = [](const Entrant& entrant)
  {
    return json{
        {"team", entrant.team_id},   {"association", entrant.association},
        {"pot", entrant.pot},        {"position", entrant.league_position},
        {"cup", entrant.cup_winner}, {"coefficient", entrant.coefficient}};
  };
  json seasons_json = json::array();
  for (const Season& season : seasons)
  {
    json entry;
    entry["competition"] = season.competition_id;
    entry["season"] = season.season_year;
    entry["clubs"] = season.clubs;
    entry["matches"] = season.matches;
    entry["draw_date"] = season.draw_date.toString();
    entry["drawn"] = season.drawn;
    entry["league_complete"] = season.league_phase_complete;
    entry["winner"] = season.winner_id;
    entry["runner_up"] = season.runner_up_id;
    json entrants = json::array();
    for (const Entrant& entrant : season.entrants)
      entrants.push_back(entrantJson(entrant));
    entry["entrants"] = std::move(entrants);
    json ties = json::array();
    for (const Tie& tie : season.ties)
      ties.push_back({std::to_underlying(tie.round), tie.seeded_id,
                      tie.unseeded_id, tie.winner_id});
    entry["ties"] = std::move(ties);
    json draws = json::array();
    for (const DrawEvent& draw : season.draws)
      draws.push_back({draw.date.toString(), std::to_underlying(draw.round)});
    entry["draws"] = std::move(draws);
    seasons_json.push_back(std::move(entry));
  }
  root["seasons"] = std::move(seasons_json);
  json associations_json = json::array();
  for (const auto& [id, coefficient] : associations)
    associations_json.push_back(
        {{"id", id}, {"seasons", coefficientsJson(coefficient.seasons)}});
  root["associations"] = std::move(associations_json);
  json clubs_json = json::array();
  for (const auto& [id, history] : clubs)
    clubs_json.push_back({{"id", id}, {"seasons", coefficientsJson(history)}});
  root["clubs"] = std::move(clubs_json);
  json qualified_json = json::array();
  for (const auto& [id, entrants] : qualified)
  {
    json list = json::array();
    for (const Entrant& entrant : entrants)
      list.push_back(entrantJson(entrant));
    qualified_json.push_back(
        {{"competition", id}, {"entrants", std::move(list)}});
  }
  root["qualified"] = std::move(qualified_json);
  return root.dump();
}

void ContinentalCompetitions::deserialize(const std::string& data)
{
  using nlohmann::json;
  seasons.clear();
  associations.clear();
  clubs.clear();
  qualified.clear();
  tables.clear();
  last_closed_season = 0;
  qualified_season = 0;
  results_dirty = true;
  if (data.empty()) return;
  const json root = json::parse(data, nullptr, false);
  if (root.is_discarded() || !root.is_object()) return;
  last_closed_season = root.value("last_closed_season", uint16_t{0});
  qualified_season = root.value("qualified_season", uint16_t{0});
  const auto entrantFrom = [](const json& item)
  {
    Entrant entrant;
    entrant.team_id = item.value("team", TeamID{0});
    entrant.association = item.value("association", LeagueID{0});
    entrant.pot = item.value("pot", uint8_t{0});
    entrant.league_position = item.value("position", uint8_t{0});
    entrant.cup_winner = item.value("cup", false);
    entrant.coefficient = item.value("coefficient", 0.0);
    return entrant;
  };
  for (const json& item : root.value("seasons", json::array()))
  {
    Season season;
    season.competition_id = item.value("competition", LeagueID{0});
    season.season_year = item.value("season", uint16_t{0});
    season.clubs = item.value("clubs", uint8_t{0});
    season.matches = item.value("matches", uint8_t{0});
    season.draw_date =
        GameDateValue::fromString(item.value("draw_date", std::string()));
    season.drawn = item.value("drawn", false);
    season.league_phase_complete = item.value("league_complete", false);
    season.winner_id = item.value("winner", TeamID{0});
    season.runner_up_id = item.value("runner_up", TeamID{0});
    for (const json& entrant : item.value("entrants", json::array()))
      season.entrants.push_back(entrantFrom(entrant));
    for (const json& tie : item.value("ties", json::array()))
    {
      if (!tie.is_array() || tie.size() < 4) continue;
      season.ties.push_back({static_cast<Round>(tie[0].get<int>()),
                             tie[1].get<TeamID>(), tie[2].get<TeamID>(),
                             tie[3].get<TeamID>()});
    }
    for (const json& draw : item.value("draws", json::array()))
    {
      if (!draw.is_array() || draw.size() < 2) continue;
      season.draws.push_back(
          {GameDateValue::fromString(draw[0].get<std::string>()),
           static_cast<Round>(draw[1].get<int>())});
    }
    if (Continental::rules(season.competition_id))
      seasons.push_back(std::move(season));
  }
  for (const json& item : root.value("associations", json::array()))
  {
    AssociationCoefficient coefficient;
    coefficient.association = item.value("id", LeagueID{0});
    coefficient.seasons =
        coefficientsFrom(item.value("seasons", json::array()));
    associations[coefficient.association] = coefficient;
  }
  for (const json& item : root.value("clubs", json::array()))
    clubs[item.value("id", TeamID{0})] =
        coefficientsFrom(item.value("seasons", json::array()));
  for (const json& item : root.value("qualified", json::array()))
  {
    std::vector<Entrant> entrants;
    for (const json& entrant : item.value("entrants", json::array()))
      entrants.push_back(entrantFrom(entrant));
    qualified[item.value("competition", LeagueID{0})] = std::move(entrants);
  }
}

void ContinentalCompetitions::load(const DatabaseConnection& db)
{
  std::string data;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db.getRaw(),
                         "SELECT data FROM ContinentalState WHERE id = 1;", -1,
                         &stmt, nullptr) == SQLITE_OK &&
      sqlite3_step(stmt) == SQLITE_ROW)
  {
    if (const auto* text = sqlite3_column_text(stmt, 0))
      data = reinterpret_cast<const char*>(text);
  }
  sqlite3_finalize(stmt);
  deserialize(data);
}

void ContinentalCompetitions::save(const DatabaseConnection& db) const
{
  sqlite3_stmt* stmt = db.prepareStatement(
      "INSERT OR REPLACE INTO ContinentalState (id, data) VALUES (1, ?);");
  const std::string data = serialize();
  sqlite3_bind_text(stmt, 1, data.c_str(), -1, SQLITE_TRANSIENT);
  db.executeStep(stmt);
  sqlite3_finalize(stmt);
}
