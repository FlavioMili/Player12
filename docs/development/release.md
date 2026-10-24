# Building and releasing desktop packages

Three workflows in `.github/workflows/` build the game:

| Workflow      | Trigger                          | What it does                                                        |
|---------------|----------------------------------|---------------------------------------------------------------------|
| `ci.yml`      | push to any branch, pull request | Runs `build.yml` (artifacts kept 3 days) plus Linux ASan/UBSan; successful main pushes publish development pre-releases |
| `release.yml` | tag `v*`, manual run             | Builds and smoke-tests packages, then publishes a GitHub Release with checksums    |
| `build.yml`   | called by the two above          | Release build, headless tests, packaging, package smoke test        |

`deploy_docs.yml` (Doxygen site) is unrelated; it publishes the API docs
and the public project, player and contributor guides.

## Development builds for main commits

Every successful push to `main` publishes a development pre-release using the
packages already produced by CI. Publication waits for all platform package
jobs and the sanitizer job. Pushes to other branches and pull requests keep
workflow artifacts only.

For source commit `0123456789abcdef...` with project version `1.0.0`, packages
report `1.0.0-dev.sha0123456789ab`. The GitHub release tag is `dev-<full commit
SHA>`, so each pushed main revision has a separate download page. A rerun
updates the same release's assets. When one push contains multiple commits,
the release belongs to that push's head commit.

Each pre-release includes the Linux tarball and AppImage, macOS ARM64 ZIP,
Windows ZIP, and `SHA256SUMS.txt`. It is explicitly excluded from GitHub's
Latest designation; official `v*` releases continue through `release.yml`.
Main runs use separate concurrency groups for each source commit, so a newer
push does not cancel the previous commit's build or publication. These
releases use the workflow's scoped `GITHUB_TOKEN`; no extra secret or duplicate
platform build is needed.

## Cutting a release

1. Make sure CI is green on the commit you want to ship, and run the full
   test suite locally (CI skips the slow labels, see below).
2. Check the version: `project(FootballManagement VERSION x.y.z)` in
   `CMakeLists.txt` is the one place it is set. Raise it there for a new
   version.
3. In `CHANGELOG.md`, replace "(unreleased)" in the heading of the version
   you are shipping with the release date and commit that change. The
   release notes are taken from that section, so check that it reads well
   on its own (`packaging/release_notes.sh 1.0.0` prints it).
4. Tag it and push the tag. The tag's numbers must equal the project
   version (`v1.0.0` for 1.0.0, or a pre-release such as `v1.0.0-rc1`);
   otherwise every build job stops at its first step
   (`packaging/release_version.sh`):

   ```sh
   git tag -a v1.0.0 -m "Player12 1.0.0"
   git push origin v1.0.0
   ```

5. `release.yml` builds every platform. When it finishes, the release
   `v1.0.0` appears on GitHub with:
   - `Player12-1.0.0-linux-x86_64.tar.gz`
   - `Player12-1.0.0-linux-x86_64.AppImage`
   - `Player12-1.0.0-macos-arm64.zip`
   - `Player12-1.0.0-windows-x86_64.zip`
   - `SHA256SUMS.txt` (check with `sha256sum -c SHA256SUMS.txt`)

   A tag with a hyphen (`v1.0.0-rc1`) becomes a pre-release. The release
   notes are the version's `CHANGELOG.md` section (a pre-release uses the
   section of its final version) followed by a line about the checksums;
   they can be edited on GitHub afterwards. Re-running the workflow for an
   existing release replaces its files.

The entire release process can also run from the GitHub CLI after the workflow
changes have been pushed. No GitHub Packages registry or extra publishing secret
is needed: the workflow uses its scoped `GITHUB_TOKEN` to upload release files.

To build packages without publishing, run:

```sh
gh workflow run release.yml --ref main -f version=1.0.0 -f publish=false
gh run list --workflow release.yml
```

Use `-f publish=true` to create a **draft** release at the selected commit once
all platform builds succeed. Inspect its downloads before publishing:

```sh
gh release view v1.0.0
gh release download v1.0.0 --dir /tmp/player12-release-review
gh release edit v1.0.0 --draft=false
```

The last command makes the draft public; run it only after reviewing the packages.
Manual builds also upload workflow artifacts, kept for 30 days. GitHub Releases
hosts the ready-to-play game archives; GitHub Packages is for supported package
registries such as npm, NuGet and containers.

The version is the project version from `CMakeLists.txt`. A tagged build
passes the tag (without the `v`, after `release_version.sh` has checked it)
to CMake as `-DFM_VERSION=...`, so a pre-release suffix such as `-rc1` is
kept. The version ends up in the game (About screen, crash reports, saves),
the package names, the macOS `Info.plist` and the Linux desktop entry
(`X-AppImage-Version`). Local builds report the project version and the
commit they were built from.

## What each job does

