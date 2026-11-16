// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/awards.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <set>
#include <utility>

#include "database/gamedata.h"
#include "database/sqlite_rows.h"
#include "model/board.h"
#include "model/competition.h"
#include "model/inbox.h"
#include "model/match_report.h"
#include "model/player.h"
#include "model/team.h"

namespace
{
constexpr const char* NOT_AWARDED = "@AWARDS_NOT_AWARDED";
constexpr std::array<const char*, 12> MONTH_KEYS = {
    "MONTH_JAN", "MONTH_FEB", "MONTH_MAR", "MONTH_APR",
    "MONTH_MAY", "MONTH_JUN", "MONTH_JUL", "MONTH_AUG",
    "MONTH_SEP", "MONTH_OCT", "MONTH_NOV", "MONTH_DEC"};
/** Morale lift of a winner. [P] */
constexpr float MONTH_MORALE = 4.0f;
constexpr float SEASON_MORALE = 6.0f;
constexpr float TEAM_OF_SEASON_MORALE = 3.0f;
/** Board confidence lift for the managed club's manager awards. [P] */
constexpr float MONTH_CONFIDENCE = 3.0f;
constexpr float SEASON_CONFIDENCE = 6.0f;

std::uint16_t seasonOf(const GameDateValue& date)
{
  return static_cast<std::uint16_t>(date.month >= 7 ? date.year
                                                    : date.year - 1);
}

bool isSeasonAward(AwardType type)
{
  using enum AwardType;
  return type == PlayerOfSeason || type == YoungPlayerOfSeason ||
         type == GoldenBoot || type == GoldenGlove;
}

bool isMonthlyPlayerAward(AwardType type)
{
  return type == AwardType::PlayerOfMonth ||
         type == AwardType::YoungPlayerOfMonth;
}

bool isManagerAward(AwardType type)
{
  return type == AwardType::ManagerOfMonth ||
         type == AwardType::ManagerOfSeason;
}

float pointsFor(int own, int other)
{
  if (own > other) return 3.0f;
  return own == other ? 1.0f : 0.0f;
}

/** Positions of the Team of the Season, natural roles then neighbours. */
struct SlotRule
{
  std::initializer_list<PlayerRole> primary;
  std::initializer_list<PlayerRole> fallback;
};

const std::array<SlotRule, Awards::TEAM_OF_SEASON_SIZE>& slotRules()
{
  using enum PlayerRole;
  static const std::array<SlotRule, Awards::TEAM_OF_SEASON_SIZE> rules = {{
      {{GK}, {}},
      {{RB}, {CB, LB}},
      {{CB}, {RB, LB, CDM}},
      {{CB}, {RB, LB, CDM}},
      {{LB}, {CB, RB}},
      {{CDM, CM, CAM}, {LM, RM}},
      {{CDM, CM, CAM}, {LM, RM}},
      {{CDM, CM, CAM}, {LM, RM}},
      {{RW, RM}, {LW, LM, CAM, ST}},
      {{ST}, {CAM, LW, RW}},
      {{LW, LM}, {RW, RM, CAM, ST}},
  }};
  return rules;
}

/** Scarce positions are filled first. */
constexpr std::array<std::uint8_t, Awards::TEAM_OF_SEASON_SIZE> FILL_ORDER = {
    0, 2, 3, 1, 4, 5, 6, 7, 9, 8, 10};

bool better(float score, PlayerID id, float best_score, PlayerID best_id)
{
  return score > best_score || (score == best_score && id < best_id);
}

std::string playerLine(const AwardRecord& record, const GameData& gamedata)
{
  const auto team = gamedata.getTeam(record.team_id);
  return std::format("{} ({}, {:.2f})", record.name,
                     team ? team->get().getName() : std::string("–"),
                     record.value);
}

std::string countLine(const AwardRecord& record, const GameData& gamedata)
{
  const auto team = gamedata.getTeam(record.team_id);
  return std::format("{} ({}, {})", record.name,
                     team ? team->get().getName() : std::string("–"),
                     static_cast<int>(std::lround(record.value)));
}

std::string managerLine(const AwardRecord& record)
{
  return std::format("{} ({:+.1f})", record.name, record.value);
}

std::string goalLine(const AwardRecord& record, const GameData& gamedata)
{
  const auto team = gamedata.getTeam(record.team_id);
  const auto opponent = gamedata.getTeam(record.opponent_id);
  return std::format("{} ({}) – {}, {}'", record.name,
                     team ? team->get().getName() : std::string("–"),
                     opponent ? opponent->get().getName() : std::string("–"),
                     record.count);
}

const AwardRecord* findType(const std::vector<const AwardRecord*>& records,
                            AwardType type)
{
  for (const AwardRecord* record : records)
    if (record->type == type) return record;
  return nullptr;
}
}  // namespace

