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
#include <memory>
#include <unordered_map>
#include <vector>

#include "global/types.h"
#include "model/gamedate.h"
#include "model/injury.h"
#include "model/staff.h"
#include "model/training.h"

class DatabaseConnection;
class GameData;
class Inbox;

/** @brief Three-step risk band shown by the medical centre. */
enum class RiskBand : std::uint8_t
{
  Low,
  Moderate,
  High
};

/** @brief Days until a player is expected back (inclusive range). */
struct ReturnWindow
{
  int earliest_days = 0;
  int latest_days = 0;
};

/** @brief Observable facts behind a player's injury risk. */
struct InjuryRiskInputs
{
  int age = 25;
  int days_since_injury = -1; /*!< Since the last injury began; -1 = none. */
  int days_since_match = -1;  /*!< -1 = no match played yet. */
  float condition = 100.0f;
  float workload_ratio = 1.0f;   /*!< Acute:chronic load. */
  float staff_prevention = 1.0f; /*!< StaffEffects::injury_prevention. */
};

/** @brief Why a risk is raised (bit flags). */
enum RiskReason : std::uint8_t
{
  RISK_REASON_NONE = 0,
  RISK_REASON_AGE = 1 << 0,
  RISK_REASON_CONGESTION = 1 << 1,
  RISK_REASON_RECENT_INJURY = 1 << 2,
  RISK_REASON_FATIGUE = 1 << 3,
  RISK_REASON_WORKLOAD = 1 << 4,
};

struct InjuryRiskAssessment
{
  float multiplier = 1.0f; /*!< Relative to an average fresh player. */
  RiskBand band = RiskBand::Low;
  std::uint8_t reasons = RISK_REASON_NONE;
};

/** @brief One injured player of the managed squad. */
struct MedicalInjuryRow
{
  PlayerID player_id = 0;
  InjuryType type = InjuryType::None;
  std::uint16_t days_left = 0;
  InjurySeverity severity = InjurySeverity::Minor;
  ReturnWindow window;
  RiskBand reinjury = RiskBand::Low;
};

/** @brief Fitness and injury risk of one available player. */
struct MedicalRiskRow
{
  PlayerID player_id = 0;
  InjuryRiskAssessment risk;
  float condition = 100.0f;
  float sharpness = 0.0f;
  float workload_ratio = 1.0f;
  WorkloadRisk workload = WorkloadRisk::Low;
  float acute = 0.0f;   /*!< 7-day load per day. */
  float chronic = 0.0f; /*!< 28-day load per day. */
  /** Recovered from an injury within the recurrence window. */
  bool returning = false;
};

/**
 * @struct MedicalReport
 * @brief The managed club's medical centre: who is out, who is at risk and
 * what the medical staff contributes.
 */
struct MedicalReport
{
  std::vector<MedicalInjuryRow> injured; /*!< Longest layoff first. */
  std::vector<MedicalRiskRow> squad;     /*!< Highest risk first. */
  StaffEffects effects;
  std::vector<const StaffMember*> medical_staff; /*!< Physios and doctors. */
  float average_condition = 0.0f; /*!< Of the available players. */
  float average_sharpness = 0.0f;
};

/**
 * @brief Instructions of the medical staff for one player (bit flags,
 * persisted).
 */
enum MedicalFlag : std::uint8_t
{
  MEDICAL_FLAG_NONE = 0,
  /** Left out of the assistant's selections while anyone else is fit. */
  MEDICAL_FLAG_REST = 1 << 0,
  /** Should come off around the hour mark. */
  MEDICAL_FLAG_LIMIT_MINUTES = 1 << 1,
};

/** @brief One day of a player's load chart. */
struct LoadChartDay
{
  std::int32_t day = 0; /*!< Day ordinal. */
  float load = 0.0f;    /*!< Training and match load, in session units. */
  float ratio = 1.0f;   /*!< Acute:chronic ratio once the day was closed. */
  bool recorded = false;
};

