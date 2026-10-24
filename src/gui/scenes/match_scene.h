// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "audio/match_audio.h"
#include "global/background_task.h"
#include "gui/gui_scene.h"
#include "gui/render/imatch_renderer.h"
#include "gui/render/match_play_overlay.h"
#include "gui/scenes/match_analysis_panel.h"
#include "gui/scenes/match_play_controller.h"
#include "gui/scenes/match_scene_tuning.h"
#include "gui/scenes/match_subs_panel.h"
#include "gui/scenes/match_tactics_panel.h"
#include "gui/scenes/match_touchline.h"
#include "gui/scenes/team_talk_dialog.h"
#include "model/match_engine.h"

/** Which renderer presents the live match. */
enum class MatchViewMode : std::uint8_t
{
  PITCH_2D,
  BROADCAST_3D,
};

/**
 * Live match of the managed club (or any two clubs for tooling).
 *
 * Before kick-off the managed selection is validated for the fixture: an
 * injured or suspended player blocks the match until the lineup is fixed
 * (auto-fix or the lineup screen). The engine then starts with every
 * player's persistent condition, the AI substitutes only for the opponent
 * unless the manager lets the assistant handle changes, and the finished
 * match is recorded with its full engine report and consequences.
 *
 * Play mode: the manager can play his club's match himself (Play, or Take
 * control while watching), driving one footballer at a time on the same
 * simulation (see MatchPlayController), and hand it back to the AI at a
 * stoppage or from the pause menu.
 */
class MatchScene : public GUIScene
{
 public:
  MatchScene(class GUIView* guiView_ptr, uint16_t home_id, uint16_t away_id);

  void onEnter() override;
  void handleEvent(const SDL_Event& event) override;
  void update(float deltaTime) override;
  void render() override;
  void onExit() override;
  SceneID getID() const override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;
  friend class GameFlowTest_ManagedMatchIntegration_Test;
  friend class GameFlowTest_ManagementScreensMidSeason_Test;
  friend class GameFlowTest_WatchedMatchSeedIsDeterministic_Test;
  friend class GameFlowTest_WatchedCupTieIsPlayedToAWinner_Test;
  friend class MatchRenderer3DSceneTest_SwitchesViewsAndCapturesFrames_Test;

  /** A selected player who may not take part in today's fixture. */
  struct LineupProblem
  {
    PlayerID id{};
    std::string name;
    std::string reason;
    /** The assistant's pick for the slot (empty: none or bench only). */
    std::string replacement;
  };

  // What the assistant may decide for the manager; kept for the session.
  /** Substitutions for the managed side (off: only the manager's). The
   * lineup assistant lives in the game (GameController). */
  inline static bool assistant_substitutions = false;

  uint16_t home_team_id;
  uint16_t away_team_id;

  std::string home_name;
  std::string away_name;

  /** Side of the managed club, if it plays in this match. */
  std::optional<bool> managed_is_home;
  /** Type of today's calendar fixture between the teams, if there is one. */
  std::optional<MatchType> fixture_type;
  /** Weekday, date and kick-off of that fixture ("Sun 17 Aug 20:45"). */
  std::string fixture_when;
  std::vector<LineupProblem> lineup_problems;
  std::string lineup_status;
  /** What the assistant changed before kick-off, shown in the HUD. */
  std::string pre_match_note;

