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
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "global/types.h"
#include "model/buyer_negotiation.h"
#include "model/gamedate.h"
#include "model/inbox.h"
#include "model/loan_negotiation.h"
#include "model/transfer_listing.h"
#include "model/transfer_negotiation.h"

class CompetitionManager;
class DatabaseConnection;
class GameData;
class Player;
class Team;
class WorldRng;
class WorldSimulation;

/**
 * @enum TransferKind
 * @brief Type of a completed move (values are persisted).
 */
enum class TransferKind : std::uint8_t
{
  Permanent = 0, /*!< Club-to-club move with a fee. */
  Free,          /*!< Free agent signing. */
  Loan,
  LoanReturn,
  PreContract, /*!< Bosman move agreed six months ahead. */
  Release      /*!< Contract terminated by the club. */
};

/** Language key naming @p kind (e.g. "TRANSFER_KIND_LOAN"). */
const char* transferKindKey(TransferKind kind);

/** One entry of the transfer history. */
struct TransferRecord
{
  PlayerID player_id = 0;
  GameDateValue date;
  TeamID from_team = 0;
  TeamID to_team = 0;
  std::uint32_t fee = 0;
  TransferKind kind = TransferKind::Permanent;
};

/**
 * @enum ObligationKind
 * @brief Future payments created by a deal (values are persisted).
 */
enum class ObligationKind : std::uint8_t
{
  Instalment = 0,  /*!< amount due on @c due. */
  AppearanceBonus, /*!< amount due at @c target appearances for the payer. */
  GoalBonus,       /*!< amount due at @c target goals for the payer. */
  SellOn,          /*!< payee gets @c amount % of the player's next fee. */
  /** Loan clause: the borrower (payer) pays @c amount to the parent when
   * the player made fewer than @c target appearances for it by @c due. */
  LoanUnplayedFee,
  /** Contract: his wage rises by @c amount % on @c due, then yearly. */
  WageRise,
  /** Contract: @c amount for each appearance for the payer beyond
   * @c baseline (raised as they are paid). */
  AppearanceFee,
  /** Agent fee of a pre-contract, paid when it is executed. */
  AgentFee
};

/** A scheduled or conditional payment between two clubs. */
struct TransferObligation
{
  std::uint32_t id = 0;
  ObligationKind kind = ObligationKind::Instalment;
  PlayerID player_id = 0;
  TeamID payer = 0; /*!< Sell-on: the club that will sell. */
  TeamID payee = 0;
  std::int64_t amount = 0;
  GameDateValue due; /*!< Instalment due date, otherwise the signing date. */
  std::uint16_t target = 0; /*!< Bonus threshold. */
  std::uint16_t baseline =
      0; /*!< Appearances/goals for the payer at signing. */
};

/** An active loan. The player belongs to the borrower's squad. */
struct LoanDeal
{
  PlayerID player_id = 0;
  TeamID parent = 0;
  TeamID borrower = 0;
  GameDateValue start;
  GameDateValue end;             /*!< Last day; the player returns then. */
  std::uint8_t wage_share = 100; /*!< % of the wage paid by the borrower. */
  std::uint32_t full_wage = 0;
  std::uint32_t option_fee = 0; /*!< 0 = no buy clause. */
  bool obligation = false;
  bool recall_clause = false;
};

/** A Bosman agreement executed on 1 July. */
struct PreContractDeal
{
  PlayerID player_id = 0;
  TeamID from_team = 0;
  TeamID to_team = 0;
  GameDateValue agreed;
  TransferNegotiation::ContractOffer terms;
};

/** Contract extras, loan-list and not-for-sale flags of a player. */
struct PlayerMarketFlags
{
  std::uint32_t release_clause = 0;
  std::optional<SquadRole> promised_role;
  GameDateValue promise_date;
  bool loan_listed = false;
  /** Declared not for sale until this day (0 = never). */
  std::int32_t not_for_sale_until = 0;

  bool empty() const
  {
    return release_clause == 0 && !promised_role && !loan_listed &&
           not_for_sale_until == 0;
  }
};

