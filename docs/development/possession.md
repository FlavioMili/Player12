# Possession: version selection and observation

Stage 2 selects one possession implementation per match and dispatches the five
shared ownership/flight-cleanup helpers, touch-claim and collection updates,
release/first-time bookkeeping, tackle/header consequences and player deflection/
collision bookkeeping, keeper hold/save/release transitions, and restart/rule/
shootout boundaries.
Aardvark freezes their `a95680a` behaviour; Binturong initially contains the same
implementation.
Collection, claims, teammate takeover, movement and technique policy are unchanged.
Other direct mutation paths and authoritative-state extraction remain future work.

Touch-claim creation (including dropping chest/thigh/header touches), conversion
from an attached outfield ball to a collected dribble, per-step collection and
dribble-touch bookkeeping now dispatch through possession. The collection path
retains its existing pass credit, tackle settlement and random decision delay in
the same order. `inTouch`, `touchCollected` and `touchPasser` mutations live in
the possession implementations. The body still calculates contact outcomes and
applies ball motion; contact eligibility/wait masks and dribble-meet planning
remain at their existing sites. Aardvark technique calculations stay behaviourally
frozen; legacy wall/restart write sites now use the dispatcher too.

Pass preparation, release attribution, delivery/throw-in flight flags and shot
flight evidence now dispatch through possession. Pass preparation calls the
existing offside judgement at its original point, before the physical strike.
`launchBall` still calculates direction and calls `sendBallOff` before releasing
the claim. Pass target geometry, launch position/velocity/spin, strike evaluation
and action execution remain in the engine/body.

Pass completion/interception accounting also dispatches through possession,
including the existing deferred collection credit. First-time play keeps its
original sequence: record the incoming pass, save motion, acquire ownership,
restore motion (including spin and aerial-delivery state), consume the forced
intent and consume/execute the pending strike. Only intent consumption and
receipt bookkeeping move; contact eligibility and strike timing are unchanged.
Rules-specific shot-origin markers and other restart/contact paths still
await their own migration groups. This is not yet authoritative-state extraction.

Tackle attempts with deferred credit now record, settle or cancel that credit
through possession. Immediate tackle-win credit remains a separate operation.
Poke bookkeeping is split around the existing physical poke: clear ownership/
flight history first, then update the credited team and transition phase at the
old point after launch/recovery writes. Nudge attribution and a tackle's pending
pass/clearance strike also dispatch. A contested block retains its existing
`clearPossession` call; neither its contact model nor its flight cleanup policy
has changed.

Header contact/sequence history, headed-shot pass/assist credit, played-header
receipt/interception cleanup and cushioned-header claim evidence dispatch too.
Headed-shot credit deliberately keeps its old specialised path rather than
calling ordinary receipt credit. Duel winner/foul decisions, reach, aim,
placement and outgoing motion remain with the existing engine/body/rules code.

Collision falls now release eligible claims and clear that player's pending
strike/meet through possession, retaining the existing keeper-hold exception.
A rebound off a fallen body records guarded restart-touch provenance without
transferring control or rewriting last possession. Its bounce remains physical.
Active body hits separately record blocked-pass/deflection credit and clear the
old claim/flight at their original boundaries. Shot contact attribution, ending
a deflected shot's tracking, and block-versus-deflection credit also dispatch;
the existing one-block-credit rule and still-on-goal judgement are unchanged.
Legacy failed-touch attribution dispatches after its unchanged physical response.
Keeper catch/parry decisions remain physical. Lifecycle/drill writers and phase
housekeeping still await their own group.

Keeper hold flags, attachment origin/time (including during a dive), held-release
kind, hand-rebound attribution, save/shot-resolution credit and the HOLD transition
now dispatch through possession. Catch eligibility, smother/punch/fumble choices,
hand contact, dive/gather geometry, held-ball placement and release velocity/spin
remain in the body. Save credit retains its original position relative to dive
recovery, acquisition and SAVE events. Legacy punches retain bookkeeping on each
side of their existing physical launch. A bounced shot's target/re-read flags
and legacy spent-shot cleanup also dispatch without changing trajectory judgement.
Body claim-attempt/outcome counters and physical keeper planning remain at their
existing sites; this is still behaviour-preserving migration, not new custody
semantics or authoritative-state extraction.

