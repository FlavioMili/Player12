// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "global/types.h"
#include "model/gamedate.h"
#include "model/scout_expertise.h"

class GameData;
class Inbox;
class Player;
struct RoleFocus;

/**
 * @namespace ScoutingTuning
 * @brief Calibration of the scouting model. [P] marks designer priors.
 *
 * Potential is judged with the discrimination measured for youth selection
 * (world-realism 1.4 / 7): AUC 0.71 / 0.82 / 0.93 gives d' = sqrt(2) *
 * PhiInv(AUC) = 0.78 (single source), 1.29 (good coach eye) and 2.09
 * (combined department). The noise of a potential estimate is
 * POTENTIAL_POPULATION_SD / d', with d' growing from 0.78 at no knowledge to
 * 1.29 + 0.80 * judging potential at full knowledge. Attributes are easier to
 * observe than a ceiling, so their noise falls towards a small floor. [P]
 */
namespace ScoutingTuning
{
constexpr float OWN_KNOWLEDGE = 100.0f;
constexpr float SAME_LEAGUE_KNOWLEDGE = 30.0f;  /*!< [P] Domestic rivals. */
constexpr float SAME_COUNTRY_KNOWLEDGE = 18.0f; /*!< [P] Other division. */
constexpr float FREE_AGENT_KNOWLEDGE = 10.0f;   /*!< [P] Public records. */
constexpr float FOREIGN_KNOWLEDGE = 5.0f;       /*!< [P] */
constexpr float MAX_SCOUTED_KNOWLEDGE = 95.0f;  /*!< Only own staff see all. */
constexpr float DAILY_DECAY = 0.05f;            /*!< [P] ~18 points/year. */
constexpr float MATCH_OBSERVATION_GAIN = 6.0f;  /*!< Opponents faced. */
constexpr float PLAYER_ASSIGNMENT_DAILY_GAIN = 7.0f;
constexpr float COVERAGE_OBSERVATION_GAIN = 20.0f;
constexpr int COVERAGE_PLAYERS_PER_DAY = 5;
constexpr float REPORT_KNOWLEDGE = 40.0f; /*!< Coverage report threshold. */
constexpr int REPORT_COOLDOWN_DAYS = 60;  /*!< One report per player. */
constexpr int MAX_REPORTS_PER_ASSIGNMENT_DAY = 2;

constexpr float ATTRIBUTE_SD_FLOOR = 0.8f;
constexpr float ATTRIBUTE_SD_UNSEEN = 11.0f;
/** [P] Spread of an attribute within a position group (prior of the
 * shrinkage estimator). */
constexpr float ATTRIBUTE_POPULATION_SD = 12.0f;
constexpr float POTENTIAL_POPULATION_SD = 9.0f; /*!< [P] Spread of ceilings. */
constexpr float D_PRIME_SINGLE_SOURCE = 0.78f;
constexpr float D_PRIME_COACH_EYE = 1.29f;
constexpr float D_PRIME_COMBINED = 2.09f;
constexpr float RANGE_Z = 1.2816f; /*!< Ranges cover 80% of outcomes. */
/** Below this knowledge screens show the overall as a range, not a
 * number (the estimate is too uncertain for a point value). */
constexpr std::uint8_t RANGE_DISPLAY_KNOWLEDGE = 50;

constexpr std::int64_t DAILY_COST_DOMESTIC = 900;  /*!< [P] Travel, tickets. */
constexpr std::int64_t DAILY_COST_FOREIGN = 2'500; /*!< [P] */
constexpr std::int64_t DAILY_COST_FREE_AGENTS = 500;
constexpr std::uint16_t MIN_DURATION_DAYS = 3;
constexpr std::uint16_t MAX_DURATION_DAYS = 120;
constexpr float EXPECTED_WAGE_RAISE = 1.15f;  /*!< [P] Wage a mover asks. */
constexpr float UNLISTED_FEE_PREMIUM = 1.35f; /*!< [P] Prying a player out. */
constexpr float EXPIRING_FEE_DISCOUNT = 0.6f;
constexpr std::size_t MAX_REPORTS = 300;
constexpr std::size_t MAX_FINISHED_ASSIGNMENTS = 40;
constexpr std::size_t MAX_FOCUSES = 6;
}  // namespace ScoutingTuning