/** Whose move an incoming offer waits for (values are persisted). */
enum class OfferStatus : std::uint8_t
{
  AwaitingClub = 0, /*!< The managed club has to answer. */
  AwaitingBuyer     /*!< The buyer answers the club's counter on respond_on. */
};

/** One step of the talks over an incoming offer. */
struct OfferRound
{
  GameDateValue date;
  BuyerNegotiation::Move move = BuyerNegotiation::Move::Bid;
  TransferNegotiation::OfferTerms terms;
  TransferNegotiation::LoanTerms loan_terms; /*!< Loan offers. */
};

/** An AI club's offer for one of the managed club's players. */
struct IncomingOffer
{
  std::uint32_t id = 0;
  PlayerID player_id = 0;
  TeamID buyer = 0;
  bool loan = false;
  TransferNegotiation::OfferTerms terms; /*!< The buyer's offer on the table. */
  TransferNegotiation::LoanTerms loan_terms;
  /** Hidden ceiling of the buyer: the most the deal may cost it, in
   * BuyerNegotiation::buyerCost() terms (present value); for a loan in
   * LoanNegotiation::borrowerCost() terms. */
  std::uint32_t max_fee = 0;
  GameDateValue created;
  GameDateValue expires;  /*!< Last day to answer (AwaitingClub). */
  std::uint8_t round = 0; /*!< Counters the buyer has answered. */
  /** Counters the buyer answers before it stops; 0 = not drawn yet. */
  std::uint8_t patience = 0;
  std::uint8_t insults = 0; /*!< Unrealistic demands so far. */
  OfferStatus status = OfferStatus::AwaitingClub;
  GameDateValue respond_on; /*!< AwaitingBuyer: the day of its answer. */
  TransferNegotiation::OfferTerms asked;     /*!< AwaitingBuyer: the counter. */
  TransferNegotiation::LoanTerms asked_loan; /*!< Loan counter. */
  bool firm = false;               /*!< The counter is a named price. */
  std::vector<OfferRound> history; /*!< Oldest first. */
};

/** A club whose talks for a managed player ended (rejected, withdrawn,
 * ignored) does not bid for him again before @c until. */
struct TalksCooldown
{
  PlayerID player_id = 0;
  TeamID buyer = 0;
  GameDateValue until;
};

/** The managed club's talks as a buyer. */
struct Negotiation
{
  PlayerID player_id = 0;
  TeamID seller = 0;
  TransferNegotiation::ContractKind kind =
      TransferNegotiation::ContractKind::Transfer;
  TransferNegotiation::OfferTerms agreed; /*!< Fee agreed with the seller. */
  bool club_agreed = false;
  std::uint8_t club_rounds = 0;
  std::uint8_t player_rounds = 0;
  GameDateValue expires;
};

/**
 * @class TransferMarket
 * @brief Deals, loans, pre-contracts, payments and history of the market.
 *
 * Owned by Game next to the world simulation. It executes structured moves
 * (fees with instalments, add-ons and sell-on clauses, agent fees, signing
 * bonuses, loans and releases), runs the scheduled parts of existing deals
 * every day and drives the AI moves that do not go through the transfer
 * list (free agents, loans, releases, pre-contracts and approaches for the
 * managed club's players). All randomness comes from world RNG streams.
 */
class TransferMarket
{
 public:
  /** A structured permanent move. */
  struct Deal
  {
    PlayerID player_id = 0;
    TeamID buyer_id = 0;
    TransferKind kind = TransferKind::Permanent;
    TransferNegotiation::OfferTerms terms;
    TransferNegotiation::ContractOffer contract;
    /** Agreed in an earlier contract (a loan's purchase clause): the
     * seller's goalkeeper floor does not apply. */
    bool binding = false;
  };

  TransferMarket(std::shared_ptr<GameData> gamedata, WorldSimulation& world,
                 const CompetitionManager& competitions);

  // ---- Lifecycle ----

