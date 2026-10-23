// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "match.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "database/gamedata.h"
#include "lineup.h"
#include "model/calendar.h"
#include "model/competition.h"
#include "model/match_engine.h"
#include "model/match_report.h"
#include "model/match_scheduler.h"
#include "model/medical_centre.h"
#include "model/world_simulation.h"
#include "player.h"

Match::Match(TeamID home_id, TeamID away_id, GameDateValue date, MatchType type,
             LeagueID competition, uint8_t round_stage)
    : home_team_id(home_id),
      away_team_id(away_id),
      match_date(date),
      match_type(type),
      home_score(0),
      competition_id(competition),
      stage(round_stage)
{
}

uint32_t Match::getSeed() const
{
  return (static_cast<uint32_t>(home_team_id) << 16U) ^
         static_cast<uint32_t>(away_team_id) ^
         (static_cast<uint32_t>(match_date.year) << 8U) ^
         (static_cast<uint32_t>(match_date.month) << 4U) ^ match_date.day;
}

uint16_t Match::getKickoff() const
{
  if (kickoff != 0) return kickoff;
  constexpr auto at = [](int hour, int minute)
  { return static_cast<uint16_t>(hour * 60 + minute); };
  constexpr uint8_t THURSDAY = 3;
  const uint8_t weekday = SeasonCalendar::dayOfWeek(match_date);
  const bool weekend =
      weekday == SeasonCalendar::SATURDAY || weekday == SeasonCalendar::SUNDAY;
  switch (match_type)
  {
    case MatchType::FRIENDLY:
      return at(17, 0);
    case MatchType::CUP:
      return weekend ? at(21, 0) : at(20, 45);
    case MatchType::CONTINENTAL:
      return weekday == THURSDAY ? at(18, 45) : at(21, 0);
    case MatchType::LEAGUE:
      break;
  }
  return weekend ? at(15, 0) : at(20, 45);
}

void Match::simulate(const GameData& game_data, MatchReport* report,
                     std::vector<PlayerMatchConsequence>* consequences)
{
  const std::optional<MatchSimulationInput> input =
      prepareSimulation(game_data);
  if (!input) return;
  MatchSimulationResult result =
      MatchSimulation::run(*input, game_data.getStatsConfig());
  if (consequences) *consequences = std::move(result.consequences);
  MatchReport simulated = applySimulation(std::move(result));
  if (report) *report = std::move(simulated);
}

std::optional<MatchSimulationInput> Match::prepareSimulation(
    const GameData& game_data) const
{
  if (_played) return std::nullopt;
  const auto home_team = game_data.getTeam(home_team_id);
  const auto away_team = game_data.getTeam(away_team_id);
  if (!home_team || !away_team) return std::nullopt;

  MatchSimulationInput input;
  input.home_id = home_team_id;
  input.away_id = away_team_id;
  input.seed = getSeed();
  input.home_lineup = home_team->get().getLineup();
  input.away_lineup = away_team->get().getLineup();
  input.home_strategy = home_team->get().getStrategy();
  input.away_strategy = away_team->get().getStrategy();
  // A cup tie is a single match: level after 90 minutes it goes to extra
  // time and penalties. Two-legged continental ties get their aggregate
  // from the competition (CompetitionManager::knockoutRules).
  input.knockout.required = isKnockout();
  // League and domestic cup matches are played in the style of the home
  // club's league; continental matches and friendlies keep the default.
  if (match_type == MatchType::LEAGUE || match_type == MatchType::CUP)
    input.league_id = home_team->get().getLeagueId();
  return input;
}

MatchReport Match::applySimulation(MatchSimulationResult result)
{
  if (result.extra_time || result.penalties)
    setKnockoutResult(result.home_goals, result.away_goals, result.extra_time,
                      result.penalties);
  else
    setPlayedResult(result.home_goals, result.away_goals);
  writeResultTo(result.report);
  return std::move(result.report);
}

void Match::setPlayedResult(uint8_t h, uint8_t a)
{
  home_score = h;
  away_score = a;
  _played = true;
}

