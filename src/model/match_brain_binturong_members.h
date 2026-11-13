// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Binturong brain's MatchEngine members (defined in
// match_brain_binturong.cpp). Included inside the MatchEngine class body, so
// no include guard and no includes of its own.

// clang-format off
  // --- Binturong brain: decisions (see the dispatchers' comments) ------------
  void refreshTacticalTargetsBinturong(float dt, bool planHome);
  /** Off-ball players of a Binturong-brained side racing someone slower to
   * the same target go round him when that still wins the race (movement
   * targets plan 4a). Runs after the targets (and drill targets) are set. */
  void planOvertakeBinturong(std::size_t slot);
  /** Way-round plan: a loose-ball chaser goes round the first player in his
   * way (Notes and plans/match_engine_way_round_plan.md). */
  void planWayRoundBinturong(std::size_t slot);
  void assignMarksBinturong(bool home);
  PitchPoint goalkeeperTargetBinturong(MatchPlayer& keeper, const MatchPlayer* carrier);
  BallAction chooseBallActionBinturong(MatchPlayer& carrier);
  MatchPlayer* chooseTakeOnBinturong(MatchPlayer& carrier);
  void afterPassReleasedBinturong(const MatchPlayer& passer, const MatchPlayer& receiver);
  /** @param details Debugger detail: every team-mate looked at (or null). */
  std::optional<PassOption> choosePassTargetBinturong(
      MatchPlayer& passer,
      std::vector<PassCandidateDetail>* details = nullptr);
  PassOption evaluatePassOptionBinturong(MatchPlayer& passer, MatchPlayer& receiver,
                                PassCandidateDetail* detail = nullptr) const;
  ShotAim chooseShotAimBinturong(const MatchPlayer& shooter, bool header, bool penalty,
                        bool freeKick);
  float choosePenaltyGuessBinturong(const MatchPlayer& keeper);
  ClearanceAim chooseClearanceAimBinturong(const MatchPlayer& defender);
  PitchPoint chooseClearBehindTargetBinturong(const MatchPlayer& defender);
  HeaderAttempt chooseHeaderAttemptBinturong(const MatchPlayer& header,
                                    bool fromTeammate);
  HeaderPlay chooseHeaderPlayBinturong(const MatchPlayer& header, bool fromTeammate);
  Challenge chooseChallengeBinturong(const MatchPlayer& carrier, float dt);
  bool goesIntoContestBinturong(const MatchPlayer& defender,
                                const MatchPlayer& owner, PitchPoint contact);
  TackleIntent chooseTackleIntentBinturong(const MatchPlayer& tackler,
                                           const MatchPlayer& attacker,
                                           PitchPoint contact, bool freeBall);
  /** No outfield team-mate goal-side of him (the last man), and one
   * goal-side within COVER_METRES (cover). */
  struct DefensiveCover
  {
    bool lastMan = true;
    bool covered = false;
  };
  DefensiveCover defensiveCoverBinturong(const MatchPlayer& defender) const;
  bool touchNowBinturong(const MatchPlayer& player, const TouchMoment& moment) const;
  bool dribbleTouchNowBinturong(const MatchPlayer& player,
                            const TouchMoment& moment) const;
  /** Whether a clearly closer point of the ball's roll, safely in his
   * reach, is coming before anyone else could play it (touch timing). */
  bool waitsForCloserBinturong(const MatchPlayer& player,
                           const TouchMoment& moment) const;
  TouchIntent chooseFirstTouchBinturong(const MatchPlayer& player,
                                    const TouchMoment& moment) const;
  bool choosesToBlockBinturong(const MatchPlayer& player);
  bool choosesToPutBlockBehindBinturong(const MatchPlayer& player);
  bool choosesToClearCrossBehindBinturong(const MatchPlayer& player);
  MatchPlayer* chooseRestartTakerBinturong(MatchState restart, bool homeTeam,
                                  PitchPoint spot, bool topCorner = false);
  RestartPlay chooseRestartPlayBinturong(MatchPlayer& taker, MatchState restartState);
  RestartPlay chooseCornerPlayBinturong(MatchPlayer& taker);
  void applyTeamMentalityBinturong(StrategySliders& sliders, bool homeTeam) const;
  SetPieceAttackers arrangeSetPieceAttackBinturong(bool attackingHome,
                                          PitchPoint ballPosition);
  void arrangeSetPieceDefenceBinturong(bool defendingHome, PitchPoint ballPosition,
                              const SetPieceAttackers& attackers);
  MatchPlayer* dutyTakerBinturong(bool homeTeam, SetPieceDuty duty);
  MatchPlayer* bestSetPieceTakerBinturong(bool homeTeam, bool shooting);
  void updateRestartSupportBinturong(MatchPlayer& player, const MatchPlayer* taker);
  static const MovementStyle& movementStyleBinturong();

  // --- Binturong brain: helpers only it uses ---------------------------------
  /** Whether a wide forward in the final third is cutting inside toward
   * the box rather than going down the line to cross. */
  bool cutsInsideBinturong(const MatchPlayer& player) const;
  /** Heading it out of play the safest way: hard and high over the nearest
   * touchline from wide of the six-yard box; in line with it, up and over
   * the bar or glanced on towards the corner, whichever is easier. */
  HeaderPlay chooseHeaderOutBinturong(const MatchPlayer& header) const;
  /** Where the carrier heads with the ball: forward, away from the nearest
   * opponent, inside or down his lane. */
  PitchPoint carryTargetBinturong(const MatchPlayer& player) const;
  /** The pace (m/s) he carries the ball at (dribble meet plan 2c): from
   * the space along his way, the team's phase and its risk-taking, walk to
   * sprint; Dribbling caps how much of his pace he can use with it. */
  float carryPaceBinturong(const MatchPlayer& player) const;
  /** What this brain decides for itself (MatchEngine::BrainTraits). */
  const BrainTraits& brainTraitsBinturong() const;
  /** How urgently he carries it (0-1): what carryPaceBinturong maps onto
   * his bands. */
  float carryUrgencyBinturong(const MatchPlayer& player) const;
  /** How long until he wants his ball again after a touch (s), the push's
   * intent (dribble meet plan 3c): as soon as he can with an opponent near;
   * with a turn on his route or the end of his run (`toEnd` metres ahead)
   * coming within the push at `pace`, as far as there; else longer the more
   * urgently he carries it. */
  float chooseTouchSecondsBinturong(const MatchPlayer& player, float pace,
                                    float toEnd) const;
  /** How far a push from `from` (normalised pitch) along `direction`
   * (metric unit) goes (m): as far as he runs at `pace` in `seconds` from
   * `startGap` behind it, never past `toGoal` (less room to roll on), and
   * no further than he still gets to it first. */
  float pushMetresBinturong(const MatchPlayer& player, PitchPoint from,
                            Vector2F direction, float startGap, float pace,
                            float seconds, std::optional<float> toGoal) const;
  /** Signed x shift of the role in the defensive block (normalised). */
  float defensiveRoleShiftBinturong(const MatchPlayer& player) const;
  /** Second presser's spot when doubling up on the carrier, if ordered. */
  std::optional<PitchPoint> doubleUpPointBinturong(const MatchPlayer& helper,
                                        const MatchPlayer& carrier,
                                        const MatchPlayer* firstPresser) const;
  /** Multiplier of a defender's rate of engaging the carrier. */
  float engageBoostBinturong(const MatchPlayer& defender,
                    const MatchPlayer& carrier) const;
  /** Chance an opponent cuts out a pass along the straight lane: a man
   * already in it close to the passer is the likeliest to block it. */
  float passingLaneRiskBinturong(const MatchPlayer& passer,
                                 const MatchPlayer& receiver) const;
  /** Pass utility added by the passer's and the receiver's roles. */
  float passingRoleBiasBinturong(const MatchPlayer& passer, const MatchPlayer& receiver,
                        float progression, float completion) const;
  /** Formation spot while his side has the ball (role and shape shift). */
  PitchPoint possessionAnchorBinturong(const MatchPlayer& player) const;
  /** Seconds a role is quicker (+) or slower to close the ball down. */
  float pressEagernessBinturong(const MatchPlayer& candidate,
                       const MatchPlayer* carrier) const;
  /** Share of the presser's normal stand-off distance. */
  float pressStandOffShareBinturong(const MatchPlayer& presser,
                           const MatchPlayer& carrier) const;
  /** Shot appetite added by a shout (positive shoots more). */
  float shoutShotBiasBinturong(bool homeTeam) const;
  /** Pass utility lost to a tight marker standing next to the receiver. */
  float tightMarkPenaltyBinturong(const MatchPlayer& receiver) const;
  /** Moves a marker's target tight and goal-side of his man. */
  PitchPoint tightMarkPointBinturong(const MatchPlayer& marker, const MatchPlayer& target,
                          PitchPoint current) const;
  /** How fast that tight-marking point moves with his man (m/s). */
  Vector2F tightMarkVelocityBinturong(const MatchPlayer& target) const;
  /** The man this player marks when he is under a tight-marking order. */
  const MatchPlayer* tightMarkTargetBinturong(const MatchPlayer& marker) const;
  /** Presser's lateral shade onto the carrier's stronger side (normalised
   * y; 0 without a weaker-foot order). */
  float weakFootShadeBinturong(const MatchPlayer& carrier) const;
  /** Collision plan step 5: a fallen player gets up by urgency: jumps up
   * for a ball near and still to be won, gets up in a stoppage, gingerly if
   * play runs on long; else stays down (0). */
  float chooseGetUpBinturong(const MatchPlayer& player);
// clang-format on
