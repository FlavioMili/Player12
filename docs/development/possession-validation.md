# Possession migration close-out

Stage 5 closes the storage/ownership audit, scenario evidence inventory, sandbox
labels and documentation. Gameplay checkpoints are Stage 3 `be6c744`, control and
claim expiry `abe384e`, and teammate coordination `5525926`. Stage 5 changes no
simulation policy. Visual acceptance of the motivating watching-teammate incident
is still pending; this document does not declare that behaviour visually fixed.

## Evidence and reproduction

The workspace evidence root is `Notes and plans`, alongside `Open_FM`. Historical
evidence remains immutable: `possession-audit/stage3-final` records preservation;
`possession-stage4/control`, `claims` and `coordination` record successive policy
slices. `possession-stage5` holds the final build, boundary export, check logs,
ten FULL/BACKGROUND lab reports and source/binary/seed manifest. The old
`possession-audit/scenario-coverage.csv` is the initial gap inventory, not the
current result. The table below supersedes its status column.

The offline Release build uses Ninja/clang-cl and cached dependencies. Available
targets are listed in [possession.md](possession.md). The Stage 5 lab comparison
requires both possession policies to reproduce their own `5525926` results;
policy slices are not required to equal one another. Lab calibration exit 1 is
retained separately from runtime failure. Determinism is established in this
build environment, not promised across compilers/platforms. GoogleTest/core suites
are unavailable in the offline configuration (`BUILD_TESTING=OFF`, no cached
GoogleTest source); they have not been reported as passing.

Evidence keys in the matrix:

- **Contract:** [snapshot/query checks](../../test/possession_contract_check.cpp).
- **Control / Claim:** [control evidence](../../test/possession_control_check.cpp)
  and [expiry/recovery evidence](../../test/possession_claim_check.cpp).
- **Coordination / Friendly:** [selection/order checks](../../test/possession_coordination_check.cpp)
  and [public-API friendly/offside scenarios](../../test/possession_friendly_check.cpp).
- **Seam:** [integration checks](../../test/possession_seam_check.cpp), including
  `compareVersions`, `compareFirstTimeDrill`, `compareContactDrill`,
  `participantLifecycle` and `persistence`.
- **Sandbox:** [full checks](../../src/tools/match_sandbox_checks.cpp): recording,
  replay/rewind, seeded drills, derived stats, ownership, first touch and restarts.
- **Boundary:** [owner audit](../../scripts/check_possession_boundary.cjs),
  [21 allowed/rejected fixtures](../../test/possession_boundary_check.cjs) and
  C++ private/const access assertions in Seam.

“Targeted” means an explicit fixture/assertion covers the named behaviour;
“representative” means seeded integration/repeatability evidence. Neither means
every legal/contact combination in a broad row is proven. The last column names
the remaining limit. All 45 original scenario IDs are accounted for.

## Scenario coverage

