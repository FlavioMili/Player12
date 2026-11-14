# Sandbox activity and Tools pages

The launch pages use the same design language as Set-up: Manrope with the CSS
em-size correction, warm dark surfaces, square cards, 6px gaps and edge fades,
and a 160px header. Titles are 42px bold, subtitles 17px regular, card headings
16px bold with tracking, body text 16px regular and action buttons 14px bold
with tracking. These changes are scoped to the sandbox pages.

Tools uses the same 160px header and three direct launch cards: Contact
Difficulty, True View and Technique Bench. Cards are 440px wide, arranged in
three columns at 1920 and above, two columns on smaller desktop screens, or
one column on narrow windows. A 400px two-column variant fits at 1100px.
Cards retain the light/mid/dark surface progression, centered Open buttons and
the existing command-line hints. Spare width stays empty. The tools themselves
retain their existing behaviour and remain focused on the Binturong body model.

Action labels use the shared typography helper to center their visible glyph
bounds on both axes, including tracking and side bearings. Catalogue and
sidebar rows retain the reference's left alignment, with vertically centered
text. Offscreen checks verified action/version-label geometry within half a
pixel and rendered ink within one pixel at 1x and 1.5x scale.

The catalogue is 260px wide, beside an 886px detail area (806px on smaller
desktop screens), capped at 1152px or 1072px plus the scrollbar gutter. Spare
width stays empty on the right. Narrow windows stack the catalogue above the
details. The scrollbar gutter stays beside the compact page, with a scrollbar
only when needed. The page scrolls together; cards and tables never scroll
independently.

Drills retains all presets, parameters, seed entry, Watch, Measure and the
conditional Shot map action. Overview, settings and run controls are separate
cards. The page header includes the Set-up note for engine versions and
conditions; no separate information card follows the catalogue.
Settings and seed entry share a two-column form, with labels above controls.
Horizontal and vertical separators and alternating row shading use the same
table colours as the training roster.
Section headings, collapsible groups, presets, automatic values and tooltips
retain their behaviour. Other drill views keep their single-column forms.

Category tabs span the top of the Drills page and filter the left-hand list:
Movement, Passing, Shooting, Control, Duels, Aerial and Goalkeeping. Category
membership is explicit catalogue metadata in `makeDrills`, not inferred from
display names. The tabs wrap on narrow screens. Each category remembers its
last drill, and switching categories retains all drill objects and settings.
Run drill remains above Settings for every selection.

Training retains the home club's squad, independent session selection, the
20-player limit and squad-order construction of the training session. The
page header identifies the training club, explains how to choose it on Matches
and points to Set-up for engine versions and conditions. The session list has
no separate training-club information card. The Start session action sits
above the squad so it stays easy to find. The roster is split into two balanced
lists, read down the left column and then down the right, retaining squad
order. Both lists share the same selection and 20-player cap. Squad
selection uses lime checkboxes; unavailable extra picks are disabled at the
limit, and starting with an empty selection remains disabled. Names truncate
with tooltips when necessary. Phys and Stam mean Physicality and Stamina.

## Validation

The Tools launch page was rendered at all six reference sizes, at 1.5x scale,
and at 1100x768 and 900x768 without horizontal overflow. All three Open actions
were checked against their intended scene types.

The Release target builds. A temporary offscreen SDL harness rendered the
actual MatchSandboxScene, using an isolated generated world. Both pages were
checked at all six reference resolutions and at 1.5x scale without horizontal
overflow. All 25 drill forms were also checked with collapsible sections open
at 1366x768 and at 1.5x scale. Input checks exercised drill selection, parameter
editing and training pick/unpick. The 20-player selection cap remained intact.
The narrow-window checks cover stacked layouts at 1100x768 and a single-column
detail fallback at 900x768. At 1920x1080, the default Sprint form and the full
28-player sample roster fit without scrolling. Inputs in both settings
columns and training selection were checked in the offscreen renderer.
Category input checks cover all seven tabs, all 25 memberships, remembered
selections and preserved per-drill seeds after switching between categories.

The live match keeps its existing rendering and behaviour. The live drill
and training sections below extend this styling to their running views.
Simulation engines are unchanged.

## Matches

The Matches launch page uses the same Manrope typography, 160px header,
6px lime rule, square cards and shaded edges. Match actions and recording
controls sit above the teams. Home and away each have club/formation,
starting XI and team-instruction cards. Columns cap at 660px, step down to
600px or 528px on smaller desktops, and stack in narrower windows. All cards
share the page scrollbar. Player pickers retain swaps, tactical roles,
duties, fit scores, automatic selection and possession shapes. The XI and
instruction tables use rules and alternating rows. Selected player names,
roles and duties have tooltips where compact cells truncate them.

The actual page was rendered at all six reference sizes, at 1.5x scale and
at 1100x768 and 900x768 without horizontal overflow. Kick off was exercised
against its MatchScene destination and checked as disabled for identical
clubs. The Release target builds successfully.