/**
 * @struct ScoutProfile
 * @brief A scout of the managed club (1-100 abilities).
 */
struct ScoutProfile
{
  std::uint32_t id = 0;
  std::string name;
  std::uint8_t judging_ability = 50;   /*!< Reads current ability. */
  std::uint8_t judging_potential = 50; /*!< Reads the ceiling. */
  Language nationality = Language::EN;
};

/** @brief What an assignment covers (values are persisted). */
enum class ScoutTargetKind : std::uint8_t
{
  Player = 0, /*!< target_id: PlayerID. */
  League,     /*!< target_id: LeagueID. */
  Country,    /*!< target_id: top LeagueID; covers its divisions and cup. */
  FreeAgents, /*!< target_id unused. */
  Region      /*!< target_id: Continent; every country on it. */
};

/** @brief Why an assignment could not be started. */
enum class ScoutAssignError : std::uint8_t
{
  None,
  NoTeam,
  UnknownScout,
  ScoutBusy,
  InvalidTarget,
  InvalidDuration,
  InsufficientFunds
};

/**
 * @struct ScoutAssignment
 * @brief A scout's trip; the cost is paid when it starts.
 */
struct ScoutAssignment
{
  std::uint32_t id = 0;
  std::uint32_t scout_id = 0;
  ScoutTargetKind kind = ScoutTargetKind::Player;
  std::uint32_t target_id = 0;
  GameDateValue start_date;
  std::uint16_t duration_days = 0;
  std::uint16_t days_done = 0;
  std::int64_t cost = 0;
  std::uint16_t players_observed = 0;
  std::uint16_t reports_filed = 0;
  bool finished = false;
};

/** @brief Recommendation grade of a report (values are persisted). */
enum class ScoutGrade : std::uint8_t
{
  A = 0, /*!< Fits a need, affordable and available. */
  B,     /*!< Two of the three. */
  C      /*!< At most one. */
};

/**
 * @struct ScoutReport
 * @brief A dated snapshot of a scout's opinion; never updated afterwards.
 */
struct ScoutReport
{
  std::uint32_t id = 0;
  GameDateValue date;
  PlayerID player_id = 0;
  std::uint32_t assignment_id = 0;
  std::uint32_t scout_id = 0;
  std::string scout_name;
  std::uint8_t knowledge = 0;  /*!< 0-100 at the time of the report. */
  std::uint8_t confidence = 0; /*!< Knowledge weighted by judging. */
  float overall = 0.0f;        /*!< Estimated current ability. */
  float overall_low = 0.0f;    /*!< 80% range of the estimate. */
  float overall_high = 0.0f;
  float potential_low = 0.0f;
  float potential_high = 0.0f;
  std::int64_t estimated_fee = 0;
  ScoutGrade grade = ScoutGrade::C;
  bool fits_need = false;
  bool affordable = false;
  bool available = false;
  bool seen = false; /*!< Opened by the manager on the scout's page. */
};

/**
 * @struct RecruitmentFocus
 * @brief A squad need scouts prioritise. Zero limits mean "no limit".
 */
struct RecruitmentFocus
{
  std::uint32_t id = 0;
  PlayerRole role = PlayerRole::UNKNOWN; /*!< UNKNOWN = any position. */
  std::uint8_t min_age = 15;
  std::uint8_t max_age = 40;
  std::int64_t max_fee = 0;
  std::uint32_t max_wage = 0;
  std::uint8_t min_ability = 0; /*!< Estimated overall. */
};

/** @brief Status bits of a shortlisted player (values are persisted). */
enum ShortlistFlag : std::uint8_t
{
  SHORTLIST_LISTED = 0x01,
  SHORTLIST_EXPIRING = 0x02,
  SHORTLIST_INJURED = 0x04
};

/**
 * @struct ShortlistEntry
 * @brief A tracked player with the last status alerts were based on.
 */