| ID | Scenario | Current evidence | Remaining limit |
|---|---|---|---|
| P01 | Stationary unclaimed ball | Contract uncontrolled projection; Sandbox loose-ball drill and ownership checks | Representative collection; not every overlap/reach edge forced. |
| P02 | Rolling ball, chaser behind, teammate ahead | Targeted Friendly nearer non-receiver; Coordination arrival versus motion/space; receiving intent asserted | Exact original recording and visual plausibility remain unverified; pursuit forecast is approximate. |
| P03 | High/rising/falling/bouncing loose ball | Control height gates; Seam header/keeper fixtures; Sandbox high-ball drills | Representative heights/bounces, not every body/trajectory combination. |
| P04 | Ground/lofted/driven/chipped pass and cross | Seam Pass and first-time fixtures; Sandbox ground/lofted/driven/chip drills | Cross and extreme weights are representative, not exhaustive attribution fixtures. |
| P05 | Intended receiver collects | Targeted Seam Pass after touch and exactly-once credit; Control acquisition | Not every collection pressure/condition combination. |
| P06 | Better-placed teammate on pass | Targeted Friendly fast delivery on both teams; selected receiving intent; Coordination ties | Numeric tuning and visual attacking-space judgement remain provisional. |
| P07 | Opponent intercepts/controls/plays first time | Seam pass/contact attribution comparisons; Sandbox interceptions | Opponent first-time interception is not separately forced across all releases. |
| P08 | Missed/overhit/removed/offside receiver | Targeted Friendly release-time onside/offside non-receivers on both teams; Seam participant lifecycle | Receiver removal during an airborne delivery and all miss/out branches are not forced individually. |
| P09 | First-time pass/shot/clearance | Targeted Seam first-time Pass and Shot target; incoming motion/spin preservation | First-time clearance has no separate forced acceptance fixture. |
| P10 | Set/into-stride foot touch | Control acquisition; Seam touch state/motion; Sandbox touch-kind/error totals | Touch-kind totals do not prove every technique parameter edge. |
| P11 | Chest/thigh/header cushion | Targeted Seam Header target/cushion transitions; Sandbox chest/thigh drill repeats | Some chest/thigh drill runs meet the head instead; successful chest/thigh contact is not guaranteed by repeatability. |
| P12 | Heavy/failed/misdirected touch | Sandbox first-touch outcomes/errors; Seam loss and physical preservation | Representative errors; not all forced error directions/heights. |
| P13 | Dribble/knock-on/stop/turn | Targeted Control retention/grace/loss/reacquisition; Seam integration counters; Sandbox motion budget | Tight-turn/knock-on extremes not exhaustively forced. |
| P14 | Unrecoverable claim, closer teammate | Targeted Claim recovery/grace/age/unavailable cases; Seam expiry invariants; Friendly takeover | Recovery is radial, not a full turn/route solver; original trailing-chaser visual acceptance pending. |
| P15 | Waiting/dead zone/commitment/recovery | Claim cooldown tests; Sandbox waits and zero waited-and-lost outcomes; contact gate traces | Every timing/refresh/substep edge is not independently forced. |
| P16 | Take-on/shield win/loss | Sandbox take-on launch-budget regression and full-match preservation | Shield win/loss has no dedicated forced policy fixture. |
| P17 | Standing tackle intents | Targeted Seam Tackle intent sweep, mixed bodies/seeds, pending/immediate credit invariants | Intent sweep does not guarantee every timing-dependent outcome. |
| P18 | Slide/failure/contested block | Seam tackle/contact comparisons and operation coverage | Sliding and simultaneous-contact permutations are not all forced. |
| P19 | Natural/reaction/friendly/opposing block | Targeted Seam Shot block parameter sweep; deflection provenance/stat comparisons | Not every body part, reaction and shooter/team combination. |
| P20 | Falls/collisions/stumble/held keeper fall | Seam Collision sweep; loss/deflection helper observations; Sandbox ownership | Keeper already holding during a forced fall is not a dedicated fixture. |
| P21 | Header shot/pass/clear/cushion/duel | Targeted Seam three Header target aims; contested headers required in matches | Not all clearance/duel/overlap variants forced. |
| P22 | Shot on/off target/blocked/dying/deflected | Seam Shot target and Shot block, keeper tests, stat/provenance comparisons | Representative dying/re-deflected shot sequences. |
| P23 | Woodwork follow-on | Seam requires woodwork observation; complete match copies/results | Every woodwork-to-goal/save/new-strike attribution chain remains unforced. |
| P24 | Own goal/deliberate defensive play | Preserved owner/rules code; mixed seeded match results | No dedicated own-goal attribution fixture; deliberate-play history is not newly modelled. |
| G01 | Standing/diving keeper catch | Seam keeper claims/Shot target; continued held-state comparisons | Not every catch posture/contact origin. |
| G02 | Parry/tip/body/arm rebound/fumble | Seam low-handling/high-skill fixtures and rebound/save operations | Not every body-part/fumble pressure combination forced. |
| G03 | High catch/punch/failure/collision | Targeted KeeperClaimFixture skill/crowding sweep, legacy/developing bodies | Aerial keeper collision is representative, not separately forced. |
| G04 | Low gather/running smother | Keeper/contact operation coverage; Sandbox/Seam shot fixtures; historical keeper evidence | Still-ball overlap versus moving-sweep smother is not a dedicated paired fixture here. |
| G05 | Held movement/dive/landing/recovery | Seam continued held steps, full-field copies and motion/attachment comparison | Forced fall/landing extremes remain unforced. |
| G06 | Roll/throw/punt/feet | Targeted Seam Keeper throw release variants/ranges, both body directions | Full held-punt drop/strike timing permutations not exhaustive. |
| G07 | Area/back-pass/hand eligibility | Contract invalid-hand diagnostics; preserved handling gates and mixed matches | No exhaustive back-pass/outside-area physical eligibility matrix. |
| R01 | All ordinary restart types | Sandbox restart tallies, both out-of-play versions; Seam rare wall/direct-free-kick branches | Not every type/version/taker condition permutation. |
| R02 | Throw entry/non-entry/direct goal | Seam throw-entry/rule operations; preserved rule code | Untouched direct-goal and failed-entry chains are not individually forced. |
| R03 | Out/whole-ball line/goal/net/celebration | Sandbox full matches; Seam goal/woodwork/lifecycle operations; Control retaining-contact boundary case | Every contact-versus-line substep ordering is not forced. |
| R04 | Foul/advantage/recall/offside | Targeted Friendly offside release judgements; seeded referee transitions | Advantage/recall and offence/interference permutations remain representative. |
| R05 | Dead-ball retrieval/return/carry/place | Sandbox restarts; board-gap approach regression and route checks | Fan/steward transport variants are not all isolated fixtures. |
| R06 | Penalty save/rebound/shootout | Targeted Seam eight shootout component combinations and copies | In-match penalty rebound is not separately forced in every keeper outcome. |
| L01 | Periods/extra time/shootout/full time/abandonment | Seam full matches/shootouts; four reset sites audited; Sandbox replay | Extra-time and abandonment boundaries are not separately forced. |
| L02 | Substitution/red card/walk-off/keeper replacement | Targeted participantLifecycle owner substitution/removal, both bodies/policies; copies | Historical identity rewriting is deliberately retained; rare removal/custody combinations remain unforced. |
| L03 | Injury/fall/unavailable actor | Targeted Control/Claim unavailable/fallen cases; Seam Collision and lifecycle | Not every injury/held-custody timing combination. |
| T01 | Switching/manual action/replayed input | Sandbox input/command replay and rewind; Seam controlled first-time scenarios | Every manual action/contact timing is not forced; switching still consumes broad attachment. |
| T02 | Scenarios/feeds/forced actions/reset | Targeted participantLifecycle synthetic feed; first-time/contact drills; public Friendly setup | Synthetic feeds deliberately have no genuine release-time offside judgement. |
| T03 | Copy/look-ahead/load/rewind/resume | Targeted Seam every stored field/deep copy/recorder detach/persistence; Sandbox replay/rewind | Cross-build/platform equality is not claimed. |
| T04 | Render/inspector/analytics/reports | Boundary const views; render value-copy audit; snapshot/trace contracts; compiled inspector badges | Labels/layout have not been visually accepted; render attachment is not a second controller. |
| V01 | Policy/component/default/format combinations | Targeted Seam selection/persistence/mixed bodies/brains/out-of-play; ten lab comparisons; sandbox default | Representative cross-products, not the complete Cartesian product of every engine component. |

