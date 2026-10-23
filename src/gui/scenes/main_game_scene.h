// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <future>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "global/background_task.h"
#include "gui/gui_scene.h"
#include "gui/gui_view.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/onboarding_overlay.h"
#include "gui/scenes/season_review_dialog.h"
#include "gui/scenes/welcome_tour.h"
#include "gui/view_models/competition_view.h"
#include "gui/view_models/player_view.h"
#include "model/finances.h"

/**
 * @brief Club hub: the base scene of a career.
 *
 * Hosts the Home dashboard and the Finances page, and owns the career clock
 * (Continue / Play Match). Other management screens are opened above it and
 * drive the clock through requestContinue().
 */
class MainGameScene : public ManagementScene
{
 public:
  /** @brief Pages hosted directly by the club hub. */
  enum class Page
  {
    OVERVIEW,
    FINANCES
  };

  /**
   * @brief Constructs a new MainGameScene.
   * @param guiView_ptr Pointer to the GUIView.
   */
  explicit MainGameScene(GUIView* guiView_ptr);

  /**
   * @brief Destroys the MainGameScene. A holiday or off-season run still
   * simulating stops at the end of its current day; a Continue to the next
   * fixture finishes its days first.
   */
  ~MainGameScene() override;

  /**
   * @brief Called when entering the scene.
   */
  void onEnter() override;

  /**
   * @brief Updates scene logic.
   * @param deltaTime Time elapsed since last update.
   */
  void update(float deltaTime) override;

  /**
   * @brief Gets the ID of this scene.
   * @return The SceneID (GAME_MENU).
   */
  [[nodiscard]] SceneID getID() const override;

  /** @brief True while days are being simulated in the background. */
  [[nodiscard]] bool isAdvancing() const { return continuation_running; }

  /**
   * @brief True from the Continue request until the days are simulated,
   * including the frames spent closing the screens above the hub first.
   */
  [[nodiscard]] bool isContinuing() const
  {
    return continuation_running || continuation_requested;
  }

  /** @brief Label of the context-aware Continue / Play Match button. */
  [[nodiscard]] std::string continueLabel() const;

  /**
   * @brief Plays today's match, or returns to the hub and simulates days up
   * to the next managed fixture.
   */
  void requestContinue();

  /**
   * @brief Returns to the hub and simulates days with the assistant in
   * charge until the plan ends; the report opens when it is done.
   */
  void requestHoliday(const HolidayPlan& plan);

  /**
   * @brief After a managed match has been recorded: returns to the hub and
   * simulates the rest of the day on the Continue worker, then opens the
   * match's report.
   */
  void requestPostMatchAdvance(const GameDateValue& date, TeamID home_id,
                               TeamID away_id);

  /**
   * @brief Draws the Continue progress card over a dimmed snapshot of the
   * screen. Reads only the controller's thread-safe progress, never game
   * state, because days are being simulated on a worker thread.
   */
  void renderContinueOverlay();

  /** @brief Opacity of the dimming that fades out after Continue ends. */
  [[nodiscard]] float continueFadeOut() const;

  /**
   * @brief Rebuilds the cached career data when the date moved on or the
   * cached next fixture has been played elsewhere (e.g. a quick result).
   */
  void refreshIfStale();

  /** @brief Switches the page shown by the hub. */
  void showPage(Page page) { active_page = page; }

  /**
   * @brief Shows the welcome tour over Home next frame (a career just
   * started from the club choice, or a replay asked for on Help).
   */
  void offerWelcome() { welcome_requested = true; }

  /**
   * @brief Asks the running Continue or holiday to stop at the end of the
   * day being simulated (the Stop button of the progress card).
   */
  void requestStop();

  /** @brief True while the end-of-season summary is open. */
  [[nodiscard]] bool isShowingSeasonReview() const
  {
    return season_review_dialog.isOpen();
  }

