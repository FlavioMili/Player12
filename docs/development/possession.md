# Possession architecture

Stages 1–3 consolidate existing behaviour. One per-match possession version owns
one stored account of attachment, receipt, flight and attribution. Old writable
possession fields on `MatchBall` and `MatchEngine` are removed. Foot-control
criteria, claim expiry, friendly takeover, receiver priority and reaction/chaser
behaviour remain unchanged; policy changes belong to Stage 4.

## Storage and mutation ownership

`MatchPossessionState` contains 43 former ball fields, plus `PossessionHistory`
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
There is no delayed transition queue. Both versions retain compatibility behaviour
validated against first-touch-fix baseline `a95680a`.

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
| `getConfirmedController()` / `controller` | Open play plus keeper hands or a collected touch. Stage 4 decides stronger foot-control criteria. |
| `getRestartOwner()` / `RESTART_OWNER` | Attachment outside ordinary open play, including setup/shootout. Not physical dead-ball custody evidence. |
| `getExpectedReceiver()` / `expectedReceiver` | Delivery intent, independent of control. |
| `hasTouchClaim()`, `isTouchCollected()`, `isTouchDropping()` | Individual stored facts, including partial helper-boundary combinations. |
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
original ball fields and nine history fields, private backing, const views,
selected-owner access, removed fields and documented restore sites. The 3D
renderer's separate projection is explicitly recognized. C++ private/const access
also rejects invalid consumers. Lexical scanning cannot prove arbitrary alias/
macro safety; review complete engine restores and new alias paths.

`possessionIssues(snapshot)` reports contradictory flags, unknown/inactive owners,
hand-state problems, stale meet ownership and conflicting pass/shot facts. It
neither asserts nor repairs. Sample completed steps: helper boundaries can be
partial. Trace observations identify 76 existing operation categories, not an
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
cmake --build build --config Release --target possession_contract_check possession_seam_check
```

Run both executables, sandbox checks and recorded baseline comparisons. The seam
check covers selection/defaults/persistence, dispatch, helper-boundary queries,
private/const API checks, every stored field across versions, copies, participant
changes, contact/release/keeper fixtures, full matches and shootouts. Replay/
rewind, FULL/BACKGROUND lab comparisons and the boundary audit complement it.
Seeded coverage is not proof of every legal/contact branch. GoogleTest suites
remain unavailable in the offline setup; known calibration gates and legacy
diagnostics are recorded rather than silently repaired.
