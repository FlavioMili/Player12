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
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
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
  return 300'000 + (getpid() % 100'000) * 10 + offset;
}

struct SlotCleanup
{
  int slot;
  explicit SlotCleanup(int slotValue) : slot(slotValue) {}
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;
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

/** Managed players ordered by overall, best first. */
std::vector<PlayerID> squadByOverall(const GameController& controller)
{
  std::vector<std::pair<double, PlayerID>> ranked;
  for (const auto& player : controller.getPlayersForTeam(managedId(controller)))
    ranked.emplace_back(player.get().getOverall(controller.getStatsConfig()),
                        player.get().getId());
  std::ranges::sort(ranked, std::greater<>());
  std::vector<PlayerID> ids;
  for (const auto& [overall, id] : ranked) ids.push_back(id);
  return ids;
}

Player& mutablePlayer(GameController& controller, PlayerID id)
{
  return controller.getGameData()->getPlayers().at(id);
}

TalkContext neutralContext()
{
  TalkContext context;
  context.role = SquadRole::FirstTeam;
  context.expected_rating = Interactions::expectedRating(context.role);
  context.expected_share =
      Interactions::expectedShare(context.role, context.age);
  context.playing_share = context.expected_share;
  context.rated_matches = 5;
  context.form = context.expected_rating;
  return context;
}

MatchReport reportFor(const GameDateValue& date, TeamID team_id,
                      TeamID opponent_id,
                      const std::vector<std::pair<PlayerID, int>>& lines,
                      MatchType type = MatchType::LEAGUE)
{
  MatchReport report;
  report.date = date;
  report.home_team_id = team_id;
  report.away_team_id = opponent_id;
  report.match_type = type;
  for (const auto& [player_id, minutes] : lines)
  {
    PlayerMatchLine line;
    line.player_id = player_id;
    line.team_id = team_id;
    line.minutes = static_cast<std::uint8_t>(minutes);
    report.players.push_back(line);
  }
  return report;
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

GameDateValue nextDays(const GameDateValue& date, int days)
{
  return SeasonCalendar::addDays(date, days);
}
}  // namespace

// ---------------------------------------------------------------------------
// Conversations (pure model)
// ---------------------------------------------------------------------------

TEST(InteractionsTest, PraiseLandsWhenEarned)
{
  TalkContext context = neutralContext();
  context.form = context.expected_rating + 0.8f;
  const float earned =
      Interactions::positiveChance(context, TalkOption::PraiseForm);
  context.form = context.expected_rating - 0.9f;
  context.traits.professionalism = 90;
  const float hollow =
      Interactions::positiveChance(context, TalkOption::PraiseForm);
  EXPECT_GT(earned, 0.8f);
  EXPECT_LT(hollow, 0.2f);
  EXPECT_GT(Interactions::negativeChance(context, TalkOption::PraiseForm),
            0.3f);
}

TEST(InteractionsTest, CriticismDependsOnPersonality)
{
  TalkContext context = neutralContext();
  context.form = context.expected_rating - 0.8f;
  context.traits.professionalism = 90;
  context.traits.temperament = 85;
  const float professional =
      Interactions::positiveChance(context, TalkOption::CriticiseForm);
  context.traits.professionalism = 20;
  context.traits.temperament = 15;
  const float volatile_player =
      Interactions::negativeChance(context, TalkOption::CriticiseForm);
  EXPECT_GT(professional, 0.7f);
  EXPECT_GT(volatile_player, 0.5f);
  // Criticising a player in form backfires on almost anyone.
  context.traits = PlayerTraits{};
  context.form = context.expected_rating + 1.0f;
  EXPECT_GT(Interactions::negativeChance(context, TalkOption::CriticiseForm),
            0.6f);
}

TEST(InteractionsTest, TransferTalkFollowsLoyaltyAndAmbition)
{
  TalkContext context = neutralContext();
  context.transfer_interest = true;
  context.traits.loyalty = 90;
  context.traits.ambition = 30;
  EXPECT_GT(Interactions::positiveChance(context, TalkOption::ReassureStay),
            0.7f);
  EXPECT_GT(Interactions::negativeChance(context, TalkOption::OpenToOffers),
            0.5f);
  context.traits.loyalty = 15;
  context.traits.ambition = 95;
  EXPECT_GT(Interactions::negativeChance(context, TalkOption::ReassureStay),
            0.5f);
  EXPECT_GT(Interactions::positiveChance(context, TalkOption::OpenToOffers),
            0.6f);
}

TEST(InteractionsTest, BrokenPromisesAreNotBelieved)
{
  TalkContext context = neutralContext();
  const float believed =
      Interactions::positiveChance(context, TalkOption::PromisePlayingTime);
  context.promise_credible = false;
  EXPECT_LT(
      Interactions::positiveChance(context, TalkOption::PromisePlayingTime),
      believed);
  EXPECT_GT(
      Interactions::negativeChance(context, TalkOption::PromisePlayingTime),
      0.5f);
}

TEST(InteractionsTest, OutcomesAreDeterministicBoundedAndLocalized)
{
  const nlohmann::json english = loadLanguage("English.json");
  const nlohmann::json italian = loadLanguage("Italian.json");
  TalkContext context = neutralContext();
  context.request = TalkRequest::Transfer;
  for (std::size_t index = 0;
       index < static_cast<std::size_t>(std::to_underlying(TalkOption::COUNT));
       ++index)
  {
    const auto option = static_cast<TalkOption>(index);
    for (const std::uint8_t trait : {5, 50, 95})
    {
      context.traits = {trait, trait, trait, trait, 50};
      for (const double roll : {0.0, 0.3, 0.6, 0.999})
      {
        for (const double variant : {0.0, 0.5, 0.999})
        {
          const TalkOutcome a =
              Interactions::evaluateTalk(context, option, roll, variant);
          const TalkOutcome b =
              Interactions::evaluateTalk(context, option, roll, variant);
          EXPECT_EQ(a.reaction, b.reaction);
          EXPECT_EQ(a.reply_key, b.reply_key);
          EXPECT_LE(std::abs(a.morale_delta), 12.0f);
          EXPECT_LE(std::abs(a.trust_delta), 10.0f);
          EXPECT_TRUE(english.contains(a.reply_key)) << a.reply_key;
          EXPECT_TRUE(italian.contains(a.reply_key)) << a.reply_key;
        }
      }
    }
    for (const char* key : {Interactions::optionKey(option),
                            Interactions::optionDescriptionKey(option)})
    {
      EXPECT_TRUE(english.contains(key)) << key;
      EXPECT_TRUE(italian.contains(key)) << key;
    }
  }
}

TEST(InteractionsTest, EveryLabelKeyExistsInBothLanguages)
{
  const nlohmann::json english = loadLanguage("English.json");
  const nlohmann::json italian = loadLanguage("Italian.json");
  std::vector<std::string> keys;
  for (const auto reaction :
       {TalkReaction::Positive, TalkReaction::Neutral, TalkReaction::Negative})
    keys.emplace_back(Interactions::reactionKey(reaction));
  for (const auto hint :
       {TalkHint::LikelyPositive, TalkHint::Uncertain, TalkHint::Risky})
    keys.emplace_back(Interactions::hintKey(hint));
  for (int block = 1;
       block <= static_cast<int>(std::to_underlying(TalkBlock::NotUnhappy));
       ++block)
    keys.emplace_back(Interactions::blockKey(static_cast<TalkBlock>(block)));
  for (const auto request : {TalkRequest::PlayingTime, TalkRequest::NewContract,
                             TalkRequest::Transfer})
  {
    keys.emplace_back(Interactions::requestKey(request));
    keys.emplace_back(std::string(Interactions::requestKey(request)) + "_BODY");
  }
  for (const auto type : {PromiseType::PlayingTime, PromiseType::NewContract,
                          PromiseType::Signing})
    keys.emplace_back(Interactions::promiseTypeKey(type));
  for (const auto state : {PromiseState::Active, PromiseState::Kept,
                           PromiseState::Broken, PromiseState::Voided})
    keys.emplace_back(Interactions::promiseStateKey(state));
  for (const auto reason :
       {PromiseVoidReason::PlayerLeft, PromiseVoidReason::LongInjury,
        PromiseVoidReason::TooFewMatches})
    keys.emplace_back(Interactions::voidReasonKey(reason));
  for (int tone = 0;
       tone < static_cast<int>(std::to_underlying(TeamTalkTone::COUNT)); ++tone)
  {
    keys.emplace_back(Interactions::toneKey(static_cast<TeamTalkTone>(tone)));
    keys.emplace_back(
        Interactions::toneDescriptionKey(static_cast<TeamTalkTone>(tone)));
  }
  for (const auto mood : {DressingMood::Buoyant, DressingMood::Settled,
                          DressingMood::Uneasy, DressingMood::Tense})
    keys.emplace_back(Interactions::moodKey(mood));
  for (int kind = 0;
       kind <= static_cast<int>(std::to_underlying(StoryKind::Rivalry)); ++kind)
    keys.emplace_back(Stories::kindKey(static_cast<StoryKind>(kind)));
  for (const char* key : {"TEAMTALK_SUMMARY_GOOD",
                          "TEAMTALK_SUMMARY_MIXED",
                          "TEAMTALK_SUMMARY_POOR",
                          "PROMISE_KEPT_TITLE",
                          "PROMISE_KEPT_BODY",
                          "PROMISE_BROKEN_TITLE",
                          "PROMISE_BROKEN_BODY",
                          "PROMISE_BROKEN_BODY_TRANSFER",
                          "PROMISE_VOIDED_TITLE",
                          "PROMISE_VOIDED_BODY",
                          "TALK_REQUEST_TITLE",
                          "TALK_ESCALATED_TITLE",
                          "TALK_ESCALATED_BODY",
                          "TALK_IGNORED_TITLE",
                          "TALK_IGNORED_BODY",
                          "STORY_DEBUT_TITLE",
                          "STORY_DEBUT_BODY",
                          "STORY_DEBUT_BODY_GOAL",
                          "STORY_BREAKOUT_TITLE",
                          "STORY_BREAKOUT_BODY",
                          "STORY_CAPTAIN_TITLE",
                          "STORY_CAPTAIN_BODY_DROPPED",
                          "STORY_CAPTAIN_BODY_UNHAPPY",
                          "STORY_POOR_RUN_TITLE",
                          "STORY_POOR_RUN_BODY",
                          "STORY_SAGA_TITLE",
                          "STORY_SAGA_BODY",
                          "STORY_COMEBACK_TITLE",
                          "STORY_COMEBACK_BODY",
                          "STORY_MILESTONE_TITLE",
                          "STORY_MILESTONE_APPS_BODY",
                          "STORY_MILESTONE_GOALS_BODY",
                          "STORY_RIVALRY_TITLE",
                          "STORY_RIVALRY_BODY_WIN",
                          "STORY_RIVALRY_BODY_DRAW",
                          "STORY_RIVALRY_BODY_LOSS"})
    keys.emplace_back(key);
  for (const std::string& key : keys)
  {
    EXPECT_TRUE(english.contains(key)) << key;
    EXPECT_TRUE(italian.contains(key)) << key;
    if (english.contains(key) && italian.contains(key))
      EXPECT_NE(english[key], italian[key]) << "untranslated " << key;
  }
}

// ---------------------------------------------------------------------------
// Team talks, cohesion and hierarchy (pure model)
// ---------------------------------------------------------------------------

TEST(InteractionsTest, TeamTalkToneFitsTheSituation)
{
  TeamTalkContext context;
  for (PlayerID id = 1; id <= 11; ++id)
    context.listeners.push_back(TalkListener{id, PlayerTraits{}, 60.0f});
  const auto share = [&](TeamTalkTone tone)
  {
    const auto predictions = Interactions::predictTeamTalk(context);
    return predictions[static_cast<std::size_t>(std::to_underlying(tone))]
               .positive_share -
           predictions[static_cast<std::size_t>(std::to_underlying(tone))]
               .negative_share;
  };
  context.expected_points = 0.7f;  // Underdog.
  EXPECT_GT(share(TeamTalkTone::NoPressure), share(TeamTalkTone::DemandMore));
  context.expected_points = 2.2f;  // Favourite.
  EXPECT_GT(share(TeamTalkTone::DemandMore), share(TeamTalkTone::NoPressure));
  context.moment = TeamTalkMoment::HalfTime;
  context.goal_difference = 1;
  EXPECT_GT(share(TeamTalkTone::Praise), share(TeamTalkTone::DemandMore));
  context.goal_difference = -1;
  EXPECT_GT(share(TeamTalkTone::DemandMore), share(TeamTalkTone::Praise));
  context.derby = true;
  const float calm_derby = share(TeamTalkTone::Calm);
  context.derby = false;
  EXPECT_GT(calm_derby, share(TeamTalkTone::Calm));
}

TEST(InteractionsTest, TeamTalkEffectIsBoundedAndDeterministic)
{
  TeamTalkContext context;
  context.day = 20'000;
  for (PlayerID id = 1; id <= 11; ++id)
  {
    PlayerTraits traits;
    traits.temperament = static_cast<std::uint8_t>(id * 8);
    context.listeners.push_back(TalkListener{id, traits, 50.0f});
  }
  for (int tone = 0;
       tone < static_cast<int>(std::to_underlying(TeamTalkTone::COUNT)); ++tone)
  {
    const auto a = Interactions::evaluateTeamTalk(
        context, static_cast<TeamTalkTone>(tone), WORLD_SEED);
    const auto b = Interactions::evaluateTeamTalk(
        context, static_cast<TeamTalkTone>(tone), WORLD_SEED);
    EXPECT_EQ(a.positive, b.positive);
    EXPECT_FLOAT_EQ(a.modifier, b.modifier);
    EXPECT_EQ(a.positive + a.neutral + a.negative, 11);
    EXPECT_LE(std::abs(a.modifier), Interactions::TALK_CAP);
    EXPECT_LE(std::abs(a.morale_delta), 4.0f);
    for (const auto& [id, delta] : a.morale_changes)
      EXPECT_LE(std::abs(delta), 4.0f);
  }
}

TEST(InteractionsTest, CohesionPenaltyFollowsNewSigningShare)
{
  EXPECT_FLOAT_EQ(Interactions::cohesionModifier(0.0f), 0.0f);
  EXPECT_FLOAT_EQ(Interactions::cohesionModifier(0.20f), 0.0f);
  EXPECT_FLOAT_EQ(Interactions::cohesionModifier(0.25f), 0.0f);
  EXPECT_NEAR(Interactions::cohesionModifier(0.325f), -0.01f, 1e-6f);
  EXPECT_NEAR(Interactions::cohesionModifier(0.40f), -0.02f, 1e-6f);
  EXPECT_NEAR(Interactions::cohesionModifier(0.60f), -0.02f, 1e-6f);
  EXPECT_NEAR(Interactions::cohesionModifier(1.0f), -0.02f, 1e-6f);
  // Newcomers settle over time.
  EXPECT_FLOAT_EQ(Interactions::newness(100, 100), 1.0f);
  EXPECT_GT(Interactions::newness(100, 150), Interactions::newness(100, 250));
  EXPECT_FLOAT_EQ(Interactions::newness(100, 100 + Interactions::SETTLE_DAYS),
                  0.0f);
  EXPECT_FLOAT_EQ(Interactions::newness(0, 500), 0.0f);
}

TEST(InteractionsTest, HierarchyRewardsTenureSeniorityAndStanding)
{
  const PlayerTraits traits;
  const float veteran = Interactions::hierarchyScore(5 * 365, 31, 0.8f, traits);
  const float newcomer = Interactions::hierarchyScore(10, 31, 0.8f, traits);
  const float youngster =
      Interactions::hierarchyScore(5 * 365, 19, 0.8f, traits);
  const float fringe = Interactions::hierarchyScore(5 * 365, 31, 0.1f, traits);
  EXPECT_GT(veteran, newcomer);
  EXPECT_GT(veteran, youngster);
  EXPECT_GT(veteran, fringe);
  EXPECT_LE(veteran, 100.0f);
  EXPECT_GE(Interactions::hierarchyScore(0, 16, 0.0f, PlayerTraits{0, 0, 0}),
            0.0f);
}

// ---------------------------------------------------------------------------
// Stories (pure helpers)
// ---------------------------------------------------------------------------

TEST(StoriesTest, MilestonesAndPoorRuns)
{
  EXPECT_EQ(Stories::appearanceMilestone(99, 100), 100u);
  EXPECT_FALSE(Stories::appearanceMilestone(100, 101));
  EXPECT_EQ(Stories::goalMilestone(49, 51), 50u);
  EXPECT_FALSE(Stories::goalMilestone(10, 11));
  EXPECT_TRUE(Stories::isPoorRun("LDLLD"));
  EXPECT_FALSE(Stories::isPoorRun("LDLWD"));
  EXPECT_FALSE(Stories::isPoorRun("LDL"));
}

// ---------------------------------------------------------------------------
// Conversations and promises in a career
// ---------------------------------------------------------------------------

TEST(InteractionsCareerTest, CooldownPreventsRepeatedTalks)
{
  const SlotCleanup slot{uniqueSlot(1)};
  auto controller = makeCareer(slot.slot);
  const PlayerID player_id = squadByOverall(*controller)[3];
  Player& player = mutablePlayer(*controller, player_id);
  player.pushMatchRating(7.8f);
  player.pushMatchRating(7.6f);

  const auto praise = [&]
  {
    const auto options = controller->getTalkOptions(player_id);
    return options[static_cast<std::size_t>(
        std::to_underlying(TalkOption::PraiseForm))];
  };
  EXPECT_EQ(praise().block, TalkBlock::None);
  const float morale_before = player.getDynamics().morale;
  const auto outcome =
      controller->talkToPlayer(player_id, TalkOption::PraiseForm);
  ASSERT_TRUE(outcome.has_value());
  EXPECT_FLOAT_EQ(
      player.getDynamics().morale,
      std::clamp(morale_before + outcome->morale_delta, 0.0f, 100.0f));
  EXPECT_FALSE(outcome->reply_key.empty());

  // Same topic group closed for two weeks, other topics still open.
  EXPECT_FALSE((controller->talkToPlayer(player_id, TalkOption::CriticiseForm)
                    .has_value()));
  EXPECT_EQ(praise().block, TalkBlock::Cooldown);
  EXPECT_EQ(praise().cooldown_days,
            Interactions::cooldownDays(TalkGroup::Form));
  EXPECT_EQ(controller
                ->getTalkOptions(player_id)[static_cast<std::size_t>(
                    TalkOption::DiscussPlayingTime)]
                .block,
            TalkBlock::None);
  for (int day = 0; day < Interactions::cooldownDays(TalkGroup::Form); ++day)
    controller->advanceDay();
  EXPECT_NE(praise().block, TalkBlock::Cooldown);

  // Other clubs' players cannot be spoken to.
  for (const auto& team : controller->getTeams())
  {
    if (team.get().getId() == managedId(*controller)) continue;
    const PlayerID other =
        controller->getPlayersForTeam(team.get().getId()).front().get().getId();
    EXPECT_FALSE(
        (controller->talkToPlayer(other, TalkOption::DiscussPlayingTime)
             .has_value()));
    break;
  }
}

TEST(InteractionsCareerTest, PlayingTimePromiseIsKeptOrBroken)
{
  const SlotCleanup slot{uniqueSlot(2)};
  auto controller = makeCareer(slot.slot);
  const auto gamedata = controller->getGameData();
  const TeamID managed = managedId(*controller);
  const auto squad = squadByOverall(*controller);
  InteractionSystem system(gamedata, [&controller](PlayerID id)
                           { return controller->getSquadRole(id); });
  Inbox inbox;
  const GameDateValue start(2025, 9, 1);
  const std::int32_t today = dayOrdinal(start);

  // Low ambition: a promise of minutes is always at least accepted.
  const PlayerID kept_id = squad[14];
  const PlayerID broken_id = squad[15];
  for (const PlayerID id : {kept_id, broken_id})
  {
    Player& player = mutablePlayer(*controller, id);
    PlayerTraits traits = player.getTraits();
    traits.ambition = 0;
    player.setTraits(traits);
    const auto outcome =
        system.talk(id, TalkOption::PromisePlayingTime, start, managed, 50.0f);
    ASSERT_TRUE(outcome.has_value());
    ASSERT_TRUE(outcome->promise.has_value());
  }
  ASSERT_EQ(system.promises().size(), 2u);
  // No second promise of the same kind while one is running.
  EXPECT_FALSE(
      system
          .talk(kept_id, TalkOption::PromisePlayingTime, start, managed, 50.0f)
          .has_value());

  const float kept_trust = system.relation(kept_id)->trust;
  const float broken_trust = system.relation(broken_id)->trust;
  const float kept_morale =
      mutablePlayer(*controller, kept_id).getDynamics().morale;
  for (int match = 0; match < 6; ++match)
  {
    const GameDateValue date = nextDays(start, 3 + match * 7);
    system.onMatchPlayed(
        reportFor(date, managed, managed + 1, {{kept_id, 90}, {squad[0], 90}}),
        managed);
    system.onDayAdvanced(date, managed, inbox);
  }
  const auto kept = system.promisesFor(kept_id).front();
  EXPECT_EQ(kept.state, PromiseState::Kept);
  EXPECT_FLOAT_EQ(kept.progress(), 1.0f);
  EXPECT_GT(system.relation(kept_id)->trust, kept_trust);
  EXPECT_GT(mutablePlayer(*controller, kept_id).getDynamics().morale,
            kept_morale - 3.0f);
  EXPECT_EQ(countTitle(inbox, "PROMISE_KEPT_TITLE"), 1u);

  // Nobody played the other one: broken exactly at the deadline.
  auto broken = system.promisesFor(broken_id).front();
  EXPECT_EQ(broken.state, PromiseState::Active);
  EXPECT_FLOAT_EQ(broken.progress(), 0.0f);
  system.onDayAdvanced(nextDays(start, broken.deadline_day - today - 1),
                       managed, inbox);
  EXPECT_EQ(system.promisesFor(broken_id).front().state, PromiseState::Active);
  system.onDayAdvanced(nextDays(start, broken.deadline_day - today), managed,
                       inbox);
  broken = system.promisesFor(broken_id).front();
  EXPECT_EQ(broken.state, PromiseState::Broken);
  EXPECT_LT(system.relation(broken_id)->trust, broken_trust);
  EXPECT_EQ(countTitle(inbox, "PROMISE_BROKEN_TITLE"), 1u);
  // A broken promise makes the next one unbelievable.
  const auto options = system.options(
      broken_id, nextDays(start, broken.deadline_day - today + 1), managed,
      50.0f);
  EXPECT_EQ(
      options[static_cast<std::size_t>(TalkOption::PromisePlayingTime)].block,
      TalkBlock::PromiseNotCredible);
}

TEST(InteractionsCareerTest, EveryPromiseResolvesByItsDeadline)
{
  const SlotCleanup slot{uniqueSlot(3)};
  auto controller = makeCareer(slot.slot);
  const auto gamedata = controller->getGameData();
  const TeamID managed = managedId(*controller);
  const auto squad = squadByOverall(*controller);
  InteractionSystem system(gamedata, [&controller](PlayerID id)
                           { return controller->getSquadRole(id); });
  Inbox inbox;
  const GameDateValue start(2025, 8, 20);  // Summer window still open.

  for (const PlayerID playerId : squad)
  {
    Player& player = mutablePlayer(*controller, playerId);
    PlayerTraits traits = player.getTraits();
    traits.ambition = 0;
    traits.loyalty = 100;
    player.setTraits(traits);
    player.setContractYears(1);
    for (const TalkOption option :
         {TalkOption::PromisePlayingTime, TalkOption::PromiseContract,
          TalkOption::PromiseSigning})
    {
      InteractionState state = system.getState();
      // Promises share a cooldown group: reset it to make all three.
      state.relations[playerId].last_talk.fill(0);
      system.restore(state);
      system.talk(playerId, option, start, managed, 50.0f);
    }
  }
  ASSERT_GT(system.promises().size(), squad.size());
  // Arbitrary minutes, renewals, an injury and a sale along the way.
  WorldRng rng(7);
  std::int32_t last_deadline = 0;
  for (const Promise& promise : system.promises())
    last_deadline = std::max(last_deadline, promise.deadline_day);
  mutablePlayer(*controller, squad[2]).mutableDynamics().injury_days = 90;
  for (GameDateValue date = start; dayOrdinal(date) <= last_deadline + 1;
       date = nextDays(date, 1))
  {
    if (dayOrdinal(date) % 4 == 0)
    {
      std::vector<std::pair<PlayerID, int>> lines;
      for (const PlayerID id : squad)
        if (rng.chance(0.5)) lines.emplace_back(id, rng.uniformInt(1, 90));
      system.onMatchPlayed(reportFor(date, managed, managed + 1, lines),
                           managed);
    }
    if (dayOrdinal(date) % 11 == 0)
    {
      Player& player = mutablePlayer(
          *controller, squad[static_cast<std::size_t>(rng.uniformInt(0, 10))]);
      player.setContractYears(3);
    }
    system.onDayAdvanced(date, managed, inbox);
  }
  for (const Promise& promise : system.promises())
  {
    EXPECT_NE(promise.state, PromiseState::Active)
        << "promise " << promise.id << " type "
        << static_cast<int>(std::to_underlying(promise.type));
    EXPECT_LE(promise.resolved_day, promise.deadline_day);
  }
  const auto injured = system.promisesFor(squad[2]);
  EXPECT_TRUE(std::ranges::any_of(
      injured, [](const Promise& promise)
      { return promise.void_reason == PromiseVoidReason::LongInjury; }));
}

TEST(InteractionsCareerTest, ContractAndSigningPromisesFollowRealEvents)
{
  const SlotCleanup slot{uniqueSlot(4)};
  auto controller = makeCareer(slot.slot);
  const auto gamedata = controller->getGameData();
  const TeamID managed = managedId(*controller);
  const auto squad = squadByOverall(*controller);
  InteractionSystem system(gamedata, [&controller](PlayerID id)
                           { return controller->getSquadRole(id); });
  Inbox inbox;
  const GameDateValue start(2025, 8, 10);

  Player& star = mutablePlayer(*controller, squad[0]);
  PlayerTraits traits = star.getTraits();
  traits.ambition = 50;
  traits.loyalty = 100;
  star.setTraits(traits);
  star.setContractYears(1);
  ASSERT_TRUE((system.talk(squad[0], TalkOption::PromiseContract, start,
                           managed, 60.0f))
                  .has_value());
  ASSERT_EQ(system.promises().size(), 1u);
  star.setContractYears(4);
  system.onDayAdvanced(nextDays(start, 1), managed, inbox);
  EXPECT_EQ(system.promises().front().state, PromiseState::Kept);

  // Signing: kept by a first-team level arrival, not by a fringe one.
  Player& captain = mutablePlayer(*controller, squad[1]);
  PlayerTraits ambitious = captain.getTraits();
  ambitious.ambition = 100;
  captain.setTraits(ambitious);
  ASSERT_TRUE(
      (system.talk(squad[1], TalkOption::PromiseSigning, start, managed, 90.0f))
          .has_value());
  const Promise signing = system.promisesFor(squad[1]).front();
  EXPECT_EQ(signing.type, PromiseType::Signing);
  EXPECT_GT(signing.deadline_day, dayOrdinal(start));

  std::vector<std::pair<double, PlayerID>> outsiders;
  for (const auto& [id, player] : gamedata->getPlayers())
    if (player.getTeamId() != managed && player.getTeamId() != 0)
      outsiders.emplace_back(player.getOverall(controller->getStatsConfig()),
                             id);
  std::ranges::sort(outsiders);
  const PlayerID weak = outsiders.front().second;
  const PlayerID strong = outsiders.back().second;
  gamedata->transferPlayer(weak, managed);
  system.onTransferCompleted(nextDays(start, 2), weak, 0, managed, managed,
                             inbox);
  system.onDayAdvanced(nextDays(start, 2), managed, inbox);
  EXPECT_EQ(system.promisesFor(squad[1]).front().state, PromiseState::Active);
  gamedata->transferPlayer(strong, managed);
  system.onTransferCompleted(nextDays(start, 3), strong, 0, managed, managed,
                             inbox);
  system.onDayAdvanced(nextDays(start, 3), managed, inbox);
  EXPECT_EQ(system.promisesFor(squad[1]).front().state, PromiseState::Kept);
  // The arrival is recorded for cohesion.
  EXPECT_EQ(system.relation(strong)->joined_day,
            dayOrdinal(nextDays(start, 3)));
}

TEST(InteractionsCareerTest, SellingAPromisedPlayerVoidsSilently)
{
  const SlotCleanup slot{uniqueSlot(5)};
  auto controller = makeCareer(slot.slot);
  const auto gamedata = controller->getGameData();
  const TeamID managed = managedId(*controller);
  const PlayerID player_id = squadByOverall(*controller)[16];
  InteractionSystem system(gamedata, [&controller](PlayerID id)
                           { return controller->getSquadRole(id); });
  Inbox inbox;
  const GameDateValue start(2025, 8, 10);
  Player& player = mutablePlayer(*controller, player_id);
  PlayerTraits traits = player.getTraits();
  traits.ambition = 0;
  player.setTraits(traits);
  ASSERT_TRUE((system.talk(player_id, TalkOption::PromisePlayingTime, start,
                           managed, 50.0f))
                  .has_value());
  const TeamID buyer = managed == 1 ? 2 : 1;
  gamedata->transferPlayer(player_id, buyer);
  system.onTransferCompleted(nextDays(start, 1), player_id, managed, buyer,
                             managed, inbox);
  const Promise promise = system.promisesFor(player_id).front();
  EXPECT_EQ(promise.state, PromiseState::Voided);
  EXPECT_EQ(promise.void_reason, PromiseVoidReason::PlayerLeft);
  EXPECT_TRUE(inbox.getMessages().empty());
}

TEST(InteractionsCareerTest, UnhappyPlayerRequestIsAnsweredOrLapses)
{
  const SlotCleanup slot{uniqueSlot(6)};
  auto controller = makeCareer(slot.slot);
  const auto gamedata = controller->getGameData();
  const TeamID managed = managedId(*controller);
  const auto squad = squadByOverall(*controller);
  InteractionSystem system(gamedata, [&controller](PlayerID id)
                           { return controller->getSquadRole(id); });
  Inbox inbox;
  for (const PlayerID id : squad)
    mutablePlayer(*controller, id).mutableDynamics().morale = 70.0f;
  const PlayerID star = squad[0];
  Player& player = mutablePlayer(*controller, star);
  PlayerTraits traits = player.getTraits();
  traits.ambition = 80;
  player.setTraits(traits);
  // A key player who has not been playing and is miserable.
  GameDateValue date(2025, 9, 1);
  while (dayOrdinal(date) % 7 != 0) date = nextDays(date, 1);
  const auto unhappy = [&]
  {
    player.mutableDynamics().morale = 20.0f;
    player.mutableDynamics().playing_share = 0.1f;
  };
  for (int week = 0; week < 2; ++week)
  {
    unhappy();
    system.onWeek(date, managed, inbox);
    EXPECT_EQ(system.relation(star)->request, TalkRequest::None);
    date = nextDays(date, 7);
  }
  unhappy();
  system.onWeek(date, managed, inbox);
  ASSERT_EQ(system.relation(star)->request, TalkRequest::PlayingTime);
  EXPECT_EQ(countTitle(inbox, "TALK_REQUEST_TITLE"), 1u);
  EXPECT_EQ(system.pendingRequests(), std::vector<PlayerID>{star});
  EXPECT_LT(system.moraleTargetOffset(star), 0.0f);
  // The request opens the answering options and marks them as suggested.
  const auto options = system.options(star, date, managed, 50.0f);
  EXPECT_TRUE(
      options[static_cast<std::size_t>(TalkOption::AskPatience)].suggested);
  EXPECT_EQ(options[static_cast<std::size_t>(TalkOption::AskPatience)].block,
            TalkBlock::None);

  // Ignored for three weeks it escalates to a transfer request, then lapses.
  for (int week = 0; week < 3; ++week)
  {
    date = nextDays(date, 7);
    unhappy();
    system.onWeek(date, managed, inbox);
  }
  EXPECT_EQ(system.relation(star)->request, TalkRequest::Transfer);
  EXPECT_EQ(countTitle(inbox, "TALK_ESCALATED_TITLE"), 1u);
  const auto transfer_options = system.options(star, date, managed, 50.0f);
  EXPECT_EQ(transfer_options[static_cast<std::size_t>(
                                 TalkOption::AcceptTransferRequest)]
                .block,
            TalkBlock::None);
  for (int week = 0; week < 3; ++week)
  {
    date = nextDays(date, 7);
    unhappy();
    system.onWeek(date, managed, inbox);
  }
  EXPECT_EQ(system.relation(star)->request, TalkRequest::None);
  EXPECT_EQ(countTitle(inbox, "TALK_IGNORED_TITLE"), 1u);
  // Quiet afterwards: no immediate new request.
  date = nextDays(date, 7);
  unhappy();
  system.onWeek(date, managed, inbox);
  EXPECT_EQ(system.relation(star)->request, TalkRequest::None);
}

TEST(InteractionsCareerTest, AcceptingATransferRequestListsThePlayer)
{
  const SlotCleanup slot{uniqueSlot(7)};
  auto controller = makeCareer(slot.slot);
  const PlayerID player_id = squadByOverall(*controller)[2];
  InteractionSystem& system =
      controller->getGame()->getWorld().getInteractions();
  InteractionState state = system.getState();
  state.relations[player_id].request = TalkRequest::Transfer;
  state.relations[player_id].request_day =
      dayOrdinal(controller->getCurrentDate());
  system.restore(state);
  EXPECT_TRUE(controller->hasPendingTalk(player_id));
  const auto outcome =
      controller->talkToPlayer(player_id, TalkOption::AcceptTransferRequest);
  ASSERT_TRUE(outcome.has_value());
  EXPECT_TRUE(outcome->list_player);
  EXPECT_TRUE(controller->isPlayerListed(player_id));
  EXPECT_FALSE(controller->hasPendingTalk(player_id));
}

// ---------------------------------------------------------------------------
// Team talks and dressing room in a career
// ---------------------------------------------------------------------------

TEST(InteractionsCareerTest, TeamTalkOncePerMomentAndBoundedModifier)
{
  const SlotCleanup slot{uniqueSlot(8)};
  auto controller = makeCareer(slot.slot);
  const TeamID managed = managedId(*controller);
  const TeamTalkContext context =
      controller->getTeamTalkContext(TeamTalkMoment::PreMatch);
  EXPECT_FALSE(context.listeners.empty());
  ASSERT_TRUE(controller->canGiveTeamTalk(TeamTalkMoment::PreMatch));
  const auto result = controller->giveTeamTalk(TeamTalkMoment::PreMatch,
                                               TeamTalkTone::Motivate);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(static_cast<std::size_t>(result->positive + result->neutral +
                                     result->negative),
            context.listeners.size());
  EXPECT_FALSE(controller->canGiveTeamTalk(TeamTalkMoment::PreMatch));
  EXPECT_FALSE(
      controller->giveTeamTalk(TeamTalkMoment::PreMatch, TeamTalkTone::Calm)
          .has_value());
  EXPECT_FLOAT_EQ(controller->getTeamTalkModifier(managed, 1),
                  result->modifier);
  ASSERT_TRUE(controller
                  ->giveTeamTalk(TeamTalkMoment::HalfTime,
                                 TeamTalkTone::DemandMore, 0, 2)
                  .has_value());
  for (const int half : {1, 2})
  {
    EXPECT_LE(std::abs(controller->getTeamTalkModifier(managed, half)),
              Interactions::TALK_CAP);
    EXPECT_LE(std::abs(controller->getHumanFactorModifier(managed, half)),
              Interactions::TOTAL_CAP);
  }
  // Other days and clubs are unaffected.
  EXPECT_FLOAT_EQ(controller->getTeamTalkModifier(managed + 1, 1), 0.0f);
  controller->advanceDay();
  EXPECT_FLOAT_EQ(controller->getTeamTalkModifier(managed, 1), 0.0f);
  EXPECT_TRUE(controller->canGiveTeamTalk(TeamTalkMoment::PreMatch));
}

TEST(InteractionsCareerTest, DressingRoomSummarisesTheSquad)
{
  const SlotCleanup slot{uniqueSlot(9)};
  auto controller = makeCareer(slot.slot);
  const DressingRoom room = controller->getDressingRoom();
  ASSERT_EQ(room.leaders.size(), 3u);
  EXPECT_TRUE(room.leaders.front().captain);
  EXPECT_FALSE(room.leaders[1].captain);
  EXPECT_GE(room.leaders[0].score, room.leaders[1].score);
  EXPECT_GE(room.cohesion, 0.0f);
  EXPECT_LE(room.cohesion, 1.0f);
  EXPECT_FLOAT_EQ(room.cohesion_modifier, 0.0f);  // Nobody new on day one.
  EXPECT_GT(room.team_morale, 0.0f);
  for (const SocialGroup& group : room.groups)
    EXPECT_GE(group.members.size(), 3u);

  // A wave of signings into the XI hurts cohesion, within the cap.
  const auto gamedata = controller->getGameData();
  const TeamID managed = managedId(*controller);
  InteractionSystem& system =
      controller->getGame()->getWorld().getInteractions();
  Inbox inbox;
  const Lineup& lineup = controller->getManagedTeam()->get().getLineup();
  std::vector<PlayerID> starters;
  for (const auto& positioned : lineup.getOutfieldPlayers())
    starters.push_back(positioned.player->getId());
  for (std::size_t index = 0; index < 6 && index < starters.size(); ++index)
    system.onTransferCompleted(controller->getCurrentDate(), starters[index], 0,
                               managed, managed, inbox);
  const DressingRoom unsettled = controller->getDressingRoom();
  EXPECT_GT(unsettled.new_share, 0.4f);
  EXPECT_NEAR(unsettled.cohesion_modifier, -Interactions::TOTAL_CAP, 1e-6f);
}

// ---------------------------------------------------------------------------
// Stories
// ---------------------------------------------------------------------------

TEST(StoriesTest, EventsTellEachStoryOnceWithinTheWeeklyCap)
{
  const SlotCleanup slot{uniqueSlot(10)};
  auto controller = makeCareer(slot.slot);
  const auto gamedata = controller->getGameData();
  const TeamID managed = managedId(*controller);
  const auto squad = squadByOverall(*controller);
  InteractionSystem interactions(gamedata, [&controller](PlayerID id)
                                 { return controller->getSquadRole(id); });
  StoryEngine stories(gamedata);
  std::unordered_map<PlayerID, CareerTotals> careers;
  stories.setCareerProvider([&careers](PlayerID id) { return careers[id]; });
  Inbox inbox;

  const PlayerID youngster = squad[20];
  const PlayerID veteran = squad[1];
  mutablePlayer(*controller, youngster).setAge(18);
  careers[youngster] = {0, 0};
  careers[veteran] = {99, 49};
  GameDateValue date(2025, 9, 6);
  auto report =
      reportFor(date, managed, managed + 1, {{youngster, 30}, {veteran, 90}});
  report.players[1].goals = 1;
  stories.onMatchPlayed(report, managed, "WDWDW", inbox);
  EXPECT_EQ(countTitle(inbox, "STORY_DEBUT_TITLE"), 1u);
  // 100 appearances and 50 goals in one match: two milestones, but the
  // weekly allowance (debut included) holds the second back.
  EXPECT_EQ(countTitle(inbox, "STORY_MILESTONE_TITLE"),
            static_cast<std::size_t>(Stories::WEEKLY_CAP - 1));

  // Replaying the week tells nothing twice.
  const std::size_t before = inbox.getMessages().size();
  date = nextDays(date, 8);
  stories.onMatchPlayed(
      reportFor(date, managed, managed + 1, {{youngster, 90}, {veteran, 90}}),
      managed, "WDWDW", inbox);
  EXPECT_EQ(inbox.getMessages().size(), before);

  // Poor run: once, then a cooldown.
  date = nextDays(date, 8);
  stories.onMatchPlayed(reportFor(date, managed, managed + 1, {}), managed,
                        "LDLLD", inbox);
  EXPECT_EQ(countTitle(inbox, "STORY_POOR_RUN_TITLE"), 1u);
  date = nextDays(date, 8);
  stories.onMatchPlayed(reportFor(date, managed, managed + 1, {}), managed,
                        "LLDLL", inbox);
  EXPECT_EQ(countTitle(inbox, "STORY_POOR_RUN_TITLE"), 1u);

  // Friendlies do not count towards debuts or milestones.
  const PlayerID newcomer = squad[21];
  mutablePlayer(*controller, newcomer).setAge(17);
  date = nextDays(date, 8);
  stories.onMatchPlayed(reportFor(date, managed, managed + 1, {{newcomer, 90}},
                                  MatchType::FRIENDLY),
                        managed, "WWWWW", inbox);
  EXPECT_EQ(countTitle(inbox, "STORY_DEBUT_TITLE"), 1u);
}

TEST(StoriesTest, TransferSagaOffersAChoiceAndComebacksAreTold)
{
  const SlotCleanup slot{uniqueSlot(11)};
  auto controller = makeCareer(slot.slot);
  const auto gamedata = controller->getGameData();
  const TeamID managed = managedId(*controller);
  const auto squad = squadByOverall(*controller);
  InteractionSystem interactions(gamedata, [&controller](PlayerID id)
                                 { return controller->getSquadRole(id); });
  StoryEngine stories(gamedata);
  Inbox inbox;
  const PlayerID star = squad[0];
  const TeamID bidder = managed == 1 ? 2 : 1;
  GameDateValue date(2025, 8, 5);
  stories.onTransferBid(date, star, bidder, true, managed, inbox);
  EXPECT_EQ(countTitle(inbox, "STORY_SAGA_TITLE"), 0u);
  stories.onTransferBid(nextDays(date, 10), star, bidder, true, managed, inbox);
  EXPECT_EQ(countTitle(inbox, "STORY_SAGA_TITLE"), 1u);
  const std::int32_t day = dayOrdinal(nextDays(date, 10));
  ASSERT_TRUE(stories.choiceFor(star, day).has_value());
  EXPECT_EQ(stories.choiceFor(star, day)->kind, StoryKind::TransferSaga);
  stories.onTransferBid(nextDays(date, 12), star, bidder, true, managed, inbox);
  EXPECT_EQ(countTitle(inbox, "STORY_SAGA_TITLE"), 1u);
  stories.resolveChoice(star);
  EXPECT_FALSE(stories.choiceFor(star, day).has_value());
  // Bids for squad players do not make sagas.
  stories.onTransferBid(date, squad[18], bidder, false, managed, inbox);
  stories.onTransferBid(nextDays(date, 1), squad[18], bidder, false, managed,
                        inbox);
  EXPECT_EQ(countTitle(inbox, "STORY_SAGA_TITLE"), 1u);

  // A long injury ends: the comeback is told at the next appearance.
  const PlayerID injured = squad[5];
  Player& player = mutablePlayer(*controller, injured);
  player.mutableDynamics().injury_days = 70;
  date = GameDateValue(2025, 9, 1);
  stories.onDayAdvanced(date, managed, interactions, inbox);
  player.mutableDynamics().injury_days = 0;
  date = nextDays(date, 70);
  stories.onDayAdvanced(date, managed, interactions, inbox);
  EXPECT_EQ(countTitle(inbox, "STORY_COMEBACK_TITLE"), 0u);
  stories.onMatchPlayed(reportFor(date, managed, managed + 1, {{injured, 25}}),
                        managed, "WDWDW", inbox);
  EXPECT_EQ(countTitle(inbox, "STORY_COMEBACK_TITLE"), 1u);
}

TEST(StoriesTest, RivalIsAStableSameLeagueClub)
{
  const SlotCleanup slot{uniqueSlot(12)};
  auto controller = makeCareer(slot.slot);
  const auto gamedata = controller->getGameData();
  const TeamID managed = managedId(*controller);
  StoryEngine stories(gamedata);
  const auto rival = stories.rivalOf(managed);
  if (!rival.has_value())
    GTEST_SKIP() << "no club of similar stature in the league";
  EXPECT_NE(*rival, managed);
  EXPECT_EQ(gamedata->getTeam(*rival)->get().getLeagueId(),
            gamedata->getTeam(managed)->get().getLeagueId());
  EXPECT_EQ(stories.rivalOf(managed), rival);
}

// ---------------------------------------------------------------------------
// Persistence and a live career
// ---------------------------------------------------------------------------

TEST(InteractionsCareerTest, RelationsPromisesAndStoriesSurviveSaveAndLoad)
{
  const SlotCleanup slot{uniqueSlot(13)};
  auto controller = makeCareer(slot.slot);
  const auto squad = squadByOverall(*controller);
  const PlayerID player_id = squad[12];
  Player& player = mutablePlayer(*controller, player_id);
  PlayerTraits traits = player.getTraits();
  traits.ambition = 0;
  player.setTraits(traits);
  ASSERT_TRUE(
      (controller->talkToPlayer(player_id, TalkOption::PromisePlayingTime))
          .has_value());
  ASSERT_TRUE(
      (controller->talkToPlayer(player_id, TalkOption::DiscussPlayingTime))
          .has_value());
  auto* game = controller->getGame();
  StoryEngine& stories = game->getWorld().getStories();
  StoryState story_state = stories.getState();
  story_state.records.push_back(
      StoryRecord{StoryKind::Breakout, player_id, 2025, 20'000, true});
  story_state.choices.push_back(
      StoryChoice{StoryKind::CaptainDispute, player_id, 20'000, 90'000});
  story_state.injury_start.emplace(squad[3], 19'990);
  story_state.comeback_due.emplace(squad[4], 75);
  story_state.bids.emplace_back(squad[0], 19'995);
  stories.restore(story_state);

  const PlayerRelation relation = *controller->getPlayerRelation(player_id);
  const auto promises = controller->getPlayerPromises(player_id);
  ASSERT_EQ(promises.size(), 1u);
  controller->saveGame();

  auto reloaded = std::make_unique<GameController>();
  ASSERT_TRUE(reloaded->loadGame(slot.slot));
  const PlayerRelation* restored = reloaded->getPlayerRelation(player_id);
  ASSERT_NE(restored, nullptr);
  EXPECT_FLOAT_EQ(restored->trust, relation.trust);
  EXPECT_EQ(restored->last_talk, relation.last_talk);
  EXPECT_EQ(restored->request, relation.request);
  const auto restored_promises = reloaded->getPlayerPromises(player_id);
  ASSERT_EQ(restored_promises.size(), 1u);
  EXPECT_EQ(restored_promises[0].id, promises[0].id);
  EXPECT_EQ(restored_promises[0].type, promises[0].type);
  EXPECT_EQ(restored_promises[0].deadline_day, promises[0].deadline_day);
  EXPECT_FLOAT_EQ(restored_promises[0].target, promises[0].target);
  // Cooldowns survive: the same talk is still closed.
  EXPECT_FALSE(
      (reloaded->talkToPlayer(player_id, TalkOption::DiscussPlayingTime))
          .has_value());
  const StoryState& loaded =
      reloaded->getGame()->getWorld().getStories().getState();
  EXPECT_EQ(loaded.records.size(), story_state.records.size());
  EXPECT_EQ(loaded.choices.size(), 1u);
  EXPECT_EQ(loaded.injury_start.at(squad[3]), 19'990);
  EXPECT_EQ(loaded.comeback_due.at(squad[4]), 75);
  EXPECT_EQ(loaded.bids.size(), 1u);
  EXPECT_TRUE(reloaded->getStoryChoice(player_id).has_value());
}

TEST(InteractionsCareerTest, ASeasonStartStaysCalmAndConsistent)
{
  const SlotCleanup slot{uniqueSlot(14)};
  auto controller = makeCareer(slot.slot);
  const TeamID managed = managedId(*controller);
  while (controller->getCurrentDate() < GameDateValue(2025, 10, 1))
    controller->advanceDay();
  std::map<std::int32_t, int> stories_by_week;
  int requests = 0;
  for (const InboxMessage& message : controller->getInbox())
  {
    if (message.title_key.starts_with("STORY_"))
      ++stories_by_week[dayOrdinal(message.date) / 7];
    if (message.title_key == "TALK_REQUEST_TITLE") ++requests;
  }
  for (const auto& [week, count] : stories_by_week)
    EXPECT_LE(count, Stories::WEEKLY_CAP + 1) << "week " << week;
  EXPECT_LE(requests, 4);  // At most one every three weeks.
  // Every promise or request refers to a managed player.
  for (const PlayerID id :
       controller->getGame()->getWorld().getInteractions().pendingRequests())
    EXPECT_EQ(controller->getGameData()->getPlayer(id)->get().getTeamId(),
              managed);
  const DressingRoom room = controller->getDressingRoom();
  EXPECT_EQ(room.leaders.size(), 3u);
  EXPECT_LE(std::abs(room.cohesion_modifier), Interactions::TOTAL_CAP);
}
