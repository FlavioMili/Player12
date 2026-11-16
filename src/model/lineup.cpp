// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "lineup.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <utility>

#include "database/gamedata.h"
#include "model/role_utils.h"

// ---------------- Constructor -----------------
Lineup::Lineup() { clear(); }

// -------------------- Goalkeeper --------------------
void Lineup::setGoalkeeper(const Player* gk)
{
  goalkeeper = gk;  // nullptr allowed
  if (gk)
  {
    removeOutfieldPlayer(gk->getId());
    std::erase_if(reserves, [gk](const Player* player)
                  { return player && player->getId() == gk->getId(); });
  }
}

const Player* Lineup::getGoalkeeper() const { return goalkeeper; }

// -------------- Outfield Players ---------------
void Lineup::addOutfieldPlayer(const Player* player, Vector2F position)
{
  if (!player) return;
  if (goalkeeper && goalkeeper->getId() == player->getId()) return;
  if (std::ranges::any_of(outfield_players,
                          [player](const auto& positioned)
                          {
                            return positioned.player &&
                                   positioned.player->getId() ==
                                       player->getId();
                          }))
  {
    return;
  }
  position.x = std::clamp(position.x, 0.0f, 1.0f);
  position.y = std::clamp(position.y, 0.0f, 1.0f);
  outfield_players.push_back({player, position});
}

bool Lineup::moveOutfieldPlayer(PlayerID playerID, Vector2F newPosition)
{
  for (auto& posPlayer : outfield_players)
  {
    if (posPlayer.player && posPlayer.player->getId() == playerID)
    {
      posPlayer.position = {std::clamp(newPosition.x, 0.0f, 1.0f),
                            std::clamp(newPosition.y, 0.0f, 1.0f)};
      return true;
    }
  }
  return false;
}

void Lineup::removeOutfieldPlayer(PlayerID playerID)
{
  auto [first, last] = std::ranges::remove_if(
      outfield_players, [playerID](const PositionedPlayer& pp)
      { return pp.player && pp.player->getId() == playerID; });
  outfield_players.erase(first, last);
}

bool Lineup::removePlayer(PlayerID playerID)
{
  bool found = false;
  if (goalkeeper && goalkeeper->getId() == playerID)
  {
    goalkeeper = nullptr;
    found = true;
  }
  const auto matches = [playerID](const Player* player)
  { return player && player->getId() == playerID; };
  found |= std::erase_if(outfield_players,
                         [&matches](const PositionedPlayer& positioned)
                         { return matches(positioned.player); }) > 0;
  found |= std::erase_if(reserves, matches) > 0;
  std::erase_if(
      stand_ins, [playerID](const StandIn& entry)
      { return entry.regular == playerID || entry.stand_in == playerID; });
  for (PlayerID& designated : designations)
  {
    if (designated != playerID) continue;
    designated = PlayerID{};
    found = true;
  }
  return found;
}

PlayerRole Lineup::roleAt(Vector2F position)
{
  using enum PlayerRole;
  // Bands match the formation presets: back line about 0.20, holding
  // midfielders 0.37-0.38, midfield 0.43-0.46, number tens and inside
  // wingers about 0.60, forwards from 0.72.
  const bool left = position.y < 0.2f;
  const bool right = position.y > 0.8f;
  if (position.x < 0.32f)
  {
    if (left) return LB;
    if (right) return RB;
    return CB;
  }
  if (position.x < 0.55f)
  {
    if (left) return LM;
    if (right) return RM;
    return position.x < 0.40f ? CDM : CM;
  }
  if (left) return LW;
  if (right) return RW;
  return position.x < 0.68f ? CAM : ST;
}

const std::vector<Lineup::PositionedPlayer>& Lineup::getOutfieldPlayers() const
{
  return outfield_players;
}