All jobs build `Release` with pinned FetchContent dependencies and cache the
fetched sources (`build/_deps/*-src`). CMake subbuilds contain absolute paths and are never
cached; source-cache keys also include the repository name. Linux and macOS also
cache compiler output with ccache. Packages use two extra CMake options so
they carry no library dependencies beyond the OS:
`-DSDLTTF_VENDORED=ON` (FreeType, HarfBuzz and PlutoSVG built in) and
`-DCMAKE_DISABLE_FIND_PACKAGE_SQLite3=ON` (static SQLite from
`sjinks/sqlite3-cmake`). SDL3 is already built from source as a static
library.

CI tests run headless (`SDL_VIDEO_DRIVER=dummy`, `SDL_AUDIO_DRIVER=dummy`) with
`ctest -LE "playtest|monkey|slow"`. That also skips the `adversarial`, `qa`,
`perf` and `soak` suites, which are labelled `slow`. The sanitizer job in
`ci.yml` (Debug, ASan and UBSan) runs only the headless core tests and the
sanitizer probe (`-L "core|sanitizer"`), because the GUI suites are too slow
under the sanitizers. Release jobs build the game and balance lab without test executables and run
the package smoke checks on every platform. The full test suites remain in
`ci.yml`; require green CI on the source commit before tagging.
Run the full suite locally before a release.

**Linux** (`ubuntu-24.04`, GCC 14): links libstdc++ and libgcc statically,
builds and runs the tests, smoke-tests a normal install (without selecting a
component), then runs `cpack` (tar.gz). It installs the
`game` component into an AppDir and turns it into an AppImage with a pinned
`linuxdeploy` release (checked against its SHA-256). Runners have no FUSE,
so the AppImage is smoke-tested by extracting it (`--appimage-extract`),
checking the version in its desktop entry and running the smoke test on
its `AppRun`.

**macOS** (`macos-15`, Apple silicon): AppleClang's libc++ cannot build the
game. The code uses floating-point `std::from_chars` (player and training
repositories) and `std::jthread`/`std::stop_token` (thread pool), and libc++
only provides both without `-fexperimental-library` from LLVM 20. So the job
uses Homebrew `llvm@20` and links its `libc++.a`/`libc++abi.a` statically
(`-nostdlib++`). The executable exports no symbols (`-no_exported_symbols`),
so its C++ runtime cannot replace symbols in the system runtime used by Cocoa.
A check step fails the build if the game links anything
outside `/usr/lib` or `/System/Library`. CMake builds
`Player12.app`, installs the assets into `Contents/Resources`,
ad-hoc signs the bundle as the last install step and zips it (`cpack` ZIP).
The job verifies the signature after extracting the zip.

**Windows** (`windows-2022`, Visual Studio generator with the ClangCL
toolset): builds the game and `fm_lab` with `BUILD_TESTING=OFF`, packs a
zip. The executable embeds `packaging/windows/footballmanagement.manifest`,
which sets the UTF-8 code page so that paths and names with non-ASCII
characters work through the narrow Windows APIs. The game and fetched
libraries use the static MSVC runtime (`CMAKE_MSVC_RUNTIME_LIBRARY`), so
players do not need to install a separate VC++ redistributable. CI checks
PE imports for accidental dynamic runtime dependencies before launching the
extracted game. Every platform is required: a failed Windows build blocks
release publication just like Linux and macOS.

Windows players download the ZIP from the release, extract the entire folder,
and double-click `Player12.exe`; `assets/`, `licenses/` and `README.txt` ship
beside it. The `package-windows-x86_64` Actions artifact also contains this ZIP
for test builds; an artifact is not automatically a published GitHub Release.

**Smoke test** (all platforms, `packaging/smoke_test.sh`): extracts the
package to a temporary directory and starts the game headless from another
working directory for 10 s. On Linux and macOS it sends SIGTERM, which SDL
turns into a quit event. The script expects a clean exit and a
`Game data root:` log line pointing inside the package. On every platform it
checks that the log file appeared in the user-data directory and that no file
inside the package was created, changed or removed. You can run it locally
on any extracted package:

```sh
# Replace this with your extracted Linux package directory.
package_dir=/path/to/extracted-package
XDG_DATA_HOME=/tmp/fm-xdg packaging/smoke_test.sh \
  "$package_dir" "$package_dir/bin/Player12" \
  /tmp/fm-xdg/FlavioMili/FootballManagement
```

## Package layout and runtime paths

Installing and packaging live in `cmake/Packaging.cmake`. Only the `game`
install component is packaged, so the install rules of fetched dependencies
never add headers or static libraries.

| Platform | Executable                                   | Read-only game data                              |
|----------|----------------------------------------------|--------------------------------------------------|
| Linux    | `bin/Player12`                     | `share/footballmanagement/assets`                |
| macOS    | `Player12.app/Contents/MacOS/...`  | `Player12.app/Contents/Resources/assets` |
| Windows  | `Player12.exe`                     | `assets` next to the executable                  |

