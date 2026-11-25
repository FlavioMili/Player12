/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
*/
var NAVTREE =
[
  [ "Player12", "index.html", [
    [ "Save migrations and save protocol", "index.html", "index" ],
    [ "Contributing to Player12", "md_CONTRIBUTING.html", [
      [ "Choose a starting point", "md_CONTRIBUTING.html#autotoc_md12", null ],
      [ "Your first pull request", "md_CONTRIBUTING.html#autotoc_md13", null ],
      [ "Set up for code changes", "md_CONTRIBUTING.html#autotoc_md14", null ],
      [ "Check your change", "md_CONTRIBUTING.html#autotoc_md15", null ],
      [ "Code and documentation conventions", "md_CONTRIBUTING.html#autotoc_md16", null ],
      [ "Names, credit and licence", "md_CONTRIBUTING.html#autotoc_md17", null ]
    ] ],
    [ "Contributors", "md_CONTRIBUTORS.html", [
      [ "Add your name", "md_CONTRIBUTORS.html#autotoc_md19", null ]
    ] ],
    [ "AI Guidelines", "md_AI__GUIDELINES.html", [
      [ "1. The Core Philosophy", "md_AI__GUIDELINES.html#autotoc_md21", null ],
      [ "2. Mandatory Declaration", "md_AI__GUIDELINES.html#autotoc_md22", null ],
      [ "3. Expectations for Reviewers", "md_AI__GUIDELINES.html#autotoc_md23", null ],
      [ "4. Expectations for AI Agents", "md_AI__GUIDELINES.html#autotoc_md24", null ]
    ] ],
    [ "Architecture", "md_docs_2ARCHITECTURE.html", [
      [ "Build targets", "md_docs_2ARCHITECTURE.html#autotoc_md26", null ],
      [ "Core model", "md_docs_2ARCHITECTURE.html#autotoc_md27", null ],
      [ "Match engine and scheduling", "md_docs_2ARCHITECTURE.html#autotoc_md28", null ],
      [ "Persistence", "md_docs_2ARCHITECTURE.html#autotoc_md29", null ],
      [ "Controller", "md_docs_2ARCHITECTURE.html#autotoc_md30", null ],
      [ "GUI", "md_docs_2ARCHITECTURE.html#autotoc_md31", null ],
      [ "Balance lab", "md_docs_2ARCHITECTURE.html#autotoc_md32", null ],
      [ "Tests", "md_docs_2ARCHITECTURE.html#autotoc_md33", null ]
    ] ],
    [ "Reproducible builds", "md_docs_2development_2builds.html", [
      [ "Presets", "md_docs_2development_2builds.html#autotoc_md35", null ],
      [ "clang-tidy", "md_docs_2development_2builds.html#autotoc_md36", null ],
      [ "Dependency updates", "md_docs_2development_2builds.html#autotoc_md37", null ]
    ] ],
    [ "Design notes recovered from Spec Kitty", "md_docs_2development_2design-notes.html", [
      [ "Source provenance", "md_docs_2development_2design-notes.html#autotoc_md39", null ],
      [ "Rationale that still applies", "md_docs_2development_2design-notes.html#autotoc_md40", null ],
      [ "Planned contracts versus current implementation", "md_docs_2development_2design-notes.html#autotoc_md41", null ],
      [ "This documentation and extension pass", "md_docs_2development_2design-notes.html#autotoc_md42", null ]
    ] ],
    [ "Extending and modding", "md_docs_2development_2extending-and-modding.html", [
      [ "Edit data without rebuilding", "md_docs_2development_2extending-and-modding.html#autotoc_md44", null ],
      [ "Per-match settings for C++ tools", "md_docs_2development_2extending-and-modding.html#autotoc_md45", null ],
      [ "Choose the smallest behavior extension", "md_docs_2development_2extending-and-modding.html#autotoc_md46", null ],
      [ "Validate a behavior change", "md_docs_2development_2extending-and-modding.html#autotoc_md47", null ]
    ] ],
    [ "Match engine: implementation and rationale", "md_docs_2development_2match-engine.html", [
      [ "Ownership and data flow", "md_docs_2development_2match-engine.html#autotoc_md49", null ],
      [ "Coordinates and clocks", "md_docs_2development_2match-engine.html#autotoc_md50", null ],
      [ "One authoritative simulation step", "md_docs_2development_2match-engine.html#autotoc_md51", [
        [ "Round the pitch", "md_docs_2development_2match-engine.html#autotoc_md52", null ],
        [ "Training pitches", "md_docs_2development_2match-engine.html#autotoc_md53", null ],
        [ "Training sessions", "md_docs_2development_2match-engine.html#autotoc_md54", null ]
      ] ],
      [ "Choosing an advance API", "md_docs_2development_2match-engine.html#autotoc_md55", null ],
      [ "Ball flight, contacts and rules", "md_docs_2development_2match-engine.html#autotoc_md56", [
        [ "Calibrating finishing without changing keeper reach", "md_docs_2development_2match-engine.html#autotoc_md57", null ]
      ] ],
      [ "Determinism, replay and observation", "md_docs_2development_2match-engine.html#autotoc_md58", null ],
      [ "Implementation map and checks", "md_docs_2development_2match-engine.html#autotoc_md59", null ]
    ] ],
    [ "Player behavior: from attributes to actions", "md_docs_2development_2player-behavior.html", [
      [ "Persistent player versus match slot", "md_docs_2development_2player-behavior.html#autotoc_md61", null ],
      [ "Behavior between matches", "md_docs_2development_2player-behavior.html#autotoc_md62", null ],
      [ "Team phase and coordinated assignments", "md_docs_2development_2player-behavior.html#autotoc_md63", null ],
      [ "Shape, roles and duties", "md_docs_2development_2player-behavior.html#autotoc_md64", null ],
      [ "Off-ball intentions and physical movement", "md_docs_2development_2player-behavior.html#autotoc_md65", null ],
      [ "On-ball choice and execution", "md_docs_2development_2player-behavior.html#autotoc_md66", null ],
      [ "Defending, keepers and external control", "md_docs_2development_2player-behavior.html#autotoc_md67", null ],
      [ "Reproducing a suspicious decision", "md_docs_2development_2player-behavior.html#autotoc_md68", null ]
    ] ],
    [ "Possession migration close-out", "md_docs_2development_2possession-validation.html", [
      [ "Evidence and reproduction", "md_docs_2development_2possession-validation.html#autotoc_md70", null ],
      [ "Scenario coverage", "md_docs_2development_2possession-validation.html#autotoc_md71", null ],
      [ "Ownership and compatibility audit", "md_docs_2development_2possession-validation.html#autotoc_md72", null ],
      [ "Remaining acceptance and follow-ups", "md_docs_2development_2possession-validation.html#autotoc_md73", null ]
    ] ],
    [ "Possession architecture", "md_docs_2development_2possession.html", [
      [ "Storage and mutation ownership", "md_docs_2development_2possession.html#autotoc_md75", null ],
      [ "Read queries and compatibility meanings", "md_docs_2development_2possession.html#autotoc_md76", null ],
      [ "Foot-control policy", "md_docs_2development_2possession.html#autotoc_md77", [
        [ "Claim expiry", "md_docs_2development_2possession.html#autotoc_md78", null ]
      ] ],
      [ "Friendly collection and receiver preference", "md_docs_2development_2possession.html#autotoc_md79", null ],
      [ "Version selection and persistence", "md_docs_2development_2possession.html#autotoc_md80", null ],
      [ "Boundary checks and diagnostics", "md_docs_2development_2possession.html#autotoc_md81", null ]
    ] ],
    [ "Building and releasing desktop packages", "md_docs_2development_2release.html", [
      [ "Development builds for main commits", "md_docs_2development_2release.html#autotoc_md86", null ],
      [ "Cutting a release", "md_docs_2development_2release.html#autotoc_md87", null ],
      [ "What each job does", "md_docs_2development_2release.html#autotoc_md88", null ],
      [ "Package layout and runtime paths", "md_docs_2development_2release.html#autotoc_md89", [
        [ "Crash reports and save recovery", "md_docs_2development_2release.html#autotoc_md90", null ]
      ] ],
      [ "Building a package locally (Linux)", "md_docs_2development_2release.html#autotoc_md91", null ],
      [ "Known limitations", "md_docs_2development_2release.html#autotoc_md92", null ]
    ] ],
    [ "Runtime editor architecture", "md_docs_2development_2runtime-editor.html", [
      [ "Persistence", "md_docs_2development_2runtime-editor.html#autotoc_md94", null ],
      [ "Runtime replacement", "md_docs_2development_2runtime-editor.html#autotoc_md95", null ],
      [ "Preserving custom data", "md_docs_2development_2runtime-editor.html#autotoc_md96", null ],
      [ "Extending a page or feature", "md_docs_2development_2runtime-editor.html#autotoc_md97", null ],
      [ "Checks", "md_docs_2development_2runtime-editor.html#autotoc_md98", null ]
    ] ],
    [ "Sandbox activity and Tools pages", "md_docs_2development_2sandbox-activities.html", [
      [ "Validation", "md_docs_2development_2sandbox-activities.html#autotoc_md100", null ],
      [ "Matches", "md_docs_2development_2sandbox-activities.html#autotoc_md101", null ],
      [ "Running a drill", "md_docs_2development_2sandbox-activities.html#autotoc_md102", null ],
      [ "Scrollbars", "md_docs_2development_2sandbox-activities.html#autotoc_md103", null ],
      [ "True View", "md_docs_2development_2sandbox-activities.html#autotoc_md104", null ],
      [ "Technique Bench", "md_docs_2development_2sandbox-activities.html#autotoc_md105", null ],
      [ "Contact Difficulty", "md_docs_2development_2sandbox-activities.html#autotoc_md106", null ],
      [ "Live training sessions", "md_docs_2development_2sandbox-activities.html#autotoc_md107", null ],
      [ "Drill Measure", "md_docs_2development_2sandbox-activities.html#autotoc_md108", null ],
      [ "Shot Map", "md_docs_2development_2sandbox-activities.html#autotoc_md109", null ],
      [ "Embedded recordings and analysis", "md_docs_2development_2sandbox-activities.html#autotoc_md110", null ]
    ] ],
    [ "Shared Sandbox engine settings", "md_docs_2development_2sandbox-engine-settings.html", [
      [ "Validation", "md_docs_2development_2sandbox-engine-settings.html#autotoc_md112", null ]
    ] ],
    [ "Sandbox sidebar", "md_docs_2development_2sandbox-sidebar.html", null ],
    [ "Sandbox splash", "md_docs_2development_2sandbox-splash.html", [
      [ "Timing", "md_docs_2development_2sandbox-splash.html#autotoc_md115", null ],
      [ "Validation", "md_docs_2development_2sandbox-splash.html#autotoc_md116", null ]
    ] ],
    [ "Tests and tools", "md_docs_2development_2testing.html", [
      [ "Lanes, labels and shards", "md_docs_2development_2testing.html#autotoc_md118", null ],
      [ "Shared fixtures (<tt>test/support/test_worlds.h</tt>)", "md_docs_2development_2testing.html#autotoc_md119", null ],
      [ "Run one area first", "md_docs_2development_2testing.html#autotoc_md120", null ],
      [ "Balance lab", "md_docs_2development_2testing.html#autotoc_md121", null ],
      [ "Screenshots", "md_docs_2development_2testing.html#autotoc_md122", null ],
      [ "Performance benchmarks", "md_docs_2development_2testing.html#autotoc_md123", null ],
      [ "Historical calibration failures", "md_docs_2development_2testing.html#autotoc_md124", null ]
    ] ],
    [ "Website documentation rendering", "md_docs_2development_2website.html", [
      [ "Lists and code examples", "md_docs_2development_2website.html#autotoc_md126", null ],
      [ "Diagram sizing and themes", "md_docs_2development_2website.html#autotoc_md127", null ],
      [ "Rendering repair walkthrough", "md_docs_2development_2website.html#autotoc_md128", null ],
      [ "Media and publication size", "md_docs_2development_2website.html#autotoc_md129", null ]
    ] ],
    [ "Controls", "md_docs_2user_2controls.html", [
      [ "Anywhere in the game", "md_docs_2user_2controls.html#autotoc_md131", null ],
      [ "Main menu and club choice", "md_docs_2user_2controls.html#autotoc_md132", null ],
      [ "Management screens", "md_docs_2user_2controls.html#autotoc_md133", [
        [ "Back and Forward", "md_docs_2user_2controls.html#autotoc_md134", null ],
        [ "Sidebar hubs", "md_docs_2user_2controls.html#autotoc_md135", null ],
        [ "Mouse on management screens", "md_docs_2user_2controls.html#autotoc_md136", null ]
      ] ],
      [ "Match day", "md_docs_2user_2controls.html#autotoc_md137", [
        [ "Playback", "md_docs_2user_2controls.html#autotoc_md138", null ],
        [ "Substitutions board and tactics", "md_docs_2user_2controls.html#autotoc_md139", null ],
        [ "Touchline shouts", "md_docs_2user_2controls.html#autotoc_md140", null ],
        [ "3D cameras", "md_docs_2user_2controls.html#autotoc_md141", null ],
        [ "Mouse in the match view", "md_docs_2user_2controls.html#autotoc_md142", null ],
        [ "Play mode", "md_docs_2user_2controls.html#autotoc_md143", null ]
      ] ],
      [ "Where your files are", "md_docs_2user_2controls.html#autotoc_md144", null ]
    ] ],
    [ "Saves, settings and troubleshooting", "md_docs_2user_2files-and-troubleshooting.html", [
      [ "Recover a save", "md_docs_2user_2files-and-troubleshooting.html#autotoc_md146", null ],
      [ "Report a problem", "md_docs_2user_2files-and-troubleshooting.html#autotoc_md147", null ],
      [ "Use an isolated test career", "md_docs_2user_2files-and-troubleshooting.html#autotoc_md148", null ],
      [ "Player12 and earlier careers", "md_docs_2user_2files-and-troubleshooting.html#autotoc_md149", null ]
    ] ],
    [ "Getting started", "md_docs_2user_2getting-started.html", [
      [ "1. Start a career", "md_docs_2user_2getting-started.html#autotoc_md151", null ],
      [ "2. Your first day", "md_docs_2user_2getting-started.html#autotoc_md152", null ],
      [ "3. Get to know the squad", "md_docs_2user_2getting-started.html#autotoc_md153", null ],
      [ "4. Lineup and tactics", "md_docs_2user_2getting-started.html#autotoc_md154", null ],
      [ "5. Training and staff", "md_docs_2user_2getting-started.html#autotoc_md155", null ],
      [ "6. Scouting and transfers", "md_docs_2user_2getting-started.html#autotoc_md156", null ],
      [ "7. The youth academy", "md_docs_2user_2getting-started.html#autotoc_md157", null ],
      [ "8. Advancing the calendar", "md_docs_2user_2getting-started.html#autotoc_md158", null ],
      [ "9. Match day", "md_docs_2user_2getting-started.html#autotoc_md159", [
        [ "3D camera controls", "md_docs_2user_2getting-started.html#autotoc_md160", null ]
      ] ],
      [ "10. Beyond the first season", "md_docs_2user_2getting-started.html#autotoc_md161", null ],
      [ "Tips", "md_docs_2user_2getting-started.html#autotoc_md162", null ]
    ] ],
    [ "In-game editor", "md_docs_2user_2in-game-editor.html", null ],
    [ "Install and run", "md_docs_2user_2installation.html", [
      [ "Download and play", "md_docs_2user_2installation.html#autotoc_md165", null ],
      [ "Build from source", "md_docs_2user_2installation.html#autotoc_md166", [
        [ "Linux", "md_docs_2user_2installation.html#autotoc_md167", null ],
        [ "macOS", "md_docs_2user_2installation.html#autotoc_md168", null ],
        [ "Windows", "md_docs_2user_2installation.html#autotoc_md169", null ],
        [ "Other build options", "md_docs_2user_2installation.html#autotoc_md170", null ]
      ] ],
      [ "Running", "md_docs_2user_2installation.html#autotoc_md171", null ]
    ] ],
    [ "Worlds: play, build and share", "md_docs_2user_2world-editor.html", [
      [ "Start a career in another world", "md_docs_2user_2world-editor.html#autotoc_md173", null ],
      [ "Build a world", "md_docs_2user_2world-editor.html#autotoc_md174", null ],
      [ "Problems", "md_docs_2user_2world-editor.html#autotoc_md175", null ],
      [ "Combine worlds", "md_docs_2user_2world-editor.html#autotoc_md176", null ],
      [ "Save and share", "md_docs_2user_2world-editor.html#autotoc_md177", null ]
    ] ],
    [ "Feature tour", "md_docs_2project_2features.html", [
      [ "Screenshots", "md_docs_2project_2features.html#autotoc_md179", null ]
    ] ],
    [ "Roadmap", "md_docs_2project_2roadmap.html", [
      [ "Start from what already works", "md_docs_2project_2roadmap.html#autotoc_md181", null ],
      [ "First priority: make existing decisions easier", "md_docs_2project_2roadmap.html#autotoc_md182", null ],
      [ "Release confidence and football quality", "md_docs_2project_2roadmap.html#autotoc_md183", null ],
      [ "In-game editing: safe changes before arbitrary worlds", "md_docs_2project_2roadmap.html#autotoc_md184", [
        [ "1. Edit supported data for a new career", "md_docs_2project_2roadmap.html#autotoc_md185", null ],
        [ "2. Make creations shareable and recoverable", "md_docs_2project_2roadmap.html#autotoc_md186", null ],
        [ "3. Separate countries from today's league-pyramid convention", "md_docs_2project_2roadmap.html#autotoc_md187", null ],
        [ "4. Expose competition rules, then expand supported formats", "md_docs_2project_2roadmap.html#autotoc_md188", null ]
      ] ],
      [ "Friendlies, invitations and summer competitions", "md_docs_2project_2roadmap.html#autotoc_md189", null ],
      [ "Community building: people and ownership", "md_docs_2project_2roadmap.html#autotoc_md190", null ],
      [ "Turn the roadmap into a contribution", "md_docs_2project_2roadmap.html#autotoc_md191", null ]
    ] ],
    [ "Add yourself as a player", "md_docs_2contributing_2player-cameo.html", [
      [ "1. Add the player in the World editor", "md_docs_2contributing_2player-cameo.html#autotoc_md193", null ],
      [ "2. Or add one line by hand", "md_docs_2contributing_2player-cameo.html#autotoc_md194", null ],
      [ "3. Check it in a new career", "md_docs_2contributing_2player-cameo.html#autotoc_md195", null ],
      [ "4. Include it in your contribution", "md_docs_2contributing_2player-cameo.html#autotoc_md196", null ]
    ] ],
    [ "Namespaces", "namespaces.html", [
      [ "Namespace List", "namespaces.html", "namespaces_dup" ],
      [ "Namespace Members", "namespacemembers.html", [
        [ "All", "namespacemembers.html", "namespacemembers_dup" ],
        [ "Functions", "namespacemembers_func.html", "namespacemembers_func" ],
        [ "Variables", "namespacemembers_vars.html", "namespacemembers_vars" ],
        [ "Typedefs", "namespacemembers_type.html", null ],
        [ "Enumerations", "namespacemembers_enum.html", null ],
        [ "Enumerator", "namespacemembers_eval.html", null ]
      ] ]
    ] ],
    [ "Concepts", "concepts.html", "concepts" ],
    [ "Classes", "annotated.html", [
      [ "Class List", "annotated.html", "annotated_dup" ],
      [ "Class Index", "classes.html", null ],
      [ "Class Hierarchy", "hierarchy.html", "hierarchy" ],
      [ "Class Members", "functions.html", [
        [ "All", "functions.html", "functions_dup" ],
        [ "Functions", "functions_func.html", "functions_func" ],
        [ "Variables", "functions_vars.html", "functions_vars" ],
        [ "Typedefs", "functions_type.html", null ],
        [ "Enumerations", "functions_enum.html", null ],
        [ "Enumerator", "functions_eval.html", null ],
        [ "Related Symbols", "functions_rela.html", null ]
      ] ]
    ] ],
    [ "Files", "files.html", [
      [ "File List", "files.html", "files_dup" ],
      [ "File Members", "globals.html", [
        [ "All", "globals.html", "globals_dup" ],
        [ "Functions", "globals_func.html", "globals_func" ],
        [ "Variables", "globals_vars.html", null ],
        [ "Typedefs", "globals_type.html", null ],
        [ "Enumerations", "globals_enum.html", null ],
        [ "Enumerator", "globals_eval.html", null ],
        [ "Macros", "globals_defs.html", null ]
      ] ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"SQLLoader_8cpp.html",
"buyer__negotiation_8h.html#adcfa219ffeb419f93b6ca37c3b7c0d6aafe44ca8c56b1c2ef63141feeb23791a4",
"classAudio_1_1OnePole.html",
"classBrainFlowScene.html#a81ba911cf64d30014e53202b55fc9283",
"classContactViewScene.html#a91bb461b928da4d4a99114fc74c41339",
"classDelegationPolicy.html#ad5250c8d962af03889135f14b27e08b0",
"classEngineDebugger.html#a2426b5cc9d4443ffb57ceae4573aa83d",
"classGame.html#a6b13fbbe3016cc6b2cc47dec3cbe1211",
"classGameController.html#a71a3cd827f2e6d6a953898cc62f54ef7",
"classGameData.html#a02aba4cd6aaef8cd1e8ebe80a06948af",
"classGameXgTable.html#a412b63d4528e118b6ec20493cf2d8ddc",
"classInternationalScene.html#a02c7c99883d62ca3bf168bd85ed09c55",
"classMainGameScene.html#ab2dea8e7dc24655cfa538948bde6769c",
"classManagerScene.html#a8b15a323105ef5db7ba335bd772bae91",
"classMatchEngine.html#a07f4dca5f9f219933bcb554cfb3b8e14",
"classMatchEngine.html#a3fee1894088048be4165452d789a7aae",
"classMatchEngine.html#a77e1e2c376a36e104c51ea571395dc58",
"classMatchEngine.html#aadc650ee3181bfa70dbd37589fb1fe0d",
"classMatchEngine.html#af01d53cde02f73e3202e8229ac94ef08",
"classMatchReview.html#a548136c70618854cc1941406f76017b4",
"classMatchScene.html#ae9eba81443ab208d2222cab655bc7c86",
"classNationalManagement.html#a8a6ba63c37906847988c177e0eb93a3c",
"classPlayer.html#a56423aa8e7b773a080a0477f40e63230",
"classRecordsScene.html",
"classSandboxRecorder.html#a4b5db52d33ff5a0a828d9d008e01772e",
"classScoutingSystem.html#a2d4fafeeab85faa281569032d469a591",
"classShotMap.html#af61e9b3b0ed4110ee2a9ef56436d6fd9",
"classStoryEngine.html#a308c6f9689c580e6428e1b79d4e6118c",
"classTechniqueBenchScene.html#a316e58dbbb18a6a0de051fcfccebe546",
"classTransferMarket.html#a4717aac30ac3ef4581b12d1abee34128",
"classTrueViewScene.html#abdae314d346dfbf1506c38a0e4b7f50b",
"classWorldSimulation.html#aa049834c54439cc9eb70f5ec1d840479",
"classYouthAcademy.html#ae39b48ed53a9a7d8cb68ed173c70c6cc",
"continental_8h.html#a7bb4accaa23e3f2aac7ad22253036f33",
"formation_8h.html",
"gui__scene_8h.html#aaf4bddc3ff70ab62a07943e024b9942fabcacee7ad79abe4e25b877c5b6480a8c",
"input__actions_8h.html#acbe346853712ec9e1a604159e12c5ba2",
"lab__stats_8cpp.html#a6fad31e96727cded38e20fd491a0849b",
"loan__negotiation_8h.html#a335a9bb17533ea37c418b45adde6a495a4905ac9d6a22bdfc1ae096094ce6248d",
"match__ball__physics__aardvark__members_8h.html#a121e08b5ca0027cb089aa86925c4d494",
"match__brain__aardvark__members_8h.html",
"match__brain__flow_8h.html#acbbd858a8f96db20f15770489ef745a8aa98f646b268fee827a65b60ef51941d4",
"match__engine_8h.html#a6a1652225e18bfaff8ecb9f9563a6a52a8a72a76fa43f54561a486c3b96c045bf",
"match__first__touch__knobs_8h.html#a1cbcef95209a1d93d71493b72d7d421cafc0dfd5b8dd842472ded850e212fb59b",
"match__pitch_8h.html#aef21703ff4b0f31a5df970eb7c7a9f80",
"match__recorder_8h.html#a481d04d95a95a6747966a74fe53b4666a2a8eb3b0974dc3508ba003d3f7a37e52",
"match__sandbox__game__xg_8h_source.html",
"match__sandbox__true__view__shapes_8cpp.html#aaa928c9a62449f7946da1e32f66c70d2",
"match__strike__knobs_8h.html#ac8f4f586c44035afce9b9c927aff2441ab05f0bcd52f96e161eff3ae0a57d2e66",
"medical__centre_8h.html#a39e9ab7cfbd0dae353271a4b4fd359a4a4d902e0ff898789b1d69223dab2464d3",
"namespaceCareerTimeline.html#aaacc94008ff614afeb6c688409be1d49",
"namespaceInput_1_1Ids.html#a18e1794a6132c675958bc5bb32112021",
"namespaceMatchAnalysisRules.html#accb6357967dd44b33db530f9b790e695",
"namespaceNextActionRules.html#a2b36e319f3d93cb4690bf692ffd220e8",
"namespaceSandboxPageUI.html#a3286de283ce48402b05d8f25a2a0e23b",
"namespaceStandings.html#a90c45eee77f26baf8bd9ff4c7fcb1311a6f9dccd85b2e0786c8d522045365eb48",
"namespaceTransferNegotiation.html#ac16348065bc0972cf21588fc7f36cd50a1e4b5845e251e1b87a8069be79ed7d92",
"namespaceWorld.html#a433d78f5444ca5c8d9ab1ad37cbb5d54a52835cee8028f45f4bcd546afd78ddef",
"namespacemembers_vars_z.html",
"onboarding__overlay_8h.html#a1dcf127cb10a8cbdc628d65a2ed10526",
"runtime__editor_8h.html#aa35fdbc113e7c179159058544b83c0f9a06933067aafd48425d67bcb01bba5cb6",
"season__review_8h.html#a131dbac78bf5923c84fe7a1c1409ac53aaf59bb7c08d6527bb26a798b7acf0ec5",
"stories_8h.html#a57dc725ce2aabeb226169f6518cb002aae9c626b8a8b62de7562dfbf51a576ae5",
"structBenchCell.html#a5b35601b3a1301cec1bf3ba7bccee82c",
"structBrainFlow_1_1Weight.html#a27b1985b9ca4cf5ecae9a772382169eb",
"structCompetitionView_1_1FixtureRow.html#a069d0777cb32ad8b1561ac971b722957",
"structContactSituation.html#a36bd8d0a3ba28c187409863ff1f6ad3c",
"structDefensiveCoverCapybara.html#af0ba4676c6c9b869ddae90b419c0dabc",
"structFollowedPlayer.html#a92934be58245e9aaeab84f3c3491a207",
"structGameDateValue.html#a4a370d1a6d40686b48a36aa8a8da78ec",
"structInboxScene_1_1Decision.html#a7664947e325ebc122943ec43e99df5fa",
"structInternational_1_1Group.html#a58b4e4d71d7a010d133d4ecdf0126dfd",
"structLab_1_1MatchSample.html#a125a1b61e792f8108923881c9be66794",
"structLab_1_1MatchSample.html#a9bea2a27c6c179173a11fdf4aa8b4cb3",
"structLab_1_1SeasonVitals.html#aa4cd0d3bda84e684dbde2aa042558218",
"structLooseCallResultCapybara.html#a2e8c6949c30ef5fd778b0d4faa8c6e4a",
"structMatchAudio_1_1PendingShot.html",
"structMatchDuelDetail.html#ace6b2746a54bdcdc72026afe17cd097c",
"structMatchEngine_1_1PassOption.html#a9e347050558f4ff983b6746c3b92dcb1",
"structMatchInsightsView_1_1StepPoint.html",
"structMatchRecording.html#aecba2025f3980e9fb1ba87af9200ba7a",
"structMatchRender3DTuning_1_1Crowd.html#a9bfff30ea243e148700a260f6ee3177c",
"structMatchRender3DTuning_1_1Number.html#a8933d53d7fc6b716fe57b1b8100dcf2c",
"structMatchRender3DTuning_1_1TouchlineFigure.html#a4c46783a999803b14edbb46f0e79e2eb",
"structMatchRenderer3D_1_1State.html#a080613a6bae852082c976b0143b724f8",
"structMatchRules_1_1TackleContext.html#ade68207ae37ac617a332697298856887",
"structMatchStats.html#a08d9854a27c76a9806ac2b8957f3616f",
"structMatchTuning_1_1Aerial.html#a58c5154f9d5ebafc0c3a3eaa7b6c8bab",
"structMatchTuning_1_1Decision.html#a0979dbd0e508339c30db40c7a3519046",
"structMatchTuning_1_1FirstTouch.html#a04d41f6e6e613067b1d9c859ca27788b",
"structMatchTuning_1_1Passing.html#a376849fa57ae9e1ea698df43b391a9d1",
"structMatchTuning_1_1Rating.html#ac402db39b943207e6c5cfe899897880b",
"structMatchTuning_1_1Shooting.html#af9e56005cdccd8ae4db8858e2fa10e61",
"structMatchTuning_1_1Substitution.html#a6fe80e0f71dc9cdb13f5c2f27f4baf2f",
"structMigrations_1_1MigrationContext.html#a1a7202859845a46f49e037fdaf5fb9e4",
"structNextActionFacts_1_1Report.html#a6bc0ada84d806e24dafe767a256478f9",
"structPlayerAgent_1_1Demands.html#a6e0710340c1fb01b99243f4ce1c10ecf",
"structPlayerRig_1_1GaitBands.html",
"structPossessionSnapshot.html#a5109b381cb5c2bbc009fe087f35e93c7",
"structReservesScene_1_1PendingAction.html#a2797199235551caff44af755b37fd60aab50339a10e1de285ac99d4c3990b8693",
"structSaveMetadata.html#ac8ab8ad0f4ce36763d7a4aba4fe69721",
"structSeasonReview.html#acf745872bf532695f0a0e37b05f49223",
"structSocialGroup.html",
"structStadium3D_1_1Section.html#abe08108e7abdb1867c758074a0096525",
"structTalkContext.html#a2a55082ee9e09226164cb4553fcd0706",
"structTouchIntent.html#a601a7902e02bccdd63b674282d71b637",
"structTransferMarketScene_1_1SquadRow.html",
"structTransferNegotiation_1_1SquadNeeds.html",
"structTransferTuning_1_1Offer.html#abb867a829c95cd3fba02915b1d31b799",
"structUpcomingFixture.html#a7423d6bf1efa9bb1899b3872aa796cc9",
"structWorldTuning_1_1Finance.html#a69a9571fd36f9ea66c7891176d1e13e6",
"structWorld_1_1Manifest.html#a7af4150111f79cafc9a581be3afb4b85",
"synth_8h.html#a762d8fd8f8ffa3d59d7179dbd720170fab69afdf9ed927382cebbb4451cf16ca0",
"training_8cpp.html#a6882da818bed2deeab58667b24d8de11",
"transfer__negotiation_8h.html#ac16348065bc0972cf21588fc7f36cd50ac277a7276016a0618c2763e2924e2305",
"world__combine_8cpp.html#a9f8137f351e95e57db9287a91be72ac7",
"world__rng_8h.html#af6efe8392939d5480542934d21ee61b5"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';