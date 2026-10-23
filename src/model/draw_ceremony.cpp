// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/draw_ceremony.h"

#include <fmt/printf.h>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <tuple>
#include <utility>

#include "global/language_manager.h"
#include "model/calendar.h"
#include "model/competition.h"
#include "model/inbox.h"

namespace
{
/** Separates the reveal order from every random stream of the draws. */
constexpr std::uint32_t REVEAL_SALT = 0x5EED'D7A1U;
constexpr std::string_view LEAGUE_PHASE_TITLE = "INBOX_CONT_DRAW_TITLE";
constexpr std::string_view KNOCKOUT_TITLE = "INBOX_CONT_KO_DRAW_TITLE";

/**
 * Fisher-Yates shuffle driven by Competitions::mixSeed(), so the order is
 * the same with every compiler and standard library.
 */
template <typename T>
void seededShuffle(std::vector<T>& items, std::uint32_t seed)
{
  for (std::size_t index = items.size(); index > 1; --index)
  {
    const std::uint32_t roll = Competitions::mixSeed(
        seed, static_cast<std::uint32_t>(index), REVEAL_SALT);
    const std::size_t other = roll % index;
    std::swap(items[index - 1], items[other]);
  }
}

std::string roundName(Continental::Round round)
{
  return LOC(Continental::roundKey(round));
}

const ContinentalCompetitions::Season* findSeason(
    const ContinentalCompetitions& continental, LeagueID competition_id,
    std::uint16_t season_year)
{
  for (const auto& season : continental.getSeasons())
    if (season.competition_id == competition_id &&
        season.season_year == season_year)
      return &season;
  return nullptr;
}

const ContinentalCompetitions::DrawEvent* findDraw(
    const ContinentalCompetitions::Season& season, Continental::Round round)
{
  for (const auto& event : season.draws)
    if (event.round == round) return &event;
  return nullptr;
}

/** First match of a pairing in a round (the first leg, or the final). */
const Match* findLeg(const Calendar& calendar, LeagueID competition_id,
                     std::uint8_t stage, TeamID home_id, TeamID away_id)
{
  for (const auto& [date, matches] : calendar.getFullCalendar())
    for (const Match& match : matches)
      if (match.getMatchType() == MatchType::CONTINENTAL &&
          match.getCompetitionId() == competition_id &&
          match.getStage() == stage && match.getHomeTeamId() == home_id &&
          match.getAwayTeamId() == away_id)
        return &match;
  return nullptr;
}

void knockoutReveals(DrawCeremony& ceremony, const Calendar& calendar,
                     const ContinentalCompetitions::Season& season,
                     Continental::Round round)
{
  for (const auto& tie : season.ties)
  {
    if (tie.round != round) continue;
    DrawReveal reveal;
    if (round == Continental::Round::Final)
    {
      reveal.home_id = tie.seeded_id;
      reveal.away_id = tie.unseeded_id;
      if (const Match* final_match = findLeg(calendar, season.competition_id,
                                             Continental::stageCode(round, 1),
                                             tie.seeded_id, tie.unseeded_id))
        reveal.date = final_match->getDate();
    }
    else
    {
      // The unseeded club hosts the first leg.
      reveal.home_id = tie.unseeded_id;
      reveal.away_id = tie.seeded_id;
      if (const Match* first = findLeg(calendar, season.competition_id,
                                       Continental::stageCode(round, 1),
                                       tie.unseeded_id, tie.seeded_id))
        reveal.date = first->getDate();
      if (const Match* second = findLeg(calendar, season.competition_id,
                                        Continental::stageCode(round, 2),
                                        tie.seeded_id, tie.unseeded_id))
        reveal.second_leg = second->getDate();
    }
    ceremony.reveals.push_back(reveal);
  }
}

void leaguePhaseReveals(DrawCeremony& ceremony, const Calendar& calendar,
                        const ContinentalCompetitions::Season& season)
{
  const auto entrant = [&season](TeamID team_id)
  {
    return std::ranges::find(season.entrants, team_id,
                             &ContinentalCompetitions::Entrant::team_id);
  };
  if (ceremony.focus_team == 0 ||
      entrant(ceremony.focus_team) == season.entrants.end())
  {
    const auto top = std::ranges::min_element(
        season.entrants,
        [](const auto& left, const auto& right)
        {
          return std::tuple{left.pot, -left.coefficient, left.team_id} <
                 std::tuple{right.pot, -right.coefficient, right.team_id};
        });
    ceremony.focus_team = top != season.entrants.end() ? top->team_id : 0;
  }
  const TeamID focus = ceremony.focus_team;
  for (const auto& [date, matches] : calendar.getFullCalendar())
  {
    for (const Match& match : matches)
    {
      if (match.getMatchType() != MatchType::CONTINENTAL ||
          match.getCompetitionId() != season.competition_id ||
          Continental::roundOf(match.getStage()) !=
              Continental::Round::LeaguePhase)
        continue;
      if (match.getHomeTeamId() != focus && match.getAwayTeamId() != focus)
        continue;
      const TeamID opponent = match.getHomeTeamId() == focus
                                  ? match.getAwayTeamId()
                                  : match.getHomeTeamId();
      DrawReveal reveal;
      reveal.home_id = match.getHomeTeamId();
      reveal.away_id = match.getAwayTeamId();
      reveal.date = match.getDate();
      if (const auto found = entrant(opponent); found != season.entrants.end())
        reveal.pot = static_cast<std::uint8_t>(found->pot + 1U);
      ceremony.reveals.push_back(reveal);
    }
  }
  // Pot by pot, the home opponent before the away one.
  std::ranges::stable_sort(
      ceremony.reveals,
      [focus](const DrawReveal& left, const DrawReveal& right)
      {
        return std::tuple{left.pot, left.home_id != focus} <
               std::tuple{right.pot, right.home_id != focus};
      });
}
}  // namespace

