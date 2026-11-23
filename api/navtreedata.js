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
      [ "One authoritative simulation step", "md_docs_2development_2match-engine.html#autotoc_md51", null ],
      [ "Choosing an advance API", "md_docs_2development_2match-engine.html#autotoc_md52", null ],
      [ "Ball flight, contacts and rules", "md_docs_2development_2match-engine.html#autotoc_md53", [
        [ "Calibrating finishing without changing keeper reach", "md_docs_2development_2match-engine.html#autotoc_md54", null ]
      ] ],
      [ "Determinism, replay and observation", "md_docs_2development_2match-engine.html#autotoc_md55", null ],
      [ "Implementation map and checks", "md_docs_2development_2match-engine.html#autotoc_md56", null ]
    ] ],
    [ "Player behavior: from attributes to actions", "md_docs_2development_2player-behavior.html", [
      [ "Persistent player versus match slot", "md_docs_2development_2player-behavior.html#autotoc_md58", null ],
      [ "Behavior between matches", "md_docs_2development_2player-behavior.html#autotoc_md59", null ],
      [ "Team phase and coordinated assignments", "md_docs_2development_2player-behavior.html#autotoc_md60", null ],
      [ "Shape, roles and duties", "md_docs_2development_2player-behavior.html#autotoc_md61", null ],
      [ "Off-ball intentions and physical movement", "md_docs_2development_2player-behavior.html#autotoc_md62", null ],
      [ "On-ball choice and execution", "md_docs_2development_2player-behavior.html#autotoc_md63", null ],
      [ "Defending, keepers and external control", "md_docs_2development_2player-behavior.html#autotoc_md64", null ],
      [ "Reproducing a suspicious decision", "md_docs_2development_2player-behavior.html#autotoc_md65", null ]
    ] ],
    [ "Building and releasing desktop packages", "md_docs_2development_2release.html", [
      [ "Development builds for main commits", "md_docs_2development_2release.html#autotoc_md70", null ],
      [ "Cutting a release", "md_docs_2development_2release.html#autotoc_md71", null ],
      [ "What each job does", "md_docs_2development_2release.html#autotoc_md72", null ],
      [ "Package layout and runtime paths", "md_docs_2development_2release.html#autotoc_md73", [
        [ "Crash reports and save recovery", "md_docs_2development_2release.html#autotoc_md74", null ]
      ] ],
      [ "Building a package locally (Linux)", "md_docs_2development_2release.html#autotoc_md75", null ],
      [ "Known limitations", "md_docs_2development_2release.html#autotoc_md76", null ]
    ] ],
    [ "Runtime editor architecture", "md_docs_2development_2runtime-editor.html", [
      [ "Persistence", "md_docs_2development_2runtime-editor.html#autotoc_md78", null ],
      [ "Runtime replacement", "md_docs_2development_2runtime-editor.html#autotoc_md79", null ],
      [ "Preserving custom data", "md_docs_2development_2runtime-editor.html#autotoc_md80", null ],
      [ "Extending a page or feature", "md_docs_2development_2runtime-editor.html#autotoc_md81", null ],
      [ "Checks", "md_docs_2development_2runtime-editor.html#autotoc_md82", null ]
    ] ],
    [ "Tests and tools", "md_docs_2development_2testing.html", [
      [ "Run one area first", "md_docs_2development_2testing.html#autotoc_md84", null ],
      [ "Balance lab", "md_docs_2development_2testing.html#autotoc_md85", null ],
      [ "Screenshots", "md_docs_2development_2testing.html#autotoc_md86", null ],
      [ "Performance benchmarks", "md_docs_2development_2testing.html#autotoc_md87", null ],
      [ "Historical calibration failures", "md_docs_2development_2testing.html#autotoc_md88", null ]
    ] ],
    [ "Website documentation rendering", "md_docs_2development_2website.html", [
      [ "Lists and code examples", "md_docs_2development_2website.html#autotoc_md90", null ],
      [ "Diagram sizing and themes", "md_docs_2development_2website.html#autotoc_md91", null ],
      [ "Rendering repair walkthrough", "md_docs_2development_2website.html#autotoc_md92", null ],
      [ "Media and publication size", "md_docs_2development_2website.html#autotoc_md93", null ]
    ] ],
    [ "Controls", "md_docs_2user_2controls.html", [
      [ "Anywhere in the game", "md_docs_2user_2controls.html#autotoc_md95", null ],
      [ "Main menu and club choice", "md_docs_2user_2controls.html#autotoc_md96", null ],
      [ "Management screens", "md_docs_2user_2controls.html#autotoc_md97", [
        [ "Back and Forward", "md_docs_2user_2controls.html#autotoc_md98", null ],
        [ "Sidebar hubs", "md_docs_2user_2controls.html#autotoc_md99", null ],
        [ "Mouse on management screens", "md_docs_2user_2controls.html#autotoc_md100", null ]
      ] ],
      [ "Match day", "md_docs_2user_2controls.html#autotoc_md101", [
        [ "Playback", "md_docs_2user_2controls.html#autotoc_md102", null ],
        [ "Substitutions board and tactics", "md_docs_2user_2controls.html#autotoc_md103", null ],
        [ "Touchline shouts", "md_docs_2user_2controls.html#autotoc_md104", null ],
        [ "3D cameras", "md_docs_2user_2controls.html#autotoc_md105", null ],
        [ "Mouse in the match view", "md_docs_2user_2controls.html#autotoc_md106", null ],
        [ "Play mode", "md_docs_2user_2controls.html#autotoc_md107", null ]
      ] ],
      [ "Where your files are", "md_docs_2user_2controls.html#autotoc_md108", null ]
    ] ],
    [ "Saves, settings and troubleshooting", "md_docs_2user_2files-and-troubleshooting.html", [
      [ "Recover a save", "md_docs_2user_2files-and-troubleshooting.html#autotoc_md110", null ],
      [ "Report a problem", "md_docs_2user_2files-and-troubleshooting.html#autotoc_md111", null ],
      [ "Use an isolated test career", "md_docs_2user_2files-and-troubleshooting.html#autotoc_md112", null ],
      [ "Player12 and earlier careers", "md_docs_2user_2files-and-troubleshooting.html#autotoc_md113", null ]
    ] ],
    [ "Getting started", "md_docs_2user_2getting-started.html", [
      [ "1. Start a career", "md_docs_2user_2getting-started.html#autotoc_md115", null ],
      [ "2. Your first day", "md_docs_2user_2getting-started.html#autotoc_md116", null ],
      [ "3. Get to know the squad", "md_docs_2user_2getting-started.html#autotoc_md117", null ],
      [ "4. Lineup and tactics", "md_docs_2user_2getting-started.html#autotoc_md118", null ],
      [ "5. Training and staff", "md_docs_2user_2getting-started.html#autotoc_md119", null ],
      [ "6. Scouting and transfers", "md_docs_2user_2getting-started.html#autotoc_md120", null ],
      [ "7. The youth academy", "md_docs_2user_2getting-started.html#autotoc_md121", null ],
      [ "8. Advancing the calendar", "md_docs_2user_2getting-started.html#autotoc_md122", null ],
      [ "9. Match day", "md_docs_2user_2getting-started.html#autotoc_md123", [
        [ "3D camera controls", "md_docs_2user_2getting-started.html#autotoc_md124", null ]
      ] ],
      [ "10. Beyond the first season", "md_docs_2user_2getting-started.html#autotoc_md125", null ],
      [ "Tips", "md_docs_2user_2getting-started.html#autotoc_md126", null ]
    ] ],
    [ "In-game editor", "md_docs_2user_2in-game-editor.html", null ],
    [ "Install and run", "md_docs_2user_2installation.html", [
      [ "Download and play", "md_docs_2user_2installation.html#autotoc_md129", null ],
      [ "Build from source", "md_docs_2user_2installation.html#autotoc_md130", [
        [ "Linux", "md_docs_2user_2installation.html#autotoc_md131", null ],
        [ "macOS", "md_docs_2user_2installation.html#autotoc_md132", null ],
        [ "Windows", "md_docs_2user_2installation.html#autotoc_md133", null ],
        [ "Other build options", "md_docs_2user_2installation.html#autotoc_md134", null ]
      ] ],
      [ "Running", "md_docs_2user_2installation.html#autotoc_md135", null ]
    ] ],
    [ "Worlds: play, build and share", "md_docs_2user_2world-editor.html", [
      [ "Start a career in another world", "md_docs_2user_2world-editor.html#autotoc_md137", null ],
      [ "Build a world", "md_docs_2user_2world-editor.html#autotoc_md138", null ],
      [ "Problems", "md_docs_2user_2world-editor.html#autotoc_md139", null ],
      [ "Combine worlds", "md_docs_2user_2world-editor.html#autotoc_md140", null ],
      [ "Save and share", "md_docs_2user_2world-editor.html#autotoc_md141", null ]
    ] ],
    [ "Feature tour", "md_docs_2project_2features.html", [
      [ "Screenshots", "md_docs_2project_2features.html#autotoc_md143", null ]
    ] ],
    [ "Roadmap", "md_docs_2project_2roadmap.html", [
      [ "Start from what already works", "md_docs_2project_2roadmap.html#autotoc_md145", null ],
      [ "First priority: make existing decisions easier", "md_docs_2project_2roadmap.html#autotoc_md146", null ],
      [ "Release confidence and football quality", "md_docs_2project_2roadmap.html#autotoc_md147", null ],
      [ "In-game editing: safe changes before arbitrary worlds", "md_docs_2project_2roadmap.html#autotoc_md148", [
        [ "1. Edit supported data for a new career", "md_docs_2project_2roadmap.html#autotoc_md149", null ],
        [ "2. Make creations shareable and recoverable", "md_docs_2project_2roadmap.html#autotoc_md150", null ],
        [ "3. Separate countries from today's league-pyramid convention", "md_docs_2project_2roadmap.html#autotoc_md151", null ],
        [ "4. Expose competition rules, then expand supported formats", "md_docs_2project_2roadmap.html#autotoc_md152", null ]
      ] ],
      [ "Friendlies, invitations and summer competitions", "md_docs_2project_2roadmap.html#autotoc_md153", null ],
      [ "Community building: people and ownership", "md_docs_2project_2roadmap.html#autotoc_md154", null ],
      [ "Turn the roadmap into a contribution", "md_docs_2project_2roadmap.html#autotoc_md155", null ]
    ] ],
    [ "Add yourself as a player", "md_docs_2contributing_2player-cameo.html", [
      [ "1. Add the player in the World editor", "md_docs_2contributing_2player-cameo.html#autotoc_md157", null ],
      [ "2. Or add one line by hand", "md_docs_2contributing_2player-cameo.html#autotoc_md158", null ],
      [ "3. Check it in a new career", "md_docs_2contributing_2player-cameo.html#autotoc_md159", null ],
      [ "4. Include it in your contribution", "md_docs_2contributing_2player-cameo.html#autotoc_md160", null ]
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
"buyer__negotiation_8h.html#addc3888f5ea8dc22b1e8dd29978f25af",
"classAudio_1_1OnePole.html#a43e5ff445f42c4522c1156e1a214ef40",
"classCallUpScene.html#a213eaa8d59f57dcba631bdc5c47a94af",
"classDataHubScene.html#ae80aae87064b9d789687073ede1defcf",
"classGUIView.html#a7ca40b58018713c5c91ab1e53e9a70e1",
"classGameController.html#a460628d35f3197c3ec08f70608872d71",
"classGameController.html#add36b9ab836aff933b8aad7bbc08d532",
"classInboxScene.html#a5ebf78a840fa040a3b688244a6b65093",
"classLineup.html#a2bdb3fa69b6d69f5a9d0a4fcfcb9168c",
"classManagementScene.html#ac13531bcdfae42fbcb37da8d6fafc5a1",
"classMatchAudio.html#a25efef9014a482e829538a12de6ae6f6",
"classMatchEngine.html#a5b10c57ce2e4ffed4ce7b93cab5c8342",
"classMatchEngine.html#aec946066a319038c31cf2f746830a3ef",
"classMatchScene.html#a64f690f01a441cc62e111f644101ce2f",
"classMentoringDialog.html#aa30f05ad7a3fe1dc926db4f3ee62250d",
"classOnboardingState.html#a01cdc7aee5e203443cbf92e5ed90d5e6",
"classPreseasonScene.html#a23cab16b4ad5d607181b042cfab050b3",
"classRuntimeEditor_1_1Extensions.html#ac29065530ff91251a065c4e80384b7e0",
"classScoutingSystem.html#a997d2fdd597c134f293f5c55242a043a",
"classStandingsScene.html#a48e933d1f6fd5af592f6b7da3efcd10c",
"classTeamTalkDialog.html#a0da054952be9061938bb7ed12e382594",
"classTransferMarketScene.html",
"classWorldSimulation.html#a53f2051c590d24977da4aaef40148069",
"classYouthScene.html#a5c5c1740f2fba5908ad1ff421ad771df",
"delegation_8h.html#a0eacc1250f4a2bce7ad69f0e0677202f",
"game_8cpp.html",
"inbox_8h.html#a31343273402baf03a41b70c99567b786a0db377921f4ce762c62526131097968f",
"interactions_8h.html#a79a2ca2ecc5f303378fb6bec918261ad",
"languages_8h.html#a15be38758ec16bd1229f53bd53c7084da928568b84963ceb76aaaa2cae9afdbfa",
"manager__career_8h.html#a5800ef10bd1343c8f0860ddb0123461d",
"match__engine_8h.html#aca6448151e924d2cb062632532d9081fa526aadb8564a9c50374ebe55ec7bade0",
"match__renderer__3d_8cpp.html#a903de250aeb5b82b87cb1b10bc4b1431",
"md_docs_2development_2website.html",
"namespaceBuyerNegotiation.html#a42e1f8a9cc41c633e8198dac54ef7eba",
"namespaceInput.html#abfea56fea6f4ed91cded855d88b364fc",
"namespaceMatchAnalysisRules.html#ae7802f08785ddb1b04ecb13b04e2bcf5",
"namespacePlayerRig.html#aa8ee7148b8793b8c1a0ca832f10fef9ea7a95bf926a0333f57705aeac07a362a2",
"namespaceStadium3D.html",
"namespaceTrainingModel.html#accb06dfa9e3533e6c69015ee7af4a8d1",
"namespaceWorld.html#a263d5d3f76d6868c4964786fefd63221a9b4c09ffb449353fdc8f593ad71cf885",
"namespacemembers_vars_k.html",
"onboarding_8h.html#a90ff1381c79d6b3eb3a853e22f612426ae009c8bb30471b76045da5c6ecf0b6fc",
"runtime__editor_8h.html",
"season__history_8h.html#ac058440ba0dfedb8c9794c12efd11a2b",
"stories_8h.html#a57dc725ce2aabeb226169f6518cb002aa08a5e15f7771c20460b0a7eb032960be",
"structBoardState.html",
"structCompetitions_1_1CupDraw.html#a1418a30299f44524fd59015a0f6b4484",
"structDilemma.html#ab101ed6c27a6cd7f96fdbe6978292498",
"structGameController_1_1IncomingOfferView.html#a27c890d509f0d6a280310c8432928e6d",
"structHolidaySummary.html#aaecb1d6ddfa7785bf1e17e20322b709b",
"structInternational_1_1Fixture.html#a0eb9bb5000c2088b87ae41f4b47bee58",
"structLab_1_1MatchSample.html#abc471c662b43a311a93ca4eccca4ff29",
"structLeagueProfile.html#a54c1a8690365b8b863554d6aee8b4c76",
"structManagerStint.html",
"structMatchEngine_1_1ShoutState.html#ada938bf6bd3963abcfafc51d72dcdbf3",
"structMatchRender3DTuning_1_1Ball.html#a032c913060622bf86d97c72cea7760b4",
"structMatchRender3DTuning_1_1Goal.html#ab9249778e4cba9ea9bd9feddd7572f83",
"structMatchRender3DTuning_1_1Ripple.html#a09c4469180d0db36ff6f426059e03616",
"structMatchRenderer3D_1_1State.html#a31fd3e2cfa572a1bee53995f402f83d3",
"structMatchSceneTuning_1_1Marker.html#a60b069bab89cb04766beadf1ba17ea25",
"structMatchTuning_1_1Ball.html#a84fa9f4bd825530c49e9e088e6c4f529",
"structMatchTuning_1_1Goalkeeper.html#a9ec5416b3e43702e67e54628e4917c3e",
"structMatchTuning_1_1Rules.html#ad8ef3780dec1c4169b405c988cb5c661",
"structMatchTuning_1_1Timing.html#a1e001b232d29d8373fb768638e45a603",
"structNationalStint.html#af2d3c1f86f30edb3d814a5aae0b5f648",
"structPassDecision.html#a83a7c165faaa3659a4e15be91059af10",
"structPlayerRig_1_1FootState.html",
"structPromise.html#a2007cdf242554bcbb191d70424b0fdde",
"structRuntimeEditor_1_1Page.html",
"structScoutingScene_1_1ScoutLine.html",
"structSquadPlan.html#a4acd7cf13a02194244df048cc2451a30",
"structStaffScene_1_1PendingAction.html#a343b6bab4606e0f7bc8143f665289ab3afb3e95efc27c9a8cac26dc3a59286c9a",
"structTeamSelectionScene_1_1ClubSummary.html#a31bf8f099a6c7f4b06753cebb3334b20",
"structTransferMarketSceneTuning_1_1Layout.html#a9cb3ef5b2e312896bd620ad03ebe80dd",
"structTransferNegotiation_1_1LoanTerms.html#ab24cd1f88db0fbf1efcc6bdd0dbf32cd",
"structTransferTuning_1_1Market.html#a32c968d58c8c65e1742c85728790dbc4",
"structWorldEditorScene_1_1State.html#a25dad3ae7b4b9bebc7bbf42add0678c4",
"structWorldTuning_1_1Morale.html#a03442e2f29083d70a97b081e6297875a",
"structYouthRecord.html#a229bc264fc4ecff4d17287afda81bd4f",
"text__encoding_8h.html",
"transfer__market__scene_8h_source.html",
"widgets_8cpp.html#aac23b6d3c883c523565edc1938ca2bb4",
"world__geography_8h.html#ae9306365326b548999e28110a4d10c7a",
"youth__academy_8cpp.html#a7c3b29cb79873dcbeca84853ff0026cf"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';