// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/match_engine.h"

// Implementation map (search these function names to find each subsystem):
// - Construction/loadAttributes: borrow career players, cache match attributes.
// - simulateStepBody: order commands, movement, actions, physics and rules.
// - refreshTacticalTargets: coordinate each team's off-ball intentions.
// - integrateMovements/integrateControlled: turn intentions into body motion.
// - decideAction/evaluatePassOption: compare actions before executing one.
// - updateBall/resolveLooseBall: integrate flight and resolve actual contacts.
// - setup*/completeRestart: own dead-ball placement, delay and legal restart.
// - performSubstitution/removeFromPitch: maintain slots and player statistics.
// See docs/development/match-engine.md and player-behavior.md for the
// rationale.

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <span>
#include <sstream>

#include "model/match_commentary.h"
#include "model/medical_centre.h"
#include "model/player.h"
#include "model/role_utils.h"

namespace
{
constexpr float EPSILON = 0.00001f;

// Credit only flight since launch before the keeper's first body update.
// The control type is deduced so this timing helper stays private to this file.
void ageInitialGoalkeeperReaction(auto& control, float flightSeconds)
{
  if (control.state != GoalkeeperState::DIVE || control.timer != 0.0F)
  {
    return;
  }
  control.reactionRemaining =
      std::max(0.0F, control.reactionRemaining - flightSeconds);
}

std::uint32_t makeNonBlockingMatchSeed()
{
  static std::atomic<std::uint32_t> sequence{0};
  const auto clockValue = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const std::uint64_t mixed =
      clockValue ^ (static_cast<std::uint64_t>(++sequence) << 32U);
  return static_cast<std::uint32_t>(mixed ^ (mixed >> 32U));
}

constexpr float PITCH_LENGTH = MatchTuning::Pitch::LENGTH_METRES;
constexpr float PITCH_WIDTH = MatchTuning::Pitch::WIDTH_METRES;
constexpr float TWO_PI = 2.0f * std::numbers::pi_v<float>;
/** Share of the drag-free horizontal travel a lofted ball really covers. */
constexpr float AIR_TRAVEL_SHARE = 0.88f;
constexpr double HIGHLIGHT_LEAD = MatchTuning::Playback::HIGHLIGHT_LEAD_SECONDS;
constexpr double HIGHLIGHT_TAIL = MatchTuning::Playback::HIGHLIGHT_TAIL_SECONDS;
constexpr double HIGHLIGHT_MERGE_GAP = MatchTuning::Playback::MERGE_GAP_SECONDS;
constexpr double PREDICTION_HORIZON =
    MatchTuning::Playback::PREDICTION_HORIZON_SECONDS;
constexpr double STEP_SECONDS = MatchTuning::Timing::FIXED_STEP_SECONDS;
constexpr auto RUN_TIMING_EPOCH_STEPS =
    static_cast<std::uint32_t>(MatchTuning::Passing::RUN_TIMING_EPOCH_SECONDS /
                               MatchTuning::Timing::FIXED_STEP_SECONDS);

float length(Vector2F vector)
{
  return std::sqrt(vector.x * vector.x + vector.y * vector.y);
}

/**
 * atan2 approximation (max error ~0.0015 rad) for per-step facing updates;
 * the facing angle is presentation state only.
 */
float fastAtan2(float y, float x)
{
  const float absX = std::abs(x);
  const float absY = std::abs(y);
  if (absX < 1e-12f && absY < 1e-12f) return 0.0f;
  const float ratio = std::min(absX, absY) / std::max(absX, absY);
  const float squared = ratio * ratio;
  float angle =
      ((-0.0464964749f * squared + 0.15931422f) * squared - 0.327622764f) *
          squared * ratio +
      ratio;
  if (absY > absX) angle = 1.57079637f - angle;
  if (x < 0.0f) angle = 3.14159274f - angle;
  return y < 0.0f ? -angle : angle;
}

/** Metric offset (metres) of a normalised pitch displacement. */
Vector2F toMetres(Vector2F normalisedDelta)
{
  return {normalisedDelta.x * PITCH_LENGTH, normalisedDelta.y * PITCH_WIDTH};
}

constexpr float INVERSE_LENGTH = 1.0f / PITCH_LENGTH;
constexpr float INVERSE_WIDTH = 1.0f / PITCH_WIDTH;
constexpr float INVERSE_FATIGUE_RANGE =
    1.0f / (1.0f - MatchTuning::Player::MINIMUM_STAMINA);

/** Normalised pitch displacement of a metric offset. */
Vector2F toPitch(Vector2F metres)
{
  return {metres.x * INVERSE_LENGTH, metres.y * INVERSE_WIDTH};
}

/** Isotropic distance in metres between two normalised pitch points. */
float distance(Vector2F first, Vector2F second)
{
  return length(toMetres({first.x - second.x, first.y - second.y}));
}

Vector2F normalized(Vector2F vector)
{
  const float magnitude = length(vector);
  if (magnitude <= EPSILON) return {0.0f, 0.0f};
  return {vector.x / magnitude, vector.y / magnitude};
}

/** Unit direction in metric space from one pitch point to another. */
Vector2F metricDirection(Vector2F from, Vector2F to)
{
  return normalized(toMetres({to.x - from.x, to.y - from.y}));
}

float uniform01(std::mt19937& engine)
{
  // Portable mapping of the 32-bit output to [0, 1) (24 bits of mantissa).
  return static_cast<float>(engine() >> 8U) * 0x1p-24f;
}

/** Standard normal sample (Box-Muller) from portable uniforms. */
float gaussian(std::mt19937& engine)
{
  const float first = std::max(uniform01(engine), 1e-7f);
  const float second = uniform01(engine);
  return std::sqrt(-2.0f * std::log(first)) * std::cos(TWO_PI * second);
}

bool stateClockRuns(MatchState state)
{
  return state != MatchState::KICK_OFF && state != MatchState::HALF_TIME &&
         state != MatchState::FULL_TIME &&
         state != MatchState::PENALTY_SHOOTOUT;
}

float stretchAttribute(float raw)
{
  using P = MatchTuning::Player;
  return std::clamp(
      P::ATTRIBUTE_PIVOT +
          P::ATTRIBUTE_SATURATION *
              std::tanh((raw - P::ATTRIBUTE_PIVOT) * P::ATTRIBUTE_CONTRAST /
                        P::ATTRIBUTE_SATURATION),
      P::MIN_ATTRIBUTE, 1.0f);
}

bool active(const MatchPlayer& player)
{
  return player.player != nullptr && player.onPitch;
}

bool isHighlightTrigger(MatchEventType type)
{
  return type == MatchEventType::SHOT || type == MatchEventType::GOAL ||
         type == MatchEventType::OWN_GOAL || type == MatchEventType::PENALTY ||
         type == MatchEventType::YELLOW_CARD ||
         type == MatchEventType::SECOND_YELLOW ||
         type == MatchEventType::RED_CARD ||
         type == MatchEventType::PENALTY_SHOOTOUT;
}

/** Air drag coefficient with the drag crisis between two speeds. */
float dragCoefficient(float speed)
{
  using B = MatchTuning::Ball;
  const float t =
      std::clamp((speed - B::DRAG_CRISIS_LOW_SPEED) /
                     (B::DRAG_CRISIS_HIGH_SPEED - B::DRAG_CRISIS_LOW_SPEED),
                 0.0f, 1.0f);
  const float smooth = t * t * (3.0f - 2.0f * t);
  return B::DRAG_SUBCRITICAL +
         (B::DRAG_SUPERCRITICAL - B::DRAG_SUBCRITICAL) * smooth;
}

/**
 * Ball state along its horizontal direction of travel. One physics function
 * serves the live integrator and the launch solvers so predicted and real
 * flights match exactly.
 */
struct FlightState
{
  float horizontalSpeed = 0.0f;
  float verticalSpeed = 0.0f;
  float height = 0.0f;
  bool bounced = false;
};

/** Advances a flight by `dt`; returns the horizontal distance travelled. */
/** Quadratic drag per unit of drag coefficient (1/m): 0.5 rho A / m. */
constexpr float FLIGHT_DRAG_SCALE = 0.5f * MatchTuning::Ball::AIR_DENSITY *
                                    MatchTuning::Ball::CROSS_SECTION_M2 /
                                    MatchTuning::Ball::MASS_KG;

float stepFlight(FlightState& flight, float dt)
{
  using B = MatchTuning::Ball;
  constexpr float DRAG_SCALE = FLIGHT_DRAG_SCALE;
  flight.bounced = false;
  const bool airborne = flight.height > 0.0f || flight.verticalSpeed > 0.0f;
  if (airborne)
  {
    const float speed =
        std::sqrt(flight.horizontalSpeed * flight.horizontalSpeed +
                  flight.verticalSpeed * flight.verticalSpeed);
    // Implicit quadratic drag stays stable at any sub-step length.
    const float damping =
        1.0f / (1.0f + DRAG_SCALE * dragCoefficient(speed) * speed * dt);
    flight.horizontalSpeed *= damping;
    flight.verticalSpeed = flight.verticalSpeed * damping - B::GRAVITY * dt;
    flight.height += flight.verticalSpeed * dt;
    if (flight.height <= 0.0f)
    {
      flight.height = 0.0f;
      flight.bounced = true;
      flight.verticalSpeed = -flight.verticalSpeed * B::BOUNCE_RESTITUTION;
      if (flight.verticalSpeed < B::MIN_BOUNCE_SPEED)
        flight.verticalSpeed = 0.0f;
      flight.horizontalSpeed *= B::BOUNCE_TANGENTIAL_RETAINED;
    }
    return flight.horizontalSpeed * dt;
  }
  // Rolling: constant rolling resistance, a small viscous term and air drag.
  const float speed = flight.horizontalSpeed;
  const float deceleration =
      B::ROLLING_DECELERATION + B::ROLLING_VISCOUS_PER_SECOND * speed +
      DRAG_SCALE * dragCoefficient(speed) * speed * speed;
  const float next = std::max(0.0f, speed - deceleration * dt);
  flight.horizontalSpeed = next < B::STOP_SPEED ? 0.0f : next;
  return 0.5f * (speed + next) * dt;
}

/** Whether a point lies in the penalty area defended by the given side. */
bool inPenaltyArea(Vector2F position, bool defendingHome)
{
  constexpr float HALF_WIDTH =
      MatchTuning::Pitch::PENALTY_AREA_HALF_WIDTH_METRES /
      MatchTuning::Pitch::WIDTH_METRES;
  if (std::abs(position.y - MatchTuning::Pitch::CENTRE) > HALF_WIDTH)
    return false;
  return defendingHome
             ? position.x <= MatchTuning::Pitch::LEFT_PENALTY_AREA_EDGE
             : position.x >= MatchTuning::Pitch::RIGHT_PENALTY_AREA_EDGE;
}

bool isDefensiveRole(PlayerRole role)
{
  return role == PlayerRole::CB || role == PlayerRole::LB ||
         role == PlayerRole::RB || role == PlayerRole::CDM;
}

bool isAttackingRole(PlayerRole role)
{
  return role == PlayerRole::ST || role == PlayerRole::LW ||
         role == PlayerRole::RW || role == PlayerRole::CAM;
}

/** Coarse role group used to pick like-for-like substitutes. */
int roleGroup(PlayerRole role)
{
  if (role == PlayerRole::GK) return 0;
  if (role == PlayerRole::CB || role == PlayerRole::LB ||
      role == PlayerRole::RB)
    return 1;
  if (isAttackingRole(role)) return 3;
  return 2;
}

/** Stable insertion sort of the first `count` entries (tiny fixed arrays). */
template <typename Array, typename Less>
void insertionSort(Array& values, std::size_t count, Less less)
{
  for (std::size_t index = 1; index < count; ++index)
  {
    auto value = values[index];
    std::size_t slot = index;
    while (slot > 0 && less(value, values[slot - 1]))
    {
      values[slot] = values[slot - 1];
      --slot;
    }
    values[slot] = value;
  }
}

std::uint64_t splitMix64(std::uint64_t value)
{
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

std::string_view stateName(MatchState state)
{
  switch (state)
  {
    case MatchState::KICK_OFF:
      return "kick_off";
    case MatchState::PLAYING:
      return "playing";
    case MatchState::THROW_IN:
      return "throw_in";
    case MatchState::GOAL_KICK:
      return "goal_kick";
    case MatchState::CORNER_KICK:
      return "corner_kick";
    case MatchState::FREE_KICK:
      return "free_kick";
    case MatchState::PENALTY:
      return "penalty";
    case MatchState::GOAL:
      return "goal";
    case MatchState::PENALTY_SHOOTOUT:
      return "penalty_shootout";
    case MatchState::HALF_TIME:
      return "half_time";
    case MatchState::FULL_TIME:
      return "full_time";
  }
  return "unknown";
}

std::string_view teamPhaseName(TeamPhase phase)
{
  switch (phase)
  {
    case TeamPhase::STOPPAGE:
      return "stoppage";
    case TeamPhase::SET_PIECE:
      return "set_piece";
    case TeamPhase::DEFENSIVE_BLOCK:
      return "defensive_block";
    case TeamPhase::DEFENSIVE_TRANSITION:
      return "defensive_transition";
    case TeamPhase::ATTACKING_TRANSITION:
      return "attacking_transition";
    case TeamPhase::POSSESSION:
      return "possession";
    case TeamPhase::FINAL_THIRD:
      return "final_third";
  }
  return "unknown";
}

std::string_view intentName(PlayerIntent intent)
{
  switch (intent)
  {
    case PlayerIntent::HOLD_SHAPE:
      return "hold_shape";
    case PlayerIntent::CARRY_BALL:
      return "carry_ball";
    case PlayerIntent::OFFER_SUPPORT:
      return "offer_support";
    case PlayerIntent::RECEIVE_PASS:
      return "receive_pass";
    case PlayerIntent::RUN_IN_BEHIND:
      return "run_in_behind";
    case PlayerIntent::ATTACK_BOX:
      return "attack_box";
    case PlayerIntent::OVERLAP:
      return "overlap";
    case PlayerIntent::PRESS_BALL:
      return "press_ball";
    case PlayerIntent::COVER_PRESS:
      return "cover_press";
    case PlayerIntent::BLOCK_PASSING_LANE:
      return "block_passing_lane";
    case PlayerIntent::MARK_OPPONENT:
      return "mark_opponent";
    case PlayerIntent::CLAIM_LOOSE_BALL:
      return "claim_loose_ball";
    case PlayerIntent::RECOVER_SHAPE:
      return "recover_shape";
    case PlayerIntent::GOALKEEP:
      return "goalkeep";
  }
  return "unknown";
}

float movementSpeedScale(PlayerIntent intent)
{
  switch (intent)
  {
    case PlayerIntent::CARRY_BALL:
      return MatchTuning::Player::CARRY_BALL_SPEED_SCALE;
    case PlayerIntent::OFFER_SUPPORT:
      return MatchTuning::Player::SUPPORT_SPEED_SCALE;
    case PlayerIntent::RECEIVE_PASS:
      return MatchTuning::Player::ATTACKING_RUN_SPEED_SCALE;
    case PlayerIntent::RUN_IN_BEHIND:
    case PlayerIntent::ATTACK_BOX:
    case PlayerIntent::OVERLAP:
      return MatchTuning::Player::ATTACKING_RUN_SPEED_SCALE;
    case PlayerIntent::PRESS_BALL:
    case PlayerIntent::CLAIM_LOOSE_BALL:
      return MatchTuning::Player::PRESS_SPEED_SCALE;
    case PlayerIntent::COVER_PRESS:
    case PlayerIntent::BLOCK_PASSING_LANE:
      return MatchTuning::Player::COVER_SPEED_SCALE;
    case PlayerIntent::MARK_OPPONENT:
      return MatchTuning::Player::MARKING_SPEED_SCALE;
    case PlayerIntent::RECOVER_SHAPE:
      return MatchTuning::Player::RECOVERY_SPEED_SCALE;
    case PlayerIntent::GOALKEEP:
      return MatchTuning::Player::GOALKEEPER_MOVEMENT_SPEED_SCALE;
    case PlayerIntent::HOLD_SHAPE:
      return MatchTuning::Player::HOLD_SHAPE_SPEED_SCALE;
  }
  return MatchTuning::Player::HOLD_SHAPE_SPEED_SCALE;
}

std::string_view passIntentName(PassIntent intent)
{
  switch (intent)
  {
    case PassIntent::RECYCLE:
      return "recycle";
    case PassIntent::PROGRESSIVE:
      return "progressive";
    case PassIntent::THROUGH_BALL:
      return "through_ball";
    case PassIntent::CROSS:
      return "cross";
    case PassIntent::CUTBACK:
      return "cutback";
    case PassIntent::SWITCH_PLAY:
      return "switch_play";
    case PassIntent::PRESSURE_RELEASE:
      return "pressure_release";
    case PassIntent::SET_PIECE:
      return "set_piece";
  }
  return "unknown";
}

std::string_view scenarioActionName(ScenarioAction action)
{
  switch (action)
  {
    case ScenarioAction::SHOT:
      return "shot";
    case ScenarioAction::PASS:
      return "pass";
    case ScenarioAction::CARRY:
      return "carry";
    case ScenarioAction::SHIELD:
      return "shield";
    case ScenarioAction::CLEAR:
      return "clear";
    case ScenarioAction::NONE:
      return "none";
  }
  return "unknown";
}
}  // namespace

std::string_view goalkeeperStateName(GoalkeeperState state)
{
  switch (state)
  {
    case GoalkeeperState::SET_POSITION:
      return "set_position";
    case GoalkeeperState::SWEEP:
      return "sweep";
    case GoalkeeperState::RUSH:
      return "rush";
    case GoalkeeperState::CLAIM:
      return "claim";
    case GoalkeeperState::DIVE:
      return "dive";
    case GoalkeeperState::HOLD:
      return "hold";
    case GoalkeeperState::DISTRIBUTE:
      return "distribute";
    case GoalkeeperState::RECOVER:
      return "recover";
  }
  return "unknown";
}

MatchEngine::MatchEngine(const Lineup& home_lineup, const Lineup& away_lineup,
                         const Strategy& home_strat, const Strategy& away_strat,
                         const StatsConfig& config)
    : MatchEngine(home_lineup, away_lineup, home_strat, away_strat, config,
                  makeNonBlockingMatchSeed())
{
}

MatchEngine::MatchEngine(const Lineup& home_lineup, const Lineup& away_lineup,
                         const Strategy& home_strat, const Strategy& away_strat,
                         const StatsConfig& config, uint32_t seed)
    : statsConfig(config),
      homeStrategy(home_strat),
      awayStrategy(away_strat),
      rng(seed),
      incidentRng(static_cast<std::uint32_t>(splitMix64(seed))),
      matchSeed(seed)
{
  players.reserve(22);
  playerStats.reserve(36);
  events.reserve(256);
  computeLevel(home_lineup, away_lineup);
  initializePlayers(home_lineup, true);
  initializePlayers(away_lineup, false);
  slotAnchors = formations;
  resolveTactics(true);
  resolveTactics(false);
  homeBench = home_lineup.getReserves();
  awayBench = away_lineup.getReserves();
  designations = {home_lineup.getDesignations(), away_lineup.getDesignations()};
  for (const MatchPlayer& player : players) squad.push_back(player.player);
  squad.insert(squad.end(), homeBench.begin(), homeBench.end());
  squad.insert(squad.end(), awayBench.begin(), awayBench.end());
  std::erase(homeBench, nullptr);
  std::erase(awayBench, nullptr);

  refreshTargetBlends();
  computeUnderdogShares();
  refreshEffectiveSliders();
  assignMarks();
  refereeDraw = gaussian(incidentRng);
  refereeStrictness = drawRefereeStrictness();

  setupKickOff(true);
  updateTeamPhases();
  logEvent(MatchEventType::KICK_OFF);
  describePendingEvents();
}

float MatchEngine::drawRefereeStrictness() const
{
  using D = MatchTuning::Discipline;
  const float mean = matchContext.refereeStrictnessMean;
  return std::clamp(mean + refereeDraw * matchContext.refereeStrictnessSd,
                    D::MIN_STRICTNESS * mean, D::MAX_STRICTNESS * mean);
}

void MatchEngine::setMatchContext(const MatchContext& context)
{
  if (stepCounter != 0) return;
  const auto finite = [](float value, float fallback)
  { return std::isfinite(value) ? value : fallback; };
  const MatchContext defaults;
  matchContext.goalRateScale =
      std::clamp(finite(context.goalRateScale, 1.0f),
                 MatchTuning::Context::MIN_GOAL_RATE_SCALE,
                 MatchTuning::Context::MAX_GOAL_RATE_SCALE);
  matchContext.refereeStrictnessMean =
      std::clamp(finite(context.refereeStrictnessMean, 1.0f),
                 MatchTuning::Context::MIN_REFEREE_STRICTNESS,
                 MatchTuning::Context::MAX_REFEREE_STRICTNESS);
  matchContext.refereeStrictnessSd = std::clamp(
      finite(context.refereeStrictnessSd, defaults.refereeStrictnessSd), 0.0f,
      MatchTuning::Context::MAX_REFEREE_SD);
  matchContext.homeAdvantageScale =
      std::clamp(finite(context.homeAdvantageScale, 1.0f), 0.0f,
                 MatchTuning::Context::MAX_HOME_ADVANTAGE_SCALE);
  finishingPrecision = std::pow(matchContext.goalRateScale,
                                MatchTuning::Context::FINISHING_EXPONENT);
  refereeStrictness = drawRefereeStrictness();
  // The home crowd's lift is part of the loaded attributes.
  for (MatchPlayer& player : players)
    if (player.player && player.isHomeTeam)
      loadAttributes(player, player.player);
  ++inputRevision;
}

void MatchEngine::initializePlayers(const Lineup& lineup, bool isHomeTeam)
{
  const auto addPlayer = [&](const Player* player, Vector2F position,
                             std::vector<MatchPlayer>& destination)
  {
    if (!player) return;
    if (!isHomeTeam) position.x = 1.0f - position.x;

    MatchPlayer matchPlayer;
    matchPlayer.player = player;
    matchPlayer.isHomeTeam = isHomeTeam;
    matchPlayer.position = position;
    matchPlayer.basePosition = position;
    matchPlayer.movementTarget = position;
    matchPlayer.facingAngle = isHomeTeam ? 0.0f : std::numbers::pi_v<float>;
    matchPlayer.targetAngle = matchPlayer.facingAngle;
    loadAttributes(matchPlayer, player);
    matchPlayer.statsIndex = addPlayerStats(matchPlayer, true);
    destination.push_back(matchPlayer);
  };

  addPlayer(
      lineup.getGoalkeeper(),
      {MatchTuning::Pitch::LINEUP_GOALKEEPER_X, MatchTuning::Pitch::CENTRE},
      players);
  if (lineup.getGoalkeeper() && !players.empty())
    players.back().isGoalkeeper = true;
  auto& formation = formations[isHomeTeam ? 0 : 1];
  for (const auto& positioned : lineup.getOutfieldPlayers())
  {
    const std::size_t before = players.size();
    addPlayer(positioned.player, positioned.position, players);
    if (players.size() > before)
      players.back().formationSlot = static_cast<std::int8_t>(formation.size());
    formation.push_back(positioned.position);
  }
}

void MatchEngine::loadAttributes(MatchPlayer& matchPlayer,
                                 const Player* player) const
{
  // Cache the named career stats once, after match-level normalization. The
  // hot movement/decision loops read these fields, never a string-keyed map.
  // A new simulation attribute needs an explicit consumer as well as a field
  // here; adding a name to stats_config.json alone creates no new behavior.
  matchPlayer.pace = attribute(player, "Pace");
  matchPlayer.shooting = attribute(player, "Shooting");
  matchPlayer.passing = attribute(player, "Passing");
  matchPlayer.dribbling = attribute(player, "Dribbling");
  matchPlayer.defending = attribute(player, "Defending");
  matchPlayer.goalkeeping = attribute(player, "Goalkeeping");
  matchPlayer.physicality = attribute(player, "Physicality");
  matchPlayer.endurance = attribute(player, "Stamina");
  matchPlayer.vision = attribute(player, "Vision");
  if (matchPlayer.isHomeTeam)
  {
    const float BONUS = MatchTuning::Rules::HOME_ATTRIBUTE_BONUS *
                        matchContext.homeAdvantageScale;
    for (float* value : {&matchPlayer.shooting, &matchPlayer.passing,
                         &matchPlayer.dribbling, &matchPlayer.defending,
                         &matchPlayer.goalkeeping, &matchPlayer.vision})
      *value = std::min(1.0f, *value + BONUS);
  }
  matchPlayer.heightMetres =
      player ? MatchRules::playerHeightMetres(player->getHeight())
             : MatchTuning::Units::DEFAULT_PLAYER_HEIGHT_METRES;
  using P = MatchTuning::Player;
  matchPlayer.maxSpeed =
      P::TOP_SPEED_BASE + matchPlayer.pace * P::TOP_SPEED_PACE;
  matchPlayer.acceleration =
      P::ACCELERATION_BASE + matchPlayer.pace * P::ACCELERATION_PACE +
      matchPlayer.physicality * P::ACCELERATION_PHYSICALITY;
  matchPlayer.braking =
      P::BRAKING_BASE + matchPlayer.physicality * P::BRAKING_PHYSICALITY;
}

std::size_t MatchEngine::addPlayerStats(const MatchPlayer& matchPlayer,
                                        bool started)
{
  PlayerMatchStats entry;
  entry.playerId = matchPlayer.player ? matchPlayer.player->getId() : 0;
  entry.isHomeTeam = matchPlayer.isHomeTeam;
  entry.role =
      matchPlayer.player ? matchPlayer.player->getRole() : PlayerRole::UNKNOWN;
  entry.started = started;
  entry.substitutedOn = !started;
  entry.condition = matchPlayer.stamina;
  playerStats.push_back(entry);
  return playerStats.size() - 1;
}

PlayerMatchStats& MatchEngine::statsOf(const MatchPlayer& matchPlayer)
{
  return playerStats[matchPlayer.statsIndex];
}

const PlayerMatchStats* MatchEngine::findPlayerStats(PlayerID playerId) const
{
  const auto found = std::ranges::find_if(
      playerStats, [playerId](const PlayerMatchStats& entry)
      { return entry.playerId == playerId; });
  return found == playerStats.end() ? nullptr : &*found;
}

std::optional<float> MatchEngine::getPlayerCondition(PlayerID playerId) const
{
  for (const auto& player : players)
  {
    if (player.player && player.player->getId() == playerId)
      return player.stamina;
  }
  if (const PlayerMatchStats* entry = findPlayerStats(playerId))
    return entry->condition;
  return std::nullopt;
}

bool MatchEngine::setPlayerCondition(PlayerID playerId, float condition)
{
  if (state != MatchState::KICK_OFF || elapsedMatchMinutes > 0.0f) return false;
  const float clamped =
      std::clamp(condition, MatchTuning::Player::MINIMUM_STAMINA, 1.0f);
  for (auto& player : players)
  {
    if (player.player && player.player->getId() == playerId)
    {
      player.stamina = clamped;
      statsOf(player).condition = player.stamina;
      ++inputRevision;
      return true;
    }
  }
  for (const auto* bench : {&homeBench, &awayBench})
  {
    if (std::ranges::none_of(*bench, [playerId](const Player* candidate)
                             { return candidate->getId() == playerId; }))
      continue;
    std::erase_if(benchConditions, [playerId](const auto& entry)
                  { return entry.first == playerId; });
    benchConditions.emplace_back(playerId, clamped);
    ++inputRevision;
    return true;
  }
  return false;
}

void MatchEngine::setTacticalFamiliarity(bool homeTeam, float familiarity)
{
  if (!std::isfinite(familiarity)) return;
  (homeTeam ? homeFamiliarity : awayFamiliarity) =
      std::clamp(familiarity, 0.0f, 1.0f);
  refreshTargetBlends();
  ++inputRevision;
}

void MatchEngine::refreshTargetBlends()
{
  using P = MatchTuning::Player;
  for (std::size_t team = 0; team < 2; ++team)
  {
    const float familiarity = effectiveFamiliarity(team);
    const float response =
        1.0f - P::FAMILIARITY_RESPONSE_LOSS * (1.0f - familiarity);
    targetBlends[team * 2] =
        1.0f - std::exp(-P::TACTICAL_TARGET_RESPONSE_PER_SECOND * response *
                        stepSeconds());
    targetBlends[team * 2 + 1] =
        1.0f - std::exp(-P::URGENT_TARGET_RESPONSE_PER_SECOND * response *
                        stepSeconds());
  }
}

void MatchEngine::setStepTicks(std::uint32_t ticks)
{
  ticks = std::max(ticks, 1U);
  if (ticks == stepTicks) return;
  stepTicks = ticks;
  refreshTargetBlends();
}

float MatchEngine::stepSeconds() const
{
  return MatchTuning::Timing::FIXED_STEP_SECONDS *
         static_cast<float>(stepTicks);
}

bool MatchEngine::periodElapsed(std::uint64_t ticks) const
{
  if (stepCounter < stepTicks) return true;
  return stepCounter / ticks != (stepCounter - stepTicks) / ticks;
}

float MatchEngine::familiarityOf(const MatchPlayer& player) const
{
  return effectiveFamiliarity(player.isHomeTeam ? 0 : 1);
}

float MatchEngine::effectiveFamiliarity(std::size_t team) const
{
  const float drilled = team == 0 ? homeFamiliarity : awayFamiliarity;
  if (reshapeSecondsRemaining[team] <= 0.0f) return drilled;
  return std::max(
      0.0f, drilled - reshapeFamiliarityCost[team] *
                          reshapeSecondsRemaining[team] /
                          MatchTuning::Touchline::RESHAPE_RECOVERY_SECONDS);
}

void MatchEngine::setStrategy(bool homeTeam, const Strategy& strategy)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::STRATEGY;
  command.homeTeam = homeTeam;
  command.strategy = strategy;
  recordCommand(command);
}

void MatchEngine::applyShout(bool homeTeam, MatchShout shout)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::SHOUT;
  command.homeTeam = homeTeam;
  command.shout = shout;
  recordCommand(command);
}

void MatchEngine::startShout(bool homeTeam, MatchShout shout)
{
  ShoutState& current = shouts[homeTeam ? 0 : 1];
  current.shout = shout;
  current.remainingSeconds = MatchTuning::Touchline::SHOUT_DURATION_SECONDS;
  current.impact = 1.0f / (1.0f + current.repeats);
  current.repeats += 1.0f;
  refreshEffectiveSliders();
  ++inputRevision;
}

float MatchEngine::getShoutStrength(bool homeTeam) const
{
  const ShoutState& shout = shouts[homeTeam ? 0 : 1];
  return std::clamp(shout.remainingSeconds /
                        MatchTuning::Touchline::SHOUT_DURATION_SECONDS,
                    0.0f, 1.0f) *
         shout.impact;
}

std::optional<MatchShout> MatchEngine::getActiveShout(bool homeTeam) const
{
  const ShoutState& shout = shouts[homeTeam ? 0 : 1];
  if (shout.remainingSeconds <= 0.0f) return std::nullopt;
  return shout.shout;
}

std::vector<Vector2F> MatchEngine::getFormation(bool homeTeam) const
{
  return formations[homeTeam ? 0 : 1];
}

bool MatchEngine::setFormation(bool homeTeam, std::span<const Vector2F> shape)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::FORMATION;
  command.homeTeam = homeTeam;
  command.formation.assign(shape.begin(), shape.end());
  return recordCommand(command);
}

std::optional<std::size_t> MatchEngine::getFormationSlot(
    PlayerID playerId) const
{
  for (const MatchPlayer& player : players)
  {
    if (active(player) && !player.isGoalkeeper && player.formationSlot >= 0 &&
        player.player->getId() == playerId)
      return static_cast<std::size_t>(player.formationSlot);
  }
  return std::nullopt;
}

bool MatchEngine::movePlayerToSlot(PlayerID playerId, std::size_t slot)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::MOVE_TO_SLOT;
  command.player = playerId;
  command.slot = slot;
  for (const MatchPlayer& player : players)
    if (player.player && player.player->getId() == playerId)
      command.homeTeam = player.isHomeTeam;
  return recordCommand(command);
}

void MatchEngine::loadCommandReplay(std::vector<MatchCommandRecord> log)
{
  std::stable_sort(
      log.begin(), log.end(),
      [](const MatchCommandRecord& first, const MatchCommandRecord& second)
      { return first.step < second.step; });
  commandLog = std::move(log);
  commandCursor = 0;
  ++inputRevision;
}

bool MatchEngine::recordCommand(const MatchCommandRecord& command)
{
  if (!executeCommand(command)) return false;
  // A live change ends any replay still pending.
  commandLog.resize(commandCursor);
  commandLog.push_back(command);
  commandLog.back().step = stepCounter;
  commandCursor = commandLog.size();
  ++inputRevision;
  return true;
}

void MatchEngine::applyDueCommands()
{
  while (commandCursor < commandLog.size() &&
         commandLog[commandCursor].step <= stepCounter)
    executeCommand(commandLog[commandCursor++]);
}

bool MatchEngine::executeCommand(const MatchCommandRecord& command)
{
  const std::size_t team = command.homeTeam ? 0 : 1;
  switch (command.type)
  {
    case MatchCommandType::TEAM_TALK:
      if (command.talkHalf < 1 || command.talkHalf > 2 ||
          !std::isfinite(command.talkModifier))
        return false;
      teamTalks[team][static_cast<std::size_t>(command.talkHalf - 1)] =
          std::clamp(command.talkModifier,
                     -MatchTuning::Touchline::MAX_TEAM_TALK_MODIFIER,
                     MatchTuning::Touchline::MAX_TEAM_TALK_MODIFIER);
      ++inputRevision;
      return true;
    case MatchCommandType::STRATEGY:
      (command.homeTeam ? homeStrategy : awayStrategy) = command.strategy;
      resolveTactics(command.homeTeam);
      refreshEffectiveSliders();
      ++inputRevision;
      return true;
    case MatchCommandType::SHOUT:
      startShout(command.homeTeam, command.shout);
      return true;
    case MatchCommandType::FORMATION:
    {
      auto& formation = formations[team];
      if (command.formation.size() != formation.size() ||
          state == MatchState::FULL_TIME)
        return false;
      using Pitch = MatchTuning::Pitch;
      float moved = 0.0f;
      std::vector<Vector2F> shape;
      shape.reserve(formation.size());
      for (std::size_t slot = 0; slot < formation.size(); ++slot)
      {
        const Vector2F wanted = command.formation[slot];
        if (!std::isfinite(wanted.x) || !std::isfinite(wanted.y)) return false;
        shape.push_back(
            {std::clamp(wanted.x, Pitch::PLAYER_MIN_X, Pitch::PLAYER_MAX_X),
             std::clamp(wanted.y, Pitch::PLAYER_MIN_Y, Pitch::PLAYER_MAX_Y)});
        moved += distance(shape.back(), formation[slot]);
      }
      formation = std::move(shape);
      using T = MatchTuning::Touchline;
      const float cost = std::min(
          T::MAX_RESHAPE_FAMILIARITY_COST,
          moved /
              static_cast<float>(std::max<std::size_t>(formation.size(), 1)) *
              T::RESHAPE_FAMILIARITY_PER_METRE);
      if (cost > 0.0f)
      {
        // A second change before the first has sunk in adds to what is left.
        reshapeFamiliarityCost[team] =
            std::min(T::MAX_RESHAPE_FAMILIARITY_COST,
                     cost + reshapeFamiliarityCost[team] *
                                reshapeSecondsRemaining[team] /
                                T::RESHAPE_RECOVERY_SECONDS);
        reshapeSecondsRemaining[team] = T::RESHAPE_RECOVERY_SECONDS;
        refreshTargetBlends();
      }
      placeFormation(command.homeTeam);
      ++inputRevision;
      return true;
    }
    case MatchCommandType::MOVE_TO_SLOT:
    {
      if (!command.slot || *command.slot >= formations[team].size() ||
          state == MatchState::FULL_TIME)
        return false;
      const auto mover = std::ranges::find_if(
          players,
          [&](const MatchPlayer& player)
          {
            return active(player) && !player.isGoalkeeper &&
                   player.formationSlot >= 0 &&
                   player.isHomeTeam == command.homeTeam &&
                   player.player->getId() == command.player;
          });
      if (mover == players.end()) return false;
      const auto slot = static_cast<std::int8_t>(*command.slot);
      for (MatchPlayer& occupant : players)
      {
        if (active(occupant) && !occupant.isGoalkeeper &&
            occupant.isHomeTeam == command.homeTeam &&
            occupant.formationSlot == slot)
          occupant.formationSlot = mover->formationSlot;
      }
      mover->formationSlot = slot;
      placeFormation(command.homeTeam);
      ++inputRevision;
      return true;
    }
    case MatchCommandType::SUBSTITUTION:
    {
      const auto& bench = command.homeTeam ? homeBench : awayBench;
      const auto incoming = std::ranges::find_if(
          bench, [&](const Player* player)
          { return player && player->getId() == command.incoming; });
      if (incoming == bench.end() ||
          (command.slot && *command.slot >= formations[team].size()))
        return false;
      const Player* inPlayer = *incoming;
      const auto outgoing = std::ranges::find_if(
          players,
          [&](const MatchPlayer& player)
          {
            return active(player) && player.player->getId() == command.player;
          });
      if (outgoing == players.end() ||
          outgoing->isHomeTeam != command.homeTeam ||
          !canSubstitute(command.homeTeam))
        return false;
      // A change made while the ball is live stops play for it; several
      // changes made together share that stoppage (and window).
      if (state == MatchState::PLAYING && manualSubstitutionStep != stepCounter)
      {
        beginStoppage();
        manualSubstitutionStep = stepCounter;
      }
      if (!canSubstitute(outgoing->isHomeTeam)) return false;
      ++inputRevision;
      const bool changed =
          performSubstitution(*outgoing, inPlayer, SubstitutionReason::MANUAL);
      if (changed && command.slot && !outgoing->isGoalkeeper &&
          outgoing->formationSlot >= 0)
      {
        MatchCommandRecord move;
        move.type = MatchCommandType::MOVE_TO_SLOT;
        move.homeTeam = command.homeTeam;
        move.player = inPlayer->getId();
        move.slot = command.slot;
        executeCommand(move);
      }
      describePendingEvents();
      return changed;
    }
  }
  return false;
}

void MatchEngine::placeFormation(bool homeTeam)
{
  const auto& formation = formations[homeTeam ? 0 : 1];
  int sideSize = 0;
  int onPitch = 0;
  for (const MatchPlayer& player : players)
  {
    if (!player.player || player.isHomeTeam != homeTeam) continue;
    ++sideSize;
    if (active(player)) ++onPitch;
  }
  const float drop = static_cast<float>(sideSize - onPitch) *
                     MatchTuning::Rules::SHORT_HANDED_DROP;
  for (MatchPlayer& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam ||
        player.isGoalkeeper || player.formationSlot < 0 ||
        static_cast<std::size_t>(player.formationSlot) >= formation.size())
      continue;
    const Vector2F spot =
        formation[static_cast<std::size_t>(player.formationSlot)];
    const float x = std::clamp(spot.x - drop, MatchTuning::Pitch::PLAYER_MIN_X,
                               MatchTuning::Pitch::PLAYER_MAX_X);
    player.basePosition = {homeTeam ? x : 1.0f - x, spot.y};
  }
}

StrategySliders MatchEngine::getEffectiveSliders(bool homeTeam) const
{
  return effectiveSliders[homeTeam ? 0 : 1];
}

void MatchEngine::refreshEffectiveSliders()
{
  effectiveSliders = {computeEffectiveSliders(true),
                      computeEffectiveSliders(false)};
}

void MatchEngine::computeUnderdogShares()
{
  // Mean outfield quality of the starting elevens, without the home lift.
  std::array<float, 2> quality{};
  std::array<int, 2> counted{};
  for (const MatchPlayer& player : players)
  {
    if (!player.player || player.isGoalkeeper) continue;
    const std::size_t side = player.isHomeTeam ? 0 : 1;
    float sum = 0.0f;
    for (const std::string_view name :
         {"Pace", "Shooting", "Passing", "Dribbling", "Defending",
          "Physicality", "Vision"})
      sum += attribute(player.player, name);
    quality[side] += sum / 7.0f;
    ++counted[side];
  }
  if (counted[0] == 0 || counted[1] == 0) return;
  const float gap = quality[1] / static_cast<float>(counted[1]) -
                    quality[0] / static_cast<float>(counted[0]);
  using T = MatchTuning::Touchline;
  const auto share = [](float deficit)
  {
    return std::clamp((deficit - T::UNDERDOG_GAP_START) / T::UNDERDOG_GAP_RANGE,
                      0.0f, 1.0f);
  };
  underdogShares = {share(gap), share(-gap)};
}

StrategySliders MatchEngine::computeEffectiveSliders(bool homeTeam) const
{
  StrategySliders sliders =
      (homeTeam ? homeStrategy : awayStrategy).getSliders();
  // Score effects: a side in front sits deeper and takes fewer risks, a side
  // behind pushes on, more so as the match runs out.
  using T = MatchTuning::Touchline;
  const int lead =
      std::clamp(homeTeam ? homeScore - awayScore : awayScore - homeScore,
                 -T::SCORE_EFFECT_MAX_GOALS, T::SCORE_EFFECT_MAX_GOALS);
  const auto clampSlider = [](float& value)
  { value = std::clamp(value, 0.0f, 1.0f); };
  if (lead != 0)
  {
    const float urgency =
        static_cast<float>(lead) *
        (T::SCORE_EFFECT_BASE +
         (1.0f - T::SCORE_EFFECT_BASE) *
             std::min(1.0f,
                      matchTimeMinutes /
                          (2.0f * MatchTuning::Timing::HALF_TIME_MINUTE)));
    // Only a side chasing the game commits more men forward; one in front
    // keeps its passing but stops pressing and taking risks.
    if (lead < 0) sliders.offensiveBias -= urgency * T::SCORE_EFFECT_OFFENSIVE;
    if (lead >= MatchTuning::Decision::COMFORTABLE_LEAD)
    {
      sliders.offensiveBias -=
          urgency / static_cast<float>(lead) *
          static_cast<float>(lead - MatchTuning::Decision::COMFORTABLE_LEAD +
                             1) *
          T::GAME_MANAGEMENT_OFFENSIVE;
    }
    sliders.riskTaking -= urgency * T::SCORE_EFFECT_RISK;
    sliders.pressing -= urgency * T::SCORE_EFFECT_PRESSING;
    sliders.compactness += urgency * T::SCORE_EFFECT_COMPACTNESS;
    clampSlider(sliders.pressing);
    clampSlider(sliders.riskTaking);
    clampSlider(sliders.offensiveBias);
    clampSlider(sliders.compactness);
  }
  if (const float underdog = underdogShares[homeTeam ? 0 : 1]; underdog > 0.0f)
  {
    sliders.pressing -= underdog * T::UNDERDOG_PRESSING;
    sliders.compactness += underdog * T::UNDERDOG_COMPACTNESS;
    sliders.offensiveBias -= underdog * T::UNDERDOG_OFFENSIVE;
    clampSlider(sliders.pressing);
    clampSlider(sliders.offensiveBias);
    clampSlider(sliders.compactness);
  }
  const float strength = getShoutStrength(homeTeam);
  if (strength <= 0.0f) return sliders;
  const float step = MatchTuning::Touchline::SHOUT_SLIDER_STEP * strength;
  switch (shouts[homeTeam ? 0 : 1].shout)
  {
    case MatchShout::PUSH_HIGHER:
      sliders.offensiveBias += step;
      sliders.pressing += step * 0.5f;
      break;
    case MatchShout::DROP_DEEPER:
      sliders.offensiveBias -= step;
      sliders.pressing -= step * 0.75f;
      sliders.compactness += step * 0.75f;
      break;
    case MatchShout::PRESS_MORE:
      sliders.pressing += step * 1.25f;
      break;
    case MatchShout::CALM_DOWN:
      sliders.riskTaking -= step;
      sliders.pressing -= step * 0.25f;
      break;
    case MatchShout::WORK_BALL_INTO_BOX:
      sliders.riskTaking -= step * 0.25f;
      break;
    case MatchShout::SHOOT_ON_SIGHT:
      sliders.riskTaking += step * 0.5f;
      break;
    case MatchShout::ENCOURAGE:
      break;
    case MatchShout::STAND_OFF:
      sliders.pressing -= step * 1.25f;
      sliders.compactness += step * 0.5f;
      break;
    case MatchShout::DEMAND_MORE:
      sliders.pressing += step * 0.5f;
      sliders.offensiveBias += step * 0.5f;
      break;
    case MatchShout::HIT_ON_COUNTER:
      sliders.pressing -= step * 0.5f;
      sliders.compactness += step * 0.5f;
      sliders.riskTaking += step;
      break;
    case MatchShout::KEEP_POSSESSION:
      sliders.riskTaking -= step;
      sliders.offensiveBias -= step * 0.5f;
      sliders.widthUsage += step * 0.5f;
      break;
  }
  clampSlider(sliders.pressing);
  clampSlider(sliders.riskTaking);
  clampSlider(sliders.offensiveBias);
  clampSlider(sliders.widthUsage);
  clampSlider(sliders.compactness);
  return sliders;
}

float MatchEngine::shoutShotBias(bool homeTeam) const
{
  const MatchShout shout = shouts[homeTeam ? 0 : 1].shout;
  const float bias = shout == MatchShout::SHOOT_ON_SIGHT       ? 1.0f
                     : shout == MatchShout::WORK_BALL_INTO_BOX ? -1.0f
                                                               : 0.0f;
  return bias * MatchTuning::Touchline::SHOUT_SHOT_BIAS *
         getShoutStrength(homeTeam);
}

[[gnu::always_inline]] inline float MatchEngine::shoutWorkRate(
    bool homeTeam) const
{
  const MatchShout shout = shouts[homeTeam ? 0 : 1].shout;
  return shout == MatchShout::ENCOURAGE || shout == MatchShout::DEMAND_MORE
             ? MatchTuning::Touchline::ENCOURAGE_WORK_RATE *
                   getShoutStrength(homeTeam)
             : 0.0f;
}

void MatchEngine::setTeamTalkModifier(bool homeTeam, int half, float modifier)
{
  // Logged like every touchline change, so a replay of the command log
  // reproduces a match a team talk played its part in.
  MatchCommandRecord command;
  command.type = MatchCommandType::TEAM_TALK;
  command.homeTeam = homeTeam;
  command.talkHalf = half;
  command.talkModifier = modifier;
  recordCommand(command);
}

float MatchEngine::getTeamTalkModifier(bool homeTeam, int half) const
{
  if (half < 1 || half > 2) return 0.0f;
  return teamTalks[homeTeam ? 0 : 1][static_cast<std::size_t>(half - 1)];
}

[[gnu::always_inline]] inline float MatchEngine::talkOf(bool homeTeam) const
{
  // A talk lifts (or deflates) the side for the first minutes of its half
  // and has worn off well before the break; extra time has none.
  if (period < 1 || period > 2) return 0.0f;
  const float talk =
      teamTalks[homeTeam ? 0 : 1][static_cast<std::size_t>(period - 1)];
  if (talk == 0.0f) return 0.0f;
  namespace T = TacticsTuning;
  const float minutes =
      matchTimeMinutes -
      (period == 2 ? MatchTuning::Timing::HALF_TIME_MINUTE : 0.0f);
  return talk *
         std::clamp((T::TALK_FADE_END_MINUTES - minutes) /
                        (T::TALK_FADE_END_MINUTES - T::TALK_FULL_MINUTES),
                    0.0f, 1.0f);
}

float MatchEngine::teamEdge(bool homeTeam) const
{
  using R = MatchTuning::Rules;
  int balance = 0;
  for (const auto& player : players)
    if (active(player)) balance += player.isHomeTeam == homeTeam ? 1 : -1;
  balance = std::clamp(balance, -R::MAX_NUMERICAL_EDGE_PLAYERS,
                       R::MAX_NUMERICAL_EDGE_PLAYERS);
  return (homeTeam ? R::HOME_EXECUTION_BONUS * matchContext.homeAdvantageScale
                   : 0.0f) +
         talkOf(homeTeam) +
         static_cast<float>(balance) * R::NUMERICAL_EDGE_PER_PLAYER;
}

void MatchEngine::runAiTouchline(bool homeTeam)
{
  using T = MatchTuning::Touchline;
  if (period < 2 || getShoutStrength(homeTeam) > 0.0f) return;
  const int lead = homeTeam ? homeScore - awayScore : awayScore - homeScore;
  if (lead < 0 && matchTimeMinutes >= T::AI_SHOOT_ON_SIGHT_MINUTE)
    startShout(homeTeam, MatchShout::SHOOT_ON_SIGHT);
  else if (lead < 0 && matchTimeMinutes >= T::AI_CHASE_MINUTE)
    startShout(homeTeam, MatchShout::PUSH_HIGHER);
  else if (lead > 0 && matchTimeMinutes >= T::AI_PROTECT_MINUTE)
    startShout(homeTeam, MatchShout::DROP_DEEPER);
}

void MatchEngine::refreshRatings()
{
  for (auto& entry : playerStats)
  {
    const int goalDifference =
        entry.isHomeTeam ? homeScore - awayScore : awayScore - homeScore;
    const bool goalkeeper = entry.role == PlayerRole::GK;
    const bool defender = entry.role == PlayerRole::CB ||
                          entry.role == PlayerRole::LB ||
                          entry.role == PlayerRole::RB;
    entry.rating = MatchRules::computeMatchRating(entry, goalkeeper, defender,
                                                  goalDifference);
  }
}

namespace
{
/** Counter-based noise keyed by (seed, tick, salt, key), uniform in [-1, 1). */
float counterNoise(std::uint32_t seed, std::uint64_t tick, std::uint32_t salt,
                   std::uint32_t key)
{
  const std::uint64_t mixed =
      splitMix64((static_cast<std::uint64_t>(seed) << 32U) ^ tick ^
                 (static_cast<std::uint64_t>(salt) << 48U) ^
                 (static_cast<std::uint64_t>(key) << 16U));
  return static_cast<float>(mixed >> 40U) / static_cast<float>(1U << 23U) -
         1.0f;
}
}  // namespace

float MatchEngine::hashNoise(std::uint32_t salt, std::uint32_t key) const
{
  return counterNoise(matchSeed, stepCounter, salt, key);
}

float MatchEngine::epochNoise(std::uint32_t salt, std::uint32_t key,
                              std::uint32_t epochSteps) const
{
  return counterNoise(matchSeed, stepCounter / std::max(epochSteps, 1U), salt,
                      key);
}

float MatchEngine::executionErrorScale(const MatchPlayer& player) const
{
  // Tired players lose precision, and a home crowd lifts the home side a
  // little: both scale the technical error of passes and shots.
  const float fatigue = 1.0f + (1.0f - player.stamina) *
                                   MatchTuning::Fatigue::TECHNIQUE_ERROR_GAIN;
  return fatigue * std::max(0.5f, 1.0f - teamEdge(player.isHomeTeam)) *
         (1.0f +
          weakFootPressure(player) * TacticsTuning::WEAK_FOOT_ERROR_GAIN);
}

void MatchEngine::update(float deltaTime)
{
  lastUpdateStepCount = 0;
  if (state == MatchState::FULL_TIME || !std::isfinite(deltaTime) ||
      deltaTime <= 0.0f)
  {
    return;
  }

  constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
  accumulator +=
      std::min(deltaTime, MatchTuning::Timing::MAX_FRAME_DELTA_SECONDS);
  while (accumulator + EPSILON >= STEP && state != MatchState::FULL_TIME &&
         lastUpdateStepCount < MatchTuning::Timing::MAX_FIXED_STEPS_PER_UPDATE)
  {
    simulateStep(STEP);
    accumulator -= STEP;
    ++lastUpdateStepCount;
  }
  if (lastUpdateStepCount == MatchTuning::Timing::MAX_FIXED_STEPS_PER_UPDATE &&
      accumulator >= STEP)
  {
    droppedSimulationSteps +=
        static_cast<std::uint64_t>(std::floor(accumulator / STEP));
    accumulator = std::fmod(accumulator, STEP);
  }
}

float MatchEngine::advance(float seconds)
{
  lastUpdateStepCount = 0;
  if (state == MatchState::FULL_TIME || !std::isfinite(seconds) ||
      seconds <= 0.0f)
  {
    return 0.0f;
  }
  constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
  const auto steps =
      static_cast<std::int64_t>(std::ceil(seconds / STEP - EPSILON));
  std::int64_t done = 0;
  for (; done < steps && state != MatchState::FULL_TIME; ++done)
    simulateStep(STEP);
  lastUpdateStepCount = static_cast<int>(
      std::min<std::int64_t>(done, std::numeric_limits<int>::max()));
  return static_cast<float>(done) * STEP;
}

void MatchEngine::simulateToEnd() { simulateToEnd(MatchFidelity::FULL); }

void MatchEngine::simulateToEnd(MatchFidelity fidelity)
{
  const bool background =
      fidelity == MatchFidelity::BACKGROUND && !controlledIndex;
  // Nobody reads the analytics of an unwatched fixture.
  if (background) tracker.setEnabled(false);
  // A match lasts at most ~7,200 simulated seconds (both halves with the
  // maximum added time and overrun); the bound only guards against bugs.
  constexpr std::int64_t MAX_STEPS = 120'000;
  for (std::int64_t step = 0;
       step < MAX_STEPS && state != MatchState::FULL_TIME; ++step)
  {
    if (background) setStepTicks(backgroundStepTicks());
    simulateStep(stepSeconds());
  }
  setStepTicks(1U);
}

std::uint32_t MatchEngine::backgroundStepTicks() const
{
  switch (state)
  {
    case MatchState::THROW_IN:
    case MatchState::GOAL_KICK:
    case MatchState::CORNER_KICK:
    case MatchState::FREE_KICK:
    case MatchState::PENALTY:
      break;
    default:
      return 1U;
  }
  // Land on the restart instead of overshooting its delay.
  const float remaining = std::ceil(
      setPieceTimer / MatchTuning::Timing::FIXED_STEP_SECONDS - 0.001f);
  if (!(remaining > 1.0f)) return 1U;
  return std::min(static_cast<std::uint32_t>(remaining),
                  MatchTuning::Timing::BACKGROUND_STOPPAGE_TICKS);
}

double MatchEngine::getSimulatedSeconds() const
{
  return static_cast<double>(stepCounter) * STEP_SECONDS;
}

void MatchEngine::setPlaybackMode(MatchPlaybackMode mode)
{
  playbackMode = mode;
  scheduledHighlight.reset();
}

void MatchEngine::setPlaybackSpeed(float simulatedSecondsPerWallSecond)
{
  if (!std::isfinite(simulatedSecondsPerWallSecond)) return;
  playbackSpeed = std::clamp(simulatedSecondsPerWallSecond,
                             MatchTuning::Playback::MIN_SPEED,
                             MatchTuning::Playback::MAX_SPEED);
}

void MatchEngine::setHighlightPlaybackSpeed(float simulatedSecondsPerWallSecond)
{
  if (!std::isfinite(simulatedSecondsPerWallSecond)) return;
  highlightSpeed = std::clamp(simulatedSecondsPerWallSecond,
                              MatchTuning::Playback::MIN_SPEED,
                              MatchTuning::Playback::MAX_SPEED);
}

bool MatchEngine::advancePlayback(float wallSeconds)
{
  if (state == MatchState::FULL_TIME || !std::isfinite(wallSeconds) ||
      wallSeconds <= 0.0f)
  {
    return false;
  }
  if (playbackMode == MatchPlaybackMode::FULL_MATCH)
  {
    update(wallSeconds * playbackSpeed);
    return false;
  }

  // A manual change alters the future, so an old prediction is void.
  if (scheduledHighlight && scheduledRevision != inputRevision)
    scheduledHighlight.reset();
  const double now = getSimulatedSeconds();
  if (scheduledHighlight && now >= scheduledHighlight->endSeconds)
    scheduledHighlight.reset();
  if (!scheduledHighlight)
  {
    scheduledHighlight = predictNextHighlight();
    scheduledRevision = inputRevision;
    const double target = scheduledHighlight ? scheduledHighlight->startSeconds
                                             : now + PREDICTION_HORIZON;
    if (target - now < STEP_SECONDS) return false;
    // The skip never jumps across a break (half-time, the breaks around
    // extra time, the shootout): it stops where one begins so the viewer
    // sees it, and the next call looks ahead again from there. The steps
    // are the same as without the stop, so the match does not change.
    const auto steps = static_cast<std::int64_t>(std::ceil(
        (target - now) / STEP_SECONDS - static_cast<double>(EPSILON)));
    const auto inBreak = [this]
    {
      return state == MatchState::HALF_TIME ||
             state == MatchState::PENALTY_SHOOTOUT;
    };
    std::int64_t done = 0;
    while (done < steps && state != MatchState::FULL_TIME)
    {
      const bool wasInBreak = inBreak();
      simulateStep(MatchTuning::Timing::FIXED_STEP_SECONDS);
      ++done;
      if (!wasInBreak && inBreak())
      {
        scheduledHighlight.reset();
        break;
      }
    }
    lastUpdateStepCount = static_cast<int>(
        std::min<std::int64_t>(done, std::numeric_limits<int>::max()));
    accumulator = 0.0f;
    return true;
  }
  update(wallSeconds * highlightSpeed);
  return false;
}

bool MatchEngine::isInHighlight() const
{
  const double now = getSimulatedSeconds();
  if (scheduledHighlight)
  {
    return now >= scheduledHighlight->startSeconds &&
           now <= scheduledHighlight->endSeconds;
  }
  return !highlights.empty() && now >= highlights.back().startSeconds &&
         now <= highlights.back().endSeconds;
}

std::optional<MatchHighlight> MatchEngine::predictNextHighlight(
    float horizonSeconds) const
{
  if (state == MatchState::FULL_TIME || !(horizonSeconds > 0.0f))
    return std::nullopt;
  MatchEngine lookahead(*this);
  lookahead.tracker.setEnabled(false);
  const std::uint64_t triggersBefore = lookahead.highlightTriggerCount;
  const auto steps = static_cast<std::int64_t>(
      std::ceil(horizonSeconds / MatchTuning::Timing::FIXED_STEP_SECONDS));
  // When the ball last came back into play before the trigger: a highlight
  // never opens on players standing at a dead ball.
  std::optional<double> lastRestart;
  for (std::int64_t step = 0;
       step < steps && lookahead.state != MatchState::FULL_TIME &&
       lookahead.highlightTriggerCount == triggersBefore;
       ++step)
  {
    const bool wasLive = lookahead.state == MatchState::PLAYING;
    lookahead.simulateStep(MatchTuning::Timing::FIXED_STEP_SECONDS);
    if (!wasLive && lookahead.state == MatchState::PLAYING)
      lastRestart = lookahead.getSimulatedSeconds();
  }
  if (lookahead.highlightTriggerCount == triggersBefore) return std::nullopt;
  MatchHighlight highlight;
  highlight.type = lookahead.lastTriggerType;
  highlight.triggerSeconds = lookahead.lastTriggerSeconds;
  highlight.triggerMinute = lookahead.lastTriggerMinute;
  highlight.startSeconds = std::max(getSimulatedSeconds(),
                                    highlight.triggerSeconds - HIGHLIGHT_LEAD);
  // Play resumed inside the build-up (a restart, a set piece): open just
  // before the ball is played rather than during the wait for it.
  if (lastRestart)
    highlight.startSeconds =
        std::max(highlight.startSeconds,
                 *lastRestart - MatchTuning::Playback::RESTART_LEAD_SECONDS);
  highlight.endSeconds = highlight.triggerSeconds + HIGHLIGHT_TAIL;
  return highlight;
}

void MatchEngine::captureInterpolationFrame()
{
  previousPlayerPositions.resize(players.size());
  previousPlayerFacingAngles.resize(players.size());
  for (std::size_t index = 0; index < players.size(); ++index)
  {
    previousPlayerPositions[index] = players[index].position;
    previousPlayerFacingAngles[index] = players[index].facingAngle;
  }
  previousBallPosition = ball.position;
  previousBallZ = ball.z;
}

float MatchEngine::getInterpolationAlpha() const
{
  return std::clamp(accumulator / MatchTuning::Timing::FIXED_STEP_SECONDS, 0.0f,
                    1.0f);
}

void MatchEngine::advanceClock(float dt)
{
  const float clockScale = matchClockRate();
  const float clockDelta =
      dt * clockScale / MatchTuning::Timing::SECONDS_PER_MINUTE;
  matchTimeMinutes += clockDelta;
  elapsedMatchMinutes += clockDelta;
  accumulatePlayerLoad(dt);
}

void MatchEngine::simulateStep(float dt)
{
  simulateStepBody(dt);
  describePendingEvents();
}

void MatchEngine::simulateStepBody(float dt)
{
  // This is the authoritative tick order. First apply external changes and
  // age timers, then dispatch exactly one match-state branch. In live play,
  // move bodies before resolving touches/actions, and advance a newly kicked
  // ball afterwards. Reordering these stages changes contacts and seeded RNG
  // consumption, even if each individual stage still looks correct.
  // Replayed touchline changes happen between the same two steps as live.
  if (commandCursor < commandLog.size()) applyDueCommands();
  captureInterpolationFrame();
  stepCounter += stepTicks;
  if (inputCursor < inputLog.size()) applyDueInputs();
  // A substituted or dismissed player hands control back (play mode).
  if (controlledIndex)
  {
    const MatchPlayer& controlled = players[*controlledIndex];
    if (!active(controlled) || controlled.isGoalkeeper ||
        controlled.player->getId() != controlledPlayerId)
      controlledIndex.reset();
  }
  controlActionRemaining = std::max(0.0f, controlActionRemaining - dt);
  if (controlActionRemaining <= 0.0f)
    controlInput.action = MatchInputAction::NONE;
  for (auto& player : players)
  {
    player.tackleCooldown = std::max(0.0f, player.tackleCooldown - dt);
    player.actionCooldown = std::max(0.0f, player.actionCooldown - dt);
    player.trapTimer = std::max(0.0f, player.trapTimer - dt);
    player.diveTimer = std::max(0.0f, player.diveTimer - dt);
    player.isTrapping = player.trapTimer > 0.0f;
    player.isDiving = player.diveTimer > 0.0f;
  }
  ball.kickerLockout = std::max(0.0f, ball.kickerLockout - dt);
  // The sliders only move with a fading shout or with the clock while a side
  // leads; otherwise they stay as last computed.
  const bool shouting =
      shouts[0].remainingSeconds > 0.0f || shouts[1].remainingSeconds > 0.0f;
  for (auto& shout : shouts)
  {
    shout.remainingSeconds = std::max(0.0f, shout.remainingSeconds - dt);
    shout.repeats = std::max(
        0.0f,
        shout.repeats - dt / MatchTuning::Touchline::SHOUT_REPEAT_FADE_SECONDS);
  }
  if (reshapeSecondsRemaining[0] > 0.0f || reshapeSecondsRemaining[1] > 0.0f)
  {
    for (float& remaining : reshapeSecondsRemaining)
      remaining = std::max(0.0f, remaining - dt);
    refreshTargetBlends();
  }
  if (shouting || homeScore != awayScore || slidersStale)
  {
    refreshEffectiveSliders();
    slidersStale = false;
  }
  if (state == MatchState::PLAYING)
  {
    transitionSecondsRemaining =
        std::max(0.0f, transitionSecondsRemaining - dt);
  }
  updateTeamPhases();

  // Dead-ball and break states have their own movement/clock rules. Their
  // early returns prevent celebrations, breaks and shootouts from also
  // executing open-play decisions or the ordinary period-ending logic.
  if (state == MatchState::GOAL)
  {
    // The scored ball settles in the net while the teams celebrate and walk
    // back; the clock keeps running until the kick-off.
    if (playHalfMinutes > 0)
      goalCelebrationRemaining = std::min(
          goalCelebrationRemaining, MatchTuning::Timing::PLAY_STOPPAGE_SECONDS);
    goalCelebrationRemaining = std::max(0.0F, goalCelebrationRemaining - dt);
    updateRestartMovement(dt);
    advanceClock(dt);
    if (!ball.possessedBy) updateBallInNet(dt);
    if (goalCelebrationRemaining <= 0.0f) setupKickOff(!goalScoredByHome);
    return;
  }

  if (state == MatchState::PENALTY_SHOOTOUT)
  {
    updateShootout(dt);
    return;
  }

  if (state == MatchState::HALF_TIME)
  {
    runAiSubstitutions();
    if (state == MatchState::FULL_TIME) return;
    if (playHalfMinutes > 0)
      setPieceTimer =
          std::min(setPieceTimer, MatchTuning::Timing::PLAY_STOPPAGE_SECONDS);
    setPieceTimer -= dt;
    if (setPieceTimer <= 0.0f)
    {
      ++period;
      matchTimeMinutes = MatchRules::periodStartMinute(period);
      // The home side kicks off the first half of each game, the away side
      // the second.
      setupKickOff(period % 2 == 1);
      logEvent(MatchEventType::SECOND_HALF);
    }
    return;
  }

  if (state != MatchState::PLAYING)
  {
    runAiSubstitutions();
    if (state == MatchState::FULL_TIME) return;
    if (state != MatchState::KICK_OFF) updateRestartMovement(dt);
    if (stateClockRuns(state)) advanceClock(dt);
    const float restartRate = matchClockRate();
    if (playHalfMinutes > 0)
      setPieceTimer =
          std::min(setPieceTimer,
                   MatchTuning::Timing::PLAY_STOPPAGE_SECONDS * restartRate);
    setPieceTimer -= dt * restartRate;
    if (userRestartPassRequested() || setPieceTimer <= 0.0F) completeRestart();
  }
  else
  {
    updateMovement(dt);
    advanceClock(dt);
    stats.ballInPlayMinutes +=
        dt * (matchClockRate()) / MatchTuning::Timing::SECONDS_PER_MINUTE;
    resolvePossessionAndActions(dt);
    trackPressures();
    if (!ball.possessedBy && state == MatchState::PLAYING) updateBall(dt);
    // Possession belongs to the side in control, including its passes in
    // flight and the loose balls it last played.
    if (lastControlledTeamHome)
    {
      (*lastControlledTeamHome ? homePossessionMinutes
                               : awayPossessionMinutes) += dt;
      stats.homePossession = homePossessionMinutes /
                             (homePossessionMinutes + awayPossessionMinutes) *
                             MatchTuning::Statistics::PERCENT_SCALE;
      stats.awayPossession =
          MatchTuning::Statistics::PERCENT_SCALE - stats.homePossession;
    }
    updatePendingAdvantage(dt);
    setPiecePhaseRemaining = std::max(0.0f, setPiecePhaseRemaining - dt);
    injuryCheckTimer += dt / MatchTuning::Timing::SECONDS_PER_MINUTE;
    if (injuryCheckTimer >= MatchTuning::Injury::CHECK_INTERVAL_MINUTES)
    {
      injuryCheckTimer -= MatchTuning::Injury::CHECK_INTERVAL_MINUTES;
      checkInjuries();
    }
  }
  if (state == MatchState::FULL_TIME) return;

  ratingRefreshTimer += dt;
  if (ratingRefreshTimer >= MatchTuning::Timing::RATING_REFRESH_SECONDS)
  {
    ratingRefreshTimer -= MatchTuning::Timing::RATING_REFRESH_SECONDS;
    refreshRatings();
  }
  updateMatchClock();
}

bool MatchEngine::isInAddedTime() const
{
  return state != MatchState::HALF_TIME && state != MatchState::FULL_TIME &&
         state != MatchState::PENALTY_SHOOTOUT &&
         matchTimeMinutes >= MatchRules::periodEndMinute(period);
}

void MatchEngine::updateMatchClock()
{
  if (state == MatchState::HALF_TIME || state == MatchState::FULL_TIME ||
      state == MatchState::GOAL || state == MatchState::PENALTY_SHOOTOUT)
  {
    return;
  }
  const auto half = static_cast<std::size_t>(period - 1);
  const float regulationEnd = MatchRules::periodEndMinute(period);
  if (matchTimeMinutes < regulationEnd) return;
  if (addedMinutes[half] == 0) announceAddedTime();

  // The announced added time is a minimum: stoppages during it extend it.
  const int required =
      std::max(addedMinutes[half],
               MatchRules::computeAddedMinutes(stoppageLogs[half], period));
  const float end = regulationEnd + static_cast<float>(required);
  if (matchTimeMinutes < end) return;
  const bool dangerousPhase = state == MatchState::PENALTY ||
                              state == MatchState::CORNER_KICK || ball.isShot;
  if (dangerousPhase &&
      matchTimeMinutes < end + MatchTuning::Stoppage::MAX_OVERRUN_MINUTES)
  {
    return;
  }
  endPeriod();
}

void MatchEngine::announceAddedTime()
{
  const auto half = static_cast<std::size_t>(period - 1);
  addedMinutes[half] =
      MatchRules::computeAddedMinutes(stoppageLogs[half], period);
  MatchEvent& event = logEvent(MatchEventType::ADDED_TIME);
  event.minutes = addedMinutes[half];
  event.addedMinute = 0.0f;
}

void MatchEngine::endPeriod()
{
  restartTakerIndex.reset();
  ball.possessedBy = nullptr;
  ball.velocity = {0.0f, 0.0f};
  ball.velocityZ = 0.0f;
  clearFlightState();
  pendingAdvantage.active = false;
  if (abandoned)
  {
    finishMatch();
    return;
  }
  if (period == 1)
  {
    beginBreak(MatchTuning::Timing::HALF_TIME_PAUSE_SECONDS,
               MatchTuning::Fatigue::HALF_TIME_RECOVERY);
    return;
  }
  if (period == 3)
  {
    beginBreak(MatchTuning::Timing::EXTRA_TIME_HALF_TIME_SECONDS,
               MatchTuning::Fatigue::EXTRA_TIME_HALF_TIME_RECOVERY);
    return;
  }
  if (knockout.required && tieLevel() && !shootout.started)
  {
    if (period == 2 && knockout.extraTime)
    {
      extraTimeReached = true;
      beginBreak(MatchTuning::Timing::EXTRA_TIME_BREAK_SECONDS,
                 MatchTuning::Fatigue::EXTRA_TIME_BREAK_RECOVERY);
      return;
    }
    startShootout();
    return;
  }
  finishMatch();
}

void MatchEngine::beginBreak(float seconds, float recovery)
{
  state = MatchState::HALF_TIME;
  setPieceTimer = seconds;
  beginStoppage();
  for (auto& player : players)
  {
    if (!active(player)) continue;
    player.stamina = std::min(1.0f, player.stamina + recovery);
    player.sprintReserve = 1.0f;
    player.velocity = {0.0f, 0.0f};
  }
  updateTeamPhases();
  logEvent(MatchEventType::HALF_TIME);
}

bool MatchEngine::tieLevel() const
{
  return homeScore + knockout.homeAggregate ==
         awayScore + knockout.awayAggregate;
}

void MatchEngine::setKnockout(const MatchRules::Knockout& rules)
{
  if (stepCounter != 0) return;
  knockout = rules;
}

std::optional<bool> MatchEngine::getTieWinnerHome() const
{
  if (!knockout.required || state != MatchState::FULL_TIME) return std::nullopt;
  const int home = homeScore + knockout.homeAggregate;
  const int away = awayScore + knockout.awayAggregate;
  if (home != away) return home > away;
  if (shootout.goals[0] != shootout.goals[1])
    return shootout.goals[0] > shootout.goals[1];
  return std::nullopt;
}

int MatchEngine::maxSubstitutions() const
{
  return MatchTuning::Rules::MAX_SUBSTITUTIONS_PER_TEAM +
         (extraTimeReached ? MatchTuning::Rules::EXTRA_TIME_SUBSTITUTIONS : 0);
}

void MatchEngine::startShootout()
{
  shootout.started = true;
  shootout.homeFirst = randomFloat(0.0f, 1.0f) < 0.5f;
  // Kicking order: the designated taker (or the best one), then the others
  // on the pitch by penalty ability, the goalkeeper last.
  const auto ability = [](const MatchPlayer& player)
  {
    return player.shooting * 0.75f + player.passing * 0.15f +
           player.vision * 0.10f;
  };
  for (const bool home : {true, false})
  {
    const MatchPlayer* first = dutyTaker(home, SetPieceDuty::Penalties);
    std::vector<const MatchPlayer*> kickers;
    for (const MatchPlayer& player : players)
      if (active(player) && player.isHomeTeam == home)
        kickers.push_back(&player);
    std::ranges::stable_sort(
        kickers,
        [&](const MatchPlayer* left, const MatchPlayer* right)
        {
          const auto rank = [&](const MatchPlayer* player)
          {
            return player == first        ? 0
                   : player->isGoalkeeper ? 3
                   : player->isInjured    ? 2
                                          : 1;
          };
          if (rank(left) != rank(right)) return rank(left) < rank(right);
          return ability(*left) > ability(*right);
        });
    std::vector<PlayerID>& order = shootout.order[home ? 0 : 1];
    order.clear();
    for (const MatchPlayer* kicker : kickers)
      order.push_back(kicker->player->getId());
  }

  // Everyone waits by the halfway line; each side shoots at the goal it
  // attacked, facing the other side's keeper.
  state = MatchState::PENALTY_SHOOTOUT;
  shootout.inFlight = false;
  shootout.timer = MatchTuning::Timing::SHOOTOUT_START_SECONDS;
  restartTakerIndex.reset();
  ball = MatchBall{};
  std::array<int, 2> placed{};
  for (MatchPlayer& player : players)
  {
    if (!active(player)) continue;
    const std::size_t side = player.isHomeTeam ? 0 : 1;
    player.velocity = {0.0f, 0.0f};
    if (player.isGoalkeeper)
    {
      keepers[side].state = GoalkeeperState::SET_POSITION;
      player.position = {player.isHomeTeam ? MatchTuning::Pitch::PLAYER_MIN_X
                                           : MatchTuning::Pitch::PLAYER_MAX_X,
                         MatchTuning::Pitch::CENTRE};
    }
    else
    {
      player.position = {player.isHomeTeam ? 0.48f : 0.52f,
                         0.32f + 0.04f * static_cast<float>(placed[side]++)};
    }
    player.movementTarget = player.position;
    player.tacticalTarget = player.position;
  }
  updateTeamPhases();
  logEvent(MatchEventType::PENALTY_SHOOTOUT);
}

void MatchEngine::beginShootoutKick()
{
  const int taken = shootout.kicks[0] + shootout.kicks[1];
  const bool home = (taken % 2 == 0) == shootout.homeFirst;
  const std::size_t side = home ? 0 : 1;
  MatchPlayer* taker = nullptr;
  if (const std::vector<PlayerID>& order = shootout.order[side]; !order.empty())
  {
    const PlayerID id =
        order[static_cast<std::size_t>(shootout.kicks[side]) % order.size()];
    for (MatchPlayer& player : players)
      if (active(player) && player.player->getId() == id) taker = &player;
  }
  shootout.kickerHome = home;
  shootout.taker = taker ? taker->player->getId() : 0;
  if (!taker)
  {
    finishShootoutKick(MatchEventDetail::MISSED);
    return;
  }
  if (MatchPlayer* keeper = findGoalkeeper(!home))
  {
    keeper->position = {home ? MatchTuning::Pitch::PLAYER_MAX_X
                             : MatchTuning::Pitch::PLAYER_MIN_X,
                        MatchTuning::Pitch::CENTRE};
    keeper->velocity = {0.0f, 0.0f};
    keeper->movementTarget = keeper->position;
  }
  const Vector2F spot{home ? MatchTuning::Pitch::RIGHT_PENALTY_SPOT_X
                           : MatchTuning::Pitch::LEFT_PENALTY_SPOT_X,
                      MatchTuning::Pitch::CENTRE};
  taker->position = spot;
  taker->velocity = {0.0f, 0.0f};
  taker->movementTarget = spot;
  ball = MatchBall{};
  ball.position = spot;
  ball.possessedBy = taker->player;
  ball.lastPossessor = taker->player;
  strikeShot(*taker, MatchTuning::Shooting::PENALTY_XG, false, true);
  planGoalkeeperDive(!home, true);
  shootout.inFlight = true;
}

void MatchEngine::updateShootout(float dt)
{
  if (!shootout.inFlight)
  {
    shootout.timer -= dt;
    if (shootout.timer <= 0.0f) beginShootoutKick();
    return;
  }
  MatchPlayer* keeper = findGoalkeeper(!shootout.kickerHome);
  if (keeper) diveGoalkeeper(*keeper, dt);
  const float lineX = shootout.kickerHome ? 1.0f : 0.0f;
  const int substeps =
      MatchTuning::Timing::BALL_SUBSTEPS * static_cast<int>(stepTicks);
  const float substep = dt / static_cast<float>(substeps);
  for (int index = 0; index < substeps; ++index)
  {
    substepBallPosition = ball.position;
    substepBallZ = ball.z;
    integrateBall(substep);
    ball.shotElapsedSeconds += substep;
    if (keeper && !ball.shotSaveResolved)
    {
      const float before = substepBallPosition.x - keeper->position.x;
      const float after = ball.position.x - keeper->position.x;
      if ((before > 0.0f) != (after > 0.0f) || std::abs(after) <= EPSILON)
      {
        ball.shotSaveResolved = true;
        if (ball.shotOnTarget && attemptSave(*keeper).saved)
        {
          finishShootoutKick(MatchEventDetail::SAVED);
          return;
        }
      }
    }
    const bool crossed = shootout.kickerHome ? ball.position.x >= lineX
                                             : ball.position.x <= lineX;
    if (crossed)
    {
      // In when it crosses the line inside the frame (the woodwork misses).
      const float span = ball.position.x - substepBallPosition.x;
      const float t =
          std::abs(span) > EPSILON
              ? std::clamp((lineX - substepBallPosition.x) / span, 0.0f, 1.0f)
              : 1.0f;
      const float lateralMetres =
          (substepBallPosition.y +
           (ball.position.y - substepBallPosition.y) * t -
           MatchTuning::Pitch::CENTRE) *
          MatchTuning::Pitch::WIDTH_METRES;
      const float heightMetres = (substepBallZ + (ball.z - substepBallZ) * t) *
                                 MatchTuning::Units::BALL_Z_METRES;
      const bool inside =
          std::abs(lateralMetres) <
              MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES -
                  MatchTuning::Units::BALL_RADIUS_METRES &&
          heightMetres < MatchTuning::Units::CROSSBAR_HEIGHT_METRES -
                             MatchTuning::Units::BALL_RADIUS_METRES;
      finishShootoutKick(inside ? MatchEventDetail::SCORED
                                : MatchEventDetail::MISSED);
      return;
    }
    if (ball.shotElapsedSeconds >=
            MatchTuning::Timing::SHOOTOUT_MAX_FLIGHT_SECONDS ||
        (ball.shotElapsedSeconds > 0.3f &&
         length(ball.velocity) < MatchTuning::Ball::DEAD_SHOT_SPEED))
    {
      finishShootoutKick(MatchEventDetail::MISSED);
      return;
    }
  }
}

void MatchEngine::finishShootoutKick(MatchEventDetail outcome)
{
  const std::size_t side = shootout.kickerHome ? 0 : 1;
  ++shootout.kicks[side];
  if (outcome == MatchEventDetail::SCORED) ++shootout.goals[side];
  shootout.inFlight = false;
  shootout.timer = MatchTuning::Timing::SHOOTOUT_KICK_INTERVAL_SECONDS;
  clearFlightState();
  ball.possessedBy = nullptr;
  ball.velocity = {0.0f, 0.0f};
  ball.velocityZ = 0.0f;
  keepers[shootout.kickerHome ? 1 : 0].state = GoalkeeperState::SET_POSITION;
  MatchPlayer* taker = nullptr;
  for (MatchPlayer& player : players)
    if (active(player) && player.player->getId() == shootout.taker)
      taker = &player;
  MatchEvent& event = taker ? logEvent(MatchEventType::PENALTY_SHOOTOUT, *taker)
                            : logEvent(MatchEventType::PENALTY_SHOOTOUT);
  event.detail = outcome;
  event.hasTeam = true;
  event.isHomeTeam = shootout.kickerHome;
  if (taker)
  {
    // The taker walks back to his team-mates.
    taker->position = {taker->isHomeTeam ? 0.47f : 0.53f,
                       MatchTuning::Pitch::CENTRE};
    taker->movementTarget = taker->position;
  }
  if (MatchRules::shootoutDecided(shootout.goals[0], shootout.kicks[0],
                                  shootout.goals[1], shootout.kicks[1]))
    finishMatch();
}

void MatchEngine::finishMatch()
{
  state = MatchState::FULL_TIME;
  for (const auto& player : players)
  {
    if (active(player))
      playerStats[player.statsIndex].condition = player.stamina;
  }
  refreshRatings();
  updateTeamPhases();
  logEvent(MatchEventType::FULL_TIME);
}

void MatchEngine::updateTeamPhases()
{
  if (state == MatchState::HALF_TIME || state == MatchState::FULL_TIME)
  {
    homePhase = TeamPhase::STOPPAGE;
    awayPhase = TeamPhase::STOPPAGE;
    return;
  }
  if (state != MatchState::PLAYING)
  {
    homePhase = TeamPhase::SET_PIECE;
    awayPhase = TeamPhase::SET_PIECE;
    transitionSecondsRemaining = 0.0f;
    if (const MatchPlayer* restartOwner = findMatchPlayer(ball.possessedBy))
      lastControlledTeamHome = restartOwner->isHomeTeam;
    return;
  }

  const MatchPlayer* carrier = findMatchPlayer(ball.possessedBy);
  if (carrier && !lastControlledTeamHome)
    lastControlledTeamHome = carrier->isHomeTeam;

  const auto setTransitionPhases = [&](bool attackingHome)
  {
    homePhase = attackingHome ? TeamPhase::ATTACKING_TRANSITION
                              : TeamPhase::DEFENSIVE_TRANSITION;
    awayPhase = attackingHome ? TeamPhase::DEFENSIVE_TRANSITION
                              : TeamPhase::ATTACKING_TRANSITION;
  };
  if (transitionSecondsRemaining > 0.0f && lastControlledTeamHome)
  {
    setTransitionPhases(*lastControlledTeamHome);
    return;
  }

  const auto setControlledPhases = [&](bool attackingHome, float ballX)
  {
    const bool finalThird =
        attackingHome ? ballX >= MatchTuning::Rules::HOME_FINAL_THIRD_START
                      : ballX <= MatchTuning::Rules::AWAY_FINAL_THIRD_START;
    const TeamPhase attackingPhase =
        finalThird ? TeamPhase::FINAL_THIRD : TeamPhase::POSSESSION;
    homePhase = attackingHome ? attackingPhase : TeamPhase::DEFENSIVE_BLOCK;
    awayPhase = attackingHome ? TeamPhase::DEFENSIVE_BLOCK : attackingPhase;
  };
  if (carrier)
  {
    setControlledPhases(carrier->isHomeTeam, carrier->position.x);
    return;
  }

  // An intentional pass or shot is still part of the attacking team's
  // controlled phase. Treating every ball flight as a transition made both
  // teams repeatedly abandon their coordinated support and defensive shape.
  if ((ball.isPass || ball.isShot) && lastControlledTeamHome)
  {
    setControlledPhases(*lastControlledTeamHome, ball.position.x);
    return;
  }

  if (!lastControlledTeamHome)
  {
    homePhase = TeamPhase::DEFENSIVE_TRANSITION;
    awayPhase = TeamPhase::DEFENSIVE_TRANSITION;
    return;
  }

  setTransitionPhases(*lastControlledTeamHome);
}

void MatchEngine::updateMovement(float dt)
{
  // Off-ball targets refresh at the tactical rate, and immediately when the
  // ball changes hands or starts/ends a flight; bodies move every step.
  const std::uint8_t flags = static_cast<std::uint8_t>(
      (ball.isPass ? 1U : 0U) | (ball.isShot ? 2U : 0U) |
      (lastControlledTeamHome ? (*lastControlledTeamHome ? 4U : 8U) : 0U));
  if (periodElapsed(MatchTuning::Timing::TACTICAL_REFRESH_STEPS) ||
      ball.possessedBy != tacticalOwner || flags != tacticalFlags)
  {
    tacticalOwner = ball.possessedBy;
    tacticalFlags = flags;
    refreshTacticalTargets(dt);
  }
  else
  {
    // Between refreshes only the keepers re-read the play.
    const MatchPlayer* holder = findMatchPlayer(ball.possessedBy);
    for (auto& player : players)
    {
      if (!active(player) || !player.isGoalkeeper ||
          keepers[player.isHomeTeam ? 0 : 1].state == GoalkeeperState::DIVE)
        continue;
      if (!(ball.isPass && ball.intendedReceiver == player.player))
        player.tacticalTarget = goalkeeperTarget(player, holder);
    }
  }

  // Every target was read from the same snapshot of the play; now the bodies
  // move: diving keepers, the controlled player and a batch of the rest.
  std::array<std::uint8_t, 32> batch;
  std::size_t batched = 0;
  for (std::size_t slot = 0; slot < players.size() && slot < batch.size();
       ++slot)
  {
    MatchPlayer& player = players[slot];
    if (!active(player)) continue;
    if (player.isGoalkeeper &&
        keepers[player.isHomeTeam ? 0 : 1].state == GoalkeeperState::DIVE)
      diveGoalkeeper(player, dt);
    else if (controlledIndex && slot == *controlledIndex)
      integrateControlled(player, dt);
    else
      batch[batched++] = static_cast<std::uint8_t>(slot);
  }
  integrateMovements(batch.data(), batched, dt, false);
  separatePlayers();
}

void MatchEngine::refreshTacticalTargets(float dt)
{
  // Team planning happens in two passes: choose shared assignments (pressers,
  // runners, support and marks), then give each player a target and intent.
  // Bodies stay still until updateMovement integrates all targets, so player
  // iteration order cannot make later players see an already-moved opponent.
  // Add an off-ball behavior here; physical acceleration belongs downstream.
  const MatchPlayer* carrier = findMatchPlayer(ball.possessedBy);
  const MatchPlayer* transitionSource = findMatchPlayer(ball.lastPossessor);
  // The players on the pitch of each side, in slot order (index 0 home).
  std::array<std::array<MatchPlayer*, 16>, 2> sides{};
  std::array<std::size_t, 2> sideSizes{};
  for (auto& player : players)
  {
    const std::size_t side = player.isHomeTeam ? 0 : 1;
    if (active(player) && sideSizes[side] < sides[side].size())
      sides[side][sideSizes[side]++] = &player;
  }
  const auto onPitch = [&sides, &sideSizes](bool homeTeam)
  {
    const std::size_t side = homeTeam ? 0 : 1;
    return std::span<MatchPlayer* const>(sides[side].data(), sideSizes[side]);
  };
  // Nobody moves while the targets are chosen, so each side's offside line
  // is read once.
  const std::array<float, 2> offsideLines{offsideLine(true),
                                          offsideLine(false)};
  const auto lineFor = [&offsideLines](bool attackingHome)
  { return offsideLines[attackingHome ? 0 : 1]; };
  const Vector2F pressurePosition = carrier ? carrier->position : ball.position;
  const StrategySliders homeSliders = getEffectiveSliders(true);
  const StrategySliders awaySliders = getEffectiveSliders(false);

  const auto estimatedArrivalTime = [&](const MatchPlayer& candidate)
  {
    const Vector2F toTarget =
        toMetres({pressurePosition.x - candidate.position.x,
                  pressurePosition.y - candidate.position.y});
    const float targetDistance = length(toTarget);
    if (targetDistance <= EPSILON) return 0.0f;
    const Vector2F pursuitDirection{toTarget.x / targetDistance,
                                    toTarget.y / targetDistance};
    const float velocityTowardTarget =
        std::max(0.0f, candidate.velocity.x * pursuitDirection.x +
                           candidate.velocity.y * pursuitDirection.y);
    const float pursuitSpeed =
        std::max(currentTopSpeed(candidate) +
                     velocityTowardTarget *
                         MatchTuning::Player::CURRENT_VELOCITY_PURSUIT_WEIGHT,
                 MatchTuning::Player::MINIMUM_PURSUIT_SPEED);
    float arrivalTime = std::max(0.0f, targetDistance / pursuitSpeed -
                                           pressEagerness(candidate, carrier));
    if (candidate.intent == PlayerIntent::PRESS_BALL ||
        candidate.intent == PlayerIntent::CLAIM_LOOSE_BALL)
    {
      arrivalTime = std::max(
          0.0f, arrivalTime - MatchTuning::Shape::PRESSER_CONTINUITY_SECONDS);
    }
    return arrivalTime;
  };

  const auto closestOutfieldPair = [&](bool homeTeam)
  {
    std::array<MatchPlayer*, 2> closest{nullptr, nullptr};
    std::array<float, 2> arrivalTimes{std::numeric_limits<float>::max(),
                                      std::numeric_limits<float>::max()};
    for (MatchPlayer* const pointer : onPitch(homeTeam))
    {
      MatchPlayer& candidate = *pointer;
      if (candidate.isGoalkeeper) continue;
      const float arrivalTime = estimatedArrivalTime(candidate);
      if (arrivalTime < arrivalTimes[0])
      {
        closest[1] = closest[0];
        arrivalTimes[1] = arrivalTimes[0];
        closest[0] = &candidate;
        arrivalTimes[0] = arrivalTime;
      }
      else if (arrivalTime < arrivalTimes[1])
      {
        closest[1] = &candidate;
        arrivalTimes[1] = arrivalTime;
      }
    }

    // A purposeful pass creates a receiving run. Prioritizing that receiver
    // prevents a merely nearby teammate from making the play look arbitrary.
    if (!carrier && ball.isPass && ball.passByHome == homeTeam)
    {
      MatchPlayer* intended = findMatchPlayer(ball.intendedReceiver);
      if (intended && active(*intended) && !intended->isGoalkeeper &&
          closest[0] != intended)
      {
        closest[1] = closest[0];
        closest[0] = intended;
      }
    }
    return closest;
  };

  const auto homePressers = closestOutfieldPair(true);
  const auto awayPressers = closestOutfieldPair(false);
  if (periodElapsed(MatchTuning::Shape::MARK_REFRESH_STEPS)) assignMarks();

  const auto runPriority = [&](const MatchPlayer& candidate)
  {
    float rolePriority = MatchTuning::Shape::MIDFIELDER_RUN_PRIORITY;
    switch (candidate.player->getRole())
    {
      case PlayerRole::ST:
        rolePriority = MatchTuning::Shape::STRIKER_RUN_PRIORITY;
        break;
      case PlayerRole::LW:
      case PlayerRole::RW:
        rolePriority = MatchTuning::Shape::WINGER_RUN_PRIORITY;
        break;
      case PlayerRole::CAM:
        rolePriority = MatchTuning::Shape::ATTACKING_MIDFIELDER_RUN_PRIORITY;
        break;
      case PlayerRole::CDM:
        rolePriority = MatchTuning::Shape::HOLDING_MIDFIELDER_RUN_PRIORITY;
        break;
      case PlayerRole::CM:
      case PlayerRole::LM:
      case PlayerRole::RM:
        break;
      default:
        return -std::numeric_limits<float>::infinity();
    }
    const float depth = candidate.isHomeTeam ? candidate.position.x
                                             : 1.0f - candidate.position.x;
    const float separation =
        carrier ? std::abs(candidate.position.y - carrier->position.y) : 0.0f;
    // Who makes the run also varies from attack to attack, so strike
    // partners share the runs in behind.
    const float variety = epochNoise(candidate.player->getId(), 0x7a11edU,
                                     RUN_TIMING_EPOCH_STEPS) *
                          MatchTuning::Shape::RUN_VARIETY_PRIORITY;
    return rolePriority + variety +
           candidate.pace * MatchTuning::Shape::RUN_PACE_PRIORITY +
           depth * MatchTuning::Shape::RUN_DEPTH_PRIORITY +
           separation * MatchTuning::Shape::RUN_SEPARATION_PRIORITY +
           (candidate.isMakingRun ? MatchTuning::Shape::RUN_CONTINUITY_PRIORITY
                                  : 0.0f) +
           roleProfileOf(candidate).runBias * TacticsTuning::RUN_BIAS_WEIGHT;
  };

  const auto selectAttackingRunners = [&](bool homeTeam)
  {
    std::array<MatchPlayer*, MatchTuning::Shape::MAX_COMMITTED_RUNNERS>
        runners{};
    std::array<float, MatchTuning::Shape::MAX_COMMITTED_RUNNERS> scores;
    scores.fill(-std::numeric_limits<float>::infinity());
    const bool possessionContext = carrier && carrier->isHomeTeam == homeTeam;
    const bool looseTransitionContext =
        !carrier && !ball.isPass && transitionSource &&
        transitionSource->isHomeTeam == homeTeam;
    if (!possessionContext && !looseTransitionContext) return runners;

    const StrategySliders& sliders = homeTeam ? homeSliders : awaySliders;
    const float attackingPosition =
        carrier ? carrier->position.x : ball.position.x;
    const float attackingProgress =
        homeTeam ? attackingPosition : 1.0f - attackingPosition;
    const float attackingCommitment =
        sliders.offensiveBias + sliders.riskTaking;
    const TeamPhase phase = homeTeam ? homePhase : awayPhase;
    const int lead = homeTeam ? homeScore - awayScore : awayScore - homeScore;
    const bool managingGame = lead >= MatchTuning::Decision::COMFORTABLE_LEAD;
    const std::size_t runnerCount =
        !managingGame &&
                (phase == TeamPhase::ATTACKING_TRANSITION ||
                 attackingProgress >=
                     MatchTuning::Shape::SECOND_RUNNER_PROGRESS_THRESHOLD ||
                 attackingCommitment >=
                     MatchTuning::Shape::SECOND_RUNNER_ATTACK_THRESHOLD)
            ? MatchTuning::Shape::MAX_COMMITTED_RUNNERS
            : MatchTuning::Shape::MIN_COMMITTED_RUNNERS;

    // The best runs by priority, with at most one striker among them: the
    // other striker comes short and the late runs come from midfield.
    std::size_t strikers = 0;
    for (std::size_t slot = 0; slot < runnerCount; ++slot)
    {
      for (MatchPlayer* const pointer : onPitch(homeTeam))
      {
        MatchPlayer& candidate = *pointer;
        if (&candidate == carrier || candidate.isInjured ||
            std::ranges::find(runners, &candidate) != runners.end())
          continue;
        const bool striker = candidate.player->getRole() == PlayerRole::ST;
        if (striker && strikers >= MatchTuning::Shape::MAX_STRIKER_RUNNERS)
          continue;
        const float score = runPriority(candidate);
        if (score > scores[slot])
        {
          scores[slot] = score;
          runners[slot] = &candidate;
        }
      }
      if (runners[slot] && runners[slot]->player->getRole() == PlayerRole::ST)
        ++strikers;
    }
    return runners;
  };

  const auto homeRunners = selectAttackingRunners(true);
  const auto awayRunners = selectAttackingRunners(false);

  const auto runChannel = [](const MatchPlayer& runner)
  {
    const PlayerRole role = runner.player->getRole();
    if (role == PlayerRole::LW)
      return MatchTuning::Shape::LEFT_INSIDE_FORWARD_CHANNEL;
    if (role == PlayerRole::RW)
      return MatchTuning::Shape::RIGHT_INSIDE_FORWARD_CHANNEL;
    if (role == PlayerRole::LM)
      return MatchTuning::Shape::LEFT_WIDE_ATTACK_CHANNEL;
    if (role == PlayerRole::RM)
      return MatchTuning::Shape::RIGHT_WIDE_ATTACK_CHANNEL;
    if (runner.basePosition.y < MatchTuning::Pitch::CENTRE)
      return MatchTuning::Shape::LEFT_STRIKER_ATTACK_CHANNEL;
    if (runner.basePosition.y > MatchTuning::Pitch::CENTRE)
      return MatchTuning::Shape::RIGHT_STRIKER_ATTACK_CHANNEL;
    return MatchTuning::Shape::CENTRAL_ATTACK_CHANNEL;
  };

  // In the final third a central midfielder arrives late at the edge of
  // the box, the wide player on the far side attacks the far post and a
  // full-back overlaps.
  enum class Arrival
  {
    MIDFIELDER,
    FAR_POST,
    FULLBACK
  };
  const auto selectFinalThirdPlayer = [&](bool homeTeam,
                                          Arrival arrival) -> MatchPlayer*
  {
    if (!carrier || carrier->isHomeTeam != homeTeam) return nullptr;
    const TeamPhase phase = homeTeam ? homePhase : awayPhase;
    if (phase != TeamPhase::FINAL_THIRD) return nullptr;
    const float ballSide = carrier->position.y - MatchTuning::Pitch::CENTRE;
    if (arrival == Arrival::FAR_POST &&
        std::abs(ballSide) < MatchTuning::Shape::FAR_POST_MIN_BALL_WIDTH)
      return nullptr;

    MatchPlayer* selected = nullptr;
    float bestScore = std::numeric_limits<float>::max();
    for (MatchPlayer* const pointer : onPitch(homeTeam))
    {
      MatchPlayer& candidate = *pointer;
      if (&candidate == carrier || candidate.isInjured) continue;
      const auto& runners = homeTeam ? homeRunners : awayRunners;
      if (std::ranges::find(runners, &candidate) != runners.end()) continue;
      const PlayerRole role = candidate.player->getRole();
      const bool wide = role == PlayerRole::LM || role == PlayerRole::RM ||
                        role == PlayerRole::LW || role == PlayerRole::RW;
      const bool farSide =
          (candidate.basePosition.y - MatchTuning::Pitch::CENTRE) * ballSide <
          0.0f;
      const bool eligible =
          arrival == Arrival::FULLBACK
              ? role == PlayerRole::LB || role == PlayerRole::RB
          : arrival == Arrival::FAR_POST
              ? wide && farSide
              : role == PlayerRole::CM || role == PlayerRole::CAM;
      if (!eligible) continue;
      // A defensive full-back never overlaps; an attacking one is keener.
      const float overlapBias = arrival == Arrival::FULLBACK
                                    ? roleProfileOf(candidate).overlapBias
                                    : 0.0f;
      if (overlapBias <= -1.0f) continue;

      float score = std::abs(candidate.basePosition.y - carrier->position.y) -
                    overlapBias * TacticsTuning::OVERLAP_BIAS_WEIGHT;
      if (arrival == Arrival::FAR_POST) score = -score;
      const PlayerIntent continuityIntent = arrival == Arrival::FULLBACK
                                                ? PlayerIntent::OVERLAP
                                                : PlayerIntent::ATTACK_BOX;
      if (candidate.intent == continuityIntent)
        score -= MatchTuning::Shape::FINAL_THIRD_SELECTION_CONTINUITY;
      if (score < bestScore)
      {
        bestScore = score;
        selected = &candidate;
      }
    }
    return selected;
  };

  MatchPlayer* homeMidfieldArrival =
      selectFinalThirdPlayer(true, Arrival::MIDFIELDER);
  MatchPlayer* awayMidfieldArrival =
      selectFinalThirdPlayer(false, Arrival::MIDFIELDER);
  MatchPlayer* homeFarPostRunner =
      selectFinalThirdPlayer(true, Arrival::FAR_POST);
  MatchPlayer* awayFarPostRunner =
      selectFinalThirdPlayer(false, Arrival::FAR_POST);
  MatchPlayer* homeOverlappingFullback =
      selectFinalThirdPlayer(true, Arrival::FULLBACK);
  MatchPlayer* awayOverlappingFullback =
      selectFinalThirdPlayer(false, Arrival::FULLBACK);

  const auto selectActiveSupporters = [&](bool homeTeam)
  {
    std::array<MatchPlayer*, MatchTuning::Shape::MAX_ACTIVE_SUPPORTERS>
        supporters{};
    std::array<float, MatchTuning::Shape::MAX_ACTIVE_SUPPORTERS> scores;
    scores.fill(std::numeric_limits<float>::max());
    if (!carrier || carrier->isHomeTeam != homeTeam) return supporters;

    const auto& runners = homeTeam ? homeRunners : awayRunners;
    const MatchPlayer* midfieldArrival =
        homeTeam ? homeMidfieldArrival : awayMidfieldArrival;
    const MatchPlayer* farPostRunner =
        homeTeam ? homeFarPostRunner : awayFarPostRunner;
    const MatchPlayer* overlappingFullback =
        homeTeam ? homeOverlappingFullback : awayOverlappingFullback;
    for (MatchPlayer* const pointer : onPitch(homeTeam))
    {
      MatchPlayer& candidate = *pointer;
      if (&candidate == carrier || candidate.isGoalkeeper ||
          &candidate == midfieldArrival || &candidate == farPostRunner ||
          &candidate == overlappingFullback ||
          std::ranges::find(runners, &candidate) != runners.end())
      {
        continue;
      }

      const PlayerRole role = candidate.player->getRole();
      const bool eligibleSupportRole =
          role == PlayerRole::CDM || role == PlayerRole::CM ||
          role == PlayerRole::CAM || role == PlayerRole::LM ||
          role == PlayerRole::RM || role == PlayerRole::LW ||
          role == PlayerRole::RW || role == PlayerRole::ST;
      if (!eligibleSupportRole) continue;

      const float score =
          distance(candidate.position, carrier->position) -
          (candidate.intent == PlayerIntent::OFFER_SUPPORT
               ? MatchTuning::Shape::SUPPORT_SELECTION_CONTINUITY_METRES
               : 0.0f);
      for (std::size_t slot = 0; slot < supporters.size(); ++slot)
      {
        if (score >= scores[slot]) continue;
        for (std::size_t shifted = supporters.size() - 1; shifted > slot;
             --shifted)
        {
          supporters[shifted] = supporters[shifted - 1];
          scores[shifted] = scores[shifted - 1];
        }
        supporters[slot] = &candidate;
        scores[slot] = score;
        break;
      }
    }
    return supporters;
  };

  const auto homeSupporters = selectActiveSupporters(true);
  const auto awaySupporters = selectActiveSupporters(false);

  const auto findCoverOutlet = [&](bool defendingHome) -> const MatchPlayer*
  {
    if (!carrier || carrier->isHomeTeam == defendingHome) return nullptr;
    const float possessionDirection = carrier->isHomeTeam ? 1.0f : -1.0f;
    const MatchPlayer* outlet = nullptr;
    float bestScore = std::numeric_limits<float>::max();
    for (const MatchPlayer* const pointer : onPitch(carrier->isHomeTeam))
    {
      const MatchPlayer& candidate = *pointer;
      if (&candidate == carrier || candidate.isGoalkeeper) continue;
      const float outletDistance =
          distance(candidate.position, carrier->position);
      if (outletDistance > MatchTuning::Shape::COVER_OUTLET_MAX_DISTANCE_METRES)
        continue;
      const bool forwardOption =
          (candidate.position.x - carrier->position.x) * possessionDirection >
          0.0f;
      const float score =
          outletDistance -
          (forwardOption ? MatchTuning::Shape::COVER_FORWARD_OPTION_METRES
                         : 0.0f);
      if (score < bestScore)
      {
        bestScore = score;
        outlet = &candidate;
      }
    }
    return outlet;
  };

  const MatchPlayer* homeCoverOutlet = findCoverOutlet(true);
  const MatchPlayer* awayCoverOutlet = findCoverOutlet(false);

  for (auto& player : players)
  {
    if (!active(player)) continue;
    const bool goalkeeper = player.isGoalkeeper;
    const StrategySliders& sliders =
        player.isHomeTeam ? homeSliders : awaySliders;
    const TeamPhase teamPhase = player.isHomeTeam ? homePhase : awayPhase;
    const float attackDirection = player.isHomeTeam ? 1.0f : -1.0f;
    const bool ownPossession =
        carrier && carrier->isHomeTeam == player.isHomeTeam;
    // While his side has the ball (or its pass is on the way) every slot
    // starts from its attacking-phase spot (role and in-possession shape),
    // otherwise from the formation.
    const bool ownControl =
        carrier ? ownPossession
                : lastControlledTeamHome &&
                      *lastControlledTeamHome == player.isHomeTeam;
    Vector2F target =
        ownControl ? possessionAnchor(player) : player.basePosition;
    const PlayerIntent previousIntent = player.intent;
    player.intent = PlayerIntent::HOLD_SHAPE;
    player.isPressing = false;
    player.isMakingRun = false;

    // The whole block follows the ball while preserving its formation. This
    // produces recognizable defensive, middle and attacking lines rather than
    // twenty outfield players independently chasing one point.
    const float widthScale =
        MatchTuning::Shape::MIN_WIDTH_SCALE +
        sliders.widthUsage * MatchTuning::Shape::WIDTH_SLIDER_SCALE;
    target.y = MatchTuning::Pitch::CENTRE +
               (target.y - MatchTuning::Pitch::CENTRE) * widthScale;
    target.x += (pressurePosition.x - MatchTuning::Pitch::CENTRE) *
                (MatchTuning::Shape::BASE_LONGITUDINAL_SHIFT +
                 sliders.compactness *
                     MatchTuning::Shape::COMPACTNESS_LONGITUDINAL_SHIFT);
    target.y +=
        (pressurePosition.y - MatchTuning::Pitch::CENTRE) *
        (MatchTuning::Shape::BASE_LATERAL_SHIFT +
         sliders.compactness * MatchTuning::Shape::COMPACTNESS_LATERAL_SHIFT);

    // Out of possession the block drops and compacts around the ball: the
    // back line sits deeper the closer the ball is to goal, and the lines
    // behind it squeeze into a 20-40 m long, ~45 m wide unit.
    const bool defending =
        !goalkeeper &&
        (carrier ? carrier->isHomeTeam != player.isHomeTeam
                 : lastControlledTeamHome &&
                       *lastControlledTeamHome != player.isHomeTeam);
    if (defending)
    {
      using S = MatchTuning::Shape;
      const auto ownGoalDepth = [&](float x)
      { return player.isHomeTeam ? x : 1.0f - x; };
      const float ballDepth = ownGoalDepth(pressurePosition.x);
      const float lineDepth = std::clamp(
          S::BLOCK_LINE_BASE + ballDepth * S::BLOCK_LINE_BALL_FACTOR +
              sliders.pressing * S::BLOCK_LINE_PRESSING,
          S::BLOCK_LINE_MIN, S::BLOCK_LINE_MAX);
      const float lengthShare =
          std::clamp((ballDepth - S::BLOCK_SHORT_BALL_DEPTH) /
                         (S::BLOCK_LONG_BALL_DEPTH - S::BLOCK_SHORT_BALL_DEPTH),
                     0.0f, 1.0f);
      const float teamLength =
          (S::BLOCK_SHORT_LENGTH +
           (S::BLOCK_LONG_LENGTH - S::BLOCK_SHORT_LENGTH) * lengthShare) *
          (1.0f - sliders.compactness * S::BLOCK_COMPACTNESS_SQUEEZE);
      const float formationShare = std::clamp(
          (ownGoalDepth(player.basePosition.x) - S::BLOCK_FORMATION_BACK) /
              (S::BLOCK_FORMATION_FRONT - S::BLOCK_FORMATION_BACK),
          0.0f, 1.0f);
      const float depth = lineDepth + formationShare * teamLength;
      target.x = player.isHomeTeam ? depth : 1.0f - depth;
      target.y = MatchTuning::Pitch::CENTRE +
                 (player.basePosition.y - MatchTuning::Pitch::CENTRE) *
                     (S::BLOCK_WIDTH_SCALE -
                      sliders.compactness * S::BLOCK_COMPACTNESS_NARROWING) +
                 (pressurePosition.y - MatchTuning::Pitch::CENTRE) *
                     S::BLOCK_BALL_SIDE_SHIFT;
      target.x += defensiveRoleShift(player);
    }

    if (&player == carrier)
    {
      player.intent = PlayerIntent::CARRY_BALL;
      target = player.position;
      target.x +=
          attackDirection *
          (MatchTuning::Shape::CARRIER_BASE_ADVANCE +
           player.dribbling * MatchTuning::Shape::CARRIER_DRIBBLING_ADVANCE +
           sliders.riskTaking * MatchTuning::Shape::CARRIER_RISK_ADVANCE);
      if (teamPhase == TeamPhase::ATTACKING_TRANSITION)
      {
        target.x += attackDirection *
                    MatchTuning::Shape::ATTACKING_TRANSITION_CARRIER_ADVANCE;
      }

      // Carry away from the nearest defender instead of running directly
      // through them, while gradually looking for a central shooting lane.
      const MatchPlayer* closestOpponent = nullptr;
      float closestDistance = std::numeric_limits<float>::max();
      for (const MatchPlayer* const opponent : onPitch(!player.isHomeTeam))
      {
        const float opponentDistance =
            distance(opponent->position, player.position);
        if (opponentDistance < closestDistance)
        {
          closestDistance = opponentDistance;
          closestOpponent = opponent;
        }
      }
      // A wide forward cutting inside heads for the box; otherwise wide
      // players keep their lane to reach the byline and cross.
      using S = MatchTuning::Shape;
      const PlayerRole role = player.player->getRole();
      const bool wideLane =
          (role == PlayerRole::LM || role == PlayerRole::RM ||
           role == PlayerRole::LW || role == PlayerRole::RW ||
           role == PlayerRole::LB || role == PlayerRole::RB) &&
          std::abs(player.position.y - MatchTuning::Pitch::CENTRE) >
              S::WIDE_LANE_DEVIATION;
      if (cutsInside(player))
      {
        target.y = player.position.y +
                   (MatchTuning::Pitch::CENTRE - player.position.y) *
                       S::CUT_INSIDE_PULL;
      }
      else if (closestOpponent &&
               closestDistance < S::CARRIER_EVASION_RANGE_METRES)
      {
        const float evadeDirection =
            player.position.y <= closestOpponent->position.y ? -1.0f : 1.0f;
        target.y +=
            evadeDirection * (S::CARRIER_BASE_EVASION +
                              player.dribbling * S::CARRIER_DRIBBLING_EVASION);
      }
      else if (wideLane)
      {
        target.y = player.position.y;
      }
      else
      {
        target.y += (MatchTuning::Pitch::CENTRE - player.position.y) *
                    S::CARRIER_CENTRALITY;
      }
    }
    else if (ownPossession)
    {
      player.intent = PlayerIntent::HOLD_SHAPE;
      const float attackingProgress =
          player.isHomeTeam ? carrier->position.x : 1.0f - carrier->position.x;
      target.x +=
          attackDirection *
          std::max(0.0f, attackingProgress -
                             MatchTuning::Shape::POSSESSION_PROGRESS_START) *
          MatchTuning::Shape::POSSESSION_BLOCK_PROGRESS;
      target.x +=
          attackDirection * (MatchTuning::Shape::SUPPORT_BASE_ADVANCE +
                             sliders.offensiveBias *
                                 MatchTuning::Shape::SUPPORT_OFFENSIVE_ADVANCE);
      const PlayerRole role = player.player->getRole();
      const bool forward = role == PlayerRole::ST || role == PlayerRole::LW ||
                           role == PlayerRole::RW || role == PlayerRole::CAM;
      const auto& runners = player.isHomeTeam ? homeRunners : awayRunners;
      const auto runnerPosition = std::ranges::find(runners, &player);
      const bool committedRunner = runnerPosition != runners.end();
      if (committedRunner)
      {
        const std::size_t runnerSlot = static_cast<std::size_t>(
            std::distance(runners.begin(), runnerPosition));
        player.intent = runnerSlot == 0 ? PlayerIntent::RUN_IN_BEHIND
                                        : PlayerIntent::ATTACK_BOX;
        player.isMakingRun = true;
        const float defenderLine = lineFor(player.isHomeTeam);
        const float legalRunLine =
            player.isHomeTeam ? std::max(defenderLine, ball.position.x)
                              : std::min(defenderLine, ball.position.x);
        target.x += attackDirection *
                    (MatchTuning::Shape::RUN_BASE_ADVANCE +
                     sliders.riskTaking * MatchTuning::Shape::RUN_RISK_ADVANCE);
        // Run timing is imperfect: runners hover around the line and now and
        // then drift beyond it, which is where offsides come from.
        const float timing =
            defenderLine * attackDirection >= ball.position.x * attackDirection
                ? (epochNoise(player.player->getId(), 0x0ff51deU,
                              RUN_TIMING_EPOCH_STEPS) *
                       0.5f +
                   0.25f) *
                      MatchTuning::Passing::RUN_TIMING_GAMBLE
                : 0.0f;
        const float onsideTarget =
            legalRunLine -
            attackDirection * (MatchTuning::Shape::RUN_ONSIDE_BUFFER - timing);
        const float runDepthTarget =
            onsideTarget - attackDirection * static_cast<float>(runnerSlot) *
                               MatchTuning::Shape::SECONDARY_RUN_DEPTH_STAGGER;
        target.x += (runDepthTarget - target.x) *
                    MatchTuning::Shape::RUN_DEPTH_TARGET_PULL;
        target.x = player.isHomeTeam ? std::min(target.x, onsideTarget)
                                     : std::max(target.x, onsideTarget);

        float channel = runChannel(player);
        if (runnerSlot > 0 && runners[0])
        {
          const float primaryChannel = runChannel(*runners[0]);
          if (std::abs(channel - primaryChannel) <
              MatchTuning::Shape::MINIMUM_RUN_CHANNEL_SEPARATION)
          {
            channel = primaryChannel <= MatchTuning::Pitch::CENTRE
                          ? MatchTuning::Shape::RIGHT_INSIDE_FORWARD_CHANNEL
                          : MatchTuning::Shape::LEFT_INSIDE_FORWARD_CHANNEL;
          }
        }
        target.y +=
            (channel - target.y) * MatchTuning::Shape::RUN_CHANNEL_BLEND;

        // Only a runner caught clearly beyond the line checks back; one who
        // is marginally off relies on his timing (and sometimes gets caught).
        if (isOffside(player, player.isHomeTeam, defenderLine,
                      MatchTuning::Passing::RUN_TIMING_GAMBLE * 0.75f))
          target.x -= attackDirection * MatchTuning::Shape::ONSIDE_RECOVERY;
      }
      else
      {
        MatchPlayer* midfieldArrival =
            player.isHomeTeam ? homeMidfieldArrival : awayMidfieldArrival;
        MatchPlayer* farPostRunner =
            player.isHomeTeam ? homeFarPostRunner : awayFarPostRunner;
        MatchPlayer* overlappingFullback = player.isHomeTeam
                                               ? homeOverlappingFullback
                                               : awayOverlappingFullback;
        const auto& supporters =
            player.isHomeTeam ? homeSupporters : awaySupporters;
        const auto supportPosition = std::ranges::find(supporters, &player);
        if (&player == midfieldArrival || &player == farPostRunner)
        {
          // Late runs into the box: the midfielder to the penalty spot on
          // the side away from the ball, the wide man to the far post; both
          // stay onside.
          using S = MatchTuning::Shape;
          player.intent = PlayerIntent::ATTACK_BOX;
          const bool farPost = &player == farPostRunner;
          const float goalX = player.isHomeTeam ? 1.0f : 0.0f;
          const float ballSide =
              carrier->position.y < MatchTuning::Pitch::CENTRE ? -1.0f : 1.0f;
          const float arrivalX =
              goalX - attackDirection * (farPost ? S::FAR_POST_ARRIVAL_DEPTH
                                                 : S::MIDFIELD_ARRIVAL_DEPTH);
          const float onside =
              lineFor(player.isHomeTeam) -
              attackDirection * MatchTuning::Shape::RUN_ONSIDE_BUFFER;
          target.x = player.isHomeTeam ? std::min(arrivalX, onside)
                                       : std::max(arrivalX, onside);
          target.y = MatchTuning::Pitch::CENTRE -
                     ballSide * (farPost ? S::FAR_POST_ARRIVAL_WIDTH
                                         : S::MIDFIELD_ARRIVAL_WIDTH);
        }
        else if (&player == overlappingFullback)
        {
          player.intent = PlayerIntent::OVERLAP;
          target.x += attackDirection *
                      MatchTuning::Shape::FINAL_THIRD_FULLBACK_OVERLAP;
        }
        else if (supportPosition != supporters.end())
        {
          player.intent = PlayerIntent::OFFER_SUPPORT;
          const std::size_t supportSlot = static_cast<std::size_t>(
              std::distance(supporters.begin(), supportPosition));
          const float primarySide =
              supporters[MatchTuning::Shape::NEAR_SUPPORT_SLOT] &&
                      supporters[MatchTuning::Shape::NEAR_SUPPORT_SLOT]
                              ->basePosition.y <= carrier->position.y
                  ? -1.0f
                  : 1.0f;
          float supportDepth = MatchTuning::Shape::NEAR_SUPPORT_DEPTH;
          float supportWidth = MatchTuning::Shape::NEAR_SUPPORT_WIDTH;
          float supportSide = primarySide;
          if (supportSlot == MatchTuning::Shape::SQUARE_SUPPORT_SLOT)
          {
            supportDepth = MatchTuning::Shape::SQUARE_SUPPORT_DEPTH;
            supportWidth = MatchTuning::Shape::SQUARE_SUPPORT_WIDTH;
            supportSide = -primarySide;
          }
          else if (supportSlot == MatchTuning::Shape::TRAILING_SUPPORT_SLOT)
          {
            supportDepth = MatchTuning::Shape::TRAILING_SUPPORT_DEPTH;
            supportWidth = MatchTuning::Shape::TRAILING_SUPPORT_WIDTH;
          }

          target = carrier->position;
          target.x -= attackDirection * supportDepth;
          target.y += supportSide * supportWidth;
          if (teamPhase == TeamPhase::ATTACKING_TRANSITION)
          {
            target.x += attackDirection *
                        MatchTuning::Shape::TRANSITION_SUPPORT_FORWARD_BONUS;
          }
        }
        else if (forward)
        {
          player.intent = PlayerIntent::OFFER_SUPPORT;
          // Not every attacker runs beyond the defence. A complementary
          // forward checks toward the ball to form a passing triangle and drag
          // a marker.
          target.x =
              carrier->position.x -
              attackDirection * MatchTuning::Shape::FORWARD_SHORT_OPTION_DEPTH;
          const float lateralDirection =
              player.basePosition.y <= carrier->position.y ? -1.0f : 1.0f;
          target.y =
              carrier->position.y +
              lateralDirection *
                  MatchTuning::Shape::FORWARD_SHORT_OPTION_LATERAL_SEPARATION;
        }
        if (distance(target, carrier->position) >
            MatchTuning::Shape::MAX_SUPPORT_DISTANCE_METRES)
        {
          target.x += (carrier->position.x - target.x) *
                      MatchTuning::Shape::SUPPORT_LONGITUDINAL_PULL;
          target.y += (carrier->position.y - target.y) *
                      MatchTuning::Shape::SUPPORT_LATERAL_PULL;
        }
      }
    }
    else if (carrier)
    {
      const auto& pressers = player.isHomeTeam ? homePressers : awayPressers;
      if (pressers[0] == &player)
      {
        player.intent = PlayerIntent::PRESS_BALL;
        player.isPressing = true;
        const float standOff =
            (MatchTuning::Shape::PRESSING_STANDOFF_BASE +
             (1.0f - sliders.pressing) *
                 MatchTuning::Shape::PRESSING_STANDOFF_CAUTIOUS_BONUS) *
            pressStandOffShare(player, *carrier);
        target = pressurePosition;
        target.x -= attackDirection * standOff;
        target.y += weakFootShade(*carrier);
      }
      else if (const auto doubling =
                   pressers[1] == &player
                       ? doubleUpPoint(player, *carrier, pressers[0])
                       : std::nullopt)
      {
        // Ordered to double up: the second man closes the carrier too.
        player.intent = PlayerIntent::COVER_PRESS;
        player.isPressing = true;
        target = *doubling;
      }
      else if (pressers[1] == &player &&
               sliders.pressing > MatchTuning::Shape::COVER_PRESS_MINIMUM)
      {
        player.isPressing = true;
        const MatchPlayer* coverOutlet =
            player.isHomeTeam ? homeCoverOutlet : awayCoverOutlet;
        if (coverOutlet)
        {
          player.intent = PlayerIntent::BLOCK_PASSING_LANE;
          target.x = pressurePosition.x +
                     (coverOutlet->position.x - pressurePosition.x) *
                         MatchTuning::Shape::COVER_LANE_INTERCEPTION_POINT;
          target.y = pressurePosition.y +
                     (coverOutlet->position.y - pressurePosition.y) *
                         MatchTuning::Shape::COVER_LANE_INTERCEPTION_POINT;
          target.x -=
              attackDirection * MatchTuning::Shape::COVER_LANE_GOAL_SIDE_OFFSET;
        }
        else
        {
          player.intent = PlayerIntent::COVER_PRESS;
          const float lateralSide =
              player.position.y <= pressurePosition.y ? -1.0f : 1.0f;
          target.x = pressurePosition.x -
                     attackDirection * MatchTuning::Shape::COVER_FALLBACK_DEPTH;
          target.y =
              pressurePosition.y +
              lateralSide * MatchTuning::Shape::COVER_FALLBACK_LATERAL_OFFSET;
        }
      }
      else if (const MatchPlayer* tight = tightMarkTarget(player))
      {
        // Under a tight-marking order he stays with his man in every phase,
        // goal-side and close enough to contest each ball played to him.
        player.intent = PlayerIntent::MARK_OPPONENT;
        target = tightMarkPoint(player, *tight, target);
      }
      else if (teamPhase == TeamPhase::DEFENSIVE_TRANSITION)
      {
        // The nearest players counter-press above. Everyone else first gets
        // goal-side and narrows toward the danger before settling into the
        // normal defensive block.
        player.intent = PlayerIntent::RECOVER_SHAPE;
        target.x -=
            attackDirection * MatchTuning::Shape::DEFENSIVE_TRANSITION_RECOVERY;
        target.y += (pressurePosition.y - target.y) *
                    MatchTuning::Shape::DEFENSIVE_TRANSITION_BALL_COMPACTNESS;
      }
      else
      {
        // Zonal marking: shade toward the opponent assigned to this player
        // (one marker per opponent), without abandoning the formation anchor.
        const std::int8_t assigned = markAssignments[slotOf(player)];
        const MatchPlayer* mark =
            assigned >= 0 && static_cast<std::size_t>(assigned) < players.size()
                ? &players[static_cast<std::size_t>(assigned)]
                : nullptr;
        if (mark && (!active(*mark) || mark->isHomeTeam == player.isHomeTeam))
          mark = nullptr;
        if (mark)
        {
          player.intent = PlayerIntent::MARK_OPPONENT;
          float markWeight =
              MatchTuning::Shape::BASE_MARK_WEIGHT +
              sliders.compactness * MatchTuning::Shape::COMPACTNESS_MARK_WEIGHT;
          Vector2F markPoint = mark->position;
          const float markDanger =
              player.isHomeTeam ? 1.0f - mark->position.x : mark->position.x;
          if (markDanger >= MatchTuning::Shape::TIGHT_MARK_DANGER_DEPTH)
          {
            // Near the own goal a marker stays tight and goal-side.
            markWeight =
                std::max(markWeight, MatchTuning::Shape::TIGHT_MARK_WEIGHT);
            markPoint.x -= attackDirection *
                           MatchTuning::Shape::GOAL_SIDE_MARK_METRES /
                           MatchTuning::Pitch::LENGTH_METRES;
          }
          target.x += (markPoint.x - target.x) * markWeight;
          target.y += (markPoint.y - target.y) * markWeight;
        }
      }
    }
    else
    {
      const bool receivingPass =
          ball.isPass && ball.passByHome == player.isHomeTeam;
      const auto& pressers = player.isHomeTeam ? homePressers : awayPressers;
      if (receivingPass)
      {
        if (player.player == ball.intendedReceiver)
        {
          // Meet the ball on its path instead of chasing where it is: the
          // closest point of the remaining flight line ahead of the ball.
          player.intent = PlayerIntent::RECEIVE_PASS;
          const float ballSpeed = length(ball.velocity);
          const Vector2F lookahead = toPitch(
              {ball.velocity.x *
                   MatchTuning::Player::PASS_RECEIVER_LOOKAHEAD_SECONDS,
               ball.velocity.y *
                   MatchTuning::Player::PASS_RECEIVER_LOOKAHEAD_SECONDS});
          Vector2F meet{ball.position.x + lookahead.x,
                        ball.position.y + lookahead.y};
          const float heightMetres = ball.z * MatchTuning::Units::BALL_Z_METRES;
          if (heightMetres > MatchTuning::Aerial::CONTROL_CEILING_METRES ||
              ball.velocityZ > 0.0f)
          {
            // A lofted ball is met where it drops to a controllable height,
            // not intercepted on the way up.
            constexpr float G = MatchTuning::Ball::GRAVITY;
            const float drop = std::max(
                0.0f, heightMetres -
                          MatchTuning::Passing::LOFTED_ARRIVAL_HEIGHT_METRES);
            const float fallSeconds =
                (ball.velocityZ +
                 std::sqrt(ball.velocityZ * ball.velocityZ + 2.0f * G * drop)) /
                G;
            const Vector2F landing =
                toPitch({ball.velocity.x * fallSeconds * AIR_TRAVEL_SHARE,
                         ball.velocity.y * fallSeconds * AIR_TRAVEL_SHARE});
            meet = {ball.position.x + landing.x, ball.position.y + landing.y};
          }
          else if (ballSpeed > EPSILON)
          {
            const Vector2F heading{ball.velocity.x / ballSpeed,
                                   ball.velocity.y / ballSpeed};
            const Vector2F offset =
                toMetres({player.position.x - ball.position.x,
                          player.position.y - ball.position.y});
            const float along = offset.x * heading.x + offset.y * heading.y;
            if (along > 0.0f)
            {
              const Vector2F ahead =
                  toPitch({heading.x * along, heading.y * along});
              meet = {ball.position.x + ahead.x, ball.position.y + ahead.y};
            }
          }
          target = {std::clamp(meet.x, MatchTuning::Pitch::PLAYER_MIN_X,
                               MatchTuning::Pitch::PLAYER_MAX_X),
                    std::clamp(meet.y, MatchTuning::Pitch::PLAYER_MIN_Y,
                               MatchTuning::Pitch::PLAYER_MAX_Y)};
        }
        else
        {
          player.intent = PlayerIntent::OFFER_SUPPORT;
          // Preserve the lane established before release instead of snapping
          // every supporting player back toward the formation anchor for the
          // duration of each pass; drift gently forward and toward the ball
          // (rates per second, so a long pass does not accumulate a run).
          target = player.movementTarget;
          target.x += attackDirection *
                      MatchTuning::Shape::IN_FLIGHT_SUPPORT_ADVANCE_PER_SECOND *
                      dt;
          target.y +=
              (ball.position.y - target.y) *
              MatchTuning::Shape::IN_FLIGHT_SUPPORT_BALL_PULL_PER_SECOND * dt;
        }
      }
      // For a genuinely loose ball, only the nearest players contest it.
      // Everybody else either joins a selected transition run or recovers the
      // team shape. A normal pass is handled above so it does not trigger a
      // full-team scramble on every release of the ball.
      else if (pressers[0] == &player ||
               (pressers[1] == &player &&
                sliders.pressing >
                    MatchTuning::Shape::SECOND_LOOSE_BALL_PRESS_THRESHOLD))
      {
        player.intent = pressers[0] == &player ? PlayerIntent::CLAIM_LOOSE_BALL
                                               : PlayerIntent::COVER_PRESS;
        player.isPressing = true;
        // Run at where the ball will be when he gets there.
        const float lookaheadSeconds =
            std::min(distance(player.position, pressurePosition) /
                         std::max(currentTopSpeed(player), EPSILON),
                     MatchTuning::Player::MAX_LOOSE_BALL_LOOKAHEAD_SECONDS);
        const Vector2F lead = toPitch({ball.velocity.x * lookaheadSeconds,
                                       ball.velocity.y * lookaheadSeconds});
        target = {std::clamp(pressurePosition.x + lead.x,
                             MatchTuning::Pitch::PLAYER_MIN_X,
                             MatchTuning::Pitch::PLAYER_MAX_X),
                  std::clamp(pressurePosition.y + lead.y,
                             MatchTuning::Pitch::PLAYER_MIN_Y,
                             MatchTuning::Pitch::PLAYER_MAX_Y)};
        if (pressers[1] == &player)
        {
          target.x -=
              attackDirection * MatchTuning::Player::COVER_LOOSE_BALL_OFFSET;
        }
      }
      else if (ball.isPass || ball.isShot)
      {
        const MatchPlayer* tight =
            ball.isPass && ball.passByHome != player.isHomeTeam
                ? tightMarkTarget(player)
                : nullptr;
        if (tight)
        {
          // A tight marker goes with his man while the pass is on its way.
          player.intent = PlayerIntent::MARK_OPPONENT;
          target = tightMarkPoint(player, *tight, target);
        }
        else if (teamPhase == TeamPhase::DEFENSIVE_TRANSITION)
        {
          player.intent = PlayerIntent::RECOVER_SHAPE;
          target.x -= attackDirection *
                      MatchTuning::Shape::DEFENSIVE_TRANSITION_RECOVERY;
          target.y += (pressurePosition.y - target.y) *
                      MatchTuning::Shape::DEFENSIVE_TRANSITION_BALL_COMPACTNESS;
        }
        else
        {
          player.intent = PlayerIntent::HOLD_SHAPE;
        }
      }
      else
      {
        const bool attackingTransition =
            teamPhase == TeamPhase::ATTACKING_TRANSITION;
        if (attackingTransition)
        {
          const auto& runners = player.isHomeTeam ? homeRunners : awayRunners;
          const bool transitionRunner =
              std::ranges::find(runners, &player) != runners.end();
          player.intent = transitionRunner ? PlayerIntent::RUN_IN_BEHIND
                                           : PlayerIntent::OFFER_SUPPORT;
          player.isMakingRun = transitionRunner;
          target.x +=
              attackDirection *
              (transitionRunner
                   ? MatchTuning::Shape::ATTACKING_TRANSITION_RUN_ADVANCE
                   : MatchTuning::Shape::ATTACKING_TRANSITION_SUPPORT_ADVANCE);
          target.y += (pressurePosition.y - target.y) *
                      MatchTuning::Shape::ATTACKING_TRANSITION_BALL_PULL;
          if (transitionRunner &&
              isOffside(player, player.isHomeTeam, lineFor(player.isHomeTeam)))
          {
            target.x -= attackDirection * MatchTuning::Shape::ONSIDE_RECOVERY;
          }
        }
        else
        {
          player.intent = PlayerIntent::RECOVER_SHAPE;
          target.x -= attackDirection *
                      MatchTuning::Shape::DEFENSIVE_TRANSITION_RECOVERY;
        }
      }
    }

    // A lofted delivery into the box is attacked by the receiver while the
    // defenders hold their marks instead of drifting back into shape.
    if (ball.isAerialDelivery && !carrier &&
        ball.passByHome != player.isHomeTeam &&
        player.intent != PlayerIntent::CLAIM_LOOSE_BALL)
    {
      target = player.movementTarget;
    }

    if (goalkeeper)
    {
      const bool receivingBackPass =
          ball.isPass && ball.intendedReceiver == player.player &&
          player.intent == PlayerIntent::RECEIVE_PASS;
      player.intent = PlayerIntent::GOALKEEP;
      if (keepers[player.isHomeTeam ? 0 : 1].state == GoalkeeperState::DIVE)
        continue;
      // A keeper receiving a team-mate's pass steps onto the ball's path.
      if (!receivingBackPass) target = goalkeeperTarget(player, carrier);
    }

    // Keepers shuffle constantly to stay on the ball-goal line.
    const bool urgentTarget = player.intent == PlayerIntent::PRESS_BALL ||
                              player.intent == PlayerIntent::CLAIM_LOOSE_BALL ||
                              player.intent == PlayerIntent::GOALKEEP;
    if (player.intent == PlayerIntent::PRESS_BALL &&
        previousIntent != PlayerIntent::PRESS_BALL)
      ++statsOf(player).pressures;
    player.tacticalTarget = target;
    player.urgentMovement = urgentTarget;
  }
}

bool MatchEngine::cutsInside(const MatchPlayer& player) const
{
  using S = MatchTuning::Shape;
  const PlayerRole role = player.player->getRole();
  const bool wideForward = role == PlayerRole::LM || role == PlayerRole::RM ||
                           role == PlayerRole::LW || role == PlayerRole::RW;
  const float depth =
      player.isHomeTeam ? player.position.x : 1.0f - player.position.x;
  // An inside forward always looks to come inside, a touchline winger
  // never does, whatever his natural position.
  const float roleCut = roleProfileOf(player).cutInside;
  if ((!wideForward && roleCut == 0.0f) ||
      depth < MatchTuning::Rules::HOME_FINAL_THIRD_START)
    return false;
  // The choice holds for a few seconds at a time.
  const float share = std::clamp(
      std::clamp(S::CUT_INSIDE_BASE + (player.shooting - player.passing) *
                                          S::CUT_INSIDE_PREFERENCE,
                 S::MIN_CUT_INSIDE, S::MAX_CUT_INSIDE) +
          roleCut * TacticsTuning::CUT_INSIDE_SHIFT,
      0.0f, 1.0f);
  return (epochNoise(player.player->getId(), 0xc0715eU,
                     RUN_TIMING_EPOCH_STEPS) +
          1.0f) *
             0.5f <
         share;
}

bool MatchEngine::isControlled(const MatchPlayer& player) const
{
  return controlledIndex && &player == &players[*controlledIndex];
}

void MatchEngine::setPlayHalfMinutes(int minutes)
{
  pendingPlayHalfMinutes =
      minutes == 0
          ? 0
          : std::clamp(minutes, MatchTuning::Timing::MIN_PLAY_HALF_MINUTES,
                       MatchTuning::Timing::MAX_PLAY_HALF_MINUTES);
  const PlayerID player = getControlledPlayer();
  inputLog.resize(inputCursor);
  inputLog.push_back({stepCounter + 1, player, {}, pendingPlayHalfMinutes});
  ++inputRevision;
}

bool MatchEngine::setControlledPlayer(PlayerID playerId)
{
  if (playerId != 0)
  {
    const auto found = std::find_if(
        players.begin(), players.end(), [&](const MatchPlayer& candidate)
        { return active(candidate) && candidate.player->getId() == playerId; });
    if (found == players.end() || found->isGoalkeeper) return false;
  }
  // Scheduled for the next step, like any other input, so a replay applies it
  // at the same moment.
  inputLog.resize(inputCursor);
  inputLog.push_back(
      {stepCounter + 1, playerId, MatchPlayerInput{}, pendingPlayHalfMinutes});
  ++inputRevision;
  return true;
}

PlayerID MatchEngine::getControlledPlayer() const
{
  // A change scheduled for the next step already counts.
  std::optional<PlayerID> scheduled;
  for (std::size_t index = inputCursor;
       index < inputLog.size() && inputLog[index].step <= stepCounter + 1;
       ++index)
    scheduled = inputLog[index].player;
  if (scheduled) return *scheduled;
  return controlledIndex ? players[*controlledIndex].player->getId() : 0;
}

void MatchEngine::submitInput(const MatchPlayerInput& input)
{
  const PlayerID controlled = getControlledPlayer();
  if (controlled == 0) return;
  inputLog.resize(inputCursor);
  inputLog.push_back(
      {stepCounter + 1, controlled, input, pendingPlayHalfMinutes});
  ++inputRevision;
}

void MatchEngine::loadInputReplay(std::vector<MatchInputRecord> log)
{
  std::stable_sort(
      log.begin(), log.end(),
      [](const MatchInputRecord& first, const MatchInputRecord& second)
      { return first.step < second.step; });
  inputLog = std::move(log);
  inputCursor = 0;
  // Records already due (a replay loaded late) are applied on the next step.
  ++inputRevision;
}

void MatchEngine::applyDueInputs()
{
  while (inputCursor < inputLog.size() &&
         inputLog[inputCursor].step <= stepCounter)
  {
    const MatchInputRecord& record = inputLog[inputCursor++];
    playHalfMinutes =
        record.playHalfMinutes == 0
            ? 0
            : std::clamp(record.playHalfMinutes,
                         MatchTuning::Timing::MIN_PLAY_HALF_MINUTES,
                         MatchTuning::Timing::MAX_PLAY_HALF_MINUTES);
    pendingPlayHalfMinutes = playHalfMinutes;
    // An action fires on the press, not while the button stays held.
    const bool pressed = record.input.action != MatchInputAction::NONE &&
                         record.input.action != lastInputAction;
    lastInputAction = record.input.action;
    // A buffered press survives stick updates but not a change of player.
    const bool samePlayer =
        controlledIndex &&
        players[*controlledIndex].player->getId() == record.player;
    const MatchInputAction pending =
        samePlayer ? controlInput.action : MatchInputAction::NONE;
    const float pendingRemaining = samePlayer ? controlActionRemaining : 0.0f;
    controlledIndex.reset();
    controlInput = {};
    controlActionRemaining = 0.0f;
    if (record.player == 0) continue;
    for (std::size_t slot = 0; slot < players.size(); ++slot)
    {
      const MatchPlayer& candidate = players[slot];
      if (active(candidate) && !candidate.isGoalkeeper &&
          candidate.player->getId() == record.player)
      {
        controlledIndex = slot;
        controlledPlayerId = record.player;
        everControlled[candidate.isHomeTeam ? 0 : 1] = true;
        break;
      }
    }
    if (!controlledIndex) continue;
    controlInput = record.input;
    if (pressed)
    {
      controlActionRemaining = MatchTuning::Control::ACTION_BUFFER_SECONDS;
    }
    else
    {
      // A stick update keeps a buffered press alive.
      controlInput.action = pending;
      controlActionRemaining = pendingRemaining;
    }
    // The body keeps its momentum; the AI resumes from where he stands.
    MatchPlayer& player = players[*controlledIndex];
    player.movementTarget = player.position;
  }
}

void MatchEngine::integrateControlled(MatchPlayer& player, float dt)
{
  using C = MatchTuning::Control;
  const float topSpeed = currentTopSpeed(player);
  const float fatigue =
      std::clamp((1.0f - player.stamina) * INVERSE_FATIGUE_RANGE, 0.0f, 1.0f);
  const MatchPlayer* carrier = findMatchPlayer(ball.possessedBy);
  const bool jockeying = controlInput.jockey && carrier &&
                         carrier->isHomeTeam != player.isHomeTeam;
  float runSpeed =
      controlInput.sprint
          ? topSpeed
          : std::min(topSpeed, player.maxSpeed * C::JOG_SPEED_SHARE);
  if (jockeying)
    runSpeed = std::min(runSpeed, player.maxSpeed * C::JOCKEY_SPEED_SHARE);
  float magnitude = std::sqrt(controlInput.moveX * controlInput.moveX +
                              controlInput.moveY * controlInput.moveY);
  Vector2F direction{0.0f, 0.0f};
  if (std::isfinite(magnitude) && magnitude > EPSILON)
  {
    direction = {controlInput.moveX / magnitude,
                 controlInput.moveY / magnitude};
    magnitude = std::min(magnitude, 1.0f);
  }
  else
  {
    magnitude = 0.0f;
    // With the stick idle he still meets a pass played to him and, while
    // jockeying, keeps his goal-side spot; he arrives without overshooting.
    if (const auto assist = controlledAssistTarget(player))
    {
      const Vector2F offset = toMetres(
          {assist->x - player.position.x, assist->y - player.position.y});
      const float metres = length(offset);
      if (metres > MatchTuning::Player::ARRIVAL_DEAD_ZONE_METRES &&
          runSpeed > EPSILON)
      {
        direction = {offset.x / metres, offset.y / metres};
        magnitude = std::min(
            1.0f, std::sqrt(2.0f * MatchTuning::Player::ARRIVAL_DECELERATION *
                            metres) /
                      runSpeed);
      }
    }
  }
  const float desiredSpeed = magnitude * runSpeed;
  const Vector2F desired{direction.x * desiredSpeed,
                         direction.y * desiredSpeed};
  // Finer sub-steps: the stick is followed with 20 ms kinematics while the
  // AI keeps the common step.
  const float facing = player.facingAngle;
  const float subStep = dt / static_cast<float>(C::PHYSICS_SUBSTEPS);
  for (int step = 0; step < C::PHYSICS_SUBSTEPS; ++step)
    stepKinematics(player, desired, desiredSpeed, topSpeed, fatigue, subStep);
  if (jockeying)
  {
    // A jockeying defender keeps his eyes (and hips) on the ball.
    const float target = fastAtan2(carrier->position.y - player.position.y,
                                   carrier->position.x - player.position.x);
    float difference = target - facing;
    if (difference > std::numbers::pi_v<float>)
      difference -= TWO_PI;
    else if (difference < -std::numbers::pi_v<float>)
      difference += TWO_PI;
    player.targetAngle = target;
    player.facingAngle = facing + std::clamp(difference, -player.turnRate * dt,
                                             player.turnRate * dt);
    if (player.facingAngle > std::numbers::pi_v<float>)
      player.facingAngle -= TWO_PI;
    else if (player.facingAngle < -std::numbers::pi_v<float>)
      player.facingAngle += TWO_PI;
  }
  // When the AI takes him back it picks up the run where his momentum
  // carries him instead of braking to the spot he stands on.
  const Vector2F carry =
      toPitch({player.velocity.x * C::RELEASE_MOMENTUM_SECONDS,
               player.velocity.y * C::RELEASE_MOMENTUM_SECONDS});
  player.movementTarget = {
      std::clamp(player.position.x + carry.x, MatchTuning::Pitch::PLAYER_MIN_X,
                 MatchTuning::Pitch::PLAYER_MAX_X),
      std::clamp(player.position.y + carry.y, MatchTuning::Pitch::PLAYER_MIN_Y,
                 MatchTuning::Pitch::PLAYER_MAX_Y)};
}

std::optional<Vector2F> MatchEngine::controlledAssistTarget(
    const MatchPlayer& player) const
{
  using C = MatchTuning::Control;
  if (ball.isPass && !ball.possessedBy &&
      ball.intendedReceiver == player.player)
  {
    // The point of the ball's path nearest to him, but never behind a
    // moment's travel of the ball: he steps toward it to meet it.
    const Vector2F ballMetres = toMetres(ball.position);
    const Vector2F self = toMetres(player.position);
    const float speedSquared =
        ball.velocity.x * ball.velocity.x + ball.velocity.y * ball.velocity.y;
    float seconds = C::MEET_PASS_LOOKAHEAD_SECONDS;
    if (speedSquared > EPSILON)
      seconds =
          std::clamp(((self.x - ballMetres.x) * ball.velocity.x +
                      (self.y - ballMetres.y) * ball.velocity.y) /
                         speedSquared,
                     C::MEET_PASS_LOOKAHEAD_SECONDS, C::SWITCH_HORIZON_SECONDS);
    const Vector2F ahead =
        toPitch({ball.velocity.x * seconds, ball.velocity.y * seconds});
    return Vector2F{
        std::clamp(ball.position.x + ahead.x, MatchTuning::Pitch::PLAYER_MIN_X,
                   MatchTuning::Pitch::PLAYER_MAX_X),
        std::clamp(ball.position.y + ahead.y, MatchTuning::Pitch::PLAYER_MIN_Y,
                   MatchTuning::Pitch::PLAYER_MAX_Y)};
  }
  if (!controlInput.jockey || !ball.possessedBy) return std::nullopt;
  for (const MatchPlayer& carrier : players)
  {
    if (carrier.player != ball.possessedBy) continue;
    if (carrier.isHomeTeam == player.isHomeTeam) return std::nullopt;
    // Goal-side of the carrier, between him and the centre of our goal.
    const Vector2F toGoal = metricDirection(
        carrier.position,
        {player.isHomeTeam ? 0.0f : 1.0f, MatchTuning::Pitch::CENTRE});
    const Vector2F gap = toPitch({toGoal.x * C::JOCKEY_CONTAIN_METRES,
                                  toGoal.y * C::JOCKEY_CONTAIN_METRES});
    return Vector2F{carrier.position.x + gap.x, carrier.position.y + gap.y};
  }
  return std::nullopt;
}

Vector2F MatchEngine::controlledAim(const MatchPlayer& carrier) const
{
  // The aim stick, else the run stick, else the way he runs or faces.
  Vector2F aim{controlInput.aimX, controlInput.aimY};
  if (aim.x * aim.x + aim.y * aim.y <= EPSILON)
    aim = {controlInput.moveX, controlInput.moveY};
  if (aim.x * aim.x + aim.y * aim.y <= EPSILON) aim = carrier.velocity;
  if (aim.x * aim.x + aim.y * aim.y <= EPSILON)
    aim = {std::cos(carrier.facingAngle) * MatchTuning::Pitch::LENGTH_METRES,
           std::sin(carrier.facingAngle) * MatchTuning::Pitch::WIDTH_METRES};
  return normalized(aim);
}

bool MatchEngine::performControlledAction(MatchPlayer& carrier)
{
  using C = MatchTuning::Control;
  const MatchInputAction action = controlInput.action;
  if (action != MatchInputAction::PASS &&
      action != MatchInputAction::LOFTED_PASS &&
      action != MatchInputAction::THROUGH_BALL &&
      action != MatchInputAction::SHOOT && action != MatchInputAction::CLEAR)
    return false;
  controlInput.action = MatchInputAction::NONE;
  ++controlledActions[carrier.isHomeTeam ? 0 : 1];
  if (action == MatchInputAction::SHOOT)
  {
    // The stick picks the spot on the goal line; his finishing, the
    // pressure and the power still decide where the ball really goes.
    controlledShot = {};
    controlledShot.active = true;
    controlledShot.power = controlInput.power > 0.0f
                               ? std::clamp(controlInput.power, 0.0f, 1.0f)
                               : C::DEFAULT_POWER;
    Vector2F aim{controlInput.aimX, controlInput.aimY};
    if (aim.x * aim.x + aim.y * aim.y <= EPSILON)
      aim = {controlInput.moveX, controlInput.moveY};
    aim = normalized(aim);
    const float forward = carrier.isHomeTeam ? 1.0f : -1.0f;
    // Only a stick pointing at least roughly at the goal aims the shot.
    constexpr float MIN_GOALWARD_AIM = 0.2f;
    if (aim.x * forward > MIN_GOALWARD_AIM)
    {
      const Vector2F from = toMetres(carrier.position);
      const float goalLine = carrier.isHomeTeam ? PITCH_LENGTH : 0.0f;
      const float crossing = from.y + aim.y * (goalLine - from.x) / aim.x;
      controlledShot.aimMetres =
          crossing - MatchTuning::Pitch::CENTRE * PITCH_WIDTH;
    }
    takeShot(carrier);
    controlledShot = {};
    return true;
  }
  if (action == MatchInputAction::CLEAR)
  {
    clearBall(carrier);
    return true;
  }
  playControlledPass(carrier, action);
  return true;
}

MatchPlayer* MatchEngine::controlledPassReceiver(MatchPlayer& carrier,
                                                 Vector2F aim, bool throughBall)
{
  using C = MatchTuning::Control;
  const std::size_t assist = std::min<std::size_t>(
      controlInput.passAssist, C::ASSIST_MIN_ALIGNMENT.size() - 1);
  const float minAlignment = C::ASSIST_MIN_ALIGNMENT[assist];
  const float completionWeight = C::ASSIST_COMPLETION_WEIGHT[assist];
  const float forward = carrier.isHomeTeam ? 1.0f : -1.0f;
  MatchPlayer* receiver = nullptr;
  float bestScore = -std::numeric_limits<float>::infinity();
  for (auto& candidate : players)
  {
    if (&candidate == &carrier || candidate.isHomeTeam != carrier.isHomeTeam ||
        !active(candidate) || (throughBall && candidate.isGoalkeeper))
      continue;
    const Vector2F offset =
        toMetres({candidate.position.x - carrier.position.x,
                  candidate.position.y - carrier.position.y});
    const float separation = length(offset);
    if (separation <= EPSILON) continue;
    const float alignment = (offset.x * aim.x + offset.y * aim.y) / separation;
    if (alignment < minAlignment) continue;
    // Mild assistance: among the team-mates near the aim, the one the ball
    // is likelier to reach. The aim still dominates.
    float score = alignment - separation * C::PASS_DISTANCE_WEIGHT;
    if (completionWeight > 0.0f)
      score += completionWeight *
               evaluatePassOption(carrier, candidate).completionProbability;
    if (throughBall)
      score += std::max(0.0f, offset.x * forward) * C::THROUGH_PROGRESS_WEIGHT +
               (candidate.isMakingRun ? C::THROUGH_RUNNER_BONUS : 0.0f);
    if (score > bestScore)
    {
      bestScore = score;
      receiver = &candidate;
    }
  }
  return receiver;
}

void MatchEngine::playControlledPass(MatchPlayer& carrier,
                                     MatchInputAction action)
{
  using C = MatchTuning::Control;
  using Pitch = MatchTuning::Pitch;
  const Vector2F aim = controlledAim(carrier);
  const bool through = action == MatchInputAction::THROUGH_BALL;
  const bool lofted = action == MatchInputAction::LOFTED_PASS;
  const float power = controlInput.power > 0.0f
                          ? std::clamp(controlInput.power, 0.0f, 1.0f)
                          : C::DEFAULT_POWER;
  const float forward = carrier.isHomeTeam ? 1.0f : -1.0f;
  if (MatchPlayer* receiver = controlledPassReceiver(carrier, aim, through))
  {
    PassOption option = evaluatePassOption(carrier, *receiver);
    if (through)
    {
      // Into the space ahead of his run; a standing receiver gets it ahead
      // of him between the stick and the goal.
      Vector2F run = normalized(receiver->velocity);
      if (length(receiver->velocity) <
          MatchTuning::Player::MOVEMENT_FACING_THRESHOLD)
        run = normalized({aim.x + forward, aim.y});
      const float lead =
          C::THROUGH_LEAD_MIN_METRES +
          (C::THROUGH_LEAD_MAX_METRES - C::THROUGH_LEAD_MIN_METRES) * power;
      const Vector2F reach = toPitch({run.x * lead, run.y * lead});
      option.targetPoint = {
          std::clamp(receiver->position.x + reach.x, Pitch::PLAYER_MIN_X,
                     Pitch::PLAYER_MAX_X),
          std::clamp(receiver->position.y + reach.y, Pitch::PLAYER_MIN_Y,
                     Pitch::PLAYER_MAX_Y)};
      option.passDistance = distance(carrier.position, option.targetPoint);
      option.intent = PassIntent::THROUGH_BALL;
      option.lofted = false;
    }
    passBall(carrier, option, lofted);
    return;
  }
  // Nobody near the aim: the ball is played into space along the stick, for
  // the team-mate nearest the spot to chase. It is still the passer's
  // technique that decides how true it runs.
  const float metres =
      lofted
          ? C::SPACE_LOFT_MIN_METRES +
                (C::SPACE_LOFT_MAX_METRES - C::SPACE_LOFT_MIN_METRES) * power
          : C::SPACE_PASS_MIN_METRES +
                (C::SPACE_PASS_MAX_METRES - C::SPACE_PASS_MIN_METRES) * power;
  const Vector2F reach = toPitch({aim.x * metres, aim.y * metres});
  const Vector2F target{std::clamp(carrier.position.x + reach.x,
                                   Pitch::PLAYER_MIN_X, Pitch::PLAYER_MAX_X),
                        std::clamp(carrier.position.y + reach.y,
                                   Pitch::PLAYER_MIN_Y, Pitch::PLAYER_MAX_Y)};
  MatchPlayer* chaser = nullptr;
  float nearest = std::numeric_limits<float>::infinity();
  for (auto& candidate : players)
  {
    if (&candidate == &carrier || candidate.isHomeTeam != carrier.isHomeTeam ||
        !active(candidate) || candidate.isGoalkeeper)
      continue;
    const float metresAway = distance(candidate.position, target);
    if (metresAway < nearest)
    {
      nearest = metresAway;
      chaser = &candidate;
    }
  }
  if (!chaser)
  {
    clearBall(carrier);
    return;
  }
  PassOption option = evaluatePassOption(carrier, *chaser);
  option.targetPoint = target;
  option.passDistance = distance(carrier.position, target);
  option.progression = (target.x - carrier.position.x) * forward;
  option.intent =
      option.progression >= MatchTuning::Passing::PROGRESSIVE_PASS_MINIMUM
          ? PassIntent::PROGRESSIVE
          : PassIntent::RECYCLE;
  option.lofted = lofted || option.passDistance >
                                MatchTuning::Passing::LOFTED_DISTANCE_METRES;
  passBall(carrier, option, lofted);
}

void MatchEngine::shapeControlledShot(const MatchPlayer& shooter,
                                      float halfGoal, float inset, float& aimY,
                                      float& aimZ, float& spread) const
{
  using C = MatchTuning::Control;
  if (!controlledShot.active || !isControlled(shooter)) return;
  if (controlledShot.aimMetres)
  {
    const float inside = std::max(0.0f, halfGoal - inset);
    aimY = std::clamp(*controlledShot.aimMetres, -inside, inside);
  }
  // A full bar is hit hard but is harder to keep down and on target.
  const float excess =
      std::clamp((controlledShot.power - C::SHOT_OVERPOWER_FROM) /
                     (1.0f - C::SHOT_OVERPOWER_FROM),
                 0.0f, 1.0f);
  aimZ += excess * C::SHOT_OVERPOWER_LIFT_METRES;
  spread *= 1.0f + excess * C::SHOT_OVERPOWER_SPREAD;
}

float MatchEngine::switchSeconds(const MatchPlayer& candidate) const
{
  using C = MatchTuning::Control;
  // Where the ball goes: with its carrier on his run, or rolling to a stop.
  const MatchPlayer* carrier = nullptr;
  if (ball.possessedBy)
    for (const MatchPlayer& player : players)
      if (player.player == ball.possessedBy) carrier = &player;
  const Vector2F start = toMetres(carrier ? carrier->position : ball.position);
  const Vector2F velocity = carrier ? carrier->velocity : ball.velocity;
  const float speed = length(velocity);
  const Vector2F heading =
      speed > EPSILON ? Vector2F{velocity.x / speed, velocity.y / speed}
                      : Vector2F{0.0f, 0.0f};
  const Vector2F self = toMetres(candidate.position);
  const float topSpeed = std::max(currentTopSpeed(candidate), 1.0f);
  const float stopSeconds = carrier ? std::numeric_limits<float>::infinity()
                                    : speed / C::SWITCH_BALL_DECELERATION;
  constexpr int SAMPLES = static_cast<int>(
      C::SWITCH_HORIZON_SECONDS / C::SWITCH_SAMPLE_SECONDS + 0.5f);
  float best = std::numeric_limits<float>::infinity();
  for (int sample = 0; sample <= SAMPLES; ++sample)
  {
    const float seconds = static_cast<float>(sample) * C::SWITCH_SAMPLE_SECONDS;
    const float rolling = std::min(seconds, stopSeconds);
    const float travelled =
        carrier ? speed * seconds
                : speed * rolling -
                      0.5f * C::SWITCH_BALL_DECELERATION * rolling * rolling;
    const Vector2F point{
        std::clamp(start.x + heading.x * travelled, 0.0f, PITCH_LENGTH),
        std::clamp(start.y + heading.y * travelled, 0.0f, PITCH_WIDTH)};
    const float reach = C::SWITCH_REACTION_SECONDS +
                        length({point.x - self.x, point.y - self.y}) / topSpeed;
    best = std::min(best, std::max(seconds, reach));
    // Reachable in time here: no later point can be reached sooner.
    if (reach <= seconds) break;
  }
  // Against a carrier, a defender already goal-side is the one to engage.
  if (carrier && carrier->isHomeTeam != candidate.isHomeTeam &&
      (candidate.isHomeTeam ? candidate.position.x < carrier->position.x
                            : candidate.position.x > carrier->position.x))
    best -= C::SWITCH_GOAL_SIDE_BONUS_SECONDS;
  return best;
}

PlayerID MatchEngine::suggestActivePlayer(bool homeTeam, PlayerID current) const
{
  const auto eligible = [homeTeam](const MatchPlayer& player)
  {
    return active(player) && !player.isGoalkeeper &&
           player.isHomeTeam == homeTeam;
  };
  const MatchPlayer* currentPlayer = nullptr;
  for (const MatchPlayer& player : players)
  {
    if (!eligible(player)) continue;
    if (player.player->getId() == current) currentPlayer = &player;
    // The side's carrier, or the receiver of its pass, is the man.
    if (player.player == ball.possessedBy ||
        (!ball.possessedBy && ball.isPass && ball.passByHome == homeTeam &&
         player.player == ball.intendedReceiver))
      return player.player->getId();
  }
  // The side's keeper has the ball: nobody to chase it, stay with the man.
  if (currentPlayer && ball.possessedBy)
    for (const MatchPlayer& player : players)
      if (player.player == ball.possessedBy && player.isHomeTeam == homeTeam)
        return current;
  PlayerID best = 0;
  float bestSeconds = std::numeric_limits<float>::infinity();
  for (const MatchPlayer& player : players)
  {
    if (!eligible(player)) continue;
    float seconds = switchSeconds(player);
    if (&player == currentPlayer)
      seconds -= MatchTuning::Control::SWITCH_CURRENT_PREFERENCE_SECONDS;
    if (seconds < bestSeconds)
    {
      bestSeconds = seconds;
      best = player.player->getId();
    }
  }
  return best;
}

PlayerID MatchEngine::nextSwitchCandidate(bool homeTeam, PlayerID current,
                                          PlayerID skip) const
{
  PlayerID best = 0;
  PlayerID skipped = 0;
  float bestSeconds = std::numeric_limits<float>::infinity();
  float skippedSeconds = std::numeric_limits<float>::infinity();
  for (const MatchPlayer& player : players)
  {
    if (!active(player) || player.isGoalkeeper ||
        player.isHomeTeam != homeTeam || player.player->getId() == current)
      continue;
    const float seconds = switchSeconds(player);
    if (player.player->getId() == skip)
    {
      skipped = skip;
      skippedSeconds = seconds;
    }
    else if (seconds < bestSeconds)
    {
      bestSeconds = seconds;
      best = player.player->getId();
    }
  }
  // Only the skipped player is left: take him rather than nobody.
  return best != 0 || skippedSeconds == std::numeric_limits<float>::infinity()
             ? best
             : skipped;
}

bool MatchEngine::stepsWithin(float deltaSeconds) const
{
  if (state == MatchState::FULL_TIME || !std::isfinite(deltaSeconds) ||
      deltaSeconds <= 0.0f)
    return false;
  return accumulator +
             std::min(deltaSeconds,
                      MatchTuning::Timing::MAX_FRAME_DELTA_SECONDS) +
             EPSILON >=
         MatchTuning::Timing::FIXED_STEP_SECONDS;
}

[[gnu::always_inline]] inline float MatchEngine::currentTopSpeed(
    const MatchPlayer& player) const
{
  using P = MatchTuning::Player;
  const float fatigue =
      std::clamp((1.0f - player.stamina) * INVERSE_FATIGUE_RANGE, 0.0f, 1.0f);
  const float reserve =
      P::EMPTY_RESERVE_TOP_SPEED +
      (1.0f - P::EMPTY_RESERVE_TOP_SPEED) * player.sprintReserve;
  const float injury =
      player.isInjured ? MatchTuning::Injury::INJURED_SPEED_SCALE : 1.0f;
  return player.maxSpeed * (1.0f - P::FATIGUE_TOP_SPEED_LOSS * fatigue) *
         reserve * injury;
}

// ---------------------------------------------------------------------------
// Roles, opposition instructions and team talks
// ---------------------------------------------------------------------------

namespace
{
const RoleProfile NO_ROLE{};
}  // namespace

void MatchEngine::resolveTactics(bool homeTeam)
{
  // Compile stored slot instructions into match-local numeric profiles. The
  // anchor identifies a formation job, not its current occupant: substitutes
  // inherit that job. Incompatible roles fall back to Standard. Keep this
  // resolution outside the per-player physics loops when adding new roles.
  const std::size_t side = homeTeam ? 0 : 1;
  const Strategy& strategy = homeTeam ? homeStrategy : awayStrategy;
  const std::vector<Vector2F>& anchors = slotAnchors[side];
  for (std::size_t slot = 0; slot < slotTactics[side].size(); ++slot)
  {
    SlotTactic tactic;
    const SlotInstruction* instruction =
        slot < anchors.size() ? strategy.findSlot(anchors[slot]) : nullptr;
    if (instruction)
    {
      const RoleFamily family = Tactics::familyForSlot(anchors[slot]);
      tactic.role = Tactics::allows(family, instruction->role)
                        ? instruction->role
                        : TacticalRole::Standard;
      tactic.profile = Tactics::profile(tactic.role, instruction->duty);
      tactic.possessionOffset = instruction->possessionOffset;
    }
    slotTactics[side][slot] = tactic;
  }
  keeperProfiles[side] =
      Tactics::profile(strategy.getKeeperRole(), RoleDuty::Support);
  oppositionOrders[side] = strategy.getOppositionOrders();
}

TacticalRole MatchEngine::getSlotRole(bool homeTeam, std::size_t slot) const
{
  const auto& tactics = slotTactics[homeTeam ? 0 : 1];
  return slot < tactics.size() ? tactics[slot].role : TacticalRole::Standard;
}

[[gnu::always_inline]] inline const RoleProfile& MatchEngine::roleProfileOf(
    const MatchPlayer& player) const
{
  const std::size_t side = player.isHomeTeam ? 0 : 1;
  if (player.isGoalkeeper) return keeperProfiles[side];
  const auto slot = static_cast<std::size_t>(player.formationSlot);
  if (player.formationSlot < 0 || slot >= slotTactics[side].size())
    return NO_ROLE;
  return slotTactics[side][slot].profile;
}

[[gnu::always_inline]] inline Vector2F MatchEngine::possessionAnchor(
    const MatchPlayer& player) const
{
  const std::size_t side = player.isHomeTeam ? 0 : 1;
  const auto slot = static_cast<std::size_t>(player.formationSlot);
  if (player.isGoalkeeper || player.formationSlot < 0 ||
      slot >= slotTactics[side].size())
    return player.basePosition;
  const SlotTactic& tactic = slotTactics[side][slot];
  if (tactic.possessionOffset.x == 0.0f && tactic.possessionOffset.y == 0.0f &&
      tactic.profile.possessionAdvanceMetres == 0.0f &&
      tactic.profile.possessionWidthMetres == 0.0f)
    return player.basePosition;
  using Pitch = MatchTuning::Pitch;
  const float direction = player.isHomeTeam ? 1.0f : -1.0f;
  const float lateral = player.basePosition.y - Pitch::CENTRE;
  // Width pushes toward his own touchline; a central player has none.
  const float flank = std::abs(lateral) < 0.05f ? 0.0f
                      : lateral > 0.0f          ? 1.0f
                                                : -1.0f;
  Vector2F anchor = player.basePosition;
  anchor.x += direction *
              (tactic.possessionOffset.x +
               tactic.profile.possessionAdvanceMetres / Pitch::LENGTH_METRES);
  anchor.y +=
      tactic.possessionOffset.y +
      flank * tactic.profile.possessionWidthMetres / Pitch::WIDTH_METRES;
  return {std::clamp(anchor.x, Pitch::PLAYER_MIN_X, Pitch::PLAYER_MAX_X),
          std::clamp(anchor.y, Pitch::PLAYER_MIN_Y, Pitch::PLAYER_MAX_Y)};
}

[[gnu::always_inline]] inline float MatchEngine::defensiveRoleShift(
    const MatchPlayer& player) const
{
  return (player.isHomeTeam ? 1.0f : -1.0f) *
         roleProfileOf(player).defensiveAdvanceMetres /
         MatchTuning::Pitch::LENGTH_METRES;
}

OppositionInstruction MatchEngine::instructionAgainst(
    const MatchPlayer& target) const
{
  const auto& orders = oppositionOrders[target.isHomeTeam ? 1 : 0];
  if (orders.empty() || !target.player) return OppositionInstruction::None;
  const PlayerID id = target.player->getId();
  for (const PlayerInstruction& order : orders)
    if (order.player == id) return order.instruction;
  return OppositionInstruction::None;
}

[[gnu::always_inline]] inline const MatchPlayer* MatchEngine::tightMarkTarget(
    const MatchPlayer& marker) const
{
  if (oppositionOrders[marker.isHomeTeam ? 0 : 1].empty()) return nullptr;
  const std::int8_t assigned = markAssignments[slotOf(marker)];
  if (assigned < 0 || static_cast<std::size_t>(assigned) >= players.size())
    return nullptr;
  const MatchPlayer& mark = players[static_cast<std::size_t>(assigned)];
  if (!active(mark) || mark.isHomeTeam == marker.isHomeTeam) return nullptr;
  return instructionAgainst(mark) == OppositionInstruction::TightMark ? &mark
                                                                      : nullptr;
}

Vector2F MatchEngine::tightMarkPoint(const MatchPlayer& marker,
                                     const MatchPlayer& target,
                                     Vector2F current) const
{
  namespace T = TacticsTuning;
  const float direction = marker.isHomeTeam ? 1.0f : -1.0f;
  const Vector2F point{
      target.position.x - direction * T::TIGHT_MARK_GOAL_SIDE_METRES /
                              MatchTuning::Pitch::LENGTH_METRES,
      target.position.y};
  return {current.x + (point.x - current.x) * T::TIGHT_MARK_WEIGHT,
          current.y + (point.y - current.y) * T::TIGHT_MARK_WEIGHT};
}

float MatchEngine::tightMarkPenalty(const MatchPlayer& receiver) const
{
  if (instructionAgainst(receiver) != OppositionInstruction::TightMark)
    return 0.0f;
  for (std::size_t slot = 0; slot < players.size() && slot < 32; ++slot)
  {
    const MatchPlayer& marker = players[slot];
    if (!active(marker) || marker.isHomeTeam == receiver.isHomeTeam ||
        markAssignments[slot] < 0 ||
        &players[static_cast<std::size_t>(markAssignments[slot])] != &receiver)
      continue;
    if (distance(marker.position, receiver.position) <=
        TacticsTuning::TIGHT_MARK_REACH_METRES)
      return TacticsTuning::TIGHT_MARK_COMPLETION_PENALTY *
             MatchTuning::Passing::COMPLETION_UTILITY_WEIGHT;
  }
  return 0.0f;
}

float MatchEngine::passingRoleBias(const MatchPlayer& passer,
                                   const MatchPlayer& receiver,
                                   float progression, float completion) const
{
  namespace T = TacticsTuning;
  const float daring = roleProfileOf(passer).passDaring;
  const float target = roleProfileOf(receiver).targetBias;
  return daring * (progression * T::PASS_DARING_PROGRESS_WEIGHT +
                   (1.0f - completion) * T::PASS_DARING_SAFETY_WEIGHT) +
         target * T::TARGET_BIAS_WEIGHT * (progression > 0.0f ? 1.0f : 0.4f) -
         tightMarkPenalty(receiver);
}

[[gnu::always_inline]] inline float MatchEngine::pressEagerness(
    const MatchPlayer& candidate, const MatchPlayer* carrier) const
{
  if (!carrier || carrier->isHomeTeam == candidate.isHomeTeam) return 0.0f;
  // A rousing talk sends the side after the ball sooner (a flat one later).
  return roleProfileOf(candidate).pressBias *
             TacticsTuning::PRESS_BIAS_SECONDS +
         talkSwing(candidate.isHomeTeam) * TacticsTuning::TALK_PRESS_SECONDS;
}

float MatchEngine::engageBoost(const MatchPlayer& defender,
                               const MatchPlayer& carrier) const
{
  namespace T = TacticsTuning;
  float boost = 1.0f +
                roleProfileOf(defender).pressBias * T::PRESS_BIAS_ENGAGE +
                talkSwing(defender.isHomeTeam) * T::TALK_ENGAGE;
  const OppositionInstruction order = instructionAgainst(carrier);
  if (order == OppositionInstruction::Press) boost += T::PRESS_ORDER_ENGAGE;
  if (order == OppositionInstruction::DoubleUp) boost += T::DOUBLE_UP_ENGAGE;
  return std::max(0.2f, boost);
}

float MatchEngine::pressStandOffShare(const MatchPlayer& presser,
                                      const MatchPlayer& carrier) const
{
  float share = 1.0f - 0.2f * roleProfileOf(presser).pressBias -
                0.2f * talkSwing(presser.isHomeTeam);
  if (instructionAgainst(carrier) == OppositionInstruction::Press)
    share *= TacticsTuning::PRESS_ORDER_STANDOFF_SHARE;
  return std::clamp(share, 0.3f, 1.3f);
}

float MatchEngine::weakFootShade(const MatchPlayer& carrier) const
{
  if (!carrier.player ||
      instructionAgainst(carrier) != OppositionInstruction::ShowWeakFoot)
    return 0.0f;
  // Facing his attacking direction, a right-footer's strong side is +y for
  // the home side (attacking toward x = 1) and -y for the away side.
  const float strongSide =
      (carrier.player->getFoot() == Foot::Right ? 1.0f : -1.0f) *
      (carrier.isHomeTeam ? 1.0f : -1.0f);
  return strongSide * TacticsTuning::WEAK_FOOT_SHADE_METRES /
         MatchTuning::Pitch::WIDTH_METRES;
}

float MatchEngine::weakFootPressure(const MatchPlayer& carrier) const
{
  if (instructionAgainst(carrier) != OppositionInstruction::ShowWeakFoot)
    return 0.0f;
  using D = MatchTuning::Decision;
  return std::clamp(
      (D::PRESSURE_RADIUS_METRES - nearestOpponentDistance(carrier)) /
          D::PRESSURE_RADIUS_METRES,
      0.0f, 1.0f);
}

std::optional<Vector2F> MatchEngine::doubleUpPoint(
    const MatchPlayer& helper, const MatchPlayer& carrier,
    const MatchPlayer* firstPresser) const
{
  if (instructionAgainst(carrier) != OppositionInstruction::DoubleUp)
    return std::nullopt;
  // The second man closes from the side the first presser leaves open,
  // goal-side of the carrier.
  const float lateral =
      firstPresser && firstPresser->position.y > carrier.position.y ? -1.0f
      : firstPresser                                                ? 1.0f
      : helper.position.y <= carrier.position.y                     ? -1.0f
                                                                    : 1.0f;
  const float direction = helper.isHomeTeam ? 1.0f : -1.0f;
  const float reach = TacticsTuning::DOUBLE_UP_SUPPORT_METRES;
  return Vector2F{
      std::clamp(carrier.position.x - direction * reach * 0.5f /
                                          MatchTuning::Pitch::LENGTH_METRES,
                 MatchTuning::Pitch::PLAYER_MIN_X,
                 MatchTuning::Pitch::PLAYER_MAX_X),
      std::clamp(carrier.position.y +
                     lateral * reach / MatchTuning::Pitch::WIDTH_METRES,
                 MatchTuning::Pitch::PLAYER_MIN_Y,
                 MatchTuning::Pitch::PLAYER_MAX_Y)};
}

float MatchEngine::doubleUpPenalty(const MatchPlayer& carrier,
                                   const MatchPlayer& defender) const
{
  if (instructionAgainst(carrier) != OppositionInstruction::DoubleUp)
    return 0.0f;
  for (const MatchPlayer& other : players)
  {
    if (&other == &defender || !active(other) ||
        other.isHomeTeam == carrier.isHomeTeam || other.isGoalkeeper)
      continue;
    if (distance(other.position, carrier.position) <=
        TacticsTuning::DOUBLE_UP_REACH_METRES)
      return TacticsTuning::DOUBLE_UP_TAKE_ON_PENALTY;
  }
  return 0.0f;
}

float MatchEngine::talkSwing(bool homeTeam) const
{
  return std::clamp(talkOf(homeTeam) / TacticsTuning::TALK_REFERENCE, -1.0f,
                    1.0f);
}

float MatchEngine::getTeamTalkEffect(bool homeTeam) const
{
  return talkOf(homeTeam);
}

void MatchEngine::assignMarks()
{
  // Greedy one-to-one assignment of outfield markers to opposing outfield
  // players by channel, depth and danger, so nobody is left unmarked while
  // two defenders shadow the same attacker.
  markAssignments.fill(-1);
  const auto score =
      [](bool markerHome, Vector2F base, float opponentX, float opponentY)
  {
    const float dangerDepth = markerHome ? 1.0f - opponentX : opponentX;
    return std::abs(opponentY - base.y) * MatchTuning::Shape::CHANNEL_WEIGHT +
           std::abs(opponentX - base.x) -
           dangerDepth * MatchTuning::Shape::DANGER_DEPTH_WEIGHT;
  };
  for (const bool home : {true, false})
  {
    std::array<std::uint8_t, 16> markers{};
    std::array<std::uint8_t, 16> opponents{};
    std::size_t markerCount = 0;
    std::size_t opponentCount = 0;
    for (std::size_t slot = 0; slot < players.size() && slot < 32; ++slot)
    {
      const MatchPlayer& candidate = players[slot];
      if (!active(candidate) || candidate.isGoalkeeper) continue;
      if (candidate.isHomeTeam == home && markerCount < markers.size())
        markers[markerCount++] = static_cast<std::uint8_t>(slot);
      else if (candidate.isHomeTeam != home && opponentCount < opponents.size())
        opponents[opponentCount++] = static_cast<std::uint8_t>(slot);
    }
    std::array<float, 16 * 16> scores{};
    std::array<float, 16> opponentX{};
    std::array<float, 16> opponentY{};
    for (std::size_t o = 0; o < opponentCount; ++o)
    {
      opponentX[o] = players[opponents[o]].position.x;
      opponentY[o] = players[opponents[o]].position.y;
    }
    for (std::size_t m = 0; m < markerCount; ++m)
    {
      const MatchPlayer& marker = players[markers[m]];
      const Vector2F base = marker.basePosition;
      for (std::size_t o = 0; o < opponentCount; ++o)
        scores[m * 16 + o] =
            score(marker.isHomeTeam, base, opponentX[o], opponentY[o]);
    }
    // Each round takes the lowest remaining score (ties: lowest marker, then
    // lowest opponent). Every marker keeps its best free opponent, so a round
    // only rescans the markers whose best opponent was just taken.
    std::array<std::uint8_t, 16> bestOf{};
    const auto findBest = [&](std::size_t m, std::uint32_t usedOpponents)
    {
      float best = std::numeric_limits<float>::max();
      std::uint8_t bestOpponent = 0;
      for (std::size_t o = 0; o < opponentCount; ++o)
      {
        if ((usedOpponents & (1U << o)) != 0U) continue;
        if (scores[m * 16 + o] < best)
        {
          best = scores[m * 16 + o];
          bestOpponent = static_cast<std::uint8_t>(o);
        }
      }
      bestOf[m] = bestOpponent;
    };
    std::uint32_t usedMarkers = 0;
    std::uint32_t usedOpponents = 0;
    for (std::size_t m = 0; m < markerCount; ++m) findBest(m, 0U);
    for (std::size_t round = 0; round < std::min(markerCount, opponentCount);
         ++round)
    {
      float best = std::numeric_limits<float>::max();
      std::size_t bestMarker = 0;
      for (std::size_t m = 0; m < markerCount; ++m)
      {
        if ((usedMarkers & (1U << m)) != 0U) continue;
        if (scores[m * 16 + bestOf[m]] < best)
        {
          best = scores[m * 16 + bestOf[m]];
          bestMarker = m;
        }
      }
      const std::uint8_t bestOpponent = bestOf[bestMarker];
      usedMarkers |= 1U << bestMarker;
      usedOpponents |= 1U << bestOpponent;
      markAssignments[markers[bestMarker]] =
          static_cast<std::int8_t>(opponents[bestOpponent]);
      for (std::size_t m = 0; m < markerCount; ++m)
        if ((usedMarkers & (1U << m)) == 0U && bestOf[m] == bestOpponent)
          findBest(m, usedOpponents);
    }
  }
}

// The kinematics kernel runs 8 players at a time where the processor
// supports it; every variant does the same IEEE arithmetic.
#if defined(__x86_64__) && defined(__linux__) && defined(__GNUC__) && \
    !defined(__clang__)
#define FM_KERNEL_CLONES __attribute__((target_clones("avx2", "default")))
#else
#define FM_KERNEL_CLONES
#endif

FM_KERNEL_CLONES void MatchEngine::integrateMovements(const std::uint8_t* slots,
                                                      std::size_t count,
                                                      float dt, bool walking)
{
  using P = MatchTuning::Player;
  using Pitch = MatchTuning::Pitch;
  constexpr float PI = std::numbers::pi_v<float>;
  // The movers are gathered into arrays and advanced with straight-line code
  // (every branch of stepKinematics() evaluated and selected per player), so
  // the compiler can process several players at once. The arithmetic of each
  // player is exactly that of stepKinematics().
  constexpr std::size_t LANES = 32;
  alignas(16) std::array<float, LANES> offsetX;
  alignas(16) std::array<float, LANES> offsetY;
  alignas(16) std::array<float, LANES> velocityX;
  alignas(16) std::array<float, LANES> velocityY;
  alignas(16) std::array<float, LANES> positionX;
  alignas(16) std::array<float, LANES> positionY;
  alignas(16) std::array<float, LANES> facing;
  alignas(16) std::array<std::int32_t, LANES> turning;
  alignas(16) std::array<float, LANES> nextFacing;
  alignas(16) std::array<float, LANES> nextHeading;
  alignas(16) std::array<float, LANES> maxSpeed;
  alignas(16) std::array<float, LANES> stamina;
  alignas(16) std::array<float, LANES> reserve;
  alignas(16) std::array<float, LANES> injury;
  alignas(16) std::array<float, LANES> acceleration;
  alignas(16) std::array<float, LANES> braking;
  alignas(16) std::array<float, LANES> intentScale;
  alignas(16) std::array<float, LANES> talk;
  alignas(16) std::array<float, LANES> shout;
  alignas(16) std::array<float, LANES> turnRate;
  std::array<MatchPlayer*, LANES> movers;
  std::size_t moving = 0;
  for (std::size_t index = 0; index < count; ++index)
  {
    MatchPlayer& player = players[slots[index]];
    Vector2F target = walking ? player.movementTarget : player.tacticalTarget;
    target.x = std::clamp(target.x, Pitch::PLAYER_MIN_X, Pitch::PLAYER_MAX_X);
    target.y = std::clamp(target.y, Pitch::PLAYER_MIN_Y, Pitch::PLAYER_MAX_Y);
    // Players react to a moving target with a perception/intent lag; a side
    // unfamiliar with its tactics reacts later.
    const bool urgent = !walking && player.urgentMovement;
    const float targetBlend =
        targetBlends[(player.isHomeTeam ? 0U : 2U) + (urgent ? 1U : 0U)];
    player.movementTarget.x +=
        (target.x - player.movementTarget.x) * targetBlend;
    player.movementTarget.y +=
        (target.y - player.movementTarget.y) * targetBlend;
    const Vector2F offset =
        toMetres({player.movementTarget.x - player.position.x,
                  player.movementTarget.y - player.position.y});
    // A player standing on his spot stays put (the common case in stoppages).
    if (offset.x * offset.x + offset.y * offset.y <=
            P::ARRIVAL_DEAD_ZONE_METRES * P::ARRIVAL_DEAD_ZONE_METRES &&
        player.velocity.x == 0.0f && player.velocity.y == 0.0f)
      continue;
    const std::size_t lane = moving++;
    movers[lane] = &player;
    offsetX[lane] = offset.x;
    offsetY[lane] = offset.y;
    velocityX[lane] = player.velocity.x;
    velocityY[lane] = player.velocity.y;
    positionX[lane] = player.position.x;
    positionY[lane] = player.position.y;
    facing[lane] = player.facingAngle;
    maxSpeed[lane] = player.maxSpeed;
    stamina[lane] = player.stamina;
    reserve[lane] = player.sprintReserve;
    injury[lane] =
        player.isInjured ? MatchTuning::Injury::INJURED_SPEED_SCALE : 1.0f;
    acceleration[lane] = player.acceleration;
    braking[lane] = player.braking;
    intentScale[lane] = walking ? P::RESTART_WALK_SPEED_SCALE
                                : movementSpeedScale(player.intent);
    talk[lane] = talkOf(player.isHomeTeam);
    shout[lane] = shoutWorkRate(player.isHomeTeam);
    turnRate[lane] = player.turnRate;
  }

  for (std::size_t lane = 0; lane < moving; ++lane)
  {
    const float targetX = offsetX[lane];
    const float targetY = offsetY[lane];
    const float targetDistance =
        std::sqrt(targetX * targetX + targetY * targetY);
    const float fatigue =
        std::clamp((1.0f - stamina[lane]) * INVERSE_FATIGUE_RANGE, 0.0f, 1.0f);
    const float topSpeed =
        maxSpeed[lane] * (1.0f - P::FATIGUE_TOP_SPEED_LOSS * fatigue) *
        (P::EMPTY_RESERVE_TOP_SPEED +
         (1.0f - P::EMPTY_RESERVE_TOP_SPEED) * reserve[lane]) *
        injury[lane];
    // Players jog while in position but run hard to recover a lost position.
    // Intent speeds are shares of the fresh top speed; tired players choose
    // to run a little less hard and lose the top end.
    const float urgency =
        walking
            ? intentScale[lane]
            : std::max(intentScale[lane],
                       std::min(P::MAX_URGENCY_SPEED_SCALE,
                                targetDistance / P::URGENCY_DISTANCE_METRES));
    const float speedCap =
        std::min(topSpeed, maxSpeed[lane] *
                               (1.0f - P::FATIGUE_WORK_RATE_LOSS * fatigue +
                                talk[lane] + shout[lane]) *
                               urgency);
    // Arrive without overshooting: the desired speed falls off so a
    // comfortable deceleration stops the player on the target.
    const float approach = targetDistance - P::ARRIVAL_DEAD_ZONE_METRES;
    const float arrivalSpeed =
        std::sqrt(2.0f * P::ARRIVAL_DECELERATION * std::max(approach, 0.0f));
    const float desiredSpeed =
        approach <= 0.0f ? 0.0f : std::min(speedCap, arrivalSpeed);
    // Quotients are formed for every lane and discarded where unused.
    const bool aimed = targetDistance > EPSILON;
    const float directionX = targetX / targetDistance;
    const float directionY = targetY / targetDistance;
    const float desiredX = (aimed ? directionX : 0.0f) * desiredSpeed;
    const float desiredY = (aimed ? directionY : 0.0f) * desiredSpeed;

    // Acceleration-speed profile a(v) = A0 (1 - v / vmax), stronger braking,
    // and a lateral limit that forces a player to slow down to turn sharply.
    const float push =
        acceleration[lane] * (1.0f - P::FATIGUE_ACCELERATION_LOSS * fatigue);
    const float oldX = velocityX[lane];
    const float oldY = velocityY[lane];
    const float speed = std::sqrt(oldX * oldX + oldY * oldY);
    const float changeX = desiredX - oldX;
    const float changeY = desiredY - oldY;
    const float brake = braking[lane];
    const float drive = push * std::max(0.05f, 1.0f - speed / topSpeed);
    // Starting from (almost) a standstill: move straight toward the target.
    const float limit = (desiredSpeed >= speed ? drive : brake) * dt;
    const float magnitude = std::sqrt(changeX * changeX + changeY * changeY);
    const float shortfall = limit / magnitude;
    const float scale = magnitude > limit ? shortfall : 1.0f;
    const float slowX = oldX + changeX * scale;
    const float slowY = oldY + changeY * scale;
    // On the move: accelerate or brake along the run, turn within the limit.
    const float tangentX = oldX / speed;
    const float tangentY = oldY / speed;
    const float normalX = -tangentY;
    const float normalY = tangentX;
    float along = changeX * tangentX + changeY * tangentY;
    float across = changeX * normalX + changeY * normalY;
    along = along > 0.0f ? std::min(along, drive * dt)
                         : std::max(along, -brake * dt);
    const float speedShare = std::clamp(speed / maxSpeed[lane], 0.0f, 1.0f);
    const float lateral =
        (P::LATERAL_ACCELERATION_SLOW +
         (P::LATERAL_ACCELERATION_FAST - P::LATERAL_ACCELERATION_SLOW) *
             speedShare) *
        dt;
    across = std::clamp(across, -lateral, lateral);
    const float fastX = oldX + (tangentX * along + normalX * across);
    const float fastY = oldY + (tangentY * along + normalY * across);
    const bool stop = (desiredSpeed == 0.0f) & (speed <= brake * dt);
    float nextVelocityX = stop ? 0.0f : speed < 0.5f ? slowX : fastX;
    float nextVelocityY = stop ? 0.0f : speed < 0.5f ? slowY : fastY;
    const float squaredSpeed =
        nextVelocityX * nextVelocityX + nextVelocityY * nextVelocityY;
    const float capScale = topSpeed / std::sqrt(squaredSpeed);
    const bool capped = squaredSpeed > topSpeed * topSpeed;
    nextVelocityX = capped ? nextVelocityX * capScale : nextVelocityX;
    nextVelocityY = capped ? nextVelocityY * capScale : nextVelocityY;

    const float nextX = positionX[lane] + nextVelocityX * dt * INVERSE_LENGTH;
    const float nextY = positionY[lane] + nextVelocityY * dt * INVERSE_WIDTH;
    const float clampedX =
        std::clamp(nextX, Pitch::PLAYER_MIN_X, Pitch::PLAYER_MAX_X);
    const float clampedY =
        std::clamp(nextY, Pitch::PLAYER_MIN_Y, Pitch::PLAYER_MAX_Y);
    nextVelocityX = clampedX != nextX ? 0.0f : nextVelocityX;
    nextVelocityY = clampedY != nextY ? 0.0f : nextVelocityY;
    positionX[lane] = clampedX;
    positionY[lane] = clampedY;
    velocityX[lane] = nextVelocityX;
    velocityY[lane] = nextVelocityY;

    // Facing (normalised pitch space, for the renderers) turns toward the
    // run at a limited rate; fastAtan2() without branches.
    const float y = nextVelocityY * INVERSE_WIDTH;
    const float x = nextVelocityX * INVERSE_LENGTH;
    const float absX = std::abs(x);
    const float absY = std::abs(y);
    const float ratio = std::min(absX, absY) / std::max(absX, absY);
    const float ratioSquared = ratio * ratio;
    float angle =
        ((-0.0464964749f * ratioSquared + 0.15931422f) * ratioSquared -
         0.327622764f) *
            ratioSquared * ratio +
        ratio;
    angle = absY > absX ? 1.57079637f - angle : angle;
    angle = x < 0.0f ? 3.14159274f - angle : angle;
    angle = y < 0.0f ? -angle : angle;
    angle = std::max(absX, absY) < 1e-12f ? 0.0f : angle;
    float difference = angle - facing[lane];
    difference = difference > PI    ? difference - TWO_PI
                 : difference < -PI ? difference + TWO_PI
                                    : difference;
    const float turn = turnRate[lane] * dt;
    float turned = facing[lane] + std::clamp(difference, -turn, turn);
    turned = turned > PI    ? turned - TWO_PI
             : turned < -PI ? turned + TWO_PI
                            : turned;
    const bool running = squaredSpeed > P::MOVEMENT_FACING_THRESHOLD *
                                            P::MOVEMENT_FACING_THRESHOLD;
    nextHeading[lane] = angle;
    turning[lane] = running ? 1 : 0;
    nextFacing[lane] = running ? turned : facing[lane];
  }

  for (std::size_t lane = 0; lane < moving; ++lane)
  {
    MatchPlayer& player = *movers[lane];
    player.velocity = {velocityX[lane], velocityY[lane]};
    player.position = {positionX[lane], positionY[lane]};
    player.facingAngle = nextFacing[lane];
    if (turning[lane] != 0) player.targetAngle = nextHeading[lane];
  }
}

[[gnu::always_inline]] inline void MatchEngine::stepKinematics(
    MatchPlayer& player, Vector2F desired, float desiredSpeed, float topSpeed,
    float fatigue, float dt)
{
  using P = MatchTuning::Player;
  // Acceleration-speed profile a(v) = A0 (1 - v / vmax), stronger braking,
  // and a lateral limit that forces a player to slow down to turn sharply.
  const float acceleration =
      player.acceleration * (1.0f - P::FATIGUE_ACCELERATION_LOSS * fatigue);
  Vector2F velocity = player.velocity;
  const float speed = length(velocity);
  const Vector2F change{desired.x - velocity.x, desired.y - velocity.y};
  if (desiredSpeed == 0.0f && speed <= player.braking * dt)
  {
    velocity = {0.0f, 0.0f};
  }
  else if (speed < 0.5f)
  {
    const float limit =
        (desiredSpeed >= speed
             ? acceleration * std::max(0.05f, 1.0f - speed / topSpeed)
             : player.braking) *
        dt;
    const float magnitude = length(change);
    const float shortfall = limit / magnitude;
    const float scale = magnitude > limit ? shortfall : 1.0f;
    velocity.x += change.x * scale;
    velocity.y += change.y * scale;
  }
  else
  {
    const Vector2F tangent{velocity.x / speed, velocity.y / speed};
    const Vector2F normal{-tangent.y, tangent.x};
    float along = change.x * tangent.x + change.y * tangent.y;
    float across = change.x * normal.x + change.y * normal.y;
    along =
        along > 0.0f
            ? std::min(along, acceleration *
                                  std::max(0.05f, 1.0f - speed / topSpeed) * dt)
            : std::max(along, -player.braking * dt);
    const float speedShare = std::clamp(speed / player.maxSpeed, 0.0f, 1.0f);
    const float lateral =
        (P::LATERAL_ACCELERATION_SLOW +
         (P::LATERAL_ACCELERATION_FAST - P::LATERAL_ACCELERATION_SLOW) *
             speedShare) *
        dt;
    across = std::clamp(across, -lateral, lateral);
    velocity.x += tangent.x * along + normal.x * across;
    velocity.y += tangent.y * along + normal.y * across;
  }
  const float squaredSpeed = velocity.x * velocity.x + velocity.y * velocity.y;
  if (squaredSpeed > topSpeed * topSpeed)
  {
    const float scale = topSpeed / std::sqrt(squaredSpeed);
    velocity.x *= scale;
    velocity.y *= scale;
  }
  player.velocity = velocity;

  const Vector2F step = toPitch({velocity.x * dt, velocity.y * dt});
  const float nextX = player.position.x + step.x;
  const float nextY = player.position.y + step.y;
  player.position.x = std::clamp(nextX, MatchTuning::Pitch::PLAYER_MIN_X,
                                 MatchTuning::Pitch::PLAYER_MAX_X);
  player.position.y = std::clamp(nextY, MatchTuning::Pitch::PLAYER_MIN_Y,
                                 MatchTuning::Pitch::PLAYER_MAX_Y);
  if (player.position.x != nextX) player.velocity.x = 0.0f;
  if (player.position.y != nextY) player.velocity.y = 0.0f;

  if (squaredSpeed >
      P::MOVEMENT_FACING_THRESHOLD * P::MOVEMENT_FACING_THRESHOLD)
  {
    // Facing is kept in normalised pitch space for the renderers.
    player.targetAngle = fastAtan2(player.velocity.y * INVERSE_WIDTH,
                                   player.velocity.x * INVERSE_LENGTH);
    float angleDifference = player.targetAngle - player.facingAngle;
    if (angleDifference > std::numbers::pi_v<float>)
      angleDifference -= TWO_PI;
    else if (angleDifference < -std::numbers::pi_v<float>)
      angleDifference += TWO_PI;
    player.facingAngle += std::clamp(angleDifference, -player.turnRate * dt,
                                     player.turnRate * dt);
    if (player.facingAngle > std::numbers::pi_v<float>)
      player.facingAngle -= TWO_PI;
    else if (player.facingAngle < -std::numbers::pi_v<float>)
      player.facingAngle += TWO_PI;
  }
}

void MatchEngine::updateRestartSupport(MatchPlayer& player,
                                       const MatchPlayer* taker)
{
  if (player.isGoalkeeper || &player == taker || restartIsSetPiece ||
      (state != MatchState::THROW_IN && state != MatchState::GOAL_KICK))
    return;

  player.movementTarget = player.basePosition;
  if (taker && player.isHomeTeam == taker->isHomeTeam)
  {
    const float gap = distance(player.position, ball.position);
    if (gap < MatchTuning::Timing::RESTART_SUPPORT_DISTANCE_METRES)
    {
      // Offer a lane inside the pitch, with teammates spread along it.
      player.movementTarget.x = std::clamp(
          ball.position.x + (player.basePosition.x - ball.position.x) *
                                MatchTuning::Timing::RESTART_SUPPORT_BLEND,
          MatchTuning::Pitch::PLAYER_MIN_X, MatchTuning::Pitch::PLAYER_MAX_X);
      player.movementTarget.y = std::clamp(
          ball.position.y + (player.basePosition.y - ball.position.y) *
                                MatchTuning::Timing::RESTART_SUPPORT_BLEND,
          MatchTuning::Pitch::PLAYER_MIN_Y, MatchTuning::Pitch::PLAYER_MAX_Y);
    }
  }
  else if (taker)
  {
    const float gap = distance(player.movementTarget, ball.position);
    const float minimum =
        state == MatchState::THROW_IN
            ? MatchTuning::Timing::THROW_OPPONENT_DISTANCE_METRES
            : MatchTuning::Timing::KICK_OPPONENT_DISTANCE_METRES;
    if (gap < minimum)
    {
      const Vector2F direction =
          metricDirection(ball.position, player.movementTarget);
      const Vector2F offset =
          toPitch({direction.x * minimum, direction.y * minimum});
      player.movementTarget = {ball.position.x + offset.x,
                               ball.position.y + offset.y};
    }
  }
}

void MatchEngine::updateRestartMovement(float dt)
{
  // During a stoppage players walk to their restart positions (set-piece
  // slots, the restart spot or their last tactical target).
  const MatchPlayer* taker = restartTaker();
  std::array<std::uint8_t, 32> batch;
  std::size_t batched = 0;
  for (std::size_t slot = 0; slot < players.size() && slot < batch.size();
       ++slot)
  {
    MatchPlayer& player = players[slot];
    if (!active(player)) continue;
    if (player.isGoalkeeper && &player != taker)
    {
      keepers[player.isHomeTeam ? 0 : 1].state = GoalkeeperState::SET_POSITION;
      if (!restartIsSetPiece && state != MatchState::GOAL)
        player.movementTarget = goalkeeperTarget(player, nullptr);
    }
    updateRestartSupport(player, taker);
    player.intent = player.isGoalkeeper ? PlayerIntent::GOALKEEP
                                        : PlayerIntent::RECOVER_SHAPE;
    batch[batched++] = static_cast<std::uint8_t>(slot);
  }
  integrateMovements(batch.data(), batched, dt, true);
  separatePlayers();
}

void MatchEngine::separatePlayers()
{
  // Resolve body contact after movement: players cannot overlap. Players who
  // left the pitch are static obstacles. Pairs are swept along the length of
  // the pitch so only neighbours within a body width are examined.
  constexpr float MINIMUM = MatchTuning::Player::MINIMUM_BODY_SEPARATION_METRES;
  constexpr float MINIMUM_X = MINIMUM / MatchTuning::Pitch::LENGTH_METRES;
  // The order persists between steps: players move little per step, so the
  // insertion sort is nearly linear. Ties break on the slot index, so the
  // result does not depend on the previous order.
  auto& order = separationOrder;
  const std::size_t count = std::min(players.size(), order.size());
  // Nobody moved since a pass that found no contact: nothing to resolve.
  bool unchanged = separationClear && separationCount == count;
  for (std::size_t index = 0; index < count && unchanged; ++index)
    unchanged = separationPositions[index].x == players[index].position.x &&
                separationPositions[index].y == players[index].position.y;
  if (unchanged) return;
  if (separationCount != count)
  {
    separationCount = count;
    for (std::size_t index = 0; index < count; ++index)
      order[index] = static_cast<std::uint8_t>(index);
  }
  std::array<float, 32> xs;
  for (std::size_t index = 0; index < count; ++index)
    xs[index] = players[index].position.x;
  separationClear = true;
  insertionSort(order, count,
                [&xs](std::uint8_t first, std::uint8_t second)
                {
                  if (xs[first] != xs[second]) return xs[first] < xs[second];
                  return first < second;
                });
  const auto push = [&xs, this](MatchPlayer& player, Vector2F offset)
  {
    player.position.x = std::clamp(player.position.x + offset.x,
                                   MatchTuning::Pitch::PLAYER_MIN_X,
                                   MatchTuning::Pitch::PLAYER_MAX_X);
    player.position.y = std::clamp(player.position.y + offset.y,
                                   MatchTuning::Pitch::PLAYER_MIN_Y,
                                   MatchTuning::Pitch::PLAYER_MAX_Y);
    player.velocity.x *= MatchTuning::Player::BODY_COLLISION_VELOCITY_RETAINED;
    player.velocity.y *= MatchTuning::Player::BODY_COLLISION_VELOCITY_RETAINED;
    xs[slotOf(player)] = player.position.x;
  };
  for (std::size_t firstIndex = 0; firstIndex < count; ++firstIndex)
  {
    MatchPlayer& first = players[order[firstIndex]];
    for (std::size_t secondIndex = firstIndex + 1; secondIndex < count;
         ++secondIndex)
    {
      if (xs[order[secondIndex]] - xs[order[firstIndex]] >= MINIMUM_X) break;
      MatchPlayer& second = players[order[secondIndex]];
      const bool firstMoves = first.onPitch;
      const bool secondMoves = second.onPitch;
      if (!firstMoves && !secondMoves) continue;
      Vector2F separationMetres =
          toMetres({second.position.x - first.position.x,
                    second.position.y - first.position.y});
      const float squared = separationMetres.x * separationMetres.x +
                            separationMetres.y * separationMetres.y;
      if (squared >= MINIMUM * MINIMUM) continue;
      float separationLengthMetres = std::sqrt(squared);
      if (separationLengthMetres <= EPSILON)
      {
        separationMetres = {0.0f, order[firstIndex] % 2 == 0 ? 1.0f : -1.0f};
        separationLengthMetres = 1.0f;
      }
      const float share = firstMoves && secondMoves
                              ? MatchTuning::Player::BODY_SEPARATION_SHARE
                              : 1.0f;
      const float correctionMetres = (MINIMUM - separationLengthMetres) * share;
      const Vector2F correction = toPitch(
          {separationMetres.x / separationLengthMetres * correctionMetres,
           separationMetres.y / separationLengthMetres * correctionMetres});
      if (firstMoves) push(first, {-correction.x, -correction.y});
      if (secondMoves) push(second, correction);
      separationClear = false;
    }
  }
  for (std::size_t index = 0; index < count; ++index)
    separationPositions[index] = players[index].position;
}

void MatchEngine::accumulatePlayerLoad(float dt)
{
  // Measure actual travelled distance, then update the slow condition pool,
  // the fast sprint reserve and report totals together. Match-clock minutes
  // can be accelerated in Play, but physical exertion uses simulated seconds.
  // The final pass rejects restart teleports before applying load or fatigue.
  using L = MatchTuning::Load;
  using F = MatchTuning::Fatigue;
  const float clockScale = matchClockRate();
  const float clockDelta =
      dt * clockScale / MatchTuning::Timing::SECONDS_PER_MINUTE;
  for (const auto& player : players)
    if (active(player)) statsOf(player).minutesPlayed += clockDelta;
  // Distance, intensity and energy are accounted over two steps at a time.
  loadSeconds += dt;
  if (loadAnchors.size() != players.size())
  {
    loadAnchors.resize(players.size());
    for (std::size_t index = 0; index < players.size(); ++index)
      loadAnchors[index] = index < previousPlayerPositions.size()
                               ? previousPlayerPositions[index]
                               : players[index].position;
  }
  if (!periodElapsed(MatchTuning::Timing::TACTICAL_REFRESH_STEPS)) return;
  dt = loadSeconds;
  loadSeconds = 0.0f;
  // Gathered into arrays so the energy model below runs as straight-line
  // (vectorisable) code; the statistics are then updated per player.
  constexpr std::size_t LANES = 32;
  alignas(16) std::array<float, LANES> fromX;
  alignas(16) std::array<float, LANES> fromY;
  alignas(16) std::array<float, LANES> toX;
  alignas(16) std::array<float, LANES> toY;
  alignas(16) std::array<float, LANES> topSpeed;
  alignas(16) std::array<float, LANES> endurance;
  alignas(16) std::array<float, LANES> pressingEffort;
  alignas(16) std::array<float, LANES> sideDrain;
  alignas(16) std::array<float, LANES> stamina;
  alignas(16) std::array<float, LANES> reserve;
  alignas(16) std::array<float, LANES> stepMetres;
  alignas(16) std::array<float, LANES> nextStamina;
  alignas(16) std::array<float, LANES> nextReserve;
  std::array<std::uint8_t, LANES> slots;
  std::size_t count = 0;
  for (std::size_t index = 0; index < players.size() && index < LANES; ++index)
  {
    MatchPlayer& player = players[index];
    const Vector2F previous = loadAnchors[index];
    loadAnchors[index] = player.position;
    if (!active(player)) continue;
    // A pressing side pays for it: the players hunting the ball most, and
    // the whole side while out of possession.
    const float pressing = getEffectiveSliders(player.isHomeTeam).pressing;
    const bool outOfPossession =
        lastControlledTeamHome && *lastControlledTeamHome != player.isHomeTeam;
    const std::size_t lane = count++;
    slots[lane] = static_cast<std::uint8_t>(index);
    fromX[lane] = previous.x;
    fromY[lane] = previous.y;
    toX[lane] = player.position.x;
    toY[lane] = player.position.y;
    topSpeed[lane] = player.maxSpeed;
    endurance[lane] = player.endurance;
    pressingEffort[lane] = player.isPressing ? pressing : 0.0f;
    sideDrain[lane] =
        outOfPossession ? std::max(0.0f, pressing - F::TEAM_PRESSING_NEUTRAL) *
                              F::TEAM_PRESSING_DRAIN
                        : 0.0f;
    stamina[lane] = player.stamina;
    reserve[lane] = player.sprintReserve;
  }
  for (std::size_t lane = 0; lane < count; ++lane)
  {
    const float metresX = (fromX[lane] - toX[lane]) * PITCH_LENGTH;
    const float metresY = (fromY[lane] - toY[lane]) * PITCH_WIDTH;
    const float metres = std::sqrt(metresX * metresX + metresY * metresY);
    const float speed = metres / dt;
    const float drain = MatchRules::staminaDrainPerSecond(
                            speed / std::max(topSpeed[lane], EPSILON),
                            endurance[lane], pressingEffort[lane]) +
                        sideDrain[lane];
    stepMetres[lane] = metres;
    nextStamina[lane] = std::clamp(stamina[lane] - drain * dt,
                                   MatchTuning::Player::MINIMUM_STAMINA, 1.0f);
    // Repeat-sprint reserve: emptied by high-intensity running, refilled
    // while jogging or walking.
    const float intensity = std::clamp(
        (speed - L::HIGH_INTENSITY_SPEED) /
            std::max(topSpeed[lane] - L::HIGH_INTENSITY_SPEED, EPSILON),
        0.0f, 1.0f);
    const float drained =
        std::max(0.0f, reserve[lane] - F::RESERVE_DRAIN_PER_SECOND * intensity *
                                           intensity * dt);
    const float refilled = reserve[lane] + (1.0f - reserve[lane]) * dt /
                                               F::RESERVE_RECOVERY_SECONDS;
    nextReserve[lane] = speed > L::HIGH_INTENSITY_SPEED ? drained : refilled;
  }
  for (std::size_t lane = 0; lane < count; ++lane)
  {
    MatchPlayer& player = players[slots[lane]];
    const float metres = stepMetres[lane];
    // Restart repositioning teleports are not running.
    if (metres > MatchTuning::Player::MAX_COUNTED_STEP_SPEED * dt) continue;
    const float speed = metres / dt;
    PlayerMatchStats& entry = statsOf(player);
    entry.distanceMetres += metres;
    if (period == 2) entry.secondHalfDistanceMetres += metres;
    // Running speed, not the displacement from body contact.
    if (speed > entry.topSpeed)
      entry.topSpeed =
          std::max(entry.topSpeed, std::min(speed, length(player.velocity)));
    if (speed >= L::HIGH_INTENSITY_SPEED)
    {
      entry.highIntensityMetres += metres;
      if (period == 2) entry.secondHalfHighIntensityMetres += metres;
    }
    if (speed >= L::SPRINT_SPEED)
    {
      entry.sprintMetres += metres;
      if (!player.isSprinting) ++entry.sprints;
      player.isSprinting = true;
    }
    else if (speed < L::SPRINT_EXIT_SPEED)
    {
      player.isSprinting = false;
    }
    player.stamina = nextStamina[lane];
    player.sprintReserve = nextReserve[lane];
  }
}

void MatchEngine::resolvePossessionAndActions(float dt)
{
  // Owning the ball is not an unconditional right to act: first move the
  // dribble touch, then allow challenges, then check touch reach/cooldowns.
  // A tackle may transfer ownership or stop play, so return immediately when
  // that happens instead of letting the former carrier act with a stale ball.
  MatchPlayer* carrier = findMatchPlayer(ball.possessedBy);
  if (!carrier) return;
  ball.lastPossessor = carrier->player;
  if (!updateDribble(*carrier, dt)) return;

  // The controlled player tackles only on the button, when in reach.
  if (controlledIndex && controlInput.action != MatchInputAction::NONE)
  {
    MatchPlayer& controlled = players[*controlledIndex];
    const bool sliding = controlInput.action == MatchInputAction::SLIDE_TACKLE;
    if ((sliding || controlInput.action == MatchInputAction::TACKLE) &&
        active(controlled) && controlled.isHomeTeam != carrier->isHomeTeam &&
        controlled.tackleCooldown <= 0.0f && !controlled.isInjured)
    {
      using D = MatchTuning::Defending;
      const float reach =
          sliding ? D::SLIDE_TACKLE_DISTANCE_METRES
                  : D::TACKLE_DISTANCE_METRES +
                        controlled.defending * D::TACKLE_DEFENDING_REACH_METRES;
      if (distance(controlled.position, ball.position) <= reach)
      {
        controlInput.action = MatchInputAction::NONE;
        ++controlledActions[controlled.isHomeTeam ? 0 : 1];
        attemptTackle(*carrier, controlled, sliding);
        if (ball.possessedBy != carrier->player || state != MatchState::PLAYING)
          return;
      }
    }
  }

  // A keeper holding the ball in his hands cannot be challenged.
  const bool inHands = carrier->isGoalkeeper &&
                       inPenaltyArea(carrier->position, carrier->isHomeTeam);
  // The nearest defender to the ball (not to the man) decides whether to
  // challenge. He waits for the ball to be away from the attacker's foot;
  // near his own box and with a pressing instruction he goes in sooner.
  MatchPlayer* defender =
      inHands ? nullptr
              : findClosestPlayer(ball.position, !carrier->isHomeTeam, false);
  if (defender && defender->tackleCooldown <= 0.0f && !defender->isInjured &&
      defender->trapTimer <= 0.0f && !isControlled(*defender))
  {
    using D = MatchTuning::Defending;
    const float reach = D::TACKLE_DISTANCE_METRES +
                        defender->defending * D::TACKLE_DEFENDING_REACH_METRES;
    const float ballDistance = distance(defender->position, ball.position);
    if (ballDistance < D::SLIDE_TACKLE_DISTANCE_METRES)
    {
      const float carrierDepth = carrier->isHomeTeam
                                     ? carrier->position.x
                                     : 1.0f - carrier->position.x;
      const float engageRate =
          D::ENGAGE_RATE_PER_SECOND *
          (1.0f + getEffectiveSliders(defender->isHomeTeam).pressing *
                      D::PRESSING_ENGAGE_BONUS) *
          (carrierDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START
               ? D::FINAL_THIRD_ENGAGE_FACTOR
               : 1.0f) *
          (D::CLOSE_CONTROL_ENGAGE_SHARE +
           D::EXPOSED_ENGAGE_SHARE * dribbleExposureShare()) *
          (ballDistance > reach ? 0.5f : 1.0f) *
          engageBoost(*defender, *carrier);
      if (randomFloat(0.0f, 1.0f) < 1.0f - std::exp(-engageRate * dt))
      {
        attemptTackle(*carrier, *defender, ballDistance > reach);
        if (ball.possessedBy != carrier->player || state != MatchState::PLAYING)
          return;
      }
    }
  }

  // The AI waits out its decision time after a touch; a controlled player
  // can play the ball as soon as it is under control.
  const bool controlled = isControlled(*carrier);
  if ((carrier->actionCooldown <= 0.0f ||
       (controlled && carrier->trapTimer <= 0.0f)) &&
      ball.dribbleExposure <= MatchTuning::Dribble::KICK_REACH_METRES)
  {
    if (controlled)
      performControlledAction(*carrier);
    else
      decideAction(*carrier);
  }
}

float MatchEngine::dribbleExposureShare() const
{
  using R = MatchTuning::Dribble;
  const float range = ball.dribbleTouchLength - R::FEET_METRES;
  if (range <= EPSILON) return 0.0f;
  return std::clamp((ball.dribbleExposure - R::FEET_METRES) / range, 0.0f,
                    1.0f);
}

bool MatchEngine::updateDribble(MatchPlayer& carrier, float dt)
{
  using R = MatchTuning::Dribble;
  const bool inHands = carrier.isGoalkeeper &&
                       inPenaltyArea(carrier.position, carrier.isHomeTeam);
  const float speed = length(carrier.velocity);
  if (inHands)
  {
    ball.dribbleExposure = 0.0f;
    ball.dribbleTouchLength = 0.0f;
  }
  else if (speed < R::CLOSE_CONTROL_SPEED)
  {
    // Close control: the ball is kept at the feet, shielded by the body.
    ball.dribbleExposure = R::FEET_METRES;
    ball.dribbleTouchLength = 0.0f;
    ball.touchTimer = 0.0f;
    if (speed > EPSILON)
      ball.dribbleDirection = {carrier.velocity.x / speed,
                               carrier.velocity.y / speed};
  }
  else
  {
    ball.touchTimer -= dt;
    if (ball.touchTimer <= 0.0f)
    {
      // A touch pushes the ball ahead along the run; faster runs and poorer
      // dribblers push it further, and pressure risks a heavy touch.
      ball.dribbleDirection = {carrier.velocity.x / speed,
                               carrier.velocity.y / speed};
      const float pressure =
          std::clamp(1.0f - nearestOpponentDistance(carrier) /
                                R::HEAVY_TOUCH_PRESSURE_METRES,
                     0.0f, 1.0f);
      const float heavyChance = R::HEAVY_TOUCH_BASE *
                                (1.0f - carrier.dribbling) *
                                std::min(1.0f, speed / carrier.maxSpeed) *
                                (1.0f + pressure * R::HEAVY_TOUCH_PRESSURE) *
                                executionErrorScale(carrier);
      if (randomFloat(0.0f, 1.0f) < heavyChance)
      {
        const float push = speed + randomFloat(R::MIN_HEAVY_PUSH_SPEED,
                                               R::MAX_HEAVY_PUSH_SPEED);
        const Vector2F origin = ball.position;
        launchBall(carrier, origin,
                   {origin.x + ball.dribbleDirection.x,
                    origin.y + ball.dribbleDirection.y},
                   push, 0.0f, 0.0f);
        ball.velocity = {ball.dribbleDirection.x * push,
                         ball.dribbleDirection.y * push};
        return false;
      }
      trackTouch(carrier);
      ball.dribbleTouchLength =
          std::clamp(R::TOUCH_BASE_METRES + speed * R::TOUCH_METRES_PER_SPEED *
                                                (1.3f - carrier.dribbling),
                     R::MIN_TOUCH_METRES, R::MAX_TOUCH_METRES);
      ball.dribbleExposure = ball.dribbleTouchLength;
      ball.touchTimer = std::clamp(
          R::TOUCH_INTERVAL_BASE - speed * R::TOUCH_INTERVAL_PER_SPEED -
              carrier.dribbling * R::TOUCH_INTERVAL_DRIBBLING,
          R::MIN_TOUCH_INTERVAL, R::MAX_TOUCH_INTERVAL);
    }
    else
    {
      // The carrier closes on the ball until the next touch.
      ball.dribbleExposure =
          R::FEET_METRES + (ball.dribbleExposure - R::FEET_METRES) *
                               (ball.touchTimer / (ball.touchTimer + dt));
    }
  }
  const Vector2F ahead =
      toPitch({ball.dribbleDirection.x * ball.dribbleExposure,
               ball.dribbleDirection.y * ball.dribbleExposure});
  ball.position = {std::clamp(carrier.position.x + ahead.x, 0.001f, 0.999f),
                   std::clamp(carrier.position.y + ahead.y, 0.001f, 0.999f)};
  ball.z = 0.0f;
  ball.velocity = carrier.velocity;
  return true;
}

bool MatchEngine::attemptTakeOn(MatchPlayer& carrier, MatchPlayer& defender)
{
  using R = MatchTuning::Dribble;
  // A feint, a change of direction or a burst of pace against a jockeying
  // defender: skill and pace against his defending decide it.
  const float success = std::clamp(
      R::TAKE_ON_BASE +
          (carrier.dribbling - defender.defending +
           (teamEdge(carrier.isHomeTeam) - teamEdge(defender.isHomeTeam)) *
               MatchTuning::Rules::DUEL_EDGE_WEIGHT) *
              R::TAKE_ON_SKILL +
          (carrier.pace - defender.pace) * R::TAKE_ON_PACE -
          // Play mode: a human defender jockeying is harder to beat.
          (controlInput.jockey && isControlled(defender)
               ? MatchTuning::Control::JOCKEY_TAKE_ON_PENALTY
               : 0.0f) -
          doubleUpPenalty(carrier, defender),
      R::MIN_TAKE_ON, R::MAX_TAKE_ON);
  if (randomFloat(0.0f, 1.0f) >= success)
  {
    // Read and stopped: the defender takes the ball (a clean challenge).
    attemptTackle(carrier, defender, false);
    return ball.possessedBy == carrier.player;
  }
  // Beaten: the defender is wrong-footed and the ball is pushed past him.
  defender.tackleCooldown =
      std::max(defender.tackleCooldown, R::BEATEN_SECONDS);
  defender.velocity.x *= R::BEATEN_VELOCITY_RETAINED;
  defender.velocity.y *= R::BEATEN_VELOCITY_RETAINED;
  const Vector2F forward{carrier.isHomeTeam ? 1.0f : -1.0f, 0.0f};
  const Vector2F toDefender =
      metricDirection(carrier.position, defender.position);
  const float side = toDefender.y >= 0.0f ? -1.0f : 1.0f;
  const float angle = side * R::TAKE_ON_ANGLE_RADIANS;
  ball.dribbleDirection =
      normalized({forward.x * std::cos(angle) - forward.y * std::sin(angle),
                  forward.x * std::sin(angle) + forward.y * std::cos(angle)});
  ball.dribbleTouchLength = R::TAKE_ON_TOUCH_METRES;
  ball.dribbleExposure = R::TAKE_ON_TOUCH_METRES;
  ball.touchTimer = R::MAX_TOUCH_INTERVAL;
  return true;
}

void MatchEngine::attemptTackle(MatchPlayer& carrier, MatchPlayer& defender,
                                bool sliding)
{
  // Timing matters: a challenge while the ball is away from the attacker's
  // foot wins it cleanly; one into close control or from behind is a foul
  // risk.
  const float exposure = dribbleExposureShare();
  const Vector2F motion = normalized(carrier.velocity);
  const Vector2F toDefender =
      metricDirection(carrier.position, defender.position);
  const bool fromBehind =
      motion.x * toDefender.x + motion.y * toDefender.y < -0.3f;
  const bool shielding =
      length(carrier.velocity) < MatchTuning::Dribble::CLOSE_CONTROL_SPEED;
  const StrategySliders defenderStrategy =
      getEffectiveSliders(defender.isHomeTeam);
  // A booked player picks his challenges more carefully.
  const float caution = defender.yellowCards > 0
                            ? MatchTuning::Discipline::BOOKED_PLAYER_CAUTION
                            : 1.0f;
  defender.tackleCooldown =
      randomFloat(MatchTuning::Defending::MIN_TACKLE_COOLDOWN,
                  MatchTuning::Defending::MAX_TACKLE_COOLDOWN) *
      (MatchTuning::Defending::TACKLE_COOLDOWN_BASE_MULTIPLIER -
       defenderStrategy.pressing *
           MatchTuning::Defending::PRESSING_COOLDOWN_REDUCTION) /
      caution;

  if (defender.isHomeTeam)
    ++stats.homeTackleAttempts;
  else
    ++stats.awayTackleAttempts;
  ++statsOf(defender).tacklesAttempted;

  MatchRules::TackleContext context;
  context.defending =
      defender.defending +
      teamEdge(defender.isHomeTeam) * MatchTuning::Rules::DUEL_EDGE_WEIGHT;
  context.dribbling =
      carrier.dribbling +
      teamEdge(carrier.isHomeTeam) * MatchTuning::Rules::DUEL_EDGE_WEIGHT;
  context.defenderPhysicality = defender.physicality;
  context.carrierPhysicality = carrier.physicality;
  context.pressing = defenderStrategy.pressing;
  context.riskTaking = defenderStrategy.riskTaking;
  context.exposure = exposure;
  context.shielding = shielding;
  context.sliding = sliding;
  context.fromBehind = fromBehind;
  context.inPenaltyArea = inPenaltyArea(carrier.position, defender.isHomeTeam);
  context.defenderBooked = defender.yellowCards > 0;
  const float winChance = MatchRules::tackleWinChance(context);
  const float foulPropensity = MatchRules::tackleFoulPropensity(context);
  if (randomFloat(0.0f, 1.0f) < winChance)
  {
    // Even a challenge that reaches the ball can be late or through the man.
    if (incidentRoll() <
        foulPropensity * MatchTuning::Defending::WINNING_TACKLE_FOUL_SHARE)
    {
      commitFoul(defender, carrier, true, sliding && fromBehind);
      return;
    }
    if (defender.isHomeTeam)
      ++stats.homeTackles;
    else
      ++stats.awayTackles;
    ++statsOf(defender).tacklesWon;
    if (randomFloat(0.0f, 1.0f) < MatchTuning::Defending::POKE_LOOSE_CHANCE)
    {
      // The challenge knocks the ball away rather than winning it cleanly.
      const float angle =
          randomFloat(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);
      const float speed = randomFloat(MatchTuning::Defending::MIN_POKE_SPEED,
                                      MatchTuning::Defending::MAX_POKE_SPEED);
      ball.possessedBy = nullptr;
      ball.lastPossessor = defender.player;
      clearFlightState();
      ball.z = 0.0f;
      ball.velocity = {std::cos(angle) * speed, std::sin(angle) * speed};
      ball.velocityZ = 0.0f;
      ball.kicker = defender.player;
      ball.kickerLockout = MatchTuning::Passing::KICKER_LOCKOUT_SECONDS;
      carrier.trapTimer = MatchTuning::Ball::FAILED_TRAP_TIME;
      lastControlledTeamHome = defender.isHomeTeam;
      transitionSecondsRemaining =
          MatchTuning::Timing::POSSESSION_TRANSITION_SECONDS;
      updateTeamPhases();
      return;
    }
    setPossession(defender);
    defender.actionCooldown =
        randomFloat(MatchTuning::Defending::MIN_RECOVERY_COOLDOWN,
                    MatchTuning::Defending::MAX_RECOVERY_COOLDOWN);
    return;
  }

  if (incidentRoll() >= foulPropensity) return;
  commitFoul(defender, carrier, false, sliding && fromBehind);
}

bool MatchEngine::deniesGoalChance(const MatchPlayer& victim,
                                   const MatchPlayer& offender) const
{
  // Only the player in control of the ball can be denied an obvious chance.
  if (ball.possessedBy != victim.player) return false;
  const float goalX = victim.isHomeTeam ? 1.0f : 0.0f;
  const Vector2F goal{goalX, MatchTuning::Pitch::CENTRE};
  if (distance(victim.position, goal) >
          MatchTuning::Discipline::DOGSO_MAX_DISTANCE_METRES ||
      std::abs(victim.position.y - MatchTuning::Pitch::CENTRE) >
          MatchTuning::Discipline::DOGSO_MAX_WIDTH_DEVIATION)
  {
    return false;
  }
  // Obvious chance: no covering outfield defender between victim and goal.
  const float direction = victim.isHomeTeam ? 1.0f : -1.0f;
  for (const auto& other : players)
  {
    if (!active(other) || other.isHomeTeam == victim.isHomeTeam ||
        other.isGoalkeeper || &other == &offender)
    {
      continue;
    }
    if ((other.position.x - victim.position.x) * direction > 0.0f) return false;
  }
  return true;
}

void MatchEngine::commitFoul(MatchPlayer& offender, MatchPlayer& victim,
                             bool ballWon, bool reckless)
{
  if (offender.isHomeTeam)
    ++stats.homeFouls;
  else
    ++stats.awayFouls;
  ++statsOf(offender).foulsCommitted;
  ++statsOf(victim).foulsSuffered;

  const Vector2F foulPosition = victim.position;
  const bool inBox = inPenaltyArea(foulPosition, offender.isHomeTeam);
  const float carrierDepth =
      victim.isHomeTeam ? victim.position.x : 1.0f - victim.position.x;
  const TeamPhase victimPhase = victim.isHomeTeam ? homePhase : awayPhase;
  const bool promisingAttack =
      carrierDepth > MatchTuning::Pitch::CENTRE &&
      (victimPhase == TeamPhase::ATTACKING_TRANSITION ||
       victimPhase == TeamPhase::FINAL_THIRD ||
       openSpaceAhead(victim) >
           MatchTuning::Discipline::ADVANTAGE_MIN_OPENNESS);

  MatchRules::FoulContext context;
  context.severityRoll =
      reckless ? std::max(incidentRoll(), 0.9f) : incidentRoll();
  context.cardRoll = incidentRoll();
  context.strictness = refereeStrictness;
  context.tactical =
      promisingAttack &&
      (victimPhase == TeamPhase::ATTACKING_TRANSITION || !ballWon);
  context.denyingGoalChance = deniesGoalChance(victim, offender);
  context.inPenaltyArea = inBox;
  context.offenderAlreadyBooked = offender.yellowCards > 0;
  context.offenderIsAway = !offender.isHomeTeam;
  context.venueBiasScale = matchContext.homeAdvantageScale;
  const MatchRules::FoulSanction sanction =
      MatchRules::decideFoulSanction(context);

  MatchEvent& foulEvent = logEvent(MatchEventType::FOUL, offender);
  foulEvent.secondaryPlayerId = victim.player ? victim.player->getId() : 0;
  foulEvent.position = foulPosition;

  // Advantage: the fouled side kept the ball in a promising attack outside
  // the box, so play continues unless the attack breaks down quickly.
  const bool victimKeepsBall = ball.possessedBy == victim.player;
  const bool playAdvantage =
      !ballWon && victimKeepsBall && !inBox && promisingAttack &&
      sanction != MatchRules::FoulSanction::RED &&
      incidentRoll() < MatchTuning::Discipline::ADVANTAGE_PLAY_CHANCE;
  const bool contactInjury =
      incidentRoll() <
      (context.severityRoll >= 1.0f - MatchTuning::Discipline::RECKLESS_SHARE
           ? MatchTuning::Injury::RECKLESS_CONTACT_INJURY_CHANCE
           : MatchTuning::Injury::CONTACT_INJURY_CHANCE);

  applySanction(offender, sanction);
  if (state == MatchState::FULL_TIME) return;

  if (playAdvantage && !contactInjury)
  {
    if (victim.isHomeTeam)
      ++stats.homeAdvantagesPlayed;
    else
      ++stats.awayAdvantagesPlayed;
    MatchEvent& advantage = logEvent(MatchEventType::ADVANTAGE, victim);
    advantage.position = foulPosition;
    pendingAdvantage = {true, victim.isHomeTeam, foulPosition,
                        MatchTuning::Discipline::ADVANTAGE_WINDOW_SECONDS};
    return;
  }

  if (contactInjury && victim.onPitch) injurePlayer(victim, true);
  if (inBox)
    setupPenalty(victim.isHomeTeam);
  else
    setupFreeKick(victim.isHomeTeam, foulPosition);
  if (sanction != MatchRules::FoulSanction::NONE)
    extendRestart(MatchTuning::Timing::CARD_DELAY_SECONDS);
}

void MatchEngine::applySanction(MatchPlayer& offender,
                                MatchRules::FoulSanction sanction)
{
  if (sanction == MatchRules::FoulSanction::NONE || !offender.player) return;
  PlayerMatchStats& entry = statsOf(offender);
  ++stoppageLogs[static_cast<std::size_t>(period - 1)].cards;
  if (sanction == MatchRules::FoulSanction::YELLOW)
  {
    ++offender.yellowCards;
    ++entry.yellowCards;
    if (offender.isHomeTeam)
      ++stats.homeYellowCards;
    else
      ++stats.awayYellowCards;
    logEvent(MatchEventType::YELLOW_CARD, offender);
    return;
  }

  if (sanction == MatchRules::FoulSanction::SECOND_YELLOW)
  {
    ++offender.yellowCards;
    ++entry.yellowCards;
    if (offender.isHomeTeam)
      ++stats.homeYellowCards;
    else
      ++stats.awayYellowCards;
    logEvent(MatchEventType::SECOND_YELLOW, offender);
  }
  else
  {
    logEvent(MatchEventType::RED_CARD, offender);
  }
  ++entry.redCards;
  entry.sentOff = true;
  // A sending-off always stops play; open the stoppage before any forced
  // goalkeeper change so it shares the restart's substitution window.
  beginStoppage();
  if (offender.isHomeTeam)
    ++stats.homeRedCards;
  else
    ++stats.awayRedCards;
  removeFromPitch(offender);
}

void MatchEngine::updatePendingAdvantage(float dt)
{
  if (!pendingAdvantage.active) return;
  const MatchPlayer* carrier = findMatchPlayer(ball.possessedBy);
  if (carrier && carrier->isHomeTeam != pendingAdvantage.fouledTeamHome)
  {
    // The advantage did not materialise: back for the original free kick.
    pendingAdvantage.active = false;
    setupFreeKick(pendingAdvantage.fouledTeamHome, pendingAdvantage.position);
    return;
  }
  pendingAdvantage.secondsRemaining -= dt;
  if (pendingAdvantage.secondsRemaining <= 0.0f)
    pendingAdvantage.active = false;
}

void MatchEngine::decideAction(MatchPlayer& carrier)
{
  // Decision layer: score pass/shot/carry/shield on comparable utility scales,
  // perturb close choices by decision quality, record the explanation, and
  // execute exactly one winner. Utilities are preferences, not probabilities.
  // The execution functions and subsequent contacts decide the outcome.
  const StrategySliders strategy = getEffectiveSliders(carrier.isHomeTeam);
  const float pressure =
      std::clamp((MatchTuning::Decision::PRESSURE_RADIUS_METRES -
                  nearestOpponentDistance(carrier)) /
                     MatchTuning::Decision::PRESSURE_RADIUS_METRES,
                 0.0f, 1.0f);
  const float shotXG = estimateShotXG(carrier);
  const float opennessAhead = openSpaceAhead(carrier);

  // Evaluate the passing candidates first: the task is pure (no random draws)
  // and the deterministic best plus runner-up options are visible in the
  // decision snapshot even when a shot or dribble is eventually chosen.
  const std::optional<PassOption> option = choosePassTarget(carrier);

  // Every candidate is scored in a shared utility currency.
  // Vision (scanning and reading the play) dominates decision quality, with
  // passing technique contributing to how well options are judged.
  const float decisionQuality =
      carrier.vision * MatchTuning::Decision::VISION_DECISION_WEIGHT +
      carrier.passing * (1.0f - MatchTuning::Decision::VISION_DECISION_WEIGHT);
  const float visionNoiseScale =
      (1.0f - decisionQuality) * MatchTuning::Decision::VISION_NOISE_SCALE *
      (1.0f + (1.0f - familiarityOf(carrier)) *
                  MatchTuning::Decision::FAMILIARITY_NOISE_GAIN) *
      std::max(0.5f, 1.0f - teamEdge(carrier.isHomeTeam)) *
      (1.0f - talkSwing(carrier.isHomeTeam) * TacticsTuning::TALK_COMPOSURE);

  float passScore = -std::numeric_limits<float>::infinity();
  if (option)
  {
    passScore = option->utility;
    const float carrierDepth =
        carrier.isHomeTeam ? carrier.position.x : 1.0f - carrier.position.x;
    const bool pinnedToByline =
        carrierDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START &&
        std::abs(MatchTuning::Pitch::CENTRE - carrier.position.y) >=
            MatchTuning::Decision::WIDE_SHOT_WIDTH_DEVIATION;
    if (pinnedToByline && option->targetPoint.x <= carrier.position.x)
    {
      passScore += MatchTuning::Decision::WIDE_RECYCLE_BONUS;
    }
  }

  float shotScore = -std::numeric_limits<float>::infinity();
  if (shotXG >= MatchTuning::Decision::MIN_SHOT_XG)
  {
    // The "have-a-go" inclination only matters from a credible shooting
    // range: it fades out completely for absurd-distance attempts, so the
    // scored decision can never invent a nonsensical long shot.
    const float rangeEligibility =
        std::clamp((shotXG - MatchTuning::Decision::SHOT_ELIGIBILITY_FLOOR) /
                       MatchTuning::Decision::SHOT_ELIGIBILITY_RANGE,
                   0.0f, 1.0f);
    const float carrierDepth =
        carrier.isHomeTeam ? carrier.position.x : 1.0f - carrier.position.x;
    const float finalThirdBonus =
        carrierDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START
            ? MatchTuning::Decision::FINAL_THIRD_SHOT_BONUS
            : 0.0f;
    shotScore = (shotXG - MatchTuning::Decision::BASE_SHOT_THRESHOLD) *
                    MatchTuning::Decision::SHOT_SCORE_SCALE +
                MatchTuning::Decision::SHOT_BASE_INCLINATION *
                    (0.6f + opennessAhead) * rangeEligibility +
                finalThirdBonus +
                carrier.shooting * MatchTuning::Decision::SHOT_SKILL_BONUS -
                pressure * MatchTuning::Decision::SHOT_PRESSURE_PENALTY +
                shoutShotBias(carrier.isHomeTeam) * rangeEligibility;
    if (carrierDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START &&
        std::abs(MatchTuning::Pitch::CENTRE - carrier.position.y) >=
            MatchTuning::Decision::WIDE_SHOT_WIDTH_DEVIATION)
    {
      shotScore *= MatchTuning::Decision::WIDE_SHOT_DISCOUNT;
    }
    // A wide forward who has cut inside is looking for his shot.
    if (cutsInside(carrier))
      shotScore +=
          MatchTuning::Decision::CUT_INSIDE_SHOT_BONUS * rangeEligibility;
    // Roles shoot more or less readily; a man forced onto his weaker foot
    // under pressure is less inclined to try.
    shotScore +=
        roleProfileOf(carrier).shotBias * TacticsTuning::SHOT_BIAS_WEIGHT *
            rangeEligibility -
        weakFootPressure(carrier) * TacticsTuning::WEAK_FOOT_SHOT_PENALTY;
  }

  // The longer a player has had the ball, the keener he is to release it:
  // individual possessions last about a second, rarely more than three.
  const auto heldSeconds =
      static_cast<float>(getSimulatedSeconds() - possessionStartSeconds);
  float carryScore =
      opennessAhead * MatchTuning::Decision::CARRY_OPENNESS_WEIGHT +
      carrier.dribbling * MatchTuning::Decision::CARRY_DRIBBLING_BONUS -
      pressure * MatchTuning::Decision::CARRY_PRESSURE_PENALTY +
      strategy.riskTaking * MatchTuning::Decision::CARRY_RISK_BIAS -
      heldSeconds * MatchTuning::Decision::CARRY_HOLD_PENALTY_PER_SECOND;

  // A wide forward cutting inside wants to run at the box himself.
  if (cutsInside(carrier))
    carryScore += MatchTuning::Decision::CUT_INSIDE_CARRY_BONUS;

  float shieldScore = -std::numeric_limits<float>::infinity();
  const bool passUnavailableOrWeak =
      !option || passScore <= MatchTuning::Passing::MIN_ACCEPTABLE_OPTION_SCORE;
  if (pressure >= MatchTuning::Decision::SHIELD_PRESSURE_THRESHOLD &&
      passUnavailableOrWeak)
  {
    shieldScore = pressure * MatchTuning::Decision::SHIELD_BONUS +
                  carrier.dribbling * MatchTuning::Decision::SHIELD_DRIBBLING;
  }

  // A side protecting a late lead no longer forces speculative long-range
  // attempts; it prefers to retain the ball. Credible chances are unaffected.
  const int carrierScore = carrier.isHomeTeam ? homeScore : awayScore;
  const int opponentScore = carrier.isHomeTeam ? awayScore : homeScore;
  if (matchTimeMinutes >= MatchTuning::Decision::LATE_GAME_MINUTE &&
      carrierScore > opponentScore &&
      shotScore > -std::numeric_limits<float>::infinity() &&
      shotXG < MatchTuning::Decision::BASE_SHOT_THRESHOLD)
  {
    shotScore -= MatchTuning::Decision::LATE_LEAD_SPECULATIVE_PENALTY;
  }
  // Game state: a side two or more goals up manages the game, keeping the
  // ball rather than forcing half-chances.
  const int lead = carrierScore - opponentScore;
  if (lead >= MatchTuning::Decision::COMFORTABLE_LEAD)
  {
    const float margin =
        static_cast<float>(lead - MatchTuning::Decision::COMFORTABLE_LEAD + 1);
    if (shotScore > -std::numeric_limits<float>::infinity() &&
        shotXG < MatchTuning::Decision::CLEAR_CHANCE_XG)
    {
      shotScore -=
          MatchTuning::Decision::COMFORTABLE_LEAD_SHOT_PENALTY * margin;
    }
    carryScore -=
        MatchTuning::Decision::COMFORTABLE_LEAD_CARRY_PENALTY * margin;
  }

  // Vision scales how much randomness perturbs close choices. The noise is
  // bounded so a truly nonsensical option can never win.
  shotScore += visionNoiseScale * randomFloat(-1.0f, 1.0f);
  carryScore += visionNoiseScale * randomFloat(-1.0f, 1.0f);
  if (shieldScore > -std::numeric_limits<float>::infinity())
  {
    shieldScore += visionNoiseScale * randomFloat(-1.0f, 1.0f);
  }
  if (option)
  {
    passScore += visionNoiseScale * randomFloat(-1.0f, 1.0f);
  }

  lastScenarioDecision.passUtility = passScore;
  lastScenarioDecision.shotUtility = shotScore;
  lastScenarioDecision.carryUtility = carryScore;
  lastScenarioDecision.shieldUtility = shieldScore;

  lastScenarioDecision.action = ScenarioAction::NONE;
  if (shotScore >= passScore && shotScore >= carryScore &&
      shotScore >= shieldScore)
  {
    lastScenarioDecision.action = ScenarioAction::SHOT;
    lastScenarioDecision.reason =
        "shot with estimated xG " + std::to_string(shotXG);
    takeShot(carrier);
    return;
  }
  if (carryScore >= passScore && carryScore >= shieldScore)
  {
    lastScenarioDecision.action = ScenarioAction::CARRY;
    lastScenarioDecision.reason =
        option ? "carry:\"space ahead (value " + std::to_string(opennessAhead) +
                     ") beats the best pass with intent " +
                     std::string(passIntentName(option->intent))
               : std::string(
                     "carry:space ahead and no acceptable pass "
                     "candidate in range");
    carrier.actionCooldown =
        randomFloat(MatchTuning::Decision::MIN_DRIBBLE_TIME,
                    MatchTuning::Decision::MAX_DRIBBLE_TIME);
    // A defender standing in the path must be beaten, not run through.
    if (state == MatchState::PLAYING)
    {
      MatchPlayer* blocker =
          findClosestPlayer(carrier.position, !carrier.isHomeTeam, false);
      const float forward = carrier.isHomeTeam ? 1.0f : -1.0f;
      if (blocker && blocker->tackleCooldown <= 0.0f &&
          distance(blocker->position, carrier.position) <
              MatchTuning::Dribble::TAKE_ON_RANGE_METRES &&
          (blocker->position.x - carrier.position.x) * forward > 0.0f)
      {
        attemptTakeOn(carrier, *blocker);
      }
    }
    return;
  }
  const float ownDepth =
      carrier.isHomeTeam ? carrier.position.x : 1.0f - carrier.position.x;
  if (shieldScore >= passScore &&
      ownDepth < MatchTuning::Defending::CLEARANCE_MAX_DEPTH)
  {
    lastScenarioDecision.action = ScenarioAction::CLEAR;
    lastScenarioDecision.reason =
        "clear:pressed in the own third with no safe outlet";
    clearBall(carrier);
    return;
  }
  if (shieldScore >= passScore)
  {
    lastScenarioDecision.action = ScenarioAction::SHIELD;
    lastScenarioDecision.reason =
        "shield:retain and protect the ball under pressure with no safe "
        "outlet";
    carrier.actionCooldown =
        randomFloat(MatchTuning::Decision::MIN_DRIBBLE_TIME,
                    MatchTuning::Decision::MAX_DRIBBLE_TIME);
    return;
  }

  lastScenarioDecision.action = ScenarioAction::PASS;
  lastScenarioDecision.reason =
      std::string("pass:") + std::string(passIntentName(option->intent)) +
      " to player " +
      std::to_string(option->receiver && option->receiver->player
                         ? option->receiver->player->getId()
                         : 0) +
      " (utility " + std::to_string(option->utility) + ")";
  passBall(carrier, *option);
}

MatchEngine::PassOption MatchEngine::evaluatePassOption(
    MatchPlayer& passer, MatchPlayer& receiver) const
{
  // Estimate one receiver's safety, progress and space without consuming RNG
  // or changing the match. The completion estimate ranks options; passBall
  // does not roll against it to guarantee a completed pass. Error at release,
  // ball flight and contested receptions determine whether it arrives.
  const StrategySliders strategy = getEffectiveSliders(passer.isHomeTeam);
  const float direction = passer.isHomeTeam ? 1.0f : -1.0f;
  const float passDistance = distance(passer.position, receiver.position);
  const float progression =
      (receiver.position.x - passer.position.x) * direction;
  const float openness =
      std::clamp(nearestOpponentDistance(receiver) /
                     MatchTuning::Passing::OPENNESS_RADIUS_METRES,
                 0.0f, 1.0f);
  const float laneRisk = passingLaneRisk(passer, receiver);
  const float pressure =
      std::clamp((MatchTuning::Decision::PRESSURE_RADIUS_METRES -
                  nearestOpponentDistance(passer)) /
                     MatchTuning::Decision::PRESSURE_RADIUS_METRES,
                 0.0f, 1.0f);
  const PlayerRole role =
      receiver.player ? receiver.player->getRole() : PlayerRole::CM;
  const bool forwardRole = role == PlayerRole::ST || role == PlayerRole::LW ||
                           role == PlayerRole::RW || role == PlayerRole::CAM;
  const float passerDepth =
      passer.isHomeTeam ? passer.position.x : 1.0f - passer.position.x;
  const float receiverDepth =
      receiver.isHomeTeam ? receiver.position.x : 1.0f - receiver.position.x;
  const bool widePasser =
      passer.position.y <= MatchTuning::Passing::WIDE_ATTACK_MINIMUM_Y ||
      passer.position.y >= MatchTuning::Passing::WIDE_ATTACK_MAXIMUM_Y;
  const bool centralReceiver =
      receiver.position.y >= MatchTuning::Passing::CENTRAL_TARGET_MINIMUM_Y &&
      receiver.position.y <= MatchTuning::Passing::CENTRAL_TARGET_MAXIMUM_Y;
  const bool cutbackOption =
      widePasser && centralReceiver &&
      passerDepth >= MatchTuning::Passing::CUTBACK_MINIMUM_PASSER_DEPTH &&
      receiverDepth >= MatchTuning::Passing::CROSS_MINIMUM_RECEIVER_DEPTH &&
      progression >= MatchTuning::Passing::CUTBACK_MINIMUM_PROGRESSION &&
      progression <= MatchTuning::Passing::CUTBACK_MAXIMUM_PROGRESSION;
  const bool crossOption =
      !cutbackOption && widePasser && centralReceiver &&
      passerDepth >= MatchTuning::Passing::CROSS_MINIMUM_PASSER_DEPTH &&
      receiverDepth >= MatchTuning::Passing::CROSS_MINIMUM_RECEIVER_DEPTH;
  const float safeOutlet = pressure * std::max(0.0f, -progression) *
                           MatchTuning::Passing::SAFE_OUTLET_WEIGHT;
  // A high defensive line leaves space in behind: a forward already running
  // into it is worth finding, and the ball is easier to play there.
  float spaceBehindShare = 0.0f;
  // Any player running in behind is worth finding, a midfielder arriving
  // late as much as a striker.
  if (receiver.isMakingRun && progression > 0.0f)
  {
    const float line = offsideLine(passer.isHomeTeam);
    const float spaceBehind = (passer.isHomeTeam ? 1.0f - line : line) *
                              MatchTuning::Pitch::LENGTH_METRES;
    spaceBehindShare = std::clamp(
        (spaceBehind - MatchTuning::Passing::SPACE_BEHIND_MIN_METRES) /
            MatchTuning::Passing::SPACE_BEHIND_RANGE_METRES,
        0.0f, 1.0f);
  }
  const float completionProbability = std::clamp(
      MatchTuning::Passing::BASE_COMPLETION_PROBABILITY +
          passer.passing * MatchTuning::Passing::PASSING_COMPLETION_BONUS +
          openness * MatchTuning::Passing::OPENNESS_COMPLETION_BONUS -
          laneRisk * MatchTuning::Passing::LANE_COMPLETION_PENALTY -
          // Completion falls steeply for long passes (~86% at 10-20 m,
          // ~41% beyond 40 m).
          passDistance * passDistance /
              (MatchTuning::Passing::LONG_PASS_REFERENCE_METRES *
               MatchTuning::Passing::LONG_PASS_REFERENCE_METRES) *
              MatchTuning::Passing::DISTANCE_COMPLETION_PENALTY -
          pressure * MatchTuning::Passing::PRESSURE_COMPLETION_PENALTY -
          (crossOption ? MatchTuning::Passing::CROSS_COMPLETION_PENALTY
                       : 0.0f) +
          (cutbackOption ? MatchTuning::Passing::CUTBACK_COMPLETION_BONUS
                         : 0.0f) +
          spaceBehindShare *
              MatchTuning::Passing::SPACE_BEHIND_COMPLETION_BONUS,
      MatchTuning::Passing::MIN_COMPLETION_PROBABILITY,
      MatchTuning::Passing::MAX_COMPLETION_PROBABILITY);
  // Risk-taking trades safety for progress: a patient side values keeping
  // the ball, a daring one the metres gained.
  const float daring = strategy.riskTaking - 0.5f;
  float utility =
      openness * MatchTuning::Passing::OPENNESS_WEIGHT -
      laneRisk * MatchTuning::Passing::LANE_RISK_WEIGHT +
      progression *
          (MatchTuning::Passing::BASE_PROGRESS_WEIGHT +
           strategy.offensiveBias *
               MatchTuning::Passing::OFFENSIVE_PROGRESS_WEIGHT) *
          (1.0f + daring * MatchTuning::Passing::RISK_PROGRESS_GAIN) -
      std::abs(passDistance - MatchTuning::Passing::IDEAL_DISTANCE_METRES) *
          MatchTuning::Passing::DISTANCE_PENALTY_PER_METRE +
      safeOutlet +
      (forwardRole && progression > 0.0f
           ? MatchTuning::Passing::FORWARD_ROLE_BONUS
           : 0.0f) +
      completionProbability * MatchTuning::Passing::COMPLETION_UTILITY_WEIGHT *
          (1.0f - daring * MatchTuning::Passing::RISK_SAFETY_GAIN);
  if (receiver.isMakingRun && progression > 0.0f)
    utility += MatchTuning::Passing::ACTIVE_RUNNER_UTILITY_BONUS;
  utility +=
      passingRoleBias(passer, receiver, progression, completionProbability);
  // In the final third a team-mate in a shooting position is worth finding.
  if (receiverDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START)
    utility +=
        estimateShotXG(receiver) * MatchTuning::Passing::SHOT_CREATION_WEIGHT;
  utility += spaceBehindShare * MatchTuning::Passing::SPACE_BEHIND_UTILITY;
  if (crossOption) utility += MatchTuning::Passing::CROSS_UTILITY_BONUS;
  if (cutbackOption) utility += MatchTuning::Passing::CUTBACK_UTILITY_BONUS;

  PassIntent intent = PassIntent::RECYCLE;
  if (cutbackOption)
  {
    intent = PassIntent::CUTBACK;
  }
  else if (crossOption)
  {
    intent = PassIntent::CROSS;
  }
  else if (pressure >= MatchTuning::Passing::PRESSURE_RELEASE_THRESHOLD &&
           progression <=
               MatchTuning::Passing::PRESSURE_RELEASE_MAX_PROGRESSION)
  {
    intent = PassIntent::PRESSURE_RELEASE;
  }
  else if (std::abs(receiver.position.y - passer.position.y) >=
           MatchTuning::Passing::SWITCH_PLAY_MINIMUM_WIDTH)
  {
    intent = PassIntent::SWITCH_PLAY;
  }
  else if (receiver.isMakingRun &&
           progression >=
               MatchTuning::Passing::THROUGH_BALL_MINIMUM_PROGRESSION)
  {
    intent = PassIntent::THROUGH_BALL;
  }
  else if (progression >= MatchTuning::Passing::PROGRESSIVE_PASS_MINIMUM)
  {
    intent = PassIntent::PROGRESSIVE;
  }

  const float flightEstimate =
      passDistance / MatchTuning::Passing::ESTIMATED_BALL_SPEED;
  const Vector2F lead =
      toPitch({receiver.velocity.x * flightEstimate *
                   MatchTuning::Passing::RECEIVER_LEAD_SCALE,
               receiver.velocity.y * flightEstimate *
                   MatchTuning::Passing::RECEIVER_LEAD_SCALE});
  Vector2F target{std::clamp(receiver.position.x + lead.x, 0.0f, 1.0f),
                  std::clamp(receiver.position.y + lead.y, 0.0f, 1.0f)};
  if (intent == PassIntent::THROUGH_BALL)
  {
    target.x += (receiver.movementTarget.x - target.x) *
                MatchTuning::Passing::THROUGH_BALL_TARGET_BLEND;
    target.y += (receiver.movementTarget.y - target.y) *
                MatchTuning::Passing::THROUGH_BALL_TARGET_BLEND;
    target.x += direction * MatchTuning::Passing::THROUGH_BALL_FORWARD_LEAD;
    target.x = std::clamp(target.x, MatchTuning::Pitch::PLAYER_MIN_X,
                          MatchTuning::Pitch::PLAYER_MAX_X);
    target.y = std::clamp(target.y, MatchTuning::Pitch::PLAYER_MIN_Y,
                          MatchTuning::Pitch::PLAYER_MAX_Y);
  }
  else if (intent == PassIntent::CROSS)
  {
    target.x += (receiver.movementTarget.x - target.x) *
                MatchTuning::Passing::CROSS_TARGET_BLEND;
    target.y += (receiver.movementTarget.y - target.y) *
                MatchTuning::Passing::CROSS_TARGET_BLEND;
  }
  const float travelDistance = distance(passer.position, target);

  return {&receiver,
          target,
          intent,
          utility,
          progression,
          laneRisk,
          completionProbability,
          travelDistance,
          !receiver.isGoalkeeper &&
              (intent == PassIntent::CROSS ||
               travelDistance > MatchTuning::Passing::LOFTED_DISTANCE_METRES)};
}

std::optional<MatchEngine::PassOption> MatchEngine::choosePassTarget(
    MatchPlayer& passer)
{
  std::optional<PassOption> best;
  float bestScore = -std::numeric_limits<float>::infinity();
  std::optional<PassOption> runnerUp;
  float runnerUpScore = -std::numeric_limits<float>::infinity();

  auto toDecision = [](const PassOption& option)
  {
    return PassDecision{0,
                        option.receiver && option.receiver->player
                            ? option.receiver->player->getId()
                            : 0,
                        option.targetPoint,
                        option.intent,
                        option.utility,
                        option.progression,
                        option.laneRisk,
                        option.completionProbability};
  };

  const float defenderLine = offsideLine(passer.isHomeTeam);
  for (auto& candidate : players)
  {
    if (&candidate == &passer || candidate.isHomeTeam != passer.isHomeTeam ||
        !active(candidate))
    {
      continue;
    }

    const float passDistance = distance(passer.position, candidate.position);
    if (passDistance < MatchTuning::Passing::MIN_DISTANCE_METRES ||
        passDistance > MatchTuning::Passing::MAX_DISTANCE_METRES)
      continue;
    // Nobody plays a long ball back towards his own goal.
    if (candidate.isGoalkeeper &&
        passDistance > MatchTuning::Passing::MAX_BACK_PASS_METRES)
      continue;
    // The passer judges the offside line from what he sees: poorer vision
    // misreads tight lines, which is where real offsides come from.
    const float perceivedLineError =
        hashNoise(passer.player->getId(), candidate.player->getId()) *
        (1.0f - passer.vision) * MatchTuning::Passing::OFFSIDE_PERCEPTION_ERROR;
    if (isOffside(candidate, passer.isHomeTeam, defenderLine,
                  perceivedLineError))
      continue;

    PassOption option = evaluatePassOption(passer, candidate);
    if (option.utility > bestScore)
    {
      runnerUp = std::move(best);
      runnerUpScore = bestScore;
      bestScore = option.utility;
      best = std::move(option);
    }
    else if (option.utility > runnerUpScore)
    {
      runnerUpScore = option.utility;
      runnerUp = std::move(option);
    }
  }

  lastScenarioDecision.best.reset();
  lastScenarioDecision.runnerUp.reset();
  if (best && best->receiver && best->receiver->player)
  {
    lastScenarioDecision.best = toDecision(*best);
  }
  if (runnerUp && runnerUp->receiver && runnerUp->receiver->player)
  {
    lastScenarioDecision.runnerUp = toDecision(*runnerUp);
  }

  if (bestScore < MatchTuning::Passing::MIN_ACCEPTABLE_OPTION_SCORE)
    return std::nullopt;
  return best;
}

void MatchEngine::passBall(MatchPlayer& passer, const PassOption& option,
                           bool forceLofted)
{
  if (!option.receiver || !option.receiver->player) return;
  MatchPlayer& receiver = *option.receiver;
  Vector2F target = option.targetPoint;
  const float passDistance = option.passDistance;
  if (passDistance <= EPSILON) return;
  const bool lofted = forceLofted || option.lofted;

  clearFlightState();
  ball.intendedReceiver = receiver.player;
  ball.isPass = true;
  ball.passByHome = passer.isHomeTeam;
  const float defenderLine = offsideLine(passer.isHomeTeam);
  ball.passWasOffside =
      state != MatchState::KICK_OFF && state != MatchState::THROW_IN &&
      state != MatchState::GOAL_KICK && state != MatchState::CORNER_KICK &&
      isOffside(receiver, passer.isHomeTeam, defenderLine);
  if (!ball.passWasOffside && state == MatchState::PLAYING &&
      option.progression > 0.0f &&
      (receiver.isMakingRun || isAttackingRole(receiver.player->getRole())))
  {
    // A runner level with the line at the moment of the pass has often
    // timed his run a fraction early: the tighter, the likelier.
    const float gap = (defenderLine - receiver.position.x) *
                      (passer.isHomeTeam ? 1.0f : -1.0f);
    const bool opponentHalf =
        passer.isHomeTeam ? receiver.position.x > MatchTuning::Pitch::CENTRE
                          : receiver.position.x < MatchTuning::Pitch::CENTRE;
    if (opponentHalf && gap >= 0.0f &&
        gap < MatchTuning::Passing::OFFSIDE_TIMING_WINDOW)
    {
      const float chance =
          MatchTuning::Passing::OFFSIDE_TIMING_CHANCE *
          (1.0f - gap / MatchTuning::Passing::OFFSIDE_TIMING_WINDOW);
      ball.passWasOffside =
          (hashNoise(receiver.player->getId(), 0x5eedU) + 1.0f) * 0.5f < chance;
    }
  }
  const float pressure =
      std::clamp((MatchTuning::Passing::PASS_PRESSURE_RADIUS_METRES -
                  nearestOpponentDistance(passer)) /
                     MatchTuning::Passing::PASS_PRESSURE_RADIUS_METRES,
                 0.0f, 1.0f);
  lastPassDecision = {
      passer.player ? passer.player->getId() : 0,
      receiver.player ? receiver.player->getId() : 0,
      target,
      state == MatchState::PLAYING ? option.intent : PassIntent::SET_PIECE,
      option.utility,
      option.progression,
      option.laneRisk,
      option.completionProbability};
  // Lateral execution error (metres) grows with poor technique, pressure,
  // distance, fatigue and away nerves. Passes back to the own keeper are
  // played safely.
  const Vector2F direct = metricDirection(passer.position, target);
  const float care = receiver.isGoalkeeper
                         ? MatchTuning::Passing::BACK_PASS_ERROR_SCALE
                         : 1.0f;
  const float error =
      ((1.0f - passer.passing) * MatchTuning::Passing::TECHNICAL_ERROR_METRES +
       pressure * MatchTuning::Passing::PRESSURE_ERROR_METRES +
       passDistance * MatchTuning::Passing::DISTANCE_ERROR) *
      executionErrorScale(passer) * care * randomFloat(-1.0f, 1.0f);
  const Vector2F miss = toPitch({-direct.y * error, direct.x * error});
  target.x = std::clamp(target.x + miss.x, 0.0f, 1.0f);
  target.y = std::clamp(target.y + miss.y, 0.0f, 1.0f);
  const float travel = distance(passer.position, target);
  const bool delivery = option.intent == PassIntent::CROSS ||
                        state == MatchState::CORNER_KICK ||
                        (state == MatchState::FREE_KICK && lofted);
  if (lofted)
  {
    // Lofted balls are launched so they drop at the target: long passes at
    // chest height to be controlled, crosses at head height to be attacked.
    const float arrivalMetres =
        delivery
            ? MatchTuning::Aerial::ARRIVAL_HEIGHT_METRES +
                  randomFloat(-MatchTuning::Passing::DELIVERY_HEIGHT_SPREAD,
                              MatchTuning::Passing::DELIVERY_HEIGHT_SPREAD)
            : MatchTuning::Passing::LOFTED_ARRIVAL_HEIGHT_METRES;
    const float speed =
        std::min(MatchTuning::Passing::LOFTED_BASE_SPEED +
                     travel * MatchTuning::Passing::LOFTED_SPEED_PER_METRE,
                 MatchTuning::Passing::MAX_LOFTED_SPEED);
    launchBall(passer, ball.position, target, speed,
               loftVerticalSpeed(travel, speed, 0.0f, arrivalMetres), 0.0f);
  }
  else
  {
    // Ground passes are weighted to arrive at a controllable pace; longer
    // passes and better passers zip the ball in harder.
    const float arrivalSpeed =
        MatchTuning::Passing::ARRIVAL_SPEED_BASE +
        passer.passing * MatchTuning::Passing::ARRIVAL_SPEED_PASSING +
        travel * MatchTuning::Passing::ARRIVAL_SPEED_PER_METRE;
    const float speed = std::clamp(groundLaunchSpeed(travel, arrivalSpeed),
                                   MatchTuning::Passing::MIN_GROUND_SPEED,
                                   MatchTuning::Passing::MAX_GROUND_SPEED);
    launchBall(passer, ball.position, target, speed, 0.0f,
               randomFloat(-MatchTuning::Passing::MAX_CURVE,
                           MatchTuning::Passing::MAX_CURVE) *
                   (MatchTuning::Passing::CURVE_SKILL_BASE + passer.passing));
  }
  ball.isAerialDelivery = lofted && delivery;
  ball.fromThrowIn = state == MatchState::THROW_IN;
  if (passer.isGoalkeeper)
  {
    GoalkeeperControl& control = keepers[passer.isHomeTeam ? 0 : 1];
    control.state = GoalkeeperState::DISTRIBUTE;
    control.timer = 0.0f;
  }

  // Keep externally visible AI state consistent with the newly released
  // pass. Without this, the final fixed step of a rendered frame could expose
  // the pre-pass run assignments until the next simulation update.
  for (auto& teammate : players)
  {
    if (teammate.isHomeTeam != passer.isHomeTeam || !active(teammate)) continue;
    teammate.isMakingRun = false;
    if (&teammate == &receiver)
      teammate.intent = PlayerIntent::RECEIVE_PASS;
    else if (!teammate.isGoalkeeper)
      teammate.intent = PlayerIntent::OFFER_SUPPORT;
  }

  if (passer.isHomeTeam)
    ++stats.homePassesAttempted;
  else
    ++stats.awayPassesAttempted;
  ++statsOf(passer).passesAttempted;
  trackPassRelease(passer);
  if (state == MatchState::PLAYING)
  {
    int* progressivePasses = passer.isHomeTeam ? &stats.homeProgressivePasses
                                               : &stats.awayProgressivePasses;
    int* throughBalls =
        passer.isHomeTeam ? &stats.homeThroughBalls : &stats.awayThroughBalls;
    int* crosses = passer.isHomeTeam ? &stats.homeCrosses : &stats.awayCrosses;
    int* cutbacks =
        passer.isHomeTeam ? &stats.homeCutbacks : &stats.awayCutbacks;
    int* switchesOfPlay = passer.isHomeTeam ? &stats.homeSwitchesOfPlay
                                            : &stats.awaySwitchesOfPlay;
    if (option.intent == PassIntent::PROGRESSIVE)
      ++(*progressivePasses);
    else if (option.intent == PassIntent::THROUGH_BALL)
      ++(*throughBalls);
    else if (option.intent == PassIntent::CROSS)
      ++(*crosses);
    else if (option.intent == PassIntent::CUTBACK)
      ++(*cutbacks);
    else if (option.intent == PassIntent::SWITCH_PLAY)
      ++(*switchesOfPlay);
  }
  passer.actionCooldown =
      randomFloat(MatchTuning::Passing::MIN_ACTION_COOLDOWN,
                  MatchTuning::Passing::MAX_ACTION_COOLDOWN);
}

float MatchEngine::strikeShot(MatchPlayer& shooter, float forcedXG, bool header,
                              bool penalty, bool freeKick)
{
  const float goalX = shooter.isHomeTeam ? 1.0f : 0.0f;
  const float metres =
      distance(shooter.position, {goalX, MatchTuning::Pitch::CENTRE});
  float xg = forcedXG >= 0.0f ? forcedXG : estimateShotXG(shooter);
  if (header && forcedXG < 0.0f) xg *= MatchTuning::Shooting::HEADER_XG_FACTOR;
  xg = std::clamp(xg, MatchTuning::Shooting::MIN_GOAL_PROBABILITY,
                  forcedXG >= 0.0f ? MatchTuning::Shooting::MAX_SET_PIECE_XG
                                   : MatchTuning::Shooting::MAX_OPEN_PLAY_XG);

  const MatchPlayer* keeper = findGoalkeeper(!shooter.isHomeTeam);
  const float pressure =
      std::clamp((MatchTuning::Shooting::PRESSURE_RADIUS_METRES -
                  nearestOpponentDistance(shooter)) /
                     MatchTuning::Shooting::PRESSURE_RADIUS_METRES,
                 0.0f, 1.0f);

  // Aim for the side away from the keeper, a little inside the post; better
  // finishers pick tighter spots. The execution error then grows with
  // distance, pressure, weak technique, fatigue and headers.
  constexpr float HALF_GOAL = MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES;
  const float keeperOffset =
      keeper ? (keeper->position.y - MatchTuning::Pitch::CENTRE) *
                   MatchTuning::Pitch::WIDTH_METRES
             : 0.0f;
  float side = keeperOffset > 0.0f ? -1.0f : 1.0f;
  if (std::abs(keeperOffset) < MatchTuning::Shooting::KEEPER_CENTRED_METRES ||
      randomFloat(0.0f, 1.0f) < MatchTuning::Shooting::NEAR_SIDE_AIM_CHANCE)
  {
    side = randomFloat(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f;
  }
  // From the spot a taker can pick a corner; in open play the spot is
  // chosen on the move and lands further inside the post.
  // A free kick is curled over the wall into a corner.
  const float inset =
      MatchTuning::Shooting::AIM_POST_INSET_BASE +
      (1.0f - shooter.shooting) * MatchTuning::Shooting::AIM_POST_INSET_SKILL +
      randomFloat(0.0f,
                  penalty    ? MatchTuning::Shooting::PENALTY_AIM_INSET_SPREAD
                  : freeKick ? MatchTuning::SetPiece::FREE_KICK_AIM_INSET_SPREAD
                             : MatchTuning::Shooting::AIM_INSET_SPREAD);
  float aimY = side * std::max(0.0f, HALF_GOAL - inset);
  if (!penalty && !freeKick && !header &&
      randomFloat(0.0f, 1.0f) <
          MatchTuning::Shooting::POWER_SHOT_BASE_CHANCE -
              shooter.shooting *
                  MatchTuning::Shooting::POWER_SHOT_SKILL_REDUCTION)
  {
    // Struck hard at the frame rather than placed: often near the keeper.
    aimY = keeperOffset +
           randomFloat(-MatchTuning::Shooting::POWER_SHOT_WIDTH_METRES,
                       MatchTuning::Shooting::POWER_SHOT_WIDTH_METRES);
  }
  float aimZ =
      freeKick
          ? randomFloat(MatchTuning::SetPiece::FREE_KICK_AIM_MIN_HEIGHT_METRES,
                        MatchTuning::Shooting::AIM_MAX_HEIGHT_METRES)
          : randomFloat(
                MatchTuning::Shooting::AIM_MIN_HEIGHT_METRES,
                header ? MatchTuning::Shooting::HEADER_AIM_MAX_HEIGHT_METRES
                       : MatchTuning::Shooting::AIM_MAX_HEIGHT_METRES);
  // Heading well is as much about strength and timing as about finishing.
  const float finishing =
      header ? shooter.shooting *
                       (1.0f - MatchTuning::Aerial::HEADING_PHYSICAL_SHARE) +
                   shooter.physicality *
                       MatchTuning::Aerial::HEADING_PHYSICAL_SHARE
             : shooter.shooting;
  float spread =
      (MatchTuning::Shooting::ERROR_BASE_METRES +
       (1.0f - finishing) * MatchTuning::Shooting::ERROR_SKILL_METRES +
       pressure * MatchTuning::Shooting::ERROR_PRESSURE_METRES) *
      std::max(metres / MatchTuning::Shooting::ERROR_REFERENCE_METRES,
               MatchTuning::Shooting::ERROR_MIN_DISTANCE_SCALE) *
      executionErrorScale(shooter);
  if (header) spread *= MatchTuning::Aerial::HEADER_ACCURACY_PENALTY;
  if (penalty)
    spread *= MatchTuning::Shooting::PENALTY_ERROR_SCALE;
  else
    spread /= finishingPrecision * levelPrecision;
  if (freeKick) spread *= MatchTuning::SetPiece::FREE_KICK_ERROR_SCALE;
  // Play mode: the human's aim and power (no effect on AI shots).
  if (controlledShot.active)
    shapeControlledShot(shooter, HALF_GOAL, inset, aimY, aimZ, spread);
  const float crossingY = aimY + gaussian(rng) * spread;
  const float crossingZ =
      std::clamp(aimZ + gaussian(rng) * spread *
                            MatchTuning::Shooting::VERTICAL_ERROR_SHARE,
                 MatchTuning::Shooting::MIN_CROSSING_HEIGHT_METRES,
                 MatchTuning::Shooting::MAX_CROSSING_HEIGHT_METRES);
  const bool onTarget =
      std::abs(crossingY) <
          HALF_GOAL - MatchTuning::Units::BALL_RADIUS_METRES &&
      crossingZ < MatchTuning::Units::CROSSBAR_HEIGHT_METRES -
                      MatchTuning::Units::BALL_RADIUS_METRES;
  const float targetY =
      MatchTuning::Pitch::CENTRE + crossingY / MatchTuning::Pitch::WIDTH_METRES;

  const float speed =
      (MatchTuning::Shooting::BASE_BALL_SPEED +
       shooter.shooting * MatchTuning::Shooting::SHOOTING_SPEED_BONUS +
       shooter.physicality * MatchTuning::Shooting::POWER_SPEED_BONUS) *
      randomFloat(MatchTuning::Shooting::MIN_SPEED_VARIATION, 1.0f) *
      (header ? MatchTuning::Aerial::HEADER_SPEED_SCALE : 1.0f) *
      (controlledShot.active ? MatchTuning::Control::SHOT_SPEED_MIN_SCALE +
                                   MatchTuning::Control::SHOT_SPEED_POWER_GAIN *
                                       controlledShot.power
                             : 1.0f);
  const Vector2F goalPoint{goalX, targetY};
  const float startHeight = header
                                ? ball.z * MatchTuning::Units::BALL_Z_METRES
                                : MatchTuning::Shooting::RELEASE_HEIGHT_METRES;
  const float goalDistance = distance(shooter.position, goalPoint);
  clearFlightState();
  launchBall(shooter, ball.position, goalPoint, speed,
             loftVerticalSpeed(goalDistance, speed, startHeight, crossingZ),
             0.0f);
  ball.z = startHeight / MatchTuning::Units::BALL_Z_METRES;
  ball.isShot = true;
  ball.shotByHome = shooter.isHomeTeam;
  ball.shotOnTarget = onTarget;
  ball.shotXG = xg;
  ball.shotTargetY = targetY;
  ball.shotTargetHeightMetres = crossingZ;
  ball.shotElapsedSeconds = 0.0f;
  ball.shotIsHeader = header;
  ball.shotIsPenalty = penalty;
  ball.shotSaveResolved = false;
  lastShooter = shooter.player;
  return xg;
}

void MatchEngine::takeShot(MatchPlayer& shooter, float forcedXG, bool header,
                           bool freeKick)
{
  const bool penalty = state == MatchState::PENALTY;
  const float xg = strikeShot(shooter, forcedXG, header, penalty, freeKick);

  PlayerMatchStats& shooterStats = statsOf(shooter);
  ++shooterStats.shots;
  shooterStats.expectedGoals += xg;
  const bool insideBox = inPenaltyArea(shooter.position, !shooter.isHomeTeam);
  const bool setPiece =
      penalty || restartIsSetPiece ||
      (setPiecePhaseRemaining > 0.0f && shooter.isHomeTeam == setPieceHome);
  ball.shotFromSetPiece = setPiece;
  if (shooter.isHomeTeam)
  {
    ++stats.homeShots;
    stats.homeShotXG += xg;
    if (insideBox) ++stats.homeShotsInsideBox;
    if (header) ++stats.homeHeadedShots;
    if (setPiece) ++stats.homeSetPieceShots;
  }
  else
  {
    ++stats.awayShots;
    stats.awayShotXG += xg;
    if (insideBox) ++stats.awayShotsInsideBox;
    if (header) ++stats.awayHeadedShots;
    if (setPiece) ++stats.awaySetPieceShots;
  }

  // Credit the pass that created the chance.
  if (!header)
  {
    shotAssistCandidate =
        lastCompletedReceiver == shooter.player ? lastCompletedPasser : nullptr;
  }
  if (MatchPlayer* creator = findMatchPlayer(shotAssistCandidate);
      creator && creator->isHomeTeam == shooter.isHomeTeam)
  {
    ++statsOf(*creator).keyPasses;
  }
  trackShot(shooter, xg, setPiece, header, penalty);

  MatchEvent& event = logEvent(MatchEventType::SHOT, shooter);
  if (header) event.detail = MatchEventDetail::HEADER;
  event.xg = xg;
  event.position = shooter.position;
  shooter.actionCooldown =
      randomFloat(MatchTuning::Shooting::MIN_ACTION_COOLDOWN,
                  MatchTuning::Shooting::MAX_ACTION_COOLDOWN);
  planGoalkeeperDive(!shooter.isHomeTeam, penalty);
}

void MatchEngine::launchBall(const MatchPlayer& kicker, Vector2F origin,
                             Vector2F target, float horizontalSpeed,
                             float verticalSpeed, float curve)
{
  const Vector2F direction = metricDirection(origin, target);
  ball.position = origin;
  ball.z = 0.0f;
  ball.velocity = {direction.x * horizontalSpeed,
                   direction.y * horizontalSpeed};
  ball.velocityZ = verticalSpeed;
  ball.curve = curve;
  ball.possessedBy = nullptr;
  ball.lastPossessor = kicker.player;
  ball.kicker = kicker.player;
  ball.kickerLockout = MatchTuning::Passing::KICKER_LOCKOUT_SECONDS;
  ball.touchAttempts = 0;
}

float MatchEngine::groundLaunchSpeed(float distanceMetres, float arrivalSpeed)
{
  // Rolling is one-dimensional and deterministic, so the distance a ball
  // rolls before stopping, D(v), is tabulated once with the live physics.
  // A ball launched at v0 arrives at va after D(v0) - D(va) metres.
  constexpr float SPEED_STEP = 0.25f;
  constexpr std::size_t ENTRIES = 161;
  static const std::array<float, ENTRIES> stoppingDistance = []
  {
    constexpr float DT = MatchTuning::Timing::FIXED_STEP_SECONDS /
                         static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
    std::array<float, ENTRIES> table{};
    for (std::size_t index = 1; index < ENTRIES; ++index)
    {
      FlightState flight{static_cast<float>(index) * SPEED_STEP, 0.0f, 0.0f,
                         false};
      float travelled = 0.0f;
      while (flight.horizontalSpeed > 0.0f) travelled += stepFlight(flight, DT);
      table[index] = travelled;
    }
    return table;
  }();
  const auto distanceFor = [&](float speed)
  {
    const float position =
        std::clamp(speed / SPEED_STEP, 0.0f, static_cast<float>(ENTRIES - 1));
    const auto low = static_cast<std::size_t>(position);
    const std::size_t high = std::min(low + 1, ENTRIES - 1);
    const float share = position - static_cast<float>(low);
    return stoppingDistance[low] +
           (stoppingDistance[high] - stoppingDistance[low]) * share;
  };
  const float needed = distanceFor(arrivalSpeed) + distanceMetres;
  const auto upper = std::ranges::lower_bound(stoppingDistance, needed);
  if (upper == stoppingDistance.end())
    return static_cast<float>(ENTRIES - 1) * SPEED_STEP;
  const auto index =
      static_cast<std::size_t>(std::distance(stoppingDistance.begin(), upper));
  if (index == 0) return 0.0f;
  const float below = stoppingDistance[index - 1];
  const float share =
      (needed - below) / std::max(stoppingDistance[index] - below, EPSILON);
  return (static_cast<float>(index - 1) + share) * SPEED_STEP;
}

float MatchEngine::loftVerticalSpeed(float distanceMetres,
                                     float horizontalSpeed, float startHeight,
                                     float arrivalHeight)
{
  // Bisection on the vertical launch speed with the live flight physics: the
  // height reached over the target distance rises with the launch angle.
  constexpr float DT = MatchTuning::Timing::FIXED_STEP_SECONDS /
                       static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
  const auto heightAt = [&](float vertical)
  {
    FlightState flight{horizontalSpeed, vertical, startHeight, false};
    float travelled = 0.0f;
    for (float time = 0.0f; time < MatchTuning::Ball::MAX_FLIGHT_SECONDS &&
                            flight.horizontalSpeed > 0.0f;
         time += DT)
    {
      const float previousHeight = flight.height;
      const float step = stepFlight(flight, DT);
      // Landing short reads as a height deficit that shrinks to zero as the
      // landing point reaches the target, so the root is continuous.
      if (flight.bounced)
        return -0.25f * std::max(0.0f, distanceMetres - travelled - step);
      if (travelled + step >= distanceMetres)
      {
        const float share =
            (distanceMetres - travelled) / std::max(step, EPSILON);
        return previousHeight + (flight.height - previousHeight) * share;
      }
      travelled += step;
    }
    return -1.0e6f;
  };
  // Probe from the drag-free estimate and its correction, step on (in
  // growing steps) until two probes bracket the arrival height, then close
  // in with the Illinois false-position method: a handful of flights, and
  // the lowest trajectory that gets there when two exist.
  constexpr float MAX_VERTICAL = 28.0f;
  constexpr float TOLERANCE_METRES = 0.03f;
  constexpr float MAX_STEP = 4.0f;
  constexpr int MAX_FLIGHTS = 14;
  // Flight time under quadratic air drag at the launch speed's drag
  // coefficient: x(t) = ln(1 + k v t) / k.
  const float launchSpeed = std::max(horizontalSpeed, 1.0f);
  const float drag = FLIGHT_DRAG_SCALE * dragCoefficient(launchSpeed);
  const float flightTime =
      std::expm1(drag * distanceMetres) / (drag * launchSpeed);
  float first = std::clamp((arrivalHeight - startHeight) / flightTime +
                               0.5f * MatchTuning::Ball::GRAVITY * flightTime,
                           0.0f, MAX_VERTICAL);
  float firstMiss = heightAt(first) - arrivalHeight;
  if (std::abs(firstMiss) < TOLERANCE_METRES) return first;
  float second = std::clamp(first - firstMiss / flightTime, 0.0f, MAX_VERTICAL);
  float secondMiss = heightAt(second) - arrivalHeight;
  int flights = 2;
  while ((firstMiss < 0.0f) == (secondMiss < 0.0f))
  {
    if (std::abs(secondMiss) < TOLERANCE_METRES) return second;
    const float step = std::clamp((second - first) * 1.5f, -MAX_STEP, MAX_STEP);
    const float next = std::clamp(second + step, 0.0f, MAX_VERTICAL);
    // Pinned at a bound (or not moving): nothing closer is reachable.
    if (next == second || flights >= MAX_FLIGHTS) return second;
    first = second;
    firstMiss = secondMiss;
    second = next;
    secondMiss = heightAt(second) - arrivalHeight;
    ++flights;
  }
  float below = firstMiss < 0.0f ? first : second;
  float belowMiss = firstMiss < 0.0f ? firstMiss : secondMiss;
  float above = firstMiss < 0.0f ? second : first;
  float aboveMiss = firstMiss < 0.0f ? secondMiss : firstMiss;
  if (std::abs(aboveMiss) < TOLERANCE_METRES) return above;
  int lastSide = 0;
  float estimate = above;
  for (; flights < MAX_FLIGHTS; ++flights)
  {
    estimate = below + (above - below) * belowMiss / (belowMiss - aboveMiss);
    const float miss = heightAt(estimate) - arrivalHeight;
    if (std::abs(miss) < TOLERANCE_METRES) return estimate;
    if (miss < 0.0f)
    {
      below = estimate;
      belowMiss = miss;
      if (lastSide < 0) aboveMiss *= 0.5f;
      lastSide = -1;
    }
    else
    {
      above = estimate;
      aboveMiss = miss;
      if (lastSide > 0) belowMiss *= 0.5f;
      lastSide = 1;
    }
  }
  return estimate;
}

void MatchEngine::planGoalkeeperDive(bool defendingHome, bool penalty)
{
  MatchPlayer* keeper = findGoalkeeper(defendingHome);
  if (!keeper) return;
  GoalkeeperControl& control = keepers[defendingHome ? 0 : 1];
  control.state = GoalkeeperState::DIVE;
  control.timer = 0.0f;
  control.lateralVelocity = 0.0f;
  if (penalty)
  {
    // From 11 m the keeper must commit at the kick: he picks a side, and a
    // good keeper reads the taker a little more often.
    const float shotSide =
        ball.shotTargetY < MatchTuning::Pitch::CENTRE ? -1.0f : 1.0f;
    float guess = randomFloat(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f;
    if (randomFloat(0.0f, 1.0f) <
        MatchTuning::Goalkeeper::PENALTY_READ_BASE +
            keeper->goalkeeping * MatchTuning::Goalkeeper::PENALTY_READ_SKILL)
    {
      guess = shotSide;
    }
    if (randomFloat(0.0f, 1.0f) < MatchTuning::Goalkeeper::PENALTY_STAY_CHANCE)
      guess = 0.0f;
    control.diveTargetY = MatchTuning::Pitch::CENTRE +
                          guess * MatchTuning::Goalkeeper::PENALTY_DIVE_METRES /
                              MatchTuning::Pitch::WIDTH_METRES;
    control.reactionRemaining = 0.0f;
    return;
  }

  // Predict where the shot crosses the keeper's plane and react after a
  // skill-dependent delay; defenders between ball and goal screen the shot.
  const float goalX = defendingHome ? 0.0f : 1.0f;
  const float span = goalX - ball.position.x;
  const float t =
      std::abs(span) > EPSILON
          ? std::clamp((keeper->position.x - ball.position.x) / span, 0.0f,
                       1.0f)
          : 1.0f;
  const float predictedY =
      ball.position.y + (ball.shotTargetY - ball.position.y) * t;
  const float readError =
      gaussian(rng) * MatchTuning::Goalkeeper::READ_ERROR_METRES *
      (1.0f - keeper->goalkeeping * 0.6f) / MatchTuning::Pitch::WIDTH_METRES;
  control.diveTargetY = std::clamp(
      predictedY + readError,
      MatchTuning::Pitch::GOAL_TOP - MatchTuning::Goalkeeper::DIVE_POST_MARGIN,
      MatchTuning::Pitch::GOAL_BOTTOM +
          MatchTuning::Goalkeeper::DIVE_POST_MARGIN);

  bool screened = false;
  for (const auto& other : players)
  {
    if (!active(other) || other.isGoalkeeper) continue;
    const float along = (other.position.x - ball.position.x) / span;
    if (along <= 0.05f || along >= 0.95f) continue;
    const float laneY =
        ball.position.y + (ball.shotTargetY - ball.position.y) * along;
    if (std::abs(other.position.y - laneY) * MatchTuning::Pitch::WIDTH_METRES <
        MatchTuning::Goalkeeper::SCREEN_WIDTH_METRES)
    {
      screened = true;
      break;
    }
  }
  const float reactionSeconds =
      MatchTuning::Goalkeeper::REACTION_BASE_SECONDS +
      (1.0f - keeper->goalkeeping) *
          MatchTuning::Goalkeeper::REACTION_SKILL_SECONDS +
      (screened ? MatchTuning::Goalkeeper::SCREENED_REACTION_SECONDS : 0.0f);
  // Flight substeps account for time between launch and the keeper's first
  // movement update. A restart launch can have no flight time in this tick.
  control.reactionRemaining = reactionSeconds;
}

void MatchEngine::diveGoalkeeper(MatchPlayer& keeper, float dt)
{
  GoalkeeperControl& control = keepers[keeper.isHomeTeam ? 0 : 1];
  control.timer += dt;
  if (!ball.isShot || ball.shotSaveResolved)
  {
    control.state = GoalkeeperState::RECOVER;
    control.timer = 0.0f;
    keeper.diveTimer = std::max(keeper.diveTimer,
                                MatchTuning::Goalkeeper::RECOVER_TIME_SECONDS);
    keeper.velocity = {0.0f, 0.0f};
    return;
  }
  if (control.reactionRemaining > 0.0f)
  {
    // A reaction can finish partway through a step. Use the remaining time
    // for movement instead of delaying the keeper for another whole step.
    const float waiting = std::min(control.reactionRemaining, dt);
    control.reactionRemaining -= waiting;
    dt -= waiting;
    if (dt <= 0.0F)
    {
      keeper.velocity = {.x = 0.0F, .y = 0.0F};
      return;
    }
  }
  // Lateral dive dynamics in m/s (the keeper moves across the width).
  // Move the visible body into reach rather than granting a distant save.
  constexpr float DIVE_CLOSING_GAIN = 1.25F;
  const float acceleration = MatchTuning::Goalkeeper::DIVE_ACCELERATION_METRES *
                             DIVE_CLOSING_GAIN /
                             MatchTuning::Pitch::WIDTH_METRES;
  const float maximumSpeed =
      std::min(currentTopSpeed(keeper),
               (MatchTuning::Goalkeeper::DIVE_BASE_SPEED_METRES +
                keeper.goalkeeping *
                    MatchTuning::Goalkeeper::DIVE_SKILL_SPEED_METRES) *
                   DIVE_CLOSING_GAIN) /
      MatchTuning::Pitch::WIDTH_METRES;
  const float offset = control.diveTargetY - keeper.position.y;
  const float direction = offset >= 0.0f ? 1.0f : -1.0f;
  control.lateralVelocity =
      std::clamp(control.lateralVelocity + direction * acceleration * dt,
                 -maximumSpeed, maximumSpeed);
  float step = control.lateralVelocity * dt;
  if (std::abs(step) >= std::abs(offset))
  {
    step = offset;
    control.lateralVelocity = 0.0f;
  }
  keeper.position.y =
      std::clamp(keeper.position.y + step, MatchTuning::Pitch::PLAYER_MIN_Y,
                 MatchTuning::Pitch::PLAYER_MAX_Y);
  keeper.velocity = {
      0.0f, step * MatchTuning::Pitch::WIDTH_METRES / std::max(dt, EPSILON)};
  keeper.movementTarget = keeper.position;
  keeper.isDiving = true;
}

Vector2F MatchEngine::goalkeeperTarget(MatchPlayer& keeper,
                                       const MatchPlayer* carrier)
{
  GoalkeeperControl& control = keepers[keeper.isHomeTeam ? 0 : 1];
  const auto setState = [&control](GoalkeeperState next)
  {
    if (control.state != next)
    {
      control.state = next;
      control.timer = 0.0f;
    }
  };
  control.timer += stepSeconds();
  const float goalX = keeper.isHomeTeam ? 0.0f : 1.0f;
  const float outward = keeper.isHomeTeam ? 1.0f : -1.0f;
  const Vector2F goalCentre{goalX, MatchTuning::Pitch::CENTRE};
  const float ballDepth =
      keeper.isHomeTeam ? ball.position.x : 1.0f - ball.position.x;

  if (ball.possessedBy == keeper.player)
  {
    setState(GoalkeeperState::HOLD);
    return keeper.position;
  }
  if (keeper.diveTimer > 0.0f)
  {
    setState(GoalkeeperState::RECOVER);
    return keeper.position;
  }
  if (control.state == GoalkeeperState::DISTRIBUTE &&
      control.timer < MatchTuning::Goalkeeper::DISTRIBUTE_TIME_SECONDS)
  {
    return keeper.position;
  }

  // Claim a high delivery dropping into the own box.
  if (!ball.possessedBy && ball.isAerialDelivery &&
      ball.passByHome != keeper.isHomeTeam)
  {
    // Anticipate high flight, then close on the ball itself once it drops
    // within handling height. Continuing to lead a low cross keeps the
    // keeper several metres ahead of it and prevents a physical catch.
    const float height = ball.z * MatchTuning::Units::BALL_Z_METRES;
    const float lookahead =
        height > MatchRules::goalkeeperReachMetres(keeper.heightMetres,
                                                   keeper.goalkeeping)
            ? MatchTuning::Goalkeeper::CLAIM_LOOKAHEAD_SECONDS
            : 0.0F;
    const Vector2F drift =
        toPitch({ball.velocity.x * lookahead, ball.velocity.y * lookahead});
    const Vector2F landing{ball.position.x + drift.x,
                           ball.position.y + drift.y};
    if (inPenaltyArea(landing, keeper.isHomeTeam) &&
        distance(landing, keeper.position) <
            MatchTuning::Goalkeeper::CLAIM_BOX_DEPTH_METRES)
    {
      setState(GoalkeeperState::CLAIM);
      return landing;
    }
  }

  // Sweep a loose ball or through ball he can reach before any attacker; a
  // sweeper keeper comes further and sooner, a line keeper hardly at all.
  const float keeperSweep = roleProfileOf(keeper).keeperSweep;
  if (!carrier && !ball.isShot &&
      ballDepth <
          MatchTuning::Pitch::GOALKEEPER_SWEEP_DEPTH *
              (1.0f + keeperSweep * TacticsTuning::KEEPER_SWEEP_DEPTH_GAIN) &&
      !(ball.isPass && ball.passByHome == keeper.isHomeTeam))
  {
    const float keeperDistance = distance(keeper.position, ball.position);
    bool keeperFirst = true;
    for (const auto& other : players)
    {
      if (!active(other) || other.isHomeTeam == keeper.isHomeTeam) continue;
      if (distance(other.position, ball.position) <
          keeperDistance * MatchTuning::Goalkeeper::SWEEP_ADVANTAGE *
              (1.0f - keeperSweep * TacticsTuning::KEEPER_SWEEP_ADVANTAGE_GAIN))
      {
        keeperFirst = false;
        break;
      }
    }
    if (keeperFirst)
    {
      setState(GoalkeeperState::SWEEP);
      const float lookahead =
          std::min(keeperDistance / std::max(currentTopSpeed(keeper), EPSILON),
                   MatchTuning::Player::MAX_LOOSE_BALL_LOOKAHEAD_SECONDS);
      const Vector2F lead =
          toPitch({ball.velocity.x * lookahead, ball.velocity.y * lookahead});
      return {ball.position.x + lead.x, ball.position.y + lead.y};
    }
  }

  // Rush a carrier who is through on goal inside the box.
  if (carrier && carrier->isHomeTeam != keeper.isHomeTeam &&
      inPenaltyArea(carrier->position, keeper.isHomeTeam))
  {
    bool covered = false;
    for (const auto& other : players)
    {
      if (!active(other) || other.isHomeTeam != keeper.isHomeTeam ||
          other.isGoalkeeper)
      {
        continue;
      }
      if ((carrier->position.x - other.position.x) * outward > 0.0f &&
          distance(other.position, carrier->position) <
              MatchTuning::Goalkeeper::RUSH_COVER_DISTANCE_METRES)
      {
        covered = true;
        break;
      }
    }
    if (!covered)
    {
      setState(GoalkeeperState::RUSH);
      return {goalCentre.x + (carrier->position.x - goalCentre.x) *
                                 MatchTuning::Goalkeeper::RUSH_CLOSING_SHARE,
              goalCentre.y + (carrier->position.y - goalCentre.y) *
                                 MatchTuning::Goalkeeper::RUSH_CLOSING_SHARE};
    }
  }

  // Set position on the ball-goal line, advancing to narrow the angle as the
  // threat approaches and acting as a sweeper when play is far away.
  setState(GoalkeeperState::SET_POSITION);
  const Vector2F threat = carrier ? carrier->position : ball.position;
  const float threatMetres = distance(threat, goalCentre);
  float depthMetres = MatchTuning::Goalkeeper::NEAR_DEPTH_METRES +
                      threatMetres * MatchTuning::Goalkeeper::DEPTH_PER_METRE;
  if (threatMetres > MatchTuning::Goalkeeper::SWEEPER_DISTANCE_METRES)
  {
    depthMetres +=
        (threatMetres - MatchTuning::Goalkeeper::SWEEPER_DISTANCE_METRES) *
        MatchTuning::Goalkeeper::SWEEPER_DEPTH_PER_METRE;
  }
  // A sweeper keeper stands higher (a line keeper deeper) the further away
  // the play is.
  const float keeperDepth = roleProfileOf(keeper).keeperDepthMetres;
  depthMetres = std::clamp(
      depthMetres + keeperDepth * std::min(1.0f, threatMetres / 40.0f), 0.5f,
      MatchTuning::Goalkeeper::MAX_DEPTH_METRES + std::max(0.0f, keeperDepth));
  const float dxMetres =
      (threat.x - goalCentre.x) * MatchTuning::Pitch::LENGTH_METRES;
  const float dyMetres =
      (threat.y - goalCentre.y) * MatchTuning::Pitch::WIDTH_METRES;
  const float norm =
      std::max(std::sqrt(dxMetres * dxMetres + dyMetres * dyMetres), EPSILON);
  return {goalCentre.x +
              dxMetres / norm * depthMetres / MatchTuning::Pitch::LENGTH_METRES,
          std::clamp(goalCentre.y + dyMetres / norm * depthMetres /
                                        MatchTuning::Pitch::WIDTH_METRES,
                     MatchTuning::Pitch::GOALKEEPER_MIN_Y,
                     MatchTuning::Pitch::GOALKEEPER_MAX_Y)};
}

void MatchEngine::updateBall(float dt)
{
  // Integrate short flight segments so a fast ball cannot jump through a
  // keeper or a line between player ticks. Keep this resolution order: shot
  // save at the keeper's plane, goal/out-of-bounds, then other loose contacts.
  // Every resolver may change state or possession, ending the remaining flight.
  // The sub-step length is the same at every fidelity.
  const int substeps =
      MatchTuning::Timing::BALL_SUBSTEPS * static_cast<int>(stepTicks);
  const float substep = dt / static_cast<float>(substeps);
  for (int index = 0; index < substeps; ++index)
  {
    if (ball.possessedBy || state != MatchState::PLAYING) return;
    substepBallPosition = ball.position;
    substepBallZ = ball.z;
    integrateBall(substep);

    // A shot that has died (smothered, spent or trickling) is a loose ball.
    if (ball.isShot && ball.shotElapsedSeconds > 0.3f &&
        length(ball.velocity) < MatchTuning::Ball::DEAD_SHOT_SPEED)
    {
      ball.isShot = false;
      ball.shotOnTarget = false;
      ball.touchAttempts = 0;
    }
    if (ball.isShot)
    {
      ball.shotElapsedSeconds += substep;
      // Headers can launch mid-flight; restarts may not fly this tick.
      // Later ticks age the reaction in diveGoalkeeper instead.
      ageInitialGoalkeeperReaction(
          keepers.at(static_cast<std::size_t>(ball.shotByHome)), substep);
      MatchPlayer* keeper = findGoalkeeper(!ball.shotByHome);
      if (!ball.shotSaveResolved && keeper)
      {
        // Resolve the save when the ball reaches the keeper's plane.
        const float before = substepBallPosition.x - keeper->position.x;
        const float after = ball.position.x - keeper->position.x;
        if ((before > 0.0f) != (after > 0.0f) || std::abs(after) <= EPSILON)
          resolveShotAtGoalkeeper(*keeper);
        if (ball.possessedBy || state != MatchState::PLAYING) return;
      }
    }
    checkOutOfBounds();
    if (state != MatchState::PLAYING) return;
    resolveLooseBall();
  }
}

void MatchEngine::integrateBall(float dt)
{
  const float speed = length(ball.velocity);
  Vector2F direction = speed > EPSILON ? Vector2F{ball.velocity.x / speed,
                                                  ball.velocity.y / speed}
                                       : Vector2F{0.0f, 0.0f};
  if (std::abs(ball.curve) > EPSILON)
  {
    // Spin bends the flight (a Magnus-effect stand-in) and slowly decays.
    const float rotation = ball.curve * dt;
    const float cosine = std::cos(rotation);
    const float sine = std::sin(rotation);
    direction = {direction.x * cosine - direction.y * sine,
                 direction.x * sine + direction.y * cosine};
    ball.curve *= 1.0f - MatchTuning::Ball::CURVE_DECAY_PER_SECOND * dt;
  }
  FlightState flight{speed, ball.velocityZ,
                     ball.z * MatchTuning::Units::BALL_Z_METRES, false};
  const float travelled = stepFlight(flight, dt);
  // A delivery that has bounced is no longer a clean aerial ball.
  if (flight.bounced) ball.isAerialDelivery = false;
  const Vector2F step =
      toPitch({direction.x * travelled, direction.y * travelled});
  ball.position.x += step.x;
  ball.position.y += step.y;
  ball.z = flight.height / MatchTuning::Units::BALL_Z_METRES;
  ball.velocityZ = flight.verticalSpeed;
  ball.velocity = {direction.x * flight.horizontalSpeed,
                   direction.y * flight.horizontalSpeed};
  if (flight.horizontalSpeed <= 0.0f) ball.curve = 0.0f;
}

MatchEngine::SaveAttempt MatchEngine::attemptSave(const MatchPlayer& keeper)
{
  const float span = ball.position.x - substepBallPosition.x;
  const float t =
      std::abs(span) > EPSILON
          ? std::clamp((keeper.position.x - substepBallPosition.x) / span, 0.0f,
                       1.0f)
          : 1.0f;
  const float crossingY =
      substepBallPosition.y + (ball.position.y - substepBallPosition.y) * t;
  const float crossingHeight = (substepBallZ + (ball.z - substepBallZ) * t) *
                               MatchTuning::Units::BALL_Z_METRES;
  const float gapMetres = std::abs(crossingY - keeper.position.y) *
                          MatchTuning::Pitch::WIDTH_METRES;

  const float lateralReach = MatchRules::goalkeeperHandlingRadiusMetres(
      keeper.heightMetres, keeper.goalkeeping, crossingHeight, keeper.isDiving);
  if (lateralReach <= 0.0F || gapMetres > lateralReach) return {};

  const float stretch = gapMetres / std::max(lateralReach, EPSILON);
  const float speedExcess =
      std::max(0.0f, length(ball.velocity) -
                         MatchTuning::Goalkeeper::COMFORT_SPEED_METRES);
  const float saveChance =
      std::clamp(
          MatchTuning::Goalkeeper::SAVE_BASE -
              stretch * stretch *
                  MatchTuning::Goalkeeper::SAVE_STRETCH_PENALTY -
              speedExcess * MatchTuning::Goalkeeper::SAVE_SPEED_PENALTY +
              (keeper.goalkeeping - MatchTuning::Player::DEFAULT_ATTRIBUTE) *
                  MatchTuning::Goalkeeper::SAVE_SKILL_BONUS,
          MatchTuning::Goalkeeper::MIN_SAVE_CHANCE,
          MatchTuning::Goalkeeper::MAX_SAVE_CHANCE) *
      (ball.shotIsPenalty ? MatchTuning::Goalkeeper::PENALTY_SAVE_FACTOR
                          : 1.0f);
  if (randomFloat(0.0f, 1.0f) >= saveChance) return {};
  return {true, stretch, speedExcess, crossingHeight};
}

void MatchEngine::resolveShotAtGoalkeeper(MatchPlayer& keeper)
{
  ball.shotSaveResolved = true;
  if (!ball.shotOnTarget) return;
  const SaveAttempt attempt = attemptSave(keeper);
  if (!attempt.saved) return;
  const float stretch = attempt.stretch;
  const float speedExcess = attempt.speedExcess;
  const float crossingHeight = attempt.crossingHeight;

  const float holdChance =
      std::clamp(MatchTuning::Goalkeeper::HOLD_BASE +
                     keeper.goalkeeping * MatchTuning::Goalkeeper::HOLD_SKILL -
                     stretch * MatchTuning::Goalkeeper::HOLD_STRETCH_PENALTY -
                     speedExcess * MatchTuning::Goalkeeper::HOLD_SPEED_PENALTY,
                 MatchTuning::Goalkeeper::MIN_HOLD_CHANCE,
                 MatchTuning::Goalkeeper::MAX_HOLD_CHANCE);
  if (randomFloat(0.0f, 1.0f) < holdChance)
  {
    makeSave(keeper);
    return;
  }
  parryShot(keeper, crossingHeight > MatchTuning::Goalkeeper::TIP_OVER_METRES ||
                        stretch > MatchTuning::Goalkeeper::TIP_AROUND_STRETCH);
}

void MatchEngine::parryShot(MatchPlayer& keeper, bool overTheBar)
{
  MatchPlayer* shooter = findMatchPlayer(lastShooter);
  if (keeper.isHomeTeam)
  {
    ++stats.homeSaves;
    ++stats.awayOnTarget;
  }
  else
  {
    ++stats.awaySaves;
    ++stats.homeOnTarget;
  }
  ++statsOf(keeper).saves;
  if (shooter) ++statsOf(*shooter).shotsOnTarget;
  keeper.diveTimer = MatchTuning::Goalkeeper::RECOVER_TIME_SECONDS;
  keeper.isDiving = true;
  MatchEvent& event = logEvent(MatchEventType::SAVE, keeper);
  event.detail =
      overTheBar ? MatchEventDetail::TIPPED_BEHIND : MatchEventDetail::PARRIED;
  event.secondaryPlayerId = lastShooter ? lastShooter->getId() : 0;
  ball.isShot = false;
  ball.shotOnTarget = false;
  ball.lastPossessor = keeper.player;
  if (overTheBar)
  {
    setupCorner(!keeper.isHomeTeam,
                ball.position.y < MatchTuning::Pitch::CENTRE);
    return;
  }
  // The ball spills back into the box where a rebound can be contested.
  const float outward = keeper.isHomeTeam ? 1.0f : -1.0f;
  const float speed =
      length(ball.velocity) * MatchTuning::Goalkeeper::PARRY_SPEED_SHARE;
  ball.velocity = {outward * speed * randomFloat(0.35f, 0.9f),
                   speed * randomFloat(-0.9f, 0.9f)};
  ball.velocityZ =
      randomFloat(0.0f, MatchTuning::Goalkeeper::PARRY_MAX_VERTICAL_SPEED);
  ball.position.x = keeper.position.x + outward * 0.004f;
  ball.kicker = keeper.player;
  ball.kickerLockout = MatchTuning::Passing::KICKER_LOCKOUT_SECONDS;
  ball.touchAttempts = 0;
}

void MatchEngine::recordPassCompletion(MatchPlayer& receiver)
{
  if (!ball.isPass) return;
  MatchPlayer* passer = findMatchPlayer(ball.lastPossessor);
  if (receiver.isHomeTeam == ball.passByHome)
  {
    if (receiver.isHomeTeam)
      ++stats.homePassesCompleted;
    else
      ++stats.awayPassesCompleted;
    if (passer && passer->isHomeTeam == receiver.isHomeTeam &&
        passer != &receiver)
    {
      ++statsOf(*passer).passesCompleted;
      trackPassCompletion(*passer, receiver);
      lastCompletedPasser = passer->player;
      lastCompletedReceiver = receiver.player;
    }
    return;
  }
  ++statsOf(receiver).interceptions;
}

void MatchEngine::resolveLooseBall()
{
  if (ball.possessedBy) return;
  using B = MatchTuning::Ball;

  const float endHeight = ball.z * MatchTuning::Units::BALL_Z_METRES;
  if (!ball.isShot && endHeight > MatchTuning::Aerial::CONTROL_CEILING_METRES)
  {
    resolveAerialContest();
    return;
  }

  // Swept contact over the last sub-step: every player whose reach the ball
  // passed through is a candidate, taken in the order the ball met them.
  const Vector2F start = toMetres(substepBallPosition);
  const Vector2F end = toMetres(ball.position);
  const Vector2F segment{end.x - start.x, end.y - start.y};
  const float segmentSquared = segment.x * segment.x + segment.y * segment.y;
  const float startHeight = substepBallZ * MatchTuning::Units::BALL_Z_METRES;
  const float ballSpeed = length(ball.velocity);
  // A ball that has slowed down can be collected by anyone again, including
  // players it flew past earlier.
  if (!ball.isShot && ballSpeed <= B::MAX_HEAVY_TOUCH_SPEED)
    ball.touchAttempts = 0;
  // Handling is allowed in the own area, except for a team-mate's pass.
  const auto handsAllowed = [this](const MatchPlayer& player)
  {
    return player.isGoalkeeper &&
           inPenaltyArea(player.position, player.isHomeTeam) &&
           !(ball.isPass && ball.passByHome == player.isHomeTeam);
  };
  struct Contact
  {
    std::size_t slot = 0;
    float along = 0.0f;
    float gap = 0.0f;
    float height = 0.0f;
    float reach = 0.0f;
  };
  std::array<Contact, 32> contacts{};
  std::size_t contactCount = 0;
  // Nobody further than the largest reach from the segment's bounding box can
  // touch the ball; the margin keeps the exact test below authoritative.
  constexpr float REACH_BOUND = B::GOALKEEPER_CONTROL_RADIUS_METRES + 0.01f;
  const float boundMinX = std::min(start.x, end.x) - REACH_BOUND;
  const float boundMaxX = std::max(start.x, end.x) + REACH_BOUND;
  const float boundMinY = std::min(start.y, end.y) - REACH_BOUND;
  const float boundMaxY = std::max(start.y, end.y) + REACH_BOUND;
  for (std::size_t slot = 0; slot < players.size() && slot < contacts.size();
       ++slot)
  {
    const MatchPlayer& player = players[slot];
    const Vector2F position = toMetres(player.position);
    if (position.x < boundMinX || position.x > boundMaxX ||
        position.y < boundMinY || position.y > boundMaxY)
      continue;
    if (!active(player) || player.trapTimer > 0.0f) continue;
    if (ball.kickerLockout > 0.0f && ball.kicker == player.player) continue;
    if ((ball.touchAttempts & (1U << slot)) != 0U) continue;
    // Team-mates let a pass run to its receiver, and never block their own
    // side's shot.
    if (ball.isPass && player.isHomeTeam == ball.passByHome &&
        player.player != ball.intendedReceiver &&
        ballSpeed > B::MAX_HEAVY_TOUCH_SPEED)
      continue;
    if (ball.isShot && player.isHomeTeam == ball.shotByHome) continue;

    const float along = segmentSquared > EPSILON
                            ? std::clamp(((position.x - start.x) * segment.x +
                                          (position.y - start.y) * segment.y) /
                                             segmentSquared,
                                         0.0f, 1.0f)
                            : 0.0f;
    const float gapX = position.x - (start.x + segment.x * along);
    const float gapY = position.y - (start.y + segment.y * along);
    const float gapSquared = gapX * gapX + gapY * gapY;
    // Cheap reject beyond the largest reach before any square root.
    if (gapSquared > B::GOALKEEPER_CONTROL_RADIUS_METRES *
                         B::GOALKEEPER_CONTROL_RADIUS_METRES)
      continue;
    const float gap = std::sqrt(gapSquared);
    const float height = startHeight + (endHeight - startHeight) * along;
    const bool hands = handsAllowed(player);
    float reach = 0.0f;
    if (ball.isShot)
    {
      const bool reachable =
          hands ? height <= MatchRules::goalkeeperReachMetres(
                                player.heightMetres, player.goalkeeping)
                : height <= MatchRules::headerReachMetres(player.heightMetres,
                                                          player.physicality);
      if (reachable)
        reach = hands ? MatchRules::goalkeeperHandlingRadiusMetres(
                            player.heightMetres, player.goalkeeping, height,
                            player.isDiving)
                      : MatchTuning::Defending::BLOCK_DISTANCE_METRES;
    }
    else if (hands)
    {
      reach = MatchRules::goalkeeperHandlingRadiusMetres(
          player.heightMetres, player.goalkeeping, height, player.isDiving);
    }
    else
    {
      reach = height <= B::LOW_BALL_METRES ? B::OUTFIELD_CONTROL_RADIUS_METRES
                                           : B::CHEST_CONTROL_RADIUS_METRES;
    }
    if (reach <= 0.0f || gap > reach) continue;
    contacts[contactCount++] = {slot, along, gap, height, reach};
  }
  insertionSort(contacts, contactCount,
                [](const Contact& first, const Contact& second)
                {
                  if (first.along != second.along)
                    return first.along < second.along;
                  return first.slot < second.slot;
                });

  for (std::size_t index = 0; index < contactCount; ++index)
  {
    const Contact& contact = contacts[index];
    MatchPlayer& player = players[contact.slot];
    ball.touchAttempts |= 1U << contact.slot;
    const float stretch = contact.gap / std::max(contact.reach, EPSILON);

    if (ball.isShot)
    {
      // Saves on target are resolved at the keeper's plane; here a keeper
      // gathers a wide shot near him, or an outfielder throws his body in.
      if (player.isGoalkeeper)
      {
        if (!ball.shotOnTarget)
        {
          setPossession(player);
          player.actionCooldown = randomFloat(B::MIN_KEEPER_HOLD_SECONDS,
                                              B::MAX_KEEPER_HOLD_SECONDS);
          return;
        }
        continue;
      }
      if (randomFloat(0.0f, 1.0f) >=
          (MatchTuning::Defending::BASE_BLOCK_CHANCE +
           player.defending * MatchTuning::Defending::DEFENDING_BLOCK_BONUS) *
              (1.0f - 0.5f * stretch))
      {
        continue;
      }
      ball.lastPossessor = player.player;
      ball.kicker = player.player;
      ball.kickerLockout = MatchTuning::Passing::KICKER_LOCKOUT_SECONDS;
      ball.touchAttempts = 0;
      ball.isShot = false;
      ball.shotOnTarget = false;
      ball.fromThrowIn = false;
      ball.velocity.x *= MatchTuning::Defending::DEFLECTION_SPEED_FACTOR;
      ball.velocity.y +=
          randomFloat(-MatchTuning::Defending::MAX_DEFLECTION_Y_SPEED,
                      MatchTuning::Defending::MAX_DEFLECTION_Y_SPEED);
      const float ownDepth =
          player.isHomeTeam ? player.position.x : 1.0f - player.position.x;
      if (ownDepth < MatchTuning::SetPiece::BLOCK_BEHIND_DEPTH &&
          randomFloat(0.0f, 1.0f) < MatchTuning::SetPiece::BLOCK_BEHIND_CHANCE)
      {
        clearBehind(player);
      }
      ++statsOf(player).clearances;
      logEvent(MatchEventType::SHOT_BLOCKED, player);
      return;
    }

    // Anyone other than the intended receiver has to stretch and react to
    // get a touch: close balls are met, fast wide ones fly past.
    const bool intended = ball.isPass && ball.intendedReceiver == player.player;
    const bool hands = handsAllowed(player);
    if (!intended && !hands && ballSpeed > B::MAX_HEAVY_TOUCH_SPEED)
    {
      const float reaction =
          std::clamp(B::INTERCEPT_BASE_CHANCE +
                         player.defending * B::INTERCEPT_DEFENDING_BONUS -
                         ballSpeed * B::INTERCEPT_SPEED_PENALTY,
                     B::INTERCEPT_MIN_CHANCE, 1.0f);
      const float closeness = (1.0f - stretch) * (1.0f - stretch);
      if (randomFloat(0.0f, 1.0f) >= reaction + (1.0f - reaction) * closeness)
        continue;
    }

    // A defender meeting a cross close to his own goal line often plays safe
    // and puts it behind, and now and then turns it into his own net.
    const float ownDepth =
        player.isHomeTeam ? player.position.x : 1.0f - player.position.x;
    const bool delivery = ball.isPass && ball.passByHome != player.isHomeTeam &&
                          !player.isGoalkeeper &&
                          (lastPassDecision.intent == PassIntent::CROSS ||
                           lastPassDecision.intent == PassIntent::CUTBACK ||
                           lastPassDecision.intent == PassIntent::SET_PIECE);
    if (delivery && ownDepth < MatchTuning::SetPiece::CROSS_CLEARANCE_DEPTH)
    {
      ++statsOf(player).interceptions;
      if (tryOwnGoalTouch(player)) return;
      if (randomFloat(0.0f, 1.0f) <
          MatchTuning::SetPiece::CLEARANCE_BEHIND_CHANCE)
      {
        clearFlightState();
        clearBehind(player);
        return;
      }
      --statsOf(player).interceptions;
    }

    const float touchSkill =
        hands ? player.goalkeeping
              : std::max({player.dribbling,
                          player.passing * B::PASSING_TOUCH_WEIGHT,
                          player.isGoalkeeper
                              ? player.goalkeeping * B::KEEPER_FEET_WEIGHT
                              : 0.0f});
    const float controlChance =
        std::clamp(B::BASE_CONTROL_CHANCE + touchSkill * B::TOUCH_SKILL_BONUS -
                       ballSpeed * B::SPEED_CONTROL_PENALTY -
                       (contact.height > B::LOW_BALL_METRES ? 0.08f : 0.0f),
                   B::MIN_CONTROL_CHANCE, B::MAX_CONTROL_CHANCE);
    if (randomFloat(0.0f, 1.0f) > controlChance)
    {
      // A heavy first touch: the ball is deadened but pops a metre or two
      // away from the player instead of rolling on at full pace.
      player.isTrapping = true;
      player.trapTimer = B::FAILED_TRAP_TIME;
      const float angle =
          randomFloat(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);
      const float popSpeed =
          randomFloat(B::MIN_HEAVY_TOUCH_SPEED, B::MAX_HEAVY_TOUCH_SPEED);
      ball.velocity = {ball.velocity.x * B::HEAVY_TOUCH_RETAINED +
                           std::cos(angle) * popSpeed,
                       ball.velocity.y * B::HEAVY_TOUCH_RETAINED +
                           std::sin(angle) * popSpeed};
      // Near his own goal a player's heavy touch still goes away from it.
      const float outward = player.isHomeTeam ? 1.0f : -1.0f;
      if (ownDepth < MatchTuning::SetPiece::CROSS_CLEARANCE_DEPTH &&
          ball.velocity.x * outward < 0.0f)
        ball.velocity.x = -ball.velocity.x;
      ball.velocityZ = 0.0f;
      ball.z = 0.0f;
      ball.curve = 0.0f;
      ball.lastPossessor = player.player;
      ball.kicker = player.player;
      ball.kickerLockout = B::FAILED_TOUCH_LOCKOUT;
      ball.touchAttempts = 0;
      return;
    }

    if (ball.isPass && ball.passWasOffside && intended)
    {
      if (player.isHomeTeam)
        ++stats.homeOffsides;
      else
        ++stats.awayOffsides;
      MatchEvent& event = logEvent(MatchEventType::OFFSIDE, player);
      event.position = player.position;
      setupFreeKick(!player.isHomeTeam, player.position);
      return;
    }

    recordPassCompletion(player);
    setPossession(player);
    player.isTrapping = true;
    player.trapTimer =
        B::BASE_TRAP_TIME + (1.0f - touchSkill) * B::TRAP_SKILL_PENALTY;
    player.actionCooldown =
        hands ? randomFloat(B::MIN_KEEPER_HOLD_SECONDS,
                            B::MAX_KEEPER_HOLD_SECONDS)
              : player.trapTimer + randomFloat(B::MIN_POST_TOUCH_DELAY,
                                               B::MAX_POST_TOUCH_DELAY);
    return;
  }
}

bool MatchEngine::resolveAerialContest()
{
  const float heightMetres = ball.z * MatchTuning::Units::BALL_Z_METRES;
  // A goalkeeper in his own box claims first when he can reach the ball.
  for (auto& player : players)
  {
    if (!active(player) || !player.isGoalkeeper ||
        (ball.kickerLockout > 0.0f && ball.kicker == player.player) ||
        !inPenaltyArea(player.position, player.isHomeTeam) ||
        distance(player.position, ball.position) >
            MatchRules::goalkeeperHandlingRadiusMetres(
                player.heightMetres, player.goalkeeping, heightMetres,
                player.isDiving) ||
        heightMetres > MatchRules::goalkeeperReachMetres(player.heightMetres,
                                                         player.goalkeeping))
    {
      continue;
    }
    if (ball.isPass && ball.passByHome == player.isHomeTeam) continue;
    int crowding = 0;
    for (const auto& other : players)
    {
      if (active(other) && other.isHomeTeam != player.isHomeTeam &&
          distance(other.position, ball.position) <
              MatchTuning::Aerial::DUEL_RADIUS_METRES)
      {
        ++crowding;
      }
    }
    const float claimChance = std::clamp(
        MatchTuning::Aerial::BASE_CLAIM_CHANCE +
            player.goalkeeping * MatchTuning::Aerial::SKILL_CLAIM_BONUS -
            static_cast<float>(crowding) *
                MatchTuning::Aerial::CROWDING_PENALTY,
        0.05f, 0.97f);
    GoalkeeperControl& control = keepers[player.isHomeTeam ? 0 : 1];
    control.state = GoalkeeperState::CLAIM;
    control.timer = 0.0f;
    if (randomFloat(0.0f, 1.0f) < claimChance)
    {
      recordPassCompletion(player);
      setPossession(player);
      player.actionCooldown =
          randomFloat(MatchTuning::Ball::MIN_KEEPER_HOLD_SECONDS,
                      MatchTuning::Ball::MAX_KEEPER_HOLD_SECONDS);
      return true;
    }
    // Punched clear under pressure.
    ++statsOf(player).clearances;
    const float outward = player.isHomeTeam ? 1.0f : -1.0f;
    ball.lastPossessor = player.player;
    clearFlightState();
    ball.velocity = {outward * MatchTuning::Aerial::HEADER_CLEARANCE_SPEED *
                         randomFloat(0.5f, 0.9f),
                     randomFloat(-5.0f, 5.0f)};
    ball.velocityZ = MatchTuning::Aerial::HEADER_LIFT;
    ball.kicker = player.player;
    ball.kickerLockout = MatchTuning::Ball::FAILED_TOUCH_LOCKOUT;
    ball.touchAttempts = 0;
    return true;
  }

  // Otherwise everyone who can reach the ball contests it in the air.
  std::array<MatchPlayer*, 6> candidates{};
  std::array<float, 6> strengths{};
  std::size_t count = 0;
  float totalStrength = 0.0f;
  bool homeInvolved = false;
  bool awayInvolved = false;
  for (auto& player : players)
  {
    if (!active(player) || player.isGoalkeeper || count == candidates.size() ||
        player.trapTimer > 0.0f ||
        (ball.kickerLockout > 0.0f && ball.kicker == player.player))
      continue;
    if (distance(player.position, ball.position) >
        MatchTuning::Aerial::DUEL_RADIUS_METRES)
      continue;
    // Defenders are goal-side and facing the ball, so they win the first
    // contact more often than the attackers they mark.
    const bool defendingDelivery =
        ball.isPass && ball.passByHome != player.isHomeTeam;
    const float strength =
        MatchRules::aerialDuelStrength(player.heightMetres, player.physicality,
                                       heightMetres) *
        (defendingDelivery ? MatchTuning::Aerial::DEFENDER_DUEL_ADVANTAGE
                           : 1.0f);
    if (strength <= 0.0f) continue;
    candidates[count] = &player;
    strengths[count] = strength;
    totalStrength += strength;
    ++count;
    (player.isHomeTeam ? homeInvolved : awayInvolved) = true;
  }
  if (count == 0) return false;

  float pick = randomFloat(0.0f, totalStrength);
  std::size_t winnerIndex = count - 1;
  for (std::size_t index = 0; index < count; ++index)
  {
    pick -= strengths[index];
    if (pick <= 0.0f)
    {
      winnerIndex = index;
      break;
    }
  }
  MatchPlayer& winner = *candidates[winnerIndex];
  const bool contested = homeInvolved && awayInvolved;
  if (contested)
  {
    for (std::size_t index = 0; index < count; ++index)
    {
      MatchPlayer& candidate = *candidates[index];
      if (&candidate == &winner)
        ++statsOf(candidate).aerialDuelsWon;
      else if (candidate.isHomeTeam != winner.isHomeTeam)
        ++statsOf(candidate).aerialDuelsLost;
    }
    if (winner.isHomeTeam)
      ++stats.homeAerialDuelsWon;
    else
      ++stats.awayAerialDuelsWon;

    // Pushing or holding in the air: the defender's foul in his own box is a
    // penalty; an attacker's push gives the defence a free kick.
    if (incidentRoll() < MatchTuning::Aerial::DUEL_FOUL_CHANCE)
    {
      MatchPlayer* opponent = nullptr;
      for (std::size_t index = 0; index < count; ++index)
      {
        if (candidates[index]->isHomeTeam != winner.isHomeTeam)
        {
          opponent = candidates[index];
          break;
        }
      }
      if (opponent)
      {
        const bool attackerFouls = incidentRoll() < 0.6f;
        const bool winnerAttacking = ball.passByHome == winner.isHomeTeam;
        MatchPlayer& attacker = winnerAttacking ? winner : *opponent;
        MatchPlayer& defender = winnerAttacking ? *opponent : winner;
        clearFlightState();
        ball.velocity = {0.0f, 0.0f};
        ball.velocityZ = 0.0f;
        if (attackerFouls)
          commitFoul(attacker, defender, true, false);
        else
          commitFoul(defender, attacker, true, false);
        return true;
      }
    }
  }
  headBall(winner);
  return true;
}

bool MatchEngine::tryOwnGoalTouch(MatchPlayer& defender)
{
  using S = MatchTuning::SetPiece;
  // Only a touch in front of his own goal can go in off a defender.
  const float goalLineMetres =
      (defender.isHomeTeam ? defender.position.x : 1.0f - defender.position.x) *
      MatchTuning::Pitch::LENGTH_METRES;
  const float lateralMetres =
      std::abs(defender.position.y - MatchTuning::Pitch::CENTRE) *
      MatchTuning::Pitch::WIDTH_METRES;
  if (goalLineMetres > S::OWN_GOAL_TOUCH_DEPTH_METRES ||
      lateralMetres > S::OWN_GOAL_TOUCH_WIDTH_METRES ||
      randomFloat(0.0f, 1.0f) >= S::OWN_GOAL_TOUCH_CHANCE)
    return false;
  // The ball skews off him toward the goal he defends.
  const float goalX = defender.isHomeTeam ? -0.01f : 1.01f;
  const float aimMetres =
      randomFloat(-MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES,
                  MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES);
  const Vector2F target{goalX,
                        MatchTuning::Pitch::CENTRE +
                            aimMetres / MatchTuning::Pitch::WIDTH_METRES};
  // An accidental defensive touch does not end the attacking restart's
  // origin. Preserve it across the replacement flight for goal accounting.
  const bool fromSetPiece =
      ball.shotFromSetPiece ||
      (setPiecePhaseRemaining > 0.0F && defender.isHomeTeam != setPieceHome);
  const float height = ball.z;
  clearFlightState();
  launchBall(defender, ball.position, target,
             randomFloat(S::OWN_GOAL_MIN_SPEED, S::OWN_GOAL_MAX_SPEED),
             randomFloat(0.0f, S::OWN_GOAL_MAX_LIFT), 0.0f);
  ball.z = height;
  ball.shotFromSetPiece = fromSetPiece;
  lastShooter = nullptr;
  return true;
}

void MatchEngine::clearBehind(MatchPlayer& defender)
{
  // Knock the ball over the own goal line, wide of the posts.
  const float goalX = defender.isHomeTeam ? 0.0f : 1.0f;
  const float side =
      defender.position.y < MatchTuning::Pitch::CENTRE ? -1.0f : 1.0f;
  const Vector2F target{
      goalX + (defender.isHomeTeam ? -0.02f : 0.02f),
      MatchTuning::Pitch::CENTRE +
          side * randomFloat(MatchTuning::Pitch::GOAL_BOTTOM -
                                 MatchTuning::Pitch::CENTRE + 0.06f,
                             0.45f)};
  const float height = ball.z;
  launchBall(defender, defender.position, target,
             MatchTuning::Aerial::HEADER_CLEARANCE_SPEED,
             MatchTuning::Aerial::HEADER_LIFT * 0.5f, 0.0f);
  ball.z = height;
  lastShooter = nullptr;
}

void MatchEngine::clearBall(MatchPlayer& defender)
{
  trackTouch(defender);
  // A long, high clearance upfield and towards the nearer touchline.
  const float forward = defender.isHomeTeam ? 1.0f : -1.0f;
  const float touchline =
      defender.position.y < MatchTuning::Pitch::CENTRE ? -1.0f : 1.0f;
  const float clearanceMetres =
      randomFloat(MatchTuning::Defending::CLEARANCE_MIN_DISTANCE_METRES,
                  MatchTuning::Defending::CLEARANCE_MAX_DISTANCE_METRES);
  const Vector2F direction = normalized(
      {forward, touchline * randomFloat(0.0f, 1.0f) *
                    MatchTuning::Defending::CLEARANCE_TOUCHLINE_BIAS * 2.0f});
  const Vector2F reach =
      toPitch({direction.x * clearanceMetres, direction.y * clearanceMetres});
  const Vector2F target{defender.position.x + reach.x,
                        defender.position.y + reach.y};
  const float speed =
      MatchTuning::Defending::CLEARANCE_SPEED * randomFloat(0.85f, 1.0f);
  ++statsOf(defender).clearances;
  clearFlightState();
  launchBall(defender, defender.position, target, speed,
             loftVerticalSpeed(clearanceMetres, speed, 0.0f, 0.0f), 0.0f);
  lastShooter = nullptr;
  defender.actionCooldown =
      randomFloat(MatchTuning::Passing::MIN_ACTION_COOLDOWN,
                  MatchTuning::Passing::MAX_ACTION_COOLDOWN);
}

void MatchEngine::headBall(MatchPlayer& header)
{
  trackTouch(header);
  const float attackDirection = header.isHomeTeam ? 1.0f : -1.0f;
  const float depth =
      header.isHomeTeam ? header.position.x : 1.0f - header.position.x;
  const bool fromTeammate = ball.isPass && ball.passByHome == header.isHomeTeam;
  const MatchPlayer* deliverer = findMatchPlayer(ball.lastPossessor);
  if (fromTeammate && ball.passWasOffside &&
      ball.intendedReceiver == header.player)
  {
    ++(header.isHomeTeam ? stats.homeOffsides : stats.awayOffsides);
    MatchEvent& event = logEvent(MatchEventType::OFFSIDE, header);
    event.position = header.position;
    setupFreeKick(!header.isHomeTeam, header.position);
    return;
  }

  // A header at goal needs a real chance; from further out the ball is
  // nodded down for a team-mate instead.
  if (fromTeammate && depth >= MatchTuning::Aerial::HEADER_SHOT_MIN_DEPTH &&
      std::abs(header.position.y - MatchTuning::Pitch::CENTRE) <=
          MatchTuning::Aerial::HEADER_SHOT_WIDTH &&
      estimateShotXG(header) * MatchTuning::Shooting::HEADER_XG_FACTOR >=
          MatchTuning::Aerial::HEADER_SHOT_MIN_XG)
  {
    if (deliverer && deliverer->isHomeTeam == header.isHomeTeam &&
        deliverer != &header)
    {
      ++statsOf(*deliverer).passesCompleted;
      trackPassCompletion(*deliverer, header);
      if (header.isHomeTeam)
        ++stats.homePassesCompleted;
      else
        ++stats.awayPassesCompleted;
      shotAssistCandidate = deliverer->player;
    }
    else
    {
      shotAssistCandidate = nullptr;
    }
    ball.position = header.position;
    takeShot(header, -1.0f, true);
    return;
  }

  if (fromTeammate && depth < MatchTuning::Aerial::HEADER_KNOCK_DOWN_MIN_DEPTH)
  {
    // A team-mate's lofted ball away from goal is cushioned down and kept.
    recordPassCompletion(header);
    setPossession(header);
    header.isTrapping = true;
    header.trapTimer =
        MatchTuning::Ball::BASE_TRAP_TIME +
        (1.0f - header.dribbling) * MatchTuning::Ball::TRAP_SKILL_PENALTY;
    header.actionCooldown =
        header.trapTimer + randomFloat(MatchTuning::Ball::MIN_POST_TOUCH_DELAY,
                                       MatchTuning::Ball::MAX_POST_TOUCH_DELAY);
    return;
  }
  if (fromTeammate)
    recordPassCompletion(header);
  else if (ball.isPass)
    ++statsOf(header).interceptions;
  const float ownDepth = 1.0f - depth;
  clearFlightState();
  ball.lastPossessor = header.player;
  lastShooter = nullptr;
  ball.position = header.position;
  if (!fromTeammate && tryOwnGoalTouch(header)) return;
  if (!fromTeammate &&
      ownDepth > 1.0f - MatchTuning::SetPiece::CROSS_CLEARANCE_DEPTH &&
      randomFloat(0.0f, 1.0f) < MatchTuning::SetPiece::HEADER_BEHIND_CHANCE)
  {
    ++statsOf(header).clearances;
    clearBehind(header);
  }
  else if (!fromTeammate && ownDepth > 0.55f)
  {
    // Defensive header: clear the danger, upfield and away from the middle.
    ++statsOf(header).clearances;
    const float wide =
        header.position.y < MatchTuning::Pitch::CENTRE ? -1.0f : 1.0f;
    const Vector2F direction =
        normalized({attackDirection * randomFloat(0.5f, 1.0f),
                    wide * randomFloat(0.1f, 0.8f)});
    // Many defensive headers are half-clearances that drop around the edge
    // of the box, where the second ball is fought for.
    const float speed =
        MatchTuning::Aerial::HEADER_CLEARANCE_SPEED *
        randomFloat(MatchTuning::Aerial::MIN_CLEARANCE_SHARE, 1.0f);
    const float height = ball.z;
    const Vector2F reach = toPitch({direction.x, direction.y});
    launchBall(header, header.position,
               {header.position.x + reach.x, header.position.y + reach.y},
               speed, MatchTuning::Aerial::HEADER_LIFT, 0.0f);
    ball.z = height;
  }
  else
  {
    // Knock-down or flick-on toward the nearest team-mate who is not
    // behind the header (never back toward the own goal).
    MatchPlayer* target = nullptr;
    float best = std::numeric_limits<float>::max();
    for (auto& other : players)
    {
      if (!active(other) || &other == &header ||
          other.isHomeTeam != header.isHomeTeam || other.isGoalkeeper)
        continue;
      if ((other.position.x - header.position.x) * attackDirection *
              MatchTuning::Pitch::LENGTH_METRES <
          -MatchTuning::Aerial::KNOCK_DOWN_MAX_BACKWARD_METRES)
        continue;
      const float d = distance(other.position, header.position);
      if (d < best)
      {
        best = d;
        target = &other;
      }
    }
    const Vector2F aim =
        target ? target->position
               : Vector2F{header.position.x + attackDirection * 0.1f,
                          header.position.y};
    const Vector2F knockDown{aim.x + randomFloat(-0.03f, 0.03f),
                             aim.y + randomFloat(-0.03f, 0.03f)};
    const float height = ball.z;
    launchBall(header, header.position, knockDown,
               MatchTuning::Aerial::HEADER_PASS_SPEED,
               MatchTuning::Aerial::HEADER_LIFT * 0.5f, 0.0f);
    ball.z = height;
  }
}

void MatchEngine::setPossession(MatchPlayer& player)
{
  // Central ownership transition: clear the old flight bookkeeping and update
  // the team transition context as well as the ball pointer. New reception or
  // interception code should use this path so phases and statistics agree.
  trackTouch(player);
  // A set piece's phase ends once the defending side has the ball.
  if (player.isHomeTeam != setPieceHome) setPiecePhaseRemaining = 0.0f;
  if (state == MatchState::PLAYING && lastControlledTeamHome &&
      *lastControlledTeamHome != player.isHomeTeam)
  {
    transitionSecondsRemaining =
        MatchTuning::Timing::POSSESSION_TRANSITION_SECONDS;
    lastCompletedPasser = nullptr;
    lastCompletedReceiver = nullptr;
  }
  lastControlledTeamHome = player.isHomeTeam;
  lastShooter = nullptr;
  if (ball.possessedBy != player.player)
  {
    possessionStartSeconds = getSimulatedSeconds();
    ++statsOf(player).touches;
  }
  ball.possessedBy = player.player;
  ball.lastPossessor = player.player;
  ball.position = player.position;
  ball.z = 0.0f;
  ball.velocity = {0.0f, 0.0f};
  ball.velocityZ = 0.0f;
  clearFlightState();
  // The first touch brings the ball under close control in front of him.
  ball.dribbleExposure = 0.0f;
  ball.dribbleTouchLength = 0.0f;
  ball.touchTimer = 0.0f;
  const Vector2F facing{std::cos(player.facingAngle) * PITCH_LENGTH,
                        std::sin(player.facingAngle) * PITCH_WIDTH};
  ball.dribbleDirection = normalized(facing);
  if (length(ball.dribbleDirection) <= EPSILON)
    ball.dribbleDirection = {player.isHomeTeam ? 1.0f : -1.0f, 0.0f};
  updateTeamPhases();
}

void MatchEngine::clearFlightState()
{
  ball.intendedReceiver = nullptr;
  ball.isPass = false;
  ball.passWasOffside = false;
  ball.isShot = false;
  ball.shotOnTarget = false;
  ball.shotXG = 0.0f;
  ball.shotIsHeader = false;
  ball.shotIsPenalty = false;
  ball.shotFromSetPiece = false;
  ball.shotSaveResolved = false;
  ball.shotElapsedSeconds = 0.0f;
  ball.isAerialDelivery = false;
  ball.fromThrowIn = false;
  ball.curve = 0.0f;
}

void MatchEngine::checkOutOfBounds()
{
  if (ball.position.x <= 0.0f || ball.position.x >= 1.0f)
  {
    const bool rightGoalLine = ball.position.x >= 1.0f;
    const float lineX = rightGoalLine ? 1.0f : 0.0f;
    const float span = ball.position.x - substepBallPosition.x;
    const float t =
        std::abs(span) > EPSILON
            ? std::clamp((lineX - substepBallPosition.x) / span, 0.0f, 1.0f)
            : 1.0f;
    const float crossingY =
        substepBallPosition.y + (ball.position.y - substepBallPosition.y) * t;
    const float crossingHeight = (substepBallZ + (ball.z - substepBallZ) * t) *
                                 MatchTuning::Units::BALL_Z_METRES;
    const float lateralMetres =
        std::abs(crossingY - MatchTuning::Pitch::CENTRE) *
        MatchTuning::Pitch::WIDTH_METRES;
    constexpr float RADIUS = MatchTuning::Units::BALL_RADIUS_METRES;
    constexpr float HALF_GOAL = MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES;
    constexpr float BAR = MatchTuning::Units::CROSSBAR_HEIGHT_METRES;
    if (lateralMetres < HALF_GOAL - RADIUS && crossingHeight < BAR - RADIUS &&
        !ball.fromThrowIn)
    {
      // Any ball that crosses the line inside the frame is a goal.
      ball.position.y = crossingY;
      scoreGoal(rightGoalLine);
      return;
    }
    const bool hitsPost =
        std::abs(lateralMetres - HALF_GOAL) <
            RADIUS + MatchTuning::Shooting::WOODWORK_BAND_METRES &&
        crossingHeight < BAR + RADIUS;
    const bool hitsBar =
        lateralMetres < HALF_GOAL + RADIUS &&
        std::abs(crossingHeight - BAR) <
            RADIUS + MatchTuning::Shooting::WOODWORK_BAND_METRES;
    if (ball.isShot && (hitsPost || hitsBar))
    {
      MatchPlayer* shooter = findMatchPlayer(lastShooter);
      MatchEvent& event = logEvent(MatchEventType::WOODWORK);
      if (shooter)
      {
        event.hasTeam = true;
        event.isHomeTeam = shooter->isHomeTeam;
        event.primaryPlayerId = shooter->player->getId();
      }
      ball.isShot = false;
      ball.shotOnTarget = false;
      ball.position.x = rightGoalLine ? 1.0f - 0.004f : 0.004f;
      ball.position.y = crossingY;
      ball.velocity = {
          -ball.velocity.x * MatchTuning::Shooting::WOODWORK_REBOUND,
          ball.velocity.y + randomFloat(-3.0f, 3.0f)};
      ball.touchAttempts = 0;
      ball.velocityZ = std::abs(ball.velocityZ) * 0.3f;
      return;
    }

    const bool lastTouchHome = isHomePlayer(ball.lastPossessor);
    if (!rightGoalLine)
    {
      // Home defends the left goal.
      if (lastTouchHome)
        setupCorner(false, ball.position.y < MatchTuning::Pitch::CENTRE);
      else
        setupGoalKick(true);
    }
    else
    {
      // Away defends the right goal.
      if (!lastTouchHome && ball.lastPossessor)
        setupCorner(true, ball.position.y < MatchTuning::Pitch::CENTRE);
      else
        setupGoalKick(false);
    }
    return;
  }

  if (ball.position.y <= 0.0f || ball.position.y >= 1.0f)
  {
    const bool receivingHome = !isHomePlayer(ball.lastPossessor);
    setupThrowIn(receivingHome);
  }
}

void MatchEngine::scoreGoal(bool homeTeam)
{
  MatchPlayer* last = findMatchPlayer(ball.lastPossessor);
  MatchPlayer* shooter = findMatchPlayer(lastShooter);
  MatchPlayer* scorer = last;
  // A shot parried or deflected in stays the shooter's goal; only a
  // defender's own deliberate touch makes it an own goal.
  if (shooter && shooter->isHomeTeam == homeTeam &&
      (!last || last->isHomeTeam != homeTeam))
  {
    scorer = shooter;
  }
  const bool ownGoal = scorer && scorer->isHomeTeam != homeTeam;

  if (homeTeam)
    ++homeScore;
  else
    ++awayScore;
  slidersStale = true;
  ++stoppageLogs[static_cast<std::size_t>(period - 1)].goals;

  // Set-piece totals include own goals caused by a restart. Header and
  // penalty conversion totals still describe the attacking player's shot.
  if (ball.shotFromSetPiece)
  {
    ++(homeTeam ? stats.homeSetPieceGoals : stats.awaySetPieceGoals);
  }
  if (ball.isShot && scorer && !ownGoal)
  {
    if (ball.shotIsHeader)
      ++(homeTeam ? stats.homeHeadedGoals : stats.awayHeadedGoals);
    if (ball.shotIsPenalty)
      ++(homeTeam ? stats.homePenaltyGoals : stats.awayPenaltyGoals);
  }
  if (scorer && !ownGoal)
  {
    // Every goal is an attempt on target; a knock-down or a ball bundled in
    // without a recorded shot is logged as one here.
    PlayerMatchStats& entry = statsOf(*scorer);
    if (scorer != shooter)
    {
      ++entry.shots;
      ++(homeTeam ? stats.homeShots : stats.awayShots);
    }
    ++entry.shotsOnTarget;
    ++(homeTeam ? stats.homeOnTarget : stats.awayOnTarget);
  }
  PlayerID assisterId = 0;
  if (scorer && !ownGoal)
  {
    ++statsOf(*scorer).goals;
    if (MatchPlayer* assister = findMatchPlayer(shotAssistCandidate);
        assister && assister != scorer && assister->isHomeTeam == homeTeam)
    {
      ++statsOf(*assister).assists;
      assisterId = assister->player->getId();
    }
  }
  else if (scorer)
  {
    ++statsOf(*scorer).ownGoals;
  }
  for (const auto& player : players)
  {
    if (active(player) && player.isHomeTeam != homeTeam)
      ++playerStats[player.statsIndex].goalsConceded;
  }

  MatchEvent& event =
      logEvent(ownGoal ? MatchEventType::OWN_GOAL : MatchEventType::GOAL);
  // The team fields describe the primary player's side, so an own goal is
  // reported for the defender's team (it counts for the other side).
  event.hasTeam = true;
  event.isHomeTeam = scorer ? scorer->isHomeTeam : homeTeam;
  event.primaryPlayerId = scorer ? scorer->player->getId() : 0;
  event.secondaryPlayerId = assisterId;
  event.position = scorer ? scorer->position : ball.position;
  event.xg = ball.isShot ? ball.shotXG : 0.0f;

  goalScoredByHome = homeTeam;
  goalCelebrationRemaining = getGoalCelebrationDuration();
  beginStoppage();
  state = MatchState::GOAL;
  ball.possessedBy = nullptr;
  ball.shotByHome = homeTeam;
  pendingAdvantage.active = false;
  restartTakerIndex.reset();
  restartIsSetPiece = false;
  // Everyone walks back to the kick-off formation during the celebration.
  for (auto& player : players)
  {
    if (active(player)) player.movementTarget = kickOffPosition(player);
  }
  updateTeamPhases();
}

void MatchEngine::updateBallInNet(float dt)
{
  integrateBall(dt);
  const float netDepth = MatchTuning::Ball::GOAL_NET_BALL_DEPTH;
  if (ball.shotByHome)
  {
    ball.position.x = std::clamp(ball.position.x, 1.0f, 1.0f + netDepth);
  }
  else
  {
    ball.position.x = std::clamp(ball.position.x, -netDepth, 0.0f);
  }
  ball.position.y = std::clamp(ball.position.y, MatchTuning::Pitch::GOAL_TOP,
                               MatchTuning::Pitch::GOAL_BOTTOM);
  // The ball settles into the net instead of bouncing out of the goal mouth.
  ball.velocityZ = 0.0f;
  ball.z = 0.0f;
  ball.curve = 0.0f;
}

void MatchEngine::makeSave(MatchPlayer& goalkeeper)
{
  MatchPlayer* shooter = findMatchPlayer(lastShooter);
  if (goalkeeper.isHomeTeam)
  {
    ++stats.homeSaves;
    ++stats.awayOnTarget;
  }
  else
  {
    ++stats.awaySaves;
    ++stats.homeOnTarget;
  }
  ++statsOf(goalkeeper).saves;
  if (shooter) ++statsOf(*shooter).shotsOnTarget;
  const PlayerID shooterId = lastShooter ? lastShooter->getId() : 0;
  goalkeeper.isDiving = true;
  goalkeeper.diveTimer = MatchTuning::Ball::SAVE_DIVE_TIME;
  setPossession(goalkeeper);
  goalkeeper.actionCooldown =
      randomFloat(MatchTuning::Ball::MIN_KEEPER_HOLD_SECONDS,
                  MatchTuning::Ball::MAX_KEEPER_HOLD_SECONDS);
  state = MatchState::PLAYING;
  keepers[goalkeeper.isHomeTeam ? 0 : 1].state = GoalkeeperState::HOLD;
  updateTeamPhases();
  MatchEvent& event = logEvent(MatchEventType::SAVE, goalkeeper);
  event.secondaryPlayerId = shooterId;
}

void MatchEngine::extendRestart(float seconds)
{
  if (state != MatchState::PLAYING && state != MatchState::GOAL &&
      state != MatchState::HALF_TIME && state != MatchState::FULL_TIME)
  {
    setPieceTimer += seconds;
  }
}

void MatchEngine::beginStoppage()
{
  // Play has stopped: a pending advantage lapses. Several stoppage causes in
  // the same step (a red card and its free kick) form one stoppage, so they
  // share one substitution window.
  pendingAdvantage.active = false;
  setPiecePhaseRemaining = 0.0f;
  if (stoppageSequence != 0 && lastStoppageStep == stepCounter) return;
  lastStoppageStep = stepCounter;
  ++stoppageSequence;
}

void MatchEngine::setupKickOff(bool homeKickingOff)
{
  // The goal already opened this break in play. Changes during its
  // celebration and the following kick-off share one substitution window.
  if (state != MatchState::GOAL)
  {
    beginStoppage();
  }
  restartIsSetPiece = false;
  pendingAdvantage.active = false;
  resetPositions();
  // At kick-off every player must be in their own half. The normal tactical
  // positions deliberately span more of the pitch and are restored through
  // regular movement once play starts.
  for (auto& player : players)
  {
    if (!active(player)) continue;
    player.position = kickOffPosition(player);
    player.movementTarget = player.position;
  }
  ball = MatchBall{};
  lastShooter = nullptr;
  const Vector2F centre{MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE};
  MatchPlayer* taker = findClosestPlayer(centre, homeKickingOff, false);
  if (!taker) taker = findClosestPlayer(centre, homeKickingOff, true);
  setRestartTaker(taker);
  if (taker)
  {
    taker->position = {homeKickingOff ? MatchTuning::Pitch::HOME_KICKOFF_X
                                      : MatchTuning::Pitch::AWAY_KICKOFF_X,
                       MatchTuning::Pitch::CENTRE};
    taker->movementTarget = taker->position;
    ball.possessedBy = taker->player;
    ball.lastPossessor = taker->player;
  }
  state = MatchState::KICK_OFF;
  setPieceTimer = MatchTuning::Timing::KICKOFF_DELAY_SECONDS;
  updateTeamPhases();
}

Vector2F MatchEngine::kickOffPosition(const MatchPlayer& player) const
{
  const float x = player.isHomeTeam
                      ? MatchTuning::Pitch::KICKOFF_FORMATION_INSET +
                            player.basePosition.x *
                                MatchTuning::Pitch::KICKOFF_FORMATION_SCALE
                      : (1.0f - MatchTuning::Pitch::KICKOFF_FORMATION_INSET) -
                            (1.0f - player.basePosition.x) *
                                MatchTuning::Pitch::KICKOFF_FORMATION_SCALE;
  return {x, player.basePosition.y};
}

void MatchEngine::setupThrowIn(bool homeTeam)
{
  beginStoppage();
  restartIsSetPiece = false;
  ball.position.x = std::clamp(
      ball.position.x, MatchTuning::Pitch::RESTART_LONGITUDINAL_MARGIN,
      1.0f - MatchTuning::Pitch::RESTART_LONGITUDINAL_MARGIN);
  ball.position.y = ball.position.y < MatchTuning::Pitch::CENTRE
                        ? MatchTuning::Pitch::RESTART_INSET
                        : 1.0f - MatchTuning::Pitch::RESTART_INSET;
  ball.z = 0.0f;
  ball.velocityZ = 0.0f;
  // A designated long-throw specialist takes the throws in the attacking
  // third; elsewhere (and without one) the nearest player does.
  const float attackingDepth =
      homeTeam ? ball.position.x : 1.0f - ball.position.x;
  MatchPlayer* taker =
      attackingDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START
          ? dutyTaker(homeTeam, SetPieceDuty::LongThrows)
          : nullptr;
  if (!taker) taker = findClosestPlayer(ball.position, homeTeam, false);
  setRestartTaker(taker);
  // The taker walks to the touchline spot during the stoppage.
  if (taker) taker->movementTarget = ball.position;
  ball.velocity = {0.0f, 0.0f};
  ball.possessedBy = taker ? taker->player : nullptr;
  ball.lastPossessor = ball.possessedBy;
  clearFlightState();
  state = MatchState::THROW_IN;
  setPieceTimer = MatchTuning::Timing::THROW_IN_DELAY_SECONDS;
  updateTeamPhases();
  MatchEvent& event = logEvent(MatchEventType::THROW_IN);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = ball.position;
  if (taker && taker->player) event.primaryPlayerId = taker->player->getId();
}

void MatchEngine::setupGoalKick(bool homeTeam)
{
  beginStoppage();
  restartIsSetPiece = false;
  ball.position = {homeTeam ? MatchTuning::Pitch::LEFT_GOAL_KICK_X
                            : MatchTuning::Pitch::RIGHT_GOAL_KICK_X,
                   MatchTuning::Pitch::CENTRE};
  ball.z = 0.0f;
  ball.velocityZ = 0.0f;
  MatchPlayer* taker = findGoalkeeper(homeTeam);
  if (!taker) taker = findClosestPlayer(ball.position, homeTeam, true);
  setRestartTaker(taker);
  if (taker) taker->movementTarget = ball.position;
  ball.velocity = {0.0f, 0.0f};
  ball.possessedBy = taker ? taker->player : nullptr;
  ball.lastPossessor = ball.possessedBy;
  clearFlightState();
  state = MatchState::GOAL_KICK;
  setPieceTimer = MatchTuning::Timing::GOAL_KICK_DELAY_SECONDS;
  updateTeamPhases();
  MatchEvent& event = logEvent(MatchEventType::GOAL_KICK);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = ball.position;
}

void MatchEngine::setupCorner(bool homeTeam, bool topCorner)
{
  beginStoppage();
  restartIsSetPiece = true;
  ball.position = {homeTeam ? 1.0f - MatchTuning::Pitch::RESTART_INSET
                            : MatchTuning::Pitch::RESTART_INSET,
                   topCorner ? MatchTuning::Pitch::RESTART_INSET
                             : 1.0f - MatchTuning::Pitch::RESTART_INSET};
  ball.z = 0.0f;
  ball.velocityZ = 0.0f;
  // Left and right follow the formation: the corner on the side of the
  // team's left-sided positions (low y) is its left corner.
  MatchPlayer* taker =
      dutyTaker(homeTeam, topCorner ? SetPieceDuty::CornersLeft
                                    : SetPieceDuty::CornersRight);
  if (!taker) taker = findClosestPlayer(ball.position, homeTeam, false);
  setRestartTaker(taker);
  if (taker) placeTaker(*taker, ball.position);
  ball.velocity = {0.0f, 0.0f};
  ball.possessedBy = taker ? taker->player : nullptr;
  ball.lastPossessor = ball.possessedBy;
  clearFlightState();
  arrangeSetPiece(homeTeam, ball.position);
  state = MatchState::CORNER_KICK;
  setPieceTimer = MatchTuning::Timing::CORNER_DELAY_SECONDS;
  updateTeamPhases();
  if (homeTeam)
    ++stats.homeCorners;
  else
    ++stats.awayCorners;
  MatchEvent& event = logEvent(MatchEventType::CORNER);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = ball.position;
  if (taker && taker->player) event.primaryPlayerId = taker->player->getId();
}

void MatchEngine::setupFreeKick(bool homeTeam, Vector2F foulPos)
{
  beginStoppage();
  pendingAdvantage.active = false;
  ball.position = {
      std::clamp(foulPos.x, MatchTuning::Pitch::RESTART_LONGITUDINAL_MARGIN,
                 1.0f - MatchTuning::Pitch::RESTART_LONGITUDINAL_MARGIN),
      std::clamp(foulPos.y, MatchTuning::Pitch::RESTART_LONGITUDINAL_MARGIN,
                 1.0f - MatchTuning::Pitch::RESTART_LONGITUDINAL_MARGIN)};
  ball.z = 0.0f;
  ball.velocityZ = 0.0f;
  const Vector2F goal{homeTeam ? 1.0f : 0.0f, MatchTuning::Pitch::CENTRE};
  const float goalMetres = distance(ball.position, goal);
  // Free kicks in the attacking third are set pieces taken by the specialist.
  restartIsSetPiece =
      goalMetres <= MatchTuning::SetPiece::CROSSING_FREE_KICK_METRES;
  MatchPlayer* taker = restartIsSetPiece
                           ? dutyTaker(homeTeam, SetPieceDuty::FreeKicks)
                           : nullptr;
  if (!taker) taker = findClosestPlayer(ball.position, homeTeam, false);
  setRestartTaker(taker);
  if (taker && restartIsSetPiece)
    placeTaker(*taker, ball.position);
  else if (taker)
    taker->movementTarget = ball.position;
  ball.velocity = {0.0f, 0.0f};
  ball.possessedBy = taker ? taker->player : nullptr;
  ball.lastPossessor = ball.possessedBy;
  clearFlightState();
  if (restartIsSetPiece) arrangeSetPiece(homeTeam, ball.position);
  state = MatchState::FREE_KICK;
  setPieceTimer = restartIsSetPiece
                      ? MatchTuning::Timing::SET_PIECE_FREE_KICK_DELAY_SECONDS
                      : MatchTuning::Timing::FREE_KICK_DELAY_SECONDS;
  updateTeamPhases();
  MatchEvent& event = logEvent(MatchEventType::FREE_KICK);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = ball.position;
  if (taker && taker->player) event.primaryPlayerId = taker->player->getId();
}

void MatchEngine::setupPenalty(bool homeTeam)
{
  beginStoppage();
  restartIsSetPiece = true;
  pendingAdvantage.active = false;
  const Vector2F spot{homeTeam ? MatchTuning::Pitch::RIGHT_PENALTY_SPOT_X
                               : MatchTuning::Pitch::LEFT_PENALTY_SPOT_X,
                      MatchTuning::Pitch::CENTRE};
  MatchPlayer* taker = dutyTaker(homeTeam, SetPieceDuty::Penalties);
  setRestartTaker(taker);
  ball.position = spot;
  ball.z = 0.0f;
  ball.velocityZ = 0.0f;
  ball.velocity = {0.0f, 0.0f};
  ball.possessedBy = taker ? taker->player : nullptr;
  ball.lastPossessor = ball.possessedBy;
  if (taker) placeTaker(*taker, spot);
  // Everyone else waits outside the area; the keeper stands on his line.
  const float edge = homeTeam ? MatchTuning::Pitch::RIGHT_PENALTY_AREA_EDGE -
                                    MatchTuning::SetPiece::PENALTY_WAIT_OFFSET
                              : MatchTuning::Pitch::LEFT_PENALTY_AREA_EDGE +
                                    MatchTuning::SetPiece::PENALTY_WAIT_OFFSET;
  for (auto& player : players)
  {
    if (!active(player) || &player == taker) continue;
    if (player.isGoalkeeper && player.isHomeTeam != homeTeam)
    {
      // Law 14: the defending keeper stays on his goal line.
      player.movementTarget = {homeTeam ? MatchTuning::Pitch::PLAYER_MAX_X
                                        : MatchTuning::Pitch::PLAYER_MIN_X,
                               MatchTuning::Pitch::CENTRE};
      player.position = player.movementTarget;
      continue;
    }
    if (player.isGoalkeeper) continue;
    const bool inside =
        homeTeam ? player.position.x > edge : player.position.x < edge;
    if (inside) player.movementTarget.x = edge;
  }
  clearFlightState();
  if (homeTeam)
    ++stats.homePenalties;
  else
    ++stats.awayPenalties;
  ++stoppageLogs[static_cast<std::size_t>(period - 1)].penalties;
  state = MatchState::PENALTY;
  setPieceTimer = MatchTuning::Timing::PENALTY_DELAY_SECONDS;
  updateTeamPhases();
  MatchEvent& event = logEvent(MatchEventType::PENALTY);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = spot;
  if (taker && taker->player) event.primaryPlayerId = taker->player->getId();
}

void MatchEngine::placeTaker(MatchPlayer& taker, Vector2F spot)
{
  taker.position = {std::clamp(spot.x, MatchTuning::Pitch::PLAYER_MIN_X,
                               MatchTuning::Pitch::PLAYER_MAX_X),
                    std::clamp(spot.y, MatchTuning::Pitch::PLAYER_MIN_Y,
                               MatchTuning::Pitch::PLAYER_MAX_Y)};
  taker.movementTarget = taker.position;
  taker.velocity = {0.0f, 0.0f};
  // Anyone standing on the spot steps aside for the taker.
  for (auto& other : players)
  {
    if (!active(other) || &other == &taker) continue;
    const float dx = (other.position.x - taker.position.x) *
                     MatchTuning::Pitch::LENGTH_METRES;
    const float dy = (other.position.y - taker.position.y) *
                     MatchTuning::Pitch::WIDTH_METRES;
    const float gap = std::sqrt(dx * dx + dy * dy);
    if (gap >= MatchTuning::Player::MINIMUM_BODY_SEPARATION_METRES) continue;
    const float towardCentre =
        taker.position.y < MatchTuning::Pitch::CENTRE ? 1.0f : -1.0f;
    const Vector2F away = gap > EPSILON ? Vector2F{dx / gap, dy / gap}
                                        : Vector2F{0.0f, towardCentre};
    const float push =
        MatchTuning::Player::MINIMUM_BODY_SEPARATION_METRES - gap;
    other.position.x = std::clamp(
        other.position.x + away.x * push / MatchTuning::Pitch::LENGTH_METRES,
        MatchTuning::Pitch::PLAYER_MIN_X, MatchTuning::Pitch::PLAYER_MAX_X);
    other.position.y = std::clamp(
        other.position.y + away.y * push / MatchTuning::Pitch::WIDTH_METRES,
        MatchTuning::Pitch::PLAYER_MIN_Y, MatchTuning::Pitch::PLAYER_MAX_Y);
  }
}

MatchPlayer* MatchEngine::restartTaker()
{
  return restartTakerIndex && *restartTakerIndex < players.size()
             ? &players[*restartTakerIndex]
             : nullptr;
}

void MatchEngine::setRestartTaker(MatchPlayer* taker)
{
  if (taker)
    restartTakerIndex = slotOf(*taker);
  else
    restartTakerIndex.reset();
}

std::size_t MatchEngine::slotOf(const MatchPlayer& player) const
{
  return static_cast<std::size_t>(&player - players.data());
}

MatchPlayer* MatchEngine::bestSetPieceTaker(bool homeTeam, bool shooting)
{
  MatchPlayer* best = nullptr;
  float bestScore = -std::numeric_limits<float>::infinity();
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam ||
        player.isGoalkeeper || player.isInjured)
      continue;
    const float score = shooting
                            ? player.shooting * 0.75f + player.passing * 0.15f +
                                  player.vision * 0.10f
                            : player.passing * 0.6f + player.vision * 0.4f;
    if (score > bestScore)
    {
      bestScore = score;
      best = &player;
    }
  }
  return best;
}

MatchPlayer* MatchEngine::dutyTaker(bool homeTeam, SetPieceDuty duty)
{
  const PlayerID designated =
      designations[homeTeam ? 0 : 1][static_cast<std::size_t>(duty)];
  if (designated != PlayerID{})
  {
    for (auto& player : players)
    {
      if (active(player) && player.isHomeTeam == homeTeam &&
          !player.isGoalkeeper && !player.isInjured &&
          player.player->getId() == designated)
        return &player;
    }
  }
  switch (duty)
  {
    case SetPieceDuty::Penalties:
    case SetPieceDuty::FreeKicks:
      return bestSetPieceTaker(homeTeam, true);
    case SetPieceDuty::CornersLeft:
    case SetPieceDuty::CornersRight:
      return bestSetPieceTaker(homeTeam, false);
    default:
      return nullptr;
  }
}

void MatchEngine::arrangeSetPiece(bool attackingHome, Vector2F ballPosition)
{
  // Box targets for the attacking side, in metres from the goal line and
  // from the centre of the goal (positive towards the ball's side).
  const float goalX = attackingHome ? 1.0f : 0.0f;
  const float inward = attackingHome ? -1.0f : 1.0f;
  const float ballSide =
      ballPosition.y < MatchTuning::Pitch::CENTRE ? -1.0f : 1.0f;
  const auto spot = [&](float depthMetres, float lateralMetres)
  {
    return Vector2F{
        goalX + inward * depthMetres / MatchTuning::Pitch::LENGTH_METRES,
        MatchTuning::Pitch::CENTRE +
            ballSide * lateralMetres / MatchTuning::Pitch::WIDTH_METRES};
  };
  const std::array<Vector2F, 5> attackSlots = {
      spot(5.5f, 3.0f), spot(6.5f, -3.5f), spot(10.5f, 0.0f), spot(8.0f, 1.0f),
      spot(17.5f, -2.0f)};

  // Rank attackers by aerial threat; the best go into the box.
  std::array<MatchPlayer*, 11> attackers{};
  std::size_t attackerCount = 0;
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != attackingHome ||
        player.isGoalkeeper || &player == restartTaker() ||
        attackerCount == attackers.size())
      continue;
    attackers[attackerCount++] = &player;
  }
  // Centre-backs and strikers attack the delivery; full-backs and a
  // holding midfielder form the rest defence.
  const auto boxThreat = [](const MatchPlayer* candidate)
  {
    float roleBias = 0.0f;
    switch (candidate->player->getRole())
    {
      case PlayerRole::ST:
        roleBias = 0.12f;
        break;
      case PlayerRole::CB:
        roleBias = 0.0f;
        break;
      case PlayerRole::LB:
      case PlayerRole::RB:
        roleBias = -0.3f;
        break;
      case PlayerRole::CDM:
        roleBias = -0.15f;
        break;
      default:
        break;
    }
    return MatchRules::headerReachMetres(candidate->heightMetres,
                                         candidate->physicality) +
           roleBias;
  };
  insertionSort(
      attackers, attackerCount,
      [&boxThreat](const MatchPlayer* first, const MatchPlayer* second)
      {
        const float a = boxThreat(first);
        const float b = boxThreat(second);
        if (a != b) return a > b;
        return first->player->getId() < second->player->getId();
      });
  const std::size_t boxAttackers =
      std::min(attackSlots.size(),
               attackerCount > MatchTuning::SetPiece::REST_DEFENDERS
                   ? attackerCount - MatchTuning::SetPiece::REST_DEFENDERS
                   : std::size_t{0});
  for (std::size_t index = 0; index < boxAttackers; ++index)
    attackers[index]->movementTarget = attackSlots[index];

  // Defenders: the keeper on his line, markers goal-side of each attacker in
  // the box, and the rest zonal at the near post and the edge of the area.
  std::array<MatchPlayer*, 11> defenders{};
  std::size_t defenderCount = 0;
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam == attackingHome ||
        defenderCount == defenders.size())
      continue;
    if (player.isGoalkeeper)
    {
      player.movementTarget = spot(0.8f, 0.0f);
      continue;
    }
    defenders[defenderCount++] = &player;
  }
  insertionSort(defenders, defenderCount,
                [](const MatchPlayer* first, const MatchPlayer* second)
                {
                  const float a = first->defending + first->physicality;
                  const float b = second->defending + second->physicality;
                  if (a != b) return a > b;
                  return first->player->getId() < second->player->getId();
                });
  std::size_t next = 0;
  for (std::size_t index = 0; index < boxAttackers && next < defenderCount;
       ++index, ++next)
  {
    const Vector2F mark = attackers[index]->movementTarget;
    defenders[next]->movementTarget = {
        mark.x - inward * MatchTuning::SetPiece::MARKING_GOAL_SIDE_OFFSET,
        mark.y};
  }
  const std::array<Vector2F, 3> zones = {spot(3.0f, 2.5f), spot(6.0f, 0.0f),
                                         spot(16.0f, 0.0f)};
  for (std::size_t zone = 0; zone < zones.size() && next < defenderCount;
       ++zone, ++next)
  {
    defenders[next]->movementTarget = zones[zone];
  }
}

bool MatchEngine::userRestartPassRequested() const
{
  if (playHalfMinutes == 0 || !controlledIndex.has_value() ||
      !restartTakerIndex.has_value())
    return false;
  switch (state)
  {
    case MatchState::KICK_OFF:
    case MatchState::THROW_IN:
    case MatchState::GOAL_KICK:
    case MatchState::CORNER_KICK:
    case MatchState::FREE_KICK:
      break;
    default:
      return false;
  }
  if (*restartTakerIndex >= players.size()) return false;
  if (players.at(*controlledIndex).isHomeTeam !=
      players.at(*restartTakerIndex).isHomeTeam)
    return false;
  return controlInput.action == MatchInputAction::PASS ||
         controlInput.action == MatchInputAction::LOFTED_PASS ||
         controlInput.action == MatchInputAction::THROUGH_BALL;
}

void MatchEngine::completeRestart()
{
  // Restart setup functions own placement and waiting; this function owns the
  // actual release. Preserve the restart state until the kick is executed:
  // passBall uses it for offside exemptions and set-piece classification.
  const MatchState restartState = state;
  MatchPlayer* taker = restartTaker();
  const bool userPass = userRestartPassRequested();
  restartTakerIndex.reset();
  if (!taker || !active(*taker))
  {
    ball.possessedBy = nullptr;
    ball.velocity = {0.0F, 0.0F};
    ball.velocityZ = 0.0F;
    clearFlightState();
    restartIsSetPiece = false;
    state = MatchState::PLAYING;
    updateTeamPhases();
    return;
  }

  // The taker has walked to the spot; line him up exactly on the ball.
  if (restartState != MatchState::KICK_OFF)
  {
    taker->position = ball.position;
    taker->velocity = {0.0f, 0.0f};
  }
  if (restartState == MatchState::PENALTY)
  {
    ball.position = taker->position;
    takeShot(*taker, MatchTuning::Shooting::PENALTY_XG);
    state = MatchState::PLAYING;
    restartIsSetPiece = false;
    updateTeamPhases();
    return;
  }

  // Corners, free kicks near goal and throw-ins in the attacking third open
  // a set-piece phase for the side taking them.
  const float restartDepth =
      taker->isHomeTeam ? ball.position.x : 1.0f - ball.position.x;
  if (restartIsSetPiece ||
      (restartState == MatchState::THROW_IN &&
       restartDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START))
  {
    setPiecePhaseRemaining = MatchTuning::SetPiece::SET_PIECE_PHASE_SECONDS;
    setPieceHome = taker->isHomeTeam;
  }
  if (userPass)
  {
    const MatchInputAction action = controlInput.action;
    playControlledPass(*taker, action);
    controlInput.action = MatchInputAction::NONE;
    controlActionRemaining = 0.0F;
    ++controlledActions[taker->isHomeTeam ? 0 : 1];
  }
  else
    takeSetPiece(*taker, restartState);
  if (state == restartState) state = MatchState::PLAYING;
  restartIsSetPiece = false;
  updateTeamPhases();
}

void MatchEngine::takeSetPiece(MatchPlayer& taker, MatchState restartState)
{
  if (restartState == MatchState::CORNER_KICK)
  {
    takeCorner(taker);
    return;
  }
  if (restartState == MatchState::FREE_KICK && restartIsSetPiece)
  {
    const Vector2F goal{taker.isHomeTeam ? 1.0f : 0.0f,
                        MatchTuning::Pitch::CENTRE};
    const float goalMetres = distance(ball.position, goal);
    const float lateralMetres =
        std::abs(ball.position.y - MatchTuning::Pitch::CENTRE) *
        MatchTuning::Pitch::WIDTH_METRES;
    const bool shootingRange =
        goalMetres <= MatchTuning::SetPiece::DIRECT_FREE_KICK_METRES &&
        lateralMetres <= MatchTuning::SetPiece::DIRECT_FREE_KICK_WIDTH_METRES;
    const float shootChance = std::clamp(
        MatchTuning::SetPiece::DIRECT_SHOT_BASE +
            taker.shooting * MatchTuning::SetPiece::DIRECT_SHOT_SKILL -
            goalMetres * MatchTuning::SetPiece::DIRECT_SHOT_DISTANCE_PENALTY,
        0.0f, 0.95f);
    if (shootingRange && randomFloat(0.0f, 1.0f) < shootChance)
    {
      takeDirectFreeKick(taker);
      return;
    }
    const bool wide =
        lateralMetres > MatchTuning::SetPiece::CROSS_MIN_WIDTH_METRES ||
        goalMetres > MatchTuning::SetPiece::DIRECT_FREE_KICK_METRES;
    if (wide)
    {
      takeCorner(taker);
      return;
    }
  }

  std::optional<PassOption> passOption = choosePassTarget(taker);
  if (!passOption && playHalfMinutes > 0)
  {
    // A restart must release the ball even when no lane meets the AI's
    // open-play safety threshold. Find the nearest teammate as an outlet.
    MatchPlayer* outlet = nullptr;
    float nearest = std::numeric_limits<float>::infinity();
    for (auto& candidate : players)
    {
      if (!active(candidate) || &candidate == &taker ||
          candidate.isHomeTeam != taker.isHomeTeam || candidate.isGoalkeeper)
        continue;
      const float gap = distance(taker.position, candidate.position);
      if (gap > EPSILON && gap < nearest)
      {
        outlet = &candidate;
        nearest = gap;
      }
    }
    if (outlet) passOption = evaluatePassOption(taker, *outlet);
  }
  if (passOption)
    passBall(taker, *passOption, restartState == MatchState::GOAL_KICK);
  else
    setPossession(taker);
}

void MatchEngine::takeCorner(MatchPlayer& taker)
{
  // Routine: short, near post, far post or penalty spot, delivered at head
  // height to the attacker assigned to that zone.
  MatchPlayer* target = nullptr;
  const float roll = randomFloat(0.0f, 1.0f);
  if (roll < MatchTuning::SetPiece::SHORT_CORNER_CHANCE)
  {
    std::optional<PassOption> shortOption = choosePassTarget(taker);
    if (shortOption &&
        shortOption->passDistance <
            MatchTuning::SetPiece::SHORT_CORNER_MAX_DISTANCE_METRES)
    {
      PassOption groundPass = *shortOption;
      groundPass.lofted = false;
      passBall(taker, groundPass);
      return;
    }
  }
  const Vector2F goal{taker.isHomeTeam ? 1.0f : 0.0f,
                      MatchTuning::Pitch::CENTRE};
  float bestScore = -std::numeric_limits<float>::infinity();
  const float nearPostShare = MatchTuning::SetPiece::NEAR_POST_CHANCE;
  const bool nearPost =
      roll < MatchTuning::SetPiece::SHORT_CORNER_CHANCE + nearPostShare;
  for (auto& candidate : players)
  {
    if (!active(candidate) || &candidate == &taker ||
        candidate.isHomeTeam != taker.isHomeTeam || candidate.isGoalkeeper)
      continue;
    if (!inPenaltyArea(candidate.movementTarget, !taker.isHomeTeam)) continue;
    const float targetMetres = distance(candidate.movementTarget, goal);
    const float sideMetres =
        (candidate.movementTarget.y - MatchTuning::Pitch::CENTRE) *
        (ball.position.y < MatchTuning::Pitch::CENTRE ? -1.0f : 1.0f) *
        MatchTuning::Pitch::WIDTH_METRES;
    const float threat = MatchRules::headerReachMetres(candidate.heightMetres,
                                                       candidate.physicality);
    const float zone = nearPost ? sideMetres : -sideMetres;
    const float score =
        threat * MatchTuning::SetPiece::TARGET_THREAT_WEIGHT + zone * 0.15f -
        targetMetres * 0.05f +
        randomFloat(0.0f, MatchTuning::SetPiece::TARGET_RANDOMNESS);
    if (score > bestScore)
    {
      bestScore = score;
      target = &candidate;
    }
  }
  if (!target)
  {
    std::optional<PassOption> fallback = choosePassTarget(taker);
    if (fallback)
      passBall(taker, *fallback, true);
    else
      setPossession(taker);
    return;
  }
  PassOption delivery = evaluatePassOption(taker, *target);
  delivery.targetPoint = target->movementTarget;
  delivery.passDistance = distance(taker.position, delivery.targetPoint);
  delivery.lofted = true;
  delivery.intent = PassIntent::CROSS;
  passBall(taker, delivery, true);
}

void MatchEngine::takeDirectFreeKick(MatchPlayer& taker)
{
  // The wall blocks a share of attempts; the rest are struck over it with
  // a little less precision, and the keeper's view is partly screened.
  const float wallChance =
      std::clamp(MatchTuning::SetPiece::WALL_BLOCK_BASE -
                     taker.shooting * MatchTuning::SetPiece::WALL_BLOCK_SKILL,
                 0.05f, 0.6f);
  if (randomFloat(0.0f, 1.0f) < wallChance)
  {
    takeShot(taker);
    const float outward = taker.isHomeTeam ? -1.0f : 1.0f;
    const float speed =
        length(ball.velocity) * MatchTuning::SetPiece::WALL_REBOUND_SPEED_SHARE;
    ball.touchAttempts = 0;
    MatchPlayer* wallPlayer = nullptr;
    float best = std::numeric_limits<float>::max();
    for (auto& player : players)
    {
      if (!active(player) || player.isHomeTeam == taker.isHomeTeam ||
          player.isGoalkeeper)
        continue;
      const float d = distance(player.position, ball.position);
      if (d < best)
      {
        best = d;
        wallPlayer = &player;
      }
    }
    ball.isShot = false;
    ball.shotOnTarget = false;
    ball.shotSaveResolved = true;
    ball.velocity = {outward * speed * randomFloat(0.3f, 1.0f),
                     speed * randomFloat(-0.8f, 0.8f)};
    ball.velocityZ = randomFloat(0.0f, MatchTuning::Aerial::HEADER_LIFT);
    if (wallPlayer)
    {
      ball.position = wallPlayer->position;
      ball.lastPossessor = wallPlayer->player;
      ball.kicker = wallPlayer->player;
      ball.kickerLockout = MatchTuning::Passing::KICKER_LOCKOUT_SECONDS;
      ++statsOf(*wallPlayer).clearances;
      logEvent(MatchEventType::SHOT_BLOCKED, *wallPlayer).detail =
          MatchEventDetail::WALL;
    }
    return;
  }
  takeShot(taker, MatchTuning::SetPiece::DIRECT_FREE_KICK_XG, false, true);
  if (MatchPlayer* keeper = findGoalkeeper(!taker.isHomeTeam))
  {
    keepers[keeper->isHomeTeam ? 0 : 1].reactionRemaining +=
        MatchTuning::SetPiece::WALL_SCREEN_SECONDS;
  }
}

void MatchEngine::resetPositions()
{
  for (auto& player : players)
  {
    if (!active(player)) continue;
    player.position = player.basePosition;
    player.movementTarget = player.basePosition;
    player.velocity = {0.0f, 0.0f};
    player.intent = PlayerIntent::HOLD_SHAPE;
    player.isPressing = false;
    player.isMakingRun = false;
    player.actionCooldown = 0.0f;
  }
}

MatchPlayer* MatchEngine::findClosestPlayer(Vector2F position, bool homeTeam,
                                            bool includeGoalkeeper)
{
  MatchPlayer* closest = nullptr;
  float bestDistance = std::numeric_limits<float>::max();
  for (auto& player : players)
  {
    if (player.isHomeTeam != homeTeam || !active(player) ||
        (!includeGoalkeeper && player.isGoalkeeper))
    {
      continue;
    }
    const float candidateDistance = distance(position, player.position);
    if (candidateDistance < bestDistance)
    {
      bestDistance = candidateDistance;
      closest = &player;
    }
  }
  return closest;
}

MatchPlayer* MatchEngine::findGoalkeeper(bool homeTeam)
{
  const auto goalkeeper =
      std::ranges::find_if(players,
                           [homeTeam](const MatchPlayer& player)
                           {
                             return player.isHomeTeam == homeTeam &&
                                    active(player) && player.isGoalkeeper;
                           });
  return goalkeeper == players.end() ? nullptr : &*goalkeeper;
}

MatchPlayer* MatchEngine::findMatchPlayer(const Player* player)
{
  if (!player) return nullptr;
  const auto found =
      std::ranges::find_if(players, [player](const MatchPlayer& matchPlayer)
                           { return matchPlayer.player == player; });
  return found == players.end() ? nullptr : &*found;
}

float MatchEngine::nearestOpponentDistance(const MatchPlayer& player) const
{
  float bestDistance = std::numeric_limits<float>::max();
  for (const auto& opponent : players)
  {
    if (opponent.isHomeTeam != player.isHomeTeam && active(opponent))
    {
      bestDistance =
          std::min(bestDistance, distance(player.position, opponent.position));
    }
  }
  return bestDistance;
}

float MatchEngine::openSpaceAhead(const MatchPlayer& carrier) const
{
  constexpr float OPEN_SPACE_RADIUS_METRES = 13.5f;
  const float direction = carrier.isHomeTeam ? 1.0f : -1.0f;
  const float probeDepth =
      std::clamp(carrier.position.x + direction * 0.16f, 0.02f, 0.98f);
  constexpr float CHANNELS[3] = {0.25f, 0.50f, 0.75f};
  float openness = 0.0f;
  for (const float channelY : CHANNELS)
  {
    const Vector2F probe{probeDepth, channelY};
    float nearest = std::numeric_limits<float>::max();
    for (const auto& opponent : players)
    {
      if (opponent.isHomeTeam == carrier.isHomeTeam || !active(opponent))
        continue;
      nearest = std::min(nearest, distance(probe, opponent.position));
    }
    openness += std::clamp(nearest / OPEN_SPACE_RADIUS_METRES, 0.0f, 1.0f);
  }
  return openness / 3.0f;
}

float MatchEngine::passingLaneRisk(const MatchPlayer& passer,
                                   const MatchPlayer& receiver) const
{
  const Vector2F segment = toMetres({receiver.position.x - passer.position.x,
                                     receiver.position.y - passer.position.y});
  const float segmentLengthSquared =
      segment.x * segment.x + segment.y * segment.y;
  if (segmentLengthSquared <= EPSILON) return 1.0f;

  float combinedSafety = 1.0f;
  for (const auto& opponent : players)
  {
    if (!active(opponent) || opponent.isHomeTeam == passer.isHomeTeam) continue;
    const Vector2F fromPasser =
        toMetres({opponent.position.x - passer.position.x,
                  opponent.position.y - passer.position.y});
    const float projection =
        std::clamp((fromPasser.x * segment.x + fromPasser.y * segment.y) /
                       segmentLengthSquared,
                   0.0f, 1.0f);
    if (projection < MatchTuning::Passing::LANE_START_MARGIN ||
        projection > MatchTuning::Passing::LANE_END_MARGIN)
      continue;

    const float laneDistance = length({fromPasser.x - segment.x * projection,
                                       fromPasser.y - segment.y * projection});
    const float interceptionRadius =
        MatchTuning::Passing::BASE_INTERCEPTION_RADIUS_METRES +
        opponent.defending *
            MatchTuning::Passing::DEFENDING_INTERCEPTION_METRES +
        opponent.pace * MatchTuning::Passing::PACE_INTERCEPTION_METRES;
    const float individualRisk =
        std::clamp(1.0f - laneDistance / std::max(interceptionRadius, EPSILON),
                   0.0f, 1.0f);
    combinedSafety *=
        1.0f -
        individualRisk * (MatchTuning::Passing::BASE_INTERCEPTION_RISK +
                          projection * MatchTuning::Passing::LATE_LANE_RISK);
  }
  return std::clamp(1.0f - combinedSafety, 0.0f, 1.0f);
}

float MatchEngine::estimateShotXG(const MatchPlayer& shooter) const
{
  // Logistic expected-goals model on distance, visible goal angle, distance
  // to the goal line and lateral offset, scaled by technique and pressure.
  using S = MatchTuning::Shooting;
  const float goalX = shooter.isHomeTeam ? 1.0f : 0.0f;
  const float lineMetres =
      std::abs(goalX - shooter.position.x) * MatchTuning::Pitch::LENGTH_METRES;
  const float lateralMetres =
      std::abs(MatchTuning::Pitch::CENTRE - shooter.position.y) *
      MatchTuning::Pitch::WIDTH_METRES;
  const float metres =
      std::sqrt(lineMetres * lineMetres + lateralMetres * lateralMetres);
  // The fitted surface is only valid within shooting range.
  if (metres > S::XG_MAX_DISTANCE_METRES) return S::MIN_OPEN_PLAY_XG;

  const Vector2F toTop =
      toMetres({goalX - shooter.position.x,
                MatchTuning::Pitch::GOAL_TOP - shooter.position.y});
  const Vector2F toBottom =
      toMetres({goalX - shooter.position.x,
                MatchTuning::Pitch::GOAL_BOTTOM - shooter.position.y});
  const float denominator = length(toTop) * length(toBottom);
  const float cosine =
      denominator > EPSILON
          ? std::clamp(
                (toTop.x * toBottom.x + toTop.y * toBottom.y) / denominator,
                -1.0f, 1.0f)
          : 1.0f;
  const float angle = std::acos(cosine);
  const float logit = S::XG_INTERCEPT + S::XG_ANGLE * angle +
                      S::XG_DISTANCE * metres + S::XG_LINE * lineMetres +
                      S::XG_LATERAL * lateralMetres +
                      S::XG_LINE_SQUARED * lineMetres * lineMetres +
                      S::XG_LATERAL_SQUARED * lateralMetres * lateralMetres +
                      S::XG_ANGLE_LINE * angle * lineMetres;
  const float location = 1.0f / (1.0f + std::exp(-logit));
  const float pressure = std::clamp(
      (S::PRESSURE_RADIUS_METRES - nearestOpponentDistance(shooter)) /
          S::PRESSURE_RADIUS_METRES,
      0.0f, 1.0f);
  return std::clamp(
      location *
          (S::BASE_SKILL_FACTOR + shooter.shooting * S::SHOOTING_SKILL_FACTOR) *
          (1.0f - pressure * S::PRESSURE_PENALTY),
      S::MIN_OPEN_PLAY_XG, S::MAX_OPEN_PLAY_XG);
}

float MatchEngine::offsideLine(bool attackingHome) const
{
  float nearestGoalDefender = attackingHome
                                  ? -std::numeric_limits<float>::infinity()
                                  : std::numeric_limits<float>::infinity();
  float secondNearestGoalDefender = nearestGoalDefender;
  std::size_t defenderCount = 0;
  for (const auto& player : players)
  {
    if (player.isHomeTeam == attackingHome || !active(player)) continue;
    ++defenderCount;
    const float position = player.position.x;
    if (attackingHome)
    {
      if (position >= nearestGoalDefender)
      {
        secondNearestGoalDefender = nearestGoalDefender;
        nearestGoalDefender = position;
      }
      else if (position > secondNearestGoalDefender)
      {
        secondNearestGoalDefender = position;
      }
    }
    else
    {
      if (position <= nearestGoalDefender)
      {
        secondNearestGoalDefender = nearestGoalDefender;
        nearestGoalDefender = position;
      }
      else if (position < secondNearestGoalDefender)
      {
        secondNearestGoalDefender = position;
      }
    }
  }
  if (defenderCount < 2) return attackingHome ? 1.0f : 0.0f;
  return secondNearestGoalDefender;
}

bool MatchEngine::isOffside(const MatchPlayer& receiver, bool attackingHome,
                            float defenderLine, float lineTolerance) const
{
  const float margin = MatchTuning::Passing::OFFSIDE_MARGIN + lineTolerance;

  if (attackingHome)
  {
    return receiver.position.x > MatchTuning::Pitch::CENTRE &&
           receiver.position.x >
               ball.position.x + MatchTuning::Passing::OFFSIDE_MARGIN &&
           receiver.position.x > defenderLine + margin;
  }

  return receiver.position.x < MatchTuning::Pitch::CENTRE &&
         receiver.position.x <
             ball.position.x - MatchTuning::Passing::OFFSIDE_MARGIN &&
         receiver.position.x < defenderLine - margin;
}

float MatchEngine::rawAttribute(const Player* player,
                                std::string_view name) const
{
  const auto stat = player->getStats().find(std::string(name));
  if (stat != player->getStats().end())
    return stat->second / MatchTuning::Player::RATING_SCALE;
  return static_cast<float>(player->getOverall(statsConfig)) /
         MatchTuning::Player::RATING_SCALE;
}

float MatchEngine::attribute(const Player* player, std::string_view name) const
{
  if (!player) return MatchTuning::Player::DEFAULT_ATTRIBUTE;
  return stretchAttribute(rawAttribute(player, name) + levelShift);
}

void MatchEngine::computeLevel(const Lineup& home, const Lineup& away)
{
  // Mean raw outfield quality of both elevens.
  float sum = 0.0f;
  int counted = 0;
  for (const Lineup* lineup : {&home, &away})
  {
    for (const auto& positioned : lineup->getOutfieldPlayers())
    {
      if (!positioned.player) continue;
      for (const std::string_view name :
           {"Pace", "Shooting", "Passing", "Dribbling", "Defending",
            "Physicality", "Vision"})
        sum += rawAttribute(positioned.player, name);
      counted += 7;
    }
  }
  if (counted == 0) return;
  using P = MatchTuning::Player;
  const float level = sum / static_cast<float>(counted);
  levelShift = P::LEVEL_NORMALISATION * (P::REFERENCE_LEVEL - level);
  const float fromReference = level - P::REFERENCE_LEVEL;
  levelPrecision = std::max(
      P::MIN_LEVEL_PRECISION,
      1.0f + fromReference * (fromReference < 0.0f ? P::LEVEL_FINISHING_GAIN
                                                   : P::ELITE_FINISHING_GAIN));
}

float MatchEngine::randomFloat(float minimum, float maximum)
{
  return minimum + (maximum - minimum) * uniform01(rng);
}

float MatchEngine::incidentRoll() { return uniform01(incidentRng); }

bool MatchEngine::isHomePlayer(const Player* player) const
{
  if (!player) return false;
  const auto found =
      std::ranges::find_if(players, [player](const MatchPlayer& matchPlayer)
                           { return matchPlayer.player == player; });
  return found != players.end() && found->isHomeTeam;
}

int MatchEngine::getSubstitutionsUsed(bool homeTeam) const
{
  return homeTeam ? stats.homeSubstitutions : stats.awaySubstitutions;
}

int MatchEngine::getSubstitutionWindowsUsed(bool homeTeam) const
{
  return homeTeam ? homeSubstitutionWindows : awaySubstitutionWindows;
}

bool MatchEngine::canSubstitute(bool homeTeam) const
{
  if (state == MatchState::FULL_TIME || state == MatchState::PENALTY_SHOOTOUT)
    return false;
  if (getSubstitutionsUsed(homeTeam) >= maxSubstitutions()) return false;
  // Half-time changes and further changes in an already used stoppage do
  // not consume a new window.
  const std::uint32_t lastWindow =
      homeTeam ? homeLastWindowStoppage : awayLastWindowStoppage;
  const bool sameStoppage =
      lastWindow == stoppageSequence &&
      (state != MatchState::PLAYING || manualSubstitutionStep == stepCounter);
  const int windows =
      MatchTuning::Substitution::MAX_WINDOWS +
      (extraTimeReached ? MatchTuning::Rules::EXTRA_TIME_SUBSTITUTIONS : 0);
  return state == MatchState::HALF_TIME ||
         getSubstitutionWindowsUsed(homeTeam) < windows || sameStoppage;
}

void MatchEngine::setAutoSubstitutions(bool home, bool away)
{
  if (home != homeAutoSubstitutions || away != awayAutoSubstitutions)
    ++inputRevision;
  homeAutoSubstitutions = home;
  awayAutoSubstitutions = away;
}

bool MatchEngine::substitutePlayer(uint32_t outPlayerId, const Player* inPlayer,
                                   std::optional<std::size_t> slot)
{
  if (!inPlayer || state == MatchState::FULL_TIME) return false;
  MatchCommandRecord command;
  command.type = MatchCommandType::SUBSTITUTION;
  command.homeTeam = std::ranges::find(homeBench, inPlayer) != homeBench.end();
  command.player = outPlayerId;
  command.incoming = inPlayer->getId();
  command.slot = slot;
  return recordCommand(command);
}

bool MatchEngine::performSubstitution(MatchPlayer& outgoing,
                                      const Player* inPlayer,
                                      SubstitutionReason reason)
{
  if (!inPlayer || !active(outgoing) || !canSubstitute(outgoing.isHomeTeam))
    return false;
  const bool homeTeam = outgoing.isHomeTeam;
  if (state != MatchState::HALF_TIME)
  {
    std::uint32_t& lastWindow =
        homeTeam ? homeLastWindowStoppage : awayLastWindowStoppage;
    if (lastWindow != stoppageSequence)
    {
      lastWindow = stoppageSequence;
      ++(homeTeam ? homeSubstitutionWindows : awaySubstitutionWindows);
    }
    ++stoppageLogs[static_cast<std::size_t>(period - 1)].substitutions;
    extendRestart(MatchTuning::Timing::SUBSTITUTION_DELAY_SECONDS);
  }
  ++(homeTeam ? stats.homeSubstitutions : stats.awaySubstitutions);
  auto& bench = homeTeam ? homeBench : awayBench;
  std::erase(bench, inPlayer);

  PlayerMatchStats& leaving = statsOf(outgoing);
  leaving.substitutedOff = true;
  leaving.condition = outgoing.stamina;

  const bool hadPossession = ball.possessedBy == outgoing.player;
  const PlayerID outgoingId = outgoing.player->getId();
  if (std::ranges::find(squad, inPlayer) == squad.end())
    squad.push_back(inPlayer);
  const bool incomingKeeper = inPlayer->getRole() == PlayerRole::GK;
  if (incomingKeeper && !outgoing.isGoalkeeper)
  {
    // A replacement keeper takes over from any emergency keeper.
    for (auto& teammate : players)
    {
      if (teammate.isHomeTeam != homeTeam || !teammate.isGoalkeeper) continue;
      teammate.isGoalkeeper = false;
      // An emergency keeper goes back out to the slot the new one frees.
      if (active(teammate) && outgoing.formationSlot >= 0)
      {
        teammate.formationSlot = outgoing.formationSlot;
        placeFormation(homeTeam);
      }
    }
    outgoing.basePosition = {
        homeTeam ? MatchTuning::Pitch::LINEUP_GOALKEEPER_X
                 : 1.0f - MatchTuning::Pitch::LINEUP_GOALKEEPER_X,
        MatchTuning::Pitch::CENTRE};
    outgoing.isGoalkeeper = true;
  }
  outgoing.player = inPlayer;
  loadAttributes(outgoing, inPlayer);
  const auto benchCondition =
      std::ranges::find_if(benchConditions, [inPlayer](const auto& entry)
                           { return entry.first == inPlayer->getId(); });
  outgoing.stamina =
      benchCondition != benchConditions.end() ? benchCondition->second : 1.0f;
  outgoing.sprintReserve = 1.0f;
  outgoing.isInjured = false;
  outgoing.yellowCards = 0;
  outgoing.actionCooldown = MatchTuning::Player::SUBSTITUTION_SETTLE_SECONDS;
  outgoing.statsIndex = addPlayerStats(outgoing, false);
  if (hadPossession) ball.possessedBy = inPlayer;
  if (ball.lastPossessor && ball.lastPossessor->getId() == outgoingId)
    ball.lastPossessor = inPlayer;

  substitutions.push_back({matchTimeMinutes, period, homeTeam, outgoingId,
                           inPlayer->getId(), reason});
  MatchEvent& event = logEvent(MatchEventType::SUBSTITUTION, outgoing);
  event.secondaryPlayerId = outgoingId;
  return true;
}

const Player* MatchEngine::chooseReplacement(bool homeTeam, PlayerRole role,
                                             bool wantGoalkeeper) const
{
  const auto& bench = homeTeam ? homeBench : awayBench;
  const Player* best = nullptr;
  float bestScore = -std::numeric_limits<float>::infinity();
  for (const Player* candidate : bench)
  {
    if (!candidate) continue;
    const bool keeper = candidate->getRole() == PlayerRole::GK;
    if (keeper != wantGoalkeeper) continue;
    const float fit = roleGroup(candidate->getRole()) == roleGroup(role)
                          ? MatchTuning::Substitution::ROLE_FIT_BONUS
                          : 0.0f;
    const float score =
        fit + static_cast<float>(candidate->getOverall(statsConfig)) /
                  MatchTuning::Player::RATING_SCALE;
    if (score > bestScore)
    {
      bestScore = score;
      best = candidate;
    }
  }
  return best;
}

void MatchEngine::runAiSubstitutions()
{
  if (homeLastAiReviewStoppage != stoppageSequence)
  {
    homeLastAiReviewStoppage = stoppageSequence;
    runAiSubstitutionsFor(true);
    if (homeAutoSubstitutions) runAiTouchline(true);
  }
  if (awayLastAiReviewStoppage != stoppageSequence)
  {
    awayLastAiReviewStoppage = stoppageSequence;
    runAiSubstitutionsFor(false);
    if (awayAutoSubstitutions) runAiTouchline(false);
  }
}

void MatchEngine::setMedicalFlags(PlayerID playerId, std::uint8_t flags)
{
  const auto found =
      std::ranges::find_if(medicalFlags, [playerId](const auto& entry)
                           { return entry.first == playerId; });
  if (found != medicalFlags.end())
    found->second = flags;
  else if (flags != 0)
    medicalFlags.emplace_back(playerId, flags);
}

void MatchEngine::runMedicalSubstitutions(bool homeTeam)
{
  if (!(homeTeam ? homeAutoSubstitutions : awayAutoSubstitutions) ||
      state == MatchState::PENALTY)
    return;
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam || player.isGoalkeeper)
      continue;
    const PlayerID id = player.player->getId();
    const auto entry =
        std::ranges::find_if(medicalFlags, [id](const auto& flagged)
                             { return flagged.first == id; });
    // The limit counts his own minutes on the pitch: a flagged substitute
    // starts from nothing when he comes on.
    if (entry == medicalFlags.end() ||
        !MedicalCentre::substitutionDue(
            entry->second, static_cast<int>(statsOf(player).minutesPlayed)))
      continue;
    if (!canSubstitute(homeTeam)) return;
    // Like for like, as for a tired player.
    if (const Player* replacement =
            chooseReplacement(homeTeam, player.player->getRole(), false))
      performSubstitution(player, replacement, SubstitutionReason::FATIGUE);
  }
}

void MatchEngine::runAiSubstitutionsFor(bool homeTeam)
{
  // Injured players leave at the first stoppage: replaced when possible, or
  // the team continues a player short.
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam || !player.isInjured)
      continue;
    const bool autoManaged =
        homeTeam ? homeAutoSubstitutions : awayAutoSubstitutions;
    if (!autoManaged) continue;
    // Treatment and the walk off hold up the restart.
    extendRestart(MatchTuning::Timing::INJURY_DELAY_SECONDS);
    const Player* replacement =
        canSubstitute(homeTeam)
            ? chooseReplacement(homeTeam, player.player->getRole(),
                                player.isGoalkeeper)
            : nullptr;
    if (!replacement && player.isGoalkeeper && canSubstitute(homeTeam))
      replacement =
          chooseReplacement(homeTeam, player.player->getRole(), false);
    if (!replacement ||
        !performSubstitution(player, replacement, SubstitutionReason::INJURY))
    {
      removeFromPitch(player);
    }
  }

  if (!medicalFlags.empty()) runMedicalSubstitutions(homeTeam);
  if (!(homeTeam ? homeAutoSubstitutions : awayAutoSubstitutions) ||
      state == MatchState::PENALTY || period < 2 ||
      matchTimeMinutes < MatchTuning::Substitution::EARLIEST_TACTICAL_MINUTE)
  {
    return;
  }

  const int goalDifference =
      homeTeam ? homeScore - awayScore : awayScore - homeScore;
  const float minute = matchTimeMinutes;
  const float fatigueThreshold =
      MatchTuning::Substitution::FATIGUE_THRESHOLD +
      (minute >= MatchTuning::Substitution::LATE_GAME_MINUTE
           ? MatchTuning::Substitution::FATIGUE_THRESHOLD_LATE_GAIN
           : 0.0f);
  struct Need
  {
    MatchPlayer* player = nullptr;
    float score = 0.0f;
    SubstitutionReason reason = SubstitutionReason::FATIGUE;
    int wantedGroup = 2;
  };
  std::array<Need, 3> picks{};
  const float minimumNeed =
      MatchTuning::Substitution::MINIMUM_NEED *
      (minute >= MatchTuning::Substitution::LATE_GAME_MINUTE
           ? MatchTuning::Substitution::LATE_NEED_SHARE
           : 1.0f);
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam || player.isGoalkeeper)
      continue;
    const PlayerRole role = player.player->getRole();
    Need need{&player, 0.0f, SubstitutionReason::FATIGUE, roleGroup(role)};
    need.score =
        std::max(0.0f, fatigueThreshold - player.stamina) *
            MatchTuning::Substitution::FATIGUE_NEED_SCALE *
            (isAttackingRole(role)
                 ? MatchTuning::Substitution::ATTACKER_FATIGUE_NEED_FACTOR
                 : 1.0f) +
        (minute - MatchTuning::Substitution::EARLIEST_TACTICAL_MINUTE) *
            MatchTuning::Substitution::MINUTE_NEED_GAIN *
            (1.0f - player.stamina);
    if (isAttackingRole(role) &&
        minute >= MatchTuning::Substitution::ATTACKER_ROTATION_MINUTE)
    {
      need.score +=
          (minute - MatchTuning::Substitution::ATTACKER_ROTATION_MINUTE) *
          MatchTuning::Substitution::ATTACKER_ROTATION_NEED_PER_MINUTE *
          (1.0f - player.stamina);
    }
    if (player.yellowCards > 0 && !isAttackingRole(role) &&
        minute >= MatchTuning::Substitution::CARD_RISK_MINUTE)
    {
      need.score += MatchTuning::Substitution::CARD_RISK_NEED;
      need.reason = SubstitutionReason::CARD_RISK;
    }
    if (goalDifference < 0 && isDefensiveRole(role) &&
        minute >= MatchTuning::Substitution::TRAILING_CHASE_MINUTE)
    {
      need.score += MatchTuning::Substitution::TACTICAL_NEED;
      need.reason = SubstitutionReason::TACTICAL;
      need.wantedGroup = 3;
    }
    else if (goalDifference > 0 && isAttackingRole(role) &&
             minute >= MatchTuning::Substitution::LEADING_PROTECT_MINUTE)
    {
      need.score += MatchTuning::Substitution::TACTICAL_NEED;
      need.reason = SubstitutionReason::TACTICAL;
      need.wantedGroup = 1;
    }
    if (need.score < minimumNeed * MatchTuning::Substitution::WINDOW_NEED_SHARE)
      continue;
    for (std::size_t slot = 0; slot < picks.size(); ++slot)
    {
      if (need.score <= picks[slot].score) continue;
      for (std::size_t shifted = picks.size() - 1; shifted > slot; --shifted)
        picks[shifted] = picks[shifted - 1];
      picks[slot] = need;
      break;
    }
  }

  // Keep one change in reserve for late injuries until the final minutes.
  const int reserve =
      minute < MatchTuning::Substitution::LATE_GAME_MINUTE ? 1 : 0;
  // The clearest need opens a window; a second or third change joins it at
  // a lower need, as managers make double changes to save windows.
  if (picks[0].score < minimumNeed) return;
  for (const Need& need : picks)
  {
    if (!need.player || !canSubstitute(homeTeam)) break;
    if (getSubstitutionsUsed(homeTeam) >= maxSubstitutions() - reserve) break;
    const PlayerRole wantedRole = need.wantedGroup == 3 ? PlayerRole::ST
                                  : need.wantedGroup == 1
                                      ? PlayerRole::CB
                                      : need.player->player->getRole();
    const Player* replacement = chooseReplacement(homeTeam, wantedRole, false);
    if (!replacement) break;
    performSubstitution(*need.player, replacement, need.reason);
  }
}

void MatchEngine::checkInjuries()
{
  for (auto& player : players)
  {
    if (!active(player) || player.isInjured) continue;
    const float speedRatio =
        length(player.velocity) / std::max(player.maxSpeed, EPSILON);
    const float tiredness = 1.0f - player.stamina;
    const float hazard =
        MatchTuning::Injury::BASE_HAZARD_PER_MINUTE *
        (1.0f + tiredness * tiredness *
                    MatchTuning::Injury::FATIGUE_HAZARD_MULTIPLIER) *
        (1.0f + speedRatio * MatchTuning::Injury::INTENSITY_HAZARD_MULTIPLIER);
    const float probability =
        1.0f - std::exp(-hazard * MatchTuning::Injury::CHECK_INTERVAL_MINUTES);
    if (incidentRoll() < probability) injurePlayer(player, false);
  }
}

void MatchEngine::injurePlayer(MatchPlayer& player, bool fromContact)
{
  if (!active(player) || player.isInjured) return;
  player.isInjured = true;
  statsOf(player).injured = true;
  ++(player.isHomeTeam ? stats.homeInjuries : stats.awayInjuries);
  ++stoppageLogs[static_cast<std::size_t>(period - 1)].injuries;
  MatchEvent& event = logEvent(MatchEventType::INJURY, player);
  if (fromContact) event.detail = MatchEventDetail::CONTACT;
  event.position = player.position;
  // A player who cannot run no longer carries the ball forward.
  if (ball.possessedBy == player.player) player.actionCooldown = 0.0f;
}

void MatchEngine::removeFromPitch(MatchPlayer& player)
{
  if (!active(player)) return;
  const bool homeTeam = player.isHomeTeam;
  const Vector2F vacated = player.basePosition;
  const std::int8_t vacatedSlot = player.formationSlot;
  const bool wasKeeper = player.isGoalkeeper;
  statsOf(player).condition = player.stamina;
  if (ball.possessedBy == player.player)
  {
    ball.possessedBy = nullptr;
    ball.velocity = {0.0f, 0.0f};
  }
  // Park beside the dugouts, clear of play and of each other.
  int parked = 0;
  for (const auto& other : players)
    if (other.player && !other.onPitch) ++parked;
  player.onPitch = false;
  player.isGoalkeeper = false;
  player.isPressing = false;
  player.isMakingRun = false;
  player.intent = PlayerIntent::HOLD_SHAPE;
  player.velocity = {0.0f, 0.0f};
  player.position = {MatchTuning::Pitch::CENTRE +
                         (homeTeam ? -1.0f : 1.0f) *
                             (MatchTuning::Rules::PARKED_PLAYER_OFFSET +
                              static_cast<float>(parked) *
                                  MatchTuning::Rules::PARKED_PLAYER_SPACING),
                     MatchTuning::Pitch::PLAYER_MIN_Y};
  player.movementTarget = player.position;
  if (restartTaker() == &player)
  {
    MatchPlayer* replacement =
        findClosestPlayer(ball.position, homeTeam, false);
    setRestartTaker(replacement);
    if (replacement && state != MatchState::PLAYING)
      ball.possessedBy = replacement->player;
  }
  if (wasKeeper) ensureGoalkeeper(homeTeam);
  rebalanceShape(homeTeam, vacated, wasKeeper ? -1 : vacatedSlot);

  // Law 3: a match cannot continue with fewer than seven players a side.
  const auto remaining = std::ranges::count_if(
      players, [homeTeam](const MatchPlayer& other)
      { return active(other) && other.isHomeTeam == homeTeam; });
  if (remaining < MatchTuning::Rules::MINIMUM_PLAYERS && !abandoned)
  {
    abandoned = true;
    logEvent(MatchEventType::INFO).detail = MatchEventDetail::ABANDONED;
    period = std::max(period, 2);
    endPeriod();
  }
}

void MatchEngine::rebalanceShape(bool homeTeam, Vector2F vacatedBase,
                                 std::int8_t vacatedSlot)
{
  // A side reduced to ten fills a defensive hole with the nearest midfielder
  // and drops a little deeper overall.
  const float ownDepth = homeTeam ? vacatedBase.x : 1.0f - vacatedBase.x;
  if (ownDepth < MatchTuning::Rules::DEFENSIVE_SLOT_DEPTH)
  {
    MatchPlayer* cover = nullptr;
    float best = std::numeric_limits<float>::max();
    for (auto& player : players)
    {
      if (!active(player) || player.isHomeTeam != homeTeam ||
          player.isGoalkeeper)
        continue;
      const float depth =
          homeTeam ? player.basePosition.x : 1.0f - player.basePosition.x;
      if (depth <= ownDepth + EPSILON) continue;
      const float d = distance(player.basePosition, vacatedBase);
      if (d < best)
      {
        best = d;
        cover = &player;
      }
    }
    if (cover)
    {
      cover->basePosition = vacatedBase;
      cover->formationSlot = vacatedSlot;
    }
  }
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam || player.isGoalkeeper)
      continue;
    player.basePosition.x = std::clamp(
        player.basePosition.x +
            (homeTeam ? -1.0f : 1.0f) * MatchTuning::Rules::SHORT_HANDED_DROP,
        MatchTuning::Pitch::PLAYER_MIN_X, MatchTuning::Pitch::PLAYER_MAX_X);
  }
}

void MatchEngine::ensureGoalkeeper(bool homeTeam)
{
  if (findGoalkeeper(homeTeam)) return;
  const bool autoManaged =
      homeTeam ? homeAutoSubstitutions : awayAutoSubstitutions;
  if (autoManaged && canSubstitute(homeTeam))
  {
    if (const Player* keeper =
            chooseReplacement(homeTeam, PlayerRole::GK, true))
    {
      // Sacrifice the most advanced outfield player for the new keeper.
      MatchPlayer* sacrificed = nullptr;
      float mostAdvanced = -std::numeric_limits<float>::infinity();
      for (auto& player : players)
      {
        if (!active(player) || player.isHomeTeam != homeTeam) continue;
        const float depth =
            homeTeam ? player.basePosition.x : 1.0f - player.basePosition.x;
        if (depth > mostAdvanced)
        {
          mostAdvanced = depth;
          sacrificed = &player;
        }
      }
      if (sacrificed &&
          performSubstitution(*sacrificed, keeper,
                              SubstitutionReason::GOALKEEPER_REPLACEMENT))
      {
        return;
      }
    }
  }
  // Otherwise the outfield player best with his hands goes in goal.
  MatchPlayer* emergency = nullptr;
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam) continue;
    if (!emergency || player.goalkeeping > emergency->goalkeeping)
      emergency = &player;
  }
  if (!emergency) return;
  emergency->isGoalkeeper = true;
  emergency->basePosition = {
      homeTeam ? MatchTuning::Pitch::LINEUP_GOALKEEPER_X
               : 1.0f - MatchTuning::Pitch::LINEUP_GOALKEEPER_X,
      MatchTuning::Pitch::CENTRE};
}

bool MatchEngine::applyScenario(const MatchScenario& scenario,
                                MatchState scenarioState,
                                float scenarioMatchTime, int scenarioHomeScore,
                                int scenarioAwayScore)
{
  if (scenario.players.empty() || state == MatchState::FULL_TIME) return false;

  ball = MatchBall{};
  state = scenarioState;
  transitionSecondsRemaining = 0.0f;
  matchTimeMinutes = scenarioMatchTime;
  period = scenarioMatchTime > MatchTuning::Timing::HALF_TIME_MINUTE ? 2 : 1;
  keepers = {};
  pendingAdvantage = {};
  homeScore = scenarioHomeScore;
  awayScore = scenarioAwayScore;
  slidersStale = true;
  accumulator = 0.0f;
  lastPassDecision = PassDecision{};
  lastScenarioDecision.best.reset();
  lastScenarioDecision.runnerUp.reset();
  lastScenarioDecision.reason.clear();
  lastScenarioDecision.passUtility = -std::numeric_limits<float>::infinity();
  lastScenarioDecision.shotUtility = -std::numeric_limits<float>::infinity();
  lastScenarioDecision.carryUtility = -std::numeric_limits<float>::infinity();
  lastScenarioDecision.shieldUtility = -std::numeric_limits<float>::infinity();

  ball.position = scenario.ballPosition;
  bool carrierFound = false;
  for (const auto& placement : scenario.players)
  {
    const auto found = std::ranges::find_if(
        players,
        [placement](const MatchPlayer& matchPlayer)
        {
          return matchPlayer.player &&
                 matchPlayer.player->getId() == placement.playerId;
        });
    if (found == players.end()) continue;
    MatchPlayer& matchPlayer = *found;
    matchPlayer.position = placement.position;
    matchPlayer.velocity = {0.0f, 0.0f};
    matchPlayer.basePosition = placement.position;
    matchPlayer.movementTarget = placement.position;
    matchPlayer.isTrapping = false;
    matchPlayer.trapTimer = 0.0f;
    matchPlayer.isPressing = false;
    matchPlayer.isMakingRun = false;
    matchPlayer.stamina = 1.0f;
    matchPlayer.actionCooldown = 0.0f;
    matchPlayer.tackleCooldown = 0.0f;
    matchPlayer.isMakingRun = placement.makingRun;
    matchPlayer.isInjured = false;
    matchPlayer.diveTimer = 0.0f;
    matchPlayer.isDiving = false;
    carrierFound = carrierFound || placement.playerId == scenario.carrierId;
  }
  if (!carrierFound) return false;

  const auto carrier = std::ranges::find_if(
      players,
      [&scenario](const MatchPlayer& matchPlayer)
      {
        return matchPlayer.player &&
               matchPlayer.player->getId() == scenario.carrierId;
      });
  if (carrier == players.end()) return false;

  ball.possessedBy = carrier->player;
  ball.lastPossessor = carrier->player;
  if (carrier->isHomeTeam)
  {
    homePhase = TeamPhase::POSSESSION;
    awayPhase = TeamPhase::DEFENSIVE_BLOCK;
  }
  else
  {
    awayPhase = TeamPhase::POSSESSION;
    homePhase = TeamPhase::DEFENSIVE_BLOCK;
  }

  // Make the interpolation baseline equal to the scenario so the snapshot is
  // stable, then evaluate the decision through the normal live path. A
  // carrier under external control (play mode) makes no AI decision: he
  // acts on his controller's input like in a live match.
  captureInterpolationFrame();
  if (!isControlled(*carrier)) decideAction(*carrier);
  return true;
}

MatchEvent& MatchEngine::logEvent(MatchEventType type)
{
  if (events.size() >= MatchTuning::Timing::MAX_EVENTS)
    events.erase(events.begin());
  MatchEvent& event = events.emplace_back();
  undescribedEvents = std::min(undescribedEvents + 1, events.size());
  event.timeMinute = matchTimeMinutes;
  event.type = type;
  event.homeScore = homeScore;
  event.awayScore = awayScore;
  event.homeShootout = shootout.goals[0];
  event.awayShootout = shootout.goals[1];
  event.period = period;
  event.addedMinute =
      std::max(0.0f, matchTimeMinutes - MatchRules::periodEndMinute(period));
  event.position = ball.position;
  if (isHighlightTrigger(type)) recordHighlight(type);
  return event;
}

void MatchEngine::setTeamNames(std::string homeTeam, std::string awayTeam)
{
  teamNames = {std::move(homeTeam), std::move(awayTeam)};
}

std::string MatchEngine::squadPlayerName(PlayerID playerId) const
{
  for (const Player* player : squad)
    if (player->getId() == playerId) return player->getName();
  return {};
}

void MatchEngine::describePendingEvents()
{
  if (undescribedEvents == 0) return;
  const MatchCommentaryNames names{teamNames[0], teamNames[1],
                                   [this](PlayerID playerId)
                                   { return squadPlayerName(playerId); }};
  for (std::size_t index = events.size() - undescribedEvents;
       index < events.size(); ++index)
    events[index].description = MatchCommentary::describe(events[index], names);
  undescribedEvents = 0;
}

void MatchEngine::recordHighlight(MatchEventType type)
{
  const auto priority = [](MatchEventType candidate)
  {
    switch (candidate)
    {
      case MatchEventType::GOAL:
      case MatchEventType::OWN_GOAL:
        return 4;
      case MatchEventType::PENALTY:
      case MatchEventType::RED_CARD:
      case MatchEventType::SECOND_YELLOW:
        return 3;
      case MatchEventType::SHOT:
        return 2;
      default:
        return 1;
    }
  };
  const double now = getSimulatedSeconds();
  ++highlightTriggerCount;
  lastTriggerType = type;
  lastTriggerSeconds = now;
  lastTriggerMinute = matchTimeMinutes;
  const double start = std::max(0.0, now - HIGHLIGHT_LEAD);
  const double end = now + HIGHLIGHT_TAIL;
  if (!highlights.empty() &&
      start <= highlights.back().endSeconds + HIGHLIGHT_MERGE_GAP)
  {
    MatchHighlight& merged = highlights.back();
    merged.endSeconds = std::max(merged.endSeconds, end);
    if (priority(type) >= priority(merged.type))
    {
      merged.type = type;
      merged.triggerSeconds = now;
      merged.triggerMinute = matchTimeMinutes;
    }
    return;
  }
  highlights.push_back({type, start, now, end, matchTimeMinutes});
}

MatchEvent& MatchEngine::logEvent(MatchEventType type, const MatchPlayer& actor)
{
  MatchEvent& event = logEvent(type);
  event.hasTeam = true;
  event.isHomeTeam = actor.isHomeTeam;
  event.primaryPlayerId = actor.player ? actor.player->getId() : 0;
  event.position = actor.position;
  return event;
}

namespace
{
std::string jsonFloat(float value)
{
  return std::isfinite(value) ? std::to_string(value) : std::string("null");
}

std::string jsonEscape(std::string_view value)
{
  std::string escaped;
  escaped.reserve(value.size());
  for (const char c : value)
  {
    switch (c)
    {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20U)
        {
          escaped += "\\u00";
          constexpr char HEX[] = "0123456789abcdef";
          escaped += HEX[(c >> 4U) & 0x0fU];
          escaped += HEX[c & 0x0fU];
        }
        else
        {
          escaped += c;
        }
    }
  }
  return escaped;
}
}  // namespace

std::string MatchEngine::getDebugSnapshotJson() const
{
  std::ostringstream output;
  output << std::fixed << std::setprecision(4);
  output << "{\"time_minute\":" << matchTimeMinutes << ",\"state\":\""
         << stateName(state) << "\",\"score\":{\"home\":" << homeScore
         << ",\"away\":" << awayScore << "},\"team_phase\":{\"home\":\""
         << teamPhaseName(homePhase) << "\",\"away\":\""
         << teamPhaseName(awayPhase) << '"'
         << ",\"transition_seconds_remaining\":" << transitionSecondsRemaining
         << "},\"performance\":{\"last_update_steps\":" << lastUpdateStepCount
         << ",\"dropped_simulation_steps\":" << droppedSimulationSteps
         << "},\"last_pass\":";
  if (lastPassDecision.receiverId == 0)
  {
    output << "null";
  }
  else
  {
    output << "{\"passer\":" << lastPassDecision.passerId
           << ",\"receiver\":" << lastPassDecision.receiverId
           << ",\"intent\":\"" << passIntentName(lastPassDecision.intent)
           << "\",\"target_x\":" << lastPassDecision.targetPoint.x
           << ",\"target_y\":" << lastPassDecision.targetPoint.y
           << ",\"utility\":" << lastPassDecision.utility
           << ",\"progression\":" << lastPassDecision.progression
           << ",\"lane_risk\":" << lastPassDecision.laneRisk
           << ",\"completion_probability\":"
           << lastPassDecision.completionProbability << '}';
  }
  output << ",\"decision\":{\"reason\":\""
         << jsonEscape(lastScenarioDecision.reason) << "\",\"action\":\""
         << scenarioActionName(lastScenarioDecision.action)
         << "\",\"analysis\":{\"pass\":"
         << jsonFloat(lastScenarioDecision.passUtility)
         << ",\"shot\":" << jsonFloat(lastScenarioDecision.shotUtility)
         << ",\"carry\":" << jsonFloat(lastScenarioDecision.carryUtility)
         << ",\"shield\":" << jsonFloat(lastScenarioDecision.shieldUtility)
         << "},\"best\":";
  if (lastScenarioDecision.best)
  {
    output << "{\"receiver\":" << lastScenarioDecision.best->receiverId
           << ",\"intent\":\""
           << passIntentName(lastScenarioDecision.best->intent)
           << "\",\"utility\":" << lastScenarioDecision.best->utility
           << ",\"progression\":" << lastScenarioDecision.best->progression
           << ",\"lane_risk\":" << lastScenarioDecision.best->laneRisk
           << ",\"completion_probability\":"
           << lastScenarioDecision.best->completionProbability << '}';
  }
  else
  {
    output << "null";
  }
  output << ",\"rejected\":";
  if (lastScenarioDecision.runnerUp)
  {
    output << "{\"receiver\":" << lastScenarioDecision.runnerUp->receiverId
           << ",\"intent\":\""
           << passIntentName(lastScenarioDecision.runnerUp->intent)
           << "\",\"utility\":" << lastScenarioDecision.runnerUp->utility
           << ",\"progression\":" << lastScenarioDecision.runnerUp->progression
           << ",\"lane_risk\":" << lastScenarioDecision.runnerUp->laneRisk
           << ",\"completion_probability\":"
           << lastScenarioDecision.runnerUp->completionProbability << '}';
  }
  else
  {
    output << "null";
  }
  output << "},\"ball\":{\"x\":" << ball.position.x
         << ",\"y\":" << ball.position.y << ",\"z\":" << ball.z
         << ",\"possessed_by\":";
  if (ball.possessedBy)
    output << ball.possessedBy->getId();
  else
    output << "null";
  output << ",\"intended_receiver\":";
  if (ball.intendedReceiver)
    output << ball.intendedReceiver->getId();
  else
    output << "null";
  output << "},\"stats\":{\"home_shots\":" << stats.homeShots
         << ",\"away_shots\":" << stats.awayShots
         << ",\"home_xg\":" << stats.homeShotXG
         << ",\"away_xg\":" << stats.awayShotXG
         << ",\"home_possession\":" << stats.homePossession
         << ",\"away_possession\":" << stats.awayPossession
         << ",\"home_substitutions\":" << stats.homeSubstitutions
         << ",\"away_substitutions\":" << stats.awaySubstitutions
         << ",\"home_passes_attempted\":" << stats.homePassesAttempted
         << ",\"away_passes_attempted\":" << stats.awayPassesAttempted
         << ",\"home_passes_completed\":" << stats.homePassesCompleted
         << ",\"away_passes_completed\":" << stats.awayPassesCompleted
         << ",\"home_progressive_passes\":" << stats.homeProgressivePasses
         << ",\"away_progressive_passes\":" << stats.awayProgressivePasses
         << ",\"home_through_balls\":" << stats.homeThroughBalls
         << ",\"away_through_balls\":" << stats.awayThroughBalls
         << ",\"home_crosses\":" << stats.homeCrosses
         << ",\"away_crosses\":" << stats.awayCrosses
         << ",\"home_cutbacks\":" << stats.homeCutbacks
         << ",\"away_cutbacks\":" << stats.awayCutbacks
         << ",\"home_switches_of_play\":" << stats.homeSwitchesOfPlay
         << ",\"away_switches_of_play\":" << stats.awaySwitchesOfPlay
         << ",\"home_tackles\":" << stats.homeTackles
         << ",\"away_tackles\":" << stats.awayTackles
         << ",\"home_offsides\":" << stats.homeOffsides
         << ",\"away_offsides\":" << stats.awayOffsides
         << ",\"home_fouls\":" << stats.homeFouls
         << ",\"away_fouls\":" << stats.awayFouls
         << ",\"home_yellow_cards\":" << stats.homeYellowCards
         << ",\"away_yellow_cards\":" << stats.awayYellowCards
         << ",\"home_red_cards\":" << stats.homeRedCards
         << ",\"away_red_cards\":" << stats.awayRedCards
         << ",\"home_corners\":" << stats.homeCorners
         << ",\"away_corners\":" << stats.awayCorners
         << ",\"home_injuries\":" << stats.homeInjuries
         << ",\"away_injuries\":" << stats.awayInjuries
         << "},\"clock\":{\"period\":" << period
         << ",\"added_first_half\":" << addedMinutes[0]
         << ",\"added_second_half\":" << addedMinutes[1]
         << ",\"referee_strictness\":" << refereeStrictness
         << "},\"goalkeepers\":{\"home\":\""
         << goalkeeperStateName(keepers[0].state) << "\",\"away\":\""
         << goalkeeperStateName(keepers[1].state) << "\"},\"players\":[";
  for (size_t index = 0; index < players.size(); ++index)
  {
    const auto& player = players[index];
    if (index > 0) output << ',';
    output << "{\"id\":" << (player.player ? player.player->getId() : 0)
           << ",\"name\":"
           << std::quoted(player.player ? player.player->getName() : "")
           << ",\"home\":" << (player.isHomeTeam ? "true" : "false")
           << ",\"x\":" << player.position.x << ",\"y\":" << player.position.y
           << ",\"target_x\":" << player.movementTarget.x
           << ",\"target_y\":" << player.movementTarget.y << ",\"intent\":\""
           << intentName(player.intent) << "\",\"stamina\":" << player.stamina
           << ",\"pressing\":" << (player.isPressing ? "true" : "false")
           << ",\"making_run\":" << (player.isMakingRun ? "true" : "false")
           << ",\"on_pitch\":" << (player.onPitch ? "true" : "false")
           << ",\"goalkeeper\":" << (player.isGoalkeeper ? "true" : "false")
           << ",\"yellow_cards\":" << player.yellowCards
           << ",\"rating\":" << playerStats[player.statsIndex].rating << '}';
  }
  output << "]}";
  return output.str();
}

bool MatchEngine::writeDebugSnapshot(std::string_view path) const
{
  std::ofstream output{std::string(path)};
  if (!output.is_open()) return false;
  output << getDebugSnapshotJson() << '\n';
  return output.good();
}
