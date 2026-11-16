// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Match analytics: the engine's tracker reconciles with the engine's own
// statistics on seeded matches, the compact detail survives a save, older
// saves upgrade, and the report summary, key moments and data hub figures
// follow their rules.

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <format>
#include <map>
#include <memory>
#include <numeric>
#include <print>
#include <string>
#include <utility>
#include <vector>

#include "database/database_connection.h"
#include "database/migrations/migrations.h"
#include "model/data_hub.h"
#include "model/guidance.h"
#include "model/match_engine.h"
#include "model/match_insights.h"
#include "model/match_report.h"
#include "model/player.h"
#include "model/team.h"

namespace
{
namespace T = MatchTracking;

StatsConfig statsConfig()
{
  StatsConfig config;
  config.possible_stats = {"Pace",      "Shooting",  "Passing",
                           "Dribbling", "Defending", "Physicality",
                           "Stamina",   "Vision",    "Goalkeeping"};
  config.role_focus["Goalkeeper"] = {{"Goalkeeping", "Vision", "Physicality"},
                                     {0.7, 0.2, 0.1}};
  config.role_focus["Defender"] = {
      {"Defending", "Physicality", "Pace", "Vision"}, {0.4, 0.3, 0.15, 0.15}};
  config.role_focus["Midfielder"] = {
      {"Passing", "Vision", "Stamina", "Dribbling"}, {0.3, 0.3, 0.2, 0.2}};
  config.role_focus["Striker"] = {
      {"Shooting", "Pace", "Dribbling", "Physicality"}, {0.4, 0.2, 0.2, 0.2}};
  return config;
}

Team makeTeam(TeamID id, int rating,
              std::vector<std::unique_ptr<Player>>& players)
{
  using enum PlayerRole;
  Team team(id, 1, std::format("Club {}", id), 50'000'000, {}, Strategy{},
            Lineup{});
  static constexpr std::array ROLES = {GK, LB, CB, CB, RB, CM,
                                       CM, LW, RW, ST, ST};
  static constexpr std::array<Vector2F, 11> POSITIONS = {{{0.04f, 0.50f},
                                                          {0.18f, 0.12f},
                                                          {0.18f, 0.38f},
                                                          {0.18f, 0.62f},
                                                          {0.18f, 0.88f},
                                                          {0.43f, 0.35f},
                                                          {0.43f, 0.65f},
                                                          {0.68f, 0.16f},
                                                          {0.68f, 0.84f},
                                                          {0.78f, 0.38f},
                                                          {0.78f, 0.62f}}};
  for (uint32_t index = 0; index < 11; ++index)
  {
    const auto value = static_cast<float>(rating);
    const std::map<std::string, float> stats = {
        {"Pace", value},      {"Shooting", value},  {"Passing", value},
        {"Dribbling", value}, {"Defending", value}, {"Physicality", value},
        {"Stamina", value},   {"Vision", value},    {"Goalkeeping", value}};
    auto player = std::make_unique<Player>(
        static_cast<PlayerID>(id) * 100U + index, id, "First",
        std::to_string(index), ROLES[index], Language::EN, 100'000, 0, 25, 3,
        180, Foot::Right, stats);
    if (ROLES[index] == GK)
      team.getLineup().setGoalkeeper(player.get());
    else
      team.getLineup().addOutfieldPlayer(player.get(), POSITIONS[index]);
    players.push_back(std::move(player));
  }
  return team;
}

/** Two seeded sides and a configured engine per call. */
struct Fixture
{
  std::vector<std::unique_ptr<Player>> players;
  StatsConfig config = statsConfig();
  Team home = makeTeam(1, 68, players);
  Team away = makeTeam(2, 62, players);

  std::unique_ptr<MatchEngine> engine(std::uint32_t seed) const
  {
    return std::make_unique<MatchEngine>(home.getLineup(), away.getLineup(),
                                         home.getStrategy(), away.getStrategy(),
                                         config, seed);
  }
};

int passesBetweenTeamMates(const MatchEngine& engine, bool home)
{
  const auto& stats = engine.getPlayerStats();
  int total = 0;
  for (std::size_t from = 0; from < stats.size(); ++from)
    for (std::size_t to = 0; to < stats.size(); ++to)
      if (stats[from].isHomeTeam == home && stats[to].isHomeTeam == home)
        total += engine.getTracker().passes(from, to);
  return total;
}
}  // namespace

