// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "global/types.h"
#include "model/gamedate.h"
#include "model/inbox.h"
#include "model/standings.h"

class Calendar;
class DatabaseConnection;
class GameData;
class Match;

/**
 * @brief Continental club competitions: rules, draws and tables.
 *
 * Every competition has a league phase (Swiss model: each club meets
 * `matches` different opponents, one at home and one away from each pot,
 * never a club of its own association and at most two of any other), after
 * which places 1-8 go straight to the round of 16, places 9-24 play a
 * two-legged play-off and the rest are out. Knockout ties have two legs
 * (no away goals; extra time and penalties when the aggregate is level) and
 * the final is a single match at a neutral venue.
 *
 * Matches are ordinary calendar fixtures with MatchType::CONTINENTAL, the
 * competition ID and a stage code (stageCode()).
 */
namespace Continental
{
enum class Continent : uint8_t
{
  Europe,
  Americas
};

enum class Round : uint8_t
{
  LeaguePhase = 0,
  Playoff,
  RoundOf16,
  QuarterFinal,
  SemiFinal,
  Final
};

/** Money paid to clubs (whole euros). */
struct Prizes
{
  int64_t participation = 0; /*!< League-phase starting fee. */
  int64_t win = 0;           /*!< Per league-phase win. */
  int64_t draw = 0;          /*!< Per league-phase draw. */
  int64_t ranking_step = 0;  /*!< Last place gets 1 step, first N steps. */
  int64_t playoff = 0;
  int64_t round_of_16 = 0;
  int64_t quarter_final = 0;
  int64_t semi_final = 0;
  int64_t final_fee = 0;
  int64_t winner_bonus = 0;
};

/** Format, access list, dates and prize table of one competition. */
struct CompetitionRules
{
  LeagueID id = 0;
  const char* name_key = "";
  Continent continent = Continent::Europe;
  uint8_t tier = 1;
  uint8_t clubs = 36;  /*!< League-phase clubs. */
  uint8_t matches = 8; /*!< League-phase matches per club (two per pot). */
  /** League-phase places by association rank (index 0 = best); the last
   * value applies to every lower rank. Free places are then shared out one
   * at a time in rank order. */
  std::array<uint8_t, 8> base_places{};
  /** Day offsets from the week's Tuesday for the two halves of a round. */
  std::array<uint8_t, 2> weekday_offsets{};
  /** Final: days after the last league Saturday (neutral venue). */
  int final_offset = 14;
  Prizes prizes;
  /** Coefficient bonuses: league-phase participation, best league-phase
   * ranking (decreasing linearly to 0) and knockout participation. */
  double participation_bonus = 0.0;
  double ranking_bonus = 0.0;
  double knockout_bonus = 0.0;
};

inline constexpr LeagueID CHAMPIONS_CUP_ID = 250;
inline constexpr LeagueID CONTINENTAL_SHIELD_ID = 251;
inline constexpr LeagueID AMERICAS_CUP_ID = 252;

/** Every competition of the world, tier order within each continent. */
inline constexpr std::array<CompetitionRules, 3> COMPETITIONS = {{
    {CHAMPIONS_CUP_ID,
     "CONT_CHAMPIONS_CUP",
     Continent::Europe,
     1,
     36,
     8,
     {4, 4, 4, 4, 4, 3, 2, 2},
     {0, 1},
     14,
     {18'620'000, 2'100'000, 700'000, 275'000, 1'000'000, 11'000'000,
      12'500'000, 15'000'000, 18'500'000, 6'500'000},
     6.0,
     12.0,
     1.5},
    {CONTINENTAL_SHIELD_ID,
     "CONT_SHIELD",
     Continent::Europe,
     2,
     24,
     6,
     {2, 2, 2, 2, 2, 2, 1, 1},
     {2, 2},
     4,
     {4'310'000, 450'000, 150'000, 75'000, 300'000, 1'750'000, 2'500'000,
      3'500'000, 6'000'000, 4'000'000},
     4.0,
     6.0,
     1.0},
    {AMERICAS_CUP_ID,
     "CONT_AMERICAS_CUP",
     Continent::Americas,
     1,
     24,
     6,
     {4, 4, 4, 4, 3, 3, 2, 2},
     {1, 0},
     14,
     {1'000'000, 300'000, 100'000, 40'000, 250'000, 1'150'000, 1'550'000,
      2'100'000, 6'000'000, 16'000'000},
     4.0,
     6.0,
     1.0},
}};

/** Rules of a competition ID; nullptr for anything else. */
const CompetitionRules* rules(LeagueID competition_id);

/** Continent of a league (its region), nullopt when it has none. */
std::optional<Continent> continentOf(LeagueID league_id);

/** League-phase places that go straight to the first knockout round. */
uint8_t directPlaces(uint8_t clubs);
/** League-phase places that enter the knockout play-off. */
uint8_t playoffPlaces(uint8_t clubs);
/** Round the direct places enter (round of 16, or quarter-finals). */
Round firstKnockoutRound(uint8_t clubs);

/** Match::stage of a league-phase matchday is 1-8; knockouts use codes. */
inline constexpr uint8_t KNOCKOUT_STAGE_BASE = 16;
uint8_t stageCode(Round round, uint8_t leg);
Round roundOf(uint8_t stage);
/** 1 or 2 (the final is leg 1). */
uint8_t legOf(uint8_t stage);
/** Language key naming a round (e.g. "CONT_ROUND_QUARTER_FINAL"). */
const char* roundKey(Round round);

/**
 * @brief League-phase places per association rank.
 * @param associations Number of associations of the continent.
 * @return Places for rank 1..n; they add up to rules.clubs unless the
 * clubs available (@p capacity per association) run out.
 */
std::vector<uint8_t> allocatePlaces(const CompetitionRules& rules,
                                    size_t associations,
                                    const std::vector<uint8_t>& capacity);

/** A club entering a league-phase draw. */
struct DrawTeam
{
  TeamID team_id = 0;
  uint32_t association = 0;
  uint8_t pot = 0; /*!< 0-based. */
};

/** One league-phase match; matchday is 1-based. */
struct LeagueFixture
{
  TeamID home_id = 0;
  TeamID away_id = 0;
  uint8_t matchday = 0;
};

/**
 * @brief Seeded Swiss-model draw.
 *
 * Teams are split into @p pots equal pots; every team meets two opponents
 * of each pot (one at home, one away), never one of its association and at
 * most two of any other, and plays once per matchday (2 * pots matchdays).
 * The search is a bounded randomised backtracking; if it cannot satisfy the
 * association limits it relaxes them (first the two-per-association limit,
 * then the own-association ban), so a valid schedule is always returned
 * for pot sizes of at least three.
 */
std::vector<LeagueFixture> drawLeaguePhase(const std::vector<DrawTeam>& teams,
                                           uint8_t pots, uint32_t seed);

/**
 * @brief League-phase table: points, goal difference, goals scored, away
 * goals scored, wins, away wins, then @p coefficient and team ID.
 */
std::vector<StandingRow> leaguePhaseTable(
    const std::vector<TeamID>& entrants,
    const std::vector<const Match*>& matches,
    const std::function<double(TeamID)>& coefficient);
}  // namespace Continental

