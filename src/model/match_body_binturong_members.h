// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Binturong body's MatchEngine members (defined in
// match_body_binturong.cpp). Included inside the MatchEngine class body, so
// no include guard and no includes of its own.

// clang-format off
  // --- Binturong body --------------------------------------------------------
  /**
   * Moves the players in `slots` toward their targets: the tactical target
   * (urgent when urgentMovement is set) in play, or the restart spot
   * (movementTarget) at a walk.
   */
  void integrateMovementsBinturong(const std::uint8_t* slots, std::size_t count,
                          float dt, bool walking);
  /** The controlled player's body following the stick (Play mode): physics
   * sub-steps, and his facing kept on `faceToward` while jockeying. */
  void moveControlledBinturong(MatchPlayer& player, Vector2F desired, float desiredSpeed,
                      float topSpeed, float fatigue, float dt,
                      const MatchPlayer* faceToward);
  /** Stamina drain and sprint-reserve drain or recovery for lanes
   * [begin, end), all players of one side. */
  void spendEnergyBinturong(FitnessLanes& lanes, std::size_t begin, std::size_t end,
                   float dt, bool homeTeam) const;
  /** Chance of a non-contact injury in the next check interval. */
  float injuryChanceBinturong(const MatchPlayer& player) const;
  /** Current top speed (m/s) after fatigue, sprint reserve and injury. */
  float currentTopSpeedBinturong(const MatchPlayer& player) const;
  /** A movement mode's speed band for him now: his walk-run transition,
   * critical speed and top speed as fuel has left them. */
  SpeedBand speedBandBinturong(const MatchPlayer& player, MovementMode mode) const;
  /** Collision plan: the fastest he can naturally move (m/s) in the
   * direction of `velocity` (metres), against his body's facing: the
   * direction caps (forward a sprint, backward a jog at 0.7...). */
  float directionTopSpeedBinturong(const MatchPlayer& player, Vector2F velocity) const;
  /** Collision plan: his hardest braking (m/s^2), fresh, by Agility. */
  float hardestBrakingBinturong(const MatchPlayer& player) const;
  /** Collision plan: a player in his collision pause neither drives, steers
   * nor brakes; he keeps his velocity, slowing gently. */
  void coastBinturong(MatchPlayer& player, float dt);
  /** Collision plan step 3: a stumbling player's recovery steps brake him
   * (a share of his hardest braking) and his body turns to his movement at
   * his normal rate; no drive, no steering. */
  void stumbleBinturong(MatchPlayer& player, float dt);
  /** Collision plan step 4: a fallen player slides or tumbles to a stop,
   * rolls about in pain through a big hit's least time down, then gets up
   * when his brain says (chooseGetUp), facing the way he slid. */
  void fallenBinturong(MatchPlayer& player, float dt);
  float executionErrorScaleBinturong(const MatchPlayer& player) const;
  /** Moves the dribbled ball with its carrier; false if a heavy touch lost it.
   */
  bool updateDribbleBinturong(MatchPlayer& carrier, float dt);
  /** The touch knobs as a contact profile (first and dribble touches). */
  ContactProfile touchProfileBinturong() const;
  /** The chest or thigh touch's contact profile: the touch knobs, less what
   * only a foot has (chest and thigh control plan steps 3 and 4). */
  ContactProfile bodyTouchProfileBinturong(bool thigh) const;
  /** How hard a contact is (contact plan §2): meeting the ball at `moment`
   * to send it along `aim` (metric, unit, seen from above) at
   * `intendedSpeed` over the ground and `intendedLift` upward (m/s).
   * `surprise`: how little he expected it (0-1, the brain's read; 0 for
   * his own ball or one he chose to strike). No random draws, so the brain
   * can read it too. `pressure`: how pressed he is (0-1, the brain's read
   * for a touch). `spin`: the spin he means (rad/s), against the most he
   * can put on, `spinCapacity`. */
  ContactAssessment assessContactBinturong(const MatchPlayer& player,
                                           const ContactProfile& profile,
                                           Vector2F aim, float intendedSpeed,
                                           float intendedLift,
                                           const TouchMoment& moment,
                                           float surprise,
                                           float pressure, BallSpin spin = {},
                                           float spinCapacity = 0.0f,
                                           float powerCapacity = 0.0f) const;
  /** His power for a kind of strike (m/s): shot power for shots and
   * headers, the throw's limit, kick power for the rest. */
  float strikePowerBinturong(const MatchPlayer& striker, StrikeKind kind) const;
  /** A strike's contact profile (provisional, contact plan §3). */
  ContactProfile strikeProfileBinturong(StrikeKind kind) const;
  /** Where a strike meets the ball: where it lies, its gap from his centre
   * against the 0.9 m strike reach, its height. */
  TouchMoment strikeMomentBinturong(const MatchPlayer& striker) const;
  /** A strike as a contact where the ball lies: `aim` metric unit, `speed`
   * over the ground and `lift` upward (m/s). No random draws. */
  ContactAssessment assessStrikeBinturong(const MatchPlayer& striker,
                                          const ContactProfile& profile,
                                          Vector2F aim, float speed,
                                          float lift = 0.0f) const;
  /** Every contact, touch or strike (unified kick plan): the request's
   * profile, assessed at `moment`, carried out with his skill for it, and
   * reported. */
  ContactResult contactBinturong(const MatchPlayer& player,
                                 const ContactRequest& request,
                                 const TouchMoment& moment);
  /** Tallies an open-play strike, and sends any contact's debugger
   * detail. */
  void reportContactBinturong(const MatchPlayer& player,
                              const ContactRequest& request,
                              const ContactResult& result);
  /** His skill for a kind of strike (0-1). */
  float strikeSkillBinturong(const MatchPlayer& striker, StrikeKind kind) const;
  /** A strike carried out (contact plan step 3): from the ball where it
   * lies towards `target` at `speed` over the ground and `lift` upward,
   * as a contact (contactBinturong; its angle error times `precision`).
   * The kick's target gives only its direction. */
  Kick strikeContactBinturong(const MatchPlayer& striker, StrikeKind kind,
                              PitchPoint target, float speed, float lift,
                              float precision = 1.0f, BallSpin spin = {},
                              float spinCapacity = 0.0f);
  /** Carries an assessed contact out with `skill` (0-1): each job's chance
   * of going wrong, how badly, and the ball off it. */
  ContactOutcome carryOutContactBinturong(const MatchPlayer& player,
                                          const ContactProfile& profile,
                                          const ContactAssessment& assessment,
                                          float skill);
  /** His first touch, trying to carry out `intent` where he meets the ball:
   * how hard it is decides the spread of its angle and weight errors.
   * `ownBall`: a dribble touch on his own claimed ball. */
  TouchResult firstTouchBinturong(const MatchPlayer& player,
                              const TouchIntent& intent,
                              const TouchMoment& moment, bool ownBall = false);
  /** His dribble touch, carrying out `intent` where he meets his own ball:
   * the first touch's two jobs, with his Dribbling. */
  TouchResult dribbleTouchBinturong(const MatchPlayer& player,
                                const TouchIntent& intent,
                                const TouchMoment& moment);
  /** His kick power: the fastest he can strike a pass (m/s), from his
   * Passing (MatchTuning::Passing::MIN_KICK_SPEED to MAX_KICK_SPEED). */
  static float kickPowerBinturong(const MatchPlayer& player);
  /** His shot power: the fastest he can strike a shot (m/s), the same
   * range from his Shooting. */
  static float shotPowerBinturong(const MatchPlayer& player);
  /** The fastest he can send the ball along `aim` (metric, unit) from his
   * `power` (m/s), the ball moving as it is now: less for pace across that
   * line, a little more or less for pace along it
   * (MatchTuning::Passing::INCOMING_*). A ball at rest: `power`. */
  float incomingPowerBudgetBinturong(float power, Vector2F aim) const;
  /** Kick power (m/s) for a skill of 0-1 (Passing for passes, Shooting
   * for shots). */
  static float kickPowerFromSkillBinturong(float skill);
  /** The most spin he can put on a pass (rad/s), from his Passing
   * (MatchTuning::Passing::MIN_SPIN_REVS to MAX_SPIN_REVS). */
  static float spinCapacityBinturong(const MatchPlayer& player);
  /** Where to aim (metres) for a ball curled `curl` rad round
   * (curlAngle) to get to `target`: that far the other way round the
   * ball. */
  PitchPoint curlAimBinturong(PitchPoint target, float curl) const;
  /** A lofted pass struck at `speed` (his kick power) for a target
   * further than his usual flight can carry at it: (along, up) m/s, the
   * flattest launch that comes down through `arrivalHeight` that far out,
   * or, when none does, the one that gets furthest. */
  std::pair<float, float> fullPowerLoftBinturong(float distanceMetres,
                                                 float speed,
                                                 float startHeight,
                                                 float arrivalHeight,
                                                 BallSpin spin) const;
  /** The passer's strike: execution error, weight, height and curve. */
  Kick kickPassBinturong(const MatchPlayer& passer, const MatchPlayer* receiver,
                PitchPoint target, float passDistance, bool lofted,
                bool delivery, std::optional<BallSpin> spin,
                std::optional<float> arrivalSpeed,
                std::optional<PassTechnique> technique);
  /** A ball launched `along` and `up` (m/s) from `startHeight` (m) with
   * `spin`, flown with the live physics: how far out it is when it comes
   * down through `arrivalHeight` (or first lands), and how high it is as
   * it passes `probeMetres` out (-1 if it is down before then). */
  struct LoftFlight
  {
    float reach = 0.0f;
    float probeHeight = -1.0f;
  };
  LoftFlight flyLoftBinturong(float along, float up, float startHeight,
                              float arrivalHeight, BallSpin spin,
                              float probeMetres) const;
  /** A chip of `distanceMetres` struck at no more than `maxSpeed` (m/s):
   * (along, up) m/s, the flattest launch between the chip's angles that
   * comes down through `arrivalHeight` there and clears the chip's height
   * on the way (the steepest, if none does); short at his limit. The
   * height to clear is `clearHeight` (m) `clearMetres` out; below 0, a
   * chip pass's (MatchTuning::Passing::CHIP_CLEAR_*). */
  std::pair<float, float> chipLaunchBinturong(float distanceMetres,
                                              float maxSpeed,
                                              float startHeight,
                                              float arrivalHeight,
                                              BallSpin spin,
                                              float clearMetres = -1.0f,
                                              float clearHeight = -1.0f) const;
  float recoverySecondsBinturong(const MatchPlayer& player, Recovery after);
  /** A shot's execution error (metres, one standard deviation). */
  float shotSpreadBinturong(const MatchPlayer& shooter, bool header, bool penalty,
                   bool freeKick, float metres) const;
  /** @param powerScale Play mode's power (1 for the AI). */
  ShotStrike strikeShotBodyBinturong(const MatchPlayer& shooter, float aimY, float aimZ,
                            float spread, bool header, float powerScale,
                            std::optional<ShotTechnique> technique);
  /** The most spin he can put on a shot (rad/s), from his Shooting as a
   * pass's from his Passing. */
  static float shotSpinCapacityBinturong(const MatchPlayer& player);
  float clearanceSpeedBinturong(const MatchPlayer& defender);
  float headerClearanceSpeedBinturong(const MatchPlayer& header);
  /** Where a knock-down meant for `aim` really goes. */
  PitchPoint knockDownPointBinturong(const MatchPlayer& header, PitchPoint aim);
  /** His header power (m/s): the pace he gives a ball with almost none of
   * its own, by Heading (heading technique plan step 3). */
  float headerPowerBinturong(const MatchPlayer& header) const;
  /** Whether the header being played was met with the top of his head. */
  bool metWithTopOfHeadBinturong() const;
  /** Brings `request` within what his head can do where he meets the ball
   * (`moment`), and sets `profile`'s lowest elevation (heading technique
   * plan step 5). */
  void headerLimitsBinturong(const MatchPlayer& header, const TouchMoment& moment,
                             ContactRequest& request, ContactProfile& profile) const;
  /** The least pace (m/s) the ball can leave his head with along `aim`
   * (metric unit) at `elevation` (rad), the ball coming in as it is. */
  float headerFloorBinturong(const MatchPlayer& header, Vector2F aim,
                             float elevation) const;
  /** The least pace (m/s) the ball can leave a body part with along `aim`
   * (metric unit) at `elevation` (rad), the ball coming in as it is, when
   * at least `keep` of the pace into it comes back off it. */
  float cushionFloorBinturong(float keep, Vector2F aim, float elevation) const;
  /** Keeps a chest or thigh touch's request within what that part can do:
   * so far off the way he faces, between its lowest and highest elevation
   * (Ball::CHEST_* or THIGH_*; its pace kept). */
  void bodyTouchLimitsBinturong(const MatchPlayer& player, ContactRequest& request,
                                bool thigh) const;
  /** A chest or thigh touch (chest and thigh control plan steps 3 and 4):
   * as softly as he can, back the way it came and up, then his to collect
   * once it is down. A thigh touch plants him. */
  void playBodyTouchBinturong(MatchPlayer& player, const TouchMoment& moment,
                              bool thigh);
  /** A cushioned header: as softly as he can, then his to collect. */
  void cushionHeaderBinturong(MatchPlayer& header);
  /** The fastest the ball can leave his head along `aim` (metric unit) at
   * `elevation` (rad, + up), the ball coming in as it is; `byAngle`: with
   * his body's share of his power that far off the way he faces. */
  float headerCeilingBinturong(const MatchPlayer& header, Vector2F aim,
                               float elevation, bool byAngle = true) const;
  std::pair<float, float> headerPaceBinturong(const MatchPlayer& header,
                                              HeaderPace purpose, PitchPoint target,
                                              float speed, float lift) const;
  /** The launch pace at `elevation` (rad) from `startHeight` (m) that first
   * lands `metres` away. */
  float landingPaceBinturong(float metres, float elevation, float startHeight) const;
  /** His Jumping reach (m): the top of his head at the top of his jump. */
  float jumpingReachBinturong(const MatchPlayer& player) const;
  /** The top of his head now (m): his height, plus his feet's in a jump. */
  float headTopBinturong(const MatchPlayer& player) const;
  /** How far his running or standing jump lifts him (m). */
  float jumpRiseBinturong(const MatchPlayer& player, bool running) const;
  /** A step of his jump: in the air, rising, falling and landing; on the
   * ground, taking off for a header the ball's flight `path` brings him. */
  void stepJumpBinturong(MatchPlayer& player, float dt,
                         const std::vector<JumpPathPoint>& path);
  /** Whether his head can meet a ball `gap` metres from his centre across
   * the ground, the way `toBall` (metric unit), `heightMetres` up, and with
   * which part (heading technique plan step 4); none: out of his head's
   * reach. */
  std::optional<HeadPart> headMeetBinturong(const MatchPlayer& player,
                                            Vector2F toBall, float gap,
                                            float heightMetres,
                                            float feetHeight) const;
  /** His feet's height (m) `secondsBefore` the end of this step. */
  float feetAtBinturong(const MatchPlayer& player, float secondsBefore) const;
  /** Brings a received ball under control: trap time and when he can act
   * (a keeper with it in his hands: how long he holds it). */
  void settleOnBallBinturong(MatchPlayer& player, float touchSkill, bool hands);
  float keeperHoldSecondsBinturong(const MatchPlayer& keeper);
  /** Whether an outfielder in a shot's path gets his body in the way
   * (gap: from his centre to the ball's path; reach: the most he had). */
  bool blocksShotBinturong(const MatchPlayer& player, float gap, float reach);
  /** Where on the ball's path from `from` to `to` (metres; share 0-1) a
   * shot first meets his torso or a leg, and what it meets: his stride
   * (shot block plan §3c, rolled once per path) until he has reacted, then
   * a leg put across its line if he can reach it (§3d). */
  std::optional<ShotBodyHit> shotMeetsBodyBinturong(const MatchPlayer& player,
                                                    std::size_t slot, Point3F from, Point3F to,
                                                    bool canPutLegOut);
  /** Any other moving ball meeting his body, not played (pass block plan):
   * off the part it met as a shot comes off it (step 3), a loose ball after
   * (his ball no longer, if it was anyone's), him kept off it a moment. */
  void ballMeetsBodyBinturong(MatchPlayer& player, std::size_t slot, const ShotBodyHit& hit);
  /** The check (shot block plan §3f): a shot that met nothing on him, but
   * whose path against his own move over the sub-step went well inside his
   * torso, is counted. */
  /** How far through the step (share) this stretch of the ball's flight
   * begins and ends: where each player is along his move over it. */
  [[nodiscard]] std::pair<float, float> stretchStepSharesBinturong() const;
  void checkShotThroughTorsoBinturong(const MatchPlayer& player, std::size_t slot, Point3F from,
                                      Point3F to);
  /** A shot off his body where it met him (shot block plan step 3): off
   * the surface against its own motion, spring-back and grip by the part,
   * spin from the grip; a leg put out's foot turned a little to send it
   * away from his goal. */
  void bounceShotOffBodyBinturong(const MatchPlayer& player, const ShotBodyHit& hit);
  /** After a shot is turned (off a body or the woodwork; shot block plan
   * step 4): whether it is still heading into the goal it was struck at;
   * if so it stays a shot, on target, and the keeper reads it afresh. */
  bool shotStaysOnGoalBinturong();
  /** Once he has reacted to a shot (shot block plan §3d): whether he puts
   * a leg across its line, seen on the path from `start` to `end` (from his
   * centre on the ground, metres), and where; fills `legs`. */
  void planShotBlockBinturong(const MatchPlayer& player, ShotLegs& legs, Point3F start,
                              Point3F end);
  /** Once a keeper has reacted to a shot (goalkeeper plan step 2): where
   * his hands go, seen on the path from `start` to `end` (from his centre
   * on the ground, metres), his centre on the pitch `centre` (metres);
   * fills `hands`. */
  void planKeeperHandsBinturong(const MatchPlayer& player, ShotHands& hands, Point3F start,
                                Point3F end, Vector2F centre);
  /** Once a keeper has reacted to a shot on target (goalkeeper plan step
   * 3): whether he dives (its line beyond his standing reach) and how;
   * fills `dive`. A penalty guess gives `forced` (from his centre, m); a
   * smother (step 8) too, with when the ball is there (he aims with his
   * miss). */
  void planKeeperDiveBinturong(MatchPlayer& keeper, KeeperDive& dive, double from,
                               std::optional<Point3F> forced = std::nullopt,
                               std::optional<double> smotherBallThere = std::nullopt);
  /** A loose ball in his area an attacker would reach before him on foot
   * but his dive would not (goalkeeper plan step 8): he dives for it, as
   * late as he still gets there first. Whether he went. */
  bool considerSmotherBinturong(MatchPlayer& keeper);
  /** A ball that is not a shot rolling into his own goal (a back-pass gone
   * wrong, a miscontrol) that nobody else, either side, gets to first: where
   * he gets his feet to it on its way (metres), else where it crosses
   * his line. Nothing if it is not going in, or someone else meets it. */
  std::optional<PitchPoint> ownGoalThreatBinturong(const MatchPlayer& keeper);
  /** How far from where he stands his midriff can land in a smother (m):
   * his feet's dive reach plus his midriff's height. */
  float smotherReachBinturong(const MatchPlayer& keeper) const;
  /** A smother (step 8): down side-on across the ball's line (`ballWay`,
   * metric unit), his midriff to `at` (from his centre, m) when the ball is
   * there (`ballThere`, simulated s), with his Reflexes miss; his run
   * carries him on. Fills `dive`. */
  void planKeeperSmotherBinturong(MatchPlayer& keeper, KeeperDive& dive, double from, Point3F at,
                                  Vector2F ballWay, double ballThere);
  /** Whether his hands can gather a loose ball now: at walking pace or
   * slower, bent to it long enough (goalkeeper plan step 8). Anyone but a
   * Binturong keeper: yes. */
  bool keeperCanGatherBinturong(const MatchPlayer& player) const;
  /** His dive's shape moved on by his run (step 8), `since` push-off (s). */
  void shiftDivePoseByRunBinturong(KeeperDivePose& pose, const KeeperDive& dive, float since) const;
  /** A diving keeper's shape at `when` (simulated s). */
  KeeperDivePose keeperDivePoseBinturong(const MatchPlayer& keeper, const KeeperDive& dive,
                                         double when) const;
  /** Where on the ball's path from `from` to `to` (pitch metres) it first
   * meets a diving keeper, and what it meets. */
  std::optional<ShotBodyHit> diveMeetsBallBinturong(const MatchPlayer& keeper, Point3F from,
                                                    Point3F to);
  /** Where a keeper's hand `index` can be: `wanted` (from his centre)
   * brought inside its reach from its shoulder (goalkeeper plan step 2). */
  Point3F keeperHandReachBinturong(const MatchPlayer& player, std::size_t index,
                                   Point3F wanted) const;
  /** How far a keeper is stretched with his hand at `hand` (goalkeeper plan
   * step 4): from his chest where he stood (`stood`, pitch metres) to the
   * hand, against his longest reach; 0 to 1. */
  float keeperStretchBinturong(const MatchPlayer& keeper, Vector2F stood, Point3F hand) const;
  /** A shot meets a keeper's hand (goalkeeper plan step 4): both hands on
   * it and within his Handling limit for its pace and his stretch, a catch
   * (true: he holds it, unless it is fumbled); otherwise `hit` is marked to
   * parry, the face turned away from goal. False: it bounces off. */
  bool keeperHandsHoldShotBinturong(MatchPlayer& keeper, ShotBodyHit& hit, bool shot = true);
  /** From his shoulder to the middle of his hand at full stretch (m). */
  float keeperArmBinturong(const MatchPlayer& keeper) const;
  /** A Binturong keeper who may claim this ball with his hands now: in his
   * area, on his feet or in a jump (not a dive), not his own side's pass. */
  bool keeperMayClaimBinturong(const MatchPlayer& keeper) const;
  /** Where the ball's stretch from `start` to `end` (metres, z up) first
   * comes within his arms, his centre at `at` (metres) and his feet
   * `feet` up (goalkeeper plan step 7). */
  std::optional<KeeperReach> keeperReachBinturong(const MatchPlayer& keeper, Vector2F at,
                                                  float feet, Point3F start, Point3F end) const;
  /** His hands meet a high ball where `reach` says (the ball there now):
   * punched if an opponent is close, held if within his Handling, else it
   * springs off (goalkeeper plan step 7). */
  void keeperClaimBinturong(MatchPlayer& keeper, const KeeperReach& reach);
  /** A ball off a keeper's hand that was no shot: loose, him kept off it a
   * moment, off his hand as `hit` says. */
  void bounceOffKeeperHandBinturong(MatchPlayer& keeper, const ShotBodyHit& hit);
  /** A Binturong keeper in his area has just been given the ball with his
   * hands (goalkeeper plan step 5): it was at `wasAt` (pitch), `wasMetres`
   * up; from there it is brought to his chest and held. Others: nothing. */
  void takeInHandsBinturong(const MatchPlayer& keeper, PitchPoint wasAt, float wasMetres);
  /** Where a ball in his hands is now (pitch metres, z up): brought from
   * where he took it to his chest; in a dive, between his hands. */
  Point3F heldBallBinturong(const MatchPlayer& keeper) const;
  /** Where he lets go of the ball in his hands for `how` (pitch, and
   * metres up). */
  std::pair<PitchPoint, float> heldReleaseBinturong(const MatchPlayer& keeper,
                                                  HeldRelease how) const;
  /** Whether he reads and reaches a ball not meant for him. */
  bool reachesBallBinturong(const MatchPlayer& player, float ballSpeed, float stretch);
  /** His first-touch skill (hands: a keeper allowed to handle it). */
  float touchSkillBinturong(const MatchPlayer& player, bool hands) const;
  /** Whether his first touch brings the ball under control. */
  bool controlsBallBinturong(const MatchPlayer& player, float skill, float ballSpeed,
                    float heightMetres);
  /** The ball's velocity after a heavy first touch. */
  Vector2F heavyTouchVelocityBinturong(const MatchPlayer& player, Vector2F velocity);
  void diveGoalkeeperBinturong(MatchPlayer& keeper, float dt);
  /** The keeper reads an open-play shot: where to dive and how long he
   * needs to react (defenders in the line of the shot screen it). */
  void readShotBinturong(const MatchPlayer& keeper, GoalkeeperControl& control);
  /** Whether the keeper stops the shot crossing his plane in the last ball
   * sub-step (reach and save roll only, no consequences). */
  SaveAttempt attemptSaveBinturong(const MatchPlayer& keeper);
  /** Whether a save is held rather than parried. */
  bool holdsShotBinturong(const MatchPlayer& keeper, float stretch, float speedExcess);
  Spill parrySpillBinturong(const MatchPlayer& keeper, float speed);
  /** Acceleration, braking, lateral limit, position and facing update. */
  void stepKinematicsBinturong(MatchPlayer& player, Vector2F desired, float desiredSpeed,
                      float topSpeed, float fatigue, float dt);
  // Contest halves (see takeOnMake and the others in match_engine.h).
  float takeOnMakeBinturong(const MatchPlayer& carrier, ScoreBreakdown* terms) const;
  float takeOnResistBinturong(const MatchPlayer& defender, ScoreBreakdown* terms) const;
  float tackleMakeBinturong(const MatchPlayer& tackler, ScoreBreakdown* terms) const;
  float tackleResistBinturong(const MatchPlayer& carrier, bool shielding,
                              ScoreBreakdown* terms) const;
  float tackleTechniqueBinturong(const MatchPlayer& tackler, ScoreBreakdown* terms) const;
  float aerialStrengthBinturong(const MatchPlayer& player, float ballHeightMetres) const;
  float claimChanceBinturong(const MatchPlayer& keeper, int crowding) const;
  void recoverFromTakeOnBinturong(MatchPlayer& defender);
  // Striking (see strikeAs and the others in match_engine.h).
  bool playsFirstTouchBinturong(const MatchPlayer& player, bool hands, float heightMetres) const;
  float feetReachBinturong(const MatchPlayer& player) const;
  BodyLines bodyLinesBinturong(const MatchPlayer& player) const;
  bool ballBehindReachBinturong(const MatchPlayer& player, float gapX, float gapY, float gap, float turnSeconds) const;
  /** The facing (a true angle) the wait read expects him to have
   * after turnSeconds: toward his facing asked of him, else his current run,
   * at his turn rate (as ballBehindReachBinturong reads it). */
  float predictedFacingBinturong(const MatchPlayer& player, float turnSeconds) const;
  PitchPoint strikePointBinturong(const MatchPlayer& striker) const;
  Kick strikeAsBinturong(const MatchPlayer& striker, StrikeKind kind, PitchPoint target, float speed, float lift);
  float clearanceDistanceBinturong(const MatchPlayer& defender, PitchPoint origin, const ClearanceAim& aim) const;
  /** A direct free kick: blocked by the wall, or a shot over it. */
  void takeDirectFreeKickBinturong(MatchPlayer& taker);
  BallRelease releaseFromBinturong(const MatchPlayer& kicker, ReleaseKind kind) const;
  void meetWithHeadBinturong(const MatchPlayer& header);
  bool followsRoutesBinturong(const MatchPlayer& player) const;
  /** He has the ball at his feet (in touch, his): he moves by what his
   * Dribbling unlocks of his Agility and Acceleration. */
  bool carryingBinturong(const MatchPlayer& player) const;
  /** The share of his Agility and Acceleration he keeps whatever his
   * Dribbling (movementRating): with the ball, his run band's top over his
   * top speed; without it, 1. */
  float freeShareBinturong(const MatchPlayer& player, bool withBall) const;
  /** Where a dribble touch at `from` (metres) sends his ball
   * (dribble meet plan): along his route's line from there, else toward
   * ball.dribbleGoal. Metric unit; none when he is at his goal. */
  std::optional<Vector2F> dribbleOutFromBinturong(const MatchPlayer& player,
                                                  PitchPoint from) const;
  /** On a route, the way from `from` (metres) to where his line
   * will be `metres` on, traced along it (into a bend, a push angled into
   * it lands back on his line), clear of obstacles (`metres` shortened when
   * it is turned round one). Metric unit; none off a route. */
  std::optional<Vector2F> dribbleOutAlongBinturong(const MatchPlayer& player,
                                                   PitchPoint from,
                                                   float& metres) const;
  /** A push from `start` (metres) along `direction` (metric unit) for
   * `metres`, turned round any obstacle it would touch or pass the other
   * side of from him, to just clear it on his side (the side the leg he is
   * on goes round it by, else the side the ball is already passing it), and
   * then no longer than takes it past it.
   * `player` is whose push it is (whose opponents, when they are
   * obstacles). Metric unit. */
  Vector2F clearOfObstaclesBinturong(const MatchPlayer& player, Vector2F start,
                                     Vector2F direction, float& metres) const;
  /** Plans the dribbler's next meet with his collected ball (ball.meet*):
   * the earliest point of its roll he can reach at the pace he wants, and
   * where his touch there sends it. Sets his target to the meet point. */
  void planDribbleMeetBinturong(MatchPlayer& owner);
  /** A pass or shot held for his meet bends the meet's way out toward its
   * line, by the time he has to spare before an opponent could get there
   * (dribble meet plan 2b). */
  void shapeMeetForStrikeBinturong(const MatchPlayer& owner);
  /** The soonest (s) an opponent of `owner` could get to `point`, with the
   * moment it takes him to read it; very large with none. */
  float opponentArrivalSecondsBinturong(const MatchPlayer& owner,
                                        PitchPoint point) const;
  /** Turning to pass: the facing (an angle like facingAngle) a held pass
   * asks of its passer, along its line from the ball, turned no further
   * than keeps the ball clear of his dead zone; none without one. */
  std::optional<float> turnToPassFacingBinturong(const MatchPlayer& player) const;
  /** Who turns to head a ball still to come, and to what facing
   * (headerTurn; heading technique plan step 7). */
  void planHeaderTurnBinturong();
  /** The facing his body turns to for a strike he means: a pass or shot he
   * holds, else a header he means. */
  std::optional<float> strikeFacingBinturong(const MatchPlayer& player) const;
  /** Where his held strike is aimed (metres): a pass's target, a
   * shot's spot on the goal line; none for a clearance or no strike. */
  std::optional<PitchPoint> heldStrikeTargetBinturong(const MatchPlayer& player) const;
  /** Whether a pass held while he turns is to be struck now: turned to it,
   * or out of time. */
  bool turnedToPassBinturong(const MatchPlayer& player) const;
  /** His average speed (m/s) over `seconds` from `startSpeed`, speeding
   * up toward `target` as hard as his legs allow (the body's a(v)); and,
   * into `speedingUpShare`, the share of the time before he gets there. */
  float dribbleReachablePaceBinturong(const MatchPlayer& player,
                                      float startSpeed, float target,
                                      float seconds,
                                      float* speedingUpShare = nullptr) const;
  /** How close (m) he means to have his own ball when he plays it: the
   * comfort gap with perfect Dribbling, out to the strike reach with
   * none. */
  float dribbleMeetGapBinturong(const MatchPlayer& player) const;
  /** The quickest he can play his own ball again after a touch (s): longer
   * for a poorer dribbler, in whole 0.1 s steps. */
  float dribbleTouchIntervalBinturong(const MatchPlayer& player) const;
  /** Before he means to play his ball again (dribble meet plan 3d):
   * whether it needs a touch now anyway: an opponent close enough to take
   * it, stopped while he wants to go (at `pace`), running well off the way
   * he pushed it, or caught (level with him). */
  bool dribbleTouchNeededEarlyBinturong(const MatchPlayer& player,
                                        const TouchMoment& moment,
                                        float pace) const;
  /** His route's next turn seen from a dribble touch at `from`
   * (metres). */
  DribbleTurnAhead dribbleTurnAheadBinturong(const MatchPlayer& player,
                                             PitchPoint from) const;
  /** Where a tackle's "win it" nudge sends a free ball met at `contact`
   * (tackling plan step 3): out of the attacker's line to the defender's
   * side, or, goal-side and face on, forward and to the side past him. */
  PitchPoint tackleNudgeSpotBinturong(const MatchPlayer& tackler,
                                    const MatchPlayer& attacker,
                                    PitchPoint contact) const;
// clang-format on