  /** Loan wages and returns, pre-contracts (1 July), instalments,
   * add-ons, promises, offer expiry and the weekly news digest. Offers and
   * talks of players who left football (retired, released youngsters) are
   * dropped first. */
  void onDayAdvanced(const GameDateValue& date, TeamID managed_team_id);
  /** Drops offers, talks, flags and cooldowns of players who no longer
   * exist (retirement, youth releases). */
  void forgetRemovedPlayers();

  void load(const std::shared_ptr<DatabaseConnection>& db_conn);
  /** Writes the market state inside the caller's transaction. */
  void save(const std::shared_ptr<DatabaseConnection>& db_conn) const;
  void onSaved();

  // ---- Decision contexts ----

  TransferNegotiation::SaleContext saleContext(
      PlayerID player_id, TeamID buyer_id, const GameDateValue& date,
      std::uint32_t listing_price) const;
  TransferNegotiation::PlayerContext playerContext(
      PlayerID player_id, TeamID club_id,
      TransferNegotiation::ContractKind kind) const;
  /** Role the player would have by ability rank at @p club_id. */
  SquadRole projectedRole(PlayerID player_id, TeamID club_id) const;
  /** True if the player accepts the terms he demands from @p club_id. */
  bool wouldJoin(PlayerID player_id, TeamID club_id,
                 TransferNegotiation::ContractKind kind) const;

  // ---- Moves ----

  /** Fee or free move with the full deal structure; false if invalid. */
  bool completeTransfer(const Deal& deal, const GameDateValue& date,
                        TeamID managed_team_id);
  bool startLoan(PlayerID player_id, TeamID borrower_id,
                 const TransferNegotiation::LoanTerms& terms,
                 const GameDateValue& date, TeamID managed_team_id);
  /** Sends a loanee back (end of term or recall). */
  bool endLoan(PlayerID player_id, const GameDateValue& date,
               TeamID managed_team_id, bool recalled);
  /** Borrower buys the loanee for the option fee. */
  bool exerciseLoanOption(PlayerID player_id, const GameDateValue& date,
                          TeamID managed_team_id);
  /** Terminates the contract, paying the remaining wages. */
  bool releasePlayer(PlayerID player_id, const GameDateValue& date,
                     TeamID managed_team_id);
  bool agreePreContract(PlayerID player_id, TeamID club_id,
                        const TransferNegotiation::ContractOffer& terms,
                        const GameDateValue& date, TeamID managed_team_id);
  /** History and sell-on clauses of a move made by the legacy path. */
  void recordLegacyTransfer(PlayerID player_id, TeamID from_team,
                            TeamID to_team, std::uint32_t fee,
                            const GameDateValue& date);

  // ---- Rules ----

