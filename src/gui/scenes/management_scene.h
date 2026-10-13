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
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "global/types.h"
#include "gui/gui_scene.h"
#include "gui/nav_history.h"
#include "gui/scenes/holiday_dialog.h"
#include "gui/swipe_gesture.h"
#include "model/gamedate.h"
#include "model/next_action.h"

/**
 * @brief Navigation between management screens.
 *
 * Sections replace whatever is stacked above the club dashboard (one routine
 * screen at a time, no overlay pile-up). Detail screens such as a player
 * profile stack on top of the current section. Every screen shown is
 * recorded in the view's history (GUIView::navHistory()), which Back and
 * Forward walk like a browser's.
 */
namespace Navigation
{
/** @brief Opens a top-level section. */
void open(GUIView* view, NavSection section);

/** @brief Opens a player's profile above the current screen. */
void openPlayer(GUIView* view, PlayerID playerId);

/** @brief Opens the report of a played fixture above the current screen. */
void openMatchReport(GUIView* view, GameDateValue date, TeamID homeId,
                     TeamID awayId);

/** @brief Opens another club's squad list. */
void openClub(GUIView* view, TeamID teamId);

/** @brief Compares players side by side (0 = pick one on the screen). */
void openCompare(GUIView* view, PlayerID first, PlayerID second = 0);

/**
 * @brief Goes back to the previous screen of the history (Back button,
 * Alt+Left, mouse button 4, a swipe to the right), skipping screens that
 * can no longer be opened. With no history it closes the top screen. Does
 * nothing unless a career screen is shown (never leaves a live match, the
 * club choice or the career).
 */
void back(GUIView* view);

/** @brief Re-opens the screen Back left (Alt+Right, mouse button 5). */
void forward(GUIView* view);

/**
 * @brief Closes the top screen, back to the one beneath it or Home (Esc).
 * When that is the previous screen of the history, this is a step back.
 */
void close(GUIView* view);

/** @brief Whether back() would change the screen. */
[[nodiscard]] bool canGoBack(GUIView* view);

/** @brief Whether forward() would change the screen. */
[[nodiscard]] bool canGoForward(GUIView* view);

/**
 * @brief Whether a history entry can still be opened: its player or clubs
 * still exist and its section is available (out of work only a few are).
 */
[[nodiscard]] bool canOpen(const GUIView* view, const NavEntry& entry);
}  // namespace Navigation

/**
 * @brief Base class for every in-career screen.
 *
 * Draws the persistent shell (sidebar navigation, top bar with club, date,
 * balance and the context-aware Continue button, command palette and
 * keyboard shortcuts) and delegates the page body to renderContent().
 */
class ManagementScene : public GUIScene
{
 public:
  explicit ManagementScene(GUIView* guiViewPtr);

  void render() final;
  void onEnter() override;
  void onResume() override;
  /** Mouse side buttons and the sideways touchpad swipe (Back / Forward). */
  void handleEvent(const SDL_Event& event) override;
  /** The screen's sidebar section; detail screens override it. */
  [[nodiscard]] std::optional<NavEntry> historyEntry() const override;

 protected:
  /** @brief Draws the page body inside the content region. */
  virtual void renderContent() = 0;

  /** @brief Sidebar entry highlighted for this screen. */
  [[nodiscard]] virtual NavSection navSection() const = 0;

  /** @brief Rebuilds cached view models from the game state. */
  virtual void refresh() {}
  /** Override for new/custom data domains. Record filtering and related items
   * belong to the page; schema discovery remains inside the generic editor. */
  virtual std::optional<EditorContext> editorContext() const
  {
    return std::nullopt;
  }

  /** @brief Shows a short confirmation message in the top bar. */
  void showToast(std::string message, bool isError = false);

  /** @brief Holiday planner and return report of this screen. */
  HolidayDialog& holidayDialog() { return holiday_dialog; }

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;
  friend class GameFlowTest_ManagementScreensMidSeason_Test;

  struct PaletteEntry
  {
    enum class Kind : uint8_t
    {
      SECTION,
      CLUB,
      PLAYER,
      ACTION, /*!< A pending next step (id: index into palette_actions). */
      HELP,   /*!< Help screen (id 0) or a glossary term (id: term + 1). */
      ABOUT   /*!< About screen: version, build and credits. */
    };
    Kind kind;
    uint32_t id;
    std::string label;
    std::string label_lower;
    std::string detail;
  };

  void renderSidebar(bool collapsed);
  /** @brief Screens of the hovered hub beside the collapsed sidebar. */
  void renderSidebarFlyout();
  void renderTopBar(float height);
  void renderPalette();
  void renderMainMenuConfirm();
  void handleShortcuts();
  /** Arrow bubble at the window edge while a sideways swipe is under way. */
  void renderSwipeIndicator();
  void openPalette();
  void buildPaletteIndex();
  void filterPalette();
  void activatePaletteEntry(const PaletteEntry& entry);
  void syncClubAccent();

  std::string toast_message;
  float toast_seconds = 0.0f;
  bool toast_is_error = false;

  /** Top-bar save indicator, refreshed about once per second. */
  std::string save_label;
  float save_poll_seconds = 0.0f;
  bool save_failed = false;
  void pollSaveStatus();

  bool palette_requested = false;
  bool palette_focus_input = false;
  std::array<char, 64> palette_query{};
  std::string palette_filtered_query;
  std::vector<PaletteEntry> palette_entries;
  std::vector<size_t> palette_matches;
  std::vector<NextAction> palette_actions;
  int palette_selection = 0;

  /** Back / Forward asked for by a side button or swipe, done next frame. */
  SwipeGesture::Step pending_history_step = SwipeGesture::Step::NONE;

  bool main_menu_confirm_requested = false;
  HolidayDialog holiday_dialog;
  /** Whether the sidebar navigation needed scrolling last frame. */
  bool sidebar_nav_overflow = false;
  /** Hub whose flyout is open beside the collapsed sidebar (-1: none). */
  int flyout_hub = -1;
  ImVec2 flyout_anchor{};
  float flyout_grace = 0.0f; /*!< Seconds left once the mouse has left. */
};
