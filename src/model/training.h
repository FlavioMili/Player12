// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "global/types.h"

class GameData;
class Player;
class Team;
struct StatsConfig;

/**
 * @file training.h
 * @brief Weekly training microcycle, workload, individual focus and tactical
 * familiarity.
 *
 * A club trains to a template of six days between two matches (MD+1 recovery,
 * MD+2 off, MD-4 strength, MD-3 endurance, MD-2 speed/tactics, MD-1
 * activation; rules-regulations 5.1, world-realism 8). The template is mapped
 * onto the real fixture list every day; weeks with two matches collapse to
 * recovery and activation only when auto-adjust is on.
 *
 * Effects (all bounded around the Balanced preset, which reproduces the
 * calibrated world simulation):
 * - Each session adds training load to a player's acute (7-day) and chronic
 *   (28-day) workload. Load spikes relative to recent history raise the
 *   injury hazard modestly (x1.0-1.5); a high chronic load is mildly
 *   protective (x0.9) (world-realism 2.6/9.2: no magic ratio).
 * - Sessions drain condition, recovery and rest days speed recovery.
 * - The week's volume, the preset mix and the staff scale the development
 *   rate (T = 0.7-1.3) and steer growth towards the drilled attributes.
 * - Tactical and match-preparation sessions build familiarity with the
 *   current strategy and shape; changing either costs familiarity.
 */

/** @brief Training days between two matches of a one-match week. */
enum class MicrocycleDay : std::uint8_t
{
  MdPlus1 = 0, /*!< Recovery for starters, compensation for the rest. */
  MdPlus2,     /*!< Usually off. */
  MdMinus4,    /*!< Strength / high intensity. */
  MdMinus3,    /*!< Endurance / volume. */
  MdMinus2,    /*!< Speed / tactics. */
  MdMinus1,    /*!< Activation and set pieces. */
  COUNT
};

inline constexpr std::size_t MICROCYCLE_DAYS =
    static_cast<std::size_t>(std::to_underlying(MicrocycleDay::COUNT));

/** @brief Session content (values are persisted). */
enum class SessionType : std::uint8_t
{
  Rest = 0,
  Recovery,  /*!< Pool, bike, mobility. */
  Fitness,   /*!< Strength and conditioning. */
  Technical, /*!< Ball work: passing, dribbling, handling. */
  Attacking, /*!< Finishing and attacking movement. */
  Defending, /*!< Defensive shape, duels and tackling. */
  Tactical,  /*!< Team shape and units (builds familiarity). */
  MatchPrep, /*!< Opponent preparation, set pieces, activation. */
  COUNT
};

inline constexpr std::size_t SESSION_TYPE_COUNT =
    static_cast<std::size_t>(std::to_underlying(SessionType::COUNT));

/** @brief Session or squad-wide intensity (values are persisted). */
enum class TrainingIntensity : std::uint8_t
{
  Low = 0,
  Normal,
  High,
  COUNT
};

/** @brief Schedule templates (values are persisted). */
enum class TrainingPreset : std::uint8_t
{
  Balanced = 0,
  Fitness,
  Attacking,
  Defending,
  YouthDevelopment, /*!< Technical emphasis, faster growth for <= 21s. */
  Light,            /*!< Low load for congested periods. */
  Custom,           /*!< A template edited by hand. */
  COUNT
};

/** @brief Individual training focus of a player (values are persisted). */
enum class TrainingFocus : std::uint8_t
{
  None = 0,
  Physical,          /*!< Pace, physicality, stamina. */
  Attacking,         /*!< Shooting, dribbling. */
  Playmaking,        /*!< Passing, vision. */
  Defending,         /*!< Defending, physicality. */
  Goalkeeping,       /*!< Handling and positioning. */
  RetrainStriker,    /*!< Attributes of the striker role. */
  RetrainMidfielder, /*!< Attributes of the midfield roles. */
  RetrainDefender,   /*!< Attributes of the defensive roles. */
  COUNT
};

inline constexpr std::size_t TRAINING_FOCUS_COUNT =
    static_cast<std::size_t>(std::to_underlying(TrainingFocus::COUNT));