TEST(MatchInsightsTest, TrackerReconcilesWithTheEngineOnASeededMatch)
{
  const Fixture fixture;
  auto engine = fixture.engine(424242);
  engine->simulateToEnd();
  const MatchTracker& tracker = engine->getTracker();
  const auto& stats = engine->getPlayerStats();

  // Every completed pass between team-mates is one pass-network link count.
  for (const bool home : {true, false})
  {
    int completed = 0;
    for (const PlayerMatchStats& line : stats)
      if (line.isHomeTeam == home) completed += line.passesCompleted;
    EXPECT_GT(completed, 100) << "home=" << home;
    EXPECT_EQ(passesBetweenTeamMates(*engine, home), completed)
        << "home=" << home;
  }
  for (std::size_t index = 0; index < stats.size(); ++index)
  {
    const TrackedPlayer* tracked = tracker.player(index);
    if (tracked == nullptr) continue;
    // The touch map holds exactly the counted touches.
    const int cells =
        std::accumulate(tracked->touches.begin(), tracked->touches.end(), 0);
    EXPECT_EQ(cells, tracked->touch_count) << "player " << index;
    EXPECT_LE(tracked->progressive_passes, stats[index].passesCompleted);
    EXPECT_GE(tracked->expected_assists, 0.0f);
    if (stats[index].keyPasses == 0)
      EXPECT_FLOAT_EQ(tracked->expected_assists, 0.0f);
    else
      EXPECT_GT(tracked->expected_assists, 0.0f);
  }
  // One tracked shot per SHOT event; set pieces match the team counters.
  const std::vector<ShotRecord> shots = extractShots(engine->getEvents());
  ASSERT_EQ(tracker.shots().size(), shots.size());
  const auto setPieceShots = static_cast<int>(std::ranges::count_if(
      tracker.shots(), [](const TrackedShot& shot) { return shot.set_piece; }));
  EXPECT_EQ(setPieceShots, engine->getStats().homeSetPieceShots +
                               engine->getStats().awaySetPieceShots);
  // Final-third touches over time never exceed the touches themselves.
  int finalThird = 0;
  for (const auto& side : tracker.finalThirdTouches())
    finalThird += std::accumulate(side.begin(), side.end(), 0);
  int touches = 0;
  for (std::size_t index = 0; index < tracker.playerCount(); ++index)
    if (const TrackedPlayer* tracked = tracker.player(index))
      touches += tracked->touch_count;
  EXPECT_GT(touches, 600);
  EXPECT_GT(finalThird, 0);
  EXPECT_LT(finalThird, touches);
  std::array<int, 2> pressures{};
  for (std::size_t index = 0; index < tracker.playerCount(); ++index)
    if (const TrackedPlayer* tracked = tracker.player(index))
      pressures[stats[index].isHomeTeam ? 0 : 1] += tracked->pressures;
  std::print("[pressures] home {}, away {}\n", pressures[0], pressures[1]);
  EXPECT_GT(pressures[0], 20);
  EXPECT_GT(pressures[1], 20);
}

TEST(MatchInsightsTest, TrackingNeverChangesTheMatch)
{
  const Fixture fixture;
  auto tracked = fixture.engine(77);
  auto untracked = fixture.engine(77);
  untracked->setTracking(false);
  tracked->simulateToEnd();
  untracked->simulateToEnd();
  EXPECT_EQ(tracked->getHomeScore(), untracked->getHomeScore());
  EXPECT_EQ(tracked->getAwayScore(), untracked->getAwayScore());
  EXPECT_EQ(tracked->getEvents().size(), untracked->getEvents().size());
  EXPECT_EQ(tracked->getStats().homePassesCompleted,
            untracked->getStats().homePassesCompleted);
  EXPECT_FLOAT_EQ(tracked->getStats().awayShotXG,
                  untracked->getStats().awayShotXG);
  EXPECT_EQ(untracked->getTracker().playerCount(), 0U);
  EXPECT_TRUE(untracked->getTracker().shots().empty());

  // Unwatched fixtures switch the tracker off and record nothing.
  auto background = fixture.engine(78);
  background->simulateToEnd(MatchFidelity::BACKGROUND);
  EXPECT_FALSE(background->getTracker().isEnabled());
  EXPECT_EQ(background->getTracker().playerCount(), 0U);
}