Linux packages also ship `share/applications/footballmanagement.desktop`
(generated from `packaging/linux/footballmanagement.desktop.in` with the
version) and a scalable icon. Licence files go to
`share/doc/footballmanagement` on Linux and to `licenses/` in the macOS and
Windows zips: the repository's `LICENSE` file and README, and `third_party/`
notices for fmt, spdlog, nlohmann/json, Dear ImGui, SDL3, SDL3_ttf,
FreeType, HarfBuzz, PlutoSVG/PlutoVG, SQLite (public domain) and the Roboto
font (Apache-2.0). The game's source code is licensed under the GNU GPL v3;
include the repository's `LICENSE` with every release. Check the notices before
tagging.

The game also carries the third-party licence texts as data in
`assets/licenses/` (one file per component). The *About* screen (from the
main menu, or the command palette in a career) shows the version, the commit and build details, and lists
these components with their licences, so they are readable in any build,
packaged or not.

`RuntimePaths::assetRoot()` finds the game data once per process. It uses
`FM_ASSET_ROOT` if set. Otherwise it takes the first directory holding
`assets/db/schema.sql` among `<exe>/../share/footballmanagement`,
`<exe>/../Resources` and `<exe>` (SDL's base path; inside a macOS bundle
that is already `Contents/Resources`). The fallback is the source checkout
baked in at configure time (`FM_SOURCE_DIR`), so development builds keep
working from `build/`. Code reads data through `AssetPaths::*()` in the
generated `global/paths.h`.

Writable files never go next to the executable. Saves, settings, logs,
crash reports, `imgui.ini` and captures use `RuntimePaths::root()`, which is
`SDL_GetPrefPath("FlavioMili", "FootballManagement")`:

- Linux: `$XDG_DATA_HOME/FlavioMili/FootballManagement` (default
  `~/.local/share/...`)
- macOS: `~/Library/Application Support/FlavioMili/FootballManagement`
- Windows: `%APPDATA%\FlavioMili\FootballManagement`

### Crash reports and save recovery

If the game dies on a fatal signal, an uncaught exception or
`std::terminate`, it writes `crash-<date>.txt` to `<user data>/logs` with
the version, the commit, the platform and the reason, and keeps the log of
that session next to it (`crash-<date>.log`). At the next start a notice
offers to open the folder; only the newest reports are kept. Ask testers
for these two files when they report a crash.

When a save fails to load, the load screen offers to restore the newest
numbered backup that loads; the damaged file is kept aside, never deleted.

## Building a package locally (Linux)

Use a separate build tree:

```sh
cmake -S . -B /tmp/fm-pkg -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DSDLTTF_VENDORED=ON -DCMAKE_DISABLE_FIND_PACKAGE_SQLite3=ON \
  "-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc"
cmake --build /tmp/fm-pkg --parallel 2 --target FootballManagement
cpack --config /tmp/fm-pkg/CPackConfig.cmake -B /tmp/fm-pkg/dist
```

## Known limitations

- **macOS packages are not notarized or signed with a Developer ID** (only
  ad-hoc signed). The first launch is blocked by Gatekeeper. On macOS 15,
  open the app once, then go to *System Settings > Privacy & Security* and
  click *Open Anyway*. Alternatively run
  `xattr -dr com.apple.quarantine Player12.app`. On older systems,
  right-click the app and choose *Open*. Real signing needs an Apple
  Developer account and `codesign`/`notarytool` secrets in the workflow.
- **macOS needs 15.0 or later on Apple silicon.** The static libc++ comes
  from Homebrew bottles built for the runner's macOS, so the deployment
  target is 15.0. There is no Intel or universal build: Homebrew's
  `libc++.a` is single-architecture.
- **The macOS package has no application icon** (it uses the generic one) and
  is a zip, not a DMG (`hdiutil` is unreliable on hosted runners).
- **Linux packages need glibc 2.39 or later** (the Ubuntu 24.04 baseline).
  libstdc++ is static, and SDL loads X11/Wayland/audio libraries at runtime
  from the host. The AppImage needs FUSE 2 to run directly. Without it, run
  it with `--appimage-extract-and-run`.
- **Windows is experimental.** The tests are not built on Windows because
  several test files use `unistd.h` and `setenv`/`unsetenv`. No Windows
  build has been checked yet. Possible problems include
  `std::filesystem::path` to `std::string` conversions (implicit only on
  POSIX) and warning flood: clang-cl
  treats the GCC-style `-Wall` as `-Weverything`. The GUI runs with the
  Windows subsystem (no console window), so check the log file in
  `%APPDATA%` (and any crash report there) for output.
- On macOS, every build (including development builds) produces
  `build/src/Player12.app` instead of a plain executable. Pre-release
  versions such as `1.0.0-rc1` go into `CFBundleShortVersionString` as-is. That
  is not Apple's `x.y.z` format, which only matters for App Store submission.
- `linuxdeploy` is pinned to the `1-alpha-20251107-1` release. Homebrew
  `llvm@20` fixes the macOS compiler major; patch releases follow Homebrew.