  std::unique_ptr<MatchEngine> engine;
  /** Quick result running on a worker thread (declared after the engine
   * so it is joined before the engine is destroyed). */
  std::jthread quick_result_worker;
  std::future<void> quick_result;
  /** Crowd, whistle and ball sounds of the live match. */
  std::unique_ptr<MatchAudio> audio;
  /** Seconds the "skipping to the next highlight" note stays visible. */
  float skip_indicator_seconds = 0.0f;
  std::unique_ptr<IMatchRenderer> renderer_2d;
  std::unique_ptr<IMatchRenderer> renderer_3d;
  MatchViewMode view_mode = MatchViewMode::PITCH_2D;
  MatchCameraMode camera_mode = MatchCameraMode::BROADCAST;
  bool show_player_names = false;
  /** Kick-off of today's fixture (minutes after midnight, -1 unknown). */
  int kickoff_minutes = -1;
  /** Daylight 3D presentation (default for day kick-offs). */
  bool day_look = false;
  /** Pitch-control overlay on the 2D view. */
  bool pressure_overlay = false;
  float pending_zoom_steps = 0.0f;
  /** Mouse input over the 3D view waiting for the next rendered frame. */
  MatchCameraInput pending_camera_input;
  /** The free camera orbits the moving ball (B). */
  bool free_follow_ball = false;
  /** "Pitch focus": the view fills the window under a compact HUD (F). */
  bool pitch_focus = false;
  /** Statistics and events hidden so the view gets the whole width. */
  bool side_panels_hidden = false;
  /** Width of the focus HUD's control strip last frame (right-aligned). */
  float focus_controls_width = 0.0f;
  float frame_seconds = 0.0f;

  bool match_finished = false;
  /** "a.e.t." / "4-3 on penalties" under the full-time score (built once). */
  std::string decided_by;
  bool decided_by_ready = false;

  /** Multiplier of the engine's real-time pace (1x = real time). */
  float match_speed = 1.0f;
  bool highlights_only = false;
  bool is_paused = false;
  TeamTalkDialog team_talk;
  MatchAnalysisPanel analysis_panel;

  bool show_substitutions = false;
  /** Substitutions dialog: planned changes, swaps and the assistant. */
  MatchSubsPanel subs_panel;
  bool show_tactics = false;
  /** Tactics dialog: style, instructions and formation during play. */
  MatchTacticsPanel tactics_panel;
  /** Play was paused by opening a matchday dialog (resumes on close). */
  bool paused_for_dialog = false;
  /** The match was in a break (half-time, around extra time, shootout)
   * last frame: entering one pauses the managed club's match. */
  bool in_break = false;
  /** The last update skipped ahead: the next frame's time (which includes
   * the skip's own work) is clamped so play does not jump. */
  bool skipped_last_update = false;
#ifdef DEBUG
  bool show_ai_debug = false;
#endif

  // --- Play mode -------------------------------------------------------------
  /** Devices, buttons and the active footballer of the human's side. */
  MatchPlayController play;
  /** "Take control" asked: the paused match waits on the confirmation. */
  bool play_confirm = false;
  /** The play-mode pause menu is open. */
  bool play_menu = false;
  /** The pause control was pressed again with the menu open: it closes
   * (from inside the popup) and play resumes. */
  bool play_menu_resume = false;
  /** Matchday dialog to open once the pause menu has closed. */
  enum class PlayMenuNext : std::uint8_t
  {
    NONE,
    TACTICS,
    SUBSTITUTIONS
  };
  PlayMenuNext play_menu_next = PlayMenuNext::NONE;
  /** How the match was being watched, restored on hand-back. */
  float watch_speed = 1.0f;
  bool watch_highlights = false;
  MatchCameraMode watch_camera = MatchCameraMode::BROADCAST;
  bool watch_focus = false;
  /** ImGui keyboard navigation is off while the keys drive the pitch. */
  bool nav_keyboard_suspended = false;
  /** Damped centre of the zoomed 2D play view (normalised pitch). */
  Vector2F play_follow{MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE};
  bool play_follow_ready = false;
  /** Stick-to-pitch mapping of the view on screen (last frame). */
  PlayScreenBasis play_basis;
  /** Render snapshot refilled every frame (no per-frame allocation). */
  MatchRenderSnapshot snapshot;

  std::string substitution_status;
  /** Feed rows: indices into the engine's events (key moments by default). */
  std::vector<std::size_t> visible_events;
  std::size_t indexed_events = 0;
  bool show_all_events = false;
  bool indexed_show_all = false;
  bool substitution_refused = false;
  std::string debug_status;
  float scene_entry_milliseconds = 0.0f;
  float last_update_milliseconds = 0.0f;
  float maximum_update_milliseconds = 0.0f;
  std::uint64_t slow_update_count = 0;
  float last_render_milliseconds = 0.0f;
  float average_render_milliseconds = 0.0f;