namespace MedicalCentre
{
/** Days after an injury during which a recurrence is more likely. */
inline constexpr int RECURRENCE_WINDOW_DAYS = 60;
/** Days kept for the load chart. */
inline constexpr int LOAD_CHART_DAYS = 28;
/** Minute around which a player limited by the medical staff comes off. */
inline constexpr int MINUTE_LIMIT = 60;
/** Acute:chronic ratios of the low-risk zone and of a load spike. */
inline constexpr float LOAD_ZONE_LOW = 0.8f;
inline constexpr float LOAD_ZONE_HIGH = 1.3f;
inline constexpr float LOAD_SPIKE = 1.5f;

/**
 * Medical staff quality in [0, 1] from the layoff multiplier the staff
 * achieves (StaffEffects: 1.12 untrained ... 0.85 elite).
 */
float staffQuality(const StaffEffects& effects);

/**
 * The physio's estimate of the return: the remaining layoff with a margin
 * that is wider for setbacks than for early returns and narrows with a
 * better medical staff (10-30% of the layoff). Always contains the
 * remaining days.
 */
ReturnWindow returnWindow(std::uint16_t days_left, float staff_quality);

/**
 * Risk of a recurrence once back: muscle strains and long layoffs recur
 * more often, and so do injuries of players past 30; ACL tears are high.
 */
RiskBand reinjuryRisk(InjuryType type, std::uint16_t days_left, int age);

/**
 * Injury risk from observable factors only, with the multipliers of the
 * injury model: age over 25, four days or less since a match, a recent
 * injury, low condition and an acute:chronic load above 1.3, scaled by
 * the medical staff. Hidden traits are never used.
 */
InjuryRiskAssessment assess(const InjuryRiskInputs& inputs);

/** Language key naming @p band ("MEDICAL_RISK_LOW", ...). */
const char* bandKey(RiskBand band);

/**
 * Risk zone of an acute:chronic workload ratio: 0.8-1.3 is the low-risk
 * zone, a spike of 1.5 or more is high, and both a ratio between them and
 * an under-loaded player (below 0.8) are moderate.
 */
RiskBand loadBand(float ratio);

/** Whether a player with @p flags should come off at @p minute. */
bool substitutionDue(std::uint8_t flags, int minute);
}  // namespace MedicalCentre

/**
 * @class MedicalDesk
 * @brief The medical staff's day-to-day work for the managed club: rest and
 * minute-limit instructions, the 28-day load log behind the load chart,
 * aggravated injuries of players who play through pain and the staff's
 * warnings in the inbox.
 *
 * Persisted in MedicalFlags and MedicalLoad (assets/db/schema.sql) inside
 * the game's save transaction.
 */
class MedicalDesk
{
 public:
  /** Flags of @p player_id (MEDICAL_FLAG_NONE when he has none). */
  std::uint8_t flags(PlayerID player_id) const;
  bool hasFlag(PlayerID player_id, MedicalFlag flag) const
  {
    return (flags(player_id) & flag) != 0;
  }
  void setFlag(PlayerID player_id, MedicalFlag flag, bool enabled);
  /** Players the staff wants rested (not picked by the assistant). */
  bool isRested(PlayerID player_id) const
  {
    return hasFlag(player_id, MEDICAL_FLAG_REST);
  }

  /**
   * A player's match was applied (WorldSimulation::applyMatchConsequences
   * returned true). If he played carrying an injury he may aggravate it:
   * the chance is InjuryModel::aggravationChance() for his minutes, and
   * certain when the match engine injured him again. The layoff is drawn
   * by InjuryModel::aggravate() and shortened by the club's medical staff.
   * @return Whether the injury was aggravated.
   */
  bool afterMatch(GameData& gamedata, const GameDateValue& date,
                  PlayerID player_id, int minutes, bool engine_injured,
                  TeamID managed_team_id, Inbox& inbox) const;

  /**
   * Closes @p date (before the calendar moves on, so the day's training and
   * match loads are complete): logs every managed player's load and
   * acute:chronic ratio, forgets players who left and warns once a week
   * per player about load spikes and players selected while injured.
   */
  void onDayEnd(GameData& gamedata, const GameDateValue& date,
                TeamID managed_team_id, Inbox& inbox);

  /** The last LOAD_CHART_DAYS days up to @p today, oldest first. */
  std::array<LoadChartDay, MedicalCentre::LOAD_CHART_DAYS> loadChart(
      PlayerID player_id, std::int32_t today) const;

  void load(const std::shared_ptr<DatabaseConnection>& db_conn);
  /** Writes the state; must run inside the caller's transaction. */
  void save(const std::shared_ptr<DatabaseConnection>& db_conn) const;

 private:
  /** Ring of daily loads indexed by day ordinal modulo LOAD_CHART_DAYS. */
  struct LoadLog
  {
    std::array<std::int32_t, MedicalCentre::LOAD_CHART_DAYS> days{};
    std::array<float, MedicalCentre::LOAD_CHART_DAYS> load{};
    std::array<float, MedicalCentre::LOAD_CHART_DAYS> ratio{};
  };

  void record(PlayerID player_id, std::int32_t day, float load, float ratio);

  std::unordered_map<PlayerID, std::uint8_t> player_flags;
  /** Day of the last warning about each player. */
  std::unordered_map<PlayerID, std::int32_t> warned;
  std::unordered_map<PlayerID, LoadLog> logs;
};
