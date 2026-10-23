// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "global/types.h"
#include "model/awards.h"
#include "model/board.h"
#include "model/club_economy.h"
#include "model/facility_projects.h"
#include "model/gamedate.h"
#include "model/holiday.h"
#include "model/inbox.h"
#include "model/interactions.h"
#include "model/mentoring.h"
#include "model/preseason.h"
#include "model/records.h"
#include "model/scouting.h"
#include "model/squad_status.h"
#include "model/stories.h"
#include "model/training.h"
#include "model/youth_academy.h"

class DatabaseConnection;
class GameData;
struct SeasonHistoryEntry;
class Match;
class Player;
class Team;
struct MatchReport;

/**
 * @struct PlayerMatchConsequence
 * @brief Physical outcome of one player's match.
 *
 * - end_condition < 0: the condition drain is estimated from minutes and
 *   stamina and the injury hazard is rolled here (ECIS match incidence).
 * - end_condition >= 0: the match engine measured the condition and owns
 *   injuries; set injured=true for players it injured and the diagnosis and
 *   layoff are drawn here.
 */
struct PlayerMatchConsequence
{
  PlayerID player_id = 0;
  std::uint8_t minutes_played = 0;
  float end_condition = -1.0f;
  bool injured = false;
};

/**
 * @enum SquadRole
 * @brief Playing-time expectation derived from ability rank in the squad.
 */
enum class SquadRole : std::uint8_t
{
  KeyPlayer, /*!< Top 5: expects to start nearly every match. */
  FirstTeam, /*!< Rank 6-11: regular starter. */
  Rotation,  /*!< Rank 12-16. */
  Backup,    /*!< Rank 17-22. */
  Fringe     /*!< Beyond the matchday squad (young players: prospect). */
};

/**
 * @struct UpcomingFixture
 * @brief A fixture of the managed club, from the club's point of view.
 */
struct UpcomingFixture
{
  GameDateValue date;
  TeamID opponent_id = 0;
  bool home = true;
  MatchType type = MatchType::FRIENDLY;
};

/** Language key naming @p role (e.g. "SQUAD_ROLE_KEY_PLAYER"). */
const char* squadRoleKey(SquadRole role);

/**
 * @class WorldSimulation
 * @brief Everything that happens between matches.
 *
 * Owns the managed club's inbox and board state and processes, from the
 * Game's day loop: condition recovery, training and match injuries, weekly
 * payroll, training development, morale, monthly revenue and costs, the
 * March youth intake and season-end prize money, contract renewals and
 * retirements. All randomness is derived from the save's world seed.
 */
class WorldSimulation
{
 public:
  explicit WorldSimulation(std::shared_ptr<GameData> gamedata);

  // ---- Hooks called by Game ----

  /** Daily/weekly/monthly processing; call once per new date. */
  void onDayAdvanced(const GameDateValue& date, TeamID managed_team_id);

  /**
   * Applies the consequences of a played match: condition, sharpness,
   * injuries, ratings, morale, recent form, gate receipts (written to
   * report.attendance), board confidence and news. Participants come from
   * report.players (starters of the line-ups when empty); ratings of 0 are
   * estimated. Players whose physical consequences were already applied on
   * the match date through applyMatchConsequences() are not drained twice.
   */
  void onMatchPlayed(const Match& match, MatchReport& report,
                     TeamID managed_team_id);

  /** Prize money, reputation, board review, renewals and retirements. */
  void onSeasonEnd(const GameDateValue& date, TeamID managed_team_id);

  /** Budgets, ticket prices, season counters and the board objective. */
  void onSeasonStart(const GameDateValue& date, TeamID managed_team_id);

  /**
   * The manager took charge of @p team_id: the board sets the objective and
   * budgets (so they are known on day one) and the inbox receives the
   * board's welcome, the assistant's squad report, the pre-season schedule
   * (from @p schedule, the club's next fixtures up to the first competitive
   * match) and a scouting suggestion. Nothing happens when the club was
   * already managed.
   */
  void onManagedTeamSelected(const GameDateValue& date, TeamID team_id,
                             const std::vector<UpcomingFixture>& schedule);

