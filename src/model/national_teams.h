// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "global/languages.h"
#include "global/stats_config.h"
#include "global/types.h"
#include "model/gamedate.h"

class DatabaseConnection;
class GameData;
class MatchScheduler;
class Player;
class WorldSimulation;

/**
 * @brief National teams: confederations, competitions and pure helpers.
 *
 * One national team per nationality with enough players. The two-season
 * cycle follows the real calendar: in the season before a summer finals
 * tournament the September-March windows host the qualifiers; in the other
 * season the autumn windows host a nations league (its best group winners
 * meet in June) and March has friendlies. Finals alternate between a world
 * tournament and continental tournaments every two years. Idle nations play
 * friendlies.
 */
namespace International
{
enum class Confederation : uint8_t
{
  Europe,
  Americas,
  Asia
};

/** Language key naming a confederation. */
const char* confederationKey(Confederation confederation);
Confederation confederationOf(Language nation);
/** Language key of a national team's name (e.g. "NT_Italian"). */
std::string teamNameKey(Language nation);

enum class Competition : uint8_t
{
  Friendly = 0,
  WorldQualifier,
  ContinentalQualifier,
  NationsLeague,
  NationsLeagueFinals,
  WorldFinals,
  ContinentalFinals
};

/** Language key naming a competition. */
const char* competitionKey(Competition competition);
bool isFinals(Competition competition);

enum class Stage : uint8_t
{
  Group = 0,
  QuarterFinal,
  SemiFinal,
  Final
};

const char* stageKey(Stage stage);

struct Fixture
{
  uint32_t id = 0;
  GameDateValue date;
  Language home = Language::EN;
  Language away = Language::EN;
  Competition competition = Competition::Friendly;
  uint8_t group = 0; /*!< 1-based group, 0 outside group stages. */
  Stage stage = Stage::Group;
  bool neutral = false;
  bool played = false;
  uint8_t home_goals = 0;
  uint8_t away_goals = 0;
  bool extra_time = false;
  bool penalties = false;
  uint8_t home_penalties = 0;
  uint8_t away_penalties = 0;
};

/** Winner of a played fixture (penalties included); nullopt for draws. */
std::optional<Language> winnerOf(const Fixture& fixture);

struct Group
{
  Competition competition = Competition::WorldQualifier;
  uint8_t index = 1; /*!< 1-based. */
  std::vector<Language> members;
};

struct GroupRow
{
  Language nation = Language::EN;
  uint8_t played = 0;
  uint8_t won = 0;
  uint8_t drawn = 0;
  uint8_t lost = 0;
  uint16_t goals_for = 0;
  uint16_t goals_against = 0;
  uint16_t points = 0;
  int goalDifference() const
  {
    return static_cast<int>(goals_for) - static_cast<int>(goals_against);
  }
};

/** Group table: points, goal difference, goals scored, then @p rating. */
std::vector<GroupRow> groupTable(const Group& group,
                                 const std::vector<Fixture>& fixtures,
                                 const std::map<Language, double>& rating);

/** A player's international career. */
struct Record
{
  Language nation = Language::EN; /*!< Nation he is tied to. */
  std::string name;               /*!< Kept for retired players. */
  uint16_t caps = 0;
  uint16_t goals = 0;
  uint16_t finals_caps = 0; /*!< Matches at finals tournaments. */
  uint8_t last_cap_age = 0;
  GameDateValue last_cap;
};

/**
 * Switching nation (design default, after the 2020 eligibility rules): at
 * most three senior caps, all before 21, none at a finals tournament, and
 * three years since the last one.
 */
bool canSwitchNation(const Record& record, const GameDateValue& today);

/** Rounds of a round robin (circle method); pairs are team indices. */
std::vector<std::vector<std::pair<size_t, size_t>>> roundRobin(
    size_t teams, bool double_round);

/** Finals size for a pool of nations: 16, 8, 4, or 0 (too few). */
size_t finalsSize(size_t pool);

/** Squad sizes: 24 for a window, 26 for a finals tournament. */
inline constexpr size_t WINDOW_SQUAD = 24;
inline constexpr size_t FINALS_SQUAD = 26;
/** A head coach calls up at least this many players, two goalkeepers. */
inline constexpr size_t MIN_CALL_UPS = 18;
inline constexpr size_t MIN_CALL_UP_GOALKEEPERS = 2;
/** Youngest age for a senior call-up. */
inline constexpr int MIN_SQUAD_AGE = 17;

/** @brief Why a squad chosen by a head coach was refused. */
enum class CallUpResult : uint8_t
{
  Ok = 0,
  NoSquad, /*!< No call-up is due for this nation. */
  Locked,  /*!< The players have already reported. */
  TooMany,
  TooFew,
  NeedGoalkeepers,
  Ineligible, /*!< Wrong nation, too young, injured or tied elsewhere. */
  Duplicate
};

/** Language key of a call-up verdict. */
const char* callUpResultKey(CallUpResult result);

/** Selection score of a player: ability, form, condition and caps. */
double selectionScore(const Player& player, uint16_t caps,
                      const StatsConfig& config);

/**
 * @brief Picks a squad of @p size: three goalkeepers and a balanced mix of
 * defenders, midfielders and forwards, ranked by ability, form, condition
 * and experience (@p caps); shortfalls are filled by the best remaining
 * players. Deterministic (ties by player ID).
 */
std::vector<PlayerID> selectSquad(
    const std::vector<const Player*>& eligible, size_t size,
    const StatsConfig& config,
    const std::unordered_map<PlayerID, uint16_t>& caps);
}  // namespace International

