// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Inbox filters kept with the career, followed players' news and decision
// moments (two-option dilemmas): triggering rules, effects and persistence.

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/paths.h"
#include "global/runtime_paths.h"
#include "model/calendar.h"
#include "model/inbox.h"
#include "model/interactions.h"
#include "model/match_report.h"
#include "model/stories.h"
#include "model/world_rng.h"
#include "model/world_simulation.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 9'100'000 + getpid() % 100'000 * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

std::unique_ptr<GameController> makeCareer(int slot)
{
  Logger::init();
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot, WORLD_SEED);
  controller->selectManagedTeam(controller->getTeams().front().get().getId());
  return controller;
}

TeamID managedId(const GameController& controller)
{
  return controller.getManagedTeam()->get().getId();
}

/** A club other than the managed one, with players. */
TeamID otherClub(const GameController& controller)
{
  for (const auto& team : controller.getTeams())
    if (team.get().getId() != managedId(controller) &&
        !controller.getPlayersForTeam(team.get().getId()).empty())
      return team.get().getId();
  return FREE_AGENTS_TEAM_ID;
}

Player& mutablePlayer(GameController& controller, PlayerID id)
{
  return controller.getGameData()->getPlayers().at(id);
}

std::size_t countTitle(const Inbox& inbox, const std::string& title)
{
  return static_cast<std::size_t>(
      std::ranges::count(inbox.getMessages(), title, &InboxMessage::title_key));
}

nlohmann::json loadLanguage(const char* file)
{
  std::ifstream stream(AssetPaths::root() + "assets/lang/" + file);
  return nlohmann::json::parse(stream);
}

std::vector<StoryKind> allDilemmas()
{
  std::vector<StoryKind> kinds;
  for (auto kind = static_cast<int>(std::to_underlying(Stories::FIRST_DILEMMA));
       kind <= static_cast<int>(std::to_underlying(Stories::LAST_DILEMMA));
       ++kind)
    kinds.push_back(static_cast<StoryKind>(kind));
  return kinds;
}

/** Days to the next fixture when the club plays every @p every days. */
StoryDayContext weeklyRhythm(std::int32_t day, int every = 7)
{
  StoryDayContext context;
  context.days_to_match = (every - day % every) % every;
  return context;
}

/** One raised moment: kind, day, subject. */
using Raised = std::tuple<StoryKind, std::int32_t, PlayerID>;

/** Runs a story engine over @p days from @p start, leaving every moment
 * unanswered, and returns the moments raised. */
std::vector<Raised> runSeason(const GameController& controller,
                              GameDateValue start, int days, Inbox& inbox)
{
  const auto gamedata = controller.getGameData();
  InteractionSystem interactions(gamedata, [&controller](PlayerID id)
                                 { return controller.getSquadRole(id); });
  StoryEngine stories(gamedata);
  GameDateValue date = start;
  for (int day = 0; day < days; ++day)
  {
    date = SeasonCalendar::addDays(date, 1);
    stories.onDayAdvanced(date, managedId(controller), interactions, inbox,
                          weeklyRhythm(dayOrdinal(date)));
  }
  std::vector<Raised> raised;
  for (const Dilemma& dilemma : stories.getState().dilemmas)
    raised.emplace_back(dilemma.kind, dilemma.day, dilemma.subject);
  return raised;
}
}  // namespace

// ---------------------------------------------------------------------------
// Inbox filters
// ---------------------------------------------------------------------------

