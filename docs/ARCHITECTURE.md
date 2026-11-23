# Architecture

Player12 is a C++23 desktop game built on SDL3, Dear ImGui
(drawn through `SDL_Renderer`) and SQLite. The code is split into a headless
core, a GUI layer on top of it, and a few tools and test executables that
link only what they need.

For implementation details and extension recipes, see the
[developer guide](development/README.md), especially the
[match engine](development/match-engine.md) and
[player behavior](development/player-behavior.md) guides. The
[design notes](development/design-notes.md) preserve rationale from the Spec
Kitty archive and identify proposals that differ from the current code.

```
                +-------------------------------------------------+
  GUI (fm_ui)   | GUIView: SDL window, main loop, scene stack      |
                |   ManagementScene shell: sidebar hubs, tabs,     |
                |     top bar, Continue, palette, shortcuts        |
                |   Scenes and dialogs  --  widgets / theme        |
                |   MatchScene --> MatchRenderSnapshot --> 2D / 3D |
                +------------------------+------------------------+
                                         | GameController API
                +------------------------v------------------------+
  Core          | GameController: the only entry point of the UI   |
  (fm_core)     +-------------------------------------------------+
                | Game: date, calendar, competitions, world,       |
                |   transfers, manager career, national teams,     |
                |   guidance, MatchScheduler (ThreadPool)          |
                |   WorldSimulation: economy, board, training,     |
                |     injuries, development, scouting, youth,      |
                |     interactions, stories, awards, records ...   |
                |   MatchEngine (real-time, headless)              |
                +-------------------------------------------------+
                | GameData: in-memory players, teams, leagues      |
                | Repositories + Migrations + SaveManager          |
                | SQLite working database (in memory) --> slot file|
                +-------------------------------------------------+
  Tools         fm_lab (links fm_core only)
```

## Build targets

| Target | Contents |
|--------|----------|
| `fm_core` | Everything headless: `src/model`, `src/database`, `src/controller`, `src/global`. No window, no ImGui. |
| `fm_ui` | `src/gui`: the view, scenes, widgets, theme and match renderers. Links `fm_core`. |
| `FootballManagement` (outputs `Player12`) | `src/main.cpp`: creates a `GameController` and a `GUIView`, runs the loop, saves a loaded career on quit. `--profile-match` times the match renderer on the first existing save. |
| `fm_lab` | `src/tools`: the headless balance lab (see below). |

## Core model

**Game** (`src/model/game.*`) owns one career: the current date, the
`Calendar`, the `CompetitionManager`, the `WorldSimulation`, the
`TransferMarket`, the `ManagerCareer`, `NationalTeams`, the `CareerGuidance`
state and the `MatchScheduler`. `Game::advanceDay()` is the daily tick:

1. move the date on and run the world's daily systems
   (`WorldSimulation::onDayAdvanced`);
2. run the manager-career day (vacancies, sackings, offers);
3. play national-team matches in international windows;
4. on 1 July, run the season transition instead of the steps below;
5. simulate the day's fixtures in a parallel batch and apply the results
   (reports, match consequences, world reactions, career records,
   competition tables);
6. let competitions react (`afterMatchday`: draws, knockout ties, continental
   phases) and the transfer market run its day;
7. keep the managed lineup eligible if the assistant is allowed to.

**WorldSimulation** (`src/model/world_simulation.*`) holds the systems that
change every club every day: club economy and finances, board state and
objectives, training and load, injuries and recovery, morale and form,
player development, squad status, scouting, the youth academy, interactions
and stories, awards, records, facility projects, pre-season, mentoring, the
inbox and holiday preferences. Each system is a class with its own state,
daily or event hooks (`onDayAdvanced`, `onMatchPlayed`, `onTransferCompleted`,
`onSeasonStart`) and its own `load`/`save`.

**Competitions** (`competition.*`, `competition_manager.*`, `standings.*`,
`continental.*`, `national_teams.*`, `calendar.*`) cover domestic leagues with
promotion and relegation, cups, the continental club competitions (Swiss
draw, league phase, knockouts, coefficients) and national teams. The season
calendar is data-driven: international windows, continental weeks and the
winter break are generated per season.

