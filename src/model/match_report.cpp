// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/match_report.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <utility>

#include "model/lineup.h"
#include "model/match_engine.h"
#include "model/player.h"
#include "model/world_rng.h"

using json = nlohmann::json;

namespace
{
constexpr uint8_t REGULATION_MINUTES = 90;
constexpr uint8_t EXTRA_TIME_MINUTES = 120;

uint16_t toCount(int value)
{
  return static_cast<uint16_t>(std::clamp(value, 0, 65'535));
}

uint8_t toSmallCount(long value)
{
  return static_cast<uint8_t>(std::clamp(value, 0L, 255L));
}

json statsToObject(const TeamMatchStats& stats)
{
  json object{{"shots", stats.shots},
              {"on_target", stats.shots_on_target},
              {"corners", stats.corners},
              {"fouls", stats.fouls},
              {"yellow", stats.yellow_cards},
              {"red", stats.red_cards},
              {"offsides", stats.offsides},
              {"saves", stats.saves},
              {"passes", stats.passes_attempted},
              {"passes_completed", stats.passes_completed},
              {"possession", stats.possession},
              {"xg", stats.expected_goals}};
  if (stats.set_pieces_known)
    object["sp"] = json::array({stats.set_piece_shots, stats.set_piece_goals});
  return object;
}

TeamMatchStats statsFromObject(const json& object)
{
  TeamMatchStats stats;
  stats.shots = object.value<uint16_t>("shots", 0);
  stats.shots_on_target = object.value<uint16_t>("on_target", 0);
  stats.corners = object.value<uint16_t>("corners", 0);
  stats.fouls = object.value<uint16_t>("fouls", 0);
  stats.yellow_cards = object.value<uint16_t>("yellow", 0);
  stats.red_cards = object.value<uint16_t>("red", 0);
  stats.offsides = object.value<uint16_t>("offsides", 0);
  stats.saves = object.value<uint16_t>("saves", 0);
  stats.passes_attempted = object.value<uint16_t>("passes", 0);
  stats.passes_completed = object.value<uint16_t>("passes_completed", 0);
  stats.possession = object.value("possession", 50.0f);
  stats.expected_goals = object.value("xg", 0.0f);
  if (const auto set = object.find("sp");
      set != object.end() && set->is_array() && set->size() == 2)
  {
    stats.set_pieces_known = true;
    stats.set_piece_shots = (*set)[0].get<uint16_t>();
    stats.set_piece_goals = (*set)[1].get<uint16_t>();
  }
  return stats;
}

/** Relative chance that @p player scores an extra-time goal. */
double scorerWeight(const Player& player)
{
  using enum PlayerRole;
  double role = 1.0;
  switch (player.getRole())
  {
    case GK:
    case UNKNOWN:
      return 0.0;
    case ST:
      role = 6.0;
      break;
    case LW:
    case RW:
    case CAM:
      role = 4.0;
      break;
    case LM:
    case RM:
    case CM:
      role = 2.0;
      break;
    case CDM:
      role = 1.5;
      break;
    case CB:
    case LB:
    case RB:
      role = 1.0;
      break;
  }
  const auto shooting = player.getStats().find("Shooting");
  const double finishing = shooting == player.getStats().end()
                               ? 50.0
                               : static_cast<double>(shooting->second);
  return role * (10.0 + finishing);
}

json parseOrEmpty(const std::string& text)
{
  if (text.empty()) return json();
  return json::parse(text, nullptr, false);
}
}  // namespace

void MatchReport::fillFromEngine(const MatchEngine& engine, TeamID home_id,
                                 TeamID away_id)
{
  const MatchStats& stats = engine.getStats();
  home_stats.shots = toCount(stats.homeShots);
  away_stats.shots = toCount(stats.awayShots);
  home_stats.shots_on_target = toCount(stats.homeOnTarget);
  away_stats.shots_on_target = toCount(stats.awayOnTarget);
  home_stats.corners = toCount(stats.homeCorners);
  away_stats.corners = toCount(stats.awayCorners);
  home_stats.fouls = toCount(stats.homeFouls);
  away_stats.fouls = toCount(stats.awayFouls);
  home_stats.yellow_cards = toCount(stats.homeYellowCards);
  away_stats.yellow_cards = toCount(stats.awayYellowCards);
  home_stats.offsides = toCount(stats.homeOffsides);
  away_stats.offsides = toCount(stats.awayOffsides);
  home_stats.saves = toCount(stats.homeSaves);
  away_stats.saves = toCount(stats.awaySaves);
  home_stats.passes_attempted = toCount(stats.homePassesAttempted);
  away_stats.passes_attempted = toCount(stats.awayPassesAttempted);
  home_stats.passes_completed = toCount(stats.homePassesCompleted);
  away_stats.passes_completed = toCount(stats.awayPassesCompleted);
  home_stats.possession = stats.homePossession;
  away_stats.possession = stats.awayPossession;
  home_stats.expected_goals = stats.homeShotXG;
  away_stats.expected_goals = stats.awayShotXG;
  home_stats.red_cards = toCount(stats.homeRedCards);
  away_stats.red_cards = toCount(stats.awayRedCards);
  home_stats.set_pieces_known = true;
  away_stats.set_pieces_known = true;
  home_stats.set_piece_shots = toCount(stats.homeSetPieceShots);
  away_stats.set_piece_shots = toCount(stats.awaySetPieceShots);
  home_stats.set_piece_goals = toCount(stats.homeSetPieceGoals);
  away_stats.set_piece_goals = toCount(stats.awaySetPieceGoals);
  home_goals = static_cast<uint8_t>(std::clamp(engine.getHomeScore(), 0, 255));
  away_goals = static_cast<uint8_t>(std::clamp(engine.getAwayScore(), 0, 255));
  played_home.reset();
  played_share = 0.0f;
  for (const bool home : {true, false})
  {
    if (!engine.wasControlled(home)) continue;
    played_home = home;
    const int actions = home ? stats.homePassesAttempted + stats.homeShots +
                                   stats.homeTackleAttempts
                             : stats.awayPassesAttempted + stats.awayShots +
                                   stats.awayTackleAttempts;
    played_share =
        std::clamp(static_cast<float>(engine.getControlledActions(home)) /
                       static_cast<float>(std::max(actions, 1)),
                   0.0f, 1.0f);
  }
  extra_time = engine.wentToExtraTime();
  penalties = engine.hasShootout();
  home_penalties = penalties ? static_cast<uint8_t>(std::clamp(
                                   engine.getShootoutScore(true), 0, 255))
                             : uint8_t{0};
  away_penalties = penalties ? static_cast<uint8_t>(std::clamp(
                                   engine.getShootoutScore(false), 0, 255))
                             : uint8_t{0};

  players.clear();
  players.reserve(engine.getPlayerStats().size());
  // Minutes are reported on the usual 90-minute scale (120 after extra
  // time) whatever the added time, as a share of the clock time played.
  const auto fullMatch =
      static_cast<float>(extra_time ? EXTRA_TIME_MINUTES : REGULATION_MINUTES);
  const float clockMinutes =
      std::max(fullMatch, engine.getElapsedMatchMinutes());
  for (const PlayerMatchStats& entry : engine.getPlayerStats())
  {
    PlayerMatchLine line;
    line.player_id = entry.playerId;
    line.team_id = entry.isHomeTeam ? home_id : away_id;
    line.started = entry.started;
    line.minutes = toSmallCount(
        std::lround(entry.minutesPlayed * fullMatch / clockMinutes));
    line.goals = toSmallCount(entry.goals);
    line.assists = toSmallCount(entry.assists);
    line.yellow_cards = toSmallCount(entry.yellowCards);
    line.red_cards = toSmallCount(entry.redCards);
    line.rating = entry.rating;
    players.push_back(line);
  }

  events.clear();
  for (const MatchEvent& event : engine.getEvents())
  {
    MatchReportEvent reported;
    switch (event.type)
    {
      case MatchEventType::GOAL:
        reported.kind = MatchEventKind::GOAL;
        break;
      case MatchEventType::OWN_GOAL:
        reported.kind = MatchEventKind::OWN_GOAL;
        break;
      case MatchEventType::YELLOW_CARD:
        reported.kind = MatchEventKind::YELLOW_CARD;
        break;
      case MatchEventType::SECOND_YELLOW:
        reported.kind = MatchEventKind::SECOND_YELLOW;
        break;
      case MatchEventType::RED_CARD:
        reported.kind = MatchEventKind::RED_CARD;
        break;
      default:
        continue;
    }
    reported.minute = toSmallCount(static_cast<long>(event.timeMinute));
    reported.added_minute = toSmallCount(static_cast<long>(event.addedMinute));
    reported.home = event.isHomeTeam;
    reported.player = event.primaryPlayerId;
    if (reported.kind == MatchEventKind::GOAL)
      reported.assist = event.secondaryPlayerId;
    events.push_back(reported);
  }
}

void MatchReport::addLineupAppearances(const Lineup& lineup, TeamID team_id)
{
  const auto addStarter = [&](const Player* player)
  {
    if (!player) return;
    if (const bool alreadyListed =
            std::ranges::any_of(players, [player](const PlayerMatchLine& line)
                                { return line.player_id == player->getId(); });
        alreadyListed)
      return;
    PlayerMatchLine line;
    line.player_id = player->getId();
    line.team_id = team_id;
    line.started = true;
    line.minutes = REGULATION_MINUTES;
    players.push_back(line);
  };
  addStarter(lineup.getGoalkeeper());
  for (const auto& positioned : lineup.getOutfieldPlayers())
    addStarter(positioned.player);
}

void MatchReport::creditExtraTimeGoals(const Lineup& lineup, TeamID team_id,
                                       bool home, uint8_t goals, uint32_t seed)
{
  if (goals == 0) return;
  std::vector<const Player*> squad = lineup.starters();
  squad.insert(squad.end(), lineup.getReserves().begin(),
               lineup.getReserves().end());
  std::vector<std::size_t> candidates;
  std::vector<float> weights;
  const auto collect = [&](bool finished_only)
  {
    for (std::size_t index = 0; index < players.size(); ++index)
    {
      const PlayerMatchLine& line = players[index];
      if (line.team_id != team_id || line.red_cards > 0 || line.minutes == 0)
        continue;
      // On the pitch at the final whistle: starters who played it all and
      // substitutes who came on.
      if (finished_only && line.started && line.minutes < REGULATION_MINUTES)
        continue;
      const auto player = std::ranges::find_if(
          squad, [&line](const Player* member)
          { return member && member->getId() == line.player_id; });
      if (player == squad.end()) continue;
      const double weight = scorerWeight(**player);
      if (weight <= 0.0) continue;
      candidates.push_back(index);
      weights.push_back(static_cast<float>(weight));
    }
  };
  collect(true);
  if (candidates.empty()) collect(false);

  WorldRng rng(mixHash(seed, home ? 1U : 2U));
  std::vector<uint8_t> minutes;
  for (uint8_t goal = 0; goal < goals; ++goal)
    minutes.push_back(static_cast<uint8_t>(rng.uniformInt(91, 120)));
  std::ranges::sort(minutes);
  for (const uint8_t minute : minutes)
  {
    MatchReportEvent event;
    event.minute = minute;
    event.kind = MatchEventKind::GOAL;
    event.home = home;
    if (!candidates.empty())
    {
      PlayerMatchLine& scorer = players[candidates[rng.weightedIndex(weights)]];
      scorer.goals = toSmallCount(scorer.goals + 1L);
      event.player = scorer.player_id;
    }
    events.push_back(event);
  }
}

std::string MatchReport::eventsToJson() const
{
  json array = json::array();
  for (const MatchReportEvent& event : events)
  {
    array.push_back({{"m", event.minute},
                     {"x", event.added_minute},
                     {"k", static_cast<int>(std::to_underlying(event.kind))},
                     {"h", event.home},
                     {"p", event.player},
                     {"a", event.assist}});
  }
  return array.dump();
}

std::string MatchReport::playersToJson() const
{
  json array = json::array();
  for (const PlayerMatchLine& line : players)
  {
    array.push_back({{"p", line.player_id},
                     {"t", line.team_id},
                     {"s", line.started},
                     {"min", line.minutes},
                     {"g", line.goals},
                     {"a", line.assists},
                     {"y", line.yellow_cards},
                     {"r", line.red_cards},
                     {"rt", line.rating}});
  }
  return array.dump();
}

std::string MatchReport::statsToJson() const
{
  json object{{"home", statsToObject(home_stats)},
              {"away", statsToObject(away_stats)}};
  if (played_home.has_value())
    object["play"] = json{{"home", *played_home}, {"share", played_share}};
  return object.dump();
}

void MatchReport::eventsFromJson(const std::string& text)
{
  events.clear();
  const json array = parseOrEmpty(text);
  if (!array.is_array()) return;
  events.reserve(array.size());
  for (const json& item : array)
  {
    MatchReportEvent event;
    event.minute = item.value<uint8_t>("m", 0);
    event.added_minute = item.value<uint8_t>("x", 0);
    event.kind = static_cast<MatchEventKind>(item.value<int>("k", 0));
    event.home = item.value("h", true);
    event.player = item.value<PlayerID>("p", 0);
    event.assist = item.value<PlayerID>("a", 0);
    events.push_back(event);
  }
}

void MatchReport::playersFromJson(const std::string& text)
{
  players.clear();
  const json array = parseOrEmpty(text);
  if (!array.is_array()) return;
  players.reserve(array.size());
  for (const json& item : array)
  {
    PlayerMatchLine line;
    line.player_id = item.value<PlayerID>("p", 0);
    line.team_id = item.value<TeamID>("t", 0);
    line.started = item.value("s", true);
    line.minutes = item.value<uint8_t>("min", 0);
    line.goals = item.value<uint8_t>("g", 0);
    line.assists = item.value<uint8_t>("a", 0);
    line.yellow_cards = item.value<uint8_t>("y", 0);
    line.red_cards = item.value<uint8_t>("r", 0);
    line.rating = item.value("rt", 0.0f);
    players.push_back(line);
  }
}

void MatchReport::statsFromJson(const std::string& text)
{
  const json object = parseOrEmpty(text);
  if (!object.is_object()) return;
  if (object.contains("home")) home_stats = statsFromObject(object.at("home"));
  if (object.contains("away")) away_stats = statsFromObject(object.at("away"));
  played_home.reset();
  played_share = 0.0f;
  if (const auto play = object.find("play");
      play != object.end() && play->is_object())
  {
    played_home = play->value("home", true);
    played_share = std::clamp(play->value("share", 0.0f), 0.0f, 1.0f);
  }
}