TEST(MatchInsightsTest, TrackingCostIsSmall)
{
  // Same seeds with and without the tracker; the matches are identical, so
  // the difference is the recording alone.
  const Fixture fixture;
  constexpr int MATCHES = 6;
  const auto run = [&](bool tracking)
  {
    const auto start = std::chrono::steady_clock::now();
    for (int seed = 0; seed < MATCHES; ++seed)
    {
      auto engine = fixture.engine(static_cast<std::uint32_t>(9000 + seed));
      engine->setTracking(tracking);
      engine->simulateToEnd();
    }
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - start)
        .count();
  };
  run(false);  // Warm-up.
  double off = 0.0;
  double on = 0.0;
  for (int round = 0; round < 2; ++round)
  {
    off += run(false);
    on += run(true);
  }
  const double perMatchOff = off / (2.0 * MATCHES);
  const double perMatchOn = on / (2.0 * MATCHES);
  std::print("[tracking] {:.2f} ms/match off, {:.2f} ms/match on ({:+.1f}%)\n",
             perMatchOff, perMatchOn,
             100.0 * (perMatchOn - perMatchOff) / perMatchOff);
  RecordProperty("ms_per_match_off", std::to_string(perMatchOff));
  RecordProperty("ms_per_match_on", std::to_string(perMatchOn));
  // Only a gross regression fails: timings on a shared machine are noisy and
  // the measured cost is a few percent.
  EXPECT_LT(perMatchOn, perMatchOff * 2.0 + 5.0);
}

TEST(MatchInsightsTest, DetailRoundTripsCompactly)
{
  const Fixture fixture;
  auto engine = fixture.engine(424242);
  engine->simulateToEnd();
  const MatchDetail detail = captureDetail(*engine);
  ASSERT_FALSE(detail.empty());
  EXPECT_GE(detail.players.size(), 22U);
  EXPECT_FALSE(detail.links.empty());
  const std::vector<std::uint8_t> bytes = detail.encode();
  std::print("[detail] {} bytes for {} players and {} pass links\n",
             bytes.size(), detail.players.size(), detail.links.size());
  RecordProperty("detail_bytes", std::to_string(bytes.size()));
  EXPECT_LT(bytes.size(), 4096U);

  const auto restored = MatchDetail::decode(bytes);
  ASSERT_TRUE(restored.has_value());
  ASSERT_EQ(restored->players.size(), detail.players.size());
  for (std::size_t index = 0; index < detail.players.size(); ++index)
  {
    const DetailPlayer& a = detail.players[index];
    const DetailPlayer& b = restored->players[index];
    EXPECT_EQ(a.player, b.player);
    EXPECT_EQ(a.home, b.home);
    EXPECT_EQ(a.touches, b.touches);
    EXPECT_EQ(a.cells, b.cells);
    EXPECT_EQ(a.progressive_passes, b.progressive_passes);
    EXPECT_EQ(a.pressures, b.pressures);
    EXPECT_NEAR(a.avg_x, b.avg_x, 0.5f / 255.0f + 1e-6f);
    EXPECT_NEAR(a.expected_assists, b.expected_assists, 0.006f);
  }
  ASSERT_EQ(restored->links.size(), detail.links.size());
  for (std::size_t index = 0; index < detail.links.size(); ++index)
  {
    EXPECT_EQ(restored->links[index].from, detail.links[index].from);
    EXPECT_EQ(restored->links[index].to, detail.links[index].to);
    EXPECT_EQ(restored->links[index].count, detail.links[index].count);
  }
  EXPECT_EQ(restored->final_third, detail.final_third);
  // The pass network of a side sums to its players' completed passes.
  int linked = 0;
  for (const PassLink& link : detail.links)
    if (detail.players[link.from].home) linked += link.count;
  EXPECT_EQ(linked, passesBetweenTeamMates(*engine, true));

  // Truncated, padded or foreign data is refused, never half-read.
  std::vector<std::uint8_t> broken = bytes;
  broken.pop_back();
  EXPECT_FALSE(MatchDetail::decode(broken).has_value());
  broken = bytes;
  broken.push_back(0);
  EXPECT_FALSE(MatchDetail::decode(broken).has_value());
  broken = bytes;
  broken[0] = 99;
  EXPECT_FALSE(MatchDetail::decode(broken).has_value());
  EXPECT_FALSE(MatchDetail::decode({}).has_value());
}

