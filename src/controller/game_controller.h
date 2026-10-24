// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

#include "database/datagenerator.h"
#include "database/save_manager.h"
#include "global/stats_config.h"
#include "model/competition.h"
#include "model/game.h"
#include "model/guidance.h"
#include "model/league.h"
#include "model/manager_career.h"
#include "model/medical_centre.h"
#include "model/next_action.h"
#include "model/player.h"
#include "model/player_agent.h"
#include "model/season_agenda.h"
#include "model/squad_numbers.h"
#include "model/squad_planner.h"
#include "model/squad_status.h"
#include "model/staff.h"
#include "model/team.h"
#include "model/training.h"
#include "model/transfer_listing.h"
#include "model/world_rng.h"
#include "model/youth_academy.h"

class MatchEngine;

/** @brief Why the latest Continue, holiday or off-season jump stopped. */
enum class ContinueStop : uint8_t
{
  None = 0,     /*!< Nothing ran. */
  Fixture,      /*!< A managed fixture is due (or now on the calendar). */
  Message,      /*!< News that needs the manager arrived. */
  Decision,     /*!< A bid, request or job offer waits for an answer. */
  WindowOpened, /*!< A transfer window opened. */
  NewSeason,    /*!< The new season (and pre-season) began. */
  LostJob,      /*!< The manager was sacked or his contract ran out. */
  Requested,    /*!< The manager pressed Stop (or closed the game). */
  DayLimit      /*!< The longest stretch simulated in one go. */
};

/**
 * @class GameController
 * @brief Manages the core game logic, interacting with teams, leagues, and
 * players.
 *
 * This controller holds the central game state and provides methods to retrieve
 * and manipulate game data such as leagues, teams, and the managed team.
 */
class GameController
{
 public:
  struct ContractTerms
  {
    uint32_t weekly_wage = 0;
    uint8_t years = 0;
  };

  struct SaveSlotMetadata
  {
    bool exists = false;
    std::string team_name = "";
    std::string game_date = "";
    std::string real_date = "";
    /** Ok, Incomplete, Corrupt or FutureVersion (Missing if !exists). */
    SaveStatus status = SaveStatus::Missing;
    /** Localized key describing a status other than Ok. */
    const char* status_key = "";
    int season = 0;
    std::int64_t playtime_seconds = 0;
    int schema_version = 0;
    int supported_schema_version = 0;
    std::string last_saved_game_date = "";
    std::string last_saved_utc = "";  ///< ISO-8601, empty for old saves.
    std::string game_version = "";
    int backups = 0;  ///< Restorable previous saves (<slot>.bak.N).
  };

  /** Outcome of the latest save of this session (manual or autosave). */
  struct SaveStatusInfo
  {
    bool ok = true;
    bool autosave = false;
    SaveError error;  ///< error.langKey() for the UI message.
    SaveTimings timings;
    std::string game_date;     ///< In-game date of the save.
    int successful_saves = 0;  ///< Since the game was loaded.
  };

  /**
   * @brief Constructs a new GameController.
   */
  GameController();

  /**
   * @brief Starts a new game on the given save slot.
   * @param world_seed Seed of the generated world and of every random stream
   * of the world simulation. Without it FM_WORLD_SEED or the clock is used.
   */
  void newGame(int slot,
               std::optional<std::uint64_t> world_seed = std::nullopt);

  /**
   * @brief Loads an existing game from the given save slot.
   * @return True if loaded successfully, false otherwise.
   */
  bool loadGame(int slot);

  /**
   * Why the last loadGame() failed (FutureVersion, Corrupt, Incomplete,
   * Missing); empty after a successful load.
   */
  const std::optional<SaveError>& getLastLoadError() const
  {
    return last_load_error;
  }

  /** Slot of the loaded career. */
  std::optional<int> getCurrentSlot() const;

  /** Previous saves and pre-upgrade copies of a slot, newest first. */
  std::vector<SaveBackup> getSaveBackups(int slot) const;

  /**
   * Replaces the slot with one of its backups (the replaced file is kept
   * aside, never deleted) and loads it; unsaved progress of a loaded
   * career in that slot is discarded.
   */
  bool restoreBackup(int slot, const std::filesystem::path& backup);

  /** Deletes a slot with its backups (not the loaded one). */
  bool deleteSave(int slot) const;

  /** When the game saves on its own and how many backups a slot keeps. */
  void setAutosavePolicy(const AutosavePolicy& policy);
  AutosavePolicy getAutosavePolicy() const;

  /** Duration of the most recent load/new-game initialization. */
  float getLastInitializationMilliseconds() const
  {
    return last_initialization_milliseconds;
  }

  /**
   * @brief Checks if a game is currently loaded.
   */
  bool isGameLoaded() const;

  /**
   * @brief Gets the current season year.
   * @return The current season as an integer.
   */
  int getCurrentSeason() const;

  /**
   * @brief Gets the current date in the game.
   * @return The current game date.
   */
  GameDateValue getCurrentDate() const;

  /**
   * @brief Checks if a team has been selected to be managed.
   * @return True if a team is selected, false otherwise.
   */
  bool hasSelectedTeam() const;

  /**
   * @brief Gets the team currently managed by the player (mutable).
   * @return An optional reference wrapper to the managed Team.
   */
  std::optional<std::reference_wrapper<Team>> getManagedTeam();

  /**
   * @brief Gets the team currently managed by the player (read-only).
   * @return An optional reference wrapper to the managed Team (const).
   */
  std::optional<std::reference_wrapper<const Team>> getManagedTeam() const;

  /**
   * @brief Selects the team for the player to manage.
   * @param team_id The ID of the team to select.
   */
  void selectManagedTeam(uint16_t team_id);

  /**
   * @brief Gets all leagues available in the game.
   * @return A vector of constant reference wrappers to the leagues.
   */
  const std::vector<std::reference_wrapper<const League>>& getLeagues() const;

  /**
   * @brief Gets all teams available in the game.
   * @return A vector of constant reference wrappers to the teams.
   */
  const std::vector<std::reference_wrapper<const Team>>& getTeams() const;

  /**
   * @brief Gets the players belonging to a specific team.
   * @param team_id The ID of the team.
   * @return A vector of constant reference wrappers to the players.
   */
  const std::vector<std::reference_wrapper<const Player>>& getPlayersForTeam(
      uint16_t team_id) const;

  /**
   * Gives a senior player of the managed club squad number @p number
   * (1-99); a teammate wearing it takes the player's old number. Other
   * clubs' players and academy players are refused (Invalid).
   */
  SquadNumbers::Change setSquadNumber(PlayerID player_id, int number);

  /**
   * @brief Gets the teams belonging to a specific league.
   * @param league_id The ID of the league.
   * @return A vector of constant reference wrappers to the teams in the league.
   */
  std::vector<std::reference_wrapper<const Team>> getTeamsInLeague(
      uint8_t league_id) const;

  /**
   * @brief Gets a league by its ID.
   * @param league_id The ID of the league.
   * @return An optional reference wrapper to the league.
   */
  std::optional<std::reference_wrapper<const League>> getLeagueById(
      uint8_t league_id) const;

  /**
   * @brief Gets a team by its ID.
   * @param team_id The ID of the team.
   * @return An optional reference wrapper to the team.
   */
  std::optional<std::reference_wrapper<const Team>> getTeamById(
      uint16_t team_id) const;

  /**
   * @brief Descriptive identity of a club from the data pack (short code,
   * kit colours, stadium, founding year, nickname). Loaded once on first
   * use and cached; UI thread only.
   * @return nullptr when the pack has no entry for this club.
   */
  const ClubIdentity* getClubIdentity(TeamID team_id) const;

  /**
   * @brief Gets the configuration settings for player stats.
   * @return A reference to the StatsConfig.
   */
  const StatsConfig& getStatsConfig() const;

  /**
   * @brief Advances the game date by one day.
   */
  void advanceDay();

  /** Advances quiet days and stops on the next managed fixture (early
   * when the job is lost or a stop is requested). */
  int advanceToNextManagedFixture(int max_days = 60);

  /** Longest off-season stretch one Continue simulates. */
  static constexpr int OFF_SEASON_MAX_DAYS = 45;
  /**
   * Continue with no managed fixture on the calendar (the off-season):
   * days pass until something needs the manager, the first of: news that
   * arrives unread, a bid or job offer, a transfer window opening, the new
   * season, a managed fixture on the calendar, the loss of the job, a stop
   * request or @p max_days. The reason is in getLastContinueStop().
   * @return Days advanced.
   */
  int advanceToNextEvent(int max_days = OFF_SEASON_MAX_DAYS);

