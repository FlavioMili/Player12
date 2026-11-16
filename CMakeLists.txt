cmake_minimum_required(VERSION 3.29)
project(FootballManagement VERSION 1.0.0 LANGUAGES CXX C)

# The game version: the project version unless a release build passes its tag
# (without the leading v, e.g. -DFM_VERSION=1.0.0-rc1). "0.0.0" was the old
# default and still means "not set" in existing build trees.
set(FM_VERSION "" CACHE STRING
  "Version stamped into the game and its packages; empty for the project version")
if(FM_VERSION STREQUAL "" OR FM_VERSION STREQUAL "0.0.0")
  set(FM_GAME_VERSION "${PROJECT_VERSION}")
else()
  set(FM_GAME_VERSION "${FM_VERSION}")
endif()

# Commit shown in crash reports; CI passes it, otherwise ask git.
set(FM_BUILD_COMMIT "" CACHE STRING "Commit recorded in the build (default: git)")
if(FM_BUILD_COMMIT STREQUAL "")
  find_package(Git QUIET)
  if(Git_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
      OUTPUT_VARIABLE FM_BUILD_COMMIT
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
  endif()
  if(FM_BUILD_COMMIT STREQUAL "")
    set(FM_BUILD_COMMIT "unknown")
  endif()
endif()

# Keep newer CMake policy behavior in dependencies without raising the minimum
# above the version required by this project's TEST_LAUNCHER usage.
set(CMAKE_POLICY_DEFAULT_CMP0219 NEW)

# -----------------------------
# C++ Standard
# -----------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# No C++20 modules are used; module scanning only slows builds and adds
# GCC-only flags to compile_commands.json that clangd cannot parse.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS OFF)

if(MSVC)
  # A portable Windows zip must run without a separately installed VC++
  # redistributable. Apply the static CRT consistently to project/dependencies.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
      CACHE STRING "MSVC runtime library for portable Windows packages")
endif()