  /** Not on loan and not committed to another club. */
  bool canBeTraded(PlayerID player_id) const;
  bool loanWithinLimits(PlayerID player_id, TeamID parent,
                        TeamID borrower) const;
  /** Instalments a club owes from @p date until the season ends. */
  std::int64_t committedPayables(TeamID team_id,
                                 const GameDateValue& date) const;
  /**
   * Transfer money a club can commit on @p date: the board's allowance
   * capped by the cash left after the payroll reserve and the instalments
   * still owed this season (ClubEconomy::availableTransferBudget); zero
   * under a transfer embargo.
   */
  std::int64_t spendableBudget(TeamID team_id, const GameDateValue& date) const;
  /** Takes a departed player out of the managed club's line-up, filling
   * his place without rebuilding the rest of the manager's selection. */
  void removeFromLineup(Team& team, const Player& departed) const;
  /** Agent fee paid by the buyer: the one agreed in the contract, else
   * 10% of a fee, or weeks of wage. */
  static std::uint32_t agentFee(const Deal& deal);
  /** Weekly wages a club can still commit (budget less payroll and the
   * wages of its agreed pre-contracts). */
  std::int64_t wageRoom(TeamID club_id) const
  {
    return aiWageRoom(club_id, false);
  }
  /** Wage room next season: contracts running past 30 June and agreed
   * pre-contracts count, players in their final year do not. */
  std::int64_t nextSeasonWageRoom(TeamID club_id) const
  {
    return aiWageRoom(club_id, true);
  }
  /** Signing bonuses and agent fees due on the club's agreed
   * pre-contracts. */
  std::int64_t committedPreContractCosts(TeamID club_id) const
  {
    return preContractCosts(club_id);
  }
  /** False when @p leaving going would leave @p club_id fewer than
   * MIN_SENIOR_SQUAD senior players or no senior goalkeeper; @p goalkeeper
   * tells which (null to ignore). */
  bool keepsSquadFloor(TeamID club_id, PlayerID leaving,
                       bool* goalkeeper = nullptr) const;
  /** Fewest senior players a managed squad may keep after a release or
   * a sale. */
  static constexpr std::size_t MIN_SENIOR_SQUAD = 11;
  /** False when @p leaving is a senior goalkeeper and @p club_id would keep
   * fewer than Market::MIN_AI_SENIOR_KEEPERS of them (applied to the AI's
   * sales, loans and releases). */
  bool keepsAiKeepers(TeamID club_id, PlayerID leaving) const;
  /** Senior players of a club plus the pre-contracts joining it. */
  std::size_t seniorSquadSize(TeamID club_id) const;
  /**
   * Renews a player's contract at his club with @p offer: wage, seasons
   * (the current one included), extras, release clause and promise; the
   * signing bonus and agent fee are paid today. False if invalid.
   */
  bool renewContract(PlayerID player_id,
                     const TransferNegotiation::ContractOffer& offer,
                     const GameDateValue& date);
  /** What players of his ability and age earn at @p club_id (the club's
   * pay scale applied to his wage index); 0 when unknown. */
  std::uint32_t deservedWage(PlayerID player_id, TeamID club_id) const;
  /** @p weekly_wage fits @p room and the single-player share of the
   * club's wage budget. */
  static bool affordsWage(const Team& club, std::uint32_t weekly_wage,
                          std::int64_t room)
  {
    return aiAffordsWage(club, weekly_wage, room);
  }
  /** Context of @p club_id borrowing @p player_id from today (budget, wage
   * room, how often he would play); ceiling and talks state left at 0. */
  LoanNegotiation::BorrowerContext borrowerContext(
      PlayerID player_id, TeamID club_id, const GameDateValue& date) const;
  /** Appearances for @p team_id in his career (competitive matches). */
  std::uint16_t appearancesFor(PlayerID player_id, TeamID team_id) const
  {
    return careerCount(player_id, team_id, ObligationKind::AppearanceBonus);
  }
  /** Gives some ambitious players at modest clubs a release clause in a
   * new world (deterministic from the world seed). */
  void seedReleaseClauses();
  /** The 30 June expiries of a new world: some players outside each
   * club's matchday squad start without a club (deterministic from the
   * world seed; no history, no money). */
  void seedSummerFreeAgents();
  /** Release clause of a player (0 = none). */
  std::uint32_t releaseClause(PlayerID player_id) const;
  void setReleaseClause(PlayerID player_id, std::uint32_t clause);
  /**
   * An AI club pays a player's release clause in cash (within its budget
   * and wage room; never leaving the managed squad short): his club cannot
   * refuse and he joins on the terms he demands. True if he moved.
   */
  bool payReleaseClause(TeamID club_id, PlayerID player_id,
                        const GameDateValue& date, TeamID managed_team_id);

  // ---- AI ----

  /**
   * One AI club's business on a visit: sheds surplus (loan-listing
   * prospects, releasing veterans), then fills its biggest need (a missing
   * position or a weak starter) with a free agent, a loan or a structured
   * fee deal with another AI club. Returns true when a player moved.
   */
  bool runAiClub(TeamID club_id,
                 const std::unordered_map<PlayerID, TransferListing>& listings,
                 const GameDateValue& date, TeamID managed_team_id,
                 WorldRng& rng, bool free_agents_only);
  /** Offers an AI club's surplus prospects for loan (no move). */
  void listLoanProspects(TeamID club_id);
  /** AI clubs agree pre-contracts (1 January - 30 June). */
  void runAiPreContracts(const GameDateValue& date, TeamID managed_team_id,
                         WorldRng& rng, int attempts);
  /** An AI club may bid for one of the managed club's players. */
  void runAiApproach(TeamID club_id, const GameDateValue& date,
                     TeamID managed_team_id, WorldRng& rng);

