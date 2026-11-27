// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "global/stats_config.h"
#include "model/match_ball_physics.h"
#include "model/match_body.h"
#include "model/match_brain.h"
#include "model/match_engine.h"
#include "model/match_handshake.h"
#include "model/match_manager.h"
#include "model/match_out_of_play.h"
#include "model/match_referee.h"
#include "model/strategy.h"
#include "tools/lab_fixtures.h"
#include "tools/lab_report.h"
#include "tools/lab_stats.h"

struct MatchReport;

/** Batch simulation of the balance lab: isolated matches and tactics. */
namespace Lab
{
/** Goal timing bins: 0-15, 16-30, 31-45+, 46-60, 61-75, 76-90+. */
inline constexpr std::size_t GOAL_TIME_BINS = 6;

/** Leading by one, level, trailing by one (team perspective). */
enum GameStateIndex : std::uint8_t
{
  LEAD_BY_ONE = 0,
  LEVEL = 1,
  TRAIL_BY_ONE = 2
};
/** Fewer players, equal numbers, more players (team perspective). */
enum StrengthIndex : std::uint8_t
{
  MAN_DOWN = 0,
  EVEN = 1,
  MAN_UP = 2
};

/**
 * Compact per-match record extracted on the simulating thread. Index 0 is
 * the home team, 1 the away team. `detailed` samples come from the live
 * engine; world samples built from a MatchReport lack the engine-only fields
 * (they stay 0 and the metrics needing them are skipped).
 */
struct MatchSample
{
  std::uint32_t seed = 0;
  bool detailed = false;
  bool abandoned = false;
  std::array<float, 2> rating{};
  std::array<int, 2> goals{};
  std::array<int, 2> shots{};
  std::array<int, 2> on_target{};
  std::array<int, 2> passes_attempted{};
  std::array<int, 2> passes_completed{};
  std::array<int, 2> fouls{};
  std::array<double, 2> xg{};
  float home_possession = 50.0F;
  int corners = 0;
  /** Restarts counted from the event log (engine only); the log keeps the
   * last MatchTuning::Timing::MAX_EVENTS events, so `event_log_full` marks
   * a match whose early restarts may be missing. */
  int throw_ins = 0;
  int goal_kicks = 0;
  int free_kicks = 0;
  bool event_log_full = false;
  int yellows = 0;
  int reds = 0;
  int offsides = 0;
  int own_goals = 0;
  /** Shots blocked by a body in open play (the free-kick wall not). */
  int shots_blocked = 0;
  /** Goals at second-half clock minute >= 90. */
  int late_goals = 0;
  std::array<int, GOAL_TIME_BINS> goal_bins{};
  // Engine-only detail.
  int inside_box = 0;
  int headed_goals = 0;
  /** Includes penalties (engine definition of a set-piece shot). */
  int set_piece_goals = 0;
  int penalty_goals = 0;
  int penalties = 0;
  int substitutions = 0;
  double substitution_minute_sum = 0.0;
  int injuries = 0;
  int added_minutes = 0;
  double ball_in_play = 0.0;
  double match_length = 0.0;
  /** Outfield starters who played the whole match. */
  int full_match_players = 0;
  double full_match_distance = 0.0;
  /** Distance of those players by half and the half lengths (minutes). */
  double first_half_distance = 0.0;
  double second_half_distance = 0.0;
  double first_half_minutes = 0.0;
  double second_half_minutes = 0.0;
  /** Team-minutes and goals by game state, summed over both teams. */
  std::array<double, 3> state_minutes{};
  std::array<int, 3> state_goals{};
  std::array<double, 3> strength_minutes{};
  std::array<int, 3> strength_goals{};
  /** Player behaviour, summed over both teams' players (engine only). */
  int tackles_attempted = 0;
  int tackles_won = 0;
  int interceptions = 0;
  int clearances = 0;
  int aerial_duels = 0;
  /** Header sequences (MatchStats::headerSequences and the rest). */
  int header_sequences = 0;
  int header_sequence_headers = 0;
  int header_sequences_long = 0;
  int header_sequence_longest = 0;
  int header_sequences_to_control = 0;
  int header_sequences_kept = 0;
  int pressures = 0;
  int key_passes = 0;
  int saves = 0;
  int touches = 0;
  /** Contests by side (home, away): attempts and successes of the side
   * named (the carrier for take-ons, the tackler, the keeper), for
   * comparing bodies (engine only). */
  std::array<int, 2> take_ons{};
  std::array<int, 2> take_ons_won{};
  std::array<int, 2> tackle_duels{};
  std::array<int, 2> tackle_duels_won{};
  /** Tackles on a free ball (tackling plan step 1), and those won. */
  std::array<int, 2> free_ball_tackles{};
  std::array<int, 2> free_ball_tackles_won{};
  /** Contest tackles (tackling plan step 2), those won, and swings at
   * nothing by carriers and by tacklers. */
  int contest_tackles = 0;
  int contest_tackles_won = 0;
  int carrier_air_swings = 0;
  int tackler_air_swings = 0;
  /** Feet first touches missed under the commit rule. */
  int commit_misses = 0;
  /** Blocks (tackling plan step 4), those with the carrier's planned
   * touch, and those that popped it up. */
  int blocks = 0;
  int blocks_committed = 0;
  int blocks_popped = 0;
  /** Open-play shots blocked by a Binturong body, those off a leg, and
   * those off a leg he put there (a reaction block). */
  int shot_body_blocks = 0;
  int shot_leg_blocks = 0;
  int shot_reaction_blocks = 0;
  int shot_deflections = 0;
  /** Game xG plan, layer 3 check (MatchStats::blockBandShots): Capybara
   * open-play shots by predicted band, the predicted shares summed, and
   * those first met by an opponent's block or a team-mate. */
  std::array<int, MatchStats::BLOCK_BANDS> block_band_shots{};
  std::array<double, MatchStats::BLOCK_BANDS> block_band_predicted{};
  std::array<int, MatchStats::BLOCK_BANDS> block_band_blocked{};
  std::array<int, MatchStats::BLOCK_BANDS> block_band_team_mate{};
  /** Shots meeting the defending keeper's body in his area (goalkeeper plan
   * step 1). */
  int keeper_body_hits = 0;
  int keeper_body_still_on_goal = 0;
  /** Of keeper_body_hits, off a hand or an arm (step 2). */
  int keeper_hand_hits = 0;
  int keeper_hands_went = 0;
  int keeper_hands_together = 0;
  int keeper_hands_late = 0;
  int keeper_dives = 0;
  int keeper_dives_late = 0;
  /** Goalkeeper plan step 4: shots off a hand; both hands; catches; held;
   * fumbled; parried. */
  int keeper_hand_shots = 0;
  int keeper_both_hands = 0;
  int keeper_catches = 0;
  int keeper_held = 0;
  int keeper_fumbles = 0;
  int keeper_parries = 0;
  /** Goalkeeper plan step 5: balls in his hands; rolled, thrown, punted. */
  int keeper_balls_in_hands = 0;
  int keeper_rolls = 0;
  int keeper_throws = 0;
  int keeper_punts = 0;
  /** Shootout kicks taken, scored and saved (goalkeeper plan step 9). */
  /** Goalkeeper plan step 7: claims tried (both keepers), punched, sprung
   * off his hands. */
  int keeper_claims_tried = 0;
  int keeper_claims_held = 0;
  int keeper_punches = 0;
  int keeper_claim_parries = 0;
  /** Goalkeeper plan step 8: smother dives, loose balls held off a hand. */
  int keeper_smother_dives = 0;
  int keeper_smothers_held = 0;
  int shootout_kicks = 0;
  int shootout_goals = 0;
  int shootout_saves = 0;
  /** Shot block plan §3f check: shots through a torso without meeting it. */
  /** Pass block plan: other balls off a body (all, off a leg put out, an
   * opponent's pass blocked, a deflection off a team-mate or nobody's). */
  int ball_body_hits = 0;
  int ball_leg_hits = 0;
  int ball_head_hits = 0;
  int ball_reaction_blocks = 0;
  int passes_blocked = 0;
  int ball_deflections = 0;
  /** Direct free kicks: taken, on target as struck, blocked, scored. */
  int direct_free_kicks = 0;
  int direct_free_kicks_on_target = 0;
  int direct_free_kicks_blocked = 0;
  int direct_free_kick_goals = 0;
  int shot_torso_pass_throughs = 0;
  int shot_torso_inside_at_start = 0;
  /** Shot block plan §3d: shots whose line passed within a defender's
   * full reach (per defender), and what came of it once he reacted
   * (ShotLegOutcome order); the rest, the ball got to him first. */
  int shot_leg_chances = 0;
  std::array<int, static_cast<std::size_t>(ShotLegOutcome::COUNT)>
      shot_leg_outcomes{};
  /** Tackling plan step 5: steps defenders went into a contest, those on
   * the carrier's free foot, steps held, and the intents chosen. */
  int go_ins = 0;
  int go_ins_free_foot = 0;
  /** Facing the ball plan step 1 (see MatchStats). */
  /** Facing the ball plan §4c: run-ats (see MatchStats). */
  int run_ats = 0;
  int run_at_shots = 0;
  int run_at_shots_defender_goal_side = 0;
  int run_at_passes = 0;
  int run_at_won = 0;
  int pounces = 0;
  int press_frames = 0;
  int press_squeezed = 0;
  int run_at_other = 0;
  int run_at_past = 0;
  double run_at_shot_xg = 0.0;
  double run_at_seconds = 0.0;
  double run_at_ground_given = 0.0;
  double run_at_start_xg = 0.0;
  double run_at_best_xg = 0.0;
  int facing_goal_side = 0;
  int facing_faces = 0;
  int facing_turned = 0;
  int facing_turned_pressing = 0;
  int facing_chase = 0;
  int facing_receive = 0;
  int facing_run = 0;
  int facing_tracks = 0;
  int facing_aerial = 0;
  /** Step O6: the shadow loose-ball calls (see MatchStats). */
  int loose_shadow_checks = 0;
  int loose_shadow_agree = 0;
  int loose_shadow_wrong_player = 0;
  int loose_shadow_nobody = 0;
  int loose_shadow_theirs = 0;
  int loose_shadow_in_doubt = 0;
  int loose_shadow_two_go = 0;
  int loose_shadow_calls = 0;
  int loose_shadow_later_point = 0;
  int loose_calls_made = 0;
  int loose_calls_right = 0;
  int loose_calls_agree = 0;
  int loose_call_millis = 0;
  int loose_wrong_shadow_faster = 0;
  int loose_wrong_near_tie = 0;
  int loose_wrong_exact_tie = 0;
  int loose_wrong_slower = 0;
  int loose_wrong_slower_big = 0;
  int loose_wrong_slower_later = 0;
  int loose_wrong_slower_missed = 0;
  /** Steps P1 to P5: the press calls (see MatchStats). */
  int press_shadow_checks = 0;
  int press_shadow_one = 0;
  int press_shadow_two_go = 0;
  int press_shadow_nobody = 0;
  int press_shadow_with_doubt = 0;
  int press_calls_made = 0;
  int press_calls_right = 0;
  int press_call_millis = 0;
  int press_commits_carried = 0;
  int press_go_one = 0;
  int press_go_several = 0;
  int press_go_none = 0;
  int press_second_checks = 0;
  int press_second_double_up = 0;
  int press_second_block_lane = 0;
  int press_second_cover = 0;
  int press_second_no_job = 0;
  int press_second_several = 0;
  int press_second_held = 0;
  int press_second_none = 0;
  int press_second_stood_down = 0;
  int press_second_with_doubt = 0;
  int press_second_own_call = 0;
  int press_second_is_nearest = 0;
  int press_second_switches = 0;
  int press_first_switches = 0;
  int holds = 0;
  int intents_win = 0;
  int intents_clear = 0;
  int intents_control = 0;
  /** Collision plan: run-ins (Binturong bodies), and those jolting either
   * player by 2 m/s or more, and by 4 or more. */
  int collisions = 0;
  int collisions_jolt_two = 0;
  int collisions_jolt_four = 0;
  /** Collision plan step 3: players knocked into a stumble, knocked over,
   * and stumbles that slipped into a fall. */
  int collision_stumbles = 0;
  int collision_falls = 0;
  int collision_slips = 0;
  int collision_knocks_by_ends_up = 0;
  int collision_knocks_small_jolt = 0;
  /** Steps 4-5: big hits, falls that lost him the ball, player-seconds
   * down (all, in open play), get-ups by the call that started them, and
   * spells down of 15 s or more. */
  int collision_big_hits = 0;
  int collision_falls_losing_ball = 0;
  double collision_down_seconds = 0.0;
  double collision_down_play_seconds = 0.0;
  int collision_get_ups_for_ball = 0;
  int collision_get_ups_in_stoppage = 0;
  int collision_get_ups_gingerly = 0;
  int collision_get_ups_fixed = 0;
  int collision_downs_fifteen = 0;
  /** Step 6: times the ball came off a fallen body. */
  int collision_ball_off_fallen = 0;
  /** Stumbles and falls by situation and by the knocked player's intent
   * (MatchStats::collisionKnocksBySituation, collisionKnocksByIntent). */
  std::array<int, 8> collision_knocks_by_situation{};
  std::array<int, PLAYER_INTENT_COUNT> collision_knocks_by_intent{};
  std::array<std::array<int, PLAYER_INTENT_COUNT>, PLAYER_INTENT_COUNT>
      collision_knocks_by_intent_pair{};
  std::array<std::array<int, 2>, 6> positioning_knocks_by_reason{};
  int give_way_slows = 0;
  int give_way_rounds = 0;
  int give_way_holds = 0;
  std::array<std::array<int, 4>, 4> positioning_knocks_by_speed{};
  std::array<int, 4> positioning_knocks_by_motion{};
  std::array<int, PLAYER_INTENT_COUNT + 1> collision_chase_knocks_with{};
  int collision_chase_knocks_on_chaser = 0;
  /** Way-round plan: chasers starting to go round someone, and refreshes
   * spent going round (chasing, holding shape, supporting, recovering). */
  std::array<int, 4> way_rounds_started{};
  std::array<int, 4> way_round_refreshes{};
  std::array<int, 2> keeper_claim_attempts{};
  std::array<int, 2> keeper_claims{};
  /** Aerial duels with both sides involved, and those the home side won. */
  int aerial_contested = 0;
  int aerial_home_won = 0;
  /** High-intensity and sprint running of the full-match outfield starters
   * (the same players as full_match_distance). */
  double full_match_high_intensity = 0.0;
  double full_match_sprint = 0.0;
  int full_match_sprints = 0;
  /** Their walking, jogging and running (below high intensity), Bradley et
   * al. 2009's bands. */
  double full_match_walk = 0.0;
  double full_match_jog = 0.0;
  double full_match_run = 0.0;
  /** Running above 14.4 km/h in the 5 minutes after each one's peak 5
   * minutes, against his match mean per window, minus 1, summed, and how
   * many (Mohr et al. 2003; a peak in a half's last window is skipped). */
  double peak_next_change_sum = 0.0;
  int peak_next_players = 0;
  /** Their metabolic energy (kJ/kg) and its anaerobic part, and among those
   * whose tank ran out, how many and the elapsed minutes when it did. */
  double full_match_energy = 0.0;
  double full_match_anaerobic = 0.0;
  double full_match_braking_energy = 0.0;
  double full_match_braking_anaerobic = 0.0;
  /** Their time (s) and distance (m) in each movement mode, by their own
   * bands (walk, jog, run, sprint). */
  std::array<double, MOVEMENT_MODE_COUNT> mode_seconds{};
  std::array<double, MOVEMENT_MODE_COUNT> mode_metres{};
  int emptied_players = 0;
  double empty_minute_sum = 0.0;
  /** Movement jitter of both sides together (MatchEngine::getMovementJitter).
   */
  MovementJitterStats jitter;
  /** Open-play strikes as the contact model reads them (Binturong bodies;
   * MatchEngine::getStrikeContacts). */
  StrikeContactStats strike_contacts;
  /** Loose balls played with the body by part (Binturong bodies;
   * MatchEngine::getBodyContacts). */
  MatchEngine::BodyContactStats body_contacts;
  /** Derived stats summed over every player who took part, and how many. */
  std::array<double, DERIVED_STAT_COUNT> derived_sum{};
  int derived_players = 0;
  /** Running by 15-minute spell, both sides together. */
  LoadProfileStats load;
  /** Outfield players on the pitch at full time: how many, their fuel
   * (condition) summed, and how many were at its floor. */
  int finishers = 0;
  double end_fuel_sum = 0.0;
  double end_fuel_square_sum = 0.0;
  float end_fuel_min = 1.0F;
  int finishers_at_floor = 0;
  /** Finishers by fuel left: 60% or more, 30-60%, 10-30%, below 10%, empty
   * (the fatigue plan's tier points). */
  static constexpr std::size_t FUEL_BANDS = 5;
  std::array<int, FUEL_BANDS> end_fuel_bands{};