## Ownership and compatibility audit

There are 92 classified ball fields: 48 in the sole private possession record
(43 original facts plus five policy fields) and 44 physical/planning/eligibility/
diagnostic fields outside it. Nine history fields are nested in the owner record.
Only the selected Aardvark/Binturong owner files mutate backing. Four whole-ball
resets occur there; each saves/restores history. Consumers borrow const views or
hold independent value snapshots. Reviewed alias hits are const `MatchBall`
references, render-value projections or camera focus values, not writable aliases
to possession backing. The scanner is lexical; C++ private/const access supplies
the language-level boundary.

No temporary writable mirror remains to remove. Retain these intentional
compatibility paths:

- Per-match Aardvark policy and dispatchers, required for frozen comparisons.
- Broad attachment queries and `LEGACY_ATTACHED`, which preserve existing
  body/rule/controller meanings without asserting confirmed foot control.
- `MatchRenderBall`, an independent renderer value projection.
- Recording format 1 fallback, format 2 selection and explicit replay overrides.
- `check_possession_writers.cjs`, a forwarding command name for the boundary
  audit, not a state adapter.

The inspector displays `(control)`, `(claim)`, `(attached)` or `(restart)` for
attachment, plus `(expected)` independently. Its follow-ball tooltip describes
broad attachment. Trace/controller/claim queries are the source for debugging;
delivery expectation, last contact and chasing intent are not evidence of control.

## Remaining acceptance and follow-ups

Stage 5 records these limits rather than quietly converting them into passes:

- Visually review the original watching-teammate behaviour. The nearer-friendly
  fixtures establish permission and receiving selection, not football realism.
- Run GoogleTest/core suites when their dependencies are available. Keep known
  calibration failures and broad row gaps separate from runtime failures.
- Physical/deliberate-touch history remains unknown where it was never stored;
  continuity statistics are not a last-touch history. Historical substitution
  rewriting, broad-attachment consumers and stale-meet diagnostics remain debt.
- Reaction/awareness timing, full turning/route prediction, keeper aggregate
  measurement and balance/tuning are follow-ups, not hidden migration changes.

Use the row IDs above when adding targeted fixtures. The matrix is a coverage
inventory, not a claim that all 45 broad semantic contracts have exhaustive proof.