  /** Re-checks the managed selection against today's fixture. */
  void refreshLineupProblems();
  /** Lets the assistant replace unavailable players (notes the changes). */
  void applyLineupFix();
  /** Builds the engine once the selection is valid. */
  void startMatch();
  /** AI substitutions: opponent always, managed side only via assistant. */
  void applySubstitutionPolicy();
  /** Performs a manual change now; false (with a reason) when refused. */
  bool substitute(PlayerID outgoing, PlayerID incoming);
  /** The managed side's touchline (only when the managed club plays). */
  [[nodiscard]] std::optional<TouchlineContext> touchline();
  /** Shows or hides a matchday dialog (pausing play if the user wants). */
  void showSubstitutions(bool show);
  void showTactics(bool show);
  /** Resumes play once no matchday dialog is left open. */
  void dialogClosed();
  /** The view with the shouts bar under it (managed matches). */
  void renderPitchArea(ImVec2 size);
  /** Shouts chip in the bottom-right corner of the focus view. */
  void renderFocusShouts(ImVec2 origin, ImVec2 size);
  /** Records the result with the full engine report and shows the report. */
  bool finishMatch();
  /** Plays the rest of the match instantly, then finishes it. */
  bool quickResult();
  [[nodiscard]] std::string clockText() const;
  [[nodiscard]] ImU32 teamColor(bool home) const;

  /** Pauses the managed club's match when it has just reached a break. */
  void pauseAtBreak();

  // Play mode (match_scene_play.cpp).
  [[nodiscard]] MatchPlayController::Options playOptions() const;
  /** The managed club's live match can be taken over now. */
  [[nodiscard]] bool canTakeControl() const;
  /** Pauses and asks for confirmation before control is taken. */
  void requestTakeControl();
  /** Hands the managed side's active footballer to the human. */
  void startPlaying();
  /** At a stoppage or while paused the AI can take the team back. */
  [[nodiscard]] bool canHandBack() const;
  void handBack();
  void openPlayMenu();
  void closePlayMenu();
  /** Device input and switching before the engine advances. */
  void updatePlay(float deltaTime);
  /** ImGui keyboard navigation off while the keys drive the pitch. */
  void suspendKeyboardNavigation(bool suspend);
  /** Marker, labels, power bar and radar over the view; stick mapping. */
  void renderPlayOverlay(const IMatchRenderer& renderer, ImVec2 viewMin,
                         ImVec2 viewMax);
  /** Zoomed 2D viewport following the ball and the active footballer. */
  [[nodiscard]] MatchViewport playViewport2D(const MatchViewport& fitted,
                                             ImVec2 viewMin, ImVec2 viewMax);
  /** Play / Take control / Hand back / Menu buttons (one row). */
  void renderPlayButtons();
  void renderPlayConfirm();
  void renderPlayMenu();
  /** Keyboard and gamepad layout, as text rows. */
  void renderPlayControlsHelp() const;
  void setPlaybackSpeed(float speed);
  void setHighlightsOnly(bool enabled);
  void setViewMode(MatchViewMode mode);
  void setCameraMode(MatchCameraMode mode);
  /** Enters or leaves pitch focus (full-window view with overlay HUD). */
  void setPitchFocus(bool enabled);
  void setSidePanelsHidden(bool hidden);
  /** Hands the 3D view to the free camera from the pose on screen. */
  void takeFreeCamera();
  void setFreeFollowBall(bool follow);
  /** Toggles the real window between windowed and full screen. */
  void toggleWindowFullscreen();
  /** Orbit, pan, zoom and double-click over the view (last item). */
  void handleViewInput();
  void renderPitchFocus();
  void renderFocusHud(ImVec2 origin, ImVec2 size);
  void renderLineupGate();
  void renderQuickResultProgress();
  void renderScoreboard();
  void renderTimeline();
  /** Appends new engine events to the (optionally filtered) feed. */
  void refreshVisibleEvents();
  void renderControls();
  void renderViewControls();
  void renderPitch(ImVec2 size);
  void renderStatistics(ImVec2 size);
  void renderEvents(ImVec2 size);
  void renderSubstitutionsModal();
  void renderTacticsModal();
#ifdef DEBUG
  void renderDebugLines();
  void exportDebugSnapshot();
#endif
};
