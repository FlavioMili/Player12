# Extending and modding

World content (countries, competitions, clubs, players, kits, league
economies and name pools) is data: a world database (`.fmdb`) built in the
game's [World editor](../user/world-editor.md) and described in the
[format reference](../../assets/world/README.md). Players share worlds as
files; the game imports them by file picker or drag and drop and never
downloads anything. Attribute names, rating weights and translations are JSON
assets; simulation behaviour is C++. There is no script or plugin loader and
no runtime JSON loader for `MatchTuning`.

## Edit data without rebuilding

| Data | Entry point | Effect and limits |
|------|-------------|-------------------|
| Countries, cities, travel, leagues and their economy/match profiles, clubs, players, name pools | World editor, or a `.fmdb` file ([reference](../../assets/world/README.md)) | New careers only; each save keeps the world it was created from. Unknown keys are preserved for mods. Travel and population have no gameplay effect yet |
| The built-in world | `assets/world/default.fmdb` | Same format; edit it in the World editor and **Export…** over the file |
| Attribute names and overall-rating weights | `assets/config/stats_config.json` | Generation and role-based overall ratings; new names do not automatically create AI behavior |
| Translations | `assets/lang/` | Display text for existing keys; not a way to define a new tactical role |

Worlds combine: the editor renumbers the ids of a world loaded into another,
merges identical entities and asks the player to resolve entities defined
differently (see the reference). Nationalities are the game's 159 built-in
demonyms; a fantasy country chooses the closest one.

To test the game against a complete copy of the assets (for example a changed
`stats_config.json`), run from the repository root:

```sh
mkdir -p /tmp/fm-custom-data
cp -R assets /tmp/fm-custom-data/
# Edit /tmp/fm-custom-data/assets/...
FM_ASSET_ROOT=/tmp/fm-custom-data FM_RUNTIME_ROOT=/tmp/fm-custom-career \
  FM_WORLD_SEED=424242 ./build/src/Player12
```

`FM_ASSET_ROOT` is the directory **containing `assets/`**, not `assets/` itself.
It is resolved once per process and is not an overlay: copy all assets.
`FM_RUNTIME_ROOT` isolates test saves, settings and imported worlds from your
normal career.

## Per-match settings for C++ tools

[MatchContext](../../src/model/match_context.h) is a value copied into each
engine before play. It configures finishing precision, referee
strictness/spread and home advantage. For a live or scenario engine, call
`engine.setMatchContext(context)` before its first step.

For a prepared headless fixture, `MatchSimulationInput::context_override`
provides the same settings through the scheduler. This example is a complete
helper; the caller supplies lineups with living Player objects and StatsConfig:

```cpp
#include "model/match_scheduler.h"

MatchSimulationResult runAtNeutralVenue(MatchSimulationInput input,
                                        const StatsConfig& config)
{
  // Start with the fixture's league settings and change just the venue.
  MatchContext context = MatchSimulation::leagueContext(input.league_id);
  context.homeAdvantageScale = 0.0f;
  input.context_override = context;
  return MatchSimulation::run(input, config);
}
```

| Override | Resolution |
|----------|------------|
| Unset (`std::nullopt`) | Derive from `league_id`; 0 or unknown league gives calibrated defaults |
| `MatchContext{}` | Explicitly use all calibrated defaults, even with a known league ID |
| Custom value | Replace the entire league context; derive/copy the league context first for a partial edit |

The engine validates the result once before play:

| Field | Accepted finite range | Meaning |
|-------|-----------------------|---------|
| `goalRateScale` | 0.5–1.5 | Shot precision factor; not a target score or guaranteed goals multiplier |
| `refereeStrictnessMean` | 0.5–2.0 | Centre of the referee strictness draw |
| `refereeStrictnessSd` | 0–0.5 | Spread of that draw |
| `homeAdvantageScale` | 0–2.0 | Home attribute/execution/referee edge; 0 for neutral |

Out-of-range finite values clamp; non-finite values fall back to the field's
default. Calls after the first step are ignored. The setting is captured by
value, so simultaneous fixtures cannot overwrite each other's context. It is
not persisted, exposed as a new `fm_lab` flag, or loaded from a JSON file.
If adding a content loader later, resolve and validate settings before workers
start, carry them in match inputs, and define save/replay versioning explicitly.

## Choose the smallest behavior extension

| Desired change | Implement at | Keep intact |
|----------------|--------------|-------------|
| Change a role's preferences | `Tactics::profile()` | Shared action/movement model and profile units |
| New tactical role | `TacticalRole`, family tables, keys, profiles and fit attributes | Persisted enum codes and fallback behavior |
| New off-ball movement | `refreshTacticalTargets()` | Shared planning before body integration, intention labels and role coordination |
| New on-ball choice | `decideAction()` plus execution helper | Eligibility, comparable utility units, diagnostics and replay determinism |
| New law calculation | `MatchRules` then engine application | Pure inputs/outputs, explicit random rolls, restart/event bookkeeping |
| Different physical constants | Named groups in `MatchTuning` | Units, full/control path consistency and calibration |
| New league | World editor or `.fmdb` data (`profile` optional: derived from the country) | Unique IDs, parent links, an even club count |
| New report statistic | Match stats/tracker, report serialization and UI view model | Single source of accounting and save compatibility |

For a role, append before `COUNT` rather than reordering persisted values.
Update `rolesFor`, the language key/description tables, English and Italian
translations, `keyAttributes`/fit as needed, and persistence round-trip coverage
in [test_tactics.cpp](../../test/test_tactics.cpp). Test what the player actually
does, such as an attacking full-back reaching a higher position. A test that
only compares the profile to the constants used to construct it misses wiring
errors.

For a new action, separate candidate evaluation from execution. A candidate
search should not consume random draws merely because a debug view asks for
scores. Keep flight and possession updates in their common engine paths. Add
its semantic intent/event/report handling where needed so both renderers and
analytics can explain it. Read the
[player decision pipeline](player-behavior.md) before changing utilities.

Use the existing value-type profiles before introducing callbacks or global
mutable registries. The engine must remain copyable for highlights, and a
scheduler worker must depend only on captured inputs. For larger extraction,
move one subsystem at a time with seeded comparisons before changing behavior.
The historical plan's layered `src/model/match/` tree does not exist yet.

## Validate a behavior change

1. Build the affected core/tools and run a focused scenario or rule regression.
2. Check seeded command/input replay when changing order, state or RNG use;
   check sequential versus parallel results when changing captured inputs.
3. Compare lab reports before and after a tuning/behavior change using the same
   seed range, rating distribution, match count and fidelity. A handful of
   plausible matches is not a balance measurement.

```sh
./build/src/fm_lab matches --n 1000 --spread --seed 424242 --threads 4 \
  --fidelity full --out-dir /tmp/fm-balance-full
./build/src/fm_lab matches --n 1000 --spread --seed 424242 --threads 4 \
  --fidelity background --out-dir /tmp/fm-balance-background
./build/src/fm_lab tactics --n 40 --seed 424242 --threads 4 \
  --out-dir /tmp/fm-balance-tactics
```

Inspect `report.md` and `metrics.json` against the compiled target table in
[lab_report.cpp](../../src/tools/lab_report.cpp). Preserve baseline output in a
different directory when testing an edit. Full/background comparisons check distributions;
same-fidelity replay checks can require exact results. If changing hot loops,
also measure performance rather than inferring speed from the code's shape.