  /**
   * The manager left the managed club (sacked, resigned or moved): the
   * board, cash warnings and the shortlist belong to that club and are
   * dropped. The inbox stays; the next club's welcome joins it.
   */
  void onManagerLeft();

  /**
   * True while the board freezes the managed club's transfer spending: its
   * cash has been negative since at least the previous monthly review (a
   * warning comes first). Lifts as soon as the balance is positive again.
   * Other clubs are never embargoed (their budget is already zero without
   * cash).
   */
  bool isTransferEmbargoed(TeamID team_id) const;

  /** Loads the inbox and board state (after GameData is loaded). */
  void load(const std::shared_ptr<DatabaseConnection>& db_conn);

  /** Writes inbox, board, world state and retired players (in a
   * transaction owned by the caller). */
  void save(const std::shared_ptr<DatabaseConnection>& db_conn) const;

  /** Marks ledgers and removals as persisted after a successful commit. */
  void onSaved();

  // ---- Per-player match pipeline ----

  /**
   * Applies one player's physical match consequences on @p date (condition,
   * sharpness, minutes, injuries). Idempotent per player and day: returns
   * false if already applied for that date or the player is unknown.
   */
  bool applyMatchConsequences(const GameDateValue& date,
                              const PlayerMatchConsequence& consequence,
                              TeamID managed_team_id);

  /**
   * Supplies final league tables (1st place first) for season-end prize
   * money, board reviews and reputation. Without a provider clubs are
   * ordered by points.
   */
  void setStandingsProvider(
      std::function<std::vector<TeamID>(LeagueID)> provider);

  /**
   * Supplies the days to every club's next fixture (within a week) so that
   * training follows the real match rhythm. Without a provider a weekly
   * rhythm after each club's last match is assumed.
   */
  void setFixtureOutlookProvider(
      std::function<void(const GameDateValue&, TrainingSystem::FixtureOutlook&)>
          provider);

  /**
   * Supplies an extra injury-risk multiplier (>= 1) per player and day, e.g.
   * after long international trips. Without a provider it is 1.
   */
  void setInjuryRiskProvider(
      std::function<double(PlayerID, const GameDateValue&)> provider);

  /**
   * Tells whether a player is on loan at his current club; loanees'
   * contracts belong to the parent club, so AI squad planning leaves them
   * alone.
   */
  void setLoanCheck(std::function<bool(PlayerID)> is_on_loan);

  /**
   * Final outcomes of the finished seasons: who went down feeds parachute
   * payments and relegation wage clauses. Without a provider neither is
   * applied.
   */
  void setSeasonHistoryProvider(
      std::function<const std::vector<SeasonHistoryEntry>&()> provider);

  /**
   * Free agents the manager is dealing with (talks, a pre-contract) stay in
   * the pool until that is settled.
   */
  void setFreeAgentHold(std::function<bool(PlayerID)> is_held);

  /**
   * Takes a departing player out of the managed club's line-up and fills
   * only his place (the rest of the manager's selection stays). Called for
   * managed players who retire, before they are erased. Without a filler
   * the line-up is rebuilt.
   */
  void setLineupGapFiller(std::function<void(Team&, const Player&)> filler);

  // ---- Transfer events (called by the controller) ----

  /** A club bid for a player; unsettles ambitious players. */
  void onTransferBid(const GameDateValue& date, PlayerID player_id,
                     TeamID bidder_id, std::uint32_t amount,
                     TeamID managed_team_id);

  /** A transfer completed. */
  void onTransferCompleted(const GameDateValue& date, PlayerID player_id,
                           TeamID from_team_id, TeamID to_team_id,
                           std::uint32_t fee, TeamID managed_team_id);

  // ---- Queries ----