const char* awardTypeKey(AwardType type)
{
  using enum AwardType;
  switch (type)
  {
    case PlayerOfMonth:
      return "AWARD_PLAYER_OF_MONTH";
    case YoungPlayerOfMonth:
      return "AWARD_YOUNG_PLAYER_OF_MONTH";
    case ManagerOfMonth:
      return "AWARD_MANAGER_OF_MONTH";
    case GoalOfMonth:
      return "AWARD_GOAL_OF_MONTH";
    case PlayerOfSeason:
      return "AWARD_PLAYER_OF_SEASON";
    case YoungPlayerOfSeason:
      return "AWARD_YOUNG_PLAYER_OF_SEASON";
    case GoldenBoot:
      return "AWARD_GOLDEN_BOOT";
    case GoldenGlove:
      return "AWARD_GOLDEN_GLOVE";
    case TeamOfSeason:
      return "AWARD_TEAM_OF_SEASON";
    case ManagerOfSeason:
    case COUNT:
      break;
  }
  return "AWARD_MANAGER_OF_SEASON";
}

// ---------------------------------------------------------------------------
// Selection rules
// ---------------------------------------------------------------------------

namespace Awards
{
float playerScore(const AwardPlayerTally& tally, bool goalkeeper)
{
  const auto appearances =
      static_cast<float>(std::max<std::uint16_t>(1, tally.appearances));
  // [P] Ratings dominate; a goal every match adds 0.3, an assist half that.
  float score =
      tally.averageRating() + 0.3f *
                                  (static_cast<float>(tally.goals) +
                                   0.5f * static_cast<float>(tally.assists)) /
                                  appearances;
  if (goalkeeper)
    score += 0.25f * static_cast<float>(tally.clean_sheets) / appearances;
  return score;
}

std::optional<std::size_t> bestPlayer(
    std::span<const AwardCandidate> candidates, std::uint16_t min_minutes,
    int max_age)
{
  std::optional<std::size_t> best;
  float best_score = 0.0f;
  for (std::size_t index = 0; index < candidates.size(); ++index)
  {
    const AwardCandidate& candidate = candidates[index];
    if (candidate.tally.minutes < min_minutes || candidate.tally.rated == 0 ||
        (max_age > 0 && candidate.age > max_age))
      continue;
    const float score =
        playerScore(candidate.tally, candidate.role == PlayerRole::GK);
    if (!best || better(score, candidate.tally.player_id, best_score,
                        candidates[*best].tally.player_id))
    {
      best = index;
      best_score = score;
    }
  }
  return best;
}

std::optional<std::size_t> goldenBoot(
    std::span<const AwardCandidate> candidates)
{
  std::optional<std::size_t> best;
  for (std::size_t index = 0; index < candidates.size(); ++index)
  {
    const AwardPlayerTally& tally = candidates[index].tally;
    if (tally.goals == 0) continue;
    if (!best.has_value())
    {
      best = index;
      continue;
    }
    const AwardPlayerTally& holder = candidates[*best].tally;
    const auto key = [](const AwardPlayerTally& t)
    {
      return std::tuple(-static_cast<int>(t.goals), static_cast<int>(t.minutes),
                        -static_cast<int>(t.assists), t.player_id);
    };
    if (key(tally) < key(holder)) best = index;
  }
  return best;
}

std::optional<std::size_t> goldenGlove(
    std::span<const AwardCandidate> candidates, std::uint16_t min_minutes)
{
  std::optional<std::size_t> best;
  for (std::size_t index = 0; index < candidates.size(); ++index)
  {
    const AwardCandidate& candidate = candidates[index];
    const AwardPlayerTally& tally = candidate.tally;
    if (candidate.role != PlayerRole::GK || tally.minutes < min_minutes ||
        tally.clean_sheets == 0)
      continue;
    if (!best.has_value())
    {
      best = index;
      continue;
    }
    const auto key = [](const AwardPlayerTally& t)
    {
      return std::tuple(-static_cast<int>(t.clean_sheets),
                        static_cast<int>(t.minutes), t.player_id);
    };
    if (key(tally) < key(candidates[*best].tally)) best = index;
  }
  return best;
}

std::array<std::optional<std::size_t>, TEAM_OF_SEASON_SIZE> teamOfSeason(
    std::span<const AwardCandidate> candidates, std::uint16_t min_minutes)
{
  std::array<std::optional<std::size_t>, TEAM_OF_SEASON_SIZE> team{};
  std::vector<bool> used(candidates.size(), false);
  const auto fill =
      [&](std::uint8_t slot, std::initializer_list<PlayerRole> roles)
  {
    std::optional<std::size_t> best;
    float best_score = 0.0f;
    for (std::size_t index = 0; index < candidates.size(); ++index)
    {
      const AwardCandidate& candidate = candidates[index];
      if (used[index] || candidate.tally.minutes < min_minutes ||
          candidate.tally.rated == 0 ||
          std::ranges::find(roles, candidate.role) == roles.end())
        continue;
      const float score = playerScore(candidate.tally, slot == 0);
      if (!best || better(score, candidate.tally.player_id, best_score,
                          candidates[*best].tally.player_id))
      {
        best = index;
        best_score = score;
      }
    }
    if (!best.has_value()) return;
    team[slot] = best;
    used[*best] = true;
  };
  for (const std::uint8_t slot : FILL_ORDER)
    fill(slot, slotRules()[slot].primary);
  for (const std::uint8_t slot : FILL_ORDER)
    if (!team[slot].has_value()) fill(slot, slotRules()[slot].fallback);
  return team;
}

const char* slotKey(std::uint8_t slot)
{
  static constexpr std::array<const char*, TEAM_OF_SEASON_SIZE> KEYS = {
      "AWARD_SLOT_GK", "AWARD_SLOT_RB",  "AWARD_SLOT_CB",  "AWARD_SLOT_CB",
      "AWARD_SLOT_LB", "AWARD_SLOT_MID", "AWARD_SLOT_MID", "AWARD_SLOT_MID",
      "AWARD_SLOT_RW", "AWARD_SLOT_ST",  "AWARD_SLOT_LW"};
  return slot < KEYS.size() ? KEYS[slot] : KEYS.back();
}

std::optional<std::size_t> bestManager(std::span<const AwardClubTally> clubs,
                                       std::uint16_t min_matches)
{
  std::optional<std::size_t> best;
  for (std::size_t index = 0; index < clubs.size(); ++index)
  {
    const AwardClubTally& club = clubs[index];
    if (club.matches < min_matches) continue;
    if (!best.has_value())
    {
      best = index;
      continue;
    }
    const AwardClubTally& holder = clubs[*best];
    const auto key = [](const AwardClubTally& c)
    { return std::tuple(-c.overPerformance(), -c.points, c.team_id); };
    if (key(club) < key(holder)) best = index;
  }
  return best;
}

std::uint16_t seasonMinMinutes(std::span<const AwardCandidate> candidates)
{
  std::uint16_t most = 0;
  for (const AwardCandidate& candidate : candidates)
    most = std::max(most, candidate.tally.minutes);
  return static_cast<std::uint16_t>(
      std::max(1.0f, std::floor(static_cast<float>(most) * SEASON_MIN_SHARE)));
}

float goalScore(std::uint8_t minute, int own_before, int other_before,
                bool decisive, int reputation_gap, bool assisted)
{
  // [P] What a panel rewards when no footage exists: timing, stakes, the
  // opponent and individual effort.
  float score = 1.0f;
  if (minute >= 80)
    score += 0.8f;
  else if (minute >= 70)
    score += 0.3f;
  if (decisive) score += 1.2f;
  if (own_before + 1 == other_before) score += 0.6f;  // equaliser
  score += std::clamp(static_cast<float>(reputation_gap) / 20.0f, -0.5f, 1.0f);
  if (!assisted) score += 0.4f;
  return score;
}

float valueMultiplier(std::span<const AwardRecord> honours,
                      const GameDateValue& today)
{
  const int now_months = static_cast<int>(today.year) * 12 + today.month;
  const int season_now = seasonOf(today);
  int monthly = 0;
  float premium = 0.0f;
  for (const AwardRecord& record : honours)
  {
    if (isMonthlyPlayerAward(record.type))
    {
      const int year =
          record.month >= 7 ? record.season_year : record.season_year + 1;
      if (now_months - (year * 12 + record.month) <= 12) ++monthly;
    }
    else if (record.season_year + 1 >= season_now)
    {
      if (isSeasonAward(record.type)) premium += 0.08f;
      if (record.type == AwardType::TeamOfSeason) premium += 0.04f;
    }
  }
  premium += 0.03f * static_cast<float>(std::min(monthly, 3));
  return 1.0f + std::min(premium, MAX_VALUE_PREMIUM);
}
}  // namespace Awards