  int totalGoals() const { return goals[0] + goals[1]; }
};

/** One side of an isolated match. */
struct TeamSetup
{
  float rating = 65.0F;
  StrategySliders sliders{};
  BrainId brain = BrainId::BINTURONG;
  BodyId body = BodyId::BINTURONG;
  /** The AI manager running the side's touchline (when auto-managed). */
  ManagerId manager = ManagerId::BINTURONG;
  /** Every stat of every player is `rating` (buildUniformLineup) instead
   * of the role-shaped squad. */
  bool uniform = false;
  /** The side's formation (fm_lab matches --formation); none: the 4-4-2. */
  std::optional<FormationSlots> formation;
};

struct MatchJob
{
  std::uint32_t seed = 0;
  TeamSetup home;
  TeamSetup away;
  /** Played with MatchFidelity::BACKGROUND (as unwatched fixtures are). */
  bool background = false;
  /** How the inputs become match state (one per match). */
  HandshakeId handshake = HandshakeId::BINTURONG;
  /** The referee and the ball physics (one each per match). */
  RefereeId referee = RefereeId::BINTURONG;
  BallPhysicsId ballPhysics = BallPhysicsId::BINTURONG;
  /** What happens when the ball leaves play (one per match). */
  OutOfPlayId outOfPlay = OutOfPlayId::BINTURONG;
  /** MatchContext::homeAdvantageScale: 0 a neutral venue, 1 the engine's
   * default (the game's full home edge), up to 2. */
  float homeAdvantage = 1.0F;
  /** A knockout tie: level at full time, straight to a shootout. */
  bool knockout = false;
  /** Tuning knobs for the match (defaults: the tuned constants). */
  FirstTouchKnobs touchKnobs;
  StrikeKnobs strikeKnobs;
  /** Independent per-match possession. */
  PossessionId possession = PossessionId::BINTURONG;
};

/** A match that did not reach full time or produced invalid numbers. */
class SimulationError : public std::runtime_error
{
 public:
  SimulationError(std::uint32_t failing_seed, const std::string& what)
      : std::runtime_error(what), seed(failing_seed)
  {
  }
  std::uint32_t seed;
};

/** Deterministic 32-bit match seed number `index` of a run seeded `seed`. */
std::uint32_t matchSeed(std::uint64_t seed, std::uint64_t index);

/**
 * Simulates one isolated match on the calling thread with fresh squads (no
 * state is shared between jobs). Throws SimulationError.
 */
MatchSample simulateMatch(const MatchJob& job, const StatsConfig& config);

/**
 * Plays the first `ticks` fixed steps of the job's match and writes every
 * player and the ball after each (tick 0 before the first) to `out` as
 * trace rows labelled `run` (lab_trace.h).
 */
void traceMatch(const MatchJob& job, const StatsConfig& config, int ticks,
                std::string_view run, std::ostream& out);

/**
 * Simulates every job on `threads` threads (caller included) through the
 * shared ThreadPool; sample i belongs to job i, so the output does not
 * depend on the thread count. `progress` (optional) is called from the
 * calling thread with the number of finished jobs.
 */
std::vector<MatchSample> simulateMatches(
    std::span<const MatchJob> jobs, const StatsConfig& config, unsigned threads,
    const std::function<void(std::size_t)>& progress = nullptr);

/** Summary of a structured world match report (not `detailed`). */
MatchSample sampleFromReport(const MatchReport& report);

/**
 * Every match metric computable from the samples (abandoned matches are
 * excluded and counted in abandoned_share). Keys are TargetSpec::metric
 * names; definitions sit next to each metric in lab_runner.cpp.
 */
std::map<std::string, Estimate> matchMetrics(
    std::span<const MatchSample> samples);

/** Total-goals histogram, top scorelines and goal timing. */
std::vector<TextTable> matchTables(std::span<const MatchSample> samples);

/** Stronger-side results by rating-gap band (NFR-019). */
TextTable ratingGapTable(std::span<const MatchSample> samples);
/** Home and away side by side, per match: results, shots, xG, possession,
 * passing, fouls and duels (for comparing two brains or bodies). */
TextTable perSideTable(std::span<const MatchSample> samples);

/**
 * Round robin of TACTIC_PRESETS between equal teams: for each pair and each
 * of `seeds_per_pair` seeds, both home/away orders with the same seed.
 */
std::vector<MatchJob> tacticJobs(std::uint64_t seed, int seeds_per_pair,
                                 float rating);
/** Preset of each side of tacticJobs() job i. */
std::array<std::size_t, 2> tacticPresetsOf(std::size_t job_index,
                                           int seeds_per_pair);
/** SC-004 metric plus the per-preset results table. */
void tacticMetrics(std::span<const MatchSample> samples, int seeds_per_pair,
                   std::map<std::string, Estimate>& metrics,
                   std::vector<TextTable>& tables);
}  // namespace Lab