bool Lineup::swapPlayers(PlayerID benchPlayerID, PlayerID pitchPlayerID)
{
  // Find bench player
  auto benchIt =
      std::ranges::find_if(reserves, [benchPlayerID](const Player* p)
                           { return p && p->getId() == benchPlayerID; });
  if (benchIt == reserves.end()) return false;

  // Check if it's the goalkeeper
  if (goalkeeper && goalkeeper->getId() == pitchPlayerID)
  {
    const Player* temp = *benchIt;
    *benchIt = goalkeeper;
    goalkeeper = temp;
    return true;
  }

  // Find pitch player
  if (auto pitchIt = std::ranges::find_if(
          outfield_players, [pitchPlayerID](const PositionedPlayer& pp)
          { return pp.player && pp.player->getId() == pitchPlayerID; });
      pitchIt != outfield_players.end())
  {
    const Player* temp = *benchIt;
    *benchIt = pitchIt->player;
    pitchIt->player = temp;
    return true;
  }

  return false;
}

// -------------- Reserves ---------------
void Lineup::setReserves(const std::vector<const Player*>& subs)
{
  reserves.clear();
  for (const Player* player : subs)
  {
    if (!player || (goalkeeper && goalkeeper->getId() == player->getId()) ||
        std::ranges::any_of(outfield_players,
                            [player](const auto& positioned)
                            {
                              return positioned.player &&
                                     positioned.player->getId() ==
                                         player->getId();
                            }) ||
        std::ranges::contains(reserves, player))
    {
      continue;
    }
    reserves.push_back(player);
  }
  if (reserves.size() > MAX_SUBSTITUTES) reserves = chooseBench(reserves);
}

std::vector<const Player*> Lineup::chooseBench(
    std::span<const Player* const> candidates)
{
  // Available players first, each group keeping the callers' order.
  std::vector<const Player*> ordered;
  ordered.reserve(candidates.size());
  for (const bool available : {true, false})
    for (const Player* player : candidates)
      if (player && player->isAvailable() == available &&
          !std::ranges::contains(ordered, player))
        ordered.push_back(player);

  enum class Group : uint8_t
  {
    Keeper,
    Defender,
    Midfielder,
    Forward
  };
  using enum Group;
  using enum PlayerRole;
  const auto groupOf = [](const Player* player)
  {
    switch (player->getRole())
    {
      case GK:
        return Keeper;
      case CB:
      case LB:
      case RB:
        return Defender;
      case LW:
      case RW:
      case ST:
        return Forward;
      default:
        return Midfielder;
    }
  };
  std::vector<const Player*> bench;
  bench.reserve(MAX_SUBSTITUTES);
  // Cover first: a keeper, then one player of each outfield line.
  for (const Group group : {Keeper, Defender, Midfielder, Forward})
  {
    const auto found = std::ranges::find_if(
        ordered, [&](const Player* player)
        { return groupOf(player) == group && player->isAvailable(); });
    if (found != ordered.end() && bench.size() < MAX_SUBSTITUTES)
      bench.push_back(*found);
  }
  // Then the best of the rest, at most one more keeper.
  for (const Player* player : ordered)
  {
    if (bench.size() >= MAX_SUBSTITUTES) break;
    if (std::ranges::contains(bench, player)) continue;
    if (groupOf(player) == Keeper &&
        std::ranges::count_if(bench, [&](const Player* chosen)
                              { return groupOf(chosen) == Keeper; }) >= 2)
      continue;
    bench.push_back(player);
  }
  return bench;
}

bool Lineup::bringIn(const Player* player, PlayerID replaced)
{
  if (!player || player->getId() == replaced) return false;
  const PlayerID incoming = player->getId();
  if (const auto selected = [incoming](const Player* candidate)
      { return candidate && candidate->getId() == incoming; };
      selected(goalkeeper) ||
      std::ranges::any_of(outfield_players, [&](const PositionedPlayer& slot)
                          { return selected(slot.player); }) ||
      std::ranges::any_of(reserves, selected))
    return false;
  if (goalkeeper && goalkeeper->getId() == replaced)
  {
    goalkeeper = player;
    return true;
  }
  for (PositionedPlayer& slot : outfield_players)
  {
    if (slot.player && slot.player->getId() == replaced)
    {
      slot.player = player;
      return true;
    }
  }
  for (const Player*& reserve : reserves)
  {
    if (reserve && reserve->getId() == replaced)
    {
      reserve = player;
      return true;
    }
  }
  return false;
}