void Match::setKnockoutResult(
    uint8_t h, uint8_t a, bool went_to_extra_time,
    std::optional<std::pair<uint8_t, uint8_t>> shootout)
{
  setPlayedResult(h, a);
  extra_time = went_to_extra_time;
  penalties = shootout.has_value();
  home_penalties = shootout ? shootout->first : 0;
  away_penalties = shootout ? shootout->second : 0;
}

std::optional<TeamID> Match::getWinnerId() const
{
  if (!_played) return std::nullopt;
  if (home_score != away_score)
    return home_score > away_score ? home_team_id : away_team_id;
  if (penalties && home_penalties != away_penalties)
    return home_penalties > away_penalties ? home_team_id : away_team_id;
  return std::nullopt;
}

void Match::writeResultTo(MatchReport& report) const
{
  constexpr uint8_t REGULATION_MINUTES = 90;
  constexpr uint8_t EXTRA_TIME_MINUTES = 120;
  report.date = match_date;
  report.home_team_id = home_team_id;
  report.away_team_id = away_team_id;
  report.match_type = match_type;
  report.competition_id = competition_id;
  report.stage = stage;
  report.home_goals = home_score;
  report.away_goals = away_score;
  report.extra_time = extra_time;
  report.penalties = penalties;
  report.home_penalties = home_penalties;
  report.away_penalties = away_penalties;
  // Lines still on the 90-minute scale (score-only results, extra time
  // settled without the engine) stretch to the extra half hour; an engine
  // that played extra time already reported 120-minute lines.
  const bool regulation_lines =
      std::ranges::none_of(report.players, [](const PlayerMatchLine& line)
                           { return line.minutes > REGULATION_MINUTES; });
  if (extra_time && regulation_lines)
  {
    for (PlayerMatchLine& line : report.players)
      if (line.started) line.minutes = EXTRA_TIME_MINUTES;
  }
}

uint16_t Match::getHomeTeamId() const { return home_team_id; }
uint16_t Match::getAwayTeamId() const { return away_team_id; }
uint8_t Match::getHomeScore() const { return home_score; }
uint8_t Match::getAwayScore() const { return away_score; }
MatchType Match::getMatchType() const { return match_type; }
const GameDateValue& Match::getDate() const { return match_date; }

bool Match::isPlayed() const { return _played; }

// ---------------------------------------------------------------------------
// Matchday squad
// ---------------------------------------------------------------------------

namespace
{
bool isSelected(const Lineup& lineup, const Player* player)
{
  return lineup.getGoalkeeper() == player ||
         std::ranges::any_of(lineup.getOutfieldPlayers(),
                             [player](const Lineup::PositionedPlayer& slot)
                             { return slot.player == player; }) ||
         std::ranges::contains(lineup.getReserves(), player);
}

/** Defence 0, midfield 1 (holding, central and wide), attack 2. */
int lineOf(PlayerRole role)
{
  using enum PlayerRole;
  switch (role)
  {
    case LB:
    case CB:
    case RB:
      return 0;
    case LW:
    case RW:
    case ST:
      return 2;
    default:
      return 1;
  }
}

/** Left flank -1, centre 0, right flank 1. */
int flankOf(PlayerRole role)
{
  using enum PlayerRole;
  switch (role)
  {
    case LB:
    case LM:
    case LW:
      return -1;
    case RB:
    case RM:
    case RW:
      return 1;
    default:
      return 0;
  }
}

/** How well a player of @p actual fills a slot asking for @p slot (0-1). */
float slotFit(PlayerRole actual, PlayerRole slot)
{
  if (actual == slot) return 1.0f;
  if (actual == PlayerRole::GK || slot == PlayerRole::GK) return 0.0f;
  const int actualLine = lineOf(actual);
  const int slotLine = lineOf(slot);
  const int flank = flankOf(actual);
  if (flank != 0 && flank == flankOf(slot))
  {
    // A winger in wide midfield and back; a full-back further up less so.
    if (std::abs(actualLine - slotLine) != 1) return 0.55f;
    return actualLine == 0 || slotLine == 0 ? 0.75f : 0.85f;
  }
  // Forwards swap freely across the front line; a wide player in the
  // middle or a centre-back out wide less so.
  if (actualLine == slotLine)
    return actualLine == 2 || (flank == 0 && flankOf(slot) == 0) ? 0.85f
                                                                 : 0.75f;
  if (const auto pair = [&](PlayerRole a, PlayerRole b)
      { return (actual == a && slot == b) || (actual == b && slot == a); };
      pair(PlayerRole::CAM, PlayerRole::ST) ||
      pair(PlayerRole::CDM, PlayerRole::CB))
    return 0.75f;
  return 0.55f;
}
}  // namespace