// ---------------------------------------------------------------------------
// AwardSystem
// ---------------------------------------------------------------------------

void AwardSystem::onMatchPlayed(const GameData& gamedata,
                                const MatchReport& report, float home_expected,
                                float away_expected)
{
  using enum MatchEventKind;
  if (report.match_type != MatchType::LEAGUE) return;
  LeagueID league_id = report.competition_id;
  if (league_id == 0)
    if (const auto home = gamedata.getTeam(report.home_team_id))
      league_id = home->get().getLeagueId();
  const int home_goals = report.home_goals;
  const int away_goals = report.away_goals;

  // Goals and assists come from the player lines; reports without them
  // (score-only results) credit the scorers of the goal events.
  int line_goals = 0;
  for (const PlayerMatchLine& line : report.players) line_goals += line.goals;
  const bool from_events = line_goals == 0;

  // Opponent goals by minute, for clean sheets of starters who came off.
  std::vector<std::uint8_t> conceded_home;
  std::vector<std::uint8_t> conceded_away;
  for (const MatchReportEvent& event : report.events)
  {
    const bool counts_for_home = (event.kind == GOAL && event.home) ||
                                 (event.kind == OWN_GOAL && !event.home);
    if (event.kind != GOAL && event.kind != OWN_GOAL) continue;
    (counts_for_home ? conceded_away : conceded_home).push_back(event.minute);
  }

  for (const PlayerMatchLine& line : report.players)
  {
    if (line.minutes == 0) continue;
    const bool home = line.team_id == report.home_team_id;
    const auto key = std::pair(league_id, line.player_id);
    std::uint16_t goals = line.goals;
    std::uint16_t assists = line.assists;
    if (from_events)
    {
      goals = 0;
      assists = 0;
      for (const MatchReportEvent& event : report.events)
      {
        if (event.kind != GOAL) continue;
        if (event.player == line.player_id) ++goals;
        if (event.assist == line.player_id) ++assists;
      }
    }
    bool clean_sheet = false;
    if (line.started && line.minutes >= Awards::CLEAN_SHEET_MINUTES)
    {
      const int conceded = home ? away_goals : home_goals;
      const auto& minutes = home ? conceded_home : conceded_away;
      clean_sheet = conceded == 0 ||
                    (static_cast<int>(minutes.size()) == conceded &&
                     std::ranges::all_of(minutes, [&](std::uint8_t minute)
                                         { return minute > line.minutes; }));
    }
    for (auto* tallies : {&month_players, &season_players})
    {
      AwardPlayerTally& tally = (*tallies)[key];
      tally.league_id = league_id;
      tally.player_id = line.player_id;
      tally.team_id = line.team_id;
      ++tally.appearances;
      tally.minutes = static_cast<std::uint16_t>(tally.minutes + line.minutes);
      tally.goals = static_cast<std::uint16_t>(tally.goals + goals);
      tally.assists = static_cast<std::uint16_t>(tally.assists + assists);
      if (clean_sheet) ++tally.clean_sheets;
      if (line.rating > 0.0f)
      {
        tally.rating_total += line.rating;
        ++tally.rated;
      }
    }
  }

  const auto addClub = [&](TeamID team_id, int own, int other, float expected)
  {
    for (auto* tallies : {&month_clubs, &season_clubs})
    {
      AwardClubTally& club = (*tallies)[team_id];
      club.team_id = team_id;
      club.league_id = league_id;
      ++club.matches;
      club.points += pointsFor(own, other);
      club.expected_points += expected;
      club.goals_for = static_cast<std::uint16_t>(club.goals_for + own);
      club.goals_against =
          static_cast<std::uint16_t>(club.goals_against + other);
    }
  };
  addClub(report.home_team_id, home_goals, away_goals, home_expected);
  addClub(report.away_team_id, away_goals, home_goals, away_expected);

  // Goal of the Month candidates, replaying the score in minute order.
  std::vector<const MatchReportEvent*> goals;
  for (const MatchReportEvent& event : report.events)
    if (event.kind == GOAL || event.kind == OWN_GOAL) goals.push_back(&event);
  std::ranges::stable_sort(
      goals, {}, [](const MatchReportEvent* event) { return event->minute; });
  const auto reputation = [&](TeamID team_id)
  {
    const auto team = gamedata.getTeam(team_id);
    return team ? static_cast<int>(team->get().getReputation()) : 50;
  };
  int home_score = 0;
  int away_score = 0;
  for (const MatchReportEvent* event : goals)
  {
    const bool for_home = (event->kind == MatchEventKind::GOAL) == event->home;
    const int own_before = for_home ? home_score : away_score;
    const int other_before = for_home ? away_score : home_score;
    (for_home ? home_score : away_score)++;
    if (event->kind != MatchEventKind::GOAL || event->player == 0) continue;
    const int own_final = for_home ? home_goals : away_goals;
    const int other_final = for_home ? away_goals : home_goals;
    const TeamID team_id = for_home ? report.home_team_id : report.away_team_id;
    const TeamID opponent_id =
        for_home ? report.away_team_id : report.home_team_id;
    // The game-winning goal: the (conceded + 1)-th goal of the winner.
    const bool decisive = own_final > other_final && own_before == other_final;
    GoalCandidate candidate{
        league_id,
        report.date,
        event->player,
        team_id,
        opponent_id,
        event->minute,
        Awards::goalScore(event->minute, own_before, other_before, decisive,
                          reputation(opponent_id) - reputation(team_id),
                          event->assist != 0)};
    const auto found = month_goals.find(league_id);
    if (found == month_goals.end())
    {
      month_goals.try_emplace(league_id, candidate);
      continue;
    }
    const GoalCandidate& holder = found->second;
    const bool wins =
        candidate.score > holder.score ||
        (candidate.score == holder.score &&
         (candidate.date < holder.date ||
          (candidate.date == holder.date && candidate.scorer < holder.scorer)));
    if (wins) found->second = candidate;
  }
}

