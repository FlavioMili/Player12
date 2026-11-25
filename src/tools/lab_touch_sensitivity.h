// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <string>
#include <vector>

struct StatsConfig;

/**
 * fm_lab touch-sensitivity: which variables move the first touch most (see
 * Notes and plans/match_engine_first_touch_tuning_plan.md, section 6).
 *
 * Every situation of the First touch drill, at several First Touch ratings,
 * first as tuned (the baseline), then with one variable at a time set low
 * and high: each tuning knob, and the receiver's own Pace, Condition, Foot
 * and First Touch. Same seeds throughout, so the comparison is like for
 * like. The variables are ranked by their largest effect on balls lost and
 * on heavy touches, side by side, with the effects on the angle and weight
 * errors beside them.
 *
 * Options: --repeats N (runs per cell, default 100), --ratings A,B,...
 * (First Touch, default 30,50,70,90), --situations all|NAME;NAME,
 * --only NAME;NAME (variables), --step S (a knob's low and high: its value
 * times 1 - S and 1 + S, default 0.5, kept within its range), --range
 * "Name=low:high" (any variable's two values as given, beyond its range
 * too; repeatable), --seed S, --out-dir DIR (report.md, cells.csv,
 * ranking.csv; default a fresh fm-lab-touch-* folder in the temp folder),
 * --brains and --bodies (HOME,AWAY or one for both; default binturong, whose
 * body has the new first touch).
 * Returns 0, or 3 for invalid options.
 */
int runTouchSensitivity(const StatsConfig& config,
                        const std::vector<std::string>& arguments);
