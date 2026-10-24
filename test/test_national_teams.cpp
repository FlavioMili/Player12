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
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/calendar.h"
#include "model/match_scheduler.h"
#include "model/national_teams.h"
#include "model/team.h"
#include "model/world_simulation.h"

namespace
{
using International::Competition;

constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 400'000 + getpid() % 100'000 * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

std::unique_ptr<GameController> makeWorld(int slot)
{
  Logger::init();
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot, WORLD_SEED);
  return controller;
}
}  // namespace

TEST(NationalTeamsTest, RoundRobinMeetsEveryOpponent)
{
  const auto double_round = International::roundRobin(4, true);
  ASSERT_EQ(double_round.size(), 6u);
  std::map<std::pair<size_t, size_t>, int> meetings;
  for (const auto& round : double_round)
  {
    std::set<size_t> busy;
    for (const auto& [home, away] : round)
    {
      EXPECT_TRUE(busy.insert(home).second);
      EXPECT_TRUE(busy.insert(away).second);
      ++meetings[{home, away}];
    }
  }
  for (size_t a = 0; a < 4; ++a)
    for (size_t b = 0; b < 4; ++b)
      if (a != b) EXPECT_EQ((meetings[{a, b}]), 1);

  const auto odd = International::roundRobin(5, false);
  EXPECT_EQ(odd.size(), 5u);
  std::map<size_t, int> games;
  for (const auto& round : odd)
    for (const auto& [home, away] : round)
    {
      ++games[home];
      ++games[away];
    }
  for (size_t team = 0; team < 5; ++team) EXPECT_EQ(games[team], 4);
}

TEST(NationalTeamsTest, FinalsSizeAndConfederations)
{
  EXPECT_EQ(International::finalsSize(30), 16u);
  EXPECT_EQ(International::finalsSize(20), 8u);
  EXPECT_EQ(International::finalsSize(7), 4u);
  EXPECT_EQ(International::finalsSize(3), 0u);
  EXPECT_EQ(International::confederationOf(Language::IT),
            International::Confederation::Europe);
  EXPECT_EQ(International::confederationOf(Language::BR),
            International::Confederation::Americas);
  EXPECT_EQ(International::confederationOf(Language::JP),
            International::Confederation::Asia);
  EXPECT_EQ(International::teamNameKey(Language::IT), "NT_Italian");
}

TEST(NationalTeamsTest, SwitchingNationFollowsEligibilityRules)
{
  const GameDateValue today(2030, 1, 1);
  International::Record record;
  EXPECT_TRUE(International::canSwitchNation(record, today));  // Uncapped.
  record.caps = 2;
  record.last_cap_age = 19;
  record.last_cap = GameDateValue(2026, 6, 1);
  EXPECT_TRUE(International::canSwitchNation(record, today));
  record.last_cap = GameDateValue(2028, 6, 1);  // Less than three years.
  EXPECT_FALSE(International::canSwitchNation(record, today));
  record.last_cap = GameDateValue(2026, 6, 1);
  record.caps = 4;
  EXPECT_FALSE(International::canSwitchNation(record, today));
  record.caps = 2;
  record.last_cap_age = 22;
  EXPECT_FALSE(International::canSwitchNation(record, today));
  record.last_cap_age = 19;
  record.finals_caps = 1;
  EXPECT_FALSE(International::canSwitchNation(record, today));
}

TEST(NationalTeamsTest, SquadSelectionIsBalancedAndDeterministic)
{
  const SlotCleanup slot{uniqueSlot(1)};
  const auto controller = makeWorld(slot.slot);
  const auto gamedata = controller->getGameData();
  std::vector<const Player*> italians;
  for (const auto& [id, player] : gamedata->getPlayers())
    if (player.getNationality() == Language::IT && player.isAvailable())
      italians.push_back(&player);
  std::ranges::sort(italians, {}, &Player::getId);
  ASSERT_GT(italians.size(), 40u);
  const auto squad = International::selectSquad(
      italians, International::FINALS_SQUAD, gamedata->getStatsConfig(), {});
  EXPECT_EQ(squad.size(), International::FINALS_SQUAD);
  const auto keepers = std::ranges::count_if(
      squad, [&](PlayerID id)
      { return gamedata->getPlayer(id)->get().getRole() == PlayerRole::GK; });
  EXPECT_GE(keepers, 3);
  EXPECT_LE(keepers, 4);
  EXPECT_EQ(squad,
            International::selectSquad(italians, International::FINALS_SQUAD,
                                       gamedata->getStatsConfig(), {}));
  // The best outfield player is always picked.
  const Player* best = *std::ranges::max_element(
      italians,
      [&](const Player* a, const Player* b)
      {
        return a->getOverall(gamedata->getStatsConfig()) <
               b->getOverall(gamedata->getStatsConfig());
      });
  EXPECT_TRUE(std::ranges::contains(squad, best->getId()));
}

