# Shared Sandbox engine settings

Set-up has separate Home engines, Away engines, Shared systems and Simulation
cards. Paired Aardvark / Binturong buttons edit the same session settings used
by the command-line options. Home and Away retain independent Brain, Body and
Manager choices; Handshake, Referee, Ball physics, Out of play and Possession
remain shared. Home advantage supports both a slider and direct numeric entry,
clamped to the engine's supported range.

The layout follows `UX Design/player-profile-2/SPEC.md`: 24px outer inset,
160px header, 6px gaps and edge fades, and square cards. Settings use fixed
440px columns (the reference's compact right-column width), rather than its
wider attribute columns. The three-column layout is capped at 1332px, including
gaps. When it cannot fit, Home and Away stack in the left column and Simulation
sits in the right, capped at 886px total. Below that, cards stack at a maximum
440px width. The header follows the same cap. Spare width stays empty on the
right; a single page scrollbar exposes content below the fold.

Typography uses Manrope: 42px bold title, 17px regular subtitle, 16px bold
tracked card headings, 16px medium row labels, 14px bold tracked buttons,
14px bold subsection headings and 16px regular explanatory text. Engine helpers
sit below the full control row, keeping the 440px cards readable without widening.
The Regular and Medium fonts are upstream static TTFs
from `googlefonts/manrope`, `fonts/ttf/manrope-{regular,medium}.ttf` on master;
the existing OFL in `assets/licenses/Manrope.txt` applies. The sidebar and setup
use ImGui's DPI font scaling once, alongside scaled geometry.
All sandbox Manrope faces compensate for stb_truetype's ascent/descent sizing
using ExtraSizeScale = 2732 / 2000, matching the reference's CSS em sizes. The
bundled Bold face's OS/2 weight is 700; headings and buttons use that face rather
than simulated bold or the Regular font.

Home advantage, Full tactical familiarity and Debug at kick-off are moved from
the match footer into Simulation settings on this page. Home advantage applies
to all new engine runs; familiarity and debug-at-kick-off remain match options.
Match seeds, club selection, lineups, tactics and recording controls stay on
Matches. Matches remains the initial selected page.

There are no engine-version selectors on the match cards, drill-run panel or
training-run panel. Each new drill or training run reads the shared settings.
Training no longer duplicates the Home brain/body choice across both sides:
its alternating-side squad honours both Home and Away choices. Existing engines
and captured match setups keep their original versions when Set-up is changed.
The headless laps diagnostic still uses its deliberately uniform Binturong
brain/body, restoring the shared settings even if constructing a run throws.

Recording Open retains recorded settings. Replay with takes its brains, bodies,
handshake and possession from Set-up instead of maintaining separate dropdowns;
the replay API's other settings remain those saved in the recording.

## Validation

The redesigned page passed the Release build. An offscreen SDL software-renderer
harness exercised the actual setup renderer at 1366x768, 1536x864, 1600x900,
1920x1080, 2560x1440 and 3440x1440, plus 2880x1620 at 1.5x scale, without horizontal
overflow. Rendered images were inspected at standard and 1.5x scale. All eleven
engine selectors, both checkboxes, the home-advantage slider and numeric entry
passed input checks. The harness uses isolated settings; it is not an engine
integration test. Live desktop verification was unavailable (Windows access denied).

Build and run the headless sandbox_settings_check target. It creates an isolated
temporary world and checks both mixed-version directions across real match,
drill and training engines, including home advantage. It also checks captured
match settings and existing engines survive a settings change, the next run
uses the changed settings, training retains its period/effort rules, and the
uniform diagnostic restores the shared brain/body settings.

The offline Release build and this integration check pass. Update
build/src/Release/match_sandbox.exe from build-possession/src/match_sandbox.exe
and verify matching SHA-256 hashes after building. No desktop control is used.
