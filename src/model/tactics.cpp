// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/tactics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

#include "model/lineup.h"
#include "model/player.h"
#include "model/strategy.h"

namespace
{
using enum TacticalRole;

// Slot classification (lineup coordinates, own goal at x = 0).
constexpr float DEFENCE_LINE_X = 0.30f;
constexpr float HOLDING_LINE_X = 0.42f;
constexpr float MIDFIELD_LINE_X = 0.55f;
constexpr float ATTACKING_LINE_X = 0.68f;
constexpr float WIDE_DEVIATION = 0.28f;
constexpr float STRIKER_WIDE_DEVIATION = 0.25f;

/** A special role has to fit this much better than Standard to be picked. */
constexpr float ROLE_PREFERENCE_MARGIN = 2.0f;
/** Full-back duties from pace, stamina and defending (attribute points). */
constexpr float ATTACKING_FULL_BACK_MARGIN = 5.0f;
constexpr float DEFENSIVE_FULL_BACK_MARGIN = 8.0f;
/** Stat used when a player lacks one (the 0-100 midpoint). */
constexpr float DEFAULT_STAT = 50.0f;

constexpr std::array<TacticalRole, 3> GOALKEEPER_ROLES = {Standard, LineKeeper,
                                                          SweeperKeeper};
constexpr std::array<TacticalRole, 2> FULL_BACK_ROLES = {Standard,
                                                         InsideFullBack};
constexpr std::array<TacticalRole, 3> CENTRE_BACK_ROLES = {Standard, Stopper,
                                                           CoverDefender};
constexpr std::array<TacticalRole, 3> HOLDING_ROLES = {Standard, Anchor,
                                                       Playmaker};
constexpr std::array<TacticalRole, 4> CENTRAL_ROLES = {Standard, BoxToBox,
                                                       Playmaker, Anchor};
constexpr std::array<TacticalRole, 3> ATTACKING_ROLES = {Standard, Playmaker,
                                                         SecondStriker};
constexpr std::array<TacticalRole, 3> WIDE_ROLES = {Standard, InsideForward,
                                                    TouchlineWinger};
constexpr std::array<TacticalRole, 5> STRIKER_ROLES = {
    Standard, TargetForward, Poacher, PressingForward, FalseNine};

constexpr std::array<const char*, static_cast<std::size_t>(
                                      std::to_underlying(TacticalRole::COUNT))>
    ROLE_KEYS = {
        "ROLE_STANDARD",         "ROLE_LINE_KEEPER",    "ROLE_SWEEPER_KEEPER",
        "ROLE_INSIDE_FULL_BACK", "ROLE_STOPPER",        "ROLE_COVER_DEFENDER",
        "ROLE_ANCHOR",           "ROLE_PLAYMAKER",      "ROLE_BOX_TO_BOX",
        "ROLE_SECOND_STRIKER",   "ROLE_INSIDE_FORWARD", "ROLE_TOUCHLINE_WINGER",
        "ROLE_TARGET_FORWARD",   "ROLE_POACHER",        "ROLE_PRESSING_FORWARD",
        "ROLE_FALSE_NINE"};

constexpr std::array<const char*, static_cast<std::size_t>(
                                      std::to_underlying(TacticalRole::COUNT))>
    ROLE_DESCRIPTION_KEYS = {
        "ROLE_STANDARD_HELP",         "ROLE_LINE_KEEPER_HELP",
        "ROLE_SWEEPER_KEEPER_HELP",   "ROLE_INSIDE_FULL_BACK_HELP",
        "ROLE_STOPPER_HELP",          "ROLE_COVER_DEFENDER_HELP",
        "ROLE_ANCHOR_HELP",           "ROLE_PLAYMAKER_HELP",
        "ROLE_BOX_TO_BOX_HELP",       "ROLE_SECOND_STRIKER_HELP",
        "ROLE_INSIDE_FORWARD_HELP",   "ROLE_TOUCHLINE_WINGER_HELP",
        "ROLE_TARGET_FORWARD_HELP",   "ROLE_POACHER_HELP",
        "ROLE_PRESSING_FORWARD_HELP", "ROLE_FALSE_NINE_HELP"};

constexpr std::array<const char*, static_cast<std::size_t>(
                                      std::to_underlying(RoleFamily::COUNT))>
    FAMILY_KEYS = {"ROLE_FAMILY_GOALKEEPER",  "ROLE_FAMILY_FULL_BACK",
                   "ROLE_FAMILY_CENTRE_BACK", "ROLE_FAMILY_HOLDING",
                   "ROLE_FAMILY_CENTRAL",     "ROLE_FAMILY_ATTACKING",
                   "ROLE_FAMILY_WIDE",        "ROLE_FAMILY_STRIKER"};

constexpr std::array<const char*, static_cast<std::size_t>(
                                      std::to_underlying(RoleDuty::COUNT))>
    DUTY_KEYS = {"ROLE_DUTY_DEFEND", "ROLE_DUTY_SUPPORT", "ROLE_DUTY_ATTACK"};

constexpr std::array<const char*, static_cast<std::size_t>(std::to_underlying(
                                      PossessionShape::COUNT))>
    SHAPE_KEYS = {"TACTIC_SHAPE_KEEP", "TACTIC_SHAPE_FULL_BACKS",
                  "TACTIC_SHAPE_BACK_THREE", "TACTIC_SHAPE_NARROW"};

// Key attributes, most important first.
using Attributes = std::span<const std::string_view>;
constexpr std::array<std::string_view, 2> GOALKEEPER_STANDARD = {"Goalkeeping",
                                                                 "Physicality"};
constexpr std::array<std::string_view, 3> FULL_BACK_STANDARD = {
    "Pace", "Defending", "Stamina"};
constexpr std::array<std::string_view, 2> CENTRE_BACK_STANDARD = {
    "Defending", "Physicality"};
constexpr std::array<std::string_view, 3> HOLDING_STANDARD = {
    "Defending", "Passing", "Stamina"};
constexpr std::array<std::string_view, 3> CENTRAL_STANDARD = {
    "Passing", "Stamina", "Vision"};
constexpr std::array<std::string_view, 3> ATTACKING_STANDARD = {
    "Passing", "Vision", "Dribbling"};
constexpr std::array<std::string_view, 3> WIDE_STANDARD = {"Pace", "Dribbling",
                                                           "Passing"};
constexpr std::array<std::string_view, 3> STRIKER_STANDARD = {
    "Shooting", "Pace", "Physicality"};

constexpr std::array<std::string_view, 2> LINE_KEEPER = {"Goalkeeping",
                                                         "Physicality"};
constexpr std::array<std::string_view, 3> SWEEPER_KEEPER = {"Goalkeeping",
                                                            "Pace", "Passing"};
constexpr std::array<std::string_view, 3> INSIDE_FULL_BACK = {
    "Passing", "Vision", "Defending"};
constexpr std::array<std::string_view, 3> STOPPER = {"Physicality", "Defending",
                                                     "Pace"};
constexpr std::array<std::string_view, 3> COVER_DEFENDER = {"Pace", "Defending",
                                                            "Vision"};
constexpr std::array<std::string_view, 3> ANCHOR = {"Defending", "Physicality",
                                                    "Passing"};
constexpr std::array<std::string_view, 3> PLAYMAKER = {"Passing", "Vision",
                                                       "Dribbling"};
constexpr std::array<std::string_view, 3> BOX_TO_BOX = {
    "Stamina", "Physicality", "Passing"};
constexpr std::array<std::string_view, 3> SECOND_STRIKER = {"Shooting", "Pace",
                                                            "Dribbling"};
constexpr std::array<std::string_view, 3> INSIDE_FORWARD = {
    "Shooting", "Dribbling", "Pace"};
constexpr std::array<std::string_view, 3> TOUCHLINE_WINGER = {
    "Pace", "Dribbling", "Passing"};
constexpr std::array<std::string_view, 3> TARGET_FORWARD = {
    "Physicality", "Shooting", "Passing"};
constexpr std::array<std::string_view, 2> POACHER = {"Shooting", "Pace"};
constexpr std::array<std::string_view, 3> PRESSING_FORWARD = {
    "Stamina", "Physicality", "Pace"};
constexpr std::array<std::string_view, 3> FALSE_NINE = {"Passing", "Vision",
                                                        "Dribbling"};

float stat(const Player& player, std::string_view name)
{
  const auto& stats = player.getStats();
  const auto found = stats.find(std::string(name));
  return found == stats.end() ? DEFAULT_STAT : found->second;
}

template <typename Array>
const char* keyOf(const Array& keys, std::size_t index)
{
  return index < keys.size() ? keys[index] : "";
}

/** Toward the middle of the pitch from the anchor's side. */
float inward(Vector2F anchor, float amount)
{
  return anchor.y < 0.5f ? amount : -amount;
}
}  // namespace

