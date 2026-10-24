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
    [ "Feature tour", "md_docs_2project_2features.html", [
      [ "Screenshots", "md_docs_2project_2features.html#autotoc_md137", null ]
    ] ],
    [ "Roadmap", "md_docs_2project_2roadmap.html", [
      [ "Start from what already works", "md_docs_2project_2roadmap.html#autotoc_md139", null ],
      [ "First priority: make existing decisions easier", "md_docs_2project_2roadmap.html#autotoc_md140", null ],
      [ "Release confidence and football quality", "md_docs_2project_2roadmap.html#autotoc_md141", null ],
      [ "In-game editing: safe changes before arbitrary worlds", "md_docs_2project_2roadmap.html#autotoc_md142", [
        [ "1. Edit supported data for a new career", "md_docs_2project_2roadmap.html#autotoc_md143", null ],
        [ "2. Make creations shareable and recoverable", "md_docs_2project_2roadmap.html#autotoc_md144", null ],
        [ "3. Separate countries from today's league-pyramid convention", "md_docs_2project_2roadmap.html#autotoc_md145", null ],
        [ "4. Expose competition rules, then expand supported formats", "md_docs_2project_2roadmap.html#autotoc_md146", null ]
      ] ],
      [ "Friendlies, invitations and summer competitions", "md_docs_2project_2roadmap.html#autotoc_md147", null ],
      [ "Community building: people and ownership", "md_docs_2project_2roadmap.html#autotoc_md148", null ],
      [ "Turn the roadmap into a contribution", "md_docs_2project_2roadmap.html#autotoc_md149", null ]
    ] ],
    [ "Add yourself as a player", "md_docs_2contributing_2player-cameo.html", [
      [ "1. Choose your club and an unused player ID", "md_docs_2contributing_2player-cameo.html#autotoc_md151", null ],
      [ "2. Create a player file", "md_docs_2contributing_2player-cameo.html#autotoc_md152", null ],
      [ "3. Check it in a new career", "md_docs_2contributing_2player-cameo.html#autotoc_md153", null ],
      [ "4. Include it in your contribution", "md_docs_2contributing_2player-cameo.html#autotoc_md154", null ]
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
        [ "Functions", "globals_func.html", null ],
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
"classAudio_1_1OnePole.html#ab00acf739f95b8f87be56d86f762f33f",
"classCallUpScene.html#a516fd8ce27b3f35e5b25d0c204248785",
"classDatabaseConnection.html#a9492092ac5ca5ebe58662e304e400601",
"classGUIView.html#aca2c06d5349f20054a22b8c232ed7b7e",
"classGameController.html#a534b33d84dea837b6b80d1894fcfc989",
"classGameController.html#ae588255ecbc93352f01d9ed96567e236",
"classInboxScene.html#ac747789b9334fb6079a05c036f4adab6",
"classLineup.html#a9485ac7458a201f6f7760757b7ec8690",
"classManagerCareer.html#a2c731b34dde88a3c504644c24c70ca55",
"classMatchAudio.html#ace3d8e07086012607e3f27213690f6faa5903d85aebc0be5e89acc60e22f7cf76",
"classMatchEngine.html#a67f4a88a88e1aee0b99f567a99ab52a3",
"classMatchEngine.html#afd3f43e7dc2db59ad2097e4a05f634e7",
"classMatchScene.html#a9838b9f36c987dc77cea8ea7965623e0",
"classMigrations_1_1FutureVersionError.html#add4bc2943ffc2d33d151f8a5215bceb7",
"classOppositionScene.html#a54a717840c44d59a7bcb6a66bd40f81e",
"classRecordBook.html",
"classRuntimePaths.html#ac9cd85a0b00c86812fc396a0c76a0462",
"classScoutingSystem.html#ae1ff1776fc16eedac9da2216dde98d5d",
"classStoryEngine.html#a0a26eb1ecf6408b7d43da8d3839be5bc",
"classThreadPool.html",
"classTransferMarketScene.html#a5b4ed12c2bb106ea2f45ca7e3929656f",
"classWorldStateRepository.html#a591bf1384bc568f90b0a872b3dde50e7",
"continental_8h.html#a0119cfd16d5abeec4b3f522b4cf870f2",
"formation_8h.html#ae13d3c7e8946d3cd01580511626dfe9d",
"holiday_8h.html#a67ed4fa6274cb8a48805cc8dded224b4a59d10f00ef825d354996126573dbb627",
"interactions_8cpp.html#ac3e2f477332a25094e28cd3975a5d302",
"languages_8h.html#ac401b05356bfe6d1335ddaf14ad31d0ca928568b84963ceb76aaaa2cae9afdbfa",
"match_8h.html#af87d89d39f9a047ea437d272ddc5d883",
"match__events_8h.html#a7c25aa006c3b414067b7239b8b60ea1ba90ddbfb474234a12e09063a62e100c31",
"match__scene_8cpp.html",
"medical__centre_8h.html#a39e9ab7cfbd0dae353271a4b4fd359a4a4d902e0ff898789b1d69223dab2464d3",
"namespaceCompetitionView.html#a4b6d0726d6e1ef6797edf0fa7a04cce4",
"namespaceInternational.html#a08f7e5d9f1e1eb6c432a6b13933ab72c",
"namespaceMatchShoutsBar.html#aed6b42ac11139078c7bbba5acbfce54f",
"namespaceRenderMath.html#a9f6627bd1676236f6a5df705bf3e1158",
"namespaceStories.html#a27cb273d08f1b21b177b822cbeb54eff",
"namespaceTransferNegotiation.html#ac16348065bc0972cf21588fc7f36cd50ac277a7276016a0618c2763e2924e2305",
"namespacemembers_vars.html",
"onboarding__overlay_8cpp.html",
"runtime__editor_8h.html#a5623cd9bce165da1f2836e0407306bbc",
"season__review_8cpp.html#a2680e3bbc006b73ceaa90393c4d94339",
"stories_8h.html#a57dc725ce2aabeb226169f6518cb002aa62d9e83f06b576e7ac3116112d3faa30",
"structBoardState.html#a5424b3d2a88daa38c9af051463e3bc08",
"structCompetitions_1_1CupDraw.html#aa1dc16b06abc6103de239a5e096047b9",
"structDisciplinaryRecord.html#a13f40f9d31cb58ea358555d34e75b9ec",
"structGameController_1_1NationalVacancyView.html#ad8cc85968076edbb6a7d07ed44d856c1",
"structInboxScene_1_1Decision.html#ae8dabd8f5ae1c7e5dcff382461963696",
"structInternational_1_1Group.html#aeeb3c39903d17e72197197c8da106678",
"structLab_1_1ReportRow.html#a6187a46a98c313fde91403b29f268477",
"structLoanDeal.html#a514fbc0d7e8a2f4cc84b2bee76d2c9d7",
"structMatchAudioFrame.html#ab288d1fb3d08e9fc97fdbe1f31a53032",
"structMatchInsightsView_1_1MomentRow.html#a2908615daabd68f91764da27df6b9a4b",
"structMatchRender3DTuning_1_1Camera.html#a35cba57c92aadb043a57f87ebbb59438",
"structMatchRender3DTuning_1_1Hud.html#abfc4aad7adac743a4c812eb42a4c1bfc",
"structMatchRender3DTuning_1_1Stands.html#a093ed2e987535acb42123c47d4637689",
"structMatchRenderer3D_1_1State.html#a9932ae2203703759d2393641b546f6cd",
"structMatchSceneTuning_1_1Pitch.html#a646bbf0ebd8e22af1e8551d3b0ccd287",
"structMatchTuning_1_1Control.html#a2d6908ef2afa062eeded8d762d0f6d9f",
"structMatchTuning_1_1Passing.html#a2587b4036b08ee93ac12b2edc408c823",
"structMatchTuning_1_1Shape.html#a05ec8082bb4d9c7a201651833d5dc87a",
"structMatchTuning_1_1Touchline.html#a57cbe5480eb267e53a7fd48b83483feb",
"structNavEntry.html#a470574f8c06a883167e1f698e39d5828",
"structPlayerAgent_1_1Demands.html#a9f331400228b01ec8b89cac494a6fb4e",
"structPlayerRig_1_1KickTiming.html#aaf271fc4eaded5afef67a602a14b58e9",
"structRecordsScene_1_1PlayerLine.html#aea5b1ef77381e36c6d814c25a65e5394",
"structRuntimeEditor_1_1Table.html",
"structSeasonHistoryEntry.html#a0541a7d97c2560b45b76488d1bfd3b72",
"structSquadPlannerScene_1_1View.html#a4811e3637a555d6904ba7e7e82f3dce0",
"structStandingsScene_1_1ScorerRow.html#aa41ec67496d6f247d1aef0e7581c7794",
"structTeamTalkDialog_1_1ToneRow.html#ac718a39858e10a0c70ae8f02cc43d5ae",
"structTransferMarketScene_1_1ListingDialog.html#a1763588f4fd86cd0705ef1087e8c2228",
"structTransferNegotiation_1_1PlayerContext.html#a978b7e00de9f448be0b4a1bba54eba72",
"structTransferTuning_1_1Market.html#ae630e2d0d2e8bf57886ab154dbb9a425",
"structWorldTuning_1_1FreeAgents.html",
"tactics_8h.html#a37cc7e29f7184926000e74743f17cd8f",
"training_8h.html#aa57d4321e7a1ad4a9df082bab322ec48a0f78b20318b5aef4dca6dbae5051960a",
"types_8h.html#a06be3d3918a8700cf086896e9f03e885af6a34a956d1ce0a552bf4731b9f9d9c0",
"youth__academy_8cpp.html#a83274e466ee3b53e21dba7768f9843d0"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';