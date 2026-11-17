// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <string>

class GameController;

/**
 * Headless checks of the engine debugger (match_sandbox --check):
 *  - recording never changes a match: the same seeds played with and without
 *    the recorder (and with highlight look-ahead copies) end identically;
 *  - measurements: full-match simulation time with and without the
 *    recorder, the size of the log, and the cost of copying an engine.
 * Prints a report to stdout. Returns 0 when every match was identical.
 */
enum class SandboxCheckGroup
{
  ALL,
  FIRST_TOUCH,
  RESTARTS
};
int runSandboxChecks(GameController& controller, int matches,
                     SandboxCheckGroup group = SandboxCheckGroup::ALL);

/** Stage 1: JSONL ownership/contact-gate trace, paired with an unobserved
 * engine at the same seed. Seconds must be finite and in (0, 3600]. */
int runPossessionTrace(GameController& controller, double seconds,
                       const std::string& outputPath);

/** Diagnostic (match_sandbox --trace-match SECONDS [CASES]): a match between
 * the first two clubs of the first league; each stumble or fall between two
 * players getting into position, with both players' last 1.2 s. */
int runMatchTrace(GameController& controller, double seconds, int cases,
                  const std::string& kind = "neither");
