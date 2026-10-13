# -----------------------------
# Path Configuration
# -----------------------------
configure_file(
  "${PROJECT_SOURCE_DIR}/src/global/paths.h.in"
  "${PROJECT_BINARY_DIR}/src/global/paths.h"
)
# Only the version: the commit and toolchain go to build_info.cpp alone.
configure_file(
  "${PROJECT_SOURCE_DIR}/src/global/version.h.in"
  "${PROJECT_BINARY_DIR}/src/global/version.h"
)

# -----------------------------
# Headless domain and persistence library
# -----------------------------
set(FM_CORE_SOURCES
    controller/game_controller.h
    controller/game_controller.cpp
    controller/game_controller_guidance.cpp
    controller/game_controller_offers.cpp
    controller/game_controller_squad.cpp
    controller/game_controller_career.cpp
    controller/game_controller_season.cpp

    database/database_connection.h
    database/database_connection.cpp
    database/runtime_editor.h
    database/runtime_editor.cpp
    controller/game_controller_editor.cpp
    model/runtime_editor_validation.h
    model/runtime_editor_validation.cpp
    database/repositories/player_repository.h
    database/repositories/player_repository.cpp
    database/repositories/team_repository.h
    database/repositories/team_repository.cpp
    database/repositories/league_repository.h
    database/repositories/league_repository.cpp
    database/repositories/fixture_repository.h
    database/repositories/fixture_repository.cpp
    database/repositories/competition_repository.h
    database/repositories/competition_repository.cpp
    database/repositories/game_state_repository.h
    database/repositories/game_state_repository.cpp
    database/repositories/finance_repository.h
    database/repositories/finance_repository.cpp
    database/repositories/inbox_repository.h
    database/repositories/inbox_repository.cpp
    database/repositories/scouting_repository.h
    database/repositories/scouting_repository.cpp
    database/repositories/world_state_repository.h
    database/repositories/world_state_repository.cpp
    database/repositories/staff_repository.h
    database/repositories/staff_repository.cpp
    database/repositories/training_repository.h
    database/repositories/training_repository.cpp
    database/repositories/transfer_repository.h
    database/repositories/transfer_repository.cpp
    database/repositories/text_encoding.h
    database/datagenerator.h
    database/datagenerator.cpp
    database/SQLLoader.h
    database/SQLLoader.cpp
    database/gamedata.h
    database/gamedata.cpp
    database/migrations/migrations.h
    database/migrations/migration_runner.cpp
    database/migrations/registry.cpp
    database/save_manager.h
    database/save_manager.cpp
    database/sqlite_rows.h

    global/build_info.h
    global/build_info.cpp
    global/crash_report.h
    global/crash_report.cpp
    global/global.h
    global/languages.h
    global/language_manager.h
    global/language_manager.cpp
    global/logger.h
    global/logger.cpp
    global/number_format.h
    global/number_format.cpp
    global/runtime_paths.h
    global/runtime_paths.cpp
    global/queries.h
    global/roles.h
    global/stats_config.h
    global/thread_pool.h
    global/thread_pool.cpp
    global/types.h

    model/awards.h
    model/awards.cpp
    model/board.h
    model/board.cpp
    model/calendar.h
    model/calendar.cpp
    model/club_economy.h
    model/club_economy.cpp
    model/competition.h
    model/competition.cpp
    model/competition_manager.h
    model/competition_manager.cpp
    model/continental.h
    model/continental.cpp
    model/draw_ceremony.h
    model/draw_ceremony.cpp
    model/news_feed.h
    model/news_feed.cpp
    model/career_timeline.h
    model/career_timeline.cpp
    model/national_teams.h
    model/national_teams.cpp
    model/national_job.h
    model/national_job.cpp
    model/discipline.h
    model/discipline.cpp
    model/finances.h
    model/facility_projects.h
    model/facility_projects.cpp
    model/finances.cpp
    model/game.h
    model/game.cpp
    model/game_season.cpp
    model/gamedate.h
    model/holiday.h
    model/holiday.cpp
    model/gamedate.cpp
    model/inbox.h
    model/inbox.cpp
    model/club_article.h
    model/club_article.cpp
    model/data_hub.h
    model/data_hub.cpp
    model/delegation.h
    model/delegation.cpp
    model/guidance.h
    model/guidance.cpp
    model/match_analysis.h
    model/match_analysis.cpp
    model/next_action.h
    model/next_action.cpp
    model/onboarding.h
    model/onboarding.cpp
    model/opposition_report.h
    model/opposition_report.cpp
    model/injury.h
    model/injury.cpp
    model/interactions.h
    model/interactions.cpp
    model/league.h
    model/league.cpp
    model/lineup.h
    model/lineup.cpp
    model/manager_career.h
    model/manager_career.cpp
    model/match.h
    model/mentoring.h
    model/mentoring.cpp
    model/match.cpp
    model/match_report.h
    model/match_report.cpp
    model/match_commentary.h
    model/match_commentary.cpp
    model/match_engine.h
    model/match_engine.cpp
    model/match_engine_tracking.cpp
    model/match_events.h
    model/match_insights.h
    model/match_insights.cpp
    model/match_tracking.h
    model/match_tracking.cpp
    model/match_rules.h
    model/match_rules.cpp
    model/match_scenario.h
    model/match_scheduler.h
    model/match_scheduler.cpp
    model/match_tuning.h
    model/medical_centre.h
    model/medical_centre.cpp
    model/player.h
    model/preseason.h
    model/preseason.cpp
    model/records.h
    model/records.cpp
    model/player.cpp
    model/role_utils.h
    model/role_utils.cpp
    model/season_agenda.h
    model/season_agenda.cpp
    model/season_history.h
    model/season_history.cpp
    model/season_review.h
    model/season_review.cpp
    model/squad_planner.h
    model/squad_planner.cpp
    model/squad_numbers.h
    model/squad_numbers.cpp
    model/squad_status.h
    model/squad_status.cpp
    model/staff.h
    model/staff.cpp
    model/stories.h
    model/stories.cpp
    model/supporters.h
    model/supporters.cpp
    model/standings.h
    model/standings.cpp
    model/standings_race.h
    model/standings_race.cpp
    model/strategy.h
    model/strategy.cpp
    model/tactics.h
    model/tactics.cpp
    model/team.h
    model/team.cpp
    model/training.h
    model/training.cpp
    model/transfer_listing.h
    model/transfer_listing.cpp
    model/transfer_market.h
    model/transfer_market.cpp
    model/transfer_negotiation.h
    model/transfer_negotiation.cpp
    model/transfer_windows.h
    model/transfer_windows.cpp
    model/buyer_negotiation.h
    model/buyer_negotiation.cpp
    model/loan_negotiation.h
    model/loan_negotiation.cpp
    model/player_agent.h
    model/player_agent.cpp
    model/world_generation.h
    model/world_generation.cpp
    model/world_rng.h
    model/world_rng.cpp
    model/world_simulation.h
    model/world_simulation.cpp
    model/youth_academy.h
    model/youth_academy.cpp
    model/scouting.h
    model/scouting.cpp
    model/scout_expertise.h
    model/scout_expertise.cpp
    model/world_tuning.h
    model/transfer_tuning.h
)