/**
 * @class ContinentalCompetitions
 * @brief Seasons, draws, knockouts, prize money and coefficients of the
 * continental club competitions.
 *
 * Qualification comes from the final domestic tables (and cup winners) of
 * the previous season, association places from the association
 * coefficient ranking (club results of the last five seasons divided by the
 * clubs entered). The league-phase draw is an event in late August; each
 * knockout draw happens the day the previous round is complete.
 */
class ContinentalCompetitions
{
 public:
  static constexpr size_t COEFFICIENT_SEASONS = 5;

  struct Entrant
  {
    TeamID team_id = 0;
    LeagueID association = 0; /*!< Top league of the club's country. */
    uint8_t pot = 0;
    uint8_t league_position = 0; /*!< Domestic finish (0 = unknown). */
    bool cup_winner = false;
    double coefficient = 0.0;
  };

  /** A knockout tie; the seeded club plays the second leg at home. */
  struct Tie
  {
    Continental::Round round = Continental::Round::Playoff;
    TeamID seeded_id = 0;
    TeamID unseeded_id = 0;
    TeamID winner_id = 0; /*!< 0 until decided. */
  };

  struct DrawEvent
  {
    GameDateValue date;
    Continental::Round round = Continental::Round::LeaguePhase;
  };

  struct Season
  {
    LeagueID competition_id = 0;
    uint16_t season_year = 0;
    uint8_t clubs = 0;   /*!< League-phase size (may be scaled down). */
    uint8_t matches = 0; /*!< League-phase matches per club. */
    GameDateValue draw_date;
    bool drawn = false;
    bool league_phase_complete = false;
    std::vector<Entrant> entrants;
    std::vector<Tie> ties;
    std::vector<DrawEvent> draws;
    TeamID winner_id = 0;
    TeamID runner_up_id = 0;
  };