  const Inbox& getInbox() const { return inbox; }
  Inbox& getInbox() { return inbox; }
  const BoardState& getBoardState() const { return board; }
  /** Moves the board's confidence in the manager by @p delta points
   * (clamped to 0-100); no-op without a managed club. */
  void adjustBoardConfidence(float delta);
  /** Merit money the managed club earned at the latest season end. */
  std::int64_t getSeasonPrizeMoney() const { return season_prize_money; }

  /** Scouting knowledge, assignments, reports and shortlist. */
  ScoutingSystem& getScouting() { return scouting; }
  const ScoutingSystem& getScouting() const { return scouting; }

  /** Conversations, promises, team talks and the dressing room. */
  InteractionSystem& getInteractions() { return interactions; }
  const InteractionSystem& getInteractions() const { return interactions; }

  /** Story chains of the managed club. */
  StoryEngine& getStories() { return stories; }
  const StoryEngine& getStories() const { return stories; }

  /** Academies: intake cycle, U18 squads and academy projects. */
  YouthAcademy& getYouth() { return youth; }
  const YouthAcademy& getYouth() const { return youth; }

  /** Squad statuses the manager gave players of his club. */
  SquadStatusBook& getSquadStatuses() { return squad_statuses; }
  const SquadStatusBook& getSquadStatuses() const { return squad_statuses; }

  /** League honours, records book, facility projects, pre-season plan,
   * mentoring groups and holiday preferences. */
  AwardSystem& getAwards() { return awards; }
  const AwardSystem& getAwards() const { return awards; }
  RecordBook& getRecords() { return records; }
  const RecordBook& getRecords() const { return records; }
  FacilityProjects& getFacilityProjects() { return facility_projects; }
  const FacilityProjects& getFacilityProjects() const
  {
    return facility_projects;
  }
  PreseasonPlanner& getPreseason() { return preseason; }
  const PreseasonPlanner& getPreseason() const { return preseason; }
  MentoringSystem& getMentoring() { return mentoring; }
  const MentoringSystem& getMentoring() const { return mentoring; }
  HolidayPreferences& getHolidayPreferences() { return holiday_preferences; }
  const HolidayPreferences& getHolidayPreferences() const
  {
    return holiday_preferences;
  }

  /**
   * Playing-time expectation of a player in his squad: the status the
   * manager gave him (managed club), else derived from his ability rank.
   */
  SquadRole squadRole(PlayerID player_id) const;

  /** Crowd of the club's latest home match (0 if none this session). */
  std::uint32_t lastHomeAttendance(TeamID team_id) const;

  /** League position of a club by points (1-based, 0 if unknown); a cheap
   * approximation used during the season. */
  int leaguePosition(TeamID team_id) const;

  /** Mean overall of the club's selected XI. */
  float lineupStrength(TeamID team_id) const;

 private:
  struct FocusStats
  {
    std::vector<std::string> all;
    double all_weight = 1.0;
    std::vector<std::string> non_physical;
    double non_physical_weight = 1.0;
  };