struct ShortlistEntry
{
  PlayerID player_id = 0;
  GameDateValue added;
  TeamID last_team = 0;
  std::uint8_t last_flags = 0;
};

/**
 * @struct ScoutKnowledge
 * @brief Accumulated knowledge of one player beyond the relation baseline.
 */
struct ScoutKnowledge
{
  float knowledge = 0.0f;
  std::uint8_t judging_ability = 50;
  std::uint8_t judging_potential = 50;
  std::int32_t last_report_day = 0; /*!< Day ordinal, 0 = none. */
};

/**
 * @struct ScoutedAttribute
 * @brief Estimated attribute with its 80% range.
 */
struct ScoutedAttribute
{
  std::string name;
  float estimate = 0.0f;
  float low = 0.0f;
  float high = 0.0f;
};

/**
 * @struct ScoutedPlayerView
 * @brief Everything the UI may show about a player's ability.
 *
 * Contains estimates only: for other clubs' players the true attributes,
 * overall and potential are never exposed. Own players are known exactly
 * (except their potential, which is always a judgement).
 */
struct ScoutedPlayerView
{
  PlayerID player_id = 0;
  bool own = false;
  std::uint8_t knowledge = 0; /*!< 0-100. */
  float overall = 0.0f;
  float overall_low = 0.0f;
  float overall_high = 0.0f;
  float potential_low = 0.0f;
  float potential_high = 0.0f;
  std::int64_t estimated_value = 0;
  std::vector<ScoutedAttribute> attributes; /*!< In stats-map order. */
  std::optional<std::uint32_t> latest_report_id;
};

/**
 * @struct ScoutedPlayerRow
 * @brief Compact estimated row for searches and tables.
 */
struct ScoutedPlayerRow
{
  PlayerID player_id = 0;
  TeamID team_id = 0;
  PlayerRole role = PlayerRole::UNKNOWN;
  std::uint8_t age = 0;
  std::uint8_t knowledge = 0;
  float overall = 0.0f;     /*!< Centre of the overall range. */
  float overall_low = 0.0f; /*!< 80% range of the overall estimate. */
  float overall_high = 0.0f;
  float potential_low = 0.0f;
  float potential_high = 0.0f;
  std::int64_t estimated_value = 0;
  std::uint32_t wage = 0;
  std::uint8_t contract_years = 0;
  bool listed = false;
  bool injured = false;
  bool matches_focus = false;
  bool shortlisted = false;
};

/**
 * @struct ScoutSearchFilter
 * @brief Player search on estimated values. Zero limits mean "no limit".
 */
struct ScoutSearchFilter
{
  PlayerRole role = PlayerRole::UNKNOWN;
  std::uint8_t min_age = 0;
  std::uint8_t max_age = 0;
  float min_overall = 0.0f;
  std::int64_t max_value = 0;
  LeagueID league_id = 0; /*!< 0 = every league (and free agents). */
  bool free_agents_only = false;
  bool focus_matches_only = false;
  std::uint8_t min_knowledge = 0;
  std::size_t limit = 250;
};

/**
 * @struct SquadComparisonRow
 * @brief One position: own depth versus the best shortlisted option.
 */
struct SquadComparisonRow
{
  PlayerRole role = PlayerRole::UNKNOWN;
  std::uint8_t own_count = 0;
  PlayerID own_best_id = 0;
  float own_best_overall = 0.0f;
  PlayerID candidate_id = 0; /*!< 0 = nobody shortlisted here. */
  float candidate_overall = 0.0f;
  float candidate_potential_low = 0.0f;
  float candidate_potential_high = 0.0f;
  std::uint8_t candidate_knowledge = 0;
};

/**
 * @struct ScoutingState
 * @brief Persisted state of the managed club's scouting department.
 */
struct ScoutingState
{
  TeamID team_id = 0;
  std::uint32_t next_assignment_id = 1;
  std::uint32_t next_report_id = 1;
  std::uint32_t next_focus_id = 1;
  std::unordered_map<PlayerID, ScoutKnowledge> knowledge;
  std::vector<ScoutAssignment> assignments;
  std::vector<ScoutReport> reports; /*!< Oldest first. */
  std::vector<RecruitmentFocus> focuses;
  std::vector<ShortlistEntry> shortlist;
  /** Background and experience of the club's scouts, by scout id. */
  std::unordered_map<std::uint32_t, ScoutExpertise> expertise;
};

