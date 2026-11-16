// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "database/repositories/transfer_repository.h"

#include <sqlite3.h>

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

#include "model/world_rng.h"
#include "model/world_simulation.h"

namespace
{
using namespace TransferNegotiation;

enum class OfferRow : int
{
  IncomingTransfer = 0,
  IncomingLoan = 1,
  Negotiation = 2,
  ClosedTalks = 3 /*!< A club's ended talks: expires = end of the cooldown. */
};

std::string columnText(sqlite3_stmt* stmt, int column)
{
  const unsigned char* text = sqlite3_column_text(stmt, column);
  return text ? reinterpret_cast<const char*>(text) : "";
}

void bindOptionalRole(sqlite3_stmt* stmt, int index,
                      const std::optional<SquadRole>& role)
{
  if (role)
    sqlite3_bind_int(stmt, index, static_cast<int>(std::to_underlying(*role)));
  else
    sqlite3_bind_null(stmt, index);
}

std::optional<SquadRole> columnOptionalRole(sqlite3_stmt* stmt, int column)
{
  if (sqlite3_column_type(stmt, column) == SQLITE_NULL) return std::nullopt;
  return static_cast<SquadRole>(sqlite3_column_int(stmt, column));
}

void execute(const DatabaseConnection& db, const char* sql)
{
  sqlite3_stmt* stmt = db.prepareStatement(sql);
  db.executeStep(stmt);
  sqlite3_finalize(stmt);
}

void finishRow(const DatabaseConnection& db, sqlite3_stmt* stmt)
{
  db.executeStep(stmt);
  sqlite3_reset(stmt);
  sqlite3_clear_bindings(stmt);
}

nlohmann::json toJson(const OfferTerms& terms)
{
  return {{"fee", terms.fee},
          {"upfront", terms.upfront_percent},
          {"years", terms.instalment_years},
          {"app_bonus", terms.appearance_bonus},
          {"app_target", terms.appearance_target},
          {"goal_bonus", terms.goal_bonus},
          {"goal_target", terms.goal_target},
          {"sell_on", terms.sell_on_percent}};
}

OfferTerms offerFromJson(const nlohmann::json& json)
{
  OfferTerms terms;
  terms.fee = json.value("fee", 0U);
  terms.upfront_percent = json.value("upfront", std::uint8_t{100});
  terms.instalment_years = json.value("years", std::uint8_t{0});
  terms.appearance_bonus = json.value("app_bonus", 0U);
  terms.appearance_target = json.value("app_target", std::uint16_t{0});
  terms.goal_bonus = json.value("goal_bonus", 0U);
  terms.goal_target = json.value("goal_target", std::uint16_t{0});
  terms.sell_on_percent = json.value("sell_on", std::uint8_t{0});
  return terms;
}

nlohmann::json toJson(const LoanTerms& terms)
{
  return {{"duration", static_cast<int>(std::to_underlying(terms.duration))},
          {"wage_share", terms.wage_share},
          {"loan_fee", terms.loan_fee},
          {"option_fee", terms.option_fee},
          {"obligation", terms.obligation},
          {"recall", terms.recall_clause},
          {"min_apps", terms.min_appearances},
          {"unplayed_fee", terms.unplayed_fee}};
}

LoanTerms loanFromJson(const nlohmann::json& json)
{
  LoanTerms terms;
  terms.duration = static_cast<LoanDuration>(json.value("duration", 0));
  terms.wage_share = json.value("wage_share", std::uint8_t{100});
  terms.loan_fee = json.value("loan_fee", 0U);
  terms.option_fee = json.value("option_fee", 0U);
  terms.obligation = json.value("obligation", false);
  terms.recall_clause = json.value("recall", false);
  terms.min_appearances = json.value("min_apps", std::uint8_t{0});
  terms.unplayed_fee = json.value("unplayed_fee", 0U);
  return terms;
}
}  // namespace

TransferRepository::TransferRepository(std::shared_ptr<DatabaseConnection> conn)
    : db_conn(std::move(conn))
{
}

// ---------------- History ----------------

