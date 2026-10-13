# Design notes recovered from Spec Kitty

This page preserves rationale from the local Spec Kitty planning and
implementation records, checked against the repository on 2026-10-07. The
original working files are ignored by Git, so the explanations below are
self-contained and do not require that archive in a fresh clone. They do not
imply that every planned requirement was implemented.

## Source provenance

The mission archive is
`.local-tools/spec-workspace/kitty-specs/open-football-manager-01M3HB8T/`.
These original paths identify historical private records, now archived outside
the checkout in `FootballManagement-private-archive/private-tools-2026-10-13.tar.gz`.
They are not required to build or run the public project. These records informed
the developer guides:

| Original record | Material reused |
|-----------------|-----------------|
| `research.md`, decisions D-01, D-02, D-10, D-11, D-17, D-20 | Isolated randomness, physical time, balance measurement, physical shot outcomes, observation boundaries and referee variation |
| `plan.md`, concerns IC-02, IC-06–IC-09, IC-16, IC-19 | Simulation/presentation separation, tactics and physics responsibilities, data extensibility and shared human/AI execution |
| `contracts/match-replay.md`, `contracts/render-snapshot.md`, `contracts/rng-streams.md` | Reasons to capture inputs, isolate observation and ownership, and version reproducibility claims |
| `contracts/data-pack-schema.md`, `contracts/rule-set-schema.md` | Longer-term pack and ruleset aspirations, separated from current modding support |
| `decisions/DM-01M3KN4FPM26CKVA30KW6QC0C5.md` (2026-09-28) | Background fidelity as a throughput/balance tradeoff; recorded by an orchestrator pending stakeholder review, not a stakeholder-approved specification |
| `.local-tools/agent-docs/handoff/day3-engine.md` | Implemented movement batching, shared target snapshot, MatchContext, finishing-level normalization and calibration work |
| `.kittify/evidence/01M4BBK427BVRD06YZR70MV38C/evidence.md` | Short Play halves, restart pacing/support and consistent keeper reach; a duplicate plan exists under `01M4BBVV9ZKXW8VSPSF0XGSVEN` |

The gameplay evidence files end at a proposed-plan/review boundary. Their
presence alone does not prove implementation; the current `PlayModeTest` and
keeper-rule tests are the implementation references. Historical handoff timing
and calibration numbers are not fresh measurements of this checkout and are
not presented as current test results.

## Rationale that still applies

**One match model, several ways to observe or control it.** The planning
records sought consistent football across watched, headless and controlled
matches. The current engine owns outcomes; the GUI supplies input and renders
snapshots. Human control replaces decisions for one player while retaining
physical execution. This prevents a second set of football rules growing in
the input controller or renderer.

**Plan together, move afterwards.** The implementation handoff explains the
shared target-planning pass: moving a player immediately after choosing his
target lets later players react to a different picture. Selecting targets
before batched movement removes that ordering bias and permits efficient
movement kernels. The current `updateMovement()` preserves this sequence.

**Let the shot travel.** Research decision D-11 argues against pre-deciding
a goal and inventing a trajectory to fit it. The current shot path produces
a strike, then resolves reach, handling, blocks and goal-line crossing.
The xG estimate describes/evaluates the chance. It is not a goal command.

**Vary execution without changing the score directly.** The engine handoff's
MatchContext controls finishing precision, referee strictness and the home
edge through existing mechanisms. Level normalization was introduced because
changing all players' quality also changes defenders and keepers, which can
produce surprising scoring trends. Evaluate both absolute level and relative
team strength when tuning those effects.

**Treat speedups as either exact or statistical changes.** The handoff separates
arithmetic-preserving optimizations from changes that alter outcomes. Current
background fidelity only coarsens selected restart waits, and must be compared
with full fidelity through aggregate balance. Replay tests compare the same
inputs and fidelity. Neither test substitutes for the other.

**Shorten the played clock without speeding up bodies.** The later gameplay
plan addresses 45-minute real-time halves and long restart waits. Current Play
mode scales the displayed clock, bounds waits and keeps the physical timestep.
The keeper change uses a shared horizontal/vertical reach budget across contact
paths so claim, save and dive geometry do not contradict each other.

## Planned contracts versus current implementation

| Historical proposal | Current code and practical consequence |
|---------------------|----------------------------------------|
| 100 Hz physics; separate perception layers and a new `src/model/match/` tree | `match_engine.cpp` remains the main implementation: 10 Hz body/action ticks, 5 Hz routine tactical refresh, 40 Hz free ball and 50 Hz controlled body integration. Use current tuning values, not old tick units. |
| All match randomness in a new registry of counter-keyed streams | World simulation has `WorldRng`; the match still has two `std::mt19937` streams plus hash/epoch noise. Reordering sequential draws can change later play. |
| Owned v2 snapshots, action rings and triple-buffered publication | The render snapshot still borrows player and stats/event pointers. Do not treat it as a serializable or thread-safe owned value. |
| Versioned replay files with input hashes and final digests | The engine exposes command/input logs and debug JSON. No such complete replay-file importer/exporter is supplied. |
| Versioned ZIP packs, `pack.json`, namespaces, pack validator and pinned career content | Current content is the JSON asset tree described in the [data reference](../../assets/user_made_data/README.md). There is no `fm_packtool` or pack manager. |
| JSON rulesets and `assets/tuning/match/` | Rules and most tuning are C++ in `MatchRules`, `MatchTuning`, `TacticsTuning` and league profiles. `MatchContext` is a bounded per-match C++ input. |
| A separate calibrated fast statistical model for some fixtures | Current senior background matches run `MatchEngine` with BACKGROUND fidelity; this does not introduce a separate statistical result model. |
| Fitted xG model and `targets.json` lab input | xG is currently an in-code heuristic; lab targets are compiled in [lab_report.cpp](../../src/tools/lab_report.cpp). |

When reviving an old work package, first locate its current symbols and tests.
Keep useful requirements, but rewrite examples against the working API. A
historical contract marked “final” is not evidence that a class, file format or
runtime option exists.

## This documentation and extension pass

The developer guides promote the verified explanations into maintained docs
and add block-level comments to the engine's planning, stepping, execution and
ownership boundaries. They also correct the legacy grid-weight description
and document snapshot borrowing explicitly.

The small functional addition is `MatchSimulationInput::context_override`,
using a shared `match_context.h` value type. It allows a headless fixture to
replace its league-derived settings without changing global tables. No override
preserves the existing path. Tests compare explicit defaults, explicit league
settings, clearing an override, clamping/fallback and sequential/parallel runs.
See [Extending and modding](extending-and-modding.md) for the full contract.