# The match engine advances the players with straight-line code; without
# errno and trapping semantics for floating point (the engine reads neither)
# GCC and Clang vectorise it. Results are unchanged.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND
   NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set_source_files_properties(model/match_engine.cpp PROPERTIES
    COMPILE_OPTIONS "-fno-math-errno;-fno-trapping-math")
endif()

find_package(Threads REQUIRED)
add_library(fm_core STATIC ${FM_CORE_SOURCES})

target_include_directories(fm_core PUBLIC
  ${PROJECT_BINARY_DIR}/src
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/model
  ${PROJECT_SOURCE_DIR}/src/controller
  ${PROJECT_SOURCE_DIR}/src/database
)

target_link_libraries(fm_core PUBLIC
  fmt::fmt
  spdlog::spdlog
  nlohmann_json::nlohmann_json
  SQLite3::SQLite3
  SDL3::SDL3
  Threads::Threads
)
if(DEFINED sdl3_SOURCE_DIR)
  target_include_directories(fm_core SYSTEM PUBLIC ${sdl3_SOURCE_DIR}/include)
endif()

# Build provenance for the main menu, the log and crash reports.
set(FM_BUILD_COMPILER_NAME "${CMAKE_CXX_COMPILER_ID}")
if(FM_BUILD_COMPILER_NAME STREQUAL "GNU")
  set(FM_BUILD_COMPILER_NAME "GCC")