std::vector<TransferRecord> TransferRepository::loadHistory() const
{
  std::vector<TransferRecord> records;
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "SELECT player_id, game_date, from_team_id, to_team_id, fee, kind FROM "
      "TransferHistory ORDER BY seq;");
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    TransferRecord record;
    record.player_id = static_cast<PlayerID>(sqlite3_column_int64(stmt, 0));
    record.date = dateFromInt(sqlite3_column_int(stmt, 1));
    record.from_team = static_cast<TeamID>(sqlite3_column_int(stmt, 2));
    record.to_team = static_cast<TeamID>(sqlite3_column_int(stmt, 3));
    record.fee = static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 4));
    record.kind = static_cast<TransferKind>(sqlite3_column_int(stmt, 5));
    records.push_back(record);
  }
  sqlite3_finalize(stmt);
  return records;
}

void TransferRepository::appendHistory(
    std::size_t first_seq, std::span<const TransferRecord> records) const
{
  if (records.empty()) return;
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "INSERT OR IGNORE INTO TransferHistory (seq, player_id, game_date, "
      "from_team_id, to_team_id, fee, kind) VALUES (?, ?, ?, ?, ?, ?, ?);");
  std::size_t seq = first_seq;
  for (const TransferRecord& record : records)
  {
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(seq++));
    sqlite3_bind_int64(stmt, 2, record.player_id);
    sqlite3_bind_int(stmt, 3, dateToInt(record.date));
    sqlite3_bind_int(stmt, 4, record.from_team);
    sqlite3_bind_int(stmt, 5, record.to_team);
    sqlite3_bind_int64(stmt, 6, record.fee);
    sqlite3_bind_int(stmt, 7,
                     static_cast<int>(std::to_underlying(record.kind)));
    finishRow(*db_conn, stmt);
  }
  sqlite3_finalize(stmt);
}

// ---------------- Obligations ----------------

std::vector<TransferObligation> TransferRepository::loadObligations() const
{
  std::vector<TransferObligation> obligations;
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "SELECT id, kind, player_id, payer_id, payee_id, amount, due_date, "
      "target, baseline FROM TransferObligations ORDER BY id;");
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    TransferObligation obligation;
    obligation.id = static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 0));
    obligation.kind = static_cast<ObligationKind>(sqlite3_column_int(stmt, 1));
    obligation.player_id = static_cast<PlayerID>(sqlite3_column_int64(stmt, 2));
    obligation.payer = static_cast<TeamID>(sqlite3_column_int(stmt, 3));
    obligation.payee = static_cast<TeamID>(sqlite3_column_int(stmt, 4));
    obligation.amount = sqlite3_column_int64(stmt, 5);
    obligation.due = dateFromInt(sqlite3_column_int(stmt, 6));
    obligation.target = static_cast<std::uint16_t>(sqlite3_column_int(stmt, 7));
    obligation.baseline =
        static_cast<std::uint16_t>(sqlite3_column_int(stmt, 8));
    obligations.push_back(obligation);
  }
  sqlite3_finalize(stmt);
  return obligations;
}

void TransferRepository::replaceObligations(
    const std::vector<TransferObligation>& obligations) const
{
  execute(*db_conn, "DELETE FROM TransferObligations;");
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "INSERT INTO TransferObligations (id, kind, player_id, payer_id, "
      "payee_id, amount, due_date, target, baseline) VALUES "
      "(?, ?, ?, ?, ?, ?, ?, ?, ?);");
  for (const TransferObligation& obligation : obligations)
  {
    sqlite3_bind_int64(stmt, 1, obligation.id);
    sqlite3_bind_int(stmt, 2,
                     static_cast<int>(std::to_underlying(obligation.kind)));
    sqlite3_bind_int64(stmt, 3, obligation.player_id);
    sqlite3_bind_int(stmt, 4, obligation.payer);
    sqlite3_bind_int(stmt, 5, obligation.payee);
    sqlite3_bind_int64(stmt, 6, obligation.amount);
    sqlite3_bind_int(stmt, 7, dateToInt(obligation.due));
    sqlite3_bind_int(stmt, 8, obligation.target);
    sqlite3_bind_int(stmt, 9, obligation.baseline);
    finishRow(*db_conn, stmt);
  }
  sqlite3_finalize(stmt);
}