  /**
   * Asks the running Continue, holiday or off-season jump to stop at the end
   * of the day being simulated. Thread-safe; each run clears the request
   * when it starts.
   */
  void requestContinueStop() { continue_stop_requested = true; }
  [[nodiscard]] bool isContinueStopRequested() const
  {
    return continue_stop_requested;
  }
  /** Why the latest run stopped (read once it has finished). */
  ContinueStop getLastContinueStop() const { return last_continue_stop; }
  /** Language key explaining @p stop (e.g. "CONTINUE_STOP_MESSAGE"). */
  static const char* continueStopKey(ContinueStop stop);

  /** Progress of the running advanceDay() / advanceToNextManagedFixture(). */
  struct ContinueProgress
  {
    /** Days before the one being simulated, out of the days to advance. */
    int days_done = 0;
    int days_total = 0;
    /** Matches simulated / scheduled so far on the day being simulated. */
    uint32_t matches_done = 0;
    uint32_t matches_total = 0;

    /** Overall completion in [0, 1]; a day counts once its matches ran. */
    float fraction() const;
  };
  /**
   * Thread-safe snapshot for a progress bar: poll it from the UI thread while
   * Continue runs on a worker thread.
   */
  ContinueProgress getContinueProgress() const;

  /** Threads simulating a matchday in the current game (1 = sequential). */
  void setSimulationThreads(unsigned threads);

  bool setMatchResult(GameDateValue date, uint16_t home_id, uint16_t away_id,
                      uint8_t home_score, uint8_t away_score);

  /** Records a managed match with the structured summary of the live engine
   * (team stats, per-player lines and the measured condition and injuries,
   * applied like for simulated matches). A knockout engine brings its extra
   * time and shootout; a tie still level is settled statistically. */
  bool setMatchResult(GameDateValue date, uint16_t home_id, uint16_t away_id,
                      const MatchEngine& engine);

  /** Knockout rules of a fixture for the engine that plays it (extra time
   * and penalties, aggregate of the first leg); nullopt for matches that may
   * end drawn or unknown fixtures. */
  std::optional<MatchRules::Knockout> getKnockoutRules(GameDateValue date,
                                                       TeamID home_id,
                                                       TeamID away_id) const;

  /** Selected players (XI and reserves) injured or suspended for @p type. */
  std::vector<PlayerID> getIneligibleSelections(TeamID team_id,
                                                MatchType type) const;
  /** Replaces those players with eligible squad members (same role first);
   * returns how many were replaced. */
  size_t autoFixLineup(TeamID team_id, MatchType type);
  /** What autoFixLineup() would change: (replaced, replacement or 0 when
   * the player just leaves the matchday squad). */
  std::vector<std::pair<PlayerID, PlayerID>> previewLineupFix(
      TeamID team_id, MatchType type) const;
  /** The selection may kick off as it is: autoFixLineup() would change
   * nothing, so any ineligible selections left are injured players nobody
   * fit can replace (they play through it). */
  bool canKickOff(TeamID team_id, MatchType type) const;
  /** Assistant keeps the managed lineup eligible (each day and at kick-off;
   * on by default, kept for the session). */
  void setAssistantFixesLineup(bool enabled);
  bool getAssistantFixesLineup() const;

  // ========== Competitions ==========
  /** Fixture-derived table: points, GD, goals, head-to-head, name, ID. */
  std::vector<StandingRow> getStandings(LeagueID league_id) const;
  /** Domestic cup IDs: the top league ID of each country. */
  std::vector<LeagueID> getCupIds() const;
  std::optional<Competitions::CupStatus> getCupStatus(LeagueID cup_id) const;
  std::string getCupName(LeagueID cup_id) const;
  /** 1 = top division, 2 = second division, ... */
  uint8_t getLeagueTier(LeagueID league_id) const;
  /** All fixtures (league, cup, friendly) of a team this season by date. */
  std::vector<Match> getTeamFixtures(TeamID team_id) const;
  std::optional<MatchReport> getMatchReport(GameDateValue date, TeamID home_id,
                                            TeamID away_id) const;
  const std::vector<SeasonHistoryEntry>& getSeasonHistory() const;
  /** Seasons with an archived final table of @p league_id, newest first. */
  std::vector<uint16_t> getArchivedSeasons(LeagueID league_id) const;
  /** Final table of a past season (nullptr if it was not archived). */
  const std::vector<StandingRow>* getArchivedTable(uint16_t season,
                                                   LeagueID league_id) const;
  /** Calendar year @p season started in (0 if not archived). */
  uint16_t getSeasonStartYear(uint16_t season) const;
  /** League and final position of a club in a past season. */
  std::optional<std::pair<LeagueID, uint16_t>> getArchivedPlacing(
      uint16_t season, TeamID team_id) const;
  /** The latest season summary not shown yet (nullptr when none). */
  const SeasonReview* getUnseenSeasonReview() const;
  void markSeasonReviewSeen(uint16_t season);
  std::vector<PlayerSeasonStats> getTopScorers(MatchType competition_type,
                                               LeagueID competition_id,
                                               size_t limit = 10) const;
  std::vector<PlayerSeasonStats> getPlayerSeasonStats(PlayerID player_id) const;
  std::vector<PlayerSeasonStats> getPlayerCareer(PlayerID player_id) const;

  // ========== International ==========
  /** Continental club competitions (nullptr without a game). */
  const ContinentalCompetitions* getContinental() const;
  /** Aggregate score of a continental knockout tie. */
  ContinentalCompetitions::TieScore getContinentalTieScore(
      LeagueID competition_id, const ContinentalCompetitions::Tie& tie) const;
  /** National teams: calendar, squads, finals, ratings (nullptr without a
   * game). */
  const NationalTeams* getNationalTeams() const;
  /** Caps, goals and finals matches of a player; nullptr when uncapped. */
  const International::Record* getInternationalRecord(PlayerID player_id) const;
  /** Nation a player is called up by (announced or away) today. */
  std::optional<Language> getInternationalDuty(PlayerID player_id) const;
  /** Matches still to serve in league or cup (0 = eligible). */
  uint8_t getSuspensionMatches(PlayerID player_id, MatchType scope) const;
  /** Players of a team with an outstanding suspension. */
  std::vector<DisciplinaryRecord> getSuspendedPlayers(TeamID team_id) const;

  /**
   * @brief Saves the current state of the game to its slot.
   *
   * The slot file is replaced atomically by a verified snapshot and the
   * previous one kept as a backup; on failure (disk full, read-only folder)
   * the previous save is untouched. Never throws.
   * @return False on failure; details in getSaveStatus().
   */
  bool saveGame();

  /** Latest save result; safe to poll from the UI thread during Continue. */
  SaveStatusInfo getSaveStatus() const;

  /**
   * @brief Gets metadata for a save slot.
   */
  SaveSlotMetadata getSaveSlotMetadata(int slot) const;

  // ========== Transfer Market: Listing ==========
  void listPlayerForTransfer(PlayerID player_id, uint32_t asking_price);
  void removePlayerFromTransfer(PlayerID player_id);
  bool isPlayerListed(PlayerID player_id) const;

  // ========== Transfer Market: Queries ==========
  const std::unordered_map<PlayerID, TransferListing>& getAllListings() const;
  std::vector<const TransferListing*> getListingsExcludingTeam(
      TeamID team_id) const;
  std::vector<const TransferListing*> getListingsByRole(PlayerRole role) const;

  // ========== Transfer Market: Buying ==========
  bool canAffordPlayer(TeamID buyer_id, PlayerID player_id,
                       uint32_t target_price) const;
  bool canAffordPlayer(TeamID buyer_id, PlayerID player_id,
                       uint32_t target_price,
                       uint32_t offered_weekly_wage) const;
  bool buyPlayer(PlayerID player_id, TeamID buyer_id, uint32_t price);
  bool buyPlayerWithContract(PlayerID player_id, TeamID buyer_id,
                             uint32_t price, ContractTerms terms);
  bool signFreeAgent(PlayerID player_id, TeamID buyer_id);
  bool signFreeAgentWithContract(PlayerID player_id, TeamID buyer_id,
                                 ContractTerms terms);
  ContractTerms getContractDemand(PlayerID player_id, bool free_agent) const;
  bool isContractOfferAcceptable(PlayerID player_id, bool free_agent,
                                 ContractTerms terms) const;
  uint32_t getPlayerMarketValue(PlayerID player_id) const;

