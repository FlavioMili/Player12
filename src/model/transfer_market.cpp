// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/transfer_market.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <span>
#include <utility>

#include "database/gamedata.h"
#include "database/repositories/transfer_repository.h"
#include "global/global.h"
#include "model/club_economy.h"
#include "model/competition_manager.h"
#include "model/finances.h"
#include "model/player.h"
#include "model/team.h"
#include "model/transfer_tuning.h"
#include "model/transfer_windows.h"
#include "model/world_rng.h"
#include "model/world_simulation.h"

namespace
{
using namespace TransferNegotiation;

constexpr std::array<const char*, 6> KIND_KEYS = {
    "TRANSFER_KIND_PERMANENT",    "TRANSFER_KIND_FREE",
    "TRANSFER_KIND_LOAN",         "TRANSFER_KIND_LOAN_RETURN",
    "TRANSFER_KIND_PRE_CONTRACT", "TRANSFER_KIND_RELEASE"};

// Recipient id that matches no club: suppresses the world's generic
// signing/sale news when this class posts a more specific message.
constexpr TeamID NO_RECIPIENT = std::numeric_limits<TeamID>::max();
constexpr int DAYS_PER_YEAR = 365;
constexpr int FIRST_TEAM_SIZE = 16;
constexpr std::size_t AI_FREE_AGENT_CHECKS = 5;

/** League of @p team_id: its country's transfer windows apply. */
LeagueID leagueOf(const GameData& gamedata, TeamID team_id)
{
  const auto team = gamedata.getTeam(team_id);
  return team ? team->get().getLeagueId() : LeagueID{0};
}
constexpr std::size_t PRE_CONTRACT_SAMPLE = 24;
constexpr float PRE_CONTRACT_LEVEL_MARGIN = 3.0f;
constexpr int PRE_CONTRACT_MAX_AGE = 31;
constexpr double PRE_CONTRACT_SUCCESS = 0.5;
constexpr std::uint8_t BIGGER_BORROWER_EXTRA_SHARE = 25;
constexpr std::uint64_t SELLER_RESOLVE_KEY = 0x5E11E4;
constexpr std::uint64_t CLAUSE_SEED_KEY = 0xC1A05E;
constexpr std::uint64_t CLAUSE_SIZE_KEY = 0xC1A05F;
/** Release clauses of a new world: share of the eligible players (ambitious,
 * up to CLAUSE_MAX_AGE, at a modest club) and their size in multiples of
 * the market value. */
constexpr double CLAUSE_SEED_SHARE = 0.5;
constexpr int CLAUSE_MAX_AGE = 30;
constexpr double CLAUSE_MIN_MULTIPLE = 1.5;
constexpr double CLAUSE_MULTIPLE_SPAN = 1.5;
constexpr double CLAUSE_ROUNDING = 100'000.0;
/** A borrower offering less than this share of the wage does not bid. */
constexpr std::uint8_t MIN_LOAN_WAGE_SHARE = 20;

int level(SquadRole role) { return std::to_underlying(role); }

std::int64_t weeklyPayroll(const GameData& gamedata, const Team& team)
{
  return team.getFinances().getCurrentWageSpending(gamedata, team);
}

/** First-team players of a club by overall, best first. Academy players
 * belong to the U18 squad and are left to the academy. */
std::vector<std::pair<double, PlayerID>> rankedSquad(const GameData& gamedata,
                                                     const Team& team)
{
  const StatsConfig& config = gamedata.getStatsConfig();
  std::vector<std::pair<double, PlayerID>> ranked;
  ranked.reserve(team.getPlayerIDs().size());
  for (const PlayerID player_id : team.getPlayerIDs())
  {
    if (const auto player = gamedata.getPlayer(player_id);
        player && !player->get().isAcademyPlayer())
      ranked.emplace_back(player->get().getOverall(config), player_id);
  }
  std::ranges::sort(ranked, std::greater<>{});
  return ranked;
}

/** Mean overall of the best FIRST_TEAM_SIZE players. */
float squadLevel(const std::vector<std::pair<double, PlayerID>>& ranked)
{
  if (ranked.empty()) return 0.0f;
  const std::size_t count =
      std::min<std::size_t>(ranked.size(), FIRST_TEAM_SIZE);
  double total = 0.0;
  for (std::size_t i = 0; i < count; ++i) total += ranked[i].first;
  return static_cast<float>(total / static_cast<double>(count));
}

}  // namespace

const char* transferKindKey(TransferKind kind)
{
  const auto index = std::to_underlying(kind);
  return index < KIND_KEYS.size() ? KIND_KEYS[index] : "";
}

TransferMarket::TransferMarket(std::shared_ptr<GameData> data,
                               WorldSimulation& world_simulation,
                               const CompetitionManager& competition_manager)
    : gamedata(std::move(data)),
      world(world_simulation),
      competitions(competition_manager)
{
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

Player* TransferMarket::mutablePlayer(PlayerID player_id)
{
  auto& players = gamedata->getPlayers();
  const auto found = players.find(player_id);
  return found == players.end() ? nullptr : &found->second;
}

std::string TransferMarket::teamName(TeamID team_id) const
{
  const auto team = team_id == FREE_AGENTS_TEAM_ID ? std::nullopt
                                                   : gamedata->getTeam(team_id);
  return team ? team->get().getName() : std::string(FREE_AGENTS_NAME_ARG);
}

void TransferMarket::movePlayer(PlayerID player_id, TeamID from_team,
                                TeamID to_team, TeamID managed_team_id)
{
  Player* player = mutablePlayer(player_id);
  if (!player) return;
  if (auto from = gamedata->getTeam(from_team))
    from->get().removePlayerID(player_id);
  if (auto to = gamedata->getTeam(to_team)) to->get().addPlayerID(player_id);
  gamedata->transferPlayer(player_id, to_team);
  if (player->getTransferStatus() == TransferStatus::Listed)
  {
    player->setTransferStatus(TransferStatus::NotListed);
    gamedata->deleteTransferListing(player_id);
  }
  // Computer-managed clubs pick their best line-up again; the manager's
  // selection is only repaired where the player leaves a gap, and a new
  // signing waits for the manager to pick him.
  const StatsConfig& config = gamedata->getStatsConfig();
  if (auto from = gamedata->getTeam(from_team);
      from && from_team != FREE_AGENTS_TEAM_ID)
  {
    if (from_team == managed_team_id)
      removeFromLineup(from->get(), *player);
    else
      from->get().generateStartingXI(*gamedata, config);
  }
  if (auto to = gamedata->getTeam(to_team);
      to && to_team != FREE_AGENTS_TEAM_ID && to_team != managed_team_id)
    to->get().generateStartingXI(*gamedata, config);
  PlayerDynamics& dynamics = player->mutableDynamics();
  dynamics.transfer_interest_weeks = 0;
  dynamics.playing_share = 0.0f;
}

void TransferMarket::removeFromLineup(Team& team, const Player& departed) const
{
  Lineup& lineup = team.getLineup();
  const PlayerID departed_id = departed.getId();
  for (std::size_t duty = 0; duty < SET_PIECE_DUTY_COUNT; ++duty)
  {
    const auto key = static_cast<SetPieceDuty>(duty);
    if (lineup.getDesignated(key) == departed_id) lineup.setDesignated(key, 0);
  }
  const bool in_goal =
      lineup.getGoalkeeper() && lineup.getGoalkeeper()->getId() == departed_id;
  const bool outfield = std::ranges::any_of(
      lineup.getOutfieldPlayers(),
      [departed_id](const auto& positioned)
      {
        return positioned.player && positioned.player->getId() == departed_id;
      });
  const auto without_departed = [&lineup, departed_id]
  {
    std::vector<const Player*> reserves = lineup.getReserves();
    std::erase_if(reserves, [departed_id](const Player* reserve)
                  { return !reserve || reserve->getId() == departed_id; });
    return reserves;
  };
  if (!in_goal && !outfield)
  {
    lineup.setReserves(without_departed());
    return;
  }

  // A starter left: the best fit from the bench takes his place (the same
  // role first), else the best player outside the match-day squad.
  const StatsConfig& config = gamedata->getStatsConfig();
  const auto better = [&](const Player* candidate, const Player* best)
  {
    if (!best) return true;
    const bool same = candidate->getRole() == departed.getRole();
    if (const bool best_same = best->getRole() == departed.getRole();
        same != best_same)
      return same;
    return candidate->getOverall(config) > best->getOverall(config);
  };
  const Player* replacement = nullptr;
  for (const Player* reserve : lineup.getReserves())
  {
    if (reserve && reserve->getId() != departed_id && reserve->isAvailable() &&
        better(reserve, replacement))
      replacement = reserve;
  }
  if (!replacement)
  {
    for (const PlayerID id : team.getPlayerIDs())
    {
      const auto candidate = gamedata->getPlayer(id);
      if (!candidate || lineup.isStarter(id) ||
          !candidate->get().isAvailable() || candidate->get().isAcademyPlayer())
        continue;
      if (better(&candidate->get(), replacement))
        replacement = &candidate->get();
    }
    if (replacement)
    {
      std::vector<const Player*> reserves = lineup.getReserves();
      reserves.push_back(replacement);
      lineup.setReserves(reserves);
    }
  }
  if (replacement)
    lineup.swapPlayers(replacement->getId(), departed_id);
  else if (in_goal)
    lineup.setGoalkeeper(nullptr);
  else
    lineup.removeOutfieldPlayer(departed_id);
  lineup.setReserves(without_departed());
}

void TransferMarket::pay(TeamID payer, TeamID payee, std::int64_t amount,
                         const GameDateValue& date)
{
  if (amount <= 0) return;
  if (auto from = gamedata->getTeam(payer);
      from && payer != FREE_AGENTS_TEAM_ID)
    from->get().getFinances().record(date, FinanceCategory::TransferFeeOut,
                                     -amount);
  if (auto to = gamedata->getTeam(payee); to && payee != FREE_AGENTS_TEAM_ID)
    to->get().getFinances().record(date, FinanceCategory::TransferFeeIn,
                                   amount);
}

std::int64_t TransferMarket::paySellOns(PlayerID player_id, TeamID seller,
                                        TeamID buyer, std::uint32_t fee,
                                        const GameDateValue& date)
{
  std::int64_t total = 0;
  std::erase_if(
      pending_obligations,
      [this, player_id, seller, buyer, fee, &date,
       &total](const TransferObligation& obligation)
      {
        if (obligation.kind != ObligationKind::SellOn ||
            obligation.player_id != player_id || obligation.payer != seller)
          return false;
        // Buying him back ends the club's own clause unpaid.
        if (obligation.payee == buyer) return true;
        if (const std::int64_t share =
                static_cast<std::int64_t>(fee) * obligation.amount / 100;
            share > 0)
        {
          if (auto payee = gamedata->getTeam(obligation.payee))
            payee->get().getFinances().record(
                date, FinanceCategory::TransferFeeIn, share);
          total += share;
        }
        return true;
      });
  return total;
}

void TransferMarket::addRecord(const TransferRecord& record)
{
  records.push_back(record);
}

void TransferMarket::post(const GameDateValue& date, InboxCategory category,
                          std::string title_key, std::string body_key,
                          std::vector<std::string> args,
                          std::optional<PlayerID> player_id,
                          std::optional<TeamID> team_id)
{
  InboxMessage message;
  message.date = date;
  message.category = category;
  message.title_key = std::move(title_key);
  message.body_key = std::move(body_key);
  message.args = std::move(args);
  message.player_id = player_id;
  message.team_id = team_id;
  world.getInbox().add(std::move(message));
}

std::uint16_t TransferMarket::careerCount(PlayerID player_id, TeamID team_id,
                                          ObligationKind kind) const
{
  int total = 0;
  for (const PlayerSeasonStats& stats : competitions.getPlayerCareer(player_id))
  {
    if (stats.team_id != team_id) continue;
    total +=
        kind == ObligationKind::GoalBonus ? stats.goals : stats.appearances;
  }
  return static_cast<std::uint16_t>(std::min(
      total, static_cast<int>(std::numeric_limits<std::uint16_t>::max())));
}

PlayerMarketFlags& TransferMarket::mutableFlags(PlayerID player_id)
{
  return player_flags[player_id];
}

void TransferMarket::pruneFlags(PlayerID player_id)
{
  const auto found = player_flags.find(player_id);
  if (found != player_flags.end() && found->second.empty())
    player_flags.erase(found);
}

void TransferMarket::clearOnMove(PlayerID player_id)
{
  if (const auto found = player_flags.find(player_id);
      found != player_flags.end())
  {
    found->second.release_clause = 0;
    found->second.promised_role.reset();
    found->second.loan_listed = false;
    found->second.not_for_sale_until = 0;
    pruneFlags(player_id);
  }
  negotiations.erase(player_id);
  std::erase_if(incoming, [player_id](const IncomingOffer& offer)
                { return offer.player_id == player_id; });
  std::erase_if(cooldowns, [player_id](const TalksCooldown& cooldown)
                { return cooldown.player_id == player_id; });
  // A move (e.g. a pre-contract completing) ends any loan he was on, so the
  // loan's end never takes him back, and its appearance clause with it.
  active_loans.erase(player_id);
  std::erase_if(pending_obligations,
                [player_id](const TransferObligation& obligation)
                {
                  return obligation.player_id == player_id &&
                         obligation.kind == ObligationKind::LoanUnplayedFee;
                });
}

// ---------------------------------------------------------------------------
// Decision contexts
// ---------------------------------------------------------------------------

SquadRole TransferMarket::projectedRole(PlayerID player_id,
                                        TeamID club_id) const
{
  const auto player = gamedata->getPlayer(player_id);
  if (!player) return SquadRole::Fringe;
  if (player->get().getTeamId() == club_id) return world.squadRole(player_id);
  const StatsConfig& config = gamedata->getStatsConfig();
  const double overall = player->get().getOverall(config);
  std::size_t rank = 0;
  for (const auto& other : gamedata->getPlayersForTeam(club_id))
  {
    if (other.get().getOverall(config) > overall) ++rank;
  }
  return roleForRank(rank);
}

SaleContext TransferMarket::saleContext(PlayerID player_id, TeamID buyer_id,
                                        const GameDateValue& date,
                                        std::uint32_t listing_price) const
{
  SaleContext context;
  const auto player = gamedata->getPlayer(player_id);
  if (!player) return context;
  const Player& p = player->get();
  p.updateMarketValue(gamedata->getStatsConfig());
  context.market_value = p.getMarketValue();
  context.age = p.getAge();
  context.role = world.squadRole(player_id);
  context.contract_years = p.getContractYears();
  const auto seller = gamedata->getTeam(p.getTeamId());
  const auto buyer = gamedata->getTeam(buyer_id);
  if (seller) context.seller_reputation = seller->get().getReputation();
  if (buyer) context.buyer_reputation = buyer->get().getReputation();
  context.same_league =
      seller && buyer &&
      seller->get().getLeagueId() == buyer->get().getLeagueId();
  context.listed = listing_price > 0;
  context.listing_price = listing_price;
  if (const PlayerMarketFlags* extras = flags(player_id))
    context.release_clause = extras->release_clause;
  const WindowInfo window = clubWindow(buyer_id, date);
  context.winter_window = window.winter;
  context.days_to_deadline = window.open ? window.days_to_deadline : -1;
  // One draw per player and window: asking again cannot change the answer.
  const std::uint64_t window_key =
      static_cast<std::uint64_t>(date.year) * 2U + (window.winter ? 1U : 0U);
  context.resolve = static_cast<float>(WorldRng::hashUniform(
      gamedata->getWorldSeed(), RngDomain::Transfers, player_id,
      mixHash(window_key, SELLER_RESOLVE_KEY)));
  return context;
}

PlayerContext TransferMarket::playerContext(PlayerID player_id, TeamID club_id,
                                            ContractKind kind) const
{
  PlayerContext context;
  context.kind = kind;
  const auto player = gamedata->getPlayer(player_id);
  if (!player) return context;
  const Player& p = player->get();
  context.age = p.getAge();
  const LoanDeal* loan = findLoan(player_id);
  context.current_wage = loan ? loan->full_wage : p.getWage();
  // A renewal is priced on what players of his level earn at the club, not
  // only on the wage he signed for years ago.
  if (kind == ContractKind::Renewal)
    context.current_wage =
        std::max(context.current_wage, deservedWage(player_id, club_id));
  p.updateMarketValue(gamedata->getStatsConfig());
  context.market_value = p.getMarketValue();
  context.ambition = p.getTraits().ambition;
  context.loyalty = p.getTraits().loyalty;
  context.unsettled = p.getDynamics().transfer_interest_weeks > 0;
  const TeamID current = loan ? loan->parent : p.getTeamId();
  if (const auto team = gamedata->getTeam(current);
      team && current != FREE_AGENTS_TEAM_ID)
  {
    context.current_club_reputation = team->get().getReputation();
    context.current_role = world.squadRole(player_id);
  }
  else
  {
    context.current_role = SquadRole::Backup;
  }
  if (const auto club = gamedata->getTeam(club_id))
    context.new_club_reputation = club->get().getReputation();
  context.projected_role = projectedRole(player_id, club_id);
  return context;
}

bool TransferMarket::wouldJoin(PlayerID player_id, TeamID club_id,
                               ContractKind kind) const
{
  const PlayerContext context = playerContext(player_id, club_id, kind);
  const ContractDemand demand = contractDemand(context);
  return evaluateContract(context, demandedOffer(demand), 0).accepted;
}

// ---------------------------------------------------------------------------
// Moves
// ---------------------------------------------------------------------------

std::uint32_t TransferMarket::agentFee(const Deal& deal)
{
  using Offer = TransferTuning::Offer;
  if (deal.contract.agent_fee.has_value()) return *deal.contract.agent_fee;
  if (deal.kind == TransferKind::Permanent && deal.terms.fee > 0)
    return static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(deal.terms.fee) * Offer::AGENT_FEE_PERCENT /
        100U);
  return deal.contract.weekly_wage * Offer::FREE_AGENT_FEE_WEEKS;
}

