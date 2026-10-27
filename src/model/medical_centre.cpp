// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/medical_centre.h"

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <utility>

#include "database/database_connection.h"
#include "database/gamedata.h"
#include "global/global.h"
#include "model/inbox.h"
#include "model/player.h"
#include "model/team.h"
#include "model/training.h"
#include "model/world_rng.h"
#include "model/world_tuning.h"

namespace
{
/** StaffEffects::layoff_multiplier of an untrained and of an elite staff. */
constexpr float WORST_LAYOFF = 1.12f;
constexpr float BEST_LAYOFF = 0.85f;
constexpr float WIDEST_MARGIN = 0.30f;
constexpr float NARROWEST_MARGIN = 0.10f;
/** Multipliers of the injury model (world_simulation.cpp, training.cpp). */
constexpr float AGE_SLOPE = 0.03f;
constexpr int AGE_FROM = 25;
constexpr float RECENT_INJURY = 1.3f;
constexpr float FATIGUE = 1.2f;
constexpr float FATIGUE_CONDITION = 60.0f;
constexpr int CONGESTION_DAYS = 4;
constexpr float WORKLOAD_THRESHOLD = 1.3f;
constexpr float WORKLOAD_SLOPE = 0.5f;
constexpr float WORKLOAD_CAP = 1.5f;
/** Band limits on the combined multiplier. */
constexpr float MODERATE_FROM = 1.25f;
constexpr float HIGH_FROM = 1.6f;
/** Age from which injuries recur more often. */
constexpr int VETERAN_AGE = 30;
/** The staff repeats a warning about a player at most once a week. */
constexpr std::int32_t WARNING_INTERVAL_DAYS = 7;
/** Keys separating the aggravation rolls from the match-injury rolls. */
constexpr std::uint64_t AGGRAVATION_ROLL_SALT = 0xA66A'0001ULL;
constexpr std::uint64_t AGGRAVATION_DRAW_SALT = 0xA66A'0002ULL;

std::size_t slotOf(std::int32_t day)
{
  const int slot = day % MedicalCentre::LOAD_CHART_DAYS;
  return static_cast<std::size_t>(
      slot < 0 ? slot + MedicalCentre::LOAD_CHART_DAYS : slot);
}

template <typename Read>
void forEachRow(const DatabaseConnection& db, const char* sql, Read read)
{
  sqlite3_stmt* stmt = db.prepareStatement(sql);
  while (sqlite3_step(stmt) == SQLITE_ROW) read(stmt);
  sqlite3_finalize(stmt);
}

void execute(const DatabaseConnection& db, const char* sql)
{
  sqlite3_exec(db.getRaw(), sql, nullptr, nullptr, nullptr);
}

void postWarning(Inbox& inbox, const GameDateValue& date, TeamID team_id,
                 const char* title_key, const char* body_key,
                 const std::vector<const Player*>& players)
{
  if (players.empty()) return;
  std::string names;
  for (const Player* player : players)
  {
    if (!names.empty()) names += ", ";
    names += player->getName();
  }
  InboxMessage message;
  message.date = date;
  message.category = InboxCategory::Injury;
  message.title_key = title_key;
  message.body_key = body_key;
  message.args = {names};
  if (players.size() == 1) message.player_id = players.front()->getId();
  message.team_id = team_id;
  inbox.add(std::move(message));
}
}  // namespace

float MedicalCentre::staffQuality(const StaffEffects& effects)
{
  return std::clamp(
      (WORST_LAYOFF - effects.layoff_multiplier) / (WORST_LAYOFF - BEST_LAYOFF),
      0.0f, 1.0f);
}

ReturnWindow MedicalCentre::returnWindow(std::uint16_t days_left,
                                         float staff_quality)
{
  if (days_left == 0) return {};
  const float margin =
      WIDEST_MARGIN - (WIDEST_MARGIN - NARROWEST_MARGIN) *
                          std::clamp(staff_quality, 0.0f, 1.0f);
  const auto days = static_cast<float>(days_left);
  // Setbacks are more common than early returns: twice the margin later.
  const auto early = static_cast<int>(std::lround(days * margin * 0.5f));
  const auto late = static_cast<int>(std::lround(days * margin));
  return {std::max(1, days_left - early), days_left + late};
}

RiskBand MedicalCentre::reinjuryRisk(InjuryType type, std::uint16_t days_left,
                                     int age)
{
  if (type == InjuryType::None) return RiskBand::Low;
  if (type == InjuryType::KneeAcl) return RiskBand::High;
  int level = InjuryModel::isMuscle(type) ? 1 : 0;
  if (InjuryModel::severity(days_left) == InjurySeverity::Major) ++level;
  if (age >= VETERAN_AGE) ++level;
  return static_cast<RiskBand>(std::min(level, 2));
}

