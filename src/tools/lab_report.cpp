// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/lab_report.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>

namespace Lab
{
namespace
{
using enum Tier;
using enum Scope;
using enum Unit;

constexpr std::optional<Band> NO_BAND = std::nullopt;
constexpr std::string_view NOT_EXPOSED =
    "not measurable yet: the engine exposes no kinematic/positional summary";

// clang-format off
/**
 * Target bands transcribed from spec.md "Realism Requirements and Calibration
 * Targets" (CT-M01..CT-M43, CT-W01..CT-W32). Source classes as in the spec:
 * CC0 openfootball/football-data aggregates, PUB league/Opta published
 * figures, PEER peer-reviewed, REG regulator reports, INT restricted-licence
 * derived (aggregate only, to be replaced before publication, C-009). Rows
 * without an explicit spec range use target +-20% (NFR-018) and say so.
 * INFO rows have no spec band: their target text is an orientation value
 * only and never produces a verdict.
 */
constexpr std::array TARGETS = {
  // ---- Physical (need per-player kinematic summaries) ----------------------
  TargetSpec{"CT-M01", "", "Outfielder match top speed", "8.5 m/s", Band{7.9, 8.9}, Gate, "PEER/PUB", Number, Engine, NOT_EXPOSED},
  TargetSpec{"CT-M02", "", "Acceleration profile A0 / S0", "7.0 m/s2 / 9.4 m/s", NO_BAND, Monitor, "PEER", Number, Engine, NOT_EXPOSED},
  TargetSpec{"CT-M03", "", "Sprint time constant", "1.3 s", Band{1.0, 1.5}, Monitor, "PEER", Number, Engine, NOT_EXPOSED},
  TargetSpec{"CT-M04", "", "Peak deceleration", "7-10 m/s2", NO_BAND, Monitor, "PEER", Number, Engine, NOT_EXPOSED},
  TargetSpec{"CT-M05", "", "Curve speed 6 m / 2 m radius", "74% / 49%", NO_BAND, Monitor, "PEER", Number, Engine, NOT_EXPOSED},
  TargetSpec{"CT-M06", "distance_per_outfielder_km", "Distance per full-match outfielder (km)", "10.6 MF / 9.2 CB", Band{9.0, 12.5}, Gate, "PUB", Number, Engine, "PlayerMatchStats::distanceMetres of outfield starters who played the whole match"},
  TargetSpec{"CT-M07", "", "Ball drag coefficient", "0.45 / 0.25", NO_BAND, Monitor, "PEER", Number, Engine, NOT_EXPOSED},
  TargetSpec{"CT-M08", "", "Instep kick speed", "28 m/s", Band{23.0, 35.0}, Monitor, "PEER", Number, Engine, NOT_EXPOSED},
  TargetSpec{"CT-M09", "", "Bounce restitution", "0.62", Band{0.55, 0.71}, Monitor, "REG", Number, Engine, NOT_EXPOSED},
  TargetSpec{"CT-M10", "", "Rolling deceleration", "0.8 m/s2", Band{0.5, 1.3}, Monitor, "REG", Number, Engine, NOT_EXPOSED},
  TargetSpec{"CT-M11", "late_high_intensity_change", "High-intensity running last vs first 15'", "-15%", Band{-0.45, -0.08}, Gate, "PEER", Percent, Engine, "outfield players per minute on the pitch, clock 75-90+ vs 0-15"},
  TargetSpec{"CT-M12", "late_distance_change", "Total distance last vs first 15'", "-14%", Band{-0.17, -0.12}, Monitor, "PEER", Percent, Engine, "outfield players per minute on the pitch, clock 75-90+ vs 0-15"},
  // ---- Results and scoring -------------------------------------------------
  TargetSpec{"CT-M13", "goals_per_match", "Goals per match", "2.83", Band{2.5, 3.3}, Gate, "CC0/PUB", Number, Match, ""},
  TargetSpec{"CT-M14", "home_goal_multiplier", "Home / away goals multiplier", "1.55/1.28 = x1.21", Band{1.07, 1.41}, Gate, "CC0", Number, Match, "sum(home goals) / sum(away goals)"},
  TargetSpec{"CT-M15", "draw_share", "Draw share", "25%", Band{0.20, 0.30}, Gate, "CC0", Percent, Match, ""},
  TargetSpec{"CT-M15H", "home_win_share", "Home win share", "44%", NO_BAND, Info, "CC0", Percent, Match, "spec gives a range for draws only"},
  TargetSpec{"CT-M15A", "away_win_share", "Away win share", "31%", NO_BAND, Info, "CC0", Percent, Match, "spec gives a range for draws only"},
  TargetSpec{"CT-M16", "goalless_share", "0-0 share", "6.1%", Band{0.029, 0.088}, Gate, "CC0", Percent, Match, ""},
  TargetSpec{"CT-M16B", "one_one_share", "1-1 share", "12.2%", Band{0.0976, 0.1464}, Monitor, "CC0", Percent, Match, "no explicit spec range: target +-20% (NFR-018)"},
  TargetSpec{"CT-M17", "goals_variance_mean", "Total goals variance / mean", "1.0", Band{0.9, 1.15}, Gate, "CC0 derived", Number, Match, "bootstrap interval"},
  TargetSpec{"CT-M18", "shots_per_team", "Shots per team", "12.8", Band{10.5, 15.5}, Gate, "CC0/PUB", Number, Match, ""},
  TargetSpec{"CT-M19", "on_target_per_team", "Shots on target per team", "4.5", Band{3.6, 5.4}, Gate, "CC0", Number, Match, ""},
  TargetSpec{"CT-M20", "goals_per_shot", "Goals per shot", "10.5%", Band{0.095, 0.12}, Gate, "PUB", Percent, Match, "own goals excluded from the numerator"},
  TargetSpec{"CT-M21", "inside_box_share", "Share of shots inside the box", "66%", Band{0.58, 0.70}, Monitor, "PUB", Percent, Engine, ""},
  TargetSpec{"CT-M22", "passes_per_match", "Passes per match (both teams)", "900", Band{850.0, 950.0}, Gate, "PUB", Number, Match, ""},
  TargetSpec{"CT-M22B", "pass_completion", "Pass completion", "82%", Band{0.76, 0.85}, Gate, "PUB", Percent, Match, ""},
  TargetSpec{"CT-M23", "", "Average pass length", "19.5 m", Band{18.9, 21.1}, Monitor, "PUB", Number, Engine, "not measurable yet: pass lengths are not recorded"},
  TargetSpec{"CT-M24", "", "PPDA", "11", Band{7.3, 17.7}, Monitor, "PUB", Number, Engine, "not measurable yet: needs pass/defensive-action locations"},
  TargetSpec{"CT-M25", "", "Team length / width out of possession", "38 / 43 m", NO_BAND, Monitor, "PEER", Number, Engine, "not measurable yet: needs team shape samples"},
  TargetSpec{"CT-M26", "", "High turnovers (both teams)", "14", Band{11.5, 16.7}, Monitor, "PUB", Number, Engine, "not measurable yet: turnover locations not recorded"},
  TargetSpec{"CT-M27", "", "Fast-break goals share", "5%", Band{0.014, 0.071}, Monitor, "PUB", Percent, Engine, "not measurable yet: no possession-chain tagging"},
  TargetSpec{"CT-M28", "corners_per_match", "Corners per match", "10.3", Band{9.3, 11.0}, Gate, "CC0/PUB", Number, Match, ""},
  TargetSpec{"CT-M28B", "", "Goals per corner", "3.5%", Band{0.028, 0.042}, Gate, "PUB", Percent, Engine, "not measurable yet: goals are not attributed to corners"},
  TargetSpec{"CT-M29", "set_piece_goal_share", "Non-penalty set-piece share of goals", "21%", Band{0.198, 0.31}, Gate, "PUB", Percent, Engine, "(set-piece goals - penalty goals) / goals; engine set-piece window definition"},
  TargetSpec{"CT-M30", "penalties_per_match", "Penalties per match", "0.27", Band{0.2, 0.36}, Gate, "PUB", Number, Engine, ""},
  TargetSpec{"CT-M30B", "penalty_conversion", "Penalty conversion", "80%", Band{0.75, 0.90}, Gate, "PUB", Percent, Engine, ""},
  TargetSpec{"LAB-THROW", "throw_ins_per_match", "Throw-ins per match", "", NO_BAND, Info, "orientation", Number, Engine, "from the event log (training ground §15: restarts of each kind)"},
  TargetSpec{"LAB-GKICK", "goal_kicks_per_match", "Goal kicks per match", "", NO_BAND, Info, "orientation", Number, Engine, "from the event log"},
  TargetSpec{"LAB-FKICK", "free_kicks_per_match", "Free kicks per match", "", NO_BAND, Info, "orientation", Number, Engine, "from the event log; every free kick, direct and indirect"},
  TargetSpec{"LAB-EVFULL", "event_log_full_share", "Matches filling the event log", "0%", NO_BAND, Info, "orientation", Percent, Engine, "the log keeps the last 1,024 events; above 0%, the restart counts miss early ones"},
  TargetSpec{"LAB-SOCONV", "shootout_conversion", "Shootout kicks scored", "", NO_BAND, Info, "orientation", Percent, Engine, "of shootout kicks (fm_lab matches --knockout: level at full time, straight to a shootout)"},
  TargetSpec{"LAB-SOSAVE", "shootout_save_share", "  ...saved", "", NO_BAND, Info, "orientation", Percent, Engine, "of shootout kicks: off the keeper or held"},
  TargetSpec{"CT-M31", "fouls_per_team", "Fouls per team", "11.5", Band{10.5, 13.0}, Gate, "CC0", Number, Match, ""},
  TargetSpec{"CT-M32", "yellows_per_match", "Yellow cards per match", "4.1", Band{3.75, 5.0}, Gate, "CC0", Number, Match, "league band (referee band 2.9-5.1)"},
  TargetSpec{"CT-M32B", "reds_per_match", "Red cards per match", "0.18", Band{0.14, 0.36}, Gate, "CC0", Number, Match, ""},
  TargetSpec{"CT-M33", "subs_per_team", "Substitutions per team", "4.5", Band{4.2, 4.6}, Monitor, "INT", Number, Engine, "INT source: replace before publication (C-009)"},
  TargetSpec{"CT-M33B", "sub_mean_minute", "Mean substitution minute", "71-73", Band{71.0, 73.0}, Monitor, "INT", Number, Engine, "INT source: replace before publication (C-009)"},
  TargetSpec{"CT-M34", "ball_in_play_minutes", "Ball in play (min)", "56.5", Band{54.75, 58.18}, Gate, "PUB", Number, Engine, "engine clock minutes"},
  TargetSpec{"CT-M34B", "match_length_minutes", "Total match length (min)", "100.5", Band{99.73, 101.6}, Gate, "PUB", Number, Engine, "90 + both added times"},
  TargetSpec{"CT-M35", "late_goal_share", "Goals from minute 90 onwards", "8.5%", Band{0.047, 0.09}, Monitor, "PUB", Percent, Match, "second-half clock minute >= 90"},
  TargetSpec{"CT-M36", "red_penalised_multiplier", "Scoring rate after own red card (x even)", "x0.58-0.83", Band{0.58, 0.83}, Monitor, "PEER", Number, Engine, "goals per team-minute a man down / at even strength; bootstrap"},
  TargetSpec{"CT-M36B", "red_opponent_multiplier", "Opponent scoring rate after red (x even)", "x1.60-1.69", Band{1.60, 1.69}, Monitor, "PEER", Number, Engine, "goals per team-minute a man up / at even strength; bootstrap"},
  TargetSpec{"CT-M37", "lead1_goal_multiplier", "Goal rate leading by 1 (x level)", "x0.91", Band{0.728, 1.092}, Monitor, "PEER", Number, Engine, "no explicit spec range: target +-20%; bootstrap"},
  TargetSpec{"CT-M37B", "trail1_goal_multiplier", "Goal rate trailing by 1 (x level)", "x1.10", Band{0.88, 1.32}, Monitor, "PEER", Number, Engine, "no explicit spec range: target +-20%; bootstrap"},
  TargetSpec{"CT-M38", "headed_goal_share", "Headed-goal share", "14%", Band{0.12, 0.19}, Monitor, "PUB", Percent, Engine, ""},
  TargetSpec{"CT-M38B", "own_goal_share", "Own-goal share", "2-4%", Band{0.02, 0.04}, Monitor, "PUB", Percent, Match, ""},
  TargetSpec{"CT-M39", "offsides_per_team", "Offsides per team", "1.9", Band{1.6, 2.0}, Monitor, "INT", Number, Match, "INT source: replace before publication (C-009)"},
  TargetSpec{"CT-M40", "", "Video-review overturns per match", "0.29", Band{0.28, 0.30}, Monitor, "PUB", Number, Engine, "not measurable yet: no video review in the engine"},
  TargetSpec{"CT-M41", "xg_per_shot", "Mean xG per shot", "0.105", Band{0.09, 0.12}, Monitor, "PUB", Number, Match, ""},
  TargetSpec{"CT-M42", "", "Goalkeeper reaction + dive", "0.21 s + 0.7-0.8 s", NO_BAND, Monitor, "PEER", Number, Engine, "not measurable yet: keeper timings not exposed"},
  TargetSpec{"CT-M43", "", "Home points advantage without crowd", "-45%", Band{-0.57, -0.33}, Monitor, "PEER", Percent, Engine, "not measurable yet: no crowd/empty-stadium switch"},
  // ---- Skill sensitivity (NFR-019; needs --spread) --------------------------
  TargetSpec{"NFR-019A", "stronger_win_gap_lt3", "Stronger side win share, rating gap < 3", "38-48%", Band{0.38, 0.48}, Monitor, "spec NFR-019", Percent, Engine, "starter mean overall gap; exact ties excluded (use --spread)"},
  TargetSpec{"NFR-019B", "stronger_win_gap_3_8", "Stronger side win share, gap 3-8", "45-55%", Band{0.45, 0.55}, Monitor, "spec NFR-019", Percent, Engine, ""},
  TargetSpec{"NFR-019C", "stronger_win_gap_8_15", "Stronger side win share, gap 8-15", "52-65%", Band{0.52, 0.65}, Monitor, "spec NFR-019", Percent, Engine, ""},
  TargetSpec{"NFR-019D", "stronger_win_gap_gt15", "Stronger side win share, gap > 15", "60-75%", Band{0.60, 0.75}, Monitor, "spec NFR-019", Percent, Engine, ""},
  // ---- Tactics (fm_lab tactics) ---------------------------------------------
  TargetSpec{"SC-004", "max_preset_points_share", "Best preset points share vs the others", "<= 60%", Band{0.0, 0.60}, Gate, "spec SC-004", Percent, Tactics, "points / (3 x matches) over paired-seed home/away round robin"},
  // ---- Match INFO (no spec band) --------------------------------------------
  TargetSpec{"LAB-ONENIL", "one_nil_share", "1-0 or 0-1 share", "~17% (big-5 leagues)", NO_BAND, Info, "orientation", Percent, Match, ""},
  TargetSpec{"LAB-ONTGT", "on_target_share", "Shots on target share", "~35%", NO_BAND, Info, "orientation", Percent, Match, ""},
  TargetSpec{"LAB-BLKSH", "blocked_share", "Shots blocked (open play)", "~25%", NO_BAND, Info, "orientation", Percent, Match, "by a body; the free-kick wall excluded"},
  TargetSpec{"LAB-BLKLEG", "shot_leg_block_share", "Shot blocks off a leg", "", NO_BAND, Info, "orientation", Percent, Engine, "of open-play shots blocked by a Binturong body: thigh, shin or foot (the rest torso or head)"},
  TargetSpec{"LAB-BLKREACT", "shot_reaction_block_share", "Shot blocks off a leg put out", "", NO_BAND, Info, "orientation", Percent, Engine, "of open-play shots blocked by a Binturong body: a leg he put across its line after reacting (shot block plan 3d); the rest his torso or stride"},
  TargetSpec{"LAB-DEFL", "shot_deflections_per_shot", "Shot deflections off a body", "", NO_BAND, Info, "orientation", Number, Engine, "per open-play shot: off a Binturong body but no block (a team-mate's, or anyone's once it was blocked; shot block plan step 4)"},
  TargetSpec{"LAB-GKBODY", "keeper_body_hits_per_shot", "Shots off the keeper", "", NO_BAND, Info, "orientation", Number, Engine, "per open-play shot: meeting the defending keeper's body, arms or hands in his area (goalkeeper plan steps 1-2)"},
  TargetSpec{"LAB-GKBODYIN", "keeper_body_still_on_goal_per_shot", "  ...still going in", "", NO_BAND, Info, "orientation", Number, Engine, "per open-play shot: off him and still on target"},
  TargetSpec{"LAB-GKWENT", "keeper_hands_went_per_shot", "Keeper's hands went for it", "", NO_BAND, Info, "orientation", Number, Engine, "per open-play shot: his hands went for its line once he had reacted (step 2)"},
  TargetSpec{"LAB-GKTOG", "keeper_hands_together_share", "  ...both hands", "", NO_BAND, Info, "orientation", Percent, Engine, "of those: both hands together (else the nearer one alone)"},
  TargetSpec{"LAB-GKLATE", "keeper_hands_late_share", "  ...ball there first", "", NO_BAND, Info, "orientation", Percent, Engine, "of those: the ball at his hand's spot before the hand could be"},
  TargetSpec{"LAB-GKDIVE", "keeper_dives_per_shot", "Keeper dived", "", NO_BAND, Info, "orientation", Number, Engine, "per open-play shot: he dived for it (its line beyond his standing reach; penalty guesses too; step 3)"},
  TargetSpec{"LAB-GKDIVELATE", "keeper_dives_late_share", "  ...ball there before full stretch", "", NO_BAND, Info, "orientation", Percent, Engine, "of dives: the ball where he aimed before his full stretch"},
  TargetSpec{"LAB-GKHANDS", "keeper_hand_shots_per_shot", "Shots off a keeper's hand", "", NO_BAND, Info, "orientation", Number, Engine, "per open-play shot: met his hand first (step 4)"},
  TargetSpec{"LAB-GKBOTH", "keeper_both_hands_share", "  ...both hands on it", "", NO_BAND, Info, "orientation", Percent, Engine, "of those: his other hand on it too"},
  TargetSpec{"LAB-GKCATCH", "keeper_catch_share", "  ...went to catch", "", NO_BAND, Info, "orientation", Percent, Engine, "of those: both hands and within his Handling limit for its pace and his stretch"},
  TargetSpec{"LAB-GKHELD", "keeper_held_share", "  ...held", "", NO_BAND, Info, "orientation", Percent, Engine, "of those: caught and held"},
  TargetSpec{"LAB-GKFUMBLE", "keeper_fumble_share", "  ...catches fumbled", "", NO_BAND, Info, "orientation", Percent, Engine, "of catches: fumbled, off his hands"},
  TargetSpec{"LAB-GKPARRY", "keeper_parry_share", "  ...parried", "", NO_BAND, Info, "orientation", Percent, Engine, "of shots off a hand: parried, the face turned away from goal"},
  TargetSpec{"LAB-GKCLAIM", "keeper_claims_per_match", "Keeper claims per match", "", NO_BAND, Info, "orientation", Number, Engine, "both keepers: his hands met a high ball (step 7; an Aardvark keeper's claim roll too)"},
  TargetSpec{"LAB-GKCLHELD", "keeper_claim_held_share", "  ...held", "", NO_BAND, Info, "orientation", Percent, Engine, "of claims: caught and held"},
  TargetSpec{"LAB-GKPUNCH", "keeper_claim_punch_share", "  ...punched", "", NO_BAND, Info, "orientation", Percent, Engine, "of claims: punched (an opponent within 2.0-0.5 m, by Command of Area)"},
  TargetSpec{"LAB-GKCLPARRY", "keeper_claim_parry_share", "  ...off his hands", "", NO_BAND, Info, "orientation", Percent, Engine, "of claims: meant to catch, sprang off (over his limit, one hand, or fumbled)"},
  TargetSpec{"LAB-GKSMOTHER", "keeper_smothers_per_match", "Keeper smother dives per match", "", NO_BAND, Info, "orientation", Number, Engine, "both keepers: dived at a loose ball an attacker would reach first on foot (step 8)"},
  TargetSpec{"LAB-GKSMHELD", "keeper_smother_held_share", "  ...held", "", NO_BAND, Info, "orientation", Percent, Engine, "loose balls held off his hand, per smother dive"},
  TargetSpec{"LAB-GKINHANDS", "keeper_balls_in_hands_per_match", "Balls in a keeper's hands", "", NO_BAND, Info, "orientation", Number, Engine, "per match: saves held, claims, balls gathered (step 5)"},
  TargetSpec{"LAB-GKROLL", "keeper_roll_share", "  ...rolled out", "", NO_BAND, Info, "orientation", Percent, Engine, "of releases from his hands: rolled (a ground pass up to 25 m)"},
  TargetSpec{"LAB-GKTHROW", "keeper_throw_share", "  ...thrown overarm", "", NO_BAND, Info, "orientation", Percent, Engine, "of releases from his hands: thrown overarm (a ground pass beyond 25 m)"},
  TargetSpec{"LAB-GKPUNT", "keeper_punt_share", "  ...punted", "", NO_BAND, Info, "orientation", Percent, Engine, "of releases from his hands: punted (a lofted pass)"},
  TargetSpec{"LAB-GKHAND", "keeper_hand_hit_share", "  ...off a hand or arm", "", NO_BAND, Info, "orientation", Percent, Engine, "of shots off the keeper: his hands or arms (step 2); the rest his body"},
  TargetSpec{"LAB-BODY", "ball_body_hits_per_match", "Other balls off a body", "", NO_BAND, Info, "orientation", Number, Engine, "per match: passes, clearances, touches and loose balls meeting a Binturong body that didn't play them (pass block plan)"},
  TargetSpec{"LAB-BODYLEG", "ball_leg_hit_share", "  ...off a leg", "", NO_BAND, Info, "orientation", Percent, Engine, "of those: thigh, shin or foot (the rest torso or head)"},
  TargetSpec{"LAB-BODYHEAD", "ball_head_hit_share", "  ...off a head", "", NO_BAND, Info, "orientation", Percent, Engine, "of those: above the top of his shoulders, not headed (shot block plan 3i)"},
  TargetSpec{"LAB-BODYREACT", "ball_reaction_block_share", "  ...off a leg put out", "", NO_BAND, Info, "orientation", Percent, Engine, "of those: a leg he put across its line after reacting"},
  TargetSpec{"LAB-PASSBLK", "passes_blocked_per_match", "Passes blocked", "", NO_BAND, Info, "orientation", Number, Engine, "per match: opponents' passes meeting a Binturong body that didn't play them"},
  TargetSpec{"LAB-BODYDEFL", "ball_deflections_per_match", "Balls off a team-mate's body", "", NO_BAND, Info, "orientation", Number, Engine, "per match: off a body when his own side (or nobody) last played it"},
  TargetSpec{"LAB-DFK", "direct_free_kicks_per_match", "Direct free kicks", "", NO_BAND, Info, "orientation", Number, Engine, "per match: free kicks shot at goal directly (Binturong body)"},
  TargetSpec{"LAB-DFKON", "direct_free_kick_on_target_share", "  ...on target as struck", "", NO_BAND, Info, "orientation", Percent, Engine, "of those: its flight heading inside the frame as it left his foot"},
  TargetSpec{"LAB-DFKBLK", "direct_free_kick_blocked_share", "  ...blocked by a body", "", NO_BAND, Info, "orientation", Percent, Engine, "of those: blocked by an opponent's body (no wall formed yet)"},
  TargetSpec{"LAB-DFKGOAL", "direct_free_kick_goal_share", "  ...scored", "~6%", NO_BAND, Info, "orientation", Percent, Engine, "of those: scored from the shot (off a body still going in included)"},
  TargetSpec{"LAB-THRU","shot_torso_pass_throughs_per_shot", "Shots through a torso unmet", "0", NO_BAND, Info, "orientation", Number, Engine, "per open-play shot: its path, against the player's own move, went 5 cm inside a Binturong torso with no contact (check; shot block plan 3f)"},
  TargetSpec{"LAB-THRUIN", "shot_torso_inside_at_start_per_shot", "  ...already inside at a sub-step's start", "", NO_BAND, Info, "orientation", Number, Engine, "per open-play shot: of those, the ball already inside him as the sub-step began"},
  TargetSpec{"LAB-LEGCH", "shot_leg_chances_per_shot", "Defenders a shot passes within reach of", "", NO_BAND, Info, "orientation", Number, Engine, "per open-play shot: Binturong outfielders its line passes within 1.0 m of (shot block plan 3d), whatever its height or their speed"},
  TargetSpec{"LAB-LEGLATE", "shot_leg_too_late_share", "  ...ball there before he reacts", "", NO_BAND, Info, "orientation", Percent, Engine, "of those: the ball reached him before his reaction (Defending) was over; body and stride only"},
  TargetSpec{"LAB-LEGOUT", "shot_leg_out_share", "  ...leg put out", "", NO_BAND, Info, "orientation", Percent, Engine, "of those: he reacted and put a leg across its line"},
  TargetSpec{"LAB-LEGHIP", "shot_leg_above_hip_share", "  ...above his hip", "", NO_BAND, Info, "orientation", Percent, Engine, "of those: reacted; the ball passes above his hip (his torso only)"},
  TargetSpec{"LAB-LEGHGT", "shot_leg_reach_height_share", "  ...out of reach at that height", "", NO_BAND, Info, "orientation", Percent, Engine, "of those: reacted; beyond his reach at the ball's height (1.0 m to the knee, narrowing to his body at the hip)"},
  TargetSpec{"LAB-LEGRUN", "shot_leg_reach_running_share", "  ...out of reach running", "", NO_BAND, Info, "orientation", Percent, Engine, "of those: reacted; in reach standing, not at his speed (full to 40% of top speed, half at a sprint)"},
  TargetSpec{"LAB-LEGBEH", "shot_leg_behind_share", "  ...behind him", "", NO_BAND, Info, "orientation", Percent, Engine, "of those: reacted; the line passes in his dead zone"},
  TargetSpec{"LAB-LEGPAST", "shot_leg_past_share", "  ...past him as he reacted", "", NO_BAND, Info, "orientation", Percent, Engine, "of those: reacted, but the ball was already past the nearest point (moving with him)"},
  TargetSpec{"LAB-LEGBUSY", "shot_leg_feet_busy_share", "  ...feet busy", "", NO_BAND, Info, "orientation", Percent, Engine, "of those: reacted in a jump, planted for a thigh touch or with his foot committed"},
  TargetSpec{"LAB-LEGHIT", "shot_leg_out_blocks_share", "Legs put out that blocked it", "", NO_BAND, Info, "orientation", Percent, Engine, "of legs put across a shot's line: those it hit (a reaction block)"},
  TargetSpec{"LAB-POSS", "possession_spread", "Possession spread |home - 50| (pts)", "~9-10 pts", NO_BAND, Info, "orientation", Number, Match, ""},
  TargetSpec{"LAB-ADDED", "added_time_minutes", "Added time per match (min)", "~10.5", NO_BAND, Info, "orientation", Number, Engine, ""},
  TargetSpec{"LAB-INJ", "injuries_per_team", "In-match injuries per team", "~0.2-0.3", NO_BAND, Info, "orientation", Number, Engine, ""},
  TargetSpec{"LAB-DIST-H2", "second_half_distance_change", "2nd vs 1st half distance per minute", "-3 to -10%", NO_BAND, Info, "orientation", Percent, Engine, "full-match outfield starters"},
  TargetSpec{"LAB-FUEL", "end_fuel_mean", "Fuel (condition) left at full time", "", NO_BAND, Info, "orientation", Number, Engine, "outfield players on the pitch; floor 0.35 (Aardvark body), 0 (Binturong)"},
  TargetSpec{"LAB-FUEL0", "end_fuel_floor_share", "Players at the fuel floor at full time", "", NO_BAND, Info, "orientation", Percent, Engine, "outfield players on the pitch"},
  // ---- Fatigue INFO (fatigue plan, step 0) ----------------------------------
  TargetSpec{"FAT-FSD", "end_fuel_sd", "Fuel at full time: spread (sd)", "", NO_BAND, Info, "fatigue", Number, Engine, "outfield players on the pitch"},
  TargetSpec{"FAT-FMIN", "end_fuel_min_mean", "Fuel at full time: lowest player per match", "", NO_BAND, Info, "fatigue", Number, Engine, "mean over matches"},
  TargetSpec{"FAT-F60", "end_fuel_60_share", "Finishers with 60%+ fuel (Fresh)", "", NO_BAND, Info, "fatigue", Percent, Engine, "outfield players on the pitch"},
  TargetSpec{"FAT-F30", "end_fuel_30_60_share", "Finishers with 30-60% fuel", "", NO_BAND, Info, "fatigue", Percent, Engine, "toward Tired"},
  TargetSpec{"FAT-F10", "end_fuel_10_30_share", "Finishers with 10-30% fuel", "", NO_BAND, Info, "fatigue", Percent, Engine, "toward Exhausted"},
  TargetSpec{"FAT-F0", "end_fuel_below_10_share", "Finishers below 10% fuel", "", NO_BAND, Info, "fatigue", Percent, Engine, "toward Spent"},
  TargetSpec{"FAT-FE", "end_fuel_empty_share", "Finishers with an empty tank", "", NO_BAND, Info, "fatigue", Percent, Engine, "Spent"},
  TargetSpec{"FAT-WALK", "walk_per_outfielder_km", "Walking < 7.2 km/h per outfielder (km)", "", NO_BAND, Info, "fatigue", Number, Engine, "full-match outfield starters; Bradley 2009 bands"},
  TargetSpec{"FAT-JOG", "jog_per_outfielder_km", "Jogging 7.2-14.4 km/h per outfielder (km)", "", NO_BAND, Info, "fatigue", Number, Engine, "full-match outfield starters"},
  TargetSpec{"FAT-RUN", "run_per_outfielder_km", "Running 14.4-19.8 km/h per outfielder (km)", "", NO_BAND, Info, "fatigue", Number, Engine, "full-match outfield starters"},
  TargetSpec{"FAT-HSR", "hsr_per_outfielder_km", "High-speed running 19.8-25.2 km/h per outfielder (km)", "", NO_BAND, Info, "fatigue", Number, Engine, "full-match outfield starters"},
  TargetSpec{"FAT-PEAK", "peak5_next_change", "Running > 14.4 km/h in the 5' after a player's peak 5' vs his mean", "-12% (Mohr 2003)", NO_BAND, Info, "fatigue", Percent, Engine, "full-match outfield starters; peak in a half's last 5' skipped"},
  TargetSpec{"FAT-NRG", "energy_per_outfielder_kj", "Metabolic energy per outfielder (kJ/kg)", "~61 (Osgnach 2010)", NO_BAND, Info, "fatigue", Number, Engine, "full-match outfield starters; net of standing"},
  TargetSpec{"FAT-ANA", "anaerobic_energy_share", "Energy above critical speed's power", "", NO_BAND, Info, "fatigue", Percent, Engine, "full-match outfield starters"},
  TargetSpec{"MOD-WT", "mode_walk_time_share", "Time walking (own bands)", "", NO_BAND, Info, "modes", Percent, Engine, "full-match outfield starters; below his walk-run transition"},
  TargetSpec{"MOD-JT", "mode_jog_time_share", "Time jogging (own bands)", "", NO_BAND, Info, "modes", Percent, Engine, "transition to 0.82 x critical speed"},
  TargetSpec{"MOD-RT", "mode_run_time_share", "Time running (own bands)", "", NO_BAND, Info, "modes", Percent, Engine, "0.82 x critical speed to critical speed"},
  TargetSpec{"MOD-ST", "mode_sprint_time_share", "Time sprinting (own bands)", "", NO_BAND, Info, "modes", Percent, Engine, "above critical speed"},
  TargetSpec{"MOD-WK", "mode_walk_km", "Walking per outfielder (km, own bands)", "", NO_BAND, Info, "modes", Number, Engine, "full-match outfield starters"},
  TargetSpec{"MOD-JK", "mode_jog_km", "Jogging per outfielder (km, own bands)", "", NO_BAND, Info, "modes", Number, Engine, ""},
  TargetSpec{"MOD-RK", "mode_run_km", "Running per outfielder (km, own bands)", "", NO_BAND, Info, "modes", Number, Engine, ""},
  TargetSpec{"MOD-SK", "mode_sprint_km", "Above critical speed per outfielder (km, own bands)", "", NO_BAND, Info, "modes", Number, Engine, ""},
  TargetSpec{"FAT-BRK", "braking_energy_share", "Energy spent while slowing down", "", NO_BAND, Info, "fatigue", Percent, Engine, "full-match outfield starters"},
  TargetSpec{"FAT-BRKA", "braking_anaerobic_share", "Above-CS energy spent while slowing down", "", NO_BAND, Info, "fatigue", Percent, Engine, "share of the energy above CS power"},
  TargetSpec{"FAT-EMP", "emptied_share", "Full-match players whose tank ran out", "", NO_BAND, Info, "fatigue", Percent, Engine, "full-match outfield starters"},
  TargetSpec{"FAT-EMPM", "empty_minute_mean", "Elapsed minute the tank ran out", "", NO_BAND, Info, "fatigue", Number, Engine, "those whose tank ran out"},
  TargetSpec{"FAT-HIR", "hir_per_outfielder_km", "Running > 14.4 km/h per outfielder (km)", "~1.8-3.1 by position (Bradley 2009)", NO_BAND, Info, "fatigue", Number, Engine, "full-match outfield starters; the research's high-intensity running"},
  TargetSpec{"LAB-ABND", "abandoned_share", "Abandoned matches", "0%", NO_BAND, Info, "sanity", Percent, Engine, "excluded from all other match metrics"},
  // ---- Player behaviour INFO (brain comparisons; no spec band) -------------
  TargetSpec{"LAB-TKL", "tackles_per_team", "Tackles attempted per team", "", NO_BAND, Info, "behaviour", Number, Engine, ""},
  TargetSpec{"LAB-TKLW", "tackle_success", "Tackles won share", "", NO_BAND, Info, "behaviour", Percent, Engine, ""},
  TargetSpec{"LAB-INT", "interceptions_per_team", "Interceptions per team", "", NO_BAND, Info, "behaviour", Number, Engine, ""},
  TargetSpec{"LAB-CLR", "clearances_per_team", "Clearances per team", "", NO_BAND, Info, "behaviour", Number, Engine, ""},
  TargetSpec{"LAB-AER", "aerial_duels_per_match", "Aerial duels per match (both teams)", "", NO_BAND, Info, "behaviour", Number, Engine, "won + lost over all players, so each duel may count twice"},
  TargetSpec{"LAB-HSQ", "header_sequences_per_match", "Header sequences per match", "", NO_BAND, Info, "behaviour", Number, Engine, "headers one after another until someone has the ball or play stops"},
  TargetSpec{"LAB-HSN", "headers_per_sequence", "Headers per sequence", "", NO_BAND, Info, "behaviour", Number, Engine, ""},
  TargetSpec{"LAB-HST", "head_tennis_share", "Sequences of 3+ headers", "", NO_BAND, Info, "behaviour", Percent, Engine, "head tennis"},
  TargetSpec{"LAB-HSM", "longest_header_sequence", "Longest header sequence per match", "", NO_BAND, Info, "behaviour", Number, Engine, ""},
  TargetSpec{"LAB-HSC", "header_sequences_to_control", "Sequences ending with a side in control", "", NO_BAND, Info, "behaviour", Percent, Engine, "the rest end with play stopped"},
  TargetSpec{"LAB-HSK", "header_sequences_kept", "Of those, kept by the side heading last", "", NO_BAND, Info, "behaviour", Percent, Engine, ""},
  TargetSpec{"LAB-PRS", "pressures_per_team", "Pressures per team", "", NO_BAND, Info, "behaviour", Number, Engine, ""},
  TargetSpec{"LAB-KEY", "key_passes_per_team", "Key passes per team", "", NO_BAND, Info, "behaviour", Number, Engine, ""},
  TargetSpec{"LAB-SAV", "saves_per_team", "Saves per team", "", NO_BAND, Info, "behaviour", Number, Engine, ""},
  TargetSpec{"LAB-TCH", "touches_per_team", "Touches per team", "", NO_BAND, Info, "behaviour", Number, Engine, ""},
  TargetSpec{"LAB-HI", "high_intensity_per_outfielder_km", "High-intensity distance per outfielder (km)", "", NO_BAND, Info, "behaviour", Number, Engine, "full-match outfield starters"},
  TargetSpec{"LAB-SPR", "sprint_per_outfielder_km", "Sprint distance per outfielder (km)", "", NO_BAND, Info, "behaviour", Number, Engine, "full-match outfield starters"},
  TargetSpec{"LAB-SPRN", "sprints_per_outfielder", "Sprints per outfielder", "", NO_BAND, Info, "behaviour", Number, Engine, "full-match outfield starters"},
  // ---- Contests by side INFO (body comparisons; --bodies HOME,AWAY) --------
  TargetSpec{"CON-TO", "take_ons_per_team", "Take-ons per team", "", NO_BAND, Info, "contests", Number, Engine, ""},
  TargetSpec{"CON-TOH", "take_on_success_home", "Take-on success, home carrier", "", NO_BAND, Info, "contests", Percent, Engine, "defender beaten"},
  TargetSpec{"CON-TOA", "take_on_success_away", "Take-on success, away carrier", "", NO_BAND, Info, "contests", Percent, Engine, "defender beaten"},
  TargetSpec{"CON-TKH", "tackle_duel_success_home", "Tackle duel won, home tackler", "", NO_BAND, Info, "contests", Percent, Engine, "the duel roll, fouls included"},
  TargetSpec{"CON-TKA", "tackle_duel_success_away", "Tackle duel won, away tackler", "", NO_BAND, Info, "contests", Percent, Engine, "the duel roll, fouls included"},
  TargetSpec{"CON-TKF", "free_ball_tackles_per_team", "Free-ball tackles per team", "", NO_BAND, Info, "contests", Number, Engine, "a ball outside the carrier's reach, played by an opponent"},
  TargetSpec{"CON-TKFW", "free_ball_tackle_success", "Free-ball tackles won share", "", NO_BAND, Info, "contests", Percent, Engine, "the tackler's side next had it under control"},
  TargetSpec{"CON-TKC", "contest_tackles_per_team", "Contest tackles per team", "", NO_BAND, Info, "contests", Number, Engine, "the carrier's ball inside both reaches, played by the tackler"},
  TargetSpec{"CON-TKCW", "contest_tackle_success", "Contest tackles won share", "", NO_BAND, Info, "contests", Percent, Engine, "the tackler's side next had it under control"},
  TargetSpec{"CON-AIRC", "carrier_air_swings_per_team", "Carriers swinging at nothing per team", "", NO_BAND, Info, "contests", Number, Engine, "the tackler played it inside his commit moment"},
  TargetSpec{"CON-AIRT", "tackler_air_swings_per_team", "Tacklers swinging at nothing per team", "", NO_BAND, Info, "contests", Number, Engine, "the carrier played it first"},
  TargetSpec{"CON-CMT", "commit_misses_per_team", "First touches missed, path too new per team", "", NO_BAND, Info, "contests", Number, Engine, "the ball's path changed under 0.2 s before (the commit rule)"},
  TargetSpec{"CON-BLK", "blocks_per_team", "Blocks per team", "", NO_BAND, Info, "contests", Number, Engine, "both feet on the ball (a contest's rows 3 and 5)"},
  TargetSpec{"CON-BLKC", "blocks_committed_share", "Blocks with the carrier's planned touch", "", NO_BAND, Info, "contests", Percent, Engine, "row 3; the rest his reacting foot (row 5)"},
  TargetSpec{"CON-BLKP", "blocks_popped_share", "Blocks popped up", "", NO_BAND, Info, "contests", Percent, Engine, "a strong, near-equal squeeze"},
  TargetSpec{"CON-GOIN", "go_ins_per_team", "Defenders going into a contest per team", "", NO_BAND, Info, "contests", Number, Engine, "steps his brain sent him in (tackling plan step 5)"},
  TargetSpec{"CON-GOFF", "go_ins_free_foot_share", "Going in on the carrier's free foot", "", NO_BAND, Info, "contests", Percent, Engine, "a misread: no touch due, nothing shown (row 5)"},
  TargetSpec{"CON-HOLD", "holds_per_team", "Steps held in a contest per team", "", NO_BAND, Info, "contests", Number, Engine, "the ball in both reaches, he held"},
  TargetSpec{"CON-INW", "intents_win_per_team", "Tackles meant to win it per team", "", NO_BAND, Info, "contests", Number, Engine, "his brain's intent"},
  TargetSpec{"CON-INC", "intents_clear_per_team", "Tackles meant to knock it away per team", "", NO_BAND, Info, "contests", Number, Engine, "own third, at a stretch, or the last man"},
  TargetSpec{"CON-INO", "intents_control_per_team", "Free balls simply controlled per team", "", NO_BAND, Info, "contests", Number, Engine, "a free ball well away from the carrier"},
  TargetSpec{"COL-ALL", "collisions_per_match", "Run-ins per match", "", NO_BAND, Info, "contests", Number, Engine, "players running into each other (collision plan; Binturong bodies)"},
  TargetSpec{"COL-J2", "collisions_jolt_two_per_match", "Run-ins jolting 2 m/s or more per match", "", NO_BAND, Info, "contests", Number, Engine, "either player"},
  TargetSpec{"COL-J4", "collisions_jolt_four_per_match", "Run-ins jolting 4 m/s or more per match", "", NO_BAND, Info, "contests", Number, Engine, "either player: a big hit becomes possible"},
  TargetSpec{"COL-STU", "collision_stumbles_per_match", "Players stumbling from a run-in per match", "", NO_BAND, Info, "contests", Number, Engine, "kept his feet, lost his balance"},
  TargetSpec{"COL-FAL", "collision_falls_per_match", "Players knocked over per match", "", NO_BAND, Info, "contests", Number, Engine, "slips included"},
  TargetSpec{"COL-SLP", "collision_slips_per_match", "Stumbles that slipped into a fall per match", "", NO_BAND, Info, "contests", Number, Engine, "his footing went"},
  TargetSpec{"COL-KEU", "collision_knocks_by_ends_up_share", "Stumbles and falls decided by where he ends up", "", NO_BAND, Info, "contests", Percent, Engine, "the rest by how sudden"},
  TargetSpec{"COL-KSJ", "collision_knocks_small_jolt_share", "Stumbles and falls from a jolt under 1 m/s", "", NO_BAND, Info, "contests", Percent, Engine, "a small knock at the edge of what he can run that way"},
  TargetSpec{"COL-BIG", "collision_big_hits_per_match", "Big hits per match", "", NO_BAND, Info, "contests", Number, Engine, "falls kept down 5-10 s at least"},
  TargetSpec{"COL-LOS", "collision_falls_losing_ball_per_match", "Falls costing the ball per match", "", NO_BAND, Info, "contests", Number, Engine, "it runs on loose"},
  TargetSpec{"COL-DPF", "collision_down_seconds_per_fall", "Seconds down per fall", "", NO_BAND, Info, "contests", Number, Engine, "sliding, rolling, getting up"},
  TargetSpec{"COL-DWN", "collision_down_play_seconds_per_match", "Player-seconds down in open play per match", "", NO_BAND, Info, "contests", Number, Engine, "his side a man short"},
  TargetSpec{"COL-D15", "collision_downs_fifteen_per_match", "Spells down of 15 s or more per match", "", NO_BAND, Info, "contests", Number, Engine, ""},
  TargetSpec{"COL-GUB", "collision_get_ups_for_ball_per_match", "Jumped up for the ball per match", "", NO_BAND, Info, "contests", Number, Engine, "ball within 15 m, still to be won"},
  TargetSpec{"COL-GUS", "collision_get_ups_in_stoppage_per_match", "Got up in a stoppage per match", "", NO_BAND, Info, "contests", Number, Engine, ""},
  TargetSpec{"COL-GUG", "collision_get_ups_gingerly_per_match", "Got up gingerly as play ran on per match", "", NO_BAND, Info, "contests", Number, Engine, "after 15 s down"},
  TargetSpec{"COL-GUF", "collision_get_ups_fixed_per_match", "Fixed get-ups per match", "", NO_BAND, Info, "contests", Number, Engine, "a brain without the get-up cues"},
  TargetSpec{"COL-BOF", "collision_ball_off_fallen_per_match", "Ball off a fallen body per match", "", NO_BAND, Info, "contests", Number, Engine, "his last touch for restarts"},
  TargetSpec{"COL-K0", "collision_knocks_situation_0_share", "Knocks from: a re-hit", "", NO_BAND, Info, "contests", Percent, Engine, "the same pair within 1 s"},
  TargetSpec{"COL-K1", "collision_knocks_situation_1_share", "Knocks from: both at the ball", "", NO_BAND, Info, "contests", Percent, Engine, "ball within 2 m of the contact"},
  TargetSpec{"COL-K2", "collision_knocks_situation_2_share", "Knocks from: his run aimed at the other", "", NO_BAND, Info, "contests", Percent, Engine, "his target within 1.5 m of him"},
  TargetSpec{"COL-K3", "collision_knocks_situation_3_share", "Knocks from: an overtake under way", "", NO_BAND, Info, "contests", Percent, Engine, "he was going round the other"},
  TargetSpec{"COL-K4", "collision_knocks_situation_4_share", "Knocks from: a race not overtaken", "", NO_BAND, Info, "contests", Percent, Engine, "same spot, same way, no way round planned"},
  TargetSpec{"COL-K5", "collision_knocks_situation_5_share", "Knocks from: either standing or slow", "", NO_BAND, Info, "contests", Percent, Engine, "under 1 m/s"},
  TargetSpec{"COL-K6", "collision_knocks_situation_6_share", "Knocks from: paths crossing or head-on", "", NO_BAND, Info, "contests", Percent, Engine, "over 45 degrees apart"},
  TargetSpec{"COL-K7", "collision_knocks_situation_7_share", "Knocks from: anything else", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-I-hold_shape", "collision_knocks_intent_hold_shape_share", "Knocked while holding shape", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-I-carry_ball", "collision_knocks_intent_carry_ball_share", "Knocked while carrying the ball", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-I-offer_support", "collision_knocks_intent_offer_support_share", "Knocked while offering support", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-I-receive_pass", "collision_knocks_intent_receive_pass_share", "Knocked while receiving a pass", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-I-run_in_behind", "collision_knocks_intent_run_in_behind_share", "Knocked while running in behind", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-I-attack_box", "collision_knocks_intent_attack_box_share", "Knocked while attacking the box", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-I-overlap", "collision_knocks_intent_overlap_share", "Knocked while overlapping", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-I-press_ball", "collision_knocks_intent_press_ball_share", "Knocked while pressing the ball", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-I-cover_press", "collision_knocks_intent_cover_press_share", "Knocked while covering the press", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-I-block_passing_lane", "collision_knocks_intent_block_passing_lane_share", "Knocked while blocking a passing lane", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-I-mark_opponent", "collision_knocks_intent_mark_opponent_share", "Knocked while marking", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-I-claim_loose_ball", "collision_knocks_intent_claim_loose_ball_share", "Knocked while claiming a loose ball", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-I-recover_shape", "collision_knocks_intent_recover_shape_share", "Knocked while recovering shape", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-I-goalkeep", "collision_knocks_intent_goalkeep_share", "Knocked while keeping goal", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-CH", "collision_chase_knocks_share", "Knocks with a loose-ball chaser in them", "", NO_BAND, Info, "contests", Percent, Engine, "either player claiming a loose ball"},
  TargetSpec{"COL-CHK", "collision_chase_knocks_on_chaser_share", "Of those, the chaser knocked", "", NO_BAND, Info, "contests", Percent, Engine, "rather than the other"},
  TargetSpec{"COL-CW-claim_loose_ball", "collision_chase_knocks_with_claim_loose_ball_share", "Chase knocks, the other an opponent also chasing it", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-CW-team_mate_chasing", "collision_chase_knocks_with_team_mate_chasing_share", "Chase knocks, the other a team-mate also chasing it", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-CW-hold_shape", "collision_chase_knocks_with_hold_shape_share", "Chase knocks, the other holding shape", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-CW-carry_ball", "collision_chase_knocks_with_carry_ball_share", "Chase knocks, the other carrying the ball", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-CW-offer_support", "collision_chase_knocks_with_offer_support_share", "Chase knocks, the other offering support", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-CW-receive_pass", "collision_chase_knocks_with_receive_pass_share", "Chase knocks, the other receiving a pass", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-CW-run_in_behind", "collision_chase_knocks_with_run_in_behind_share", "Chase knocks, the other running in behind", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-CW-attack_box", "collision_chase_knocks_with_attack_box_share", "Chase knocks, the other attacking the box", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-CW-overlap", "collision_chase_knocks_with_overlap_share", "Chase knocks, the other overlapping", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-CW-press_ball", "collision_chase_knocks_with_press_ball_share", "Chase knocks, the other pressing the ball", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-CW-cover_press", "collision_chase_knocks_with_cover_press_share", "Chase knocks, the other covering the press", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-CW-block_passing_lane", "collision_chase_knocks_with_block_passing_lane_share", "Chase knocks, the other blocking a passing lane", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-CW-mark_opponent", "collision_chase_knocks_with_mark_opponent_share", "Chase knocks, the other marking", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-CW-recover_shape", "collision_chase_knocks_with_recover_shape_share", "Chase knocks, the other recovering shape", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"COL-CW-goalkeep", "collision_chase_knocks_with_goalkeep_share", "Chase knocks, the other keeping goal", "", NO_BAND, Info, "contests", Percent, Engine, ""},
  TargetSpec{"WAY-SCHA", "way_rounds_started_chase_per_match", "Ways round started per match: chasers", "", NO_BAND, Info, "contests", Number, Engine, "way-round plan"},
  TargetSpec{"WAY-RCHA", "way_round_refreshes_chase_per_match", "Plan refreshes going round per match: chasers", "", NO_BAND, Info, "contests", Number, Engine, ""},
  TargetSpec{"WAY-SHOL", "way_rounds_started_hold_per_match", "Ways round started per match: players moving to hold shape", "", NO_BAND, Info, "contests", Number, Engine, "way-round plan"},
  TargetSpec{"WAY-RHOL", "way_round_refreshes_hold_per_match", "Plan refreshes going round per match: players moving to hold shape", "", NO_BAND, Info, "contests", Number, Engine, ""},
  TargetSpec{"WAY-SSUP", "way_rounds_started_support_per_match", "Ways round started per match: players offering support", "", NO_BAND, Info, "contests", Number, Engine, "way-round plan"},
  TargetSpec{"WAY-RSUP", "way_round_refreshes_support_per_match", "Plan refreshes going round per match: players offering support", "", NO_BAND, Info, "contests", Number, Engine, ""},
  TargetSpec{"WAY-SREC", "way_rounds_started_recover_per_match", "Ways round started per match: players recovering shape", "", NO_BAND, Info, "contests", Number, Engine, "way-round plan"},
  TargetSpec{"WAY-RREC", "way_round_refreshes_recover_per_match", "Plan refreshes going round per match: players recovering shape", "", NO_BAND, Info, "contests", Number, Engine, ""},
  TargetSpec{"GW-SLOW", "give_way_slows_per_match", "Plans easing off to give way per match", "", NO_BAND, Info, "contests", Number, Engine, "someone with the right of way crossing ahead"},
  TargetSpec{"GW-ROUND", "give_way_rounds_per_match", "Plans going round alone to give way per match", "", NO_BAND, Info, "contests", Number, Engine, "head-on or the same way"},
  TargetSpec{"GW-HOLD", "give_way_holds_per_match", "Plans holding the line with the right of way per match", "", NO_BAND, Info, "contests", Number, Engine, ""},
  TargetSpec{"CON-AERH", "aerial_home_share", "Contested aerial duels won by home", "", NO_BAND, Info, "contests", Percent, Engine, "both sides within reach"},
  TargetSpec{"CON-CLMH", "keeper_claim_success_home", "Keeper claims held, home keeper", "", NO_BAND, Info, "contests", Percent, Engine, "the rest punched clear"},
  TargetSpec{"CON-CLMA", "keeper_claim_success_away", "Keeper claims held, away keeper", "", NO_BAND, Info, "contests", Percent, Engine, "the rest punched clear"},
  // ---- Movement jitter INFO (outfield players, open play) -------------------
  TargetSpec{"JIT-MOVE", "jitter_target_move_m", "Target move per scheduled refresh (m)", "", NO_BAND, Info, "jitter", Number, Engine, "every 0.2 s"},
  TargetSpec{"JIT-MOVS", "jitter_moving_share", "Scheduled refreshes moving the target > 0.5 m", "", NO_BAND, Info, "jitter", Percent, Engine, ""},
  TargetSpec{"JIT-REV", "jitter_reversal_share", "Target moves that turn back on the previous move", "", NO_BAND, Info, "jitter", Percent, Engine, "both moves > 0.5 m, more than 90 degrees apart"},
  TargetSpec{"JIT-REVM", "jitter_reversals_per_minute", "Target reversals per player-minute", "", NO_BAND, Info, "jitter", Number, Engine, ""},
  TargetSpec{"JIT-SWM", "jitter_switches_per_minute", "Intent switches per player-minute", "", NO_BAND, Info, "jitter", Number, Engine, "scheduled refreshes"},
  TargetSpec{"JIT-FLIP", "jitter_flip_back_share", "Intent switches undone within 1 s", "", NO_BAND, Info, "jitter", Percent, Engine, "back to the intent held before"},
  TargetSpec{"JIT-EVT", "jitter_event_refreshes_per_minute", "Forced replans per player-minute", "", NO_BAND, Info, "jitter", Number, Engine, "ball changed hands, or a pass or shot started or ended"},
  TargetSpec{"JIT-EVTM", "jitter_event_target_move_m", "Target move per forced replan (m)", "", NO_BAND, Info, "jitter", Number, Engine, ""},
  TargetSpec{"JIT-EFF", "jitter_run_efficiency", "Run efficiency (ground gained / distance run, 2 s windows)", "", NO_BAND, Info, "jitter", Percent, Engine, ""},
  TargetSpec{"JIT-TURN", "jitter_sharp_turn_share", "Runs turned > 90 degrees within 0.2 s", "", NO_BAND, Info, "jitter", Percent, Engine, "both speeds > 1 m/s"},
  TargetSpec{"JIT-SURGE", "jitter_speed_reversals_per_running_minute", "Speed surges (rise/fall reversals) per running minute", "", NO_BAND, Info, "jitter", Number, Engine, "above 2 m/s; changes over 0.5 m/s^2 per step"},
  // ---- World (season runs) --------------------------------------------------
  TargetSpec{"CT-W01", "", "Output peak age by position", "FW~25, MF 25-27, DF~27", NO_BAND, Gate, "PEER", Number, World, "not measurable in one season: needs multi-season rating-by-age curves"},
  TargetSpec{"CT-W02", "", "Physical peaks", "25.7 / 24.8 / 26.0", NO_BAND, Monitor, "PEER", Number, World, "not measurable yet"},
  TargetSpec{"CT-W03", "", "Peak market-value age", "26-30", Band{26.0, 30.0}, Monitor, "PEER", Number, World, "not measurable yet"},
  TargetSpec{"CT-W04", "mean_age_top_squads", "Mean age of top-division squads", "~26.5", Band{24.9, 26.5}, Monitor, "PEER", Number, World, "club mean ages at season end, top tier"},
  TargetSpec{"CT-W05", "injuries_per_player_season", "Injuries per player-season", "~2.0", Band{1.6, 2.4}, Gate, "PEER", Number, World, "injury onsets (daily poll) / mean registered players"},
  TargetSpec{"CT-W06", "", "Match / training injury incidence", "21-27.5 / 3.5-4.1 per 1000 h", NO_BAND, Gate, "PEER", Number, World, "not measurable yet: exposure hours not tracked"},
  TargetSpec{"CT-W07", "hamstring_share", "Hamstring share of injuries", "19-24%", Band{0.19, 0.24}, Monitor, "PEER", Percent, World, "strain + tightness"},
  TargetSpec{"CT-W08", "hamstring_layoff_median", "Layoff median: hamstring (days)", "13", Band{10.0, 17.0}, Gate, "PEER", Number, World, "hamstring strain"},
  TargetSpec{"CT-W08B", "ankle_layoff_median", "Layoff median: ankle (days)", "8", Band{6.0, 11.0}, Gate, "PEER", Number, World, "ankle sprain"},
  TargetSpec{"CT-W08C", "acl_layoff_median", "Layoff median: ACL (days)", "205", Band{160.0, 250.0}, Gate, "PEER", Number, World, "knee ACL"},
  TargetSpec{"CT-W09", "", "Re-injury share / duration multiplier", "12%; x1.33", NO_BAND, Monitor, "PEER", Number, World, "not measurable yet"},
  TargetSpec{"CT-W10", "", "Muscle-injury risk with <=4 days recovery", "RR 1.32", NO_BAND, Monitor, "PEER", Number, World, "not measurable yet"},
  TargetSpec{"CT-W11", "", "Residual impairment 72 h after a match", "2-4%", NO_BAND, Monitor, "PEER", Number, World, "not measurable yet"},
  TargetSpec{"CT-W12", "", "Scholar outcomes", "4% / 6%", NO_BAND, Monitor, "PEER", Number, World, "not measurable in one season"},
  TargetSpec{"CT-W13", "", "Senior birth-quarter shares", "31/26/24/20%", NO_BAND, Monitor, "PEER", Number, World, "not measurable yet: birth dates not exposed"},
  TargetSpec{"CT-W14", "fee_transfer_share", "Share of transfers involving a fee", "17.7%", Band{0.15, 0.25}, Gate, "REG", Percent, World, "permanent with fee / (permanent + free + pre-contract + loan)"},
  TargetSpec{"CT-W15", "", "Agent fees / fee spend", "~10%", Band{0.08, 0.12}, Monitor, "REG", Percent, World, "not measurable yet: agent fees not in the ledger by category"},
  TargetSpec{"CT-W16", "", "Unpaid share of squad fees at year end", "~33%", NO_BAND, Monitor, "REG", Percent, World, "not measurable yet"},
  TargetSpec{"CT-W17", "", "Fee model correlation with clean valuation", "r 0.80-0.85", NO_BAND, Monitor, "PEER", Number, World, "not measurable yet"},
  TargetSpec{"CT-W18", "", "Deadline-day share of window spend", "~11%", Band{0.10, 0.15}, Monitor, "PUB", Percent, World, "not measurable yet"},
  TargetSpec{"CT-W19", "", "Loan outcomes return/sold/re-loaned", "29.6/43.4/27.0%", NO_BAND, Monitor, "PEER", Percent, World, "not measurable in one season"},
  TargetSpec{"CT-W20", "", "Scouting discrimination d'", "0.8 / 1.3 / 2.1", NO_BAND, Monitor, "PEER", Number, World, "not measurable yet"},
  TargetSpec{"CT-W21", "wage_revenue_top", "Wage / revenue, top divisions", "65%", Band{0.57, 0.73}, Gate, "REG", Percent, World, "player wages / (matchday + TV + commercial + prize money), clubs as units"},
  TargetSpec{"CT-W22", "", "Revenue mix, richest league", "TV 46 / gate 14 / commercial 32%", NO_BAND, Monitor, "REG", Percent, World, "see LAB-REVMIX table"},
  TargetSpec{"CT-W23", "", "Central-distribution top/bottom ratio", "~1.6x", NO_BAND, Monitor, "PUB", Number, World, "not measurable yet"},
  TargetSpec{"CT-W24", "", "Insolvency hazard (top two tiers)", "0.6%/yr", Band{0.002, 0.012}, Gate, "REG/PEER", Percent, World, "not measurable yet: no insolvency events (see LAB-NEGCASH)"},
  TargetSpec{"CT-W25", "", "Clubs receiving owner equity per year", "~20%", NO_BAND, Monitor, "REG", Percent, World, "not measurable yet"},
  TargetSpec{"CT-W26", "", "Top-tier dismissals per season", "~9 of 20", Band{6.0, 12.0}, Gate, "PUB", Number, World, "not measurable yet: AI clubs have no managers"},
  TargetSpec{"CT-W27", "", "Dismissed-manager tenure", "< 1 year", Band{0.74, 0.87}, Monitor, "PUB", Number, World, "not measurable yet"},
  TargetSpec{"CT-W28", "wage_position_r2", "Wage-to-position R2 (annual)", "~0.45", Band{0.35, 0.55}, Gate, "PEER", Number, World, "ln(wage bill / league mean) vs -ln(p/(N+1-p)); bootstrap over clubs"},
  TargetSpec{"CT-W29", "", "Top-division membership persistence", "68% of decade", NO_BAND, Monitor, "PEER", Percent, World, "not measurable in one season"},
  TargetSpec{"CT-W30", "", "Result momentum after ability control", "<= 0", NO_BAND, Gate, "PEER", Number, World, "not measurable yet"},
  TargetSpec{"CT-W31", "", "New-manager effect", "~0", NO_BAND, Gate, "PEER", Number, World, "not measurable yet: AI clubs have no managers"},
  TargetSpec{"CT-W32", "", "New-signing share weakest vs strongest", "42% vs 31%", NO_BAND, Monitor, "PEER", Percent, World, "not measurable yet"},
  // ---- World INFO (no spec band) --------------------------------------------
  TargetSpec{"LAB-CHAMP", "champion_ppg", "Champion points per game", "~2.2-2.4 (big-5)", NO_BAND, Info, "orientation", Number, World, "league-seasons as units"},
  TargetSpec{"LAB-CHAMPPTS", "champion_points", "Champion points", "~86 over 38 games", NO_BAND, Info, "orientation", Number, World, ""},
  TargetSpec{"LAB-RELEG", "best_relegated_ppg", "Best relegated club points per game", "~0.9 (big-5)", NO_BAND, Info, "orientation", Number, World, "highest-placed relegated club"},
  TargetSpec{"LAB-RELEGPTS", "best_relegated_points", "Best relegated club points", "~34 over 38 games", NO_BAND, Info, "orientation", Number, World, ""},
  TargetSpec{"LAB-SCORER", "top_scorer_goals", "League top scorer goals", "~23-27 over 38 games", NO_BAND, Info, "orientation", Number, World, ""},
  TargetSpec{"LAB-SCORERPG", "top_scorer_goals_per_game", "Top scorer goals per team game", "~0.6-0.7", NO_BAND, Info, "orientation", Number, World, ""},
  TargetSpec{"LAB-PTSSD", "points_sd_ppg", "Points spread (SD of points per game)", "~0.45-0.5", NO_BAND, Info, "orientation", Number, World, "within league, then averaged"},
  TargetSpec{"LAB-GDSD", "goal_diff_sd_per_game", "Goal difference spread (SD per game)", "~0.6-0.7", NO_BAND, Info, "orientation", Number, World, "within league, then averaged"},
  TargetSpec{"LAB-REV", "median_club_revenue", "Median club revenue (season)", "-", NO_BAND, Info, "economy", Number, World, "matchday + TV + commercial + prize money"},
  TargetSpec{"LAB-NET", "median_club_net", "Median club net result (season)", "~0 or slightly negative", NO_BAND, Info, "orientation", Number, World, "all ledger categories except the opening balance"},
  TargetSpec{"LAB-WAGELOW", "wage_revenue_lower", "Wage / revenue, lower divisions", "up to 125%", NO_BAND, Info, "REG (CT-W21 note)", Percent, World, ""},
  TargetSpec{"LAB-NEGCASH", "negative_cash_share", "Clubs with negative cash at season end", "few %", NO_BAND, Info, "orientation", Percent, World, ""},
  TargetSpec{"LAB-XFER", "transfers_per_club", "Transfers per club per season", "~10-15 moves", NO_BAND, Info, "orientation", Number, World, "all recorded moves except releases and loan returns"},
  TargetSpec{"LAB-FEEPC", "fee_moves_per_club", "Fee-paying transfers per club per season", "-", NO_BAND, Info, "orientation", Number, World, ""},
  TargetSpec{"LAB-LOANPC", "loans_per_club", "Loans per club per season", "-", NO_BAND, Info, "orientation", Number, World, ""},
  TargetSpec{"LAB-SQUAD", "squad_size", "Squad size (season end)", "~25-30", NO_BAND, Info, "orientation", Number, World, ""},
  TargetSpec{"LAB-AGE", "player_age_mean", "Mean age, all club players", "~25-26", NO_BAND, Info, "orientation", Number, World, ""},
  TargetSpec{"LAB-YOUTH", "new_players_per_club", "New players per club per season (youth intake)", "top academy ~8-15", NO_BAND, Info, "FR-098", Number, World, "IDs present after the rollover but not at season start"},
  TargetSpec{"LAB-RETIRE", "removed_players_per_club", "Removed players per club per season (retirements)", "-", NO_BAND, Info, "orientation", Number, World, "IDs present at season start but gone after the rollover"},
  TargetSpec{"LAB-WORLDSPEED", "ms_per_world_match", "World simulation cost per match (ms)", "-", NO_BAND, Info, "performance", Number, World, "wall time of the season / matches played"},
};
// clang-format on

const char* unitName(Unit unit) { return unit == Percent ? "%" : ""; }

std::optional<Unit> unitFromName(std::string_view name)
{
  if (name == "%") return Percent;
  if (name.empty()) return Number;
  return std::nullopt;
}

template <typename Enum, std::size_t N>
Enum enumFromName(std::string_view name,
                  const std::array<std::string_view, N>& names)
{
  for (std::size_t i = 0; i < N; ++i)
    if (names[i] == name) return static_cast<Enum>(i);
  throw nlohmann::json::other_error::create(
      501, std::format("unknown enum value '{}'", name), nullptr);
}

constexpr std::array<std::string_view, 5> VERDICT_NAMES = {
    "PASS", "WARN", "FAIL", "INFO", "N/A"};
constexpr std::array<std::string_view, 3> TIER_NAMES = {"Gate", "Monitor",
                                                        "Info"};

nlohmann::json numberJson(double value)
{
  return std::isfinite(value) ? nlohmann::json(value) : nlohmann::json();
}

double numberFrom(const nlohmann::json& json)
{
  return json.is_number() ? json.get<double>()
                          : std::numeric_limits<double>::quiet_NaN();
}

nlohmann::json estimateJson(const Estimate& estimate)
{
  return {{"value", numberJson(estimate.value)},
          {"ci95", {numberJson(estimate.low), numberJson(estimate.high)}},
          {"n", estimate.n}};
}

Estimate estimateFrom(const nlohmann::json& json)
{
  Estimate estimate;
  estimate.value = numberFrom(json.at("value"));
  estimate.low = numberFrom(json.at("ci95").at(0));
  estimate.high = numberFrom(json.at("ci95").at(1));
  estimate.n = json.at("n").get<std::size_t>();
  return estimate;
}

std::string escapeCell(std::string_view text)
{
  std::string cell;
  cell.reserve(text.size());
  for (const char c : text)
  {
    if (c == '|')
      cell += "\\|";
    else if (c == '\n')
      cell += ' ';
    else
      cell += c;
  }
  return cell;
}

std::string bandText(const ReportRow& row)
{
  if (!row.band) return "-";
  return std::format("{} - {}", formatValue(row.band->low, row.unit),
                     formatValue(row.band->high, row.unit));
}

std::string ciText(const ReportRow& row)
{
  if (!row.estimate.valid() || !std::isfinite(row.estimate.low)) return "-";
  return std::format("[{}, {}]", formatValue(row.estimate.low, row.unit),
                     formatValue(row.estimate.high, row.unit));
}

void appendRowTable(std::string& out, std::span<const ReportRow> rows)
{
  out +=
      "| ID | Metric | Value | 95% CI | Band | Target | n | Verdict | "
      "Source | Note |\n";
  out += "|---|---|---|---|---|---|---|---|---|---|\n";
  for (const ReportRow& row : rows)
  {
    out += std::format(
        "| {} | {} | {} | {} | {} | {} | {} | {} | {} | {} |\n", row.id,
        escapeCell(row.label),
        row.estimate.valid() ? formatValue(row.estimate.value, row.unit) : "-",
        ciText(row), bandText(row), escapeCell(row.target), row.estimate.n,
        verdictName(row.verdict), escapeCell(row.source), escapeCell(row.note));
  }
}

void appendTextTable(std::string& out, const TextTable& table)
{
  out += std::format("\n### {}\n\n", table.title);
  if (table.header.empty()) return;
  out += "|";
  for (const std::string& cell : table.header)
    out += std::format(" {} |", escapeCell(cell));
  out += "\n|";
  for (std::size_t i = 0; i < table.header.size(); ++i) out += "---|";
  out += "\n";
  for (const auto& row : table.rows)
  {
    out += "|";
    for (const std::string& cell : row)
      out += std::format(" {} |", escapeCell(cell));
    out += "\n";
  }
}
}  // namespace

std::span<const TargetSpec> targetTable() { return TARGETS; }

double Summary::monitorPassRate() const
{
  return monitor_evaluated == 0
             ? 1.0
             : static_cast<double>(monitor_pass) / monitor_evaluated;
}

Summary LabReport::summary() const
{
  Summary summary;
  for (const ReportRow& row : rows)
  {
    if (row.verdict == Verdict::NotMeasured)
    {
      if (row.tier != Tier::Info) ++summary.not_measured;
      continue;
    }
    if (row.tier == Tier::Gate)
    {
      if (row.verdict == Verdict::Pass) ++summary.gate_pass;
      if (row.verdict == Verdict::Warn) ++summary.gate_warn;
      if (row.verdict == Verdict::Fail) ++summary.gate_fail;
    }
    else if (row.tier == Tier::Monitor)
    {
      ++summary.monitor_evaluated;
      if (row.verdict == Verdict::Pass) ++summary.monitor_pass;
    }
  }
  return summary;
}

int LabReport::exitCode() const
{
  const Summary result = summary();
  if (result.gate_fail > 0) return ExitCode::GATE_FAILED;
  if (result.monitorPassRate() < MONITOR_PASS_THRESHOLD)
    return ExitCode::MONITOR_BELOW_THRESHOLD;
  return ExitCode::OK;
}

std::vector<ReportRow> evaluateTargets(
    const std::map<std::string, Estimate>& metrics,
    std::span<const Scope> scopes)
{
  std::vector<ReportRow> rows;
  for (const TargetSpec& spec : targetTable())
  {
    if (std::ranges::find(scopes, spec.scope) == scopes.end()) continue;
    ReportRow row;
    row.id = spec.id;
    row.metric = spec.metric;
    row.label = spec.label;
    row.target = spec.target;
    row.band = spec.band;
    row.tier = spec.tier;
    row.source = spec.source;
    row.unit = spec.unit;
    row.note = spec.note;
    const auto found = spec.metric.empty()
                           ? metrics.end()
                           : metrics.find(std::string(spec.metric));
    if (found != metrics.end())
      row.estimate = found->second;
    else if (!spec.metric.empty())
      row.note = row.note.empty() ? "no data in this mode"
                                  : row.note + "; no data in this mode";
    row.verdict = row.tier == Tier::Info && row.estimate.valid()
                      ? Verdict::Info
                      : classify(row.estimate, row.band);
    rows.push_back(std::move(row));
  }
  return rows;
}

std::string_view verdictName(Verdict verdict)
{
  return VERDICT_NAMES[static_cast<std::size_t>(verdict)];
}

std::string_view tierName(Tier tier)
{
  return TIER_NAMES[static_cast<std::size_t>(tier)];
}

std::string formatValue(double value, Unit unit)
{
  if (!std::isfinite(value)) return "-";
  if (unit == Percent) return std::format("{:.1f}%", value * 100.0);
  const double magnitude = std::abs(value);
  if (magnitude >= 1000.0) return std::format("{:.0f}", value);
  if (magnitude >= 100.0) return std::format("{:.1f}", value);
  if (magnitude >= 1.0) return std::format("{:.2f}", value);
  return std::format("{:.3f}", value);
}

nlohmann::json toJson(const LabReport& report)
{
  nlohmann::json json;
  json["schema_version"] = LabReport::SCHEMA_VERSION;
  json["mode"] = report.mode;
  json["seed"] = report.seed;
  json["samples"] = report.samples;
  json["threads"] = report.threads;
  json["wall_seconds"] = report.wall_seconds;
  json["ms_per_match"] = report.ms_per_match;
  json["seconds_per_season"] = report.seconds_per_season;
  json["parameters"] = report.parameters;
  nlohmann::json rows = nlohmann::json::array();
  for (const ReportRow& row : report.rows)
  {
    nlohmann::json item = {{"id", row.id},
                           {"metric", row.metric},
                           {"label", row.label},
                           {"target", row.target},
                           {"tier", tierName(row.tier)},
                           {"source", row.source},
                           {"unit", unitName(row.unit)},
                           {"note", row.note},
                           {"estimate", estimateJson(row.estimate)},
                           {"verdict", verdictName(row.verdict)}};
    item["band"] = row.band ? nlohmann::json{row.band->low, row.band->high}
                            : nlohmann::json();
    rows.push_back(std::move(item));
  }
  json["rows"] = std::move(rows);
  nlohmann::json tables = nlohmann::json::array();
  for (const TextTable& table : report.tables)
    tables.push_back({{"title", table.title},
                      {"header", table.header},
                      {"rows", table.rows}});
  json["tables"] = std::move(tables);
  json["notes"] = report.notes;
  const Summary summary = report.summary();
  json["summary"] = {{"gate_pass", summary.gate_pass},
                     {"gate_warn", summary.gate_warn},
                     {"gate_fail", summary.gate_fail},
                     {"monitor_pass", summary.monitor_pass},
                     {"monitor_evaluated", summary.monitor_evaluated},
                     {"monitor_pass_rate", summary.monitorPassRate()},
                     {"not_measured", summary.not_measured},
                     {"exit_code", report.exitCode()}};
  return json;
}

LabReport reportFromJson(const nlohmann::json& json)
{
  if (json.at("schema_version").get<int>() != LabReport::SCHEMA_VERSION)
    throw nlohmann::json::other_error::create(
        502, "unsupported lab report schema version", nullptr);
  LabReport report;
  report.mode = json.at("mode").get<std::string>();
  report.seed = json.at("seed").get<std::uint64_t>();
  report.samples = json.at("samples").get<std::size_t>();
  report.threads = json.at("threads").get<unsigned>();
  report.wall_seconds = json.at("wall_seconds").get<double>();
  report.ms_per_match = json.at("ms_per_match").get<double>();
  report.seconds_per_season = json.at("seconds_per_season").get<double>();
  report.parameters =
      json.at("parameters").get<std::map<std::string, std::string>>();
  for (const nlohmann::json& item : json.at("rows"))
  {
    ReportRow row;
    row.id = item.at("id").get<std::string>();
    row.metric = item.at("metric").get<std::string>();
    row.label = item.at("label").get<std::string>();
    row.target = item.at("target").get<std::string>();
    row.tier =
        enumFromName<Tier>(item.at("tier").get<std::string>(), TIER_NAMES);
    row.source = item.at("source").get<std::string>();
    const std::optional<Unit> unit =
        unitFromName(item.at("unit").get<std::string>());
    if (!unit)
      throw nlohmann::json::other_error::create(503, "unknown unit", nullptr);
    row.unit = *unit;
    row.note = item.at("note").get<std::string>();
    row.estimate = estimateFrom(item.at("estimate"));
    row.verdict = enumFromName<Verdict>(item.at("verdict").get<std::string>(),
                                        VERDICT_NAMES);
    if (const nlohmann::json& band = item.at("band"); !band.is_null())
      row.band = Band{band.at(0).get<double>(), band.at(1).get<double>()};
    report.rows.push_back(std::move(row));
  }
  for (const nlohmann::json& item : json.at("tables"))
  {
    TextTable table;
    table.title = item.at("title").get<std::string>();
    table.header = item.at("header").get<std::vector<std::string>>();
    table.rows = item.at("rows").get<std::vector<std::vector<std::string>>>();
    report.tables.push_back(std::move(table));
  }
  report.notes = json.at("notes").get<std::vector<std::string>>();
  return report;
}

std::string toMarkdown(const LabReport& report)
{
  const Summary summary = report.summary();
  std::string out;
  out += std::format("# fm_lab report: {}\n\n", report.mode);
  out += std::format("- seed {}, samples {}, threads {}, wall {:.1f} s",
                     report.seed, report.samples, report.threads,
                     report.wall_seconds);
  if (report.ms_per_match > 0.0)
    out += std::format(", {:.1f} ms/match", report.ms_per_match);
  if (report.seconds_per_season > 0.0)
    out += std::format(", {:.1f} s/season", report.seconds_per_season);
  out += "\n";
  for (const auto& [key, value] : report.parameters)
    out += std::format("- {}: {}\n", key, value);
  out += std::format(
      "- Gate: {} pass, {} warn, {} FAIL; Monitor pass rate {:.0f}% ({}/{}); "
      "{} targets not measurable yet; exit code {}\n",
      summary.gate_pass, summary.gate_warn, summary.gate_fail,
      100.0 * summary.monitorPassRate(), summary.monitor_pass,
      summary.monitor_evaluated, summary.not_measured, report.exitCode());
  out +=
      "- Verdicts: PASS = estimate inside the band; WARN = outside but the "
      "95% CI overlaps the band; FAIL = whole CI outside; N/A = no "
      "extractor/data.\n";

  const auto section = [&](const char* title, auto predicate)
  {
    std::vector<ReportRow> selected;
    for (const ReportRow& row : report.rows)
      if (predicate(row)) selected.push_back(row);
    if (selected.empty()) return;
    out += std::format("\n## {}\n\n", title);
    appendRowTable(out, selected);
  };
  section("Gate targets",
          [](const ReportRow& row)
          {
            return row.tier == Tier::Gate &&
                   row.verdict != Verdict::NotMeasured;
          });
  section("Monitor targets",
          [](const ReportRow& row)
          {
            return row.tier == Tier::Monitor &&
                   row.verdict != Verdict::NotMeasured;
          });
  section("Info metrics (no spec band)",
          [](const ReportRow& row)
          {
            return row.tier == Tier::Info &&
                   row.verdict != Verdict::NotMeasured;
          });
  if (!report.tables.empty()) out += "\n## Distributions and tables\n";
  for (const TextTable& table : report.tables) appendTextTable(out, table);
  if (!report.notes.empty())
  {
    out += "\n## Notes\n\n";
    for (const std::string& note : report.notes)
      out += std::format("- {}\n", note);
  }
  section("Not measurable yet", [](const ReportRow& row)
          { return row.verdict == Verdict::NotMeasured; });
  return out;
}
}  // namespace Lab