**Transfers** (`transfer_market.*`, `transfer_negotiation.*`,
`transfer_listing.*`) run AI club business and the manager's negotiations.
Pure valuation and negotiation rules live in `TransferNegotiation`; tuning
constants in `transfer_tuning.h`.

**Randomness** is always seeded: `WorldRng` (`world_rng.h`, xoshiro256**
with its own portable distributions) derives independent streams per domain
(generation, injuries, development, youth intake, transfers, scouting,
managers, ...) from the world seed. The match engine separately uses seeded
play/incident streams. Reproducibility also depends on inputs, engine version
and fidelity; portable RNG alone does not guarantee whole-career identity
across builds/platforms. `FM_WORLD_SEED` and `FM_MATCH_SEED` override the
seeds for tests and bug reports.

**Tuning** constants are kept out of the logic in `match_tuning.h`,
`world_tuning.h` (league economy profiles, reserves, calibration) and
`transfer_tuning.h`.

## Match engine and scheduling

`MatchEngine` (`match_engine.*`, `match_rules.*`, `match_tuning.h`) is a
headless real-time simulation: a 10 Hz fixed step for players, the ball in
four sub-steps with swept contacts, metric kinematics, fatigue and the laws
of the game. It supports playback speeds and highlight windows for the live
view, `simulateToEnd()` for instant results, and a controlled-player input
seam with a replayable input log. Match statistics and reports are built from
the engine's events (`match_events.h`, `match_report.*`).

`MatchContext` (`match_context.h`) carries finishing, refereeing and home-edge
settings. The scheduler normally derives it from the league; tools can provide
`MatchSimulationInput::context_override` for one fixture. These settings are
validated before stepping, not read from mutable globals during a match.

Background fixtures go through `MatchScheduler` (`match_scheduler.*`).
`Game` captures a `MatchSimulationInput` per fixture on the owning thread
(lineups, strategies, seed, knockout resolution), the scheduler runs them on
a `ThreadPool` (`src/global/thread_pool.*`) and returns results in input
order. A club plays once per batch and each match depends only on its input,
so results are identical for any thread count. The pool's workers run at a
lower priority than the caller, the calling thread takes part in its own
batch, and `FM_SIM_THREADS` caps the thread count (default: hardware threads
minus one, at most 8).

The managed match is not scheduled. `MatchScene` owns its own `MatchEngine`
with the same inputs, advances it every frame and hands the finished engine
to `GameController::setMatchResult()`, which applies it exactly like a
background result.

## Persistence

- **Schema and queries**: `assets/db/schema.sql` (all
  `CREATE TABLE IF NOT EXISTS`) and `assets/db/queries.sql`, indexed by
  `src/global/queries.h`.
- **Repositories** (`src/database/repositories/`) are the only code that runs
  SQL: one per aggregate (players, teams, leagues, fixtures, competitions,
  finances, transfers, scouting, staff, training, inbox, world and game
  state). They use prepared statements and write only changed rows where
  possible.
- **GameData** (`src/database/gamedata.*`) keeps players, teams and leagues
  in memory for the whole session. New worlds are built by `DataGenerator`
  and `WorldGeneration` from the installed world database
  (`src/database/world/`): the built-in `assets/world/default.fmdb` or a
  world the player chose, which the save keeps a copy of (`WorldDocument`).
- **Working copy and crash-safe saves**: a career is played on an in-memory
  SQLite database. `SaveManager` (`src/database/save_manager.*`) writes a slot
  only through a verified snapshot: online backup to `<slot>.tmp`,
  `quick_check`, `fsync`, rotation of numbered backups, atomic rename and a
  directory `fsync`. Loading checks integrity first and refuses damaged or
  newer saves without touching them. Autosave runs at the end of a simulated
  day on the Continue worker.
- **Migrations** (`src/database/migrations/`): numbered, idempotent upgrades
  recorded in `schema_migrations` and `save_meta`; new tables come from
  re-running `schema.sql`. The full protocol is in
  `src/database/migrations/README.md`.
- **Runtime paths** (`src/global/runtime_paths.*`): read-only data is found
  next to the installed executable (or in the source checkout for
  development builds, `FM_ASSET_ROOT` to override); saves, settings, logs,
  `imgui.ini` and captures go to `SDL_GetPrefPath`. See
  [development/release.md](development/release.md).

## Controller