/** @brief What a scout is doing, which decides his page. */
enum class ScoutStatus : std::uint8_t
{
  OnAssignment,    /*!< Live assignment and the reports filed so far. */
  IdleWithHistory, /*!< Past assignments and their reports. */
  IdleNew          /*!< Nothing yet: send him somewhere. */
};

/**
 * @struct ScoutSummary
 * @brief One row of the scouts list.
 */
struct ScoutSummary
{
  ScoutProfile profile;
  ScoutStatus status = ScoutStatus::IdleNew;
  std::uint32_t active_assignment_id = 0; /*!< 0 when idle. */
  std::size_t unread_reports = 0;
  std::size_t total_reports = 0;
  std::size_t finished_assignments = 0;
};

/**
 * @class ScoutingSystem
 * @brief Recruitment under uncertainty for the managed club.
 *
 * Every (club, player) pair has a knowledge level: a baseline from the
 * relation (own squad, same league, same country, abroad) plus what scouts
 * and matches added, decaying slowly back to the baseline. A scout observes
 * the truth plus a fixed standard-normal offset per (club, player,
 * attribute) scaled by a noise that shrinks with knowledge and his judging;
 * the displayed estimate regresses that observation towards what is typical
 * for the player's position at his club (normal-prior shrinkage), so
 * estimates converge towards the truth, never flicker, and poorly known
 * players do not top rankings through noise alone. Scouts are sent on paid
 * assignments (a player, a league, a country, a continent or the free-agent
 * pool) and perform according to their regional expertise (home country,
 * languages, experience; see ScoutExpertiseModel); they file
 * graded reports and prioritise the club's recruitment focuses. Shortlisted
 * players raise alerts when they are listed, enter their final contract year,
 * get injured or move.
 * Const cache lookups may run concurrently; returned scout lists are snapshots.
 * Career mutations and borrowed assignment/state references require exclusive
 * access to the underlying career, including its GameData.
 */
class ScoutingSystem
{
 public:
  static constexpr std::size_t ROLE_COUNT =
      static_cast<std::size_t>(std::to_underlying(PlayerRole::UNKNOWN));
  using ScoutProvider = std::function<std::vector<ScoutProfile>(TeamID)>;
  using BudgetProvider = std::function<std::int64_t(TeamID)>;

  explicit ScoutingSystem(std::shared_ptr<GameData> gamedata);

  /**
   * Replaces the source of the club's scouts. Without it the club's staff
   * members in the Scout role are used (see defaultScouts()).
   */
  void setScoutProvider(ScoutProvider provider);

  /**
   * Replaces the source of the transfer money a club can commit, used to
   * grade reports as affordable. Without it the board's allowance capped by
   * the club's spare cash (ClubEconomy::availableTransferBudget) is used.
   */
  void setBudgetProvider(BudgetProvider provider);

  /**
   * The club's scouts: its Scout staff (judging ability and potential from
   * the staff attributes). Worlds without a staff roster fall back to
   * scouts derived from the club's reputation and the world seed.
   */
  static std::vector<ScoutProfile> defaultScouts(const GameData& gamedata,
                                                 TeamID team_id);

  /** Binds the department to the managed club; a new club starts afresh. */
  void setManagedTeam(TeamID team_id);
  TeamID getManagedTeam() const { return state.team_id; }

  // ---- Hooks ----

  /** Assignment progress, knowledge decay and shortlist alerts. */
  void onDayAdvanced(const GameDateValue& date, Inbox& inbox);
  /** Drops the cached squad priors (squads or attributes changed), so
   * estimates match what a reloaded save computes. */
  void invalidatePriors() const
  {
    const std::scoped_lock lock(cache_mutex);
    priors.clear();
  }

  /** The managed club faced @p opponent_id: its players become known. */
  void onManagedMatch(TeamID opponent_id);

  // ---- Knowledge & estimates ----