namespace Tactics
{
RoleFamily familyForSlot(Vector2F anchor)
{
  using enum RoleFamily;
  const float width = std::abs(anchor.y - 0.5f);
  const bool wide = width > WIDE_DEVIATION;
  if (anchor.x < DEFENCE_LINE_X) return wide ? FullBack : CentreBack;
  if (anchor.x < HOLDING_LINE_X) return wide ? FullBack : Holding;
  if (anchor.x < MIDFIELD_LINE_X) return wide ? Wide : Central;
  if (anchor.x < ATTACKING_LINE_X) return wide ? Wide : Attacking;
  return width > STRIKER_WIDE_DEVIATION ? Wide : Striker;
}

std::span<const TacticalRole> rolesFor(RoleFamily family)
{
  using enum RoleFamily;
  switch (family)
  {
    case Goalkeeper:
      return GOALKEEPER_ROLES;
    case FullBack:
      return FULL_BACK_ROLES;
    case CentreBack:
      return CENTRE_BACK_ROLES;
    case Holding:
      return HOLDING_ROLES;
    case Central:
      return CENTRAL_ROLES;
    case Attacking:
      return ATTACKING_ROLES;
    case Wide:
      return WIDE_ROLES;
    case Striker:
    case COUNT:
      break;
  }
  return STRIKER_ROLES;
}

bool allows(RoleFamily family, TacticalRole role)
{
  return std::ranges::contains(rolesFor(family), role);
}

bool hasDuty(RoleFamily family) { return family != RoleFamily::Goalkeeper; }

RoleProfile profile(TacticalRole role, RoleDuty duty)
{
  using enum RoleDuty;
  // Roles are composable numeric biases over the common match AI, rather than
  // separate player controllers. Start neutral, apply the role, then the duty.
  // To add a role, also register its allowed families, text keys and attribute
  // fit above/below; append its persisted enum value instead of renumbering.
  RoleProfile result;
  switch (role)
  {
    case Standard:
    case TacticalRole::COUNT:
      break;
    case LineKeeper:
      result.keeperDepthMetres = -2.5f;
      result.keeperSweep = -0.6f;
      break;
    case SweeperKeeper:
      result.keeperDepthMetres = 5.0f;
      result.keeperSweep = 1.0f;
      result.passDaring = 0.1f;
      break;
    case InsideFullBack:
      result.possessionAdvanceMetres = 4.0f;
      result.possessionWidthMetres = -14.0f;
      result.overlapBias = -1.0f;
      result.passDaring = 0.05f;
      break;
    case Stopper:
      result.defensiveAdvanceMetres = 4.0f;
      result.pressBias = 0.4f;
      break;
    case CoverDefender:
      result.defensiveAdvanceMetres = -4.0f;
      result.pressBias = -0.4f;
      break;
    case Anchor:
      result.possessionAdvanceMetres = -6.0f;
      result.defensiveAdvanceMetres = -3.0f;
      result.runBias = -1.0f;
      result.passDaring = -0.15f;
      result.shotBias = -0.15f;
      result.pressBias = -0.2f;
      break;
    case Playmaker:
      result.possessionAdvanceMetres = -2.0f;
      result.runBias = -0.4f;
      result.passDaring = 0.25f;
      result.targetBias = 0.1f;
      result.shotBias = -0.05f;
      break;
    case BoxToBox:
      result.possessionAdvanceMetres = 3.0f;
      result.runBias = 0.6f;
      result.pressBias = 0.2f;
      result.shotBias = 0.05f;
      break;
    case SecondStriker:
      result.possessionAdvanceMetres = 5.0f;
      result.runBias = 0.8f;
      result.shotBias = 0.12f;
      result.pressBias = -0.1f;
      break;
    case InsideForward:
      result.possessionWidthMetres = -6.0f;
      result.runBias = 0.3f;
      result.shotBias = 0.12f;
      result.cutInside = 1.0f;
      break;
    case TouchlineWinger:
      result.possessionWidthMetres = 6.0f;
      result.runBias = -0.1f;
      result.shotBias = -0.1f;
      result.cutInside = -1.0f;
      break;
    case TargetForward:
      result.possessionAdvanceMetres = 2.0f;
      result.runBias = -1.0f;
      result.targetBias = 0.25f;
      result.shotBias = 0.05f;
      result.pressBias = -0.2f;
      break;
    case Poacher:
      result.possessionAdvanceMetres = 3.0f;
      result.defensiveAdvanceMetres = 2.0f;
      result.runBias = 0.8f;
      result.shotBias = 0.15f;
      result.pressBias = -0.5f;
      break;
    case PressingForward:
      result.defensiveAdvanceMetres = 4.0f;
      result.runBias = 0.2f;
      result.pressBias = 1.0f;
      break;
    case FalseNine:
      result.possessionAdvanceMetres = -10.0f;
      result.runBias = -1.5f;
      result.passDaring = 0.2f;
      result.targetBias = 0.1f;
      result.shotBias = -0.08f;
      break;
  }
  if (role == LineKeeper || role == SweeperKeeper) return result;
  switch (duty)
  {
    case Defend:
      result.possessionAdvanceMetres -= 5.0f;
      result.runBias -= 0.5f;
      result.overlapBias -= 1.0f;
      result.passDaring -= 0.1f;
      result.shotBias -= 0.1f;
      break;
    case Attack:
      result.possessionAdvanceMetres += 5.0f;
      result.runBias += 0.5f;
      result.overlapBias += 0.6f;
      result.passDaring += 0.05f;
      result.shotBias += 0.05f;
      break;
    case Support:
    case COUNT:
      break;
  }
  return result;
}

const char* familyKey(RoleFamily family)
{
  return keyOf(FAMILY_KEYS,
               static_cast<std::size_t>(std::to_underlying(family)));
}

const char* roleKey(TacticalRole role)
{
  return keyOf(ROLE_KEYS, static_cast<std::size_t>(std::to_underlying(role)));
}

const char* roleDescriptionKey(TacticalRole role)
{
  return keyOf(ROLE_DESCRIPTION_KEYS,
               static_cast<std::size_t>(std::to_underlying(role)));
}

const char* dutyKey(RoleDuty duty)
{
  return keyOf(DUTY_KEYS, static_cast<std::size_t>(std::to_underlying(duty)));
}

const char* possessionShapeKey(PossessionShape shape)
{
  return keyOf(SHAPE_KEYS, static_cast<std::size_t>(std::to_underlying(shape)));
}

std::span<const std::string_view> keyAttributes(RoleFamily family,
                                                TacticalRole role)
{
  using enum RoleFamily;
  switch (role)
  {
    case LineKeeper:
      return LINE_KEEPER;
    case SweeperKeeper:
      return SWEEPER_KEEPER;
    case InsideFullBack:
      return INSIDE_FULL_BACK;
    case Stopper:
      return STOPPER;
    case CoverDefender:
      return COVER_DEFENDER;
    case Anchor:
      return ANCHOR;
    case Playmaker:
      return PLAYMAKER;
    case BoxToBox:
      return BOX_TO_BOX;
    case SecondStriker:
      return SECOND_STRIKER;
    case InsideForward:
      return INSIDE_FORWARD;
    case TouchlineWinger:
      return TOUCHLINE_WINGER;
    case TargetForward:
      return TARGET_FORWARD;
    case Poacher:
      return POACHER;
    case PressingForward:
      return PRESSING_FORWARD;
    case FalseNine:
      return FALSE_NINE;
    case Standard:
    case TacticalRole::COUNT:
      break;
  }
  switch (family)
  {
    case Goalkeeper:
      return GOALKEEPER_STANDARD;
    case FullBack:
      return FULL_BACK_STANDARD;
    case CentreBack:
      return CENTRE_BACK_STANDARD;
    case Holding:
      return HOLDING_STANDARD;
    case Central:
      return CENTRAL_STANDARD;
    case Attacking:
      return ATTACKING_STANDARD;
    case Wide:
      return WIDE_STANDARD;
    case Striker:
    case COUNT:
      break;
  }
  return STRIKER_STANDARD;
}

float roleFit(const Player& player, RoleFamily family, TacticalRole role)
{
  // Weighted by importance: the first attribute counts most.
  const Attributes attributes = keyAttributes(family, role);
  float total = 0.0f;
  float weights = 0.0f;
  float weight = 1.0f;
  for (const std::string_view name : attributes)
  {
    total += stat(player, name) * weight;
    weights += weight;
    weight *= 0.7f;
  }
  return weights > 0.0f ? total / weights : DEFAULT_STAT;
}

RoleDuty suggestedDuty(const Player& player, RoleFamily family)
{
  using enum RoleDuty;
  if (family != RoleFamily::FullBack) return Support;
  const float defending = stat(player, "Defending");
  const float engine = (stat(player, "Pace") + stat(player, "Stamina")) * 0.5f;
  const float flair = (stat(player, "Pace") + stat(player, "Dribbling")) * 0.5f;
  if (engine >= defending + ATTACKING_FULL_BACK_MARGIN) return Attack;
  if (defending >= flair + DEFENSIVE_FULL_BACK_MARGIN) return Defend;
  return Support;
}

TacticalRole suggestedRole(const Player& player, RoleFamily family)
{
  TacticalRole best = Standard;
  float bestFit = roleFit(player, family, Standard) + ROLE_PREFERENCE_MARGIN;
  for (const TacticalRole role : rolesFor(family))
  {
    if (role == Standard) continue;
    const float fit = roleFit(player, family, role);
    if (fit > bestFit)
    {
      bestFit = fit;
      best = role;
    }
  }
  return best;
}

Vector2F possessionOffset(PossessionShape shape, Vector2F anchor)
{
  using enum PossessionShape;
  const RoleFamily family = familyForSlot(anchor);
  switch (shape)
  {
    case KeepShape:
    case COUNT:
      break;
    case FullBacksPush:
      if (family == RoleFamily::FullBack) return {0.22f, 0.0f};
      if (family == RoleFamily::Wide) return {0.05f, inward(anchor, 0.08f)};
      break;
    case BuildWithThree:
      // The left full-back becomes a third centre-back, the right one a
      // wing-back; the holding player screens and the wide men push on.
      if (family == RoleFamily::FullBack)
        return anchor.y < 0.5f ? Vector2F{0.02f, inward(anchor, 0.14f)}
                               : Vector2F{0.25f, 0.0f};
      if (family == RoleFamily::Wide) return {0.10f, 0.0f};
      if (family == RoleFamily::Holding) return {-0.04f, 0.0f};
      if (family == RoleFamily::Central) return {0.03f, 0.0f};
      break;
    case NarrowFront:
      if (family == RoleFamily::FullBack) return {0.20f, 0.0f};
      if (family == RoleFamily::Wide) return {0.04f, inward(anchor, 0.18f)};
      if (family == RoleFamily::Attacking) return {0.03f, 0.0f};
      break;
  }
  return {0.0f, 0.0f};
}

void assignRolesToFit(Strategy& strategy, const Lineup& lineup)
{
  std::vector<SlotInstruction> slots;
  slots.reserve(lineup.getOutfieldPlayers().size());
  for (const auto& positioned : lineup.getOutfieldPlayers())
  {
    if (!positioned.player) continue;
    const RoleFamily family = familyForSlot(positioned.position);
    slots.push_back({positioned.position,
                     suggestedRole(*positioned.player, family),
                     suggestedDuty(*positioned.player, family),
                     {0.0f, 0.0f}});
  }
  strategy.setSlotInstructions(std::move(slots));
  strategy.setKeeperRole(
      lineup.getGoalkeeper()
          ? suggestedRole(*lineup.getGoalkeeper(), RoleFamily::Goalkeeper)
          : Standard);
}
}  // namespace Tactics
