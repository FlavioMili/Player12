// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// World news feed, draw ceremonies and the career timeline: stories and
// entries built from synthetic saved facts, then from a seeded world after
// six weeks of football (identical after a save and reload).

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/awards.h"
#include "model/calendar.h"
#include "model/career_timeline.h"
#include "model/competition.h"
#include "model/continental.h"
#include "model/draw_ceremony.h"
#include "model/game.h"
#include "model/inbox.h"
#include "model/manager_career.h"
#include "model/news_feed.h"
#include "model/records.h"
#include "model/season_history.h"
#include "model/transfer_market.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 6'400'000 + getpid() % 100'000 * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

/** Two linked divisions of four clubs with a few named players. */
class SyntheticWorld : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    Logger::init();
    ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::EN));
    gamedata.addLeague(1, League(1, "Top League", {1, 2, 3, 4}));
    gamedata.addLeague(2, League(2, "Second League", {5, 6, 7, 8}, 1));
    const std::array<const char*, 8> names = {
        "Acaya",   "Foggia",   "Lecce",     "Bari",
        "Taranto", "Brindisi", "Cerignola", "Monopoli"};
    for (TeamID id = 1; id <= 8; ++id)
    {
      Team team(id, id <= 4 ? 1 : 2, names[id - 1U], 1'000'000);
      ClubProfile profile;
      profile.reputation =
          static_cast<std::uint8_t>(id <= 4 ? 100 - id * 15 : 40);
      team.setProfile(profile);
      gamedata.addTeam(id, team);
    }
    for (PlayerID id = 1; id <= 4; ++id)
      gamedata.addPlayer(
          id, Player(id, static_cast<TeamID>(id), "Nico",
                     std::format("Player{}", id), PlayerRole::ST, Language::IT,
                     10'000, 0, 24, 3, 180, Foot::Right, {}));
  }

  GameData gamedata;
};

std::string describe(const std::vector<NewsItem>& items)
{
  std::ostringstream out;
  for (const NewsItem& item : items)
    out << item.date.toString() << " "
        << static_cast<int>(std::to_underlying(item.kind)) << " "
        << item.headline() << "\n";
  return out.str();
}
}  // namespace

// ---- News feed
// ---------------------------------------------------------------