`GameController` (`src/controller/`, split into `game_controller.cpp`,
`game_controller_squad.cpp` and `game_controller_guidance.cpp`) is the API
the GUI and the tools use. It creates and loads careers, exposes read models
for every screen (squad, finances, scouting estimates, youth, manager career,
next actions, ...), validates and performs actions (offers, contracts,
training, delegation, talks), and advances time (`advanceDay`,
`advanceToNextManagedFixture`, `goOnHoliday`, `advanceWhileUnemployed`)
with progress counters the UI can poll. Players at other clubs are only
exposed through scouted estimates.

## GUI

- **GUIView** (`src/gui/gui_view.*`) owns the SDL window and renderer, the
  ImGui context and the frame loop. It keeps a base scene plus a stack of
  overlays, handles HiDPI (ImGui is drawn with the render scale set to the
  framebuffer scale, see `render_scale.h`), screenshots (F12) and appearance
  settings.
- **Shell**: `ManagementScene` is the base of every in-career screen. It
  draws the sidebar with seven hubs (Home, Inbox, Squad, Training, Matches,
  Recruitment, Club) whose screens appear as tabs, the top bar with the date,
  balance, save status, holiday and Continue buttons, the Ctrl+K palette and
  the keyboard shortcuts. `Navigation` opens sections, player profiles, match
  reports and club pages.
- **Career hub**: `MainGameScene` is the base scene of a career (Home and
  Finances) and owns the clock. Continue runs on a `std::async` worker behind
  a progress overlay; screens above the hub are closed first so nothing reads
  the game state while it changes.
- **Scenes and dialogs** (`src/gui/scenes/`): one class per screen, plus
  modal dialogs (talks, set pieces, holiday, mentoring). View models in
  `src/gui/view_models/` format domain data for display.
- **Widgets and theme** (`src/gui/widgets/`): the design system (buttons,
  money input, segmented controls, responsive tables that drop columns
  instead of scrolling sideways, cards, badges), number and money formatting,
  icons and the theme presets with spacing and scale.
- **Match view**: `MatchScene` builds a read-only `MatchRenderSnapshot`
  (previous and current fixed-step positions plus an interpolation factor)
  from the engine every frame and passes it to an `IMatchRenderer`:
  `MatchRenderer2D` (top-down pitch) or `MatchRenderer3D` (CPU perspective
  projection drawn as ImDrawList triangles, with six camera modes in
  `MatchCamera3D`). Renderers never touch the engine or its RNG, so switching
  views cannot change the match.
  The current snapshot copies positions but still borrows player, event and
  statistics pointers; it is consumed while the live engine is stable, not
  published as an independently owned object across threads.

## Balance lab

`fm_lab` runs large batches without a window: `matches` (synthetic squads,
fixed or spread ratings), `season` (a whole AI-only world through
`GameController` in a scratch directory) and `tactics` (round robin of the
tactic presets). It compares the results with target bands and writes
`report.md` and `metrics.json`; the exit code tells whether every gate
target passed.

## Tests

GoogleTest suites in `test/`, discovered by CTest with labels:

| Label | Executables | Contents |
|-------|-------------|----------|
| `unit` | `unit_tests`, plus the GUI and lab suites | Model, persistence and view-model tests |
| `core` | `core_unit_tests` (prefix `core::`) | The headless tests linked against `fm_core` only; also `leak-check` |
| `gui` | `scouting_ui_tests`, `youth_ui_tests`, `squad_ui_tests`, `manager_ui_tests`, `navigation_ui_tests`, `guidance_ui_tests`, `career_ui_tests` | Screens driven through a real `GUIView` with the dummy video driver, with captures at several sizes and scales |
| `lab` | `lab_tests` | Balance lab reports |
| `playtest`, `slow` | `playtest_tests` | A scripted career journey through the real UI |
| `monkey`, `slow` | `monkey_tests` | Seeded random input with invariant checks, and a sweep that every enabled widget has an effect |
| `adversarial`, `slow` | `adversarial_tests` | Hostile scenarios: input during Continue, old saves, resizing mid-match, and so on |
| `sanitizer` | `sanitizer_probe` | Checks that the sanitizer build really instruments the code |

Every test runs with fixed world and match seeds, two simulation threads and
a scratch data directory, so it never touches real saves. CI runs everything
except `playtest|monkey|slow`.