const std::vector<const Player*>& Lineup::getReserves() const
{
  return reserves;
}

// --------------- Strategy ------------------
void Lineup::setStrategy(const Strategy& strat) { strategy = strat; }

const Strategy& Lineup::getStrategy() const { return strategy; }

// ---------- Captain and set-piece takers ---------
namespace
{
float stat(const Player& player, const char* name)
{
  const auto found = player.getStats().find(name);
  return found == player.getStats().end() ? 0.0f : found->second;
}
}  // namespace

const char* SetPieces::dutyKey(SetPieceDuty duty)
{
  using enum SetPieceDuty;
  switch (duty)
  {
    case Captain:
      return "SET_PIECE_CAPTAIN";
    case ViceCaptain:
      return "SET_PIECE_VICE_CAPTAIN";
    case Penalties:
      return "SET_PIECE_PENALTIES";
    case FreeKicks:
      return "SET_PIECE_FREE_KICKS";
    case CornersLeft:
      return "SET_PIECE_CORNERS_LEFT";
    case CornersRight:
      return "SET_PIECE_CORNERS_RIGHT";
    case LongThrows:
      return "SET_PIECE_LONG_THROWS";
    case COUNT:
      break;
  }
  return "SET_PIECE_CAPTAIN";
}

bool SetPieces::isLeadership(SetPieceDuty duty)
{
  using enum SetPieceDuty;
  return duty == Captain || duty == ViceCaptain;
}

float SetPieces::score(SetPieceDuty duty, const Player& player)
{
  using enum SetPieceDuty;
  if (isLeadership(duty) || player.getRole() == PlayerRole::GK) return 0.0f;
  // A corner from the side of the preferred foot is the natural delivery.
  constexpr float FOOT_BONUS = 3.0f;
  switch (duty)
  {
    case Penalties:
    case FreeKicks:
      return stat(player, "Shooting") * 0.75f +
             stat(player, "Passing") * 0.15f + stat(player, "Vision") * 0.10f;
    case CornersLeft:
    case CornersRight:
    {
      const bool leftCorner = duty == CornersLeft;
      const bool leftFooted = player.getFoot() == Foot::Left;
      return stat(player, "Passing") * 0.6f + stat(player, "Vision") * 0.4f +
             (leftCorner == leftFooted ? FOOT_BONUS : 0.0f);
    }
    case LongThrows:
    {
      // Strength first; every centimetre above 175 adds some distance.
      const float reach =
          std::clamp((static_cast<float>(player.getHeight()) - 175.0f) * 0.5f,
                     -5.0f, 10.0f);
      return std::clamp(stat(player, "Physicality") * 0.85f +
                            stat(player, "Stamina") * 0.15f + reach,
                        0.0f, 100.0f);
    }
    default:
      return 0.0f;
  }
}

const Player* SetPieces::best(SetPieceDuty duty,
                              const std::vector<const Player*>& candidates)
{
  const Player* chosen = nullptr;
  float chosenScore = 0.0f;
  for (const Player* player : candidates)
  {
    if (player == nullptr) continue;
    const float value = score(duty, *player);
    if (value <= 0.0f) continue;
    if (chosen == nullptr || value > chosenScore ||
        (value == chosenScore && player->getId() < chosen->getId()))
    {
      chosen = player;
      chosenScore = value;
    }
  }
  return chosen;
}

PlayerID Lineup::getDesignated(SetPieceDuty duty) const
{
  return duty < SetPieceDuty::COUNT
             ? designations[static_cast<size_t>(std::to_underlying(duty))]
             : PlayerID{};
}

