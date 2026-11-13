# Shared Sandbox engine settings

Set-up now has its own page. The Engine versions table has a setting-name column
and two clickable option columns: Aardvark and Binturong. The selected option is
highlighted. Home and Away retain independent Brain, Body and Manager rows;
Handshake, Referee, Ball physics, Out of play and Possession are shared rows.
The table edits the same session settings used by the command-line options.

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

Build and run the headless sandbox_settings_check target. It creates an isolated
temporary world and checks both mixed-version directions across real match,
drill and training engines, including home advantage. It also checks captured
match settings and existing engines survive a settings change, the next run
uses the changed settings, training retains its period/effort rules, and the
uniform diagnostic restores the shared brain/body settings.

The offline Release build and this integration check pass. Update
build/src/Release/match_sandbox.exe from build-possession/src/match_sandbox.exe
and verify matching SHA-256 hashes after building. No desktop control is used.