  /** @brief The welcome tour (open while it is shown). */
  [[nodiscard]] const WelcomeTour& welcomeTour() const { return welcome_tour; }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override;
  void refresh() override { refreshData(); }

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  void renderOverview();
  void renderFinances();
  void renderNextMatchCard(float width, float height);
  void renderStandingsCard(float width, float height);
  void renderRecentResultsCard(float width, float height);
  void renderKeyPlayersCard(float width, float height);
  void startContinuation();
  void autoFixLineup();
  /** The next fixture's selection has unavailable players the assistant
   * could still replace (players nobody fit can replace play through it). */
  [[nodiscard]] bool lineupBlocked() const
  {
    return !cached_unavailable.empty() && !cached_can_kick_off;
  }

  void refreshData();

  Page active_page = Page::OVERVIEW;
  WelcomeTour welcome_tour;
  bool welcome_requested = false;
  /** Opens the season summary once nothing else is in the way. */
  void showPendingSeasonReview();
  void renderSeasonReview();
  SeasonReviewDialog season_review_dialog;
  std::vector<std::pair<int, int>> cached_standings;
  std::vector<std::reference_wrapper<const Player>> cached_top_players;
  int cached_season = -1;
  GameDateValue cached_date;
  TeamID cached_club = 0; /**< Club the cached page shows (0: none). */
  std::jthread continue_worker;
  std::future<int> continue_operation;
  bool continuation_running = false;
  bool continuation_requested = false;
  std::optional<HolidayPlan> pending_holiday;
  bool holiday_running = false;
  /** Runs that closing the game interrupts (holiday, off-season, out of
   * work); a Continue to the next fixture completes its days. */
  bool stop_on_close = false;
  bool stop_requested = false;
  /** Controller of the running worker (stopped from the destructor). */
  GameController* continue_controller = nullptr;
  /** Managed match whose report opens once the rest of its day is done. */
  struct PlayedMatch
  {
    GameDateValue date;
    TeamID home_id = 0;
    TeamID away_id = 0;
  };
  std::optional<PlayedMatch> match_report;

  // Continue overlay state (progress snapshots only, see update()).
  void trackContinueProgress();
  GameDateValue continue_start_date;
  GameController::ContinueProgress continue_progress;
  int continue_logged_day = 0;
  uint32_t continue_day_matches = 0;
  std::vector<std::string> continue_log;
  double continue_started_at = 0.0;
  double continue_finished_at = -1.0;
  bool continue_overlay_shown = false;

  // Dashboard view models, rebuilt by refreshData().
  std::vector<CompetitionView::StandingRow> cached_table;
  CompetitionView::Zones cached_zones;
  std::unordered_set<PlayerID> cached_unavailable;
  size_t cached_starters = 0;
  size_t cached_unavailable_starters = 0;
  /** Auto-fix would change nothing (see GameController::canKickOff). */
  bool cached_can_kick_off = true;
  std::vector<CompetitionView::FixtureRow> cached_recent;
  std::optional<CompetitionView::FixtureRow> cached_next;
  std::vector<PlayerView::PlayerRow> cached_squad;
  int64_t cached_payroll = 0;
  int64_t cached_squad_value = 0;
  /** Transfer money available today and the board's embargo. */
  int64_t cached_transfer_budget = 0;
  bool cached_embargo = false;
  float cached_average_overall = 0.0f;

  // Finance page view models.
  struct FinanceBar
  {
    const char* label_key; /**< Localised at render time. */
    int64_t amount;
    std::string amount_text;
  };
  struct LedgerRow
  {
    std::string date;
    const char* category_key; /**< Localised at render time. */
    int64_t amount;
    std::string amount_text;
  };
  FinanceSummary cached_season_summary;
  std::vector<FinanceBar> cached_finance_bars;
  std::vector<FinanceBar> cached_month_bars;
  bool finance_show_month = false;
  std::vector<LedgerRow> cached_ledger; /**< Newest first. */
  std::vector<float> cached_balance_trend;
  float budget_shift = 0.0f;

  /** Checklist and next steps card on Home. */
  NextStepsCard next_steps;
};
