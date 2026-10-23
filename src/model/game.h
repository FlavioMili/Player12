// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "database/database_connection.h"
#include "database/runtime_editor.h"
#include "model/calendar.h"
#include "model/competition_manager.h"
#include "model/gamedate.h"
#include "model/guidance.h"
#include "model/manager_career.h"
#include "model/match.h"
#include "model/match_report.h"
#include "model/match_scheduler.h"
#include "model/medical_centre.h"
#include "model/national_job.h"
#include "model/national_teams.h"
#include "model/season_review.h"
#include "model/supporters.h"
#include "model/transfer_market.h"
#include "model/world_simulation.h"

/**
 * @class Game
 * @brief Represents the core game state and logic.
 *
 * The Game class orchestrates the simulation mechanics, handling the
 * progression of time, saving and loading the game state, and triggering match
 * simulations.
 */
class Game
{
 public:
  /**
   * @brief Constructs a new Game object.
   * @param gd Shared pointer to the GameData instance.
   * @param conn Shared pointer to the DatabaseConnection.
   */
  explicit Game(std::shared_ptr<class GameData> gd,
                std::shared_ptr<DatabaseConnection> conn);

  /**
   * @brief Advances the game time by one day, simulating any matches or events
   * scheduled for the current date.
   */
  void advanceDay();

  /** Records a managed match exactly once and applies its consequences. */
  bool setMatchResult(const GameDateValue& date, TeamID home_id, TeamID away_id,
                      uint8_t home_score, uint8_t away_score);

  /**
   * Records a managed match with its structured report (e.g. built from the
   * live MatchEngine). A drawn cup tie is settled by extra time and
   * penalties unless the report already carries them. @p consequences
   * (MatchdaySquad::consequences of the engine) apply the measured condition
   * and injuries exactly as for simulated matches.
   */
  bool setMatchResult(
      const GameDateValue& date, TeamID home_id, TeamID away_id,
      MatchReport report,
      std::span<const PlayerMatchConsequence> consequences = {});

  /** Fit (not injured), outside friendlies not suspended for @p type, and
   * not away with his national team on @p date (default: today). */
  bool isEligible(const Player& player, MatchType type,
                  std::optional<GameDateValue> date = std::nullopt) const;
  /** Selected players of a club who may not play a match of @p type. */
  std::vector<PlayerID> ineligibleSelections(TeamID team_id,
                                             MatchType type) const;
  /**
   * The assistant's matchday selection: regulars fit again take back their
   * places from the players who stood in for them, then ineligible players
   * are replaced by the best-fitting eligible squad and academy players,
   * then by trialists, and the replacements are noted as stand-ins
   * (Lineup::getStandIns()). When nobody fit
   * is left for a starting place, injured players play through it (the
   * least injured first), so a side always takes the field; players who may
   * not play at all (suspended, on national duty) and cannot be replaced
   * leave the XI and the side plays with the players it has. Availability
   * is judged on @p date (default: today). Returns how many selections
   * changed.
   */
  std::size_t fixMatchdaySquad(
      TeamID team_id, MatchType type,
      std::optional<GameDateValue> date = std::nullopt);
  /** What fixMatchdaySquad() would change: (replaced, replacement or 0). */
  std::vector<std::pair<PlayerID, PlayerID>> previewMatchdaySquadFix(
      TeamID team_id, MatchType type) const;
  /**
   * Whether the club's selection may kick off as it is: it is exactly what
   * fixMatchdaySquad() would pick, so players still listed by
   * ineligibleSelections() are injured players nobody fit can replace.
   */
  bool canKickOff(TeamID team_id, MatchType type) const;
  /**
   * Whether the assistant keeps the managed selection eligible: after each
   * day, and at kick-off, injured or suspended players are replaced.
   */
  void setAssistantFixesLineup(bool enabled)
  {
    guidance.delegation.set(
        Duty::LineupFixes, enabled ? DutyOwner::Assistant : DutyOwner::Manager);
  }
  bool getAssistantFixesLineup() const
  {
    return guidance.delegation.delegated(Duty::LineupFixes);
  }

