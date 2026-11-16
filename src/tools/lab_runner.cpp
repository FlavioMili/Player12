// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/lab_runner.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <memory>
#include <numeric>
#include <utility>

#include "global/thread_pool.h"
#include "model/match.h"
#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/match_report.h"
#include "model/player.h"
#include "tools/lab_fixtures.h"
#include "tools/lab_trace.h"

namespace Lab
{
namespace
{
/** Headless update step when the engine has no simulateToEnd() (same as
 * MatchSimulation::run). */
constexpr float HEADLESS_STEP_SECONDS = 0.25f;
/** A match needing more updates than this is reported as stuck. */
constexpr int MAX_HEADLESS_UPDATES = 2'000'000;
constexpr TeamID HOME_TEAM_ID = 1;
constexpr TeamID AWAY_TEAM_ID = 2;
constexpr double REGULATION_MINUTES = 90.0;
constexpr double HALF_MINUTES = 45.0;
/** Jobs per parallel batch between progress callbacks. */
constexpr std::size_t PROGRESS_CHUNK = 64;
constexpr int TOP_SCORELINES = 10;
constexpr int HISTOGRAM_GOALS = 8;
/** Starter-rating gaps below this are ties without a stronger side. */
constexpr double RATING_TIE = 0.05;
/** Reference goals per match for the Poisson column (CT-M13 target). */
constexpr double REFERENCE_GOALS = 2.83;

template <typename Engine>
void runToFullTime(Engine& engine, std::uint32_t seed, bool background)
{
  if constexpr (requires { engine.simulateToEnd(MatchFidelity::FULL); })
  {
    engine.simulateToEnd(background ? MatchFidelity::BACKGROUND
                                    : MatchFidelity::FULL);
  }
  else if constexpr (requires { engine.simulateToEnd(); })
  {
    engine.simulateToEnd();
  }
  else
  {
    for (int step = 0; engine.getState() != MatchState::FULL_TIME; ++step)
    {
      if (step >= MAX_HEADLESS_UPDATES)
        throw SimulationError(seed, "match did not reach full time");
      engine.update(HEADLESS_STEP_SECONDS);
    }
  }
  if (engine.getState() != MatchState::FULL_TIME)
    throw SimulationError(seed, "match did not reach full time");
}

std::size_t goalBin(int period, double minute)
{
  if (period <= 1)
  {
    if (minute < 15.0) return 0;
    if (minute < 30.0) return 1;
    return 2;
  }
  if (minute < 60.0) return 3;
  if (minute < 75.0) return 4;
  return 5;
}

struct ScoringEvent
{
  double time = 0.0;
  bool goal = false; /*!< Otherwise a sending-off. */
  bool home = false; /*!< Side credited with the goal / sent-off side. */
};

/**
 * Splits the match into intervals between goals and sendings-off and adds,
 * for each team, the minutes and goals by score state and numbers. A goal
 * counts in the state before it.
 */
void accumulateGameStates(MatchSample& sample,
                          std::span<const ScoringEvent> events, double length)
{
  std::array<int, 2> score{};
  std::array<int, 2> sentOff{};
  double previous = 0.0;
  const auto stateOf = [&](std::size_t team) -> int
  {
    const int diff = score[team] - score[1 - team];
    if (diff == 1) return LEAD_BY_ONE;
    if (diff == 0) return LEVEL;
    if (diff == -1) return TRAIL_BY_ONE;
    return -1;
  };
  const auto strengthOf = [&](std::size_t team) -> int
  {
    if (sentOff[team] > sentOff[1 - team]) return MAN_DOWN;
    if (sentOff[team] < sentOff[1 - team]) return MAN_UP;
    return EVEN;
  };
  const auto addMinutes = [&](double until)
  {
    const double span = std::max(0.0, until - previous);
    for (std::size_t team = 0; team < 2; ++team)
    {
      if (const int state = stateOf(team); state >= 0)
        sample.state_minutes[static_cast<std::size_t>(state)] += span;
      sample.strength_minutes[static_cast<std::size_t>(strengthOf(team))] +=
          span;
    }
    previous = std::max(previous, until);
  };
  for (const ScoringEvent& event : events)
  {
    const double time = std::clamp(event.time, previous, length);
    addMinutes(time);
    const std::size_t team = event.home ? 0 : 1;
    if (event.goal)
    {
      if (const int state = stateOf(team); state >= 0)
        ++sample.state_goals[static_cast<std::size_t>(state)];
      ++sample.strength_goals[static_cast<std::size_t>(strengthOf(team))];
      ++score[team];
    }
    else
    {
      ++sentOff[team];
    }
  }
  addMinutes(length);
}

void extractEngine(MatchSample& sample, const MatchEngine& engine)
{
  const MatchStats& stats = engine.getStats();
  sample.detailed = true;
  sample.abandoned = engine.isAbandoned();
  sample.goals = {engine.getHomeScore(), engine.getAwayScore()};
  sample.shots = {stats.homeShots, stats.awayShots};
  sample.on_target = {stats.homeOnTarget, stats.awayOnTarget};
  sample.passes_attempted = {stats.homePassesAttempted,
                             stats.awayPassesAttempted};
  sample.passes_completed = {stats.homePassesCompleted,
                             stats.awayPassesCompleted};
  sample.fouls = {stats.homeFouls, stats.awayFouls};
  sample.xg = {static_cast<double>(stats.homeShotXG),
               static_cast<double>(stats.awayShotXG)};
  sample.home_possession = stats.homePossession;
  sample.corners = stats.homeCorners + stats.awayCorners;
  for (const MatchEvent& event : engine.getEvents())
  {
    sample.throw_ins += event.type == MatchEventType::THROW_IN ? 1 : 0;
    sample.goal_kicks += event.type == MatchEventType::GOAL_KICK ? 1 : 0;
    sample.free_kicks += event.type == MatchEventType::FREE_KICK ? 1 : 0;
  }
  sample.event_log_full =
      engine.getEvents().size() >= MatchTuning::Timing::MAX_EVENTS;
  sample.yellows = stats.homeYellowCards + stats.awayYellowCards;
  sample.reds = stats.homeRedCards + stats.awayRedCards;
  sample.offsides = stats.homeOffsides + stats.awayOffsides;
  sample.inside_box = stats.homeShotsInsideBox + stats.awayShotsInsideBox;
  sample.headed_goals = stats.homeHeadedGoals + stats.awayHeadedGoals;
  sample.set_piece_goals = stats.homeSetPieceGoals + stats.awaySetPieceGoals;
  sample.penalty_goals = stats.homePenaltyGoals + stats.awayPenaltyGoals;
  sample.penalties = stats.homePenalties + stats.awayPenalties;
  sample.substitutions = stats.homeSubstitutions + stats.awaySubstitutions;
  sample.injuries = stats.homeInjuries + stats.awayInjuries;
  sample.take_ons = {stats.homeTakeOns, stats.awayTakeOns};
  sample.take_ons_won = {stats.homeTakeOnsWon, stats.awayTakeOnsWon};
  // The duel roll's attempts: free-ball and contest tackles have no roll.
  sample.tackle_duels = {stats.homeTackleAttempts - stats.homeFreeBallTackles -
                             stats.homeContestTackles,
                         stats.awayTackleAttempts - stats.awayFreeBallTackles -
                             stats.awayContestTackles};
  sample.contest_tackles = stats.homeContestTackles + stats.awayContestTackles;
  sample.contest_tackles_won =
      stats.homeContestTacklesWon + stats.awayContestTacklesWon;
  sample.carrier_air_swings =
      stats.homeCarrierAirSwings + stats.awayCarrierAirSwings;
  sample.tackler_air_swings =
      stats.homeTacklerAirSwings + stats.awayTacklerAirSwings;
  sample.commit_misses = stats.homeCommitMisses + stats.awayCommitMisses;
  sample.blocks = stats.homeBlocks + stats.awayBlocks;
  sample.blocks_committed = stats.blocksCommitted;
  sample.blocks_popped = stats.blocksPoppedUp;
  for (const int count : stats.shotBlocksByPart) sample.shot_body_blocks += count;
  for (std::size_t part = 0; part < stats.shotBlocksByPart.size(); ++part)
    if (isLegPart(static_cast<ShotBodyPart>(part)))
      sample.shot_leg_blocks += stats.shotBlocksByPart[part];
  sample.shot_reaction_blocks = stats.shotReactionBlocks;
  sample.shot_deflections = stats.shotDeflections;
  for (const int count : stats.keeperBodyHitsByPart) sample.keeper_body_hits += count;
  sample.keeper_body_still_on_goal = stats.keeperBodyStillOnGoal;
  sample.keeper_hand_hits =
      stats.keeperBodyHitsByPart[static_cast<std::size_t>(ShotBodyPart::HAND)] +
      stats.keeperBodyHitsByPart[static_cast<std::size_t>(ShotBodyPart::ARM)];
  sample.keeper_hands_went = stats.keeperHandsWent;
  sample.keeper_hands_together = stats.keeperHandsTogether;
  sample.keeper_hands_late = stats.keeperHandsLate;
  sample.keeper_dives = stats.keeperDives;
  sample.keeper_dives_late = stats.keeperDivesLate;
  sample.keeper_hand_shots = stats.keeperHandShots;
  sample.keeper_both_hands = stats.keeperBothHands;
  sample.keeper_catches = stats.keeperCatches;
  sample.keeper_held = stats.keeperHeld;
  sample.keeper_fumbles = stats.keeperFumbles;
  sample.keeper_parries = stats.keeperParries;
  sample.keeper_balls_in_hands = stats.keeperBallsInHands;
  sample.keeper_rolls = stats.keeperRolls;
  sample.keeper_throws = stats.keeperThrows;
  sample.keeper_punts = stats.keeperPunts;
  sample.keeper_claims_tried = stats.homeKeeperClaimAttempts + stats.awayKeeperClaimAttempts;
  sample.keeper_claims_held = stats.homeKeeperClaims + stats.awayKeeperClaims;
  sample.keeper_punches = stats.keeperPunches;
  sample.keeper_claim_parries = stats.keeperClaimParries;
  sample.keeper_smother_dives = stats.keeperSmotherDives;
  sample.keeper_smothers_held = stats.keeperSmothersHeld;
  sample.shootout_kicks = stats.shootoutKicks;
  sample.shootout_goals = stats.shootoutGoals;
  sample.shootout_saves = stats.shootoutSaves;
  for (const int count : stats.ballBodyHitsByPart) sample.ball_body_hits += count;
  for (std::size_t part = 0; part < stats.ballBodyHitsByPart.size(); ++part)
    if (isLegPart(static_cast<ShotBodyPart>(part)))
      sample.ball_leg_hits += stats.ballBodyHitsByPart[part];
  sample.ball_head_hits = stats.ballBodyHitsByPart[static_cast<std::size_t>(ShotBodyPart::HEAD)];
  sample.ball_reaction_blocks = stats.ballReactionBlocks;
  sample.passes_blocked = stats.passesBlocked;
  sample.ball_deflections = stats.ballDeflections;
  sample.direct_free_kicks = stats.directFreeKicks;
  sample.direct_free_kicks_on_target = stats.directFreeKicksOnTarget;
  sample.direct_free_kicks_blocked = stats.directFreeKicksBlocked;
  sample.direct_free_kick_goals = stats.directFreeKickGoals;
  sample.shot_torso_pass_throughs = stats.shotTorsoPassThroughs;
  sample.shot_torso_inside_at_start = stats.shotTorsoInsideAtStart;
  sample.shot_leg_chances = stats.shotLegChances;
  sample.shot_leg_outcomes = stats.shotLegOutcomes;
  sample.go_ins = stats.homeGoIns + stats.awayGoIns;
  sample.go_ins_free_foot = stats.goInsOnFreeFoot;
  sample.holds = stats.homeHolds + stats.awayHolds;
  sample.intents_win = stats.intentsWin;
  sample.intents_clear = stats.intentsClear;
  sample.intents_control = stats.intentsControl;
  sample.collisions = stats.collisions;
  sample.collisions_jolt_two = stats.collisionsJoltTwo;
  sample.collisions_jolt_four = stats.collisionsJoltFour;
  sample.collision_stumbles = stats.collisionStumbles;
  sample.collision_falls = stats.collisionFalls;
  sample.collision_slips = stats.collisionSlips;
  sample.collision_knocks_by_ends_up = stats.collisionKnocksByEndsUp;
  sample.collision_knocks_small_jolt = stats.collisionKnocksSmallJolt;
  sample.collision_big_hits = stats.collisionBigHits;
  sample.collision_falls_losing_ball = stats.collisionFallsLosingBall;
  sample.collision_down_seconds = stats.collisionDownSeconds;
  sample.collision_down_play_seconds = stats.collisionDownPlaySeconds;
  sample.collision_get_ups_for_ball = stats.collisionGetUpsForBall;
  sample.collision_get_ups_in_stoppage = stats.collisionGetUpsInStoppage;
  sample.collision_get_ups_gingerly = stats.collisionGetUpsGingerly;
  sample.collision_get_ups_fixed = stats.collisionGetUpsFixed;
  sample.collision_downs_fifteen = stats.collisionDownsFifteen;
  sample.collision_ball_off_fallen = stats.collisionBallOffFallen;
  sample.collision_knocks_by_situation = stats.collisionKnocksBySituation;
  sample.collision_knocks_by_intent = stats.collisionKnocksByIntent;
  sample.collision_knocks_by_intent_pair = stats.collisionKnocksByIntentPair;
  sample.positioning_knocks_by_reason = stats.positioningKnocksByReason;
  sample.positioning_knocks_by_speed = stats.positioningKnocksBySpeed;
  sample.positioning_knocks_by_motion = stats.positioningKnocksByMotion;
  sample.give_way_slows = stats.giveWaySlows;
  sample.give_way_rounds = stats.giveWayRounds;
  sample.give_way_holds = stats.giveWayHolds;
  sample.collision_chase_knocks_with = stats.collisionChaseKnocksWith;
  sample.collision_chase_knocks_on_chaser = stats.collisionChaseKnocksOnChaser;
  sample.way_rounds_started = stats.wayRoundsStarted;
  sample.way_round_refreshes = stats.wayRoundRefreshes;
  sample.tackle_duels_won = {stats.homeTackleDuelsWon,
                             stats.awayTackleDuelsWon};
  sample.free_ball_tackles = {stats.homeFreeBallTackles,
                              stats.awayFreeBallTackles};
  sample.free_ball_tackles_won = {stats.homeFreeBallTacklesWon,
                                  stats.awayFreeBallTacklesWon};  sample.keeper_claim_attempts = {stats.homeKeeperClaimAttempts,
                                  stats.awayKeeperClaimAttempts};
  sample.keeper_claims = {stats.homeKeeperClaims, stats.awayKeeperClaims};
  sample.aerial_contested = stats.aerialDuelsContested;
  sample.aerial_home_won = stats.homeAerialDuelsWon;
  sample.header_sequences = stats.headerSequences;
  sample.header_sequence_headers = stats.headerSequenceHeaders;
  sample.header_sequences_long = stats.headerSequencesLong;
  sample.header_sequence_longest = stats.headerSequenceLongest;
  sample.header_sequences_to_control = stats.headerSequencesToControl;
  sample.header_sequences_kept = stats.headerSequencesKept;
  sample.added_minutes = engine.getAddedMinutes(1) + engine.getAddedMinutes(2);
  sample.ball_in_play = static_cast<double>(stats.ballInPlayMinutes);
  sample.match_length = static_cast<double>(engine.getElapsedMatchMinutes());
  for (const MatchSubstitution& change : engine.getSubstitutions())
    sample.substitution_minute_sum += static_cast<double>(change.timeMinute);

  // Clock of the second half restarts at 45; convert to elapsed minutes.
  const double secondHalf = std::max(
      0.0, static_cast<double>(engine.getMatchTimeMinutes()) - HALF_MINUTES);
  const double firstHalf = std::max(0.0, sample.match_length - secondHalf);
  std::vector<ScoringEvent> timeline;
  for (const MatchEvent& event : engine.getEvents())
  {
    const auto minute = static_cast<double>(event.timeMinute);
    const double elapsed =
        event.period <= 1 ? minute : firstHalf + (minute - HALF_MINUTES);
    using enum MatchEventType;
    const bool goal = event.type == MatchEventType::GOAL;
    const bool ownGoal = event.type == MatchEventType::OWN_GOAL;
    if (event.type == MatchEventType::SHOT_BLOCKED &&
        event.detail != MatchEventDetail::WALL)
      ++sample.shots_blocked;
    if (goal || ownGoal)
    {
      if (ownGoal) ++sample.own_goals;
      if (event.period >= 2 && minute >= REGULATION_MINUTES)
        ++sample.late_goals;
      ++sample.goal_bins[goalBin(event.period, minute)];
      // An own goal counts for the side of the other team.
      const bool home = ownGoal ? !event.isHomeTeam : event.isHomeTeam;
      timeline.push_back({elapsed, true, home});
    }
    else if ((event.type == RED_CARD || event.type == SECOND_YELLOW) &&
             event.hasTeam)
    {
      timeline.push_back({elapsed, false, event.isHomeTeam});
    }
  }
  std::ranges::stable_sort(timeline, {}, &ScoringEvent::time);
  accumulateGameStates(sample, timeline, sample.match_length);

  for (const PlayerMatchStats& entry : engine.getPlayerStats())
  {
    sample.tackles_attempted += entry.tacklesAttempted;
    sample.tackles_won += entry.tacklesWon;
    sample.interceptions += entry.interceptions;
    sample.clearances += entry.clearances;
    sample.aerial_duels += entry.aerialDuelsWon + entry.aerialDuelsLost;
    sample.pressures += entry.pressures;
    sample.key_passes += entry.keyPasses;
    sample.saves += entry.saves;
    sample.touches += entry.touches;
    const bool fullMatch = entry.started && !entry.substitutedOff &&
                           !entry.sentOff && !entry.injured;
    if (!fullMatch || entry.role == PlayerRole::GK) continue;
    ++sample.full_match_players;
    sample.full_match_distance += static_cast<double>(entry.distanceMetres);
    sample.second_half_distance +=
        static_cast<double>(entry.secondHalfDistanceMetres);
    sample.full_match_high_intensity +=
        static_cast<double>(entry.highIntensityMetres);
    sample.full_match_sprint += static_cast<double>(entry.sprintMetres);
    sample.full_match_sprints += entry.sprints;
    sample.full_match_walk += static_cast<double>(entry.walkMetres);
    sample.full_match_jog += static_cast<double>(entry.jogMetres);
    sample.full_match_run += static_cast<double>(entry.runMetres);
    sample.full_match_energy += static_cast<double>(entry.metabolicEnergy) / 1000.0;
    sample.full_match_anaerobic +=
        static_cast<double>(entry.anaerobicEnergy) / 1000.0;
    sample.full_match_braking_energy +=
        static_cast<double>(entry.brakingEnergy) / 1000.0;
    sample.full_match_braking_anaerobic +=
        static_cast<double>(entry.brakingAnaerobicEnergy) / 1000.0;
    for (std::size_t mode = 0; mode < MOVEMENT_MODE_COUNT; ++mode)
    {
      sample.mode_seconds[mode] += static_cast<double>(entry.modeSeconds[mode]);
      sample.mode_metres[mode] += static_cast<double>(entry.modeMetres[mode]);
    }
    if (entry.emptyAtMinute >= 0.0f)
    {
      ++sample.emptied_players;
      sample.empty_minute_sum += static_cast<double>(entry.emptyAtMinute);
    }
    const auto& windows = entry.runningByFive;
    constexpr std::size_t PER_HALF = PlayerMatchStats::FIVE_MINUTE_WINDOWS / 2;
    double windowSum = 0.0;
    for (const float metres : windows) windowSum += static_cast<double>(metres);
    const auto peak = static_cast<std::size_t>(
        std::ranges::max_element(windows) - windows.begin());
    if (windowSum > 0.0 && peak % PER_HALF != PER_HALF - 1)
    {
      const double mean = windowSum / static_cast<double>(windows.size());
      sample.peak_next_change_sum +=
          static_cast<double>(windows[peak + 1]) / mean - 1.0;
      ++sample.peak_next_players;
    }
  }
  for (const LoadProfileStats& side : engine.getLoadProfile())
    for (std::size_t spell = 0; spell < LoadProfileStats::SPELLS; ++spell)
    {
      sample.load.metres[spell] += side.metres[spell];
      sample.load.highIntensityMetres[spell] += side.highIntensityMetres[spell];
      sample.load.playerSeconds[spell] += side.playerSeconds[spell];
    }
  for (const MatchPlayer& player : engine.getPlayers())
  {
    if (!player.player) continue;
    if (player.onPitch && !player.isGoalkeeper)
    {
      ++sample.finishers;
      const float fuel = player.stamina;
      sample.end_fuel_sum += static_cast<double>(fuel);
      sample.end_fuel_square_sum += static_cast<double>(fuel) * fuel;
      sample.end_fuel_min = std::min(sample.end_fuel_min, fuel);
      if (fuel <= engine.fuelFloor(player.isHomeTeam) + 0.005f)
        ++sample.finishers_at_floor;
      const std::size_t band = fuel >= 0.6f   ? 0
                               : fuel >= 0.3f ? 1
                               : fuel >= 0.1f ? 2
                               : fuel > 0.005f ? 3
                                               : 4;
      ++sample.end_fuel_bands[band];
    }
    ++sample.derived_players;
    for (std::size_t index = 0; index < DERIVED_STAT_COUNT; ++index)
      sample.derived_sum[index] += static_cast<double>(player.derived[index]);
  }
  for (const MovementJitterStats& side : engine.getMovementJitter())
  {
    MovementJitterStats& total = sample.jitter;
    for (std::size_t intent = 0; intent < PLAYER_INTENT_COUNT; ++intent)
    {
      const auto& from = side.byIntent[intent];
      auto& to = total.byIntent[intent];
      to.refreshes += from.refreshes;
      to.targetMoveMetres += from.targetMoveMetres;
      to.moves += from.moves;
      to.reversals += from.reversals;
      to.switchesIn += from.switchesIn;
      to.flipBacks += from.flipBacks;
      to.runningSeconds += from.runningSeconds;
      to.speedReversals += from.speedReversals;
    }
    total.eventRefreshes += side.eventRefreshes;
    total.eventTargetMoveMetres += side.eventTargetMoveMetres;
    total.playerSeconds += side.playerSeconds;
    total.pathMetres += side.pathMetres;
    total.netMetres += side.netMetres;
    total.turnSamples += side.turnSamples;
    total.sharpTurns += side.sharpTurns;
    total.runningSeconds += side.runningSeconds;
    total.speedReversals += side.speedReversals;
  }
  sample.strike_contacts = engine.getStrikeContacts();
  sample.body_contacts = engine.getBodyContacts();
  sample.first_half_distance =
      sample.full_match_distance - sample.second_half_distance;
  sample.first_half_minutes = firstHalf;
  sample.second_half_minutes = secondHalf;
}

bool finite(const MatchSample& sample)
{
  return std::isfinite(sample.xg[0]) && std::isfinite(sample.xg[1]) &&
         std::isfinite(sample.home_possession) &&
         std::isfinite(sample.ball_in_play) &&
         std::isfinite(sample.match_length) &&
         std::isfinite(sample.full_match_distance) &&
         std::isfinite(sample.second_half_distance);
}

double share(std::size_t part, std::size_t whole)
{
  return whole == 0 ? 0.0
                    : static_cast<double>(part) / static_cast<double>(whole);
}

std::string percent(double fraction)
{
  return std::format("{:.1f}%", 100.0 * fraction);
}

std::string ciPercent(const Estimate& estimate)
{
  if (!estimate.valid()) return "-";
  return std::format("{:.1f}% [{:.1f}, {:.1f}]", 100.0 * estimate.value,
                     100.0 * estimate.low, 100.0 * estimate.high);
}

/** Pairs (a, b) with a < b of the presets, in a fixed order. */
std::vector<std::pair<std::size_t, std::size_t>> presetPairs()
{
  std::vector<std::pair<std::size_t, std::size_t>> pairs;
  for (std::size_t a = 0; a < TACTIC_PRESETS.size(); ++a)
    for (std::size_t b = a + 1; b < TACTIC_PRESETS.size(); ++b)
      pairs.emplace_back(a, b);
  return pairs;
}
}  // namespace

std::uint32_t matchSeed(std::uint64_t seed, std::uint64_t index)
{
  // splitmix64 of (seed, index): decorrelated, stable across platforms.
  std::uint64_t z = seed * 0x9E3779B97F4A7C15ULL + index + 1;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  z ^= z >> 31;
  return static_cast<std::uint32_t>(z >> 32);
}

namespace
{
/** Builds the job's squads and engine, sets every seam, and hands the engine
 * and both lineups to `use` (simulateMatch, traceMatch). */
template <typename Use>
auto withMatchEngine(const MatchJob& job, const StatsConfig& config, Use&& use)
{
  std::vector<std::unique_ptr<Player>> pool;
  const auto build = [&](TeamID id, const TeamSetup& side)
  {
    const FormationSlots* formation =
        side.formation ? &*side.formation : nullptr;
    return side.uniform
               ? buildUniformLineup(id, side.rating, config, pool, formation)
               : buildLabLineup(id, side.rating, pool, formation);
  };
  const Lineup home = build(HOME_TEAM_ID, job.home);
  const Lineup away = build(AWAY_TEAM_ID, job.away);
  Strategy homeStrategy;
  homeStrategy.setAllSliders(job.home.sliders);
  Strategy awayStrategy;
  awayStrategy.setAllSliders(job.away.sliders);

  MatchEngine engine(home, away, homeStrategy, awayStrategy, config, job.seed,
                     job.handshake, job.possession);
  // Setting a context redraws the referee, so the default is left alone.
  if (job.homeAdvantage != 1.0f)
  {
    MatchContext context;
    context.homeAdvantageScale = job.homeAdvantage;
    engine.setMatchContext(context);
  }
  engine.setBrain(true, job.home.brain);
  engine.setBrain(false, job.away.brain);
  engine.setBody(true, job.home.body);
  engine.setBody(false, job.away.body);
  engine.setManager(true, job.home.manager);
  engine.setManager(false, job.away.manager);
  engine.setReferee(job.referee);
  engine.setBallPhysics(job.ballPhysics);
  engine.setOutOfPlay(job.outOfPlay);
  engine.setFirstTouchKnobs(job.touchKnobs);
  engine.setStrikeKnobs(job.strikeKnobs);
  if (job.knockout)
  {
    MatchRules::Knockout rules;
    rules.required = true;
    rules.extraTime = false;
    engine.setKnockout(rules);
  }
  MatchdaySquad::carryCondition(engine, home);
  MatchdaySquad::carryCondition(engine, away);
  return use(engine, home, away);
}
}  // namespace

MatchSample simulateMatch(const MatchJob& job, const StatsConfig& config)
{
  return withMatchEngine(
      job, config,
      [&](MatchEngine& engine, const Lineup& home, const Lineup& away)
      {
        runToFullTime(engine, job.seed, job.background);
        MatchSample sample;
        sample.seed = job.seed;
        sample.rating = {lineupRating(home, config),
                         lineupRating(away, config)};
        extractEngine(sample, engine);
        if (!finite(sample))
          throw SimulationError(job.seed,
                                "match produced non-finite statistics");
        return sample;
      });
}

void traceMatch(const MatchJob& job, const StatsConfig& config, int ticks,
                std::string_view run, std::ostream& out)
{
  withMatchEngine(job, config,
                  [&](MatchEngine& engine, const Lineup&, const Lineup&)
                  {
                    writeTraceRows(out, run, 0, engine);
                    for (int tick = 1; tick <= ticks; ++tick)
                    {
                      if (engine.getState() == MatchState::FULL_TIME) break;
                      engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
                      writeTraceRows(out, run, tick, engine);
                    }
                    return 0;
                  });
}

std::vector<MatchSample> simulateMatches(
    std::span<const MatchJob> jobs, const StatsConfig& config, unsigned threads,
    const std::function<void(std::size_t)>& progress)
{
  std::vector<MatchSample> samples(jobs.size());
  const unsigned count = std::clamp(threads, 1U, ThreadPool::MAX_THREADS);
  std::unique_ptr<ThreadPool> pool;
  if (count > 1) pool = std::make_unique<ThreadPool>(count - 1);
  for (std::size_t begin = 0; begin < jobs.size(); begin += PROGRESS_CHUNK)
  {
    const std::size_t size = std::min(PROGRESS_CHUNK, jobs.size() - begin);
    const auto task = [&samples, &jobs, &config, begin](std::size_t offset)
    {
      const std::size_t index = begin + offset;
      samples[index] = simulateMatch(jobs[index], config);
    };
    if (pool)
      pool->parallelFor(size, task);
    else
      for (std::size_t offset = 0; offset < size; ++offset) task(offset);
    if (progress) progress(begin + size);
  }
  return samples;
}

MatchSample sampleFromReport(const MatchReport& report)
{
  MatchSample sample;
  sample.detailed = false;
  sample.goals = {report.home_goals, report.away_goals};
  const auto side = [&sample](std::size_t index, const TeamMatchStats& stats)
  {
    sample.shots[index] = stats.shots;
    sample.on_target[index] = stats.shots_on_target;
    sample.passes_attempted[index] = stats.passes_attempted;
    sample.passes_completed[index] = stats.passes_completed;
    sample.fouls[index] = stats.fouls;
    sample.xg[index] = static_cast<double>(stats.expected_goals);
    sample.corners += stats.corners;
    sample.yellows += stats.yellow_cards;
    sample.reds += stats.red_cards;
    sample.offsides += stats.offsides;
  };
  side(0, report.home_stats);
  side(1, report.away_stats);
  sample.home_possession = report.home_stats.possession;
  for (const MatchReportEvent& event : report.events)
  {
    if (event.kind != MatchEventKind::GOAL &&
        event.kind != MatchEventKind::OWN_GOAL)
      continue;
    if (event.kind == MatchEventKind::OWN_GOAL) ++sample.own_goals;
    const bool firstHalfAdded =
        event.added_minute > 0 &&
        event.minute - event.added_minute <= static_cast<int>(HALF_MINUTES);
    const int period =
        firstHalfAdded || event.minute <= static_cast<int>(HALF_MINUTES) ? 1
                                                                         : 2;
    const auto minute = static_cast<double>(event.minute);
    if (period == 2 && minute >= REGULATION_MINUTES) ++sample.late_goals;
    ++sample.goal_bins[goalBin(period, minute)];
  }
  return sample;
}

std::map<std::string, Estimate> matchMetrics(
    std::span<const MatchSample> samples)
{
  std::map<std::string, Estimate> metrics;
  std::vector<const MatchSample*> valid;
  std::size_t abandoned = 0;
  bool detailed = !samples.empty();
  for (const MatchSample& sample : samples)
  {
    if (sample.abandoned)
      ++abandoned;
    else
      valid.push_back(&sample);
    detailed = detailed && sample.detailed;
  }
  if (detailed)
    metrics["abandoned_share"] = proportionEstimate(abandoned, samples.size());
  if (valid.empty()) return metrics;

  const std::size_t n = valid.size();
  const auto column = [&](auto extract)
  {
    std::vector<double> values;
    values.reserve(n);
    for (const MatchSample* sample : valid)
      values.push_back(static_cast<double>(extract(*sample)));
    return values;
  };
  const auto count = [&](auto predicate)
  {
    return static_cast<std::size_t>(std::ranges::count_if(
        valid, [&](const MatchSample* sample) { return predicate(*sample); }));
  };

  const std::vector<double> goals =
      column([](const MatchSample& s) { return s.totalGoals(); });
  const std::vector<double> shots =
      column([](const MatchSample& s) { return s.shots[0] + s.shots[1]; });

  // Goals per match: both teams, regulation plus added time.
  metrics["goals_per_match"] = meanEstimate(goals);
  // Home multiplier: sum of home goals / sum of away goals.
  metrics["home_goal_multiplier"] =
      ratioEstimate(column([](const MatchSample& s) { return s.goals[0]; }),
                    column([](const MatchSample& s) { return s.goals[1]; }));
  // Result shares over matches (Wilson intervals).
  metrics["home_win_share"] = proportionEstimate(
      count([](const MatchSample& s) { return s.goals[0] > s.goals[1]; }), n);
  metrics["draw_share"] = proportionEstimate(
      count([](const MatchSample& s) { return s.goals[0] == s.goals[1]; }), n);
  metrics["away_win_share"] = proportionEstimate(
      count([](const MatchSample& s) { return s.goals[0] < s.goals[1]; }), n);
  metrics["goalless_share"] = proportionEstimate(
      count([](const MatchSample& s) { return s.totalGoals() == 0; }), n);
  metrics["one_one_share"] =
      proportionEstimate(count([](const MatchSample& s)
                               { return s.goals[0] == 1 && s.goals[1] == 1; }),
                         n);
  metrics["one_nil_share"] = proportionEstimate(
      count([](const MatchSample& s) { return s.totalGoals() == 1; }), n);
  // Dispersion of total goals: sample variance / mean (Poisson = 1).
  metrics["goals_variance_mean"] = bootstrapEstimate(
      n,
      [&goals](std::span<const std::size_t> indices)
      {
        if (indices.size() < 2) return std::nan("");
        double sum = 0.0;
        for (const std::size_t i : indices) sum += goals[i];
        const double mean = sum / static_cast<double>(indices.size());
        if (mean <= 0.0) return std::nan("");
        double squares = 0.0;
        for (const std::size_t i : indices)
          squares += (goals[i] - mean) * (goals[i] - mean);
        return squares / static_cast<double>(indices.size() - 1) / mean;
      },
      0xB0075747ULL);
  // Per-team counts are per-match totals halved.
  const auto perTeam = [&column](auto extract)
  {
    return meanEstimate(
        column([extract](const MatchSample& s) { return extract(s) / 2.0; }));
  };
  metrics["shots_per_team"] =
      perTeam([](const MatchSample& s) { return s.shots[0] + s.shots[1]; });
  metrics["on_target_per_team"] = perTeam(
      [](const MatchSample& s) { return s.on_target[0] + s.on_target[1]; });
  metrics["fouls_per_team"] =
      perTeam([](const MatchSample& s) { return s.fouls[0] + s.fouls[1]; });
  metrics["offsides_per_team"] =
      perTeam([](const MatchSample& s) { return s.offsides; });
  // Goals per shot: goals credited to a shooter (own goals excluded).
  metrics["goals_per_shot"] = ratioEstimate(
      column([](const MatchSample& s) { return s.totalGoals() - s.own_goals; }),
      shots);
  metrics["on_target_share"] =
      ratioEstimate(column([](const MatchSample& s)
                           { return s.on_target[0] + s.on_target[1]; }),
                    shots);
  metrics["blocked_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.shots_blocked; }), shots);
  metrics["shot_leg_block_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.shot_leg_blocks; }),
      column([](const MatchSample& s) { return s.shot_body_blocks; }));
  metrics["shot_reaction_block_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.shot_reaction_blocks; }),
      column([](const MatchSample& s) { return s.shot_body_blocks; }));
  metrics["shot_deflections_per_shot"] = ratioEstimate(
      column([](const MatchSample& s) { return s.shot_deflections; }), shots);
  metrics["keeper_body_hits_per_shot"] = ratioEstimate(
      column([](const MatchSample& s) { return s.keeper_body_hits; }), shots);
  metrics["keeper_body_still_on_goal_per_shot"] = ratioEstimate(
      column([](const MatchSample& s) { return s.keeper_body_still_on_goal; }), shots);
  metrics["keeper_hand_hit_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.keeper_hand_hits; }),
      column([](const MatchSample& s) { return s.keeper_body_hits; }));
  const std::vector<double> handsWent =
      column([](const MatchSample& s) { return s.keeper_hands_went; });
  metrics["keeper_hands_went_per_shot"] = ratioEstimate(handsWent, shots);
  metrics["keeper_hands_together_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.keeper_hands_together; }), handsWent);
  metrics["keeper_hands_late_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.keeper_hands_late; }), handsWent);
  const std::vector<double> dives = column([](const MatchSample& s) { return s.keeper_dives; });
  metrics["keeper_dives_per_shot"] = ratioEstimate(dives, shots);
  metrics["keeper_dives_late_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.keeper_dives_late; }), dives);
  // Goalkeeper plan step 4: off his hands.
  const std::vector<double> handShots =
      column([](const MatchSample& s) { return s.keeper_hand_shots; });
  const std::vector<double> catches =
      column([](const MatchSample& s) { return s.keeper_catches; });
  metrics["keeper_hand_shots_per_shot"] = ratioEstimate(handShots, shots);
  metrics["keeper_both_hands_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.keeper_both_hands; }), handShots);
  metrics["keeper_catch_share"] = ratioEstimate(catches, handShots);
  metrics["keeper_held_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.keeper_held; }), handShots);
  metrics["keeper_fumble_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.keeper_fumbles; }), catches);
  metrics["keeper_parry_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.keeper_parries; }), handShots);
  // Goalkeeper plan step 5: the ball in his hands, and how it leaves them.
  metrics["keeper_balls_in_hands_per_match"] =
      meanEstimate(column([](const MatchSample& s) { return s.keeper_balls_in_hands; }));
  const std::vector<double> rolls = column([](const MatchSample& s) { return s.keeper_rolls; });
  const std::vector<double> throws = column([](const MatchSample& s) { return s.keeper_throws; });
  const std::vector<double> punts = column([](const MatchSample& s) { return s.keeper_punts; });
  std::vector<double> releases(rolls.size());
  for (std::size_t index = 0; index < releases.size(); ++index)
    releases[index] = rolls[index] + throws[index] + punts[index];
  metrics["keeper_roll_share"] = ratioEstimate(rolls, releases);
  metrics["keeper_throw_share"] = ratioEstimate(throws, releases);
  metrics["keeper_punt_share"] = ratioEstimate(punts, releases);
  // Goalkeeper plan step 7: high claims.
  const std::vector<double> claimsTried =
      column([](const MatchSample& s) { return s.keeper_claims_tried; });
  metrics["keeper_claims_per_match"] = meanEstimate(claimsTried);
  metrics["keeper_claim_held_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.keeper_claims_held; }), claimsTried);
  metrics["keeper_claim_punch_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.keeper_punches; }), claimsTried);
  metrics["keeper_claim_parry_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.keeper_claim_parries; }), claimsTried);
  // Goalkeeper plan step 8: smothers.
  const std::vector<double> smothers =
      column([](const MatchSample& s) { return s.keeper_smother_dives; });
  metrics["keeper_smothers_per_match"] = meanEstimate(smothers);
  metrics["keeper_smother_held_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.keeper_smothers_held; }), smothers);
  // Goalkeeper plan step 9: shootout kicks.
  const std::vector<double> shootoutKicks =
      column([](const MatchSample& s) { return s.shootout_kicks; });
  metrics["shootout_conversion"] = ratioEstimate(
      column([](const MatchSample& s) { return s.shootout_goals; }), shootoutKicks);
  metrics["shootout_save_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.shootout_saves; }), shootoutKicks);
  // Pass block plan: other balls off a body.
  const std::vector<double> ballBodyHits =
      column([](const MatchSample& s) { return s.ball_body_hits; });
  metrics["ball_body_hits_per_match"] = meanEstimate(ballBodyHits);
  metrics["ball_leg_hit_share"] =
      ratioEstimate(column([](const MatchSample& s) { return s.ball_leg_hits; }), ballBodyHits);
  metrics["ball_head_hit_share"] =
      ratioEstimate(column([](const MatchSample& s) { return s.ball_head_hits; }), ballBodyHits);
  metrics["ball_reaction_block_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.ball_reaction_blocks; }), ballBodyHits);
  metrics["passes_blocked_per_match"] =
      meanEstimate(column([](const MatchSample& s) { return s.passes_blocked; }));
  metrics["ball_deflections_per_match"] =
      meanEstimate(column([](const MatchSample& s) { return s.ball_deflections; }));
  // Direct free kicks.
  const std::vector<double> directFreeKicks =
      column([](const MatchSample& s) { return s.direct_free_kicks; });
  metrics["direct_free_kicks_per_match"] = meanEstimate(directFreeKicks);
  metrics["direct_free_kick_on_target_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.direct_free_kicks_on_target; }),
      directFreeKicks);
  metrics["direct_free_kick_blocked_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.direct_free_kicks_blocked; }), directFreeKicks);
  metrics["direct_free_kick_goal_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.direct_free_kick_goals; }), directFreeKicks);
  metrics["shot_torso_pass_throughs_per_shot"] = ratioEstimate(
      column([](const MatchSample& s) { return s.shot_torso_pass_throughs; }), shots);
  metrics["shot_torso_inside_at_start_per_shot"] = ratioEstimate(
      column([](const MatchSample& s) { return s.shot_torso_inside_at_start; }), shots);
  // Shot block plan §3d: defenders a shot's line passed within reach of,
  // and what came of each.
  const std::vector<double> legChances =
      column([](const MatchSample& s) { return s.shot_leg_chances; });
  metrics["shot_leg_chances_per_shot"] = ratioEstimate(legChances, shots);
  metrics["shot_leg_too_late_share"] = ratioEstimate(
      column(
          [](const MatchSample& s)
          {
            int reacted = 0;
            for (const int count : s.shot_leg_outcomes) reacted += count;
            return s.shot_leg_chances - reacted;
          }),
      legChances);
  static constexpr std::array<const char*, static_cast<std::size_t>(ShotLegOutcome::COUNT)>
      LEG_OUTCOME_METRICS{"shot_leg_out_share",          "shot_leg_feet_busy_share",
                          "shot_leg_chose_not_share",    "shot_leg_not_travelling_share",
                          "shot_leg_past_share",         "shot_leg_above_hip_share",
                          "shot_leg_reach_height_share", "shot_leg_reach_running_share",
                          "shot_leg_behind_share"};
  for (std::size_t index = 0; index < LEG_OUTCOME_METRICS.size(); ++index)
    metrics[LEG_OUTCOME_METRICS[index]] = ratioEstimate(
        column([index](const MatchSample& s) { return s.shot_leg_outcomes[index]; }), legChances);
  metrics["shot_leg_out_blocks_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.shot_reaction_blocks; }),
      column([](const MatchSample& s)
             { return s.shot_leg_outcomes[static_cast<std::size_t>(ShotLegOutcome::LEG_OUT)]; }));
  // Mean xG of the engine's own shot model per shot.
  metrics["xg_per_shot"] = ratioEstimate(
      column([](const MatchSample& s) { return s.xg[0] + s.xg[1]; }), shots);
  const std::vector<double> attempted =
      column([](const MatchSample& s)
             { return s.passes_attempted[0] + s.passes_attempted[1]; });
  metrics["passes_per_match"] = meanEstimate(attempted);
  metrics["pass_completion"] = ratioEstimate(
      column([](const MatchSample& s)
             { return s.passes_completed[0] + s.passes_completed[1]; }),
      attempted);
  metrics["corners_per_match"] =
      meanEstimate(column([](const MatchSample& s) { return s.corners; }));
  metrics["yellows_per_match"] =
      meanEstimate(column([](const MatchSample& s) { return s.yellows; }));
  metrics["reds_per_match"] =
      meanEstimate(column([](const MatchSample& s) { return s.reds; }));
  metrics["late_goal_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.late_goals; }), goals);
  metrics["own_goal_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.own_goals; }), goals);
  // Possession spread: mean absolute distance of home possession from 50%.
  metrics["possession_spread"] = meanEstimate(column(
      [](const MatchSample& s)
      { return std::abs(static_cast<double>(s.home_possession) - 50.0); }));

  if (!detailed) return metrics;

  metrics["inside_box_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.inside_box; }), shots);
  // Non-penalty set-piece goals: the engine flags penalties as set pieces.
  metrics["set_piece_goal_share"] =
      ratioEstimate(column([](const MatchSample& s)
                           { return s.set_piece_goals - s.penalty_goals; }),
                    goals);
  metrics["headed_goal_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.headed_goals; }), goals);
  const std::vector<double> penalties =
      column([](const MatchSample& s) { return s.penalties; });
  metrics["penalties_per_match"] = meanEstimate(penalties);
  metrics["throw_ins_per_match"] =
      meanEstimate(column([](const MatchSample& s) { return s.throw_ins; }));
  metrics["goal_kicks_per_match"] =
      meanEstimate(column([](const MatchSample& s) { return s.goal_kicks; }));
  metrics["free_kicks_per_match"] =
      meanEstimate(column([](const MatchSample& s) { return s.free_kicks; }));
  metrics["event_log_full_share"] = meanEstimate(
      column([](const MatchSample& s) { return s.event_log_full ? 1 : 0; }));
  metrics["penalty_conversion"] = ratioEstimate(
      column([](const MatchSample& s) { return s.penalty_goals; }), penalties);
  const std::vector<double> substitutions =
      column([](const MatchSample& s) { return s.substitutions; });
  metrics["subs_per_team"] =
      perTeam([](const MatchSample& s) { return s.substitutions; });
  // Mean clock minute of a substitution (second half runs 45-90+).
  metrics["sub_mean_minute"] = ratioEstimate(
      column([](const MatchSample& s) { return s.substitution_minute_sum; }),
      substitutions);
  metrics["injuries_per_team"] =
      perTeam([](const MatchSample& s) { return s.injuries; });
  metrics["ball_in_play_minutes"] =
      meanEstimate(column([](const MatchSample& s) { return s.ball_in_play; }));
  metrics["match_length_minutes"] =
      meanEstimate(column([](const MatchSample& s) { return s.match_length; }));
  metrics["added_time_minutes"] = meanEstimate(
      column([](const MatchSample& s) { return s.added_minutes; }));
  // Distance: full-match outfield starters, km per player.
  const std::vector<double> fullMatchPlayers =
      column([](const MatchSample& s) { return s.full_match_players; });
  metrics["distance_per_outfielder_km"] = ratioEstimate(
      column([](const MatchSample& s)
             { return s.full_match_distance / 1000.0; }),
      fullMatchPlayers);
  // Player behaviour: what the brain decides shows here first.
  const std::vector<double> tackles =
      column([](const MatchSample& s) { return s.tackles_attempted; });
  metrics["tackles_per_team"] =
      perTeam([](const MatchSample& s) { return s.tackles_attempted; });
  metrics["tackle_success"] = ratioEstimate(
      column([](const MatchSample& s) { return s.tackles_won; }), tackles);
  metrics["interceptions_per_team"] =
      perTeam([](const MatchSample& s) { return s.interceptions; });
  metrics["clearances_per_team"] =
      perTeam([](const MatchSample& s) { return s.clearances; });
  metrics["aerial_duels_per_match"] =
      meanEstimate(column([](const MatchSample& s) { return s.aerial_duels; }));
  // Header sequences (aerial challenge plan step 4).
  const std::vector<double> sequences =
      column([](const MatchSample& s) { return s.header_sequences; });
  metrics["header_sequences_per_match"] = meanEstimate(sequences);
  metrics["headers_per_sequence"] = ratioEstimate(
      column([](const MatchSample& s) { return s.header_sequence_headers; }), sequences);
  metrics["head_tennis_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.header_sequences_long; }), sequences);
  metrics["longest_header_sequence"] = meanEstimate(
      column([](const MatchSample& s) { return s.header_sequence_longest; }));
  const std::vector<double> toControl =
      column([](const MatchSample& s) { return s.header_sequences_to_control; });
  metrics["header_sequences_to_control"] = ratioEstimate(toControl, sequences);
  metrics["header_sequences_kept"] = ratioEstimate(
      column([](const MatchSample& s) { return s.header_sequences_kept; }), toControl);
  metrics["pressures_per_team"] =
      perTeam([](const MatchSample& s) { return s.pressures; });
  metrics["key_passes_per_team"] =
      perTeam([](const MatchSample& s) { return s.key_passes; });
  metrics["saves_per_team"] =
      perTeam([](const MatchSample& s) { return s.saves; });
  metrics["touches_per_team"] =
      perTeam([](const MatchSample& s) { return s.touches; });
  // Contests by side: with different bodies home and away, each side's
  // success rate shows how its body fares against the other's.
  const auto bySide = [&](const char* name, auto successes, auto attempts)
  {
    for (const std::size_t side : {std::size_t{0}, std::size_t{1}})
      metrics[std::string(name) + (side == 0 ? "_home" : "_away")] =
          ratioEstimate(
              column([&](const MatchSample& s) { return successes(s)[side]; }),
              column([&](const MatchSample& s) { return attempts(s)[side]; }));
  };
  metrics["take_ons_per_team"] = perTeam(
      [](const MatchSample& s) { return s.take_ons[0] + s.take_ons[1]; });
  bySide(
      "take_on_success",
      [](const MatchSample& s) { return s.take_ons_won; },
      [](const MatchSample& s) { return s.take_ons; });
  bySide(
      "tackle_duel_success",
      [](const MatchSample& s) { return s.tackle_duels_won; },
      [](const MatchSample& s) { return s.tackle_duels; });
  metrics["free_ball_tackles_per_team"] = perTeam(
      [](const MatchSample& s)
      { return s.free_ball_tackles[0] + s.free_ball_tackles[1]; });
  metrics["contest_tackles_per_team"] =
      perTeam([](const MatchSample& s) { return s.contest_tackles; });
  metrics["contest_tackle_success"] = ratioEstimate(
      column([](const MatchSample& s) { return s.contest_tackles_won; }),
      column([](const MatchSample& s) { return s.contest_tackles; }));
  metrics["carrier_air_swings_per_team"] =
      perTeam([](const MatchSample& s) { return s.carrier_air_swings; });
  metrics["tackler_air_swings_per_team"] =
      perTeam([](const MatchSample& s) { return s.tackler_air_swings; });
  metrics["commit_misses_per_team"] =
      perTeam([](const MatchSample& s) { return s.commit_misses; });
  const std::vector<double> blocks =
      column([](const MatchSample& s) { return s.blocks; });
  metrics["blocks_per_team"] = perTeam([](const MatchSample& s) { return s.blocks; });
  metrics["blocks_committed_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.blocks_committed; }), blocks);
  metrics["go_ins_per_team"] = perTeam([](const MatchSample& s) { return s.go_ins; });
  metrics["go_ins_free_foot_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.go_ins_free_foot; }),
      column([](const MatchSample& s) { return s.go_ins; }));
  metrics["holds_per_team"] = perTeam([](const MatchSample& s) { return s.holds; });
  metrics["intents_win_per_team"] = perTeam([](const MatchSample& s) { return s.intents_win; });
  metrics["intents_clear_per_team"] = perTeam([](const MatchSample& s) { return s.intents_clear; });
  metrics["intents_control_per_team"] = perTeam([](const MatchSample& s) { return s.intents_control; });
  metrics["collisions_per_match"] = meanEstimate(
      column([](const MatchSample& s) { return static_cast<double>(s.collisions); }));
  metrics["collisions_jolt_two_per_match"] = meanEstimate(column(
      [](const MatchSample& s) { return static_cast<double>(s.collisions_jolt_two); }));
  metrics["collisions_jolt_four_per_match"] = meanEstimate(column(
      [](const MatchSample& s) { return static_cast<double>(s.collisions_jolt_four); }));
  metrics["collision_stumbles_per_match"] = meanEstimate(column(
      [](const MatchSample& s) { return static_cast<double>(s.collision_stumbles); }));
  metrics["collision_falls_per_match"] = meanEstimate(column(
      [](const MatchSample& s) { return static_cast<double>(s.collision_falls); }));
  metrics["collision_slips_per_match"] = meanEstimate(column(
      [](const MatchSample& s) { return static_cast<double>(s.collision_slips); }));
  const std::vector<double> knocks = column([](const MatchSample& s)
                                            { return static_cast<double>(s.collision_stumbles + s.collision_falls); });
  metrics["collision_knocks_by_ends_up_share"] = ratioEstimate(
      column([](const MatchSample& s) { return static_cast<double>(s.collision_knocks_by_ends_up); }),
      knocks);
  metrics["collision_knocks_small_jolt_share"] = ratioEstimate(
      column([](const MatchSample& s) { return static_cast<double>(s.collision_knocks_small_jolt); }),
      knocks);
  const auto perMatch = [&](const char* name, auto field)
  { metrics[name] = meanEstimate(column([field](const MatchSample& s) { return static_cast<double>(s.*field); })); };
  perMatch("collision_big_hits_per_match", &MatchSample::collision_big_hits);
  perMatch("collision_falls_losing_ball_per_match", &MatchSample::collision_falls_losing_ball);
  perMatch("collision_down_play_seconds_per_match", &MatchSample::collision_down_play_seconds);
  perMatch("collision_get_ups_for_ball_per_match", &MatchSample::collision_get_ups_for_ball);
  perMatch("collision_get_ups_in_stoppage_per_match", &MatchSample::collision_get_ups_in_stoppage);
  perMatch("collision_get_ups_gingerly_per_match", &MatchSample::collision_get_ups_gingerly);
  perMatch("collision_get_ups_fixed_per_match", &MatchSample::collision_get_ups_fixed);
  perMatch("collision_downs_fifteen_per_match", &MatchSample::collision_downs_fifteen);
  perMatch("collision_ball_off_fallen_per_match", &MatchSample::collision_ball_off_fallen);
  perMatch("give_way_slows_per_match", &MatchSample::give_way_slows);
  perMatch("give_way_rounds_per_match", &MatchSample::give_way_rounds);
  perMatch("give_way_holds_per_match", &MatchSample::give_way_holds);
  for (std::size_t kind = 0; kind < 4; ++kind)
  {
    static constexpr std::array<const char*, 4> KINDS{"chase", "hold", "support", "recover"};
    metrics[std::format("way_rounds_started_{}_per_match", KINDS[kind])] = meanEstimate(
        column([kind](const MatchSample& s) { return static_cast<double>(s.way_rounds_started[kind]); }));
    metrics[std::format("way_round_refreshes_{}_per_match", KINDS[kind])] = meanEstimate(
        column([kind](const MatchSample& s) { return static_cast<double>(s.way_round_refreshes[kind]); }));
  }
  for (std::size_t index = 0; index < 8; ++index)
    metrics[std::format("collision_knocks_situation_{}_share", index)] = ratioEstimate(
        column([index](const MatchSample& s)
               { return static_cast<double>(s.collision_knocks_by_situation[index]); }),
        knocks);
  {
    // Knocks with a loose-ball chaser in them: what the other was doing.
    const std::vector<double> chaseKnocks = column([](const MatchSample& s)
    {
      double total = 0.0;
      for (const int count : s.collision_chase_knocks_with) total += count;
      return total;
    });
    metrics["collision_chase_knocks_share"] = ratioEstimate(chaseKnocks, knocks);
    metrics["collision_chase_knocks_on_chaser_share"] = ratioEstimate(
        column([](const MatchSample& s)
               { return static_cast<double>(s.collision_chase_knocks_on_chaser); }),
        chaseKnocks);
    for (std::size_t index = 0; index <= PLAYER_INTENT_COUNT; ++index)
      metrics[std::format("collision_chase_knocks_with_{}_share",
                          index == PLAYER_INTENT_COUNT
                              ? std::string_view("team_mate_chasing")
                              : MatchEngineInternal::intentName(static_cast<PlayerIntent>(index)))] =
          ratioEstimate(column([index](const MatchSample& s)
                               { return static_cast<double>(s.collision_chase_knocks_with[index]); }),
                        chaseKnocks);
  }
  for (std::size_t index = 0; index < PLAYER_INTENT_COUNT; ++index)
    metrics[std::format("collision_knocks_intent_{}_share",
                        MatchEngineInternal::intentName(static_cast<PlayerIntent>(index)))] =
        ratioEstimate(column([index](const MatchSample& s)
                             { return static_cast<double>(s.collision_knocks_by_intent[index]); }),
                      knocks);
  metrics["collision_down_seconds_per_fall"] = ratioEstimate(
      column([](const MatchSample& s) { return s.collision_down_seconds; }),
      column([](const MatchSample& s) { return static_cast<double>(s.collision_falls); }));
  metrics["blocks_popped_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.blocks_popped; }), blocks);
  metrics["free_ball_tackle_success"] = ratioEstimate(
      column([](const MatchSample& s)
             { return s.free_ball_tackles_won[0] + s.free_ball_tackles_won[1]; }),
      column([](const MatchSample& s)
             { return s.free_ball_tackles[0] + s.free_ball_tackles[1]; }));
  metrics["aerial_home_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.aerial_home_won; }),
      column([](const MatchSample& s) { return s.aerial_contested; }));
  bySide(
      "keeper_claim_success",
      [](const MatchSample& s) { return s.keeper_claims; },
      [](const MatchSample& s) { return s.keeper_claim_attempts; });
  metrics["high_intensity_per_outfielder_km"] = ratioEstimate(
      column([](const MatchSample& s)
             { return s.full_match_high_intensity / 1000.0; }),
      fullMatchPlayers);
  metrics["sprint_per_outfielder_km"] = ratioEstimate(
      column([](const MatchSample& s) { return s.full_match_sprint / 1000.0; }),
      fullMatchPlayers);
  metrics["sprints_per_outfielder"] = ratioEstimate(
      column([](const MatchSample& s) { return s.full_match_sprints; }),
      fullMatchPlayers);
  metrics["walk_per_outfielder_km"] = ratioEstimate(
      column([](const MatchSample& s) { return s.full_match_walk / 1000.0; }),
      fullMatchPlayers);
  metrics["jog_per_outfielder_km"] = ratioEstimate(
      column([](const MatchSample& s) { return s.full_match_jog / 1000.0; }),
      fullMatchPlayers);
  metrics["run_per_outfielder_km"] = ratioEstimate(
      column([](const MatchSample& s) { return s.full_match_run / 1000.0; }),
      fullMatchPlayers);
  metrics["hsr_per_outfielder_km"] = ratioEstimate(
      column([](const MatchSample& s)
             { return (s.full_match_high_intensity - s.full_match_sprint) /
                      1000.0; }),
      fullMatchPlayers);
  metrics["hir_per_outfielder_km"] = ratioEstimate(
      column([](const MatchSample& s)
             { return (s.full_match_run + s.full_match_high_intensity) /
                      1000.0; }),
      fullMatchPlayers);
  metrics["energy_per_outfielder_kj"] = ratioEstimate(
      column([](const MatchSample& s) { return s.full_match_energy; }),
      fullMatchPlayers);
  metrics["anaerobic_energy_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.full_match_anaerobic; }),
      column([](const MatchSample& s) { return s.full_match_energy; }));
  // Time and distance in each movement mode, by each player's own bands.
  const std::vector<double> modeTime = column(
      [](const MatchSample& s)
      {
        double total = 0.0;
        for (const double seconds : s.mode_seconds) total += seconds;
        return total;
      });
  for (std::size_t mode = 0; mode < MOVEMENT_MODE_COUNT; ++mode)
  {
    const std::string name = movementModeName(static_cast<MovementMode>(mode));
    metrics["mode_" + name + "_time_share"] = ratioEstimate(
        column([mode](const MatchSample& s) { return s.mode_seconds[mode]; }),
        modeTime);
    metrics["mode_" + name + "_km"] = ratioEstimate(
        column([mode](const MatchSample& s)
               { return s.mode_metres[mode] / 1000.0; }),
        fullMatchPlayers);
  }
  metrics["braking_energy_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.full_match_braking_energy; }),
      column([](const MatchSample& s) { return s.full_match_energy; }));
  metrics["braking_anaerobic_share"] = ratioEstimate(
      column([](const MatchSample& s)
             { return s.full_match_braking_anaerobic; }),
      column([](const MatchSample& s) { return s.full_match_anaerobic; }));
  metrics["emptied_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.emptied_players; }),
      fullMatchPlayers);
  metrics["empty_minute_mean"] = ratioEstimate(
      column([](const MatchSample& s) { return s.empty_minute_sum; }),
      column([](const MatchSample& s) { return s.emptied_players; }));
  metrics["peak5_next_change"] = ratioEstimate(
      column([](const MatchSample& s) { return s.peak_next_change_sum; }),
      column([](const MatchSample& s) { return s.peak_next_players; }));
  // Movement jitter: the plan's scheduled refreshes, summed over intents.
  const auto planned = [&](auto field)
  {
    return column(
        [field](const MatchSample& s)
        {
          double sum = 0.0;
          for (const auto& bucket : s.jitter.byIntent)
            sum += static_cast<double>(bucket.*field);
          return sum;
        });
  };
  using Bucket = MovementJitterStats::ByIntent;
  const std::vector<double> refreshes = planned(&Bucket::refreshes);
  const std::vector<double> targetMoves = planned(&Bucket::moves);
  const std::vector<double> switches = planned(&Bucket::switchesIn);
  const std::vector<double> playerMinutes = column(
      [](const MatchSample& s) { return s.jitter.playerSeconds / 60.0; });
  metrics["jitter_target_move_m"] =
      ratioEstimate(planned(&Bucket::targetMoveMetres), refreshes);
  metrics["jitter_moving_share"] = ratioEstimate(targetMoves, refreshes);
  metrics["jitter_reversal_share"] =
      ratioEstimate(planned(&Bucket::reversals), targetMoves);
  metrics["jitter_reversals_per_minute"] =
      ratioEstimate(planned(&Bucket::reversals), playerMinutes);
  metrics["jitter_switches_per_minute"] =
      ratioEstimate(switches, playerMinutes);
  metrics["jitter_flip_back_share"] =
      ratioEstimate(planned(&Bucket::flipBacks), switches);
  metrics["jitter_event_refreshes_per_minute"] = ratioEstimate(
      column([](const MatchSample& s)
             { return static_cast<double>(s.jitter.eventRefreshes); }),
      playerMinutes);
  metrics["jitter_event_target_move_m"] = ratioEstimate(
      column([](const MatchSample& s)
             { return s.jitter.eventTargetMoveMetres; }),
      column([](const MatchSample& s)
             { return static_cast<double>(s.jitter.eventRefreshes); }));
  metrics["jitter_run_efficiency"] = ratioEstimate(
      column([](const MatchSample& s) { return s.jitter.netMetres; }),
      column([](const MatchSample& s) { return s.jitter.pathMetres; }));
  metrics["jitter_sharp_turn_share"] = ratioEstimate(
      column([](const MatchSample& s)
             { return static_cast<double>(s.jitter.sharpTurns); }),
      column([](const MatchSample& s)
             { return static_cast<double>(s.jitter.turnSamples); }));
  metrics["jitter_speed_reversals_per_running_minute"] = ratioEstimate(
      column([](const MatchSample& s)
             { return static_cast<double>(s.jitter.speedReversals); }),
      column([](const MatchSample& s)
             { return s.jitter.runningSeconds / 60.0; }));
  // Second-half vs first-half distance per minute of those players, pooled
  // over matches (distance / (half length x players)); bootstrap.
  metrics["second_half_distance_change"] = bootstrapEstimate(
      n,
      [&valid](std::span<const std::size_t> indices)
      {
        double first = 0.0;
        double second = 0.0;
        double firstTime = 0.0;
        double secondTime = 0.0;
        for (const std::size_t i : indices)
        {
          const MatchSample& s = *valid[i];
          const auto players = static_cast<double>(s.full_match_players);
          first += s.first_half_distance;
          second += s.second_half_distance;
          firstTime += s.first_half_minutes * players;
          secondTime += s.second_half_minutes * players;
        }
        if (first <= 0.0 || firstTime <= 0.0 || secondTime <= 0.0)
          return std::nan("");
        return (second / secondTime) / (first / firstTime) - 1.0;
      },
      0xD157ULL);

  // Last vs first 15 minutes (clock 75-90+ vs 0-15) per outfield minute on
  // the pitch, pooled over matches; bootstrap.
  const auto lateChange = [&](bool highIntensity, std::uint64_t seed)
  {
    constexpr std::size_t FIRST = 0;
    constexpr std::size_t LAST = LoadProfileStats::SPELLS - 1;
    return bootstrapEstimate(
        n,
        [&, highIntensity](std::span<const std::size_t> indices)
        {
          double first = 0.0, last = 0.0, firstTime = 0.0, lastTime = 0.0;
          for (const std::size_t i : indices)
          {
            const LoadProfileStats& load = valid[i]->load;
            const auto& metres =
                highIntensity ? load.highIntensityMetres : load.metres;
            first += metres[FIRST];
            last += metres[LAST];
            firstTime += load.playerSeconds[FIRST];
            lastTime += load.playerSeconds[LAST];
          }
          if (first <= 0.0 || firstTime <= 0.0 || lastTime <= 0.0)
            return std::nan("");
          return (last / lastTime) / (first / firstTime) - 1.0;
        },
        seed);
  };
  metrics["late_distance_change"] = lateChange(false, 0x15A1ULL);
  metrics["late_high_intensity_change"] = lateChange(true, 0x15A2ULL);
  // Fuel (condition) left at full time by the outfield players on the pitch.
  const std::vector<double> finishers =
      column([](const MatchSample& s) { return s.finishers; });
  metrics["end_fuel_mean"] = ratioEstimate(
      column([](const MatchSample& s) { return s.end_fuel_sum; }), finishers);
  metrics["end_fuel_floor_share"] = ratioEstimate(
      column([](const MatchSample& s) { return s.finishers_at_floor; }),
      finishers);
  // Its spread over all finishers, pooled; bootstrap.
  metrics["end_fuel_sd"] = bootstrapEstimate(
      n,
      [&](std::span<const std::size_t> indices)
      {
        double count = 0.0, sum = 0.0, squares = 0.0;
        for (const std::size_t i : indices)
        {
          count += valid[i]->finishers;
          sum += valid[i]->end_fuel_sum;
          squares += valid[i]->end_fuel_square_sum;
        }
        if (count < 2.0) return std::nan("");
        const double mean = sum / count;
        return std::sqrt(std::max(0.0, squares / count - mean * mean));
      },
      0xF0E1ULL);
  metrics["end_fuel_min_mean"] = ratioEstimate(
      column([](const MatchSample& s) { return s.end_fuel_min; }),
      column([](const MatchSample&) { return 1; }));
  constexpr std::array<const char*, MatchSample::FUEL_BANDS> FUEL_BAND_NAMES{
      "end_fuel_60_share", "end_fuel_30_60_share", "end_fuel_10_30_share",
      "end_fuel_below_10_share", "end_fuel_empty_share"};
  for (std::size_t band = 0; band < MatchSample::FUEL_BANDS; ++band)
    metrics[FUEL_BAND_NAMES[band]] = ratioEstimate(
        column([band](const MatchSample& s) { return s.end_fuel_bands[band]; }),
        finishers);

  // Rate multipliers: goals per team-minute in a state / in the reference
  // state, pooled over matches; bootstrap over matches.
  const auto rateMultiplier = [&](auto minutes, auto goalsOf, std::size_t state,
                                  std::size_t reference, std::uint64_t seed)
  {
    return bootstrapEstimate(
        n,
        [&valid, minutes, goalsOf, state,
         reference](std::span<const std::size_t> indices)
        {
          double stateMinutes = 0.0;
          double referenceMinutes = 0.0;
          double stateGoals = 0.0;
          double referenceGoals = 0.0;
          for (const std::size_t i : indices)
          {
            stateMinutes += minutes(*valid[i])[state];
            referenceMinutes += minutes(*valid[i])[reference];
            stateGoals += goalsOf(*valid[i])[state];
            referenceGoals += goalsOf(*valid[i])[reference];
          }
          if (stateMinutes <= 0.0 || referenceMinutes <= 0.0 ||
              referenceGoals <= 0.0)
            return std::nan("");
          return (stateGoals / stateMinutes) /
                 (referenceGoals / referenceMinutes);
        },
        seed);
  };
  const auto stateMinutes = [](const MatchSample& s) -> const auto&
  { return s.state_minutes; };
  const auto stateGoals = [](const MatchSample& s) -> const auto&
  { return s.state_goals; };
  const auto strengthMinutes = [](const MatchSample& s) -> const auto&
  { return s.strength_minutes; };
  const auto strengthGoals = [](const MatchSample& s) -> const auto&
  { return s.strength_goals; };
  metrics["lead1_goal_multiplier"] =
      rateMultiplier(stateMinutes, stateGoals, LEAD_BY_ONE, LEVEL, 0x1EAD1ULL);
  metrics["trail1_goal_multiplier"] =
      rateMultiplier(stateMinutes, stateGoals, TRAIL_BY_ONE, LEVEL, 0x7EA11ULL);
  metrics["red_penalised_multiplier"] = rateMultiplier(
      strengthMinutes, strengthGoals, MAN_DOWN, EVEN, 0x4EDD0ULL);
  metrics["red_opponent_multiplier"] =
      rateMultiplier(strengthMinutes, strengthGoals, MAN_UP, EVEN, 0x4EDA1ULL);

  // NFR-019: the stronger side (higher starter rating) wins; exact rating
  // ties have no stronger side and are left out.
  static constexpr std::array<std::pair<const char*, std::array<double, 2>>, 4>
      GAP_BANDS = {{{"stronger_win_gap_lt3", {0.0, 3.0}},
                    {"stronger_win_gap_3_8", {3.0, 8.0}},
                    {"stronger_win_gap_8_15", {8.0, 15.0}},
                    {"stronger_win_gap_gt15", {15.0, 1.0e9}}}};
  for (const auto& [key, band] : GAP_BANDS)
  {
    std::size_t matches = 0;
    std::size_t wins = 0;
    for (const MatchSample* sample : valid)
    {
      if (const double gap = std::abs(static_cast<double>(sample->rating[0]) -
                                      static_cast<double>(sample->rating[1]));
          gap < RATING_TIE || gap < band[0] || gap >= band[1])
        continue;
      ++matches;
      const std::size_t strong = sample->rating[0] > sample->rating[1] ? 0 : 1;
      if (sample->goals[strong] > sample->goals[1 - strong]) ++wins;
    }
    if (matches > 0) metrics[key] = proportionEstimate(wins, matches);
  }
  return metrics;
}

std::vector<TextTable> matchTables(std::span<const MatchSample> samples)
{
  std::vector<const MatchSample*> valid;
  for (const MatchSample& sample : samples)
    if (!sample.abandoned) valid.push_back(&sample);
  std::vector<TextTable> tables;
  if (valid.empty()) return tables;
  const std::size_t n = valid.size();

  TextTable histogram{"Total goals per match",
                      {"Goals", "Matches", "Share", "Poisson(2.83) reference"},
                      {}};
  std::array<std::size_t, HISTOGRAM_GOALS> totals{};
  for (const MatchSample* sample : valid)
    ++totals[static_cast<std::size_t>(
        std::min(sample->totalGoals(), HISTOGRAM_GOALS - 1))];
  double poissonTerm = std::exp(-REFERENCE_GOALS);
  double poissonUsed = 0.0;
  for (int goals = 0; goals < HISTOGRAM_GOALS; ++goals)
  {
    const bool last = goals == HISTOGRAM_GOALS - 1;
    const double reference = last ? 1.0 - poissonUsed : poissonTerm;
    poissonUsed += poissonTerm;
    poissonTerm *= REFERENCE_GOALS / (goals + 1);
    histogram.rows.push_back(
        {last ? std::format("{}+", goals) : std::to_string(goals),
         std::to_string(totals[static_cast<std::size_t>(goals)]),
         percent(share(totals[static_cast<std::size_t>(goals)], n)),
         percent(reference)});
  }
  tables.push_back(std::move(histogram));

  std::map<std::pair<int, int>, std::size_t> scorelines;
  for (const MatchSample* sample : valid)
    ++scorelines[{sample->goals[0], sample->goals[1]}];
  std::vector<std::pair<std::pair<int, int>, std::size_t>> sorted(
      scorelines.begin(), scorelines.end());
  std::ranges::stable_sort(sorted, [](const auto& left, const auto& right)
                           { return left.second > right.second; });
  TextTable top{
      "Top scorelines (home-away)", {"Score", "Matches", "Share"}, {}};
  for (std::size_t i = 0;
       i < sorted.size() && i < static_cast<std::size_t>(TOP_SCORELINES); ++i)
    top.rows.push_back(
        {std::format("{}-{}", sorted[i].first.first, sorted[i].first.second),
         std::to_string(sorted[i].second),
         percent(share(sorted[i].second, n))});
  tables.push_back(std::move(top));

  static constexpr std::array<const char*, GOAL_TIME_BINS> BIN_NAMES = {
      "0-15", "16-30", "31-45+", "46-60", "61-75", "76-90+"};
  std::array<std::size_t, GOAL_TIME_BINS> bins{};
  std::size_t goals = 0;
  for (const MatchSample* sample : valid)
    for (std::size_t bin = 0; bin < GOAL_TIME_BINS; ++bin)
    {
      bins[bin] += static_cast<std::size_t>(sample->goal_bins[bin]);
      goals += static_cast<std::size_t>(sample->goal_bins[bin]);
    }
  TextTable timing{"Goal timing", {"Minutes", "Goals", "Share"}, {}};
  for (std::size_t bin = 0; bin < GOAL_TIME_BINS; ++bin)
    timing.rows.push_back({BIN_NAMES[bin], std::to_string(bins[bin]),
                           percent(share(bins[bin], goals))});
  tables.push_back(std::move(timing));

  // Movement jitter by the intent planned (scheduled refreshes).
  MovementJitterStats jitter;
  std::uint64_t allRefreshes = 0;
  for (const MatchSample* sample : valid)
    for (std::size_t intent = 0; intent < PLAYER_INTENT_COUNT; ++intent)
    {
      const auto& from = sample->jitter.byIntent[intent];
      auto& to = jitter.byIntent[intent];
      to.refreshes += from.refreshes;
      to.targetMoveMetres += from.targetMoveMetres;
      to.moves += from.moves;
      to.reversals += from.reversals;
      to.switchesIn += from.switchesIn;
      to.flipBacks += from.flipBacks;
      to.runningSeconds += from.runningSeconds;
      to.speedReversals += from.speedReversals;
      allRefreshes += from.refreshes;
    }
  double allRunning = 0.0;
  for (const auto& bucket : jitter.byIntent) allRunning += bucket.runningSeconds;
  TextTable byIntent{"Movement jitter by intent (scheduled refreshes)",
                     {"Intent", "Share of refreshes", "Target move (m)",
                      "Moves > 0.5 m", "Moves reversed",
                      "Switches in per 100", "Switched back within 1 s",
                      "Share of running", "Speed surges per running min"},
                     {}};
  for (std::size_t intent = 0; intent < PLAYER_INTENT_COUNT; ++intent)
  {
    const auto& bucket = jitter.byIntent[intent];
    if (bucket.refreshes == 0) continue;
    const double count = static_cast<double>(bucket.refreshes);
    byIntent.rows.push_back(
        {std::string(MatchEngineInternal::intentName(
             static_cast<PlayerIntent>(intent))),
         percent(share(bucket.refreshes, allRefreshes)),
         std::format("{:.2f}", bucket.targetMoveMetres / count),
         percent(share(bucket.moves, bucket.refreshes)),
         percent(share(bucket.reversals, bucket.moves)),
         std::format("{:.1f}", 100.0 * bucket.switchesIn / count),
         percent(share(bucket.flipBacks, bucket.switchesIn)),
         percent(allRunning > 0.0 ? bucket.runningSeconds / allRunning : 0.0),
         std::format("{:.1f}", bucket.runningSeconds > 0.0
                                   ? 60.0 * bucket.speedReversals /
                                         bucket.runningSeconds
                                   : 0.0)});
  }
  tables.push_back(std::move(byIntent));

  // Stumbles and falls by what each player was doing: the knocked player's
  // intent down the side, the other's across (per match).
  {
    static constexpr std::array<const char*, PLAYER_INTENT_COUNT> SHORT{
        "hold", "carry", "support", "receive", "behind", "box", "overlap",
        "press", "cover", "lane", "mark", "claim", "recover", "keeper"};
    std::array<std::array<double, PLAYER_INTENT_COUNT>, PLAYER_INTENT_COUNT> grid{};
    for (const MatchSample* sample : valid)
      for (std::size_t knocked = 0; knocked < PLAYER_INTENT_COUNT; ++knocked)
        for (std::size_t other = 0; other < PLAYER_INTENT_COUNT; ++other)
          grid[knocked][other] += sample->collision_knocks_by_intent_pair[knocked][other];
    const double matches = std::max<double>(1.0, static_cast<double>(valid.size()));
    std::array<bool, PLAYER_INTENT_COUNT> rowUsed{};
    std::array<bool, PLAYER_INTENT_COUNT> columnUsed{};
    for (std::size_t knocked = 0; knocked < PLAYER_INTENT_COUNT; ++knocked)
      for (std::size_t other = 0; other < PLAYER_INTENT_COUNT; ++other)
        if (grid[knocked][other] > 0.0)
        {
          rowUsed[knocked] = true;
          columnUsed[other] = true;
        }
    std::vector<std::string> header{"Knocked \\ other"};
    for (std::size_t other = 0; other < PLAYER_INTENT_COUNT; ++other)
      if (columnUsed[other]) header.emplace_back(SHORT[other]);
    header.emplace_back("all");
    TextTable pairs{"Stumbles and falls per match: what the knocked player (down) and "
                    "the other (across) were doing",
                    header,
                    {}};
    for (std::size_t knocked = 0; knocked < PLAYER_INTENT_COUNT; ++knocked)
    {
      if (!rowUsed[knocked]) continue;
      std::vector<std::string> row{SHORT[knocked]};
      double total = 0.0;
      for (std::size_t other = 0; other < PLAYER_INTENT_COUNT; ++other)
      {
        if (!columnUsed[other]) continue;
        const double perMatch = grid[knocked][other] / matches;
        total += perMatch;
        row.push_back(perMatch >= 0.05 ? std::format("{:.1f}", perMatch) : std::string("."));
      }
      row.push_back(std::format("{:.1f}", total));
      pairs.rows.push_back(std::move(row));
    }
    tables.push_back(std::move(pairs));
  }

  // Two players getting into position (holding shape, offering support,
  // recovering shape) knocking one over: why the way round did not stop it,
  // how fast each was going, and how they were moving (per match).
  {
    std::array<std::array<double, 2>, 6> reasons{};
    std::array<std::array<double, 4>, 4> speeds{};
    std::array<double, 4> motions{};
    for (const MatchSample* sample : valid)
    {
      for (std::size_t reason = 0; reason < 6; ++reason)
        for (std::size_t team = 0; team < 2; ++team)
          reasons[reason][team] += sample->positioning_knocks_by_reason[reason][team];
      for (std::size_t mine = 0; mine < 4; ++mine)
        for (std::size_t theirs = 0; theirs < 4; ++theirs)
          speeds[mine][theirs] += sample->positioning_knocks_by_speed[mine][theirs];
      for (std::size_t motion = 0; motion < 4; ++motion)
        motions[motion] += sample->positioning_knocks_by_motion[motion];
    }
    const double matches = std::max<double>(1.0, static_cast<double>(valid.size()));
    const auto perMatch = [&](double count) { return std::format("{:.1f}", count / matches); };
    static constexpr std::array<const char*, 6> REASONS{
        "one within 2 m of his spot", "one going round the other",
        "one saw the other, no clear side", "one going round someone else",
        "neither saw it coming", "one giving way to the other"};
    TextTable why{"Positioning knocks per match: why the way round did not stop them",
                  {"Why", "Team-mates", "Opponents", "All"},
                  {}};
    for (std::size_t reason = 0; reason < 6; ++reason)
      why.rows.push_back({REASONS[reason], perMatch(reasons[reason][0]),
                          perMatch(reasons[reason][1]),
                          perMatch(reasons[reason][0] + reasons[reason][1])});
    tables.push_back(std::move(why));
    static constexpr std::array<const char*, 4> BANDS{"under 1 m/s", "1-3 m/s", "3-5 m/s",
                                                      "over 5 m/s"};
    TextTable fast{"Positioning knocks per match: the knocked player's speed (down) and "
                   "the other's (across), just before",
                   {"Knocked \\ other", BANDS[0], BANDS[1], BANDS[2], BANDS[3]},
                   {}};
    for (std::size_t mine = 0; mine < 4; ++mine)
      fast.rows.push_back({BANDS[mine], perMatch(speeds[mine][0]), perMatch(speeds[mine][1]),
                           perMatch(speeds[mine][2]), perMatch(speeds[mine][3])});
    tables.push_back(std::move(fast));
    static constexpr std::array<const char*, 4> MOTIONS{
        "one standing (under 0.5 m/s)", "the same way (within 45 deg)", "crossing",
        "head-on (over 135 deg)"};
    TextTable how{"Positioning knocks per match: how they were moving", {"Moving", "Knocks"},
                  {}};
    for (std::size_t motion = 0; motion < 4; ++motion)
      how.rows.push_back({MOTIONS[motion], perMatch(motions[motion])});
    tables.push_back(std::move(how));
  }

  // Derived stats over every player who took part: do the recipes give the
  // intended spread?
  std::array<double, DERIVED_STAT_COUNT> derivedSums{};
  double derivedPlayers = 0.0;
  for (const MatchSample* sample : valid)
  {
    derivedPlayers += sample->derived_players;
    for (std::size_t index = 0; index < DERIVED_STAT_COUNT; ++index)
      derivedSums[index] += sample->derived_sum[index];
  }
  TextTable derived{"Derived stats (mean over players)",
                    {"Stat", "Layer", "Rating", "Physical value"},
                    {}};
  for (std::size_t index = 0; index < DERIVED_STAT_COUNT && derivedPlayers > 0;
       ++index)
  {
    const auto stat = static_cast<DerivedStat>(index);
    const DerivedStatInfo& info = derivedStatInfo(stat);
    const double rating = derivedSums[index] / derivedPlayers;
    derived.rows.push_back(
        {std::string(info.name), std::string(derivedLayerName(info.layer)),
         std::format("{:.1f}", rating * 100.0),
         info.unit.empty()
             ? std::string("-")
             : std::format("{:.4g} {}",
                           derivedPhysicalValue(stat, static_cast<float>(rating)),
                           info.unit)});
  }
  tables.push_back(std::move(derived));

  // Strikes as the contact model reads them (contact plan step 2): how
  // often each condition comes up, before it changes anything.
  StrikeContactStats strikes;
  for (const MatchSample* sample : valid)
    for (std::size_t kind = 0; kind < STRIKE_KIND_COUNT; ++kind)
    {
      const auto& from = sample->strike_contacts.byKind[kind];
      auto& to = strikes.byKind[kind];
      to.strikes += from.strikes;
      to.carriedOut += from.carriedOut;
      to.angleFailed += from.angleFailed;
      to.weightFailed += from.weightFailed;
      to.angleErrorSum += from.angleErrorSum;
      to.weightErrorSum += from.weightErrorSum;
      to.elevationErrorSum += from.elevationErrorSum;
      to.droppedOffGround += from.droppedOffGround;
      to.angleDifficulty += from.angleDifficulty;
      to.weightDifficulty += from.weightDifficulty;
      for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
      {
        to.factorSum[index] += from.factorSum[index];
        to.factorMarked[index] += from.factorMarked[index];
      }
    }
  std::vector<std::string> strikeHeader{"Condition"};
  for (const std::string_view name : STRIKE_KIND_NAMES)
    strikeHeader.emplace_back(name);
  TextTable strikeTable{
      "Strike contact, open play (Binturong bodies): conditions as mean "
      "value, and in brackets the share at half or more; failures and errors "
      "for the kinds the contact model carries out",
      std::move(strikeHeader),
      {}};
  const auto strikeRow = [&](std::string label, auto cell)
  {
    std::vector<std::string> row{std::move(label)};
    for (const auto& kind : strikes.byKind)
      row.push_back(kind.strikes > 0 ? cell(kind) : std::string("-"));
    strikeTable.rows.push_back(std::move(row));
  };
  strikeRow("Strikes per team",
            [&](const StrikeContactStats::ByKind& kind)
            {
              return std::format("{:.1f}", static_cast<double>(kind.strikes) /
                                               (2.0 * static_cast<double>(n)));
            });
  strikeRow("Dropped off the ground per team",
            [&](const StrikeContactStats::ByKind& kind)
            {
              return std::format("{:.2f}",
                                 static_cast<double>(kind.droppedOffGround) /
                                     (2.0 * static_cast<double>(n)));
            });
  const auto carriedRow = [&](std::string label, auto cell)
  {
    strikeRow(std::move(label),
              [&cell](const StrikeContactStats::ByKind& kind)
              {
                return kind.carriedOut > 0 ? cell(kind)
                                           : std::string("measured only");
              });
  };
  carriedRow("Angle failed",
             [](const StrikeContactStats::ByKind& kind)
             { return percent(share(kind.angleFailed, kind.carriedOut)); });
  carriedRow("Weight failed",
             [](const StrikeContactStats::ByKind& kind)
             { return percent(share(kind.weightFailed, kind.carriedOut)); });
  carriedRow("Mean angle error (deg)",
             [](const StrikeContactStats::ByKind& kind)
             {
               return std::format("{:.2f}", kind.angleErrorSum /
                                                kind.carriedOut * 180.0 /
                                                std::numbers::pi);
             });
  carriedRow("Mean elevation error (deg, + higher)",
             [](const StrikeContactStats::ByKind& kind)
             {
               return std::format("{:+.2f}", kind.elevationErrorSum /
                                                 kind.carriedOut * 180.0 /
                                                 std::numbers::pi);
             });
  carriedRow("Mean weight error (m/s, + harder)",
             [](const StrikeContactStats::ByKind& kind)
             {
               return std::format("{:+.2f}",
                                  kind.weightErrorSum / kind.carriedOut);
             });
  strikeRow("Angle difficulty",
            [](const StrikeContactStats::ByKind& kind)
            { return std::format("{:.3f}", kind.angleDifficulty / kind.strikes); });
  strikeRow("Weight difficulty",
            [](const StrikeContactStats::ByKind& kind)
            {
              return std::format("{:.3f}", kind.weightDifficulty / kind.strikes);
            });
  for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
    strikeRow(std::string(CONTACT_FACTOR_LABELS[index]),
              [index](const StrikeContactStats::ByKind& kind)
              {
                return std::format(
                    "{:.3f} ({})", kind.factorSum[index] / kind.strikes,
                    percent(share(kind.factorMarked[index], kind.strikes)));
              });
  tables.push_back(std::move(strikeTable));

  // Loose balls played with the body, by the part that met them (chest and
  // thigh control plan step 2).
  MatchEngine::BodyContactStats body;
  for (const MatchSample* sample : valid)
    for (std::size_t part = 0; part < 3; ++part)
    {
      body.receiver[part] += sample->body_contacts.receiver[part];
      body.other[part] += sample->body_contacts.other[part];
    }
  TextTable bodyTable{"Loose balls played with the body per team (Binturong "
                      "outfielders; heads apart)",
                      {"Who", "Feet", "Thigh", "Chest"},
                      {}};
  const auto bodyRow = [&](std::string label,
                           const std::array<std::uint32_t, 3>& counts)
  {
    std::vector<std::string> row{std::move(label)};
    for (const std::uint32_t count : counts)
      row.push_back(std::format("{:.2f}", static_cast<double>(count) /
                                             (2.0 * static_cast<double>(n))));
    bodyTable.rows.push_back(std::move(row));
  };
  bodyRow("Receiver of a team-mate's pass", body.receiver);
  bodyRow("Anyone else", body.other);
  tables.push_back(std::move(bodyTable));
  return tables;
}

TextTable ratingGapTable(std::span<const MatchSample> samples)
{
  static constexpr std::array<std::pair<const char*, std::array<double, 3>>, 4>
      BANDS = {{{"< 3", {0.0, 3.0, 0.0}},
                {"3-8", {3.0, 8.0, 0.0}},
                {"8-15", {8.0, 15.0, 0.0}},
                {"> 15", {15.0, 1.0e9, 0.0}}}};
  static constexpr std::array<const char*, 4> TARGETS = {"38-48%", "45-55%",
                                                         "52-65%", "60-75%"};
  TextTable table{
      "Stronger side by starter-rating gap (NFR-019; exact ties "
      "excluded)",
      {"Gap", "Matches", "Stronger wins (95% CI)", "Draws", "Stronger losses",
       "Target win share"},
      {}};
  for (std::size_t b = 0; b < BANDS.size(); ++b)
  {
    const auto& band = BANDS[b].second;
    std::size_t matches = 0;
    std::size_t wins = 0;
    std::size_t draws = 0;
    for (const MatchSample& sample : samples)
    {
      if (sample.abandoned) continue;
      if (const double gap = std::abs(static_cast<double>(sample.rating[0]) -
                                      static_cast<double>(sample.rating[1]));
          gap < RATING_TIE || gap < band[0] || gap >= band[1])
        continue;
      ++matches;
      const std::size_t strong = sample.rating[0] > sample.rating[1] ? 0 : 1;
      if (sample.goals[strong] > sample.goals[1 - strong])
        ++wins;
      else if (sample.goals[0] == sample.goals[1])
        ++draws;
    }
    table.rows.push_back({BANDS[b].first, std::to_string(matches),
                          ciPercent(proportionEstimate(wins, matches)),
                          percent(share(draws, matches)),
                          percent(share(matches - wins - draws, matches)),
                          TARGETS[b]});
  }
  return table;
}

TextTable perSideTable(std::span<const MatchSample> samples)
{
  struct Side
  {
    double wins = 0, goals = 0, shots = 0, onTarget = 0, xg = 0,
           possession = 0, passes = 0, completed = 0, fouls = 0, takeOns = 0,
           takeOnsWon = 0, duels = 0, duelsWon = 0;
  };
  std::array<Side, 2> sides{};
  double matches = 0;
  double draws = 0;
  for (const MatchSample& sample : samples)
  {
    if (sample.abandoned) continue;
    ++matches;
    if (sample.goals[0] == sample.goals[1]) ++draws;
    for (std::size_t s = 0; s < 2; ++s)
    {
      Side& side = sides[s];
      side.wins += sample.goals[s] > sample.goals[1 - s] ? 1 : 0;
      side.goals += sample.goals[s];
      side.shots += sample.shots[s];
      side.onTarget += sample.on_target[s];
      side.xg += sample.xg[s];
      side.possession += s == 0 ? sample.home_possession
                                : 100.0 - sample.home_possession;
      side.passes += sample.passes_attempted[s];
      side.completed += sample.passes_completed[s];
      side.fouls += sample.fouls[s];
      side.takeOns += sample.take_ons[s];
      side.takeOnsWon += sample.take_ons_won[s];
      side.duels += sample.tackle_duels[s];
      side.duelsWon += sample.tackle_duels_won[s];
    }
  }
  TextTable table{std::format("Per side, per match ({:.0f} matches, draws "
                              "{:.1f}%)",
                              matches, matches > 0 ? 100.0 * draws / matches
                                                   : 0.0),
                  {"Side", "Wins", "Goals", "Shots", "On target", "xG",
                   "Goals/shot", "Possession", "Passes", "Completion",
                   "Fouls", "Take-ons (won)", "Tackle duels (won)"},
                  {}};
  const auto per = [&](double value)
  { return std::format("{:.2f}", matches > 0 ? value / matches : 0.0); };
  const auto ratio = [](double part, double whole)
  {
    return whole > 0 ? std::format("{:.1f}%", 100.0 * part / whole)
                     : std::string("-");
  };
  for (std::size_t s = 0; s < 2; ++s)
  {
    const Side& side = sides[s];
    table.rows.push_back(
        {s == 0 ? "Home" : "Away", ratio(side.wins, matches), per(side.goals),
         per(side.shots), per(side.onTarget), per(side.xg),
         ratio(side.goals, side.shots),
         std::format("{:.1f}%", matches > 0 ? side.possession / matches : 0.0),
         per(side.passes), ratio(side.completed, side.passes), per(side.fouls),
         std::format("{} ({})", per(side.takeOns),
                     ratio(side.takeOnsWon, side.takeOns)),
         std::format("{} ({})", per(side.duels),
                     ratio(side.duelsWon, side.duels))});
  }
  return table;
}

std::vector<MatchJob> tacticJobs(std::uint64_t seed, int seeds_per_pair,
                                 float rating)
{
  std::vector<MatchJob> jobs;
  const auto pairs = presetPairs();
  const auto perPair = static_cast<std::size_t>(std::max(seeds_per_pair, 0));
  jobs.reserve(pairs.size() * perPair * 2);
  for (std::size_t p = 0; p < pairs.size(); ++p)
  {
    const auto [a, b] = pairs[p];
    for (std::size_t k = 0; k < perPair; ++k)
    {
      const std::uint32_t matchSeedValue = matchSeed(seed, p * perPair + k);
      // Same seed with home and away swapped removes venue and seed luck.
      jobs.push_back({matchSeedValue,
                      {rating, TACTIC_PRESETS[a].sliders},
                      {rating, TACTIC_PRESETS[b].sliders}});
      jobs.push_back({matchSeedValue,
                      {rating, TACTIC_PRESETS[b].sliders},
                      {rating, TACTIC_PRESETS[a].sliders}});
    }
  }
  return jobs;
}

std::array<std::size_t, 2> tacticPresetsOf(std::size_t job_index,
                                           int seeds_per_pair)
{
  const auto pairs = presetPairs();
  const auto perPair = static_cast<std::size_t>(std::max(seeds_per_pair, 1));
  const std::size_t pair = (job_index / 2) / perPair;
  const auto [a, b] = pairs[std::min(pair, pairs.size() - 1)];
  return job_index % 2 == 0 ? std::array<std::size_t, 2>{a, b}
                            : std::array<std::size_t, 2>{b, a};
}

void tacticMetrics(std::span<const MatchSample> samples, int seeds_per_pair,
                   std::map<std::string, Estimate>& metrics,
                   std::vector<TextTable>& tables)
{
  constexpr std::size_t PRESETS = TACTIC_PRESETS.size();
  struct PresetTotals
  {
    std::vector<double> points;
    int wins = 0;
    int draws = 0;
    int losses = 0;
    double goals_for = 0.0;
    double goals_against = 0.0;
    double shots = 0.0;
    double possession = 0.0;
    double fouls = 0.0;
    double passes_attempted = 0.0;
    double passes_completed = 0.0;
  };
  std::array<PresetTotals, PRESETS> totals{};
  std::array<std::array<double, PRESETS>, PRESETS> headPoints{};
  std::array<std::array<int, PRESETS>, PRESETS> headMatches{};

  for (std::size_t i = 0; i < samples.size(); ++i)
  {
    const MatchSample& sample = samples[i];
    if (sample.abandoned) continue;
    const auto presets = tacticPresetsOf(i, seeds_per_pair);
    for (std::size_t side = 0; side < 2; ++side)
    {
      PresetTotals& preset = totals[presets[side]];
      const int own = sample.goals[side];
      const int other = sample.goals[1 - side];
      double points = 0.0;
      if (own > other)
        points = 3.0;
      else if (own == other)
        points = 1.0;
      preset.points.push_back(points);
      (own > other    ? preset.wins
       : own == other ? preset.draws
                      : preset.losses) += 1;
      preset.goals_for += own;
      preset.goals_against += other;
      preset.shots += sample.shots[side];
      if (side == 0)
        preset.possession += static_cast<double>(sample.home_possession);
      else
        preset.possession +=
            100.0 - static_cast<double>(sample.home_possession);
      preset.fouls += sample.fouls[side];
      preset.passes_attempted += sample.passes_attempted[side];
      preset.passes_completed += sample.passes_completed[side];
      headPoints[presets[side]][presets[1 - side]] += points;
      ++headMatches[presets[side]][presets[1 - side]];
    }
  }

  TextTable table{"Tactic presets round robin (equal teams, paired seeds)",
                  {"Preset", "Matches", "W-D-L", "Points share (95% CI)",
                   "GF/match", "GA/match", "Shots/match", "Possession",
                   "Fouls/match", "Pass completion"},
                  {}};
  Estimate best;
  for (std::size_t p = 0; p < PRESETS; ++p)
  {
    const PresetTotals& preset = totals[p];
    const std::vector maxPoints(preset.points.size(), 3.0);
    const Estimate pointsShare = ratioEstimate(preset.points, maxPoints);
    if (pointsShare.valid() &&
        (!best.valid() || pointsShare.value > best.value))
      best = pointsShare;
    const auto matches = static_cast<double>(preset.points.size());
    const auto perMatch = [&](double value)
    { return matches > 0.0 ? std::format("{:.2f}", value / matches) : "-"; };
    table.rows.push_back(
        {std::string(TACTIC_PRESETS[p].name),
         std::to_string(preset.points.size()),
         std::format("{}-{}-{}", preset.wins, preset.draws, preset.losses),
         ciPercent(pointsShare), perMatch(preset.goals_for),
         perMatch(preset.goals_against), perMatch(preset.shots),
         matches > 0.0 ? std::format("{:.1f}%", preset.possession / matches)
                       : "-",
         perMatch(preset.fouls),
         preset.passes_attempted > 0.0
             ? percent(preset.passes_completed / preset.passes_attempted)
             : "-"});
  }
  metrics["max_preset_points_share"] = best;
  tables.push_back(std::move(table));

  TextTable matrix{
      "Head to head: points per match of the row preset", {"Preset"}, {}};
  for (const TacticPreset& preset : TACTIC_PRESETS)
    matrix.header.emplace_back(preset.name);
  for (std::size_t row = 0; row < PRESETS; ++row)
  {
    std::vector cells{std::string(TACTIC_PRESETS[row].name)};
    for (std::size_t col = 0; col < PRESETS; ++col)
      cells.push_back(headMatches[row][col] == 0
                          ? "-"
                          : std::format("{:.2f}", headPoints[row][col] /
                                                      headMatches[row][col]));
    matrix.rows.push_back(std::move(cells));
  }
  tables.push_back(std::move(matrix));
}
}  // namespace Lab