std::vector<AwardCandidate> AwardSystem::candidates(
    const GameData& gamedata, const PlayerTallies& tallies,
    LeagueID league_id) const
{
  std::vector<AwardCandidate> result;
  for (auto it = tallies.lower_bound({league_id, 0});
       it != tallies.end() && it->first.first == league_id; ++it)
  {
    const auto player = gamedata.getPlayer(it->second.player_id);
    if (!player) continue;
    result.emplace_back(player->get().getRole(), player->get().getAge(),
                        it->second);
  }
  return result;
}

void AwardSystem::addRecord(const GameData& gamedata, AwardRecord record,
                            std::vector<AwardRecord>& given)
{
  if (record.name.empty())
  {
    if (record.player_id != 0)
    {
      const auto player = gamedata.getPlayer(record.player_id);
      record.name = player ? player->get().getName() : std::string("–");
    }
    else if (const auto team = gamedata.getTeam(record.team_id))
    {
      record.name = team->get().getName();
    }
  }
  records.push_back(record);
  given.push_back(std::move(record));
}

std::vector<AwardRecord> AwardSystem::awardMonth(const GameData& gamedata,
                                                 std::uint16_t season_year,
                                                 std::uint8_t month)
{
  std::set<LeagueID> leagues;
  for (const auto& [key, tally] : month_players) leagues.insert(key.first);
  for (const auto& [team_id, club] : month_clubs)
    leagues.insert(club.league_id);

  std::vector<AwardRecord> given;
  for (const LeagueID league_id : leagues)
  {
    const std::vector<AwardCandidate> pool =
        candidates(gamedata, month_players, league_id);
    const auto playerRecord = [&](AwardType type, const AwardCandidate& winner)
    {
      AwardRecord record;
      record.season_year = season_year;
      record.month = month;
      record.league_id = league_id;
      record.type = type;
      record.player_id = winner.tally.player_id;
      record.team_id = winner.tally.team_id;
      record.value = std::round(winner.tally.averageRating() * 100.0f) / 100.0f;
      record.count = winner.tally.appearances;
      addRecord(gamedata, record, given);
    };
    if (const auto best = Awards::bestPlayer(pool, Awards::MONTH_MIN_MINUTES);
        best.has_value())
      playerRecord(AwardType::PlayerOfMonth, pool[*best]);
    if (const auto best = Awards::bestPlayer(pool, Awards::MONTH_MIN_MINUTES,
                                             Awards::YOUNG_MAX_AGE);
        best.has_value())
      playerRecord(AwardType::YoungPlayerOfMonth, pool[*best]);

    std::vector<AwardClubTally> clubs;
    for (const auto& [team_id, club] : month_clubs)
      if (club.league_id == league_id) clubs.push_back(club);
    if (const auto best = Awards::bestManager(clubs, Awards::MONTH_MIN_MATCHES);
        best.has_value())
    {
      AwardRecord record;
      record.season_year = season_year;
      record.month = month;
      record.league_id = league_id;
      record.type = AwardType::ManagerOfMonth;
      record.team_id = clubs[*best].team_id;
      record.value = std::round(clubs[*best].overPerformance() * 10.0f) / 10.0f;
      record.count = clubs[*best].matches;
      addRecord(gamedata, record, given);
    }

    if (const auto goal = month_goals.find(league_id);
        goal != month_goals.end() && gamedata.getPlayer(goal->second.scorer))
    {
      AwardRecord record;
      record.season_year = season_year;
      record.month = month;
      record.league_id = league_id;
      record.type = AwardType::GoalOfMonth;
      record.player_id = goal->second.scorer;
      record.team_id = goal->second.team_id;
      record.opponent_id = goal->second.opponent_id;
      record.value = goal->second.score;
      record.count = goal->second.minute;
      record.date = goal->second.date;
      addRecord(gamedata, record, given);
    }
  }
  month_players.clear();
  month_clubs.clear();
  month_goals.clear();
  return given;
}

