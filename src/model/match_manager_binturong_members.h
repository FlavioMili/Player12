// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Binturong manager's MatchEngine members (defined in
// match_manager_binturong.cpp). Included inside the MatchEngine class body,
// so no include guard and no includes of its own.

// clang-format off
  // --- Binturong manager -----------------------------------------------------
  void runAiSubstitutionsForBinturong(bool homeTeam);
  /** Takes off the side's players the medical staff limited to an hour. */
  void runMedicalSubstitutionsBinturong(bool homeTeam);
  const Player* chooseReplacementBinturong(bool homeTeam, PlayerRole role, bool wantGoalkeeper) const;
  void runAiTouchlineBinturong(bool homeTeam);
// clang-format on