// ---------------- Loans ----------------

std::vector<LoanDeal> TransferRepository::loadLoans() const
{
  std::vector<LoanDeal> loans;
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "SELECT player_id, parent_id, borrower_id, start_date, end_date, "
      "wage_share, full_wage, option_fee, obligation, recall_clause FROM "
      "Loans ORDER BY player_id;");
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    LoanDeal loan;
    loan.player_id = static_cast<PlayerID>(sqlite3_column_int64(stmt, 0));
    loan.parent = static_cast<TeamID>(sqlite3_column_int(stmt, 1));
    loan.borrower = static_cast<TeamID>(sqlite3_column_int(stmt, 2));
    loan.start = dateFromInt(sqlite3_column_int(stmt, 3));
    loan.end = dateFromInt(sqlite3_column_int(stmt, 4));
    loan.wage_share = static_cast<std::uint8_t>(sqlite3_column_int(stmt, 5));
    loan.full_wage = static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 6));
    loan.option_fee = static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 7));
    loan.obligation = sqlite3_column_int(stmt, 8) != 0;
    loan.recall_clause = sqlite3_column_int(stmt, 9) != 0;
    loans.push_back(loan);
  }
  sqlite3_finalize(stmt);
  return loans;
}

void TransferRepository::replaceLoans(
    const std::unordered_map<PlayerID, LoanDeal>& loans) const
{
  execute(*db_conn, "DELETE FROM Loans;");
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "INSERT INTO Loans (player_id, parent_id, borrower_id, start_date, "
      "end_date, wage_share, full_wage, option_fee, obligation, "
      "recall_clause) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
  for (const auto& [player_id, loan] : loans)
  {
    sqlite3_bind_int64(stmt, 1, player_id);
    sqlite3_bind_int(stmt, 2, loan.parent);
    sqlite3_bind_int(stmt, 3, loan.borrower);
    sqlite3_bind_int(stmt, 4, dateToInt(loan.start));
    sqlite3_bind_int(stmt, 5, dateToInt(loan.end));
    sqlite3_bind_int(stmt, 6, loan.wage_share);
    sqlite3_bind_int64(stmt, 7, loan.full_wage);
    sqlite3_bind_int64(stmt, 8, loan.option_fee);
    sqlite3_bind_int(stmt, 9, loan.obligation ? 1 : 0);
    sqlite3_bind_int(stmt, 10, loan.recall_clause ? 1 : 0);
    finishRow(*db_conn, stmt);
  }
  sqlite3_finalize(stmt);
}

// ---------------- Pre-contracts ----------------

std::vector<PreContractDeal> TransferRepository::loadPreContracts() const
{
  std::vector<PreContractDeal> deals;
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "SELECT player_id, from_team_id, to_team_id, agreed_date, weekly_wage, "
      "years, signing_bonus, release_clause, promised_role FROM PreContracts "
      "ORDER BY player_id;");
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    PreContractDeal deal;
    deal.player_id = static_cast<PlayerID>(sqlite3_column_int64(stmt, 0));
    deal.from_team = static_cast<TeamID>(sqlite3_column_int(stmt, 1));
    deal.to_team = static_cast<TeamID>(sqlite3_column_int(stmt, 2));
    deal.agreed = dateFromInt(sqlite3_column_int(stmt, 3));
    deal.terms.weekly_wage =
        static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 4));
    deal.terms.years = static_cast<std::uint8_t>(sqlite3_column_int(stmt, 5));
    deal.terms.signing_bonus =
        static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 6));
    deal.terms.release_clause =
        static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 7));
    deal.terms.promised_role = columnOptionalRole(stmt, 8);
    deals.push_back(deal);
  }
  sqlite3_finalize(stmt);
  return deals;
}

