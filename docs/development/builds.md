# Reproducible builds

For your first build, start with the
[platform installation guide](../user/installation.md). For pull requests and
focused verification, see [Contributing](../../CONTRIBUTING.md) and
[Tests and tools](testing.md).

The project requires CMake 3.29 because discovered GTest cases use the
`TEST_LAUNCHER` target property. Ninja is used by the checked-in presets.
Dependencies fetched by CMake are pinned to releases or commit hashes. CI uses
the fixed Ubuntu 24.04 image and GCC 14 toolchain; local GCC 16 is also
verified. Other C++23 compilers require their own clean validation.

## Presets

Run from the source root:

```sh
cmake --preset release
cmake --build --preset release --parallel 2

cmake --preset release-tests
cmake --build --preset release-tests --parallel 2
ctest --preset release-tests

cmake --preset debug-sanitized
cmake --build --preset debug-sanitized --parallel 2
ctest --preset debug-sanitized

cmake --preset profile
cmake --build --preset profile --parallel 2
```

`release` is portable and does not use `-march=native`. `profile` is explicitly
machine-local and does. Sanitizers are enabled only by `debug-sanitized`, so a
plain Debug build does not claim diagnostics it did not run. Build trees are
placed under the ignored `build` directory; validation artifacts and
runtime data still belong in an isolated `/tmp/fm-*` root.

Tests, benchmarks, UI automation, sanitizers, clang-tidy, native optimization,
and source-tree compile-command copying are independent options. Production
builds do not fetch GTest, Benchmark, or ImGui Test Engine. `fm_core` contains
headless domain/persistence code; `fm_ui` owns Dear ImGui and scenes.

## clang-tidy

```sh
cmake --preset clang-tidy
cmake --build --preset clang-tidy --parallel 2
```

This is the strict full-project audit and can expose pre-existing lint debt.
For a bounded change, feed a zero-context source diff to the installed
`clang-tidy-diff.py`; task evidence records the exact checks used. Tidy is
attached only to project targets. Third-party include directories are
system includes and vendored dependency targets are not modified.

## Dependency updates

1. Choose released, mutually compatible SDL3 and SDL3_ttf versions and an
   ImGui/Test Engine pair.
2. Replace each `GIT_TAG` with the immutable tag commit hash. Keep the release
   name in the adjacent comment.
3. Configure clean `release`, `release-tests`, `debug-sanitized`, and
   `BUILD_UI_AUTOMATION=ON` trees; do not rely on an old FetchContent cache.
4. Build, run the full tests, inspect dependency licenses, and record the exact
   hashes and results in task evidence.

Pinned graphics/input revisions are currently SDL3 3.4.16
(`fa2c02bb...`), SDL3_ttf 3.2.2 (`a1ce3670...`), Dear ImGui
`e8602501...`, and ImGui Test Engine `cf4b9749...`. The inspected Test Engine
license is v1.04 (SHA-256 `8d01df5085b3d7c055999188bfc743354b00658be309819eb0415239820b1835`);
its free-license terms cover this OSI-licensed public project, but must be
rechecked on update. The default is to build these pinned SDL sources. `FM_USE_SYSTEM_SDL=ON` is an explicit convenience
option and accepts only those exact SDL package versions.

Leak detection remains enabled in sanitizer tests. The GUI executable has a
narrow LeakSanitizer suppression for allocations reported from `libSDL3.so`;
there is no project-wide `detect_leaks=0`. The `core`/`leak-check` CTest labels
run without GUI linkage or that SDL-library suppression.