/**
 * @class NationalTeams
 * @brief National-team calendar, call-ups, matches and records.
 *
 * Matches are simulated with the match engine through the same
 * MatchScheduler batch as club matches. Called-up players are unavailable
 * to their clubs from the Monday of a window to its last day (finals: until
 * their nation is out), come back with the match fatigue and injuries of
 * the engine plus travel fatigue, and raise the injury risk for a few days
 * after long trips. Clubs are compensated per player and day for finals.
 */
class NationalTeams
{
 public:
  /** Team IDs used for national teams inside match reports. */
  static constexpr TeamID TEAM_ID_BASE = 0xF000;
  /** Nations need this many players (and two goalkeepers). */
  static constexpr size_t MIN_POOL = 20;
  /** Club compensation per finals player and day (whole euros). */
  static constexpr int64_t COMPENSATION_PER_DAY = 5'000;

  struct Team
  {
    Language nation = Language::EN;
    std::string coach; /*!< Head coach. */
    double rating = 1500.0;
  };

  struct Squad
  {
    Language nation = Language::EN;
    std::vector<PlayerID> players;
    GameDateValue announced;
    GameDateValue start;
    GameDateValue until;
    bool finals = false;
  };

  /** A finals tournament (or the nations-league finals). */
  struct Finals
  {
    International::Competition competition =
        International::Competition::WorldFinals;
    uint16_t year = 0;
    International::Confederation confederation =
        International::Confederation::Europe;
    uint8_t size = 0;
    GameDateValue start; /*!< Monday players report. */
    bool drawn = false;
    std::vector<Language> qualified;
    std::vector<International::Group> groups;
    std::optional<Language> winner;
    std::optional<Language> runner_up;
  };

  struct Honour
  {
    uint16_t year = 0;
    International::Competition competition =
        International::Competition::WorldFinals;
    Language winner = Language::EN;
    Language runner_up = Language::EN;
  };

  struct Travel
  {
    GameDateValue returned;
    uint16_t km = 0;
    uint8_t time_zones = 0;
  };

  /** A played match and the home side's expected score before it. */
  using ResultSink =
      std::function<void(const International::Fixture&, double home_expected)>;
  /** A finals tournament was drawn (qualifiers known) or decided. */
  using FinalsSink = std::function<void(const Finals&, bool decided)>;

  explicit NationalTeams(std::shared_ptr<GameData> gamedata);

  void setResultSink(ResultSink sink) { result_sink = std::move(sink); }
  void setFinalsSink(FinalsSink sink) { finals_sink = std::move(sink); }

  /**
   * @brief Daily processing: plans the season's international calendar,
   * announces squads a week before each window, plays the day's matches
   * through @p scheduler, runs the finals and releases players.
   */
  void onDay(const GameDateValue& today, MatchScheduler& scheduler,
             WorldSimulation& world, TeamID managed_team_id);

  /** Plans the international matches of the season (idempotent). */
  void planSeason(uint16_t season_year, const GameDateValue& today,
                  WorldSimulation* world);

  /** Called up and not yet released on @p date. */
  bool isOnDuty(PlayerID player_id, const GameDateValue& date) const;
  /** Nation a player is (or will be) with on @p date. */
  std::optional<Language> dutyNation(PlayerID player_id,
                                     const GameDateValue& date) const;
  /** Injury-risk multiplier (>= 1) after long trips, for a few days. */
  double injuryRiskMultiplier(PlayerID player_id,
                              const GameDateValue& date) const;
  /** Whether @p player may be picked by @p nation on @p date. */
  bool isEligibleFor(const Player& player, Language nation,
                     const GameDateValue& date) const;