InjuryRiskAssessment MedicalCentre::assess(const InjuryRiskInputs& inputs)
{
  InjuryRiskAssessment result;
  std::byte reasons{0};
  float multiplier = 1.0f;
  if (inputs.age > AGE_FROM)
  {
    multiplier *=
        std::exp(AGE_SLOPE * static_cast<float>(inputs.age - AGE_FROM));
    // Only flagged once it matters (about +16% at 30).
    if (inputs.age >= VETERAN_AGE)
      reasons |= static_cast<std::byte>(RISK_REASON_AGE);
  }
  if (inputs.days_since_match >= 0 &&
      inputs.days_since_match <= CONGESTION_DAYS)
  {
    using Fitness = WorldTuning::Fitness;
    multiplier *=
        static_cast<float>(0.5 * (Fitness::CONGESTION_MUSCLE_MULTIPLIER +
                                  Fitness::CONGESTION_OTHER_MULTIPLIER));
    reasons |= static_cast<std::byte>(RISK_REASON_CONGESTION);
  }
  if (inputs.days_since_injury >= 0 &&
      inputs.days_since_injury <= RECURRENCE_WINDOW_DAYS)
  {
    multiplier *= RECENT_INJURY;
    reasons |= static_cast<std::byte>(RISK_REASON_RECENT_INJURY);
  }
  if (inputs.condition < FATIGUE_CONDITION)
  {
    multiplier *= FATIGUE;
    reasons |= static_cast<std::byte>(RISK_REASON_FATIGUE);
  }
  if (inputs.workload_ratio > WORKLOAD_THRESHOLD)
  {
    multiplier *= std::min(
        WORKLOAD_CAP,
        1.0f + WORKLOAD_SLOPE * (inputs.workload_ratio - WORKLOAD_THRESHOLD));
    reasons |= static_cast<std::byte>(RISK_REASON_WORKLOAD);
  }
  result.reasons = std::to_integer<std::uint8_t>(reasons);
  multiplier *= inputs.staff_prevention;
  result.multiplier = multiplier;
  if (multiplier >= HIGH_FROM)
    result.band = RiskBand::High;
  else if (multiplier >= MODERATE_FROM)
    result.band = RiskBand::Moderate;
  else
    result.band = RiskBand::Low;
  return result;
}

const char* MedicalCentre::bandKey(RiskBand band)
{
  using enum RiskBand;
  switch (band)
  {
    case Moderate:
      return "MEDICAL_RISK_MODERATE";
    case High:
      return "MEDICAL_RISK_HIGH";
    case Low:
      break;
  }
  return "MEDICAL_RISK_LOW";
}

RiskBand MedicalCentre::loadBand(float ratio)
{
  using enum RiskBand;
  if (ratio >= LOAD_SPIKE) return High;
  if (ratio > LOAD_ZONE_HIGH || ratio < LOAD_ZONE_LOW) return Moderate;
  return Low;
}

bool MedicalCentre::substitutionDue(std::uint8_t flags, int minute)
{
  const std::byte stored = static_cast<std::byte>(flags);
  const std::byte limit = static_cast<std::byte>(MEDICAL_FLAG_LIMIT_MINUTES);
  return (stored & limit) != std::byte{0} && minute >= MINUTE_LIMIT;
}

// ---------------------------------------------------------------------------
// MedicalDesk
// ---------------------------------------------------------------------------

std::uint8_t MedicalDesk::flags(PlayerID player_id) const
{
  const auto found = player_flags.find(player_id);
  return found == player_flags.end() ? std::to_underlying(MEDICAL_FLAG_NONE)
                                     : found->second;
}

void MedicalDesk::setFlag(PlayerID player_id, MedicalFlag flag, bool enabled)
{
  const std::byte current = static_cast<std::byte>(flags(player_id));
  const std::byte bit = static_cast<std::byte>(flag);
  const std::byte value = enabled ? current | bit : current & ~bit;
  if (value == std::byte{0})
    player_flags.erase(player_id);
  else
    player_flags[player_id] = std::to_integer<std::uint8_t>(value);
}