std::optional<DrawCeremony> DrawCeremonies::cupRound(const Calendar& calendar,
                                                     const GameData& gamedata,
                                                     LeagueID root,
                                                     std::uint8_t stage,
                                                     TeamID focus_team)
{
  const std::optional<Competitions::CupDraw> draw =
      Competitions::cupDraw(calendar, gamedata, root, stage);
  if (!draw) return std::nullopt;
  DrawCeremony ceremony;
  ceremony.kind = DrawCeremony::Kind::DomesticCup;
  ceremony.competition_id = root;
  ceremony.stage = stage;
  ceremony.competition_name = Competitions::cupName(gamedata, root);
  ceremony.round_name = fmt::sprintf(
      LOC(Competitions::cupRoundLabelKey(stage, draw->total_rounds)),
      static_cast<int>(stage));
  ceremony.drawn_on = draw->drawn_on;
  ceremony.focus_team = focus_team;
  ceremony.reveals.reserve(draw->ties.size());
  for (const Match& tie : draw->ties)
  {
    DrawReveal reveal;
    reveal.home_id = tie.getHomeTeamId();
    reveal.away_id = tie.getAwayTeamId();
    reveal.date = tie.getDate();
    ceremony.reveals.push_back(reveal);
  }
  seededShuffle(ceremony.reveals,
                Competitions::mixSeed(SeasonCalendar::seasonStartYear(
                                          draw->ties.front().getDate()),
                                      root, stage));
  return ceremony;
}

std::optional<DrawCeremony> DrawCeremonies::latestCupRound(
    const Calendar& calendar, const GameData& gamedata, LeagueID root,
    const GameDateValue& today, TeamID focus_team)
{
  std::uint8_t latest = 0;
  for (const auto& [date, matches] : calendar.getFullCalendar())
    for (const Match& match : matches)
      if (match.getMatchType() == MatchType::CUP &&
          match.getCompetitionId() == root)
        latest = std::max(latest, match.getStage());
  for (std::uint8_t stage = latest; stage > 0; --stage)
  {
    std::optional<DrawCeremony> ceremony =
        cupRound(calendar, gamedata, root, stage, focus_team);
    if (ceremony && !(today < ceremony->drawn_on)) return ceremony;
  }
  return std::nullopt;
}

