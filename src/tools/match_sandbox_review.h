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
#include <memory>
#include <optional>
#include <vector>

#include "global/types.h"
#include "gui/render/match_render_snapshot.h"
#include "model/match_engine.h"

struct ImDrawList;

class DebugInspector;
class IMatchRenderer;
class SandboxRecorder;

/**
 * The match sandbox's embedded review (debugger phase 2): a timeline of the
 * whole recorded match (both sides' phases, goals, shots, cards and
 * substitutions) that can be scrubbed, stepped and played back, with the
 * rebuilt moment on a pitch of its own. Live, it leaves the live match
 * alone until the timeline is used, then pauses it; after full time it
 * reviews the finished match.
 */
class MatchReview
{
 public:
  MatchReview();
  ~MatchReview();
  MatchReview(const MatchReview&) = delete;
  MatchReview& operator=(const MatchReview&) = delete;

  /**
   * Timeline and playback controls to embed (the sandbox match's side
   * panel, where the match's own pitch shows the moment): where it is, the
   * timeline, the transport and the inspector's overlay switches.
   */
  void renderPanel(const SandboxRecorder& recorder, const MatchEngine* live,
                   bool* livePaused, DebugInspector* inspector);

  /** Embedded pitch; overlay controls can be placed in a separate card. */
  void renderPitch(const SandboxRecorder& recorder, const MatchEngine* live,
                   const std::array<TeamID, 2>& teams,
                   DebugInspector* inspector, bool overlayControls = true);

  /** Hide players off the pitch (a drill's players not in it). */
  bool hideOffPitch = false;

  /** The rewound match shown, or null while following the live one. */
  [[nodiscard]] const MatchEngine* shownEngine() const { return shown.get(); }

  /** Shows the match at the end of `tick` (rebuilt from the recording). */
  void seek(const SandboxRecorder& recorder, std::uint64_t tick);

  /** The tick shown, or empty while following the live match. */
  [[nodiscard]] std::optional<std::uint64_t> cursor() const;

  /** A new match: forgets the shown moment and the timeline. */
  void reset();

 private:
  struct Marker
  {
    std::uint64_t tick = 0;
    MatchEventType type = MatchEventType::INFO;
    bool homeTeam = true;
    std::size_t event = 0;
  };
  struct PhaseSpan
  {
    std::uint64_t tick = 0;
    TeamPhase phase = TeamPhase::STOPPAGE;
  };

  void refreshTimeline(const SandboxRecorder& recorder);
  /** The timeline across the card: ticks to x and back. */
  struct TimelineFrame
  {
    float left = 0.0F;
    float width = 1.0F;
    float span = 1.0F;
    std::uint64_t end = 0;

    [[nodiscard]] float xOf(std::uint64_t tick) const;
    [[nodiscard]] std::uint64_t tickAt(float x) const;
  };
  /** Where event markers go: home above the middle, away below. */
  struct MarkerLane
  {
    float top = 0.0F;
    float middle = 0.0F;
    float bottom = 0.0F;
  };

  void timelineTooltip(const SandboxRecorder& recorder, const Marker* nearest,
                       std::uint64_t tick) const;
  static void drawPhaseBands(ImDrawList* drawList, const TimelineFrame& frame,
                             const std::vector<PhaseSpan>& spans, float top);
  const Marker* drawMarkers(ImDrawList* drawList, const TimelineFrame& frame,
                            const MarkerLane& lane, bool hovered) const;
  void scrubTimeline(const SandboxRecorder& recorder, std::uint64_t tick,
                     bool* livePaused);
  void renderTimeline(const SandboxRecorder& recorder, std::uint64_t end,
                      bool* livePaused);
  void renderTransport(const SandboxRecorder& recorder, std::uint64_t end,
                       bool* livePaused);

  void play(const SandboxRecorder& recorder, std::uint64_t end);
  void step(const SandboxRecorder& recorder, std::int64_t ticks,
            std::uint64_t end);

  std::unique_ptr<MatchEngine> shown;
  std::optional<std::uint64_t> divergedAt;
  std::unique_ptr<IMatchRenderer> renderer;
  MatchRenderSnapshot snapshot;

  bool playing = false;
  float playSpeed = 1.0F;
  float playAccumulator = 0.0F;

  // Timeline, built incrementally from the recorder's log.
  std::size_t timelineEntries = 0;
  std::vector<PhaseSpan> homePhases;
  std::vector<PhaseSpan> awayPhases;
  std::vector<Marker> markers;
};
