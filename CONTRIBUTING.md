# Contributing to Mirab

Thank you for helping. Bug reports, models that expose problems, documentation and code are all welcome.

## Reporting a problem

Open an issue with the bug report form. It asks for:

- the Mirab version (Help > About Mirab, or `mirab-cli version`),
- the engine you selected,
- the smallest input file that shows the problem, if you can share it,
- what you expected and what happened.

## Contributing code

1. Fork the repository and create a branch from `main`.
2. Build with `cmake --preset debug` and make sure `ctest --preset release` (or the debug equivalent) passes.
3. Start every new source file with a one-line header marker, for example
   `// @mirab-header Short description of the file.`
   and run `python tools/license_headers.py --apply`. The tool expands the marker into the standard copyright and license header. CI runs `--check` and fails when a file has no header.
4. Keep engine sources untouched. Engine differences belong in the adapters (`src/engine`) and in the capability manifest (`src/engine/src/EngineRegistry.cpp`).
5. Open a pull request that explains the change and how you tested it.

## Contributor agreement

Mirab is released under GPL-3.0-or-later and its copyright is held by Ehsan Madadi. So that the project can be offered under additional licenses in the future, contributions of code are accepted under a contributor license agreement: by submitting a pull request you agree that your contribution may be distributed under GPL-3.0-or-later and under other licenses chosen by the copyright holder, and you confirm that you have the right to grant this. Your authorship is recorded in the git history and in `AUTHORS`.

## Code style

- C++17, formatted with the `.clang-format` file in the repository.
- No exceptions across the C engine boundary; adapters translate engine error codes.
- Libraries under `src/core`, `src/engine`, `src/resilience` and `src/interop` must not depend on Qt.
