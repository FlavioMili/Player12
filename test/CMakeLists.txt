set(FM_TEST_MATCH_SEED 424242)
# Fixed world seed: generated leagues, players and world events are then
# identical on every test run.
set(FM_TEST_WORLD_SEED 20250702)
set(FM_TEST_ENVIRONMENT
  "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;FM_TEST_RUNTIME_ROOT=/tmp/football-management-tests;FM_MATCH_SEED=${FM_TEST_MATCH_SEED};FM_WORLD_SEED=${FM_TEST_WORLD_SEED};FM_SIM_THREADS=2")
set(FM_TEST_LAUNCHER
  "${CMAKE_COMMAND};-E;env;${FM_TEST_ENVIRONMENT}")

add_executable(runtime_editor_tests test_runtime_editor.cpp)
target_link_libraries(runtime_editor_tests PRIVATE GTest::gtest_main fm_core)
set_property(TARGET runtime_editor_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")

add_executable(runtime_editor_ui_tests test_runtime_editor_ui.cpp)
target_link_libraries(runtime_editor_ui_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET runtime_editor_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")

add_executable(unit_tests
  test_background_task.cpp
  test_localization.cpp
  test_next_action.cpp
  test_opposition_report.cpp
  test_match_analysis.cpp
  test_delegation.cpp
  test_data_hub.cpp
  test_match_insights.cpp
  test_player.cpp
  test_game_flow.cpp
  test_gamedata.cpp
  test_database.cpp
  test_match_engine.cpp
  test_match_renderer_3d.cpp
  test_match_scenarios.cpp
  test_transfer_market.cpp
  test_transfer_negotiation.cpp
  test_buyer_negotiation.cpp
  test_loan_negotiation.cpp
  test_competitions.cpp
  test_standings_race.cpp
  test_staggered_rounds.cpp
  test_view_models.cpp
  test_finances.cpp
  test_world_sim.cpp
  test_match_scheduler.cpp
  test_scouting.cpp
  test_training.cpp
  test_staff.cpp
  test_world_data.cpp
  test_interactions.cpp
  test_save_migrations.cpp
  test_continental.cpp
  test_national_teams.cpp
  test_youth_academy.cpp
  test_manager_career.cpp
  test_career_jobs.cpp
  test_squad_status.cpp
  test_squad_planner.cpp
  test_set_pieces.cpp
  test_medical_agenda.cpp
  test_medical_supporters.cpp
  test_awards.cpp
  test_records.cpp
  test_facility_projects.cpp
  test_preseason.cpp
  test_mentoring.cpp
  test_holiday.cpp
  test_nav_history.cpp
  test_matchday_selection.cpp
)
target_link_libraries(unit_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET unit_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")

# This target deliberately excludes all GUI sources. It is the fast headless
# correctness and leak-check lane for model, controller, and persistence code.
add_executable(core_unit_tests
  test_background_task.cpp
  test_localization.cpp
  test_next_action.cpp
  test_opposition_report.cpp
  test_match_analysis.cpp
  test_delegation.cpp
  test_data_hub.cpp
  test_match_insights.cpp
  test_player.cpp
  test_gamedata.cpp
  test_database.cpp
  test_match_scenarios.cpp
  test_match_calibration.cpp
  test_match_rules.cpp
  test_transfer_market.cpp
  test_transfer_negotiation.cpp
  test_buyer_negotiation.cpp
  test_loan_negotiation.cpp
  test_competitions.cpp
  test_standings_race.cpp
  test_staggered_rounds.cpp
  test_finances.cpp
  test_world_sim.cpp
  test_match_scheduler.cpp
  test_scouting.cpp
  test_training.cpp
  test_staff.cpp
  test_world_data.cpp
  test_interactions.cpp
  test_decision_moments.cpp
  test_save_migrations.cpp
  test_continental.cpp
  test_national_teams.cpp
  test_youth_academy.cpp
  test_manager_career.cpp
  test_career_jobs.cpp
  test_squad_status.cpp
  test_squad_planner.cpp
  test_set_pieces.cpp
  test_medical_agenda.cpp
  test_medical_supporters.cpp
  test_awards.cpp
  test_records.cpp
  test_facility_projects.cpp
  test_preseason.cpp
  test_mentoring.cpp
  test_holiday.cpp
  test_season_end.cpp
  test_crash_report.cpp
  test_tactics.cpp
  test_news_feed.cpp
  test_portable_rng.cpp
  test_transfer_windows.cpp
  test_squad_numbers.cpp
)
target_link_libraries(core_unit_tests PRIVATE GTest::gtest_main fm_core)
target_compile_definitions(core_unit_tests PRIVATE
  FM_PROJECT_VERSION="${PROJECT_VERSION}")
set_property(TARGET core_unit_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(unit_tests PRIVATE DEBUG)
  target_compile_definitions(core_unit_tests PRIVATE DEBUG)
endif()

include(GoogleTest)
gtest_discover_tests(runtime_editor_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES ENVIRONMENT "${FM_TEST_ENVIRONMENT}" LABELS "unit;core;editor")
gtest_discover_tests(runtime_editor_ui_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES ENVIRONMENT "${FM_TEST_ENVIRONMENT}" LABELS "unit;gui;editor")
gtest_discover_tests(
  unit_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit"
)
gtest_discover_tests(
  core_unit_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "core::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;core;leak-check"
)

# Match audio: mixer, procedural sounds and event mapping (headless, rendered
# by hand; the device test uses the dummy audio driver).
add_executable(audio_tests test_audio.cpp)
target_link_libraries(audio_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET audio_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  audio_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "audio::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;audio"
)

# Long GUI playtest journey (a season of a career through the real screens).
# Own executable so it carries its own label: exclude it with `-LE playtest`.
add_executable(playtest_tests test_playtest_journey.cpp)
target_link_libraries(playtest_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET playtest_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(playtest_tests PRIVATE DEBUG)
endif()
gtest_discover_tests(
  playtest_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "playtest::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "playtest;slow"
    TIMEOUT 400
)

# Seeded GUI monkey (random user through the real GUIView) and the widget
# effect sweep. Replay: FM_MONKEY_SEED=<n> FM_MONKEY_STEPS=<n>; exclude with
# `-LE monkey`.
add_executable(monkey_tests test_gui_monkey.cpp)
target_link_libraries(monkey_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET monkey_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  monkey_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "monkey::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "monkey;slow"
    TIMEOUT 900
)

# Scout pages of the Scouting screen through the real GUIView (screenshots
# in the runtime captures folder; FM_KEEP_TEST_ARTIFACTS=1 keeps them).
add_executable(scouting_ui_tests test_scouting_ui.cpp)
target_link_libraries(scouting_ui_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET scouting_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  scouting_ui_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "scouting_ui::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;gui"
    TIMEOUT 600
)

# The Youth screen through the real GUIView (screenshots in the runtime
# captures folder; FM_KEEP_TEST_ARTIFACTS=1 keeps them).
add_executable(youth_ui_tests test_youth_ui.cpp)
target_link_libraries(youth_ui_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET youth_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  youth_ui_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "youth_ui::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;gui"
    TIMEOUT 600
)

# Medical load chart and instructions, and the supporters card, through the
# real GUIView (screenshots in the runtime captures folder).
add_executable(medical_ui_tests test_medical_supporters_ui.cpp)
target_link_libraries(medical_ui_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET medical_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  medical_ui_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "medical_ui::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;gui"
    TIMEOUT 600
)

# Medical centre, calendar, squad planner, comparison and set-piece dialog
# through the real GUIView (screenshots in the runtime captures folder;
# FM_KEEP_TEST_ARTIFACTS=1 keeps them).
add_executable(squad_ui_tests test_squad_screens.cpp)
target_link_libraries(squad_ui_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET squad_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  squad_ui_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "squad_ui::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;gui"
    TIMEOUT 600
)

# Under-21 and national-team call-up screens through the real GUIView at
# 1280x720 and 2560x1440 at 200% (screenshots in the runtime captures folder).
add_executable(career_jobs_ui_tests test_career_jobs_ui.cpp)
target_link_libraries(career_jobs_ui_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET career_jobs_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  career_jobs_ui_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "career_jobs_ui::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;gui"
    TIMEOUT 900
)

# Saved column views of the big tables and the league table run-in through
# the real GUIView (screenshots in the runtime captures folder;
# FM_KEEP_TEST_ARTIFACTS=1 keeps them).
add_executable(tables_ui_tests test_tables_ui.cpp)
target_link_libraries(tables_ui_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET tables_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  tables_ui_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "tables_ui::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;gui"
    TIMEOUT 600
)

# Manager career screens through the real GUIView (screenshots in the
# runtime captures folder; FM_KEEP_TEST_ARTIFACTS=1 keeps them).
add_executable(manager_ui_tests test_manager_ui.cpp)
target_link_libraries(manager_ui_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET manager_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  manager_ui_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "manager_ui::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;gui"
    TIMEOUT 600
)

# World news, the career timeline and the draw ceremonies through the real
# GUIView at several window sizes and UI scales (screenshots in the runtime
# captures folder; FM_KEEP_TEST_ARTIFACTS=1 keeps them).
add_executable(news_ui_tests test_news_ui.cpp)
target_link_libraries(news_ui_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET news_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  news_ui_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "news_ui::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;gui"
    TIMEOUT 600
)

# The About screen and the main menu's crash notice through the real GUIView
# (screenshots in the runtime captures folder).
add_executable(about_ui_tests test_about.cpp)
target_link_libraries(about_ui_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET about_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  about_ui_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "about_ui::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;gui"
    TIMEOUT 600
)

# Back / Forward history driven by touchpad swipes, mouse side buttons and
# Alt+arrows through the real GUIView.
add_executable(navigation_ui_tests test_navigation_ui.cpp)
target_link_libraries(navigation_ui_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET navigation_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  navigation_ui_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "navigation_ui::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;gui"
    TIMEOUT 600
)

# Talks over an AI club's bid for a managed player, from the inbox and the
# transfer market, through the real GUIView at several window sizes and UI
# scales (screenshots in the runtime captures folder).
add_executable(transfer_ui_tests test_transfer_ui.cpp)
target_link_libraries(transfer_ui_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET transfer_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  transfer_ui_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "transfer_ui::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;gui"
    TIMEOUT 600
)

# Guidance screens (checklist, next steps, delegation, opposition report,
# data hub, inbox decisions, half-time analysis) through the real GUIView at
# several window sizes and UI scales; screenshots in the captures folder.
add_executable(guidance_ui_tests test_guidance_ui.cpp)
target_link_libraries(guidance_ui_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET guidance_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  guidance_ui_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "guidance_ui::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;gui"
    TIMEOUT 600
)

# The inbox (decision moments, filters kept with the career, followed
# players) through the real GUIView at 1280x720, a narrow window and UI
# scale 2; screenshots in the captures folder.
add_executable(inbox_ui_tests test_inbox_ui.cpp)
target_link_libraries(inbox_ui_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET inbox_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  inbox_ui_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "inbox_ui::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;gui"
    TIMEOUT 600
)

# The tactics screen (roles, duties, shape with the ball) through the real
# GUIView at 1280x720 and UI scale 2; screenshots in the captures folder.
add_executable(tactics_ui_tests test_tactics_ui.cpp)
target_link_libraries(tactics_ui_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET tactics_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  tactics_ui_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "tactics_ui::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;gui"
    TIMEOUT 600
)

# Welcome tour, Help screen, rebindable controls, theme contrast and the
# scan for hard-coded English in GUI code (screenshots in the captures
# folder).
add_executable(onboarding_ui_tests test_onboarding_ui.cpp)
target_link_libraries(onboarding_ui_tests PRIVATE GTest::gtest_main fm_ui)
target_compile_definitions(onboarding_ui_tests PRIVATE
  FM_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
set_property(TARGET onboarding_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  onboarding_ui_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "onboarding_ui::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;gui"
    TIMEOUT 600
)

# The match report's Analysis tab and the data hub's season analytics
# through the real GUIView at several window sizes and UI scales;
# screenshots in the captures folder.
add_executable(match_insights_ui_tests test_match_insights_ui.cpp)
target_link_libraries(match_insights_ui_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET match_insights_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  match_insights_ui_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "insights_ui::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;gui"
    TIMEOUT 900
)

# Matchday controls of the live match (substitutions, tactics, shouts)
# through the real GUIView at several sizes; screenshots in the captures
# folder.
add_executable(match_controls_ui_tests test_match_controls_ui.cpp)
target_link_libraries(match_controls_ui_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET match_controls_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  match_controls_ui_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "match_ui::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;gui"
    TIMEOUT 600
)

# Play mode through the real GUIView: take control, keys move and pass,
# switch player, hand back (screenshots in the runtime captures folder).
add_executable(play_mode_ui_tests test_play_mode_ui.cpp)
target_link_libraries(play_mode_ui_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET play_mode_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  play_mode_ui_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "play_ui::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;gui"
    TIMEOUT 600
)

# Planning, holiday, awards and records screens through the real GUIView
# (screenshots in the runtime captures folder).
add_executable(career_ui_tests test_career_ui.cpp)
target_link_libraries(career_ui_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET career_ui_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  career_ui_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "career_ui::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;gui"
    TIMEOUT 900
)

# The README screenshots (docs/images) from a real career. Skipped unless
# FM_SHOWCASE_DIR names the output folder; see the file header.
add_executable(showcase_captures test_showcase_captures.cpp)
target_link_libraries(showcase_captures PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET showcase_captures PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  showcase_captures
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "showcase::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "gui;showcase"
    TIMEOUT 1800
)

# Adversarial user scenarios (double Continue, saves during Continue, empty
# squads, old saves, corrupt settings, HiDPI). Exclude with `-LE adversarial`.
add_executable(adversarial_tests test_adversarial.cpp)
target_link_libraries(adversarial_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET adversarial_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  adversarial_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "adversarial::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "adversarial;slow"
    TIMEOUT 600
)

# Performance budgets: Continue on busy and quiet days, save/load and every
# management screen at 2560x1440, scale 2 (median of repetitions against
# generous budgets; replaces the global allocation functions to count heap
# allocations per frame). Exclude with `-LE perf`.
add_executable(perf_tests test_perf_budget.cpp)
target_link_libraries(perf_tests PRIVATE GTest::gtest_main fm_ui)
set_property(TARGET perf_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  perf_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "perf::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "perf;slow"
    TIMEOUT 900
)

# Exploit probes and long-run integrity (two seasons, rollovers, reloads),
# headless. DISABLED_ tests reproduce open findings. Exclude with `-LE qa`.
add_executable(qa_tests test_qa_exploits.cpp)
target_link_libraries(qa_tests PRIVATE GTest::gtest_main fm_core)
set_property(TARGET qa_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  qa_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "qa::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "qa;adversarial;slow"
    TIMEOUT 3600
)

# Balance lab (fm_lab): report math, verdicts, JSON schema and a tiny
# thread-count-invariant smoke run. Headless, links fm_core only.
add_executable(lab_tests test_lab_report.cpp)
target_link_libraries(lab_tests PRIVATE GTest::gtest_main fm_lab_lib)
set_property(TARGET lab_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  lab_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "lab::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "unit;lab"
)

# Multi-season soak of the whole world (population, ability, finances,
# competitions, save/reload): three seasons, 5-30 minutes each depending on
# the machine. Exclude with `-LE soak`.
add_executable(soak_tests test_soak.cpp)
target_link_libraries(soak_tests PRIVATE GTest::gtest_main fm_lab_lib)
set_property(TARGET soak_tests PROPERTY TEST_LAUNCHER "${FM_TEST_LAUNCHER}")
gtest_discover_tests(
  soak_tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "soak::"
  PROPERTIES
    ENVIRONMENT "${FM_TEST_ENVIRONMENT}"
    LABELS "soak;slow"
    TIMEOUT 7200
)

# A deliberately invalid executable verifies that the sanitizer preset really
# instruments project code. CTest expects its non-zero sanitizer exit.
if(FM_ENABLE_SANITIZERS)
  add_executable(sanitizer_probe EXCLUDE_FROM_ALL sanitizer_probe.cpp)
  target_compile_options(sanitizer_probe PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(sanitizer_probe PRIVATE -fsanitize=address,undefined)
  add_test(NAME sanitizer-instrumentation
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target sanitizer_probe)
  set_tests_properties(sanitizer-instrumentation PROPERTIES
    LABELS "unit;sanitizer"
    FIXTURES_SETUP sanitizer_probe_built)
  add_test(NAME sanitizer-detects-invalid-access COMMAND sanitizer_probe)
  set_tests_properties(sanitizer-detects-invalid-access PROPERTIES
    LABELS "unit;sanitizer"
    FIXTURES_REQUIRED sanitizer_probe_built
    WILL_FAIL TRUE)
endif()