/** @brief One day of the template. */
struct TrainingSlot
{
  SessionType session = SessionType::Rest;
  TrainingIntensity intensity = TrainingIntensity::Normal;
  bool operator==(const TrainingSlot&) const = default;
};

using Microcycle = std::array<TrainingSlot, MICROCYCLE_DAYS>;

/** @brief Strategy sliders and outfield shape the squad has drilled. */
struct TacticSnapshot
{
  static constexpr std::size_t MAX_POSITIONS = 10;
  std::array<float, 5> sliders{};
  std::array<Vector2F, MAX_POSITIONS> positions{}; /*!< Sorted by x, then y. */
  std::uint8_t outfield = 0;
  bool valid = false;
};

/** @brief What a club does on one calendar day. */
struct TrainingDay
{
  bool match_day = false;
  MicrocycleDay day = MicrocycleDay::MdPlus2;
  TrainingSlot slot;
  bool congested = false; /*!< Two matches within four days. */
  bool adjusted = false;  /*!< Template replaced by the congestion rule. */
};

/**
 * @struct TeamTrainingPlan
 * @brief A club's schedule, familiarity and training load.
 */
struct TeamTrainingPlan
{
  TrainingPreset preset = TrainingPreset::Balanced;
  TrainingIntensity intensity = TrainingIntensity::Normal; /*!< Squad-wide. */
  bool auto_congestion = true;
  Microcycle slots{};
  float familiarity = 70.0f; /*!< 0-100 with the drilled tactic. */
  TacticSnapshot tactic;
  float week_load = 0.0f;      /*!< Session load accumulated this week. */
  float last_week_load = 0.0f; /*!< Load of the previous full week. */
  std::int32_t last_match_day = 0;

  // Transient, rebuilt every day.
  TrainingDay today;
};

/**
 * @struct PlayerTrainingState
 * @brief Individual focus and workload history of a player.
 *
 * Loads are in session units (a normal endurance session = 1.0, a full match
 * = 1.8) averaged per day; acute and chronic are exponentially weighted over
 * 7 and 28 days.
 */
struct PlayerTrainingState
{
  TrainingFocus focus = TrainingFocus::None;
  float acute = 0.0f;
  float chronic = 0.0f;
  float pending = 0.0f; /*!< Load of the current day. */
  float trend = 0.0f;   /*!< Smoothed weekly change of the overall rating. */
};

/** @brief Injury risk band from workload and freshness. */
enum class WorkloadRisk : std::uint8_t
{
  Low,
  Moderate,
  High
};

/** @brief Severity of an assistant recommendation. */
enum class AdviceSeverity : std::uint8_t
{
  Info,
  Suggestion,
  Warning
};

/**
 * @struct TrainingAdvice
 * @brief One assistant recommendation as a language key plus arguments
 * (formatted with formatLocalized()).
 */
struct TrainingAdvice
{
  AdviceSeverity severity = AdviceSeverity::Info;
  std::string key;
  std::vector<std::string> args;
};

/**
 * @struct AdviceInputs
 * @brief Squad facts the assistant bases the advice on.
 */
struct AdviceInputs
{
  TrainingPreset preset = TrainingPreset::Balanced;
  TrainingIntensity intensity = TrainingIntensity::Normal;
  bool auto_congestion = true;
  float familiarity = 70.0f;
  float average_condition = 100.0f;
  int tired_players = 0; /*!< Fit players below 75 condition. */
  std::vector<std::string> spike_players; /*!< High workload risk. */
  int injured_players = 0;
  int matches_next_week = 0;
  int young_prospects = 0;                /*!< <= 21 with clear room to grow. */
  std::vector<std::string> missing_roles; /*!< Language keys. */
};

/**
 * @class TrainingRegistry
 * @brief Training plans of every club and training state of every player.
 */
class TrainingRegistry
{
 public:
  /** Plan of a club, created with the Balanced template when missing. */
  TeamTrainingPlan& plan(TeamID team_id);

  /** Plan of a club or nullptr. */
  const TeamTrainingPlan* findPlan(TeamID team_id) const;

  /** State of a player, created with a typical workload when missing. */
  PlayerTrainingState& player(PlayerID player_id);

  /** State of a player or nullptr. */
  const PlayerTrainingState* findPlayer(PlayerID player_id) const;