bool TransferMarket::completeTransfer(const Deal& deal,
                                      const GameDateValue& date,
                                      TeamID managed_team_id)
{
  Player* player = mutablePlayer(deal.player_id);
  const auto buyer = gamedata->getTeam(deal.buyer_id);
  if (!player || !buyer || deal.buyer_id == FREE_AGENTS_TEAM_ID) return false;
  const TeamID seller_id = player->getTeamId();
  if (seller_id == deal.buyer_id || !isValid(deal.terms) ||
      deal.contract.years == 0 ||
      deal.contract.years > maxContractYears(player->getAge()))
    return false;
  if (deal.kind != TransferKind::PreContract && !canBeTraded(deal.player_id))
    return false;
  // A computer-managed club never sells the goalkeepers it needs.
  if (deal.kind != TransferKind::PreContract && !deal.binding &&
      seller_id != managed_team_id &&
      !keepsAiKeepers(seller_id, deal.player_id))
    return false;
  Deal effective = deal;
  if (seller_id == FREE_AGENTS_TEAM_ID)
  {
    effective.terms = OfferTerms{};
    if (effective.kind == TransferKind::Permanent)
      effective.kind = TransferKind::Free;
  }
  const OfferTerms& terms = effective.terms;
  Finances& buyer_finances = buyer->get().getFinances();

  if (terms.fee > 0)
  {
    const std::uint32_t upfront = upfrontAmount(terms);
    const std::int64_t sell_on =
        paySellOns(deal.player_id, seller_id, deal.buyer_id, terms.fee, date);
    buyer_finances.record(date, FinanceCategory::TransferFeeOut,
                          -static_cast<std::int64_t>(upfront));
    if (auto seller = gamedata->getTeam(seller_id))
    {
      // A sell-on share above the upfront part is a net outflow; the
      // allowance never drops below zero.
      Finances& finances = seller->get().getFinances();
      finances.record(date, FinanceCategory::TransferFeeIn,
                      static_cast<std::int64_t>(upfront) - sell_on);
      finances.setTransferBudget(finances.getTransferBudget());
    }
    const auto instalments = instalmentAmounts(terms);
    for (std::size_t year = 0; year < instalments.size(); ++year)
    {
      TransferObligation obligation;
      obligation.id = next_id++;
      obligation.kind = ObligationKind::Instalment;
      obligation.player_id = deal.player_id;
      obligation.payer = deal.buyer_id;
      obligation.payee = seller_id;
      obligation.amount = instalments[year];
      obligation.due = date + static_cast<std::size_t>(
                                  DAYS_PER_YEAR * static_cast<int>(year + 1));
      pending_obligations.push_back(obligation);
    }
    const auto add_bonus =
        [&](ObligationKind kind, std::uint32_t amount, std::uint16_t target)
    {
      if (amount == 0 || target == 0) return;
      TransferObligation obligation;
      obligation.id = next_id++;
      obligation.kind = kind;
      obligation.player_id = deal.player_id;
      obligation.payer = deal.buyer_id;
      obligation.payee = seller_id;
      obligation.amount = amount;
      obligation.due = date;
      obligation.target = target;
      obligation.baseline = careerCount(deal.player_id, deal.buyer_id, kind);
      pending_obligations.push_back(obligation);
    };
    add_bonus(ObligationKind::AppearanceBonus, terms.appearance_bonus,
              terms.appearance_target);
    add_bonus(ObligationKind::GoalBonus, terms.goal_bonus, terms.goal_target);
    if (terms.sell_on_percent > 0)
    {
      TransferObligation obligation;
      obligation.id = next_id++;
      obligation.kind = ObligationKind::SellOn;
      obligation.player_id = deal.player_id;
      obligation.payer = deal.buyer_id;
      obligation.payee = seller_id;
      obligation.amount = terms.sell_on_percent;
      obligation.due = date;
      pending_obligations.push_back(obligation);
    }
    {
      const std::scoped_lock lock(payables_cache_mutex);
      payables_dirty = true;
    }
  }
  else
  {
    // Clauses of the old club lapse on a free move.
    std::erase_if(pending_obligations,
                  [deal_player_id = deal.player_id,
                   seller_id](const TransferObligation& obligation)
                  {
                    return obligation.player_id == deal_player_id &&
                           obligation.kind != ObligationKind::Instalment &&
                           obligation.payer == seller_id;
                  });
  }

  if (const std::uint32_t agent = agentFee(effective); agent > 0)
    buyer_finances.record(date, FinanceCategory::TransferFeeOut,
                          -static_cast<std::int64_t>(agent));
  if (deal.contract.signing_bonus > 0)
    buyer_finances.record(
        date, FinanceCategory::Wages,
        -static_cast<std::int64_t>(deal.contract.signing_bonus));

  clearOnMove(deal.player_id);
  movePlayer(deal.player_id, seller_id, deal.buyer_id, managed_team_id);
  player->setWage(deal.contract.weekly_wage);
  player->setContractYears(deal.contract.years);
  addContractExtras(deal.player_id, deal.buyer_id, deal.contract, date);
  if (deal.contract.release_clause > 0 || deal.contract.promised_role)
  {
    PlayerMarketFlags& extras = mutableFlags(deal.player_id);
    extras.release_clause = deal.contract.release_clause;
    extras.promised_role = deal.contract.promised_role;
    extras.promise_date = date;
  }
  addRecord({deal.player_id, date, seller_id, deal.buyer_id, terms.fee,
             effective.kind});

  if (effective.kind == TransferKind::Permanent)
  {
    world.onTransferCompleted(date, deal.player_id, seller_id, deal.buyer_id,
                              terms.fee, managed_team_id);
  }
  else
  {
    world.onTransferCompleted(date, deal.player_id, seller_id, deal.buyer_id, 0,
                              NO_RECIPIENT);
    if (deal.buyer_id == managed_team_id)
      post(date, InboxCategory::Transfer, "INBOX_FREE_SIGNING_TITLE",
           "INBOX_FREE_SIGNING_BODY",
           {player->getName(), teamName(seller_id),
            std::to_string(deal.contract.years)},
           deal.player_id, seller_id);
    else if (seller_id == managed_team_id)
      post(date, InboxCategory::Transfer, "INBOX_FREE_EXIT_TITLE",
           "INBOX_FREE_EXIT_BODY", {player->getName(), teamName(deal.buyer_id)},
           deal.player_id, deal.buyer_id);
  }
  return true;
}

bool TransferMarket::startLoan(PlayerID player_id, TeamID borrower_id,
                               const LoanTerms& terms,
                               const GameDateValue& date,
                               TeamID managed_team_id)
{
  Player* player = mutablePlayer(player_id);
  if (!player || !gamedata->getTeam(borrower_id)) return false;
  const TeamID parent = player->getTeamId();
  if (parent == FREE_AGENTS_TEAM_ID || borrower_id == FREE_AGENTS_TEAM_ID ||
      parent == borrower_id || !canBeTraded(player_id) ||
      terms.wage_share > 100 ||
      !loanWithinLimits(player_id, parent, borrower_id) ||
      (parent != managed_team_id && !keepsAiKeepers(parent, player_id)))
    return false;

  LoanDeal loan;
  loan.player_id = player_id;
  loan.parent = parent;
  loan.borrower = borrower_id;
  loan.start = date;
  loan.end = loanEndDate(date, terms.duration);
  loan.wage_share = terms.wage_share;
  loan.full_wage = player->getWage();
  loan.option_fee = terms.option_fee;
  loan.obligation = terms.obligation && terms.option_fee > 0;
  loan.recall_clause = terms.recall_clause;
  // A loan never outlives the player's contract: he would be released while
  // still registered with the borrower.
  if (!(date < loan.end) || player->getContractYears() == 0 ||
      contractEndDate(date, player->getContractYears()) < loan.end)
    return false;

  pay(borrower_id, parent, terms.loan_fee, date);
  clearOnMove(player_id);
  movePlayer(player_id, parent, borrower_id, managed_team_id);
  player->setWage(static_cast<std::uint32_t>(
      static_cast<std::uint64_t>(loan.full_wage) * loan.wage_share / 100U));
  active_loans[player_id] = loan;
  if (terms.min_appearances > 0 && terms.unplayed_fee > 0)
  {
    TransferObligation clause;
    clause.id = next_id++;
    clause.kind = ObligationKind::LoanUnplayedFee;
    clause.player_id = player_id;
    clause.payer = borrower_id;
    clause.payee = parent;
    clause.amount = terms.unplayed_fee;
    clause.due = loan.end;
    clause.target = terms.min_appearances;
    clause.baseline =
        careerCount(player_id, borrower_id, ObligationKind::AppearanceBonus);
    pending_obligations.push_back(clause);
  }
  addRecord({player_id, date, parent, borrower_id, terms.loan_fee,
             TransferKind::Loan});

  const std::string end_text = loan.end.toString();
  if (parent == managed_team_id)
    post(date, InboxCategory::Transfer, "INBOX_LOAN_OUT_TITLE",
         "INBOX_LOAN_OUT_BODY",
         {player->getName(), teamName(borrower_id), end_text,
          std::to_string(loan.wage_share)},
         player_id, borrower_id);
  else if (borrower_id == managed_team_id)
    post(date, InboxCategory::Transfer, "INBOX_LOAN_IN_TITLE",
         "INBOX_LOAN_IN_BODY",
         {player->getName(), teamName(parent), end_text,
          std::to_string(loan.wage_share)},
         player_id, parent);
  return true;
}

bool TransferMarket::endLoan(PlayerID player_id, const GameDateValue& date,
                             TeamID managed_team_id, bool recalled)
{
  const auto found = active_loans.find(player_id);
  if (found == active_loans.end()) return false;
  const LoanDeal loan = found->second;
  active_loans.erase(found);
  // A recall voids the appearance clause: the parent took him back.
  settleLoanClause(loan, date, managed_team_id, !recalled);
  Player* player = mutablePlayer(player_id);
  if (!player) return true;
  player->setWage(loan.full_wage);
  movePlayer(player_id, player->getTeamId(), loan.parent, managed_team_id);
  addRecord({player_id, date, loan.borrower, loan.parent, 0,
             TransferKind::LoanReturn});
  if (loan.parent == managed_team_id || loan.borrower == managed_team_id)
    post(date, InboxCategory::Transfer,
         recalled ? "INBOX_LOAN_RECALL_TITLE" : "INBOX_LOAN_RETURN_TITLE",
         recalled ? "INBOX_LOAN_RECALL_BODY" : "INBOX_LOAN_RETURN_BODY",
         {player->getName(), teamName(loan.borrower), teamName(loan.parent)},
         player_id,
         loan.parent == managed_team_id ? loan.borrower : loan.parent);
  return true;
}

