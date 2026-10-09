# Changelog

## 0.1.0 (2026-10-08)

First release.

- Desktop application (Qt 6) for Windows, macOS and Linux with map editing, property editor, input-file editor, results map coloring, time slider and charts.
- Engines: EPA EPANET 2.2, OWA EPANET 2.3, EPANET-MSX 2.0 on either EPANET engine, EPA SWMM 5.2, and SWMM5+ 1.0 through the separate swmm5plus-capi library.
- SWMM5+ installation from the application, `anahita-cli engines install|uninstall|verify|where`, and `anahita --install-engine`, with SHA-256 checks of every file.
- SWMM5+ on Windows, through the native Windows build of swmm5plus-capi.
- Capability manifests and compatibility checks; features an engine ignores are stripped automatically with a warning, features it cannot run block the run.
- Isolated solver process with progress reporting and cancellation.
- Resilience tools: Todini index, water service availability, topology, pipe criticality and fire flow.
- GeoPackage and GeoJSON export, WNTR JSON import and export, "Open in QGIS".
- `anahita-cli` for scripted runs, analyses and conversions.
- C++ plugin interface with an example plugin.