void Lineup::setDesignated(SetPieceDuty duty, PlayerID playerID)
{
  if (duty >= SetPieceDuty::COUNT) return;
  designations[static_cast<size_t>(std::to_underlying(duty))] = playerID;
  // One player cannot be captain and vice-captain at once.
  if (playerID == PlayerID{} || !SetPieces::isLeadership(duty)) return;
  const SetPieceDuty other = duty == SetPieceDuty::Captain
                                 ? SetPieceDuty::ViceCaptain
                                 : SetPieceDuty::Captain;
  if (designations[static_cast<size_t>(std::to_underlying(other))] == playerID)
    designations[static_cast<size_t>(std::to_underlying(other))] = PlayerID{};
}

const SetPieceDesignations& Lineup::getDesignations() const
{
  return designations;
}

void Lineup::setDesignations(const SetPieceDesignations& values)
{
  designations = values;
}

std::vector<const Player*> Lineup::starters() const
{
  std::vector<const Player*> result;
  result.reserve(outfield_players.size() + 1);
  if (goalkeeper) result.push_back(goalkeeper);
  for (const auto& positioned : outfield_players)
    if (positioned.player) result.push_back(positioned.player);
  return result;
}

bool Lineup::isStarter(PlayerID playerID) const
{
  if (playerID == PlayerID{}) return false;
  if (goalkeeper && goalkeeper->getId() == playerID) return true;
  return std::ranges::any_of(
      outfield_players, [playerID](const PositionedPlayer& positioned)
      { return positioned.player && positioned.player->getId() == playerID; });
}

const Player* Lineup::effectiveTaker(SetPieceDuty duty) const
{
  if (duty >= SetPieceDuty::COUNT) return nullptr;
  const std::vector<const Player*> xi = starters();
  const auto starter = [&xi](PlayerID playerID) -> const Player*
  {
    if (playerID == PlayerID{}) return nullptr;
    const auto found =
        std::ranges::find_if(xi, [playerID](const Player* player)
                             { return player->getId() == playerID; });
    return found == xi.end() ? nullptr : *found;
  };
  if (duty == SetPieceDuty::Captain)
  {
    const PlayerID captainID = getDesignated(SetPieceDuty::Captain);
    if (captainID == PlayerID{}) return nullptr;
    if (const Player* captain = starter(captainID)) return captain;
    return starter(getDesignated(SetPieceDuty::ViceCaptain));
  }
  if (duty == SetPieceDuty::ViceCaptain)
    return starter(getDesignated(SetPieceDuty::ViceCaptain));
  if (const Player* designated = starter(getDesignated(duty));
      designated && SetPieces::score(duty, *designated) > 0.0f)
    return designated;
  return SetPieces::best(duty, xi);
}

// ---------- Debug / Visualisation --------------
std::string Lineup::toString() const
{
  std::ostringstream oss;
  oss << "Goalkeeper: " << (goalkeeper ? goalkeeper->getName() : "None")
      << "\n";
  oss << "Outfield Players:\n";
  for (const auto& posPlayer : outfield_players)
  {
    if (posPlayer.player)
    {
      oss << "- " << posPlayer.player->getName() << " at ("
          << posPlayer.position.x << ", " << posPlayer.position.y << ")\n";
    }
  }
  oss << "Reserves: ";
  for (auto* sub : reserves)
  {
    oss << (sub ? sub->getName() : "Empty") << " ";
  }
  oss << "\n";
  return oss.str();
}