bool MedicalDesk::afterMatch(GameData& gamedata, const GameDateValue& date,
                             PlayerID player_id, int minutes,
                             bool engine_injured, TeamID managed_team_id,
                             Inbox& inbox) const
{
  auto& players = gamedata.getPlayers();
  const auto found = players.find(player_id);
  if (found == players.end()) return false;
  Player& player = found->second;
  PlayerDynamics& dynamics = player.mutableDynamics();
  const std::int32_t ordinal = dayOrdinal(date);
  // Fit players, and players hurt in this very match, have nothing to
  // aggravate.
  if (dynamics.injury_days == 0 || dynamics.injury == InjuryType::None ||
      dynamics.last_injury_day == ordinal)
    return false;
  const std::uint64_t seed = gamedata.getWorldSeed();
  if (const double chance = engine_injured ? 1.0
                                           : InjuryModel::aggravationChance(
                                                 dynamics.injury_days, minutes);
      WorldRng::hashUniform(seed, RngDomain::MatchInjury,
                            static_cast<std::uint64_t>(ordinal),
                            player_id ^ AGGRAVATION_ROLL_SALT) >= chance)
    return false;

  WorldRng rng = WorldRng::stream(seed, RngDomain::MatchInjury,
                                  static_cast<std::uint64_t>(ordinal),
                                  player_id ^ AGGRAVATION_DRAW_SALT);
  const std::uint16_t carried = dynamics.injury_days;
  const Injury injury = InjuryModel::aggravate(rng, dynamics.injury, carried);
  const float layoff =
      player.getTeamId() == FREE_AGENTS_TEAM_ID
          ? 1.0f
          : gamedata.getStaff().effects(player.getTeamId()).layoff_multiplier;
  const long days =
      std::max(static_cast<long>(carried) + 1,
               std::lround(static_cast<float>(injury.days) * layoff));
  dynamics.injury_days = static_cast<std::uint16_t>(std::min(days, 400L));
  dynamics.last_injury = dynamics.injury;
  dynamics.last_injury_day = ordinal;

  if (player.getTeamId() == managed_team_id &&
      managed_team_id != FREE_AGENTS_TEAM_ID)
  {
    InboxMessage message;
    message.date = date;
    message.category = InboxCategory::Injury;
    message.title_key = "MEDICAL_AGGRAVATED_TITLE";
    message.body_key = "MEDICAL_AGGRAVATED_BODY";
    message.args = {player.getName(),
                    std::string("@") + InjuryModel::nameKey(dynamics.injury),
                    std::to_string(carried),
                    std::to_string(dynamics.injury_days)};
    message.player_id = player_id;
    message.team_id = managed_team_id;
    inbox.add(std::move(message));
  }
  return true;
}

void MedicalDesk::record(PlayerID player_id, std::int32_t day, float load,
                         float ratio)
{
  LoadLog& log = logs[player_id];
  const std::size_t slot = slotOf(day);
  log.days[slot] = day;
  log.load[slot] = load;
  log.ratio[slot] = ratio;
}

void MedicalDesk::onDayEnd(GameData& gamedata, const GameDateValue& date,
                           TeamID managed_team_id, Inbox& inbox)
{
  if (managed_team_id == FREE_AGENTS_TEAM_ID) return;
  const auto team = gamedata.getTeam(managed_team_id);
  if (!team) return;
  const std::int32_t today = dayOrdinal(date);
  const std::vector<PlayerID>& squad = team->get().getPlayerIDs();
  const std::unordered_set<PlayerID> members(squad.begin(), squad.end());
  std::erase_if(logs, [&members](const auto& entry)
                { return !members.contains(entry.first); });
  std::erase_if(player_flags, [&members](const auto& entry)
                { return !members.contains(entry.first); });
  std::erase_if(warned, [&members](const auto& entry)
                { return !members.contains(entry.first); });

  const TrainingRegistry& training = gamedata.getTraining();
  const auto dueWarning = [&](PlayerID player_id)
  {
    const auto last = warned.find(player_id);
    return last == warned.end() ||
           today - last->second >= WARNING_INTERVAL_DAYS;
  };
  std::vector<const Player*> spikes;
  for (const PlayerID player_id : squad)
  {
    const auto player = gamedata.getPlayer(player_id);
    if (!player) continue;
    PlayerTrainingState state;
    if (const PlayerTrainingState* found = training.findPlayer(player_id))
      state = *found;
    const float load = state.pending;
    // The ratio once the day is folded into the acute and chronic loads.
    TrainingModel::rollDay(state);
    const float ratio = TrainingModel::workloadRatio(state);
    record(player_id, today, load, ratio);
    if (player->get().isAvailable() && ratio >= MedicalCentre::LOAD_SPIKE &&
        !isRested(player_id) && dueWarning(player_id))
      spikes.push_back(&player->get());
  }

  std::vector<const Player*> carrying;
  for (const Player* starter : team->get().getLineup().starters())
  {
    if (starter && !starter->isAvailable() && dueWarning(starter->getId()) &&
        std::ranges::find(spikes, starter) == spikes.end())
      carrying.push_back(starter);
  }
  for (const auto* list : {&spikes, &carrying})
    for (const Player* player : *list) warned[player->getId()] = today;
  // Read the morning after, when the manager sees them.
  const GameDateValue morning = date + 1;
  postWarning(inbox, morning, managed_team_id, "MEDICAL_WARN_LOAD_TITLE",
              "MEDICAL_WARN_LOAD_BODY", spikes);
  postWarning(inbox, morning, managed_team_id, "MEDICAL_WARN_CARRYING_TITLE",
              "MEDICAL_WARN_CARRYING_BODY", carrying);
}

