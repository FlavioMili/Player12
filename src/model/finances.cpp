// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "finances.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

#include "gamedata.h"
#include "team.h"
#include "world_tuning.h"

namespace
{
constexpr std::int64_t WEEKS_PER_SEASON = 52;

constexpr std::array<const char*, FINANCE_CATEGORY_COUNT> CATEGORY_KEYS = {
    "FIN_CAT_OPENING_BALANCE", "FIN_CAT_INVESTMENT",   "FIN_CAT_WAGES",
    "FIN_CAT_TRANSFER_IN",     "FIN_CAT_TRANSFER_OUT", "FIN_CAT_MATCHDAY",
    "FIN_CAT_BROADCASTING",    "FIN_CAT_SPONSORSHIP",  "FIN_CAT_PRIZE_MONEY",
    "FIN_CAT_FACILITIES",      "FIN_CAT_STAFF",        "FIN_CAT_ADJUSTMENT"};

std::int64_t reinvestedShare(std::int64_t fee)
{
  return static_cast<std::int64_t>(std::llround(
      static_cast<double>(fee) *
      static_cast<double>(WorldTuning::Finance::TRANSFER_INCOME_REINVESTMENT)));
}
}  // namespace

const char* financeCategoryKey(FinanceCategory category)
{
  const auto index = static_cast<std::size_t>(std::to_underlying(category));
  return index < CATEGORY_KEYS.size() ? CATEGORY_KEYS[index]
                                      : "FIN_CAT_ADJUSTMENT";
}

Finances::Finances(std::int64_t opening_balance, GameDateValue opening_date)
{
  record(opening_date, FinanceCategory::OpeningBalance, opening_balance);
}

void Finances::record(const GameDateValue& date, FinanceCategory category,
                      std::int64_t amount)
{
  using enum FinanceCategory;
  ledger.push_back({date, category, amount});
  balance += amount;
  if (category == TransferFeeOut)
    transfer_budget = std::max<std::int64_t>(0, transfer_budget + amount);
  else if (category == TransferFeeIn)
    transfer_budget += reinvestedShare(amount);
}

bool Finances::revertLastTransaction()
{
  if (ledger.size() <= persisted || ledger.empty()) return false;
  const FinanceTransaction last = ledger.back();
  ledger.pop_back();
  balance -= last.amount;
  if (last.category == FinanceCategory::TransferFeeOut)
    transfer_budget -= last.amount;
  else if (last.category == FinanceCategory::TransferFeeIn)
    transfer_budget = std::max<std::int64_t>(
        0, transfer_budget - reinvestedShare(last.amount));
  else if (last.category == FinanceCategory::Investment && last.amount > 0)
  {
    transfer_budget = std::max<std::int64_t>(0, transfer_budget - last.amount);
    wage_budget =
        std::max<std::int64_t>(0, wage_budget - last.amount / WEEKS_PER_SEASON);
  }
  return true;
}

void Finances::addBalance(std::int64_t amount)
{
  const GameDateValue date =
      ledger.empty() ? GameDateValue(2025, 7, 2) : ledger.back().date;
  record(date, FinanceCategory::Investment, amount);
  if (amount > 0)
  {
    transfer_budget += amount;
    wage_budget += amount / WEEKS_PER_SEASON;
  }
}

std::int64_t Finances::getBalance() const noexcept { return balance; }

std::int64_t Finances::getTransferBudget() const noexcept
{
  return transfer_budget;
}

void Finances::setTransferBudget(std::int64_t budget)
{
  transfer_budget = std::max<std::int64_t>(0, budget);
}

std::int64_t Finances::getWageBudget() const noexcept { return wage_budget; }

void Finances::setWageBudget(std::int64_t weekly_budget)
{
  wage_budget = std::max<std::int64_t>(0, weekly_budget);
}

bool Finances::moveTransferToWageBudget(std::int64_t transfer_amount,
                                        std::int64_t current_weekly_wages)
{
  const std::int64_t weekly_delta = transfer_amount / WEEKS_PER_SEASON;
  const std::int64_t new_transfer = transfer_budget - transfer_amount;
  const std::int64_t new_wage = wage_budget + weekly_delta;
  if (weekly_delta == 0 || new_transfer < 0 || new_wage < current_weekly_wages)
    return false;
  transfer_budget = new_transfer;
  wage_budget = new_wage;
  return true;
}

const std::vector<FinanceTransaction>& Finances::getLedger() const noexcept
{
  return ledger;
}

std::int64_t Finances::ledgerTotal() const
{
  std::int64_t total = 0;
  for (const FinanceTransaction& transaction : ledger)
    total += transaction.amount;
  return total;
}

std::int64_t Finances::balanceAt(const GameDateValue& date) const
{
  // Linear scan: the ledger is not strictly date-ordered (see summarize()).
  std::int64_t total = 0;
  for (const FinanceTransaction& transaction : ledger)
  {
    if (!(date < transaction.date)) total += transaction.amount;
  }
  return total;
}

FinanceSummary Finances::summarize(const GameDateValue& from,
                                   const GameDateValue& to) const
{
  // Linear scan: a result recorded a day late (a fixture simulated the next
  // morning) may sit after later entries, so the ledger is not strictly
  // date-ordered.
  FinanceSummary summary;
  for (const FinanceTransaction& transaction : ledger)
  {
    if (transaction.date < from || to < transaction.date) continue;
    summary.by_category[static_cast<std::size_t>(
        std::to_underlying(transaction.category))] += transaction.amount;
    if (transaction.amount >= 0)
      summary.income += transaction.amount;
    else
      summary.expenses -= transaction.amount;
  }
  return summary;
}

int64_t Finances::getCurrentWageSpending(const GameData& gamedata,
                                         const Team& team) const
{
  int64_t wages{};
  // Academy scholarships and contracts are part of the payroll too.
  for (const auto* ids : {&team.getPlayerIDs(), &team.getAcademyIDs()})
  {
    for (const auto player_id : *ids)
    {
      if (const auto player = gamedata.getPlayer(player_id))
      {
        wages += player->get().getWage();
      }
    }
  }
  return wages;
}

void Finances::restoreLedger(std::vector<FinanceTransaction> restored)
{
  ledger = std::move(restored);
  persisted = ledger.size();
  balance = ledgerTotal();
}

std::span<const FinanceTransaction> Finances::pendingTransactions() const
{
  return std::span<const FinanceTransaction>(ledger).subspan(persisted);
}

std::size_t Finances::persistedCount() const noexcept { return persisted; }

void Finances::markPersisted() noexcept { persisted = ledger.size(); }