  /** Effective knowledge 0-100 of a player. */
  float knowledgeOf(PlayerID player_id) const;

  /** Knowledge without any scouting (relation to the managed club). */
  float baselineKnowledge(const Player& player) const;

  /** Adds observation (diminishing returns towards the cap). */
  void observe(PlayerID player_id, float amount, const ScoutProfile* scout);

  /** Estimated view of a player; nullopt if unknown. */
  std::optional<ScoutedPlayerView> view(PlayerID player_id) const;

  /** Estimated overall and potential range without attribute detail. */
  std::optional<ScoutedPlayerRow> row(PlayerID player_id) const;

  /** Search on estimated values, best estimated overall first. */
  std::vector<ScoutedPlayerRow> search(const ScoutSearchFilter& filter) const;

  /** True if the estimated row satisfies @p focus. */
  static bool matchesFocus(const RecruitmentFocus& focus,
                           const ScoutedPlayerRow& row);

  // ---- Scouts & assignments ----

  /** Current scouts (re-read from the staff roster on each call). */
  std::vector<ScoutProfile> getScouts() const;
  const std::vector<ScoutAssignment>& getAssignments() const
  {
    return state.assignments;
  }

  /** Active assignment of a scout, if any. */
  const ScoutAssignment* activeAssignment(std::uint32_t scout_id) const;

  /** What a scout is doing (drives the scout page). */
  ScoutStatus scoutStatus(std::uint32_t scout_id) const;

  /** Every scout with his status and report counters, in roster order. */
  std::vector<ScoutSummary> scoutSummaries() const;

  /** Background, languages and experience of a scout. */
  ScoutExpertise expertiseOf(std::uint32_t scout_id) const;

  /** Expected effectiveness of sending a scout to a target (0.6x-1.5x). */
  ScoutEffectiveness effectiveness(std::uint32_t scout_id, ScoutTargetKind kind,
                                   std::uint32_t target_id) const;

  /** Top league (country) of a league. */
  LeagueID countryOf(LeagueID league_id) const;

  /** Price of an assignment (0 if the target is invalid). */
  std::int64_t assignmentCost(ScoutTargetKind kind, std::uint32_t target_id,
                              std::uint16_t days) const;

  /** Starts an assignment and books its cost on @p date. */
  ScoutAssignError startAssignment(const GameDateValue& date,
                                   std::uint32_t scout_id, ScoutTargetKind kind,
                                   std::uint32_t target_id, std::uint16_t days);

  /** Recalls a scout; travel is booked in advance, so nothing is refunded. */
  bool cancelAssignment(std::uint32_t assignment_id);

  // ---- Reports ----

  const std::vector<ScoutReport>& getReports() const { return state.reports; }

  /** Reports not yet opened, of every scout or of one. */
  std::size_t unreadReports() const;
  std::size_t unreadReports(std::uint32_t scout_id) const;

  /** Marks a scout's reports as opened; false if none were unread. */
  bool markReportsSeen(std::uint32_t scout_id);

  // ---- Recruitment focus ----

  const std::vector<RecruitmentFocus>& getFocuses() const
  {
    return state.focuses;
  }
  /** Adds (id 0) or updates a focus; returns its id, 0 if full/invalid. */
  std::uint32_t upsertFocus(RecruitmentFocus focus);
  bool removeFocus(std::uint32_t focus_id);

  // ---- Shortlist ----

  const std::vector<ShortlistEntry>& getShortlist() const
  {
    return state.shortlist;
  }
  bool isShortlisted(PlayerID player_id) const;
  bool addToShortlist(const GameDateValue& date, PlayerID player_id);
  bool removeFromShortlist(PlayerID player_id);

  /** Own depth per position against the best shortlisted estimate. */
  std::vector<SquadComparisonRow> compareWithSquad() const;

  // ---- Persistence ----

  const ScoutingState& getState() const { return state; }
  void restore(ScoutingState restored);

 private:
  struct Noise
  {
    float attribute_sd = 0.0f;
    float potential_sd = 0.0f;
  };

  struct Estimate
  {
    float value = 0.0f;
    float sd = 0.0f; /*!< Posterior standard deviation. */
  };