  // ========== Transfer Market: Bids & Negotiation ==========
  bool submitBid(PlayerID player_id, TeamID bidder_id, uint32_t bid_amount);
  bool acceptBid(PlayerID player_id);
  bool rejectBid(PlayerID player_id);
  bool counterOffer(PlayerID player_id, uint32_t new_price);
  /** True while the managed club's country has its transfer window open
   * (the managed club can sign players from other clubs). */
  bool isTransferWindowOpen() const;
  /** True while @p club can sign players from other clubs: the buying
   * club's window decides (TransferWindows). */
  bool isTransferWindowOpenFor(TeamID club) const;
  /** True while some country's window is open, so the managed club may
   * still sell to a club whose window is open. */
  bool isTransferWindowOpenAnywhere() const;
  /** True when @p club may sign a player without a club today: any day in
   * most countries, only in and shortly after the windows in some. */
  bool canSignFreeAgentFor(TeamID club) const;

  // ========== Transfer Market: structured deals ==========
  /** The managed club's transfer window today (deadline countdown). */
  TransferNegotiation::WindowInfo getTransferWindow() const;
  /** Transfer window of @p club's country today. */
  TransferNegotiation::WindowInfo getTransferWindowFor(TeamID club) const;
  /**
   * Offer of the managed club for another club's player. An accepted fee
   * opens contract talks with the player for a week.
   */
  TransferNegotiation::ClubResponse makeTransferOffer(
      PlayerID player_id, const TransferNegotiation::OfferTerms& terms);
  /** Kind of contract talks the managed club can hold with a player now:
   * agreed transfer, free agent or pre-contract (final six months). */
  std::optional<TransferNegotiation::ContractKind> getContractTalkKind(
      PlayerID player_id) const;
  /** The player's demands for talks of @p kind with the managed club;
   * asking_wage is the agent's current ask in these talks. */
  TransferNegotiation::ContractDemand getPlayerDemand(
      PlayerID player_id, TransferNegotiation::ContractKind kind) const;

  /** Why an action on a managed player is not possible now. */
  enum class PlayerActionBlock : std::uint8_t
  {
    None,
    NotYours,       /*!< Not the managed club's (or an academy player). */
    WindowClosed,   /*!< The transfer window is shut. */
    OnLoan,         /*!< Borrowed from another club. */
    Leaving,        /*!< Committed to another club (pre-contract). */
    SquadFloor,     /*!< He would leave fewer than 11 senior players. */
    LastGoalkeeper, /*!< He is the club's only senior goalkeeper. */
    MaxLength,      /*!< His contract already runs as long as allowed. */
    NotLonger,      /*!< The proposal does not extend his contract. */
    TooLong,        /*!< Longer than the rules allow at his age. */
    TalksEnded,     /*!< He will not talk again for a few days. */
    SquadFull       /*!< The squad already has the most senior players. */
  };
  /** Language key explaining @p block (empty for None). */
  static const char* playerActionBlockKey(PlayerActionBlock block);
  /** Why the player cannot be released now (None if he can). */
  PlayerActionBlock getReleaseBlock(PlayerID player_id) const;
  /** Why the player cannot be sold or loaned out now. */
  PlayerActionBlock getSaleBlock(PlayerID player_id) const;
  /** Why the player cannot be transfer-listed now. */
  PlayerActionBlock getListingBlock(PlayerID player_id) const;
  /** Why the player's contract cannot be renewed now. */
  PlayerActionBlock getRenewalBlock(PlayerID player_id) const;

  struct ContractTalkResult
  {
    TransferNegotiation::ContractResponse response;
    /** Not proposed: why (the proposal used no round). */
    PlayerActionBlock block = PlayerActionBlock::None;
    /** What the player's agent says about it (language key, takes the
     * player's name). */
    const char* agent_line = "";
    bool completed = false;   /*!< Moved, renewed or pre-contract signed. */
    bool over_budget = false; /*!< Agreed but the club cannot pay. */
    std::uint8_t rounds_left = 0;
  };
  /**
   * Proposes personal terms; completes the move (or the renewal of a
   * managed player's contract) when the player and his agent agree. A
   * renewal's years count the current season: it must extend the contract.
   */
  ContractTalkResult proposeContract(
      PlayerID player_id, const TransferNegotiation::ContractOffer& offer);
  /** Proposals the player will still consider. */
  std::uint8_t getContractRoundsLeft(PlayerID player_id) const;

  /** What the player's agent asks for in the open contract talks, the
   * package priced for @p years. */
  PlayerAgent::Demands getAgentDemands(PlayerID player_id,
                                       TransferNegotiation::ContractKind kind,
                                       std::uint8_t years) const;
  /** The agent's line as contract talks of @p kind open. */
  const char* getAgentOpeningLine(PlayerID player_id,
                                  TransferNegotiation::ContractKind kind) const;
  /** The agent's line after the selling club answered the club's offer. */
  const char* getAgentPurchaseLine(
      PlayerID player_id,
      const TransferNegotiation::ClubResponse& response) const;
  /** Release clause in a player's contract (0 = none). */
  uint32_t getReleaseClause(PlayerID player_id) const;
  /**
   * Pays another club's player's release clause in cash: the selling club
   * cannot refuse, and contract talks with the player open. Fails over
   * budget, out of the window or without a clause.
   */
  TransferNegotiation::ClubResponse payReleaseClause(PlayerID player_id);

  /** Loan offer for another club's player; starts the loan on agreement. */
  TransferNegotiation::ClubResponse makeLoanOffer(
      PlayerID player_id, const TransferNegotiation::LoanTerms& terms);
  /** Offers (or withdraws) a managed player for loan. */
  bool setLoanListed(PlayerID player_id, bool listed);
  /** Recalls a loaned-out player (recall clause, open window, at least
   * Loan::RECALL_MIN_DAYS into the loan). */
  bool recallLoan(PlayerID player_id);
  /** True when recallLoan() would succeed today. */
  bool canRecallLoan(PlayerID player_id) const;
  /** True when the managed squad has room for one more senior player. */
  bool hasSquadRoom() const;
  /** Buys a player on loan at the managed club for the option fee. */
  bool exerciseLoanOption(PlayerID player_id);

  /** AI offers for the managed club's players (loans included). Bids of
   * AI clubs on the club's listed players arrive here too. */
  const std::vector<IncomingOffer>& getIncomingOffers() const;

  /** Everything the talks over an incoming transfer or loan offer show. */
  struct IncomingOfferView
  {
    std::uint32_t offer_id = 0;
    PlayerID player_id = 0;
    TeamID buyer = 0;
    std::string player_name;
    std::string buyer_name;
    int age = 0;
    SquadRole role{};
    std::uint32_t market_value = 0;
    std::uint32_t asking_price = 0; /*!< Listing price, 0 if not listed. */
    std::uint8_t contract_years = 0;
    BuyerNegotiation::PlayerStance stance =
        BuyerNegotiation::PlayerStance::Open;
    std::uint8_t rivals = 0; /*!< Other clubs bidding for him. */
    OfferStatus status = OfferStatus::AwaitingClub;
    GameDateValue expires;
    GameDateValue respond_on;
    TransferNegotiation::OfferTerms terms; /*!< The bid on the table. */
    TransferNegotiation::OfferTerms asked; /*!< The club's pending counter. */
    bool loan = false;
    TransferNegotiation::LoanTerms loan_terms; /*!< Loan: on the table. */
    TransferNegotiation::LoanTerms asked_loan; /*!< Loan: pending counter. */
    std::uint32_t weekly_wage = 0;             /*!< His full wage. */
    std::uint32_t release_clause = 0;          /*!< 0 = none. */
    int season_weeks = 0; /*!< Weeks to the season's end. */
    /** Why the club could not let him go now (squad floor). */
    PlayerActionBlock sale_block = PlayerActionBlock::None;
    std::vector<OfferRound> history;
    bool final_offer = false; /*!< The bid is the buyer's last word. */
    bool window_open = false;
    int days_to_deadline = -1;
  };
  std::optional<IncomingOfferView> getIncomingOfferView(
      std::uint32_t offer_id) const;