TEST(NationalTeamsTest, CycleRunsQualifiersAndFinals)
{
  const SlotCleanup slot{uniqueSlot(2)};
  const auto controller = makeWorld(slot.slot);
  const auto gamedata = controller->getGameData();
  NationalTeams nations(gamedata);
  WorldSimulation world(gamedata);
  MatchScheduler scheduler(2);
  GameDateValue day(2025, 7, 2);
  nations.planSeason(2025, day, &world);
  ASSERT_GE(nations.getTeams().size(), 12u);
  ASSERT_FALSE(nations.getGroups().empty());
  EXPECT_EQ(nations.getGroups().front().competition,
            Competition::WorldQualifier);
  ASSERT_EQ(nations.getFinals().size(), 1u);
  EXPECT_EQ(nations.getFinals().front().competition, Competition::WorldFinals);
  for (const auto& team : nations.getTeams()) EXPECT_FALSE(team.coach.empty());

  bool saw_duty = false;
  const GameDateValue end(2026, 7, 12);
  while (day < end)
  {
    day = SeasonCalendar::addDays(day, 1);
    world.onDayAdvanced(day, FREE_AGENTS_TEAM_ID);  // Injuries heal.
    nations.onDay(day, scheduler, world, FREE_AGENTS_TEAM_ID);
    for (const auto& squad : nations.getSquads())
    {
      EXPECT_GE(squad.players.size(), 16u);
      EXPECT_LE(squad.players.size(), International::FINALS_SQUAD);
      if (!squad.players.empty() &&
          nations.isOnDuty(squad.players.front(), day))
        saw_duty = true;
    }
  }
  EXPECT_TRUE(saw_duty);
  for (const auto& fixture : nations.getFixtures())
    if (fixture.date < end)
      EXPECT_TRUE(fixture.played) << fixture.date.toString();

  // The finals were drawn from the qualifiers and produced a winner.
  const auto& finals = nations.getFinals().front();
  EXPECT_TRUE(finals.drawn);
  EXPECT_EQ(finals.qualified.size(), finals.size);
  ASSERT_TRUE(finals.winner.has_value());
  EXPECT_NE(*finals.winner, *finals.runner_up);
  ASSERT_EQ(nations.getHonours().size(), 1u);
  EXPECT_EQ(nations.getHonours().front().winner, *finals.winner);
  for (const auto& fixture : nations.getFixtures())
  {
    if (fixture.competition != Competition::WorldFinals) continue;
    EXPECT_TRUE(fixture.neutral);
    EXPECT_FALSE(fixture.date.month == 7 && fixture.date.day == 1);
    if (fixture.stage != International::Stage::Group)
      EXPECT_TRUE(International::winnerOf(fixture).has_value());
  }
  // Everyone is back and clubs were paid for their finals players.
  EXPECT_TRUE(nations.getSquads().empty());
  // Nothing else pays competition money in this standalone world.
  bool compensated = false;
  for (const auto& [id, team] : gamedata->getTeams())
    for (const FinanceTransaction& entry : team.getFinances().getLedger())
      if (entry.category == FinanceCategory::PrizeMoney &&
          GameDateValue(2026, 6, 1) < entry.date &&
          entry.amount % NationalTeams::COMPENSATION_PER_DAY == 0)
        compensated = true;
  EXPECT_TRUE(compensated);
  const auto leaders = nations.capsLeaders(10);
  ASSERT_FALSE(leaders.empty());
  EXPECT_GE(leaders.front().second.caps, 8);
  EXPECT_GE(leaders.front().second.finals_caps, 3);

  // A new season plans the nations league (odd finals year).
  nations.planSeason(2026, GameDateValue(2026, 7, 13), &world);
  ASSERT_FALSE(nations.getGroups().empty());
  EXPECT_EQ(nations.getGroups().front().competition,
            Competition::NationsLeague);

  const std::string saved = nations.serialize();
  NationalTeams restored(gamedata);
  restored.deserialize(saved);
  EXPECT_EQ(restored.serialize(), saved);
}

