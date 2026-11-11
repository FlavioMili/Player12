# Possession architecture

Stages 1–3 consolidated existing behaviour. One per-match possession version owns
one stored account of attachment, receipt, flight and attribution. Old writable
possession fields on `MatchBall` and `MatchEngine` are removed. Stage 4 now changes
Binturong foot-control establishment/retention, claim expiry and friendly
collection coordination. Reaction/awareness timing and broader chaser tuning
remain separate work; Aardvark retains its legacy policy.

Stage 5 ownership/compatibility review and documentation close-out are complete.
See [validation and the 45-row coverage inventory](possession-validation.md) for
evidence, preserved debt and explicit gaps. Visual acceptance of the motivating
watching-teammate behaviour remains separate and has not been claimed.

## Storage and mutation ownership

`MatchPossessionState` contains 43 former ball fields, Binturong's current foot-
control bit/separation timer, two claim clocks and release-time offside collector
identities, plus `PossessionHistory`
with nine former engine fields: continuity team/time, completed-pass and shot
attribution, pending tackle credit and header-sequence bookkeeping. Its sole live
instance is private `MatchBall::possession`. It is value-owned: engine copies and
reconstruction restore the complete record. Player pointers retain their previous
borrowed identities.

The engine retains references to configuration, so copy assignment is unavailable;
restore paths reconstruct from copied engines rather than assigning engines.

Consumers receive const references through `getPossessionState()`,
`getPossessionHistory()` or `getBall().possessionState()`. These borrowed views
are not writable compatibility mirrors. Retain `PossessionSnapshot` instead when
an ID/value view must outlive the current engine or step. A local record copy is
independent of live state.

Selected implementations in `match_possession_aardvark.cpp` and
`match_possession_binturong.cpp` own writes. Synchronous helpers dispatch through
`match_possession_dispatch.cpp`; nested helpers use the same match selection.
There is no delayed transition queue. Aardvark retains compatibility behaviour
validated against first-touch-fix baseline `a95680a`; Binturong policy changes are
compared against Stage 3 checkpoint `be6c744`.
The control/expiry slice is committed as `abe384e`; coordination is committed as
`5525926` and compared with that checkpoint. Stage 5 preserves both policies.

`MatchBall` retains 44 fields outside possession: physical/rules geometry,
brain/body plans, eligibility masks/timers and contact diagnostics/exposure.
They are not competing control authorities. Dead-ball return/transport, restart
entitlement/readiness, pending actions and tactical caches stay with existing
rule/body/planning subsystems. Stats consume history, not another live owner.

Whole-ball lifecycle/shootout resets preserve `PossessionHistory`, matching the
old separation between ball resets and engine history. Explicit history changes
remain at their original points. Complete engine restores copy both. Recorder
links detach on copy. First-time play still preserves incoming motion/spin and
aerial-delivery state before consuming the outgoing intent. Motion restoration
and aerial-bounce cleanup emit no observations because look-ahead uses them.

## Read queries and compatibility meanings

| Query / snapshot field | Meaning |
|---|---|
| `getPossessionAttachment()` / `legacyOwner` | Broad attachment: claim, attached ball or restart owner. Not confirmed control. |
| `getTouchClaimant()` / `claimant` | Owner while the touch flag is set, including collected touches. |
| `getConfirmedController()` / `controller` | Open play plus keeper hands or current foot control. Binturong uses explicit control evidence; Aardvark retains its legacy collected flag. |
| `getRestartOwner()` / `RESTART_OWNER` | Attachment outside ordinary open play, including setup/shootout. Not physical dead-ball custody evidence. |
| `getExpectedReceiver()` / `expectedReceiver` | Delivery intent, independent of control. |
| `hasTouchClaim()`, `isTouchCollected()`, `isTouchDropping()` | Claim/receipt facts, including partial helper boundaries. Collected means receipt completed, not necessarily current Binturong control. |
| `isKeeperHoldingBall()` | Stored hand-hold fact; eligibility remains a rules/body judgement. |
| `getPendingPasser()` | Pass origin awaiting collection/completion. |
| `getLastPossessionAttachment()`, `getLegacyContactActor()` | Limited legacy history, not a complete physical/deliberate-touch stream. |
| `getPossessionState()` | Const flight/provenance: pass target/offside origin, shot/header/xG/save facts, keeper attachment, path, deflection/woodwork/throw records. |
| `getPossessionHistory()` | Const continuity/accounting and pass/shot/tackle/header history. |

`PossessionSnapshot` interprets stored facts without repairing them. A touch flag
alone need not identify a valid actor. Open-play attachment without touch/hand
evidence is `LEGACY_ATTACHED`. `lastPhysicalTouch` and `lastDeliberatePlay` remain
unknown because existing history cannot reconstruct them reliably.

Team possession statistics retain two-team continuity through passes, loose balls
and contested intervals, using `lastControlledTeamHome`. Percentages sum to 100%;
there is no uncontrolled bucket. This differs from physical control and a strict
last-touch model. Existing substitution history rewriting and the known stale-meet
diagnostic remain compatibility debt, not migration repairs.

