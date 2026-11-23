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
  /** The keeper's set position against a threat at `threat` (his standing
   * place on the ball-goal line, deeper the nearer the threat): what
   * goalkeeperTargetCapybara returns when nothing else calls him away, and
   * the "usual" position the game xG keeper layer measures. */
  [[nodiscard]] PitchPoint keeperSetPositionCapybara(const MatchPlayer& keeper,
                                                     PitchPoint threat) const;
  /** The chance a shot from `from` goes in (game xG plan): the empty-goal
   * map times the keeper map's factor for the opponents' keeper, by the
   * shooter's Shooting, the keeper's Goalkeeping and the shooter's foot;
   * where this goal has no maps yet, the fitted model (xgBreakdown). */
  [[nodiscard]] float gameShotXGCapybara(const MatchPlayer& shooter,
                                         PitchPoint from) const;
  /** Whether the opponents' keeper leaves the goal more open than usual for
   * a shot from where `shooter` stands: more of the mouth uncovered than at
   * his set position (Decision::GOAL_OPEN_SHARE_MARGIN), or rushing him. */
  [[nodiscard]] bool goalOpenCapybara(const MatchPlayer& shooter) const;
  /** The opponents' keeper of `homeTeam`'s side, if one is on (const). */
  [[nodiscard]] const MatchPlayer* opponentKeeperCapybara(bool homeTeam) const;
  /** Game xG layer 3: the chance a shot by `shooter` from `from` gets past
   * every body in the way (team-mates too; not the opponents' keeper, layer
   * 2's). Averaged over the aim cone (the lines Capybara's aim can pick,
   * weighted by their chances); along each line, each body where his
   * movement will have him when the ball gets there: through his torso a
   * block (unless it glances off narrowly and still goes in), through his
   * leg-space the leg-chance table's chance; the chances of getting past
   * each multiplied. 1 with no leg-chance table. */
  [[nodiscard]] float bodiesPassCapybara(const MatchPlayer& shooter,
                                         PitchPoint from) const;
  /** A driven shot's launch, (along, up) m/s, at `speed` (his launch
   * speed) to be `arrivalHeight` m up `distanceMetres` out: as
   * strikeShotBodyBinturong launches one. */
  [[nodiscard]] std::pair<float, float> drivenLaunchCapybara(
      float distanceMetres, float speed, float startHeight,
      float arrivalHeight, BallSpin spin) const;
  BallAction chooseBallActionCapybara(MatchPlayer& carrier);
 public:
  /** The brain flow tool (model/match_brain_flow.h): a record for each
   * carrier decision to fill (null: none, as in every match), and the
   * influence layers it plays with (a BrainFlow::LayerMask; all by
   * default). Only the carrier decision reads the mask so far. */
  /** The brain flow tool's off-ball view (step O5): places the scenario
   * as applyScenario does, but nobody decides; runs the team planner once
   * (marks, then both sides' targets), filling `record` for its player.
   * False if the scenario could not be placed or he was not planned. */
  bool brainFlowOffBallCapybara(const MatchScenario& scenario,
                                float matchMinute, int homeGoals,
                                int awayGoals,
                                BrainFlow::CarrierRecord& record);
  void setBrainFlowCapybara(BrainFlow::CarrierRecord* record,
                            std::uint8_t layers)
  {
    flowRecord = record;
    flowLayers = layers;
  }
  // Public for the sandbox xG map, which draws the shot score core.
  /** Weighting for the carrier decision (DecisionFrame; brain redesign plan
   * section 15, step A): how much each of its measures counts for him. The
   * defaults are MatchTuning's; the layers (team instructions, his role, a
   * shout) move them. Weights that depend on the receiver
   * (his role, an order on him) are set where the pass is scored. */
  struct CarrierWeightsCapybara
  {
    // Layer steps (DecisionFrame::stepped), summed per weight; the weights
    // below already include them. Kept so a pass can add the receiver's
    // role to progress.
    float progressSteps = 0.0F;
    float keepSteps = 0.0F;
    float shotThreatSteps = 0.0F;
    // Shared by pass and carry: progress (the chance it comes off times the
    // gain towards goal) and the space where the ball ends up.
    float progress = 0.0F;
    float space = 0.0F;
    // Pass, per candidate.
    float lengthOffIdeal = 0.0F;
    float safeOutlet = 0.0F;
    float completion = 0.0F;
    float runner = 0.0F;
    float spaceBehind = 0.0F;
    // Threat (pass, shot, carry): the chance of a goal from the option.
    float threat = 0.0F;
    // Shot. Its own copy of the threat weight, which his role and a shout
    // step.
    float shotThreat = 0.0F;
    float wideShot = 1.0F;
    // The goal more open than usual (game xG plan, step 2).
    float goalOpen = 0.0F;
    float cutInsideShot = 0.0F;
    float weakFootShot = 0.0F;
    // Carry.
    float carryKeep = 0.0F;
    float carryHeld = 0.0F;
    // Shield.
    float shieldKeep = 0.0F;
  };
  /** The carrier decision's weights with no layers: every default, no
   * steps. The xG map draws the shot score with these. */
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
  /** BrainFlow::LAYER_COUNT and BrainFlow::ALL_LAYERS (checked where the
   * brain uses them; this header can't include match_brain_flow.h). */
  static constexpr std::size_t FLOW_LAYER_COUNT = 6;
  static constexpr std::uint8_t FLOW_ALL_LAYERS = 0x3F;
  BrainFlow::CarrierRecord* flowRecord = nullptr;
  /** The tool's off-ball record while the planner fills it (step O5). */
  BrainFlow::CarrierRecord* offBallRecord = nullptr;
  /** Notes one player's off-ball frame in offBallRecord, if he is its
   * player: the situation, each option (name, open, what opens it) and
   * the pick. */
  void noteOffBallCapybara(const MatchPlayer& player, const char* situation,
                           std::span<const bool> open,
                           std::span<const char* const> names,
                           std::span<const char* const> opensWhen,
                           std::size_t chosen);
  std::uint8_t flowLayers = FLOW_ALL_LAYERS;
  /** Each influence layer's steps on the stepped weights (index by
   * BrainFlow::Layer), as carrierWeightsCapybara sums them. The team
   * sliders are split into the manager's tactics, the game state (team
   * mentality) and the shout. */
  struct LayerStepsCapybara
  {
    std::array<float, FLOW_LAYER_COUNT> progress{};
    std::array<float, FLOW_LAYER_COUNT> keep{};
    std::array<float, FLOW_LAYER_COUNT> shotThreat{};
  };
  [[nodiscard]] LayerStepsCapybara layerStepsCapybara(
      const MatchPlayer& carrier) const;
  /** Fills the brain flow record's weights for this carrier. */
  void noteFlowWeightsCapybara(const MatchPlayer& carrier,
                               const CarrierWeightsCapybara& weights) const;
  /** A carry candidate (brain redesign plan section 16, step C): a
   * direction and a pace, measured at his 1 s point on the true chances. */
  struct CarryCandidateCapybara
  {
    /** Metric unit vector. */
    Vector2F direction{};
    /** The pace as a level: walk 0, jog 1/3, run 2/3, sprint 1. */
    float urgency = 0.0F;
    PitchPoint end{};
    /** Path keep and the xG at the end, true (before his misreading). */
    float keep = 1.0F;
    float endXG = 0.0F;
    /** On the true chances; the search keeps the highest. */
    float score = 0.0F;
  };
  /** The carry he has chosen, held until his next decision (step C):
   * read by the planner (carryTargetCapybara) and the dribble pace and
   * touch (carryUrgencyCapybara). Only for this player in this
   * possession. */
  struct HeldCarryCapybara
  {
    PlayerID player = 0;
    double possessionStart = 0.0;
    Vector2F direction{};
    float urgency = 0.0F;
  };
  std::optional<HeldCarryCapybara> heldCarryCapybara;
  /** The held carry if it is his, in this possession; else null. */
  [[nodiscard]] const HeldCarryCapybara* heldCarryOfCapybara(
      const MatchPlayer& player) const;
  /** The best carry candidate: each direction (towards the goal; down the
   * pitch at 0, ±30, ±60, ±90, ±135 and 180 degrees) at each pace (walk,
   * jog, run, sprint), scored on progress, space, keep and threat at its
   * 1 s point. Off the pitch, or backwards near his own goal: ruled out.
   * No take-on choice (step C2): a carry past a defender is a carry. */
  [[nodiscard]] CarryCandidateCapybara bestCarryCapybara(
      const MatchPlayer& carrier, const CarrierWeightsCapybara& weights) const;
  /** A carry pace: its level (walk 0 to sprint 1) and m/s. */
  struct CarryPaceCapybara
  {
    float level = 0.0F;
    float metresPerSecond = 0.0F;
  };
  /** The carry directions open to him (metric unit vectors). */
  [[nodiscard]] std::vector<Vector2F> carryDirectionsCapybara(
      const MatchPlayer& carrier) const;
  /** One carry candidate scored on the true chances; none when its 1 s
   * point is off the pitch. */
  [[nodiscard]] std::optional<CarryCandidateCapybara> carryCandidateCapybara(
      const MatchPlayer& carrier, const CarrierWeightsCapybara& weights,
      Vector2F direction, CarryPaceCapybara pace) const;
  /** Chance he keeps the ball carrying from where he is to `to`: 1 minus
   * the largest, over opponents, of the pressure on his path (as near the
   * line he covers as the pressure radius) times that opponent's tackle
   * win chance. */
  [[nodiscard]] float pathKeepCapybara(const MatchPlayer& carrier,
                                       PitchPoint to) const;
  /** The chance `tackler`'s standing tackle wins the ball off `carrier`
   * (MatchRules::tackleWinChance), as the rules would settle it. */
  [[nodiscard]] float tackleWinChanceCapybara(const MatchPlayer& tackler,
                                              const MatchPlayer& carrier,
                                              bool shielding) const;
  /** The xG from `end` (a carry's 1 s point), with the nearest opponent
   * measured from there. */
  [[nodiscard]] float carryEndXGCapybara(const MatchPlayer& carrier,
                                         PitchPoint end) const;
  /** Progress, shared by pass and carry: the share of the pitch length the
   * ball moves towards the opponents' goal from `from` to `to`
   * (negative backwards). */
  [[nodiscard]] float progressCapybara(bool homeTeam, PitchPoint from,
                                       PitchPoint to) const;
  /** Space, shared by pass and carry: the nearest opponent to `at` over
   * the openness radius, 0 (on him) to 1 (10 m or more). */
  [[nodiscard]] float spaceAtCapybara(bool homeTeam, PitchPoint at) const;
  /** Chance he keeps the ball if he carries it (or shields it): 1 minus
   * the pressure on him times the chance the nearest opponent's tackle
   * wins it (MatchRules::tackleWinChance, a standing tackle). */
  [[nodiscard]] float keepChanceCapybara(const MatchPlayer& carrier,
                                         bool shielding,
                                         float pressure) const;
  static MatchPlayer* chooseTakeOnCapybara(MatchPlayer& carrier);
  void afterPassReleasedCapybara(const MatchPlayer& passer, const MatchPlayer& receiver);
  /** @param details Debugger detail: every team-mate looked at (or null). */
  std::optional<PassOption> choosePassTargetCapybara(
      MatchPlayer& passer,
      std::vector<PassCandidateDetail>* details = nullptr);
  /** How he misreads a pass's chances: log-odds offsets (judgement,
   * DecisionFrame::misread). {} is none. */
  struct PassMisreadCapybara
  {
    float completion;
    float receiverXG;
  };
  PassOption evaluatePassOptionCapybara(MatchPlayer& passer, MatchPlayer& receiver,
                                PassCandidateDetail* detail = nullptr,
                                PassMisreadCapybara misread = {},
                                DecisionFrame::Score* terms = nullptr) const;
  /** The aim for a strike: an open-play shot uses the aim his decision
   * held (the shot option carried it); otherwise rolled now. */
  ShotAim chooseShotAimCapybara(const MatchPlayer& shooter, bool header, bool penalty,
                        bool freeKick);
  /** Today's aim rules (side away from the keeper, inset, power shot,
   * height), rolled. */
  ShotAim rollShotAimCapybara(const MatchPlayer& shooter, bool header,
                              bool penalty, bool freeKick);
  /** The aim his shot option carried, held for the strike: only for this
   * player in this possession. */
  struct HeldShotAimCapybara
  {
    PlayerID player = 0;
    double possessionStart = 0.0;
    ShotAim aim;
  };
  std::optional<HeldShotAimCapybara> heldShotAimCapybara;
  /** The aim a restart's direct shot carried, held for the strike (set
   * and cleared each time a restart play is chosen). */
  struct HeldRestartAimCapybara
  {
    PlayerID player = 0;
    ShotAim aim;
    /** A penalty's aim, not a direct free kick's. */
    bool penalty = false;
  };
  std::optional<HeldRestartAimCapybara> heldRestartAimCapybara;
  /** Today's clearance aim (a random length, upfield and towards the
   * nearer touchline), rolled. */
  ClearanceAim rollClearanceAimCapybara(const MatchPlayer& defender);
  /** The aim his clear option carried, held for the strike: only for this
   * player in this possession. */
  struct HeldClearAimCapybara
  {
    PlayerID player = 0;
    double possessionStart = 0.0;
    ClearanceAim aim;
  };
  std::optional<HeldClearAimCapybara> heldClearAimCapybara;
  float choosePenaltyGuessCapybara(const MatchPlayer& keeper);
  /** "Restart: I'm the taker", a penalty (step H): his kick decided
   * before the run-up; it carries its aim (today's penalty rules, rolled
   * now), held for the strike. */
  void decidePenaltyCapybara(const MatchPlayer& taker);
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
  /** Where the carrier heads with the ball: along the direction of his
   * held carry (bestCarryCapybara), or straight down the pitch before he
   * has chosen one. */
  PitchPoint carryTargetCapybara(const MatchPlayer& player) const;
  /** Where his first touch aims (before his decision on the ball): the
   * carry target as it was before step C, until the first touch moves
   * onto the decision frame. */
  PitchPoint firstTouchAimCapybara(const MatchPlayer& player) const;
  /** The pace (m/s) he carries the ball at (dribble meet plan 2c): his
   * held carry's pace, walk to sprint; Dribbling caps how much of his pace
   * he can use with it. */
  float carryPaceCapybara(const MatchPlayer& player) const;
  /** The pace (m/s) he carries the ball at for an urgency level (0 walk
   * to 1 sprint), capped by Dribbling. */
  float carryPaceAtCapybara(const MatchPlayer& player, float urgency) const;
  /** What this brain decides for itself (MatchEngine::BrainTraits). */
  const BrainTraits& brainTraitsCapybara() const;
  /** How urgently he carries it (0-1): what carryPaceCapybara maps onto
   * his bands; his held carry's pace level (a drill's set dribbler: the
   * drill's). */
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
  /** Steps a shout gives the shot's threat weight (positive shoots more). */
  float shoutShotStepsCapybara(bool homeTeam) const;
  /** Steps a role profile value gives a weight: the largest values (about
   * 0.25) give DecisionFrame::LAYER_MAX_STEPS. */
  static float roleStepsCapybara(float profile);
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
