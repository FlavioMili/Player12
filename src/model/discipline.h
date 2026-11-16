// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "global/types.h"

class GameData;
struct MatchReport;

/**
 * @struct DisciplineRules
 * @brief Suspension rules of a competition (every-fifth-yellow style).
 */
struct DisciplineRules
{
  uint8_t yellow_card_threshold = 5; /*!< Every Nth yellow triggers a ban. */
  uint8_t yellow_accumulation_ban = 1;
  uint8_t second_yellow_ban = 1;
  uint8_t straight_red_ban = 1;
  uint8_t repeat_red_extra = 1; /*!< Added per earlier red this season. */
};

/** @brief Cards and outstanding ban of a player in one competition type. */
struct DisciplinaryRecord
{
  PlayerID player_id = 0;
  MatchType scope = MatchType::LEAGUE; /*!< LEAGUE or CUP. */
  uint16_t season_yellows = 0;
  uint8_t season_reds = 0;
  uint8_t ban_matches = 0; /*!< Matches still to serve in this scope. */
};

/**
 * @class Discipline
 * @brief Yellow-card accumulation and red-card suspensions.
 *
 * League and cup are separate scopes: cards collected in one are served in
 * the same competition type. Friendlies never count. Card counts reset every
 * season; outstanding bans carry over.
 */
class Discipline
{
 public:
  explicit Discipline(DisciplineRules competition_rules = {})
      : rules(competition_rules)
  {
  }

  /**
   * @brief Serves one match of every ban of the two teams, then books the
   * cards of this match (so a red card is not served by the same match).
   */
  void processMatch(const MatchReport& report, const GameData& gamedata);

  bool isSuspended(PlayerID player_id, MatchType scope) const;
  uint8_t banMatches(PlayerID player_id, MatchType scope) const;

  /** Players of a team with an outstanding ban (any scope). */
  std::vector<DisciplinaryRecord> suspendedPlayers(
      TeamID team_id, const GameData& gamedata) const;

  /** Clears the season card counts; outstanding bans are kept. */
  void resetSeason();

  std::vector<DisciplinaryRecord> records() const;
  void restore(const std::vector<DisciplinaryRecord>& stored);

 private:
  using Key = std::pair<PlayerID, MatchType>;

  void book(DisciplinaryRecord& record, unsigned yellows, unsigned reds) const;

  DisciplineRules rules;
  std::map<Key, DisciplinaryRecord> by_player;
  /** Records with ban_matches > 0, so serving bans skips everyone else. */
  std::set<Key> active_bans;
};