TEST(MatchInsightsTest, SnapshotWithDetailSurvivesASaveAndOldShotsStillLoad)
{
  const Fixture fixture;
  auto engine = fixture.engine(31337);
  engine->simulateToEnd();
  const GameDateValue date(2025, 9, 14);
  ManagedMatchSnapshot snapshot = captureSnapshot(*engine, date, 1, 2, true);
  ASSERT_FALSE(snapshot.detail.empty());
  ASSERT_FALSE(snapshot.shots.empty());

  auto db = std::make_shared<DatabaseConnection>(":memory:");
  Migrations::migrate(*db);
  CareerGuidance saved;
  saved.addSnapshot(snapshot);
  sqlite3_exec(db->getRaw(), "BEGIN;", nullptr, nullptr, nullptr);
  saved.save(db);
  sqlite3_exec(db->getRaw(), "COMMIT;", nullptr, nullptr, nullptr);

  // Bytes per match on disk: the JSON snapshot plus the detail blob.
  sqlite3_stmt* stmt = db->prepareStatement(
      "SELECT length(data), length(detail) FROM ManagedMatchAnalytics;");
  ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
  const int json = sqlite3_column_int(stmt, 0);
  const int blob = sqlite3_column_int(stmt, 1);
  sqlite3_finalize(stmt);
  std::print("[persisted] {} bytes snapshot JSON + {} bytes detail\n", json,
             blob);
  RecordProperty("snapshot_json_bytes", std::to_string(json));
  RecordProperty("detail_blob_bytes", std::to_string(blob));
  EXPECT_GT(blob, 0);

  CareerGuidance loaded;
  loaded.load(db);
  ASSERT_EQ(loaded.getSnapshots().size(), 1U);
  const ManagedMatchSnapshot& back = loaded.getSnapshots().front();
  ASSERT_EQ(back.shots.size(), snapshot.shots.size());
  for (std::size_t index = 0; index < back.shots.size(); ++index)
  {
    EXPECT_EQ(back.shots[index].set_piece, snapshot.shots[index].set_piece);
    EXPECT_EQ(back.shots[index].header, snapshot.shots[index].header);
    EXPECT_EQ(back.shots[index].outcome, snapshot.shots[index].outcome);
  }
  ASSERT_EQ(back.detail.players.size(), snapshot.detail.players.size());
  EXPECT_EQ(back.detail.links.size(), snapshot.detail.links.size());
  EXPECT_EQ(back.detail.players[3].cells, snapshot.detail.players[3].cells);

  // Shots saved before the flags existed (eight elements) still load.
  const auto legacy = ManagedMatchSnapshot::fromJson(
      R"({"date":20250914,"home":1,"away":2,"managed_home":true,)"
      R"("shots":[[12.5,1,1,100,0.9,0.4,0.31,0]],"players":[]})");
  ASSERT_TRUE(legacy.has_value());
  ASSERT_EQ(legacy->shots.size(), 1U);
  EXPECT_EQ(legacy->shots[0].outcome, ShotOutcome::Goal);
  EXPECT_FALSE(legacy->shots[0].set_piece);
  EXPECT_TRUE(legacy->detail.empty());
}