std::optional<DrawCeremony> DrawCeremonies::continentalRound(
    const ContinentalCompetitions& continental, const Calendar& calendar,
    LeagueID competition_id, std::uint16_t season_year,
    Continental::Round round, TeamID focus_team)
{
  const Continental::CompetitionRules* rules =
      Continental::rules(competition_id);
  const ContinentalCompetitions::Season* season =
      findSeason(continental, competition_id, season_year);
  if (rules == nullptr || season == nullptr) return std::nullopt;
  const ContinentalCompetitions::DrawEvent* event = findDraw(*season, round);
  if (event == nullptr) return std::nullopt;

  DrawCeremony ceremony;
  ceremony.competition_id = competition_id;
  ceremony.stage = std::to_underlying(round);
  ceremony.competition_name = LOC(rules->name_key);
  ceremony.round_name = roundName(round);
  ceremony.drawn_on = event->date;
  ceremony.focus_team = focus_team;
  if (round == Continental::Round::LeaguePhase)
  {
    ceremony.kind = DrawCeremony::Kind::ContinentalLeaguePhase;
    leaguePhaseReveals(ceremony, calendar, *season);
  }
  else
  {
    ceremony.kind = DrawCeremony::Kind::ContinentalKnockout;
    knockoutReveals(ceremony, calendar, *season, round);
  }
  if (ceremony.reveals.empty()) return std::nullopt;
  return ceremony;
}

std::optional<DrawCeremony> DrawCeremonies::latestContinentalRound(
    const ContinentalCompetitions& continental, const Calendar& calendar,
    LeagueID competition_id, const GameDateValue& today, TeamID focus_team)
{
  const ContinentalCompetitions::Season* season = findSeason(
      continental, competition_id, SeasonCalendar::seasonStartYear(today));
  if (season == nullptr) return std::nullopt;
  const ContinentalCompetitions::DrawEvent* latest = nullptr;
  for (const auto& event : season->draws)
    if (!(today < event.date) &&
        (latest == nullptr || !(event.date < latest->date)))
      latest = &event;
  if (latest == nullptr) return std::nullopt;
  return continentalRound(continental, calendar, competition_id,
                          season->season_year, latest->round, focus_team);
}

bool DrawCeremonies::announcesDraw(const InboxMessage& message)
{
  return message.title_key == LEAGUE_PHASE_TITLE ||
         message.title_key == KNOCKOUT_TITLE;
}

std::optional<DrawCeremony> DrawCeremonies::forMessage(
    const InboxMessage& message, const ContinentalCompetitions& continental,
    const Calendar& calendar, TeamID focus_team)
{
  if (!announcesDraw(message) || message.args.empty()) return std::nullopt;
  const Continental::CompetitionRules* rules = nullptr;
  for (const Continental::CompetitionRules& candidate :
       Continental::COMPETITIONS)
    if (message.args[0] == std::string("@") + candidate.name_key)
      rules = &candidate;
  if (rules == nullptr) return std::nullopt;
  const std::uint16_t season_year =
      SeasonCalendar::seasonStartYear(message.date);
  const ContinentalCompetitions::Season* season =
      findSeason(continental, rules->id, season_year);
  if (season == nullptr) return std::nullopt;
  for (const auto& event : season->draws)
  {
    if (!(event.date == message.date)) continue;
    const bool league_phase = event.round == Continental::Round::LeaguePhase;
    if (league_phase != (message.title_key == LEAGUE_PHASE_TITLE)) continue;
    if (!league_phase &&
        (message.args.size() < 2 ||
         message.args[1] !=
             std::string("@") + Continental::roundKey(event.round)))
      continue;
    return continentalRound(continental, calendar, rules->id, season_year,
                            event.round, focus_team);
  }
  return std::nullopt;
}

std::size_t DrawCeremonies::shownAt(float elapsed, float speed,
                                    std::size_t total, bool reduced_motion)
{
  if (reduced_motion || total == 0) return total;
  const float steps =
      std::max(0.0f, elapsed) * std::max(speed, 0.0f) / REVEAL_SECONDS;
  const auto shown = static_cast<std::size_t>(std::floor(steps)) + 1U;
  return std::min(shown, total);
}