## Running a drill

The live DrillScene shares the launch pages' corrected Manrope fonts (42px
bold title, 17px summary, 16px body/card headings and 14px bold action labels),
warm surfaces, square corners, 6px gaps/edge fades and 24px outer inset. It
reuses the current font atlas when entered from the launch page and loads the
same fonts for direct command-line entry. The header grows beyond 160px only
when wrapped title/summary/context text needs the space.

The live viewer uses the whole window. Its left column is 440px wide on
larger desktops and 400px on smaller ones, plus its scrollbar gutter. The
title, playback controls, result and settings cards sit in this independently
scrolling column. The pitch fills all remaining width and the available
height, staying fixed when settings scroll. Neither the title width nor the
former 660px stage height limits it. The renderer retains its pitch aspect
ratio. Below 1048px logical window width, the layout stacks a full-width
pitch before the controls. The Back action remains above settings.

Restart, play/pause, both fixed-step actions, all seven speeds, new seeds,
debugger windows, 2D/3D and all three cameras remain available. Finished runs
keep their report and expose Restart; inactive playback/step controls are
disabled. Human-control hints and keyboard/gamepad steering remain intact.
Results retain PASS/FAIL colouring. Strike/header difficulty details are
shown with the results. Settings use the same ruled, alternating-row form as
the launch page, including presets and seed entry, with labels above inputs.


Validation rendered the actual live DrillScene at the six reference sizes,
at 150% scaling, and at 1100x768 and 900x768. All 26 currently registered drills
were rendered with all parameter sections expanded at 1366x768 and 150%
scaling, including scrolling to the end of each form, without horizontal
overflow. Input checks covered pause, both step buttons, restart, random
seeds, finished reports, keyboard restart, debugger windows, the speed
selector, the 3D checkbox and camera selector. All three camera projections
rendered. A human-controlled run checked its paused start, Space playback
and keyboard-navigation suspension/restoration. Re-entering scenes reused
font sources. The Release target builds successfully.

The expanded viewer was checked against the right window inset and full
available height. Scrolling every expanded form kept the desktop pitch's
position and size fixed. Human playback, speed selection and the 3D/camera
selectors were exercised again after the controls moved into the left column.

## Scrollbars