TEST(MatchInsightsTest, OlderSavesGainTheDetailColumn)
{
  DatabaseConnection connection(":memory:");
  sqlite3* raw = connection.getRaw();
  // The table as it was before match detail was stored.
  ASSERT_EQ(sqlite3_exec(raw,
                         "CREATE TABLE ManagedMatchAnalytics (game_date "
                         "INTEGER NOT NULL, home_id INTEGER NOT NULL, away_id "
                         "INTEGER NOT NULL, data TEXT NOT NULL, PRIMARY KEY "
                         "(game_date, home_id, away_id));"
                         "INSERT INTO ManagedMatchAnalytics VALUES (20250914, "
                         "1, 2, '{\"date\":20250914,\"home\":1,\"away\":2,"
                         "\"managed_home\":true}');",
                         nullptr, nullptr, nullptr),
            SQLITE_OK);
  ASSERT_FALSE(
      Migrations::columnExists(raw, "ManagedMatchAnalytics", "detail"));
  const auto report = Migrations::migrate(connection);
  EXPECT_TRUE(Migrations::columnExists(raw, "ManagedMatchAnalytics", "detail"));
  EXPECT_TRUE(std::ranges::contains(report.applied, 11));
  // Upgrading twice changes nothing.
  EXPECT_TRUE(Migrations::migrate(connection).applied.empty());

  auto shared = std::shared_ptr<DatabaseConnection>(&connection,
                                                    [](DatabaseConnection*)
                                                    {
                                                      // This wrapper does not
                                                      // own the stack
                                                      // connection.
                                                    });
  CareerGuidance guidance;
  guidance.load(shared);
  ASSERT_EQ(guidance.getSnapshots().size(), 1U);
  EXPECT_TRUE(guidance.getSnapshots().front().detail.empty());
}

namespace
{
MatchReport reportOf(int home_goals, int away_goals, float home_xg,
                     float away_xg)
{
  MatchReport report;
  report.home_team_id = 1;
  report.away_team_id = 2;
  report.home_goals = static_cast<uint8_t>(home_goals);
  report.away_goals = static_cast<uint8_t>(away_goals);
  report.home_stats.expected_goals = home_xg;
  report.away_stats.expected_goals = away_xg;
  report.home_stats.shots = 14;
  report.away_stats.shots = 6;
  report.away_stats.saves = 5;
  return report;
}

ShotRecord shotOf(bool home, PlayerID player, float minute, float xg,
                  ShotOutcome outcome, bool set_piece = false)
{
  ShotRecord shot;
  shot.home = home;
  shot.player = player;
  shot.minute = minute;
  shot.period = minute < 45.0f ? 1 : 2;
  shot.x = 0.88f;
  shot.y = 0.5f;
  shot.xg = xg;
  shot.outcome = outcome;
  shot.set_piece = set_piece;
  return shot;
}

bool hasKey(const std::vector<AnalysisLine>& lines, std::string_view key)
{
  return std::ranges::any_of(
      lines, [key](const AnalysisLine& line) { return line.key == key; });
}

/** A detail where the away side attacks down its left (our right). */
MatchDetail overloadedDetail()
{
  MatchDetail detail;
  DetailPlayer back;
  back.player = 104;  // Home right-back.
  back.home = true;
  back.touches = 40;
  back.avg_x = 0.25f;
  back.avg_y = 0.85f;
  detail.players.push_back(back);
  DetailPlayer winger;
  winger.player = 207;
  winger.home = false;
  winger.touches = 60;
  winger.avg_x = 0.7f;
  winger.avg_y = 0.15f;
  // Final third (columns 8-11), left flank (rows 0-2).
  for (std::size_t column = 8; column < 12; ++column)
    winger.cells[1 * T::GRID_COLUMNS + column] = 8;
  winger.cells[4 * T::GRID_COLUMNS + 9] = 6;
  detail.players.push_back(winger);
  detail.final_third[1][12] = 9;
  detail.final_third[1][13] = 8;
  detail.final_third[1][14] = 7;
  return detail;
}
}  // namespace

