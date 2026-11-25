# Sandbox splash

The normal Sandbox launch shows the approved tactical-board design while its
world loads in a background task. Native text and geometry scale together;
the scene does not read controller data until loading completes. The loading
future is joined before scene destruction. Direct match/drill launches and
headless commands retain their existing startup path.

The formation attacks upwards: goalkeeper, flat back four, defensive midfielder,
two central midfielders, two wingers and striker. A dashed yellow pass with an
open arrowhead runs from left-back to right-wing. Player 12 and the rule below
Again share the lime accent; the tagline has no final full stop.

## Timing

- Pitch appears over 0–0.65 seconds.
- Players appear over 0.3–0.7 seconds.
- Pass draws over 0.7–1.2 seconds.
- Completed composition holds until 2.6 seconds.
- The title and tactical-board panels split at their shared seam and slide
  left/right with smooth easing over 0.4 seconds, revealing the actual setup
  screen underneath. This gives approximately three seconds in total when
  loading completes within the hold period. Slower loading extends the hold.
- A key, click or touch skips the presentation once loading completes. F12
  remains available for screenshots. Setup controls are disabled during the
  reveal to prevent clicks through the opening panels. Reduced motion shows a static composition
  while loading and proceeds immediately when ready.

## Validation

Built the Release configuration with the existing offline Ninja/clang-cl build
in build-possession. The original visual verification attempt was stopped by
the user; no completed visual inspection is claimed. The executable is copied
to build/src/Release/match_sandbox.exe after a successful rebuild, with matching
SHA-256 hashes checked.

Typography uses the bundled Manrope Bold font explicitly for all splash copy, including title-width measurement for the lime rule.