void Lineup::generateStartingXI(const class GameData& gamedata,
                                const std::vector<PlayerID>& allPlayerIDs,
                                const StatsConfig& stats_config)
{
  using enum PlayerRole;
  // Clear previous lineup
  clear();
  reserves.clear();

  std::vector<const Player*> potentialOutfieldPlayers;
  const Player* bestGK = nullptr;

  // Separate GK from outfield
  for (const auto& playerID : allPlayerIDs)
  {
    const Player& p = gamedata.getPlayers().at(playerID);
    if (p.getRole() == GK)
    {
      if (!bestGK ||
          p.getOverall(stats_config) > bestGK->getOverall(stats_config))
      {
        if (bestGK) reserves.push_back(bestGK);
        bestGK = &p;
      }
      else
      {
        reserves.push_back(&p);
      }
    }
    else
    {
      potentialOutfieldPlayers.push_back(&p);
    }
  }

  // If no GK found, try to use the worst outfield player as GK
  if (!bestGK && !potentialOutfieldPlayers.empty())
  {
    const auto emergencyGoalkeeper = std::ranges::min_element(
        potentialOutfieldPlayers, [&](const Player* a, const Player* b)
        { return a->getOverall(stats_config) < b->getOverall(stats_config); });
    bestGK = *emergencyGoalkeeper;
    potentialOutfieldPlayers.erase(emergencyGoalkeeper);
  }

  goalkeeper = bestGK;
  if (!goalkeeper) return;  // Still no players at all

  // Fill a balanced 4-4-2. Pure overall sorting routinely produced teams with
  // no defenders because ratings from unlike roles are not interchangeable.
  static constexpr std::array<PlayerRole, 10> SLOT_ROLES = {LB, CB, CB, RB, LM,
                                                            CM, CM, RM, ST, ST};
  static constexpr std::array<Vector2F, 10> SLOT_POSITIONS = {
      Vector2F{0.20f, 0.12f}, Vector2F{0.20f, 0.38f}, Vector2F{0.20f, 0.62f},
      Vector2F{0.20f, 0.88f}, Vector2F{0.43f, 0.12f}, Vector2F{0.43f, 0.38f},
      Vector2F{0.43f, 0.62f}, Vector2F{0.43f, 0.88f}, Vector2F{0.78f, 0.38f},
      Vector2F{0.78f, 0.62f}};

  const auto isDefender = [](PlayerRole role)
  { return role == LB || role == CB || role == RB; };
  const auto isCentralMidfielder = [](PlayerRole role)
  { return role == CDM || role == CM || role == CAM; };
  const auto roleFit = [&](PlayerRole actual, PlayerRole expected)
  {
    if (actual == expected) return 30.0;
    if (expected == CB && isDefender(actual)) return 16.0;
    if ((expected == LB || expected == RB) && isDefender(actual)) return 13.0;
    if (expected == CM && isCentralMidfielder(actual)) return 24.0;
    if (expected == LM && (actual == LW || actual == LB)) return 18.0;
    if (expected == RM && (actual == RW || actual == RB)) return 18.0;
    if ((expected == LM || expected == RM) &&
        (isCentralMidfielder(actual) || actual == LM || actual == RM ||
         actual == LW || actual == RW))
      return 10.0;
    if (expected == ST && (actual == LW || actual == RW)) return 15.0;
    return -15.0;
  };

  for (size_t slot = 0;
       slot < SLOT_ROLES.size() && !potentialOutfieldPlayers.empty(); ++slot)
  {
    auto best = potentialOutfieldPlayers.end();
    double bestScore = -std::numeric_limits<double>::infinity();
    for (auto candidate = potentialOutfieldPlayers.begin();
         candidate != potentialOutfieldPlayers.end(); ++candidate)
    {
      const double score = (*candidate)->getOverall(stats_config) +
                           roleFit((*candidate)->getRole(), SLOT_ROLES[slot]);
      if (score > bestScore)
      {
        bestScore = score;
        best = candidate;
      }
    }
    addOutfieldPlayer(*best, SLOT_POSITIONS[slot]);
    potentialOutfieldPlayers.erase(best);
  }

  // The matchday bench from everyone left out, best first.
  std::vector<const Player*> candidates = reserves;
  candidates.insert(candidates.end(), potentialOutfieldPlayers.begin(),
                    potentialOutfieldPlayers.end());
  std::ranges::stable_sort(
      candidates, [&stats_config](const Player* a, const Player* b)
      { return a->getOverall(stats_config) > b->getOverall(stats_config); });
  reserves.clear();
  setReserves(candidates);
}
