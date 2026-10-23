# Developer guide

Start with [Architecture](../ARCHITECTURE.md) for the career, database,
controller and UI boundaries, then use these guides to work on the simulation:

New to the project? Follow the [contributor guide](../../CONTRIBUTING.md) for
your first change and [platform setup](../user/installation.md) for dependencies.

| Guide | What it explains |
|-------|------------------|
| [Match engine](match-engine.md) | State ownership, time and units, tick order, ball physics, rules, replay and scheduling |
| [Player behavior](player-behavior.md) | Attributes, team shape, roles, movement, action selection, defending and goalkeepers |
| [Extending and modding](extending-and-modding.md) | Working JSON edits, C++ extension points, custom match contexts and validation |
| [Design notes and Spec Kitty history](design-notes.md) | Recovered rationale, original source references and differences between plans and shipped code |
| [Builds](builds.md) | Configure/build/test presets and tooling |
| [Tests and tools](testing.md) | Focused tests, suite labels, balance lab and benchmarks |
| [Runtime editor](runtime-editor.md) | Schema discovery, page contexts, validation and preservation of custom data |
| [Website and documentation](website.md) | Maintain the showcase, media and rendered guides |
| [Release](release.md) | Packaging and runtime paths |

These pages describe the source in this repository. Historical specifications
are evidence of intent; a planned class or file is not an available API until
it exists in the code. The design notes preserve that distinction.

## Finding the right implementation

For a visible behavior, follow **input → decision → execution → observation**.
For example, a missed pass can originate in a poor target choice, kick error,
an interception, or a failed first touch. Changing the renderer or the displayed
completion estimate will not fix those mechanisms.

Use symbol searches rather than line numbers in the large engine file:

```sh
rg -n 'simulateStepBody|refreshTacticalTargets|decideAction|updateBall' src/model/match_engine.cpp
rg -n 'TEST\(MatchScenarioTest|TEST\(TacticsTest' test
```

Keep comments at the boundaries of substantial blocks: explain their purpose,
inputs/outputs, units, ordering constraints and the reason for a non-obvious
choice. Leave tuning numbers in their named tuning structs. When behavior
changes, update the relevant guide and its focused test in the same change.

## Building the API documentation

With Doxygen installed, run `doxygen Doxyfile` from the repository root, or
`cmake --build build --target docs` after configuring with Doxygen available.
The developer guides and source comments are included in `docs/html/index.html`.
Generated HTML is ignored; edit the Markdown and source comments instead.
