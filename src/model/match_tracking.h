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
#include <vector>

#include "global/types.h"
#include "model/match_pitch.h"
#include "model/match_point.h"

/**
 * Grid and thresholds of the per-match tracking. Positions are stored in
 * each side's attacking frame: x runs from the own goal line (0) to the
 * opponent's (1) and y = 0 is the attacking side's left touchline, the
 * frame ShotRecord uses.
 */
namespace MatchTracking
{
/** Columns along the pitch (thirds are 4 columns each). */
inline constexpr std::size_t GRID_COLUMNS = 12;
/** Rows across the pitch (flanks are 3 rows each). */
inline constexpr std::size_t GRID_ROWS = 9;
inline constexpr std::size_t CELLS = GRID_COLUMNS * GRID_ROWS;
/** Momentum buckets: five clock minutes each, up to 120 minutes. */
inline constexpr int BUCKET_MINUTES = 5;
inline constexpr std::size_t BUCKETS = 24;
/** Stats entries tracked (starters, substitutes and replacements). */
inline constexpr std::size_t MAX_PLAYERS = 40;
/** A completed open-play pass that gains this much ground towards the
 * opponent goal, ending outside the passer's own defensive 40%, is
 * progressive. */
inline constexpr float PROGRESSIVE_METRES = 10.0F;
inline constexpr float PROGRESSIVE_MIN_END_X = 0.4F;
/** The closest pressing player within this distance of the carrier applies
 * a pressure (counted once per player and possession spell). */
inline constexpr float PRESSURE_RADIUS_METRES = 4.0F;

/** Cell of a position in the attacking frame. */
std::size_t cellOf(float x, float y);
/** Momentum bucket of a clock minute in a period (added time stays in the
 * period's last bucket). */
std::size_t bucketOf(float minute, int period);
}  // namespace MatchTracking

/** @brief Tracked numbers of one stats entry (one player's stint). */
struct TrackedPlayer
{
  std::array<std::uint16_t, MatchTracking::CELLS> touches{};
  std::uint16_t touch_count = 0;
  /** Sums of the touch positions (attacking frame). */
  float sum_x = 0.0F;
  float sum_y = 0.0F;
  std::uint16_t progressive_passes = 0;
  std::uint16_t pressures = 0;
  /** xG of the shots his passes created. */
  float expected_assists = 0.0F;
  /** Possession spell of his last pressure (counts once per spell). */
  double pressure_spell = -1.0;
};

/** @brief How a tracked shot came about (same order as the SHOT events). */
struct TrackedShot
{
  bool set_piece = false;
  bool header = false;
  bool penalty = false;
};

/**
 * Cheap per-match recorder fed by the match engine: touch maps, completed
 * passes between team-mates, progressive passes, pressures, expected
 * assists and final-third touches over time. Players are the engine's stats
 * indices (MatchEngine::getPlayerStats()). Storage is allocated on the first
 * recorded event, so a disabled recorder (background fixtures) costs
 * nothing.
 */
class MatchTracker
{
 public:
  void setEnabled(bool on) { enabled = on; }
  [[nodiscard]] bool isEnabled() const { return enabled; }

  /** A touch at a pitch position (engine frame: home attacks x = 1). */
  void touch(std::size_t player, bool home, PitchPoint position,
             const MatchPitch& pitch, float minute, int period);
  /** A pass leaves the passer's foot (origin in the engine frame). */
  void passReleased(PitchPoint origin, const MatchPitch& pitch, bool open_play);
  /** The last released pass reached a team-mate, on this pitch. */
  void passCompleted(std::size_t passer, std::size_t receiver, bool home,
                     PitchPoint reception, const MatchPitch& pitch);
  /** A shot; @p creator is the stats index of the chance creator, or
   * MatchTracking::MAX_PLAYERS when none. */
  void shot(std::size_t creator, float xg, TrackedShot kind);
  /** A pressing player close to the carrier during @p spell. */
  void pressure(std::size_t player, double spell);

  [[nodiscard]] std::size_t playerCount() const { return players.size(); }
  /** nullptr for players without any tracked action. */
  [[nodiscard]] const TrackedPlayer* player(std::size_t index) const;
  /** Completed passes from one stats index to another. */
  [[nodiscard]] std::uint16_t passes(std::size_t from, std::size_t to) const;
  [[nodiscard]] const std::vector<TrackedShot>& shots() const
  {
    return shot_kinds;
  }
  /** Final-third touches per bucket, index 0 home. */
  [[nodiscard]] const std::array<
      std::array<std::uint16_t, MatchTracking::BUCKETS>, 2>&
  finalThirdTouches() const
  {
    return final_third;
  }

 private:
  TrackedPlayer* slot(std::size_t index);

  bool enabled = true;
  std::vector<TrackedPlayer> players;
  /** Dense MAX_PLAYERS x MAX_PLAYERS matrix, allocated on the first pass. */
  std::vector<std::uint16_t> pass_matrix;
  std::vector<TrackedShot> shot_kinds;
  std::array<std::array<std::uint16_t, MatchTracking::BUCKETS>, 2>
      final_third{};
  /** Where the last pass left, as fractions of the pitch (x of the length,
   * y of the width). */
  Vector2F pass_origin{.x = 0.5F, .y = 0.5F};
  bool pass_open_play = false;
};
