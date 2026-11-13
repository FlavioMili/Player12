// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Binturong out of play's MatchEngine members (defined in
// match_out_of_play_binturong.cpp). Included inside the MatchEngine class body,
// so no include guard and no includes of its own.

// clang-format off
  // --- Binturong out of play -------------------------------------------------
  void checkOutOfBoundsBinturong();
  bool ballInPlayAtBinturong(PitchPoint at) const;
  void scoreGoalBinturong(bool homeTeam);
  void kickOffAfterGoalBinturong(bool homeKickingOff);
  bool kickOffReadyBinturong();
  void startDeadBallBinturong(PitchPoint spot);
  void updateDeadBallBinturong(float dt);
  void moveDeadBallBinturong(float dt);
  void throwDeadBallBinturong(float releaseMetres);
  void placeDeadBallBinturong(MatchPlayer* taker);
  void chooseDeadBallFetcherBinturong(bool homeTeam);
  void dropKickDeadBallBinturong(MatchPlayer& keeper);
  bool keptOutBinturong(const MatchPlayer& player, const MatchPlayer& taker,
                        PitchPoint at, float slack) const;
  void keepRestartDistanceBinturong(MatchPlayer& player);
  void updateRestartReadinessBinturong(float dt);
  bool restartReadyBinturong() const;
  std::optional<float> frameInPathBinturong(float seconds) const;
  void bounceOffFrameBinturong();
  bool shotOffWoodworkStandsBinturong() const;
  void updateBallInNetBinturong(float dt);
  void setupThrowInBinturong(bool homeTeam);
  void setupGoalKickBinturong(bool homeTeam);
  void setupCornerBinturong(bool homeTeam, bool topCorner);
  void setupFreeKickBinturong(bool homeTeam, PitchPoint foulPos);
  void setupPenaltyBinturong(bool homeTeam);
  void placeTakerBinturong(MatchPlayer& taker, PitchPoint spot);
  void completeRestartBinturong();
// clang-format on