void TransferRepository::replacePreContracts(
    const std::unordered_map<PlayerID, PreContractDeal>& deals) const
{
  execute(*db_conn, "DELETE FROM PreContracts;");
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "INSERT INTO PreContracts (player_id, from_team_id, to_team_id, "
      "agreed_date, weekly_wage, years, signing_bonus, release_clause, "
      "promised_role) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);");
  for (const auto& [player_id, deal] : deals)
  {
    sqlite3_bind_int64(stmt, 1, player_id);
    sqlite3_bind_int(stmt, 2, deal.from_team);
    sqlite3_bind_int(stmt, 3, deal.to_team);
    sqlite3_bind_int(stmt, 4, dateToInt(deal.agreed));
    sqlite3_bind_int64(stmt, 5, deal.terms.weekly_wage);
    sqlite3_bind_int(stmt, 6, deal.terms.years);
    sqlite3_bind_int64(stmt, 7, deal.terms.signing_bonus);
    sqlite3_bind_int64(stmt, 8, deal.terms.release_clause);
    bindOptionalRole(stmt, 9, deal.terms.promised_role);
    finishRow(*db_conn, stmt);
  }
  sqlite3_finalize(stmt);
}

// ---------------- Player flags ----------------

std::unordered_map<PlayerID, PlayerMarketFlags> TransferRepository::loadFlags()
    const
{
  std::unordered_map<PlayerID, PlayerMarketFlags> flags;
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "SELECT player_id, release_clause, promised_role, promise_date, "
      "loan_listed, not_for_sale_until FROM PlayerMarketFlags;");
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    PlayerMarketFlags entry;
    entry.release_clause =
        static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 1));
    entry.promised_role = columnOptionalRole(stmt, 2);
    if (const int date = sqlite3_column_int(stmt, 3); date > 0)
      entry.promise_date = dateFromInt(date);
    entry.loan_listed = sqlite3_column_int(stmt, 4) != 0;
    entry.not_for_sale_until = sqlite3_column_int(stmt, 5);
    flags[static_cast<PlayerID>(sqlite3_column_int64(stmt, 0))] = entry;
  }
  sqlite3_finalize(stmt);
  return flags;
}

void TransferRepository::replaceFlags(
    const std::unordered_map<PlayerID, PlayerMarketFlags>& flags) const
{
  execute(*db_conn, "DELETE FROM PlayerMarketFlags;");
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "INSERT INTO PlayerMarketFlags (player_id, release_clause, "
      "promised_role, promise_date, loan_listed, not_for_sale_until) VALUES "
      "(?, ?, ?, ?, ?, ?);");
  for (const auto& [player_id, entry] : flags)
  {
    if (entry.empty()) continue;
    sqlite3_bind_int64(stmt, 1, player_id);
    sqlite3_bind_int64(stmt, 2, entry.release_clause);
    bindOptionalRole(stmt, 3, entry.promised_role);
    sqlite3_bind_int(stmt, 4,
                     entry.promised_role ? dateToInt(entry.promise_date) : 0);
    sqlite3_bind_int(stmt, 5, entry.loan_listed ? 1 : 0);
    sqlite3_bind_int(stmt, 6, entry.not_for_sale_until);
    finishRow(*db_conn, stmt);
  }
  sqlite3_finalize(stmt);
}

// ---------------- Offers and talks ----------------