TEST_F(SyntheticWorld, HeadlinesComeFromSavedFactsAndCiteTheirEntities)
{
  std::vector<TransferRecord> transfers = {
      {1, GameDateValue(2025, 8, 10), 2, 1, 20'000'000,
       TransferKind::Permanent},
      // A small deal between two other clubs is not news...
      {2, GameDateValue(2025, 8, 11), 2, 4, 900'000, TransferKind::Permanent},
      // ...but every signing of the managed club is.
      {3, GameDateValue(2025, 8, 12), 5, 3, 0, TransferKind::Free},
      {4, GameDateValue(2025, 8, 13), 1, 4, 0, TransferKind::LoanReturn},
  };
  ManagerStint stint;
  stint.team_id = 3;
  stint.club_name = "Lecce";
  stint.league_id = 1;
  stint.start = GameDateValue(2025, 7, 1);
  std::vector<ManagerStint> stints = {stint};
  AiManager appointed;
  appointed.id = 7;
  appointed.first_name = "Rino";
  appointed.last_name = "Mister";
  appointed.team_id = 2;
  appointed.appointed = GameDateValue(2025, 11, 20);
  AiManager veteran = appointed;
  veteran.id = 8;
  veteran.team_id = 4;
  veteran.appointed = GameDateValue(2019, 7, 1);  // Before the save began.
  std::vector<AiManager> managers = {appointed, veteran};
  std::vector<Vacancy> vacancies = {
      {6, GameDateValue(2025, 12, 1), GameDateValue(2026, 1, 1)}};
  InboxMessage dismissal;
  dismissal.date = GameDateValue(2025, 11, 2);
  dismissal.title_key = "INBOX_MANAGER_NEWS_TITLE";
  dismissal.args = {"Gino Panchina", "Foggia"};
  dismissal.team_id = 2;
  std::vector<InboxMessage> inbox = {dismissal};
  AwardRecord month;
  month.season_year = 2025;
  month.month = 9;
  month.league_id = 1;
  month.type = AwardType::PlayerOfMonth;
  month.player_id = 1;
  month.team_id = 1;
  month.name = "Nico Player1";
  std::vector<AwardRecord> awards = {month};
  SeasonHistoryEntry season;
  season.start_year = 2025;
  season.competition_type = MatchType::LEAGUE;
  season.competition_id = 2;
  season.champion_id = 5;
  season.promoted = {5};
  std::vector<SeasonHistoryEntry> history = {season};
  RecordEntry record;
  record.scope = RecordScope::League;
  record.scope_id = 1;
  record.kind = RecordKind::BiggestWin;
  record.value = 6;
  record.team_id = 1;
  record.opponent_id = 4;
  record.goals_for = 6;
  record.date = GameDateValue(2025, 10, 4);
  record.season_year = 2025;
  std::vector<RecordEntry> records = {record};

  NewsSources sources;
  sources.gamedata = &gamedata;
  sources.transfers = transfers;
  sources.stints = stints;
  sources.manager_name = "Flavio Coach";
  sources.managers = managers;
  sources.vacancies = vacancies;
  sources.inbox = inbox;
  sources.awards = awards;
  sources.records = records;
  sources.history = history;
  sources.managed_team = 3;
  sources.world_start = GameDateValue(2024, 7, 1);

  const auto items = NewsFeed::build(sources, GameDateValue(2025, 7, 1),
                                     GameDateValue(2026, 6, 30));
  ASSERT_EQ(items.size(), 10U) << describe(items);
  for (const NewsItem& item : items)
  {
    EXPECT_TRUE(item.team_id != 0 || item.player_id != 0) << item.headline();
    EXPECT_FALSE(item.headline().empty());
    EXPECT_EQ(item.headline().find('{'), std::string::npos) << item.headline();
  }
  for (std::size_t index = 1; index < items.size(); ++index)
    EXPECT_FALSE(items[index - 1].date < items[index].date) << "newest first";

  const auto count = [&items](NewsKind kind)
  { return std::ranges::count(items, kind, &NewsItem::kind); };
  EXPECT_EQ(count(NewsKind::Transfer), 2);  // The 20M move and our signing.
  EXPECT_EQ(count(NewsKind::Manager), 4);   // Ours, the dismissal, the job,
                                            // the vacancy.
  EXPECT_EQ(count(NewsKind::Race), 2);      // Champions and promotion.
  EXPECT_EQ(count(NewsKind::Record), 1);
  EXPECT_EQ(count(NewsKind::Award), 1);

  const auto big = std::ranges::find_if(
      items, [](const NewsItem& item)
      { return item.kind == NewsKind::Transfer && item.player_id == 1; });
  ASSERT_NE(big, items.end());
  const std::string headline = big->headline();
  for (const char* name : {"Nico Player1", "Acaya", "Foggia"})
    EXPECT_NE(headline.find(name), std::string::npos) << headline;
  // The award comes the day after September ends.
  const auto award = std::ranges::find(items, NewsKind::Award, &NewsItem::kind);
  EXPECT_EQ(award->date, GameDateValue(2025, 10, 1));
  // The dismissal names the departed manager.
  const auto sacked = std::ranges::find_if(
      items, [](const NewsItem& item)
      { return item.headline().contains("Gino Panchina"); });
  EXPECT_NE(sacked, items.end());

  // Same facts, same stories in the same order.
  const auto again = NewsFeed::build(sources, GameDateValue(2025, 7, 1),
                                     GameDateValue(2026, 6, 30));
  EXPECT_EQ(describe(items), describe(again));

  // Filters: everything here belongs to the country of the top league.
  NewsFilter filter;
  filter.country = 1;
  EXPECT_EQ(NewsFeed::filter(items, filter).size(), items.size());
  filter.country = 0;
  EXPECT_TRUE(NewsFeed::filter(items, filter).empty());
  filter = {};
  filter.competition = NewsCompetition{MatchType::LEAGUE, 2};
  EXPECT_EQ(NewsFeed::filter(items, filter).size(), 3U);  // Verdicts, vacancy.
  filter = {};
  filter.kind = NewsKind::Manager;
  for (const std::size_t index : NewsFeed::filter(items, filter))
    EXPECT_EQ(items[index].kind, NewsKind::Manager);

  // A narrower window keeps only what happened inside it.
  const auto autumn = NewsFeed::build(sources, GameDateValue(2025, 10, 1),
                                      GameDateValue(2025, 11, 30));
  for (const NewsItem& item : autumn)
  {
    EXPECT_FALSE(item.date < GameDateValue(2025, 10, 1));
    EXPECT_FALSE(GameDateValue(2025, 11, 30) < item.date);
  }
  EXPECT_EQ(autumn.size(), 4U) << describe(autumn);
}

TEST_F(SyntheticWorld, ResultsGiveUpsetsAndTheTitleRace)
{
  Calendar calendar;
  // Acaya (85) lose at home to Bari (40); Foggia lead Lecce by a point with
  // four games to go.
  const auto play = [&calendar](TeamID home, TeamID away, std::uint8_t day,
                                std::uint8_t home_goals,
                                std::uint8_t away_goals)
  {
    Match match(home, away, GameDateValue(2025, 8, day), MatchType::LEAGUE, 1,
                static_cast<std::uint8_t>(day / 7));
    match.setPlayedResult(home_goals, away_goals);
    calendar.addMatch(match);
  };
  play(1, 2, 16, 1, 1);
  play(3, 4, 16, 2, 0);
  play(2, 3, 23, 3, 0);
  play(1, 4, 23, 0, 2);

  NewsSources sources;
  sources.gamedata = &gamedata;
  sources.calendar = &calendar;
  sources.world_start = GameDateValue(2025, 7, 1);
  const auto items = NewsFeed::build(sources, GameDateValue(2025, 7, 1),
                                     GameDateValue(2025, 8, 31));
  const auto upset = std::ranges::find(items, NewsKind::Upset, &NewsItem::kind);
  ASSERT_NE(upset, items.end()) << describe(items);
  EXPECT_EQ(upset->team_id, 4U);
  EXPECT_EQ(upset->other_team_id, 1U);
  EXPECT_TRUE(upset->has_match);
  EXPECT_EQ(upset->home_id, 1U);
  EXPECT_EQ(upset->away_id, 4U);
  EXPECT_NE(upset->headline().find("2-0"), std::string::npos)
      << upset->headline();
  // The other results were no surprise.
  EXPECT_EQ(std::ranges::count(items, NewsKind::Upset, &NewsItem::kind), 1);

  const auto race = std::ranges::find(items, NewsKind::Race, &NewsItem::kind);
  ASSERT_NE(race, items.end()) << describe(items);
  EXPECT_EQ(race->headline_key, "NEWS_RACE_TIGHT");
  EXPECT_EQ(race->team_id, 2U);        // Foggia: 4 points.
  EXPECT_EQ(race->other_team_id, 3U);  // Lecce: 3 points.
  EXPECT_EQ(race->args[3], "4");       // Games left.
}

TEST(NewsFeedTest, HeadlineVariantDependsOnlyOnTheFacts)
{
  const GameDateValue date(2025, 9, 14);
  const std::size_t first =
      NewsFeed::variant(NewsKind::Transfer, 11, 4, date, 3);
  EXPECT_EQ(first, NewsFeed::variant(NewsKind::Transfer, 11, 4, date, 3));
  EXPECT_LT(first, 3U);
  EXPECT_EQ(NewsFeed::variant(NewsKind::Award, 1, 2, date, 1), 0U);
  // Different facts spread over every variant.
  std::set<std::size_t> used;
  for (std::uint32_t player = 1; player <= 40; ++player)
    used.insert(NewsFeed::variant(NewsKind::Transfer, player, 4, date, 3));
  EXPECT_EQ(used.size(), 3U);
}

// ---- Draw ceremonies
// ---------------------------------------------------------

TEST(DrawCeremonyTest, RevealTimingAndReducedMotion)
{
  EXPECT_EQ(DrawCeremonies::shownAt(0.0f, 1.0f, 8, false), 1U);
  EXPECT_EQ(DrawCeremonies::shownAt(DrawCeremonies::REVEAL_SECONDS * 2.5f, 1.0f,
                                    8, false),
            3U);
  EXPECT_EQ(DrawCeremonies::shownAt(DrawCeremonies::REVEAL_SECONDS * 2.5f, 2.0f,
                                    8, false),
            6U);
  EXPECT_EQ(DrawCeremonies::shownAt(1000.0f, 1.0f, 8, false), 8U);
  EXPECT_EQ(DrawCeremonies::shownAt(0.0f, 1.0f, 8, true), 8U);
  EXPECT_EQ(DrawCeremonies::shownAt(0.0f, 1.0f, 0, false), 0U);
}

// ---- Career timeline
// ---------------------------------------------------------

TEST_F(SyntheticWorld, TimelineOfTwoClubsAndASacking)
{
  ManagerStint first;
  first.team_id = 4;
  first.club_name = "Bari";
  first.league_id = 1;
  first.start = GameDateValue(2025, 7, 1);
  first.end = GameDateValue(2026, 2, 10);
  first.reason = DepartureReason::Sacked;
  first.played = 25;
  first.won = 6;
  first.drawn = 7;
  first.lost = 12;
  ManagerStint second;
  second.team_id = 5;
  second.club_name = "Taranto";
  second.league_id = 2;
  second.start = GameDateValue(2026, 3, 1);
  second.played = 12;
  second.won = 9;
  std::vector<ManagerStint> stints = {first, second};
  ManagerAward promotion;
  promotion.start_year = 2025;
  promotion.kind = ManagerAwardKind::Promotion;
  promotion.team_id = 5;
  promotion.club_name = "Taranto";
  std::vector<ManagerAward> honours = {promotion};
  // A signing at the first club, and one by another club (not his).
  std::vector<TransferRecord> transfers = {
      {2, GameDateValue(2025, 8, 20), 2, 4, 3'000'000, TransferKind::Permanent},
      {3, GameDateValue(2025, 8, 21), 3, 1, 9'000'000,
       TransferKind::Permanent}};

  TimelineSources sources;
  sources.gamedata = &gamedata;
  sources.manager_name = "Flavio Coach";
  sources.stints = stints;
  sources.honours = honours;
  sources.transfers = transfers;
  const auto entries = CareerTimeline::build(sources);
  // Appointed, signing, sacked, appointed again, promoted.
  ASSERT_EQ(entries.size(), 5U);
  EXPECT_EQ(entries[0].kind, TimelineKind::Appointed);
  EXPECT_EQ(entries[1].kind, TimelineKind::Signing);
  EXPECT_EQ(entries[1].player_id, 2U);
  EXPECT_EQ(entries[2].kind, TimelineKind::Departed);
  EXPECT_EQ(entries[3].kind, TimelineKind::Appointed);
  EXPECT_EQ(entries[3].team_id, 5U);
  EXPECT_EQ(entries[4].kind, TimelineKind::Promotion);
  EXPECT_EQ(entries[4].season_year, 2025U);
  EXPECT_NE(entries[2].text().find("Sacked by Bari after 25 matches"),
            std::string::npos)
      << entries[2].text();
  for (const TimelineEntry& entry : entries)
    EXPECT_EQ(entry.text().find('{'), std::string::npos) << entry.text();

  // Stable: the same facts give the same timeline.
  const auto again = CareerTimeline::build(sources);
  ASSERT_EQ(again.size(), entries.size());
  for (std::size_t index = 0; index < entries.size(); ++index)
    EXPECT_EQ(again[index].text(), entries[index].text());

  // The journal: a club table and one section per season.
  const std::string markdown =
      CareerTimeline::toMarkdown(sources, entries, GameDateValue(2026, 4, 1));
  EXPECT_NE(markdown.find("# Career journal of Flavio Coach"),
            std::string::npos);
  EXPECT_NE(markdown.find("| Bari |"), std::string::npos);
  EXPECT_NE(markdown.find("| Taranto |"), std::string::npos);
  EXPECT_NE(markdown.find("## Season 2025/26"), std::string::npos);
  EXPECT_EQ(std::ranges::count(markdown, '\n') > 10, true);
  EXPECT_EQ(CareerTimeline::journalFileName("Flàvio  O'Coach",
                                            GameDateValue(2026, 4, 1)),
            "career-fl-vio-o-coach-2026-04-01.md");

  const auto folder =
      RuntimePaths::root() / std::format("fm-journal-{}", getpid());
  const auto path = CareerTimeline::writeJournal(markdown, folder, "career.md");
  ASSERT_TRUE(path.has_value());
  std::ifstream file(*path);
  const std::string written((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
  EXPECT_EQ(written, markdown);
  std::filesystem::remove_all(folder);
}

// ---- A seeded world
// ----------------------------------------------------------

TEST(NewsWorldTest, SeededSeasonFeedDrawsAndTimelineSurviveAReload)
{
  Logger::init();
  ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::EN));
  const SlotCleanup slot{uniqueSlot(1)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  controller.selectManagedTeam(controller.getTeams().front().get().getId());
  const auto gamedata = controller.getGameData();
  const Game& game = *controller.getGame();

  // The cup's first round is drawn with the season's fixtures.
  const LeagueID root = Competitions::countryRoots(*gamedata).front();
  const auto cup =
      DrawCeremonies::cupRound(game.getCalendar(), *gamedata, root, 1, 0);
  ASSERT_TRUE(cup.has_value());
  EXPECT_EQ(cup->drawn_on, GameDateValue(2025, 7, 1));
  const auto scheduled =
      Competitions::cupDraw(game.getCalendar(), *gamedata, root, 1);
  ASSERT_TRUE(scheduled.has_value());
  using Pair = std::tuple<TeamID, TeamID, GameDateValue>;
  std::vector<Pair> revealed;
  for (const DrawReveal& reveal : cup->reveals)
    revealed.emplace_back(reveal.home_id, reveal.away_id, reveal.date);
  std::vector<Pair> fixtures;
  for (const Match& tie : scheduled->ties)
    fixtures.emplace_back(tie.getHomeTeamId(), tie.getAwayTeamId(),
                          tie.getDate());
  ASSERT_EQ(revealed.size(), fixtures.size());
  EXPECT_NE(revealed, fixtures) << "the reveal is not the calendar order";
  std::vector<Pair> sorted = revealed;
  std::ranges::sort(sorted);
  std::ranges::sort(fixtures);
  EXPECT_EQ(sorted, fixtures) << "every scheduled tie is revealed once";
  // Same draw, same order; a later round has not been drawn yet.
  const auto replay =
      DrawCeremonies::cupRound(game.getCalendar(), *gamedata, root, 1, 0);
  ASSERT_TRUE(replay.has_value());
  for (std::size_t index = 0; index < cup->reveals.size(); ++index)
    EXPECT_EQ(replay->reveals[index].home_id, cup->reveals[index].home_id);
  EXPECT_FALSE(
      DrawCeremonies::cupRound(game.getCalendar(), *gamedata, root, 2, 0)
          .has_value());

  // Six weeks: the transfer window, the league start and the continental
  // league-phase draw.
  for (int day = 0; day < 64; ++day) controller.advanceDay();
  const GameDateValue today = controller.getCurrentDate();
  const std::size_t fixtures_before = [&game]
  {
    std::size_t total = 0;
    for (const auto& [date, matches] : game.getCalendar().getFullCalendar())
      total += matches.size();
    return total;
  }();

  const auto continental = DrawCeremonies::latestContinentalRound(
      *controller.getContinental(), game.getCalendar(),
      Continental::CHAMPIONS_CUP_ID, today, 0);
  ASSERT_TRUE(continental.has_value());
  EXPECT_EQ(continental->kind, DrawCeremony::Kind::ContinentalLeaguePhase);
  EXPECT_NE(continental->focus_team, 0U);
  const auto* season =
      controller.getContinental()->getSeason(Continental::CHAMPIONS_CUP_ID);
  ASSERT_NE(season, nullptr);
  EXPECT_EQ(continental->reveals.size(), season->matches);
  for (std::size_t index = 1; index < continental->reveals.size(); ++index)
    EXPECT_LE(continental->reveals[index - 1].pot,
              continental->reveals[index].pot);
  for (const DrawReveal& reveal : continental->reveals)
  {
    EXPECT_TRUE(reveal.home_id == continental->focus_team ||
                reveal.away_id == continental->focus_team);
    EXPECT_NE(game.getCalendar().findMatch(reveal.date, reveal.home_id,
                                           reveal.away_id),
              nullptr);
  }
  // The inbox message of the draw leads to the same ceremony.
  InboxMessage message;
  message.date = continental->drawn_on;
  message.title_key = "INBOX_CONT_DRAW_TITLE";
  message.args = {"@CONT_CHAMPIONS_CUP"};
  ASSERT_TRUE(DrawCeremonies::announcesDraw(message));
  const auto from_message =
      DrawCeremonies::forMessage(message, *controller.getContinental(),
                                 game.getCalendar(), continental->focus_team);
  ASSERT_TRUE(from_message.has_value());
  EXPECT_EQ(from_message->reveals.size(), continental->reveals.size());
  message.date = SeasonCalendar::addDays(message.date, 1);
  EXPECT_FALSE(DrawCeremonies::forMessage(message, *controller.getContinental(),
                                          game.getCalendar(), 0)
                   .has_value());

  // The feed: dated, citing an entity, never touching the game.
  const std::size_t inbox_before = controller.getInbox().size();
  const auto feed = NewsFeed::forGame(game, *gamedata);
  ASSERT_FALSE(feed.empty());
  std::set<NewsKind> kinds;
  for (const NewsItem& item : feed)
  {
    kinds.insert(item.kind);
    EXPECT_TRUE(item.team_id != 0 || item.player_id != 0) << item.headline();
    EXPECT_FALSE(today < item.date) << item.headline();
    EXPECT_EQ(item.headline().find('{'), std::string::npos) << item.headline();
    if (item.player_id != 0) EXPECT_TRUE(gamedata->getPlayer(item.player_id));
    if (item.has_match)
      EXPECT_NE(game.getCalendar().findMatch(item.match_date, item.home_id,
                                             item.away_id),
                nullptr);
  }
  EXPECT_TRUE(kinds.contains(NewsKind::Transfer)) << describe(feed);
  EXPECT_TRUE(kinds.contains(NewsKind::Manager)) << describe(feed);
  EXPECT_EQ(controller.getInbox().size(), inbox_before);
  // Bounded: a page of stories, not a flood.
  EXPECT_LT(feed.size(), 600U) << describe(feed);

  std::vector<RecordEntry> club_records;
  const auto timeline = CareerTimeline::build(
      CareerTimeline::sourcesFor(game, *gamedata, club_records));
  ASSERT_FALSE(timeline.empty());
  EXPECT_EQ(timeline.front().kind, TimelineKind::Appointed);

  std::size_t fixtures_after = 0;
  for (const auto& [date, matches] : game.getCalendar().getFullCalendar())
    fixtures_after += matches.size();
  EXPECT_EQ(fixtures_after, fixtures_before) << "building never schedules";

  ASSERT_TRUE(controller.saveGame());
  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  const auto reloaded_feed =
      NewsFeed::forGame(*reloaded.getGame(), *reloaded.getGameData());
  EXPECT_EQ(describe(reloaded_feed), describe(feed));
  std::vector<RecordEntry> records;
  const auto reloaded_timeline =
      CareerTimeline::build(CareerTimeline::sourcesFor(
          *reloaded.getGame(), *reloaded.getGameData(), records));
  ASSERT_EQ(reloaded_timeline.size(), timeline.size());
  for (std::size_t index = 0; index < timeline.size(); ++index)
    EXPECT_EQ(reloaded_timeline[index].text(), timeline[index].text());
  const auto reloaded_draw = DrawCeremonies::latestContinentalRound(
      *reloaded.getContinental(), reloaded.getGame()->getCalendar(),
      Continental::CHAMPIONS_CUP_ID, today, continental->focus_team);
  ASSERT_TRUE(reloaded_draw.has_value());
  ASSERT_EQ(reloaded_draw->reveals.size(), continental->reveals.size());
  for (std::size_t index = 0; index < continental->reveals.size(); ++index)
    EXPECT_EQ(reloaded_draw->reveals[index].away_id,
              continental->reveals[index].away_id);
}
