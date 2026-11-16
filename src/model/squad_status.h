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
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

#include "global/types.h"

class DatabaseConnection;
enum class SquadRole : std::uint8_t;

/**
 * @enum SquadStatus
 * @brief Place in the squad the manager gives a player (values persisted).
 *
 * A status sets the player's playing-time expectation: it replaces the
 * ability-rank role (SquadRole) for morale, requests and contract talks.
 */
enum class SquadStatus : std::uint8_t
{
  Star,      /*!< The team is built around him. */
  Important, /*!< Starts the big matches. */
  Regular,   /*!< Regular starter. */
  Rotation,  /*!< Shares the minutes of his position. */
  Backup,    /*!< Covers injuries and suspensions. */
  Prospect,  /*!< Young player developing; minutes are a bonus. */
  COUNT
};

inline constexpr std::size_t SQUAD_STATUS_COUNT =
    static_cast<std::size_t>(std::to_underlying(SquadStatus::COUNT));

namespace SquadStatusModel
{
/** Age up to which a player may be given (or suggested) Prospect status. */
inline constexpr int PROSPECT_MAX_AGE = 21;

/** Language key naming @p status (e.g. "SQUAD_STATUS_STAR"). */
const char* nameKey(SquadStatus status);

/** One-line playing-time expectation of @p status (language key). */
const char* expectationKey(SquadStatus status);

/** Playing-time expectation of a status. */
SquadRole toSquadRole(SquadStatus status);

/**
 * Status whose minutes a player expects once the manager gave him
 * @p assigned: the assigned one, but never more than one level below what
 * his ability rank earns in his eyes (@p deserved). A promotion raises the
 * expectation; a demotion only lowers it a little and is resented on its own
 * (moraleOffset()).
 */
SquadStatus expectation(SquadStatus assigned, SquadStatus deserved);

/**
 * Status a player's ability rank in his squad earns (0 = best), as the
 * player himself sees it: top 3 Star, then Important (4-6), Regular (7-11),
 * Rotation (12-16); beyond, young players are prospects and the rest
 * backups.
 */
SquadStatus deserved(std::size_t rank, int age);

/**
 * Weekly morale target offset (negative) when the manager gives a player a
 * status clearly below the one he believes he deserves: nothing within one
 * level, then -4 per extra level, scaled by @p ambition (clamped to
 * 0.75-1.5, so even modest players resent a clear demotion). A status above
 * his standing costs nothing here but raises the minutes he expects.
 */
float moraleOffset(SquadStatus assigned, SquadStatus deserved, float ambition);
}  // namespace SquadStatusModel

/**
 * @class SquadStatusBook
 * @brief Statuses the manager gave players of his club.
 *
 * An entry only counts while the player is still at the club that set it,
 * so transfers and a change of club reset statuses without bookkeeping.
 */
class SquadStatusBook
{
 public:
  /** Sets (or with nullopt clears) the status of a player of @p team_id. */
  void set(PlayerID player_id, TeamID team_id,
           std::optional<SquadStatus> status);

  /** Status of a player who still plays for @p current_team_id. */
  std::optional<SquadStatus> get(PlayerID player_id,
                                 TeamID current_team_id) const;

  /** Number of stored entries (stale ones included until pruned). */
  std::size_t size() const { return entries.size(); }

  /** Drops entries whose player no longer plays for the club that set them. */
  template <typename TeamOf>
  void prune(TeamOf&& team_of)
  {
    std::erase_if(entries, [this, &team_of](const auto& entry)
                  { return team_of(entry.first) != entry.second.team_id; });
  }

  /** Replaces the book with the SquadStatuses table. */
  void load(const DatabaseConnection& db_conn);

  /** Rewrites the SquadStatuses table (inside the caller's transaction). */
  void save(const DatabaseConnection& db_conn) const;

 private:
  struct Entry
  {
    TeamID team_id = 0;
    SquadStatus status = SquadStatus::Regular;
  };
  std::unordered_map<PlayerID, Entry> entries;
};
