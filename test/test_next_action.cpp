// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <format>
#include <memory>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/next_action.h"
#include "model/onboarding.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 310'000 + getpid() % 100'000 * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

bool hasKind(const std::vector<NextAction>& actions, NextActionKind kind)
{
  return std::ranges::any_of(actions, [kind](const NextAction& action)
                             { return action.kind == kind; });
}

const NextAction* find(const std::vector<NextAction>& actions,
                       NextActionKind kind)
{
  const auto found = std::ranges::find(actions, kind, &NextAction::kind);
  return found != actions.end() ? std::to_address(found) : nullptr;
}

NextActionFacts::Offer offer(std::uint32_t id, int days_left)
{
  NextActionFacts::Offer entry;
  entry.id = id;
  entry.player = id * 10;
  entry.player_name = std::format("Player {}", id);
  entry.buyer = "Club";
  entry.fee = "€1.00M";
  entry.days_left = days_left;
  return entry;
}
}  // namespace

TEST(NextActionTest, CalmClubHasNothingPending)
{
  EXPECT_TRUE(rankNextActions(NextActionFacts{}).empty());
}

TEST(NextActionTest, UnavailableStarterBeforeMatchComesFirst)
{
  NextActionFacts facts;
  facts.days_to_match = 1;
  facts.next_opponent = "Rivals";
  facts.unavailable_selected = {"Injured Striker"};
  facts.unseen_grade_a = {{7, "Prospect", "Scout"}};
  facts.board_confidence = 20.0f;
  const auto actions = rankNextActions(facts);
  ASSERT_FALSE(actions.empty());
  EXPECT_EQ(actions.front().kind, NextActionKind::UnavailableInLineup);
  EXPECT_EQ(actions.front().target, ActionTarget::Lineup);
  EXPECT_EQ(actions.front().reason.args.front(), "Injured Striker");
  EXPECT_TRUE(hasKind(actions, NextActionKind::BoardWarning));
  EXPECT_TRUE(hasKind(actions, NextActionKind::ScoutReportA));
}

TEST(NextActionTest, UrgencyGrowsAsTheMatchNears)
{
  NextActionFacts facts;
  facts.unavailable_selected = {"A"};
  facts.days_to_match = 6;
  const int later = rankNextActions(facts).front().priority;
  facts.days_to_match = 3;
  const int soon = rankNextActions(facts).front().priority;
  facts.days_to_match = 0;
  const int today = rankNextActions(facts).front().priority;
  EXPECT_LT(later, soon);
  EXPECT_LT(soon, today);
}

TEST(NextActionTest, OnlyTheFirstExpiringContractIsSurfaced)
{
  NextActionFacts facts;
  facts.expiring_contracts = {{11, "First Player"}, {22, "Second Player"}};

  const auto actions = rankNextActions(facts);
  const NextAction* contract = find(actions, NextActionKind::ContractExpiring);

  ASSERT_NE(contract, nullptr);
  EXPECT_EQ(contract->ref, 11U);
  EXPECT_EQ(contract->title.args.front(), "First Player");
  EXPECT_EQ(std::ranges::count(actions, NextActionKind::ContractExpiring,
                               &NextAction::kind),
            1);
}

TEST(NextActionTest, DelegatedLineupFixIsLowPriority)
{
  NextActionFacts facts;
  facts.days_to_match = 0;
  facts.unavailable_selected = {"A"};
  facts.board_confidence = 20.0f;
  facts.assistant_fixes_lineup = true;
  const auto actions = rankNextActions(facts);
  EXPECT_EQ(actions.front().kind, NextActionKind::BoardWarning);
  const NextAction* lineup = find(actions, NextActionKind::UnavailableInLineup);
  ASSERT_NE(lineup, nullptr);
  EXPECT_EQ(lineup->reason.key, "NEXT_LINEUP_REASON_ASSISTANT");
}

TEST(NextActionTest, OffersAreRankedByDeadlineAndFolded)
{
  NextActionFacts facts;
  facts.offers = {offer(1, 6), offer(2, 1), offer(3, 4), offer(4, 9)};
  const auto actions = rankNextActions(facts);
  std::vector<std::uint32_t> refs;
  for (const NextAction& action : actions)
    if (action.kind == NextActionKind::IncomingOffer)
      refs.push_back(action.ref);
  // Two individual offers (soonest deadline first) and one "more" entry.
  ASSERT_EQ(refs.size(), NextActionRules::MAX_OFFER_ACTIONS + 1);
  EXPECT_EQ(refs[0], 2U);
  EXPECT_EQ(refs[1], 3U);
  EXPECT_EQ(refs[2], 0U);
}

TEST(NextActionTest, CongestedWeekNeedsMatchesAndTiredPlayers)
{
  NextActionFacts facts;
  facts.matches_next_week = 2;
  facts.tired_players = 1;
  facts.average_condition = 95.0f;
  EXPECT_FALSE(hasKind(rankNextActions(facts), NextActionKind::CongestedWeek));
  facts.tired_players = NextActionRules::CONGESTED_TIRED_PLAYERS;
  EXPECT_TRUE(hasKind(rankNextActions(facts), NextActionKind::CongestedWeek));
  facts.matches_next_week = 1;
  EXPECT_FALSE(hasKind(rankNextActions(facts), NextActionKind::CongestedWeek));
}

