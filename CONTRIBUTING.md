# Contributing to Player12

Your first contribution can be a small one: a clearer tooltip, a translation,
a reproducible bug report, a documentation correction or a focused code fix.
You do not need to understand the entire game to help.

## Choose a starting point

| Contribution | Where to begin |
|--------------|----------------|
| Report a bug or share playtest feedback | [Open an issue](https://github.com/FlavioMili/FootballManagement/issues/new/choose) with steps, expected behavior and what happened |
| Suggest a feature | Describe the player problem and an example of the experience you want in a [feature request](https://github.com/FlavioMili/FootballManagement/issues/new/choose) |
| Improve documentation | Edit the relevant guide in `docs/`; preview the Markdown and check its links |
| Translate or improve wording | Edit matching keys in `assets/lang/English.json` and `Italian.json` or your first language; preserve placeholders |
| Add game data or your player cameo | Read the [data reference](assets/user_made_data/README.md) and [cameo walkthrough](docs/contributing/player-cameo.md) |
| Fix code | Use the [developer guide](docs/development/README.md) to find the subsystem and its tests |

Browse [open issues](https://github.com/FlavioMili/FootballManagement/issues),
including [good first issues](https://github.com/FlavioMili/FootballManagement/labels/good%20first%20issue)
when available. Leave a comment if you are taking an issue so others can
coordinate. Small fixes can go straight to a pull request; discuss larger
features or architectural changes in an issue before investing in them.

## Your first pull request

1. Fork the repository on GitHub. For a Markdown-only change, you can use
   GitHub's file editor and open a pull request without a local build.
2. For local work, clone your fork and create a branch:

   ```sh
   git clone https://github.com/YOUR_USERNAME/FootballManagement.git
   cd FootballManagement
   git switch -c improve-player-docs
   ```

3. Make one focused change. Explain the behavior or wording it improves;
   include an issue link if there is one.
4. Run the checks relevant to your change (below). You can ask for feedback
   early by opening a draft pull request.
5. Optionally add your name to [CONTRIBUTORS.md](CONTRIBUTORS.md), and/or add a
   [player cameo](docs/contributing/player-cameo.md) at your favorite club.
6. Commit and push your branch, then open a pull request against `main`.
   Fill in the template with what changed and how you checked it. Respond to
   review feedback by pushing further commits to the same branch.

## Set up for code changes

On Linux, install the [platform prerequisites](docs/user/installation.md),
then use the test-enabled preset from the source root:

```sh
cmake --preset release-tests
cmake --build --preset release-tests --parallel 4
./build/release-tests/src/Player12
```

On Linux with GCC 14, prefix the configure step with
`CC=gcc-14 CXX=g++-14`. macOS needs the compiler/linker setup described in its
[installation guide](docs/user/installation.md#macos); select
`-DBUILD_TESTING=ON` there. Windows builds are experimental and the current
test suites require POSIX APIs, so use Linux/macOS for test development.

The [build guide](docs/development/builds.md) explains sanitizers, profiling,
dependency configuration and linting. The
[architecture map](docs/ARCHITECTURE.md) explains how the core, GUI and
database fit together.

## Check your change

| Change | Useful verification |
|--------|---------------------|
| Markdown | Preview it, check local links, and confirm commands match the current API/build presets |
| Translations | Parse the JSON, preserve formatting placeholders, and inspect the changed screen |
| Player/game data | Parse JSON, check unique IDs and team references, then start a new test career |
| Model or rules | Build the affected target and run its focused tests; add a regression for a fixed behavior |
| GUI | Check the affected screen and relevant GUI tests, including layout at different sizes |
| Match behavior or tuning | Use scenarios, replay/determinism tests and the balance lab as described in the [engine guide](docs/development/match-engine.md) |

For example, with the test preset configured and built:

```sh
# Find tests for the area you changed.
ctest --preset release-tests -N -R 'MatchScenarioTest'
ctest --preset release-tests -R '^core::MatchScenarioTest\.'

# CI-style suite, excluding long playtests and GUI monkey runs.
ctest --preset release-tests -LE 'playtest|monkey|slow'
```

See [Tests and tools](docs/development/testing.md) for the full suite, labels,
lab commands and known baseline failures. Report the exact checks you ran and
any failures; compare a suspected existing failure on the base revision rather
than weakening the test. Documentation-only changes do not require building
every game target.

## Code and documentation conventions

Use C++23, two-space indentation, Allman braces and the checked-in
`.clang-format`. Format changed C++ files or ranges and check that they add no
new `.clang-tidy` findings. Keep comments about substantial blocks explicit:
purpose, units, inputs/outputs and important ordering constraints.

```sh
clang-format -i src/path/to/file.cpp src/path/to/file.h
```

Keep simulation behavior in the headless core and presentation in the GUI.
When changing an implementation contract, update its guide and focused
coverage. For code that changes performance, measure the affected workload
and include the results: the [testing guide](docs/development/testing.md)
lists benchmarks and balance tools.

## Names, credit and licence

[CONTRIBUTORS.md](CONTRIBUTORS.md) is the human-readable contributor list;
Git history also records authorship. Add the name or handle you want displayed,
with an optional public profile link. Documentation, translations, data and
testing count as contributions too.

The default world uses fictional footballers. As an optional contributor
credit, you may submit your own name or nickname for a fictional player in
your favorite existing club. The [cameo guide](docs/contributing/player-cameo.md)
explains the required fields. Use your own chosen name; keep professional
players, copied kits and real club badges out of the default data.

Contributions to the game's source are made under the project's
[GNU GPL v3 licence](LICENSE). Identify the source and licence of any
third-party material you propose to add. AI-assisted contributions are welcome:
explain the assistance in your PR, review the result and follow the
[AI guidelines](AI_GUIDELINES.md).

Please follow the [code of conduct](CODE_OF_CONDUCT.md) in issues and reviews.