void TransferRepository::loadOffers(
    std::vector<IncomingOffer>& offers,
    std::unordered_map<PlayerID, Negotiation>& talks,
    std::vector<TalksCooldown>& cooldowns) const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "SELECT id, kind, player_id, club_id, created, expires, rounds, terms, "
      "status, respond_on FROM TransferOffers ORDER BY id;");
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    const auto row = static_cast<OfferRow>(sqlite3_column_int(stmt, 1));
    const auto json =
        nlohmann::json::parse(columnText(stmt, 7), nullptr, false);
    if (!json.is_object()) continue;
    const auto player_id = static_cast<PlayerID>(sqlite3_column_int64(stmt, 2));
    const auto club_id = static_cast<TeamID>(sqlite3_column_int(stmt, 3));
    const GameDateValue expires = dateFromInt(sqlite3_column_int(stmt, 5));
    if (row == OfferRow::ClosedTalks)
    {
      cooldowns.push_back({player_id, club_id, expires});
      continue;
    }
    if (row == OfferRow::Negotiation)
    {
      Negotiation talk;
      talk.player_id = player_id;
      talk.seller = club_id;
      talk.expires = expires;
      talk.club_rounds = static_cast<std::uint8_t>(sqlite3_column_int(stmt, 6));
      talk.kind = static_cast<ContractKind>(json.value("contract_kind", 0));
      talk.agreed =
          offerFromJson(json.value("offer", nlohmann::json::object()));
      talk.club_agreed = json.value("club_agreed", false);
      talk.player_rounds = json.value("player_rounds", std::uint8_t{0});
      talks[player_id] = talk;
      continue;
    }
    IncomingOffer offer;
    offer.id = static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 0));
    offer.player_id = player_id;
    offer.buyer = club_id;
    offer.loan = row == OfferRow::IncomingLoan;
    offer.created = dateFromInt(sqlite3_column_int(stmt, 4));
    offer.expires = expires;
    offer.round = static_cast<std::uint8_t>(sqlite3_column_int(stmt, 6));
    offer.terms = offerFromJson(json.value("offer", nlohmann::json::object()));
    offer.loan_terms =
        loanFromJson(json.value("loan", nlohmann::json::object()));
    offer.max_fee = json.value("max_fee", 0U);
    offer.patience = json.value("patience", std::uint8_t{0});
    offer.insults = json.value("insults", std::uint8_t{0});
    offer.firm = json.value("firm", false);
    offer.asked = offerFromJson(json.value("asked", nlohmann::json::object()));
    offer.asked_loan =
        loanFromJson(json.value("asked_loan", nlohmann::json::object()));
    offer.status = static_cast<OfferStatus>(sqlite3_column_int(stmt, 8));
    if (const int respond_on = sqlite3_column_int(stmt, 9); respond_on > 0)
      offer.respond_on = dateFromInt(respond_on);
    offers.push_back(offer);
  }
  sqlite3_finalize(stmt);

  // Rounds of the talks, oldest first per offer.
  stmt = db_conn->prepareStatement(
      "SELECT offer_id, game_date, move, terms FROM TransferOfferRounds "
      "ORDER BY offer_id, seq;");
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    const auto offer_id =
        static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 0));
    const auto offer = std::ranges::find(offers, offer_id, &IncomingOffer::id);
    const auto move = sqlite3_column_int(stmt, 2);
    if (offer == offers.end() || move < 0 ||
        move >=
            static_cast<int>(std::to_underlying(BuyerNegotiation::Move::COUNT)))
      continue;
    const auto json =
        nlohmann::json::parse(columnText(stmt, 3), nullptr, false);
    OfferRound round;
    round.date = dateFromInt(sqlite3_column_int(stmt, 1));
    round.move = static_cast<BuyerNegotiation::Move>(move);
    round.terms =
        offerFromJson(json.is_object() ? json : nlohmann::json::object());
    if (json.is_object() && json.contains("loan"))
      round.loan_terms = loanFromJson(json["loan"]);
    offer->history.push_back(round);
  }
  sqlite3_finalize(stmt);
}

