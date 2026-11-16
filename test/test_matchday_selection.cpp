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
#include <memory>
#include <unordered_set>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/view_models/formation.h"
#include "model/game.h"
#include "model/match.h"
#include "model/youth_academy.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;
constexpr LeagueID OWN_LEAGUE = 1;
/** A substitute or a player left out may be better than the starter of his
 * own position, but never by this much. */
constexpr double WIDE_MARGIN = 8.0;

int uniqueSlot(int offset)
{
  return 6'500'000 + (getpid() % 100'000) * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int value) : slot(value) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;
  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

/** The club in the middle of the league by reputation (the showcase club). */
TeamID middleOfTheLeague(const GameController& controller)
{
  std::vector<TeamID> clubs =
      controller.getLeagueById(OWN_LEAGUE)->get().getTeamIDs();
  // Equal reputations need an explicit order: otherwise different standard
  // libraries can pick a different club and simulate a different career.
  std::ranges::sort(clubs,
                    [&](TeamID left, TeamID right)
                    {
                      const auto leftReputation =
                          controller.getTeamById(left)->get().getReputation();
                      const auto rightReputation =
                          controller.getTeamById(right)->get().getReputation();
                      return leftReputation != rightReputation
                                 ? leftReputation < rightReputation
                                 : left < right;
                    });
  return clubs[clubs.size() / 2];
}

std::string nameOf(const Player* player)
{
  return player ? player->getName() : std::string("nobody");
}

/** Seniors and academy players who are not trialists. */
std::vector<const Player*> clubPlayers(const GameController& controller,
                                       TeamID club)
{
  const Game& game = *controller.getGame();
  const Team& team = controller.getTeamById(club)->get();
  std::vector<const Player*> players;
  for (const auto* ids : {&team.getPlayerIDs(), &team.getAcademyIDs()})
    for (const PlayerID player_id : *ids)
    {
      if (const YouthRecord* youth =
              game.getWorld().getYouth().record(player_id);
          youth && youth->status == YouthStatus::Candidate)
        continue;
      if (const auto player = controller.getGameData()->getPlayer(player_id))
        players.push_back(&player->get());
    }
  return players;
}

bool compareQuality(const Lineup& lineup, PlayerID player, bool standInsOnly)
{
  return !standInsOnly ||
         std::ranges::any_of(lineup.getStandIns(),
                             [player](const Lineup::StandIn& entry)
                             { return entry.stand_in == player; });
}

void expectOutfieldQuality(const Lineup& lineup, const Player& player,
                           const StatsConfig& config, const std::string& what,
                           bool standInsOnly)
{
  for (const Lineup::PositionedPlayer& slot : lineup.getOutfieldPlayers())
  {
    ASSERT_NE(slot.player, nullptr) << what;
    if (!compareQuality(lineup, slot.player->getId(), standInsOnly) ||
        Lineup::roleAt(slot.position) != player.getRole())
      continue;
    EXPECT_LT(player.getOverall(config) - slot.player->getOverall(config),
              WIDE_MARGIN)
        << what << ": " << nameOf(&player) << " sits out behind "
        << nameOf(slot.player);
  }
}

/**
 * The XI has the best goalkeeper among @p available, and nobody of
 * @p available outside the XI is better than the starter of his position
 * by WIDE_MARGIN or more. With @p standInsOnly, quality comparisons apply
 * only to temporary replacements; a fit regular is the manager's choice.
 */
void expectBestAvailable(const Lineup& lineup,
                         const std::vector<const Player*>& available,
                         const StatsConfig& config, const std::string& what,
                         bool standInsOnly = false)
{
  const Player* goalkeeper = lineup.getGoalkeeper();
  ASSERT_NE(goalkeeper, nullptr) << what;
  ASSERT_TRUE(std::ranges::contains(available, goalkeeper))
      << what << ": " << nameOf(goalkeeper) << " cannot play";
  for (const Player* player : available)
  {
    if (player->getRole() == PlayerRole::GK)
    {
      if (!compareQuality(lineup, goalkeeper->getId(), standInsOnly))
      {
        continue;
      }
      EXPECT_LE(player->getOverall(config), goalkeeper->getOverall(config))
          << what << ": " << nameOf(player) << " is a better keeper than "
          << nameOf(goalkeeper);
      continue;
    }
    if (lineup.isStarter(player->getId())) continue;
    expectOutfieldQuality(lineup, *player, config, what, standInsOnly);
  }
  for (const Lineup::PositionedPlayer& slot : lineup.getOutfieldPlayers())
    EXPECT_TRUE(std::ranges::contains(available, slot.player))
        << what << ": " << nameOf(slot.player) << " cannot play";
}
}  // namespace

TEST(MatchdaySelectionTest, SlotRolesFollowTheFormationPresets)
{
  for (const Formation::Preset& preset : Formation::PRESETS)
    for (const Formation::Slot& slot : preset.slots)
      EXPECT_EQ(Lineup::roleAt(slot.position), slot.role) << preset.name;
}

// Two months of a career with the assistant keeping the selection eligible:
// players replaced while injured, rested or away with their country take
// their places back once they can play, so neither the assistant's XI for a
// cup or league match nor the auto-picked one fields a reserve keeper ahead
// of a fit first choice when filling temporary vacancies. Healthy regulars
// remain the manager's choices; auto-pick explicitly selects the best XI.
TEST(MatchdaySelectionTest, AssistantPreservesRegularsAndAutoPickSelectsBestXI)
{
  Logger::init();
  const SlotCleanup slot{uniqueSlot(0)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  const TeamID club = middleOfTheLeague(controller);
  ASSERT_NE(club, 0u);
  controller.selectManagedTeam(club);
  ASSERT_TRUE(controller.getAssistantFixesLineup());
  const StatsConfig& config = controller.getStatsConfig();
  const Game& game = *controller.getGame();

  bool checkedLeague = false;
  bool checkedCup = false;
  bool checkedReload = false;
  for (int day = 0; day < 140 && !(checkedLeague && checkedCup); ++day)
  {
    // The assistant may replace unavailable players, but cannot override a
    // healthy manager-selected regular simply to improve the overall rating.
    const Lineup& previous = controller.getManagedTeam()->get().getLineup();
    std::vector<PlayerID> regulars;
    for (const Player* starter : previous.starters())
    {
      if (!std::ranges::any_of(previous.getStandIns(),
                               [&](const Lineup::StandIn& entry)
                               { return entry.stand_in == starter->getId(); }))
      {
        regulars.push_back(starter->getId());
      }
    }
    controller.advanceDay();
    const Lineup& lineup = controller.getManagedTeam()->get().getLineup();

    // Stand-ins are saved with the selection.
    if (!checkedReload && !lineup.getStandIns().empty())
    {
      checkedReload = true;
      ASSERT_TRUE(controller.saveGame());
      GameController reloaded;
      ASSERT_TRUE(reloaded.loadGame(slot.slot));
      EXPECT_EQ(reloaded.getManagedTeam()->get().getLineup().getStandIns(),
                lineup.getStandIns());
    }

    // Match days after the first injuries and international breaks.
    const auto fixture = controller.getNextManagedFixture();
    if (!fixture || !(fixture->date == controller.getCurrentDate()) ||
        controller.getCurrentDate() < GameDateValue(2025, 9, 1))
      continue;
    const MatchType type = fixture->type;
    if (type != MatchType::LEAGUE && type != MatchType::CUP) continue;
    bool& checked = type == MatchType::LEAGUE ? checkedLeague : checkedCup;
    if (checked) continue;
    checked = true;
    const std::string what = controller.getCurrentDate().toString() +
                             (type == MatchType::LEAGUE ? " league" : " cup");

    // The assistant fills vacancies and recalls the manager's regulars
    // (MatchdaySquad::recallRegulars). It does not auto-pick a new best XI
    // whenever a healthy squad player's rating improves.
    std::vector<const Player*> fit;
    for (const Player* player : clubPlayers(controller, club))
      if (game.isEligible(*player, type, fixture->date) &&
          !game.getMedical().isRested(player->getId()))
        fit.push_back(player);
    for (const Player* player : fit)
    {
      if (std::ranges::contains(regulars, player->getId()))
      {
        EXPECT_TRUE(lineup.isStarter(player->getId()))
            << what << ": the assistant replaced fit regular "
            << nameOf(player);
      }
    }
    expectBestAvailable(lineup, fit, config, what + " (assistant)", true);

    // Auto-pick best XI on the Lineup screen: the senior squad without the
    // injured and the suspended.
    std::unordered_set<PlayerID> unavailable;
    for (const PlayerID injured : controller.getInjuredPlayers(club))
      unavailable.insert(injured);
    for (const auto& record : controller.getSuspendedPlayers(club))
      if (record.scope == type && record.ban_matches > 0)
        unavailable.insert(record.player_id);
    std::vector<const Player*> squad;
    std::vector<const Player*> pickable;
    for (const auto& player : controller.getPlayersForTeam(club))
    {
      squad.push_back(&player.get());
      if (!unavailable.contains(player.get().getId()))
        pickable.push_back(&player.get());
    }
    Lineup picked = lineup;
    const int preset = std::max(Formation::detectPreset(lineup), 0);
    Formation::autoPickAvailable(picked, Formation::PRESETS[preset], squad,
                                 unavailable, config);
    ASSERT_GE(pickable.size(), 11u) << what;
    EXPECT_EQ(picked.getReserves().size(),
              std::min(pickable.size() - 11u, Lineup::MAX_SUBSTITUTES))
        << what;
    expectBestAvailable(picked, pickable, config, what + " (auto-pick)");
  }
  EXPECT_TRUE(checkedLeague) << "no league match after the first month";
  EXPECT_TRUE(checkedCup) << "no cup match in the first four months";
  EXPECT_TRUE(checkedReload) << "the assistant never needed a stand-in";
}