  // ---- Managed club offers and talks ----

  const std::vector<IncomingOffer>& incomingOffers() const { return incoming; }
  const IncomingOffer* findIncomingOffer(std::uint32_t offer_id) const;
  /** Adds an offer with a fresh id; a transfer offer without history gets
   * its opening bid as the first round, and a default patience. */
  std::uint32_t addIncomingOffer(IncomingOffer offer);
  bool updateIncomingOffer(const IncomingOffer& offer);
  bool removeIncomingOffer(std::uint32_t offer_id);
  /** Transfer offers whose buyer answers on or before @p date, by id. */
  std::vector<std::uint32_t> dueOfferReplies(const GameDateValue& date) const;
  /** Other clubs with a transfer offer for the same player. */
  std::uint8_t rivalBids(const IncomingOffer& offer) const;
  /** Last day to answer an offer of @p buyer made on @p date: @p days
   * later, but never after the buyer's transfer window closes. */
  GameDateValue answerDeadline(TeamID buyer, const GameDateValue& date,
                               int days) const;
  /** Transfer window of @p club's country on @p date: the buying club's
   * window decides whether it can sign a player (TransferWindows). */
  TransferNegotiation::WindowInfo clubWindow(TeamID club,
                                             const GameDateValue& date) const;
  /** Talks of @p buyer for @p player_id ended on @p date: the club does
   * not come back for him for a while (at most until the window closes). */
  void closeTalks(PlayerID player_id, TeamID buyer, const GameDateValue& date);
  bool talksClosed(PlayerID player_id, TeamID buyer,
                   const GameDateValue& date) const;
  const std::vector<TalksCooldown>& closedTalks() const { return cooldowns; }

  /** Open talks of the managed club by player. */
  const std::unordered_map<PlayerID, Negotiation>& talks() const
  {
    return negotiations;
  }
  const Negotiation* findNegotiation(PlayerID player_id) const;
  void setNegotiation(const Negotiation& negotiation);
  void removeNegotiation(PlayerID player_id);

  // ---- Flags ----

  const PlayerMarketFlags* flags(PlayerID player_id) const;
  void setLoanListed(PlayerID player_id, bool listed);
  bool isLoanListed(PlayerID player_id) const;
  /** Clubs are told the player is not for sale until @p until (they do
   * not approach him before then). */
  void setNotForSale(PlayerID player_id, const GameDateValue& until);
  bool isNotForSale(PlayerID player_id, const GameDateValue& date) const;

  // ---- Queries ----

  const LoanDeal* findLoan(PlayerID player_id) const;
  const std::unordered_map<PlayerID, LoanDeal>& loans() const
  {
    return active_loans;
  }
  const PreContractDeal* findPreContract(PlayerID player_id) const;
  const std::unordered_map<PlayerID, PreContractDeal>& preContracts() const
  {
    return pre_contracts;
  }
  /** Every move, oldest first. */
  const std::vector<TransferRecord>& history() const { return records; }
  /** Moves of one player, oldest first. */
  std::vector<TransferRecord> historyFor(PlayerID player_id) const;
  const std::vector<TransferObligation>& obligations() const
  {
    return pending_obligations;
  }