Both out-of-play versions now hand restart ownership/history through possession,
while keeping entitlement, taker choice, retrieval/carrying, placement and legal
readiness with the existing rules/body code. Throw-in entry flags, woodwork path/
touch provenance, legacy wall-stop/block credit and shot set-piece/direct-free-
kick origin markers dispatch. Physical wall/frame response and legal decisions
are unchanged. Shootout ball reset/assignment and keeper-plane resolution also
use the selected implementation; live shot simulation, result decisions and the
existing match-stat rollback remain where they were. The raw shootout ball reset
is an explicitly named compatibility operation; its spot is still placed by the
shootout orchestrator immediately afterwards.

## Facts and uncertainty

`MatchEngine::getPossessionSnapshot()` projects current legacy state into
`PossessionSnapshot` (`model/match_possession.h`). Each call is read-only,
consumes no random draws and returns IDs/values, not pointers into an engine.

| Field / mode | Meaning |
|---|---|
| `legacyOwner` | The existing `ball.possessedBy`, without stronger claims. |
| `claimant` / `TOUCH_CLAIM` | An owner following an uncollected touched ball. |
| `controller` / `FOOT_CONTROL` | A legacy touch already marked collected. |
| `controller` / `KEEPER_HANDS` | The owner is recorded as holding the ball. |
| `LEGACY_ATTACHED` | An open-play owner without touch/hand flags. The old code can attach the ball, but this does not establish new physical-control semantics. |
| `RESTART_OWNER` | Owner pointer outside ordinary open play, including setup/shootout. Not proof of live control or actual dead-ball custody. |
| `expectedReceiver` | Receipt intention; does not grant control. |
| `legacyLastPossessor`, `legacyKicker`, `legacyDeflector` | Existing histories preserved under explicit legacy names. |
| `lastPhysicalTouch`, `lastDeliberatePlay` | Unknown in this projection. Existing histories cannot reconstruct these reliably. |
| `lastControlledTeamHome` | Existing accounting/phase history, distinct from current control. |

Team possession statistics use that attribution history as a two-team continuity
model. Credited time continues through passes, loose balls and contested periods;
the reported percentages sum to 100%. There is no separate uncontrolled/contested
percentage, and a touch does not necessarily change the credited team. Migration
preserves this accounting model; the control/claim distinction above does not
silently redefine the statistics.

`PossessionId` is independent of the selected brains and bodies. The seeded
engine constructor accepts it before kick-off setup; ordinary engine/career
construction defaults to Aardvark. `setPossessionVersion` accepts a valid choice
before the first simulation step and rejects changing it afterwards. Copies
retain the selection. Snapshots report the selected version.

New sandbox matches and drills default to Binturong. The setup screen has a
Possession selector; sandbox and lab matches/tactics/drill accept
`--possession aardvark|binturong`. Season simulation retains legacy possession
and rejects this option.
Sandbox `--check` and lab commands preserve the Aardvark default unless explicitly
selected. Lab reports include the actual choice.

Setups store `possession`; an absent old field loads as Aardvark even when
brains/bodies are Binturong. New recording format 2 requires that choice;
format 1 remains readable with the legacy fallback. Unsupported versions and
invalid choices are rejected. Opening a recording preserves its choice;
"Replay with" can explicitly override it and reports recorded/replayed versions.

`possessionIssues(snapshot)` diagnoses contradictory flags, unknown/inactive
owners, invalid hand-state evidence, stale meet ownership and conflicting
pass/shot flags. These are observations, not assertions or repairs. Sample
them at step end: ownership helpers deliberately pass through intermediate
states before callers finish a touch, release or catch. Some findings may be
legacy compatibility debt rather than a physical impossibility.

## Transition contract for the migration

The planned coordinator is value-owned by the engine. One selected possession
implementation handles the whole match, including mixed bodies. Requests are
committed synchronously at existing contact boundaries, before later contacts
see the new state. No delayed event queue and no second writable mirror.

