# Sandbox sidebar: first pass

The Sandbox shell has a fixed 220-design-pixel sidebar, scaled with the existing
UI scale, separated from the content by a 6-pixel gap. Its warm dark surface,
square rows, cream text and lime active marker follow the UX Design reference.
The shell has no outer padding, border or rounded corners: the sidebar touches
the left, top and bottom window edges. The content child retains the original
24px horizontal and 16px vertical padding and existing page background; the
6px separator uses the sidebar's warm dark surface.

Matches is the initial selected item and opens the existing two-club match
page. Drills and Training retain their pages; Tools opens Visualisers. The
recordings browser remains available from the match page's recording controls
and does not determine the sidebar selection. Set-up contains the shared engine
version table and simulation settings described in sandbox-engine-settings.md. The previous
header section selector is removed. Page headers, cards and settings retain
their existing UI.

Sidebar branding and navigation use bundled Manrope Bold (700) at 28 design
pixels. Navigation labels have 0.5px tracking, matching the player-name style
in the UX specification at a smaller size. Rows retain their existing height;
labels are vertically centred, with lime for the active item and cream otherwise.

Font provenance: Fontsource's static Latin Manrope 700 face,
`fonts/google/manrope/files/manrope-latin-700-normal.woff` in
https://github.com/fontsource/font-files (blob
`6000ec7448eb54eb2a1b0e6234bef9ff2bb6547b`). The WOFF container was losslessly
unpacked into sfnt/TTF with table checksums preserved and the whole-font checksum
recomputed. Glyph outlines and font names are unchanged. The SIL OFL is bundled
in assets/licenses/Manrope.txt.

Validation: the offline Release build of match_sandbox passes. Copy the resulting
build-possession/src/match_sandbox.exe into build/src/Release/match_sandbox.exe
and compare SHA-256 hashes. A temporary native check using ImGui's bundled
stb_truetype successfully loads the font, finds every label glyph and verifies
that all labels, including tracking, fit the 188px inner sidebar width (the
widest is Player 12 at approximately 124px). No desktop automation is required.