TEST(InboxViewTest, FiltersMatchCategoryUnreadEntityAndFollowed)
{
  InboxMessage message;
  message.category = InboxCategory::Transfer;
  message.player_id = 42;
  message.team_id = 7;
  const std::vector<PlayerID> followed = {42};
  const std::vector<PlayerID> nobody;

  InboxView view;
  EXPECT_TRUE(Inbox::matchesView(message, view, nobody));
  view.category =
      static_cast<std::int8_t>(std::to_underlying(InboxCategory::Injury));
  EXPECT_FALSE(Inbox::matchesView(message, view, nobody));
  view.category =
      static_cast<std::int8_t>(std::to_underlying(InboxCategory::Transfer));
  EXPECT_TRUE(Inbox::matchesView(message, view, nobody));

  view.unread_only = true;
  message.read = true;
  EXPECT_FALSE(Inbox::matchesView(message, view, nobody));
  message.read = false;
  EXPECT_TRUE(Inbox::matchesView(message, view, nobody));

  view.player_id = 43;
  EXPECT_FALSE(Inbox::matchesView(message, view, nobody));
  view.player_id = 42;
  view.team_id = 7;
  EXPECT_TRUE(Inbox::matchesView(message, view, nobody));
  view.team_id = 8;
  EXPECT_FALSE(Inbox::matchesView(message, view, nobody));
  view.team_id.reset();

  view.followed_only = true;
  EXPECT_FALSE(Inbox::matchesView(message, view, nobody));
  EXPECT_TRUE(Inbox::matchesView(message, view, followed));
  message.player_id.reset();
  view.player_id.reset();
  EXPECT_FALSE(Inbox::matchesView(message, view, followed));
}

TEST(InboxViewTest, FiltersFollowsAndMomentsSurviveSaveAndLoad)
{
  const SlotCleanup slot{uniqueSlot(1)};
  auto controller = makeCareer(slot.slot);
  const TeamID other = otherClub(*controller);
  ASSERT_NE(other, FREE_AGENTS_TEAM_ID);
  const PlayerID star =
      controller->getPlayersForTeam(other).front().get().getId();

  InboxView view;
  view.tab = 1;
  view.category =
      static_cast<std::int8_t>(std::to_underlying(InboxCategory::Transfer));
  view.unread_only = true;
  view.followed_only = true;
  view.team_id = other;
  controller->setInboxView(view);
  ASSERT_TRUE(controller->followPlayer(star));
  EXPECT_FALSE(controller->followPlayer(star)) << "already followed";
  EXPECT_FALSE(controller->followPlayer(999'999'999)) << "unknown player";

  // An open decision moment, raised today, with its message.
  StoryEngine& stories = controller->getGame()->getWorld().getStories();
  StoryState state = stories.getState();
  const std::int32_t today = dayOrdinal(controller->getCurrentDate());
  const PlayerID captain = controller->getPlayersForTeam(managedId(*controller))
                               .front()
                               .get()
                               .getId();
  state.dilemmas.emplace_back(StoryKind::FineDispute, captain, 0, today,
                              today + Stories::DILEMMA_ANSWER_DAYS);
  stories.restore(state);
  InboxMessage message;
  message.date = controller->getCurrentDate();
  message.title_key = Stories::dilemmaTitleKey(StoryKind::FineDispute);
  message.body_key = Stories::dilemmaBodyKey(StoryKind::FineDispute);
  message.args = {"A", "", ""};
  message.player_id = captain;
  controller->getGame()->getWorld().getInbox().add(message);
  ASSERT_TRUE(
      controller->isInboxDecisionPending(controller->getInbox().back()));
  controller->saveGame();

  auto reloaded = std::make_unique<GameController>();
  ASSERT_TRUE(reloaded->loadGame(slot.slot));
  EXPECT_EQ(reloaded->getInboxView(), view);
  EXPECT_TRUE(reloaded->isFollowingPlayer(star));
  EXPECT_EQ(reloaded->getFollowedPlayers(), std::vector<PlayerID>{star});
  const auto open = reloaded->getOpenDilemma();
  ASSERT_TRUE(open.has_value());
  EXPECT_EQ(open->kind, StoryKind::FineDispute);
  EXPECT_EQ(open->subject, captain);
  EXPECT_EQ(open->day, today);
  const auto found = std::ranges::find(reloaded->getInbox(), message.title_key,
                                       &InboxMessage::title_key);
  ASSERT_NE(found, reloaded->getInbox().end());
  // A copy: answering posts a note, which may move the inbox's messages.
  const InboxMessage pending = *found;
  EXPECT_TRUE(reloaded->isInboxDecisionPending(pending));

  // The answer is recorded and survives the next save too.
  ASSERT_TRUE(reloaded->resolveDilemma(1));
  EXPECT_FALSE(reloaded->isInboxDecisionPending(pending));
  EXPECT_TRUE(reloaded->unfollowPlayer(star));
  reloaded->saveGame();
  auto again = std::make_unique<GameController>();
  ASSERT_TRUE(again->loadGame(slot.slot));
  EXPECT_FALSE(again->getOpenDilemma().has_value());
  const auto& dilemmas =
      again->getGame()->getWorld().getStories().getState().dilemmas;
  ASSERT_EQ(dilemmas.size(), 1u);
  EXPECT_EQ(dilemmas.front().chosen, 1);
  EXPECT_EQ(dilemmas.front().resolved_day, today);
  EXPECT_TRUE(again->getFollowedPlayers().empty());
}