bool TransferMarket::exerciseLoanOption(PlayerID player_id,
                                        const GameDateValue& date,
                                        TeamID managed_team_id)
{
  const auto found = active_loans.find(player_id);
  if (found == active_loans.end() || found->second.option_fee == 0)
    return false;
  const LoanDeal loan = found->second;
  Player* player = mutablePlayer(player_id);
  if (!player) return false;
  active_loans.erase(found);
  settleLoanClause(loan, date, managed_team_id, false);
  player->setWage(loan.full_wage);
  movePlayer(player_id, player->getTeamId(), loan.parent, managed_team_id);

  Deal deal;
  deal.player_id = player_id;
  deal.buyer_id = loan.borrower;
  deal.kind = TransferKind::Permanent;
  deal.terms = aiOfferTerms(loan.option_fee);
  deal.binding = true;
  deal.contract.weekly_wage = loan.full_wage;
  deal.contract.years = std::clamp<std::uint8_t>(
      std::max<std::uint8_t>(player->getContractYears(), 2), 1,
      maxContractYears(player->getAge()));
  if (completeTransfer(deal, date, managed_team_id)) return true;
  // The purchase failed: the player stays with the parent club.
  addRecord({player_id, date, loan.borrower, loan.parent, 0,
             TransferKind::LoanReturn});
  return false;
}

bool TransferMarket::releasePlayer(PlayerID player_id,
                                   const GameDateValue& date,
                                   TeamID managed_team_id)
{
  return endContract(player_id, date, managed_team_id, true);
}

bool TransferMarket::endContract(PlayerID player_id, const GameDateValue& date,
                                 TeamID managed_team_id, bool severance_due)
{
  Player* player = mutablePlayer(player_id);
  if (!player) return false;
  const TeamID club_id = player->getTeamId();
  const auto club = gamedata->getTeam(club_id);
  if (!club || club_id == FREE_AGENTS_TEAM_ID || !canBeTraded(player_id) ||
      (severance_due && club_id != managed_team_id &&
       !keepsAiKeepers(club_id, player_id)))
    return false;
  const std::int64_t severance =
      severance_due
          ? severancePay(player->getWage(), player->getContractYears(), date)
          : 0;
  if (severance > 0)
    club->get().getFinances().record(date, FinanceCategory::Wages, -severance);
  std::erase_if(pending_obligations,
                [player_id, club_id](const TransferObligation& obligation)
                {
                  return obligation.player_id == player_id &&
                         obligation.kind != ObligationKind::Instalment &&
                         obligation.payer == club_id;
                });
  clearOnMove(player_id);
  movePlayer(player_id, club_id, FREE_AGENTS_TEAM_ID, managed_team_id);
  player->setContractYears(0);
  addRecord({player_id, date, club_id, FREE_AGENTS_TEAM_ID,
             static_cast<std::uint32_t>(std::min<std::int64_t>(
                 severance, std::numeric_limits<std::uint32_t>::max())),
             TransferKind::Release});
  if (club_id == managed_team_id)
    post(date, InboxCategory::Contract, "INBOX_RELEASE_TITLE",
         "INBOX_RELEASE_BODY", {player->getName(), formatMoney(severance)},
         player_id, club_id);
  return true;
}

bool TransferMarket::agreePreContract(PlayerID player_id, TeamID club_id,
                                      const ContractOffer& terms,
                                      const GameDateValue& date,
                                      TeamID managed_team_id)
{
  const Player* player = mutablePlayer(player_id);
  if (!player || !gamedata->getTeam(club_id)) return false;
  const LoanDeal* loan = findLoan(player_id);
  const TeamID owner = loan ? loan->parent : player->getTeamId();
  if (owner == FREE_AGENTS_TEAM_ID || owner == club_id ||
      club_id == FREE_AGENTS_TEAM_ID || pre_contracts.contains(player_id) ||
      !canSignPreContract(player->getContractYears(), date) ||
      terms.years == 0 || terms.years > maxContractYears(player->getAge()))
    return false;
  pre_contracts[player_id] = {player_id, owner, club_id, date, terms};
  // The contract runs from 1 July: its extras and the agreed agent fee wait
  // with the obligations until then.
  addContractExtras(player_id, club_id, terms, seasonEndDate(date) + 1);
  if (terms.agent_fee.has_value())
  {
    TransferObligation fee;
    fee.id = next_id++;
    fee.kind = ObligationKind::AgentFee;
    fee.player_id = player_id;
    fee.payer = club_id;
    fee.payee = FREE_AGENTS_TEAM_ID;
    fee.amount = *terms.agent_fee;
    fee.due = date;
    pending_obligations.push_back(fee);
  }
  negotiations.erase(player_id);
  std::erase_if(incoming, [player_id](const IncomingOffer& offer)
                { return offer.player_id == player_id; });
  if (club_id == managed_team_id)
    post(date, InboxCategory::Transfer, "INBOX_PRECONTRACT_SIGNED_TITLE",
         "INBOX_PRECONTRACT_SIGNED_BODY", {player->getName(), teamName(owner)},
         player_id, owner);
  else if (owner == managed_team_id)
    post(date, InboxCategory::Transfer, "INBOX_PRECONTRACT_LOST_TITLE",
         "INBOX_PRECONTRACT_LOST_BODY", {player->getName(), teamName(club_id)},
         player_id, club_id);
  return true;
}

void TransferMarket::recordLegacyTransfer(PlayerID player_id, TeamID from_team,
                                          TeamID to_team, std::uint32_t fee,
                                          const GameDateValue& date)
{
  if (fee > 0 && from_team != FREE_AGENTS_TEAM_ID)
  {
    const std::int64_t sell_on =
        paySellOns(player_id, from_team, to_team, fee, date);
    if (auto seller = gamedata->getTeam(from_team); seller && sell_on > 0)
    {
      Finances& finances = seller->get().getFinances();
      finances.record(date, FinanceCategory::TransferFeeIn, -sell_on);
      finances.setTransferBudget(finances.getTransferBudget());
    }
  }
  clearOnMove(player_id);
  addRecord({player_id, date, from_team, to_team, fee,
             from_team == FREE_AGENTS_TEAM_ID ? TransferKind::Free
                                              : TransferKind::Permanent});
}

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

bool TransferMarket::canBeTraded(PlayerID player_id) const
{
  return !active_loans.contains(player_id) &&
         !pre_contracts.contains(player_id);
}

bool TransferMarket::loanWithinLimits(PlayerID player_id, TeamID parent,
                                      TeamID borrower) const
{
  using L = TransferTuning::Loan;
  int out = 0;
  int in = 0;
  int between = 0;
  const auto counts = [&](PlayerID id)
  {
    const auto player = gamedata->getPlayer(id);
    return player && player->get().getAge() > L::EXEMPT_MAX_AGE;
  };
  for (const auto& [id, loan] : active_loans)
  {
    if (loan.parent == parent && loan.borrower == borrower) ++between;
    if (!counts(id)) continue;
    if (loan.parent == parent) ++out;
    if (loan.borrower == borrower) ++in;
  }
  if (between >= L::MAX_BETWEEN_CLUBS) return false;
  if (!counts(player_id)) return true;
  return out < L::MAX_LOANS_OUT && in < L::MAX_LOANS_IN;
}

std::int64_t TransferMarket::committedPayables(TeamID team_id,
                                               const GameDateValue& date) const
{
  const std::scoped_lock lock(payables_cache_mutex);
  if (const std::int32_t day = dayOrdinal(date);
      payables_dirty || payables_day != day)
  {
    payables_cache.clear();
    const GameDateValue season_end = seasonEndDate(date);
    for (const TransferObligation& obligation : pending_obligations)
    {
      if (obligation.kind == ObligationKind::Instalment &&
          !(season_end < obligation.due))
        payables_cache[obligation.payer] += obligation.amount;
    }
    payables_day = day;
    payables_dirty = false;
  }
  const auto found = payables_cache.find(team_id);
  return found == payables_cache.end() ? 0 : found->second;
}

std::int64_t TransferMarket::spendableBudget(TeamID team_id,
                                             const GameDateValue& date) const
{
  const auto team = gamedata->getTeam(team_id);
  if (!team || team_id == FREE_AGENTS_TEAM_ID ||
      world.isTransferEmbargoed(team_id))
    return 0;
  const Finances& finances = team->get().getFinances();
  return ClubEconomy::availableTransferBudget(
      finances.getTransferBudget(), finances.getBalance(),
      finances.getCurrentWageSpending(*gamedata, team->get()),
      committedPayables(team_id, date));
}

std::int64_t TransferMarket::aiWageRoom(TeamID club_id, bool next_season) const
{
  const auto club = gamedata->getTeam(club_id);
  if (!club || club_id == FREE_AGENTS_TEAM_ID) return 0;
  std::int64_t committed = 0;
  if (next_season)
  {
    // Contracts in their final year run out on 30 June and loanees go back
    // to their clubs; the club's own loanees return to its payroll.
    for (const PlayerID player_id : club->get().getPlayerIDs())
    {
      const auto player = gamedata->getPlayer(player_id);
      const LoanDeal* loan = findLoan(player_id);
      if (player && player->get().getContractYears() > 1 &&
          !(loan && loan->borrower == club_id))
        committed += player->get().getWage();
    }
    for (const auto& [player_id, loan] : active_loans)
    {
      const auto player = gamedata->getPlayer(player_id);
      if (loan.parent == club_id && player &&
          player->get().getContractYears() > 1)
        committed += loan.full_wage;
    }
  }
  else
  {
    committed = weeklyPayroll(*gamedata, club->get());
  }
  for (const auto& [player_id, deal] : pre_contracts)
  {
    if (deal.to_team == club_id) committed += deal.terms.weekly_wage;
  }
  return club->get().getFinances().getWageBudget() - committed;
}

std::int64_t TransferMarket::preContractCosts(TeamID club_id) const
{
  std::int64_t costs = 0;
  for (const auto& [player_id, deal] : pre_contracts)
  {
    if (deal.to_team == club_id)
      costs += static_cast<std::int64_t>(deal.terms.signing_bonus) +
               static_cast<std::int64_t>(deal.terms.weekly_wage) *
                   TransferTuning::Offer::FREE_AGENT_FEE_WEEKS;
  }
  return costs;
}

bool TransferMarket::aiAffordsWage(const Team& club, std::uint32_t weekly_wage,
                                   std::int64_t room)
{
  const auto wage = static_cast<double>(weekly_wage);
  return wage <= static_cast<double>(room) &&
         wage <= static_cast<double>(club.getFinances().getWageBudget()) *
                     static_cast<double>(
                         TransferTuning::Market::MAX_SINGLE_WAGE_SHARE);
}

bool TransferMarket::lastSeasonMove(TeamID club_id, bool promoted) const
{
  const auto& history = competitions.getSeasonHistory();
  std::uint16_t last = 0;
  for (const SeasonHistoryEntry& entry : history)
  {
    if (entry.competition_type == MatchType::LEAGUE)
      last = std::max(last, entry.season);
  }
  return last > 0 &&
         std::ranges::any_of(
             history,
             [&](const SeasonHistoryEntry& entry)
             {
               return entry.season == last &&
                      entry.competition_type == MatchType::LEAGUE &&
                      std::ranges::contains(
                          promoted ? entry.promoted : entry.relegated, club_id);
             });
}

// ---------------------------------------------------------------------------
// Daily processing
// ---------------------------------------------------------------------------

void TransferMarket::onDayAdvanced(const GameDateValue& date,
                                   TeamID managed_team_id)
{
  forgetRemovedPlayers();
  const bool week_end = dayOrdinal(date) % 7 == 0;
  if (week_end) payLoanWageShares(date);
  processLoanEnds(date, managed_team_id);
  // Contracts that ran out on 30 June start the clock on 1 July.
  if (date.day == 1 && date.month != 7) lowerFreeAgentExpectations();
  if (date.month == 7 && date.day == 1)
  {
    executePreContracts(date, managed_team_id);
    releaseOnRelegation(date, managed_team_id);
  }
  payDueInstalments(date, managed_team_id);
  applyWageRises(date);
  if (week_end) checkAddOns(date, managed_team_id);
  if (week_end) payAppearanceFees(date);
  if (date.day == 1) checkPromises(date, managed_team_id);
  expireOffers(date);
  postNewsDigest(date, managed_team_id);
  postDeadlineSummary(date, managed_team_id);
}

void TransferMarket::payLoanWageShares(const GameDateValue& date)
{
  // The borrower's payroll carries its share through the loanee's wage;
  // the parent pays the rest on the same weekly cadence.
  for (const auto& [player_id, loan] : active_loans)
  {
    if (const auto player = gamedata->getPlayer(player_id);
        !player || player->get().getTeamId() != loan.borrower)
      continue;
    const auto parent = gamedata->getTeam(loan.parent);
    const std::int64_t share = static_cast<std::int64_t>(loan.full_wage) *
                               (100 - loan.wage_share) / 100;
    if (parent && share > 0)
      parent->get().getFinances().record(date, FinanceCategory::Wages, -share);
  }
}

void TransferMarket::processLoanEnds(const GameDateValue& date,
                                     TeamID managed_team_id)
{
  // A loanee who retired or already moved on (a completed pre-contract)
  // has nothing left to return from.
  std::erase_if(active_loans,
                [this](const auto& entry)
                {
                  const auto player = gamedata->getPlayer(entry.first);
                  return !player ||
                         player->get().getTeamId() != entry.second.borrower;
                });
  std::vector<PlayerID> ending;
  for (const auto& [player_id, loan] : active_loans)
  {
    if (!(date < loan.end)) ending.push_back(player_id);
  }
  std::ranges::sort(ending);
  for (const PlayerID player_id : ending)
  {
    const LoanDeal loan = active_loans.at(player_id);
    bool bought = false;
    if (loan.option_fee > 0)
    {
      bool buy = loan.obligation;
      if (!buy && loan.borrower != managed_team_id)
      {
        // AI borrowers keep loanees who earned a place and are affordable.
        const auto borrower = gamedata->getTeam(loan.borrower);
        buy = borrower &&
              level(world.squadRole(player_id)) <= level(SquadRole::Rotation) &&
              spendableBudget(loan.borrower, date) >=
                  static_cast<std::int64_t>(loan.option_fee);
      }
      if (buy) bought = exerciseLoanOption(player_id, date, managed_team_id);
    }
    if (!bought && active_loans.contains(player_id))
      endLoan(player_id, date, managed_team_id, false);
  }
}