std::vector<AwardRecord> AwardSystem::awardSeason(const GameData& gamedata,
                                                  std::uint16_t season_year)
{
  std::set<LeagueID> leagues;
  for (const auto& [key, tally] : season_players) leagues.insert(key.first);
  for (const auto& [team_id, club] : season_clubs)
    leagues.insert(club.league_id);

  std::vector<AwardRecord> given;
  for (const LeagueID league_id : leagues)
  {
    const std::vector<AwardCandidate> pool =
        candidates(gamedata, season_players, league_id);
    const std::uint16_t min_minutes = Awards::seasonMinMinutes(pool);
    const auto playerRecord = [&](AwardType type, const AwardCandidate& winner,
                                  float value, std::uint16_t count,
                                  std::uint8_t slot = 0)
    {
      AwardRecord record;
      record.season_year = season_year;
      record.league_id = league_id;
      record.type = type;
      record.player_id = winner.tally.player_id;
      record.team_id = winner.tally.team_id;
      record.value = value;
      record.count = count;
      record.slot = slot;
      addRecord(gamedata, record, given);
    };
    const auto rating = [](const AwardCandidate& candidate)
    { return std::round(candidate.tally.averageRating() * 100.0f) / 100.0f; };
    if (const auto best = Awards::bestPlayer(pool, min_minutes);
        best.has_value())
      playerRecord(AwardType::PlayerOfSeason, pool[*best], rating(pool[*best]),
                   pool[*best].tally.appearances);
    if (const auto best =
            Awards::bestPlayer(pool, min_minutes, Awards::YOUNG_MAX_AGE);
        best.has_value())
      playerRecord(AwardType::YoungPlayerOfSeason, pool[*best],
                   rating(pool[*best]), pool[*best].tally.appearances);
    if (const auto best = Awards::goldenBoot(pool); best.has_value())
      playerRecord(AwardType::GoldenBoot, pool[*best],
                   static_cast<float>(pool[*best].tally.goals),
                   pool[*best].tally.appearances);
    if (const auto best = Awards::goldenGlove(pool, min_minutes);
        best.has_value())
      playerRecord(AwardType::GoldenGlove, pool[*best],
                   static_cast<float>(pool[*best].tally.clean_sheets),
                   pool[*best].tally.appearances);
    const auto team = Awards::teamOfSeason(pool, min_minutes);
    for (std::uint8_t slot = 0; slot < team.size(); ++slot)
      if (team[slot].has_value())
        playerRecord(AwardType::TeamOfSeason, pool[*team[slot]],
                     rating(pool[*team[slot]]),
                     pool[*team[slot]].tally.appearances, slot);

    std::vector<AwardClubTally> clubs;
    for (const auto& [team_id, club] : season_clubs)
      if (club.league_id == league_id) clubs.push_back(club);
    if (const auto best =
            Awards::bestManager(clubs, Awards::SEASON_MIN_MATCHES);
        best.has_value())
    {
      AwardRecord record;
      record.season_year = season_year;
      record.league_id = league_id;
      record.type = AwardType::ManagerOfSeason;
      record.team_id = clubs[*best].team_id;
      record.value = std::round(clubs[*best].overPerformance() * 10.0f) / 10.0f;
      record.count = clubs[*best].matches;
      addRecord(gamedata, record, given);
    }
  }
  season_players.clear();
  season_clubs.clear();
  return given;
}

