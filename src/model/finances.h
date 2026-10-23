// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "model/gamedate.h"

class Team;
class GameData;

/**
 * @enum FinanceCategory
 * @brief Ledger categories (values are persisted).
 */
enum class FinanceCategory : std::uint8_t
{
  OpeningBalance = 0,
  Investment,    /*!< Owner funds injected into the club. */
  Wages,         /*!< Weekly player payroll. */
  TransferFeeIn, /*!< Fees received for sold players. */
  TransferFeeOut,
  Matchday,     /*!< Gate receipts of home matches. */
  Broadcasting, /*!< Monthly equal share of TV money. */
  Sponsorship,  /*!< Monthly commercial income. */
  PrizeMoney,   /*!< Continental and international prize money. */
  Facilities,   /*!< Stadium, training ground and operations. */
  Staff,        /*!< Non-player wages. */
  Adjustment,   /*!< Reconciliation of legacy saves. */
  COUNT
};

/** Number of ledger categories. */
inline constexpr std::size_t FINANCE_CATEGORY_COUNT =
    static_cast<std::size_t>(std::to_underlying(FinanceCategory::COUNT));

/** Language key naming @p category (e.g. "FIN_CAT_WAGES"). */
const char* financeCategoryKey(FinanceCategory category);

/**
 * @struct FinanceTransaction
 * @brief One dated ledger entry; positive amounts are income.
 */
struct FinanceTransaction
{
  GameDateValue date;
  FinanceCategory category = FinanceCategory::Adjustment;
  std::int64_t amount = 0;
};

/**
 * @struct FinanceSummary
 * @brief Per-category totals of a ledger range.
 */
struct FinanceSummary
{
  std::array<std::int64_t, FINANCE_CATEGORY_COUNT> by_category{};
  std::int64_t income = 0;
  std::int64_t expenses = 0; /*!< Positive number. */
  std::int64_t net() const { return income - expenses; }
};

/**
 * @class Finances
 * @brief A club's balance, budgets and dated transaction ledger.
 *
 * Every balance change is recorded as a transaction, so the balance always
 * equals the sum of the ledger. The transfer budget and the weekly wage
 * budget are separate allowances set by the board each season.
 */
class Finances
{
 public:
  /**
   * @brief Creates the finances with an opening-balance transaction.
   * @param opening_balance Initial cash.
   * @param opening_date Date of the opening transaction.
   */
  explicit Finances(std::int64_t opening_balance,
                    GameDateValue opening_date = GameDateValue(2025, 7, 2));

  /**
   * @brief Records a dated transaction and updates the balance.
   *
   * Fees paid reduce the transfer budget; a share of fees received is
   * returned to it.
   */
  void record(const GameDateValue& date, FinanceCategory category,
              std::int64_t amount);

  /** Removes the newest transaction and undoes its effects. */
  bool revertLastTransaction();

  /**
   * @brief Owner investment dated at the newest transaction.
   *
   * The funds are unrestricted: they raise the transfer budget by the full
   * amount and the weekly wage budget by one season's worth (amount / 52).
   */
  void addBalance(std::int64_t amount);

  /** @brief Current cash. */
  std::int64_t getBalance() const noexcept;

  /** @brief Remaining transfer allowance for this season. */
  std::int64_t getTransferBudget() const noexcept;

  /** @brief Sets the transfer allowance. */
  void setTransferBudget(std::int64_t budget);

  /** @brief Weekly wage allowance (cap on the total weekly payroll). */
  std::int64_t getWageBudget() const noexcept;

  /** @brief Sets the weekly wage allowance. */
  void setWageBudget(std::int64_t weekly_budget);

  /**
   * @brief Moves money between the two budgets at 52 weeks per season.
   * @param transfer_amount Positive moves transfer budget into weekly wages,
   * negative moves unused weekly wage room back into the transfer budget.
   * @param current_weekly_wages Payroll that the wage budget must cover.
   * @return False when a budget would drop below its floor.
   */
  bool moveTransferToWageBudget(std::int64_t transfer_amount,
                                std::int64_t current_weekly_wages);

  /** @brief All transactions in recording order (chronological). */
  const std::vector<FinanceTransaction>& getLedger() const noexcept;

  /** @brief Sum of all ledger amounts (equals the balance). */
  std::int64_t ledgerTotal() const;

  /** @brief Balance from the transactions dated on or before @p date. */
  std::int64_t balanceAt(const GameDateValue& date) const;

  /** @brief Totals of the transactions dated in [from, to]. */
  FinanceSummary summarize(const GameDateValue& from,
                           const GameDateValue& to) const;

  /**
   * @brief Gets the current total wage spending of the team.
   * @return The current weekly wage spending.
   */
  int64_t getCurrentWageSpending(const GameData& gamedata,
                                 const Team& team) const;

  // Persistence helpers

  /** Replaces the ledger with persisted rows; the balance follows it. */
  void restoreLedger(std::vector<FinanceTransaction> ledger);

  /** Transactions not yet written to the database. */
  std::span<const FinanceTransaction> pendingTransactions() const;

  /** Index of the first pending transaction. */
  std::size_t persistedCount() const noexcept;

  /** Marks every transaction as written. */
  void markPersisted() noexcept;

 private:
  std::int64_t balance = 0;
  std::int64_t transfer_budget = 0;
  std::int64_t wage_budget = 0;
  std::vector<FinanceTransaction> ledger;
  std::size_t persisted = 0;
};
