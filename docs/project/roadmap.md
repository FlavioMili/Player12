# Roadmap

Player12 has a playable career, live 2D/3D matches and direct player control.
This roadmap describes the gap between that game and the game we want to build.
It is not a release schedule. Priorities can change with playtesting and
contributor availability. The [feature tour](features.md) describes current
capabilities; the [changelog](../../CHANGELOG.md) records shipped changes.

## Start from what already works

Do not open tasks to add features that are already present:

- Navigation already has seven hubs, a searchable command palette, shortcuts
  and Back/Forward history. Onboarding already has a welcome tour, a first-week
  checklist and Help. Improve their effectiveness rather than replacing them.
- Six themes, text/interface scaling, colour-vision modes and rebindable keys
  already exist. Contrast checks are useful, but do not establish that every
  screen is readable or every journey works with a keyboard.
- Domestic leagues and cups, promotion/relegation, continental club competitions
  and national-team qualifiers/finals already run. The missing editor must expose
  supported rules safely, then extend the simulation where necessary.
- Preseason friendlies already let you change an opponent and home/away venue,
  book tours and training camps, or accept an assistant's suggestions. Current
  opponent changes swap existing fixtures within a preseason week; they are not
  invitations negotiated with another club.
- JSON user-made data already supports custom clubs, players and leagues.
  It is not an in-game editor or a versioned, shareable world-pack system.
- Play mode already has automatic restart release and pass-button release for
  the restart side. Do not describe all restart input as missing.
- National-team jobs and call-ups already exist. Their matches are simulated;
  watching or directing those matches live is a separate missing capability.

## First priority: make existing decisions easier

Work from observed player problems, not a list of new screens. Start with five
journeys: create a career, select a lineup, change tactics, negotiate a signing
and play the next match.

1. **Find the friction.** Have newcomers try those journeys using the existing
   tour, Help and command palette. Record where they get stuck, miss a decision
   or misunderstand its consequences. Turn each observation into a reproducible
   example with a small proposed fix.
2. **Fix the highest-impact obstacles.** Prioritise clipped content, unreadable
   charts, lost selection/context and unclear costs or validation messages.
   Reuse existing navigation and theme components; keep the managed club, date
   and pending action clear. Verify at 1280×720 and larger displays, with scaled
   text and both light and dark themes.
3. **Close interaction gaps.** Audit the same journeys with keyboard navigation
   and colour-vision modes. Make focus visible, expose status without colour
   alone, and make table sorting/filtering and related-record links consistent.
4. **Retest with players.** A finished improvement lets a newcomer complete the
   affected task and explain the decision without maintainer guidance. Include
   evidence of that outcome, not just a screenshot of the new layout.

The first contribution can be one documented usability problem or one corrected
screen. A wholesale interface rewrite is not a prerequisite.

## Release confidence and football quality

A playable download and trustworthy results are part of the player experience.

- **Finish the release pipeline.** Platform build/package workflows exist.
  Establish successful Linux, macOS and Windows release jobs, then test the
  packaged game on machines without development dependencies. For Windows,
  verify extraction and double-click startup, assets, save location and runtime
  dependencies. Keep a failed or untested preview distinct from a validated
  public release. macOS notarisation and wider architecture support are later
  distribution work, not prerequisites for improving gameplay.
- **Calibrate using current evidence.** The engine already implements shots,
  possession, offsides and goalkeeping. Investigate failing calibration cases
  against the current build; distinguish incorrect test assumptions from model
  defects. Validate both equal-strength and unequal-strength teams, complete
  seasons and more than one seed. Do not treat old conversion percentages as
  targets or fix failures by relaxing assertions without a football reason.
- **Preserve reproducibility.** Seeded matches and portable random-number
  generation exist, but whole careers are not guaranteed bit-identical across
  platforms. First document what can be reproduced and compare representative
  results across platforms. Pursue stricter numerical determinism only with
  clear compatibility and performance costs.
- **Extend match control deliberately.** Add a standalone practice match using
  existing Play mode. The headless `fm_lab` balance tool is not a player-facing
  practice screen. A first playable slice should launch without changing a
  career and use the same laws, attributes and fatigue as normal matches.
  Separately, design fuller set-piece interaction beyond the existing
  pass-button restart release: visible taker/target choices, aimed delivery or
  shots where supported, and penalty interaction. Specify the controls and
  validate each restart type before treating that broader feature as complete.
- **Close career gaps.** Add live national-team matchday through the existing
  match pipeline; introduce multiple nationalities and eligibility rules before
  exposing them in a world editor. Support country-specific season shapes for
  Brazil, Argentina and the United States instead of the current August–May
  assumption. Validate season rollover, transfers and international windows.

