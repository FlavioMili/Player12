// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Aardvark brain's MatchEngine members (defined in
// match_brain_aardvark.cpp). Included inside the MatchEngine class body, so
// no include guard and no includes of its own.

// clang-format off
  // --- Aardvark brain: decisions (see the dispatchers' comments) -------------
  void refreshTacticalTargetsAardvark(float dt, bool planHome);
  void assignMarksAardvark(bool home);
  PitchPoint goalkeeperTargetAardvark(MatchPlayer& keeper, const MatchPlayer* carrier);
  BallAction chooseBallActionAardvark(MatchPlayer& carrier);
  MatchPlayer* chooseTakeOnAardvark(MatchPlayer& carrier);
  void planOvertakeAardvark(std::size_t slot);
  void afterPassReleasedAardvark(const MatchPlayer& passer, const MatchPlayer& receiver);
  /** @param details Debugger detail: every team-mate looked at (or null). */
  std::optional<PassOption> choosePassTargetAardvark(
      MatchPlayer& passer,
      std::vector<PassCandidateDetail>* details = nullptr);
  PassOption evaluatePassOptionAardvark(MatchPlayer& passer, MatchPlayer& receiver,
                                PassCandidateDetail* detail = nullptr) const;
  ShotAim chooseShotAimAardvark(const MatchPlayer& shooter, bool header, bool penalty,
                        bool freeKick);
  float choosePenaltyGuessAardvark(const MatchPlayer& keeper);
  ClearanceAim chooseClearanceAimAardvark(const MatchPlayer& defender);
  PitchPoint chooseClearBehindTargetAardvark(const MatchPlayer& defender);
  HeaderAttempt chooseHeaderAttemptAardvark(const MatchPlayer& header,
                                    bool fromTeammate);
  HeaderPlay chooseHeaderPlayAardvark(const MatchPlayer& header, bool fromTeammate);
  Challenge chooseChallengeAardvark(const MatchPlayer& carrier, float dt);
  bool choosesToBlockAardvark(const MatchPlayer& player);
  bool choosesToPutBlockBehindAardvark(const MatchPlayer& player);
  bool choosesToClearCrossBehindAardvark(const MatchPlayer& player);
  MatchPlayer* chooseRestartTakerAardvark(MatchState restart, bool homeTeam,
                                  PitchPoint spot, bool topCorner = false);
  RestartPlay chooseRestartPlayAardvark(MatchPlayer& taker, MatchState restartState);
  RestartPlay chooseCornerPlayAardvark(MatchPlayer& taker);
  void applyTeamMentalityAardvark(StrategySliders& sliders, bool homeTeam) const;
  SetPieceAttackers arrangeSetPieceAttackAardvark(bool attackingHome,
                                          PitchPoint ballPosition);
  void arrangeSetPieceDefenceAardvark(bool defendingHome, PitchPoint ballPosition,
                              const SetPieceAttackers& attackers);
  MatchPlayer* dutyTakerAardvark(bool homeTeam, SetPieceDuty duty);
  MatchPlayer* bestSetPieceTakerAardvark(bool homeTeam, bool shooting);
  void updateRestartSupportAardvark(MatchPlayer& player, const MatchPlayer* taker);
  static const MovementStyle& movementStyleAardvark();

  // --- Aardvark brain: helpers only it uses ----------------------------------
  /** Whether a wide forward in the final third is cutting inside toward
   * the box rather than going down the line to cross. */
  bool cutsInsideAardvark(const MatchPlayer& player) const;
  /** Signed x shift of the role in the defensive block (normalised). */
  float defensiveRoleShiftAardvark(const MatchPlayer& player) const;
  /** Second presser's spot when doubling up on the carrier, if ordered. */
  std::optional<PitchPoint> doubleUpPointAardvark(const MatchPlayer& helper,
                                        const MatchPlayer& carrier,
                                        const MatchPlayer* firstPresser) const;
  /** Multiplier of a defender's rate of engaging the carrier. */
  float engageBoostAardvark(const MatchPlayer& defender,
                    const MatchPlayer& carrier) const;
  /** Pass utility added by the passer's and the receiver's roles. */
  float passingRoleBiasAardvark(const MatchPlayer& passer, const MatchPlayer& receiver,
                        float progression, float completion) const;
  /** Formation spot while his side has the ball (role and shape shift). */
  PitchPoint possessionAnchorAardvark(const MatchPlayer& player) const;
  /** Seconds a role is quicker (+) or slower to close the ball down. */
  float pressEagernessAardvark(const MatchPlayer& candidate,
                       const MatchPlayer* carrier) const;
  /** Share of the presser's normal stand-off distance. */
  float pressStandOffShareAardvark(const MatchPlayer& presser,
                           const MatchPlayer& carrier) const;
  /** Shot appetite added by a shout (positive shoots more). */
  float shoutShotBiasAardvark(bool homeTeam) const;
  /** Pass utility lost to a tight marker standing next to the receiver. */
  float tightMarkPenaltyAardvark(const MatchPlayer& receiver) const;
  /** Moves a marker's target tight and goal-side of his man. */
  PitchPoint tightMarkPointAardvark(const MatchPlayer& marker, const MatchPlayer& target,
                          PitchPoint current) const;
  /** The man this player marks when he is under a tight-marking order. */
  const MatchPlayer* tightMarkTargetAardvark(const MatchPlayer& marker) const;
  /** Presser's lateral shade onto the carrier's stronger side (normalised
   * y; 0 without a weaker-foot order). */
  float weakFootShadeAardvark(const MatchPlayer& carrier) const;
  /** Collision plan step 4: no get-up cues; up at once, by Agility. */
  float chooseGetUpAardvark(const MatchPlayer& player);
// clang-format on
