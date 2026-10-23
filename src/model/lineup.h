// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once
#include <array>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "global/global.h"
#include "global/types.h"
#include "player.h"
#include "strategy.h"

/**
 * @enum SetPieceDuty
 * @brief Leadership and dead-ball duties the manager can hand out (values
 * are persisted in the lineup).
 */
enum class SetPieceDuty : uint8_t
{
  Captain,
  ViceCaptain,
  Penalties,
  FreeKicks, /*!< Direct free kicks in shooting range. */
  CornersLeft,
  CornersRight,
  LongThrows,
  COUNT
};

inline constexpr size_t SET_PIECE_DUTY_COUNT =
    static_cast<size_t>(std::to_underlying(SetPieceDuty::COUNT));

/** @brief Player designated for each duty; 0 = chosen automatically. */
using SetPieceDesignations = std::array<PlayerID, SET_PIECE_DUTY_COUNT>;

/**
 * @brief Attribute-based suitability of players for set-piece duties.
 *
 * The weights mirror the match engine's own choice of taker (shooting for
 * penalties and direct free kicks, delivery for corners), so a manager's
 * pick and the automatic fallback are judged on the same scale.
 */
namespace SetPieces
{
/** Language key naming @p duty (e.g. "SET_PIECE_PENALTIES"). */
const char* dutyKey(SetPieceDuty duty);

/** True for the captaincy duties (chosen by standing, not attributes). */
bool isLeadership(SetPieceDuty duty);

/**
 * Suitability of @p player for a dead-ball duty on the 0-100 attribute
 * scale. Goalkeepers score 0 for every kicking or throwing duty, and so does
 * everyone for the leadership duties (the captain comes from the
 * dressing-room hierarchy).
 */
float score(SetPieceDuty duty, const Player& player);

/**
 * Best candidate for a dead-ball duty among @p candidates (ties: lower id);
 * nullptr when nobody qualifies. Leadership duties return nullptr.
 */
const Player* best(SetPieceDuty duty,
                   const std::vector<const Player*>& candidates);
}  // namespace SetPieces

/**
 * @class Lineup
 * @brief Manages the starting XI and reserves of a team.
 *
 * This class will manage the starting 11 of the team,
 * the reserves, possibly changing depending on the type
 * of competition of the next match, the strategy and
 * tactics of the team.
 */
class Lineup
{
 public:
  /**
   * @brief Constructs a Lineup object.
   */
  Lineup();

  // Goalkeeper
  /**
   * @brief Sets the goalkeeper.
   * @param gk Pointer to the Player object.
   */
  void setGoalkeeper(const Player* gk);

  /**
   * @brief Gets the goalkeeper.
   * @return Pointer to the goalkeeper Player object.
   */
  const Player* getGoalkeeper() const;

  // Outfield Players
  struct PositionedPlayer
  {
    const Player* player;
    Vector2F position;
  };

  /**
   * @brief Adds an outfield player to the pitch at the specified coordinate.
   * @param player Pointer to the Player object.
   * @param position The position on the pitch (x, y in [0.0, 1.0]).
   */
  void addOutfieldPlayer(const Player* player, Vector2F position);

  /**
   * @brief Updates the position of an existing outfield player.
   * @param playerID The ID of the player to move.
   * @param newPosition The new position on the pitch.
   * @return true if the player was found and moved, false otherwise.
   */
  bool moveOutfieldPlayer(PlayerID playerID, Vector2F newPosition);

  /**
   * @brief Removes an outfield player from the pitch.
   * @param playerID The ID of the player to remove.
   */
  void removeOutfieldPlayer(PlayerID playerID);

  /**
   * @brief Drops every reference to a player: goalkeeper, outfield slot,
   * bench and set-piece designations. Call it before the Player object is
   * destroyed, since the lineup holds raw pointers.
   * @return true if the player was referenced anywhere.
   */
  bool removePlayer(PlayerID playerID);

  /**
   * @brief Swaps a bench player with an outfield player.
   * @param benchPlayerID The ID of the substitute.
   * @param pitchPlayerID The ID of the current starting player.
   * @return true if successful, false otherwise.
   */
  bool swapPlayers(PlayerID benchPlayerID, PlayerID pitchPlayerID);

  /**
   * @brief Gets all positioned outfield players.
   * @return A vector of positioned players.
   */
  const std::vector<PositionedPlayer>& getOutfieldPlayers() const;

  // Reserves
  /** @brief Substitutes named for a match (the top-league norm). */
  static constexpr size_t MAX_SUBSTITUTES = 9;

  /**
   * @brief Sets the substitutes. Starters and duplicates are skipped; more
   * than MAX_SUBSTITUTES are cut down with chooseBench() (pass the
   * candidates best first).
   * @param subs A vector of pointers to the reserve Player objects.
   */
  void setReserves(const std::vector<const Player*>& subs);

