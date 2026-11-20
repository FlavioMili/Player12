// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Capybara brain's MatchEngine members (defined in
// match_brain_capybara.cpp). Included inside the MatchEngine class body, so
// no include guard and no includes of its own.

// clang-format off
  // --- Capybara brain: decisions (see the dispatchers' comments) -------------
  void refreshTacticalTargetsCapybara(float dt, bool planHome);
  /** Off-ball players of a Capybara-brained side racing someone slower to
   * the same target go round him when that still wins the race (movement
   * targets plan 4a). Runs after the targets (and drill targets) are set. */
  void planOvertakeCapybara(std::size_t slot);
  /** Way-round plan: a loose-ball chaser goes round the first player in his
   * way (Notes and plans/match_engine_way_round_plan.md). */
  void planWayRoundCapybara(std::size_t slot);
  void assignMarksCapybara(bool home);
  PitchPoint goalkeeperTargetCapybara(MatchPlayer& keeper, const MatchPlayer* carrier);
  BallAction chooseBallActionCapybara(MatchPlayer& carrier);
 public:
  // Public for the sandbox xG map, which draws the shot score core.
  /** Weighting for the carrier decision (DecisionFrame; brain redesign plan
   * section 15, step A): how much each of its measures counts for him. The
   * defaults are MatchTuning's; the layers (team instructions, his role, a
   * shout, the game state) move them. Weights that depend on the receiver
   * (his role, an order on him) are set where the pass is scored. */
  struct CarrierWeightsCapybara
  {
    // Pass, per candidate.
    float receiverSpace = 0.0F;
    float progress = 0.0F;
    float progressInstruction = 0.0F;
    float lengthOffIdeal = 0.0F;
    float safeOutlet = 0.0F;
    float completion = 0.0F;
    float runner = 0.0F;
    float passDaring = 0.0F;
    float spaceBehind = 0.0F;
    // Threat (pass, shot, carry): the chance of a goal from the option.
    float threat = 0.0F;
    // Shot.
    float shout = 0.0F;
    float wideShot = 1.0F;
    float cutInsideShot = 0.0F;
    float roleShotBias = 0.0F;
    float weakFootShot = 0.0F;
    float lateLeadShot = 0.0F;
    float comfortableLeadShot = 0.0F;
    // Carry.
    float carrySpace = 0.0F;
    float carryKeep = 0.0F;
    float carryRisk = 0.0F;
    float carryHeld = 0.0F;
    float cutInsideCarry = 0.0F;
    float comfortableLeadCarry = 0.0F;
    // Shield.
    float shieldKeep = 0.0F;
  };
  /** The carrier decision's weights with no layers: every default, and 0
   * for every weight a layer sets (instructions, his role, a shout, the
   * game state). The xG map draws the shot score with these. */
  [[nodiscard]] static CarrierWeightsCapybara neutralCarrierWeightsCapybara();
  [[nodiscard]] CarrierWeightsCapybara carrierWeightsCapybara(
      const MatchPlayer& carrier) const;
  /** The shot score's core at a spot, added to `shot`: the threat, a
   * shout, and the wide-angle discount. Shared by the brain and the xG map. Returns the
   * range eligibility (0 to 1) the brain's role terms fade with. */
  static float addShotCoreCapybara(DecisionFrame::Score& shot,
                                   const CarrierWeightsCapybara& weights,
                                   const MatchPitch& pitch, PitchPoint at,
                                   bool attackingRight, float shotXG);
 private:
  /** Chance he keeps the ball if he carries it (or shields it): 1 minus
   * the pressure on him times the chance the nearest opponent's tackle
   * wins it (MatchRules::tackleWinChance, a standing tackle). */
  /** The xG from where his carry would take him by his next decision:
   * about a second towards carryTargetCapybara at carrying speed, with the
   * nearest opponent measured from there. */
  [[nodiscard]] float carryEndXGCapybara(const MatchPlayer& carrier) const;
  [[nodiscard]] float keepChanceCapybara(const MatchPlayer& carrier,
                                         bool shielding,
                                         float pressure) const;
  MatchPlayer* chooseTakeOnCapybara(MatchPlayer& carrier);
  void afterPassReleasedCapybara(const MatchPlayer& passer, const MatchPlayer& receiver);
  /** @param details Debugger detail: every team-mate looked at (or null). */
  std::optional<PassOption> choosePassTargetCapybara(
      MatchPlayer& passer,
      std::vector<PassCandidateDetail>* details = nullptr);
  PassOption evaluatePassOptionCapybara(MatchPlayer& passer, MatchPlayer& receiver,
                                PassCandidateDetail* detail = nullptr) const;
  ShotAim chooseShotAimCapybara(const MatchPlayer& shooter, bool header, bool penalty,
                        bool freeKick);
  float choosePenaltyGuessCapybara(const MatchPlayer& keeper);
  ClearanceAim chooseClearanceAimCapybara(const MatchPlayer& defender);
  PitchPoint chooseClearBehindTargetCapybara(const MatchPlayer& defender);
  HeaderAttempt chooseHeaderAttemptCapybara(const MatchPlayer& header,
                                    bool fromTeammate);
  HeaderPlay chooseHeaderPlayCapybara(const MatchPlayer& header, bool fromTeammate);
  Challenge chooseChallengeCapybara(const MatchPlayer& carrier, float dt);
  bool goesIntoContestCapybara(const MatchPlayer& defender,
                                const MatchPlayer& owner, PitchPoint contact);
  TackleIntent chooseTackleIntentCapybara(const MatchPlayer& tackler,
                                           const MatchPlayer& attacker,
                                           PitchPoint contact, bool freeBall);
  /** No outfield team-mate goal-side of him (the last man), and one
   * goal-side within COVER_METRES (cover). */
  struct DefensiveCoverCapybara
  {
    bool lastMan = true;
    bool covered = false;
  };
  DefensiveCoverCapybara defensiveCoverCapybara(const MatchPlayer& defender) const;
  bool touchNowCapybara(const MatchPlayer& player, const TouchMoment& moment) const;
  bool dribbleTouchNowCapybara(const MatchPlayer& player,
                            const TouchMoment& moment) const;
  /** Whether a clearly closer point of the ball's roll, safely in his
   * reach, is coming before anyone else could play it (touch timing). */
  bool waitsForCloserCapybara(const MatchPlayer& player,
                           const TouchMoment& moment) const;
  TouchIntent chooseFirstTouchCapybara(const MatchPlayer& player,
                                    const TouchMoment& moment) const;
  bool choosesToBlockCapybara(const MatchPlayer& player);
  bool choosesToPutBlockBehindCapybara(const MatchPlayer& player);
  bool choosesToClearCrossBehindCapybara(const MatchPlayer& player);
  MatchPlayer* chooseRestartTakerCapybara(MatchState restart, bool homeTeam,
                                  PitchPoint spot, bool topCorner = false);
  RestartPlay chooseRestartPlayCapybara(MatchPlayer& taker, MatchState restartState);
  RestartPlay chooseCornerPlayCapybara(MatchPlayer& taker);
  void applyTeamMentalityCapybara(StrategySliders& sliders, bool homeTeam) const;
  SetPieceAttackers arrangeSetPieceAttackCapybara(bool attackingHome,
                                          PitchPoint ballPosition);
  void arrangeSetPieceDefenceCapybara(bool defendingHome, PitchPoint ballPosition,
                              const SetPieceAttackers& attackers);
  MatchPlayer* dutyTakerCapybara(bool homeTeam, SetPieceDuty duty);
  MatchPlayer* bestSetPieceTakerCapybara(bool homeTeam, bool shooting);
  void updateRestartSupportCapybara(MatchPlayer& player, const MatchPlayer* taker);
  static const MovementStyle& movementStyleCapybara();

  // --- Capybara brain: helpers only it uses ----------------------------------
  /** Whether a wide forward in the final third is cutting inside toward
   * the box rather than going down the line to cross. */
  bool cutsInsideCapybara(const MatchPlayer& player) const;
  /** Heading it out of play the safest way: hard and high over the nearest
   * touchline from wide of the six-yard box; in line with it, up and over
   * the bar or glanced on towards the corner, whichever is easier. */
  HeaderPlay chooseHeaderOutCapybara(const MatchPlayer& header) const;
  /** Where the carrier heads with the ball: forward, away from the nearest
   * opponent, inside or down his lane. */
  PitchPoint carryTargetCapybara(const MatchPlayer& player) const;
  /** The pace (m/s) he carries the ball at (dribble meet plan 2c): from
   * the space along his way, the team's phase and its risk-taking, walk to
   * sprint; Dribbling caps how much of his pace he can use with it. */
  float carryPaceCapybara(const MatchPlayer& player) const;
  /** What this brain decides for itself (MatchEngine::BrainTraits). */
  const BrainTraits& brainTraitsCapybara() const;
  /** How urgently he carries it (0-1): what carryPaceCapybara maps onto
   * his bands. */
  float carryUrgencyCapybara(const MatchPlayer& player) const;
  /** How long until he wants his ball again after a touch (s), the push's
   * intent (dribble meet plan 3c): as soon as he can with an opponent near;
   * with a turn on his route or the end of his run (`toEnd` metres ahead)
   * coming within the push at `pace`, as far as there; else longer the more
   * urgently he carries it. */
  float chooseTouchSecondsCapybara(const MatchPlayer& player, float pace,
                                    float toEnd) const;
  /** How far a push from `from` (metres) along `direction`
   * (metric unit) goes (m): as far as he runs at `pace` in `seconds` from
   * `startGap` behind it, never past `toGoal` (less room to roll on), and
   * no further than he still gets to it first. */
  float pushMetresCapybara(const MatchPlayer& player, PitchPoint from,
                            Vector2F direction, float startGap, float pace,
                            float seconds, std::optional<float> toGoal) const;
  /** Signed x shift of the role in the defensive block (metres). */
  float defensiveRoleShiftCapybara(const MatchPlayer& player) const;
  /** Second presser's spot when doubling up on the carrier, if ordered. */
  std::optional<PitchPoint> doubleUpPointCapybara(const MatchPlayer& helper,
                                        const MatchPlayer& carrier,
                                        const MatchPlayer* firstPresser) const;
  /** Multiplier of a defender's rate of engaging the carrier. */
  float engageBoostCapybara(const MatchPlayer& defender,
                    const MatchPlayer& carrier) const;
  /** Chance an opponent cuts out a pass along the straight lane: a man
   * already in it close to the passer is the likeliest to block it. */
  float passingLaneRiskCapybara(const MatchPlayer& passer,
                                 const MatchPlayer& receiver) const;
  /** Formation spot while his side has the ball (role and shape shift). */
  PitchPoint possessionAnchorCapybara(const MatchPlayer& player) const;
  /** Seconds a role is quicker (+) or slower to close the ball down. */
  float pressEagernessCapybara(const MatchPlayer& candidate,
                       const MatchPlayer* carrier) const;
  /** Share of the presser's normal stand-off distance. */
  float pressStandOffShareCapybara(const MatchPlayer& presser,
                           const MatchPlayer& carrier) const;
  /** Shot appetite added by a shout (positive shoots more). */
  float shoutShotBiasCapybara(bool homeTeam) const;
  /** Pass utility lost to a tight marker standing next to the receiver. */
  float tightMarkPenaltyCapybara(const MatchPlayer& receiver) const;
  /** Moves a marker's target tight and goal-side of his man. */
  PitchPoint tightMarkPointCapybara(const MatchPlayer& marker, const MatchPlayer& target,
                          PitchPoint current) const;
  /** How fast that tight-marking point moves with his man (m/s). */
  Vector2F tightMarkVelocityCapybara(const MatchPlayer& target) const;
  /** The man this player marks when he is under a tight-marking order. */
  const MatchPlayer* tightMarkTargetCapybara(const MatchPlayer& marker) const;
  /** Presser's lateral shade onto the carrier's stronger side (metres
   * y; 0 without a weaker-foot order). */
  float weakFootShadeCapybara(const MatchPlayer& carrier) const;
  /** Collision plan step 5: a fallen player gets up by urgency: jumps up
   * for a ball near and still to be won, gets up in a stoppage, gingerly if
   * play runs on long; else stays down (0). */
  float chooseGetUpCapybara(const MatchPlayer& player);
// clang-format on