  void processDaily(const GameDateValue& date, std::int32_t ordinal,
                    TeamID managed_team_id);
  void processWeekly(const GameDateValue& date, TeamID managed_team_id);
  void reviewCash(const GameDateValue& date, TeamID managed_team_id,
                  bool monthly);
  /** An AI club deep in the red may be recapitalised by its owner. */
  void rescueByOwner(const GameDateValue& date, Team& team) const;
  /**
   * Parachute money per season of the clubs that went down from a top
   * division in the last two finished seasons and are still below it.
   * @param relegated_now Receives the clubs that went down at the end of
   * the latest finished season.
   */
  std::unordered_map<TeamID, double> parachutes(
      const std::unordered_map<LeagueID, LeagueEconomy>& economies,
      std::vector<TeamID>* relegated_now = nullptr) const;
  /** Relegation clauses: AI players of relegated clubs take a wage cut. */
  void applyRelegationWageCuts(const std::vector<TeamID>& relegated,
                               TeamID managed_team_id);
  /** Unsigned free agents leave the world (see WorldTuning::FreeAgents). */
  void clearFreeAgentPool(const GameDateValue& date);
  void postMedicalDigest(const GameDateValue& date, TeamID managed_team_id);
  void postSquadReport(const GameDateValue& date, const Team& team);
  void postPreseasonSchedule(const GameDateValue& date, const Team& team,
                             const std::vector<UpcomingFixture>& schedule);
  void postScoutSuggestion(const GameDateValue& date, const Team& team);
  void processMonthly(const GameDateValue& date, TeamID managed_team_id);
  void sendContractNotices(const GameDateValue& date, TeamID managed_team_id);
  void awardPrizeMoney(const GameDateValue& date, TeamID managed_team_id);
  void renewAiContracts(TeamID managed_team_id);
  void trimAiSquads(TeamID managed_team_id);
  void retirePlayers(const GameDateValue& date, TeamID managed_team_id);
  void ensureBoard(const GameDateValue& date, TeamID managed_team_id,
                   bool new_season, bool post_objective = true);
  void developPlayer(Player& player, float training_quality,
                     std::int32_t ordinal);
  void updateWeeklyMorale(Team& team);
  void injure(Player& player, const GameDateValue& date, std::int32_t ordinal,
              bool in_match, TeamID managed_team_id);
  void recordGateReceipts(const Match& match);
  void postTrainingWarning(const GameDateValue& date, TeamID managed_team_id);
  void refreshAiLineups(TeamID managed_team_id);
  void dropUnavailableFromAiLineups(TeamID managed_team_id);
  std::vector<TeamID> standings(LeagueID league_id) const;
  std::vector<TeamID> pointsOrder(LeagueID league_id) const;
  double sportingSuccess(const Team& team) const;
  void post(const GameDateValue& date, InboxCategory category,
            std::string title_key, std::string body_key,
            std::vector<std::string> args,
            std::optional<PlayerID> player_id = std::nullopt,
            std::optional<TeamID> team_id = std::nullopt, bool read = false);
  const FocusStats& focusFor(const Player& player) const;
  static std::uint16_t seasonYear(const GameDateValue& date);

  std::shared_ptr<GameData> gamedata;
  ScoutingSystem scouting;
  InteractionSystem interactions;
  StoryEngine stories;
  YouthAcademy youth;
  SquadStatusBook squad_statuses;
  AwardSystem awards;
  RecordBook records;
  FacilityProjects facility_projects;
  PreseasonPlanner preseason;
  MentoringSystem mentoring;
  HolidayPreferences holiday_preferences;
  Inbox inbox;
  BoardState board;
  mutable std::unordered_map<std::string, FocusStats> focus_by_category;
  std::unordered_map<TeamID, std::uint32_t> last_attendance;
  std::function<std::vector<TeamID>(LeagueID)> standings_provider;
  std::function<void(const GameDateValue&, TrainingSystem::FixtureOutlook&)>
      fixture_outlook_provider;
  std::function<double(PlayerID, const GameDateValue&)> injury_risk_provider;
  TrainingSystem::FixtureOutlook fixture_outlook;
  std::function<bool(PlayerID)> loan_check;
  std::function<const std::vector<SeasonHistoryEntry>&()>
      season_history_provider;
  std::function<bool(PlayerID)> free_agent_hold;
  std::function<void(Team&, const Player&)> lineup_gap_filler;
  std::int64_t season_prize_money = 0;
  std::unordered_set<TeamID> lineup_dirty;
  /** Managed-club medical news of the current week, sent as one digest. */
  std::vector<std::string> week_recoveries;
  std::vector<std::string> week_minor_injuries;
  /** Cash review of the managed club (rebuilt from the ledger on load). */
  bool cash_warning_sent = false;
  bool transfer_embargo = false;
};
