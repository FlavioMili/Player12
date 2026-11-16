// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/next_action.h"

#include <algorithm>
#include <array>
#include <format>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "model/delegation.h"
#include "model/inbox.h"
#include "model/role_utils.h"
#include "model/world_rng.h"

namespace
{
namespace R = NextActionRules;

std::string playerName(const GameController& controller, PlayerID player)
{
  const auto data = controller.getGameData();
  if (!data) return {};
  const auto found = std::as_const(*data).getPlayer(player);
  return found ? found->get().getName() : std::string();
}

std::string teamName(const GameController& controller, TeamID team)
{
  const auto found = controller.getTeamById(team);
  return found ? found->get().getName() : std::string();
}

std::string joined(const std::vector<std::string>& names, std::size_t limit)
{
  std::string text;
  for (std::size_t index = 0; index < names.size() && index < limit; ++index)
  {
    if (index > 0) text += ", ";
    text += names[index];
  }
  if (names.size() > limit) text += std::format(" +{}", names.size() - limit);
  return text;
}

int urgency(int days, int today_priority, int soon_priority, int later)
{
  if (days <= 1) return today_priority;
  if (days <= 3) return soon_priority;
  return later;
}
}  // namespace

std::vector<NextAction> rankNextActions(const NextActionFacts& facts,
                                        std::size_t limit)
{
  std::vector<NextAction> actions;
  const auto add = [&actions](NextActionKind kind, ActionTarget target,
                              std::uint32_t ref, int priority,
                              AnalysisLine title, AnalysisLine reason)
  {
    actions.emplace_back(kind, target, ref, priority, std::move(title),
                         std::move(reason));
  };

  if (!facts.unavailable_selected.empty() && facts.days_to_match >= 0 &&
      !facts.no_fit_replacements)
  {
    // The assistant replaces them at kick-off when he is in charge of it.
    const int priority = facts.assistant_fixes_lineup
                             ? 30
                             : urgency(facts.days_to_match, 95, 80, 50);
    add(NextActionKind::UnavailableInLineup, ActionTarget::Lineup, 0, priority,
        {"NEXT_LINEUP_TITLE",
         {std::to_string(facts.unavailable_selected.size())}},
        {facts.assistant_fixes_lineup ? "NEXT_LINEUP_REASON_ASSISTANT"
                                      : "NEXT_LINEUP_REASON",
         {joined(facts.unavailable_selected, 3), facts.next_opponent,
          std::to_string(facts.days_to_match)}});
  }

  std::vector<NextActionFacts::Offer> offers = facts.offers;
  std::ranges::stable_sort(offers, {}, &NextActionFacts::Offer::days_left);
  for (std::size_t index = 0; index < offers.size(); ++index)
  {
    const NextActionFacts::Offer& offer = offers[index];
    if (index == R::MAX_OFFER_ACTIONS)
    {
      add(NextActionKind::IncomingOffer, ActionTarget::Inbox, 0, 50,
          {"NEXT_OFFERS_MORE_TITLE",
           {std::to_string(offers.size() - R::MAX_OFFER_ACTIONS)}},
          {"NEXT_OFFERS_MORE_REASON", {}});
      break;
    }
    add(NextActionKind::IncomingOffer, ActionTarget::Inbox, offer.id,
        urgency(offer.days_left, 85, 70, 55),
        {offer.loan ? "NEXT_LOAN_OFFER_TITLE" : "NEXT_OFFER_TITLE",
         {offer.player_name, offer.buyer}},
        {"NEXT_OFFER_REASON",
         {offer.fee, std::to_string(std::max(offer.days_left, 0))}});
  }

  if (!facts.pending_talks.empty())
  {
    const NextActionFacts::Person& first = facts.pending_talks.front();
    add(NextActionKind::PendingTalk, ActionTarget::Player, first.player, 60,
        {"NEXT_TALK_TITLE", {first.name}},
        {facts.pending_talks.size() > 1 ? "NEXT_TALK_REASON_MANY"
                                        : "NEXT_TALK_REASON",
         {std::to_string(facts.pending_talks.size() - 1)}});
  }

  if (!facts.expiring_contracts.empty())
  {
    const NextActionFacts::Person& person = facts.expiring_contracts.front();
    add(NextActionKind::ContractExpiring, ActionTarget::Player, person.player,
        facts.pre_contract_period ? 65 : 40,
        {"NEXT_CONTRACT_TITLE", {person.name}},
        {facts.pre_contract_period ? "NEXT_CONTRACT_REASON_PRECONTRACT"
                                   : "NEXT_CONTRACT_REASON",
         {person.name}});
  }

  if (!facts.unseen_grade_a.empty())
  {
    const NextActionFacts::Report& report = facts.unseen_grade_a.front();
    add(NextActionKind::ScoutReportA, ActionTarget::Scouting, report.player, 50,
        {"NEXT_SCOUT_TITLE", {report.name}},
        {"NEXT_SCOUT_REASON",
         {report.scout, std::to_string(facts.unseen_grade_a.size())}});
  }

  if (facts.matches_next_week >= 2 &&
      (facts.tired_players >= R::CONGESTED_TIRED_PLAYERS ||
       facts.average_condition < R::CONGESTED_AVERAGE))
  {
    add(NextActionKind::CongestedWeek, ActionTarget::Training, 0,
        facts.assistant_runs_training ? 25 : 60,
        {"NEXT_CONGESTED_TITLE", {std::to_string(facts.matches_next_week)}},
        {facts.assistant_runs_training ? "NEXT_CONGESTED_REASON_ASSISTANT"
                                       : "NEXT_CONGESTED_REASON",
         {std::to_string(facts.tired_players),
          std::format("{:.0f}", facts.average_condition)}});
  }

  if (facts.board_confidence < R::BOARD_WARNING_CONFIDENCE)
  {
    add(NextActionKind::BoardWarning, ActionTarget::Club, 0, 75,
        {"NEXT_BOARD_TITLE", {}},
        {"NEXT_BOARD_REASON", {std::format("{:.0f}", facts.board_confidence)}});
  }

  if (facts.window_open && facts.window_days_left >= 0 &&
      facts.window_days_left <= R::WINDOW_WARNING_DAYS &&
      !facts.squad_holes.empty())
  {
    add(NextActionKind::WindowSquadHole, ActionTarget::Transfers, 0,
        facts.window_days_left <= 3 ? 72 : 55,
        {"NEXT_WINDOW_TITLE", {std::to_string(facts.window_days_left)}},
        {"NEXT_WINDOW_REASON", {joined(facts.squad_holes, 3)}});
  }

  if (facts.days_to_match >= 0 && facts.days_to_match <= R::OPPOSITION_DAYS &&
      !facts.opposition_viewed && !facts.next_opponent.empty())
  {
    add(NextActionKind::OppositionReport, ActionTarget::Opposition, 0, 40,
        {"NEXT_OPPOSITION_TITLE", {facts.next_opponent}},
        {"NEXT_OPPOSITION_REASON", {std::to_string(facts.days_to_match)}});
  }

  std::ranges::stable_sort(actions, std::greater{}, &NextAction::priority);
  if (actions.size() > limit) actions.resize(limit);
  return actions;
}

