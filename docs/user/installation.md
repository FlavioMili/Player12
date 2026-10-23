# Install and run

## Download and play

Download the package for your system from
[GitHub Releases](https://github.com/FlavioMili/FootballManagement/releases).
Release publication requires the Linux, macOS and Windows package checks to pass.

- **Windows 10/11, 64-bit:** extract the whole `Player12-*-windows-x86_64.zip`
  folder and double-click `Player12.exe`. Keep `assets/` and `licenses/` beside
  it. No compiler or separate VC++ redistributable is needed.
- **Linux, 64-bit:** mark the AppImage executable and run it, or extract the
  tarball and run `bin/Player12`.
- **macOS 15+, Apple silicon:** extract the ARM64 ZIP and open `Player12.app`.
  The app is ad-hoc signed; if Gatekeeper blocks the first launch, open it once
  and use *System Settings > Privacy & Security > Open Anyway*.

If there is no release yet, a successful
[Actions build](https://github.com/FlavioMili/FootballManagement/actions)
provides `package-<platform>` artifacts while they are retained. Sign in to
GitHub to download an artifact, extract it, then extract the game ZIP inside.

## Build from source

The game builds with CMake 3.29 or newer and a C++23 compiler. The first
configure needs Git and network access: SDL3, SDL3_ttf, Dear ImGui, fmt,
spdlog and nlohmann/json (and GoogleTest when tests are enabled) are fetched
at pinned versions. SQLite comes from the system if CMake finds it and is
fetched otherwise.

CI builds Linux with GCC 14, macOS with Homebrew LLVM and Windows with
Visual Studio 2022 and clang-cl. All three platform packages must pass CI; Windows has had less
manual playtesting than Linux.

### Linux

SDL is built from source, so you need its build dependencies. On Ubuntu
24.04 or Debian:

```sh
sudo apt install git g++-14 ninja-build pkg-config \
  libfreetype-dev libharfbuzz-dev libsqlite3-dev \
  libasound2-dev libpulse-dev libpipewire-0.3-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev \
  libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
  libdrm-dev libgbm-dev libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev \
  libwayland-dev libdecor-0-dev libdbus-1-dev libudev-dev
```

Ubuntu 24.04 ships CMake 3.28, which is too old: install a newer one, for
example with `pip install cmake` or from [cmake.org](https://cmake.org).
It also defaults to GCC 13, so select GCC 14 before the first configure.
Any newer GCC works too. The complete package list CI uses is in
[`.github/workflows/build.yml`](../../.github/workflows/build.yml).

```sh
git clone https://github.com/FlavioMili/FootballManagement
cd FootballManagement
CC=gcc-14 CXX=g++-14 cmake --preset release
cmake --build --preset release --parallel 4
./build/release/src/Player12
```

If CMake cannot find FreeType or HarfBuzz, add `-DSDLTTF_VENDORED=ON` to the
configure command to build them as well.

### macOS

macOS 15 on Apple silicon is the supported setup. AppleClang's standard
library is missing parts of C++23 that the game uses, so build with
Homebrew's LLVM and link its libc++ statically, as CI does:

```sh
git clone https://github.com/FlavioMili/FootballManagement.git
cd FootballManagement
brew install cmake ninja llvm
LLVM="$(brew --prefix llvm)"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DCMAKE_C_COMPILER="$LLVM/bin/clang" \
  -DCMAKE_CXX_COMPILER="$LLVM/bin/clang++" \
  "-DCMAKE_EXE_LINKER_FLAGS=-nostdlib++ $LLVM/lib/c++/libc++.a $LLVM/lib/c++/libc++abi.a"
cmake --build build --parallel 4
open build/src/Player12.app
```

### Windows

Install Visual Studio 2022 with the *Desktop development with C++* workload
and its *C++ Clang tools for Windows* component (clang-cl), CMake 3.29 or
newer and Git.
From a Developer PowerShell:

```powershell
git clone https://github.com/FlavioMili/FootballManagement
cd FootballManagement
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -T ClangCL `
  -DBUILD_TESTING=OFF -DSDLTTF_VENDORED=ON
cmake --build build --config Release --target FootballManagement
.\build\src\Release\Player12.exe
```

The test suites use POSIX APIs, so tests are not built on Windows.

### Other build options

The presets in [`CMakePresets.json`](../../CMakePresets.json) are `release`,
`release-tests`, `debug-sanitized` (AddressSanitizer and UBSan), `profile`
and `clang-tidy`; their build trees go to `build/<preset>`. A plain
build directory works as well (`cmake -S . -B build -G Ninja`). See
[docs/development/builds.md](../development/builds.md) for the details and
[docs/development/release.md](../development/release.md) for packaging.

## Running

Start the executable from the build tree (see the commands above). A
development build reads the game data from the source checkout, so it runs
from any working directory; `FM_ASSET_ROOT` points it at another directory
containing `assets/`. Installed packages use the data next to the executable.

To install a Linux build for your user (including all game assets):

```sh
cmake --preset release
cmake --build --preset release --parallel 2
cmake --install build/release --component game --prefix "$HOME/.local"
"$HOME/.local/bin/Player12"
```

For a Makefiles build, `make -C build install` also installs the game and
assets, using the prefix chosen with `-DCMAKE_INSTALL_PREFIX=...` when
configuring. Saves and settings go to your user data directory, so the
installation directory does not need to be writable during play.

`FM_WORLD_SEED` fixes the seed of a new world, which helps when you report a
bug.
