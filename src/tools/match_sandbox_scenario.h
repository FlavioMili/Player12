// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "model/match_recorder.h"

class MatchEngine;
class SandboxRecorder;

/**
 * Turns a recorded decision into a test case (debugger phase 5): a TEST for
 * test/test_match_scenarios.cpp that places its dummy teams where the 22
 * players stood just before the decision, with the same carrier and runs.
 * The dummy players are uniformly rated with a fixed 4-4-2 of roles, so the
 * case reproduces the geometry, not the real players' attributes or roles;
 * the test states what was decided and leaves the expectation to write.
 */
namespace ScenarioExport
{
/** @param before The match at the end of the tick before the decision. */
std::string testCase(const MatchEngine& before, const SandboxRecorder& names,
                     const MatchDecisionDetail& decision, std::uint64_t tick,
                     float minute);

/** Writes the case next to the recordings (scenarios/); returns the path,
 * or an empty path and `error`. */
std::filesystem::path save(const std::string& text, std::uint64_t tick,
                           std::string& error);
}  // namespace ScenarioExport