float MatchdaySquad::slotScore(const Player& player, PlayerRole slot,
                               const StatsConfig& config)
{
  return static_cast<float>(player.getOverall(config)) *
         slotFit(player.getRole(), slot);
}

std::vector<PlayerID> MatchdaySquad::ineligible(const Lineup& lineup,
                                                const Eligibility& eligible)
{
  std::vector<PlayerID> result;
  const auto check = [&](const Player* player)
  {
    if (player && !eligible(*player)) result.push_back(player->getId());
  };
  check(lineup.getGoalkeeper());
  for (const Lineup::PositionedPlayer& slot : lineup.getOutfieldPlayers())
    check(slot.player);
  for (const Player* reserve : lineup.getReserves()) check(reserve);
  return result;
}

std::size_t MatchdaySquad::replaceIneligible(
    Lineup& lineup, std::span<const Player* const> squad,
    const Eligibility& eligible, const StatsConfig& config)
{
  // Unselected eligible players, best first.
  std::vector<std::pair<double, const Player*>> ranked;
  for (const Player* player : squad)
  {
    if (player && eligible(*player) && !isSelected(lineup, player))
      ranked.emplace_back(player->getOverall(config), player);
  }
  std::ranges::sort(ranked,
                    [](const auto& left, const auto& right)
                    {
                      if (left.first != right.first)
                        return left.first > right.first;
                      return left.second->getId() < right.second->getId();
                    });
  std::vector<const Player*> pool;
  pool.reserve(ranked.size());
  for (const auto& [score, player] : ranked)
  {
    static_cast<void>(score);
    pool.push_back(player);
  }

  const std::size_t before = ineligible(lineup, eligible).size();
  const std::size_t reserveCount = lineup.getReserves().size();

  std::vector<const Player*> starters;
  if (lineup.getGoalkeeper()) starters.push_back(lineup.getGoalkeeper());
  for (const Lineup::PositionedPlayer& slot : lineup.getOutfieldPlayers())
    if (slot.player) starters.push_back(slot.player);

  for (const Player* starter : starters)
  {
    if (eligible(*starter)) continue;
    const bool keeperSlot = starter == lineup.getGoalkeeper();
    PlayerRole slotRole = PlayerRole::GK;
    if (!keeperSlot)
      for (const Lineup::PositionedPlayer& slot : lineup.getOutfieldPlayers())
        if (slot.player == starter) slotRole = Lineup::roleAt(slot.position);

    // The best fit for the slot among the substitutes and the players left
    // out; substitutes win ties. A keeper slot takes a keeper, and only when
    // none is left the weakest outfielder; an outfield slot never takes one.
    const Player* replacement = nullptr;
    bool fromPool = false;
    float best = -1.0f;
    const auto consider = [&](const Player* candidate, bool pooled)
    {
      if (candidate == nullptr || !eligible(*candidate)) return;
      const bool keeper = candidate->getRole() == PlayerRole::GK;
      if (!keeperSlot && keeper) return;
      // Emergency keepers rank below every real one, weakest first.
      const float score =
          keeperSlot && !keeper
              ? -1.0f - static_cast<float>(candidate->getOverall(config))
              : slotScore(*candidate, slotRole, config);
      if (replacement != nullptr && score <= best) return;
      replacement = candidate;
      fromPool = pooled;
      best = score;
    };
    for (const Player* reserve : lineup.getReserves()) consider(reserve, false);
    for (const Player* candidate : pool) consider(candidate, true);
    if (replacement == nullptr) continue;
    // From outside the matchday squad he takes the starter's place outright
    // (the bench is full); from the bench the two swap.
    if (fromPool)
    {
      std::erase(pool, replacement);
      lineup.bringIn(replacement, starter->getId());
    }
    else
      lineup.swapPlayers(replacement->getId(), starter->getId());
  }

  // Unavailable reserves (including the starters just benched) leave the
  // squad; the best remaining players fill the bench back up.
  std::vector<const Player*> bench;
  bench.reserve(reserveCount);
  for (const Player* reserve : lineup.getReserves())
  {
    if (reserve && eligible(*reserve)) bench.push_back(reserve);
  }
  if (bench.size() != lineup.getReserves().size())
  {
    for (const Player* candidate : pool)
    {
      if (bench.size() >= reserveCount) break;
      bench.push_back(candidate);
    }
    lineup.setReserves(bench);
  }
  return before - ineligible(lineup, eligible).size();
}

