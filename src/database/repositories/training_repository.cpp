// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "database/repositories/training_repository.h"

#include <sqlite3.h>

#include <charconv>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
std::string columnText(sqlite3_stmt* stmt, int column)
{
  const unsigned char* text = sqlite3_column_text(stmt, column);
  return text ? reinterpret_cast<const char*>(text) : "";
}

std::vector<float> parseNumbers(std::string_view text)
{
  std::vector<float> values;
  const char* cursor = text.data();
  const char* end = text.data() + text.size();
  while (cursor < end)
  {
    float value = 0.0f;
    const auto [next, error] = std::from_chars(cursor, end, value);
    if (error != std::errc()) break;
    values.push_back(value);
    cursor = next < end && *next == ',' ? next + 1 : next;
  }
  return values;
}

std::string encodeSchedule(const Microcycle& slots)
{
  std::string text;
  for (const TrainingSlot& slot : slots)
  {
    if (!text.empty()) text += ',';
    text +=
        std::format("{},{}", static_cast<int>(std::to_underlying(slot.session)),
                    static_cast<int>(std::to_underlying(slot.intensity)));
  }
  return text;
}

void decodeSchedule(const std::string& text, Microcycle& slots)
{
  const std::vector<float> values = parseNumbers(text);
  if (values.size() != 2 * slots.size()) return;
  for (std::size_t i = 0; i < slots.size(); ++i)
  {
    const auto session = static_cast<int>(values[2 * i]);
    const auto intensity = static_cast<int>(values[2 * i + 1]);
    if (session < 0 || session >= static_cast<int>(SESSION_TYPE_COUNT) ||
        intensity < 0 ||
        intensity >=
            static_cast<int>(std::to_underlying(TrainingIntensity::COUNT)))
      continue;
    slots[i] = {static_cast<SessionType>(session),
                static_cast<TrainingIntensity>(intensity)};
  }
}

std::string encodeTactic(const TacticSnapshot& tactic)
{
  if (!tactic.valid) return "";
  std::string text = std::to_string(tactic.outfield);
  for (const float slider : tactic.sliders) text += std::format(",{}", slider);
  for (std::uint8_t i = 0; i < tactic.outfield; ++i)
    text += std::format(",{},{}", tactic.positions[i].x, tactic.positions[i].y);
  return text;
}

TacticSnapshot decodeTactic(const std::string& text)
{
  TacticSnapshot tactic;
  const std::vector<float> values = parseNumbers(text);
  if (values.size() < 6) return tactic;
  const auto outfield = static_cast<std::size_t>(values[0]);
  if (outfield > TacticSnapshot::MAX_POSITIONS ||
      values.size() != 6 + 2 * outfield)
    return tactic;
  for (std::size_t i = 0; i < tactic.sliders.size(); ++i)
    tactic.sliders[i] = values[1 + i];
  for (std::size_t i = 0; i < outfield; ++i)
    tactic.positions[i] = {values[6 + 2 * i], values[7 + 2 * i]};
  tactic.outfield = static_cast<std::uint8_t>(outfield);
  tactic.valid = true;
  return tactic;
}
}  // namespace

TrainingRepository::TrainingRepository(std::shared_ptr<DatabaseConnection> conn)
    : db_conn(std::move(conn))
{
}

std::unordered_map<TeamID, TeamTrainingPlan> TrainingRepository::loadPlans()
    const
{
  std::unordered_map<TeamID, TeamTrainingPlan> plans;
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "SELECT team_id, preset, intensity, auto_congestion, schedule, "
      "familiarity, tactic, week_load, last_week_load, last_match_day FROM "
      "TeamTraining;");
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    TeamTrainingPlan plan;
    const int preset = sqlite3_column_int(stmt, 1);
    const int intensity = sqlite3_column_int(stmt, 2);
    if (preset >= 0 &&
        preset < static_cast<int>(std::to_underlying(TrainingPreset::COUNT)))
      plan.preset = static_cast<TrainingPreset>(preset);
    if (intensity >= 0 && intensity < static_cast<int>(std::to_underlying(
                                          TrainingIntensity::COUNT)))
      plan.intensity = static_cast<TrainingIntensity>(intensity);
    plan.auto_congestion = sqlite3_column_int(stmt, 3) != 0;
    plan.slots = TrainingModel::presetMicrocycle(plan.preset);
    decodeSchedule(columnText(stmt, 4), plan.slots);
    plan.familiarity = static_cast<float>(sqlite3_column_double(stmt, 5));
    plan.tactic = decodeTactic(columnText(stmt, 6));
    plan.week_load = static_cast<float>(sqlite3_column_double(stmt, 7));
    plan.last_week_load = static_cast<float>(sqlite3_column_double(stmt, 8));
    plan.last_match_day = sqlite3_column_int(stmt, 9);
    plans.try_emplace(static_cast<TeamID>(sqlite3_column_int(stmt, 0)),
                      std::move(plan));
  }
  sqlite3_finalize(stmt);
  return plans;
}

