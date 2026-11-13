// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "global/types.h"
#include "model/match_body.h"
#include "model/match_brain.h"
#include "tools/match_sandbox_drill.h"

/**
 * Training sessions: drills for a whole squad at once (the sandbox's
 * Training mode). Each drill of a session is an ordinary drill run with the
 * squad on the pitch and nobody else, and gives a results table with a row
 * per player as he finishes.
 */

/** One squad member in a session. */
struct TrainingPlayer
{
  std::string name;
  PlayerRole role = PlayerRole::CM;
  std::map<std::string, float> stats;
  int height = 180;
  Foot foot = Foot::Right;

  /** A stat by name (0-100), 0 if he has none. */
  [[nodiscard]] float stat(const std::string& stat) const;
};

/** One column of a training drill's results table. */
struct TrainingColumn
{
  const char* name = "";
  const char* tooltip = nullptr;
};

/** A player's row of the results: the cells as shown, and the numbers they
 * sort by (one each, in column order). */
struct TrainingRow
{
  /** Index of the player in the squad. */
  std::size_t player = 0;
  std::vector<std::string> cells;
  std::vector<double> values;
};

/** A drill the whole squad does at once. */
class TrainingDrill : public Drill
{
 public:
  explicit TrainingDrill(const std::vector<TrainingPlayer>& squad_ref)
      : squad(squad_ref)
  {
  }

  std::vector<DrillParameter> parameters() override { return {}; }
  /** The squad, alternately home and away (at most MAX_PLAYERS); keepers
   * run as outfielders, so the keeper's own movement stays out of it. */
  std::vector<DrillPlayerSpec> players() const override;
  std::vector<DrillMetric> metrics() const override { return {}; }

  /** The results table's columns after the player's name. */
  [[nodiscard]] virtual std::vector<TrainingColumn> columns() const = 0;
  /** The rows of the players done so far, in the order they finished. */
  [[nodiscard]] virtual std::vector<TrainingRow> results() const = 0;
  /** A player's top speed in this drill (m/s), once he has finished it. */
  [[nodiscard]] virtual std::optional<float> topSpeed(
      std::size_t /*player*/) const
  {
    return std::nullopt;
  }

  /** Ten a side, no keepers: the most the engine fields at once. */
  static constexpr std::size_t MAX_PLAYERS = 20;

 protected:
  const std::vector<TrainingPlayer>& squad;
};

/** A training run using all current Set-up settings, including each side's
 * independent brain and body. No recorder. */
std::unique_ptr<DrillRun> startTrainingRun(TrainingDrill& drill,
                                        const StatsConfig& config);

/** A diagnostic run with the whole squad on one brain and body
 * (a drill run takes each side's from the sandbox, and the squad is split
 * across both sides). No recorder. */
std::unique_ptr<DrillRun> startTrainingRun(TrainingDrill& drill,
                                           const StatsConfig& config,
                                           BrainId brain, BodyId body);

/** A session: the squad, and its drills in order. */
struct TrainingSession
{
  std::string name;
  std::string summary;
  std::vector<TrainingPlayer> squad;
  std::vector<std::unique_ptr<TrainingDrill>> drills;
};

/** The sessions on offer: a name, a summary, and how to make one for a
 * squad. */
struct TrainingSessionInfo
{
  const char* name;
  const char* summary;
  std::unique_ptr<TrainingSession> (*make)(std::vector<TrainingPlayer> squad);
};
const std::vector<TrainingSessionInfo>& trainingSessions();

/** Diagnostic (match_sandbox --trace-laps SECONDS [PLAYER]): runs the Running
 * session's laps for `squad` headless (the Binturong brain and body) and
 * prints overtakes, slow-downs and run-ins; with `follow`, the route of
 * each player whose name contains it, every half second. */
int runLapsTrace(const std::vector<TrainingPlayer>& squad, const StatsConfig& config,
                 double seconds, const std::string& follow = {});