  const std::unordered_map<TeamID, TeamTrainingPlan>& plans() const
  {
    return team_plans;
  }
  const std::unordered_map<PlayerID, PlayerTrainingState>& players() const
  {
    return player_states;
  }

  /** Replaces everything with persisted state. */
  void restore(std::unordered_map<TeamID, TeamTrainingPlan> plans,
               std::unordered_map<PlayerID, PlayerTrainingState> players);

  /** Drops players that no longer exist. */
  template <typename PlayerMap>
  void prunePlayers(const PlayerMap& existing)
  {
    std::erase_if(player_states, [&existing](const auto& entry)
                  { return !existing.contains(entry.first); });
  }

  void clear();

 private:
  std::unordered_map<TeamID, TeamTrainingPlan> team_plans;
  std::unordered_map<PlayerID, PlayerTrainingState> player_states;
};

/**
 * @namespace TrainingModel
 * @brief Pure training rules (no game state), unit tested in isolation.
 */
namespace TrainingModel
{
/** Load of a full match, in session units. [P: match ~1.8x MD-3] */
inline constexpr float MATCH_LOAD = 1.8f;

/** Template of a preset (Custom returns Balanced). */
Microcycle presetMicrocycle(TrainingPreset preset);

/** Training load of a session at the squad-wide intensity. */
float sessionLoad(TrainingSlot slot, TrainingIntensity squad_intensity);

/** Load of a Balanced week at normal intensity (six training days). */
float balancedWeekLoad();

/** Typical average daily load (training plus one match a week). */
float typicalDailyLoad();

/**
 * Training-injury exposure of a session relative to an average training day
 * (the Balanced template averages 1.0 over its six days, so its weekly
 * exposure equals the calibrated 6 x 1.2 training hours).
 */
float sessionInjuryExposure(TrainingSlot slot,
                            TrainingIntensity squad_intensity);

/**
 * Maps a calendar day onto the microcycle.
 * @param days_since_last Days since the previous match (<= 0: unknown).
 * @param days_to_next Days to the next match (0: today, < 0: unknown).
 * @param ordinal Day ordinal, used to cycle through the template when no
 * fixture is near.
 * @return std::nullopt on match days.
 */
std::optional<MicrocycleDay> dayFor(int days_since_last, int days_to_next,
                                    std::int32_t ordinal);

/** True when the previous and next match are at most four days apart. */
bool isCongested(int days_since_last, int days_to_next);

/** The day's session after the congestion rule. */
TrainingDay scheduleDay(const TeamTrainingPlan& plan, int days_since_last,
                        int days_to_next, std::int32_t ordinal);

/** Condition lost in a session with @p load for a player with @p stamina. */
float conditionDrain(float load, float stamina);

/** Extra share of the condition deficit recovered by a session type. */
float recoveryShare(SessionType session);

/** Match-sharpness gain of a session (sharpness decays 0.5 per day). */
float sharpnessGain(TrainingSlot slot);

/** Acute:chronic workload ratio (1.0 with no history). */
float workloadRatio(const PlayerTrainingState& state);

/**
 * Injury hazard multiplier from the workload history: 1 + 0.5 * (ACWR - 1.3)
 * above 1.3 (capped at 1.5), 0.9 with a high, stable chronic load.
 */
float workloadRiskMultiplier(const PlayerTrainingState& state);

/** Risk band shown to the user. */
WorkloadRisk workloadRisk(const PlayerTrainingState& state, float condition);

/** Closes a day: folds the pending load into the acute and chronic loads. */
void rollDay(PlayerTrainingState& state);

/**
 * Development-rate multiplier from the week's training volume, the preset
 * and the coaching quality (0-1), before facilities.
 */
float developmentMultiplier(const TeamTrainingPlan& plan, int age,
                            float coaching_quality);

/**
 * Relative attention of the plan's template to each attribute (Pace,
 * Shooting, Passing, Dribbling, Defending, Physicality, Stamina, Vision,
 * Goalkeeping), used to steer development.
 */
std::array<float, 9> scheduleAttributeMix(const TeamTrainingPlan& plan);

/**
 * Attributes trained by an individual focus (same order). Role retraining
 * uses the target role category's weights from @p config.
 */
std::array<float, 9> focusAttributeMix(TrainingFocus focus,
                                       const StatsConfig& config);

/** Attribute names in the order used by the mixes. */
const std::array<const char*, 9>& attributeNames();

/**
 * Applies the directed part of a week's growth: 30% follows the schedule
 * mix over the role's attributes (overall-preserving), 35% follows the
 * individual focus (any attribute, capped at potential + 5). Physical
 * attributes only grow up to 25.
 * @return The growth left for the regular role-weighted development.
 */
float applyDirectedGrowth(Player& player, float growth,
                          const TeamTrainingPlan& plan, TrainingFocus focus,
                          const StatsConfig& config);

/** Snapshot of a club's current strategy and outfield shape. */
TacticSnapshot snapshotOf(const Team& team);

/**
 * Share of familiarity lost when the drilled tactic changes from @p before
 * to @p after (0 for identical tactics, at most 0.6).
 */
float familiarityLoss(const TacticSnapshot& before,
                      const TacticSnapshot& after);

/** Share of the familiarity gap closed by one session. */
float familiarityGain(TrainingSlot slot);

/** Assistant recommendations, most important first (never empty). */
std::vector<TrainingAdvice> advise(const AdviceInputs& inputs);

/** Language keys. */
const char* sessionKey(SessionType session);
const char* intensityKey(TrainingIntensity intensity);
const char* presetKey(TrainingPreset preset);
const char* focusKey(TrainingFocus focus);
const char* dayKey(MicrocycleDay day);
const char* riskKey(WorkloadRisk risk);
}  // namespace TrainingModel