void TransferRepository::replaceOffers(
    const std::vector<IncomingOffer>& offers,
    const std::unordered_map<PlayerID, Negotiation>& talks,
    const std::vector<TalksCooldown>& cooldowns) const
{
  execute(*db_conn, "DELETE FROM TransferOffers;");
  execute(*db_conn, "DELETE FROM TransferOfferRounds;");
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "INSERT INTO TransferOffers (id, kind, player_id, club_id, created, "
      "expires, rounds, terms, status, respond_on) VALUES (?, ?, ?, ?, ?, ?, "
      "?, ?, ?, ?);");
  for (const IncomingOffer& offer : offers)
  {
    const std::string terms =
        nlohmann::json{{"offer", toJson(offer.terms)},
                       {"loan", toJson(offer.loan_terms)},
                       {"max_fee", offer.max_fee},
                       {"patience", offer.patience},
                       {"insults", offer.insults},
                       {"firm", offer.firm},
                       {"asked", toJson(offer.asked)},
                       {"asked_loan", toJson(offer.asked_loan)}}
            .dump();
    sqlite3_bind_int64(stmt, 1, offer.id);
    sqlite3_bind_int(
        stmt, 2,
        std::to_underlying(offer.loan ? OfferRow::IncomingLoan
                                      : OfferRow::IncomingTransfer));
    sqlite3_bind_int64(stmt, 3, offer.player_id);
    sqlite3_bind_int(stmt, 4, offer.buyer);
    sqlite3_bind_int(stmt, 5, dateToInt(offer.created));
    sqlite3_bind_int(stmt, 6, dateToInt(offer.expires));
    sqlite3_bind_int(stmt, 7, offer.round);
    sqlite3_bind_text(stmt, 8, terms.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 9,
                     static_cast<int>(std::to_underlying(offer.status)));
    sqlite3_bind_int(stmt, 10,
                     offer.status == OfferStatus::AwaitingBuyer
                         ? dateToInt(offer.respond_on)
                         : 0);
    finishRow(*db_conn, stmt);
  }
  for (const auto& [player_id, talk] : talks)
  {
    const std::string terms = nlohmann::json{
        {"offer", toJson(talk.agreed)},
        {"contract_kind", static_cast<int>(std::to_underlying(talk.kind))},
        {"club_agreed", talk.club_agreed},
        {"player_rounds",
         talk.player_rounds}}.dump();
    sqlite3_bind_int64(stmt, 1, 0);
    sqlite3_bind_int(stmt, 2, std::to_underlying(OfferRow::Negotiation));
    sqlite3_bind_int64(stmt, 3, player_id);
    sqlite3_bind_int(stmt, 4, talk.seller);
    sqlite3_bind_int(stmt, 5, dateToInt(talk.expires));
    sqlite3_bind_int(stmt, 6, dateToInt(talk.expires));
    sqlite3_bind_int(stmt, 7, talk.club_rounds);
    sqlite3_bind_text(stmt, 8, terms.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 9, 0);
    sqlite3_bind_int(stmt, 10, 0);
    finishRow(*db_conn, stmt);
  }
  for (const TalksCooldown& cooldown : cooldowns)
  {
    sqlite3_bind_int64(stmt, 1, 0);
    sqlite3_bind_int(stmt, 2, std::to_underlying(OfferRow::ClosedTalks));
    sqlite3_bind_int64(stmt, 3, cooldown.player_id);
    sqlite3_bind_int(stmt, 4, cooldown.buyer);
    sqlite3_bind_int(stmt, 5, dateToInt(cooldown.until));
    sqlite3_bind_int(stmt, 6, dateToInt(cooldown.until));
    sqlite3_bind_int(stmt, 7, 0);
    sqlite3_bind_text(stmt, 8, "{}", -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 9, 0);
    sqlite3_bind_int(stmt, 10, 0);
    finishRow(*db_conn, stmt);
  }
  sqlite3_finalize(stmt);

  stmt = db_conn->prepareStatement(
      "INSERT INTO TransferOfferRounds (offer_id, seq, game_date, move, "
      "terms) VALUES (?, ?, ?, ?, ?);");
  for (const IncomingOffer& offer : offers)
  {
    for (std::size_t seq = 0; seq < offer.history.size(); ++seq)
    {
      const OfferRound& round = offer.history[seq];
      nlohmann::json json = toJson(round.terms);
      if (offer.loan) json["loan"] = toJson(round.loan_terms);
      const std::string terms = json.dump();
      sqlite3_bind_int64(stmt, 1, offer.id);
      sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(seq));
      sqlite3_bind_int(stmt, 3, dateToInt(round.date));
      sqlite3_bind_int(stmt, 4,
                       static_cast<int>(std::to_underlying(round.move)));
      sqlite3_bind_text(stmt, 5, terms.c_str(), -1, SQLITE_TRANSIENT);
      finishRow(*db_conn, stmt);
    }
  }
  sqlite3_finalize(stmt);
}
