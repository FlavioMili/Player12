# Tests and tools

Configure with tests (`release-tests` preset or `-DBUILD_TESTING=ON`):

Install the [platform prerequisites](../user/installation.md) first. If you
are already using a build directory such as `build`, use `ctest --test-dir build`
instead of the preset; a preset always targets its own `out/build/` directory.

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
./out/build/release-tests/src/fm_lab matches --n 1000 --spread --threads 4
./out/build/release-tests/src/fm_lab season --seasons 1 --threads 2
./out/build/release-tests/src/fm_lab tactics
```

`./out/build/release-tests/src/fm_lab --help` lists every option. The lab writes
`report.md` and `metrics.json` to its output directory. Compare before/after
results using the same seeds, ratings, count and fidelity; see
[Extending and modding](extending-and-modding.md#validate-a-behavior-change).

For a change that should only alter float rounding, compare position traces
from two builds tick by tick. `fm_lab trace` takes the `matches` options plus
`--out FILE` and writes every player and the ball, in metres, at every fixed
step for the first `--seconds` (default 10) of each match. `fm_lab drill
"NAME" --trace FILE` does the same for one run of a drill. `fm_lab drill
--pitch 40x30 "NAME" ...` plays a drill on a walled training pitch of that
size instead of its own, and `--goal KIND` puts a goal of that kind at each
end (see the Walled box drill and
[match-engine.md](match-engine.md#training-pitches)).

`fm_lab session "NAME"` runs a training session headless (see
[match-engine.md](match-engine.md#training-sessions)) and prints each
exercise's zones' outcomes, each change (arrivals, time, the largest jump of
place and condition at the merge and the split) and the squad's condition;
`--zone-brain "ZONE=B"` and `--zone-body "ZONE=B"` set a zone's versions,
and `--carry-check` checks that a player's running state carries from one
engine to another exactly. `match_sandbox --zone-round-trip` saves zones of
the example session, loads and replays them, and says whether each replays
identically.

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
./out/build/profile/benchmarks/db_benchmarks
./out/build/profile/benchmarks/match_benchmarks
```

The `profile` preset enables machine-local native optimizations; use comparable
builds and hardware when reporting results. Benchmark target definitions are
in [benchmarks/CMakeLists.txt](../../benchmarks/CMakeLists.txt).

## Known baseline failures

Verification on 2026-10-07 reproduced these failures with the original engine
from revision `8b0aafb`, before the documentation/context-override changes:

| Test | Observed mismatch |
|------|-------------------|
| `MatchEngineTest.StrongerTeamHasStatisticalAdvantage` | 7.1875 goals/match exceeded the 5.5 calibration bound |
| `MatchEngineTest.ConditionCanBeCarriedBetweenMatches` | Both compared players finished at the 0.35 condition floor |
| `TacticsTest.InPossessionShapeMovesTheSlotWithTheBallOnly` | The observed in-possession width shift missed the expected threshold |

These are baseline observations, not permission to ignore new failures. If
you encounter one, compare your change with its base revision and include the
evidence in the PR. Timing tests may skip unless their opt-in environment is
enabled; a skipped case is not a test pass.