std::unordered_map<PlayerID, PlayerTrainingState>
TrainingRepository::loadPlayers() const
{
  std::unordered_map<PlayerID, PlayerTrainingState> players;
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "SELECT player_id, focus, acute, chronic, pending, trend FROM "
      "PlayerTraining;");
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    PlayerTrainingState state;
    if (const int focus = sqlite3_column_int(stmt, 1);
        focus >= 0 && focus < static_cast<int>(TRAINING_FOCUS_COUNT))
      state.focus = static_cast<TrainingFocus>(focus);
    state.acute = static_cast<float>(sqlite3_column_double(stmt, 2));
    state.chronic = static_cast<float>(sqlite3_column_double(stmt, 3));
    state.pending = static_cast<float>(sqlite3_column_double(stmt, 4));
    state.trend = static_cast<float>(sqlite3_column_double(stmt, 5));
    players.try_emplace(static_cast<PlayerID>(sqlite3_column_int64(stmt, 0)),
                        state);
  }
  sqlite3_finalize(stmt);
  return players;
}

void TrainingRepository::replaceAll(const TrainingRegistry& registry) const
{
  // Rows are compared with what is stored: only changed, new and removed
  // plans and players are written.
  struct StoredPlan
  {
    int preset = 0;
    int intensity = 0;
    int auto_congestion = 0;
    std::string schedule;
    double familiarity = 0.0;
    std::string tactic;
    double week_load = 0.0;
    double last_week_load = 0.0;
    int last_match_day = 0;
    bool seen = false;
  };
  std::unordered_map<TeamID, StoredPlan> stored_plans;
  sqlite3_stmt* select = db_conn->prepareStatement(
      "SELECT team_id, preset, intensity, auto_congestion, schedule, "
      "familiarity, tactic, week_load, last_week_load, last_match_day FROM "
      "TeamTraining;");
  while (sqlite3_step(select) == SQLITE_ROW)
  {
    StoredPlan row;
    row.preset = sqlite3_column_int(select, 1);
    row.intensity = sqlite3_column_int(select, 2);
    row.auto_congestion = sqlite3_column_int(select, 3);
    row.schedule = columnText(select, 4);
    row.familiarity = sqlite3_column_double(select, 5);
    row.tactic = columnText(select, 6);
    row.week_load = sqlite3_column_double(select, 7);
    row.last_week_load = sqlite3_column_double(select, 8);
    row.last_match_day = sqlite3_column_int(select, 9);
    stored_plans.try_emplace(static_cast<TeamID>(sqlite3_column_int(select, 0)),
                             std::move(row));
  }
  sqlite3_finalize(select);

  sqlite3_stmt* plan_stmt = db_conn->prepareStatement(
      "INSERT OR REPLACE INTO TeamTraining (team_id, preset, intensity, "
      "auto_congestion, schedule, familiarity, tactic, week_load, "
      "last_week_load, last_match_day) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
  for (const auto& [team_id, plan] : registry.plans())
  {
    const std::string schedule = encodeSchedule(plan.slots);
    const std::string tactic = encodeTactic(plan.tactic);
    if (const auto found = stored_plans.find(team_id);
        found != stored_plans.end())
    {
      StoredPlan& row = found->second;
      row.seen = true;
      if (row.preset == static_cast<int>(std::to_underlying(plan.preset)) &&
          row.intensity ==
              static_cast<int>(std::to_underlying(plan.intensity)) &&
          row.auto_congestion == (plan.auto_congestion ? 1 : 0) &&
          row.schedule == schedule &&
          row.familiarity == static_cast<double>(plan.familiarity) &&
          row.tactic == tactic &&
          row.week_load == static_cast<double>(plan.week_load) &&
          row.last_week_load == static_cast<double>(plan.last_week_load) &&
          row.last_match_day == plan.last_match_day)
        continue;
    }
    sqlite3_bind_int(plan_stmt, 1, team_id);
    sqlite3_bind_int(plan_stmt, 2,
                     static_cast<int>(std::to_underlying(plan.preset)));
    sqlite3_bind_int(plan_stmt, 3,
                     static_cast<int>(std::to_underlying(plan.intensity)));
    sqlite3_bind_int(plan_stmt, 4, plan.auto_congestion ? 1 : 0);
    sqlite3_bind_text(plan_stmt, 5, schedule.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(plan_stmt, 6, static_cast<double>(plan.familiarity));
    sqlite3_bind_text(plan_stmt, 7, tactic.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(plan_stmt, 8, static_cast<double>(plan.week_load));
    sqlite3_bind_double(plan_stmt, 9, static_cast<double>(plan.last_week_load));
    sqlite3_bind_int(plan_stmt, 10, plan.last_match_day);
    db_conn->executeStep(plan_stmt);
    sqlite3_reset(plan_stmt);
  }
  sqlite3_finalize(plan_stmt);
  sqlite3_stmt* remove_plan =
      db_conn->prepareStatement("DELETE FROM TeamTraining WHERE team_id = ?;");
  for (const auto& [team_id, row] : stored_plans)
  {
    if (row.seen) continue;
    sqlite3_bind_int(remove_plan, 1, team_id);
    db_conn->executeStep(remove_plan);
    sqlite3_reset(remove_plan);
  }
  sqlite3_finalize(remove_plan);

  struct StoredPlayer
  {
    int focus = 0;
    double acute = 0.0;
    double chronic = 0.0;
    double pending = 0.0;
    double trend = 0.0;
    bool seen = false;
  };
  std::unordered_map<PlayerID, StoredPlayer> stored_players;
  stored_players.reserve(registry.players().size());
  select = db_conn->prepareStatement(
      "SELECT player_id, focus, acute, chronic, pending, trend FROM "
      "PlayerTraining;");
  while (sqlite3_step(select) == SQLITE_ROW)
  {
    stored_players.try_emplace(
        static_cast<PlayerID>(sqlite3_column_int64(select, 0)),
        StoredPlayer{
            sqlite3_column_int(select, 1), sqlite3_column_double(select, 2),
            sqlite3_column_double(select, 3), sqlite3_column_double(select, 4),
            sqlite3_column_double(select, 5), false});
  }
  sqlite3_finalize(select);

  sqlite3_stmt* player_stmt = db_conn->prepareStatement(
      "INSERT OR REPLACE INTO PlayerTraining (player_id, focus, acute, "
      "chronic, pending, trend) VALUES (?, ?, ?, ?, ?, ?);");
  for (const auto& [player_id, state] : registry.players())
  {
    if (const auto found = stored_players.find(player_id);
        found != stored_players.end())
    {
      StoredPlayer& row = found->second;
      row.seen = true;
      if (row.focus == static_cast<int>(std::to_underlying(state.focus)) &&
          row.acute == static_cast<double>(state.acute) &&
          row.chronic == static_cast<double>(state.chronic) &&
          row.pending == static_cast<double>(state.pending) &&
          row.trend == static_cast<double>(state.trend))
        continue;
    }
    sqlite3_bind_int64(player_stmt, 1, player_id);
    sqlite3_bind_int(player_stmt, 2,
                     static_cast<int>(std::to_underlying(state.focus)));
    sqlite3_bind_double(player_stmt, 3, static_cast<double>(state.acute));
    sqlite3_bind_double(player_stmt, 4, static_cast<double>(state.chronic));
    sqlite3_bind_double(player_stmt, 5, static_cast<double>(state.pending));
    sqlite3_bind_double(player_stmt, 6, static_cast<double>(state.trend));
    db_conn->executeStep(player_stmt);
    sqlite3_reset(player_stmt);
  }
  sqlite3_finalize(player_stmt);
  sqlite3_stmt* remove_player = db_conn->prepareStatement(
      "DELETE FROM PlayerTraining WHERE player_id = ?;");
  for (const auto& [player_id, row] : stored_players)
  {
    if (row.seen) continue;
    sqlite3_bind_int64(remove_player, 1, player_id);
    db_conn->executeStep(remove_player);
    sqlite3_reset(remove_player);
  }
  sqlite3_finalize(remove_player);
}