  /** What became of an action on an incoming offer. */
  enum class OfferOutcome : std::uint8_t
  {
    Sold,          /*!< The player moved (or left on loan). */
    AwaitingReply, /*!< The buyer answers on a later day. */
    Countered,     /*!< The buyer answered at once (deadline) with terms. */
    WalkedAway,    /*!< The buyer ended the talks. */
    Rejected,      /*!< The club turned the offer down. */
    TermsRefused,  /*!< The clubs agreed but the player would not sign. */
    SquadTooSmall, /*!< Selling him would break the squad floor. */
    Failed         /*!< Not possible now (window, budget, player gone). */
  };
  /** Accepts the bid on the table: the player agrees personal terms with
   * the buyer (or, rarely, refuses) and the sale completes. */
  OfferOutcome settleIncomingOffer(std::uint32_t offer_id);
  /** settleIncomingOffer() that only reports whether the player moved. */
  bool acceptIncomingOffer(std::uint32_t offer_id);
  /** Turns the offer down; a player keen on the move takes it badly. */
  bool rejectIncomingOffer(std::uint32_t offer_id);
  /** Proposes a full structure to the bidder. It answers in a day or two
   * (the same day near the deadline): accepts, counters or walks away. */
  OfferOutcome counterIncomingOffer(
      std::uint32_t offer_id, const TransferNegotiation::OfferTerms& terms);
  /** Proposes loan terms (wage share, length, option or obligation to buy,
   * recall clause, guaranteed appearances) to the club that wants to
   * borrow the player. It answers like a buyer: later, the same day near
   * the deadline, with its own terms, or by walking away. */
  OfferOutcome counterLoanOffer(std::uint32_t offer_id,
                                const TransferNegotiation::LoanTerms& terms);
  /** Lists the player at @p price and gives the bidder that figure as
   * a take-it-or-leave-it price (it meets it, makes its final offer or
   * leaves). */
  OfferOutcome nameAskingPrice(std::uint32_t offer_id, uint32_t price);
  /** Rejects every bid for the offer's player and tells the clubs he is
   * not for sale until the window closes. */
  bool declareNotForSale(std::uint32_t offer_id);

  /** Remaining wages owed if a managed player is released today. */
  int64_t getReleaseCost(PlayerID player_id) const;
  /** Terminates a managed player's contract, paying the remaining wages. */
  bool releasePlayer(PlayerID player_id);

  // ========== AI Helpers ==========
  struct SquadNeeds
  {
    int missing_gk = 0;
    int missing_cb = 0;
    int missing_lb = 0;
    int missing_rb = 0;
    int missing_mid = 0;
    int missing_wing = 0;
    int missing_st = 0;

    int surplus_gk = 0;
    int surplus_cb = 0;
    int surplus_lb = 0;
    int surplus_rb = 0;
    int surplus_mid = 0;
    int surplus_wing = 0;
    int surplus_st = 0;

    std::optional<PlayerRole>
        upgrade_target;  // Role that needs quality upgrade
  };

  SquadNeeds evaluateSquadNeeds(TeamID team_id) const;
  float calculateAttentionScore(PlayerID player_id) const;
  float getLeagueAttentionMultiplier(LeagueID league_id) const;
  uint32_t calculateMaxPrice(PlayerID player_id, TeamID buyer_id,
                             const SquadNeeds& needs) const;
  void evaluateAndActForTeam(TeamID team_id);
  std::vector<PlayerID> findTargetsForRole(PlayerRole role, TeamID buyer_id,
                                           const SquadNeeds& needs) const;
  PlayerRole getRoleCategory(PlayerRole role) const;
  /**
   * Transfer money the club can commit today (the "available budget" to
   * show): min(board allowance, cash - 12 weeks of payroll - instalments
   * owed this season), never negative, 0 while embargoed.
   */
  uint32_t transferBudgetForTeam(TeamID team_id) const;

  float randomFloat(float min, float max)
  {
    return PortableRandom::uniformReal(transfer_rng, min, max);
  }

  // ========== World: inbox ==========
  /** Managed club's messages, oldest first. Display text via
   * InboxMessage::formatTitle() / formatBody(). */
  const std::vector<InboxMessage>& getInbox() const;
  /** Marks one message read; false if the id is unknown. */
  bool markInboxMessageRead(uint32_t message_id);
  void markAllInboxMessagesRead();
  size_t getUnreadInboxCount() const;
  /** Saved filters of the inbox screen (kept with the career). */
  InboxView getInboxView() const;
  void setInboxView(const InboxView& view);
  /** Followed players: inbox news about their moves, injuries, big
   * matches, honours and new contracts. Following fails for unknown or
   * already followed players and beyond Stories::MAX_FOLLOWS. */
  bool followPlayer(PlayerID player_id);
  bool unfollowPlayer(PlayerID player_id);
  bool isFollowingPlayer(PlayerID player_id) const;
  std::vector<PlayerID> getFollowedPlayers() const;
  /** The open decision moment of the managed club, if any. */
  std::optional<Dilemma> getOpenDilemma() const;
  /** What answer @p option (0 or 1) of the open moment would do. */
  std::optional<DilemmaEffects> getDilemmaEffects(int option) const;
  /** Answers the open decision moment; false if none is open. */
  bool resolveDilemma(int option);

  // ========== World: board ==========
  /** Objective, expected/target position, confidence (0-100), dismissal. */
  const BoardState& getBoardState() const;

  /** @brief The board's targets this season and how they are going. */
  struct BoardTargets
  {
    BoardObjective league = BoardObjective::MidTable;
    int target_position = 0;
    int position = 0; /*!< 0 before the first league match. */
    ObjectiveGrade league_grade = ObjectiveGrade::Met;
    CupObjective cup = CupObjective::None;
    ObjectiveGrade cup_grade = ObjectiveGrade::Met;
    bool cup_still_in = false;
    FinanceObjective finances = FinanceObjective::WithinWageBudget;
    ObjectiveGrade finance_grade = ObjectiveGrade::Met;
    std::int64_t start_balance = 0;
    std::uint8_t youth_target = 0;
    int young_regulars = 0;
    ObjectiveGrade youth_grade = ObjectiveGrade::Met;
  };
  /** Targets of the managed club's board (nullopt without a club). */
  std::optional<BoardTargets> getBoardTargets() const;

  /**
   * True while the board freezes the managed club's transfers because its
   * cash stayed negative (after a warning): no signings, loans or free
   * agents until the balance is positive again; transferBudgetForTeam()
   * is then 0.
   */
  bool isTransferEmbargoed() const;

  // ========== World: finances ==========
  /** Dated transactions of a club, oldest first (empty if unknown). */
  const std::vector<FinanceTransaction>& getFinanceLedger(TeamID team_id) const;
  /** Per-category income/expense totals for transactions in [from, to]. */
  FinanceSummary getFinanceSummary(TeamID team_id, GameDateValue from,
                                   GameDateValue to) const;
  /** Current weekly payroll of a club. */
  int64_t getWeeklyWageBill(TeamID team_id) const;
  /**
   * Moves money between the managed club's budgets (52 weeks per season):
   * positive moves transfer budget into weekly wages, negative moves unused
   * wage room back. False if a budget would become insufficient.
   */
  bool moveTransferToWageBudget(int64_t transfer_amount);
  /** Sets the managed club's average ticket price (1-1000). */
  bool setTicketPrice(uint32_t price);
  /** Reference price at which the club meets its league's gate target. */
  uint32_t getFairTicketPrice(TeamID team_id) const;
  /** Crowd of the club's latest home match this session (0 if none). */
  uint32_t getLastHomeAttendance(TeamID team_id) const;

  // ========== World: players ==========
  /** False while injured. */
  bool isPlayerAvailable(PlayerID player_id) const;
  /** Injured players of a club. */
  std::vector<PlayerID> getInjuredPlayers(TeamID team_id) const;
  /** Selected players (XI and reserves) who are currently unavailable. */
  std::vector<PlayerID> getUnavailableLineupPlayers(TeamID team_id) const;
  /** Playing-time expectation from the player's ability rank. */
  SquadRole getSquadRole(PlayerID player_id) const;

  struct PotentialEstimate
  {
    float low = 0.0f;
    float high = 0.0f;
  };
  /** Scouted potential range (the scouting view's range; see below). */
  PotentialEstimate getPotentialEstimate(PlayerID player_id) const;

  /**
   * Physical consequences of the live match for one player (call before
   * setMatchResult, once per player and match day). end_condition < 0 lets
   * the simulation estimate drain and roll injuries; otherwise injured
   * reports an engine injury event whose diagnosis is drawn here.
   */
  bool applyMatchConsequences(PlayerID player_id, uint8_t minutes_played,
                              float end_condition, bool injured);

  /** Extends a managed player's contract to @p terms.years seasons (the
   * current one included) if he and his agent accept the terms, as one
   * round of the renewal talks. */
  bool renewContract(PlayerID player_id, ContractTerms terms);