  /** Checklist, delegation, opposition instructions, match analytics. */
  CareerGuidance& getGuidance() { return guidance; }
  const CareerGuidance& getGuidance() const { return guidance; }

  /** Rest and minute-limit flags, load log and warnings of the physios. */
  MedicalDesk& getMedical() { return medical; }
  const MedicalDesk& getMedical() const { return medical; }
  /** The managed club's supporters and their weekly mood. */
  const Supporters& getSupporters() const { return supporters; }

  /** Standings, cups, reports, player season stats and season history. */
  const CompetitionManager& getCompetitions() const { return competitions; }

  /** Final tables of past seasons and the managed club's season reviews. */
  const SeasonArchive& getSeasonArchive() const { return season_archive; }
  SeasonArchive& getSeasonArchive() { return season_archive; }

  /** Squad players aged 21 or under with ten or more appearances. */
  int youngRegulars(TeamID team_id) const;
  /** A club's run in its domestic cup this season. */
  struct CupRun
  {
    /** Rounds after the furthest one played (0 = the final); nullopt
     * before the club's first tie. */
    std::optional<int> rounds_left;
    bool won = false;
    bool still_in = false;
  };
  CupRun cupRun(TeamID team_id) const;

  /** National teams: calendar, call-ups, finals, caps. */
  const NationalTeams& getNationalTeams() const { return international; }
  NationalTeams& getNationalTeams() { return international; }

  /** The manager's national-team job and the market for national jobs. */
  NationalManagement& getNationalJob() { return national_job; }
  const NationalManagement& getNationalJob() const { return national_job; }
  /** What the national-job market did on the latest simulated day. */
  const NationalDayEvents& getLastNationalEvents() const
  {
    return last_national_events;
  }

  /** Inbox, board, injuries, finances and development between matches. */
  WorldSimulation& getWorld() { return world; }
  const WorldSimulation& getWorld() const { return world; }

  /** Deals, loans, pre-contracts, scheduled payments and transfer history. */
  TransferMarket& getTransfers() { return transfers; }
  const TransferMarket& getTransfers() const { return transfers; }

  /** The human manager's career and the market for managers. */
  ManagerCareer& getCareer() { return career; }
  const ManagerCareer& getCareer() const { return career; }
  /** What the manager market did on the latest simulated day. */
  const CareerDayEvents& getLastCareerEvents() const
  {
    return last_career_events;
  }

  /**
   * The manager leaves the managed club (sacked, resigned, contract
   * expired, or moving on): the club gets a vacancy and an AI manager in
   * due course, its board, shortlist, transfer talks and offers are dropped
   * and no command reaches it any more. The inbox stays.
   */
  void leaveManagedTeam(DepartureReason reason);

  /**
   * The manager takes charge of @p team_id under @p contract (leaving the
   * current club first): the board sets objectives and budgets for the new
   * club, scouting starts afresh and the club's squad, line-up, tactics and
   * training plan become the manager's.
   */
  void takeJob(TeamID team_id, const ManagerContract& contract);

  /**
   * Matches simulated / scheduled so far in the current advanceDay(). Safe to
   * poll from another thread while advanceDay() runs.
   */
  SimulationProgress getSimulationProgress() const
  {
    return scheduler.getProgress();
  }
  void resetSimulationProgress() { scheduler.resetProgress(); }
  /** Threads used to simulate a matchday (1 = one match after another). */
  void setSimulationThreads(unsigned threads)
  {
    scheduler.setThreadCount(threads);
  }
  unsigned getSimulationThreads() const { return scheduler.getThreadCount(); }

  /**
   * @brief Retrieves the current in-game date.
   * @return A constant reference to the current GameDateValue.
   */
  const GameDateValue& getCurrentDate() const;