// ---------------------------------------------------------------------------
// Followed players
// ---------------------------------------------------------------------------

TEST(FollowTest, FollowedPlayersMakeNewsUntilUnfollowed)
{
  const SlotCleanup slot{uniqueSlot(2)};
  auto controller = makeCareer(slot.slot);
  const auto gamedata = controller->getGameData();
  const TeamID managed = managedId(*controller);
  const TeamID other = otherClub(*controller);
  ASSERT_NE(other, FREE_AGENTS_TEAM_ID);
  const auto& others = controller->getPlayersForTeam(other);
  ASSERT_GE(others.size(), 2u);
  const PlayerID star = others[0].get().getId();
  const PlayerID own =
      controller->getPlayersForTeam(managed).front().get().getId();
  InteractionSystem interactions(gamedata, [&controller](PlayerID id)
                                 { return controller->getSquadRole(id); });
  StoryEngine stories(gamedata);
  Inbox inbox;
  GameDateValue date(2025, 9, 2);
  ASSERT_TRUE(stories.follow(star, date));
  ASSERT_TRUE(stories.follow(own, date));

  // Match days throughout: no decision moment gets in the way.
  StoryDayContext match_day;
  match_day.days_to_match = 0;
  const auto day =
      [&date, &stories, managed, &interactions, &inbox, &match_day](int days)
  {
    date = SeasonCalendar::addDays(date, days);
    stories.onDayAdvanced(date, managed, interactions, inbox, match_day);
  };
  day(1);
  EXPECT_EQ(inbox.getMessages().size(),
            countTitle(inbox, "STORY_CAPTAIN_TITLE"))
      << "nothing happened yet";

  // Injury.
  Player& player = mutablePlayer(*controller, star);
  player.mutableDynamics().injury = InjuryType::HamstringStrain;
  player.mutableDynamics().injury_days = 20;
  day(1);
  EXPECT_EQ(countTitle(inbox, "INBOX_FOLLOW_INJURY_TITLE"), 1u);
  day(1);
  EXPECT_EQ(countTitle(inbox, "INBOX_FOLLOW_INJURY_TITLE"), 1u) << "once";
  player.mutableDynamics().injury_days = 0;

  // New terms at the same club.
  player.setWage(player.getWage() + 5'000);
  day(1);
  EXPECT_EQ(countTitle(inbox, "INBOX_FOLLOW_CONTRACT_TITLE"), 1u);

  // A big match anywhere in the world; one note a week at most.
  MatchReport report;
  report.date = date;
  report.home_team_id = other;
  report.away_team_id = managed;
  PlayerMatchLine line;
  line.player_id = star;
  line.team_id = other;
  line.minutes = 90;
  line.goals = 2;
  line.rating = 8.1f;
  report.players.push_back(line);
  stories.onMatchPlayed(report, managed, "WWWWW", inbox);
  EXPECT_EQ(countTitle(inbox, "INBOX_FOLLOW_MATCH_TITLE"), 1u);
  report.date = SeasonCalendar::addDays(date, 3);
  stories.onMatchPlayed(report, managed, "WWWWW", inbox);
  EXPECT_EQ(countTitle(inbox, "INBOX_FOLLOW_MATCH_TITLE"), 1u);
  // A quiet match is no news.
  report.date = SeasonCalendar::addDays(date, 10);
  report.players.front().goals = 0;
  report.players.front().rating = 6.8f;
  stories.onMatchPlayed(report, managed, "WWWWW", inbox);
  EXPECT_EQ(countTitle(inbox, "INBOX_FOLLOW_MATCH_TITLE"), 1u);

  // A move to another club.
  TeamID buyer = FREE_AGENTS_TEAM_ID;
  for (const auto& team : controller->getTeams())
    if (team.get().getId() != managed && team.get().getId() != other)
    {
      buyer = team.get().getId();
      break;
    }
  ASSERT_NE(buyer, FREE_AGENTS_TEAM_ID);
  player.setTeamId(buyer);
  day(1);
  EXPECT_EQ(countTitle(inbox, "INBOX_FOLLOW_TRANSFER_TITLE"), 1u);
  const auto note = std::ranges::find(
      inbox.getMessages(), std::string("INBOX_FOLLOW_TRANSFER_TITLE"),
      &InboxMessage::title_key);
  ASSERT_NE(note, inbox.getMessages().end());
  EXPECT_EQ(note->player_id, star);
  EXPECT_FALSE(note->read) << "followed news arrives unread";
  player.setTeamId(other);
  day(1);

  // The club's own players: their news comes through the club already.
  Player& mine = mutablePlayer(*controller, own);
  mine.mutableDynamics().injury_days = 15;
  day(1);
  mine.mutableDynamics().injury_days = 0;
  for (const InboxMessage& message : inbox.getMessages())
    EXPECT_FALSE(message.player_id == own &&
                 message.title_key.starts_with("INBOX_FOLLOW_"))
        << message.title_key;

  // Unfollowed: silence.
  ASSERT_TRUE(stories.unfollow(star));
  EXPECT_FALSE(stories.isFollowed(star));
  player.mutableDynamics().injury_days = 10;
  day(1);
  player.mutableDynamics().injury_days = 0;
  EXPECT_EQ(countTitle(inbox, "INBOX_FOLLOW_INJURY_TITLE"), 1u);
}

TEST(FollowTest, FollowingIsCapped)
{
  const SlotCleanup slot{uniqueSlot(3)};
  auto controller = makeCareer(slot.slot);
  StoryEngine stories(controller->getGameData());
  const GameDateValue date(2025, 9, 2);
  std::size_t followed = 0;
  for (const auto& [id, player] : controller->getGameData()->getPlayers())
  {
    if (followed == Stories::MAX_FOLLOWS) break;
    ASSERT_TRUE(stories.follow(id, date));
    ++followed;
  }
  ASSERT_EQ(followed, Stories::MAX_FOLLOWS);
  for (const auto& [id, player] : controller->getGameData()->getPlayers())
  {
    if (stories.isFollowed(id)) continue;
    EXPECT_FALSE(stories.follow(id, date));
    break;
  }
}

// ---------------------------------------------------------------------------
// Decision moments
// ---------------------------------------------------------------------------

TEST(DilemmaTest, EveryMomentHasKeysAndAnAction)
{
  const nlohmann::json english = loadLanguage("English.json");
  const nlohmann::json italian = loadLanguage("Italian.json");
  std::vector<std::string> keys = {"STORY_KIND_DILEMMA",
                                   "INBOX_DILEMMA_DONE_TITLE",
                                   "INBOX_DILEMMA_DONE",
                                   "INBOX_DILEMMA_EXPIRES",
                                   "INBOX_DILEMMA_EXPIRES_ONE",
                                   "INBOX_DILEMMA_EFFECT_MORALE",
                                   "INBOX_DILEMMA_EFFECT_TRUST",
                                   "INBOX_DILEMMA_EFFECT_SHARPNESS",
                                   "INBOX_DILEMMA_EFFECT_SQUAD",
                                   "INBOX_DILEMMA_EFFECT_INCOME",
                                   "INBOX_DILEMMA_EFFECT_COST",
                                   "INBOX_FOLLOW_NO_CLUB",
                                   "INBOX_FOLLOW_TRANSFER_TITLE",
                                   "INBOX_FOLLOW_TRANSFER_BODY",
                                   "INBOX_FOLLOW_RELEASED_TITLE",
                                   "INBOX_FOLLOW_RELEASED_BODY",
                                   "INBOX_FOLLOW_INJURY_TITLE",
                                   "INBOX_FOLLOW_INJURY_BODY",
                                   "INBOX_FOLLOW_MATCH_TITLE",
                                   "INBOX_FOLLOW_MATCH_BODY",
                                   "INBOX_FOLLOW_MATCH_BODY_GOALS",
                                   "INBOX_FOLLOW_AWARD_TITLE",
                                   "INBOX_FOLLOW_AWARD_BODY",
                                   "INBOX_FOLLOW_CONTRACT_TITLE",
                                   "INBOX_FOLLOW_CONTRACT_BODY"};
  for (const StoryKind kind : allDilemmas())
  {
    EXPECT_TRUE(Stories::isDilemma(kind));
    const std::string title = Stories::dilemmaTitleKey(kind);
    EXPECT_EQ(Inbox::actionFor(title), InboxAction::Dilemma) << title;
    EXPECT_EQ(Stories::dilemmaForTitle(title), kind) << title;
    keys.push_back(title);
    keys.emplace_back(Stories::dilemmaBodyKey(kind));
    for (const int option : {0, 1})
    {
      keys.emplace_back(Stories::dilemmaOptionKey(kind, option));
      keys.emplace_back(Stories::dilemmaDoneKey(kind, option));
    }
  }
  EXPECT_FALSE(Stories::isDilemma(StoryKind::Rivalry));
  EXPECT_FALSE(Stories::dilemmaForTitle("STORY_SAGA_TITLE").has_value());
  for (const std::string& key : keys)
  {
    ASSERT_FALSE(key.empty());
    EXPECT_TRUE(english.contains(key)) << key;
    EXPECT_TRUE(italian.contains(key)) << key;
  }
}

TEST(DilemmaTest, EffectsAreBoundedAndEveryAnswerCostsSomething)
{
  std::array<ClubProfile, 2> profiles{};
  profiles[1].reputation = 100;
  profiles[1].stadium_capacity = 90'000;
  profiles[1].ticket_price = 120;
  for (const ClubProfile& profile : profiles)
  {
    for (const StoryKind kind : allDilemmas())
    {
      std::array<bool, 2> any_cost{};
      for (const int option : {0, 1})
      {
        const DilemmaEffects effects =
            Stories::dilemmaEffects(kind, option, profile);
        for (const float morale : {effects.morale, effects.other_morale})
          EXPECT_LE(std::abs(morale), Stories::DILEMMA_MAX_MORALE);
        for (const float trust : {effects.trust, effects.other_trust})
          EXPECT_LE(std::abs(trust), Stories::DILEMMA_MAX_TRUST);
        EXPECT_LE(std::abs(effects.squad_morale),
                  Stories::DILEMMA_MAX_SQUAD_MORALE);
        EXPECT_LE(std::abs(effects.sharpness), Stories::DILEMMA_MAX_SHARPNESS);
        EXPECT_LE(std::abs(effects.money), Stories::DILEMMA_MAX_MONEY);
        any_cost[option] = effects.morale < 0 || effects.trust < 0 ||
                           effects.other_morale < 0 ||
                           effects.other_trust < 0 ||
                           effects.squad_morale < 0 || effects.sharpness < 0 ||
                           effects.money < 0;
      }
      // Turning down money is a cost too (the sponsor's fee).
      for (const int option : {0, 1})
        any_cost[option] =
            any_cost[option] ||
            Stories::dilemmaEffects(kind, 1 - option, profile).money > 0;
      // No free lunch: both answers have a downside.
      EXPECT_TRUE(any_cost[0]) << static_cast<int>(std::to_underlying(kind));
      EXPECT_TRUE(any_cost[1]) << static_cast<int>(std::to_underlying(kind));
    }
  }
  EXPECT_EQ(
      Stories::dilemmaEffects(StoryKind::FineDispute, 2, profiles[0]).morale,
      0.0f);
}

TEST(DilemmaTest, NeverOnMatchDayRareAndDeterministic)
{
  const SlotCleanup slot{uniqueSlot(4)};
  auto controller = makeCareer(slot.slot);
  Inbox first_inbox;
  const GameDateValue start(2025, 8, 1);
  const auto raised = runSeason(*controller, start, 365, first_inbox);
  ASSERT_GE(raised.size(), 3u) << "a season brings a few moments";
  // At most one a fortnight: a season holds no more than 27.
  EXPECT_LE(raised.size(), 27u);
  for (std::size_t index = 0; index < raised.size(); ++index)
  {
    const auto& [kind, day, subject] = raised[index];
    EXPECT_TRUE(Stories::isDilemma(kind));
    EXPECT_NE(weeklyRhythm(day).days_to_match, 0) << "match day " << day;
    if (index > 0)
      EXPECT_GE(day - std::get<1>(raised[index - 1]),
                Stories::DILEMMA_GAP_DAYS);
    for (std::size_t earlier = 0; earlier < index; ++earlier)
      if (std::get<0>(raised[earlier]) == kind)
        EXPECT_GE(day - std::get<1>(raised[earlier]),
                  Stories::DILEMMA_KIND_COOLDOWN_DAYS)
            << "kind " << static_cast<int>(std::to_underlying(kind))
            << " repeats too soon";
    if (kind == StoryKind::SponsorAppearance)
    {
      const int to_match = weeklyRhythm(day).days_to_match;
      EXPECT_TRUE(to_match >= 1 && to_match <= 3) << "just before a match";
    }
  }
  // Each moment posted one message that asks for a decision.
  std::size_t asked = 0;
  for (const InboxMessage& message : first_inbox.getMessages())
    if (Inbox::actionFor(message.title_key) == InboxAction::Dilemma) ++asked;
  EXPECT_EQ(asked, raised.size());

  // Same world, same days: the same moments about the same players.
  Inbox second_inbox;
  EXPECT_EQ(runSeason(*controller, start, 365, second_inbox), raised);

  // A club that plays every day never meets one.
  const auto gamedata = controller->getGameData();
  InteractionSystem interactions(gamedata, [&controller](PlayerID id)
                                 { return controller->getSquadRole(id); });
  StoryEngine busy(gamedata);
  Inbox busy_inbox;
  GameDateValue date = start;
  for (int day = 0; day < 365; ++day)
  {
    date = SeasonCalendar::addDays(date, 1);
    StoryDayContext context;
    context.days_to_match = 0;
    busy.onDayAdvanced(date, managedId(*controller), interactions, busy_inbox,
                       context);
  }
  EXPECT_TRUE(busy.getState().dilemmas.empty());
}

TEST(DilemmaTest, UnansweredMomentsLapseWithoutEffects)
{
  const SlotCleanup slot{uniqueSlot(5)};
  auto controller = makeCareer(slot.slot);
  const auto gamedata = controller->getGameData();
  const TeamID managed = managedId(*controller);
  InteractionSystem interactions(gamedata, [&controller](PlayerID id)
                                 { return controller->getSquadRole(id); });
  StoryEngine stories(gamedata);
  const PlayerID subject =
      controller->getPlayersForTeam(managed).front().get().getId();
  const float morale = gamedata->getPlayer(subject)->get().getDynamics().morale;
  GameDateValue date(2025, 10, 1);
  const std::int32_t today = dayOrdinal(date);
  StoryState state;
  state.dilemmas.emplace_back(StoryKind::CompassionateLeave, subject, 0, today,
                              today + Stories::DILEMMA_ANSWER_DAYS);
  stories.restore(state);
  Inbox inbox;
  for (int day = 0; day <= Stories::DILEMMA_ANSWER_DAYS; ++day)
  {
    date = SeasonCalendar::addDays(date, 1);
    StoryDayContext context;
    context.days_to_match = 0;  // No new moment in the way.
    stories.onDayAdvanced(date, managed, interactions, inbox, context);
  }
  EXPECT_EQ(stories.openDilemma(), nullptr);
  ASSERT_EQ(stories.getState().dilemmas.size(), 1u);
  EXPECT_EQ(stories.getState().dilemmas.front().chosen, 2);
  EXPECT_FLOAT_EQ(gamedata->getPlayer(subject)->get().getDynamics().morale,
                  morale);
  EXPECT_FALSE(stories.resolveDilemma(0, date, managed, interactions, inbox));
}

TEST(DilemmaTest, AnswersApplyTheEffectsShownAndAreRecorded)
{
  const SlotCleanup slot{uniqueSlot(6)};
  auto controller = makeCareer(slot.slot);
  const auto gamedata = controller->getGameData();
  const TeamID managed = managedId(*controller);
  Game* game = controller->getGame();
  StoryEngine& stories = game->getWorld().getStories();
  const auto& squad = controller->getPlayersForTeam(managed);
  ASSERT_GE(squad.size(), 3u);
  const PlayerID veteran = squad[0].get().getId();
  const PlayerID bystander = squad[2].get().getId();
  const std::int32_t today = dayOrdinal(controller->getCurrentDate());

  // A coaching course: player morale, trust, fitness and the ledger.
  Player& player = mutablePlayer(*controller, veteran);
  player.mutableDynamics().morale = 50.0f;
  player.mutableDynamics().sharpness = 70.0f;
  mutablePlayer(*controller, bystander).mutableDynamics().morale = 50.0f;
  StoryState state = stories.getState();
  state.dilemmas.emplace_back(StoryKind::CoachingCourse, veteran, 0, today,
                              today + Stories::DILEMMA_ANSWER_DAYS);
  stories.restore(state);
  const auto preview = controller->getDilemmaEffects(0);
  ASSERT_TRUE(preview.has_value());
  EXPECT_FALSE(controller->getDilemmaEffects(2).has_value());
  const float trust = controller->getPlayerRelation(veteran)
                          ? controller->getPlayerRelation(veteran)->trust
                          : 0.0f;
  const std::int64_t balance =
      controller->getManagedTeam()->get().getFinances().getBalance();
  ASSERT_TRUE(controller->resolveDilemma(0));
  EXPECT_FLOAT_EQ(player.getDynamics().morale, 50.0f + preview->morale);
  EXPECT_FLOAT_EQ(player.getDynamics().sharpness, 70.0f + preview->sharpness);
  ASSERT_NE(controller->getPlayerRelation(veteran), nullptr);
  EXPECT_FLOAT_EQ(controller->getPlayerRelation(veteran)->trust,
                  trust + preview->trust);
  const Finances& finances = controller->getManagedTeam()->get().getFinances();
  EXPECT_EQ(finances.getBalance(), balance + preview->money);
  ASSERT_FALSE(finances.getLedger().empty());
  EXPECT_EQ(finances.getLedger().back().category, FinanceCategory::Staff);
  EXPECT_EQ(finances.getLedger().back().amount, preview->money);
  EXPECT_FLOAT_EQ(gamedata->getPlayer(bystander)->get().getDynamics().morale,
                  50.0f)
      << "no squad effect for this answer";
  const Dilemma& recorded = stories.getState().dilemmas.back();
  EXPECT_EQ(recorded.chosen, 0);
  EXPECT_EQ(recorded.money, preview->money);
  const InboxMessage& note = controller->getInbox().back();
  EXPECT_EQ(note.title_key, "INBOX_DILEMMA_DONE_TITLE");
  EXPECT_EQ(note.body_key,
            Stories::dilemmaDoneKey(StoryKind::CoachingCourse, 0));
  EXPECT_TRUE(note.read);
  EXPECT_FALSE(controller->resolveDilemma(0)) << "answered once";

  // Fans' protest, held prices: the whole squad feels it.
  state = stories.getState();
  state.dilemmas.emplace_back(StoryKind::TicketProtest, 0, 0, today + 20,
                              today + 20 + Stories::DILEMMA_ANSWER_DAYS);
  stories.restore(state);
  const auto held = controller->getDilemmaEffects(1);
  ASSERT_TRUE(held.has_value());
  ASSERT_LT(held->squad_morale, 0.0f);
  ASSERT_TRUE(controller->resolveDilemma(1));
  EXPECT_FLOAT_EQ(gamedata->getPlayer(bystander)->get().getDynamics().morale,
                  50.0f + held->squad_morale);
  EXPECT_EQ(controller->getManagedTeam()->get().getFinances().getBalance(),
            balance + preview->money)
      << "holding prices costs no money";
}