endif()
string(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" FM_BUILD_PLATFORM)
set_property(SOURCE global/build_info.cpp APPEND PROPERTY COMPILE_DEFINITIONS
  "FM_BUILD_COMMIT=\"${FM_BUILD_COMMIT}\""
  "FM_BUILD_CONFIG=\"$<CONFIG>\""
  "FM_BUILD_COMPILER=\"${FM_BUILD_COMPILER_NAME} ${CMAKE_CXX_COMPILER_VERSION}\""
  "FM_BUILD_PLATFORM=\"${FM_BUILD_PLATFORM}\"")

# -----------------------------
# Presentation library
# -----------------------------
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
set_source_files_properties(${IMGUI_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

set(FM_UI_SOURCES
    ${IMGUI_SOURCES}
    audio/audio_device.h
    audio/audio_device.cpp
    audio/audio_mixer.h
    audio/audio_mixer.cpp
    audio/match_audio.h
    audio/match_audio.cpp
    audio/synth.h
    audio/synth.cpp
    model/settings_manager.h
    model/settings_manager.cpp
    gui/button_manager.h
    gui/button_manager.cpp
    gui/dropdown.h
    gui/dropdown.cpp
    gui/gui_constants.h
    gui/gui_scene.h
    gui/gui_scene.cpp
    gui/gui_view.h
    gui/gui_view.cpp
    gui/nav_history.h
    gui/nav_history.cpp
    gui/input_actions.h
    gui/input_actions.cpp
    gui/swipe_gesture.h
    gui/swipe_gesture.cpp
    gui/render_scale.h
    gui/player_ui.h
    gui/render/imatch_renderer.h
    gui/render/match_render_snapshot.h
    gui/render/match_renderer_2d.h
    gui/render/match_renderer_2d.cpp
    gui/render/match_render_2d_tuning.h
    gui/render/match_shirt_numbers.h
    gui/render/match_camera_3d.h
    gui/render/match_camera_3d.cpp
    gui/render/match_kit_colors.h
    gui/render/match_kit_colors.cpp
    gui/render/match_render_3d_tuning.h
    gui/render/match_render_math.h
    gui/render/match_renderer_3d.h
    gui/render/match_renderer_3d.cpp
    gui/render/match_stadium_3d.h
    gui/render/match_stadium_3d.cpp
    gui/render/match_play_overlay.h
    gui/render/match_play_overlay.cpp
    gui/scenes/lineup_scene.h
    gui/scenes/lineup_scene.cpp
    gui/scenes/match_scene.h
    gui/scenes/match_scene.cpp
    gui/scenes/match_scene_play.cpp
    gui/scenes/match_play_controller.h
    gui/scenes/match_play_controller.cpp
    gui/scenes/match_scene_tuning.h
    gui/scenes/main_game_scene.h
    gui/scenes/main_game_scene.cpp
    gui/scenes/main_menu_scene.h
    gui/scenes/main_menu_scene.cpp
    gui/scenes/roster_scene.h
    gui/scenes/roster_scene.cpp
    gui/scenes/settings_scene.h
    gui/scenes/settings_scene.cpp
    gui/scenes/help_scene.h
    gui/scenes/help_scene.cpp
    gui/scenes/welcome_tour.h
    gui/scenes/welcome_tour.cpp
    gui/scenes/strategy_scene.h
    gui/scenes/strategy_scene.cpp
    gui/scenes/team_selection_scene.h
    gui/scenes/team_selection_scene.cpp
    gui/scenes/transfer_market_scene.h
    gui/scenes/transfer_market_scene.cpp
    gui/scenes/transfer_market_scene_tuning.h
    gui/scenes/management_scene.h
    gui/scenes/management_scene.cpp
    gui/scenes/runtime_editor_dialog.h
    gui/scenes/runtime_editor_dialog.cpp
    gui/scenes/manager_scene.h
    gui/scenes/manager_scene.cpp
    gui/scenes/player_profile_scene.h
    gui/scenes/player_profile_scene.cpp
    gui/scenes/player_talk_dialog.h
    gui/scenes/player_talk_dialog.cpp
    gui/scenes/contract_talks_dialog.h
    gui/scenes/contract_talks_dialog.cpp
    gui/scenes/offer_negotiation_dialog.h
    gui/scenes/offer_negotiation_dialog.cpp
    gui/scenes/transfer_terms_editor.h
    gui/scenes/transfer_terms_editor.cpp
    gui/scenes/team_talk_dialog.h
    gui/scenes/team_talk_dialog.cpp
    gui/scenes/fixtures_scene.h
    gui/scenes/fixtures_scene.cpp
    gui/scenes/standings_scene.h
    gui/scenes/standings_scene.cpp
    gui/scenes/international_scene.h
    gui/scenes/international_scene.cpp
    gui/scenes/inbox_scene.h
    gui/scenes/inbox_dilemma_card.h
    gui/scenes/inbox_dilemma_card.cpp
    gui/scenes/inbox_scene.cpp
    gui/scenes/data_hub_scene.h
    gui/scenes/data_hub_scene.cpp
    gui/scenes/delegation_scene.h
    gui/scenes/delegation_scene.cpp
    gui/scenes/match_analysis_panel.h
    gui/scenes/match_analysis_panel.cpp
    gui/scenes/match_touchline.h
    gui/scenes/match_touchline.cpp
    gui/scenes/match_subs_panel.h
    gui/scenes/match_subs_panel.cpp
    gui/scenes/match_tactics_panel.h
    gui/scenes/match_tactics_panel.cpp
    gui/scenes/match_shouts_bar.h
    gui/scenes/match_shouts_bar.cpp
    gui/scenes/onboarding_overlay.h
    gui/scenes/onboarding_overlay.cpp
    gui/scenes/opposition_scene.h
    gui/scenes/opposition_scene.cpp
    gui/scenes/club_scene.h
    gui/scenes/club_scene.cpp
    gui/scenes/about_scene.h
    gui/scenes/about_scene.cpp
    gui/scenes/awards_scene.h
    gui/scenes/awards_scene.cpp
    gui/scenes/records_scene.h
    gui/scenes/records_scene.cpp
    gui/scenes/preseason_scene.h
    gui/scenes/preseason_scene.cpp
    gui/scenes/holiday_dialog.h
    gui/scenes/holiday_dialog.cpp
    gui/scenes/season_review_dialog.h
    gui/scenes/season_review_dialog.cpp
    gui/scenes/mentoring_dialog.h
    gui/scenes/mentoring_dialog.cpp
    gui/scenes/draw_ceremony_dialog.h
    gui/scenes/draw_ceremony_dialog.cpp
    gui/scenes/news_scene.h
    gui/scenes/news_scene.cpp
    gui/scenes/timeline_scene.h
    gui/scenes/timeline_scene.cpp
    gui/scenes/match_insights_view.h
    gui/scenes/match_insights_view.cpp
    gui/scenes/match_report_scene.h
    gui/scenes/match_report_scene.cpp
    gui/scenes/scouting_scene.h
    gui/scenes/scouting_scene.cpp
    gui/scenes/scouting_scene_internal.h
    gui/scenes/scouting_scouts_page.cpp
    gui/scenes/training_scene.h
    gui/scenes/training_scene.cpp
    gui/scenes/staff_scene.h
    gui/scenes/staff_scene.cpp
    gui/scenes/youth_scene.h
    gui/scenes/youth_scene.cpp
    gui/scenes/reserves_scene.h
    gui/scenes/reserves_scene.cpp
    gui/scenes/callup_scene.h
    gui/scenes/callup_scene.cpp
    gui/scenes/medical_scene.h
    gui/scenes/medical_scene.cpp
    gui/scenes/calendar_scene.h
    gui/scenes/calendar_scene.cpp
    gui/scenes/squad_planner_scene.h
    gui/scenes/squad_planner_scene.cpp
    gui/scenes/player_compare_scene.h
    gui/scenes/player_compare_scene.cpp
    gui/scenes/set_pieces_dialog.h
    gui/scenes/set_pieces_dialog.cpp
    gui/view_models/competition_view.h
    gui/view_models/competition_view.cpp
    gui/view_models/formation.h
    gui/view_models/formation.cpp
    gui/view_models/match_changes.h
    gui/view_models/match_changes.cpp
    gui/view_models/match_clock.h
    gui/view_models/match_clock.cpp
    gui/view_models/player_view.h
    gui/view_models/editor_context.h
    gui/view_models/player_view.cpp
    gui/widgets/format.h
    gui/widgets/format.cpp
    gui/widgets/icons.h
    gui/widgets/icons.cpp
    gui/widgets/theme.h
    gui/widgets/theme.cpp
    gui/widgets/widgets.h
    gui/widgets/widgets.cpp
)

add_library(fm_ui STATIC ${FM_UI_SOURCES})
add_library(core_lib ALIAS fm_ui)

if(BUILD_UI_AUTOMATION)
  set(IMGUI_TEST_ENGINE_SOURCES
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_capture_tool.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_context.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_coroutine.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_engine.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_exporters.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_perftool.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_ui.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_utils.cpp
  )
  add_library(imgui_test_engine_lib STATIC ${IMGUI_TEST_ENGINE_SOURCES})
  target_include_directories(imgui_test_engine_lib SYSTEM PUBLIC
    ${imgui_test_engine_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
  )
  target_compile_definitions(imgui_test_engine_lib PUBLIC
    IMGUI_ENABLE_TEST_ENGINE
    IMGUI_TEST_ENGINE_ENABLE_CAPTURE=1
  )
  target_link_libraries(fm_ui PUBLIC imgui_test_engine_lib)
  target_compile_definitions(fm_ui PUBLIC IMGUI_ENABLE_TEST_ENGINE)
endif()

target_include_directories(fm_ui PUBLIC
  ${PROJECT_SOURCE_DIR}/src/gui
  ${PROJECT_SOURCE_DIR}/src/gui/scenes
)
target_include_directories(fm_ui SYSTEM PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(fm_ui PUBLIC fm_core SDL3_ttf::SDL3_ttf)
if(DEFINED sdl3_ttf_SOURCE_DIR)
  target_include_directories(fm_ui SYSTEM PUBLIC ${sdl3_ttf_SOURCE_DIR}/include)
endif()

# -----------------------------
# Project diagnostics and build-mode flags
# -----------------------------
foreach(target fm_core fm_ui)
  target_compile_options(${target} PRIVATE ${FM_WARNING_FLAGS})
  if(FM_ENABLE_CLANG_TIDY)
    set_property(TARGET ${target} PROPERTY CXX_CLANG_TIDY
      "${CLANG_TIDY_EXECUTABLE};--header-filter=^${PROJECT_SOURCE_DIR}/src/;-p=${CMAKE_BINARY_DIR}")
  endif()
endforeach()

if(FM_ENABLE_SANITIZERS)
  foreach(target fm_core fm_ui)
    target_compile_options(${target} PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PUBLIC -fsanitize=address,undefined)
  endforeach()
endif()

# Seeded simulation results must not depend on the platform: no fused
# multiply-add contraction (Clang contracts by default, e.g. on Apple
# silicon). MSVC's default /fp:precise does not contract.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND
   NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  target_compile_options(fm_core PRIVATE -ffp-contract=off)
endif()

if(FM_ENABLE_NATIVE_OPTIMIZATIONS)
  foreach(target fm_core fm_ui)
    target_compile_options(${target} PRIVATE -march=native)
  endforeach()
endif()

# -----------------------------
# Executable
# -----------------------------
add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE fm_ui)
target_compile_options(${PROJECT_NAME} PRIVATE ${FM_WARNING_FLAGS})

if(FM_ENABLE_CLANG_TIDY)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_CLANG_TIDY
    "${CLANG_TIDY_EXECUTABLE};--header-filter=^${PROJECT_SOURCE_DIR}/src/;-p=${CMAKE_BINARY_DIR}")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(fm_core PRIVATE DEBUG)
  target_compile_definitions(fm_ui PRIVATE DEBUG)
  target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_definitions(fm_core PRIVATE NDEBUG)
  target_compile_definitions(fm_ui PRIVATE NDEBUG)
  target_compile_definitions(${PROJECT_NAME} PRIVATE NDEBUG)
endif()

# -----------------------------
# Balance lab: headless batch simulation and calibration reports (fm_lab).
# Links fm_core only (no window, no ImGui).
# -----------------------------
add_library(fm_lab_lib STATIC
    tools/lab_stats.h
    tools/lab_stats.cpp
    tools/lab_report.h
    tools/lab_report.cpp
    tools/lab_fixtures.h
    tools/lab_fixtures.cpp
    tools/lab_runner.h
    tools/lab_runner.cpp
    tools/lab_season.h
    tools/lab_season.cpp
)
target_link_libraries(fm_lab_lib PUBLIC fm_core)
target_compile_options(fm_lab_lib PRIVATE ${FM_WARNING_FLAGS})

add_executable(fm_lab tools/lab_main.cpp)
target_link_libraries(fm_lab PRIVATE fm_lab_lib)
target_compile_options(fm_lab PRIVATE ${FM_WARNING_FLAGS})