  // ========== Human side: conversations, team talks, dressing room ==========
  /**
   * The manager's standing with the players, 0-100: board confidence
   * blended with the club's reputation (there is no manager reputation).
   */
  float getManagerStanding() const;
  /** Conversation options for a managed player: availability, cooldowns
   * and the predicted reaction. */
  std::vector<TalkOptionView> getTalkOptions(PlayerID player_id) const;
  /**
   * Holds one conversation line (morale, trust, promises, requests).
   * Accepting a transfer request also lists the player at his market
   * value. Answers the player's open story, if any. nullopt when the
   * option is not available.
   */
  std::optional<TalkOutcome> talkToPlayer(PlayerID player_id,
                                          TalkOption option);
  /** nullptr when the manager and player have no history. */
  const PlayerRelation* getPlayerRelation(PlayerID player_id) const;
  /** Promises made to a player, newest first (resolved ones for a year). */
  std::vector<Promise> getPlayerPromises(PlayerID player_id) const;
  /** Active promises to managed players, soonest deadline first. */
  std::vector<Promise> getActivePromises() const;
  /** Open story decision for a player (transfer saga, captain dispute). */
  std::optional<StoryChoice> getStoryChoice(PlayerID player_id) const;
  /** True when the player waits for an answer (request or story). */
  bool hasPendingTalk(PlayerID player_id) const;

  /** Situation of the managed club's team talk for today's fixture. */
  TeamTalkContext getTeamTalkContext(TeamTalkMoment moment, int own_goals = 0,
                                     int other_goals = 0) const;
  /** False once this moment's talk was given for today's match. */
  bool canGiveTeamTalk(TeamTalkMoment moment) const;
  /** Gives the talk to the selected XI (once per moment and match). */
  std::optional<TeamTalkResult> giveTeamTalk(TeamTalkMoment moment,
                                             TeamTalkTone tone,
                                             int own_goals = 0,
                                             int other_goals = 0);
  /**
   * Execution-quality hint of today's team talks for @p half (1 or 2),
   * within +-1.2%. For the match engine; nothing reads it yet.
   */
  float getTeamTalkModifier(TeamID team_id, int half) const;
  /** Team talks plus squad cohesion, capped at +-2% (FR-119, FR-123). */
  float getHumanFactorModifier(TeamID team_id, int half) const;
  /** Leaders, cliques, cohesion, mood, requests and promises. */
  DressingRoom getDressingRoom() const;

  // ========== Squad management: leaders, statuses, planning ==========
  /** Player designated for a duty in the managed lineup (0 = automatic). */
  PlayerID getSetPieceDesignation(SetPieceDuty duty) const;
  /**
   * Designates a managed player for a duty (0 = automatic). Goalkeepers
   * cannot take kicks or throws; false when the player is not allowed.
   */
  bool setSetPieceDesignation(SetPieceDuty duty, PlayerID player_id);
  /**
   * Who performs a duty at the next kick-off: the designated player when he
   * starts, else the automatic choice among the XI (captain: the highest
   * dressing-room standing, vice-captain: the next one). 0 without an XI.
   */
  PlayerID getEffectiveSetPieceTaker(SetPieceDuty duty) const;
  /** Designates every duty from the current XI (the automatic choices). */
  void autoPickSetPieces();

  /** Status the manager gave a managed player (nullopt: none given). */
  std::optional<SquadStatus> getSquadStatus(PlayerID player_id) const;
  /** Status the player's ability rank in his squad earns in his own eyes. */
  SquadStatus getDeservedSquadStatus(PlayerID player_id) const;
  /**
   * Gives a managed player a status (nullopt clears it). It sets his
   * playing-time expectation (getSquadRole(), never more than one level
   * below his standing; see SquadStatusModel::expectation()) for morale,
   * requests and contract talks. Prospect is only for players up to
   * SquadStatusModel::PROSPECT_MAX_AGE.
   */
  bool setSquadStatus(PlayerID player_id, std::optional<SquadStatus> status);

  /** Depth chart, needs, age profile and contracts of the managed squad for
   * this season (0) or a projected later one (1 = next season). */
  SquadPlan getSquadPlan(int season_offset) const;
  /** Injuries, fitness, injury risk and the medical staff of the club. */
  MedicalReport getMedicalReport() const;
  /** Dated events of the managed club's current season. */
  std::vector<AgendaEvent> getSeasonAgenda() const;

  // ========== Scouting & recruitment ==========
  // Screens must show and sort other clubs' players by these estimates,
  // never by Player::getStats()/getOverall()/getPotential().

  /** Estimated attributes, overall, potential range and knowledge (0-100);
   * exact values only for the managed club's own players. */
  std::optional<ScoutedPlayerView> getScoutedView(PlayerID player_id) const;
  /** Compact estimate (overall, potential range, value, knowledge). */
  std::optional<ScoutedPlayerRow> getScoutedRow(PlayerID player_id) const;
  /** Player search on estimated values, best estimate first. */
  std::vector<ScoutedPlayerRow> searchScoutedPlayers(
      const ScoutSearchFilter& filter) const;
  /** Managed club's knowledge of a player, 0-100. */
  float getScoutingKnowledge(PlayerID player_id) const;

  std::vector<ScoutProfile> getScouts() const;
  const std::vector<ScoutAssignment>& getScoutAssignments() const;
  /** Price of an assignment; 0 if the target is invalid. */
  int64_t getScoutAssignmentCost(ScoutTargetKind kind, uint32_t target_id,
                                 uint16_t days) const;
  /** Sends a scout (cost booked to the ledger today). */
  ScoutAssignError startScoutAssignment(uint32_t scout_id, ScoutTargetKind kind,
                                        uint32_t target_id, uint16_t days);
  bool cancelScoutAssignment(uint32_t assignment_id);
  /** Scout reports, oldest first. */
  const std::vector<ScoutReport>& getScoutReports() const;
  /** Scouts with status (on assignment / idle with history / new) and
   * report counters, in roster order. */
  std::vector<ScoutSummary> getScoutSummaries() const;
  /** Nationality, languages and league experience of a scout. */
  ScoutExpertise getScoutExpertise(uint32_t scout_id) const;
  /** Expected effectiveness (0.6x-1.5x) with its breakdown. */
  ScoutEffectiveness getScoutEffectiveness(uint32_t scout_id,
                                           ScoutTargetKind kind,
                                           uint32_t target_id) const;
  /** Scout reports not yet opened on their scout's page. */
  size_t getUnreadScoutReportCount() const;
  /** Marks a scout's reports as opened. */
  bool markScoutReportsSeen(uint32_t scout_id);

  const std::vector<RecruitmentFocus>& getRecruitmentFocuses() const;
  /** Adds (id 0) or updates a focus; returns its id, 0 on failure. */
  uint32_t saveRecruitmentFocus(const RecruitmentFocus& focus);
  bool removeRecruitmentFocus(uint32_t focus_id);

  const std::vector<ShortlistEntry>& getShortlist() const;
  bool isShortlisted(PlayerID player_id) const;
  bool addToShortlist(PlayerID player_id);
  bool removeFromShortlist(PlayerID player_id);
  /** Own depth per position against the best shortlisted estimate. */
  std::vector<SquadComparisonRow> compareShortlistWithSquad() const;

  // ========== Training ==========
  /** Managed club's training plan (nullptr without a managed club). */
  const TeamTrainingPlan* getTrainingPlan() const;
  /** Applies a preset's template (Custom keeps the current days). */
  bool setTrainingPreset(TrainingPreset preset);
  /** Changes one day of the managed club's template (preset -> Custom). */
  bool setTrainingSlot(MicrocycleDay day, TrainingSlot slot);
  /** Squad-wide intensity applied on top of the session intensities. */
  bool setTrainingIntensity(TrainingIntensity intensity);
  /** Collapses weeks with two matches to recovery and activation only. */
  bool setCongestionAutoAdjust(bool enabled);
  /** Individual training focus of a managed player. */
  bool setPlayerTrainingFocus(PlayerID player_id, TrainingFocus focus);

  struct PlayerWorkload
  {
    TrainingFocus focus = TrainingFocus::None;
    float acute = 0.0f;   /*!< 7-day load per day. */
    float chronic = 0.0f; /*!< 28-day load per day. */
    float ratio = 1.0f;   /*!< Acute:chronic. */
    WorkloadRisk risk = WorkloadRisk::Low;
    float trend = 0.0f; /*!< Smoothed weekly overall change. */
  };
  /** Focus, workload and development trend of a player. */
  PlayerWorkload getPlayerWorkload(PlayerID player_id) const;

