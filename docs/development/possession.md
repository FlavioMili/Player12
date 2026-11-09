# Possession: Stage 1 model and observation

The match still uses its original possession implementation. Stage 1 adds a
value-only view and optional diagnostics; it does not redirect ownership,
change collection/takeover rules, or dispatch a new possession version.

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

`PossessionId` defines the agreed independent per-match version vocabulary.
Stage 1 snapshots truthfully report Aardvark (legacy possession), regardless
of selected brains/bodies. Version dispatch, the sandbox selector/default and
recording persistence are Stage 2/3 work. No stored version field exists yet.

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

These are design contracts, not implemented mutators. During Stage 2 existing
random draws, resets (including spin), placement and accounting order must be
preserved. Aardvark stays behaviourally frozen. During Stage 3 readers migrate
and old writable fields can be removed; read-only projections may remain.

## Diagnostic trace

```text
match_sandbox --world-seed 1 --brains binturong --bodies binturong
  --handshake binturong --referee binturong --managers binturong
  --ball binturong --out-of-play binturong --home-advantage 0
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
  Direct field writes are not all represented as helper events.
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
```

Run the resulting executable. It tests separation of expectation, claim,
control and restart ownership, unknown legacy history and invalid-state
diagnostics. Integration equivalence needs the sandbox trace and baseline
comparisons as well.