| Proposed operation | Evidence / responsibilities |
|---|---|
| Begin touch claim | Actor, actual contact/path and purpose; do not assume collection. |
| Confirm foot control | Actor and actual collection evidence; finish pending pass once. |
| Release | Actor, action kind, recipient expectation and attribution; physics applies the actual launch separately. |
| Record contact | Physical actor, deliberate-play judgement if known, path change and outcome; a deflection need not transfer control. |
| Acquire in hands | Eligible keeper, actual catch and body-owned attachment origin. |
| Lose control | Actor/reason and dependent-plan invalidation; preserve still-needed rules history. |
| Begin dead ball / set custody / release restart | Entitlement and actual holder separately from live control. |
| Participant unavailable / scenario reset / restore | Stable identity, explicit lifecycle semantics and complete copied state. |

These are the remaining coordinator design contracts. The existing `setPossession`,
`takePossession`, `clearPossession`, `handBallTo` and `clearFlightState` now dispatch
to separate Aardvark/Binturong implementations, as do `beginTouchClaim`,
`beginCollectedDribble`, `updateTouch`, `noteTouchOnBall` and `noteDribbleTouch`.
Release/receipt helpers are `releasePossession`, `beginPassFlight`,
`finishPassFlight`, `noteShotFlight`, `recordPassCompletion`,
`creditPassCompletion`, `consumeFirstTimeIntent` and `consumePendingStrike`.
Tackle/header helpers cover deferred and immediate tackle credit, stoppage
cancellation, poke/nudge provenance, a pending tackle strike, header sequence
history, headed-shot credit, played-header cleanup and cushioned-header claims.
Deflection/collision helpers are `loseBallOnFall`, `noteFallenDeflection`,
`noteBodyDeflectionHit`, `beginBodyDeflection`, `beginShotDeflection`,
`endDeflectedShotFlight`, `finishShotDeflection` and `noteFailedTouch`.
Keeper helpers cover beginning/rebasing/ending a hold, distribution type, hand
rebound attribution, shot resolution/parry/save credit, legacy punches and shot
rearming/spent-shot cleanup. Caller guards and physical attachment stay in place.
Restart/rule helpers are `assignRestartOwner`, `markThrowEntry`,
`noteWoodworkContact`, `markWallStoppedShot`, `creditWallBlocker`,
`noteShotSetPieceOrigin`, `noteDirectFreeKickShot`, `resetShootoutBall` and
`resetRuleContactAttempts`. Existing selected cleanup/resolution helpers are reused.
Nested helper calls use the same
per-match dispatcher. No second writable state is introduced. Existing
random draws, resets (including spin), placement and accounting order must be
preserved. Aardvark stays behaviourally frozen. During Stage 3 readers migrate
and old writable fields can be removed; read-only projections may remain.

## Diagnostic trace

```text
match_sandbox --world-seed 1 --brains binturong --bodies binturong
  --handshake binturong --referee binturong --managers binturong
  --ball binturong --out-of-play binturong --home-advantage 0
  --possession binturong
  --trace-possession 300 possession.jsonl
```

Use one command line in a shell, with an existing writable parent directory.
Duration is simulated seconds in `(0, 3600]`. The first two clubs of the first
league are used, at match seed 1000. World seed controls the generated squads.

JSONL contains:

- `metadata`: schema, legacy projection and sampling limitations.
- `team-plan`: actual selected candidates and arrival ranking keys reported
  by the existing brain; intended-receiver priority may override ranking.
- `helper`: existing assign/acquire/clear helper boundary, possibly partial.
  Touch helpers also report claim creation, collection updates, initial collected
  dribbles and touch/dribble bookkeeping. These are observations at existing
  boundaries, not an authoritative transition stream.
  Release, pass/shot flight evidence, receipt credit and consumed first-time/
  pending-strike intents have their own observation labels too.
  Tackle/header bookkeeping adds labels for attempts, settlement/cancellation,
  poke/nudge history, header contact/sequence/credit and cushioned claims.
  Deflection/collision labels identify fall cleanup, fallen-body provenance,
  body-hit credit, shot/pass flight cleanup and failed-touch attribution.
  Keeper labels identify holds and release type, save/parry credit, punch/hand
  rebounds and shot resolution/rearming.
  Restart/rule labels identify custody handover, throw entry, woodwork provenance,
  wall credit, shot origin and shootout reset. Raw woodwork flag writes now use
  an existing shared cleanup helper, adding observations of that category too.
  `executed_possession` identifies the implementation which actually ran, allowing
  routing tests independently of snapshot labels. Direct field writes are not all
  represented as helper events.