NextActionFacts gatherNextActionFacts(const GameController& controller)
{
  using enum PlayerRole;
  NextActionFacts facts;
  const auto managed = controller.getManagedTeam();
  if (!managed) return facts;
  const Team& club = managed->get();
  const TeamID club_id = club.getId();
  const GameDateValue today = controller.getCurrentDate();
  const std::int32_t today_ordinal = dayOrdinal(today);

  for (const Match& match : controller.getTeamFixtures(club_id))
  {
    if (match.isPlayed() || match.getDate() < today) continue;
    facts.days_to_match = dayOrdinal(match.getDate()) - today_ordinal;
    const TeamID opponent = match.getHomeTeamId() == club_id
                                ? match.getAwayTeamId()
                                : match.getHomeTeamId();
    facts.next_opponent = teamName(controller, opponent);
    facts.opposition_viewed = controller.wasOppositionReportViewed(opponent);
    for (const PlayerID player :
         controller.getIneligibleSelections(club_id, match.getMatchType()))
      facts.unavailable_selected.push_back(playerName(controller, player));
    facts.no_fit_replacements =
        !facts.unavailable_selected.empty() &&
        controller.canKickOff(club_id, match.getMatchType());
    break;
  }
  facts.assistant_fixes_lineup = controller.isDelegated(Duty::LineupFixes);
  facts.assistant_runs_training =
      controller.isDelegated(Duty::TrainingSchedule);

  for (const IncomingOffer& offer : controller.getIncomingOffers())
  {
    if (offer.status != OfferStatus::AwaitingClub) continue;
    NextActionFacts::Offer entry;
    entry.id = offer.id;
    entry.player = offer.player_id;
    entry.player_name = playerName(controller, offer.player_id);
    entry.buyer = teamName(controller, offer.buyer);
    entry.loan = offer.loan;
    entry.fee = offer.loan ? formatMoney(offer.loan_terms.loan_fee)
                           : formatMoney(offer.terms.fee);
    entry.days_left = dayOrdinal(offer.expires) - today_ordinal;
    facts.offers.push_back(std::move(entry));
  }

  float condition_sum = 0.0f;
  int fit_players = 0;
  for (const auto& player_ref : controller.getPlayersForTeam(club_id))
  {
    const Player& player = player_ref.get();
    const PlayerID id = player.getId();
    if (controller.hasPendingTalk(id))
      facts.pending_talks.emplace_back(id, player.getName());
    if (player.getContractYears() <= 1)
    {
      const SquadRole role = controller.getSquadRole(id);
      if (role == SquadRole::KeyPlayer || role == SquadRole::FirstTeam)
        facts.expiring_contracts.emplace_back(id, player.getName());
    }
    if (!controller.isPlayerAvailable(id)) continue;
    const float condition = player.getDynamics().condition;
    condition_sum += condition;
    ++fit_players;
    if (condition < static_cast<float>(R::TIRED_CONDITION))
      ++facts.tired_players;
  }
  if (fit_players > 0)
    facts.average_condition = condition_sum / static_cast<float>(fit_players);
  facts.pre_contract_period = today.month >= 1 && today.month <= 6;

  for (const ScoutReport& report : controller.getScoutReports())
  {
    if (report.grade == ScoutGrade::A && !report.seen)
      facts.unseen_grade_a.emplace_back(
          report.player_id, playerName(controller, report.player_id),
          report.scout_name);
  }

  for (const auto& day : controller.getTrainingWeekPreview())
    if (day.opponent != 0) ++facts.matches_next_week;

  facts.board_confidence = controller.getBoardState().confidence;

  const TransferNegotiation::WindowInfo window = controller.getTransferWindow();
  facts.window_open = window.open;
  facts.window_days_left = window.days_to_deadline;
  if (window.open)
  {
    const GameController::SquadNeeds needs =
        controller.evaluateSquadNeeds(club_id);
    const std::array<std::pair<int, PlayerRole>, 7> holes = {
        {{needs.missing_gk, GK},
         {needs.missing_cb, CB},
         {needs.missing_lb, LB},
         {needs.missing_rb, RB},
         {needs.missing_mid, CM},
         {needs.missing_wing, LW},
         {needs.missing_st, ST}}};
    for (const auto& [missing, role] : holes)
      if (missing > 0)
        facts.squad_holes.emplace_back(RoleUtils::shortName(role));
  }
  return facts;
}