Advantage, goal-chance denial, carrier planning and automatic switching use broad
attachment where they previously did. Requiring confirmed control would change
policy. Renderers consume separate `MatchRenderBall` values populated through
engine views; their compatibility fields are not live possession storage. Sandbox
reports/checks, inspector, audio, controllers and recorder checksum also read the
consolidated record.

## Foot-control policy

Agreed policy: actual collection establishes foot control; normal dribbling
retains it with brief separation tolerance. Uncollected claims use
recoverability, brief tolerance and a maximum duration. Any teammate may contest
a playable ball, even during existing control. Competing teammate intentions are
coordinated by arrival time and collection speed/direction: a marginally
earlier arrival can defer to a better attacking collection. Intended receivers
get a tie-break preference only. Two-team continuity statistics remain unchanged.

In Binturong `updateTouch`, a claimed
ball must be within the existing 0.9 m kick reach, at foot height and within the
configured collection-relative-speed limit before becoming collected. Normal
touches retain control in a wider 3.0 m envelope. Relative ball/player velocity
projects separation 0.35 s ahead. Marginal separation has 0.20 s grace, reset on
regaining the envelope. Current or predicted separation over 4.5 m, height more
than 0.75 m above the foot line, or an inactive/fallen actor ends control without
grace. Minor height excursions have 0.15 m tolerance. These are initial tuning
values in `match_possession_control.h`, not user-agreed numerical constants.

Control loss clears current foot control while keeping its claimant, completed
receipt, path and physical motion. Regaining control does not repeat pass credit
or first-collection decision delay. Initial receipt still uses the existing
bookkeeping once. Claim expiry and teammate collection coordination are described
below; broader pursuit forecasting and reaction timing remain separate work.
Legacy attached Aardvark-body states retain their compatibility interpretation;
the new policy applies to Binturong possession's touch-based path, independently
of which body produced the touch. Keeper hand custody is unchanged.

The current control bit and timer are private owner-written state and follow copies/
restores. New claims, attachment/clear transitions and collected-dribble setup
reset them; a claim without current control does not accumulate this timer. It is not a claim
expiry counter. `possession_control_check` supplies explicit acquisition, normal
retention, grace, clear loss and recollection scenarios. Run
`possession_seam_check --possession aardvark` and the Binturong equivalent
separately: each compares two independent runs of that policy, full state/copies,
dispatch and motion. Cross-version equality is no longer required. No-argument
seam checks use Aardvark; selection/default contracts are still checked.

### Claim expiry

An uncollected Binturong claim now expires immediately if its actor is inactive/
fallen, after 0.30 s without a plausible recovery, or after 6.0 s continuously
without current foot control. These are initial tuning values, not new policy
decisions. Control resets both claim clocks before receipt callbacks. A recovered
forecast resets only the unreachable clock; a new claim resets both.

Recovery samples the selected ball physics 2.5 s ahead at 0.10 s intervals,
including gravity, bounce, rolling and spin. Samples must still be in play under
the whole-ball line rule (including a centre slightly outside the pitch) and
within foot height, reach and cooldown availability. The arrival estimate uses
the claimant's speed toward each point, top-speed cap and cached acceleration;
it is a radial estimate, not a full body/turning or obstacle simulation. It does
not compare teammates or grant exclusive collection permission.

Expiry clears attachment/current touch and deferred receipt, cancels that actor's
pending strike and meet plan, and preserves velocity/spin, path and rules/contact
provenance, continuity and accounted pass/shot/tackle/header history. Deferred
tackle credit still resolves when somebody actually acquires the ball. The rest
of the current action step returns without acting through the expired carrier.
Trace snapshots expose both claim clocks and an `expire-touch-claim` observation.
Aardvark retains its previous claim lifetime.

Expiry's changed match paths exposed an existing retaining-contact inset: a ball
partially over a line was snapped back inside by a normalized 0.001 margin.
Binturong foot/dribble, body-control and cushioned-header contacts now retain the
actual contact point. Aardvark retains the old inset. No velocity/spin calculation
or motion-budget tolerance was relaxed. `--check-first-touch` runs the focused
existing first-touch check when diagnosing these physical-contact cases.

Changed trajectories also exposed a legacy restart approach that sent a taker
outside the bottom advertising boards directly through a solid panel. With
Binturong possession and Aardvark out-of-play, the taker now approaches through
a board gap before lining up. Already queued manual restarts retain their input
while approaching. Kick-off setup keeps its existing sequence. Aardvark possession
and Binturong out-of-play keep their existing paths. `--check-restarts` runs the
focused restart checks; no line-up timeout or contact tolerance was increased.

## Friendly collection and receiver preference

Binturong permits any teammate to attempt a playable contact, including a ball
inside the current claimant's reach or a fast delivery intended for someone
else. This removes identity-based protection; reach, body orientation, recovery,
commitment, kicker lockout, keeper handling and opponent contests still apply.
Keeper-held and genuinely attached balls keep their existing handling paths.
There is still only one actual attachment/controller. Candidates are temporary
choices, not extra stored owners. A friendly foot/body takeover cancels the previous
claimant's pending strike/meet plan and creates no tackle or interception credit.