  struct TrainingDayPreview
  {
    GameDateValue date;
    TrainingDay day;
    TeamID opponent = 0; /*!< On match days. */
    bool home = false;
  };
  /** The managed club's next seven days (today first) as scheduled. */
  std::vector<TrainingDayPreview> getTrainingWeekPreview() const;
  /** Tactical familiarity in [0, 1]; see TrainingSystem::tacticalFamiliarity.
   */
  float getTacticalFamiliarity(TeamID team_id) const;
  /** Assistant manager's recommendations, most important first. */
  std::vector<TrainingAdvice> getTrainingAdvice() const;

  // ========== Staff ==========
  enum class StaffActionResult : uint8_t
  {
    Ok,
    NoClub,
    UnknownStaff,
    NotAvailable, /*!< Not on the market / not at the managed club. */
    RoleFull,     /*!< StaffModel::ROLE_LIMITS reached. */
    CannotAfford, /*!< Cash below 12 weeks of the wage or the severance. */
    InvalidTerms
  };
  /** Staff of a club by role, best first. */
  std::vector<const StaffMember*> getStaff(TeamID team_id) const;
  /** Unattached staff, best first. */
  std::vector<const StaffMember*> getStaffMarket() const;
  /** What a club's staff does for the squad. */
  StaffEffects getStaffEffects(TeamID team_id) const;
  /**
   * Scouting accuracy of a club in [0, 1], from its scouts' judging
   * attributes and network size. Discrimination d' = 0.8 + 1.3 * accuracy;
   * StaffModel::potentialErrorSd() converts it to the standard deviation of
   * a potential estimate in overall points.
   */
  float getScoutingAccuracy(TeamID team_id) const;
  /** Weekly wage a candidate asks for (market) or to extend (own staff). */
  uint32_t getStaffWageDemand(StaffID staff_id) const;
  /** Hires a market candidate for @p years seasons (1-4). */
  StaffActionResult hireStaff(StaffID staff_id, uint8_t years);
  /** Releases a managed staff member, paying StaffModel::severance(). */
  StaffActionResult releaseStaff(StaffID staff_id);
  /** Extends a contract to @p years remaining seasons (2-5) at the
   * renewal wage. */
  StaffActionResult extendStaffContract(StaffID staff_id, uint8_t years);

  // ========== Youth academy ==========
  /** An academy player of the managed club (scouted ranges, never exact). */
  struct YouthPlayerView
  {
    PlayerID id = 0;
    std::string name;
    PlayerRole role = PlayerRole::UNKNOWN;
    int age = 0;
    Language nationality = Language::EN;
    uint8_t height = 0;
    YouthStatus status = YouthStatus::Squad;
    YouthContract contract = YouthContract::None;
    int contract_years = 0;
    uint32_t wage = 0; /*!< Weekly wage (0 for trialists). */
    YouthContract offer = YouthContract::None; /*!< Contract he would get. */
    uint32_t offer_wage = 0; /*!< Weekly wage of that contract. */
    YouthEstimate estimate;
    const char* personality_key = "";
    bool homegrown = false;
    bool loan_listed = false;
    uint16_t appearances = 0;
    uint16_t goals = 0;
    float average_rating = 0.0f;
    /** Monthly overall snapshots, oldest first. */
    std::vector<YouthProgressPoint> progress;
  };
  /** Managed club's academy players with @p status, best prospect first. */
  std::vector<YouthPlayerView> getYouthPlayers(YouthStatus status) const;
  /** First-team players of the managed club young enough for the U18s. */
  std::vector<YouthPlayerView> getYouthEligibleFirstTeam() const;
  /** True for U18 players and intake trialists (they are not first-team
   * players of their club). */
  bool isAcademyPlayer(PlayerID player_id) const;

  struct AcademyOverview
  {
    AcademyRatings ratings;
    HeadOfYouth head;
    GameDateValue preview_date; /*!< Of the next (or current) intake. */
    GameDateValue intake_date;
    GameDateValue decision_deadline;
    bool preview_ready = false; /*!< The head has reported on this intake. */
    IntakePreview preview;
    size_t candidates = 0; /*!< Trialists waiting for a decision. */
    size_t squad = 0;
    int league_position = 0; /*!< In the U18 league (0: no league). */
    int league_size = 0;
    YouthTableRow table;
  };
  AcademyOverview getAcademyOverview() const;
  /** U18 league of the managed club, leader first. */
  std::vector<YouthTableRow> getYouthTable() const;
  /** Managed club's U18 results this season, oldest first. */
  const std::vector<YouthResult>& getYouthResults() const;
  /** Next step of an academy investment and the board's answer today. */
  UpgradeQuote getAcademyUpgradeQuote(AcademyUpgrade kind) const;
  /** Asks the board to fund the next step (cost booked when approved). */
  UpgradeRequestResult requestAcademyUpgrade(AcademyUpgrade kind);
  /** Offers a trialist a scholarship or, from the first professional
   * contract age, a professional contract. */
  YouthActionResult signYouthCandidate(PlayerID player_id);
  /** Lets a trialist go. */
  YouthActionResult releaseYouthCandidate(PlayerID player_id);
  /** Turns a scholarship into a first professional contract. */
  YouthActionResult offerYouthProfessionalContract(PlayerID player_id);
  /** Moves a U18 player up to the first-team squad. */
  YouthActionResult promoteYouthPlayer(PlayerID player_id);
  /** Moves a first-team player aged 18 or younger to the U18 squad. */
  YouthActionResult moveToYouthSquad(PlayerID player_id);

  // ========== U21 squad ==========
  // Implemented in game_controller_career.cpp.
  /** U21 candidates of the managed club: first-team players up to 21 and
   * the over-age ones up to 24 (status Graduated), U18 players from 17
   * (status Squad). */
  std::vector<YouthPlayerView> getReserveCandidates() const;
  struct ReserveOverview
  {
    ReserveQuota quota;
    int league_position = 0; /*!< In the country's U21 league. */
    int league_size = 0;
    YouthTableRow table;
    Language country = Language::EN;
  };
  ReserveOverview getReserveOverview() const;
  /** The country's U21 league, leader first. */
  std::vector<YouthTableRow> getReserveTable() const;
  /** Managed club's U21 results this season, oldest first. */
  const std::vector<YouthResult>& getReserveResults() const;
  /** A first-team or U18 player joins the U21 squad. */
  YouthActionResult moveToReserves(PlayerID player_id);
  /** A U21 player joins the first-team squad. */
  YouthActionResult promoteReservePlayer(PlayerID player_id);
  /** A U21 player aged 18 or younger goes back to the U18s. */
  YouthActionResult moveReserveToU18(PlayerID player_id);

  // ========== Manager career ==========
  /** A manager exists (created in the new-game step, or for older saves). */
  bool hasCareer() const;
  /**
   * The career runs without a club: Continue advances time, the inbox and
   * the Job Centre work, club screens and commands are closed.
   */
  bool isUnemployed() const;
  /** nullptr before the manager step. */
  const ManagerProfile* getManagerProfile() const;
  /** Creates the manager; the career starts without a club until
   * selectManagedTeam() or an accepted job offer. */
  void createManager(const ManagerSetup& setup);
  const std::vector<ManagerStint>& getManagerStints() const;
  const std::vector<ManagerSeasonLine>& getManagerSeasons() const;
  const std::vector<ManagerAward>& getManagerAwards() const;
  /** AI manager of a club (nullptr for the managed club and vacancies). */
  const AiManager* getClubManager(TeamID team_id) const;

  /** A vacancy as the Job Centre shows it. */
  struct VacancyView
  {
    TeamID team_id = 0;
    LeagueID league_id = 0;
    uint8_t tier = 1;
    uint8_t reputation = 0;
    int expected_position = 0; /*!< Board expectation (wage-bill rank). */
    const char* objective_key = "";
    OwnerType owner = OwnerType::Patient;
    float chance = 0.0f; /*!< Chance of an interview invitation. */
    CoachingLicence required_licence = CoachingLicence::None;
    GameDateValue opened = GameDateValue();
    std::optional<ApplicationStage> stage; /*!< When applied. */
  };
  /** Open jobs, best chance first. */
  std::vector<VacancyView> getVacancies() const;
  const std::vector<JobApplication>& getJobApplications() const;
  ApplyResult applyForJob(TeamID team_id);
  /** Answers the interview (one option per InterviewTopic). */
  std::optional<InterviewResult> attendInterview(
      TeamID team_id, std::span<const std::uint8_t> answers);
  const std::vector<JobOffer>& getJobOffers() const;
  OfferReply negotiateJobOffer(std::uint32_t offer_id, int64_t weekly_wage,
                               std::uint8_t years);
  /**
   * Takes the job: a manager under contract leaves his club (the new club
   * pays it the release compensation) and every managed-club binding moves.
   */
  bool acceptJobOffer(std::uint32_t offer_id);
  bool declineJobOffer(std::uint32_t offer_id);
  /** Leaves the managed club without compensation. */
  bool resignFromClub();
  /**
   * Continue while out of work: up to @p max_days, stopping early when an
   * offer or an interview invitation arrives. Returns the days simulated.
   */
  int advanceWhileUnemployed(int max_days = 7);

