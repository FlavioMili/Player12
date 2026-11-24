// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Aardvark body's MatchEngine members (defined in
// match_body_aardvark.cpp). Included inside the MatchEngine class body, so
// no include guard and no includes of its own.

// clang-format off
  // --- Aardvark body ---------------------------------------------------------
  /**
   * Moves the players in `slots` toward their targets: the tactical target
   * (urgent when urgentMovement is set) in play, or the restart spot
   * (movementTarget) at a walk.
   */
  void integrateMovementsAardvark(const std::uint8_t* slots, std::size_t count,
                          float dt, bool walking);
  /** The controlled player's body following the stick (Play mode): physics
   * sub-steps, and his facing kept on `faceToward` while jockeying. */
  void moveControlledAardvark(MatchPlayer& player, Vector2F desired, float desiredSpeed,
                      float topSpeed, float fatigue, float dt,
                      const MatchPlayer* faceToward);
  /** Stamina drain and sprint-reserve drain or recovery for lanes
   * [begin, end), all players of one side. */
  void spendEnergyAardvark(FitnessLanes& lanes, std::size_t begin, std::size_t end,
                   float dt, bool homeTeam) const;
  /** Chance of a non-contact injury in the next check interval. */
  float injuryChanceAardvark(const MatchPlayer& player) const;
  /** Current top speed (m/s) after fatigue, sprint reserve and injury. */
  float currentTopSpeedAardvark(const MatchPlayer& player) const;
  /** A movement mode's speed band for him now (the Aardvark body has no
   * critical-speed model of its own: the Endurance stat, unfatigued). */
  SpeedBand speedBandAardvark(const MatchPlayer& player, MovementMode mode) const;
  /** What those bands are made from (speedBandAardvark). */
  SpeedBandInputs speedBandInputsAardvark(const MatchPlayer& player) const;
  float executionErrorScaleAardvark(const MatchPlayer& player) const;
  /** Moves the dribbled ball with its carrier; false if a heavy touch lost it.
   */
  bool updateDribbleAardvark(MatchPlayer& carrier, float dt);
  /** The passer's strike: execution error, weight, height and curve. */
  Kick kickPassAardvark(const MatchPlayer& passer, const MatchPlayer* receiver,
                PitchPoint target, float passDistance, bool lofted,
                bool delivery, std::optional<BallSpin> spin,
                std::optional<float> arrivalSpeed);
  float recoverySecondsAardvark(const MatchPlayer& player, Recovery after);
  /** A shot's execution error (metres, one standard deviation). */
  float shotSpreadAardvark(const MatchPlayer& shooter, bool header, bool penalty,
                   bool freeKick, float metres) const;
  /** @param powerScale Play mode's power (1 for the AI). */
  ShotStrike strikeShotBodyAardvark(const MatchPlayer& shooter, float aimY, float aimZ,
                            float spread, bool header, float powerScale);
  float clearanceSpeedAardvark(const MatchPlayer& defender);
  float headerClearanceSpeedAardvark(const MatchPlayer& header);
  /** Where a knock-down meant for `aim` really goes. */
  PitchPoint knockDownPointAardvark(const MatchPlayer& header, PitchPoint aim);
  /** A header's pace: the brain's, as set. */
  std::pair<float, float> headerPaceAardvark(const MatchPlayer& header,
                                             HeaderPace purpose, PitchPoint target,
                                             float speed, float lift) const;
  /** Brings a received ball under control: trap time and when he can act
   * (a keeper with it in his hands: how long he holds it). */
  void settleOnBallAardvark(MatchPlayer& player, float touchSkill, bool hands);
  float keeperHoldSecondsAardvark(const MatchPlayer& keeper);
  /** Whether an outfielder in a shot's path gets his body in the way
   * (gap: from his centre to the ball's path; reach: the most he had). */
  bool blocksShotAardvark(const MatchPlayer& player, float gap, float reach);
  /** Whether he reads and reaches a ball not meant for him. */
  bool reachesBallAardvark(const MatchPlayer& player, float ballSpeed, float stretch);
  /** His first-touch skill (hands: a keeper allowed to handle it). */
  float touchSkillAardvark(const MatchPlayer& player, bool hands) const;
  /** Whether his first touch brings the ball under control. */
  bool controlsBallAardvark(const MatchPlayer& player, float skill, float ballSpeed,
                    float heightMetres);
  /** The ball's velocity after a heavy first touch. */
  Vector2F heavyTouchVelocityAardvark(const MatchPlayer& player, Vector2F velocity);
  /** A defender's touch in front of his own goal that now and then goes
   * toward it instead (true when it did). */
  bool tryOwnGoalTouchAardvark(MatchPlayer& defender);
  void diveGoalkeeperAardvark(MatchPlayer& keeper, float dt);
  /** The keeper reads an open-play shot: where to dive and how long he
   * needs to react (defenders in the line of the shot screen it). */
  void readShotAardvark(const MatchPlayer& keeper, GoalkeeperControl& control);
  /** Whether the keeper stops the shot crossing his plane in the last ball
   * sub-step (reach and save roll only, no consequences). */
  SaveAttempt attemptSaveAardvark(const MatchPlayer& keeper);
  /** Whether a save is held rather than parried. */
  bool holdsShotAardvark(const MatchPlayer& keeper, float stretch, float speedExcess);
  Spill parrySpillAardvark(const MatchPlayer& keeper, float speed);
  /** Acceleration, braking, lateral limit, position and facing update. */
  void stepKinematicsAardvark(MatchPlayer& player, Vector2F desired, float desiredSpeed,
                      float topSpeed, float fatigue, float dt);
  // Contest halves (see takeOnMake and the others in match_engine.h).
  float takeOnMakeAardvark(const MatchPlayer& carrier, ScoreBreakdown* terms) const;
  float takeOnResistAardvark(const MatchPlayer& defender, ScoreBreakdown* terms) const;
  float tackleMakeAardvark(const MatchPlayer& tackler, ScoreBreakdown* terms) const;
  float tackleResistAardvark(const MatchPlayer& carrier, bool shielding,
                             ScoreBreakdown* terms) const;
  float tackleTechniqueAardvark(const MatchPlayer& tackler, ScoreBreakdown* terms) const;
  float aerialStrengthAardvark(const MatchPlayer& player, float ballHeightMetres) const;
  float claimChanceAardvark(const MatchPlayer& keeper, int crowding) const;
  void recoverFromTakeOnAardvark(MatchPlayer& defender);
  // Striking (see strikeAs and the others in match_engine.h).
  bool playsFirstTouchAardvark(const MatchPlayer& player, bool hands, float heightMetres) const;
  float feetReachAardvark(const MatchPlayer& player) const;
  BodyLines bodyLinesAardvark(const MatchPlayer& player) const;
  bool ballBehindReachAardvark(const MatchPlayer& player, float gapX, float gapY, float gap, float turnSeconds) const;
  PitchPoint strikePointAardvark(const MatchPlayer& striker) const;
  Kick strikeAsAardvark(const MatchPlayer& striker, StrikeKind kind, PitchPoint target, float speed, float lift);
  float clearanceDistanceAardvark(const MatchPlayer& defender, PitchPoint origin, const ClearanceAim& aim) const;
  /** A direct free kick: blocked by the wall, or a shot over it. */
  void takeDirectFreeKickAardvark(MatchPlayer& taker);
  BallRelease releaseFromAardvark(const MatchPlayer& kicker, ReleaseKind kind) const;
  void meetWithHeadAardvark(const MatchPlayer& header);
  bool followsRoutesAardvark(const MatchPlayer& player) const;
// clang-format on