 private:
  Player* mutablePlayer(PlayerID player_id);
  std::string teamName(TeamID team_id) const;
  void movePlayer(PlayerID player_id, TeamID from_team, TeamID to_team,
                  TeamID managed_team_id);
  void pay(TeamID payer, TeamID payee, std::int64_t amount,
           const GameDateValue& date);
  /** Pays the sell-on clauses of @p seller's sale to @p buyer; a clause
   * whose beneficiary is the buyer itself lapses unpaid. */
  std::int64_t paySellOns(PlayerID player_id, TeamID seller, TeamID buyer,
                          std::uint32_t fee, const GameDateValue& date);
  void addRecord(const TransferRecord& record);
  void post(const GameDateValue& date, InboxCategory category,
            std::string title_key, std::string body_key,
            std::vector<std::string> args, std::optional<PlayerID> player_id,
            std::optional<TeamID> team_id);
  std::uint16_t careerCount(PlayerID player_id, TeamID team_id,
                            ObligationKind kind) const;
  PlayerMarketFlags& mutableFlags(PlayerID player_id);
  void pruneFlags(PlayerID player_id);
  void clearOnMove(PlayerID player_id);
  void recordTransferClauses(const Deal& deal, TeamID seller_id,
                             TeamID buyer_id,
                             const TransferNegotiation::OfferTerms& terms,
                             const GameDateValue& date);

  /** Position group an AI club wants to strengthen and the level a
   * signing must reach. */
  struct AiNeed
  {
    PlayerRole group = PlayerRole::UNKNOWN;
    float min_overall = 0.0f;
    bool depth = false;   /*!< Cover for a thin squad, not a starter. */
    bool upgrade = false; /*!< A better starter, the position is filled. */
  };
  /** @p level_boost raises the level starters are measured against. */
  std::optional<AiNeed> assessNeed(
      const std::vector<std::pair<double, PlayerID>>& ranked,
      float level_boost = 0.0f) const;
  /**
   * Weekly wages an AI club can still commit: its wage budget less the
   * payroll and the wages of the pre-contracts it has agreed. With
   * @p next_season only contracts that run beyond this season count, and
   * its players out on loan are back.
   */
  std::int64_t aiWageRoom(TeamID club_id, bool next_season) const;
  /** Signing bonuses and agent fees due on the club's pre-contracts. */
  std::int64_t preContractCosts(TeamID club_id) const;
  /** A wage within @p room and the single-player share of the budget. */
  static bool aiAffordsWage(const Team& club, std::uint32_t weekly_wage,
                            std::int64_t room);
  /** Clubs promoted or relegated at the last season's close. */
  bool lastSeasonMove(TeamID club_id, bool promoted) const;
  /** Terminates a contract; @p severance_due pays the remaining wages
   * (a relegation clause ends it for free). */
  bool endContract(PlayerID player_id, const GameDateValue& date,
                   TeamID managed_team_id, bool severance_due);
  void releaseOnRelegation(const GameDateValue& date, TeamID managed_team_id);
  void lowerFreeAgentExpectations();
  void listLoanProspects(
      const std::vector<std::pair<double, PlayerID>>& ranked);
  bool aiShedSurplus(TeamID club_id,
                     const std::vector<std::pair<double, PlayerID>>& ranked,
                     const GameDateValue& date, TeamID managed_team_id,
                     WorldRng& rng);
  bool releaseSeniorSurplus(
      const std::vector<std::pair<double, PlayerID>>& ranked,
      const GameDateValue& date, TeamID managed_team_id,
      std::size_t max_squad_size);
  bool placeSurplusLoans(TeamID club_id,
                         const std::vector<std::pair<double, PlayerID>>& ranked,
                         const GameDateValue& date, TeamID managed_team_id,
                         WorldRng& rng);
  bool placeSurplusLoan(TeamID club_id, PlayerID player_id,
                        const GameDateValue& date, TeamID managed_team_id,
                        WorldRng& rng);
  std::optional<PlayerID> bestLoanCandidate(TeamID club_id, const AiNeed& need,
                                            TeamID managed_team_id) const;
  bool aiSignFreeAgent(TeamID club_id, const AiNeed& need,
                       std::int64_t wage_room, const GameDateValue& date,
                       TeamID managed_team_id);
  bool aiTakeLoan(TeamID club_id, const AiNeed& need, std::int64_t wage_room,
                  const GameDateValue& date, TeamID managed_team_id,
                  WorldRng& rng);
  bool aiBuy(TeamID club_id, const AiNeed& need, std::int64_t wage_room,
             const std::unordered_map<PlayerID, TransferListing>& listings,
             const GameDateValue& date, TeamID managed_team_id, WorldRng& rng);
  std::vector<std::pair<double, PlayerID>> aiBuyCandidates(
      TeamID club_id, const AiNeed& need,
      const std::unordered_map<PlayerID, TransferListing>& listings,
      TeamID managed_team_id, WorldRng& rng) const;
  bool tryAiBuyCandidate(
      TeamID club_id, PlayerID player_id, std::int64_t budget,
      std::int64_t wage_room,
      const std::unordered_map<PlayerID, TransferListing>& listings,
      const GameDateValue& date, TeamID managed_team_id);
  bool tryPreContractClub(TeamID club_id,
                          const std::vector<PlayerID>& candidates,
                          const GameDateValue& date, TeamID managed_team_id,
                          WorldRng& rng);

