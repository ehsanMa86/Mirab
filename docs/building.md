# Building and packaging

## Requirements

- CMake 3.21 or newer and Ninja (or another generator)
- a C++17 compiler: GCC 9+, Clang 12+, MSVC 2019+ or MinGW-w64 GCC 11+
- Qt 6.5 or newer (Widgets) for the desktop application
- Python 3 is optional; it runs the license header check and the icon generator

The first configure downloads the engine sources (pinned commits with SHA-256 checksums) and SQLite. For offline builds point CMake at local copies, for example `-DFETCHCONTENT_SOURCE_DIR_MIRAB_UPSTREAM_OWA_EPANET=/path/to/EPANET`.

## Presets

```
cmake --preset release          # GUI, engines and tests
cmake --build --preset release
ctest --preset release
cmake --preset headless         # mirab-cli and libraries only, no Qt
```

Options: `MIRAB_BUILD_GUI`, `MIRAB_BUILD_TESTS`, `MIRAB_BUILD_ENGINES`, `MIRAB_WITH_GEOPACKAGE`, `MIRAB_BUILD_EXAMPLE_PLUGINS`.

## Windows

Either Visual Studio 2022 with the Qt MSVC kit, or MinGW-w64 with the Qt MinGW kit. `tools/bootstrap-windows.ps1` installs a private toolchain (CMake, Ninja, Qt 6.8 with MinGW 13.1 and NSIS) under `%LOCALAPPDATA%\mirab-dev` without administrator rights. Then, from Git Bash:

```
source tools/env-mingw.sh
cmake --preset release && cmake --build --preset release
cmake --build --preset release --target package
```

`package` produces `Mirab-<version>-windows-x64.zip` and, when NSIS is on the PATH, the installer `Mirab-<version>-windows-x64.exe`. Qt and the compiler runtime are deployed next to `Mirab.exe` automatically.

## macOS

```
brew install cmake ninja qt
cmake --preset release -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build --preset release --target package
```

This produces `Mirab-<version>-macos-<arch>.dmg` with a self-contained `Mirab.app`. For distribution outside your own machines, sign and notarize the bundle with your Apple Developer ID.

## Linux

```
sudo apt install cmake ninja-build g++ qt6-base-dev libgl1-mesa-dev   # Qt 6.5+ (Ubuntu 24.10 or newer)
cmake --preset release
cmake --build --preset release --target package
```

This produces `Mirab-<version>-linux-x86_64.tar.gz`. The release workflow also builds an AppImage with linuxdeploy (see `.github/workflows/release.yml`).

## Continuous integration

- `.github/workflows/ci.yml` builds and tests on Windows, macOS and Linux for every push and pull request, and checks license headers.
- `.github/workflows/release.yml` runs on version tags (`v*`) and attaches the Windows installer and ZIP, the macOS DMG, and the Linux AppImage and tarball to a GitHub release.

## License headers

New files start with a marker line such as `// @mirab-header What this file does.` Run `python tools/license_headers.py --apply` to expand it and `--check` to verify all files.