Audio refinements, club chants/commentary, Italian club articles and loan
appearance clauses remain smaller follow-up directions. They should not displace
release blockers or the highest-impact usability findings.

## In-game editing: safe changes before arbitrary worlds

The end goal is to create countries, clubs, players and competitions without
editing database files. Reach it through useful, testable stages.

### 1. Edit supported data for a new career

Build a browser/editor for the entities the loader already understands. Start
with players and clubs, then leagues. Expose fields supported by the game,
preview changes and validate IDs, required values and references before saving.
Add staff and venue editing when their creation/import lifecycle is supported;
those are not existing JSON pack capabilities to assume.

A first release should let a creator clone a small example, edit a club and its
players, and start a new career that contains them. Preserve the original data,
provide a recoverable draft and report errors against the relevant field.
Editing an active save is a separate feature with explicit compatibility rules.

### 2. Make creations shareable and recoverable

Add pack metadata, authorship/version information, export/import, dependency and
conflict reporting. Show which records will change before applying a pack. Test
that export/import preserves the edited world and an invalid pack cannot damage
an existing career. Do not promise that unrelated packs can always be combined.

### 3. Separate countries from today's league-pyramid convention

Currently a domestic competition's country is represented by its root league,
with linked divisions and a domestic cup. League economy/nationality profiles
also live in C++ tuning tables, and unknown player nationalities fall back to
English. Arbitrary countries and associations need an explicit model and migration plan before an editor can safely expose
flags, regions, nationalities, national teams and calendar rules.

First support one added country with a valid league pyramid and generated
national team. Verify generated players, nationality, club membership, season
rollover and existing-save compatibility. Then expand associations, continental
membership and national-team eligibility, including multiple nationalities.

### 4. Expose competition rules, then expand supported formats

Begin with editable versions of formats the game already runs: domestic leagues
and cups, continental phases and national-team qualifiers/finals. Make
participants, promotion/relegation and qualification routes explicit. Preserve
current worlds as a compatibility example.

Broader formats need reusable rules for stages, calendars, squad eligibility,
tie-breakers, extra time/penalties and prize money. Add one format at a time for
both clubs and nations. Before starting a career, preview fixtures and diagnose
missing participants, impossible qualification paths and scheduling conflicts.
Validate a complete season/tournament, the resulting qualifiers and the next
season, not only that a bracket can be drawn.

## Friendlies, invitations and summer competitions

Build on the existing preseason planner rather than adding basic friendlies
again. These can advance independently of the complete world editor.

1. **Club invitations.** Introduce a proposal with opponent, date, venue and
   terms, followed by acceptance, rejection or a counter-proposal. Pending
   invitations must not silently create fixtures. Start with one ordinary
   preseason friendly; enforce availability and recovery time, and keep
   cancellation and the confirmed calendar consistent.
2. **A match for a cause.** Extend an agreed friendly with a charity, memorial
   or community purpose and a stated allocation of in-game proceeds. Show the
   purpose in the fixture/report and account for proceeds once in the ledger.
   This is an in-game feature, not real-world fundraising.
3. **Summer tournaments.** Add a small invited-club tournament, initially one
   supported round-robin or knockout format. Reuse invitation status, calendar
   checks and existing match rules. Show confirmed participants and pending
   replies; handle a declined invitation or withdrawal before confirmation.
   Validate fixtures, travel/recovery, standings or the winner, and settlement
   of costs and receipts. More hosts and formats follow that working slice.

These proposals concern computer-controlled clubs in a career. Online
multiplayer would require its own design and is not implied.

## Community building: people and ownership

This is a parallel, nontechnical initiative, not a game-engine milestone.
Contribution guides, optional contributor credits and player cameos already
exist; the next step is helping people use them.

- Choose one community space with an identified organiser and moderation
  expectations. Define participation and credit rules before inviting people;
  avoid opening several unmaintained channels.
- Run a small, focused newcomer playtest or career-sharing activity. Publish
  what feedback is sought, welcome guides/translations/data/accessibility help,
  and report which observations led to a change.
- Help a new contributor complete a first contribution using the existing
  guides. Improve those guides where the newcomer gets stuck.
- Grow towards regular career/tactics showcases, custom-world sharing and
  community challenges when people volunteer to sustain them.

Progress means participants can find help, receive a response and see their
contributions acknowledged. A new platform or a feature ticket alone does not
establish a community.

## Turn the roadmap into a contribution

Use the [contribution guide](../../CONTRIBUTING.md) and
[existing issues](https://github.com/FlavioMili/FootballManagement/issues).
For a task, describe today's behaviour, the desired outcome, a bounded first
change, dependencies and how a player or test will demonstrate improvement.
Large editor features should start with a focused design discussion; community
activities need an organiser and a practical invitation rather than architecture.