  // ========== National-team job ==========
  // Implemented in game_controller_career.cpp.
  /** The manager's national-team job (nullptr when he has none). */
  const NationalJob* getNationalJob() const;
  bool hasNationalJob() const;
  const std::vector<NationalStint>& getNationalJobHistory() const;
  /** A national team looking for a head coach, as the Job Centre shows it. */
  struct NationalVacancyView
  {
    Language nation = Language::EN;
    int rank = 0;         /*!< 1 = best-rated nation. */
    float stature = 0.0f; /*!< Club reputation scale. */
    float chance = 0.0f;  /*!< Chance of an offer after applying. */
    CoachingLicence required_licence = CoachingLicence::A;
    std::int64_t weekly_wage = 0;
    GameDateValue opened = GameDateValue();
    bool compatriot = false;
    std::optional<NationalApplicationStage> stage; /*!< When applied. */
  };
  /** Open national jobs, best chance first. */
  std::vector<NationalVacancyView> getNationalVacancies() const;
  NationalApplyResult applyForNationalJob(Language nation);
  const std::vector<NationalJobOffer>& getNationalJobOffers() const;
  /** Takes the job; refused while a club job cannot be combined with it. */
  NationalApplyResult acceptNationalJobOffer(std::uint32_t offer_id);
  bool declineNationalJobOffer(std::uint32_t offer_id);
  bool resignNationalJob();
  /** A club job and a national team together (reputation rule). */
  bool canCombineClubAndNation() const;

  /** One player the national team may call up. */
  struct CallUpCandidate
  {
    PlayerID id = 0;
    std::string name;
    PlayerRole role = PlayerRole::UNKNOWN;
    int age = 0;
    TeamID club = 0;
    int overall = 0;
    float form = 0.0f; /*!< Average match rating (0: no matches). */
    int condition = 0; /*!< 0-100. */
    uint16_t caps = 0;
    uint16_t goals = 0;
    bool available = true; /*!< Not injured. */
    bool selected = false; /*!< In the announced squad. */
  };
  /** The managed nation's call-up: squad, candidates and fixtures. */
  struct CallUpView
  {
    Language nation = Language::EN;
    bool announced = false; /*!< A squad is announced (editable or away). */
    bool locked = false;    /*!< The players have reported. */
    bool finals = false;
    GameDateValue start = GameDateValue();
    GameDateValue until = GameDateValue();
    GameDateValue next_announcement = GameDateValue(); /*!< When none yet. */
    size_t limit = International::WINDOW_SQUAD;
    /** Eligible players, the assistant's order (best first). */
    std::vector<CallUpCandidate> candidates;
    /** The nation's next matches (at most five). */
    std::vector<International::Fixture> fixtures;
  };
  CallUpView getCallUpView() const;
  /** Replaces the announced squad of the managed nation. */
  International::CallUpResult setNationalSquad(
      const std::vector<PlayerID>& players);

  // ========== Guidance: checklist, next steps, delegation, analysis ==========
  // Implemented in game_controller_guidance.cpp.
  /** First-week checklist of the career. */
  const OnboardingState& getOnboarding() const;
  /** Ticks a checklist step; false when it was already done. */
  bool completeOnboardingTask(OnboardingTask task);
  /** Hides the checklist (Help and Settings can bring it back). */
  void dismissOnboarding();
  /** Shows a hidden checklist again. */
  void showOnboarding();

  /** Pending work of the managed club, most important first. */
  std::vector<NextAction> getNextActions(size_t limit = 5) const;

  /** Who handles each duty of the managed club. */
  const DelegationPolicy& getDelegation() const;
  /** True when the assistant handles @p duty (systems check this). */
  bool isDelegated(Duty duty) const;
  /** Changes one duty (fixed duties refuse). */
  bool setDutyOwner(Duty duty, DutyOwner owner);
  void applyDelegationPreset(DelegationPreset preset);
  /** Staff member acting for the manager (assistant, else best coach). */
  const StaffMember* getDelegate() const;
  /**
   * A duty the manager took back by changing it by hand (training plan,
   * cancelled scouting trip, friendly); shown with an undo until dismissed.
   */
  std::optional<Duty> getReclaimedDuty() const { return reclaimed_duty; }
  /** Hands the reclaimed duty back to the assistant. */
  void undoReclaimedDuty();
  void dismissReclaimedDuty() { reclaimed_duty.reset(); }

  /** The managed club's next fixture. */
  struct NextFixture
  {
    GameDateValue date;
    TeamID opponent = 0;
    bool home = true;
    MatchType type = MatchType::LEAGUE;
  };
  std::optional<NextFixture> getNextManagedFixture() const;
  /** Opponent's form, likely XI (scouted estimates only), strengths,
   * weaknesses and counter-tactics. */
  OppositionReport getOppositionReport(TeamID opponent) const;
  /** Remembers (for this session) that the report was read. */
  void markOppositionReportViewed(TeamID opponent);
  bool wasOppositionReportViewed(TeamID opponent) const;
  /** Instruction against an opposing player for the next meeting. */
  bool setOppositionInstruction(TeamID opponent, PlayerID player,
                                OppositionInstruction instruction);
  OppositionInstruction getOppositionInstruction(TeamID opponent,
                                                 PlayerID player) const;
  /** Instructions against @p opponent (for the match engine). */
  std::vector<OppositionOrder> getOppositionInstructions(TeamID opponent) const;
  /** Adds a counter-tactic's slider changes to the managed tactic. */
  bool applyCounterTactic(const CounterTactic& counter);

  /** Data hub of the managed club: this season's matches and players. */
  struct DataHubView
  {
    TeamAnalytics team;
    std::vector<PlayerAnalyticsRow> players;
  };
  DataHubView getDataHub() const;
  /** Tracked data (shots, touch maps, pass network) of a managed club's
   * match, if it was recorded. */
  std::optional<ManagedMatchSnapshot> getMatchSnapshot(GameDateValue date,
                                                       TeamID home_id,
                                                       TeamID away_id) const;

  /** A decision message still waits for the manager (offer, player
   * request, youth trialists, decision moment). */
  bool isInboxDecisionPending(const InboxMessage& message) const;

  // ========== Honours: awards, records, hall of fame ==========
  /** Every league honour given so far, oldest first. */
  const std::vector<AwardRecord>& getAwardHistory() const;
  /** Honours of a league in a season, in award order. */
  std::vector<AwardRecord> getLeagueAwards(LeagueID league_id,
                                           uint16_t season_year) const;
  /** Honours of a player, newest first (profile honours section). */
  std::vector<AwardRecord> getPlayerHonours(PlayerID player_id) const;
  /** Current season award race of a league (qualified players first,
   * then by the award score); @p young keeps players up to 21. */
  std::vector<AwardPlayerTally> getAwardRace(LeagueID league_id, bool young,
                                             size_t limit = 5) const;
  std::vector<RecordEntry> getClubRecords(TeamID team_id) const;
  std::vector<RecordEntry> getLeagueRecords(LeagueID league_id) const;
  std::vector<ClubPlayerTotal> getClubTopScorers(TeamID team_id,
                                                 size_t limit = 10) const;
  std::vector<ClubPlayerTotal> getClubMostAppearances(TeamID team_id,
                                                      size_t limit = 10) const;
  std::vector<AllTimeRow> getAllTimeTable(LeagueID league_id) const;
  std::vector<LegendEntry> getHallOfFame(TeamID team_id) const;

  // ========== Board: facility projects ==========
  /** Quote for the managed club (seats only matter for stadiums). */
  ProjectQuote getProjectQuote(FacilityProjectType type,
                               uint32_t seats = 0) const;
  /** Asks the board to fund a project for the managed club. */
  ProjectVerdict requestFacilityProject(FacilityProjectType type,
                                        uint32_t seats = 0);
  /** Managed club's projects, running first. */
  std::vector<FacilityProject> getFacilityProjects() const;
  /** Medical centre level of a club (50 = standard). */
  uint8_t getMedicalLevel(TeamID team_id) const;
  /** Day a refused project type may be asked for again. */
  std::optional<GameDateValue> getProjectCooldown(
      FacilityProjectType type) const;