void TransferMarket::executePreContracts(const GameDateValue& date,
                                         TeamID managed_team_id)
{
  std::vector<PreContractDeal> due;
  due.reserve(pre_contracts.size());
  for (const auto& [player_id, deal] : pre_contracts) due.push_back(deal);
  pre_contracts.clear();
  std::ranges::sort(due, {}, &PreContractDeal::player_id);
  for (const PreContractDeal& agreement : due)
  {
    Deal deal;
    deal.player_id = agreement.player_id;
    deal.buyer_id = agreement.to_team;
    deal.kind = TransferKind::PreContract;
    deal.contract = agreement.terms;
    if (const auto fee = std::ranges::find_if(
            pending_obligations,
            [&](const TransferObligation& obligation)
            {
              return obligation.kind == ObligationKind::AgentFee &&
                     obligation.player_id == agreement.player_id &&
                     obligation.payer == agreement.to_team;
            });
        fee != pending_obligations.end())
    {
      deal.contract.agent_fee =
          static_cast<std::uint32_t>(std::max<std::int64_t>(fee->amount, 0));
      pending_obligations.erase(fee);
    }
    const Player* player = mutablePlayer(agreement.player_id);
    // Retired players simply drop out of their agreement.
    if (player && player->getTeamId() != agreement.to_team)
      completeTransfer(deal, date, managed_team_id);
  }
}

void TransferMarket::lowerFreeAgentExpectations()
{
  // Nobody pays what a free agent earned before: the longer he waits, the
  // closer his demands come to what clubs at his level can offer.
  const auto free_agents = gamedata->getTeam(FREE_AGENTS_TEAM_ID);
  if (!free_agents) return;
  for (const PlayerID player_id : free_agents->get().getPlayerIDs())
  {
    Player* player = mutablePlayer(player_id);
    if (!player) continue;
    player->setWage(
        std::max(TransferTuning::Contract::MINIMUM_WEEKLY_WAGE,
                 static_cast<std::uint32_t>(
                     static_cast<float>(player->getWage()) *
                     TransferTuning::Market::FREE_AGENT_MONTHLY_WAGE_FACTOR)));
  }
}

void TransferMarket::releaseOnRelegation(const GameDateValue& date,
                                         TeamID managed_team_id)
{
  using M = TransferTuning::Market;
  // Relegation clauses: some first-team players of a relegated club may
  // leave for free. Whether a player has one is drawn from the world seed,
  // so it needs no saved state. The manager's own contracts carry none.
  constexpr std::uint64_t RELEGATION_CLAUSE_KEY = 0x2E1E6A;
  const std::uint64_t seed = gamedata->getWorldSeed();
  std::vector<TeamID> relegated;
  for (const auto& team : gamedata->getTeamsVector())
  {
    const TeamID id = team.get().getId();
    if (id != FREE_AGENTS_TEAM_ID && id != managed_team_id &&
        lastSeasonMove(id, false))
      relegated.push_back(id);
  }
  std::ranges::sort(relegated);
  for (const TeamID club_id : relegated)
  {
    const auto ranked =
        rankedSquad(*gamedata, gamedata->getTeam(club_id)->get());
    int exits = 0;
    for (std::size_t rank = 0; rank < ranked.size() && rank < FIRST_TEAM_SIZE &&
                               exits < M::RELEGATION_CLAUSE_MAX_EXITS;
         ++rank)
    {
      const PlayerID player_id = ranked[rank].second;
      const auto player = gamedata->getPlayer(player_id);
      if (player && player->get().getAge() <= M::RELEGATION_CLAUSE_MAX_AGE &&
          player->get().getContractYears() > 0 &&
          WorldRng::hashUniform(seed, RngDomain::Transfers, player_id,
                                RELEGATION_CLAUSE_KEY) <
              static_cast<double>(M::RELEGATION_CLAUSE_SHARE) &&
          endContract(player_id, date, managed_team_id, false))
        ++exits;
    }
  }
}

void TransferMarket::payDueInstalments(const GameDateValue& date,
                                       TeamID managed_team_id)
{
  std::vector<TransferObligation> due;
  std::erase_if(pending_obligations,
                [&due, date](const TransferObligation& obligation)
                {
                  if (obligation.kind != ObligationKind::Instalment ||
                      date < obligation.due)
                    return false;
                  due.push_back(obligation);
                  return true;
                });
  if (due.empty()) return;
  {
    const std::scoped_lock lock(payables_cache_mutex);
    payables_dirty = true;
  }
  for (const TransferObligation& obligation : due)
  {
    pay(obligation.payer, obligation.payee, obligation.amount, date);
    if (obligation.payer != managed_team_id &&
        obligation.payee != managed_team_id)
      continue;
    const auto player = gamedata->getPlayer(obligation.player_id);
    const bool paying = obligation.payer == managed_team_id;
    post(date, InboxCategory::Finance,
         paying ? "INBOX_INSTALMENT_PAID_TITLE"
                : "INBOX_INSTALMENT_RECEIVED_TITLE",
         paying ? "INBOX_INSTALMENT_PAID_BODY"
                : "INBOX_INSTALMENT_RECEIVED_BODY",
         {formatMoney(obligation.amount),
          teamName(paying ? obligation.payee : obligation.payer),
          player ? player->get().getName() : std::string()},
         std::nullopt, paying ? obligation.payee : obligation.payer);
  }
}

void TransferMarket::checkAddOns(const GameDateValue& date,
                                 TeamID managed_team_id)
{
  std::vector<TransferObligation> triggered;
  std::erase_if(
      pending_obligations,
      [this, &triggered](const TransferObligation& obligation)
      {
        if (obligation.kind != ObligationKind::AppearanceBonus &&
            obligation.kind != ObligationKind::GoalBonus)
          return false;
        // The condition lapses when the player leaves the buying
        // club.
        if (const auto player = gamedata->getPlayer(obligation.player_id);
            !player || player->get().getTeamId() != obligation.payer)
          return true;
        if (const std::uint16_t count = careerCount(
                obligation.player_id, obligation.payer, obligation.kind);
            count < obligation.baseline + obligation.target)
          return false;
        triggered.push_back(obligation);
        return true;
      });
  for (const TransferObligation& obligation : triggered)
  {
    pay(obligation.payer, obligation.payee, obligation.amount, date);
    if (obligation.payer != managed_team_id &&
        obligation.payee != managed_team_id)
      continue;
    const auto player = gamedata->getPlayer(obligation.player_id);
    post(date, InboxCategory::Finance, "INBOX_ADDON_TITLE", "INBOX_ADDON_BODY",
         {player ? player->get().getName() : std::string(),
          formatMoney(obligation.amount), teamName(obligation.payer),
          teamName(obligation.payee)},
         obligation.player_id, std::nullopt);
  }
}

void TransferMarket::checkPromises(const GameDateValue& date,
                                   TeamID managed_team_id)
{
  using N = TransferTuning::Negotiation;
  const std::int32_t today = dayOrdinal(date);
  for (auto& [player_id, extras] : player_flags)
  {
    if (!extras.promised_role ||
        today - dayOrdinal(extras.promise_date) < N::PROMISE_GRACE_DAYS)
      continue;
    Player* player = mutablePlayer(player_id);
    if (!player || player->getTeamId() != managed_team_id) continue;
    if (const SquadRole actual = world.squadRole(player_id);
        level(actual) - level(*extras.promised_role) < N::BROKEN_PROMISE_LEVELS)
      continue;
    PlayerDynamics& dynamics = player->mutableDynamics();
    dynamics.morale =
        std::max(0.0f, dynamics.morale - N::BROKEN_PROMISE_MORALE);
    post(date, InboxCategory::Contract, "INBOX_PROMISE_BROKEN_TITLE",
         "INBOX_PROMISE_BROKEN_BODY",
         {player->getName(),
          "@" + std::string(squadRoleKey(*extras.promised_role))},
         player_id, managed_team_id);
    extras.promised_role.reset();
  }
  std::erase_if(player_flags,
                [](const auto& entry) { return entry.second.empty(); });
}

void TransferMarket::expireOffers(const GameDateValue& date)
{
  // An offer waiting for the buyer's answer stays until the answer comes;
  // one left unanswered counts as turned down.
  std::vector<std::pair<PlayerID, TeamID>> ignored;
  std::erase_if(incoming,
                [date, &ignored](const IncomingOffer& offer)
                {
                  const bool expired =
                      offer.expires < date &&
                      offer.status != OfferStatus::AwaitingBuyer;
                  if (expired)
                    ignored.emplace_back(offer.player_id, offer.buyer);
                  return expired;
                });
  for (const auto& [player_id, buyer] : ignored)
    closeTalks(player_id, buyer, date);
  std::erase_if(cooldowns, [date](const TalksCooldown& cooldown)
                { return cooldown.until < date; });
  const std::int32_t today = dayOrdinal(date);
  for (auto& [player_id, extras] : player_flags)
  {
    if (extras.not_for_sale_until != 0 && extras.not_for_sale_until < today)
      extras.not_for_sale_until = 0;
  }
  std::erase_if(player_flags,
                [](const auto& entry) { return entry.second.empty(); });
  std::erase_if(negotiations, [date](const auto& entry)
                { return entry.second.expires < date; });
}

void TransferMarket::postNewsDigest(const GameDateValue& date,
                                    TeamID managed_team_id)
{
  if (managed_team_id == FREE_AGENTS_TEAM_ID) return;
  const std::int32_t today = dayOrdinal(date);
  const LeagueID league = leagueOf(*gamedata, managed_team_id);
  const bool open = TransferWindows::isOpen(league, date);
  const bool window_closed = !open && TransferWindows::isOpen(league, date - 1);
  std::int32_t first = 0;
  std::int32_t last = 0;
  if (constexpr std::int32_t PERIOD = TransferTuning::Market::NEWS_DIGEST_DAYS;
      open && today % PERIOD == 0)
  {
    first = today - PERIOD + 1;
    last = today;
  }
  else if (window_closed)
  {
    const std::int32_t offset = today % PERIOD == 0 ? PERIOD : today % PERIOD;
    first = today - offset + 1;
    last = today - 1;
  }
  else
  {
    return;
  }

  int fee_moves = 0;
  int free_moves = 0;
  int loan_moves = 0;
  std::vector<const TransferRecord*> deals;
  using enum TransferKind;
  for (auto it = records.rbegin(); it != records.rend(); ++it)
  {
    const std::int32_t day = dayOrdinal(it->date);
    if (day < first) break;
    if (day > last) continue;
    switch (it->kind)
    {
      case Permanent:
        ++fee_moves;
        deals.push_back(std::to_address(it));
        break;
      case Free:
      case PreContract:
        ++free_moves;
        break;
      case Loan:
        ++loan_moves;
        break;
      default:
        break;
    }
  }
  const int total = fee_moves + free_moves + loan_moves;
  if (total == 0) return;
  std::ranges::sort(deals, [](const TransferRecord* a, const TransferRecord* b)
                    { return a->fee > b->fee; });
  std::string lines;
  std::size_t listed = 0;
  for (const TransferRecord* deal : deals)
  {
    if (listed == TransferTuning::Market::NEWS_DIGEST_LIMIT) break;
    const auto player = gamedata->getPlayer(deal->player_id);
    if (!player) continue;
    if (!lines.empty()) lines += '\n';
    lines += player->get().getName() + ": " + teamName(deal->from_team) +
             " -> " + teamName(deal->to_team) + " (" + formatMoney(deal->fee) +
             ")";
    ++listed;
  }
  post(date, InboxCategory::Transfer, "INBOX_TRANSFER_NEWS_TITLE",
       "INBOX_TRANSFER_NEWS_BODY",
       {std::to_string(total), std::to_string(fee_moves),
        std::to_string(free_moves), std::to_string(loan_moves),
        lines.empty() ? std::string("@INBOX_TRANSFER_NEWS_NONE") : lines},
       std::nullopt, std::nullopt);
}

// ---------------------------------------------------------------------------
// AI
// ---------------------------------------------------------------------------

std::optional<TransferMarket::AiNeed> TransferMarket::assessNeed(
    const std::vector<std::pair<double, PlayerID>>& ranked,
    float level_boost) const
{
  using M = TransferTuning::Market;
  struct Group
  {
    PlayerRole group;
    std::size_t starters; /*!< Slots in a typical XI. */
    std::size_t depth;    /*!< Squad players wanted. */
  };
  // Two senior goalkeepers: the third is usually an academy prospect.
  static constexpr std::array<Group, 7> GROUPS = {{{PlayerRole::GK, 1, 2},
                                                   {PlayerRole::CB, 2, 4},
                                                   {PlayerRole::LB, 1, 2},
                                                   {PlayerRole::RB, 1, 2},
                                                   {PlayerRole::CM, 3, 5},
                                                   {PlayerRole::LW, 2, 3},
                                                   {PlayerRole::ST, 1, 3}}};
  if (ranked.size() >= M::AI_MAX_SQUAD + 2) return std::nullopt;
  const float team_level = squadLevel(ranked);
  std::optional<AiNeed> need;
  auto worst_deficit = M::UPGRADE_DEFICIT;
  // Thinnest group relative to the depth wanted, for a squad short of
  // players overall.
  PlayerRole thinnest = PlayerRole::UNKNOWN;
  std::ptrdiff_t thinnest_margin = std::numeric_limits<std::ptrdiff_t>::max();
  for (const Group& group : GROUPS)
  {
    std::vector<double> overalls;
    for (const auto& [overall, player_id] : ranked)
    {
      // Loanees count; players leaving on a pre-contract do not.
      const auto player = gamedata->getPlayer(player_id);
      if (player && positionGroup(player->get().getRole()) == group.group &&
          !pre_contracts.contains(player_id))
        overalls.push_back(overall);
    }
    if (overalls.size() < group.depth)
      return AiNeed{group.group, team_level - M::SHORTAGE_LEVEL_MARGIN};
    if (const auto margin = static_cast<std::ptrdiff_t>(overalls.size()) -
                            static_cast<std::ptrdiff_t>(group.depth);
        margin < thinnest_margin)
    {
      thinnest_margin = margin;
      thinnest = group.group;
    }
    // overalls are sorted best first because ranked is.
    const auto weakest_starter =
        static_cast<float>(overalls[group.starters - 1]);
    const float deficit = team_level + level_boost - weakest_starter;
    if (deficit > worst_deficit)
    {
      worst_deficit = deficit;
      need =
          AiNeed{group.group, weakest_starter + M::UPGRADE_MARGIN, false, true};
    }
  }
  if (!need && ranked.size() < M::AI_TARGET_SQUAD &&
      thinnest != PlayerRole::UNKNOWN)
    need = AiNeed{thinnest, team_level - M::DEPTH_LEVEL_MARGIN, true};
  return need;
}

