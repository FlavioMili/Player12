# Tests and tools

Configure with tests (`release-tests` preset or `-DBUILD_TESTING=ON`):

Install the [platform prerequisites](../user/installation.md) first. If you
are already using a build directory such as `build`, use `ctest --test-dir build`
instead of the preset; a preset always targets its own `build/` directory.

```sh
cmake --preset release-tests
cmake --build --preset release-tests --parallel 4

# The fast suites, as in CI
ctest --preset release-tests -LE "playtest|monkey|slow"

# Everything, including the long playtest, GUI monkey and adversarial suites
ctest --preset release-tests
```

Tests run headless (SDL's dummy video driver) with fixed seeds and a scratch
data directory under `/tmp/football-management-tests`, so they never touch
your saves. Labels: `unit`, `core` (no GUI), `gui`, `lab`, `playtest`,
`monkey`, `adversarial` and `slow`.

## Run one area first

```sh
ctest --preset release-tests -N -R 'MatchScenarioTest'
ctest --preset release-tests -R '^core::MatchScenarioTest\.'
```

CTest lists each GoogleTest case separately. Core cases have a `core::` prefix;
the main `unit_tests` cases have no prefix. Engine and Play tests currently
live in `unit_tests`; match scenarios, rules, tactics and scheduler tests also
have headless coverage. Use `-N` to confirm that a filter selects actual tests
before relying on its result.

The [engine guide](match-engine.md) maps behaviors to tests. For larger code
changes, also run the CI-style suite above and the relevant sanitizer checks
from [Builds](builds.md). Preserve failing output in your PR description.

## Balance lab

`fm_lab`, built next to the game, is a headless balance lab. It plays large
batches of matches or whole seasons and compares the results with real-world
ranges:

```sh
./build/release-tests/src/fm_lab matches --n 1000 --spread --threads 4
./build/release-tests/src/fm_lab season --seasons 1 --threads 2
./build/release-tests/src/fm_lab tactics
```

`./build/release-tests/src/fm_lab --help` lists every option. The lab writes
`report.md` and `metrics.json` to its output directory. Compare before/after
results using the same seeds, ratings, count and fidelity; see
[Extending and modding](extending-and-modding.md#validate-a-behavior-change).

## Screenshots

The screenshots in `docs/images` come from a real career, captured by the
`showcase_captures` test (skipped unless `FM_SHOWCASE_DIR` is set):

```sh
FM_SHOWCASE_DIR=/tmp/showcase ctest --preset release-tests -R showcase
```

It writes PNG files at interface scale 2; the header of
[test/test_showcase_captures.cpp](../../test/test_showcase_captures.cpp) explains
the options.

## Performance benchmarks

For a change to database work or a hot simulation loop, measure its workload
before and after the edit. Benchmarks are opt-in and are not part of normal CI.

```sh
cmake --preset profile
cmake --build --preset profile --target db_benchmarks match_benchmarks --parallel 4
./build/profile/benchmarks/db_benchmarks
./build/profile/benchmarks/match_benchmarks
```

The `profile` preset enables machine-local native optimizations; use comparable
builds and hardware when reporting results. Benchmark target definitions are
in [benchmarks/CMakeLists.txt](../../benchmarks/CMakeLists.txt).

The ten-seed headless match unit test checks full-time state, simulated match
duration and the final event. It reports average and fastest CPU times as
GoogleTest properties, without enforcing a machine-specific millisecond limit.
CPU time still varies with frequency limits, compiler and hosted-runner hardware;
use the opt-in match benchmarks for comparable before/after performance results.

## Historical calibration failures

Verification on 2026-10-07 reproduced these failures with the original engine
from revision `8b0aafb`, before the documentation/context-override changes:

| Test | Observed mismatch |
|------|-------------------|
| `MatchEngineTest.StrongerTeamHasStatisticalAdvantage` | 7.1875 goals/match exceeded the 5.5 calibration bound |
| `MatchEngineTest.ConditionCanBeCarriedBetweenMatches` | Both compared players finished at the 0.35 condition floor |
| `TacticsTest.InPossessionShapeMovesTheSlotWithTheBallOnly` | The observed in-possession width shift missed the expected threshold |

The condition and shape fixtures now compare controlled, equivalent scenarios.
Goalkeeper timing, handling calibration and defensive-touch calibration address
the stronger-team and season regressions without widening their expected ranges.
Set-piece own goals retain their restart origin and contribute to set-piece
totals; `SetPieceOwnGoalsKeepTheirRestartOrigin` covers that accounting path.
That regression pins a replay containing an actual own goal, rather than
requiring the rare incident to occur in an arbitrary batch of matches.
The tight-marking comparison uses 64 paired matches with identical seeds for
marked and unmarked opponents; its original 10% reduction requirement remains.

Run renderer checks through CTest so SDL's dummy driver and the fixed world
seed match CI. A direct invocation can inherit a desktop video driver and open
a visible window. Camera input fixtures dismiss the pre-match modal explicitly;
highlight skipping must not determine whether input is blocked.

These historical observations are not permission to ignore new failures. Compare
a behavior change with its base revision and include the evidence in the PR.
Timing tests may skip unless their opt-in environment is enabled; a skipped case
is not a test pass. Local success does not replace checks on the platform runners.
