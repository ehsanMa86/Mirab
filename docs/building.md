# Building and packaging

## Requirements

- CMake 3.21 or newer and Ninja (or another generator)
- a C++17 compiler: GCC 9+, Clang 12+, MSVC 2019+ or MinGW-w64 GCC 11+
- Qt 6.5 or newer (Widgets) for the desktop application, with Qt Linguist tools (`LinguistTools`, part of the standard Qt installation) for the translations; without them the application is built in English only
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
sudo apt install cmake ninja-build g++ qt6-base-dev qt6-tools-dev qt6-l10n-tools qt6-translations-l10n libgl1-mesa-dev   # Qt 6.5+ (Ubuntu 24.10 or newer)
cmake --preset release
cmake --build --preset release --target package
```

This produces `Mirab-<version>-linux-x86_64.tar.gz`. The release workflow also builds an AppImage with linuxdeploy (see `.github/workflows/release.yml`).

## Translations

The `.ts` files in `translations/` are compiled into the application together with Qt's own translations of the same languages (`qtbase_<lang>.qm` from the Qt installation), so the packages need no extra files and the deploy steps run with `NO_TRANSLATIONS`. `cmake --build <dir> --target update_translations` refreshes the `.ts` files from the sources and `--target update_core_strings` the list of library texts they include; see [translations/README.md](../translations/README.md).

## Continuous integration

- `.github/workflows/ci.yml` builds and tests on Windows, macOS and Linux for every push and pull request, and checks license headers.
- `.github/workflows/release.yml` runs on version tags (`v*`) and attaches the Windows installer and ZIP, the macOS DMGs, and the Linux AppImage and tarball to a GitHub release, together with `Mirab-<version>-SHA256SUMS.txt` and a signed build provenance attestation for every file. Releases are immutable once published; a tag with a hyphen (`v0.3.0-rc1`) therefore becomes a draft prerelease, to be published by hand after checking it. Running the workflow by hand (*Actions > Release > Run workflow*) builds every package without publishing anything.
- `.github/workflows/pages.yml` builds the website and publishes it on GitHub Pages when a change to the documentation reaches `main`; for pull requests it only checks that the site builds. See [Website](#website).
- Every action is pinned to a commit, and the linuxdeploy tools to a release and checksum. Dependabot proposes action updates once a month; to move linuxdeploy, change the tags and checksums at the top of `release.yml`.

## Website

The website at <https://ehsanma86.github.io/Mirab/> is built with [MkDocs](https://www.mkdocs.org) and the Material theme from the pages in `docs/`, so the documentation is written once and reads the same on GitHub and on the website. `mkdocs.yml` holds the navigation and the address of the site; `docs/index.md` and `docs/download.md` are the home and download pages, and the download buttons find the files of the latest release when the page opens. The changelog and the [SWMM5+ C interface reference](https://github.com/ehsanMa86/swmm5plus-capi/blob/main/docs/api.md) are included at build time, the latter from a checkout of swmm5plus-capi next to this repository. Links from `docs/` to other files of the repository open them on GitHub.

To preview the site, with swmm5plus-capi cloned next to Mirab:

```
python -m venv .venv
.venv/bin/pip install -r tools/website-requirements.txt      # .venv\Scripts\pip on Windows
.venv/bin/mkdocs serve                                       # http://127.0.0.1:8000/Mirab/
```

`mkdocs build --strict` fails on broken links and anchors, as the workflow does. The workflow publishes the site from `main`, so it always describes the latest release; run it by hand (*Actions > Website > Run workflow* on `main`) after a change to the C interface reference. Publishing needs *Settings > Pages > Source: GitHub Actions* once.

## License headers

New files start with a marker line such as `// @mirab-header What this file does.` Run `python tools/license_headers.py --apply` to expand it and `--check` to verify all files.