std::vector<AwardRecord> AwardSystem::onMonthStart(GameData& gamedata,
                                                   const GameDateValue& date,
                                                   TeamID managed_team_id,
                                                   Inbox& inbox,
                                                   BoardState& board)
{
  if (date.day != 1) return {};
  const GameDateValue previous = date - 1;
  const std::uint16_t season_year = seasonOf(previous);
  std::vector<AwardRecord> given =
      awardMonth(gamedata, season_year, previous.month);
  const std::size_t monthly_count = given.size();
  if (date.month == 6)
  {
    std::vector<AwardRecord> season = awardSeason(gamedata, season_year);
    given.insert(given.end(), season.begin(), season.end());
  }
  if (given.empty()) return given;

  // Effects: winners' morale, the board, the best manager's club.
  auto& players = gamedata.getPlayers();
  for (const AwardRecord& record : given)
  {
    if (record.player_id != 0)
    {
      const auto found = players.find(record.player_id);
      if (found == players.end()) continue;
      float lift = SEASON_MORALE;
      if (record.month != 0) lift = MONTH_MORALE;
      if (record.type == AwardType::TeamOfSeason) lift = TEAM_OF_SEASON_MORALE;
      PlayerDynamics& dynamics = found->second.mutableDynamics();
      dynamics.morale = std::min(100.0f, dynamics.morale + lift);
      continue;
    }
    if (record.team_id == managed_team_id && board.team_id == managed_team_id)
      board.confidence = std::min(
          100.0f, board.confidence + (record.month != 0 ? MONTH_CONFIDENCE
                                                        : SEASON_CONFIDENCE));
    if (record.type == AwardType::ManagerOfSeason)
      if (auto team = gamedata.getTeam(record.team_id))
      {
        ClubProfile profile = team->get().getProfile();
        profile.reputation =
            static_cast<std::uint8_t>(std::min(100, profile.reputation + 1));
        team->get().setProfile(profile);
      }
  }

  // News for the managed club's league only; unread when the club won.
  const auto managed = gamedata.getTeam(managed_team_id);
  if (managed_team_id == FREE_AGENTS_TEAM_ID || !managed) return given;
  const LeagueID managed_league = managed->get().getLeagueId();
  const auto league = gamedata.getLeague(managed_league);
  const std::string league_name =
      league ? Competitions::leagueNameArg(league->get()) : std::string();
  const auto post = [&](std::size_t from, std::size_t to, bool season)
  {
    std::vector<const AwardRecord*> mine;
    bool won = false;
    for (std::size_t index = from; index < to; ++index)
    {
      if (given[index].league_id != managed_league) continue;
      mine.push_back(&given[index]);
      won = won || given[index].team_id == managed_team_id;
    }
    if (mine.empty()) return;
    const auto line = [&](AwardType type, auto format) -> std::string
    {
      const AwardRecord* record = findType(mine, type);
      return record ? format(*record) : std::string(NOT_AWARDED);
    };
    const auto player = [&](const AwardRecord& r)
    { return playerLine(r, gamedata); };
    const auto count = [&](const AwardRecord& r)
    { return countLine(r, gamedata); };
    InboxMessage message;
    message.date = date;
    message.category = InboxCategory::General;
    message.read = !won;
    message.team_id = managed_team_id;
    if (!season)
    {
      message.title_key = "INBOX_AWARDS_MONTH_TITLE";
      message.body_key = "INBOX_AWARDS_MONTH_BODY";
      message.args = {league_name,
                      std::string("@") + MONTH_KEYS[previous.month - 1U],
                      std::to_string(previous.year),
                      line(AwardType::PlayerOfMonth, player),
                      line(AwardType::YoungPlayerOfMonth, player),
                      line(AwardType::ManagerOfMonth, managerLine),
                      line(AwardType::GoalOfMonth, [&](const AwardRecord& r)
                           { return goalLine(r, gamedata); })};
    }
    else
    {
      std::string team;
      for (const AwardRecord* record : mine)
      {
        if (record->type != AwardType::TeamOfSeason) continue;
        if (!team.empty()) team += ", ";
        team += record->name;
      }
      message.title_key = "INBOX_AWARDS_SEASON_TITLE";
      message.body_key = "INBOX_AWARDS_SEASON_BODY";
      message.args = {
          league_name,
          std::format("{}/{:02}", season_year, (season_year + 1) % 100),
          line(AwardType::PlayerOfSeason, player),
          line(AwardType::YoungPlayerOfSeason, player),
          line(AwardType::GoldenBoot, count),
          line(AwardType::GoldenGlove, count),
          line(AwardType::ManagerOfSeason, managerLine),
          team.empty() ? std::string(NOT_AWARDED) : team};
    }
    inbox.add(std::move(message));
  };
  post(0, monthly_count, false);
  post(monthly_count, given.size(), true);
  return given;
}