namespace
{
/** A fit player who fills the place of @p standIn clearly better than he
 * does, or nullptr. */
const Player* betterStandIn(const Lineup& lineup,
                            std::span<const Player* const> squad,
                            const MatchdaySquad::Eligibility& eligible,
                            PlayerID standIn, const StatsConfig& config)
{
  // Small differences do not reshuffle the side.
  constexpr float UPGRADE_MARGIN = 2.0f;
  const Player* current = nullptr;
  PlayerRole slotRole = PlayerRole::GK;
  if (const Player* keeper = lineup.getGoalkeeper();
      keeper && keeper->getId() == standIn)
    current = keeper;
  for (const Lineup::PositionedPlayer& slot : lineup.getOutfieldPlayers())
    if (slot.player && slot.player->getId() == standIn)
    {
      current = slot.player;
      slotRole = Lineup::roleAt(slot.position);
    }
  if (current == nullptr || !eligible(*current)) return nullptr;
  const bool keeperSlot = slotRole == PlayerRole::GK;
  const Player* best = nullptr;
  float bestScore =
      MatchdaySquad::slotScore(*current, slotRole, config) + UPGRADE_MARGIN;
  const auto consider = [&](const Player* candidate)
  {
    if (candidate == nullptr || lineup.isStarter(candidate->getId()) ||
        (candidate->getRole() == PlayerRole::GK) != keeperSlot ||
        !eligible(*candidate))
      return;
    const float score = MatchdaySquad::slotScore(*candidate, slotRole, config);
    if (score <= bestScore) return;
    best = candidate;
    bestScore = score;
  };
  for (const Player* reserve : lineup.getReserves()) consider(reserve);
  for (const Player* candidate : squad) consider(candidate);
  return best;
}
}  // namespace

std::size_t MatchdaySquad::recallRegulars(Lineup& lineup,
                                          std::span<const Player* const> squad,
                                          const Eligibility& eligible,
                                          const StatsConfig& config)
{
  std::size_t recalled = 0;
  std::vector<Lineup::StandIn> kept;
  for (const Lineup::StandIn& entry : lineup.getStandIns())
  {
    if (lineup.isStarter(entry.regular) || !lineup.isStarter(entry.stand_in))
      continue;
    const auto regular = std::ranges::find_if(
        squad, [&](const Player* player)
        { return player && player->getId() == entry.regular; });
    if (regular == squad.end()) continue;
    if (!eligible(**regular))
    {
      // While he is out, a clearly better fit who can play again takes over
      // from the stand-in.
      Lineup::StandIn current = entry;
      if (const Player* better =
              betterStandIn(lineup, squad, eligible, entry.stand_in, config))
      {
        const bool benched =
            std::ranges::contains(lineup.getReserves(), better);
        if (benched ? lineup.swapPlayers(better->getId(), entry.stand_in)
                    : lineup.bringIn(better, entry.stand_in))
          current.stand_in = better->getId();
      }
      kept.push_back(current);
      continue;
    }
    const bool onBench = std::ranges::contains(lineup.getReserves(), *regular);
    if (onBench ? lineup.swapPlayers(entry.regular, entry.stand_in)
                : lineup.bringIn(*regular, entry.stand_in))
      ++recalled;
  }
  lineup.setStandIns(std::move(kept));
  return recalled;
}