  /**
   * @brief Retrieves the calendar containing all fixtures.
   * @return A constant reference to the game Calendar.
   */
  const Calendar& getCalendar() const;
  Calendar& getCalendar();

  /**
   * @brief Retrieves the current season number.
   * @return The current season as an integer.
   */
  int getCurrentSeason() const;

  /**
   * @brief Retrieves the ID of the team currently managed by the user.
   * @return The managed team's ID.
   */
  uint16_t getManagedTeamId() const;

  /**
   * @brief Sets the team managed by the user.
   *
   * Taking charge of a new club sets the board's objective and budgets
   * immediately and posts the day-one inbox (welcome, squad report,
   * pre-season schedule, scouting suggestion).
   * @param id The ID of the team to manage.
   */
  void setManagedTeamId(uint16_t id);

  /**
   * @brief Saves the current game state, including calendar and game variables,
   * to the database.
   */
  void saveGame();

 private:
  void loadGame();
  void endSeason();
  void handleSeasonTransition();
  void startNewSeason();
  /** The managed club's season numbers, read from the final tables before
   * promotion and relegation move the clubs. */
  void beginSeasonReview(
      const std::map<LeagueID, std::vector<StandingRow>>& final_tables,
      std::uint16_t start_year);
  /** Outcome, board verdict and season news once the clubs have moved. */
  void judgeSeason(std::span<const SeasonHistoryEntry> finished);
  /** Archives the review with next season's objectives. */
  void finishSeasonReview();
  /** Takes a departing managed player out of the line-up, filling only his
   * place, and notes the change for postLineupRepairs(). */
  void fillLineupGap(Team& team, const Player& departed);
  /** Tells the manager which starting places were filled. */
  void postLineupRepairs();
  /** The club's fixtures from today up to its first competitive match. */
  std::vector<UpcomingFixture> upcomingFixtures(TeamID team_id) const;

  /** Replaces unavailable managed players for the next managed fixture. */
  void keepManagedSelectionEligible();
  /** fixMatchdaySquad() applied to @p lineup, a selection of @p team_id;
   * without @p recall_regulars recovered regulars stay where they are (the
   * selection is only made eligible). */
  std::size_t fillMatchdaySquad(Lineup& lineup, TeamID team_id, MatchType type,
                                const GameDateValue& date,
                                bool recall_regulars) const;

  /** Manager market day and the board's verdict on the manager. */
  void runCareerDay();
  /** Job security of AI managers and the human's record. */
  void recordCareerMatch(const Match& match);

  // Matchday simulation helper
  void simulateMatches(std::vector<Match>& matches,
                       bool include_managed = false);
  /**
   * Fallback for a knockout tie recorded level without extra time (a
   * score-only result, or a match abandoned before the engine could settle
   * it): extra time and penalties are resolved from the squads and the
   * extra-time goals credited to players who finished the match.
   */
  void settleLevelTie(Match& match, MatchReport& report);

  std::shared_ptr<DatabaseConnection> db_conn;
  std::unique_ptr<RuntimeEditor::Extensions> extensions;
  int extensions_version = -1;
  RuntimeEditor::Payloads payload_extensions;
  void discoverExtensions();
  void serialize(bool probe);
  std::shared_ptr<class GameData> gamedata;
  Calendar calendar;
  CompetitionManager competitions;
  WorldSimulation world;
  TransferMarket transfers;
  ManagerCareer career;
  CareerDayEvents last_career_events;
  NationalTeams international;
  NationalManagement national_job;
  NationalDayEvents last_national_events;
  MatchScheduler scheduler;
  GameDateValue currentDate;
  uint8_t current_season = 1;
  uint16_t managed_team_id;
  CareerGuidance guidance;
  MedicalDesk medical;
  Supporters supporters;
  SeasonArchive season_archive;
  std::optional<SeasonReview> closing_review;
  /** Starting places filled at the season end: (departed, replacement). */
  std::vector<std::pair<std::string, std::string>> lineup_repairs;
};