  void payLoanWageShares(const GameDateValue& date);
  void processLoanEnds(const GameDateValue& date, TeamID managed_team_id);
  void executePreContracts(const GameDateValue& date, TeamID managed_team_id);
  void payDueInstalments(const GameDateValue& date, TeamID managed_team_id);
  void checkAddOns(const GameDateValue& date, TeamID managed_team_id);
  void checkPromises(const GameDateValue& date, TeamID managed_team_id);
  void expireOffers(const GameDateValue& date);
  void postNewsDigest(const GameDateValue& date, TeamID managed_team_id);
  /** On the day a window shuts: the deals of its last days. */
  void postDeadlineSummary(const GameDateValue& date, TeamID managed_team_id);
  struct DeadlineSummary
  {
    int fee_moves = 0;
    int loan_moves = 0;
    int other_moves = 0;
    std::int64_t spent = 0;
    std::vector<const TransferRecord*> biggest;
    std::vector<const TransferRecord*> ours;
  };
  DeadlineSummary collectDeadlineSummary(std::int32_t first, std::int32_t last,
                                         TeamID managed_team_id) const;
  std::string describeDeadlineDeals(
      const std::vector<const TransferRecord*>& deals) const;
  /** Yearly rises due on @p date and weekly appearance money. */
  void applyWageRises(const GameDateValue& date);
  void payAppearanceFees(const GameDateValue& date);
  /** Contract extras (rise, appearance money) of @p contract for the
   * player at @p club_id from @p start; old clubs' extras lapse. */
  void addContractExtras(PlayerID player_id, TeamID club_id,
                         const TransferNegotiation::ContractOffer& contract,
                         const GameDateValue& start);
  /** Settles the appearance clause of a loan ending on @p date (paid only
   * when @p charge and he played too little). */
  void settleLoanClause(const LoanDeal& loan, const GameDateValue& date,
                        TeamID managed_team_id, bool charge);

  std::shared_ptr<GameData> gamedata;
  WorldSimulation& world;
  const CompetitionManager& competitions;

  std::vector<TransferRecord> records;
  std::size_t persisted_records = 0;
  std::vector<TransferObligation> pending_obligations;
  std::unordered_map<PlayerID, LoanDeal> active_loans;
  std::unordered_map<PlayerID, PreContractDeal> pre_contracts;
  std::unordered_map<PlayerID, PlayerMarketFlags> player_flags;
  std::vector<IncomingOffer> incoming;
  std::unordered_map<PlayerID, Negotiation> negotiations;
  std::vector<TalksCooldown> cooldowns;
  std::uint32_t next_id = 1;

  // Instalments due before season end, per payer (rebuilt when dirty).
  // Guards the lazy cache; the remaining market state is game-thread owned.
  mutable std::mutex payables_cache_mutex;
  mutable std::unordered_map<TeamID, std::int64_t> payables_cache;
  mutable std::int32_t payables_day = -1;
  mutable bool payables_dirty = true;
};