  // ========== Pre-season planner ==========
  std::vector<FriendlySlot> getPreseasonFriendlies() const;
  std::vector<OpponentOption> getFriendlyOpponents(GameDateValue date,
                                                   OpponentLevel level,
                                                   bool abroad) const;
  bool setPreseasonFriendly(GameDateValue date, TeamID opponent_id, bool home,
                            bool tour);
  /** The assistant's friendlies for the editable dates. */
  std::vector<FriendlySuggestion> getPreseasonSuggestion() const;
  /** Applies the assistant's friendlies; returns how many were set. */
  size_t applyPreseasonSuggestion();
  CampQuote getCampQuote(TrainingCamp camp) const;
  TrainingCamp getSuggestedCamp() const;
  bool bookTrainingCamp(TrainingCamp camp);
  const PreseasonState& getPreseasonState() const;
  /** Net fee of a tour match against @p opponent_id. */
  int64_t getTourFee(TeamID opponent_id) const;

  // ========== Mentoring groups ==========
  std::vector<MentoringGroup> getMentoringGroups() const;
  MentoringError createMentoringGroup(PlayerID mentor_id,
                                      uint32_t* group_id = nullptr);
  MentoringError addMentee(uint32_t group_id, PlayerID mentee_id);
  MentoringError removeMentee(uint32_t group_id, PlayerID mentee_id);
  MentoringError dissolveMentoringGroup(uint32_t group_id);
  /** Development multiplier a player gets from his mentor (1.0 = none). */
  float getMentoringMultiplier(PlayerID player_id) const;

  // ========== Holiday / continue until ==========
  HolidayPreferences getHolidayPreferences() const;
  void setHolidayPreferences(const HolidayPreferences& preferences);
  /** Day a plan would end on (nullopt: open-ended or nothing to wait for);
   * "next match" means the next fixture after today. */
  std::optional<GameDateValue> getHolidayTarget(const HolidayPlan& plan) const;
  /**
   * Simulates days on the Continue machinery (progress in
   * getContinueProgress()) with the assistant in charge, until the plan's
   * target or an early stop; the report is in getHolidaySummary().
   * @return Days advanced.
   */
  int goOnHoliday(const HolidayPlan& plan);
  const HolidaySummary& getHolidaySummary() const { return holiday_summary; }

  /** Seed of the current world. */
  uint64_t getWorldSeed() const;

  const Game* getGame() const { return game.get(); }
  Game* getGame() { return game.get(); }

  /** @brief Gets the database connection (for repos that need it). */
  std::shared_ptr<DatabaseConnection> getDbConn() const { return db_conn; }

  /** @brief Gets the game data cache. */
  std::shared_ptr<GameData> getGameData() const { return gamedata; }

  /** Editing pauses career advancement. UI callers must also exclude live
   * matches and discard scene caches at a frame boundary after apply/undo.
   * The session database is private; cancelling only releases the pause. */
  struct EditorSession
  {
    std::shared_ptr<DatabaseConnection> database;
    std::shared_ptr<DatabaseConnection> source;
    int source_changes = 0;
    std::string source_schema;
  };
  std::shared_ptr<EditorSession> beginEditor();
  void cancelEditor();
  /** Contains loader, codec and feature-validator exceptions at the runtime
   * replacement boundary; failures preserve the live career and draft. */
  bool applyEditor(const std::shared_ptr<EditorSession>& session,
                   const std::vector<RuntimeEditor::Change>& changes,
                   std::string& error);
  bool undoEditor(std::string& error);
  bool canUndoEditor() const { return editor_backup != nullptr; }
  void registerEditorValidator(RuntimeEditor::Validator validator);

  /** Number of successful runtime replacements; used to invalidate UI caches.
   */
  std::uint64_t getEditorRevision() const { return editor_revision; }

 private:
  mutable std::mutex runtime_mutex;
  std::weak_ptr<EditorSession> editor_session;
  std::shared_ptr<DatabaseConnection> editor_backup;
  std::vector<RuntimeEditor::Validator> editor_validators;
  std::uint64_t editor_revision = 0;
  void replaceEditorDatabase(std::shared_ptr<DatabaseConnection> connection);
  std::shared_ptr<class DatabaseConnection> db_conn;
  std::unique_ptr<Game> game;
  std::shared_ptr<class GameData> gamedata;

  std::unordered_map<PlayerID, TransferListing> transfer_listings;
  std::mt19937 transfer_rng;
  float last_initialization_milliseconds = 0.0f;
  std::atomic<int> continue_days_started{0};
  std::atomic<int> continue_days_total{0};
  std::atomic<bool> continue_stop_requested{false};
  std::atomic<ContinueStop> last_continue_stop{ContinueStop::None};
  HolidaySummary holiday_summary;
  /** The manager's own delegation while a holiday hands extra duties to the
   * assistant: saves made on holiday store this one. */
  std::optional<DelegationPolicy> delegation_after_holiday;

  int current_slot = -1;
  AutosavePolicy autosave_policy;
  GameDateValue last_autosave_date;
  int last_autosave_season = 0;
  std::int64_t playtime_before_session = 0;
  std::chrono::steady_clock::time_point session_started;
  std::optional<SaveError> last_load_error;
  mutable std::mutex club_identity_mutex;
  /** Data-pack club identities, read on the first getClubIdentity(). */
  mutable std::optional<std::unordered_map<TeamID, ClubIdentity>>
      club_identities;
  mutable std::mutex save_status_mutex;
  SaveStatusInfo save_status;
  /** Flushes the game and atomically replaces the slot file. */
  /** Caller must hold runtime_mutex, including autosaves during advancement. */
  bool persistLocked(bool autosave);
  void maybeAutosave();
  void startSession(int slot, std::int64_t playtime_seconds);
  /** One day of simulation plus the AI transfer activity that follows. */
  void simulateDay();
  bool executeTransfer(PlayerID pid, TeamID buyer_id, TeamID seller_id,
                       uint32_t price,
                       std::optional<ContractTerms> contract = std::nullopt);
  void processAITransferActivity();
  void evaluateIncomingAIBids();
  /** Buyers' answers to the club's counters that are due today. */
  void processOfferReplies();
  /** Applies the buyer's answer to the counter of offer @p offer_id. */
  OfferOutcome answerCounter(std::uint32_t offer_id);
  /** Applies the borrower's answer to the loan counter of @p offer_id. */
  OfferOutcome answerLoanCounter(std::uint32_t offer_id);
  OfferOutcome sendCounter(std::uint32_t offer_id,
                           const TransferNegotiation::OfferTerms& terms,
                           bool firm);
  /** An AI club's bid on a listed managed player becomes an offer the
   * manager can negotiate. */
  bool routeListingBid(PlayerID pid, TeamID bidder_id, uint32_t bid,
                       bool announce = true);
  /** Listing bids for managed players (older saves) become offers. */
  void absorbListingBids();
  /** How a managed player feels about joining @p buyer. */
  BuyerNegotiation::PlayerStance playerStance(PlayerID pid, TeamID buyer) const;
  /** Drops listings of players who moved, left or may not be traded. */
  void purgeStaleListings();
  /** Withdraws the bids a club placed while it had no manager. */
  void clearBidsBy(TeamID team_id);
  /** AI seller accepts the highest bid as a structured deal. */
  bool completeAiSale(PlayerID pid);
  /** Asking price of a live listing by the player's club (0 if none). */
  uint32_t listingPrice(PlayerID pid) const;
  /** Signing-day cash of a deal fits the buyer's transfer budget and the
   * new wage its wage budget. */
  bool canPayDeal(const TransferMarket::Deal& deal) const;
  /** A pre-contract's wage fits next season's wage room and its bonus and
   * agent fee the transfer money not yet committed. */
  bool canPayPreContract(const TransferMarket::Deal& deal) const;

  /** Opponents whose report was opened this session. */
  std::vector<TeamID> viewed_opposition;
  /** The assistant's delegated daily jobs (after each simulated day). */
  void runDelegatedDuties();
  /** Set while the assistant changes things on the manager's behalf. */
  bool assistant_acting = false;
  std::optional<Duty> reclaimed_duty;
  /** A manual change of something the assistant owns hands it back. */
  void reclaimDuty(Duty duty);
  /** Match snapshot and checklist after a managed live match. */
  void recordManagedMatch(GameDateValue date, TeamID home_id, TeamID away_id,
                          const MatchEngine& engine);

  /** The managed club, only once one has been selected. */
  std::optional<std::reference_wrapper<Team>> managedClub();
  std::optional<std::reference_wrapper<const Team>> managedClub() const;
  /** Copies the opposition plan into the managed club's tactics, which
   * carry it into every match engine (live or simulated). */
  void syncOppositionOrders();
};