  /** Association coefficient: season points, newest first. */
  struct AssociationCoefficient
  {
    LeagueID association = 0;
    std::array<double, COEFFICIENT_SEASONS> seasons{};
    double total() const;
  };

  /** Knockout status of a tie derived from its fixtures. */
  struct TieScore
  {
    const Match* first_leg = nullptr;
    const Match* second_leg = nullptr; /*!< The final for single matches. */
    uint16_t seeded_goals = 0;
    uint16_t unseeded_goals = 0;
  };

  /** Message, clubs it concerns, and whether it is general news. */
  using NewsSink =
      std::function<void(InboxMessage, const std::vector<TeamID>&, bool)>;

  explicit ContinentalCompetitions(std::shared_ptr<GameData> gamedata);

  void setNewsSink(NewsSink sink) { news = std::move(sink); }

  /**
   * @brief Enters the qualified clubs of @p season_year and plans the
   * league-phase draw. Uses the qualification computed by closeSeason() or,
   * without one (first season, older saves), the clubs' reputation.
   * Nothing happens if the season already exists or if @p today is too
   * late to play the league phase.
   */
  void startSeason(uint16_t season_year, const GameDateValue& today);

  /** Draws that are due and knockout progress; call once per day. */
  void afterMatchday(Calendar& calendar, const GameDateValue& today);

  /** Prize money of a league-phase result; call once per played match. */
  void onResult(const Match& match);

  /** A second leg (or final) whose aggregate is level after 90 minutes. */
  bool needsExtraTime(const Calendar& calendar, const Match& match) const;

  /**
   * For a deciding match (a final, or a second leg whose first leg is
   * played): the home side's aggregate lead from the earlier leg (0 for a
   * final). The tie goes to extra time when the home side's goals plus this
   * lead equal the away side's goals. nullopt for any other match.
   */
  std::optional<int> deciderLead(const Calendar& calendar,
                                 const Match& match) const;

  /**
   * For a deciding match: the goals this match's home and away sides scored
   * in the earlier leg ({0, 0} for a final); nullopt for any other match.
   */
  std::optional<std::pair<int, int>> deciderAggregate(const Calendar& calendar,
                                                      const Match& match) const;

  /**
   * Settles a deciding match whose aggregate is level with extra time and
   * penalties; true if @p match changed.
   */
  bool resolveDecider(const Calendar& calendar, Match& match);

  /**
   * @brief Season end: updates coefficients from the season's matches and
   * computes the clubs qualified for the next season.
   * @param tables Final tables of the top divisions (1st first).
   * @param cup_winners Domestic cup winner per association (top league).
   */
  void closeSeason(
      const Calendar& calendar, uint16_t season_year,
      const std::unordered_map<LeagueID, std::vector<StandingRow>>& tables,
      const std::map<LeagueID, TeamID>& cup_winners);

