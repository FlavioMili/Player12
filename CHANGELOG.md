# Changelog

All notable changes to Player12 are listed here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and version
numbers follow [Semantic Versioning](https://semver.org/).

## v1.0.0 (unreleased)

First public release. Everything below is new compared to the earlier
development snapshots, so most of it is listed under *Added*.

### Added

**Management shell**
- Sidebar with seven hubs (Home, Inbox, Squad, Training, Matches,
  Recruitment, Club); the open hub lists its screens with counters (a hover
  menu when the sidebar is collapsed), F1-F7 shortcuts, and a command
  palette (Ctrl+K) that finds players, clubs, screens and pending tasks.
- Browser-style Back and Forward between screens: two-finger touchpad
  swipe, mouse side buttons, Alt+Left/Alt+Right and top-bar arrows.
- Home dashboard with the next fixture, a "Next steps" card and a Continue
  button that advances the calendar on a background worker with a progress
  overlay. A Stop button ends a long Continue at the end of the current
  day, and Continue stops on its own for a match, a decision or news that
  needs you.
- Holiday mode: your assistant manager runs the club until a date, the next
  match, the next decision or the end of the transfer window, with
  configurable reasons to come back early and a "Welcome back" summary.
- Inbox split into Decisions and Information, with inline actions (accept,
  reject, sign, release, reply), weekly medical digests and routine news filed
  as read. Filters by player or club, and players you follow, are kept
  between sessions.
- Decision moments in the inbox: a player asking for compassionate leave, a
  homesick player, a disputed fine, press questions about a rival, a sponsor event,
  a training-ground clash, a player who wants to start his coaching badges
  and supporters protesting about ticket prices. Each choice has
  consequences for the player, the dressing room, the finances or the fans.
- News hub (Inbox hub): transfers, managers, title races, upsets, records
  and awards from around the world, filtered by country and competition.
- Draw ceremonies for the domestic cups and the continental competitions,
  revealed tie by tie.
- Career timeline (Club hub): every club, trophy, record and key signing of
  your career, with an export of the journal as a Markdown file.
- Delegation screen: hand lineup fixes, substitutions, training, contract
  renewals, scouting assignments, friendlies and youth contracts to the
  assistant, with three presets. Changing a delegated item by hand takes it
  back, with an undo notice.
- Onboarding: a welcome tour for new careers, a first-week checklist that
  can be brought back after you hide it, screen tips, and a Help screen
  (F8 or the palette) with getting-started notes, the keyboard shortcuts as
  currently bound and a glossary of football terms.
- Rebindable shortcuts: every action has a main and a second key that you
  can change in Settings > Controls, with conflict warnings and reset to
  defaults; screens and help texts show the keys as bound.
- Full Italian localization: every screen, the inbox, the match commentary
  and the club names with their Italian articles, plurals and dates, with
  English as a per-key fallback.
- Locale-aware numbers and money: amounts are shown in the language's
  format and money fields accept either decimal mark and the usual units
  (`1.5M`, `850k`, `1.500.000`, `2,25 mln`, `850 mila`), refusing ambiguous
  input instead of guessing.
- Settings for language, resolution, fullscreen, VSync, frame cap, six
  themes, accent colour (club colour or custom), interface scale (automatic
  or manual), text size, table density, reduced motion, autosave frequency
  and backups.
- Accessibility: colour-vision modes (red-green safe and blue-yellow safe)
  for good and bad news, ratings and warnings, and text contrast that meets
  WCAG AA in every theme.
- Responsive layout that works from 1280x720 up to 4K and at HiDPI scale 2;
  tables adapt their columns instead of scrolling sideways, and a column
  picker on the standings, squad and scouting tables lets you choose what
  they show.
- Two-colour club badges and club identity (nickname, founding year,
  stadium) across the UI; persistent squad numbers (1-99) that you can
  change from the squad screen.
- About screen with the version, the build and the licences of the bundled
  components.

**Match engine and match day**
- Real-time match engine: one simulated second per real second at 1x, a
  10 Hz fixed step with a 40 Hz ball, player kinematics (acceleration,
  braking, fatigue), ball flight with drag, bounce and roll, goalkeeper
  positioning, dribbling, tackles, marking and pressing.
- Laws of the game: offside, fouls with advantage, yellow and red cards
  (including second yellows) with per-referee strictness, free kicks,
  penalties, corners, throw-ins, goal kicks, added time from recorded
  stoppages, and five substitutions in three windows (half-time is free)
  from a bench of up to nine.
- Background fixtures use the same engine at a cheaper step, so watched and
  unwatched matches follow the same model.
- Play mode: take control of your team on the pitch in your club's live
  match, before kick-off or at any moment while watching. You control one
  footballer at a time (keyboard or gamepad: move, sprint, pass, shot with
  power, through ball, lofted pass or cross, tackles, jockey, switch
  player), with the same rules, attributes and fatigue as a watched match;
  the AI keeps the other players, the goalkeepers and the restarts. Player
  switching and pass assistance have three levels each, and you can hand
  the team back to the AI at any stoppage.
- Live match screen with 2D and 3D views of the same simulation, playback
  speeds from 1x to 30x, a Highlights mode, pitch focus, key-moment event
  feed with a timeline, statistics, substitutions and a Quick result button.
- 3D view with six cameras (broadcast, tactical, end, player follow, free
  and a TV director that cuts to goal close-ups, goal-line and reverse
  angles), mouse orbit, pan and zoom and a follow-ball option. Players are
  animated with a procedural skeleton: planted feet, running gaits, the
  foot on the ball when striking it, headers, tackles, throw-ins and a
  goalkeeper holding the ball. Afternoon kick-offs are played in a day-lit
  stadium with roof shadows, and the crowd rises for chances and goals.
- 2D tactical view with a mown pitch, stands, nets, shirt numbers, facing,
  ball height and trail, pass and shot paths, offside flashes and an
  optional pressure map.
- In-match changes: a substitutions board (drag a substitute onto a player,
  several changes made together at the next stoppage, position swaps and
  assistant suggestions), a tactics panel (style presets, sliders and
  formation presets with their familiarity cost, undo) and eleven touchline
  shouts (Shift + number keys) whose effect fades when repeated.
- Your match pauses at half-time and the other breaks at any speed
  (optional), and Highlights mode opens each highlight on live play.
- Pre-match and half-time team talks. How the squad takes a talk sharpens
  (or unsettles) its decisions, execution and pressing for the first minutes
  of the half, in watched and simulated matches.
- Player roles and duties per position (line or sweeper keeper, inside
  full-back, stopper, cover defender, anchor, playmaker, box-to-box, second
  striker, inside forward, touchline winger, target forward, poacher,
  pressing forward, false nine) with Defend, Support or Attack duties. Each
  role changes how the player positions himself with and without the ball,
  how often he runs in behind, how much width he keeps, how riskily he
  passes, when he presses and how readily he shoots. AI clubs give their
  players the roles that suit them.
- A separate shape with the ball: drag each player to his attacking-phase
  spot on the tactics screen, or start from a ready-made shape (full-backs
  push on, build with three, narrow front line).
- Lineup gate before kick-off: the assistant replaces injured or suspended
  players, or you fix the lineup yourself.
- Knockout matches (cup ties, continental second legs and finals, national
  team finals) are played on through extra time, with a sixth substitution,
  and a live penalty shootout, whether you watch them or not; a second leg
  counts the first leg's goals.
- Structured match commentary in English and Italian with the clubs' names.
- Procedural match sound: a crowd that follows the play and reacts to
  chances, goals and cards, referee whistles and ball contacts, with master,
  crowd and effects volumes and a mute key (M).
- Match report with per-player lines and an Analysis tab: why the match went
  the way it did, an expected-goals race, a shot map, key moments, touch
  maps and pass networks. Half-time and full-time analysis in the match.
- Opposition report for the next opponent, with individual instructions
  the engine plays out: mark a player tightly, close him down, show him onto
  his weaker foot or double up on him.

**Competitions**
- 22 leagues with 440 clubs: a top division and a second division in each
  of eleven countries, with three clubs promoted and relegated every season,
  and a domestic cup per country.
- Staggered matchdays: each league round is spread over a long weekend
  (Friday to Monday, or Tuesday and Wednesday for midweek rounds) with the
  league's own days and kick-off times, so each Continue day plays fewer
  matches.
- Continental club competitions: Old Continent Champions Cup (36-club league
  phase), Old Continent Shield and Pan-American Champions Cup, with Swiss-style
  draws, play-offs, two-legged knockout rounds, a neutral final, prize money
  and club and association coefficients that decide future access.
- National teams: qualifiers, alternating world and continental finals,
  nations series and friendlies in international windows, squad call-ups,
  caps and goals, travel fatigue and club compensation.
- Standings with clinch marks (champions, promoted, play-off place,
  continental qualification, safe, relegated) as soon as they are
  mathematically certain, fixtures and results, a season calendar, season
  history with the archived final tables, top scorers, and a data hub with
  team and player trends, finishing and goalkeeping figures and league
  leaders.
- An end-of-season review: final position, record and goals, the board's
  verdict on each objective and the headline of your season, with the news
  of the season's end in the inbox.

**World simulation**
- Club economy sized on expected income (TV, merit money, gate receipts,
  commercial, continental money), wage and staff budgets, a finance ledger,
  ticket pricing where overpricing empties the stands, parachute payments
  and wage cuts after relegation, a wage ceiling for second-division clubs,
  and a transfer budget limited by the cash the club can actually spend.
- Board objectives for the league, the cup, the finances and the
  development of young players, monthly board confidence, warnings,
  dismissal, a five-level verdict at the end of the season (delighted to
  dismissed), and a transfer embargo when the club stays in the red.
- Supporter mood, updated every week from results against expectation,
  derbies, ticket prices and the arrival or sale of star players, with the
  reasons the fans talk about; very happy or very angry fans move the
  board's confidence.
- Facility projects requested from the board, including a medical centre.
- Injuries with a medical centre screen, fatigue, sharpness, morale and form.
  The medical screen charts each player's load, the physios warn about
  players at risk, and you can rest a player or limit him to 60 minutes.
  Playing a player through pain can aggravate the injury.
- Player development driven by age, training, minutes and staff.
- Squad status, captain, vice-captain and set-piece takers.
- Squad planner (depth, needs, age profile, contract calendar, next-season
  projection) and a player comparison screen.
- Player conversations, promises, dressing-room mood and short story chains
  (debuts, breakouts, captain disputes, poor runs, transfer sagas, comebacks,
  milestones, rivalries).
- Pre-season planning with friendlies against clubs from the same region,
  and mentoring groups for young players.
- Generated players with names that follow each nationality; unsigned free
  agents eventually leave the game, and computer-controlled clubs keep their
  senior squads filled from their academies.
- Seeded random numbers are now portable: the same seed draws the same
  numbers with every compiler and on every platform.

**Youth academy**
- Yearly intake cycle: a preview from the head of youth development on
  1 February, trialists arrive on 15 March, then a 30-day decision window.
- Intake quality from facilities, recruitment, coaching, staff, reputation
  and the country's talent pool, with occasional golden generations.
- Under-18 league with minutes, ratings and goals that feed development;
  scholarships, first professional contracts and homegrown status.
- Under-21 squad for every club on its own screen (Squad hub): players move
  between the first team, the U21s and the U18s under age rules (21 and
  younger, plus three outfield players and a goalkeeper over age); U18
  players move up at 19. One U21 league per country plays a cheap weekly
  round whose minutes speed up development, and AI clubs send young players
  outside their plans down and promote them when they are ready.

**Transfers and scouting**
- Transfer market with search, recommendations based on squad needs,
  affordability filters and a fit score.
- Transfer windows per country, so clubs in different countries buy and
  sell at different times of the year, ending with deadline day.
- Negotiations with counter-offers, instalments, add-ons, sell-on clauses,
  pre-contracts, free agents, transfer listing and releases with severance.
- Bids for your players are real negotiations: counter with fee,
  instalments, add-ons and sell-on, name your price or declare a player not
  for sale. Buyers answer on later days with their own structures, walk
  away when pushed too far, compete with rival bids and push harder near
  deadline day; a player who wanted the move can take a rejection badly.
  Selling clubs ask more for key players and long contracts; some refuse to
  sell key players. Buying clubs check their wage budget before they bid.
- Loans negotiated in both directions: loan fee, the share of the wage each
  club pays, an option or obligation to buy, and recalls; loan offers for
  your players can be countered as well.
- Contract talks with agents: they open high, soften round by round, react
  to your offer in their own words, ask for promises about playing time and
  can walk away. Contracts can carry release clauses, which any club may
  pay to open talks with the player.
- Scouts with nationalities, languages and regional experience; sending a
  scout to a continent, country or league shows the expected effectiveness.
- Players outside your club are shown as scouted estimates (ranges) until
  your knowledge is good enough.
- AI clubs buy, sell, loan and trim their squads.

**Staff and training**
- Staff grouped into coaching, medical, scouting and youth, with hiring,
  extensions and releases.
- Weekly training plans with presets, per-slot intensity and automatic
  adjustment in congested weeks.

**Manager career**
- Manager profile with nationality, reputation and coaching licence.
- Job centre with vacancies, applications, interviews and contract
  negotiation; unsolicited offers, resignation, sacking and severance.
- Option to start unemployed; AI managers for every club with their own
  sackings and appointments.
- National-team jobs: federations look for a coach after failed campaigns
  and tournaments; apply or accept an approach, alone or (with a continental
  reputation) next to your club job. Pick the squad on the Call-ups screen
  in the week before each window; qualifiers, nations series and finals
  results move your reputation, qualifying earns a new contract and missing
  out can cost a big nation's job.

**Awards and records**
- Monthly awards (player, young player, manager, goal of the month) and
  season awards (player, young player, manager, golden boot, golden glove,
  team of the season), player honours, and club and league records
  (biggest wins, attendances, points, goals, record signings and sales).

**Saves and reliability**
- Three save slots with autosave and numbered backups that can be restored
  from the main menu.
- Crash-safe saving: the game works on an in-memory copy and replaces the
  slot file only with a verified snapshot. When a save does not load, the
  load screen offers the newest backup that does, and the damaged file is
  kept aside.
- Numbered save migrations; saves from earlier development builds are
  upgraded when loaded.
- Crash reports: after a crash the game writes a report with the version
  and technical details, keeps the log of that session next to it in the
  user data folder, and tells you where to find them at the next start.
  Nothing is sent anywhere.

**Tools, tests and packaging**
- `fm_lab`, a headless balance lab for batches of matches, whole seasons and
  tactic round robins, with Markdown and JSON reports.
- Unit, core, GUI, playtest, monkey, adversarial, QA, performance and
  multi-season soak test suites.
- CI builds and release packages for Linux (tar.gz and AppImage), macOS
  (Apple silicon) and Windows (experimental), with the version taken from
  the project, release notes from this changelog and SHA-256 checksums.

### Changed

- The match engine was rewritten as a real-time simulation; results,
  statistics and the live views all come from it.
- The old separate scenes were replaced by one management shell.
- Tactics changes apply straight away and are kept with the career, with a
  button to revert to the tactic you had when you opened the screen.
- A matchday squad is the starting eleven plus up to nine substitutes.
- SDL3, SDL3_ttf and Dear ImGui are built from pinned sources by default
  (`FM_USE_SYSTEM_SDL=ON` to use exact-version system packages).
- Read-only game data is found next to the installed executable, and all
  writable files (saves, settings, logs, crash reports, captures) live in
  the user data directory.
- Engine and economy calibration: wider spreads of team strength, league
  goal references that follow those spreads, and a world economy that stays
  stable over many seasons.

### Fixed

- macOS packaging disables the unused vendored CoreText backend after SDL_ttf
  configuration, avoiding incompatible SDK headers while retaining font shaping.
- Linux Clang builds use the portable movement kernel instead of GCC-only
  function multiversioning.
- Goalkeepers account for reaction time across simulation ticks and use the
  remaining part of a tick to dive. Reachable-shot handling and accidental
  defensive touches are calibrated against the existing season ranges.
- Shot placement scatter and league-level finishing precision reduce excessive
  conversion while preserving shot volume and the existing calibration bands.
- Own goals caused by set pieces retain the restart origin in goal statistics.
- Matchday test careers choose the same club when reputation values are tied.
  Selection checks distinguish the assistant's preservation of healthy regulars
  from Auto Pick's selection of the best available XI.
- Camera tests dismiss the pre-match dialog before dragging the pitch, making
  input checks independent of highlight timing and graphics backend.
- Tight-marking checks use 64 paired matches to reduce sampling noise while
  retaining the existing minimum reduction in touches.
- Blurry or wrongly scaled rendering on HiDPI displays.
- A memory leak in the transfer market.
- Exploits found in testing: a very high ticket price was always the most
  profitable; a loaned player could be recalled the same day while keeping
  the fee; a sell-on clause could pay the club its own money on a buy-back;
  a loan option skipped the wage check; the bench could hold the whole
  squad; a managed squad had no size limit (now 36); computer-controlled
  clubs could end up without a senior goalkeeper.

### Known limitations

- Play mode has no accelerated clock, no practice match, and the AI takes
  every set piece and restart.
- Loans for other clubs' players do not offer an appearance clause.
- The leagues of Brazil, Argentina and the United States follow the
  European season shape (August to May).
- Worlds started from the same seed can still differ between platforms:
  the random numbers are portable, but some floating-point maths (match
  physics among it) is not yet.
- Realism gaps remain: about 8% of shots become goals across the world,
  possession differs too little between strong and weak sides, and there
  are fewer offsides than in real football.
- The second divisions exist only in new careers; saves started with the
  earlier twelve-league world keep their leagues.
- A knockout match abandoned while level (a side down to six players) is
  settled by a statistical extra time and shootout instead of being awarded.
- You do not watch or direct your national team's matches live; they are
  simulated with the squad you pick.
- U21 players are not sold or loaned out by AI clubs while in the U21
  squad (they can be when back in the first team).
- Each player has one nationality.
- The swipe direction and distance for Back/Forward are tuned for common
  touchpads; natural-scrolling setups may need them flipped.
- Italian commentary uses club names without their articles.
- Match sound is synthesised and still being tuned by ear; there are no club
  chants and no commentary voice.
- Awards are not rebuilt for careers saved before awards existed.
- The macOS package is only ad-hoc signed (not notarised) and needs
  macOS 15 on Apple silicon. The Windows build is experimental and has not
  been validated on real Windows systems. See
  [docs/development/release.md](docs/development/release.md).