  /**
   * @brief A matchday bench of at most MAX_SUBSTITUTES from @p candidates
   * (best first): available players before injured ones, a reserve
   * goalkeeper and one defender, midfielder and forward as cover, then the
   * rest in order.
   */
  static std::vector<const Player*> chooseBench(
      std::span<const Player* const> candidates);

  /**
   * @brief Puts @p player, who is not in the matchday squad, in place of the
   * starter or substitute @p replaced, who leaves the squad.
   * @return false when @p player is already selected or @p replaced is not.
   */
  bool bringIn(const Player* player, PlayerID replaced);

  /**
   * @brief Gets the reserve players.
   * @return A vector of pointers to the reserve Player objects.
   */
  const std::vector<const Player*>& getReserves() const;

  // Strategy
  /**
   * @brief Sets the strategy for this lineup.
   * @param strat The strategy to set.
   */
  void setStrategy(const Strategy& strat);

  /**
   * @brief Gets the strategy for this lineup.
   * @return The strategy.
   */
  const Strategy& getStrategy() const;

  // Captain and set-piece takers
  /** @brief Designated player of a duty (0 = automatic). */
  PlayerID getDesignated(SetPieceDuty duty) const;

  /** @brief Designates a player (0 = automatic) for a duty. */
  void setDesignated(SetPieceDuty duty, PlayerID playerID);

  /** @brief Every designation, indexed by SetPieceDuty. */
  const SetPieceDesignations& getDesignations() const;

  /** @brief Replaces every designation (persistence). */
  void setDesignations(const SetPieceDesignations& designations);

  /**
   * @brief A starter who was replaced because he could not play, and the
   * player standing in for him until he can.
   */
  struct StandIn
  {
    PlayerID regular = 0;
    PlayerID stand_in = 0;
    bool operator==(const StandIn&) const = default;
  };

  /** @brief Stand-ins currently filling a regular's place in the XI. */
  const std::vector<StandIn>& getStandIns() const { return stand_ins; }

  /** @brief Replaces the stand-ins (persistence, matchday fixes). */
  void setStandIns(std::vector<StandIn> values)
  {
    stand_ins = std::move(values);
  }

  /**
   * @brief The position a slot at @p position asks for: x from the own goal
   * (defence, holding midfield, midfield, attacking midfield, attack), y
   * from the left touchline (left flank, centre, right flank).
   */
  static PlayerRole roleAt(Vector2F position);

  /** @brief Goalkeeper and outfield players of the XI (no null entries). */
  std::vector<const Player*> starters() const;

  /** @brief True when the player is in the starting XI. */
  bool isStarter(PlayerID playerID) const;

  /**
   * @brief Who performs a dead-ball duty at kick-off: the designated player
   * when he starts, otherwise the best starter for it (SetPieces::best).
   * Leadership duties return the designated captain when he starts (the
   * designated vice-captain stands in for an absent one), else nullptr:
   * an automatic captain comes from the dressing-room hierarchy.
   */
  const Player* effectiveTaker(SetPieceDuty duty) const;

  // Debug / visualisation
  /**
   * @brief Converts the lineup to a string representation for debugging.
   * @return The string representation of the lineup.
   */
  std::string toString() const;

  /**
   * @brief Generates a starting XI automatically.
   * @param gamedata The game data containing player information.
   * @param allPlayerIDs A vector of all available player IDs.
   * @param stats_config The stats configuration for evaluating players.
   */
  void generateStartingXI(const class GameData& gamedata,
                          const std::vector<PlayerID>& allPlayerIDs,
                          const StatsConfig& stats_config);

  /**
   * @brief Clears the outfield players and goalkeeper.
   */
  void clear()
  {
    goalkeeper = nullptr;
    outfield_players.clear();
    stand_ins.clear();
  }

 private:
  const Player* goalkeeper;
  std::vector<PositionedPlayer> outfield_players;
  std::vector<const Player*> reserves;
  Strategy strategy;
  SetPieceDesignations designations{};
  std::vector<StandIn> stand_ins;
};

/***************************************************************
 * This is how to visualise the grid where to place the players:
 *
 *   GK: (always separate, fixed role)
 *
 *   Row 0 → furthest forward (attacking line)
 *   Row 4 → deepest outfield line (defensive line)
 *
 *   Example 5×5 grid (row × col):
 *
 *       (0,0)   (0,1)   (0,2)   (0,3)   (0,4)
 *       (1,0)   (1,1)   (1,2)   (1,3)   (1,4)
 *       (2,0)   (2,1)   (2,2)   (2,3)   (2,4)
 *       (3,0)   (3,1)   (3,2)   (3,3)   (3,4)
 *       (4,0)   (4,1)   (4,2)   (4,3)   (4,4)
 *
 * To place a player in a position, use:
 *   index = row * LINEUP_GRID_COLS + col;
 *
 * Example: Striker at (0,2) → index = 0 * 5 + 2 = 2
 *          Left-back at (4,0) → index = 4 * 5 + 0 = 20
 *
 ***************************************************************/
