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
      [ "Cutting a release", "md_docs_2development_2release.html#autotoc_md70", null ],
      [ "What each job does", "md_docs_2development_2release.html#autotoc_md71", null ],
      [ "Package layout and runtime paths", "md_docs_2development_2release.html#autotoc_md72", [
        [ "Crash reports and save recovery", "md_docs_2development_2release.html#autotoc_md73", null ]
      ] ],
      [ "Building a package locally (Linux)", "md_docs_2development_2release.html#autotoc_md74", null ],
      [ "Known limitations", "md_docs_2development_2release.html#autotoc_md75", null ]
    ] ],
    [ "Runtime editor architecture", "md_docs_2development_2runtime-editor.html", [
      [ "Persistence", "md_docs_2development_2runtime-editor.html#autotoc_md77", null ],
      [ "Runtime replacement", "md_docs_2development_2runtime-editor.html#autotoc_md78", null ],
      [ "Preserving custom data", "md_docs_2development_2runtime-editor.html#autotoc_md79", null ],
      [ "Extending a page or feature", "md_docs_2development_2runtime-editor.html#autotoc_md80", null ],
      [ "Checks", "md_docs_2development_2runtime-editor.html#autotoc_md81", null ]
    ] ],
    [ "Tests and tools", "md_docs_2development_2testing.html", [
      [ "Run one area first", "md_docs_2development_2testing.html#autotoc_md83", null ],
      [ "Balance lab", "md_docs_2development_2testing.html#autotoc_md84", null ],
      [ "Screenshots", "md_docs_2development_2testing.html#autotoc_md85", null ],
      [ "Performance benchmarks", "md_docs_2development_2testing.html#autotoc_md86", null ],
      [ "Historical calibration failures", "md_docs_2development_2testing.html#autotoc_md87", null ]
    ] ],
    [ "Website documentation rendering", "md_docs_2development_2website.html", [
      [ "Lists and code examples", "md_docs_2development_2website.html#autotoc_md89", null ],
      [ "Diagram sizing and themes", "md_docs_2development_2website.html#autotoc_md90", null ],
      [ "Rendering repair walkthrough", "md_docs_2development_2website.html#autotoc_md91", null ],
      [ "Media and publication size", "md_docs_2development_2website.html#autotoc_md92", null ]
    ] ],
    [ "Controls", "md_docs_2user_2controls.html", [
      [ "Anywhere in the game", "md_docs_2user_2controls.html#autotoc_md94", null ],
      [ "Main menu and club choice", "md_docs_2user_2controls.html#autotoc_md95", null ],
      [ "Management screens", "md_docs_2user_2controls.html#autotoc_md96", [
        [ "Back and Forward", "md_docs_2user_2controls.html#autotoc_md97", null ],
        [ "Sidebar hubs", "md_docs_2user_2controls.html#autotoc_md98", null ],
        [ "Mouse on management screens", "md_docs_2user_2controls.html#autotoc_md99", null ]
      ] ],
      [ "Match day", "md_docs_2user_2controls.html#autotoc_md100", [
        [ "Playback", "md_docs_2user_2controls.html#autotoc_md101", null ],
        [ "Substitutions board and tactics", "md_docs_2user_2controls.html#autotoc_md102", null ],
        [ "Touchline shouts", "md_docs_2user_2controls.html#autotoc_md103", null ],
        [ "3D cameras", "md_docs_2user_2controls.html#autotoc_md104", null ],
        [ "Mouse in the match view", "md_docs_2user_2controls.html#autotoc_md105", null ],
        [ "Play mode", "md_docs_2user_2controls.html#autotoc_md106", null ]
      ] ],
      [ "Where your files are", "md_docs_2user_2controls.html#autotoc_md107", null ]
    ] ],
    [ "Saves, settings and troubleshooting", "md_docs_2user_2files-and-troubleshooting.html", [
      [ "Recover a save", "md_docs_2user_2files-and-troubleshooting.html#autotoc_md109", null ],
      [ "Report a problem", "md_docs_2user_2files-and-troubleshooting.html#autotoc_md110", null ],
      [ "Use an isolated test career", "md_docs_2user_2files-and-troubleshooting.html#autotoc_md111", null ],
      [ "Player12 and earlier careers", "md_docs_2user_2files-and-troubleshooting.html#autotoc_md112", null ]
    ] ],
    [ "Getting started", "md_docs_2user_2getting-started.html", [
      [ "1. Start a career", "md_docs_2user_2getting-started.html#autotoc_md114", null ],
      [ "2. Your first day", "md_docs_2user_2getting-started.html#autotoc_md115", null ],
      [ "3. Get to know the squad", "md_docs_2user_2getting-started.html#autotoc_md116", null ],
      [ "4. Lineup and tactics", "md_docs_2user_2getting-started.html#autotoc_md117", null ],
      [ "5. Training and staff", "md_docs_2user_2getting-started.html#autotoc_md118", null ],
      [ "6. Scouting and transfers", "md_docs_2user_2getting-started.html#autotoc_md119", null ],
      [ "7. The youth academy", "md_docs_2user_2getting-started.html#autotoc_md120", null ],
      [ "8. Advancing the calendar", "md_docs_2user_2getting-started.html#autotoc_md121", null ],
      [ "9. Match day", "md_docs_2user_2getting-started.html#autotoc_md122", [
        [ "3D camera controls", "md_docs_2user_2getting-started.html#autotoc_md123", null ]
      ] ],
      [ "10. Beyond the first season", "md_docs_2user_2getting-started.html#autotoc_md124", null ],
      [ "Tips", "md_docs_2user_2getting-started.html#autotoc_md125", null ]
    ] ],
    [ "In-game editor", "md_docs_2user_2in-game-editor.html", null ],
    [ "Install and run", "md_docs_2user_2installation.html", [
      [ "Download and play", "md_docs_2user_2installation.html#autotoc_md128", null ],
      [ "Build from source", "md_docs_2user_2installation.html#autotoc_md129", [
        [ "Linux", "md_docs_2user_2installation.html#autotoc_md130", null ],
        [ "macOS", "md_docs_2user_2installation.html#autotoc_md131", null ],
        [ "Windows", "md_docs_2user_2installation.html#autotoc_md132", null ],
        [ "Other build options", "md_docs_2user_2installation.html#autotoc_md133", null ]
      ] ],
      [ "Running", "md_docs_2user_2installation.html#autotoc_md134", null ]
    ] ],
    [ "Feature tour", "md_docs_2project_2features.html", [
      [ "Screenshots", "md_docs_2project_2features.html#autotoc_md136", null ]
    ] ],
    [ "Roadmap", "md_docs_2project_2roadmap.html", [
      [ "Start from what already works", "md_docs_2project_2roadmap.html#autotoc_md138", null ],
      [ "First priority: make existing decisions easier", "md_docs_2project_2roadmap.html#autotoc_md139", null ],
      [ "Release confidence and football quality", "md_docs_2project_2roadmap.html#autotoc_md140", null ],
      [ "In-game editing: safe changes before arbitrary worlds", "md_docs_2project_2roadmap.html#autotoc_md141", [
        [ "1. Edit supported data for a new career", "md_docs_2project_2roadmap.html#autotoc_md142", null ],
        [ "2. Make creations shareable and recoverable", "md_docs_2project_2roadmap.html#autotoc_md143", null ],
        [ "3. Separate countries from today's league-pyramid convention", "md_docs_2project_2roadmap.html#autotoc_md144", null ],
        [ "4. Expose competition rules, then expand supported formats", "md_docs_2project_2roadmap.html#autotoc_md145", null ]
      ] ],
      [ "Friendlies, invitations and summer competitions", "md_docs_2project_2roadmap.html#autotoc_md146", null ],
      [ "Community building: people and ownership", "md_docs_2project_2roadmap.html#autotoc_md147", null ],
      [ "Turn the roadmap into a contribution", "md_docs_2project_2roadmap.html#autotoc_md148", null ]
    ] ],
    [ "Add yourself as a player", "md_docs_2contributing_2player-cameo.html", [
      [ "1. Choose your club and an unused player ID", "md_docs_2contributing_2player-cameo.html#autotoc_md150", null ],
      [ "2. Create a player file", "md_docs_2contributing_2player-cameo.html#autotoc_md151", null ],
      [ "3. Check it in a new career", "md_docs_2contributing_2player-cameo.html#autotoc_md152", null ],
      [ "4. Include it in your contribution", "md_docs_2contributing_2player-cameo.html#autotoc_md153", null ]
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
"buyer__negotiation_8h.html#ae14b96fbe35e926af0b1222a9aeab048a13727661cffa8b83aef4332c92dfad1e",
"classAudio_1_1OnePole.html#adcc5d0a621c72c05d017d59b9bd13f09",
"classCallUpScene.html#a63725f76a3dd281db9221726d47dca4c",
"classDatabaseConnection.html#ad7f9a09e8d63929b39c7c2535a57d2df",
"classGUIView.html#ad92f0d45a39f78d897c97f22c68a0e12",
"classGameController.html#a57b18093d18ce1cdf0d8767e3e28ae35",
"classGameController.html#ae8d6ae91cd406b7ca68363e068ae4345",
"classInboxScene.html#ae419026bf040aa3c1ce9ddb2f9ba02f9",
"classLineupScene.html#a03e1dcc4648cf1dcff2e7aae07d8d93f",
"classManagerCareer.html#a60c410c1367e4eb79e000e2d67b5dff5",
"classMatchCamera3D.html#a6ecb02a849d35e148ad032f28f5dde90",
"classMatchEngine.html#a7cca1abac6923190b69d00066bd3beab",
"classMatchInsightsView.html#aaf2d7f01cf250e3bfab378357120c76d",
"classMatchScene.html#ad8bcc5af9bb07259eaea1d68beb9b86a",
"classNationalManagement.html#ad777168d7275a84d647b423b65ebe6e2",
"classPlayer.html#a71057df9bb971e2d00f6f09b6adcea8e",
"classRecordsScene.html#a79af6c8aefedd80b6d6fb174a6a75817",
"classSaveManager.html#af1d6f01e6a2fae700434cea8878b5d10",
"classSeasonReviewDialog.html#a414d8747c45c8bd185188dc5aab05169",
"classStrategy.html#a68de47cb6ef6dedff4a68ff7910799b9",
"classTrainingRegistry.html#a01ffdf102cbeca57975fa1bda85882d9",
"classTransferMarketScene.html#ad18bb1ec7a8160487b6c64df99ed4879",
"classYouthAcademy.html#ab6f2d6581312ce495c2886a1a340a5ca",
"data__hub_8h.html#a2f03747c6d1083e8b8c6247a1ce430cfa2a95e3d74ab54e49e1c00ce5abbdf58d",
"functions_u.html",
"inbox_8cpp.html#a88381ef0207b659c839d8f60978a84a9",
"interactions_8h.html#a56b692a5c945d9ee26382061a4e33d74ab8138b33f67c622f743d4bd056799592",
"loan__negotiation_8cpp.html#a9c82d9a5ecf422c3350c88a601fbefa4",
"match__camera__3d_8h_source.html",
"match__kit__colors_8h.html#abe9fb8e1a849005661f826cd65b58e9d",
"match__tracking_8h.html#a62834be222b36ccbf49af032bc8941e3",
"namespaceAudio.html#a762d8fd8f8ffa3d59d7179dbd720170fa53cdc28dbf6aae8038fb2f50796d9c0f",
"namespaceFormat.html#a1d9b3374e93faec0e04894a1c79e5b27",
"namespaceLab.html#aa9e615f65f18f339638d02553fd63ef4",
"namespaceNewsFeed.html#a5c96473e8bb68b71c4eafb599a7174b3",
"namespaceScoutingUi.html#abf2ab13b6102a56f84e8a51b1b7c05dc",
"namespaceTacticsTuning.html#af929e05d878cb819d0e72718b78d2579",
"namespaceUI.html#a4ba4b11511ad78f19e7fcff204ac22f4",
"national__teams_8h.html#a479e74e00ef8cd27fe937a2d96b02127",
"player__view_8cpp.html#a1f5c556d6cec240dee920268dceffd43",
"scout__expertise_8h.html#a9a4823f765516f87098486697141d7c4",
"squad__status_8h.html#a5a22dfac935e563cd4f4cb190131ff3f",
"structAiManager.html#ab9b6a8759f90a4c8c0c5c153a141debc",
"structCalendarScene_1_1Day.html#a6d1d18d0f3960143ea794a72132b85b9",
"structContinental_1_1CompetitionRules.html#a1d96f7187c7bb132aa45d0976ad12e85",
"structFollowedPlayer.html#a92934be58245e9aaeab84f3c3491a207",
"structGameDateValue.html#a4a370d1a6d40686b48a36aa8a8da78ec",
"structIntakeInputs.html#a1334f4b77108b23ac8233a30d0ebcac1",
"structLab_1_1Estimate.html#ade8d46eca1c12fadd253ae20648b3199",
"structLab_1_1Summary.html#a22a8b423c07196d33501937bd8a963a1",
"structManagerProfile.html#a737e5ab1754a844d8338588c9b9e73f2",
"structMatchChanges_1_1SubstitutionOutcome.html#a2bdd7ad80f61255f779e3a9058848a76",
"structMatchRender2DTuning_1_1Ball.html#aa02c46c749aa3db5f35f54d27b3538df",
"structMatchRender3DTuning_1_1Day.html#aa09d83ee060e5beea427dd0c79667797",
"structMatchRender3DTuning_1_1Player.html#a8dfa583199e71afd55ae60a30cf44c64",
"structMatchRenderer2D_1_1State.html",
"structMatchRules_1_1Knockout.html#ab98629c6c6325f84c4918a04166282cf",
"structMatchStats.html#a8646b7fab81ab158855e64feb3296230",
"structMatchTuning_1_1Discipline.html#a1da679a178c6db42bdf6295c18a05764",
"structMatchTuning_1_1Playback.html#ac76e7fc9eeaf1432aed26c383c5974f5",
"structMatchTuning_1_1Shape.html#af82b63bba495266be64bd0a257535392",
"structMigrations_1_1ColumnSpec.html",
"structNextActionFacts_1_1Report.html#a6bc0ada84d806e24dafe767a256478f9",
"structPlayerMatchStats.html#a4ba29a69cfd5cce5324d18d32cf81250",
"structPlayerView_1_1PlayerRow.html#a6949c3a1a4791a83b6c57be9fc7b5c5b",
"structReservesScene_1_1TableLine.html#a0af21baf1b706cc0d2c161b67f211e50",
"structScoutedAttribute.html#a06a5b0e594c005455b7eab58f7384c8f",
"structSettingsScene_1_1Conflict.html#af59eb92d0d4e13362c2e86e02392e5fc",
"structStadium3D_1_1GroundPolygon.html#af3f10bbedef0f7cbd41611615121cb24",
"structTalkContext.html#a60ac7639efa6ae81079a7ba8a4d791a7",
"structTrackedPlayer.html#a5fd915b537d4677a4af8e9346aee4d00",
"structTransferNegotiation_1_1ClubResponse.html#afaa9137bb13924311bcb2e5f699aa890",
"structTransferTuning_1_1Buyer.html#a779e52ad437d5a1862fd13c4c56c919c",
"structUI_1_1MoneyChip.html#a2442f02d99e5a6642e96e6e441bb6beb",
"supporters_8h.html#aa48394cc3d97914b23573a4c83b1a4eaa4905ac9d6a22bdfc1ae096094ce6248d",
"training_8cpp.html#a33de1205f51a0ba42f5fd1bb6466cb72",
"transfer__negotiation_8h.html#ac16348065bc0972cf21588fc7f36cd50a8342181f4a611ab840450b347523b9f3",
"world__generation_8cpp.html#ab6dcdbd0743831d3e8eb55adf4c5cebd"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';