void TransferMarket::listLoanProspects(TeamID club_id)
{
  const auto club = gamedata->getTeam(club_id);
  if (!club || club_id == FREE_AGENTS_TEAM_ID) return;
  const auto ranked = rankedSquad(*gamedata, club->get());
  if (ranked.size() > TransferTuning::Market::AI_TARGET_SQUAD)
    listLoanProspects(ranked);
}

void TransferMarket::listLoanProspects(
    const std::vector<std::pair<double, PlayerID>>& ranked)
{
  using M = TransferTuning::Market;
  int listed = 0;
  for (const auto& [overall, player_id] : ranked)
  {
    if (isLoanListed(player_id)) ++listed;
  }
  for (std::size_t rank = M::LOAN_OUT_MIN_RANK;
       rank < ranked.size() && listed < M::MAX_LOAN_LISTED_PER_CLUB; ++rank)
  {
    const PlayerID player_id = ranked[rank].second;
    if (const auto player = gamedata->getPlayer(player_id);
        !player ||
        player->get().getAge() > TransferTuning::Loan::PROSPECT_MAX_AGE ||
        !canBeTraded(player_id) || isLoanListed(player_id) ||
        player->get().getTransferStatus() == TransferStatus::Listed ||
        !keepsAiKeepers(player->get().getTeamId(), player_id))
      continue;
    setLoanListed(player_id, true);
    ++listed;
  }
}

bool TransferMarket::aiShedSurplus(
    TeamID club_id, const std::vector<std::pair<double, PlayerID>>& ranked,
    const GameDateValue& date, TeamID managed_team_id, WorldRng& rng)
{
  using M = TransferTuning::Market;
  if (ranked.size() > M::AI_TARGET_SQUAD + 2)
  {
    // A bloated squad lets any surplus senior go; prospects are loaned.
    const int min_age = ranked.size() > M::AI_MAX_SQUAD
                            ? M::SURPLUS_RELEASE_MIN_AGE
                            : M::RELEASE_MIN_AGE;
    for (std::size_t rank = ranked.size(); rank-- > M::RELEASE_MIN_RANK;)
    {
      const PlayerID player_id = ranked[rank].second;
      const auto player = gamedata->getPlayer(player_id);
      if (player && player->get().getAge() >= min_age &&
          player->get().getContractYears() > 0 && canBeTraded(player_id) &&
          releasePlayer(player_id, date, managed_team_id))
        return true;
    }
  }
  if (ranked.size() <= M::AI_TARGET_SQUAD) return false;
  listLoanProspects(ranked);
  // Listed prospects wait for borrowers; a club only goes out to place them
  // itself when its squad is clearly too big.
  if (ranked.size() <= M::LOAN_PLACEMENT_SQUAD) return false;
  // Parent clubs place their prospects where they will play: smaller
  // clubs, often in the division below, whose wages the parent mostly
  // covers. [P]
  const auto& clubs = gamedata->getTeamsVector();
  const int parent_reputation =
      gamedata->getTeam(club_id)->get().getReputation();
  for (const auto& [overall, player_id] : ranked)
  {
    if (!isLoanListed(player_id) || clubs.empty()) continue;
    const Player& player = gamedata->getPlayer(player_id)->get();
    const SquadRole role = world.squadRole(player_id);
    int tries = 0;
    for (int draw = 0;
         draw < 3 * M::LOAN_PLACEMENT_TRIES && tries < M::LOAN_PLACEMENT_TRIES;
         ++draw)
    {
      const Team& borrower = clubs[static_cast<std::size_t>(rng.uniformInt(
                                       0, static_cast<int>(clubs.size()) - 1))]
                                 .get();
      const TeamID borrower_id = borrower.getId();
      const int gap = parent_reputation - borrower.getReputation();
      // The borrower registers him: its own window must be open.
      if (borrower_id == club_id || borrower_id == managed_team_id ||
          borrower_id == FREE_AGENTS_TEAM_ID || gap < 0 ||
          !clubWindow(borrower_id, date).open)
        continue;
      ++tries;
      if (rankedSquad(*gamedata, borrower).size() >= M::AI_MAX_SQUAD) continue;
      if (std::vector<Reason> reasons;
          !playerAcceptsLoan(role, projectedRole(player_id, borrower_id),
                             player.getTraits().ambition, gap, reasons) ||
          !loanWithinLimits(player_id, club_id, borrower_id))
        continue;
      LoanTerms terms;
      terms.wage_share =
          gap >= TransferTuning::Loan::SMALL_BORROWER_REPUTATION_GAP
              ? TransferTuning::Loan::SMALL_BORROWER_WAGE_SHARE
              : TransferTuning::Loan::LISTED_WAGE_SHARE;
      terms.duration =
          rng.chance(0.25) ? LoanDuration::SixMonths : LoanDuration::SeasonEnd;
      if (!aiAffordsWage(borrower,
                         static_cast<std::uint32_t>(
                             static_cast<std::uint64_t>(player.getWage()) *
                             terms.wage_share / 100U),
                         aiWageRoom(borrower_id, false)))
        continue;
      if (startLoan(player_id, borrower_id, terms, date, managed_team_id))
        return true;
    }
    break;
  }
  return false;
}

bool TransferMarket::aiSignFreeAgent(TeamID club_id, const AiNeed& need,
                                     std::int64_t wage_room,
                                     const GameDateValue& date,
                                     TeamID managed_team_id)
{
  const auto club = gamedata->getTeam(club_id);
  if (!club || wage_room <= 0) return false;
  // Bonus and agent fee are paid in cash, not from the transfer budget.
  const std::int64_t cash = club->get().getFinances().getBalance();
  if (cash <= 0) return false;
  const StatsConfig& config = gamedata->getStatsConfig();
  std::vector<std::pair<double, PlayerID>> free_agents;
  for (const auto& reference : gamedata->getPlayersForTeam(FREE_AGENTS_TEAM_ID))
  {
    const Player& player = reference.get();
    // The cheap position check first: the overall is a dozen map lookups.
    if (positionGroup(player.getRole()) != need.group) continue;
    const double overall = player.getOverall(config);
    // Expectations far beyond the club's means rule a player out.
    if (overall >= static_cast<double>(need.min_overall) &&
        aiAffordsWage(club->get(), player.getWage(), wage_room))
      free_agents.emplace_back(overall, player.getId());
  }
  // The best affordable players are sounded out.
  std::ranges::sort(free_agents, std::greater<>{});
  std::size_t checks = 0;
  for (const auto& [overall, player_id] : free_agents)
  {
    if (checks == AI_FREE_AGENT_CHECKS) break;
    const PlayerContext context =
        playerContext(player_id, club_id, ContractKind::FreeAgent);
    const ContractOffer offer = demandedOffer(contractDemand(context));
    if (!aiAffordsWage(club->get(), offer.weekly_wage, wage_room) ||
        static_cast<std::int64_t>(offer.signing_bonus) +
                static_cast<std::int64_t>(offer.weekly_wage) *
                    TransferTuning::Offer::FREE_AGENT_FEE_WEEKS >
            cash)
      continue;
    ++checks;
    if (!evaluateContract(context, offer, 0).accepted) continue;
    Deal deal;
    deal.player_id = player_id;
    deal.buyer_id = club_id;
    deal.kind = TransferKind::Free;
    deal.contract = offer;
    if (completeTransfer(deal, date, managed_team_id)) return true;
  }
  return false;
}

bool TransferMarket::aiTakeLoan(TeamID club_id, const AiNeed& need,
                                std::int64_t wage_room,
                                const GameDateValue& date,
                                TeamID managed_team_id, WorldRng& rng)
{
  using L = TransferTuning::Loan;
  const auto club = gamedata->getTeam(club_id);
  const StatsConfig& config = gamedata->getStatsConfig();
  std::optional<PlayerID> best;
  auto best_overall = static_cast<double>(
      need.min_overall - TransferTuning::Market::LOAN_LEVEL_SLACK);
  for (const auto& [player_id, extras] : player_flags)
  {
    if (!extras.loan_listed) continue;
    const auto player = gamedata->getPlayer(player_id);
    if (!player) continue;
    const Player& p = player->get();
    const TeamID parent = p.getTeamId();
    // Cheap checks before the overall (a dozen map lookups per player).
    if (parent == club_id || parent == FREE_AGENTS_TEAM_ID ||
        positionGroup(p.getRole()) != need.group)
      continue;
    const double overall = p.getOverall(config);
    if (overall < best_overall ||
        (best.has_value() && overall == best_overall && player_id > *best) ||
        !canBeTraded(player_id) ||
        (parent != managed_team_id && !keepsAiKeepers(parent, player_id)))
      continue;
    const auto parent_team = gamedata->getTeam(parent);
    const int gap = parent_team ? parent_team->get().getReputation() -
                                      club->get().getReputation()
                                : 0;
    if (std::vector<Reason> reasons;
        !playerAcceptsLoan(world.squadRole(player_id),
                           projectedRole(player_id, club_id),
                           p.getTraits().ambition, gap, reasons) ||
        !loanWithinLimits(player_id, parent, club_id))
      continue;
    best = player_id;
    best_overall = overall;
  }
  if (!best.has_value()) return false;
  const Player& loanee = gamedata->getPlayer(*best)->get();
  const TeamID parent = loanee.getTeamId();
  if (parent == managed_team_id)
  {
    // The managed club decides on its own players: the borrower opens with
    // what its wage room allows and negotiates from there.
    if (const bool pending = std::ranges::any_of(
            incoming, [&](const IncomingOffer& offer)
            { return offer.player_id == *best && offer.buyer == club_id; });
        !pending && !talksClosed(*best, club_id, date))
    {
      IncomingOffer offer;
      offer.player_id = *best;
      offer.buyer = club_id;
      offer.loan = true;
      const LoanDuration duration =
          rng.chance(0.3) ? LoanDuration::SixMonths : LoanDuration::SeasonEnd;
      const double option_roll = rng.uniform01();
      const double ceiling_roll = rng.uniform01();
      const double patience_roll = rng.uniform01();
      const LoanNegotiation::BorrowerContext context =
          borrowerContext(*best, club_id, date);
      offer.loan_terms =
          LoanNegotiation::openingOffer(context, duration, L::LISTED_WAGE_SHARE,
                                        loanee.getAge(), option_roll);
      if (offer.loan_terms.wage_share < MIN_LOAN_WAGE_SHARE) return false;
      offer.max_fee =
          LoanNegotiation::ceilingFor(context, offer.loan_terms, ceiling_roll);
      offer.patience = BuyerNegotiation::drawPatience(
          patience_roll, clubWindow(club_id, date).days_to_deadline);
      offer.created = date;
      offer.expires = answerDeadline(
          club_id, date, TransferTuning::Offer::INCOMING_OFFER_DAYS);
      addIncomingOffer(offer);
      post(date, InboxCategory::Transfer, "INBOX_LOAN_OFFER_TITLE",
           "INBOX_LOAN_OFFER_BODY",
           {loanee.getName(), club->get().getName(),
            std::to_string(offer.loan_terms.wage_share)},
           *best, club_id);
    }
    return false;
  }
  LoanTerms terms;
  terms.duration =
      rng.chance(0.25) ? LoanDuration::SixMonths : LoanDuration::SeasonEnd;
  const auto parent_team = gamedata->getTeam(parent);
  if (const int gap = parent_team ? parent_team->get().getReputation() -
                                        club->get().getReputation()
                                  : 0;
      gap < 0)
    terms.wage_share = static_cast<std::uint8_t>(
        std::min(100, L::LISTED_WAGE_SHARE + BIGGER_BORROWER_EXTRA_SHARE));
  else
    terms.wage_share = gap >= L::SMALL_BORROWER_REPUTATION_GAP
                           ? L::SMALL_BORROWER_WAGE_SHARE
                           : L::LISTED_WAGE_SHARE;
  if (!aiAffordsWage(club->get(),
                     static_cast<std::uint32_t>(
                         static_cast<std::uint64_t>(loanee.getWage()) *
                         terms.wage_share / 100U),
                     wage_room))
    return false;
  return startLoan(*best, club_id, terms, date, managed_team_id);
}

