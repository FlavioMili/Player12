// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "global/types.h"
#include "model/gamedate.h"

/**
 * @brief Screens of the management shell. The sidebar groups them into a
 * few hubs (Home, Inbox, Squad, Training, Matches, Recruitment, Club); the
 * current hub lists its screens under its label (a flyout, plus tabs above
 * the page, when the sidebar is collapsed to icons).
 */
enum class NavSection : uint8_t
{
  HOME,
  INBOX,
  CLUB,
  SQUAD,
  LINEUP,
  TACTICS,
  FIXTURES,
  STANDINGS,
  TRANSFERS,
  FINANCES,
  SCOUTING,
  TRAINING,
  STAFF,
  YOUTH,
  MANAGER,
  MEDICAL,
  CALENDAR,
  SQUAD_PLANNER,
  COMPARE,
  DELEGATION,
  DATA_HUB,
  OPPOSITION,
  INTERNATIONAL,
  AWARDS,
  RECORDS,
  PLANNING,
  RESERVES,
  CALL_UPS,
  NEWS,
  TIMELINE,
  NONE
};

/**
 * @brief A screen of the career as the recipe to open it again (which
 * section, player, club or fixture), never the screen object itself.
 */
struct NavEntry
{
  enum class Kind : uint8_t
  {
    SECTION,      /*!< A sidebar screen. */
    PLAYER,       /*!< A player's profile. */
    CLUB,         /*!< Another club's squad list. */
    MATCH_REPORT, /*!< The report of a played fixture. */
    COMPARE       /*!< The comparison, with the players it opened with. */
  };

  Kind kind = Kind::SECTION;
  NavSection section = NavSection::HOME; /*!< SECTION only. */
  PlayerID player = 0;        /*!< PLAYER, or the first compared player. */
  PlayerID second_player = 0; /*!< COMPARE: the second compared player. */
  TeamID team = 0;            /*!< CLUB, or the home side of a report. */
  TeamID away_team = 0;       /*!< MATCH_REPORT: the away side. */
  GameDateValue date{};       /*!< MATCH_REPORT: the day it was played. */

  static NavEntry ofSection(NavSection section);
  static NavEntry ofPlayer(PlayerID player);
  static NavEntry ofClub(TeamID team);
  static NavEntry ofMatchReport(GameDateValue date, TeamID home, TeamID away);
  static NavEntry ofCompare(PlayerID first, PlayerID second);

  /**
   * @brief True for screens opened on top of another one (a profile, a
   * report, a comparison of chosen players) rather than from the sidebar.
   */
  [[nodiscard]] bool isDetail() const;

  bool operator==(const NavEntry& other) const = default;
};

/**
 * @brief Browser-style history of the screens visited in a career.
 *
 * visit() records a newly shown screen and drops everything ahead of the
 * current one, like following a link after going back in a browser. back()
 * and forward() move through the list, skipping entries the caller no longer
 * accepts (a player who left the game, a screen closed while out of work)
 * and entries identical to the current one. The oldest entry goes once the
 * capacity is reached. No allocation after construction.
 */
class NavHistory
{
 public:
  static constexpr std::size_t DEFAULT_CAPACITY = 50;

  explicit NavHistory(std::size_t capacity = DEFAULT_CAPACITY);

  /** @brief Records a newly shown screen (repeating the current: ignored). */
  void visit(const NavEntry& entry);

  /** @brief Forgets every entry (a career was started or loaded). */
  void clear();

  /**
   * @brief Steps back onto @p entry, a screen shown beneath the current one
   * that the history cannot step back to (never recorded, dropped by the
   * capacity, or rejected as invalid): the nearest earlier copy becomes
   * current, else @p entry is inserted before the current one. The current
   * entry stays ahead, for Forward.
   */
  void stepBackTo(const NavEntry& entry);

  /** @brief Screen shown now, if any was recorded. */
  [[nodiscard]] std::optional<NavEntry> current() const;

  /** @brief Previous screen @p valid accepts, without moving. */
  template <typename Valid>
  [[nodiscard]] std::optional<NavEntry> peekBack(const Valid& valid) const
  {
    const std::optional<std::size_t> index = findBack(valid);
    return index ? std::optional<NavEntry>(entries[*index]) : std::nullopt;
  }

  /** @brief Next screen @p valid accepts, without moving. */
  template <typename Valid>
  [[nodiscard]] std::optional<NavEntry> peekForward(const Valid& valid) const
  {
    const std::optional<std::size_t> index = findForward(valid);
    return index ? std::optional<NavEntry>(entries[*index]) : std::nullopt;
  }

  /** @brief Steps back to the previous screen @p valid accepts. */
  template <typename Valid>
  std::optional<NavEntry> back(const Valid& valid)
  {
    const std::optional<std::size_t> index = findBack(valid);
    if (!index.has_value()) return std::nullopt;
    cursor = *index;
    return entries[cursor];
  }

  /** @brief Steps forward to the next screen @p valid accepts. */
  template <typename Valid>
  std::optional<NavEntry> forward(const Valid& valid)
  {
    const std::optional<std::size_t> index = findForward(valid);
    if (!index.has_value()) return std::nullopt;
    cursor = *index;
    return entries[cursor];
  }

  [[nodiscard]] std::size_t size() const { return entries.size(); }
  [[nodiscard]] std::size_t capacity() const { return limit; }
  /** @brief Index of the current entry (0 when empty). */
  [[nodiscard]] std::size_t position() const { return cursor; }

 private:
  template <typename Valid>
  [[nodiscard]] std::optional<std::size_t> findBack(const Valid& valid) const
  {
    for (std::size_t index = cursor; index-- > 0;)
      if (entries[index] != entries[cursor] && valid(entries[index]))
        return index;
    return std::nullopt;
  }

  template <typename Valid>
  [[nodiscard]] std::optional<std::size_t> findForward(const Valid& valid) const
  {
    for (std::size_t index = cursor + 1; index < entries.size(); ++index)
      if (entries[index] != entries[cursor] && valid(entries[index]))
        return index;
    return std::nullopt;
  }

  std::vector<NavEntry> entries;
  std::size_t cursor = 0;
  std::size_t limit;
};
