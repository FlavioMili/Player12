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
#include <utility>

namespace Lab
{
namespace
{
using enum Tier;
using enum Scope;
using enum Unit;

constexpr std::optional<Band> NO_BAND = std::nullopt;
constexpr int UNKNOWN_ENUM_ERROR = 501;
constexpr int UNSUPPORTED_SCHEMA_ERROR = 502;
constexpr int UNKNOWN_UNIT_ERROR = 503;
constexpr double INTEGER_DISPLAY_THRESHOLD = 1000.0;
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
  TargetSpec{.id="CT-M01", .metric="", .label="Outfielder match top speed", .target="8.5 m/s", .band=Band{.low=7.9, .high=8.9}, .tier=Gate, .source="PEER/PUB", .unit=Number, .scope=Engine, .note=NOT_EXPOSED},
  TargetSpec{.id="CT-M02", .metric="", .label="Acceleration profile A0 / S0", .target="7.0 m/s2 / 9.4 m/s", .band=NO_BAND, .tier=Monitor, .source="PEER", .unit=Number, .scope=Engine, .note=NOT_EXPOSED},
  TargetSpec{.id="CT-M03", .metric="", .label="Sprint time constant", .target="1.3 s", .band=Band{.low=1.0, .high=1.5}, .tier=Monitor, .source="PEER", .unit=Number, .scope=Engine, .note=NOT_EXPOSED},
  TargetSpec{.id="CT-M04", .metric="", .label="Peak deceleration", .target="7-10 m/s2", .band=NO_BAND, .tier=Monitor, .source="PEER", .unit=Number, .scope=Engine, .note=NOT_EXPOSED},
  TargetSpec{.id="CT-M05", .metric="", .label="Curve speed 6 m / 2 m radius", .target="74% / 49%", .band=NO_BAND, .tier=Monitor, .source="PEER", .unit=Number, .scope=Engine, .note=NOT_EXPOSED},
  TargetSpec{.id="CT-M06", .metric="distance_per_outfielder_km", .label="Distance per full-match outfielder (km)", .target="10.6 MF / 9.2 CB", .band=Band{.low=9.0, .high=12.5}, .tier=Gate, .source="PUB", .unit=Number, .scope=Engine, .note="PlayerMatchStats::distanceMetres of outfield starters who played the whole match"},
  TargetSpec{.id="CT-M07", .metric="", .label="Ball drag coefficient", .target="0.45 / 0.25", .band=NO_BAND, .tier=Monitor, .source="PEER", .unit=Number, .scope=Engine, .note=NOT_EXPOSED},
  TargetSpec{.id="CT-M08", .metric="", .label="Instep kick speed", .target="28 m/s", .band=Band{.low=23.0, .high=35.0}, .tier=Monitor, .source="PEER", .unit=Number, .scope=Engine, .note=NOT_EXPOSED},
  TargetSpec{.id="CT-M09", .metric="", .label="Bounce restitution", .target="0.62", .band=Band{.low=0.55, .high=0.71}, .tier=Monitor, .source="REG", .unit=Number, .scope=Engine, .note=NOT_EXPOSED},
  TargetSpec{.id="CT-M10", .metric="", .label="Rolling deceleration", .target="0.8 m/s2", .band=Band{.low=0.5, .high=1.3}, .tier=Monitor, .source="REG", .unit=Number, .scope=Engine, .note=NOT_EXPOSED},
  TargetSpec{.id="CT-M11", .metric="late_high_intensity_change", .label="High-intensity running last vs first 15'", .target="-15%", .band=Band{.low=-0.45, .high=-0.08}, .tier=Gate, .source="PEER", .unit=Percent, .scope=Engine, .note="outfield players per minute on the pitch, clock 75-90+ vs 0-15"},
  TargetSpec{.id="CT-M12", .metric="late_distance_change", .label="Total distance last vs first 15'", .target="-14%", .band=Band{.low=-0.17, .high=-0.12}, .tier=Monitor, .source="PEER", .unit=Percent, .scope=Engine, .note="outfield players per minute on the pitch, clock 75-90+ vs 0-15"},
  // ---- Results and scoring -------------------------------------------------
  TargetSpec{.id="CT-M13", .metric="goals_per_match", .label="Goals per match", .target="2.83", .band=Band{.low=2.5, .high=3.3}, .tier=Gate, .source="CC0/PUB", .unit=Number, .scope=Match, .note=""},
  TargetSpec{.id="CT-M14", .metric="home_goal_multiplier", .label="Home / away goals multiplier", .target="1.55/1.28 = x1.21", .band=Band{.low=1.07, .high=1.41}, .tier=Gate, .source="CC0", .unit=Number, .scope=Match, .note="sum(home goals) / sum(away goals)"},
  TargetSpec{.id="CT-M15", .metric="draw_share", .label="Draw share", .target="25%", .band=Band{.low=0.20, .high=0.30}, .tier=Gate, .source="CC0", .unit=Percent, .scope=Match, .note=""},
  TargetSpec{.id="CT-M15H", .metric="home_win_share", .label="Home win share", .target="44%", .band=NO_BAND, .tier=Info, .source="CC0", .unit=Percent, .scope=Match, .note="spec gives a range for draws only"},
  TargetSpec{.id="CT-M15A", .metric="away_win_share", .label="Away win share", .target="31%", .band=NO_BAND, .tier=Info, .source="CC0", .unit=Percent, .scope=Match, .note="spec gives a range for draws only"},
  TargetSpec{.id="CT-M16", .metric="goalless_share", .label="0-0 share", .target="6.1%", .band=Band{.low=0.029, .high=0.088}, .tier=Gate, .source="CC0", .unit=Percent, .scope=Match, .note=""},
  TargetSpec{.id="CT-M16B", .metric="one_one_share", .label="1-1 share", .target="12.2%", .band=Band{.low=0.0976, .high=0.1464}, .tier=Monitor, .source="CC0", .unit=Percent, .scope=Match, .note="no explicit spec range: target +-20% (NFR-018)"},
  TargetSpec{.id="CT-M17", .metric="goals_variance_mean", .label="Total goals variance / mean", .target="1.0", .band=Band{.low=0.9, .high=1.15}, .tier=Gate, .source="CC0 derived", .unit=Number, .scope=Match, .note="bootstrap interval"},
  TargetSpec{.id="CT-M18", .metric="shots_per_team", .label="Shots per team", .target="12.8", .band=Band{.low=10.5, .high=15.5}, .tier=Gate, .source="CC0/PUB", .unit=Number, .scope=Match, .note=""},
  TargetSpec{.id="CT-M19", .metric="on_target_per_team", .label="Shots on target per team", .target="4.5", .band=Band{.low=3.6, .high=5.4}, .tier=Gate, .source="CC0", .unit=Number, .scope=Match, .note=""},
  TargetSpec{.id="CT-M20", .metric="goals_per_shot", .label="Goals per shot", .target="10.5%", .band=Band{.low=0.095, .high=0.12}, .tier=Gate, .source="PUB", .unit=Percent, .scope=Match, .note="own goals excluded from the numerator"},
  TargetSpec{.id="CT-M21", .metric="inside_box_share", .label="Share of shots inside the box", .target="66%", .band=Band{.low=0.58, .high=0.70}, .tier=Monitor, .source="PUB", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="CT-M22", .metric="passes_per_match", .label="Passes per match (both teams)", .target="900", .band=Band{.low=850.0, .high=950.0}, .tier=Gate, .source="PUB", .unit=Number, .scope=Match, .note=""},
  TargetSpec{.id="CT-M22B", .metric="pass_completion", .label="Pass completion", .target="82%", .band=Band{.low=0.76, .high=0.85}, .tier=Gate, .source="PUB", .unit=Percent, .scope=Match, .note=""},
  TargetSpec{.id="CT-M23", .metric="", .label="Average pass length", .target="19.5 m", .band=Band{.low=18.9, .high=21.1}, .tier=Monitor, .source="PUB", .unit=Number, .scope=Engine, .note="not measurable yet: pass lengths are not recorded"},
  TargetSpec{.id="CT-M24", .metric="", .label="PPDA", .target="11", .band=Band{.low=7.3, .high=17.7}, .tier=Monitor, .source="PUB", .unit=Number, .scope=Engine, .note="not measurable yet: needs pass/defensive-action locations"},
  TargetSpec{.id="CT-M25", .metric="", .label="Team length / width out of possession", .target="38 / 43 m", .band=NO_BAND, .tier=Monitor, .source="PEER", .unit=Number, .scope=Engine, .note="not measurable yet: needs team shape samples"},
  TargetSpec{.id="CT-M26", .metric="", .label="High turnovers (both teams)", .target="14", .band=Band{.low=11.5, .high=16.7}, .tier=Monitor, .source="PUB", .unit=Number, .scope=Engine, .note="not measurable yet: turnover locations not recorded"},
  TargetSpec{.id="CT-M27", .metric="", .label="Fast-break goals share", .target="5%", .band=Band{.low=0.014, .high=0.071}, .tier=Monitor, .source="PUB", .unit=Percent, .scope=Engine, .note="not measurable yet: no possession-chain tagging"},
  TargetSpec{.id="CT-M28", .metric="corners_per_match", .label="Corners per match", .target="10.3", .band=Band{.low=9.3, .high=11.0}, .tier=Gate, .source="CC0/PUB", .unit=Number, .scope=Match, .note=""},
  TargetSpec{.id="CT-M28B", .metric="", .label="Goals per corner", .target="3.5%", .band=Band{.low=0.028, .high=0.042}, .tier=Gate, .source="PUB", .unit=Percent, .scope=Engine, .note="not measurable yet: goals are not attributed to corners"},
  TargetSpec{.id="CT-M29", .metric="set_piece_goal_share", .label="Non-penalty set-piece share of goals", .target="21%", .band=Band{.low=0.198, .high=0.31}, .tier=Gate, .source="PUB", .unit=Percent, .scope=Engine, .note="(set-piece goals - penalty goals) / goals; engine set-piece window definition"},
  TargetSpec{.id="CT-M30", .metric="penalties_per_match", .label="Penalties per match", .target="0.27", .band=Band{.low=0.2, .high=0.36}, .tier=Gate, .source="PUB", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="CT-M30B", .metric="penalty_conversion", .label="Penalty conversion", .target="80%", .band=Band{.low=0.75, .high=0.90}, .tier=Gate, .source="PUB", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="LAB-THROW", .metric="throw_ins_per_match", .label="Throw-ins per match", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="from the event log (training ground §15: restarts of each kind)"},
  TargetSpec{.id="LAB-GKICK", .metric="goal_kicks_per_match", .label="Goal kicks per match", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="from the event log"},
  TargetSpec{.id="LAB-FKICK", .metric="free_kicks_per_match", .label="Free kicks per match", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="from the event log; every free kick, direct and indirect"},
  TargetSpec{.id="LAB-EVFULL", .metric="event_log_full_share", .label="Matches filling the event log", .target="0%", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="the log keeps the last 1,024 events; above 0%, the restart counts miss early ones"},
  TargetSpec{.id="LAB-SOCONV", .metric="shootout_conversion", .label="Shootout kicks scored", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of shootout kicks (fm_lab matches --knockout: level at full time, straight to a shootout)"},
  TargetSpec{.id="LAB-SOSAVE", .metric="shootout_save_share", .label="  ...saved", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of shootout kicks: off the keeper or held"},
  TargetSpec{.id="CT-M31", .metric="fouls_per_team", .label="Fouls per team", .target="11.5", .band=Band{.low=10.5, .high=13.0}, .tier=Gate, .source="CC0", .unit=Number, .scope=Match, .note=""},
  TargetSpec{.id="CT-M32", .metric="yellows_per_match", .label="Yellow cards per match", .target="4.1", .band=Band{.low=3.75, .high=5.0}, .tier=Gate, .source="CC0", .unit=Number, .scope=Match, .note="league band (referee band 2.9-5.1)"},
  TargetSpec{.id="CT-M32B", .metric="reds_per_match", .label="Red cards per match", .target="0.18", .band=Band{.low=0.14, .high=0.36}, .tier=Gate, .source="CC0", .unit=Number, .scope=Match, .note=""},
  TargetSpec{.id="CT-M33", .metric="subs_per_team", .label="Substitutions per team", .target="4.5", .band=Band{.low=4.2, .high=4.6}, .tier=Monitor, .source="INT", .unit=Number, .scope=Engine, .note="INT source: replace before publication (C-009)"},
  TargetSpec{.id="CT-M33B", .metric="sub_mean_minute", .label="Mean substitution minute", .target="71-73", .band=Band{.low=71.0, .high=73.0}, .tier=Monitor, .source="INT", .unit=Number, .scope=Engine, .note="INT source: replace before publication (C-009)"},
  TargetSpec{.id="CT-M34", .metric="ball_in_play_minutes", .label="Ball in play (min)", .target="56.5", .band=Band{.low=54.75, .high=58.18}, .tier=Gate, .source="PUB", .unit=Number, .scope=Engine, .note="engine clock minutes"},
  TargetSpec{.id="CT-M34B", .metric="match_length_minutes", .label="Total match length (min)", .target="100.5", .band=Band{.low=99.73, .high=101.6}, .tier=Gate, .source="PUB", .unit=Number, .scope=Engine, .note="90 + both added times"},
  TargetSpec{.id="CT-M35", .metric="late_goal_share", .label="Goals from minute 90 onwards", .target="8.5%", .band=Band{.low=0.047, .high=0.09}, .tier=Monitor, .source="PUB", .unit=Percent, .scope=Match, .note="second-half clock minute >= 90"},
  TargetSpec{.id="CT-M36", .metric="red_penalised_multiplier", .label="Scoring rate after own red card (x even)", .target="x0.58-0.83", .band=Band{.low=0.58, .high=0.83}, .tier=Monitor, .source="PEER", .unit=Number, .scope=Engine, .note="goals per team-minute a man down / at even strength; bootstrap"},
  TargetSpec{.id="CT-M36B", .metric="red_opponent_multiplier", .label="Opponent scoring rate after red (x even)", .target="x1.60-1.69", .band=Band{.low=1.60, .high=1.69}, .tier=Monitor, .source="PEER", .unit=Number, .scope=Engine, .note="goals per team-minute a man up / at even strength; bootstrap"},
  TargetSpec{.id="CT-M37", .metric="lead1_goal_multiplier", .label="Goal rate leading by 1 (x level)", .target="x0.91", .band=Band{.low=0.728, .high=1.092}, .tier=Monitor, .source="PEER", .unit=Number, .scope=Engine, .note="no explicit spec range: target +-20%; bootstrap"},
  TargetSpec{.id="CT-M37B", .metric="trail1_goal_multiplier", .label="Goal rate trailing by 1 (x level)", .target="x1.10", .band=Band{.low=0.88, .high=1.32}, .tier=Monitor, .source="PEER", .unit=Number, .scope=Engine, .note="no explicit spec range: target +-20%; bootstrap"},
  TargetSpec{.id="CT-M38", .metric="headed_goal_share", .label="Headed-goal share", .target="14%", .band=Band{.low=0.12, .high=0.19}, .tier=Monitor, .source="PUB", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="CT-M38B", .metric="own_goal_share", .label="Own-goal share", .target="2-4%", .band=Band{.low=0.02, .high=0.04}, .tier=Monitor, .source="PUB", .unit=Percent, .scope=Match, .note=""},
  TargetSpec{.id="CT-M39", .metric="offsides_per_team", .label="Offsides per team", .target="1.9", .band=Band{.low=1.6, .high=2.0}, .tier=Monitor, .source="INT", .unit=Number, .scope=Match, .note="INT source: replace before publication (C-009)"},
  TargetSpec{.id="CT-M40", .metric="", .label="Video-review overturns per match", .target="0.29", .band=Band{.low=0.28, .high=0.30}, .tier=Monitor, .source="PUB", .unit=Number, .scope=Engine, .note="not measurable yet: no video review in the engine"},
  TargetSpec{.id="CT-M41", .metric="xg_per_shot", .label="Mean xG per shot", .target="0.105", .band=Band{.low=0.09, .high=0.12}, .tier=Monitor, .source="PUB", .unit=Number, .scope=Match, .note=""},
  TargetSpec{.id="CT-M42", .metric="", .label="Goalkeeper reaction + dive", .target="0.21 s + 0.7-0.8 s", .band=NO_BAND, .tier=Monitor, .source="PEER", .unit=Number, .scope=Engine, .note="not measurable yet: keeper timings not exposed"},
  TargetSpec{.id="CT-M43", .metric="", .label="Home points advantage without crowd", .target="-45%", .band=Band{.low=-0.57, .high=-0.33}, .tier=Monitor, .source="PEER", .unit=Percent, .scope=Engine, .note="not measurable yet: no crowd/empty-stadium switch"},
  // ---- Skill sensitivity (NFR-019; needs --spread) --------------------------
  TargetSpec{.id="NFR-019A", .metric="stronger_win_gap_lt3", .label="Stronger side win share, rating gap < 3", .target="38-48%", .band=Band{.low=0.38, .high=0.48}, .tier=Monitor, .source="spec NFR-019", .unit=Percent, .scope=Engine, .note="starter mean overall gap; exact ties excluded (use --spread)"},
  TargetSpec{.id="NFR-019B", .metric="stronger_win_gap_3_8", .label="Stronger side win share, gap 3-8", .target="45-55%", .band=Band{.low=0.45, .high=0.55}, .tier=Monitor, .source="spec NFR-019", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="NFR-019C", .metric="stronger_win_gap_8_15", .label="Stronger side win share, gap 8-15", .target="52-65%", .band=Band{.low=0.52, .high=0.65}, .tier=Monitor, .source="spec NFR-019", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="NFR-019D", .metric="stronger_win_gap_gt15", .label="Stronger side win share, gap > 15", .target="60-75%", .band=Band{.low=0.60, .high=0.75}, .tier=Monitor, .source="spec NFR-019", .unit=Percent, .scope=Engine, .note=""},
  // ---- Tactics (fm_lab tactics) ---------------------------------------------
  TargetSpec{.id="SC-004", .metric="max_preset_points_share", .label="Best preset points share vs the others", .target="<= 60%", .band=Band{.low=0.0, .high=0.60}, .tier=Gate, .source="spec SC-004", .unit=Percent, .scope=Tactics, .note="points / (3 x matches) over paired-seed home/away round robin"},
  // ---- Match INFO (no spec band) --------------------------------------------
  TargetSpec{.id="LAB-ONENIL", .metric="one_nil_share", .label="1-0 or 0-1 share", .target="~17% (big-5 leagues)", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Match, .note=""},
  TargetSpec{.id="LAB-ONTGT", .metric="on_target_share", .label="Shots on target share", .target="~35%", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Match, .note=""},
  TargetSpec{.id="LAB-BLKSH", .metric="blocked_share", .label="Shots blocked (open play)", .target="~25%", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Match, .note="by a body; the free-kick wall excluded"},
  TargetSpec{.id="LAB-BLKLEG", .metric="shot_leg_block_share", .label="Shot blocks off a leg", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of open-play shots blocked by a Binturong body: thigh, shin or foot (the rest torso or head)"},
  TargetSpec{.id="LAB-BLKREACT", .metric="shot_reaction_block_share", .label="Shot blocks off a leg put out", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of open-play shots blocked by a Binturong body: a leg he put across its line after reacting (shot block plan 3d); the rest his torso or stride"},
  TargetSpec{.id="LAB-DEFL", .metric="shot_deflections_per_shot", .label="Shot deflections off a body", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="per open-play shot: off a Binturong body but no block (a team-mate's, or anyone's once it was blocked; shot block plan step 4)"},
  TargetSpec{.id="LAB-GXB", .metric="block_check_predicted", .label="Capybara shots: predicted stopped", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="game xG layer 3 check: of Capybara open-play shots, the mean share its estimate said the bodies would stop"},
  TargetSpec{.id="LAB-GXBA", .metric="block_check_blocked", .label="  ...blocked by an opponent", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those shots: the first body it met was an opponent's, and it was a block"},
  TargetSpec{.id="LAB-GXBT", .metric="block_check_team_mate", .label="  ...first met a team-mate", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those shots: the first body it met was a team-mate's"},
  TargetSpec{.id="LAB-GXB0", .metric="block_check_band_0_share", .label="Predicted 0-20% stopped", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="share of Capybara open-play shots in this band"},
  TargetSpec{.id="LAB-GXB0P", .metric="block_check_band_0_predicted", .label="  ...predicted", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="mean predicted share stopped"},
  TargetSpec{.id="LAB-GXB0A", .metric="block_check_band_0_blocked", .label="  ...blocked", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="first body an opponent's block"},
  TargetSpec{.id="LAB-GXB0T", .metric="block_check_band_0_team_mate", .label="  ...off a team-mate", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="first body a team-mate's"},
  TargetSpec{.id="LAB-GXB1", .metric="block_check_band_1_share", .label="Predicted 20-40% stopped", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="share of Capybara open-play shots in this band"},
  TargetSpec{.id="LAB-GXB1P", .metric="block_check_band_1_predicted", .label="  ...predicted", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="mean predicted share stopped"},
  TargetSpec{.id="LAB-GXB1A", .metric="block_check_band_1_blocked", .label="  ...blocked", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="first body an opponent's block"},
  TargetSpec{.id="LAB-GXB1T", .metric="block_check_band_1_team_mate", .label="  ...off a team-mate", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="first body a team-mate's"},
  TargetSpec{.id="LAB-GXB2", .metric="block_check_band_2_share", .label="Predicted 40-60% stopped", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="share of Capybara open-play shots in this band"},
  TargetSpec{.id="LAB-GXB2P", .metric="block_check_band_2_predicted", .label="  ...predicted", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="mean predicted share stopped"},
  TargetSpec{.id="LAB-GXB2A", .metric="block_check_band_2_blocked", .label="  ...blocked", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="first body an opponent's block"},
  TargetSpec{.id="LAB-GXB2T", .metric="block_check_band_2_team_mate", .label="  ...off a team-mate", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="first body a team-mate's"},
  TargetSpec{.id="LAB-GXB3", .metric="block_check_band_3_share", .label="Predicted 60-80% stopped", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="share of Capybara open-play shots in this band"},
  TargetSpec{.id="LAB-GXB3P", .metric="block_check_band_3_predicted", .label="  ...predicted", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="mean predicted share stopped"},
  TargetSpec{.id="LAB-GXB3A", .metric="block_check_band_3_blocked", .label="  ...blocked", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="first body an opponent's block"},
  TargetSpec{.id="LAB-GXB3T", .metric="block_check_band_3_team_mate", .label="  ...off a team-mate", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="first body a team-mate's"},
  TargetSpec{.id="LAB-GXB4", .metric="block_check_band_4_share", .label="Predicted 80-100% stopped", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="share of Capybara open-play shots in this band"},
  TargetSpec{.id="LAB-GXB4P", .metric="block_check_band_4_predicted", .label="  ...predicted", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="mean predicted share stopped"},
  TargetSpec{.id="LAB-GXB4A", .metric="block_check_band_4_blocked", .label="  ...blocked", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="first body an opponent's block"},
  TargetSpec{.id="LAB-GXB4T", .metric="block_check_band_4_team_mate", .label="  ...off a team-mate", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="first body a team-mate's"},
  TargetSpec{.id="LAB-GKBODY", .metric="keeper_body_hits_per_shot", .label="Shots off the keeper", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="per open-play shot: meeting the defending keeper's body, arms or hands in his area (goalkeeper plan steps 1-2)"},
  TargetSpec{.id="LAB-GKBODYIN", .metric="keeper_body_still_on_goal_per_shot", .label="  ...still going in", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="per open-play shot: off him and still on target"},
  TargetSpec{.id="LAB-GKWENT", .metric="keeper_hands_went_per_shot", .label="Keeper's hands went for it", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="per open-play shot: his hands went for its line once he had reacted (step 2)"},
  TargetSpec{.id="LAB-GKTOG", .metric="keeper_hands_together_share", .label="  ...both hands", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those: both hands together (else the nearer one alone)"},
  TargetSpec{.id="LAB-GKLATE", .metric="keeper_hands_late_share", .label="  ...ball there first", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those: the ball at his hand's spot before the hand could be"},
  TargetSpec{.id="LAB-GKDIVE", .metric="keeper_dives_per_shot", .label="Keeper dived", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="per open-play shot: he dived for it (its line beyond his standing reach; penalty guesses too; step 3)"},
  TargetSpec{.id="LAB-GKDIVELATE", .metric="keeper_dives_late_share", .label="  ...ball there before full stretch", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of dives: the ball where he aimed before his full stretch"},
  TargetSpec{.id="LAB-GKHANDS", .metric="keeper_hand_shots_per_shot", .label="Shots off a keeper's hand", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="per open-play shot: met his hand first (step 4)"},
  TargetSpec{.id="LAB-GKBOTH", .metric="keeper_both_hands_share", .label="  ...both hands on it", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those: his other hand on it too"},
  TargetSpec{.id="LAB-GKCATCH", .metric="keeper_catch_share", .label="  ...went to catch", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those: both hands and within his Handling limit for its pace and his stretch"},
  TargetSpec{.id="LAB-GKHELD", .metric="keeper_held_share", .label="  ...held", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those: caught and held"},
  TargetSpec{.id="LAB-GKFUMBLE", .metric="keeper_fumble_share", .label="  ...catches fumbled", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of catches: fumbled, off his hands"},
  TargetSpec{.id="LAB-GKPARRY", .metric="keeper_parry_share", .label="  ...parried", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of shots off a hand: parried, the face turned away from goal"},
  TargetSpec{.id="LAB-GKCLAIM", .metric="keeper_claims_per_match", .label="Keeper claims per match", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="both keepers: his hands met a high ball (step 7; an Aardvark keeper's claim roll too)"},
  TargetSpec{.id="LAB-GKCLHELD", .metric="keeper_claim_held_share", .label="  ...held", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of claims: caught and held"},
  TargetSpec{.id="LAB-GKPUNCH", .metric="keeper_claim_punch_share", .label="  ...punched", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of claims: punched (an opponent within 2.0-0.5 m, by Command of Area)"},
  TargetSpec{.id="LAB-GKCLPARRY", .metric="keeper_claim_parry_share", .label="  ...off his hands", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of claims: meant to catch, sprang off (over his limit, one hand, or fumbled)"},
  TargetSpec{.id="LAB-GKSMOTHER", .metric="keeper_smothers_per_match", .label="Keeper smother dives per match", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="both keepers: dived at a loose ball an attacker would reach first on foot (step 8)"},
  TargetSpec{.id="LAB-GKSMHELD", .metric="keeper_smother_held_share", .label="  ...held", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="loose balls held off his hand, per smother dive"},
  TargetSpec{.id="LAB-GKINHANDS", .metric="keeper_balls_in_hands_per_match", .label="Balls in a keeper's hands", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="per match: saves held, claims, balls gathered (step 5)"},
  TargetSpec{.id="LAB-GKROLL", .metric="keeper_roll_share", .label="  ...rolled out", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of releases from his hands: rolled (a ground pass up to 25 m)"},
  TargetSpec{.id="LAB-GKTHROW", .metric="keeper_throw_share", .label="  ...thrown overarm", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of releases from his hands: thrown overarm (a ground pass beyond 25 m)"},
  TargetSpec{.id="LAB-GKPUNT", .metric="keeper_punt_share", .label="  ...punted", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of releases from his hands: punted (a lofted pass)"},
  TargetSpec{.id="LAB-GKHAND", .metric="keeper_hand_hit_share", .label="  ...off a hand or arm", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of shots off the keeper: his hands or arms (step 2); the rest his body"},
  TargetSpec{.id="LAB-BODY", .metric="ball_body_hits_per_match", .label="Other balls off a body", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="per match: passes, clearances, touches and loose balls meeting a Binturong body that didn't play them (pass block plan)"},
  TargetSpec{.id="LAB-BODYLEG", .metric="ball_leg_hit_share", .label="  ...off a leg", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those: thigh, shin or foot (the rest torso or head)"},
  TargetSpec{.id="LAB-BODYHEAD", .metric="ball_head_hit_share", .label="  ...off a head", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those: above the top of his shoulders, not headed (shot block plan 3i)"},
  TargetSpec{.id="LAB-BODYREACT", .metric="ball_reaction_block_share", .label="  ...off a leg put out", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those: a leg he put across its line after reacting"},
  TargetSpec{.id="LAB-PASSBLK", .metric="passes_blocked_per_match", .label="Passes blocked", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="per match: opponents' passes meeting a Binturong body that didn't play them"},
  TargetSpec{.id="LAB-BODYDEFL", .metric="ball_deflections_per_match", .label="Balls off a team-mate's body", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="per match: off a body when his own side (or nobody) last played it"},
  TargetSpec{.id="LAB-DFK", .metric="direct_free_kicks_per_match", .label="Direct free kicks", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="per match: free kicks shot at goal directly (Binturong body)"},
  TargetSpec{.id="LAB-DFKON", .metric="direct_free_kick_on_target_share", .label="  ...on target as struck", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those: its flight heading inside the frame as it left his foot"},
  TargetSpec{.id="LAB-DFKBLK", .metric="direct_free_kick_blocked_share", .label="  ...blocked by a body", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those: blocked by an opponent's body (no wall formed yet)"},
  TargetSpec{.id="LAB-DFKGOAL", .metric="direct_free_kick_goal_share", .label="  ...scored", .target="~6%", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those: scored from the shot (off a body still going in included)"},
  TargetSpec{.id="LAB-THRU",.metric="shot_torso_pass_throughs_per_shot", .label="Shots through a torso unmet", .target="0", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="per open-play shot: its path, against the player's own move, went 5 cm inside a Binturong torso with no contact (check; shot block plan 3f)"},
  TargetSpec{.id="LAB-THRUIN", .metric="shot_torso_inside_at_start_per_shot", .label="  ...already inside at a sub-step's start", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="per open-play shot: of those, the ball already inside him as the sub-step began"},
  TargetSpec{.id="LAB-LEGCH", .metric="shot_leg_chances_per_shot", .label="Defenders a shot passes within reach of", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="per open-play shot: Binturong outfielders its line passes within 1.0 m of (shot block plan 3d), whatever its height or their speed"},
  TargetSpec{.id="LAB-LEGLATE", .metric="shot_leg_too_late_share", .label="  ...ball there before he reacts", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those: the ball reached him before his reaction (Defending) was over; body and stride only"},
  TargetSpec{.id="LAB-LEGOUT", .metric="shot_leg_out_share", .label="  ...leg put out", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those: he reacted and put a leg across its line"},
  TargetSpec{.id="LAB-LEGHIP", .metric="shot_leg_above_hip_share", .label="  ...above his hip", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those: reacted; the ball passes above his hip (his torso only)"},
  TargetSpec{.id="LAB-LEGHGT", .metric="shot_leg_reach_height_share", .label="  ...out of reach at that height", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those: reacted; beyond his reach at the ball's height (1.0 m to the knee, narrowing to his body at the hip)"},
  TargetSpec{.id="LAB-LEGRUN", .metric="shot_leg_reach_running_share", .label="  ...out of reach running", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those: reacted; in reach standing, not at his speed (full to 40% of top speed, half at a sprint)"},
  TargetSpec{.id="LAB-LEGBEH", .metric="shot_leg_behind_share", .label="  ...behind him", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those: reacted; the line passes in his dead zone"},
  TargetSpec{.id="LAB-LEGPAST", .metric="shot_leg_past_share", .label="  ...past him as he reacted", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those: reacted, but the ball was already past the nearest point (moving with him)"},
  TargetSpec{.id="LAB-LEGBUSY", .metric="shot_leg_feet_busy_share", .label="  ...feet busy", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of those: reacted in a jump, planted for a thigh touch or with his foot committed"},
  TargetSpec{.id="LAB-LEGHIT", .metric="shot_leg_out_blocks_share", .label="Legs put out that blocked it", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="of legs put across a shot's line: those it hit (a reaction block)"},
  TargetSpec{.id="LAB-POSS", .metric="possession_spread", .label="Possession spread |home - 50| (pts)", .target="~9-10 pts", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Match, .note=""},
  TargetSpec{.id="LAB-ADDED", .metric="added_time_minutes", .label="Added time per match (min)", .target="~10.5", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="LAB-INJ", .metric="injuries_per_team", .label="In-match injuries per team", .target="~0.2-0.3", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="LAB-DIST-H2", .metric="second_half_distance_change", .label="2nd vs 1st half distance per minute", .target="-3 to -10%", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="full-match outfield starters"},
  TargetSpec{.id="LAB-FUEL", .metric="end_fuel_mean", .label="Fuel (condition) left at full time", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=Engine, .note="outfield players on the pitch; floor 0.35 (Aardvark body), 0 (Binturong)"},
  TargetSpec{.id="LAB-FUEL0", .metric="end_fuel_floor_share", .label="Players at the fuel floor at full time", .target="", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=Engine, .note="outfield players on the pitch"},
  // ---- Fatigue INFO (fatigue plan, step 0) ----------------------------------
  TargetSpec{.id="FAT-FSD", .metric="end_fuel_sd", .label="Fuel at full time: spread (sd)", .target="", .band=NO_BAND, .tier=Info, .source="fatigue", .unit=Number, .scope=Engine, .note="outfield players on the pitch"},
  TargetSpec{.id="FAT-FMIN", .metric="end_fuel_min_mean", .label="Fuel at full time: lowest player per match", .target="", .band=NO_BAND, .tier=Info, .source="fatigue", .unit=Number, .scope=Engine, .note="mean over matches"},
  TargetSpec{.id="FAT-F60", .metric="end_fuel_60_share", .label="Finishers with 60%+ fuel (Fresh)", .target="", .band=NO_BAND, .tier=Info, .source="fatigue", .unit=Percent, .scope=Engine, .note="outfield players on the pitch"},
  TargetSpec{.id="FAT-F30", .metric="end_fuel_30_60_share", .label="Finishers with 30-60% fuel", .target="", .band=NO_BAND, .tier=Info, .source="fatigue", .unit=Percent, .scope=Engine, .note="toward Tired"},
  TargetSpec{.id="FAT-F10", .metric="end_fuel_10_30_share", .label="Finishers with 10-30% fuel", .target="", .band=NO_BAND, .tier=Info, .source="fatigue", .unit=Percent, .scope=Engine, .note="toward Exhausted"},
  TargetSpec{.id="FAT-F0", .metric="end_fuel_below_10_share", .label="Finishers below 10% fuel", .target="", .band=NO_BAND, .tier=Info, .source="fatigue", .unit=Percent, .scope=Engine, .note="toward Spent"},
  TargetSpec{.id="FAT-FE", .metric="end_fuel_empty_share", .label="Finishers with an empty tank", .target="", .band=NO_BAND, .tier=Info, .source="fatigue", .unit=Percent, .scope=Engine, .note="Spent"},
  TargetSpec{.id="FAT-WALK", .metric="walk_per_outfielder_km", .label="Walking < 7.2 km/h per outfielder (km)", .target="", .band=NO_BAND, .tier=Info, .source="fatigue", .unit=Number, .scope=Engine, .note="full-match outfield starters; Bradley 2009 bands"},
  TargetSpec{.id="FAT-JOG", .metric="jog_per_outfielder_km", .label="Jogging 7.2-14.4 km/h per outfielder (km)", .target="", .band=NO_BAND, .tier=Info, .source="fatigue", .unit=Number, .scope=Engine, .note="full-match outfield starters"},
  TargetSpec{.id="FAT-RUN", .metric="run_per_outfielder_km", .label="Running 14.4-19.8 km/h per outfielder (km)", .target="", .band=NO_BAND, .tier=Info, .source="fatigue", .unit=Number, .scope=Engine, .note="full-match outfield starters"},
  TargetSpec{.id="FAT-HSR", .metric="hsr_per_outfielder_km", .label="High-speed running 19.8-25.2 km/h per outfielder (km)", .target="", .band=NO_BAND, .tier=Info, .source="fatigue", .unit=Number, .scope=Engine, .note="full-match outfield starters"},
  TargetSpec{.id="FAT-PEAK", .metric="peak5_next_change", .label="Running > 14.4 km/h in the 5' after a player's peak 5' vs his mean", .target="-12% (Mohr 2003)", .band=NO_BAND, .tier=Info, .source="fatigue", .unit=Percent, .scope=Engine, .note="full-match outfield starters; peak in a half's last 5' skipped"},
  TargetSpec{.id="FAT-NRG", .metric="energy_per_outfielder_kj", .label="Metabolic energy per outfielder (kJ/kg)", .target="~61 (Osgnach 2010)", .band=NO_BAND, .tier=Info, .source="fatigue", .unit=Number, .scope=Engine, .note="full-match outfield starters; net of standing"},
  TargetSpec{.id="FAT-ANA", .metric="anaerobic_energy_share", .label="Energy above critical speed's power", .target="", .band=NO_BAND, .tier=Info, .source="fatigue", .unit=Percent, .scope=Engine, .note="full-match outfield starters"},
  TargetSpec{.id="MOD-WT", .metric="mode_walk_time_share", .label="Time walking (own bands)", .target="", .band=NO_BAND, .tier=Info, .source="modes", .unit=Percent, .scope=Engine, .note="full-match outfield starters; below his walk-run transition"},
  TargetSpec{.id="MOD-JT", .metric="mode_jog_time_share", .label="Time jogging (own bands)", .target="", .band=NO_BAND, .tier=Info, .source="modes", .unit=Percent, .scope=Engine, .note="transition to 0.82 x critical speed"},
  TargetSpec{.id="MOD-RT", .metric="mode_run_time_share", .label="Time running (own bands)", .target="", .band=NO_BAND, .tier=Info, .source="modes", .unit=Percent, .scope=Engine, .note="0.82 x critical speed to critical speed"},
  TargetSpec{.id="MOD-ST", .metric="mode_sprint_time_share", .label="Time sprinting (own bands)", .target="", .band=NO_BAND, .tier=Info, .source="modes", .unit=Percent, .scope=Engine, .note="above critical speed"},
  TargetSpec{.id="MOD-WK", .metric="mode_walk_km", .label="Walking per outfielder (km, own bands)", .target="", .band=NO_BAND, .tier=Info, .source="modes", .unit=Number, .scope=Engine, .note="full-match outfield starters"},
  TargetSpec{.id="MOD-JK", .metric="mode_jog_km", .label="Jogging per outfielder (km, own bands)", .target="", .band=NO_BAND, .tier=Info, .source="modes", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="MOD-RK", .metric="mode_run_km", .label="Running per outfielder (km, own bands)", .target="", .band=NO_BAND, .tier=Info, .source="modes", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="MOD-SK", .metric="mode_sprint_km", .label="Above critical speed per outfielder (km, own bands)", .target="", .band=NO_BAND, .tier=Info, .source="modes", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="FAT-BRK", .metric="braking_energy_share", .label="Energy spent while slowing down", .target="", .band=NO_BAND, .tier=Info, .source="fatigue", .unit=Percent, .scope=Engine, .note="full-match outfield starters"},
  TargetSpec{.id="FAT-BRKA", .metric="braking_anaerobic_share", .label="Above-CS energy spent while slowing down", .target="", .band=NO_BAND, .tier=Info, .source="fatigue", .unit=Percent, .scope=Engine, .note="share of the energy above CS power"},
  TargetSpec{.id="FAT-EMP", .metric="emptied_share", .label="Full-match players whose tank ran out", .target="", .band=NO_BAND, .tier=Info, .source="fatigue", .unit=Percent, .scope=Engine, .note="full-match outfield starters"},
  TargetSpec{.id="FAT-EMPM", .metric="empty_minute_mean", .label="Elapsed minute the tank ran out", .target="", .band=NO_BAND, .tier=Info, .source="fatigue", .unit=Number, .scope=Engine, .note="those whose tank ran out"},
  TargetSpec{.id="FAT-HIR", .metric="hir_per_outfielder_km", .label="Running > 14.4 km/h per outfielder (km)", .target="~1.8-3.1 by position (Bradley 2009)", .band=NO_BAND, .tier=Info, .source="fatigue", .unit=Number, .scope=Engine, .note="full-match outfield starters; the research's high-intensity running"},
  TargetSpec{.id="LAB-ABND", .metric="abandoned_share", .label="Abandoned matches", .target="0%", .band=NO_BAND, .tier=Info, .source="sanity", .unit=Percent, .scope=Engine, .note="excluded from all other match metrics"},
  // ---- Player behaviour INFO (brain comparisons; no spec band) -------------
  TargetSpec{.id="LAB-TKL", .metric="tackles_per_team", .label="Tackles attempted per team", .target="", .band=NO_BAND, .tier=Info, .source="behaviour", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="LAB-TKLW", .metric="tackle_success", .label="Tackles won share", .target="", .band=NO_BAND, .tier=Info, .source="behaviour", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="LAB-INT", .metric="interceptions_per_team", .label="Interceptions per team", .target="", .band=NO_BAND, .tier=Info, .source="behaviour", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="LAB-CLR", .metric="clearances_per_team", .label="Clearances per team", .target="", .band=NO_BAND, .tier=Info, .source="behaviour", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="LAB-AER", .metric="aerial_duels_per_match", .label="Aerial duels per match (both teams)", .target="", .band=NO_BAND, .tier=Info, .source="behaviour", .unit=Number, .scope=Engine, .note="won + lost over all players, so each duel may count twice"},
  TargetSpec{.id="LAB-HSQ", .metric="header_sequences_per_match", .label="Header sequences per match", .target="", .band=NO_BAND, .tier=Info, .source="behaviour", .unit=Number, .scope=Engine, .note="headers one after another until someone has the ball or play stops"},
  TargetSpec{.id="LAB-HSN", .metric="headers_per_sequence", .label="Headers per sequence", .target="", .band=NO_BAND, .tier=Info, .source="behaviour", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="LAB-HST", .metric="head_tennis_share", .label="Sequences of 3+ headers", .target="", .band=NO_BAND, .tier=Info, .source="behaviour", .unit=Percent, .scope=Engine, .note="head tennis"},
  TargetSpec{.id="LAB-HSM", .metric="longest_header_sequence", .label="Longest header sequence per match", .target="", .band=NO_BAND, .tier=Info, .source="behaviour", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="LAB-HSC", .metric="header_sequences_to_control", .label="Sequences ending with a side in control", .target="", .band=NO_BAND, .tier=Info, .source="behaviour", .unit=Percent, .scope=Engine, .note="the rest end with play stopped"},
  TargetSpec{.id="LAB-HSK", .metric="header_sequences_kept", .label="Of those, kept by the side heading last", .target="", .band=NO_BAND, .tier=Info, .source="behaviour", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="LAB-PRS", .metric="pressures_per_team", .label="Pressures per team", .target="", .band=NO_BAND, .tier=Info, .source="behaviour", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="LAB-KEY", .metric="key_passes_per_team", .label="Key passes per team", .target="", .band=NO_BAND, .tier=Info, .source="behaviour", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="LAB-SAV", .metric="saves_per_team", .label="Saves per team", .target="", .band=NO_BAND, .tier=Info, .source="behaviour", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="LAB-TCH", .metric="touches_per_team", .label="Touches per team", .target="", .band=NO_BAND, .tier=Info, .source="behaviour", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="LAB-HI", .metric="high_intensity_per_outfielder_km", .label="High-intensity distance per outfielder (km)", .target="", .band=NO_BAND, .tier=Info, .source="behaviour", .unit=Number, .scope=Engine, .note="full-match outfield starters"},
  TargetSpec{.id="LAB-SPR", .metric="sprint_per_outfielder_km", .label="Sprint distance per outfielder (km)", .target="", .band=NO_BAND, .tier=Info, .source="behaviour", .unit=Number, .scope=Engine, .note="full-match outfield starters"},
  TargetSpec{.id="LAB-SPRN", .metric="sprints_per_outfielder", .label="Sprints per outfielder", .target="", .band=NO_BAND, .tier=Info, .source="behaviour", .unit=Number, .scope=Engine, .note="full-match outfield starters"},
  // ---- Contests by side INFO (body comparisons; --bodies HOME,AWAY) --------
  TargetSpec{.id="CON-TO", .metric="take_ons_per_team", .label="Take-ons per team", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="CON-TOH", .metric="take_on_success_home", .label="Take-on success, home carrier", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="defender beaten"},
  TargetSpec{.id="CON-TOA", .metric="take_on_success_away", .label="Take-on success, away carrier", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="defender beaten"},
  TargetSpec{.id="CON-TKH", .metric="tackle_duel_success_home", .label="Tackle duel won, home tackler", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="the duel roll, fouls included"},
  TargetSpec{.id="CON-TKA", .metric="tackle_duel_success_away", .label="Tackle duel won, away tackler", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="the duel roll, fouls included"},
  TargetSpec{.id="CON-TKF", .metric="free_ball_tackles_per_team", .label="Free-ball tackles per team", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="a ball outside the carrier's reach, played by an opponent"},
  TargetSpec{.id="CON-TKFW", .metric="free_ball_tackle_success", .label="Free-ball tackles won share", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="the tackler's side next had it under control"},
  TargetSpec{.id="CON-TKC", .metric="contest_tackles_per_team", .label="Contest tackles per team", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="the carrier's ball inside both reaches, played by the tackler"},
  TargetSpec{.id="CON-TKCW", .metric="contest_tackle_success", .label="Contest tackles won share", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="the tackler's side next had it under control"},
  TargetSpec{.id="CON-AIRC", .metric="carrier_air_swings_per_team", .label="Carriers swinging at nothing per team", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="the tackler played it inside his commit moment"},
  TargetSpec{.id="CON-AIRT", .metric="tackler_air_swings_per_team", .label="Tacklers swinging at nothing per team", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="the carrier played it first"},
  TargetSpec{.id="CON-CMT", .metric="commit_misses_per_team", .label="First touches missed, path too new per team", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="the ball's path changed under 0.2 s before (the commit rule)"},
  TargetSpec{.id="CON-BLK", .metric="blocks_per_team", .label="Blocks per team", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="both feet on the ball (a contest's rows 3 and 5)"},
  TargetSpec{.id="CON-BLKC", .metric="blocks_committed_share", .label="Blocks with the carrier's planned touch", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="row 3; the rest his reacting foot (row 5)"},
  TargetSpec{.id="CON-BLKP", .metric="blocks_popped_share", .label="Blocks popped up", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="a strong, near-equal squeeze"},
  TargetSpec{.id="CON-GOIN", .metric="go_ins_per_team", .label="Defenders going into a contest per team", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="steps his brain sent him in (tackling plan step 5)"},
  TargetSpec{.id="CON-GOFF", .metric="go_ins_free_foot_share", .label="Going in on the carrier's free foot", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="a misread: no touch due, nothing shown (row 5)"},
  TargetSpec{.id="LBS-CHK", .metric="loose_shadow_checks_per_match", .label="Loose-ball shadow checks per match", .target="", .band=NO_BAND, .tier=Info, .source="loose-ball shadow", .unit=Number, .scope=Engine, .note="side-steps with a loose ball (brain plan section 27, step O6)"},
  TargetSpec{.id="LBS-AGR", .metric="loose_shadow_agree_share", .label="Shadow agrees with the team's pick", .target="", .band=NO_BAND, .tier=Info, .source="loose-ball shadow", .unit=Percent, .scope=Engine, .note="one player says mine, and he is the team's chaser"},
  TargetSpec{.id="LBS-WRG", .metric="loose_shadow_wrong_player_share", .label="Shadow picks another player", .target="", .band=NO_BAND, .tier=Info, .source="loose-ball shadow", .unit=Percent, .scope=Engine, .note="one player says mine, not the team's chaser"},
  TargetSpec{.id="LBS-NOB", .metric="loose_shadow_nobody_share", .label="Shadow: nobody goes", .target="", .band=NO_BAND, .tier=Info, .source="loose-ball shadow", .unit=Percent, .scope=Engine, .note="nobody says mine, nobody in doubt"},
  TargetSpec{.id="LBS-THR", .metric="loose_shadow_theirs_share", .label="Shadow: nobody, an opponent says mine", .target="", .band=NO_BAND, .tier=Info, .source="loose-ball shadow", .unit=Percent, .scope=Engine, .note="of nobody goes: the ball is theirs (the team still sends a chaser to close him)"},
  TargetSpec{.id="LBS-DBT", .metric="loose_shadow_in_doubt_share", .label="Shadow: only chasers in doubt", .target="", .band=NO_BAND, .tier=Info, .source="loose-ball shadow", .unit=Percent, .scope=Engine, .note="nobody says mine, some chase in doubt"},
  TargetSpec{.id="LBS-TWO", .metric="loose_shadow_two_go_share", .label="Shadow: two or more say mine", .target="", .band=NO_BAND, .tier=Info, .source="loose-ball shadow", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="LBS-LAT", .metric="loose_shadow_later_point_share", .label="Calls judged at a later point", .target="", .band=NO_BAND, .tier=Info, .source="loose-ball shadow", .unit=Percent, .scope=Engine, .note="per player call: nobody could reach the earliest point"},
  TargetSpec{.id="LBW-FST", .metric="loose_wrong_shadow_faster_share", .label="Wrong player: his is sooner (team rules chose another)", .target="", .band=NO_BAND, .tier=Info, .source="loose-ball shadow", .unit=Percent, .scope=Engine, .note="share of wrong-player calls (precise meeting times)"},
  TargetSpec{.id="LBW-TIE", .metric="loose_wrong_near_tie_share", .label="Wrong player: a near tie (0.1 s)", .target="", .band=NO_BAND, .tier=Info, .source="loose-ball shadow", .unit=Percent, .scope=Engine, .note="share of wrong-player calls (precise meeting times)"},
  TargetSpec{.id="LBW-EXT", .metric="loose_wrong_exact_tie_share", .label="  of ties: exactly level (both there before the ball)", .target="", .band=NO_BAND, .tier=Info, .source="loose-ball shadow", .unit=Percent, .scope=Engine, .note="share of wrong-player calls"},
  TargetSpec{.id="LBW-SLW", .metric="loose_wrong_slower_share", .label="Wrong player: his is later (over 0.1 s)", .target="", .band=NO_BAND, .tier=Info, .source="loose-ball shadow", .unit=Percent, .scope=Engine, .note="share of wrong-player calls (precise meeting times)"},
  TargetSpec{.id="LBW-BIG", .metric="loose_wrong_slower_big_share", .label="  of later: by over 0.5 s", .target="", .band=NO_BAND, .tier=Info, .source="loose-ball shadow", .unit=Percent, .scope=Engine, .note="share of wrong-player calls (precise meeting times)"},
  TargetSpec{.id="LBW-LAT", .metric="loose_wrong_slower_later_share", .label="  of later: judged at a later point", .target="", .band=NO_BAND, .tier=Info, .source="loose-ball shadow", .unit=Percent, .scope=Engine, .note="share of wrong-player calls (precise meeting times)"},
  TargetSpec{.id="LBW-MIS", .metric="loose_wrong_slower_missed_share", .label="  of later: pick not compared with", .target="", .band=NO_BAND, .tier=Info, .source="loose-ball shadow", .unit=Percent, .scope=Engine, .note="share of wrong-player calls (precise meeting times)"},
  TargetSpec{.id="LBC-N", .metric="loose_calls_per_match", .label="First-to-commit calls per match", .target="", .band=NO_BAND, .tier=Info, .source="loose-ball shadow", .unit=Number, .scope=Engine, .note="two team-mates going for it, one calls (shadow)"},
  TargetSpec{.id="LBC-R", .metric="loose_calls_right_share", .label="Calls the caller got right", .target="", .band=NO_BAND, .tier=Info, .source="loose-ball shadow", .unit=Percent, .scope=Engine, .note="chance set by his Decisions"},
  TargetSpec{.id="LBC-A", .metric="loose_calls_agree_share", .label="Calls leaving the team's pick as chaser", .target="", .band=NO_BAND, .tier=Info, .source="loose-ball shadow", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="LBC-S", .metric="loose_call_seconds", .label="Seconds from the second commit to the call", .target="", .band=NO_BAND, .tier=Info, .source="loose-ball shadow", .unit=Number, .scope=Engine, .note="Concentration plus Leadership delays"},
  TargetSpec{.id="PRS-CHK", .metric="press_shadow_checks_per_match", .label="Press shadow checks per match", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Number, .scope=Engine, .note="side-steps with an opponent on the ball (brain plan section 28, step P1)"},
  TargetSpec{.id="PRS-AGR", .metric="press_shadow_agree_share", .label="Shadow agrees with the team's presser", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Percent, .scope=Engine, .note="one player says mine, and he is the team's first presser"},
  TargetSpec{.id="PRS-WRG", .metric="press_shadow_wrong_player_share", .label="Shadow picks another presser", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Percent, .scope=Engine, .note="one player says mine, not the team's first presser"},
  TargetSpec{.id="PRS-TWO", .metric="press_shadow_two_go_share", .label="Shadow: two or more say mine", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="PRS-NOB", .metric="press_shadow_nobody_share", .label="Shadow: nobody says mine", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="PRS-DBT", .metric="press_shadow_with_doubt_share", .label="Shadow: one mine, a team-mate in doubt", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Percent, .scope=Engine, .note="a first-to-commit case"},
  TargetSpec{.id="PRW-FST", .metric="press_wrong_shadow_faster_share", .label="  wrong: shadow's man sooner (>0.1 s)", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Percent, .scope=Engine, .note="share of wrong-player calls (pursuit times without eagerness or continuity)"},
  TargetSpec{.id="PRW-TIE", .metric="press_wrong_near_tie_share", .label="  wrong: near tie (within 0.1 s)", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Percent, .scope=Engine, .note="share of wrong-player calls"},
  TargetSpec{.id="PRW-SLW", .metric="press_wrong_slower_share", .label="  wrong: shadow's man later (>0.1 s)", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Percent, .scope=Engine, .note="share of wrong-player calls"},
  TargetSpec{.id="PRW-CON", .metric="press_wrong_pick_pressing_share", .label="  wrong: team's pick already pressing", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Percent, .scope=Engine, .note="share of wrong-player calls (the planner's continuity)"},
  TargetSpec{.id="PRC-N", .metric="press_calls_per_match", .label="Press first-to-commit calls per match", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Number, .scope=Engine, .note="two team-mates going for him, one calls (step P3, shadow)"},
  TargetSpec{.id="PRC-R", .metric="press_calls_right_share", .label="Press calls the caller got right", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Percent, .scope=Engine, .note="chance set by his Decisions (hashed roll in the shadow)"},
  TargetSpec{.id="PRC-A", .metric="press_calls_agree_share", .label="Press calls leaving the team's pick as presser", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="PRC-S", .metric="press_call_seconds", .label="Seconds from the second press commit to the call", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Number, .scope=Engine, .note="Concentration plus Leadership delays"},
  TargetSpec{.id="PRC-C", .metric="press_commits_carried_per_match", .label="Loose-ball commitments carried into the press", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Number, .scope=Engine, .note="per player, when an opponent wins the side's loose ball"},
  TargetSpec{.id="PRG-AGR", .metric="press_go_agree_share", .label="After commits: one presser, the team's pick", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Percent, .scope=Engine, .note="share of checks"},
  TargetSpec{.id="PRG-OTH", .metric="press_go_other_share", .label="After commits: one presser, another player", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Percent, .scope=Engine, .note="share of checks"},
  TargetSpec{.id="PRG-CON", .metric="press_go_other_pick_pressing_share", .label="  of those: team's pick already pressing", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Percent, .scope=Engine, .note="share of another-player checks"},
  TargetSpec{.id="PRG-SEV", .metric="press_go_several_share", .label="After commits: two or more pressing", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Percent, .scope=Engine, .note="share of checks (before the call settles it)"},
  TargetSpec{.id="PRG-NON", .metric="press_go_none_share", .label="After commits: nobody pressing", .target="", .band=NO_BAND, .tier=Info, .source="press shadow", .unit=Percent, .scope=Engine, .note="share of checks"},
  TargetSpec{.id="CON-HOLD", .metric="holds_per_team", .label="Steps held in a contest per team", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="the ball in both reaches, he held"},
  TargetSpec{.id="CON-INW", .metric="intents_win_per_team", .label="Tackles meant to win it per team", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="his brain's intent"},
  TargetSpec{.id="CON-INC", .metric="intents_clear_per_team", .label="Tackles meant to knock it away per team", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="own third, at a stretch, or the last man"},
  TargetSpec{.id="CON-INO", .metric="intents_control_per_team", .label="Free balls simply controlled per team", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="a free ball well away from the carrier"},
  TargetSpec{.id="COL-ALL", .metric="collisions_per_match", .label="Run-ins per match", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="players running into each other (collision plan; Binturong bodies)"},
  TargetSpec{.id="COL-J2", .metric="collisions_jolt_two_per_match", .label="Run-ins jolting 2 m/s or more per match", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="either player"},
  TargetSpec{.id="COL-J4", .metric="collisions_jolt_four_per_match", .label="Run-ins jolting 4 m/s or more per match", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="either player: a big hit becomes possible"},
  TargetSpec{.id="COL-STU", .metric="collision_stumbles_per_match", .label="Players stumbling from a run-in per match", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="kept his feet, lost his balance"},
  TargetSpec{.id="COL-FAL", .metric="collision_falls_per_match", .label="Players knocked over per match", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="slips included"},
  TargetSpec{.id="COL-SLP", .metric="collision_slips_per_match", .label="Stumbles that slipped into a fall per match", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="his footing went"},
  TargetSpec{.id="COL-KEU", .metric="collision_knocks_by_ends_up_share", .label="Stumbles and falls decided by where he ends up", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="the rest by how sudden"},
  TargetSpec{.id="COL-KSJ", .metric="collision_knocks_small_jolt_share", .label="Stumbles and falls from a jolt under 1 m/s", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="a small knock at the edge of what he can run that way"},
  TargetSpec{.id="COL-BIG", .metric="collision_big_hits_per_match", .label="Big hits per match", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="falls kept down 5-10 s at least"},
  TargetSpec{.id="COL-LOS", .metric="collision_falls_losing_ball_per_match", .label="Falls costing the ball per match", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="it runs on loose"},
  TargetSpec{.id="COL-DPF", .metric="collision_down_seconds_per_fall", .label="Seconds down per fall", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="sliding, rolling, getting up"},
  TargetSpec{.id="COL-DWN", .metric="collision_down_play_seconds_per_match", .label="Player-seconds down in open play per match", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="his side a man short"},
  TargetSpec{.id="COL-D15", .metric="collision_downs_fifteen_per_match", .label="Spells down of 15 s or more per match", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="COL-GUB", .metric="collision_get_ups_for_ball_per_match", .label="Jumped up for the ball per match", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="ball within 15 m, still to be won"},
  TargetSpec{.id="COL-GUS", .metric="collision_get_ups_in_stoppage_per_match", .label="Got up in a stoppage per match", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="COL-GUG", .metric="collision_get_ups_gingerly_per_match", .label="Got up gingerly as play ran on per match", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="after 15 s down"},
  TargetSpec{.id="COL-GUF", .metric="collision_get_ups_fixed_per_match", .label="Fixed get-ups per match", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="a brain without the get-up cues"},
  TargetSpec{.id="COL-BOF", .metric="collision_ball_off_fallen_per_match", .label="Ball off a fallen body per match", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="his last touch for restarts"},
  TargetSpec{.id="COL-K0", .metric="collision_knocks_situation_0_share", .label="Knocks from: a re-hit", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="the same pair within 1 s"},
  TargetSpec{.id="COL-K1", .metric="collision_knocks_situation_1_share", .label="Knocks from: both at the ball", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="ball within 2 m of the contact"},
  TargetSpec{.id="COL-K2", .metric="collision_knocks_situation_2_share", .label="Knocks from: his run aimed at the other", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="his target within 1.5 m of him"},
  TargetSpec{.id="COL-K3", .metric="collision_knocks_situation_3_share", .label="Knocks from: an overtake under way", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="he was going round the other"},
  TargetSpec{.id="COL-K4", .metric="collision_knocks_situation_4_share", .label="Knocks from: a race not overtaken", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="same spot, same way, no way round planned"},
  TargetSpec{.id="COL-K5", .metric="collision_knocks_situation_5_share", .label="Knocks from: either standing or slow", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="under 1 m/s"},
  TargetSpec{.id="COL-K6", .metric="collision_knocks_situation_6_share", .label="Knocks from: paths crossing or head-on", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="over 45 degrees apart"},
  TargetSpec{.id="COL-K7", .metric="collision_knocks_situation_7_share", .label="Knocks from: anything else", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-I-hold_shape", .metric="collision_knocks_intent_hold_shape_share", .label="Knocked while holding shape", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-I-carry_ball", .metric="collision_knocks_intent_carry_ball_share", .label="Knocked while carrying the ball", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-I-offer_support", .metric="collision_knocks_intent_offer_support_share", .label="Knocked while offering support", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-I-receive_pass", .metric="collision_knocks_intent_receive_pass_share", .label="Knocked while receiving a pass", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-I-run_in_behind", .metric="collision_knocks_intent_run_in_behind_share", .label="Knocked while running in behind", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-I-attack_box", .metric="collision_knocks_intent_attack_box_share", .label="Knocked while attacking the box", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-I-overlap", .metric="collision_knocks_intent_overlap_share", .label="Knocked while overlapping", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-I-press_ball", .metric="collision_knocks_intent_press_ball_share", .label="Knocked while pressing the ball", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-I-cover_press", .metric="collision_knocks_intent_cover_press_share", .label="Knocked while covering the press", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-I-block_passing_lane", .metric="collision_knocks_intent_block_passing_lane_share", .label="Knocked while blocking a passing lane", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-I-mark_opponent", .metric="collision_knocks_intent_mark_opponent_share", .label="Knocked while marking", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-I-claim_loose_ball", .metric="collision_knocks_intent_claim_loose_ball_share", .label="Knocked while claiming a loose ball", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-I-recover_shape", .metric="collision_knocks_intent_recover_shape_share", .label="Knocked while recovering shape", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-I-goalkeep", .metric="collision_knocks_intent_goalkeep_share", .label="Knocked while keeping goal", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-CH", .metric="collision_chase_knocks_share", .label="Knocks with a loose-ball chaser in them", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="either player claiming a loose ball"},
  TargetSpec{.id="COL-CHK", .metric="collision_chase_knocks_on_chaser_share", .label="Of those, the chaser knocked", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="rather than the other"},
  TargetSpec{.id="COL-CW-claim_loose_ball", .metric="collision_chase_knocks_with_claim_loose_ball_share", .label="Chase knocks, the other an opponent also chasing it", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-CW-team_mate_chasing", .metric="collision_chase_knocks_with_team_mate_chasing_share", .label="Chase knocks, the other a team-mate also chasing it", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-CW-hold_shape", .metric="collision_chase_knocks_with_hold_shape_share", .label="Chase knocks, the other holding shape", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-CW-carry_ball", .metric="collision_chase_knocks_with_carry_ball_share", .label="Chase knocks, the other carrying the ball", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-CW-offer_support", .metric="collision_chase_knocks_with_offer_support_share", .label="Chase knocks, the other offering support", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-CW-receive_pass", .metric="collision_chase_knocks_with_receive_pass_share", .label="Chase knocks, the other receiving a pass", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-CW-run_in_behind", .metric="collision_chase_knocks_with_run_in_behind_share", .label="Chase knocks, the other running in behind", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-CW-attack_box", .metric="collision_chase_knocks_with_attack_box_share", .label="Chase knocks, the other attacking the box", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-CW-overlap", .metric="collision_chase_knocks_with_overlap_share", .label="Chase knocks, the other overlapping", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-CW-press_ball", .metric="collision_chase_knocks_with_press_ball_share", .label="Chase knocks, the other pressing the ball", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-CW-cover_press", .metric="collision_chase_knocks_with_cover_press_share", .label="Chase knocks, the other covering the press", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-CW-block_passing_lane", .metric="collision_chase_knocks_with_block_passing_lane_share", .label="Chase knocks, the other blocking a passing lane", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-CW-mark_opponent", .metric="collision_chase_knocks_with_mark_opponent_share", .label="Chase knocks, the other marking", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-CW-recover_shape", .metric="collision_chase_knocks_with_recover_shape_share", .label="Chase knocks, the other recovering shape", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="COL-CW-goalkeep", .metric="collision_chase_knocks_with_goalkeep_share", .label="Chase knocks, the other keeping goal", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="WAY-SCHA", .metric="way_rounds_started_chase_per_match", .label="Ways round started per match: chasers", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="way-round plan"},
  TargetSpec{.id="WAY-RCHA", .metric="way_round_refreshes_chase_per_match", .label="Plan refreshes going round per match: chasers", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="WAY-SHOL", .metric="way_rounds_started_hold_per_match", .label="Ways round started per match: players moving to hold shape", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="way-round plan"},
  TargetSpec{.id="WAY-RHOL", .metric="way_round_refreshes_hold_per_match", .label="Plan refreshes going round per match: players moving to hold shape", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="WAY-SSUP", .metric="way_rounds_started_support_per_match", .label="Ways round started per match: players offering support", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="way-round plan"},
  TargetSpec{.id="WAY-RSUP", .metric="way_round_refreshes_support_per_match", .label="Plan refreshes going round per match: players offering support", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="WAY-SREC", .metric="way_rounds_started_recover_per_match", .label="Ways round started per match: players recovering shape", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="way-round plan"},
  TargetSpec{.id="WAY-RREC", .metric="way_round_refreshes_recover_per_match", .label="Plan refreshes going round per match: players recovering shape", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="GW-SLOW", .metric="give_way_slows_per_match", .label="Plans easing off to give way per match", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="someone with the right of way crossing ahead"},
  TargetSpec{.id="GW-ROUND", .metric="give_way_rounds_per_match", .label="Plans going round alone to give way per match", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note="head-on or the same way"},
  TargetSpec{.id="GW-HOLD", .metric="give_way_holds_per_match", .label="Plans holding the line with the right of way per match", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="CON-AERH", .metric="aerial_home_share", .label="Contested aerial duels won by home", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="both sides within reach"},
  TargetSpec{.id="CON-CLMH", .metric="keeper_claim_success_home", .label="Keeper claims held, home keeper", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="the rest punched clear"},
  TargetSpec{.id="CON-CLMA", .metric="keeper_claim_success_away", .label="Keeper claims held, away keeper", .target="", .band=NO_BAND, .tier=Info, .source="contests", .unit=Percent, .scope=Engine, .note="the rest punched clear"},
  // ---- Movement jitter INFO (outfield players, open play) -------------------
  TargetSpec{.id="JIT-MOVE", .metric="jitter_target_move_m", .label="Target move per scheduled refresh (m)", .target="", .band=NO_BAND, .tier=Info, .source="jitter", .unit=Number, .scope=Engine, .note="every 0.2 s"},
  TargetSpec{.id="JIT-MOVS", .metric="jitter_moving_share", .label="Scheduled refreshes moving the target > 0.5 m", .target="", .band=NO_BAND, .tier=Info, .source="jitter", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="JIT-REV", .metric="jitter_reversal_share", .label="Target moves that turn back on the previous move", .target="", .band=NO_BAND, .tier=Info, .source="jitter", .unit=Percent, .scope=Engine, .note="both moves > 0.5 m, more than 90 degrees apart"},
  TargetSpec{.id="JIT-REVM", .metric="jitter_reversals_per_minute", .label="Target reversals per player-minute", .target="", .band=NO_BAND, .tier=Info, .source="jitter", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="JIT-SWM", .metric="jitter_switches_per_minute", .label="Intent switches per player-minute", .target="", .band=NO_BAND, .tier=Info, .source="jitter", .unit=Number, .scope=Engine, .note="scheduled refreshes"},
  TargetSpec{.id="JIT-FLIP", .metric="jitter_flip_back_share", .label="Intent switches undone within 1 s", .target="", .band=NO_BAND, .tier=Info, .source="jitter", .unit=Percent, .scope=Engine, .note="back to the intent held before"},
  TargetSpec{.id="JIT-EVT", .metric="jitter_event_refreshes_per_minute", .label="Forced replans per player-minute", .target="", .band=NO_BAND, .tier=Info, .source="jitter", .unit=Number, .scope=Engine, .note="ball changed hands, or a pass or shot started or ended"},
  TargetSpec{.id="JIT-EVTM", .metric="jitter_event_target_move_m", .label="Target move per forced replan (m)", .target="", .band=NO_BAND, .tier=Info, .source="jitter", .unit=Number, .scope=Engine, .note=""},
  TargetSpec{.id="JIT-EFF", .metric="jitter_run_efficiency", .label="Run efficiency (ground gained / distance run, 2 s windows)", .target="", .band=NO_BAND, .tier=Info, .source="jitter", .unit=Percent, .scope=Engine, .note=""},
  TargetSpec{.id="JIT-TURN", .metric="jitter_sharp_turn_share", .label="Runs turned > 90 degrees within 0.2 s", .target="", .band=NO_BAND, .tier=Info, .source="jitter", .unit=Percent, .scope=Engine, .note="both speeds > 1 m/s"},
  TargetSpec{.id="JIT-SURGE", .metric="jitter_speed_reversals_per_running_minute", .label="Speed surges (rise/fall reversals) per running minute", .target="", .band=NO_BAND, .tier=Info, .source="jitter", .unit=Number, .scope=Engine, .note="above 2 m/s; changes over 0.5 m/s^2 per step"},
  // ---- World (season runs) --------------------------------------------------
  TargetSpec{.id="CT-W01", .metric="", .label="Output peak age by position", .target="FW~25, MF 25-27, DF~27", .band=NO_BAND, .tier=Gate, .source="PEER", .unit=Number, .scope=World, .note="not measurable in one season: needs multi-season rating-by-age curves"},
  TargetSpec{.id="CT-W02", .metric="", .label="Physical peaks", .target="25.7 / 24.8 / 26.0", .band=NO_BAND, .tier=Monitor, .source="PEER", .unit=Number, .scope=World, .note="not measurable yet"},
  TargetSpec{.id="CT-W03", .metric="", .label="Peak market-value age", .target="26-30", .band=Band{.low=26.0, .high=30.0}, .tier=Monitor, .source="PEER", .unit=Number, .scope=World, .note="not measurable yet"},
  TargetSpec{.id="CT-W04", .metric="mean_age_top_squads", .label="Mean age of top-division squads", .target="~26.5", .band=Band{.low=24.9, .high=26.5}, .tier=Monitor, .source="PEER", .unit=Number, .scope=World, .note="club mean ages at season end, top tier"},
  TargetSpec{.id="CT-W05", .metric="injuries_per_player_season", .label="Injuries per player-season", .target="~2.0", .band=Band{.low=1.6, .high=2.4}, .tier=Gate, .source="PEER", .unit=Number, .scope=World, .note="injury onsets (daily poll) / mean registered players"},
  TargetSpec{.id="CT-W06", .metric="", .label="Match / training injury incidence", .target="21-27.5 / 3.5-4.1 per 1000 h", .band=NO_BAND, .tier=Gate, .source="PEER", .unit=Number, .scope=World, .note="not measurable yet: exposure hours not tracked"},
  TargetSpec{.id="CT-W07", .metric="hamstring_share", .label="Hamstring share of injuries", .target="19-24%", .band=Band{.low=0.19, .high=0.24}, .tier=Monitor, .source="PEER", .unit=Percent, .scope=World, .note="strain + tightness"},
  TargetSpec{.id="CT-W08", .metric="hamstring_layoff_median", .label="Layoff median: hamstring (days)", .target="13", .band=Band{.low=10.0, .high=17.0}, .tier=Gate, .source="PEER", .unit=Number, .scope=World, .note="hamstring strain"},
  TargetSpec{.id="CT-W08B", .metric="ankle_layoff_median", .label="Layoff median: ankle (days)", .target="8", .band=Band{.low=6.0, .high=11.0}, .tier=Gate, .source="PEER", .unit=Number, .scope=World, .note="ankle sprain"},
  TargetSpec{.id="CT-W08C", .metric="acl_layoff_median", .label="Layoff median: ACL (days)", .target="205", .band=Band{.low=160.0, .high=250.0}, .tier=Gate, .source="PEER", .unit=Number, .scope=World, .note="knee ACL"},
  TargetSpec{.id="CT-W09", .metric="", .label="Re-injury share / duration multiplier", .target="12%; x1.33", .band=NO_BAND, .tier=Monitor, .source="PEER", .unit=Number, .scope=World, .note="not measurable yet"},
  TargetSpec{.id="CT-W10", .metric="", .label="Muscle-injury risk with <=4 days recovery", .target="RR 1.32", .band=NO_BAND, .tier=Monitor, .source="PEER", .unit=Number, .scope=World, .note="not measurable yet"},
  TargetSpec{.id="CT-W11", .metric="", .label="Residual impairment 72 h after a match", .target="2-4%", .band=NO_BAND, .tier=Monitor, .source="PEER", .unit=Number, .scope=World, .note="not measurable yet"},
  TargetSpec{.id="CT-W12", .metric="", .label="Scholar outcomes", .target="4% / 6%", .band=NO_BAND, .tier=Monitor, .source="PEER", .unit=Number, .scope=World, .note="not measurable in one season"},
  TargetSpec{.id="CT-W13", .metric="", .label="Senior birth-quarter shares", .target="31/26/24/20%", .band=NO_BAND, .tier=Monitor, .source="PEER", .unit=Number, .scope=World, .note="not measurable yet: birth dates not exposed"},
  TargetSpec{.id="CT-W14", .metric="fee_transfer_share", .label="Share of transfers involving a fee", .target="17.7%", .band=Band{.low=0.15, .high=0.25}, .tier=Gate, .source="REG", .unit=Percent, .scope=World, .note="permanent with fee / (permanent + free + pre-contract + loan)"},
  TargetSpec{.id="CT-W15", .metric="", .label="Agent fees / fee spend", .target="~10%", .band=Band{.low=0.08, .high=0.12}, .tier=Monitor, .source="REG", .unit=Percent, .scope=World, .note="not measurable yet: agent fees not in the ledger by category"},
  TargetSpec{.id="CT-W16", .metric="", .label="Unpaid share of squad fees at year end", .target="~33%", .band=NO_BAND, .tier=Monitor, .source="REG", .unit=Percent, .scope=World, .note="not measurable yet"},
  TargetSpec{.id="CT-W17", .metric="", .label="Fee model correlation with clean valuation", .target="r 0.80-0.85", .band=NO_BAND, .tier=Monitor, .source="PEER", .unit=Number, .scope=World, .note="not measurable yet"},
  TargetSpec{.id="CT-W18", .metric="", .label="Deadline-day share of window spend", .target="~11%", .band=Band{.low=0.10, .high=0.15}, .tier=Monitor, .source="PUB", .unit=Percent, .scope=World, .note="not measurable yet"},
  TargetSpec{.id="CT-W19", .metric="", .label="Loan outcomes return/sold/re-loaned", .target="29.6/43.4/27.0%", .band=NO_BAND, .tier=Monitor, .source="PEER", .unit=Percent, .scope=World, .note="not measurable in one season"},
  TargetSpec{.id="CT-W20", .metric="", .label="Scouting discrimination d'", .target="0.8 / 1.3 / 2.1", .band=NO_BAND, .tier=Monitor, .source="PEER", .unit=Number, .scope=World, .note="not measurable yet"},
  TargetSpec{.id="CT-W21", .metric="wage_revenue_top", .label="Wage / revenue, top divisions", .target="65%", .band=Band{.low=0.57, .high=0.73}, .tier=Gate, .source="REG", .unit=Percent, .scope=World, .note="player wages / (matchday + TV + commercial + prize money), clubs as units"},
  TargetSpec{.id="CT-W22", .metric="", .label="Revenue mix, richest league", .target="TV 46 / gate 14 / commercial 32%", .band=NO_BAND, .tier=Monitor, .source="REG", .unit=Percent, .scope=World, .note="see LAB-REVMIX table"},
  TargetSpec{.id="CT-W23", .metric="", .label="Central-distribution top/bottom ratio", .target="~1.6x", .band=NO_BAND, .tier=Monitor, .source="PUB", .unit=Number, .scope=World, .note="not measurable yet"},
  TargetSpec{.id="CT-W24", .metric="", .label="Insolvency hazard (top two tiers)", .target="0.6%/yr", .band=Band{.low=0.002, .high=0.012}, .tier=Gate, .source="REG/PEER", .unit=Percent, .scope=World, .note="not measurable yet: no insolvency events (see LAB-NEGCASH)"},
  TargetSpec{.id="CT-W25", .metric="", .label="Clubs receiving owner equity per year", .target="~20%", .band=NO_BAND, .tier=Monitor, .source="REG", .unit=Percent, .scope=World, .note="not measurable yet"},
  TargetSpec{.id="CT-W26", .metric="", .label="Top-tier dismissals per season", .target="~9 of 20", .band=Band{.low=6.0, .high=12.0}, .tier=Gate, .source="PUB", .unit=Number, .scope=World, .note="not measurable yet: AI clubs have no managers"},
  TargetSpec{.id="CT-W27", .metric="", .label="Dismissed-manager tenure", .target="< 1 year", .band=Band{.low=0.74, .high=0.87}, .tier=Monitor, .source="PUB", .unit=Number, .scope=World, .note="not measurable yet"},
  TargetSpec{.id="CT-W28", .metric="wage_position_r2", .label="Wage-to-position R2 (annual)", .target="~0.45", .band=Band{.low=0.35, .high=0.55}, .tier=Gate, .source="PEER", .unit=Number, .scope=World, .note="ln(wage bill / league mean) vs -ln(p/(N+1-p)); bootstrap over clubs"},
  TargetSpec{.id="CT-W29", .metric="", .label="Top-division membership persistence", .target="68% of decade", .band=NO_BAND, .tier=Monitor, .source="PEER", .unit=Percent, .scope=World, .note="not measurable in one season"},
  TargetSpec{.id="CT-W30", .metric="", .label="Result momentum after ability control", .target="<= 0", .band=NO_BAND, .tier=Gate, .source="PEER", .unit=Number, .scope=World, .note="not measurable yet"},
  TargetSpec{.id="CT-W31", .metric="", .label="New-manager effect", .target="~0", .band=NO_BAND, .tier=Gate, .source="PEER", .unit=Number, .scope=World, .note="not measurable yet: AI clubs have no managers"},
  TargetSpec{.id="CT-W32", .metric="", .label="New-signing share weakest vs strongest", .target="42% vs 31%", .band=NO_BAND, .tier=Monitor, .source="PEER", .unit=Percent, .scope=World, .note="not measurable yet"},
  // ---- World INFO (no spec band) --------------------------------------------
  TargetSpec{.id="LAB-CHAMP", .metric="champion_ppg", .label="Champion points per game", .target="~2.2-2.4 (big-5)", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=World, .note="league-seasons as units"},
  TargetSpec{.id="LAB-CHAMPPTS", .metric="champion_points", .label="Champion points", .target="~86 over 38 games", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=World, .note=""},
  TargetSpec{.id="LAB-RELEG", .metric="best_relegated_ppg", .label="Best relegated club points per game", .target="~0.9 (big-5)", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=World, .note="highest-placed relegated club"},
  TargetSpec{.id="LAB-RELEGPTS", .metric="best_relegated_points", .label="Best relegated club points", .target="~34 over 38 games", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=World, .note=""},
  TargetSpec{.id="LAB-SCORER", .metric="top_scorer_goals", .label="League top scorer goals", .target="~23-27 over 38 games", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=World, .note=""},
  TargetSpec{.id="LAB-SCORERPG", .metric="top_scorer_goals_per_game", .label="Top scorer goals per team game", .target="~0.6-0.7", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=World, .note=""},
  TargetSpec{.id="LAB-PTSSD", .metric="points_sd_ppg", .label="Points spread (SD of points per game)", .target="~0.45-0.5", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=World, .note="within league, then averaged"},
  TargetSpec{.id="LAB-GDSD", .metric="goal_diff_sd_per_game", .label="Goal difference spread (SD per game)", .target="~0.6-0.7", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=World, .note="within league, then averaged"},
  TargetSpec{.id="LAB-REV", .metric="median_club_revenue", .label="Median club revenue (season)", .target="-", .band=NO_BAND, .tier=Info, .source="economy", .unit=Number, .scope=World, .note="matchday + TV + commercial + prize money"},
  TargetSpec{.id="LAB-NET", .metric="median_club_net", .label="Median club net result (season)", .target="~0 or slightly negative", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=World, .note="all ledger categories except the opening balance"},
  TargetSpec{.id="LAB-WAGELOW", .metric="wage_revenue_lower", .label="Wage / revenue, lower divisions", .target="up to 125%", .band=NO_BAND, .tier=Info, .source="REG (CT-W21 note)", .unit=Percent, .scope=World, .note=""},
  TargetSpec{.id="LAB-NEGCASH", .metric="negative_cash_share", .label="Clubs with negative cash at season end", .target="few %", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Percent, .scope=World, .note=""},
  TargetSpec{.id="LAB-XFER", .metric="transfers_per_club", .label="Transfers per club per season", .target="~10-15 moves", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=World, .note="all recorded moves except releases and loan returns"},
  TargetSpec{.id="LAB-FEEPC", .metric="fee_moves_per_club", .label="Fee-paying transfers per club per season", .target="-", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=World, .note=""},
  TargetSpec{.id="LAB-LOANPC", .metric="loans_per_club", .label="Loans per club per season", .target="-", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=World, .note=""},
  TargetSpec{.id="LAB-SQUAD", .metric="squad_size", .label="Squad size (season end)", .target="~25-30", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=World, .note=""},
  TargetSpec{.id="LAB-AGE", .metric="player_age_mean", .label="Mean age, all club players", .target="~25-26", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=World, .note=""},
  TargetSpec{.id="LAB-YOUTH", .metric="new_players_per_club", .label="New players per club per season (youth intake)", .target="top academy ~8-15", .band=NO_BAND, .tier=Info, .source="FR-098", .unit=Number, .scope=World, .note="IDs present after the rollover but not at season start"},
  TargetSpec{.id="LAB-RETIRE", .metric="removed_players_per_club", .label="Removed players per club per season (retirements)", .target="-", .band=NO_BAND, .tier=Info, .source="orientation", .unit=Number, .scope=World, .note="IDs present at season start but gone after the rollover"},
  TargetSpec{.id="LAB-WORLDSPEED", .metric="ms_per_world_match", .label="World simulation cost per match (ms)", .target="-", .band=NO_BAND, .tier=Info, .source="performance", .unit=Number, .scope=World, .note="wall time of the season / matches played"},
};
// clang-format on

const char* unitName(Unit unit) { return unit == Percent ? "%" : ""; }

std::optional<Unit> unitFromName(std::string_view name)
{
  if (name == "%")
  {
    return Percent;
  }
  if (name.empty())
  {
    return Number;
  }
  return std::nullopt;
}

template <typename Enum, std::size_t N>
Enum enumFromName(std::string_view name,
                  const std::array<std::string_view, N>& names)
{
  for (std::size_t i = 0; i < N; ++i)
  {
    if (names.at(i) == name)
    {
      return static_cast<Enum>(i);
    }
  }
  throw nlohmann::json::other_error::create(
      UNKNOWN_ENUM_ERROR, std::format("unknown enum value '{}'", name),
      nullptr);
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
    {
      cell += "\\|";
    }
    else if (c == '\n')
    {
      cell += ' ';
    }
    else
    {
      cell += c;
    }
  }
  return cell;
}

std::string bandText(const ReportRow& row)
{
  if (!row.band)
  {
    return "-";
  }
  return std::format("{} - {}", formatValue(row.band->low, row.unit),
                     formatValue(row.band->high, row.unit));
}

std::string ciText(const ReportRow& row)
{
  if (!row.estimate.valid() || !std::isfinite(row.estimate.low))
  {
    return "-";
  }
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
  if (table.header.empty())
  {
    return;
  }
  out += "|";
  for (const std::string& cell : table.header)
  {
    out += std::format(" {} |", escapeCell(cell));
  }
  out += "\n|";
  for ([[maybe_unused]] const std::string& header : table.header)
  {
    out += "---|";
  }
  out += "\n";
  for (const auto& row : table.rows)
  {
    out += "|";
    for (const std::string& cell : row)
    {
      out += std::format(" {} |", escapeCell(cell));
    }
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
  using enum Tier;
  Summary summary;
  for (const ReportRow& row : rows)
  {
    if (row.verdict == Verdict::NotMeasured)
    {
      if (row.tier != Info)
      {
        ++summary.not_measured;
      }
      continue;
    }
    if (row.tier == Gate)
    {
      if (row.verdict == Verdict::Pass)
      {
        ++summary.gate_pass;
      }
      if (row.verdict == Verdict::Warn)
      {
        ++summary.gate_warn;
      }
      if (row.verdict == Verdict::Fail)
      {
        ++summary.gate_fail;
      }
    }
    else if (row.tier == Monitor)
    {
      ++summary.monitor_evaluated;
      if (row.verdict == Verdict::Pass)
      {
        ++summary.monitor_pass;
      }
    }
  }
  return summary;
}

int LabReport::exitCode() const
{
  const Summary result = summary();
  if (result.gate_fail > 0)
  {
    return ExitCode::GATE_FAILED;
  }
  if (result.monitorPassRate() < MONITOR_PASS_THRESHOLD)
  {
    return ExitCode::MONITOR_BELOW_THRESHOLD;
  }
  return ExitCode::OK;
}

std::vector<ReportRow> evaluateTargets(
    const std::map<std::string, Estimate>& metrics,
    std::span<const Scope> scopes)
{
  std::vector<ReportRow> rows;
  for (const TargetSpec& spec : targetTable())
  {
    if (std::ranges::find(scopes, spec.scope) == scopes.end())
    {
      continue;
    }
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
    if (const auto found = spec.metric.empty()
                               ? metrics.end()
                               : metrics.find(std::string(spec.metric));
        found != metrics.end())
    {
      row.estimate = found->second;
    }
    else if (!spec.metric.empty())
    {
      row.note = row.note.empty() ? "no data in this mode"
                                  : row.note + "; no data in this mode";
    }
    row.verdict = row.tier == Tier::Info && row.estimate.valid()
                      ? Verdict::Info
                      : classify(row.estimate, row.band);
    rows.push_back(std::move(row));
  }
  return rows;
}

std::string_view verdictName(Verdict verdict)
{
  return VERDICT_NAMES.at(
      static_cast<std::size_t>(std::to_underlying(verdict)));
}

std::string_view tierName(Tier tier)
{
  return TIER_NAMES.at(static_cast<std::size_t>(std::to_underlying(tier)));
}

std::string formatValue(double value, Unit unit)
{
  if (!std::isfinite(value))
  {
    return "-";
  }
  if (unit == Percent)
  {
    return std::format("{:.1f}%", value * 100.0);
  }
  const double magnitude = std::abs(value);
  if (magnitude >= INTEGER_DISPLAY_THRESHOLD)
  {
    return std::format("{:.0f}", value);
  }
  if (magnitude >= 100.0)
  {
    return std::format("{:.1f}", value);
  }
  if (magnitude >= 1.0)
  {
    return std::format("{:.2f}", value);
  }
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
  {
    tables.push_back({{"title", table.title},
                      {"header", table.header},
                      {"rows", table.rows}});
  }
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
  {
    throw nlohmann::json::other_error::create(
        UNSUPPORTED_SCHEMA_ERROR, "unsupported lab report schema version",
        nullptr);
  }
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
    {
      throw nlohmann::json::other_error::create(UNKNOWN_UNIT_ERROR,
                                                "unknown unit", nullptr);
    }
    row.unit = *unit;
    row.note = item.at("note").get<std::string>();
    row.estimate = estimateFrom(item.at("estimate"));
    row.verdict = enumFromName<Verdict>(item.at("verdict").get<std::string>(),
                                        VERDICT_NAMES);
    if (const nlohmann::json& band = item.at("band"); !band.is_null())
    {
      row.band = Band{.low = band.at(0).get<double>(),
                      .high = band.at(1).get<double>()};
    }
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
  {
    out += std::format(", {:.1f} ms/match", report.ms_per_match);
  }
  if (report.seconds_per_season > 0.0)
  {
    out += std::format(", {:.1f} s/season", report.seconds_per_season);
  }
  out += "\n";
  for (const auto& [key, value] : report.parameters)
  {
    out += std::format("- {}: {}\n", key, value);
  }
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
    {
      if (predicate(row))
      {
        selected.push_back(row);
      }
    }
    if (selected.empty())
    {
      return;
    }
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
  if (!report.tables.empty())
  {
    out += "\n## Distributions and tables\n";
  }
  for (const TextTable& table : report.tables)
  {
    appendTextTable(out, table);
  }
  if (!report.notes.empty())
  {
    out += "\n## Notes\n\n";
    for (const std::string& note : report.notes)
    {
      out += std::format("- {}\n", note);
    }
  }
  section("Not measurable yet", [](const ReportRow& row)
          { return row.verdict == Verdict::NotMeasured; });
  return out;
}
}  // namespace Lab