bool TransferMarket::aiBuy(
    TeamID club_id, const AiNeed& need, std::int64_t wage_room,
    const std::unordered_map<PlayerID, TransferListing>& listings,
    const GameDateValue& date, TeamID managed_team_id, WorldRng& rng)
{
  using M = TransferTuning::Market;
  const std::int64_t budget = spendableBudget(club_id, date);
  if (budget <= 0) return false;
  const StatsConfig& config = gamedata->getStatsConfig();
  const auto eligible = [&](const Player& player)
  {
    const TeamID owner = player.getTeamId();
    return owner != club_id && owner != managed_team_id &&
           owner != FREE_AGENTS_TEAM_ID &&
           positionGroup(player.getRole()) == need.group &&
           canBeTraded(player.getId()) &&
           keepsAiKeepers(owner, player.getId()) &&
           player.getOverall(config) >= static_cast<double>(need.min_overall);
  };

  // Listed players plus a sample of other clubs' players.
  std::vector<std::pair<double, PlayerID>> candidates;
  for (const auto& [player_id, listing] : listings)
  {
    const auto player = gamedata->getPlayer(player_id);
    if (player && listing.seller_team_id == player->get().getTeamId() &&
        eligible(player->get()))
      candidates.emplace_back(player->get().getOverall(config), player_id);
  }
  const auto& everyone = gamedata->getPlayersVector();
  for (std::size_t sample = 0; sample < M::AI_BUY_SAMPLE && !everyone.empty();
       ++sample)
  {
    const Player& player =
        everyone[static_cast<std::size_t>(
                     rng.uniformInt(0, static_cast<int>(everyone.size()) - 1))]
            .get();
    if (eligible(player) && !listings.contains(player.getId()))
      candidates.emplace_back(player.getOverall(config), player.getId());
  }
  // Cheapest route to the level first: the weakest player that fixes the
  // need is the most affordable.
  std::ranges::sort(candidates);
  candidates.erase(std::ranges::unique(candidates).begin(), candidates.end());
  std::size_t tried = 0;
  for (const auto& [overall, player_id] : candidates)
  {
    if (tried++ == M::AI_BUY_ATTEMPTS) break;
    const auto listing = listings.find(player_id);
    const std::uint32_t listing_price =
        listing == listings.end() ? 0 : listing->second.asking_price;
    const SaleContext sale =
        saleContext(player_id, club_id, date, listing_price);
    const Valuation valuation = valueForSale(sale);
    // A release clause at or below the asking price is paid in cash, and
    // the selling club has no say.
    const bool clause =
        sale.release_clause > 0 && valuation.asking_fee >= sale.release_clause;
    if ((valuation.not_for_sale && !clause) ||
        static_cast<double>(valuation.asking_fee) >
            static_cast<double>(sale.market_value) * M::AI_MAX_VALUE_MULTIPLE)
      continue;
    Deal deal;
    deal.player_id = player_id;
    deal.buyer_id = club_id;
    deal.kind = TransferKind::Permanent;
    // Big fees are spread over instalments, raised so the seller's
    // valuation still meets his price; the club must fund the upfront part
    // from its spendable budget, and the instalments then weigh on it.
    deal.terms = aiBidFor(valuation.asking_fee,
                          gamedata->getPlayer(player_id)->get().getAge());
    if (clause)
    {
      deal.terms = OfferTerms{};
      deal.terms.fee = sale.release_clause;
      deal.binding = true;
    }
    const PlayerContext context =
        playerContext(player_id, club_id, ContractKind::Transfer);
    deal.contract = demandedOffer(contractDemand(context));
    if (const std::int64_t cash =
            static_cast<std::int64_t>(upfrontAmount(deal.terms)) +
            static_cast<std::int64_t>(agentFee(deal));
        cash > budget ||
        !aiAffordsWage(gamedata->getTeam(club_id)->get(),
                       deal.contract.weekly_wage, wage_room) ||
        !evaluateContract(context, deal.contract, 0).accepted ||
        evaluateOffer(sale, deal.terms, 0).decision !=
            ClubResponse::Decision::Accept)
      continue;
    return completeTransfer(deal, date, managed_team_id);
  }
  return false;
}

bool TransferMarket::runAiClub(
    TeamID club_id,
    const std::unordered_map<PlayerID, TransferListing>& listings,
    const GameDateValue& date, TeamID managed_team_id, WorldRng& rng,
    bool free_agents_only)
{
  using M = TransferTuning::Market;
  const auto club = gamedata->getTeam(club_id);
  if (!club || club_id == FREE_AGENTS_TEAM_ID || club_id == managed_team_id)
    return false;
  const auto ranked = rankedSquad(*gamedata, club->get());
  if (!free_agents_only &&
      aiShedSurplus(club_id, ranked, date, managed_team_id, rng))
    return true;
  // In the summer after promotion a club measures itself against the
  // division it has joined.
  const WindowInfo window = clubWindow(club_id, date);
  const bool promoted =
      window.open && !window.winter && lastSeasonMove(club_id, true);
  const float boost = promoted ? M::PROMOTED_LEVEL_BOOST : 0.0f;
  const auto need = assessNeed(ranked, boost);
  if (!need) return false;
  // A club running out of cash adds wages only to fill a missing position.
  if (const std::int64_t payroll = weeklyPayroll(*gamedata, club->get());
      (need->upgrade || need->depth) &&
      club->get().getFinances().getBalance() <
          M::UPGRADE_CASH_RESERVE_WEEKS * payroll)
    return false;
  const std::int64_t wage_room = aiWageRoom(club_id, false);
  if (aiSignFreeAgent(club_id, *need, wage_room, date, managed_team_id))
    return true;
  if (free_agents_only) return false;
  // Most clubs look for a loan before paying a fee; squad cover is never
  // worth one. [P]
  if ((need->depth || rng.chance(M::LOAN_BEFORE_FEE_CHANCE)) &&
      aiTakeLoan(club_id, *need, wage_room, date, managed_team_id, rng))
    return true;
  return !need->depth && rng.chance(M::FEE_ROUTE_CHANCE) &&
         aiBuy(club_id, *need, wage_room, listings, date, managed_team_id, rng);
}

void TransferMarket::runAiPreContracts(const GameDateValue& date,
                                       TeamID managed_team_id, WorldRng& rng,
                                       int attempts)
{
  using M = TransferTuning::Market;
  if (!canSignPreContract(1, date) || attempts <= 0) return;
  const StatsConfig& config = gamedata->getStatsConfig();
  std::vector<PlayerID> candidates;
  for (const auto& reference : gamedata->getPlayersVector())
  {
    const Player& player = reference.get();
    if (player.getContractYears() == 1 &&
        player.getTeamId() != FREE_AGENTS_TEAM_ID &&
        player.getAge() <= PRE_CONTRACT_MAX_AGE &&
        !pre_contracts.contains(player.getId()))
      candidates.push_back(player.getId());
  }
  std::vector<TeamID> clubs;
  for (const auto& team : gamedata->getTeamsVector())
  {
    const TeamID id = team.get().getId();
    if (id != FREE_AGENTS_TEAM_ID && id != managed_team_id) clubs.push_back(id);
  }
  if (candidates.empty() || clubs.empty()) return;
  std::ranges::sort(candidates);
  std::ranges::sort(clubs);

  for (int attempt = 0; attempt < attempts; ++attempt)
  {
    if (!rng.chance(PRE_CONTRACT_SUCCESS)) continue;
    const TeamID club_id = clubs[static_cast<std::size_t>(
        rng.uniformInt(0, static_cast<int>(clubs.size()) - 1))];
    const auto club = gamedata->getTeam(club_id);
    if (!club) continue;
    // The club pays the wage from next season's budget and the bonus and
    // agent fee from its spendable money, like any other signing.
    const std::int64_t wage_room = aiWageRoom(club_id, true);
    const std::int64_t budget =
        spendableBudget(club_id, date) - preContractCosts(club_id);
    if (wage_room <= 0 || budget <= 0) continue;
    const float team_level = squadLevel(rankedSquad(*gamedata, club->get()));
    std::optional<PlayerID> best;
    ContractOffer best_offer;
    double best_overall = 0.0;
    for (std::size_t sample = 0; sample < PRE_CONTRACT_SAMPLE; ++sample)
    {
      const PlayerID player_id = candidates[static_cast<std::size_t>(
          rng.uniformInt(0, static_cast<int>(candidates.size()) - 1))];
      const auto player = gamedata->getPlayer(player_id);
      if (!player || player->get().getTeamId() == club_id ||
          pre_contracts.contains(player_id))
        continue;
      const double overall = player->get().getOverall(config);
      if (overall <
              static_cast<double>(team_level - PRE_CONTRACT_LEVEL_MARGIN) ||
          overall > static_cast<double>(team_level +
                                        M::PRE_CONTRACT_MAX_ABOVE_LEVEL) ||
          overall <= best_overall ||
          !aiAffordsWage(club->get(), player->get().getWage(), wage_room))
        continue;
      const PlayerContext context =
          playerContext(player_id, club_id, ContractKind::PreContract);
      const ContractOffer offer = demandedOffer(contractDemand(context));
      if (!aiAffordsWage(club->get(), offer.weekly_wage, wage_room) ||
          static_cast<std::int64_t>(offer.signing_bonus) +
                  static_cast<std::int64_t>(offer.weekly_wage) *
                      TransferTuning::Offer::FREE_AGENT_FEE_WEEKS >
              budget ||
          !evaluateContract(context, offer, 0).accepted)
        continue;
      best = player_id;
      best_offer = offer;
      best_overall = overall;
    }
    if (best.has_value())
      agreePreContract(*best, club_id, best_offer, date, managed_team_id);
  }
}

void TransferMarket::runAiApproach(TeamID club_id, const GameDateValue& date,
                                   TeamID managed_team_id, WorldRng& rng)
{
  using Buyer = TransferTuning::Buyer;
  // Clubs scrambling before the deadline knock on more doors.
  if (const double chance =
          static_cast<double>(TransferTuning::Market::MANAGED_APPROACH_CHANCE) *
          (BuyerNegotiation::deadlinePressure(
               clubWindow(club_id, date).days_to_deadline)
               ? static_cast<double>(Buyer::DEADLINE_APPROACH_MULTIPLIER)
               : 1.0);
      managed_team_id == FREE_AGENTS_TEAM_ID || club_id == managed_team_id ||
      !rng.chance(chance))
    return;
  const auto club = gamedata->getTeam(club_id);
  if (!club) return;
  const std::int64_t wage_room = aiWageRoom(club_id, false);
  // The wage he would ask of this club, and whether it can carry it.
  const auto demanded_wage = [&](PlayerID player_id)
  {
    return demandedOffer(contractDemand(playerContext(player_id, club_id,
                                                      ContractKind::Transfer)))
        .weekly_wage;
  };
  const auto need = assessNeed(rankedSquad(*gamedata, club->get()));
  if (!need || need->depth) return;
  const StatsConfig& config = gamedata->getStatsConfig();
  std::optional<PlayerID> best;
  auto best_overall = static_cast<double>(need->min_overall);
  for (const auto& reference : gamedata->getPlayersForTeam(managed_team_id))
  {
    const Player& player = reference.get();
    const PlayerID player_id = player.getId();
    const double overall = player.getOverall(config);
    if (positionGroup(player.getRole()) != need->group ||
        overall < best_overall ||
        (best.has_value() && overall == best_overall && player_id > *best) ||
        player.getTransferStatus() == TransferStatus::Listed ||
        !canBeTraded(player_id) || isNotForSale(player_id, date) ||
        talksClosed(player_id, club_id, date) ||
        std::ranges::any_of(
            incoming,
            [&](const IncomingOffer& offer)
            {
              return offer.player_id == player_id &&
                     (offer.buyer == club_id || offer.loan);
            }) ||
        !wouldJoin(player_id, club_id, ContractKind::Transfer) ||
        !aiAffordsWage(club->get(), demanded_wage(player_id), wage_room))
      continue;
    best = player_id;
    best_overall = overall;
  }
  if (!best.has_value()) return;
  // A second club joins the race for a player only some of the time.
  IncomingOffer offer;
  offer.player_id = *best;
  offer.buyer = club_id;
  const std::uint8_t rivals = rivalBids(offer);
  if (rivals > 0 && !rng.chance(Buyer::RIVAL_APPROACH_CHANCE)) return;
  const SaleContext sale = saleContext(*best, club_id, date, 0);
  const Valuation valuation = valueForSale(sale);
  const std::int64_t budget = spendableBudget(club_id, date);
  const double willing = static_cast<double>(valuation.asking_fee) *
                         static_cast<double>(rng.uniform(1.0f, 1.15f));
  // A release clause within reach is simply paid: the club cannot refuse.
  if (sale.release_clause > 0 &&
      static_cast<double>(sale.release_clause) <= willing &&
      payReleaseClause(club_id, *best, date, managed_team_id))
    return;
  // A signing who takes most of its wage room leaves less for the fee.
  const double wage_share = wage_room > 0
                                ? static_cast<double>(demanded_wage(*best)) /
                                      static_cast<double>(wage_room)
                                : 1.0;
  const double wage_cut =
      static_cast<double>(Buyer::WAGE_CEILING_CUT) *
      std::clamp(
          (wage_share - static_cast<double>(Buyer::WAGE_PRESSURE_SHARE)) /
              (1.0 - static_cast<double>(Buyer::WAGE_PRESSURE_SHARE)),
          0.0, 1.0);
  const double ceiling =
      std::min(static_cast<double>(budget), willing * (1.0 - wage_cut));
  if (ceiling < static_cast<double>(valuation.asking_fee) *
                    static_cast<double>(TransferTuning::Offer::REJECT_SHARE))
    return;
  const WindowInfo window = clubWindow(club_id, date);
  const int age = gamedata->getPlayer(*best)->get().getAge();
  offer.max_fee = static_cast<std::uint32_t>(ceiling);
  const double share_roll = rng.uniform01();
  const double add_on_roll = rng.uniform01();
  offer.terms = BuyerNegotiation::openingBid(offer.max_fee, age, rivals,
                                             share_roll, add_on_roll);
  offer.patience =
      BuyerNegotiation::drawPatience(rng.uniform01(), window.days_to_deadline);
  offer.created = date;
  offer.expires =
      answerDeadline(club_id, date, TransferTuning::Offer::INCOMING_OFFER_DAYS);
  addIncomingOffer(offer);
  world.onTransferBid(date, *best, club_id, offer.terms.fee, managed_team_id);
}

bool TransferMarket::keepsSquadFloor(TeamID club_id, PlayerID leaving,
                                     bool* goalkeeper) const
{
  const auto club = gamedata->getTeam(club_id);
  if (!club) return true;
  std::size_t seniors = 0;
  std::size_t keepers = 0;
  bool leaving_keeper = false;
  for (const PlayerID player_id : club->get().getPlayerIDs())
  {
    const auto player = gamedata->getPlayer(player_id);
    if (!player || player->get().isAcademyPlayer()) continue;
    const bool keeper = player->get().getRole() == PlayerRole::GK;
    if (player_id == leaving)
    {
      leaving_keeper = keeper;
      continue;
    }
    ++seniors;
    if (keeper) ++keepers;
  }
  const bool no_keeper = leaving_keeper && keepers == 0;
  if (goalkeeper) *goalkeeper = no_keeper;
  return seniors >= MIN_SENIOR_SQUAD && !no_keeper;
}