std::array<LoadChartDay, MedicalCentre::LOAD_CHART_DAYS> MedicalDesk::loadChart(
    PlayerID player_id, std::int32_t today) const
{
  std::array<LoadChartDay, MedicalCentre::LOAD_CHART_DAYS> chart{};
  const auto log = logs.find(player_id);
  for (std::size_t index = 0; index < chart.size(); ++index)
  {
    LoadChartDay& entry = chart[index];
    entry.day = today - static_cast<std::int32_t>(chart.size() - 1 - index);
    if (log == logs.end()) continue;
    const std::size_t slot = slotOf(entry.day);
    if (log->second.days[slot] != entry.day) continue;
    entry.load = log->second.load[slot];
    entry.ratio = log->second.ratio[slot];
    entry.recorded = true;
  }
  return chart;
}

void MedicalDesk::load(const std::shared_ptr<DatabaseConnection>& db_conn)
{
  *this = MedicalDesk();
  const DatabaseConnection& db = *db_conn;
  forEachRow(db, "SELECT player_id, flags, warned_day FROM MedicalFlags;",
             [this](sqlite3_stmt* stmt)
             {
               const auto player_id =
                   static_cast<PlayerID>(sqlite3_column_int64(stmt, 0));
               if (const auto value = static_cast<std::uint8_t>(
                       sqlite3_column_int(stmt, 1) &
                       (MEDICAL_FLAG_REST | MEDICAL_FLAG_LIMIT_MINUTES));
                   value != MEDICAL_FLAG_NONE)
                 player_flags[player_id] = value;
               if (const int day = sqlite3_column_int(stmt, 2); day != 0)
                 warned[player_id] = day;
             });
  forEachRow(db, "SELECT player_id, day, load, ratio FROM MedicalLoad;",
             [this](sqlite3_stmt* stmt)
             {
               record(static_cast<PlayerID>(sqlite3_column_int64(stmt, 0)),
                      sqlite3_column_int(stmt, 1),
                      static_cast<float>(sqlite3_column_double(stmt, 2)),
                      static_cast<float>(sqlite3_column_double(stmt, 3)));
             });
}

void MedicalDesk::save(const std::shared_ptr<DatabaseConnection>& db_conn) const
{
  const DatabaseConnection& db = *db_conn;
  execute(db, "DELETE FROM MedicalFlags;");
  sqlite3_stmt* stmt = db.prepareStatement(
      "INSERT INTO MedicalFlags (player_id, flags, warned_day) "
      "VALUES (?, ?, ?);");
  std::unordered_set<PlayerID> ids;
  for (const auto& [player_id, value] : player_flags) ids.insert(player_id);
  for (const auto& [player_id, day] : warned) ids.insert(player_id);
  for (const PlayerID player_id : ids)
  {
    const auto day = warned.find(player_id);
    sqlite3_bind_int64(stmt, 1, player_id);
    sqlite3_bind_int(stmt, 2, flags(player_id));
    sqlite3_bind_int(stmt, 3, day == warned.end() ? 0 : day->second);
    db.executeStep(stmt);
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);

  execute(db, "DELETE FROM MedicalLoad;");
  stmt = db.prepareStatement(
      "INSERT INTO MedicalLoad (player_id, day, load, ratio) "
      "VALUES (?, ?, ?, ?);");
  for (const auto& [player_id, log] : logs)
  {
    for (std::size_t slot = 0; slot < log.days.size(); ++slot)
    {
      if (log.days[slot] == 0) continue;
      sqlite3_bind_int64(stmt, 1, player_id);
      sqlite3_bind_int(stmt, 2, log.days[slot]);
      sqlite3_bind_double(stmt, 3, log.load[slot]);
      sqlite3_bind_double(stmt, 4, log.ratio[slot]);
      db.executeStep(stmt);
      sqlite3_reset(stmt);
    }
  }
  sqlite3_finalize(stmt);
}