TEST(MatchInsightsTest, SummaryExplainsWastefulFinishingAndAnOverload)
{
  const MatchReport report = reportOf(0, 1, 2.4f, 0.4f);
  const std::vector shots = {
      shotOf(true, 109, 12.0f, 0.45f, ShotOutcome::Saved),
      shotOf(true, 110, 30.0f, 0.38f, ShotOutcome::OffTarget),
      shotOf(true, 109, 70.0f, 0.6f, ShotOutcome::Woodwork),
      shotOf(false, 207, 61.0f, 0.25f, ShotOutcome::Goal)};
  const MatchDetail detail = overloadedDetail();
  InsightInput input;
  input.report = &report;
  input.shots = shots;
  input.detail = &detail;
  input.managed_home = true;
  input.name_of = [](PlayerID id) { return std::format("P{}", id); };
  const auto lines = summariseMatch(input);
  ASSERT_FALSE(lines.empty());
  EXPECT_EQ(lines.front().key, "SUM_LOSS_UNLUCKY");
  EXPECT_EQ(lines.front().args[0], "2.4");
  ASSERT_TRUE(hasKey(lines, "SUM_OWN_WASTEFUL"));
  const auto wasteful =
      std::ranges::find(lines, "SUM_OWN_WASTEFUL", &AnalysisLine::key);
  EXPECT_EQ(wasteful->args[2], "3");  // Three big chances missed.
  const auto overload = std::ranges::find(lines, "SUM_OVERLOAD_AGAINST_PLAYER",
                                          &AnalysisLine::key);
  ASSERT_NE(overload, lines.end());
  EXPECT_EQ(overload->args[0], "@SUM_FLANK_LEFT");
  EXPECT_EQ(overload->args[1], "@SUM_FLANK_RIGHT");
  EXPECT_EQ(overload->args[3], "P104");
  EXPECT_LE(static_cast<int>(lines.size()), MatchInsights::MAX_SUMMARY_LINES);

  // The same match from the other bench reads as a smash-and-grab.
  input.managed_home = false;
  const std::vector<AnalysisLine> theirs = summariseMatch(input);
  EXPECT_EQ(theirs.front().key, "SUM_WIN_AGAINST_RUN");
  EXPECT_TRUE(hasKey(theirs, "SUM_OPP_WASTEFUL"));
  EXPECT_TRUE(hasKey(theirs, "SUM_SPELL_OWN"));

  // No shots at all: nothing to explain.
  const MatchReport bare;
  InsightInput empty;
  empty.report = &bare;
  EXPECT_TRUE(summariseMatch(empty).empty());
}

TEST(MatchInsightsTest, KeyMomentsLinkGoalsToTheirShots)
{
  MatchReport report = reportOf(1, 1, 1.2f, 0.9f);
  report.events = {{61, 0, MatchEventKind::GOAL, false, 207, 0},
                   {20, 0, MatchEventKind::GOAL, true, 109, 105},
                   {33, 0, MatchEventKind::YELLOW_CARD, false, 203, 0}};
  const std::vector shots = {
      shotOf(true, 109, 20.4f, 0.31f, ShotOutcome::Goal),
      shotOf(true, 110, 40.0f, 0.52f, ShotOutcome::Saved),
      shotOf(false, 207, 61.2f, 0.12f, ShotOutcome::Goal),
      shotOf(false, 208, 80.0f, 0.05f, ShotOutcome::OffTarget)};
  MatchDetail detail;
  detail.substitutions.push_back({70, true, 110, 112});
  InsightInput input;
  input.report = &report;
  input.shots = shots;
  input.detail = &detail;
  const std::vector<KeyMoment> moments = keyMoments(input);
  ASSERT_EQ(moments.size(), 5U);
  EXPECT_TRUE(std::ranges::is_sorted(moments, {}, &KeyMoment::minute));
  EXPECT_EQ(moments[0].kind, KeyMoment::Kind::Goal);
  EXPECT_EQ(moments[0].shot, 0);
  EXPECT_FLOAT_EQ(moments[0].xg, 0.31f);
  EXPECT_EQ(moments[0].other, 105U);
  EXPECT_EQ(moments[1].kind, KeyMoment::Kind::Yellow);
  EXPECT_EQ(moments[2].kind, KeyMoment::Kind::BigChance);
  EXPECT_EQ(moments[2].shot, 1);
  EXPECT_EQ(moments[3].shot, 2);
  EXPECT_EQ(moments[4].kind, KeyMoment::Kind::Substitution);
  EXPECT_EQ(moments[4].other, 110U);
}