bool TransferMarket::keepsAiKeepers(TeamID club_id, PlayerID leaving) const
{
  const auto player = gamedata->getPlayer(leaving);
  const auto club = gamedata->getTeam(club_id);
  if (!player || !club || club_id == FREE_AGENTS_TEAM_ID ||
      player->get().getRole() != PlayerRole::GK ||
      player->get().isAcademyPlayer())
    return true;
  int keepers = 0;
  for (const PlayerID player_id : club->get().getPlayerIDs())
  {
    const auto other = gamedata->getPlayer(player_id);
    if (player_id != leaving && other && !other->get().isAcademyPlayer() &&
        other->get().getRole() == PlayerRole::GK)
      ++keepers;
  }
  return keepers >= TransferTuning::Market::MIN_AI_SENIOR_KEEPERS;
}

std::size_t TransferMarket::seniorSquadSize(TeamID club_id) const
{
  const auto club = gamedata->getTeam(club_id);
  if (!club) return 0;
  std::size_t count = 0;
  for (const PlayerID player_id : club->get().getPlayerIDs())
    if (const auto player = gamedata->getPlayer(player_id);
        player && !player->get().isAcademyPlayer())
      ++count;
  for (const auto& [player_id, deal] : pre_contracts)
    if (deal.to_team == club_id) ++count;
  return count;
}

std::uint32_t TransferMarket::deservedWage(PlayerID player_id,
                                           TeamID club_id) const
{
  const auto player = gamedata->getPlayer(player_id);
  const auto club = gamedata->getTeam(club_id);
  if (!player || !club || club_id == FREE_AGENTS_TEAM_ID) return 0;
  // The club's pay scale: what its senior players earn per unit of index.
  const StatsConfig& config = gamedata->getStatsConfig();
  double wages = 0.0;
  double indices = 0.0;
  for (const PlayerID id : club->get().getPlayerIDs())
  {
    const auto other = gamedata->getPlayer(id);
    if (!other || other->get().isAcademyPlayer() || id == player_id) continue;
    const LoanDeal* loan = findLoan(id);
    wages +=
        static_cast<double>(loan ? loan->full_wage : other->get().getWage());
    indices += ClubEconomy::wageIndex(other->get().getOverall(config),
                                      other->get().getAge());
  }
  if (indices <= 0.0) return 0;
  const double wage = wages / indices *
                      ClubEconomy::wageIndex(player->get().getOverall(config),
                                             player->get().getAge());
  return static_cast<std::uint32_t>(std::clamp(
      wage, 0.0,
      static_cast<double>(std::numeric_limits<std::uint32_t>::max())));
}

bool TransferMarket::renewContract(PlayerID player_id,
                                   const ContractOffer& offer,
                                   const GameDateValue& date)
{
  Player* player = mutablePlayer(player_id);
  if (!player) return false;
  const TeamID club_id = player->getTeamId();
  const auto club = gamedata->getTeam(club_id);
  if (!club || club_id == FREE_AGENTS_TEAM_ID || !canBeTraded(player_id) ||
      offer.years <= player->getContractYears() ||
      offer.years > maxContractYears(player->getAge()) ||
      offer.weekly_wage == 0)
    return false;
  // The new contract replaces the old one's extras.
  std::erase_if(pending_obligations,
                [player_id](const TransferObligation& obligation)
                {
                  return obligation.player_id == player_id &&
                         (obligation.kind == ObligationKind::WageRise ||
                          obligation.kind == ObligationKind::AppearanceFee);
                });
  player->setWage(offer.weekly_wage);
  player->setContractYears(offer.years);
  addContractExtras(player_id, club_id, offer, date);
  PlayerMarketFlags& extras = mutableFlags(player_id);
  extras.release_clause = offer.release_clause;
  extras.promised_role = offer.promised_role;
  extras.promise_date = date;
  pruneFlags(player_id);
  Finances& finances = club->get().getFinances();
  if (offer.signing_bonus > 0)
    finances.record(date, FinanceCategory::Wages,
                    -static_cast<std::int64_t>(offer.signing_bonus));
  if (const std::uint32_t agent = offer.agent_fee.value_or(
          offer.weekly_wage * TransferTuning::Offer::FREE_AGENT_FEE_WEEKS);
      agent > 0)
    finances.record(date, FinanceCategory::TransferFeeOut,
                    -static_cast<std::int64_t>(agent));
  return true;
}

bool TransferMarket::payReleaseClause(TeamID club_id, PlayerID player_id,
                                      const GameDateValue& date,
                                      TeamID managed_team_id)
{
  const std::uint32_t clause = releaseClause(player_id);
  const auto club = gamedata->getTeam(club_id);
  const Player* player = mutablePlayer(player_id);
  if (clause == 0 || !club || !player || !canBeTraded(player_id)) return false;
  Deal deal;
  deal.player_id = player_id;
  deal.buyer_id = club_id;
  deal.kind = TransferKind::Permanent;
  deal.terms.fee = clause;
  deal.binding = true;  // A clause cannot be refused.
  deal.contract = demandedOffer(contractDemand(
      playerContext(player_id, club_id, ContractKind::Transfer)));
  if (const std::int64_t cash = static_cast<std::int64_t>(clause) +
                                static_cast<std::int64_t>(agentFee(deal));
      cash > spendableBudget(club_id, date) ||
      !aiAffordsWage(club->get(), deal.contract.weekly_wage,
                     aiWageRoom(club_id, false)))
    return false;
  const TeamID seller = player->getTeamId();
  if (seller == managed_team_id && !keepsSquadFloor(seller, player_id))
    return false;
  const std::string name = player->getName();
  if (!completeTransfer(deal, date, managed_team_id)) return false;
  if (seller == managed_team_id)
    post(date, InboxCategory::Transfer, "INBOX_RELEASE_CLAUSE_PAID_TITLE",
         "INBOX_RELEASE_CLAUSE_PAID_BODY",
         {name, club->get().getName(), formatMoney(clause)}, player_id,
         club_id);
  return true;
}

LoanNegotiation::BorrowerContext TransferMarket::borrowerContext(
    PlayerID player_id, TeamID club_id, const GameDateValue& date) const
{
  LoanNegotiation::BorrowerContext context;
  const auto player = gamedata->getPlayer(player_id);
  if (!player) return context;
  const Player& p = player->get();
  p.updateMarketValue(gamedata->getStatsConfig());
  context.market_value = p.getMarketValue();
  const LoanDeal* loan = findLoan(player_id);
  context.weekly_wage = loan ? loan->full_wage : p.getWage();
  context.cash = spendableBudget(club_id, date);
  context.wage_room = aiWageRoom(club_id, false);
  context.season_weeks = std::max(
      1, weeksBetween(date, loanEndDate(date, LoanDuration::SeasonEnd)));
  context.apps_per_week = appearanceRate(projectedRole(player_id, club_id));
  const WindowInfo window = clubWindow(club_id, date);
  context.days_to_deadline = window.open ? window.days_to_deadline : -1;
  return context;
}

void TransferMarket::seedReleaseClauses()
{
  using N = TransferTuning::Negotiation;
  const std::uint64_t seed = gamedata->getWorldSeed();
  const StatsConfig& config = gamedata->getStatsConfig();
  for (const auto& reference : gamedata->getPlayersVector())
  {
    const Player& player = reference.get();
    if (const auto club = gamedata->getTeam(player.getTeamId());
        !club || player.getTeamId() == FREE_AGENTS_TEAM_ID ||
        player.isAcademyPlayer() || player.getAge() > CLAUSE_MAX_AGE ||
        player.getContractYears() == 0 ||
        player.getTraits().ambition < N::RELEASE_CLAUSE_AMBITION ||
        club->get().getReputation() >= N::RELEASE_CLAUSE_CLUB_REPUTATION)
      continue;
    const PlayerID player_id = player.getId();
    if (WorldRng::hashUniform(seed, RngDomain::Transfers, player_id,
                              CLAUSE_SEED_KEY) >= CLAUSE_SEED_SHARE)
      continue;
    player.updateMarketValue(config);
    const double multiple =
        CLAUSE_MIN_MULTIPLE +
        CLAUSE_MULTIPLE_SPAN * WorldRng::hashUniform(seed, RngDomain::Transfers,
                                                     player_id,
                                                     CLAUSE_SIZE_KEY);
    const double clause =
        std::ceil(static_cast<double>(player.getMarketValue()) * multiple /
                  CLAUSE_ROUNDING) *
        CLAUSE_ROUNDING;
    if (clause <= 0.0) continue;
    mutableFlags(player_id).release_clause = static_cast<std::uint32_t>(
        std::min(clause, static_cast<double>(
                             std::numeric_limits<std::uint32_t>::max())));
  }
}

void TransferMarket::seedSummerFreeAgents()
{
  using M = TransferTuning::Market;
  constexpr std::uint64_t EXPIRY_KEY = 0xE7B1E5;
  const std::uint64_t seed = gamedata->getWorldSeed();
  std::vector<TeamID> clubs;
  for (const auto& team : gamedata->getTeamsVector())
    if (team.get().getId() != FREE_AGENTS_TEAM_ID)
      clubs.push_back(team.get().getId());
  std::ranges::sort(clubs);
  for (const TeamID club_id : clubs)
  {
    const auto ranked =
        rankedSquad(*gamedata, gamedata->getTeam(club_id)->get());
    for (std::size_t rank = M::OPENING_EXPIRY_MIN_RANK; rank < ranked.size();
         ++rank)
    {
      const PlayerID player_id = ranked[rank].second;
      Player* player = mutablePlayer(player_id);
      // Goalkeepers stay: a club's keeper cover includes youngsters who
      // may yet join its academy, and the pool rarely holds keepers.
      if (!player || player->getAge() < M::OPENING_EXPIRY_MIN_AGE ||
          player->getRole() == PlayerRole::GK ||
          WorldRng::hashUniform(seed, RngDomain::Transfers, player_id,
                                EXPIRY_KEY) >= M::OPENING_EXPIRY_SHARE)
        continue;
      // His contract has ended: flags, offers and loan listing go with it.
      clearOnMove(player_id);
      player_flags.erase(player_id);
      movePlayer(player_id, club_id, FREE_AGENTS_TEAM_ID, FREE_AGENTS_TEAM_ID);
      player->setContractYears(0);
    }
  }
}

std::uint32_t TransferMarket::releaseClause(PlayerID player_id) const
{
  const PlayerMarketFlags* extras = flags(player_id);
  return extras ? extras->release_clause : 0;
}

void TransferMarket::setReleaseClause(PlayerID player_id, std::uint32_t clause)
{
  mutableFlags(player_id).release_clause = clause;
  pruneFlags(player_id);
}

void TransferMarket::addContractExtras(PlayerID player_id, TeamID club_id,
                                       const ContractOffer& contract,
                                       const GameDateValue& start)
{
  // Extras belong to one contract: a new club's deal ends the old ones.
  const auto extra = [](ObligationKind kind)
  {
    return kind == ObligationKind::WageRise ||
           kind == ObligationKind::AppearanceFee;
  };
  std::erase_if(pending_obligations,
                [player_id, club_id, &extra,
                 &contract](const TransferObligation& obligation)
                {
                  return obligation.player_id == player_id &&
                         extra(obligation.kind) &&
                         (obligation.payer != club_id ||
                          (obligation.kind == ObligationKind::WageRise &&
                           contract.yearly_rise > 0) ||
                          (obligation.kind == ObligationKind::AppearanceFee &&
                           contract.appearance_bonus > 0));
                });
  if (contract.yearly_rise > 0 && contract.years > 1)
  {
    TransferObligation rise;
    rise.id = next_id++;
    rise.kind = ObligationKind::WageRise;
    rise.player_id = player_id;
    rise.payer = club_id;
    rise.payee = FREE_AGENTS_TEAM_ID;
    rise.amount = contract.yearly_rise;
    rise.due = start + static_cast<std::size_t>(DAYS_PER_YEAR);
    pending_obligations.push_back(rise);
  }
  if (contract.appearance_bonus > 0)
  {
    TransferObligation bonus;
    bonus.id = next_id++;
    bonus.kind = ObligationKind::AppearanceFee;
    bonus.player_id = player_id;
    bonus.payer = club_id;
    bonus.payee = FREE_AGENTS_TEAM_ID;
    bonus.amount = contract.appearance_bonus;
    bonus.due = start;
    bonus.baseline =
        careerCount(player_id, club_id, ObligationKind::AppearanceBonus);
    pending_obligations.push_back(bonus);
  }
}

void TransferMarket::applyWageRises(const GameDateValue& date)
{
  const auto raise = [](std::uint32_t wage, std::int64_t percent)
  {
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(
        static_cast<std::uint64_t>(wage) *
            static_cast<std::uint64_t>(100 + percent) / 100U,
        std::numeric_limits<std::uint32_t>::max()));
  };
  for (auto it = pending_obligations.begin(); it != pending_obligations.end();)
  {
    TransferObligation& obligation = *it;
    if (obligation.kind != ObligationKind::WageRise || date < obligation.due)
    {
      ++it;
      continue;
    }
    Player* player = mutablePlayer(obligation.player_id);
    const auto loan = active_loans.find(obligation.player_id);
    const bool lent =
        loan != active_loans.end() && loan->second.parent == obligation.payer;
    // The rise lapses with the contract or when he has left the club.
    if (!player || player->getContractYears() == 0 ||
        (player->getTeamId() != obligation.payer && !lent))
    {
      it = pending_obligations.erase(it);
      continue;
    }
    if (lent)
    {
      LoanDeal& deal = loan->second;
      deal.full_wage = raise(deal.full_wage, obligation.amount);
      player->setWage(static_cast<std::uint32_t>(
          static_cast<std::uint64_t>(deal.full_wage) * deal.wage_share / 100U));
    }
    else
    {
      player->setWage(raise(player->getWage(), obligation.amount));
    }
    obligation.due = obligation.due + static_cast<std::size_t>(DAYS_PER_YEAR);
    ++it;
  }
}

