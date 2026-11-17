// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "global/types.h"
#include "tools/match_sandbox_detail.h"
#include "tools/match_sandbox_recorder.h"

class IMatchRenderer;
class MatchEngine;

/**
 * The match sandbox's inspector (debugger phase 3): what the planner, a
 * player and the referee had decided at the moment under review, and
 * whole-match summaries, plus the overlays drawn on the review pitch.
 *
 * Every view reads the inspected engine (the rewound moment, or the live
 * match) and the recorder's log up to that engine's tick, so the tabs rewind
 * with the timeline. One selected player links the pitch, the Player tab
 * and the log.
 */
class DebugInspector
{
 public:
  /** The player inspected and highlighted (0: none). */
  PlayerID selected = 0;

  void renderTeam(const SandboxRecorder& recorder, const MatchEngine& engine);
  void renderPlayer(const SandboxRecorder& recorder, const MatchEngine& engine);
  void renderReferee(const SandboxRecorder& recorder,
                     const MatchEngine& engine);
  void renderSummary(const SandboxRecorder& recorder,
                     const MatchEngine& engine);
  /** On-ball decisions with every score broken down (full detail). */
  void renderDecision(const SandboxRecorder& recorder,
                      const MatchEngine& engine);

  /** Overlay switches, drawn above the review pitch. */
  void renderOverlayToggles();
  /**
   * Draws the overlays over a pitch just drawn by `renderer`, and selects
   * the player nearest a click inside it (`clicked`).
   */
  void drawOverlays(const SandboxRecorder& recorder, const MatchEngine& engine,
                    const IMatchRenderer& renderer, bool clicked);

  /** A new match. */
  void reset();

 private:
  static constexpr std::size_t JOBS = SandboxRecorder::JOB_COUNT;
  static constexpr std::uint64_t NONE =
      std::numeric_limits<std::uint64_t>::max();

  /** The planner's state as of a tick, rebuilt from the log. */
  struct AsOf
  {
    std::uint64_t tick = NONE;
    /** Log entries up to and including the tick. */
    std::size_t prefix = 0;
    std::array<std::array<PlayerID, JOBS>, 2> jobs{};
    std::array<std::array<float, JOBS>, 2> scores{};
    /** Tick the side's current phase began. */
    std::array<std::uint64_t, 2> phaseSince{};
    /** Log index of the latest decision, if any. */
    std::optional<std::size_t> lastDecision;
  };

  /** Per-player totals of the Summary tab. */
  struct PlayerTotals
  {
    PlayerID player = 0;
    bool homeTeam = true;
    int decisions = 0;
    std::array<int, 6> chosen{};
    int noiseFlips = 0;
    int actions = 0;
    int intentChanges = 0;
    /** Ticks spent in each intent. */
    std::array<std::uint64_t, 14> intentTicks{};
  };

  /** Brings `asOf` to the tick; hold the recorder's lock. */
  void refresh(const SandboxRecorder& recorder, std::uint64_t tick);
  [[nodiscard]] SandboxRecorder::Job jobOf(PlayerID player,
                                           bool homeTeam) const;
  void selectable(const SandboxRecorder& recorder, PlayerID player);
  void renderIntentStrip(const SandboxRecorder& recorder, PlayerID player,
                         std::uint64_t cursor, std::uint64_t end);
  void refreshSummary(const SandboxRecorder& recorder);
  /** Full detail up to the engine's tick; a line about its stretch. */
  void ensureDetail(const SandboxRecorder& recorder, const MatchEngine& engine);
  void renderRankingDetail(const SandboxRecorder& recorder, bool home,
                           std::uint64_t tick);
  void renderDuelDetail(const SandboxRecorder& recorder, std::uint64_t tick);
  static void renderDecisionTree(
      const SandboxRecorder& recorder,
      const DetailRecorder::At<MatchDecisionDetail>& at);

  AsOf asOf;
  DetailSession detail_session;
  /** The decision opened in the Decision tab: its tick and player. */
  std::optional<std::pair<std::uint64_t, PlayerID>> opened_decision;
  bool decisions_of_selected = false;
  /** Result of the last "Export as test scenario". */
  std::string export_status;
  int team_side = 0;
  bool show_targets = true;
  bool show_jobs = true;
  bool show_pass_options = true;
  bool selected_only = false;
  /** Player tab: inspect the ball carrier until a player is picked. */
  bool follow_ball = true;
  PlayerID followed = 0;
  PlayerID last_selected = 0;

  // Summary, cached for one prefix of the log.
  std::size_t summary_prefix = std::numeric_limits<std::size_t>::max();
  std::vector<PlayerTotals> totals;
  std::array<std::array<std::uint64_t, 7>, 2> phaseTicks{};
};
