// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Aardvark manager's MatchEngine members (defined in
// match_manager_aardvark.cpp). Included inside the MatchEngine class body,
// so no include guard and no includes of its own.

// clang-format off
  // --- Aardvark manager ------------------------------------------------------
  void runAiSubstitutionsForAardvark(bool homeTeam);
  /** Takes off the side's players the medical staff limited to an hour. */
  void runMedicalSubstitutionsAardvark(bool homeTeam);
  const Player* chooseReplacementAardvark(bool homeTeam, PlayerRole role, bool wantGoalkeeper) const;
  void runAiTouchlineAardvark(bool homeTeam);
// clang-format on