/**
 * @namespace TrainingSystem
 * @brief Training processing on the live world (called by WorldSimulation).
 */
namespace TrainingSystem
{
/** Days to each club's next fixture (0 = today), for clubs with one soon. */
using FixtureOutlook = std::unordered_map<TeamID, std::uint8_t>;

/**
 * Plans the day of every club (session, congestion, familiarity).
 * @param outlook Upcoming fixtures, or nullptr when the calendar is unknown
 * (a weekly rhythm after the last match is assumed).
 * @param training_period False during the June holiday.
 */
void planDay(GameData& gamedata, std::int32_t ordinal,
             const FixtureOutlook* outlook, bool training_period);

/** Folds yesterday's load into the workload history of every player. */
void rollWorkloads(GameData& gamedata);

/**
 * Trains one fit player of a club on day @p ordinal: adds load, drains
 * condition, speeds recovery, builds sharpness. Players who played outside
 * the club's rhythm recover first; on MD+1 the players who did not play do
 * a compensation session.
 * @return Training-injury exposure (0 when not training), including the
 * workload and medical staff multipliers.
 */
double trainPlayer(GameData& gamedata, Player& player, std::int32_t ordinal);

/** Adds match load and returns the match injury hazard multiplier. */
double recordMatchLoad(GameData& gamedata, const Player& player,
                       std::uint8_t minutes);

/** Marks a match day for a club (microcycle anchor, familiarity). */
void recordTeamMatch(GameData& gamedata, TeamID team_id, std::int32_t ordinal);

/** Development multiplier T (facilities, staff, volume, preset). */
float trainingQuality(const GameData& gamedata, const Player& player);

/** Directed growth for a player (see TrainingModel::applyDirectedGrowth). */
float directGrowth(GameData& gamedata, Player& player, float growth);

/** Records a player's weekly change of overall rating (trend arrows). */
void recordWeeklyGrowth(GameData& gamedata, PlayerID player_id, float delta);

/** Ends the training week of every club (load bookkeeping). */
void endWeek(GameData& gamedata);

/** Squad facts for the assistant's advice. */
AdviceInputs adviceInputs(const GameData& gamedata, TeamID team_id,
                          int matches_next_week);

/**
 * Tactical familiarity of a club with its current strategy and formation,
 * in [0, 1] (1 = fully drilled). It grows with tactical and
 * match-preparation sessions, matches and time, and drops when sliders or
 * the outfield shape change. Intended match-engine use: scale positional
 * discipline / decision noise, e.g. by 0.9 + 0.1 * familiarity.
 */
float tacticalFamiliarity(const GameData& gamedata, TeamID team_id);
}  // namespace TrainingSystem