  static constexpr std::size_t POSITION_GROUPS = 4;
  /** Mean attributes per position group of a club (possible_stats order). */
  using TeamPrior = std::array<std::vector<float>, POSITION_GROUPS>;

  float priorMean(TeamID team_id, std::size_t group, std::size_t index) const;
  Estimate estimateStat(const Player& player, const std::string& stat,
                        float value, float noise_sd) const;

  /** Mean judging (ability, potential) of the club's scouts. */
  std::pair<float, float> departmentJudging() const;
  Noise noiseFor(const Player& player, float knowledge) const;
  float standardNormal(PlayerID player_id, std::uint64_t key) const;
  const RoleFocus* focusFor(PlayerRole role) const;
  float estimatedOverall(const Player& player, float attribute_sd,
                         float* overall_sd) const;
  ScoutedPlayerRow makeRow(const Player& player) const;
  static std::int64_t estimatedValue(const Player& player,
                                     float estimated_overall,
                                     float estimated_potential);
  std::int64_t estimatedFee(const Player& player,
                            const ScoutedPlayerRow& row) const;
  bool anyFocusMatches(const ScoutedPlayerRow& row) const;
  std::vector<LeagueID> worldCountries() const;
  /** Stores the generated background of scouts seen for the first time. */
  void ensureExpertise();
  /** Countries of an assignment with their weights, plus its league. */
  std::vector<TargetCountry> targetCountries(const ScoutExpertise& expertise,
                                             ScoutTargetKind kind,
                                             std::uint32_t target_id,
                                             LeagueID* league) const;
  /** The scout as he performs with @p multiplier (sharper or blunter). */
  static ScoutProfile effectiveScout(const ScoutProfile& scout,
                                     float multiplier);
  void addExperience(std::uint32_t scout_id, LeagueID league_id);
  bool isForeign(const Player& player) const;
  bool isForeignTarget(ScoutTargetKind kind, std::uint32_t target_id) const;
  const ScoutProfile* findScout(std::uint32_t scout_id) const;
  void refreshScouts() const;
  void progressAssignment(ScoutAssignment& assignment,
                          const GameDateValue& date, std::int32_t ordinal,
                          Inbox& inbox);
  void coverageDay(ScoutAssignment& assignment, const ScoutProfile& scout,
                   const GameDateValue& date, std::int32_t ordinal,
                   Inbox& inbox);
  std::vector<PlayerID> coverage(const ScoutAssignment& assignment) const;
  const ScoutReport& fileReport(const GameDateValue& date, const Player& player,
                                const ScoutProfile& scout, std::int32_t ordinal,
                                std::uint32_t assignment_id);
  void finishAssignment(ScoutAssignment& assignment, const GameDateValue& date,
                        Inbox& inbox);
  void checkShortlist(const GameDateValue& date, Inbox& inbox);
  void pruneHistory();
  std::uint8_t shortlistFlags(const Player& player) const;
  /** Best own overall per PlayerRole index (0 = none). */
  std::array<float, ROLE_COUNT> ownBestByRole() const;

  std::shared_ptr<GameData> gamedata;
  ScoutProvider scout_provider;
  BudgetProvider budget_provider;
  /** Cache lookups call one another while building summaries and estimates. */
  mutable std::recursive_mutex cache_mutex;
  mutable std::vector<ScoutProfile> scouts; /*!< Cache of the provider. */
  mutable std::unordered_map<TeamID, TeamPrior> priors; /*!< Daily cache. */
  /** Backgrounds of scouts not stored yet (identical once stored). */
  mutable std::unordered_map<std::uint32_t, ScoutExpertise> generated;
  ScoutingState state;
};

/** Language key of a grade ("SCOUT_GRADE_A"...). */
const char* scoutGradeKey(ScoutGrade grade);

/** Language key of a target kind ("SCOUT_TARGET_PLAYER"...). */
const char* scoutTargetKindKey(ScoutTargetKind kind);

/** Language key of an assignment error ("SCOUT_ERROR_BUSY"...). */
const char* scoutAssignErrorKey(ScoutAssignError error);