- `contact-gate`: a branch actually taken in the contact sweep, for actors
  within 6 m of the ball at observation. Most gates run before reach testing;
  their presence alone does not prove the actor could collect the ball.
- `step`: snapshot, issue mask and every active player's target, intent,
  position, velocity, facing, reach-related cooldowns and pursuit state.
- `summary`: per-tick observed/reference equivalence and counts.
- `final-fingerprint`: the existing event/stat fingerprint comparison.

The trace runs a second engine with identical inputs and no recorder, comparing
the value view, match state and existing checksum after each tick. It also
compares the existing final fingerprint. This detects observation effects but
is not a proof that every hidden engine field is equal. Recorder attachment
still detaches on copies, so look-ahead copies do not emit live observations.

For pure state-contract checks without GoogleTest:

```text
cmake --build build --config Release --target possession_contract_check
cmake --build build --config Release --target possession_seam_check
```

Run the resulting executables. The contract check tests separation of expectation, claim,
control and restart ownership, unknown legacy history and invalid-state
diagnostics. Integration equivalence needs the sandbox trace and baseline
comparisons as well. The seam check compares both possession implementations
tick-by-tick across four brain/body pairings, checks actual helper routing,
version immutability/copy behaviour, setup fallback and recording compatibility.

Lifecycle and writer boundary audit
-----------------------------------

Stage 2 now routes lifecycle resets, scenario ownership, fed balls, participant
substitution/removal, phase history, shot/assist history and contact provenance
through the selected possession implementation. The existing substitution rule
which rewrites matching last-possessor history to the incoming player is retained.
There are 76 observable operation categories. Motion restoration and aerial
bounce cleanup also dispatch, but emit no helper observation because speculative
look-ahead uses these operations.

Run `node scripts/check_possession_writers.cjs` to check the mutation boundary;
`--out-dir <directory>` additionally exports the inventory and review tables.
It classifies all 87 MatchBall fields and guards 43 control, receipt and provenance
fields plus nine engine history fields. Remaining fields belong to physical/rules
geometry, body/brain plans, contact eligibility or diagnostics. The check is lexical;
aliases, macros and complete engine copies still require review. Current live-ball
alias review found only the const getBall accessor. Complete engine copies remain
supported restores, with recorder detachment handled by the existing copy contract.

Dead-ball transport, restart entitlement/readiness, pending actions and tactical
caches remain in their existing subsystems. They must be distinguished from live
control when readers migrate. Team possession accounting continues to use the
last-controlled-team model. Routing mutations does not yet make the new state
authoritative: Stage 3 still needs explicit reader queries and removal of competing
legacy state. Claim expiry, takeover and chaser/reaction policy remain later work.

Stage 3 reader migration (in progress)
--------------------------------------

Shared engine attachment and expected-receiver reads now use explicit queries.
`getPossessionAttachment()` preserves the broad legacy owner meaning, including
claims and restart attachment; callers must not interpret it as confirmed control.
`getTouchClaimant()` includes collected and uncollected touch ownership, matching
the value snapshot. `getConfirmedController()` requires open play and keeper hands
or a collected touch. `getRestartOwner()` returns attachment only outside open
play. `getExpectedReceiver()` is delivery intent, independent of control.

Queries return borrowed player pointers and do not validate eligibility or repair
state. The value snapshot remains the retained/copy-safe view. The seam check
compares these queries with the snapshot at observed transition boundaries.
Brain/body, rules, tools and external getBall consumers still require migration;
legacy fields remain the single storage authority until that work is complete.