  // ---------------- Head coach ----------------
  /** The nation's announced or current squad on @p date (nullptr if none). */
  const Squad* squadOf(Language nation, const GameDateValue& date) const;
  /** Players the nation may call up on @p date (injured ones included),
   * sorted by ID. */
  std::vector<PlayerID> eligiblePool(Language nation,
                                     const GameDateValue& date) const;
  /** Checks a squad a head coach wants to call up for the announced window. */
  International::CallUpResult validateSquad(
      Language nation, const std::vector<PlayerID>& players,
      const GameDateValue& today) const;
  /** Replaces the announced squad once validateSquad() accepts it. */
  International::CallUpResult setSquad(Language nation,
                                       std::vector<PlayerID> players,
                                       const GameDateValue& today);
  /** Names the nation's head coach. */
  void setCoach(Language nation, std::string name);
  /** Squad limit of a call-up (window or finals). */
  static size_t squadLimit(const Squad& squad)
  {
    return squad.finals ? International::FINALS_SQUAD
                        : International::WINDOW_SQUAD;
  }
  /** Expected score of @p home against @p away (Elo, home advantage unless
   * @p neutral). */
  double expectedScore(Language home, Language away, bool neutral) const;

  // ---------------- Queries ----------------
  const std::vector<Team>& getTeams() const { return teams; }
  const Team* getTeam(Language nation) const;
  /** Nations by rating, best first. */
  std::vector<Language> ranking() const;
  const std::vector<International::Fixture>& getFixtures() const
  {
    return fixtures;
  }
  /** Qualifying and nations-league groups of the current cycle. */
  const std::vector<International::Group>& getGroups() const { return groups; }
  const std::vector<Finals>& getFinals() const { return finals; }
  const std::vector<Squad>& getSquads() const { return squads; }
  const std::vector<Honour>& getHonours() const { return honours; }
  const International::Record* getRecord(PlayerID player_id) const;
  /** Most capped players (ties: goals, then ID). */
  std::vector<std::pair<PlayerID, International::Record>> capsLeaders(
      size_t limit) const;
  /** Group table with the current ratings as the last tie-breaker. */
  std::vector<International::GroupRow> table(
      const International::Group& group) const;

  // ---------------- Persistence ----------------
  void load(const DatabaseConnection& db);
  void save(const DatabaseConnection& db) const;
  std::string serialize() const;
  void deserialize(const std::string& data);

 private:
  void refreshNations();
  std::vector<Language> pool(
      std::optional<International::Confederation> confederation) const;
  void planQualifiers(uint16_t season_year, uint16_t finals_year,
                      const std::vector<GameDateValue>& slots,
                      const GameDateValue& summer_start);
  void planNationsLeague(uint16_t season_year,
                         const std::vector<GameDateValue>& slots,
                         const GameDateValue& summer_start);
  void scheduleGroup(const International::Group& group,
                     const std::vector<GameDateValue>& slots, bool neutral);
  void drawFinals(Finals& entry, const GameDateValue& today,
                  WorldSimulation& world);
  void progressFinals(Finals& entry, const GameDateValue& today,
                      WorldSimulation& world);
  void announceWindow(const GameDateValue& today,
                      const std::vector<GameDateValue>& match_days,
                      const GameDateValue& start, const GameDateValue& end,
                      WorldSimulation& world, TeamID managed_team_id);
  void playMatches(const GameDateValue& today, MatchScheduler& scheduler,
                   WorldSimulation& world, TeamID managed_team_id);
  void releasePlayers(const GameDateValue& today, WorldSimulation& world,
                      TeamID managed_team_id);
  void scheduleWindowFriendlies(const std::vector<GameDateValue>& match_days);
  void pairFriendlyNations(std::vector<Language>& nations,
                           const GameDateValue& day, std::mt19937& rng,
                           std::vector<Language>* leftovers);
  bool replaceUnavailablePlayers(Squad& squad, const GameDateValue& today);
  void replaceInjured(const GameDateValue& today);
  std::vector<PlayerID> pickSquad(Language nation, size_t size,
                                  const GameDateValue& date,
                                  const std::vector<PlayerID>& keep) const;
  void updateRatings(const International::Fixture& fixture);
  uint32_t addFixture(International::Fixture fixture);
  bool hasFixture(Language nation, const GameDateValue& date) const;
  const Finals* finalsFor(Language nation, const GameDateValue& date) const;
  void rebuildDutyIndex();
  void post(WorldSimulation& world, const GameDateValue& date,
            std::string title_key, std::string body_key,
            std::vector<std::string> args, bool read) const;

  std::shared_ptr<GameData> gamedata;
  std::vector<Team> teams;
  std::vector<International::Fixture> fixtures;
  std::vector<International::Group> groups;
  std::vector<Finals> finals;
  std::vector<Squad> squads;
  std::vector<Honour> honours;
  std::unordered_map<PlayerID, International::Record> records;
  std::unordered_map<PlayerID, Travel> travel;
  /** Player -> (start, until) of his current or announced duty. */
  std::unordered_map<PlayerID, std::pair<GameDateValue, GameDateValue>> duty;
  uint16_t planned_season = 0;
  uint32_t next_fixture_id = 1;
  ResultSink result_sink;
  FinalsSink finals_sink;
};
