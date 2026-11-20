// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Binturong referee's MatchEngine members (defined in
// match_referee_binturong.cpp). Included inside the MatchEngine class body,
// so no include guard and no includes of its own.

// clang-format off
  // --- Binturong referee -----------------------------------------------------
  /** Whether the offender stopped an obvious goal-scoring opportunity. */
  bool deniesGoalChanceBinturong(const MatchPlayer& victim, const MatchPlayer& offender) const;
  void commitFoulBinturong(MatchPlayer& offender, MatchPlayer& victim, bool ballWon, bool reckless);
  void updatePendingAdvantageBinturong(float dt);
  bool judgeOffsideBinturong(const MatchPlayer& passer, const PassOption& option, float defenderLine) const;
  int addedMinutesDueBinturong() const;
// clang-format on