std::vector<AwardRecord> AwardSystem::honoursFor(PlayerID player_id) const
{
  std::vector<AwardRecord> result;
  for (auto it = records.rbegin(); it != records.rend(); ++it)
    if (it->player_id == player_id && player_id != 0) result.push_back(*it);
  return result;
}

std::vector<AwardRecord> AwardSystem::forLeague(LeagueID league_id,
                                                std::uint16_t season_year) const
{
  std::vector<AwardRecord> result;
  for (const AwardRecord& record : records)
    if (record.league_id == league_id && record.season_year == season_year)
      result.push_back(record);
  return result;
}

int AwardSystem::seasonHonours(PlayerID player_id, TeamID team_id) const
{
  return static_cast<int>(std::ranges::count_if(
      records,
      [&](const AwardRecord& record)
      {
        return record.player_id == player_id && record.team_id == team_id &&
               isSeasonAward(record.type);
      }));
}

float AwardSystem::valueMultiplier(PlayerID player_id,
                                   const GameDateValue& today) const
{
  std::vector<AwardRecord> honours;
  for (const AwardRecord& record : records)
    if (record.player_id == player_id && !isManagerAward(record.type))
      honours.push_back(record);
  return Awards::valueMultiplier(honours, today);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void AwardSystem::clear()
{
  records.clear();
  month_players.clear();
  season_players.clear();
  month_clubs.clear();
  season_clubs.clear();
  month_goals.clear();
}

void AwardSystem::load(const std::shared_ptr<DatabaseConnection>& db_conn)
{
  using namespace SqliteRows;
  clear();
  const DatabaseConnection& db = *db_conn;
  forEach(db,
          "SELECT season_year, month, league_id, type, player_id, team_id, "
          "opponent_id, name, value, count, slot, match_date FROM AwardHistory "
          "ORDER BY rowid;",
          [this](sqlite3_stmt* stmt)
          {
            const auto type = column<std::uint8_t>(stmt, 3);
            if (type >= std::to_underlying(AwardType::COUNT)) return;
            AwardRecord record;
            record.season_year = column<std::uint16_t>(stmt, 0);
            record.month = column<std::uint8_t>(stmt, 1);
            record.league_id = column<LeagueID>(stmt, 2);
            record.type = static_cast<AwardType>(type);
            record.player_id = column<PlayerID>(stmt, 4);
            record.team_id = column<TeamID>(stmt, 5);
            record.opponent_id = column<TeamID>(stmt, 6);
            record.name = columnText(stmt, 7);
            record.value = columnFloat(stmt, 8);
            record.count = column<std::uint16_t>(stmt, 9);
            record.slot = column<std::uint8_t>(stmt, 10);
            if (const std::string date = columnText(stmt, 11); !date.empty())
              record.date = GameDateValue::fromString(date);
            records.push_back(std::move(record));
          });
  forEach(
      db,
      "SELECT scope, league_id, player_id, team_id, appearances, minutes, "
      "goals, assists, clean_sheets, rating_total, rated FROM AwardTallies;",
      [this](sqlite3_stmt* stmt)
      {
        AwardPlayerTally tally;
        tally.league_id = column<LeagueID>(stmt, 1);
        tally.player_id = column<PlayerID>(stmt, 2);
        tally.team_id = column<TeamID>(stmt, 3);
        tally.appearances = column<std::uint16_t>(stmt, 4);
        tally.minutes = column<std::uint16_t>(stmt, 5);
        tally.goals = column<std::uint16_t>(stmt, 6);
        tally.assists = column<std::uint16_t>(stmt, 7);
        tally.clean_sheets = column<std::uint16_t>(stmt, 8);
        tally.rating_total = columnFloat(stmt, 9);
        tally.rated = column<std::uint16_t>(stmt, 10);
        auto& tallies =
            column<int>(stmt, 0) == 0 ? month_players : season_players;
        tallies[{tally.league_id, tally.player_id}] = tally;
      });
  forEach(db,
          "SELECT scope, team_id, league_id, matches, points, expected_points, "
          "goals_for, goals_against FROM AwardClubTallies;",
          [this](sqlite3_stmt* stmt)
          {
            AwardClubTally club;
            club.team_id = column<TeamID>(stmt, 1);
            club.league_id = column<LeagueID>(stmt, 2);
            club.matches = column<std::uint16_t>(stmt, 3);
            club.points = columnFloat(stmt, 4);
            club.expected_points = columnFloat(stmt, 5);
            club.goals_for = column<std::uint16_t>(stmt, 6);
            club.goals_against = column<std::uint16_t>(stmt, 7);
            auto& tallies =
                column<int>(stmt, 0) == 0 ? month_clubs : season_clubs;
            tallies[club.team_id] = club;
          });
  forEach(db,
          "SELECT league_id, match_date, player_id, team_id, opponent_id, "
          "minute, score FROM AwardGoals;",
          [this](sqlite3_stmt* stmt)
          {
            GoalCandidate goal;
            goal.league_id = column<LeagueID>(stmt, 0);
            goal.date = GameDateValue::fromString(columnText(stmt, 1));
            goal.scorer = column<PlayerID>(stmt, 2);
            goal.team_id = column<TeamID>(stmt, 3);
            goal.opponent_id = column<TeamID>(stmt, 4);
            goal.minute = column<std::uint8_t>(stmt, 5);
            goal.score = columnFloat(stmt, 6);
            month_goals[goal.league_id] = goal;
          });
}

void AwardSystem::save(const std::shared_ptr<DatabaseConnection>& db_conn) const
{
  using namespace SqliteRows;
  const DatabaseConnection& db = *db_conn;
  for (const char* table :
       {"AwardHistory", "AwardTallies", "AwardClubTallies", "AwardGoals"})
    clearTable(db, table);
  insertAll(db,
            "INSERT INTO AwardHistory (season_year, month, league_id, type, "
            "player_id, team_id, opponent_id, name, value, count, slot, "
            "match_date) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
            records,
            [](sqlite3_stmt* row, const AwardRecord& record)
            {
              sqlite3_bind_int(row, 1, record.season_year);
              sqlite3_bind_int(row, 2, record.month);
              sqlite3_bind_int(row, 3, static_cast<int>(record.league_id));
              sqlite3_bind_int(
                  row, 4, static_cast<int>(std::to_underlying(record.type)));
              sqlite3_bind_int64(row, 5, record.player_id);
              sqlite3_bind_int(row, 6, record.team_id);
              sqlite3_bind_int(row, 7, record.opponent_id);
              bindText(row, 8, record.name);
              sqlite3_bind_double(row, 9, record.value);
              sqlite3_bind_int(row, 10, record.count);
              sqlite3_bind_int(row, 11, record.slot);
              bindText(row, 12,
                       record.type == AwardType::GoalOfMonth
                           ? record.date.toString()
                           : std::string());
            });
  const auto savePlayers = [&](const PlayerTallies& tallies, int scope)
  {
    insertAll(db,
              "INSERT INTO AwardTallies (scope, league_id, player_id, team_id, "
              "appearances, minutes, goals, assists, clean_sheets, "
              "rating_total, rated) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
              tallies,
              [scope](sqlite3_stmt* row, const auto& entry)
              {
                const AwardPlayerTally& tally = entry.second;
                sqlite3_bind_int(row, 1, scope);
                sqlite3_bind_int(row, 2, static_cast<int>(tally.league_id));
                sqlite3_bind_int64(row, 3, tally.player_id);
                sqlite3_bind_int(row, 4, tally.team_id);
                sqlite3_bind_int(row, 5, tally.appearances);
                sqlite3_bind_int(row, 6, tally.minutes);
                sqlite3_bind_int(row, 7, tally.goals);
                sqlite3_bind_int(row, 8, tally.assists);
                sqlite3_bind_int(row, 9, tally.clean_sheets);
                sqlite3_bind_double(row, 10, tally.rating_total);
                sqlite3_bind_int(row, 11, tally.rated);
              });
  };
  savePlayers(month_players, 0);
  savePlayers(season_players, 1);
  const auto saveClubs = [&](const ClubTallies& tallies, int scope)
  {
    insertAll(db,
              "INSERT INTO AwardClubTallies (scope, team_id, league_id, "
              "matches, points, expected_points, goals_for, goals_against) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
              tallies,
              [scope](sqlite3_stmt* row, const auto& entry)
              {
                const AwardClubTally& club = entry.second;
                sqlite3_bind_int(row, 1, scope);
                sqlite3_bind_int(row, 2, club.team_id);
                sqlite3_bind_int(row, 3, static_cast<int>(club.league_id));
                sqlite3_bind_int(row, 4, club.matches);
                sqlite3_bind_double(row, 5, club.points);
                sqlite3_bind_double(row, 6, club.expected_points);
                sqlite3_bind_int(row, 7, club.goals_for);
                sqlite3_bind_int(row, 8, club.goals_against);
              });
  };
  saveClubs(month_clubs, 0);
  saveClubs(season_clubs, 1);
  insertAll(
      db,
      "INSERT INTO AwardGoals (league_id, match_date, player_id, "
      "team_id, opponent_id, minute, score) VALUES (?, ?, ?, ?, ?, ?, ?);",
      month_goals,
      [](sqlite3_stmt* row, const auto& entry)
      {
        const GoalCandidate& goal = entry.second;
        sqlite3_bind_int(row, 1, static_cast<int>(goal.league_id));
        bindText(row, 2, goal.date.toString());
        sqlite3_bind_int64(row, 3, goal.scorer);
        sqlite3_bind_int(row, 4, goal.team_id);
        sqlite3_bind_int(row, 5, goal.opponent_id);
        sqlite3_bind_int(row, 6, goal.minute);
        sqlite3_bind_double(row, 7, goal.score);
      });
}
