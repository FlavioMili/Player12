# Tests and tools

Configure with tests (`release-tests` preset or `-DBUILD_TESTING=ON`):

Install the [platform prerequisites](../user/installation.md) first. If you
are already using a build directory such as `build`, use `ctest --test-dir build`
instead of the preset; a preset always targets its own `build/` directory.

```sh
cmake --preset release-tests
cmake --build --preset release-tests --parallel 4

# The default lanes (unit + scenario); this is also what CI runs
ctest --preset release-tests
```

The default presets run the fast lanes; the slow lanes (fuzz, soak, monkey,
playtest, adversarial, QA hunts, perf budgets) are excluded by them
(`slow|fuzz`) and selected by label:

```sh
ctest --preset release-tests            # unit + scenario
ctest --test-dir build -L fuzz          # the fuzz lane
ctest --test-dir build -L slow          # every slow lane
```

Tests run headless (SDL's dummy video driver) with fixed seeds and a scratch
data directory under `/tmp/football-management-tests`, so they never touch
your saves.

## Lanes, labels and shards

Every test runs once. Each executable is compiled from distinct files
(`core_unit_tests` holds the headless model tests, `unit_tests` the files
that need the GUI library).

The default lanes do **not** register one CTest entry per GoogleTest case.
`core_unit_tests` runs as 4 CTest entries (`shard0of4::core_unit_tests`, ...)
and `unit_tests` as 2, each a single process running its share of the cases
(`GTEST_TOTAL_SHARDS` / `GTEST_SHARD_INDEX`). A process builds or loads the
shared worlds and checkpoints once and every test gets its own copy. The other
GUI executables hold a handful of tests and keep one entry per case.

| Label | Contents |
|-------|----------|
| `unit` | The default lane: the shards and the per-case GUI executables |
| `slow` | Opt-in lanes, never part of the default run: `fuzz`, `soak`, `monkey`, `playtest`, `adversarial`, `qa`, `perf` (run with `-L fuzz` or the `fuzz` test preset) |

Further labels select areas: `core` (headless), `gui`, `lab`, `audio`,
`editor`, and `leak-check` (see below). `test/test_budgets.txt` lists the
tests that are kept out of the shards and run one by one in the `fuzz` lane
(with their own timeout); every line there is a test waiting to be made faster
or replaced by targeted tests, so when you speed one up, delete its line.

To debug one test, configure with `-DFM_TEST_PER_CASE=ON`: every case is then
registered separately (`ctest -R 'core::Holiday.Sacking'`). Without it, run the
executable directly: `build/test/core_unit_tests --gtest_filter=Holiday.*`.

Tests share a process, so they must not leak global state. `fm_test_support`
installs a listener in every executable that fingerprints the installed world,
its league profiles and the nationality registry before and after each test:
a test that leaves another world installed fails and the built-in world is put
back (call `World::install(nullptr)`).

Debug and sanitizer builds are several times slower: configure with
`-DFM_TEST_TIMEOUT_SCALE=10` (the `debug-sanitized` preset does) to scale
every timeout.

The leak-check lane is the `core` label run under the sanitizer build
(`ctest --preset debug-sanitized -L core`, as in CI); it is the same
executable as the normal lane, not a second copy of the tests, so the
default Release run does not repeat it.

## Shared fixtures (`test/support/test_worlds.h`)

Generating a world or simulating weeks of a career is the main cost of the
suite, so do not do it per test. Link is automatic (`fm_test_support`);
include `"test_worlds.h"`:

```cpp
// A copy of the generated world (GameData + in-memory connection).
auto world = TestWorlds::defaultWorld();

// A started career, club chosen, first day: independent of other tests.
const int slot = TestWorlds::uniqueSlot();
auto controller = TestWorlds::newCareer(slot);

// A career at a date or at its n-th managed fixture.
auto at = TestWorlds::careerAt(slot, GameDateValue(2025, 8, 20));
auto match = TestWorlds::careerAtManagedFixture(slot, 3);

// Any other state: the first caller runs `prepare`, everyone else loads it.
auto custom = TestWorlds::career(slot, "my-state-key",
                                 [](GameController& c) { /* build state */ });
```

How it works: the first process to need a state builds it (one real
simulation) and stores a checkpoint save in a `fm-test-fixtures` folder next to the test executable (the build tree), keyed
by the test executable's size and timestamp; later processes (a sharded process, or a
per-case test process) copy the file into their own save slot and load it.
A rebuilt executable never sees an older build's checkpoints. The calendar is
deliberately not jumped: the model advances players, finances, the market and
competitions day by day, so a state reached by a shortcut could never occur
in a game. `defaultWorld()` also keeps a per-process template and clones it
with a SQLite backup into a new in-memory connection.

Rules: give a different `key` to every different `prepare`; remove your slot
with `RuntimePaths::removeSave(slot)` when done; keep the same seeds and the
same assertions as the long version of the test.

## Run one area first

```sh
ctest --preset release-tests -N -R 'MatchScenarioTest'
ctest --preset release-tests -R '^core::MatchScenarioTest\.'
```

CTest lists each GoogleTest case separately. Core cases have a `core::` prefix;
the `unit_tests` cases (engine, Play, view models, game flow) have no prefix. Use `-N` to confirm that a filter selects actual tests
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

The ten-seed headless match unit test compares each completed match with the
same seed advanced one fixed step at a time. It checks identical simulation-step
counts, final debug snapshots and event types/player IDs, without an elapsed-time
threshold or estimated match-duration range. Use the opt-in match benchmarks on
comparable builds and hardware to measure performance changes.

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