TEST(NationalTeamsTest, SeasonOpeningJourney)
{
  const SlotCleanup slot{uniqueSlot(3)};
  auto controller = makeWorld(slot.slot);
  Game* game = controller->getGame();
  const auto& continental = game->getCompetitions().getContinental();
  ASSERT_EQ(continental.getSeasons().size(), 3u);
  // Manage the strongest club of the top continental competition.
  const TeamID club = continental.getSeasons().front().entrants.front().team_id;
  controller->selectManagedTeam(club);

  // League-phase draw (late August) happens as an event.
  const GameDateValue draw_date = continental.getSeasons().front().draw_date;
  while (controller->getCurrentDate() < draw_date) controller->advanceDay();
  controller->advanceDay();
  ASSERT_TRUE(continental.getSeasons().front().drawn);
  size_t own = 0;
  for (const Match& match : game->getCalendar().getTeamFixtures(club))
    if (match.getMatchType() == MatchType::CONTINENTAL) ++own;
  EXPECT_EQ(own, 8u);
  const auto has = [&](const char* key)
  {
    return std::ranges::any_of(controller->getInbox(),
                               [key](const InboxMessage& message)
                               { return message.title_key == key; });
  };
  EXPECT_TRUE(has("INBOX_CONT_DRAW_TITLE"));

  // September window: call-ups a week before, duty, matches, release.
  const auto window = SeasonCalendar::internationalWindows(2025).front();
  while (controller->getCurrentDate() < window.start) controller->advanceDay();
  const NationalTeams& nations = game->getNationalTeams();
  std::vector<PlayerID> called;
  for (const auto& squad : nations.getSquads())
    for (const PlayerID player_id : squad.players)
    {
      const auto player = controller->getGameData()->getPlayer(player_id);
      if (player && player->get().getTeamId() == club)
        called.push_back(player_id);
    }
  ASSERT_FALSE(called.empty());
  EXPECT_TRUE(has("INBOX_INTL_CALLUP_TITLE"));
  const Player& away =
      controller->getGameData()->getPlayer(called.front())->get();
  EXPECT_TRUE(nations.isOnDuty(away.getId(), controller->getCurrentDate()));
  EXPECT_FALSE(game->isEligible(away, MatchType::LEAGUE));
  // No competitive club match inside the window.
  for (GameDateValue day = window.start; !(window.end < day);
       day = SeasonCalendar::addDays(day, 1))
    for (const Match& match : game->getCalendar().getMatchesForDate(day))
      EXPECT_EQ(match.getMatchType(), MatchType::FRIENDLY);

  // Mid-window save and reload keep the duty.
  controller->advanceDay();
  controller->saveGame();
  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(slot.slot));
  game = controller->getGame();
  EXPECT_TRUE(game->getNationalTeams().isOnDuty(called.front(),
                                                controller->getCurrentDate()));
  EXPECT_TRUE(
      game->getCompetitions().getContinental().getSeasons().front().drawn);
  EXPECT_EQ(game->getCompetitions()
                .getContinental()
                .getTable(Continental::CHAMPIONS_CUP_ID)
                .size(),
            36u);

  while (!(window.end < controller->getCurrentDate())) controller->advanceDay();
  controller->advanceDay();
  const NationalTeams& after = game->getNationalTeams();
  EXPECT_FALSE(after.isOnDuty(called.front(), controller->getCurrentDate()));
  size_t played = 0;
  for (const auto& fixture : after.getFixtures())
    if (!(window.end < fixture.date) && fixture.played) ++played;
  EXPECT_GT(played, 10u);
  EXPECT_FALSE(after.capsLeaders(1).empty());
}