TEST(MatchInsightsTest, HubMeasuresFinishingKeepingAndLeaders)
{
  constexpr TeamID CLUB = 1;
  std::vector<MatchReport> reports;
  for (int day = 1; day <= 4; ++day)
  {
    MatchReport report = reportOf(2, 1, 1.0f, 1.5f);
    report.date = GameDateValue(2025, 10, static_cast<uint8_t>(day));
    report.match_type = MatchType::LEAGUE;
    report.home_stats.passes_attempted = 400;
    report.home_stats.shots_on_target = 5;
    report.away_stats.shots_on_target = 6;
    report.home_stats.set_pieces_known = true;
    report.home_stats.set_piece_shots = 7;  // Half of 14 shots.
    report.away_stats.set_pieces_known = true;
    report.away_stats.set_piece_shots = 0;
    reports.push_back(report);
  }
  ManagedMatchSnapshot snapshot;
  snapshot.date = reports.front().date;
  snapshot.home_id = CLUB;
  snapshot.away_id = 2;
  snapshot.players = {{109, 90, 40, 30, 3, 4, 0.8f, 0, 0},
                      {110, 90, 60, 50, 1, 1, 0.1f, 2, 1}};
  DetailPlayer forward;
  forward.player = 109;
  forward.expected_assists = 0.4f;
  forward.progressive_passes = 2;
  forward.pressures = 9;
  DetailPlayer midfielder;
  midfielder.player = 110;
  midfielder.expected_assists = 0.7f;
  midfielder.progressive_passes = 11;
  midfielder.pressures = 4;
  snapshot.detail.players = {forward, midfielder};
  const std::vector<ManagedMatchSnapshot> snapshots = {snapshot};

  DataHubInput input;
  input.team_id = CLUB;
  input.team_reports = reports;
  input.league_reports = reports;
  input.snapshots = snapshots;
  const TeamAnalytics team = DataHub::buildTeamAnalytics(input);
  const auto metric = [&team](HubMetric which)
  { return team.metrics[static_cast<std::size_t>(std::to_underlying(which))]; };
  // Two goals a match from 1.0 xG.
  EXPECT_NEAR(metric(HubMetric::Finishing).team, 1.0f, 1e-4f);
  // League conversion: 3 goals per 11 shots on target; the club faced 6
  // on target a match and conceded 1.
  EXPECT_NEAR(team.league_conversion, 3.0f / 11.0f, 1e-4f);
  EXPECT_NEAR(metric(HubMetric::GoalsPrevented).team,
              6.0f * 3.0f / 11.0f - 1.0f, 1e-4f);
  EXPECT_NEAR(metric(HubMetric::SetPieceShare).team, 50.0f, 1e-3f);
  // Both sides of every report count for the league: 7 of 20 shots.
  EXPECT_NEAR(metric(HubMetric::SetPieceShare).league, 35.0f, 1e-3f);
  ASSERT_EQ(team.cumulative_finishing.size(), 4U);
  EXPECT_NEAR(team.cumulative_finishing.back(), 4.0f, 1e-4f);
  EXPECT_NEAR(team.cumulative_prevention.back(), 2.0f, 1e-4f);

  const std::vector<PlayerAnalyticsRow> rows =
      DataHub::buildPlayerAnalytics(input);
  const auto xa = DataHub::leaders(rows, LeaderMetric::ExpectedAssists);
  ASSERT_EQ(xa.size(), 2U);
  EXPECT_EQ(rows[xa[0]].player, 110U);
  EXPECT_NEAR(rows[xa[0]].xa, 0.7f, 1e-4f);
  const auto press = DataHub::leaders(rows, LeaderMetric::Pressures);
  ASSERT_FALSE(press.empty());
  EXPECT_EQ(rows[press[0]].player, 109U);
  EXPECT_EQ(rows[press[0]].detail_minutes, 90);
}