option(BUILD_TESTING "Build tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)
option(BUILD_UI_AUTOMATION "Build UI automation support" OFF)
option(FM_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(FM_ENABLE_CLANG_TIDY "Run clang-tidy while compiling project targets" OFF)
option(FM_ENABLE_NATIVE_OPTIMIZATIONS "Tune local profiling builds for this CPU" OFF)
option(FM_COPY_COMPILE_COMMANDS "Copy compile_commands.json into the source tree" OFF)
option(FM_USE_SYSTEM_SDL "Use exact-version system SDL packages instead of pinned sources" OFF)

if(FM_ENABLE_SANITIZERS AND NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  message(FATAL_ERROR "FM_ENABLE_SANITIZERS requires GCC or Clang")
endif()
if(FM_ENABLE_NATIVE_OPTIMIZATIONS AND NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  message(FATAL_ERROR "FM_ENABLE_NATIVE_OPTIMIZATIONS requires GCC or Clang")
endif()

if(FM_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
endif()

# -----------------------------
# Build types
# -----------------------------
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# -----------------------------
# Compiler diagnostics
# -----------------------------
set(FM_WARNING_FLAGS)
# clang-cl (Clang with the MSVC command line) takes the MSVC flags: its -Wall
# would mean -Weverything.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND
   NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  list(APPEND FM_WARNING_FLAGS
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
    -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual
    -Wconversion -Wsign-conversion -Wmisleading-indentation
    -Wnull-dereference -Wdouble-promotion -Wformat=2
    -Wimplicit-fallthrough
  )
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND FM_WARNING_FLAGS
      -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wuseless-cast)
  endif()
elseif(MSVC)
  list(APPEND FM_WARNING_FLAGS /W4 /permissive-)
endif()
if(MSVC)
  # Sources and string literals are UTF-8 (e.g. the "·" separators).
  add_compile_options(/utf-8)
endif()

# -----------------------------
# Dependencies (FetchContent & find_package)
# -----------------------------
include(FetchContent)

# fmt
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG        11.2.0
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(fmt)

# Use the same fmt release for logging and the game. Older bundled fmt
# releases fail constant evaluation with current Clang.
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)

# spdlog
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.15.3
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)

# nlohmann_json
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(json)

# --- SQLite3 ---
find_package(SQLite3 3.37 QUIET)
if(NOT SQLite3_FOUND)
  message(STATUS "SQLite3 not found, fetching from GitHub...")
  FetchContent_Declare(
    sqlite3
    GIT_REPOSITORY https://github.com/sjinks/sqlite3-cmake.git
    GIT_TAG        v3.45.1
  )
  FetchContent_MakeAvailable(sqlite3)
  if(NOT TARGET SQLite3::SQLite3)
    add_library(SQLite3::SQLite3 ALIAS SQLite3)
  endif()
else()
  # CMake < 3.30 defines SQLite::SQLite3 but not SQLite3::SQLite3
  if(TARGET SQLite::SQLite3 AND NOT TARGET SQLite3::SQLite3)
    add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
  endif()
endif()

# --- SDL3 / SDL3_ttf ---
# Pinned source builds are the reproducible default. Distro packages are an
# explicit opt-in and must report the exact compatible versions.
if(FM_USE_SYSTEM_SDL)
  find_package(SDL3 3.4.16 EXACT REQUIRED)
  find_package(SDL3_ttf 3.2.2 EXACT REQUIRED)
else()
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        fa2c02bb6e21974a89ea9824bc53c9932abe5f9c # release-3.4.16
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(SDL3)

  set(SDL3TTF_SAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    SDL3_ttf
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
    GIT_TAG        a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b # release-3.2.2
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(SDL3_ttf)
  if(APPLE AND TARGET harfbuzz)
    # SDL_ttf 3.2.2 forces the optional CoreText backend on, overriding a cached
    # HB_HAVE_CORETEXT=OFF. Disable its actual compiler definition after setup:
    # LLVM rejects the old SDK headers it includes. FreeType and HarfBuzz's
    # OpenType shaping remain enabled for the game's shipped fonts.
    get_target_property(FM_HARFBUZZ_SOURCE_DIR harfbuzz SOURCE_DIR)
    get_directory_property(FM_HARFBUZZ_DEFINITIONS
      DIRECTORY "${FM_HARFBUZZ_SOURCE_DIR}" COMPILE_DEFINITIONS)
    list(REMOVE_ITEM FM_HARFBUZZ_DEFINITIONS HAVE_CORETEXT)
    set_property(DIRECTORY "${FM_HARFBUZZ_SOURCE_DIR}"
      PROPERTY COMPILE_DEFINITIONS "${FM_HARFBUZZ_DEFINITIONS}")
    set(HB_HAVE_CORETEXT OFF CACHE BOOL "Unused HarfBuzz CoreText backend" FORCE)
  endif()
endif()

# --- ImGui ---
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        e8602501bb900fc7c965e65b88fef30b2c80aff1
)
FetchContent_MakeAvailable(imgui)

# --- ImGui Test Engine ---
if(BUILD_UI_AUTOMATION)
  FetchContent_Declare(
    imgui_test_engine
    GIT_REPOSITORY https://github.com/ocornut/imgui_test_engine.git
    GIT_TAG        cf4b9749fad4cfdf2096a27f9a688ea71c47fe49
  )
  FetchContent_MakeAvailable(imgui_test_engine)
endif()


# GoogleTest (for tests)
if(BUILD_TESTING)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(googletest)
endif()

# Google Benchmark
if(BUILD_BENCHMARKS)
  FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.8.3
    GIT_SHALLOW    TRUE
  )
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark tests" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "Disable benchmark install" FORCE)
  FetchContent_MakeAvailable(benchmark)
endif()
# -----------------------------
# Subdirectories
# -----------------------------
add_subdirectory(src)
include(cmake/Packaging.cmake)

if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(test)
endif()

if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()


# -----------------------------
# Tools & Formatting
# -----------------------------

# Copying this generated file into the checkout is opt-in.
if(CMAKE_EXPORT_COMPILE_COMMANDS AND FM_COPY_COMPILE_COMMANDS)
  add_custom_target(copy_compile_commands
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${CMAKE_BINARY_DIR}/compile_commands.json
      ${CMAKE_SOURCE_DIR}/compile_commands.json
    COMMENT "Copying compile_commands.json to root"
  )
endif()

find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
  file(GLOB_RECURSE FORMAT_FILES
    src/*.cpp
    src/*.h
    src/*.hpp
    test/*.cpp
  )
  add_custom_target(format
    COMMAND ${CLANG_FORMAT} -i ${FORMAT_FILES}
    COMMENT "Running clang-format"
  )
endif()

find_program(CPPCHECK cppcheck)
if(CPPCHECK)
  add_custom_target(cppcheck
    COMMAND ${CPPCHECK}
    --enable=warning,performance,portability
    --std=c++23
    --language=c++
    --inline-suppr
    --quiet
    src
  )
endif()

# -----------------------------
# Documentation (Doxygen)
# -----------------------------
find_package(Doxygen)
if (DOXYGEN_FOUND)
  # Create a custom target that runs doxygen using our highly configured Doxyfile
  add_custom_target(docs
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating API documentation with Doxygen and Catppuccin theme"
    VERBATIM
  )
endif()