Converted pages use the sandbox identity's cream (#F0EAE2) for the track
and lime (#9BD06B) for the handle. This adapts the two-colour track/trim
treatment in player-profile-2 SPEC section 4.2 to the neutral sandbox identity.
Cream keeps the whole rail visible around the handle and along unused track;
a brown track still blended into the page in the rendered check. Hover/active handles mix lime with white
at 15%. The header's solid lime top rule stays unchanged. Both the launch
page shell and live drill viewer use the shared scrollbar tokens.

## True View

The standalone True View tool now uses the shared Manrope typography, 24px
outer inset, square light/mid/dark cards, 6px spacing/edge fades and the
cream/lime scrollbar. A 440px controls column (400px on smaller desktops)
scrolls independently beside a model canvas that takes all remaining width
and height. Narrow windows stack the canvas before the controls. The heading
holds the tool description and orbit/zoom/elevation gestures. Back and camera
presets remain near the top of the controls column.

Controls and Engine values tabs share that column so engine/source readouts
do not take space from the canvas. The Player card uses two columns of
labelled inputs with table rules and alternating rows. Pose/playback,
technique/strike and layer cards retain their conditional controls. All
player attributes, foot preference, poses, techniques, strike types, layer
switches and camera gestures retain their behaviour. The values tab reads
this frame's calculated entries, including sources and WIP notes; its
column headings are 14px bold and the body remains 16px. The technical shape
colours are preserved. The canvas surface, grid and caption styling use the
warm palette, and captions wrap within the canvas.

Validation used the actual offscreen SDL TrueViewScene at all six reference
sizes, 150% scaling, 1100x768 and 900x768 without horizontal overflow. All seven
poses and nine techniques rendered at 1366x768 and 150% scaling. Scrolling
readouts kept the desktop canvas position/size fixed. Native input checks
covered the tabs, player input, foot preference, orbit, wheel zoom, camera
elevation and presets. Re-entry reused font sources. The Release target
builds successfully.

Controls now shows a second button row: Player, Pose, Technique and Layers.
Only the selected control card is rendered. The row is hidden under Engine
values, and returning to Controls restores its selected section. Player,
pose, technique, playback and layer state stay intact when switching tabs.
Native tab clicks, exclusive section visibility, state preservation and the
Engine values round trip were checked across all nine preview sizes. The
model canvas retained its desktop bounds throughout.

## Technique Bench

The bench uses the shared Manrope fonts, warm palette, 24px inset, square
cards, 6px gaps/edge fades and cream/lime scrollbars. A fixed 440px controls
column (400px on smaller desktops) sits beside the results area; narrow
windows stack the two. The results plot uses the remaining width. The header
contains the test description and pass/shot success criteria.

Run actions stay above Run, Tests, Filters and Tuning tabs. Run setup uses a
ruled, alternating-row form, and Show/Compare/Load saved selectors have their
own card. The test catalogue retains checkboxes, section collapse, stable
series-colour swatches, full-name tooltips and Tick/Untick shown/Reload.
Filters use wrapping, selected-colour buttons. Tuning retains Contact View,
Copy and Reset actions, including disabled states and change descriptions.

Results use Curves, Grid and Test buttons. Result and colour selectors are
capped at 660px together instead of stretching across the chart. Comparison
curves remain dashed, stable series colours and heatmap semantics are
preserved, and charts use the warm surface/text palette. The matrix keeps
its frozen columns/header and two-dimensional table scrolling; detailed
numeric tables retain horizontal scrolling. Neither scrolls the whole page
horizontally. Watch and its compact Skill/Seed row precede the detailed
table, opening the converted drill screen with the test settings and tuning.

The actual scene was rendered at all six reference sizes, 150% scaling,
1100x768 and 900x768. Run/Tests/Filters/Tuning, curves, comparison, rate and
metric grids, and individual details were checked without page overflow.
Native inputs checked tab selection, Tick/Untick shown, a tag filter, Watch
routing into DrillScene and Run ticked launching/collecting a real bench job.
The worker check avoided the user's CSV save folder. Font sources are reused
on re-entry. The Release target builds successfully.

## Contact Difficulty

Contact Difficulty uses the same corrected Manrope fonts, 24px outer inset,
6px card gaps/edge fades, square surfaces and cream/lime scrollbar. Its fixed
440px left column (400px on smaller desktops) scrolls independently beside
a full-height contact map; narrow windows stack the map before the controls.
Contact type and colour mode stay visible above Inputs, Analysis and Tuning.
Inputs has Player/Ball/Aim tabs; Tuning has Summary/Stretch/Knobs tabs. Switching
tabs preserves inputs, tuning and the colour mode. Back and Technique Bench
navigation remain near the top.

Input forms use two ruled, alternating columns with labels above controls.
Analysis contains the side view, three job bars with readable cream numeric
values, and the condition/contribution table. Stretch retains all 6x6 editable
values, ordered angle/distance headers, heat colours, reset and copy; its
14px table font and tighter input padding fit the seven columns. Knob groups
retain their individual resets, changed-value warning colour, default-value
tooltips and unrestricted Ctrl-click entry, with labels above inputs. Summary
retains change descriptions, Copy and Reset all. Shared tuning still feeds
the engine functions and Technique Bench; no assessment formulas changed.

The map retains disc/arrival/aim drags, right-drag facing, reach clamping,
dead zones, direction arrows, heat modes and hover assessments. Its compact
legend measures its wrapped height before sizing the circle. The complete
chevron explanation remains available on hover. Neutral surfaces use the
new palette while semantic heat and zone colours remain unchanged.

Validation rendered the actual ContactViewScene across the six reference
sizes, 150% scaling, 1100x768 and 900x768. All input/tuning tabs and analysis
were checked without horizontal overflow. Every contact and colour mode,
plus fully expanded touch/strike knob groups, were checked at 1366x768 and
150% scaling. Native input checks exercised a player slider, contact type,
all four map drag semantics, stretch/global resets and Technique Bench
routing. Diagram bounds stayed fixed while controls scrolled, style stacks
balanced, and font sources were reused. The Release target builds.

## Live training sessions

The live session uses the same corrected Manrope fonts, 24px inset, square
light/mid/dark cards, 6px gaps/edge fades and cream/lime scrollbars. A 440px
controls column (400px on smaller desktops) sits beside a pitch that takes
all remaining width and height. Only the left column scrolls; narrow windows
stack the pitch above the controls. Session/club and current-drill context
sit in the heading. Run actions stay above Progress and Results tabs.

Restart, Space/R shortcuts, all eight speeds and Skip to the end are retained.
Next drill and End session remain disabled until completion. Skip works even
when paused; playback controls are disabled while skipping or finished.
Progress shows the current description/report and the ordered session drills.
Results retain finish ranks, full-name tooltips, live updates and sorting.
Two selectable metrics keep the name and numeric columns readable in the
compact column; every original metric and its header tooltip remains
available, and sorting follows the selected metric. Metric choices reset for
a new drill and persist on restart. The results table uses alternating rows,
inner rules, 14px bold headings and 16px body values, with the page's scrollbar.

Validation rendered the actual TrainingSessionScene at all six reference
sizes, 150% scaling, 1100x768 and 900x768, using controlled completion fixtures
wrapped around the real drills' setup and pitch overlays. All three result
schemas and every metric were checked without horizontal overflow; scrolling
kept the desktop pitch fixed. Native tests covered completion gating, pause,
Space/R, speed selection, skip, metric selection, Next and End session.
Clicking name/selected-metric headers changed rendered row order. A separate
real Across and back run completed with both players' results. Font sources
were reused and the Release target builds. No training or engine formulas
were changed.