void TransferMarket::payAppearanceFees(const GameDateValue& date)
{
  for (auto it = pending_obligations.begin(); it != pending_obligations.end();)
  {
    TransferObligation& obligation = *it;
    if (obligation.kind != ObligationKind::AppearanceFee)
    {
      ++it;
      continue;
    }
    const auto player = gamedata->getPlayer(obligation.player_id);
    if (player && player->get().getTeamId() != obligation.payer)
    {
      // Still to join (pre-contract) or away on loan: it waits; gone: it
      // lapses.
      const PreContractDeal* agreed = findPreContract(obligation.player_id);
      const LoanDeal* loan = findLoan(obligation.player_id);
      const bool waits = (agreed && agreed->to_team == obligation.payer) ||
                         (loan && loan->parent == obligation.payer);
      it = waits ? std::next(it) : pending_obligations.erase(it);
      continue;
    }
    if (!player)
    {
      it = pending_obligations.erase(it);
      continue;
    }
    if (const std::uint16_t count =
            careerCount(obligation.player_id, obligation.payer,
                        ObligationKind::AppearanceBonus);
        count > obligation.baseline)
    {
      const std::int64_t owed =
          static_cast<std::int64_t>(count - obligation.baseline) *
          obligation.amount;
      if (auto club = gamedata->getTeam(obligation.payer))
        club->get().getFinances().record(date, FinanceCategory::Wages, -owed);
      obligation.baseline = count;
    }
    ++it;
  }
}

void TransferMarket::settleLoanClause(const LoanDeal& loan,
                                      const GameDateValue& date,
                                      TeamID managed_team_id, bool charge)
{
  const auto found = std::ranges::find_if(
      pending_obligations,
      [&](const TransferObligation& obligation)
      {
        return obligation.kind == ObligationKind::LoanUnplayedFee &&
               obligation.player_id == loan.player_id &&
               obligation.payer == loan.borrower;
      });
  if (found == pending_obligations.end()) return;
  const TransferObligation clause = *found;
  pending_obligations.erase(found);
  if (!charge) return;
  const std::uint16_t played = careerCount(loan.player_id, loan.borrower,
                                           ObligationKind::AppearanceBonus);
  const int games = std::max(0, played - static_cast<int>(clause.baseline));
  if (games >= clause.target) return;
  pay(loan.borrower, loan.parent, clause.amount, date);
  if (loan.parent != managed_team_id && loan.borrower != managed_team_id)
    return;
  const auto player = gamedata->getPlayer(loan.player_id);
  post(date, InboxCategory::Finance, "INBOX_LOAN_UNPLAYED_FEE_TITLE",
       "INBOX_LOAN_UNPLAYED_FEE_BODY",
       {player ? player->get().getName() : std::string(),
        teamName(loan.borrower), std::to_string(games),
        std::to_string(clause.target), formatMoney(clause.amount),
        teamName(loan.parent)},
       loan.player_id,
       loan.parent == managed_team_id ? loan.borrower : loan.parent);
}

void TransferMarket::postDeadlineSummary(const GameDateValue& date,
                                         TeamID managed_team_id)
{
  if (managed_team_id == FREE_AGENTS_TEAM_ID) return;
  if (const LeagueID league = leagueOf(*gamedata, managed_team_id);
      TransferWindows::isOpen(league, date) ||
      !TransferWindows::isOpen(league, date - 1))
    return;
  // The window's last days: the deadline day and the ones before it.
  const std::int32_t last = dayOrdinal(date) - 1;
  const std::int32_t first = last - TransferTuning::Buyer::DEADLINE_DAYS;
  int fee_moves = 0;
  int loan_moves = 0;
  int other_moves = 0;
  std::int64_t spent = 0;
  std::vector<const TransferRecord*> biggest;
  std::vector<const TransferRecord*> ours;
  using enum TransferKind;
  for (auto it = records.rbegin(); it != records.rend(); ++it)
  {
    const std::int32_t day = dayOrdinal(it->date);
    if (day < first) break;
    if (day > last) continue;
    switch (it->kind)
    {
      case Permanent:
        ++fee_moves;
        spent += it->fee;
        biggest.push_back(std::to_address(it));
        break;
      case Loan:
        ++loan_moves;
        break;
      case Free:
      case PreContract:
        ++other_moves;
        break;
      default:
        continue;
    }
    if (it->from_team == managed_team_id || it->to_team == managed_team_id)
      ours.push_back(std::to_address(it));
  }
  const int total = fee_moves + loan_moves + other_moves;
  if (total == 0) return;
  std::ranges::sort(biggest,
                    [](const TransferRecord* a, const TransferRecord* b)
                    { return a->fee > b->fee; });
  const auto describe = [&](const std::vector<const TransferRecord*>& deals)
  {
    std::string lines;
    std::size_t listed = 0;
    for (const TransferRecord* deal : deals)
    {
      if (listed == TransferTuning::Market::NEWS_DIGEST_LIMIT) break;
      const auto player = gamedata->getPlayer(deal->player_id);
      if (!player) continue;
      if (!lines.empty()) lines += '\n';
      lines += player->get().getName() + ": " + teamName(deal->from_team) +
               " -> " + teamName(deal->to_team);
      if (deal->fee > 0) lines += " (" + formatMoney(deal->fee) + ")";
      ++listed;
    }
    return lines;
  };
  const std::string top = describe(biggest);
  const std::string own = describe(ours);
  post(date, InboxCategory::Transfer, "INBOX_DEADLINE_SUMMARY_TITLE",
       "INBOX_DEADLINE_SUMMARY_BODY",
       {std::to_string(total), std::to_string(fee_moves),
        std::to_string(loan_moves), formatMoney(spent),
        top.empty() ? std::string("@INBOX_TRANSFER_NEWS_NONE") : top,
        own.empty() ? std::string("@INBOX_DEADLINE_SUMMARY_NONE_OURS") : own},
       std::nullopt, std::nullopt);
}

// ---------------------------------------------------------------------------
// Offers and flags
// ---------------------------------------------------------------------------

const IncomingOffer* TransferMarket::findIncomingOffer(
    std::uint32_t offer_id) const
{
  const auto found = std::ranges::find(incoming, offer_id, &IncomingOffer::id);
  return found == incoming.end() ? nullptr : std::to_address(found);
}

std::uint32_t TransferMarket::addIncomingOffer(IncomingOffer offer)
{
  offer.id = next_id++;
  if (offer.history.empty())
    offer.history.emplace_back(offer.created, BuyerNegotiation::Move::Bid,
                               offer.terms, offer.loan_terms);
  if (offer.patience == 0)
    offer.patience = TransferTuning::Buyer::DEFAULT_PATIENCE;
  incoming.push_back(std::move(offer));
  return incoming.back().id;
}

bool TransferMarket::updateIncomingOffer(const IncomingOffer& offer)
{
  const auto found = std::ranges::find(incoming, offer.id, &IncomingOffer::id);
  if (found == incoming.end()) return false;
  *found = offer;
  return true;
}

bool TransferMarket::removeIncomingOffer(std::uint32_t offer_id)
{
  return std::erase_if(incoming, [offer_id](const IncomingOffer& offer)
                       { return offer.id == offer_id; }) > 0;
}

std::vector<std::uint32_t> TransferMarket::dueOfferReplies(
    const GameDateValue& date) const
{
  std::vector<std::uint32_t> due;
  for (const IncomingOffer& offer : incoming)
  {
    if (offer.status == OfferStatus::AwaitingBuyer &&
        !(date < offer.respond_on))
      due.push_back(offer.id);
  }
  std::ranges::sort(due);
  return due;
}

std::uint8_t TransferMarket::rivalBids(const IncomingOffer& offer) const
{
  const auto count = std::ranges::count_if(
      incoming,
      [&](const IncomingOffer& other)
      {
        return !other.loan && other.player_id == offer.player_id &&
               other.buyer != offer.buyer;
      });
  return static_cast<std::uint8_t>(std::min<std::ptrdiff_t>(
      count, std::numeric_limits<std::uint8_t>::max()));
}

void TransferMarket::closeTalks(PlayerID player_id, TeamID buyer,
                                const GameDateValue& date)
{
  const WindowInfo window = clubWindow(buyer, date);
  const int days = window.open
                       ? std::min(window.days_to_deadline,
                                  TransferTuning::Buyer::TALKS_COOLDOWN_DAYS)
                       : 0;
  const GameDateValue until =
      date + static_cast<std::size_t>(std::max(days, 0));
  const auto found = std::ranges::find_if(
      cooldowns, [&](const TalksCooldown& cooldown)
      { return cooldown.player_id == player_id && cooldown.buyer == buyer; });
  if (found == cooldowns.end())
    cooldowns.push_back({player_id, buyer, until});
  else if (found->until < until)
    found->until = until;
}

bool TransferMarket::talksClosed(PlayerID player_id, TeamID buyer,
                                 const GameDateValue& date) const
{
  return std::ranges::any_of(cooldowns,
                             [&](const TalksCooldown& cooldown)
                             {
                               return cooldown.player_id == player_id &&
                                      cooldown.buyer == buyer &&
                                      !(cooldown.until < date);
                             });
}

void TransferMarket::forgetRemovedPlayers()
{
  std::vector<PlayerID> gone;
  const auto missing = [&](PlayerID player_id)
  {
    if (gamedata->getPlayer(player_id) ||
        std::ranges::contains(gone, player_id))
      return;
    gone.push_back(player_id);
  };
  for (const IncomingOffer& offer : incoming) missing(offer.player_id);
  for (const auto& [player_id, talk] : negotiations) missing(player_id);
  for (const TalksCooldown& cooldown : cooldowns) missing(cooldown.player_id);
  for (const auto& [player_id, extras] : player_flags) missing(player_id);
  for (const PlayerID player_id : gone)
  {
    clearOnMove(player_id);
    player_flags.erase(player_id);
  }
}

GameDateValue TransferMarket::answerDeadline(TeamID buyer,
                                             const GameDateValue& date,
                                             int days) const
{
  const WindowInfo window = clubWindow(buyer, date);
  const int allowed =
      window.open ? std::min(days, window.days_to_deadline) : days;
  return date + static_cast<std::size_t>(std::max(allowed, 0));
}

WindowInfo TransferMarket::clubWindow(TeamID club,
                                      const GameDateValue& date) const
{
  return windowInfo(leagueOf(*gamedata, club), date);
}

const Negotiation* TransferMarket::findNegotiation(PlayerID player_id) const
{
  const auto found = negotiations.find(player_id);
  return found == negotiations.end() ? nullptr : &found->second;
}

void TransferMarket::setNegotiation(const Negotiation& negotiation)
{
  negotiations[negotiation.player_id] = negotiation;
}

void TransferMarket::removeNegotiation(PlayerID player_id)
{
  negotiations.erase(player_id);
}

const PlayerMarketFlags* TransferMarket::flags(PlayerID player_id) const
{
  const auto found = player_flags.find(player_id);
  return found == player_flags.end() ? nullptr : &found->second;
}

void TransferMarket::setLoanListed(PlayerID player_id, bool listed)
{
  mutableFlags(player_id).loan_listed = listed;
  pruneFlags(player_id);
}

bool TransferMarket::isLoanListed(PlayerID player_id) const
{
  const PlayerMarketFlags* extras = flags(player_id);
  return extras && extras->loan_listed;
}

void TransferMarket::setNotForSale(PlayerID player_id,
                                   const GameDateValue& until)
{
  mutableFlags(player_id).not_for_sale_until = dayOrdinal(until);
}

bool TransferMarket::isNotForSale(PlayerID player_id,
                                  const GameDateValue& date) const
{
  const PlayerMarketFlags* extras = flags(player_id);
  return extras && extras->not_for_sale_until >= dayOrdinal(date);
}

const LoanDeal* TransferMarket::findLoan(PlayerID player_id) const
{
  const auto found = active_loans.find(player_id);
  return found == active_loans.end() ? nullptr : &found->second;
}

const PreContractDeal* TransferMarket::findPreContract(PlayerID player_id) const
{
  const auto found = pre_contracts.find(player_id);
  return found == pre_contracts.end() ? nullptr : &found->second;
}

std::vector<TransferRecord> TransferMarket::historyFor(PlayerID player_id) const
{
  std::vector<TransferRecord> moves;
  for (const TransferRecord& record : records)
  {
    if (record.player_id == player_id) moves.push_back(record);
  }
  return moves;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void TransferMarket::load(const std::shared_ptr<DatabaseConnection>& db_conn)
{
  TransferRepository repository(db_conn);
  records = repository.loadHistory();
  persisted_records = records.size();
  pending_obligations = repository.loadObligations();
  active_loans.clear();
  for (const LoanDeal& loan : repository.loadLoans())
    active_loans[loan.player_id] = loan;
  pre_contracts.clear();
  for (const PreContractDeal& deal : repository.loadPreContracts())
    pre_contracts[deal.player_id] = deal;
  player_flags = repository.loadFlags();
  incoming.clear();
  negotiations.clear();
  cooldowns.clear();
  repository.loadOffers(incoming, negotiations, cooldowns);
  next_id = 1;
  for (const TransferObligation& obligation : pending_obligations)
    next_id = std::max(next_id, obligation.id + 1);
  for (IncomingOffer& offer : incoming)
  {
    next_id = std::max(next_id, offer.id + 1);
    // Offers of saves from before the talks: their bid opens the history.
    if (offer.history.empty())
      offer.history.emplace_back(offer.created, BuyerNegotiation::Move::Bid,
                                 offer.terms, offer.loan_terms);
    if (offer.patience == 0)
      offer.patience = TransferTuning::Buyer::DEFAULT_PATIENCE;
  }
  {
    const std::scoped_lock lock(payables_cache_mutex);
    payables_dirty = true;
  }
}

void TransferMarket::save(
    const std::shared_ptr<DatabaseConnection>& db_conn) const
{
  TransferRepository repository(db_conn);
  repository.appendHistory(
      persisted_records,
      std::span<const TransferRecord>(records).subspan(persisted_records));
  repository.replaceObligations(pending_obligations);
  repository.replaceLoans(active_loans);
  repository.replacePreContracts(pre_contracts);
  repository.replaceFlags(player_flags);
  repository.replaceOffers(incoming, negotiations, cooldowns);
}

void TransferMarket::onSaved() { persisted_records = records.size(); }
