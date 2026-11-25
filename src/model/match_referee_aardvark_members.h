// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Aardvark referee's MatchEngine members (defined in
// match_referee_aardvark.cpp). Included inside the MatchEngine class body,
// so no include guard and no includes of its own.

// clang-format off
  // --- Aardvark referee ------------------------------------------------------
  /** Whether the offender stopped an obvious goal-scoring opportunity. */
  bool deniesGoalChanceAardvark(const MatchPlayer& victim, const MatchPlayer& offender) const;
  void commitFoulAardvark(MatchPlayer& offender, MatchPlayer& victim, bool ballWon, bool reckless);
  void updatePendingAdvantageAardvark(float dt);
  bool judgeOffsideAardvark(const MatchPlayer& passer, const PassOption& option, float defenderLine) const;
  int addedMinutesDueAardvark() const;
// clang-format on