  /** Rebuilds cached tables from the calendar (after loading). */
  void refresh(const Calendar& calendar);

  // ---------------- Queries ----------------
  const std::vector<Season>& getSeasons() const { return seasons; }
  const Season* getSeason(LeagueID competition_id) const;
  /** Cached league-phase table (empty before the draw). */
  const std::vector<StandingRow>& getTable(LeagueID competition_id) const;
  /** Aggregate score of a tie. */
  TieScore tieScore(const Calendar& calendar, const Season& season,
                    const Tie& tie) const;
  /** Associations of a continent, best coefficient first. */
  std::vector<AssociationCoefficient> associationRanking(
      Continental::Continent continent) const;
  /** Club coefficient: own points of five seasons or 20% of its
   * association's coefficient, whichever is higher. */
  double clubCoefficient(TeamID team_id) const;
  /** Competition a club plays this season, if any. */
  std::optional<LeagueID> competitionOf(TeamID team_id) const;
  /** Clubs qualified for next season (after closeSeason()). */
  const std::map<LeagueID, std::vector<Entrant>>& getQualified() const
  {
    return qualified;
  }

  // ---------------- Persistence ----------------
  void load(const DatabaseConnection& db);
  void save(const DatabaseConnection& db) const;
  std::string serialize() const;
  void deserialize(const std::string& data);

 private:
  Season* findSeason(LeagueID competition_id);
  std::vector<Entrant> qualifyByReputation(
      const Continental::CompetitionRules& rules,
      std::vector<TeamID>& taken) const;
  std::vector<Entrant> qualify(
      const Continental::CompetitionRules& rules,
      const std::unordered_map<LeagueID, std::vector<StandingRow>>& tables,
      const std::map<LeagueID, TeamID>& cup_winners,
      std::vector<TeamID>& taken) const;
  std::vector<LeagueID> rankedAssociations(
      Continental::Continent continent) const;
  AssociationCoefficient associationCoefficient(LeagueID association) const;
  std::vector<TeamID> topDivisionClubs(LeagueID association) const;
  bool isLowestTier(const Continental::CompetitionRules& rules) const;
  void assignPots(Season& season) const;
  void drawLeaguePhase(Calendar& calendar, Season& season,
                       const GameDateValue& today);
  void advanceKnockouts(Calendar& calendar, Season& season,
                        const GameDateValue& today);
  void drawKnockoutRound(Calendar& calendar, Season& season,
                         Continental::Round round, std::vector<TeamID> seeded,
                         std::vector<TeamID> unseeded,
                         const GameDateValue& today);
  void payPrize(TeamID team_id, const GameDateValue& date, int64_t amount);
  void postNews(const GameDateValue& date, std::string title_key,
                std::string body_key, std::vector<std::string> args,
                const std::vector<TeamID>& involved, bool headline) const;
  std::vector<const Match*> competitionMatches(const Calendar& calendar,
                                               LeagueID competition_id) const;
  const Match* findLeg(const Calendar& calendar, LeagueID competition_id,
                       uint8_t stage, TeamID home_id, TeamID away_id) const;
  std::string teamName(TeamID team_id) const;

  std::shared_ptr<GameData> gamedata;
  NewsSink news;
  std::vector<Season> seasons;
  std::map<LeagueID, AssociationCoefficient> associations;
  std::map<TeamID, std::array<double, COEFFICIENT_SEASONS>> clubs;
  /** Latest season whose results are in the coefficients. */
  uint16_t last_closed_season = 0;
  /** A result changed since the last knockout check. */
  bool results_dirty = true;
  std::map<LeagueID, std::vector<Entrant>> qualified;
  uint16_t qualified_season = 0;
  std::map<LeagueID, std::vector<StandingRow>> tables;
};
