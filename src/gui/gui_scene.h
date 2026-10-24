// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once
#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <optional>

#include "gui/gui_view.h"
#include "gui/nav_history.h"

/**
 * @brief Identifiers for different GUI scenes.
 */
// TODO add scene types
enum class SceneID : uint8_t
{
  MAIN_MENU,       /**< Main menu scene */
  SETTINGS,        /**< Settings scene */
  GAME_MENU,       /**< In-game menu scene */
  LINEUP,          /**< Lineup management scene */
  TEAM_SELECTION,  /**< Team selection scene */
  ROSTER,          /**< Team roster scene */
  STRATEGY,        /**< Strategy management scene */
  TRANSFER_MARKET, /**< Transfer market scene */
  MATCH,           /**< Live match scene */
  PLAYER_PROFILE,  /**< Single player profile */
  FIXTURES,        /**< Fixtures and results */
  STANDINGS,       /**< League table */
  INBOX,           /**< Club inbox */
  CLUB,            /**< Club, board and history */
  MATCH_REPORT,    /**< Post-match report */
  SCOUTING,        /**< Scouting and recruitment */
  TRAINING,        /**< Training schedule and workload */
  STAFF,           /**< Backroom staff and staff market */
  MANAGER,         /**< Manager profile, career and job centre */
  YOUTH,           /**< Youth academy and intake */
  MEDICAL,         /**< Medical centre: injuries, risk, medical staff */
  CALENDAR,        /**< Season calendar and agenda */
  SQUAD_PLANNER,   /**< Depth chart and squad planning */
  PLAYER_COMPARE,  /**< Side-by-side player comparison */
  DELEGATION,      /**< Who handles which duty */
  DATA_HUB,        /**< Team and player analytics */
  OPPOSITION,      /**< Pre-match opposition report */
  INTERNATIONAL,   /**< Continental competitions and national teams */
  AWARDS,          /**< League honours */
  RECORDS,         /**< Records book and hall of fame */
  PLANNING,        /**< Pre-season, facility projects and mentoring */
  HELP,            /**< Getting started, shortcuts and glossary */
  RESERVES,        /**< U21 squad and its league */
  CALL_UPS,        /**< National-team call-ups of the head coach */
  ABOUT,           /**< Version, build, licence and third-party credits */
  NEWS,            /**< World news feed */
  TIMELINE,        /**< Manager's career timeline and journal */
};

/**
 * @brief Base class for all GUI scenes.
 */
class GUIScene
{
 public:
  /**
   * @brief Constructs a new GUIScene.
   * @param guiView_ptr Pointer to the main GUIView.
   */
  explicit GUIScene(GUIView* guiView_ptr);

  /**
   * @brief Destroys the GUIScene.
   */
  virtual ~GUIScene() = default;

  // Core scene interface - must be implemented by derived classes

  /**
   * @brief Handles SDL events for the scene.
   * @param event The SDL event to process.
   */
  virtual void handleEvent(const SDL_Event& event);

  /**
   * @brief Updates scene logic.
   * @param deltaTime The time elapsed since the last update.
   */
  virtual void update(float deltaTime) = 0;

  /**
   * @brief Renders the scene.
   */
  virtual void render() = 0;

  // Optional lifecycle hooks

  /**
   * @brief Called when the scene is entered/becomes active.
   */
  virtual void onEnter();

  /**
   * @brief Called when the scene is exited/becomes inactive.
   */
  virtual void onExit();

  /**
   * @brief Called when an overlay above this scene closes and it becomes the
   * active scene again. Screens use it to refresh cached view models after a
   * match, a transfer or any other state change made elsewhere.
   */
  virtual void onResume();

  /**
   * @brief Called when the window is resized.
   * @param width The new window width.
   * @param height The new window height.
   */
  virtual void onResize(int width, int height);

  /**
   * @brief Gets the ID of the scene.
   * @return The SceneID of this scene.
   *
   * GUIView uses this identifier to apply match-specific event and scene
   * behavior.
   */
  virtual SceneID getID() const = 0;

  /**
   * @brief How to open this screen again from the navigation history, or
   * nothing for scenes outside it (menus, team selection, the live match).
   */
  [[nodiscard]] virtual std::optional<NavEntry> historyEntry() const
  {
    return std::nullopt;
  }

 protected:
  // Helper methods for derived classes

  /**
   * @brief Gets the SDL renderer.
   * @return The SDL_Renderer pointer.
   */
  SDL_Renderer* getRenderer() const;

  /**
   * @brief Gets the SDL window.
   * @return The SDL_Window pointer.
   */
  SDL_Window* getWindow() const;

  /**
   * @brief Gets the associated GUIView.
   * @return The GUIView pointer.
   */
  GUIView* getGuiView() const { return guiView; }

  /**
   * @brief Requests a scene change.
   * @param newScene The new scene to transition to.
   */
  void changeScene(std::unique_ptr<GUIScene> newScene);

  /**
   * @brief Requests the application to quit.
   */
  void quit();

  GUIView* guiView;
};