void MatchdaySquad::recordStandIns(const Lineup& before, Lineup& after)
{
  std::vector<Lineup::StandIn> entries = after.getStandIns();
  const auto record = [&](const Player* out, const Player* in)
  {
    if (!out || !in || out == in) return;
    const auto handed =
        std::ranges::find_if(entries, [out](const Lineup::StandIn& entry)
                             { return entry.stand_in == out->getId(); });
    if (handed != entries.end())
      handed->stand_in = in->getId();
    else
      entries.push_back({out->getId(), in->getId()});
  };
  record(before.getGoalkeeper(), after.getGoalkeeper());
  // Slots line up only while nobody has left the XI without a replacement.
  const auto& oldSlots = before.getOutfieldPlayers();
  if (const auto& newSlots = after.getOutfieldPlayers();
      oldSlots.size() == newSlots.size())
    for (std::size_t slot = 0; slot < oldSlots.size(); ++slot)
      record(oldSlots[slot].player, newSlots[slot].player);
  after.setStandIns(std::move(entries));
}

std::vector<std::pair<PlayerID, PlayerID>> MatchdaySquad::replacements(
    const Lineup& before, const Lineup& after)
{
  std::vector<std::pair<PlayerID, PlayerID>> result;
  const auto idOf = [](const Player* player)
  { return player ? player->getId() : PlayerID{}; };
  if (idOf(before.getGoalkeeper()) != idOf(after.getGoalkeeper()))
    result.emplace_back(idOf(before.getGoalkeeper()),
                        idOf(after.getGoalkeeper()));
  const auto& oldSlots = before.getOutfieldPlayers();
  const auto& newSlots = after.getOutfieldPlayers();
  for (std::size_t slot = 0; slot < std::min(oldSlots.size(), newSlots.size());
       ++slot)
  {
    if (idOf(oldSlots[slot].player) != idOf(newSlots[slot].player))
      result.emplace_back(idOf(oldSlots[slot].player),
                          idOf(newSlots[slot].player));
  }
  for (const Player* reserve : before.getReserves())
  {
    if (reserve && !isSelected(after, reserve))
      result.emplace_back(reserve->getId(), PlayerID{});
  }
  return result;
}

std::vector<std::pair<PlayerID, std::uint8_t>> MatchdaySquad::medicalFlags(
    const MedicalDesk& medical, const Lineup& lineup)
{
  std::vector<std::pair<PlayerID, std::uint8_t>> flagged;
  const auto add = [&](const Player* player)
  {
    if (!player) return;
    if (const std::uint8_t flags = medical.flags(player->getId()); flags != 0)
      flagged.emplace_back(player->getId(), flags);
  };
  add(lineup.getGoalkeeper());
  for (const auto& outfield : lineup.getOutfieldPlayers()) add(outfield.player);
  for (const Player* reserve : lineup.getReserves()) add(reserve);
  return flagged;
}

void MatchdaySquad::carryCondition(MatchEngine& engine, const Lineup& lineup)
{
  const auto carry = [&engine](const Player* player)
  {
    if (player)
      engine.setPlayerCondition(player->getId(),
                                player->getDynamics().condition / 100.0f);
  };
  carry(lineup.getGoalkeeper());
  for (const Lineup::PositionedPlayer& slot : lineup.getOutfieldPlayers())
    carry(slot.player);
  // Substitutes come on with their own condition.
  for (const Player* reserve : lineup.getReserves()) carry(reserve);
}

std::vector<PlayerMatchConsequence> MatchdaySquad::consequences(
    const MatchEngine& engine)
{
  std::vector<PlayerMatchConsequence> result;
  result.reserve(engine.getPlayerStats().size());
  for (const PlayerMatchStats& stats : engine.getPlayerStats())
  {
    const long minutes = std::lround(stats.minutesPlayed);
    if (minutes <= 0) continue;
    result.push_back(
        {stats.playerId, static_cast<std::uint8_t>(std::min(minutes, 255L)),
         std::clamp(stats.condition, 0.0f, 1.0f) * 100.0f, stats.injured});
  }
  return result;
}
