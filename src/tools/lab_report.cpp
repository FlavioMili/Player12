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
  TargetSpec{"CT-M11", "", "High-intensity running last vs first 15'", "-15%", Band{-0.45, -0.08}, Gate, "PEER", Percent, Engine, "not measurable yet: no speed-zone or 15-minute distance bins"},
  TargetSpec{"CT-M12", "", "Total distance last vs first 15'", "-14%", Band{-0.17, -0.12}, Monitor, "PEER", Percent, Engine, "not measurable yet: only half splits exist (see LAB-DIST-H2 info row)"},
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
  TargetSpec{"LAB-POSS", "possession_spread", "Possession spread |home - 50| (pts)", "~9-10 pts", NO_BAND, Info, "orientation", Number, Match, ""},
  TargetSpec{"LAB-ADDED", "added_time_minutes", "Added time per match (min)", "~10.5", NO_BAND, Info, "orientation", Number, Engine, ""},
  TargetSpec{"LAB-INJ", "injuries_per_team", "In-match injuries per team", "~0.2-0.3", NO_BAND, Info, "orientation", Number, Engine, ""},
  TargetSpec{"LAB-DIST-H2", "second_half_distance_change", "2nd vs 1st half distance per minute", "-3 to -10%", NO_BAND, Info, "orientation", Percent, Engine, "full-match outfield starters"},
  TargetSpec{"LAB-ABND", "abandoned_share", "Abandoned matches", "0%", NO_BAND, Info, "sanity", Percent, Engine, "excluded from all other match metrics"},
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
  for ([[maybe_unused]] const std::string& header : table.header) out += "---|";
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
  using enum Tier;
  Summary summary;
  for (const ReportRow& row : rows)
  {
    if (row.verdict == Verdict::NotMeasured)
    {
      if (row.tier != Info) ++summary.not_measured;
      continue;
    }
    if (row.tier == Gate)
    {
      if (row.verdict == Verdict::Pass) ++summary.gate_pass;
      if (row.verdict == Verdict::Warn) ++summary.gate_warn;
      if (row.verdict == Verdict::Fail) ++summary.gate_fail;
    }
    else if (row.tier == Monitor)
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
    if (const auto found = spec.metric.empty()
                               ? metrics.end()
                               : metrics.find(std::string(spec.metric));
        found != metrics.end())
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
  return VERDICT_NAMES[static_cast<std::size_t>(std::to_underlying(verdict))];
}

std::string_view tierName(Tier tier)
{
  return TIER_NAMES[static_cast<std::size_t>(std::to_underlying(tier))];
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