TEST(NextActionTest, WindowHoleOnlyCloseToTheDeadline)
{
  NextActionFacts facts;
  facts.window_open = true;
  facts.squad_holes = {"GK"};
  facts.window_days_left = NextActionRules::WINDOW_WARNING_DAYS + 5;
  EXPECT_FALSE(
      hasKind(rankNextActions(facts), NextActionKind::WindowSquadHole));
  facts.window_days_left = 2;
  const auto actions = rankNextActions(facts);
  ASSERT_TRUE(hasKind(actions, NextActionKind::WindowSquadHole));
  EXPECT_EQ(find(actions, NextActionKind::WindowSquadHole)->target,
            ActionTarget::Transfers);
  facts.squad_holes.clear();
  EXPECT_FALSE(
      hasKind(rankNextActions(facts), NextActionKind::WindowSquadHole));
}

TEST(NextActionTest, OppositionReportShortlyBeforeTheMatchUntilRead)
{
  NextActionFacts facts;
  facts.next_opponent = "Rivals";
  facts.days_to_match = 5;
  EXPECT_FALSE(
      hasKind(rankNextActions(facts), NextActionKind::OppositionReport));
  facts.days_to_match = 1;
  EXPECT_TRUE(
      hasKind(rankNextActions(facts), NextActionKind::OppositionReport));
  facts.opposition_viewed = true;
  EXPECT_FALSE(
      hasKind(rankNextActions(facts), NextActionKind::OppositionReport));
}

TEST(NextActionTest, ExpiringContractMattersMoreOnceOthersCanPreSign)
{
  NextActionFacts facts;
  facts.expiring_contracts = {{11, "Captain"}, {12, "Winger"}};
  const auto autumn = rankNextActions(facts);
  ASSERT_EQ(autumn.size(), 1U);  // One contract at a time.
  facts.pre_contract_period = true;
  const auto winter = rankNextActions(facts);
  EXPECT_GT(winter.front().priority, autumn.front().priority);
  EXPECT_EQ(winter.front().ref, 11U);
}

TEST(NextActionTest, LimitAndOrderAreDeterministic)
{
  NextActionFacts facts;
  facts.days_to_match = 1;
  facts.next_opponent = "Rivals";
  facts.unavailable_selected = {"A", "B"};
  facts.offers = {offer(1, 2)};
  facts.pending_talks = {{5, "Unhappy"}};
  facts.board_confidence = 10.0f;
  const auto all = rankNextActions(facts);
  const auto again = rankNextActions(facts);
  ASSERT_EQ(all.size(), again.size());
  for (std::size_t index = 0; index < all.size(); ++index)
  {
    EXPECT_EQ(all[index].kind, again[index].kind);
    if (index > 0) EXPECT_GE(all[index - 1].priority, all[index].priority);
  }
  EXPECT_EQ(rankNextActions(facts, 2).size(), 2U);
}

TEST(NextActionTest, NothingToFixWhenNobodyFitCanReplaceThem)
{
  NextActionFacts facts;
  facts.days_to_match = 0;
  facts.next_opponent = "Rivals";
  facts.unavailable_selected = {"A", "B"};
  facts.no_fit_replacements = true;
  EXPECT_FALSE(
      hasKind(rankNextActions(facts), NextActionKind::UnavailableInLineup));
}

TEST(NextActionTest, ControllerReportsUnavailableStarters)
{
  Logger::init();
  SlotCleanup slot{uniqueSlot(1)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  controller.selectManagedTeam(controller.getTeams().front().get().getId());
  controller.setAssistantFixesLineup(false);
  const Team& club = controller.getManagedTeam()->get();
  const Player* keeper = club.getLineup().getGoalkeeper();
  ASSERT_NE(keeper, nullptr);
  PlayerDynamics& dynamics = controller.getGameData()
                                 ->getPlayers()
                                 .at(keeper->getId())
                                 .mutableDynamics();
  dynamics.injury = InjuryType::HamstringStrain;
  dynamics.injury_days = 20;

  const NextActionFacts facts = gatherNextActionFacts(controller);
  EXPECT_FALSE(facts.unavailable_selected.empty());
  EXPECT_FALSE(facts.no_fit_replacements) << "the reserve keeper can play";
  EXPECT_FALSE(facts.assistant_fixes_lineup);
  const auto actions = controller.getNextActions(10);
  EXPECT_TRUE(hasKind(actions, NextActionKind::UnavailableInLineup));
}

TEST(NextActionTest, EveryoneInjuredIsNotReportedAsAFixableLineup)
{
  Logger::init();
  SlotCleanup slot{uniqueSlot(2)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  controller.selectManagedTeam(controller.getTeams().front().get().getId());
  controller.setAssistantFixesLineup(false);
  const Team& club = controller.getManagedTeam()->get();
  for (const PlayerID id : club.getPlayerIDs())
  {
    PlayerDynamics& dynamics =
        controller.getGameData()->getPlayers().at(id).mutableDynamics();
    dynamics.injury = InjuryType::HamstringStrain;
    dynamics.injury_days = 30;
  }
  const auto fixture = controller.getNextManagedFixture();
  ASSERT_TRUE(fixture.has_value());
  const MatchType type = fixture->type;
  // The assistant's best selection: injured players nobody can replace.
  controller.autoFixLineup(club.getId(), type);
  ASSERT_TRUE(controller.canKickOff(club.getId(), type));

  const NextActionFacts facts = gatherNextActionFacts(controller);
  EXPECT_FALSE(facts.unavailable_selected.empty());
  EXPECT_TRUE(facts.no_fit_replacements);
  EXPECT_FALSE(hasKind(controller.getNextActions(10),
                       NextActionKind::UnavailableInLineup));
}