`match_possession_coordination.h` shares one choice rule between Binturong's
receiving/chasing plans and playable swept contacts. Arrival time is reduced by
up to 0.10 seconds for collection quality: current attacking velocity into space,
and movement along the delivery. A clearly earlier player therefore still wins.
The adjusted cost is grouped into 0.005-second bins; receiver intention breaks a
tie, then stable player identity. These are initial tuning values. Ground-contact
coordination is limited to chronological groups from one side; it cannot cross an
earlier opponent, body collision or shot block. If the preferred player declines
or misses, remaining candidates still run. Header choices similarly preserve an
earlier opponent's opportunity. The existing physical forecast and reaction/
commitment models are unchanged; this is not full turning/route prediction.

The selected Binturong collector receives the receiving run and preparation;
the named receiver no longer forces that role. First-touch surprise follows the
player's preparation intent, rather than delivery identity. Aardvark brains keep
their legacy planning, even when paired with Binturong possession permission.

Opening collection also requires release-time offside judgements for other
collectors. Binturong records judged-offside player identities when a real pass is
released, using the selected referee and its restart exemptions. Foot/body and
header contacts consult that record. Substitutes cannot inherit another player's
judgement through a pitch slot. The intended receiver's existing judgement is
retained. Synthetic drill feeds have no actual release judgement. Snapshots and
traces expose `passOffsidePlayers` / `pass_offside_players`; flight cleanup resets
it and engine copies preserve it. Team possession continuity remains unchanged.

## Version selection and persistence

`PossessionId` is independent of brains, bodies and out-of-play, including mixed
pairings. Constructor selection precedes kick-off setup. Engine/career defaults
remain Aardvark. `setPossessionVersion` permits selection before the first step
and rejects changing it later. Copies retain selection.

New sandbox matches/drills default to Binturong. Sandbox selectors and sandbox/lab
match, tactics and drill commands accept `--possession aardvark|binturong`.
Implicit sandbox checks and lab commands preserve the Aardvark default. Season
simulation retains legacy possession and rejects the option. Lab reports record
the choice.

Setups store `possession`; absent old fields load as Aardvark regardless of body/
brain selection. Recording format 2 requires the choice; format 1 uses Aardvark
fallback. Invalid choices and unsupported versions are rejected. Opening a
recording preserves its choice. Explicit "Replay with" overrides report recorded
and replayed versions. Internal state is not serialized as a raw memory blob, so
this layout change needs no recording format bump. Deterministic comparisons
apply to the validated build environment; cross-build/platform identity is not
guaranteed.

## Boundary checks and diagnostics

Run `node scripts/check_possession_boundary.cjs`; the older
`check_possession_writers.cjs` forwards to it. `--out-dir <directory>` exports
classifications, writes, whole-ball restores and alias review. It checks all 87
original ball fields, five added policy fields and nine history fields,
private backing, const views,
selected-owner access, removed fields and documented restore sites. The 3D
renderer's separate projection is explicitly recognized. C++ private/const access
also rejects invalid consumers. Lexical scanning cannot prove arbitrary alias/
macro safety; review complete engine restores and new alias paths.

`possessionIssues(snapshot)` reports contradictory flags, unknown/inactive owners,
hand-state problems, stale meet ownership and conflicting pass/shot facts. It
neither asserts nor repairs. Sample completed steps: helper boundaries can be
partial. Trace observations identify 77 operation categories, including Binturong
claim expiry, not an
invented complete physical-contact or transition stream.

```text
match_sandbox --world-seed 1 --brains binturong --bodies binturong --handshake binturong --referee binturong --managers binturong --ball binturong --out-of-play binturong --home-advantage 0 --possession binturong --trace-possession 300 possession.jsonl
```

Use a writable output directory. Duration is simulated seconds in `(0, 3600]`;
the first two clubs of the first league and match seed 1000 are used. JSONL
contains metadata, plans, helper observations, contact gates, per-step snapshots/
targets/intents and issue masks, summary and fingerprint. Gates report a taken
branch, often before reach testing; they do not prove collection was possible.
The trace compares observed/unobserved engines and copy recorder detachment.

Standalone offline checks:

```text
cmake --build build --config Release --target possession_contract_check possession_control_check possession_claim_check possession_coordination_check possession_friendly_check possession_seam_check
```

Run these executables, sandbox checks and recorded baseline comparisons. The seam
check covers selection/defaults/persistence, dispatch, helper-boundary queries,
private/const API checks, every stored field across versions, copies, participant
changes, contact/release/keeper fixtures, full matches and shootouts. Replay/
rewind, FULL/BACKGROUND lab comparisons and the boundary audit complement it.
Seeded coverage is not proof of every legal/contact branch. GoogleTest suites
remain unavailable in the offline setup; known calibration gates and legacy
diagnostics are recorded rather than silently repaired.

`node test/possession_boundary_check.cjs` exercises isolated allowed/rejected
owner, const-view and restore cases. Trace snapshots expose `foot_control` as a
boolean for Binturong and null for Aardvark's legacy interpretation; `collected`
continues to describe completed collection/receipt.
